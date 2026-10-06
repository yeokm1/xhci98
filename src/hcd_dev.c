/*
 * hcd_dev.c - the device layer the kept controller files call into
 * (xhci_hw.h, the XhciSlot* and XhciRootHub* contracts), as the HCD's own
 * (roadmap-hcd.md tasks 26-A.2 to 26-A.4).
 *
 * The miniport's xhci_slot.c and xhci_rh.c answered these calls with usbport
 * in mind and left the tree with it (design record 13 section 5.1, the
 * "Rewritten" row). The event drain and the command engine still call them,
 * so this file answers each with what the HCD does at this task:
 *
 *   - a Port Status Change Event marks the port in a change mask and asks
 *     the event DPC to wake the controller thread, which owns the ports from
 *     26-A.3;
 *   - a command completion is handed to the one waiter the thread may have
 *     (HcdCommandWait), the enumeration machine of 26-A.4 being that waiter;
 *   - a transfer event names no transfer until 26-A.5 and is counted.
 *
 * IRQL: each carries the contract of xhci_hw.h it implements, and none
 * blocks. KeSetEvent is the only call out of the driver.
 */

#include "hcd.h"
#include "xhci_hw.h"
#include "xhci_xfer.h"

/* ----------------------------------------------------------------------- */
/* Slots and commands                                                       */
/* ----------------------------------------------------------------------- */

/* IRQL: <= DISPATCH_LEVEL (init sequence). */
VOID XhciSlotInit(PXHCI_EXTENSION ext)
{
    UNREFERENCED_PARAMETER(ext);
}

/* A slot command's Command Completion Event, from the event DPC. IRQL:
 * DISPATCH_LEVEL. */
VOID XhciSlotCommandEvent(PXHCI_EXTENSION ext, ULONG completionCode,
                          ULONG control)
{
    PHCD_CONTROLLER hc;

    hc = HcdControllerFromExt(ext);
    hc->CmdDoneCode = completionCode;
    hc->CmdDoneControl = control;
    hc->CmdDoneLost = 0;
    /* The engine recorded the matched TRB before calling here
     * (xhciCommandCompleted); the waiter takes only its own (hcd_enum.c). */
    hc->CmdDonePA = ext->LastCommandTrbPA;
    (VOID)KeSetEvent(&hc->CmdDoneEvent, IO_NO_INCREMENT, FALSE);
}

/* The outstanding command will never complete (ring stopped, controller
 * reset, abort): release its waiter with the loss. IRQL: <= DISPATCH_LEVEL. */
VOID XhciSlotCommandLost(PXHCI_EXTENSION ext)
{
    PHCD_CONTROLLER hc;

    hc = HcdControllerFromExt(ext);
    hc->CmdDoneLost = 1;
    (VOID)KeSetEvent(&hc->CmdDoneEvent, IO_NO_INCREMENT, FALSE);
}

/* IRQL: DISPATCH_LEVEL. */
VOID XhciSlotCommandSlotFatal(PXHCI_EXTENSION ext, ULONG completionCode,
                              ULONG control)
{
    UNREFERENCED_PARAMETER(completionCode);
    UNREFERENCED_PARAMETER(control);
    HcdControllerFromExt(ext)->SlotFatalEvents++;
}

/*
 * The location's stable progress (35-T.5), read by the thread: one count
 * for each result that retired a transfer the engine latched as a success -
 * whatever path retired it, a Transfer Event or the drain's settlement of
 * a deferred short packet, and an isochronous transfer once its packets are
 * answered - since every retirement passes through hcdEp0Result or
 * hcdPipeResult once. A rejected, unmatched, trailing or deferred event
 * retires nothing and is not progress. Controller lock held.
 */
static VOID hcdTolCountProgress(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                                const XHCI_XFER_EVENT_RESULT *result)
{
    PXHCI_TRANSFER t;

    if (dev == NULL || dev->Location < 1 || dev->Location > HCD_PORT_COUNT ||
        result->Action != XHCI_XFER_ACTION_COMPLETE) {
        return;
    }
    for (t = result->Completed; t != NULL; t = t->Next) {
        if (t->UsbdStatus == XHCI_USBD_STATUS_SUCCESS) {
            hc->Ports[dev->Location - 1].TolCompletions++;
            return;
        }
    }
}

/*
 * The soft retry's counts (35-T.2, design record 17 sections 4.2 and 4.8),
 * from every result a pipe's event, settle or replay produced, and the
 * thread's wake for a divert: the engine has kept the Transaction Error on
 * the head and set the queue's RetryWanted, which HcdCfgRetryService
 * decides. Controller lock held.
 */
static VOID hcdTolCountRetry(PHCD_CONTROLLER hc,
                             const XHCI_XFER_EVENT_RESULT *result)
{
    PXHCI_TOL_STATS stats;

    stats = &hc->Hc.Tol.Stats;
    if (result->RetryDiverted) {
        stats->RetryDiverts++;
        hc->RetryWork = 1;
        HcdThreadWake(hc);
    }
    if (result->RetryExhausted) {
        stats->RetryExhausted++;
    }
    stats->RetryRecovered += result->RetryRecovered;
}

/*
 * What the engine decided about one EP0 event or settle: a completed record
 * that is the thread's own ends its wait. A halted EP0 or a refused retire
 * is counted - the enumeration step that sees the failed status gives the
 * slot back, and a record left queued times out into the reset. Returns
 * nonzero for an event the engine calls fatal. Controller lock held.
 */
static ULONG hcdEp0Result(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                          const XHCI_XFER_EVENT_RESULT *result)
{
    PXHCI_TRANSFER t;

    if (result->Fatal) {
        return 1;
    }
    hcdTolCountProgress(hc, dev, result);
    if (result->NeedsRecovery || result->RefusedRetire) {
        hc->Ep0Recoveries++;
        /* A URB's control transfer halted EP0 (a STALL): the thread owes
         * Reset Endpoint and Set TR Dequeue before the next one can run.
         * A refused retire is left to the thread's timeout and reset. */
        if (result->NeedsRecovery && !result->RefusedRetire) {
            dev->Ep0Halted = 1;
            HcdThreadWake(hc);
        }
        if (result->RefusedRetire) {
            dev->Ep0Pipe.DrainPending = 1;
            hc->CancelWork = 1;
            HcdThreadWake(hc);
        }
    }
    if (result->Action == XHCI_XFER_ACTION_COMPLETE) {
        for (t = result->Completed; t != NULL; t = t->Next) {
            if (t == &dev->Ep0Xfer) {
                dev->Ep0Done = 1;
                (VOID)KeSetEvent(&hc->XferDoneEvent, IO_NO_INCREMENT, FALSE);
            } else {
                /* A URB's record: completed after the lock's release, by
                 * the deferred work (hcd_io.c). */
                hc->Counters.TransfersCompleted++;
                HcdIoRetired(hc, t);
            }
        }
    }
    return 0;
}

/* The same decision for a pipe the configuration opened (hcd_cfg.c): its
 * retired records go to the deferred work; a halt is the client's to
 * clear with RESET_PIPE, as usbport left it. An isochronous endpoint never
 * halts (xHCI p.177): what it owes is the thread's stop and drain, as a
 * refused retire does. Controller lock held. */
static ULONG hcdPipeResult(PHCD_CONTROLLER hc, PHCD_PIPE pipe,
                           const XHCI_XFER_EVENT_RESULT *result)
{
    PXHCI_TRANSFER t;

    if (result->Fatal) {
        return 1;
    }
    hcdTolCountProgress(hc, pipe->Device, result);
    hcdTolCountRetry(hc, result);
    if (result->NeedsRecovery && !result->RefusedRetire &&
        pipe->TransferType != XHCI_PIPE_XFER_ISOCH) {
        pipe->Halted = 1;
        /* A stream's own failure: the endpoint's recovery moves this
         * stream alone past its failed TD (hcd_cfg.c, 31-A.1). */
        if (pipe->StreamId != 0) {
            pipe->StreamFault = 1;
        }
    }
    if (result->RefusedRetire ||
        (result->NeedsRecovery &&
         pipe->TransferType == XHCI_PIPE_XFER_ISOCH)) {
        /* The ring and the record disagree about where the TD ends: the
         * endpoint is Running and owes a Stop plus a drain, which the
         * thread performs (hcd_cfg.c; round 2, finding 9). */
        pipe->DrainPending = 1;
        hc->CancelWork = 1;
        HcdThreadWake(hc);
    }
    if (result->Action == XHCI_XFER_ACTION_COMPLETE) {
        for (t = result->Completed; t != NULL; t = t->Next) {
            if (t == &pipe->Device->HubXfer) {
                /* A hub's status-change report: the thread's, not a URB's
                 * (hcd_hub.c). */
                HcdHubXferRetired(hc, pipe->Device);
                continue;
            }
            hc->Counters.TransfersCompleted++;
            HcdIoRetired(hc, t);
        }
    }
    return 0;
}

/*
 * A Transfer Event on an isochronous pipe. Ring Underrun and Overrun name
 * no TD (4.10.3.1 p.185) and never reach the engine: the endpoint left the
 * schedule and a doorbell puts it back (4.14.2.1 p.239) - withheld while
 * the thread has the pipe paused or owes it a stop, or the device is being
 * torn down (Gone), since a doorbell then would restart the endpoint under
 * the thread's edit or its teardown. Everything else is
 * the engine's per-packet match; a TRB Error leaves the endpoint in Error,
 * not Halted (p.177), and the thread stops and drains it as it does a
 * refused retire (hcdPipeResult). Returns nonzero for a fatal event. IRQL:
 * DISPATCH_LEVEL, controller lock held.
 */
static ULONG hcdIsoEvent(PHCD_CONTROLLER hc, PHCD_PIPE pipe, ULONG slotId,
                         ULONG dci, const XHCI_TRB *event, ULONG cc)
{
    XHCI_XFER_EVENT_RESULT result;
    PXHCI_TRANSFER_QUEUE q;
    ULONG answered;
    ULONG errors;
    ULONG missed;
    ULONG answer;

    if (cc == XHCI_CC_RING_UNDERRUN || cc == XHCI_CC_RING_OVERRUN) {
        if (cc == XHCI_CC_RING_UNDERRUN) {
            hc->Hc.IsoRingUnderruns++;
        } else {
            hc->Hc.IsoRingOverruns++;
        }
        if (pipe->Queue->Count == 0) {
            hc->Hc.IsoEventsUnattributed++;
        } else if (pipe->Paused || pipe->Closed || pipe->DrainPending ||
                   pipe->CancelPending || pipe->Device->Gone) {
            /* Gone: the device's teardown froze it and may already have
             * stopped this endpoint to complete its requests before the
             * Disable Slot; a late underrun's doorbell would restart it
             * under buffers about to go back (Codex review of d54eef0,
             * finding 1). */
            hc->Hc.IsoDoorbellsSuppressed++;
        } else {
            XhciWriteDoorbell(&hc->Hc, slotId, dci);
        }
        return 0;
    }
    /* The engine counts per queue, and a queue goes with its pipe: the
     * block takes what this one event moved. */
    q = pipe->Queue;
    answered = q->IsoPacketsAnswered;
    errors = q->IsoPacketErrors;
    missed = q->IsoMissedService;
    answer = XhciXferIsoEvent(q, pipe->Ring, slotId, dci, event->Param0,
                              event->Status, event->Control, &result);
    hc->Counters.IsoPacketsAnswered += q->IsoPacketsAnswered - answered;
    hc->Counters.IsoPacketErrors += q->IsoPacketErrors - errors;
    hc->Counters.IsoMissedService += q->IsoMissedService - missed;
    if (answer != XHCI_XFER_OK) {
        hc->Counters.TransferEventsUnclaimed++;
        return 0;
    }
    if (result.NeedsRecovery && !result.RefusedRetire && !result.Fatal) {
        hc->Hc.IsoTrbErrorRecoveries++;
    }
    return hcdPipeResult(hc, pipe, &result);
}

/* The pipe a Transfer Event names, or NULL. Controller lock held. */
static PHCD_PIPE hcdEventPipe(PHCD_CONTROLLER hc, ULONG slotId, ULONG dci)
{
    PHCD_USB_DEVICE dev;

    if (slotId < 1 || slotId > XHCI_MAX_SLOTS || dci < 1 || dci > 31) {
        return NULL;
    }
    dev = hc->SlotDevice[slotId];
    if (dev == NULL) {
        return NULL;
    }
    return (dci == 1) ? &dev->Ep0Pipe : dev->Pipes[dci];
}

/* The stream of an endpoint with streams open whose ring holds `trbPA`: a
 * Transfer Event names its TRB and not its stream (xHCI 6.4.2.1), and every
 * stream ring is one segment (xhci_stream.h, XhciStreamFind). NULL when no
 * stream's ring holds it. Controller lock held. */
static PHCD_PIPE hcdEventStream(PHCD_PIPE pipe, ULONG trbPA)
{
    PHCD_STREAMS s;
    ULONG id;

    s = pipe->Streams;
    id = XhciStreamFind((const XHCI_RING *const *)s->Ring, s->Count + 1UL,
                        trbPA);
    return id == 0 ? NULL : s->Pipe[id];
}

/*
 * Controller tolerance's counting of one Transfer Event (35-T.8, design
 * record 17 section 4.8): every completion code into the histogram, and an
 * error code's first records into the note ring with the slot and endpoint
 * it named - XhciLogErrorBudget's per-code budget, so a storm of one code
 * cannot crowd out the first of another. IRQL: DISPATCH_LEVEL, controller
 * lock held.
 */
static VOID hcdTolCountEvent(PXHCI_EXTENSION ext, ULONG slotId, ULONG dci,
                             ULONG cc)
{
    XhciTolCountCode(&ext->Tol.Stats, cc);
    if (cc == XHCI_CC_SUCCESS || cc == XHCI_CC_SHORT_PACKET ||
        cc == XHCI_CC_STOPPED || cc == XHCI_CC_STOPPED_LENGTH_INVALID ||
        cc == XHCI_CC_STOPPED_SHORT_PACKET ||
        (cc >= XHCI_CC_VENDOR_INFO_MIN && cc <= XHCI_CC_VENDOR_INFO_MAX)) {
        return;
    }
    if (XhciLogErrorBudget(&ext->Log, cc)) {
        XhciLogNoteLocked(ext, "xfer.error",
                          (slotId << 16) | (dci << 8) | cc);
    } else {
        ext->LogErrorsOverBudget++;
    }
}
/* The queue's counts moved by one event, added to the controller-wide sums
 * the dump carries (35-T.8); the queue's own stay where they were. IRQL:
 * DISPATCH_LEVEL, controller lock held. */
static VOID hcdTolSumQueue(PXHCI_EXTENSION ext,
                           const XHCI_TRANSFER_QUEUE *queue, ULONG errors,
                           ULONG badCodes, ULONG unmatched, ULONG foreign)
{
    ext->Tol.Stats.QueueErrors += queue->Errors - errors;
    ext->Tol.Stats.QueueBadCodes += queue->BadCodes - badCodes;
    ext->Tol.Stats.QueueUnmatched += queue->UnmatchedEvents - unmatched;
    ext->Tol.Stats.QueueForeign += queue->ForeignEvents - foreign;
}

/*
 * 35-T.3 and 35-T.4's producer (design record 17 section 4.3): a Transfer
 * Event on a non-isochronous endpoint whose code the engine refused
 * (claimed 0), on a slot that names a device, in any endpoint state; or a
 * Stall, Transaction, Babble or Split Transaction error the queue could not
 * match (unattributed: zero, off the ring, or inside no TD) on an open
 * pipe, which the thread confirms from the endpoint's context. Either marks
 * the device; the isochronous path never comes here. IRQL: DISPATCH_LEVEL,
 * controller lock held.
 */
static VOID hcdTolCycleEvent(PHCD_CONTROLLER hc, ULONG slotId, ULONG dci,
                             ULONG cc, ULONG claimed, ULONG unattributed,
                             ULONG pipeOpen)
{
    PHCD_USB_DEVICE dev;
    ULONG tol;

    dev = (slotId >= 1 && slotId <= XHCI_MAX_SLOTS) ? hc->SlotDevice[slotId]
                                                    : NULL;
    tol = hc->Hc.Tol.Stats.Tolerance;
    if (XhciTolCycleOnRefused(tol, claimed, 0, dev != NULL)) {
        HcdTolCycleMark(hc, dev, XHCI_TOL_CYCLE_REFUSED_CODE, dci);
    } else if (unattributed && dev != NULL &&
               XhciTolHaltCandidate(tol, cc, 0, pipeOpen)) {
        HcdTolCycleMark(hc, dev, XHCI_TOL_CYCLE_HALT_NO_TD, dci);
    }
}
/*
 * A Transfer Event, matched by the transfer engine (xhci_xfer.c) against the
 * queue of the pipe its slot and endpoint name - EP0's, or one the
 * configuration opened: the engine checks the TRB address, the slot and the
 * endpoint, latches the short packet's residual, and says which records
 * retire (26-A.5, design record 13 section 6). An event for no open pipe is
 * counted, and escalated when its code is fatal. Returns nonzero when the
 * caller must request a controller reset. IRQL: DISPATCH_LEVEL, controller
 * lock held.
 */
ULONG XhciSlotTransferEvent(PXHCI_EXTENSION ext, const XHCI_TRB *event)
{
    PHCD_CONTROLLER hc;
    PHCD_PIPE pipe;
    PHCD_PIPE endpoint;
    XHCI_XFER_EVENT_RESULT result;
    XHCI_XFER_CODE code;
    ULONG cc;
    ULONG slotId;
    ULONG dci;
    ULONG errors;
    ULONG badCodes;
    ULONG unmatched;
    ULONG foreign;
    ULONG reset;

    hc = HcdControllerFromExt(ext);
    slotId = XHCI_TRB_GET_SLOT_ID(event->Control);
    dci = XHCI_TRB_GET_EP_ID(event->Control);
    hcdTolCountEvent(ext, slotId, dci, XHCI_TRB_GET_COMPLETION(event->Status));
    endpoint = NULL;
    pipe = hcdEventPipe(hc, slotId, dci);
    if (pipe != NULL &&
        XhciXferEventHighRefused(pipe->Queue, event->Control, event->Param1)) {
        /* A TRB pointer above 4 GB names no ring of this driver; its low
         * dword alone could alias a queued TD and complete or retry it.
         * Refused as foreign before anything reads the pointer (review
         * round 1 of 35-T.2, finding 3), and escalated when fatal, as an
         * event no queue owns is. */
        ext->Tol.Stats.QueueForeign++;
        return XhciXferCodeInfo(XHCI_TRB_GET_COMPLETION(event->Status),
                                &code) == XHCI_XFER_OK &&
               code.Fatal;
    }
    if (pipe != NULL && pipe->Streams != NULL) {
        endpoint = pipe;
        /* An Event Data TRB's event carries that TRB's parameter, not a
         * TRB address; the engine queues none on a stream ring. */
        pipe = XHCI_EVENT_IS_EVENT_DATA(event->Control)
                   ? NULL
                   : hcdEventStream(pipe, event->Param0);
        if (pipe == NULL && !XHCI_EVENT_IS_EVENT_DATA(event->Control) &&
            event->Param0 == 0 && event->Param1 == 0 &&
            XHCI_TRB_GET_COMPLETION(event->Status) == XHCI_CC_STALL) {
            /* A STALL in a Prime Pipe transaction: no TD was running, so
             * the event names no TRB (xHCI 4.12), yet the endpoint is
             * Halted with every stream's requests on it. The endpoint's,
             * not a stream's: the thread resets it and completes what is
             * on it as stalled (hcd_cfg.c, hcdCfgCancelOne). A nonzero
             * pointer no stream ring holds stays unclaimed below. */
            endpoint->Halted = 1;
            endpoint->DrainPending = 1;
            hc->CancelWork = 1;
            HcdThreadWake(hc);
            return 0;
        }
    }
    if (pipe == NULL) {
        /* An event no queue owns is still the controller's to escalate
         * when its code is fatal (xhci_xfer.h, XhciXferEvent: "escalating
         * that belongs to the caller that routed the event"; Codex review
         * of batch (c), round 1, finding 3). */
        hc->Counters.TransferEventsUnclaimed++;
        cc = XHCI_TRB_GET_COMPLETION(event->Status);
        reset = XhciXferCodeInfo(cc, &code) == XHCI_XFER_OK;
        /* 35-T.3/4: a refused code on a slot naming a device, whatever its
         * endpoint; and on a streams endpoint, a halt whose pointer no
         * stream ring holds - the endpoint's pipe is open. An Event Data
         * event carries no pointer to judge. */
        if (!XHCI_EVENT_IS_EVENT_DATA(event->Control)) {
            hcdTolCycleEvent(hc, slotId, dci, cc, reset, endpoint != NULL,
                             endpoint != NULL);
        }
        return reset && code.Fatal;
    }
    cc = XHCI_TRB_GET_COMPLETION(event->Status);
    if (cc == XHCI_CC_STOPPED || cc == XHCI_CC_STOPPED_LENGTH_INVALID ||
        cc == XHCI_CC_STOPPED_SHORT_PACKET) {
        /* A Stop Endpoint's event: it completes nothing, but its byte
         * count is the only measure of what a stopped TD moved, latched
         * for the abort or cancel that follows (round 2, finding 12). */
        (VOID)XhciXferQueueStopped(pipe->Queue, pipe->Ring, event->Param0,
                                   event->Status);
        return 0;
    }
    errors = pipe->Queue->Errors;
    badCodes = pipe->Queue->BadCodes;
    unmatched = pipe->Queue->UnmatchedEvents;
    foreign = pipe->Queue->ForeignEvents;
    if (pipe->TransferType == XHCI_PIPE_XFER_ISOCH) {
        reset = hcdIsoEvent(hc, pipe, slotId, dci, event, cc);
        hcdTolSumQueue(ext, pipe->Queue, errors, badCodes, unmatched, foreign);
        return reset;
    }
    if (XhciXferEvent(pipe->Queue, pipe->Ring, slotId, dci, event->Param0,
                      event->Status, event->Control, &result) !=
        XHCI_XFER_OK) {
        hc->Counters.TransferEventsUnclaimed++;
        return 0;
    }
    hcdTolSumQueue(ext, pipe->Queue, errors, badCodes, unmatched, foreign);
    if (result.NeedsRecovery && !result.RefusedRetire) {
        ext->Tol.Stats.QueueHalts++;
    }
    if (result.Refused || result.Unattributed) {
        hcdTolCycleEvent(hc, slotId, dci, result.Code, !result.Refused,
                         result.Unattributed, 1);
    }
    if (dci == 1) {
        return hcdEp0Result(hc, pipe->Device, &result);
    }
    return hcdPipeResult(hc, pipe, &result);
}

/*
 * The soft retry's replay (35-T.2, design record 17 section 4.2): the
 * deferred outcome of the TD `token` names on `pipe`'s queue, if it is still
 * the head and still holds one, applied through the engine with the
 * interception bypassed - so the TD completes as today's Transaction Error
 * would have completed it, DEV_NOT_RESPONDING, retired or placed past, the
 * pipe left Halted - and accounted as XhciSlotTransferEvent accounts an
 * event. The completed record goes to the done list: the caller runs
 * HcdIoDeferred once the lock is released. Returns 1 when an outcome was
 * applied. IRQL: <= DISPATCH_LEVEL, controller lock held.
 */
ULONG HcdDevRetryReplay(PHCD_CONTROLLER hc, PHCD_PIPE pipe, ULONG token)
{
    PXHCI_EXTENSION ext;
    XHCI_XFER_EVENT_RESULT result;
    ULONG errors;
    ULONG badCodes;
    ULONG unmatched;
    ULONG foreign;

    ext = &hc->Hc;
    errors = pipe->Queue->Errors;
    badCodes = pipe->Queue->BadCodes;
    unmatched = pipe->Queue->UnmatchedEvents;
    foreign = pipe->Queue->ForeignEvents;
    if (!XhciXferRetryReplay(pipe->Queue, pipe->Ring, pipe->Device->SlotId,
                             pipe->Dci, token, &result)) {
        return 0;
    }
    ext->Tol.Stats.RetryReplayed++;
    hcdTolSumQueue(ext, pipe->Queue, errors, badCodes, unmatched, foreign);
    if (result.NeedsRecovery && !result.RefusedRetire) {
        ext->Tol.Stats.QueueHalts++;
    }
    /* A Transaction Error is not a fatal code (XhciXferCodeInfo), so there
     * is no escalation to return. */
    (VOID)hcdPipeResult(hc, pipe, &result);
    return 1;
}

/* One pipe's settle: a short packet's promised tail that never came. The
 * ring refusing the settle's retire leaves the transfer queued on a Running
 * endpoint, which owes Stop plus a drain (xhci_xfer.h, XhciXferDrainSettled;
 * round 3 of the batch (c) review, round-2 finding 9). Controller lock
 * held. */
static VOID hcdSettlePipe(PHCD_CONTROLLER hc, PHCD_PIPE pipe, ULONG dci)
{
    XHCI_XFER_EVENT_RESULT result;
    ULONG guard;

    if (pipe == NULL || !XhciXferDeferralsArmed(pipe->Queue)) {
        return;
    }
    for (guard = 0; guard < 8; guard++) {
        if (XhciXferDrainSettled(pipe->Queue, pipe->Ring, &result) !=
            XHCI_XFER_OK) {
            break;
        }
        if (result.Action != XHCI_XFER_ACTION_COMPLETE) {
            if (result.NeedsRecovery || result.RefusedRetire) {
                hc->Ep0Recoveries++;
                pipe->DrainPending = 1;
                hc->CancelWork = 1;
                HcdThreadWake(hc);
            }
            break;
        }
        if (dci == 1) {
            (VOID)hcdEp0Result(hc, pipe->Device, &result);
        } else {
            (VOID)hcdPipeResult(hc, pipe, &result);
        }
    }
}

/* The drain saw the event ring empty: a short packet's promised tail that
 * never came settles now (XhciXferDrainSettled's gate), on every open pipe
 * and every open stream. IRQL: DISPATCH_LEVEL, controller lock held. */
VOID XhciSlotDrainSettled(PXHCI_EXTENSION ext)
{
    PHCD_CONTROLLER hc;
    PHCD_PIPE pipe;
    ULONG slot;
    ULONG dci;
    ULONG id;

    hc = HcdControllerFromExt(ext);
    for (slot = 1; slot <= XHCI_MAX_SLOTS; slot++) {
        if (hc->SlotDevice[slot] == NULL) {
            continue;
        }
        for (dci = 1; dci < 32; dci++) {
            pipe = hcdEventPipe(hc, slot, dci);
            if (pipe != NULL && pipe->Streams != NULL) {
                for (id = 1; id <= pipe->Streams->Count; id++) {
                    hcdSettlePipe(hc, pipe->Streams->Pipe[id], dci);
                }
                continue;
            }
            hcdSettlePipe(hc, pipe, dci);
        }
    }
}

/* IRQL: <= DISPATCH_LEVEL, controller lock released. */
VOID XhciSlotDeferredWork(PXHCI_EXTENSION ext, ULONG armMode)
{
    UNREFERENCED_PARAMETER(armMode);
    /* The URB records the drain retired (hcd_io.c). */
    HcdIoDeferred(HcdControllerFromExt(ext));
}

/* IRQL: <= DISPATCH_LEVEL, controller lock released. */
VOID XhciSlotDeferredWorkForced(PXHCI_EXTENSION ext, ULONG armMode)
{
    UNREFERENCED_PARAMETER(ext);
    UNREFERENCED_PARAMETER(armMode);
}

/* IRQL: <= DISPATCH_LEVEL, controller lock released. */
VOID XhciSlotResumeSweep(PXHCI_EXTENSION ext)
{
    UNREFERENCED_PARAMETER(ext);
}

/* Every slot is gone (HCRST, stop, recovery): the thread drops the records
 * and settles the ports at its next powered pass (hcd_enum.c,
 * hcdInvalidate). IRQL: <= DISPATCH_LEVEL, controller lock held. */
VOID XhciSlotInvalidateAll(PXHCI_EXTENSION ext, ULONG controllerStopped)
{
    PHCD_CONTROLLER hc;

    UNREFERENCED_PARAMETER(controllerStopped);
    hc = HcdControllerFromExt(ext);
    hc->SlotsInvalidated = 1;
    HcdThreadWake(hc);
}

/* ----------------------------------------------------------------------- */
/* Root ports                                                               */
/* ----------------------------------------------------------------------- */

/* IRQL: PASSIVE_LEVEL, or DISPATCH_LEVEL with InitBelowPassive set. */
ULONG XhciRootHubInit(PXHCI_EXTENSION ext, ULONG afterRestore)
{
    PHCD_CONTROLLER hc;
    ULONG i;

    UNREFERENCED_PARAMETER(afterRestore);
    hc = HcdControllerFromExt(ext);
    for (i = 0; i < HCD_PORT_WORDS; i++) {
        hc->PortChange[i] = 0xFFFFFFFFUL;
    }
    return XHCI_RH_OK;
}

/* One Port Status Change Event, from the event DPC's drain with the
 * controller lock held. The thread reads PORTSC itself (26-A.3); all this
 * keeps is which port to look at. IRQL: DISPATCH_LEVEL. */
VOID XhciRootHubPortEvent(PXHCI_EXTENSION ext, ULONG portId)
{
    PHCD_CONTROLLER hc;

    hc = HcdControllerFromExt(ext);
    if (portId >= 1 && portId <= XHCI_MAX_ROOT_PORTS) {
        hc->PortChange[(portId - 1) / 32UL] |= 1UL << ((portId - 1) % 32UL);
    }
    hc->PortEvents++;
}

/* IRQL: <= DISPATCH_LEVEL, controller lock released. */
VOID XhciRootHubDeferredWork(PXHCI_EXTENSION ext, ULONG armMode)
{
    UNREFERENCED_PARAMETER(ext);
    UNREFERENCED_PARAMETER(armMode);
}

/* The NT 6.x report route of the miniport; the HCD never takes it
 * (RootHubReportThroughDpc is 0). IRQL: DISPATCH_LEVEL. */
ULONG XhciRootHubDeferredReport(PXHCI_EXTENSION ext)
{
    UNREFERENCED_PARAMETER(ext);
    return 0;
}

/* IRQL: DISPATCH_LEVEL, controller lock held. */
VOID XhciRootHubRetireOperations(PXHCI_EXTENSION ext)
{
    UNREFERENCED_PARAMETER(ext);
}
