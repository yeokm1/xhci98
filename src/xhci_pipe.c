/*
 * xhci_pipe.c - the pure half of URB dispatch (xhci_pipe.h; roadmap-hcd.md
 * task 26-A.5).
 *
 * Every encoding rule here cites the section it comes from; the xHCI ones
 * are spec revision 1.2c as transcribed in
 * docs/usb-xhci-info/xhci-data-structures.md, and the USB ones are USB 2.0
 * chapters 5 and 9, cross-checked against the Windows 2000 DDK's inc\usb100.h and
 * inc\usbdi.h where those carry the constant.
 *
 * C89, pure: IRQL any.
 */

#include "xhci_pipe.h"

/* bmRequestType (USB 2.0 9.3, Table 9-2): direction 7, type 6:5,
 * recipient 4:0, of which 1:0 are used and 4:2 reserved. */
#define XHCI_PIPE_RT_IN             0x80UL
#define XHCI_PIPE_RT_STANDARD       0x00UL
#define XHCI_PIPE_RT_CLASS          0x20UL
#define XHCI_PIPE_RT_VENDOR         0x40UL
#define XHCI_PIPE_RT_TYPE_MASK      0x60UL
#define XHCI_PIPE_RCPT_DEVICE       0UL
#define XHCI_PIPE_RCPT_INTERFACE    1UL
#define XHCI_PIPE_RCPT_ENDPOINT     2UL
#define XHCI_PIPE_RCPT_OTHER        3UL

/* Standard bRequest codes (USB 2.0 Table 9-4; inc\usb100.h lines 70-82). */
#define XHCI_PIPE_REQ_GET_STATUS        0x00UL
#define XHCI_PIPE_REQ_CLEAR_FEATURE     0x01UL
#define XHCI_PIPE_REQ_SET_FEATURE       0x03UL
#define XHCI_PIPE_REQ_SET_ADDRESS       0x05UL
#define XHCI_PIPE_REQ_GET_DESCRIPTOR    0x06UL
#define XHCI_PIPE_REQ_SET_DESCRIPTOR    0x07UL
#define XHCI_PIPE_REQ_GET_CONFIGURATION 0x08UL
#define XHCI_PIPE_REQ_SET_CONFIGURATION 0x09UL
#define XHCI_PIPE_REQ_GET_INTERFACE     0x0AUL
#define XHCI_PIPE_REQ_SET_INTERFACE     0x0BUL

/* 4.14.1.1's "reasonable initial values" for Average TRB Length, and Table
 * 6-11's note fixing control at 8. All three clear the 65-byte floor the
 * same table's note derives for a 4 KB segment. */
#define XHCI_PIPE_AVG_TRB_INTERRUPT 1024UL
#define XHCI_PIPE_AVG_TRB_BULK      3072UL
#define XHCI_PIPE_AVG_TRB_ISOCH     3072UL

/* Footnote 112 of Table 6-9: "Software should set CErr to '3' for normal
 * operations"; the same table: '0' for isochronous. */
#define XHCI_PIPE_CERR              3UL

#define XHCI_PIPE_MAX_PACKET        1024UL

/* Max ESIT Payload Lo and Hi together (6.2.3.8, LEC). */
#define XHCI_PIPE_MAX_ESIT_LEC      0x00FFFFFFUL

/* The largest isochronous interval payload the transfer path carries: one
 * page, in at most two pieces (XhciPipeIsoFragments). */
#define XHCI_PIPE_ISO_MAX_PAYLOAD   XHCI_PIPE_PAGE_SIZE
/* The most bursts one isochronous TD may carry: TBC is two bits, the count
 * minus one (xhci.h, XHCI_TRB_TBC_MAX; 4.11.2.3). */
#define XHCI_PIPE_ISO_MAX_BURSTS    4UL

ULONG XhciPipeDci(ULONG endpointAddress)
{
    ULONG number;

    number = endpointAddress & 0x0FUL;
    if (number == 0) {
        return 0;
    }
    return number * 2UL + ((endpointAddress & 0x80UL) != 0 ? 1UL : 0UL);
}

static ULONG xhciPipeWord(const UCHAR *p)
{
    return (ULONG)p[0] | ((ULONG)p[1] << 8);
}

ULONG XhciPipeFindInterface(const UCHAR *config, ULONG length,
                            ULONG interfaceNumber, ULONG alternate,
                            PXHCI_PIPE_IFACE iface)
{
    XHCI_PIPE_IFACE found;
    ULONG total;
    ULONG offset;
    ULONG bLength;
    ULONG bType;
    ULONG declared;
    ULONG dci;
    ULONG seen;
    ULONG inTarget;
    ULONG matched;

    if (config == NULL || iface == NULL) {
        return XHCI_PIPE_BAD_PARAM;
    }
    if (length < XHCI_PIPE_CONFIG_BYTES ||
        (ULONG)config[0] < XHCI_PIPE_CONFIG_BYTES ||
        (ULONG)config[1] != XHCI_PIPE_DT_CONFIGURATION) {
        return XHCI_PIPE_MALFORMED;
    }
    total = xhciPipeWord(config + 2);
    if (total < XHCI_PIPE_CONFIG_BYTES || total > length ||
        (ULONG)config[0] > total) {
        return XHCI_PIPE_MALFORMED;
    }

    declared = 0;
    seen = 0;
    inTarget = 0;
    matched = 0;
    found.EndpointCount = 0;
    offset = (ULONG)config[0];
    while (offset < total) {
        /* A one-byte tail cannot even hold bDescriptorType. */
        if (total - offset < 2UL) {
            return XHCI_PIPE_MALFORMED;
        }
        bLength = (ULONG)config[offset];
        bType = (ULONG)config[offset + 1];
        /* bLength 0 would never advance; 1 cannot hold its own type. */
        if (bLength < 2UL || bLength > total - offset) {
            return XHCI_PIPE_MALFORMED;
        }
        if (bType == XHCI_PIPE_DT_INTERFACE) {
            if (bLength < XHCI_PIPE_INTERFACE_BYTES) {
                return XHCI_PIPE_MALFORMED;
            }
            inTarget = 0;
            if (!matched &&
                (ULONG)config[offset + 2] == interfaceNumber &&
                (ULONG)config[offset + 3] == alternate) {
                matched = 1;
                inTarget = 1;
                declared = (ULONG)config[offset + 4];
                if (declared > XHCI_PIPE_MAX_ENDPOINTS) {
                    return XHCI_PIPE_MALFORMED;
                }
                found.InterfaceOffset = offset;
                found.InterfaceNumber = interfaceNumber;
                found.AlternateSetting = alternate;
                found.InterfaceClass = (ULONG)config[offset + 5];
                found.InterfaceSubClass = (ULONG)config[offset + 6];
                found.InterfaceProtocol = (ULONG)config[offset + 7];
            }
        } else if (bType == XHCI_PIPE_DT_ENDPOINT) {
            if (bLength < XHCI_PIPE_ENDPOINT_BYTES) {
                return XHCI_PIPE_MALFORMED;
            }
            if (inTarget) {
                if (found.EndpointCount >= declared) {
                    return XHCI_PIPE_MALFORMED;
                }
                dci = XhciPipeDci((ULONG)config[offset + 2]);
                if (dci == 0 || (seen & XHCI_PIPE_DCI_BIT(dci)) != 0) {
                    return XHCI_PIPE_MALFORMED;
                }
                seen |= XHCI_PIPE_DCI_BIT(dci);
                found.EndpointOffset[found.EndpointCount] = offset;
                found.EndpointCount++;
            }
        }
        offset += bLength;
    }

    if (!matched) {
        return XHCI_PIPE_NOT_FOUND;
    }
    if (found.EndpointCount != declared) {
        return XHCI_PIPE_MALFORMED;
    }
    *iface = found;
    return XHCI_PIPE_OK;
}

/* floor(log2(value)) for value >= 1. */
static ULONG xhciPipeLog2(ULONG value)
{
    ULONG n;

    n = 0;
    while (value > 1UL) {
        value >>= 1;
        n++;
    }
    return n;
}

/*
 * A zero-bandwidth isochronous endpoint: wMaxPacketSize 0, what an audio or
 * video interface's alternate 0 typically declares. Accepted, so the
 * SELECT_CONFIGURATION or SELECT_INTERFACE naming one succeeds (owner's
 * ruling, 2026-10-04). Which sizes are legal:
 *
 *   - SuperSpeed: USB 3.2 Table 9-26 (USB 3.2 p.365) lets an isochronous
 *     endpoint with bMaxBurst 0 have "any value from 0 to 1024", and
 *     requires 1024 when bMaxBurst is nonzero, 1-1024 for interrupt, 1024
 *     for bulk and 512 for control. So a zero size with a nonzero burst is
 *     MALFORMED, as is any zero-size endpoint that is not isochronous. So
 *     is a nonzero wBytesPerInterval beside a zero size (Table 9-28: the
 *     bytes "this endpoint will transfer", and it can transfer none), and
 *     an SSP isochronous companion beside it (Table 9-29's companion is for
 *     an endpoint needing more than 48K per interval, USB 3.2 9.6.8,
 *     p.369).
 *   - USB 2.0: its specification is not in docs/references. The zero-size
 *     isochronous allowance (USB 2.0 5.6.3, where a default interface
 *     setting carries no isochronous payload) is taken as the same rule,
 *     and the zero-size refusal of every other type with it; at High
 *     Speed a zero size with additional transactions in 12:11 is already
 *     MALFORMED by Table 9-14's ranges (xhciPipeMpsLegal).
 *
 * How it is programmed: the Endpoint Context is built and added like any
 * other, with Max Packet Size 0 - what xHCI 1.2c 6.2.3.5 (p.419) prescribes,
 * "bits 10:0 of the USB Endpoint Descriptor wMaxPacketSize" - and Max ESIT
 * Payload 0, which by 4.14.2's "Reserved Bandwidth = Max ESIT Payload /
 * (2^Interval * 0.000125)" (p.237) reserves no periodic bandwidth; Average
 * TRB Length stays nonzero (Table 6-11). Leaving the context out of the
 * Configure Endpoint instead would make the pipe the one handle on the
 * device the xHC does not know, and every Stop Endpoint, Reset Endpoint,
 * Set TR Dequeue and Drop this bus issues by DCI would have to step round
 * it. No URB is ever made a TD on it: hcd_io.c refuses an isochronous URB
 * on such a pipe before any TRB is built, so no TD Size, TBC or chunk
 * arithmetic divides by its size. That a controller accepts Max Packet Size
 * 0 is unobserved; 6.2.3.2 (p.417) asks only that it be "within range for
 * endpoint type and the speed of the device".
 */
ULONG XhciPipeZeroBandwidth(const XHCI_PIPE_EP *ep)
{
    return ep != NULL && ep->TransferType == XHCI_PIPE_XFER_ISOCH &&
           ep->MaxPacketSize == 0;
}

/*
 * Table 6-12's bInterval range for the rows that take 2^(bInterval-1): the
 * HS interrupt and isoch row and the FS isoch row both list 1-16.
 */
static ULONG xhciPipeExponent(ULONG bInterval, PULONG clamped)
{
    if (bInterval < 1UL) {
        *clamped = 1;
        return 1UL;
    }
    if (bInterval > 16UL) {
        *clamped = 1;
        return 16UL;
    }
    return bInterval;
}

/*
 * The Max Packet Size a USB 2.0 endpoint may declare at its speed (USB 2.0
 * 5.6.3 isochronous, 5.7.3 interrupt, 5.8.3 bulk, Table 9-14 for the HS
 * high-bandwidth ranges). `mps` is already 1..1024 and `transactions` (HS
 * periodic only) 0..2. A device outside these limits declares a packet size
 * its speed does not have, and is refused rather than programmed as declared.
 * HS control (64, 5.5.3) has no row: control endpoints are refused earlier.
 */
static ULONG xhciPipeMpsLegal(ULONG speed, ULONG type, ULONG mps,
                              ULONG transactions)
{
    if (speed == XHCI_PIPE_SPEED_HIGH) {
        if (type == XHCI_PIPE_XFER_BULK) {
            return mps == 512UL;
        }
        /* Table 9-14: 1 extra transaction needs 513-1024, 2 need 683-1024. */
        if (transactions == 1UL) {
            return mps >= 513UL;
        }
        if (transactions == 2UL) {
            return mps >= 683UL;
        }
        return 1;
    }
    if (speed == XHCI_PIPE_SPEED_LOW) {
        return mps <= 8UL;
    }
    switch (type) {
    case XHCI_PIPE_XFER_BULK:
        return mps == 8UL || mps == 16UL || mps == 32UL || mps == 64UL;
    case XHCI_PIPE_XFER_INTERRUPT:
        return mps <= 64UL;
    default:
        return mps <= 1023UL;
    }
}

ULONG XhciPipeEndpointParams(const UCHAR *endpoint, ULONG speed,
                             PXHCI_PIPE_EP ep)
{
    XHCI_PIPE_EP out;
    ULONG wMaxPacketSize;
    ULONG transactions;
    ULONG periodic;
    ULONG b;

    if (endpoint == NULL || ep == NULL) {
        return XHCI_PIPE_BAD_PARAM;
    }
    if ((ULONG)endpoint[0] < XHCI_PIPE_ENDPOINT_BYTES ||
        (ULONG)endpoint[1] != XHCI_PIPE_DT_ENDPOINT) {
        return XHCI_PIPE_MALFORMED;
    }
    out.Address = (ULONG)endpoint[2];
    out.TransferType = (ULONG)endpoint[3] & 0x03UL;
    out.DirectionIn = (out.Address & 0x80UL) != 0 ? 1UL : 0UL;
    out.BInterval = (ULONG)endpoint[6];
    out.IntervalClamped = 0;
    out.CompanionMissing = 0;
    out.MaxStreams = 0;
    out.SspIso = 0;
    wMaxPacketSize = xhciPipeWord(endpoint + 4);

    out.Dci = XhciPipeDci(out.Address);
    if (out.Dci == 0) {
        return XHCI_PIPE_MALFORMED;
    }
    /* SuperSpeed needs the companion descriptor for Max Burst and Max ESIT
     * Payload (6.2.3.4, 4.14.2): XhciPipeEndpointParamsAt reads it. */
    if (speed != XHCI_PIPE_SPEED_FULL && speed != XHCI_PIPE_SPEED_LOW &&
        speed != XHCI_PIPE_SPEED_HIGH) {
        return XHCI_PIPE_UNSUPPORTED;
    }
    /* A non-default control endpoint would need DCI 2n+1 whatever its
     * direction bit (4.5.1); no target's class driver uses one. */
    if (out.TransferType == XHCI_PIPE_XFER_CONTROL) {
        return XHCI_PIPE_UNSUPPORTED;
    }
    /* USB 2.0 5.6 and 5.8: Low Speed has no isochronous or bulk transfers. */
    if (speed == XHCI_PIPE_SPEED_LOW &&
        out.TransferType != XHCI_PIPE_XFER_INTERRUPT) {
        return XHCI_PIPE_UNSUPPORTED;
    }

    /* 6.2.3.5: "bits 10:0 of the USB Endpoint Descriptor wMaxPacketSize",
     * and USB 2.0 allows no endpoint more than 1024. A size of 0 is a
     * zero-bandwidth isochronous endpoint, accepted (XhciPipeZeroBandwidth
     * says why and how); on any other type it is refused. */
    out.MaxPacketSize = wMaxPacketSize & 0x07FFUL;
    if (out.MaxPacketSize > XHCI_PIPE_MAX_PACKET ||
        (out.MaxPacketSize == 0 &&
         out.TransferType != XHCI_PIPE_XFER_ISOCH)) {
        return XHCI_PIPE_MALFORMED;
    }

    periodic = out.TransferType == XHCI_PIPE_XFER_INTERRUPT ||
               out.TransferType == XHCI_PIPE_XFER_ISOCH;

    /* 6.2.3.4: 0 for every LS/FS endpoint and for HS control and bulk; for
     * HS interrupt and isoch, wMaxPacketSize 12:11. 11b is reserved (USB 2.0
     * Table 9-13), so a device sending it has no meaning to program. The
     * bits are ignored where 6.2.3.4 clears the field. */
    out.MaxBurstSize = 0;
    if (speed == XHCI_PIPE_SPEED_HIGH && periodic) {
        transactions = (wMaxPacketSize >> 11) & 0x03UL;
        if (transactions == 3UL) {
            return XHCI_PIPE_MALFORMED;
        }
        out.MaxBurstSize = transactions;
    }
    if (!xhciPipeMpsLegal(speed, out.TransferType, out.MaxPacketSize,
                          out.MaxBurstSize)) {
        return XHCI_PIPE_MALFORMED;
    }
    /* Table 6-8: Mult is 0 for everything but SS / eUSB2 isochronous. */
    out.Mult = 0;

    switch (out.TransferType) {
    case XHCI_PIPE_XFER_ISOCH:
        out.EpType = out.DirectionIn ? XHCI_PIPE_EPT_ISOCH_IN
                                     : XHCI_PIPE_EPT_ISOCH_OUT;
        break;
    case XHCI_PIPE_XFER_BULK:
        out.EpType = out.DirectionIn ? XHCI_PIPE_EPT_BULK_IN
                                     : XHCI_PIPE_EPT_BULK_OUT;
        break;
    default:
        out.EpType = out.DirectionIn ? XHCI_PIPE_EPT_INTERRUPT_IN
                                     : XHCI_PIPE_EPT_INTERRUPT_OUT;
        break;
    }

    /* 6.2.3.6 and Table 6-12. */
    if (speed == XHCI_PIPE_SPEED_HIGH && periodic) {
        b = xhciPipeExponent(out.BInterval, &out.IntervalClamped);
        out.Interval = b - 1UL;
    } else if (out.TransferType == XHCI_PIPE_XFER_ISOCH) {
        b = xhciPipeExponent(out.BInterval, &out.IntervalClamped);
        out.Interval = b + 2UL;
    } else if (out.TransferType == XHCI_PIPE_XFER_INTERRUPT) {
        /* bInterval ms, 1-255, and footnote 113: round "down to the nearest
         * base 2 multiple of bInterval * 8", which lands in 3-10 by itself. */
        b = out.BInterval;
        if (b < 1UL) {
            b = 1UL;
            out.IntervalClamped = 1;
        }
        out.Interval = xhciPipeLog2(b * 8UL);
    } else {
        /* Bulk. At HS the field is the endpoint's maximum NAK rate
         * (6.2.3.6: "A value of 0 indicates the endpoint never NAKs"), not
         * a polling period. At FS Table 6-12 has no row for bulk. */
        out.Interval = 0;
    }

    out.ErrorCount = out.TransferType == XHCI_PIPE_XFER_ISOCH
                         ? 0UL : XHCI_PIPE_CERR;

    if (out.TransferType == XHCI_PIPE_XFER_INTERRUPT) {
        out.AverageTrbLength = XHCI_PIPE_AVG_TRB_INTERRUPT;
    } else if (out.TransferType == XHCI_PIPE_XFER_ISOCH) {
        out.AverageTrbLength = XHCI_PIPE_AVG_TRB_ISOCH;
    } else {
        out.AverageTrbLength = XHCI_PIPE_AVG_TRB_BULK;
    }

    /* 4.14.2: "Max ESIT Payload in Bytes = Max Packet Size * (Max Burst
     * Size + 1)" for USB2 periodic endpoints; Table 6-11 gives the field
     * meaning only for periodic ones. */
    out.MaxEsitPayload = periodic
                             ? out.MaxPacketSize * (out.MaxBurstSize + 1UL)
                             : 0UL;

    *ep = out;
    return XHCI_PIPE_OK;
}

static ULONG xhciPipeDword(const UCHAR *p)
{
    return (ULONG)p[0] | ((ULONG)p[1] << 8) | ((ULONG)p[2] << 16) |
           ((ULONG)p[3] << 24);
}

/* The bytes of `config` the descriptors may be read in: wTotalLength when
 * the header says one inside `length`, `length` otherwise. */
static ULONG xhciPipeTotal(const UCHAR *config, ULONG length)
{
    ULONG total;

    if (length < 4UL) {
        return length;
    }
    total = xhciPipeWord(config + 2);
    return (total >= 4UL && total <= length) ? total : length;
}

/*
 * The SuperSpeed endpoint (29-A.3) and its SuperSpeedPlus isochronous case
 * (29-A.6). The rules and their sources are on XhciPipeEndpointParamsAt in
 * xhci_pipe.h; section 10.6 of xhci-data-structures.md has the Endpoint
 * Context half, every row verified against xHCI 1.2c and USB 3.2.
 */
static ULONG xhciPipeSuperSpeed(const UCHAR *config, ULONG total,
                                ULONG offset, ULONG speed, ULONG lec,
                                PXHCI_PIPE_EP ep)
{
    XHCI_PIPE_EP out;
    const UCHAR *endpoint;
    ULONG wMaxPacketSize;
    ULONG next;
    ULONG burst;
    ULONG attributes;
    ULONG perInterval;
    ULONG largest;
    ULONG perBurst;
    ULONG bursts;
    ULONG sspFollows;
    ULONG b;

    endpoint = config + offset;
    out.Address = (ULONG)endpoint[2];
    out.TransferType = (ULONG)endpoint[3] & 0x03UL;
    out.DirectionIn = (out.Address & 0x80UL) != 0 ? 1UL : 0UL;
    out.BInterval = (ULONG)endpoint[6];
    out.IntervalClamped = 0;
    out.CompanionMissing = 0;
    out.MaxStreams = 0;
    out.SspIso = 0;
    wMaxPacketSize = xhciPipeWord(endpoint + 4);
    out.Dci = XhciPipeDci(out.Address);
    if (out.Dci == 0) {
        return XHCI_PIPE_MALFORMED;
    }
    if (out.TransferType == XHCI_PIPE_XFER_CONTROL) {
        return XHCI_PIPE_UNSUPPORTED;
    }
    /* At SuperSpeed bits 12:11 are reserved: the burst is the companion's.
     * Size 0 only for isochronous (USB 3.2 Table 9-26; XhciPipeZeroBandwidth),
     * and then only with burst 0, which the burst check below enforces. */
    out.MaxPacketSize = wMaxPacketSize & 0x07FFUL;
    if (out.MaxPacketSize > XHCI_PIPE_MAX_PACKET ||
        (out.MaxPacketSize == 0 &&
         out.TransferType != XHCI_PIPE_XFER_ISOCH)) {
        return XHCI_PIPE_MALFORMED;
    }

    /* The SuperSpeed Endpoint Companion "shall immediately follow" the
     * endpoint descriptor (USB 3.2 9.6.7). */
    next = offset + (ULONG)endpoint[0];
    burst = 0;
    attributes = 0;
    perInterval = 0;
    if (next < total && total - next >= XHCI_PIPE_SS_COMPANION_BYTES &&
        (ULONG)config[next + 1] == XHCI_PIPE_DT_SS_COMPANION) {
        if ((ULONG)config[next] < XHCI_PIPE_SS_COMPANION_BYTES ||
            (ULONG)config[next] > total - next) {
            return XHCI_PIPE_MALFORMED;
        }
        burst = (ULONG)config[next + 2];
        attributes = (ULONG)config[next + 3];
        perInterval = xhciPipeWord(config + next + 4);
    } else {
        out.CompanionMissing = 1;
    }
    if (burst > 15UL) {
        return XHCI_PIPE_MALFORMED;
    }
    if (out.TransferType == XHCI_PIPE_XFER_BULK) {
        if (out.MaxPacketSize != 1024UL) {
            return XHCI_PIPE_MALFORMED;
        }
    } else if (burst != 0 && out.MaxPacketSize != 1024UL) {
        return XHCI_PIPE_MALFORMED;
    }
    out.MaxBurstSize = burst;
    out.Mult = 0;
    out.MaxEsitPayload = 0;

    switch (out.TransferType) {
    case XHCI_PIPE_XFER_BULK:
        out.EpType = out.DirectionIn ? XHCI_PIPE_EPT_BULK_IN
                                     : XHCI_PIPE_EPT_BULK_OUT;
        out.MaxStreams = attributes & 0x1FUL;
        out.Interval = 0;
        out.ErrorCount = XHCI_PIPE_CERR;
        out.AverageTrbLength = XHCI_PIPE_AVG_TRB_BULK;
        break;

    case XHCI_PIPE_XFER_INTERRUPT:
        out.EpType = out.DirectionIn ? XHCI_PIPE_EPT_INTERRUPT_IN
                                     : XHCI_PIPE_EPT_INTERRUPT_OUT;
        largest = out.MaxPacketSize * (burst + 1UL);
        if (perInterval > largest) {
            return XHCI_PIPE_MALFORMED;
        }
        out.MaxEsitPayload = perInterval != 0 ? perInterval : largest;
        b = xhciPipeExponent(out.BInterval, &out.IntervalClamped);
        out.Interval = b - 1UL;
        out.ErrorCount = XHCI_PIPE_CERR;
        out.AverageTrbLength = XHCI_PIPE_AVG_TRB_INTERRUPT;
        break;

    default:
        out.EpType = out.DirectionIn ? XHCI_PIPE_EPT_ISOCH_IN
                                     : XHCI_PIPE_EPT_ISOCH_OUT;
        b = xhciPipeExponent(out.BInterval, &out.IntervalClamped);
        out.Interval = b - 1UL;
        out.ErrorCount = 0;
        out.AverageTrbLength = XHCI_PIPE_AVG_TRB_ISOCH;
        perBurst = out.MaxPacketSize * (burst + 1UL);
        sspFollows = (attributes & 0x80UL) != 0 &&
                     speed == XHCI_PIPE_SPEED_SUPER_PLUS;
        if (out.MaxPacketSize == 0) {
            /* Zero bandwidth (XhciPipeZeroBandwidth): burst 0 is already
             * held to (a burst needs 1024 above), and an endpoint with no
             * packet transfers no bytes per interval and needs no SSP
             * companion. Mult 0: Table 9-28 has it zero with bMaxBurst 0. */
            if (sspFollows || perInterval != 0) {
                return XHCI_PIPE_MALFORMED;
            }
            out.Mult = 0;
            out.MaxEsitPayload = 0;
            break;
        }
        if (sspFollows) {
            /* USB 3.2 9.6.8: the SuperSpeedPlus Isochronous Endpoint
             * Companion follows the SS companion, and its 32-bit
             * dwBytesPerInterval replaces wBytesPerInterval and Mult. */
            next += (ULONG)config[next];
            if (next >= total ||
                total - next < XHCI_PIPE_SSP_ISO_COMPANION_BYTES ||
                (ULONG)config[next + 1] != XHCI_PIPE_DT_SSP_ISO_COMPANION ||
                (ULONG)config[next] < XHCI_PIPE_SSP_ISO_COMPANION_BYTES ||
                (ULONG)config[next] > total - next) {
                return XHCI_PIPE_MALFORMED;
            }
            perInterval = xhciPipeDword(config + next + 4);
            out.SspIso = 1;
            if (lec) {
                if (perInterval > XHCI_PIPE_MAX_ESIT_LEC) {
                    return XHCI_PIPE_ESIT_REFUSED;
                }
                out.MaxEsitPayload = perInterval;
                out.Mult = 0;
            } else {
                /* The legacy Mult describes up to three bursts per
                 * interval; a payload needing more cannot be programmed. */
                bursts = (perInterval + perBurst - 1UL) / perBurst;
                if (bursts > 3UL) {
                    return XHCI_PIPE_ESIT_REFUSED;
                }
                out.Mult = bursts != 0 ? bursts - 1UL : 0UL;
                out.MaxEsitPayload = perInterval;
            }
            break;
        }
        out.Mult = attributes & 0x03UL;
        if (out.Mult == 3UL) {
            return XHCI_PIPE_MALFORMED;
        }
        largest = perBurst * (out.Mult + 1UL);
        if (perInterval > largest) {
            return XHCI_PIPE_MALFORMED;
        }
        out.MaxEsitPayload = perInterval != 0 ? perInterval : largest;
        if (lec) {
            /* Reserved under LEC: the xHC derives it from Max ESIT Payload,
             * Max Packet Size and Max Burst Size (xHCI 1.2c Table 6-8,
             * p.414; verified). */
            out.Mult = 0;
        }
        break;
    }

    /*
     * One range for admission, mapping and TRB construction (Codex review of
     * Phase 29, round 1, finding 1). An isochronous URB packet is one
     * interval's payload, and the transfer path carries it as at most two
     * page-bounded pieces of one page in all (XhciPipeIsoFragments, through
     * hcd_io.c's fill) with a burst count TBC can hold. An endpoint whose
     * interval payload is larger is refused here, counted, never admitted
     * to fail every URB later or be truncated. Lifting it means multi-page
     * isochronous packets (several TRBs per packet) in hcd_io.c and
     * xhci_xfer.c; the 48 KiB and LEC derivations above stand ready for that.
     *
     * The page is not the whole bound: the TRB builder also needs the
     * interval's burst count, ROUNDUP(packets / (Max Burst + 1)), to fit TBC
     * (xhci_xfer.c, xhciXferIsoBurstFields; 4.11.2.3), and the page alone
     * guarantees that only at 1024-byte packets. A small packet size with
     * burst 0 - 4096 bytes in 512-byte packets is eight bursts - would be
     * admitted and then fail every full-sized request, so the builder's
     * bound is applied here too (Codex review of Phase 29, round 2, finding
     * 2). Without LEC the Mult the endpoint is programmed with already
     * covers the bursts (the SS companion's payload is held to it above,
     * and the SSP derivation computes it from them).
     */
    if (out.TransferType == XHCI_PIPE_XFER_ISOCH && out.MaxPacketSize != 0) {
        ULONG packets;

        if (out.MaxEsitPayload > XHCI_PIPE_ISO_MAX_PAYLOAD) {
            return XHCI_PIPE_ESIT_REFUSED;
        }
        packets = (out.MaxEsitPayload + out.MaxPacketSize - 1UL) /
                  out.MaxPacketSize;
        if ((packets + out.MaxBurstSize) / (out.MaxBurstSize + 1UL) >
            XHCI_PIPE_ISO_MAX_BURSTS) {
            return XHCI_PIPE_ESIT_REFUSED;
        }
    }

    *ep = out;
    return XHCI_PIPE_OK;
}

ULONG XhciPipeEndpointParamsAt(const UCHAR *config, ULONG length,
                               ULONG offset, ULONG speed, ULONG lec,
                               PXHCI_PIPE_EP ep)
{
    ULONG total;

    if (config == NULL || ep == NULL) {
        return XHCI_PIPE_BAD_PARAM;
    }
    total = xhciPipeTotal(config, length);
    if (offset >= total || total - offset < XHCI_PIPE_ENDPOINT_BYTES) {
        return XHCI_PIPE_BAD_PARAM;
    }
    if (speed != XHCI_PIPE_SPEED_SUPER &&
        speed != XHCI_PIPE_SPEED_SUPER_PLUS) {
        return XhciPipeEndpointParams(config + offset, speed, ep);
    }
    if ((ULONG)config[offset] < XHCI_PIPE_ENDPOINT_BYTES ||
        (ULONG)config[offset] > total - offset ||
        (ULONG)config[offset + 1] != XHCI_PIPE_DT_ENDPOINT) {
        return XHCI_PIPE_MALFORMED;
    }
    return xhciPipeSuperSpeed(config, total, offset, speed, lec, ep);
}

ULONG XhciPipeParseBos(const UCHAR *data, ULONG length, PXHCI_PIPE_BOS bos)
{
    XHCI_PIPE_BOS out;
    ULONG total;
    ULONG offset;
    ULONG bLength;
    ULONG declared;
    ULONG i;

    if (data == NULL || bos == NULL) {
        return XHCI_PIPE_BAD_PARAM;
    }
    if (length < XHCI_PIPE_BOS_BYTES ||
        (ULONG)data[0] < XHCI_PIPE_BOS_BYTES ||
        (ULONG)data[1] != XHCI_PIPE_DT_BOS) {
        return XHCI_PIPE_MALFORMED;
    }
    total = xhciPipeWord(data + 2);
    if (total < XHCI_PIPE_BOS_BYTES || total > length ||
        (ULONG)data[0] > total) {
        return XHCI_PIPE_MALFORMED;
    }
    out.Capabilities = 0;
    out.Usb2Attributes = 0;
    out.HasSuperSpeed = 0;
    out.SsAttributes = 0;
    out.SsSpeeds = 0;
    out.SsFunctionality = 0;
    out.SsU1ExitLatency = 0;
    out.SsU2ExitLatency = 0;
    out.HasSuperSpeedPlus = 0;
    out.SspAttributes = 0;
    out.SspFunctionality = 0;
    out.SspSublinks = 0;
    for (i = 0; i < XHCI_PIPE_BOS_SUBLINKS; i++) {
        out.SspSublink[i] = 0;
    }

    offset = (ULONG)data[0];
    while (offset < total) {
        if (total - offset < 3UL) {
            return XHCI_PIPE_MALFORMED;
        }
        bLength = (ULONG)data[offset];
        if (bLength < 3UL || bLength > total - offset) {
            return XHCI_PIPE_MALFORMED;
        }
        if ((ULONG)data[offset + 1] == XHCI_PIPE_DT_DEVICE_CAP) {
            out.Capabilities++;
            switch ((ULONG)data[offset + 2]) {
            case XHCI_PIPE_CAP_USB2_EXTENSION:
                if (bLength >= 7UL) {
                    out.Usb2Attributes = xhciPipeDword(data + offset + 3);
                }
                break;
            case XHCI_PIPE_CAP_SUPERSPEED:
                if (bLength < 10UL) {
                    return XHCI_PIPE_MALFORMED;
                }
                out.HasSuperSpeed = 1;
                out.SsAttributes = (ULONG)data[offset + 3];
                out.SsSpeeds = xhciPipeWord(data + offset + 4);
                out.SsFunctionality = (ULONG)data[offset + 6];
                out.SsU1ExitLatency = (ULONG)data[offset + 7];
                out.SsU2ExitLatency = xhciPipeWord(data + offset + 8);
                break;
            case XHCI_PIPE_CAP_SUPERSPEED_PLUS:
                /* bReserved 3, bmAttributes 7:4, wFunctionalitySupport
                 * 9:8, wReserved 11:10, then SSAC + 1 attribute DWORDs
                 * (USB 3.2 Table 9-19, USB 3.2 p.357-358; verified). */
                if (bLength < 12UL) {
                    return XHCI_PIPE_MALFORMED;
                }
                out.SspAttributes = xhciPipeDword(data + offset + 4);
                declared = (out.SspAttributes & 0x1FUL) + 1UL;
                if (bLength < 12UL + declared * 4UL) {
                    return XHCI_PIPE_MALFORMED;
                }
                out.HasSuperSpeedPlus = 1;
                out.SspFunctionality = xhciPipeWord(data + offset + 8);
                out.SspSublinks = declared < XHCI_PIPE_BOS_SUBLINKS
                                      ? declared : XHCI_PIPE_BOS_SUBLINKS;
                for (i = 0; i < out.SspSublinks; i++) {
                    out.SspSublink[i] =
                        xhciPipeDword(data + offset + 12 + i * 4UL);
                }
                break;
            default:
                break;
            }
        }
        offset += bLength;
    }
    *bos = out;
    return XHCI_PIPE_OK;
}

ULONG XhciPipeConfigurePlan(ULONG keepMask, ULONG oldMask, ULONG newMask,
                            PXHCI_PIPE_PLAN plan)
{
    ULONG enabled;
    ULONG dci;

    if (plan == NULL) {
        return XHCI_PIPE_BAD_PARAM;
    }
    if (((keepMask | oldMask | newMask) & ~XHCI_PIPE_ENDPOINT_MASK) != 0) {
        return XHCI_PIPE_BAD_PARAM;
    }
    if ((keepMask & (oldMask | newMask)) != 0) {
        return XHCI_PIPE_BAD_PARAM;
    }

    enabled = keepMask | newMask;
    plan->Enabled = enabled;
    /* Every old endpoint is dropped, including one re-added: an Add on an
     * endpoint not Disabled with its Drop clear is undefined (4.6.6 p.106).
     * A0 is mandatory and A1, D0, D1 are cleared (4.6.6 p.104). */
    plan->DropFlags = oldMask;
    plan->AddFlags = XHCI_PIPE_DCI_BIT(0) | newMask;
    /* 6.2.2.2: "the index of the last valid Endpoint Context that is defined
     * by the target configuration"; EP0 alone is 1. */
    plan->ContextEntries = 1;
    for (dci = XHCI_PIPE_MAX_DCI; dci >= 2UL; dci--) {
        if ((enabled & XHCI_PIPE_DCI_BIT(dci)) != 0) {
            plan->ContextEntries = dci;
            break;
        }
    }
    return XHCI_PIPE_OK;
}

static VOID xhciPipeSetup(UCHAR *setup, ULONG requestType, ULONG request,
                          ULONG value, ULONG index, ULONG length)
{
    setup[0] = (UCHAR)requestType;
    setup[1] = (UCHAR)request;
    setup[2] = (UCHAR)(value & 0xFFUL);
    setup[3] = (UCHAR)((value >> 8) & 0xFFUL);
    setup[4] = (UCHAR)(index & 0xFFUL);
    setup[5] = (UCHAR)((index >> 8) & 0xFFUL);
    setup[6] = (UCHAR)(length & 0xFFUL);
    setup[7] = (UCHAR)((length >> 8) & 0xFFUL);
}

/* Table 4-7: the TRT follows bmRequestType bit 7 and wLength. */
static ULONG xhciPipeTrt(ULONG requestType, ULONG length)
{
    if (length == 0) {
        return XHCI_PIPE_TRT_NO_DATA;
    }
    return (requestType & XHCI_PIPE_RT_IN) != 0 ? XHCI_PIPE_TRT_IN_DATA
                                                : XHCI_PIPE_TRT_OUT_DATA;
}

ULONG XhciPipeBuildSetup(const XHCI_PIPE_CONTROL *control, UCHAR *setup,
                         PULONG trt)
{
    ULONG rt;
    ULONG request;
    ULONG value;
    ULONG index;
    ULONG length;
    ULONG recipient;
    ULONG type;

    if (control == NULL || setup == NULL || trt == NULL) {
        return XHCI_PIPE_BAD_PARAM;
    }
    if (control->Length > 0xFFFFUL) {
        return XHCI_PIPE_BAD_PARAM;
    }

    switch (control->Function) {
    case XHCI_PIPE_URB_GET_DESC_DEVICE:
    case XHCI_PIPE_URB_GET_DESC_INTERFACE:
    case XHCI_PIPE_URB_GET_DESC_ENDPOINT:
    case XHCI_PIPE_URB_SET_DESC_DEVICE:
    case XHCI_PIPE_URB_SET_DESC_INTERFACE:
    case XHCI_PIPE_URB_SET_DESC_ENDPOINT:
        if (control->DescriptorType > 0xFFUL ||
            control->DescriptorIndex > 0xFFUL ||
            control->LanguageId > 0xFFFFUL) {
            return XHCI_PIPE_BAD_PARAM;
        }
        if (control->Function == XHCI_PIPE_URB_GET_DESC_DEVICE ||
            control->Function == XHCI_PIPE_URB_SET_DESC_DEVICE) {
            recipient = XHCI_PIPE_RCPT_DEVICE;
        } else if (control->Function == XHCI_PIPE_URB_GET_DESC_INTERFACE ||
                   control->Function == XHCI_PIPE_URB_SET_DESC_INTERFACE) {
            recipient = XHCI_PIPE_RCPT_INTERFACE;
        } else {
            recipient = XHCI_PIPE_RCPT_ENDPOINT;
        }
        if (control->Function == XHCI_PIPE_URB_GET_DESC_DEVICE ||
            control->Function == XHCI_PIPE_URB_GET_DESC_INTERFACE ||
            control->Function == XHCI_PIPE_URB_GET_DESC_ENDPOINT) {
            rt = XHCI_PIPE_RT_IN | recipient;
            request = XHCI_PIPE_REQ_GET_DESCRIPTOR;
        } else {
            rt = recipient;
            request = XHCI_PIPE_REQ_SET_DESCRIPTOR;
        }
        /* USB 2.0 9.4.3: wValue is type in the high byte and index in the
         * low (inc\usb100.h line 25, USB_DESCRIPTOR_MAKE_TYPE_AND_INDEX);
         * wIndex is the URB's LanguageId, which a class driver also uses for
         * the interface number of a class descriptor. */
        value = (control->DescriptorType << 8) | control->DescriptorIndex;
        index = control->LanguageId;
        length = control->Length;
        break;

    case XHCI_PIPE_URB_SET_FEATURE_DEVICE:
    case XHCI_PIPE_URB_SET_FEATURE_INTERFACE:
    case XHCI_PIPE_URB_SET_FEATURE_ENDPOINT:
    case XHCI_PIPE_URB_SET_FEATURE_OTHER:
    case XHCI_PIPE_URB_CLEAR_FEATURE_DEVICE:
    case XHCI_PIPE_URB_CLEAR_FEATURE_INTERFACE:
    case XHCI_PIPE_URB_CLEAR_FEATURE_ENDPOINT:
    case XHCI_PIPE_URB_CLEAR_FEATURE_OTHER:
        if (control->FeatureSelector > 0xFFFFUL ||
            control->Index > 0xFFFFUL) {
            return XHCI_PIPE_BAD_PARAM;
        }
        switch (control->Function) {
        case XHCI_PIPE_URB_SET_FEATURE_DEVICE:
        case XHCI_PIPE_URB_CLEAR_FEATURE_DEVICE:
            recipient = XHCI_PIPE_RCPT_DEVICE;
            break;
        case XHCI_PIPE_URB_SET_FEATURE_INTERFACE:
        case XHCI_PIPE_URB_CLEAR_FEATURE_INTERFACE:
            recipient = XHCI_PIPE_RCPT_INTERFACE;
            break;
        case XHCI_PIPE_URB_SET_FEATURE_ENDPOINT:
        case XHCI_PIPE_URB_CLEAR_FEATURE_ENDPOINT:
            recipient = XHCI_PIPE_RCPT_ENDPOINT;
            break;
        default:
            recipient = XHCI_PIPE_RCPT_OTHER;
            break;
        }
        rt = recipient;
        request = (control->Function == XHCI_PIPE_URB_SET_FEATURE_DEVICE ||
                   control->Function == XHCI_PIPE_URB_SET_FEATURE_INTERFACE ||
                   control->Function == XHCI_PIPE_URB_SET_FEATURE_ENDPOINT ||
                   control->Function == XHCI_PIPE_URB_SET_FEATURE_OTHER)
                      ? XHCI_PIPE_REQ_SET_FEATURE
                      : XHCI_PIPE_REQ_CLEAR_FEATURE;
        value = control->FeatureSelector;
        index = control->Index;
        /* The feature URB has no buffer at all (inc\usbdi.h line 612's
         * structure); USB 2.0 9.4.1 / 9.4.9 have wLength zero. */
        length = 0;
        break;

    case XHCI_PIPE_URB_GET_STATUS_DEVICE:
    case XHCI_PIPE_URB_GET_STATUS_INTERFACE:
    case XHCI_PIPE_URB_GET_STATUS_ENDPOINT:
    case XHCI_PIPE_URB_GET_STATUS_OTHER:
        if (control->Index > 0xFFFFUL || control->Length < 2UL) {
            return XHCI_PIPE_BAD_PARAM;
        }
        if (control->Function == XHCI_PIPE_URB_GET_STATUS_DEVICE) {
            recipient = XHCI_PIPE_RCPT_DEVICE;
        } else if (control->Function == XHCI_PIPE_URB_GET_STATUS_INTERFACE) {
            recipient = XHCI_PIPE_RCPT_INTERFACE;
        } else if (control->Function == XHCI_PIPE_URB_GET_STATUS_ENDPOINT) {
            recipient = XHCI_PIPE_RCPT_ENDPOINT;
        } else {
            recipient = XHCI_PIPE_RCPT_OTHER;
        }
        rt = XHCI_PIPE_RT_IN | recipient;
        request = XHCI_PIPE_REQ_GET_STATUS;
        value = 0;
        index = control->Index;
        length = 2UL;
        break;

    case XHCI_PIPE_URB_GET_CONFIGURATION:
        if (control->Length < 1UL) {
            return XHCI_PIPE_BAD_PARAM;
        }
        rt = XHCI_PIPE_RT_IN | XHCI_PIPE_RCPT_DEVICE;
        request = XHCI_PIPE_REQ_GET_CONFIGURATION;
        value = 0;
        index = 0;
        length = 1UL;
        break;

    case XHCI_PIPE_URB_GET_INTERFACE:
        if (control->Index > 0xFFFFUL || control->Length < 1UL) {
            return XHCI_PIPE_BAD_PARAM;
        }
        rt = XHCI_PIPE_RT_IN | XHCI_PIPE_RCPT_INTERFACE;
        request = XHCI_PIPE_REQ_GET_INTERFACE;
        value = 0;
        index = control->Index;
        length = 1UL;
        break;

    case XHCI_PIPE_URB_VENDOR_DEVICE:
    case XHCI_PIPE_URB_VENDOR_INTERFACE:
    case XHCI_PIPE_URB_VENDOR_ENDPOINT:
    case XHCI_PIPE_URB_VENDOR_OTHER:
    case XHCI_PIPE_URB_CLASS_DEVICE:
    case XHCI_PIPE_URB_CLASS_INTERFACE:
    case XHCI_PIPE_URB_CLASS_ENDPOINT:
    case XHCI_PIPE_URB_CLASS_OTHER:
        if (control->Request > 0xFFUL || control->Value > 0xFFFFUL ||
            control->Index > 0xFFFFUL) {
            return XHCI_PIPE_BAD_PARAM;
        }
        switch (control->Function) {
        case XHCI_PIPE_URB_VENDOR_DEVICE:
        case XHCI_PIPE_URB_CLASS_DEVICE:
            recipient = XHCI_PIPE_RCPT_DEVICE;
            break;
        case XHCI_PIPE_URB_VENDOR_INTERFACE:
        case XHCI_PIPE_URB_CLASS_INTERFACE:
            recipient = XHCI_PIPE_RCPT_INTERFACE;
            break;
        case XHCI_PIPE_URB_VENDOR_ENDPOINT:
        case XHCI_PIPE_URB_CLASS_ENDPOINT:
            recipient = XHCI_PIPE_RCPT_ENDPOINT;
            break;
        default:
            recipient = XHCI_PIPE_RCPT_OTHER;
            break;
        }
        type = (control->Function == XHCI_PIPE_URB_VENDOR_DEVICE ||
                control->Function == XHCI_PIPE_URB_VENDOR_INTERFACE ||
                control->Function == XHCI_PIPE_URB_VENDOR_ENDPOINT ||
                control->Function == XHCI_PIPE_URB_VENDOR_OTHER)
                   ? XHCI_PIPE_RT_VENDOR
                   : XHCI_PIPE_RT_CLASS;
        /* "direction is specified in TransferFlags" (inc\usbdi.h lines
         * 112 and 120), bit 0 (line 151). RequestTypeReservedBits is not
         * read: it overlays the SETUP packet's bmRequestType, and Windows
         * 2000 SP4's usbport.sys writes type, direction and recipient over
         * that byte and then clears bits 4:2 (`and cl,0E3h` at 0x235E4,
         * static), so nothing a client puts there reaches the bus. Windows
         * 98 SE's hidusb.sys puts 0x22 there for SET_REPORT and SET_IDLE
         * (`mov byte ptr [esi+48h],22h` at 0x10C2A and 0x11044, static);
         * refusing it failed every one (c10, 2026-10-03). */
        rt = (control->DirectionIn ? XHCI_PIPE_RT_IN : 0UL) | type |
             recipient;
        request = control->Request;
        value = control->Value;
        index = control->Index;
        length = control->Length;
        break;

    default:
        return XHCI_PIPE_UNSUPPORTED;
    }

    xhciPipeSetup(setup, rt, request, value, index, length);
    *trt = xhciPipeTrt(rt, length);
    return XHCI_PIPE_OK;
}

ULONG XhciPipeCheckRawSetup(const UCHAR *setup, PULONG trt)
{
    ULONG rt;

    if (setup == NULL || trt == NULL) {
        return XHCI_PIPE_BAD_PARAM;
    }
    rt = (ULONG)setup[0];
    /* Refused at any recipient: each request is defined for one recipient
     * only (the device, or the interface for SET_INTERFACE), and a
     * reserved-recipient spelling of it is no safer. SET_INTERFACE is the
     * bus's own, after the Configure Endpoint of a SELECT_INTERFACE; one sent
     * raw would leave the Endpoint Contexts naming the old alternate. */
    if ((rt & XHCI_PIPE_RT_TYPE_MASK) == XHCI_PIPE_RT_STANDARD &&
        ((ULONG)setup[1] == XHCI_PIPE_REQ_SET_ADDRESS ||
         (ULONG)setup[1] == XHCI_PIPE_REQ_SET_CONFIGURATION ||
         (ULONG)setup[1] == XHCI_PIPE_REQ_SET_INTERFACE)) {
        return XHCI_PIPE_REFUSED;
    }
    *trt = xhciPipeTrt(rt, xhciPipeWord(setup + 6));
    return XHCI_PIPE_OK;
}

ULONG XhciPipeSplit(ULONG pageOffset, ULONG length, ULONG maxPacketSize,
                    ULONG mapRegisters, ULONG maxElements,
                    PXHCI_PIPE_ELEMENT elements, PULONG count,
                    PULONG chunkBytes)
{
    ULONG pages;
    ULONG cap;
    ULONG chunk;
    ULONG remaining;
    ULONG offset;
    ULONG piece;
    ULONG n;

    if (count == NULL || chunkBytes == NULL ||
        pageOffset >= XHCI_PIPE_PAGE_SIZE || maxPacketSize == 0) {
        return XHCI_PIPE_BAD_PARAM;
    }
    if (length == 0) {
        *count = 0;
        *chunkBytes = 0;
        return XHCI_PIPE_OK;
    }
    if (elements == NULL) {
        return XHCI_PIPE_BAD_PARAM;
    }
    pages = mapRegisters;
    if (pages > maxElements) {
        pages = maxElements;
    }
    if (pages > XHCI_PIPE_CHUNK_ELEMENTS) {
        pages = XHCI_PIPE_CHUNK_ELEMENTS;
    }
    if (pages == 0) {
        return XHCI_PIPE_BAD_PARAM;
    }

    cap = pages * XHCI_PIPE_PAGE_SIZE - pageOffset;
    if (cap > XHCI_PIPE_TD_MAX_BYTES) {
        cap = XHCI_PIPE_TD_MAX_BYTES;
    }
    chunk = length < cap ? length : cap;
    /* A packet shorter than Max Packet Size ends the transfer at the device
     * (USB 2.0 5.8.3), so a chunk with more to follow must stop on a packet
     * boundary. Only the last chunk may end short. */
    if (chunk < length) {
        chunk -= chunk % maxPacketSize;
        if (chunk == 0) {
            return XHCI_PIPE_BAD_PARAM;
        }
    }

    n = 0;
    offset = pageOffset;
    remaining = chunk;
    while (remaining != 0) {
        piece = XHCI_PIPE_PAGE_SIZE - offset;
        if (piece > remaining) {
            piece = remaining;
        }
        elements[n].Page = n;
        elements[n].Offset = offset;
        elements[n].Length = piece;
        remaining -= piece;
        offset = 0;
        n++;
    }
    *count = n;
    *chunkBytes = chunk;
    return XHCI_PIPE_OK;
}

ULONG XhciPipeIsoLength(const XHCI_PIPE_ISO_PACKET *packets, ULONG count,
                        ULONG bufferLength, ULONG i)
{
    ULONG end;

    end = (i + 1 < count) ? packets[i + 1].Offset : bufferLength;
    return end - packets[i].Offset;
}

ULONG XhciPipeIsoCheck(const XHCI_PIPE_ISO_PACKET *packets, ULONG count,
                       ULONG bufferLength, ULONG maxCount, ULONG maxPacket)
{
    ULONG i;

    if (packets == NULL || count == 0 || count > maxCount ||
        bufferLength == 0) {
        return XHCI_PIPE_BAD_PARAM;
    }
    for (i = 0; i < count; i++) {
        if (packets[i].Offset > bufferLength ||
            (i + 1 < count && packets[i + 1].Offset < packets[i].Offset)) {
            return XHCI_PIPE_BAD_PARAM;
        }
        if (maxPacket != 0 &&
            XhciPipeIsoLength(packets, count, bufferLength, i) > maxPacket) {
            return XHCI_PIPE_BAD_PARAM;
        }
    }
    return XHCI_PIPE_OK;
}

ULONG XhciPipeIsoFragments(ULONG pageOffset, ULONG offset, ULONG length,
                           PULONG lengths)
{
    ULONG room;

    if (lengths == NULL || pageOffset >= XHCI_PIPE_PAGE_SIZE ||
        length > XHCI_PIPE_PAGE_SIZE) {
        return 0;
    }
    room = XHCI_PIPE_PAGE_SIZE -
           ((pageOffset + offset) & XHCI_PIPE_PAGE_MASK);
    if (length <= room) {
        lengths[0] = length;
        lengths[1] = 0;
        return 1;
    }
    lengths[0] = room;
    lengths[1] = length - room;
    return 2;
}

ULONG XhciPipeIsoFrameOf(ULONG i, ULONG interval)
{
    return (i << interval) >> 3;
}

ULONG XhciPipeIsoFrames(ULONG packets, ULONG interval)
{
    return ((packets << interval) + 7UL) >> 3;
}

ULONG XhciPipeIsoStartOk(ULONG startFrame, ULONG now, ULONG ahead)
{
    if (startFrame - now <= ahead ||
        now - startFrame <= XHCI_PIPE_ISO_START_RANGE) {
        return XHCI_PIPE_OK;
    }
    return XHCI_PIPE_REFUSED;
}

ULONG XhciPipeNtStatus(ULONG usbd)
{
    switch (usbd) {
    case XHCI_PIPE_USBD_SUCCESS:
        return XHCI_PIPE_NT_SUCCESS;
    case XHCI_PIPE_USBD_PENDING:
        return XHCI_PIPE_NT_PENDING;
    case XHCI_PIPE_USBD_CANCELED:
        return XHCI_PIPE_NT_CANCELLED;
    case XHCI_PIPE_USBD_INVALID_URB_FUNCTION:
    case XHCI_PIPE_USBD_INVALID_PARAMETER:
    case XHCI_PIPE_USBD_INVALID_PIPE_HANDLE:
    case XHCI_PIPE_USBD_BAD_START_FRAME:
        return XHCI_PIPE_NT_INVALID_PARAMETER;
    case XHCI_PIPE_USBD_NO_MEMORY:
    case XHCI_PIPE_USBD_NO_BANDWIDTH:
        return XHCI_PIPE_NT_INSUFFICIENT_RESOURCES;
    case XHCI_PIPE_USBD_ERROR_BUSY:
        return XHCI_PIPE_NT_DEVICE_BUSY;
    case XHCI_PIPE_USBD_DEVICE_GONE:
        return XHCI_PIPE_NT_DEVICE_NOT_CONNECTED;
    default:
        break;
    }
    /* USBD_ERROR is "(USBD_STATUS)(Status) < 0" (inc\usbdi.h), bit 31. */
    if ((usbd & 0x80000000UL) != 0) {
        return XHCI_PIPE_NT_UNSUCCESSFUL;
    }
    return XHCI_PIPE_NT_SUCCESS;
}

ULONG XhciPipeConfigureUsbdStatus(ULONG completionCode)
{
    switch (completionCode) {
    case 1UL:
        return XHCI_PIPE_USBD_SUCCESS;
    case 7UL:
    case 8UL:
    case 35UL:
        return XHCI_PIPE_USBD_NO_BANDWIDTH;
    default:
        return XHCI_PIPE_USBD_INTERNAL_HC_ERROR;
    }
}

ULONG XhciPipeResetParts(ULONG function)
{
    switch (function) {
    case XHCI_PIPE_URB_RESET_PIPE:
        return XHCI_PIPE_RESET_HOST | XHCI_PIPE_RESET_DEVICE;
    case XHCI_PIPE_URB_SYNC_RESET_PIPE:
        return XHCI_PIPE_RESET_HOST;
    case XHCI_PIPE_URB_SYNC_CLEAR_STALL:
        return XHCI_PIPE_RESET_DEVICE;
    default:
        return 0;
    }
}

ULONG XhciPipeUsbdiSize(ULONG version, ULONG pointerBytes)
{
    ULONG pointers;

    if (pointerBytes != 4UL && pointerBytes != 8UL) {
        return 0;
    }
    switch (version) {
    case 0:
        pointers = 7;
        break;
    case 1:
        pointers = 8;
        break;
    case 2:
        pointers = 9;
        break;
    case 3:
        pointers = 11;
        break;
    default:
        return 0;
    }
    /* Size and Version, padded to the first pointer. */
    return pointerBytes + pointers * pointerBytes;
}

static VOID xhciPipePut32(UCHAR *p, ULONG value)
{
    p[0] = (UCHAR)(value & 0xFFUL);
    p[1] = (UCHAR)((value >> 8) & 0xFFUL);
    p[2] = (UCHAR)((value >> 16) & 0xFFUL);
    p[3] = (UCHAR)((value >> 24) & 0xFFUL);
}

ULONG XhciPipeBusInformation(ULONG level, ULONG totalBandwidth,
                             ULONG consumedBandwidth, UCHAR *buffer,
                             PULONG length, PULONG actual)
{
    ULONG need;
    ULONG i;

    if (level == 0) {
        need = XHCI_PIPE_BUSINFO0_BYTES;
    } else if (level == 1) {
        need = XHCI_PIPE_BUSINFO1_BYTES;
    } else {
        return XHCI_PIPE_UNSUPPORTED;
    }
    if (actual != NULL) {
        *actual = need;
    }
    if (length == NULL) {
        return XHCI_PIPE_BAD_PARAM;
    }
    if (*length < need) {
        return XHCI_PIPE_TOO_SMALL;
    }
    if (buffer == NULL) {
        return XHCI_PIPE_BAD_PARAM;
    }
    for (i = 0; i < need; i++) {
        buffer[i] = 0;
    }
    xhciPipePut32(buffer, totalBandwidth);
    xhciPipePut32(buffer + 4, consumedBandwidth);
    *length = need;
    return XHCI_PIPE_OK;
}

ULONG XhciPipeTopologyAddress(ULONG pciBus, ULONG pciAddress, ULONG rootPort,
                              ULONG route, UCHAR *out)
{
    ULONG i;

    if (out == NULL || rootPort == 0 || rootPort > 255UL) {
        return XHCI_PIPE_BAD_PARAM;
    }
    for (i = 0; i < XHCI_PIPE_TOPOLOGY_BYTES; i++) {
        out[i] = 0;
    }
    xhciPipePut32(out, pciBus);
    xhciPipePut32(out + 4, (pciAddress >> 16) & 0xFFFFUL);
    xhciPipePut32(out + 8, pciAddress & 0xFFFFUL);
    out[16] = (UCHAR)(rootPort & 0xFFUL);
    for (i = 0; i < 5UL; i++) {
        out[18 + 2 * i] = (UCHAR)((route >> (4 * i)) & 0xFUL);
    }
    return XHCI_PIPE_OK;
}

/* IRQL: any. A low word of 0 is skipped, so a lap is 2^32 - 1 values: the
 * reconstruction's bound (xhci_pipe.h). */
VOID XhciSeqNext(PXHCI_SEQ64 seq)
{
    seq->Lo++;
    if (seq->Lo == 0) {
        seq->Hi++;
        seq->Lo = 1;
    }
}

/* IRQL: any. */
VOID XhciSeqFromStamp(const XHCI_SEQ64 *current, ULONG stamp,
                      PXHCI_SEQ64 out)
{
    ULONG age;

    /* current - age, with age = (current.Lo - stamp) mod 2^32: the low
     * word comes back as the stamp, and the high word borrows when the
     * subtraction crossed a lap. Exact while fewer than 2^32 - 1
     * submissions followed the stamp (xhci_pipe.h). */
    age = current->Lo - stamp;
    out->Lo = stamp;
    out->Hi = current->Hi - ((age > current->Lo) ? 1UL : 0UL);
}

/* IRQL: any. */
ULONG XhciSeqLessEq(const XHCI_SEQ64 *a, const XHCI_SEQ64 *b)
{
    return a->Hi < b->Hi || (a->Hi == b->Hi && a->Lo <= b->Lo);
}

/* IRQL: any. */
ULONG XhciSeqIsNone(const XHCI_SEQ64 *a)
{
    return a->Lo == 0 && a->Hi == 0;
}

/* IRQL: any. */
ULONG XhciSeqCovers(const XHCI_SEQ64 *horizon, ULONG stamp,
                    const XHCI_SEQ64 *current)
{
    XHCI_SEQ64 full;

    if (XhciSeqIsNone(horizon) || stamp == 0) {
        return 0;
    }
    XhciSeqFromStamp(current, stamp, &full);
    return XhciSeqLessEq(&full, horizon);
}

/* IRQL: any. */
VOID XhciSeqLatest(PXHCI_SEQ64 into, const XHCI_SEQ64 *with)
{
    if (XhciSeqIsNone(with)) {
        return;
    }
    if (XhciSeqIsNone(into) || XhciSeqLessEq(into, with)) {
        *into = *with;
    }
}
