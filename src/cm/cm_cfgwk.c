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
 * the bridge's devnode cannot be found or the call fails; and a marking
 * window (CM_WK_MARK_MS) in which each new child of an active member gets
 * DN_REMOVABLE (Change_DevNode_Status).
 *
 * LIFETIME. The worker's state is its own pool block, never the NHI's
 * extension. CmCfgwkStop detaches the NHI under the worker's lock; when
 * the worker is between calls the stop waits for it to leave and frees the
 * block; when it is inside a CONFIGMG call - which, during the NHI's own
 * removal, may not return until that removal has finished - the stop does
 * not wait (waiting there would deadlock): the worker frees its block and
 * closes its thunks itself when the call returns, making no further call.
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

typedef struct _CM_WK_PORT {
    ULONG Bdf;
    ULONG Devnode;          /* the bridge's, 0 until found                */
    ULONG PathRead;
    ULONG PathCount;
    ULONG Path[CM_PORTS_PATH_MAX];
    ULONG WalkLogged;
    ULONG KnownCount;
    ULONG Known[CM_WK_KNOWN];
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
    ULONG Retry;            /* member calls owed after a failure          */
    ULONG RetryVanish;
    ULONG RetryAt;          /* ms clock of the last failure               */
    ULONG MarkOpen;
    ULONG MarkFrom;
    ULONG OpenState;        /* CM_CFGWK_OPEN_*                            */
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
    ULONG perTick;

    /* KeQueryTimeIncrement is in 100 ns units: at least 1 ms a tick. */
    perTick = KeQueryTimeIncrement() / 10000UL;
    if (perTick == 0) {
        perTick = 1;
    }
    return (cmWkNowTicks() - thenTicks) * perTick;
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
 * returns.
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

/* The devnode of member i's bridge: the cached one while its id still
 * names that location, else a walk of the tree from the root. 0 for none. */
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
        if (cmWkPciPath(p->Bdf, p->Path, &p->PathCount)) {
            p->PathRead = 1;
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

/* The children of `devnode`, up to `room`. */
static ULONG cmWkChildren(PCM_CFGWK w, ULONG devnode, PULONG out, ULONG room)
{
    ULONG n;
    ULONG node;

    n = 0;
    if (!cmWkGo(w) ||
        CmCfgmgChild(&w->Cfgmg, devnode, &node) != CMG_CR_SUCCESS) {
        return 0;
    }
    while (node != 0 && n < room) {
        out[n++] = node;
        if (!cmWkGo(w)) {
            break;
        }
        if (CmCfgmgSibling(&w->Cfgmg, node, &node) != CMG_CR_SUCCESS) {
            break;
        }
    }
    return n;
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

/*
 * The single removal step of a vanished child of member i, on the worker:
 * Query_Remove_SubTree, and Remove_SubTree only when that was accepted (a
 * refusal - a driver with open files - is respected: nothing forced,
 * logged and counted). The request is checked to be current before each
 * call. FALSE when it went stale (the caller stops), TRUE otherwise,
 * whatever the outcome. Any orderly-remove handshake with the child's
 * driver wraps this function.
 */
static BOOLEAN cmWkRemoveChild(PCM_CFGWK w, ULONG i, ULONG gen,
                               ULONG devnode)
{
    ULONG cr;

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
    if (!cmWkCurrent(w, i, gen)) {
        return FALSE;
    }
    cr = CmCfgmgRemove(&w->Cfgmg, devnode);
    cmWkLog(w, "cm.reenum.9x.remove", cr);
    if (cr == CMG_CR_SUCCESS) {
        w->C.Removed++;
    }
    return TRUE;
}

/*
 * Section 6.1 on member i: the vanished children removed
 * (cmWkRemoveChild) (a refusal respected, never forced), then Reenumerate_DevNode.
 * TRUE when the devnode was found and the re-enumeration succeeded, or the
 * request went stale meanwhile (a newer one redoes it). Before each call
 * the request is checked to be current (cmWkCurrent): a stop, a newer post
 * for the member or a lost claim ends it, and no removal is made on an
 * inventory a reconnection may have outdated.
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
        count = idCount <= CM_WK_BUS_FUNCTIONS
                    ? cmWkChildren(w, bridge, children, CM_WK_KNOWN)
                    : 0;
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
            if (!cmWkRemoveChild(w, i, gen, children[c])) {
                return TRUE;
            }
        }
    }
    if (!cmWkCurrent(w, i, gen)) {
        return TRUE;
    }
    cr = CmCfgmgReenumerate(&w->Cfgmg, bridge);
    w->C.Calls++;
    if (cr != CMG_CR_SUCCESS) {
        w->C.CallFailures++;
    }
    cmWkLog(w, "cm.reenum.9x.reenumerate", (p->Bdf << 8) | cr);
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

    p = &w->Port[i];
    bridge = cmWkFindDevnode(w, i);
    if (bridge == 0) {
        return;
    }
    count = cmWkChildren(w, bridge, children, CM_WK_KNOWN);
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
    if (!cmWkGo(w)) {
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
 * between them - the one exception to "no further call after a stop":
 * they change nothing, and the image stays resident (CmCfgwkStart). The
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
        if (w->OpenState == CM_CFGWK_OPEN_OK) {
            for (i = 0; i < w->Members; i++) {
                if ((reenum & (1UL << i)) == 0 ||
                    !cmWkCurrent(w, i, gen[i])) {
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
        if ((done & retried) != 0 && w->MarkWanted) {
            /* An owed re-enumeration came late: so does the marking of
             * what it brings. */
            w->MarkOpen = 1;
            w->MarkFrom = cmWkNowTicks();
        }
        timed = (BOOLEAN)(w->Retry != 0 || w->MarkOpen);
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
                       ULONG active)
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
