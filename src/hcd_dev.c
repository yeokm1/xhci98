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
    }
    if (result->Action == XHCI_XFER_ACTION_COMPLETE) {
        for (t = result->Completed; t != NULL; t = t->Next) {
            if (t == &dev->Ep0Xfer) {
                dev->Ep0Done = 1;
                (VOID)KeSetEvent(&hc->XferDoneEvent, IO_NO_INCREMENT, FALSE);
            }
        }
    }
    return 0;
}

/*
 * A Transfer Event, matched by the transfer engine (xhci_xfer.c) against the
 * EP0 queue of the device on its slot: the engine checks the TRB address,
 * the slot and the endpoint, latches the short packet's residual, and says
 * which records retire (26-A.5's first step, design record 13 section 6).
 * An event for no device's EP0 is counted. Returns nonzero when the caller
 * must request a controller reset. IRQL: DISPATCH_LEVEL, controller lock
 * held.
 */
ULONG XhciSlotTransferEvent(PXHCI_EXTENSION ext, const XHCI_TRB *event)
{
    PHCD_CONTROLLER hc;
    PHCD_USB_DEVICE dev;
    XHCI_XFER_EVENT_RESULT result;
    ULONG slotId;
    ULONG dci;

    hc = HcdControllerFromExt(ext);
    slotId = XHCI_TRB_GET_SLOT_ID(event->Control);
    dci = XHCI_TRB_GET_EP_ID(event->Control);
    dev = (slotId >= 1 && slotId <= XHCI_MAX_SLOTS) ? hc->SlotDevice[slotId]
                                                     : NULL;
    if (dev == NULL || dci != 1) {
        hc->TransferEventsUnclaimed++;
        return 0;
    }
    if (XhciXferEvent(&dev->Ep0Queue, &dev->Ep0, slotId, 1, event->Param0,
                      event->Status, event->Control, &result) !=
        XHCI_XFER_OK) {
        hc->TransferEventsUnclaimed++;
        return 0;
    }
    return hcdEp0Result(hc, dev, &result);
}

/* The drain saw the event ring empty: a short packet's promised tail that
 * never came settles now (XhciXferDrainSettled's gate). IRQL:
 * DISPATCH_LEVEL, controller lock held. */
VOID XhciSlotDrainSettled(PXHCI_EXTENSION ext)
{
    PHCD_CONTROLLER hc;
    PHCD_USB_DEVICE dev;
    XHCI_XFER_EVENT_RESULT result;
    ULONG slot;
    ULONG guard;

    hc = HcdControllerFromExt(ext);
    for (slot = 1; slot <= XHCI_MAX_SLOTS; slot++) {
        dev = hc->SlotDevice[slot];
        if (dev == NULL || !XhciXferDeferralsArmed(&dev->Ep0Queue)) {
            continue;
        }
        for (guard = 0; guard < 8; guard++) {
            if (XhciXferDrainSettled(&dev->Ep0Queue, &dev->Ep0, &result) !=
                XHCI_XFER_OK) {
                break;
            }
            if (result.Action != XHCI_XFER_ACTION_COMPLETE) {
                if (result.NeedsRecovery) {
                    hc->Ep0Recoveries++;
                }
                break;
            }
            (VOID)hcdEp0Result(hc, dev, &result);
        }
    }
}

/* IRQL: <= DISPATCH_LEVEL, controller lock released. */
VOID XhciSlotDeferredWork(PXHCI_EXTENSION ext, ULONG armMode)
{
    UNREFERENCED_PARAMETER(ext);
    UNREFERENCED_PARAMETER(armMode);
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
