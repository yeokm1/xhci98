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
 *                    sequences' re-enumeration, no poll). The NHI does not
 *                    call it while XhciCmReenum is 0 (section 8) and
 *                    counts the action as skipped; called anyway, it
 *                    re-enumerates nothing then either.
 *   CmReenumService  once per pass of the thread (each idle poll), after
 *                    the actions are drained: the eject notifications the
 *                    child filters posted (section 7.4, record 22 section
 *                    10.4's per-port entry), and the members whose filter
 *                    published or took its baseline since the last pass.
 *                    Cheap when there is nothing to do.
 *   CmReenumSuspend  at the system suspend's D3, before the client sends
 *                    anything to the firmware: the held mark on every
 *                    active member (section 6.3, resume ordering), and the
 *                    filters' watchdog bound, taken from the client's own
 *                    resume bound plus 10 s.
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
} CM_REENUM_PORT, *PCM_REENUM_PORT;

/* Children of one tunnel port remembered on 98 as already marked, so that
 * a re-enumeration marks only what is new (section 7.4). */
#define CM_REENUM_MAX_KNOWN 16UL

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

    /* 98: the root devnode, and each port's children already marked. */
    ULONG RootDevnode;
    ULONG Known[XHCI_TBT_MAX_TUNNELS][CM_REENUM_MAX_KNOWN];
    ULONG KnownCount[XHCI_TBT_MAX_TUNNELS];

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
    ULONG Ejects;           /* eject notifications served                 */
} CM_REENUM, *PCM_REENUM;

VOID CmReenumStart(struct _CM_NHI *nhi);
VOID CmReenumAction(struct _CM_NHI *nhi, const TBT_ICM_ACTION *action);
VOID CmReenumService(struct _CM_NHI *nhi);
VOID CmReenumSuspend(struct _CM_NHI *nhi);
VOID CmReenumResumed(struct _CM_NHI *nhi, BOOLEAN ok);
VOID CmReenumStop(struct _CM_NHI *nhi);

VOID CmReenumSetEject(struct _CM_NHI *nhi, CM_REENUM_EJECT hook,
                      PVOID context);
/* Writes up to `capacity` active members' locations into `ports` and
 * returns how many were written. */
ULONG CmReenumPorts(struct _CM_NHI *nhi, PULONG ports, ULONG capacity);

/* ---- the registration list, for cm_filt.c (NT only) ------------------ */

/* Once, from DriverEntry, before any AddDevice. Any IRQL. */
VOID CmRegInit(VOID);

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
                    const DEVICE_RELATIONS *relations);

/* A device IRP_MN_SET_POWER to D0 on a bridge filter: TRUE when it was
 * passed down with the hold's completion routine (the caller returns
 * STATUS_PENDING), FALSE when the caller passes it down as usual.
 * <= DISPATCH_LEVEL. */
BOOLEAN CmRegHoldD0(struct _CM_FILTER *filter, PIRP irp);

/* A child filter's device is being removed after a query-remove (not a
 * surprise removal): post the eject to the owning domain. PASSIVE_LEVEL. */
VOID CmRegChildEjected(struct _CM_FILTER *child);

#endif /* CM_REENUM_H */
