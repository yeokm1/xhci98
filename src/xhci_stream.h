/*
 * xhci_stream.h - the pure half of bulk streams in xhci98.sys (roadmap-hcd.md
 * task 31-A.1; xHCI 1.2c section 4.12, transcribed in
 * docs/usb-xhci-info/xhci-data-structures.md, "Streams").
 *
 * Primary streams only: one Primary Stream Context Array per endpoint, Linear
 * Stream Array (LSA = 1), a Stream ID read as an index into that array, no
 * Secondary Stream Arrays. Every computation the bus needs that touches no
 * register, lock or kernel service is here, so the host suite drives it
 * (test\test_stream.c):
 *
 *   - the controller's limit from HCCPARAMS1.MaxPSASize, and the endpoint's
 *     from its SuperSpeed Endpoint Companion descriptor;
 *   - the plan: how many streams a request is granted, the array's entries
 *     and the Endpoint Context's MaxPStreams;
 *   - the layout of the one common buffer that holds the array and the
 *     stream rings;
 *   - the Stream Context, the Endpoint Context's stream fields, the Set TR
 *     Dequeue Pointer command with a Stream ID, and the doorbell value;
 *   - the stream a Transfer Event names, from its TRB pointer (the event
 *     carries no Stream ID, 6.4.2.1).
 *
 * DDK-free: part of the pure core. C89. IRQL: any.
 */

#ifndef XHCI_STREAM_H
#define XHCI_STREAM_H

#include "xhci_compat.h"
#include "xhci.h"

/* Status codes. */
#define XHCI_STREAM_OK              0UL
#define XHCI_STREAM_BAD_PARAM       1UL /* a caller error                    */
#define XHCI_STREAM_HC_NONE         2UL /* MaxPSASize 0: no streams (5.3.6)  */
#define XHCI_STREAM_EP_NONE         3UL /* the endpoint defines no streams   */
#define XHCI_STREAM_MALFORMED       4UL /* the device's descriptors are bad  */

/*
 * The array this driver builds, at most: a policy, not a limit of either
 * side - 31 streams per endpoint, which bounds the common buffer and the
 * pool a stream endpoint costs (one ring and one pipe object per stream).
 * A power of two, so it is itself a legal array size.
 */
#define XHCI_STREAM_MAX_ENTRIES     32UL
/* LSA = 1 takes MaxPStreams 1..15 (Table 6-8), so the smallest array is
 * 2^(1+1) entries: Stream IDs 1 to 3. */
#define XHCI_STREAM_MIN_ENTRIES     4UL
#define XHCI_STREAM_CONTEXT_BYTES   16UL
/* Stream Context Type (Table 6-13 as transcribed; "to verify" there): 1 is
 * a Primary Transfer Ring, the only type an LSA = 1 array holds. */
#define XHCI_STREAM_SCT_PRIMARY_TR  1UL
/* The common buffer is allocated in whole pages, which is what makes it
 * page-aligned (xhci-data-structures.md section 1). */
#define XHCI_STREAM_PAGE_BYTES      4096UL

/* USB 3.2 Table 9-27, the SuperSpeed Endpoint Companion descriptor. */
#define XHCI_STREAM_DT_SS_COMPANION 0x30UL
#define XHCI_STREAM_COMPANION_BYTES 6UL
#define XHCI_STREAM_MAX_STREAMS_EXP 16UL

/* Endpoint Context DW0 (Table 6-8). */
#define XHCI_STREAM_EP_MAXPSTREAMS_MASK 0x00007C00UL
#define XHCI_STREAM_EP_LSA              0x00008000UL

/* ------------------------------------------------------------------ */
/* Limits and the plan                                                  */
/* ------------------------------------------------------------------ */

/*
 * The largest Primary Stream Context Array the controller takes,
 * 2^(MaxPSASize+1) entries, from the HCCPARAMS1 value `hccparams1` (5.3.6,
 * bits 15:12); 0 when MaxPSASize is 0, "Streams are not supported". An
 * all-ones read (a controller not decoding) is 0 as well.
 */
ULONG XhciStreamHcEntries(ULONG hccparams1);

/*
 * The MaxStreams exponent of the endpoint descriptor at `endpointOffset` of
 * the configuration descriptor `config` (`length` bytes, already walked by
 * XhciPipeFindInterface): the SuperSpeed Endpoint Companion must follow it
 * at once (USB 3.2 9.6.7), and its bmAttributes 4:0 are MaxStreams for a
 * bulk endpoint - 0 none, 1..16 meaning 2^MaxStreams streams. *exponent is
 * 0 and XHCI_STREAM_EP_NONE when no companion follows (a USB 2.0 endpoint);
 * XHCI_STREAM_MALFORMED for a companion shorter than 6 bytes, running past
 * wTotalLength, or with MaxStreams above 16 (reserved).
 */
ULONG XhciStreamCompanionExponent(const UCHAR *config, ULONG length,
                                  ULONG endpointOffset, PULONG exponent);

typedef struct _XHCI_STREAM_PLAN {
    ULONG Entries;      /* array entries, a power of two, 4..32           */
    ULONG Streams;      /* granted: Stream IDs 1..Streams                 */
    ULONG MaxPStreams;  /* Endpoint Context DW0 14:10: Entries =
                         * 2^(MaxPStreams+1)                               */
} XHCI_STREAM_PLAN, *PXHCI_STREAM_PLAN;

/*
 * The streams a request for `requested` gets, on an endpoint whose
 * companion says 2^`exponent` and a controller taking `hcEntries`
 * (XhciStreamHcEntries), under the cap `capEntries` (a power of two, at
 * least XHCI_STREAM_MIN_ENTRIES). Stream ID 0 is reserved, so the
 * array needs one entry more than the streams it serves, rounded up to a
 * power of two, and never under 4; the smaller of the controller's array and
 * the cap bounds it, and the grant is what fits. XHCI_STREAM_HC_NONE for a
 * controller below 4 entries, XHCI_STREAM_EP_NONE for exponent 0,
 * XHCI_STREAM_MALFORMED above 16, XHCI_STREAM_BAD_PARAM for requested 0 or a
 * cap that is not a power of two of at least 4. Nothing is written on a
 * refusal.
 */
ULONG XhciStreamPlan(ULONG requested, ULONG exponent, ULONG hcEntries,
                     ULONG capEntries, PXHCI_STREAM_PLAN plan);

/* ------------------------------------------------------------------ */
/* The common buffer                                                    */
/* ------------------------------------------------------------------ */

typedef struct _XHCI_STREAM_LAYOUT {
    ULONG ArrayBytes;   /* the array, at offset 0                          */
    ULONG RingBase;     /* stream 1's ring; stream n's at RingBase +
                         * (n - 1) * RingBytes                             */
    ULONG RingBytes;
    ULONG TotalBytes;   /* whole pages                                     */
} XHCI_STREAM_LAYOUT, *PXHCI_STREAM_LAYOUT;

/*
 * One page-aligned block per stream endpoint: the array at offset 0, then
 * one ring of `ringTrbs` TRBs per stream, each at a multiple of its own
 * size. With a page-aligned base, a ring whose size is a power of two no
 * larger than a page then lies inside one naturally aligned block of that
 * size, so it crosses neither a page nor a 64 KB boundary (Table 6-1), and
 * the array, at most 32 x 16 = 512 bytes, lies in the first page.
 * XHCI_STREAM_BAD_PARAM for an entry count that is not a power of two in
 * 4..32, a stream count of 0 or not below it, or a ring that is not a power
 * of two of 4 to 256 TRBs.
 */
ULONG XhciStreamLayout(ULONG entries, ULONG streams, ULONG ringTrbs,
                       PXHCI_STREAM_LAYOUT layout);

/* Stream `streamId`'s ring offset in the block; only for 1..streams. */
ULONG XhciStreamRingOffset(const XHCI_STREAM_LAYOUT *layout, ULONG streamId);

/* ------------------------------------------------------------------ */
/* Encoders                                                             */
/* ------------------------------------------------------------------ */

/*
 * One Stream Context (6.2.4.1): DW0 the TR Dequeue Pointer with SCT 3:1 and
 * DCS 0, DW1 the pointer's high half (0), DW2 Stopped EDTLA (0 from
 * software), DW3 reserved. A `ringPA` of 0 with sct 0 and dcs 0 writes the
 * all-zero context Stream ID 0 and every unused ID take.
 * XHCI_STREAM_BAD_PARAM for a pointer with its low four bits set, a dcs
 * above 1 or an SCT above 7, or a nonzero SCT with no pointer.
 */
ULONG XhciStreamContextBuild(volatile ULONG *context, ULONG ringPA,
                             ULONG dcs, ULONG sct);

/*
 * The stream half of an Endpoint Context already built by
 * XhciBuildEndpointContext (xhci_ctx.c, which writes MaxPStreams and LSA as
 * 0): MaxPStreams into DW0 14:10 with LSA (bit 15), and the TR Dequeue
 * Pointer the array's address with DCS 0 - "the TR Dequeue Pointer field
 * references a Primary Stream Context Array" when MaxPStreams > 0 (Table
 * 6-8, 6-10). XHCI_STREAM_BAD_PARAM for MaxPStreams outside 1..15 or an
 * array address of 0 or not 16-byte aligned.
 */
ULONG XhciStreamEndpointContext(volatile ULONG *context, ULONG maxPStreams,
                                ULONG arrayPA);

/*
 * Set TR Dequeue Pointer for one stream (6.4.3.9): DW0 the new pointer with
 * SCT 3:1 and DCS 0, DW2 Stream ID 31:16, DW3 type 16, Endpoint ID 20:16,
 * Slot ID 31:24. The cycle bit is the command ring's to stamp.
 * XHCI_STREAM_BAD_PARAM for slot 0, a DCI outside 1..31, Stream ID 0 or
 * above 0xFFFF (0 is the non-stream command, xhci_ring.c's
 * XhciTrbSetTrDequeue), a pointer of 0 or not 16-byte aligned, a dcs above
 * 1, or an SCT outside 1..7.
 */
ULONG XhciStreamSetDequeueTrb(XHCI_TRB *trb, ULONG slotId, ULONG dci,
                              ULONG streamId, ULONG dequeuePA, ULONG dcs,
                              ULONG sct);

/* The doorbell value for one stream of an endpoint (5.6, Table 5-43): DB
 * Target 7:0 the DCI, DB Stream ID 31:16. */
ULONG XhciStreamDoorbell(ULONG dci, ULONG streamId);

/* ------------------------------------------------------------------ */
/* Events                                                               */
/* ------------------------------------------------------------------ */

/*
 * The Stream ID whose ring holds the TRB at `trbPA`: `rings` is indexed by
 * Stream ID, `count` entries, a NULL entry (ID 0, an unused ID) skipped.
 * A Transfer Event names its TRB and not its stream (6.4.2.1), and every
 * stream ring here is one segment, so the address alone decides. 0 when no
 * ring holds it, or for a pointer not on a TRB boundary.
 */
ULONG XhciStreamFind(const XHCI_RING *const *rings, ULONG count,
                     ULONG trbPA);

#endif /* XHCI_STREAM_H */
