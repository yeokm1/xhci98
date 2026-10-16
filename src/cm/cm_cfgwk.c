/*
 * cm_cfgwk.c - the Windows 98 SE and ME re-enumeration worker (design
 * record 21 sections 6.1 and 7.4; roadmap-usb4.md tasks 39-A.5 and 39-A.8).
 *
 * WHY A WORKER. A CONFIGMG service called while the configuration manager
 * is busy - an Add New Hardware wizard open on another devnode, for one -
 * does not return until it is free: read on 98 SE (2026-10-15, the
 * emulator's generic host with a "PCI Card" tunnelled before boot), where
 * the call made on the connection-manager thread held that thread for as
 * long as the wizard stayed open, ring 0 went unserviced, hot-plug events
 * overflowed and an unplug went unseen. So the connection-manager thread
 * never calls CONFIGMG: it posts requests here (CmCfgwkPost, collapsed per
 * member, the latest wins) and goes on servicing its ring; this thread
 * makes every CONFIGMG call of the domain, and only it blocks when
 * CONFIGMG does.
 *
 * WHAT IT DOES. At its start it opens the CONFIGMG route (cm_cfgmg.c's
 * thunks and guards) and records each member's children unmarked (the
 * start's baseline). Then, per request: a member's re-enumeration (its
 * vanished children query-removed and removed - a refusal respected, never
 * forced - then Reenumerate_DevNode), retried every CM_WK_RETRY_MS while
 * the bridge's devnode cannot be found or the call fails - with, before
 * that call, the bus numbers of any bridge the tunnel brought up below the
 * member in reset state (record 21 section 6.1.1, XhciCmBridgeNumbering;
 * tbt_brnum.c, written through PCI.VXD's _PCI_Write_Config once its guard
 * has passed); and a marking
 * window (CM_WK_MARK_MS) in which each new child of an active member gets
 * DN_REMOVABLE (Change_DevNode_Status); and CONTROLLER_UNAVAILABLE
 * (CmCfgwkUnavailable, the host interface lost: record 21 section 5.5),
 * which removes the children retained at a member's last healthy walk -
 * each revalidated by the instance id recorded then, and found through the
 * CONFIGMG tree alone, never by reading a bridge header the loss may have
 * reset - before any further re-enumeration of that member.
 *
 * LIFETIME. The worker's state is its own pool block, never the NHI's
 * extension. CmCfgwkStop detaches the NHI under the worker's lock; when
 * the worker is between calls the stop waits for it to leave and frees the
 * block; when it is inside a CONFIGMG call - which, during the NHI's own
 * removal, may not return until that removal has finished - the stop does
 * not wait (waiting there would deadlock): the worker frees its block and
 * closes its thunks itself when the call returns, making no further call
 * (but the removal that completes a query already accepted, and its
 * rescan of the bridge, cmWkRemoveChild).
 * Because such a worker may outlive its NHI for as long as CONFIGMG
 * blocks, a driver that has started one has no unload routine (the image
 * stays resident until the restart); CmCfgwkDrain remains for the unload
 * routine of a driver that never started one.
 *
 * IRQL: CmCfgwkStart, CmCfgwkStop and the worker at PASSIVE_LEVEL;
 * CmCfgwkPost and CmCfgwkOpenState at <= DISPATCH_LEVEL.
 */

#include "cm.h"
#include "cm_cfgmg.h"
#include "cm_ports.h"
#include "cm_nvlink.h"
#include "cm_qryb.h"
#include "tbt_brnum.h"

#if defined(_WIN64) && defined(NO_LEGACY_DRIVERS)
NTHALAPI ULONG HalGetBusData(BUS_DATA_TYPE BusDataType, ULONG BusNumber,
                             ULONG SlotNumber, PVOID Buffer, ULONG Length);
#endif

#define CM_WK_RETRY_MS      5000UL  /* a failed member call tried again   */
#define CM_WK_MARK_MS       10000UL /* a tunnel up's marking window       */
#define CM_WK_TICK_MS       200UL   /* the wait while a window or retry runs */
#define CM_WK_WALK_STACK    64UL
#define CM_WK_WALK_NODES    2048UL
#define CM_WK_BUS_FUNCTIONS 64UL
#define CM_WK_KNOWN         16UL
#define CM_WK_NO_OP         0xFFFFFFFFUL
#define CM_WK_GONE_TRIES    6UL     /* unresolved CONTROLLER_UNAVAILABLE
                                       attempts, CM_WK_RETRY_MS apart     */

/* Children of a member by devnode handle, instance id (cmWkIdHash) and the
 * member's publication generation when they were recorded (PubGen). */
typedef struct _CM_WK_IDS {
    ULONG Count;
    ULONG Node[CM_WK_KNOWN];
    ULONG Hash[CM_WK_KNOWN];
    ULONG Gen[CM_WK_KNOWN];
} CM_WK_IDS;

typedef struct _CM_WK_PORT {
    ULONG Bdf;
    ULONG Devnode;          /* the bridge's, 0 until found                */
    ULONG PathRead;
    ULONG PathCount;
    ULONG Path[CM_PORTS_PATH_MAX];
    ULONG WalkLogged;
    ULONG KnownCount;
    ULONG Known[CM_WK_KNOWN];
    CM_WK_IDS Ret;          /* the children retained at the last healthy
                               walk (cmWkRetain)                         */
    CM_WK_IDS Gone;         /* CONTROLLER_UNAVAILABLE's: Ret as it stood
                               when the worker took the request          */
    ULONG GoneTries;        /* unresolved attempts of the current one     */
    ULONG TakenSeq;         /* the CONTROLLER_UNAVAILABLE sequence taken  */
    ULONG PubGen;           /* bumped by every re-enumeration this worker
                               makes of the bridge: a devnode recorded
                               before one may have been replaced since   */
    ULONG HaveSaved;        /* the last path read while healthy, kept for
                               the lookups that read no configuration
                               space (cmWkNoCfg)                         */
    ULONG SavedCount;
    ULONG Saved[CM_PORTS_PATH_MAX];
} CM_WK_PORT;

typedef struct _CM_CFGWK {
    KSPIN_LOCK Lock;
    KEVENT Wake;            /* synchronization: a request was posted      */
    KEVENT Exited;          /* the worker left (owner frees the block)    */
    struct _CM_NHI *Nhi;    /* the log's owner; NULL once detached        */
    ULONG Exit;
    ULONG InCall;           /* inside a unit of CONFIGMG calls            */
    ULONG Detached;         /* the worker frees its own block             */
    ULONG Members;
    CM_WK_PORT Port[XHCI_TBT_MAX_TUNNELS];
    ULONG Active;           /* members the domain owns (bit per index)    */
    ULONG Gen[XHCI_TBT_MAX_TUNNELS]; /* each member's request generation:
                               bumped by every post naming it            */
    ULONG MarkWanted;       /* safe eject on: a retry's success marks too */
    ULONG OpMember;         /* the worker's operation in progress: its
                               member (CM_WK_NO_OP: none, the baseline)  */
    ULONG OpGen;            /* ... and the generation it was taken at     */
    ULONG ReqReenum;        /* members to re-enumerate                    */
    ULONG ReqVanish;        /* ... removing vanished children first       */
    ULONG ReqMark;          /* a marking window to open                   */
    ULONG PruneOwed;        /* members owing a complete walk forgetting
                               the children gone (bit per index)          */
    ULONG PruneSeq;         /* bumped by every CmCfgwkPrune               */
    ULONG PruneTried;       /* the worker's: PruneSeq at its last try     */
    ULONG PruneAt;          /* ... and the tick count then                */
    ULONG ReqGone;          /* members owing CONTROLLER_UNAVAILABLE's
                               removal (bit per index), cleared only when
                               it was resolved                           */
    ULONG GoneSeq[XHCI_TBT_MAX_TUNNELS]; /* each member's
                               CONTROLLER_UNAVAILABLE sequence: bumped by
                               every CmCfgwkUnavailable naming it        */
    ULONG GoneAt;           /* tick count of the last unresolved attempt  */
    ULONG NoScan;           /* members whose next re-enumeration reads no
                               configuration space (no vanished-child
                               scan): CONTROLLER_UNAVAILABLE's           */
    ULONG Retry;            /* member calls owed after a failure          */
    ULONG RetryVanish;
    ULONG RetryAt;          /* ms clock of the last failure               */
    ULONG MarkOpen;
    ULONG MarkFrom;
    ULONG OpenState;        /* CM_CFGWK_OPEN_*                            */
    ULONG Numbering;        /* XhciCmBridgeNumbering as the start read it */
    ULONG PciRefused;       /* PCI.VXD's guard failed: logged once        */
    ULONG NumberMember;     /* the member cmWkNumber works on ...         */
    ULONG NumberGen;        /* ... and its request's generation           */
    CM_CFGMG Cfgmg;
    CM_CFGWK_COUNTERS C;
} CM_CFGWK, *PCM_CFGWK;

/* Workers alive, for the driver's unload (CmCfgwkDrain). */
static LONG cmWkLive;

/* ---- the clock and configuration space -------------------------------- */

/* The clock: the tick count's low word, as cm_thr.c reads it (monotonic,
 * never the system time); intervals in ms from the tick period. */
static ULONG cmWkNowTicks(VOID)
{
    LARGE_INTEGER t;

    KeQueryTickCount(&t);
    return t.LowPart;
}

static ULONG cmWkSince(ULONG thenTicks)
{
    ULONG unit;

    /* KeQueryTimeIncrement is in 100 ns units. Milliseconds rounded down,
     * a tick's fraction kept (15.6 ms is not counted as 15), with no
     * product past 32 bits for any tick period of 10 to 55 ms and any
     * stall: cm_query.c's conversion, host-tested. */
    unit = KeQueryTimeIncrement();
    if (unit == 0) {
        unit = 10000UL;
    }
    return CmQueryAgeMs(cmWkNowTicks(), thenTicks, unit);
}

/* As cm_reenum.c's reader: HalGetBusData from offset 0. */
static ULONG cmWkCfgRead(ULONG bdf, ULONG offset, PULONG value)
{
    UCHAR buffer[256];
    ULONG need;
    ULONG got;

    if ((offset & 3UL) != 0 || offset > 252UL) {
        return 0;
    }
    need = offset + 4UL;
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

/* The NHI's log, while one is attached. <= DISPATCH_LEVEL. */
static VOID cmWkLog(PCM_CFGWK w, const char *label, ULONG value)
{
    KIRQL irql;

    KeAcquireSpinLock(&w->Lock, &irql);
    if (w->Nhi != NULL) {
        CmLogNote(w->Nhi, label, value);
    }
    KeReleaseSpinLock(&w->Lock, irql);
}

/* TRUE while member i's request is still the one taken (`gen`) and the
 * domain still owns the member: a newer post or a lost claim makes the
 * operation in progress stale. */
static BOOLEAN cmWkCurrent(PCM_CFGWK w, ULONG i, ULONG gen);

/* The member's generation now. */
static ULONG cmWkGenOf(PCM_CFGWK w, ULONG i)
{
    KIRQL irql;
    ULONG gen;

    KeAcquireSpinLock(&w->Lock, &irql);
    gen = w->Gen[i];
    KeReleaseSpinLock(&w->Lock, irql);
    return gen;
}

/* The operation the next CONFIGMG calls belong to (cmWkGo). */
static VOID cmWkOp(PCM_CFGWK w, ULONG i, ULONG gen)
{
    w->OpMember = i;
    w->OpGen = gen;
}

/*
 * Checked before every CONFIGMG call and before any result is kept: FALSE
 * once the stop asked the worker to leave or, inside a member's operation
 * (cmWkOp), once that operation went stale (cmWkCurrent). An operation in
 * progress when either came makes no further call after its current one
 * returns - but the Remove_SubTree that completes an accepted query, and
 * its rescan (cmWkRemoveChild).
 */
static BOOLEAN cmWkGo(PCM_CFGWK w)
{
    KIRQL irql;
    BOOLEAN go;

    if (w->OpMember != CM_WK_NO_OP) {
        return cmWkCurrent(w, w->OpMember, w->OpGen);
    }
    KeAcquireSpinLock(&w->Lock, &irql);
    go = (BOOLEAN)!w->Exit;
    KeReleaseSpinLock(&w->Lock, irql);
    return go;
}

static BOOLEAN cmWkCurrent(PCM_CFGWK w, ULONG i, ULONG gen)
{
    KIRQL irql;
    BOOLEAN ok;

    KeAcquireSpinLock(&w->Lock, &irql);
    ok = (BOOLEAN)(!w->Exit && w->Gen[i] == gen &&
                   (w->Active & (1UL << i)) != 0);
    KeReleaseSpinLock(&w->Lock, irql);
    return ok;
}

/* ---- the 98 devnode tree ---------------------------------------------- */

/*
 * The 98 path of the function at `bdf` (cm_ports.h CmPortsIdPath): its
 * devfn, then each parent bridge's up to the root bus, then the root bus
 * number. A parent is the type 1 function whose secondary bus is the
 * child's bus, found by reading every lower bus's functions; a bus with no
 * such parent is a root bus, but only after a scan that read every
 * function it met. FALSE where the path is not known.
 */
static BOOLEAN cmWkPciPath(ULONG bdf, PULONG path, PULONG count)
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
                    if (!cmWkCfgRead(XHCI_TBT_BDF(b, d, f), 0, &id)) {
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
                    if (!cmWkCfgRead(XHCI_TBT_BDF(b, d, f),
                                     XHCI_TBT_CFG_HEADER, &hdr)) {
                        incomplete = 1;
                        break;
                    }
                    if (XHCI_TBT_HDR_LAYOUT(hdr >> 16) ==
                            XHCI_TBT_HDR_BRIDGE &&
                        cmWkCfgRead(XHCI_TBT_BDF(b, d, f),
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
static BOOLEAN cmWkIdNames(const char *id, ULONG bdf, const ULONG *want,
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

/* TRUE while member i's lookups read no configuration space: its
 * CONTROLLER_UNAVAILABLE removal is owed, or the re-enumeration after it
 * is (read now, under the lock). */
static BOOLEAN cmWkNoCfg(PCM_CFGWK w, ULONG i)
{
    KIRQL irql;
    BOOLEAN no;

    KeAcquireSpinLock(&w->Lock, &irql);
    no = (BOOLEAN)(((w->ReqGone | w->NoScan) & (1UL << i)) != 0);
    KeReleaseSpinLock(&w->Lock, irql);
    return no;
}

/* The devnode of member i's bridge: the cached one while its id still
 * names that location, else a walk of the tree from the root. 0 for none.
 * In the no-configuration mode (cmWkNoCfg) the path is the one last read
 * while healthy, never read again. */
static ULONG cmWkFindDevnode(PCM_CFGWK w, ULONG i)
{
    CM_WK_PORT *p;
    ULONG stack[CM_WK_WALK_STACK];
    ULONG depth;
    ULONG nodes;
    ULONG node;
    ULONG next;
    ULONG dropped;
    ULONG cr;
    char id[CMG_MAX_DEVICE_ID_LEN + 1];

    p = &w->Port[i];
    if (!p->PathRead) {
        if (cmWkNoCfg(w, i)) {
            if (!p->HaveSaved) {
                return 0;
            }
            for (depth = 0; depth < p->SavedCount; depth++) {
                p->Path[depth] = p->Saved[depth];
            }
            p->PathCount = p->SavedCount;
            p->PathRead = 1;
        } else if (cmWkPciPath(p->Bdf, p->Path, &p->PathCount)) {
            p->PathRead = 1;
            for (depth = 0; depth < p->PathCount; depth++) {
                p->Saved[depth] = p->Path[depth];
            }
            p->SavedCount = p->PathCount;
            p->HaveSaved = 1;
        } else {
            p->PathCount = 0;
        }
    }
    if (!cmWkGo(w)) {
        return 0;
    }
    if (p->Devnode != 0 &&
        CmCfgmgDeviceId(&w->Cfgmg, p->Devnode, id, sizeof(id)) ==
            CMG_CR_SUCCESS &&
        cmWkIdNames(id, p->Bdf, p->Path, p->PathCount)) {
        return p->Devnode;
    }
    p->Devnode = 0;
    depth = 0;
    nodes = 0;
    dropped = 0;
    stack[depth++] = w->Cfgmg.Root;
    while (depth > 0 && nodes < CM_WK_WALK_NODES) {
        if (!cmWkGo(w)) {
            return 0;
        }
        node = stack[--depth];
        nodes++;
        cr = CmCfgmgDeviceId(&w->Cfgmg, node, id, sizeof(id));
        if (cr == CMG_CR_SUCCESS &&
            cmWkIdNames(id, p->Bdf, p->Path, p->PathCount)) {
            p->Devnode = node;
            return node;
        }
        if (!cmWkGo(w) ||
            CmCfgmgChild(&w->Cfgmg, node, &next) != CMG_CR_SUCCESS) {
            continue;
        }
        while (next != 0) {
            if (depth == CM_WK_WALK_STACK) {
                dropped++;
                break;
            }
            stack[depth++] = next;
            if (!cmWkGo(w)) {
                break;
            }
            if (CmCfgmgSibling(&w->Cfgmg, next, &next) != CMG_CR_SUCCESS) {
                break;
            }
        }
    }
    /* Nothing matched: the path is read again at the next walk. */
    p->PathRead = 0;
    if (!p->WalkLogged) {
        p->WalkLogged = 1;
        cmWkLog(w, "cm.reenum.9x.no-devnode", p->Bdf);
        cmWkLog(w, "cm.reenum.9x.walk.nodes", nodes);
        cmWkLog(w, "cm.reenum.9x.walk.dropped", dropped);
        cmWkLog(w, "cm.reenum.9x.walk.path-len", p->PathCount);
    }
    return 0;
}

/*
 * The children of `devnode`, up to `room`. *complete is TRUE only when the
 * list ended normally (no child, or no further sibling: CR_NO_SUCH_DEVNODE
 * or a zero handle) - never after an error, a stop, a stale operation or
 * a list cut at its room; only a complete list may stand for the set of
 * present children.
 */
static ULONG cmWkChildren(PCM_CFGWK w, ULONG devnode, PULONG out, ULONG room,
                          BOOLEAN *complete)
{
    ULONG n;
    ULONG node;
    ULONG cr;

    n = 0;
    *complete = FALSE;
    if (!cmWkGo(w)) {
        return 0;
    }
    cr = CmCfgmgChild(&w->Cfgmg, devnode, &node);
    if (cr == CMG_CR_NO_SUCH_DEVNODE) {
        *complete = TRUE;
        return 0;
    }
    if (cr != CMG_CR_SUCCESS) {
        return 0;
    }
    for (;;) {
        if (node == 0) {
            *complete = TRUE;
            return n;
        }
        if (n == room) {
            return n;
        }
        out[n++] = node;
        if (!cmWkGo(w)) {
            return n;
        }
        cr = CmCfgmgSibling(&w->Cfgmg, node, &node);
        if (cr == CMG_CR_NO_SUCH_DEVNODE) {
            *complete = TRUE;
            return n;
        }
        if (cr != CMG_CR_SUCCESS) {
            return n;
        }
    }
}

/* The ids answering on the bridge's secondary bus; room + 1 when the bus
 * could not be read completely (nothing is then taken for vanished). */
static ULONG cmWkBusIds(ULONG bridge, PULONG ids, ULONG room)
{
    ULONG buses;
    ULONG bus;
    ULONG dev;
    ULONG fn;
    ULONG id;
    ULONG hdr;
    ULONG n;

    if (!cmWkCfgRead(bridge, XHCI_TBT_CFG_BUSES, &buses) ||
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
            if (!cmWkCfgRead(XHCI_TBT_BDF(bus, dev, fn), 0, &id)) {
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
                if (!cmWkCfgRead(XHCI_TBT_BDF(bus, dev, 0),
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

static VOID cmWkForget(CM_WK_PORT *p, ULONG devnode)
{
    ULONG k;

    for (k = 0; k < p->KnownCount; k++) {
        if (p->Known[k] == devnode) {
            p->Known[k] = p->Known[p->KnownCount - 1];
            p->KnownCount--;
            return;
        }
    }
}

/* Drops entry k of a set. */
static VOID cmWkIdsDrop(CM_WK_IDS *s, ULONG k)
{
    s->Count--;
    s->Node[k] = s->Node[s->Count];
    s->Hash[k] = s->Hash[s->Count];
    s->Gen[k] = s->Gen[s->Count];
}

/* Adds an entry unless the handle is there already or the set is full. */
static VOID cmWkIdsAdd(CM_WK_IDS *s, ULONG node, ULONG hash, ULONG gen)
{
    ULONG k;

    for (k = 0; k < s->Count; k++) {
        if (s->Node[k] == node) {
            return;
        }
    }
    if (s->Count < CM_WK_KNOWN) {
        s->Node[s->Count] = node;
        s->Hash[s->Count] = hash;
        s->Gen[s->Count] = gen;
        s->Count++;
    }
}

/* Forgets `devnode` in a set: TRUE, with its entry, when it was there. */
static BOOLEAN cmWkIdsForget(CM_WK_IDS *s, ULONG devnode, PULONG hash,
                             PULONG gen)
{
    ULONG k;

    for (k = 0; k < s->Count; k++) {
        if (s->Node[k] == devnode) {
            *hash = s->Hash[k];
            *gen = s->Gen[k];
            cmWkIdsDrop(s, k);
            return TRUE;
        }
    }
    return FALSE;
}

/* Keeps in a set only the entries whose bit is set in `present`. */
static VOID cmWkIdsKeep(CM_WK_IDS *s, ULONG present)
{
    ULONG k;

    k = s->Count;
    while (k != 0) {
        k--;
        if ((present & (1UL << k)) == 0) {
            cmWkIdsDrop(s, k);
        }
    }
}

/* The bits of a set's entries found in children[0..count). */
static ULONG cmWkIdsPresent(const CM_WK_IDS *s, const ULONG *children,
                            ULONG count)
{
    ULONG k;
    ULONG c;
    ULONG mask;

    mask = 0;
    for (k = 0; k < s->Count; k++) {
        for (c = 0; c < count; c++) {
            if (children[c] == s->Node[k]) {
                mask |= 1UL << k;
            }
        }
    }
    return mask;
}

/* A child's entries in the retained and CONTROLLER_UNAVAILABLE sets,
 * taken out by cmWkRetire and put back by cmWkRestore. */
typedef struct _CM_WK_RETIRED {
    BOOLEAN InRet;
    BOOLEAN InGone;
    ULONG RetHash;
    ULONG RetGen;
    ULONG GoneHash;
    ULONG GoneGen;
} CM_WK_RETIRED;

/*
 * A child of member i about to be removed by another route (the ordinary
 * vanished-child removal): retired from the retained and the
 * CONTROLLER_UNAVAILABLE sets, so its handle, reused by a later devnode,
 * is never taken for it; put back (cmWkRestore) when the removal did not
 * happen.
 */
static VOID cmWkRetire(CM_WK_PORT *p, ULONG devnode, CM_WK_RETIRED *r)
{
    r->InRet = cmWkIdsForget(&p->Ret, devnode, &r->RetHash, &r->RetGen);
    r->InGone = cmWkIdsForget(&p->Gone, devnode, &r->GoneHash, &r->GoneGen);
}

static VOID cmWkRestore(CM_WK_PORT *p, ULONG devnode, const CM_WK_RETIRED *r)
{
    if (r->InRet) {
        cmWkIdsAdd(&p->Ret, devnode, r->RetHash, r->RetGen);
    }
    if (r->InGone) {
        cmWkIdsAdd(&p->Gone, devnode, r->GoneHash, r->GoneGen);
    }
}

/*
 * Member i's known children kept only while present (CmPortsKeepPresent):
 * every walk of a bridge's children prunes the set, so a child that left
 * by any removal - the user's included - is forgotten before a re-plug
 * can reuse its handle.
 */
static VOID cmWkKeepPresent(PCM_CFGWK w, ULONG i, const ULONG *children,
                            ULONG count)
{
    CM_WK_PORT *p;

    p = &w->Port[i];
    if (cmWkGo(w)) {
        p->KnownCount = CmPortsKeepPresent(p->Known, p->KnownCount,
                                           children, count);
    }
}

/*
 * A prune walk of member i alone (CmCfgwkPrune), outside any request's
 * operation (it only observes, so only a stop ends it). The whole sibling
 * chain is walked (up to CM_WK_WALK_NODES), noting which known handles
 * are present, so a bridge with more children than the known set holds
 * still completes. TRUE when the chain ended normally and the set was
 * pruned.
 */
static BOOLEAN cmWkPrune(PCM_CFGWK w, ULONG i)
{
    CM_WK_PORT *p;
    ULONG bridge;
    ULONG present[CM_WK_KNOWN];
    ULONG count;
    ULONG node;
    ULONG nodes;
    ULONG cr;
    ULONG k;
    ULONG retSeen;
    ULONG goneSeen;

    p = &w->Port[i];
    bridge = cmWkFindDevnode(w, i);
    if (bridge == 0 || !cmWkGo(w)) {
        return FALSE;
    }
    count = 0;
    nodes = 0;
    retSeen = 0;
    goneSeen = 0;
    cr = CmCfgmgChild(&w->Cfgmg, bridge, &node);
    if (cr == CMG_CR_NO_SUCH_DEVNODE) {
        node = 0;
    } else if (cr != CMG_CR_SUCCESS) {
        return FALSE;
    }
    while (node != 0) {
        if (++nodes > CM_WK_WALK_NODES) {
            return FALSE;
        }
        for (k = 0; k < p->KnownCount; k++) {
            if (p->Known[k] == node && count < CM_WK_KNOWN) {
                present[count++] = node;
                break;
            }
        }
        retSeen |= cmWkIdsPresent(&p->Ret, &node, 1);
        goneSeen |= cmWkIdsPresent(&p->Gone, &node, 1);
        if (!cmWkGo(w)) {
            return FALSE;
        }
        cr = CmCfgmgSibling(&w->Cfgmg, node, &node);
        if (cr == CMG_CR_NO_SUCH_DEVNODE) {
            break;
        }
        if (cr != CMG_CR_SUCCESS) {
            return FALSE;
        }
    }
    if (!cmWkGo(w)) {
        return FALSE;
    }
    cmWkKeepPresent(w, i, present, count);
    /* A complete walk proves the others gone. */
    cmWkIdsKeep(&p->Ret, retSeen);
    cmWkIdsKeep(&p->Gone, goneSeen);
    return TRUE;
}

/* TRUE while member i owes no prune walk (read now, under the lock): no
 * re-enumeration of the member is made before its debt is paid. */
static BOOLEAN cmWkPruneClear(PCM_CFGWK w, ULONG i)
{
    KIRQL irql;
    BOOLEAN clear;

    KeAcquireSpinLock(&w->Lock, &irql);
    clear = (BOOLEAN)((w->PruneOwed & (1UL << i)) == 0);
    KeReleaseSpinLock(&w->Lock, irql);
    return clear;
}

/* ---- CONTROLLER_UNAVAILABLE ------------------------------------------- */

#define CM_WK_GONE_DONE       0UL   /* resolved: every entry removed, gone,
                                       changed or refused                 */
#define CM_WK_GONE_STALE      1UL   /* the request went stale: again now  */
#define CM_WK_GONE_UNRESOLVED 2UL   /* an entry not decided: again later  */

/* An instance id's FNV-1a hash, over at most CMG_MAX_DEVICE_ID_LEN
 * characters. */
static ULONG cmWkIdHash(const char *id)
{
    ULONG h;
    ULONG n;

    h = 0x811C9DC5UL;
    for (n = 0; n < CMG_MAX_DEVICE_ID_LEN && id[n] != '\0'; n++) {
        h ^= (ULONG)(UCHAR)id[n];
        h *= 0x01000193UL;
    }
    return h;
}

/* TRUE while member i owes CONTROLLER_UNAVAILABLE's removal (read now,
 * under the lock). */
static BOOLEAN cmWkGoneOwed(PCM_CFGWK w, ULONG i)
{
    KIRQL irql;
    BOOLEAN owed;

    KeAcquireSpinLock(&w->Lock, &irql);
    owed = (BOOLEAN)((w->ReqGone & (1UL << i)) != 0);
    KeReleaseSpinLock(&w->Lock, irql);
    return owed;
}

/*
 * The children of member i's `bridge` retained, with their instance ids,
 * from a complete walk: what a later CONTROLLER_UNAVAILABLE removes. Never
 * while that removal is owed (the set was taken when the worker took the
 * request; nothing seen after it belongs to it), and only a complete list,
 * every id read, replaces the set.
 */
static VOID cmWkRetain(PCM_CFGWK w, ULONG i, ULONG bridge)
{
    CM_WK_PORT *p;
    ULONG children[CM_WK_KNOWN];
    ULONG hash[CM_WK_KNOWN];
    ULONG count;
    ULONG c;
    BOOLEAN complete;
    char text[CMG_MAX_DEVICE_ID_LEN + 1];

    p = &w->Port[i];
    if (bridge == 0 || cmWkGoneOwed(w, i)) {
        return;
    }
    count = cmWkChildren(w, bridge, children, CM_WK_KNOWN, &complete);
    if (!complete) {
        return;
    }
    for (c = 0; c < count; c++) {
        if (!cmWkGo(w) ||
            CmCfgmgDeviceId(&w->Cfgmg, children[c], text, sizeof(text)) !=
                CMG_CR_SUCCESS) {
            return;
        }
        text[CMG_MAX_DEVICE_ID_LEN] = '\0';
        hash[c] = cmWkIdHash(text);
    }
    if (!cmWkGo(w) || cmWkGoneOwed(w, i)) {
        return;
    }
    for (c = 0; c < count; c++) {
        p->Ret.Node[c] = children[c];
        p->Ret.Hash[c] = hash[c];
        p->Ret.Gen[c] = p->PubGen;
    }
    p->Ret.Count = count;
}

/*
 * The worker takes a CONTROLLER_UNAVAILABLE request for member i: the
 * retained set moves into the Gone set (merged with what an earlier one
 * left). From here the obligation is to those devnodes, as they were
 * recorded; cmWkRetire takes one off when another route removes it, a
 * complete walk proves it gone, and an entry recorded before a
 * re-enumeration this worker has made since (PubGen) is not acted on, so a
 * later devnode reusing its handle is never taken for it. CONFIGMG gives a
 * devnode no identity beyond its handle and instance id, so one route
 * remains: a re-enumeration made outside this driver (the user's Refresh in
 * Device Manager) that, while the request is owed, recreates a child the
 * user removed meanwhile under the same handle at the same location. The
 * removal then asks Query_Remove_SubTree of that devnode - refused if it
 * is in use - and the re-enumeration this request makes right after it
 * brings the device back: a remove and re-add of an idle device, never a
 * forced removal.
 */
static VOID cmWkGoneTake(CM_WK_PORT *p)
{
    ULONG k;

    for (k = 0; k < p->Ret.Count; k++) {
        cmWkIdsAdd(&p->Gone, p->Ret.Node[k], p->Ret.Hash[k], p->Ret.Gen[k]);
    }
    p->Ret.Count = 0;
    p->GoneTries = 0;
}

static BOOLEAN cmWkRemoveChild(PCM_CFGWK w, ULONG i, ULONG gen,
                               ULONG bridge, ULONG devnode);

/*
 * CONTROLLER_UNAVAILABLE on member i, request `gen`, over its Gone set:
 * each entry still a child of the bridge and still answering to its
 * recorded instance id is removed (cmWkRemoveChild: a refusal respected,
 * never forced); one whose id changed is left alone and counted; one a
 * complete list shows absent is forgotten. A removal that completed, a
 * refusal and a changed id each settle their entry (a refused one goes
 * back to the retained set: asked again only by a later request). An
 * entry whose id did not read, a list cut short, a bridge devnode not
 * found, or a Remove_SubTree that failed leave it undecided: UNRESOLVED,
 * tried again at CM_WK_RETRY_MS. The bridge's devnode is found from the
 * path read while the member was healthy; no configuration space is read.
 */
static ULONG cmWkGone(PCM_CFGWK w, ULONG i, ULONG gen)
{
    CM_WK_PORT *p;
    ULONG bridge;
    ULONG children[CM_WK_KNOWN];
    ULONG count;
    ULONG present;
    ULONG node;
    ULONG hash;
    ULONG vetoed;
    ULONG removed;
    ULONG calls;
    ULONG egen;
    ULONG k;
    ULONG c;
    BOOLEAN complete;
    BOOLEAN unresolved;
    BOOLEAN current;
    char text[CMG_MAX_DEVICE_ID_LEN + 1];

    p = &w->Port[i];
    cmWkOp(w, i, gen);
    if (p->Gone.Count == 0) {
        return CM_WK_GONE_DONE;
    }
    /* The path last read while healthy (cmWkNoCfg): no bridge is read. */
    bridge = cmWkFindDevnode(w, i);
    if (!cmWkCurrent(w, i, gen)) {
        return CM_WK_GONE_STALE;
    }
    if (bridge == 0) {
        cmWkLog(w, "cm.reenum.9x.gone.no-devnode", p->Bdf);
        return CM_WK_GONE_UNRESOLVED;
    }
    count = cmWkChildren(w, bridge, children, CM_WK_KNOWN, &complete);
    if (!cmWkCurrent(w, i, gen)) {
        return CM_WK_GONE_STALE;
    }
    present = cmWkIdsPresent(&p->Gone, children, count);
    unresolved = FALSE;
    if (complete) {
        cmWkIdsKeep(&p->Gone, present);
    } else {
        unresolved = TRUE;
    }
    /* The entries are visited from the end, so a dropped one (its slot
     * refilled from the end) is never skipped. */
    k = p->Gone.Count;
    while (k != 0) {
        k--;
        node = p->Gone.Node[k];
        hash = p->Gone.Hash[k];
        egen = p->Gone.Gen[k];
        if (egen != p->PubGen) {
            /* Recorded before a re-enumeration made since: the devnode
             * under that handle may be another. Not acted on. */
            cmWkIdsDrop(&p->Gone, k);
            w->C.GoneDropped++;
            cmWkLog(w, "cm.reenum.9x.gone.superseded", node);
            continue;
        }
        present = 0;
        for (c = 0; c < count; c++) {
            if (children[c] == node) {
                present = 1;
            }
        }
        if (!present) {
            /* Not in a list cut short: undecided (already counted). */
            continue;
        }
        if (!cmWkCurrent(w, i, gen)) {
            return CM_WK_GONE_STALE;
        }
        if (CmCfgmgDeviceId(&w->Cfgmg, node, text, sizeof(text)) !=
            CMG_CR_SUCCESS) {
            unresolved = TRUE;
            continue;
        }
        text[CMG_MAX_DEVICE_ID_LEN] = '\0';
        if (cmWkIdHash(text) != hash) {
            cmWkIdsDrop(&p->Gone, k);
            w->C.GoneDropped++;
            cmWkLog(w, "cm.reenum.9x.gone.id-changed", node);
            continue;
        }
        vetoed = w->C.Vetoed;
        removed = w->C.Removed;
        calls = w->C.Calls;
        current = cmWkRemoveChild(w, i, gen, bridge, node);
        if (w->C.Calls != calls) {
            /* The helper rescanned the bridge (stale after an accepted
             * query, whether or not the removal succeeded). */
            p->PubGen++;
        }
        if (w->C.Removed != removed) {
            cmWkIdsDrop(&p->Gone, k);
        } else if (w->C.Vetoed != vetoed) {
            /* Refused: settled for this request, kept retained. */
            cmWkIdsDrop(&p->Gone, k);
            cmWkIdsAdd(&p->Ret, node, hash, egen);
        } else if (current) {
            /* Accepted, but Remove_SubTree failed: undecided. */
            unresolved = TRUE;
        }
        if (!current) {
            return CM_WK_GONE_STALE;
        }
    }
    return unresolved ? CM_WK_GONE_UNRESOLVED : CM_WK_GONE_DONE;
}

/*
 * The single removal step of a vanished child of member i, on the worker:
 * Query_Remove_SubTree, and Remove_SubTree only when that was accepted (a
 * refusal - a driver with open files - is respected: nothing forced,
 * logged and counted). The request is checked to be current before the
 * query only: a query once accepted is always followed by its removal,
 * stale or not, even after a stop, because Windows 98 has no service that
 * cancels an accepted query and a driver left query-removed stays
 * quiesced. A removal so completed for a request gone stale meanwhile
 * (a newer request, a lost claim, a stop - after which another worker may
 * already have rescanned the bridge) is followed here by its own
 * Reenumerate_DevNode of `bridge`, so a device re-plugged meanwhile and
 * removed with the old child comes back whoever else has rescanned.
 * Returns TRUE while the request is current (the caller goes on), and
 * after a refused query whatever the request's state (nothing was changed;
 * the caller's own next check decides); FALSE when it went stale, before
 * the query (nothing done) or after a completed removal and its rescan:
 * the caller abandons the rest. Any
 * orderly-remove handshake with the child's driver wraps this function.
 */
static BOOLEAN cmWkRemoveChild(PCM_CFGWK w, ULONG i, ULONG gen,
                               ULONG bridge, ULONG devnode)
{
    ULONG cr;
    ULONG id;
    ULONG entry;
    ULONG seq;
    ULONG answer;
    ULONG start;
    ULONG k;
    ULONG n;
    ULONG cbus;
    ULONG cslot;
    ULONG cgen;
    ULONG egen;
    ULONG want[CM_PORTS_PATH_MAX];
    LARGE_INTEGER pause;
    char text[CMG_MAX_DEVICE_ID_LEN + 1];

    if (!cmWkCurrent(w, i, gen)) {
        return FALSE;
    }
    /* The child's ids, for the stor98 handshake below (a function still
     * present with an UP entry published for them). */
    id = 0;
    if (CmCfgmgDeviceId(&w->Cfgmg, devnode, text, sizeof(text)) !=
            CMG_CR_SUCCESS ||
        !CmPortsIdVenDev(text, sizeof(text), &id)) {
        id = 0;
    }
    if (!cmWkCurrent(w, i, gen)) {
        return FALSE;
    }
    /* The stor98 entry this very devnode names, established before the
     * query: an entry of this domain behind this bridge, published for the
     * devnode's ids, whose function's path - its devfn, then the bridge's
     * path - is the one the devnode's instance id spells. Not established
     * (no such entry, two candidates, the bridge's path unread): no
     * handshake, the removal goes on. */
    entry = CM_NVLINK_NONE;
    egen = 0;
    if (id != 0) {
        for (k = 0; k < STOR98LS_ENTRIES; k++) {
            if (!CmNvlinkWkCandidate(w->Nhi, w->Port[i].Bdf, id, k, &cbus,
                                     &cslot, &cgen)) {
                continue;
            }
            /* The path form needs the bridge's path; the BUS_&DEV_&FUNC_
             * form is matched by location alone (count 0). */
            n = 0;
            if (w->Port[i].PathRead && w->Port[i].PathCount != 0 &&
                w->Port[i].PathCount < CM_PORTS_PATH_MAX) {
                want[0] = ((cslot & 0x1FUL) << 3) | ((cslot >> 5) & 0x7UL);
                for (n = 0; n < w->Port[i].PathCount; n++) {
                    want[n + 1] = w->Port[i].Path[n];
                }
                n = w->Port[i].PathCount + 1;
            }
            if (cmWkIdNames(text, XHCI_TBT_BDF(cbus, cslot & 0x1FUL,
                                               (cslot >> 5) & 0x7UL),
                            want, n)) {
                if (entry != CM_NVLINK_NONE) {
                    entry = CM_NVLINK_NONE;
                    break;
                }
                entry = k;
                egen = cgen;
            }
        }
    }
    /* Stopped or superseded during the selection: no query begun. */
    if (!cmWkCurrent(w, i, gen)) {
        return FALSE;
    }
    /* A vanished child leaves the known set whatever follows: CONFIGMG
     * reuses a freed devnode's handle, so a re-plug's devnode can come
     * back under the same value and must be marked as new (section
     * 7.4). */
    cmWkForget(&w->Port[i], devnode);
    cr = CmCfgmgQueryRemove(&w->Cfgmg, devnode);
    cmWkLog(w, "cm.reenum.9x.query-remove", cr);
    if (cr != CMG_CR_SUCCESS) {
        /* The re-enumeration after it only asks PCI.VXD to read the bus
         * again. */
        w->C.Vetoed++;
        return TRUE;
    }
    /* stor98's orderly-removal handshake (MD-5; cm_nvlink.h), for a
     * function still present: REMOVE_PENDING, then its answer, waited for
     * here on the worker (no ring is serviced) and ended early when the
     * request goes stale. The removal below follows whatever the answer:
     * Windows 98 cannot cancel the query just accepted. */
    seq = 0;
    /* T_ack counts from before the request is published, never from a
     * later point a descheduled worker resumes at (R11). */
    start = cmWkNowTicks();
    if (entry != CM_NVLINK_NONE && cmWkCurrent(w, i, gen)) {
        /* Only for a request still current, and only for the identity
         * established before the query, unchanged since; a stale one
         * completes the accepted query's removal without asking stor98
         * anything. */
        entry = CmNvlinkWkRequest(w->Nhi, entry, egen, id, &seq);
    } else {
        entry = CM_NVLINK_NONE;
    }
    answer = CM_NVLINK_RM_NONE;
    if (entry != CM_NVLINK_NONE) {
        /* The deadline before each read: an answer first seen after it is
         * late, logged and not taken (interface 3, R11; CmNvlinkRemoveStep
         * answers CM_NVLINK_RM_TIMEOUT or CM_NVLINK_RM_LATE then). */
        while (cmWkCurrent(w, i, gen)) {
            answer = CmNvlinkWkAnswer(entry, seq, egen, cmWkSince(start));
            if (answer != CM_NVLINK_RM_NONE) {
                break;
            }
            /* -CM_NVLINK_POLL_MS ms, relative, in 100 ns units, without
             * 64-bit arithmetic. */
            pause.LowPart = (ULONG)(0UL - CM_NVLINK_POLL_MS * 10000UL);
            pause.HighPart = -1;
            (VOID)KeDelayExecutionThread(KernelMode, FALSE, &pause);
        }
        cmWkLog(w, "cm.nvlink.9x.remove-answer", (seq << 8) | answer);
    }
    /* Remove_SubTree; after a stor98 request, retried every
     * CM_NVLINK_RETRY_MS while it fails, until CM_NVLINK_RETRY_FOR_MS after
     * the first attempt (R11 O6's bound, CmNvlinkRetryAgain; a veto is
     * never forced). */
    start = cmWkNowTicks();
    for (;;) {
        cr = CmCfgmgRemove(&w->Cfgmg, devnode);
        cmWkLog(w, "cm.reenum.9x.remove", cr);
        if (cr == CMG_CR_SUCCESS || entry == CM_NVLINK_NONE ||
            !CmNvlinkRetryAgain(cmWkSince(start))) {
            break;
        }
        pause.LowPart = (ULONG)(0UL - CM_NVLINK_RETRY_MS * 10000UL);
        pause.HighPart = -1;
        (VOID)KeDelayExecutionThread(KernelMode, FALSE, &pause);
    }
    if (cr == CMG_CR_SUCCESS) {
        w->C.Removed++;
    }
    /* R11 O5: REMOVE_PENDING cleared after the removal. */
    if (entry != CM_NVLINK_NONE) {
        CmNvlinkWkWithdraw(entry, seq);
    }
    if (cmWkCurrent(w, i, gen)) {
        return TRUE;
    }
    /* Stale since the query: the rescan that the removal owes is made
     * here, not left to a request that may already have run. */
    cr = CmCfgmgReenumerate(&w->Cfgmg, bridge);
    w->C.Calls++;
    if (cr != CMG_CR_SUCCESS) {
        w->C.CallFailures++;
    }
    cmWkLog(w, "cm.reenum.9x.reenumerate", (w->Port[i].Bdf << 8) | cr);
    return FALSE;
}

/* tbt_brnum.c's accessors: PCI.VXD's own configuration-space services,
 * reads and dword writes alike, once their guard has passed (a function
 * PCI.VXD does not know yet is read the way PCI.VXD itself reads it). */
static ULONG cmWkIoRead(PVOID context, ULONG bdf, ULONG offset,
                        PULONG value)
{
    return CmCfgmgPciRead(&((PCM_CFGWK)context)->Cfgmg, bdf, offset, value)
               ? 1UL : 0UL;
}

static ULONG cmWkIoWrite(PVOID context, ULONG bdf, ULONG offset,
                         ULONG width, ULONG value)
{
    if (width != 4) {
        return 0;
    }
    return CmCfgmgPciWrite(&((PCM_CFGWK)context)->Cfgmg, bdf, offset, value)
               ? 1UL : 0UL;
}

/* The numbering's go callback: member NumberMember's request still the one
 * taken (NumberGen) and its configuration space still to be read (no
 * CONTROLLER_UNAVAILABLE since). */
static ULONG cmWkNumberGo(PVOID context)
{
    PCM_CFGWK w;

    w = (PCM_CFGWK)context;
    return (cmWkCurrent(w, w->NumberMember, w->NumberGen) &&
            !cmWkNoCfg(w, w->NumberMember))
               ? 1UL : 0UL;
}

#define CM_WK_NUMBER_TRIES   20UL   /* a chain not answering yet ...      */
#define CM_WK_NUMBER_WAIT_MS 100UL  /* ... read again this much later     */

/* HalGetBusData as the port reading's reader (the port is a function
 * PCI.VXD knows); no writer, so an SHPC port gives no reading. */
static ULONG cmWkHalRead(PVOID context, ULONG bdf, ULONG offset,
                         PULONG value)
{
    UNREFERENCED_PARAMETER(context);
    return cmWkCfgRead(bdf, offset, value);
}

/* The attempt, for XhciCmBridgeDiag (cm_statb.h), copied to the NHI under
 * the worker's lock while it is attached. */
static VOID cmWkDiag(PCM_CFGWK w, ULONG bdf, PXHCI_TBT_BRNUM r, ULONG tries,
                     ULONG flags)
{
    ULONG d[CM_BRDIAG_WORDS];
    ULONG k;
    KIRQL irql;
    KIRQL inner;

    for (k = 0; k < CM_BRDIAG_WORDS; k++) {
        d[k] = 0;
    }
    d[1] = bdf;
    if (r != NULL) {
        d[2] = r->Outcome;
        d[3] = (r->Functions << 16) | ((r->Bridges & 0xFFUL) << 8) |
               (tries & 0xFFUL);
        d[4] = r->PortBuses;
        d[5] = r->WinOutcome;
        d[6] = r->PortMem;
        d[7] = r->WinNeed;
        d[8] = r->Where != XHCI_TBT_NO_BDF ? r->Where : r->WinWhere;
        d[9] = (r->UndoFailed << 16) | (r->Undone & 0xFFFFUL);
        for (k = 0; k < 2; k++) {
            d[10 + k] = r->Bdf[k];
            d[12 + k] = r->Now[k];
            d[14 + k] = r->Win[k];
        }
    }
    d[16] = flags;
    d[17] = w->Cfgmg.PciGuard;
    if (r != NULL) {
        d[18] = (r->IrqOutcome & 0xFFUL) | ((r->IrqLine & 0xFFUL) << 8) |
                ((r->IrqPin & 0xFFUL) << 16);
        d[19] = (r->IrqFunctions & 0xFFUL) | ((r->IrqSet & 0xFFUL) << 8) |
                ((r->IrqKept & 0xFFUL) << 16) |
                ((r->IrqOther & 0xFFUL) << 24);
    }
    KeAcquireSpinLock(&w->Lock, &irql);
    if (w->Nhi != NULL) {
        /* The NHI's own lock inside the worker's: the thread's reader
         * takes it alone. The sequence is the NHI's, so a newer worker
         * of the same NHI never repeats one already written. */
        KeAcquireSpinLock(&w->Nhi->BrDiagLock, &inner);
        w->Nhi->BrDiagSeq++;
        if (w->Nhi->BrDiagSeq == 0) {
            w->Nhi->BrDiagSeq = 1;
        }
        d[0] = w->Nhi->BrDiagSeq;
        for (k = 0; k < CM_BRDIAG_WORDS; k++) {
            w->Nhi->BrDiag[k] = d[k];
        }
        KeReleaseSpinLock(&w->Nhi->BrDiagLock, inner);
    }
    KeReleaseSpinLock(&w->Lock, irql);
}

/*
 * Record 21 section 6.1.1 on member i (bridge devnode `bridge`), before
 * its re-enumeration: the bridges a tunnel brought up below it in reset
 * state (secondary and subordinate 0, which PCI.VXD leaves so: Code 29)
 * numbered inside its bus range and given memory windows inside its window,
 * as the BIOS sets them up at POST (left to itself, PCI.VXD placed a
 * hot-added bridge's window outside its parent's in the emulator). Only on
 * a healthy request still current, and only while the bridge's devnode
 * has no child: a subtree PCI.VXD still holds (a removal vetoed, a
 * re-plug before the removal ran) is never renumbered under it. A port
 * that reads up with nothing answering behind it yet, or a function
 * answering Retry Status, is read again for up to two seconds. Nothing
 * is written when no bridge is below it or any is numbered already
 * (present at boot), and a refusal leaves every bridge as it was read
 * (tbt_brnum.h).
 */
static VOID cmWkNumber(PCM_CFGWK w, ULONG i, ULONG gen, ULONG bridge)
{
    XHCI_TBT_CFG io;
    XHCI_TBT_CFG hal;
    XHCI_TBT_BRNUM r;
    XHCI_TBT_PORT port;
    LARGE_INTEGER pause;
    ULONG children[1];
    ULONG count;
    ULONG bdf;
    ULONG id;
    ULONG k;
    ULONG tries;
    ULONG flags;
    BOOLEAN complete;
    BOOLEAN up;

    bdf = w->Port[i].Bdf;
    if (!cmWkCurrent(w, i, gen) || cmWkNoCfg(w, i)) {
        return;
    }
    if (!w->Cfgmg.PciOpen) {
        if (w->PciRefused) {
            return;
        }
        if (!cmWkCfgRead(bdf, XHCI_TBT_CFG_ID, &id) ||
            !CmCfgmgPciOpen(&w->Cfgmg, bdf, id)) {
            w->PciRefused = 1;
            cmWkLog(w, "cm.brnum.9x.pci-refused", w->Cfgmg.PciGuard);
            cmWkDiag(w, bdf, NULL, 0, 2);
            return;
        }
        cmWkLog(w, "cm.brnum.9x.pci-open", id);
    }
    complete = FALSE;
    count = cmWkChildren(w, bridge, children, 1, &complete);
    if (!complete || count != 0) {
        cmWkLog(w, "cm.brnum.9x.held", (bdf << 8) | (complete ? 1UL : 0UL));
        cmWkDiag(w, bdf, NULL, 0, 1);
        return;
    }
    hal.Read = cmWkHalRead;
    hal.Write = NULL;
    hal.Context = NULL;
    hal.Reads = 0;
    hal.Writes = 0;
    up = (BOOLEAN)(XhciTbtPortRead(&hal, bdf, &port) == XHCI_TBT_PORT_UP);
    flags = up ? 4UL : 0UL;
    io.Read = cmWkIoRead;
    io.Write = cmWkIoWrite;
    io.Context = w;
    w->NumberMember = i;
    w->NumberGen = gen;
    for (tries = 1;; tries++) {
        io.Reads = 0;
        io.Writes = 0;
        /* Every attempt but the last also waits for an empty bus behind
         * a bridge (a link below still training); the last takes what
         * answers. */
        (VOID)XhciTbtNumberBridges(&io, bdf,
                                   XHCI_TBT_BRNUM_F_WINDOWS |
                                       XHCI_TBT_BRNUM_F_IRQ |
                                       (tries + 1UL < CM_WK_NUMBER_TRIES
                                            ? XHCI_TBT_BRNUM_F_WAIT_EMPTY
                                            : 0UL),
                                   cmWkNumberGo, w, &r);
        if (tries >= CM_WK_NUMBER_TRIES || !cmWkNumberGo(w)) {
            break;
        }
        if (r.Outcome != XHCI_TBT_BRNUM_NOT_READY &&
            !(r.Outcome == XHCI_TBT_BRNUM_NONE && r.Functions == 0 && up)) {
            break;
        }
        /* -CM_WK_NUMBER_WAIT_MS, relative, in 100 ns units, without
         * 64-bit arithmetic. */
        pause.LowPart = (ULONG)(0UL - CM_WK_NUMBER_WAIT_MS * 10000UL);
        pause.HighPart = -1;
        (VOID)KeDelayExecutionThread(KernelMode, FALSE, &pause);
    }
    cmWkDiag(w, bdf, &r, tries, flags);
    cmWkLog(w, "cm.brnum.9x.outcome", (bdf << 8) | r.Outcome);
    cmWkLog(w, "cm.brnum.9x.seen",
            (r.Functions << 16) | (r.Bridges << 8) | tries);
    if (r.Outcome == XHCI_TBT_BRNUM_NONE) {
        return;
    }
    cmWkLog(w, "cm.brnum.9x.port-buses", r.PortBuses);
    if (r.Outcome == XHCI_TBT_BRNUM_DONE) {
        w->C.Numbered += r.Numbered;
        for (k = 0; k < r.Numbered; k++) {
            cmWkLog(w, "cm.brnum.9x.bridge", r.Bdf[k]);
            cmWkLog(w, "cm.brnum.9x.buses", r.Now[k]);
            cmWkLog(w, "cm.brnum.9x.window", r.Win[k]);
        }
        cmWkLog(w, "cm.brnum.9x.win-outcome", r.WinOutcome);
        cmWkLog(w, "cm.brnum.9x.port-window", r.PortMem);
        cmWkLog(w, "cm.brnum.9x.win-need", r.WinNeed);
        cmWkLog(w, "cm.brnum.9x.irq",
                (r.IrqOutcome & 0xFFUL) | ((r.IrqLine & 0xFFUL) << 8) |
                    ((r.IrqPin & 0xFFUL) << 16));
        cmWkLog(w, "cm.brnum.9x.irq-counts",
                (r.IrqFunctions & 0xFFUL) | ((r.IrqSet & 0xFFUL) << 8) |
                    ((r.IrqKept & 0xFFUL) << 16) |
                    ((r.IrqOther & 0xFFUL) << 24));
        if (r.WinOutcome != XHCI_TBT_BRWIN_DONE) {
            w->C.NumberRefused++;
            cmWkLog(w, "cm.brnum.9x.win-where", r.WinWhere);
            cmWkLog(w, "cm.brnum.9x.undone", (r.UndoFailed << 16) | r.Undone);
        }
    } else if (r.Outcome != XHCI_TBT_BRNUM_NUMBERED) {
        w->C.NumberRefused++;
        cmWkLog(w, "cm.brnum.9x.where", r.Where);
        cmWkLog(w, "cm.brnum.9x.undone", (r.UndoFailed << 16) | r.Undone);
    }
}

/*
 * Section 6.1 on member i: the vanished children removed
 * (cmWkRemoveChild: a refusal respected, never forced), then
 * Reenumerate_DevNode. TRUE when the devnode was found and the
 * re-enumeration succeeded, or the request went stale meanwhile (a newer
 * one redoes it). Before each call the request is checked to be current
 * (cmWkCurrent): a stop, a newer post for the member or a lost claim ends
 * it, and no removal is begun on an inventory a reconnection may have
 * outdated - the one exception being the Remove_SubTree, and its rescan,
 * that complete a query already accepted (cmWkRemoveChild).
 */
static BOOLEAN cmWkMember(PCM_CFGWK w, ULONG i, BOOLEAN removeVanished,
                          ULONG gen)
{
    CM_WK_PORT *p;
    ULONG bridge;
    ULONG children[CM_WK_KNOWN];
    ULONG ids[CM_WK_BUS_FUNCTIONS];
    ULONG count;
    ULONG idCount;
    ULONG c;
    ULONG k;
    ULONG id;
    ULONG answers;
    ULONG cr;
    ULONG removed;
    ULONG calls;
    BOOLEAN complete;
    BOOLEAN current;
    CM_WK_RETIRED retired;
    char text[CMG_MAX_DEVICE_ID_LEN + 1];

    p = &w->Port[i];
    cmWkOp(w, i, gen);
    bridge = cmWkFindDevnode(w, i);
    if (!cmWkCurrent(w, i, gen)) {
        return TRUE;
    }
    if (bridge == 0) {
        w->C.CallFailures++;
        return FALSE;
    }
    if (removeVanished) {
        idCount = cmWkBusIds(p->Bdf, ids, CM_WK_BUS_FUNCTIONS);
        complete = FALSE;
        count = idCount <= CM_WK_BUS_FUNCTIONS
                    ? cmWkChildren(w, bridge, children, CM_WK_KNOWN,
                                   &complete)
                    : 0;
        if (complete) {
            cmWkKeepPresent(w, i, children, count);
            cmWkIdsKeep(&p->Ret, cmWkIdsPresent(&p->Ret, children, count));
            cmWkIdsKeep(&p->Gone,
                        cmWkIdsPresent(&p->Gone, children, count));
        }
        for (c = 0; c < count; c++) {
            if (!cmWkCurrent(w, i, gen)) {
                return TRUE;
            }
            if (CmCfgmgDeviceId(&w->Cfgmg, children[c], text,
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
            /* Removed by this route, not CONTROLLER_UNAVAILABLE's: its
             * entries back only if the removal did not happen. */
            cmWkRetire(p, children[c], &retired);
            removed = w->C.Removed;
            calls = w->C.Calls;
            current = cmWkRemoveChild(w, i, gen, bridge, children[c]);
            if (w->C.Calls != calls) {
                /* The helper rescanned the bridge. */
                p->PubGen++;
            }
            if (w->C.Removed == removed) {
                cmWkRestore(p, children[c], &retired);
            }
            if (!current) {
                return TRUE;
            }
        }
    }
    if (!cmWkCurrent(w, i, gen)) {
        return TRUE;
    }
    if (!cmWkPruneClear(w, i)) {
        /* A prune posted during the operation: paid first (failed,
         * retried). */
        return FALSE;
    }
    if (w->Numbering) {
        cmWkNumber(w, i, gen, bridge);
        if (!cmWkCurrent(w, i, gen)) {
            /* Superseded while it numbered: the newer request redoes the
             * re-enumeration. */
            return TRUE;
        }
    }
    cr = CmCfgmgReenumerate(&w->Cfgmg, bridge);
    p->PubGen++;
    w->C.Calls++;
    if (cr != CMG_CR_SUCCESS) {
        w->C.CallFailures++;
    }
    cmWkLog(w, "cm.reenum.9x.reenumerate", (p->Bdf << 8) | cr);
    if (cr == CMG_CR_SUCCESS && cmWkCurrent(w, i, gen)) {
        /* What the re-enumeration left under the bridge, retained for a
         * later CONTROLLER_UNAVAILABLE. */
        cmWkRetain(w, i, bridge);
    }
    return (BOOLEAN)(cr == CMG_CR_SUCCESS);
}

/* Safe eject's 9x half on member i: each child not seen before gets
 * DN_REMOVABLE; `mark` FALSE only records the children (the baseline,
 * outside any member's operation). Inside one (cmWkOp), a stale operation
 * makes no further call and keeps no result. */
static VOID cmWkMark(PCM_CFGWK w, ULONG i, BOOLEAN mark)
{
    CM_WK_PORT *p;
    ULONG bridge;
    ULONG children[CM_WK_KNOWN];
    ULONG count;
    ULONG c;
    ULONG k;
    ULONG known;
    ULONG status;
    ULONG problem;
    ULONG cr;
    BOOLEAN complete;

    p = &w->Port[i];
    bridge = cmWkFindDevnode(w, i);
    if (bridge == 0) {
        return;
    }
    count = cmWkChildren(w, bridge, children, CM_WK_KNOWN, &complete);
    if (!cmWkGo(w)) {
        return;
    }
    for (c = 0; c < count && mark; c++) {
        if (!cmWkGo(w)) {
            return;
        }
        known = 0;
        for (k = 0; k < p->KnownCount; k++) {
            if (p->Known[k] == children[c]) {
                known = 1;
            }
        }
        if (known) {
            continue;
        }
        cr = CmCfgmgStatus(&w->Cfgmg, children[c], &status, &problem);
        if (cr == CMG_CR_SUCCESS && (status & CMG_DN_REMOVABLE) != 0) {
            continue;
        }
        if (!cmWkGo(w)) {
            return;
        }
        cr = CmCfgmgAddRemovable(&w->Cfgmg, children[c]);
        cmWkLog(w, "cm.reenum.9x.removable", cr);
        if (cr == CMG_CR_SUCCESS) {
            w->C.Marked++;
        } else {
            w->C.MarkFailures++;
        }
    }
    if (!complete || !cmWkGo(w)) {
        /* Only a complete list replaces the set: a partial one would
         * forget children still present (each marked one reads removable
         * at the next walk all the same). */
        return;
    }
    for (c = 0; c < count; c++) {
        p->Known[c] = children[c];
    }
    p->KnownCount = count;
}

/* ---- the thread ------------------------------------------------------- */

/* Enters a unit of CONFIGMG calls: FALSE once the stop asked it to leave. */
static BOOLEAN cmWkEnter(PCM_CFGWK w)
{
    KIRQL irql;
    BOOLEAN go;

    KeAcquireSpinLock(&w->Lock, &irql);
    go = (BOOLEAN)!w->Exit;
    if (go) {
        w->InCall = 1;
    }
    KeReleaseSpinLock(&w->Lock, irql);
    return go;
}

static VOID cmWkLeave(PCM_CFGWK w)
{
    KIRQL irql;

    KeAcquireSpinLock(&w->Lock, &irql);
    w->InCall = 0;
    KeReleaseSpinLock(&w->Lock, irql);
}

/*
 * The opening: CmCfgmgOpen's read-only services (Get_Version, the root's
 * Locate_DevNode, its Get_Device_ID) run as one unit, with no check
 * between them - an exception to "no further call after a stop", with the
 * removal completing an accepted query (cmWkRemoveChild): they change
 * nothing, and the image stays resident (CmCfgwkStart). The
 * baseline after them is checked before each call as every operation is.
 */
static VOID cmWkOpen(PCM_CFGWK w)
{
    ULONG state;
    ULONG i;
    KIRQL irql;

    state = CmCfgmgOpen(&w->Cfgmg) ? CM_CFGWK_OPEN_OK : CM_CFGWK_OPEN_FAILED;
    cmWkLog(w, "cm.reenum.9x.version", w->Cfgmg.Version);
    if (state == CM_CFGWK_OPEN_OK) {
        /* The baseline: children present now are not marked. */
        for (i = 0; i < w->Members; i++) {
            if (w->Active & (1UL << i)) {
                cmWkMark(w, i, FALSE);
                cmWkRetain(w, i, w->Port[i].Devnode);
            }
        }
    }
    KeAcquireSpinLock(&w->Lock, &irql);
    w->OpenState = state;
    KeReleaseSpinLock(&w->Lock, irql);
}

static VOID cmWkThread(PVOID context)
{
    PCM_CFGWK w;
    KIRQL irql;
    ULONG reenum;
    ULONG vanish;
    ULONG failed;
    ULONG failedVanish;
    ULONG gen[XHCI_TBT_MAX_TUNNELS];
    ULONG retried;
    ULONG done;
    ULONG prune;
    ULONG pruneSeq;
    ULONG gone;
    ULONG take;
    ULONG noScan;
    ULONG goneRc;
    ULONG seq[XHCI_TBT_MAX_TUNNELS];
    ULONG i;
    BOOLEAN open;
    BOOLEAN go;
    BOOLEAN timed;
    BOOLEAN detached;
    LARGE_INTEGER tick;

    w = (PCM_CFGWK)context;
    for (;;) {
        KeAcquireSpinLock(&w->Lock, &irql);
        if (w->Exit) {
            KeReleaseSpinLock(&w->Lock, irql);
            break;
        }
        open = (BOOLEAN)(w->OpenState == CM_CFGWK_OPEN_PENDING);
        reenum = w->ReqReenum;
        vanish = w->ReqVanish;
        w->ReqReenum = 0;
        w->ReqVanish = 0;
        retried = 0;
        if (w->Retry != 0 && cmWkSince(w->RetryAt) >= CM_WK_RETRY_MS) {
            retried = w->Retry & ~reenum;
            reenum |= w->Retry;
            vanish |= w->RetryVanish;
            w->Retry = 0;
            w->RetryVanish = 0;
        }
        for (i = 0; i < XHCI_TBT_MAX_TUNNELS; i++) {
            gen[i] = w->Gen[i];
        }
        if (w->ReqMark) {
            w->ReqMark = 0;
            w->MarkOpen = 1;
            w->MarkFrom = cmWkNowTicks();
        }
        /* Members no longer owned owe no removal. A request newer than
         * the one the worker took (its sequence) is taken again: the
         * retained set merged into the owed one. */
        w->ReqGone &= w->Active;
        take = 0;
        for (i = 0; i < w->Members; i++) {
            seq[i] = w->GoneSeq[i];
            if ((w->ReqGone & (1UL << i)) != 0 &&
                w->Port[i].TakenSeq != seq[i]) {
                take |= 1UL << i;
            }
        }
        gone = w->ReqGone;
        if ((gone & ~take) != 0 && cmWkSince(w->GoneAt) < CM_WK_RETRY_MS) {
            /* An unresolved removal is tried again at the retry
             * interval; one just taken at once. */
            gone = take;
        }
        noScan = w->NoScan;
        vanish &= ~noScan;
        prune = w->PruneOwed;
        pruneSeq = w->PruneSeq;
        if (prune != 0 && pruneSeq == w->PruneTried &&
            cmWkSince(w->PruneAt) < CM_WK_RETRY_MS) {
            /* A walk that failed is tried again at the retry interval,
             * or at once for a newer prune. */
            prune = 0;
        }
        if (prune != 0) {
            w->PruneTried = pruneSeq;
            w->PruneAt = cmWkNowTicks();
        }
        w->InCall = 1;
        KeReleaseSpinLock(&w->Lock, irql);

        cmWkOp(w, CM_WK_NO_OP, 0);
        if (open) {
            cmWkOpen(w);
        }
        failed = 0;
        failedVanish = 0;
        done = 0;
        go = TRUE;
        if (w->OpenState == CM_CFGWK_OPEN_OK && prune != 0) {
            /* The prune debt is paid first, before any re-enumeration of
             * this pass can bring a child back under a freed handle; it
             * is cleared only by a complete walk, and only if no newer
             * prune came meanwhile. */
            for (i = 0; i < w->Members && go; i++) {
                if ((prune & (1UL << i)) == 0) {
                    continue;
                }
                cmWkLeave(w);
                go = cmWkEnter(w);
                if (go && cmWkPrune(w, i)) {
                    /* Paid, unless a newer prune came during the walk. */
                    KeAcquireSpinLock(&w->Lock, &irql);
                    if (w->PruneSeq == pruneSeq) {
                        w->PruneOwed &= ~(1UL << i);
                    }
                    KeReleaseSpinLock(&w->Lock, irql);
                }
            }
        }
        for (i = 0; i < w->Members; i++) {
            if (take & (1UL << i)) {
                /* The retained set as it stands now is the request's. */
                cmWkGoneTake(&w->Port[i]);
                w->Port[i].TakenSeq = seq[i];
            }
        }
        if (go && w->OpenState == CM_CFGWK_OPEN_OK && gone != 0) {
            /* CONTROLLER_UNAVAILABLE's removals before any re-enumeration
             * of the same members (the lost connection's children before
             * any later one's work); the obligation cleared only when
             * resolved, or after CM_WK_GONE_TRIES unresolved attempts
             * (the undecided entries then go back to the retained set,
             * for a later request). */
            for (i = 0; i < w->Members && go; i++) {
                if ((gone & (1UL << i)) == 0) {
                    continue;
                }
                cmWkLeave(w);
                go = cmWkEnter(w);
                if (!go) {
                    break;
                }
                goneRc = cmWkGone(w, i, gen[i]);
                if (goneRc == CM_WK_GONE_STALE) {
                    continue;
                }
                if (goneRc == CM_WK_GONE_UNRESOLVED &&
                    ++w->Port[i].GoneTries < CM_WK_GONE_TRIES) {
                    KeAcquireSpinLock(&w->Lock, &irql);
                    w->GoneAt = cmWkNowTicks();
                    KeReleaseSpinLock(&w->Lock, irql);
                    continue;
                }
                if (goneRc == CM_WK_GONE_DONE) {
                    w->C.GoneDone++;
                } else {
                    cmWkLog(w, "cm.reenum.9x.gone.abandoned",
                            (w->Port[i].Bdf << 8) | w->Port[i].Gone.Count);
                    /* Undecided entries back to the retained set (the
                     * refused ones are there already). */
                    cmWkGoneTake(&w->Port[i]);
                    w->Port[i].Ret = w->Port[i].Gone;
                }
                w->Port[i].Gone.Count = 0;
                KeAcquireSpinLock(&w->Lock, &irql);
                if (w->GoneSeq[i] == seq[i]) {
                    /* Only the request this attempt took; a newer one is
                     * taken at the next pass. */
                    w->ReqGone &= ~(1UL << i);
                }
                KeReleaseSpinLock(&w->Lock, irql);
            }
            cmWkOp(w, CM_WK_NO_OP, 0);
        }
        if (go && w->OpenState == CM_CFGWK_OPEN_OK) {
            for (i = 0; i < w->Members; i++) {
                if ((reenum & (1UL << i)) == 0 ||
                    !cmWkCurrent(w, i, gen[i])) {
                    continue;
                }
                if (cmWkGoneOwed(w, i)) {
                    /* Its CONTROLLER_UNAVAILABLE removal is still owed:
                     * no re-enumeration before it, tried again later. */
                    failed |= 1UL << i;
                    failedVanish |= vanish & (1UL << i);
                    continue;
                }
                if (!cmWkPruneClear(w, i)) {
                    /* Its prune walk is still owed (read now, a newer
                     * prune included): no re-enumeration before it,
                     * tried again later. */
                    failed |= 1UL << i;
                    failedVanish |= vanish & (1UL << i);
                    continue;
                }
                /* Between members the stop may take its turn. */
                cmWkLeave(w);
                if (!cmWkEnter(w)) {
                    go = FALSE;
                    break;
                }
                if (!cmWkMember(w, i, (BOOLEAN)((vanish >> i) & 1UL),
                                gen[i])) {
                    failed |= 1UL << i;
                    failedVanish |= vanish & (1UL << i);
                } else if (cmWkCurrent(w, i, gen[i])) {
                    /* Done, not merely gone stale. */
                    done |= 1UL << i;
                }
            }
            if (go && w->MarkOpen) {
                cmWkLeave(w);
                go = cmWkEnter(w);
            }
            if (go && w->MarkOpen) {
                for (i = 0; i < w->Members; i++) {
                    /* Ownership read again per member: a claim lost since
                     * the request ends its marking. */
                    cmWkOp(w, i, cmWkGenOf(w, i));
                    if (cmWkGo(w)) {
                        cmWkMark(w, i, TRUE);
                        cmWkRetain(w, i, w->Port[i].Devnode);
                    }
                }
                if (cmWkSince(w->MarkFrom) >= CM_WK_MARK_MS) {
                    w->MarkOpen = 0;
                }
            }
        }

        KeAcquireSpinLock(&w->Lock, &irql);
        w->InCall = 0;
        for (i = 0; i < w->Members; i++) {
            /* A failure is owed only while its request is still the
             * member's latest: a newer post redoes it anyway. */
            if ((failed & (1UL << i)) != 0 && w->Gen[i] == gen[i]) {
                w->Retry |= 1UL << i;
                w->RetryVanish |= failedVanish & (1UL << i);
                w->RetryAt = cmWkNowTicks();
            }
        }
        for (i = 0; i < w->Members; i++) {
            if ((done & (1UL << i)) != 0 && w->Gen[i] == gen[i]) {
                w->NoScan &= ~(1UL << i);
            }
        }
        if ((done & retried) != 0 && w->MarkWanted) {
            /* An owed re-enumeration came late: so does the marking of
             * what it brings. */
            w->MarkOpen = 1;
            w->MarkFrom = cmWkNowTicks();
        }
        timed = (BOOLEAN)(w->Retry != 0 || w->MarkOpen || w->PruneOwed != 0 ||
                          w->ReqGone != 0);
        if (w->Exit) {
            KeReleaseSpinLock(&w->Lock, irql);
            break;
        }
        KeReleaseSpinLock(&w->Lock, irql);

        if (timed) {
            /* CM_WK_TICK_MS relative, in 100 ns units: -2,000,000. */
            tick.LowPart = 0xFFE17B80UL;
            tick.HighPart = -1;
            (VOID)KeWaitForSingleObject(&w->Wake, Executive, KernelMode,
                                        FALSE, &tick);
        } else {
            (VOID)KeWaitForSingleObject(&w->Wake, Executive, KernelMode,
                                        FALSE, NULL);
        }
    }

    KeAcquireSpinLock(&w->Lock, &irql);
    detached = (BOOLEAN)(w->Detached != 0);
    KeReleaseSpinLock(&w->Lock, irql);
    if (detached) {
        /* The stop did not wait: the block is this thread's to free. */
        CmCfgmgClose(&w->Cfgmg);
        CmFree(w);
        (VOID)InterlockedDecrement(&cmWkLive);
    } else {
        (VOID)InterlockedDecrement(&cmWkLive);
        KeSetEvent(&w->Exited, IO_NO_INCREMENT, FALSE);
    }
    (VOID)PsTerminateSystemThread(STATUS_SUCCESS);
}

/* ---- the calls -------------------------------------------------------- */

PCM_CFGWK CmCfgwkStart(struct _CM_NHI *nhi, const ULONG *bdf, ULONG members,
                       ULONG active, ULONG numbering)
{
    PCM_CFGWK w;
    HANDLE handle;
    NTSTATUS status;
    ULONG i;

    w = (PCM_CFGWK)CmAlloc(sizeof(CM_CFGWK));
    if (w == NULL) {
        return NULL;
    }
    KeInitializeSpinLock(&w->Lock);
    KeInitializeEvent(&w->Wake, SynchronizationEvent, FALSE);
    KeInitializeEvent(&w->Exited, NotificationEvent, FALSE);
    w->Nhi = nhi;
    w->Members = members > XHCI_TBT_MAX_TUNNELS ? XHCI_TBT_MAX_TUNNELS
                                                : members;
    for (i = 0; i < w->Members; i++) {
        w->Port[i].Bdf = bdf[i];
    }
    w->Active = active;
    w->Numbering = numbering;
    w->OpenState = CM_CFGWK_OPEN_PENDING;
    w->OpMember = CM_WK_NO_OP;
    /* A worker the stop cannot wait for (inside a CONFIGMG call) runs this
     * image's code after its NHI is gone, for as long as CONFIGMG blocks:
     * no timeout makes an unload safe then, so a driver that has run one
     * is not unloaded at all (no DriverUnload: the image stays resident
     * until the restart). Windows 98 SE and ME only. */
    CmDriverObject->DriverUnload = NULL;
    (VOID)InterlockedIncrement(&cmWkLive);
    status = PsCreateSystemThread(&handle, THREAD_ALL_ACCESS, NULL, NULL,
                                  NULL, cmWkThread, w);
    if (!NT_SUCCESS(status)) {
        (VOID)InterlockedDecrement(&cmWkLive);
        CmFree(w);
        return NULL;
    }
    (VOID)ZwClose(handle);
    return w;
}

VOID CmCfgwkPost(PCM_CFGWK w, ULONG reenum, ULONG vanish, ULONG active,
                 BOOLEAN mark)
{
    KIRQL irql;
    ULONG i;

    KeAcquireSpinLock(&w->Lock, &irql);
    w->ReqReenum |= reenum;
    w->ReqVanish |= vanish;
    /* A newer request naming a member is a healthy one: its scan is read
     * again. */
    w->NoScan &= ~reenum;
    /* A newer request for a member supersedes its owed retry and any
     * operation on it in progress (its generation). */
    w->Retry &= ~reenum;
    w->RetryVanish &= ~reenum;
    for (i = 0; i < w->Members; i++) {
        if (reenum & (1UL << i)) {
            w->Gen[i]++;
        }
    }
    w->Active = active;
    if (mark) {
        w->ReqMark = 1;
        w->MarkWanted = 1;
    }
    KeSetEvent(&w->Wake, IO_NO_INCREMENT, FALSE);
    KeReleaseSpinLock(&w->Lock, irql);
}

VOID CmCfgwkUnavailable(PCM_CFGWK w, ULONG active)
{
    KIRQL irql;
    ULONG i;

    KeAcquireSpinLock(&w->Lock, &irql);
    w->Active = active;
    w->ReqGone |= active;
    for (i = 0; i < w->Members; i++) {
        if (active & (1UL << i)) {
            w->GoneSeq[i]++;
        }
    }
    /* The re-enumeration after the removal reads no configuration space:
     * no vanished-child scan, here or in a coalesced earlier request. */
    w->ReqReenum |= active;
    w->ReqVanish &= ~active;
    w->NoScan |= active;
    w->Retry &= ~active;
    w->RetryVanish &= ~active;
    for (i = 0; i < w->Members; i++) {
        if (active & (1UL << i)) {
            w->Gen[i]++;
        }
    }
    KeSetEvent(&w->Wake, IO_NO_INCREMENT, FALSE);
    KeReleaseSpinLock(&w->Lock, irql);
}

VOID CmCfgwkPrune(PCM_CFGWK w)
{
    KIRQL irql;

    KeAcquireSpinLock(&w->Lock, &irql);
    w->PruneOwed = (w->Members >= 32) ? 0xFFFFFFFFUL
                                      : (1UL << w->Members) - 1;
    w->PruneSeq++;
    KeSetEvent(&w->Wake, IO_NO_INCREMENT, FALSE);
    KeReleaseSpinLock(&w->Lock, irql);
}

ULONG CmCfgwkOpenState(PCM_CFGWK w, PULONG why, PULONG version)
{
    KIRQL irql;
    ULONG state;

    KeAcquireSpinLock(&w->Lock, &irql);
    state = w->OpenState;
    *why = w->Cfgmg.Why;
    *version = w->Cfgmg.Version;
    KeReleaseSpinLock(&w->Lock, irql);
    return state;
}

VOID CmCfgwkStop(PCM_CFGWK w, PCM_CFGWK_COUNTERS counters)
{
    KIRQL irql;
    BOOLEAN wait;

    CmZero(counters, sizeof(*counters));
    KeAcquireSpinLock(&w->Lock, &irql);
    w->Exit = 1;
    w->Nhi = NULL;
    wait = (BOOLEAN)!w->InCall;
    if (!wait) {
        w->Detached = 1;
    }
    /* Signalled under the lock: once it is released a detached worker
     * may free the block. */
    KeSetEvent(&w->Wake, IO_NO_INCREMENT, FALSE);
    KeReleaseSpinLock(&w->Lock, irql);
    if (!wait) {
        /* Inside a CONFIGMG call that may be waiting for this very stop to
         * finish: not waited for; the worker frees its own block. */
        counters->Detached = 1;
        return;
    }
    (VOID)KeWaitForSingleObject(&w->Exited, Executive, KernelMode, FALSE,
                                NULL);
    *counters = w->C;
    CmCfgmgClose(&w->Cfgmg);
    CmFree(w);
}

VOID CmCfgwkDrain(VOID)
{
    LARGE_INTEGER step;
    ULONG n;

    /* 100 ms relative, in 100 ns units: -1,000,000. */
    step.LowPart = 0xFFF0BDC0UL;
    step.HighPart = -1;
    for (n = 0; n < 50UL && cmWkLive != 0; n++) {
        (VOID)KeDelayExecutionThread(KernelMode, FALSE, &step);
    }
}
