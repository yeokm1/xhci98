/*
 * cm_filt.c - the tunnel bridge's upper filter (record 21 section 3.2,
 * role 2), NT targets only. It sits above the PDO pci.sys made for each
 * Thunderbolt switch port or tunnel root port that xhcicmf.inf names and
 * does nothing to what it sees: every IRP goes down unchanged (a
 * device-usage notification with the power flags mimicked, as a filter
 * must), and its own work is to attach, and to detach and delete itself
 * on removal.
 *
 * Task 39-A.5 adds what it exists for: the registration list entry
 * published after its start has completed below it, withdrawn on stop,
 * surprise removal and removal with the rundown of section 3.2, through
 * which the NHI asks it for IoInvalidateDeviceRelations on its PDO.
 * Task 39-A.8 adds the Removable answer to IRP_MN_QUERY_CAPABILITIES.
 *
 * IRQL: PASSIVE_LEVEL (PnP and power IRPs), or the lower device's power
 * context: the filter copies DO_POWER_PAGABLE and DO_POWER_INRUSH from
 * the bridge it sits on, as a filter must.
 */

#include "cm.h"

NTSTATUS CmFilterAddDevice(PDRIVER_OBJECT driver, PDEVICE_OBJECT pdo)
{
    PDEVICE_OBJECT self;
    PDEVICE_OBJECT lower;
    PCM_FILTER filter;
    NTSTATUS status;

    status = IoCreateDevice(driver, sizeof(CM_FILTER), NULL,
                            FILE_DEVICE_UNKNOWN, 0, FALSE, &self);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    filter = (PCM_FILTER)self->DeviceExtension;
    CmZero(filter, sizeof(CM_FILTER));
    filter->Common.Kind = CM_KIND_FILTER;
    filter->Common.Self = self;
    filter->Common.Pdo = pdo;
    filter->Outstanding = 1;
    KeInitializeSpinLock(&filter->Lock);
    KeInitializeEvent(&filter->Drained, NotificationEvent, FALSE);
    lower = IoAttachDeviceToDeviceStack(self, pdo);
    if (lower == NULL) {
        IoDeleteDevice(self);
        return STATUS_NO_SUCH_DEVICE;
    }
    filter->Common.Lower = lower;
    self->DeviceType = lower->DeviceType;
    self->Characteristics = lower->Characteristics;
    self->Flags |= lower->Flags &
                   (DO_BUFFERED_IO | DO_DIRECT_IO | DO_POWER_PAGABLE |
                    DO_POWER_INRUSH);
    self->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

/*
 * A device-usage notification (paging, hibernation or dump file on a
 * device below this bridge) changes the power flags of the stack. A filter
 * mimics the device below it: DO_POWER_PAGABLE set on the way down where
 * nothing above is non-pageable, cleared on the way up where the device
 * below cleared it (the WDM contract for IRP_MN_DEVICE_USAGE_NOTIFICATION).
 * A notification can also come from a child stack propagating its own
 * usage to this one, outside the PnP manager's serialisation, so each one
 * holds a reference on the filter from before its dispatch touches the
 * filter to the completion's last access, and removal drains them - a
 * remove lock of this driver's own (the IO_REMOVE_LOCK family is absent on
 * Windows 98, the allowlist's deny set, though only the NT targets ever
 * create a filter), with IoAcquireRemoveLock's and
 * IoReleaseRemoveLockAndWait's order: no reference is taken once removal
 * has begun, and removal forwards the IRP before it waits.
 * IRQL: <= DISPATCH_LEVEL.
 */
static NTSTATUS cmFilterAcquire(PCM_FILTER filter)
{
    KIRQL irql;
    NTSTATUS status;

    status = STATUS_SUCCESS;
    KeAcquireSpinLock(&filter->Lock, &irql);
    if (filter->Removing) {
        status = STATUS_DELETE_PENDING;
    } else {
        /* Interlocked: the release decrements without the lock. */
        (VOID)InterlockedIncrement(&filter->Outstanding);
    }
    KeReleaseSpinLock(&filter->Lock, irql);
    return status;
}

/* IRQL: <= DISPATCH_LEVEL (the completion below calls it). */
static VOID cmFilterRelease(PCM_FILTER filter)
{
    if (InterlockedDecrement(&filter->Outstanding) == 0) {
        KeSetEvent(&filter->Drained, IO_NO_INCREMENT, FALSE);
    }
}

/* IRQL: <= DISPATCH_LEVEL (a completion routine). */
static NTSTATUS cmFilterUsageDone(PDEVICE_OBJECT device, PIRP irp,
                                  PVOID context)
{
    PCM_FILTER filter;

    UNREFERENCED_PARAMETER(context);
    filter = (PCM_FILTER)device->DeviceExtension;
    if (irp->PendingReturned) {
        IoMarkIrpPending(irp);
    }
    if ((filter->Common.Lower->Flags & DO_POWER_PAGABLE) == 0) {
        device->Flags &= ~DO_POWER_PAGABLE;
    }
    /* The last access: removal may free the filter after this. */
    cmFilterRelease(filter);
    return STATUS_SUCCESS;
}

NTSTATUS CmFilterPnp(PCM_FILTER filter, PIRP irp)
{
    PIO_STACK_LOCATION stack;
    PDEVICE_OBJECT self;
    NTSTATUS status;
    KIRQL irql;

    stack = IoGetCurrentIrpStackLocation(irp);
    switch (stack->MinorFunction) {
    case IRP_MN_DEVICE_USAGE_NOTIFICATION:
        status = cmFilterAcquire(filter);
        if (!NT_SUCCESS(status)) {
            return CmCompleteIrp(irp, status, 0);
        }
        self = filter->Common.Self;
        if (self->AttachedDevice == NULL ||
            (self->AttachedDevice->Flags & DO_POWER_PAGABLE) != 0) {
            self->Flags |= DO_POWER_PAGABLE;
        }
        IoCopyCurrentIrpStackLocationToNext(irp);
        CM_SET_COMPLETION_ALWAYS(irp, cmFilterUsageDone, NULL);
        return IoCallDriver(filter->Common.Lower, irp);

    case IRP_MN_START_DEVICE:
        status = CmForwardSync(&filter->Common, irp);
        if (NT_SUCCESS(status)) {
            filter->Started = 1;
        }
        return CmCompleteIrp(irp, status, irp->IoStatus.Information);

    case IRP_MN_STOP_DEVICE:
    case IRP_MN_SURPRISE_REMOVAL:
        filter->Started = 0;
        break;

    case IRP_MN_REMOVE_DEVICE:
        filter->Started = 0;
        /* Close acquisition, forward the removal, then drop the attach's
         * reference and wait for every notification in flight to finish
         * with the filter before it is detached and deleted. */
        KeAcquireSpinLock(&filter->Lock, &irql);
        filter->Removing = 1;
        KeReleaseSpinLock(&filter->Lock, irql);
        IoSkipCurrentIrpStackLocation(irp);
        status = IoCallDriver(filter->Common.Lower, irp);
        cmFilterRelease(filter);
        (VOID)KeWaitForSingleObject(&filter->Drained, Executive, KernelMode,
                                    FALSE, NULL);
        IoDetachDevice(filter->Common.Lower);
        IoDeleteDevice(filter->Common.Self);
        return status;

    default:
        break;
    }
    IoSkipCurrentIrpStackLocation(irp);
    return IoCallDriver(filter->Common.Lower, irp);
}

NTSTATUS CmFilterPower(PCM_FILTER filter, PIRP irp)
{
    PoStartNextPowerIrp(irp);
    IoSkipCurrentIrpStackLocation(irp);
    return PoCallDriver(filter->Common.Lower, irp);
}
