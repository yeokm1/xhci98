/*
 * cm_reenum.h - re-enumeration and safe eject (design record 21 sections
 * 3.2, 6 and 7.4; roadmap-usb4.md tasks 39-A.5, driver half, and 39-A.8),
 * the seam between the NHI's connection-manager thread and the operating
 * system's view of what a tunnel changed. cm_reenum.c implements it; the
 * NHI function driver (cm_nhi.c, cm_power.c, task 39-A.4) and, later, the
 * USB4 software connection manager (design record 22, task 40-A.9) only
 * call it.
 *
 * ONE DOMAIN PER NHI. A domain is an NHI and the fabric behind it; its
 * state is CM_REENUM, embedded in CM_NHI. Every call names the domain by
 * its CM_NHI.
 *
 * THE NHI'S CALLS, in their order over one start:
 *
 *   CmReenumStart    once per start, after the selection said a
 *                    connection manager runs and before the bind sends
 *                    anything: discover the domain's tunnel-port set
 *                    (record 21 section 6.3, through tbt_topo.c), publish
 *                    its claims in the registration list, decide exclusive
 *                    ownership against every other domain, and mark the
 *                    exclusive members active (section 3.2). On Windows 98
 *                    SE and ME also open the CONFIGMG route and run its
 *                    guards (section 6.1). Reads, from CM_NHI: Common.Pdo
 *                    (the NHI's own location, IoGetDeviceProperty),
 *                    Vendor and Device (the ids the start read from
 *                    configuration space; they choose the family's id
 *                    lists of section 3.3), Values (XhciCmReenum,
 *                    XhciCmSafeEject, XhciCmTunnelBridge) and Icm.
 *   CmReenumAction   once per TBT_ICM_ACTION the client queued
 *                    (TbtIcmNextAction), oldest first: the tunnel-up or
 *                    tunnel-down sequence of section 6.5 on every active
 *                    member, or a RESYNC of the whole set (both
 *                    sequences' re-enumeration, no poll). Called for every
 *                    action whatever XhciCmReenum says: at 0 (section 8)
 *                    it makes no re-enumeration call and counts the action
 *                    as not done, but still voids the domain's pending
 *                    ejects, since the tunnel changed.
 *   CmReenumService  once per pass of the thread (each idle poll), after
 *                    the actions are drained: the eject notifications the
 *                    child filters posted (section 7.4, record 22 section
 *                    10.4's per-port entry), and the members whose filter
 *                    published or took its baseline since the last pass.
 *                    Cheap when there is nothing to do.
 *   CmReenumSuspend  at the system suspend's D3, before the client sends
 *                    anything to the firmware: the held mark on every
 *                    active member (section 6.3, resume ordering), and the
 *                    filters' watchdog: a fixed five minutes
 *                    (CM_PORTS_HOLD_MAX_MS, below the power-IRP watchdog),
 *                    past which the failure path runs; the client's own
 *                    resume bound is computed and logged as a diagnostic
 *                    only.
 *   CmReenumResumed  at the D0 after it, once the client's resume and
 *                    reconciliation have run (ok TRUE) or failed (ok
 *                    FALSE): release the held members' retained D0 IRPs;
 *                    on failure also ask each member's filter for an
 *                    invalidation (section 6.3's failure path).
 *   CmReenumStop     at stop, surprise removal, removal and shutdown,
 *                    before the client's unbind: release every held member
 *                    by the failure path, clear the active marks, withdraw
 *                    the domain's claims (which may make another domain's
 *                    port exclusive again) and close the CONFIGMG route.
 *                    Idempotent.
 *
 * THE SOFTWARE CONNECTION MANAGER'S CALLS (record 22, Phase 40):
 *
 *   CmReenumSetEject the per-port eject entry of record 22 section 10.4
 *                    (decision U9): `hook` is called on the thread from
 *                    CmReenumService with the tunnel port's location
 *                    (XHCI_TBT_BDF) after the children of that verified
 *                    port were removed in an orderly way (a query-remove
 *                    then a remove, never a surprise removal). NULL (the
 *                    default, and the ICM client's choice: under the
 *                    firmware connection manager there is no per-device
 *                    teardown, record 21 section 7.4) logs the eject and
 *                    leaves the tunnel up.
 *   CmReenumPorts    the domain's active members, for a caller that
 *                    tunnels to one port at a time and wants the PCI side
 *                    of its correspondence (record 22 section 7.6).
 *
 * THE USB4 PROFILE (task 40-A.6; CM_NHI.U4On). The NHI hands every
 * TBT_U4_ACTION to CmReenumAction in the TBT_ICM_ACTION form (cm_nhi.c,
 * cmU4ToIcm): Kind TUNNEL_UP, TUNNEL_DOWN or RESYNC (a router removed below
 * depth 1 is a RESYNC), Link the USB4 port (TBT_U4_PORT.Index, 1-based),
 * Serial its connection generation, Why CM_U4_WHY_BASE (80h) or the USB4
 * kind; no ICM table entry stands behind it. CmReenumStart is called from
 * the bind, after TbtU4Start, so that the host router's mapping answer is
 * known. An implementation, for both profiles:
 *   - calls CmNhiWaitService(nhi) and CmNhiActionLive(nhi, action) where
 *     this contract says TbtIcmService and TbtIcmActionLive (they forward
 *     to those under the ICM client);
 *   - may act on the one member CmNhiActionMember(nhi, action) returns,
 *     the index in Topo.Set of the port's own bridge (record 22 section
 *     7.6, router-named or learned); the whole set is always correct, and
 *     is what XHCI_TBT_NO_MEMBER (always, under the ICM client) asks for;
 *   - calls CmNhiNamePorts(nhi) after each XhciTbtTopoFind it acts on and
 *     before it fills Port[] from Topo.Set and publishes the claims: the
 *     router-named members join the set there (XhciTbtTopoAddNamed, which
 *     may void the discovery INCOMPLETE or MANY: discover again), and
 *     CmNhiSetChanged(nhi) once the set and its claims are rebuilt, so
 *     the learning baseline is taken again over the set as it now is;
 *   - keeps Port[i] for Topo.Set[i], i below Topo.Tunnels, its Flags'
 *     CM_PORT_ACTIVE read by the NHI's learning (XhciTbtLearn's `active`)
 *     and its eject hook's bridge-to-port lookup.
 * Under the USB4 profile the NHI hands every action to CmReenumAction
 * whatever XhciCmReenum says (at 0 the seam re-enumerates nothing), and the
 * seam voids every eject notification pending on the domain at any action
 * (the port's connection changed: its successor's children were never
 * removed in order); the NHI's eject hook also refuses a port whose
 * connection generation is no longer the one last handed to the seam.
 * The NHI keeps the correspondence (CM_NHI.Binds) and drives the learning
 * itself. At a suspend under U4On, CM_NHI.U4ResumeBoundMs holds the USB4
 * client's resume bound (TbtU4ResumeBoundUs for the most obligations a
 * domain holds, rounded up) before CmReenumSuspend is called; the hold's
 * watchdog stays the fixed five-minute outer cap on every NT target
 * (CmPortsHoldWatchdog): that bound plus record 21's 10 s is logged when it
 * would pass the cap, and a client still working when the cap ends takes
 * the failure path. CM_NHI.Icm is zeroed then, so nothing of it may be taken as the
 * USB4 client's bound. CmReenumResumed's `ok` is FALSE whenever an
 * obligation was not met, though the domain keeps running.
 *
 * Removal on Windows 98 and ME: every Query_Remove_SubTree and
 * Remove_SubTree of a tunnel port's child is this seam's own (cm_reenum.c),
 * for both profiles; the NHI never calls CONFIGMG, and its eject hook runs
 * only after a removal the seam made in order (a refused query-remove is
 * respected and the hook is not called).
 *
 * CONTRACT. Every call above runs on the NHI's connection-manager thread
 * at PASSIVE_LEVEL with no lock held, serialised with the ICM traffic
 * (section 6.5); none is called concurrently with another for the same
 * domain. While it waits - the tunnel-up and tunnel-down polls of section
 * 6.5, at most 2 s each - an implementation calls TbtIcmService(&nhi->Icm)
 * between its reads and TbtIcmActionLive(&nhi->Icm, action) to learn
 * whether the device left meanwhile; it never calls TbtIcmPoll or
 * TbtIcmNextAction (the caller drains the queue) and never sends on the
 * ring. Inside, on Windows 98 SE and ME (IoIsWdmVersionAvailable(1, 0x10)
 * FALSE), the CONFIGMG route of section 6.1 runs, which must come from a
 * passive thread of the system VM - this one; on the NT targets the
 * bridge filters' registration list of section 3.2. CmReenumStart must
 * have been called before any other call but CmReenumStop and
 * CmReenumSetEject; CmReenumStop is owed for every CmReenumStart.
 *
 * THE FILTERS' CALLS (cm_filt.c, NT only) are the registration list's
 * protocol of section 3.2 and are declared at the end; they run in the
 * filters' PnP and power paths at the IRQL each one states.
 *
 * IRQL: PASSIVE_LEVEL, on the connection-manager thread, unless stated.
 */

#ifndef CM_REENUM_H
#define CM_REENUM_H

#include "tbt_icm.h"
#include "tbt_topo.h"

/* cm.h's CM_NHI and CM_FILTER; the calls take them by their structure tags
 * so that this header stands before cm.h's definitions. */
struct _CM_NHI;
struct _CM_FILTER;

/* The per-port eject entry (record 22 section 10.4). `port` is the tunnel
 * port's XHCI_TBT_BDF. PASSIVE_LEVEL, on the connection-manager thread,
 * no lock held. */
typedef VOID (*CM_REENUM_EJECT)(PVOID context, ULONG port);

/* CM_REENUM.Route: how this domain reaches the operating system. */
#define CM_ROUTE_NONE       0UL /* re-enumeration cannot be done (6.4)    */
#define CM_ROUTE_FILTER     1UL /* NT: the bridge filters (6.2)           */
#define CM_ROUTE_CONFIGMG   2UL /* 98 SE, ME: CONFIGMG (6.1)              */

/* CM_REENUM.Why when Route is NONE (record 21 section 6.4, the log). */
#define CM_NOROUTE_NONE         0UL
#define CM_NOROUTE_NO_SET       1UL /* the discovery found no member      */
#define CM_NOROUTE_ALL_CLAIMED  2UL /* every member claimed by another
                                       domain as well                     */
#define CM_NOROUTE_CFGMG_VER    3UL /* Get_Version's high byte not 4      */
#define CM_NOROUTE_CFGMG_ROOT   4UL /* the root devnode's id did not read
                                       back                               */
#define CM_NOROUTE_CFGMG_THUNK  5UL /* no non-paged thunk                 */
#define CM_NOROUTE_NO_LOCATION  6UL /* the NHI's own location unread      */
#define CM_NOROUTE_ARCH         7UL /* CONFIGMG on a build with no thunk  */

/* CM_REENUM_PORT.Flags. */
#define CM_PORT_EXCLUSIVE   0x01UL  /* no other domain claims it          */
#define CM_PORT_ACTIVE      0x02UL  /* exclusive, and (NT) its filter has
                                       published and taken its baseline   */
#define CM_PORT_HELD        0x04UL  /* held for the resume ordering       */
#define CM_PORT_EJECTED     0x08UL  /* an eject notification is pending   */
#define CM_PORT_NO_FILTER   0x10UL  /* NT: no filter published for it (the
                                       INF did not match its id): no
                                       re-enumeration, no safe eject      */

/* One member of the set, the per-member state of record 21 section 6.3.
 * Flags is written under the registration list's lock; the rest by the
 * thread alone. */
typedef struct _CM_REENUM_PORT {
    ULONG Bdf;              /* XHCI_TBT_BDF                               */
    ULONG Source;           /* XHCI_TBT_SRC_*                             */
    ULONG Flags;            /* CM_PORT_*                                  */
    ULONG Last;             /* the last XHCI_TBT_PORT_* read              */
    ULONG Reenums;          /* re-enumerations asked for it               */
    ULONG LastStatus;       /* the last call's NTSTATUS or CONFIGRET      */
    ULONG Devnode;          /* 98: the bridge devnode, 0 until found      */
    ULONG Marked;           /* 98: children marked removable              */
    ULONG AskedSerial;      /* NT: the filter publication a baseline was
                               last asked of                              */
    ULONG MissedAt;         /* NowMs of the last retry of Missed          */
    ULONG Missed;           /* a tunnel change came while the member was
                               exclusive but not active, or its call could
                               not be made: re-enumerated once it is      */
    ULONG EjectSerial;      /* the eject's bridge publication and          */
    ULONG EjectGen;         /* population generation, checked at hand-off */
    ULONG WalkLogged;       /* 98: a failed devnode walk was logged       */
    ULONG PathRead;         /* 98: Path read (cmPciPath)                  */
    ULONG PathCount;        /* 98: its elements, 0 for none               */
    ULONG Path[16];         /* 98: the instance path, cm_ports.h's form   */
} CM_REENUM_PORT, *PCM_REENUM_PORT;

/* Children of one tunnel port remembered on 98 as already seen, so that
 * a re-enumeration marks only what is new (section 7.4). */
#define CM_REENUM_MAX_KNOWN 16UL

/* CM_CFGMG.Why: why the CONFIGMG route did not open. */
#define CM_CFGMG_WHY_NONE       0UL
#define CM_CFGMG_WHY_THUNK      1UL /* no non-paged block for the thunks */
#define CM_CFGMG_WHY_VERSION    2UL /* Get_Version's high byte not 4     */
#define CM_CFGMG_WHY_ROOT       3UL /* the root devnode did not read back */
#define CM_CFGMG_WHY_ARCH       4UL /* a build with no thunk (amd64)     */

/* The CONFIGMG route of one domain (cm_cfgmg.c), Windows 98 SE and ME. */
typedef struct _CM_CFGMG {
    PUCHAR Thunks;          /* non-paged; the VMM patches it in place     */
    ULONG Open;             /* both guards passed                         */
    ULONG Why;              /* CM_CFGMG_WHY_*                             */
    ULONG Version;          /* Get_Version's AX                           */
    ULONG Root;             /* the root devnode                           */
} CM_CFGMG, *PCM_CFGMG;

/* cm_cfgmg.c. Every call but Open and Close returns the service's
 * CONFIGRET, or CR_FAILURE (13h) where the route is not open.
 * PASSIVE_LEVEL, on the connection-manager thread, Windows 98 SE and ME
 * only. */
BOOLEAN CmCfgmgOpen(PCM_CFGMG cfg);
VOID CmCfgmgClose(PCM_CFGMG cfg);
ULONG CmCfgmgChild(PCM_CFGMG cfg, ULONG devnode, PULONG child);
ULONG CmCfgmgSibling(PCM_CFGMG cfg, ULONG devnode, PULONG sibling);
ULONG CmCfgmgDeviceId(PCM_CFGMG cfg, ULONG devnode, char *buffer,
                      ULONG bytes);
ULONG CmCfgmgQueryRemove(PCM_CFGMG cfg, ULONG devnode);
ULONG CmCfgmgRemove(PCM_CFGMG cfg, ULONG devnode);
ULONG CmCfgmgReenumerate(PCM_CFGMG cfg, ULONG devnode);
ULONG CmCfgmgStatus(PCM_CFGMG cfg, ULONG devnode, PULONG status,
                    PULONG problem);
ULONG CmCfgmgAddRemovable(PCM_CFGMG cfg, ULONG devnode);

typedef struct _CM_REENUM {
    /* The registration list's link (cm_reenum.c), and whether this domain
     * is on it. Under the list's lock. */
    LIST_ENTRY Link;
    ULONG Listed;

    ULONG Route;            /* CM_ROUTE_*                                 */
    ULONG Why;              /* CM_NOROUTE_* when Route is NONE            */
    ULONG Nhi;              /* the NHI's XHCI_TBT_BDF, or XHCI_TBT_NO_BDF */
    ULONG Reenum;           /* XhciCmReenum as read                        */
    ULONG SafeEject;        /* XhciCmSafeEject as read                     */
    ULONG Members;
    CM_REENUM_PORT Port[XHCI_TBT_MAX_TUNNELS];

    /* Eject notifications posted by child filters, one bit per Port[]
     * index; under the list's lock. */
    ULONG EjectPending;
    CM_REENUM_EJECT EjectHook;
    PVOID EjectContext;

    /* The filters' watchdog for a held D0 IRP, ms (CmReenumSuspend). */
    ULONG HoldBoundMs;

    /* 98: the CONFIGMG route, each port's children already seen, and the
     * window after a tunnel up in which CmReenumService looks for new
     * children to mark (the re-enumeration is not synchronous). */
    CM_CFGMG Cfgmg;         /* unused since the worker (cm_cfgwk.c)       */
    ULONG CfgmgOwed;        /* the worker starts at the first Action or
                               Service, after the NHI's start completed  */
    struct _CM_CFGWK *Wk;   /* 98: the CONFIGMG worker (cm_cfgwk.c)       */
    ULONG WkOpenSeen;       /* its opening read back                      */
    ULONG WkActive;         /* the owned-member mask it was last given    */
    ULONG RediscoverLeft;   /* an empty set's discoveries still to try    */
    ULONG DebtPending;      /* a tunnel change came while it was empty    */
    ULONG RediscoverAt;     /* NowMs of the last                          */
    ULONG Known[XHCI_TBT_MAX_TUNNELS][CM_REENUM_MAX_KNOWN];
    ULONG KnownCount[XHCI_TBT_MAX_TUNNELS];
    ULONG MarkFrom;         /* NowMs when the window opened               */
    ULONG MarkOpen;

    /* tbt_topo.c's discovery and port state (too large for a stack). */
    XHCI_TBT_TOPO Topo;
    XHCI_TBT_CFG Cfg;
    XHCI_TBT_POLL Poll;
    XHCI_TBT_PORT Reading;

    /* Counters, emitted at stop as cm.reenum.* lines (record 21 section
     * 8). */
    ULONG Starts;
    ULONG Stops;
    ULONG Ups;
    ULONG Downs;
    ULONG Resyncs;
    ULONG Suspends;
    ULONG Resumes;
    ULONG ResumeFailures;
    ULONG NotDone;          /* actions with no route (section 6.4)        */
    ULONG Calls;            /* invalidations or Reenumerate_DevNode made  */
    ULONG CallFailures;
    ULONG Abandoned;        /* tunnel-up polls a disconnect ended         */
    ULONG PollTimeouts;
    ULONG Removed;          /* 98: vanished children removed             */
    ULONG Vetoed;           /* 98: Query_Remove_SubTree refused           */
    ULONG Marked;           /* 98: DN_REMOVABLE set                       */
    ULONG MarkFailures;
    ULONG LossDrain;        /* between CmReenumLossBegin and its end: the
                               actions drained are the loss's            */
    ULONG LossSeen;         /* a loss came with no 98 worker running: none
                               is started and no rediscovery made for this
                               instance (a baseline or a discovery would
                               be read from a lost tree)                 */
    ULONG Unavailable;      /* 98: CONTROLLER_UNAVAILABLE requests posted */
    ULONG GoneDone;         /* 98: ... members whose retained children
                               were reconciled                           */
    ULONG GoneDropped;      /* 98: retained children no longer matching
                               their recorded identity, left alone       */
    ULONG Ejects;           /* eject notifications served                 */
    ULONG EjectsStale;      /* dropped at hand-off: the port changed      */
} CM_REENUM, *PCM_REENUM;

VOID CmReenumStart(struct _CM_NHI *nhi);
VOID CmReenumAction(struct _CM_NHI *nhi, const TBT_ICM_ACTION *action);
VOID CmReenumService(struct _CM_NHI *nhi);
VOID CmReenumSuspend(struct _CM_NHI *nhi);
VOID CmReenumResumed(struct _CM_NHI *nhi, BOOLEAN ok);
/* A resume the thread will not run (the D0 IRP failed below, or no
 * thread to queue it to): every held member released by the failure path
 * at once, so no bridge's D0 waits for the watchdog. The exception to the
 * thread contract: any context at <= DISPATCH_LEVEL, idempotent, and
 * harmless where nothing is held. IRQL: <= DISPATCH_LEVEL, any context
 * (cm_power.c's D0 completion calls it). */
VOID CmReenumResumeFailed(struct _CM_NHI *nhi);
VOID CmReenumStop(struct _CM_NHI *nhi);
/*
 * The host interface lost (cm_live.h, CM_LIVE_ACT_RECONCILE) in that
 * episode: called once per episode after the client's TUNNEL_DOWN and
 * RESYNC actions (why TBT_ICM_WHY_UNAVAILABLE) were handed over. On NT
 * those actions' invalidations are the reconciliation and this records the
 * episode; on 98 SE and ME it is the CONFIGMG worker's CONTROLLER_UNAVAILABLE
 * route, which removes the children recorded at tunnel up without reading
 * the bridge headers the loss may have reset. Writes nothing to the NHI.
 * IRQL: PASSIVE_LEVEL, the connection-manager thread.
 */
VOID CmReenumUnavailable(struct _CM_NHI *nhi, ULONG episode);
/*
 * The bracket around a loss's drain (cm_nhi.c CmNhiLost, cm_power.c's
 * failed resume): CmReenumLossBegin right after TbtIcmLost, before any
 * drain; CmReenumUnavailable (or CmReenumLossEnd where no request is owed
 * this episode) after the drain. In between, on the CONFIGMG route, every
 * action handed over - those TbtIcmLost queued and those queued before it
 * - is counted and voids pending ejects, and nothing more: no bridge
 * polled, no request posted; the worker's CONTROLLER_UNAVAILABLE request
 * is the reconciliation. On NT the actions run as always. IRQL:
 * PASSIVE_LEVEL, the connection-manager thread.
 */
VOID CmReenumLossBegin(struct _CM_NHI *nhi);
VOID CmReenumLossEnd(struct _CM_NHI *nhi);

VOID CmReenumSetEject(struct _CM_NHI *nhi, CM_REENUM_EJECT hook,
                      PVOID context);
/* Writes up to `capacity` active members' locations into `ports` and
 * returns how many were written. */
ULONG CmReenumPorts(struct _CM_NHI *nhi, PULONG ports, ULONG capacity);

/* ---- Windows 98 SE and ME: the CONFIGMG worker (cm_cfgwk.c) --------- */

/*
 * Every CONFIGMG call of a 98 domain is made on a worker thread of its
 * own, never on the connection-manager thread: a CONFIGMG service blocks
 * while the configuration manager is busy (an Add New Hardware wizard
 * open, read on 98 SE), and the connection manager must keep servicing its
 * ring meanwhile. The connection-manager thread posts requests, collapsed
 * per member (bit i of a mask is member i), and reads the route's opening
 * back.
 */
#define CM_CFGWK_OPEN_PENDING   0UL
#define CM_CFGWK_OPEN_OK        1UL
#define CM_CFGWK_OPEN_FAILED    2UL

typedef struct _CM_CFGWK_COUNTERS {
    ULONG Calls;            /* Reenumerate_DevNode made                   */
    ULONG CallFailures;     /* ... failed, or no devnode for the bridge  */
    ULONG Removed;          /* vanished children removed                  */
    ULONG Vetoed;           /* Query_Remove_SubTree refused, respected    */
    ULONG Marked;           /* DN_REMOVABLE added                         */
    ULONG MarkFailures;
    ULONG Detached;         /* the stop found it inside a call            */
    ULONG GoneDone;         /* CONTROLLER_UNAVAILABLE members reconciled  */
    ULONG GoneDropped;      /* retained children whose identity changed   */
} CM_CFGWK_COUNTERS, *PCM_CFGWK_COUNTERS;

/* Creates the worker for `members` bridges at bdf[] (members the domain
 * owns in `active`) and asks it to open the route. NULL on failure.
 * PASSIVE_LEVEL. */
struct _CM_CFGWK *CmCfgwkStart(struct _CM_NHI *nhi, const ULONG *bdf,
                               ULONG members, ULONG active);

/* Asks for the members in `reenum` to be re-enumerated (those also in
 * `vanish` with their vanished children removed first) and, with `mark`,
 * for a marking window; `active` replaces the owned members. Returns at
 * once. <= DISPATCH_LEVEL. */
VOID CmCfgwkPost(struct _CM_CFGWK *w, ULONG reenum, ULONG vanish,
                 ULONG active, BOOLEAN mark);

/* CM_CFGWK_OPEN_*, with CM_CFGMG_WHY_* and Get_Version's AX. <=
 * DISPATCH_LEVEL. */
ULONG CmCfgwkOpenState(struct _CM_CFGWK *w, PULONG why, PULONG version);
/* A walk of every member forgetting the known children no longer present
 * (a tunnel down: a child the user removed is gone by then). */
VOID CmCfgwkPrune(struct _CM_CFGWK *w);

/* CONTROLLER_UNAVAILABLE (CmReenumUnavailable): each member in `active`
 * owes the removal of the children retained at its last healthy walk,
 * revalidated by their recorded instance ids and made without reading a
 * bridge header, before any further re-enumeration of it; then a
 * re-enumeration with vanished children (a request as CmCfgwkPost's, so
 * an operation in progress on the member goes stale). `active` replaces
 * the owned members. Returns at once. <= DISPATCH_LEVEL. */
VOID CmCfgwkUnavailable(struct _CM_CFGWK *w, ULONG active);

/* Ends the worker: waits for it when it is between calls (and frees it);
 * when it is inside a CONFIGMG call, which may be waiting for this very
 * stop, leaves it to free itself. `counters` gets its counts (Detached
 * set, the rest zero, in the second case). PASSIVE_LEVEL. */
VOID CmCfgwkStop(struct _CM_CFGWK *w, PCM_CFGWK_COUNTERS counters);

/* The running worker's counts so far, copied under its lock, each word as
 * last written (for the query device's counters, design record 25 section
 * 4.5): what CmCfgwkStop will add to the NHI's at the stop. From the
 * connection-manager thread, which owns the worker's lifetime.
 * <= DISPATCH_LEVEL. */
VOID CmCfgwkCounters(struct _CM_CFGWK *w, PCM_CFGWK_COUNTERS counters);

/* From the driver's unload: waits, bounded, for every worker to have
 * left. PASSIVE_LEVEL. */
VOID CmCfgwkDrain(VOID);

/* ---- the registration list, for cm_filt.c (NT only) ------------------ */

/* Once, from DriverEntry, before any AddDevice. Any IRQL. */
VOID CmRegInit(VOID);

/* The registration fields of a new bridge or child filter's extension,
 * from its AddDevice or creation, before it is attached. Any IRQL. */
VOID CmRegFilterInit(struct _CM_FILTER *filter);

/* The registration list's counters, for the NHI's log at stop. */
VOID CmRegCounters(PULONG attached, PULONG over, PULONG skipped,
                   PULONG refused, PULONG watchdogs);

/* The bridge filter's start has completed below it successfully: publish
 * its entry under its bus, device and function (IoGetDeviceProperty). A
 * filter whose location cannot be read publishes nothing. PASSIVE_LEVEL. */
VOID CmRegPublish(struct _CM_FILTER *filter);

/* Stop, surprise removal, removal: withdraw the entry, release a retained
 * D0 IRP by the failure path, and wait for every call in flight on it to
 * drain. Idempotent. PASSIVE_LEVEL. */
VOID CmRegWithdraw(struct _CM_FILTER *filter);

/* A BusRelations request completed below a bridge filter (successfully):
 * keep the snapshot, set the baseline, and attach a child filter to each
 * PDO not in the previous snapshot where the bridge is active and safe
 * eject is on. `relations` may be NULL (no children). PASSIVE_LEVEL, in
 * the dispatch routine after the completion wait. */
VOID CmRegRelations(struct _CM_FILTER *filter,
                    const DEVICE_RELATIONS *relations, ULONG seq);

/* Before a BusRelations request is passed down: the bridge's tunnel-change
 * sequence, handed to CmRegRelations with the answer. PASSIVE_LEVEL. */
ULONG CmRegRelationsBegin(struct _CM_FILTER *filter);

/* A device IRP_MN_SET_POWER to D0 on a bridge filter: TRUE when it was
 * passed down with the hold's completion routine (the caller returns
 * STATUS_PENDING), FALSE when the caller passes it down as usual.
 * <= DISPATCH_LEVEL. */
BOOLEAN CmRegHoldD0(struct _CM_FILTER *filter, PIRP irp);

/* A child filter's device has been removed, its REMOVE_DEVICE passed
 * down and finished below; `ejected` when a query-remove preceded it and
 * no surprise removal did. Once no live child stack and no unwatched
 * device is left on the port, an orderly removal seen there is posted to
 * the owning domain as an eject. PASSIVE_LEVEL. */
VOID CmRegChildRemoved(struct _CM_FILTER *child, BOOLEAN ejected);

/* NT: `width` bytes (1, 2 or 4) of `value` written at `offset` (0x40 or
 * above: a capability, never the header) of the configuration space of
 * this domain's member `bdf`, by IRP_MN_WRITE_CONFIG to its bridge's PDO
 * through the filter's registration (the SHPC DWORD Select byte of the
 * stor98 channel's reading, cm_nvlinkos.c). FALSE where the member has no
 * published filter (Windows 98 SE and ME have none). PASSIVE_LEVEL. */
BOOLEAN CmRegConfigWrite(struct _CM_NHI *nhi, ULONG bdf, ULONG offset,
                         ULONG width, ULONG value);

#endif /* CM_REENUM_H */
