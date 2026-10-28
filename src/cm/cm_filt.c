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
 *       the baseline, and on every bridge it sits on, active or not,
 *       attaches a child filter to each new PDO (section 7.4; design
 *       record 31 section 4.1, the tunnel id's vehicle). A device D0 request
 *       on a bridge the NHI marked for its resume is passed down at once
 *       and counted; the NHI's release then invalidates the bridge
 *       (section 6.3).
 *   [CM_KIND_CHILD]   role 3, safe eject's child filter, created by the
 *       bridge filter above a new device's PDO (directly, or above the bus
 *       filters already there, CmPortsChildAttach), by a child on a
 *       PCI-to-PCI bridge below it (the chain of design record 31 section
 *       4.3, to CM_TUNNEL_DEPTH_MAX), or since task 42-A.5 by AddDevice
 *       for a device whose LowerFilters value names this driver
 *       (CmChildAddDevice; record 21 section 7.4, revision 3). It answers
 *       Removable in the IRP_MN_QUERY_CAPABILITIES answer, on the way up,
 *       only while its location is a unit on an active member's secondary
 *       bus with safe eject on (CmRegAttMayMark), whatever made it; a
 *       removal after a query-remove is recorded on the PDO's marks entry
 *       as a pending departure (CmRegAttRemoved), and an eject follows
 *       only on its confirmation. Since record 31 it also puts the tunnel
 *       id, PCI\XHCI98_TUNNEL&CC_010802, first among the compatible ids
 *       of an NVMe-class function (cmChildIds), and on a PCI-to-PCI bridge
 *       within the depth bound reads that bridge's BusRelations as the
 *       bridge filter does (cmChainRelations). Since task 44.53, on
 *       Windows 2000 with the Standard PC HAL alone, it gives a function
 *       pci.sys gave no interrupt the IRQ of the tunnel port's own wire,
 *       in the requirements, and keeps it out of pci.sys's start (design
 *       record 21 section 6.2.1; cmChildReqQuery, cmChildReqFilter,
 *       cmChildStartIrq).
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
#include "cm_ports.h"

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
    /* A unit: the device on a tunnel port's secondary bus. */
    child->Depth = 1;
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

/*
 * The tunnel id (design record 31 section 4.2): a BusQueryCompatibleIDs
 * answer, on its way back, read by CmPortsTunnelIds. For an NVMe-class
 * function without the id the answer is replaced by a new list, the id
 * first, from paged pool as IRP_MN_QUERY_ID's contract requires, the old
 * one freed; the PnP manager frees the new one. Every other answer, a
 * failed request, a list over the bound and a failed allocation go up as
 * they came (the device then has its plain ids, for the mark service's
 * fallback, section 6.2), counted. What the strings say of a PCI-to-PCI
 * bridge is kept for the chain (ChainKind). PASSIVE_LEVEL.
 */
/*
 * Whether this system takes the tunnel id: Windows 2000 to 8 alone (NT
 * 6.2 and below). 8.1 and later choose a driver again when a device
 * restarts with ids that changed since its enumeration (read on 8.1,
 * design record 31 section 4.8), and there Stor98's INF keeps 3.2.0.0's
 * class model, so the id would only undo the safe-removal mark service's
 * install. The version is the kernel's shared data page's
 * (NtMajorVersion, NtMinorVersion: 6.3 on 8.1, 10.0 on 10), since
 * IoIsWdmVersionAvailable does not tell 8 from 8.1 and no import is
 * added. PASSIVE_LEVEL.
 */
static BOOLEAN cmChildTagOs(VOID)
{
    ULONG major = SharedUserData->NtMajorVersion;
    ULONG minor = SharedUserData->NtMinorVersion;

    return (BOOLEAN)(major < 6 || (major == 6 && minor < 3));
}

static NTSTATUS cmChildIds(PCM_FILTER child, PIRP irp)
{
    NTSTATUS status;
    const USHORT *list;
    USHORT *fresh;
    ULONG chars;
    ULONG kind;
    ULONG verdict;

    status = CmForwardSync(&child->Common, irp);
    if (!NT_SUCCESS(status)) {
        return CmCompleteIrp(irp, status, irp->IoStatus.Information);
    }
    list = (const USHORT *)irp->IoStatus.Information;
    verdict = CmPortsTunnelIds(list, NULL, 0, &chars, &kind);
    if (list != NULL && verdict != CM_TID_OVER &&
        child->ChainKind == CM_CHAIN_UNKNOWN) {
        child->ChainKind = (kind & CM_TID_KIND_BRIDGE) != 0 ? CM_CHAIN_BRIDGE
                                                            : CM_CHAIN_OTHER;
        if (child->ChainKind == CM_CHAIN_BRIDGE) {
            CmRegChainNote(child, CM_CHAIN_EV_BRIDGE);
        }
    }
    if (verdict == CM_TID_OVER) {
        CmRegChainNote(child, CM_CHAIN_EV_OVER);
    } else if (verdict == CM_TID_TAG && cmChildTagOs()) {
        fresh = (USHORT *)CmAllocPaged(chars * (ULONG)sizeof(USHORT));
        if (fresh == NULL) {
            CmRegChainNote(child, CM_CHAIN_EV_NOMEM);
        } else {
            (VOID)CmPortsTunnelIds(list, fresh, chars, &chars, &kind);
            CmFree((PVOID)list);
            irp->IoStatus.Information = (ULONG_PTR)fresh;
            CmRegChainNote(child, CM_CHAIN_EV_ID);
        }
    }
    return CmCompleteIrp(irp, status, irp->IoStatus.Information);
}

/*
 * Whether a child sits on a PCI-to-PCI bridge: what its own
 * BusQueryCompatibleIDs answer said (cmChildIds), or, for a child that
 * never saw one - made by AddDevice, or re-attached after the ids were
 * read - the devnode's recorded compatible ids, read once at its first
 * BusRelations, when the devnode exists. A list the buffer cannot hold is
 * no bridge's. PASSIVE_LEVEL.
 */
static ULONG cmChainKindOf(PCM_FILTER child)
{
    USHORT buf[258];
    ULONG bytes;
    ULONG chars;
    ULONG kind;
    NTSTATUS status;

    if (child->ChainKind != CM_CHAIN_UNKNOWN) {
        return child->ChainKind;
    }
    bytes = 0;
    status = IoGetDeviceProperty(child->Common.Pdo,
                                 DevicePropertyCompatibleIDs,
                                 256 * (ULONG)sizeof(USHORT), buf, &bytes);
    child->ChainKind = CM_CHAIN_OTHER;
    if (NT_SUCCESS(status) && bytes <= 256 * (ULONG)sizeof(USHORT)) {
        buf[bytes / sizeof(USHORT)] = 0;
        buf[bytes / sizeof(USHORT) + 1] = 0;
        if (CmPortsTunnelIds(buf, NULL, 0, &chars, &kind) != CM_TID_OVER &&
            (kind & CM_TID_KIND_BRIDGE) != 0) {
            child->ChainKind = CM_CHAIN_BRIDGE;
            CmRegChainNote(child, CM_CHAIN_EV_BRIDGE);
        }
    }
    return child->ChainKind;
}

/* A chain child's BusRelations (record 31 section 4.3), read on the way
 * back once its device has started. PASSIVE_LEVEL. */
static NTSTATUS cmChainRelations(PCM_FILTER child, PIRP irp)
{
    NTSTATUS status;

    status = CmForwardSync(&child->Common, irp);
    if (NT_SUCCESS(status) && child->Started) {
        CmRegChainRelations(
            child, (const DEVICE_RELATIONS *)irp->IoStatus.Information);
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

/*
 * A child's interrupt (design record 21 section 6.2.1, task 44.53):
 * Windows 2000 on the Standard PC HAL gives a function hot-added behind a
 * tunnel no interrupt (Interrupt Line 0, no routing), and SCSIport fails
 * its start. A child filter on such a system (CmIrqGate) adds, to a query
 * answer of pci.sys's without an interrupt, the IRQ the tunnel port's own
 * wire carries (CmRegIrqWire), adds it again to a filter request's list
 * that lacks it, and takes it out of the start it passes to pci.sys, which
 * never asked for it (the contract of IRP_MN_QUERY_RESOURCE_REQUIREMENTS
 * for a bus filter's added resource). The records and their sequences are
 * cm_irq.c's (CmIrqQueryStep, CmIrqFilterStep, CmIrqStartStep); the lists
 * are read as bytes at the offsets cm_irq.h names, asserted here against
 * the DDK's structures. PASSIVE_LEVEL.
 */
XHCI_C_ASSERT(cm_irq_req_alts,
    XHCI_FIELD_OFFSET(IO_RESOURCE_REQUIREMENTS_LIST, AlternativeLists) ==
        CM_IRQ_REQ_ALTS_AT);
XHCI_C_ASSERT(cm_irq_req_list,
    XHCI_FIELD_OFFSET(IO_RESOURCE_REQUIREMENTS_LIST, List) == CM_IRQ_REQ_HDR);
XHCI_C_ASSERT(cm_irq_iol_count,
    XHCI_FIELD_OFFSET(IO_RESOURCE_LIST, Count) == CM_IRQ_IOL_COUNT_AT);
XHCI_C_ASSERT(cm_irq_iol_desc,
    XHCI_FIELD_OFFSET(IO_RESOURCE_LIST, Descriptors) == CM_IRQ_IOL_HDR);
XHCI_C_ASSERT(cm_irq_iod_size,
    sizeof(IO_RESOURCE_DESCRIPTOR) == CM_IRQ_IOD_SIZE);
XHCI_C_ASSERT(cm_irq_iod_type,
    XHCI_FIELD_OFFSET(IO_RESOURCE_DESCRIPTOR, Type) == CM_IRQ_IOD_TYPE_AT);
XHCI_C_ASSERT(cm_irq_iod_share,
    XHCI_FIELD_OFFSET(IO_RESOURCE_DESCRIPTOR, ShareDisposition) ==
        CM_IRQ_IOD_SHARE_AT);
XHCI_C_ASSERT(cm_irq_iod_flags,
    XHCI_FIELD_OFFSET(IO_RESOURCE_DESCRIPTOR, Flags) == CM_IRQ_IOD_FLAGS_AT);
/* A nested member's offset is no constant expression for the 2000 DDK's
 * compiler, so each level is asserted on its own: the union at 8, and
 * u.Interrupt's MinimumVector and MaximumVector its first two ULONGs (the
 * DDK's declaration order). */
XHCI_C_ASSERT(cm_irq_iod_min,
    XHCI_FIELD_OFFSET(IO_RESOURCE_DESCRIPTOR, u) == CM_IRQ_IOD_MIN_AT);
XHCI_C_ASSERT(cm_irq_iod_max,
    CM_IRQ_IOD_MAX_AT == CM_IRQ_IOD_MIN_AT + sizeof(ULONG));
XHCI_C_ASSERT(cm_irq_cml_list,
    XHCI_FIELD_OFFSET(CM_RESOURCE_LIST, List) == CM_IRQ_CML_HDR);
XHCI_C_ASSERT(cm_irq_cmf_count,
    XHCI_FIELD_OFFSET(CM_FULL_RESOURCE_DESCRIPTOR, PartialResourceList) +
        XHCI_FIELD_OFFSET(CM_PARTIAL_RESOURCE_LIST, Count) ==
        CM_IRQ_CMF_COUNT_AT);
XHCI_C_ASSERT(cm_irq_cmf_desc,
    XHCI_FIELD_OFFSET(CM_FULL_RESOURCE_DESCRIPTOR, PartialResourceList) +
        XHCI_FIELD_OFFSET(CM_PARTIAL_RESOURCE_LIST, PartialDescriptors) ==
        CM_IRQ_CMF_HDR);
XHCI_C_ASSERT(cm_irq_cmd_size,
    sizeof(CM_PARTIAL_RESOURCE_DESCRIPTOR) == CM_IRQ_CMD_SIZE);
/* The union at 4: u.Interrupt.Level and u.DeviceSpecificData.DataSize
 * are each the first ULONG of their member (the DDK's declaration). */
XHCI_C_ASSERT(cm_irq_cmd_level,
    XHCI_FIELD_OFFSET(CM_PARTIAL_RESOURCE_DESCRIPTOR, u) ==
        CM_IRQ_CMD_LEVEL_AT);
XHCI_C_ASSERT(cm_irq_cmd_datasize,
    CM_IRQ_CMD_DATASIZE_AT == CM_IRQ_CMD_LEVEL_AT);
XHCI_C_ASSERT(cm_irq_type_int, CmResourceTypeInterrupt == CM_IRQ_TYPE_INTERRUPT);
XHCI_C_ASSERT(cm_irq_type_dev,
    CmResourceTypeDeviceSpecific == CM_IRQ_TYPE_DEVSPECIFIC);
XHCI_C_ASSERT(cm_irq_share, CmResourceShareShared == CM_IRQ_SHARE_SHARED);
XHCI_C_ASSERT(cm_irq_level,
    CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE == CM_IRQ_FLAGS_LEVEL);

/* The answer's list replaced by one with the interrupt added: TRUE when it
 * was; the old one freed, the new one from paged pool. */
static BOOLEAN cmIrqReplace(PIRP irp, ULONG newBytes, ULONG line)
{
    const UCHAR *list;
    UCHAR *fresh;

    list = (const UCHAR *)irp->IoStatus.Information;
    fresh = (UCHAR *)CmAllocPaged(newBytes);
    if (fresh == NULL) {
        return FALSE;
    }
    if (CmIrqReqAdd(list, fresh, newBytes, line) != newBytes) {
        CmFree(fresh);
        return FALSE;
    }
    CmFree((PVOID)list);
    irp->IoStatus.Information = (ULONG_PTR)fresh;
    return TRUE;
}

static NTSTATUS cmChildReqQuery(PCM_FILTER child, PIRP irp)
{
    NTSTATUS status;
    const UCHAR *list;
    ULONG ok;
    ULONG scan;
    ULONG newBytes;
    ULONG wire;
    ULONG why;
    ULONG line;
    ULONG address;
    ULONG bytes;

    status = CmForwardSync(&child->Common, irp);
    list = (const UCHAR *)irp->IoStatus.Information;
    ok = (ULONG)(NT_SUCCESS(status) && list != NULL);
    newBytes = 0;
    scan = ok ? CmIrqReqScan(list, &newBytes) : CM_IRQ_REQ_BAD;
    wire = 0;
    why = CM_IRQ_WHY_GIVEN;
    if (ok && scan == CM_IRQ_REQ_NONE) {
        /* The function's device and function: DevicePropertyAddress (its
         * bus is not yet answered on 2000 here; CmRegIrqWire reads it from
         * the bridge above). The PnP manager answers the property with an
         * IRP_MN_QUERY_CAPABILITIES through this stack, which passes this
         * object unlocated (InLocate), as CmRegAttLocate's does. */
        address = 0;
        bytes = 0;
        child->InLocate++;
        if (NT_SUCCESS(IoGetDeviceProperty(child->Common.Pdo,
                                           DevicePropertyAddress,
                                           sizeof(address), &address,
                                           &bytes))) {
            child->InLocate--;
            wire = CmRegIrqWire(child, address, &why, &child->IrqBdf);
        } else {
            child->InLocate--;
            why = CM_IRQ_WHY_UNREAD;
        }
    }
    if (CmIrqQueryStep(&child->Irq, ok, scan, wire, &line) == CM_IRQ_ACT_ADD) {
        if (cmIrqReplace(irp, newBytes, line)) {
            CmIrqQueryAdded(&child->Irq, line);
            CmRegIrqNote(child, "cm.chain.irq", (line << 16) | child->IrqBdf);
        } else {
            CmRegIrqNote(child, "cm.chain.irq.nomem", child->IrqBdf);
        }
    } else if (ok && scan == CM_IRQ_REQ_NONE) {
        CmRegIrqNote(child, "cm.chain.irq.no", (why << 16) | child->IrqBdf);
    }
    return CmCompleteIrp(irp, status, irp->IoStatus.Information);
}

static NTSTATUS cmChildReqFilter(PCM_FILTER child, PIRP irp)
{
    NTSTATUS status;
    const UCHAR *list;
    ULONG ok;
    ULONG scan;
    ULONG newBytes;
    ULONG line;

    status = CmForwardSync(&child->Common, irp);
    /* Read afresh: a driver below may have replaced the list it carried. */
    list = (const UCHAR *)irp->IoStatus.Information;
    ok = (ULONG)((NT_SUCCESS(status) || status == STATUS_NOT_SUPPORTED) &&
                 list != NULL);
    newBytes = 0;
    scan = ok ? CmIrqReqScan(list, &newBytes) : CM_IRQ_REQ_BAD;
    if (CmIrqFilterStep(&child->Irq, ok, scan, &line) == CM_IRQ_ACT_ADD) {
        if (cmIrqReplace(irp, newBytes, line)) {
            status = STATUS_SUCCESS;
            CmRegIrqNote(child, "cm.chain.irq.filter",
                         (line << 16) | child->IrqBdf);
        } else {
            CmRegIrqNote(child, "cm.chain.irq.nomem", child->IrqBdf);
        }
    }
    return CmCompleteIrp(irp, status, irp->IoStatus.Information);
}

static NTSTATUS cmIrqStartDone(PDEVICE_OBJECT device, PIRP irp, PVOID context)
{
    UNREFERENCED_PARAMETER(device);
    UNREFERENCED_PARAMETER(irp);
    KeSetEvent((PKEVENT)context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

/*
 * A start on a child that added an interrupt: CM_IRQ_ACT_PASS (forward it
 * as it is), CM_IRQ_ACT_FAIL (fail it), or CM_IRQ_ACT_STRIP with the start
 * forwarded here, the next stack location given copies of both lists
 * without the added interrupt, waited for, the copies freed; *status is
 * then its status. The lists of this and the upper stack locations, which
 * the PnP manager owns, are not touched.
 */
static ULONG cmChildStartIrq(PCM_FILTER child, PIRP irp, NTSTATUS *status)
{
    PIO_STACK_LOCATION stack;
    PIO_STACK_LOCATION next;
    const UCHAR *raw;
    const UCHAR *tr;
    UCHAR *raw2;
    UCHAR *tr2;
    CM_IRQ_STRIP s;
    ULONG act;
    ULONG bytes;
    KEVENT done;

    *status = STATUS_SUCCESS;
    if (!CmIrqStartNeedsScan(&child->Irq)) {
        return CM_IRQ_ACT_PASS;
    }
    stack = IoGetCurrentIrpStackLocation(irp);
    raw = (const UCHAR *)stack->Parameters.StartDevice.AllocatedResources;
    tr = (const UCHAR *)
             stack->Parameters.StartDevice.AllocatedResourcesTranslated;
    act = CmIrqStartStep(&child->Irq, CmIrqStartScan(raw, tr,
                                                     child->Irq.Added, &s));
    if (act != CM_IRQ_ACT_STRIP) {
        if (act == CM_IRQ_ACT_FAIL) {
            CmRegIrqNote(child, "cm.chain.irq.start.refused",
                         child->IrqBdf);
        }
        return act;
    }
    bytes = s.Bytes - CM_IRQ_CMD_SIZE;
    raw2 = (UCHAR *)CmAlloc(bytes);
    tr2 = (UCHAR *)CmAlloc(bytes);
    if (raw2 == NULL || tr2 == NULL ||
        CmIrqStartStrip(raw, raw2, bytes, &s) != bytes ||
        CmIrqStartStrip(tr, tr2, bytes, &s) != bytes) {
        if (raw2 != NULL) {
            CmFree(raw2);
        }
        if (tr2 != NULL) {
            CmFree(tr2);
        }
        CmRegIrqNote(child, "cm.chain.irq.start.nomem", child->IrqBdf);
        return CM_IRQ_ACT_FAIL;
    }
    KeInitializeEvent(&done, NotificationEvent, FALSE);
    IoCopyCurrentIrpStackLocationToNext(irp);
    next = IoGetNextIrpStackLocation(irp);
    next->Parameters.StartDevice.AllocatedResources = (PCM_RESOURCE_LIST)raw2;
    next->Parameters.StartDevice.AllocatedResourcesTranslated =
        (PCM_RESOURCE_LIST)tr2;
    CM_SET_COMPLETION_ALWAYS(irp, cmIrqStartDone, &done);
    (VOID)IoCallDriver(child->Common.Lower, irp);
    (VOID)KeWaitForSingleObject(&done, Executive, KernelMode, FALSE, NULL);
    *status = irp->IoStatus.Status;
    CmFree(raw2);
    CmFree(tr2);
    CmRegIrqNote(child, "cm.chain.irq.start.stripped",
                 (child->Irq.Added << 16) | child->IrqBdf);
    return CM_IRQ_ACT_STRIP;
}

NTSTATUS CmFilterPnp(PCM_FILTER filter, PIRP irp)
{
    PIO_STACK_LOCATION stack;
    PDEVICE_OBJECT self;
    NTSTATUS status;
    KIRQL irql;
    BOOLEAN child;
    BOOLEAN started;
    ULONG irqAct;

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
        irqAct = CM_IRQ_ACT_PASS;
        if (child && CmIrqGate) {
            /* Design record 21 section 6.2.1: the added interrupt kept
             * from pci.sys (stripped), or the start failed. */
            irqAct = cmChildStartIrq(filter, irp, &status);
        }
        if (irqAct == CM_IRQ_ACT_FAIL) {
            CmRegAttEvent(filter, CM_MK_START_FAILED);
            return CmCompleteIrp(irp, STATUS_INSUFFICIENT_RESOURCES, 0);
        }
        if (irqAct != CM_IRQ_ACT_STRIP) {
            status = CmForwardSync(&filter->Common, irp);
        }
        CmRegAttLocate(filter);
        if (NT_SUCCESS(status)) {
            filter->Started = 1;
            filter->QueryRemoved = 0;
            filter->SurpriseRemoved = 0;
            if (!child) {
                CmRegPublish(filter);
            } else {
                CmRegChainNote(filter, CM_CHAIN_EV_START);
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
        if (stack->Parameters.QueryDeviceRelations.Type != BusRelations) {
            break;
        }
        if (!child) {
            return cmBridgeRelations(filter, irp);
        }
        if (filter->Started && cmChainKindOf(filter) == CM_CHAIN_BRIDGE) {
            if (CmPortsChainReads(filter->Depth, 1)) {
                return cmChainRelations(filter, irp);
            }
            CmRegChainNote(filter, CM_CHAIN_EV_DEEP);
        }
        break;

    case IRP_MN_QUERY_RESOURCE_REQUIREMENTS:
        if (child && CmIrqGate) {
            return cmChildReqQuery(filter, irp);
        }
        break;

    case IRP_MN_FILTER_RESOURCE_REQUIREMENTS:
        if (child && CmIrqGate) {
            return cmChildReqFilter(filter, irp);
        }
        break;

    case IRP_MN_QUERY_ID:
        if (child &&
            stack->Parameters.QueryId.IdType == BusQueryCompatibleIDs) {
            return cmChildIds(filter, irp);
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
        started = (BOOLEAN)(filter->Started != 0);
        filter->Started = 0;
        filter->SurpriseRemoved = 1;
        CmRegAttSurprise(filter, started);
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
            CmRegChainNote(filter, CM_CHAIN_EV_REMOVE);
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
 * Power: passed down as NT 5.x requires of a filter, every IRP and at
 * once. A device D0 on a bridge the NHI marked at its suspend is never
 * retained (record 21 section 6.3, task 43.24h): the power manager does
 * not send the pageable NHI its own resume while a bridge's resume waits,
 * so a retained D0 stopped every resume from hibernation and every Fast
 * Startup boot with bug check 0x9F. It is counted and logged
 * (CmRegPassD0); the NHI's resume releases the mark and invalidates the
 * bridge, and the devices behind it are enumerated again.
 */
NTSTATUS CmFilterPower(PCM_FILTER filter, PIRP irp)
{
    PIO_STACK_LOCATION stack;

    stack = IoGetCurrentIrpStackLocation(irp);
    if (filter->Common.Kind == CM_KIND_FILTER &&
        stack->MinorFunction == IRP_MN_SET_POWER &&
        stack->Parameters.Power.Type == DevicePowerState &&
        stack->Parameters.Power.State.DeviceState == PowerDeviceD0) {
        CmRegPassD0(filter);
    }
    PoStartNextPowerIrp(irp);
    IoSkipCurrentIrpStackLocation(irp);
    return PoCallDriver(filter->Common.Lower, irp);
}
