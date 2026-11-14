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
 *       cmChildStartIrq). Since task 45.12, on Windows 2000, XP and XP x64,
 *       on a PCI-to-PCI bridge below an active tunnel port it rewrites the
 *       fixed requirement list the PCI driver gives a hot-added bridge
 *       found closed, so a dock's downstream ports share its upstream
 *       port's windows (design record 33; cmBridgeReqFilter).
 *       Since task 49.46 (design record 21 section 6.3.1) a child on a
 *       unit records the status of each device D0 it passes down
 *       (cmChildD0Done), keeps the logical units of disk type its
 *       BusRelations answer carries (cmUnitRelations), and closes, reopens
 *       and drops them at its PnP requests, for the wake power-up.
 *
 * Every attachment, bridge or child, is on the registration list's
 * attachment list from its attach to its removal and reads its PDO's
 * location at the first request the PnP manager sends it (CmRegAttLocate);
 * a bridge filter whose location is a unit answers Removable as the
 * second kind of child does (decision M15: one object serving both).
 *
 * Every other IRP goes down unchanged (a device-usage notification with
 * the power flags mimicked, as a filter must). Each filter detaches and
 * deletes itself on removal, but on Windows 8.1 a child the BusRelations
 * attached (the kept child, design record 31 section 4.10): it stays on
 * its PDO across a removal of the device's stack and is destroyed when
 * the object below it is deleted (FastIoDetachDevice).
 *
 * IRQL: PASSIVE_LEVEL (PnP IRPs), or the lower device's power context: the
 * filter copies DO_POWER_PAGABLE and DO_POWER_INRUSH from the device it
 * sits on, as a filter must.
 */

#include "cm.h"
#include "cm_ports.h"
#include "cm_iso.h"
#include "../xhci_cmiso.h"

NTSTATUS CmFilterAddDevice(PDRIVER_OBJECT driver, PDEVICE_OBJECT pdo,
                           ULONG portId, ULONG oscRole)
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
    CmCommonInit(&filter->Common);
    filter->Common.Pdo = pdo;
    filter->Outstanding = 1;
    filter->PortId = portId;
    KeInitializeSpinLock(&filter->Lock);
    KeInitializeEvent(&filter->Drained, NotificationEvent, FALSE);
    CmRegFilterInit(filter);
    lower = IoAttachDeviceToDeviceStack(self, pdo);
    if (lower == NULL) {
        IoDeleteDevice(self);
        return STATUS_NO_SUCH_DEVICE;
    }
    filter->Common.Lower = lower;
    CmOscmInit(&filter->Osc, oscRole, pdo, lower);
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
        CmRegChildAddFailed();
        return STATUS_SUCCESS;
    }
    child = (PCM_FILTER)self->DeviceExtension;
    CmZero(child, sizeof(CM_FILTER));
    child->Common.Kind = CM_KIND_CHILD;
    child->Common.Self = self;
    CmCommonInit(&child->Common);
    child->Common.Pdo = pdo;
    child->Outstanding = 1;
    child->AddDev = 1;
    /* A unit: the device on a tunnel port's secondary bus. */
    child->Depth = 1;
    KeInitializeSpinLock(&child->Lock);
    KeInitializeEvent(&child->Drained, NotificationEvent, FALSE);
    CmWpuChildInit(child);
    CmRegFilterInit(child);
    lower = IoAttachDeviceToDeviceStack(self, pdo);
    if (lower == NULL) {
        IoDeleteDevice(self);
        CmRegChildAddFailed();
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

static VOID cmKeptLast(PCM_FILTER filter);

/* IRQL: <= DISPATCH_LEVEL (the completion below calls it). A kept
 * child's last reference is its destruction's (design record 31 section
 * 4.10, K3): it never waits on Drained. */
static VOID cmFilterRelease(PCM_FILTER filter)
{
    if (InterlockedDecrement(&filter->Outstanding) == 0) {
        if (filter->Kept) {
            cmKeptLast(filter);
            return;
        }
        KeSetEvent(&filter->Drained, IO_NO_INCREMENT, FALSE);
    }
}

/*
 * Task 45.13 (design record 34 sections 3.9 and 3.10): the chain action
 * holds a child filter's removal reference across its
 * IoInvalidateDeviceRelations on the PDO below, and the chained unit's
 * recheck across its parent's classification, so each is done before the
 * filter is detached and its IRP_MN_REMOVE_DEVICE completed, which waits
 * for the release. The caller takes it under the registration list's lock
 * (the list's lock, then the filter's: no path takes them the other way
 * round). IRQL: <= DISPATCH_LEVEL.
 */
BOOLEAN CmFilterTryHold(PCM_FILTER filter)
{
    KIRQL irql;
    BOOLEAN held;

    /* cmFilterAcquire's rule, with the PDO the chain action uses outside
     * the list's lock referenced in the same critical section, since a
     * kept child's callback can detach meanwhile (design record 31 section
     * 4.10, K3): admitted, the callback has not claimed Detached under
     * this lock, so the PDO is not yet freed. */
    held = FALSE;
    KeAcquireSpinLock(&filter->Lock, &irql);
    if (!filter->Removing) {
        (VOID)InterlockedIncrement(&filter->Outstanding);
        ObReferenceObject(filter->Common.Pdo);
        held = TRUE;
    }
    KeReleaseSpinLock(&filter->Lock, irql);
    return held;
}

VOID CmFilterUnhold(PCM_FILTER filter)
{
    ObDereferenceObject(filter->Common.Pdo);
    cmFilterRelease(filter);
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
    if (filter->Kept) {
        /* The lower object's reference its dispatch took for this read
         * (design record 31 section 4.10, K3). */
        ObDereferenceObject(filter->Common.Lower);
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
        if (!child->InLocate) {
            /* Record 34 section 3.10: a parent bridge not yet classified
             * as a chain port is read again before the answer. */
            CmRegChainRecheck(child);
        }
        if (CmRegAttMayMark(child)) {
            caps->Removable = 1;
            CmRegAttAnswered(child);
        }
    }
    return CmCompleteIrp(irp, status, irp->IoStatus.Information);
}

/*
 * Whether this system takes the tunnel id (CmPortsTagOs): Windows 2000 to
 * 8.1. 8.1 chooses a driver again when a device restarts with ids that
 * changed since its enumeration (read on 8.1, design record 31 section
 * 4.8), so there the id is given with the kept child, which stays on the
 * PDO across every restart of the stack and gives it again (section
 * 4.10); 10 keeps the class model and gets no id. The version is the
 * kernel's shared data page's (NtMajorVersion, NtMinorVersion: 6.3 on 8.1,
 * 10.0 on 10), since IoIsWdmVersionAvailable does not tell 8 from 8.1 and
 * no import is added. PASSIVE_LEVEL.
 */
static BOOLEAN cmChildTagOs(VOID)
{
    return (BOOLEAN)CmPortsTagOs(SharedUserData->NtMajorVersion,
                                 SharedUserData->NtMinorVersion);
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
    ULONG seq;

    /* Before the request goes down: its answer's PDOs are stamped with
     * when the query began (design record 34 section 3.12 item 5). */
    seq = CmRegChainBegin(child);
    status = CmForwardSync(&child->Common, irp);
    if (NT_SUCCESS(status) && child->Started) {
        CmRegChainRelations(
            child, (const DEVICE_RELATIONS *)irp->IoStatus.Information, seq);
    }
    return CmCompleteIrp(irp, status, irp->IoStatus.Information);
}

/* A unit's BusRelations (design record 21 section 6.3.1, task 49.46): the
 * list the function's driver put in before passing the request down, read
 * on its way back for the wake power-up's logical units (route R1). The
 * list is read and never changed. PASSIVE_LEVEL. */
static NTSTATUS cmUnitRelations(PCM_FILTER child, PIRP irp)
{
    NTSTATUS status;

    status = CmForwardSync(&child->Common, irp);
    if (NT_SUCCESS(status) && child->Started) {
        CmWpuRelations(child,
                       (const DEVICE_RELATIONS *)irp->IoStatus.Information);
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

/*
 * A bridge's requirements (design record 33 section 5.1, task 45.12): on
 * NT 5.x, for a child on a PCI-to-PCI bridge below an active tunnel port
 * (CmRegBreqFind) that is the lowest of this driver's filters in the
 * stack, the list the PCI driver answered rewritten by the pure rules
 * (cm_breq.c) from a snapshot read at this IRP (CmRegBreqSnap); every
 * attempt posted to the member's domain (XhciCmBridgeReqDiag). Anything
 * else goes down untouched. PASSIVE_LEVEL.
 */

/* TRUE when no other device object of this driver is between `f` and its
 * PDO: the lowest of a kernel-attached and a registered child acts alone
 * (record 33 section 6.1). The stack is the PnP manager's to change and
 * this IRP is its own, so it holds still. */
static BOOLEAN cmBreqLowest(PCM_FILTER f)
{
    PDEVICE_OBJECT walk;
    ULONG n;

    n = 0;
    for (walk = f->Common.Pdo->AttachedDevice;
         walk != NULL && walk != f->Common.Self && n < 32;
         walk = walk->AttachedDevice) {
        if (walk->DriverObject == CmDriverObject) {
            return FALSE;
        }
        n++;
    }
    return (BOOLEAN)(walk == f->Common.Self);
}

/* The plan of an attempt that could not be planned (zero throughout). */
static const CM_BREQ_PLAN cmBreqNoPlan = { 0 };

static NTSTATUS cmBridgeReqFilter(PCM_FILTER child, PIRP irp)
{
    NTSTATUS status;
    const UCHAR *list;
    UCHAR *fresh;
    ULONG parent;
    ULONG port;
    ULONG mode;
    ULONG address;
    ULONG bytes;
    ULONG bdf;
    ULONG bus;
    ULONG minor;
    ULONG outcome;
    ULONG e[7];
    CM_BREQ_SNAP *snap;
    CM_BREQ_PLAN *plan;

    if (!CmRegBreqFind(child, &parent, &port, &mode)) {
        CmRegBreqNoBridge(child);
        IoSkipCurrentIrpStackLocation(irp);
        return IoCallDriver(child->Common.Lower, irp);
    }
    minor = CmBreqGate - 1UL;
    status = CmForwardSync(&child->Common, irp);
    /* Read afresh: a driver below may have replaced the list it carried. */
    list = (const UCHAR *)irp->IoStatus.Information;
    snap = (CM_BREQ_SNAP *)CmAlloc(sizeof(CM_BREQ_SNAP) +
                                   sizeof(CM_BREQ_PLAN));
    if (snap == NULL) {
        /* No room to plan in: the list goes up as it came, the attempt
         * recorded with an empty plan. */
        CmBreqEntry(&cmBreqNoPlan, 0xFFFFUL, port, minor, CM_BREQ_OUT_NOMEM,
                    child->BreqTimes, e);
        CmRegBreqPost(port, e, &cmBreqNoPlan, 0);
        return CmCompleteIrp(irp, status, irp->IoStatus.Information);
    }
    plan = (CM_BREQ_PLAN *)(snap + 1);
    CmZero(plan, sizeof(*plan));
    bdf = 0xFFFFUL;
    bus = 0;
    if (!(NT_SUCCESS(status) || status == STATUS_NOT_SUPPORTED) ||
        list == NULL) {
        outcome = CM_BREQ_OUT_LOWER;
    } else if (mode == CM_BRIDGE_REQ_OFF) {
        plan->Shape = CmBreqShape(list, minor);
        outcome = CM_BREQ_OUT_OFF;
    } else {
        /* The bridge's device and function; its bus is the parent's
         * secondary (DevicePropertyBusNumber may not be answered yet, as
         * CmRegIrqWire found on 2000). The property's own capability
         * request passes this object unlocated (InLocate). */
        address = 0;
        bytes = 0;
        child->InLocate++;
        if (!NT_SUCCESS(IoGetDeviceProperty(child->Common.Pdo,
                                            DevicePropertyAddress,
                                            sizeof(address), &address,
                                            &bytes))) {
            address = 0xFFFFFFFFUL;
        }
        child->InLocate--;
        CmRegBreqSnap(parent, address, snap, &bdf, &bus);
        outcome = CmBreqPlan(list, minor, snap, plan);
        if (outcome == CM_BREQ_OUT_REWRITTEN && mode == CM_BRIDGE_REQ_RECORD) {
            outcome = CM_BREQ_OUT_RECORD;
        }
        if (outcome == CM_BREQ_OUT_REWRITTEN) {
            fresh = (UCHAR *)CmAllocPaged(plan->Bytes);
            if (fresh == NULL ||
                CmBreqWrite(list, plan, fresh, plan->Bytes) != plan->Bytes) {
                if (fresh != NULL) {
                    CmFree(fresh);
                }
                outcome = CM_BREQ_OUT_NOMEM;
            } else {
                CmFree((PVOID)list);
                irp->IoStatus.Information = (ULONG_PTR)fresh;
                status = STATUS_SUCCESS;
                if (child->BreqTimes != 0xFFFFFFFFUL) {
                    child->BreqTimes++;
                }
                child->BreqStart = 1;
                child->BreqBdf = bdf;
                child->BreqPort = port;
            }
        }
    }
    CmBreqEntry(plan, bdf, port, minor, outcome, child->BreqTimes, e);
    CmRegBreqPost(port, e, plan, bus);
    CmFree(snap);
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
    (VOID)CmWaitEvent(&done, 0);
    *status = irp->IoStatus.Status;
    CmFree(raw2);
    CmFree(tr2);
    CmRegIrqNote(child, "cm.chain.irq.start.stripped",
                 (child->Irq.Added << 16) | child->IrqBdf);
    return CM_IRQ_ACT_STRIP;
}

/* ---- the isolation statement (design record 22 section 10.5) ---- */

static const GUID cmIsoGuid = XHCI_CM_ISO_GUID_INIT;

static BOOLEAN cmIsoIsGuid(const GUID *g)
{
    ULONG i;

    if (g == NULL || g->Data1 != cmIsoGuid.Data1 ||
        g->Data2 != cmIsoGuid.Data2 || g->Data3 != cmIsoGuid.Data3) {
        return FALSE;
    }
    for (i = 0; i < 8; i++) {
        if (g->Data4[i] != cmIsoGuid.Data4[i]) {
            return FALSE;
        }
    }
    return TRUE;
}

/* The interface's references: counted on the filter under cmRegLock,
 * the first taken by CmRegIsoBind. xhci98.sys drops its last before its
 * own REMOVE goes down to this filter (record 17 section 4.13.6), so the
 * device object outlives every call. IRQL: <= DISPATCH_LEVEL. */
static VOID cmIsoRef(PVOID context)
{
    CmRegIsoRef((PCM_FILTER)context);
}

static VOID cmIsoDeref(PVOID context)
{
    CmRegIsoDeref((PCM_FILTER)context);
}

/* 1 when the bound tunnel's path stands read back closed (CmRegIsoAsk,
 * cm_iso.h CM_ISO_YES). PASSIVE_LEVEL. */
static ULONG cmIsoIsolated(PVOID context)
{
    return CmRegIsoAsk((PCM_FILTER)context) == CM_ISO_YES ? 1UL : 0UL;
}

/*
 * IRP_MN_QUERY_INTERFACE for GUID_XHCI_CM_ISOLATION on a child (task
 * 45.35): answered when the version and size fit and the binding can be
 * taken (CmRegIsoBind), with the one reference the caller then owns;
 * otherwise passed down as it came. NT 5.x alone (CmBreqGate).
 * PASSIVE_LEVEL.
 */
static NTSTATUS cmIsoQuery(PCM_FILTER filter, PIRP irp)
{
    PIO_STACK_LOCATION stack;
    PXHCI_CM_ISO_INTERFACE out;

    stack = IoGetCurrentIrpStackLocation(irp);
    out = (PXHCI_CM_ISO_INTERFACE)stack->Parameters.QueryInterface.Interface;
    if (out == NULL ||
        stack->Parameters.QueryInterface.Version != XHCI_CM_ISO_VERSION ||
        stack->Parameters.QueryInterface.Size < sizeof(XHCI_CM_ISO_INTERFACE) ||
        !CmRegIsoBind(filter)) {
        IoSkipCurrentIrpStackLocation(irp);
        return IoCallDriver(filter->Common.Lower, irp);
    }
    out->Header.Size = (USHORT)sizeof(XHCI_CM_ISO_INTERFACE);
    out->Header.Version = XHCI_CM_ISO_VERSION;
    out->Header.Context = (PVOID)filter;
    out->Header.InterfaceReference = cmIsoRef;
    out->Header.InterfaceDereference = cmIsoDeref;
    out->Isolated = cmIsoIsolated;
    /* The one reference the caller owns was taken with the binding. */
    return CmCompleteIrp(irp, STATUS_SUCCESS, irp->IoStatus.Information);
}

/* ---- the kept child (design record 31 section 4.10, task 48.29) ---- */

/*
 * On 8.1 alone (CmKeptOn) a child the bridge's BusRelations attached does
 * a logical removal at IRP_MN_REMOVE_DEVICE while the object below it
 * stays: it stays attached, so the PnP manager's IRP_MN_QUERY_ID at the
 * next start of the stack reaches it and the tunnel id is given again. It
 * is destroyed once the object below it is deleted: the I/O manager's
 * FastIoDetachDevice (cm_entry.c) claims Detached, detaches and drops the
 * attach's reference, and the last reference, the callback's own or a
 * dispatch's still in flight, destroys it (K3, K5).
 */

/* The destruction, the child detached and no reference left. The
 * departure is recorded here only if no removal recorded it
 * (CmRegAttRemoved reads KeptDeparted). PASSIVE_LEVEL. */
static VOID cmKeptDestroy(PCM_FILTER filter)
{
    CmRegAttRemoved(filter, (BOOLEAN)(filter->QueryRemoved &&
                                      !filter->SurpriseRemoved));
    CmRegKeptNote(filter, "cm.kept.final", filter->KeptRemoves & 0xFUL);
    IoDeleteDevice(filter->Common.Self);
}

/* The work item's routine: the destruction a release above PASSIVE_LEVEL
 * handed over (K5). Nothing of the child is touched after it. The image
 * stays loaded on 8.1 (DriverEntry clears DriverUnload). PASSIVE_LEVEL. */
static VOID cmKeptWork(PVOID context)
{
    cmKeptDestroy((PCM_FILTER)context);
}

/* The release that left no reference (cmFilterRelease). IRQL: <=
 * DISPATCH_LEVEL. */
static VOID cmKeptLast(PCM_FILTER filter)
{
    ULONG last;

    last = CmPortsKeptLast(0, filter->KeptState,
                           (ULONG)(KeGetCurrentIrql() > PASSIVE_LEVEL));
    if (last == CM_KEPT_L_DESTROY) {
        cmKeptDestroy(filter);
    } else if (last == CM_KEPT_L_QUEUE) {
        ExQueueWorkItem(&filter->KeptWork, DelayedWorkQueue);
    } else {
        /* Not expected: the attach's reference goes only at Detached. */
        CmRegKeptNote(filter, "cm.kept.zero", filter->KeptState);
    }
}

/* A child cmChildCreate is making, before its attach (cm_reenum.c). */
VOID CmKeptInit(PCM_FILTER child)
{
    child->Kept = 1;
    child->KeptState = CM_KEPT_ATTACHED;
    ExInitializeWorkItem(&child->KeptWork, cmKeptWork, child);
}

/* One step of the state (CmPortsKeptStep) under the child's lock, and the
 * marks' part of its action. PASSIVE_LEVEL. */
static ULONG cmKeptStep(PCM_FILTER filter, ULONG event)
{
    KIRQL irql;
    ULONG action;

    KeAcquireSpinLock(&filter->Lock, &irql);
    filter->KeptState = CmPortsKeptStep(filter->KeptState, event, &action);
    KeReleaseSpinLock(&filter->Lock, irql);
    if ((action & (CM_KEPT_A_PRESENT | CM_KEPT_A_END_DEPART)) != 0) {
        CmRegKeptAttached(filter);
        CmRegKeptNote(filter, "cm.kept.rearm", filter->KeptRemoves & 0xFUL);
    }
    return action;
}

/* IRP_MN_REMOVE_DEVICE on a kept child (K4). PASSIVE_LEVEL. */
static NTSTATUS cmKeptRemove(PCM_FILTER filter, PIRP irp)
{
    NTSTATUS status;
    BOOLEAN afterQr;
    ULONG action;

    CmRegIsoGone(filter);
    status = CmForwardSync(&filter->Common, irp);
    CmRegChainNote(filter, CM_CHAIN_EV_REMOVE);
    afterQr = (BOOLEAN)(filter->QueryRemoved && !filter->SurpriseRemoved);
    filter->KeptRemoves++;
    action = cmKeptStep(filter, CM_KEPT_E_REMOVED);
    if ((action & CM_KEPT_A_DEPART) != 0) {
        CmRegAttDepart(filter, afterQr);
    }
    filter->QueryRemoved = 0;
    filter->SurpriseRemoved = 0;
    CmRegKeptNote(filter,
                  filter->KeptState == CM_KEPT_DETACHED ? "cm.kept.gone"
                                                        : "cm.kept.logical",
                  filter->KeptRemoves & 0xFUL);
    /* The dispatch's reference (CmKeptDispatch) is released after this:
     * where the object below was deleted inside the forwarded removal,
     * that release destroys the child. */
    return CmCompleteIrp(irp, status, irp->IoStatus.Information);
}

/*
 * The I/O manager's FastIoDetachDevice for a filter or child of this
 * driver (cm_entry.c, K6): `target`, the object below, is being deleted,
 * and the callback's contract is that the object above detaches here. A
 * bridge filter or a registered child only detaches (its removal in
 * progress deletes it), waiting for a detach that removal claimed first,
 * so it never returns with the object attached. A kept child claims
 * Detached under its lock,
 * which closes admission, detaches, and drops the attach's reference
 * without waiting, so it never waits on the dispatch that caused it.
 * Every call is recorded: event bits 0-1 the kind (1 child, 2 filter),
 * bit 2 kept, bit 3 the state was Logical. PASSIVE_LEVEL (IoDeleteDevice).
 */
VOID CmFilterDetachCb(PCM_FILTER filter, PDEVICE_OBJECT target)
{
    ULONG ev;
    ULONG action;
    KIRQL irql;

    ev = (filter->Common.Kind == CM_KIND_CHILD ? 1UL : 2UL) |
         (filter->Kept ? 4UL : 0UL) |
         (filter->KeptState == CM_KEPT_LOGICAL ? 8UL : 0UL);
    CmRegKeptNote(filter, "cm.kept.cb", ev);
    if (filter->Common.Lower != target) {
        CmRegKeptNote(filter, "cm.kept.cb.notlower", ev);
        return;
    }
    if (!filter->Kept) {
        /* Waits for a removal that claimed the detach first: the object
         * is detached when this returns (K6). */
        (VOID)CmDetachOnce(&filter->Common, TRUE);
        return;
    }
    KeAcquireSpinLock(&filter->Lock, &irql);
    filter->KeptState = CmPortsKeptStep(filter->KeptState, CM_KEPT_E_CALLBACK,
                                        &action);
    if ((action & CM_KEPT_A_DETACH) != 0) {
        /* Closes cmFilterAcquire too (the chain's holds, a usage
         * notification), with the dispatch's admission. */
        filter->Removing = 1;
    }
    KeReleaseSpinLock(&filter->Lock, irql);
    if ((action & CM_KEPT_A_ABSENT) != 0) {
        CmRegKeptAbsent(filter);
    }
    if ((action & CM_KEPT_A_DETACH) != 0) {
        (VOID)CmDetachOnce(&filter->Common, TRUE);
    }
    if ((action & CM_KEPT_A_DROP_ATTACH) != 0) {
        cmFilterRelease(filter);
    }
}

/*
 * Every request to a kept child, of every major function (cm_entry.c's
 * dispatch, K3 and K7): admitted, while the state is not Detached, with a
 * reference on the child and an object reference on the object below it
 * for the dispatch; refused otherwise, completed with
 * STATUS_NO_SUCH_DEVICE (a power request started on first), never
 * forwarded. Admission is under the child's lock, where the callback
 * claims Detached. IRQL: the request's.
 */
NTSTATUS CmKeptDispatch(PCM_FILTER filter, PIRP irp)
{
    PIO_STACK_LOCATION stack;
    PDEVICE_OBJECT lower;
    NTSTATUS status;
    KIRQL irql;
    BOOLEAN in;

    stack = IoGetCurrentIrpStackLocation(irp);
    in = FALSE;
    lower = NULL;
    KeAcquireSpinLock(&filter->Lock, &irql);
    if (CmPortsKeptAdmits(filter->KeptState) && !filter->Removing) {
        (VOID)InterlockedIncrement(&filter->Outstanding);
        /* Under the lock the callback claims Detached under: the object
         * below is still attached here, so not yet freed. */
        lower = filter->Common.Lower;
        ObReferenceObject(lower);
        in = TRUE;
    }
    KeReleaseSpinLock(&filter->Lock, irql);
    if (!in) {
        CmRegKeptNote(filter, "cm.kept.late",
                      ((ULONG)stack->MajorFunction << 8) |
                          (ULONG)stack->MinorFunction);
        if (stack->MajorFunction == IRP_MJ_POWER) {
            PoStartNextPowerIrp(irp);
        }
        return CmCompleteIrp(irp, STATUS_NO_SUCH_DEVICE, 0);
    }
    if (stack->MajorFunction == IRP_MJ_PNP) {
        status = CmFilterPnp(filter, irp);
    } else if (stack->MajorFunction == IRP_MJ_POWER) {
        status = CmFilterPower(filter, irp);
    } else {
        IoSkipCurrentIrpStackLocation(irp);
        status = IoCallDriver(lower, irp);
    }
    ObDereferenceObject(lower);
    cmFilterRelease(filter);
    return status;
}

static NTSTATUS cmFilterPnpBody(PCM_FILTER filter, PIRP irp, BOOLEAN sync);

/*
 * An ASM4242 downstream port's member half (record 32 section 12.8.15.7,
 * cm_oscnt.c): its tracked requests counted from their arrival until they
 * return, waiting while an exchange holds the members; a surprise removal
 * never waits. Every other filter goes straight to the body.
 */
NTSTATUS CmFilterPnp(PCM_FILTER filter, PIRP irp)
{
    PCM_OSC_MEMBER m;
    NTSTATUS status;
    UCHAR minor;

    m = &filter->Osc;
    minor = IoGetCurrentIrpStackLocation(irp)->MinorFunction;
    if (filter->Common.Kind != CM_KIND_FILTER || m->Role == 0) {
        return cmFilterPnpBody(filter, irp, FALSE);
    }
    if (minor == IRP_MN_SURPRISE_REMOVAL) {
        CmOscmSurprise(m);
        return cmFilterPnpBody(filter, irp, FALSE);
    }
    if (!CmOscmTracked(irp)) {
        return cmFilterPnpBody(filter, irp, FALSE);
    }
    CmOscmEnter(m);
    if (minor == IRP_MN_REMOVE_DEVICE) {
        /* The body deletes the filter: the member goes first. */
        CmOscmRemove(m);
        CmOscmLeave(m);
        return cmFilterPnpBody(filter, irp, FALSE);
    }
    /* Forwarded and waited for, so the member's state is the request's
     * final answer and it is counted until it returns. */
    status = cmFilterPnpBody(filter, irp, TRUE);
    CmOscmAfter(m, minor, status);
    CmOscmLeave(m);
    return status;
}

static NTSTATUS cmFilterPnpBody(PCM_FILTER filter, PIRP irp, BOOLEAN sync)
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
        if (filter->Kept) {
            /* For the completion's read of the lower object's Flags. */
            ObReferenceObject(filter->Common.Lower);
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
        if (filter->Kept) {
            /* A logically removed kept child: Starting (K3). */
            (VOID)cmKeptStep(filter, CM_KEPT_E_START);
        }
        irqAct = CM_IRQ_ACT_PASS;
        if (child && CmIrqGate) {
            /* Design record 21 section 6.2.1: the added interrupt kept
             * from pci.sys (stripped), or the start failed. */
            irqAct = cmChildStartIrq(filter, irp, &status);
        }
        if (irqAct == CM_IRQ_ACT_FAIL) {
            if (filter->Kept) {
                (VOID)cmKeptStep(filter, CM_KEPT_E_START_FAIL);
            }
            CmRegAttEvent(filter, CM_MK_START_FAILED);
            return CmCompleteIrp(irp, STATUS_INSUFFICIENT_RESOURCES, 0);
        }
        if (irqAct != CM_IRQ_ACT_STRIP) {
            status = CmForwardSync(&filter->Common, irp);
        }
        CmRegAttLocate(filter);
        if (filter->Kept) {
            /* Attached, presence and the departure's end, only once the
             * start completed with success below the child (K3, K8). */
            (VOID)cmKeptStep(filter, NT_SUCCESS(status) ? CM_KEPT_E_START_OK
                                                        : CM_KEPT_E_START_FAIL);
        }
        if (child && filter->BreqStart) {
            /* The start after a rewrite (record 33 section 9.2). */
            filter->BreqStart = 0;
            CmRegBreqStarted(filter->BreqPort, filter->BreqBdf,
                             (BOOLEAN)NT_SUCCESS(status), status);
        }
        if (NT_SUCCESS(status)) {
            filter->Started = 1;
            filter->QueryRemoved = 0;
            filter->SurpriseRemoved = 0;
            if (!child) {
                CmRegPublish(filter);
                /* The ACPI-named source's reading (record 22 section
                 * 7.6.1), on an AMD family's root port, where the NHI
                 * can read its own name (CmDsdFilterRead). */
                if (CmPortsAcpiPort(filter->PortId)) {
                    CmDsdFilterRead(filter);
                }
            } else {
                CmRegChainNote(filter, CM_CHAIN_EV_START);
                /* Record 21 section 6.3.1: reopened, its units read
                 * afresh. */
                CmWpuStarted(filter);
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
        } else if (filter->Started &&
                   cmChainKindOf(filter) != CM_CHAIN_BRIDGE) {
            return cmUnitRelations(filter, irp);
        }
        break;

    case IRP_MN_QUERY_RESOURCE_REQUIREMENTS:
        if (child && CmIrqGate) {
            return cmChildReqQuery(filter, irp);
        }
        break;

    case IRP_MN_FILTER_RESOURCE_REQUIREMENTS:
        if (child && CmBreqGate != 0 &&
            cmChainKindOf(filter) == CM_CHAIN_BRIDGE) {
            /* Design record 33: a bridge, never given an interrupt by the
             * branch below (CmRegIrqWire refuses a type 1 header). */
            if (!cmBreqLowest(filter)) {
                break;
            }
            return cmBridgeReqFilter(filter, irp);
        }
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

    case IRP_MN_QUERY_INTERFACE:
        if (child && CmBreqGate != 0 &&
            cmIsoIsGuid(stack->Parameters.QueryInterface.InterfaceType)) {
            return cmIsoQuery(filter, irp);
        }
        break;

    case IRP_MN_QUERY_REMOVE_DEVICE:
        filter->QueryRemoved = 1;
        CmRegAttEvent(filter, CM_MK_QUERY_REMOVE);
        if (child) {
            /* Record 21 section 6.3.1: no wake power-up asked from here
             * on, before the query goes down; its units are kept. */
            CmWpuClose(filter, FALSE);
        }
        break;

    case IRP_MN_CANCEL_REMOVE_DEVICE:
        filter->QueryRemoved = 0;
        CmRegAttEvent(filter, CM_MK_CANCEL_REMOVE);
        if (child) {
            /* Reopened once the drivers below have cancelled it. */
            status = CmForwardSync(&filter->Common, irp);
            CmWpuReopen(filter);
            return CmCompleteIrp(irp, status, irp->IoStatus.Information);
        }
        break;

    case IRP_MN_STOP_DEVICE:
        if (child) {
            CmWpuClose(filter, TRUE);
        }
        filter->Started = 0;
        if (!child) {
            CmRegWithdraw(filter);
        }
        break;

    case IRP_MN_SURPRISE_REMOVAL:
        if (child) {
            CmWpuClose(filter, TRUE);
        }
        started = (BOOLEAN)(filter->Started != 0);
        filter->Started = 0;
        filter->SurpriseRemoved = 1;
        if (child) {
            CmRegIsoGone(filter);
        }
        CmRegAttSurprise(filter, started);
        if (!child) {
            CmRegWithdraw(filter);
        }
        break;

    case IRP_MN_REMOVE_DEVICE:
        if (child) {
            /* Every unit reference dropped before the removal goes down
             * (the kept child's logical removal too). */
            CmWpuClose(filter, TRUE);
        }
        filter->Started = 0;
        if (!child) {
            CmRegWithdraw(filter);
        }
        if (filter->Kept) {
            /* Design record 31 section 4.10: a logical removal. */
            return cmKeptRemove(filter, irp);
        }
        /* Close acquisition, forward the removal, then drop the attach's
         * reference and wait for every notification in flight to finish
         * with the filter before it is detached and deleted. */
        KeAcquireSpinLock(&filter->Lock, &irql);
        filter->Removing = 1;
        KeReleaseSpinLock(&filter->Lock, irql);
        if (child) {
            CmRegIsoGone(filter);
            /* The child waits for the removal to finish below, detaches,
             * and only then records the departure (CmRegAttRemoved), so it
             * is recorded once this function's stack is gone. */
            status = CmForwardSync(&filter->Common, irp);
            CmRegChainNote(filter, CM_CHAIN_EV_REMOVE);
            cmFilterRelease(filter);
            (VOID)CmWaitEvent(&filter->Drained, 0);
            /* The one detach owner (design record 31 section 4.10, K6). */
            (VOID)CmDetachOnce(&filter->Common, TRUE);
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
        (VOID)CmWaitEvent(&filter->Drained, 0);
        (VOID)CmDetachOnce(&filter->Common, TRUE);
        CmRegAttRemoved(filter, (BOOLEAN)(filter->QueryRemoved &&
                                          !filter->SurpriseRemoved));
        IoDeleteDevice(filter->Common.Self);
        return status;

    default:
        break;
    }
    if (sync) {
        status = CmForwardSync(&filter->Common, irp);
        return CmCompleteIrp(irp, status, irp->IoStatus.Information);
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
/* A child's device D0, on its way back (design record 21 section 6.3.1,
 * task 49.46): its status recorded against the epoch it was dispatched
 * in; the IRP is never retained. IRQL: <= DISPATCH_LEVEL. */
static NTSTATUS cmChildD0Done(PDEVICE_OBJECT device, PIRP irp, PVOID context)
{
    PCM_FILTER child;

    child = (PCM_FILTER)device->DeviceExtension;
    if (irp->PendingReturned) {
        IoMarkIrpPending(irp);
    }
    CmWpuD0Seen(child, (ULONG)(ULONG_PTR)context, irp->IoStatus.Status);
    /* The last access: removal may free the child after this. */
    cmFilterRelease(child);
    return STATUS_SUCCESS;
}

NTSTATUS CmFilterPower(PCM_FILTER filter, PIRP irp)
{
    PIO_STACK_LOCATION stack;

    stack = IoGetCurrentIrpStackLocation(irp);
    /* Record 21 section 6.3.1: a sleep, shutdown or power-down seen here
     * closes the wake power-up's asks at once. */
    CmWpuPowerSeen(stack->MinorFunction, (ULONG)stack->Parameters.Power.Type,
                   (ULONG)stack->Parameters.Power.State.SystemState);
    if (filter->Common.Kind == CM_KIND_CHILD &&
        stack->MinorFunction == IRP_MN_SET_POWER &&
        stack->Parameters.Power.Type == DevicePowerState &&
        stack->Parameters.Power.State.DeviceState == PowerDeviceD0 &&
        CmWpuOn() && NT_SUCCESS(cmFilterAcquire(filter))) {
        /* Passed down at once, as every power IRP, with a completion
         * routine that only records the outcome (record 21 section
         * 6.3.1); the epoch and start count read now, so a D0 that
         * completes after a later sleep or restart is known as stale. */
        PoStartNextPowerIrp(irp);
        IoCopyCurrentIrpStackLocationToNext(irp);
        CM_SET_COMPLETION_ALWAYS(irp, cmChildD0Done, CmWpuD0Context(filter));
        return PoCallDriver(filter->Common.Lower, irp);
    }
    if (filter->Common.Kind == CM_KIND_FILTER &&
        stack->MinorFunction == IRP_MN_SET_POWER &&
        stack->Parameters.Power.Type == DevicePowerState &&
        stack->Parameters.Power.State.DeviceState == PowerDeviceD0) {
        CmRegPassD0(filter);
    }
    if (filter->Common.Kind == CM_KIND_FILTER && filter->Osc.Role != 0) {
        return CmOscmPowerPass(&filter->Osc, filter->Common.Lower, irp);
    }
    PoStartNextPowerIrp(irp);
    IoSkipCurrentIrpStackLocation(irp);
    return PoCallDriver(filter->Common.Lower, irp);
}
