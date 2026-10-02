/*
 * hcd_pnp.c - PnP for the controller FDO of xhci98.sys (design record 13
 * section 5.2; roadmap-hcd.md task 26-A.2; win98-wdm.md, "PnP Dispatch
 * Requirements").
 *
 * Windows 98 never sends IRP_MN_SURPRISE_REMOVAL: a device that leaves
 * arrives as an IRP_MN_REMOVE_DEVICE with no stop or surprise removal before
 * it, so the remove handler is written to be the first PnP IRP the object
 * sees after a start, and runs the same teardown the surprise removal does.
 *
 * IRQL: every function here runs at PASSIVE_LEVEL except hcdSignalOnComplete,
 * a completion routine (<= DISPATCH_LEVEL). The controller's start and stop
 * are hcd_ctl.c's.
 */

#include "hcd.h"
#include "xhci_dbg.h"

static NTSTATUS NTAPI hcdSignalOnComplete(PDEVICE_OBJECT DeviceObject,
                                          PIRP Irp, PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);

    (VOID)KeSetEvent((PKEVENT)Context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

/* IoForwardIrpSynchronously is XP-era and blocks the load on Windows 2000
 * (the allowlist's [deny]); this is the replacement it names. */
NTSTATUS HcdForwardAndWait(PHCD_CONTROLLER hc, PIRP irp)
{
    KEVENT done;
    NTSTATUS status;

    KeInitializeEvent(&done, NotificationEvent, FALSE);
    IoCopyCurrentIrpStackLocationToNext(irp);
    HCD_SET_COMPLETION_ALWAYS(irp, hcdSignalOnComplete, &done);
    status = IoCallDriver(hc->LowerDevice, irp);
    if (status == STATUS_PENDING) {
        (VOID)KeWaitForSingleObject(&done, Executive, KernelMode, FALSE, NULL);
        status = irp->IoStatus.Status;
    }
    return status;
}

static NTSTATUS hcdStart(PHCD_CONTROLLER hc, PIRP irp)
{
    NTSTATUS status;

    status = HcdForwardAndWait(hc, irp);
    if (!NT_SUCCESS(status)) {
        return HcdCompleteIrp(irp, status, irp->IoStatus.Information);
    }

    status = HcdStartController(hc, irp);
    if (NT_SUCCESS(status)) {
        hc->Common.PnpState = HCD_PNP_STARTED;
        hc->Common.DevicePower = PowerDeviceD0;
    }
    return HcdCompleteIrp(irp, status, 0);
}

/* Windows 98's surprise removal is this IRP with nothing before it, so the
 * controller is stopped here if nothing stopped it yet - the same teardown
 * IRP_MN_SURPRISE_REMOVAL runs on NT. */
static NTSTATUS hcdRemove(PHCD_CONTROLLER hc, PIRP irp)
{
    NTSTATUS status;

    if (hc->ControllerStarted) {
        HcdStopController(hc);
    }
    hc->Common.PnpState = HCD_PNP_REMOVED;

    irp->IoStatus.Status = STATUS_SUCCESS;
    status = HcdPassDown(hc, irp);

    /* Drop the bias AddDevice set, then wait for every IRP still inside. */
    HcdIoLeave(hc);
    (VOID)KeWaitForSingleObject(&hc->RemoveEvent, Executive, KernelMode,
                                FALSE, NULL);

    IoDetachDevice(hc->LowerDevice);
    IoDeleteDevice(hc->Common.Self);
    return status;
}

NTSTATUS HcdControllerPnp(PHCD_CONTROLLER hc, PIRP irp)
{
    PIO_STACK_LOCATION stack;
    NTSTATUS status;

    stack = IoGetCurrentIrpStackLocation(irp);
    XHCI_DBG_VALUE("hcd: controller PnP minor", stack->MinorFunction);

    if (stack->MinorFunction == IRP_MN_REMOVE_DEVICE) {
        return hcdRemove(hc, irp);
    }

    if (!HcdIoEnter(hc)) {
        return HcdCompleteIrp(irp, STATUS_DELETE_PENDING, 0);
    }

    switch (stack->MinorFunction) {
    case IRP_MN_START_DEVICE:
        status = hcdStart(hc, irp);
        break;

    case IRP_MN_QUERY_STOP_DEVICE:
    case IRP_MN_QUERY_REMOVE_DEVICE:
        hc->Common.PnpStateBeforeQuery = hc->Common.PnpState;
        hc->Common.PnpState = (stack->MinorFunction == IRP_MN_QUERY_STOP_DEVICE)
                                  ? HCD_PNP_STOP_PENDING
                                  : HCD_PNP_REMOVE_PENDING;
        irp->IoStatus.Status = STATUS_SUCCESS;
        status = HcdPassDown(hc, irp);
        break;

    case IRP_MN_CANCEL_STOP_DEVICE:
    case IRP_MN_CANCEL_REMOVE_DEVICE:
        if (hc->Common.PnpState == HCD_PNP_STOP_PENDING ||
            hc->Common.PnpState == HCD_PNP_REMOVE_PENDING) {
            hc->Common.PnpState = hc->Common.PnpStateBeforeQuery;
        }
        irp->IoStatus.Status = STATUS_SUCCESS;
        status = HcdPassDown(hc, irp);
        break;

    case IRP_MN_STOP_DEVICE:
        HcdStopController(hc);
        hc->Common.PnpState = HCD_PNP_STOPPED;
        irp->IoStatus.Status = STATUS_SUCCESS;
        status = HcdPassDown(hc, irp);
        break;

    case IRP_MN_SURPRISE_REMOVAL:
        HcdStopController(hc);
        hc->Common.PnpState = HCD_PNP_SURPRISE_REMOVED;
        irp->IoStatus.Status = STATUS_SUCCESS;
        status = HcdPassDown(hc, irp);
        break;

    case IRP_MN_QUERY_CAPABILITIES:
        /* win98-wdm.md, "PnP Dispatch Requirements": the controller is not
         * surprise-removable; the bus's answer is otherwise kept. */
        status = HcdForwardAndWait(hc, irp);
        if (NT_SUCCESS(status)) {
            stack->Parameters.DeviceCapabilities.Capabilities
                ->SurpriseRemovalOK = FALSE;
        }
        status = HcdCompleteIrp(irp, status, irp->IoStatus.Information);
        break;

    default:
        status = HcdPassDown(hc, irp);
        break;
    }

    HcdIoLeave(hc);
    return status;
}
