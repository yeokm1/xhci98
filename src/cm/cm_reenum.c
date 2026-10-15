/*
 * cm_reenum.c - re-enumeration and safe eject (cm_reenum.h has the calls
 * and their contract; design record 21 sections 3.2, 6 and 7.4;
 * roadmap-usb4.md tasks 39-A.5, driver half, and 39-A.8).
 *
 * THE REGISTRATION LIST (section 3.2). One list in the image, under one
 * spin lock, holds two kinds of entry: each bridge filter's, published
 * after its start completed below it and withdrawn at stop, surprise
 * removal and removal; and each domain's, listed by CmReenumStart with the
 * members of its tunnel-port set and delisted by CmReenumStop. Every
 * change to either recomputes, under the lock, each member's flags and
 * each filter's owner: a member is EXCLUSIVE when no other listed domain
 * claims its location (tbt_topo.c XhciTbtExclusive's rule, any source
 * against any source), and on NT ACTIVE when it is exclusive and its
 * filter has published and taken its baseline; on 98 ACTIVE is EXCLUSIVE,
 * there being no filter. A filter's Owner, OwnerIndex, Active and
 * SafeEject follow. The lock is never held across a call out: a call on a
 * filter takes it under the lock with InFlight raised, releases the lock,
 * calls, and lowers InFlight under the lock again; a withdrawal sets Gone
 * under the lock and waits on Rundown, which the last lowering sets.
 *
 * THE ROUTES. NT: IoInvalidateDeviceRelations(BusRelations) on each
 * active member's PDO, through its filter (section 6.2). Windows 98 SE
 * and ME: CONFIGMG through cm_cfgmg.c (section 6.1) - the bridge's devnode
 * found by its BUS_&DEV_&FUNC_ instance, vanished children removed with
 * Query_Remove_SubTree and Remove_SubTree, then Reenumerate_DevNode.
 * Either way every tunnel change acts on every active member (section
 * 6.3, "a set of more than one"), after the bounded port poll of section
 * 6.5 through tbt_topo.c (up: until a member's reading turns up, down:
 * until every member reads down, 2 s either way; RESYNC polls nothing).
 * XhciCmReenum 0 makes no call on either route: the set is still found,
 * claimed and marked active, so the hold and safe eject keep working
 * after a manual refresh.
 *
 * SAFE EJECT (section 7.4). NT: the bridge filter's BusRelations reading
 * (CmRegRelations) attaches a child filter (CM_KIND_CHILD) above each PDO
 * new since its snapshot, on an active bridge with XhciCmSafeEject 1; the
 * child answers IRP_MN_QUERY_CAPABILITIES with Removable (cm_filt.c). A
 * child removed after a query-remove posts an eject to the owning domain,
 * which CmReenumService hands to the per-port entry of record 22 section
 * 10.4 when one is set. 98 SE and ME, best effort: after a tunnel up, for
 * a window of CM_MARK_WINDOW_MS, CmReenumService walks each active
 * member's children and adds DN_REMOVABLE (Change_DevNode_Status) to each
 * new one, never to a tunnel port itself.
 *
 * PCI configuration space is read through HalGetBusData from offset 0 on
 * every target (the ...ByOffset form has no Windows 98 evidence; hcd_ctl.c
 * hcdPirqSlot has the record). Nothing is written: no by-offset write to a
 * bridge is qualified on every target (tbt_topo.h), so tbt_topo.c is given
 * no writer and an SHPC-only port (the emulator's i440FX bridge) has no
 * presence reading - its poll ends at once with nothing to read, and the
 * re-enumeration follows.
 *
 * IRQL: the CmReenum* calls at PASSIVE_LEVEL on the connection-manager
 * thread; the CmReg* calls as cm_reenum.h states; the hold's completion
 * routine and DPC at <= DISPATCH_LEVEL.
 */

#include "cm.h"
#include "cm_cfgmg.h"
#include "cm_ports.h"
#include "cm_nvlink.h"

/* The USB4 profile's actions reach the seam with this bit in Why (cm_nhi.c,
 * cmU4ToIcm); its nvme98 statement is made at the core's upcall, not here. */
#ifndef CM_U4_WHY_BASE
#define CM_U4_WHY_BASE 0x80UL
#endif

/* HalGetBusData: as hcd_ctl.c restates it for amd64 builds whose headers
 * leave it out. */
#if defined(_WIN64) && defined(NO_LEGACY_DRIVERS)
NTHALAPI ULONG HalGetBusData(BUS_DATA_TYPE BusDataType, ULONG BusNumber,
                             ULONG SlotNumber, PVOID Buffer, ULONG Length);
#endif

/* How long after a tunnel up CmReenumService looks for new children to
 * mark on 98 (Reenumerate_DevNode is not synchronous). */
#define CM_MARK_WINDOW_MS   10000UL

/* A discovery that could not read every function is retried this often. */
#define CM_FIND_TRIES       3UL
#define CM_FIND_RETRY_MS    100UL

/* An owed re-enumeration is retried no faster than this. */
#define CM_MISSED_RETRY_MS  5000UL

/* An empty set is looked for again this often, this far apart. */
#define CM_REDISCOVER_TRIES 30UL
#define CM_REDISCOVER_MS    2000UL

/* The 98 devnode walk's bounds. */
#define CM_WALK_STACK       64UL
#define CM_WALK_NODES       2048UL

/* What still answers on a secondary bus, for 98's vanished children. */
#define CM_BUS_FUNCTIONS    64UL

/* The registration list. */
static KSPIN_LOCK cmRegLock;
static LIST_ENTRY cmRegFilters;
static LIST_ENTRY cmRegDomains;
static ULONG cmRegSerial;
static ULONG cmRegAttached;
static ULONG cmRegSkipped;
static ULONG cmRegRefused;
static ULONG cmRegWatchdogs;

static NTSTATUS cmChildCreate(PCM_FILTER bridge, PDEVICE_OBJECT pdo);
static VOID cmDiscoverAndList(PCM_NHI nhi);
static VOID cmHoldDpc(PKDPC dpc, PVOID context, PVOID arg1, PVOID arg2);

/* ====================================================================== */
/* Configuration space                                                    */
/* ====================================================================== */

/* tbt_topo.c's reader. A function that does not answer reads all-ones
 * (HalGetBusData returns 2 then, the vendor id it read); a bus the HAL
 * does not know is a failed access. IRQL: PASSIVE_LEVEL. */
static ULONG cmCfgRead(PVOID context, ULONG bdf, ULONG offset, PULONG value)
{
    UCHAR buffer[256];
    ULONG need;
    ULONG got;

    UNREFERENCED_PARAMETER(context);
    if ((offset & 3UL) != 0 || offset > 252UL) {
        return 0;
    }
    need = offset + 4UL;
    /* PCI_SLOT_NUMBER: device 4:0, function 7:5. */
    got = HalGetBusData(PCIConfiguration, XHCI_TBT_BDF_BUS(bdf),
                        XHCI_TBT_BDF_DEV(bdf) |
                            (XHCI_TBT_BDF_FN(bdf) << 5),
                        buffer, need);
    if (got >= need) {
        *value = (ULONG)buffer[offset] | ((ULONG)buffer[offset + 1] << 8) |
                 ((ULONG)buffer[offset + 2] << 16) |
                 ((ULONG)buffer[offset + 3] << 24);
        return 1;
    }
    if (got == 2) {
        *value = 0xFFFFFFFFUL;
        return 1;
    }
    return 0;
}

static ULONG cmNow(PCM_NHI nhi)
{
    return nhi->Host.NowMs(nhi->Host.Context);
}

static VOID cmSleep(PCM_NHI nhi, ULONG ms)
{
    if (ms != 0) {
        nhi->Host.Sleep(nhi->Host.Context, ms);
    }
}

/* ====================================================================== */
/* The registration list                                                  */
/* ====================================================================== */

VOID CmRegInit(VOID)
{
    KeInitializeSpinLock(&cmRegLock);
    InitializeListHead(&cmRegFilters);
    InitializeListHead(&cmRegDomains);
}

VOID CmRegFilterInit(PCM_FILTER filter)
{
    InitializeListHead(&filter->RegLink);
    KeInitializeEvent(&filter->Rundown, NotificationEvent, FALSE);
    KeInitializeTimer(&filter->HoldTimer);
    KeInitializeDpc(&filter->HoldDpc, cmHoldDpc, filter);
}

VOID CmRegCounters(PULONG attached, PULONG skipped, PULONG refused,
                   PULONG watchdogs)
{
    KIRQL irql;

    KeAcquireSpinLock(&cmRegLock, &irql);
    *attached = cmRegAttached;
    *skipped = cmRegSkipped;
    *refused = cmRegRefused;
    *watchdogs = cmRegWatchdogs;
    KeReleaseSpinLock(&cmRegLock, irql);
}

/* The published, not withdrawn, filter at `bdf`, or NULL. Under the lock. */
static PCM_FILTER cmRegFind(ULONG bdf)
{
    PLIST_ENTRY e;
    PCM_FILTER f;

    for (e = cmRegFilters.Flink; e != &cmRegFilters; e = e->Flink) {
        f = CONTAINING_RECORD(e, CM_FILTER, RegLink);
        if (f->Published && !f->Gone && f->Bdf == bdf) {
            return f;
        }
    }
    return NULL;
}

/* InFlight lowered; the last one after a withdrawal sets Rundown. Under
 * the lock. */
static VOID cmRegDrop(PCM_FILTER f)
{
    f->InFlight--;
    if (f->InFlight == 0 && f->Gone) {
        KeSetEvent(&f->Rundown, IO_NO_INCREMENT, FALSE);
    }
}

/*
 * Every member's flags and every filter's owner, from the list as it
 * stands (the comment at the top of this file). Under the lock.
 */
static VOID cmRegRecompute(VOID)
{
    PLIST_ENTRY e;
    PLIST_ENTRY o;
    PCM_FILTER f;
    PCM_NHI d;
    PCM_NHI other;
    PCM_REENUM r;
    ULONG i;
    ULONG j;
    ULONG claimed;
    ULONG flags;

    for (e = cmRegFilters.Flink; e != &cmRegFilters; e = e->Flink) {
        f = CONTAINING_RECORD(e, CM_FILTER, RegLink);
        f->Owner = NULL;
        f->Active = 0;
        f->SafeEject = 0;
    }
    for (e = cmRegDomains.Flink; e != &cmRegDomains; e = e->Flink) {
        r = CONTAINING_RECORD(e, CM_REENUM, Link);
        d = CONTAINING_RECORD(r, CM_NHI, Reenum);
        for (i = 0; i < r->Members; i++) {
            claimed = 0;
            for (o = cmRegDomains.Flink; o != &cmRegDomains; o = o->Flink) {
                other = CONTAINING_RECORD(CONTAINING_RECORD(o, CM_REENUM,
                                                            Link),
                                          CM_NHI, Reenum);
                if (other == d) {
                    continue;
                }
                for (j = 0; j < other->Reenum.Members; j++) {
                    if (other->Reenum.Port[j].Bdf == r->Port[i].Bdf) {
                        claimed = 1;
                    }
                }
            }
            flags = r->Port[i].Flags & (CM_PORT_HELD | CM_PORT_EJECTED);
            if (!claimed) {
                flags |= CM_PORT_EXCLUSIVE;
            }
            if (r->Route == CM_ROUTE_FILTER) {
                f = cmRegFind(r->Port[i].Bdf);
                if (f == NULL) {
                    flags |= CM_PORT_NO_FILTER;
                } else if (!claimed && f->Baseline) {
                    flags |= CM_PORT_ACTIVE;
                    f->Owner = d;
                    f->OwnerIndex = i;
                    f->Active = 1;
                    f->SafeEject = r->SafeEject;
                } else if (!claimed) {
                    /* Owned, not yet active: the baseline is awaited. */
                    f->Owner = d;
                    f->OwnerIndex = i;
                }
            } else if (r->Route == CM_ROUTE_CONFIGMG && !claimed) {
                flags |= CM_PORT_ACTIVE;
            }
            r->Port[i].Flags = flags;
        }
    }
}

/*
 * Takes the filter of `d`'s member `index` for a call: TRUE with *pdo and
 * *filter, InFlight raised, when it is published and owned by `d` (and,
 * with `activeOnly`, active). Release with cmRegRelease.
 */
static BOOLEAN cmRegTake(PCM_NHI d, ULONG index, BOOLEAN activeOnly,
                         PCM_FILTER *filter, PDEVICE_OBJECT *pdo)
{
    KIRQL irql;
    PCM_FILTER f;
    BOOLEAN ok;

    ok = FALSE;
    KeAcquireSpinLock(&cmRegLock, &irql);
    f = cmRegFind(d->Reenum.Port[index].Bdf);
    if (f != NULL && f->Owner == d && (!activeOnly || f->Active)) {
        f->InFlight++;
        *filter = f;
        *pdo = f->Common.Pdo;
        ok = TRUE;
    }
    KeReleaseSpinLock(&cmRegLock, irql);
    return ok;
}

static VOID cmRegRelease(PCM_FILTER f)
{
    KIRQL irql;

    KeAcquireSpinLock(&cmRegLock, &irql);
    cmRegDrop(f);
    KeReleaseSpinLock(&cmRegLock, irql);
}

/* The bridge's location, from its PDO. PASSIVE_LEVEL. */
static BOOLEAN cmLocation(PDEVICE_OBJECT pdo, PULONG bdf)
{
    ULONG bus;
    ULONG address;
    ULONG bytes;
    NTSTATUS status;

    bus = 0;
    address = 0;
    status = IoGetDeviceProperty(pdo, DevicePropertyBusNumber, sizeof(bus),
                                 &bus, &bytes);
    if (!NT_SUCCESS(status) || bus > 0xFFUL) {
        return FALSE;
    }
    status = IoGetDeviceProperty(pdo, DevicePropertyAddress, sizeof(address),
                                 &address, &bytes);
    if (!NT_SUCCESS(status)) {
        return FALSE;
    }
    /* PCI: device in 31:16, function in 15:0. */
    if ((address >> 16) > 0x1FUL || (address & 0xFFFFUL) > 7UL) {
        return FALSE;
    }
    *bdf = XHCI_TBT_BDF(bus, address >> 16, address & 0xFFFFUL);
    return TRUE;
}

VOID CmRegPublish(PCM_FILTER filter)
{
    ULONG bdf;
    KIRQL irql;

    if (!cmLocation(filter->Common.Pdo, &bdf)) {
        return;
    }
    KeAcquireSpinLock(&cmRegLock, &irql);
    if (!filter->Published) {
        filter->Bdf = bdf;
        filter->Gone = 0;
        filter->Baseline = 0;
        filter->InFlight = 0;
        filter->Held = 0;
        filter->HeldBy = NULL;
        cmRegSerial++;
        filter->Serial = cmRegSerial;
        KeClearEvent(&filter->Rundown);
        InsertTailList(&cmRegFilters, &filter->RegLink);
        filter->Published = 1;
        cmRegRecompute();
    }
    KeReleaseSpinLock(&cmRegLock, irql);
}

/*
 * Releases a held filter's retained D0 IRP. Under the lock: returns the
 * IRP for the caller to complete after the lock is dropped (PoStartNext-
 * PowerIrp, then the completion), or NULL. The hold's reference passes to
 * the caller when the timer is not armed or was cancelled (*dropRef TRUE:
 * the caller lowers InFlight after its last use of the filter); otherwise
 * the DPC, already queued, lowers it.
 */
static PIRP cmHoldRelease(PCM_FILTER f, BOOLEAN *dropRef)
{
    PIRP irp;

    *dropRef = FALSE;
    f->Held = 0;
    f->HeldBy = NULL;
    irp = f->HeldIrp;
    if (irp == NULL) {
        return NULL;
    }
    f->HeldIrp = NULL;
    if (f->TimerArmed) {
        if (KeCancelTimer(&f->HoldTimer)) {
            f->TimerArmed = 0;
            *dropRef = TRUE;
        }
    } else {
        *dropRef = TRUE;
    }
    return irp;
}

static VOID cmHoldComplete(PIRP irp)
{
    PoStartNextPowerIrp(irp);
    IoCompleteRequest(irp, IO_NO_INCREMENT);
}

VOID CmRegWithdraw(PCM_FILTER filter)
{
    KIRQL irql;
    PIRP irp;
    BOOLEAN dropRef;
    ULONG wait;

    irp = NULL;
    dropRef = FALSE;
    KeAcquireSpinLock(&cmRegLock, &irql);
    if (!filter->Published) {
        KeReleaseSpinLock(&cmRegLock, irql);
        return;
    }
    filter->Gone = 1;
    filter->Published = 0;
    filter->Baseline = 0;
    RemoveEntryList(&filter->RegLink);
    InitializeListHead(&filter->RegLink);
    irp = cmHoldRelease(filter, &dropRef);
    if (dropRef) {
        cmRegDrop(filter);
    }
    wait = filter->InFlight;
    cmRegRecompute();
    KeReleaseSpinLock(&cmRegLock, irql);
    if (irp != NULL) {
        cmHoldComplete(irp);
    }
    if (wait != 0) {
        (VOID)KeWaitForSingleObject(&filter->Rundown, Executive, KernelMode,
                                    FALSE, NULL);
    }
}

/* ---- the snapshot and the child filters ------------------------------- */

ULONG CmRegRelationsBegin(PCM_FILTER filter)
{
    KIRQL irql;
    ULONG seq;

    KeAcquireSpinLock(&cmRegLock, &irql);
    seq = filter->ChangeSeq;
    KeReleaseSpinLock(&cmRegLock, irql);
    return seq;
}

VOID CmRegRelations(PCM_FILTER filter, const DEVICE_RELATIONS *relations,
                    ULONG seq)
{
    PVOID now[CM_SNAP_MAX];
    ULONG isNew[CM_SNAP_MAX];
    ULONG count;
    ULONG fresh;
    ULONG i;
    ULONG attach;
    KIRQL irql;
    NTSTATUS status;

    count = 0;
    if (relations != NULL) {
        count = relations->Count;
    }
    if (count > CM_SNAP_MAX) {
        /* A full snapshot: nothing new is marked from this answer, and the
         * snapshot keeps the first CM_SNAP_MAX. */
        KeAcquireSpinLock(&cmRegLock, &irql);
        cmRegSkipped += count - CM_SNAP_MAX;
        KeReleaseSpinLock(&cmRegLock, irql);
        count = CM_SNAP_MAX;
    }
    for (i = 0; i < count; i++) {
        now[i] = (PVOID)relations->Objects[i];
    }
    fresh = CmPortsNewPdos((PVOID const *)filter->Snap, filter->SnapCount,
                           (PVOID const *)now, count, isNew);

    /* Whether this answer is one an active bridge with safe eject sees:
     * read before the baseline below can make the bridge active. */
    KeAcquireSpinLock(&cmRegLock, &irql);
    attach = filter->Published && !filter->Gone && filter->Active &&
             filter->SafeEject != 0;
    KeReleaseSpinLock(&cmRegLock, irql);

    for (i = 0; i < count; i++) {
        if (!isNew[i] || !attach) {
            continue;
        }
        status = cmChildCreate(filter, (PDEVICE_OBJECT)now[i]);
        KeAcquireSpinLock(&cmRegLock, &irql);
        if (NT_SUCCESS(status)) {
            cmRegAttached++;
        } else if (status == STATUS_DEVICE_BUSY) {
            cmRegSkipped++;
        } else {
            cmRegRefused++;
        }
        KeReleaseSpinLock(&cmRegLock, irql);
    }

    /* The snapshot is replaced under the lock, since the eject's
     * eligibility (cmEjectReady) reads it from the child's removal. A
     * changed population starts a new generation (RelGen), which an eject
     * posted against the old one fails at its hand-off; a new function
     * forgets an orderly removal seen before it arrived (EjectSeen), so a
     * replacement is never torn down for its predecessor's eject. The
     * answer settles the bridge after a tunnel change only if the query
     * began after that change (`seq`, CmRegRelationsBegin); an answer that
     * overlapped one leaves it unsettled for the next query, which the
     * change's own invalidation brings. */
    KeAcquireSpinLock(&cmRegLock, &irql);
    if (fresh != 0 || count != filter->SnapCount) {
        filter->RelGen++;
    }
    if (fresh != 0) {
        filter->EjectSeen = 0;
    }
    if (seq == filter->ChangeSeq) {
        filter->Unsettled = 0;
    }
    for (i = 0; i < count; i++) {
        filter->Snap[i] = now[i];
    }
    filter->SnapCount = count;
    filter->SnapTruncated = (ULONG)(relations != NULL &&
                                    relations->Count > CM_SNAP_MAX);
    if (filter->Published && !filter->Gone && !filter->Baseline) {
        filter->Baseline = 1;
        cmRegRecompute();
    }
    KeReleaseSpinLock(&cmRegLock, irql);
}

/*
 * A child filter directly above a PDO pci.sys newly reported on an active
 * bridge, before the PnP manager builds that device's stack (section 7.4
 * step 1). STATUS_DEVICE_BUSY: something is already attached to the PDO
 * (not a new device's stack), so nothing is attached. PASSIVE_LEVEL.
 */
static NTSTATUS cmChildCreate(PCM_FILTER bridge, PDEVICE_OBJECT pdo)
{
    PDEVICE_OBJECT self;
    PDEVICE_OBJECT lower;
    PCM_FILTER child;
    NTSTATUS status;

    if (pdo->AttachedDevice != NULL) {
        return STATUS_DEVICE_BUSY;
    }
    status = IoCreateDevice(CmDriverObject, sizeof(CM_FILTER), NULL,
                            FILE_DEVICE_UNKNOWN, 0, FALSE, &self);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    child = (PCM_FILTER)self->DeviceExtension;
    CmZero(child, sizeof(CM_FILTER));
    child->Common.Kind = CM_KIND_CHILD;
    child->Common.Self = self;
    child->Common.Pdo = pdo;
    child->Outstanding = 1;
    child->Bridge = bridge;

    KeInitializeSpinLock(&child->Lock);
    KeInitializeEvent(&child->Drained, NotificationEvent, FALSE);
    CmRegFilterInit(child);
    self->DeviceType = pdo->DeviceType;
    self->Characteristics = pdo->Characteristics;
    self->Flags |= pdo->Flags & DO_POWER_PAGABLE;
    lower = IoAttachDeviceToDeviceStack(self, pdo);
    if (lower == NULL) {
        IoDeleteDevice(self);
        return STATUS_NO_SUCH_DEVICE;
    }
    child->Common.Lower = lower;
    self->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

/*
 * Whether every function behind a bridge is without a device stack: each
 * PDO its last BusRelations reported has nothing attached (an eject, a
 * disable or no driver removed or never built its stack), and that answer
 * was not truncated. Whatever put a stack back - a re-enable, a driver
 * installed - shows as an attached device, so no state is remembered per
 * PDO. The snapshot's PDOs are those pci.sys still reports, so they are
 * not yet deleted. Under the lock.
 */
static BOOLEAN cmEjectReady(PCM_FILTER bridge)
{
    ULONG i;

    if (!bridge->Published || bridge->Gone || bridge->SnapTruncated) {
        return FALSE;
    }
    for (i = 0; i < bridge->SnapCount; i++) {
        if (((PDEVICE_OBJECT)bridge->Snap[i])->AttachedDevice != NULL) {
            return FALSE;
        }
    }
    return TRUE;
}

/*
 * A child filter's device has been removed: its REMOVE_DEVICE completed
 * below and the filter detached from the PDO. `ejected` when that removal
 * followed a query-remove, not a surprise removal. Once no function behind
 * the port has a stack (cmEjectReady), a port on which an orderly removal
 * was seen is posted to the owning domain with the bridge's population
 * generation, so record 22's per-port entry runs only after every
 * function behind the port has finished its removal, and never against a
 * population that changed since (section 10.4). PASSIVE_LEVEL.
 */
VOID CmRegChildRemoved(PCM_FILTER child, BOOLEAN ejected)
{
    KIRQL irql;
    PCM_FILTER bridge;
    PCM_NHI d;
    ULONG i;

    bridge = child->Bridge;
    if (bridge == NULL) {
        return;
    }
    KeAcquireSpinLock(&cmRegLock, &irql);
    /* An orderly removal counts only for a function its bridge still
     * reports: one already reported missing belongs to an old connection.
     * A function that arrives later forgets it (CmRegRelations). */
    if (ejected) {
        for (i = 0; i < bridge->SnapCount; i++) {
            if (bridge->Snap[i] == (PVOID)child->Common.Pdo) {
                bridge->EjectSeen = 1;
            }
        }
    }
    d = bridge->Owner;
    if (bridge->EjectSeen && cmEjectReady(bridge) && d != NULL &&
        bridge->OwnerIndex < d->Reenum.Members) {
        bridge->EjectSeen = 0;
        d->Reenum.EjectPending |= 1UL << bridge->OwnerIndex;
        d->Reenum.Port[bridge->OwnerIndex].Flags |= CM_PORT_EJECTED;
        d->Reenum.Port[bridge->OwnerIndex].EjectSerial = bridge->Serial;
        d->Reenum.Port[bridge->OwnerIndex].EjectGen = bridge->RelGen;
    }
    KeReleaseSpinLock(&cmRegLock, irql);
}

/* ---- the resume hold -------------------------------------------------- */

/* One second, relative, in 100 ns units: -10,000,000 as two halves (no
 * 64-bit arithmetic). */
static VOID cmHoldArm(PCM_FILTER f)
{
    LARGE_INTEGER due;

    due.LowPart = 0xFF676980UL;
    due.HighPart = -1;
    f->TimerArmed = 1;
    (VOID)KeSetTimer(&f->HoldTimer, due, &f->HoldDpc);
}

/*
 * The watchdog (section 6.3): one tick a second while a D0 IRP is
 * retained; at the bound's end the IRP is released by the failure path,
 * never taken for success, and the bridge invalidated so the functions
 * that did not come back are reported missing. <= DISPATCH_LEVEL.
 */
static VOID cmHoldDpc(PKDPC dpc, PVOID context, PVOID arg1, PVOID arg2)
{
    PCM_FILTER f;
    PIRP irp;
    PDEVICE_OBJECT pdo;
    ULONG invalidate;
    KIRQL irql;

    UNREFERENCED_PARAMETER(dpc);
    UNREFERENCED_PARAMETER(arg1);
    UNREFERENCED_PARAMETER(arg2);
    f = (PCM_FILTER)context;
    irp = NULL;
    KeAcquireSpinLock(&cmRegLock, &irql);
    f->TimerArmed = 0;
    if (f->HeldIrp == NULL) {
        /* Released while this DPC was queued: the hold's reference is
         * this DPC's to drop. */
        cmRegDrop(f);
        KeReleaseSpinLock(&cmRegLock, irql);
        return;
    }
    if (f->HoldLeftS > 1) {
        f->HoldLeftS--;
        cmHoldArm(f);
        KeReleaseSpinLock(&cmRegLock, irql);
        return;
    }
    irp = f->HeldIrp;
    f->HeldIrp = NULL;
    f->Held = 0;
    f->HeldBy = NULL;
    invalidate = f->HoldReenum;
    pdo = f->Common.Pdo;
    cmRegWatchdogs++;
    KeReleaseSpinLock(&cmRegLock, irql);
    cmHoldComplete(irp);
    if (invalidate) {
        IoInvalidateDeviceRelations(pdo, BusRelations);
    }
    KeAcquireSpinLock(&cmRegLock, &irql);
    cmRegDrop(f);
    KeReleaseSpinLock(&cmRegLock, irql);
}

/* The D0 IRP's completion on a held bridge: retained while the hold
 * stands, after the bus driver below has finished with it.
 * <= DISPATCH_LEVEL. */
static NTSTATUS cmHoldDone(PDEVICE_OBJECT device, PIRP irp, PVOID context)
{
    PCM_FILTER f;
    KIRQL irql;

    UNREFERENCED_PARAMETER(device);
    f = (PCM_FILTER)context;
    KeAcquireSpinLock(&cmRegLock, &irql);
    if (f->Held && !f->Gone && NT_SUCCESS(irp->IoStatus.Status)) {
        f->HeldIrp = irp;
        cmHoldArm(f);
        KeReleaseSpinLock(&cmRegLock, irql);
        return STATUS_MORE_PROCESSING_REQUIRED;
    }
    cmRegDrop(f);
    KeReleaseSpinLock(&cmRegLock, irql);
    PoStartNextPowerIrp(irp);
    return STATUS_SUCCESS;
}

BOOLEAN CmRegHoldD0(PCM_FILTER filter, PIRP irp)
{
    KIRQL irql;
    BOOLEAN hold;

    hold = FALSE;
    KeAcquireSpinLock(&cmRegLock, &irql);
    if (filter->Published && !filter->Gone && filter->Held &&
        filter->HeldIrp == NULL && !filter->TimerArmed) {
        filter->InFlight++;
        hold = TRUE;
    }
    KeReleaseSpinLock(&cmRegLock, irql);
    if (!hold) {
        return FALSE;
    }
    IoMarkIrpPending(irp);
    IoCopyCurrentIrpStackLocationToNext(irp);
    CM_SET_COMPLETION_ALWAYS(irp, cmHoldDone, filter);
    (VOID)PoCallDriver(filter->Common.Lower, irp);
    return TRUE;
}

/* ====================================================================== */
/* The domain                                                             */
/* ====================================================================== */

VOID CmReenumSetEject(PCM_NHI nhi, CM_REENUM_EJECT hook, PVOID context)
{
    nhi->Reenum.EjectHook = hook;
    nhi->Reenum.EjectContext = context;
}

ULONG CmReenumPorts(PCM_NHI nhi, PULONG ports, ULONG capacity)
{
    KIRQL irql;
    ULONG i;
    ULONG n;

    n = 0;
    KeAcquireSpinLock(&cmRegLock, &irql);
    for (i = 0; i < nhi->Reenum.Members && n < capacity; i++) {
        if (nhi->Reenum.Port[i].Flags & CM_PORT_ACTIVE) {
            ports[n] = nhi->Reenum.Port[i].Bdf;
            n++;
        }
    }
    KeReleaseSpinLock(&cmRegLock, irql);
    return n;
}

/* A member's flags, read under the lock. */
static ULONG cmFlags(PCM_NHI nhi, ULONG i)
{
    KIRQL irql;
    ULONG flags;

    KeAcquireSpinLock(&cmRegLock, &irql);
    flags = nhi->Reenum.Port[i].Flags;
    KeReleaseSpinLock(&cmRegLock, irql);
    return flags;
}

/*
 * The NHI's own location: its PDO's bus number and address; where those
 * cannot be read, the one function on any bus that carries the NHI's
 * vendor and device ids (none, or two, is no location). PASSIVE_LEVEL.
 */
static ULONG cmNhiLocation(PCM_NHI nhi)
{
    ULONG bdf;
    ULONG id;
    ULONG want;
    ULONG bus;
    ULONG dev;
    ULONG fn;
    ULONG hdr;
    ULONG found;
    ULONG at;

    if (cmLocation(nhi->Common.Pdo, &bdf)) {
        return bdf;
    }
    want = CM_PORTS_ID(nhi->Vendor, nhi->Device);
    found = 0;
    at = XHCI_TBT_NO_BDF;
    for (bus = 0; bus < 256UL; bus++) {
        for (dev = 0; dev < 32UL; dev++) {
            for (fn = 0; fn < 8UL; fn++) {
                if (!cmCfgRead(NULL, XHCI_TBT_BDF(bus, dev, fn), 0, &id)) {
                    /* The HAL knows no such bus. */
                    dev = 32UL;
                    break;
                }
                if (id == 0xFFFFFFFFUL) {
                    if (fn == 0) {
                        break;
                    }
                    continue;
                }
                if (id == want) {
                    found++;
                    at = XHCI_TBT_BDF(bus, dev, fn);
                }
                if (fn == 0) {
                    if (!cmCfgRead(NULL, XHCI_TBT_BDF(bus, dev, 0),
                                   XHCI_TBT_CFG_HEADER, &hdr) ||
                        ((hdr >> 16) & XHCI_TBT_HDR_MF) == 0) {
                        break;
                    }
                }
            }
        }
    }
    return found == 1 ? at : XHCI_TBT_NO_BDF;
}

/* ---- Windows 98: CONFIGMG --------------------------------------------- */

/*
 * The 98 path of the function at `bdf` (cm_ports.h CmPortsIdPath): its
 * devfn, then each parent bridge's up to the root bus, then the root bus
 * number. A parent is the type 1 function whose secondary bus is the
 * child's bus, found by reading every lower bus's functions; a bus with no
 * such parent is a root bus. FALSE where the path outgrows its room.
 * PASSIVE_LEVEL.
 */
static BOOLEAN cmPciPath(ULONG bdf, PULONG path, PULONG count)
{
    ULONG n;
    ULONG cur;
    ULONG bus;
    ULONG b;
    ULONG d;
    ULONG f;
    ULONG id;
    ULONG hdr;
    ULONG buses;
    ULONG parent;
    ULONG incomplete;

    n = 0;
    cur = bdf;
    while (n < CM_PORTS_PATH_MAX - 1) {
        path[n++] = cur & 0xFFUL;
        bus = XHCI_TBT_BDF_BUS(cur);
        if (bus == 0) {
            path[n++] = 0;
            *count = n;
            return TRUE;
        }
        parent = XHCI_TBT_NO_BDF;
        incomplete = 0;
        for (b = 0; b < bus && parent == XHCI_TBT_NO_BDF; b++) {
            for (d = 0; d < 32UL && parent == XHCI_TBT_NO_BDF; d++) {
                for (f = 0; f < 8UL; f++) {
                    if (!cmCfgRead(NULL, XHCI_TBT_BDF(b, d, f), 0, &id)) {
                        /* A bus the HAL does not know is no parent's;
                         * a bus 0 that cannot be read leaves it open. */
                        if (b == 0) {
                            incomplete = 1;
                        }
                        d = 32UL;
                        break;
                    }
                    if (id == 0xFFFFFFFFUL) {
                        if (f == 0) {
                            break;
                        }
                        continue;
                    }
                    if (!cmCfgRead(NULL, XHCI_TBT_BDF(b, d, f),
                                   XHCI_TBT_CFG_HEADER, &hdr)) {
                        incomplete = 1;
                        break;
                    }
                    if (XHCI_TBT_HDR_LAYOUT(hdr >> 16) ==
                            XHCI_TBT_HDR_BRIDGE &&
                        cmCfgRead(NULL, XHCI_TBT_BDF(b, d, f),
                                  XHCI_TBT_CFG_BUSES, &buses) &&
                        ((buses >> 8) & 0xFFUL) == bus) {
                        parent = XHCI_TBT_BDF(b, d, f);
                        break;
                    }
                    if (f == 0 && ((hdr >> 16) & XHCI_TBT_HDR_MF) == 0) {
                        break;
                    }
                }
            }
        }
        if (parent == XHCI_TBT_NO_BDF) {
            /* No bridge leads to this bus: a root bus of its own (a host
             * with more than one), whose number ends the path as 00 ends
             * one under bus 0 - but only after a scan that read every
             * function it met; otherwise the path is not known. */
            if (incomplete) {
                return FALSE;
            }
            path[n++] = bus;
            *count = n;
            return TRUE;
        }
        cur = parent;
    }
    return FALSE;
}

/* Whether a 98 device id names the bridge at `bdf`, in either instance
 * form, its expected path given. */
static BOOLEAN cmIdNames(const char *id, ULONG bdf, const ULONG *want,
                         ULONG wantCount)
{
    ULONG at;
    ULONG path[CM_PORTS_PATH_MAX];
    ULONG n;
    ULONG i;

    if (CmPortsIdLocation(id, CMG_MAX_DEVICE_ID_LEN, &at)) {
        return (BOOLEAN)(at == bdf);
    }
    if (wantCount == 0 ||
        !CmPortsIdPath(id, CMG_MAX_DEVICE_ID_LEN, path, CM_PORTS_PATH_MAX,
                       &n) ||
        n != wantCount) {
        return FALSE;
    }
    for (i = 0; i < n; i++) {
        if (path[i] != want[i]) {
            return FALSE;
        }
    }
    return TRUE;
}

/*
 * The walk's diagnostic, once per member: the instance part of a vendor
 * 8086 PCI device id whose location did not parse, as up to three words of
 * four characters, so a reading shows what form Windows 98 gave it.
 */
static VOID cmLogInstance(PCM_NHI nhi, const char *id)
{
    ULONG i;
    ULONG k;
    ULONG w;
    ULONG n;
    ULONG id32;

    if (!CmPortsIdVenDev(id, CMG_MAX_DEVICE_ID_LEN, &id32) ||
        (id32 & 0xFFFFUL) != 0x8086UL) {
        return;
    }
    for (i = 4; i < CMG_MAX_DEVICE_ID_LEN && id[i] != 0 && id[i] != '\\';
         i++) {
    }
    if (i >= CMG_MAX_DEVICE_ID_LEN || id[i] == 0) {
        return;
    }
    i++;
    CmLogNote(nhi, "cm.reenum.9x.walk.id", id32);
    for (k = 0; k < 3 && id[i] != 0; k++) {
        w = 0;
        for (n = 0; n < 4 && id[i] != 0; n++) {
            w = (w << 8) | (ULONG)(UCHAR)id[i];
            i++;
        }
        CmLogNote(nhi, "cm.reenum.9x.walk.inst", w);
    }
}

/*
 * The devnode of the bridge at `bdf`: the cached one while its id still
 * names that location, else a walk of the tree from the root. 0 for none.
 */
static ULONG cmFindDevnode(PCM_NHI nhi, ULONG index)
{
    PCM_REENUM r;
    ULONG stack[CM_WALK_STACK];
    ULONG depth;
    ULONG nodes;
    ULONG node;
    ULONG next;
    ULONG at;
    ULONG pci;
    ULONG dropped;
    ULONG cr;
    PULONG want;
    ULONG wantCount;
    char id[CMG_MAX_DEVICE_ID_LEN + 1];

    r = &nhi->Reenum;
    /* The member's expected path, read once (its parents do not move). */
    if (!r->Port[index].PathRead) {
        /* Cached once read; a failed read is tried again at the next walk
         * (the callers' retries are themselves bounded). */
        if (cmPciPath(r->Port[index].Bdf, r->Port[index].Path,
                      &r->Port[index].PathCount)) {
            r->Port[index].PathRead = 1;
        } else {
            r->Port[index].PathCount = 0;
        }
    }
    want = r->Port[index].Path;
    wantCount = r->Port[index].PathCount;
    if (r->Port[index].Devnode != 0 &&
        CmCfgmgDeviceId(&r->Cfgmg, r->Port[index].Devnode, id,
                        sizeof(id)) == CMG_CR_SUCCESS &&
        cmIdNames(id, r->Port[index].Bdf, want, wantCount)) {
        return r->Port[index].Devnode;
    }
    r->Port[index].Devnode = 0;
    depth = 0;
    nodes = 0;
    pci = 0;
    dropped = 0;
    stack[depth++] = r->Cfgmg.Root;
    while (depth > 0 && nodes < CM_WALK_NODES) {
        node = stack[--depth];
        nodes++;
        cr = CmCfgmgDeviceId(&r->Cfgmg, node, id, sizeof(id));
        if (cr == CMG_CR_SUCCESS &&
            cmIdNames(id, r->Port[index].Bdf, want, wantCount)) {
            r->Port[index].Devnode = node;
            return node;
        }
        if (cr == CMG_CR_SUCCESS && CmPortsIdLocation(id, sizeof(id), &at)) {
            pci++;
        } else if (cr == CMG_CR_SUCCESS && !r->Port[index].WalkLogged) {
            cmLogInstance(nhi, id);
        }
        if (CmCfgmgChild(&r->Cfgmg, node, &next) != CMG_CR_SUCCESS) {
            continue;
        }
        while (next != 0) {
            if (depth == CM_WALK_STACK) {
                dropped++;
                break;
            }
            stack[depth++] = next;
            if (CmCfgmgSibling(&r->Cfgmg, next, &next) != CMG_CR_SUCCESS) {
                break;
            }
        }
    }
    /* Nothing matched: the path is read again at the next walk, in case
     * the configuration space it came from was not yet as it is. */
    r->Port[index].PathRead = 0;
    /* Why the walk did not find it, once per member: devnodes visited,
     * PCI locations parsed, and siblings the stack could not hold. */
    if (!r->Port[index].WalkLogged) {
        r->Port[index].WalkLogged = 1;
        CmLogNote(nhi, "cm.reenum.9x.walk.nodes", nodes);
        CmLogNote(nhi, "cm.reenum.9x.walk.pci", pci);
        CmLogNote(nhi, "cm.reenum.9x.walk.dropped", dropped);
        CmLogNote(nhi, "cm.reenum.9x.walk.path-len", wantCount);
    }
    return 0;
}

/* The children of `devnode`, up to `room`, into `out`. */
static ULONG cmChildren(PCM_NHI nhi, ULONG devnode, PULONG out, ULONG room)
{
    ULONG n;
    ULONG node;

    n = 0;
    if (CmCfgmgChild(&nhi->Reenum.Cfgmg, devnode, &node) != CMG_CR_SUCCESS) {
        return 0;
    }
    while (node != 0 && n < room) {
        out[n++] = node;
        if (CmCfgmgSibling(&nhi->Reenum.Cfgmg, node, &node) !=
            CMG_CR_SUCCESS) {
            break;
        }
    }
    return n;
}

/* The ids of every function answering on the bridge's secondary bus, up
 * to `room`; returns how many, or room + 1 when the bus could not be read
 * (nothing is then taken for vanished). */
static ULONG cmBusIds(ULONG bridge, PULONG ids, ULONG room)
{
    ULONG buses;
    ULONG bus;
    ULONG dev;
    ULONG fn;
    ULONG id;
    ULONG hdr;
    ULONG n;

    if (!cmCfgRead(NULL, bridge, XHCI_TBT_CFG_BUSES, &buses) ||
        buses == 0xFFFFFFFFUL) {
        return room + 1;
    }
    bus = (buses >> 8) & 0xFFUL;
    if (bus == 0) {
        return room + 1;
    }
    n = 0;
    for (dev = 0; dev < 32UL; dev++) {
        for (fn = 0; fn < 8UL; fn++) {
            if (!cmCfgRead(NULL, XHCI_TBT_BDF(bus, dev, fn), 0, &id)) {
                return room + 1;
            }
            if (id == 0xFFFFFFFFUL || (id & 0xFFFFUL) == 0xFFFFUL) {
                if (fn == 0) {
                    break;
                }
                continue;
            }
            if (n == room) {
                return room + 1;
            }
            ids[n++] = id;
            if (fn == 0) {
                /* A header that cannot be read leaves functions 1 to 7
                 * unknown: the inventory is incomplete, not single. */
                if (!cmCfgRead(NULL, XHCI_TBT_BDF(bus, dev, 0),
                               XHCI_TBT_CFG_HEADER, &hdr) ||
                    hdr == 0xFFFFFFFFUL) {
                    return room + 1;
                }
                if (((hdr >> 16) & XHCI_TBT_HDR_MF) == 0) {
                    break;
                }
            }
        }
    }
    return n;
}

/*
 * Section 6.1 on one member: the vanished children removed (tunnel down
 * and RESYNC), then Reenumerate_DevNode. TRUE when that succeeded.
 */
static BOOLEAN cmCfgmgMember(PCM_NHI nhi, ULONG i, BOOLEAN removeVanished)
{
    PCM_REENUM r;
    ULONG bridge;
    ULONG children[CM_REENUM_MAX_KNOWN];
    ULONG ids[CM_BUS_FUNCTIONS];
    ULONG count;
    ULONG idCount;
    ULONG c;
    ULONG k;
    ULONG id;
    ULONG answers;
    ULONG cr;
    char text[CMG_MAX_DEVICE_ID_LEN + 1];

    r = &nhi->Reenum;
    bridge = cmFindDevnode(nhi, i);
    if (bridge == 0) {
        /* Owed (Missed) and retried by CmReenumService no faster than
         * CM_MISSED_RETRY_MS; logged once per member. */
        r->CallFailures++;
        if (!r->Port[i].WalkLogged) {
            CmLogNote(nhi, "cm.reenum.9x.no-devnode", r->Port[i].Bdf);
        }
        return FALSE;
    }
    if (removeVanished) {
        idCount = cmBusIds(r->Port[i].Bdf, ids, CM_BUS_FUNCTIONS);
        count = idCount <= CM_BUS_FUNCTIONS
                    ? cmChildren(nhi, bridge, children, CM_REENUM_MAX_KNOWN)
                    : 0;
        for (c = 0; c < count; c++) {
            if (CmCfgmgDeviceId(&r->Cfgmg, children[c], text,
                                sizeof(text)) != CMG_CR_SUCCESS ||
                !CmPortsIdVenDev(text, sizeof(text), &id)) {
                continue;
            }
            answers = 0;
            for (k = 0; k < idCount; k++) {
                if (ids[k] == id) {
                    answers = 1;
                }
            }
            if (answers) {
                continue;
            }
            cr = CmCfgmgQueryRemove(&r->Cfgmg, children[c]);
            CmLogNote(nhi, "cm.reenum.9x.query-remove", cr);
            if (cr != CMG_CR_SUCCESS) {
                /* A refusal is respected (a driver with open files, the
                 * usb4-emu handoff's rule): no Remove_SubTree, nothing
                 * forced, the devnode left as it is and the refusal
                 * logged and counted. The function is physically gone, so
                 * its driver meets an absent device; the re-enumeration
                 * below only asks PCI.VXD to read the bus again, and what
                 * the configuration manager does with a refused, vanished
                 * devnode is its own decision, not a removal made here. */
                r->Vetoed++;
                continue;
            }
            cr = CmCfgmgRemove(&r->Cfgmg, children[c]);
            CmLogNote(nhi, "cm.reenum.9x.remove", cr);
            if (cr == CMG_CR_SUCCESS) {
                r->Removed++;
            }
        }
    }
    cr = CmCfgmgReenumerate(&r->Cfgmg, bridge);
    r->Port[i].LastStatus = cr;
    r->Port[i].Reenums++;
    r->Calls++;
    if (cr != CMG_CR_SUCCESS) {
        r->CallFailures++;
    }
    CmLogNote(nhi, "cm.reenum.9x.reenumerate", (r->Port[i].Bdf << 8) | cr);
    return (BOOLEAN)(cr == CMG_CR_SUCCESS);
}

/*
 * Safe eject's 9x half (section 7.4) on one member: each child not seen
 * before gets DN_REMOVABLE; `mark` FALSE only records the children (the
 * start's baseline: a device tunnelled before is not marked).
 */
static VOID cmCfgmgMark(PCM_NHI nhi, ULONG i, BOOLEAN mark)
{
    PCM_REENUM r;
    ULONG bridge;
    ULONG children[CM_REENUM_MAX_KNOWN];
    ULONG count;
    ULONG c;
    ULONG k;
    ULONG known;
    ULONG status;
    ULONG problem;
    ULONG cr;

    r = &nhi->Reenum;
    bridge = cmFindDevnode(nhi, i);
    if (bridge == 0) {
        return;
    }
    count = cmChildren(nhi, bridge, children, CM_REENUM_MAX_KNOWN);
    for (c = 0; c < count && mark; c++) {
        known = 0;
        for (k = 0; k < r->KnownCount[i]; k++) {
            if (r->Known[i][k] == children[c]) {
                known = 1;
            }
        }
        if (known) {
            continue;
        }
        cr = CmCfgmgStatus(&r->Cfgmg, children[c], &status, &problem);
        if (cr == CMG_CR_SUCCESS && (status & CMG_DN_REMOVABLE) != 0) {
            continue;
        }
        cr = CmCfgmgAddRemovable(&r->Cfgmg, children[c]);
        CmLogNote(nhi, "cm.reenum.9x.removable", cr);
        if (cr == CMG_CR_SUCCESS) {
            r->Marked++;
            r->Port[i].Marked++;
        } else {
            r->MarkFailures++;
        }
    }
    for (c = 0; c < count; c++) {
        r->Known[i][c] = children[c];
    }
    r->KnownCount[i] = count;
}

/* ---- NT: the filters -------------------------------------------------- */

/* IoInvalidateDeviceRelations on member i through its filter: TRUE when
 * the call was made. */
static BOOLEAN cmInvalidate(PCM_NHI nhi, ULONG i, BOOLEAN activeOnly)
{
    PCM_FILTER f;
    PDEVICE_OBJECT pdo;

    if (!cmRegTake(nhi, i, activeOnly, &f, &pdo)) {
        return FALSE;
    }
    IoInvalidateDeviceRelations(pdo, BusRelations);
    cmRegRelease(f);
    nhi->Reenum.Port[i].Reenums++;
    nhi->Reenum.Port[i].LastStatus = STATUS_SUCCESS;
    nhi->Reenum.Calls++;
    return TRUE;
}

/*
 * Members owned by this domain whose filter has published and not yet
 * taken its baseline are asked for one (section 7.4 step 1), once per
 * publication.
 */
static VOID cmAskBaselines(PCM_NHI nhi)
{
    PCM_REENUM r;
    KIRQL irql;
    PCM_FILTER f;
    ULONG i;
    ULONG serial;

    r = &nhi->Reenum;
    for (i = 0; i < r->Members; i++) {
        serial = 0;
        KeAcquireSpinLock(&cmRegLock, &irql);
        f = cmRegFind(r->Port[i].Bdf);
        if (f != NULL && f->Owner == nhi && !f->Baseline &&
            f->Serial != r->Port[i].AskedSerial) {
            serial = f->Serial;
        }
        KeReleaseSpinLock(&cmRegLock, irql);
        if (serial != 0 && cmInvalidate(nhi, i, FALSE)) {
            /* Recorded once asked, so a failed call is asked again. */
            r->Port[i].AskedSerial = serial;
            CmLogNote(nhi, "cm.reenum.baseline-asked", r->Port[i].Bdf);
        }
    }
}

/* ---- NT: a configuration write to a member's bridge ------------------- */

static NTSTATUS NTAPI cmCfgWriteDone(PDEVICE_OBJECT device, PIRP irp,
                                     PVOID context)
{
    UNREFERENCED_PARAMETER(device);
    UNREFERENCED_PARAMETER(irp);
    (VOID)KeSetEvent((PKEVENT)context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

BOOLEAN CmRegConfigWrite(PCM_NHI nhi, ULONG bdf, ULONG offset, ULONG width,
                         ULONG value)
{
    PCM_FILTER f;
    PDEVICE_OBJECT pdo;
    PIO_STACK_LOCATION next;
    KEVENT done;
    PIRP irp;
    UCHAR b[4];
    ULONG i;
    ULONG index;
    NTSTATUS status;

    if (width != 1 && width != 2 && width != 4) {
        return FALSE;
    }
    if (offset < 0x40UL) {
        /* A bridge's header is pci.sys's; only its capabilities here. */
        return FALSE;
    }
    for (index = 0; index < nhi->Reenum.Members; index++) {
        if (nhi->Reenum.Port[index].Bdf == bdf) {
            break;
        }
    }
    if (index == nhi->Reenum.Members ||
        !cmRegTake(nhi, index, FALSE, &f, &pdo)) {
        return FALSE;
    }
    for (i = 0; i < width; i++) {
        b[i] = (UCHAR)((value >> (8 * i)) & 0xFFUL);
    }
    status = STATUS_INSUFFICIENT_RESOURCES;
    irp = IoAllocateIrp(pdo->StackSize, FALSE);
    if (irp != NULL) {
        irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
        irp->IoStatus.Information = 0;
        next = IoGetNextIrpStackLocation(irp);
        next->MajorFunction = IRP_MJ_PNP;
        next->MinorFunction = IRP_MN_WRITE_CONFIG;
        next->Parameters.ReadWriteConfig.WhichSpace = PCI_WHICHSPACE_CONFIG;
        next->Parameters.ReadWriteConfig.Buffer = b;
        next->Parameters.ReadWriteConfig.Offset = offset;
        next->Parameters.ReadWriteConfig.Length = width;
        KeInitializeEvent(&done, NotificationEvent, FALSE);
        CM_SET_COMPLETION_ALWAYS(irp, cmCfgWriteDone, &done);
        status = IoCallDriver(pdo, irp);
        if (status == STATUS_PENDING) {
            (VOID)KeWaitForSingleObject(&done, Executive, KernelMode, FALSE,
                                        NULL);
        }
        status = irp->IoStatus.Status;
        IoFreeIrp(irp);
    }
    cmRegRelease(f);
    return (BOOLEAN)NT_SUCCESS(status);
}

/* ---- both routes ------------------------------------------------------ */
/* Every active member re-enumerated by the domain's route. */
static VOID cmReenumAll(PCM_NHI nhi, BOOLEAN removeVanished)
{
    PCM_REENUM r;
    ULONG i;
    ULONG flags;

    r = &nhi->Reenum;
    for (i = 0; i < r->Members; i++) {
        flags = cmFlags(nhi, i);
        if ((flags & CM_PORT_ACTIVE) == 0) {
            /* An exclusive member not yet active (NT: its filter has not
             * published or not taken its baseline) owes this change: it
             * is re-enumerated once it becomes active (CmReenumService). */
            if (flags & CM_PORT_EXCLUSIVE) {
                r->Port[i].Missed = 1;
            }
            continue;
        }
        /* nvme98's UP for what the call is about to expose (cm_nvlink.h,
         * obligation 1). */
        (VOID)CmNvlinkExpose(nhi, r->Port[i].Bdf);
        if (r->Route == CM_ROUTE_FILTER) {
            if (!cmInvalidate(nhi, i, TRUE)) {
                /* Lost its filter or its exclusivity since it was read
                 * active: owed when it is active again. */
                r->CallFailures++;
                r->Port[i].Missed = 1;
            }
        } else if (r->Route == CM_ROUTE_CONFIGMG) {
            if (!cmCfgmgMember(nhi, i, removeVanished)) {
                r->Port[i].Missed = 1;
            }
        }
    }
}

/*
 * The bounded poll of section 6.5 over the set. Returns the poll's end
 * (XHCI_TBT_POLL_*). For a tunnel up the ICM is serviced between the
 * readings and a disconnect of the device the action names abandons it.
 */
static ULONG cmPoll(PCM_NHI nhi, const TBT_ICM_ACTION *action, ULONG want)
{
    PCM_REENUM r;
    ULONG verdict;
    ULONG state;

    r = &nhi->Reenum;
    if (r->Topo.Tunnels == 0) {
        return XHCI_TBT_POLL_NO_READING;
    }
    XhciTbtSetArm(&r->Cfg, &r->Topo);
    state = XhciTbtPollBegin(&r->Poll, want, cmNow(nhi), 0);
    while (state == XHCI_TBT_POLL_RUNNING) {
        verdict = XhciTbtSetRead(&r->Cfg, &r->Topo, want);
        state = XhciTbtPollStep(&r->Poll, verdict, cmNow(nhi));
        if (state != XHCI_TBT_POLL_RUNNING) {
            break;
        }
        TbtIcmService(&nhi->Icm);
        if (want == XHCI_TBT_WANT_UP &&
            !TbtIcmActionLive(&nhi->Icm, action)) {
            state = XhciTbtPollAbandon(&r->Poll, cmNow(nhi));
            break;
        }
        cmSleep(nhi, XhciTbtPollWait(&r->Poll, cmNow(nhi)));
    }
    return state;
}

/* ====================================================================== */
/* The calls                                                              */
/* ====================================================================== */

static VOID cmDiscover(PCM_NHI nhi)
{
    PCM_REENUM r;
    XHCI_TBT_TOPO_IN in;
    const CM_FAMILY *family;
    ULONG tries;
    ULONG i;

    r = &nhi->Reenum;
    CmZero(&in, sizeof(in));
    in.Nhi = r->Nhi;
    if (nhi->Values.TunnelBridgeChars != 0) {
        in.Overrides = XhciTbtParseBdfList(nhi->Values.TunnelBridge,
                                           nhi->Values.TunnelBridgeChars,
                                           in.Override,
                                           XHCI_TBT_MAX_TUNNELS);
        if (in.Overrides == 0) {
            CmLogNote(nhi, "cm.reenum.override-unparsed",
                      nhi->Values.TunnelBridgeChars);
        }
    }
    family = CmPortsFamily(CM_PORTS_ID(nhi->Vendor, nhi->Device));
    if (family != NULL) {
        in.FamilyNhiIds = family->NhiIds;
        in.FamilyNhiIdCount = family->NhiIdCount;
        in.SwitchPortIds = family->SwitchPortIds;
        in.SwitchPortIdCount = family->SwitchPortIdCount;
        in.RootPortIds = family->RootPortIds;
        in.RootPortIdCount = family->RootPortIdCount;
    }
    for (tries = 0; tries < CM_FIND_TRIES; tries++) {
        if (XhciTbtTopoFind(&r->Cfg, &in, &r->Topo) !=
            XHCI_TBT_TOPO_INCOMPLETE) {
            break;
        }
        cmSleep(nhi, CM_FIND_RETRY_MS);
    }
    CmLogNote(nhi, "cm.reenum.topo", r->Topo.Outcome);
    CmLogNote(nhi, "cm.reenum.topo.parent", r->Topo.Parent);
    CmLogNote(nhi, "cm.reenum.topo.search-bus", r->Topo.SearchBus);
    CmLogNote(nhi, "cm.reenum.topo.siblings", r->Topo.Siblings);
    CmLogNote(nhi, "cm.reenum.topo.unread", r->Topo.Unread);
    CmLogNote(nhi, "cm.reenum.topo.candidates", r->Topo.Candidates);
    for (i = 0; i < r->Topo.Candidates && i < XHCI_TBT_MAX_CANDIDATES; i++) {
        CmLogNote(nhi, "cm.reenum.topo.candidate", r->Topo.Cand[i]);
    }
    r->Members = 0;
    for (i = 0; i < r->Topo.Tunnels && i < XHCI_TBT_MAX_TUNNELS; i++) {
        CmZero(&r->Port[i], sizeof(r->Port[i]));
        r->Port[i].Bdf = r->Topo.Set[i];
        r->Port[i].Source = r->Topo.Source[i];
        r->Members++;
        CmLogNote(nhi, "cm.reenum.member",
                  (r->Port[i].Bdf << 8) | r->Port[i].Source);
    }
}

VOID CmReenumStart(PCM_NHI nhi)
{
    PCM_REENUM r;
    ULONG i;

    r = &nhi->Reenum;
    r->Starts++;
    r->Route = CM_ROUTE_NONE;
    r->Why = CM_NOROUTE_NONE;
    r->Members = 0;
    r->EjectPending = 0;
    r->MarkOpen = 0;
    r->CfgmgOwed = 0;
    for (i = 0; i < XHCI_TBT_MAX_TUNNELS; i++) {
        r->KnownCount[i] = 0;
    }
    r->Reenum = nhi->Values.Value[CM_VAL_REENUM];
    r->SafeEject = nhi->Values.Value[CM_VAL_SAFE_EJECT];
    r->Cfg.Read = cmCfgRead;
    r->Cfg.Write = NULL;
    r->Cfg.Context = NULL;
    r->Cfg.Reads = 0;
    r->Cfg.Writes = 0;

    CmNvlinkStart(nhi);
    r->Nhi = cmNhiLocation(nhi);
    CmLogNote(nhi, "cm.reenum.nhi", r->Nhi);
    if (r->Nhi == XHCI_TBT_NO_BDF) {
        r->Why = CM_NOROUTE_NO_LOCATION;
        CmLogNote(nhi, "cm.reenum.no-route", r->Why);
        return;
    }
    r->RediscoverLeft = CM_REDISCOVER_TRIES;
    cmDiscoverAndList(nhi);
}

/*
 * The discovery, and with a set the domain's listing. An empty set is
 * tried again from CmReenumService (CM_REDISCOVER_TRIES, CM_REDISCOVER_MS
 * apart): on Windows 98 the NHI can start during the boot before the
 * configuration space behind its bridges reads back as it will (a 98 SE
 * leg found no candidate at boot where the same machine found the tunnel
 * port once the desktop was up).
 */
static VOID cmDiscoverAndList(PCM_NHI nhi)
{
    PCM_REENUM r;
    KIRQL irql;
    ULONG i;
    ULONG active;

    r = &nhi->Reenum;
    cmDiscover(nhi);
    r->RediscoverAt = cmNow(nhi);
    if (r->Members == 0) {
        r->Why = CM_NOROUTE_NO_SET;
        CmLogNote(nhi, "cm.reenum.no-route", r->Why);
        if (r->RediscoverLeft != 0) {
            r->RediscoverLeft--;
        }
        return;
    }
    r->RediscoverLeft = 0;
    r->Why = CM_NOROUTE_NONE;
    if (r->DebtPending) {
        /* Tunnel changes came while the set was unknown: every member owes
         * a re-enumeration (CmReenumService pays it once it is active). */
        r->DebtPending = 0;
        for (i = 0; i < r->Members; i++) {
            r->Port[i].Missed = 1;
        }
        CmLogNote(nhi, "cm.reenum.debt", r->Members);
    }

    if (IoIsWdmVersionAvailable(1, 0x10)) {
        r->Route = CM_ROUTE_FILTER;
    } else {
        /* CONFIGMG is not called while the NHI's own start is still in
         * progress (the configuration manager is the one starting it):
         * the route opens at the first CmReenumAction or CmReenumService,
         * both of which run after the start was completed. */
        r->CfgmgOwed = 1;
    }

    /* The claims are published whatever the route, so another domain
     * claiming the same port stays out of it (exclusive ownership). */
    KeAcquireSpinLock(&cmRegLock, &irql);
    InsertTailList(&cmRegDomains, &r->Link);
    r->Listed = 1;
    cmRegRecompute();
    KeReleaseSpinLock(&cmRegLock, irql);

    active = 0;
    for (i = 0; i < r->Members; i++) {
        if (cmFlags(nhi, i) & CM_PORT_EXCLUSIVE) {
            active++;
        }
        CmLogNote(nhi, "cm.reenum.member.flags",
                  (r->Port[i].Bdf << 8) | cmFlags(nhi, i));
    }
    if (active == 0) {
        r->Why = CM_NOROUTE_ALL_CLAIMED;
        CmLogNote(nhi, "cm.reenum.all-claimed", r->Members);
    }
    if (r->Route == CM_ROUTE_FILTER && r->Reenum != 0) {
        cmAskBaselines(nhi);
    }
}

/*
 * The CONFIGMG route of a Windows 98 SE or ME domain, opened after the
 * NHI's start: the guards of section 6.1, then the members' children
 * recorded unmarked (a device tunnelled before is not marked).
 */
static VOID cmCfgmgLate(PCM_NHI nhi)
{
    PCM_REENUM r;
    KIRQL irql;
    ULONG i;

    r = &nhi->Reenum;
    if (!r->CfgmgOwed) {
        return;
    }
    r->CfgmgOwed = 0;
    if (!CmCfgmgOpen(&r->Cfgmg)) {
        CmLogNote(nhi, "cm.reenum.9x.version", r->Cfgmg.Version);
        r->Why = r->Cfgmg.Why == CM_CFGMG_WHY_VERSION ? CM_NOROUTE_CFGMG_VER
                 : r->Cfgmg.Why == CM_CFGMG_WHY_ROOT  ? CM_NOROUTE_CFGMG_ROOT
                 : r->Cfgmg.Why == CM_CFGMG_WHY_ARCH  ? CM_NOROUTE_ARCH
                                                       : CM_NOROUTE_CFGMG_THUNK;
        CmLogNote(nhi, "cm.reenum.no-route", r->Why);
        return;
    }
    CmLogNote(nhi, "cm.reenum.9x.version", r->Cfgmg.Version);
    KeAcquireSpinLock(&cmRegLock, &irql);
    r->Route = CM_ROUTE_CONFIGMG;
    cmRegRecompute();
    KeReleaseSpinLock(&cmRegLock, irql);
    for (i = 0; i < r->Members; i++) {
        CmLogNote(nhi, "cm.reenum.member.flags",
                  (r->Port[i].Bdf << 8) | cmFlags(nhi, i));
        if (cmFlags(nhi, i) & CM_PORT_ACTIVE) {
            cmCfgmgMark(nhi, i, FALSE);
        }
    }
}

/*
 * A tunnel change unsettles every member's bridge: which port a device
 * used is not known, and the BusRelations answer that would show the new
 * population comes later, asynchronously. An eject posted or seen meanwhile
 * is not handed off until its bridge has settled, and then only if the
 * bridge's population generation is still the one it was posted against
 * (an unplug and replug on that port changes it; a change on another port
 * does not).
 */
static VOID cmEjectsVoid(PCM_NHI nhi)
{
    PCM_REENUM r;
    KIRQL irql;
    PCM_FILTER f;
    ULONG i;

    r = &nhi->Reenum;
    KeAcquireSpinLock(&cmRegLock, &irql);
    for (i = 0; i < r->Members; i++) {
        f = cmRegFind(r->Port[i].Bdf);
        if (f != NULL && f->Owner == nhi) {
            /* Which port changed is not known: every member's bridge is
             * unsettled until its next BusRelations answer shows its
             * population, and no eject is handed off meanwhile. */
            f->Unsettled = 1;
            f->ChangeSeq++;
        }
    }
    KeReleaseSpinLock(&cmRegLock, irql);
}

VOID CmReenumAction(PCM_NHI nhi, const TBT_ICM_ACTION *action)
{
    PCM_REENUM r;
    ULONG state;

    r = &nhi->Reenum;
    cmCfgmgLate(nhi);
    cmEjectsVoid(nhi);
    if (action->Kind == TBT_ICM_ACT_TUNNEL_UP) {
        r->Ups++;
    } else if (action->Kind == TBT_ICM_ACT_TUNNEL_DOWN) {
        r->Downs++;
    } else {
        r->Resyncs++;
    }
    if (r->Members == 0 && r->RediscoverLeft != 0 && r->Reenum != 0) {
        /* The set is still being looked for: the change is owed to every
         * member the rediscovery finds (cmDiscoverAndList). */
        r->DebtPending = 1;
    }
    if (r->Route == CM_ROUTE_NONE || r->Reenum == 0) {
        r->NotDone++;
        CmLogNote(nhi, "cm.reenum.not-done",
                  (action->Kind << 8) | (r->Reenum == 0 ? 0xFFUL : r->Why));
        return;
    }
    if (action->Kind == TBT_ICM_ACT_TUNNEL_UP) {
        state = cmPoll(nhi, action, XHCI_TBT_WANT_UP);
        CmLogNote(nhi, "cm.reenum.up.poll", (state << 8) | r->Poll.Last);
        if (state == XHCI_TBT_POLL_ABANDONED) {
            /* The device left during the poll: its TUNNEL_DOWN is queued
             * and runs in this one's place. */
            r->Abandoned++;
            return;
        }
        if (state == XHCI_TBT_POLL_TIMEOUT) {
            r->PollTimeouts++;
        }
        cmReenumAll(nhi, FALSE);
    } else if (action->Kind == TBT_ICM_ACT_TUNNEL_DOWN) {
        state = cmPoll(nhi, action, XHCI_TBT_WANT_DOWN);
        CmLogNote(nhi, "cm.reenum.down.poll", (state << 8) | r->Poll.Last);
        if (state == XHCI_TBT_POLL_TIMEOUT) {
            /* Not read clear (another device may still be tunnelled):
             * every port is rescanned all the same. */
            r->PollTimeouts++;
        }
        /* nvme98's statement, then its acknowledgement, before the removal
         * (cm_nvlink.h, obligations 2 and 3). */
        if ((action->Why & CM_U4_WHY_BASE) == 0) {
            (VOID)CmNvlinkIcmDown(nhi, state);
        }
        CmNvlinkAwait(nhi);
        cmReenumAll(nhi, TRUE);
    } else {
        CmNvlinkAwait(nhi);
        cmReenumAll(nhi, TRUE);
    }
    if (r->Route == CM_ROUTE_CONFIGMG && r->SafeEject != 0 &&
        action->Kind != TBT_ICM_ACT_TUNNEL_DOWN) {
        r->MarkOpen = 1;
        r->MarkFrom = cmNow(nhi);
    }
}

VOID CmReenumService(PCM_NHI nhi)
{
    PCM_REENUM r;
    KIRQL irql;
    PCM_FILTER f;
    ULONG pending;
    ULONG stale;
    ULONG i;

    r = &nhi->Reenum;
    if (!r->Listed && r->RediscoverLeft != 0 &&
        cmNow(nhi) - r->RediscoverAt >= CM_REDISCOVER_MS) {
        cmDiscoverAndList(nhi);
    }
    if (!r->Listed) {
        return;
    }
    stale = 0;
    cmCfgmgLate(nhi);
    /* Each eject is checked again at the hand-off, under the lock: the
     * same publication of the bridge, still this domain's, the same
     * population, and still no function with a stack behind it. One that
     * no longer holds is dropped and logged; the hook is called after the
     * lock is released. */
    KeAcquireSpinLock(&cmRegLock, &irql);
    pending = r->EjectPending;
    r->EjectPending = 0;
    for (i = 0; i < r->Members; i++) {
        if ((pending & (1UL << i)) == 0) {
            continue;
        }
        f = cmRegFind(r->Port[i].Bdf);
        if (f != NULL && f->Owner == nhi && f->Unsettled) {
            /* Deferred to a later pass: the bridge has not shown its
             * population since the last tunnel change. */
            pending &= ~(1UL << i);
            r->EjectPending |= 1UL << i;
            continue;
        }
        r->Port[i].Flags &= ~CM_PORT_EJECTED;
        if (f == NULL || f->Owner != nhi ||
            f->Serial != r->Port[i].EjectSerial ||
            f->RelGen != r->Port[i].EjectGen || !cmEjectReady(f)) {
            pending &= ~(1UL << i);
            stale |= 1UL << i;
        }
    }
    KeReleaseSpinLock(&cmRegLock, irql);
    for (i = 0; i < r->Members; i++) {
        if (stale & (1UL << i)) {
            r->EjectsStale++;
            CmLogNote(nhi, "cm.reenum.eject.stale", r->Port[i].Bdf);
        }
        if ((pending & (1UL << i)) == 0) {
            continue;
        }
        r->Ejects++;
        CmLogNote(nhi, "cm.reenum.eject", r->Port[i].Bdf);
        if (r->EjectHook != NULL) {
            r->EjectHook(r->EjectContext, r->Port[i].Bdf);
        } else {
            /* Under the firmware connection manager there is no
             * per-device teardown: the tunnel stays up and the user
             * unplugs (record 21 section 7.4). */
            CmLogNote(nhi, "cm.reenum.eject.tunnel-left-up",
                      r->Port[i].Bdf);
        }
    }
    if (r->Route == CM_ROUTE_FILTER && r->Reenum != 0) {
        cmAskBaselines(nhi);
    }
    /* A change skipped while a member was not active, now that it is;
     * the debt stays until a call is actually made. */
    for (i = 0; i < r->Members && r->Reenum != 0; i++) {
        if (r->Port[i].Missed && (cmFlags(nhi, i) & CM_PORT_ACTIVE) &&
            cmNow(nhi) - r->Port[i].MissedAt >= CM_MISSED_RETRY_MS) {
            r->Port[i].MissedAt = cmNow(nhi);
            (VOID)CmNvlinkExpose(nhi, r->Port[i].Bdf);
            if (r->Route == CM_ROUTE_FILTER) {
                if (cmInvalidate(nhi, i, TRUE)) {
                    r->Port[i].Missed = 0;
                }
            } else if (r->Route == CM_ROUTE_CONFIGMG) {
                if (cmCfgmgMember(nhi, i, TRUE)) {
                    r->Port[i].Missed = 0;
                    if (r->SafeEject != 0) {
                        /* The re-enumeration was late: so is the marking
                         * of what it brought (section 7.4). */
                        r->MarkOpen = 1;
                        r->MarkFrom = cmNow(nhi);
                    }
                }
            }
            if (!r->Port[i].Missed) {
                CmLogNote(nhi, "cm.reenum.missed", r->Port[i].Bdf);
            }
        }
    }
    if (r->Route == CM_ROUTE_CONFIGMG && r->MarkOpen) {
        for (i = 0; i < r->Members; i++) {
            if (cmFlags(nhi, i) & CM_PORT_ACTIVE) {
                cmCfgmgMark(nhi, i, TRUE);
            }
        }
        if (cmNow(nhi) - r->MarkFrom >= CM_MARK_WINDOW_MS) {
            r->MarkOpen = 0;
        }
    }
}

VOID CmReenumSuspend(PCM_NHI nhi)
{
    PCM_REENUM r;
    const TBT_ICM_PROFILE *profile;
    CM_PORTS_RESUME resume;
    KIRQL irql;
    PCM_FILTER f;
    ULONG seconds;
    ULONG i;

    r = &nhi->Reenum;
    r->Suspends++;
    /* Every synchronous stage of the client's resume (cm_ports.h), with
     * every device the client's table can hold re-announced and approved
     * (a dock plugged during the sleep can add several), held to the
     * operating system's limit on a retained power IRP: past it the
     * failure path runs, never a stalled power transition. */
    profile = nhi->Icm.Profile;
    resume.ReadyMs = nhi->Icm.CmdTimeoutMs;
    resume.ReadyRetries = 0;
    if (profile != NULL) {
        if (profile->ReadyTimeoutMs != 0) {
            resume.ReadyMs = profile->ReadyTimeoutMs;
        }
        resume.ReadyRetries = profile->ReadyRetries;
    }
    resume.HostPollTries = TBT_ICM_HOSTPOLL_TRIES;
    resume.HostPollMs = TBT_ICM_HOSTPOLL_TIMEOUT_MS;
    resume.ReconcileMs = TBT_ICM_RECONCILE_MAX_MS;
    resume.Devices = TBT_ICM_MAX_DEVICES;
    resume.CmdMs = nhi->Icm.CmdTimeoutMs;
    resume.ApproveMs = nhi->Icm.ApproveTimeoutMs;
    resume.Retries = nhi->Icm.Retries;
    /* The client's own bound is logged; the watchdog itself is the fixed
     * limit, CM_PORTS_HOLD_MAX_MS: the client's announcement-driven work
     * has no bound this driver can count exactly (each drain may process
     * many announcements), and past the limit the failure path is the
     * policy (cm_ports.h). */
    CmLogNote(nhi, "cm.reenum.hold.client-ms", CmPortsHoldBound(&resume));
    r->HoldBoundMs = CM_PORTS_HOLD_MAX_MS;
    seconds = (r->HoldBoundMs + 999UL) / 1000UL;
    CmLogNote(nhi, "cm.reenum.hold.bound-ms", r->HoldBoundMs);
    if (r->Route != CM_ROUTE_FILTER) {
        return;
    }
    KeAcquireSpinLock(&cmRegLock, &irql);
    for (i = 0; i < r->Members; i++) {
        f = cmRegFind(r->Port[i].Bdf);
        if (f != NULL && f->Owner == nhi && f->Active) {
            f->Held = 1;
            f->HeldBy = nhi;
            f->HoldLeftS = seconds;
            f->HoldReenum = r->Reenum;
            r->Port[i].Flags |= CM_PORT_HELD;
        }
    }
    KeReleaseSpinLock(&cmRegLock, irql);
}

/*
 * Every member this domain holds released; `fail` takes section 6.3's
 * failure path, an invalidation of each released member. Spin locks,
 * IoCompleteRequest, PoStartNextPowerIrp and IoInvalidateDeviceRelations
 * only: callable at <= DISPATCH_LEVEL (CmReenumResumeFailed's use).
 */
static VOID cmReleaseHeld(PCM_NHI nhi, BOOLEAN fail)
{
    PCM_REENUM r;
    KIRQL irql;
    PCM_FILTER f;
    PIRP irp;
    BOOLEAN dropRef;
    ULONG i;
    ULONG was;

    r = &nhi->Reenum;
    if (r->Route != CM_ROUTE_FILTER) {
        return;
    }
    for (i = 0; i < r->Members; i++) {
        irp = NULL;
        dropRef = FALSE;
        was = 0;
        f = NULL;
        KeAcquireSpinLock(&cmRegLock, &irql);
        r->Port[i].Flags &= ~CM_PORT_HELD;
        f = cmRegFind(r->Port[i].Bdf);
        if (f != NULL && f->HeldBy == nhi) {
            was = 1;
            irp = cmHoldRelease(f, &dropRef);
        }
        KeReleaseSpinLock(&cmRegLock, irql);
        if (irp != NULL) {
            cmHoldComplete(irp);
            CmLogNote(nhi, fail ? "cm.reenum.hold.released-failed"
                                : "cm.reenum.hold.released",
                      r->Port[i].Bdf);
        }
        if (dropRef) {
            cmRegRelease(f);
        }
        if (was && fail && r->Reenum != 0) {
            (VOID)cmInvalidate(nhi, i, FALSE);
        }
    }
}

VOID CmReenumResumeFailed(PCM_NHI nhi)
{
    nhi->Reenum.ResumeFailures++;
    CmLogNote(nhi, "cm.reenum.resume-failed", nhi->Reenum.Members);
    cmReleaseHeld(nhi, TRUE);
}

VOID CmReenumResumed(PCM_NHI nhi, BOOLEAN ok)
{
    if (ok) {
        nhi->Reenum.Resumes++;
    } else {
        nhi->Reenum.ResumeFailures++;
    }
    CmLogNote(nhi, "cm.reenum.resumed", ok ? 1UL : 0UL);
    cmReleaseHeld(nhi, (BOOLEAN)!ok);
}

VOID CmReenumStop(PCM_NHI nhi)
{
    PCM_REENUM r;
    KIRQL irql;
    ULONG attached;
    ULONG skipped;
    ULONG refused;
    ULONG watchdogs;

    r = &nhi->Reenum;
    CmNvlinkStop(nhi);
    r->RediscoverLeft = 0;
    if (!r->Listed && r->Route == CM_ROUTE_NONE) {
        r->Members = 0;
        return;
    }
    r->Stops++;
    cmReleaseHeld(nhi, TRUE);
    KeAcquireSpinLock(&cmRegLock, &irql);
    if (r->Listed) {
        RemoveEntryList(&r->Link);
        r->Listed = 0;
    }
    r->EjectPending = 0;
    cmRegRecompute();
    KeReleaseSpinLock(&cmRegLock, irql);
    if (r->Route == CM_ROUTE_CONFIGMG) {
        CmCfgmgClose(&r->Cfgmg);
    }
    r->Route = CM_ROUTE_NONE;
    r->Members = 0;
    r->MarkOpen = 0;
    r->CfgmgOwed = 0;

    CmLogNote(nhi, "cm.reenum.ups", r->Ups);
    CmLogNote(nhi, "cm.reenum.downs", r->Downs);
    CmLogNote(nhi, "cm.reenum.resyncs", r->Resyncs);
    CmLogNote(nhi, "cm.reenum.not-done", r->NotDone);
    CmLogNote(nhi, "cm.reenum.calls", r->Calls);
    CmLogNote(nhi, "cm.reenum.call-failures", r->CallFailures);
    CmLogNote(nhi, "cm.reenum.abandoned", r->Abandoned);
    CmLogNote(nhi, "cm.reenum.poll-timeouts", r->PollTimeouts);
    CmLogNote(nhi, "cm.reenum.9x.removed", r->Removed);
    CmLogNote(nhi, "cm.reenum.9x.vetoed", r->Vetoed);
    CmLogNote(nhi, "cm.reenum.9x.marked", r->Marked);
    CmLogNote(nhi, "cm.reenum.9x.mark-failures", r->MarkFailures);
    CmLogNote(nhi, "cm.reenum.ejects", r->Ejects);
    CmRegCounters(&attached, &skipped, &refused, &watchdogs);
    CmLogNote(nhi, "cm.reenum.child.attached", attached);
    CmLogNote(nhi, "cm.reenum.child.skipped", skipped);
    CmLogNote(nhi, "cm.reenum.child.refused", refused);
    CmLogNote(nhi, "cm.reenum.hold.watchdogs", watchdogs);
}
