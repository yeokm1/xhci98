/*
 * cm_filt.c - the filters of record 21 section 3.2, NT targets only:
 *
 *   [CM_KIND_FILTER]  role 2, the tunnel bridge's upper filter. It sits
 *       above the PDO pci.sys made for each Thunderbolt switch port or
 *       tunnel root port that xhcicmf.inf names and does no device work.
 *       It publishes its registration entry (cm_reenum.c) once its start
 *       has completed below it successfully, and withdraws it, with the
 *       rundown of section 3.2, on stop, surprise removal and removal;
 *       through that entry the NHI's thread asks for
 *       IoInvalidateDeviceRelations on its PDO (section 6.2). It reads
 *       every BusRelations answer on its way back, keeps the snapshot and
 *       the baseline, and on an active bridge with safe eject on attaches
 *       a child filter to each new PDO (section 7.4). A device D0 request
 *       on a bridge the NHI holds for its resume is retained (section 6.3).
 *   [CM_KIND_CHILD]   role 3, safe eject's child filter, created by the
 *       bridge filter above a new device's PDO (directly, or above the bus
 *       filters already there, CmPortsChildAttach), or since task 42-A.5
 *       by AddDevice for a device whose LowerFilters value names this
 *       driver (CmChildAddDevice; record 21 section 7.4, revision 3). Its
 *       one act is Removable in the IRP_MN_QUERY_CAPABILITIES answer, on
 *       the way up - always for the first kind, and for the second only
 *       while its location is a unit on an active member's secondary bus
 *       with safe eject on (CmRegAttMayMark); a removal after a
 *       query-remove is recorded on the PDO's marks entry as a pending
 *       departure (CmRegAttRemoved), and an eject follows only on its
 *       confirmation.
 *
 * Every attachment, bridge or child, is on the registration list's
 * attachment list from its attach to its removal and reads its PDO's
 * location at the first request the PnP manager sends it (CmRegAttLocate);
 * a bridge filter whose location is a unit answers Removable as the
 * second kind of child does (decision M15: one object serving both).
 *
 * Every other IRP goes down unchanged (a device-usage notification with
 * the power flags mimicked, as a filter must). Each filter detaches and
 * deletes itself on removal.
 *
 * IRQL: PASSIVE_LEVEL (PnP IRPs), or the lower device's power context: the
 * filter copies DO_POWER_PAGABLE and DO_POWER_INRUSH from the device it
 * sits on, as a filter must.
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
    CmRegFilterInit(filter);
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
    filter->AddDev = 1;
    CmRegAttLink(filter);
    return STATUS_SUCCESS;
}

/*
 * The child by AddDevice (record 21 section 7.4, revision 3): this driver
 * named in the device's LowerFilters value by the mark service. Attached
 * to the top of what is on the PDO, copying its flags as CmPortsChildAttach
 * does; passive until associated (CmRegAttMayMark). A failed attach is no
 * failed AddDevice: a registered filter whose AddDevice fails fails the
 * device's start (decision M7), so this answers STATUS_SUCCESS with no
 * device object rather than stop the unit.
 */
NTSTATUS CmChildAddDevice(PDRIVER_OBJECT driver, PDEVICE_OBJECT pdo)
{
    PDEVICE_OBJECT self;
    PDEVICE_OBJECT lower;
    PCM_FILTER child;
    NTSTATUS status;

    status = IoCreateDevice(driver, sizeof(CM_FILTER), NULL,
                            FILE_DEVICE_UNKNOWN, 0, FALSE, &self);
    if (!NT_SUCCESS(status)) {
        return STATUS_SUCCESS;
    }
    child = (PCM_FILTER)self->DeviceExtension;
    CmZero(child, sizeof(CM_FILTER));
    child->Common.Kind = CM_KIND_CHILD;
    child->Common.Self = self;
    child->Common.Pdo = pdo;
    child->Outstanding = 1;
    child->AddDev = 1;
    KeInitializeSpinLock(&child->Lock);
    KeInitializeEvent(&child->Drained, NotificationEvent, FALSE);
    CmRegFilterInit(child);
    lower = IoAttachDeviceToDeviceStack(self, pdo);
    if (lower == NULL) {
        IoDeleteDevice(self);
        return STATUS_SUCCESS;
    }
    child->Common.Lower = lower;
    self->DeviceType = lower->DeviceType;
    self->Characteristics = lower->Characteristics;
    self->Flags |= lower->Flags &
                   (DO_BUFFERED_IO | DO_DIRECT_IO | DO_POWER_PAGABLE |
                    DO_POWER_INRUSH);
    self->Flags &= ~DO_DEVICE_INITIALIZING;
    CmRegAttLink(child);
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

/*
 * The child's answer (record 21 section 7.4 step 2): Removable set on the
 * way up, where pci.sys succeeded the request and CmRegAttMayMark says
 * this attachment may mark the device (any child of the bridge filter's;
 * an AddDevice child or a bridge filter only while it is a unit of an
 * active member with safe eject on). SurpriseRemovalOK, EjectSupported and
 * LockSupported are left as the bus driver set them. The location is read
 * on the way up, the address from the answer itself (CmRegAttLocateCaps),
 * so that a capability request before the start - a never-seen device's
 * first, before any driver of its own - finds it; a request this object
 * caused by asking for its own location passes through unlocated.
 * PASSIVE_LEVEL.
 */
static NTSTATUS cmChildCapabilities(PCM_FILTER child, PIRP irp)
{
    PIO_STACK_LOCATION stack;
    PDEVICE_CAPABILITIES caps;
    NTSTATUS status;

    status = CmForwardSync(&child->Common, irp);
    stack = IoGetCurrentIrpStackLocation(irp);
    caps = stack->Parameters.DeviceCapabilities.Capabilities;
    if (NT_SUCCESS(status) && caps != NULL && caps->Version >= 1 &&
        caps->Size >= sizeof(DEVICE_CAPABILITIES)) {
        CmRegAttLocateCaps(child, caps->Address);
        if (CmRegAttMayMark(child)) {
            caps->Removable = 1;
            CmRegAttAnswered(child);
        }
    }
    return CmCompleteIrp(irp, status, irp->IoStatus.Information);
}

/* A bridge's BusRelations, read on the way back (section 6.2) on every
 * bridge from its start, whatever its active mark says. */
static NTSTATUS cmBridgeRelations(PCM_FILTER filter, PIRP irp)
{
    NTSTATUS status;
    ULONG seq;

    seq = CmRegRelationsBegin(filter);
    status = CmForwardSync(&filter->Common, irp);
    if (NT_SUCCESS(status) && filter->Started) {
        CmRegRelations(filter,
                       (const DEVICE_RELATIONS *)irp->IoStatus.Information,
                       seq);
    }
    return CmCompleteIrp(irp, status, irp->IoStatus.Information);
}

NTSTATUS CmFilterPnp(PCM_FILTER filter, PIRP irp)
{
    PIO_STACK_LOCATION stack;
    PDEVICE_OBJECT self;
    NTSTATUS status;
    KIRQL irql;
    BOOLEAN child;

    child = (BOOLEAN)(filter->Common.Kind == CM_KIND_CHILD);
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
        if ((child || filter->AddDev) && !CmRegStartAllowed(filter)) {
            /* Its tunnel is being torn down for an eject (the function
             * read stackless when the eject was taken). */
            CmRegAttEvent(filter, CM_MK_START_FAILED);
            return CmCompleteIrp(irp, STATUS_DEVICE_NOT_READY, 0);
        }
        status = CmForwardSync(&filter->Common, irp);
        CmRegAttLocate(filter);
        if (NT_SUCCESS(status)) {
            filter->Started = 1;
            filter->QueryRemoved = 0;
            filter->SurpriseRemoved = 0;
            if (!child) {
                CmRegPublish(filter);
            }
        } else {
            CmRegAttEvent(filter, CM_MK_START_FAILED);
        }
        return CmCompleteIrp(irp, status, irp->IoStatus.Information);

    case IRP_MN_QUERY_CAPABILITIES:
        if (child || filter->AddDev) {
            return cmChildCapabilities(filter, irp);
        }
        break;

    case IRP_MN_QUERY_DEVICE_RELATIONS:
        if (!child && stack->Parameters.QueryDeviceRelations.Type ==
                          BusRelations) {
            return cmBridgeRelations(filter, irp);
        }
        break;

    case IRP_MN_QUERY_REMOVE_DEVICE:
        filter->QueryRemoved = 1;
        CmRegAttEvent(filter, CM_MK_QUERY_REMOVE);
        break;

    case IRP_MN_CANCEL_REMOVE_DEVICE:
        filter->QueryRemoved = 0;
        CmRegAttEvent(filter, CM_MK_CANCEL_REMOVE);
        break;

    case IRP_MN_STOP_DEVICE:
        filter->Started = 0;
        if (!child) {
            CmRegWithdraw(filter);
        }
        break;

    case IRP_MN_SURPRISE_REMOVAL:
        filter->Started = 0;
        filter->SurpriseRemoved = 1;
        CmRegAttEvent(filter, CM_MK_SURPRISE);
        if (!child) {
            CmRegWithdraw(filter);
        }
        break;

    case IRP_MN_REMOVE_DEVICE:
        filter->Started = 0;
        if (!child) {
            CmRegWithdraw(filter);
        }
        /* Close acquisition, forward the removal, then drop the attach's
         * reference and wait for every notification in flight to finish
         * with the filter before it is detached and deleted. */
        KeAcquireSpinLock(&filter->Lock, &irql);
        filter->Removing = 1;
        KeReleaseSpinLock(&filter->Lock, irql);
        if (child) {
            /* The child waits for the removal to finish below, detaches,
             * and only then records the departure (CmRegAttRemoved), so it
             * is recorded once this function's stack is gone. */
            status = CmForwardSync(&filter->Common, irp);
            cmFilterRelease(filter);
            (VOID)KeWaitForSingleObject(&filter->Drained, Executive,
                                        KernelMode, FALSE, NULL);
            IoDetachDevice(filter->Common.Lower);
            CmRegAttRemoved(filter,
                            (BOOLEAN)(filter->QueryRemoved &&
                                      !filter->SurpriseRemoved));
            (VOID)CmCompleteIrp(irp, status, irp->IoStatus.Information);
            IoDeleteDevice(filter->Common.Self);
            return status;
        }
        IoSkipCurrentIrpStackLocation(irp);
        status = IoCallDriver(filter->Common.Lower, irp);
        cmFilterRelease(filter);
        (VOID)KeWaitForSingleObject(&filter->Drained, Executive, KernelMode,
                                    FALSE, NULL);
        IoDetachDevice(filter->Common.Lower);
        CmRegAttRemoved(filter, (BOOLEAN)(filter->QueryRemoved &&
                                          !filter->SurpriseRemoved));
        IoDeleteDevice(filter->Common.Self);
        return status;

    default:
        break;
    }
    IoSkipCurrentIrpStackLocation(irp);
    return IoCallDriver(filter->Common.Lower, irp);
}

/*
 * Power: passed down as NT 5.x requires of a filter, except a device D0
 * request on a bridge the NHI holds for its resume (record 21 section
 * 6.3), which cm_reenum.c passes down with a completion routine that
 * retains it.
 */
NTSTATUS CmFilterPower(PCM_FILTER filter, PIRP irp)
{
    PIO_STACK_LOCATION stack;

    stack = IoGetCurrentIrpStackLocation(irp);
    if (filter->Common.Kind == CM_KIND_FILTER &&
        stack->MinorFunction == IRP_MN_SET_POWER &&
        stack->Parameters.Power.Type == DevicePowerState &&
        stack->Parameters.Power.State.DeviceState == PowerDeviceD0 &&
        CmRegHoldD0(filter, irp)) {
        return STATUS_PENDING;
    }
    PoStartNextPowerIrp(irp);
    IoSkipCurrentIrpStackLocation(irp);
    return PoCallDriver(filter->Common.Lower, irp);
}
