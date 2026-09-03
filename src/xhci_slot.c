/*
 * xhci_slot.c - devices: slots, the default control endpoint, and the
 * asynchronous command chain that gets a device from "a port reset finished"
 * to "usbport has an address for it".
 *
 * Roadmap Phase 6 batch B, tasks 6-B.1 to 6-B.5. What each one is:
 *
 *   6-B.1  QueryEndpointRequirements and the endpoint-state callbacks.
 *   6-B.2  the EP0 open state machine: Enable Slot, then Address Device with
 *          BSR = 1, neither of which may be waited for.
 *   6-B.3  SET_ADDRESS interception - the setup packet that must never reach a
 *          transfer ring - and the usbport-address -> Slot ID map.
 *   6-B.4  the EP0 close/reopen round usbport performs after the first device
 *          descriptor read, and the Evaluate Context that corrects MPS0.
 *   6-B.5  disconnect at every one of those states.
 *
 * Three structural facts decide almost everything here, and all three came out
 * of batch 6-0 reading both shipping usbport.sys builds rather than out of
 * ReactOS:
 *
 *   **No callback ever says "this device is gone."** `CloseEndpoint` and
 *   `GetEndpointState` are never called by either build, and an endpoint is
 *   deleted with its common buffer freed and no callback at all. The only
 *   notice is `SetEndpointState(REMOVE)`, which arrives for a *reopen* as well
 *   as for a delete and therefore cannot mean teardown. So slot teardown is
 *   derived from the **port**: a connect change is the trigger, and it is the
 *   only one (task 6-B.5).
 *
 *   **The miniport endpoint extension is zeroed between a REMOVE and the next
 *   open**, and the endpoint's common buffer is freed and reallocated. So no
 *   device state may live there - it lives in XHCI_EXTENSION.Devices - and EP0's
 *   transfer ring lives in the *controller* common buffer, which is also why the
 *   reopen must not reinitialise it: the xHC's TR Dequeue Pointer still points
 *   into that ring, and Evaluate Context does not update it.
 *
 *   **The root-port association is derived from a completed port reset**,
 *   checked against a live view of the port, and refused when it cannot be
 *   believed - never guessed.
 *
 *   Batch 6-B's reason for that was **wrong and batch 6-V's VM runs measured
 *   it**: this file said `HubAddr`/`PortNumber` "read 0xFFFF/0 for a device on
 *   a root port", and on both shipping builds they read `0xFFFF`/**the root
 *   port number** (`docs/usb-xhci-info/usbport-miniport-abi.md`, the measured correction
 *   under `USBPORT_ENDPOINT_PROPERTIES`). `HubAddr` is TT-only.
 *
 *   **`PortNumber` is *not* simply "the port on the parent hub"** - batch 7a-0's
 *   corrected read of both builds says it carries the **TT port**
 *   on a successful TT lookup, at any tier depth: `USBPORT_GetTt` overwrites the
 *   port local in `USBPORT_CreateDevice`'s frame on every non-High-Speed step of
 *   its upward walk, and that mutated local becomes `DeviceHandle->PortNumber`.
 *
 *   **It reads the root port here because of *this driver's own* untruth**, not
 *   because of anything in usbport's walk: `xhci_port.c` reports every connected
 *   root port as High Speed (Phase 5 task 7), so usbport creates the device as
 *   High-Speed and skips `GetTt` entirely - the port local is never touched.
 *   Remove that untruth and a truthfully non-HS root-port device would instead
 *   bugcheck inside `OpenPipe` before any property is filled. So the batch 6-V
 *   measurement is downstream of a decision this driver makes, and it moves if
 *   that decision moves.
 *
 *   **Either way `PortNumber` may never be read as a root-port index without
 *   first establishing that the device is on a root port** - behind an external
 *   hub the same field names a port on a different hub entirely. Phase 7b
 *   inherits that.
 *
 *   The mechanism below is kept, because it was proven correct on both targets
 *   and because a derived hint that agrees with an independent check is worth
 *   more than either alone. Whether `PortNumber` should replace it or
 *   corroborate it is a design question nothing measured yet settles - in
 *   particular, nothing says which should win if the two ever disagree. The
 *   correction above narrows that question rather than answering it: a
 *   replacement would now also have to prove the device is root-attached.
 *
 * The locking rule is design doc 05 section 7's, and it is the same one the root
 * hub follows: **decide under the controller lock, act after releasing it.**
 * Every usbport service and every command submission happens in
 * XhciSlotDeferredWork. A function's comment says which side of the lock it is
 * on, because getting that wrong here is a self-deadlock rather than a style
 * problem.
 *
 * C89 only. Every function carries its IRQL requirement.
 */

#include "xhci.h"
#include "xhci_usbport.h"
#include "xhci_hw.h"
#include "xhci_xfer.h"
#include "xhci_dbg.h"

/*
 * The cap this driver puts on **one transfer on any pipe**, reported through
 * QueryEndpointRequirements.
 *
 * It was named `XHCI_EP0_MAX_TRANSFER` until task 8-A.1, when the name stopped
 * being true: usbport adopts this number as the endpoint's cap for bulk and
 * interrupt and keeps its own 0x1000 for the default pipe, so the one pipe it
 * has never applied to is EP0's. The value is unchanged - what changed is that
 * it is now load-bearing rather than advisory, because a bulk endpoint really is
 * offered transfers this large.
 *
 * 64 KB rather than the 128 KB XHCI_XFER_MAX_DATA_TRBS pages would allow,
 * because the bound that matters is TRBs and not bytes: usbport's scatter/gather
 * elements are page-granular but the first one may start part-way into a page,
 * so N bytes can need N/4096 + 1 elements. 64 KB is at most 17 data TRBs against
 * a 32-TRB policy cap and a 62-TRB ring capacity, which leaves the "preflight
 * the whole group or write nothing" rule room to be true rather than nearly
 * true - and leaves a bulk ring room for three of them outstanding at once,
 * which is what makes the ring-full case backpressure rather than a stall.
 */
#define XHCI_MAX_TRANSFER   65536UL

XHCI_C_ASSERT(max_transfer_fits_the_trb_cap,
              (XHCI_MAX_TRANSFER / XHCI_PAGE_SIZE) + 1UL <=
                  XHCI_XFER_MAX_DATA_TRBS);
XHCI_C_ASSERT(max_transfer_fits_the_ep0_ring,
              XHCI_XFER_MAX_CONTROL_TRBS <= XHCI_EP0_RING_TRBS - 2UL);
/*
 * And the pooled rings, which task 8-A.1 made reachable by transfers of that
 * size. The two rings have the same TRB count today (`pool_ring_stride_matches
 * _ep0` in xhci.h), and this is the assert that would catch them diverging in
 * the direction that matters: a pool ring too small for one maximum transfer
 * would refuse it on an *empty* ring, which is the one state the backpressure
 * latch cannot get out of.
 */
XHCI_C_ASSERT(max_transfer_fits_a_pool_ring,
              XHCI_XFER_MAX_CONTROL_TRBS <= XHCI_POOL_RING_TRBS - 2UL);

/* USB 2.0 section 9.4: SET_ADDRESS is bmRequestType 0 (host-to-device,
 * standard, device recipient) and bRequest 5. Both are checked, because
 * bRequest 5 with any other recipient is a different request entirely. */
#define XHCI_SETUP_TYPE_SET_ADDRESS     0x00
#define XHCI_SETUP_REQUEST_SET_ADDRESS  0x05

/* ------------------------------------------------------------------ */
/* Record lookup - all of these are called with the lock held          */
/* ------------------------------------------------------------------ */

static PXHCI_DEVICE xhciDevAt(PXHCI_EXTENSION ext, ULONG index)
{
    if (index >= XHCI_MAX_SLOTS) {
        return NULL;
    }
    return &ext->Devices[index];
}

/* The index a record is stored at, plus one - the encoding every reference to a
 * record uses, so that the zero a cleared field holds names no device rather
 * than device 0. */
static ULONG xhciDevRef(PXHCI_EXTENSION ext, const XHCI_DEVICE *dev)
{
    ULONG i;

    for (i = 0; i < XHCI_MAX_SLOTS; i++) {
        if (&ext->Devices[i] == dev) {
            return i + 1;
        }
    }
    return 0;
}

static PXHCI_DEVICE xhciDevFromRef(PXHCI_EXTENSION ext, ULONG ref)
{
    if (ref == 0) {
        return NULL;
    }
    return xhciDevAt(ext, ref - 1);
}

static PXHCI_DEVICE xhciDevByAddress(PXHCI_EXTENSION ext, ULONG address)
{
    ULONG i;

    if (address == 0) {
        return NULL;
    }
    for (i = 0; i < XHCI_MAX_SLOTS; i++) {
        PXHCI_DEVICE dev = &ext->Devices[i];

        if ((dev->Flags & XHCI_DEV_FLAG_ADDRESS_VALID) != 0 &&
            dev->DeviceAddress == address) {
            return dev;
        }
    }
    return NULL;
}

/*
 * Publishing a TD can retract mid-TD tail records whose TRBs it re-lets, and
 * that loss has to reach the extension while the device is still attached -
 * the whole reading is taken during a run, and on a repost loop retraction is
 * the common case rather than the rare one (repo audit round 3, finding 1).
 * So it is folded as a delta across the submit, exactly as the short-packet
 * family is folded across `XhciXferEvent`.
 *
 * Called with the controller lock held.
 * IRQL: <= DISPATCH_LEVEL.
 */
static VOID xhciDevFoldCensored(PXHCI_EXTENSION ext,
                                PXHCI_TRANSFER_QUEUE queue,
                                ULONG before)
{
    if (ext == NULL || queue == NULL) {
        return;
    }
    if (queue->MidTdTailsCensored != before) {
        ext->MidTdTailsCensoredTotal += queue->MidTdTailsCensored - before;
        /* Sticky, and the thing the verdict actually reads. The totals are
         * wrapping ULONGs, so "censored == 0" stops being proof that nothing
         * was censored after 2^32 of them; a flag that only ever goes one way
         * cannot be undone by a wrap (repo audit round 4, finding 4). */
        ext->MidTdVerdictVoided = 1;
    }
}
/*
 * Task 9-0.2's `MidTdDeferralsLost`, folded the same way and for the same
 * reason: a transfer can leave a queue still holding a deferral from paths that
 * are nothing to do with an event - a teardown drain, an `AbortTransfer` - and
 * the partition it belongs to is read *during* a run.
 *
 * It is its own helper rather than a line inside those call sites because the
 * counter has four movers (the event path, the settle, the drain and the abort)
 * and a missed one breaks the identity `XhciSlotDrainSettled` checks. That check
 * is what makes a missed site visible instead of quiet.
 *
 * Called with the controller lock held. IRQL: <= DISPATCH_LEVEL.
 */
static VOID xhciDevFoldDeferralsLost(PXHCI_EXTENSION ext,
                                     PXHCI_TRANSFER_QUEUE queue,
                                     ULONG before)
{
    if (ext == NULL || queue == NULL) {
        return;
    }
    ext->MidTdDeferralsLostTotal += queue->MidTdDeferralsLost - before;
}

/*
 * Tail-record evictions, and this is a helper for the reason the counter was
 * unreadable without one: it has **two** movers on paths that do not nest.
 *
 * Since task 9-0.2 the only site that records a tail is the settle's early
 * retire, so the only site that can evict one is the settle - and the fold
 * stood alone inside the event path's bracket, whose `before` is sampled after
 * any settle-time eviction has already happened. The extension's total was
 * therefore **structurally 0** and `MidTdVerdictVoided` never fired for slot
 * pressure, which is the one thing this counter exists to make visible: with
 * XHCI_XFER_MID_TD_TAIL slots and a controller that sends no tails at all, the
 * FIFO is full within four retires and evicting from then on. Measured on 2b
 * Phase 10's matrix: 607 retires, 0 claimed, 0 censored live, 4 alive at the teardown,
 * and 0 reported.
 *
 * Called with the controller lock held. IRQL: <= DISPATCH_LEVEL.
 */
static VOID xhciDevFoldTailsDropped(PXHCI_EXTENSION ext,
                                    PXHCI_TRANSFER_QUEUE queue,
                                    ULONG before)
{
    if (ext == NULL || queue == NULL) {
        return;
    }
    if (queue->MidTdTailsDropped != before) {
        ext->MidTdTailsDroppedTotal += queue->MidTdTailsDropped - before;
        /* Sticky, for the same reason `xhciDevFoldCensored` sets it: a wrapping
         * total cannot carry a "nothing was lost" claim, and an evicted record
         * is a tail this driver can no longer recognise if it does arrive. */
        ext->MidTdVerdictVoided = 1;
    }
}

/*
 * Save what a transfer queue measured before the queue stops existing.
 *
 * Seven of `XHCI_TRANSFER_QUEUE`'s counters had no reader at all: they were
 * never printed, never folded up, and `XhciXferQueueInit` re-zeroed them on
 * every reopen - so the endpoint-torn-down-after-a-fault case, the one whose
 * `OrphanedGroups` and `PlacementFailures` readings are the whole reason those
 * counters exist, was also the case that destroyed them.
 *
 * Every path that ends a queue's life calls this first: the two
 * `XhciXferQueueInit` sites, `xhciDevRelease` when the record is zeroed, and
 * `XhciSlotInit`'s table reset. One helper rather than seven additions per
 * site, because a fold assembled site-by-site is only as complete as whoever
 * assembled it - the same reason task 7b-A.1.0's open accounting is one net.
 *
 * Idempotent only in the sense that a queue is folded once and then zeroed; the
 * caller must not fold a queue it leaves standing.
 *
 * Called with or without the lock held - it touches only this record's queue and
 * the extension totals, and every caller already owns both.
 * IRQL: <= DISPATCH_LEVEL.
 */
static VOID xhciDevFoldQueue(PXHCI_EXTENSION ext, PXHCI_TRANSFER_QUEUE queue)
{
    if (ext == NULL || queue == NULL) {
        return;
    }
    ext->OrphanedGroupsTotal += queue->OrphanedGroups;
    ext->PlacementFailuresTotal += queue->PlacementFailures;
    ext->SweptTransfersTotal += queue->SweptTransfers;
    ext->ResidualRejectsTotal += queue->ResidualRejects;
    ext->LengthOverrunsTotal += queue->LengthOverruns;
    ext->SumFailuresTotal += queue->SumFailures;
    ext->ResidualIgnoredTotal += queue->ResidualIgnored;
    /*
     * **The seven that were written and never read** (audit finding B3). Each of
     * these is incremented across `src/xhci_xfer.c` and each died with the device
     * record, so its whole life was a store: `xhci_xfer.c` even says "the visible
     * failure is the counter" about `ForeignEvents`, on a counter no print site
     * could reach. Folded here beside the other seven, which makes them readable
     * exactly the way those are - as ext-level totals that survive an unplug.
     */
    ext->ForeignEventsTotal += queue->ForeignEvents;
    ext->EventDataEventsTotal += queue->EventDataEvents;
    ext->BadCodesTotal += queue->BadCodes;
    ext->QueueErrorsTotal += queue->Errors;
    ext->QueueRecoveriesTotal += queue->Recoveries;
    ext->QueueSubmittedTotal += queue->Submitted;
    ext->QueueCompletedTotal += queue->Completed;
    ext->StoppedRefusedTotal += queue->StoppedRefused;
    /*
     * The records this queue still holds: an outstanding record dies with the
     * queue, so that observation is censored by the teardown rather than
     * answered by the controller.
     *
     * **Only those.** `queue->MidTdTailsCensored` is deliberately *not* added
     * here, because every retraction was already folded live as a delta across
     * the submit that caused it - adding the cumulative value again at death
     * counted each one twice, which is what an earlier revision did (repo audit
     * round 4, finding 2). The distinction is the same one `xhciDevFoldQueue`'s
     * header makes about the seven counters above: a counter folded at death is
     * one nothing else folds.
     */
    if (queue->MidTdTailCount > 0) {
        ext->MidTdTailsCensoredTotal += queue->MidTdTailCount;
        ext->MidTdVerdictVoided = 1;
    }
}

/* Every queue one device record owns, for the paths that end the whole record.
 * IRQL: <= DISPATCH_LEVEL. */
static VOID xhciDevFoldQueues(PXHCI_EXTENSION ext, PXHCI_DEVICE dev)
{
    ULONG i;

    if (ext == NULL || dev == NULL) {
        return;
    }
    xhciDevFoldQueue(ext, &dev->Ep0Queue);
    for (i = 0; i < XHCI_MAX_DEVICE_ENDPOINTS; i++) {
        xhciDevFoldQueue(ext, &dev->Endpoints[i].Queue);
        /*
         * **And this is where an endpoint record dies** (audit finding B1).
         * `EndpointsReleased` was declared and printed and had no increment site
         * anywhere in the tree, so it read 0 for ever and poisoned the only
         * opened-vs-released balance a run can be read for. The two callers of
         * this function are the two ways a record stops existing: the release
         * that has proved the slot gone, and the reinitialisation's sweep. A
         * REMOVE is deliberately not one of them - it unbinds usbport's endpoint
         * and leaves the record, the ring and the Endpoint Context standing.
         */
        if (dev->Endpoints[i].Dci != 0) {
            ext->EndpointsReleased++;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Batch 7a-A.1: non-default endpoint records                          */
/* ------------------------------------------------------------------ */

/*
 * May this record still be asked to open a non-default endpoint?
 *
 * **One predicate rather than two lists**, because it is asked in two places
 * whose answers must agree exactly: `XhciSlotOpenEndpoint` refuses an open for a
 * record it says no to, and `xhciDevRingless` reserves a pool ring for every
 * record it says yes to. If the two ever disagreed in the permissive direction
 * the pool would reserve rings nobody can spend; in the strict direction a
 * record would be admitted to an open with nothing held for it. The ninth review
 * found them already disagreeing about `DISOWNED` while the second reader had no
 * caller at all - so the shape that prevents it is a shared function, not two
 * lists kept in step by hand.
 *
 * FREE, FAILED and GONE are out for the obvious reason. **DISOWNED is out too**:
 * usbport has destroyed its device object and freed its USB address, so nothing
 * can name it - `xhciDevByAddress` cannot find it (the flag clears
 * `ADDRESS_VALID`), and the only path that revives it is an `OpenEndpoint` at
 * address 0 on its port, which clears the flag and restarts the chain *before*
 * any non-default endpoint can be opened.
 *
 * That is the argument for FAILED as well, and it replaces the one this comment
 * used to give: "a FAILED record is not retried until its port reports a fresh
 * connect" was **false**. `xhciDevEnumeratingPort` deliberately admits a port
 * that already has a record, so a re-enumeration revives a FAILED one with no
 * connect change at all. What is actually true is narrower and enough: while the
 * record is in one of these states no endpoint open can reach it, and the path
 * that changes that moves it back into a counted state first.
 *
 * IRQL: any. Called with the lock held.
 */
static ULONG xhciDevMayOpenEndpoint(const XHCI_DEVICE *dev)
{
    if (dev == NULL) {
        return 0;
    }
    if (dev->State == XHCI_DEV_STATE_FREE ||
        dev->State == XHCI_DEV_STATE_FAILED ||
        dev->State == XHCI_DEV_STATE_GONE) {
        return 0;
    }
    if ((dev->Flags & XHCI_DEV_FLAG_DISOWNED) != 0) {
        return 0;
    }
    return 1;
}

/* The record holding this DCI, or NULL. Dci 0 marks a free entry, which is safe
 * because DCI 0 is the Slot Context and never an endpoint. */
static PXHCI_ENDPOINT_RECORD xhciEpByDci(PXHCI_DEVICE dev, ULONG dci)
{
    ULONG i;

    /* DCI 0 is the Slot Context and 31 is the last endpoint (spec 4.5.1), so
     * anything outside 1..31 names no endpoint context and must not be allowed
     * to match a record - a bound the array size does not supply, because the
     * array is indexed by record and not by DCI. */
    if (dev == NULL || dci == 0 || dci > XHCI_MAX_DCI) {
        return NULL;
    }
    for (i = 0; i < XHCI_MAX_DEVICE_ENDPOINTS; i++) {
        if (dev->Endpoints[i].Dci == dci) {
            return &dev->Endpoints[i];
        }
    }
    return NULL;
}

static PXHCI_ENDPOINT_RECORD xhciEpFree(PXHCI_DEVICE dev)
{
    ULONG i;

    if (dev == NULL) {
        return NULL;
    }
    for (i = 0; i < XHCI_MAX_DEVICE_ENDPOINTS; i++) {
        if (dev->Endpoints[i].Dci == 0) {
            return &dev->Endpoints[i];
        }
    }
    return NULL;
}

/*
 * How many *other* live device records hold no pool ring - the `ringless`
 * argument `XhciPoolAcquire`'s admission rule needs.
 *
 * The allocator cannot compute this itself: it sees pool entries, not device
 * lifecycle, and "could still ask for an endpoint" is a question only this layer
 * can answer. Design doc 04 section 3.6's guarantee is exactly that no such
 * record is ever refused its first ring, so the set counted here **is** the
 * guarantee's subject and getting it wrong silently weakens the rule rather
 * than breaking anything visibly.
 *
 * Which records those are is `xhciDevMayOpenEndpoint`'s answer and not a second
 * list written here: counting a record the open would refuse reserves a ring
 * nothing can ever spend, which turns the fairness rule into an availability
 * bug. RESERVED and ENABLED are counted - they are mid-enumeration and an
 * endpoint open is exactly what comes next.
 *
 * Called with the lock held. IRQL: DISPATCH_LEVEL.
 */
/* `exclude`, not `except`: the DDK's headers define `except` as a macro for
 * `__except`, so a parameter of that name turns this declaration into a
 * malformed SEH block. The host suite compiles without those headers and does
 * not see it, which is why it survived batch 7a-A. */
static ULONG xhciDevRingless(PXHCI_EXTENSION ext, const XHCI_DEVICE *exclude)
{
    ULONG i;
    ULONG count;

    count = 0;
    for (i = 0; i < XHCI_MAX_SLOTS; i++) {
        PXHCI_DEVICE dev = &ext->Devices[i];

        if (dev == exclude) {
            continue;
        }
        if (!xhciDevMayOpenEndpoint(dev)) {
            continue;
        }
        if (XhciPoolDeviceCount(&ext->RingPool, xhciDevRef(ext, dev)) == 0) {
            count++;
        }
    }
    return count;
}

/*
 * The endpoint record owing a Configure Endpoint, **lowest DCI first**.
 *
 * The order is fixed rather than "whichever the array holds first" so that a
 * device opening two endpoints in one breath produces a determined sequence of
 * commands - which is what lets a vector assert the second one at all.
 *
 * Called with the lock held. IRQL: any.
 */
static PXHCI_ENDPOINT_RECORD xhciEpFirstPending(PXHCI_DEVICE dev)
{
    PXHCI_ENDPOINT_RECORD found;
    ULONG i;

    found = NULL;
    for (i = 0; i < XHCI_MAX_DEVICE_ENDPOINTS; i++) {
        PXHCI_ENDPOINT_RECORD record = &dev->Endpoints[i];

        if (record->Dci == 0 || record->State != XHCI_EP_REC_PENDING) {
            continue;
        }
        /*
         * **Nothing is configured for an endpoint usbport is not bound to.** A
         * `SetEndpointState(REMOVE)` unbinds the record and deliberately leaves
         * everything else standing, so a Configure Endpoint owed from before it
         * would otherwise still go out and hand the xHC an endpoint - and its
         * periodic bandwidth - that nobody is holding. The stop-time review of
         * round 11 found that: `xhciEpOweContextRestore` declines on the
         * give-up path, but only when the REMOVE is *ordered before* the stop's
         * completion; a REMOVE arriving after the restoration was queued raced
         * past that guard.
         *
         * Deferring rather than cancelling is what keeps the reopen half right,
         * and the two are indistinguishable at the REMOVE (batch 6-0): the debt
         * is true either way - the endpoint really has no context - so a reopen
         * rebinds the record and this pump issues it then, while a delete never
         * spends the command at all.
         */
        if (record->EndpointExtension == NULL) {
            continue;
        }
        if (found == NULL || record->Dci < found->Dci) {
            found = record;
        }
    }
    return found;
}

/* ------------------------------------------------------------------ */
/* Batch 7a-B: one endpoint, whichever kind it is                      */
/* ------------------------------------------------------------------ */

/*
 * The three things every per-endpoint path needs, gathered so that EP0 and a
 * non-default endpoint reach the same code.
 *
 * EP0 has no `XHCI_ENDPOINT_RECORD`: its ring, queue and quiescence state live
 * directly on the device. Every batch 7a-B path would otherwise carry its own
 * `if (dci <= 1)`, and the ones that already do are why this exists - the abort
 * path and the event path each grew a copy in Phase 6, and a third copy deciding
 * what a Stop Endpoint means is how a control endpoint and an interrupt endpoint
 * end up with different answers to the same question.
 *
 * `Record` is NULL for EP0 and is the only thing a caller may branch on.
 * Called with the lock held. IRQL: any.
 */
typedef struct _XHCI_EP_BINDING {
    ULONG Dci;
    PXHCI_RING Ring;
    PXHCI_TRANSFER_QUEUE Queue;
    PXHCI_EP_QUIESCE Quiesce;
    PXHCI_ENDPOINT_RECORD Record;
} XHCI_EP_BINDING, *PXHCI_EP_BINDING;

static ULONG xhciEpResolve(PXHCI_DEVICE dev, ULONG dci, PXHCI_EP_BINDING out)
{
    PXHCI_ENDPOINT_RECORD record;

    if (dev == NULL || out == NULL) {
        return 0;
    }
    out->Dci = dci;
    out->Record = NULL;
    if (dci <= 1) {
        /* Spec 4.5.1 makes the default control endpoint DCI 1 whichever way its
         * direction bit reads, and DCI 0 is the Slot Context - which no caller
         * should reach here with, but which resolves to EP0 rather than to a
         * wild pointer if one does. A ring with no base has not been carved. */
        if (dev->Ep0Ring.Base == NULL) {
            return 0;
        }
        out->Ring = &dev->Ep0Ring;
        out->Queue = &dev->Ep0Queue;
        out->Quiesce = &dev->Ep0Quiesce;
        out->Dci = 1;
        return 1;
    }

    record = xhciEpByDci(dev, dci);
    if (record == NULL || record->Ring.Base == NULL) {
        return 0;
    }
    out->Ring = &record->Ring;
    out->Queue = &record->Queue;
    out->Quiesce = &record->Quiesce;
    out->Record = record;
    return 1;
}

/*
 * "Context Entries ... identifies the index of the last valid Endpoint Context"
 * (Table 6-5), computed as if `extra` had already been added.
 *
 * **Only CONFIGURED records count**, plus the one being added. A PENDING or
 * REFUSED record has no Endpoint Context in the device context at all, and
 * claiming it would tell the xHC to treat whatever the slot's context array
 * happens to hold at that index as an endpoint.
 *
 * The floor is 1 because EP0 exists for the whole life of an addressed slot -
 * this is where spec 6.2.2.2's requirement is met, and it is easy to miss
 * because Address Device only ever sets it to 1 and nothing before task 7a-A.1
 * ever needed it to move. An endpoint context the Slot Context does not claim is
 * one the xHC will not look at.
 *
 * Called with the lock held. IRQL: any.
 */
static ULONG xhciDevMaxDci(const XHCI_DEVICE *dev, ULONG extra)
{
    ULONG max;
    ULONG i;

    max = 1;
    if (extra > max) {
        max = extra;
    }
    for (i = 0; i < XHCI_MAX_DEVICE_ENDPOINTS; i++) {
        const XHCI_ENDPOINT_RECORD *record = &dev->Endpoints[i];

        if (record->Dci != 0 && record->State == XHCI_EP_REC_CONFIGURED &&
            record->Dci > max) {
            max = record->Dci;
        }
    }
    return max;
}

/* Is this one of the three per-endpoint quiescence commands? Asked wherever a
 * path has to tell "a command about this device" from "a command about one of
 * its endpoints" - the two unwind differently. IRQL: any. */
static ULONG xhciDevOpIsQuiesce(ULONG op)
{
    return (op == XHCI_DEV_OP_STOP_EP || op == XHCI_DEV_OP_RESET_EP ||
            op == XHCI_DEV_OP_SET_DEQUEUE) ? 1 : 0;
}

/*
 * The quiescence command one endpoint owes, from its flags alone.
 *
 * The order is fixed and is the spec's, not a preference. A Set TR Dequeue
 * Pointer "may be executed only if the target endpoint is in the Error or
 * Stopped state" (4.6.10 p.126), so it can only ever follow the stop or the
 * reset that put it there; and a Reset Endpoint "may only be issued to endpoints
 * in the Halted state" (4.6.8 p.118), which is why nothing sets NEED_RESET for
 * an endpoint that is not halted.
 *
 * `BUSY` answering NONE is what stops the pump issuing a second copy of a
 * command whose completion has not arrived. IRQL: any.
 */
static ULONG xhciEpQuiesceOp(const XHCI_EP_QUIESCE *quiesce)
{
    if ((quiesce->Flags & XHCI_EPQ_BUSY) != 0) {
        return XHCI_DEV_OP_NONE;
    }
    if ((quiesce->Flags & XHCI_EPQ_NEED_STOP) != 0) {
        return XHCI_DEV_OP_STOP_EP;
    }
    if ((quiesce->Flags & XHCI_EPQ_NEED_RESET) != 0) {
        return XHCI_DEV_OP_RESET_EP;
    }
    if ((quiesce->Flags & XHCI_EPQ_NEED_DEQUEUE) != 0) {
        return XHCI_DEV_OP_SET_DEQUEUE;
    }
    return XHCI_DEV_OP_NONE;
}

/*
 * The first endpoint of this device owing a quiescence command, **EP0 first and
 * then by ascending DCI**.
 *
 * Deterministic for the same reason `xhciEpFirstPending` is: a device whose
 * endpoints are all torn down at once produces a determined sequence of
 * commands, which is what lets a vector assert the second one. EP0 leads because
 * it is the endpoint a teardown is most likely to be waiting on and because
 * putting it anywhere else would make its position depend on which DCIs happen
 * to be open.
 *
 * Called with the lock held. IRQL: any.
 */
static ULONG xhciEpFirstQuiesce(PXHCI_DEVICE dev, PXHCI_EP_BINDING out)
{
    XHCI_EP_BINDING binding;
    ULONG op;
    ULONG i;

    if (xhciEpResolve(dev, 1, &binding)) {
        op = xhciEpQuiesceOp(binding.Quiesce);
        if (op != XHCI_DEV_OP_NONE) {
            *out = binding;
            return op;
        }
    }
    for (i = 0; i < XHCI_MAX_DEVICE_ENDPOINTS; i++) {
        if (dev->Endpoints[i].Dci == 0) {
            continue;
        }
        if (!xhciEpResolve(dev, dev->Endpoints[i].Dci, &binding)) {
            continue;
        }
        op = xhciEpQuiesceOp(binding.Quiesce);
        if (op != XHCI_DEV_OP_NONE) {
            *out = binding;
            return op;
        }
    }
    return XHCI_DEV_OP_NONE;
}

/* ------------------------------------------------------------------ */
/* Task 7b-A.2: what a hub's own Slot Context has to say                */
/* ------------------------------------------------------------------ */

/*
 * The marking this record's slot should carry right now, as an XHCI_HUBMARK
 * word, or 0 for a device the graph does not describe as a hub with a
 * descriptor behind it.
 *
 * **Derived on every ask rather than stored when the graph changes**, for the
 * reason `XHCI_DEV_OP_CONFIGURE_EP`'s need is derived: the graph is the source,
 * and a copy of it kept on the record is a second place that can be stale. The
 * ask is a walk of at most XHCI_TOPO_NODES entries and happens only when the
 * pump is looking for work.
 *
 * The address is usbport's and is the graph's key, so a record with none - a
 * device mid-enumeration, or one that has been disowned - describes no node and
 * answers 0. That is the same 0 as "not a hub", deliberately: neither is a
 * reason to send a command.
 *
 * Called with the lock held. IRQL: any.
 */
static ULONG xhciDevHubMark(PXHCI_EXTENSION ext, const XHCI_DEVICE *dev)
{
    XHCI_TOPO_HUBMARK mark;
    const XHCI_TOPO_NODE *node;

    node = XhciTopoFind(&ext->Topology, dev->DeviceAddress);

    /*
     * **The node must describe a hub sitting where this record sits**, and the
     * address alone does not establish that.
     *
     * A record that re-enumerates gives its usbport address back without any
     * disconnect, so nothing detaches its node - the detach sites are all
     * teardown, disown and release, and none of them runs here. If usbport then
     * hands that address to a *different* device, the graph still holds a hub
     * node under it, and a driver that read the address alone would program
     * `Hub = 1` and a port count onto whatever that device is. Reading a graph
     * was harmless while nothing consumed it; task 7b-A.2 is what makes it a
     * write to hardware.
     *
     * The root port is the discriminator, and it is sufficient rather than
     * merely convenient: the other device must be on a *different* root port,
     * because a device arriving on this one means the old one disconnected,
     * which tears the record down and detaches the node - keyed through
     * `TopoAddress`, so a teardown mid-re-enumeration detaches it too, and the
     * SET_ADDRESS interception prunes whatever stale node sits under a newly
     * assigned address before the address can be consulted here (Phase 7
     * review, findings A4/A5; an earlier revision leaned on a generation prune
     * that no driver path called). `RootPort == 0` - a hub identified before
     * anything said where it is - fails this by construction.
     */
    if (node == NULL || node->RootPort == 0 || node->RootPort != dev->RootPort) {
        return 0;
    }
    /*
     * **And the same position on it** (task 7b-A.3). The root port alone was
     * sufficient while every record was on one: a device arriving on this root
     * port meant the old one had disconnected, which tore the record down and
     * detached the node. That argument does not survive devices behind hubs -
     * two records now share a root port, so a child that re-enumerates and has
     * its usbport address handed to a *sibling* would find this hub node under
     * the sibling's address with the root port matching.
     *
     * The position is the identity: tier, route and the parent it hangs off.
     * Two live records cannot share all three, because that is what "the same
     * place on the bus" means.
     */
    if (node->Tier != dev->Tier || node->Route != dev->RouteString ||
        node->ParentAddress != dev->ParentHubAddress) {
        return 0;
    }

    if (!XhciTopoHubMark(node, dev->Speed, &mark)) {
        return 0;
    }
    return XHCI_HUBMARK(mark.Hub, mark.NumberOfPorts, mark.MultiTt,
                        mark.TtThinkTime);
}

/* ------------------------------------------------------------------ */
/* Task 7b-A.3: where a device sits, in the Slot Context's vocabulary   */
/* ------------------------------------------------------------------ */

/*
 * Does this device need a transaction translator, and which one?
 *
 * Two questions have to agree before the pair may be written, and only one of
 * them is about this record:
 *
 *   - **This device is Low or Full Speed.** Table 6-6 p.409 clears Parent Hub
 *     Slot ID and Parent Port Number "if the device is not Low-/Full-speed",
 *     and 6.2.2.1 p.411 then clears every field its validity list does not
 *     name. A High-Speed device behind a High-Speed hub needs no split
 *     transaction and carries no TT.
 *   - **A High-Speed hub is above it.** That is `XhciTopoTtFor`'s walk, and it
 *     is asked of the graph rather than of usbport for the reason
 *     `XHCI_DEVICE.TtClaimAddress` gives: usbport reports a translator for a
 *     Full-Speed hub that has none, measured on both targets in batch 7b-V0.
 *     An all-Full-Speed path has no TT anywhere, and saying it does would
 *     describe hardware that does not exist.
 *
 * Returns 1 and fills `out` when a TT applies, 0 with `out` zeroed otherwise.
 * **One function for both the fill and the cross-check**, so the pair this
 * driver programs and the pair it compares against usbport's cannot be derived
 * two different ways.
 *
 * Called with the lock held. IRQL: any.
 */
static ULONG xhciDevTtExpected(PXHCI_EXTENSION ext,
                               const XHCI_DEVICE *dev,
                               PXHCI_TOPO_TT out)
{
    out->HubAddress = 0;
    out->HubPort = 0;
    out->MultiTt = 0;

    if (dev->Tier == 0 || dev->ParentHubAddress == 0) {
        return 0;
    }
    if (dev->Speed != XHCI_SPEED_LOW && dev->Speed != XHCI_SPEED_FULL) {
        return 0;
    }
    return XhciTopoTtFor(&ext->Topology, dev->ParentHubAddress,
                         dev->ParentHubPort, out);
}

#define XHCI_TT_NONE        0   /* this device carries no TT fields  */
#define XHCI_TT_FILLED      1   /* ...and here they are              */
#define XHCI_TT_UNRESOLVED  2   /* a TT applies and cannot be named  */

/*
 * Put this record's **position** into a Slot Context being built - Route
 * String, speed, root port, and the TT triple - which is every field spec
 * 6.2.2.1 p.411 names for an Address Device except Context Entries and the
 * Interrupter Target.
 *
 * Every Input Slot Context this driver builds goes through here, the Configure
 * Endpoint ones included. That is not tidiness: those commands' Slot Contexts
 * have to be valid too, and a route or a TT spelled differently in two builders
 * is a device described one way while it is being addressed and another way
 * while an endpoint is added.
 *
 * Returns XHCI_TT_UNRESOLVED when the graph says an HS hub is above this device
 * and no record of that hub holds a Slot ID. That is a **refusal**, and the
 * choice matters: the alternative is to clear the fields and address the device
 * anyway, which tells the controller an FS/LS device needs no split
 * transactions - a device the xHC then schedules wrongly, on a bus segment
 * shared with everything else behind that hub. Design doc 02 section 3's rule
 * is that a wrongly-addressed device corrupts the schedule and a cleanly
 * rejected one does not.
 *
 * Called with the lock held. IRQL: <= DISPATCH_LEVEL.
 */
static ULONG xhciDevFillSlotPosition(PXHCI_EXTENSION ext,
                                     PXHCI_DEVICE dev,
                                     PXHCI_SLOT_PARAMS slot)
{
    XHCI_TOPO_TT tt;
    const XHCI_DEVICE *hub;

    slot->RouteString = dev->RouteString;
    slot->Psiv = dev->Psiv;
    slot->RootHubPort = dev->RootPort;

    if (!xhciDevTtExpected(ext, dev, &tt)) {
        return XHCI_TT_NONE;
    }

    hub = xhciDevByAddress(ext, tt.HubAddress);
    if (hub == NULL || hub->SlotId == 0) {
        return XHCI_TT_UNRESOLVED;
    }

    slot->ParentSlotId = hub->SlotId;
    slot->ParentPortNumber = tt.HubPort;
    /*
     * The child's own MTT (DW0 `25`), which is *not* the hub's TTT: Table 6-4
     * p.409 sets MTT for "a Low-/Full-speed device or Full-speed hub ...
     * connected to the xHC through a parent High-speed hub that supports
     * Multiple TTs", so what a child inherits from the hub above it is this bit
     * and nothing else. `XhciTopoHubMark` owns the other direction - a hub's own
     * MTT - and the two are OR-ed rather than assigned, because a Full-Speed hub
     * behind a multi-TT High-Speed hub is both.
     *
     * **"Supports" is read as "has its multi-TT interface enabled", and that is
     * a deliberate departure from the sentence above.** Table 6-4's *hub* clause
     * says "and the Multiple TT Interface has been enabled by software" and its
     * *child* clause says only "supports Multiple TTs" - but a hub running its
     * single-TT alternate setting really does route every downstream port
     * through one translator, so a child marked MTT would have its split
     * transactions addressed to a TT the hub is not using. The graph follows the
     * `SET_INTERFACE` for exactly this reason (`XHCI_TOPO_F_MTT`), which is also
     * what Linux does - `tt->multi` is set when the alternate setting is
     * selected, not from the descriptor's capability.
     */
    if (tt.MultiTt) {
        slot->MultiTt = 1;
    }
    /*
     * The `MTT`/`TTT` context-field clause, child half - and the *absence* of a line from the hub
     * site above for this same Slot ID is what reads the rest of the clause.
     * TTT is written by `xhciDevFillHubFields` and nowhere else, so a slot
     * that appears here and never there is a slot whose Think Time is 0. That
     * is "TTT 0 on the child" read off the trace rather than asserted, which
     * is the whole difference between this clause being taken and being
     * reported unread.
     *
     * The MTT printed here is the one cause a *child* has - hanging off a
     * multi-TT hub. A hub that is itself behind one reaches both sites and
     * prints both, which is the OR in `xhciDevFillHubFields` visible as two
     * lines; that is the Full-Speed-hub-behind-a-multi-TT-hub case, which the
     * fleet has no hardware for (device-table row 5, published untested).
     */
    XHCI_DBG_VALUE_CHANGED("slot: child TT fields, slot id << 24 | "
                           "MTT << 16 | parent slot id << 8 | parent port",
                           XHCI_TT_TRACE_WORD(dev->SlotId, slot->MultiTt,
                                              slot->ParentSlotId,
                                              slot->ParentPortNumber));
    return XHCI_TT_FILLED;
}

/*
 * Put that marking into a Slot Context being built, and record what the command
 * carrying it will program.
 *
 * Every Input Slot Context this driver builds for a **Configure Endpoint** goes
 * through here - the endpoint one and the standalone marking alike - so the two
 * cannot describe the same hub differently, and so an endpoint's own command
 * marks the hub for free rather than leaving it to a second one. It is
 * deliberately *not* reached from the Address Device builder: 6.2.2.1 p.411's
 * validity list ends "and all other fields are cleared to '0'", and Hub is one
 * of the fields it does not name.
 *
 * `HubMarkIssued` is written even when the marking is 0, because the completion
 * commits it unconditionally: a Configure Endpoint for an ordinary device must
 * leave `HubMarkDone` at 0 rather than at whatever the last hub's command set.
 *
 * Called with the lock held. IRQL: <= DISPATCH_LEVEL.
 */
static VOID xhciDevFillHubFields(PXHCI_EXTENSION ext,
                                 PXHCI_DEVICE dev,
                                 PXHCI_SLOT_PARAMS slot)
{
    ULONG mark;

    mark = xhciDevHubMark(ext, dev);
    dev->HubMarkIssued = mark;
    if (mark == 0) {
        return;
    }

    slot->Hub = 1;
    slot->NumberOfPorts = XHCI_HUBMARK_GET_PORTS(mark);
    /*
     * OR-ed, never assigned: `xhciDevFillSlotPosition` may already have set MTT
     * because this hub is *itself* behind a multi-TT High-Speed hub, and Table
     * 6-4 p.409 gives the bit two independent causes - being a multi-TT
     * High-Speed hub, and hanging off one. An assignment here would clear the
     * second whenever the first did not hold, which is exactly a Full-Speed hub
     * behind a multi-TT parent.
     */
    if ((mark & XHCI_HUBMARK_MTT) != 0) {
        slot->MultiTt = 1;
    }
    slot->TtThinkTime = XHCI_HUBMARK_GET_TTT(mark);
    /*
     * The `MTT`/`TTT` context-field clause, hub half. The counters this driver
     * already keeps for the marking - `hub slots marked`, `hub marking
     * commands` - say that a marking happened; the clause is about what was
     * *in* it, and on a bench there is no other channel: the vm-matrix counter
     * reader needs a live QEMU monitor and a target machine has none. The batch
     * that owed the reading was removed for want of any vehicle
     * and the reading was never taken; the clause is published as a limitation.
     *
     * **Change-gated rather than unbounded, and the value is what makes that
     * the right gate.** Every endpoint a hub opens reaches here and carries
     * the same marking, so a hub that is not changing prints once and goes
     * quiet; a hub swapped at rig position T prints again because the word it
     * carries really did change, which is the experiment task 13-E.2 and this
     * clause are both about. The durable witness `docs/contributing/design/
     * 03-host-unit-tests.md` requires behind a budgeted trace site is already
     * here and is not new: the two counters above are free-build counters and
     * stay reconcilable against these lines after the budget is spent.
     */
    XHCI_DBG_VALUE_CHANGED("slot: hub marking applied, slot id << 24 | "
                           "MTT << 16 | ports << 8 | TTT",
                           XHCI_HUBMARK_TRACE_WORD(dev->SlotId, mark));
}

/*
 * Does this record owe a Configure Endpoint whose only purpose is the marking?
 *
 * Three ways to answer no, and each is load-bearing:
 *
 *   - the wanted marking is 0. The driver never issues a command to *un*-mark a
 *     hub. A node disappears when the device does, and a device that stops
 *     being a hub is not a thing that happens; what a 0 really means here is
 *     "nothing to say", and sending a Slot Context saying Hub = 0 on the
 *     strength of it would be acting on the absence of evidence.
 *   - the xHC already holds it. `HubMarkDone` is what a completed command
 *     programmed, so this is the whole of the idempotence - including the MTT
 *     that a `SET_INTERFACE` can move back and forth, which changes the wanted
 *     word and therefore re-arms this on its own.
 *   - a previous attempt failed. The need is derived, so nothing else would
 *     ever end the retry (see XHCI_DEV_FLAG_HUB_MARK_FAILED).
 *
 * Called with the lock held. IRQL: any.
 */
static ULONG xhciDevHubMarkNeeded(PXHCI_EXTENSION ext, const XHCI_DEVICE *dev)
{
    ULONG mark;

    if ((dev->Flags & XHCI_DEV_FLAG_HUB_MARK_FAILED) != 0) {
        return 0;
    }
    mark = xhciDevHubMark(ext, dev);
    return (mark != 0 && mark != dev->HubMarkDone) ? 1UL : 0UL;
}

/*
 * The command this record owes next, and which endpoint it is for.
 *
 * `PendingOp` is one slot and the EP0 chain owns it, so the need for a Configure
 * Endpoint is **derived** here instead of written there. That is not tidiness:
 * usbport opens a device's interrupt pipe while the EP0 reopen's
 * `EVALUATE_MPS` can still be owed, and an endpoint open that assigned
 * `PendingOp` would discard it - a Max Packet Size correction lost with nothing
 * anywhere reporting it. A derived need cannot collide with a stored one and
 * cannot be overwritten by the next one. Batch 7a-B's three quiescence commands
 * are derived for the same reason and from the same kind of state.
 *
 * **Quiescence outranks `PendingOp`, and that ordering is a spec requirement
 * rather than a priority.** The only op that can sit in `PendingOp` while an
 * endpoint owes a stop is `DISABLE_SLOT`, and 4.6.4 p.97 says what has to
 * happen first: "before a Disable Slot Command is issued ... any active
 * endpoints of the device slot shall be in the Stopped state or Idle in the
 * Running state, and any outstanding Transfer Events shall have been received".
 * Issuing the Disable Slot while a HID device's posted read is still Busy is
 * undefined behaviour, and unplugging one hits that on every disconnect.
 *
 * Below `PendingOp`, the EP0 chain still wins over a Configure Endpoint, which
 * is also the right order: a Configure Endpoint against a slot that is not yet
 * Addressed is a Context State Error.
 *
 * Task 7b-A.2's hub marking is derived here too, and **last**: every Configure
 * Endpoint carries A0 with the same four fields, so an endpoint that owes one
 * marks the hub on its way past and the standalone command is only reached when
 * there is no endpoint work left to ride on. Ordering it the other way would
 * issue two commands where one does.
 *
 * `binding` receives the endpoint for the three quiescence ops; it is untouched
 * for every other answer. Called with the lock held. IRQL: any.
 */
static ULONG xhciDevOwedOp(PXHCI_EXTENSION ext,
                           PXHCI_DEVICE dev,
                           PXHCI_EP_BINDING binding)
{
    ULONG op;

    /*
     * Nothing at all for a record with no slot: all three quiescence commands
     * name a Slot ID, and a device that never got one has no Endpoint Context
     * for them to reach.
     */
    if (dev->SlotId != 0) {
        op = xhciEpFirstQuiesce(dev, binding);
        if (op != XHCI_DEV_OP_NONE) {
            return op;
        }
    }
    if (dev->PendingOp != XHCI_DEV_OP_NONE) {
        return dev->PendingOp;
    }
    /*
     * **The intercepted SET_ADDRESS, derived from the transfer this record is
     * holding rather than stored when it arrives** - and the reason is the one
     * this function's header already gives for the others: a stored need can be
     * overwritten, and there is now a path that overwrites it.
     *
     * `xhciDevOweFromSlotState` re-decides the chain from the slot's own state,
     * and it writes `PendingOp`. It runs at a re-enumeration and again when a
     * command completes for a tenancy that has been re-enumerated under it - and
     * at the second of those the record can legitimately be holding an
     * *accepted* SET_ADDRESS. Stored, that transfer was either clobbered (a
     * usbport thread blocked for ever on a request nothing would issue) or had
     * to be error-completed to avoid it, which throws away an enumeration round:
     * usbport does not re-offer a transfer it has been given an error for.
     *
     * Derived, it simply waits. Whatever the chain owes runs first - it is above
     * this line - and the moment the slot is back at Default with nothing else
     * owed, the same transfer is issued from a state that accepts it.
     * `xhciDevFinishSetAddress` clearing `PendingSetAddress` is what ends the
     * need, which is the same shape as `HubMarkDone` ending the hub marking's.
     */
    if (dev->PendingSetAddress != NULL &&
        dev->State == XHCI_DEV_STATE_DEFAULT) {
        return XHCI_DEV_OP_ADDRESS_SET;
    }
    /*
     * "A Configure Endpoint Command ... is only valid if the Slot is in the
     * Addressed or Configured state" (4.6.6). This driver's `ADDRESSED` covers
     * both - it never distinguishes them, because nothing it does depends on
     * which - and every other state is either too early or unwinding.
     */
    if (!xhciDevMayOpenEndpoint(dev) ||
        dev->State != XHCI_DEV_STATE_ADDRESSED) {
        return XHCI_DEV_OP_NONE;
    }
    if (xhciEpFirstPending(dev) != NULL) {
        return XHCI_DEV_OP_CONFIGURE_EP;
    }
    /*
     * The same `ADDRESSED` gate covers the marking, and it is the spec's:
     * "Prior to command execution, a 'valid' Output Slot Context for a Configure
     * Endpoint Command requires the Slot State field to be in the Addressed or
     * Configured state. If the Slot State is not in the Addressed or Configured
     * state a Context State Error shall be generated" (6.2.2.2 p.412). A hub
     * whose descriptor arrived while its slot was still in Default - which the
     * address-0 reopen puts it back into - waits for the address rather than
     * spending a command on a Context State Error.
     */
    return xhciDevHubMarkNeeded(ext, dev) ? XHCI_DEV_OP_MARK_HUB
                                          : XHCI_DEV_OP_NONE;
}

/* A record still serving a port. GONE records are excluded on purpose: their
 * device has left, and a new one arriving on the same port must not inherit
 * either the slot or the address the old one held. */
static PXHCI_DEVICE xhciDevByHubPort(PXHCI_EXTENSION ext, ULONG hubPort)
{
    ULONG i;

    if (hubPort == 0) {
        return NULL;
    }
    for (i = 0; i < XHCI_MAX_SLOTS; i++) {
        PXHCI_DEVICE dev = &ext->Devices[i];

        if (dev->State != XHCI_DEV_STATE_FREE &&
            dev->State != XHCI_DEV_STATE_GONE &&
            dev->HubPort == hubPort) {
            return dev;
        }
    }
    return NULL;
}

/*
 * The record for a device **behind an external hub**, found by its position -
 * task 7b-A.3's counterpart to `xhciDevByHubPort`.
 *
 * A behind-hub record carries `HubPort` 0 (see XHCI_DEVICE), so the root-port
 * lookup cannot find it and must not: the record on a root port is the *hub*.
 * The key one tier down is the immediate parent's usbport address and the port
 * on it, which is what usbhub resets and what the graph claims.
 *
 * `RootPort` is required to match as well, and it is **documented as consistency
 * rather than claimed**: a mutation that removes it fails no check in the host
 * suite, and the reason is that the case it defends against cannot be built. Two
 * live records cannot share a parent address, because usbport's addresses are
 * unique among live devices - and a hub that leaves takes its subtree's records
 * with it (`xhciDevTeardownBehind`), so there is no stale record left holding a
 * parent address for a later hub to collide with. It is kept because "the same
 * position" means the whole path and not the last two steps of it, and because
 * the argument above rests on a teardown running rather than on the key itself.
 * GONE records are excluded for `xhciDevByHubPort`'s reason.
 *
 * Called with the lock held.
 */
static PXHCI_DEVICE xhciDevByHubPath(PXHCI_EXTENSION ext,
                                     ULONG rootPort,
                                     ULONG parentAddress,
                                     ULONG parentPort)
{
    ULONG i;

    if (rootPort == 0 || parentAddress == 0 || parentPort == 0) {
        return NULL;
    }
    for (i = 0; i < XHCI_MAX_SLOTS; i++) {
        PXHCI_DEVICE dev = &ext->Devices[i];

        if (dev->State == XHCI_DEV_STATE_FREE ||
            dev->State == XHCI_DEV_STATE_GONE) {
            continue;
        }
        if (dev->HubPort == 0 && dev->RootPort == rootPort &&
            dev->ParentHubAddress == parentAddress &&
            dev->ParentHubPort == parentPort) {
            return dev;
        }
    }
    return NULL;
}

/*
 * The root port the hub at this usbport address sits on, or 0 - the second half
 * of the key `xhciDevByHubPath` wants.
 *
 * The record set answers first, because a hub whose record has gone should name
 * no path at all. **But `xhciDevByAddress` requires `ADDRESS_VALID`, and that bit
 * is cleared for the whole of the hub's own address-0 window** - so during the
 * ~15 s recovery cycle in which usbhub re-enumerates a stuck hub, every
 * disconnect that hub reports about a child resolved to root port 0, found no
 * record, and left the departed child's slot and usbport address standing until
 * the position was re-enumerated or the root port dropped (audit finding A8).
 *
 * The graph is the fallback, and it is the right one: a topology node's
 * `RootPort` is where that hub's path starts, it is learned from the same
 * enumeration that created the record, and `XhciTopoDetach` removes the node
 * when the hub really goes - so this cannot resurrect a path for a hub that has
 * left. Called with the lock held.
 */
static ULONG xhciDevRootPortOfHub(PXHCI_EXTENSION ext, ULONG hubAddress)
{
    const XHCI_DEVICE *hub;
    const XHCI_TOPO_NODE *node;

    hub = xhciDevByAddress(ext, hubAddress);
    if (hub != NULL) {
        return hub->RootPort;
    }
    node = XhciTopoFind(&ext->Topology, hubAddress);
    if (node != NULL && node->RootPort != 0) {
        ext->HubRootPortsFromGraph++;
        return node->RootPort;
    }
    return 0;
}

static PXHCI_DEVICE xhciDevBySlotId(PXHCI_EXTENSION ext, ULONG slotId)
{
    ULONG i;

    if (slotId == 0) {
        return NULL;
    }
    for (i = 0; i < XHCI_MAX_SLOTS; i++) {
        PXHCI_DEVICE dev = &ext->Devices[i];

        if (dev->State != XHCI_DEV_STATE_FREE && dev->SlotId == slotId) {
            return dev;
        }
    }
    return NULL;
}

/*
 * **This record now belongs to a device that may not be the one it held
 * before** (task 9-A.2, review round 1 and corrected by round 2).
 *
 * The descriptor channel decides an action at a transfer's placement and
 * applies it at its completion, and a record can change hands inside that
 * window. It changes hands at two kinds of moment, not one: when a released
 * record is allocated again, and when a *retained* record is re-enumerated -
 * usbhub resets a port and creates a device at address 0 on it, and what
 * appears there may be a different device altogether. Round 1 advanced the
 * tenancy only at the first, which left the second - the common recovery cycle
 * this driver performs every fifteen seconds on a stuck hub - matching a stale
 * action exactly.
 *
 * Monotone within a start, so no value captured before one of those moments can
 * match one after it. Called with the lock held. IRQL: any.
 */
static VOID xhciDevNewTenancy(PXHCI_EXTENSION ext, PXHCI_DEVICE dev)
{
    ext->DeviceTenancyNext++;
    dev->Tenancy = ext->DeviceTenancyNext;
}

static PXHCI_DEVICE xhciDevAllocate(PXHCI_EXTENSION ext)
{
    ULONG i;

    for (i = 0; i < XHCI_MAX_SLOTS; i++) {
        if (ext->Devices[i].State == XHCI_DEV_STATE_FREE) {
            xhciDevNewTenancy(ext, &ext->Devices[i]);
            return &ext->Devices[i];
        }
    }
    return NULL;
}

/*
 * Is this controller in a state where device work may be done at all? The same
 * three questions the ISR and the health poll ask, in the same order: can a
 * register be touched, is this driver still admitted, and has the recovery
 * ladder ended. Called with the lock held. IRQL: any.
 */
static ULONG xhciDevAdmitted(PXHCI_EXTENSION ext)
{
    if (ext->HcInfoStatus != XHCI_HC_OK || ext->ControllerFailed) {
        return 0;
    }
    if ((ext->Flags & (XHCI_EXT_FLAG_INITIALIZED | XHCI_EXT_FLAG_RUNNING)) !=
        (XHCI_EXT_FLAG_INITIALIZED | XHCI_EXT_FLAG_RUNNING)) {
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* The completion list - called with the lock held                     */
/* ------------------------------------------------------------------ */

/*
 * Append a list of retired transfers to the ones owing a
 * UsbPortCompleteTransfer call. Oldest first throughout, because usbport's own
 * bookkeeping and the device's URB order both depend on it.
 *
 * Called with the lock held. IRQL: <= DISPATCH_LEVEL.
 */
static VOID xhciDevOweCompletion(PXHCI_EXTENSION ext, PXHCI_TRANSFER list)
{
    PXHCI_TRANSFER walk;

    while (list != NULL) {
        walk = list;
        list = walk->Next;
        walk->Next = NULL;

        /*
         * Finding A7: a completion queued while a `SubmitTransfer` bracket is
         * open must not be delivered by a drain pass that was already running
         * when the bracket closes - usbport writes the record *after* the
         * wrapper returns (SP4 `000165BD`), with nothing on its completion
         * path ordering the two (SP4 `000183C0`). The epoch stamp is what the
         * drain's pass snapshot is compared against; outside any bracket the
         * stamp is one behind so every pass qualifies.
         */
        walk->HeldEpoch = (ext->SubmitDepth != 0) ? ext->SubmitEpoch
                                                  : ext->SubmitEpoch - 1UL;

        if (ext->CompletionTail != NULL) {
            ext->CompletionTail->Next = walk;
        } else {
            ext->CompletionHead = walk;
        }
        ext->CompletionTail = walk;
        ext->CompletionsOwed++;
    }
}

/*
 * Take a transfer off the list of ones owing a `UsbPortCompleteTransfer` call,
 * if it is on it.
 *
 * **The completion list is the second place a transfer can be, and the abort
 * path has to search it too.** A Transfer Event detaches a transfer from its
 * endpoint queue and threads it here under the lock; the service call itself
 * happens after the lock is dropped (design doc 05 section 7). An abort landing
 * in that window finds the endpoint queue empty of it, concludes it was already
 * answered, and returns - and usbport then frees the record, leaving the
 * deferred drain to call `UsbPortCompleteTransfer` through freed memory. The
 * queue removal alone is only half of "nothing can complete it twice".
 *
 * Called with the lock held. IRQL: <= DISPATCH_LEVEL.
 */
static ULONG xhciDevTakeCompletion(PXHCI_EXTENSION ext, PXHCI_TRANSFER transfer)
{
    PXHCI_TRANSFER walk;
    PXHCI_TRANSFER prev;

    prev = NULL;
    for (walk = ext->CompletionHead; walk != NULL; walk = walk->Next) {
        if (walk == transfer) {
            break;
        }
        prev = walk;
    }
    if (walk == NULL) {
        return 0;
    }
    if (prev != NULL) {
        prev->Next = walk->Next;
    } else {
        ext->CompletionHead = walk->Next;
    }
    if (ext->CompletionTail == walk) {
        ext->CompletionTail = prev;
    }
    walk->Next = NULL;
    if (ext->CompletionsOwed != 0) {
        ext->CompletionsOwed--;
    }
    return 1;
}

/*
 * Ask usbport to re-offer the transfers it is holding for an endpoint.
 *
 * A SubmitTransfer refused while a command chain was running is left *queued* by
 * usbport rather than failed, but nothing re-offers it until something asks:
 * without this the retry waits on usbport's 500 ms timer, and enumeration is a
 * sequence of such refusals.
 *
 * The debt is recorded on the endpoint that owes it rather than in one slot in
 * the extension, because from task 7a-A.1 a device has several - see
 * XHCI_EXTENSION.EndpointInvalidatesOwed for what the single slot lost.
 *
 * Both are idempotent: the flag is a request to re-examine, so asking twice
 * before the deferred pass runs is one call, which is the only part of the old
 * "collapses into the first" reasoning that was right.
 *
 * Called with the lock held; the service call itself is XhciSlotDeferredWork's.
 */
static VOID xhciDevOweInvalidate(PXHCI_EXTENSION ext, PXHCI_DEVICE dev)
{
    if (dev != NULL && dev->EndpointExtension != NULL &&
        (dev->Flags & XHCI_DEV_FLAG_INVALIDATE_EP0) == 0) {
        dev->Flags |= XHCI_DEV_FLAG_INVALIDATE_EP0;
        ext->EndpointInvalidatesOwed++;
    }
}

static VOID xhciEpOweInvalidate(PXHCI_EXTENSION ext,
                                PXHCI_ENDPOINT_RECORD record)
{
    if (record != NULL && record->EndpointExtension != NULL &&
        record->InvalidateOwed == 0) {
        record->InvalidateOwed = 1;
        ext->EndpointInvalidatesOwed++;
    }
}

/*
 * Cancel an invalidation debt without performing it - the endpoint it named has
 * been unbound, so there is nothing left to re-offer.
 *
 * Its own function so that clearing the flag and dropping the running total
 * cannot be done separately, which is exactly how they came apart: REMOVE
 * cleared the flags directly and left `EndpointInvalidatesOwed` counting a debt
 * that no longer existed, permanently, until a wholesale `XhciSlotInit`.
 * `owed` is the word holding the flag - `dev->Flags` or `record->InvalidateOwed`
 * - so one mechanism serves both. Called with the lock held. IRQL:
 * <= DISPATCH_LEVEL (the suspend path reaches it at PASSIVE).
 */
static VOID xhciDevDropInvalidate(PXHCI_EXTENSION ext, PULONG owed, ULONG mask)
{
    if ((*owed & mask) == 0) {
        return;
    }
    *owed &= ~mask;
    if (ext->EndpointInvalidatesOwed != 0) {
        ext->EndpointInvalidatesOwed--;
    }
}

/*
 * The next endpoint owing an invalidation, taken off the list of debts.
 *
 * Answered as a *pointer to usbport's extension* rather than as a record,
 * because the service call happens after the lock is dropped and neither the
 * record nor the device may be touched by then. Clearing the debt here rather
 * than at the call site is what makes a failed or absent service a dropped
 * request instead of a loop.
 *
 * Called with the lock held. IRQL: <= DISPATCH_LEVEL (the suspend path
 * reaches it at PASSIVE).
 */
static PVOID xhciDevTakeInvalidate(PXHCI_EXTENSION ext)
{
    ULONG i;
    ULONG j;

    for (i = 0; i < XHCI_MAX_SLOTS; i++) {
        PXHCI_DEVICE dev = &ext->Devices[i];

        if ((dev->Flags & XHCI_DEV_FLAG_INVALIDATE_EP0) != 0) {
            PVOID endpointExtension = dev->EndpointExtension;

            xhciDevDropInvalidate(ext, &dev->Flags,
                                  XHCI_DEV_FLAG_INVALIDATE_EP0);
            if (endpointExtension != NULL) {
                return endpointExtension;
            }
        }
        for (j = 0; j < XHCI_MAX_DEVICE_ENDPOINTS; j++) {
            PXHCI_ENDPOINT_RECORD record = &dev->Endpoints[j];

            if (record->InvalidateOwed != 0) {
                PVOID endpointExtension = record->EndpointExtension;

                xhciDevDropInvalidate(ext, &record->InvalidateOwed, 1UL);
                if (endpointExtension != NULL) {
                    return endpointExtension;
                }
            }
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Carving a slot's common-buffer objects - called with the lock held  */
/* ------------------------------------------------------------------ */

/*
 * Give a record its device context and EP0 transfer ring, and publish the
 * device context in the DCBAA.
 *
 * Both objects live in the *controller* common buffer at fixed per-slot offsets,
 * which is what makes them survive the endpoint reopen that frees usbport's own
 * endpoint buffer (design doc 04 section 3.4). They are indexed by **Slot ID**,
 * so this cannot run before Enable Slot has answered - which is why it is here
 * and not at reservation time.
 *
 * The DCBAA entry is written last, after the context it names has been cleared:
 * the controller is running, so this is the first structure in this driver
 * published into a live controller's view, and the order is what makes the
 * window safe rather than an argument that there is no window.
 *
 * Returns 1 on success. IRQL: <= DISPATCH_LEVEL.
 */
static ULONG xhciDevPrepareSlot(PXHCI_EXTENSION ext, PXHCI_DEVICE dev)
{
    ULONG contextOffset;
    ULONG ringOffset;
    ULONG i;
    volatile ULONG *context;

    if (XhciDeviceContextOffset(&ext->Layout, dev->SlotId, &contextOffset) !=
            XHCI_LAYOUT_OK ||
        XhciEp0RingOffset(&ext->Layout, dev->SlotId, &ringOffset) !=
            XHCI_LAYOUT_OK) {
        return 0;
    }

    /*
     * The whole Device Context, at the stride rather than at
     * XHCI_CONTEXT_DWORDS: this one is hardware-owned output space and the xHC
     * writes every context in it, so leaving a previous slot's bytes behind
     * would hand the controller a Slot Context and 31 Endpoint Contexts full of
     * a departed device's state. The Input Context's builders write eight words
     * each because they fill *input* fields; this is the block being handed over.
     */
    context = XhciCommonAt(ext, contextOffset);
    for (i = 0; i < ext->Layout.DeviceContextBytes / sizeof(ULONG); i++) {
        context[i] = 0;
    }

    if (XhciRingInit(&dev->Ep0Ring,
                     (volatile XHCI_TRB *)XhciCommonAt(ext, ringOffset),
                     XhciCommonPA(ext, ringOffset),
                     ext->Layout.Ep0RingTrbs,
                     XHCI_RING_KIND_ENDPOINT) != XHCI_RING_OK) {
        return 0;
    }
    /* Before the re-init throws them away - this is a reopen as often as it is
     * a first open, and a slot being prepared again for the same device carries
     * whatever the previous attempt measured. */
    xhciDevFoldQueue(ext, &dev->Ep0Queue);
    XhciXferQueueInit(&dev->Ep0Queue);

    /*
     * DCBAA[SlotID] = the device context's physical address. The high half is
     * written as zero explicitly for the reason every pointer in this driver is:
     * a value that is always zero costs one store to make true and is a wild
     * pointer when it is not.
     */
    {
        volatile ULONG *entry;

        entry = XhciCommonAt(ext, ext->Layout.DcbaaOffset +
                                      dev->SlotId * XHCI_DCBAA_ENTRY_BYTES);
        entry[1] = 0;
        entry[0] = XhciCommonPA(ext, contextOffset);
    }
    dev->Flags |= XHCI_DEV_FLAG_DCBAA_SET;
    return 1;
}

/* The other end of the same publication, run after Disable Slot completes - see
 * XHCI_DEV_FLAG_DCBAA_SET for why the order is that way round. Called with the
 * lock held. */
static VOID xhciDevClearDcbaa(PXHCI_EXTENSION ext, PXHCI_DEVICE dev)
{
    volatile ULONG *entry;

    if ((dev->Flags & XHCI_DEV_FLAG_DCBAA_SET) == 0 || dev->SlotId == 0) {
        return;
    }
    if (ext->LayoutStatus != XHCI_LAYOUT_OK || ext->StartVA == 0) {
        /* The carve this entry belongs to is gone, so there is no entry to
         * clear - only a flag to drop. */
        dev->Flags &= ~XHCI_DEV_FLAG_DCBAA_SET;
        return;
    }
    entry = XhciCommonAt(ext, ext->Layout.DcbaaOffset +
                                  dev->SlotId * XHCI_DCBAA_ENTRY_BYTES);
    entry[0] = 0;
    entry[1] = 0;
    dev->Flags &= ~XHCI_DEV_FLAG_DCBAA_SET;
}

/*
 * Fill the shared Input Context for an Address Device or an Evaluate Context.
 *
 * **One Input Context serves the whole driver**, and that is sound rather than
 * frugal: the command engine allows exactly one command outstanding at a time,
 * so the block is filled and consumed between one submission and its completion.
 * A second Input Context would only be needed by a second concurrent command,
 * which the engine refuses.
 *
 * `mps0` is passed rather than read from the record because the Evaluate Context
 * path is programming a value the record does not hold yet - the record is only
 * updated when the command succeeds.
 *
 * Returns the input context's physical address, or 0 if anything refused.
 * Called with the lock held. IRQL: <= DISPATCH_LEVEL.
 */
static ULONG xhciDevBuildInput(PXHCI_EXTENSION ext,
                               PXHCI_DEVICE dev,
                               ULONG mps0,
                               ULONG slotToo)
{
    XHCI_SLOT_PARAMS slot;
    XHCI_EP_PARAMS ep0;
    ULONG controlOffset;
    ULONG slotOffset;
    ULONG epOffset;
    ULONG addFlags;
    ULONG i;

    if (XhciInputControlContextOffset(&ext->Layout, &controlOffset) !=
            XHCI_LAYOUT_OK ||
        XhciInputSlotContextOffset(&ext->Layout, &slotOffset) !=
            XHCI_LAYOUT_OK ||
        XhciInputEndpointContextOffset(&ext->Layout, 1, &epOffset) !=
            XHCI_LAYOUT_OK) {
        return 0;
    }

    /*
     * Zero the whole block at the stride first. "Zero the whole Input Context
     * before filling it" (docs/usb-xhci-info/xhci-data-structures.md section 8) - it has RsvdZ
     * padding the per-context builders do not reach, and it is reused by every
     * command, so a field one command set is a field the next inherits.
     */
    XhciContextZero(XhciCommonAt(ext, controlOffset),
                    ext->Layout.InputContextBytes / sizeof(ULONG));

    /*
     * A0 and A1 for Address Device - "Address Device sets A0 + A1" - and A1
     * alone for the MPS0 correction, because Evaluate Context "sets only the
     * A-bits of contexts being changed" and evaluating the Slot Context would
     * re-assert a Route String and speed the xHC already holds.
     */
    addFlags = XHCI_ICC_A1;
    if (slotToo) {
        addFlags |= XHCI_ICC_A0;
    }
    if (XhciBuildInputControlContext(XhciCommonAt(ext, controlOffset),
                                     addFlags, 0) != XHCI_CTX_OK) {
        return 0;
    }

    if (slotToo) {
        for (i = 0; i < sizeof(slot) / sizeof(ULONG); i++) {
            ((ULONG *)&slot)[i] = 0;
        }
        /*
         * Route String, speed, root port and the TT triple - task 7b-A.3. A
         * root-port device still gets a Route String of 0 and no TT, which is
         * what the record carries for one; the values are the record's rather
         * than a constant because a device behind a hub has all four.
         *
         * **An unresolvable TT refuses the whole build**, which the pump turns
         * into a failed record rather than a device addressed as though it sat
         * on a root port.
         */
        if (xhciDevFillSlotPosition(ext, dev, &slot) == XHCI_TT_UNRESOLVED) {
            ext->TtUnresolved++;
            XHCI_DBG_VALUE_CHANGED("slot: no Slot ID for the TT hub above a "
                                   "device, parent address",
                                   dev->ParentHubAddress);
            return 0;
        }
        if (slot.ParentSlotId != 0) {
            ext->TtProgrammed++;
        }
        /*
         * **No hub fields here, and that is the specification's instruction
         * rather than a deferral.** 6.2.2.1 p.411 lists what makes an Input Slot
         * Context valid for an Address Device and ends "and all other fields are
         * cleared to '0'"; Hub, Number of Ports and TTT are none of the seven it
         * names. Marking a hub is task 7b-A.2's Configure Endpoint, and this
         * command is what *un*-marks one - see XHCI_DEVICE.HubMarkDone.
         */
        /* "Context Entries ... 1 during Address Device" - EP0 is the only
         * endpoint this device has until Phase 7a configures another. */
        slot.ContextEntries = 1;
        slot.InterrupterTarget = 0;
        if (XhciBuildSlotContext(XhciCommonAt(ext, slotOffset), &slot) !=
            XHCI_CTX_OK) {
            return 0;
        }
    }

    /*
     * The EP0 context always names the ring's **current** dequeue position and
     * cycle, not its base and 1. On the Address Device path they are the same
     * thing, because the ring was just initialised; on the MPS0 correction path
     * they are not, and a context rebuilt from the base would restart the xHC
     * over TRBs already executed. XhciRingDequeuePA/XhciRingDequeueCycle are the
     * only two functions allowed to answer this (docs/usb-xhci-info/xhci-data-structures.md
     * section 7, "DCS is not a constant").
     */
    if (XhciBuildEp0Params(mps0,
                           XhciRingDequeuePA(&dev->Ep0Ring),
                           XhciRingDequeueCycle(&dev->Ep0Ring),
                           &ep0) != XHCI_CTX_OK) {
        return 0;
    }
    if (XhciBuildEndpointContext(XhciCommonAt(ext, epOffset), &ep0) !=
        XHCI_CTX_OK) {
        return 0;
    }

    return XhciCommonPA(ext, controlOffset);
}

/*
 * The same block, filled for a **Configure Endpoint** on one non-default
 * endpoint (task 7a-A.1). A separate function rather than another flag on the
 * one above, because almost nothing about it is the same: a different A-bit set,
 * a Slot Context whose Context Entries has to move, and an Endpoint Context at a
 * DCI the caller names.
 *
 * **A0 is set even though the Slot Context is not otherwise changing.**
 * Spec 6.2.2.2 requires the Slot Context to be valid for a Configure Endpoint,
 * and "Context Entries ... identifies the index of the last valid Endpoint
 * Context" has to grow to cover the endpoint being added. This is the step it is
 * easy to leave out - Address Device only ever writes 1 there, so nothing before
 * this task needed it to move, and an Endpoint Context the Slot Context does not
 * claim is one the xHC will not look at.
 *
 * **A D-bit beside the A-bit when the record asks for one** (task 7a-B.1). An
 * alternate-interface change reprograms an endpoint the xHC already has enabled,
 * and "xHC behavior is undefined if the Drop Context (D) flag is '0', the Add
 * Context (A) flag is '1', and the Output Endpoint Context is not in the
 * Disabled state" (4.6.6 p.106). The Drop is evaluated first - "The Drop Context
 * flags are evaluated before the Add Context flags" (p.105) - so one command
 * does both. `DropOnConfigure` is only ever set from `xhciEpStopped`, which is
 * reached exactly where the endpoint has been *shown* to be Stopped - a Stop
 * Endpoint that completed, or the Set TR Dequeue Pointer that takes one out of
 * the Error state (4.8.3 p.149). That is the other half of the same page's
 * requirement: "An endpoint shall be in the Stopped state or if in the Running
 * state shall be 'idle' ... if its Drop Context flag is set."
 *
 * The EP0 context is left as the zero the block-clear wrote and A1 is *not* set,
 * so the xHC leaves the default control endpoint exactly as it is - which is the
 * whole reason the Add flags are a set rather than a mode.
 *
 * Returns the input context's physical address, or 0 if anything refused.
 * Called with the lock held. IRQL: <= DISPATCH_LEVEL.
 */
static ULONG xhciDevBuildConfigureInput(PXHCI_EXTENSION ext,
                                        PXHCI_DEVICE dev,
                                        PXHCI_ENDPOINT_RECORD record)
{
    XHCI_SLOT_PARAMS slot;
    XHCI_EP_PARAMS ep;
    ULONG controlOffset;
    ULONG slotOffset;
    ULONG epOffset;
    ULONG i;

    if (XhciInputControlContextOffset(&ext->Layout, &controlOffset) !=
            XHCI_LAYOUT_OK ||
        XhciInputSlotContextOffset(&ext->Layout, &slotOffset) !=
            XHCI_LAYOUT_OK ||
        XhciInputEndpointContextOffset(&ext->Layout, record->Dci, &epOffset) !=
            XHCI_LAYOUT_OK) {
        return 0;
    }

    XhciContextZero(XhciCommonAt(ext, controlOffset),
                    ext->Layout.InputContextBytes / sizeof(ULONG));

    if (XhciBuildInputControlContext(
            XhciCommonAt(ext, controlOffset),
            XHCI_ICC_A0 | XHCI_ICC_FLAG(record->Dci),
            record->DropOnConfigure ? XHCI_ICC_FLAG(record->Dci) : 0UL) !=
        XHCI_CTX_OK) {
        return 0;
    }

    for (i = 0; i < sizeof(slot) / sizeof(ULONG); i++) {
        ((ULONG *)&slot)[i] = 0;
    }
    /*
     * Identical to the Address Device slot context but for Context Entries.
     * **Through the same filler**, task 7b-A.3, so a Configure Endpoint cannot
     * describe a device as sitting somewhere else than the Address Device that
     * put it there. An unresolvable TT is not fatal here - the device is already
     * addressed and working, and what fails is one endpoint rather than the
     * record - so it is counted and the command is refused, which the pump turns
     * into a failed endpoint record.
     */
    if (xhciDevFillSlotPosition(ext, dev, &slot) == XHCI_TT_UNRESOLVED) {
        ext->TtUnresolved++;
        return 0;
    }
    slot.ContextEntries = xhciDevMaxDci(dev, record->Dci);
    slot.InterrupterTarget = 0;
    /*
     * Task 7b-A.2 rides here, and not as an optimisation. A Configure Endpoint
     * has to carry a valid Slot Context whatever it was issued for (6.2.2.2
     * p.412) and A0 is mandatory on it (4.6.6 p.104), so a command built without
     * these fields would **clear** a marking the standalone command had already
     * programmed - the hub's interrupt-pipe open is the very next thing usbhub
     * does after reading the descriptor, so that is not a corner case.
     */
    xhciDevFillHubFields(ext, dev, &slot);
    if (XhciBuildSlotContext(XhciCommonAt(ext, slotOffset), &slot) !=
        XHCI_CTX_OK) {
        return 0;
    }

    /*
     * The dequeue position and cycle are taken from the **ring**, not from the
     * copy in the record: they are the two fields that move, and a context built
     * from a stale pair would start the xHC on a lap software has already left.
     * The same rule EP0's builder follows for the same reason
     * (docs/usb-xhci-info/xhci-data-structures.md section 7, "DCS is not a constant").
     */
    /* The reprogram's parameters while one is owed, the record's otherwise -
     * see `PendingParams` for why the two are not one field. */
    ep = record->DropOnConfigure ? record->PendingParams : record->Params;
    ep.DequeuePA = XhciRingDequeuePA(&record->Ring);
    ep.Dcs = XhciRingDequeueCycle(&record->Ring);
    if (XhciBuildEndpointContext(XhciCommonAt(ext, epOffset), &ep) !=
        XHCI_CTX_OK) {
        return 0;
    }

    return XhciCommonPA(ext, controlOffset);
}

/*
 * The same block, filled for a Configure Endpoint whose **only** subject is the
 * Slot Context: task 7b-A.2's hub marking, when the descriptor facts arrived
 * with no endpoint command left to carry them.
 *
 * **A0 alone, and no endpoint flag of any kind.** 4.6.6 p.104 requires it -
 * "A0 shall be set to '1' and refer to section 6.2.2.2 for the Slot Context
 * fields used by the Configure Endpoint Command" - and p.106 says exactly what
 * the absent flags mean: "If the Drop Context flag is '0' and the Add Context
 * flag is '0', the xHC shall: Do nothing. The respective Input Endpoint Context
 * is ignored by the xHC." So this command touches no endpoint, needs no
 * endpoint quiesced (the Stopped-or-idle rule on p.104 applies to an endpoint
 * "if its Drop Context flag is set"), and can be issued with the hub's
 * interrupt pipe running and a HID device's read posted behind it.
 *
 * `Context Entries` is still the highest DCI in use rather than 1: 6.2.2.2 p.412
 * requires it to be "the index of the last valid Endpoint Context that is
 * defined by the target configuration", and the target configuration here is the
 * one the device already has. Writing 1 would tell the xHC to stop looking at
 * the endpoint contexts it is servicing.
 *
 * Returns the input context's physical address, or 0 if anything refused.
 * Called with the lock held. IRQL: <= DISPATCH_LEVEL.
 */
static ULONG xhciDevBuildMarkHubInput(PXHCI_EXTENSION ext, PXHCI_DEVICE dev)
{
    XHCI_SLOT_PARAMS slot;
    ULONG controlOffset;
    ULONG slotOffset;
    ULONG i;

    if (XhciInputControlContextOffset(&ext->Layout, &controlOffset) !=
            XHCI_LAYOUT_OK ||
        XhciInputSlotContextOffset(&ext->Layout, &slotOffset) !=
            XHCI_LAYOUT_OK) {
        return 0;
    }

    XhciContextZero(XhciCommonAt(ext, controlOffset),
                    ext->Layout.InputContextBytes / sizeof(ULONG));

    if (XhciBuildInputControlContext(XhciCommonAt(ext, controlOffset),
                                     XHCI_ICC_A0, 0) != XHCI_CTX_OK) {
        return 0;
    }

    for (i = 0; i < sizeof(slot) / sizeof(ULONG); i++) {
        ((ULONG *)&slot)[i] = 0;
    }
    if (xhciDevFillSlotPosition(ext, dev, &slot) == XHCI_TT_UNRESOLVED) {
        ext->TtUnresolved++;
        return 0;
    }
    slot.ContextEntries = xhciDevMaxDci(dev, 0);
    slot.InterrupterTarget = 0;
    xhciDevFillHubFields(ext, dev, &slot);
    /*
     * A marking of nothing is not a command, because a Configure Endpoint whose
     * Slot Context says Hub = 0 would *clear* a marking rather than skip one.
     *
     * Unreachable through the pump, and stated rather than left implied: the
     * pump derives the need and builds the command inside one hold of the
     * controller lock, so the graph cannot move between the two reads. It is
     * here because the derivation and the build are separate functions and only
     * their current caller makes them atomic.
     */
    if (slot.Hub == 0) {
        return 0;
    }
    if (XhciBuildSlotContext(XhciCommonAt(ext, slotOffset), &slot) !=
        XHCI_CTX_OK) {
        return 0;
    }

    return XhciCommonPA(ext, controlOffset);
}

/* ------------------------------------------------------------------ */
/* Teardown - called with the lock held                                */
/* ------------------------------------------------------------------ */

/*
 * Take every transfer this device still owns off its queue, and the intercepted
 * SET_ADDRESS with them, and put them on the completion list.
 *
 * The ring is deliberately **not** repositioned. Reclaiming TRBs the xHC may
 * still be executing needs Stop Endpoint and Set TR Dequeue Pointer, neither of
 * which can be issued from a context that may not wait, and both of which are
 * task 7a-B.1's. On a teardown that costs nothing, because the whole slot is
 * about to be disabled; the note matters for the REMOVE path, which is where the
 * ring survives the drain.
 */
/* One endpoint's queue, detached and put on the completion list. Split out
 * because a REMOVE names one endpoint and a teardown names them all, and the two
 * must not be able to disagree about what cancelling a queue does.
 * Called with the lock held. IRQL: <= DISPATCH_LEVEL. */
static VOID xhciDevCancelQueue(PXHCI_EXTENSION ext,
                               PXHCI_TRANSFER_QUEUE queue,
                               LONG usbdStatus)
{
    PXHCI_TRANSFER list;
    ULONG count;
    ULONG lostBefore;

    count = 0;
    /* A drained transfer may still be holding a deferral, and a teardown is no
     * evidence about the tail it was waiting for - so the observation ends here
     * as lost rather than as either verdict (task 9-0.2). */
    lostBefore = queue->MidTdDeferralsLost;
    list = XhciXferQueueDrain(queue, usbdStatus, &count);
    xhciDevFoldDeferralsLost(ext, queue, lostBefore);
    if (count != 0) {
        ext->TransfersCancelled += count;
        xhciDevOweCompletion(ext, list);
    }
}

/* The intercepted SET_ADDRESS, which is EP0's alone and is held on the device
 * rather than in any queue - so no drain can see it and both the teardown and
 * an EP0 REMOVE have to ask for it by name.
 * Called with the lock held. IRQL: <= DISPATCH_LEVEL. */
static VOID xhciDevCancelSetAddress(PXHCI_EXTENSION ext,
                                    PXHCI_DEVICE dev,
                                    LONG usbdStatus)
{
    PXHCI_TRANSFER setAddress;

    if (dev->PendingSetAddress == NULL) {
        return;
    }
    setAddress = dev->PendingSetAddress;
    dev->PendingSetAddress = NULL;
    setAddress->UsbdStatus = usbdStatus;
    setAddress->BytesTransferred = 0;
    setAddress->Next = NULL;
    ext->TransfersCancelled++;
    xhciDevOweCompletion(ext, setAddress);
}

/* Every queue this record owns, plus the intercepted SET_ADDRESS.
 * Called with the lock held. IRQL: <= DISPATCH_LEVEL. */
static VOID xhciDevCancelWork(PXHCI_EXTENSION ext,
                              PXHCI_DEVICE dev,
                              LONG usbdStatus)
{
    ULONG i;

    xhciDevCancelQueue(ext, &dev->Ep0Queue, usbdStatus);

    /*
     * The non-default endpoints too (task 7a-A.1). Every reason this function
     * exists applies to them identically - a transfer nobody answers is a thread
     * usbport leaves blocked whichever pipe it was on - and the queues are
     * separate only because the *rings* are.
     */
    for (i = 0; i < XHCI_MAX_DEVICE_ENDPOINTS; i++) {
        if (dev->Endpoints[i].Dci != 0) {
            xhciDevCancelQueue(ext, &dev->Endpoints[i].Queue, usbdStatus);
        }
    }

    xhciDevCancelSetAddress(ext, dev, usbdStatus);
}

/*
 * The Slot State the **xHC** reports for this slot, read out of the Output Slot
 * Context - DW3 `31:27` (`docs/usb-xhci-info/xhci-data-structures.md`, Slot
 * Context table).
 *
 * The same argument as `xhciEpHardwareState` below, at slot granularity, and the
 * repo audit's finding A2 is what it is for: three commands of the re-enumeration
 * chain have *different* legal Slot State lists (the "Which Slot State each
 * command requires" table in that document), so a caller choosing between them
 * has to know which state the slot is actually in. This driver's own
 * `dev->State` is bookkeeping - it does not distinguish Addressed from
 * Configured at all, and it is written by paths the xHC never answered.
 *
 * Returns 1 and writes `*state` when the context is readable at all; the gates
 * are `xhciEpHardwareState`'s, for its reasons.
 *
 * Called with the lock held. IRQL: <= DISPATCH_LEVEL.
 */
static ULONG xhciDevHardwareSlotState(PXHCI_EXTENSION ext,
                                      PXHCI_DEVICE dev,
                                      ULONG *state)
{
    volatile ULONG *context;
    ULONG offset;

    if (ext->LayoutStatus != XHCI_LAYOUT_OK || ext->StartVA == 0 ||
        dev->SlotId == 0 || (dev->Flags & XHCI_DEV_FLAG_DCBAA_SET) == 0) {
        return 0;
    }
    if (XhciDeviceContextOffset(&ext->Layout, dev->SlotId, &offset) !=
        XHCI_LAYOUT_OK) {
        return 0;
    }
    context = XhciCommonAt(ext, offset);
    *state = XHCI_SLOT_GET_STATE(context[3]);
    return 1;
}

/*
 * An Address Device has succeeded, so the xHC has an Output Endpoint Context for
 * EP0 again - "The Address Device Command transitions the Default Control
 * Endpoint from the Disabled to the Running state" (4.8.3 p.148, transition 1 of
 * Figure 4-5), and that is true of both the BSR form and the addressing one. A
 * Reset Device is the third caller and says the same thing in its own words: for
 * the Default Control Endpoint the xHC shall "transition the endpoint to the
 * Running state" (4.6.11 p.129).
 *
 * **One function rather than the two lines written at each of the sites**,
 * for the reason `xhciDevDisableCompleted` is one: the branches owe the
 * controller the same thing, and a rule duplicated at several sites is a rule a
 * change can delete from one of them.
 *
 * This is EP0's half of the `XHCI_EPQ_NO_CONTEXT` lifecycle. A non-default
 * endpoint gets its context back from a Configure Endpoint and is cleared there;
 * **EP0 never goes through one**, so without this the bit set by a Stop Endpoint
 * that read Disabled would outlive the very command that undoes it, and every
 * later control transfer would be failed by the submit gate for the life of the
 * device record. The address-zero reopen re-enters the chain at one of those
 * three commands, so this is an ordinary re-enumeration rather than an exotic
 * path.
 *
 * Called with the lock held. IRQL: DISPATCH_LEVEL.
 */
static VOID xhciDevEp0Restored(PXHCI_DEVICE dev)
{
    /*
     * The endpoint-level verdicts go with the context, the same set the Reset
     * Device arm clears for the non-default records: EP0 is Running again
     * after each of the three commands, so a chain FAILED under the previous
     * context (a Stop or Reset Endpoint the engine lost or the xHC refused)
     * would otherwise outlive the re-enumeration that is the documented
     * recovery for it, and the submit gate would fail every EP0 transfer from
     * the first GET_DESCRIPTOR on. The owed and outstanding bits are software
     * debt and stay, as they do in that arm.
     */
    dev->Ep0Quiesce.Flags &= ~(XHCI_EPQ_NO_CONTEXT | XHCI_EPQ_HALTED |
                               XHCI_EPQ_STOPPED | XHCI_EPQ_FAILED |
                               XHCI_EPQ_UNAVAILABLE);
    dev->Ep0Quiesce.StoppedArmed = 0;
}

/* ------------------------------------------------------------------ */
/* Batch 7a-B: the endpoint quiescence machine                         */
/* ------------------------------------------------------------------ */

/*
 * The EP State the **xHC** reports for this endpoint, read out of the Output
 * Endpoint Context in the device context this driver published.
 *
 * It exists because spec 4.6.9 p.123 says to read it rather than infer it: a
 * Busy endpoint "may asynchronously transition from the Running to the Halted or
 * Error state", so a Stop Endpoint can answer Context State Error for three
 * different reasons, and "software may verify that this case occurred by
 * inspecting the EP State for Halted or Error when a Stop Endpoint Command
 * results in a Context State Error". Those three lead to three different next
 * commands, and this driver's own `HALTED` bit is bookkeeping rather than
 * evidence.
 *
 * Returns 1 and writes `*state` when the context is readable at all. The gates
 * are the same ones `xhciDevClearDcbaa` uses: without a carve, a Slot ID and a
 * published DCBAA entry there is no Output Device Context to read.
 *
 * Called with the lock held. IRQL: <= DISPATCH_LEVEL.
 */
static ULONG xhciEpHardwareState(PXHCI_EXTENSION ext,
                                 PXHCI_DEVICE dev,
                                 ULONG dci,
                                 ULONG *state)
{
    volatile ULONG *context;
    ULONG offset;

    if (ext->LayoutStatus != XHCI_LAYOUT_OK || ext->StartVA == 0 ||
        dev->SlotId == 0 || (dev->Flags & XHCI_DEV_FLAG_DCBAA_SET) == 0) {
        return 0;
    }
    if (XhciEndpointContextOffset(&ext->Layout, dev->SlotId, dci, &offset) !=
        XHCI_LAYOUT_OK) {
        return 0;
    }
    context = XhciCommonAt(ext, offset);
    *state = XHCI_EP_GET_STATE(context[0]);
    return 1;
}

/* The invalidation debt, posted against whichever owner this endpoint has. One
 * function so that no batch 7a-B path has to remember which of the two shapes it
 * is looking at. Called with the lock held. */
static VOID xhciEpOweInvalidateAny(PXHCI_EXTENSION ext,
                                   PXHCI_DEVICE dev,
                                   PXHCI_EP_BINDING binding)
{
    if (binding->Record != NULL) {
        xhciEpOweInvalidate(ext, binding->Record);
    } else {
        xhciDevOweInvalidate(ext, dev);
    }
}

/*
 * Task 8-A.1. If this endpoint refused a submission for want of ring space and
 * space has since appeared, ask usbport to offer it again.
 *
 * **One function, two triggers, and that is deliberate.** The trigger that
 * matters is a completion, because retiring a TD is what actually returns TRBs to
 * the ring and it is the event a bulk device produces continuously. The health
 * poll calls the same function over every live endpoint as a backstop, for the
 * reclaims a completion is not: a Set TR Dequeue Pointer placement jumps the
 * dequeue pointer past TRBs no event ever retired, and a queue drained by a
 * teardown frees the lot. Writing the release at each of those sites instead is
 * the shape batch 7a-B's sweep spent four rounds undoing - the property belongs
 * to the endpoint, not to whichever path happened to free the space.
 *
 * The re-offer is posted as an ordinary invalidation debt rather than called
 * here, because `UsbPortInvalidateEndpoint` is a usbport service and this runs
 * under the controller lock (design doc 05: decide under the lock, act after
 * releasing it).
 *
 * Called with the lock held. IRQL: <= DISPATCH_LEVEL.
 */
static VOID xhciEpReleaseRetry(PXHCI_EXTENSION ext,
                               PXHCI_DEVICE dev,
                               PXHCI_EP_BINDING binding)
{
    if (!XhciXferQueueRetryDue(binding->Queue, binding->Ring)) {
        return;
    }
    /*
     * Disarmed and counted together, and *before* the debt is posted rather than
     * after: `xhciEpOweInvalidateAny` declines silently for an endpoint whose
     * usbport extension has gone, and a latch left armed behind that refusal
     * would be re-examined on every poll for the life of the record. There is
     * nothing to re-offer to in that case, which is the same thing the debt
     * itself concludes.
     */
    XhciXferQueueDisarmRetry(binding->Queue, 1);
    ext->EndpointRetriesAsked++;
    xhciEpOweInvalidateAny(ext, dev, binding);
}

/*
 * Raise the position debt - **the one place it is raised**, so that no site can
 * set `XHCI_EPQ_REPOSITION` without also invalidating a Set TR Dequeue Pointer
 * that is already in flight. `force` says the endpoint's own position is not
 * usable (a reset or the Error state); `unowned` says the ring may not be
 * rewritten yet. Called with the lock held. IRQL: any.
 */
static VOID xhciEpOweReposition(PXHCI_EP_QUIESCE quiesce,
                                ULONG force,
                                ULONG unowned)
{
    quiesce->Flags |= XHCI_EPQ_REPOSITION;
    if (force) {
        quiesce->Flags |= XHCI_EPQ_FORCE_DEQUEUE;
    }
    if (unowned) {
        quiesce->Flags |= XHCI_EPQ_UNOWNED_RING;
    }
    /*
     * Whatever command is in flight was built before this debt existed, so its
     * success may not be taken as having paid it - and **the restart it would
     * have ended with is stale too**. Ringing the doorbell on the strength of a
     * placement that no longer describes the ring is how a TD removed since gets
     * executed: the seventh review round found exactly that, an abort during a
     * cancellation pass leaving a `RESTART` from the abort before it.
     */
    quiesce->Flags &= ~(XHCI_EPQ_DEQUEUE_ISSUED | XHCI_EPQ_RESTART);
}

static VOID xhciEpMarkHalted(PXHCI_EXTENSION ext,
                             PXHCI_EP_QUIESCE quiesce,
                             ULONG dci)
{
    if ((quiesce->Flags & XHCI_EPQ_HALTED) != 0) {
        return;
    }
    quiesce->Flags |= XHCI_EPQ_HALTED;
    ext->EndpointHalts++;
    /*
     * **Task 11-V.9's third tier, and it is change-gated by construction**: the
     * early return above means this function only reaches here on the
     * transition into halted, so an endpoint that stays halted through a
     * recovery chain produces one record and not one per event. That is the
     * tier's whole rule, and here it costs nothing to keep because the code
     * already had to be written that way for `EndpointHalts` to mean anything.
     */
    XhciLogNoteLocked(ext, "ep.halted", dci);
}

/*
 * The chain cannot be completed.
 *
 * **Nothing is drained here**, and that is the same rule the rest of this file
 * follows: a completion hands the transfer's mapped buffer back, and this path
 * is precisely the one where nothing has shown the xHC stopped reading it. The
 * queued transfers stay queued and are answered by whichever path does prove it
 * - `XhciSlotInit` after an HCRST, or a Disable Slot whose completion says the
 * slot went. What does change is that new submissions are *failed* rather than
 * refused for a retry that will never succeed.
 *
 * Called with the lock held.
 */
static VOID xhciEpQuiesceFail(PXHCI_EXTENSION ext,
                              PXHCI_DEVICE dev,
                              PXHCI_EP_BINDING binding,
                              ULONG why)
{
    /*
     * **The position debt is kept**, and the fourth review round is why. Only
     * the bits about the command that just failed are dropped: the NEED/BUSY
     * pair, the restart it would have ended with, and the drain and reconfigure
     * intents that need a stop this chain did not achieve.
     *
     * `XHCI_EPQ_REPOSITION` and `XHCI_EPQ_FORCE_DEQUEUE` are not about the
     * command - they are about the *ring*, and a failed Set TR Dequeue Pointer
     * leaves that debt exactly as outstanding as it was. Clearing them made a
     * later reset-pipe retry unsafe: the chain would rearm, reach Stopped and
     * place nothing, and the next doorbell would ring the endpoint at the TD
     * that halted it (4.6.8 p.117, 4.10.1 p.172).
     */
    binding->Quiesce->Flags &= ~(XHCI_EPQ_INFLIGHT | XHCI_EPQ_RESTART |
                                 XHCI_EPQ_DRAIN | XHCI_EPQ_RECONFIGURE |
                                 XHCI_EPQ_UNAVAILABLE);
    binding->Quiesce->Flags |= XHCI_EPQ_FAILED;
    /*
     * **The record's own state is left alone**, and the second review round is
     * why. It used to be set to `XHCI_EP_REC_FAILED` here, which conflated two
     * different facts: that state says what a *Configure Endpoint* achieved, and
     * a stop this driver could not complete says nothing about whether the xHC
     * still has the endpoint configured. Worse, it was unrecoverable - the
     * reset-pipe of task 7a-B.3 clears `XHCI_EPQ_FAILED` and re-arms the chain,
     * but nothing put the record back, so every later submission failed on the
     * record's state before the recovered quiescence state was ever consulted.
     * `XHCI_EPQ_FAILED` alone already fails submissions, and it alone is what a
     * reset-pipe can clear.
     */
    xhciEpOweInvalidateAny(ext, dev, binding);
    /*
     * Counted here rather than at the call sites, because this function is the
     * whole of the state the save gate declines on for good: it is the only
     * place `XHCI_EPQ_FAILED` is set without `XHCI_EPQ_UNAVAILABLE` beside it,
     * and every one of its ten callers reaches it. `EndpointQuiesceLost` names
     * one caller and `EndpointQuiesceUnavailable` names the *other* state
     * entirely - neither can answer "is this controller in it". Task 12.1's
     * published limitation is what needs the answer; see the field in xhci.h.
     */
    ext->EndpointQuiesceFailures++;
    XHCI_DBG_VALUE_CHANGED("slot: endpoint quiescence failed, dci << 8 | op",
                           (binding->Dci << 8) | why);
}

/*
 * Choose where execution resumes on a ring the xHC is no longer executing, and
 * owe the Set TR Dequeue Pointer that programs it.
 *
 * **The queue is the record of what survived**, and there is deliberately no
 * stored list of cancelled TRB ranges beside it. usbport aborts transfers one at
 * a time, several per endpoint-worker pass, so a range list would have to grow;
 * and a list plus the queue it describes are two statements of one fact. So
 * every outstanding TRB from the oldest surviving transfer forward that no
 * surviving transfer owns is a cancelled TD's leftover, and it is rewritten as a
 * No Op - which is the use the spec offers the state for: "While the endpoint is
 * stopped, software may add, delete, or otherwise rearrange TDs on an associated
 * Transfer Ring ... or to 'abort' one or more TDs by removing them from the
 * ring" (4.6.9 p.119).
 *
 * Leftovers *before* the oldest survivor need no rewrite: moving the dequeue
 * pointer forward past them reclaims them in one store.
 *
 * Called with the lock held, and only from a path that has established the
 * endpoint is not executing. IRQL: <= DISPATCH_LEVEL.
 */
static VOID xhciEpPlaceDequeue(PXHCI_EXTENSION ext,
                               PXHCI_DEVICE dev,
                               PXHCI_EP_BINDING binding)
{
    PXHCI_RING ring;
    PXHCI_TRANSFER_QUEUE queue;
    ULONG target;
    ULONG index;
    ULONG steps;
    ULONG stoppedTail;
    ULONG stoppedSurvived;
    ULONG force;

    ring = binding->Ring;
    queue = binding->Queue;

    /*
     * **Read, not consumed.** The fifth review round found the first draft
     * clearing it here: both position bits describe a debt against the *ring*,
     * and clearing them before the placement - or before the command that
     * carries it - has completed means a failure at either point loses the debt
     * silently. They are dropped only where the debt is discharged: the branch
     * below that decides no command is needed, and the successful Set TR Dequeue
     * Pointer completion.
     */
    force = (binding->Quiesce->Flags & XHCI_EPQ_FORCE_DEQUEUE) ? 1UL : 0UL;

    stoppedSurvived = 0;
    if (ring->Dequeue != ring->Enqueue) {
        /*
         * **The TD at the dequeue pointer is the one the xHC may have stopped
         * inside, and it is off limits.** "If the xHC stopped in the middle of a
         * TD, then that TD may not be modified by software, however any other
         * TDs on the ring may be" (4.6.9 p.121). Software cannot tell whether
         * the stop landed inside it or before it, so the conservative reading is
         * the only one available: never rewrite it, and never step the dequeue
         * pointer over it unless its transfer is gone.
         */
        stoppedTail = ring->Dequeue;
        (VOID)XhciRingTdBounds(ring, ring->Dequeue, NULL, &stoppedTail);
        stoppedSurvived = XhciXferQueueOwnsIndex(queue, ring, ring->Dequeue);

        /*
         * Every outstanding TRB *after* that TD which no surviving transfer owns
         * is a cancelled TD's leftover, and rewriting it is what the stopped
         * state is offered for (4.6.9 p.119). One that is already a No Op is
         * left alone, so the counter measures TDs removed rather than passes
         * over them.
         *
         * **Skipped entirely while the ring is not software's.** An endpoint
         * found in the Error state has not been through a Stop Endpoint, so its
         * TRBs may still be owned or prefetched by the xHC; the rewrite is
         * deferred to the pass after the Set TR Dequeue Pointer that takes it to
         * Stopped - see `XHCI_EPQ_UNOWNED_RING`.
         */
        if ((binding->Quiesce->Flags & XHCI_EPQ_UNOWNED_RING) == 0) {
            index = XhciRingNextIndex(ring, stoppedTail);
            for (steps = 0; steps < ring->Trbs && index != ring->Enqueue;
                 steps++) {
                if (!XhciXferQueueOwnsIndex(queue, ring, index) &&
                    XHCI_TRB_GET_TYPE(ring->Base[index].Control) !=
                        XHCI_TRB_TYPE_NOOP) {
                    if (XhciRingNoOpAt(ring, index) == XHCI_RING_OK) {
                        ext->TransfersNoOpped++;
                    }
                }
                index = XhciRingNextIndex(ring, index);
            }
        }
    }

    if (stoppedSurvived && !force) {
        /*
         * The transfer the endpoint stopped on is still wanted, and the stop
         * wrote the endpoint's own final Dequeue Pointer into the Endpoint
         * Context (4.6.9 p.119) - so a doorbell resumes from it with the partial
         * progress intact, where a Set TR Dequeue Pointer would discard that
         * progress (4.6.10 p.128). The leftovers behind it have already been
         * rewritten. `XHCI_EPQ_FORCE_DEQUEUE` is the case where that reasoning
         * does not hold, because the endpoint did not reach this state through a
         * Stop Endpoint: a *reset* leaves the position on the TD that halted,
         * and an endpoint found in the *Error* state is only taken out of it by
         * this very command (4.8.3 p.149).
         */
        binding->Quiesce->Flags &= ~(XHCI_EPQ_NEED_DEQUEUE |
                                     XHCI_EPQ_REPOSITION |
                                     XHCI_EPQ_FORCE_DEQUEUE |
                                     XHCI_EPQ_UNOWNED_RING);
        if ((binding->Quiesce->Flags & XHCI_EPQ_PAUSED) == 0) {
            binding->Quiesce->Flags |= XHCI_EPQ_RESTART;
        }
        return;
    }

    if (queue->Count == 0 || queue->Head == NULL) {
        /* Nothing survived. The enqueue position is the one target
         * XhciRingSetDequeue accepts unconditionally, and it leaves the ring
         * empty rather than holding TRBs no transfer owns. */
        target = XhciRingTrbPA(ring, ring->Enqueue);
    } else {
        target = XhciRingTrbPA(ring, queue->Head->FirstIndex);
    }

    if (XhciRingSetDequeue(ring, target) != XHCI_RING_OK) {
        /* The ring refused the position rather than corrupting itself, so the
         * software and hardware pointers cannot be brought together from here. */
        ext->EndpointPlacementFailures++;
        xhciEpQuiesceFail(ext, dev, binding, XHCI_DEV_OP_SET_DEQUEUE);
        return;
    }

    /*
     * The pair the command carries, taken from the ring rather than from the
     * target computed above: `XhciRingDequeuePA` and `XhciRingDequeueCycle` are
     * the only two functions allowed to answer this, and DCS "is not a constant"
     * (docs/usb-xhci-info/xhci-data-structures.md section 7).
     */
    binding->Quiesce->DequeuePA = XhciRingDequeuePA(ring) |
                                  XhciRingDequeueCycle(ring);
    binding->Quiesce->Flags |= XHCI_EPQ_NEED_DEQUEUE;
    /*
     * **A paused endpoint is not restarted**, and that is the whole mitigation
     * for the window `AbortTransfer` cannot close: usbport is in a cancellation
     * pass, and the abort callbacks that end it must find the endpoint still
     * Stopped or their TRBs are live while usbport unmaps their buffers. See
     * `XHCI_EPQ_PAUSED`. Restarting it here - which the first draft did - put
     * the endpoint back into Running one command round *before* the abort
     * arrived, which is worse than never having stopped it.
     */
    if (queue->Count != 0 &&
        (binding->Quiesce->Flags & XHCI_EPQ_PAUSED) == 0) {
        binding->Quiesce->Flags |= XHCI_EPQ_RESTART;
    }
}

/*
 * Restart an endpoint this driver left Stopped with work still on it.
 *
 * The doorbell is the only thing that takes a Stopped endpoint back to Running -
 * "The next time the doorbell is rung, the endpoint shall start execution at the
 * beginning of the TRB referenced by the TR Dequeue Pointer" (4.6.9 p.119) - and
 * `XHCI_EPQ_PAUSED` is what deferred it to here instead of to the placement.
 *
 * Nothing happens unless every reason to have held it has gone: no command in
 * flight (a doorbell would put the endpoint back in Running under a Set TR
 * Dequeue Pointer that requires Stopped), no failed chain, work to run, and a
 * controller that can be talked to.
 *
 * Called with the lock held. IRQL: <= DISPATCH_LEVEL.
 */
/* Returns nonzero only when the doorbell was actually rung (Phase 7 review,
 * B6): the poll's counter reads "the net had to act", and a declined attempt
 * is not an act. */
static ULONG xhciEpRestartIfStopped(PXHCI_EXTENSION ext,
                                    PXHCI_DEVICE dev,
                                    PXHCI_EP_BINDING binding)
{
    PXHCI_EP_QUIESCE quiesce;

    quiesce = binding->Quiesce;
    quiesce->Flags &= ~XHCI_EPQ_PAUSED;
    quiesce->StoppedArmed = 0;

    if ((quiesce->Flags & (XHCI_EPQ_FAILED | XHCI_EPQ_NO_CONTEXT)) != 0 ||
        binding->Queue->Count == 0 || dev->SlotId == 0 ||
        !xhciDevAdmitted(ext)) {
        return 0;
    }
    if ((quiesce->Flags & XHCI_EPQ_INFLIGHT) != 0) {
        /*
         * **The request is recorded rather than dropped.** A chain is running,
         * so the doorbell cannot be rung here - but the placement that started
         * it left no `RESTART` behind, because `XHCI_EPQ_PAUSED` was still set
         * when it ran. Returning without a trace lost the restart between the
         * two, and the endpoint then sat stopped with a HID device's read on it
         * until the health poll's net noticed - a visible freeze and a nonzero
         * `EndpointRestartsByPoll`.
         */
        quiesce->Flags |= XHCI_EPQ_RESTART;
        return 0;
    }
    if ((quiesce->Flags & XHCI_EPQ_STOPPED) == 0) {
        return 0;
    }
    quiesce->Flags &= ~XHCI_EPQ_STOPPED;
    XhciWriteDoorbell(ext, dev->SlotId, binding->Dci);
    ext->EndpointRestarts++;
    return 1;
}

/*
 * Ring the doorbell for a restart that was *asked for* rather than derived -
 * `XHCI_EPQ_RESTART` - once nothing stands in its way.
 *
 * It exists because the bit has two producers and used to have one consumer.
 * The placement sets it, and the Set TR Dequeue Pointer that follows consumed
 * it; but a stop whose placement decides no command is needed leaves it set with
 * no command to carry it, and so does `SetEndpointState(ACTIVE)` arriving while
 * a chain is in flight. Either way the endpoint stayed stopped with work on it
 * until the health poll's net - a visible freeze, and a nonzero
 * `EndpointRestartsByPoll` that should have been zero.
 *
 * Called at the end of every quiescence completion, which is the one place all
 * of those paths converge. Called with the lock held.
 */
static VOID xhciEpHonourRestart(PXHCI_EXTENSION ext,
                                PXHCI_DEVICE dev,
                                PXHCI_EP_BINDING binding)
{
    PXHCI_EP_QUIESCE quiesce;

    quiesce = binding->Quiesce;
    if ((quiesce->Flags & XHCI_EPQ_RESTART) == 0) {
        return;
    }
    if ((quiesce->Flags & (XHCI_EPQ_INFLIGHT | XHCI_EPQ_FAILED |
                           XHCI_EPQ_PAUSED)) != 0) {
        /* Another command is owed, the chain has been given up on, or usbport's
         * cancellation pass is still running. The request keeps until it is not. */
        return;
    }
    quiesce->Flags &= ~XHCI_EPQ_RESTART;
    /* `XHCI_EPQ_NO_CONTEXT` is here rather than with the group above because it
     * is not a "keeps until it is not" condition: an endpoint the xHC has no
     * Endpoint Context for cannot be rung and cannot start owing one, so the
     * request is dropped rather than held. */
    if ((quiesce->Flags & (XHCI_EPQ_STOPPED | XHCI_EPQ_NO_CONTEXT)) !=
            XHCI_EPQ_STOPPED ||
        binding->Queue->Count == 0 || dev->SlotId == 0 ||
        !xhciDevAdmitted(ext)) {
        return;
    }
    quiesce->Flags &= ~XHCI_EPQ_STOPPED;
    quiesce->StoppedArmed = 0;
    XhciWriteDoorbell(ext, dev->SlotId, binding->Dci);
    ext->EndpointRestarts++;
}

/*
 * The xHC reported this endpoint **Disabled**. Ask for its Endpoint Context
 * back, because nothing else will.
 *
 * `XHCI_EPQ_NO_CONTEXT` makes the endpoint safe - no doorbell, no TRB published
 * - but safety is not recovery, and the eleventh review round is why this
 * exists: a plain cancellation `PAUSE` arms no reconfigure, so `xhciEpStopped`
 * cleared the position debts and stopped there, and every later submission was
 * accepted and failed with `INTERNAL_HC_ERROR` for the life of the record. A
 * same-parameter reopen only rebinds; only a *changed* parameter armed a
 * Configure Endpoint. So the pipe was permanently stranded and nothing said so.
 *
 * The command is an **Add with no Drop**, which is legal here and only here:
 * "xHC behavior is undefined if the Drop Context (D) flag is '0', the Add
 * Context (A) flag is '1', and the Output Endpoint Context is not in the
 * Disabled state" (4.6.6 p.106) - and Disabled is exactly what was just read out
 * of it. `DropOnConfigure` is therefore cleared rather than set, which is the
 * opposite of the alternate-interface path and for the opposite reason.
 *
 * Putting the record back to `XHCI_EP_REC_PENDING` is the whole mechanism: the
 * pump issues the Configure Endpoint from that state, submissions meanwhile are
 * refused for a retry rather than failed, and its success clears
 * `XHCI_EPQ_NO_CONTEXT` at the one site that may.
 *
 * **Nothing is done for EP0** (`Record == NULL`): its context comes from an
 * Address Device, which `xhciDevEp0Restored` handles, and a Configure Endpoint
 * "does not affect the Default Control Endpoint" (4.8.3 p.149).
 *
 * Called with the lock held, *after* `xhciEpStopped`, so the reconfigure that
 * schedules its own Drop+Add is already visible in the record's state and is not
 * overwritten. IRQL: <= DISPATCH_LEVEL.
 */
static VOID xhciEpOweContextRestore(PXHCI_EXTENSION ext,
                                    PXHCI_EP_BINDING binding)
{
    PXHCI_ENDPOINT_RECORD record;

    record = binding->Record;
    if (record == NULL) {
        return;
    }
    /*
     * `CONFIGURED` is what says no other command is already owed for this
     * record: the reconfigure branch of `xhciEpStopped` has just set `PENDING`
     * with its Drop, a refused placement has left `FAILED` on the quiescence,
     * and neither wants a second opinion from here.
     */
    if ((binding->Quiesce->Flags & XHCI_EPQ_FAILED) != 0 ||
        record->State != XHCI_EP_REC_CONFIGURED) {
        return;
    }
    record->DropOnConfigure = 0;
    record->State = XHCI_EP_REC_PENDING;
    ext->EndpointContextRestores++;
    XHCI_DBG_VALUE_CHANGED("slot: endpoint found Disabled, asking for its "
                           "context back, dci", binding->Dci);
}

/* Defined with the other EP Type predicates further down, and declared here
 * because the reconfigure branch below commits a new endpoint description and
 * has to give the ring the kind that description implies. */
static ULONG xhciEpTypeIsIsoch(ULONG epType);

/*
 * The endpoint has been shown not to be executing its ring. Pay whatever the
 * arming path asked for.
 *
 * `mayReposition` is 0 only where there is provably no Output Endpoint Context
 * to program - a device with no Slot ID, or an endpoint the xHC reports as
 * Disabled - because a Set TR Dequeue Pointer there would be refused for exactly
 * the reason the stop was.
 *
 * Called with the lock held. IRQL: <= DISPATCH_LEVEL.
 */
static VOID xhciEpStopped(PXHCI_EXTENSION ext,
                          PXHCI_DEVICE dev,
                          PXHCI_EP_BINDING binding,
                          ULONG mayReposition)
{
    PXHCI_EP_QUIESCE quiesce;
    ULONG reconfigure;

    quiesce = binding->Quiesce;
    quiesce->Flags |= XHCI_EPQ_STOPPED;
    reconfigure = (quiesce->Flags & XHCI_EPQ_RECONFIGURE) != 0;

    if ((quiesce->Flags & XHCI_EPQ_DRAIN) != 0) {
        quiesce->Flags &= ~XHCI_EPQ_DRAIN;
        /*
         * Now, and not before: the endpoint is not executing, so its TRBs
         * cannot be executed, so the buffers those TRBs name cannot be written
         * - which is the whole precondition `UsbPortCompleteTransfer` needs.
         * This is the ordering 4.6.4 p.97 requires from the other side, and it
         * is what replaces Phase 6's "wait for the Disable Slot".
         */
        if (binding->Queue->Count != 0) {
            xhciEpOweReposition(quiesce, 0, 0);
        }
        xhciDevCancelQueue(ext, binding->Queue, XHCI_USBD_STATUS_CANCELED);
    }

    if (reconfigure) {
        quiesce->Flags &= ~(XHCI_EPQ_RECONFIGURE | XHCI_EPQ_RESTART |
                            XHCI_EPQ_NEED_DEQUEUE | XHCI_EPQ_REPOSITION |
                            XHCI_EPQ_FORCE_DEQUEUE | XHCI_EPQ_UNOWNED_RING);
        if (binding->Record != NULL) {
            /*
             * **The ring has to be emptied, and draining the queue did not do
             * it.** `XhciXferQueueDrain` detaches transfers and deliberately
             * touches no ring, so the dequeue pointer still sits on the TRBs
             * those transfers left behind - and the Add Context flag's Endpoint
             * Context takes its TR Dequeue Pointer from exactly there. Left
             * alone, the first doorbell after a successful Drop+Add would
             * execute cancelled TRBs whose buffers usbport has already
             * reclaimed. The third review round found this; the comment here
             * previously *asserted* the ring was empty.
             *
             * The enqueue position is the one target `XhciRingSetDequeue`
             * accepts unconditionally, and no Set TR Dequeue Pointer command is
             * owed for it: the Configure Endpoint programs the field itself.
             * Nothing else on the ring is touched, so a refused command leaves
             * an endpoint the xHC still has configured pointing where its stop
             * left it - which is why the record is failed rather than reused if
             * that happens.
             */
            if (XhciRingSetDequeue(&binding->Record->Ring,
                                   XhciRingTrbPA(&binding->Record->Ring,
                                                 binding->Record->Ring.Enqueue))
                != XHCI_RING_OK) {
                ext->EndpointPlacementFailures++;
                xhciEpQuiesceFail(ext, dev, binding, XHCI_DEV_OP_CONFIGURE_EP);
                return;
            }
            /*
             * Task 9-A.1. A reprogram may change the endpoint's *type*, and the
             * ring's kind is part of what a type means here rather than a label
             * on it: it decides which completion codes the classifier accepts,
             * whether an error is read as a halt, and whether a group may be
             * retired. `XhciRingSetDequeue` above rebuilds the position and
             * leaves the kind alone, so this is the one place the two can
             * diverge - an alternate setting that turns an interrupt endpoint
             * into an isochronous one at the same DCI.
             *
             * Taken from `PendingParams`, which is exactly what this branch is
             * committing, and not from usbport's properties: those belong to a
             * callback that returned long ago.
             */
            binding->Record->Ring.Kind =
                xhciEpTypeIsIsoch(binding->Record->PendingParams.EpType)
                    ? XHCI_RING_KIND_ISOCH
                    : XHCI_RING_KIND_ENDPOINT;
            binding->Record->DropOnConfigure = 1;
            binding->Record->State = XHCI_EP_REC_PENDING;
            ext->EndpointReconfigures++;
        }
        xhciEpOweInvalidateAny(ext, dev, binding);
        return;
    }

    if (!mayReposition) {
        /*
         * No Output Endpoint Context to program, so a Set TR Dequeue Pointer
         * would be refused for exactly the reason the stop was. Any debt owed
         * for one goes here, because this is where that is known.
         */
        quiesce->Flags &= ~(XHCI_EPQ_NEED_DEQUEUE | XHCI_EPQ_REPOSITION |
                            XHCI_EPQ_FORCE_DEQUEUE | XHCI_EPQ_UNOWNED_RING);
        xhciEpOweInvalidateAny(ext, dev, binding);
        return;
    }
    if ((quiesce->Flags & XHCI_EPQ_REPOSITION) == 0) {
        /*
         * The ring is exactly as the xHC left it, and a Set TR Dequeue Pointer
         * would destroy the partial progress of the TD it stopped inside - see
         * `XHCI_EPQ_REPOSITION`. The doorbell that ends the pause resumes it
         * where it stopped.
         */
        xhciEpOweInvalidateAny(ext, dev, binding);
        return;
    }
    /* `XHCI_EPQ_REPOSITION` is deliberately still set going in: it is the debt,
     * and only the placement - or the command that carries it - may discharge
     * it. */
    xhciEpPlaceDequeue(ext, dev, binding);
}

/*
 * Ask for this endpoint to be quiesced, and say what happens when it is.
 *
 * `intent` carries `XHCI_EPQ_DRAIN` and/or `XHCI_EPQ_RECONFIGURE`; the command
 * itself is chosen here rather than by the caller, because which one is legal
 * depends on a state the caller has no business reading: a Reset Endpoint "may
 * only be issued to endpoints in the Halted state" (4.6.8 p.118) and a Stop
 * Endpoint answers Context State Error for one that is not Running.
 *
 * Called with the lock held. IRQL: <= DISPATCH_LEVEL.
 */
static VOID xhciEpArmQuiesce(PXHCI_EXTENSION ext,
                             PXHCI_DEVICE dev,
                             PXHCI_EP_BINDING binding,
                             ULONG intent)
{
    PXHCI_EP_QUIESCE quiesce;

    quiesce = binding->Quiesce;
    if ((quiesce->Flags & XHCI_EPQ_FAILED) != 0) {
        /* The chain was already given up on. Arming it again would ask for a
         * command whose predecessor could not be completed, and the endpoint's
         * transfers are already being answered with an error. */
        return;
    }
    quiesce->Flags |= intent;

    if (dev->SlotId == 0) {
        /* No slot was ever granted, so no Endpoint Context ever named this ring
         * and nothing can have executed it. Quiescent by construction rather
         * than by command - and it has to be this branch, because the command
         * would be refused for exactly the same reason. */
        xhciEpStopped(ext, dev, binding, 0);
        return;
    }
    if (!xhciDevAdmitted(ext)) {
        /*
         * No command can be issued, so the debt is unpayable. **Not** treated as
         * quiescence: a controller that is failed rather than halted may still
         * be executing this ring, and claiming otherwise here is how a mapped
         * buffer gets handed back under live DMA.
         */
        quiesce->Flags &= ~(XHCI_EPQ_INFLIGHT | XHCI_EPQ_DRAIN |
                            XHCI_EPQ_RECONFIGURE | XHCI_EPQ_RESTART);
        /* UNAVAILABLE beside it (Phase 7 review, B3): this FAILED says the
         * *controller* could take no command, not that one failed - and a
         * successful restore is entitled to take it back. */
        quiesce->Flags |= XHCI_EPQ_FAILED | XHCI_EPQ_UNAVAILABLE;
        ext->EndpointQuiesceUnavailable++;
        return;
    }

    if ((quiesce->Flags & XHCI_EPQ_HALTED) != 0) {
        quiesce->Flags |= XHCI_EPQ_NEED_RESET;
    } else {
        quiesce->Flags |= XHCI_EPQ_NEED_STOP;
    }
}

/*
 * Arm a quiescence only for an endpoint that needs one.
 *
 * The condition is spec 4.6.4 p.97's own, read as a test rather than
 * paraphrased: an endpoint may be given up without being stopped when it is
 * "Idle in the Running state", which that page glosses as "no USB Transactions
 * are in progress, the Transfer Ring is empty, and software has processed all
 * outstanding events for the Transfer Ring" (the same sentence appears at 4.6.6
 * p.104 for a Drop Context flag). An empty queue *and* a ring whose dequeue has
 * caught up with its enqueue is exactly that: every transfer completed, every
 * TRB reclaimed. Halted is neither Stopped nor Idle-Running, so it is excluded.
 *
 * Without this test every EP0 reopen - which is every enumeration - would pay a
 * Stop Endpoint and a Set TR Dequeue Pointer for a ring with nothing on it, and
 * the Phase 6 path that both targets already pass would be a different path.
 *
 * The reconfigure of task 7a-B.1 deliberately does **not** come through here: it
 * arms unconditionally, so that p.104's precondition is established by a
 * completed Stop Endpoint rather than inferred from this driver's own view of
 * the ring.
 *
 * Called with the lock held. IRQL: <= DISPATCH_LEVEL.
 */
static VOID xhciEpArmIfBusy(PXHCI_EXTENSION ext,
                            PXHCI_DEVICE dev,
                            PXHCI_EP_BINDING binding,
                            ULONG intent)
{
    if (binding->Queue->Count == 0 &&
        binding->Ring->Dequeue == binding->Ring->Enqueue &&
        (binding->Quiesce->Flags & XHCI_EPQ_HALTED) == 0) {
        return;
    }
    xhciEpArmQuiesce(ext, dev, binding, intent);
}

/*
 * One quiescence command has completed. Called with the lock held from
 * XhciSlotCommandEvent. IRQL: DISPATCH_LEVEL.
 */
static VOID xhciEpQuiesceCompleted(PXHCI_EXTENSION ext,
                                   PXHCI_DEVICE dev,
                                   PXHCI_EP_BINDING binding,
                                   ULONG op,
                                   ULONG completionCode)
{
    PXHCI_EP_QUIESCE quiesce;
    ULONG hwState;
    ULONG now;
    ULONG givenUp;

    quiesce = binding->Quiesce;
    quiesce->Flags &= ~XHCI_EPQ_BUSY;

    /*
     * **Task 11-V.9's third tier: the recovery, beside the halt that asked for
     * it.** `ep.halted` says an endpoint stopped; this says what was done about
     * it and whether the controller accepted it - a Stop Endpoint, a Reset
     * Endpoint or a Set TR Dequeue Pointer, with its completion code.
     *
     * **The gate is what puts this in the change-gated tier rather than in the
     * never-per-event one, and it is not "a command completed".** This function
     * is also the ordinary end of usbport's *cancellation* path: a
     * `SetEndpointState(PAUSED)` on a healthy endpoint issues a Stop Endpoint
     * and completes here with Success, and a bus doing in-flight cancels
     * produces those continuously - stage F measured 90,447 transfers with
     * cancellation in the mix. A record apiece would be a per-event producer
     * wearing a recovery's name, and it would spend the ring on the case where
     * nothing went wrong.
     *
     * So a record is made only where there is something to recover from: a
     * completion code that is not Success, or an endpoint the driver has marked
     * halted. Both are bounded by the quiesce state machine's own `INFLIGHT`
     * flag, which issues each command of a chain once. A chain that will not
     * converge shows up as repeated records with the same code - a reading
     * rather than a flood, and precisely the fault this tier exists to catch.
     */
    if (completionCode != XHCI_CC_SUCCESS ||
        (quiesce->Flags & XHCI_EPQ_HALTED) != 0) {
        XhciLogNoteLocked(ext, "ep.recovery",
                          ((ULONG)binding->Dci << 16) | (op << 8) |
                              completionCode);
    }

    switch (op) {
    case XHCI_DEV_OP_STOP_EP:
        if (completionCode == XHCI_CC_SUCCESS) {
            /* Success is defined only for an endpoint that was Running and has
             * been moved to Stopped (4.6.9 p.125), so this is the one code that
             * says both "the ring is software's" and "a Set TR Dequeue Pointer
             * will be accepted". A HALTED bit surviving here was stale. */
            ext->EndpointStops++;
            quiesce->Flags &= ~XHCI_EPQ_HALTED;
            xhciEpStopped(ext, dev, binding, 1);
            break;
        }
        if (completionCode != XHCI_CC_CONTEXT_STATE_ERROR) {
            ext->EndpointStopFailures++;
            xhciEpQuiesceFail(ext, dev, binding, op);
            break;
        }
        /*
         * "The Endpoint State (EP State) field is not Running" (4.6.9 p.126) -
         * which is Disabled, Halted, Stopped or Error, and the four need three
         * different next steps. The spec says to read it rather than guess:
         * "Software may verify that this case occurred by inspecting the EP
         * State for Halted or Error when a Stop Endpoint Command results in a
         * Context State Error" (p.123).
         */
        hwState = XHCI_EP_STATE_RUNNING;
        if (!xhciEpHardwareState(ext, dev, binding->Dci, &hwState)) {
            ext->EndpointStopFailures++;
            xhciEpQuiesceFail(ext, dev, binding, op);
            break;
        }
        ext->EndpointStops++;
        if (hwState == XHCI_EP_STATE_HALTED) {
            /* The race the note above names: the endpoint halted while the stop
             * was in flight. A Set TR Dequeue Pointer would be refused from
             * Halted (4.6.10 p.126 allows Error and Stopped only), so the reset
             * has to come first. */
            xhciEpMarkHalted(ext, quiesce, binding->Dci);
            quiesce->Flags |= XHCI_EPQ_NEED_RESET;
            break;
        }
        if (hwState == XHCI_EP_STATE_ERROR) {
            /*
             * **Error is not Stopped, and only one command leaves it**: "A TRB
             * Error condition should cause a Running Endpoint to transition to
             * the Error state. A Set TR Dequeue Pointer Command shall be used to
             * transition the endpoint to the Stopped state" (4.8.3 p.149). So
             * the placement is owed whatever the ring looks like - without it
             * the endpoint stays in Error and no doorbell restarts it.
             */
            /*
             * **Nothing on this ring is touched, and nothing queued on it is
             * answered.** An endpoint in Error has not been through a Stop
             * Endpoint, which is what "transfer[s] ownership of all the TDs on
             * the associated Transfer Ring to software" (4.11.4.8), and only
             * one command takes it out: "A Set TR Dequeue Pointer Command shall
             * be used to transition the endpoint to the Stopped state" (4.8.3
             * p.149).
             *
             * So this branch deliberately does **not** call `xhciEpStopped`. A
             * drain there would complete transfers - handing their mapped
             * buffers back - while the xHC may still own or have prefetched
             * their TRBs, which is the hazard the whole batch is about; the
             * seventh review round found the first draft doing exactly that.
             * The command is armed against the position the ring already has,
             * which changes nothing and is the point: it is the state
             * transition that is wanted, not a new position. Everything else -
             * the drain, the reconfigure, the No Op rewrite and the real
             * placement - happens on its completion, with ownership.
             */
            quiesce->Flags &= ~XHCI_EPQ_HALTED;
            xhciEpOweReposition(quiesce, 1, 1);
            quiesce->DequeuePA = XhciRingDequeuePA(binding->Ring) |
                                 XhciRingDequeueCycle(binding->Ring);
            quiesce->Flags |= XHCI_EPQ_NEED_DEQUEUE;
            break;
        }
        if (hwState == XHCI_EP_STATE_STOPPED) {
            quiesce->Flags &= ~XHCI_EPQ_HALTED;
            xhciEpStopped(ext, dev, binding, 1);
            break;
        }
        if (hwState == XHCI_EP_STATE_DISABLED) {
            /* There is no Output Endpoint Context, so nothing has ever executed
             * this ring - and nothing would accept a position for it either. */
            quiesce->Flags &= ~XHCI_EPQ_HALTED;
            /*
             * **And nothing may ring it.** `xhciEpStopped` is about to set
             * `XHCI_EPQ_STOPPED`, which is true of a Disabled endpoint - it is
             * not executing - and which is the bit every restart path reads.
             * Without this the restart honoured at the end of this function,
             * the one `SetEndpointState(ACTIVE)` asks for, and the health
             * poll's net would each write a doorbell 4.8.3 p.150 forbids.
             */
            quiesce->Flags |= XHCI_EPQ_NO_CONTEXT;
            /*
             * Read **before** `xhciEpStopped`, which consumes it: an endpoint
             * usbport is giving up must not have its context asked back. Doing
             * that re-establishes an endpoint nobody wants and re-books its
             * periodic bandwidth - and the REMOVE vector caught exactly that
             * when the restoration below was first written unconditionally.
             */
            givenUp = (quiesce->Flags & XHCI_EPQ_DRAIN) != 0;
            xhciEpStopped(ext, dev, binding, 0);
            if (!givenUp) {
                xhciEpOweContextRestore(ext, binding);
            }
            break;
        }
        /* Running, which contradicts the completion code the xHC just gave. */
        ext->EndpointStopFailures++;
        XHCI_DBG_VALUE_CHANGED("slot: Stop Endpoint said Context State Error "
                               "with EP State Running, dci", binding->Dci);
        xhciEpQuiesceFail(ext, dev, binding, op);
        break;

    case XHCI_DEV_OP_RESET_EP:
        if (completionCode == XHCI_CC_SUCCESS) {
            /* "Set the Endpoint Context EP State field to Stopped" (4.6.8
             * p.117). The Set TR Dequeue Pointer that follows is not optional
             * for a control endpoint: "After a Reset Endpoint Command is
             * executed for a control endpoint, software shall execute a Set TR
             * Dequeue Pointer Command to ensure that the endpoint's Dequeue
             * Pointer references a Setup TD" (p.118). */
            ext->EndpointResets++;
            quiesce->Flags &= ~XHCI_EPQ_HALTED;
            xhciEpStopped(ext, dev, binding, 1);
            break;
        }
        if (completionCode == XHCI_CC_CONTEXT_STATE_ERROR) {
            /* The endpoint was not Halted (4.6.8 p.117), so the HALTED bit this
             * driver was carrying is stale. Convert to a Stop, which is the
             * command that reaches Stopped from Running - and which answers
             * Context State Error harmlessly from anything else. */
            ext->EndpointResetsNotHalted++;
            quiesce->Flags &= ~XHCI_EPQ_HALTED;
            quiesce->Flags |= XHCI_EPQ_NEED_STOP;
            break;
        }
        ext->EndpointResetFailures++;
        xhciEpQuiesceFail(ext, dev, binding, op);
        break;

    case XHCI_DEV_OP_SET_DEQUEUE:
        if (completionCode != XHCI_CC_SUCCESS) {
            ext->EndpointDequeueFailures++;
            xhciEpQuiesceFail(ext, dev, binding, op);
            break;
        }
        ext->EndpointDequeueSets++;
        now = XhciRingDequeuePA(binding->Ring) |
              XhciRingDequeueCycle(binding->Ring);
        if (now != quiesce->DequeuePA) {
            /*
             * The software dequeue moved while a command naming the old one was
             * in flight, so the two pointers have diverged. Reissue against what
             * the ring says now rather than believe a command about a position
             * nothing is at.
             */
            ext->EndpointDequeueRearms++;
            quiesce->DequeuePA = now;
            quiesce->Flags |= XHCI_EPQ_NEED_DEQUEUE;
            break;
        }
        /*
         * **Here, and only here, is the position debt discharged** - and only
         * the debt this command was built for. `XHCI_EPQ_DEQUEUE_ISSUED` is what
         * says no newer one was raised in the meantime: an abort removing
         * another transfer while this command was in flight sets the debt again,
         * and the divergence check above cannot see it because taking a transfer
         * off the queue moves no ring pointer.
         */
        if ((quiesce->Flags & XHCI_EPQ_DEQUEUE_ISSUED) != 0) {
            quiesce->Flags &= ~(XHCI_EPQ_REPOSITION | XHCI_EPQ_FORCE_DEQUEUE |
                                XHCI_EPQ_DEQUEUE_ISSUED);
            if ((quiesce->Flags & XHCI_EPQ_UNOWNED_RING) != 0) {
                /*
                 * The endpoint was in Error and this command is what took it to
                 * Stopped (4.8.3 p.149). **Everything the Error branch refused
                 * to do happens now**, with the ownership it lacked: the drain
                 * that answers queued transfers, the reconfigure, the No Op
                 * rewrite and the real placement are all `xhciEpStopped`'s, and
                 * this is the first moment any of them is safe.
                 */
                quiesce->Flags &= ~XHCI_EPQ_UNOWNED_RING;
                xhciEpOweReposition(quiesce, 0, 0);
                xhciEpStopped(ext, dev, binding, 1);
                break;
            }
        }
        xhciEpOweInvalidateAny(ext, dev, binding);
        break;

    default:
        break;
    }

    /*
     * "The next time the doorbell is rung, the endpoint shall start execution at
     * the beginning of the TRB referenced by the TR Dequeue Pointer" (4.6.9
     * p.119) - and this is the one place every path that can end a chain reaches,
     * which is why the restart is honoured here rather than inside the Set TR
     * Dequeue Pointer branch it used to live in.
     */
    xhciEpHonourRestart(ext, dev, binding);
}

/*
 * The ring refused to retire a TD because it and the transfer record disagree
 * about where that TD ends - `XHCI_XFER_EVENT_RESULT.RefusedRetire`.
 *
 * **Two callers, one policy, and that is the point.** The divergence can be
 * found at the event (`XhciXferEvent`) or at the settle
 * (`XhciXferDrainSettled`), and the batch-9-0 review found the same defect at
 * each of them in turn, because only one route had been fixed: routing it
 * through `xhciEpRecoveryNeeded` with a Short Packet code falls past that
 * function's one non-halt branch and marks a **healthy** endpoint Halted, which
 * on a non-default endpoint arms no command at all and then answers every later
 * submission with `STALL_PID`.
 *
 * What the endpoint actually needs is a **Stop Endpoint**. Nothing halted it,
 * so it is Running - and a Set TR Dequeue Pointer "may be executed only if the
 * target endpoint is in the Error or Stopped state" (4.6.10 p.126), while its
 * queued transfers may not be answered at all until a stop has transferred
 * ownership of the ring's TDs to software (4.11.4.8, 4.6.4 p.97). So the debt
 * is raised and the ordinary quiescence is armed with `XHCI_EPQ_DRAIN`, which
 * is what answers the queue afterwards.
 *
 * Called with the lock held. IRQL: DISPATCH_LEVEL.
 */
static VOID xhciEpRefusedRetireRecovery(PXHCI_EXTENSION ext,
                                        PXHCI_DEVICE dev,
                                        PXHCI_EP_BINDING binding)
{
    PXHCI_EP_QUIESCE quiesce;

    quiesce = binding->Quiesce;
    /*
     * Already asked. A second divergence on the same endpoint before the stop
     * completes needs nothing new, and counting it again would overstate how
     * often this defensive path was entered.
     */
    if ((quiesce->Flags & XHCI_EPQ_DRAIN) != 0) {
        return;
    }
    if ((quiesce->Flags & XHCI_EPQ_FAILED) != 0) {
        /*
         * **This endpoint's chain was already given up on**, so
         * `xhciEpArmQuiesce` would return without arming anything and counting
         * a reposition would claim a request that was never made.
         *
         * The transfer stays queued, and that is `xhciEpQuiesceFail`'s standing
         * policy rather than a gap here: on a failed chain nothing has shown
         * the xHC stopped reading the mapped buffer, so queued transfers wait
         * for a path that does prove it - `XhciSlotInit` after an HCRST, or a
         * Disable Slot. Answering it early is the use-after-unmap this whole
         * mechanism exists to prevent, and `UsbPortInvalidateController(RESET)`
         * is not an HCRST on either shipping stack, so escalating there would
         * not end the wait either.
         */
        ext->MidTdRefusedRetiresUnarmable++;
        return;
    }
    ext->MidTdRefusedRetires++;
    xhciEpOweReposition(quiesce, 1, 0);
    xhciEpArmQuiesce(ext, dev, binding, XHCI_EPQ_DRAIN);
}

/*
 * A TRB Error on an **isochronous** endpoint (batch 9-A review round 2).
 *
 * Same mechanism as the refused retire above, and deliberately not
 * `xhciEpRecoveryNeeded`'s TRB Error branch - see the call site for why that
 * branch's precondition does not hold here. Its own counter because the two say
 * different things on a target: a refused retire is a record/ring divergence,
 * and this is the controller rejecting a TRB this driver built, which is a
 * host-side encoding fault and the one isochronous error software must act on.
 *
 * The whole queue is given up rather than the failed packet alone. A TRB Error
 * says the xHC would not accept a TRB this builder produced, so the rest of the
 * group was built by the same code from the same parameters and is not worth
 * more confidence than the packet that failed; and advancing past one TD would
 * need a position the ring's dequeue does not hold. usbport reposts.
 *
 * Called with the lock held. IRQL: DISPATCH_LEVEL.
 */
static VOID xhciEpIsoErrorRecovery(PXHCI_EXTENSION ext,
                                   PXHCI_DEVICE dev,
                                   PXHCI_EP_BINDING binding)
{
    PXHCI_EP_QUIESCE quiesce;

    quiesce = binding->Quiesce;
    if ((quiesce->Flags & XHCI_EPQ_DRAIN) != 0) {
        return;
    }
    if ((quiesce->Flags & XHCI_EPQ_FAILED) != 0) {
        /* `xhciEpArmQuiesce` would return without arming, so counting a request
         * here would claim one that was never made - the same split batch 9-0
         * had to make for the refused retire. */
        ext->IsoTrbErrorsUnarmable++;
        return;
    }
    ext->IsoTrbErrorRecoveries++;
    xhciEpOweReposition(quiesce, 1, 0);
    xhciEpArmQuiesce(ext, dev, binding, XHCI_EPQ_DRAIN);
}

/* Defined with the rest of the device lifecycle further down, and declared here
 * because a slot-fatal completion code is answered by the same teardown an
 * unplug takes rather than by a second route to the Disable Slot. */
static VOID xhciDevTeardown(PXHCI_EXTENSION ext, PXHCI_DEVICE dev);

/*
 * Some completion codes are fatal to the **slot** rather than to the endpoint
 * that reported them or to the controller, and Table 6-90 names the recovery:
 * Incompatible Device Error is "fatal as far as the Slot is concerned. Software
 * shall issue a Disable Slot Command to recover" (p.468).
 *
 * **Audit round 8 found nothing performing that recovery.** The code was filed
 * with the ordinary INTERNAL_HC_ERROR set, so a Transfer Event carrying it
 * reached `xhciEpRecoveryNeeded` and got a halt's answer - Reset Endpoint, then
 * a Set TR Dequeue Pointer - after which the slot is still enabled, the device
 * still addressed, and the controller still unable to access it. The next
 * submission takes the same path, and the loop has no exit that is not an
 * unplug.
 *
 * The Disable Slot is *owed*, not issued here: `xhciDevTeardown` is the route
 * every other "this device is finished" path already takes, and it puts the
 * endpoint stops ahead of the slot command, which is the ordering 4.6.4 p.97
 * requires ("Before a Disable Slot Command is issued ... any active endpoints of
 * the device slot shall be in the Stopped state"). Writing a second route to the
 * same command is how two teardown orders drift apart.
 *
 * Called **after** the event has been accounted, from both transfer-event paths,
 * so that the transfer that carried the code is retired and completed by the
 * ordinary machinery before the record stops accepting work. The endpoint
 * recovery armed on the way past is not unwound: the teardown supersedes it, and
 * "recovery armed on a record that then went away" is the ordinary unplug shape
 * this machinery already handles.
 *
 * Called with the lock held. IRQL: DISPATCH_LEVEL.
 */
static VOID xhciDevSlotFatalEvent(PXHCI_EXTENSION ext,
                                  PXHCI_DEVICE dev,
                                  ULONG completionCode)
{
    XHCI_XFER_CODE info;

    if (dev == NULL) {
        return;
    }
    if (XhciXferCodeInfo(completionCode, &info) != XHCI_XFER_OK ||
        info.SlotFatal == 0) {
        return;
    }
    /*
     * A record already unwinding has its Disable Slot owed, and a device that
     * produces one of these per microframe must not re-arm the teardown on every
     * event - `xhciDevTeardown` would re-run its cancellation over a record whose
     * state says the answers have already been given.
     */
    if (dev->State == XHCI_DEV_STATE_FREE ||
        dev->State == XHCI_DEV_STATE_GONE) {
        return;
    }
    ext->IncompatibleDeviceTeardowns++;
    XHCI_DBG_VALUE_CHANGED("slot: incompatible device, disabling the slot",
                           dev->SlotId);
    xhciDevTeardown(ext, dev);
}

/*
 * A Transfer Event asked for recovery. Called with the lock held from
 * XhciSlotTransferEvent. IRQL: DISPATCH_LEVEL.
 */
static VOID xhciEpRecoveryNeeded(PXHCI_EXTENSION ext,
                                 PXHCI_DEVICE dev,
                                 PXHCI_EP_BINDING binding,
                                 ULONG completionCode)
{
    PXHCI_EP_QUIESCE quiesce;

    quiesce = binding->Quiesce;

    /*
     * The Stopped family (26-28) cannot reach here: `XhciSlotTransferEvent`
     * takes those events out before the queue sees them, because a forced
     * Stopped Transfer Event is a statement about the ring rather than about a
     * transfer.
     *
     * **TRB Error does not halt the endpoint - it puts it in Error**, and the
     * two need different commands. "All Transfer Ring error conditions force the
     * state of the associated endpoint to Halted" (p.176) is the general
     * sentence; 4.8.3 p.149 states the specific exception: "A TRB Error
     * condition should cause a Running Endpoint to transition to the Error
     * state. A Set TR Dequeue Pointer Command shall be used to transition the
     * endpoint to the Stopped state." Treating it as a halt was worse than
     * cosmetic for a non-default endpoint: this driver would have waited for a
     * device-side reset-pipe that nothing is obliged to send for a host-side
     * error, answering every later submission with `STALL_PID` in the meantime.
     *
     * So it takes the same route as the Error state read out of a Context State
     * Error: only the position is programmed, and nothing on the ring is touched
     * or answered until that command has taken it to Stopped.
     */
    if (completionCode == XHCI_CC_TRB_ERROR) {
        if ((quiesce->Flags & (XHCI_EPQ_INFLIGHT | XHCI_EPQ_FAILED)) == 0) {
            xhciEpOweReposition(quiesce, 1, 1);
            quiesce->DequeuePA = XhciRingDequeuePA(binding->Ring) |
                                 XhciRingDequeueCycle(binding->Ring);
            quiesce->Flags |= XHCI_EPQ_NEED_DEQUEUE;
        }
        XHCI_DBG_VALUE_CHANGED("slot: endpoint in Error, dci", binding->Dci);
        xhciEpOweInvalidateAny(ext, dev, binding);
        return;
    }

    xhciEpMarkHalted(ext, quiesce, binding->Dci);
    /*
     * A halt always reprograms the position: the TD the endpoint stopped on is
     * abandoned rather than resumed ("software shall use a Set TR Dequeue
     * Pointer Command to advance the Transfer Ring to the next TD", 4.10.2
     * p.172), and for a control endpoint the command is mandatory outright
     * ("After a Reset Endpoint Command is executed for a control endpoint,
     * software shall execute a Set TR Dequeue Pointer Command to ensure that the
     * endpoint's Dequeue Pointer references a Setup TD", 4.6.8 p.118). The event
     * path has already placed the software dequeue on the next TD head.
     */
    xhciEpOweReposition(quiesce, 1, 0);
    XHCI_DBG_VALUE_CHANGED("slot: endpoint halted, dci << 8 | completion code",
                           (binding->Dci << 8) | completionCode);

    if (binding->Record == NULL) {
        /*
         * **EP0 recovers itself; every other endpoint waits to be told.**
         *
         * The difference is not symmetry, it is spec 4.6.8 p.116's reset-a-pipe
         * sequence: "If not a Control endpoint: Issue a ClearFeature
         * (ENDPOINT_HALT) request to device", and "Undefined behavior may occur
         * if this command is executed with TSP = '0' and the associated device
         * endpoint is not successfully reset by system software". That device-
         * side clear is the client's reset-pipe URB, which reaches this driver
         * as `SetEndpointStatus(RUN)` - so resetting a non-default endpoint
         * ahead of it would desynchronise the Data Toggle.
         *
         * A control endpoint has no ENDPOINT_HALT to clear (the same sentence
         * excludes it) and the device clears its own stall at the next SETUP, so
         * nothing else can ever restart EP0 - leaving it halted would strand the
         * device with no way to be asked anything, the CLEAR_FEATURE included.
         */
        if ((quiesce->Flags & (XHCI_EPQ_INFLIGHT | XHCI_EPQ_FAILED)) == 0) {
            quiesce->Flags |= XHCI_EPQ_NEED_RESET;
        }
    }
    xhciEpOweInvalidateAny(ext, dev, binding);
}

/*
 * Arm a stop and a drain on every endpoint this device has - the teardown's half
 * of 4.6.4 p.97.
 *
 * EP0 is included and is usually the one that matters least; the endpoint that
 * makes this necessary is the interrupt pipe a HID device keeps a read posted
 * on. Endpoints with no Output Endpoint Context resolve through the same path
 * and cost one command that answers Context State Error, which is deliberate:
 * deciding here which of them the xHC has a context for would be this driver's
 * bookkeeping standing in for the controller's answer.
 *
 * Called with the lock held. IRQL: <= DISPATCH_LEVEL.
 */
static VOID xhciDevArmTeardownStops(PXHCI_EXTENSION ext, PXHCI_DEVICE dev)
{
    XHCI_EP_BINDING binding;
    ULONG i;

    if (xhciEpResolve(dev, 1, &binding)) {
        xhciEpArmIfBusy(ext, dev, &binding, XHCI_EPQ_DRAIN);
    }
    for (i = 0; i < XHCI_MAX_DEVICE_ENDPOINTS; i++) {
        if (dev->Endpoints[i].Dci == 0) {
            continue;
        }
        if (xhciEpResolve(dev, dev->Endpoints[i].Dci, &binding)) {
            xhciEpArmIfBusy(ext, dev, &binding, XHCI_EPQ_DRAIN);
        }
    }
}

/* Has any endpoint of this device been given up on without being shown stopped?
 * The one reading that says 4.6.4 p.97's precondition is unmet when the Disable
 * Slot goes out. Called with the lock held. */
static ULONG xhciDevHasFailedQuiesce(PXHCI_DEVICE dev)
{
    ULONG i;

    if ((dev->Ep0Quiesce.Flags & XHCI_EPQ_FAILED) != 0) {
        return 1;
    }
    for (i = 0; i < XHCI_MAX_DEVICE_ENDPOINTS; i++) {
        if (dev->Endpoints[i].Dci != 0 &&
            (dev->Endpoints[i].Quiesce.Flags & XHCI_EPQ_FAILED) != 0) {
            return 1;
        }
    }
    return 0;
}

/*
 * Detach this record's topology node, whatever address the record is at
 * (Phase 7 review, finding A4). `DeviceAddress` alone was the key, and it is 0
 * for the whole of a re-enumeration - the address-0 open clears it while the
 * graph deliberately keeps the node - which is exactly where the recovering
 * hub spends its time (task 7b-A.0's measured cycle). A teardown landing then
 * detached address 0, a no-op, and the node leaked for the life of the driver;
 * eight leaks fill the table and end behind-hub support until StopController.
 * `TopoAddress` bridges the window.
 * Called with the lock held. IRQL: <= DISPATCH_LEVEL.
 */
static VOID xhciDevTopoDetach(PXHCI_EXTENSION ext, PXHCI_DEVICE dev)
{
    if (dev->TopoAddress != 0 && dev->TopoAddress != dev->DeviceAddress) {
        XhciTopoDetach(&ext->Topology, dev->TopoAddress);
    }
    XhciTopoDetach(&ext->Topology, dev->DeviceAddress);
    dev->TopoAddress = 0;
}

/*
 * Put a record back in the pool.
 *
 * `slotReleased` says whether the xHC has been shown to have let this device's
 * slot go - a completed Disable Slot with a code that proves it, a halted
 * controller, or a record that never had a Slot ID at all. It decides whether
 * this is a release at all, and the two answers are not interchangeable:
 *
 *   released - the slot is gone, so its Endpoint Contexts are gone with it: the
 *              queued transfers can be answered, the pooled rings can go back to
 *              the free list, and the record can be reused.
 *   not      - **nothing is given up.** The record stays GONE with its Slot ID,
 *              its DCBAA entry, its rings and its queued transfers exactly as
 *              the hardware may still be reading them, and is counted in
 *              `DevicesAbandoned`. That is what `XhciSlotInvalidateAll` already
 *              does when it cannot prove the controller let go.
 *
 * An earlier draft released the record here and marked the *rings* with a
 * sentinel owner instead, so that they were withheld from the free list while
 * the record's index was reused. That protected the ring and handed the
 * transfers' mapped pages back in the same breath, which is the larger half of
 * the same hazard - so the sentinel is gone and the whole record is withheld.
 *
 * **This is not a complete answer and must not be read as one.** usbport
 * reclaims a deleted device's transfers on its own, so withholding a completion
 * here cannot keep a mapping alive indefinitely; what closes the hazard is Stop
 * Endpoint, which is task 7a-B.1's. See the batch 7a-B note in
 * `docs/contributing/roadmap.md` for the four hazards that batch owns.
 *
 * Called with the lock held.
 */
/* Give a record up, and `slotReleased` is the whole contract: 1 means the
 * controller has been *shown* to have let the slot go, 0 means it has not and
 * the record is abandoned in place instead. See the two branches.
 * Called with the lock held. IRQL: <= DISPATCH_LEVEL. */
static VOID xhciDevRelease(PXHCI_EXTENSION ext,
                           PXHCI_DEVICE dev,
                           ULONG slotReleased)
{
    ULONG ref;
    ULONG i;

    if (!slotReleased) {
        /*
         * **Nothing is released and nothing is answered**, and the eleventh
         * review is why this branch exists at all.
         *
         * A previous round drained the queues here unconditionally, on the
         * reasoning that the record is about to be zeroed so a transfer left on
         * it would be lost. That is true, and it made this the very fault the
         * drain was moved out of `xhciDevTeardown` to avoid:
         * `UsbPortCompleteTransfer` hands the mapped pages back, and on *this*
         * path nothing has shown the slot is gone - so the endpoint may still
         * execute the TRBs that name them. Quarantining the rings stops another
         * endpoint being handed the ring; it does nothing for the pages.
         *
         * So the record is **abandoned** rather than released, which is exactly
         * what `XhciSlotInvalidateAll` does when it cannot prove the controller
         * let go: the Slot ID stays taken, the DCBAA entry stays as the xHC
         * reads it, the rings stay owned by a record that still exists, and the
         * transfers stay queued. They are answered by whichever path *does*
         * prove it - the reinitialisation's HCRST reaches `XhciSlotInit`, which
         * drains them safely because 4.23.1 p.312 puts every register back at
         * its default and there is nothing left to execute them.
         *
         * That costs a device record until the next start. It is the same
         * bounded leak the Slot ID this path has already given up on represents,
         * and it is strictly better than either alternative: losing the
         * transfers hangs a thread, and answering them is a DMA target handed
         * back while the controller can still write into it.
         */
        dev->State = XHCI_DEV_STATE_GONE;
        dev->PendingOp = XHCI_DEV_OP_NONE;
        dev->ActiveOp = XHCI_DEV_OP_NONE;
        /*
         * **The topology node goes even though nothing else does** (task
         * 7b-A.1), and the asymmetry is the point: everything else withheld
         * here is withheld because the *controller* may still be reading it -
         * the Slot ID, the DCBAA entry, the rings, the queued transfers. A
         * graph node is none of those. It is software bookkeeping keyed on a
         * usbport device address, and that address stops naming this device
         * the moment the record is given up on; usbport is free to hand it to
         * the next device, which would then find this hub's position waiting
         * for it under its own address.
         */
        xhciDevTopoDetach(ext, dev);
        ext->DevicesAbandoned++;
        return;
    }

    /*
     * The slot is provably gone, so both halves are safe together: the TRBs
     * cannot be executed, so the buffers cannot be written, so the transfers can
     * be answered - and the rings cannot be pointed at, so they can be reused.
     */
    xhciDevCancelWork(ext, dev, XHCI_USBD_STATUS_CANCELED);

    /* Any invalidation this record still owes goes with it, count included -
     * the endpoints it named are about to stop existing, so there is nothing to
     * re-offer and nothing that would ever clear the debt otherwise. */
    xhciDevDropInvalidate(ext, &dev->Flags, XHCI_DEV_FLAG_INVALIDATE_EP0);
    for (i = 0; i < XHCI_MAX_DEVICE_ENDPOINTS; i++) {
        xhciDevDropInvalidate(ext, &dev->Endpoints[i].InvalidateOwed, 1UL);
    }

    ref = xhciDevRef(ext, dev);
    (VOID)XhciPoolReleaseDevice(&ext->RingPool, ref);

    /* Task 7b-A.1: the record is going, so nothing may still be described as
     * sitting on it. A disowned record has already done this with the address
     * it had then; a record released without a disown - a failed enumeration,
     * a teardown - reaches it only here. */
    xhciDevTopoDetach(ext, dev);

    /* Last thing before the record stops existing. */
    xhciDevFoldQueues(ext, dev);
    for (i = 0; i < sizeof(XHCI_DEVICE) / sizeof(ULONG); i++) {
        ((ULONG *)dev)[i] = 0;
    }
    ext->DevicesTornDown++;
}

/*
 * A Disable Slot has completed. **One function rather than the three lines
 * written at each of the two sites that reach it**, and the duplication was
 * real rather than hypothetical: a mutation that deleted the DCBAA clear from
 * one of them failed zero checks, because the vector that covers it goes
 * through the other. A record that reaches a Disable Slot from the unwind path
 * and one that reaches it from a failed slot preparation owe the controller
 * exactly the same thing.
 *
 * Called with the lock held.
 */
static VOID xhciDevDisableCompleted(PXHCI_EXTENSION ext, PXHCI_DEVICE dev)
{
    ULONG released;

    /*
     * **Whether the pooled rings may go back depends on the completion code**,
     * and the tenth review found this path treating every Disable Slot as proof.
     * It is proof for two codes and not for the rest:
     *
     *   Success - the slot is disabled and its Endpoint Contexts are gone.
     *   Slot Not Enabled - it was already disabled, which is the same
     *     postcondition arrived at differently.
     *
     * Anything else says the command did not do what its name says. Table 6-90
     * makes Incompatible Device Error returnable by *any* command and names
     * Disable Slot as the recovery, so a code arriving here can be a Disable
     * Slot that has not disabled anything.
     *
     * **The DCBAA entry is part of the same decision**, which is why the clear
     * moved below this test: a null entry under a slot the controller still has
     * enabled is a pointer it may follow to zero, and that is the reason the
     * clear was ordered after the Disable Slot in the first place. Clearing it
     * on a completion that proved nothing would defeat that ordering rather than
     * observe it. `xhciDevRelease` then abandons rather than releases, so the
     * entry, the Slot ID, the rings and the queued transfers all stay exactly as
     * the hardware may still be reading them.
     */
    released = (dev->LastCompletionCode == XHCI_CC_SUCCESS ||
                dev->LastCompletionCode == XHCI_CC_SLOT_NOT_ENABLED) ? 1UL : 0UL;
#ifdef XHCI_FIX_QUIESCE_GATE
    /*
     * **EXPERIMENTAL, bench candidate W1 for Finding 3** (HANDOFF.md open item
     * 1; `run-13e.md` findings K to M). Not built into any shipping flavour -
     * this whole block exists only under the define.
     *
     * The completion code above says the *Disable Slot* did what its name says.
     * It says nothing about whether this slot's endpoints were ever shown
     * stopped, and 4.6.4 p.97 makes that a precondition of the command rather
     * than a consequence of it. `xhciDevTeardown` issues the Disable Slot even
     * when the stop could not be shown to have worked - deliberately, because
     * holding the slot for the life of the driver is the worse of two bad
     * outcomes - and counts `TeardownsWithoutStop` when it does.
     *
     * So a Success here can arrive over an endpoint that was never stopped, and
     * the release below then answers the queued transfers (usbport unmaps the
     * pages) and returns the transfer rings to a **first-fit** pool that hands
     * the same index straight to the next device. That is the hypothesised
     * mechanism, and this gate is its fix: a slot whose endpoints were not shown
     * stopped is **abandoned** rather than released - the same containment
     * `xhciDevRelease`'s other branch already applies, and a bounded leak
     * instead of shared DMA.
     *
     * **It is a conditional no-op**, and that is the point of pairing it with
     * the ring-reuse candidate: if the stop never actually fails on this
     * silicon, `xhciDevHasFailedQuiesce` is false and this binary behaves
     * exactly like the control. A "still wedges" result from W1 alone therefore
     * does not distinguish "the fix is insufficient" from "link 2 of the
     * hypothesis is false".
     */
    if (released != 0 && xhciDevHasFailedQuiesce(dev) != 0) {
        released = 0;
    }
#endif
    /*
     * **`SlotsDisabled` and `slot.disabled` are behind that decision, and audit
     * round 10 found them in front of it.** Both were emitted on every Disable
     * Slot completion, so a command that proved nothing - the case the whole
     * paragraph above exists for - reported a slot given back while the DCBAA
     * entry, the rings and the Slot ID all stayed exactly where they were. A
     * release build then read "slots enabled" and "slots disabled" as a balanced
     * pair while the controller still held the slot, which hides precisely the
     * bounded capacity leak those counters exist to make visible.
     *
     * The refused case is not uncounted: `xhciDevRelease` records it as
     * `DevicesAbandoned`, which is the counter that already means "nothing was
     * given up". Two causes with opposite diagnoses may not share one reading,
     * and "the slot went" and "the slot did not go" are as opposite as it gets.
     */
    if (released) {
        ext->SlotsDisabled++;
        /*
         * Task 11-V.9's second tier: the device's last record. It closes the
         * story `slot.enabled` opened, and the absence of one for a Slot ID that
         * was enabled is itself the reading - a slot the driver never gave back.
         */
        XhciLogNoteLocked(ext, "slot.disabled", (ULONG)dev->SlotId);
        xhciDevClearDcbaa(ext, dev);
    } else {
        /* Its own record rather than silence, for the same reason: a reader
         * following `slot.enabled` needs to see *why* no `slot.disabled`
         * followed it, and the completion code is that why. */
        XhciLogNoteLocked(ext, "slot.disable.refused",
                          ((ULONG)dev->SlotId << 8) | dev->LastCompletionCode);
        XHCI_DBG_VALUE_CHANGED("slot: Disable Slot did not prove the slot went, "
                               "completion code", dev->LastCompletionCode);
    }
    xhciDevRelease(ext, dev, released);
}

/*
 * The device on this record has gone. Complete what it owed, stop accepting
 * work, and arrange for its slot to be given back.
 *
 * **A command already outstanding is not cancelled**, because it cannot be: the
 * engine has one command in flight and its completion still names this record.
 * The record therefore stays until that completion arrives, at which point the
 * GONE state routes it into the Disable Slot rather than into the next step of
 * an enumeration nobody is waiting for.
 *
 * Called with the lock held. IRQL: <= DISPATCH_LEVEL.
 */
static VOID xhciDevTeardown(PXHCI_EXTENSION ext, PXHCI_DEVICE dev)
{
    if (dev->State == XHCI_DEV_STATE_FREE) {
        return;
    }

    /*
     * **The intercepted SET_ADDRESS is answered now; the queued transfers are
     * not**, and the tenth review is why the two are split.
     *
     * SET_ADDRESS owns no TRBs - it never reached a ring - so answering it costs
     * nothing and frees the enumeration thread immediately.
     *
     * The queued transfers do own TRBs, on rings the xHC still reaches through a
     * DCBAA entry this path deliberately leaves alone. `UsbPortCompleteTransfer`
     * is not just an answer: it hands the transfer's *mapped buffer* back, and
     * usbport unmaps the scatter/gather list and returns the pages. Completing
     * them here would do that while the slot is still enabled and the endpoint
     * still Running - and spec 4.6.4 p.97 makes the same point from the other
     * side, requiring that before a Disable Slot "any active endpoints ... shall
     * be in the Stopped state or Idle in the Running state, and any outstanding
     * Transfer Events shall have been received".
     *
     * Phase 6 could not reach this: EP0 is idle between enumeration steps, so a
     * teardown found an empty queue. A HID device keeps an interrupt read posted
     * at all times, so **every unplug** reaches it.
     *
     * **Batch 7a-B is what finally answers them, and it answers them earlier
     * rather than later.** Every endpoint is armed for a Stop Endpoint below,
     * and each one's queue is drained the moment its stop completes - which is
     * both the ordering 4.6.4 asks for and a window measured in command
     * completions rather than in whatever the Disable Slot costs.
     *
     * **The stronger reason once given for it does not hold, and saying so here
     * is the point** (repo-audit finding A1, settled from both
     * shipping builds - `tools/win2ksp4-extracted/usbport-removedevice-disasm.txt`).
     * This comment used to add that usbport frees a NUKEd endpoint's transfers
     * itself with no miniport callback (SP4 `0x16909` skips the `AbortTransfer`
     * slot, `0x19B21` completes them with `USBD_STATUS_DEVICE_GONE`, `0x18973`
     * frees the record), and concluded that a transfer held indefinitely is a
     * pointer usbport may reclaim. The short-circuit is real; the lifetime it
     * was attributed to is not. `ENDPOINT_FLAG_NUKE` is set at exactly one
     * instruction per image, inside a routine that walks the *controller's*
     * whole endpoint list and is only called after this miniport's own
     * `StopController` has returned. A **device** removal marks
     * `Endpoint->Flags |= 0x20` instead, aborts each endpoint - which does
     * reach `AbortTransfer` - and then blocks at PASSIVE in a 100 ms poll loop
     * until the device handle's references drain. So a held transfer is
     * unanswered, not reclaimed, and 4.6.4 p.97 above is the whole reason the
     * queues are drained in command-completion time.
     */
    xhciDevCancelSetAddress(ext, dev, XHCI_USBD_STATUS_CANCELED);

    dev->Flags &= ~(XHCI_DEV_FLAG_EP0_OPEN | XHCI_DEV_FLAG_ADDRESS_VALID);
    dev->State = XHCI_DEV_STATE_GONE;
    dev->PendingOp = XHCI_DEV_OP_NONE;

    /*
     * Stop every endpoint before the slot goes. "Before a Disable Slot Command
     * is issued ... any active endpoints of the device slot shall be in the
     * Stopped state or Idle in the Running state, and any outstanding Transfer
     * Events shall have been received" (4.6.4 p.97). Phase 6 could not reach
     * that clause because EP0 is idle between enumeration steps; a HID device
     * keeps an interrupt read posted, so every unplug does.
     *
     * The Disable Slot below is only *owed*, not issued: `xhciDevOwedOp` puts
     * the quiescence commands ahead of `PendingOp`, so the stops go first.
     */
    xhciDevArmTeardownStops(ext, dev);

    /*
     * A *device* command in flight finishes the unwind from its own completion.
     * A quiescence command does not - it completes into the endpoint machine and
     * knows nothing about slots - so the Disable Slot has to be owed here, and
     * `xhciDevOwedOp` is what keeps it behind the stops.
     */
    if (dev->ActiveOp != XHCI_DEV_OP_NONE &&
        !xhciDevOpIsQuiesce(dev->ActiveOp)) {
        return;                 /* its completion will finish the unwind */
    }
    if (dev->SlotId != 0) {
        dev->PendingOp = XHCI_DEV_OP_DISABLE_SLOT;
        return;
    }
    /* No Slot ID was ever granted, so there are no Endpoint Contexts to be
     * pointing at anything. */
    xhciDevRelease(ext, dev, 1);
}

/*
 * Stop serving this record. The record keeps its slot - the xHC still has one
 * enabled, and giving it back needs a Disable Slot this path has no reason to
 * issue - but stops serving transfers, so anything usbport submits is completed
 * with an error instead of being retried for ever.
 *
 * **The queued transfers are not completed here.** The slot is still enabled,
 * the DCBAA entry live and the rings Running, and nothing on any caller's path
 * has shown an endpoint stopped - so a drain from here would hand each
 * transfer's mapped buffer back to usbport while the xHC can still DMA into it
 * (a posted interrupt-IN read is the concrete case: the device's next report
 * lands in reclaimed pages). That is the same evidence rule `xhciEpQuiesceFail`
 * and `XhciSlotInvalidateAll` already follow. Instead the teardown's own
 * machinery is used: a stop with a drain intent is armed on every busy
 * endpoint, and each queue is answered the moment its Stop Endpoint completes
 * (`xhciEpStopped`) - answered with `USBD_STATUS_CANCELED` by that shared
 * path, which is as terminal as the HC-error status this function used to
 * write. If a stop cannot complete - the engine is what failed - the transfers
 * stay queued, which is safe: usbport's own timeout aborts them through
 * `AbortTransfer`, or `XhciSlotInit` answers them after the HCRST that follows
 * a controller failure.
 *
 * The intercepted SET_ADDRESS is the one thing answered immediately: it owns
 * no TRBs, so there is nothing on any ring for the xHC to execute.
 *
 * The invalidation matters as much as the state: usbport is holding transfers it
 * was told to retry, and without being asked to re-offer them they wait on its
 * 500 ms timer before they can reach the gate that now fails them.
 *
 * Called with the lock held. IRQL: <= DISPATCH_LEVEL.
 */
static VOID xhciDevFailRecord(PXHCI_EXTENSION ext, PXHCI_DEVICE dev, ULONG why)
{
    dev->State = XHCI_DEV_STATE_FAILED;
    dev->PendingOp = XHCI_DEV_OP_NONE;
    xhciDevCancelSetAddress(ext, dev, XHCI_USBD_STATUS_INTERNAL_HC_ERROR);
    xhciDevArmTeardownStops(ext, dev);
    xhciDevOweInvalidate(ext, dev);
    XHCI_DBG_VALUE_CHANGED("slot: device failed, reason << 8 | completion code",
                           (why << 8) | dev->LastCompletionCode);
}

/*
 * The command chain failed. Separate from the unwind above only so that the
 * counter stays honest: task 7b-A.0's progress detector reaches the same state
 * with no command involved at all, and folding it in here would report a
 * hardware command failure that never happened.
 *
 * Called with the lock held.
 */
static VOID xhciDevFail(PXHCI_EXTENSION ext, PXHCI_DEVICE dev, ULONG why)
{
    ext->CommandFailures++;
    xhciDevFailRecord(ext, dev, why);
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

/* IRQL: <= DISPATCH_LEVEL (the in-place recovery runs XhciInitController from
 * a DPC). See the contract in src/xhci_hw.h. */
VOID XhciSlotInit(PXHCI_EXTENSION ext)
{
    ULONG i;

    if (ext == NULL) {
        return;
    }

    /*
     * **Answer what an abandonment left outstanding, before the table goes.**
     *
     * `XhciSlotInvalidateAll` deliberately does *not* complete the transfers of
     * a record it cannot prove the controller has let go of: a completion hands
     * the transfer's mapped buffer back to usbport, and those TRBs were still on
     * a live ring. This function is the other end of that, and it is safe here
     * for one reason only - it runs after HCRST, where "all of the Operational
     * and Runtime Registers shall be at their default values" (4.23.1, p.312),
     * so there is no ring, no enabled slot and no DCBAA entry left to execute
     * them. Moving this call earlier in the init sequence would reintroduce
     * exactly the fault it exists to close.
     *
     * Under the lock, because the completion list is the DPC's too. On an
     * ordinary start there is nothing here: usbport zeroed the extension.
     */
    {
        KIRQL oldIrql;

        XhciControllerLockAcquire(&oldIrql);
        for (i = 0; i < XHCI_MAX_SLOTS; i++) {
            if (ext->Devices[i].State != XHCI_DEV_STATE_FREE) {
                xhciDevCancelWork(ext, &ext->Devices[i],
                                  XHCI_USBD_STATUS_CANCELED);
                /* The table is zeroed below. On a start there is nothing here
                 * (usbport zeroed the extension); on a resume reinitialisation
                 * the extension survives, and so should what these queues
                 * measured. */
                xhciDevFoldQueues(ext, &ext->Devices[i]);
            }
        }
        XhciControllerLockRelease(oldIrql);
    }

    for (i = 0; i < XHCI_MAX_SLOTS * (sizeof(XHCI_DEVICE) / sizeof(ULONG));
         i++) {
        ((ULONG *)ext->Devices)[i] = 0;
    }
    ext->CommandOwner = 0;
    ext->CommandOwnerOp = XHCI_DEV_OP_NONE;
    ext->PumpCursor = 0;
    /*
     * The completion list is **not** cleared: it may hold transfers the loop
     * above has just detached, and dropping the head here would lose the answer
     * usbport is waiting for rather than tidy anything. Its own drain empties
     * it, and the caller reaches that drain after this returns.
     */
    /* The invalidation debts went with the device table the loop above cleared -
     * they live on the records themselves now - so what is left here is the
     * running total that counts them. */
    ext->EndpointInvalidatesOwed = 0;
    ext->DeferredBusy = 0;
    /*
     * Task 7b-A.1: the device table this function just cleared is what every
     * topology node's position was derived from, so the graph goes with it. On
     * an ordinary start usbport has already zeroed the extension and this is a
     * no-op; what it covers is a reinitialisation, where the tree would
     * otherwise describe devices whose records have just been destroyed.
     */
    XhciTopoReset(&ext->Topology);
    ext->EnumHubPort = 0;
    /*
     * Unspent, not spent: dropping the hint is exactly the "a reset completion
     * was missed" case the fallback scan exists for, so leaving the claim spent
     * here would disable the recovery this function is part of.
     */
    ext->EnumClaimSpent = 0;
    ext->EnumResetSuppressed = 0;
    /*
     * EnumSequence deliberately survives, because it is not device state: it
     * counts port resets for the life of the extension, and a test that watched
     * it reset would be watching this function rather than the port.
     */
}

/* IRQL: <= DISPATCH_LEVEL, controller lock held. */
VOID XhciSlotInvalidateAll(PXHCI_EXTENSION ext, ULONG controllerStopped)
{
    ULONG i;

    if (ext == NULL || ext->Signature != XHCI_EXTENSION_SIGNATURE) {
        return;
    }
    for (i = 0; i < XHCI_MAX_SLOTS; i++) {
        PXHCI_DEVICE dev = &ext->Devices[i];

        if (dev->State == XHCI_DEV_STATE_FREE) {
            continue;
        }
        dev->Flags &= ~(XHCI_DEV_FLAG_EP0_OPEN | XHCI_DEV_FLAG_ADDRESS_VALID);
        dev->PendingOp = XHCI_DEV_OP_NONE;
        dev->ActiveOp = XHCI_DEV_OP_NONE;

        if (!controllerStopped) {
            /*
             * **No evidence that the xHC has let go**, so the record is
             * abandoned rather than released: its Slot ID stays taken, its
             * common-buffer blocks stay reserved, and its DCBAA entry is left
             * exactly as the controller is still reading it. Releasing here
             * would hand the same Slot ID and the same device context to the
             * next device while the hardware was still following the old one.
             *
             * **And the queued transfers stay queued, which is the half a first
             * draft got backwards.** It completed them here on the reasoning
             * that usbport is waiting on them whatever the controller is doing.
             * But a completion is not just an answer: `UsbPortCompleteTransfer`
             * gives the transfer's *mapped buffer* back - usbport unmaps the
             * scatter/gather list and finishes the URB, and the pages return to
             * whoever owned them. Those TRBs are still on a live EP0 ring, on an
             * endpoint the xHC still has Running, reachable through a DCBAA
             * entry this branch has deliberately left alone. Answering them here
             * is a DMA target handed back while the controller can still write
             * into it - the same class of fault XhciFailClosedDma exists for,
             * and one this path would have created rather than reported.
             *
             * They are answered by whichever path *does* prove the controller
             * stopped: the reinitialisation's HCRST reaches XhciSlotInit, which
             * drains them. Where nothing ever proves it - a stop whose quiesce
             * failed - the caller's XhciFailClosedDma is the answer, and it
             * bugchecks rather than completing anything.
             *
             * `EndpointExtension` is kept for the same reason: it is what a
             * later completion is answered through.
             */
            dev->State = XHCI_DEV_STATE_GONE;
            ext->DevicesAbandoned++;
            continue;
        }

        /*
         * The controller is provably stopped, so the transfers can go back:
         * their TRBs cannot be executed and their buffers cannot be written.
         */
        xhciDevCancelWork(ext, dev, XHCI_USBD_STATUS_CANCELED);
        dev->EndpointExtension = NULL;

        /*
         * No Disable Slot and no DCBAA write on this path either, and here the
         * reason is that neither is needed rather than that neither is safe: a
         * halted controller has no command ring to run a Disable Slot on, and
         * the DCBAA is about to be rebuilt from scratch - or freed with the
         * common buffer. Dropping the flag first is what keeps xhciDevRelease
         * from being the place that decides it.
         */
        dev->Flags &= ~XHCI_DEV_FLAG_DCBAA_SET;
        /* The controller is provably stopped, which is the same evidence that
         * lets the transfers go: no endpoint can be executing, so no pooled ring
         * is reachable. */
        xhciDevRelease(ext, dev, 1);
        ext->DevicesInvalidated++;
    }
    ext->CommandOwner = 0;
    ext->CommandOwnerOp = XHCI_DEV_OP_NONE;
    /*
     * Every record here has just been released or abandoned, so no address in
     * the graph still names a device this driver is serving (task 7b-A.1).
     */
    XhciTopoReset(&ext->Topology);
    ext->EnumHubPort = 0;
    ext->EnumClaimSpent = 0;
    ext->EnumResetSuppressed = 0;
}

/* ------------------------------------------------------------------ */
/* Task 6-B.1: QueryEndpointRequirements                               */
/* ------------------------------------------------------------------ */

/* IRQL: DISPATCH_LEVEL, under MiniportSpinLock. Touches no state at all. */
VOID XhciSlotQueryEndpointRequirements(
    PXHCI_EXTENSION ext,
    const USBPORT_ENDPOINT_PROPERTIES *properties,
    PUSBPORT_ENDPOINT_REQUIREMENTS requirements)
{
    if (requirements == NULL) {
        return;
    }

    /*
     * **HeaderBufferSize 0: this driver asks usbport for no per-endpoint common
     * buffer at all**, and that is a decision rather than an omission.
     *
     * The roadmap's wording for this task predates batch 6-0. It asked for a
     * buffer big enough to hold a transfer-ring segment - which was design doc
     * 04 section 3.4's model until the disassembly showed that
     * `USBPORT_ReopenPipe` **frees that buffer and reallocates it** between a
     * REMOVE and the next open, with no close callback in between and no
     * guarantee the new one is at the same address. A ring there would be
     * destroyed underneath a TR Dequeue Pointer the xHC still holds, exactly
     * during the EP0 reopen that task 6-B.4 exists for. EP0's ring therefore
     * lives in the controller common buffer, and a per-endpoint buffer would be
     * memory nothing reads.
     *
     * **Where a non-default endpoint's ring lives has since been settled the
     * same way**, and this comment described it as an open question until task
     * 8-A.1 - a stale scope note left behind by two phases of work. Design doc
     * 04 section 3.6 was resolved in batch 7a-A: those rings come from a
     * 32-entry pool in the *controller* block, for the reason above rather than
     * a different one, because a per-endpoint buffer is unsound at any price
     * and not merely expensive. So 0 is the answer for every type, and it is
     * now a settled one.
     *
     * The old sentence "the open refuses anything but EP0 today" was true when
     * it was written and is doubly false now: interrupt endpoints opened from
     * task 7a-A.1 and bulk from 8-A.1. Nothing depends on it - `HeaderBufferSize`
     * is 0 either way - which is exactly why it survived unread.
     */
    requirements->HeaderBufferSize = 0;

    /*
     * MaxTransferSize is only adopted by usbport for bulk and interrupt
     * endpoints - the default pipe keeps usbport's own 0x1000 - so it was
     * advisory until task 7a-A.1 and is what actually bounds a bulk transfer
     * from task 8-A.1. One number for every type, because a cap that changes
     * meaning between phases is how the transfer splitter and the ring's
     * capacity stop agreeing.
     */
    requirements->MaxTransferSize = XHCI_MAX_TRANSFER;

    XHCI_DBG_CB("QueryEndpointRequirements", ext, properties, requirements);
}

/* ------------------------------------------------------------------ */
/* Task 6-B.2 / 6-B.4: opening EP0                                     */
/* ------------------------------------------------------------------ */

/*
 * Which root-hub port is the address-0 pipe on?
 *
 * usbport cannot say (see the file header), so the answer is derived and then
 * **checked**. The derivation is the port whose reset completed most recently:
 * usbhub resets a port and then immediately creates a device at address 0 on it,
 * so that is the one event that means "this port is being enumerated". The check
 * is a live read of the port's own shadow - still connected, still enabled - and
 * a candidate that fails it is discarded rather than used.
 *
 * The fallback exists because a reset completion can be missed (a stop between
 * the two, a shadow rebuilt by a resume) and refusing every open afterwards
 * would be worse than the ambiguity: if exactly one managed port is connected,
 * enabled and not already serving a device, that is the port, and if there is
 * more than one the answer is a refusal rather than a guess.
 *
 * **Both routes need permission as well as an answer** (task 7b-A.0). Naming a
 * port is not the same as being entitled to claim it, and until batch 7b-V0 this
 * function conflated the two: the hint is set by every port reset and was never
 * consumed, so a *second* address-0 open with no reset between it and the first
 * was handed the same port again. That is precisely the shape of a device behind
 * a hub - usbhub resets the hub's downstream port, which this driver cannot see,
 * and then creates a device at address 0 - and the caller's re-enumeration
 * branch then took the hub's own record apart. `EnumClaimSpent` is the
 * permission: one claim per reset, re-armed by the next one.
 *
 * Called with the lock held. Returns a hub port, or 0.
 */
static ULONG xhciDevEnumeratingPort(PXHCI_EXTENSION ext)
{
    ULONG hubPort;
    ULONG found;
    ULONG i;

    if (ext->RootHub.Status != XHCI_RH_OK) {
        return 0;
    }
    if (ext->EnumClaimSpent) {
        return 0;
    }

    if (ext->EnumHubPort != 0 && ext->EnumHubPort <= ext->RootHub.PortCount) {
        const XHCI_PORT_SHADOW *shadow;

        shadow = &ext->RootHub.Ports[ext->EnumHubPort - 1];
        /*
         * Connected and enabled, and **deliberately nothing about whether the
         * port already has a record**. usbhub re-enumerates by resetting the
         * port and creating the device again at address 0, so a port with a
         * record is the *ordinary* second case rather than a disqualification -
         * and the caller knows what to do with one. Requiring the port to be
         * free here was the first draft's mistake: it refused every
         * re-enumeration, which is the recovery path a device that failed once
         * depends on.
         *
         * The scan below keeps that rule, because there it is doing different
         * work: with no reset to name a port it is disambiguating, and a port
         * that already has a device is not the one being enumerated.
         */
        if ((shadow->Portsc & (XHCI_PORTSC_CCS | XHCI_PORTSC_PED)) ==
            (XHCI_PORTSC_CCS | XHCI_PORTSC_PED)) {
            return ext->EnumHubPort;
        }
    }

    found = 0;
    hubPort = 0;
    for (i = 1; i <= ext->RootHub.PortCount; i++) {
        const XHCI_PORT_SHADOW *shadow;

        shadow = &ext->RootHub.Ports[i - 1];
        if ((shadow->Portsc & (XHCI_PORTSC_CCS | XHCI_PORTSC_PED)) !=
            (XHCI_PORTSC_CCS | XHCI_PORTSC_PED)) {
            continue;
        }
        if (xhciDevByHubPort(ext, i) != NULL) {
            continue;
        }
        found++;
        hubPort = i;
    }

    return (found == 1) ? hubPort : 0;
}

/*
 * The Slot Type an Enable Slot for this port must carry. It comes from the
 * Supported Protocol capability of the group the port belongs to, which the port
 * map already holds - so it is looked up rather than assumed to be the 0 that
 * every USB 2.0 group measured so far uses. Called with the lock held.
 */
static ULONG xhciDevSlotType(PXHCI_EXTENSION ext, ULONG port)
{
    ULONG index;

    if (ext->PortMapStatus != XHCI_CAPS_OK || port == 0 ||
        port > XHCI_MAX_ROOT_PORTS) {
        return 0;
    }
    index = ext->PortMap.Protocol[port - 1];
    if (index >= XHCI_MAX_PROTOCOLS) {
        return 0;
    }
    return ext->PortMap.Protocols[index].SlotType;
}

/*
 * The default control endpoint - Phase 6's, and still the only *control*
 * endpoint usbport opens.
 *
 * EndpointAddress carries the raw bEndpointAddress including its direction bit;
 * the default pipe is endpoint number 0, which spec 4.5.1 makes DCI 1 whichever
 * way that bit reads. IRQL: any.
 */
static ULONG xhciDevPropertiesAreEp0(
    const USBPORT_ENDPOINT_PROPERTIES *properties)
{
    if (properties == NULL) {
        return 0;
    }
    if (properties->TransferType != USBPORT_TRANSFER_TYPE_CONTROL) {
        return 0;
    }
    return ((properties->EndpointAddress & 0x7FU) == 0) ? 1 : 0;
}

/*
 * A non-default endpoint this driver serves: **interrupt or bulk, with a real
 * endpoint number**, and nothing else.
 *
 * Bulk joined interrupt in task 8-A.1. The two share every part of the path -
 * the pool ring, the Configure Endpoint, the Normal-TRB builder, the queue and
 * the event - because the xHCI difference between them lives entirely in the
 * Endpoint Context's EP Type and Interval fields, which `XhciBuildEndpointParams`
 * has encoded since task 7a-A.1. What was missing was never the encoding; it was
 * an open that admitted the type and a submit that derived the direction from it.
 *
 * **Isochronous joined them in task 9-A.1**, on the same terms and after the
 * same kind of missing piece was found rather than invented. What had kept it
 * out was that usbport forces an isoch `Period` to 1 and carries no interval at
 * all, so there was no source for the Endpoint Context's Interval field; task
 * 9-0.1's disassembly supplied one from the other direction - the frame and
 * microframe usbport stamps on each packet of a request *are* an ESIT (see
 * `XhciBuildEndpointParams`). It still differs from the two above in the two
 * places that matter, and neither of them is in this predicate: it needs an
 * `XHCI_RING_KIND_ISOCH` ring, and its transfers arrive through a different
 * callback entirely.
 *
 * The one exclusion that remains is a decision rather than a gap: a **non-
 * default control endpoint** is not something either shipping usbport build
 * opens.
 *
 * IRQL: any.
 */
static ULONG xhciDevPropertiesAreNonDefault(
    const USBPORT_ENDPOINT_PROPERTIES *properties)
{
    if (properties == NULL) {
        return 0;
    }
    if (properties->TransferType != USBPORT_TRANSFER_TYPE_INTERRUPT &&
        properties->TransferType != USBPORT_TRANSFER_TYPE_BULK &&
        properties->TransferType != USBPORT_TRANSFER_TYPE_ISOCHRONOUS) {
        return 0;
    }
    return ((properties->EndpointAddress & 0x7FU) != 0) ? 1 : 0;
}

/*
 * Task 9-A.1. Which ring kind an endpoint's transfer ring is created with, from
 * the type usbport declared.
 *
 * It is a *behavioural* choice and not a label. `XHCI_RING_KIND_ISOCH` is what
 * makes `XhciRingClassifyEvent` accept Missed Service and Isoch Buffer Overrun
 * as legal codes at all, decline to read an error as a halt ("an isoch endpoint
 * never halts", p.177), and let `XhciRingRetireIsoGroup` reclaim a group. An
 * isochronous endpoint given an `XHCI_RING_KIND_ENDPOINT` ring rejects its own
 * controller's ordinary events and asks for recovery on a pipe that is running.
 *
 * IRQL: any.
 */
static ULONG xhciDevRingKindFor(ULONG transferType)
{
    return (transferType == USBPORT_TRANSFER_TYPE_ISOCHRONOUS)
               ? XHCI_RING_KIND_ISOCH
               : XHCI_RING_KIND_ENDPOINT;
}

/*
 * Does this EP Type move bytes towards the host?
 *
 * Task 8-A.1. Written once here rather than as a comparison at each site,
 * because the submit path derives a transfer's direction from the Endpoint
 * Context this driver programmed - deliberately, so that usbport's own
 * `TransferFlags` bit is a second opinion to check against rather than the
 * source - and a site that knew about `INTERRUPT_IN` but not `BULK_IN` would
 * quietly build every bulk IN transfer as an OUT one and refuse it on the
 * direction conflict the check exists to catch.
 *
 * IRQL: any.
 */
static ULONG xhciEpTypeIsIn(ULONG epType)
{
    return (epType == XHCI_EP_TYPE_INTERRUPT_IN ||
            epType == XHCI_EP_TYPE_BULK_IN ||
            epType == XHCI_EP_TYPE_ISOCH_IN) ? 1UL : 0UL;
}

/* Task 9-A.1, and the reason it exists is the same one that comment gives: the
 * only other reading of "is this endpoint isochronous" is usbport's
 * `TransferType`, which is not available on every path that has to know - a
 * reprogram commits from `PendingParams` long after the properties are gone. */
static ULONG xhciEpTypeIsIsoch(ULONG epType)
{
    return (epType == XHCI_EP_TYPE_ISOCH_IN ||
            epType == XHCI_EP_TYPE_ISOCH_OUT) ? 1UL : 0UL;
}

/*
 * usbport's speed for this endpoint, in this driver's vocabulary.
 *
 * **This is not the same question as "how fast is the device", and the
 * difference is load-bearing.** `Period` was bucketed by usbport using the speed
 * *usbport* believes, so it counts microframes when that speed is High Speed and
 * frames otherwise - and the conversion to an xHCI `Interval` has to use the
 * speed that produced the number, not the one the port decoded. The Slot
 * Context's Speed field is the other one and is filled from the port
 * (`dev->Psiv`), because that is what tells the xHC how to talk to the device.
 *
 * The two do disagree today, systematically: Phase 5 task 7 reports **every**
 * connected root port to usbport as High Speed, so a Full- or Low-Speed HID
 * device arrives here with `DeviceSpeed == UsbHighSpeed` and a `Period` already
 * bucketed in microframes. The endpoint is then scheduled on the microframe
 * reading, which is the correct reading *of that number* and **not** the
 * interval the descriptor asked for - usbport's High-Speed bucketing has already
 * discarded that. The endpoint ends up serviced at least as often as it asked
 * and sometimes more; see `XhciIntervalForSpeed` for what that costs, why
 * refusing is not available, and who owns removing the cause.
 * `EndpointSpeedMismatches` and `EndpointIntervalsFloored` are what make it
 * visible on a target rather than a paragraph.
 *
 * IRQL: any.
 */
static ULONG xhciDevSpeedFromProperties(
    const USBPORT_ENDPOINT_PROPERTIES *properties)
{
    if (properties->DeviceSpeed == UsbLowSpeed) {
        return XHCI_SPEED_LOW;
    }
    if (properties->DeviceSpeed == UsbFullSpeed) {
        return XHCI_SPEED_FULL;
    }
    if (properties->DeviceSpeed == UsbHighSpeed) {
        return XHCI_SPEED_HIGH;
    }
    return XHCI_SPEED_UNKNOWN;
}

/*
 * Do two sets of endpoint parameters describe the same endpoint?
 *
 * `DequeuePA` and `Dcs` are deliberately **not** compared: they are the ring's
 * current position rather than a property of the endpoint, and they move on
 * every transfer. Everything else is what a Configure Endpoint programmed, so a
 * difference means usbport is describing a *different* endpoint at the same DCI
 * - an alternate-interface change - which needs a Drop plus an Add and is task
 * 7a-B.1's. IRQL: any.
 */
static ULONG xhciEpParamsSame(const XHCI_EP_PARAMS *a, const XHCI_EP_PARAMS *b)
{
    return (a->EpType == b->EpType &&
            a->MaxPacketSize == b->MaxPacketSize &&
            a->MaxBurstSize == b->MaxBurstSize &&
            a->Mult == b->Mult &&
            a->Interval == b->Interval &&
            a->ErrorCount == b->ErrorCount &&
            a->AverageTrbLength == b->AverageTrbLength &&
            a->MaxEsitPayload == b->MaxEsitPayload)
               ? 1
               : 0;
}

/*
 * If usbport did allocate a per-endpoint common buffer, is it one a ring could
 * live in?
 *
 * Unreachable today - this driver asks for HeaderBufferSize 0 - and written
 * anyway, because task 7a-B.1 decides whether a non-default endpoint's ring goes
 * there, and the check that would have caught a buffer straddling a 64 KB
 * boundary is exactly the kind that gets written *after* the corruption it
 * explains. It refuses rather than ignores: a buffer this driver cannot use is a
 * disagreement with usbport about the request it answered.
 *
 * IRQL: any.
 */
static ULONG xhciDevBufferUsable(const USBPORT_ENDPOINT_PROPERTIES *properties)
{
    ULONG trbs;

    if (properties->BufferLength == 0) {
        return 1;               /* none asked for, none supplied */
    }
    if (properties->BufferVA == 0 || properties->BufferPA == 0) {
        return 0;
    }
    trbs = properties->BufferLength / XHCI_TRB_BYTES;
    return XhciSegmentUsable(properties->BufferPA, trbs, XHCI_TRB_BYTES);
}

/*
 * The Max Packet Size usbport is describing this endpoint with.
 *
 * `TotalMaxPacketSize` is the field that carries the corrected MPS0 after the
 * device descriptor has been read (`docs/usb-xhci-info/usbport-miniport-abi.md` section 4,
 * [device.c:1365]), and `MaxPacketSize` is the raw wMaxPacketSize. They agree for
 * a control endpoint, which has no transactions-per-microframe multiplier, so
 * either would do - and the one that is documented to carry the correction is
 * the one to read, with the other as the fallback for a build that leaves it 0.
 * IRQL: any.
 */
static ULONG xhciDevPropertiesMps(
    const USBPORT_ENDPOINT_PROPERTIES *properties)
{
    if (properties->TotalMaxPacketSize != 0) {
        return properties->TotalMaxPacketSize;
    }
    return properties->MaxPacketSize;
}

/*
 * **Read the slot's actual state and decide what the chain owes from it.** The
 * decision half of the re-entry below, in a function of its own because a second
 * caller needs exactly this and nothing else around it: a device command that
 * completed for a tenancy that has since been re-enumerated (see
 * `XhciSlotCommandEvent`'s tenancy guard) changed the slot in hardware *after*
 * the re-entry read it, so the debt the re-entry recorded was derived from a
 * reading the completion invalidated. Re-deriving is the only honest answer -
 * the same answer for the same reason, one command later.
 *
 * The endpoint-record sweep is deliberately **not** in here. It is a statement
 * about contexts that are going away, it is made once per re-enumeration, and
 * repeating it would count `EndpointContextsDroppedByReset` again for records
 * already reconciled.
 *
 * Called with the lock held. IRQL: DISPATCH_LEVEL.
 */
static VOID xhciDevOweFromSlotState(PXHCI_EXTENSION ext, PXHCI_DEVICE dev)
{
    ULONG slotState;

    if (!xhciDevHardwareSlotState(ext, dev, &slotState)) {
        /*
         * No readable Output Slot Context - no carve, no DCBAA entry, or a
         * layout that refused. Fall back to this driver's own record, and in the
         * direction that keeps the *later* command legal: anything that ever
         * reached Addressed is treated as Addressed, because Reset Device
         * against a Default slot and Address Device (BSR) against a Configured
         * one both answer Context State Error, and only the first of those two
         * mistakes is recoverable - the chain's next step is Address Device with
         * BSR = 0, which is legal from Default as well as Enabled.
         *
         * `XHCI_DEV_STATE_DEFAULT` gets its own answer for that same reason
         * rather than falling into the `DISABLED` default. It is written only
         * where hardware has *confirmed* a Default slot - a Reset Device or a
         * BSR-form Address Device that completed with Success, and the readable
         * branch below - so treating it as Disabled would owe an `ADDRESS_BSR`
         * that a slot at Default refuses with Context State Error, costing an
         * enumeration round. Owing nothing keeps the later command legal, which
         * is the whole principle this fallback is written to.
         */
        if (dev->State == XHCI_DEV_STATE_ADDRESSED) {
            slotState = XHCI_SLOT_STATE_ADDRESSED;
        } else if (dev->State == XHCI_DEV_STATE_DEFAULT) {
            slotState = XHCI_SLOT_STATE_DEFAULT;
        } else {
            slotState = XHCI_SLOT_STATE_DISABLED;
        }
        ext->SlotStatesUnreadable++;
    }

    switch (slotState) {
    case XHCI_SLOT_STATE_DEFAULT:
        /* Already exactly where the BSR form would have put it: Slot State
         * Default, address 0, EP0 Running. Nothing is owed, and issuing anything
         * would be issuing it from a state that refuses it. */
        dev->State = XHCI_DEV_STATE_DEFAULT;
        dev->PendingOp = XHCI_DEV_OP_NONE;
        xhciDevEp0Restored(dev);
        break;
    case XHCI_SLOT_STATE_ADDRESSED:
    case XHCI_SLOT_STATE_CONFIGURED:
        dev->State = XHCI_DEV_STATE_ENABLED;
        dev->PendingOp = XHCI_DEV_OP_RESET_DEVICE;
        break;
    default:
        /* 0 is Disabled *or* Enabled in one encoding. A slot this record holds
         * has been enabled, so it is the Enabled half - the one state the BSR
         * form is legal from. */
        dev->State = XHCI_DEV_STATE_ENABLED;
        dev->PendingOp = XHCI_DEV_OP_ADDRESS_BSR;
        break;
    }
}

/*
 * **A record that is keeping its slot across a re-enumeration: what does the
 * chain owe to get that slot back to Default?** The repo audit's finding A2, and
 * the one step both reuse branches used to get wrong in the same way.
 *
 * The branches used to queue an Address Device with BSR = 1 unconditionally,
 * under a comment citing 4.6.5's valid states as "Enabled, Default or Addressed".
 * That list is not in the specification. What 4.6.5 p.101 actually says is that
 * the BSR form is legal **from the Enabled state and no other** - "else // The
 * slot is not in the Enabled state: Completion Code = Context State Error" - so
 * on every slot that has already been addressed once (which is every slot on its
 * second enumeration, and every slot whose interrupt or bulk pipe was opened is
 * Configured on top of that) a conforming xHC refuses the command and the device
 * is dead until an unplug releases the slot. QEMU does not enforce the check,
 * which is why five audits and every VM run missed it.
 *
 * The three cases and their commands are transcribed in
 * `docs/usb-xhci-info/xhci-data-structures.md`, "Which Slot State each command
 * requires". Read from the Output Slot Context rather than from `dev->State`,
 * because it is the xHC's answer that the command will be judged against and
 * `dev->State` does not even distinguish Addressed from Configured.
 *
 * **The endpoint records are downgraded whichever command is owed**, and that is
 * the second half of the finding. Address Device rewrites Context Entries to 1
 * ("all fields of the Output Slot Context are overwritten by the xHC", 6.2.2.1
 * p.412) and Reset Device sets it to 1 and every endpoint but EP0 to Disabled
 * (4.6.11 p.130) - so after either, the slot claims no non-default endpoint.
 * Records left saying `CONFIGURED` make the later same-parameter reopen take the
 * rebind-only path, no Configure Endpoint is ever re-issued, and this driver
 * rings doorbells for DCIs the Slot Context no longer has. Putting them back to
 * `PENDING` is the established mechanism (`xhciEpContextRestore`): the pump
 * issues the Configure Endpoint from that state and submissions meanwhile are
 * refused for a retry rather than failed.
 *
 * The ring is deliberately not rebuilt. `xhciDevBuildConfigureInput` takes the
 * dequeue pointer and its cycle from the ring live, so the re-issued command
 * programs wherever software actually is - which is the same contract the reopen
 * path relies on, and rebuilding underneath a TR Dequeue Pointer the xHC may
 * still hold is the defect that arrangement exists to avoid.
 *
 * Called with the lock held. IRQL: DISPATCH_LEVEL.
 */
static VOID xhciDevReenterAtDefault(PXHCI_EXTENSION ext, PXHCI_DEVICE dev)
{
    ULONG i;

    if (dev->SlotId == 0) {
        dev->State = XHCI_DEV_STATE_RESERVED;
        dev->PendingOp = XHCI_DEV_OP_ENABLE_SLOT;
        return;
    }

    /*
     * **Every record's *state*, whatever state it is in - and deliberately not
     * its quiescence.** Two review rounds shaped this loop and the second
     * overturned half of the first, so both are written down.
     *
     * Round 1 was right that downgrading only `CONFIGURED` is too little:
     *
     *   - `REFUSED` and `FAILED` are **terminal**. They are verdicts about an
     *     Endpoint Context that is about to stop existing, so carrying one past
     *     a re-enumeration fails a pipe for the life of the record on the
     *     strength of what the *previous* enumeration's controller said.
     *   - A record already `CONFIGURING` has a Configure Endpoint on the ring.
     *     Left alone it completes as `CONFIGURED` **after** this sweep and
     *     before the Reset Device that removes the context it just gained.
     *     Putting it back to `PENDING` here is also what disarms that
     *     completion: it commits nothing unless it still finds `CONFIGURING`.
     *
     * **Nothing here erases quiescence state, and the one bit it *sets* is the
     * one that is true.** Two review rounds argued this and both were needed.
     *
     * Round 1 cleared each endpoint's `XHCI_EP_QUIESCE`, on the reasoning that an
     * owed quiescence command names an endpoint the controller is about to
     * disable. It does not: the Reset Device is *behind* that command, because
     * `xhciDevOwedOp` ranks quiescence ahead of `PendingOp` - which is exactly
     * the ordering 4.6.11 p.129 asks for ("Software should stop all endpoint
     * activity before issuing a Reset Device Command"), and the same ordering a
     * teardown uses for 4.6.4's Disable Slot. So the chain runs first, against
     * contexts that still exist, and finishes. What the clearing did instead was
     * falsify the state an **already outstanding** command's completion reads:
     * an erased `XHCI_EPQ_DRAIN` means a successful Stop Endpoint no longer
     * cancels the transfers it was issued to cancel, and a zeroed `DequeuePA`
     * makes a Set TR Dequeue Pointer's consistency check see a mismatch that did
     * not happen. Zeroing the record of a command in flight is not the same as
     * cancelling it.
     *
     * Round 2 moved the clearing to the Reset Device completion and round 3
     * found that wrong too, for the mirror-image reason: a **new** debt can be
     * armed while that command is outstanding - `AbortTransfer` arms a
     * reposition, a `SetEndpointState(REMOVE)` arms a drain - and neither is an
     * outstanding *command*, so the one-command-per-record argument does not
     * cover them. Discarding those loses a cancellation, which is a transfer
     * whose buffer usbport has reclaimed left standing on a ring.
     *
     * **So nothing is discarded, and `XHCI_EPQ_NO_CONTEXT` is set instead.** It
     * is the exact statement this situation makes - the Endpoint Context is
     * about to stop existing - it is already the bit every doorbell path and the
     * submit gate consult, and its documented lifecycle already says what ends
     * it: "whichever command gives that endpoint a context back", which for a
     * non-default endpoint is the Configure Endpoint the `PENDING` state above
     * will produce. Round 3's second finding closes with it: a stale `STOPPED`
     * or `RESTART` can no longer reach a doorbell, in the window before the
     * Reset Device *or* after it, because both restart paths refuse on this bit.
     *
     * `DropOnConfigure` goes here, because after Reset Device the Output
     * Endpoint Context **is** Disabled, which is exactly the state 4.6.6 p.105
     * requires for an Add with no Drop beside it. `PendingParams` is reconciled
     * with `Params` for the same reason - it is the record of a reprogram whose
     * target context is about to stop existing; the reopen that follows
     * describes the endpoint afresh and re-arms one if the device really has
     * moved to a different alternate setting.
     *
     * EP0 is deliberately **not** in this loop: Reset Device transitions the
     * Default Control Endpoint "to the Running state" (4.6.11 p.129) rather than
     * disabling it, and `xhciDevEp0Restored` is what records that.
     */
    for (i = 0; i < XHCI_MAX_DEVICE_ENDPOINTS; i++) {
        PXHCI_ENDPOINT_RECORD record = &dev->Endpoints[i];

        if (record->Dci == 0) {
            continue;
        }
        record->DropOnConfigure = 0;
        record->PendingParams = record->Params;
        record->Quiesce.Flags |= XHCI_EPQ_NO_CONTEXT;
        if (record->State != XHCI_EP_REC_PENDING) {
            /*
             * Counted only for a record that had, or was about to have, an
             * Endpoint Context - which is what the counter's name and its
             * documented reading against `EndpointsConfigured` mean. A `REFUSED`
             * or `FAILED` record is reconciled on the line below like every
             * other, and is *not* counted, because it may never have held a
             * context for the controller to drop.
             */
            if (record->State == XHCI_EP_REC_CONFIGURED ||
                record->State == XHCI_EP_REC_CONFIGURING) {
                ext->EndpointContextsDroppedByReset++;
            }
            record->State = XHCI_EP_REC_PENDING;
        }
    }

    xhciDevOweFromSlotState(ext, dev);
}

/*
 * An address-0 EP0 open on a **root port** this driver is entitled to claim:
 * task 6-B.2's record, moved into a function of its own by task 7b-A.3 so that
 * the two tiers read alike and neither can quietly acquire a step the other
 * lacks.
 *
 * Returns the record to bind, or NULL with the refusal already counted - which
 * has to be the callee's job, because only it knows which of its several
 * refusals happened.
 *
 * Called with the lock held. IRQL: DISPATCH_LEVEL.
 */
static PXHCI_DEVICE xhciDevOpenOnRootPort(PXHCI_EXTENSION ext, ULONG hubPort)
{
    const XHCI_PORT_SHADOW *shadow;
    PXHCI_DEVICE dev;

    /*
     * The claim is spent here rather than at the successful return, because
     * every refusal below has already consumed the reset's meaning: they are
     * all "this port cannot carry a device", and re-offering the same port to
     * the next open would only reproduce them.
     */
    ext->EnumClaimSpent = 1;
    shadow = &ext->RootHub.Ports[hubPort - 1];

    dev = xhciDevByHubPort(ext, hubPort);
    if (dev != NULL) {
        /*
         * **A record that already exists for this port, and an open at
         * address 0 means it has to go back to the beginning of the chain.**
         *
         * The stop-time review found this: the first draft rebound the
         * record and re-entered the chain only when it was FAILED, on the
         * reasoning that a retry is worth serving. But usbhub re-enumerates
         * a device by *resetting its port* and creating it again at address
         * 0 - and a reset returns the device to the Default state with no
         * address, whatever this driver's record says. An ADDRESSED record
         * left alone here would have had usbport submitting to a slot whose
         * device answers on a bus address it no longer has, with nothing
         * anywhere reporting it.
         *
         * The slot itself is kept when there is one, and **which command
         * re-enters the chain depends on the state that slot is in** -
         * `xhciDevReenterAtDefault` reads it and decides. What is dropped is
         * everything the reset invalidated - the address map entry and the
         * corrected Max Packet Size, since usbport will read the descriptor
         * again and address it again from the speed's default.
         */
        /* An open at address 0 on this port is usbport claiming the device
         * again, so a disown from an earlier disable is spent - otherwise
         * the record would refuse every transfer of the enumeration that is
         * re-establishing it. */
        dev->Flags &= ~(XHCI_DEV_FLAG_ADDRESS_VALID | XHCI_DEV_FLAG_DISOWNED);
        /*
         * **The graph's half of the reset, and it has to happen here** - above
         * the line that clears the address, because the graph is keyed on it.
         *
         * A reset returns every interface to alternate 0, so a hub that came
         * back is not running its multi-TT interface until something selects it
         * again. The node kept `XHCI_TOPO_F_MTT` across this branch, and the
         * re-enumeration clears `HubMarkDone`, so the pump would re-derive the
         * *previous tenancy's* MTT and program a multi-TT hub that the device
         * now on this port had never been asked to be. The node itself stays:
         * same port, same position. *(Round 11. This branch reset the
         * descriptor half of exactly this fact - `XhciDescStateReset` below -
         * and had never reset the graph's.)*
         */
        XhciTopoForgetAlternate(&ext->Topology, dev->DeviceAddress);
        dev->DeviceAddress = 0;
        dev->WantedMaxPacketSize0 = 0;
        /* Task 9-A.2: a configuration descriptor describes the device that
         * answered it, and this port is being enumerated again from the
         * beginning - possibly by a different device. The next enumeration
         * reads its own; until it does, every isochronous endpoint falls back
         * to the assumption, which is a lost reading and never a wrong one. */
        XhciDescStateReset(&dev->IsoDesc);
        /* ...and the record now holds a device that may not be the one it
         * held a moment ago, so an action armed under the old tenancy must
         * not land on this one (review round 2). */
        xhciDevNewTenancy(ext, dev);
        /*
         * The speed is re-read with the Max Packet Size below, not just under
         * it. A device can come back on the same root port at a different speed
         * - a marginal HS link falling back to FS on the second reset is the
         * ordinary way it happens - and MPS0 was being recomputed from the new
         * speed while the Slot Context PSIV and every speed-derived decision
         * (`xhciDevTtExpected`, the interval floors, the isochronous cadence)
         * kept the old one. That is an internally inconsistent Address Device
         * with nothing anywhere reporting it. The new-record branch below and
         * the behind-hub open both refresh the pair; this branch is the one
         * that did not.
         */
        dev->Speed = shadow->Speed;
        dev->Psiv = XHCI_PORTSC_GET_SPEED(shadow->Portsc);
        dev->MaxPacketSize0 = XhciInitialMps0(shadow->Speed);
        if (dev->MaxPacketSize0 == 0) {
            ext->OpenRefusals++;
            XHCI_DBG_VALUE_CHANGED("slot: re-enumeration at a speed this "
                                   "driver cannot address, hub port << 8 | "
                                   "speed", (hubPort << 8) | shadow->Speed);
            /*
             * Audit finding A9: the refusal used to return here having cleared
             * the address, the flags and the tenancy but leaving `State` and
             * `PendingOp` - an ADDRESSED record with no address, mps0 = 0 and
             * stale endpoint records, unreachable by every lookup and never
             * failed. Nothing submits to it, so it was bounded; it was still a
             * record in a state no path can describe.
             */
            xhciDevFailRecord(ext, dev, XHCI_DEV_OP_NONE);
            return NULL;
        }
        xhciDevReenterAtDefault(ext, dev);
        return dev;
    }

    dev = xhciDevAllocate(ext);
    if (dev == NULL) {
        ext->OpenRefusals++;
        ext->OpenRefusalsNoRecord++;
        return NULL;
    }

    dev->State = XHCI_DEV_STATE_RESERVED;
    dev->HubPort = hubPort;
    dev->RootPort = XhciRootHubPortOf(&ext->RootHub, hubPort);
    dev->Speed = shadow->Speed;
    dev->Psiv = XHCI_PORTSC_GET_SPEED(shadow->Portsc);
    /* Tier 0, no route, no parent: a device on a root port is where the path
     * *starts*, and 4.3.3 footnote 8 keeps the root port out of the Route
     * String. Written rather than left to the zeroed record, because these are
     * the fields task 7b-A.3 programs and "we left it alone" and "we decided it
     * is 0" are different claims. */
    dev->Tier = 0;
    dev->RouteString = 0;
    dev->ParentHubAddress = 0;
    dev->ParentHubPort = 0;
    /*
     * The initial Max Packet Size comes from the *decoded* speed class
     * rather than from what usbport says: at address 0 usbport has read
     * no descriptor and its properties carry a default. A speed this
     * driver does not address answers 0 and refuses here rather than
     * producing a context.
     */
    dev->MaxPacketSize0 = XhciInitialMps0(shadow->Speed);
    if (dev->RootPort == 0 || dev->MaxPacketSize0 == 0) {
        ext->OpenRefusals++;
        XHCI_DBG_VALUE_CHANGED("slot: EP0 open on a port this driver "
                               "cannot address, hub port << 8 | speed",
                               (hubPort << 8) | shadow->Speed);
        /* Allocated moments ago and never given a slot or a ring. */
        xhciDevRelease(ext, dev, 1);
        return NULL;
    }
    dev->PendingOp = XHCI_DEV_OP_ENABLE_SLOT;
    return dev;
}

/*
 * The same, one or more tiers down: **task 7b-A.3's whole point.**
 *
 * The position comes from the claim the topology graph handed over - the hub
 * whose downstream port was reset, and where that hub sits - and everything
 * this driver has to tell the xHC about the device follows from it: the Route
 * String, the root port the path starts at, and (through
 * `xhciDevFillSlotPosition`, later, when the command is built) the transaction
 * translator.
 *
 * Three things are *not* taken from the port shadow, because there is no port
 * shadow one tier down - usbhub owns that port and this driver only sees the
 * traffic:
 *
 *   - the **speed** is usbport's `DeviceSpeed`, which for a behind-hub device
 *     is the device's real speed rather than Phase 5 task 7's root-port
 *     untruth (batch 7b-V0 measured `UsbFullSpeed` for the child of a hub the
 *     same run measured as `UsbHighSpeed`);
 *   - the **Protocol Speed ID** is looked up from that class against the root
 *     port's protocol group, since the Slot Context wants the controller's own
 *     ID and not a class (`XhciPortPsivForSpeed`);
 *   - the **Max Packet Size** is the speed's default, exactly as on a root
 *     port.
 *
 * Returns the record to bind, or NULL with the refusal counted.
 * Called with the lock held. IRQL: DISPATCH_LEVEL.
 */
static PXHCI_DEVICE xhciDevOpenBehindHub(
    PXHCI_EXTENSION ext,
    const USBPORT_ENDPOINT_PROPERTIES *properties,
    const XHCI_TOPO_CHILD *at)
{
    PXHCI_DEVICE dev;
    XHCI_TOPO_TT tt;
    ULONG speed;
    ULONG psiv;
    ULONG mps0;
    ULONG claimAddress;

    if (at->TooDeep) {
        /*
         * **Refused, never truncated** - task 7b-A.3's own clause, and the
         * reason is not conservatism: the Route String holds five 4-bit tiers,
         * so a sixth tier's port would either be dropped or wrap onto an
         * existing nibble, and either way the string names a *different, real*
         * device on the same bus. The device is a yellow bang; nothing else on
         * the bus is disturbed.
         */
        ext->OpenRefusals++;
        ext->BehindHubTooDeep++;
        XHCI_DBG_VALUE_CHANGED("slot: a device deeper than the Route String "
                               "can express, tier", at->Tier);
        return NULL;
    }

    speed = xhciDevSpeedFromProperties(properties);
    mps0 = XhciInitialMps0(speed);
    if (mps0 == 0 || ext->PortMapStatus != XHCI_CAPS_OK ||
        XhciPortPsivForSpeed(&ext->PortMap, at->RootPort, speed, &psiv) !=
            XHCI_CAPS_OK) {
        /*
         * A speed this driver cannot describe on this controller: either one it
         * does not address at all, or one the port's protocol group advertises
         * no Protocol Speed ID for. Refusing beats inventing an ID - the value
         * goes into the Slot Context's Speed field, which is what tells the xHC
         * how to talk to the device.
         */
        ext->OpenRefusals++;
        ext->BehindHubNoSpeed++;
        XHCI_DBG_VALUE_CHANGED("slot: a behind-hub device at a speed this "
                               "controller does not name, speed", speed);
        return NULL;
    }

    dev = xhciDevByHubPath(ext, at->RootPort, at->HubAddress, at->HubPort);
    if (dev != NULL) {
        /*
         * A re-enumeration one tier down, and the same rule as on a root port:
         * usbhub resets the hub's downstream port and creates the device again
         * at address 0, so whatever the record says, the device is back in the
         * Default state with no address. The suppression allowance travels
         * with the bracket: this open is the claim spend that starts one, so
         * the one hub-tier reset suppression it is allowed re-arms here
         * (finding A6).
         */
        dev->Flags &= ~(XHCI_DEV_FLAG_ADDRESS_VALID | XHCI_DEV_FLAG_DISOWNED |
                        XHCI_DEV_FLAG_HUB_RESET_SUPPRESSED);
        /* The graph's half of the reset, one tier down and for the same reason
         * as the root-port branch: a reset returns every interface to alternate
         * 0, so a hub *behind* a hub must not keep an MTT its new occupant has
         * never been asked for. Above the address clear, because the graph is
         * keyed on it. *(Round 12. Round 11 added this to the root-port branch
         * and missed the branch beside it - the two reset the same state and
         * only one of them said so.)* */
        XhciTopoForgetAlternate(&ext->Topology, dev->DeviceAddress);
        dev->DeviceAddress = 0;
        dev->WantedMaxPacketSize0 = 0;
        /* Task 9-A.2, one tier down and for the same reason. */
        XhciDescStateReset(&dev->IsoDesc);
        /* ...and the record now holds a device that may not be the one it
         * held a moment ago, so an action armed under the old tenancy must
         * not land on this one (review round 2). */
        xhciDevNewTenancy(ext, dev);
    } else {
        dev = xhciDevAllocate(ext);
        if (dev == NULL) {
            ext->OpenRefusals++;
            ext->OpenRefusalsNoRecord++;
            ext->BehindHubNoRecord++;
            return NULL;
        }
        dev->State = XHCI_DEV_STATE_RESERVED;
    }

    /*
     * `HubPort` stays 0. A behind-hub record must never answer
     * `xhciDevByHubPort`: that lookup is how a connect change, a port disable
     * and a disown find "the device on this root port", and here that device is
     * the hub. What ties this record to the root port is `RootPort`, which the
     * subtree teardowns use instead.
     */
    dev->HubPort = 0;
    dev->RootPort = at->RootPort;
    dev->Tier = at->Tier;
    dev->RouteString = at->Route;
    dev->ParentHubAddress = at->HubAddress;
    dev->ParentHubPort = at->HubPort;
    dev->Speed = speed;
    dev->Psiv = psiv;
    dev->MaxPacketSize0 = mps0;

    /*
     * usbport's own answer about the transaction translator, kept beside the
     * graph's so a run can compare them (task 7b-A.1.2's measurement, which
     * needed exactly this: a device that enumerates one tier down). `0xFFFF` is
     * usbport's "no TT" and is normalised to 0 here so that the stored value is
     * a hub address or nothing - the test batch 7a-0 says to branch on, made
     * once at the only point the properties exist.
     */
    claimAddress = (ULONG)properties->HubAddr;
    dev->TtClaimAddress = (claimAddress == USBPORT_NO_TT_HUB) ? 0 : claimAddress;
    dev->TtClaimPort = (ULONG)properties->PortNumber;
    if (xhciDevTtExpected(ext, dev, &tt)) {
        if (dev->TtClaimAddress == tt.HubAddress &&
            dev->TtClaimPort == tt.HubPort) {
            ext->TtPairsAgreed++;
        } else {
            ext->TtPairsDisagreed++;
        }
    } else if (dev->TtClaimAddress == 0) {
        ext->TtPairsAgreed++;
    } else {
        /*
         * The measured QEMU case: usbport names a Full-Speed `usb-hub` as a
         * transaction translator it does not have. The graph wins - see
         * `xhciDevTtExpected` - and this counter is what makes the disagreement
         * a reading rather than a silence.
         */
        ext->TtPairsDisagreed++;
    }

    /* The same three-way re-entry as on a root port, and through the same
     * function for the reason task 7b-A.3 put the two openers side by side: a
     * step one tier acquires and the other does not is how they drift. */
    xhciDevReenterAtDefault(ext, dev);
    ext->BehindHubOpens++;
    XHCI_DBG_VALUE_CHANGED("slot: EP0 open behind a hub, tier << 24 | route",
                           (at->Tier << 24) | at->Route);
    return dev;
}

/* The default control endpoint: tasks 6-B.2 and 6-B.4, unchanged by 7a-A.1
 * except for being reached through a dispatcher.
 * IRQL: DISPATCH_LEVEL, under MiniportSpinLock, controller lock not held. */
static MPSTATUS xhciSlotOpenControl(
    PXHCI_EXTENSION ext,
    const USBPORT_ENDPOINT_PROPERTIES *properties,
    PXHCI_ENDPOINT endpoint)
{
    KIRQL oldIrql;
    PXHCI_DEVICE dev;
    ULONG hubPort;
    ULONG mps;

    mps = xhciDevPropertiesMps(properties);

    XhciControllerLockAcquire(&oldIrql);

    if (!xhciDevAdmitted(ext)) {
        ext->OpenRefusals++;
        XhciControllerLockRelease(oldIrql);
        return MP_STATUS_NO_RESOURCES;
    }

    if (properties->DeviceAddress != 0) {
        /*
         * Task 6-B.4. usbport has torn EP0's private state down and rebuilt it,
         * and it is telling us the device's real address for the first time -
         * the address map is what resolves it back to the slot that already
         * exists, which is the whole reason the map is keyed on usbport's
         * address and written by the SET_ADDRESS interception.
         */
        dev = xhciDevByAddress(ext, properties->DeviceAddress);
        if (dev == NULL) {
            /*
             * An addressed device this driver has no record of. It is not a
             * device it can serve: the slot, the device context and the ring the
             * xHC would need are all things only the addressing chain creates.
             */
            ext->OpenRefusals++;
            XHCI_DBG_VALUE_CHANGED("slot: EP0 open for an address no record "
                                   "holds", properties->DeviceAddress);
            XhciControllerLockRelease(oldIrql);
            return MP_STATUS_NO_RESOURCES;
        }

        ext->DevicesReopened++;
        /*
         * The ring is **not** reinitialised. The xHC's TR Dequeue Pointer still
         * points into it and Evaluate Context does not update that field
         * (spec 6.2.3.2 evaluates Max Packet Size and Max Exit Latency only), so
         * a fresh ring would leave the two pointing at different laps of the
         * same memory. This is what design doc 04 section 3.4 put EP0's ring in
         * the controller buffer for.
         */
        if (mps != dev->MaxPacketSize0) {
            if (!XHCI_EP0_MPS_IS_LEGAL(mps)) {
                /* A device descriptor that named an impossible bMaxPacketSize0.
                 * Refusing here beats programming it: the value ends up in the
                 * TD Size arithmetic of every later control transfer. */
                ext->OpenRefusals++;
                XHCI_DBG_VALUE_CHANGED("slot: EP0 reopen with an illegal max "
                                       "packet size", mps);
                XhciControllerLockRelease(oldIrql);
                return MP_STATUS_NO_RESOURCES;
            }
            dev->WantedMaxPacketSize0 = mps;
            dev->PendingOp = XHCI_DEV_OP_EVALUATE_MPS;
        }
    } else {
        /* Task 6-B.2: a device that has not been addressed yet. */
        hubPort = xhciDevEnumeratingPort(ext);
        if (hubPort != 0) {
            dev = xhciDevOpenOnRootPort(ext, hubPort);
        } else {
            XHCI_TOPO_CHILD at;

            /*
             * Task 7b-A.1: ask the graph where this device is. A snooped
             * SET_FEATURE(PORT_RESET) on an external hub is the entitlement
             * here, exactly as this driver's own root-port reset is the
             * entitlement above - and it is *spent* by the ask, so a second
             * open cannot ride one reset (the 7b-A.0 hijack shape, closed at
             * this tier by construction).
             *
             * **Task 7b-A.3 is what consumes it.** The two entitlements are
             * never both armed - a root-port reset drops a pending hub claim
             * and a hub-port reset spends the root-port one - so which of the
             * two branches runs is not a precedence decision made here.
             * Whichever reset came last is the only one standing, which is what
             * design doc 02 section 2's measured serialization means in code.
             */
            if (XhciTopoClaimChild(&ext->Topology, &at)) {
                dev = xhciDevOpenBehindHub(ext, properties, &at);
            } else {
                dev = NULL;
                ext->OpenRefusals++;
                if (ext->EnumClaimSpent) {
                    /*
                     * The reset that would have entitled this open to a root
                     * port has already been spent by another one, and no hub
                     * claimed the device either - so this is a device usbport
                     * placed somewhere this driver cannot name: behind a hub
                     * whose port reset was never seen, or behind one whose own
                     * enumeration never finished. Counted apart because it is a
                     * topology statement rather than a resource one - "the port
                     * map is fine, the *route* is missing".
                     */
                    ext->OpenRefusalsNoClaim++;
                }
                XHCI_DBG_VALUE_CHANGED("slot: EP0 open at address 0 with no "
                                       "port to attribute it to, last reset "
                                       "port", ext->EnumHubPort);
            }
        }
        if (dev == NULL) {
            /* Every path above counted its own refusal - the two openers own
             * that, because only they know which of their several refusals it
             * was. */
            XhciControllerLockRelease(oldIrql);
            return MP_STATUS_NO_RESOURCES;
        }
    }

    dev->EndpointExtension = endpoint;
    dev->Flags |= XHCI_DEV_FLAG_EP0_OPEN;

    endpoint->Signature = XHCI_ENDPOINT_SIGNATURE;
    endpoint->DeviceIndex = xhciDevRef(ext, dev);
    endpoint->SlotId = dev->SlotId;
    endpoint->Dci = 1;
    endpoint->Flags = XHCI_ENDPOINT_FLAG_OPEN;

    XhciControllerLockRelease(oldIrql);

    /*
     * Success **before** the slot exists, deliberately. This callback runs at
     * DISPATCH_LEVEL and may not wait, and there is no state channel to fail
     * into afterwards: batch 6-0 established that `GetEndpointState` is never
     * called, so a "failed" state only the miniport can see is a state nobody
     * reads. Everything that can go wrong from here is reported through the
     * transfer path instead - refused for retry while the chain runs, completed
     * with an error once it has failed.
     */
    XhciSlotDeferredWork(ext);
    return MP_STATUS_SUCCESS;
}

/*
 * Task 9-A.2's verdict, counted at the one place an isochronous endpoint's
 * Interval is decided.
 *
 * Every other transfer type returns immediately: the descriptor table holds
 * isochronous endpoints only, so a `bInterval` of 0 on an interrupt pipe means
 * "not the kind of endpoint this task is about" rather than "no descriptor was
 * read", and counting it as assumed would bury the reading under every HID
 * device on the bus.
 *
 * `built` says whether the context builder accepted the endpoint at all. A
 * refusal with a descriptor reading in hand gets `DescIntervalsRefused`, which
 * is deliberately a *weak* statement - the conversion is only one of the
 * reasons that call can fail - and is read beside `EndpointRefusalsParams`
 * rather than alone. It should be 0: the walk records only `bInterval` 1..16 and
 * both isochronous speeds accept all of it, so the only reachable cause is an
 * isochronous endpoint on a Low-Speed device, which USB 2.0 section 5.6 says
 * cannot exist.
 *
 * Called with the lock held. IRQL: DISPATCH_LEVEL.
 */
static VOID xhciDevCountIsoInterval(PXHCI_EXTENSION ext,
                                    ULONG transferType,
                                    ULONG deviceSpeed,
                                    ULONG bInterval,
                                    ULONG built,
                                    ULONG derived,
                                    ULONG interval)
{
    ULONG assumed;

    if (transferType != USBPORT_TRANSFER_TYPE_ISOCHRONOUS) {
        return;
    }
    if (!built) {
        if (bInterval != 0) {
            ext->DescIntervalsRefused++;
        }
        return;
    }
    if (!derived) {
        ext->DescIntervalsAssumed++;
        return;
    }

    ext->DescIntervalsDerived++;
    /*
     * **Whether the derivation changed anything is the question this counter
     * answers - for the endpoints that got this far.** Only a built, derived
     * isochronous endpoint reaches this line (see the two returns above), so
     * `Agreed == Derived` means every such endpoint declared the assumed
     * cadence; a device that never bound or never opened the endpoint is not
     * in the comparison (the Sound Blaster X4 declares `bInterval` 3 and 4
     * and never opened one - run-13e.md, Finding Y). The assumed value is read
     * from the same two constants the builder falls back to, so the comparison
     * cannot drift away from what it means. *(Until a later review this comment
     * spoke of "every audio device this project can reach".)*
     */
    assumed = (deviceSpeed == XHCI_SPEED_HIGH) ? XHCI_EP_INTERVAL_ISOCH_HS
                                               : XHCI_EP_INTERVAL_ISOCH_FS;
    if (interval == assumed) {
        ext->DescIntervalsAgreed++;
    }
    XHCI_DBG_VALUE_CHANGED("slot: isochronous interval from a descriptor, "
                           "bInterval << 8 | interval",
                           (bInterval << 8) | interval);
}

/*
 * Task 7a-A.1: a non-default endpoint.
 *
 * The shape is the EP0 open's inverted. There, usbport names a device this
 * driver has to *find a port for*; here it names a device by an address the
 * SET_ADDRESS interception put in the map, so the lookup is exact and a miss is
 * a refusal rather than a guess.
 *
 * **Success is returned before the Configure Endpoint has been issued**, for the
 * same reason task 6-B.2 does it: this callback runs at DISPATCH_LEVEL and may
 * not wait, and batch 6-0 established there is no state channel to fail into
 * afterwards - `GetEndpointState` is never called by either shipping build. What
 * the xHC decides is reported through the transfer path instead, which is why
 * a REFUSED record is kept rather than torn down.
 *
 * IRQL: DISPATCH_LEVEL, under MiniportSpinLock, controller lock not held.
 */
static MPSTATUS xhciSlotOpenNonDefault(
    PXHCI_EXTENSION ext,
    const USBPORT_ENDPOINT_PROPERTIES *properties,
    PXHCI_ENDPOINT endpoint)
{
    XHCI_EP_PARAMS params;
    KIRQL oldIrql;
    PXHCI_DEVICE dev;
    PXHCI_ENDPOINT_RECORD record;
    ULONG dci;
    ULONG speed;
    ULONG directionIn;
    ULONG poolIndex;
    ULONG ringOffset;
    ULONG status;
    ULONG floored;
    ULONG bInterval;
    ULONG derived;
    ULONG reprogramming;

    floored = 0;
    bInterval = 0;
    derived = 0;
    reprogramming = 0;
    dci = XhciDciFromEndpointAddress(properties->EndpointAddress);
    directionIn = (properties->EndpointAddress & 0x80U) ? 1UL : 0UL;
    speed = xhciDevSpeedFromProperties(properties);

    XhciControllerLockAcquire(&oldIrql);

    /*
     * A DCI outside 2..31 is not an endpoint this driver can have a context
     * for. 1 cannot occur here - `xhciDevPropertiesAreNonDefault` has already
     * required a nonzero endpoint number - and it is checked anyway, because the
     * cost of being wrong is a Configure Endpoint that reprograms EP0.
     */
    if (dci < 2 || dci > XHCI_MAX_DCI) {
        ext->EndpointRefusalsType++;
        XHCI_DBG_VALUE_CHANGED("slot: endpoint open with an unusable DCI, "
                               "address << 8 | dci",
                               ((ULONG)properties->EndpointAddress << 8) | dci);
        XhciControllerLockRelease(oldIrql);
        return MP_STATUS_NOT_SUPPORTED;
    }

    dev = xhciDevByAddress(ext, properties->DeviceAddress);
    if (dev == NULL || !xhciDevMayOpenEndpoint(dev)) {
        /*
         * Permanent from here: the slot, the device context and the address map
         * entry an endpoint needs are all things only the addressing chain
         * creates, and it does not run again for this open.
         */
        ext->EndpointRefusalsNoDevice++;
        XHCI_DBG_VALUE_CHANGED("slot: endpoint open for an address no record "
                               "holds", properties->DeviceAddress);
        XhciControllerLockRelease(oldIrql);
        return MP_STATUS_NO_RESOURCES;
    }
    if (!xhciDevAdmitted(ext) || dev->State != XHCI_DEV_STATE_ADDRESSED) {
        /*
         * Transient, and the two halves are both genuinely so: a controller
         * mid-resume comes back, and a device still finishing its EP0 chain
         * reaches ADDRESSED. usbport retries a nonzero return.
         */
        ext->EndpointRefusalsNotReady++;
        XhciControllerLockRelease(oldIrql);
        return MP_STATUS_NO_RESOURCES;
    }
    if (speed != dev->Speed) {
        ext->EndpointSpeedMismatches++;
        XHCI_DBG_VALUE_CHANGED("slot: endpoint speed differs from the port's, "
                               "usbport << 8 | decoded",
                               (speed << 8) | dev->Speed);
    }

    /*
     * Task 9-A.2's channel, asked once for both open paths below. It answers 0
     * for every endpoint that is not isochronous - the table holds nothing else
     * - and for an isochronous one whose configuration descriptor was never
     * read or never completed; the context builder's own assumption is the
     * fallback in both, which is what this driver did before this task.
     *
     * **A third case is counted apart**: the device declared this endpoint and
     * the declarations could not be resolved to one cadence. That is the only
     * shape where falling back is known to be *answering the question wrongly*
     * rather than not answering it, so it may not disappear into
     * `DescIntervalsAssumed`.
     */
    if (!XhciDescIsoInterval(&dev->IsoDesc, properties->EndpointAddress,
                             &bInterval)) {
        bInterval = 0;
        if (properties->TransferType == USBPORT_TRANSFER_TYPE_ISOCHRONOUS &&
            XhciDescIsoDeclared(&dev->IsoDesc, properties->EndpointAddress)) {
            ext->DescIntervalsUnresolved++;
            XHCI_DBG_VALUE_CHANGED("slot: a declared isochronous endpoint whose "
                                   "alternate settings disagree, address",
                                   properties->EndpointAddress);
        }
    }

    record = xhciEpByDci(dev, dci);
    if (record != NULL) {
        /*
         * The DCI is already open. usbport reaches here through `ReopenPipe`,
         * which tears its *own* endpoint state down and rebuilds it - the same
         * sequence task 6-B.4 handles for EP0 - so the ring, the record and the
         * xHC's Endpoint Context all survive and only the binding is renewed.
         *
         * Rebuilding the ring would be the defect this arrangement exists to
         * avoid: the xHC's TR Dequeue Pointer still points into it and no
         * command here updates that field.
         */
        status = XhciBuildEndpointParams(properties->TransferType, directionIn,
                                         properties->MaxPacketSize,
                                         properties->Period, speed, dev->Speed,
                                         properties->TransactionPerMicroframe,
                                         bInterval,
                                         XhciRingDequeuePA(&record->Ring),
                                         XhciRingDequeueCycle(&record->Ring),
                                         &params, &floored, &derived);
        if (status != XHCI_CTX_OK) {
            xhciDevCountIsoInterval(ext, properties->TransferType,
                                    dev->Speed, bInterval, 0UL, 0UL, 0UL);
            ext->EndpointRefusalsParams++;
            XhciControllerLockRelease(oldIrql);
            return MP_STATUS_NO_RESOURCES;
        }
        xhciDevCountIsoInterval(ext, properties->TransferType, dev->Speed,
                                bInterval, 1UL, derived, params.Interval);
        /*
         * Compared against what the endpoint is *becoming*, not only what it
         * is (Phase 7 review, B2). `PendingParams` differs from `Params`
         * exactly while a reprogram is outstanding - the commit is the only
         * writer that makes them equal again - and a reopen judged against
         * `Params` alone reads a revert to the original setting as "same
         * endpoint": the armed reconfigure then proceeds and commits the
         * alternate the device has already left. Re-targeting `PendingParams`
         * is the cancellation - the Drop+Add commits what usbport currently
         * describes, whichever direction the last reopen moved.
         */
        reprogramming = xhciEpParamsSame(&record->PendingParams,
                                         &record->Params) ? 0UL : 1UL;
        if (!xhciEpParamsSame(&params, reprogramming ? &record->PendingParams
                                                     : &record->Params)) {
            XHCI_EP_BINDING binding;

            /*
             * **A different endpoint at the same DCI** - an alternate-interface
             * or configuration change, and task 7a-B.1's own clause.
             *
             * It is not programmed here and it is not a second Add either. "xHC
             * behavior is undefined if the Drop Context (D) flag is '0', the Add
             * Context (A) flag is '1', and the Output Endpoint Context is not in
             * the Disabled state" (4.6.6 p.106), so the new parameters need a
             * Drop beside the Add - and the endpoint has to be quiet first: "An
             * endpoint shall be in the Stopped state or if in the Running state
             * shall be 'idle' ... if its Drop Context flag is set. If this
             * condition is not met undefined behavior may occur" (p.104).
             *
             * So the record takes the new parameters, and the stop is what
             * carries it the rest of the way: `XHCI_EPQ_RECONFIGURE` rebuilds
             * the ring from its base once the endpoint is provably stopped and
             * re-arms the record for a Configure Endpoint with `DropOnConfigure`
             * set. Nothing here touches the ring, because until that stop
             * completes the xHC's TR Dequeue Pointer still points into it.
             */
            /*
             * Into `PendingParams`, not `Params`: a Configure Endpoint the xHC
             * refuses changes nothing on its side (4.6.6 p.106), so overwriting
             * the record here would leave this driver describing an endpoint the
             * controller does not have, with the old description gone.
             */
            record->PendingParams = params;
            if (floored) {
                ext->EndpointIntervalsFloored++;
            }
            if (xhciEpResolve(dev, dci, &binding)) {
                xhciEpArmQuiesce(ext, dev, &binding,
                                 XHCI_EPQ_DRAIN | XHCI_EPQ_RECONFIGURE);
            }
            XHCI_DBG_VALUE_CHANGED("slot: reopen with different parameters, "
                                   "reprogramming dci", dci);
        }
        record->EndpointExtension = endpoint;
        ext->EndpointsReopened++;
    } else {
        record = xhciEpFree(dev);
        if (record == NULL) {
            /* More endpoints than XHCI_MAX_DEVICE_ENDPOINTS - the declared
             * per-device limit, refused rather than silently under-served. */
            ext->EndpointRefusalsPool++;
            XHCI_DBG_VALUE_CHANGED("slot: device is at its endpoint cap, dci",
                                   dci);
            XhciControllerLockRelease(oldIrql);
            return MP_STATUS_NO_RESOURCES;
        }

        status = XhciPoolAcquire(&ext->RingPool, xhciDevRef(ext, dev),
                                 xhciDevRingless(ext, dev), &poolIndex);
        if (status != XHCI_POOL_OK) {
            /* The pool counts its own three causes apart; this is the one
             * reading that says "the driver ran out" rather than "the device
             * asked for something it cannot have". */
            ext->EndpointRefusalsPool++;
            XHCI_DBG_VALUE_CHANGED("slot: no pool ring for a new endpoint, "
                                   "pool status", status);
            XhciControllerLockRelease(oldIrql);
            return MP_STATUS_NO_RESOURCES;
        }

        if (XhciPoolRingOffset(&ext->Layout, poolIndex, &ringOffset) !=
                XHCI_LAYOUT_OK ||
            XhciRingInit(&record->Ring,
                         (volatile XHCI_TRB *)XhciCommonAt(ext, ringOffset),
                         XhciCommonPA(ext, ringOffset),
                         ext->Layout.PoolRingTrbs,
                         xhciDevRingKindFor(properties->TransferType)) !=
                XHCI_RING_OK) {
            (VOID)XhciPoolRelease(&ext->RingPool, xhciDevRef(ext, dev),
                                  poolIndex);
            ext->EndpointRefusalsPool++;
            XHCI_DBG_VALUE_CHANGED("slot: pool ring would not initialise, "
                                   "index", poolIndex);
            XhciControllerLockRelease(oldIrql);
            return MP_STATUS_NO_RESOURCES;
        }

        status = XhciBuildEndpointParams(properties->TransferType, directionIn,
                                         properties->MaxPacketSize,
                                         properties->Period, speed, dev->Speed,
                                         properties->TransactionPerMicroframe,
                                         bInterval,
                                         XhciRingDequeuePA(&record->Ring),
                                         XhciRingDequeueCycle(&record->Ring),
                                         &params, &floored, &derived);
        if (status != XHCI_CTX_OK) {
            /*
             * usbport described an endpoint this driver will not encode - a
             * Period outside the derived contract, a transaction count no USB
             * 2.0 endpoint has, a zero Max Packet Size. The ring goes straight
             * back: nothing has been told about it, so this is the one place a
             * pool entry can be reclaimed with no argument about what the xHC
             * might still be doing with it.
             */
            (VOID)XhciPoolRelease(&ext->RingPool, xhciDevRef(ext, dev),
                                  poolIndex);
            record->Ring.Base = NULL;
            xhciDevCountIsoInterval(ext, properties->TransferType,
                                    dev->Speed, bInterval, 0UL, 0UL, 0UL);
            ext->EndpointRefusalsParams++;
            XHCI_DBG_VALUE_CHANGED("slot: endpoint properties refused by the "
                                   "context builder, dci << 8 | period",
                                   (dci << 8) | properties->Period);
            XhciControllerLockRelease(oldIrql);
            return MP_STATUS_NO_RESOURCES;
        }
        xhciDevCountIsoInterval(ext, properties->TransferType, dev->Speed,
                                bInterval, 1UL, derived, params.Interval);

        if (floored) {
            /*
             * The bucketed `Period` landed under the 1 ms floor Table 6-12 gives
             * this endpoint's real speed, because usbport bucketed it against a
             * speed this driver misreports (Phase 5 task 7). What that says is
             * that the context would have been *illegal* - not that the endpoint
             * asked for something impossible, since usbport's own bucketing
             * discarded its request first. Counted rather than logged
             * only, because the alternative reading - a Configure Endpoint
             * answered with Parameter Error - is what the untranslated interval
             * would have produced, and the two must be tellable apart on a
             * target.
             */
            ext->EndpointIntervalsFloored++;
            XHCI_DBG_VALUE_CHANGED("slot: interval raised to the speed's floor, "
                                   "dci << 8 | period",
                                   (dci << 8) | properties->Period);
        }

        /* Same reason as the EP0 queue's: usbport reopens a pipe on every
         * alternate-setting change, and the reading the previous tenancy took
         * is exactly the one worth keeping. */
        xhciDevFoldQueue(ext, &record->Queue);
        XhciXferQueueInit(&record->Queue);
        record->PoolIndex = poolIndex;
        record->Params = params;
        /*
         * Kept equal to `Params` except while a reprogram is outstanding -
         * that is the invariant the reset-pipe recovery and the reopen's
         * effective-target comparison read (Phase 7 review, B1/B2), and the
         * fresh open is where it has to start being true: a zeroed
         * PendingParams would read as a reprogram that was never asked for.
         */
        record->PendingParams = params;
        record->EndpointExtension = endpoint;
        record->State = XHCI_EP_REC_PENDING;
        /* Written **last**: `Dci` is what makes the record findable, and the
         * lookups that find it read every other field. */
        record->Dci = dci;
        ext->EndpointsOpened++;
        /*
         * Task 11-V.9's second tier. Three fields, because an endpoint that
         * does not work is diagnosed from exactly these: its DCI says which
         * endpoint and which direction, its type says what the driver made of
         * usbport's request, and the interval and max packet are the two the
         * xHC will refuse a Configure Endpoint over. Two records rather than
         * one because four values do not fit a record's single ULONG, and
         * splitting them is cheaper than a wider record format every other
         * producer would then carry.
         */
        XhciLogNoteLocked(ext, "ep.open",
                          ((ULONG)dev->SlotId << 16) | (dci << 8) |
                              (ULONG)properties->TransferType);
        XhciLogNoteLocked(ext, "ep.open.rate",
                          ((ULONG)properties->Period << 16) |
                              (ULONG)properties->MaxPacketSize);
    }

    endpoint->Signature = XHCI_ENDPOINT_SIGNATURE;
    endpoint->DeviceIndex = xhciDevRef(ext, dev);
    endpoint->SlotId = dev->SlotId;
    endpoint->Dci = dci;
    endpoint->Flags = XHCI_ENDPOINT_FLAG_OPEN;

    XhciControllerLockRelease(oldIrql);

    XhciSlotDeferredWork(ext);
    return MP_STATUS_SUCCESS;
}

/*
 * **Every way out of this function is counted, and the total is counted too.**
 *
 * Task 7b-A.1.0. The two refusals below used to answer usbport with nothing
 * recorded, and the batch 7a-V 2a run is what that cost: a behind-hub child's
 * EP0 open was certainly refused - the probe counted the open, no refusal
 * counter moved - and the run could establish only that an open had gone
 * somewhere unaccounted. Giving those two paths a counter each is the obvious
 * half; the half that matters is `OpensTotal`, because a counter set assembled
 * one path at a time is complete only until the next path is added. See the
 * identity on `OpensTotal` in XHCI_EXTENSION for what it buys and what enforces
 * it.
 *
 * The counter writes here are outside the controller lock, as the type refusal
 * below already was. That is not laxity: usbport serializes its endpoint
 * callbacks under `MiniportSpinLock`, so two opens cannot race, and nothing at
 * DIRQL touches any of these fields.
 *
 * IRQL: DISPATCH_LEVEL, under MiniportSpinLock, controller lock not held.
 */
MPSTATUS XhciSlotOpenEndpoint(PXHCI_EXTENSION ext,
                              const USBPORT_ENDPOINT_PROPERTIES *properties,
                              PXHCI_ENDPOINT endpoint)
{
    MPSTATUS status;

    if (ext == NULL) {
        /* The one return with no counter, because there is nowhere to put one.
         * The dispatch wrapper's `xhciExtensionValid` bracket means usbport
         * cannot reach it. */
        return MP_STATUS_NOT_SUPPORTED;
    }

    ext->OpensTotal++;

    if (endpoint == NULL || properties == NULL) {
        ext->OpenRefusalsMalformed++;
        return MP_STATUS_NOT_SUPPORTED;
    }
    /* usbport's own per-endpoint common buffer, which this driver asks none of
     * and therefore expects none of - checked for both kinds of endpoint,
     * because a buffer arriving unasked is a disagreement about the request that
     * was answered rather than a spare allocation. */
    if (!xhciDevBufferUsable(properties)) {
        ext->OpenRefusalsBuffer++;
        XHCI_DBG_VALUE_CHANGED("slot: endpoint buffer this driver cannot put a "
                               "ring in, length", properties->BufferLength);
        return MP_STATUS_NO_RESOURCES;
    }

    if (xhciDevPropertiesAreEp0(properties)) {
        status = xhciSlotOpenControl(ext, properties, endpoint);
    } else if (xhciDevPropertiesAreNonDefault(properties)) {
        status = xhciSlotOpenNonDefault(ext, properties, endpoint);
    } else {
        /*
         * A non-default control endpoint, or a type this driver does not
         * serve. Counted rather than merely refused: this is the reading that
         * says a device failed to start because of what this driver admits and
         * not because of a defect. Bulk left this branch in task 8-A.1 and
         * isochronous in task 9-A.1, so a nonzero reading now names an
         * endpoint no phase has taken on.
         */
        ext->EndpointRefusalsType++;
        XHCI_DBG_VALUE_CHANGED("slot: endpoint type not served by this phase, "
                               "type << 8 | address",
                               ((ULONG)properties->TransferType << 8) |
                                   (ULONG)properties->EndpointAddress);
        return MP_STATUS_NOT_SUPPORTED;
    }

    if (status == MP_STATUS_SUCCESS) {
        ext->OpensAccepted++;
    }
    return status;
}

/* ------------------------------------------------------------------ */
/* Task 6-B.1 / 6-B.5: SetEndpointState                                */
/* ------------------------------------------------------------------ */

/* IRQL: DISPATCH_LEVEL, under MiniportSpinLock, controller lock not held. */
VOID XhciSlotSetEndpointState(PXHCI_EXTENSION ext,
                              PXHCI_ENDPOINT endpoint,
                              ULONG state)
{
    KIRQL oldIrql;
    PXHCI_DEVICE dev;

    if (ext == NULL || ext->Signature != XHCI_EXTENSION_SIGNATURE ||
        endpoint == NULL || endpoint->Signature != XHCI_ENDPOINT_SIGNATURE) {
        return;
    }
    /*
     * **PAUSED is the cancellation's advance warning, and batch 7a-B is what
     * uses it.** usbport only ever reaches PAUSED because
     * `USBPORT_DmaEndpointActive` met a transfer already flagged
     * `CANCELED|ABORTED` (SP4 `0x165ED` -> `0x16729`), and `AbortTransfer`
     * follows on a later worker pass, once usbport's own frame gate has moved.
     * Starting the Stop Endpoint here rather than there is what shrinks the
     * window in which the xHC can still execute the aborted TD's TRBs after
     * usbport has unmapped their buffers - a window this driver cannot close
     * outright, because `AbortTransfer` runs at DISPATCH under a usbport spin
     * lock and may not wait for a command.
     *
     * The stop asks for no drain: the transfers that survive the abort are
     * usbport's and must keep running. A round that turns out to cancel nothing
     * is transparent - the placement lands on the oldest surviving TD and the
     * doorbell restarts it.
     *
     * ACTIVE is usbport saying it may submit again, which this driver decides
     * from the endpoint's own state instead; CLOSED is never sent by either
     * shipping build, and neither is a call to GetEndpointState to read any of
     * them back (batch 6-0).
     */
    if (state == USBPORT_ENDPOINT_PAUSED || state == USBPORT_ENDPOINT_ACTIVE) {
        XHCI_EP_BINDING binding;

        XhciControllerLockAcquire(&oldIrql);
        dev = xhciDevFromRef(ext, endpoint->DeviceIndex);
        if (dev != NULL && dev->State != XHCI_DEV_STATE_FREE &&
            xhciEpResolve(dev, endpoint->Dci, &binding)) {
            if (state == USBPORT_ENDPOINT_PAUSED) {
                binding.Quiesce->Flags |= XHCI_EPQ_PAUSED;
                xhciEpArmIfBusy(ext, dev, &binding, 0);
            } else {
                /* The pass is over. This is the notice that ends the pause, and
                 * it is the one that has to restart the endpoint, because the
                 * placement deliberately did not. */
                xhciEpRestartIfStopped(ext, dev, &binding);
            }
        }
        XhciControllerLockRelease(oldIrql);

        XhciSlotDeferredWork(ext);
        return;
    }
    if (state != USBPORT_ENDPOINT_REMOVE) {
        return;
    }

    XhciControllerLockAcquire(&oldIrql);
    dev = xhciDevFromRef(ext, endpoint->DeviceIndex);
    if (dev != NULL && dev->State != XHCI_DEV_STATE_FREE) {
        XHCI_EP_BINDING binding;
        PXHCI_TRANSFER_QUEUE queue;

        /*
         * **This is not a teardown**, and that is the finding batch 6-0 paid
         * for: the same REMOVE arrives before an endpoint is deleted *and*
         * before `USBPORT_ReopenPipe` rebuilds it, and the two are
         * indistinguishable from here. So the slot, the device context and the
         * ring all survive; only the binding to usbport's endpoint goes away.
         * The device is torn down when its **port** says so, and from nowhere
         * else (task 6-B.5).
         *
         * **It names one endpoint, not the device**, which mattered from the
         * moment a device had more than one: unbinding EP0 because an interrupt
         * pipe was removed would leave the enumeration pipe unreachable, and
         * cancelling EP0's queue with it would answer transfers nobody had
         * withdrawn.
         */
        if (endpoint->Dci <= 1) {
            if (dev->EndpointExtension != NULL &&
                dev->EndpointExtension != (PVOID)endpoint) {
                /*
                 * **A superseded EP0 handle, not the live one** (issue 4,
                 * task 19.7). XP's hub re-creates a device it is still
                 * enumerating through a second usbport device handle: the
                 * port reset again, EP0 opened at address 0 through a new
                 * extension that `xhciDevOpenOnRootPort` resolved to this
                 * same record, the device addressed again through it - and
                 * the first handle's EP0 removed last. Everything below this
                 * branch belongs to the extension the record is bound to:
                 * the flag, the pointer, the owed invalidate, the EP0 queue
                 * and any SET_ADDRESS in flight. Taking it here unbound the
                 * live handle, every submit through it was refused for
                 * retry, and the progress detector failed a healthy device
                 * (the XP guest, 2026-09-03, run `p194`). So this REMOVE
                 * closes its own extension and touches nothing else.
                 *
                 * The same-extension reopen both primary targets perform
                 * arrives with `EndpointExtension == endpoint` and falls
                 * through unchanged; a REMOVE with the binding already gone
                 * (`NULL`) does too, so a second REMOVE of a departed handle
                 * still drains and cancels as it always has.
                 */
                ext->Ep0RemovesSuperseded++;
                endpoint->Flags &= ~XHCI_ENDPOINT_FLAG_OPEN;
                XhciControllerLockRelease(oldIrql);
                XhciSlotDeferredWork(ext);
                return;
            }
            dev->Flags &= ~XHCI_DEV_FLAG_EP0_OPEN;
            dev->EndpointExtension = NULL;
            xhciDevDropInvalidate(ext, &dev->Flags,
                                  XHCI_DEV_FLAG_INVALIDATE_EP0);
            queue = &dev->Ep0Queue;
        } else {
            PXHCI_ENDPOINT_RECORD record;

            record = xhciEpByDci(dev, endpoint->Dci);
            if (record == NULL) {
                endpoint->Flags &= ~XHCI_ENDPOINT_FLAG_OPEN;
                XhciControllerLockRelease(oldIrql);
                XhciSlotDeferredWork(ext);
                return;
            }
            /*
             * The record, its pool ring and whatever the xHC has configured all
             * **stay**. A reopen at the same DCI rebinds to exactly this ring,
             * which is required rather than convenient: the xHC's TR Dequeue
             * Pointer still points into it and nothing here updates that field.
             * Recycling the pool entry now is the one thing that could hand live
             * DMA memory to another endpoint, and proving it safe needs the Stop
             * Endpoint machine task 7a-B.1 owns.
             */
            record->EndpointExtension = NULL;
            xhciDevDropInvalidate(ext, &record->InvalidateOwed, 1UL);
            queue = &record->Queue;
            if (record->State == XHCI_EP_REC_CONFIGURED) {
                /*
                 * **The xHC keeps this endpoint configured, and that is a
                 * declared limitation rather than an oversight.** A REMOVE
                 * precedes a *reopen* as well as a delete and the two are
                 * indistinguishable from here (batch 6-0), so dropping the
                 * endpoint now would drop it on every enumeration's reopen too -
                 * and re-adding it would need a second Configure Endpoint on the
                 * path both targets already pass.
                 *
                 * What it costs is the endpoint's periodic bandwidth and its
                 * Endpoint Context, held until the device goes and the Disable
                 * Slot releases everything. That is bounded by device lifetime
                 * and by `XHCI_MAX_DEVICE_ENDPOINTS`; what it is *not* bounded
                 * by is a device that keeps selecting alternate settings with
                 * fewer endpoints, which would accumulate. This counter is what
                 * would say so on a target, and no HID device does it.
                 */
                ext->EndpointRemovesHeld++;
            }
        }

        if (queue->Count != 0) {
            /*
             * Transfers still queued at a REMOVE. On the enumeration path this
             * does not happen - usbport completes SET_ADDRESS before reopening -
             * and it is still counted apart from every other cancellation,
             * because it is the reading that says the stop below was needed
             * rather than merely correct.
             */
            ext->RemovesWithWork++;
            XHCI_DBG_VALUE_CHANGED("slot: REMOVE with transfers still queued, "
                                   "dci << 16 | count",
                                   (endpoint->Dci << 16) | queue->Count);
        }
        /*
         * **The queue is not drained here, and that is task 7a-B.1's whole
         * change.** Completing a transfer hands its mapped buffer back to
         * usbport, and its TRBs are still on a ring the xHC reaches through a
         * DCBAA entry and an Endpoint Context this path deliberately leaves
         * alone - so the answer has to wait for a Stop Endpoint, which is what
         * `XHCI_EPQ_DRAIN` asks for. Where nothing can be stopped (no slot, or a
         * controller that cannot be commanded) the arming path decides between
         * "quiescent by construction" and "unpayable", and neither of those is
         * this function's judgement to make.
         *
         * The record, its pool ring and whatever the xHC has configured all
         * still **stay**: a reopen at the same DCI rebinds to exactly this ring.
         */
        if (xhciEpResolve(dev, endpoint->Dci, &binding)) {
            xhciEpArmIfBusy(ext, dev, &binding, XHCI_EPQ_DRAIN);
        } else {
            /* No ring was ever carved for this DCI, so nothing can be executing
             * one - the queue cannot be non-empty either, and the drain is a
             * formality that keeps the two paths' postconditions identical. */
            xhciDevCancelQueue(ext, queue, XHCI_USBD_STATUS_CANCELED);
        }
        if (endpoint->Dci <= 1) {
            xhciDevCancelSetAddress(ext, dev, XHCI_USBD_STATUS_CANCELED);
        }
    }
    endpoint->Flags &= ~XHCI_ENDPOINT_FLAG_OPEN;
    XhciControllerLockRelease(oldIrql);

    XhciSlotDeferredWork(ext);
}

/* ------------------------------------------------------------------ */
/* Task 7a-B.3: reset-pipe                                             */
/* ------------------------------------------------------------------ */

/*
 * The three status callbacks, and the first thing to say about them is that they
 * are **not** in batch 6-0's dead list. Both shipping builds call all three:
 * `GetEndpointStatus` from one site each (SP4 `0x1B947` inside the wrapper at
 * `0x1B904`, NUSB `0x1B531`), `SetEndpointDataToggle` and `SetEndpointStatus`
 * from two each - the reset-pipe URB path (SP4 `0x23EC4`/`0x23F6C`) and the
 * device-restore path (SP4 `0x27742`/`0x2776D`). So this is live contract rather
 * than defensive stubbing.
 *
 * The reset-pipe path also gives a precondition worth having: it refuses while
 * the endpoint's own transfer list is non-empty, failing the URB with
 * `0x80000400` (SP4 `0x23E88`-`0x23ED9`). So by the time `SetEndpointStatus(RUN)`
 * arrives, usbport believes nothing is outstanding on this pipe.
 *
 * IRQL: DISPATCH_LEVEL, under MiniportSpinLock, controller lock not held.
 */
ULONG XhciSlotGetEndpointStatus(PXHCI_EXTENSION ext, PXHCI_ENDPOINT endpoint)
{
    XHCI_EP_BINDING binding;
    KIRQL oldIrql;
    PXHCI_DEVICE dev;
    ULONG status;

    status = USBPORT_ENDPOINT_RUN;
    if (ext == NULL || ext->Signature != XHCI_EXTENSION_SIGNATURE ||
        endpoint == NULL || endpoint->Signature != XHCI_ENDPOINT_SIGNATURE) {
        return status;
    }

    XhciControllerLockAcquire(&oldIrql);
    ext->EndpointStatusQueries++;
    dev = xhciDevFromRef(ext, endpoint->DeviceIndex);
    if (dev != NULL && dev->State != XHCI_DEV_STATE_FREE &&
        xhciEpResolve(dev, endpoint->Dci, &binding) &&
        (binding.Quiesce->Flags & XHCI_EPQ_HALTED) != 0) {
        /*
         * Answered from this driver's own halt bit rather than from a fresh read
         * of the Endpoint Context, and that is deliberate: the bit is set by the
         * Transfer Event that *reported* the halt, so it is true for exactly as
         * long as usbport needs to be told about it, and it survives a recovery
         * chain reading Stopped in the middle. The register read exists for the
         * one question the bit cannot answer - which of four states a Context
         * State Error meant - and lives at that decision instead.
         */
        status = USBPORT_ENDPOINT_HALT;
    }
    XhciControllerLockRelease(oldIrql);
    return status;
}

/* IRQL: DISPATCH_LEVEL, under MiniportSpinLock, controller lock not held. */
VOID XhciSlotSetEndpointStatus(PXHCI_EXTENSION ext,
                               PXHCI_ENDPOINT endpoint,
                               ULONG status)
{
    XHCI_EP_BINDING binding;
    KIRQL oldIrql;
    PXHCI_DEVICE dev;

    if (ext == NULL || ext->Signature != XHCI_EXTENSION_SIGNATURE ||
        endpoint == NULL || endpoint->Signature != XHCI_ENDPOINT_SIGNATURE) {
        return;
    }
    if (status != USBPORT_ENDPOINT_RUN) {
        /* Neither shipping build asks for anything else - both call sites push a
         * zeroed register as the third argument - so this is counted rather than
         * acted on. Halting an endpoint on request is not something xHCI offers
         * anyway; the states it has are the xHC's. */
        XhciControllerLockAcquire(&oldIrql);
        ext->EndpointStatusOtherRequests++;
        XhciControllerLockRelease(oldIrql);
        return;
    }

    XhciControllerLockAcquire(&oldIrql);
    ext->EndpointStatusRunRequests++;
    dev = xhciDevFromRef(ext, endpoint->DeviceIndex);
    if (dev != NULL && dev->State != XHCI_DEV_STATE_FREE &&
        xhciEpResolve(dev, endpoint->Dci, &binding)) {
        PXHCI_EP_QUIESCE quiesce;

        quiesce = binding.Quiesce;
        if ((quiesce->Flags & (XHCI_EPQ_HALTED | XHCI_EPQ_FAILED)) != 0) {
            /*
             * "Reset a pipe" is spec 4.6.8 p.116's sequence, and this is the
             * point in it where the host side may finally be reset: the client
             * has issued its `ClearFeature(ENDPOINT_HALT)` (or decided it does
             * not need one), which is the step the same page warns about -
             * "Undefined behavior may occur if this command is executed with
             * TSP = '0' and the associated device endpoint is not successfully
             * reset by system software".
             *
             * `XHCI_EPQ_FAILED` is cleared first because a reset-pipe is exactly
             * the request that should be allowed to retry a chain this driver
             * gave up on; leaving it set would make the pipe permanently dead
             * with a recovery request pending against it. The arming path then
             * chooses Reset Endpoint or Stop Endpoint from the halt bit, which
             * is the only one of the two that is legal from each state.
             *
             * `XHCI_EPQ_DRAIN` because anything still on this queue cannot be
             * finished by hardware - usbport believes the pipe is empty (its own
             * reset path refuses otherwise), so a transfer left here is one
             * nobody is waiting on and nothing will complete.
             */
            quiesce->Flags &= ~(XHCI_EPQ_FAILED | XHCI_EPQ_UNAVAILABLE);
            /*
             * **A reprogram the FAILED chain swallowed is re-armed here**
             * (Phase 7 review, B1). A reopen with different parameters that
             * landed while the chain was FAILED stored its target in
             * `PendingParams` and had its RECONFIGURE arm declined by the
             * FAILED early-return - the open still answered success, so
             * usbport and the device are on the new alternate setting while
             * the xHC keeps the old Endpoint Context. `PendingParams`
             * differing from `Params` is exactly that state (the commit is
             * the only writer that reconciles them), and this is the one
             * request that reopens the chain, so it is the place the intent
             * has to be restored.
             */
            if (binding.Record != NULL &&
                !xhciEpParamsSame(&binding.Record->PendingParams,
                                  &binding.Record->Params)) {
                xhciEpArmQuiesce(ext, dev, &binding,
                                 XHCI_EPQ_DRAIN | XHCI_EPQ_RECONFIGURE);
            } else {
                xhciEpArmQuiesce(ext, dev, &binding, XHCI_EPQ_DRAIN);
            }
        }
    }
    XhciControllerLockRelease(oldIrql);

    XhciSlotDeferredWork(ext);
}

/*
 * A restore succeeded, so every record survived untouched - including any
 * whose quiesce chain was marked FAILED because a REMOVE or PAUSE arrived
 * inside the suspend window, when no command could be issued (Phase 7 review,
 * B3). That FAILED says nothing about the endpoint - `XHCI_EPQ_UNAVAILABLE`
 * beside it is the discriminator - and left standing it is a permanently dead
 * pipe whose only other recovery is a client reset-pipe nothing is obliged to
 * send. Cleared here, and re-armed with what the record still says it needs:
 * a swallowed reprogram (PendingParams differing from Params, the B1
 * invariant) re-arms the reconfigure; otherwise a busy endpoint gets a plain
 * stop so its queue and position are re-established the ordinary way.
 *
 * IRQL: <= DISPATCH_LEVEL, controller lock **not** held (acquired here).
 */
VOID XhciSlotResumeSweep(PXHCI_EXTENSION ext)
{
    KIRQL oldIrql;
    ULONG i;
    ULONG dciIndex;

    if (ext == NULL || ext->Signature != XHCI_EXTENSION_SIGNATURE) {
        return;
    }
    XhciControllerLockAcquire(&oldIrql);
    for (i = 0; i < XHCI_MAX_SLOTS; i++) {
        PXHCI_DEVICE dev = &ext->Devices[i];

        if (dev->State == XHCI_DEV_STATE_FREE ||
            dev->State == XHCI_DEV_STATE_GONE ||
            dev->State == XHCI_DEV_STATE_FAILED) {
            continue;
        }
        for (dciIndex = 0; dciIndex <= XHCI_MAX_DEVICE_ENDPOINTS; dciIndex++) {
            XHCI_EP_BINDING binding;
            ULONG dci;

            dci = (dciIndex == 0) ? 1UL : dev->Endpoints[dciIndex - 1].Dci;
            if (dci == 0 || !xhciEpResolve(dev, dci, &binding)) {
                continue;
            }
            if ((binding.Quiesce->Flags &
                 (XHCI_EPQ_FAILED | XHCI_EPQ_UNAVAILABLE)) !=
                (XHCI_EPQ_FAILED | XHCI_EPQ_UNAVAILABLE)) {
                continue;
            }
            binding.Quiesce->Flags &= ~(XHCI_EPQ_FAILED |
                                        XHCI_EPQ_UNAVAILABLE);
            ext->EndpointsRevivedByResume++;
            if (binding.Record != NULL &&
                !xhciEpParamsSame(&binding.Record->PendingParams,
                                  &binding.Record->Params)) {
                xhciEpArmQuiesce(ext, dev, &binding,
                                 XHCI_EPQ_DRAIN | XHCI_EPQ_RECONFIGURE);
            } else {
                xhciEpArmIfBusy(ext, dev, &binding, 0);
            }
        }
    }
    XhciControllerLockRelease(oldIrql);
}

/*
 * IRQL: DISPATCH_LEVEL, under MiniportSpinLock.
 *
 * A documented no-op rather than an omission: xHCI keeps the Data Toggle in the
 * Endpoint Context and clears it itself, as part of the Reset Endpoint this
 * driver issues with TSP = 0 - "If the Transfer State Preserve (TSP) flag is
 * '0': Reset the Data Toggle for USB2 devices" (4.6.8 p.115). There is no
 * software copy for usbport's request to set, and the *reason* the request is
 * safe to ignore is that the reset which follows it does the same thing.
 *
 * Counted so a target can show it arrived, because "this callback is a no-op"
 * and "this callback is never called" are different claims and only the counter
 * separates them.
 */
VOID XhciSlotSetEndpointDataToggle(PXHCI_EXTENSION ext,
                                   PXHCI_ENDPOINT endpoint,
                                   ULONG toggle)
{
    KIRQL oldIrql;

    if (ext == NULL || ext->Signature != XHCI_EXTENSION_SIGNATURE) {
        return;
    }
    XhciControllerLockAcquire(&oldIrql);
    ext->EndpointDataToggleResets++;
    XhciControllerLockRelease(oldIrql);
}

/* ------------------------------------------------------------------ */
/* Task 6-B.3: transfers, and the SET_ADDRESS that is not one          */
/* ------------------------------------------------------------------ */

/*
 * Is this the setup packet software is forbidden to place on a transfer ring?
 *
 * "The xHC blocks software-issued SET_ADDRESS and completes the TRB with a TRB
 * Error" (spec 4.5.4.1), so this is not an optimisation: a SET_ADDRESS that
 * reaches the ring fails, and the device never gets an address. IRQL: any.
 */
static ULONG xhciDevIsSetAddress(const USBPORT_TRANSFER_PARAMETERS *parameters)
{
    return (parameters->SetupPacket.bmRequestType ==
                XHCI_SETUP_TYPE_SET_ADDRESS &&
            parameters->SetupPacket.bRequest == XHCI_SETUP_REQUEST_SET_ADDRESS)
               ? 1
               : 0;
}

/* ------------------------------------------------------------------ */
/* Task 7b-A.1: feeding the hub topology graph                         */
/* ------------------------------------------------------------------ */

/*
 * Tear down every behind-hub record on this root port whose **parent is no
 * longer in the graph** (task 7b-A.3).
 *
 * The graph is the discriminator rather than the record set, and deliberately: a
 * node is pruned when a device really leaves - a disconnect its parent reported,
 * a detach at release or disown, a root-port generation change - and is *kept*
 * across the re-enumeration cycle that clears a record's usbport address every
 * fifteen seconds on this driver today (batch 7b-A.1.0). Keying on "no live
 * record holds that address" would therefore tear the subtree down on every
 * recovery round, which is churn rather than cleanup.
 *
 * One pass suffices because `xhciTopoPruneFrom` removes a whole subtree at once,
 * so every descendant of a departed hub is orphaned in the same instant.
 *
 * Called with the lock held. IRQL: DISPATCH_LEVEL.
 */
static VOID xhciDevTeardownOrphans(PXHCI_EXTENSION ext, ULONG rootPort)
{
    ULONG i;

    if (rootPort == 0) {
        return;
    }
    for (i = 0; i < XHCI_MAX_SLOTS; i++) {
        PXHCI_DEVICE dev = &ext->Devices[i];

        if (dev->State == XHCI_DEV_STATE_FREE ||
            dev->State == XHCI_DEV_STATE_GONE) {
            continue;
        }
        if (dev->HubPort != 0 || dev->RootPort != rootPort ||
            dev->ParentHubAddress == 0) {
            continue;
        }
        if (XhciTopoFind(&ext->Topology, dev->ParentHubAddress) != NULL) {
            continue;
        }
        ext->BehindHubGone++;
        XHCI_DBG_VALUE_CHANGED("slot: a device whose parent hub has gone, "
                               "tier << 24 | route",
                               (dev->Tier << 24) | dev->RouteString);
        xhciDevTeardown(ext, dev);
    }
}

/*
 * Snoop one EP0 transfer that was **successfully placed on the ring**, and arm
 * the reply capture if the graph wants the bytes.
 *
 * On placement, not on submission: a refused transfer is requeued and
 * resubmitted by usbport, and observing each attempt would count one
 * SET_FEATURE(PORT_RESET) as several - the graph's pending-parent overwrite
 * counter would then report a serialization violation that never happened.
 *
 * Address-0 traffic is skipped, not folded: the graph is keyed on usbport's
 * device address, and an unaddressed device has none to key on. Nothing is
 * lost - the hub-class requests the graph reads are all sent to *addressed*
 * hubs, and an address-0 pipe only ever carries GET_DESCRIPTOR(device).
 *
 * The position push sits here rather than inside the graph because the graph
 * cannot know it: usbport's traffic says "this device is a hub" and never
 * where it sits, while this driver's own record does. **A root port or a
 * claimed parent**, and the branch below picks between `XhciTopoAttachRoot`
 * and `XhciTopoAttachChild` on which one the record carries.
 *
 * *(This said the position was "today always a root port, because a behind-hub
 * device does not enumerate until task 7b-A.3 consumes this graph", and that
 * "when that task lands, this is the site that gains the AttachChild branch",
 * until the post-Phase 13 review rounds. It landed, and the branch it predicted is the `else if`
 * twenty lines below this comment.)*
 *
 * Called with the lock held.
 */
static VOID xhciDevTopoSnoopSubmit(PXHCI_EXTENSION ext,
                                   PXHCI_DEVICE dev,
                                   PXHCI_TRANSFER transfer,
                                   const USBPORT_TRANSFER_PARAMETERS *parameters,
                                   const USBPORT_SCATTER_GATHER_LIST *sgList)
{
    XHCI_TOPO_SNOOP snoop;
    const XHCI_TOPO_NODE *node;

    if (dev->DeviceAddress == 0) {
        return;
    }

    XhciTopoObserveSetup(&ext->Topology, dev->DeviceAddress,
                         &parameters->SetupPacket, &snoop);

    /*
     * **A hub-port reset spends the root-port entitlement** (task 7b-A.3).
     * usbhub has just told a hub to reset one of its downstream ports, so the
     * next address-0 open is the device appearing there - not a device on a
     * root port whose own reset was never spent. Leaving both armed would put
     * the two entitlements in a race the open would have to arbitrate, and
     * `xhciDevEnumeratingPort` runs first, so the root-port claim would win
     * every time and reproduce the 7b-A.0 hijack one tier up.
     */
    if (snoop.Armed) {
        PXHCI_DEVICE child;

        /*
         * **Task 7b-A.1.1's rule, one tier down** (Phase 7 review, finding
         * A6). usbhub's enumeration bracket resets the port *twice* - reset,
         * address-0 open, GET_DESCRIPTOR, reset, SET_ADDRESS (measured: the
         * open sits between the two resets, 2a line 1007 between 999 and
         * 1017) - so the claim this second reset just re-armed is one nothing
         * will spend: after SET_ADDRESS it sits there for whichever address-0
         * open next arrives with its own reset unobserved, and
         * `xhciDevByHubPath` then re-enters the already-ADDRESSED child at
         * that position - the 7b-A.0 hijack shape, binding the wrong physical
         * device. The discriminator mirrors the root tier's: the claim's
         * target position holds a live record mid-enumeration (EP0 open, no
         * address yet, not disowned, not failed). Bounded to one suppression
         * per claim spend - the flag is cleared when the open at this
         * position spends a claim - because a bracket abandoned
         * mid-enumeration would otherwise suppress every later reset and
         * disable the position permanently.
         */
        child = xhciDevByHubPath(ext, dev->RootPort, dev->DeviceAddress,
                                 snoop.Port);
        if (child != NULL && child->State != XHCI_DEV_STATE_FAILED &&
            (child->Flags & (XHCI_DEV_FLAG_ADDRESS_VALID |
                             XHCI_DEV_FLAG_EP0_OPEN |
                             XHCI_DEV_FLAG_DISOWNED |
                             XHCI_DEV_FLAG_HUB_RESET_SUPPRESSED)) ==
                XHCI_DEV_FLAG_EP0_OPEN) {
            child->Flags |= XHCI_DEV_FLAG_HUB_RESET_SUPPRESSED;
            XhciTopoSuppressClaim(&ext->Topology);
            XHCI_DBG_VALUE_CHANGED("slot: a mid-enumeration hub-port reset "
                                   "re-armed nothing, hub << 8 | port",
                                   (dev->DeviceAddress << 8) | snoop.Port);
        }
        ext->EnumClaimSpent = 1;
    }

    /*
     * If this packet - or any earlier one - made the device a hub, give the
     * node the position the record holds. Idempotent and gated on the node
     * still being position-less, so a hub keeps the position it was learned
     * at rather than following the record through a re-enumeration mid-fold.
     *
     * Which entry point depends on where the record sits, and the graph cannot
     * decide that for itself - a hub behind a hub has no root-hub port shadow
     * to take a generation from, so it inherits its parent's inside
     * `XhciTopoAttachChild`. Task 7b-A.3 is what makes the second branch
     * reachable at all: before it, no record ever had a tier.
     */
    node = XhciTopoFind(&ext->Topology, dev->DeviceAddress);
    if (node != NULL) {
        /* The graph holds a node under this record's address, so remember the
         * key: a teardown arriving mid-re-enumeration finds `DeviceAddress`
         * already cleared to 0 and would otherwise detach nothing (finding
         * A4 - see xhciDevTopoDetach). */
        dev->TopoAddress = dev->DeviceAddress;
    }
    if (node != NULL && node->RootPort == 0 && dev->RootPort != 0) {
        if (dev->HubPort != 0 && dev->HubPort <= ext->RootHub.PortCount) {
            (VOID)XhciTopoAttachRoot(
                &ext->Topology, dev->DeviceAddress, dev->RootPort,
                ext->RootHub.Ports[dev->HubPort - 1].Generation, dev->Speed);
        } else if (dev->Tier != 0 && dev->ParentHubAddress != 0) {
            XHCI_TOPO_CHILD at;

            at.HubAddress = dev->ParentHubAddress;
            at.HubPort = dev->ParentHubPort;
            at.RootPort = dev->RootPort;
            at.Tier = dev->Tier;
            at.Route = dev->RouteString;
            at.TooDeep = 0;     /* a record with one was never created */
            (VOID)XhciTopoAttachChild(&ext->Topology, dev->DeviceAddress, &at,
                                      dev->Speed);
        }
    }

    if (snoop.Reply == XHCI_TOPO_REPLY_NONE) {
        return;
    }
    /*
     * The reply's bytes live at the SG list's mapped VA, which usbport keeps
     * mapped until `UsbPortCompleteTransfer` - and the fold runs before that
     * call, under the lock, in the completion drain. A zero-length or unmapped
     * transfer has no bytes to read and stays unarmed; the graph then simply
     * never learns what this reply carried, which is a lost reading rather
     * than a wrong one.
     */
    if (sgList == NULL || sgList->SgElementCount == 0 ||
        sgList->MappedSystemVa == NULL) {
        return;
    }

    transfer->TopoReply = snoop.Reply;
    transfer->TopoAddress = snoop.Address;
    transfer->TopoPort = snoop.Port;
    transfer->TopoReplyVa = (ULONG_PTR)sgList->MappedSystemVa;
}

/* ------------------------------------------------------------------ */
/* Task 9-A.2: the isochronous bInterval channel                       */
/* ------------------------------------------------------------------ */

/*
 * Arm the capture of a `GET_DESCRIPTOR(Configuration)` reply, whose endpoint
 * descriptors carry the one number usbport discards - an isochronous endpoint's
 * `bInterval` (src/xhci_desc.h).
 *
 * A second function beside `xhciDevTopoSnoopSubmit` rather than a branch inside
 * it, on the same terms as the two field sets in XHCI_TRANSFER: the topology
 * snoop is about a *hub* and mutates a graph, this is about the *device record*
 * the transfer is on, and the only thing they share is the reply channel.
 *
 * Called with the lock held, from the placement path for the reason the
 * topology snoop is: a refused transfer comes back and would be counted twice.
 */
static VOID xhciDevDescSnoopSubmit(PXHCI_EXTENSION ext,
                                   PXHCI_DEVICE dev,
                                   PXHCI_TRANSFER transfer,
                                   const USBPORT_TRANSFER_PARAMETERS *parameters,
                                   const USBPORT_SCATTER_GATHER_LIST *sgList)
{
    XHCI_DESC_SNOOP snoop;

    XhciDescObserveSetup(dev->DeviceAddress, &parameters->SetupPacket, &snoop);
    if (snoop.Action == XHCI_DESC_ACT_NONE) {
        return;
    }
    /*
     * **Every action is armed here and applied at the completion**, which the
     * first review round is why: a `SET_CONFIGURATION` or `SET_INTERFACE` that
     * stalls changed nothing on the device, and acting on the placement would
     * have thrown away a correct reading - or believed a selection the device
     * refused - on the strength of a request that never took effect. A
     * configuration reply has to wait for its bytes anyway.
     */
    if (snoop.Action == XHCI_DESC_ACT_CONFIG_REPLY) {
        /*
         * The bytes live at the SG list's mapped VA until
         * `UsbPortCompleteTransfer`, and the fold runs before that call under
         * this lock. No mapping means no reading - counted rather than silent,
         * because "the descriptor never arrived" and "the descriptor said
         * `bInterval` is 1" are the two readings task 9-A.2's verdict turns on
         * and they must not look alike.
         */
        if (sgList == NULL || sgList->SgElementCount == 0 ||
            sgList->MappedSystemVa == NULL) {
            ext->DescRepliesUnmapped++;
            return;
        }
        transfer->DescReplyVa = (ULONG_PTR)sgList->MappedSystemVa;
    }

    transfer->DescAction = snoop.Action;
    transfer->DescAddress = snoop.Address;
    transfer->DescValue = snoop.Value;
    transfer->DescIndex = snoop.Index;
    /*
     * The record this action belongs to, named by tenancy rather than by the
     * address it is currently reachable under - see XHCI_TRANSFER.
     */
    transfer->DescDeviceRef = xhciDevRef(ext, dev);
    transfer->DescTenancy = dev->Tenancy;
}

/*
 * Walk a captured configuration descriptor into the device record's table.
 * Called with the lock held, from `xhciDevDescApply`.
 *
 * **The reply is fed to the walk in chunks off the mapped buffer rather than
 * copied whole.** A configuration descriptor is up to 65,535 bytes; this driver
 * has no private pool (AGENTS.md) and this runs at DISPATCH_LEVEL under
 * usbport's own frames, so neither a buffer in the extension nor one on the
 * stack is available at that size. The walk is a byte-level state machine for
 * exactly that reason, and nothing in src/xhci_desc.c ever sees usbport's
 * memory.
 */
static ULONG xhciDevDescWalkReply(PXHCI_EXTENSION ext,
                                  PXHCI_DEVICE dev,
                                  PXHCI_TRANSFER transfer)
{
    ULONG installed;

    XHCI_DESC_PARSE parse;
    UCHAR bytes[XHCI_DESC_FOLD_CHUNK];
    const UCHAR *source;
    ULONG remaining;
    ULONG chunk;
    ULONG i;

    /*
     * A zero-length or unmapped reply carries nothing to walk. It is not
     * counted as a fold - `DescRepliesFolded` is the number of replies that
     * reached the walk - and it cannot be counted as unmapped either, since
     * that was decided at the submit.
     */
    installed = 0;
    if (transfer->BytesTransferred == 0 || transfer->DescReplyVa == 0) {
        return installed;
    }

    ext->DescRepliesFolded++;
    XhciDescParseBegin(&parse);
    source = (const UCHAR *)transfer->DescReplyVa;
    remaining = transfer->BytesTransferred;
    while (remaining != 0) {
        chunk = (remaining > XHCI_DESC_FOLD_CHUNK) ? XHCI_DESC_FOLD_CHUNK
                                                   : remaining;
        for (i = 0; i < chunk; i++) {
            bytes[i] = source[i];
        }
        XhciDescParseFeed(&parse, bytes, chunk);
        source += chunk;
        remaining -= chunk;
    }

    switch (XhciDescParseEnd(&parse)) {
    case XHCI_DESC_FOLD_COMMIT:
        if (XhciDescCommit(&dev->IsoDesc, &parse.Table) ==
                XHCI_DESC_COMMIT_OK) {
            installed = 1;
            ext->DescConfigsCommitted++;
            /*
             * **Counted only for the configuration that was installed**, which
             * is what the counter block says they are: an inactive
             * configuration's declarations describe endpoints this device is
             * not running, and folding them in would report declarations
             * unrelated to the configuration a target run is exercising (review
             * round 2).
             */
            ext->DescIsoEntries += parse.Table.Count;
            ext->DescIsoEntriesDropped += parse.Table.Dropped;
            ext->DescIsoBadInterval += parse.Table.BadInterval;
            ext->DescIsoBadDescriptor += parse.Table.BadDescriptor;
            if (parse.Table.Count != 0) {
                XHCI_DBG_VALUE_CHANGED("slot: configuration descriptor declared "
                                       "isochronous endpoints, "
                                       "address << 8 | count",
                                       (transfer->DescAddress << 8) |
                                           parse.Table.Count);
            }
        } else {
            /* A descriptor for a configuration this device is not running -
             * see XhciDescCommit. */
            ext->DescConfigsInactive++;
            XHCI_DBG_VALUE_CHANGED("slot: a configuration descriptor for a "
                                   "configuration this device is not running, "
                                   "address << 8 | value",
                                   (transfer->DescAddress << 8) |
                                       parse.Table.ConfigValue);
        }
        break;
    case XHCI_DESC_FOLD_PARTIAL:
        ext->DescConfigsPartial++;
        break;
    default:
        ext->DescConfigsMalformed++;
        XHCI_DBG_VALUE_CHANGED("slot: configuration descriptor did not fit its "
                               "own lengths, address", transfer->DescAddress);
        break;
    }
    return installed;
}

/*
 * **Did a selection just resolve an isochronous endpoint that is already open
 * at a different cadence?** (Review round 2's third finding.)
 *
 * The resolution depends on an ordering this repository has **not**
 * established: whether usbport sends its `SET_INTERFACE` before or after
 * reopening the pipes of the alternate it selects. If it reopens afterwards,
 * every endpoint is opened with the alternate already known and this never
 * fires. If it opens first, an endpoint whose alternates disagree is built at
 * the assumed cadence and the selection that follows arrives too late - and
 * this driver would have to reprogram an endpoint usbport did not ask it to.
 *
 * **So this measures rather than repairs, and that is deliberate**: task 7's
 * own rule is never to write a callback body against an assumed ordering, and
 * the repair (a Drop+Add on a live isochronous pipe, driven by nothing usbport
 * asked for) is a great deal more dangerous than the reading it would fix. A
 * nonzero `DescIntervalsStaleAfterSelect` on a target says the ordering is the
 * late one and the reprogram is owed; a zero says the question never arises.
 *
 * Called with the lock held. IRQL: DISPATCH_LEVEL.
 */
static VOID xhciDevDescCheckStale(PXHCI_EXTENSION ext, PXHCI_DEVICE dev)
{
    ULONG i;

    for (i = 0; i < XHCI_MAX_DEVICE_ENDPOINTS; i++) {
        PXHCI_ENDPOINT_RECORD record = &dev->Endpoints[i];
        ULONG bInterval;
        ULONG interval;

        if (record->Dci == 0) {
            continue;
        }
        /*
         * **Bound, not merely retained** (review round 3). A `REMOVE` clears
         * `EndpointExtension` and keeps the record, its DCI and the xHC's
         * context, precisely so the reopen that follows can rebind to them - so
         * an unbound record is one usbport is *between* opens on, and a
         * selection landing there is the ordinary remove/select/reopen order
         * rather than a selection that arrived too late. Counting it would make
         * the row report the sequence this driver is designed for.
         */
        if (record->EndpointExtension == NULL) {
            continue;
        }
        /*
         * `PendingParams` throughout, and the type with the Interval: it is
         * what the endpoint is *becoming*, which is the thing a reopen would
         * have to disagree with. Reading the type from one block and the
         * Interval from the other would miss a DCI whose type is being changed
         * and fire on one whose type is not isochronous any more.
         */
        if (!xhciEpTypeIsIsoch(record->PendingParams.EpType)) {
            continue;
        }
        if (!XhciDescIsoInterval(&dev->IsoDesc,
                                 XhciEndpointAddressFromDci(record->Dci),
                                 &bInterval)) {
            continue;
        }
        if (XhciIsochIntervalFromBInterval(bInterval, dev->Speed, &interval) !=
                XHCI_CTX_OK) {
            continue;
        }
        if (interval != record->PendingParams.Interval) {
            ext->DescIntervalsStaleAfterSelect++;
            XHCI_DBG_VALUE_CHANGED("slot: a selection resolved an open "
                                   "isochronous endpoint to another cadence, "
                                   "dci << 8 | interval",
                                   (record->Dci << 8) | interval);
        }
    }
}

/*
 * Apply whatever this transfer's setup packet armed, now that it has completed.
 *
 * Called with the lock held, from the completion drain's pop and strictly
 * before `UsbPortCompleteTransfer` - the same window, and for the same reason,
 * as `xhciDevTopoFoldReply`: usbport unmaps the reply buffer only after that
 * call.
 *
 * **Only a successful completion applies anything.** For a reply that is the
 * topology fold's rule - a failed transfer's buffer holds whatever the device
 * managed, and half a configuration descriptor commits a table saying the
 * device has fewer endpoints than it does. For a selection it is the stronger
 * statement: a request the device refused did not change what the device is
 * running, so believing it would describe a configuration or an alternate
 * setting that is not there.
 */
static VOID xhciDevDescApply(PXHCI_EXTENSION ext, PXHCI_TRANSFER transfer)
{
    PXHCI_DEVICE dev;

    if (transfer->DescAction == XHCI_DESC_ACT_NONE) {
        return;
    }
    if (transfer->UsbdStatus != XHCI_USBD_STATUS_SUCCESS) {
        return;
    }
    /*
     * **The record is named by tenancy, not by address.** usbport's addresses
     * are recycled, and a record released while this transfer sat on the
     * completion list can already belong to a different device by now - which
     * would install one device's descriptor readings on another. The reference
     * is the record slot and the tenancy is which device has held it since, so
     * a mismatch is the answer rather than a plausible wrong record.
     */
    dev = xhciDevFromRef(ext, transfer->DescDeviceRef);
    if (dev == NULL || dev->Tenancy != transfer->DescTenancy ||
        dev->State == XHCI_DEV_STATE_FREE) {
        ext->DescRepliesOrphaned++;
        return;
    }

    switch (transfer->DescAction) {
    case XHCI_DESC_ACT_CONFIG_REPLY:
        /*
         * Only a walk that *installed* a table can have changed what the
         * lookup answers, so only that one asks the detector - a partial probe
         * read, a malformed reply and a refused inactive configuration all
         * leave the answer exactly as it was, and asking after them would count
         * the same standing mismatch again on every configuration read (review
         * round 4).
         */
        if (xhciDevDescWalkReply(ext, dev, transfer)) {
            xhciDevDescCheckStale(ext, dev);
        }
        break;
    case XHCI_DESC_ACT_SELECT_CONFIG:
        if (XhciDescSelectConfig(&dev->IsoDesc, transfer->DescValue)) {
            ext->DescConfigsSuperseded++;
            XHCI_DBG_VALUE_CHANGED("slot: a SET_CONFIGURATION selected a "
                                   "configuration this driver had not read, "
                                   "address << 8 | value",
                                   (transfer->DescAddress << 8) |
                                       transfer->DescValue);
        }
        /* A selection always moves the answer: it returns every interface to
         * alternate 0 whether or not it discarded a table. */
        xhciDevDescCheckStale(ext, dev);
        break;
    default:
        {
            ULONG dropped = dev->IsoDesc.AltDropped;

            ext->DescInterfacesSelected++;
            XhciDescSelectInterface(&dev->IsoDesc, transfer->DescIndex,
                                    transfer->DescValue);
            /*
             * **The same packet's other reading**, and it belongs here for the
             * reason this function's header gives rather than for a new one: a
             * SET_INTERFACE the hub refused did not enable its multi-TT
             * interface, and xHCI 4.5.2 conditions MTT on the interface having
             * "been enabled". The graph applied it on placement until
             * a later review while this half already waited for the completion -
             * one packet, two observers, two rules.
             *
             * **The tenancy check above is not enough for this one, and it is
             * enough for the line above it.** `XhciDescSelectInterface` writes
             * into this record's own `IsoDesc`; the graph is keyed on
             * *usbport's address*, which is a shared namespace. `xhciDevDisown`
             * detaches the node and clears `DeviceAddress` **without advancing
             * the tenancy** and without completing what is queued, so a
             * disowned record's in-flight SET_INTERFACE still passes every test
             * above - and by the time it completes, usbport may have handed
             * that address to a device that has since been promoted to a hub of
             * its own. Applying here would then write one hub's alternate
             * setting onto another's node.
             *
             * So the test is that the record still answers to the address the
             * graph is keyed on. A disowned record holds 0 and matches nothing;
             * a re-enumerated one has had its tenancy advanced and never
             * reaches this line. *(Round 11, in round 10's own fix, whose
             * comment claimed the tenancy check established this.)*
             */
            if (dev->DeviceAddress == transfer->DescAddress) {
                XhciTopoApplySetInterface(&ext->Topology, transfer->DescAddress,
                                          transfer->DescValue);
            } else {
                ext->DescSelectionsOffAddress++;
            }
            /*
             * The *increase*, because the state's own count has to stay - an
             * interface whose selection was dropped is one whose alternate this
             * driver does not know, and `XhciDescIsoInterval` reads that to
             * refuse rather than answer "alternate 0" (review round 3). Zeroing
             * it here, which an earlier draft did, threw away the very fact.
             */
            if (dev->IsoDesc.AltDropped != dropped) {
                ext->DescInterfacesDropped++;
            }
            xhciDevDescCheckStale(ext, dev);
        }
        break;
    }
}

/*
 * Fold a snooped reply's bytes into the graph, if this transfer carried one
 * and completed with them.
 *
 * Called with the lock held, from the completion drain's pop - strictly before
 * `UsbPortCompleteTransfer`, which is what makes reading `TopoReplyVa` sound:
 * usbport unmaps the buffer only after that call. The bytes are copied out
 * first and the graph is handed the copy, so nothing in src/xhci_topo.c ever
 * holds or reads usbport-owned memory. Only a successful completion is folded:
 * a failed or cancelled transfer's buffer holds whatever the device managed,
 * and a graph built from half a descriptor is worse than one that waited for
 * the retry.
 */
static VOID xhciDevTopoFoldReply(PXHCI_EXTENSION ext, PXHCI_TRANSFER transfer)
{
    XHCI_TOPO_SNOOP snoop;
    XHCI_TOPO_GONE gone;
    UCHAR bytes[XHCI_TOPO_REPLY_BYTES];
    const UCHAR *source;
    ULONG copy;
    ULONG i;

    if (transfer->TopoReply == XHCI_TOPO_REPLY_NONE) {
        return;
    }
    if (transfer->UsbdStatus != XHCI_USBD_STATUS_SUCCESS ||
        transfer->BytesTransferred == 0 || transfer->TopoReplyVa == 0) {
        return;
    }

    snoop.Reply = transfer->TopoReply;
    snoop.Address = transfer->TopoAddress;
    snoop.Port = transfer->TopoPort;

    source = (const UCHAR *)transfer->TopoReplyVa;
    copy = transfer->BytesTransferred;
    if (copy > XHCI_TOPO_REPLY_BYTES) {
        copy = XHCI_TOPO_REPLY_BYTES;
    }
    for (i = 0; i < copy; i++) {
        bytes[i] = source[i];
    }

    /* The full transferred count, not the copied prefix: the descriptor's
     * self-consistency check reasons about what arrived, and the folds index
     * at most XHCI_TOPO_REPLY_BYTES - the asserts in src/xhci_topo.h pin
     * every offset inside the window. */
    (VOID)XhciTopoObserveReply(&ext->Topology, &snoop, bytes,
                               transfer->BytesTransferred, &gone);

    /*
     * **Task 7b-A.3: this is the only way a behind-hub device is ever known to
     * have gone.** A root-port device leaves through the root-hub callbacks;
     * one tier down there are none, because usbhub owns that port and this
     * driver sees only the traffic on it. The hub's own `GET_STATUS(port)`
     * reply is the whole of the event, and a record left standing after it
     * holds a Slot ID nothing gives back and a usbport address that will refuse
     * the next device given it - which is task 6-B.5's port-disable defect,
     * exactly, one tier lower.
     */
    if (gone.Disconnected) {
        PXHCI_DEVICE child;
        ULONG rootPort;

        rootPort = xhciDevRootPortOfHub(ext, gone.HubAddress);
        child = xhciDevByHubPath(ext, rootPort, gone.HubAddress, gone.HubPort);
        if (child != NULL) {
            ext->BehindHubGone++;
            XHCI_DBG_VALUE_CHANGED("slot: a hub reported a downstream device "
                                   "gone, hub address << 8 | port",
                                   (gone.HubAddress << 8) | gone.HubPort);
            xhciDevTeardown(ext, child);
        }
        /*
         * **And whatever was below it**, which is not the same records: if the
         * device that left was itself a hub, its own children's records name a
         * parent that no longer exists, and nothing else would ever reach them.
         * A root-port event sweeps by root port; this tier has no such event.
         */
        xhciDevTeardownOrphans(ext, rootPort);
    }
}

/* ------------------------------------------------------------------ */
/* Batch 7a-B: what a quiescing endpoint does with a new transfer      */
/* ------------------------------------------------------------------ */

#define XHCI_EPQ_GATE_PASS          0
#define XHCI_EPQ_GATE_RETRY         1
#define XHCI_EPQ_GATE_FAIL_HC       2
#define XHCI_EPQ_GATE_FAIL_STALL    3

/*
 * May a transfer go on this endpoint's ring right now?
 *
 * Three answers, and the split is the one the batch 6-V Win2000 run paid for -
 * a refusal that can never stop being true has to be a *failure*, or usbport
 * resubmits for ever:
 *
 *   RETRY      - a quiescence chain is running, or EP0 is halted with its own
 *                recovery under way. Both end, and both end soon; the endpoint
 *                owes an invalidation at the end of the chain so the retry does
 *                not wait on usbport's 500 ms timer.
 *   FAIL_STALL - a non-default endpoint is halted and *nothing this driver will
 *                do* clears it: spec 4.6.8 p.116's reset-a-pipe sequence needs a
 *                `ClearFeature(ENDPOINT_HALT)` on the device, which is the
 *                client's reset-pipe URB and reaches this driver as
 *                `SetEndpointStatus(RUN)`. Answering `USBD_STATUS_STALL_PID` is
 *                also what *causes* that URB, and it is what lets usbport's own
 *                transfer list drain - the reset-pipe path refuses while it is
 *                non-empty (SP4 `0x23E8B` -> `0x23ED9`, failing with
 *                `0x80000400`), so holding the transfers would deadlock the
 *                pipe against the very request that would fix it.
 *   FAIL_HC    - the chain was given up on.
 *
 * Called with the lock held. IRQL: <= DISPATCH_LEVEL.
 */
static ULONG xhciEpSubmitGate(PXHCI_EXTENSION ext,
                              PXHCI_DEVICE dev,
                              PXHCI_EP_BINDING binding)
{
    PXHCI_EP_QUIESCE quiesce;

    quiesce = binding->Quiesce;
    if ((quiesce->Flags & (XHCI_EPQ_FAILED | XHCI_EPQ_NO_CONTEXT)) != 0) {
        /* `NO_CONTEXT` is a failure rather than a retry for the reason the
         * comment above gives: the xHC has no Endpoint Context for this
         * endpoint and nothing pending will give it one, so the submission that
         * follows would publish TRBs nothing can execute and then ring a
         * doorbell 4.8.3 p.150 forbids. */
        return XHCI_EPQ_GATE_FAIL_HC;
    }
    if ((quiesce->Flags & XHCI_EPQ_HALTED) != 0) {
        if (binding->Record != NULL) {
            return XHCI_EPQ_GATE_FAIL_STALL;
        }
        /*
         * EP0's recovery is this driver's own (see `xhciEpRecoveryNeeded`), so
         * the refusal is transient - but only if it really is armed. Arming it
         * here as well is what makes the retry provably terminate rather than
         * depend on the halt path having reached the same conclusion.
         */
        if ((quiesce->Flags & XHCI_EPQ_INFLIGHT) == 0) {
            xhciEpArmQuiesce(ext, dev, binding, 0);
        }
        return XHCI_EPQ_GATE_RETRY;
    }
    if ((quiesce->Flags & XHCI_EPQ_INFLIGHT) != 0) {
        return XHCI_EPQ_GATE_RETRY;
    }
    return XHCI_EPQ_GATE_PASS;
}

/*
 * Fill a transfer record that never reached a ring, without queueing it.
 *
 * Split from the queueing so the isochronous wrapper below can add its own
 * fields between the two rather than repeat these lines - and the four
 * isochronous fields are among them for a reason task 9-A.1's vectors found:
 * usbport does **not** zero the transfer extension between transfers, and the
 * completion drain now reads a *flag* to decide which of two services answers.
 * A record inheriting `XHCI_XFER_FLAG_ISOCH` from a previous tenant would be
 * answered through `UsbPortCompleteIsoTransfer` with a block pointer that
 * belonged to a transfer usbport has already freed, and `XhciXferIsoFinalise`
 * would first write two fields per packet through that stale pointer.
 * `Flags` alone is not enough to say: the finalise gate reads the flag *and*
 * the pointer, so both are cleared here and every stamping site in this file
 * goes through this one function rather than repeating the list.
 *
 * Called with the lock held. IRQL: <= DISPATCH_LEVEL.
 */
static VOID xhciDevStampTransfer(PXHCI_ENDPOINT endpoint,
                                 PXHCI_TRANSFER transfer,
                                 PUSBPORT_TRANSFER_PARAMETERS parameters,
                                 LONG usbdStatus)
{
    transfer->Signature = XHCI_TRANSFER_SIGNATURE;
    transfer->Next = NULL;
    transfer->TransferParameters = parameters;
    transfer->EndpointExtension = endpoint;
    transfer->BytesTransferred = 0;
    transfer->Flags = 0;
    transfer->IsoParams = NULL;
    transfer->IsoPacketCount = 0;
    transfer->IsoPacketsAnswered = 0;
    transfer->UsbdStatus = usbdStatus;
    transfer->TopoReply = XHCI_TOPO_REPLY_NONE;
    transfer->DescAction = XHCI_DESC_ACT_NONE;
}

/*
 * Fail one transfer outright rather than leaving it queued.
 *
 * usbport has no "reject this transfer" return: a nonzero SubmitTransfer status
 * leaves it queued for retry, which is right while a device is still addressing
 * itself and is an infinite loop once it has failed. So the only way to *fail* a
 * transfer is to accept it and complete it with an error, which is what this
 * does. Called with the lock held. IRQL: DISPATCH_LEVEL.
 */
static VOID xhciDevFailTransfer(PXHCI_EXTENSION ext,
                                PXHCI_ENDPOINT endpoint,
                                PXHCI_TRANSFER transfer,
                                PUSBPORT_TRANSFER_PARAMETERS parameters,
                                LONG usbdStatus)
{
    xhciDevStampTransfer(endpoint, transfer, parameters, usbdStatus);
    xhciDevOweCompletion(ext, transfer);
}

/*
 * Task 9-A.1: the same, for a request that arrived through `SubmitIsoTransfer`
 * and never reached the ring.
 *
 * **It is a separate entry point because the completion drain decides which
 * usbport service to call from the transfer's own flag**, and a failed
 * isochronous request that skipped this would be answered through
 * `UsbPortCompleteTransfer` - the wrong slot, with a status and a length where
 * usbport expects to read a per-packet block. The host vectors are what found
 * that: `xhciDevFailTransfer` zeroes nothing and the transfer extension is not
 * assumed zeroed, so the flag came from whatever the previous tenant left.
 *
 * `IsoPacketCount` is stamped **only when the block's signature checks out**.
 * Without it there is no reason to believe the pointer is a parameter block at
 * all, and `XhciXferIsoFinalise` would write a status into every one of
 * `NumberOfPackets` entries of something else. With the signature good the count
 * is usbport's own - the block was sized from it - so it is safe even for a
 * request this driver refused as too large, which is a policy of *this* driver
 * rather than a statement about the allocation.
 *
 * **The pointer itself is carried whatever the signature says, and that is not
 * the same decision.** `UsbPortCompleteIsoTransfer`'s fourth argument is "the
 * same block `SubmitIsoTransfer` got" (docs/usb-xhci-info/usbport-miniport-abi.md), and
 * usbport walks it per packet on the way out - so answering with NULL would
 * fault inside usbport rather than report anything, which is the opposite of
 * what a defensive path is for. Not writing into the block and not naming it are
 * separate things: `IsoPacketCount` withholds the writes, and the pointer is
 * still handed back so the other side can find its own request.
 *
 * Called with the lock held. IRQL: <= DISPATCH_LEVEL.
 */
static VOID xhciDevFailIsoTransfer(PXHCI_EXTENSION ext,
                                   PXHCI_ENDPOINT endpoint,
                                   PXHCI_TRANSFER transfer,
                                   PUSBPORT_TRANSFER_PARAMETERS parameters,
                                   const USBPORT_ISO_TRANSFER *isoParams,
                                   LONG usbdStatus)
{
    xhciDevStampTransfer(endpoint, transfer, parameters, usbdStatus);
    transfer->Flags = XHCI_XFER_FLAG_ISOCH;
    transfer->IsoParams = (PVOID)isoParams;
    if (isoParams != NULL &&
        isoParams->Signature == USBPORT_ISO_SIGNATURE) {
        transfer->IsoPacketCount = isoParams->NumberOfPackets;
    }
    xhciDevOweCompletion(ext, transfer);
}

/*
 * Fail whichever kind of request this is, for the gates `XhciSlotSubmitTransfer`
 * applies to **both** callbacks - a device record that has gone, a controller
 * that has failed terminally.
 *
 * It exists so those sites cannot pick the wrong one. They are shared code
 * reached from two callbacks, and "which service answers" is not a property of
 * the gate: an `if` written at one of them and forgotten at the other is exactly
 * the shape the vectors caught once already in this task.
 *
 * Called with the lock held. IRQL: <= DISPATCH_LEVEL.
 */
static VOID xhciDevFailEitherTransfer(
    PXHCI_EXTENSION ext,
    PXHCI_ENDPOINT endpoint,
    PXHCI_TRANSFER transfer,
    PUSBPORT_TRANSFER_PARAMETERS parameters,
    const USBPORT_ISO_TRANSFER *isoParams,
    LONG usbdStatus)
{
    if (isoParams != NULL) {
        xhciDevFailIsoTransfer(ext, endpoint, transfer, parameters, isoParams,
                               usbdStatus);
    } else {
        xhciDevFailTransfer(ext, endpoint, transfer, parameters, usbdStatus);
    }
}

/*
 * The two halves of task 7b-A.0's progress detector, and every refusal and every
 * placement in this file goes through one of them.
 *
 * One mechanism rather than a line at each of the eleven sites, for the reason
 * batch 7a-B's sweep wrote down: the property is the *record's*, not the
 * property of whichever gate happened to answer, and a site added where someone
 * was already looking is how the other ten stay uncovered. The two directions
 * are not symmetric, either - a missed refusal only delays the detector, while a
 * missed submission would let it fail a device that is working - which is why
 * the placement site is the one that must be exhaustive.
 *
 * Called with the lock held. IRQL: <= DISPATCH_LEVEL.
 */
static VOID xhciDevTransferRefused(PXHCI_EXTENSION ext,
                                   PXHCI_DEVICE dev,
                                   ULONG ringFull)
{
    ext->TransfersRefused++;
    /*
     * **A ring-full refusal is not charged to the record** (task 8-A.1), and
     * this is the third reason a refusal can be uncharged rather than a new
     * mechanism - `xhciDevAdmitted`'s controller-wide wait already passes NULL
     * for a different one.
     *
     * The progress detector fails a record that refuses for
     * XHCI_DEV_STALL_MS of consecutive polls without placing anything, which is
     * about five seconds. Every endpoint type before bulk made that equivalent
     * to "stuck": a control chain and an interrupt endpoint with one read posted
     * are both idle rings, so a refusal with nothing placed had no mechanism
     * behind it. A **bulk** ring saturated by three 64 KB transfers has one -
     * nothing new can be placed until one of them completes, so a device that
     * took longer than five seconds to answer, well inside usbport's own 10 s
     * URB timeout, would have had its whole record failed while the xHC was
     * executing its transfers correctly.
     *
     * The exclusion is right for that reason and not for the frequency once
     * claimed here: Phase 8 measured saturation as **unreached** by any device
     * class this project can drive (`TransfersRefusedRingFull` 0 on storage and
     * Ethernet, both targets), so this is a guard against a state no target run
     * has produced rather than against the working device's normal condition.
     *
     * The exclusion is deliberately **per refusal** rather than per record. A
     * first draft excluded any record with work outstanding anywhere, which made
     * a HID device keeping one interrupt read posted immune to the detector for
     * ever while EP0 refused every offer - batch 7b-V0's hang exactly. What has a
     * mechanism here is *this* refusal: the transfers on the ring that refused
     * it, ended by a Transfer Event, by this driver's halt and quiesce nets, or
     * by usbport aborting them at its URB timeout. `TransfersRefusedRingFull` is
     * what still counts them, so an uncharged refusal is not an unrecorded one.
     */
    if (dev != NULL && !ringFull) {
        dev->RefusedSincePoll = 1;
    }
}

static VOID xhciDevTransferSubmitted(PXHCI_EXTENSION ext, PXHCI_DEVICE dev)
{
    ext->TransfersSubmitted++;
    if (dev != NULL) {
        dev->SubmittedSincePoll = 1;
    }
}

/*
 * Task 7a-A.2 (interrupt) and 8-A.1 (bulk): one non-control transfer onto a
 * non-default endpoint's own ring. Nothing here branches on which of the two it
 * is - the direction comes from the Endpoint Context either way.
 *
 * The device-level gates have already run in the caller; what is left is the
 * *endpoint's* state, and the three answers it produces are the whole reason a
 * REFUSED record is kept alive rather than torn down at its completion. usbport
 * was told the open succeeded and has no other channel to be told otherwise
 * (batch 6-0: `GetEndpointState` is never called), so this is where the xHC's
 * verdict on the pipe is finally reported.
 *
 * Called with the lock held; the caller releases it and runs the deferred pass.
 * IRQL: DISPATCH_LEVEL.
 */
static MPSTATUS xhciSlotSubmitNonDefault(
    PXHCI_EXTENSION ext,
    PXHCI_DEVICE dev,
    PXHCI_ENDPOINT endpoint,
    PUSBPORT_TRANSFER_PARAMETERS parameters,
    PXHCI_TRANSFER transfer,
    const USBPORT_SCATTER_GATHER_LIST *sgList,
    XHCI_TRB *scratch,
    ULONG scratchCount)
{
    XHCI_NORMAL_REQUEST request;
    XHCI_EP_BINDING binding;
    PXHCI_ENDPOINT_RECORD record;
    ULONG built;
    ULONG censoredBefore;

    record = xhciEpByDci(dev, endpoint->Dci);
    if (record == NULL || record->EndpointExtension == NULL) {
        /*
         * Permanent, and the same shape as the EP0 gate above: a record that is
         * not there does not arrive later, and an unbound one is followed by a
         * *different* binding rather than by this transfer being served. Failing
         * it is accepting it and answering it - a nonzero return would leave
         * usbport resubmitting for ever (roadmap batch 6-V).
         */
        xhciDevFailTransfer(ext, endpoint, transfer, parameters,
                            XHCI_USBD_STATUS_CANCELED);
        ext->TransfersFailedGone++;
        XHCI_DBG_VALUE_CHANGED("slot: transfer failed - no endpoint record, "
                               "dci", endpoint->Dci);
        return MP_STATUS_SUCCESS;
    }

    if (record->State == XHCI_EP_REC_PENDING ||
        record->State == XHCI_EP_REC_CONFIGURING) {
        /*
         * The Configure Endpoint has not completed, so the xHC has no Endpoint
         * Context for this DCI and a doorbell would be rung at nothing. Genuinely
         * transient: the completion asks usbport to re-offer this exact transfer.
         */
        xhciDevTransferRefused(ext, dev, 0);
        return MP_STATUS_NO_RESOURCES;
    }
    if (record->State == XHCI_EP_REC_REFUSED) {
        /* The xHC declined to schedule the pipe. `USBD_STATUS_NO_BANDWIDTH` is
         * the answer usbhub is built to degrade on, and it is a *different*
         * answer from a controller fault - which is the whole point of keeping
         * the two completion codes apart at the command's completion. */
        xhciDevFailTransfer(ext, endpoint, transfer, parameters,
                            XHCI_USBD_STATUS_NO_BANDWIDTH);
        return MP_STATUS_SUCCESS;
    }
    if (record->State != XHCI_EP_REC_CONFIGURED) {
        xhciDevFailTransfer(ext, endpoint, transfer, parameters,
                            XHCI_USBD_STATUS_INTERNAL_HC_ERROR);
        return MP_STATUS_SUCCESS;
    }

    /*
     * Batch 7a-B's gate. It has to be here rather than folded into the states
     * above, because a quiescing endpoint is `CONFIGURED` throughout: the xHC
     * still has its Endpoint Context, and what has changed is only whether a TRB
     * placed now would be executed at the position the chain is about to
     * program.
     */
    if (xhciEpResolve(dev, endpoint->Dci, &binding)) {
        switch (xhciEpSubmitGate(ext, dev, &binding)) {
        case XHCI_EPQ_GATE_RETRY:
            xhciDevTransferRefused(ext, dev, 0);
            return MP_STATUS_NO_RESOURCES;
        case XHCI_EPQ_GATE_FAIL_STALL:
            xhciDevFailTransfer(ext, endpoint, transfer, parameters,
                                XHCI_USBD_STATUS_STALL_PID);
            ext->TransfersOnHaltedEndpoint++;
            return MP_STATUS_SUCCESS;
        case XHCI_EPQ_GATE_FAIL_HC:
            xhciDevFailTransfer(ext, endpoint, transfer, parameters,
                                XHCI_USBD_STATUS_INTERNAL_HC_ERROR);
            return MP_STATUS_SUCCESS;
        default:
            break;
        }
    }

    request.TransferLength = parameters->TransferBufferLength;
    /*
     * The direction is the **endpoint's**, read back out of the EP Type this
     * driver programmed rather than out of usbport's flags - which are passed
     * separately and checked against it. Deriving it from the Endpoint Context's
     * own field is what makes the check meaningful: two readings of the same
     * usbport structure would always agree.
     */
    request.DirectionIn = xhciEpTypeIsIn(record->Params.EpType);
    request.MaxPacketSize = record->Params.MaxPacketSize;
    request.SgList = sgList;

    transfer->EndpointExtension = endpoint;
    censoredBefore = record->Queue.MidTdTailsCensored;
    built = XhciXferSubmitNormal(&record->Queue, &record->Ring, &request,
                                 parameters->TransferFlags & 1UL, transfer,
                                 parameters, scratch, scratchCount);
    xhciDevFoldCensored(ext, &record->Queue, censoredBefore);
    if (built == XHCI_XFER_OK) {
        xhciDevTransferSubmitted(ext, dev);
        /* Published first, then the doorbell, on this endpoint's own DCI -
         * "all ring/TRB memory writes must hit memory before the doorbell"
         * (docs/usb-xhci-info/xhci-data-structures.md section 8). */
        XhciWriteDoorbell(ext, dev->SlotId, endpoint->Dci);
        /* The doorbell is what takes a Stopped endpoint back to Running (4.6.9
         * p.119), so the bit that says "software owns this ring" has to go with
         * it - a No Op rewrite performed on the strength of a stale `STOPPED`
         * would edit TRBs the xHC is executing. `PAUSED` goes too: usbport is
         * offering work again, so whatever pass it was in is over. */
        record->Quiesce.Flags &= ~(XHCI_EPQ_STOPPED | XHCI_EPQ_PAUSED);
        record->Quiesce.StoppedArmed = 0;
        return MP_STATUS_SUCCESS;
    }
    if (built == XHCI_XFER_BUSY) {
        /*
         * The ring is full and nothing was written - the one status usbport's
         * requeue is exactly right for. The latch is armed here rather than the
         * refusal simply being counted, so the re-offer follows the first
         * retirement instead of usbport's 500 ms timer.
         *
         * Batch 8-A expected this to be an ordinary state on a bulk endpoint.
         * It is not: Phase 8 measured this counter at 0 across working storage
         * and working Ethernet on both targets, because BOT is serial and the
         * NIC posts few receives. Defensive, and reached only by host vectors.
         */
        XhciXferQueueArmRetry(&record->Queue, &record->Ring);
        ext->TransfersRefusedRingFull++;
        xhciDevTransferRefused(ext, dev, 1);
        return MP_STATUS_NO_RESOURCES;
    }

    xhciDevFailTransfer(ext, endpoint, transfer, parameters,
                        XHCI_USBD_STATUS_INTERNAL_HC_ERROR);
    XHCI_DBG_VALUE_CHANGED("slot: non-default transfer refused by the builder, "
                           "status", built);
    return MP_STATUS_SUCCESS;
}

/*
 * Task 9-A.1: one isochronous request onto an isochronous endpoint's own ring.
 *
 * The endpoint-state gates are `xhciSlotSubmitNonDefault`'s, deliberately
 * repeated in full rather than factored out with it: they read the same fields
 * but they are the only part of this function that could be shared, and the
 * bodies below them - a different builder, a different scratch shape, a
 * different refusal set - have nothing in common. What *is* shared is everything
 * above the callback split, which is why there is one `XhciSlotSubmitTransfer`.
 *
 * Called with the lock held; the caller releases it and runs the deferred pass.
 * IRQL: DISPATCH_LEVEL.
 */
static MPSTATUS xhciSlotSubmitIsoNonDefault(
    PXHCI_EXTENSION ext,
    PXHCI_DEVICE dev,
    PXHCI_ENDPOINT endpoint,
    PUSBPORT_TRANSFER_PARAMETERS parameters,
    PXHCI_TRANSFER transfer,
    const USBPORT_ISO_TRANSFER *isoParams)
{
    XHCI_ISO_REQUEST request;
    XHCI_EP_BINDING binding;
    PXHCI_ENDPOINT_RECORD record;
    PXHCI_ISO_LAYOUT layout;
    ULONG built;

    /*
     * **The scratch and the layout live in the miniport extension, not on this
     * stack**, and that is a size decision rather than a style one. A worst-case
     * isochronous group is a whole ring - 62 TRBs, 992 bytes - and the per-TD
     * length array is another 248, which is more than this driver has any
     * business adding to a DISPATCH_LEVEL stack underneath usbport's own frames.
     * AGENTS.md points the same way: the miniport has no private pool, and fixed
     * software state belongs in the extension usbport already allocated.
     *
     * One buffer shared between endpoints is safe because the controller lock is
     * held across the whole of this call - the build, the publish and the record
     * - which is the same argument every other piece of shared state in this
     * driver rests on.
     */
    layout = &ext->IsoLayout;

    record = xhciEpByDci(dev, endpoint->Dci);
    if (record == NULL || record->EndpointExtension == NULL) {
        xhciDevFailIsoTransfer(ext, endpoint, transfer, parameters,
                               isoParams,
                            XHCI_USBD_STATUS_CANCELED);
        ext->TransfersFailedGone++;
        return MP_STATUS_SUCCESS;
    }
    if (!xhciEpTypeIsIsoch(record->Params.EpType)) {
        /*
         * usbport sent an isochronous request to a pipe this driver configured
         * as something else. Permanent - the record is what it is until a
         * reopen replaces it - and failed rather than retried for that reason.
         */
        ext->IsoSubmitsWrongType++;
        xhciDevFailIsoTransfer(ext, endpoint, transfer, parameters,
                               isoParams,
                            XHCI_USBD_STATUS_INVALID_PIPE_HANDLE);
        return MP_STATUS_SUCCESS;
    }
    if (record->State == XHCI_EP_REC_PENDING ||
        record->State == XHCI_EP_REC_CONFIGURING) {
        xhciDevTransferRefused(ext, dev, 0);
        return MP_STATUS_NO_RESOURCES;
    }
    if (record->State == XHCI_EP_REC_REFUSED) {
        xhciDevFailIsoTransfer(ext, endpoint, transfer, parameters,
                               isoParams,
                            XHCI_USBD_STATUS_NO_BANDWIDTH);
        return MP_STATUS_SUCCESS;
    }
    if (record->State != XHCI_EP_REC_CONFIGURED) {
        xhciDevFailIsoTransfer(ext, endpoint, transfer, parameters,
                               isoParams,
                            XHCI_USBD_STATUS_INTERNAL_HC_ERROR);
        return MP_STATUS_SUCCESS;
    }

    if (xhciEpResolve(dev, endpoint->Dci, &binding)) {
        switch (xhciEpSubmitGate(ext, dev, &binding)) {
        case XHCI_EPQ_GATE_RETRY:
            xhciDevTransferRefused(ext, dev, 0);
            return MP_STATUS_NO_RESOURCES;
        case XHCI_EPQ_GATE_FAIL_STALL:
            /*
             * An isoch endpoint cannot halt (p.177), so this gate is reachable
             * only through the *quiescence* machine's own failed state rather
             * than through a device stall - and `STALL_PID` would name something
             * that did not happen. It is reported as the host-side failure it
             * is, which is the same answer the branch below gives.
             */
            xhciDevFailIsoTransfer(ext, endpoint, transfer, parameters,
                               isoParams,
                                XHCI_USBD_STATUS_INTERNAL_HC_ERROR);
            ext->TransfersOnHaltedEndpoint++;
            return MP_STATUS_SUCCESS;
        case XHCI_EPQ_GATE_FAIL_HC:
            xhciDevFailIsoTransfer(ext, endpoint, transfer, parameters,
                               isoParams,
                                XHCI_USBD_STATUS_INTERNAL_HC_ERROR);
            return MP_STATUS_SUCCESS;
        default:
            break;
        }
    }

    request.Iso = isoParams;
    request.DirectionIn = xhciEpTypeIsIn(record->Params.EpType);
    request.MaxPacketSize = record->Params.MaxPacketSize;
    request.MaxBurstSize = record->Params.MaxBurstSize;
    request.MaxEsitPayload = record->Params.MaxEsitPayload;
    /*
     * The Frame ID policy, and both halves of `Allowed` are read here because
     * this is the only context that can read either: the capability is the
     * controller's and the frame index is a register.
     *
     * `XhciFrameIdNow` refusing is the ordinary answer on Win98, where the
     * controller idle-suspends within about half a second of every start and the
     * published frame axis stops being congruent with MFINDEX the moment it
     * does. The request is then scheduled with SIA, which is always legal.
     */
    /*
     * How many of this request's TDs the endpoint consumes per frame, from the
     * Interval this driver actually programmed rather than from the speed it
     * derived it from - so the two sides of the comparison in
     * `xhciXferIsoCadenceAgrees` are usbport's stamps and the Endpoint Context,
     * with nothing recomputed in between.
     *
     * An ESIT of `2^Interval` microframes fits inside a frame only up to
     * Interval 3. A larger one is some number of frames *per TD* rather than
     * TDs per frame, and rather than invert the arithmetic it is reported as no
     * stateable cadence - which refuses every Frame ID and drops the request to
     * SIA, the schedule that is correct whatever the interval.
     *
     * **That case is reachable since task 9-A.2** and the sentence here used to
     * say it could not arise: the Interval is no longer 0 or 3 and nothing else,
     * because an endpoint whose descriptor asks for a slower cadence is now
     * programmed with it. A High-Speed endpoint with `bInterval = 5` is Interval
     * 4 and lands exactly here.
     */
    request.PacketsPerFrame = (record->Params.Interval <= 3)
                                  ? (8UL >> record->Params.Interval)
                                  : 0UL;
    request.Frames.Allowed = 0;
    request.Frames.CurrentFrame = 0;
    request.Frames.IstFrames = ext->HcInfo.IstFrames;
    if (ext->HcInfo.Cfc &&
        XhciFrameIdNow(ext, &request.Frames.CurrentFrame)) {
        request.Frames.Allowed = 1;
    }

    transfer->EndpointExtension = endpoint;
    built = XhciXferSubmitIso(&record->Queue, &record->Ring, &request,
                              parameters->TransferFlags &
                                  USBPORT_TRANSFER_FLAG_DIRECTION_IN,
                              transfer, parameters, ext->IsoScratch,
                              XHCI_XFER_MAX_ISO_TRBS, layout);
    if (built == XHCI_XFER_OK) {
        xhciDevTransferSubmitted(ext, dev);
        ext->IsoSubmits++;
        ext->IsoPacketsSubmitted += layout->TdCount;
        if (layout->FrameIdsUsed) {
            ext->IsoSubmitsWithFrameId++;
        }
        if (layout->CadenceMismatch) {
            /*
             * **Task 9-A.2 splits this reading in two, because after the
             * derivation the same observation has two opposite meanings.**
             *
             * On an endpoint still carrying the assumed Interval, usbport's
             * stamps and the Endpoint Context were built from the same belief
             * about the cadence, so a disagreement says that belief failed -
             * which on this driver means Phase 5 task 7's root-port speed
             * report, the one thing that can make them differ.
             *
             * On an endpoint programmed at a cadence usbport is *not* stamping
             * to, the two sides no longer share a premise: usbport still
             * indexes packets one per microframe on High Speed while the
             * endpoint asked to be serviced every `2^(bInterval-1)`, so a
             * disagreement is the *expected* outcome and the request going out
             * with SIA is the correct schedule rather than a fallback from one.
             * Counting them together would make a working audio device look
             * like a driver fault, which is the shape task 8's lifecycle review
             * named as a rule: never let two causes with opposite diagnoses
             * share a counter in a release build.
             *
             * **The test is the Interval's value, not where it came from**, and
             * review round 2 is why: a derived Interval that comes out *equal*
             * to the assumption behaves exactly like the assumption, so its
             * mismatch means what the assumption's does - Phase 5 task 7's
             * root-port speed report - and charging it to the "expected"
             * counter would have hidden that fault behind the derivation. This
             * also removes the provenance field entirely: the question was
             * never who decided the Interval, only whether it differs from the
             * cadence usbport schedules to.
             *
             * **On a Full-Speed device on a root port the two causes overlap**,
             * and this row cannot separate them: Phase 5 task 7 reports the port
             * as High Speed, so usbport stamps eight packets per frame whatever
             * the Interval is, and a device asking for anything but one ESIT per
             * frame lands here with both causes present. The discriminating test
             * is the one task 9-V.1 already names - the same device behind a
             * hub, where usbport is told the true speed.
             */
            if (record->Params.Interval !=
                    ((dev->Speed == XHCI_SPEED_HIGH)
                         ? (ULONG)XHCI_EP_INTERVAL_ISOCH_HS
                         : (ULONG)XHCI_EP_INTERVAL_ISOCH_FS)) {
                ext->IsoCadenceMismatchesDerived++;
            } else {
                ext->IsoCadenceMismatches++;
            }
        }
        /*
         * **The doorbell is not optional here even on an idle endpoint**, and
         * that is an isochronous-specific reason rather than the general one.
         * "Ringing the doorbell of a periodic endpoint that has encountered a
         * Ring Overrun or Ring Underrun condition shall place it back on the
         * periodic schedule" (4.14.2.1 p.239) - an endpoint the controller took
         * off the schedule when its ring ran dry is restarted by nothing else.
         */
        XhciWriteDoorbell(ext, dev->SlotId, endpoint->Dci);
        record->Quiesce.Flags &= ~(XHCI_EPQ_STOPPED | XHCI_EPQ_PAUSED);
        record->Quiesce.StoppedArmed = 0;
        return MP_STATUS_SUCCESS;
    }
    if (built == XHCI_XFER_BUSY) {
        XhciXferQueueArmRetry(&record->Queue, &record->Ring);
        ext->TransfersRefusedRingFull++;
        xhciDevTransferRefused(ext, dev, 1);
        return MP_STATUS_NO_RESOURCES;
    }
    if (built == XHCI_XFER_ISO_TOO_LARGE) {
        /*
         * **A declared limit with an explicit refusal above it**, the shape
         * design doc 04 uses for the slot and scratchpad counts. A request
         * needing more TRBs than an empty ring holds can never be placed, so
         * retrying it is an infinite loop; it is failed, and counted, so a
         * target run says whether any real USB Audio device asks for one. The
         * lever if one does is the pooled ring size
         * (`XHCI_POOL_RING_TRBS`), not a change here.
         */
        ext->IsoRefusalsTooLarge++;
        xhciDevFailIsoTransfer(ext, endpoint, transfer, parameters,
                               isoParams,
                            XHCI_USBD_STATUS_INVALID_PARAMETER);
        XHCI_DBG_VALUE_CHANGED("slot: isoch request larger than the ring can "
                               "hold, packets", isoParams->NumberOfPackets);
        return MP_STATUS_SUCCESS;
    }

    ext->IsoRefusalsMalformed++;
    xhciDevFailIsoTransfer(ext, endpoint, transfer, parameters,
                               isoParams,
                        XHCI_USBD_STATUS_INVALID_PARAMETER);
    XHCI_DBG_VALUE_CHANGED("slot: isoch request refused by the builder, status",
                           built);
    return MP_STATUS_SUCCESS;
}

/* IRQL: DISPATCH_LEVEL, under MiniportSpinLock, controller lock not held. */
MPSTATUS XhciSlotSubmitTransfer(PXHCI_EXTENSION ext,
                                PXHCI_ENDPOINT endpoint,
                                PUSBPORT_TRANSFER_PARAMETERS parameters,
                                PXHCI_TRANSFER transfer,
                                const USBPORT_SCATTER_GATHER_LIST *sgList,
                                const USBPORT_ISO_TRANSFER *isoParams)
{
    XHCI_TRB scratch[XHCI_XFER_MAX_CONTROL_TRBS];
    XHCI_CONTROL_REQUEST request;
    XHCI_EP_BINDING binding;
    KIRQL oldIrql;
    PXHCI_DEVICE dev;
    MPSTATUS status;
    ULONG built;
    ULONG censoredBefore;

    if (ext == NULL || ext->Signature != XHCI_EXTENSION_SIGNATURE ||
        endpoint == NULL || endpoint->Signature != XHCI_ENDPOINT_SIGNATURE ||
        parameters == NULL || transfer == NULL) {
        return MP_STATUS_NOT_SUPPORTED;
    }
    /* Exactly one of the two request bodies. Neither is a caller that forgot an
     * argument; both is a caller that does not know which callback it is in. */
    if ((sgList == NULL) == (isoParams == NULL)) {
        return MP_STATUS_NOT_SUPPORTED;
    }

    status = MP_STATUS_SUCCESS;
    XhciControllerLockAcquire(&oldIrql);

    /*
     * **A refusal that can never stop being true must fail the transfer, not
     * ask for a retry.** The batch 6-V Win2000 run is why this gate is split.
     *
     * A nonzero return leaves the transfer queued and usbport resubmits it. The
     * first draft answered `MP_STATUS_NO_RESOURCES` for all three conditions
     * below on the reasoning recorded here - "a controller in the middle of a
     * resume comes back, and an endpoint whose REMOVE crossed this call is
     * usbport's own race to resolve". usbport does **not** resolve it: the run
     * showed refusals climbing two per port-change cycle for ever, submissions
     * frozen, nothing completing, and the thread that issued the URB blocked
     * with its Device Manager window half-painted. `src/xhci_dispatch.c` had
     * already written down that a nonzero return is "an infinite loop for one
     * that has failed"; the loop was here.
     *
     * So the two causes are separated by whether anything can change them:
     *
     *   **Permanent** - there is no record behind this endpoint at all, or the
     *   controller has failed terminally. Nothing this driver will ever do
     *   changes either: a reopen produces a *different* binding, and a failed
     *   controller needs a stop/start usbport will not perform while a thread
     *   waits. The transfer is completed with an error, which is the mechanism
     *   task 6-B.2 already established for a failed command chain - failing a
     *   transfer means accepting it and answering it.
     *
     *   **Transient** - a controller that is suspended or reinitialising really
     *   does come back, and an endpoint whose REMOVE crossed this call is
     *   followed by usbport's own reopen. Retry is right for both.
     *
     * **The REMOVE case deliberately stays a retry**, and that is a decision not
     * to overturn a considered one on a guess: task 6-B.5 chose it, and the run
     * did not establish *which* of these gates was the one looping. What the run
     * did establish is that some permanent refusal exists, and the two counters
     * below are what will name it next time - a climbing
     * `transfers refused for retry` with `transfers submitted` frozen is the
     * livelock, and `transfers failed - endpoint gone` is this fix working.
     */
    dev = xhciDevFromRef(ext, endpoint->DeviceIndex);
    if (dev == NULL || dev->State == XHCI_DEV_STATE_FREE ||
        (dev->Flags & XHCI_DEV_FLAG_DISOWNED) != 0 ||
        ext->ControllerFailed || ext->HcInfoStatus != XHCI_HC_OK) {
        /*
         * **`dev == NULL` is not the shape "there is nothing here" usually
         * takes**, and the first draft of this gate tested only that.
         * `xhciDevFromRef` bounds-checks an index and nothing else, so a record
         * that has been torn down and *released* still answers a non-NULL
         * pointer - with `EP0_OPEN` clear, which fell through to the retry
         * below. The permanent case therefore reached the permanent-retry path
         * it was written to remove. `XHCI_DEV_STATE_FREE` is what actually
         * names it; NULL only happens for an endpoint that was never bound.
         *
         * `HcInfoStatus` joins for the same reason: a controller whose window
         * would not decode is not going to start decoding, and
         * `xhciDevAdmitted` below folds that permanent condition together with
         * the genuinely transient `INITIALIZED`/`RUNNING` pair.
         *
         * The status follows the convention already in this file rather than a
         * new one: a device that has gone is `CANCELED`, exactly as the `GONE`
         * branch below answers, and a controller fault is
         * `INTERNAL_HC_ERROR`, exactly as the `FAILED` branch does. (The Win2000
         * DDK's `usbdi.h` has no `USBD_STATUS_DEVICE_GONE` - it is later-WDK
         * vocabulary, the same trap batch 6-A recorded for three other names.)
         */
        xhciDevFailEitherTransfer(ext, endpoint, transfer, parameters,
                                  isoParams,
                                  (ext->ControllerFailed ||
                                   ext->HcInfoStatus != XHCI_HC_OK)
                                      ? XHCI_USBD_STATUS_INTERNAL_HC_ERROR
                                      : XHCI_USBD_STATUS_CANCELED);
        ext->TransfersFailedGone++;
        XHCI_DBG_VALUE_CHANGED("slot: transfer failed - nothing behind this "
                               "endpoint, device index", endpoint->DeviceIndex);
        XhciControllerLockRelease(oldIrql);
        XhciSlotDeferredWork(ext);
        return MP_STATUS_SUCCESS;
    }
    if (!xhciDevAdmitted(ext)) {
        /*
         * **Deliberately not charged to the record** (task 7b-A.0). This is the
         * controller's state, not this device's, and every device has it at
         * once: a suspended controller really does come back, and the case where
         * it does not is already permanent above through `ControllerFailed` and
         * `HcInfoStatus`. Charging it here would let a resume that took longer
         * than XHCI_DEV_STALL_MS fail every device on the bus.
         */
        xhciDevTransferRefused(ext, NULL, 0);
        XhciControllerLockRelease(oldIrql);
        return MP_STATUS_NO_RESOURCES;
    }

    if (dev->State == XHCI_DEV_STATE_FAILED ||
        dev->State == XHCI_DEV_STATE_GONE) {
        xhciDevFailEitherTransfer(ext, endpoint, transfer, parameters,
                                  isoParams,
                                  (dev->State == XHCI_DEV_STATE_GONE)
                                      ? XHCI_USBD_STATUS_CANCELED
                                      : XHCI_USBD_STATUS_INTERNAL_HC_ERROR);
        XhciControllerLockRelease(oldIrql);
        XhciSlotDeferredWork(ext);
        return MP_STATUS_SUCCESS;
    }

    /*
     * Task 7a-A.2. Everything above is about the *device* and applies to every
     * pipe it has; everything below this branch is EP0's - the SET_ADDRESS
     * interception, the enumeration states, the Max Packet Size ordering - and
     * none of it means anything on a non-default endpoint.
     *
     * `XHCI_DEV_FLAG_EP0_OPEN` moved below the branch with them, which is the
     * one behavioural consequence: an interrupt or bulk transfer is no longer
     * refused because EP0 happens to be between a REMOVE and its reopen. The
     * bindings are independent and always were; only the gate conflated them.
     */
    if (endpoint->Dci > 1) {
        if (isoParams != NULL) {
            status = xhciSlotSubmitIsoNonDefault(ext, dev, endpoint, parameters,
                                                 transfer, isoParams);
        } else {
            status = xhciSlotSubmitNonDefault(ext, dev, endpoint, parameters,
                                              transfer, sgList, scratch,
                                              XHCI_XFER_MAX_CONTROL_TRBS);
        }
        XhciControllerLockRelease(oldIrql);
        XhciSlotDeferredWork(ext);
        return status;
    }

    if (isoParams != NULL) {
        /*
         * An isochronous request on the default control pipe. There is no such
         * USB endpoint, so this is not a phase boundary or a resource shortage -
         * it is a request that cannot be served by any driver, and failing it is
         * the only honest answer. Counted, because the alternative reading is
         * that this driver routed a callback to the wrong endpoint.
         */
        ext->IsoSubmitsWrongType++;
        xhciDevFailIsoTransfer(ext, endpoint, transfer, parameters, isoParams,
                               XHCI_USBD_STATUS_INVALID_PIPE_HANDLE);
        XhciControllerLockRelease(oldIrql);
        XhciSlotDeferredWork(ext);
        return MP_STATUS_SUCCESS;
    }

    if ((dev->Flags & XHCI_DEV_FLAG_EP0_OPEN) == 0) {
        xhciDevTransferRefused(ext, dev, 0);
        XhciControllerLockRelease(oldIrql);
        return MP_STATUS_NO_RESOURCES;
    }

    if (xhciDevIsSetAddress(parameters)) {
        ULONG address;

        /*
         * Task 6-B.3. The address is wValue's low byte; usbport allocates it
         * from its own bitmap counting up from 1, and it is **unrelated** to the
         * Slot ID even when the two numbers coincide
         * (docs/contributing/implementation-invariants.md, "Device Addressing"). The map is
         * keyed on this number and nothing is ever inferred from equality.
         */
        address = (ULONG)(parameters->SetupPacket.wValue & 0x00FFU);
        if (dev->State != XHCI_DEV_STATE_DEFAULT ||
            dev->PendingSetAddress != NULL) {
            /* The slot is not in the Default state yet, or one interception is
             * already outstanding. Retry: this is the ordinary shape of usbport
             * submitting while the BSR = 1 chain is still running. */
            xhciDevTransferRefused(ext, dev, 0);
            XhciControllerLockRelease(oldIrql);
            return MP_STATUS_NO_RESOURCES;
        }
        if (address == 0 || address > USBPORT_MAX_DEVICE_ADDRESS ||
            xhciDevByAddress(ext, address) != NULL) {
            /* An address of 0, out of range, or one another record already
             * holds. Failing it beats addressing two devices the same. */
            xhciDevFailTransfer(ext, endpoint, transfer, parameters,
                                XHCI_USBD_STATUS_INTERNAL_HC_ERROR);
            XHCI_DBG_VALUE_CHANGED("slot: refused SET_ADDRESS for address",
                                   address);
            XhciControllerLockRelease(oldIrql);
            XhciSlotDeferredWork(ext);
            return MP_STATUS_SUCCESS;
        }

        /*
         * Through the shared stamp for the reason its comment gives: this
         * record is a recycled extension whose previous tenant may have been
         * isochronous, and the Address Device completion (or a cancellation)
         * routes it into the same drain that reads `Flags` and `IsoParams`.
         */
        xhciDevStampTransfer(endpoint, transfer, parameters,
                             XHCI_USBD_STATUS_SUCCESS);

        dev->DeviceAddress = address;
        /*
         * The topology node follows the address, and whatever stale node sat
         * under the newly assigned one is pruned before it can hand this
         * device a departed hub's position (Phase 7 review, findings A4/A5).
         * Before the command completes rather than after, because from this
         * line on the address names this record - the map check above just
         * enforced that - and a failure path that skipped the migration would
         * leave the graph keyed on an address the record now holds.
         */
        XhciTopoMigrate(&ext->Topology, dev->TopoAddress, address);
        if (dev->TopoAddress != 0) {
            dev->TopoAddress = address;
        }
        /* `PendingOp` is deliberately NOT written: the need is derived from
         * this pointer in `xhciDevOwedOp`, so nothing downstream can overwrite
         * it and the transfer survives a chain that has to run first. */
        dev->PendingSetAddress = transfer;
        ext->SetAddressIntercepts++;

        XhciControllerLockRelease(oldIrql);
        XhciSlotDeferredWork(ext);
        return MP_STATUS_SUCCESS;
    }

    if (dev->State != XHCI_DEV_STATE_DEFAULT &&
        dev->State != XHCI_DEV_STATE_ADDRESSED) {
        /* Still enabling or addressing. Queued for retry, and the retry is asked
         * for when the chain completes rather than waited for. */
        xhciDevTransferRefused(ext, dev, 0);
        XhciControllerLockRelease(oldIrql);
        return MP_STATUS_NO_RESOURCES;
    }
    if (dev->PendingOp == XHCI_DEV_OP_EVALUATE_MPS ||
        dev->ActiveOp == XHCI_DEV_OP_EVALUATE_MPS) {
        /*
         * Task 6-B.4's ordering clause: the corrected Max Packet Size must reach
         * the endpoint context "before subsequent control traffic". A transfer
         * built against the old MPS0 would compute its TD Size field from a
         * packet size the xHC no longer uses.
         */
        xhciDevTransferRefused(ext, dev, 0);
        XhciControllerLockRelease(oldIrql);
        return MP_STATUS_NO_RESOURCES;
    }

    /* Batch 7a-B's gate, for the same reason and in the same place as the
     * interrupt path's: EP0 is halted or quiescing, and a TRB placed now would
     * be executed from a position the chain is about to reprogram. The
     * SET_ADDRESS interception above is deliberately ahead of it - it never
     * reaches a ring, so no endpoint state can stop it. */
    if (xhciEpResolve(dev, 1, &binding)) {
        switch (xhciEpSubmitGate(ext, dev, &binding)) {
        case XHCI_EPQ_GATE_RETRY:
            xhciDevTransferRefused(ext, dev, 0);
            XhciControllerLockRelease(oldIrql);
            XhciSlotDeferredWork(ext);
            return MP_STATUS_NO_RESOURCES;
        case XHCI_EPQ_GATE_FAIL_STALL:
        case XHCI_EPQ_GATE_FAIL_HC:
            xhciDevFailTransfer(ext, endpoint, transfer, parameters,
                                XHCI_USBD_STATUS_INTERNAL_HC_ERROR);
            XhciControllerLockRelease(oldIrql);
            XhciSlotDeferredWork(ext);
            return MP_STATUS_SUCCESS;
        default:
            break;
        }
    }

    request.Setup = parameters->SetupPacket;
    request.TransferLength = parameters->TransferBufferLength;
    request.TransferFlagsIn = parameters->TransferFlags & 1UL;
    request.MaxPacketSize = dev->MaxPacketSize0;
    request.SgList = sgList;

    transfer->EndpointExtension = endpoint;
    censoredBefore = dev->Ep0Queue.MidTdTailsCensored;
    built = XhciXferSubmitControl(&dev->Ep0Queue, &dev->Ep0Ring, &request,
                                  transfer, parameters, scratch,
                                  XHCI_XFER_MAX_CONTROL_TRBS);
    xhciDevFoldCensored(ext, &dev->Ep0Queue, censoredBefore);
    if (built == XHCI_XFER_OK) {
        xhciDevTransferSubmitted(ext, dev);
        /* Task 7b-A.1: placed, so the topology graph may read it - a refused
         * transfer will come back and must not be counted twice. */
        xhciDevTopoSnoopSubmit(ext, dev, transfer, parameters, sgList);
        /* Task 9-A.2, on the same terms and from the same placement. */
        xhciDevDescSnoopSubmit(ext, dev, transfer, parameters, sgList);
        /*
         * The doorbell is rung inside the lock, with the TD already published:
         * "all ring/TRB memory writes must hit memory before the doorbell"
         * (docs/usb-xhci-info/xhci-data-structures.md section 8), and WRITE_REGISTER_ULONG
         * serialises on x86. DCI 1 is EP0 (4.5.1).
         */
        XhciWriteDoorbell(ext, dev->SlotId, 1);
        /* And the ring stops being software's the moment it is rung - see the
         * interrupt path for why those bits must not survive a doorbell. */
        dev->Ep0Quiesce.Flags &= ~(XHCI_EPQ_STOPPED | XHCI_EPQ_PAUSED);
        dev->Ep0Quiesce.StoppedArmed = 0;
    } else if (built == XHCI_XFER_BUSY) {
        /* The ring cannot hold the whole group and nothing was written - the
         * one status usbport's requeue is exactly right for. EP0 gets the same
         * latch as a bulk endpoint, though it is far less likely to need it: a
         * control ring holding three TDs per transfer can fill under a burst of
         * enumeration traffic, and the reason to share the mechanism is that a
         * second way of handling the same condition is what drifts. */
        XhciXferQueueArmRetry(&dev->Ep0Queue, &dev->Ep0Ring);
        ext->TransfersRefusedRingFull++;
        xhciDevTransferRefused(ext, dev, 1);
        status = MP_STATUS_NO_RESOURCES;
    } else {
        /*
         * The request itself is unbuildable: a scatter/gather list that does not
         * tile the buffer, a physical address above 4 GB, a direction the setup
         * bytes and the transfer flags disagree about. None of those get better
         * on a retry.
         */
        xhciDevFailTransfer(ext, endpoint, transfer, parameters,
                            XHCI_USBD_STATUS_INTERNAL_HC_ERROR);
        XHCI_DBG_VALUE_CHANGED("slot: control transfer refused by the builder, "
                               "status", built);
    }

    XhciControllerLockRelease(oldIrql);
    XhciSlotDeferredWork(ext);
    return status;
}

/*
 * Task 7a-B.2, and what makes it a state machine rather than a queue edit is a
 * fact batch 7a-0 read out of both shipping builds: **nothing usbport owns
 * survives the return.** The transfer record is `ExFreePool`d in the same
 * endpoint-worker pass (SP4 `0x18973`), the miniport transfer extension is
 * interior to it, and `USBPORT_CompleteTransfer` releases the DMA map registers
 * on the way past (`0x18764`/`0x187EE`). So:
 *
 *   - **Nothing usbport owns is held afterwards.** The bytes moved are read
 *     during the callback and everything the Stop Endpoint / Set TR Dequeue
 *     chain needs later is derived from this driver's own ring and queue.
 *   - **The aborted transfer is never completed.** usbport completes it itself
 *     with `USBD_STATUS_CANCELED`, using the length written through arg 4;
 *     answering it again would be a double-complete on freed memory. Taking it
 *     off the queue is what makes that impossible, and it is why the queue
 *     removal happens here rather than at the stop's completion.
 *   - **Its TRBs are still live.** `XhciXferQueueRemove` is software
 *     bookkeeping; the ring still holds the TD and the xHC may execute it
 *     against pages usbport is about to unmap. The Stop Endpoint armed below is
 *     the only thing that ends that, and `SetEndpointState(PAUSED)` has usually
 *     started it already.
 *
 * IRQL: DISPATCH_LEVEL, under MiniportSpinLock, controller lock not held.
 */
VOID XhciSlotAbortTransfer(PXHCI_EXTENSION ext,
                           PXHCI_ENDPOINT endpoint,
                           PXHCI_TRANSFER transfer,
                           PULONG completedLength)
{
    XHCI_EP_BINDING binding;
    KIRQL oldIrql;
    PXHCI_DEVICE dev;
    ULONG moved;
    ULONG removed;
    ULONG lostBefore;

    moved = 0;
    removed = 0;

    if (ext != NULL && ext->Signature == XHCI_EXTENSION_SIGNATURE &&
        endpoint != NULL && endpoint->Signature == XHCI_ENDPOINT_SIGNATURE &&
        transfer != NULL) {
        XhciControllerLockAcquire(&oldIrql);

        dev = xhciDevFromRef(ext, endpoint->DeviceIndex);
        if (dev != NULL) {
            if (dev->PendingSetAddress == transfer) {
                /* An intercepted SET_ADDRESS: it owns no TRBs, so detaching it
                 * is the whole cancellation - there is nothing on any ring to
                 * stop. The Address Device command it was waiting for stays
                 * outstanding and its completion will find no transfer to
                 * complete, which is the case that path checks. */
                dev->PendingSetAddress = NULL;
                removed = 1;
            } else if (xhciEpResolve(dev, endpoint->Dci, &binding)) {
                ULONG bytes;

                /*
                 * Read before the removal and published only if the removal
                 * succeeded. A transfer this driver does not hold has moved
                 * nothing *as far as this driver knows*, and reporting a number
                 * out of a record whose ownership is in doubt is worse than
                 * reporting the zero usbport pre-set (SP4 `0x1691A`).
                 */
                bytes = transfer->BytesTransferred;
                lostBefore = binding.Queue->MidTdDeferralsLost;
                if (XhciXferQueueRemove(binding.Queue, transfer)) {
                    /* Same as the drain's: an abort answers nothing about the
                     * tail this transfer may have been deferred for. Folded
                     * inside the success branch, because a removal that found
                     * nothing moved nothing (task 9-0.2). */
                    xhciDevFoldDeferralsLost(ext, binding.Queue, lostBefore);
                    moved = bytes;
                    removed = 1;
                    ext->TransfersAborted++;
                    /*
                     * **The window this cannot close, measured rather than
                     * asserted.** `XHCI_EPQ_PAUSED` starts the Stop Endpoint at
                     * least one of usbport's frame gates earlier, but nothing
                     * makes the command *complete* before this callback runs,
                     * and this callback may not wait for it. If the stop is
                     * still outstanding here, the xHC can execute the aborted
                     * TD's TRBs after usbport unmaps their buffer - so it is
                     * counted, because a claim that the pass runs against a
                     * stopped endpoint is only true when it is.
                     */
                    if ((binding.Quiesce->Flags & XHCI_EPQ_STOPPED) == 0) {
                        ext->AbortsBeforeStopped++;
                    }
                    /*
                     * No drain: everything still on this queue is a transfer
                     * usbport has *not* withdrawn, and the placement that
                     * follows the stop keeps it - the cancelled TD's TRBs are
                     * rewritten as No Ops around it. The removal is also what
                     * makes the placement necessary, which is what
                     * `XHCI_EPQ_REPOSITION` says.
                     */
                    xhciEpOweReposition(binding.Quiesce, 0, 0);
                    xhciEpArmIfBusy(ext, dev, &binding, 0);
                }
            }
        }
        /*
         * **The completion-side searches must not depend on the record
         * resolving.** A Disable Slot completion releases the device record -
         * zeroing it - with its retired transfers parked on the completion
         * list, and the `SubmitDepth` hold can keep them parked across this
         * callback. An abort landing in that window finds `xhciEpResolve`
         * failing on the zeroed record; if these searches lived inside that
         * resolve, the abort would fall through to `AbortsUnmatched`, usbport
         * would free the record on return, and the deferred drain would call
         * `UsbPortCompleteTransfer` through freed memory - the exact failure
         * the list-search comment at `xhciDevTakeCompletion` exists to
         * prevent. So they run whenever the queue removal did not claim the
         * transfer, whatever the reason the queue removal did not.
         */
        if (!removed && ext->CompletingTransfer == transfer) {
            /*
             * Another context has it off both lists and is inside
             * `UsbPortCompleteTransfer` for it right now. Nothing here
             * can recall that call - which is the SMP window this
             * driver cannot close - but the abort can at least answer
             * with the length that transfer measured rather than a zero,
             * and the case is countable instead of silent.
             */
            moved = transfer->BytesTransferred;
            removed = 1;
            ext->TransfersAborted++;
            ext->AbortsDuringCompletion++;
        } else if (!removed && xhciDevTakeCompletion(ext, transfer)) {
            /*
             * A Transfer Event claimed it between usbport deciding to
             * cancel it and this call, and it was waiting for
             * `UsbPortCompleteTransfer` with the lock dropped. usbport
             * frees the record on this return, so the answer it is
             * waiting for must not be given - and the length the event
             * measured is the right one to report.
             */
            moved = transfer->BytesTransferred;
            removed = 1;
            ext->TransfersAborted++;
            ext->AbortsFromCompletionList++;
        }
        if (!removed) {
            /*
             * It never reached a ring, or it was completed and answered before
             * usbport got here. Answered with the zero usbport already wrote,
             * and counted so that case can be told from a queue this driver lost
             * track of.
             */
            ext->AbortsUnmatched++;
        }

        XhciControllerLockRelease(oldIrql);
    }

    if (completedLength != NULL) {
        *completedLength = moved;
    }

    /*
     * The stop is pumped now rather than left for the next callback, because the
     * window this whole path is about is measured from *this return*: usbport
     * frees the transfer and its mapping a few hundred instructions later.
     *
     * Completing *other* transfers from inside this callback is safe, and the
     * reason is the one that matters for every callback except `SubmitTransfer`
     * (both binaries): `UsbPortCompleteTransfer` re-enters no
     * miniport slot - it unlinks the transfer, queues it to the FDO done list,
     * queues a DPC and sets a worker event. So the only hazard a completion can
     * carry is what the *caller* touches after this callback returns, and the
     * abort path's caller does not touch a transfer this driver just handed
     * back. `SubmitTransfer` is the one that does, which is why the
     * `SubmitDepth` bracket exists and is confined to it.
     */
    XhciSlotDeferredWork(ext);
}

/* ------------------------------------------------------------------ */
/* Events                                                              */
/* ------------------------------------------------------------------ */

/*
 * Task 9-A.1: one Transfer Event on an **isochronous** ring.
 *
 * Split from `XhciSlotTransferEvent`'s body rather than branching inside it,
 * because the two share only the routing above them: an isoch endpoint never
 * halts, so no *halt* escalation is ever right - a TRB Error is the one code
 * that still needs software, and it takes the drain rather than the halt path;
 * its completions carry a status per packet rather than one for the request; and
 * two of its completion codes name no TD at all and have to be answered before
 * anything resolves a pointer.
 *
 * Called with the controller lock held. IRQL: DISPATCH_LEVEL.
 */
static ULONG xhciDevIsoTransferEvent(PXHCI_EXTENSION ext,
                                     PXHCI_DEVICE dev,
                                     PXHCI_EP_BINDING binding,
                                     ULONG slotId,
                                     ULONG dci,
                                     const XHCI_TRB *event,
                                     ULONG completionCode)
{
    XHCI_XFER_EVENT_RESULT result;
    PXHCI_TRANSFER_QUEUE queue;
    PXHCI_RING ring;
    ULONG shortBefore;
    ULONG answeredBefore;
    ULONG errorsBefore;
    ULONG missedBefore;
    ULONG awaitingBefore;
    ULONG unmatchedBefore;

    ring = binding->Ring;
    queue = binding->Queue;

    /*
     * **Ring Underrun and Ring Overrun are answered here and go no further.**
     *
     * Their TRB Pointer is "the value of the Dequeue Pointer where the Overrun or
     * Ring Underrun condition was detected" on a 1.1+ controller and zero on a
     * pre-1.1 one (4.10.3.1 p.185) - so it is either nothing or a live ring
     * address that belongs to no outstanding TD, and letting it reach the
     * per-packet matcher would attribute an underrun to whatever TD happens to
     * sit there.
     *
     * Neither is a fault and neither needs recovery: "the endpoint shall remain
     * in the Running state, and be removed from the Pipe Schedule", and it goes
     * back on "the next time system software rings the doorbell" (p.185 - the
     * p.185 citation above covers only the event-pointer rule, not the return
     * to the schedule). So the answer
     * is a doorbell if there is anything to run - which there is exactly when a
     * submission arrived between the controller detecting the empty ring and
     * this event being drained, and which the ordinary submit path's own
     * doorbell may already have covered. Ringing it twice costs nothing; not
     * ringing it at all leaves a stream that will never restart.
     *
     * **But it is still a doorbell, so it obeys the quiescence machine like
     * every other one in this file.** These events name no TD and can be
     * stale - an abort or an event-ring loss can put one in front of the drain
     * after a Stop Endpoint has already left the endpoint Stopped with
     * survivors queued. Ringing then returns the endpoint to Running while the
     * quiescence machine still believes software owns the ring: the placement
     * that follows rewrites TRBs the xHC is executing, or the in-flight Set TR
     * Dequeue Pointer fails with Context State Error (4.6.10 permits it in the
     * Stopped and Error states only) and `xhciEpQuiesceFail` kills the pipe.
     * A restart that is owed is the quiescence/reset machinery's to give -
     * `xhciEpRestartIfStopped` and `xhciEpHonourRestart` are gated for exactly
     * this reason - so a declined ring here loses nothing.
     */
    if (completionCode == XHCI_CC_RING_UNDERRUN ||
        completionCode == XHCI_CC_RING_OVERRUN) {
        if (completionCode == XHCI_CC_RING_UNDERRUN) {
            ext->IsoRingUnderruns++;
        } else {
            ext->IsoRingOverruns++;
        }
        if (queue->Count == 0 || dev->SlotId == 0) {
            /*
             * Nothing queued, so nothing to restart. Ordinary at the end of a
             * stream - the client stops posting and the ring runs dry once -
             * and counted apart because a *rising* reading with submissions
             * still happening is the one shape that says the driver and the
             * controller disagree about whether this pipe is running.
             */
            ext->IsoEventsUnattributed++;
        } else if ((binding->Quiesce->Flags &
                    (XHCI_EPQ_INFLIGHT | XHCI_EPQ_STOPPED | XHCI_EPQ_PAUSED |
                     XHCI_EPQ_HALTED | XHCI_EPQ_FAILED |
                     XHCI_EPQ_NO_CONTEXT)) != 0) {
            /*
             * **Its own counter, because it is the opposite diagnosis.** There
             * is work queued and a slot to ring, and the doorbell is withheld
             * on purpose: the quiescence machine owns this endpoint and a
             * restart it is owed is its to give. That is a healthy, expected
             * suppression - a stale underrun racing a Stop Endpoint is exactly
             * what this gate exists for.
             *
             * `IsoEventsUnattributed` means the driver and the controller
             * disagree about whether the pipe is running, and a *rising*
             * reading with submissions still happening is the shape that says
             * so. Folding a deliberate suppression into it would manufacture
             * precisely that shape out of the healthy case, on the one counter
             * whose whole value is that reading - and this repository's rule
             * since Phase 4 task 8 is that two causes with opposite diagnoses
             * may not share a counter in a release build.
             */
            ext->IsoDoorbellsSuppressed++;
        } else {
            XhciWriteDoorbell(ext, dev->SlotId, dci);
            binding->Quiesce->StoppedArmed = 0;
        }
        XHCI_DBG_VALUE_CHANGED("slot: isoch ring ran dry, dci << 8 | code",
                               (dci << 8) | completionCode);
        return 0;
    }

    shortBefore = queue->ShortPackets;
    answeredBefore = queue->IsoPacketsAnswered;
    errorsBefore = queue->IsoPacketErrors;
    missedBefore = queue->IsoMissedService;
    awaitingBefore = queue->IsoGroupsAwaitingTail;
    unmatchedBefore = queue->UnmatchedEvents;

    if (XhciXferIsoEvent(queue, ring, slotId, dci, event->Param0,
                         event->Status, event->Control,
                         &result) != XHCI_XFER_OK) {
        return 0;
    }

    ext->ShortPacketsTotal += queue->ShortPackets - shortBefore;
    ext->IsoPacketsAnsweredTotal += queue->IsoPacketsAnswered - answeredBefore;
    ext->IsoPacketErrorsTotal += queue->IsoPacketErrors - errorsBefore;
    ext->IsoMissedServiceTotal += queue->IsoMissedService - missedBefore;
    ext->IsoGroupsAwaitingTailTotal +=
        queue->IsoGroupsAwaitingTail - awaitingBefore;
    ext->UnmatchedEventsTotal += queue->UnmatchedEvents - unmatchedBefore;

    if (result.Action == XHCI_XFER_ACTION_COMPLETE) {
        ext->TransfersCompleted += result.CompletedCount;
        xhciDevOweCompletion(ext, result.Completed);
        xhciEpReleaseRetry(ext, dev, binding);
    }
    if (result.RefusedRetire) {
        /*
         * The ring refused the retire, so the group is still queued on a running
         * endpoint. Answered exactly as the bulk path answers its own version -
         * a Stop Endpoint with a drain continuation, which is the only thing
         * that makes both the completion and a repositioning legal. **Not**
         * `xhciEpRecoveryNeeded`: that would read the completion code, and on an
         * isoch pipe no completion code means "halted".
         */
        xhciEpRefusedRetireRecovery(ext, dev, binding);
    } else if (result.NeedsRecovery) {
        /*
         * Only a TRB Error reaches here on an isochronous ring - an isoch
         * endpoint does not halt, and the Stopped family never gets this far
         * because `XhciSlotTransferEvent` takes those events out before the
         * queue sees them.
         *
         * **It does NOT go through `xhciEpRecoveryNeeded`, and the first version
         * of this branch did.** That function's TRB Error case programs a Set TR
         * Dequeue Pointer at the ring's *current* dequeue position and then lets
         * the endpoint restart from it. Its unstated precondition is the bulk and
         * interrupt path's: by the time recovery runs, the event path has already
         * retired the TD that failed, so "the current dequeue" is the *next* TD.
         * On an isochronous ring that is false by construction - a TRB Error
         * mid-group retires nothing, so the dequeue still names the head of the
         * very TD that faulted, and 4.10.1 p.172 says the opposite: "software
         * shall use a Set TR Dequeue Pointer Command to advance the Transfer Ring
         * to the **next** TD". Programming the failed TD's own head and ringing
         * the doorbell re-executes it, which for a deterministic TRB Error - a
         * malformed TRB, i.e. this driver's own encoding - is an endless loop,
         * and for a transient one repeats a packet already stamped as answered.
         *
         * This is [[phase4-task5-run-and-port-power]]'s rule again: **copy a
         * rule's precondition, not its shape.** So the recovery here is the one
         * whose precondition does hold - the same drain the refused retire above
         * uses. It stops the endpoint, cancels what is queued *after* ownership
         * has transferred, and only then places the dequeue, which by then has an
         * empty queue and targets the enqueue position. Nothing re-runs, and
         * nothing is answered while the xHC may still own it.
         */
        xhciEpIsoErrorRecovery(ext, dev, binding);
    }
    /*
     * An isochronous endpoint does not halt, so nothing above this line would
     * have noticed a code that is fatal to the slot: `xhciEventNeedsRecovery`
     * lets only a TRB Error through on this ring. Table 6-90 scopes Incompatible
     * Device Error to the Slot and not to the endpoint type, so it is answered
     * here on the same terms as on a bulk ring.
     */
    xhciDevSlotFatalEvent(ext, dev, completionCode);
    return result.Fatal ? 1UL : 0UL;
}

/* IRQL: DISPATCH_LEVEL, controller lock held by the DPC. */
ULONG XhciSlotTransferEvent(PXHCI_EXTENSION ext, const XHCI_TRB *event)
{
    XHCI_XFER_EVENT_RESULT result;
    XHCI_EP_BINDING binding;
    PXHCI_DEVICE dev;
    PXHCI_RING ring;
    PXHCI_TRANSFER_QUEUE queue;
    ULONG slotId;
    ULONG dci;
    ULONG completionCode;
    ULONG shortBefore;
    ULONG shortSuccessBefore;
    ULONG intermediateBefore;
    ULONG midTdShortBefore;
    ULONG midTdTailBefore;
    ULONG midTdDroppedBefore;
    ULONG midTdDeferralsBefore;
    ULONG midTdTailedBefore;
    ULONG midTdTailedSpuriousBefore;
    ULONG midTdLostBefore;
    ULONG unmatchedBefore;

    if (ext == NULL || event == NULL) {
        return 0;
    }

    slotId = XHCI_TRB_GET_SLOT_ID(event->Control);
    dci = XHCI_TRB_GET_EP_ID(event->Control);
    completionCode = XHCI_TRB_GET_COMPLETION(event->Status);
    dev = xhciDevBySlotId(ext, slotId);

    /*
     * Which ring this event belongs to. EP0's lives on the device record and
     * every other endpoint's on its own record, so the DCI is what selects it -
     * and an event naming a DCI nothing has open resolves to neither.
     *
     * `xhciEpResolve` is that selection, shared with the whole batch 7a-B family
     * so that "which endpoint is this" has one answer in the file. Note it maps
     * DCI 0 onto EP0, which this path must not: an event naming DCI 0 names no
     * endpoint, so it is excluded before the call rather than after it.
     */
    ring = NULL;
    queue = NULL;
    if (dev != NULL && dci != 0 && xhciEpResolve(dev, dci, &binding)) {
        ring = binding.Ring;
        queue = binding.Queue;
    }

    /*
     * A Transfer Event naming a slot or an endpoint no record has open. It is
     * counted and dropped here rather than inside the queue layer, because that
     * layer is told which ring it is looking at and cannot be the place an event
     * belonging to *no* ring is noticed (see XhciXferEvent's contract).
     */
    if (ring == NULL) {
        ext->TransferEventsForeign++;
        XHCI_DBG_VALUE_CHANGED("slot: transfer event for no open endpoint, "
                               "slot << 8 | dci", (slotId << 8) | dci);
        return 0;
    }
    if (event->Param1 != 0) {
        /* A TRB pointer above 4 GB, which no ring this driver owns can be at.
         * Same rule as the command engine's: checked rather than discarded. */
        ext->TransferEventsForeign++;
        XHCI_DBG_VALUE_CHANGED("slot: transfer event pointer above 4 GB",
                               event->Param1);
        return 0;
    }

    /*
     * **Task 11-V.9's fourth tier: budgeted, then counted.** Transfer error
     * completion codes are the one producer whose rate the driver does not
     * control - a device that has come loose can produce one per microframe -
     * so each code is worth XHCI_LOG_ERROR_BUDGET records and after that the
     * existing counters carry it, with `LogErrorsOverBudget` saying so
     * explicitly. A diagnostic that quietly stopped being complete is worse
     * than one that says where it stopped.
     *
     * The gate is "not one of the ordinary outcomes" rather than a list of
     * errors, because the list of errors is Table 6-90's and this driver must
     * not carry a second copy of it that can drift. Success, Short Packet and
     * the Stopped family (26-28) are what an ordinary bus produces; everything
     * else is worth a reader's attention on a bus that is not working.
     *
     * `XHCI_LOG_ERROR_BUDGET` per *code*, not overall: a run whose bulk
     * endpoint is stalling and whose interrupt endpoint is babbling should show
     * both, and one flooding code must not spend the other's budget.
     */
    if (completionCode != XHCI_CC_SUCCESS &&
        completionCode != XHCI_CC_SHORT_PACKET &&
        (completionCode < XHCI_CC_STOPPED ||
         completionCode > XHCI_CC_STOPPED_SHORT_PACKET)) {
        if (XhciLogErrorBudget(&ext->Log, completionCode)) {
            XhciLogNoteLocked(ext, "xfer.error",
                              ((ULONG)slotId << 16) | (dci << 8) |
                                  completionCode);
        } else {
            ext->LogErrorsOverBudget++;
        }
    }

    /*
     * **A Stopped Transfer Event is a statement about the ring, not about a
     * transfer, and it never reaches the queue.**
     *
     * Spec 4.6.9 p.122: "The xHC shall generate a Stopped Transfer Event every
     * time a Transfer Ring is stopped with a Stop Endpoint Command ... The
     * forced Stopped Transfer Event explicitly indicates to software that the
     * selected Transfer Ring has stopped. If a Transfer Ring is empty when a
     * Stop Endpoint Command is issued, a Stopped Transfer Event shall be
     * generated" - so one arrives for a stop of a *running* ring whether or not
     * anything was executing, and it is "forced ... irrespective of whether its
     * IOC or ISP flags are set". There is exactly one stated exception, and it
     * is why this must not be counted as one-per-stop: "If a Transfer Ring has
     * been Halted due to error condition when a Stop Endpoint Command is
     * received, no Stopped Transfer Event shall be generated" (p.124).
     *
     * Routed into `XhciXferEvent` it would land on whichever transfer owns the
     * TRB the endpoint stopped on and complete it as cancelled, sweeping every
     * transfer ahead of it with it. That is wrong for every stop this batch
     * issues except a drain: a `SetEndpointState(PAUSED)` pre-emption cancels
     * nothing, and an abort deliberately keeps the transfers usbport did not
     * withdraw. The TD is untouched on the ring and the placement that follows
     * restarts it from its head.
     *
     * Codes 26-28 have exactly one producer - a Stop Endpoint command, which
     * only this driver issues (Table 6-90 assigns them to no other event) - so
     * there is no case where one means something else.
     */
    if (completionCode >= XHCI_CC_STOPPED &&
        completionCode <= XHCI_CC_STOPPED_SHORT_PACKET) {
        /*
         * **It does not set `XHCI_EPQ_STOPPED`.** That bit licenses rewriting
         * TRBs, so it may only be set by something that establishes the endpoint
         * really is in the Stopped state - which is the Stop Endpoint's own
         * completion code, read against the Endpoint Context where it is a
         * Context State Error. An endpoint in the Error state is a live example:
         * the stop there "shall have no effect" (4.8.3 p.149), and taking this
         * event as proof of ownership would license a rewrite of TRBs the xHC
         * still holds.
         */
        ext->EndpointStoppedEvents++;
        /*
         * **What it measured is kept even though the event completes nothing.**
         * Code 26 carries "the residual bytes to transfer" for the interrupted
         * TRB (4.6.9 p.122), and a stopped TD produces no completion of its own -
         * so this is the only event that ever measures a cancelled transfer, and
         * `AbortTransfer` has to report those bytes through its OUT parameter.
         * Discarding it made that report the zero the record was created with.
         */
        if (XhciXferQueueStopped(queue, ring, event->Param0, event->Status)) {
            ext->StoppedLengthsLatched++;
        }
        XHCI_DBG_VALUE_CHANGED("slot: the ring stopped, dci << 8 | code",
                               (dci << 8) | completionCode);
        return 0;
    }

    /*
     * Task 9-A.1. The isochronous engine is a different machine below this line
     * and takes the event whole; the Stopped family above it is deliberately
     * *not* part of that split, because a Stop Endpoint means the same thing on
     * every transfer ring and its forced event has one reader.
     */
    if (ring->Kind == XHCI_RING_KIND_ISOCH) {
        return xhciDevIsoTransferEvent(ext, dev, &binding, slotId, dci, event,
                                       completionCode);
    }

    /*
     * The queue counts these per endpoint and dies with the device record, so
     * the difference across this one call is what makes them survive an unplug.
     * Taken as a delta rather than by re-deriving the classification here:
     * `XhciXferEvent` is the only thing that knows which of the three an event
     * was, and a second opinion in this file would be a copy that can drift.
     */
    shortBefore = queue->ShortPackets;
    shortSuccessBefore = queue->ShortSuccesses;
    intermediateBefore = queue->IntermediateEvents;
    midTdShortBefore = queue->MidTdShortRetires;
    midTdTailBefore = queue->MidTdShortTails;
    midTdDroppedBefore = queue->MidTdTailsDropped;
    midTdDeferralsBefore = queue->MidTdDeferrals;
    midTdTailedBefore = queue->MidTdDeferralsTailed;
    midTdTailedSpuriousBefore = queue->MidTdDeferralsTailedSpurious;
    midTdLostBefore = queue->MidTdDeferralsLost;
    unmatchedBefore = queue->UnmatchedEvents;

    if (XhciXferEvent(queue, ring, slotId, dci,
                      event->Param0, event->Status, event->Control,
                      &result) != XHCI_XFER_OK) {
        return 0;
    }

    ext->ShortPacketsTotal += queue->ShortPackets - shortBefore;
    ext->ShortSuccessesTotal += queue->ShortSuccesses - shortSuccessBefore;
    ext->IntermediateEventsTotal +=
        queue->IntermediateEvents - intermediateBefore;
    ext->MidTdShortRetiresTotal +=
        queue->MidTdShortRetires - midTdShortBefore;
    /* The conformance half of that reading, and the eviction that would make it
     * unreadable - both folded here for the same reason the four above are: the
     * queue dies with the device record, and the question is about the run.
     * `UnmatchedEvents` joins them because the mid-TD departure's
     * no-double-completion argument rests on it, and it had no reader at all. */
    ext->MidTdShortTailsTotal += queue->MidTdShortTails - midTdTailBefore;
    xhciDevFoldTailsDropped(ext, queue, midTdDroppedBefore);
    ext->UnmatchedEventsTotal += queue->UnmatchedEvents - unmatchedBefore;
    /*
     * Task 9-0.2's partition. Four of its five terms move here: an event arms a
     * deferral, an in-band tail resolves one - strictly, or by the Success-code
     * quirk - and a sweep or a refused classification can unlink one. The
     * fifth, the settle's early retire, is `MidTdShortRetires` above.
     *
     * `MidTdDeferPending` is **bumped, not assigned**: this is the *hint* that
     * says a walk is worth doing, and it may only over-state. So an interim
     * value above one is ordinary rather than corrupt - several arms can land
     * between two settle passes. Its exact value is `XhciSlotDrainSettled`'s to
     * compute, and it assigns it there.
     */
    ext->MidTdDeferralsTotal += queue->MidTdDeferrals - midTdDeferralsBefore;
    ext->MidTdDeferralsTailedTotal +=
        queue->MidTdDeferralsTailed - midTdTailedBefore;
    ext->MidTdDeferralsTailedSpuriousTotal +=
        queue->MidTdDeferralsTailedSpurious - midTdTailedSpuriousBefore;
    xhciDevFoldDeferralsLost(ext, queue, midTdLostBefore);
    if (queue->MidTdDeferrals != midTdDeferralsBefore) {
        ext->MidTdDeferPending++;
    }

    if (result.Action == XHCI_XFER_ACTION_COMPLETE) {
        ext->TransfersCompleted += result.CompletedCount;
        xhciDevOweCompletion(ext, result.Completed);
        /*
         * Task 8-A.1: retiring the TD is what put TRBs back on the ring, so this
         * is the moment a submission refused for ring-full becomes offerable
         * again. After the completion is posted rather than before, because the
         * free count the latch compares against is only correct once the retire
         * inside `XhciXferEvent` has moved the dequeue pointer.
         */
        xhciEpReleaseRetry(ext, dev, &binding);
    }
    if (result.RefusedRetire) {
        /*
         * **A record/ring divergence, not a halt.** Nothing was retired and the
         * transfer is still queued on a Running endpoint, so this needs a Stop
         * Endpoint rather than the halt recovery below - which is what the
         * completion code alone would have selected, because it is a Short
         * Packet. See `xhciEpRefusedRetireRecovery`.
         */
        xhciEpRefusedRetireRecovery(ext, dev, &binding);
    } else if (result.NeedsRecovery) {
        /*
         * The endpoint will not run again until software acts, and *which* act
         * depends on why - a halt needs Reset Endpoint, a stop this driver asked
         * for needs only the Set TR Dequeue Pointer already in flight. That
         * decision is `xhciEpRecoveryNeeded`'s (task 7a-B.3), and it reads the
         * completion code because the ring layer sets this one flag for both.
         */
        xhciEpRecoveryNeeded(ext, dev, &binding, completionCode);
    }
    /*
     * After the recovery decision rather than instead of it: the endpoint-level
     * answer is what retires and completes the transfer this event carried, and
     * only then does the record stop accepting work. See `xhciDevSlotFatalEvent`.
     */
    xhciDevSlotFatalEvent(ext, dev, completionCode);
    return result.Fatal ? 1UL : 0UL;
}

/*
 * One endpoint's share of the settle: retire and complete every transfer on
 * this queue whose deferred tail is not coming, and answer how many are still
 * armed afterwards.
 *
 * The loop is what keeps this from being a second accounting written beside
 * the event path's: each settled transfer goes through the same fold, the same
 * `xhciDevOweCompletion`, the same ring-full release and the same recovery
 * escalation a Transfer Event's completion does.
 *
 * Called with the controller lock held. IRQL: DISPATCH_LEVEL.
 */
static ULONG xhciDevSettleQueue(PXHCI_EXTENSION ext,
                                PXHCI_DEVICE dev,
                                PXHCI_EP_BINDING binding)
{
    XHCI_XFER_EVENT_RESULT result;
    PXHCI_TRANSFER_QUEUE queue;
    ULONG guard;

    queue = binding->Queue;

    /*
     * Bounded by the queue's own length, which every iteration shortens by at
     * least one - so the bound is a backstop against a future change breaking
     * that, not a limit the ordinary path approaches. It is deliberately not a
     * constant: a queue that is longer than this bound would be one whose
     * settles were silently truncated, and there is no such queue.
     */
    guard = queue->Count;
    while (guard > 0) {
        ULONG shortBefore;
        ULONG lostBefore;
        ULONG droppedBefore;

        shortBefore = queue->MidTdShortRetires;
        lostBefore = queue->MidTdDeferralsLost;
        /* The settle is where a tail record is *made*, so it is also the only
         * place one can be evicted - see `xhciDevFoldTailsDropped`. */
        droppedBefore = queue->MidTdTailsDropped;
        if (XhciXferDrainSettled(queue, binding->Ring, &result) !=
                XHCI_XFER_OK) {
            break;
        }
        if (result.Action != XHCI_XFER_ACTION_COMPLETE) {
            if (result.RefusedRetire) {
                /*
                 * The ring refused the retire and the transfer is still queued
                 * on a Running endpoint. This is the same divergence the event
                 * path can find a pass earlier, and it gets the same answer -
                 * see `xhciEpRefusedRetireRecovery`, which is one function
                 * precisely because the batch-9-0 review found this defect
                 * separately at each of the two sites.
                 */
                xhciDevFoldDeferralsLost(ext, queue, lostBefore);
                xhciEpRefusedRetireRecovery(ext, dev, binding);
            }
            /* Folded on **every** exit, not only the completing one. A refused
             * retire records no tail today, so this is consistency rather than
             * a case with a witness - but the alternative is a fold whose
             * correctness depends on a rule stated three files away. */
            xhciDevFoldTailsDropped(ext, queue, droppedBefore);
            break;
        }
        guard--;

        ext->MidTdShortRetiresTotal +=
            queue->MidTdShortRetires - shortBefore;
        xhciDevFoldDeferralsLost(ext, queue, lostBefore);
        xhciDevFoldTailsDropped(ext, queue, droppedBefore);
        ext->MidTdSettles++;

        ext->TransfersCompleted += result.CompletedCount;
        xhciDevOweCompletion(ext, result.Completed);
        /* Task 8-A.1's latch, released for the same reason the event path
         * releases it: the retire is what put TRBs back on the ring. */
        xhciEpReleaseRetry(ext, dev, binding);
    }

    return XhciXferDeferralsArmed(queue);
}

/*
 * Task 9-0.2. **A drain pass has found the event ring empty**, so every tail
 * event 4.10.1.1.2 p.175 promised for a TD that ended short has either arrived
 * and been matched, or was never sent. Retire the ones that were never sent.
 *
 * `XhciEventDpc` calls this and nothing else does - and only when that pass
 * **observed** the ring empty, which is a different question from which exit
 * its loop took: a pass that stopped at `XHCI_DPC_MAX_EVENTS` qualifies if a
 * peek then shows the ring empty, and does not if events remain, because the
 * tail may be one of them. See `XhciXferDrainSettled` for what the timing buys
 * and for the one window it does not close.
 *
 * **The walk is gated, and the gate is safe in one direction only.**
 * `MidTdDeferPending` is set by the arm fold and recomputed exactly here, so
 * between passes it can only over-state - which costs a wasted walk that then
 * corrects it. It cannot under-state, because the only thing that arms a
 * deferral is `XhciSlotTransferEvent`, which sets it in the same fold. A hint
 * that can only be too high is usable as a skip test; one that could be too
 * low would not be.
 *
 * **And the walk is what checks the partition.** Every path that can move one
 * of the five terms folds it, and this is the one instant where all five are
 * settled at once, so
 *
 *     deferrals == tailed + tailedSpurious + retired + lost + still armed
 *
 * is checkable rather than asserted in a comment. `MidTdDeferAccountingBroken`
 * is sticky and is what a fold site added later and not wired up would trip -
 * a row set assembled path-by-path is only as complete as whoever assembled
 * it (task 7b-A.1.0), and this is that net.
 *
 * Called with the controller lock held. IRQL: DISPATCH_LEVEL.
 */
VOID XhciSlotDrainSettled(PXHCI_EXTENSION ext)
{
    XHCI_EP_BINDING binding;
    ULONG armed;
    ULONG i;
    ULONG j;

    if (ext == NULL || ext->Signature != XHCI_EXTENSION_SIGNATURE) {
        return;
    }
    /*
     * **The gate and the net are the same test**, which is what makes the net
     * reachable. Skipping on the hint alone would mean a term that stopped being
     * folded is never noticed - the books would be short, the hint would read 0
     * because no arm had happened since, and the walk that checks the identity
     * would never run again. So the skip requires *both* that nothing is armed
     * and that the other totals already balance without an armed term; a missed
     * fold forces the walk, and the walk is what sets the sticky flag.
     *
     * Four loads and a compare on a pass with nothing to do, which is the cost
     * of the identity being checked rather than asserted in a comment.
     */
    if (ext->MidTdDeferPending == 0 &&
        ext->MidTdDeferralsTotal ==
            (ULONG)(ext->MidTdDeferralsTailedTotal +
                    ext->MidTdDeferralsTailedSpuriousTotal +
                    ext->MidTdShortRetiresTotal +
                    ext->MidTdDeferralsLostTotal)) {
        return;
    }

    ext->MidTdSettlePasses++;
    armed = 0;
    for (i = 0; i < XHCI_MAX_SLOTS; i++) {
        PXHCI_DEVICE dev = &ext->Devices[i];

        if (dev->State == XHCI_DEV_STATE_FREE) {
            continue;
        }
        /* EP0 is walked though a control transfer can never arm a deferral -
         * its TD is never entirely data, which is the shape test. Walking it
         * anyway is what makes the partition a statement about every queue
         * rather than about the ones this file expects to be involved. */
        if (xhciEpResolve(dev, 1, &binding)) {
            armed += xhciDevSettleQueue(ext, dev, &binding);
        }
        for (j = 0; j < XHCI_MAX_DEVICE_ENDPOINTS; j++) {
            if (dev->Endpoints[j].Dci == 0) {
                continue;
            }
            if (xhciEpResolve(dev, dev->Endpoints[j].Dci, &binding)) {
                armed += xhciDevSettleQueue(ext, dev, &binding);
            }
        }
    }
    ext->MidTdDeferPending = armed;

    /*
     * Unsigned throughout and deliberately: all the totals count the same
     * events, so they wrap together and the equality still holds modulo 2^32.
     * A wrap therefore cannot make this fire, which is the property repo audit
     * finding 24 wanted from a gate.
     */
    if (ext->MidTdDeferralsTotal !=
        (ULONG)(ext->MidTdDeferralsTailedTotal +
                ext->MidTdDeferralsTailedSpuriousTotal +
                ext->MidTdShortRetiresTotal +
                ext->MidTdDeferralsLostTotal + armed)) {
        ext->MidTdDeferAccountingBroken = 1;
    }
}

/*
 * The TRB type each device operation goes out as. It is what separates this
 * layer's completion from somebody else's: the owner field says *which record*
 * has the engine, and this says whether the completion that arrived is that
 * record's kind of command at all. Without it, a No Op self-test completing in
 * the window between recording the owner and submitting would be read as the
 * owner's answer. IRQL: any.
 */
static ULONG xhciDevOpTrbType(ULONG op)
{
    switch (op) {
    case XHCI_DEV_OP_ENABLE_SLOT:
        return XHCI_TRB_TYPE_ENABLE_SLOT;
    case XHCI_DEV_OP_ADDRESS_BSR:
    case XHCI_DEV_OP_ADDRESS_SET:
        return XHCI_TRB_TYPE_ADDRESS_DEVICE;
    case XHCI_DEV_OP_EVALUATE_MPS:
        return XHCI_TRB_TYPE_EVALUATE_CONTEXT;
    case XHCI_DEV_OP_CONFIGURE_EP:
    /* The same TRB type, and the two ops stay separate anyway: this function
     * only has to tell one *record's* command from another submitter's, and the
     * owner op is what the completion switches on (task 7b-A.2). */
    case XHCI_DEV_OP_MARK_HUB:
        return XHCI_TRB_TYPE_CONFIGURE_EP;
    case XHCI_DEV_OP_STOP_EP:
        return XHCI_TRB_TYPE_STOP_EP;
    case XHCI_DEV_OP_RESET_EP:
        return XHCI_TRB_TYPE_RESET_EP;
    case XHCI_DEV_OP_SET_DEQUEUE:
        return XHCI_TRB_TYPE_SET_TR_DEQUEUE;
    case XHCI_DEV_OP_DISABLE_SLOT:
        return XHCI_TRB_TYPE_DISABLE_SLOT;
    case XHCI_DEV_OP_RESET_DEVICE:
        return XHCI_TRB_TYPE_RESET_DEVICE;
    default:
        return 0;
    }
}

/*
 * An Address Device has succeeded, so whatever task 7b-A.2 had marked on this
 * slot is gone.
 *
 * Not a precaution: "Any Output Slot Context is 'valid' for subsequent Address
 * Device Commands because **all fields of the Output Slot Context are
 * overwritten by the xHC**" (6.2.2.1 p.412), and the Input Slot Context this
 * driver hands it carries Hub = 0 because the same section's validity list ends
 * "and all other fields are cleared to '0'". So a re-addressed hub is an
 * unmarked hub, and the derived need has to come back with it. It does, because
 * `HubMarkDone` is what the derivation compares against - this is the whole
 * mechanism, and the counter is what says it fired.
 *
 * The failure flag goes too. Whatever the xHC objected to last time, the slot it
 * objected about no longer exists in the state it objected in, so suppressing
 * the retry past this point would be inheriting a verdict about something else.
 *
 * Called with the lock held. IRQL: DISPATCH_LEVEL.
 */
static VOID xhciDevHubMarkLost(PXHCI_EXTENSION ext, PXHCI_DEVICE dev)
{
    if (dev->HubMarkDone != 0) {
        ext->HubMarksLostToAddress++;
    }
    dev->HubMarkDone = 0;
    dev->HubMarkIssued = 0;
    dev->Flags &= ~XHCI_DEV_FLAG_HUB_MARK_FAILED;
}

/*
 * The other end: a Configure Endpoint completed with Success, so the four hub
 * fields its Input Slot Context carried are what the xHC now holds.
 *
 * `HubMarkIssued` rather than a fresh derivation, for the reason
 * `record->PendingParams` exists: the graph may have moved since the command was
 * built, and what this records is what was *programmed*. A derivation here would
 * write the current wanted value onto a command that carried the previous one
 * and lose the re-mark.
 *
 * Committed unconditionally, zero included: an ordinary device's Configure
 * Endpoint carries Hub = 0, and `HubMarkDone` has to say so rather than keep
 * whatever a hub's command left there.
 *
 * `HubSlotsMarked` counts hubs described to the controller, so it moves only on
 * the transition - a hub whose alternate setting flips MTT back and forth is one
 * hub marked several times, and counting each would read as several hubs.
 *
 * Called with the lock held. IRQL: DISPATCH_LEVEL.
 */
static VOID xhciDevHubMarkTook(PXHCI_EXTENSION ext, PXHCI_DEVICE dev)
{
    if (dev->HubMarkIssued != 0 && dev->HubMarkDone == 0) {
        ext->HubSlotsMarked++;
    }
    dev->HubMarkDone = dev->HubMarkIssued;
}

/* Hand the intercepted SET_ADDRESS back to usbport. Called with the lock held. */
static VOID xhciDevFinishSetAddress(PXHCI_EXTENSION ext,
                                    PXHCI_DEVICE dev,
                                    LONG usbdStatus)
{
    PXHCI_TRANSFER transfer;

    transfer = dev->PendingSetAddress;
    if (transfer == NULL) {
        /* Aborted, or torn down, while the command was in flight. The command
         * still completed and its effect on the slot is real; there is simply
         * nobody left to tell. */
        return;
    }
    dev->PendingSetAddress = NULL;
    transfer->Next = NULL;
    transfer->UsbdStatus = usbdStatus;
    /* SET_ADDRESS has no data stage, so the byte count is zero by construction
     * rather than by measurement. */
    transfer->BytesTransferred = 0;
    xhciDevOweCompletion(ext, transfer);
}

/*
 * Take the Slot ID an Enable Slot completion carries, **whatever its completion
 * code says**, and audit round 10 is why that is not "whatever its completion
 * code says about success".
 *
 * Table 6-42 p.444 is unconditional: the Slot ID field "shall be set to the ID
 * of the newly allocated Device Slot for the Enable Slot Command". 4.6.3 p.95
 * gives the two-branch algorithm (a slot, or 0 with No Slots Available) and
 * p.97 names the single completion code for which the field may not be
 * believed: "If this command is aborted (i.e. Completion Code = Command Aborted)
 * the Slot ID field should be considered by software to be invalid (e.g. no slot
 * was allocated)" - code 25, which the command engine handles before this
 * function is ever reached. Every other non-success code is a code the table
 * does not exempt, so a slot may have been allocated and this driver has to
 * assume it was.
 *
 * A record that does not adopt it is a slot nothing can ever give back: the
 * Disable Slot is issued from the device record, and a record whose `SlotId` is
 * 0 asks for none. That is a bounded leak on its own, and it is what made round
 * 10's finding **HIGH**: Incompatible Device Error "may be returned by any
 * command" with a Disable Slot as its named recovery, and
 * `XhciSlotCommandSlotFatal` resolves the device through `xhciDevBySlotId`, so
 * an unadopted slot made the mandated recovery unreachable.
 *
 * The three checks are the ones the success path always made and are unchanged:
 * a nonzero ID, inside the carve (which is what decides whether the slot has a
 * device context and an EP0 ring at all), and held by no other record - two
 * records sharing a Slot ID would share both.
 *
 * **An adopted slot on a failed Enable Slot has no Device Context**, because
 * `xhciDevPrepareSlot` never ran, and the Disable Slot that follows is issued
 * against a slot whose DCBAA entry is still the zero `XhciSlotInit` left. That
 * is deliberate and it is not a new route: `xhciDevPrepareSlot`'s own failure
 * has always ended in a Disable Slot for exactly that state. 4.6.4 p.97's list
 * of what the command does - disable the doorbell, free bandwidth, terminate USB
 * activity, free internal resources, flag the slot available - reads no device
 * context, and a controller that considers the slot disabled already answers
 * Slot Not Enabled Error, which `xhciDevDisableCompleted` accepts as released.
 * The alternative is a permanent leak.
 *
 * Returns 1 when the record holds a usable Slot ID afterwards. Called with the
 * lock held. IRQL: DISPATCH_LEVEL.
 */
static ULONG xhciDevAdoptSlotId(PXHCI_EXTENSION ext,
                                PXHCI_DEVICE dev,
                                ULONG control)
{
    ULONG slotId;

    if (dev->SlotId != 0) {
        /* Already holds one. A record takes a Slot ID from its own Enable Slot
         * and from nowhere else, so this is "nothing to do" rather than a second
         * grant to account for. */
        return 1;
    }
    slotId = XHCI_TRB_GET_SLOT_ID(control);
    if (slotId == 0 || slotId > ext->Layout.MaxSlotsEn ||
        xhciDevBySlotId(ext, slotId) != NULL) {
        return 0;
    }
    dev->SlotId = slotId;
    ext->SlotsEnabled++;
    return 1;
}

/* IRQL: DISPATCH_LEVEL, controller lock held. */
VOID XhciSlotCommandEvent(PXHCI_EXTENSION ext,
                          ULONG completionCode,
                          ULONG control)
{
    PXHCI_DEVICE dev;
    ULONG op;
    ULONG succeeded;

    if (ext == NULL || ext->CommandOwner == 0) {
        return;
    }
    dev = xhciDevFromRef(ext, ext->CommandOwner);
    op = ext->CommandOwnerOp;
    if (dev == NULL) {
        ext->CommandOwner = 0;
        ext->CommandOwnerOp = XHCI_DEV_OP_NONE;
        return;
    }
    /*
     * The type check, and it is not belt and braces: the owner is recorded
     * before the submit, so a command belonging to something else - the init
     * sequence's No Op self-test is the only other submitter - can complete
     * inside that window. Attributing it here would advance a device's state on
     * the strength of an unrelated event.
     */
    if (xhciDevOpTrbType(op) != ext->CommandType) {
        XHCI_DBG_VALUE_CHANGED("slot: command completion of another type while "
                               "a device owned the engine, type",
                               ext->CommandType);
        return;
    }

    ext->CommandOwner = 0;
    ext->CommandOwnerOp = XHCI_DEV_OP_NONE;
    dev->ActiveOp = XHCI_DEV_OP_NONE;
    dev->OpAgeArmed = 0;
    dev->LastCompletionCode = completionCode;
    succeeded = (completionCode == XHCI_CC_SUCCESS) ? 1 : 0;

    if (xhciDevOpIsQuiesce(op)) {
        XHCI_EP_BINDING binding;

        /*
         * Handled ahead of the GONE unwind and outside the switch below, because
         * a quiescence command means the same thing whatever the device is
         * doing - and a teardown is exactly the case where one is outstanding.
         * Routing it into the unwind would lose the drain the stop exists to
         * make safe, and the unwind does not need it: the Disable Slot is
         * already owed and `xhciDevOwedOp` releases it once nothing is.
         *
         * A resolve that fails means the record went away while its command was
         * in flight, taking the quiescence state with it - there is nothing left
         * to tell.
         */
        if (xhciEpResolve(dev, dev->EndpointOpDci, &binding)) {
            xhciEpQuiesceCompleted(ext, dev, &binding, op, completionCode);
        }
        return;
    }

    /*
     * A device that left while its command was in flight. Whatever the command
     * did, the answer now is the unwind - and a slot the command may have just
     * enabled still has to be given back, which is why the Slot ID is taken from
     * an Enable Slot even here, **and from a failed one as well as a successful
     * one**: see `xhciDevAdoptSlotId`, and audit round 10 for why.
     */
    if (dev->State == XHCI_DEV_STATE_GONE) {
        if (op == XHCI_DEV_OP_ENABLE_SLOT) {
            (VOID)xhciDevAdoptSlotId(ext, dev, control);
        }
        if (op == XHCI_DEV_OP_DISABLE_SLOT) {
            xhciDevDisableCompleted(ext, dev);
            return;
        }
        if (dev->SlotId != 0) {
            dev->PendingOp = XHCI_DEV_OP_DISABLE_SLOT;
        } else {
            xhciDevRelease(ext, dev, 1);
        }
        return;
    }

    /*
     * **A device that was re-enumerated while its command was in flight** - the
     * residue audit finding A2 left behind (see
     * `XHCI_EXTENSION.CommandOwnerTenancy`). The record is the same; the device
     * in it may not be. usbhub gives up, resets the port and reopens EP0 at
     * address 0, `xhciDevReenterAtDefault` decides the chain afresh, and only
     * then does the old command answer. Every arm below would then commit a
     * decision about the previous tenancy onto this one: the `ADDRESS_SET` arm
     * re-asserts `ADDRESS_VALID` with `DeviceAddress` 0, the `EVALUATE_MPS` arm
     * commits the old command's value against the new record's wanted one, and a
     * `RESET_DEVICE` or `ADDRESS_BSR` advance moves a slot the re-entry has
     * already decided is owed something else.
     *
     * So the completion commits nothing about the *device* - and then the chain
     * is **re-derived from the slot**, which is the half a first version of this
     * guard left out and a review round found. Suppressing the software advance
     * does not undo the hardware one: the command really did execute, so the
     * Slot State the re-entry read a moment earlier is now stale, and the debt it
     * recorded from that reading can be one the slot refuses. Concretely, a
     * re-entry that read Enabled and owed `ADDRESS_BSR` against a slot the stale
     * command then moved to Default gets Context State Error from the very
     * command this guard exists to protect - the finding's own second case,
     * reached by the fix for its first. `xhciDevOweFromSlotState` re-reads the
     * Output Slot Context and decides again; that is one memory read on a path
     * that is expected never to run.
     *
     * **`ENABLE_SLOT` is exempt and must be**, and audit round 10 is the reason:
     * it allocates a slot to this *record* whatever device is in it, so a
     * completion that did not adopt its Slot ID is a slot no path can give back
     * - and the follow-up the arm below writes (`ENABLED`, owing `ADDRESS_BSR`)
     * is exactly what the new tenancy needs anyway.
     *
     * **`DISABLE_SLOT` is not exempt, and a review round is why.** Its hardware
     * effect has to be honoured for the same reason - the slot really has gone -
     * but the ordinary arm honours it by *releasing the record*, which here would
     * zero or abandon a record a new tenancy has already been bound to, leaving
     * usbport's EP0 pointing at something no lookup answers for. It is handled
     * below instead: the accounting and the DCBAA clear happen, and then the new
     * tenancy starts its chain over from `ENABLE_SLOT`, which is exactly what a
     * re-entry that finds no slot already does. It is reachable through the one
     * path that owes a Disable Slot from a record a reopen can still find: a
     * failed `xhciDevPrepareSlot` leaves `FAILED`, and `xhciDevByHubPort`
     * excludes only `FREE` and `GONE`.
     *
     * `XhciSlotCommandLost` deliberately does **not** take this guard. There the
     * command's effect is unknown rather than known-stale, so it may have
     * executed against the slot this record still holds, and failing the record
     * is the answer that stays safe under either reading.
     */
    if (dev->Tenancy != ext->CommandOwnerTenancy &&
        op != XHCI_DEV_OP_ENABLE_SLOT) {
        ext->CommandsStaleTenancy++;
        XHCI_DBG_VALUE_CHANGED("slot: command completion for a tenancy that "
                               "has been re-enumerated, op << 8 | completion "
                               "code", (op << 8) | completionCode);
        if (op == XHCI_DEV_OP_DISABLE_SLOT) {
            /*
             * The two completion codes that prove the slot went are the same
             * two `xhciDevDisableCompleted` tests, and for the same reason -
             * Table 6-90 makes Incompatible Device Error returnable by any
             * command, so a code arriving here can be a Disable Slot that
             * disabled nothing. What differs is only what is done with the
             * record: proved, it starts over from `ENABLE_SLOT` with the slot
             * given back; not proved, the slot may still be enabled and no
             * further command on it can be trusted, so the record is failed
             * rather than handed to the new tenancy to reuse.
             */
            if (completionCode == XHCI_CC_SUCCESS ||
                completionCode == XHCI_CC_SLOT_NOT_ENABLED) {
                ext->SlotsDisabled++;
                XhciLogNoteLocked(ext, "slot.disabled", (ULONG)dev->SlotId);
                xhciDevClearDcbaa(ext, dev);
                dev->SlotId = 0;
                dev->State = XHCI_DEV_STATE_RESERVED;
                dev->PendingOp = XHCI_DEV_OP_ENABLE_SLOT;
                xhciDevOweInvalidate(ext, dev);
            } else {
                XhciLogNoteLocked(ext, "slot.disable.refused",
                                  ((ULONG)dev->SlotId << 8) | completionCode);
                xhciDevFail(ext, dev, XHCI_DEV_OP_DISABLE_SLOT);
            }
            return;
        }
        if (dev->SlotId != 0) {
            xhciDevOweFromSlotState(ext, dev);
            /*
             * An intercepted SET_ADDRESS the record is holding needs nothing
             * done to it. It is derived from `PendingSetAddress` in
             * `xhciDevOwedOp` rather than stored in `PendingOp`, precisely so
             * that the re-derivation above cannot discard it: whatever the chain
             * owes runs first, and the transfer is issued when the slot is back
             * at Default. An earlier version of this arm stored it, and had to
             * choose between clobbering the transfer and error-completing it -
             * and usbport does not re-offer a transfer it has been given an
             * error for, so that choice cost an enumeration round either way.
             */
            xhciDevOweInvalidate(ext, dev);
        }
        return;
    }

    switch (op) {
    case XHCI_DEV_OP_ENABLE_SLOT:
        if (!succeeded) {
            /*
             * **The slot is adopted before the record is failed, and audit round
             * 10 found that it was not.** Table 6-42 p.444 says the Slot ID field
             * "shall be set to the ID of the newly allocated Device Slot for the
             * Enable Slot Command" - unconditionally, not only on Success - and
             * 4.6.3 p.97 names exactly one completion code for which it is not
             * to be believed: "If this command is aborted (i.e. Completion Code =
             * Command Aborted) the Slot ID field should be considered by software
             * to be invalid (e.g. no slot was allocated)", which is code 25 and
             * never reaches this function. So a failed Enable Slot can still have
             * taken a slot, and a record that does not record it is a slot no
             * path in this driver can ever give back.
             *
             * That was a leak on its own; what made it a **HIGH** finding is
             * Incompatible Device Error, which "may be returned by any command"
             * and whose named recovery is a Disable Slot for exactly that slot.
             * `XhciSlotCommandSlotFatal` resolves the device through
             * `xhciDevBySlotId`, so without the adoption it could not find the
             * record and the mandated recovery was silently dropped.
             */
            (VOID)xhciDevAdoptSlotId(ext, dev, control);
            xhciDevFail(ext, dev, XHCI_DEV_OP_ENABLE_SLOT);
            break;
        }
        {
            ULONG slotId;

            slotId = XHCI_TRB_GET_SLOT_ID(control);
            if (!xhciDevAdoptSlotId(ext, dev, control)) {
                XHCI_DBG_VALUE_CHANGED("slot: Enable Slot returned an "
                                       "unusable Slot ID", slotId);
                xhciDevFail(ext, dev, XHCI_DEV_OP_ENABLE_SLOT);
                break;
            }
            /*
             * Task 11-V.9's second tier. The Slot ID is the name every later
             * record in this file uses - a transfer event, an endpoint halt and
             * a disable all carry it and nothing else identifies the device -
             * so a log without this line has no way to tie any of them to the
             * port the device arrived on.
             */
            XhciLogNoteLocked(ext, "slot.enabled",
                              (slotId << 8) | dev->HubPort);
            if (!xhciDevPrepareSlot(ext, dev)) {
                dev->PendingOp = XHCI_DEV_OP_DISABLE_SLOT;
                dev->State = XHCI_DEV_STATE_FAILED;
                ext->CommandFailures++;
                break;
            }
            if (dev->EndpointExtension != NULL) {
                ((PXHCI_ENDPOINT)dev->EndpointExtension)->SlotId = slotId;
            }
            dev->State = XHCI_DEV_STATE_ENABLED;
            dev->PendingOp = XHCI_DEV_OP_ADDRESS_BSR;
        }
        break;

    case XHCI_DEV_OP_RESET_DEVICE:
        if (!succeeded) {
            /*
             * There is no second rung. The command's own list is Addressed or
             * Configured (4.6.11 p.130) and the branch that issued it read the
             * Output Slot State to satisfy that, so a refusal here means this
             * driver and the xHC disagree about the slot - which is exactly the
             * disagreement no further command on it can be trusted to resolve.
             * The failure unwinds the record and stops serving it. **The slot is
             * kept**, as `xhciDevFailRecord`'s own header says and as the
             * `ADDRESS_BSR` refusal arm below does: the xHC still has one
             * enabled, and giving it back needs a Disable Slot this path has no
             * reason to issue. A later unplug's teardown owes that.
             */
            xhciDevFail(ext, dev, XHCI_DEV_OP_RESET_DEVICE);
            break;
        }
        /*
         * "Set the Slot State field of Slot Context to the Default state ... Set
         * the Context Entries field of Slot Context to '1' ... Set the USB
         * Device Address field of Slot Context to '0'" and every endpoint but
         * EP0 to Disabled, with EP0 itself transitioned "to the Running state"
         * (4.6.11 p.129). That is the same place a successful BSR-form
         * Address Device lands, so the chain continues identically from here.
         */
        dev->State = XHCI_DEV_STATE_DEFAULT;
        ext->SlotsResetToDefault++;
        /*
         * **The four bits that were statements about an Endpoint Context the xHC
         * has just disabled, and nothing else** (review rounds 2 and 3).
         *
         * `HALTED`, `STOPPED`, `FAILED` and `UNAVAILABLE` all describe the state
         * of a context that no longer exists, so carrying one into the next
         * enumeration is carrying a verdict about a different endpoint - which is
         * the same argument `XhciSlotResumeSweep` makes for `FAILED` after a
         * restore.
         *
         * **Everything else is deliberately left alone**, and round 3 is why: an
         * owed or outstanding command's bits are a *software debt*, not a
         * statement about hardware, and a new one can be armed while this very
         * command is outstanding - `AbortTransfer` arms a reposition, a
         * `SetEndpointState(REMOVE)` arms a drain. Discarding those loses a
         * cancellation, which is a transfer whose buffer usbport has reclaimed
         * left standing on a ring. They run against a Disabled endpoint, answer
         * Context State Error, and are handled: that is a slower unwind than
         * dropping them, and it is the one that answers the transfers.
         *
         * `XHCI_EPQ_NO_CONTEXT` is also left alone, and set rather than cleared:
         * it is still true, and the Configure Endpoint that re-adds the endpoint
         * is what ends it, which is its documented lifecycle.
         */
        {
            ULONG epIndex;

            for (epIndex = 0; epIndex < XHCI_MAX_DEVICE_ENDPOINTS; epIndex++) {
                PXHCI_ENDPOINT_RECORD record = &dev->Endpoints[epIndex];

                if (record->Dci == 0) {
                    continue;
                }
                record->Quiesce.Flags |= XHCI_EPQ_NO_CONTEXT;
                record->Quiesce.Flags &=
                    ~(XHCI_EPQ_HALTED | XHCI_EPQ_STOPPED | XHCI_EPQ_FAILED |
                      XHCI_EPQ_UNAVAILABLE);
                record->Quiesce.StoppedArmed = 0;
            }
        }
        xhciDevEp0Restored(dev);
        xhciDevHubMarkLost(ext, dev);
        xhciDevOweInvalidate(ext, dev);
        break;

    case XHCI_DEV_OP_ADDRESS_BSR:
        if (!succeeded) {
            xhciDevFail(ext, dev, XHCI_DEV_OP_ADDRESS_BSR);
            break;
        }
        /* "if the BSR flag is '1' ... the Slot State is set to Default and no
         * SET_ADDRESS request is generated" (4.6.5, p.101). EP0 is Running from
         * here, and control traffic to the unaddressed device may flow. */
        dev->State = XHCI_DEV_STATE_DEFAULT;
        xhciDevEp0Restored(dev);
        xhciDevHubMarkLost(ext, dev);
        xhciDevOweInvalidate(ext, dev);
        break;

    case XHCI_DEV_OP_ADDRESS_SET:
        if (!succeeded) {
            xhciDevFinishSetAddress(ext, dev,
                                    XHCI_USBD_STATUS_INTERNAL_HC_ERROR);
            xhciDevFail(ext, dev, XHCI_DEV_OP_ADDRESS_SET);
            break;
        }
        dev->State = XHCI_DEV_STATE_ADDRESSED;
        dev->Flags |= XHCI_DEV_FLAG_ADDRESS_VALID;
        ext->DevicesAddressed++;
        /*
         * Task 11-V.9's second tier, and the record the task exists for: **a
         * device is now on the bus, and this says which, at what speed and
         * where.** `DevicesAddressed` already answered "how many"; nothing
         * before this answered "which one, and what happened to it next".
         *
         * Three fields in one record rather than three records, because they
         * are one event and a reader needs them together: the Slot ID names it
         * for every later line, the address is what usbport and usbhub call it,
         * and the speed is this driver's own decode - the only place it
         * survives, since usbport is told High Speed for everything.
         */
        XhciLogNoteLocked(ext, "slot.addressed",
                          ((ULONG)dev->SlotId << 16) |
                              ((ULONG)dev->DeviceAddress << 8) |
                              (ULONG)dev->Speed);
        /*
         * The topology half, and it is a separate record because it is a
         * separate fact: where this device sits. A root-port device has route
         * string 0 and tier 0, and a device behind a hub carries the string
         * that took four review rounds of task 7b-A.3 to build - which is
         * exactly the value a bug report about "devices behind hubs" needs and
         * which no counter can carry.
         */
        XhciLogNoteLocked(ext, "slot.route",
                          ((ULONG)dev->Tier << 24) | dev->RouteString);
        XhciLogNoteLocked(ext, "slot.parenthub",
                          ((ULONG)dev->ParentHubAddress << 8) |
                              (ULONG)dev->ParentHubPort);
        if (dev->Tier != 0) {
            /* Task 7b-A.3's headline reading: a device *behind a hub* has an
             * address. Counted here rather than at the open because the open is
             * an intention and this is the outcome - the gap between the two is
             * enumerations that started one tier down and did not finish. */
            ext->BehindHubAddressed++;
        }
        xhciDevEp0Restored(dev);
        xhciDevHubMarkLost(ext, dev);
        xhciDevFinishSetAddress(ext, dev, XHCI_USBD_STATUS_SUCCESS);
        xhciDevOweInvalidate(ext, dev);
        break;

    case XHCI_DEV_OP_EVALUATE_MPS:
        if (!succeeded) {
            xhciDevFail(ext, dev, XHCI_DEV_OP_EVALUATE_MPS);
            break;
        }
        /*
         * **The wanted value is re-read here and may have gone to zero, and a
         * zero is refused rather than committed** (audit finding A3). The
         * re-enumeration branches clear `WantedMaxPacketSize0` without checking
         * whether an Evaluate Context is in flight, and the window - a command
         * completion against a port reset plus reopen - is small but real on
         * SMP. Committing the zero costs a whole enumeration round, because
         * `XhciBuildEp0Params` refuses mps0 = 0 and the record is failed; and
         * the correct answer is available and free, which is to leave
         * `MaxPacketSize0` at the speed's default that the reopen just wrote.
         * The command itself did land, so this is not a failure - the value it
         * carried is simply no longer the value this record is asking for.
         *
         * The tenancy guard above now reaches that window first, so this arm
         * fires only if some future path clears the wanted value without the
         * record changing hands. It is kept for that: it asks about the value,
         * not about the tenancy, and the two are not the same question.
         */
        if (dev->WantedMaxPacketSize0 == 0) {
            ext->Mps0CorrectionsStale++;
            break;
        }
        dev->MaxPacketSize0 = dev->WantedMaxPacketSize0;
        ext->Mps0Corrections++;
        xhciDevOweInvalidate(ext, dev);
        break;

    case XHCI_DEV_OP_CONFIGURE_EP:
        {
            PXHCI_ENDPOINT_RECORD record;

            record = xhciEpByDci(dev, dev->EndpointOpDci);
            if (record == NULL || record->State != XHCI_EP_REC_CONFIGURING) {
                /* The endpoint went away while its command was in flight - a
                 * teardown, or a record the unwind already cleared. The command
                 * still happened; there is simply nobody left to tell. */
                break;
            }
            if (succeeded) {
                record->State = XHCI_EP_REC_CONFIGURED;
                /*
                 * **The hub marking this command carried is now the xHC's**
                 * (task 7b-A.2). Committed only on Success, and only here: on
                 * any other completion code 4.6.6 p.105 leaves the Output
                 * Endpoint Contexts and the Slot State unchanged and says
                 * nothing at all about the Slot Context's other fields, so the
                 * one thing that cannot be claimed is that the marking landed.
                 * Leaving `HubMarkDone` alone re-derives the need, and the
                 * standalone command then marks the hub on its own - the safe
                 * direction, since programming the same four values twice costs
                 * one command and believing a failure costs an unmarked hub.
                 */
                xhciDevHubMarkTook(ext, dev);
                /*
                 * **The endpoint has an Output Endpoint Context again, so the
                 * doorbell is back in bounds.** Every Configure Endpoint this
                 * driver issues for a record carries that record's Add Context
                 * flag - the Drop beside it is the optional half - so a success
                 * here is the one event that reverses a Disabled reading.
                 *
                 * This is not hypothetical: the alternate-interface reconfigure
                 * *is* a stop whose Context State Error can read back Disabled,
                 * and `XHCI_EPQ_RECONFIGURE` is handled ahead of everything else
                 * in `xhciEpStopped`, so the same completion both raises the bit
                 * and starts the command that clears it. Left set, it would fail
                 * every submission to an endpoint the xHC had just accepted.
                 */
                record->Quiesce.Flags &= ~XHCI_EPQ_NO_CONTEXT;
                /*
                 * **The doorbell is deliberately not rung here.** The endpoint
                 * is left `STOPPED` with whatever work survived, and the
                 * ordinary ends of that state take it from there: the
                 * `SetEndpointState(ACTIVE)` that closes usbport's cancellation
                 * pass, the next submission, or the health poll's net. Ringing
                 * from here would have to overrule `XHCI_EPQ_PAUSED` - that pass
                 * is usually still running when this command lands - and the
                 * pause is the one guarantee batch 7a-B has about aborts.
                 */
                /*
                 * **Only now do the new parameters become the record's.** The
                 * Drop was evaluated with the Add, so the endpoint the xHC holds
                 * is the one `PendingParams` describes; on any other completion
                 * code the xHC kept the old one ("The Output Endpoint
                 * Contexts referenced by the command in the Device Context
                 * shall be unchanged", 4.6.6 p.106) and so does this
                 * record, which is what keeps the two descriptions the same.
                 */
                if (record->DropOnConfigure) {
                    record->Params = record->PendingParams;
                    record->DropOnConfigure = 0;
                }
                ext->EndpointsConfigured++;
            } else if (completionCode == XHCI_CC_BANDWIDTH_ERROR ||
                       completionCode == XHCI_CC_SECONDARY_BANDWIDTH ||
                       completionCode == XHCI_CC_RESOURCE_ERROR) {
                /*
                 * **Not a controller failure**, and this is the distinction task
                 * 7a-A.1 names explicitly - but **not with the codes the task
                 * text named**, which said 8 and 9. The tenth review checked
                 * Table 6-90 (p.466) and 9 is the wrong one:
                 *
                 *   7 Resource Error - "Asserted by a Configure Endpoint Command
                 *     or an Address Device Command if there are not adequate xHC
                 *     resources available to successfully complete the command."
                 *   8 Bandwidth Error - "Asserted by a Configure Endpoint
                 *     Command if periodic endpoints are declared and the xHC is
                 *     not able to allocate the required Bandwidth."
                 *   9 No Slots Available - Enable Slot's answer, about
                 *     `MaxSlots` (4.6.3). A Configure Endpoint does not produce
                 *     it, and this code was watching for it while letting the
                 *     one a real controller returns fall through to a generic
                 *     failure.
                 *  35 Secondary Bandwidth Error - "Asserted by a Configure
                 *     Endpoint Command if periodic endpoints are declared and
                 *     the xHC is not able to allocate the required Bandwidth due
                 *     to a Secondary Bandwidth Domain" (p.469). The eleventh
                 *     review found it missing: it is word for word code 8's
                 *     sentence with a bandwidth domain named, so treating it as
                 *     a malformed context rather than a full schedule was the
                 *     same error one code over.
                 *
                 * All three of the right ones are the xHC saying it cannot *schedule*
                 * this endpoint, which usbhub is built to degrade on. Treating
                 * either as a fault would fail a device whose EP0 and every
                 * other pipe are working.
                 *
                 * The record is kept rather than torn down: usbport was told the
                 * open succeeded, so it will submit, and a REFUSED record is
                 * where those submissions are answered from. Its ring stays with
                 * it until the device goes, because a pool entry recycled while
                 * the xHC might still name it is the fault design doc 04
                 * section 3.6 chose the controller block to avoid.
                 */
                record->State = XHCI_EP_REC_REFUSED;
                /* Split by *what* ran out, which is the only part of this a
                 * target reading can act on: 8 and 35 are both the periodic
                 * schedule (35 naming a Secondary Bandwidth Domain), 7 is the
                 * xHC's own internal resources. */
                if (completionCode == XHCI_CC_RESOURCE_ERROR) {
                    ext->EndpointsNoResources++;
                } else {
                    ext->EndpointsNoBandwidth++;
                }
                XHCI_DBG_VALUE_CHANGED("slot: the xHC would not schedule an "
                                       "endpoint, dci << 8 | completion code",
                                       (record->Dci << 8) | completionCode);
            } else {
                record->State = XHCI_EP_REC_FAILED;
                ext->EndpointConfigureFailures++;
                XHCI_DBG_VALUE_CHANGED("slot: Configure Endpoint failed, "
                                       "dci << 8 | completion code",
                                       (record->Dci << 8) | completionCode);
            }
            /*
             * Asked for in every case, success included: usbport has been
             * holding this pipe's transfers since the open returned, and the
             * three outcomes all need it to offer them again - to be served, or
             * to be answered with the error the submit path now has a state for.
             */
            xhciEpOweInvalidate(ext, record);
        }
        break;

    case XHCI_DEV_OP_MARK_HUB:
        /*
         * Task 7b-A.2's standalone marking. No endpoint was named, so there is
         * no record to move and nothing to invalidate: usbport is not waiting on
         * this command, and every pipe it did not mention kept running through
         * it (4.6.6 p.106, "If the Drop Context flag is '0' and the Add Context
         * flag is '0', the xHC shall: Do nothing").
         *
         * A failure is the hub's own description giving up and nothing else -
         * the device stays addressed with its endpoints. What it costs is
         * measured rather than shrugged off: the xHC is scheduling this hub's
         * downstream traffic without being told it is a hub, so a nonzero
         * `HubMarkFailures` is the reading that says an FS/LS device behind it
         * cannot be expected to work.
         */
        if (succeeded) {
            xhciDevHubMarkTook(ext, dev);
        } else {
            dev->Flags |= XHCI_DEV_FLAG_HUB_MARK_FAILED;
            ext->HubMarkFailures++;
            XHCI_DBG_VALUE_CHANGED("slot: the xHC would not mark a hub slot, "
                                   "address << 8 | completion code",
                                   (dev->DeviceAddress << 8) | completionCode);
        }
        break;

    case XHCI_DEV_OP_DISABLE_SLOT:
        /* Counted whatever the completion code says, but **not acted on** the
         * same way: `xhciDevDisableCompleted` reads the code to decide whether
         * the slot was really released, and a completion that proves nothing
         * leaves the record, its DCBAA entry and its rings exactly as the
         * hardware may still be reading them. */
        xhciDevDisableCompleted(ext, dev);
        break;

    default:
        break;
    }
}

/*
 * The command ring's half of Table 6-90's slot-fatal instruction, and **audit
 * round 9 found it missing entirely**.
 *
 * Round 8 taught the transfer paths and the restore's drain that Incompatible
 * Device Error owes a Disable Slot, and read the table's own scope while doing
 * it: "This error may be returned by any command or transfer, and is fatal as
 * far as the Slot is concerned. Software shall issue a Disable Slot Command to
 * recover" (p.468). The *command* half of that sentence was never wired up.
 * `XhciSlotCommandEvent` reduces every completion code to success/non-success,
 * so a Configure Endpoint answered with 22 became one `EndpointConfigureFailures`
 * on a record whose device stayed in service - the same never-ending loop the
 * transfer path had before round 8, reached through the other event family.
 *
 * **The event's Slot ID selects the device, not `CommandOwner`.** Table 6-42
 * p.444 gives the Command Completion Event a Slot ID field that "shall be
 * updated by the xHC to reflect the slot associated with the command that
 * generated the event", which is the controller's own statement about which slot
 * is unusable; the owner is this driver's bookkeeping and is already cleared by
 * the time the ordinary accounting above has run.
 *
 * **A Slot ID of 0 is ordinary, not exceptional**, and audit round 10 corrected
 * a claim here that a failed Enable Slot was the only command producing one. The
 * same table lists the exceptions: it "shall be cleared to '0' for No Op, Set
 * Latency Tolerance Value, Get Port Bandwidth, and Force Event Commands", and
 * for a Host Controller Command. This driver issues a **No Op on every start and
 * every resume**, so a zero Slot ID is a code path taken in ordinary service and
 * not a corner. There is then no slot to Disable and nothing to take out of
 * service; the record that owned the command, if any, has already been answered
 * by the accounting above.
 *
 * Called **after** the ordinary accounting, for the same reason the transfer
 * paths call `xhciDevSlotFatalEvent` after theirs: the command's own effect on
 * the record is applied by the machinery that owns it, and only then does the
 * record stop accepting work. And called **only for a matched completion** - see
 * `XhciCommandEvent`, where audit round 10 split the two severities: a
 * controller-level fault is a statement about the controller whichever command
 * it names, while this one destroys a specific device and needs the match to tie
 * the event's Slot ID to a record this driver still owns.
 *
 * IRQL: DISPATCH_LEVEL, controller lock held.
 */
VOID XhciSlotCommandSlotFatal(PXHCI_EXTENSION ext,
                              ULONG completionCode,
                              ULONG control)
{
    PXHCI_DEVICE dev;
    ULONG slotId;

    if (ext == NULL) {
        return;
    }
    slotId = XHCI_TRB_GET_SLOT_ID(control);
    if (slotId == 0) {
        return;
    }
    dev = xhciDevBySlotId(ext, slotId);
    if (dev == NULL) {
        return;
    }
    xhciDevSlotFatalEvent(ext, dev, completionCode);
}

/* IRQL: DISPATCH_LEVEL, controller lock held. */
VOID XhciSlotCommandLost(PXHCI_EXTENSION ext)
{
    PXHCI_DEVICE dev;
    ULONG op;

    if (ext == NULL || ext->CommandOwner == 0) {
        return;
    }
    dev = xhciDevFromRef(ext, ext->CommandOwner);
    op = ext->CommandOwnerOp;
    ext->CommandOwner = 0;
    ext->CommandOwnerOp = XHCI_DEV_OP_NONE;
    ext->CommandOwnerLost++;

    if (dev == NULL) {
        return;
    }
    dev->ActiveOp = XHCI_DEV_OP_NONE;
    dev->OpAgeArmed = 0;

    if (xhciDevOpIsQuiesce(op)) {
        XHCI_EP_BINDING binding;

        /*
         * A quiescence command that will not answer. **Nothing is drained and
         * nothing is released**: the whole point of the stop was to establish
         * that the xHC is no longer reading the buffers those transfers name,
         * and a command the engine gave up on establishes the opposite. So the
         * endpoint's chain is marked failed - new submissions are answered with
         * an error rather than refused for ever - and the queued transfers wait
         * for a path that does prove it, which is `XhciSlotInit` after the
         * controller reset the engine's own ladder ends in.
         *
         * A device unwinding keeps its owed Disable Slot; it goes out with
         * `TeardownsWithoutStop` counted, because holding the slot for the life
         * of the driver is the worse of the two bad outcomes.
         */
        ext->EndpointQuiesceLost++;
        if (xhciEpResolve(dev, dev->EndpointOpDci, &binding)) {
            xhciEpQuiesceFail(ext, dev, &binding, op);
        }
        return;
    }

    if (op == XHCI_DEV_OP_MARK_HUB) {
        /*
         * A marking that will not answer (task 7b-A.2). Nothing about the device
         * changes: this command allocated nothing, named no endpoint, and
         * usbport is not waiting on it - so failing the record over it would
         * tear down a working hub to punish a lost description of it.
         *
         * `HubMarkDone` is deliberately **not** written. The command's effect is
         * unknown, which is exactly the state in which claiming the xHC holds
         * the marking is the one wrong answer; the flag stops the retry instead,
         * and the count says the hub is undescribed.
         */
        dev->Flags |= XHCI_DEV_FLAG_HUB_MARK_FAILED;
        ext->HubMarkFailures++;
        return;
    }

    /*
     * The command is gone and its effect is unknown - a timeout that turned into
     * an abort may or may not have executed. Retrying is therefore not an
     * option for anything that allocates (a second Enable Slot would leak the
     * first), so the record fails; its transfers are answered once the stops
     * `xhciDevFailRecord` arms complete (or by usbport's own timeout if the
     * engine that lost this command cannot complete those either).
     *
     * The exception is a record that was already unwinding: there the command is
     * gone with no evidence either way about the slot, so the record is
     * *abandoned* rather than released - see xhciDevRelease.
     */
    if (dev->State == XHCI_DEV_STATE_GONE) {
        /* The slot is lost with the command, so nothing has shown the xHC let go
         * of it: any pooled ring this record holds is quarantined rather than
         * recycled (see xhciDevRelease). A record with no Slot ID is released,
         * unless the lost command was the Enable Slot itself: the xHC may have
         * allocated a slot no record names, and the completion path adopts the
         * Slot ID even on failure, so that record is abandoned in place. */
        xhciDevRelease(ext, dev,
                       (dev->SlotId == 0 && op != XHCI_DEV_OP_ENABLE_SLOT)
                           ? 1UL : 0UL);
        return;
    }
    if (dev->State != XHCI_DEV_STATE_FREE) {
        xhciDevFinishSetAddress(ext, dev, XHCI_USBD_STATUS_INTERNAL_HC_ERROR);
        xhciDevFail(ext, dev, XHCI_DEV_OP_NONE);
    }
}

/* ------------------------------------------------------------------ */
/* Task 6-B.5: what the port says                                      */
/* ------------------------------------------------------------------ */

/*
 * Tear down every device that reaches the bus **through** this root port - task
 * 7b-A.3.
 *
 * A behind-hub record carries `HubPort` 0, so none of the three root-hub
 * teardown triggers can find one, and every one of them means the same thing
 * for the whole subtree: if the hub on this port is gone or has been taken out
 * of service, so is everything below it. Leaving the children would strand
 * their Slot IDs and their usbport addresses, which is the defect a root-port
 * record's own teardown exists to prevent.
 *
 * Keyed on the **root port** rather than on the departing record's address,
 * deliberately: the hub's record may already have been released by an earlier
 * trigger, and the children would then have nothing to be found by. Descendants
 * at every tier match, so no walk of the tree is needed.
 *
 * Called with the lock held. IRQL: DISPATCH_LEVEL.
 */
static VOID xhciDevTeardownBehind(PXHCI_EXTENSION ext, ULONG hubPort)
{
    ULONG rootPort;
    ULONG i;

    rootPort = XhciRootHubPortOf(&ext->RootHub, hubPort);
    if (rootPort == 0) {
        return;
    }
    for (i = 0; i < XHCI_MAX_SLOTS; i++) {
        PXHCI_DEVICE dev = &ext->Devices[i];

        if (dev->State == XHCI_DEV_STATE_FREE ||
            dev->State == XHCI_DEV_STATE_GONE) {
            continue;
        }
        if (dev->HubPort != 0 || dev->RootPort != rootPort) {
            continue;
        }
        ext->BehindHubGone++;
        XHCI_DBG_VALUE_CHANGED("slot: a root port going down took a "
                               "behind-hub device with it, tier << 24 | route",
                               (dev->Tier << 24) | dev->RouteString);
        xhciDevTeardown(ext, dev);
    }
}

/* IRQL: DISPATCH_LEVEL, controller lock held. */
VOID XhciSlotPortConnectChanged(PXHCI_EXTENSION ext, ULONG hubPort)
{
    PXHCI_DEVICE dev;

    if (ext == NULL) {
        return;
    }
    /*
     * **Either direction is a teardown.** A disconnect obviously; a *connect*
     * too, because a connect change on a port that already has a record means
     * the device that record describes is gone - whether or not the port is
     * occupied again by the time this runs. Reading CCS to decide would make the
     * teardown depend on how fast the replug was.
     */
    xhciDevTeardownBehind(ext, hubPort);
    dev = xhciDevByHubPort(ext, hubPort);
    if (dev == NULL) {
        return;
    }
    XHCI_DBG_VALUE_CHANGED("slot: connect change tore down a device, "
                           "hub port << 8 | slot", (hubPort << 8) | dev->SlotId);
    xhciDevTeardown(ext, dev);
}

/*
 * **The second teardown trigger, and the batch 6-V Win98 run is why there is
 * one.**
 *
 * Task 6-B.5 derived teardown from the connect change alone, and said in its own
 * text that this was "a design choice on top of that, not something the binaries
 * state". The choice was incomplete. usbhub also abandons a device by
 * **disabling its port while the device stays physically connected** - which is
 * what a cancelled driver-install wizard produces, and what it does to any
 * enumeration it gives up on. No connect change follows, because nothing was
 * unplugged; `PED` going to 0 is the whole of the event.
 *
 * What it costs to ignore is not a leaked record - it is the **address map**.
 * usbport frees that device's USB address when it destroys the device, and this
 * driver's map keeps it, so the next device usbport hands that address to is
 * refused by the SET_ADDRESS interception's "another record already holds it"
 * rule and never enumerates. Measured: two ports disabled after two cancelled
 * wizards, then a replug answered `refused SET_ADDRESS for address 1`.
 *
 * A disabled port cannot carry traffic in either direction, so the device behind
 * it is unreachable whatever its record says - which is what makes tearing down
 * here correct rather than merely convenient.
 *
 * **Only the explicit callback drives this, not an observed `PED` = 0.** A port
 * being reset also reads PED = 0 (it is cleared automatically when PR is set,
 * Table 5-27 p.371), so deriving the trigger from the shadow would tear down
 * every device in the middle of the reset that is enumerating it. The callback
 * says "the caller has taken this port out of service" and nothing else does.
 *
 * **This is two obligations, not one, and conflating them is what the first two
 * drafts each got wrong in opposite directions.** A batch 6-V Win98 run showed
 * why:
 *
 *   `disown` is about **usbport's** view. It called this, so it has destroyed
 *   its device object and freed that device's USB address. That is true the
 *   instant the callback arrives, whatever the hardware did with the write - and
 *   it is the half that matters for the defect this exists for, because a
 *   surviving address-map entry is what refuses the next device. Gating it on
 *   hardware left it unfixed whenever the PORTSC write was declined, refused or
 *   never confirmed, which is exactly what the run measured.
 *
 *   `release` is about **the controller**. Completing the device's queued
 *   transfers hands their mapped buffers back to usbport while the TRBs naming
 *   them may still be executable, so that half waits for the port to be
 *   confirmed down - the DMA hazard `XhciSlotInvalidateAll` exists to prevent.
 *
 * So the caller announces the disown immediately and the release when the port
 * confirms it. The same split `XhciSlotInvalidateAll` already makes between
 * updating bookkeeping and releasing what hardware can still reach.
 *
 * IRQL: DISPATCH_LEVEL, controller lock held.
 */
/* One record's half of it, so the root-port device and everything behind it are
 * given up in exactly the same way (task 7b-A.3). Called with the lock held. */
static VOID xhciDevDisown(PXHCI_EXTENSION ext, PXHCI_DEVICE dev, ULONG hubPort)
{
    if ((dev->Flags & XHCI_DEV_FLAG_DISOWNED) != 0) {
        return;
    }

    /*
     * The address map entry and the usbport binding go now; the slot, the ring
     * and anything queued stay until the port is confirmed down. Marking the
     * record refuses further transfers through the state check below, so nothing
     * new is accepted for a device usbport has stopped believing in.
     */
    dev->Flags |= XHCI_DEV_FLAG_DISOWNED;
    dev->Flags &= ~(XHCI_DEV_FLAG_EP0_OPEN | XHCI_DEV_FLAG_ADDRESS_VALID);
    /*
     * Task 7b-A.1: the topology graph is keyed on usbport's address, and this
     * is the instant that address stops naming this device - usbport has freed
     * it and will hand it to the next one. A node left behind would give a
     * later device the departed hub's position, and everything below a hub
     * that has gone is gone with it, which is what the detach prunes. Before
     * the field is cleared, because the address is the key.
     */
    xhciDevTopoDetach(ext, dev);
    dev->DeviceAddress = 0;
    dev->EndpointExtension = NULL;
    /* The binding this debt would have been paid through has just gone. */
    xhciDevDropInvalidate(ext, &dev->Flags, XHCI_DEV_FLAG_INVALIDATE_EP0);
    ext->DevicesDisownedOut++;
    XHCI_DBG_VALUE_CHANGED("slot: port disowned a device, hub port << 8 | slot",
                           (hubPort << 8) | dev->SlotId);
}

/* IRQL: DISPATCH_LEVEL, controller lock held - the same contract as the
 * `xhciDevDisown` it calls per record. */
VOID XhciSlotPortDisowned(PXHCI_EXTENSION ext, ULONG hubPort)
{
    PXHCI_DEVICE dev;
    ULONG rootPort;
    ULONG i;

    if (ext == NULL) {
        return;
    }

    /*
     * **Everything behind the port is disowned too** (task 7b-A.3), and for the
     * half of this callback that matters most: usbport has destroyed the device
     * objects for the whole subtree and freed *every* one of those USB
     * addresses. A behind-hub record that kept its address would refuse the next
     * device given it through the SET_ADDRESS interception's "another record
     * already holds it" rule - the measured Phase 6 defect, one tier down.
     *
     * Their slots and rings stay exactly as the hardware may still be reading
     * them, like the root-port record's: what the release waits on is the port
     * being confirmed down, which `XhciSlotPortDisabled` then applies to the
     * same set.
     */
    rootPort = XhciRootHubPortOf(&ext->RootHub, hubPort);
    if (rootPort != 0) {
        for (i = 0; i < XHCI_MAX_SLOTS; i++) {
            PXHCI_DEVICE child = &ext->Devices[i];

            if (child->State == XHCI_DEV_STATE_FREE ||
                child->State == XHCI_DEV_STATE_GONE) {
                continue;
            }
            if (child->HubPort != 0 || child->RootPort != rootPort) {
                continue;
            }
            xhciDevDisown(ext, child, hubPort);
        }
    }

    dev = xhciDevByHubPort(ext, hubPort);
    if (dev == NULL) {
        return;
    }
    xhciDevDisown(ext, dev, hubPort);
}

/*
 * The port has now been *observed* out of service, so the half that touches
 * memory the controller could still be reading may run.
 *
 * IRQL: DISPATCH_LEVEL, controller lock held.
 */
VOID XhciSlotPortDisabled(PXHCI_EXTENSION ext, ULONG hubPort)
{
    PXHCI_DEVICE dev;

    if (ext == NULL) {
        return;
    }
    /* The subtree first, for the same reason the connect change takes it: a
     * port out of service carries no traffic in either direction, so nothing
     * behind it is reachable whatever its record says. */
    xhciDevTeardownBehind(ext, hubPort);
    dev = xhciDevByHubPort(ext, hubPort);
    if (dev == NULL) {
        return;
    }
    ext->DevicesDisabledOut++;
    XHCI_DBG_VALUE_CHANGED("slot: port disable tore down a device, "
                           "hub port << 8 | slot", (hubPort << 8) | dev->SlotId);
    xhciDevTeardown(ext, dev);
}

/*
 * Is an enumeration this driver has already served **still running** on this
 * port?
 *
 * That is the question task 7b-A.1.1 turns on, and it is asked of the record
 * rather than of a step this driver remembers taking. A record that is on the
 * port, holds EP0, and has no valid address yet is one whose address-0 open has
 * happened and whose SET_ADDRESS has not: precisely the window usbhub's second
 * port reset falls in. Everything else - addressed, failed, disowned, torn down,
 * never opened - is a port whose next address-0 open would be a *new*
 * enumeration, and those must keep their entitlement.
 *
 * `FAILED` is excluded explicitly because it is the state the health poll's
 * progress detector puts a stalled record into, and the re-enumeration that
 * follows is the recovery measured on both targets. `FREE` and `GONE` are
 * excluded by xhciDevByHubPort itself.
 *
 * Called with the lock held.
 */
static ULONG xhciDevEnumerationInProgress(PXHCI_EXTENSION ext, ULONG hubPort)
{
    PXHCI_DEVICE dev;

    dev = xhciDevByHubPort(ext, hubPort);
    if (dev == NULL || dev->State == XHCI_DEV_STATE_FAILED) {
        return 0;
    }
    if ((dev->Flags & XHCI_DEV_FLAG_ADDRESS_VALID) != 0) {
        return 0;
    }
    return ((dev->Flags & (XHCI_DEV_FLAG_EP0_OPEN | XHCI_DEV_FLAG_DISOWNED)) ==
            XHCI_DEV_FLAG_EP0_OPEN) ? 1UL : 0UL;
}

/* IRQL: DISPATCH_LEVEL, controller lock held. */
VOID XhciSlotPortReset(PXHCI_EXTENSION ext, ULONG hubPort)
{
    if (ext == NULL || hubPort == 0) {
        return;
    }
    /* Counted whatever is decided below: this counts port resets, and the
     * suppression is about what one of them entitles. */
    ext->EnumSequence++;

    /*
     * **Task 7b-A.1.1: the second reset of an enumeration bracket entitles
     * nothing.** usbhub resets the port, opens EP0 at address 0, reads the
     * device descriptor, resets the port *again*, and only then sends
     * SET_ADDRESS - so this reset is the one in the middle of an enumeration
     * whose claim is already spent, not the opening of a new one. Re-arming here
     * is what left an unspent claim lying on a hub's root port for a device
     * behind that hub to consume (the 2b measurement).
     *
     * At most one per claim: `EnumResetSuppressed` is the bound, because a record
     * can be left mid-enumeration by an abandonment that produces no refusal for
     * the progress detector to act on, and a rule with no bound would then
     * disable this port permanently.
     */
    if (ext->EnumClaimSpent && !ext->EnumResetSuppressed &&
        xhciDevEnumerationInProgress(ext, hubPort)) {
        ext->EnumResetSuppressed = 1;
        ext->EnumResetsSuppressed++;
        XHCI_DBG_VALUE_CHANGED("slot: a mid-enumeration port reset re-armed "
                               "nothing, hub port", hubPort);
        return;
    }

    ext->EnumHubPort = hubPort;
    /* The reset is what entitles one address-0 open to claim a root port, so it
     * re-arms the claim as well as naming the port - and re-arms the one
     * suppression that claim is allowed. */
    ext->EnumClaimSpent = 0;
    ext->EnumResetSuppressed = 0;
    /*
     * **And it supersedes any pending hub claim** (task 7b-A.3). Enumeration is
     * serialized above this driver - one port reset, one device created at
     * address 0 (design doc 02 section 2, measured in batch 7b-V0) - so a hub
     * claim still armed when a root port is reset belongs to a bracket that was
     * abandoned, and nothing will ever spend it. Dropping it here is what keeps
     * the two entitlements mutually exclusive, which is why the open needs no
     * rule for which one to believe: at most one is ever armed. The alternative -
     * leaving both and picking - was the shape of the 7b-A.0 hijack, where the
     * root-port answer was reached first and applied to a device that was not
     * there.
     */
    XhciTopoDropPending(&ext->Topology);
}

/* ------------------------------------------------------------------ */
/* The deferred half: commands, completions and usbport services       */
/* ------------------------------------------------------------------ */

/*
 * Compose the next owed command and record its owner, then submit it with the
 * lock released.
 *
 * Returns 1 if it did something - which is what lets XhciSlotDeferredWork loop
 * until the driver is quiet rather than doing one thing per call.
 *
 * IRQL: <= DISPATCH_LEVEL, controller lock **not** held.
 */
static ULONG xhciDevPumpCommand(PXHCI_EXTENSION ext)
{
    XHCI_TRB trb;
    XHCI_EP_BINDING binding;
    KIRQL oldIrql;
    PXHCI_DEVICE dev;
    PXHCI_ENDPOINT_RECORD record;
    ULONG i;
    ULONG index;
    ULONG op;
    ULONG ref;
    ULONG inputPA;
    ULONG builtOk;
    ULONG status;

    XhciControllerLockAcquire(&oldIrql);

    if (ext->CommandOwner != 0 || !xhciDevAdmitted(ext)) {
        XhciControllerLockRelease(oldIrql);
        return 0;
    }

    dev = NULL;
    index = 0;
    op = XHCI_DEV_OP_NONE;
    for (i = 0; i < XHCI_MAX_SLOTS; i++) {
        index = (ext->PumpCursor + i) % XHCI_MAX_SLOTS;
        op = xhciDevOwedOp(ext, &ext->Devices[index], &binding);
        if (op != XHCI_DEV_OP_NONE) {
            dev = &ext->Devices[index];
            break;
        }
    }
    if (dev == NULL) {
        XhciControllerLockRelease(oldIrql);
        return 0;
    }
    ext->PumpCursor = (index + 1) % XHCI_MAX_SLOTS;

    record = NULL;
    builtOk = 0;
    switch (op) {
    case XHCI_DEV_OP_ENABLE_SLOT:
        builtOk = (XhciTrbEnableSlot(&trb, xhciDevSlotType(ext, dev->RootPort)) ==
                   XHCI_RING_OK);
        break;
    case XHCI_DEV_OP_ADDRESS_BSR:
    case XHCI_DEV_OP_ADDRESS_SET:
        inputPA = xhciDevBuildInput(ext, dev, dev->MaxPacketSize0, 1);
        builtOk = (inputPA != 0 &&
                   XhciTrbAddressDevice(&trb, dev->SlotId, inputPA,
                                        (op == XHCI_DEV_OP_ADDRESS_BSR) ? 1 : 0)
                       == XHCI_RING_OK);
        break;
    case XHCI_DEV_OP_EVALUATE_MPS:
        inputPA = xhciDevBuildInput(ext, dev, dev->WantedMaxPacketSize0, 0);
        builtOk = (inputPA != 0 &&
                   XhciTrbEvaluateContext(&trb, dev->SlotId, inputPA) ==
                       XHCI_RING_OK);
        break;
    case XHCI_DEV_OP_CONFIGURE_EP:
        /*
         * The record is picked here and not by the caller, because "which
         * endpoint owes one" is only answerable under the lock and the pump is
         * the only thing holding it at this point. `ConfigureDci` is what the
         * completion resolves it back through - the record pointer itself must
         * not be carried across the unlock.
         */
        record = xhciEpFirstPending(dev);
        if (record != NULL) {
            dev->EndpointOpDci = record->Dci;
            inputPA = xhciDevBuildConfigureInput(ext, dev, record);
            builtOk = (inputPA != 0 &&
                       XhciTrbConfigureEndpoint(&trb, dev->SlotId, inputPA, 0) ==
                           XHCI_RING_OK);
        }
        break;
    case XHCI_DEV_OP_MARK_HUB:
        inputPA = xhciDevBuildMarkHubInput(ext, dev);
        builtOk = (inputPA != 0 &&
                   XhciTrbConfigureEndpoint(&trb, dev->SlotId, inputPA, 0) ==
                       XHCI_RING_OK);
        break;
    case XHCI_DEV_OP_STOP_EP:
        /* SP = 0: this is a cancellation or a teardown, not the suspend stop a
         * controller entering a low-power state performs. */
        dev->EndpointOpDci = binding.Dci;
        builtOk = (XhciTrbStopEndpoint(&trb, dev->SlotId, binding.Dci, 0) ==
                   XHCI_RING_OK);
        break;
    case XHCI_DEV_OP_RESET_EP:
        /* TSP = 0, which is what resets the Data Toggle (4.6.8 p.115). TSP = 1
         * is the Soft Retry of 4.6.8.1 and deliberately preserves it, which is
         * the opposite of what a reset-pipe means. */
        dev->EndpointOpDci = binding.Dci;
        builtOk = (XhciTrbResetEndpoint(&trb, dev->SlotId, binding.Dci, 0) ==
                   XHCI_RING_OK);
        break;
    case XHCI_DEV_OP_SET_DEQUEUE:
        /*
         * The address and its Dequeue Cycle State come from the value the
         * placement recorded, not from a second read of the ring: they are one
         * decision, and re-deriving them here would let the command and the
         * completion's consistency check disagree about which position was
         * programmed.
         */
        dev->EndpointOpDci = binding.Dci;
        builtOk = (XhciTrbSetTrDequeue(&trb, dev->SlotId, binding.Dci,
                                       binding.Quiesce->DequeuePA & ~1UL,
                                       binding.Quiesce->DequeuePA & 1UL) ==
                   XHCI_RING_OK);
        break;
    case XHCI_DEV_OP_DISABLE_SLOT:
        if (xhciDevHasFailedQuiesce(dev)) {
            /*
             * 4.6.4 p.97's precondition is unmet: an endpoint of this slot
             * could not be shown stopped. The Disable Slot goes out anyway -
             * holding the slot for the life of the driver is the worse of the
             * two bad outcomes, and it is the containment ladder Phase 4 task 7
             * already established - but it is counted, because a nonzero reading
             * here is the only thing that says a teardown was undefined
             * behaviour rather than merely slow.
             */
            ext->TeardownsWithoutStop++;
        }
        builtOk = (XhciTrbDisableSlot(&trb, dev->SlotId) == XHCI_RING_OK);
        break;
    case XHCI_DEV_OP_RESET_DEVICE:
        builtOk = (XhciTrbResetDevice(&trb, dev->SlotId) == XHCI_RING_OK);
        break;
    default:
        break;
    }

    if (!builtOk) {
        /*
         * The command could not be encoded at all, which is this driver's own
         * bookkeeping being wrong about the device rather than anything the
         * controller did. A record that cannot even be unwound is released
         * outright: there is nothing left to say to the hardware that would not
         * be another malformed command.
         */
        XHCI_DBG_VALUE_CHANGED("slot: could not encode a device command, op",
                               op);
        if (xhciDevOpIsQuiesce(op)) {
            /*
             * Endpoint-local, and `PendingOp` is deliberately **not** cleared:
             * a quiescence command is derived while `PendingOp` may hold the
             * teardown's Disable Slot, and clearing it here would drop the
             * unwind on the floor. The failure ends the derived need, which is
             * the only thing that could otherwise loop.
             */
            xhciEpQuiesceFail(ext, dev, &binding, op);
            XhciControllerLockRelease(oldIrql);
            return 1;
        }
        dev->PendingOp = XHCI_DEV_OP_NONE;
        if (op == XHCI_DEV_OP_CONFIGURE_EP) {
            /*
             * **The failure is the endpoint's, not the device's**, and it has to
             * be recorded on the record for a second reason beside honesty: the
             * need for this command is *derived* from the record's state, so
             * leaving it PENDING would have the pump rebuild the same
             * unbuildable command for ever. Clearing `PendingOp` - which is
             * already NONE here - cannot end it.
             *
             * The device keeps EP0 and its address. A context this driver could
             * not encode says nothing about the ones it already did.
             */
            if (record != NULL) {
                record->State = XHCI_EP_REC_FAILED;
                xhciEpOweInvalidate(ext, record);
            }
            ext->EndpointConfigureFailures++;
            XhciControllerLockRelease(oldIrql);
            return 1;
        }
        if (op == XHCI_DEV_OP_MARK_HUB) {
            /*
             * **Failing the device over a hub marking would be the wrong trade
             * by a wide margin**: the hub is enumerated, its interrupt pipe is
             * running, and what could not be encoded is a description of it -
             * so the record keeps everything and only the marking gives up. Same
             * shape as the Configure Endpoint above, and for the same second
             * reason: the need is derived, so nothing but the flag ends it.
             */
            dev->Flags |= XHCI_DEV_FLAG_HUB_MARK_FAILED;
            ext->HubMarkFailures++;
            XhciControllerLockRelease(oldIrql);
            return 1;
        }
        if (dev->State == XHCI_DEV_STATE_GONE) {
            xhciDevRelease(ext, dev, (dev->SlotId == 0) ? 1UL : 0UL);
        } else {
            xhciDevFail(ext, dev, op);
        }
        XhciControllerLockRelease(oldIrql);
        return 1;
    }

    ref = xhciDevRef(ext, dev);
    if (op == XHCI_DEV_OP_CONFIGURE_EP) {
        /* CONFIGURE_EP is never in `PendingOp`, so what stops the pump reissuing
         * it is the record leaving the PENDING state the derivation reads. */
        record->State = XHCI_EP_REC_CONFIGURING;
    } else if (xhciDevOpIsQuiesce(op)) {
        /*
         * Same shape one layer down: the need is derived from the NEED bits, so
         * the one being issued is cleared as the command goes out and `BUSY`
         * holds the derivation off until its completion. `PendingOp` is
         * untouched - it may be holding the teardown's Disable Slot, which is
         * exactly what these commands are running ahead of.
         *
         * **Only the bit being issued**, and the third review round is why. A
         * blanket clear looked safer and dropped a debt: an abort arriving while
         * a stop was in flight arms a *second* stop, the first stop's completion
         * places the position and owes a Set TR Dequeue Pointer, and the pump
         * then issues that second stop - which under a blanket clear took the
         * dequeue debt with it. The software ring had moved and the hardware's
         * had not, with nothing left to reconcile them. The case the blanket
         * clear was written for is handled where it belongs instead: a stop that
         * finds the endpoint Disabled clears `NEED_DEQUEUE` in `xhciEpStopped`,
         * because that is where it is known.
         */
        binding.Quiesce->Flags &=
            ~((op == XHCI_DEV_OP_STOP_EP) ? XHCI_EPQ_NEED_STOP :
              (op == XHCI_DEV_OP_RESET_EP) ? XHCI_EPQ_NEED_RESET :
                                             XHCI_EPQ_NEED_DEQUEUE);
        binding.Quiesce->Flags |= XHCI_EPQ_BUSY;
        if (op == XHCI_DEV_OP_SET_DEQUEUE) {
            /* Stamped here so the completion can tell "this command paid the
             * debt" from "a newer debt was raised while it was in flight" -
             * `xhciEpOweReposition` clears it. */
            binding.Quiesce->Flags |= XHCI_EPQ_DEQUEUE_ISSUED;
        }
    } else if (op == XHCI_DEV_OP_MARK_HUB) {
        /*
         * Never in `PendingOp` either, so there is nothing to clear - and
         * clearing it anyway would be a rule stated in one place and quietly
         * broken in another. What holds the derivation off while this is in
         * flight is the engine's own one-command-at-a-time gate; what ends it is
         * the completion writing `HubMarkDone`.
         *
         * Counted as an **attempt**: a submit the command engine refuses leaves
         * the need derived, so the next pump builds it again and counts again.
         * That is the honest reading for what this number is for - whether the
         * measured ordering (descriptor before the endpoint open) held, in which
         * case it is 0.
         */
        ext->HubMarkCommands++;
    } else {
        dev->PendingOp = XHCI_DEV_OP_NONE;
    }
    dev->ActiveOp = op;
    dev->OpAgeArmed = 0;
    ext->CommandOwner = ref;
    ext->CommandOwnerOp = op;
    /* The generation `CommandOwner` does not carry. Captured here, with the
     * reservation and under the same lock hold, because that is the instant the
     * command is about to describe: anything that re-enters the record after it
     * gets a new tenancy, and the completion is then no longer this device's. */
    ext->CommandOwnerTenancy = dev->Tenancy;

    XhciControllerLockRelease(oldIrql);

    status = XhciCommandSubmit(ext, &trb, NULL);
    if (status == XHCI_CMD_OK) {
        return 1;
    }

    XhciControllerLockAcquire(&oldIrql);
    /*
     * Back the reservation out, but only if it is still ours: a completion for
     * something else could have cleared the owner in between, and a blind reset
     * would drop a reservation this call never made.
     */
    if (ext->CommandOwner == ref && ext->CommandOwnerOp == op) {
        ext->CommandOwner = 0;
        ext->CommandOwnerOp = XHCI_DEV_OP_NONE;
        dev->ActiveOp = XHCI_DEV_OP_NONE;

        /*
         * Re-resolved rather than reused: the lock was dropped across the
         * submit, and the pointers taken before it would name a record inside a
         * device the teardown path may have zeroed. `EndpointOpDci` survives
         * that because it is read back through a lookup that checks the record
         * is still there.
         */
        if (op == XHCI_DEV_OP_CONFIGURE_EP) {
            record = xhciEpByDci(dev, dev->EndpointOpDci);
            if (record != NULL && record->State != XHCI_EP_REC_CONFIGURING) {
                record = NULL;
            }
        }
        if (xhciDevOpIsQuiesce(op)) {
            /*
             * Endpoint-local on every status, including a GONE device's: the
             * teardown's Disable Slot is in `PendingOp` and is not this
             * command's to touch. A transient refusal puts the need back, so
             * the next pump reissues it; anything else gives the chain up,
             * which is what lets the Disable Slot through with
             * `TeardownsWithoutStop` counted rather than waiting for a stop
             * that is not coming.
             */
            if (!xhciEpResolve(dev, dev->EndpointOpDci, &binding)) {
                XhciControllerLockRelease(oldIrql);
                return 0;
            }
            binding.Quiesce->Flags &= ~XHCI_EPQ_BUSY;
            if (status == XHCI_CMD_BUSY || status == XHCI_CMD_NOT_READY ||
                status == XHCI_CMD_RING_FULL || status == XHCI_CMD_NO_TIMER) {
                binding.Quiesce->Flags |=
                    (op == XHCI_DEV_OP_STOP_EP) ? XHCI_EPQ_NEED_STOP :
                    (op == XHCI_DEV_OP_RESET_EP) ? XHCI_EPQ_NEED_RESET :
                                                   XHCI_EPQ_NEED_DEQUEUE;
            } else {
                xhciEpQuiesceFail(ext, dev, &binding, op);
            }
            XhciControllerLockRelease(oldIrql);
            return 0;
        }

        /*
         * **The record changed hands while the lock was dropped across the
         * submit**, so everything below is about a device that is no longer in
         * it. The tenancy guard in `XhciSlotCommandEvent` closed this window for
         * a command that *completed*; a review round found the same window open
         * for one that was *refused*, reached through the same unlock.
         *
         * Nothing here is unwound and nothing is put back. The submit failed, so
         * the command never executed and has no effect on the slot to reverse -
         * and the re-entry that took the record has already decided its chain
         * from a reading taken after that. Restoring `op` would write the old
         * device's debt over the new device's: a refused `DISABLE_SLOT` put back
         * this way is re-issued under the *new* tenancy, so the completion guard
         * no longer sees it as stale and it releases a record usbport has bound
         * an endpoint to; a refused `EVALUATE_MPS` put back over an owed
         * `RESET_DEVICE` completes as a no-op and leaves the record `ENABLED`
         * with nothing owed, refusing every EP0 transfer until the next recovery
         * cycle.
         *
         * The quiescence back-out above is deliberately ahead of this and does
         * not take it: those are endpoint-local, and a re-entry keeps every
         * record's quiescence state precisely so an outstanding one still means
         * what it meant.
         */
        if (dev->Tenancy != ext->CommandOwnerTenancy) {
            ext->CommandsStaleTenancy++;
            XHCI_DBG_VALUE_CHANGED("slot: command submit refused for a tenancy "
                                   "that has been re-enumerated, op << 8 | "
                                   "status", (op << 8) | (ULONG)status);
            XhciControllerLockRelease(oldIrql);
            return 0;
        }

        if (dev->State == XHCI_DEV_STATE_GONE) {
            /*
             * **The teardown is waiting on a completion that is not coming**,
             * and this branch has to come first for that reason alone.
             *
             * `xhciDevTeardown` returns without scheduling anything when
             * `ActiveOp` is nonzero, because a command in flight will finish the
             * unwind. Here the submit was *refused*, so there is no completion -
             * and the tenth review found that a Configure Endpoint taking this
             * path left the record GONE with no owed operation at all:
             * `xhciDevOwedOp` derives nothing for a GONE device, so the slot,
             * its DCBAA entry and its pooled rings would have been held for the
             * life of the driver.
             *
             * The endpoint-specific rollbacks below are still done, so the
             * record does not sit in CONFIGURING for ever either; what changes
             * is that the device's own unwind is resumed rather than skipped.
             */
            if (op == XHCI_DEV_OP_CONFIGURE_EP && record != NULL) {
                record->State = XHCI_EP_REC_FAILED;
            }
            if (dev->SlotId != 0) {
                dev->PendingOp = XHCI_DEV_OP_DISABLE_SLOT;
            } else {
                xhciDevRelease(ext, dev, 1);
            }
        } else if (status == XHCI_CMD_BUSY || status == XHCI_CMD_NOT_READY ||
            status == XHCI_CMD_RING_FULL || status == XHCI_CMD_NO_TIMER) {
            /*
             * Transient by construction: another command is in flight, the
             * controller is between states, the ring is momentarily full, or
             * there was no timer to arm this instant. The operation goes back on
             * the record and the next pump - at worst the next CheckController
             * poll - reissues it.
             */
            if (op == XHCI_DEV_OP_CONFIGURE_EP) {
                if (record != NULL) {
                    record->State = XHCI_EP_REC_PENDING;
                }
            } else if (op == XHCI_DEV_OP_MARK_HUB) {
                /* Nothing to put back: the need is derived from the graph and
                 * `HubMarkDone`, neither of which the attempt touched, so the
                 * next pump asks the same question and gets the same answer.
                 * Writing it into `PendingOp` would be the one place this op is
                 * allowed to collide with the EP0 chain. */
            } else if (op == XHCI_DEV_OP_ADDRESS_SET) {
                /* Nothing to put back here either, and it has to stay that way:
                 * the need is derived from `PendingSetAddress`, and that pointer
                 * is what `AbortTransfer` and an EP0 `REMOVE` clear when they
                 * answer the transfer. A stored `ADDRESS_SET` would survive
                 * them - the two paths do not touch `PendingOp` - and the next
                 * pump would issue Address Device for a transfer that has
                 * already been completed, marking the record Addressed and
                 * address-valid with nothing to hand back. */
            } else {
                dev->PendingOp = op;
            }
        } else if (op == XHCI_DEV_OP_MARK_HUB) {
            /* A marking the engine would not take. The device is intact and its
             * endpoints are running; only the description of it gives up, and
             * only the flag can end a derived need. */
            dev->Flags |= XHCI_DEV_FLAG_HUB_MARK_FAILED;
            ext->HubMarkFailures++;
        } else if (op == XHCI_DEV_OP_CONFIGURE_EP) {
            /* Endpoint-local again, and for the same two reasons as the
             * unbuildable case: the device is intact, and only the record's
             * state can end the derived need. */
            if (record != NULL) {
                record->State = XHCI_EP_REC_FAILED;
                xhciEpOweInvalidate(ext, record);
            }
            ext->EndpointConfigureFailures++;
        } else {
            xhciDevFail(ext, dev, op);
        }
    }
    XhciControllerLockRelease(oldIrql);
    /* A refusal is progress in the sense the loop cares about only if something
     * changed; a transient one must not spin. */
    return 0;
}

/*
 * The `SubmitTransfer` bracket. Both take the controller lock rather than
 * touching the counter bare, because `XhciSlotDeferredWork` reads it under that
 * lock and every other `Flags`-adjacent field in this driver is written the
 * same way (design doc 05).
 *
 * IRQL: <= DISPATCH_LEVEL, controller lock **not** held.
 */
VOID XhciSlotEnterSubmit(PXHCI_EXTENSION ext)
{
    KIRQL oldIrql;

    if (ext == NULL || ext->Signature != XHCI_EXTENSION_SIGNATURE) {
        return;
    }
    XhciControllerLockAcquire(&oldIrql);
    ext->SubmitDepth++;
    XhciControllerLockRelease(oldIrql);
}

VOID XhciSlotLeaveSubmit(PXHCI_EXTENSION ext)
{
    KIRQL oldIrql;

    if (ext == NULL || ext->Signature != XHCI_EXTENSION_SIGNATURE) {
        return;
    }
    XhciControllerLockAcquire(&oldIrql);
    if (ext->SubmitDepth != 0) {
        ext->SubmitDepth--;
    } else {
        /* The saturation keeps a future bracket imbalance from wedging every
         * delivery, but silently absorbing it would also hide the imbalance
         * for ever - this is the one observable a mispaired Leave has
         * (Phase 7 review, C2). */
        ext->SubmitUnderflows++;
    }
    /*
     * The bracket is closing, so completions stamped under it become
     * deliverable - but only to a drain pass that *begins* from here on
     * (finding A7): the pass epoch snapshot has to exceed the stamp, and a
     * pass already inside its loop snapshotted before this line.
     */
    ext->SubmitEpoch++;
    XhciControllerLockRelease(oldIrql);

    /*
     * **And it deliberately does not drain here**, though the first draft of
     * this function did, to spare a held completion the wait for the next poll.
     * That would have reintroduced the very defect: this runs inside the
     * dispatch wrapper, which is inside usbport's own `SubmitTransfer` call, so
     * the record is still one usbport is about to write to. There is no point
     * inside this driver's own call stack at which the callback is over - the
     * *caller* is what has to return - so the delivery belongs to a different
     * context and nothing else will do.
     */
}

/* IRQL: <= DISPATCH_LEVEL, controller lock **not** held. */
VOID XhciSlotDeferredWork(PXHCI_EXTENSION ext)
{
    KIRQL oldIrql;
    PXHCI_TRANSFER transfer;
    PVOID endpointExtension;
    ULONG passEpoch;
    ULONG heldBySubmitCounted;
    ULONG heldByPassCounted;

    if (ext == NULL || ext->Signature != XHCI_EXTENSION_SIGNATURE) {
        return;
    }
    heldBySubmitCounted = 0;
    heldByPassCounted = 0;

    XhciControllerLockAcquire(&oldIrql);
    if (ext->DeferredBusy) {
        /*
         * Either this function is already on this stack - a usbport callback
         * that drains on entry can be reached from one that is already
         * draining - or another CPU is inside it. Returning is right in both
         * cases: the active call re-checks everything after each item, and
         * usbport's 500 ms poll reaches XhciSlotPoll if the handoff is missed.
         *
         * It is **not** reached by `UsbPortCompleteTransfer` answering with a
         * fresh `SubmitTransfer` on this stack, which is what an earlier draft
         * of this comment claimed. That service calls no miniport slot at all:
         * it unlinks the transfer, `ExfInterlockedInsertTailList`s it onto the
         * FDO done list, queues a DPC and sets a worker event, then returns
         * (SP4 `0001A4EA` -> `000183C0`/`000154B0`, NUSB `0001A0D4` ->
         * `00017FA4`/`000152EE`). The guard is still needed; only its reason
         * was wrong.
         */
        ext->DeferredReentries++;
        XhciControllerLockRelease(oldIrql);
        return;
    }
    ext->DeferredBusy = 1;
    /*
     * The pass epoch (finding A7): everything this invocation delivers must
     * have been queued under a submit bracket that had already closed when
     * the pass began. Snapshotted once, in the same hold that claims the
     * drain, so "this pass" has exactly one meaning.
     */
    passEpoch = ext->SubmitEpoch;
    XhciControllerLockRelease(oldIrql);

    for (;;) {
        /*
         * Completions first. A transfer usbport is already waiting on outranks
         * the next step of a chain, and completing one is what usually produces
         * the next submission - so draining first keeps the two moving together
         * rather than alternating.
         */
        XhciControllerLockAcquire(&oldIrql);
        /*
         * **Except while a `SubmitTransfer` is on a stack.** usbport writes to
         * the transfer record after that callback returns success, so a
         * completion delivered from inside it hands back a record usbport then
         * touches (XHCI_EXTENSION.SubmitDepth carries the call site and the
         * batch 7a-V fault). The transfer stays on the list, and the next event
         * DPC or XhciSlotPoll delivers it - which is inside usbport's own 10 s
         * URB timeout by more than an order of magnitude **while the
         * controller is running**. While suspended neither deliverer exists
         * (usbport gates the worker and the 500 ms timer on HC_SUSPEND, and
         * the quiesce clears INITIALIZED so the event DPC declines), which is
         * why `XhciSuspendController` drains this list before the quiesce
         * (Phase 7 review, B4) - the guarantee here is conditional and that
         * drain is what discharges the suspended case.
         *
         * Only the delivery is held: the pump below still runs here, because a
         * command issued on the submit stack is what keeps each enumeration
         * step off the 500 ms poll.
         */
        transfer = ext->CompletionHead;
        if (transfer != NULL && ext->SubmitDepth != 0) {
            /*
             * Latched once per pass (Phase 7 review, B5): this sits inside the
             * drain loop, so counting every iteration recounted the same held
             * completion each time pump or invalidate work made progress -
             * during an enumeration with held completions the counter outran
             * `TransfersFailed - EndpointGone` and mimicked the documented
             * stuck-depth signature. One pass, one reading.
             */
            if (!heldBySubmitCounted) {
                heldBySubmitCounted = 1;
                ext->CompletionsHeldBySubmit++;
            }
            transfer = NULL;
        }
        /*
         * Finding A7's second gate: the bracket this completion was queued
         * under must have closed *before this pass began*. `SubmitDepth == 0`
         * alone can be observed a handful of instructions after the
         * decrement, while usbport's post-callback writes are still coming;
         * a pass that was running then holds the completion for the next DPC
         * or poll. Wrap-safe: the epochs are compared by distance.
         */
        if (transfer != NULL &&
            (passEpoch - transfer->HeldEpoch) - 1UL >= 0x7FFFFFFFUL) {
            if (!heldByPassCounted) {
                heldByPassCounted = 1;
                ext->CompletionsHeldByPass++;
            }
            transfer = NULL;
        }
        if (transfer != NULL) {
            ext->CompletionHead = transfer->Next;
            if (ext->CompletionHead == NULL) {
                ext->CompletionTail = NULL;
            }
            transfer->Next = NULL;
            if (ext->CompletionsOwed != 0) {
                ext->CompletionsOwed--;
            }
            /*
             * **Published in the same lock hold as the pop**, which is what
             * makes the transfer reachable at every instant: off the queue, off
             * the completion list, and named here. The fourth review round found
             * the first draft publishing it in a *second* lock hold, which left
             * a window between the two where an abort on another CPU would find
             * it nowhere at all - and that half of the race is closable, unlike
             * the overlap with the service call itself.
             */
            ext->CompletingTransfer = transfer;
            /*
             * Task 7b-A.1: the last moment a snooped reply's bytes are
             * readable - usbport unmaps the buffer after the completion call
             * below, and this hold of the lock is the one that owns the pop.
             */
            xhciDevTopoFoldReply(ext, transfer);
            /* Task 9-A.2's reply, in the same window and for the same reason. */
            xhciDevDescApply(ext, transfer);
            /*
             * Task 9-A.1, and it is here for the same reason the fold above is:
             * this is the last moment usbport's own memory for this transfer is
             * certainly still valid, and it is the **one** place every
             * isochronous completion passes through. Any packet no event
             * answered gets the transfer's status and a length of zero, because
             * the block is zero-filled and zero is `USBD_STATUS_SUCCESS` - a
             * request cancelled or failed before it ever ran would otherwise
             * reach the client driver as every packet having succeeded with no
             * data.
             */
            XhciXferIsoFinalise(transfer);
        }
        XhciControllerLockRelease(oldIrql);

        if (transfer != NULL) {
            /*
             * **Completed from whichever context got here**, including one
             * running under usbport's own `MiniportSpinLock` - a
             * `SubmitTransfer` that had to fail a transfer outright, or an
             * `OpenEndpoint` that found completions owed. That is not an
             * assumption about the service being re-entrant: usbport's own
             * pending-transfer flush calls `SubmitTransfer` under that lock and
             * completes the failures inline, so the lock is one it is holding
             * across completions anyway. What this driver's lock discipline is
             * about is the *controller* lock, which is released here and is the
             * one a completion could deadlock against.
             */
            /*
             * **Which service answers is read off the transfer, not off the
             * endpoint.** An isochronous request is completed through
             * `UsbPortCompleteIsoTransfer` - four arguments, no status and no
             * length, because usbport takes both out of the per-packet block -
             * and an endpoint's records are rebuilt by a reopen while a transfer
             * already on this list still names the old binding, so asking the
             * endpoint what kind it is would be a second statement of a fact
             * that can drift.
             */
            if ((transfer->Flags & XHCI_XFER_FLAG_ISOCH) != 0) {
                if (XhciRegPacket.UsbPortCompleteIsoTransfer != NULL) {
                    (VOID)XhciRegPacket.UsbPortCompleteIsoTransfer(
                        ext, transfer->EndpointExtension,
                        transfer->TransferParameters, transfer->IsoParams);
                }
            } else if (XhciRegPacket.UsbPortCompleteTransfer != NULL) {
                XhciRegPacket.UsbPortCompleteTransfer(
                    ext, transfer->EndpointExtension,
                    transfer->TransferParameters, transfer->UsbdStatus,
                    transfer->BytesTransferred);
            }
            XhciControllerLockAcquire(&oldIrql);
            /* Only if it is still ours to clear. The binaries say the service
             * call re-enters no miniport slot (the corrected comment at the
             * DeferredBusy guard above), so nothing on this stack should have
             * moved it - the check is the same defensive posture the guard
             * keeps, not a claim that a re-entry happens. */
            if (ext->CompletingTransfer == transfer) {
                ext->CompletingTransfer = NULL;
            }
            XhciControllerLockRelease(oldIrql);
            continue;
        }

        XhciControllerLockAcquire(&oldIrql);
        endpointExtension = xhciDevTakeInvalidate(ext);
        XhciControllerLockRelease(oldIrql);

        if (endpointExtension != NULL) {
            if (XhciRegPacket.UsbPortInvalidateEndpoint != NULL) {
                ext->EndpointInvalidates++;
                (VOID)XhciRegPacket.UsbPortInvalidateEndpoint(
                    ext, endpointExtension);
            }
            continue;
        }

        if (!xhciDevPumpCommand(ext)) {
            break;
        }
    }

    XhciControllerLockAcquire(&oldIrql);
    ext->DeferredBusy = 0;
    XhciControllerLockRelease(oldIrql);
}

/*
 * The per-endpoint sweep of the health poll. Two nets, walking one list.
 *
 * The second is task 8-A.1's backpressure release (`xhciEpReleaseRetry`), the
 * backstop half of that mechanism: it runs for every live endpoint whatever
 * state it is in, which is what covers the ring space a completion did not free.
 *
 * The first is the net under `XHCI_EPQ_PAUSED`: an endpoint left Stopped with
 * work on it that nothing has restarted.
 *
 * Its ordinary ends are `SetEndpointState(ACTIVE)` and the next submission. This
 * exists because neither is *guaranteed* - usbport decides both - and an
 * endpoint stopped for ever with a HID device's read on it is a device that goes
 * quiet with nothing anywhere saying why. Counted apart from the ordinary
 * restarts precisely so that a nonzero reading is a statement about usbport
 * rather than a statistic.
 *
 * Called with the lock held. IRQL: DISPATCH_LEVEL.
 */
static VOID xhciEpPollEndpoints(PXHCI_EXTENSION ext, PXHCI_DEVICE dev)
{
    XHCI_EP_BINDING binding;
    ULONG i;
    ULONG dci;

    for (i = 0; i <= XHCI_MAX_DEVICE_ENDPOINTS; i++) {
        dci = (i == 0) ? 1UL : dev->Endpoints[i - 1].Dci;
        if (dci == 0 || !xhciEpResolve(dev, dci, &binding)) {
            continue;
        }
        /*
         * Task 8-A.1's backstop trigger, and it is deliberately ahead of the
         * `continue`s below: a ring whose space came back through a dequeue
         * placement or a drain rather than through a completion has an endpoint
         * in exactly the states those tests skip.
         */
        xhciEpReleaseRetry(ext, dev, &binding);
        if ((binding.Quiesce->Flags & XHCI_EPQ_STOPPED) == 0 ||
            (binding.Quiesce->Flags &
             (XHCI_EPQ_INFLIGHT | XHCI_EPQ_FAILED)) != 0 ||
            binding.Queue->Count == 0) {
            binding.Quiesce->StoppedArmed = 0;
            continue;
        }
        /* The first poll of a run stamps it; the run is broken by any poll that
         * takes the branch above, which is what keeps this a measure of an
         * unbroken wait rather than of the endpoint's whole life. */
        if (!binding.Quiesce->StoppedArmed) {
            binding.Quiesce->StoppedArmed = 1;
            binding.Quiesce->StoppedStamp = ext->PollClockMs;
            continue;
        }
        /* One stuck pause produces one restart rather than one per poll: the
         * restart helper below disarms this, and so does the branch above. */
        if ((ext->PollClockMs - binding.Quiesce->StoppedStamp) >=
            XHCI_EP_RESTART_MS) {
            XHCI_DBG_VALUE_CHANGED("slot: endpoint left stopped with work, "
                                   "restarting it, dci", binding.Dci);
            /*
             * Counted only when the helper rang (Phase 7 review, B6): it can
             * decline - NO_CONTEXT, not admitted - and it disarms the run
             * either way, so counting the call had the counter climbing
             * indefinitely with no doorbell written, corrupting its one
             * documented reading ("climbing against idle traffic is the
             * alarm") with a non-alarm case.
             */
            if (xhciEpRestartIfStopped(ext, dev, &binding)) {
                ext->EndpointRestartsByPoll++;
            }
        }
    }
}

/*
 * Task 7b-A.0: the bound under every refusal this driver can give.
 *
 * Each gate that answers `MP_STATUS_NO_RESOURCES` argues its own case for being
 * transient, and every one of those arguments is about a mechanism that will
 * end the wait - a command completing, a ring draining, a chain finishing. This
 * is the net for the wait that has **no mechanism at all**, which is what batch
 * 7b-V0 measured: 1,803 refusals on Windows 98 with the guest dead at the end of
 * them, because usbport's answer to a refusal is to offer the transfer again and
 * there is nothing else it can do.
 *
 * Three conditions, and all three are needed. `RefusedSincePoll` says usbport is
 * still being turned away. `SubmittedSincePoll` clear says nothing at all got
 * through in the meantime, which is what separates a stuck record from a busy
 * one. `ActiveOp` NONE says no command is outstanding, so there is no recovery
 * ladder to pre-empt and this can never fire ahead of the watchdogs that would
 * have resolved the wait properly.
 *
 * **What is *not* here is the fourth condition task 8-A.1 first tried to add**,
 * and the reason is worth keeping: a draft excluded any record with a transfer
 * outstanding on *any* of its rings, on the argument that outstanding work is a
 * mechanism. It is - but only for a refusal on the ring that holds it. A HID
 * device keeping one interrupt read posted indefinitely while EP0 refuses every
 * offer after an unmatched REMOVE is precisely the batch 7b-V0 hang, and that
 * draft made it immune here for ever. The exclusion belongs at the refusal that
 * has the mechanism, not at the record that happens to be near one - see
 * `xhciDevTransferRefused`'s `ringFull` argument.
 *
 * Called with the lock held. IRQL: DISPATCH_LEVEL.
 */
static VOID xhciDevPollProgress(PXHCI_EXTENSION ext, PXHCI_DEVICE dev)
{
    ULONG stuck;

    stuck = (dev->RefusedSincePoll != 0 && dev->SubmittedSincePoll == 0 &&
             dev->ActiveOp == XHCI_DEV_OP_NONE);
    /* Read once per poll and cleared here whatever the verdict, so the window
     * the detector judges is one poll rather than the whole life of the record. */
    dev->RefusedSincePoll = 0;
    dev->SubmittedSincePoll = 0;

    if (!stuck) {
        dev->StallArmed = 0;
        return;
    }
    if (dev->State == XHCI_DEV_STATE_FAILED ||
        dev->State == XHCI_DEV_STATE_GONE) {
        /* Already answering with an error; the refusal came from a gate above
         * the state test and there is nothing left to give up on. */
        return;
    }

    /*
     * **Task 13-R.3.5: a millisecond budget on the poll clock, where this was
     * ten consecutive polls.** The quantity batch 7b-V0 calibrated was five
     * seconds of a Windows 98 desktop being kept waiting; ten polls delivered
     * that only on a host polling at 500 ms, and the E460 polls at 36-80 ms,
     * where the same ten polls are 0.36-0.8 s. See XHCI_DEV_STALL_MS.
     *
     * The stamp is taken on the first stuck poll of a run rather than at any
     * arming site, because there is no arming site - the run is defined by the
     * observation, and `StallArmed` is what makes it a *consecutive* run: the
     * clear above ends it at the first poll that saw anything move.
     */
    if (!dev->StallArmed) {
        dev->StallArmed = 1;
        dev->StallStamp = ext->PollClockMs;
        return;
    }
    /* One stuck record is failed once rather than once per poll for as long as
     * usbport keeps offering: the record is FAILED after this, which the state
     * test above then answers on every later poll. */
    if ((ext->PollClockMs - dev->StallStamp) >= XHCI_DEV_STALL_MS) {
        dev->StallArmed = 0;
        ext->DevicesStalledOut++;
        XHCI_DBG_VALUE_CHANGED("slot: record refused without progressing, "
                               "failing it, state << 8 | pending op",
                               (dev->State << 8) | dev->PendingOp);
        xhciDevFailRecord(ext, dev, XHCI_DEV_OP_NONE);
    }
}

/* IRQL: <= DISPATCH_LEVEL, controller lock **not** held - this one takes it
 * itself, unlike every helper it drives. */
VOID XhciSlotPoll(PXHCI_EXTENSION ext)
{
    KIRQL oldIrql;
    ULONG i;

    if (ext == NULL || ext->Signature != XHCI_EXTENSION_SIGNATURE) {
        return;
    }

    XhciControllerLockAcquire(&oldIrql);
    for (i = 0; i < XHCI_MAX_SLOTS; i++) {
        PXHCI_DEVICE dev = &ext->Devices[i];

        if (dev->State != XHCI_DEV_STATE_FREE) {
            xhciEpPollEndpoints(ext, dev);
            xhciDevPollProgress(ext, dev);
        }
        if (dev->ActiveOp == XHCI_DEV_OP_NONE) {
            dev->OpAgeArmed = 0;
            continue;
        }
        if (!dev->OpAgeArmed) {
            dev->OpAgeArmed = 1;
            dev->OpAgeStamp = ext->PollClockMs;
            continue;
        }
        /*
         * One lost command produces one recovery rather than one per poll, and
         * the bound is twice the command engine's own - which is what keeps this
         * from firing ahead of the ladder that would have resolved the command
         * properly. See XHCI_DEV_AGE_MS.
         *
         * **Task 13-R.3.5: milliseconds on the poll clock, where this was a
         * count of polls.** The re-stamp is what replaces the poll count's
         * equality test: a clock steps by whatever the period was and can cross
         * the threshold rather than land on it, so "once" has to be arranged
         * rather than fallen into. It also means a crossing that finds the
         * engine owned by somebody else is not thrown away - it fires on the
         * first later poll where the ownership test passes, instead of never.
         */
        if ((ext->PollClockMs - dev->OpAgeStamp) >= XHCI_DEV_AGE_MS &&
            ext->CommandOwner == xhciDevRef(ext, dev)) {
            dev->OpAgeArmed = 0;
            ext->OpAgeRecoveries++;
            XHCI_DBG_VALUE_CHANGED("slot: command outlived every watchdog, "
                                   "op", dev->ActiveOp);
            XhciSlotCommandLost(ext);
        }
    }
    XhciControllerLockRelease(oldIrql);

    XhciSlotDeferredWork(ext);
}
