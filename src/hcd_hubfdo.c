/*
 * hcd_hubfdo.c - an external hub's FDO: xhci98.sys's third PnP role
 * (roadmap-hcd.md task 33.4; design record 13 section 10.11).
 *
 * The bus serves every hub itself (hcd_hub.c, hcd_sshub.c); what changes in
 * 33.4 is only the presentation. A hub's PDO (hcd_pdo.c, an HCD_DEVICE_PDO
 * with Hub set, under the project-owned ids XHCI98\HUB or XHCI98\HUB30) is
 * bound by this driver's own INF, and AddDevice - decided as for the root
 * hub by the PDO's driver object - attaches this FDO over it. Its
 * BusRelations are the PDOs presented under that hub (ParentSerial), its
 * door the hub IOCTLs of the Power tab for the hub's own ports
 * (hcd_door.c). Its remove tears down only itself: the bus, the hub and
 * the devices behind it keep running, and the PDOs stay listed (the WDM
 * rule), to be reported again when PnP adds the FDO again.
 *
 * The controller is reached only through the hub PDO's Controller, inside
 * that PDO's Busy count - the guard a device PDO's own dispatch takes, and
 * which the parent's release (HcdDevicePdoReleaseAll) waits out before it
 * orphans the PDO. Busy is never held across a call down the stack: the
 * PDO's own STOP and REMOVE wait for it (hcd_pdo.c, hcdPdoQuiesce).
 *
 * IRQL: PASSIVE_LEVEL throughout (PnP, power on DO_POWER_PAGABLE objects,
 * and IRP_MJ_DEVICE_CONTROL from user mode), except where a function says
 * otherwise.
 */

#include "hcd.h"
#include "xhci_dbg.h"

/* The hub PDO under this FDO. */
#define hcdHubPdoOf(fdo) ((PHCD_DEVICE_PDO)(fdo)->Pdo->DeviceExtension)

static ULONG hcdHubIoEnter(PHCD_HUB_FDO fdo)
{
    (VOID)InterlockedIncrement(&fdo->OutstandingIo);
    if (fdo->Common.PnpState == HCD_PNP_REMOVED) {
        if (InterlockedDecrement(&fdo->OutstandingIo) == 0) {
            (VOID)KeSetEvent(&fdo->RemoveEvent, IO_NO_INCREMENT, FALSE);
        }
        return 0;
    }
    return 1;
}

static VOID hcdHubIoLeave(PHCD_HUB_FDO fdo)
{
    if (InterlockedDecrement(&fdo->OutstandingIo) == 0) {
        (VOID)KeSetEvent(&fdo->RemoveEvent, IO_NO_INCREMENT, FALSE);
    }
}

/* The controller, through the hub PDO, with that PDO's Busy raised before
 * Controller is read, as hcd_urb.c's dispatch does; NULL (and nothing
 * raised) once the parent's release has orphaned the PDO. IRQL: <=
 * DISPATCH_LEVEL. */
static PHCD_CONTROLLER hcdHubControllerEnter(PHCD_HUB_FDO fdo)
{
    PHCD_DEVICE_PDO pdo;
    PHCD_CONTROLLER hc;

    pdo = hcdHubPdoOf(fdo);
    (VOID)InterlockedIncrement(&pdo->Busy);
    hc = pdo->Controller;
    if (hc == NULL) {
        (VOID)InterlockedDecrement(&pdo->Busy);
    }
    return hc;
}

/* IRQL: <= DISPATCH_LEVEL. */
static VOID hcdHubControllerLeave(PHCD_HUB_FDO fdo, PHCD_CONTROLLER hc)
{
    if (hc != NULL) {
        (VOID)InterlockedDecrement(&hcdHubPdoOf(fdo)->Busy);
    }
}

static NTSTATUS hcdHubPassDown(PHCD_HUB_FDO fdo, PIRP irp)
{
    IoSkipCurrentIrpStackLocation(irp);
    return IoCallDriver(fdo->LowerDevice, irp);
}

static NTSTATUS NTAPI hcdHubSignal(PDEVICE_OBJECT DeviceObject, PIRP Irp,
                                   PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);

    (VOID)KeSetEvent((PKEVENT)Context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

/* AddDevice for a hub's PDO. Anything else of this driver's own that is not
 * the root hub's is refused. */
NTSTATUS HcdHubAddDevice(PDRIVER_OBJECT driver, PDEVICE_OBJECT pdo)
{
    PHCD_DEVICE_PDO pdoExt;
    PDEVICE_OBJECT fdo;
    PHCD_HUB_FDO ext;
    NTSTATUS status;

    pdoExt = (PHCD_DEVICE_PDO)pdo->DeviceExtension;
    if (pdoExt->Common.Kind != HCD_KIND_DEVICE_PDO || !pdoExt->Hub) {
        return STATUS_NOT_SUPPORTED;
    }
    status = IoCreateDevice(driver, sizeof(HCD_HUB_FDO), NULL,
                            FILE_DEVICE_BUS_EXTENDER, 0, FALSE, &fdo);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    ext = (PHCD_HUB_FDO)fdo->DeviceExtension;
    ext->Common.Kind = HCD_KIND_HUB_FDO;
    ext->Common.Self = fdo;
    ext->Common.PnpState = HCD_PNP_ADDED;
    ext->Common.DevicePower = PowerDeviceD0;
    ext->Common.SystemPower = PowerSystemWorking;
    ext->Pdo = pdo;
    ext->OutstandingIo = 1;
    KeInitializeEvent(&ext->RemoveEvent, NotificationEvent, FALSE);

    ext->LowerDevice = IoAttachDeviceToDeviceStack(fdo, pdo);
    if (ext->LowerDevice == NULL) {
        IoDeleteDevice(fdo);
        return STATUS_NO_SUCH_DEVICE;
    }
    fdo->Flags |= DO_POWER_PAGABLE;
    fdo->Flags &= ~DO_DEVICE_INITIALIZING;
    XHCI_DBG_VALUE("hcd: hub FDO attached, hub PDO serial", pdoExt->Serial);
    return STATUS_SUCCESS;
}

/* The door's names, at a start that succeeded below. */
static VOID hcdHubStarted(PHCD_HUB_FDO fdo)
{
    PHCD_CONTROLLER hc;

    hc = hcdHubControllerEnter(fdo);
    HcdDoorHubStart(fdo, hc);
    hcdHubControllerLeave(fdo, hc);
}

NTSTATUS HcdHubFdoPnp(PHCD_HUB_FDO fdo, PIRP irp)
{
    PIO_STACK_LOCATION stack;
    PHCD_CONTROLLER hc;
    PDEVICE_RELATIONS old;
    PDEVICE_RELATIONS rel;
    KEVENT done;
    NTSTATUS status;

    stack = IoGetCurrentIrpStackLocation(irp);
    XHCI_DBG_VALUE("hcd: hub FDO PnP minor", stack->MinorFunction);

    if (stack->MinorFunction == IRP_MN_REMOVE_DEVICE) {
        fdo->Common.PnpState = HCD_PNP_REMOVED;
        hcdHubIoLeave(fdo);
        (VOID)KeWaitForSingleObject(&fdo->RemoveEvent, Executive, KernelMode,
                                    FALSE, NULL);
        /* Only the door goes; the bus keeps the hub and the devices behind
         * it, whose PDOs PnP has removed already and which stay listed. */
        hc = hcdHubControllerEnter(fdo);
        HcdDoorHubRemove(fdo, hc);
        hcdHubControllerLeave(fdo, hc);
        irp->IoStatus.Status = STATUS_SUCCESS;
        status = hcdHubPassDown(fdo, irp);
        IoDetachDevice(fdo->LowerDevice);
        IoDeleteDevice(fdo->Common.Self);
        return status;
    }

    if (!hcdHubIoEnter(fdo)) {
        return HcdCompleteIrp(irp, STATUS_DELETE_PENDING, 0);
    }

    if (stack->MinorFunction == IRP_MN_START_DEVICE) {
        KeInitializeEvent(&done, NotificationEvent, FALSE);
        IoCopyCurrentIrpStackLocationToNext(irp);
        HCD_SET_COMPLETION_ALWAYS(irp, hcdHubSignal, &done);
        status = IoCallDriver(fdo->LowerDevice, irp);
        if (status == STATUS_PENDING) {
            (VOID)KeWaitForSingleObject(&done, Executive, KernelMode, FALSE,
                                        NULL);
        }
        status = irp->IoStatus.Status;
        if (NT_SUCCESS(status)) {
            fdo->Common.PnpState = HCD_PNP_STARTED;
            hcdHubStarted(fdo);
        }
        status = HcdCompleteIrp(irp, status, 0);
        hcdHubIoLeave(fdo);
        return status;
    }

    switch (stack->MinorFunction) {
    case IRP_MN_STOP_DEVICE:
        fdo->Common.PnpState = HCD_PNP_STOPPED;
        HcdDoorHubStop(fdo);
        irp->IoStatus.Status = STATUS_SUCCESS;
        break;
    case IRP_MN_SURPRISE_REMOVAL:
        fdo->Common.PnpState = HCD_PNP_SURPRISE_REMOVED;
        HcdDoorHubStop(fdo);
        irp->IoStatus.Status = STATUS_SUCCESS;
        break;
    case IRP_MN_QUERY_STOP_DEVICE:
    case IRP_MN_QUERY_REMOVE_DEVICE:
    case IRP_MN_CANCEL_STOP_DEVICE:
    case IRP_MN_CANCEL_REMOVE_DEVICE:
        irp->IoStatus.Status = STATUS_SUCCESS;
        break;
    case IRP_MN_QUERY_DEVICE_RELATIONS:
        if (stack->Parameters.QueryDeviceRelations.Type == BusRelations) {
            old = (PDEVICE_RELATIONS)irp->IoStatus.Information;
            hc = hcdHubControllerEnter(fdo);
            rel = (hc != NULL)
                      ? HcdDevicePdoRelations(hc, old,
                                              hcdHubPdoOf(fdo)->Serial)
                      : NULL;
            hcdHubControllerLeave(fdo, hc);
            if (rel != NULL) {
                if (old != NULL) {
                    HcdPoolFreeForeign(old);
                }
                irp->IoStatus.Information = (ULONG_PTR)rel;
                irp->IoStatus.Status = STATUS_SUCCESS;
            }
        }
        break;
    default:
        break;
    }
    status = hcdHubPassDown(fdo, irp);
    hcdHubIoLeave(fdo);
    return status;
}

NTSTATUS HcdHubFdoPower(PHCD_HUB_FDO fdo, PIRP irp)
{
    NTSTATUS status;

    if (!hcdHubIoEnter(fdo)) {
        PoStartNextPowerIrp(irp);
        return HcdCompleteIrp(irp, STATUS_DELETE_PENDING, 0);
    }
    PoStartNextPowerIrp(irp);
    IoSkipCurrentIrpStackLocation(irp);
    status = PoCallDriver(fdo->LowerDevice, irp);
    hcdHubIoLeave(fdo);
    return status;
}

/* IRP_MJ_DEVICE_CONTROL on a hub FDO: the hub IOCTLs of the Power tab for
 * this hub's own ports (hcd_door.c), inside the FDO's I/O count and the
 * PDO's Busy. */
NTSTATUS HcdHubFdoDeviceControl(PHCD_HUB_FDO fdo, PIRP irp)
{
    PHCD_CONTROLLER hc;
    NTSTATUS status;

    if (!hcdHubIoEnter(fdo)) {
        return HcdCompleteIrp(irp, STATUS_DELETE_PENDING, 0);
    }
    hc = hcdHubControllerEnter(fdo);
    status = HcdDoorHubIoctl(hc, hcdHubPdoOf(fdo), irp);
    hcdHubControllerLeave(fdo, hc);
    hcdHubIoLeave(fdo);
    return status;
}
