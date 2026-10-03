/*
 * xhci_pipe.h - the pure half of URB dispatch in xhci98.sys (roadmap-hcd.md
 * task 26-A.5; design record 13 section 6).
 *
 * Six computations the dispatch path needs and none of which touches a
 * register, a lock or a kernel service, so the host suite drives every one
 * of them (test\test_pipe.c):
 *
 *   - the configuration-descriptor walk: interface N, alternate A, and its
 *     endpoint descriptors, with every length bound checked;
 *   - the endpoint parameters an Endpoint Context needs, from an endpoint
 *     descriptor and the device's speed;
 *   - the Configure Endpoint plan: Add and Drop flags and Context Entries;
 *   - the 8-byte SETUP packet for each control URB function, and the
 *     refusal of a raw SET_ADDRESS, SET_CONFIGURATION or SET_INTERFACE;
 *   - the split of one transfer buffer over the map registers available;
 *   - the USBD_STATUS to NTSTATUS table.
 *
 * **Not built on XhciBuildEndpointParams** (xhci.h), which takes what
 * usbport supplies - a Period already converted, a TransactionsPerMicroframe
 * - and so answers a question this driver no longer asks. The fields of
 * XHCI_PIPE_EP are named after XHCI_EP_PARAMS's so the caller copies them
 * across and adds the ring's DequeuePA and DCS before XhciBuildEndpointContext.
 *
 * The USBD_STATUS and NTSTATUS values are local constants with the Windows
 * 2000 DDK's numbers (inc\usbdi.h, inc\ntstatus.h; line numbers at each
 * block), under this module's own prefix so a file that also includes the DDK
 * headers sees no second definition of the DDK's names.
 *
 * DDK-free: part of the pure core.
 */

#ifndef XHCI_PIPE_H
#define XHCI_PIPE_H

#include "xhci_compat.h"

/* Status codes. */
#define XHCI_PIPE_OK                0UL
#define XHCI_PIPE_BAD_PARAM         1UL /* a caller error: NULL, a range     */
#define XHCI_PIPE_MALFORMED         2UL /* the device's descriptors are bad  */
#define XHCI_PIPE_NOT_FOUND         3UL /* no such interface / alternate     */
#define XHCI_PIPE_UNSUPPORTED       4UL /* legal, but not served here        */
#define XHCI_PIPE_REFUSED           5UL /* a request this bus must not pass  */
#define XHCI_PIPE_ESIT_REFUSED      6UL /* a SuperSpeedPlus isochronous
                                         * payload the controller's Endpoint
                                         * Context cannot describe (29-A.6) */

/*
 * The endpoint rules' speeds: a decoded class in the default-ID vocabulary
 * (1 = FS, 2 = LS, 3 = HS, 4 = SS; equal to XHCI_ENUM_SPEED_*), never the
 * controller's raw PSIV, which a PSI table may assign differently - the
 * caller decodes it (hcd_cfg.c). SUPER_PLUS is a SuperSpeed-class link above
 * Gen 1x1 (29-A.1's rate): its bulk and interrupt rules are SUPER's, and it
 * alone reads the SuperSpeedPlus Isochronous Endpoint Companion (29-A.6).
 */
#define XHCI_PIPE_SPEED_FULL        1UL
#define XHCI_PIPE_SPEED_LOW         2UL
#define XHCI_PIPE_SPEED_HIGH        3UL
#define XHCI_PIPE_SPEED_SUPER       4UL
#define XHCI_PIPE_SPEED_SUPER_PLUS  5UL

/* USB descriptor types and lengths (Windows 2000 DDK inc\usb100.h lines
 * 14-18; USB 2.0 Tables 9-10, 9-12, 9-13). */
#define XHCI_PIPE_DT_CONFIGURATION  0x02UL
#define XHCI_PIPE_DT_INTERFACE      0x04UL
#define XHCI_PIPE_DT_ENDPOINT       0x05UL
#define XHCI_PIPE_CONFIG_BYTES      9UL
#define XHCI_PIPE_INTERFACE_BYTES   9UL
#define XHCI_PIPE_ENDPOINT_BYTES    7UL
/* USB 3.2 Table 9-6 and 9.6.2 to 9.6.8 (xhci-data-structures.md section
 * 10.7; to verify against the USB 3.2 specification). */
#define XHCI_PIPE_DT_BOS            0x0FUL
#define XHCI_PIPE_DT_DEVICE_CAP     0x10UL
#define XHCI_PIPE_DT_SS_COMPANION   0x30UL
#define XHCI_PIPE_DT_SSP_ISO_COMPANION 0x31UL
#define XHCI_PIPE_BOS_BYTES         5UL
#define XHCI_PIPE_SS_COMPANION_BYTES 6UL
#define XHCI_PIPE_SSP_ISO_COMPANION_BYTES 8UL

/* bmAttributes 1:0 (inc\usb100.h lines 32-37). */
#define XHCI_PIPE_XFER_CONTROL      0UL
#define XHCI_PIPE_XFER_ISOCH        1UL
#define XHCI_PIPE_XFER_BULK         2UL
#define XHCI_PIPE_XFER_INTERRUPT    3UL

/* Endpoint Context EP Type (spec Table 6-9; equal to XHCI_EP_TYPE_*). */
#define XHCI_PIPE_EPT_ISOCH_OUT     1UL
#define XHCI_PIPE_EPT_BULK_OUT      2UL
#define XHCI_PIPE_EPT_INTERRUPT_OUT 3UL
#define XHCI_PIPE_EPT_CONTROL       4UL
#define XHCI_PIPE_EPT_ISOCH_IN      5UL
#define XHCI_PIPE_EPT_BULK_IN       6UL
#define XHCI_PIPE_EPT_INTERRUPT_IN  7UL

/* DCIs 2..31 are the non-default endpoints (spec 4.5.1): 15 IN and 15 OUT,
 * so one interface can name at most 30. */
#define XHCI_PIPE_MAX_DCI           31UL
#define XHCI_PIPE_MAX_ENDPOINTS     30UL

/* ------------------------------------------------------------------ */
/* The configuration-descriptor walk                                   */
/* ------------------------------------------------------------------ */

typedef struct _XHCI_PIPE_IFACE {
    ULONG InterfaceOffset;      /* of the interface descriptor           */
    ULONG InterfaceNumber;
    ULONG AlternateSetting;
    ULONG InterfaceClass;
    ULONG InterfaceSubClass;
    ULONG InterfaceProtocol;
    ULONG EndpointCount;        /* = bNumEndpoints, checked              */
    ULONG EndpointOffset[XHCI_PIPE_MAX_ENDPOINTS];
} XHCI_PIPE_IFACE, *PXHCI_PIPE_IFACE;

/*
 * Find interface `interfaceNumber`, alternate `alternate`, in the whole
 * configuration descriptor `config` of `length` bytes, and list the offsets
 * of its endpoint descriptors in the order the device gives them.
 *
 * The walk covers wTotalLength bytes and checks the whole set, not just the
 * interface asked for, because one bad length makes every later offset a
 * guess: XHCI_PIPE_MALFORMED for a header that is not a configuration
 * descriptor, a wTotalLength below 9 or above `length`, any descriptor whose
 * bLength is below 2 or runs past wTotalLength, an interface descriptor
 * shorter than 9 or an endpoint descriptor shorter than 7 bytes, and - in the
 * interface asked for - an endpoint count that disagrees with bNumEndpoints,
 * more than 30 endpoints, an endpoint numbered 0, or two endpoint descriptors
 * naming the same DCI. XHCI_PIPE_NOT_FOUND when the set is sound and the pair
 * is absent. The first match is taken if a device repeats a pair.
 */
ULONG XhciPipeFindInterface(const UCHAR *config, ULONG length,
                            ULONG interfaceNumber, ULONG alternate,
                            PXHCI_PIPE_IFACE iface);

/* ------------------------------------------------------------------ */
/* Endpoint parameters                                                  */
/* ------------------------------------------------------------------ */

typedef struct _XHCI_PIPE_EP {
    ULONG Address;              /* bEndpointAddress                       */
    ULONG Dci;
    ULONG TransferType;         /* XHCI_PIPE_XFER_*                        */
    ULONG DirectionIn;
    ULONG EpType;               /* XHCI_PIPE_EPT_*                         */
    ULONG MaxPacketSize;        /* wMaxPacketSize 10:0                     */
    ULONG MaxBurstSize;         /* HS periodic: wMaxPacketSize 12:11       */
    ULONG Mult;                 /* 0 for every USB 2.0 endpoint            */
    ULONG Interval;             /* Endpoint Context encoding               */
    ULONG ErrorCount;           /* CErr                                    */
    ULONG AverageTrbLength;
    ULONG MaxEsitPayload;
    ULONG BInterval;            /* as the descriptor gave it               */
    ULONG IntervalClamped;      /* bInterval was outside Table 6-12's range */
    /* SuperSpeed (29-A.3, 29-A.6): what the companion descriptors said. */
    ULONG CompanionMissing;     /* no SS companion followed: burst 0      */
    ULONG MaxStreams;           /* bulk: bmAttributes 4:0, unused before
                                 * Phase 31                               */
    ULONG SspIso;               /* the SSP isochronous companion's
                                 * dwBytesPerInterval was used            */
} XHCI_PIPE_EP, *PXHCI_PIPE_EP;

/*
 * The Endpoint Context fields for one endpoint descriptor of a device at
 * `speed` (XHCI_PIPE_SPEED_*). XHCI_PIPE_MALFORMED for a descriptor that is
 * not an endpoint descriptor of at least 7 bytes, an endpoint number of 0, a
 * Max Packet Size of 0 or above 1024, a high-bandwidth count of 3 (bits
 * 12:11 = 11b, reserved), or a Max Packet Size its speed and type do not
 * allow (USB 2.0 5.6.3, 5.7.3, 5.8.3, Table 9-14: LS interrupt <= 8; FS
 * interrupt <= 64; FS bulk 8, 16, 32 or 64; FS isochronous <= 1023; HS bulk
 * 512; HS interrupt and isochronous 1-1024, 513-1024 with one additional
 * transaction and 683-1024 with two); XHCI_PIPE_UNSUPPORTED for a
 * non-default control endpoint, any speed but Full, Low and High, and a
 * Low-Speed bulk or isochronous endpoint (the speed and type refusals come
 * first, so an LS bulk endpoint is UNSUPPORTED whatever its size). Clamps an
 * out-of-range bInterval to Table 6-12's range and says so in
 * IntervalClamped.
 */
ULONG XhciPipeEndpointParams(const UCHAR *endpoint, ULONG speed,
                             PXHCI_PIPE_EP ep);

/*
 * The same for the endpoint descriptor at `offset` of a whole configuration
 * descriptor `config` of `length` bytes (one XhciPipeFindInterface has
 * accepted), at any speed: USB 2.0 speeds are XhciPipeEndpointParams
 * unchanged, and SuperSpeed reads the companions that follow the endpoint
 * descriptor (tasks 29-A.3 and 29-A.6; USB 3.2 9.6.7 and 9.6.8, to verify):
 *
 *   - wMaxPacketSize 10:0: bulk exactly 1024, interrupt and isochronous
 *     1-1024, and 1024 whenever bMaxBurst is nonzero;
 *   - Max Burst = the SS companion's bMaxBurst (0-15); a missing companion
 *     is taken as burst 0 and said so in CompanionMissing, the way Linux
 *     tolerates one, rather than refusing the device;
 *   - Mult = the isochronous companion's bmAttributes 1:0 (3 is reserved:
 *     MALFORMED), 0 for every other type, and 0 always when `lec` is set
 *     (HCCPARAMS2.LEC: the field is reserved and the xHC derives it);
 *   - Max ESIT Payload = wBytesPerInterval for a periodic endpoint, or the
 *     largest payload its burst and Mult allow when the device wrote 0;
 *     above that largest payload is MALFORMED;
 *   - Interval = bInterval - 1 for a periodic endpoint (Table 6-12's
 *     SuperSpeed row, bInterval 1-16, clamped as at High Speed), 0 for bulk;
 *   - at SUPER_PLUS, an isochronous endpoint whose SS companion sets
 *     bmAttributes bit 7 takes its payload from the SuperSpeedPlus
 *     Isochronous Endpoint Companion that must follow (MALFORMED if it does
 *     not), and ignores the SS companion's Mult (USB 3.2 Table 9-28): with
 *     `lec` the 32-bit dwBytesPerInterval is Max ESIT Payload Lo and Hi and
 *     Mult is 0, and one past 24 bits is ESIT_REFUSED; without `lec` the
 *     payload must fit three bursts (48 KiB at 1024 bytes and burst 16) and
 *     Mult is the bursts it needs less one, ESIT_REFUSED otherwise - never
 *     truncated.
 *
 * XHCI_PIPE_BAD_PARAM for NULL or an offset whose 7 bytes are not inside
 * `length`.
 */
ULONG XhciPipeEndpointParamsAt(const UCHAR *config, ULONG length,
                               ULONG offset, ULONG speed, ULONG lec,
                               PXHCI_PIPE_EP ep);

/* ------------------------------------------------------------------ */
/* The BOS descriptor (29-A.3, 29-A.6)                                 */
/* ------------------------------------------------------------------ */

/* bDevCapabilityType (USB 3.2 Table 9-14; to verify). */
#define XHCI_PIPE_CAP_USB2_EXTENSION    0x02UL
#define XHCI_PIPE_CAP_SUPERSPEED        0x03UL
#define XHCI_PIPE_CAP_CONTAINER_ID      0x04UL
#define XHCI_PIPE_CAP_SUPERSPEED_PLUS   0x0AUL
/* Sublink speed attributes kept from a SuperSpeedPlus capability: SSAC is
 * five bits, so up to 32; XHCISNAP needs the few a real device lists. */
#define XHCI_PIPE_BOS_SUBLINKS          8UL

typedef struct _XHCI_PIPE_BOS {
    ULONG Capabilities;         /* bNumDeviceCaps as walked              */
    ULONG Usb2Attributes;       /* USB 2.0 Extension bmAttributes        */
    ULONG HasSuperSpeed;        /* a SuperSpeed USB capability (10 bytes) */
    ULONG SsAttributes;         /* its bmAttributes (bit 1 LTM)          */
    ULONG SsSpeeds;             /* wSpeedsSupported                      */
    ULONG SsFunctionality;      /* bFunctionalitySupport                 */
    ULONG SsU1ExitLatency;      /* bU1DevExitLat, us                     */
    ULONG SsU2ExitLatency;      /* wU2DevExitLat, us                     */
    ULONG HasSuperSpeedPlus;    /* a SuperSpeedPlus capability           */
    ULONG SspAttributes;        /* bmAttributes: SSAC 4:0, SSIC 8:5      */
    ULONG SspFunctionality;     /* wFunctionalitySupport                 */
    ULONG SspSublinks;          /* attributes kept, at most the array's  */
    ULONG SspSublink[XHCI_PIPE_BOS_SUBLINKS];
} XHCI_PIPE_BOS, *PXHCI_PIPE_BOS;

/*
 * Walk a BOS descriptor of `length` bytes and keep what the bus uses or
 * XHCISNAP shows. XHCI_PIPE_MALFORMED for a header that is not a BOS
 * descriptor, a wTotalLength below 5 or above `length`, a capability whose
 * bLength is below 3 or runs past wTotalLength, or a SuperSpeed or
 * SuperSpeedPlus capability shorter than its fixed part (10 and 12 bytes, the
 * latter plus four per sublink attribute it declares). Capabilities of other
 * types are skipped. `bos` is written only on XHCI_PIPE_OK.
 */
ULONG XhciPipeParseBos(const UCHAR *data, ULONG length, PXHCI_PIPE_BOS bos);

/* The DCI for a non-control endpoint address (spec 4.5.1): 2n OUT, 2n+1 IN;
 * 0 for endpoint number 0. */
ULONG XhciPipeDci(ULONG endpointAddress);

/* ------------------------------------------------------------------ */
/* The Configure Endpoint plan                                          */
/* ------------------------------------------------------------------ */

/* Masks are by DCI: bit i stands for DCI i. */
#define XHCI_PIPE_DCI_BIT(dci)      (1UL << ((dci) & 0x1FUL))
#define XHCI_PIPE_ENDPOINT_MASK     0xFFFFFFFCUL    /* DCIs 2..31 */

typedef struct _XHCI_PIPE_PLAN {
    ULONG AddFlags;             /* Input Control Context DW1              */
    ULONG DropFlags;            /* Input Control Context DW0              */
    ULONG ContextEntries;       /* Slot Context DW0 31:27                 */
    ULONG Enabled;              /* the set enabled once it completes      */
} XHCI_PIPE_PLAN, *PXHCI_PIPE_PLAN;

/*
 * The flags for a Configure Endpoint that takes the endpoints `oldMask` out
 * and puts `newMask` in, beside `keepMask` - endpoints of other interfaces
 * that stay as they are. Every DCI in oldMask is dropped and every DCI in
 * newMask added, so a DCI in both is dropped and re-added; Add always carries
 * A0, and D0, D1 and A1 are never set (4.6.6). ContextEntries is the highest
 * DCI in keepMask | newMask, or 1 when that is empty. XHCI_PIPE_BAD_PARAM for
 * a mask with DCI 0 or 1 in it, or keepMask overlapping either other mask.
 */
ULONG XhciPipeConfigurePlan(ULONG keepMask, ULONG oldMask, ULONG newMask,
                            PXHCI_PIPE_PLAN plan);

/* ------------------------------------------------------------------ */
/* SETUP packets                                                        */
/* ------------------------------------------------------------------ */

/* URB function codes (Windows 2000 DDK inc\usbdi.h lines 69-141). */
#define XHCI_PIPE_URB_CONTROL_TRANSFER          0x0008UL
#define XHCI_PIPE_URB_GET_DESC_DEVICE           0x000BUL
#define XHCI_PIPE_URB_SET_DESC_DEVICE           0x000CUL
#define XHCI_PIPE_URB_SET_FEATURE_DEVICE        0x000DUL
#define XHCI_PIPE_URB_SET_FEATURE_INTERFACE     0x000EUL
#define XHCI_PIPE_URB_SET_FEATURE_ENDPOINT      0x000FUL
#define XHCI_PIPE_URB_CLEAR_FEATURE_DEVICE      0x0010UL
#define XHCI_PIPE_URB_CLEAR_FEATURE_INTERFACE   0x0011UL
#define XHCI_PIPE_URB_CLEAR_FEATURE_ENDPOINT    0x0012UL
#define XHCI_PIPE_URB_GET_STATUS_DEVICE         0x0013UL
#define XHCI_PIPE_URB_GET_STATUS_INTERFACE      0x0014UL
#define XHCI_PIPE_URB_GET_STATUS_ENDPOINT       0x0015UL
#define XHCI_PIPE_URB_VENDOR_DEVICE             0x0017UL
#define XHCI_PIPE_URB_VENDOR_INTERFACE          0x0018UL
#define XHCI_PIPE_URB_VENDOR_ENDPOINT           0x0019UL
#define XHCI_PIPE_URB_CLASS_DEVICE              0x001AUL
#define XHCI_PIPE_URB_CLASS_INTERFACE           0x001BUL
#define XHCI_PIPE_URB_CLASS_ENDPOINT            0x001CUL
#define XHCI_PIPE_URB_CLASS_OTHER               0x001FUL
#define XHCI_PIPE_URB_VENDOR_OTHER              0x0020UL
#define XHCI_PIPE_URB_GET_STATUS_OTHER          0x0021UL
#define XHCI_PIPE_URB_CLEAR_FEATURE_OTHER       0x0022UL
#define XHCI_PIPE_URB_SET_FEATURE_OTHER         0x0023UL
#define XHCI_PIPE_URB_GET_DESC_ENDPOINT         0x0024UL
#define XHCI_PIPE_URB_SET_DESC_ENDPOINT         0x0025UL
#define XHCI_PIPE_URB_GET_CONFIGURATION         0x0026UL
#define XHCI_PIPE_URB_GET_INTERFACE             0x0027UL
#define XHCI_PIPE_URB_GET_DESC_INTERFACE        0x0028UL
#define XHCI_PIPE_URB_SET_DESC_INTERFACE        0x0029UL

/* Setup TRB Transfer Type (TRT), spec Table 4-7 (xhci-data-structures.md
 * "Control transfers are two or three TDs"). */
#define XHCI_PIPE_TRT_NO_DATA       0UL
#define XHCI_PIPE_TRT_OUT_DATA      2UL
#define XHCI_PIPE_TRT_IN_DATA       3UL

/*
 * What a control URB carries that reaches the SETUP bytes. Each family
 * reads only its own fields (inc\usbdi.h lines 555-637 give the URB
 * structures they come from):
 *
 *   descriptor   DescriptorType, DescriptorIndex, LanguageId, Length
 *   feature      FeatureSelector, Index                  (no data stage)
 *   status       Index                                  (2 bytes, IN)
 *   vendor/class DirectionIn, ReservedBits, Request, Value, Index, Length
 *   GET_INTERFACE Index; GET_CONFIGURATION nothing       (1 byte, IN)
 */
typedef struct _XHCI_PIPE_CONTROL {
    ULONG Function;             /* XHCI_PIPE_URB_*                         */
    ULONG Length;               /* TransferBufferLength                    */
    ULONG DirectionIn;          /* TransferFlags bit 0 (vendor/class)      */
    ULONG ReservedBits;         /* RequestTypeReservedBits: not read      */
    ULONG Request;
    ULONG Value;
    ULONG Index;
    ULONG DescriptorType;
    ULONG DescriptorIndex;
    ULONG LanguageId;
    ULONG FeatureSelector;
} XHCI_PIPE_CONTROL, *PXHCI_PIPE_CONTROL;

/*
 * The SETUP bytes for one control URB, and the Setup TRB's TRT. 0x08
 * (CONTROL_TRANSFER, whose caller supplies the bytes) and every function this
 * module does not list are XHCI_PIPE_UNSUPPORTED; a Length above 0xFFFF, a
 * field wider than its SETUP slot, and a GET_STATUS or GET_CONFIGURATION / GET_INTERFACE Length shorter than
 * the reply's fixed size (2, 1, 1; wLength is that size whatever the buffer)
 * are XHCI_PIPE_BAD_PARAM. Nothing is written on a refusal.
 */
ULONG XhciPipeBuildSetup(const XHCI_PIPE_CONTROL *control, UCHAR *setup,
                         PULONG trt);

/*
 * A raw SETUP packet a client supplied (URB 0x08): XHCI_PIPE_REFUSED for a
 * standard SET_ADDRESS, SET_CONFIGURATION or SET_INTERFACE, which this bus
 * performs itself - with Address Device, and with Configure Endpoint before
 * the SET_CONFIGURATION / SET_INTERFACE of a SELECT_CONFIGURATION /
 * SELECT_INTERFACE - and which must not reach the ring from a client (spec
 * 4.5.4.1; 4.6.6); otherwise XHCI_PIPE_OK and the TRT from
 * bmRequestType bit 7 and wLength.
 */
ULONG XhciPipeCheckRawSetup(const UCHAR *setup, PULONG trt);

/* ------------------------------------------------------------------ */
/* The buffer split                                                     */
/* ------------------------------------------------------------------ */

#define XHCI_PIPE_PAGE_SIZE         4096UL
#define XHCI_PIPE_PAGE_MASK         0x00000FFFUL
/* The plan's caps: a chunk is at most 32 data TRBs and a TD 128 KB. */
#define XHCI_PIPE_CHUNK_ELEMENTS    32UL
#define XHCI_PIPE_TD_MAX_BYTES      0x00020000UL

typedef struct _XHCI_PIPE_ELEMENT {
    ULONG Page;                 /* which map register, from 0              */
    ULONG Offset;               /* byte offset within that page            */
    ULONG Length;               /* bytes, never past the page's end        */
} XHCI_PIPE_ELEMENT, *PXHCI_PIPE_ELEMENT;

/*
 * Split the first chunk of a buffer that starts `pageOffset` bytes into its
 * first page and is `length` bytes long, for an endpoint whose Max Packet
 * Size is `maxPacketSize`, given `mapRegisters` pages of map registers and
 * room for `maxElements` elements. The chunk is bounded by the smaller of the
 * two counts, by XHCI_PIPE_CHUNK_ELEMENTS and by XHCI_PIPE_TD_MAX_BYTES, and
 * a chunk that does not reach the end of `length` is then rounded down to a
 * multiple of maxPacketSize, since a short packet mid-transfer would end the
 * transfer at the device (USB 2.0 5.8.3). *chunkBytes says how much of
 * `length` it covers and the caller maps the rest as the next chunk; that
 * chunk's pageOffset need not be 0, and the caller recomputes it from its own
 * address. One element per page, so none can cross a 64 KB boundary (spec
 * 6.4.1 note). Length 0 is a zero-length transfer: no element, *chunkBytes 0.
 * XHCI_PIPE_BAD_PARAM for a pageOffset of a page or more, a maxPacketSize of
 * 0, no page or no element to put a nonzero length in, or a non-final chunk
 * that rounds down to nothing (the pages available hold less than one
 * packet).
 */
ULONG XhciPipeSplit(ULONG pageOffset, ULONG length, ULONG maxPacketSize,
                    ULONG mapRegisters, ULONG maxElements,
                    PXHCI_PIPE_ELEMENT elements, PULONG count,
                    PULONG chunkBytes);

/* ------------------------------------------------------------------ */
/* Isochronous URBs                                                     */
/* ------------------------------------------------------------------ */

/* USBD_ISO_PACKET_DESCRIPTOR's layout (Windows 2000 DDK inc\usbdi.h
 * lines 741-748): Offset in, Length and Status out. */
typedef struct _XHCI_PIPE_ISO_PACKET {
    ULONG Offset;
    ULONG Length;
    ULONG Status;
} XHCI_PIPE_ISO_PACKET, *PXHCI_PIPE_ISO_PACKET;

/* How far behind the current frame a StartFrame may lie (usbdi.h:160). */
#define XHCI_PIPE_ISO_START_RANGE   1024UL

/*
 * An isochronous URB's packet table against its buffer: 1..maxCount packets,
 * offsets non-decreasing and none past bufferLength (a packet may be empty),
 * and no packet longer than maxPacket (0: unbounded). XHCI_PIPE_BAD_PARAM for
 * any of those, a NULL table or a bufferLength of 0.
 */
ULONG XhciPipeIsoCheck(const XHCI_PIPE_ISO_PACKET *packets, ULONG count,
                       ULONG bufferLength, ULONG maxCount, ULONG maxPacket);

/* Packet i's length: up to the next packet's offset, the last one up to
 * bufferLength. Only for a table XhciPipeIsoCheck accepted. */
ULONG XhciPipeIsoLength(const XHCI_PIPE_ISO_PACKET *packets, ULONG count,
                        ULONG bufferLength, ULONG i);

/*
 * The page-bounded pieces of a packet `length` bytes long at byte `offset`
 * of a buffer starting `pageOffset` bytes into its first page: 1 or 2,
 * their lengths in lengths[0..1] (lengths[1] 0 for one). 0 for a packet
 * longer than a page, a pageOffset of a page or more, or NULL. An empty
 * packet is one empty piece.
 */
ULONG XhciPipeIsoFragments(ULONG pageOffset, ULONG offset, ULONG length,
                           PULONG lengths);

/* Frames from the first packet to packet i, and frames `packets` packets
 * span, at an Endpoint Context Interval (ESIT = 2^interval microframes).
 * packets <= 62 and interval <= 15 cannot overflow. */
ULONG XhciPipeIsoFrameOf(ULONG i, ULONG interval);
ULONG XhciPipeIsoFrames(ULONG packets, ULONG interval);

/* A StartFrame up to `ahead` frames after `now` or up to
 * XHCI_PIPE_ISO_START_RANGE before it, in the wrapping 32-bit frame domain:
 * XHCI_PIPE_OK, else XHCI_PIPE_REFUSED. */
ULONG XhciPipeIsoStartOk(ULONG startFrame, ULONG now, ULONG ahead);

/* ------------------------------------------------------------------ */
/* Status                                                               */
/* ------------------------------------------------------------------ */

/* USBD_STATUS values, Windows 2000 DDK inc\usbdi.h lines 220-314. */
#define XHCI_PIPE_USBD_SUCCESS              0x00000000UL    /* :220 */
#define XHCI_PIPE_USBD_PENDING              0x40000000UL    /* :221 */
#define XHCI_PIPE_USBD_STALL_PID            0xC0000004UL    /* :232 */
#define XHCI_PIPE_USBD_DEV_NOT_RESPONDING   0xC0000005UL    /* :233 */
#define XHCI_PIPE_USBD_DATA_OVERRUN         0xC0000008UL    /* :236 */
#define XHCI_PIPE_USBD_BUFFER_OVERRUN       0xC000000CUL    /* :240 */
#define XHCI_PIPE_USBD_ENDPOINT_HALTED      0xC0000030UL    /* :249 */
#define XHCI_PIPE_USBD_NO_MEMORY            0x80000100UL    /* :255 */
#define XHCI_PIPE_USBD_INVALID_URB_FUNCTION 0x80000200UL    /* :256 */
#define XHCI_PIPE_USBD_INVALID_PARAMETER    0x80000300UL    /* :257 */
#define XHCI_PIPE_USBD_ERROR_BUSY           0x80000400UL    /* :263 */
#define XHCI_PIPE_USBD_REQUEST_FAILED       0x80000500UL    /* :269 */
#define XHCI_PIPE_USBD_INVALID_PIPE_HANDLE  0x80000600UL    /* :271 */
#define XHCI_PIPE_USBD_NO_BANDWIDTH         0x80000700UL    /* :275 */
#define XHCI_PIPE_USBD_INTERNAL_HC_ERROR    0x80000800UL    /* :279 */
#define XHCI_PIPE_USBD_ERROR_SHORT_TRANSFER 0x80000900UL    /* :284 */
#define XHCI_PIPE_USBD_BAD_START_FRAME      0xC0000A00UL    /* :290 */
#define XHCI_PIPE_USBD_ISOCH_REQUEST_FAILED 0xC0000B00UL    /* :294 */
#define XHCI_PIPE_USBD_CANCELED             0x00010000UL    /* :312 */
/* DEVICE_GONE is absent from the Windows 2000 DDK; WDK 7.1 inc\api\usb.h:459
 * defines it, and design record 13 section 10.5 pairs it with
 * STATUS_DEVICE_NOT_CONNECTED. */
#define XHCI_PIPE_USBD_DEVICE_GONE          0xC0007000UL

/* NTSTATUS values, Windows 2000 DDK inc\ntstatus.h. */
#define XHCI_PIPE_NT_SUCCESS                0x00000000UL    /* :50   */
#define XHCI_PIPE_NT_PENDING                0x00000103UL    /* :220  */
#define XHCI_PIPE_NT_DEVICE_BUSY            0x80000011UL    /* :1114 */
#define XHCI_PIPE_NT_UNSUCCESSFUL           0xC0000001UL    /* :1334 */
#define XHCI_PIPE_NT_INVALID_PARAMETER      0xC000000DUL    /* :1444 */
#define XHCI_PIPE_NT_INSUFFICIENT_RESOURCES 0xC000009AUL    /* :2753 */
#define XHCI_PIPE_NT_DEVICE_NOT_CONNECTED   0xC000009DUL    /* :2780 */
#define XHCI_PIPE_NT_CANCELLED              0xC0000120UL    /* :3999 */

/*
 * The NTSTATUS an IRP completes with for a URB whose status is `usbd`. The
 * table is this driver's choice, not one the DDK states: the
 * named rows are mapped one by one, any other error-class value (bit 31) is
 * STATUS_UNSUCCESSFUL, and any other value is STATUS_SUCCESS.
 */
ULONG XhciPipeNtStatus(ULONG usbd);

/* The USBD_STATUS a Configure Endpoint completion code means for SELECT_
 * CONFIGURATION / SELECT_INTERFACE: 1 is success; 7, 8 and 35 (Resource,
 * Bandwidth and Secondary Bandwidth Error, Table 6-90) are NO_BANDWIDTH;
 * anything else is INTERNAL_HC_ERROR. */
ULONG XhciPipeConfigureUsbdStatus(ULONG completionCode);

#endif /* XHCI_PIPE_H */
