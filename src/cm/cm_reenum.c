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
 * and ME: CONFIGMG (section 6.1), every call made by the domain's worker
 * thread (cm_cfgwk.c) and never by this one, since a CONFIGMG service
 * blocks while the configuration manager is busy - the bridge's devnode
 * found by its instance id, vanished children removed with
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
 * child answers IRP_MN_QUERY_CAPABILITIES with Removable (cm_filt.c).
 * Since task 42-A.5 (revision 3) a child is also made by AddDevice for a
 * device whose LowerFilters value the mark service wrote, so the mark
 * survives a rebuild of the device's stack; every attachment is on the
 * attachment list, each domain's marks table (cm_marks.h) reads it and
 * the query device answers it (IOCTL_XHCICM_MARKS). A child removed after
 * a query-remove posts no eject by itself: the departure is pending on its
 * entry, and an eject is posted only on the identity-checked confirmation
 * the service or an administrator's tool writes (CmReenumConfirm), which
 * CmReenumService hands to the per-port entry of record 22 section 10.4
 * when one is set. 98 SE and ME, best effort: after a tunnel up the
 * worker, for a window, walks each active member's children and adds
 * DN_REMOVABLE (Change_DevNode_Status) to each new one, never to a tunnel
 * port itself.
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
#include "cm_call.h"

/* The USB4 profile's actions reach the seam with this bit in Why (cm_nhi.c,
 * cmU4ToIcm); its stor98 statement is made at the core's upcall, not here. */
#ifndef CM_U4_WHY_BASE
#define CM_U4_WHY_BASE 0x80UL
#endif

/* HalGetBusData: as hcd_ctl.c restates it for amd64 builds whose headers
 * leave it out. */
#if defined(_WIN64) && defined(NO_LEGACY_DRIVERS)
NTHALAPI ULONG HalGetBusData(BUS_DATA_TYPE BusDataType, ULONG BusNumber,
                             ULONG SlotNumber, PVOID Buffer, ULONG Length);
#endif

/* A discovery that could not read every function is retried this often. */
#define CM_FIND_TRIES       3UL
#define CM_FIND_RETRY_MS    100UL

/* An owed re-enumeration is retried no faster than this. */
#define CM_MISSED_RETRY_MS  5000UL

/* An empty set is looked for again this often, this far apart. */
#define CM_REDISCOVER_TRIES 30UL
#define CM_REDISCOVER_MS    2000UL

/* The registration list. */
static KSPIN_LOCK cmRegLock;
static LIST_ENTRY cmRegFilters;
static LIST_ENTRY cmRegDomains;
static ULONG cmRegSerial;
static ULONG cmRegAttached;
/* Child filters AddDevice made that no bridge could be named for yet
 * (CmRegAttLink, CmRegRelations): while any exists, a bridge that ever
 * answered more functions than its snapshot keeps (TruncSeen) cannot prove
 * a retirement (record 22 section 7.7). Under the lock. */
static ULONG cmRegUnattributed;
static ULONG cmRegSkipped;
static ULONG cmRegRefused;
static ULONG cmRegOver;
static ULONG cmRegWatchdogs;
/* Every attachment of this driver on a device (task 42-A.5, cm_reenum.h's
 * "The marks"), and the LowerFilters children no active member owned. */
static LIST_ENTRY cmRegAtts;
static ULONG cmRegStray;

static NTSTATUS cmChildCreate(PCM_FILTER bridge, PDEVICE_OBJECT pdo,
                              PULONG over);
static VOID cmDiscoverAndList(PCM_NHI nhi);
static VOID cmMarksSyncAll(VOID);
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
    InitializeListHead(&cmRegAtts);
}

VOID CmRegFilterInit(PCM_FILTER filter)
{
    InitializeListHead(&filter->RegLink);
    InitializeListHead(&filter->AttLink);
    KeInitializeEvent(&filter->Rundown, NotificationEvent, FALSE);
    KeInitializeTimer(&filter->HoldTimer);
    KeInitializeDpc(&filter->HoldDpc, cmHoldDpc, filter);
}

VOID CmRegCounters(PULONG attached, PULONG over, PULONG skipped,
                   PULONG refused, PULONG watchdogs)
{
    KIRQL irql;

    KeAcquireSpinLock(&cmRegLock, &irql);
    *attached = cmRegAttached;
    *over = cmRegOver;
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
    cmMarksSyncAll();
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

    /* cmLocation's DevicePropertyAddress is answered by a QUERY_CAPABILITIES
     * down this stack, which this filter must pass through unlocated. */
    filter->InLocate++;
    if (!cmLocation(filter->Common.Pdo, &bdf)) {
        filter->InLocate--;
        return;
    }
    filter->InLocate--;
    KeAcquireSpinLock(&cmRegLock, &irql);
    if (!filter->Published) {
        filter->Bdf = bdf;
        filter->Gone = 0;
        filter->Baseline = 0;
        filter->InFlight = 0;
        filter->Held = 0;
        filter->HeldBy = NULL;
        /* A new publication: an eject's hold ended before the withdrawal
         * completed (it holds a reference), so none is carried over. */
        filter->EjectHold = 0;
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
    ULONG newLocd[CM_SNAP_MAX];
    ULONG newBus[CM_SNAP_MAX];
    ULONG newAddr[CM_SNAP_MAX];
    ULONG count;
    ULONG fresh;
    ULONG i;
    ULONG j;
    PLIST_ENTRY e;
    PCM_FILTER child;
    ULONG attach;
    ULONG over;
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
        status = cmChildCreate(filter, (PDEVICE_OBJECT)now[i], &over);
        KeAcquireSpinLock(&cmRegLock, &irql);
        if (NT_SUCCESS(status)) {
            cmRegAttached++;
            if (over) {
                cmRegOver++;
            }
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
        /* An answer to a query begun after the last change: what the
         * recovery's retirement proof counts (CmReenumRetired), with
         * whether that same answer was complete and reported nothing. */
        filter->RelAnswers++;
        filter->RelEmptyAt = count == 0 ? filter->RelAnswers : 0;
    } else if (count != 0) {
        /* An older query's answer that still reports a function: no
         * proof of absence stands past it. */
        filter->RelEmptyAt = 0;
    }
    for (i = 0; i < count; i++) {
        ULONG j;
        ULONG locd;
        ULONG bus;
        ULONG addr;

        /* A PDO that stays in the snapshot keeps the location kept for it. */
        locd = 0;
        bus = 0;
        addr = 0;
        for (j = 0; j < filter->SnapCount && !isNew[i]; j++) {
            if (filter->Snap[j] == now[i] && filter->SnapLocd[j]) {
                locd = 1;
                bus = filter->SnapBus[j];
                addr = filter->SnapAddr[j];
            }
        }
        newLocd[i] = locd;
        newBus[i] = bus;
        newAddr[i] = addr;
    }
    for (i = 0; i < count; i++) {
        filter->Snap[i] = now[i];
        filter->SnapLocd[i] = newLocd[i];
        filter->SnapBus[i] = newBus[i];
        filter->SnapAddr[i] = newAddr[i];
    }
    filter->SnapCount = count;
    if (relations != NULL) {
        /* A child filter AddDevice made that no bridge was found for when
         * it linked (CmRegAttLink): counted for this bridge once its whole
         * answer, not only the kept snapshot, reports the PDO (record 22
         * section 7.7's retirement proof). */
        for (e = cmRegAtts.Flink; e != &cmRegAtts; e = e->Flink) {
            child = CONTAINING_RECORD(e, CM_FILTER, AttLink);
            if (child->Common.Kind != CM_KIND_CHILD || child->Bridge != NULL) {
                continue;
            }
            for (j = 0; j < relations->Count; j++) {
                if ((PVOID)relations->Objects[j] == (PVOID)child->Common.Pdo) {
                    child->Bridge = filter;
                    filter->ChildLive++;
                    child->ChildCounted = 1;
                    if (child->Unattributed) {
                        child->Unattributed = 0;
                        cmRegUnattributed--;
                    }
                    break;
                }
            }
        }
    }
    filter->SnapTruncated = (ULONG)(relations != NULL &&
                                    relations->Count > CM_SNAP_MAX);
    if (filter->SnapTruncated) {
        filter->TruncSeen = 1;
    }
    if (filter->Published && !filter->Gone && !filter->Baseline) {
        filter->Baseline = 1;
        cmRegRecompute();
    } else {
        cmMarksSyncAll();
    }
    KeReleaseSpinLock(&cmRegLock, irql);
}

/* Four characters of a driver object's name from `from` on (8 skips the
 * "\Driver\" prefix), one per byte, high byte first, for the log: which
 * driver a new PDO already carried. PASSIVE_LEVEL. */
static ULONG cmNameChars(PDRIVER_OBJECT driver, ULONG from)
{
    ULONG v;
    ULONG k;
    ULONG chars;

    v = 0;
    if (driver == NULL || driver->DriverName.Buffer == NULL) {
        return 0;
    }
    chars = (ULONG)driver->DriverName.Length / sizeof(WCHAR);
    for (k = 0; k < 4; k++) {
        if (from + k < chars) {
            v |= ((ULONG)driver->DriverName.Buffer[from + k] & 0xFFUL)
                 << (8UL * (3UL - k));
        }
    }
    return v;
}

/*
 * A child filter above a PDO pci.sys newly reported on an active bridge,
 * before the PnP manager builds that device's stack (section 7.4 step 1):
 * directly above the PDO, or (revision 2) above the bus filters a driver
 * below the bridge filter already put on it, which is where an ACPI
 * machine leaves a PCI-to-PCI bridge; CmPortsChildAttach decides.
 * STATUS_SUCCESS, with *over 1 when it went above bus filters;
 * STATUS_DEVICE_BUSY when anything else is attached, so nothing is.
 * PASSIVE_LEVEL.
 */
static NTSTATUS cmChildCreate(PCM_FILTER bridge, PDEVICE_OBJECT pdo,
                              PULONG over)
{
    PDEVICE_OBJECT self;
    PDEVICE_OBJECT lower;
    PDEVICE_OBJECT walk;
    PCM_FILTER child;
    PVOID chain[CM_CHILD_CHAIN_MAX + 1];
    ULONG chainCount;
    ULONG decision;
    PCM_NHI owner;
    ULONG name0;
    ULONG name1;
    KIRQL irql;
    ULONG truncated;
    NTSTATUS status;

    *over = 0;
    chainCount = 0;
    name0 = 0;
    name1 = 0;
    /* SnapTruncated is written by this bridge's PnP dispatch alone, which
     * is where this runs, so the value read stays the value. */
    KeAcquireSpinLock(&cmRegLock, &irql);
    truncated = bridge->SnapTruncated;
    KeReleaseSpinLock(&cmRegLock, irql);
    walk = pdo->AttachedDevice;
    decision = CmPortsChildFirst(truncated, (ULONG)(walk != NULL));
    if (decision == CM_CHILD_WALK) {
        /* Only for a PDO that is new by a whole snapshot: nothing the PnP
         * manager builds can attach or detach before this answer reaches
         * it, so the chain holds still without the I/O manager's database
         * lock. After a truncated snapshot "new" proves nothing, and
         * CmPortsChildFirst has skipped without the walk. */
        name0 = cmNameChars(walk->DriverObject, 8);
        name1 = cmNameChars(walk->DriverObject, 12);
        while (walk != NULL && chainCount <= CM_CHILD_CHAIN_MAX) {
            chain[chainCount] = (PVOID)walk->DriverObject;
            chainCount++;
            walk = walk->AttachedDevice;
        }
        decision = CmPortsChildAttach(truncated, (PVOID const *)chain,
                                      chainCount, (PVOID)pdo->DriverObject,
                                      (PVOID)CmDriverObject);
    }
    /* The owner is used under the registration list's lock only: its
     * delisting (CmReenumStop) takes the same lock, so the NHI cannot
     * leave while its log is written. Order: cmRegLock, then LogLock. */
    KeAcquireSpinLock(&cmRegLock, &irql);
    owner = bridge->Owner;
    if (chainCount != 0 && owner != NULL) {
        CmLogNote(owner, "cm.reenum.child.under", name0);
        CmLogNote(owner, "cm.reenum.child.under2", name1);
        CmLogNote(owner, "cm.reenum.child.under.count", chainCount);
        CmLogNote(owner, "cm.reenum.child.decision", decision);
    }
    KeReleaseSpinLock(&cmRegLock, irql);
    if (decision == CM_CHILD_SKIP) {
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
    lower = IoAttachDeviceToDeviceStack(self, pdo);
    if (lower == NULL) {
        IoDeleteDevice(self);
        return STATUS_NO_SUCH_DEVICE;
    }
    child->Common.Lower = lower;
    /* A filter mirrors the device it sits on (as CmFilterAddDevice does),
     * which since revision 2 may be another driver's bus filter rather
     * than the PDO. */
    self->DeviceType = lower->DeviceType;
    self->Characteristics = lower->Characteristics;
    self->Flags |= lower->Flags &
                   (DO_BUFFERED_IO | DO_DIRECT_IO | DO_POWER_PAGABLE |
                    DO_POWER_INRUSH);
    self->Flags &= ~DO_DEVICE_INITIALIZING;
    CmRegAttLink(child);
    *over = (ULONG)(decision == CM_CHILD_ATTACH_OVER);
    return STATUS_SUCCESS;
}

/*
 * Whether every function behind a bridge is without a device stack: each
 * PDO its last BusRelations reported has nothing attached, or only the
 * child filter that answer put on it with nothing above it (an eject, a
 * disable or no driver removed or never built its stack: CmPortsStackless),
 * and that answer was not truncated. Whatever put a stack back - a
 * re-enable, a driver installed - shows as an object attached above the
 * child, so no state is remembered per PDO. The snapshot's PDOs are those
 * pci.sys still reports, so they are not yet deleted; a child on the
 * attachment list is not yet deleted either (it leaves the list before
 * IoDeleteDevice). Under the lock.
 */
static BOOLEAN cmEjectReady(PCM_FILTER bridge)
{
    ULONG i;
    PDEVICE_OBJECT pdo;
    PLIST_ENTRY e;
    PCM_FILTER f;
    ULONG busChild;
    ULONG addDev;
    ULONG above;

    if (!bridge->Published || bridge->Gone || bridge->SnapTruncated) {
        return FALSE;
    }
    for (i = 0; i < bridge->SnapCount; i++) {
        pdo = (PDEVICE_OBJECT)bridge->Snap[i];
        if (pdo->AttachedDevice == NULL) {
            continue;
        }
        busChild = 0;
        addDev = 0;
        above = 0;
        for (e = cmRegAtts.Flink; e != &cmRegAtts; e = e->Flink) {
            f = CONTAINING_RECORD(e, CM_FILTER, AttLink);
            if (f->Common.Pdo != pdo) {
                continue;
            }
            if (f->Common.Kind == CM_KIND_CHILD && !f->AddDev) {
                busChild = 1;
                if (f->Common.Self->AttachedDevice != NULL) {
                    above = 1;
                }
            } else {
                addDev = 1;
            }
        }
        if (!CmPortsStackless(1, busChild, addDev, above)) {
            return FALSE;
        }
    }
    return TRUE;
}

BOOLEAN CmRegStartAllowed(PCM_FILTER filter)
{
    KIRQL irql;
    PLIST_ENTRY e;
    PCM_FILTER f;
    PCM_FILTER held;
    ULONG i;

    held = NULL;
    KeAcquireSpinLock(&cmRegLock, &irql);
    for (e = cmRegFilters.Flink; e != &cmRegFilters && held == NULL;
         e = e->Flink) {
        f = CONTAINING_RECORD(e, CM_FILTER, RegLink);
        if (!f->EjectHold) {
            continue;
        }
        for (i = 0; i < f->SnapCount; i++) {
            if (f->Snap[i] == (PVOID)filter->Common.Pdo) {
                held = f;
                break;
            }
        }
    }
    /* Refused whoever owns the member now (a claim recomputed meanwhile
     * does not end the teardown); the owner, used under the lock only as
     * its other notes are, is only where the note goes. */
    if (held != NULL && held->Owner != NULL) {
        CmLogNote(held->Owner, "cm.reenum.eject.start-refused", held->Bdf);
    }
    KeReleaseSpinLock(&cmRegLock, irql);
    return (BOOLEAN)(held == NULL);
}

/* ---- the marks (record 21 section 7.4, revision 3; task 42-A.5) ------- */

/* The domain's clock as its thread last set it, never 0 (a departure's
 * time 0 reads "none"). Under the lock. */
static ULONG cmMarkNow(PCM_NHI d)
{
    return d->ClockMs != 0 ? d->ClockMs : 1UL;
}

/* The current entry of `pdo` in any listed domain, with its domain.
 * Under the lock. */
static PCM_MARK cmMarkFind(PVOID pdo, PCM_NHI *dom)
{
    PLIST_ENTRY e;
    PCM_NHI d;
    PCM_MARK m;

    for (e = cmRegDomains.Flink; e != &cmRegDomains; e = e->Flink) {
        d = CONTAINING_RECORD(CONTAINING_RECORD(e, CM_REENUM, Link), CM_NHI,
                              Reenum);
        m = CmMarksFind(&d->Reenum.Marks, pdo);
        if (m != NULL) {
            *dom = d;
            return m;
        }
    }
    *dom = NULL;
    return NULL;
}

/* The location of `pdo` kept beside the snapshot of the member bridge at
 * `memberBdf` (CM_FILTER.SnapLocd). Under the lock. */
static VOID cmSnapKeepLoc(ULONG memberBdf, PVOID pdo, ULONG bus,
                          ULONG address)
{
    PCM_FILTER f;
    ULONG k;

    f = cmRegFind(memberBdf);
    if (f == NULL) {
        return;
    }
    for (k = 0; k < f->SnapCount; k++) {
        if (f->Snap[k] == pdo) {
            f->SnapLocd[k] = 1;
            f->SnapBus[k] = bus;
            f->SnapAddr[k] = address;
        }
    }
}

/* An entry's attachments, location and Removable answer, from the
 * attachment list as it stands; an attachment present clears a pending
 * departure (the stack was rebuilt). Under the lock. */
static VOID cmMarkAtt(PCM_NHI d, PCM_MARK m)
{
    PLIST_ENTRY e;
    PCM_FILTER f;
    ULONG flags;
    ULONG bus;
    ULONG address;

    flags = 0;
    bus = m->Bus;
    address = m->Address;
    for (e = cmRegAtts.Flink; e != &cmRegAtts; e = e->Flink) {
        f = CONTAINING_RECORD(e, CM_FILTER, AttLink);
        if ((PVOID)f->Common.Pdo != m->Pdo) {
            continue;
        }
        if (f->Common.Kind == CM_KIND_CHILD && !f->AddDev) {
            flags |= CM_QM_EF_BUS_ATT;
        } else {
            flags |= CM_QM_EF_ADD_ATT;
        }
        if (f->Located) {
            flags |= CM_QM_EF_LOCATED;
            bus = f->LocBus;
            address = f->LocAddress;
        }
        if (f->Answered) {
            flags |= CM_QM_EF_REMOVABLE;
        }
        /* The entry the attachment's surprise removal is counted in. */
        f->MarkDom = d;
        f->MarkStarts = d->Reenum.Starts;
        f->MarkSerial = m->Serial;
    }
    /* A location once known stays known: the PDO is the same device. */
    flags |= m->Flags & CM_QM_EF_LOCATED;
    if (flags & CM_QM_EF_LOCATED) {
        cmSnapKeepLoc(m->MemberBdf, m->Pdo, bus, address);
    }
    if (flags != m->Flags || bus != m->Bus || address != m->Address ||
        ((flags & (CM_QM_EF_BUS_ATT | CM_QM_EF_ADD_ATT)) != 0 &&
         m->PendingAt != 0)) {
        m->Flags = flags;
        m->Bus = bus;
        m->Address = address;
        if ((flags & (CM_QM_EF_BUS_ATT | CM_QM_EF_ADD_ATT)) != 0) {
            m->PendingAt = 0;
            m->PendingGen = 0;
        }
        CmMarksTouch(&d->Reenum.Marks, m, cmMarkNow(d));
    }
}

/* Every listed domain's table against its members' snapshots and the
 * attachment list. Under the lock (cmRegRecompute's end, and each
 * BusRelations answer). */
static VOID cmMarksSyncAll(VOID)
{
    PLIST_ENTRY e;
    PCM_NHI d;
    PCM_REENUM r;
    PCM_FILTER f;
    ULONG i;
    BOOLEAN active;

    for (e = cmRegDomains.Flink; e != &cmRegDomains; e = e->Flink) {
        r = CONTAINING_RECORD(e, CM_REENUM, Link);
        d = CONTAINING_RECORD(r, CM_NHI, Reenum);
        if (r->Route != CM_ROUTE_FILTER) {
            continue;
        }
        CmMarksBegin(&r->Marks);
        for (i = 0; i < r->Members; i++) {
            f = cmRegFind(r->Port[i].Bdf);
            active = (BOOLEAN)(f != NULL && f->Owner == d && f->Active &&
                               (r->Port[i].Flags & CM_PORT_ACTIVE) != 0);
            (VOID)CmMarksSync(&r->Marks, i, r->Port[i].Bdf,
                              active ? (PVOID const *)f->Snap : NULL,
                              active ? f->SnapCount : 0,
                              (BOOLEAN)(active && f->SnapTruncated), active,
                              cmMarkNow(d));
            if (active) {
                (VOID)CmMarksLocate(&r->Marks, i, (PVOID const *)f->Snap,
                                    f->SnapLocd, f->SnapBus, f->SnapAddr,
                                    f->SnapCount, cmMarkNow(d));
            }
        }
        (VOID)CmMarksDropMembers(&r->Marks, r->Members, cmMarkNow(d));
        for (i = 0; i < CM_QM_MAX_ENTRIES; i++) {
            if (r->Marks.E[i].Pdo != NULL && !r->Marks.E[i].Tomb) {
                cmMarkAtt(d, &r->Marks.E[i]);
            }
        }
    }
}

/* The entry of `f`'s PDO brought up to date. Under the lock. */
static VOID cmMarkAttOf(PCM_FILTER f)
{
    PCM_NHI d;
    PCM_MARK m;

    m = cmMarkFind((PVOID)f->Common.Pdo, &d);
    if (m != NULL) {
        cmMarkAtt(d, m);
    }
}

VOID CmRegAttLink(PCM_FILTER filter)
{
    KIRQL irql;
    PLIST_ENTRY e;
    PCM_FILTER f;
    ULONG i;

    KeAcquireSpinLock(&cmRegLock, &irql);
    if (!filter->AttListed) {
        InsertTailList(&cmRegAtts, &filter->AttLink);
        filter->AttListed = 1;
        cmMarkAttOf(filter);
        if (filter->Common.Kind == CM_KIND_CHILD && filter->Bridge == NULL) {
            /* A child AddDevice made: the bridge whose last BusRelations
             * reported its PDO, so record 22 section 7.7's retirement
             * proof waits for its removal as for one the bridge attached. */
            for (e = cmRegFilters.Flink;
                 e != &cmRegFilters && filter->Bridge == NULL; e = e->Flink) {
                f = CONTAINING_RECORD(e, CM_FILTER, RegLink);
                for (i = 0; f->Published && !f->Gone && i < f->SnapCount; i++) {
                    if (f->Snap[i] == (PVOID)filter->Common.Pdo) {
                        filter->Bridge = f;
                    }
                }
            }
        }
        if (filter->Common.Kind == CM_KIND_CHILD && filter->Bridge != NULL &&
            !filter->ChildCounted) {
            filter->Bridge->ChildLive++;
            filter->ChildCounted = 1;
        }
        if (filter->Common.Kind == CM_KIND_CHILD && filter->Bridge == NULL &&
            !filter->Unattributed) {
            filter->Unattributed = 1;
            cmRegUnattributed++;
        }
    }
    KeReleaseSpinLock(&cmRegLock, irql);
}

/* The PDO's bus number and address, as the PnP manager's devnode has them
 * (record 21 section 7.4, revision 3: never read at the BusRelations
 * attachment, before the devnode exists). PASSIVE_LEVEL. */
/* The location published once read. PASSIVE_LEVEL. */
static VOID cmAttLocated(PCM_FILTER filter, ULONG bus, ULONG address)
{
    KIRQL irql;

    if (bus > 0xFFUL || (address >> 16) > 0x1FUL ||
        (address & 0xFFFFUL) > 7UL) {
        return;
    }
    KeAcquireSpinLock(&cmRegLock, &irql);
    filter->LocBus = bus;
    filter->LocAddress = address;
    filter->Located = 1;
    cmMarkAttOf(filter);
    KeReleaseSpinLock(&cmRegLock, irql);
}

VOID CmRegAttLocate(PCM_FILTER filter)
{
    ULONG bus;
    ULONG address;
    ULONG bytes;
    BOOLEAN ok;

    if (filter->Located || filter->InLocate) {
        return;
    }
    bus = 0;
    address = 0;
    filter->InLocate++;
    ok = (BOOLEAN)(NT_SUCCESS(IoGetDeviceProperty(filter->Common.Pdo,
                                                  DevicePropertyBusNumber,
                                                  sizeof(bus), &bus,
                                                  &bytes)) &&
                   NT_SUCCESS(IoGetDeviceProperty(filter->Common.Pdo,
                                                  DevicePropertyAddress,
                                                  sizeof(address), &address,
                                                  &bytes)));
    filter->InLocate--;
    if (ok) {
        cmAttLocated(filter, bus, address);
    }
}

VOID CmRegAttLocateCaps(PCM_FILTER filter, ULONG address)
{
    ULONG bus;
    ULONG bytes;
    BOOLEAN ok;

    if (filter->Located || filter->InLocate) {
        return;
    }
    bus = 0;
    filter->InLocate++;
    ok = (BOOLEAN)NT_SUCCESS(IoGetDeviceProperty(filter->Common.Pdo,
                                                 DevicePropertyBusNumber,
                                                 sizeof(bus), &bus, &bytes));
    filter->InLocate--;
    if (ok) {
        cmAttLocated(filter, bus, address);
    }
}

BOOLEAN CmRegAttMayMark(PCM_FILTER filter)
{
    PLIST_ENTRY e;
    PCM_REENUM r;
    PCM_NHI d;
    KIRQL irql;
    ULONG i;
    ULONG want;
    BOOLEAN may;
    BOOLEAN seen;

    if (filter->Common.Kind == CM_KIND_CHILD && !filter->AddDev) {
        return TRUE;
    }
    may = FALSE;
    seen = FALSE;
    want = CM_PORT_ACTIVE | CM_PORT_EXCLUSIVE;
    KeAcquireSpinLock(&cmRegLock, &irql);
    if (filter->Located) {
        for (e = cmRegDomains.Flink; e != &cmRegDomains; e = e->Flink) {
            r = CONTAINING_RECORD(e, CM_REENUM, Link);
            d = CONTAINING_RECORD(r, CM_NHI, Reenum);
            if (r->Route != CM_ROUTE_FILTER) {
                continue;
            }
            for (i = 0; i < r->Members; i++) {
                if ((r->Port[i].Flags & want) == want &&
                    r->Port[i].SecBusValid &&
                    r->Port[i].SecBus == filter->LocBus &&
                    CmMarksFind(&r->Marks, (PVOID)filter->Common.Pdo) !=
                        NULL) {
                    seen = TRUE;
                    if (r->SafeEject != 0) {
                        may = TRUE;
                    }
                }
            }
            UNREFERENCED_PARAMETER(d);
        }
    }
    if (!seen && filter->Common.Kind == CM_KIND_CHILD && !filter->Stray) {
        /* A value on a device outside every set: attached and passive. */
        filter->Stray = 1;
        cmRegStray++;
    }
    KeReleaseSpinLock(&cmRegLock, irql);
    return may;
}

VOID CmRegAttAnswered(PCM_FILTER filter)
{
    KIRQL irql;

    KeAcquireSpinLock(&cmRegLock, &irql);
    if (!filter->Answered) {
        filter->Answered = 1;
        cmMarkAttOf(filter);
    }
    KeReleaseSpinLock(&cmRegLock, irql);
}

VOID CmRegAttEvent(PCM_FILTER filter, ULONG which)
{
    KIRQL irql;
    PCM_NHI d;
    PCM_MARK m;

    KeAcquireSpinLock(&cmRegLock, &irql);
    m = cmMarkFind((PVOID)filter->Common.Pdo, &d);
    if (m != NULL) {
        CmMarksCount(&d->Reenum.Marks, m, which, cmMarkNow(d));
    }
    KeReleaseSpinLock(&cmRegLock, irql);
}

/* Design record 25 section 7.6 (cm_reenum.h): the entry this attachment
 * was bound to, by serial in its domain, if that domain is still listed in
 * the same start; a loss of the host interface counts apart. */
VOID CmRegAttSurprise(PCM_FILTER filter, BOOLEAN started)
{
    KIRQL irql;
    PLIST_ENTRY e;
    PCM_NHI d;
    PCM_MARK m;
    BOOLEAN lost;

    KeAcquireSpinLock(&cmRegLock, &irql);
    for (e = cmRegDomains.Flink; e != &cmRegDomains; e = e->Flink) {
        d = CONTAINING_RECORD(CONTAINING_RECORD(e, CM_REENUM, Link), CM_NHI,
                              Reenum);
        if (d != filter->MarkDom) {
            continue;
        }
        m = CmMarksBound(&d->Reenum.Marks, d->Reenum.Starts,
                         filter->MarkStarts, filter->MarkSerial,
                         (PVOID)filter->Common.Pdo);
        if (m == NULL) {
            break;
        }
        lost = CmMarksLoss((BOOLEAN)(d->Live.State == CM_LIVE_ACTIVE),
                           (BOOLEAN)(d->Reenum.LossDrain != 0),
                           d->RecBarred, d->RecDebtMask, m->Member);
        if (CmMarksSurprise(&d->Reenum.Marks, m, started, lost,
                            cmMarkNow(d))) {
            CmLogNote(d, "cm.reenum.unexpected", m->MemberBdf);
        } else if (lost && started) {
            CmLogNote(d, "cm.reenum.unexpected.lost", m->MemberBdf);
        }
        break;
    }
    KeReleaseSpinLock(&cmRegLock, irql);
}

/*
 * The attachment's device has been removed (cm_reenum.h). A departure
 * after a query-remove is recorded only when the last attachment of this
 * driver on the PDO leaves, and posts nothing: Windows sends the same
 * query-remove and removal for a disable, a driver update and a restart
 * as for an eject, so the eject waits for CmReenumConfirm (decision M16).
 * PASSIVE_LEVEL.
 */
VOID CmRegAttRemoved(PCM_FILTER filter, BOOLEAN afterQr)
{
    KIRQL irql;
    PCM_NHI d;
    PCM_MARK m;
    PLIST_ENTRY e;
    PCM_FILTER other;
    BOOLEAN left;

    KeAcquireSpinLock(&cmRegLock, &irql);
    if (filter->AttListed) {
        RemoveEntryList(&filter->AttLink);
        InitializeListHead(&filter->AttLink);
        filter->AttListed = 0;
        if (filter->ChildCounted && filter->Bridge != NULL &&
            filter->Bridge->ChildLive != 0) {
            /* Record 22 section 7.7 part 4: a child filter this bridge's
             * BusRelations attached has seen its removal complete
             * (CmReenumRetired waits for every one). */
            filter->Bridge->ChildLive--;
            filter->ChildCounted = 0;
        }
        if (filter->Unattributed) {
            filter->Unattributed = 0;
            cmRegUnattributed--;
        }
    }
    m = cmMarkFind((PVOID)filter->Common.Pdo, &d);
    if (m != NULL) {
        CmMarksCount(&d->Reenum.Marks, m,
                     afterQr ? CM_MK_REMOVE_AFTER_QR : CM_MK_REMOVE_NO_QR,
                     cmMarkNow(d));
        left = TRUE;
        for (e = cmRegAtts.Flink; e != &cmRegAtts; e = e->Flink) {
            other = CONTAINING_RECORD(e, CM_FILTER, AttLink);
            if (other->Common.Pdo == filter->Common.Pdo) {
                left = FALSE;
            }
        }
        cmMarkAtt(d, m);
        if (afterQr && left && m->Member < d->Reenum.Members) {
            m->PendingAt = cmMarkNow(d);
            m->PendingGen = d->Reenum.Port[m->Member].ConnGen;
            d->Reenum.Departures++;
            CmMarksTouch(&d->Reenum.Marks, m, cmMarkNow(d));
        }
    }
    KeReleaseSpinLock(&cmRegLock, irql);
}

/* Each member's secondary bus, read from its header where the host
 * interface may be read (record 21 section 5.5), published under the
 * lock. PASSIVE_LEVEL, the connection-manager thread. */
static VOID cmSecBuses(PCM_NHI nhi)
{
    PCM_REENUM r;
    ULONG buses[XHCI_TBT_MAX_TUNNELS];
    ULONG ok[XHCI_TBT_MAX_TUNNELS];
    ULONG i;
    ULONG v;
    KIRQL irql;

    r = &nhi->Reenum;
    if (r->Route != CM_ROUTE_FILTER || !CmNhiHwAccess(nhi)) {
        return;
    }
    for (i = 0; i < r->Members && i < XHCI_TBT_MAX_TUNNELS; i++) {
        ok[i] = 0;
        buses[i] = 0;
        if (cmCfgRead(NULL, r->Port[i].Bdf, 0x18UL, &v) &&
            v != 0xFFFFFFFFUL) {
            buses[i] = (v >> 8) & 0xFFUL;
            ok[i] = (ULONG)(buses[i] != 0);
        }
    }
    KeAcquireSpinLock(&cmRegLock, &irql);
    for (i = 0; i < r->Members && i < XHCI_TBT_MAX_TUNNELS; i++) {
        if (ok[i]) {
            r->Port[i].SecBus = buses[i];
            r->Port[i].SecBusValid = 1;
        }
    }
    KeReleaseSpinLock(&cmRegLock, irql);
}

/* Each member's connection generation (CmNhiMemberGen): a pending
 * departure recorded under another is dropped - an unplug and re-plug the
 * connection manager saw before BusRelations reported the PDO missing.
 * PASSIVE_LEVEL, the connection-manager thread. */
static VOID cmConnGens(PCM_NHI nhi)
{
    PCM_REENUM r;
    ULONG gens[XHCI_TBT_MAX_TUNNELS];
    ULONG i;
    ULONG k;
    ULONG dropped;
    KIRQL irql;

    r = &nhi->Reenum;
    for (i = 0; i < r->Members && i < XHCI_TBT_MAX_TUNNELS; i++) {
        gens[i] = nhi->U4On ? CmNhiMemberGen(nhi, i) : 0;
    }
    dropped = 0;
    KeAcquireSpinLock(&cmRegLock, &irql);
    for (i = 0; i < r->Members && i < XHCI_TBT_MAX_TUNNELS; i++) {
        if (r->Port[i].ConnGen == gens[i]) {
            continue;
        }
        r->Port[i].ConnGen = gens[i];
        for (k = 0; k < CM_QM_MAX_ENTRIES; k++) {
            if (r->Marks.E[k].Pdo != NULL && !r->Marks.E[k].Tomb &&
                r->Marks.E[k].Member == i && r->Marks.E[k].PendingAt != 0) {
                r->Marks.E[k].PendingAt = 0;
                r->Marks.E[k].PendingGen = 0;
                CmMarksTouch(&r->Marks, &r->Marks.E[k], cmMarkNow(nhi));
                dropped++;
            }
        }
    }
    r->DeparturesDropped += dropped;
    KeReleaseSpinLock(&cmRegLock, irql);
    if (dropped != 0) {
        CmLogNote(nhi, "cm.reenum.departure.dropped", dropped);
    }
}

ULONG CmReenumMarksBuild(PCM_NHI nhi, UCHAR *out, BOOLEAN readAny,
                         ULONG readGen)
{
    PCM_REENUM r;
    CM_QM_IN in;
    KIRQL irql;
    ULONG bytes;

    r = &nhi->Reenum;
    in.EpochLo = nhi->QueryEpochLo;
    in.EpochHi = nhi->QueryEpochHi;
    in.NhiBdf = nhi->QueryBdf;
    in.SafeEjectOff = (ULONG)(r->SafeEject == 0);
    KeAcquireSpinLock(&cmRegLock, &irql);
    if (readAny) {
        CmMarksSeen(&r->Marks, readGen);
    }
    CmMarksEvict(&r->Marks, cmMarkNow(nhi));
    bytes = CmMarksBuild(out, &r->Marks, &in);
    KeReleaseSpinLock(&cmRegLock, irql);
    return bytes;
}

VOID CmReenumRemovals(PCM_NHI nhi, UCHAR *out)
{
    PCM_REENUM r;
    KIRQL irql;

    r = &nhi->Reenum;
    /* Unlisted (no route, or an inert start that never reached
     * CmReenumStart, whose table was never reset): nothing watched. */
    KeAcquireSpinLock(&cmRegLock, &irql);
    if (r->Listed && r->Route == CM_ROUTE_FILTER) {
        CmMarksRemovalBlock(out, &r->Marks, TRUE);
    } else {
        CmMarksRemovalBlock(out, NULL, FALSE);
    }
    KeReleaseSpinLock(&cmRegLock, irql);
}

VOID CmReenumLossMove(PCM_NHI nhi)
{
    KIRQL irql;

    KeAcquireSpinLock(&cmRegLock, &irql);
    nhi->RecBarred |= nhi->RecDebtMask;
    nhi->RecDebtMask = 0;
    KeReleaseSpinLock(&cmRegLock, irql);
}

VOID CmReenumConfirm(PCM_NHI nhi, const UCHAR *v, ULONG bytes)
{
    PCM_REENUM r;
    PCM_MARK m;
    PCM_FILTER f;
    ULONG gens[XHCI_TBT_MAX_TUNNELS];
    ULONG why;
    ULONG i;
    ULONG member;
    KIRQL irql;
    BOOLEAN posted;

    r = &nhi->Reenum;
    posted = FALSE;
    member = 0;
    KeAcquireSpinLock(&cmRegLock, &irql);
    for (i = 0; i < XHCI_TBT_MAX_TUNNELS; i++) {
        gens[i] = i < r->Members ? r->Port[i].ConnGen : 0;
    }
    m = NULL;
    why = CM_MK_CONF_SERIAL;
    if (r->Listed && r->Route == CM_ROUTE_FILTER) {
        m = CmMarksConfirm(&r->Marks, v, bytes, nhi->QueryEpochLo,
                           nhi->QueryEpochHi, (BOOLEAN)(nhi->U4On != 0),
                           gens, r->Members, &why);
    }
    if (m != NULL && m->Member < r->Members) {
        member = m->Member;
        f = cmRegFind(r->Port[member].Bdf);
        if (f != NULL && f->Owner == nhi) {
            /* The hand-off (CmReenumService) checks it once more: the same
             * publication, population and no function with a stack. */
            r->EjectPending |= 1UL << member;
            r->Port[member].Flags |= CM_PORT_EJECTED;
            r->Port[member].EjectSerial = f->Serial;
            r->Port[member].EjectGen = f->RelGen;
            posted = TRUE;
        } else {
            why = CM_MK_CONF_IDENTITY;
        }
        m->PendingAt = 0;
        m->PendingGen = 0;
        CmMarksTouch(&r->Marks, m, cmMarkNow(nhi));
    }
    if (posted) {
        r->Confirms++;
    } else {
        r->ConfirmsRefused++;
    }
    KeReleaseSpinLock(&cmRegLock, irql);
    if (posted) {
        CmLogNote(nhi, "cm.reenum.confirm", r->Port[member].Bdf);
    } else {
        CmLogNote(nhi, "cm.reenum.confirm.refused", why);
    }
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
 * Every CONFIGMG call is the worker's (cm_cfgwk.c): this thread only posts.
 * The members the domain owns, as the worker's `active` mask.
 */
static ULONG cmOwnedMask(PCM_NHI nhi)
{
    ULONG i;
    ULONG mask;

    mask = 0;
    for (i = 0; i < nhi->Reenum.Members; i++) {
        if (cmFlags(nhi, i) & CM_PORT_EXCLUSIVE) {
            mask |= 1UL << i;
        }
    }
    return mask;
}

ULONG CmReenumMemberFlags(PCM_NHI nhi, ULONG bdf, PULONG flags)
{
    ULONG i;

    *flags = 0;
    for (i = 0; i < nhi->Reenum.Members; i++) {
        if (nhi->Reenum.Port[i].Bdf == bdf) {
            *flags = cmFlags(nhi, i);
            return i;
        }
    }
    return XHCI_TBT_NO_MEMBER;
}

VOID CmReenumOwe(PCM_NHI nhi, ULONG i)
{
    PCM_REENUM r;

    r = &nhi->Reenum;
    if (i >= r->Members) {
        return;
    }
    /* Owed with its exposure, run by the pass's owed-change loop at once
     * once the member's gate is down: the UP before the call
     * (obligation 1), the re-enumeration with its vanished-child scan. */
    r->Port[i].Missed = 1;
    r->Port[i].MissedExpose = 1;
    r->Port[i].MissedAt = cmNow(nhi) - CM_MISSED_RETRY_MS;
    CmLogNote(nhi, "cm.reenum.owed", r->Port[i].Bdf);
}

/*
 * Every post to the worker, on the CM thread: the owned-member mask read
 * once, sent, and recorded as the one the worker holds (WkActive), so the
 * Service's comparison is always against what the worker was last given.
 */
static VOID cmWkPost(PCM_NHI nhi, ULONG reenum, ULONG vanish, BOOLEAN mark)
{
    PCM_REENUM r;

    /* No removal is posted for a member whose stor98 gate is up: the
     * callers hold that member's work (cmHeld, task 42-A.12) and post it
     * once the gate falls; the wait is the thread's, stepped on its
     * passes, never the worker's. */
    r = &nhi->Reenum;
    r->WkActive = cmOwnedMask(nhi);
    CmCfgwkPost(r->Wk, reenum, vanish, r->WkActive, mark);
}

/* ---- NT: the filters -------------------------------------------------- */

/* IoInvalidateDeviceRelations on member i through its filter: TRUE when
 * the call was made. */
static BOOLEAN cmInvalidate(PCM_NHI nhi, ULONG i, BOOLEAN activeOnly)
{
    PCM_FILTER f;
    PDEVICE_OBJECT pdo;

    /* No wait here: CmReenumResumeFailed reaches this at DISPATCH_LEVEL.
     * The thread's callers make no call for a member whose stor98 gate is
     * up (cmHeld, cmAskBaselines, CmReenumResumed; task 42-A.12); the
     * failure release runs only where the thread did not run the resume,
     * so no statement of the resume is pending then. */
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
            f->Serial != r->Port[i].AskedSerial &&
            (nhi->RecBarred & (1UL << i)) == 0) {
            serial = f->Serial;
        }
        KeReleaseSpinLock(&cmRegLock, irql);
        if (serial != 0 && CmNvlinkHeld(nhi, r->Port[i].Bdf)) {
            /* Its stor98 gate is up (task 42-A.12): asked on a later pass,
             * since AskedSerial is not recorded; the other members go on. */
            continue;
        }
        if (serial != 0) {
            /* The baseline answer may already report an endpoint: its
             * stor98 UP first (cm_nvlink.h, obligation 1). */
            (VOID)CmNvlinkExpose(nhi, r->Port[i].Bdf, TRUE,
                                 CM_NVLINK_TUNNEL_NONE);
        }
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
    ULONG written;
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
    written = 0;
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
        written = (ULONG)irp->IoStatus.Information;
        IoFreeIrp(irp);
    }
    cmRegRelease(f);
    /* A short write would leave the select where it was. */
    return (BOOLEAN)(NT_SUCCESS(status) && written == width);
}

/* ---- both routes ------------------------------------------------------ */

static VOID cmReleaseHeld(PCM_NHI nhi, BOOLEAN fail, ULONG mask);

/* The host interface there and not draining a loss: a member's held work
 * and its owed skipped change are consumed only then, by every path (the
 * exposure reads the bridge and a post would undo the loss's own
 * reconciliation; record 21 section 5.5). */
static BOOLEAN cmTransport(PCM_NHI nhi)
{
    return (BOOLEAN)(CmLiveTransport(&nhi->Live) && !nhi->Reenum.LossDrain);
}

/* Member i's exposure and its route's call: the stor98 UP first
 * (cm_nvlink.h, obligation 1), then the invalidation or the worker's
 * post (with a marking window when `mark`); with `expose` FALSE the call
 * alone (publication mode none, record 22 section 7.7). Only for a member
 * whose stor98 gate is down (cmHeld). */
static VOID cmReenumOne(PCM_NHI nhi, ULONG i, BOOLEAN removeVanished,
                        BOOLEAN fresh, ULONG tunnel, BOOLEAN mark,
                        BOOLEAN expose)
{
    PCM_REENUM r;

    r = &nhi->Reenum;
    if (expose) {
        (VOID)CmNvlinkExpose(nhi, r->Port[i].Bdf, fresh, tunnel);
    }
    if (r->Route == CM_ROUTE_FILTER) {
        if (!cmInvalidate(nhi, i, TRUE)) {
            /* Lost its filter or its exclusivity since it was read
             * active: owed when it is active again. */
            r->CallFailures++;
            r->Port[i].Missed = 1;
            r->Port[i].MissedExpose |= expose ? 1UL : 0UL;
        }
    } else if (r->Route == CM_ROUTE_CONFIGMG && r->Wk != NULL) {
        /* Posted: the worker makes the call, and retries it itself. */
        cmWkPost(nhi, 1UL << i, removeVanished ? 1UL << i : 0, mark);
        r->Calls++;
    }
}

/*
 * Task 42-A.12 (record 21 section 7.2 part 3): member i's stor98 gate. Up
 * - a statement made behind it still awaiting its acknowledgement - its
 * re-enumeration is held, collapsed with any held before (a removal of
 * vanished children or a fresh exposure asked by either is kept), and
 * TRUE is returned; CmReenumService runs it from the first pass that
 * finds the gate down. The exposure is still offered, so the tunnel it
 * names is recorded for the held one (CmNvlinkExpose publishes nothing
 * while the gate is up). Down, any work held before is folded into this
 * call's flags and FALSE is returned - unless the host interface is lost
 * (cmTransport): the loss's own call is then made alone and the held work
 * and owed change kept for the pass that finds the transport back. No
 * other member is looked at.
 */
static BOOLEAN cmHeld(PCM_NHI nhi, ULONG i, BOOLEAN *removeVanished,
                      BOOLEAN *fresh, ULONG tunnel, BOOLEAN *mark,
                      BOOLEAN *expose)
{
    PCM_REENUM r;
    ULONG before;
    BOOLEAN up;
    BOOLEAN missed;
    BOOLEAN live;

    r = &nhi->Reenum;
    *mark = FALSE;
    live = cmTransport(nhi);
    before = r->Port[i].Held;
    up = CmNvlinkHeld(nhi, r->Port[i].Bdf);
    if (up) {
        if (*expose && live) {
            (VOID)CmNvlinkExpose(nhi, r->Port[i].Bdf, *fresh, tunnel);
        }
        if ((before & (CM_HELD_REENUM | CM_HELD_LOSS)) == 0) {
            r->Holds++;
            CmLogNote(nhi, "cm.reenum.held", r->Port[i].Bdf);
        }
    }
    missed = (BOOLEAN)(r->Port[i].Missed != 0);
    if (CmNvlinkHoldReenum(&r->Port[i].Held, up, live, &missed,
                           removeVanished, fresh)) {
        /* Its publication mode held with it (record 22 section 7.7, U25):
         * exposing if this call or an owed change it took in exposes. The
         * loss's call (CM_HELD_LOSS) exposes nothing and changes no mode. */
        if (live) {
            r->Port[i].HeldExpose |= (*expose || r->Port[i].MissedExpose)
                                         ? 1UL
                                         : 0UL;
        }
        return TRUE;
    }
    if (!live) {
        return FALSE;
    }
    if (r->Port[i].HeldExpose) {
        *expose = TRUE;
        r->Port[i].HeldExpose = 0;
    }
    if (r->Port[i].Missed && !missed) {
        /* An owed skipped change paid by this call: its marking window
         * with it, as the owed call itself would open. */
        *mark = (BOOLEAN)(r->SafeEject != 0);
        r->Port[i].Missed = 0;
        r->Port[i].MissedExpose = 0;
        r->Port[i].MissedAt = cmNow(nhi);
        CmLogNote(nhi, "cm.reenum.missed", r->Port[i].Bdf);
    }
    return FALSE;
}

/* Every active member re-enumerated by the domain's route. `action` is the
 * one that asked for it: a tunnel up names the new tunnel to its own
 * member's exposure alone (CmNvlinkActionTunnel, task 42-A.6), so no other
 * member's stor98 entry is advanced; a tunnel down's and a RESYNC's name
 * none. `fresh` is CmNvlinkExpose's. */
static VOID cmReenumAll(PCM_NHI nhi, BOOLEAN removeVanished, BOOLEAN fresh,
                        const TBT_ICM_ACTION *action)
{
    PCM_REENUM r;
    ULONG i;
    ULONG flags;
    ULONG member;
    ULONG tunnel;
    BOOLEAN none;
    BOOLEAN expose;
    BOOLEAN vanish;
    BOOLEAN exposeFresh;
    BOOLEAN mark;

    r = &nhi->Reenum;
    member = XHCI_TBT_NO_MEMBER;
    if (action->Kind == TBT_ICM_ACT_TUNNEL_UP && nhi->U4On) {
        member = CmNhiActionMember(nhi, action);
    }
    none = (BOOLEAN)(action->Kind == TBT_ICM_ACT_RESYNC &&
                     (action->Why == TBT_ICM_WHY_RECOVERED ||
                      (nhi->U4On && action->Why == TBT_ICM_WHY_UNAVAILABLE)));
    for (i = 0; i < r->Members; i++) {
        flags = cmFlags(nhi, i);
        if ((flags & CM_PORT_ACTIVE) == 0) {
            /* An exclusive member not yet active (NT: its filter has not
             * published or not taken its baseline) owes this change: it
             * is re-enumerated once it becomes active (CmReenumService). */
            if (flags & CM_PORT_EXCLUSIVE) {
                r->Port[i].Missed = 1;
                r->Port[i].MissedExpose |= none ? 0UL : 1UL;
            }
            continue;
        }
        if ((nhi->RecBarred & (1UL << i)) != 0) {
            /* Record 22 section 7.7 part 4: a member behind the
             * retirement barrier gets its removal-only request alone
             * (CmReenumRetire); the change is owed to it once the
             * barrier is lifted, in its own publication mode. */
            r->Port[i].Missed = 1;
            r->Port[i].MissedExpose |= none ? 0UL : 1UL;
            continue;
        }
        tunnel = CmNvlinkActionTunnel(
            (BOOLEAN)(action->Kind == TBT_ICM_ACT_TUNNEL_UP), action->Link,
            action->Serial, member, i);
        vanish = removeVanished;
        exposeFresh = fresh;
        /* stor98's UP for what the call is about to expose (cm_nvlink.h,
         * obligation 1); none in publication mode none (record 22 section
         * 7.7, decision U25), which the USB4 profile's containment RESYNC
         * and its recovery's take - unless an owed exposing change is paid
         * by this call. */
        expose = (BOOLEAN)(!none || (r->Port[i].MissedExpose != 0 &&
                                     cmTransport(nhi)));
        /* A member whose stor98 acknowledgement is pending waits alone;
         * every other member is exposed and called now (task 42-A.12). */
        if (!cmHeld(nhi, i, &vanish, &exposeFresh, tunnel, &mark, &expose)) {
            cmReenumOne(nhi, i, vanish, exposeFresh, tunnel, mark, expose);
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
        /* Profile-neutral (cm_reenum.h, "THE USB4 PROFILE"). */
        CmNhiWaitService(nhi);
        if (want == XHCI_TBT_WANT_UP && !CmNhiActionLive(nhi, action)) {
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
    /* The USB4 profile's router-named members join the set here, before
     * Port[] is filled and the claims are published (cm_reenum.h). */
    CmNhiNamePorts(nhi);
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
    CmEjectReset(nhi);
    r->Route = CM_ROUTE_NONE;
    r->Why = CM_NOROUTE_NONE;
    r->Members = 0;
    r->EjectPending = 0;
    r->MarkOpen = 0;
    r->CfgmgOwed = 0;
    r->LossDrain = 0;
    r->LossSeen = 0;
    for (i = 0; i < XHCI_TBT_MAX_TUNNELS; i++) {
        r->KnownCount[i] = 0;
        r->Port[i].SecBus = 0;
        r->Port[i].SecBusValid = 0;
        r->Port[i].ConnGen = 0;
    }
    /* Not listed yet: no attachment reaches the table before the listing. */
    CmMarksReset(&r->Marks);
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
            r->Port[i].MissedExpose = 1;
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
    cmSecBuses(nhi);
    /* The set and its claims rebuilt: the USB4 profile's learning
     * baseline taken again over it (cm_reenum.h). */
    CmNhiSetChanged(nhi);
}

/*
 * The CONFIGMG route of a Windows 98 SE or ME domain, after the NHI's
 * start: its worker started (cm_cfgwk.c), which opens the route, runs
 * section 6.1's guards and records the members' children unmarked (a
 * device tunnelled before is not marked). The route counts as CONFIGMG at
 * once, so requests posted before the opening queue behind it; a failed
 * opening (CmReenumService reads it) takes the route away again.
 */
static VOID cmCfgmgLate(PCM_NHI nhi)
{
    PCM_REENUM r;
    KIRQL irql;
    ULONG bdf[XHCI_TBT_MAX_TUNNELS];
    ULONG i;

    r = &nhi->Reenum;
    if (!r->CfgmgOwed) {
        return;
    }
    if (r->LossSeen) {
        /* The host interface was lost before the worker started: a
         * baseline taken now would record a lost tree as healthy, and its
         * paths would be read from reset bridges. The route stays none
         * for this instance (a PnP restart starts afresh). */
        r->CfgmgOwed = 0;
        CmLogNote(nhi, "cm.reenum.9x.not-started.lost", 1);
        return;
    }
    r->CfgmgOwed = 0;
    for (i = 0; i < r->Members; i++) {
        bdf[i] = r->Port[i].Bdf;
    }
    r->WkActive = cmOwnedMask(nhi);
    r->Wk = CmCfgwkStart(nhi, bdf, r->Members, r->WkActive,
                         nhi->Values.BridgeNumbering,
                         nhi->Values.NoIrqStart);
    if (r->Wk == NULL) {
        r->Why = CM_NOROUTE_CFGMG_THUNK;
        CmLogNote(nhi, "cm.reenum.no-route", r->Why);
        return;
    }
    KeAcquireSpinLock(&cmRegLock, &irql);
    r->Route = CM_ROUTE_CONFIGMG;
    cmRegRecompute();
    KeReleaseSpinLock(&cmRegLock, irql);
    /* The per-member call workers beside it (task 42-A.12; record 21
     * section 7.2 part 0): one per member, each opening its own route.
     * The route does not depend on them. */
    r->Callwk = CmCallwkStart(nhi, r->Members);
}

/*
 * The worker's opening, read back once: a failed one takes the route away
 * (section 6.4) with its reason.
 */
static VOID cmCfgmgOpened(PCM_NHI nhi)
{
    PCM_REENUM r;
    KIRQL irql;
    ULONG state;
    ULONG why;
    ULONG version;
    ULONG i;

    r = &nhi->Reenum;
    if (r->Wk == NULL || r->Route != CM_ROUTE_CONFIGMG || r->WkOpenSeen) {
        return;
    }
    state = CmCfgwkOpenState(r->Wk, &why, &version);
    if (state == CM_CFGWK_OPEN_PENDING) {
        return;
    }
    r->WkOpenSeen = 1;
    if (state == CM_CFGWK_OPEN_OK) {
        for (i = 0; i < r->Members; i++) {
            CmLogNote(nhi, "cm.reenum.member.flags",
                      (r->Port[i].Bdf << 8) | cmFlags(nhi, i));
        }
        return;
    }
    r->Why = why == CM_CFGMG_WHY_VERSION ? CM_NOROUTE_CFGMG_VER
             : why == CM_CFGMG_WHY_ROOT  ? CM_NOROUTE_CFGMG_ROOT
             : why == CM_CFGMG_WHY_ARCH  ? CM_NOROUTE_ARCH
                                         : CM_NOROUTE_CFGMG_THUNK;
    CmLogNote(nhi, "cm.reenum.no-route", r->Why);
    KeAcquireSpinLock(&cmRegLock, &irql);
    r->Route = CM_ROUTE_NONE;
    cmRegRecompute();
    KeReleaseSpinLock(&cmRegLock, irql);
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
            if (nhi->U4On) {
                /* The USB4 profile (cm_reenum.h): a connection changed on
                 * some port, so an orderly removal seen before it belongs
                 * to an old connection and is forgotten, whatever the
                 * bridge's population shows later. */
                f->EjectSeen = 0;
            }
        }
        if (nhi->U4On) {
            r->Port[i].Flags &= ~CM_PORT_EJECTED;
        }
    }
    if (nhi->U4On && r->EjectPending != 0) {
        r->EjectPending = 0;
        r->EjectsStale++;
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
    if ((r->LossDrain || action->Why == TBT_ICM_WHY_UNAVAILABLE) &&
        r->Route == CM_ROUTE_CONFIGMG) {
        /* The host interface lost (CmReenumLossBegin): counted, ejects
         * voided above, and nothing else - no bridge polled, no request
         * posted, whatever the action's reason (one queued before the
         * loss included). The worker's CONTROLLER_UNAVAILABLE request is
         * this episode's reconciliation; an ordinary scan or
         * re-enumeration here would read the bridge headers the loss may
         * have reset, or run ahead of its removal. */
        CmLogNote(nhi, "cm.reenum.9x.unavailable.action",
                  (action->Kind << 8) | action->Why);
        return;
    }
    if (action->Kind == TBT_ICM_ACT_TUNNEL_DOWN &&
        r->Route == CM_ROUTE_CONFIGMG && r->Wk != NULL) {
        /* Whatever XhciCmReenum says: the known children are pruned to
         * those present, so a child the user removed is forgotten before
         * a re-plug reuses its handle (section 7.4). */
        CmCfgwkPrune(r->Wk);
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
        cmReenumAll(nhi, FALSE, TRUE, action);
    } else if (action->Kind == TBT_ICM_ACT_TUNNEL_DOWN) {
        /* The 9x statement decision's allowance counts from here (task
         * 43.18). */
        CmEjectDownBegin(nhi);
        state = cmPoll(nhi, action, XHCI_TBT_WANT_DOWN);
        CmLogNote(nhi, "cm.reenum.down.poll", (state << 8) | r->Poll.Last);
        if (state == XHCI_TBT_POLL_TIMEOUT) {
            /* Not read clear (another device may still be tunnelled):
             * every port is rescanned all the same. */
            r->PollTimeouts++;
        }
        /* stor98's statement, then its acknowledgement, before the removal
         * (cm_nvlink.h, obligations 2 and 3). */
        if (action->Why == TBT_ICM_WHY_UNAVAILABLE) {
            /* The host interface lost: presence lost, no quiescence
             * claimed (record 21 section 7.3). */
            CmNvlinkLost(nhi);
        } else if ((action->Why & CM_U4_WHY_BASE) == 0) {
            (VOID)CmNvlinkIcmDown(nhi, state);
        }
        /* A 9x eject transaction on the member learns of the tunnel down
         * only now, after the statement's decision was entered (the USB4
         * profile's DOWN hook entered it before this action was queued),
         * so a decision not pending then has completed: its unit's
         * function read gone after this, and after any acknowledgement
         * wait, is retired by its own removal (task 43.18). Under the
         * firmware connection manager no member is named: every member. */
        if (r->Route == CM_ROUTE_CONFIGMG &&
            action->Why != TBT_ICM_WHY_UNAVAILABLE) {
            CmEjectDown(nhi, nhi->U4On ? CmNhiActionMember(nhi, action)
                                       : XHCI_TBT_NO_MEMBER);
        }
        /* The acknowledgement before the removal is per member: a member
         * with a statement pending is held, the rest go on (cmHeld). */
        cmReenumAll(nhi, TRUE, FALSE, action);
    } else {
        cmReenumAll(nhi, TRUE, TRUE, action);
    }
    if (r->Route == CM_ROUTE_CONFIGMG && r->Wk != NULL &&
        r->SafeEject != 0 && action->Kind != TBT_ICM_ACT_TUNNEL_DOWN) {
        /* The worker's marking window (section 7.4). */
        cmWkPost(nhi, 0, 0, TRUE);
    }
    /* A tunnel's bridge may have been given its buses meanwhile. */
    cmSecBuses(nhi);
}

VOID CmReenumService(PCM_NHI nhi)
{
    PCM_REENUM r;
    KIRQL irql;
    PCM_FILTER f;
    PCM_FILTER ejHeld[XHCI_TBT_MAX_TUNNELS];
    ULONG pending;
    ULONG stale;
    ULONG mask;
    ULONG taken;
    ULONG held;
    ULONG act;
    ULONG i;
    BOOLEAN missed;
    BOOLEAN vanish;
    BOOLEAN exposeFresh;
    BOOLEAN mark;
    BOOLEAN heldExpose;

    r = &nhi->Reenum;
    /* The stor98 acknowledgement waits, stepped once per pass whatever
     * else the pass does (task 42-A.12). */
    CmNvlinkStep(nhi);
    /* The stor98 channel's per-pass work: on 98 SE and ME the rescan of a
     * bus a fresh exposure found silent; under the USB4 profile the
     * presence samples for the statement's anchor (record 22 section
     * 10.3). */
    if (CmLiveTransport(&nhi->Live)) {
        /* Not while the host interface is lost: its scans read the
         * bridges (record 21 section 5.5). */
        CmNvlinkService(nhi);
    }
    if (!r->Listed && r->RediscoverLeft != 0 && !r->LossSeen &&
        cmNow(nhi) - r->RediscoverAt >= CM_REDISCOVER_MS) {
        cmDiscoverAndList(nhi);
    }
    if (!r->Listed) {
        return;
    }
    /* The 9x eject transactions and statement decisions (task 43.18),
     * before any member's held work is looked at: a transaction retired or
     * a decision made this pass lowers its member's gate for it. */
    CmEjectService(nhi, FALSE);
    if (r->Route == CM_ROUTE_FILTER) {
        cmConnGens(nhi);
    }
    stale = 0;
    taken = 0;
    cmCfgmgLate(nhi);
    if (r->Wk != NULL) {
        mask = cmOwnedMask(nhi);
        if (mask != r->WkActive) {
            /* A claim won or lost with no request: the worker learns it
             * at once, so an operation in progress on a lost member
             * stops. */
            r->WkActive = mask;
            CmCfgwkPost(r->Wk, 0, 0, mask, FALSE);
        }
    }
    /* Each eject is checked again at the hand-off, under the lock: the
     * same publication of the bridge, still this domain's, the same
     * population, and still no function with a stack behind it. One that
     * no longer holds is dropped and logged; the hook is called after the
     * lock is released. */
    for (i = 0; i < XHCI_TBT_MAX_TUNNELS; i++) {
        ejHeld[i] = NULL;
    }
    KeAcquireSpinLock(&cmRegLock, &irql);
    pending = r->EjectPending;
    r->EjectPending = 0;
    for (i = 0; i < r->Members; i++) {
        if ((pending & (1UL << i)) == 0) {
            continue;
        }
        f = cmRegFind(r->Port[i].Bdf);
        if ((f != NULL && f->Owner == nhi && f->Unsettled) ||
            (nhi->U4On && taken)) {
            /* Deferred to a later pass: the bridge has not shown its
             * population since the last tunnel change; or, under the USB4
             * profile, one eject has already been taken this pass - its
             * hook tears a tunnel down and may take seconds, so every
             * other is checked again after the next pass's actions. */
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
        } else {
            taken = 1;
            if (r->EjectHook != NULL) {
                /* Held with the eligibility, under the lock: a function
                 * found stackless here cannot start before its tunnel is
                 * torn down (CmRegStartAllowed). The reference keeps the
                 * bridge from withdrawing (and so from a new publication)
                 * until the hook has returned. */
                f->EjectHold = 1;
                f->InFlight++;
                ejHeld[i] = f;
            }
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
            if (ejHeld[i] != NULL) {
                KeAcquireSpinLock(&cmRegLock, &irql);
                ejHeld[i]->EjectHold = 0;
                cmRegDrop(ejHeld[i]);
                KeReleaseSpinLock(&cmRegLock, irql);
            }
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
    /* Task 42-A.12: each member's work held behind its stor98 gate, run
     * from the first pass that finds the gate down - another member's
     * gate never holds it. Not while a lost host interface drains on the
     * CONFIGMG route (CmReenumAction's rule: no request posted then). */
    for (i = 0; i < r->Members; i++) {
        held = r->Port[i].Held;
        if (held == 0) {
            continue;
        }
        /* Kept while the host interface is lost, whatever the route: the
         * exposure reads the bridge and a post would undo the loss's own
         * reconciliation (record 21 section 5.5); run once it is back. */
        missed = (BOOLEAN)(r->Port[i].Missed != 0);
        act = CmNvlinkHoldRun(&r->Port[i].Held,
                              CmNvlinkHeld(nhi, r->Port[i].Bdf),
                              cmTransport(nhi),
                              (BOOLEAN)(r->Reenum != 0),
                              (BOOLEAN)((cmFlags(nhi, i) & CM_PORT_ACTIVE) !=
                                        0),
                              (BOOLEAN)((cmFlags(nhi, i) &
                                         CM_PORT_EXCLUSIVE) != 0),
                              &missed, &vanish, &exposeFresh);
        if (r->Port[i].Held == held) {
            continue;
        }
        CmLogNote(nhi, "cm.reenum.held.run",
                  ((held & 0xFFUL) << 24) | ((act & 0xFFUL) << 16) |
                      (r->Port[i].Bdf & 0xFFFFUL));
        if (act & CM_HELD_DO_RELEASE) {
            cmReleaseHeld(nhi, (BOOLEAN)((act & CM_HELD_DO_FAIL) != 0),
                          1UL << i);
        }
        if (act & CM_HELD_DO_LOSS) {
            /* The loss's own call held behind the gate (NT's unavailable
             * reconciliation): the removal alone, whatever the transport,
             * and behind the retirement barrier too - a removal with no
             * exposure, which the barrier's own request may not come to
             * replace while the host stays contained. */
            cmReenumOne(nhi, i, TRUE, FALSE, CM_NVLINK_TUNNEL_NONE, FALSE,
                        FALSE);
        }
        if (act & CM_HELD_DO_REENUM) {
            /* An owed skipped change paid by this call too: its marking
             * window with it, as the owed call itself would open; its
             * publication mode with the held work's (record 22 section
             * 7.7, U25). */
            heldExpose = (BOOLEAN)(r->Port[i].HeldExpose != 0 ||
                                   (r->Port[i].Missed && !missed &&
                                    r->Port[i].MissedExpose != 0));
            r->Port[i].HeldExpose = 0;
            mark = (BOOLEAN)(r->Port[i].Missed && !missed &&
                             r->SafeEject != 0);
            if (r->Port[i].Missed && !missed) {
                r->Port[i].MissedAt = cmNow(nhi);
                CmLogNote(nhi, "cm.reenum.missed", r->Port[i].Bdf);
            }
            if ((nhi->RecBarred & (1UL << i)) != 0) {
                /* Behind record 22 section 7.7's retirement barrier: the
                 * held call is owed until it is lifted; a loss call it
                 * carried is made now, alone, as CM_HELD_DO_LOSS is. */
                r->Port[i].Missed = 1;
                r->Port[i].MissedExpose |= heldExpose ? 1UL : 0UL;
                if (held & CM_HELD_LOSS) {
                    cmReenumOne(nhi, i, TRUE, FALSE, CM_NVLINK_TUNNEL_NONE,
                                FALSE, FALSE);
                }
            } else {
                r->Port[i].Missed = missed ? 1UL : 0UL;
                if (!missed) {
                    r->Port[i].MissedExpose = 0;
                }
                cmReenumOne(nhi, i, vanish, exposeFresh,
                            CM_NVLINK_TUNNEL_NONE, mark, heldExpose);
            }
        } else if (act & CM_HELD_DO_OWE) {
            r->Port[i].Missed = 1;
            r->Port[i].MissedExpose |= r->Port[i].HeldExpose;
            r->Port[i].HeldExpose = 0;
        }
        if ((r->Port[i].Held & CM_HELD_REENUM) == 0) {
            /* Run, owed or discarded: the held work's mode goes with it. */
            r->Port[i].HeldExpose = 0;
        }
    }
    /* A change skipped while a member was not active, now that it is;
     * the debt stays until a call is actually made, and while the host
     * interface is lost (cmTransport). */
    for (i = 0; i < r->Members && r->Reenum != 0 && cmTransport(nhi); i++) {
        if (r->Port[i].Missed && (cmFlags(nhi, i) & CM_PORT_ACTIVE) &&
            (nhi->RecBarred & (1UL << i)) == 0 &&
            cmNow(nhi) - r->Port[i].MissedAt >= CM_MISSED_RETRY_MS &&
            !CmNvlinkHeld(nhi, r->Port[i].Bdf)) {
            r->Port[i].MissedAt = cmNow(nhi);
            if (r->Port[i].MissedExpose) {
                (VOID)CmNvlinkExpose(nhi, r->Port[i].Bdf, TRUE,
                                     CM_NVLINK_TUNNEL_NONE);
            }
            if (r->Route == CM_ROUTE_FILTER) {
                if (cmInvalidate(nhi, i, TRUE)) {
                    r->Port[i].Missed = 0;
                }
            } else if (r->Route == CM_ROUTE_CONFIGMG && r->Wk != NULL) {
                /* Posted with a marking window: the re-enumeration is
                 * late, so is the marking of what it brings. */
                cmWkPost(nhi, 1UL << i, 1UL << i,
                         (BOOLEAN)(r->SafeEject != 0));
                r->Port[i].Missed = 0;
            }
            if (!r->Port[i].Missed) {
                r->Port[i].MissedExpose = 0;
            }
            if (!r->Port[i].Missed) {
                CmLogNote(nhi, "cm.reenum.missed", r->Port[i].Bdf);
            }
        }
    }
    cmCfgmgOpened(nhi);
    CmCallwkService(nhi, r->Callwk);
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
    BOOLEAN over;

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
    /* The fixed five-minute outer cap, under either profile; under USB4
     * the client's bound is logged, and a bound that with record 21's
     * 10 s would pass the cap is logged as such (cm_reenum.h). */
    r->HoldBoundMs = CmPortsHoldWatchdog(nhi->U4On ? nhi->U4ResumeBoundMs
                                                   : 0, &over);
    if (nhi->U4On) {
        CmLogNote(nhi, "cm.reenum.hold.u4-ms", nhi->U4ResumeBoundMs);
        if (over) {
            CmLogNote(nhi, "cm.reenum.hold.u4-over-cap",
                      nhi->U4ResumeBoundMs);
        }
    }
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
 * Every member in `mask` this domain holds released; `fail` takes section
 * 6.3's failure path, an invalidation of each released member. Spin locks,
 * IoCompleteRequest, PoStartNextPowerIrp and IoInvalidateDeviceRelations
 * only: callable at <= DISPATCH_LEVEL (CmReenumResumeFailed's use).
 */
static VOID cmReleaseHeld(PCM_NHI nhi, BOOLEAN fail, ULONG mask)
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
        if ((mask & (1UL << i)) == 0) {
            continue;
        }
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
    cmReleaseHeld(nhi, TRUE, 0xFFFFFFFFUL);
}

VOID CmReenumResumed(PCM_NHI nhi, BOOLEAN ok)
{
    PCM_REENUM r;
    ULONG mask;
    ULONG i;

    r = &nhi->Reenum;
    if (ok) {
        r->Resumes++;
    } else {
        r->ResumeFailures++;
    }
    CmLogNote(nhi, "cm.reenum.resumed", ok ? 1UL : 0UL);
    /* A member's release (and on failure its invalidation) waits for any
     * stor98 statement the resume's own teardown made behind it, and only
     * that member's (task 42-A.12): held, it is released by the first
     * pass that finds its gate down (CmReenumService). */
    mask = 0;
    for (i = 0; i < r->Members; i++) {
        if (r->Route == CM_ROUTE_FILTER &&
            CmNvlinkHeld(nhi, r->Port[i].Bdf)) {
            r->Port[i].Held |= CM_HELD_RELEASE | (ok ? 0 : CM_HELD_FAIL);
            CmLogNote(nhi, "cm.reenum.hold.release-held", r->Port[i].Bdf);
        } else {
            mask |= 1UL << i;
        }
    }
    cmReleaseHeld(nhi, (BOOLEAN)!ok, mask);
}

VOID CmReenumStop(PCM_NHI nhi)
{
    PCM_REENUM r;
    KIRQL irql;
    ULONG attached;
    ULONG over;
    ULONG skipped;
    ULONG refused;
    ULONG watchdogs;
    ULONG i;

    r = &nhi->Reenum;
    CmNvlinkStop(nhi);
    r->RediscoverLeft = 0;
    if (!r->Listed && r->Route == CM_ROUTE_NONE) {
        r->Members = 0;
        return;
    }
    r->Stops++;
    cmReleaseHeld(nhi, TRUE, 0xFFFFFFFFUL);
    KeAcquireSpinLock(&cmRegLock, &irql);
    if (r->Listed) {
        RemoveEntryList(&r->Link);
        r->Listed = 0;
    }
    r->EjectPending = 0;
    CmMarksReset(&r->Marks);
    cmRegRecompute();
    KeReleaseSpinLock(&cmRegLock, irql);
    if (r->Wk != NULL) {
        CM_CFGWK_COUNTERS wc;

        /* The query device lets go of the worker first (task 42-A.14). */
        CmQueryWorkerGone(nhi);
        CmCfgwkStop(r->Wk, &wc);
        r->Wk = NULL;
        r->Removed += wc.Removed;
        r->Vetoed += wc.Vetoed;
        r->Marked += wc.Marked;
        r->MarkFailures += wc.MarkFailures;
        r->CallFailures += wc.CallFailures;
        r->GoneDone += wc.GoneDone;
        r->GoneDropped += wc.GoneDropped;
        if (wc.Detached) {
            CmLogNote(nhi, "cm.reenum.9x.worker-detached", 1);
        }
        CmLogNote(nhi, "cm.brnum.9x.numbered", wc.Numbered);
        CmLogNote(nhi, "cm.brnum.9x.refused", wc.NumberRefused);
        /* Design record 28 section 7's counters, at the stop. */
        CmLogNote(nhi, "cm.reenum.9x.noirq.candidates", wc.NoIrqCandidates);
        CmLogNote(nhi, "cm.reenum.9x.noirq.attempts", wc.NoIrqAttempts);
        CmLogNote(nhi, "cm.reenum.9x.noirq.started", wc.NoIrqStarted);
        CmLogNote(nhi, "cm.reenum.9x.noirq.unstarted", wc.NoIrqUnstarted);
        CmLogNote(nhi, "cm.reenum.9x.noirq.leftovers", wc.NoIrqLeftovers);
        CmLogNote(nhi, "cm.reenum.9x.noirq.refused.left-over",
                  wc.NoIrqLeftOver);
        CmLogNote(nhi, "cm.reenum.9x.noirq.gone", wc.NoIrqGone);
        CmLogNote(nhi, "cm.reenum.9x.noirq.timeouts", wc.NoIrqTimeouts);
        CmLogNote(nhi, "cm.reenum.9x.noirq.late", wc.NoIrqLate);
        CmLogNote(nhi, "cm.reenum.9x.noirq.pin0",
                  (wc.NoIrqPin0Started << 16) | (wc.NoIrqPin0Seen & 0xFFFFUL));
    }
    if (r->Callwk != NULL) {
        /* The eject transactions detached first: one already authorised
         * to remove is handed to its worker to finish alone (task 43.18). */
        CmEjectStop(nhi);
        /* Never waited for: a worker inside a call frees the block itself
         * when the call returns (cm_callwk.c). */
        CmCallwkStop(r->Callwk);
        r->Callwk = NULL;
    }
    r->WkOpenSeen = 0;
    r->WkActive = 0;
    r->Route = CM_ROUTE_NONE;
    for (i = 0; i < r->Members; i++) {
        /* Held work belongs to this start (task 42-A.12). */
        r->Port[i].Held = 0;
    }
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
    CmLogNote(nhi, "cm.reenum.9x.unavailable", r->Unavailable);
    CmLogNote(nhi, "cm.reenum.9x.gone", r->GoneDone);
    CmLogNote(nhi, "cm.reenum.9x.gone-dropped", r->GoneDropped);
    CmLogNote(nhi, "cm.reenum.ejects", r->Ejects);
    CmLogNote(nhi, "cm.reenum.holds", r->Holds);
    CmRegCounters(&attached, &over, &skipped, &refused, &watchdogs);
    CmLogNote(nhi, "cm.reenum.child.attached", attached);
    CmLogNote(nhi, "cm.reenum.child.over", over);
    CmLogNote(nhi, "cm.reenum.child.skipped", skipped);
    CmLogNote(nhi, "cm.reenum.child.refused", refused);
    CmLogNote(nhi, "cm.reenum.hold.watchdogs", watchdogs);
    CmLogNote(nhi, "cm.reenum.child.stray", cmRegStray);
    CmLogNote(nhi, "cm.reenum.departures", r->Departures);
    CmLogNote(nhi, "cm.reenum.departures.dropped", r->DeparturesDropped);
    CmLogNote(nhi, "cm.reenum.confirms", r->Confirms);
    CmLogNote(nhi, "cm.reenum.confirms.refused", r->ConfirmsRefused);
}

/*
 * The host interface lost (cm_reenum.h). On NT the client's TUNNEL_DOWN
 * and RESYNC actions handed over just before are the reconciliation (the
 * bridge filters' invalidations); this records the episode. On 98 SE and
 * ME the CONFIGMG worker is asked for the CONTROLLER_UNAVAILABLE
 * reconciliation (CmCfgwkUnavailable): the children it retained at the
 * members' last healthy walk removed without reading a bridge header,
 * before any further re-enumeration of those members. XhciCmReenum 0
 * asks for nothing. Nothing here states quiescence: no endpoint
 * notification is made (presence lost only). IRQL: PASSIVE_LEVEL.
 */
VOID CmReenumLossBegin(PCM_NHI nhi)
{
    nhi->Reenum.LossDrain = 1;
    CmNvlinkDropLate(nhi);
    if (nhi->Reenum.Wk == NULL) {
        /* No 98 worker running (not started yet, or the set still being
         * looked for): none is started, and no rediscovery is made, for
         * this instance (cmCfgmgLate, CmReenumService). */
        nhi->Reenum.LossSeen = 1;
    }
}

VOID CmReenumLossEnd(PCM_NHI nhi)
{
    nhi->Reenum.LossDrain = 0;
}

VOID CmReenumUnavailable(PCM_NHI nhi, ULONG episode)
{
    PCM_REENUM r;

    r = &nhi->Reenum;
    r->LossDrain = 0;
    CmLogNote(nhi, "cm.reenum.unavailable", episode);
    if (r->Route != CM_ROUTE_CONFIGMG || r->Wk == NULL || r->Reenum == 0) {
        return;
    }
    r->WkActive = cmOwnedMask(nhi);
    CmCfgwkUnavailable(r->Wk, r->WkActive);
    r->Unavailable++;
    CmLogNote(nhi, "cm.reenum.9x.unavailable.members", r->WkActive);
}
/* ---- record 22 section 7.7: the retirement barrier ------------------- */

BOOLEAN CmReenumRetire(PCM_NHI nhi, ULONG i, PULONG answers)
{
    PCM_REENUM r;
    PCM_FILTER f;
    PDEVICE_OBJECT pdo;
    KIRQL irql;

    r = &nhi->Reenum;
    *answers = 0;
    if (i >= r->Members) {
        return FALSE;
    }
    if (CmNvlinkHeld(nhi, r->Port[i].Bdf)) {
        /* A stor98 statement behind this member still awaits its
         * acknowledgement, or a 9x eject transaction or decision holds
         * it: no removal call before it falls (tasks 42-A.12, 43.18);
         * asked again at a later pass. */
        return FALSE;
    }
    if (r->Route == CM_ROUTE_FILTER) {
        if (!cmRegTake(nhi, i, FALSE, &f, &pdo)) {
            return FALSE;
        }
        /* A change: an answer to a query already in flight does not
         * count for it (CmRegRelations' seq rule). */
        KeAcquireSpinLock(&cmRegLock, &irql);
        f->ChangeSeq++;
        f->Unsettled = 1;
        *answers = f->RelAnswers;
        KeReleaseSpinLock(&cmRegLock, irql);
        IoInvalidateDeviceRelations(pdo, BusRelations);
        cmRegRelease(f);
        r->Port[i].Reenums++;
        r->Calls++;
        CmLogNote(nhi, "cm.reenum.retire", (i << 16) | (*answers & 0xFFFFUL));
        return TRUE;
    }
    if (r->Route == CM_ROUTE_CONFIGMG && r->Wk != NULL) {
        CmCfgwkHold(r->Wk, nhi->RecBarred);
        if ((CmCfgwkEnumBusy(r->Wk) & (1UL << i)) != 0) {
            /* A re-enumeration admitted before the hold has not returned:
             * the removal follows it, never precedes it (asked again at
             * the next pass). */
            return FALSE;
        }
        r->WkActive = cmOwnedMask(nhi);
        CmCfgwkGone(r->Wk, 1UL << i, r->WkActive);
        CmLogNote(nhi, "cm.reenum.9x.retire", i);
        return TRUE;
    }
    return FALSE;
}

BOOLEAN CmReenumRetired(PCM_NHI nhi, ULONG i, ULONG answers)
{
    PCM_REENUM r;
    PCM_FILTER f;
    PDEVICE_OBJECT pdo;
    BOOLEAN done;
    KIRQL irql;

    r = &nhi->Reenum;
    if (i >= r->Members) {
        return FALSE;
    }
    if (r->Route == CM_ROUTE_FILTER) {
        if (!cmRegTake(nhi, i, FALSE, &f, &pdo)) {
            return FALSE;
        }
        KeAcquireSpinLock(&cmRegLock, &irql);
        /* Where a child filter of this driver sat on the endpoint's PDO,
         * its own observation of the removal (ChildLive back to 0) as
         * well as the settled answer that no longer reports it. */
        done = (BOOLEAN)(f->RelEmptyAt != 0 && f->RelEmptyAt != answers &&
                         f->RelEmptyAt - answers < 0x80000000UL &&
                         f->SnapCount == 0 && !f->SnapTruncated &&
                         f->ChildLive == 0 &&
                         !(f->TruncSeen && cmRegUnattributed != 0));
        KeReleaseSpinLock(&cmRegLock, irql);
        cmRegRelease(f);
        return done;
    }
    if (r->Route == CM_ROUTE_CONFIGMG && r->Wk != NULL) {
        return (BOOLEAN)((CmCfgwkRetired(r->Wk) & (1UL << i)) != 0);
    }
    return FALSE;
}

VOID CmReenumHold(PCM_NHI nhi, ULONG members)
{
    if (nhi->Reenum.Route == CM_ROUTE_CONFIGMG && nhi->Reenum.Wk != NULL) {
        CmCfgwkHold(nhi->Reenum.Wk, members);
    }
}

BOOLEAN CmReenumSourcesHold(PCM_NHI nhi)
{
    PCM_REENUM r;
    XHCI_TBT_TOPO_IN in;
    const CM_FAMILY *family;
    ULONG i;
    ULONG k;

    r = &nhi->Reenum;
    /* cmDiscover's inputs, as the start gave them. */
    CmZero(&in, sizeof(in));
    in.Nhi = r->Nhi;
    if (nhi->Values.TunnelBridgeChars != 0) {
        in.Overrides = XhciTbtParseBdfList(nhi->Values.TunnelBridge,
                                           nhi->Values.TunnelBridgeChars,
                                           in.Override,
                                           XHCI_TBT_MAX_TUNNELS);
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
    if (XhciTbtTopoFind(&r->Cfg, &in, &r->RecTopo) ==
        XHCI_TBT_TOPO_INCOMPLETE) {
        return FALSE;
    }
    for (i = 0; i < r->Topo.Tunnels && i < XHCI_TBT_MAX_TUNNELS; i++) {
        if (r->Topo.Source[i] == XHCI_TBT_SRC_ROUTER) {
            continue;
        }
        for (k = 0; k < r->RecTopo.Tunnels && k < XHCI_TBT_MAX_TUNNELS; k++) {
            if (r->RecTopo.Set[k] == r->Topo.Set[i] &&
                r->RecTopo.Source[k] == r->Topo.Source[i]) {
                break;
            }
        }
        if (k == r->RecTopo.Tunnels || k == XHCI_TBT_MAX_TUNNELS) {
            CmLogNote(nhi, "u4.recover.member.source-lost",
                      (r->Topo.Set[i] << 8) | r->Topo.Source[i]);
            return FALSE;
        }
    }
    return TRUE;
}
