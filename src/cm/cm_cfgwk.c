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
 * forced - then Reenumerate_DevNode; since task 43.47 the vanished
 * children are the member's sweep's and transactions' where it has a call
 * worker, cm_eject.h, and this worker refuses any re-enumeration of a
 * member they hold, CmEjectVanishHeld), retried every CM_WK_RETRY_MS while
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
 * reset - before any further re-enumeration of that member. Under the
 * USB4 profile that removal is held (Defer) until the recovery's commit
 * proves the path behind the member closed (CmCfgwkRelease, record 21
 * section 6.1.2, task 44.54): read on 98 SE, a removal made while the
 * member and everything above it read all ones leaves the drive's next
 * devnode at problem 14 until a restart.
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
 * THE PCI ANSWER BELOW A MEMBER (record 25 section 4.7, task 42-A.14). The
 * query device's request for the functions below the members is made here,
 * between operations, so its reads go through PCI.VXD's guarded route
 * (CmCfgmgPciRead behind CmCfgmgPciOpen) like the numbering's, never
 * alongside a numbering of the same chain, and never below a member whose
 * controller was lost (cmWkNoCfg). A bus is entered only where PCI.VXD
 * reads its bridge with the same numbers itself (HalGetBusData, which on
 * 98 SE answers only functions PCI.VXD knows) or where this worker's own
 * numbering gave those numbers (cmWkNumber's record). The request is its
 * own pool block shared by the asker and the worker, counted, freed by
 * whichever lets go last: the asker's wait is bounded and a worker inside
 * a blocked CONFIGMG call answers it late or never.
 *
 * A FUNCTION WITH NO INTERRUPT (design record 28, task 42-A.15). After the
 * result of a member's Reenumerate_DevNode comes back, its no-IRQ window
 * opens (cm_noirq.c holds the window, the per-device state, the journal,
 * the rules and the steps); at each of the worker's ticks while any member
 * has one in progress, the worker walks the member's descendants and makes
 * the operations the machine hands out, one after the other, taking each
 * result back at once. THIS IS THE INTERIM of record 28's dated note
 * (2026-10-19): the record places each operation on the member's call
 * worker behind R6f's handoff word (task 42-A.12); until those exist the
 * worker makes the calls itself, so a CONFIGMG call that blocks (a wizard
 * open) holds the domain's other work as every call of this worker does,
 * and the attempt's budget runs out meanwhile ("timeout", the late result
 * accounted). The operations take no part in the request generations
 * (cmWkGo): a newer post never cancels a transaction; the gate is the
 * stop, the claim and CONTROLLER_UNAVAILABLE's owed removal or no-scan
 * re-enumeration (cmWkNiGate).
 *
 * THE TUNNEL ID (design record 31 section 5, task 44.3). Windows 98 SE
 * and ME give no driver of this project a place on a bridge's devnode, so
 * the id PCI\XHCI98_TUNNEL&CC_010802 that Stor98's INF matches is given
 * here: in a window (CM_WK_MARK_MS) opened at the start for each active
 * member, when a member becomes active, and after each of its successful
 * Reenumerate_DevNode calls, every devnode below the member - six levels,
 * CM_WK_WALK_NODES nodes at most, as record 28's walk - whose
 * CompatibleIDs (Read_Registry_Value of its Enum key) carry
 * PCI\CC_010802 and not the id gets it from Add_ID as a compatible id,
 * a statement of where the drive sits: 98 SE and ME install Stor98 by its
 * INF's class match, since their hardware wizard does not search again
 * for a drive whose first search found nothing (record 31 section 5.2,
 * owner 2026-10-25). Each devnode is dealt with once (cm_tagid.c's
 * record, by handle and instance id), the record cleared at each
 * re-enumeration of a member so that a devnode made again is read again.
 * The set is an active member's descendants: the member is the port the
 * domain verified, so on 9x the id is the verified port's by
 * construction (record 31 section 5.1).
 *
 * IRQL: CmCfgwkStart, CmCfgwkStop, CmCfgwkPciAsk, CmCfgwkPciTake and the
 * worker at PASSIVE_LEVEL; CmCfgwkPost and CmCfgwkOpenState at <=
 * DISPATCH_LEVEL.
 */

#include "cm.h"
#include "cm_cfgmg.h"
#include "cm_ports.h"
#include "cm_nvlink.h"
#include "cm_qryb.h"
#include "tbt_brnum.h"
#include "cm_noirq.h"
#include "cm_tagid.h"

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
    ULONG NumCount;         /* the bridges this worker's last numbering of
                               the member left numbered, and their 18h
                               dwords as left (the PCI answer's admission) */
    ULONG NumBdf[XHCI_TBT_BRNUM_MAX_BRIDGES];
    ULONG NumNow[XHCI_TBT_BRNUM_MAX_BRIDGES];
    /* Design record 28: the member's no-IRQ state, and the interrupt
     * record the numbering just made, kept for the generation its
     * re-enumeration opens (NiIrqHave). */
    CM_NI_MEMBER Ni;
    CM_NI_IRQ NiIrqNext;
    ULONG NiIrqHave;
    ULONG NiDiagSeen;
} CM_WK_PORT;

/* The no-IRQ walk's stack (cmWkNiWalk). */
#define CM_WK_NI_DEPTH      6UL
#define CM_WK_NI_STACK      32UL
#define CM_WK_NI_OPS        512UL   /* operations a member, a tick         */

typedef struct _CM_WK_NI_NODE {
    ULONG Node;
    ULONG Depth;            /* 1 for the member's children                */
    ULONG Rot;              /* the swizzle of the bridges above it        */
    ULONG ParentBdf;        /* its parent's location (the member's: the
                               member)                                   */
    ULONG PathCount;        /* its parent's path, the path form's suffix  */
    ULONG Path[CM_PORTS_PATH_MAX];
} CM_WK_NI_NODE;

/* The tunnel id's walk (cmWkTag): a node and its level. */
typedef struct _CM_WK_TAG_NODE {
    ULONG Node;
    ULONG Depth;            /* 1 for the member's children                */
} CM_WK_TAG_NODE;
#define CM_WK_TAG_LIST      1024UL  /* a CompatibleIDs value read whole    */

/* A PCI answer asked of the worker (CmCfgwkPciAsk). */
#define CM_WK_PCI_POSTED    0UL
#define CM_WK_PCI_DONE      1UL
#define CM_WK_PCI_LEFT      2UL     /* the asker stopped waiting         */

typedef struct _CM_CFGWK_PCI {
    KSPIN_LOCK Lock;
    KEVENT Done;            /* notification: the answer is in Buf         */
    ULONG Refs;             /* the asker's and the worker's, under Lock   */
    ULONG State;            /* CM_WK_PCI_*, under Lock                    */
    ULONG Bytes;            /* the answer's length, 0 for none            */
    ULONG Reason;           /* CM_QP_NR_* when the walk was not made      */
    CM_QPCI_IN In;
    UCHAR Scratch[CM_QP_CFG_BYTES];
    UCHAR Buf[CM_QP_MAX_BYTES];
} CM_CFGWK_PCI, *PCM_CFGWK_PCI;

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
    ULONG Hold;             /* members whose re-enumeration is held: the
                               recovery's retirement barrier (CmCfgwkHold) */
    ULONG GoneOnly;         /* members whose owed removal includes a
                               removal-only request (CmCfgwkGone): every
                               child found then is removed, and it is
                               clean only once the bridge has none        */
    ULONG Defer;            /* members held: their CONTROLLER_UNAVAILABLE
                               removal, and any request merged into it,
                               waits until the commit proves the path
                               closed (record 21 section 6.1.2,
                               CmCfgwkRelease)                            */
    ULONG DeferReenum;      /* ... and the re-enumeration owed after it,
                               dropped when the hold ends: a released
                               member's tunnel is rebuilt, and the
                               rebuild's own post re-enumerates it       */
    ULONG EnumBusy;         /* members with a CmCfgmgReenumerate or a
                               no-IRQ operation admitted and not yet
                               returned (cmWkEnumAdmit, cmWkNiAdmit): the
                               barrier's removal-only request waits for
                               them (CmCfgwkEnumBusy)                     */
    ULONG GoneClean;        /* members whose last CONTROLLER_UNAVAILABLE
                               removal resolved with every recorded
                               devnode removed or absent (CmCfgwkRetired,
                               record 22 section 7.7's barrier)          */
    ULONG Takeover;         /* members whose call worker is live: a claim
                               whose removal failed may become their
                               vanished transaction (task 43.47)         */
    ULONG ClaimFull;        /* a claim found no free record in the
                               operation in progress: its removal is owed,
                               so no re-enumeration follows it            */
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
    PCM_CFGWK_PCI PciReq;   /* a PCI answer asked, not yet taken          */
    ULONG PciHeld;          /* the members' ReqGone | NoScan as the walk in
                               progress began (cmWkQpVxd)                */
    ULONG NoIrq;            /* XhciCmNoIrqStart as the start read it      */
    ULONG NiActive;         /* the claim the no-IRQ state belongs to      */
    ULONG NiClaimLost;      /* members whose claim a post took away since
                               the worker last looked (bit per index)    */
    ULONG NiBase;           /* the no-IRQ clock's origin (tick count)     */
    CM_WK_NI_NODE NiStack[CM_WK_NI_STACK];
    /* Design record 31 section 5: the tunnel id. TagReq under Lock (the
     * members newly active, posted); the rest the worker thread's own. */
    ULONG TagReq;
    ULONG TagOpen;          /* members whose window is open (bit each)    */
    ULONG TagFrom[XHCI_TBT_MAX_TUNNELS]; /* each window's opening, ticks */
    ULONG TagGiven;         /* ids given since the start                  */
    ULONG TagFailures;      /* Add_ID refused                             */
    ULONG TagUnread;        /* lists not read whole                       */
    CM_TAG_SEEN TagSeen;
    CM_WK_TAG_NODE TagStack[CM_WK_NI_STACK];
    char TagList[CM_WK_TAG_LIST + 2];
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

/* The same for a value that is a devnode handle: an address record,
 * level 4 alone (CmLogNoteAddress). <= DISPATCH_LEVEL. */
static VOID cmWkLogAddress(PCM_CFGWK w, const char *label, ULONG value)
{
    KIRQL irql;

    KeAcquireSpinLock(&w->Lock, &irql);
    if (w->Nhi != NULL) {
        CmLogNoteAddress(w->Nhi, label, value);
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

BOOLEAN CmCfgwkPciPath(ULONG bdf, PULONG path, PULONG count)
{
    return cmWkPciPath(bdf, path, count);
}

BOOLEAN CmCfgwkIdNames(const char *id, ULONG bdf, const ULONG *want,
                       ULONG wantCount)
{
    return cmWkIdNames(id, bdf, want, wantCount);
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
            /* Vendor 0001h is Configuration Request Retry Status: the
             * function is there but not ready, so the bus is not read
             * completely (tbt_brnum.c defers on the same id). */
            if ((id & 0xFFFFUL) == 0x0001UL) {
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

ULONG CmCfgwkBusIds(ULONG bridge, PULONG ids, ULONG room)
{
    return cmWkBusIds(bridge, ids, room);
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
static BOOLEAN cmWkHeld(PCM_CFGWK w, ULONG i)
{
    KIRQL irql;
    BOOLEAN held;

    KeAcquireSpinLock(&w->Lock, &irql);
    held = (BOOLEAN)((w->Hold & (1UL << i)) != 0);
    KeReleaseSpinLock(&w->Lock, irql);
    return held;
}

/* A re-enumeration of member i admitted against the retirement barrier:
 * the hold read and the call marked in flight under one lock, so a hold
 * raised after this either refused it or finds it in EnumBusy, and the
 * barrier's removal-only request is not made until it has returned
 * (CmCfgwkEnumBusy). The CONFIGMG call itself is made outside the lock;
 * cmWkEnumDone ends it. */
static BOOLEAN cmWkEnumAdmit(PCM_CFGWK w, ULONG i)
{
    KIRQL irql;
    BOOLEAN ok;

    KeAcquireSpinLock(&w->Lock, &irql);
    /* Task 43.47: nor while the member's vanished children are not all
     * retired (a sweep, a record or a claim on it), read under the same
     * lock, so a sweep begun after this finds the call in EnumBusy and
     * waits for it. */
    ok = (BOOLEAN)((w->Hold & (1UL << i)) == 0 &&
                   (w->PruneOwed & (1UL << i)) == 0 &&
                   !CmEjectVanishHeld(w->Port[i].Bdf));
    if (ok) {
        w->EnumBusy |= 1UL << i;
    }
    KeReleaseSpinLock(&w->Lock, irql);
    return ok;
}

static VOID cmWkEnumDone(PCM_CFGWK w, ULONG i)
{
    KIRQL irql;

    KeAcquireSpinLock(&w->Lock, &irql);
    w->EnumBusy &= ~(1UL << i);
    KeReleaseSpinLock(&w->Lock, irql);
}

/* Record 21 section 6.1.2: TRUE when member i's removal is held and still
 * owed; its re-enumeration then joins the held one (DeferReenum, dropped
 * at the release) instead of a retry. Under the lock. */
static BOOLEAN cmWkDeferOwed(PCM_CFGWK w, ULONG i)
{
    KIRQL irql;
    BOOLEAN held;

    KeAcquireSpinLock(&w->Lock, &irql);
    held = (BOOLEAN)((w->Defer & w->ReqGone & (1UL << i)) != 0);
    if (held) {
        w->DeferReenum |= 1UL << i;
    }
    KeReleaseSpinLock(&w->Lock, irql);
    return held;
}

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

/*
 * Record 22 section 7.7's removal-only request on member i, whose tunnel
 * was drained: every child the bridge has now joins the Gone set at this
 * generation, so the removal takes whatever devnode of the old endpoint is
 * still there, however the retained inventory was left. With `add` FALSE
 * it only reads. *none TRUE when a complete list shows no child at all -
 * the retirement this request waits for. FALSE when the list or an id
 * could not be read (nothing proven).
 */
static BOOLEAN cmWkGoneSeed(PCM_CFGWK w, ULONG i, BOOLEAN add, BOOLEAN *none)
{
    CM_WK_PORT *p;
    ULONG bridge;
    ULONG children[CM_WK_KNOWN];
    ULONG count;
    ULONG c;
    BOOLEAN complete;
    char text[CMG_MAX_DEVICE_ID_LEN + 1];

    p = &w->Port[i];
    *none = FALSE;
    bridge = cmWkFindDevnode(w, i);
    if (bridge == 0) {
        return FALSE;
    }
    count = cmWkChildren(w, bridge, children, CM_WK_KNOWN, &complete);
    if (!complete) {
        return FALSE;
    }
    *none = (BOOLEAN)(count == 0);
    if (!add) {
        return TRUE;
    }
    for (c = 0; c < count; c++) {
        if (!cmWkGo(w) ||
            CmCfgmgDeviceId(&w->Cfgmg, children[c], text, sizeof(text)) !=
                CMG_CR_SUCCESS) {
            return FALSE;
        }
        text[CMG_MAX_DEVICE_ID_LEN] = '\0';
        cmWkIdsAdd(&p->Gone, children[c], cmWkIdHash(text), p->PubGen);
    }
    return TRUE;
}

static BOOLEAN cmWkRemoveChild(PCM_CFGWK w, ULONG i, ULONG gen,
                               ULONG bridge, ULONG devnode,
                               BOOLEAN vanished);

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
            cmWkLogAddress(w, "cm.reenum.9x.gone.superseded", node);
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
            cmWkLogAddress(w, "cm.reenum.9x.gone.id-changed", node);
            continue;
        }
        vetoed = w->C.Vetoed;
        removed = w->C.Removed;
        calls = w->C.Calls;
        current = cmWkRemoveChild(w, i, gen, bridge, node, FALSE);
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
 * Task 43.47: whether `devnode` still carries instance id `id`, read back
 * immediately before a call that names it: CMG_CR_SUCCESS (the call is
 * made), CMG_CR_NO_SUCH_DEVNODE (verified gone: no such devnode, or a
 * successful read finds another id - CONFIGMG reuses handles), or
 * CMG_CR_FAILURE (inconclusive: nothing concluded, no call).
 */
static ULONG cmWkStill(PCM_CFGWK w, ULONG devnode, const char *id)
{
    char back[CMG_MAX_DEVICE_ID_LEN + 1];
    ULONG cr;
    ULONG k;

    cr = CmCfgmgDeviceId(&w->Cfgmg, devnode, back, sizeof(back));
    if (cr == CMG_CR_NO_SUCH_DEVNODE || cr == CMG_CR_INVALID_DEVNODE) {
        return CMG_CR_NO_SUCH_DEVNODE;
    }
    if (cr != CMG_CR_SUCCESS) {
        return CMG_CR_FAILURE;
    }
    back[CMG_MAX_DEVICE_ID_LEN] = '\0';
    for (k = 0; k <= CMG_MAX_DEVICE_ID_LEN; k++) {
        if (back[k] != id[k]) {
            return CMG_CR_NO_SUCH_DEVNODE;
        }
        if (back[k] == '\0') {
            break;
        }
    }
    return CMG_CR_SUCCESS;
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
 *
 * Task 43.47: the devnode is claimed in the eject records before the query
 * (CmEjectClaim). A devnode a record names (a vanished child's
 * transaction, an eject, a sweep's) is not called, and neither is one when
 * no record is free: undecided, TRUE returned with nothing counted. The
 * claim then ends (CmEjectClaimEnd): an accepted query whose removal
 * failed becomes the vanished transaction, which owns the devnode from
 * there - its request left in place, no rescan made here (the
 * re-enumeration owed waits for the retirement) - and, for a vanished
 * child (`vanished`), so does a refused query, its retries owed; both only
 * where the member's call worker is live (Takeover).
 */
static BOOLEAN cmWkRemoveChild(PCM_CFGWK w, ULONG i, ULONG gen,
                               ULONG bridge, ULONG devnode, BOOLEAN vanished)
{
    ULONG cr;
    ULONG id;
    ULONG claim;
    ULONG first;
    ULONG last;
    ULONG attempts;
    ULONG keptEntry;
    ULONG keptSeq;
    ULONG keptGen;
    BOOLEAN owned;
    BOOLEAN resumed;
    BOOLEAN textOk;
    BOOLEAN takeover;
    BOOLEAN took;
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
    KIRQL irql;
    char text[CMG_MAX_DEVICE_ID_LEN + 1];

    if (!cmWkCurrent(w, i, gen)) {
        return FALSE;
    }
    /* The child's ids, for the stor98 handshake below (a function still
     * present with an UP entry published for them). */
    id = 0;
    textOk = (BOOLEAN)(CmCfgmgDeviceId(&w->Cfgmg, devnode, text,
                                       sizeof(text)) == CMG_CR_SUCCESS);
    text[CMG_MAX_DEVICE_ID_LEN] = '\0';
    if (!textOk || !CmPortsIdVenDev(text, sizeof(text), &id)) {
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
    /* The claim before anything irreversible (task 43.47). */
    claim = 0;
    owned = FALSE;
    resumed = FALSE;
    keptEntry = CM_EJ_NONE;
    keptSeq = 0;
    keptGen = 0;
    if (textOk) {
        claim = CmEjectClaim(w->Port[i].Bdf, devnode, text, &owned, &resumed,
                             &keptEntry, &keptSeq, &keptGen);
    }
    if (claim == 0) {
        if (!owned) {
            w->ClaimFull = 1;
        }
        cmWkLog(w, "cm.vanish.claim.refused",
                (w->Port[i].Bdf << 8) |
                    (owned ? 1UL : (textOk ? 2UL : 3UL)));
        return TRUE;
    }
    KeAcquireSpinLock(&w->Lock, &irql);
    takeover = (BOOLEAN)((w->Takeover & (1UL << i)) != 0);
    KeReleaseSpinLock(&w->Lock, irql);
    /* A vanished child leaves the known set whatever follows: CONFIGMG
     * reuses a freed devnode's handle, so a re-plug's devnode can come
     * back under the same value and must be marked as new (section
     * 7.4). */
    cmWkForget(&w->Port[i], devnode);
    if (resumed) {
        /* A query of this devnode was accepted before (its parked claim):
         * never asked again; the removal alone, the request it left in
         * place withdrawn on success. */
        cmWkLog(w, "cm.vanish.claim.resumed", w->Port[i].Bdf);
        entry = keptEntry;
        seq = keptSeq;
        egen = keptGen;
        goto removal;
    }
    /* Its identity read back right before the query (task 43.47): gone,
     * nothing owed; not read, no query made. */
    cr = cmWkStill(w, devnode, text);
    if (cr == CMG_CR_SUCCESS) {
        cr = CmCfgmgQueryRemove(&w->Cfgmg, devnode);
    } else if (cr != CMG_CR_NO_SUCH_DEVNODE) {
        if (CmEjectClaimEnd(claim, CM_EJ_NO_CR, CM_EJ_NO_CR, 0, 0, 0,
                            CM_EJ_NONE, 0, 0, vanished, takeover)) {
            cmWkLog(w, "cm.vanish.takeover", (w->Port[i].Bdf << 8) | 4UL);
        }
        return TRUE;
    }
    cmWkLog(w, "cm.reenum.9x.query-remove", cr);
    if (cr != CMG_CR_SUCCESS) {
        /* The re-enumeration after it only asks PCI.VXD to read the bus
         * again. A vanished child's refusal may become its transaction,
         * which asks again, backed off (CmEjVanishRetryMs). A devnode
         * read gone is no veto. */
        if (cr != CMG_CR_NO_SUCH_DEVNODE) {
            w->C.Vetoed++;
        }
        if (CmEjectClaimEnd(claim, cr, CM_EJ_NO_CR, 0, 0, 0, CM_EJ_NONE,
                            0, 0, vanished, takeover)) {
            cmWkLog(w, "cm.vanish.takeover", (w->Port[i].Bdf << 8) | 1UL);
        }
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
removal:
    /* Remove_SubTree; after a stor98 request, retried every
     * CM_NVLINK_RETRY_MS while it fails, until CM_NVLINK_RETRY_FOR_MS after
     * the first attempt (R11 O6's bound, CmNvlinkRetryAgain; a veto is
     * never forced). */
    start = cmWkNowTicks();
    first = 0;
    last = 0;
    attempts = 0;
    for (;;) {
        /* Published before the first attempt: the member's blocked bit
         * follows O6's 30 s even while this call runs (task 43.47). */
        last = CmEjectNow();
        if (attempts == 0) {
            first = last;
            CmEjectClaimAccepted(claim, first);
        }
        attempts++;
        /* Each attempt names the devnode only while it still carries the
         * id (task 43.47): gone, retired; not read, a failed attempt. */
        cr = cmWkStill(w, devnode, text);
        if (cr == CMG_CR_SUCCESS) {
            cr = CmCfgmgRemove(&w->Cfgmg, devnode);
        }
        cmWkLog(w, "cm.reenum.9x.remove", cr);
        if (cr == CMG_CR_SUCCESS || cr == CMG_CR_NO_SUCH_DEVNODE ||
            entry == CM_NVLINK_NONE ||
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
    took = CmEjectClaimEnd(claim, CMG_CR_SUCCESS, cr, first, last, attempts,
                           entry, seq, egen, vanished, takeover);
    if (took || (cr != CMG_CR_SUCCESS && cr != CMG_CR_NO_SUCH_DEVNODE &&
                 takeover == FALSE)) {
        /* The vanished transaction owns the devnode from here (or, with no
         * call worker, its parked claim, this worker's to finish), its
         * request left in place for it to withdraw (R9a); the
         * re-enumeration this removal owed waits for its retirement
         * (CmEjectVanishHeld), posted here when the request went stale. */
        cmWkLog(w, "cm.vanish.takeover",
                (w->Port[i].Bdf << 8) | (took ? 2UL : 3UL));
        if (cmWkCurrent(w, i, gen)) {
            return TRUE;
        }
        KeAcquireSpinLock(&w->Lock, &irql);
        w->ReqReenum |= 1UL << i;
        KeReleaseSpinLock(&w->Lock, irql);
        return FALSE;
    }
    /* R11 O5: REMOVE_PENDING cleared after the removal. */
    if (entry != CM_NVLINK_NONE) {
        CmNvlinkWkWithdraw(entry, seq);
    }
    if (cmWkCurrent(w, i, gen)) {
        return TRUE;
    }
    if (!cmWkEnumAdmit(w, i)) {
        /* Record 22 section 7.7's retirement barrier stands (the request
         * that made this stale is its removal-only one): the rescan owed
         * is posted, and the enumeration pass holds it until the barrier
         * is lifted. */
        KeAcquireSpinLock(&w->Lock, &irql);
        w->ReqReenum |= 1UL << i;
        KeReleaseSpinLock(&w->Lock, irql);
        return FALSE;
    }
    /* Stale since the query: the rescan that the removal owes is made
     * here, not left to a request that may already have run. */
    cr = CmCfgmgReenumerate(&w->Cfgmg, bridge);
    /* The generation moved before the admission ends (the caller moves it
     * again on seeing the call: still one new generation for every entry
     * recorded before it). */
    w->Port[i].PubGen++;
    cmWkEnumDone(w, i);
    w->C.Calls++;
    if (cr != CMG_CR_SUCCESS) {
        w->C.CallFailures++;
    }
    cmWkLog(w, "cm.reenum.9x.reenumerate", (w->Port[i].Bdf << 8) | cr);
    return FALSE;
}

/*
 * Task 43.47: one parked claim of member i due for its retry, made here
 * whatever the member's admission (a claim parks only where the member has
 * no call worker to take it over): its devnode's instance id read back
 * first (gone, or another id: verified gone, the claim freed; not read:
 * parked again), then the vanished child's query asked again where it was
 * refused, and the removal of an accepted one - never a second query of an
 * accepted devnode - its request withdrawn on success.
 */
static VOID cmWkParked(PCM_CFGWK w, ULONG i)
{
    ULONG claim;
    ULONG devnode;
    ULONG what;
    ULONG entry;
    ULONG seq;
    ULONG egen;
    ULONG cr;
    ULONG now;
    ULONG k;
    BOOLEAN takeover;
    KIRQL irql;
    char id[CMG_MAX_DEVICE_ID_LEN + 1];
    char back[CMG_MAX_DEVICE_ID_LEN + 1];

    claim = CmEjectParkedNext(w->Port[i].Bdf, &devnode, id, sizeof(id), &what,
                              &entry, &seq, &egen);
    if (claim == 0) {
        return;
    }
    KeAcquireSpinLock(&w->Lock, &irql);
    takeover = (BOOLEAN)((w->Takeover & (1UL << i)) != 0);
    KeReleaseSpinLock(&w->Lock, irql);
    now = CmEjectNow();
    cr = CmCfgmgDeviceId(&w->Cfgmg, devnode, back, sizeof(back));
    back[CMG_MAX_DEVICE_ID_LEN] = '\0';
    if (cr == CMG_CR_SUCCESS) {
        for (k = 0; k <= CMG_MAX_DEVICE_ID_LEN; k++) {
            if (back[k] != id[k]) {
                cr = CMG_CR_NO_SUCH_DEVNODE;
                break;
            }
            if (back[k] == '\0') {
                break;
            }
        }
    } else if (cr == CMG_CR_INVALID_DEVNODE) {
        cr = CMG_CR_NO_SUCH_DEVNODE;
    }
    if (cr == CMG_CR_NO_SUCH_DEVNODE) {
        /* Verified gone: its request withdrawn and the prune posted while
         * the claim still holds the gate, then the claim ended. */
        if (entry != CM_EJ_NONE) {
            CmNvlinkWkWithdraw(entry, seq);
        }
        CmCfgwkPrune(w);
        (VOID)CmEjectClaimEnd(claim, CMG_CR_SUCCESS, CMG_CR_NO_SUCH_DEVNODE,
                              now, now, 1, CM_EJ_NONE, 0, 0, TRUE, takeover);
        cmWkLog(w, "cm.vanish.parked.gone", w->Port[i].Bdf);
        return;
    }
    if (cr != CMG_CR_SUCCESS) {
        /* Not read: nothing concluded, parked again. */
        (VOID)CmEjectClaimEnd(claim,
                              what == CM_EJ_DO_QUERY ? CMG_CR_FAILURE
                                                     : CMG_CR_SUCCESS,
                              what == CM_EJ_DO_QUERY ? CM_EJ_NO_CR
                                                     : CMG_CR_FAILURE,
                              now, now, 1, entry, seq, egen, TRUE, takeover);
        return;
    }
    if (what == CM_EJ_DO_QUERY) {
        cr = CmCfgmgQueryRemove(&w->Cfgmg, devnode);
        cmWkLog(w, "cm.vanish.parked.query", (w->Port[i].Bdf << 8) | cr);
        if (cr == CMG_CR_NO_SUCH_DEVNODE) {
            if (entry != CM_EJ_NONE) {
                CmNvlinkWkWithdraw(entry, seq);
            }
            CmCfgwkPrune(w);
        }
        if (cr != CMG_CR_SUCCESS) {
            (VOID)CmEjectClaimEnd(claim, cr, CM_EJ_NO_CR, 0, 0, 0, CM_EJ_NONE,
                                  0, 0, TRUE, takeover);
            return;
        }
        now = CmEjectNow();
    }
    CmEjectClaimAccepted(claim, now);
    cr = cmWkStill(w, devnode, id);
    if (cr == CMG_CR_SUCCESS) {
        cr = CmCfgmgRemove(&w->Cfgmg, devnode);
    }
    cmWkLog(w, "cm.vanish.parked.remove", (w->Port[i].Bdf << 8) | cr);
    if (cr == CMG_CR_SUCCESS || cr == CMG_CR_NO_SUCH_DEVNODE) {
        if (cr == CMG_CR_SUCCESS) {
            w->C.Removed++;
        }
        if (entry != CM_EJ_NONE) {
            /* R11 O5: the request it kept, withdrawn after the removal. */
            CmNvlinkWkWithdraw(entry, seq);
        }
        /* Retired: this worker's sets forget the handle before any
         * re-enumeration it makes. */
        CmCfgwkPrune(w);
    }
    (VOID)CmEjectClaimEnd(claim, CMG_CR_SUCCESS, cr, now, now, 1, entry, seq,
                          egen, TRUE, takeover);
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
 * PCI.VXD's configuration-space route, opened once: a read of `bdf` (a
 * member, which PCI.VXD knows) through _PCI_Read_Config must equal the
 * HAL's. A refusal stands for the worker's life and is logged once. A
 * member that does not answer through the HAL (no read, all ones, no
 * vendor, or Configuration Request Retry Status: the controller lost, as
 * when the USB Tree asks during a loss, or not ready yet), or stops
 * answering so before the guard's own read, gives no verdict:
 * nothing is latched, the route stays closed and is tried again at the
 * next need (record 21 section 6.1.2, task 44.54; read on ME, where such
 * an attempt during a loss turned the numbering off for the boot).
 */
/* A function's identity dword that names a function: not all ones, and a
 * vendor neither 0, FFFFh nor 0001h (Configuration Request Retry Status:
 * not ready yet). */
static BOOLEAN cmWkIdAnswers(ULONG id)
{
    ULONG vendor;

    vendor = id & 0xFFFFUL;
    return (BOOLEAN)(id != 0xFFFFFFFFUL && vendor != 0 &&
                     vendor != 0xFFFFUL && vendor != 0x0001UL);
}

static BOOLEAN cmWkPciRoute(PCM_CFGWK w, ULONG bdf)
{
    ULONG id;

    if (w->Cfgmg.PciOpen) {
        return TRUE;
    }
    if (w->PciRefused) {
        return FALSE;
    }
    if (!cmWkCfgRead(bdf, XHCI_TBT_CFG_ID, &id) || !cmWkIdAnswers(id)) {
        return FALSE;
    }
    if (!CmCfgmgPciOpen(&w->Cfgmg, bdf, id) &&
        !cmWkIdAnswers(w->Cfgmg.PciGuard)) {
        /* Gone or not ready between the two reads: no verdict either
         * (the route stays closed, so nothing is written through it). */
        return FALSE;
    }
    if (!w->Cfgmg.PciOpen) {
        w->PciRefused = 1;
        cmWkLog(w, "cm.brnum.9x.pci-refused", w->Cfgmg.PciGuard);
        cmWkDiag(w, bdf, NULL, 0, 2);
        return FALSE;
    }
    cmWkLog(w, "cm.brnum.9x.pci-open", id);
    return TRUE;
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
    ULONG k;
    ULONG tries;
    ULONG flags;
    BOOLEAN complete;
    BOOLEAN up;

    bdf = w->Port[i].Bdf;
    if (!cmWkCurrent(w, i, gen) || cmWkNoCfg(w, i)) {
        return;
    }
    /* What an earlier numbering left is not this one's record. */
    w->Port[i].NumCount = 0;
    w->Port[i].NiIrqHave = 0;
    if (!cmWkPciRoute(w, bdf)) {
        return;
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
                                       (tries < CM_WK_NUMBER_TRIES
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
    /* Record 28 rule 5: the interrupt stage's outcomes, for the
     * generation the re-enumeration after it opens. */
    CmNiIrqFromBrnum(&r, &w->Port[i].NiIrqNext);
    w->Port[i].NiIrqHave = 1;
    cmWkLog(w, "cm.brnum.9x.outcome", (bdf << 8) | r.Outcome);
    cmWkLog(w, "cm.brnum.9x.seen",
            (r.Functions << 16) | (r.Bridges << 8) | tries);
    if (r.Outcome == XHCI_TBT_BRNUM_NONE) {
        return;
    }
    cmWkLog(w, "cm.brnum.9x.port-buses", r.PortBuses);
    if (r.Outcome == XHCI_TBT_BRNUM_DONE ||
        r.Outcome == XHCI_TBT_BRNUM_REOPENED) {
        for (k = 0; k < r.Numbered && k < XHCI_TBT_BRNUM_MAX_BRIDGES; k++) {
            w->Port[i].NumBdf[k] = r.Bdf[k];
            w->Port[i].NumNow[k] = r.Now[k];
        }
        w->Port[i].NumCount = k;
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
static VOID cmWkTagWindow(PCM_CFGWK w, ULONG members);

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
    w->ClaimFull = 0;
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
            current = cmWkRemoveChild(w, i, gen, bridge, children[c], TRUE);
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
    if (w->ClaimFull) {
        /* Task 43.47: a vanished child found no free record for its
         * claim, so it was not removed: failed, tried again (its removal
         * before any re-enumeration of the member). */
        return FALSE;
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
    if (!cmWkEnumAdmit(w, i)) {
        /* The retirement barrier rose while this operation ran (the
         * pass checked it before): failed, retried once it is lifted. */
        return FALSE;
    }
    cr = CmCfgmgReenumerate(&w->Cfgmg, bridge);
    p->PubGen++;
    w->C.Calls++;
    if (cr != CMG_CR_SUCCESS) {
        w->C.CallFailures++;
    }
    cmWkLog(w, "cm.reenum.9x.reenumerate", (p->Bdf << 8) | cr);
    if (cr == CMG_CR_SUCCESS && w->NoIrq) {
        /* Record 28: the result came back - the member's no-IRQ window
         * for the generation this re-enumeration made, with the
         * numbering's record when it ran for it. */
        if (p->NiIrqHave) {
            CmNiSetIrq(&p->Ni, p->PubGen, &p->NiIrqNext);
        }
        CmNiOpenWindow(&p->Ni, p->PubGen, cmWkSince(w->NiBase));
    }
    p->NiIrqHave = 0;
    if (cr == CMG_CR_SUCCESS && cmWkCurrent(w, i, gen)) {
        /* What the re-enumeration left under the bridge, retained for a
         * later CONTROLLER_UNAVAILABLE. */
        cmWkRetain(w, i, bridge);
    }
    if (cr == CMG_CR_SUCCESS) {
        /* Record 31 section 5.1: the tunnel id's window, every devnode
         * read again (one made anew may have an old handle). */
        CmTagSeenInit(&w->TagSeen);
        cmWkTagWindow(w, 1UL << i);
    }
    /* Admitted until its bookkeeping is done: a removal-only request
     * posted after this sees the generation and the inventory it left. */
    cmWkEnumDone(w, i);
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

/* ---- a function with no interrupt (design record 28, task 42-A.15) ----- */

/* Every submission's gate (record 28, "the controller's loss and the
 * member's gates"): the worker not leaving, the claim held, neither
 * CONTROLLER_UNAVAILABLE's removal nor its no-scan re-enumeration owed,
 * and no retirement barrier standing on the member (record 22 section
 * 7.7, CmCfgwkHold). */
static ULONG cmWkNiGate(PCM_CFGWK w, ULONG i)
{
    KIRQL irql;
    ULONG open;

    KeAcquireSpinLock(&w->Lock, &irql);
    /* Task 43.47: nor while a vanished child of the member is not retired
     * or the worker's prune walk is owed. */
    open = (ULONG)(!w->Exit && (w->Active & (1UL << i)) != 0 &&
                   ((w->ReqGone | w->NoScan | w->NiClaimLost | w->Hold |
                     w->PruneOwed) &
                    (1UL << i)) == 0 &&
                   !CmEjectVanishHeld(w->Port[i].Bdf));
    KeReleaseSpinLock(&w->Lock, irql);
    return open;
}

/* One no-IRQ operation admitted: the gate read and the operation marked
 * in flight (EnumBusy) under one lock, so a retirement hold raised after
 * this either refused it or finds it busy and the removal-only request
 * follows its completion (record 22 section 7.7, CmCfgwkEnumBusy);
 * cmWkEnumDone ends it. */
static BOOLEAN cmWkNiAdmit(PCM_CFGWK w, ULONG i)
{
    KIRQL irql;
    BOOLEAN open;

    KeAcquireSpinLock(&w->Lock, &irql);
    open = (BOOLEAN)(!w->Exit && (w->Active & (1UL << i)) != 0 &&
                     ((w->ReqGone | w->NoScan | w->NiClaimLost | w->Hold |
                       w->PruneOwed) &
                      (1UL << i)) == 0 &&
                     !CmEjectVanishHeld(w->Port[i].Bdf));
    if (open) {
        w->EnumBusy |= 1UL << i;
    }
    KeReleaseSpinLock(&w->Lock, irql);
    return open;
}

/* ---- the tunnel id (design record 31 section 5, task 44.3) ------------ */

/* Non-const: the services take them as far pointers to writable data. */
static char cmWkTagId[] = CM_TAG_ID;
static char cmWkTagCompat[] = "CompatibleIDs";
/* A window opened for the members in `members` (the thread's own). */
static VOID cmWkTagWindow(PCM_CFGWK w, ULONG members)
{
    ULONG i;

    for (i = 0; i < w->Members; i++) {
        if (members & (1UL << i)) {
            w->TagOpen |= 1UL << i;
            w->TagFrom[i] = cmWkNowTicks();
        }
    }
}

/* The devnode's CompatibleIDs into w->TagList: CONFIGRET, *bytes what
 * was read; REG_SZ first, REG_BINARY where the value has that type. */
static ULONG cmWkTagRead(PCM_CFGWK w, ULONG node, PULONG bytes,
                         PULONG binary)
{
    ULONG cr;

    *binary = 0;
    *bytes = CM_WK_TAG_LIST;
    cr = CmCfgmgReadHardwareValue(&w->Cfgmg, node, cmWkTagCompat,
                                  CMG_REG_SZ, w->TagList, bytes);
    if (cr == CMG_CR_WRONG_TYPE) {
        *binary = 1;
        *bytes = CM_WK_TAG_LIST;
        cr = CmCfgmgReadHardwareValue(&w->Cfgmg, node, cmWkTagCompat,
                                      CMG_REG_BINARY, w->TagList, bytes);
    }
    if (cr == CMG_CR_SUCCESS && *bytes > CM_WK_TAG_LIST) {
        cr = CMG_CR_BUFFER_SMALL;
    }
    if (cr != CMG_CR_SUCCESS) {
        *bytes = 0;
    }
    w->TagList[*bytes] = 0;
    w->TagList[*bytes + 1] = 0;
    return cr;
}

/* A read as one log value: CONFIGRET 31:24; bit 16 the class there, 17
 * the id, 18 read as REG_BINARY; the bytes 15:0. */
static ULONG cmWkTagWord(ULONG cr, ULONG bytes, ULONG binary, ULONG hasClass,
                         ULONG hasId)
{
    return ((cr & 0xFFUL) << 24) | (bytes & 0xFFFFUL) | (hasClass << 16) |
           (hasId << 17) | (binary << 18);
}

/* One devnode below an active member (record 31 sections 5.1 and 5.2). */
static VOID cmWkTagNode(PCM_CFGWK w, ULONG node)
{
    ULONG cr;
    ULONG bytes;
    ULONG binary;
    ULONG hash;
    ULONG vd;
    ULONG readOk;
    ULONG hasClass;
    ULONG hasId;
    ULONG decision;
    ULONG addCr;
    char text[CMG_MAX_DEVICE_ID_LEN + 1];

    if (CmCfgmgDeviceId(&w->Cfgmg, node, text, sizeof(text)) !=
        CMG_CR_SUCCESS) {
        return;
    }
    text[CMG_MAX_DEVICE_ID_LEN] = '\0';
    hash = cmWkIdHash(text);
    if (CmTagSeenHas(&w->TagSeen, node, hash)) {
        return;
    }
    if (!CmPortsIdVenDev(text, sizeof(text), &vd)) {
        vd = 0;
    }
    if (!cmWkGo(w)) {
        return;
    }
    cr = cmWkTagRead(w, node, &bytes, &binary);
    /* A devnode with no CompatibleIDs at all reads as an empty list. */
    readOk = (ULONG)(cr == CMG_CR_SUCCESS || cr == CMG_CR_NO_SUCH_VALUE);
    hasClass = (ULONG)(cr == CMG_CR_SUCCESS &&
                       CmTagListHas(w->TagList, bytes, CM_TAG_CLASS));
    hasId = (ULONG)(cr == CMG_CR_SUCCESS &&
                    CmTagListHas(w->TagList, bytes, CM_TAG_ID));
    decision = CmTagDecide(readOk, hasClass, hasId);
    /* Once a devnode, whatever the outcome: a list not read is counted
     * and logged, not read again every tick. */
    CmTagSeenAdd(&w->TagSeen, node, hash);
    if (decision == CM_TAG_NOT_CLASS) {
        return;
    }
    cmWkLogAddress(w, "cm.reenum.9x.id.node", node);
    cmWkLog(w, "cm.reenum.9x.id.devid", vd);
    cmWkLog(w, "cm.reenum.9x.id.read",
            cmWkTagWord(cr, bytes, binary, hasClass, hasId));
    if (decision == CM_TAG_NO_READ) {
        w->TagUnread++;
        cmWkLog(w, "cm.reenum.9x.id.unread", w->TagUnread);
        return;
    }
    if (decision != CM_TAG_ADD || !cmWkGo(w)) {
        return;
    }
    addCr = CmCfgmgAddCompatibleId(&w->Cfgmg, node, cmWkTagId);
    cmWkLog(w, "cm.reenum.9x.id.add", addCr);
    if (addCr == CMG_CR_SUCCESS) {
        w->TagGiven++;
        cmWkLog(w, "cm.reenum.9x.id.given", w->TagGiven);
    } else {
        w->TagFailures++;
        cmWkLog(w, "cm.reenum.9x.id.failed", w->TagFailures);
    }
    if (!cmWkGo(w)) {
        return;
    }
    /* What the call left in the Enum key (record 31 section 9.3 item 5). */
    cr = cmWkTagRead(w, node, &bytes, &binary);
    hasClass = (ULONG)(cr == CMG_CR_SUCCESS &&
                       CmTagListHas(w->TagList, bytes, CM_TAG_CLASS));
    hasId = (ULONG)(cr == CMG_CR_SUCCESS &&
                    CmTagListHas(w->TagList, bytes, CM_TAG_ID));
    cmWkLog(w, "cm.reenum.9x.id.after",
            cmWkTagWord(cr, bytes, binary, hasClass, hasId));
}

/* Member i's descendants, six levels and CM_WK_WALK_NODES nodes at most,
 * each one through cmWkTagNode, depth first; the member's gate
 * (cmWkNiGate) after every call that may have blocked, as record 28's
 * walk. A level past the stack's room is left for the next tick. */
static VOID cmWkTag(PCM_CFGWK w, ULONG i)
{
    CM_WK_TAG_NODE cur;
    ULONG bridge;
    ULONG depth;
    ULONG nodes;
    ULONG child;
    ULONG cr;

    bridge = cmWkFindDevnode(w, i);
    if (bridge == 0 || !cmWkGo(w) || !cmWkNiGate(w, i)) {
        return;
    }
    depth = 0;
    nodes = 0;
    cur.Node = bridge;
    cur.Depth = 0;
    for (;;) {
        if (cur.Depth < CM_WK_NI_DEPTH) {
            cr = CmCfgmgChild(&w->Cfgmg, cur.Node, &child);
            if (cr != CMG_CR_SUCCESS) {
                child = 0;
            }
            while (child != 0 && depth < CM_WK_NI_STACK) {
                w->TagStack[depth].Node = child;
                w->TagStack[depth].Depth = cur.Depth + 1;
                depth++;
                if (CmCfgmgSibling(&w->Cfgmg, child, &child) !=
                    CMG_CR_SUCCESS) {
                    child = 0;
                }
            }
        }
        if (depth == 0 || nodes >= CM_WK_WALK_NODES || !cmWkGo(w) ||
            !cmWkNiGate(w, i)) {
            return;
        }
        cur = w->TagStack[--depth];
        nodes++;
        cmWkTagNode(w, cur.Node);
        if (!cmWkGo(w) || !cmWkNiGate(w, i)) {
            return;
        }
    }
}

/* The installer's record of Stor98 (rule 4): CM_NI_STOR98_*. */
static ULONG cmWkNiStor98(PCM_CFGWK w)
{
    char text[32];
    ULONG bytes;
    ULONG type;
    ULONG error;

    bytes = sizeof(text) - 1;
    text[0] = 0;
    error = CmCfgmgStor98Version(&w->Cfgmg, text, &bytes, &type);
    if (error == 2) {
        return CM_NI_STOR98_NONE;           /* ERROR_FILE_NOT_FOUND */
    }
    if (error != 0) {
        return CM_NI_STOR98_FAILED;
    }
    if (type != CMG_REG_SZ || bytes > sizeof(text) - 1) {
        return CM_NI_STOR98_OLD;
    }
    text[bytes] = 0;
    return CmNiStor98Version(text, bytes);
}

/* A devnode's instance id hash; CONFIGRET. */
static ULONG cmWkNiHash(PCM_CFGWK w, ULONG dn, PULONG hash)
{
    ULONG cr;
    char text[CMG_MAX_DEVICE_ID_LEN + 1];

    *hash = 0;
    cr = CmCfgmgDeviceId(&w->Cfgmg, dn, text, sizeof(text));
    if (cr == CMG_CR_SUCCESS) {
        text[CMG_MAX_DEVICE_ID_LEN] = '\0';
        *hash = cmWkIdHash(text);
    }
    return cr;
}

/* The member's or a bridge's secondary bus: the member through the HAL,
 * a bridge below it through PCI.VXD; 0 when not read. */
static ULONG cmWkNiSecondary(PCM_CFGWK w, ULONG member, ULONG bdf)
{
    ULONG v;

    if (bdf == member) {
        if (!cmWkCfgRead(bdf, XHCI_TBT_CFG_BUSES, &v)) {
            return 0;
        }
    } else if (!CmCfgmgPciRead(&w->Cfgmg, bdf, XHCI_TBT_CFG_BUSES, &v)) {
        return 0;
    }
    if (v == 0xFFFFFFFFUL) {
        return 0;
    }
    return (v >> 8) & 0xFFUL;
}

/*
 * The walk (record 28, "The walk"): the member's descendants, one
 * Get_Child or Get_Sibling at a time, six levels and CM_WK_WALK_NODES
 * nodes at most; each devnode's location from its instance id in either
 * form - BUS_&DEV_&FUNC_ by itself, the path form by the ancestry in hand
 * (its devfn on its parent's secondary bus, its path's tail the parent's)
 * - and the swizzle of the bridges between the member and it. One whose
 * id resolves to no location is skipped and counted.
 */
static VOID cmWkNiWalk(PCM_CFGWK w, ULONG i)
{
    CM_WK_PORT *p;
    CM_WK_NI_NODE *e;
    CM_WK_NI_NODE cur;
    ULONG bridge;
    ULONG depth;
    ULONG nodes;
    ULONG child;
    ULONG node;
    ULONG bdf;
    ULONG bus;
    ULONG path[CM_PORTS_PATH_MAX];
    ULONG n;
    ULONG k;
    ULONG ok;
    ULONG hash;
    ULONG cr;
    ULONG complete;
    char text[CMG_MAX_DEVICE_ID_LEN + 1];

    p = &w->Port[i];
    cmWkOp(w, CM_WK_NO_OP, 0);
    bridge = cmWkFindDevnode(w, i);
    if (bridge == 0 || !p->PathRead || !cmWkNiGate(w, i) ||
        !cmWkPciRoute(w, p->Bdf)) {
        return;
    }
    depth = 0;
    nodes = 0;
    complete = 1;
    CmNiWalkBegin(&p->Ni);
    cr = CmCfgmgChild(&w->Cfgmg, bridge, &child);
    if (cr == CMG_CR_NO_SUCH_DEVNODE) {
        child = 0;
    } else if (cr != CMG_CR_SUCCESS) {
        return;
    }
    while (child != 0) {
        if (depth >= CM_WK_NI_STACK) {
            complete = 0;
            break;
        }
        e = &w->NiStack[depth++];
        e->Node = child;
        e->Depth = 1;
        e->Rot = 0;
        e->ParentBdf = p->Bdf;
        e->PathCount = p->PathCount;
        for (k = 0; k < p->PathCount && k < CM_PORTS_PATH_MAX; k++) {
            e->Path[k] = p->Path[k];
        }
        cr = CmCfgmgSibling(&w->Cfgmg, child, &child);
        if (cr == CMG_CR_NO_SUCH_DEVNODE) {
            break;
        }
        if (cr != CMG_CR_SUCCESS) {
            complete = 0;
            break;
        }
    }
    while (depth > 0) {
        /* The member's gate after every call that may have blocked: a
         * loss ends the walk before any bridge below is read. */
        if (nodes >= CM_WK_WALK_NODES || !cmWkGo(w) || !cmWkNiGate(w, i)) {
            complete = 0;
            break;
        }
        cur = w->NiStack[--depth];
        node = cur.Node;
        nodes++;
        if (CmCfgmgDeviceId(&w->Cfgmg, node, text, sizeof(text)) !=
            CMG_CR_SUCCESS) {
            complete = 0;
            continue;
        }
        text[CMG_MAX_DEVICE_ID_LEN] = '\0';
        hash = cmWkIdHash(text);
        ok = 0;
        bdf = XHCI_TBT_NO_BDF;
        if (CmPortsIdLocation(text, CMG_MAX_DEVICE_ID_LEN, &bdf)) {
            ok = 1;
        } else if (CmPortsIdPath(text, CMG_MAX_DEVICE_ID_LEN, path,
                                 CM_PORTS_PATH_MAX, &n) &&
                   n == cur.PathCount + 1) {
            ok = 1;
            for (k = 0; k < cur.PathCount; k++) {
                if (path[k + 1] != cur.Path[k]) {
                    ok = 0;
                }
            }
            /* The gate again: Get_Device_ID may have blocked. */
            if (ok && !cmWkNiGate(w, i)) {
                complete = 0;
                break;
            }
            bus = ok ? cmWkNiSecondary(w, p->Bdf, cur.ParentBdf) : 0;
            if (bus == 0) {
                ok = 0;
            } else {
                bdf = (bus << 8) | (path[0] & 0xFFUL);
            }
        }
        if (!ok) {
            /* Present but not placed: the walk cannot prove the others
             * gone, so nothing is forgotten by it. */
            complete = 0;
            CmNiUnresolved(&p->Ni);
            continue;
        }
        CmNiSeen(&p->Ni, node, hash, bdf, cur.Rot & 3UL);
        if (cur.Depth >= CM_WK_NI_DEPTH) {
            continue;
        }
        cr = CmCfgmgChild(&w->Cfgmg, node, &child);
        if (cr == CMG_CR_NO_SUCH_DEVNODE) {
            continue;
        }
        if (cr != CMG_CR_SUCCESS) {
            complete = 0;
            continue;
        }
        while (child != 0) {
            if (depth >= CM_WK_NI_STACK) {
                complete = 0;
                break;
            }
            e = &w->NiStack[depth++];
            e->Node = child;
            e->Depth = cur.Depth + 1;
            e->Rot = cur.Rot + XHCI_TBT_BDF_DEV(bdf);
            e->ParentBdf = bdf;
            e->Path[0] = bdf & 0xFFUL;
            for (k = 0; k < cur.PathCount && k + 1 < CM_PORTS_PATH_MAX; k++) {
                e->Path[k + 1] = cur.Path[k];
            }
            e->PathCount = k + 1;
            cr = CmCfgmgSibling(&w->Cfgmg, child, &child);
            if (cr == CMG_CR_NO_SUCH_DEVNODE) {
                break;
            }
            if (cr != CMG_CR_SUCCESS) {
                complete = 0;
                break;
            }
        }
    }
    CmNiWalkDone(&p->Ni, complete);
}

/* One operation of cm_noirq.h, made here at once (the interim). */
static VOID cmWkNiExec(PCM_CFGWK w, ULONG i, const CM_NI_OP *op,
                       PCM_NI_RES res)
{
    CM_NI_MEMBER *m;
    ULONG cr;
    ULONG lc;
    ULONG pri;
    ULONG k;
    ULONG v;
    ULONG rd;
    ULONG hash;
    ULONG dlBytes;
    ULONG pdBytes;
    ULONG dlCr;
    ULONG pdCr;
    char dl[16];
    char pd[16];

    m = &w->Port[i].Ni;
    CmZero(res, sizeof(*res));
    res->Id = op->Id;
    switch (op->Kind) {
    case CM_NI_OP_HEADER:
        /* No configuration space below a member whose controller was
         * lost, however late the loss came (cmWkNiGate). */
        if (!cmWkNiGate(w, i) || !cmWkPciRoute(w, w->Port[i].Bdf)) {
            res->Cr = CMG_CR_FAILURE;
            break;
        }
        for (k = 0; k < 16; k++) {
            if (!CmCfgmgPciRead(&w->Cfgmg, op->Bdf, k * 4, &res->Val[k])) {
                res->Cr = CMG_CR_FAILURE;
                break;
            }
        }
        break;
    case CM_NI_OP_PORT_INTR:
    case CM_NI_OP_LINE:
        if (!cmWkNiGate(w, i) ||
            !CmCfgmgPciRead(&w->Cfgmg, op->Bdf, 0x3CUL, &res->Val[0]) ||
            !CmCfgmgPciRead(&w->Cfgmg, op->Bdf, 0x00UL, &res->Val[1])) {
            res->Cr = CMG_CR_FAILURE;
        }
        break;
    case CM_NI_OP_STATUS:
        res->Cr = CmCfgmgStatus(&w->Cfgmg, op->Dn, &res->Status,
                                &res->Problem);
        if (res->Cr == CMG_CR_SUCCESS) {
            res->Cr = cmWkNiHash(w, op->Dn, &res->Hash);
        }
        break;
    case CM_NI_OP_BINDING:
        dlBytes = sizeof(dl) - 1;
        pdBytes = sizeof(pd) - 1;
        dl[0] = 0;
        pd[0] = 0;
        dlCr = CmCfgmgReadSoftwareValue(&w->Cfgmg, op->Dn, "DevLoader", dl,
                                        &dlBytes);
        pdCr = CmCfgmgReadSoftwareValue(&w->Cfgmg, op->Dn, "PortDriver", pd,
                                        &pdBytes);
        if (dlBytes > sizeof(dl) - 1) {
            dlBytes = sizeof(dl) - 1;
        }
        if (pdBytes > sizeof(pd) - 1) {
            pdBytes = sizeof(pd) - 1;
        }
        dl[dlBytes] = 0;
        pd[pdBytes] = 0;
        /* A name longer than the room reads as another driver. */
        res->Bind = CmNiBindFlags(dlCr == CMG_CR_SUCCESS || dlCr ==
                                      CMG_CR_BUFFER_SMALL,
                                  dlCr == CMG_CR_SUCCESS ? dl : "", dlBytes,
                                  pdCr == CMG_CR_SUCCESS || pdCr ==
                                      CMG_CR_BUFFER_SMALL,
                                  pdCr == CMG_CR_SUCCESS ? pd : "", pdBytes);
        break;
    case CM_NI_OP_LC_LIST:
        cr = CmCfgmgFirstLogConf(&w->Cfgmg, op->Dn, op->LcKind, &lc);
        while (cr == CMG_CR_SUCCESS) {
            if (res->Count == CM_NI_LC_MAX) {
                res->Cut = 1;
                break;
            }
            cr = CmCfgmgLogConfPriority(&w->Cfgmg, lc, &pri);
            if (cr != CMG_CR_SUCCESS) {
                break;
            }
            res->Lc[res->Count] = lc;
            res->Pri[res->Count] = pri;
            res->Count++;
            cr = CmCfgmgNextLogConf(&w->Cfgmg, lc, &lc);
        }
        res->Cr = (cr == CMG_CR_NO_MORE_LOG_CONF && res->Count != 0)
                      ? CMG_CR_SUCCESS : cr;
        /* The list as read: count, first and second priorities. */
        cmWkLog(w, "cm.reenum.9x.noirq.lc",
                ((res->Count & 0xFFUL) << 24) | (res->Pri[0] & 0xFFFFUL) |
                    (res->Count > 1 ? ((res->Pri[1] >> 8) & 0xFFUL) << 16
                                    : 0UL));
        break;
    case CM_NI_OP_RD_READ:
        res->Cr = CmCfgmgNextResDes(&w->Cfgmg, op->Lc, op->ResType, &res->Rd,
                                    &res->Type);
        if (res->Cr != CMG_CR_SUCCESS) {
            cmWkLog(w, "cm.reenum.9x.noirq.rd",
                    (op->ResType << 24) | (res->Cr & 0xFFFFUL));
            break;
        }
        res->Cr = CmCfgmgResDesSize(&w->Cfgmg, res->Rd, &res->Size);
        if (res->Cr == CMG_CR_SUCCESS && res->Size != 0 &&
            res->Size <= op->Len && op->Off + op->Len <= sizeof(m->Desc)) {
            res->Cr = CmCfgmgResDesData(&w->Cfgmg, res->Rd,
                                        m->Desc + op->Off, res->Size);
        }
        cmWkLog(w, "cm.reenum.9x.noirq.rd",
                (op->ResType << 24) | ((res->Type & 0xFFUL) << 16) |
                    ((res->Size & 0xFFUL) << 8) | (res->Cr & 0xFFUL));
        break;
    case CM_NI_OP_ADD_EMPTY:
        res->Cr = CmCfgmgAddEmptyLogConf(&w->Cfgmg, op->Dn,
                                         CMG_LCPRI_DISABLED,
                                         op->LcKind | CMG_PRIORITY_EQUAL_LAST,
                                         &res->Handle);
        break;
    case CM_NI_OP_PLACE:
        cr = cmWkNiHash(w, op->Dn, &hash);
        if (cr == CMG_CR_NO_SUCH_DEVNODE || cr == CMG_CR_INVALID_DEVNODE) {
            res->Place = CM_NI_PLACE_GONE;
            res->Cr = cr;
            break;
        }
        if (cr != CMG_CR_SUCCESS) {
            res->Place = CM_NI_PLACE_FAILED;
            res->Cr = cr;
            break;
        }
        if (hash != op->Hash) {
            res->Place = CM_NI_PLACE_OTHER;
            break;
        }
        res->Place = CM_NI_PLACE_MISSING;
        cr = CmCfgmgFirstLogConf(&w->Cfgmg, op->Dn, op->LcKind, &lc);
        while (cr == CMG_CR_SUCCESS) {
            if (lc == op->Lc) {
                cr = CmCfgmgLogConfPriority(&w->Cfgmg, lc, &pri);
                if (cr == CMG_CR_SUCCESS) {
                    res->Place = CM_NI_PLACE_AT;
                    res->PlacePri = pri;
                }
                break;
            }
            cr = CmCfgmgNextLogConf(&w->Cfgmg, lc, &lc);
        }
        if (cr != CMG_CR_SUCCESS && cr != CMG_CR_NO_MORE_LOG_CONF) {
            res->Place = CM_NI_PLACE_FAILED;
        }
        res->Cr = cr == CMG_CR_NO_MORE_LOG_CONF ? CMG_CR_SUCCESS : cr;
        break;
    case CM_NI_OP_ADD_RD:
        if (op->Off + op->Len > sizeof(m->Desc)) {
            res->Cr = CMG_CR_FAILURE;
            break;
        }
        res->Cr = CmCfgmgAddResDes(&w->Cfgmg, op->Lc, CMG_RESTYPE_MEM,
                                   m->Desc + op->Off, op->Len, &rd);
        break;
    case CM_NI_OP_SET_PRI:
        res->Cr = CmCfgmgSetLogConfPriority(&w->Cfgmg, op->Lc, op->Pri);
        break;
    case CM_NI_OP_REPROCESS:
        res->Cr = CmCfgmgReprocess(&w->Cfgmg, op->Dn);
        break;
    case CM_NI_OP_READY:
        res->Cr = CmCfgmgSetupReady(&w->Cfgmg, op->Dn);
        break;
    default:
        res->Cr = CMG_CR_FAILURE;
        break;
    }
    /* The steps that change something, and the outcome reads, logged
     * (step 31:24, CONFIGRET 15:0); the devnode handle at level 4. */
    v = (op->Kind << 24) | (res->Cr & 0xFFFFUL);
    if (op->Kind >= CM_NI_OP_ADD_EMPTY && op->Kind != CM_NI_OP_PLACE) {
        cmWkLog(w, "cm.reenum.9x.noirq", v);
    }
    if (op->Kind == CM_NI_OP_ADD_EMPTY) {
        cmWkLogAddress(w, "cm.reenum.9x.noirq.devnode", op->Dn);
    }
}

/* XhciCmNoIrqDiag: the member's newer diagnostic to the NHI. */
static VOID cmWkNiDiagCopy(PCM_CFGWK w, ULONG i)
{
    ULONG d[CM_NI_DIAG_WORDS];
    ULONG k;
    KIRQL irql;
    KIRQL inner;

    if (w->Port[i].Ni.DiagSeq == w->Port[i].NiDiagSeen) {
        return;
    }
    w->Port[i].NiDiagSeen = w->Port[i].Ni.DiagSeq;
    CmNiDiag(&w->Port[i].Ni, d);
    KeAcquireSpinLock(&w->Lock, &irql);
    if (w->Nhi != NULL) {
        KeAcquireSpinLock(&w->Nhi->BrDiagLock, &inner);
        w->Nhi->NiDiagSeq++;
        if (w->Nhi->NiDiagSeq == 0) {
            w->Nhi->NiDiagSeq = 1;
        }
        d[0] = w->Nhi->NiDiagSeq;
        for (k = 0; k < CM_NIDIAG_WORDS && k < CM_NI_DIAG_WORDS; k++) {
            w->Nhi->NiDiag[k] = d[k];
        }
        KeReleaseSpinLock(&w->Nhi->BrDiagLock, inner);
    }
    KeReleaseSpinLock(&w->Lock, irql);
}

/* One tick of member i's no-IRQ work: the walk while its window is open,
 * then every operation the pass hands out. */
static VOID cmWkNoIrq(PCM_CFGWK w, ULONG i)
{
    CM_NI_MEMBER *m;
    CM_NI_CTX ctx;
    CM_NI_OP op;
    CM_NI_RES res;
    ULONG ops;
    ULONG state;

    m = &w->Port[i].Ni;
    ctx.Now = cmWkSince(w->NiBase);
    ctx.Switch = w->NoIrq;
    ctx.Gate = cmWkNiGate(w, i);
    ctx.Stor98 = CM_NI_STOR98_FAILED;
    if (ctx.Gate && CmNiWindow(m)) {
        cmWkNiWalk(w, i);
        /* The walk's calls may have blocked: the clock and the gate as
         * they are now. */
        ctx.Now = cmWkSince(w->NiBase);
        ctx.Gate = cmWkNiGate(w, i);
    }
    CmNiPass(m, &ctx);
    if (ctx.Gate) {
        ctx.Stor98 = cmWkNiStor98(w);
    }
    ops = 0;
    while (ops++ < CM_WK_NI_OPS && CmNiNext(m, &ctx, &op)) {
        state = m->Dev[op.Dev].State;
        if (cmWkNiAdmit(w, i)) {
            cmWkNiExec(w, i, &op, &res);
            cmWkEnumDone(w, i);
        } else {
            /* The gate closed since the pass sampled it: not made. */
            CmZero(&res, sizeof(res));
            res.Id = op.Id;
            res.Cr = CMG_CR_FAILURE;
        }
        ctx.Now = cmWkSince(w->NiBase);
        ctx.Gate = cmWkNiGate(w, i);
        CmNiTake(m, &ctx, &res);
        if (op.Dev < CM_NOIRQ_DEVICES && m->Dev[op.Dev].Used &&
            m->Dev[op.Dev].State != state &&
            (m->Dev[op.Dev].State == CM_NI_S_EXCLUDED ||
             m->Dev[op.Dev].State == CM_NI_S_FAILED ||
             m->Dev[op.Dev].State == CM_NI_S_DONE ||
             m->Dev[op.Dev].State == CM_NI_S_ACTIVE)) {
            cmWkLog(w, m->Dev[op.Dev].State == CM_NI_S_DONE ||
                           m->Dev[op.Dev].State == CM_NI_S_ACTIVE
                           ? "cm.reenum.9x.noirq.after"
                           : "cm.reenum.9x.noirq.refused",
                    (m->Dev[op.Dev].Bdf << 16) |
                        (m->Dev[op.Dev].State << 8) | m->Dev[op.Dev].Why);
        }
    }
    cmWkNiDiagCopy(w, i);
}

/* The members' no-IRQ counts into `c` (record 28 section 7). */
static VOID cmWkNiCounters(PCM_CFGWK w, PCM_CFGWK_COUNTERS c)
{
    ULONG i;
    ULONG k;
    const CM_NI_COUNTERS *n;

    for (i = 0; i < w->Members; i++) {
        n = &w->Port[i].Ni.C;
        c->NoIrqCandidates += n->Candidates;
        c->NoIrqAttempts += n->Attempts;
        c->NoIrqStarted += n->Started;
        c->NoIrqUnstarted += n->Unstarted;
        c->NoIrqLeftovers += n->Leftovers;
        c->NoIrqLeftOver += n->LeftOver;
        c->NoIrqGone += n->Gone;
        c->NoIrqTimeouts += n->Timeouts;
        c->NoIrqLate += n->Late;
        c->NoIrqPin0Seen += n->Pin0Seen;
        c->NoIrqPin0Started += n->Pin0Started;
        for (k = 0; k < 16 && k < CM_NI_W_COUNT; k++) {
            c->NoIrqRefused[k] += n->Refused[k];
        }
    }
}

/* ---- the PCI answer below the members (task 42-A.14) ------------------ */

/* HalGetBusData from offset 0, the query device's reader of the NHI and
 * the members (cm_qryos.c's): functions PCI.VXD knows. */
static ULONG cmWkQpPrefix(PVOID context, ULONG bdf, UCHAR *buf, ULONG bytes)
{
    UNREFERENCED_PARAMETER(context);
    return HalGetBusData(PCIConfiguration, XHCI_TBT_BDF_BUS(bdf),
                         XHCI_TBT_BDF_DEV(bdf) |
                             (XHCI_TBT_BDF_FN(bdf) << 5),
                         buf, bytes);
}

static ULONG cmWkQpHal(PVOID context, ULONG bdf, UCHAR *buf, ULONG bytes,
                       PULONG valid)
{
    return CmQpciReadByDword(cmWkQpPrefix, NULL, bdf, buf, bytes,
                             ((PCM_CFGWK_PCI)context)->Scratch, valid);
}

/* Below a member: PCI.VXD's _PCI_Read_Config, one DWORD at a time (a
 * function PCI.VXD does not know yet is read the way PCI.VXD reads it);
 * all ones where nothing answers, which the walk takes for absent. */
static ULONG cmWkQpVxd(PVOID context, ULONG bdf, UCHAR *buf, ULONG bytes,
                       PULONG valid)
{
    PCM_CFGWK w;
    ULONG off;
    ULONG v;
    ULONG i;
    ULONG lost;
    KIRQL irql;

    w = (PCM_CFGWK)context;
    *valid = 0;
    if (bytes == 0 || bytes > CM_QP_CFG_BYTES || (bytes & 3UL) != 0) {
        return CM_QP_RD_FAIL;
    }
    /* A controller lost since the walk began (a member newly held): no
     * further read below any member, refused. */
    KeAcquireSpinLock(&w->Lock, &irql);
    lost = (w->ReqGone | w->NoScan) & ~w->PciHeld;
    KeReleaseSpinLock(&w->Lock, irql);
    if (lost != 0) {
        return CM_QP_RD_FAIL;
    }
    for (off = 0; off < bytes; off += 4) {
        if (!CmCfgmgPciRead(&w->Cfgmg, bdf, off, &v)) {
            if (off == 0) {
                return CM_QP_RD_FAIL;
            }
            for (i = off; i < bytes; i++) {
                buf[i] = 0;
            }
            *valid = off;
            return CM_QP_RD_OK;
        }
        buf[off] = (UCHAR)(v & 0xFFUL);
        buf[off + 1] = (UCHAR)((v >> 8) & 0xFFUL);
        buf[off + 2] = (UCHAR)((v >> 16) & 0xFFUL);
        buf[off + 3] = (UCHAR)((v >> 24) & 0xFFUL);
    }
    *valid = bytes;
    return CM_QP_RD_OK;
}

/* The walk's admission (cm_qryb.h CM_QP_ADMIT): the bridge's bus numbers
 * (18h bits 23:0) as PCI.VXD reads them itself, or as this worker's last
 * numbering of a member left them. Nothing else is entered: a bus number
 * nobody assigned may stand for a bus elsewhere. */
static ULONG cmWkQpAdmit(PVOID context, ULONG bridge, ULONG buses)
{
    PCM_CFGWK w;
    ULONG v;
    ULONG i;
    ULONG k;

    w = (PCM_CFGWK)context;
    if (cmWkCfgRead(bridge, XHCI_TBT_CFG_BUSES, &v) && v != 0xFFFFFFFFUL &&
        (v & 0x00FFFFFFUL) == (buses & 0x00FFFFFFUL)) {
        return 1;
    }
    for (i = 0; i < w->Members; i++) {
        for (k = 0; k < w->Port[i].NumCount; k++) {
            if (w->Port[i].NumBdf[k] == bridge &&
                (w->Port[i].NumNow[k] & 0x00FFFFFFUL) ==
                    (buses & 0x00FFFFFFUL)) {
                return 1;
            }
        }
    }
    return 0;
}

/* One reference let go; the last frees the request. */
static VOID cmWkPciRelease(PCM_CFGWK_PCI req)
{
    KIRQL irql;
    ULONG left;

    KeAcquireSpinLock(&req->Lock, &irql);
    left = --req->Refs;
    KeReleaseSpinLock(&req->Lock, irql);
    if (left == 0) {
        CmFree(req);
    }
}

/* The worker's answer (or none: `bytes` 0), unless the asker left; then
 * the worker's reference let go. */
static VOID cmWkPciDone(PCM_CFGWK_PCI req, ULONG bytes, ULONG reason)
{
    KIRQL irql;

    KeAcquireSpinLock(&req->Lock, &irql);
    if (req->State == CM_WK_PCI_POSTED) {
        req->State = CM_WK_PCI_DONE;
        req->Bytes = bytes;
        req->Reason = reason;
        (VOID)KeSetEvent(&req->Done, IO_NO_INCREMENT, FALSE);
    }
    KeReleaseSpinLock(&req->Lock, irql);
    cmWkPciRelease(req);
}

/*
 * The answer, built on the worker between operations. The members whose
 * controller was lost (cmWkNoCfg: no bridge below them may be read) are
 * held; PCI.VXD's route is opened here when no numbering opened it, and
 * a refusal of its guard leaves the functions below unread.
 */
static VOID cmWkPciServe(PCM_CFGWK w, PCM_CFGWK_PCI req)
{
    CM_QPCI_IN in;
    ULONG reason;
    ULONG below;
    ULONG held;
    ULONG bytes;
    ULONG p;
    ULONG i;
    KIRQL irql;

    in = req->In;
    reason = CM_QP_NR_SYSTEM;
    below = 0;
    if (w->OpenState != CM_CFGWK_OPEN_OK || w->Members == 0) {
        reason = CM_QP_NR_NO_WORKER;
    } else if (cmWkPciRoute(w, w->Port[0].Bdf)) {
        below = 1;
    } else {
        reason = CM_QP_NR_GUARD;
    }
    /* One snapshot for both: the members held for this answer, and the
     * ones whose later loss stops the reads below (cmWkQpVxd). */
    held = 0;
    KeAcquireSpinLock(&w->Lock, &irql);
    w->PciHeld = w->ReqGone | w->NoScan;
    KeReleaseSpinLock(&w->Lock, irql);
    for (p = 0; p < in.PortCount && p < CM_QP_MAX_PORTS; p++) {
        for (i = 0; i < w->Members; i++) {
            if (w->Port[i].Bdf == in.Ports[p].Bdf &&
                (w->PciHeld & (1UL << i)) != 0) {
                held |= 1UL << p;
            }
        }
    }
    in.ReadBelow = below;
    in.Reason = reason;
    in.Held = held;
    in.Read = cmWkQpHal;
    in.Context = req;
    in.BelowRead = cmWkQpVxd;
    in.BelowContext = w;
    in.Admit = cmWkQpAdmit;
    in.AdmitContext = w;
    bytes = CmQpciBuild(req->Buf, CM_QP_MAX_BYTES, &in);
    cmWkLog(w, "cm.query.pci.9x", (below << 16) | (held << 8) | reason);
    cmWkPciDone(req, bytes, reason);
}

PCM_CFGWK_PCI CmCfgwkPciAsk(PCM_CFGWK w, const CM_QPCI_IN *in,
                            PULONG reason)
{
    PCM_CFGWK_PCI req;
    KIRQL irql;

    *reason = CM_QP_NR_NO_WORKER;
    req = (PCM_CFGWK_PCI)CmAlloc(sizeof(CM_CFGWK_PCI));
    if (req == NULL) {
        return NULL;
    }
    KeInitializeSpinLock(&req->Lock);
    KeInitializeEvent(&req->Done, NotificationEvent, FALSE);
    req->Refs = 2;
    req->State = CM_WK_PCI_POSTED;
    req->In = *in;
    req->In.Read = NULL;
    req->In.Context = NULL;
    req->In.BelowRead = NULL;
    req->In.BelowContext = NULL;
    req->In.Admit = NULL;
    req->In.AdmitContext = NULL;
    KeAcquireSpinLock(&w->Lock, &irql);
    if (w->Exit || w->PciReq != NULL) {
        /* An earlier request still waiting for the worker: it is busy. */
        if (!w->Exit) {
            *reason = CM_QP_NR_BUSY;
        }
        KeReleaseSpinLock(&w->Lock, irql);
        CmFree(req);
        return NULL;
    }
    w->PciReq = req;
    (VOID)KeSetEvent(&w->Wake, IO_NO_INCREMENT, FALSE);
    KeReleaseSpinLock(&w->Lock, irql);
    return req;
}

ULONG CmCfgwkPciTake(PCM_CFGWK_PCI req, ULONG waitMs, UCHAR *out,
                     PULONG reason)
{
    LARGE_INTEGER t;
    KIRQL irql;
    ULONG bytes;
    ULONG i;

    /* -waitMs ms, relative, in 100 ns units, without 64-bit arithmetic
     * (waitMs is far below the 429,496 ms one ULONG holds). */
    t.LowPart = (ULONG)(0UL - waitMs * 10000UL);
    t.HighPart = -1;
    (VOID)KeWaitForSingleObject(&req->Done, Executive, KernelMode, FALSE,
                                &t);
    KeAcquireSpinLock(&req->Lock, &irql);
    if (req->State == CM_WK_PCI_DONE) {
        bytes = req->Bytes;
        *reason = req->Reason;
    } else {
        bytes = 0;
        *reason = CM_QP_NR_BUSY;
    }
    req->State = CM_WK_PCI_LEFT;
    KeReleaseSpinLock(&req->Lock, irql);
    /* Once DONE the worker writes Buf no more; the asker's reference keeps
     * it until the copy is made. */
    if (bytes > CM_QP_MAX_BYTES) {
        bytes = 0;
        *reason = CM_QP_NR_NO_WORKER;
    }
    for (i = 0; i < bytes; i++) {
        out[i] = req->Buf[i];
    }
    cmWkPciRelease(req);
    return bytes;
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
        /* Record 31 section 5.1: the start baseline's tunnel id, a
         * pre-boot tunnel's drive included. */
        cmWkTagWindow(w, w->Active);
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
    ULONG goneOnly;
    ULONG defer;
    ULONG trivial;
    ULONG seq[XHCI_TBT_MAX_TUNNELS];
    ULONG i;
    ULONG lostClaim;
    ULONG tagNew;
    PCM_CFGWK_PCI pci;
    BOOLEAN clean;
    BOOLEAN only;
    BOOLEAN none;
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
        w->Defer &= w->Active;
        w->DeferReenum &= w->Active;
        /* Record 28: a lost claim ends the member's no-IRQ state and its
         * journal (retired below, outside the lock; nothing freed). */
        lostClaim = w->NiClaimLost | (w->NiActive & ~w->Active);
        w->NiClaimLost = 0;
        w->NiActive = w->Active;
        take = 0;
        for (i = 0; i < w->Members; i++) {
            seq[i] = w->GoneSeq[i];
            if ((w->ReqGone & (1UL << i)) != 0 &&
                w->Port[i].TakenSeq != seq[i]) {
                take |= 1UL << i;
            }
        }
        gone = w->ReqGone;
        goneOnly = w->GoneOnly;
        if ((gone & ~take) != 0 && cmWkSince(w->GoneAt) < CM_WK_RETRY_MS) {
            /* An unresolved removal is tried again at the retry
             * interval; one just taken at once. */
            gone = take;
        }
        /* Record 21 section 6.1.2: a held removal waits for the hold's
         * end (taken below all the same, so its set is the loss's). */
        defer = w->Defer;
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
        pci = w->PciReq;
        w->PciReq = NULL;
        /* Record 31 section 5.1: members newly active, still active. */
        tagNew = w->TagReq & w->Active;
        w->TagReq = 0;
        w->InCall = 1;
        KeReleaseSpinLock(&w->Lock, irql);

        cmWkOp(w, CM_WK_NO_OP, 0);
        for (i = 0; i < w->Members; i++) {
            if ((lostClaim & (1UL << i)) != 0) {
                cmWkLog(w, "cm.reenum.9x.noirq.claim-lost", w->Port[i].Bdf);
                CmNiInit(&w->Port[i].Ni, w->Port[i].Bdf);
                w->Port[i].NiDiagSeen = 0;
            }
        }
        if (open) {
            cmWkOpen(w);
        }
        if (pci != NULL) {
            /* Reads alone, before this pass's operations: the asker's
             * wait is bounded. */
            cmWkPciServe(w, pci);
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
        /* A held removal with nothing to remove (no child retained, no
         * removal-only request merged) is resolved at once: no devnode is
         * named, so no closure is owed; its held re-enumeration is
         * dropped with it (below), but the member stays held (Defer), so
         * any later request on it waits for CmCfgwkRelease. Any other
         * held one waits. */
        trivial = 0;
        for (i = 0; i < w->Members; i++) {
            if ((defer & gone & (1UL << i)) != 0 &&
                w->Port[i].Gone.Count == 0 &&
                (goneOnly & (1UL << i)) == 0) {
                trivial |= 1UL << i;
            }
        }
        gone &= ~(defer & ~trivial);
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
                only = (BOOLEAN)((goneOnly & (1UL << i)) != 0);
                if (only) {
                    /* A removal-only request: whatever children the
                     * bridge has now are the old endpoint's. */
                    cmWkOp(w, i, gen[i]);
                    (VOID)cmWkGoneSeed(w, i, TRUE, &none);
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
                clean = FALSE;
                if (goneRc == CM_WK_GONE_DONE) {
                    w->C.GoneDone++;
                    /* Nothing refused went back to the retained set. */
                    clean = (BOOLEAN)(w->Port[i].Ret.Count == 0);
                    if (clean && only) {
                        /* And the bridge has no child left (a complete
                         * list read now), whatever the inventory said. */
                        cmWkOp(w, i, gen[i]);
                        clean = (BOOLEAN)(cmWkGoneSeed(w, i, FALSE, &none) &&
                                          none);
                    }
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
                    w->GoneOnly &= ~(1UL << i);
                    if (clean) {
                        w->GoneClean |= 1UL << i;
                    }
                    if ((trivial & (1UL << i)) != 0) {
                        /* Record 21 section 6.1.2: nothing was named,
                         * nothing is owed after it; the hold stands. */
                        w->DeferReenum &= ~(1UL << i);
                    }
                }
                KeReleaseSpinLock(&w->Lock, irql);
            }
            cmWkOp(w, CM_WK_NO_OP, 0);
        }
        if (go && w->OpenState == CM_CFGWK_OPEN_OK) {
            /* Task 43.47: parked claims retried (a removal every 2 s, a
             * refused query backed off, CmEjClaimRetry), before and
             * whatever the members' re-enumerations. */
            cmWkOp(w, CM_WK_NO_OP, 0);
            for (i = 0; i < w->Members && go; i++) {
                if (!CmEjectParkedAny(w->Port[i].Bdf)) {
                    continue;
                }
                cmWkLeave(w);
                go = cmWkEnter(w);
                if (go) {
                    cmWkParked(w, i);
                }
            }
        }
        if (go && w->OpenState == CM_CFGWK_OPEN_OK) {
            for (i = 0; i < w->Members; i++) {
                if ((reenum & (1UL << i)) == 0 ||
                    !cmWkCurrent(w, i, gen[i])) {
                    continue;
                }
                if (CmNiInTransaction(&w->Port[i].Ni)) {
                    /* Record 28: this driver's re-enumerations of the
                     * member queue behind its no-IRQ attempt (bounded by
                     * the attempt's budget); a loss aborts the attempt
                     * through its gate instead. */
                    failed |= 1UL << i;
                    failedVanish |= vanish & (1UL << i);
                    continue;
                }
                if (cmWkDeferOwed(w, i)) {
                    /* Held with the removal (record 21 section 6.1.2):
                     * no retry, no timer; dropped at the release. */
                    continue;
                }
                if (cmWkGoneOwed(w, i) || cmWkHeld(w, i) ||
                    CmEjectVanishHeld(w->Port[i].Bdf)) {
                    /* Its CONTROLLER_UNAVAILABLE removal is still owed,
                     * its barrier stands, or a vanished child of it is not
                     * retired (task 43.47): no re-enumeration before it,
                     * tried again later. */
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
            /* Record 31 section 5: the tunnel id below each member whose
             * window is open, once a tick. */
            cmWkTagWindow(w, tagNew);
            for (i = 0; i < w->Members && go; i++) {
                if ((w->TagOpen & (1UL << i)) == 0) {
                    continue;
                }
                cmWkLeave(w);
                go = cmWkEnter(w);
                if (!go) {
                    break;
                }
                cmWkOp(w, i, cmWkGenOf(w, i));
                if (cmWkGo(w)) {
                    cmWkTag(w, i);
                }
                if (cmWkSince(w->TagFrom[i]) >= CM_WK_MARK_MS) {
                    w->TagOpen &= ~(1UL << i);
                }
            }
            cmWkOp(w, CM_WK_NO_OP, 0);
            /* Record 28: each member's no-IRQ work, once a tick. */
            for (i = 0; i < w->Members && go; i++) {
                if (!CmNiBusy(&w->Port[i].Ni)) {
                    continue;
                }
                cmWkLeave(w);
                go = cmWkEnter(w);
                if (go) {
                    cmWkNoIrq(w, i);
                }
            }
            cmWkOp(w, CM_WK_NO_OP, 0);
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
                          (w->ReqGone & ~w->Defer) != 0 || w->TagOpen != 0);
        for (i = 0; i < w->Members && !timed; i++) {
            if (CmNiBusy(&w->Port[i].Ni) ||
                CmEjectParkedAny(w->Port[i].Bdf)) {
                timed = TRUE;
            }
        }
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
    /* No asking after Exit (CmCfgwkPciAsk); one asked before it is
     * answered with nothing. */
    pci = w->PciReq;
    w->PciReq = NULL;
    KeReleaseSpinLock(&w->Lock, irql);
    if (pci != NULL) {
        cmWkPciDone(pci, 0, CM_QP_NR_NO_WORKER);
    }
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
                       ULONG active, ULONG numbering, ULONG noIrq)
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
        CmNiInit(&w->Port[i].Ni, bdf[i]);
    }
    w->Active = active;
    w->Numbering = numbering;
    w->NoIrq = noIrq;
    w->NiActive = active;
    w->NiBase = cmWkNowTicks();
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
    /* A claim lost, however soon regained, ends the member's no-IRQ
     * state: latched for the worker (record 28, "a lost claim"). */
    w->NiClaimLost |= w->Active & ~active;
    /* Record 31 section 5.1: a member newly active opens its tunnel id
     * window (a pre-boot tunnel's drive, once the member is verified). */
    w->TagReq |= active & ~w->Active;
    w->Active = active;
    if (mark) {
        w->ReqMark = 1;
        w->MarkWanted = 1;
    }
    KeSetEvent(&w->Wake, IO_NO_INCREMENT, FALSE);
    KeReleaseSpinLock(&w->Lock, irql);
}

static VOID cmWkGoneReq(PCM_CFGWK w, ULONG members, ULONG active,
                        BOOLEAN reenum, BOOLEAN defer);

VOID CmCfgwkUnavailable(PCM_CFGWK w, ULONG active, BOOLEAN defer)
{
    cmWkGoneReq(w, active, active, TRUE, defer);
}

VOID CmCfgwkGone(PCM_CFGWK w, ULONG members, ULONG active)
{
    /* Removal only: no re-enumeration is asked (the barrier's member is
     * re-enumerated by the change that lifts it). On a held member it
     * merges into the hold and waits with it (record 21 section 6.1.2). */
    cmWkGoneReq(w, members, active, FALSE, FALSE);
}

VOID CmCfgwkRelease(PCM_CFGWK w, ULONG members)
{
    KIRQL irql;

    KeAcquireSpinLock(&w->Lock, &irql);
    /* The hold on `members` ended: the worker now makes the removal. The
     * held re-enumeration is dropped, not posted: a released member's
     * tunnel was drained, and the rebuild's own post re-enumerates it
     * with the rebuilt bridges numbered first (record 21 sections 6.1.1
     * and 6.1.2) - this no-scan one, retried until the barrier lifted,
     * could run before that post and let PCI.VXD meet them unnumbered
     * (read on 98 SE: problem 29) - or vacant, with nothing to bring
     * back. */
    members &= w->Defer;
    w->DeferReenum &= ~members;
    w->Defer &= ~members;
    KeSetEvent(&w->Wake, IO_NO_INCREMENT, FALSE);
    KeReleaseSpinLock(&w->Lock, irql);
}

VOID CmCfgwkTakeover(PCM_CFGWK w, ULONG members)
{
    KIRQL irql;

    KeAcquireSpinLock(&w->Lock, &irql);
    w->Takeover = members;
    KeReleaseSpinLock(&w->Lock, irql);
}

VOID CmCfgwkHold(PCM_CFGWK w, ULONG members)
{
    KIRQL irql;

    KeAcquireSpinLock(&w->Lock, &irql);
    /* Record 21 section 6.1.2: a member leaves the hold once it is past
     * the commit, its retirement and its tunnel's rebuild (the caller
     * holds it until then), so its next re-enumeration reads
     * configuration space again and numbers the rebuilt bridges before
     * PCI.VXD meets them (section 6.1.1), whichever request it is. */
    w->NoScan &= ~(w->Hold & ~members);
    w->Hold = members;
    KeSetEvent(&w->Wake, IO_NO_INCREMENT, FALSE);
    KeReleaseSpinLock(&w->Lock, irql);
}

static VOID cmWkGoneReq(PCM_CFGWK w, ULONG members, ULONG active,
                        BOOLEAN reenum, BOOLEAN defer)
{
    KIRQL irql;
    ULONG i;

    KeAcquireSpinLock(&w->Lock, &irql);
    w->NiClaimLost |= w->Active & ~active;
    /* Record 31 section 5.1: a member newly active opens its tunnel id
     * window (a pre-boot tunnel's drive, once the member is verified). */
    w->TagReq |= active & ~w->Active;
    w->Active = active;
    members &= active;
    w->ReqGone |= members;
    w->GoneClean &= ~members;
    for (i = 0; i < w->Members; i++) {
        if (members & (1UL << i)) {
            w->GoneSeq[i]++;
        }
    }
    /* The re-enumeration after the removal reads no configuration space:
     * no vanished-child scan, here or in a coalesced earlier request. A
     * held removal keeps its re-enumeration with it until the hold ends
     * (record 21 section 6.1.2); a request merged into a held one (the
     * barrier's removal-only request) waits with it - only
     * CmCfgwkRelease ends a hold. */
    if (defer) {
        w->Defer |= members;
    }
    if ((w->Defer & members) != 0) {
        /* A re-enumeration already queued for a held member waits with
         * the hold too. */
        w->DeferReenum |= w->ReqReenum & w->Defer & members;
        w->ReqReenum &= ~(w->Defer & members);
        if (reenum) {
            w->DeferReenum |= w->Defer & members;
        }
    }
    if (reenum) {
        w->ReqReenum |= members & ~w->Defer;
    } else {
        w->GoneOnly |= members;
    }
    w->ReqVanish &= ~members;
    w->NoScan |= members;
    w->Retry &= ~members;
    w->RetryVanish &= ~members;
    for (i = 0; i < w->Members; i++) {
        if (members & (1UL << i)) {
            w->Gen[i]++;
        }
    }
    KeSetEvent(&w->Wake, IO_NO_INCREMENT, FALSE);
    KeReleaseSpinLock(&w->Lock, irql);
}

ULONG CmCfgwkRetired(PCM_CFGWK w)
{
    KIRQL irql;
    ULONG done;

    KeAcquireSpinLock(&w->Lock, &irql);
    done = w->GoneClean & ~w->ReqGone;
    KeReleaseSpinLock(&w->Lock, irql);
    return done;
}

ULONG CmCfgwkEnumBusy(PCM_CFGWK w)
{
    KIRQL irql;
    ULONG busy;

    KeAcquireSpinLock(&w->Lock, &irql);
    busy = w->EnumBusy;
    KeReleaseSpinLock(&w->Lock, irql);
    return busy;
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
    cmWkNiCounters(w, counters);
    CmCfgmgClose(&w->Cfgmg);
    CmFree(w);
}

/* The worker's counts so far (cm_reenum.h). IRQL: <= DISPATCH_LEVEL, w->Lock
 * not held; the caller (the connection-manager thread) owns w's lifetime. */
VOID CmCfgwkCounters(PCM_CFGWK w, PCM_CFGWK_COUNTERS counters)
{
    KIRQL irql;

    KeAcquireSpinLock(&w->Lock, &irql);
    *counters = w->C;
    KeReleaseSpinLock(&w->Lock, irql);
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
