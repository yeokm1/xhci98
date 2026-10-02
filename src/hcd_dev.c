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
 * A Transfer Event. The one transfer the HCD queues before 26-A.5 is the
 * enumeration's EP0 control transfer on the slot the thread is waiting on
 * (hcd_enum.c): a Short Packet on its Data Stage records the residual and
 * the transfer goes on to its Status Stage; any other code ends it and wakes
 * the thread. Everything else is counted. Returns nonzero when the caller
 * must request a controller reset - never, here. IRQL: DISPATCH_LEVEL.
 */
ULONG XhciSlotTransferEvent(PXHCI_EXTENSION ext, const XHCI_TRB *event)
{
    PHCD_CONTROLLER hc;
    ULONG slotId;
    ULONG dci;
    ULONG code;

    hc = HcdControllerFromExt(ext);
    slotId = XHCI_TRB_GET_SLOT_ID(event->Control);
    dci = XHCI_TRB_GET_EP_ID(event->Control);
    code = XHCI_TRB_GET_COMPLETION(event->Status);
    if (slotId == 0 || slotId != hc->XferWaitSlot || dci != 1) {
        hc->TransferEventsUnclaimed++;
        return 0;
    }
    if (code == XHCI_CC_SHORT_PACKET) {
        hc->XferResidual = event->Status & 0x00FFFFFFUL;
        return 0;
    }
    hc->XferCode = code;
    (VOID)KeSetEvent(&hc->XferDoneEvent, IO_NO_INCREMENT, FALSE);
    return 0;
}

/* IRQL: DISPATCH_LEVEL. */
VOID XhciSlotDrainSettled(PXHCI_EXTENSION ext)
{
    UNREFERENCED_PARAMETER(ext);
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

/* Every slot is gone (HCRST, stop, recovery). IRQL: <= DISPATCH_LEVEL,
 * controller lock held. */
VOID XhciSlotInvalidateAll(PXHCI_EXTENSION ext, ULONG controllerStopped)
{
    UNREFERENCED_PARAMETER(ext);
    UNREFERENCED_PARAMETER(controllerStopped);
}

/* ----------------------------------------------------------------------- */
/* Root ports                                                               */
/* ----------------------------------------------------------------------- */

/* IRQL: PASSIVE_LEVEL, or DISPATCH_LEVEL with InitBelowPassive set. */
ULONG XhciRootHubInit(PXHCI_EXTENSION ext, ULONG afterRestore)
{
    UNREFERENCED_PARAMETER(afterRestore);
    HcdControllerFromExt(ext)->PortChangeMask = 0xFFFFFFFFUL;
    return XHCI_RH_OK;
}

/* One Port Status Change Event, from the event DPC's drain with the
 * controller lock held. The thread reads PORTSC itself (26-A.3); all this
 * keeps is which port to look at. IRQL: DISPATCH_LEVEL. */
VOID XhciRootHubPortEvent(PXHCI_EXTENSION ext, ULONG portId)
{
    PHCD_CONTROLLER hc;

    hc = HcdControllerFromExt(ext);
    if (portId >= 1 && portId <= 32) {
        hc->PortChangeMask |= 1UL << (portId - 1);
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
