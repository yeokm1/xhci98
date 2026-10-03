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
    if (result->NeedsRecovery && !result->RefusedRetire &&
        pipe->TransferType != XHCI_PIPE_XFER_ISOCH) {
        pipe->Halted = 1;
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
 * the thread has the pipe paused or owes it a stop, since a doorbell then
 * would restart the endpoint under the thread's edit. Everything else is
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
                   pipe->CancelPending) {
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
    XHCI_XFER_EVENT_RESULT result;
    XHCI_XFER_CODE code;
    ULONG cc;
    ULONG slotId;
    ULONG dci;

    hc = HcdControllerFromExt(ext);
    slotId = XHCI_TRB_GET_SLOT_ID(event->Control);
    dci = XHCI_TRB_GET_EP_ID(event->Control);
    pipe = hcdEventPipe(hc, slotId, dci);
    if (pipe == NULL) {
        /* An event no queue owns is still the controller's to escalate
         * when its code is fatal (xhci_xfer.h, XhciXferEvent: "escalating
         * that belongs to the caller that routed the event"; Codex review
         * of batch (c), round 1, finding 3). */
        hc->Counters.TransferEventsUnclaimed++;
        return XhciXferCodeInfo(XHCI_TRB_GET_COMPLETION(event->Status),
                                &code) == XHCI_XFER_OK &&
               code.Fatal;
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
    if (pipe->TransferType == XHCI_PIPE_XFER_ISOCH) {
        return hcdIsoEvent(hc, pipe, slotId, dci, event, cc);
    }
    if (XhciXferEvent(pipe->Queue, pipe->Ring, slotId, dci, event->Param0,
                      event->Status, event->Control, &result) !=
        XHCI_XFER_OK) {
        hc->Counters.TransferEventsUnclaimed++;
        return 0;
    }
    if (dci == 1) {
        return hcdEp0Result(hc, pipe->Device, &result);
    }
    return hcdPipeResult(hc, pipe, &result);
}

/* The drain saw the event ring empty: a short packet's promised tail that
 * never came settles now (XhciXferDrainSettled's gate), on every open
 * pipe. IRQL: DISPATCH_LEVEL, controller lock held. */
VOID XhciSlotDrainSettled(PXHCI_EXTENSION ext)
{
    PHCD_CONTROLLER hc;
    PHCD_PIPE pipe;
    XHCI_XFER_EVENT_RESULT result;
    ULONG slot;
    ULONG dci;
    ULONG guard;

    hc = HcdControllerFromExt(ext);
    for (slot = 1; slot <= XHCI_MAX_SLOTS; slot++) {
        if (hc->SlotDevice[slot] == NULL) {
            continue;
        }
        for (dci = 1; dci < 32; dci++) {
            pipe = hcdEventPipe(hc, slot, dci);
            if (pipe == NULL || !XhciXferDeferralsArmed(pipe->Queue)) {
                continue;
            }
            for (guard = 0; guard < 8; guard++) {
                if (XhciXferDrainSettled(pipe->Queue, pipe->Ring, &result) !=
                    XHCI_XFER_OK) {
                    break;
                }
                if (result.Action != XHCI_XFER_ACTION_COMPLETE) {
                    if (result.NeedsRecovery || result.RefusedRetire) {
                        /* The ring refused the settle's retire: the
                         * transfer is still queued on a Running endpoint
                         * and owes Stop plus a drain (xhci_xfer.h,
                         * XhciXferDrainSettled; round 3 of the batch (c)
                         * review, round-2 finding 9). */
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
