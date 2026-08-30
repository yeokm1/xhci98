/*
 * xhci_xfer.h - the control-transfer engine (src/xhci_xfer.c), Phase 6 batch A.
 *
 * This is pure core in the design doc 03 section 2 sense - computation plus
 * stores into caller-supplied common-buffer memory, no MMIO, no DDK, no IRQL -
 * but it speaks the usbport transfer ABI, so it needs src/xhci_usbport.h beside
 * src/xhci.h. That header is itself DDK-free (test/test_packet.c compiles it on
 * the host), so the property that matters is preserved; what would break it is
 * putting these declarations in xhci.h, which every pure file includes.
 *
 * Three tasks live here:
 *
 *   6-A.1  XhciXferBuildControl / XhciXferSubmitControl - the minimum complete
 *          control-transfer engine: the whole SG list in SgOffset order, split
 *          again at 64 KB physical boundaries, Setup + Data + Status published
 *          as one group.
 *   6-A.2  XhciXferQueue* / XhciXferEvent - per-transfer bookkeeping keyed on
 *          the TRB range, and the event that selects one transfer out of it.
 *   6-A.3  XhciXferCodeInfo - the completion-code mapping, with the residual
 *          arithmetic that rejects an impossible answer instead of reporting a
 *          plausible one.
 *
 * C89 only. IRQL: every function is callable at any IRQL. None of them rings a
 * doorbell or calls a usbport service; the caller does both, after the call.
 */

#ifndef XHCI_XFER_H
#define XHCI_XFER_H

#include "xhci.h"
#include "xhci_usbport.h"

/* ------------------------------------------------------------------ */
/* Status codes                                                        */
/* ------------------------------------------------------------------ */

#define XHCI_XFER_OK                0
#define XHCI_XFER_BAD_PARAM         1
/* The ring cannot hold the whole group. Nothing was written; usbport requeues
 * and retries on a nonzero SubmitTransfer return
 * (docs/usb-xhci-info/usbport-miniport-abi.md section 4, Transfers). */
#define XHCI_XFER_BUSY              2
/* More TRBs than the caller's scratch space, or than policy allows. */
#define XHCI_XFER_TOO_MANY_TRBS     3
/* The SG list does not tile [0, TransferBufferLength) exactly: a gap, an
 * overlap, a zero-length element, or elements left over past the end. */
#define XHCI_XFER_SG_MISMATCH       4
/* An SG element carries a nonzero physical high DWORD. usbport does not mask
 * it - the zero is inherited from its adapter's declared 32-bit width, not
 * enforced at the element (docs/contributing/implementation-invariants.md, "DMA Addresses") -
 * so it is checked, never assumed. */
#define XHCI_XFER_SG_HIGH_ADDRESS   5
/* The direction the SETUP bytes require and the direction usbport's
 * TransferFlags declares disagree. */
#define XHCI_XFER_DIRECTION_CONFLICT 6
/* Task 9-A.1. The isochronous parameter block does not describe something this
 * driver can encode: a bad signature, a packet count of zero, a fragment count
 * outside 1..2, fragments that do not add up to the packet's length, or a packet
 * larger than the endpoint's Max ESIT Payload (which spec 4.14.2.1 p.238 forbids
 * software from defining at all). Separate from SG_MISMATCH because the block is
 * not a scatter/gather list and the diagnoses do not overlap. */
#define XHCI_XFER_ISO_MALFORMED     7
/* The request needs more TRBs than the endpoint's ring can ever hold, so no
 * retry can make it fit. Distinct from XHCI_XFER_BUSY, which is the same ring
 * being full *right now*: one is answered by waiting, the other never is. */
#define XHCI_XFER_ISO_TOO_LARGE     8

/* ------------------------------------------------------------------ */
/* 6-A.3: completion codes                                             */
/* ------------------------------------------------------------------ */

/*
 * The USBD_STATUS vocabulary is the **Windows 2000 DDK's** usbdi.h, which is
 * the header this driver builds against and the one both targets' USB stacks
 * were built from. That is not a detail: `USBD_STATUS_XACT_ERROR`,
 * `USBD_STATUS_BABBLE_DETECTED` and `USBD_STATUS_DATA_BUFFER_ERROR` - the three
 * names ReactOS's usbehci returns, and the three
 * docs/usb-xhci-info/xhci-programming.md carried until Phase 6 batch A - **do not exist in
 * it**; they are later WDK additions. The era-appropriate equivalents are the
 * ones ReactOS's *uhci* miniport uses, which is the closest precedent this
 * project has for a miniport shipping against this header:
 * BUFFER_OVERRUN for babble, DEV_NOT_RESPONDING for a transaction that got no
 * valid response, DATA_OVERRUN for a data-buffer error, STALL_PID for a stall,
 * and INTERNAL_HC_ERROR for everything the mapping cannot name.
 *
 * Values transcribed from C:\NTDDK\inc\usbdi.h rather than recalled;
 * test/test_xfer.c pins them from a second hand-typed copy.
 */
#define XHCI_USBD_STATUS_SUCCESS            ((LONG)0x00000000L)
#define XHCI_USBD_STATUS_STALL_PID          ((LONG)0xC0000004L)
#define XHCI_USBD_STATUS_DEV_NOT_RESPONDING ((LONG)0xC0000005L)
#define XHCI_USBD_STATUS_DATA_OVERRUN       ((LONG)0xC0000008L)
#define XHCI_USBD_STATUS_BUFFER_OVERRUN     ((LONG)0xC000000CL)
#define XHCI_USBD_STATUS_INTERNAL_HC_ERROR  ((LONG)0x80000800L)
#define XHCI_USBD_STATUS_CANCELED           ((LONG)0x00010000L)
/*
 * The answer for a pipe the *xHC* would not schedule - Configure Endpoint
 * completion codes 7, 8 and 35 (task 7a-A.1). Unlike the three names batch 6-A had
 * to replace, this one **is** in the Windows 2000 DDK's usbdi.h, at
 * 0x80000700 - checked there rather than assumed from the pattern of the
 * others, because the whole trap that list records is a name that reads as
 * era-appropriate and is not.
 */
#define XHCI_USBD_STATUS_NO_BANDWIDTH       ((LONG)0x80000700L)
/*
 * Task 9-A.1's two, both transcribed from C:\NTDDK\inc\usbdi.h like the rest.
 *
 * `DATA_UNDERRUN` is the per-packet status for an isochronous IN packet the
 * device answered with fewer bytes than were asked for, and it is not a guess at
 * an idiom: usbport's own isoch completion path **special-cases exactly this
 * value** - it stores it into the URB's packet status, logs it, then replaces it
 * with 0 and does not count it in `URB->ErrorCount`
 * (docs/usb-xhci-info/usbport-miniport-abi.md, "Isochronous transfers"). A short isoch read is
 * therefore something the other side already expects a miniport to report this
 * way, and reporting plain Success instead would throw away the distinction it
 * bothered to make.
 *
 * `NOT_ACCESSED` is the packet the controller never serviced - a Missed Service
 * Error, whose data "shall be lost" (4.10.3.2 p.187). Unlike the one above,
 * usbport does count it, which is correct: a packet that never went on the wire
 * is an error to whoever asked for it.
 */
#define XHCI_USBD_STATUS_DATA_UNDERRUN      ((LONG)0xC0000009L)
#define XHCI_USBD_STATUS_NOT_ACCESSED       ((LONG)0xC000000FL)
/*
 * The two "this request was not something the miniport can serve" answers, also
 * from the same header. `INVALID_PARAMETER` is the isochronous request whose own
 * parameter block this driver cannot encode - too many packets for the ring, a
 * fragment count outside 1..2, a packet above Max ESIT Payload - and
 * `INVALID_PIPE_HANDLE` is an isochronous request arriving on a pipe configured
 * as something else. Neither is a transfer that failed on the bus, and reporting
 * one as `INTERNAL_HC_ERROR` would send a reader looking at the controller.
 */
#define XHCI_USBD_STATUS_INVALID_PARAMETER  ((LONG)0x80000300L)
#define XHCI_USBD_STATUS_INVALID_PIPE_HANDLE ((LONG)0x80000600L)

/*
 * What a completion code means to a *transfer* - which is not the same
 * question XhciRingClassifyEvent answers. That one decides ring ownership and
 * recovery from position and Kind; this one decides what usbport is told.
 */
#define XHCI_XFER_CC_SUCCESS    0   /* the data moved                          */
#define XHCI_XFER_CC_SHORT      1   /* success, with fewer bytes than asked for */
#define XHCI_XFER_CC_ERROR      2   /* the transfer failed                     */
#define XHCI_XFER_CC_CANCELED   3   /* codes 26-28: software stopped the ring  */
#define XHCI_XFER_CC_INVALID    4   /* unassigned, or impossible on this ring  */

typedef struct _XHCI_XFER_CODE {
    ULONG Class;            /* XHCI_XFER_CC_*                                */
    LONG UsbdStatus;        /* what UsbPortCompleteTransfer is handed         */
    /* The residual field means "bytes not transferred" only for the ordinary
     * codes. For Stopped - Short Packet it is the Event Data Transfer Length
     * Accumulator instead (Table 6-38, p.440), which is a running *total* rather
     * than a residual, so `sum - residual` may not be applied to it. Cleared for
     * every code whose length field this driver may not read as a residual -
     * which is not the same as "may not read at all": the EDTLA is a real byte
     * count, maintained per endpoint whether or not software posts an Event Data
     * TRB (4.11.5.2 p.209-210), and `XhciXferQueueStopped` takes it directly. */
    ULONG ResidualIsBytes;
    /* Controller-level failure rather than a statistic
     * (docs/contributing/implementation-invariants.md, "Fatal Errors"). **Two
     * architected codes carry it - Event Lost (32) and Undefined Error (33) -
     * plus every code in the vendor error range 192-223**, which Table 6-90
     * directs software to read as the second of them. Round 8 found 33 and the
     * range missing; **audit round 9 found this comment still saying "two
     * codes"** after round 8's own change had made three readings carry it.
     *
     * Event Lost says the controller dropped events, and a dropped event is not
     * a late one - every completion here is matched by identity - so the
     * transfer it belonged to has nothing left that can resolve it.
     *
     * Undefined Error is fatal because Table 6-90 says it is: "An Undefined
     * Error shall be treated as a fatal error by software" (p.469). It had been
     * filed with the ordinary INTERNAL_HC_ERROR codes, which read that sentence
     * as a severity label rather than as the instruction it is. The vendor error
     * range 192-223 inherits it by the same table's own rule - "If software does
     * not recognize the code, it shall interpret this range of vendor defined
     * values as a Undefined Error condition" (p.470) - and this driver
     * recognises none of them. */
    ULONG Fatal;
    /* Fatal to the **slot**, not to the controller, which is a distinction
     * Table 6-90 draws and this structure did not until audit round 8.
     * Incompatible Device Error is "fatal as far as the Slot is concerned.
     * Software shall issue a Disable Slot Command to recover" (p.468), so
     * endpoint-level halt recovery cannot discharge it however well it runs -
     * and escalating it to a controller invalidation would take down every other
     * device to answer for one.
     *
     * **`xhciDevSlotFatalEvent` is what routes it to the device teardown**,
     * which owes exactly that Disable Slot. Audit round 9 found this naming
     * `xhciEpRecoveryNeeded` instead, which is the function beside it in both
     * transfer-event paths and reads only the endpoint's half of the code.
     * There are three call sites and none of them is that one: the bulk and
     * isochronous transfer-event paths call it directly, and the command path
     * reaches it through `XhciSlotCommandSlotFatal` - because "this error may be
     * returned by any command or transfer" (p.468) and round 9 found the command
     * half of that sentence unimplemented. */
    ULONG SlotFatal;
} XHCI_XFER_CODE, *PXHCI_XFER_CODE;

/*
 * Decode one Transfer Event completion code. Returns XHCI_XFER_OK for every
 * code Table 6-90 assigns to a Transfer Event on a control/bulk/interrupt ring,
 * and XHCI_XFER_BAD_PARAM for anything else - including 0, the isoch-only
 * codes, and both command-ring families - with Class left as
 * XHCI_XFER_CC_INVALID so a caller that ignores the return still fails visibly
 * rather than treating an unknown code as success.
 */
ULONG XhciXferCodeInfo(ULONG completionCode, PXHCI_XFER_CODE info);

/* ------------------------------------------------------------------ */
/* 6-A.1: building a control transfer                                  */
/* ------------------------------------------------------------------ */

/*
 * Policy caps. A control transfer is 1 Setup TRB + the data TRBs + 1 Status
 * TRB, and the data half is bounded by two independent things: how many SG
 * elements usbport hands over, and the 64 KB physical-boundary split each one
 * may need.
 *
 * usbport's elements are page-granular (Phase 3 task 10, both builds), and a
 * 4 KB page never spans a 64 KB boundary, so on the measured path a transfer of
 * N pages costs N data TRBs. The split is still performed, because "page
 * granular" is a property of the two builds that were read rather than of the
 * ABI, and the cost of being wrong about it is silent data corruption on some
 * controllers (spec 6.4.1 note).
 *
 * XHCI_XFER_MAX_DATA_TRBS is therefore the number that must not be exceeded by
 * MaxTransferSize, and task 6-B.1 is what caps that: 32 page-granular elements
 * is 128 KB, comfortably above the 4 KB usbport uses for the default pipe.
 */
#define XHCI_XFER_MAX_DATA_TRBS     32
#define XHCI_XFER_MAX_CONTROL_TRBS  (XHCI_XFER_MAX_DATA_TRBS + 2)

/*
 * What SubmitTransfer knows, gathered into one argument. Everything here comes
 * from the callback's own parameters; nothing is read back out of usbport's
 * record later, which is what makes the builder testable without one.
 */
typedef struct _XHCI_CONTROL_REQUEST {
    /* The 8 SETUP bytes, from TransferParameters->SetupPacket. */
    XHCI_SETUP_PACKET Setup;
    /* TransferParameters->TransferBufferLength. **Not** wLength: the two may
     * legitimately differ ("communicating with some non-compliant devices may
     * require violating this rule. The transfer lengths managed by the xHC
     * depend strictly on the TRB Length fields", p.193), and this is the one
     * the buffer was mapped for. Zero is the common enumeration case and the
     * one that must not index the SG list at all (batch 6-0). */
    ULONG TransferLength;
    /* TransferParameters->TransferFlags & USBD_TRANSFER_DIRECTION_IN. Carried
     * to be checked against the SETUP bytes, not to decide direction. */
    ULONG TransferFlagsIn;
    /* EP0's Max Packet Size - 8, 16, 32 or 64. Only the TD Size field needs it
     * (spec 4.11.2.4), and a zero would divide by it. */
    ULONG MaxPacketSize;
    /* Never NULL - it is &Transfer->SgList, an interior pointer - but routinely
     * empty (batch 6-0). Consulted only when TransferLength is nonzero. */
    const USBPORT_SCATTER_GATHER_LIST *SgList;
} XHCI_CONTROL_REQUEST, *PXHCI_CONTROL_REQUEST;

/*
 * Where each stage landed in the built TRB array, and the TD partition
 * XhciRingEnqueueTdGroup needs. TdLengths is 2 or 3 entries: Setup, then Data
 * if there is one, then Status.
 */
typedef struct _XHCI_CONTROL_LAYOUT {
    ULONG TrbCount;
    ULONG DataFirst;        /* index into the TRB array, or XHCI_XFER_NO_INDEX */
    ULONG DataCount;
    ULONG TdLengths[3];
    ULONG TdCount;
} XHCI_CONTROL_LAYOUT, *PXHCI_CONTROL_LAYOUT;

/*
 * Encode a control transfer into `out`, which must hold `capacity` TRBs.
 * Touches no ring: this is the "encode first, then one call site publishes"
 * split, so a test can inspect every field of every TRB.
 *
 * The Cycle Bit is not set here - XhciRingEnqueueTdGroup owns it.
 */
ULONG XhciXferBuildControl(const XHCI_CONTROL_REQUEST *request,
                           XHCI_TRB *out,
                           ULONG capacity,
                           PXHCI_CONTROL_LAYOUT layout);

/*
 * Build, preflight and publish one control transfer onto `ring`, and record it
 * in `queue` as `transfer`. `scratch` is caller-supplied space for the built
 * TRBs (XHCI_XFER_MAX_CONTROL_TRBS entries is always enough).
 *
 * Returns XHCI_XFER_BUSY having written nothing at all if the whole group does
 * not fit - the ring-full contract of docs/contributing/implementation-invariants.md, "Ring
 * Full and Backpressure". On XHCI_XFER_OK the group is published and the
 * caller must ring DB[slotId] with DCI 1, once, afterwards.
 */
ULONG XhciXferSubmitControl(PXHCI_TRANSFER_QUEUE queue,
                            PXHCI_RING ring,
                            const XHCI_CONTROL_REQUEST *request,
                            PXHCI_TRANSFER transfer,
                            PVOID transferParameters,
                            XHCI_TRB *scratch,
                            ULONG scratchCount);

/* ------------------------------------------------------------------ */
/* 7a-A.2: building a non-control transfer (interrupt, and bulk from 8-A.1) */
/* ------------------------------------------------------------------ */

/*
 * What SubmitTransfer knows about a **non-control** transfer.
 *
 * Smaller than the control request because there is no setup packet and no
 * three-TD structure: an interrupt or bulk transfer is one TD of Normal TRBs
 * (spec 4.11.2.2), and the direction is the endpoint's rather than something the
 * request states.
 */
typedef struct _XHCI_NORMAL_REQUEST {
    /* TransferParameters->TransferBufferLength. Zero is legal and means one
     * zero-length Normal TRB, which is how a zero-length packet is sent - not
     * "no TRBs", which would be a TD the xHC never completes. */
    ULONG TransferLength;
    /*
     * The **endpoint's** direction, taken from bEndpointAddress bit 7 and
     * therefore from the same field the Endpoint Context's EP Type came from.
     * usbport's `TransferFlags` direction is checked against it rather than used
     * (see XhciXferBuildNormal), for the same reason the control builder checks
     * it against the SETUP bytes: two statements of a direction that disagree
     * are a malformed request, and picking one would move bytes the wrong way.
     */
    ULONG DirectionIn;
    /* The endpoint's Max Packet Size, from the Endpoint Context this driver
     * programmed. Only the TD Size field needs it (spec 4.11.2.4) and a zero
     * would divide by it. */
    ULONG MaxPacketSize;
    /* usbport's list. Never NULL - it is an interior pointer - and empty for a
     * zero-length transfer, which is why it is consulted only when
     * TransferLength is nonzero (batch 6-0). */
    const USBPORT_SCATTER_GATHER_LIST *SgList;
} XHCI_NORMAL_REQUEST, *PXHCI_NORMAL_REQUEST;

/*
 * Encode one interrupt or bulk transfer into `out` as a single TD of Normal
 * TRBs - the two are the same shape here, and the difference between them lives
 * entirely in the Endpoint Context (task 8-A.1).
 * `transferFlagsIn` is usbport's own direction bit, checked against the
 * endpoint's. Touches no ring; the Cycle Bit is XhciRingEnqueueTdGroup's.
 * `trbCount` receives the number written.
 */
ULONG XhciXferBuildNormal(const XHCI_NORMAL_REQUEST *request,
                          ULONG transferFlagsIn,
                          XHCI_TRB *out,
                          ULONG capacity,
                          ULONG *trbCount);

/*
 * Build, preflight and publish one interrupt or bulk transfer onto `ring`, and
 * record it
 * in `queue` as `transfer`. Same all-or-nothing contract as the control
 * submitter: XHCI_XFER_BUSY having written nothing at all if the TD does not
 * fit. On XHCI_XFER_OK the caller must ring DB[slotId] with the endpoint's DCI,
 * once, afterwards.
 */
ULONG XhciXferSubmitNormal(PXHCI_TRANSFER_QUEUE queue,
                           PXHCI_RING ring,
                           const XHCI_NORMAL_REQUEST *request,
                           ULONG transferFlagsIn,
                           PXHCI_TRANSFER transfer,
                           PVOID transferParameters,
                           XHCI_TRB *scratch,
                           ULONG scratchCount);

/* ------------------------------------------------------------------ */
/* 6-A.2: the pending queue and the event                              */
/* ------------------------------------------------------------------ */

VOID XhciXferQueueInit(PXHCI_TRANSFER_QUEUE queue);

/* ------------------------------------------------------------------ */
/* 8-A.1: the ring-full backpressure latch                             */
/* ------------------------------------------------------------------ */

/*
 * Record that a submission was refused because the ring could not hold it.
 *
 * Takes the ring so the free count is read here rather than by the caller: the
 * whole point of the latch is that the count is the one that was true when the
 * refusal happened, and a caller that read it a line earlier or later would arm
 * against a different ring than the one that refused. Re-arming an already-armed
 * queue is deliberate and is how a second refusal at a higher free count raises
 * the bar rather than leaving the old one standing.
 */
VOID XhciXferQueueArmRetry(PXHCI_TRANSFER_QUEUE queue, const XHCI_RING *ring);

/*
 * Has enough changed since the refusal to be worth re-offering? Returns 1 when
 * the queue is armed and the ring now has **strictly more** free TRBs than it did
 * at the refusal - the terminating condition described on XHCI_TRANSFER_QUEUE.
 *
 * Answering is not consuming: `XhciXferQueueDisarmRetry` is separate, so a caller
 * that cannot act on the answer (no endpoint extension, controller not admitted)
 * leaves the debt standing instead of dropping it.
 */
ULONG XhciXferQueueRetryDue(const XHCI_TRANSFER_QUEUE *queue,
                            const XHCI_RING *ring);

/*
 * Clear the latch, counting the re-offer if `asked` is nonzero. The two are one
 * call because a disarm that forgot to count would make the mechanism invisible
 * on a target, and a count without a disarm would re-offer for ever.
 */
VOID XhciXferQueueDisarmRetry(PXHCI_TRANSFER_QUEUE queue, ULONG asked);

/*
 * What one Transfer Event asks the caller to do. Everything needed for the
 * UsbPortCompleteTransfer call is here, because that call happens after the
 * controller lock is dropped and the queue may have moved on by then
 * (design doc 05 section 7).
 */
#define XHCI_XFER_ACTION_NONE       0   /* deferred, trailing, or not ours   */
#define XHCI_XFER_ACTION_COMPLETE   1   /* complete the listed transfers     */

typedef struct _XHCI_XFER_EVENT_RESULT {
    ULONG Action;
    /* A list, not one transfer: retiring jumps the dequeue pointer past the
     * matched TD, so any transfer still queued *behind* it has had its TRBs
     * reclaimed in the same store and must be completed too
     * (docs/contributing/implementation-invariants.md, "Completion Matching"). Threaded
     * through XHCI_TRANSFER.Next, oldest first. */
    PXHCI_TRANSFER Completed;
    ULONG CompletedCount;
    /*
     * Software must act before this endpoint is usable again. **Neither which
     * command it needs, nor even that it has stopped, may be inferred from this
     * flag alone.**
     *
     * Read `RefusedRetire` first: when that is also set the endpoint is still
     * **Running** and the transfer is still queued, and what is owed is a Stop
     * Endpoint with a drain continuation - not a recovery command at all.
     *
     * With `RefusedRetire` clear the endpoint really has stopped, and then the
     * usual case is Halted, needing Reset Endpoint then Set TR Dequeue Pointer
     * (p.172, p.177), but a TRB Error puts the endpoint in **Error** instead,
     * where a Set TR Dequeue Pointer alone is both necessary and sufficient
     * (4.8.3 p.149) and a Reset Endpoint is illegal (4.6.8 p.118 restricts it
     * to Halted). The caller reads the completion code to tell them apart. This
     * contract said "is halted" until the batch-9-0 review round 7, which is an
     * invitation to issue the wrong command.
     */
    ULONG NeedsRecovery;
    /*
     * **A stronger request than `NeedsRecovery`, and a different one.** The
     * ring and this transfer's record disagree about where its TD ends, so
     * nothing was retired and the transfer is **still queued** - `Action` is
     * `XHCI_XFER_ACTION_NONE` and `Completed` is empty.
     *
     * The endpoint is *Running*: nothing halted it, so it must not be given a
     * Set TR Dequeue Pointer (legal only in Error or Stopped, 4.6.10 p.126)
     * and its queue must not be answered, because a running endpoint may still
     * own or have prefetched the TRBs those transfers name. The caller owes a
     * Stop Endpoint with a drain continuation, which is what makes both legal.
     *
     * Routing this through the ordinary `NeedsRecovery` handling is what the
     * batch-9-0 review found twice, once per path: it reaches
     * `xhciEpRecoveryNeeded` with a Short Packet code and halts a healthy
     * endpoint.
     */
    ULONG RefusedRetire;
    /* The controller reported a condition no transfer can recover from
     * (Event Lost). Escalate; do not treat it as this transfer's problem. */
    ULONG Fatal;
} XHCI_XFER_EVENT_RESULT, *PXHCI_XFER_EVENT_RESULT;

/*
 * Match one Transfer Event to its transfer and decide what happens to it.
 *
 * `eventDw2` and `eventDw3` are the event TRB's own words, passed whole rather
 * than pre-decoded, so the ED flag, the residual and the completion code are
 * read here once and cannot be read differently by two callers. `slotId` and
 * `dci` are what the caller expects this ring to be - the event's own Slot ID
 * and Endpoint ID are checked against them, because an event for another
 * endpoint that happened to resolve onto this ring would complete the wrong
 * transfer.
 *
 * Every *event* it can be handed answers XHCI_XFER_OK with an Action - a
 * rejected one is XHCI_XFER_ACTION_NONE plus a counter, never an error return,
 * because the trailing events a single TD can legitimately produce are the
 * expected case rather than a fault. XHCI_XFER_BAD_PARAM is reserved for a NULL
 * argument, which is a caller bug rather than an event.
 *
 * An event whose Slot ID or Endpoint ID is not this endpoint's is counted and
 * dropped, **including one carrying a fatal completion code**. Escalating that
 * belongs to the caller that routed the event here: a Transfer Event naming a
 * slot or DCI nobody has open never reaches any queue, so a queue cannot be the
 * place it is noticed.
 */
ULONG XhciXferEvent(PXHCI_TRANSFER_QUEUE queue,
                    PXHCI_RING ring,
                    ULONG slotId,
                    ULONG dci,
                    ULONG eventTrbPA,
                    ULONG eventDw2,
                    ULONG eventDw3,
                    PXHCI_XFER_EVENT_RESULT result);

/*
 * Task 9-0.2. Settle one transfer whose TD ended on a mid-TD Short Packet Event
 * and whose promised tail (4.10.1.1.2 p.175) never arrived.
 *
 * **Callable only at the end of a drain pass that observed the event ring
 * empty.** That is the whole content of the fix: until the ring is empty, the
 * tail may be sitting in it, and retiring the TD re-lets TRBs a delayed tail
 * would then be matched against. The caller owns that gate; this function
 * cannot check it.
 *
 * Note the gate is the **observation**, not which exit the drain loop took. A
 * pass that stopped at `XHCI_DPC_MAX_EVENTS` qualifies if a peek then shows the
 * ring empty, and a pass that stopped there with events still in it does not.
 * The two were conflated in the first draft, and gating on the exit stranded a
 * deferral whenever the bound and the controller's last event coincided - see
 * `XhciEventDpc`.
 *
 * One transfer per call, so the caller loops:
 *
 *   `Action` == `XHCI_XFER_ACTION_COMPLETE`   one was settled and is in
 *                                             `Completed`; keep going.
 *   `Action` == `XHCI_XFER_ACTION_NONE` and
 *   `NeedsRecovery` == 0                      the queue holds none; stop.
 *   `Action` == `XHCI_XFER_ACTION_NONE` and
 *   `NeedsRecovery` == 1                      the ring refused the retire. The
 *                                             transfer is **still queued** and
 *                                             must not be completed until the
 *                                             endpoint has been stopped: it is
 *                                             Running, so its TRBs are still
 *                                             the xHC's. Stop, then drain.
 */
ULONG XhciXferDrainSettled(PXHCI_TRANSFER_QUEUE queue,
                           PXHCI_RING ring,
                           PXHCI_XFER_EVENT_RESULT result);

/*
 * How many transfers on this queue are holding a deferral right now.
 *
 * Derived by walking, never stored: it is the last term of the partition
 * `MidTdDeferrals == tailed + tailedSpurious + retired + lost + armed`, and a
 * stored copy of
 * something the state already determines can drift from it - which is the shape
 * repo audit finding 23 removed from the mid-TD accounting once already.
 */
ULONG XhciXferDeferralsArmed(const XHCI_TRANSFER_QUEUE *queue);

/*
 * Detach every queued transfer, in order, without touching the ring - the
 * disconnect and teardown path (task 6-B.5), and the only way a transfer whose
 * completion event will never arrive leaves the queue. The caller completes
 * them with `usbdStatus` after dropping the lock.
 */
PXHCI_TRANSFER XhciXferQueueDrain(PXHCI_TRANSFER_QUEUE queue,
                                  LONG usbdStatus,
                                  ULONG *count);

/*
 * Take one named transfer off the queue, leaving the rest in order. Returns 1 if
 * it was there.
 *
 * The cancellation path (`AbortTransfer`) is what needs this, and it is
 * deliberately the *whole* of what this layer does about a cancellation: the
 * transfer's TRBs stay on the ring, because reclaiming them means Stop Endpoint
 * and Set TR Dequeue Pointer and those cannot be issued from a callback that may
 * not wait (task 7a-B.2 owns the machine that can). What it does buy is the part
 * that cannot wait either - after this the transfer can never be completed a
 * second time, which is the difference between a cancellation and a
 * use-after-free of a record usbport has reclaimed.
 */
ULONG XhciXferQueueRemove(PXHCI_TRANSFER_QUEUE queue, PXHCI_TRANSFER transfer);

/*
 * Does any transfer still on this queue occupy the TRB at `index`? Returns 1 if
 * one does.
 *
 * The cancellation path (task 7a-B.2) needs it to answer the only question a
 * stopped ring poses: which of the TRBs still outstanding belong to a transfer
 * that survived, and which are the leftovers of transfers usbport has taken
 * back. **The queue is the record** - there is no stored list of cancelled
 * ranges to keep in step with it, because a range list and the queue it
 * describes are two statements of one fact and this driver has already paid for
 * that shape once (the Chain-flag walk in XhciRingTdBounds exists for the same
 * reason).
 *
 * Walks the same way XhciXferEvent resolves an event's owner, so "this index
 * belongs to that transfer" has one answer in the file rather than two.
 */
ULONG XhciXferQueueOwnsIndex(const XHCI_TRANSFER_QUEUE *queue,
                             const XHCI_RING *ring,
                             ULONG index);

/*
 * Latch what a **forced Stopped Transfer Event** measured onto the transfer that
 * owns the TRB it names, without completing or retiring anything.
 *
 * A Stop Endpoint forces one of these before its command completion, unless the
 * ring was already Halted (4.6.9 p.122 and p.124). The three codes carry three
 * different things and each is handled as what it is:
 *
 *   26 Stopped - a real residual: "TRB Transfer Length set to the residual bytes
 *      to transfer", so the length is the spec's `sum - residual` arithmetic.
 *   27 Stopped - Length Invalid - the *field* is invalid, not the length:
 *      "software shall ignore the TRB Transfer Length field of the Transfer
 *      Event, and simply sum of the TRB Transfer Length fields of all Transfer
 *      TRBs in the TD executed **prior to** the TRB referenced by the Transfer
 *      Event" (4.6.9 p.122) - prior to, where 26 is up to and including.
 *   28 Stopped - Short Packet - the **EDTLA**, which is a running *total* of the
 *      bytes the TD has moved rather than a residual, and is maintained per
 *      endpoint by the xHC whether or not software ever posts an Event Data TRB
 *      (4.11.5.2 p.209-210). It is taken directly, with no sum and no
 *      subtraction. Batch 6-A had excluded it on the premise that nothing
 *      accumulates without Event Data TRBs, which that section refutes.
 *
 * The number matters because `AbortTransfer` has to report the bytes the
 * cancelled transfer moved, and this is the only event that ever measures them:
 * a stopped TD produces no completion of its own.
 *
 * It does **not** set `XHCI_XFER_FLAG_LENGTH_FIXED`: this is progress on a
 * transfer that may still be resumed by a doorbell, and its real completion has
 * to stay free to report the final count.
 *
 * Returns 1 when a length was latched. Everything else - an event for no
 * transfer, a code carrying no byte count, a transfer whose length is already
 * fixed - is a 0 and no change.
 */
ULONG XhciXferQueueStopped(PXHCI_TRANSFER_QUEUE queue,
                           PXHCI_RING ring,
                           ULONG eventTrbPA,
                           ULONG eventDw2);

/* ------------------------------------------------------------------ */
/* 9-A.1: building an isochronous transfer                             */
/*                                                                     */
/* The caps and XHCI_ISO_LAYOUT are in src/xhci.h with XHCI_TRANSFER   */
/* and XHCI_TRANSFER_QUEUE, on this file's usual split: the storage a  */
/* record needs is declared there, the operations on it here.          */
/* ------------------------------------------------------------------ */

/*
 * Whether this submission may carry explicit Frame IDs, and what to measure them
 * against. Filled by the device layer, because two of the three terms are MMIO
 * or capability reads and this file touches neither.
 *
 * `Allowed` is the conjunction of two independent conditions, and **both** are
 * load-bearing:
 *
 *   - **HCCPARAMS1.CFC.** With CFC = 0 the spec does not merely prefer SIA, it
 *     says software "shall set SIA = '1' in all subsequent TDs of the data flow"
 *     and that the xHC "may ... ignore the Frame ID fields in subsequent Isoch
 *     TDs" (4.11.2.5 p.200). Only the first TD of a flow may carry one, and
 *     "the first TD of a flow" is a state this driver would have to track across
 *     underruns, stops and doorbells to know.
 *   - **The frame domain is congruent to MFINDEX.** usbport's per-packet
 *     `FrameNumber` is in the domain *this driver publishes* through
 *     `Get32BitFrameNumber`, and that number is only congruent to MFINDEX's
 *     Frame Index while the controller has been running continuously - the
 *     halted path advances it by one per call rather than per frame. A Frame ID
 *     taken from it while it is not congruent names a different, real frame.
 *
 * When either fails, every TD in the submission carries SIA = 1, which is always
 * legal ("If this flag is set ('1'), the Frame ID is ignored and the Isoch TD is
 * scheduled as soon as possible", Table 6-34) and is what the pre-9-A note in
 * docs/usb-xhci-info/xhci-data-structures.md already recommended.
 */
typedef struct _XHCI_ISO_FRAME_POLICY {
    ULONG Allowed;
    /*
     * **The current frame in the driver's published 32-bit domain**, not the
     * 11-bit Frame Index, and the second review round of batch 9-A is why.
     *
     * A window test done in 11 bits cannot tell a near-future stamp from a stale
     * one a whole lap behind: at current frame 5,000 a stamp of 3,000 is 2,000
     * frames in the past, but their low-11-bit distance is 48, so it read as 48
     * frames ahead and got an explicit Frame ID naming a frame that had already
     * gone. The 32-bit subtraction has no such alias - a stale stamp produces a
     * distance near 2^32, which no window accepts - and it is available for
     * nothing extra, because `FrameCongruent` already means the published number
     * and MFINDEX's Frame Index agree in their low 11 bits.
     *
     * Meaningful only under `Allowed`.
     */
    ULONG CurrentFrame;
    /* HCSPARAMS2's IST, already in frames (XHCI_HC_INFO.IstFrames). */
    ULONG IstFrames;
} XHCI_ISO_FRAME_POLICY, *PXHCI_ISO_FRAME_POLICY;

/*
 * Is `frameNumber` - one packet's stamp, in the driver's published 32-bit frame
 * domain - a Frame ID this submission may use? Returns 1 and writes the 11-bit
 * Frame ID, or 0.
 *
 * The window is the spec's (4.11.2.5 p.199), and every comparison in it is a
 * **distance mod 2048** rather than a magnitude, because a Frame ID space that
 * wraps every 2,048 frames has no ordering: 2047 is one frame before 0, and a
 * driver that compared the two numbers would refuse every schedule that straddled
 * the wrap and accept every one that had already expired past it.
 *
 *   Start = (current + IST + 1) MOD 2048     "should not" schedule before
 *   End   = (current + 895)     MOD 2048     "shall not" schedule after
 *
 * A packet stamped in the past therefore fails here rather than being clamped,
 * and the caller drops the whole submission to SIA: a late request scheduled ASAP
 * is one interval late, where the same request given an expired Frame ID is a
 * Missed Service Error per TD until the pipe resynchronizes (p.186).
 */
ULONG XhciXferFrameIdUsable(const XHCI_ISO_FRAME_POLICY *policy,
                            ULONG frameNumber,
                            ULONG *frameId);

/*
 * What SubmitIsoTransfer knows, gathered the way the other two request
 * structures are. Everything comes from the callback's arguments and from the
 * Endpoint Context this driver programmed; nothing is read back out of usbport's
 * record later.
 */
typedef struct _XHCI_ISO_REQUEST {
    /* usbport's parameter block. Never NULL at the real call site - the
     * allocator sets the pointer and the dispatch flag from one condition in one
     * basic block (task 9-0.1) - and checked anyway, because "the caller cannot
     * produce it" is a statement about two disassembled builds. */
    const USBPORT_ISO_TRANSFER *Iso;
    /* The **endpoint's** direction, from the EP Type this driver programmed, for
     * the same reason the normal builder takes it that way: usbport's own
     * TransferFlags bit is a second opinion to check against, not the source. */
    ULONG DirectionIn;
    ULONG MaxPacketSize;
    /* The Endpoint Context's Max Burst Size, which TBC and TLBPC are computed
     * against (4.11.2.3 p.197). Taken from the context rather than recomputed,
     * so the numbers in the TRB describe the pipe the xHC actually has. */
    ULONG MaxBurstSize;
    /* Also the context's. "Software shall not define a TD Transfer Size for a TD
     * of an Isoch endpoint that exceeds the Max ESIT Payload" (4.14.2.1 p.238) -
     * a `shall not`, so a packet above it is refused here rather than sent and
     * left to come back as a Bandwidth Overrun. */
    ULONG MaxEsitPayload;
    /*
     * **How many of these TDs the endpoint this driver programmed will consume
     * per frame**, from the Endpoint Context's own Interval: `8 >> Interval` for
     * an ESIT inside a frame, which is 8 on High Speed (Interval 0) and 1 on
     * Full Speed (Interval 3). Zero means "not a cadence this driver can state",
     * and every Frame ID is then refused rather than guessed.
     *
     * It exists to be checked against usbport's per-packet stamps, which are
     * `StartFrame + (i >> 3)` on High Speed and `StartFrame + i` otherwise
     * (docs/usb-xhci-info/usbport-miniport-abi.md section 4). Those two are statements about
     * the same schedule from opposite sides, and this is the only place they can
     * be compared - see `xhciXferIsoCadenceAgrees`.
     */
    ULONG PacketsPerFrame;
    XHCI_ISO_FRAME_POLICY Frames;
} XHCI_ISO_REQUEST, *PXHCI_ISO_REQUEST;

/*
 * Decode one Transfer Event completion code **on an isochronous ring**.
 *
 * A separate entry point from `XhciXferCodeInfo` rather than three more cases
 * inside it, because two of the differences are reinterpretations rather than
 * additions and putting them in the shared decoder would change what a bulk
 * endpoint is told:
 *
 *   - Short Packet is `USBD_STATUS_DATA_UNDERRUN` here and plain Success there.
 *     On a bulk pipe a short read is the ordinary way a transfer ends and the
 *     URB's own flags decide whether it is an error; on an isoch pipe it is a
 *     per-packet fact usbport has a specific value for - see that value's
 *     comment for why using it is not a stylistic choice.
 *   - The three isoch-only codes - Bandwidth Overrun (18), Missed Service (23)
 *     and Isoch Buffer Overrun (31) - are `XHCI_XFER_BAD_PARAM` in the shared
 *     decoder *on purpose*, because on a control, interrupt or bulk ring they
 *     are impossible and treating one as meaningful would hide a controller
 *     fault.
 *
 * Ring Underrun (14) and Ring Overrun (15) are refused here too: they name no
 * TD, so there is nothing for a per-packet decode to be about. The device layer
 * answers them before any of this runs.
 */
ULONG XhciXferIsoCodeInfo(ULONG completionCode, PXHCI_XFER_CODE info);

/*
 * Encode one isochronous request into `out`. Touches no ring; the Cycle Bit is
 * XhciRingEnqueueTdGroup's. The per-TD length array is inside `layout`.
 */
ULONG XhciXferBuildIso(const XHCI_ISO_REQUEST *request,
                       ULONG transferFlagsIn,
                       XHCI_TRB *out,
                       ULONG capacity,
                       PXHCI_ISO_LAYOUT layout);

/*
 * Build, preflight and publish one isochronous request onto `ring`, and record
 * it in `queue` as `transfer`. Same all-or-nothing contract as the other two
 * submitters. On XHCI_XFER_OK the caller must ring DB[slotId] with the
 * endpoint's DCI, once, afterwards - and that doorbell is what puts an endpoint
 * back on the periodic schedule after a Ring Underrun or Ring Overrun (spec
 * 4.14.2.1 p.238), so it is not optional even when nothing was previously
 * outstanding.
 */
ULONG XhciXferSubmitIso(PXHCI_TRANSFER_QUEUE queue,
                        PXHCI_RING ring,
                        const XHCI_ISO_REQUEST *request,
                        ULONG transferFlagsIn,
                        PXHCI_TRANSFER transfer,
                        PVOID transferParameters,
                        XHCI_TRB *scratch,
                        ULONG scratchCount,
                        PXHCI_ISO_LAYOUT layout);

/*
 * Match one Transfer Event on an **isochronous** ring to its transfer and its
 * packet, write that packet's two output fields into usbport's block, and decide
 * whether the request is finished.
 *
 * This is a separate function from `XhciXferEvent` rather than a widening of it,
 * and the separation is deliberate: almost nothing the bulk path does is right
 * here. An isoch endpoint never halts (p.177, restated 4.10.2.8 p.184), so no
 * *halt* recovery is ever right - though a TRB Error still puts it in the Error
 * state and does need one, which is the distinction the first review round found
 * this sentence had flattened into "no recovery at all"; a Missed Service Error
 * is the pipe resynchronizing
 * rather than a fault (4.10.3.2 p.187); the mid-TD short-packet deferral machine
 * exists to protect a *reposted bulk IN* and has no counterpart on a pipe whose
 * TDs are consumed one per interval; and the completion carries a status per
 * packet rather than one status for the request. Folding those five differences
 * into the existing branches is how the batch-9-0 review's five MAJORs happened.
 *
 * **Ring Underrun and Ring Overrun must not reach here.** Their TRB Pointer is
 * "the value of the Dequeue Pointer where the Overrun or Ring Underrun condition
 * was detected" (4.10.3.1 p.185) - a live ring address belonging to no
 * outstanding TD on a 1.1+ controller, and zero on a pre-1.1 one - so resolving
 * it as a completion attributes an underrun to whatever TD happens to sit there.
 * The device layer intercepts both by completion code before calling this.
 */
ULONG XhciXferIsoEvent(PXHCI_TRANSFER_QUEUE queue,
                       PXHCI_RING ring,
                       ULONG slotId,
                       ULONG dci,
                       ULONG eventTrbPA,
                       ULONG eventDw2,
                       ULONG eventDw3,
                       PXHCI_XFER_EVENT_RESULT result);

/*
 * Give every packet of an isochronous transfer that no event answered the
 * transfer's own status and a length of zero, immediately before it is handed to
 * `UsbPortCompleteIsoTransfer`.
 *
 * It exists because usbport's block is **zero-filled**, and zero is
 * `USBD_STATUS_SUCCESS`: a request torn down at a disconnect, cancelled, or
 * failed before it ever reached the ring would otherwise be reported to the
 * client driver as every packet having succeeded with no data - a silent lie
 * rather than a visible failure.
 *
 * **Called from the one place every isochronous completion passes through**, the
 * device layer's completion drain, rather than at each of the several routes a
 * transfer can take to it. A row set assembled route-by-route is only as
 * complete as whoever assembled it (task 7b-A.1.0), and this is a row set: the
 * drain, the abort, the teardown, the failed submit and the settle all end here.
 *
 * Idempotent, and safe on a transfer that is not isochronous (it does nothing).
 */
VOID XhciXferIsoFinalise(PXHCI_TRANSFER transfer);

#endif /* XHCI_XFER_H */
