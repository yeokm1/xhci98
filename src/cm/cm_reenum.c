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
 * new since its snapshot, on every bridge it filters (design record 31
 * section 4.1: the child is also the tunnel id's vehicle, cm_filt.c
 * cmChildIds, and a child on a PCI-to-PCI bridge reads that bridge's
 * BusRelations in turn, CmRegChainRelations, to CM_TUNNEL_DEPTH_MAX); the
 * child answers IRP_MN_QUERY_CAPABILITIES with Removable (cm_filt.c) only
 * as a unit of an active, exclusive member with XhciCmSafeEject 1
 * (CmRegAttMayMark).
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
 * thread; the CmReg* calls as cm_reenum.h states.
 */

#include "cm.h"
#include "cm_cfgmg.h"
#include "cm_ports.h"
#include "cm_nvlink.h"
#include "cm_call.h"
#include "cm_iso.h"

/* The wake power-up (design record 21 section 6.3.1, task 49.46), at this
 * file's tail. */
static VOID cmWpuService(PCM_NHI nhi);
static VOID cmWpuPortReleased(PCM_NHI nhi, ULONG i);

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
/* Design record 34 section 3.12 item 5: one stamp counter for the whole
 * list, never 0, never reused in the driver's life (compared as elapsed
 * counts): a chain child PDO's identity, a chain child's BusRelations
 * begin stamp, a transaction's arm and learning. Under the lock. */
static ULONG cmRegSeq;
static ULONG cmRegRefused;
static ULONG cmRegOver;
static ULONG cmRegPassed;
/* Every attachment of this driver on a device (task 42-A.5, cm_reenum.h's
 * "The marks"), and the LowerFilters children no active member owned. */
static LIST_ENTRY cmRegAtts;
static ULONG cmRegStray;
/* Record 21 section 7.7: AddDevice children that answered Removable while
 * no domain was listed (a start of Windows before the NHI's), under the
 * lock. */
static ULONG cmRegPreTrust;
/* The window for those answers: open from CmRegInit until the first domain
 * lists (cmRegEverListed, never cleared) or CM_PRETRUST_MS pass, so an NHI
 * stopped, inert or never started opens nothing. Under the lock. */
static ULONG cmRegEverListed;
/* An AddDevice child that could not be made: a mark of the service's may
 * then sit on a PDO with no attachment showing it, so the window closes
 * (cmRegUnitOfFilter could take a marked bridge for an unmarked one). */
static ULONG cmRegChildFailed;
static LARGE_INTEGER cmRegInitTicks;
#define CM_PRETRUST_MS 300000UL
/* The tunnel id's chain (design record 31 sections 4.2 and 4.3), under
 * the lock: ids given, rewrites refused for want of pool and over the
 * size bound, bridges past the depth bound, and re-attaches. */
static ULONG cmRegIdGiven;
static ULONG cmRegIdNomem;
static ULONG cmRegIdOver;
static ULONG cmRegChainDeep;
static ULONG cmRegReattached;

static NTSTATUS cmChildCreate(PCM_FILTER parent, PDEVICE_OBJECT pdo,
                              ULONG isNew, PULONG over, PULONG again);
static VOID cmDiscoverAndList(PCM_NHI nhi);
static VOID cmMarksSyncAll(VOID);
static VOID cmChainClassify(PCM_FILTER child);

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
    KeQueryTickCount(&cmRegInitTicks);
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
}

VOID CmRegCounters(PULONG attached, PULONG over, PULONG skipped,
                   PULONG refused, PULONG passed)
{
    KIRQL irql;

    KeAcquireSpinLock(&cmRegLock, &irql);
    *attached = cmRegAttached;
    *over = cmRegOver;
    *skipped = cmRegSkipped;
    *refused = cmRegRefused;
    *passed = cmRegPassed;
    KeReleaseSpinLock(&cmRegLock, irql);
}

/* Bumped at every claim made or withdrawn (record 22 section 7.6.1): a
 * domain with no set discovers again when it moves. Under the lock. */
static ULONG cmRegAcpiGen;

static PCM_FILTER cmRegFind(ULONG bdf);

/* TRUE when the filter at `bdf` holds a current claim for `name`, made
 * under `serial`, and it is the only current claim naming `name` (record
 * 22 section 7.6.1: one ACPI-named port per NHI). Under the lock. */
static BOOLEAN cmRegAcpiSole(ULONG bdf, ULONG serial, const char *name)
{
    PLIST_ENTRY e;
    PCM_FILTER f;
    PCM_FILTER own;
    ULONG claims;

    own = cmRegFind(bdf);
    if (own == NULL || !own->AcpiClaim || own->AcpiSerial != serial ||
        !CmDsdSameName(own->AcpiRef, name)) {
        return FALSE;
    }
    claims = 0;
    for (e = cmRegFilters.Flink; e != &cmRegFilters; e = e->Flink) {
        f = CONTAINING_RECORD(e, CM_FILTER, RegLink);
        if (f->Published && !f->Gone && f->AcpiClaim &&
            CmDsdSameName(f->AcpiRef, name)) {
            claims++;
        }
    }
    return (BOOLEAN)(CmDsdClaimsTaken(claims) == 1);
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
            if (r->Port[i].Source == XHCI_TBT_SRC_ACPI) {
                /* Active only while its filter's claim, made under the
                 * publication the member was added from, still names this
                 * NHI (record 22 section 7.6.1): otherwise not exclusive,
                 * as a port another domain claims. */
                if (!cmRegAcpiSole(r->Port[i].Bdf, r->Port[i].AcpiSerial,
                                   r->AcpiName)) {
                    flags |= CM_PORT_NO_CLAIM;
                    claimed = 1;
                }
            }
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

VOID CmRegSetClaim(PCM_FILTER filter, ULONG rc, const char *ref, ULONG port)
{
    KIRQL irql;
    ULONG i;

    KeAcquireSpinLock(&cmRegLock, &irql);
    if (filter->Published && !filter->Gone) {
        filter->AcpiRead = 1;
        filter->AcpiRc = rc;
        filter->AcpiClaim = 0;
        if (rc == CM_DSD_OK && ref != NULL) {
            for (i = 0; i < CM_DSD_NAME_MAX && ref[i] != 0; i++) {
                filter->AcpiRef[i] = ref[i];
            }
            filter->AcpiRef[i] = 0;
            filter->AcpiPort = port;
            filter->AcpiSerial = filter->Serial;
            filter->AcpiClaim = 1;
            cmRegAcpiGen++;
            cmRegRecompute();
        }
    }
    KeReleaseSpinLock(&cmRegLock, irql);
}

VOID CmRegWithdraw(PCM_FILTER filter)
{
    KIRQL irql;
    ULONG wait;

    KeAcquireSpinLock(&cmRegLock, &irql);
    if (!filter->Published) {
        KeReleaseSpinLock(&cmRegLock, irql);
        return;
    }
    if (filter->AcpiClaim) {
        cmRegAcpiGen++;
    }
    filter->AcpiClaim = 0;
    filter->AcpiRead = 0;
    filter->AcpiRc = 0;
    filter->Gone = 1;
    filter->Published = 0;
    filter->Baseline = 0;
    RemoveEntryList(&filter->RegLink);
    InitializeListHead(&filter->RegLink);
    filter->Held = 0;
    filter->HeldBy = NULL;
    wait = filter->InFlight;
    cmRegRecompute();
    KeReleaseSpinLock(&cmRegLock, irql);
    if (wait != 0) {
        (VOID)CmWaitEvent(&filter->Rundown, 0);
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

/* The filter whose registration a child's records and counts go under:
 * a bridge filter is its own, a child names the tunnel-port bridge filter
 * its chain started from (CM_FILTER.Bridge, NULL when none is known). */
static PCM_FILTER cmTopOf(PCM_FILTER f)
{
    return f->Common.Kind == CM_KIND_FILTER ? f : f->Bridge;
}

/* The domain a chain record is written to: the top bridge's owner, else
 * the first listed domain on the filter route (a bridge that is not
 * active has no owner, and the chain's reading is wanted all the same),
 * else none: the counts alone. Under the lock. */
static PCM_NHI cmLogDom(PCM_FILTER top)
{
    PLIST_ENTRY e;
    PCM_REENUM r;

    if (top != NULL && top->Owner != NULL) {
        return top->Owner;
    }
    for (e = cmRegDomains.Flink; e != &cmRegDomains; e = e->Flink) {
        r = CONTAINING_RECORD(e, CM_REENUM, Link);
        if (r->Route == CM_ROUTE_FILTER) {
            return CONTAINING_RECORD(r, CM_NHI, Reenum);
        }
    }
    return NULL;
}

/* A filter's location for the chain's records: a published bridge
 * filter's, a located attachment's, else 0xFFFF. Under the lock. */
static ULONG cmFiltBdf(PCM_FILTER f)
{
    if (f == NULL) {
        return 0xFFFFUL;
    }
    if (f->Common.Kind == CM_KIND_FILTER && f->Published) {
        return f->Bdf & 0xFFFFUL;
    }
    if (f->Located) {
        return XHCI_TBT_BDF(f->LocBus, f->LocAddress >> 16,
                            f->LocAddress & 0xFFFFUL) & 0xFFFFUL;
    }
    return 0xFFFFUL;
}

/* One BusRelations answer read by `f`, a bridge filter or a chain child,
 * as the chain's trace records it: the count (bits 31:24), the depth
 * (23:16) and the location, then how many PDOs are new by its snapshot. */
static VOID cmChainRelNote(PCM_FILTER f, ULONG count, ULONG fresh)
{
    KIRQL irql;
    PCM_NHI dom;

    KeAcquireSpinLock(&cmRegLock, &irql);
    dom = cmLogDom(cmTopOf(f));
    if (dom != NULL) {
        CmLogNote(dom, "cm.chain.rel", ((count & 0xFFUL) << 24) |
                                           ((f->Depth & 0xFFUL) << 16) |
                                           cmFiltBdf(f));
        CmLogNote(dom, "cm.chain.rel.new", fresh);
    }
    KeReleaseSpinLock(&cmRegLock, irql);
}

/*
 * A child filter on the PDOs of a BusRelations answer `parent` read - a
 * bridge filter, or a chain child on a PCI-to-PCI bridge (design record
 * 31 sections 4.1 and 4.3): on each PDO new by a whole snapshot, and
 * again on one whose child was lost to a rebuild of its stack
 * (CmPortsChildFirst's re-attach), on every filtered bridge whatever its
 * active mark and XhciCmSafeEject say. What a child answers is the marks'
 * gate's (CmRegAttMayMark). PASSIVE_LEVEL, the parent's PnP dispatch,
 * before the snapshot is replaced.
 */
static VOID cmAttachNew(PCM_FILTER parent, PVOID const *now, ULONG count,
                        const ULONG *isNew)
{
    ULONG i;
    ULONG over;
    ULONG again;
    KIRQL irql;
    NTSTATUS status;

    for (i = 0; i < count; i++) {
        status = cmChildCreate(parent, (PDEVICE_OBJECT)now[i], isNew[i],
                               &over, &again);
        if (status == STATUS_NOT_FOUND) {
            continue;
        }
        KeAcquireSpinLock(&cmRegLock, &irql);
        if (NT_SUCCESS(status)) {
            cmRegAttached++;
            if (over) {
                cmRegOver++;
            }
            if (again) {
                cmRegReattached++;
            }
        } else if (status == STATUS_DEVICE_BUSY) {
            cmRegSkipped++;
        } else {
            cmRegRefused++;
        }
        KeReleaseSpinLock(&cmRegLock, irql);
    }
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
    KIRQL irql;

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
    cmChainRelNote(filter, count, fresh);
    cmAttachNew(filter, (PVOID const *)now, count, isNew);

    /* The snapshot is replaced under the lock, since the eject's
     * eligibility (cmEjectReady) reads it from the CM thread
     * (CmReenumService). A
     * function newly answering starts a new generation (RelGen,
     * CmPortsNewGeneration), which an eject posted against the old one
     * fails at its hand-off; a departure alone keeps it; a new function
     * forgets an orderly removal seen before it arrived (EjectSeen), so a
     * replacement is never torn down for its predecessor's eject. The
     * answer settles the bridge after a tunnel change only if the query
     * began after that change (`seq`, CmRegRelationsBegin); an answer that
     * overlapped one leaves it unsettled for the next query, which the
     * change's own invalidation brings. */
    KeAcquireSpinLock(&cmRegLock, &irql);
    if (CmPortsNewGeneration(fresh, filter->SnapCount, count)) {
        filter->RelGen++;
    } else if (count != filter->SnapCount && filter->Owner != NULL) {
        /* A departure alone (task 43.70): under native hot-plug the
         * ejected function leaves the answer only after Windows has turned
         * the slot off, after the confirmation was posted; the
         * generation is kept, so that eject is still the bridge's. */
        CmLogNote(filter->Owner, "cm.reenum.departure", filter->Bdf);
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
 * A child filter above a PDO pci.sys reported to `parent` (cmAttachNew),
 * before the PnP manager builds that device's stack (record 21 section 7.4
 * step 1): directly above the PDO, or (revision 2) above the bus filters a
 * driver below the parent already put on it, which is where an ACPI
 * machine leaves a PCI-to-PCI bridge; CmPortsChildAttach decides. A PDO
 * already in the snapshot (`isNew` 0) gets one again only when nothing at
 * all is on it and no attachment of this driver is (record 31 section
 * 4.3's re-attach; CmPortsChildFirst), and its chain is never walked.
 * STATUS_SUCCESS, with *over 1 when it went above bus filters and *again 1
 * for a re-attach; STATUS_DEVICE_BUSY when anything else is attached, so
 * nothing is; STATUS_NOT_FOUND when nothing was to be done. The child is
 * one deeper than its parent and names the parent's tunnel-port bridge
 * filter (cmTopOf). PASSIVE_LEVEL.
 */
static NTSTATUS cmChildCreate(PCM_FILTER parent, PDEVICE_OBJECT pdo,
                              ULONG isNew, PULONG over, PULONG again)
{
    PDEVICE_OBJECT self;
    PDEVICE_OBJECT lower;
    PDEVICE_OBJECT walk;
    PCM_FILTER child;
    PCM_FILTER f;
    PLIST_ENTRY e;
    PVOID chain[CM_CHILD_CHAIN_MAX + 1];
    ULONG chainCount;
    ULONG decision;
    PCM_FILTER top;
    PCM_NHI dom;
    ULONG name0;
    ULONG name1;
    KIRQL irql;
    ULONG truncated;
    ULONG hasOurs;
    NTSTATUS status;

    *over = 0;
    *again = 0;
    chainCount = 0;
    name0 = 0;
    name1 = 0;
    hasOurs = 0;
    top = cmTopOf(parent);
    /* SnapTruncated is written by the parent's PnP dispatch alone, which
     * is where this runs, so the value read stays the value. */
    KeAcquireSpinLock(&cmRegLock, &irql);
    truncated = parent->SnapTruncated;
    for (e = cmRegAtts.Flink; e != &cmRegAtts && !isNew; e = e->Flink) {
        f = CONTAINING_RECORD(e, CM_FILTER, AttLink);
        if (f->Common.Pdo == pdo) {
            hasOurs = 1;
            break;
        }
    }
    KeReleaseSpinLock(&cmRegLock, irql);
    walk = pdo->AttachedDevice;
    decision = CmPortsChildFirst(truncated, (ULONG)(walk != NULL), isNew,
                                 hasOurs);
    if (decision == CM_CHILD_KEEP) {
        return STATUS_NOT_FOUND;
    }
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
    /* The domain is used under the registration list's lock only: its
     * delisting (CmReenumStop) takes the same lock, so the NHI cannot
     * leave while its log is written. Order: cmRegLock, then LogLock. */
    KeAcquireSpinLock(&cmRegLock, &irql);
    dom = cmLogDom(top);
    if (dom != NULL) {
        if (chainCount != 0) {
            CmLogNote(dom, "cm.reenum.child.under", name0);
            CmLogNote(dom, "cm.reenum.child.under2", name1);
            CmLogNote(dom, "cm.reenum.child.under.count", chainCount);
            CmLogNote(dom, "cm.reenum.child.decision", decision);
        }
        CmLogNote(dom, isNew ? "cm.chain.attach" : "cm.chain.reattach",
                  ((decision & 0xFFUL) << 24) |
                      (((parent->Depth + 1) & 0xFFUL) << 16) |
                      cmFiltBdf(parent));
        CmLogNoteAddress(dom, "cm.chain.attach.pdo", (ULONG)(ULONG_PTR)pdo);
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
    CmCommonInit(&child->Common);
    child->Common.Pdo = pdo;
    child->Outstanding = 1;
    child->Bridge = top;
    child->Parent = parent;
    child->Depth = parent->Depth + 1;

    KeInitializeSpinLock(&child->Lock);
    KeInitializeEvent(&child->Drained, NotificationEvent, FALSE);
    CmWpuChildInit(child);
    CmRegFilterInit(child);
    if (CmKeptOn) {
        /* Design record 31 section 4.10: kept, before the attach, after
         * which the callback can come. */
        CmKeptInit(child);
    }
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
    *again = (ULONG)(isNew == 0);
    return STATUS_SUCCESS;
}

/* The next stamp of the list's counter. Under the lock. */
static ULONG cmRegSeqNext(VOID)
{
    cmRegSeq++;
    if (cmRegSeq == 0) {
        cmRegSeq = 1;
    }
    return cmRegSeq;
}

ULONG CmRegChainBegin(PCM_FILTER child)
{
    KIRQL irql;
    ULONG seq;

    UNREFERENCED_PARAMETER(child);
    KeAcquireSpinLock(&cmRegLock, &irql);
    seq = cmRegSeqNext();
    KeReleaseSpinLock(&cmRegLock, irql);
    return seq;
}

ULONG CmReenumChainStamp(PCM_NHI nhi)
{
    KIRQL irql;
    ULONG seq;

    UNREFERENCED_PARAMETER(nhi);
    KeAcquireSpinLock(&cmRegLock, &irql);
    seq = cmRegSeqNext();
    KeReleaseSpinLock(&cmRegLock, irql);
    return seq;
}

VOID CmRegChainRelations(PCM_FILTER child, const DEVICE_RELATIONS *relations,
                         ULONG beginSeq)
{
    PVOID now[CM_SNAP_MAX];
    ULONG isNew[CM_SNAP_MAX];
    ULONG locd[CM_SNAP_MAX];
    ULONG bus[CM_SNAP_MAX];
    ULONG addr[CM_SNAP_MAX];
    ULONG stamp[CM_SNAP_MAX];
    ULONG count;
    ULONG fresh;
    ULONG truncated;
    ULONG i;
    ULONG j;
    KIRQL irql;

    count = 0;
    if (relations != NULL) {
        count = relations->Count;
    }
    truncated = (ULONG)(count > CM_SNAP_MAX);
    if (truncated) {
        KeAcquireSpinLock(&cmRegLock, &irql);
        cmRegSkipped += count - CM_SNAP_MAX;
        KeReleaseSpinLock(&cmRegLock, irql);
        count = CM_SNAP_MAX;
    }
    for (i = 0; i < count; i++) {
        now[i] = (PVOID)relations->Objects[i];
    }
    fresh = CmPortsNewPdos((PVOID const *)child->Snap, child->SnapCount,
                           (PVOID const *)now, count, isNew);
    cmChainRelNote(child, count, fresh);
    /* Before the new PDOs are attached and the answer goes up: Windows
     * asks a new PDO's capabilities only after this answer, so its entry
     * and its parent's classification exist by then (record 34 section
     * 3.10). */
    cmChainClassify(child);
    cmAttachNew(child, (PVOID const *)now, count, isNew);
    KeAcquireSpinLock(&cmRegLock, &irql);
    for (i = 0; i < count; i++) {
        /* A PDO that stays keeps the location kept for it. */
        locd[i] = 0;
        bus[i] = 0;
        addr[i] = 0;
        /* Record 34 section 3.12 item 5: a PDO new to this snapshot takes
         * this request's begin stamp, but in the filter's first answer
         * (no provenance); one that stays keeps its own. */
        stamp[i] = (isNew[i] && child->ChainAnswered) ? beginSeq : 0;
        for (j = 0; j < child->SnapCount && !isNew[i]; j++) {
            if (child->Snap[j] == now[i]) {
                stamp[i] = child->SnapSeq[j];
                if (child->SnapLocd[j]) {
                    locd[i] = 1;
                    bus[i] = child->SnapBus[j];
                    addr[i] = child->SnapAddr[j];
                }
            }
        }
    }
    for (i = 0; i < count; i++) {
        child->Snap[i] = now[i];
        child->SnapLocd[i] = locd[i];
        child->SnapBus[i] = bus[i];
        child->SnapAddr[i] = addr[i];
        child->SnapSeq[i] = stamp[i];
    }
    child->SnapCount = count;
    child->SnapTruncated = truncated;
    if (fresh != 0) {
        /* A new PDO reported: a kid eject posted against the population
         * before it is stale at its hand-off (record 34 section 3.12
         * item 7). */
        child->RelGen++;
    }
    child->ChainAnswered = 1;
    child->ChainAnsSeq = beginSeq;
    cmMarksSyncAll();
    KeReleaseSpinLock(&cmRegLock, irql);
}

VOID CmRegChainNote(PCM_FILTER f, ULONG event)
{
    static const char *const labels[] = {
        "cm.chain.id", "cm.chain.id.nomem", "cm.chain.id.over",
        "cm.chain.deep", "cm.chain.bridge", "cm.chain.start",
        "cm.chain.remove"
    };
    KIRQL irql;
    PCM_NHI dom;
    PCM_FILTER at;

    if (event >= sizeof(labels) / sizeof(labels[0])) {
        return;
    }
    KeAcquireSpinLock(&cmRegLock, &irql);
    switch (event) {
    case CM_CHAIN_EV_ID:
        cmRegIdGiven++;
        break;
    case CM_CHAIN_EV_NOMEM:
        cmRegIdNomem++;
        break;
    case CM_CHAIN_EV_OVER:
        cmRegIdOver++;
        break;
    case CM_CHAIN_EV_DEEP:
        cmRegChainDeep++;
        break;
    default:
        break;
    }
    /* The bridge the device sits on: the filter that attached this one,
     * or its own location for one AddDevice made. The parent outlives the
     * child: the PnP manager removes a device's children before it. */
    at = f->Parent != NULL ? f->Parent : f;
    dom = cmLogDom(cmTopOf(f));
    if (dom != NULL) {
        CmLogNote(dom, labels[event], ((f->Depth & 0xFFUL) << 16) |
                                          cmFiltBdf(at));
        if (event == CM_CHAIN_EV_ID || event == CM_CHAIN_EV_NOMEM) {
            CmLogNoteAddress(dom, "cm.chain.id.pdo",
                             (ULONG)(ULONG_PTR)f->Common.Pdo);
        }
    }
    KeReleaseSpinLock(&cmRegLock, irql);
}

/* ====================================================================== */
/* A child's interrupt (design record 21 section 6.2.1, task 44.53)      */
/* ====================================================================== */

/* TRUE when `bdf` (16-bit XHCI_TBT_BDF) is an active member of a filter
 * route's set. Under the list's lock. */
static BOOLEAN cmRegIsActiveMember(ULONG bdf)
{
    PLIST_ENTRY e;
    PCM_REENUM r;
    ULONG i;

    for (e = cmRegDomains.Flink; e != &cmRegDomains; e = e->Flink) {
        r = CONTAINING_RECORD(e, CM_REENUM, Link);
        if (r->Route != CM_ROUTE_FILTER) {
            continue;
        }
        for (i = 0; i < r->Members; i++) {
            if ((r->Port[i].Bdf & 0xFFFFUL) == bdf &&
                (r->Port[i].Flags & CM_PORT_ACTIVE) != 0) {
                return TRUE;
            }
        }
    }
    return FALSE;
}

/* A bridge's secondary bus: TRUE with *sec when `bdf` answers as a present
 * type 1 header (not all-ones, not Retry Status) with a secondary bus that
 * is neither 0 nor FFh. PASSIVE_LEVEL. */
static BOOLEAN cmIrqBridgeSec(ULONG bdf, PULONG sec)
{
    ULONG id;
    ULONG v;

    if (!cmCfgRead(NULL, bdf, 0x00UL, &id) ||
        (id & 0xFFFFUL) == 0xFFFFUL || (id & 0xFFFFUL) == 0x0001UL ||
        !cmCfgRead(NULL, bdf, 0x0CUL, &v) || ((v >> 16) & 0x7FUL) != 1UL ||
        !cmCfgRead(NULL, bdf, 0x18UL, &v)) {
        return FALSE;
    }
    *sec = (v >> 8) & 0xFFUL;
    return (BOOLEAN)(*sec != 0 && *sec != 0xFFUL);
}

ULONG CmRegIrqWire(PCM_FILTER child, ULONG address, PULONG why,
                   PULONG fnBdf)
{
    ULONG sec;
    ULONG path[CM_IRQ_PATH_MAX];
    ULONG devs[CM_IRQ_PATH_MAX];
    ULONG n;
    ULONG port;
    ULONG bdf;
    ULONG k;
    ULONG v;
    ULONG id;
    ULONG pin;
    ULONG portPin;
    ULONG portLine;
    BOOLEAN found;
    PCM_FILTER f;
    PCM_FILTER a;
    KIRQL irql;

    /* The ancestry: each ancestor's location, up to the first that is an
     * active tunnel port; path[0] is kept for the function itself, whose
     * bus is read below from the bridge above it. A chain child's Parent
     * is the filter whose BusRelations attached it; a child AddDevice made
     * has none, and its Bridge is the bridge whose snapshot reported it.
     * Each outlives the child (its device's children are removed before
     * it), and only the locations are kept past the lock. */
    *fnBdf = 0xFFFFUL;
    if (((address >> 16) & 0xFFFFUL) > 31UL || (address & 0xFFFFUL) > 7UL) {
        *why = CM_IRQ_WHY_UNREAD;
        return 0;
    }
    n = 1;
    path[0] = 0;
    port = 0;
    found = FALSE;
    KeAcquireSpinLock(&cmRegLock, &irql);
    f = child;
    for (;;) {
        a = f->Parent;
        if (a == NULL && f->Common.Kind == CM_KIND_CHILD) {
            a = f->Bridge;
        }
        if (a == NULL) {
            break;
        }
        bdf = cmFiltBdf(a);
        if (bdf == 0xFFFFUL) {
            break;
        }
        if (cmRegIsActiveMember(bdf)) {
            port = bdf;
            found = TRUE;
            break;
        }
        if (n >= CM_IRQ_PATH_MAX) {
            break;
        }
        path[n++] = bdf;
        f = a;
    }
    KeReleaseSpinLock(&cmRegLock, irql);
    if (!found) {
        *why = CM_IRQ_WHY_NO_PORT;
        return 0;
    }
    /* The function's bus: the secondary bus of the bridge directly above
     * it (path[1], or the tunnel port for a function on the tunnel bus).
     * On Windows 2000 DevicePropertyBusNumber is not yet answered when the
     * requirements are queried (read on q35: STATUS_OBJECT_NAME_NOT_FOUND),
     * DevicePropertyAddress is. */
    if (!cmIrqBridgeSec((n > 1) ? path[1] : port, &sec)) {
        *why = CM_IRQ_WHY_UNREAD;
        return 0;
    }
    path[0] = XHCI_TBT_BDF(sec, (address >> 16) & 0x1FUL,
                           address & 0x7UL) & 0xFFFFUL;
    *fnBdf = path[0];

    /* The function: present, a type 0 header, its pin. */
    if (!cmCfgRead(NULL, path[0], 0x00UL, &id) ||
        (id & 0xFFFFUL) == 0xFFFFUL || (id & 0xFFFFUL) == 0x0001UL ||
        !cmCfgRead(NULL, path[0], 0x0CUL, &v) ||
        ((v >> 16) & 0x7FUL) != 0 ||
        !cmCfgRead(NULL, path[0], 0x3CUL, &v)) {
        *why = CM_IRQ_WHY_UNREAD;
        return 0;
    }
    pin = (v >> 8) & 0xFFUL;
    /* Each bridge, and the tunnel port, must answer as a present bridge
     * with the link below it on its secondary bus (for the bridge above
     * the function a second read, which catches a change between the
     * two). */
    for (k = 1; k <= n; k++) {
        bdf = (k < n) ? path[k] : port;
        if (!cmIrqBridgeSec(bdf, &sec)) {
            *why = CM_IRQ_WHY_UNREAD;
            return 0;
        }
        if (sec != XHCI_TBT_BDF_BUS(path[k - 1])) {
            *why = CM_IRQ_WHY_MOVED;
            return 0;
        }
    }
    if (!cmCfgRead(NULL, port, 0x3CUL, &v)) {
        *why = CM_IRQ_WHY_UNREAD;
        return 0;
    }
    portLine = v & 0xFFUL;
    portPin = (v >> 8) & 0xFFUL;
    for (k = 0; k < n; k++) {
        devs[k] = XHCI_TBT_BDF_DEV(path[k]);
    }
    return CmIrqWire(pin, devs, n, portPin, portLine, why);
}

/*
 * The chained unit's parent (design record 34 section 3.10): whether the
 * bridge `child` sits on is a chain port of an active member now, made
 * again at each of its BusRelations answers, before the answer goes up,
 * and when a child of it asks its capabilities while it is not one
 * (CmRegChainRecheck). Its location is the one read at its start (a chain
 * child reads its BusRelations only once started); the member is the
 * first active member on its ancestry (CmRegIrqWire's walk). The bridge,
 * every bridge between it and the member, and the member's bus numbers
 * are read now (XhciTbtChainPortTest): a bridge between them that is a
 * chain port (one level of chain ports, section 3.9), or that cannot be
 * told not to be one, withholds it. The outcome is committed only while
 * the member is still active, and logged when it changes. PASSIVE_LEVEL;
 * the list's lock is not held across the reads.
 */
static VOID cmChainClassify(PCM_FILTER child)
{
    XHCI_TBT_CFG io;
    PCM_FILTER f;
    PCM_FILTER a;
    PCM_NHI dom;
    ULONG path[CM_TUNNEL_DEPTH_MAX + 2UL];
    ULONG n;
    ULONG k;
    ULONG self;
    ULONG member;
    ULONG bdf;
    ULONG sec;
    ULONG s;
    ULONG out;
    ULONG hops;
    BOOLEAN found;
    BOOLEAN nested;
    KIRQL irql;

    self = 0xFFFFUL;
    member = 0;
    found = FALSE;
    nested = FALSE;
    n = 0;
    KeAcquireSpinLock(&cmRegLock, &irql);
    if (child->Located) {
        self = XHCI_TBT_BDF(child->LocBus, child->LocAddress >> 16,
                            child->LocAddress & 0xFFFFUL) & 0xFFFFUL;
        f = child;
        for (hops = 0; hops <= CM_TUNNEL_DEPTH_MAX + 1UL; hops++) {
            a = f->Parent;
            if (a == NULL && f->Common.Kind == CM_KIND_CHILD) {
                a = f->Bridge;
            }
            if (a == NULL) {
                break;
            }
            bdf = cmFiltBdf(a);
            if (bdf == 0xFFFFUL) {
                break;
            }
            if (cmRegIsActiveMember(bdf)) {
                member = bdf;
                found = TRUE;
                break;
            }
            path[n++] = bdf;
            f = a;
        }
    }
    KeReleaseSpinLock(&cmRegLock, irql);
    sec = 0;
    io.Read = cmCfgRead;
    io.Write = NULL;
    io.Context = NULL;
    io.Reads = 0;
    io.Writes = 0;
    if (self == 0xFFFFUL) {
        out = XHCI_TBT_CPT_FAILED;
    } else if (!found) {
        out = XHCI_TBT_CPT_NOT;
    } else {
        out = XHCI_TBT_CPT_NOT;
        for (k = 0; k < n && !nested; k++) {
            s = XhciTbtChainPortTest(&io, path[k], member, &bdf);
            if (s != XHCI_TBT_CPT_NOT) {
                /* A chain port above it, or a bridge between it and the
                 * member that is not a numbered bridge positively no
                 * candidate. */
                nested = TRUE;
                out = (s == XHCI_TBT_CPT_YES) ? XHCI_TBT_CPT_NOT
                                              : XHCI_TBT_CPT_FAILED;
            }
        }
        if (!nested) {
            out = XhciTbtChainPortTest(&io, self, member, &sec);
        }
    }
    KeAcquireSpinLock(&cmRegLock, &irql);
    if (out == XHCI_TBT_CPT_YES && !cmRegIsActiveMember(member)) {
        out = XHCI_TBT_CPT_NOT;
    }
    child->ChainLanding = (out == XHCI_TBT_CPT_YES)
                              ? (self | CM_MK_LANDING_VALID) : 0;
    child->ChainSec = (out == XHCI_TBT_CPT_YES) ? sec : 0;
    child->ChainMember = (out == XHCI_TBT_CPT_YES) ? member : 0;
    if (child->ChainWhy != out + 1UL) {
        child->ChainWhy = out + 1UL;
        dom = cmLogDom(cmTopOf(child));
        if (dom != NULL && found) {
            if (out == XHCI_TBT_CPT_YES) {
                CmLogNote(dom, "cm.chain.class", (sec << 16) | self);
            } else {
                CmLogNote(dom, "cm.chain.class.no",
                          (out << 24) | ((nested ? 1UL : 0UL) << 16) |
                              (self & 0xFFFFUL));
            }
        }
    }
    KeReleaseSpinLock(&cmRegLock, irql);
}

VOID CmRegChainRecheck(PCM_FILTER child)
{
    PCM_FILTER p;
    KIRQL irql;
    BOOLEAN need;

    KeAcquireSpinLock(&cmRegLock, &irql);
    p = child->Parent;
    need = (BOOLEAN)(p != NULL && p->Common.Kind == CM_KIND_CHILD &&
                     p->AttListed && p->Located && !p->SurpriseRemoved &&
                     (p->ChainLanding & CM_MK_LANDING_VALID) == 0);
    /* The parent's removal reference, taken under the list's lock (the
     * list's lock, then the filter's), so a capabilities request that is
     * not the PnP manager's own cannot outlive it. */
    if (need && !CmFilterTryHold(p)) {
        need = FALSE;
    }
    KeReleaseSpinLock(&cmRegLock, irql);
    if (!need) {
        return;
    }
    cmChainClassify(p);
    KeAcquireSpinLock(&cmRegLock, &irql);
    if (!p->AttListed || p->SurpriseRemoved) {
        /* Retired meanwhile: no landing of a filter on its way out. */
        p->ChainLanding = 0;
    }
    if ((p->ChainLanding & CM_MK_LANDING_VALID) != 0) {
        cmMarksSyncAll();
    }
    KeReleaseSpinLock(&cmRegLock, irql);
    CmFilterUnhold(p);
}

VOID CmRegIrqNote(PCM_FILTER f, const char *label, ULONG value)
{
    KIRQL irql;
    PCM_NHI dom;

    KeAcquireSpinLock(&cmRegLock, &irql);
    dom = cmLogDom(cmTopOf(f));
    if (dom != NULL) {
        CmLogNote(dom, label, value);
    }
    KeReleaseSpinLock(&cmRegLock, irql);
}

/* ====================================================================== */
/* A bridge's requirements (design record 33, task 45.12)                */
/* ====================================================================== */

/* The counts of record 33 section 9.3, emitted at the stop (cm.breq.*). */
#define CM_BQC_SEEN         0
#define CM_BQC_REWRITTEN    1
#define CM_BQC_DROP_IO      2
#define CM_BQC_DROP_MEM     3
#define CM_BQC_DROP_PREF    4
#define CM_BQC_UNMET        5
#define CM_BQC_SHAPE        6
#define CM_BQC_PARENT       7
#define CM_BQC_NOTALONE     8
#define CM_BQC_NOBRIDGE     9
#define CM_BQC_NOMEM        10
#define CM_BQC_NOWIN        11
#define CM_BQC_LOWER        12
#define CM_BQC_START_OK     13
#define CM_BQC_START_FAIL   14
#define CM_BQC_COUNT        15
static ULONG cmBqCount[CM_BQC_COUNT];
static const char *const cmBqNames[CM_BQC_COUNT] = {
    "cm.breq.seen", "cm.breq.rewritten", "cm.breq.dropped.io",
    "cm.breq.dropped.mem", "cm.breq.dropped.pref", "cm.breq.unmet",
    "cm.breq.shape", "cm.breq.parent", "cm.breq.notalone",
    "cm.breq.nobridge", "cm.breq.nomem", "cm.breq.nowin", "cm.breq.lower",
    "cm.breq.start.ok", "cm.breq.start.fail"
};

/* The domain whose set holds `bdf` (16-bit) as an active member, or NULL.
 * Under the list's lock. */
static PCM_NHI cmRegMemberDom(ULONG bdf)
{
    PLIST_ENTRY e;
    PCM_REENUM r;
    ULONG i;

    for (e = cmRegDomains.Flink; e != &cmRegDomains; e = e->Flink) {
        r = CONTAINING_RECORD(e, CM_REENUM, Link);
        if (r->Route != CM_ROUTE_FILTER) {
            continue;
        }
        for (i = 0; i < r->Members; i++) {
            if ((r->Port[i].Bdf & 0xFFFFUL) == bdf &&
                (r->Port[i].Flags & CM_PORT_ACTIVE) != 0) {
                return CONTAINING_RECORD(r, CM_NHI, Reenum);
            }
        }
    }
    return NULL;
}

BOOLEAN CmRegBreqFind(PCM_FILTER child, PULONG parent, PULONG port,
                      PULONG mode)
{
    PCM_FILTER f;
    PCM_FILTER a;
    PCM_NHI dom;
    ULONG bdf;
    ULONG n;
    BOOLEAN found;
    KIRQL irql;

    *parent = 0xFFFFUL;
    *port = 0xFFFFUL;
    *mode = CM_BRIDGE_REQ_OFF;
    found = FALSE;
    KeAcquireSpinLock(&cmRegLock, &irql);
    f = child;
    for (n = 0; n <= CM_TUNNEL_DEPTH_MAX; n++) {
        a = f->Parent;
        if (a == NULL && f->Common.Kind == CM_KIND_CHILD) {
            a = f->Bridge;
        }
        if (a == NULL) {
            break;
        }
        bdf = cmFiltBdf(a);
        if (n == 0) {
            *parent = bdf;
        }
        if (bdf == 0xFFFFUL) {
            break;
        }
        dom = cmRegMemberDom(bdf);
        if (dom != NULL) {
            *port = bdf;
            *mode = dom->Values.BridgeReq;
            found = TRUE;
            break;
        }
        f = a;
    }
    KeReleaseSpinLock(&cmRegLock, irql);
    return found;
}

/*
 * Design record 22 section 10.5 (task 45.35). The member above `child`
 * (its ancestry, as CmRegBreqFind walks it), that member's domain when it
 * runs the USB4 software connection manager, the USB4 port whose member by
 * correspondence it is, and that port's generation while its PCIe tunnel
 * is up: the binding, kept in the filter. Taken once a start; a later call
 * keeps the first. The interface's first reference is taken here, under
 * cmRegLock with the binding, and every reference after it and its release
 * under the same lock (CmRegIsoRef, CmRegIsoDeref), so the last release's
 * unbinding and a new binding never interleave.
 */
BOOLEAN CmRegIsoBind(PCM_FILTER child)
{
    PCM_FILTER f;
    PCM_FILTER a;
    PCM_NHI dom;
    ULONG bdf;
    ULONG n;
    ULONG port;
    ULONG gen;
    BOOLEAN bound;
    KIRQL irql;

    bound = FALSE;
    KeAcquireSpinLock(&cmRegLock, &irql);
    if (child->IsoBound && child->IsoRefs != 0) {
        /* Immutable while a reference is held; the caller's own. */
        child->IsoRefs++;
        KeReleaseSpinLock(&cmRegLock, irql);
        return TRUE;
    }
    child->IsoBound = 0;
    dom = NULL;
    bdf = 0xFFFFUL;
    f = child;
    for (n = 0; n <= CM_TUNNEL_DEPTH_MAX; n++) {
        a = f->Parent;
        if (a == NULL && f->Common.Kind == CM_KIND_CHILD) {
            a = f->Bridge;
        }
        if (a == NULL) {
            break;
        }
        bdf = cmFiltBdf(a);
        if (bdf == 0xFFFFUL) {
            break;
        }
        dom = cmRegMemberDom(bdf);
        if (dom != NULL) {
            break;
        }
        f = a;
    }
    if (dom != NULL && dom->U4On && !child->IsoGone) {
        /* Every USB4 port's tunnel up at this start, by generation: the
         * port that feeds the member is named at the ask, by the
         * correspondence then (it may be learned after the start). */
        child->IsoUpMask = 0;
        for (port = 1; port <= TBT_U4_MAX_PORTS; port++) {
            child->IsoGens[port - 1] = 0;
            if (TbtU4PortPcieGen(&dom->U4, port, &gen)) {
                child->IsoGens[port - 1] = gen;
                child->IsoUpMask |= 1UL << (port - 1);
            }
        }
        if (child->IsoUpMask != 0) {
            child->IsoDom = (PVOID)dom;
            child->IsoStarts = dom->Reenum.Starts;
            child->IsoMember = bdf;
            child->IsoBound = 1;
            child->IsoRefs++;
            bound = TRUE;
            CmLogNote(dom, "cm.iso.bind", (child->IsoUpMask << 24) | bdf);
        }
    }
    KeReleaseSpinLock(&cmRegLock, irql);
    return bound;
}

ULONG CmRegIsoAsk(PCM_FILTER child)
{
    PLIST_ENTRY e;
    PCM_NHI d;
    PCM_NHI dom;
    CM_ISO_IN in;
    ULONG why;
    ULONG m;
    ULONG p;
    ULONG port;
    ULONG gen;
    KIRQL irql;

    RtlZeroMemory(&in, sizeof(in));
    port = 0;
    gen = 0;
    KeAcquireSpinLock(&cmRegLock, &irql);
    in.Bound = child->IsoBound;
    in.Gone = child->IsoGone;
    dom = NULL;
    if (in.Bound) {
        for (e = cmRegDomains.Flink; e != &cmRegDomains; e = e->Flink) {
            d = CONTAINING_RECORD(CONTAINING_RECORD(e, CM_REENUM, Link),
                                  CM_NHI, Reenum);
            if ((PVOID)d == child->IsoDom) {
                dom = d;
                break;
            }
        }
    }
    if (dom != NULL) {
        in.DomainListed = 1;
        in.StartsSame = (dom->Reenum.Starts == child->IsoStarts) ? 1UL : 0UL;
        in.U4On = dom->U4On ? 1UL : 0UL;
        in.StageActive = (dom->Stage == CM_STAGE_ACTIVE) ? 1UL : 0UL;
        in.LiveActive = (dom->Live.State == CM_LIVE_ACTIVE) ? 1UL : 0UL;
        in.LossDrain = dom->Reenum.LossDrain ? 1UL : 0UL;
        /* The port that feeds the bound member now, by correspondence;
         * exactly one, else none is named. */
        for (p = 1; p <= TBT_U4_MAX_PORTS; p++) {
            m = XhciTbtPortMember(&dom->Reenum.Topo, &dom->Binds, p);
            if (m != XHCI_TBT_NO_MEMBER && m < dom->Reenum.Topo.Tunnels &&
                (dom->Reenum.Topo.Set[m] & 0xFFFFUL) == child->IsoMember) {
                port = (port == 0) ? p : 0xFFFFFFFFUL;
            }
        }
        if (port != 0 && port != 0xFFFFFFFFUL) {
            in.Corresponds = 1;
            gen = child->IsoGens[port - 1];
            in.Isolated =
                ((child->IsoUpMask & (1UL << (port - 1))) != 0 &&
                 TbtU4PortIsolated(&dom->U4, port, gen))
                    ? 1UL
                    : 0UL;
        } else {
            port = 0;
        }
    }
    why = CmIsoDecide(&in);
    if (dom != NULL) {
        dom->Counters.IsoAsked++;
        if (why == CM_ISO_YES) {
            dom->Counters.IsoYes++;
        } else {
            dom->Counters.IsoNo++;
        }
        CmLogNote(dom, "cm.iso", (why << 24) | ((port & 0xFFUL) << 16) |
                                     (gen & 0xFFFFUL));
    }
    KeReleaseSpinLock(&cmRegLock, irql);
    return why;
}

VOID CmRegIsoRef(PCM_FILTER child)
{
    KIRQL irql;

    KeAcquireSpinLock(&cmRegLock, &irql);
    child->IsoRefs++;
    KeReleaseSpinLock(&cmRegLock, irql);
}

VOID CmRegIsoDeref(PCM_FILTER child)
{
    KIRQL irql;

    KeAcquireSpinLock(&cmRegLock, &irql);
    if (child->IsoRefs > 0) {
        child->IsoRefs--;
        if (child->IsoRefs == 0) {
            child->IsoBound = 0;
        }
    }
    KeReleaseSpinLock(&cmRegLock, irql);
}

VOID CmRegIsoGone(PCM_FILTER child)
{
    KIRQL irql;

    KeAcquireSpinLock(&cmRegLock, &irql);
    child->IsoGone = 1;
    KeReleaseSpinLock(&cmRegLock, irql);
}

/* One function's id and header type: TRUE when it answers (not all-ones,
 * not Configuration Request Retry Status); *ok 0 when a read failed or it
 * answered Retry Status, which makes what it is unknown. PASSIVE_LEVEL. */
static BOOLEAN cmBqPresent(ULONG bdf, PULONG hdr, PULONG ok)
{
    ULONG id;
    ULONG v;

    *ok = 1;
    *hdr = 0;
    if (!cmCfgRead(NULL, bdf, 0x00UL, &id)) {
        *ok = 0;
        return FALSE;
    }
    if ((id & 0xFFFFUL) == 0xFFFFUL) {
        return FALSE;
    }
    if ((id & 0xFFFFUL) == 0x0001UL || !cmCfgRead(NULL, bdf, 0x0CUL, &v)) {
        *ok = 0;
        return FALSE;
    }
    *hdr = (v >> 16) & 0xFFUL;
    return TRUE;
}

/* A function's PCI Express Device/Port Type and Slot Implemented from its
 * capability list, *capAt its offset: CM_BQ_PCIE when found, CM_BQ_NONE
 * when the function reads as having none (no list, or a list that ends
 * without it), CM_BQ_UNREAD when a read failed or the list does not end
 * within its bound (what it is, unknown). PASSIVE_LEVEL. */
#define CM_BQ_NONE      0UL
#define CM_BQ_PCIE      1UL
#define CM_BQ_UNREAD    2UL
static ULONG cmBqPcie(ULONG bdf, PULONG type, PULONG slot, PULONG capAt)
{
    ULONG v;
    ULONG at;
    ULONG steps;

    if (!cmCfgRead(NULL, bdf, 0x04UL, &v)) {
        return CM_BQ_UNREAD;
    }
    if ((v & (1UL << 20)) == 0) {
        return CM_BQ_NONE;
    }
    if (!cmCfgRead(NULL, bdf, 0x34UL, &v)) {
        return CM_BQ_UNREAD;
    }
    at = v & 0xFCUL;
    for (steps = 0; steps < 48; steps++) {
        if (at == 0) {
            return CM_BQ_NONE;
        }
        if (at < 0x40UL || !cmCfgRead(NULL, bdf, at, &v)) {
            return CM_BQ_UNREAD;
        }
        if ((v & 0xFFUL) == 0x10UL) {
            *type = (v >> 20) & 0xFUL;
            *slot = (v >> 24) & 1UL;
            *capAt = at;
            return CM_BQ_PCIE;
        }
        at = (v >> 8) & 0xFCUL;
    }
    return CM_BQ_UNREAD;
}

/* A bridge's secondary and subordinate buses: TRUE when numbered
 * (secondary not 0 and not FFh, subordinate not below it). */
static BOOLEAN cmBqNumbered(ULONG bdf, PULONG sec)
{
    ULONG v;
    ULONG s;
    ULONG sub;

    if (!cmCfgRead(NULL, bdf, 0x18UL, &v)) {
        return FALSE;
    }
    s = (v >> 8) & 0xFFUL;
    sub = (v >> 16) & 0xFFUL;
    *sec = s;
    return (BOOLEAN)(s != 0 && s != 0xFFUL && sub >= s);
}

/* What a downstream port's secondary bus holds (section 5.2): device 0
 * alone answers below a PCI Express downstream port. */
static ULONG cmBqBehind(ULONG sec, PULONG anyIo, PULONG anyPref)
{
    ULONG fn;
    ULONG fns;
    ULONG hdr;
    ULONG hdr2;
    ULONG ok;
    ULONG bdf;
    ULONG bars[6];
    ULONG io;
    ULONG pref;
    ULONG k;
    ULONG seen;
    ULONG behind;

    *anyIo = 0;
    *anyPref = 0;
    seen = 0;
    behind = CM_BREQ_B_TYPE0;
    fns = 1;
    for (fn = 0; fn < fns; fn++) {
        bdf = XHCI_TBT_BDF(sec, 0, fn) & 0xFFFFUL;
        if (!cmBqPresent(bdf, &hdr, &ok)) {
            if (!ok) {
                return CM_BREQ_B_UNREAD;
            }
            continue;
        }
        if (fn == 0 && (hdr & 0x80UL) != 0) {
            fns = 8;
        }
        seen++;
        if ((hdr & 0x7FUL) == 1UL) {
            behind = CM_BREQ_B_TYPE1;
            continue;
        }
        if ((hdr & 0x7FUL) != 0) {
            return CM_BREQ_B_UNREAD;
        }
        for (k = 0; k < 6; k++) {
            if (!cmCfgRead(NULL, bdf, 0x10UL + 4UL * k, &bars[k])) {
                return CM_BREQ_B_UNREAD;
            }
        }
        if (!cmBqPresent(bdf, &hdr2, &ok) || hdr2 != hdr ||
            !CmBreqBars(bars, &io, &pref)) {
            return CM_BREQ_B_UNREAD;
        }
        *anyIo |= io;
        *anyPref |= pref;
    }
    return seen == 0 ? CM_BREQ_B_NONE : behind;
}

VOID CmRegBreqSnap(ULONG parent, ULONG address, PCM_BREQ_SNAP s, PULONG bdf,
                   PULONG bus)
{
    ULONG sec;
    ULONG own;
    ULONG me;
    ULONG hdr;
    ULONG ok;
    ULONG type;
    ULONG slot;
    ULONG capAt;
    ULONG dev;
    ULONG fn;
    ULONG fns;
    ULONG f;
    ULONG count;
    ULONG unread;
    ULONG v;
    ULONG k;
    CM_BREQ_SIB *b;

    CmZero(s, sizeof(*s));
    s->Role = CM_BREQ_ROLE_OTHER;
    s->Alone = CM_BREQ_UNKNOWN;
    s->N = CM_BREQ_N_UNKNOWN;
    *bdf = 0xFFFFUL;
    *bus = 0;
    if (parent == 0xFFFFUL || ((address >> 16) & 0xFFFFUL) > 31UL ||
        (address & 0xFFFFUL) > 7UL || !cmIrqBridgeSec(parent, &sec)) {
        return;
    }
    me = XHCI_TBT_BDF(sec, (address >> 16) & 0x1FUL, address & 7UL) &
         0xFFFFUL;
    *bdf = me;
    /* The parent's windows, as the PCI driver programmed them at its
     * start. */
    s->ParentRead = 1;
    for (k = 0; k < 6; k++) {
        static const UCHAR at[6] = { 0x1C, 0x20, 0x24, 0x28, 0x2C, 0x30 };

        if (!cmCfgRead(NULL, parent, at[k], &s->Par[k])) {
            s->ParentRead = 0;
        }
    }
    if (!cmIrqBridgeSec(parent, &v) || v != sec) {
        s->ParentRead = 0;
    }
    /* This bridge. */
    if (!cmBqPresent(me, &hdr, &ok) || (hdr & 0x7FUL) != 1UL) {
        return;
    }
    if (cmCfgRead(NULL, me, 0x18UL, &v)) {
        *bus = v;
    }
    if (cmBqPcie(me, &type, &slot, &capAt) == CM_BQ_PCIE) {
        if (type == 5UL) {
            s->Role = CM_BREQ_ROLE_UP;
        } else if (type == 6UL) {
            s->Role = CM_BREQ_ROLE_DOWN;
        }
    }
    /* Alone on the parent's secondary bus. */
    count = 0;
    unread = 0;
    for (dev = 0; dev < 32 && !unread; dev++) {
        fns = 1;
        for (fn = 0; fn < fns; fn++) {
            f = XHCI_TBT_BDF(sec, dev, fn) & 0xFFFFUL;
            if (!cmBqPresent(f, &hdr, &ok)) {
                if (!ok) {
                    unread = 1;
                }
                if (fn == 0) {
                    break;
                }
                continue;
            }
            if (fn == 0 && (hdr & 0x80UL) != 0) {
                fns = 8;
            }
            count++;
        }
    }
    if (!unread) {
        s->Alone = count == 1 ? CM_BREQ_YES : CM_BREQ_NO;
    }
    if (s->Role == CM_BREQ_ROLE_UP) {
        if (!cmBqNumbered(me, &own)) {
            return;
        }
        s->SecNumbered = 1;
        count = 0;
        for (dev = 0; dev < 32; dev++) {
            fns = 1;
            for (fn = 0; fn < fns; fn++) {
                f = XHCI_TBT_BDF(own, dev, fn) & 0xFFFFUL;
                if (!cmBqPresent(f, &hdr, &ok)) {
                    if (!ok) {
                        return;
                    }
                    if (fn == 0) {
                        break;
                    }
                    continue;
                }
                if (fn == 0 && (hdr & 0x80UL) != 0) {
                    fns = 8;
                }
                if ((hdr & 0x7FUL) != 1UL) {
                    continue;
                }
                k = cmBqPcie(f, &type, &slot, &capAt);
                if (k == CM_BQ_UNREAD) {
                    /* What it is cannot be told: N stays unknown. */
                    return;
                }
                if (k == CM_BQ_PCIE && type == 6UL) {
                    count++;
                }
            }
        }
        s->N = count > 0xFEUL ? 0xFEUL : count;
        return;
    }
    if (s->Role != CM_BREQ_ROLE_DOWN) {
        return;
    }
    /* The siblings: every function on this bridge's own bus, the upstream
     * port's secondary, in device and function order. */
    s->Index = CM_BREQ_SIBS_MAX;
    for (dev = 0; dev < 32; dev++) {
        fns = 1;
        for (fn = 0; fn < fns; fn++) {
            f = XHCI_TBT_BDF(sec, dev, fn) & 0xFFFFUL;
            if (!cmBqPresent(f, &hdr, &ok)) {
                if (!ok) {
                    return;
                }
                if (fn == 0) {
                    break;
                }
                continue;
            }
            if (fn == 0 && (hdr & 0x80UL) != 0) {
                fns = 8;
            }
            if (s->Sibs >= CM_BREQ_SIBS_MAX) {
                return;
            }
            b = &s->Sib[s->Sibs];
            if (f == me) {
                s->Index = s->Sibs;
            }
            s->Sibs++;
            type = 0;
            slot = 0;
            if ((hdr & 0x7FUL) != 1UL) {
                continue;
            }
            k = cmBqPcie(f, &type, &slot, &capAt);
            if (k == CM_BQ_UNREAD) {
                /* A sibling that cannot be told from a downstream port:
                 * no division over this bus. */
                return;
            }
            if (k != CM_BQ_PCIE || type != 6UL) {
                continue;
            }
            b->Dsp = 1;
            if (!cmBqNumbered(f, &own)) {
                b->Behind = CM_BREQ_B_UNREAD;
                continue;
            }
            b->Numbered = 1;
            if (slot) {
                if (!cmCfgRead(NULL, f, capAt + 0x14UL, &v)) {
                    /* Hot-plug capable or not, unknown: its claims too. */
                    b->Behind = CM_BREQ_B_UNREAD;
                    continue;
                }
                b->HotPlug = (ULONG)((v & (1UL << 6)) != 0);
            }
            b->Behind = cmBqBehind(own, &b->AnyIo, &b->AnyPref);
        }
    }
    s->SibsRead = (ULONG)(s->Index < s->Sibs);
}

/* Under the list's lock: a count raised, saturating. */
static VOID cmBqBump(ULONG which)
{
    if (cmBqCount[which] != 0xFFFFFFFFUL) {
        cmBqCount[which]++;
    }
}

VOID CmRegBreqPost(ULONG port, const ULONG *e, const CM_BREQ_PLAN *plan,
                   ULONG bus)
{
    KIRQL irql;
    KIRQL inner;
    PCM_NHI dom;
    ULONG outcome;
    ULONG k;
    ULONG j;

    outcome = (e[1] >> 12) & 0xFUL;
    KeAcquireSpinLock(&cmRegLock, &irql);
    cmBqBump(CM_BQC_SEEN);
    switch (outcome) {
    case CM_BREQ_OUT_REWRITTEN:
        cmBqBump(CM_BQC_REWRITTEN);
        break;
    case CM_BREQ_OUT_SHAPE:
        cmBqBump(CM_BQC_SHAPE);
        break;
    case CM_BREQ_OUT_PARENT:
        cmBqBump(CM_BQC_PARENT);
        break;
    case CM_BREQ_OUT_NOTALONE:
        cmBqBump(CM_BQC_NOTALONE);
        break;
    case CM_BREQ_OUT_NOMEM:
        cmBqBump(CM_BQC_NOMEM);
        break;
    case CM_BREQ_OUT_NOWIN:
        cmBqBump(CM_BQC_NOWIN);
        break;
    case CM_BREQ_OUT_LOWER:
        cmBqBump(CM_BQC_LOWER);
        break;
    default:
        break;
    }
    if (outcome == CM_BREQ_OUT_REWRITTEN || outcome == CM_BREQ_OUT_RECORD) {
        for (k = 0; k < CM_BREQ_KINDS; k++) {
            if ((plan->Dropped & (1UL << k)) != 0) {
                cmBqBump(CM_BQC_DROP_IO + k);
            }
        }
    }
    if (plan->Unmet != 0) {
        cmBqBump(CM_BQC_UNMET);
    }
    dom = cmRegMemberDom(port & 0xFFFFUL);
    if (dom != NULL) {
        KeAcquireSpinLock(&dom->BrDiagLock, &inner);
        dom->BqDiagSeq++;
        if (dom->BqDiagSeq == 0) {
            dom->BqDiagSeq = 1;
        }
        CmBreqDiagAdd(dom->BqDiag, dom->BqDiagSeq, e);
        dom->BqDiagGen++;
        if (dom->BqDiagGen == 0) {
            dom->BqDiagGen = 1;
        }
        KeReleaseSpinLock(&dom->BrDiagLock, inner);
        /* Order: cmRegLock, then the log's own lock. */
        CmLogNote(dom, "cm.breq", e[0]);
        CmLogNote(dom, "cm.breq.how", e[1]);
        CmLogNote(dom, "cm.breq.sibs", e[2]);
        CmLogNote(dom, "cm.breq.bus", bus);
        if (outcome == CM_BREQ_OUT_REWRITTEN ||
            outcome == CM_BREQ_OUT_RECORD) {
            for (k = 0; k < CM_BREQ_KINDS; k++) {
                for (j = 0; plan->Keep[k] && plan->Sized[k] &&
                            j < plan->Steps[k];
                     j++) {
                    CmLogNote(dom, "cm.breq.list",
                              (k << 28) | (plan->Len[k][j] >> 12));
                }
            }
        }
    }
    KeReleaseSpinLock(&cmRegLock, irql);
}

VOID CmRegBreqStarted(ULONG port, ULONG bdf, BOOLEAN ok, NTSTATUS status)
{
    KIRQL irql;
    KIRQL inner;
    PCM_NHI dom;

    KeAcquireSpinLock(&cmRegLock, &irql);
    cmBqBump(ok ? CM_BQC_START_OK : CM_BQC_START_FAIL);
    dom = cmRegMemberDom(port & 0xFFFFUL);
    if (dom != NULL) {
        KeAcquireSpinLock(&dom->BrDiagLock, &inner);
        if (CmBreqDiagStart(dom->BqDiag, bdf, ok)) {
            dom->BqDiagGen++;
            if (dom->BqDiagGen == 0) {
                dom->BqDiagGen = 1;
            }
        }
        KeReleaseSpinLock(&dom->BrDiagLock, inner);
        CmLogNote(dom, "cm.breq.start", (ULONG)status);
        CmLogNote(dom, "cm.breq.start.bdf", bdf);
    }
    KeReleaseSpinLock(&cmRegLock, irql);
}

VOID CmRegBreqNoBridge(PCM_FILTER child)
{
    KIRQL irql;
    KIRQL inner;
    PCM_NHI dom;

    KeAcquireSpinLock(&cmRegLock, &irql);
    cmBqBump(CM_BQC_NOBRIDGE);
    dom = cmLogDom(cmTopOf(child));
    if (dom != NULL) {
        KeAcquireSpinLock(&dom->BrDiagLock, &inner);
        CmBreqDiagNoBridge(dom->BqDiag);
        dom->BqDiagGen++;
        if (dom->BqDiagGen == 0) {
            dom->BqDiagGen = 1;
        }
        KeReleaseSpinLock(&dom->BrDiagLock, inner);
        CmLogNote(dom, "cm.breq.nobridge", cmFiltBdf(child));
    }
    KeReleaseSpinLock(&cmRegLock, irql);
}

VOID CmRegIdCounters(PULONG given, PULONG nomem, PULONG over, PULONG deep,
                     PULONG reattached)
{
    KIRQL irql;

    KeAcquireSpinLock(&cmRegLock, &irql);
    *given = cmRegIdGiven;
    *nomem = cmRegIdNomem;
    *over = cmRegIdOver;
    *deep = cmRegChainDeep;
    *reattached = cmRegReattached;
    KeReleaseSpinLock(&cmRegLock, irql);
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
    /* Record 34 section 3.12 item 7: a chained unit's eject holds every
     * function its chain port's filters report. */
    for (e = cmRegAtts.Flink; e != &cmRegAtts && held == NULL;
         e = e->Flink) {
        f = CONTAINING_RECORD(e, CM_FILTER, AttLink);
        if (f->Common.Kind != CM_KIND_CHILD || !f->EjectHold) {
            continue;
        }
        for (i = 0; i < f->SnapCount; i++) {
            if (f->Snap[i] == (PVOID)filter->Common.Pdo) {
                held = f;
                break;
            }
        }
    }
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
static PCM_FILTER cmLandingFind(ULONG memberBdf, ULONG landing);

static VOID cmSnapKeepLoc(ULONG memberBdf, ULONG landing, PVOID pdo,
                          ULONG bus, ULONG address)
{
    PCM_FILTER f;
    ULONG k;

    PLIST_ENTRY e;

    if (landing != 0) {
        /* A chained entry's location is kept by its landing's snapshot
         * (record 34 section 3.10), in every child filter classified as
         * that chain port (a BusRelations child and an AddDevice one may
         * both sit on its PDO, each with the same answer). */
        for (e = cmRegAtts.Flink; e != &cmRegAtts; e = e->Flink) {
            f = CONTAINING_RECORD(e, CM_FILTER, AttLink);
            if (f->Common.Kind != CM_KIND_CHILD || f->ChainLanding != landing ||
                f->ChainMember != (memberBdf & 0xFFFFUL)) {
                continue;
            }
            for (k = 0; k < f->SnapCount; k++) {
                if (f->Snap[k] == pdo) {
                    f->SnapLocd[k] = 1;
                    f->SnapBus[k] = bus;
                    f->SnapAddr[k] = address;
                }
            }
        }
        return;
    }
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
        if (f->KeptLogical) {
            /* A kept child logically removed or detached (design record 31
             * section 4.10, K8): no presence; its location is the
             * device's still. */
            if (f->Located) {
                flags |= CM_QM_EF_LOCATED;
                bus = f->LocBus;
                address = f->LocAddress;
            }
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
        cmSnapKeepLoc(m->MemberBdf, m->Landing, m->Pdo, bus, address);
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

/* A chain child that may carry chained entries (record 34 section 3.10):
 * listed, located, not surprise-removed, classified a chain port. Under
 * the lock. */
static BOOLEAN cmIsLanding(PCM_FILTER g)
{
    return (BOOLEAN)(g->Common.Kind == CM_KIND_CHILD && g->AttListed &&
                     g->Located && !g->SurpriseRemoved &&
                     (g->ChainLanding & CM_MK_LANDING_VALID) != 0);
}

/* The chain child classified as the chain port `landing` under the member
 * at `memberBdf`, or NULL. Under the lock. */
static PCM_FILTER cmLandingFind(ULONG memberBdf, ULONG landing)
{
    PLIST_ENTRY e;
    PCM_FILTER g;

    for (e = cmRegAtts.Flink; e != &cmRegAtts; e = e->Flink) {
        g = CONTAINING_RECORD(e, CM_FILTER, AttLink);
        if (cmIsLanding(g) && g->ChainLanding == landing &&
            g->ChainMember == (memberBdf & 0xFFFFUL)) {
            return g;
        }
    }
    return NULL;
}

/* The chain ports classified under the member at `memberBdf`, at most
 * XHCI_TBT_CHAIN_MAX (the first found; any further one carries no
 * entries), each landing once. Under the lock. Returns the count. */
static ULONG cmLandings(ULONG memberBdf, PULONG lands, PCM_FILTER *lf)
{
    PLIST_ENTRY e;
    PCM_FILTER g;
    ULONG n;
    ULONG k;
    BOOLEAN dup;

    n = 0;
    for (e = cmRegAtts.Flink; e != &cmRegAtts && n < XHCI_TBT_CHAIN_MAX;
         e = e->Flink) {
        g = CONTAINING_RECORD(e, CM_FILTER, AttLink);
        if (!cmIsLanding(g) || g->ChainMember != (memberBdf & 0xFFFFUL)) {
            continue;
        }
        dup = FALSE;
        for (k = 0; k < n; k++) {
            if (lands[k] == g->ChainLanding) {
                dup = TRUE;
            }
        }
        if (!dup) {
            lands[n] = g->ChainLanding;
            lf[n] = g;
            n++;
        }
    }
    return n;
}

/* ---- the chained unit's provenance and token (record 34 section 3.12,
 * task 48.30), under the lock ---- */

/* A chain child's own location, 16 bits, or 0xFFFF when unread. */
static ULONG cmChildLoc(PCM_FILTER g)
{
    if (!g->Located) {
        return 0xFFFFUL;
    }
    return XHCI_TBT_BDF(g->LocBus, g->LocAddress >> 16,
                        g->LocAddress & 0xFFFFUL) & 0xFFFFUL;
}

/*
 * Item 5 and item 8: each binding's chain port identity fixed from, or
 * checked against, the filters classified at it - the first one older
 * than the transaction's arm fixes it; another identity, a classification
 * at the port that is not the candidate under the binding's member and
 * bus, or a first identity not older than the arm, sets Wrong for the
 * thread to withdraw - and every chain child's token made what its
 * binding says (0 for the rest), over every domain's bindings at once.
 */
static VOID cmChainTokSync(VOID)
{
    PLIST_ENTRY e;
    PLIST_ENTRY d;
    PCM_REENUM r;
    PCM_FILTER g;
    PCM_CHAIN_LAND l;
    PCM_CHAIN_LAND from;
    ULONG i;
    ULONG w;
    BOOLEAN yes;

    for (e = cmRegAtts.Flink; e != &cmRegAtts; e = e->Flink) {
        g = CONTAINING_RECORD(e, CM_FILTER, AttLink);
        if (g->Common.Kind != CM_KIND_CHILD) {
            continue;
        }
        from = NULL;
        for (d = cmRegDomains.Flink; d != &cmRegDomains; d = d->Flink) {
            r = CONTAINING_RECORD(d, CM_REENUM, Link);
            if (r->Route != CM_ROUTE_FILTER) {
                continue;
            }
            for (i = 0; i < CM_CHAIN_LANDS; i++) {
                l = &r->ChainLand[i];
                if (!l->Used || !g->AttListed || g->SurpriseRemoved ||
                    cmChildLoc(g) != (l->Landing & 0xFFFFUL)) {
                    continue;
                }
                yes = (BOOLEAN)(cmIsLanding(g) &&
                                g->ChainLanding == l->Landing &&
                                g->ChainMember == (l->MemberBdf & 0xFFFFUL) &&
                                g->ChainSec == l->Sec);
                if (!yes) {
                    if (g->ChainWhy != 0 &&
                        (l->PdoSeq == 0 || g->PdoSeq == l->PdoSeq)) {
                        /* Classified at the port, and not as this binding's
                         * chain port. */
                        l->Wrong = 1;
                    }
                    continue;
                }
                if (l->PdoSeq == 0 && (l->Tok[0] & CM_MK_TOK_VALID) != 0) {
                    if (CmMarksSeqAfter(l->Tok[3], g->PdoSeq)) {
                        l->PdoSeq = g->PdoSeq;
                    } else {
                        l->Wrong = 1;
                    }
                }
                if (l->PdoSeq != 0 && g->PdoSeq != l->PdoSeq) {
                    l->Wrong = 1;
                }
                if (!l->Wrong && l->PdoSeq != 0 && g->PdoSeq == l->PdoSeq &&
                    (l->Tok[0] & CM_MK_TOK_VALID) != 0) {
                    from = l;
                }
            }
        }
        for (w = 0; w < CM_MK_TOK_WORDS; w++) {
            g->ChainTok[w] = from != NULL ? from->Tok[w] : 0;
        }
    }
}

/* Item 5's grant for member `member`'s landing `landing`, from every
 * filter classified there carrying a token. */
static VOID cmChainGrant(PCM_NHI d, ULONG member, ULONG memberBdf,
                         ULONG landing)
{
    PLIST_ENTRY e;
    PCM_FILTER g;
    ULONG n;

    for (e = cmRegAtts.Flink; e != &cmRegAtts; e = e->Flink) {
        g = CONTAINING_RECORD(e, CM_FILTER, AttLink);
        if (!cmIsLanding(g) || g->ChainLanding != landing ||
            g->ChainMember != (memberBdf & 0xFFFFUL) ||
            (g->ChainTok[0] & CM_MK_TOK_VALID) == 0) {
            continue;
        }
        n = CmMarksGrant(&d->Reenum.Marks, member, landing,
                         (PVOID const *)g->Snap, g->SnapSeq, g->SnapCount,
                         g->ChainTok, cmMarkNow(d));
        if (n != 0) {
            CmLogNote(d, "cm.chain.tok",
                      (g->ChainTok[0] & 0xFFFFUL) << 16 |
                          (landing & 0xFFFFUL));
        }
    }
}

/* Item 7's landing check: a filter classified at the posted landing under
 * its member, of the posted identity and token, settled, of the posted
 * population and stackless; 0 with *found, else why (CM_KE_*). */
#define CM_KE_NONE          1UL
#define CM_KE_IDENTITY      2UL
#define CM_KE_TOKEN         3UL
#define CM_KE_UNSETTLED     4UL
#define CM_KE_POPULATION    5UL
#define CM_KE_STACK         6UL
#define CM_KE_HOLD          7UL
#define CM_KE_REFUSED       8UL

static BOOLEAN cmLandingReady(PCM_FILTER g)
{
    ULONG i;
    PDEVICE_OBJECT pdo;
    PLIST_ENTRY e;
    PCM_FILTER f;
    ULONG busChild;
    ULONG addDev;
    ULONG above;

    if (g->SnapTruncated) {
        return FALSE;
    }
    for (i = 0; i < g->SnapCount; i++) {
        pdo = (PDEVICE_OBJECT)g->Snap[i];
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

/* The listed chain children on the PDO of identity `pdoSeq`, at most
 * `room` in found[] (the count may exceed it). */
static ULONG cmKidSibs(ULONG pdoSeq, PCM_FILTER *found, ULONG room)
{
    PLIST_ENTRY e;
    PCM_FILTER g;
    ULONG n;

    n = 0;
    for (e = cmRegAtts.Flink; e != &cmRegAtts; e = e->Flink) {
        g = CONTAINING_RECORD(e, CM_FILTER, AttLink);
        if (g->Common.Kind != CM_KIND_CHILD || !g->AttListed ||
            g->PdoSeq != pdoSeq || pdoSeq == 0) {
            continue;
        }
        if (n < room) {
            found[n] = g;
        }
        n++;
    }
    return n;
}

/* Item 7.3: the posted set of filters, none gone and none new, each
 * classified at the landing with the token, settled, of its posted
 * population and stackless; 0 with them in found[] (*count), else why. */
static ULONG cmKidLandingCheck(const CM_KID_EJECT *post, PCM_FILTER *found,
                               PULONG count)
{
    PCM_FILTER g;
    ULONG n;
    ULONG i;
    ULONG k;
    ULONG w;
    BOOLEAN seen;

    *count = 0;
    n = cmKidSibs(post->PdoSeq, found, CM_KID_SIBS);
    if (n == 0) {
        return CM_KE_NONE;
    }
    if (n != post->Sibs || n > CM_KID_SIBS) {
        return CM_KE_IDENTITY;
    }
    for (i = 0; i < n; i++) {
        g = found[i];
        seen = FALSE;
        for (k = 0; k < post->Sibs; k++) {
            if (post->SibSeq[k] == g->AttSeq) {
                seen = TRUE;
                if (post->SibGen[k] != g->RelGen) {
                    return CM_KE_POPULATION;
                }
            }
        }
        if (!seen) {
            return CM_KE_IDENTITY;
        }
        if (!cmIsLanding(g) || g->ChainLanding != post->Landing ||
            g->ChainMember != (post->MemberBdf & 0xFFFFUL)) {
            return CM_KE_IDENTITY;
        }
        for (w = 0; w < CM_MK_TOK_WORDS; w++) {
            if (g->ChainTok[w] != post->Tok[w]) {
                return CM_KE_TOKEN;
            }
        }
        if (!CmMarksSeqAfter(g->ChainAnsSeq, post->Tok[4])) {
            return CM_KE_UNSETTLED;
        }
        if (!cmLandingReady(g)) {
            return CM_KE_STACK;
        }
    }
    *count = n;
    return 0;
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
    PCM_FILTER lf[XHCI_TBT_CHAIN_MAX];
    ULONG lands[XHCI_TBT_CHAIN_MAX];
    ULONG nl;
    ULONG i;
    ULONG k;
    BOOLEAN active;

    /* Record 34 section 3.12 item 5: the filters' tokens from every
     * domain's bindings, before any domain's grant. */
    cmChainTokSync();
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
            /* Record 34 section 3.10: the member's own set and each chain
             * port's below it, buried first and made after, so the
             * member's bound is freed before a new entry needs it. */
            nl = active ? cmLandings(r->Port[i].Bdf, lands, lf) : 0;
            (VOID)CmMarksBury(&r->Marks, i, r->Port[i].Bdf, 0,
                              active ? (PVOID const *)f->Snap : NULL,
                              active ? f->SnapCount : 0, active,
                              cmMarkNow(d));
            for (k = 0; k < nl; k++) {
                (VOID)CmMarksBury(&r->Marks, i, r->Port[i].Bdf, lands[k],
                                  (PVOID const *)lf[k]->Snap,
                                  lf[k]->SnapCount, TRUE, cmMarkNow(d));
            }
            (VOID)CmMarksBuryLandings(&r->Marks, i, lands, nl,
                                      cmMarkNow(d));
            if (!active) {
                continue;
            }
            (VOID)CmMarksMake(&r->Marks, i, r->Port[i].Bdf, 0,
                              (PVOID const *)f->Snap, f->SnapCount,
                              (BOOLEAN)(f->SnapTruncated != 0), TRUE,
                              cmMarkNow(d));
            (VOID)CmMarksLocate(&r->Marks, i, (PVOID const *)f->Snap,
                                f->SnapLocd, f->SnapBus, f->SnapAddr,
                                f->SnapCount, cmMarkNow(d));
            for (k = 0; k < nl; k++) {
                (VOID)CmMarksMake(&r->Marks, i, r->Port[i].Bdf, lands[k],
                                  (PVOID const *)lf[k]->Snap,
                                  lf[k]->SnapCount,
                                  (BOOLEAN)(lf[k]->SnapTruncated != 0), TRUE,
                                  cmMarkNow(d));
                (VOID)CmMarksLocate(&r->Marks, i,
                                    (PVOID const *)lf[k]->Snap,
                                    lf[k]->SnapLocd, lf[k]->SnapBus,
                                    lf[k]->SnapAddr, lf[k]->SnapCount,
                                    cmMarkNow(d));
                cmChainGrant(d, i, r->Port[i].Bdf, lands[k]);
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
        if (filter->Common.Kind == CM_KIND_CHILD && filter->PdoSeq == 0) {
            /* Record 34 section 3.12 item 5: the PDO's identity, shared
             * with any filter of this driver already on it. */
            for (e = cmRegAtts.Flink;
                 e != &cmRegAtts && filter->PdoSeq == 0; e = e->Flink) {
                f = CONTAINING_RECORD(e, CM_FILTER, AttLink);
                if (f->Common.Kind == CM_KIND_CHILD &&
                    f->Common.Pdo == filter->Common.Pdo && f->PdoSeq != 0) {
                    filter->PdoSeq = f->PdoSeq;
                }
            }
            if (filter->PdoSeq == 0) {
                filter->PdoSeq = cmRegSeqNext();
            }
            filter->AttSeq = cmRegSeqNext();
        }
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
            /* Record 33 section 4.4: else the child filter on a bridge
             * whose last BusRelations reported it (a registered child below
             * a dock's child-filtered upstream port), its immediate
             * ancestor as Parent, the chain's root as Bridge, one deeper. */
            for (e = cmRegAtts.Flink;
                 e != &cmRegAtts && filter->Bridge == NULL &&
                 filter->Parent == NULL;
                 e = e->Flink) {
                f = CONTAINING_RECORD(e, CM_FILTER, AttLink);
                if (f == filter || f->Common.Kind != CM_KIND_CHILD ||
                    f->SurpriseRemoved) {
                    continue;
                }
                for (i = 0; i < f->SnapCount; i++) {
                    if (f->Snap[i] == (PVOID)filter->Common.Pdo) {
                        filter->Parent = f;
                        filter->Bridge = cmTopOf(f);
                        filter->Depth = f->Depth + 1UL;
                        break;
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

/* The PDO's bus number and address, as the PnP manager's devnode has them
 * (record 21 section 7.4, revision 3: never read at the BusRelations
 * attachment, before the devnode exists). PASSIVE_LEVEL. */
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

/* Record 21 section 7.7: the published bridge filter whose last
 * BusRelations snapshot holds `pdo`, or NULL. Under the lock. */
static PCM_FILTER cmRegSnapOwner(PVOID pdo)
{
    PLIST_ENTRY e;
    PCM_FILTER f;
    ULONG i;

    for (e = cmRegFilters.Flink; e != &cmRegFilters; e = e->Flink) {
        f = CONTAINING_RECORD(e, CM_FILTER, RegLink);
        if (!f->Published || f->Gone || f->Common.Kind != CM_KIND_FILTER) {
            continue;
        }
        for (i = 0; i < f->SnapCount && i < CM_SNAP_MAX; i++) {
            if (f->Snap[i] == pdo) {
                return f;
            }
        }
    }
    return NULL;
}

/* Record 21 section 7.7: whether the mark service's value is on `pdo` (an
 * AddDevice child of this driver attached to it: a unit's mark, or a bridge
 * mark of record 33 section 4.3). Under the lock. */
static BOOLEAN cmRegMarkedPdo(PVOID pdo)
{
    PLIST_ENTRY e;
    PCM_FILTER g;

    for (e = cmRegAtts.Flink; e != &cmRegAtts; e = e->Flink) {
        g = CONTAINING_RECORD(e, CM_FILTER, AttLink);
        if ((PVOID)g->Common.Pdo == pdo && g->Common.Kind == CM_KIND_CHILD &&
            g->AddDev) {
            return TRUE;
        }
    }
    return FALSE;
}

/* Record 21 section 7.7: whether `pdo` is a unit by the filters alone: a
 * function a published bridge filter last reported, where neither that
 * bridge nor any bridge filter above it (each reported by the next, up to
 * CM_TUNNEL_DEPTH_MAX) carries a mark of the service's. The service never
 * marks a tunnel port or the host's own switch ports (protected), and marks
 * every unit and every bridge below one, so a switch port inside a dock or
 * an enclosure never counts, whether or not the host's upstream port above
 * the tunnel ports is filtered (record 33 section 4.4). Under the lock. */
static BOOLEAN cmRegUnitOfFilter(PVOID pdo)
{
    PCM_FILTER f;
    ULONG depth;

    f = cmRegSnapOwner(pdo);
    if (f == NULL) {
        return FALSE;
    }
    for (depth = 0; depth <= CM_TUNNEL_DEPTH_MAX; depth++) {
        if (cmRegMarkedPdo((PVOID)f->Common.Pdo)) {
            return FALSE;
        }
        f = cmRegSnapOwner((PVOID)f->Common.Pdo);
        if (f == NULL) {
            return TRUE;
        }
    }
    return FALSE;
}

/* Record 21 section 7.7: the window for an answer before association is
 * still open. Under the lock. */
static BOOLEAN cmRegPreTrustOpen(VOID)
{
    LARGE_INTEGER now;
    ULONG ticks;
    ULONG incr;

    if (cmRegEverListed || cmRegChildFailed ||
        !IsListEmpty(&cmRegDomains)) {
        return FALSE;
    }
    KeQueryTickCount(&now);
    ticks = now.LowPart - cmRegInitTicks.LowPart;
    /* KeQueryTimeIncrement is in 100 ns units; CM_PRETRUST_MS * 10000
     * stays below 2^32, so no 64-bit division is needed. */
    incr = KeQueryTimeIncrement();
    if (incr == 0) {
        incr = 1;
    }
    return (BOOLEAN)(ticks < (CM_PRETRUST_MS * 10000UL) / incr);
}

BOOLEAN CmRegAttMayMark(PCM_FILTER filter)
{
    PLIST_ENTRY e;
    PCM_REENUM r;
    PCM_MARK m;
    PCM_FILTER g;
    KIRQL irql;
    ULONG i;
    ULONG v;
    ULONG note;
    BOOLEAN may;
    BOOLEAN seen;

    may = FALSE;
    seen = FALSE;
    KeAcquireSpinLock(&cmRegLock, &irql);
    if (filter->Located) {
        for (e = cmRegDomains.Flink; e != &cmRegDomains; e = e->Flink) {
            r = CONTAINING_RECORD(e, CM_REENUM, Link);
            if (r->Route != CM_ROUTE_FILTER) {
                continue;
            }
            m = CmMarksFind(&r->Marks, (PVOID)filter->Common.Pdo);
            for (i = 0; i < r->Members; i++) {
                if (m != NULL && m->Landing != 0) {
                    /* A chained unit (record 34 section 3.10): its bus is
                     * the chain port's secondary, the chain port still
                     * classified under this member. */
                    g = (m->Member == i)
                            ? cmLandingFind(r->Port[i].Bdf, m->Landing)
                            : NULL;
                    v = CmPortsMayMark(
                        1, filter->LocBus,
                        (ULONG)((r->Port[i].Flags & CM_PORT_ACTIVE) != 0),
                        (ULONG)((r->Port[i].Flags & CM_PORT_EXCLUSIVE) != 0),
                        (ULONG)(g != NULL), g != NULL ? g->ChainSec : 0,
                        (ULONG)(g != NULL), (ULONG)(r->SafeEject != 0));
                    note = (v << 28) | ((g != NULL ? 1UL : 0UL) << 24) |
                           ((filter->LocBus & 0xFFUL) << 16) |
                           (m->Landing & 0xFFFFUL);
                    if (m->Member == i && filter->ChainMayLog != note) {
                        /* The chained unit's answer, for the log when it
                         * changes: the gate's verdict, the chain port
                         * found, its bus, the chain port. */
                        filter->ChainMayLog = note;
                        CmLogNote(CONTAINING_RECORD(r, CM_NHI, Reenum),
                                  "cm.chain.removable", note);
                    }
                } else {
                    v = CmPortsMayMark(
                        1, filter->LocBus,
                        (ULONG)((r->Port[i].Flags & CM_PORT_ACTIVE) != 0),
                        (ULONG)((r->Port[i].Flags & CM_PORT_EXCLUSIVE) != 0),
                        (ULONG)(r->Port[i].SecBusValid != 0),
                        r->Port[i].SecBus, (ULONG)(m != NULL),
                        (ULONG)(r->SafeEject != 0));
                }
                if (v != CM_MM_NO) {
                    seen = TRUE;
                }
                if (v == CM_MM_YES) {
                    may = TRUE;
                }
            }
        }
    }
    if (!may && filter->Common.Kind == CM_KIND_CHILD && filter->AddDev &&
        cmRegPreTrustOpen() &&
        cmRegUnitOfFilter((PVOID)filter->Common.Pdo)) {
        /* Record 21 section 7.7: no domain has listed yet since this
         * driver loaded, within CM_PRETRUST_MS - a start of Windows, the
         * unit's stack built before its NHI's start - and the
         * PDO is a function a published bridge filter reported, so a unit
         * and not a bridge mark deeper in it. The mark service wrote this
         * value only on a unit it verified behind an active member with
         * safe eject on, and removes it otherwise, so the value stands for
         * the association until a domain lists; the PnP manager records
         * this answer, which it would not ask for again before the next
         * rebuild. */
        may = TRUE;
        seen = TRUE;
        cmRegPreTrust++;
    }
    if (!seen && filter->Common.Kind == CM_KIND_CHILD && filter->AddDev &&
        !filter->Stray) {
        /* A value on a device outside every set: attached and passive. A
         * child attached at BusRelations outside every active set is the
         * tunnel id's chain (record 31 section 4.1), not a stray. */
        filter->Stray = 1;
        cmRegStray++;
    }
    KeReleaseSpinLock(&cmRegLock, irql);
    return may;
}

VOID CmRegChildAddFailed(VOID)
{
    KIRQL irql;

    KeAcquireSpinLock(&cmRegLock, &irql);
    cmRegChildFailed = 1;
    KeReleaseSpinLock(&cmRegLock, irql);
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
    if (filter->ChainLanding != 0) {
        /* A chain port surprise-removed: its chained entries buried
         * (record 34 section 3.10). */
        filter->ChainLanding = 0;
        cmMarksSyncAll();
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
    BOOLEAN resync;

    KeAcquireSpinLock(&cmRegLock, &irql);
    /* Record 33 section 4.4: no child keeps naming this one (a device's
     * children are removed before it, so this only guards the order); and
     * since record 31 section 4.10 a kept child can outlive the filter that
     * attached it or names it as its bridge, so those links end here. */
    for (e = cmRegAtts.Flink; e != &cmRegAtts; e = e->Flink) {
        other = CONTAINING_RECORD(e, CM_FILTER, AttLink);
        if (other->Parent == filter) {
            other->Parent = NULL;
        }
        if (other->Bridge == filter) {
            other->Bridge = NULL;
            other->ChildCounted = 0;
        }
    }
    resync = (BOOLEAN)(filter->ChainLanding != 0);
    filter->ChainLanding = 0;
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
    if (m != NULL && filter->KeptDeparted) {
        /* A kept child whose logical removal recorded the departure
         * (CmRegAttDepart): the entry brought up to date alone. */
        cmMarkAtt(d, m);
    } else if (m != NULL) {
        CmMarksCount(&d->Reenum.Marks, m,
                     afterQr ? CM_MK_REMOVE_AFTER_QR : CM_MK_REMOVE_NO_QR,
                     cmMarkNow(d));
        left = TRUE;
        for (e = cmRegAtts.Flink; e != &cmRegAtts; e = e->Flink) {
            other = CONTAINING_RECORD(e, CM_FILTER, AttLink);
            if (other->Common.Pdo == filter->Common.Pdo &&
                !other->KeptLogical) {
                left = FALSE;
            }
        }
        cmMarkAtt(d, m);
        if (afterQr && left && m->Member < d->Reenum.Members) {
            m->PendingAt = cmMarkNow(d);
            m->PendingGen = CmMarksDepartGen(
                m, d->Reenum.Port[m->Member].ConnGen);
            d->Reenum.Departures++;
            CmMarksTouch(&d->Reenum.Marks, m, cmMarkNow(d));
        }
    }
    if (resync) {
        /* A chain port gone: its chained entries buried (record 34
         * section 3.10). */
        cmMarksSyncAll();
    }
    KeReleaseSpinLock(&cmRegLock, irql);
}

/*
 * A kept child's logical removal (design record 31 section 4.10, K4): its
 * departure recorded once, as CmRegAttRemoved records it, with the child
 * left on the attachment list (still on its PDO, which the eject's
 * stackless test reads) and in its bridge's live count; "the last
 * attachment leaves" counts the other attachments present on the PDO. The
 * child is no presence (KeptLogical), its Removable answer and its chain
 * snapshot dropped, so every PDO its bridge reports after a restart is
 * read as new (a re-created PDO may reuse a deleted one's address).
 * PASSIVE_LEVEL.
 */
VOID CmRegAttDepart(PCM_FILTER filter, BOOLEAN afterQr)
{
    KIRQL irql;
    PCM_NHI d;
    PCM_MARK m;
    PLIST_ENTRY e;
    PCM_FILTER other;
    BOOLEAN left;

    KeAcquireSpinLock(&cmRegLock, &irql);
    filter->KeptLogical = 1;
    filter->Answered = 0;
    filter->SnapCount = 0;
    filter->SnapTruncated = 0;
    m = cmMarkFind((PVOID)filter->Common.Pdo, &d);
    if (filter->KeptDeparted) {
        if (m != NULL) {
            cmMarkAtt(d, m);
        }
    } else {
        filter->KeptDeparted = 1;
        if (m != NULL) {
            CmMarksCount(&d->Reenum.Marks, m,
                         afterQr ? CM_MK_REMOVE_AFTER_QR : CM_MK_REMOVE_NO_QR,
                         cmMarkNow(d));
            left = TRUE;
            for (e = cmRegAtts.Flink; e != &cmRegAtts; e = e->Flink) {
                other = CONTAINING_RECORD(e, CM_FILTER, AttLink);
                if (other != filter &&
                    other->Common.Pdo == filter->Common.Pdo &&
                    !other->KeptLogical) {
                    left = FALSE;
                }
            }
            cmMarkAtt(d, m);
            if (afterQr && left && m->Member < d->Reenum.Members) {
                m->PendingAt = cmMarkNow(d);
                m->PendingGen = CmMarksDepartGen(
                    m, d->Reenum.Port[m->Member].ConnGen);
                d->Reenum.Departures++;
                CmMarksTouch(&d->Reenum.Marks, m, cmMarkNow(d));
            }
        }
    }
    KeReleaseSpinLock(&cmRegLock, irql);
}

/* A kept child no presence (the callback, K8). IRQL: <= DISPATCH_LEVEL. */
VOID CmRegKeptAbsent(PCM_FILTER filter)
{
    KIRQL irql;

    KeAcquireSpinLock(&cmRegLock, &irql);
    filter->KeptLogical = 1;
    cmMarkAttOf(filter);
    KeReleaseSpinLock(&cmRegLock, irql);
}

/* A kept child Attached again (K8): presence back and the departure
 * ended, its PDO's entry brought up to date (which clears a pending
 * departure, as for any attachment present). IRQL: <= DISPATCH_LEVEL. */
VOID CmRegKeptAttached(PCM_FILTER filter)
{
    KIRQL irql;

    KeAcquireSpinLock(&cmRegLock, &irql);
    filter->KeptLogical = 0;
    filter->KeptDeparted = 0;
    cmMarkAttOf(filter);
    KeReleaseSpinLock(&cmRegLock, irql);
}

/*
 * A kept child's record, to the first listed filter-route domain
 * (cmLogDom(NULL)), so that neither Parent nor Bridge is read: the
 * callback can come after both are gone. Value: event in bits 28-31, the
 * caller's IRQL in 24-27, the depth in 16-23, the located
 * bus/device/function in 0-15 (0xFFFF when not located); with the PDO's
 * address. IRQL: <= DISPATCH_LEVEL.
 */
VOID CmRegKeptNote(PCM_FILTER filter, const char *label, ULONG event)
{
    KIRQL irql;
    KIRQL now;
    PCM_NHI dom;
    ULONG bdf;

    now = KeGetCurrentIrql();
    bdf = 0xFFFFUL;
    if (filter->Located) {
        bdf = ((filter->LocBus & 0xFFUL) << 8) |
              (((filter->LocAddress >> 16) & 0x1FUL) << 3) |
              (filter->LocAddress & 7UL);
    }
    KeAcquireSpinLock(&cmRegLock, &irql);
    dom = cmLogDom(NULL);
    if (dom != NULL) {
        CmLogNote(dom, label, ((event & 0xFUL) << 28) |
                                  (((ULONG)now & 0xFUL) << 24) |
                                  ((filter->Depth & 0xFFUL) << 16) | bdf);
        CmLogNoteAddress(dom, "cm.kept.pdo",
                         (ULONG)(ULONG_PTR)filter->Common.Pdo);
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

/*
 * The bridge inventory of the marks answer (design record 33 section 4.3,
 * cm_query.h CM_QB_*): for each member, every child filter on the
 * attachment list whose ancestry reaches the member's published bridge
 * filter and whose device is a PCI-to-PCI bridge, by its location. A child
 * whose kind or location is not known yet, any child no bridge has been
 * named for, or a member with more than CM_QB_MAX_BRIDGES leaves the
 * section incomplete. Under the list's lock.
 */
static VOID cmBridgeInventory(PCM_REENUM r, PULONG br)
{
    PLIST_ENTRY e;
    PCM_FILTER f;
    PCM_FILTER a;
    PCM_FILTER p;
    ULONG bdf;
    ULONG unresolved;
    ULONG i;
    ULONG n;
    ULONG count[CM_QM_MAX_MEMBERS];
    ULONG complete[CM_QM_MAX_MEMBERS];
    ULONG member;
    ULONG at;

    for (i = 0; i < CM_QB_WORDS; i++) {
        br[i] = 0;
    }
    for (i = 0; i < CM_QM_MAX_MEMBERS; i++) {
        count[i] = 0;
        complete[i] = (ULONG)(cmRegUnattributed == 0);
    }
    for (e = cmRegAtts.Flink; e != &cmRegAtts; e = e->Flink) {
        f = CONTAINING_RECORD(e, CM_FILTER, AttLink);
        if (f->Common.Kind != CM_KIND_CHILD || f->SurpriseRemoved ||
            f->ChainKind == CM_CHAIN_OTHER) {
            continue;
        }
        /* Up the ancestry (Parent, else Bridge) to the first ancestor
         * that is one of this set's active members, as CmRegBreqFind
         * walks it: the chain can pass through a member below an inactive
         * filter (a discrete controller's upstream switch port). */
        member = CM_QM_MAX_MEMBERS;
        unresolved = 0;
        a = f;
        for (n = 0; n <= CM_TUNNEL_DEPTH_MAX + 1UL; n++) {
            p = a->Parent;
            if (p == NULL && a->Common.Kind == CM_KIND_CHILD) {
                p = a->Bridge;
            }
            if (p == NULL) {
                /* The root reached, a bridge filter with no member above
                 * it: no member's; or a child no bridge is named for. */
                unresolved = (ULONG)(a->Common.Kind == CM_KIND_CHILD);
                break;
            }
            bdf = cmFiltBdf(p);
            if (bdf == 0xFFFFUL) {
                unresolved = 1;
                break;
            }
            for (i = 0; i < r->Members && i < CM_QM_MAX_MEMBERS; i++) {
                if ((r->Port[i].Bdf & 0xFFFFUL) == bdf &&
                    (r->Port[i].Flags & CM_PORT_ACTIVE) != 0) {
                    member = i;
                }
            }
            if (member != CM_QM_MAX_MEMBERS) {
                break;
            }
            a = p;
        }
        if (member == CM_QM_MAX_MEMBERS && (unresolved ||
                                            n > CM_TUNNEL_DEPTH_MAX + 1UL)) {
            /* Whose it is cannot be told: no section may be read as
             * complete. */
            for (i = 0; i < CM_QM_MAX_MEMBERS; i++) {
                complete[i] = 0;
            }
            continue;
        }
        if (member == CM_QM_MAX_MEMBERS) {
            continue;
        }
        if (f->ChainKind == CM_CHAIN_UNKNOWN || !f->Located ||
            count[member] >= CM_QB_MAX_BRIDGES) {
            complete[member] = 0;
            continue;
        }
        at = member * (CM_QB_SEC_BYTES / 4UL) + 1UL + count[member];
        br[at] = XHCI_TBT_BDF(f->LocBus, f->LocAddress >> 16,
                              f->LocAddress & 0xFFFFUL) & 0xFFFFUL;
        count[member]++;
    }
    for (i = 0; i < r->Members && i < CM_QM_MAX_MEMBERS; i++) {
        if ((r->Port[i].Flags & CM_PORT_ACTIVE) == 0) {
            continue;
        }
        br[i * (CM_QB_SEC_BYTES / 4UL)] =
            (r->Port[i].Bdf & 0xFFFFUL) | (count[i] << 16) |
            ((CM_QB_F_MEMBER | (complete[i] ? CM_QB_F_COMPLETE : 0UL))
             << 24);
    }
}

ULONG CmReenumMarksBuild(PCM_NHI nhi, UCHAR *out, BOOLEAN readAny,
                         ULONG readGen)
{
    PCM_REENUM r;
    CM_QM_IN in;
    KIRQL irql;
    ULONG bytes;
    ULONG br[CM_QB_WORDS];

    r = &nhi->Reenum;
    in.EpochLo = nhi->QueryEpochLo;
    in.EpochHi = nhi->QueryEpochHi;
    in.NhiBdf = nhi->QueryBdf;
    in.SafeEjectOff = (ULONG)(r->SafeEject == 0);
    in.Bridges = br;
    KeAcquireSpinLock(&cmRegLock, &irql);
    cmBridgeInventory(r, br);
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
    PCM_FILTER sib[CM_KID_SIBS];
    const TBT_U4_KID *kd;
    PCM_KID_EJECT post;
    ULONG gens[XHCI_TBT_MAX_TUNNELS];
    ULONG kgens[CM_CHAIN_KIDS];
    ULONG why;
    ULONG i;
    ULONG n;
    ULONG w;
    ULONG kid;
    ULONG landLog;
    ULONG member;
    KIRQL irql;
    BOOLEAN posted;
    BOOLEAN chained;

    r = &nhi->Reenum;
    posted = FALSE;
    chained = FALSE;
    member = 0;
    kid = 0;
    landLog = 0;
    /* The kids' generations now (record 34 section 3.12 item 6), read on
     * this thread, which alone changes them. */
    for (i = 0; i < CM_CHAIN_KIDS; i++) {
        kd = nhi->U4On ? TbtU4Kid(&nhi->U4, i + 1UL) : NULL;
        kgens[i] = (kd != NULL && kd->Used) ? kd->Generation : 0;
    }
    KeAcquireSpinLock(&cmRegLock, &irql);
    for (i = 0; i < XHCI_TBT_MAX_TUNNELS; i++) {
        gens[i] = i < r->Members ? r->Port[i].ConnGen : 0;
    }
    m = NULL;
    why = CM_MK_CONF_SERIAL;
    if (r->Listed && r->Route == CM_ROUTE_FILTER) {
        m = CmMarksConfirmEx(&r->Marks, v, bytes, nhi->QueryEpochLo,
                             nhi->QueryEpochHi, (BOOLEAN)(nhi->U4On != 0),
                             gens, r->Members, kgens, CM_CHAIN_KIDS, &why);
    }
    if (m != NULL && m->Landing != 0) {
        /* A chained unit's (item 6): posted for its kid alone, with every
         * filter on its chain port's PDO as it stands. */
        chained = TRUE;
        kid = (m->Tok[0] >> 8) & 0xFFUL;
        landLog = m->Landing & 0xFFFFUL;
        f = cmLandingFind(m->MemberBdf, m->Landing);
        n = 0;
        if (f != NULL) {
            n = cmKidSibs(f->PdoSeq, sib, CM_KID_SIBS);
        }
        for (i = 0; i < n && i < CM_KID_SIBS; i++) {
            if (sib[i]->ChainTok[0] != m->Tok[0] ||
                sib[i]->ChainTok[1] != m->Tok[1] ||
                sib[i]->ChainTok[2] != m->Tok[2]) {
                n = 0;
            }
        }
        if (f != NULL && n != 0 && n <= CM_KID_SIBS && kid >= 1 &&
            kid <= CM_CHAIN_KIDS && r->KidEjectHook != NULL) {
            post = &r->KidEject[kid - 1];
            CmZero(post, sizeof(*post));
            post->Posted = 1;
            for (w = 0; w < CM_MK_TOK_WORDS; w++) {
                post->Tok[w] = f->ChainTok[w];
            }
            post->MemberBdf = m->MemberBdf;
            post->Landing = m->Landing;
            post->PdoSeq = f->PdoSeq;
            post->Sibs = n;
            for (i = 0; i < n; i++) {
                post->SibSeq[i] = sib[i]->AttSeq;
                post->SibGen[i] = sib[i]->RelGen;
            }
            posted = TRUE;
        } else {
            why = CM_MK_CONF_IDENTITY;
        }
        m->PendingAt = 0;
        m->PendingGen = 0;
        m->LeftPendingAt = 0;
        m->LeftPendingGen = 0;
        CmMarksTouch(&r->Marks, m, cmMarkNow(nhi));
    } else if (m != NULL && m->Member < r->Members) {
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
        m->LeftPendingAt = 0;       /* a tombstone's, confirmed once */
        m->LeftPendingGen = 0;
        CmMarksTouch(&r->Marks, m, cmMarkNow(nhi));
    }
    if (posted) {
        r->Confirms++;
    } else {
        r->ConfirmsRefused++;
    }
    KeReleaseSpinLock(&cmRegLock, irql);
    if (posted && chained) {
        CmLogNote(nhi, "cm.chain.confirm", (kid << 16) | landLog);
    } else if (posted) {
        CmLogNote(nhi, "cm.reenum.confirm", r->Port[member].Bdf);
    } else {
        CmLogNote(nhi, "cm.reenum.confirm.refused", why);
    }
}

/* ---- the resume mark -------------------------------------------------- */

VOID CmRegPassD0(PCM_FILTER filter)
{
    KIRQL irql;

    KeAcquireSpinLock(&cmRegLock, &irql);
    if (filter->Published && !filter->Gone && filter->Held &&
        filter->HeldBy != NULL) {
        cmRegPassed++;
        /* The holder is used under the list's lock only, which its
         * delisting takes too (order: cmRegLock, then LogLock). */
        CmLogNote(filter->HeldBy, "cm.reenum.hold.passed", filter->Bdf);
    }
    KeReleaseSpinLock(&cmRegLock, irql);
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
            (VOID)CmWaitEvent(&done, 0);
        }
        status = irp->IoStatus.Status;
        written = (ULONG)irp->IoStatus.Information;
        IoFreeIrp(irp);
    }
    cmRegRelease(f);
    /* A short write would leave the select where it was. */
    return (BOOLEAN)(NT_SUCCESS(status) && written == width);
}

VOID CmRegMemberRecords(PCM_NHI nhi, PULONG record)
{
    PCM_FILTER f;
    KIRQL irql;
    ULONG i;

    KeAcquireSpinLock(&cmRegLock, &irql);
    for (i = 0; i < XHCI_TBT_MAX_TUNNELS; i++) {
        record[i] = 0;
        if (i < nhi->Reenum.Members) {
            f = cmRegFind(nhi->Reenum.Port[i].Bdf);
            if (f != NULL && f->Owner == nhi) {
                record[i] = f->Serial;
            }
        }
    }
    KeReleaseSpinLock(&cmRegLock, irql);
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
static BOOLEAN cmVanishSweep(PCM_NHI nhi, ULONG i, ULONG gen, BOOLEAN own,
                             ULONG serial);

static VOID cmReenumOne(PCM_NHI nhi, ULONG i, BOOLEAN removeVanished,
                        BOOLEAN fresh, ULONG tunnel, BOOLEAN mark,
                        BOOLEAN expose)
{
    PCM_REENUM r;

    r = &nhi->Reenum;
    if (removeVanished &&
        cmVanishSweep(nhi, i, CM_EJ_SWEEP_NO_GEN, FALSE, 0)) {
        /* Task 43.47: the removal is the member's sweep's now, and its
         * gate is up: the exposure and re-enumeration held behind it,
         * with their publication mode and marking window, run once every
         * vanished child is retired. */
        r->Port[i].Held |= CM_HELD_REENUM | (fresh ? CM_HELD_FRESH : 0UL);
        r->Port[i].HeldExpose |= expose ? 1UL : 0UL;
        r->Port[i].HeldMark |= mark ? 1UL : 0UL;
        CmLogNote(nhi, "cm.reenum.vanish-held", r->Port[i].Bdf);
        return;
    }
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
 * Task 43.47: member i's vanished children are its sweep's (cm_eject.h),
 * entered where the CONFIGMG route has call workers and the host interface
 * is live; TRUE when it was. Otherwise the worker's removal, as before.
 */
static BOOLEAN cmVanishSweep(PCM_NHI nhi, ULONG i, ULONG gen, BOOLEAN own,
                             ULONG serial)
{
    PCM_REENUM r;

    r = &nhi->Reenum;
    if (!(r->Route == CM_ROUTE_CONFIGMG && r->Wk != NULL &&
          r->Callwk != NULL && cmTransport(nhi) &&
          CmEjectSweep(nhi, i, gen, own, serial))) {
        return FALSE;
    }
    /* Task 46.13 (record 21 section 8.2): the asking action's kind and
     * generation, and the member's connection generation now. */
    CmLogNote(nhi, "cm.vanish.sweep.gen",
              (gen & 0xF0000000UL) | ((i & 0xFUL) << 24) |
                  (gen & CM_EJ_SWEEP_NO_GEN));
    CmLogNote(nhi, "cm.vanish.sweep.now",
              ((i & 0xFFUL) << 24) | (CmNhiMemberGen(nhi, i) & 0xFFFFFFUL));
    return TRUE;
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
    ULONG downMember;
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
    /* Task 46.13 (record 21 section 8.3): a USB4 DOWN's own member, whose
     * sweep the guard binds; a chained tunnel's names none. */
    downMember = XHCI_TBT_NO_MEMBER;
    if (action->Kind == TBT_ICM_ACT_TUNNEL_DOWN && nhi->U4On) {
        downMember = CmNhiActionMember(nhi, action);
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
        if (vanish &&
            cmVanishSweep(nhi, i,
                          (action->Kind << 28) |
                              (action->Serial & CM_EJ_SWEEP_NO_GEN),
                          (BOOLEAN)(downMember == i), action->Serial)) {
            /* Task 43.47: the vanished children are read now, at the
             * down, whatever the member's gates; the call below is held
             * behind the sweep's gate without its removal. */
            vanish = FALSE;
        }
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

/*
 * The ACPI-named source at a discovery (record 22 section 7.6.1): every
 * current claim naming this NHI offered to the set, the claims copied as
 * scalars under the list's lock and offered after it is released. The
 * serial each was made under is returned in serials[], parallel to bdfs[],
 * for the members' AcpiSerial. Returns how many were offered.
 */
static ULONG cmAcpiOffer(PCM_NHI nhi, PULONG bdfs, PULONG serials)
{
    PCM_REENUM r;
    PLIST_ENTRY e;
    PCM_FILTER f;
    KIRQL irql;
    ULONG n;
    ULONG unclaimed;
    ULONG i;
    ULONG rc;

    r = &nhi->Reenum;
    n = 0;
    unclaimed = 0;
    if (r->AcpiNameLen == 0) {
        return 0;
    }
    KeAcquireSpinLock(&cmRegLock, &irql);
    r->AcpiGenSeen = cmRegAcpiGen;
    for (e = cmRegFilters.Flink; e != &cmRegFilters; e = e->Flink) {
        f = CONTAINING_RECORD(e, CM_FILTER, RegLink);
        if (!f->Published || f->Gone) {
            continue;
        }
        if (f->AcpiRead && !f->AcpiClaim) {
            unclaimed++;
        }
        if (f->AcpiClaim && CmDsdSameName(f->AcpiRef, r->AcpiName)) {
            if (n < XHCI_TBT_MAX_TUNNELS) {
                bdfs[n] = f->Bdf;
                serials[n] = f->AcpiSerial;
            }
            n++;
        }
    }
    KeReleaseSpinLock(&cmRegLock, irql);
    if (unclaimed != 0) {
        CmLogNote(nhi, "cm.acpi.unclaimed", unclaimed);
    }
    if (CmDsdClaimsTaken(n) == 0) {
        /* Two or more ports claim this NHI: none is taken (record 22
         * section 7.6.1); the router-named source binds as before. */
        if (n != 0) {
            CmLogNote(nhi, "cm.acpi.several", n);
        }
        return 0;
    }
    for (i = 0; i < n; i++) {
        rc = XhciTbtTopoAddAcpi(&r->Cfg, &r->Topo, bdfs[i]);
        CmLogNote(nhi, "cm.acpi.offer", (bdfs[i] << 8) | (rc & 0xFFUL));
        if (r->Topo.Outcome == XHCI_TBT_TOPO_INCOMPLETE ||
            r->Topo.Outcome == XHCI_TBT_TOPO_MANY) {
            break;
        }
    }
    return n;
}

static VOID cmDiscover(PCM_NHI nhi)
{
    PCM_REENUM r;
    XHCI_TBT_TOPO_IN in;
    const CM_FAMILY *family;
    ULONG tries;
    ULONG i;
    ULONG k;
    ULONG acpi;
    ULONG acpiBdf[XHCI_TBT_MAX_TUNNELS];
    ULONG acpiSerial[XHCI_TBT_MAX_TUNNELS];

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
    /* Then the ACPI-named ones (record 22 section 7.6.1). */
    acpi = cmAcpiOffer(nhi, acpiBdf, acpiSerial);
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
        if (r->Port[i].Source == XHCI_TBT_SRC_ACPI) {
            for (k = 0; k < acpi; k++) {
                if (acpiBdf[k] == r->Port[i].Bdf) {
                    r->Port[i].AcpiSerial = acpiSerial[k];
                }
            }
        }
        r->Members++;
        CmLogNote(nhi, "cm.reenum.member",
                  (r->Port[i].Bdf << 8) | r->Port[i].Source);
    }
}

VOID CmReenumStart(PCM_NHI nhi)
{
    PCM_REENUM r;
    KIRQL irql;
    ULONG i;

    r = &nhi->Reenum;
    r->Starts++;
    /* Record 21 section 6.3.1: no release of an earlier start is waited
     * for, then the domain opened. */
    KeInitializeEvent(&r->WpuSendDone, NotificationEvent, TRUE);
    r->WpuSending = 0;
    r->WpuArmed = 0;
    r->WpuAsked = 0;
    r->WpuOk = 0;
    r->WpuFailed = 0;
    r->WpuNone = 0;
    for (i = 0; i < XHCI_TBT_MAX_TUNNELS; i++) {
        r->Port[i].WpuRelEpoch = 0;
        r->Port[i].WpuRelAt = 0;
    }
    KeAcquireSpinLock(&cmRegLock, &irql);
    r->WpuClosed = 0;
    KeReleaseSpinLock(&cmRegLock, irql);
    CmEjectReset(nhi);
    r->Route = CM_ROUTE_NONE;
    r->Why = CM_NOROUTE_NONE;
    r->Members = 0;
    r->EjectPending = 0;
    CmZero(r->KidEject, sizeof(r->KidEject));
    CmZero(r->ChainLand, sizeof(r->ChainLand));
    r->MarkOpen = 0;
    r->CfgmgOwed = 0;
    r->LossDrain = 0;
    r->LossSeen = 0;
    r->Deferred = 0;
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
    r->AcpiNameLen = CmDsdNhiName(nhi, r->AcpiName, sizeof(r->AcpiName));
    r->AcpiLateLogged = 0;
    CmLogNote(nhi, "cm.acpi.name", r->AcpiNameLen);
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
    ULONG pre;

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
    cmRegEverListed = 1;
    cmRegRecompute();
    pre = cmRegPreTrust;
    KeReleaseSpinLock(&cmRegLock, irql);
    CmLogNote(nhi, "cm.reenum.child.pretrust", pre);

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
    ULONG mask;

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
    /* Task 43.47: no member's CONFIGMG worker claim becomes a vanished
     * transaction until that member's call worker has opened its route
     * (CmReenumService posts the mask as the openings are read back). */
    r->CallUsable = 0;
    CmCfgwkTakeover(r->Wk, 0);
    if (nhi->U4On) {
        /* Record 21 section 6.1.2: a worker made after a recovery's commit
         * (a loss during the bind) takes the hold in force before any
         * work is posted to it. */
        nhi->RecWkHold = CmNhiU4WorkerHold(nhi);
        CmCfgwkHold(r->Wk, nhi->RecWkHold);
    }
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

/* ---- NT: the chain action (task 45.13, design record 34 section 3.9) -- */

/*
 * The child filter on the PDO located at `bdf` (16-bit XHCI_TBT_BDF), its
 * removal reference taken under the list's lock (CmFilterTryHold): TRUE
 * with *child and *pdo. Release with CmFilterUnhold.
 */
static BOOLEAN cmChainTake(ULONG bdf, PCM_FILTER *child, PDEVICE_OBJECT *pdo)
{
    KIRQL irql;
    PLIST_ENTRY e;
    PCM_FILTER f;
    BOOLEAN ok;

    ok = FALSE;
    KeAcquireSpinLock(&cmRegLock, &irql);
    for (e = cmRegAtts.Flink; e != &cmRegAtts && !ok; e = e->Flink) {
        f = CONTAINING_RECORD(e, CM_FILTER, AttLink);
        if (f->Common.Kind != CM_KIND_CHILD || !f->AttListed ||
            !f->Located || f->SurpriseRemoved ||
            XHCI_TBT_BDF(f->LocBus, f->LocAddress >> 16,
                         f->LocAddress & 0xFFFFUL) != bdf) {
            continue;
        }
        if (CmFilterTryHold(f)) {
            *child = f;
            *pdo = f->Common.Pdo;
            ok = TRUE;
        }
    }
    KeReleaseSpinLock(&cmRegLock, irql);
    return ok;
}

/*
 * One try of a chain action: every active member this domain owns (the
 * action's own member once one is named) walked, each candidate not yet
 * done for the entry `e` invalidated through the child filter on it and
 * recorded done. TRUE when every walk was complete, at least one candidate
 * was found, and every candidate found is done; else *missing says what
 * was lacking (XHCI_TBT_CHAIN_MISS_*). PASSIVE_LEVEL, the thread.
 */
static BOOLEAN cmChainTry(PCM_NHI nhi, const TBT_ICM_ACTION *action,
                          PXHCI_TBT_CHAIN_OWED e, PULONG missing)
{
    PCM_REENUM r;
    PXHCI_TBT_CHAIN_SET s;
    PCM_FILTER f;
    PDEVICE_OBJECT pdo;
    ULONG member;
    ULONG i;
    ULONG k;
    ULONG found;
    ULONG left;
    BOOLEAN incomplete;

    r = &nhi->Reenum;
    s = &r->ChainSet;
    member = nhi->U4On ? CmNhiActionMember(nhi, action) : XHCI_TBT_NO_MEMBER;
    found = 0;
    left = 0;
    incomplete = FALSE;
    for (i = 0; i < r->Members; i++) {
        if ((member != XHCI_TBT_NO_MEMBER && i != member) ||
            (cmFlags(nhi, i) & CM_PORT_ACTIVE) == 0 ||
            (nhi->RecBarred & (1UL << i)) != 0) {
            continue;
        }
        if (XhciTbtChainPorts(&r->Cfg, r->Port[i].Bdf, i, s) !=
            XHCI_TBT_CHAIN_COMPLETE) {
            incomplete = TRUE;
            r->ChainIncomplete++;
            CmLogNote(nhi, "cm.chain.incomplete",
                      (s->Why << 24) | (i << 16) | (s->FailBdf & 0xFFFFUL));
            continue;
        }
        r->ChainPorts += s->Count;
        CmLogNote(nhi, "cm.chain.ports", (i << 16) | s->Count);
        if (s->Over != 0) {
            r->ChainOver += s->Over;
            CmLogNote(nhi, "cm.chain.over", (i << 16) | s->Over);
        }
        for (k = 0; k < s->Count; k++) {
            found++;
            if (XhciTbtChainDone(e, s->Port[k].Bdf)) {
                continue;
            }
            if (s->Port[k].Unpowered) {
                r->ChainUnpowered++;
                CmLogNote(nhi, "cm.chain.port-unpowered", s->Port[k].Bdf);
            }
            if (!cmChainTake(s->Port[k].Bdf, &f, &pdo)) {
                left++;
                continue;
            }
            IoInvalidateDeviceRelations(pdo, BusRelations);
            CmFilterUnhold(f);
            XhciTbtChainMark(e, s->Port[k].Bdf);
            r->ChainInvalidate++;
            r->Calls++;
            CmLogNote(nhi, "cm.chain.invalidate",
                      (action->Kind << 24) | ((action->Depth & 0xFFUL) << 16) |
                      s->Port[k].Bdf);
        }
    }
    if (incomplete) {
        *missing = XHCI_TBT_CHAIN_MISS_WALK;
    } else if (found == 0) {
        *missing = XHCI_TBT_CHAIN_MISS_NONE;
    } else {
        *missing = XHCI_TBT_CHAIN_MISS_FILTER;
    }
    return (BOOLEAN)(!incomplete && found != 0 && left == 0);
}

/* Every owed chain action dropped (a loss, the stop), counted. */
static VOID cmChainDropAll(PCM_NHI nhi, const char *why)
{
    PCM_REENUM r;
    ULONG k;

    r = &nhi->Reenum;
    for (k = 0; k < CM_CHAIN_OWED_MAX; k++) {
        if (r->ChainOwed[k].Used) {
            r->ChainOwed[k].Used = 0;
            r->ChainCancelled++;
            CmLogNote(nhi, why, (r->ChainAction[k].Depth << 16) |
                                (r->ChainOwed[k].Key & 0xFFFFUL));
        }
    }
}

/*
 * A tunnel change at depth 2 or more on the NT filter route, after the
 * members' own handling (record 34 sections 3.2 and 3.9). An up is tried
 * now and kept owed, keyed by the chained device's identity (the action's
 * Slot), until every candidate is invalidated; a down, and a
 * reconciliation (RESYNC), is tried once.
 */
static VOID cmChainAction(PCM_NHI nhi, const TBT_ICM_ACTION *action)
{
    PCM_REENUM r;
    XHCI_TBT_CHAIN_OWED once;
    ULONG missing;
    ULONG k;
    ULONG what;
    BOOLEAN fresh;

    r = &nhi->Reenum;
    missing = XHCI_TBT_CHAIN_MISS_NONE;
    what = (action->Depth << 16) | (action->Slot & 0xFFFFUL);
    if (action->Kind != TBT_ICM_ACT_TUNNEL_UP) {
        CmZero(&once, sizeof(once));
        if (!cmChainTry(nhi, action, &once, &missing) &&
            missing != XHCI_TBT_CHAIN_MISS_NONE) {
            CmLogNote(nhi, "cm.chain.down.missed",
                      (action->Kind << 28) | (missing << 24) | what);
        }
        return;
    }
    fresh = TRUE;
    for (k = 0; k < CM_CHAIN_OWED_MAX; k++) {
        if (r->ChainOwed[k].Used && r->ChainOwed[k].Key == action->Slot &&
            r->ChainOwed[k].Serial == action->Serial) {
            fresh = FALSE;
        }
    }
    k = XhciTbtChainOwe(r->ChainOwed, CM_CHAIN_OWED_MAX, action->Slot,
                        action->Serial, missing, cmNow(nhi));
    if (k >= CM_CHAIN_OWED_MAX) {
        /* Tried once, not kept. */
        r->ChainOwedFull++;
        CmLogNote(nhi, "cm.chain.owed-full", what);
        CmZero(&once, sizeof(once));
        (VOID)cmChainTry(nhi, action, &once, &missing);
        return;
    }
    r->ChainAction[k] = *action;
    if (cmChainTry(nhi, action, &r->ChainOwed[k], &missing)) {
        r->ChainOwed[k].Used = 0;
        return;
    }
    r->ChainOwed[k].Missing = missing;
    if (fresh) {
        r->ChainOwedCount++;
    }
    CmLogNote(nhi, "cm.chain.owed", (missing << 24) | what);
}

/*
 * Each pass of the thread: every owed chain action tried again, cancelled
 * when no longer live, dropped at 10 s (record 34 section 3.9).
 */
static VOID cmChainService(PCM_NHI nhi)
{
    PCM_REENUM r;
    PXHCI_TBT_CHAIN_OWED e;
    ULONG missing;
    ULONG k;
    ULONG what;

    r = &nhi->Reenum;
    for (k = 0; k < CM_CHAIN_OWED_MAX; k++) {
        e = &r->ChainOwed[k];
        if (!e->Used) {
            continue;
        }
        what = (r->ChainAction[k].Depth << 16) | (e->Key & 0xFFFFUL);
        if (r->Route != CM_ROUTE_FILTER || r->Reenum == 0 ||
            !CmNhiActionLive(nhi, &r->ChainAction[k])) {
            e->Used = 0;
            r->ChainCancelled++;
            CmLogNote(nhi, "cm.chain.cancelled", what);
            continue;
        }
        if (!cmTransport(nhi)) {
            /* No bridge read while the host interface is lost; the loss
             * drops the entry (CmReenumLossBegin). */
            continue;
        }
        if (XhciTbtChainOwedExpired(e, cmNow(nhi))) {
            /* The deadline first: an entry past it is never acted
             * on, however late this pass runs. */
            missing = e->Missing;
            e->Used = 0;
            if (missing == XHCI_TBT_CHAIN_MISS_FILTER) {
                r->ChainNoFilter++;
                CmLogNote(nhi, "cm.chain.nofilter", what);
            } else {
                r->ChainOwedExpired++;
                CmLogNote(nhi, "cm.chain.owed-expired",
                          (missing << 24) | what);
            }
            continue;
        }
        missing = e->Missing;
        if (cmChainTry(nhi, &r->ChainAction[k], e, &missing)) {
            e->Used = 0;
            CmLogNote(nhi, "cm.chain.owed.done", what);
            continue;
        }
        e->Missing = missing;
    }
}


/* ---- the chained unit's eject (task 48.30, record 34 section 3.12) ---- */

VOID CmReenumChainSet(PCM_NHI nhi, ULONG i, ULONG memberBdf, ULONG bdf,
                      ULONG sec, const ULONG *tok)
{
    PCM_CHAIN_LAND l;
    ULONG landing;
    ULONG w;
    KIRQL irql;

    if (i >= CM_CHAIN_LANDS) {
        return;
    }
    landing = (bdf & 0xFFFFUL) | CM_MK_LANDING_VALID;
    KeAcquireSpinLock(&cmRegLock, &irql);
    l = &nhi->Reenum.ChainLand[i];
    if (!l->Used || l->Landing != landing || l->MemberBdf != memberBdf ||
        l->Sec != sec) {
        CmZero(l, sizeof(*l));
        l->Used = 1;
        l->MemberBdf = memberBdf;
        l->Landing = landing;
        l->Sec = sec;
    }
    for (w = 0; w < CM_MK_TOK_WORDS; w++) {
        l->Tok[w] = tok != NULL ? tok[w] : 0;
    }
    cmMarksSyncAll();
    KeReleaseSpinLock(&cmRegLock, irql);
}

VOID CmReenumChainClear(PCM_NHI nhi, ULONG i)
{
    KIRQL irql;

    if (i >= CM_CHAIN_LANDS) {
        return;
    }
    KeAcquireSpinLock(&cmRegLock, &irql);
    if (nhi->Reenum.ChainLand[i].Used) {
        CmZero(&nhi->Reenum.ChainLand[i], sizeof(CM_CHAIN_LAND));
        cmMarksSyncAll();
    }
    KeReleaseSpinLock(&cmRegLock, irql);
}

BOOLEAN CmReenumChainWrong(PCM_NHI nhi, ULONG i)
{
    KIRQL irql;
    BOOLEAN wrong;

    if (i >= CM_CHAIN_LANDS) {
        return FALSE;
    }
    KeAcquireSpinLock(&cmRegLock, &irql);
    wrong = (BOOLEAN)(nhi->Reenum.ChainLand[i].Used &&
                      nhi->Reenum.ChainLand[i].Wrong);
    KeReleaseSpinLock(&cmRegLock, irql);
    return wrong;
}

VOID CmReenumChainKick(PCM_NHI nhi, ULONG bdf)
{
    PCM_FILTER f;
    PDEVICE_OBJECT pdo;

    if (nhi->Reenum.Route != CM_ROUTE_FILTER ||
        !cmChainTake(bdf & 0xFFFFUL, &f, &pdo)) {
        return;
    }
    IoInvalidateDeviceRelations(pdo, BusRelations);
    CmFilterUnhold(f);
    nhi->Reenum.Calls++;
    CmLogNote(nhi, "cm.chain.kick", bdf & 0xFFFFUL);
}

VOID CmReenumSetKidEject(PCM_NHI nhi, CM_REENUM_KIDEJECT hook)
{
    nhi->Reenum.KidEjectHook = hook;
}

BOOLEAN CmReenumKidRecheck(PCM_NHI nhi, const CM_KID_EJECT *post)
{
    PCM_FILTER found[CM_KID_SIBS];
    ULONG n;
    ULONG why;
    KIRQL irql;

    UNREFERENCED_PARAMETER(nhi);
    KeAcquireSpinLock(&cmRegLock, &irql);
    why = cmKidLandingCheck(post, found, &n);
    KeReleaseSpinLock(&cmRegLock, irql);
    return (BOOLEAN)(why == 0);
}

/*
 * The posted kid ejects at the hand-off (record 34 section 3.12 item 7),
 * one a pass: item 3 under the lock with the holds taken, then the hook
 * (cm_nhi.c, items 4 to 8 and the teardown), the holds released after it.
 */
static VOID cmKidEjects(PCM_NHI nhi)
{
    PCM_REENUM r;
    CM_KID_EJECT post;
    PCM_FILTER held[CM_KID_SIBS];
    ULONG n;
    ULONG h;
    ULONG k;
    ULONG why;
    KIRQL irql;
    BOOLEAN ok;

    r = &nhi->Reenum;
    for (k = 0; k < CM_CHAIN_KIDS; k++) {
        KeAcquireSpinLock(&cmRegLock, &irql);
        if (!r->KidEject[k].Posted) {
            KeReleaseSpinLock(&cmRegLock, irql);
            continue;
        }
        post = r->KidEject[k];
        r->KidEject[k].Posted = 0;
        n = 0;
        h = 0;
        why = r->KidEjectHook != NULL ? cmKidLandingCheck(&post, held, &n)
                                      : CM_KE_REFUSED;
        for (h = 0; why == 0 && h < n; h++) {
            if (!CmFilterTryHold(held[h])) {
                why = CM_KE_HOLD;
                break;
            }
        }
        if (why != 0) {
            while (h > 0) {
                h--;
                CmFilterUnhold(held[h]);
            }
        } else {
            for (h = 0; h < n; h++) {
                held[h]->EjectHold = 1;
            }
        }
        KeReleaseSpinLock(&cmRegLock, irql);
        if (why != 0) {
            r->KidEjectsStale++;
            CmLogNote(nhi, "cm.chain.eject.stale", (why << 16) | (k + 1UL));
            continue;
        }
        r->KidEjects++;
        CmLogNote(nhi, "cm.chain.eject",
                  ((k + 1UL) << 16) | (post.Landing & 0xFFFFUL));
        ok = r->KidEjectHook(nhi, &post);
        KeAcquireSpinLock(&cmRegLock, &irql);
        for (h = 0; h < n; h++) {
            held[h]->EjectHold = 0;
        }
        KeReleaseSpinLock(&cmRegLock, irql);
        for (h = 0; h < n; h++) {
            CmFilterUnhold(held[h]);
        }
        if (!ok) {
            r->KidEjectsStale++;
        }
        return;
    }
}

/* Record 34 section 3.3.7 item 1: the members a chained tunnel's change
 * concerns on the CONFIGMG route: the kid's port's member where it is known
 * by correspondence (the USB4 profile), else every member this domain
 * owns. */
static ULONG cmChainMembers(PCM_NHI nhi, const TBT_ICM_ACTION *action)
{
    ULONG member;

    member = nhi->U4On ? CmNhiPortMember(nhi, action->Link)
                       : XHCI_TBT_NO_MEMBER;
    if (member != XHCI_TBT_NO_MEMBER && member < nhi->Reenum.Members) {
        return 1UL << member;
    }
    return cmOwnedMask(nhi);
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
        if (r->Route == CM_ROUTE_CONFIGMG && CmNhiActionLive(nhi, action)) {
            /* Task 43.47f: a re-plug is the event that most often follows
             * the holder's release; a vetoed vanished child on the member
             * asks again now instead of at its backed-off time. A stale up
             * (the device gone again, a later generation) nudges nothing. */
            CmEjectNudge(nhi, nhi->U4On ? CmNhiActionMember(nhi, action)
                                        : XHCI_TBT_NO_MEMBER);
        }
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
        if (action->Depth >= 2 && r->Route == CM_ROUTE_CONFIGMG &&
            r->Wk != NULL) {
            /* A chained tunnel on 98 SE and ME: its chain port numbered
             * and re-enumerated by the worker (task 48.31), and no member
             * re-enumerated before it - PCI.VXD would take a chained
             * switch's bridges in reset state as they are and the chain
             * port would hold them; the worker falls back to the member's
             * re-enumeration when the request runs out (record 34 section
             * 3.3.7 item 1). */
            CmCfgwkChain(r->Wk, cmChainMembers(nhi, action), TRUE, FALSE);
        } else {
            cmReenumAll(nhi, FALSE, TRUE, action);
        }
        if (action->Depth >= 2 && r->Route == CM_ROUTE_FILTER) {
            /* A chained tunnel: its chain port's bus (task 45.13). */
            cmChainAction(nhi, action);
        }
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
        if (action->Depth >= 2 && r->Route == CM_ROUTE_FILTER) {
            /* A chained tunnel down: its chain port's bus (task 45.13). */
            cmChainAction(nhi, action);
        }
        if (action->Depth >= 2 && r->Route == CM_ROUTE_CONFIGMG &&
            r->Wk != NULL && action->Why != TBT_ICM_WHY_UNAVAILABLE) {
            /* The chained device's devnodes removed (task 48.31). */
            CmCfgwkChain(r->Wk, cmChainMembers(nhi, action), FALSE, TRUE);
        }
    } else {
        cmReenumAll(nhi, TRUE, TRUE, action);
        if (r->Route == CM_ROUTE_FILTER &&
            action->Why != TBT_ICM_WHY_RECOVERED &&
            action->Why != TBT_ICM_WHY_UNAVAILABLE) {
            /* A reconciliation may concern a chained device: its
             * chain port's bus too (task 45.13). */
            cmChainAction(nhi, action);
        }
        if (r->Route == CM_ROUTE_CONFIGMG && r->Wk != NULL &&
            action->Why != TBT_ICM_WHY_RECOVERED &&
            action->Why != TBT_ICM_WHY_UNAVAILABLE) {
            /* And on 98 SE and ME (task 48.31). */
            CmCfgwkChain(r->Wk, cmOwnedMask(nhi), TRUE, TRUE);
        }
    }
    if (r->Route == CM_ROUTE_CONFIGMG && r->Wk != NULL &&
        r->SafeEject != 0 && action->Kind != TBT_ICM_ACT_TUNNEL_DOWN) {
        /* The worker's marking window (section 7.4). */
        cmWkPost(nhi, 0, 0, TRUE);
    }
    /* A tunnel's bridge may have been given its buses meanwhile. */
    cmSecBuses(nhi);
}

/*
 * A claim made or withdrawn since this domain last looked (record 22
 * section 7.6.1): with no set, the rediscovery is armed again so the claim
 * is offered; with a set, a member whose root port's filter was restarted
 * and claims the same NHI again, alone, adopts the new claim's serial (the
 * member and its index unchanged), and a claim for a bridge not in the set
 * is logged once and taken at the next start. Not while the host interface
 * is lost or contained: the generation is left unseen until it is back, so
 * the recovery revalidates the set as it was.
 */
static VOID cmAcpiLate(PCM_NHI nhi)
{
    PCM_REENUM r;
    PLIST_ENTRY e;
    PCM_FILTER f;
    KIRQL irql;
    ULONG gen;
    ULONG late;
    ULONG renewed;
    ULONG i;
    ULONG in;

    r = &nhi->Reenum;
    late = XHCI_TBT_NO_BDF;
    renewed = XHCI_TBT_NO_BDF;
    if (r->Nhi == XHCI_TBT_NO_BDF || !cmTransport(nhi) ||
        CmNhiFailed(nhi)) {
        return;
    }
    KeAcquireSpinLock(&cmRegLock, &irql);
    gen = cmRegAcpiGen;
    if (gen == r->AcpiGenSeen) {
        KeReleaseSpinLock(&cmRegLock, irql);
        return;
    }
    r->AcpiGenSeen = gen;
    if (r->Listed) {
        for (i = 0; i < r->Members; i++) {
            if (r->Port[i].Source != XHCI_TBT_SRC_ACPI) {
                continue;
            }
            f = cmRegFind(r->Port[i].Bdf);
            if (f != NULL && f->AcpiClaim &&
                CmDsdRenewed(r->Port[i].AcpiSerial, f->AcpiSerial,
                             cmRegAcpiSole(f->Bdf, f->AcpiSerial,
                                           r->AcpiName))) {
                r->Port[i].AcpiSerial = f->AcpiSerial;
                renewed = f->Bdf;
            }
        }
        if (renewed != XHCI_TBT_NO_BDF) {
            cmRegRecompute();
        }
        for (e = cmRegFilters.Flink; e != &cmRegFilters; e = e->Flink) {
            f = CONTAINING_RECORD(e, CM_FILTER, RegLink);
            if (!f->Published || f->Gone || !f->AcpiClaim ||
                !CmDsdSameName(f->AcpiRef, r->AcpiName)) {
                continue;
            }
            in = 0;
            for (i = 0; i < r->Members; i++) {
                if (r->Port[i].Bdf == f->Bdf) {
                    in = 1;
                }
            }
            if (!in) {
                late = f->Bdf;
            }
        }
    }
    KeReleaseSpinLock(&cmRegLock, irql);
    if (!r->Listed && !r->LossSeen) {
        r->RediscoverLeft = CM_REDISCOVER_TRIES;
        r->RediscoverAt = cmNow(nhi) - CM_REDISCOVER_MS;
        CmLogNote(nhi, "cm.acpi.rediscover", gen);
    } else if (late != XHCI_TBT_NO_BDF && !r->AcpiLateLogged) {
        r->AcpiLateLogged = 1;
        CmLogNote(nhi, "cm.acpi.late", late);
    }
    if (renewed != XHCI_TBT_NO_BDF) {
        CmLogNote(nhi, "cm.acpi.renewed", renewed);
    }
}

VOID CmReenumService(PCM_NHI nhi)
{
    PCM_REENUM r;
    KIRQL irql;
    PCM_FILTER f;
    PCM_FILTER ejHeld[XHCI_TBT_MAX_TUNNELS];
    ULONG staleWhy[XHCI_TBT_MAX_TUNNELS];
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
    if (r->Route == CM_ROUTE_FILTER) {
        /* Record 21 section 6.3.1 (task 49.46). */
        cmWpuService(nhi);
    }
    /* Owed chain actions, tried again each pass (task 45.13). */
    cmChainService(nhi);
    /* The stor98 channel's per-pass work: on 98 SE and ME the rescan of a
     * bus a fresh exposure found silent; under the USB4 profile the
     * presence samples for the statement's anchor (record 22 section
     * 10.3). */
    if (CmLiveTransport(&nhi->Live)) {
        /* Not while the host interface is lost: its scans read the
         * bridges (record 21 section 5.5). */
        CmNvlinkService(nhi);
    }
    if (r->AcpiNameLen != 0) {
        cmAcpiLate(nhi);
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
        staleWhy[i] = 0;
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
            /* Which check refused it (task 43.70), for the log: bit 0
             * the bridge gone or another domain's, 1 a new publication,
             * 2 a new population generation, 3 a function with a stack;
             * bits 8 to 15 the bridge's generation now, 16 to 23 the
             * eject's. */
            if (f == NULL || f->Owner != nhi) {
                staleWhy[i] = CM_EJ_STALE_OWNER;
            } else {
                staleWhy[i] = CmPortsStaleWhy(
                    (ULONG)(f->Serial != r->Port[i].EjectSerial),
                    (ULONG)(f->RelGen != r->Port[i].EjectGen),
                    (ULONG)!cmEjectReady(f), f->RelGen,
                    r->Port[i].EjectGen);
            }
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
            CmLogNote(nhi, "cm.reenum.eject.stale.why", staleWhy[i]);
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
    if (r->Route == CM_ROUTE_FILTER && !(nhi->U4On && taken)) {
        /* The chained units' ejects (record 34 section 3.12 item 7),
         * after the members' own, one eject a pass. */
        cmKidEjects(nhi);
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
            /* Record 22 section 11.1.14: a successful resume's release
             * deferred behind the gate exposes first (cm_nvlink.h,
             * obligation 1), since the take's UP was refused while the
             * gate stood; an entry already UP keeps its generation.
             * Behind section 7.7's retirement barrier the exposure is
             * owed until the barrier lifts, as a held call is. */
            switch (CmPortsDeferredExpose(
                (act & CM_HELD_DO_FAIL) != 0,
                r->Route == CM_ROUTE_FILTER, cmTransport(nhi),
                (nhi->RecBarred & (1UL << i)) != 0)) {
            case CM_PORTS_DX_EXPOSE:
                (VOID)CmNvlinkExpose(nhi, r->Port[i].Bdf, TRUE,
                                     CM_NVLINK_TUNNEL_NONE);
                break;
            case CM_PORTS_DX_OWE:
                r->Port[i].Missed = 1;
                r->Port[i].MissedExpose |= 1UL;
                break;
            default:
                break;
            }
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
            mark = (BOOLEAN)((r->Port[i].Missed && !missed &&
                              r->SafeEject != 0) ||
                             r->Port[i].HeldMark != 0);
            r->Port[i].HeldMark = 0;
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
            r->Port[i].HeldMark = 0;
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
            if (cmVanishSweep(nhi, i, CM_EJ_SWEEP_NO_GEN, FALSE, 0)) {
                /* Task 43.47: its vanished children the sweep's; the owed
                 * call held behind its gate, exposure and marking window
                 * with it. */
                r->Port[i].Held |= CM_HELD_REENUM | CM_HELD_FRESH;
                r->Port[i].HeldExpose |= r->Port[i].MissedExpose;
                r->Port[i].HeldMark |= (r->SafeEject != 0) ? 1UL : 0UL;
                r->Port[i].Missed = 0;
                r->Port[i].MissedExpose = 0;
                CmLogNote(nhi, "cm.reenum.vanish-held", r->Port[i].Bdf);
                continue;
            }
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
    if (r->Wk != NULL && r->Callwk != NULL) {
        /* Task 43.47: the members whose call worker opened its route, the
         * ones a claim may be handed to. */
        mask = 0;
        for (i = 0; i < r->Members && i < XHCI_TBT_MAX_TUNNELS; i++) {
            if (CmCallwkUsable(r->Callwk, i)) {
                mask |= 1UL << i;
            }
        }
        if (mask != r->CallUsable) {
            r->CallUsable = mask;
            CmCfgwkTakeover(r->Wk, mask);
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
    ULONG i;
    BOOLEAN over;

    r = &nhi->Reenum;
    r->Suspends++;
    /* Every synchronous stage of the client's resume (cm_ports.h), with
     * every device the client's table can hold re-announced and approved
     * (a dock plugged during the sleep can add several): the bound logged
     * below, a diagnostic (no power IRP is retained, task 43.24h). */
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
    /* The client's own bound and the five-minute cap are logged as
     * diagnostics (cm_ports.h): no power IRP waits on them (task 43.24h,
     * record 21 section 6.3). */
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
    CmLogNote(nhi, "cm.reenum.hold.bound-ms", r->HoldBoundMs);
    if (r->Route == CM_ROUTE_CONFIGMG && r->Wk != NULL) {
        /* Record 21 section 6.1.3: the members whose bridge Windows 98 SE
         * and ME must restore before their next operation touches them. */
        CmCfgwkSuspend(r->Wk);
    }
    if (r->Route != CM_ROUTE_FILTER) {
        return;
    }
    KeAcquireSpinLock(&cmRegLock, &irql);
    for (i = 0; i < r->Members; i++) {
        f = cmRegFind(r->Port[i].Bdf);
        /* Record 22 section 11.1.14: a member that reported a PDO now
         * has its function waited for at the resume (latched here: the
         * snapshot moves with every answer while it is held). */
        r->Port[i].SettleWant = 0;
        if (f != NULL && f->Owner == nhi && f->Active) {
            f->Held = 1;
            f->HeldBy = nhi;
            r->Port[i].Flags |= CM_PORT_HELD;
            r->Port[i].SettleWant = (ULONG)(f->SnapCount != 0);
        }
    }
    KeReleaseSpinLock(&cmRegLock, irql);
}

/*
 * Record 22 section 11.1.14 (task 49.11): before a successful resume is
 * published, the function behind each member held at the suspend whose
 * bridge last reported a PDO is read until it answers again, all within
 * one CM_PORTS_SETTLE_MS from the first reading. A PCIe tunnel rebuilt by
 * the resume may bring its function back later than the tunnel itself,
 * and a BusRelations answer taken before then reports the bridge empty,
 * which PnP takes as a removal. The members are copied under the list's
 * lock; the reads and sleeps hold no lock. The bound passing changes
 * nothing but the log: the take, the release and the drain follow. The
 * thread, PASSIVE_LEVEL.
 */
/* The settle wait goes on only while the host interface is reachable and
 * no stop or exit is asked (the liveness probe after it judges a loss). */
static BOOLEAN cmSettleLive(PCM_NHI nhi)
{
    KIRQL irql;
    BOOLEAN live;

    if (!CmNhiHwAccess(nhi)) {
        return FALSE;
    }
    KeAcquireSpinLock(&nhi->WorkLock, &irql);
    live = (BOOLEAN)(!nhi->Live.Revoked && !nhi->PnpStopPending &&
                     (nhi->WorkBits & (CM_WORK_STOP | CM_WORK_EXIT)) == 0);
    KeReleaseSpinLock(&nhi->WorkLock, irql);
    return live;
}

VOID CmReenumResumeSettle(PCM_NHI nhi)
{
    PCM_REENUM r;
    PCM_FILTER f;
    KIRQL irql;
    ULONG bdf[XHCI_TBT_MAX_TUNNELS];
    ULONG last[XHCI_TBT_MAX_TUNNELS];
    ULONG n;
    ULONG i;
    ULONG shown;
    ULONG start;
    ULONG elapsed;
    ULONG wait;

    r = &nhi->Reenum;
    if (r->Route != CM_ROUTE_FILTER) {
        return;
    }
    n = 0;
    KeAcquireSpinLock(&cmRegLock, &irql);
    for (i = 0; i < r->Members && i < XHCI_TBT_MAX_TUNNELS; i++) {
        f = cmRegFind(r->Port[i].Bdf);
        if (r->Port[i].SettleWant && f != NULL && f->Held &&
            f->HeldBy == nhi) {
            bdf[n] = r->Port[i].Bdf;
            last[n] = XHCI_TBT_FN_UNREAD;
            n++;
        }
        r->Port[i].SettleWant = 0;
    }
    KeReleaseSpinLock(&cmRegLock, irql);
    if (n == 0) {
        return;
    }
    start = cmNow(nhi);
    for (;;) {
        shown = 0;
        for (i = 0; i < n; i++) {
            if (last[i] != XHCI_TBT_FN_PRESENT) {
                last[i] = XhciTbtFunctionRead(&r->Cfg, bdf[i]);
            }
            if (last[i] == XHCI_TBT_FN_PRESENT) {
                shown++;
            }
        }
        elapsed = cmNow(nhi) - start;
        if (!CmPortsSettleAgain(shown, n, elapsed, CM_PORTS_SETTLE_MS) ||
            !cmSettleLive(nhi)) {
            break;
        }
        CmNhiWaitService(nhi);
        wait = CM_PORTS_SETTLE_MS - elapsed;
        cmSleep(nhi, wait < XHCI_TBT_POLL_STEP_MS ? wait
                                                  : XHCI_TBT_POLL_STEP_MS);
    }
    CmLogNote(nhi, "cm.reenum.resume.settle",
              ((n & 0xFFUL) << 24) | ((shown & 0xFFUL) << 16) |
                  (elapsed > 0xFFFFUL ? 0xFFFFUL : elapsed));
    for (i = 0; i < n; i++) {
        if (last[i] != XHCI_TBT_FN_PRESENT) {
            CmLogNote(nhi, "cm.reenum.resume.settle.late",
                      ((bdf[i] & 0xFFFFUL) << 8) | (last[i] & 0xFFUL));
        }
    }
}

/*
 * Every member in `mask` this domain marked released, and each one that
 * was still marked invalidated, whatever the resume's outcome (`fail`
 * names it in the log only): its D0 went down at once (task 43.24h), so
 * the devices behind it may have resumed against an absent tunnel, and
 * the bridge's relations are asked for again now the tunnels are back
 * (record 21 section 6.3). Spin locks and IoInvalidateDeviceRelations
 * only: callable at <= DISPATCH_LEVEL (CmReenumResumeFailed's use).
 */
static VOID cmReleaseHeld(PCM_NHI nhi, BOOLEAN fail, ULONG mask)
{
    PCM_REENUM r;
    KIRQL irql;
    PCM_FILTER f;
    ULONG i;
    ULONG was;
    ULONG act;

    r = &nhi->Reenum;
    if (r->Route != CM_ROUTE_FILTER) {
        return;
    }
    for (i = 0; i < r->Members; i++) {
        if ((mask & (1UL << i)) == 0) {
            continue;
        }
        KeAcquireSpinLock(&cmRegLock, &irql);
        r->Port[i].Flags &= ~CM_PORT_HELD;
        f = cmRegFind(r->Port[i].Bdf);
        was = (ULONG)(f != NULL && f->HeldBy == nhi);
        act = CmPortsRelease(was, r->Reenum);
        if (act & CM_PORTS_REL_CLEAR) {
            f->Held = 0;
            f->HeldBy = NULL;
        }
        KeReleaseSpinLock(&cmRegLock, irql);
        if (act & CM_PORTS_REL_CLEAR) {
            CmLogNote(nhi, fail ? "cm.reenum.hold.released-failed"
                                : "cm.reenum.hold.released",
                      r->Port[i].Bdf);
        }
        /* Record 21 section 6.3.1: the release a wake power-up waits for
         * (none at a stop, whose domain is closed first). */
        cmWpuPortReleased(nhi, i);
        if (act & CM_PORTS_REL_INVALIDATE) {
            (VOID)cmInvalidate(nhi, i, FALSE);
        }
    }
}

/* Record 21 section 6.1.3: every end of the NHI's resume ends the CONFIGMG
 * worker's sleep, so the operations it parked are taken again and wait
 * for Windows' restore of their bridges. <= DISPATCH_LEVEL. */
static VOID cmResumeWk(PCM_NHI nhi)
{
    if (nhi->Reenum.Route == CM_ROUTE_CONFIGMG && nhi->Reenum.Wk != NULL) {
        CmCfgwkResumed(nhi->Reenum.Wk);
    }
}

VOID CmReenumResumeFailed(PCM_NHI nhi)
{
    cmResumeWk(nhi);
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
    cmResumeWk(nhi);
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
    PLIST_ENTRY e;
    PCM_FILTER f;
    KIRQL irql;
    ULONG attached;
    ULONG over;
    ULONG skipped;
    ULONG refused;
    ULONG passed;
    ULONG i;

    r = &nhi->Reenum;
    /* Record 21 section 6.3.1: no ask admitted from here on; one being
     * sent (from another thread than this one) waited for, bounded. */
    KeAcquireSpinLock(&cmRegLock, &irql);
    r->WpuClosed = 1;
    passed = r->WpuSending;
    KeReleaseSpinLock(&cmRegLock, irql);
    if (passed) {
        (VOID)CmWaitEventMs(&r->WpuSendDone, 2000UL);
    }

    if (r->WpuArmed != 0 || r->WpuAsked != 0) {
        CmLogNote(nhi, "cm.wakepowerup.counts",
                  ((r->WpuArmed & 0xFFUL) << 24) | ((r->WpuAsked & 0xFFUL) << 16) |
                      ((r->WpuOk & 0xFFUL) << 8) | ((r->WpuFailed & 0xFUL) << 4) |
                      (r->WpuNone & 0xFUL));
    }
    CmNvlinkStop(nhi);
    r->RediscoverLeft = 0;
    r->AcpiNameLen = 0;
    if (!r->Listed && r->Route == CM_ROUTE_NONE) {
        r->Members = 0;
        return;
    }
    r->Stops++;
    cmReleaseHeld(nhi, TRUE, 0xFFFFFFFFUL);
    cmChainDropAll(nhi, "cm.chain.cancelled.stop");
    KeAcquireSpinLock(&cmRegLock, &irql);
    if (r->Listed) {
        RemoveEntryList(&r->Link);
        r->Listed = 0;
    }
    /* Task 45.35's bindings to this domain voided with its listing: a
     * domain is named by its object's address and its starts count from
     * 0 again, so a later domain at the same address must never be taken
     * for it; an ask of a voided binding finds no domain and answers no
     * until its holder lets go. */
    for (e = cmRegAtts.Flink; e != &cmRegAtts; e = e->Flink) {
        f = CONTAINING_RECORD(e, CM_FILTER, AttLink);
        if (f->IsoDom == (PVOID)nhi) {
            f->IsoDom = NULL;
        }
    }
    r->EjectPending = 0;
    CmZero(r->KidEject, sizeof(r->KidEject));
    CmZero(r->ChainLand, sizeof(r->ChainLand));
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
        CmLogNote(nhi, "cm.reenum.9x.noirq.held",
                  (wc.NoIrqCredited << 16) | (wc.NoIrqHeld & 0xFFFFUL));
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
    r->Deferred = 0;
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
    CmLogNote(nhi, "cm.chain.ports", r->ChainPorts);
    CmLogNote(nhi, "cm.chain.invalidate", r->ChainInvalidate);
    CmLogNote(nhi, "cm.chain.nofilter", r->ChainNoFilter);
    CmLogNote(nhi, "cm.chain.owed", r->ChainOwedCount);
    CmLogNote(nhi, "cm.chain.owed-expired", r->ChainOwedExpired);
    CmLogNote(nhi, "cm.chain.owed-full", r->ChainOwedFull);
    CmLogNote(nhi, "cm.chain.incomplete", r->ChainIncomplete);
    CmLogNote(nhi, "cm.chain.port-unpowered", r->ChainUnpowered);
    CmLogNote(nhi, "cm.chain.over", r->ChainOver);
    CmLogNote(nhi, "cm.chain.cancelled", r->ChainCancelled);
    CmRegCounters(&attached, &over, &skipped, &refused, &passed);
    CmLogNote(nhi, "cm.reenum.child.attached", attached);
    CmLogNote(nhi, "cm.reenum.child.over", over);
    CmLogNote(nhi, "cm.reenum.child.skipped", skipped);
    CmLogNote(nhi, "cm.reenum.child.refused", refused);
    CmLogNote(nhi, "cm.reenum.hold.passed", passed);
    CmLogNote(nhi, "cm.reenum.child.stray", cmRegStray);
    CmLogNote(nhi, "cm.reenum.child.pretrust", cmRegPreTrust);
    CmRegIdCounters(&attached, &over, &skipped, &refused, &passed);
    CmLogNote(nhi, "cm.reenum.id.given", attached);
    CmLogNote(nhi, "cm.reenum.id.nomem", over);
    CmLogNote(nhi, "cm.reenum.id.over", skipped);
    CmLogNote(nhi, "cm.reenum.chain.deep", refused);
    CmLogNote(nhi, "cm.reenum.child.reattached", passed);
    for (i = 0; i < CM_BQC_COUNT; i++) {
        CmLogNote(nhi, cmBqNames[i], cmBqCount[i]);
    }
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
    cmChainDropAll(nhi, "cm.chain.cancelled.loss");
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
    BOOLEAN defer;

    r = &nhi->Reenum;
    r->LossDrain = 0;
    CmLogNote(nhi, "cm.reenum.unavailable", episode);
    if (r->Route != CM_ROUTE_CONFIGMG || r->Wk == NULL || r->Reenum == 0) {
        return;
    }
    r->WkActive = cmOwnedMask(nhi);
    /* Record 21 section 6.1.2: under the USB4 profile, whose loss has a
     * recovery, the removal waits until the commit proves the path
     * behind the member closed. */
    defer = (BOOLEAN)(nhi->U4On != 0);
    CmCfgwkUnavailable(r->Wk, r->WkActive, defer);
    r->Unavailable++;
    CmLogNote(nhi, "cm.reenum.9x.unavailable.members", r->WkActive);
    if (defer) {
        r->Deferred |= r->WkActive;
        CmLogNote(nhi, "cm.reenum.9x.unavailable.deferred", r->WkActive);
    } else {
        r->Deferred = 0;
    }
}

VOID CmReenumLossReturned(PCM_NHI nhi, ULONG members)
{
    PCM_REENUM r;

    r = &nhi->Reenum;
    members &= r->Deferred;
    if (r->Route != CM_ROUTE_CONFIGMG || r->Wk == NULL || members == 0) {
        return;
    }
    CmCfgwkRelease(r->Wk, members);
    r->Deferred &= ~members;
    CmLogNote(nhi, "cm.reenum.9x.unavailable.released",
              (r->Deferred << 16) | (members & 0xFFFFUL));
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
        CmCfgwkHold(r->Wk, CmNhiU4WorkerHold(nhi));
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

BOOLEAN CmReenumHold(PCM_NHI nhi, ULONG members)
{
    if (nhi->Reenum.Route == CM_ROUTE_CONFIGMG && nhi->Reenum.Wk != NULL) {
        CmCfgwkHold(nhi->Reenum.Wk, members);
        return TRUE;
    }
    return FALSE;
}

/* TRUE when the member at `bdf`, ACPI-named, still has its claim: the
 * filter published, its claim made under the member's AcpiSerial and
 * naming this NHI. Takes the list's lock. */
static BOOLEAN cmAcpiHolds(PCM_NHI nhi, ULONG bdf)
{
    PCM_REENUM r;
    PCM_FILTER f;
    KIRQL irql;
    BOOLEAN ok;
    ULONG i;

    r = &nhi->Reenum;
    ok = FALSE;
    KeAcquireSpinLock(&cmRegLock, &irql);
    for (i = 0; i < r->Members; i++) {
        if (r->Port[i].Bdf != bdf) {
            continue;
        }
        if (cmRegAcpiSole(bdf, r->Port[i].AcpiSerial, r->AcpiName)) {
            ok = TRUE;
        }
    }
    KeReleaseSpinLock(&cmRegLock, irql);
    return ok;
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
        if (r->Topo.Source[i] == XHCI_TBT_SRC_ACPI) {
            /* Excepted only while its filter's claim, under the publication
             * it was added from, still names this NHI (record 22 section
             * 7.6.1). */
            if (!cmAcpiHolds(nhi, r->Topo.Set[i])) {
                CmLogNote(nhi, "u4.recover.member.source-lost",
                          (r->Topo.Set[i] << 8) | r->Topo.Source[i]);
                return FALSE;
            }
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

/* ======================================================================
 * The wake power-up (design record 21 section 6.3.1, task 49.46). After a
 * standby or hibernation, a drive Stor98 runs behind a tunnel can wake
 * before its tunnel is back: the PCI driver fails its D0, and the
 * resume's re-enumeration powers the adapter up again but nothing asks
 * its logical units for D0 again, so they stay in D3 with their requests
 * held and Plug and Play waits on them for ever. Once its port is
 * released by the resume, the thread asks each such unit for D0 once
 * (PoRequestPowerIrp), only where the member's own wake D0 was seen to
 * fail by its child filter and only for a drive Stor98 runs. The units
 * are found by public means alone: the member's BusRelations answer read
 * by the child filter (route R1, cmUnitRelations in cm_filt.c). A second
 * route, the disks' interface arrivals matched to the member by their
 * DEVPKEY_Device_Parent property on Vista and later (route R2, on a system
 * work item), is not built in this release (CM_WPU_R2_BUILT). With
 * XhciCmWakePowerUp 0 nothing of this runs but the cleanup of a request
 * already outstanding. Locks: cmRegLock, then a filter's Lock, then
 * cmWpuQLock. IRQL: each function says.
 * ====================================================================== */

XHCI_C_ASSERT(cm_wpu_units, CM_WPU_UNITS == 4UL);

#define CM_WPU_CAND         32UL    /* filters looked at per pass (more are
                                       logged as over)                       */
#define CM_WPU_PASS_MS      500UL   /* the thread's step at most this often */
#define CM_WPU_LINKS        32UL
#define CM_WPU_LINK_CHARS   256UL
#define CM_WPU_QUEUE        8UL
#define CM_WPU_PROP_CHARS   260UL

/* Route R2 (record 21 section 6.3.1): 0, none of its code is compiled in
 * any flavour - no disk notification registered, no work item queued, no
 * disk opened. */
#define CM_WPU_R2_BUILT     0

#define CM_WPU_SEND_WAIT_MS 2000UL  /* a removal's wait for a request    */
#define CM_WPU_STUCK_MS     60000UL /* a work item's lookup out longer    */

/* The system's gate (cm_ports.h CM_WPU_SYS): the epoch, system sleeps
 * begun, 0 none yet, advanced once per sleep at the first system
 * SET_POWER below S0 any NHI or filter of this driver receives (Claim,
 * cleared by the next S0); Halt, set at the dispatch of any sleep query,
 * sleep or shutdown, cleared by a domain's accepted release; DownSeq, the
 * device power-downs. Each domain's own part is in its CM_REENUM. The
 * closing hooks, the release and every send check change or read both
 * under cmWpuGateLock (a zeroed static spin lock, an initialised one;
 * taken last, never held across a call out). */
static CM_WPU_SYS cmWpuSys;
static KSPIN_LOCK cmWpuGateLock;
/* NHIs started whose XhciCmWakePowerUp is 1; NHIs added whose value is
 * not read yet; whether any NHI was ever added (CmWpuAdded, CmWpuCount). */
static LONG cmWpuOnCount;
static LONG cmWpuPending;
static LONG cmWpuAnyAdded;
#if CM_WPU_R2_BUILT
/* Quiet: see route R2 below; cleared at once by every closing hook. */
static volatile ULONG cmWpuQuiet;
static ULONG cmWpuLastRelTick;      /* the last release, by the kernel's
                                       tick count (one clock for every NHI) */
static ULONG cmWpuReleased;
#endif
static ULONG cmWpuSendTimeouts;     /* bounded removal waits that expired */
static ULONG cmWpuLoggedTimeouts;

/* The requests in flight, driver-wide static data, so a late callback
 * writes valid memory after any NHI's removal; the image is resident from
 * the first request on (DriverUnload cleared). Busy is the thread's; the
 * callback writes Status, then Done. */
typedef struct _CM_WPU_SLOT {
    ULONG Busy;
    LONG Done;
    volatile ULONG Status;
    ULONG Bdf;
    ULONG StartAt;          /* NowMs at the request                        */
    ULONG Overdue;          /* logged as overdue                           */
    PDEVICE_OBJECT Unit;    /* the request's own reference                */
} CM_WPU_SLOT;

/* Every NHI has its own thread: the slots are reserved, read and freed
 * under cmWpuSlotLock (a zeroed static spin lock, an initialised one),
 * never held across PoRequestPowerIrp; the callback takes no lock. */
static CM_WPU_SLOT cmWpuSlot[CM_WPU_SLOTS];
static KSPIN_LOCK cmWpuSlotLock;
static LONG cmWpuR2Claim;           /* the one thread that starts route R2 */
static ULONG cmWpuOver;             /* passes that found more than CAND    */
static ULONG cmWpuLoggedOver;

ULONG CmWpuEpochNow(VOID)
{
    return *(volatile ULONG *)&cmWpuSys.Epoch;
}

BOOLEAN CmWpuOn(VOID)
{
    return (BOOLEAN)(*(volatile LONG *)&cmWpuAnyAdded == 0 ||
                     *(volatile LONG *)&cmWpuPending > 0 ||
                     *(volatile LONG *)&cmWpuOnCount > 0);
}

/* The NHI's AddDevice. PASSIVE_LEVEL. */
VOID CmWpuAdded(PCM_NHI nhi)
{
    if (!nhi->Reenum.WpuPendCounted) {
        nhi->Reenum.WpuPendCounted = 1;
        (VOID)InterlockedIncrement(&cmWpuPending);
        /* An aligned LONG store (no InterlockedExchange: an x86 import
         * the gate does not admit); it only ever goes from 0 to 1. */
        *(volatile LONG *)&cmWpuAnyAdded = 1;
    }
}

/* The NHI's PnP dispatch: `on` at its start once the values are read
 * (counted when the value is 1), else at its stop, surprise removal or
 * removal. The count is raised before the pending one is lowered, so a
 * start with the value 1 never turns the filters' half off in between.
 * PASSIVE_LEVEL. */
VOID CmWpuCount(PCM_NHI nhi, BOOLEAN on)
{
    PCM_REENUM r;

    r = &nhi->Reenum;
    if (on && nhi->Values.WakePowerUp != 0 && !r->WpuOnCounted) {
        r->WpuOnCounted = 1;
        (VOID)InterlockedIncrement(&cmWpuOnCount);
    } else if (!on && r->WpuOnCounted) {
        r->WpuOnCounted = 0;
        (VOID)InterlockedDecrement(&cmWpuOnCount);
    }
    if (r->WpuPendCounted) {
        /* Read at this start, or gone without one. */
        r->WpuPendCounted = 0;
        (VOID)InterlockedDecrement(&cmWpuPending);
    }
}

/* A domain's gate copied out of and back into its CM_REENUM; under the
 * gate lock. */
static VOID cmWpuDomLoad(const CM_REENUM *r, CM_WPU_DOM *d)
{
    d->StopLatch = r->WpuStopLatch;
    d->PowerDown = r->WpuPowerDown;
    d->ResumeEpoch = r->WpuResumeEpoch;
    d->ResumeHaltSeq = r->WpuResumeHaltSeq;
    d->Resuming = r->WpuResuming;
    d->RelDownSeq = r->WpuRelDownSeq;
    d->RelHaltSeq = r->WpuRelHaltSeq;
}

static VOID cmWpuDomStore(CM_REENUM *r, const CM_WPU_DOM *d)
{
    r->WpuStopLatch = d->StopLatch;
    r->WpuPowerDown = d->PowerDown;
    r->WpuResumeEpoch = d->ResumeEpoch;
    r->WpuResumeHaltSeq = d->ResumeHaltSeq;
    r->WpuResuming = d->Resuming;
    r->WpuRelDownSeq = d->RelDownSeq;
    r->WpuRelHaltSeq = d->RelHaltSeq;
}

#define CM_WPU_DOM_STOP     1UL
#define CM_WPU_DOM_CANCEL   2UL
#define CM_WPU_DOM_START    3UL
#define CM_WPU_DOM_DOWN     4UL
#define CM_WPU_DOM_RESUME   5UL

/* IRQL: <= DISPATCH_LEVEL. */
static VOID cmWpuDomEvent(PCM_NHI nhi, ULONG what)
{
    CM_WPU_DOM d;
    KIRQL irql;

    KeAcquireSpinLock(&cmWpuGateLock, &irql);
    cmWpuDomLoad(&nhi->Reenum, &d);
    switch (what) {
    case CM_WPU_DOM_STOP:
        CmPortsWpuDomStop(&d);
        break;
    case CM_WPU_DOM_CANCEL:
        CmPortsWpuDomCancel(&d);
        break;
    case CM_WPU_DOM_START:
        CmPortsWpuDomStart(&d);
        break;
    case CM_WPU_DOM_DOWN:
        CmPortsWpuDomPowerDown(&d);
        break;
    default:
        CmPortsWpuDomResumeBegin(&cmWpuSys, &d);
        break;
    }
    cmWpuDomStore(&nhi->Reenum, &d);
#if CM_WPU_R2_BUILT
    if (what != CM_WPU_DOM_CANCEL && what != CM_WPU_DOM_START) {
        cmWpuQuiet = 0;
    }
#endif
    KeReleaseSpinLock(&cmWpuGateLock, irql);
}

VOID CmWpuDomStop(PCM_NHI nhi)
{
    cmWpuDomEvent(nhi, CM_WPU_DOM_STOP);
}

VOID CmWpuDomCancel(PCM_NHI nhi)
{
    cmWpuDomEvent(nhi, CM_WPU_DOM_CANCEL);
}

VOID CmWpuDomStart(PCM_NHI nhi)
{
    cmWpuDomEvent(nhi, CM_WPU_DOM_START);
}

VOID CmWpuResumeBegin(PCM_NHI nhi)
{
    cmWpuDomEvent(nhi, CM_WPU_DOM_RESUME);
}

/* IRQL: <= DISPATCH_LEVEL (a power dispatch). */
VOID CmWpuPowerSeen(UCHAR minor, ULONG type, ULONG state)
{
    KIRQL irql;

    if (minor != IRP_MN_SET_POWER && minor != IRP_MN_QUERY_POWER) {
        return;
    }
    KeAcquireSpinLock(&cmWpuGateLock, &irql);
    if (type == (ULONG)SystemPowerState) {
        CmPortsWpuSysPower(&cmWpuSys, (ULONG)(minor == IRP_MN_SET_POWER),
                           (ULONG)(state != (ULONG)PowerSystemWorking));
#if CM_WPU_R2_BUILT
        if (state != (ULONG)PowerSystemWorking) {
            cmWpuQuiet = 0;
        }
#endif
    } else if (type == (ULONG)DevicePowerState && minor == IRP_MN_SET_POWER &&
               state != (ULONG)PowerDeviceD0) {
        CmPortsWpuSysDown(&cmWpuSys);
#if CM_WPU_R2_BUILT
        cmWpuQuiet = 0;
#endif
    }
    KeReleaseSpinLock(&cmWpuGateLock, irql);
}

/* An NHI's: the same, and its own device power-down latched in its
 * domain until its own release. IRQL: <= DISPATCH_LEVEL. */
VOID CmWpuNhiPowerSeen(PCM_NHI nhi, UCHAR minor, ULONG type, ULONG state)
{
    CmWpuPowerSeen(minor, type, state);
    if (type == (ULONG)DevicePowerState && minor == IRP_MN_SET_POWER &&
        state != (ULONG)PowerDeviceD0) {
        cmWpuDomEvent(nhi, CM_WPU_DOM_DOWN);
    }
}

#if CM_WPU_R2_BUILT
static ULONG cmWpuTick(VOID);
static ULONG cmWpuWindowTicks(VOID);

/* Quiet, computed and published under the gate lock by whoever asks (the
 * work item before each lookup, the thread for its hand-back): no sleep,
 * shutdown, power-down or stop received since the last release, and that
 * release (if any) older than the window, by the kernel's one tick count.
 * <= DISPATCH_LEVEL. */
static BOOLEAN cmWpuQuietNow(VOID)
{
    KIRQL irql;
    BOOLEAN quiet;
    ULONG window;

    window = cmWpuWindowTicks();
    KeAcquireSpinLock(&cmWpuGateLock, &irql);
    quiet = (BOOLEAN)(cmWpuSys.Halt == 0 &&
                      (!cmWpuReleased ||
                       cmWpuTick() - cmWpuLastRelTick > window));
    cmWpuQuiet = quiet ? 1UL : 0UL;
    KeReleaseSpinLock(&cmWpuGateLock, irql);
    return quiet;
}
#endif

/* TRUE while, since the domain's accepted release in `epoch`, no sleep
 * query, sleep, shutdown or device power-down was received and the domain
 * is not latched by a stop or query (CmPortsWpuDomOpen): read under the
 * gate lock, at the admission and again immediately before each request.
 * A closing dispatch that comes between that last check and the call
 * meets a request already handed to the power manager, which orders it
 * with its own IRPs for the device; nothing narrower is possible without
 * holding a lock across the call. <= DISPATCH_LEVEL. */
static BOOLEAN cmWpuOpen(PCM_NHI nhi, ULONG epoch)
{
    CM_WPU_DOM d;
    KIRQL irql;
    BOOLEAN open;

    KeAcquireSpinLock(&cmWpuGateLock, &irql);
    cmWpuDomLoad(&nhi->Reenum, &d);
    open = (BOOLEAN)(CmPortsWpuDomOpen(&cmWpuSys, &d, epoch) != 0);
    KeReleaseSpinLock(&cmWpuGateLock, irql);
    return open;
}

/* The epoch in bits 31:8, the start count's low byte in 7:0. */
PVOID CmWpuD0Context(PCM_FILTER child)
{
    return (PVOID)(ULONG_PTR)((CmWpuEpochNow() << 8) |
                              (child->WpuStartGen & 0xFFUL));
}

/* ---- the child filter's half (cm_filt.c) ---- */

#if CM_WPU_R2_BUILT
static VOID cmWpuKick(ULONG rescan);
#endif

VOID CmWpuChildInit(PCM_FILTER child)
{
    KeInitializeEvent(&child->WpuSendDone, NotificationEvent, TRUE);
}

/* IRQL: <= DISPATCH_LEVEL (the D0's completion). `context` is
 * CmWpuD0Context's at the dispatch: a completion of an earlier start or
 * epoch arms nothing. Only STATUS_NO_SUCH_DEVICE arms (the status read on
 * every parked wake); any other failure is kept for the log alone. */
VOID CmWpuD0Seen(PCM_FILTER child, ULONG context, NTSTATUS status)
{
    KIRQL irql;
    ULONG epoch;

    if (!CmWpuOn()) {
        return;
    }
    epoch = context >> 8;
    KeAcquireSpinLock(&child->Lock, &irql);
    if ((context & 0xFFUL) == (child->WpuStartGen & 0xFFUL) &&
        epoch == CmWpuEpochNow()) {
        if (CmPortsWpuArm(epoch, CmWpuEpochNow(), child->WpuConsumed,
                          (ULONG)(status == STATUS_NO_SUCH_DEVICE)) &&
            child->WpuFailEpoch != epoch) {
            child->WpuFailEpoch = epoch;
            child->WpuFailStatus = (ULONG)status;
        } else if (!NT_SUCCESS(status) && status != STATUS_NO_SUCH_DEVICE &&
                   child->WpuOtherEpoch != epoch) {
            child->WpuOtherEpoch = epoch;
            child->WpuOtherStatus = (ULONG)status;
        }
    }
    KeReleaseSpinLock(&child->Lock, irql);
}

static VOID cmWpuDeref(PDEVICE_OBJECT *list, ULONG n)
{
    ULONG i;

    for (i = 0; i < n; i++) {
        if (list[i] != NULL) {
            ObDereferenceObject(list[i]);
        }
    }
}

/* An answer on its way back: the units of disk type it carries, at most
 * CM_WPU_UNITS, referenced; the earlier list dropped. PASSIVE_LEVEL. */
VOID CmWpuRelations(PCM_FILTER child, const DEVICE_RELATIONS *rel)
{
    PDEVICE_OBJECT add[CM_WPU_UNITS];
    PDEVICE_OBJECT old[CM_WPU_UNITS];
    PDEVICE_OBJECT d;
    KIRQL irql;
    ULONG n;
    ULONG nOld;
    ULONG i;

    if (!CmWpuOn()) {
        /* Off: no discovery; a list an earlier start kept goes at the
         * next start or removal. */
        return;
    }
    n = 0;
    for (i = 0; rel != NULL && i < rel->Count && n < CM_WPU_UNITS; i++) {
        d = rel->Objects[i];
        if (d != NULL && (d->DeviceType == FILE_DEVICE_DISK ||
                          d->DeviceType == FILE_DEVICE_MASS_STORAGE)) {
            ObReferenceObject(d);
            add[n++] = d;
        }
    }
    KeAcquireSpinLock(&child->Lock, &irql);
    nOld = child->WpuRelCount;
    for (i = 0; i < nOld; i++) {
        old[i] = child->WpuRel[i];
    }
    for (i = 0; i < n; i++) {
        child->WpuRel[i] = add[i];
    }
    child->WpuRelCount = n;
    child->WpuRelAnswers++;
    KeReleaseSpinLock(&child->Lock, irql);
    cmWpuDeref(old, nOld);
}

/* Takes both lists out of `child`; under its lock. */
static ULONG cmWpuTakeLists(PCM_FILTER child, PDEVICE_OBJECT *out)
{
    ULONG n;
    ULONG i;

    n = 0;
    for (i = 0; i < child->WpuRelCount; i++) {
        out[n++] = child->WpuRel[i];
        child->WpuRel[i] = NULL;
    }
    for (i = 0; i < child->WpuDiskCount; i++) {
        out[n++] = child->WpuDisk[i];
        child->WpuDisk[i] = NULL;
    }
    child->WpuRelCount = 0;
    child->WpuDiskCount = 0;
    return n;
}

/* A successful start below the child: reopened, its units read afresh,
 * its arm and owner forgotten; the owner is read by the thread
 * (cmWpuOwnerRead), only while the recovery is on, so a value of 0 reads
 * nothing. PASSIVE_LEVEL. */
VOID CmWpuStarted(PCM_FILTER child)
{
    PDEVICE_OBJECT old[2 * CM_WPU_UNITS];
    KIRQL irql;
    ULONG n;

    KeAcquireSpinLock(&child->Lock, &irql);
    n = cmWpuTakeLists(child, old);
    child->WpuClosed = 0;
    child->WpuFailEpoch = 0;
    child->WpuStartGen++;
    child->WpuRelAnswers = 0;
    child->WpuRelLogged = 0;
    child->WpuOwner = CM_WPU_OWN_UNKNOWN;
    child->WpuOwnerOwed = 1;
    child->WpuOwnerWhy = 0;
    KeReleaseSpinLock(&child->Lock, irql);
    cmWpuDeref(old, n);
#if CM_WPU_R2_BUILT
    /* Route R2: the present disks matched again, and the owner read. */
    cmWpuKick(1);
#endif
}

/* Before a removal, stop or query-remove is forwarded: no request from
 * here on; one being sent is waited for before the units go. PASSIVE_LEVEL. */
VOID CmWpuClose(PCM_FILTER child, BOOLEAN drop)
{
    PDEVICE_OBJECT old[2 * CM_WPU_UNITS];
    KIRQL irql;
    ULONG n;
    ULONG sending;

    n = 0;
    KeAcquireSpinLock(&child->Lock, &irql);
    child->WpuClosed = 1;
    sending = child->WpuSending;
    if (drop) {
        n = cmWpuTakeLists(child, old);
    }
    KeReleaseSpinLock(&child->Lock, irql);
    if (sending && !CmWaitEventMs(&child->WpuSendDone, CM_WPU_SEND_WAIT_MS)) {
        /* PoRequestPowerIrp has no documented bound: the removal goes on
         * after a bounded wait, and the request then meets the removal in
         * the storage stack, which handles power IRPs during one. The
         * references stay valid: the thread holds its own. */
        cmWpuSendTimeouts++;
    }
    cmWpuDeref(old, n);
}

/* A cancelled query-remove, once the drivers below cancelled it: open
 * again with the units it kept. PASSIVE_LEVEL. */
VOID CmWpuReopen(PCM_FILTER child)
{
    KIRQL irql;

    KeAcquireSpinLock(&child->Lock, &irql);
    child->WpuClosed = 0;
    KeReleaseSpinLock(&child->Lock, irql);
}

/* ---- counted strings ---- */

static WCHAR cmWpuUp(WCHAR c)
{
    return (WCHAR)((c >= 'a' && c <= 'z') ? c - ('a' - 'A') : c);
}

/* Two counted strings equal but for ASCII case. */
static BOOLEAN cmWpuEqi(const WCHAR *a, ULONG aChars, const WCHAR *b,
                        ULONG bChars)
{
    ULONG i;

    if (aChars != bChars) {
        return FALSE;
    }
    for (i = 0; i < aChars; i++) {
        if (cmWpuUp(a[i]) != cmWpuUp(b[i])) {
            return FALSE;
        }
    }
    return TRUE;
}

static ULONG cmWpuChars(const WCHAR *s, ULONG room)
{
    ULONG n;

    n = 0;
    while (n < room && s[n] != 0) {
        n++;
    }
    return n;
}

/* ---- route R2: the disks' arrivals (Vista and later; not built) ---- */

#if CM_WPU_R2_BUILT

typedef struct _CM_WPU_PROPKEY {
    GUID Fmtid;
    ULONG Pid;
} CM_WPU_PROPKEY;

typedef NTSTATUS (NTAPI *CM_WPU_GETPROP)(PDEVICE_OBJECT pdo,
                                         const CM_WPU_PROPKEY *key,
                                         ULONG lcid, ULONG flags, ULONG size,
                                         PVOID data, PULONG required,
                                         PULONG type);

/* The value of WDK 7.1's devpkey.h. */
static const CM_WPU_PROPKEY cmWpuKeyService = { { 0xa45c254eUL, 0xdf1c,
    0x4efd, { 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0 } }, 6 };

static const WCHAR cmWpuStor98[] = { 's', 't', 'o', 'r', '9', '8', 0 };

static CM_WPU_GETPROP cmWpuGetProp;

/* One string property, terminated; its length in characters, or
 * 0xFFFFFFFF when it was not read. PASSIVE_LEVEL, no lock held. */
static ULONG cmWpuProp(PDEVICE_OBJECT pdo, const CM_WPU_PROPKEY *key,
                       WCHAR *buf)
{
    ULONG req;
    ULONG type;
    ULONG chars;

    req = 0;
    type = 0;
    buf[0] = 0;
    if (!NT_SUCCESS(cmWpuGetProp(pdo, key, 0, 0,
                                 (CM_WPU_PROP_CHARS - 1) * sizeof(WCHAR), buf,
                                 &req, &type))) {
        return 0xFFFFFFFFUL;
    }
    /* A string (DEVPROP_TYPE_STRING, 0x12) of whole characters that fits
     * and is terminated within what was written. */
    if (type != 0x12UL || (req & 1UL) != 0 || req < sizeof(WCHAR) ||
        req > (CM_WPU_PROP_CHARS - 1) * sizeof(WCHAR)) {
        return 0xFFFFFFFFUL;
    }
    buf[CM_WPU_PROP_CHARS - 1] = 0;
    chars = cmWpuChars(buf, req / sizeof(WCHAR));
    if (chars >= req / sizeof(WCHAR)) {
        return 0xFFFFFFFFUL;
    }
    return chars;
}


typedef NTSTATUS (NTAPI *CM_WPU_REGPNP)(IO_NOTIFICATION_EVENT_CATEGORY cat,
                                        ULONG flags, PVOID data,
                                        PDRIVER_OBJECT driver,
                                        PDRIVER_NOTIFICATION_CALLBACK_ROUTINE cb,
                                        PVOID context, PVOID *entry);
typedef PDEVICE_OBJECT (NTAPI *CM_WPU_BASEREF)(PDEVICE_OBJECT device);
typedef NTSTATUS (NTAPI *CM_WPU_IFACES)(const GUID *guid, PDEVICE_OBJECT pdo,
                                        ULONG flags, PWSTR *list);
typedef NTSTATUS (NTAPI *CM_WPU_OPEN)(PUNICODE_STRING name, ACCESS_MASK access,
                                      PFILE_OBJECT *file,
                                      PDEVICE_OBJECT *device);

/* The values of WDK 7.1's devpkey.h, ntddstor.h and wdmguid.h. */
static const GUID cmWpuDiskGuid = { 0x53f56307UL, 0xb6bf, 0x11d0,
    { 0x94, 0xf2, 0x00, 0xa0, 0xc9, 0x1e, 0xfb, 0x8b } };
static const GUID cmWpuArrival = { 0xcb3a4004UL, 0x46f0, 0x11d0,
    { 0xb0, 0x8f, 0x00, 0x60, 0x97, 0x13, 0x05, 0x3f } };
static const GUID cmWpuRemoval = { 0xcb3a4005UL, 0x46f0, 0x11d0,
    { 0xb0, 0x8f, 0x00, 0x60, 0x97, 0x13, 0x05, 0x3f } };
static const CM_WPU_PROPKEY cmWpuKeyParent = { { 0x4340a6c5UL, 0x93fa,
    0x4706, { 0x97, 0x2c, 0x7b, 0x64, 0x80, 0x08, 0xa5, 0xa7 } }, 8 };
static const CM_WPU_PROPKEY cmWpuKeyInst = { { 0x78c34fc8UL, 0x104a,
    0x4aca, { 0x9e, 0xa4, 0x52, 0x4d, 0x52, 0x99, 0x6e, 0x57 } }, 256 };
static CM_WPU_BASEREF cmWpuBaseRef;
static CM_WPU_IFACES cmWpuIfaces;
static CM_WPU_REGPNP cmWpuRegPnp;
static CM_WPU_OPEN cmWpuOpenPtr;    /* IoGetDeviceObjectPointer, looked up:
                                       its import row admits cm_oscnt.obj */
static ULONG cmWpuR2;           /* 0 not tried, 1 to register, 2 on, 3 off */
static PVOID cmWpuEntry;

/* The links and the queue, under cmWpuQLock (a zeroed static spin lock
 * is an initialised one). A link's token changes at each arrival and
 * removal; a unit matched through it is current while it holds the
 * link's present token. */
static KSPIN_LOCK cmWpuQLock;
static WCHAR cmWpuLinkName[CM_WPU_LINKS][CM_WPU_LINK_CHARS];
static USHORT cmWpuLinkBytes[CM_WPU_LINKS];
static ULONG cmWpuLinkTok[CM_WPU_LINKS];
static ULONG cmWpuLinkUsed[CM_WPU_LINKS];
static ULONG cmWpuLinkRemovedAt[CM_WPU_LINKS];  /* its last removal's seq */
static ULONG cmWpuTokSeq;
static ULONG cmWpuRemovalSeq;
static ULONG cmWpuQIdx[CM_WPU_QUEUE];
static ULONG cmWpuQTok[CM_WPU_QUEUE];
static ULONG cmWpuQHead;
static ULONG cmWpuQCount;
static ULONG cmWpuRescan;
static ULONG cmWpuWkQueued;
static WORK_QUEUE_ITEM cmWpuWork;
/* Quiet: set by the thread while no wake is in progress (no sleep or
 * stop received since the last release, and that release more than
 * CM_WPU_WINDOW_MS ago); the work item opens no disk otherwise, since a
 * parked disk's open could block, and leaves its queue for the thread to
 * hand back (Deferred). Lookups begun and ended, for the stuck note. */
static ULONG cmWpuDeferred;
static LONG cmWpuLookBegun;
static LONG cmWpuLookEnded;
static ULONG cmWpuStuckFrom;
static ULONG cmWpuStuckSeen;
static ULONG cmWpuStuckLogged;

/* The work item's tallies, for the thread's notes: each lookup's furthest
 * point (cm.wakepowerup.r2.why: opens failed, no base PDO, parent not read,
 * members snapshotted, instance ids not read, ids that differed, matches
 * refused at the commit), so a lookup that finds nothing says why. */
static ULONG cmWpuWhy[7];
static ULONG cmWpuLoggedWhy[7];
static ULONG cmWpuSeen;
static ULONG cmWpuMatched;
static ULONG cmWpuLoggedSeen;
static ULONG cmWpuLoggedMatched;
static ULONG cmWpuLoggedR2;

static BOOLEAN cmWpuGuidEq(const GUID *a, const GUID *b)
{
    ULONG i;

    if (a->Data1 != b->Data1 || a->Data2 != b->Data2 ||
        a->Data3 != b->Data3) {
        return FALSE;
    }
    for (i = 0; i < 8; i++) {
        if (a->Data4[i] != b->Data4[i]) {
            return FALSE;
        }
    }
    return TRUE;
}

/* A link's entry, found or made, with a fresh token; CM_WPU_LINKS when
 * the name cannot be held, or (guard nonzero) when the link's last
 * removal is numbered `guard` or above (a replay's listing older than
 * that removal never makes it current again). Under cmWpuQLock. */
static ULONG cmWpuEnterEx(const WCHAR *name, ULONG bytes, PULONG tok,
                          ULONG guard, BOOLEAN keep);

static ULONG cmWpuEnter(const WCHAR *name, ULONG bytes, PULONG tok,
                        ULONG guard)
{
    return cmWpuEnterEx(name, bytes, tok, guard, FALSE);
}

static ULONG cmWpuEnterEx(const WCHAR *name, ULONG bytes, PULONG tok,
                          ULONG guard, BOOLEAN keep)
{
    ULONG i;
    ULONG pick;

    if (bytes == 0 || bytes > (CM_WPU_LINK_CHARS - 1) * sizeof(WCHAR)) {
        return CM_WPU_LINKS;
    }
    pick = CM_WPU_LINKS;
    for (i = 0; i < CM_WPU_LINKS; i++) {
        if (cmWpuLinkBytes[i] == bytes &&
            cmWpuEqi(cmWpuLinkName[i], bytes / sizeof(WCHAR), name,
                     bytes / sizeof(WCHAR))) {
            pick = i;
            break;
        }
    }
    if (pick != CM_WPU_LINKS && guard != 0 &&
        cmWpuLinkRemovedAt[pick] >= guard) {
        return CM_WPU_LINKS;
    }
    if (pick != CM_WPU_LINKS && keep && cmWpuLinkRemovedAt[pick] == 0) {
        /* A replay of a link already current: its token kept, so a unit
         * it matched stays current if this lookup fails or stalls. */
        *tok = cmWpuLinkTok[pick];
        return pick;
    }
    if (pick == CM_WPU_LINKS) {
        /* An unused entry, else the least recently entered; a unit still
         * carrying the old token of a reused entry is never asked. */
        pick = 0;
        for (i = 0; i < CM_WPU_LINKS; i++) {
            if (cmWpuLinkBytes[i] == 0) {
                pick = i;
                break;
            }
            if (cmWpuLinkUsed[i] < cmWpuLinkUsed[pick]) {
                pick = i;
            }
        }
        RtlCopyMemory(cmWpuLinkName[pick], name, bytes);
        cmWpuLinkName[pick][bytes / sizeof(WCHAR)] = 0;
        cmWpuLinkBytes[pick] = (USHORT)bytes;
        cmWpuLinkRemovedAt[pick] = 0;
    }
    cmWpuTokSeq++;
    cmWpuLinkTok[pick] = cmWpuTokSeq;
    cmWpuLinkUsed[pick] = cmWpuTokSeq;
    *tok = cmWpuTokSeq;
    return pick;
}

static VOID cmWpuWorker(PVOID context);

/* Marks the work item queued if it is not already. Under cmWpuQLock;
 * TRUE when the caller must queue it once the lock is released. */
static BOOLEAN cmWpuNeedWork(VOID)
{
    if (cmWpuWkQueued) {
        return FALSE;
    }
    cmWpuWkQueued = 1;
    return TRUE;
}

/* A START (rescan 1): the present disks matched again by the work item.
 * PASSIVE_LEVEL. */
static VOID cmWpuKick(ULONG rescan)
{
    KIRQL irql;
    BOOLEAN queue;

    if (cmWpuR2 != 2) {
        return;
    }
    /* Any queue still owed comes with it. */
    KeAcquireSpinLock(&cmWpuQLock, &irql);
    cmWpuRescan |= rescan;
    cmWpuDeferred = 0;
    queue = cmWpuNeedWork();
    KeReleaseSpinLock(&cmWpuQLock, irql);
    if (queue) {
        ExQueueWorkItem(&cmWpuWork, DelayedWorkQueue);
    }
}

/* PnP's interface notification: the link recorded and queued; nothing is
 * opened here (the work item does it). PASSIVE_LEVEL. */
static NTSTATUS cmWpuNotify(PVOID notification, PVOID context)
{
    PDEVICE_INTERFACE_CHANGE_NOTIFICATION n;
    KIRQL irql;
    ULONG idx;
    ULONG tok;
    ULONG i;
    BOOLEAN arrival;
    BOOLEAN queue;

    UNREFERENCED_PARAMETER(context);
    n = (PDEVICE_INTERFACE_CHANGE_NOTIFICATION)notification;
    if (n == NULL || n->SymbolicLinkName == NULL ||
        n->SymbolicLinkName->Buffer == NULL) {
        return STATUS_SUCCESS;
    }
    arrival = cmWpuGuidEq(&n->Event, &cmWpuArrival);
    if (!arrival && !cmWpuGuidEq(&n->Event, &cmWpuRemoval)) {
        return STATUS_SUCCESS;
    }
    queue = FALSE;
    KeAcquireSpinLock(&cmWpuQLock, &irql);
    if (!arrival) {
        /* Never dropped: the link's entry (made for it if it had none, a
         * tombstone a replay respects) takes a fresh token no lookup
         * carries, so every unit matched through it stops being current,
         * and the removal's number. */
        idx = cmWpuEnter(n->SymbolicLinkName->Buffer,
                         n->SymbolicLinkName->Length, &tok, 0);
        if (idx < CM_WPU_LINKS) {
            cmWpuRemovalSeq++;
            cmWpuLinkRemovedAt[idx] = cmWpuRemovalSeq;
        }
    } else {
        idx = cmWpuEnter(n->SymbolicLinkName->Buffer,
                         n->SymbolicLinkName->Length, &tok, 0);
        if (idx < CM_WPU_LINKS) {
            cmWpuLinkRemovedAt[idx] = 0;
            if (cmWpuQCount < CM_WPU_QUEUE) {
                i = (cmWpuQHead + cmWpuQCount) % CM_WPU_QUEUE;
                cmWpuQIdx[i] = idx;
                cmWpuQTok[i] = tok;
                cmWpuQCount++;
            } else {
                /* Overflow: the present disks replayed once drained. */
                cmWpuRescan = 1;
            }
            queue = cmWpuNeedWork();
        }
    }
    KeReleaseSpinLock(&cmWpuQLock, irql);
    if (queue) {
        ExQueueWorkItem(&cmWpuWork, DelayedWorkQueue);
    }
    return STATUS_SUCCESS;
}

/* The member filters, as references the work item owns: each admitted
 * through CmFilterTryHold under cmRegLock (which takes the PDO's
 * reference under the filter's lock, where a kept child's detachment
 * claims Removing), its PDO referenced again, its own stamp and start
 * count recorded, the hold released after the list's lock. PASSIVE_LEVEL. */
typedef struct _CM_WPU_SNAP {
    PDEVICE_OBJECT Pdo;
    ULONG AttSeq;
    ULONG StartGen;
} CM_WPU_SNAP;

static ULONG cmWpuSnapshot(CM_WPU_SNAP *s, ULONG room)
{
    PCM_FILTER held[CM_WPU_CAND];
    PLIST_ENTRY e;
    PCM_FILTER g;
    KIRQL irql;
    ULONG n;
    ULONG i;

    n = 0;
    KeAcquireSpinLock(&cmRegLock, &irql);
    for (e = cmRegAtts.Flink; e != &cmRegAtts; e = e->Flink) {
        g = CONTAINING_RECORD(e, CM_FILTER, AttLink);
        if (g->Common.Kind != CM_KIND_CHILD || !g->AttListed ||
            !g->Started || g->ChainKind == CM_CHAIN_BRIDGE) {
            continue;
        }
        if (n >= room || n >= CM_WPU_CAND) {
            cmWpuOver++;
            break;
        }
        if (CmFilterTryHold(g)) {
            ObReferenceObject(g->Common.Pdo);
            s[n].Pdo = g->Common.Pdo;
            s[n].AttSeq = g->AttSeq;
            s[n].StartGen = g->WpuStartGen;
            held[n] = g;
            n++;
        }
    }
    KeReleaseSpinLock(&cmRegLock, irql);
    for (i = 0; i < n; i++) {
        CmFilterUnhold(held[i]);
    }
    return n;
}

/* The commit: the filter found again by its PDO and its own stamp, held,
 * and changed only while it is open and its start count unchanged, and a
 * unit only while its link's token is current. PASSIVE_LEVEL. */
static VOID cmWpuCommit(const CM_WPU_SNAP *s, ULONG owner,
                        PDEVICE_OBJECT unit, ULONG idx, ULONG tok)
{
    PDEVICE_OBJECT stale[CM_WPU_UNITS];
    PLIST_ENTRY e;
    PCM_FILTER g;
    PCM_FILTER held;
    KIRQL irql;
    KIRQL qirql;
    ULONG i;
    ULONG k;
    ULONG nStale;
    BOOLEAN current;

    held = NULL;
    KeAcquireSpinLock(&cmRegLock, &irql);
    for (e = cmRegAtts.Flink; e != &cmRegAtts; e = e->Flink) {
        g = CONTAINING_RECORD(e, CM_FILTER, AttLink);
        if (g->AttListed && g->AttSeq == s->AttSeq &&
            g->Common.Pdo == s->Pdo && CmFilterTryHold(g)) {
            held = g;
            break;
        }
    }
    KeReleaseSpinLock(&cmRegLock, irql);
    if (held == NULL) {
        return;
    }
    nStale = 0;
    KeAcquireSpinLock(&held->Lock, &irql);
    if (!held->WpuClosed && held->WpuStartGen == s->StartGen) {
        if (owner != CM_WPU_OWN_UNKNOWN &&
            held->WpuOwner == CM_WPU_OWN_UNKNOWN) {
            /* Only where the start's own reading found nothing. */
            held->WpuOwner = owner;
        }
        /* Entries whose link's token moved on (a removal, or the entry
         * reused) are pruned first, so they never crowd out a current
         * unit; their references are dropped after the lock. */
        KeAcquireSpinLock(&cmWpuQLock, &qirql);
        for (i = 0, k = 0; i < held->WpuDiskCount; i++) {
            if (held->WpuDiskLink[i] < CM_WPU_LINKS &&
                cmWpuLinkTok[held->WpuDiskLink[i]] == held->WpuDiskTok[i]) {
                held->WpuDisk[k] = held->WpuDisk[i];
                held->WpuDiskLink[k] = held->WpuDiskLink[i];
                held->WpuDiskTok[k] = held->WpuDiskTok[i];
                k++;
            } else {
                stale[nStale++] = held->WpuDisk[i];
            }
        }
        held->WpuDiskCount = k;
        KeReleaseSpinLock(&cmWpuQLock, qirql);
        if (unit != NULL) {
            KeAcquireSpinLock(&cmWpuQLock, &qirql);
            current = (BOOLEAN)(idx < CM_WPU_LINKS &&
                                cmWpuLinkTok[idx] == tok);
            KeReleaseSpinLock(&cmWpuQLock, qirql);
            for (i = 0; current && i < held->WpuDiskCount; i++) {
                if (held->WpuDisk[i] == unit) {
                    held->WpuDiskLink[i] = idx;
                    held->WpuDiskTok[i] = tok;
                    current = FALSE;
                }
            }
            if (!current) {
                cmWpuWhy[6]++;
            }
            if (current && held->WpuDiskCount < CM_WPU_UNITS) {
                ObReferenceObject(unit);
                i = held->WpuDiskCount++;
                held->WpuDisk[i] = unit;
                held->WpuDiskLink[i] = idx;
                held->WpuDiskTok[i] = tok;
                cmWpuMatched++;
            }
        }
    }
    KeReleaseSpinLock(&held->Lock, irql);
    cmWpuDeref(stale, nStale);
    CmFilterUnhold(held);
}


/* Every member's owner read and committed, and the disk `unit` (whose
 * parent is `parent`; NULL for none) installed on the member it names.
 * PASSIVE_LEVEL, no lock held. */
static VOID cmWpuMatch(PDEVICE_OBJECT unit, const WCHAR *parent,
                       ULONG parentChars, ULONG idx, ULONG tok)
{
    CM_WPU_SNAP s[CM_WPU_CAND];
    WCHAR prop[CM_WPU_PROP_CHARS];
    ULONG n;
    ULONG i;
    ULONG c;
    ULONG owner;
    BOOLEAN match;

    n = cmWpuSnapshot(s, CM_WPU_CAND);
    if (unit != NULL) {
        cmWpuWhy[3] += n;
    }
    for (i = 0; i < n; i++) {
        owner = CM_WPU_OWN_UNKNOWN;
        c = cmWpuProp(s[i].Pdo, &cmWpuKeyService, prop);
        if (c != 0xFFFFFFFFUL) {
            owner = cmWpuEqi(prop, c, cmWpuStor98, 6) ? CM_WPU_OWN_STOR98
                                                      : CM_WPU_OWN_OTHER;
        }
        match = FALSE;
        if (unit != NULL) {
            c = cmWpuProp(s[i].Pdo, &cmWpuKeyInst, prop);
            match = (BOOLEAN)(c != 0xFFFFFFFFUL &&
                              cmWpuEqi(prop, c, parent, parentChars));
            if (c == 0xFFFFFFFFUL) {
                cmWpuWhy[4]++;
            } else if (!match) {
                cmWpuWhy[5]++;
            }
        }
        cmWpuCommit(&s[i], owner, match ? unit : NULL, idx, tok);
        ObDereferenceObject(s[i].Pdo);
    }
}

/* One disk link: opened for its PDO (a shared open and close of the
 * disk), its parent read, matched. PASSIVE_LEVEL, the work item. */
static VOID cmWpuLookup(WCHAR *name, ULONG bytes, ULONG idx, ULONG tok)
{
    UNICODE_STRING us;
    PFILE_OBJECT fo;
    PDEVICE_OBJECT top;
    PDEVICE_OBJECT base;
    WCHAR parent[CM_WPU_PROP_CHARS];
    ULONG c;

    cmWpuSeen++;
    (VOID)InterlockedIncrement(&cmWpuLookBegun);
    us.Buffer = name;
    us.Length = (USHORT)bytes;
    us.MaximumLength = (USHORT)(bytes + sizeof(WCHAR));
    if (NT_SUCCESS(cmWpuOpenPtr(&us, FILE_READ_ATTRIBUTES, &fo, &top))) {
        /* The base PDO's own reference first, then the open given back. */
        base = cmWpuBaseRef(top);
        ObDereferenceObject(fo);
        if (base != NULL) {
            c = cmWpuProp(base, &cmWpuKeyParent, parent);
            if (c != 0xFFFFFFFFUL && c != 0) {
                cmWpuMatch(base, parent, c, idx, tok);
            } else {
                cmWpuWhy[2]++;
            }
            ObDereferenceObject(base);
        } else {
            cmWpuWhy[1]++;
        }
    } else {
        cmWpuWhy[0]++;
    }
    (VOID)InterlockedIncrement(&cmWpuLookEnded);
}

/* The present disks, each as an arrival with a fresh token, then every
 * member's owner. PASSIVE_LEVEL, the work item. */
static VOID cmWpuReplay(VOID)
{
    PWSTR list;
    PWSTR p;
    WCHAR name[CM_WPU_LINK_CHARS];
    KIRQL irql;
    ULONG chars;
    ULONG idx;
    ULONG tok;
    ULONG guard;

    KeAcquireSpinLock(&cmWpuQLock, &irql);
    guard = cmWpuRemovalSeq;
    KeReleaseSpinLock(&cmWpuQLock, irql);
    list = NULL;
    if (NT_SUCCESS(cmWpuIfaces(&cmWpuDiskGuid, NULL, 0, &list)) &&
        list != NULL) {
        for (p = list; *p != 0; p += chars + 1) {
            chars = cmWpuChars(p, 0xFFFFUL);
            if (!cmWpuQuietNow()) {
                /* A wake began: no more opens; the replay is owed again
                 * and the thread hands it back once quiet. */
                KeAcquireSpinLock(&cmWpuQLock, &irql);
                cmWpuRescan = 1;
                cmWpuDeferred = 1;
                KeReleaseSpinLock(&cmWpuQLock, irql);
                break;
            }
            if (chars >= CM_WPU_LINK_CHARS) {
                continue;
            }
            KeAcquireSpinLock(&cmWpuQLock, &irql);
            /* A link removed after the listing began (a removal numbered
             * above guard) is not made current again. */
            idx = cmWpuEnterEx(p, chars * (ULONG)sizeof(WCHAR), &tok,
                               guard + 1, TRUE);
            KeReleaseSpinLock(&cmWpuQLock, irql);
            if (idx < CM_WPU_LINKS) {
                RtlCopyMemory(name, p, chars * sizeof(WCHAR));
                name[chars] = 0;
                cmWpuLookup(name, chars * (ULONG)sizeof(WCHAR), idx, tok);
            }
        }
        /* The list is the I/O manager's pool: freed at the one site the
         * import allowlist admits for ExFreePool (cm_pool.c). */
        CmFree(list);
    }
    cmWpuMatch(NULL, NULL, 0, CM_WPU_LINKS, 0);
}

/* The work item: the registration first, then each queued link, then a
 * replay where one is owed. Never on the connection-manager thread.
 * PASSIVE_LEVEL. */
static VOID cmWpuWorker(PVOID context)
{
    WCHAR name[CM_WPU_LINK_CHARS];
    KIRQL irql;
    ULONG have;
    ULONG idx;
    ULONG tok;
    ULONG bytes;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(context);
    if (cmWpuR2 == 1) {
        status = cmWpuRegPnp(
            EventCategoryDeviceInterfaceChange,
            PNPNOTIFY_DEVICE_INTERFACE_INCLUDE_EXISTING_INTERFACES,
            (PVOID)&cmWpuDiskGuid, CmDriverObject,
            (PDRIVER_NOTIFICATION_CALLBACK_ROUTINE)cmWpuNotify, NULL,
            &cmWpuEntry);
        cmWpuR2 = NT_SUCCESS(status) ? 2UL : 3UL;
    }
    for (;;) {
        idx = CM_WPU_LINKS;
        tok = 0;
        bytes = 0;
        KeAcquireSpinLock(&cmWpuQLock, &irql);
        if ((cmWpuQCount != 0 || cmWpuRescan) && !cmWpuQuietNow()) {
            /* A wake in progress: nothing opened; the thread hands the
             * queue back once quiet. */
            cmWpuDeferred = 1;
            cmWpuWkQueued = 0;
            have = 0;
        } else if (cmWpuQCount != 0) {
            idx = cmWpuQIdx[cmWpuQHead];
            tok = cmWpuQTok[cmWpuQHead];
            cmWpuQHead = (cmWpuQHead + 1) % CM_WPU_QUEUE;
            cmWpuQCount--;
            /* Stale (the link removed or its entry reused since it was
             * queued): skipped. */
            have = 3;
            if (cmWpuLinkTok[idx] == tok) {
                bytes = cmWpuLinkBytes[idx];
                RtlCopyMemory(name, cmWpuLinkName[idx], bytes);
                name[bytes / sizeof(WCHAR)] = 0;
                have = 1;
            }
        } else if (cmWpuRescan) {
            cmWpuRescan = 0;
            have = 2;
        } else {
            cmWpuWkQueued = 0;
            have = 0;
        }
        KeReleaseSpinLock(&cmWpuQLock, irql);
        if (have == 0) {
            break;
        }
        if (have == 1) {
            cmWpuLookup(name, bytes, idx, tok);
        } else if (have == 2 && cmWpuR2 == 2) {
            cmWpuReplay();
        }
    }
}

/* Route R2's start, from the thread: the routines looked up (CmRoutine,
 * no import), and where they exist the registration handed to the work
 * item. The image is made resident first: the callback and the work item
 * are this image's code. PASSIVE_LEVEL. */
static VOID cmWpuR2Start(PCM_NHI nhi)
{
    KIRQL irql;
    BOOLEAN queue;

    if (cmWpuR2 != 0 || InterlockedIncrement(&cmWpuR2Claim) != 1) {
        return;
    }
    cmWpuGetProp = (CM_WPU_GETPROP)CmRoutine(L"IoGetDevicePropertyData");
    cmWpuBaseRef = (CM_WPU_BASEREF)CmRoutine(L"IoGetDeviceAttachmentBaseRef");
    cmWpuIfaces = (CM_WPU_IFACES)CmRoutine(L"IoGetDeviceInterfaces");
    cmWpuRegPnp = (CM_WPU_REGPNP)CmRoutine(L"IoRegisterPlugPlayNotification");
    cmWpuOpenPtr = (CM_WPU_OPEN)CmRoutine(L"IoGetDeviceObjectPointer");
    if (cmWpuGetProp == NULL || cmWpuBaseRef == NULL || cmWpuIfaces == NULL ||
        cmWpuRegPnp == NULL || cmWpuOpenPtr == NULL) {
        cmWpuR2 = 3;
        CmLogNote(nhi, "cm.wakepowerup.r2.routines",
                  (cmWpuGetProp != NULL ? 1UL : 0UL) |
                      (cmWpuBaseRef != NULL ? 2UL : 0UL) |
                      (cmWpuIfaces != NULL ? 4UL : 0UL) |
                      (cmWpuRegPnp != NULL ? 8UL : 0UL) |
                      (cmWpuOpenPtr != NULL ? 16UL : 0UL));
        return;
    }
    CmDriverObject->DriverUnload = NULL;
    ExInitializeWorkItem(&cmWpuWork, cmWpuWorker, NULL);
    cmWpuR2 = 1;
    KeAcquireSpinLock(&cmWpuQLock, &irql);
    queue = cmWpuNeedWork();
    KeReleaseSpinLock(&cmWpuQLock, irql);
    if (queue) {
        ExQueueWorkItem(&cmWpuWork, DelayedWorkQueue);
    }
}
#endif /* CM_WPU_R2_BUILT */

/* ---- the thread's half ---- */

/* The kernel's tick count (one clock for every NHI's thread), and the
 * window in ticks: CM_WPU_WINDOW_MS * 10000 stays below 2^32, so no 64-bit
 * arithmetic is needed (as cmRegPreTrustOpen). Route R2's alone. */
#if CM_WPU_R2_BUILT
static ULONG cmWpuTick(VOID)
{
    LARGE_INTEGER now;

    KeQueryTickCount(&now);
    return now.LowPart;
}

static ULONG cmWpuWindowTicks(VOID)
{
    ULONG incr;

    incr = KeQueryTimeIncrement();
    if (incr == 0) {
        incr = 1;
    }
    return (CM_WPU_WINDOW_MS * 10000UL) / incr;
}
#endif

/* A tunnel port of `nhi` released by a resume (cmReleaseHeld, never at a
 * stop): accepted only as the release of this domain's own resume, with
 * no sleep query, sleep or shutdown since that resume began
 * (CmWpuResumeBegin) and the domain not latched by a stop or query
 * (CmPortsWpuDomRelease); then its epoch and time recorded and the
 * system's halt cleared, never another domain's latch. The thread,
 * PASSIVE_LEVEL. */
static VOID cmWpuPortReleased(PCM_NHI nhi, ULONG i)
{
    CM_WPU_DOM d;
    PCM_REENUM r;
    KIRQL irql;
    ULONG ok;
    ULONG epoch;

    r = &nhi->Reenum;
    if (r->WpuClosed || nhi->Values.WakePowerUp == 0 ||
        (r->Port[i].Flags & (CM_PORT_ACTIVE | CM_PORT_EXCLUSIVE)) !=
            (CM_PORT_ACTIVE | CM_PORT_EXCLUSIVE)) {
        /* Only a port this domain owns alone (an active, exclusive member)
         * authorises a wake power-up behind it; off, nothing recorded. */
        return;
    }
    KeAcquireSpinLock(&cmWpuGateLock, &irql);
    cmWpuDomLoad(r, &d);
    ok = CmPortsWpuDomRelease(&cmWpuSys, &d);
    cmWpuDomStore(r, &d);
    epoch = cmWpuSys.Epoch;
    if (ok) {
        r->Port[i].WpuRelEpoch = epoch;
#if CM_WPU_R2_BUILT
        cmWpuLastRelTick = cmWpuTick();
        cmWpuReleased = 1;
        cmWpuQuiet = 0;
#endif
    }
    KeReleaseSpinLock(&cmWpuGateLock, irql);
    if (ok) {
        r->Port[i].WpuRelAt = cmNow(nhi);
    } else {
        /* A release of an earlier resume, or of a latched domain: no ask
         * behind it (counted; the first of each epoch logged). */
        if (r->WpuRelRefused != epoch) {
            r->WpuRelRefused = epoch;
            CmLogNote(nhi, "cm.wakepowerup.release.refused",
                      (epoch << 8) | (i & 0xFFUL));
        }
    }
}

/* The power manager's callback. IRQL: <= DISPATCH_LEVEL. */
static VOID NTAPI cmWpuDone(PDEVICE_OBJECT DeviceObject, UCHAR MinorFunction,
                            POWER_STATE PowerState, PVOID Context,
                            PIO_STATUS_BLOCK IoStatus)
{
    CM_WPU_SLOT *s;
    PDEVICE_OBJECT unit;

    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(MinorFunction);
    UNREFERENCED_PARAMETER(PowerState);
    s = (CM_WPU_SLOT *)Context;
    unit = s->Unit;
    s->Status = (ULONG)IoStatus->Status;
    /* Published last (a full barrier): the thread reuses the slot only
     * after it reads Done. */
    (VOID)InterlockedIncrement(&s->Done);
    ObDereferenceObject(unit);
}

static VOID cmWpuLogSlots(PCM_NHI nhi)
{
    CM_WPU_SLOT *s;
    KIRQL irql;
    ULONG now;
    ULONG i;
    ULONG bdf;
    ULONG status;
    BOOLEAN overdue;
    BOOLEAN done;

    now = cmNow(nhi);
    for (i = 0; i < CM_WPU_SLOTS; i++) {
        s = &cmWpuSlot[i];
        overdue = FALSE;
        done = FALSE;
        bdf = 0;
        status = 0;
        KeAcquireSpinLock(&cmWpuSlotLock, &irql);
        if (s->Busy && s->Done == 0 && !s->Overdue &&
            now - s->StartAt > CM_WPU_OVERDUE_MS) {
            /* Detected, never cancelled (a power IRP has no cancel): the
             * slot and its reference stay until the callback, and no
             * second request is made for the unit meanwhile. */
            s->Overdue = 1;
            overdue = TRUE;
            bdf = s->Bdf;
        } else if (s->Busy && s->Done != 0) {
            bdf = s->Bdf;
            status = s->Status;
            s->Unit = NULL;
            s->Done = 0;
            s->Busy = 0;
            done = TRUE;
        }
        KeReleaseSpinLock(&cmWpuSlotLock, irql);
        if (overdue) {
            CmLogNote(nhi, "cm.wakepowerup.overdue", (i << 24) | (bdf & 0xFFFFUL));
        }
        if (done) {
            CmLogNote(nhi, "cm.wakepowerup.done", (i << 24) | (bdf & 0xFFFFUL));
            CmLogNote(nhi, "cm.wakepowerup.done.status", status);
            if (NT_SUCCESS((NTSTATUS)status)) {
                nhi->Reenum.WpuOk++;
            } else {
                nhi->Reenum.WpuFailed++;
            }
        }
    }
}

static ULONG cmWpuSlotsFree(VOID)
{
    KIRQL irql;
    ULONG i;
    ULONG n;

    n = 0;
    KeAcquireSpinLock(&cmWpuSlotLock, &irql);
    for (i = 0; i < CM_WPU_SLOTS; i++) {
        if (!cmWpuSlot[i].Busy) {
            n++;
        }
    }
    KeReleaseSpinLock(&cmWpuSlotLock, irql);
    return n;
}

/* Every unit's slot reserved at once under the slots' lock, or none:
 * TRUE with out[i] the slot of units[i], each holding its unit (the
 * caller's reference passes to it). The callback's context and reference
 * never change after this. */
static BOOLEAN cmWpuReserve(PCM_NHI nhi, PDEVICE_OBJECT *units, ULONG n,
                            ULONG bdf, CM_WPU_SLOT **out)
{
    KIRQL irql;
    ULONG now;
    ULONG i;
    ULONG k;
    ULONG free;

    now = cmNow(nhi);
    KeAcquireSpinLock(&cmWpuSlotLock, &irql);
    free = 0;
    for (i = 0; i < CM_WPU_SLOTS; i++) {
        if (!cmWpuSlot[i].Busy) {
            free++;
            continue;
        }
        for (k = 0; k < n; k++) {
            if (cmWpuSlot[i].Unit == units[k]) {
                /* A request for this unit is still out (one at most per
                 * unit, whatever the wake): nothing reserved. */
                free = 0;
                i = CM_WPU_SLOTS;
                break;
            }
        }
    }
    if (free < n) {
        KeReleaseSpinLock(&cmWpuSlotLock, irql);
        return FALSE;
    }
    k = 0;
    for (i = 0; i < CM_WPU_SLOTS && k < n; i++) {
        if (!cmWpuSlot[i].Busy) {
            cmWpuSlot[i].Busy = 1;
            cmWpuSlot[i].Done = 0;
            cmWpuSlot[i].Status = 0;
            cmWpuSlot[i].Overdue = 0;
            cmWpuSlot[i].StartAt = now;
            cmWpuSlot[i].Bdf = bdf;
            cmWpuSlot[i].Unit = units[k];
            out[k] = &cmWpuSlot[i];
            k++;
        }
    }
    KeReleaseSpinLock(&cmWpuSlotLock, irql);
    return TRUE;
}

/* A reserved slot given back unused, its reference with it. */
static VOID cmWpuRelease(CM_WPU_SLOT *s)
{
    KIRQL irql;
    PDEVICE_OBJECT unit;

    KeAcquireSpinLock(&cmWpuSlotLock, &irql);
    unit = s->Unit;
    s->Unit = NULL;
    s->Busy = 0;
    KeReleaseSpinLock(&cmWpuSlotLock, irql);
    if (unit != NULL) {
        ObDereferenceObject(unit);
    }
}

/* The request on a reserved slot (which holds the unit's reference). */
static VOID cmWpuRequest(PCM_NHI nhi, CM_WPU_SLOT *s)
{
    POWER_STATE ps;
    NTSTATUS status;
    KIRQL irql;
    PDEVICE_OBJECT unit;

    unit = s->Unit;
    ps.DeviceState = PowerDeviceD0;
    /* No returned IRP is asked for (NULL); the callback may run before
     * the call returns, and touches nothing but the slot, already set. */
    status = PoRequestPowerIrp(unit, IRP_MN_SET_POWER, ps, cmWpuDone, s,
                               NULL);
    if (!NT_SUCCESS(status)) {
        /* No IRP and no callback (as cm_power.c's own caller handles):
         * the slot and the reference given back here. */
        CmLogNote(nhi, "cm.wakepowerup.done.status", (ULONG)status);
        nhi->Reenum.WpuFailed++;
        KeAcquireSpinLock(&cmWpuSlotLock, &irql);
        s->Unit = NULL;
        s->Busy = 0;
        KeReleaseSpinLock(&cmWpuSlotLock, irql);
        ObDereferenceObject(unit);
    }
}

/* A request for one of `units` still out (overdue or not). */
static ULONG cmWpuOutstanding(PDEVICE_OBJECT *units, ULONG n)
{
    KIRQL irql;
    ULONG i;
    ULONG j;
    ULONG out;

    out = 0;
    KeAcquireSpinLock(&cmWpuSlotLock, &irql);
    for (i = 0; i < CM_WPU_SLOTS && !out; i++) {
        if (!cmWpuSlot[i].Busy) {
            continue;
        }
        for (j = 0; j < n; j++) {
            if (cmWpuSlot[i].Unit == units[j]) {
                out = 1;
            }
        }
    }
    KeReleaseSpinLock(&cmWpuSlotLock, irql);
    return out;
}

/* Adds `d` to `set` once and within its room: TRUE when added. */
static BOOLEAN cmWpuAddUnit(PDEVICE_OBJECT *set, PULONG n, PDEVICE_OBJECT d)
{
    ULONG i;

    for (i = 0; i < *n; i++) {
        if (set[i] == d) {
            return FALSE;
        }
    }
    if (*n >= CM_WPU_UNITS) {
        return FALSE;
    }
    set[(*n)++] = d;
    return TRUE;
}

/* The filter's current units, both routes, into `set` (each once);
 * `ref` references each one added. Under the filter's lock. */
static VOID cmWpuUnitsOf(PCM_FILTER f, PDEVICE_OBJECT *set, PULONG n,
                         BOOLEAN ref)
{
#if CM_WPU_R2_BUILT
    KIRQL qirql;
    BOOLEAN current;
#endif
    ULONG i;

    for (i = 0; i < f->WpuRelCount; i++) {
        if (cmWpuAddUnit(set, n, f->WpuRel[i]) && ref) {
            ObReferenceObject(f->WpuRel[i]);
        }
    }
#if CM_WPU_R2_BUILT
    for (i = 0; i < f->WpuDiskCount; i++) {
        KeAcquireSpinLock(&cmWpuQLock, &qirql);
        current = (BOOLEAN)(f->WpuDiskLink[i] < CM_WPU_LINKS &&
                            cmWpuLinkTok[f->WpuDiskLink[i]] ==
                                f->WpuDiskTok[i]);
        KeReleaseSpinLock(&cmWpuQLock, qirql);
        if (current && cmWpuAddUnit(set, n, f->WpuDisk[i]) && ref) {
            ObReferenceObject(f->WpuDisk[i]);
        }
    }
#endif
}

typedef PDEVICE_OBJECT (NTAPI *CM_WPU_ATTREF)(PDEVICE_OBJECT device);

static CM_WPU_ATTREF cmWpuAttRef;
static LONG cmWpuAttRefClaim;       /* the lookup made once (the thread)  */

/* The driver at the top of the member's own stack, the device
 * IoGetAttachedDeviceReference returns for its PDO (looked up at run
 * time): CM_WPU_OWN_STOR98 when its driver object is named
 * \Driver\stor98 (in any case, the length checked), CM_WPU_OWN_OTHER for
 * any other driver - an upper filter above Stor98's device included: the
 * stack is not walked - and CM_WPU_OWN_UNKNOWN when it is not read. The
 * name is DRIVER_OBJECT's DriverName, a documented member (the
 * \Driver\<service> name), read only, under the top device's reference,
 * which is dropped at once. It says which driver runs the top of the stack
 * now; it proves nothing of the stack's later topology (record 21 section
 * 6.3.1; ObQueryNameString on the driver object is a possible later
 * alternative). The thread, PASSIVE_LEVEL. */
static ULONG cmWpuTopOwner(PDEVICE_OBJECT pdo)
{
    static const WCHAR name[] = { '\\', 'D', 'r', 'i', 'v', 'e', 'r', '\\',
                                  's', 't', 'o', 'r', '9', '8' };
    PDEVICE_OBJECT top;
    PDRIVER_OBJECT drv;
    ULONG v;

    if (cmWpuAttRef == NULL && InterlockedIncrement(&cmWpuAttRefClaim) == 1) {
        cmWpuAttRef = (CM_WPU_ATTREF)CmRoutine(L"IoGetAttachedDeviceReference");
    }
    if (cmWpuAttRef == NULL) {
        return CM_WPU_OWN_UNKNOWN;
    }
    top = cmWpuAttRef(pdo);
    if (top == NULL) {
        return CM_WPU_OWN_UNKNOWN;
    }
    v = CM_WPU_OWN_UNKNOWN;
    drv = top->DriverObject;
    if (drv != NULL && drv->DriverName.Buffer != NULL &&
        (drv->DriverName.Length & 1U) == 0 &&
        drv->DriverName.Length <= drv->DriverName.MaximumLength) {
        v = cmWpuEqi(drv->DriverName.Buffer,
                     drv->DriverName.Length / (ULONG)sizeof(WCHAR), name, 14)
                ? CM_WPU_OWN_STOR98
                : CM_WPU_OWN_OTHER;
    }
    ObDereferenceObject(top);
    return v;
}

/* A held child's owner, owed since its start, from its two readings
 * (CmPortsWpuOwner): the driver key's install section (cm_reg.c) and the
 * driver at the top of the member's stack. Read at a healthy moment - never while a
 * sleep is in progress or once its wake D0 has failed in this epoch, when
 * its stack may be held - and kept only if the start it was owed for is
 * still the current one; logged once a start (cm.wakepowerup.owner). The
 * thread, PASSIVE_LEVEL, the recovery on. */
static VOID cmWpuOwnerRead(PCM_NHI nhi, PCM_FILTER g)
{
    KIRQL irql;
    ULONG gen;
    ULONG owner;
    ULONG inf;
    ULONG top;
    ULONG epoch;
    ULONG bdf;
    BOOLEAN owed;
    BOOLEAN kept;

    epoch = CmWpuEpochNow();
    KeAcquireSpinLock(&g->Lock, &irql);
    owed = (BOOLEAN)(g->WpuOwnerOwed && !g->WpuClosed &&
                     (epoch == 0 || g->WpuFailEpoch != epoch));
    gen = g->WpuStartGen;
    KeReleaseSpinLock(&g->Lock, irql);
    if (!owed) {
        return;
    }
    KeAcquireSpinLock(&cmWpuGateLock, &irql);
    owed = (BOOLEAN)(cmWpuSys.Halt == 0);
    KeReleaseSpinLock(&cmWpuGateLock, irql);
    if (!owed) {
        return;
    }
    bdf = XHCI_TBT_BDF(g->LocBus, g->LocAddress >> 16,
                       g->LocAddress & 0xFFFFUL) & 0xFFFFUL;
    inf = CmRegWpuOwner(g->Common.Pdo);
    top = cmWpuTopOwner(g->Common.Pdo);
    owner = CmPortsWpuOwner(inf, top);
    kept = FALSE;
    KeAcquireSpinLock(&g->Lock, &irql);
    if (g->WpuOwnerOwed && g->WpuStartGen == gen) {
        g->WpuOwnerOwed = 0;
        g->WpuOwner = owner;
        g->WpuOwnerWhy = (inf & 0xFUL) | ((top & 0xFUL) << 4);
        kept = TRUE;
    }
    KeReleaseSpinLock(&g->Lock, irql);
    if (kept) {
        /* Bits 3:0 the install section, 7:4 the top of the stack, each
         * CM_WPU_OWN_* (0 not read, 1 stor98's, 2 another's). */
        CmLogNote(nhi, "cm.wakepowerup.owner", (bdf << 8) | (top << 4) | inf);
    }
}

/* One member: its filters f[0..n-1] (held), behind a tunnel port released
 * at `relAt` in this epoch when `released`. PASSIVE_LEVEL, the thread. */
static VOID cmWpuMember(PCM_NHI nhi, PCM_FILTER *f, ULONG n,
                        BOOLEAN released, ULONG relAt)
{
    PCM_REENUM r;
    CM_WPU_IN in;
    PDEVICE_OBJECT units[CM_WPU_UNITS];
    PDEVICE_OBJECT ask[CM_WPU_UNITS];
    CM_WPU_SLOT *slots[CM_WPU_UNITS];
    ULONG gens[CM_WPU_CAND];
    KIRQL irql;
    BOOLEAN reserved;
    ULONG nUnits;
    ULONG nAsk;
    ULONG epoch;
    ULONG bdf;
    ULONG why;
    ULONG verdict;
    ULONG failStatus;
    ULONG relCount;
    ULONG i;
    ULONG otherStatus;
    BOOLEAN logFail;
    BOOLEAN logOther;
    BOOLEAN relLog;
    BOOLEAN admitted;

    r = &nhi->Reenum;
    epoch = CmWpuEpochNow();
    bdf = XHCI_TBT_BDF(f[0]->LocBus, f[0]->LocAddress >> 16,
                       f[0]->LocAddress & 0xFFFFUL) & 0xFFFFUL;
    CmZero(&in, sizeof(in));
    in.Epoch = epoch;
    in.Switch = nhi->Values.WakePowerUp;
    /* Released: the port's release in this epoch, and since then no sleep,
     * shutdown, power-down or stop received, the NHI in D0. */
    in.Released = (released && cmWpuOpen(nhi, epoch) && !nhi->Suspended &&
                   nhi->DevicePower == PowerDeviceD0)
                      ? 1UL
                      : 0UL;
    in.Elapsed = cmNow(nhi) - relAt;
    in.Slots = cmWpuSlotsFree();
    nUnits = 0;
    failStatus = 0;
    relCount = 0;
    logFail = FALSE;
    logOther = FALSE;
    otherStatus = 0;
    relLog = FALSE;
    for (i = 0; i < n; i++) {
        KeAcquireSpinLock(&f[i]->Lock, &irql);
        /* The start this reading is of: admission refuses a later one. */
        gens[i] = f[i]->WpuStartGen;
#if defined(XHCI_FLAVOUR_QEMU)
        if (nhi->QemuWpuForce != 0 && released && epoch != 0 &&
            f[i]->WpuConsumed != epoch && f[i]->WpuFailEpoch != epoch) {
            /* The legs' forced trigger: armed as if the D0 had failed. */
            f[i]->WpuFailEpoch = epoch;
            f[i]->WpuFailStatus = 0xE0000049UL;
        }
#endif
        if (epoch != 0 && f[i]->WpuFailEpoch == epoch) {
            in.FailEpoch = epoch;
            failStatus = f[i]->WpuFailStatus;
            if (f[i]->WpuFailLogged != epoch) {
                f[i]->WpuFailLogged = epoch;
                logFail = TRUE;
            }
        }
        if (epoch != 0 && f[i]->WpuOtherEpoch == epoch &&
            f[i]->WpuOtherLogged != epoch) {
            f[i]->WpuOtherLogged = epoch;
            otherStatus = f[i]->WpuOtherStatus;
            logOther = TRUE;
        }
        if (f[i]->WpuConsumed == epoch) {
            in.Consumed = epoch;
        }
        if (f[i]->WpuClosed) {
            in.Closed = 1;
        }
        if (f[i]->WpuOwner == CM_WPU_OWN_OTHER) {
            in.Owner = CM_WPU_OWN_OTHER;
        } else if (in.Owner == CM_WPU_OWN_UNKNOWN &&
                   f[i]->WpuOwner == CM_WPU_OWN_STOR98) {
            in.Owner = CM_WPU_OWN_STOR98;
        }
        cmWpuUnitsOf(f[i], units, &nUnits, FALSE);
        if (f[i]->WpuRelAnswers != 0 &&
            f[i]->WpuRelLogged != f[i]->WpuRelCount + 1) {
            f[i]->WpuRelLogged = f[i]->WpuRelCount + 1;
            relCount = f[i]->WpuRelCount;
            relLog = TRUE;
        }
        KeReleaseSpinLock(&f[i]->Lock, irql);
    }
    if (relLog) {
        CmLogNote(nhi, "cm.wakepowerup.rel", (bdf << 8) | (relCount & 0xFFUL));
    }
    if (logFail) {
        r->WpuArmed++;
        CmLogNote(nhi, "cm.wakepowerup.fail", bdf);
        CmLogNote(nhi, "cm.wakepowerup.fail.status", failStatus);
    }
    if (logOther) {
        /* Never armed on: counted, so widening is decided on evidence. */
        CmLogNote(nhi, "cm.wakepowerup.fail.other", bdf);
        CmLogNote(nhi, "cm.wakepowerup.fail.other.status", otherStatus);
    }
    in.Units = nUnits;
    in.Outstanding = cmWpuOutstanding(units, nUnits);
    verdict = CmPortsWpuStep(&in, &why);
    if (verdict == CM_WPU_V_IDLE || verdict == CM_WPU_V_WAIT) {
        return;
    }
    if (verdict != CM_WPU_V_ASK) {
        for (i = 0; i < n; i++) {
            KeAcquireSpinLock(&f[i]->Lock, &irql);
            f[i]->WpuConsumed = epoch;
            KeReleaseSpinLock(&f[i]->Lock, irql);
        }
        if (verdict == CM_WPU_V_NONE) {
            r->WpuNone++;
            CmLogNote(nhi, "cm.wakepowerup.none", bdf);
        } else {
            CmLogNote(nhi, "cm.wakepowerup.skip", (why << 16) | bdf);
        }
        return;
    }
    /* Admission, the one point where the ask becomes requests: the domain
     * and every filter of the member still open, each marked Sending and
     * consumed, the units referenced under the filters' locks. */
    nAsk = 0;
    admitted = FALSE;
    KeAcquireSpinLock(&cmRegLock, &irql);
    if (!r->WpuClosed && cmWpuOpen(nhi, epoch) && !nhi->Suspended &&
        nhi->DevicePower == PowerDeviceD0) {
        r->WpuSending = 1;
        KeClearEvent(&r->WpuSendDone);
        admitted = TRUE;
    }
    KeReleaseSpinLock(&cmRegLock, irql);
    for (i = 0; admitted && i < n; i++) {
        KeAcquireSpinLock(&f[i]->Lock, &irql);
        if (f[i]->WpuClosed || f[i]->WpuConsumed == epoch ||
            f[i]->WpuStartGen != gens[i] || f[i]->WpuSending) {
            /* Closed, consumed, or restarted since this pass read its
             * arm, owner and units: nothing sent now; the next pass reads
             * the member afresh. */
            admitted = FALSE;
        } else {
            f[i]->WpuSending = 1;
            KeClearEvent(&f[i]->WpuSendDone);
            cmWpuUnitsOf(f[i], ask, &nAsk, TRUE);
        }
        KeReleaseSpinLock(&f[i]->Lock, irql);
    }
    reserved = (BOOLEAN)(admitted && nAsk != 0 &&
                         cmWpuReserve(nhi, ask, nAsk, bdf, slots));
    if (reserved) {
        /* Consumed only now that every unit has its slot; the Sending
         * marks keep every removal waiting until the requests are out. */
        for (i = 0; i < n; i++) {
            KeAcquireSpinLock(&f[i]->Lock, &irql);
            f[i]->WpuConsumed = epoch;
            KeReleaseSpinLock(&f[i]->Lock, irql);
        }
        /* The image resident from the first request on: the callback is
         * this image's code. */
        CmDriverObject->DriverUnload = NULL;
        r->WpuAsked++;
        CmLogNote(nhi, "cm.wakepowerup.ask", (bdf << 8) | nAsk);
        for (i = 0; i < nAsk; i++) {
            if (cmWpuOpen(nhi, epoch)) {
                cmWpuRequest(nhi, slots[i]);
            } else {
                /* Closed since the admission: this one not sent. */
                cmWpuRelease(slots[i]);
                CmLogNote(nhi, "cm.wakepowerup.skip",
                          (CM_WPU_WHY_CLOSED << 16) | bdf);
            }
        }
        nAsk = 0;
    } else {
        /* Not logged: a retry each pass until the window ends, whose own
         * outcome is logged. */
    }
    /* Not reserved (the slots taken meanwhile by another controller's
     * thread): nothing consumed, the next pass tries again within the
     * window; the references are dropped below. */
    /* Every mark this admission made is released, whether it completed or
     * stopped half-way; the references of a refused one dropped. */
    for (i = 0; i < n; i++) {
        KeAcquireSpinLock(&f[i]->Lock, &irql);
        if (f[i]->WpuSending) {
            f[i]->WpuSending = 0;
            (VOID)KeSetEvent(&f[i]->WpuSendDone, IO_NO_INCREMENT, FALSE);
        }
        KeReleaseSpinLock(&f[i]->Lock, irql);
    }
    KeAcquireSpinLock(&cmRegLock, &irql);
    if (r->WpuSending) {
        r->WpuSending = 0;
        (VOID)KeSetEvent(&r->WpuSendDone, IO_NO_INCREMENT, FALSE);
    }
    KeReleaseSpinLock(&cmRegLock, irql);
    cmWpuDeref(ask, nAsk);
}

/* The thread's step, from CmReenumService on the filter route, at most
 * every CM_WPU_PASS_MS. PASSIVE_LEVEL. */
static VOID cmWpuService(PCM_NHI nhi)
{
    PCM_REENUM r;
    PCM_FILTER held[CM_WPU_CAND];
    PCM_FILTER group[CM_WPU_CAND];
    ULONG port[CM_WPU_CAND];
    ULONG done[CM_WPU_CAND];
    ULONG lo[XHCI_TBT_MAX_TUNNELS];
    ULONG hi[XHCI_TBT_MAX_TUNNELS];
    PLIST_ENTRY e;
    PCM_FILTER g;
    KIRQL irql;
    ULONG now;
    ULONG epoch;
    ULONG members;
    ULONG n;
    ULONG m;
    ULONG i;
    ULONG j;
    ULONG v;
    BOOLEAN own;

    r = &nhi->Reenum;
    now = cmNow(nhi);
    if (r->WpuPassed && now - r->WpuLastPass < CM_WPU_PASS_MS) {
        return;
    }
    r->WpuPassed = 1;
    r->WpuLastPass = now;
    /* A request already outstanding finished and freed, whatever the
     * value; off, nothing else. */
    cmWpuLogSlots(nhi);
    if (cmWpuLoggedTimeouts != cmWpuSendTimeouts) {
        cmWpuLoggedTimeouts = cmWpuSendTimeouts;
        CmLogNote(nhi, "cm.wakepowerup.sendwait", cmWpuSendTimeouts);
    }
    if (nhi->Values.WakePowerUp == 0) {
        return;
    }
#if CM_WPU_R2_BUILT
    if (SharedUserData->NtMajorVersion >= 6) {
        cmWpuR2Start(nhi);
    } else if (cmWpuR2 == 0 && InterlockedIncrement(&cmWpuR2Claim) == 1) {
        cmWpuR2 = 3;
    }
    /* Quiet: no sleep or stop received since the last release, and that
     * release (if any) older than the window: R2 may open disks. A queue
     * the work item left (Deferred) is handed back then. */
    if (cmWpuDeferred && cmWpuR2 == 2 && cmWpuQuietNow()) {
        cmWpuKick(0);
    }
    if (cmWpuLookBegun != cmWpuLookEnded) {
        /* One lookup's open or property read has not returned: the queue
         * waits behind it (a delayed worker thread held); logged once. */
        if (!cmWpuStuckSeen || cmWpuStuckFrom != (ULONG)cmWpuLookBegun) {
            cmWpuStuckSeen = 1;
            cmWpuStuckFrom = (ULONG)cmWpuLookBegun;
            cmWpuStuckLogged = now;
        } else if (cmWpuStuckLogged != 0 &&
                   now - cmWpuStuckLogged > CM_WPU_STUCK_MS) {
            cmWpuStuckLogged = 0;
            CmLogNote(nhi, "cm.wakepowerup.r2.stuck", cmWpuStuckFrom);
        }
    } else {
        cmWpuStuckSeen = 0;
    }
    for (i = 0; i < 7; i++) {
        if (cmWpuLoggedWhy[i] != cmWpuWhy[i]) {
            cmWpuLoggedWhy[i] = cmWpuWhy[i];
            CmLogNote(nhi, "cm.wakepowerup.r2.why", (i << 24) | (cmWpuWhy[i] & 0xFFFFFFUL));
        }
    }
    if (cmWpuLoggedR2 != cmWpuR2 || cmWpuLoggedSeen != cmWpuSeen ||
        cmWpuLoggedMatched != cmWpuMatched) {
        cmWpuLoggedR2 = cmWpuR2;
        cmWpuLoggedSeen = cmWpuSeen;
        cmWpuLoggedMatched = cmWpuMatched;
        CmLogNote(nhi, "cm.wakepowerup.r2",
                  (cmWpuR2 << 24) | ((cmWpuMatched & 0xFFFUL) << 12) |
                      (cmWpuSeen & 0xFFFUL));
    }
#endif
    if (nhi->Suspended) {
        return;
    }
    epoch = CmWpuEpochNow();
    members = r->Members < XHCI_TBT_MAX_TUNNELS ? r->Members
                                                : XHCI_TBT_MAX_TUNNELS;
    for (i = 0; i < members; i++) {
        lo[i] = 0;
        hi[i] = 0;
        v = 0;
        KeAcquireSpinLock(&cmRegLock, &irql);
        own = (BOOLEAN)((r->Port[i].Flags & (CM_PORT_ACTIVE | CM_PORT_EXCLUSIVE)) ==
                        (CM_PORT_ACTIVE | CM_PORT_EXCLUSIVE));
        KeReleaseSpinLock(&cmRegLock, irql);
        if (!own) {
            /* Not this domain's alone: nothing behind it is looked at. */
            continue;
        }
        if (cmCfgRead(NULL, r->Port[i].Bdf, 0x18UL, &v) != 0 &&
            ((v >> 8) & 0xFFUL) != 0 &&
            ((v >> 16) & 0xFFUL) >= ((v >> 8) & 0xFFUL)) {
            lo[i] = (v >> 8) & 0xFFUL;
            hi[i] = (v >> 16) & 0xFFUL;
        }
    }
    n = 0;
    KeAcquireSpinLock(&cmRegLock, &irql);
    if (r->WpuClosed) {
        KeReleaseSpinLock(&cmRegLock, irql);
        return;
    }
    for (e = cmRegAtts.Flink; e != &cmRegAtts; e = e->Flink) {
        g = CONTAINING_RECORD(e, CM_FILTER, AttLink);
        if (g->Common.Kind != CM_KIND_CHILD || !g->AttListed ||
            !g->Located || !g->Started || g->ChainKind == CM_CHAIN_BRIDGE) {
            continue;
        }
        for (i = 0; i < members; i++) {
            if (lo[i] != 0 && g->LocBus >= lo[i] && g->LocBus <= hi[i] &&
                (r->Port[i].Flags & (CM_PORT_ACTIVE | CM_PORT_EXCLUSIVE)) ==
                    (CM_PORT_ACTIVE | CM_PORT_EXCLUSIVE)) {
                break;
            }
        }
        if (i >= members) {
            continue;
        }
        if (n >= CM_WPU_CAND) {
            cmWpuOver++;
            break;
        }
        if (CmFilterTryHold(g)) {
            held[n] = g;
            port[n] = i;
            done[n] = 0;
            n++;
        }
    }
    KeReleaseSpinLock(&cmRegLock, irql);
    if (cmWpuLoggedOver != cmWpuOver) {
        cmWpuLoggedOver = cmWpuOver;
        CmLogNote(nhi, "cm.wakepowerup.over", cmWpuOver);
    }
    for (i = 0; i < n; i++) {
        cmWpuOwnerRead(nhi, held[i]);
    }
    for (i = 0; i < n; i++) {
        if (done[i]) {
            continue;
        }
        m = 0;
        for (j = i; j < n; j++) {
            if (!done[j] && held[j]->Common.Pdo == held[i]->Common.Pdo) {
                group[m++] = held[j];
                done[j] = 1;
            }
        }
        cmWpuMember(nhi, group, m,
                    (BOOLEAN)(epoch != 0 &&
                              r->Port[port[i]].WpuRelEpoch == epoch),
                    r->Port[port[i]].WpuRelAt);
    }
    for (i = 0; i < n; i++) {
        CmFilterUnhold(held[i]);
    }
}
