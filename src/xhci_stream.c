/*
 * xhci_stream.c - the pure half of bulk streams (xhci_stream.h; roadmap-hcd.md
 * task 31-A.1).
 *
 * Every encoding cites the xHCI 1.2c section it comes from as transcribed in
 * docs/usb-xhci-info/xhci-data-structures.md, "Streams"; the rows marked "to
 * verify" there were written without the PDF at hand and are what a reader
 * with it checks first. The companion descriptor rule is USB 3.2 9.6.7.
 *
 * C89, pure: IRQL any.
 */

#include "xhci_stream.h"

static ULONG xhciStreamIsPow2(ULONG v)
{
    return v != 0 && (v & (v - 1UL)) == 0;
}

/* log2 of a power of two. */
static ULONG xhciStreamLog2(ULONG v)
{
    ULONG n;

    n = 0;
    while (v > 1UL) {
        v >>= 1;
        n++;
    }
    return n;
}

ULONG XhciStreamHcEntries(ULONG hccparams1)
{
    ULONG psa;

    if (hccparams1 == 0xFFFFFFFFUL) {
        return 0;
    }
    /* 5.3.6: "Primary Stream Array size = 2^(MaxPSASize+1)", and "a value of
     * '0' indicates that Streams are not supported". */
    psa = XHCI_HCCPARAMS1_MAXPSA(hccparams1);
    if (psa == 0) {
        return 0;
    }
    return 1UL << (psa + 1UL);
}

ULONG XhciStreamCompanionExponent(const UCHAR *config, ULONG length,
                                  ULONG endpointOffset, PULONG exponent)
{
    ULONG total;
    ULONG at;
    ULONG exp;

    if (config == NULL || exponent == NULL || length < 4UL) {
        return XHCI_STREAM_BAD_PARAM;
    }
    *exponent = 0;
    total = (ULONG)config[2] | ((ULONG)config[3] << 8);
    if (total > length) {
        total = length;
    }
    if (endpointOffset >= total || total - endpointOffset < 2UL ||
        (ULONG)config[endpointOffset] < 2UL ||
        (ULONG)config[endpointOffset] > total - endpointOffset) {
        return XHCI_STREAM_BAD_PARAM;
    }
    at = endpointOffset + (ULONG)config[endpointOffset];
    /* "shall immediately follow the endpoint descriptor" (USB 3.2 9.6.7):
     * anything else there, or nothing, is an endpoint without one. */
    if (total - at < 2UL ||
        (ULONG)config[at + 1] != XHCI_STREAM_DT_SS_COMPANION) {
        return XHCI_STREAM_EP_NONE;
    }
    if ((ULONG)config[at] < XHCI_STREAM_COMPANION_BYTES ||
        (ULONG)config[at] > total - at) {
        return XHCI_STREAM_MALFORMED;
    }
    exp = (ULONG)config[at + 3] & 0x1FUL;
    if (exp > XHCI_STREAM_MAX_STREAMS_EXP) {
        return XHCI_STREAM_MALFORMED;
    }
    *exponent = exp;
    return exp == 0 ? XHCI_STREAM_EP_NONE : XHCI_STREAM_OK;
}

ULONG XhciStreamPlan(ULONG requested, ULONG exponent, ULONG hcEntries,
                     ULONG capEntries, PXHCI_STREAM_PLAN plan)
{
    ULONG want;
    ULONG entries;
    ULONG limit;

    if (plan == NULL || requested == 0 || !xhciStreamIsPow2(capEntries) ||
        capEntries < XHCI_STREAM_MIN_ENTRIES) {
        return XHCI_STREAM_BAD_PARAM;
    }
    if (hcEntries < XHCI_STREAM_MIN_ENTRIES) {
        return XHCI_STREAM_HC_NONE;
    }
    if (exponent == 0) {
        return XHCI_STREAM_EP_NONE;
    }
    if (exponent > XHCI_STREAM_MAX_STREAMS_EXP) {
        return XHCI_STREAM_MALFORMED;
    }
    limit = hcEntries < capEntries ? hcEntries : capEntries;
    /* Compared against the limit before anything is shifted or added, so no
     * request and no exponent can overflow what follows. */
    want = requested;
    if (want > (1UL << exponent)) {
        want = 1UL << exponent;
    }
    if (want > limit - 1UL) {
        want = limit - 1UL;
    }
    /* Stream ID 0 is reserved (4.12.2, "to verify" in the transcription):
     * one entry more than the streams served. */
    entries = XHCI_STREAM_MIN_ENTRIES;
    while (entries < want + 1UL) {
        entries <<= 1;
    }
    plan->Entries = entries;
    plan->Streams = want;
    plan->MaxPStreams = xhciStreamLog2(entries) - 1UL;
    return XHCI_STREAM_OK;
}

ULONG XhciStreamLayout(ULONG entries, ULONG streams, ULONG ringTrbs,
                       PXHCI_STREAM_LAYOUT layout)
{
    ULONG ringBytes;
    ULONG base;
    ULONG total;

    if (layout == NULL || !xhciStreamIsPow2(entries) ||
        entries < XHCI_STREAM_MIN_ENTRIES ||
        entries > XHCI_STREAM_MAX_ENTRIES || streams == 0 ||
        streams >= entries || !xhciStreamIsPow2(ringTrbs) || ringTrbs < 4UL ||
        ringTrbs > XHCI_STREAM_PAGE_BYTES / XHCI_TRB_BYTES) {
        return XHCI_STREAM_BAD_PARAM;
    }
    ringBytes = ringTrbs * XHCI_TRB_BYTES;
    layout->ArrayBytes = entries * XHCI_STREAM_CONTEXT_BYTES;
    base = layout->ArrayBytes;
    if ((base % ringBytes) != 0) {
        base += ringBytes - (base % ringBytes);
    }
    layout->RingBase = base;
    layout->RingBytes = ringBytes;
    total = base + streams * ringBytes;
    if ((total % XHCI_STREAM_PAGE_BYTES) != 0) {
        total += XHCI_STREAM_PAGE_BYTES - (total % XHCI_STREAM_PAGE_BYTES);
    }
    layout->TotalBytes = total;
    return XHCI_STREAM_OK;
}

ULONG XhciStreamRingOffset(const XHCI_STREAM_LAYOUT *layout, ULONG streamId)
{
    return layout->RingBase + (streamId - 1UL) * layout->RingBytes;
}

ULONG XhciStreamContextBuild(volatile ULONG *context, ULONG ringPA,
                             ULONG dcs, ULONG sct)
{
    if (context == NULL || (ringPA & 0x0FUL) != 0 || dcs > 1UL ||
        sct > 7UL || (ringPA == 0 && (sct != 0 || dcs != 0))) {
        return XHCI_STREAM_BAD_PARAM;
    }
    context[0] = ringPA | (sct << 1) | dcs;
    context[1] = 0;
    context[2] = 0;
    context[3] = 0;
    return XHCI_STREAM_OK;
}

ULONG XhciStreamEndpointContext(volatile ULONG *context, ULONG maxPStreams,
                                ULONG arrayPA)
{
    ULONG dw0;

    if (context == NULL || maxPStreams < 1UL || maxPStreams > 15UL ||
        arrayPA == 0 || (arrayPA & 0x0FUL) != 0) {
        return XHCI_STREAM_BAD_PARAM;
    }
    dw0 = context[0];
    dw0 &= ~(XHCI_STREAM_EP_MAXPSTREAMS_MASK | XHCI_STREAM_EP_LSA);
    dw0 |= (maxPStreams << 10) | XHCI_STREAM_EP_LSA;
    context[0] = dw0;
    /* DCS is the array's, not a ring's: 0 (Table 6-10, "to verify"). */
    context[2] = arrayPA;
    context[3] = 0;
    return XHCI_STREAM_OK;
}

ULONG XhciStreamSetDequeueTrb(XHCI_TRB *trb, ULONG slotId, ULONG dci,
                              ULONG streamId, ULONG dequeuePA, ULONG dcs,
                              ULONG sct)
{
    if (trb == NULL || slotId == 0 || slotId > 0xFFUL || dci < 1UL ||
        dci > 31UL || streamId == 0 || streamId > 0xFFFFUL ||
        dequeuePA == 0 || (dequeuePA & 0x0FUL) != 0 || dcs > 1UL ||
        sct < 1UL || sct > 7UL) {
        return XHCI_STREAM_BAD_PARAM;
    }
    trb->Param0 = dequeuePA | (sct << 1) | dcs;
    trb->Param1 = 0;
    trb->Status = streamId << 16;
    trb->Control = XHCI_TRB_TYPE(XHCI_TRB_TYPE_SET_TR_DEQUEUE) |
                   XHCI_TRB_EP_ID(dci) | XHCI_TRB_SLOT_ID(slotId);
    return XHCI_STREAM_OK;
}

ULONG XhciStreamDoorbell(ULONG dci, ULONG streamId)
{
    return (dci & 0xFFUL) | ((streamId & 0xFFFFUL) << 16);
}

ULONG XhciStreamFind(const XHCI_RING *const *rings, ULONG count,
                     ULONG trbPA)
{
    const XHCI_RING *ring;
    ULONG id;

    if (rings == NULL || (trbPA & 0x0FUL) != 0 || trbPA == 0) {
        return 0;
    }
    for (id = 1; id < count; id++) {
        ring = rings[id];
        if (ring == NULL || ring->Trbs == 0) {
            continue;
        }
        if (trbPA >= ring->BasePA &&
            trbPA - ring->BasePA < ring->Trbs * XHCI_TRB_BYTES) {
            return id;
        }
    }
    return 0;
}
