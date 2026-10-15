/*
 * cm.h - xhcicm.sys, the Thunderbolt 3 connection manager (design record
 * 21; roadmap-usb4.md Phase 39), a second binary beside xhci98.sys with
 * its own INFs, allowlists and INF gate, built from src\cm as xhciuas.sys
 * is built from src\uas.
 *
 * TWO ROLES IN ONE BINARY (record 21 section 3.2), told apart in AddDevice
 * by the PDO's hardware ids (cm_vals.c, CmClassifyIds):
 *
 *   [CM_NHI]     the NHI's function driver, every target (xhcicm.inf):
 *                cm_nhi.c (PnP, the start's selection and the stop),
 *                cm_thr.c (the connection-manager thread, its work queue
 *                and the accessors the pure core is given), cm_power.c
 *                (the power policy owner's sequence of section 4.4).
 *                Owns BAR0, ring 0, the ICM client and the thread (task
 *                39-A.4), or in its place the USB4 software connection
 *                manager of design record 22 (tbt_u4cm.c, task 40-A.6),
 *                whichever tbt_hw.c's selection admits.
 *   [CM_FILTER]  the tunnel bridge's upper filter, NT targets only
 *                (xhcicmf.inf; IoIsWdmVersionAvailable(1, 0x10) TRUE):
 *                cm_filt.c, with the registration list of cm_reenum.c the
 *                NHI asks it through (task 39-A.5).
 *   [CM_CHILD]   safe eject's child filter (task 39-A.8), NT only,
 *                created by a bridge filter above a device newly reported
 *                on an active tunnel bridge: cm_filt.c.
 *
 * FILES. cm_entry.c (DriverEntry, AddDevice, the dispatch table),
 * cm_nhi.c, cm_thr.c, cm_power.c, cm_reenum.c (the re-enumeration and
 * safe eject of cm_reenum.h, tasks 39-A.5 and 39-A.8, with its pure half
 * cm_ports.c and the CONFIGMG calls of cm_cfgmg.c), cm_filt.c, cm_reg.c (the one
 * registry site), cm_log.c (the shared log ring of src\xhci_log.c and its
 * emission, decision D12), cm_pool.c (the one pool and DMA site,
 * AGENTS.md's named-sites rule), cm_vals.c (pure: roles and value rules),
 * and the pure core tbt_ring.c, tbt_icm.c, tbt_hw.c, tbt_u4cm.c and
 * tbt_topo.c.
 *
 * IRQL: as each file's header says; every object this driver creates is
 * DO_POWER_PAGABLE (the filter copies its lower device's power flags).
 */

#ifndef CM_H
#define CM_H

#include "../xhci_compat.h"
#include "../xhci_log.h"
#include "cm_vals.h"
#include "tbt_hw.h"
#include "tbt_u4cm.h"
#include "cm_reenum.h"
#include "cm_live.h"

#define CM_POOL_TAG 'mChX'

/* CM_NHI.Stage. */
#define CM_STAGE_STOPPED 0  /* not started, or stopped                    */
#define CM_STAGE_INERT   1  /* started, pre-activation: no ring, protocol
                               session or connection-manager mailbox      */
#define CM_STAGE_ACTIVE  2  /* the selection said ICM, or admitted the
                               USB4 software CM (U4On): resources taken,
                               the bind running or done                   */
#define CM_STAGE_FAILED  3  /* a failure after activation (record 21
                               section 1.4.2): rings stopped, the instance
                               idle until its stop                        */

#define CM_KIND_NHI    0x4E4D4358UL  /* 'XCMN' */
#define CM_KIND_FILTER 0x464D4358UL  /* 'XCMF' */
#define CM_KIND_CHILD  0x434D4358UL  /* 'XCMC' (record 21 section 3.2 role 3) */

/* The connection-manager thread's work (CM_NHI.WorkBits), taken in this
 * order on each wake (cm_thr.c). */
#define CM_WORK_SYSTEM 0x01UL  /* complete the system power IRP            */
#define CM_WORK_D3     0x02UL  /* a device power IRP below D0              */
#define CM_WORK_D0     0x04UL  /* a device power IRP to D0, after the bus  */
#define CM_WORK_START  0x08UL  /* the start's selection, then the bind     */
#define CM_WORK_STOP   0x10UL  /* the unbind                               */
#define CM_WORK_EXIT   0x20UL  /* leave the loop                           */
#define CM_WORK_DIRECT 0x40UL  /* the system IRP's transition made without
                                  a device IRP (PoRequestPowerIrp failed),
                                  then its completion                      */

/* BAR0's extent this driver reads up to: the firmware status at 39944h
 * is the highest register touched (tbt_regs.h). A smaller window is
 * "BAR0 not delivered", record 21 section 1.4.2's Code 10. */
#define CM_BAR0_MIN_BYTES 0x3A000UL

/*
 * The nvme98 link-down channel's seam under the USB4 profile (record 22
 * section 10; nvme98's include/nvme98cm.h and gate G3; task 40-A.7, which
 * implements the channel in cm_nvlink.c - nothing here does). Both run on
 * the connection-manager thread at PASSIVE_LEVEL, no lock held, and call
 * nothing of tbt_u4cm.c's:
 *   CM_U4_UP_HOOK    for every TBT_U4_ACT_TUNNEL_UP, before the action
 *                    reaches the re-enumeration seam, so before the
 *                    operating system can expose the endpoint (the
 *                    channel's UP is published first);
 *   CM_U4_DOWN_HOOK  from the core's statement upcall, after section 7.3
 *                    step 1 of a PCIe teardown, with TBT_U4_LINKDOWN
 *                    (PathClosed, the anchor, the time left of the 3 s),
 *                    before the TUNNEL_DOWN it queues is drained to the
 *                    seam (the removal); possibly nested at a safe point.
 *                    DOWN_QUIESCED may be set only with PathClosed and the
 *                    endpoint's reset complete; returns TBT_U4_STMT_*.
 * Unregistered (NULL), the statement is withheld and counted.
 */
typedef VOID (*CM_U4_UP_HOOK)(PVOID context, const TBT_U4_ACTION *action);
typedef ULONG (*CM_U4_DOWN_HOOK)(PVOID context,
                                 const TBT_U4_LINKDOWN *info);

/* The head of both extensions: Kind is the first field, read by the
 * dispatch routines before they know which role a device object plays. */
typedef struct _CM_COMMON {
    ULONG Kind;
    PDEVICE_OBJECT Self;
    PDEVICE_OBJECT Pdo;
    PDEVICE_OBJECT Lower;
} CM_COMMON, *PCM_COMMON;

/* Counters of the NHI driver's own (record 21 section 8), beside the
 * ring's and the client's, emitted at stop as cm.* lines. */
typedef struct _CM_COUNTERS {
    ULONG Passes;           /* thread wakes                                */
    ULONG Polls;            /* idle polls run                              */
    ULONG ActionsUp;
    ULONG ActionsDown;
    ULONG ActionsResync;
    ULONG ActionsNotLive;   /* the entry an action named had left          */
    ULONG ReenumSkipped;    /* XhciCmReenum 0                              */
    ULONG ConfigFailures;   /* a READ_CONFIG or WRITE_CONFIG refused       */
    ULONG Suspends;
    ULONG Resumes;
    ULONG ResumeFailures;
    ULONG Shutdowns;
    ULONG PowerRequestFailures;
    ULONG PowerInline;      /* a power IRP finished without the thread     */
    ULONG BuffersKept;      /* a common buffer not freed: rings unproven   */
    ULONG WakesUnpowered;   /* S0 with no D0 IRP, the function not in D0   */
    /* The USB4 profile's driver half (task 40-A.6), emitted as u4.drv.*. */
    ULONG U4Starts;         /* TbtU4Start called                           */
    ULONG U4ActionsUp;
    ULONG U4ActionsDown;
    ULONG U4ActionsResync;
    ULONG U4ActionsDeep;    /* a router below depth 1 left                 */
    ULONG U4Named;          /* XhciTbtTopoAddNamed ADDED or KNOWN          */
    ULONG U4NameRefused;    /* any other answer                            */
    ULONG U4Learned;        /* XhciTbtLearn LEARNED                        */
    ULONG U4LearnTries;
    ULONG U4LinkDowns;      /* the statement upcall                        */
    ULONG U4PathsClosed;    /* ... with the path proven closed             */
    ULONG U4NoChannel;      /* statements withheld: no nvme98 channel      */
    ULONG U4Ejects;         /* the eject hook called                       */
    ULONG U4EjectsUnmapped; /* ... for a bridge no port corresponds to     */
    ULONG U4EjectsFailed;   /* TbtU4Eject not TBT_OK                       */
    ULONG U4ResumeHeld;     /* TbtU4Resume: an obligation not met          */
    ULONG U4ForcePowerKept; /* a router connected at stop: force power kept */
    ULONG U4Contained;      /* the USB4 profile contained (record 22 7.7)  */
    ULONG U4ContainRefused; /* a lifecycle step refused while contained    */
} CM_COUNTERS;

typedef struct _CM_NHI {
    CM_COMMON Common;
    /* What the matched NHI's ids say (CmClassifyIds) and the profile
     * they choose (CmProfileFor), at AddDevice. Both zero where the ids
     * are not read (Windows 98 SE and ME). The start reads the ids from
     * configuration space on every target, and those decide (Vendor ..
     * SubVendor, Hw). */
    CM_IDS Ids;
    ULONG Profile;
    /* CM_INERT_* of the last start: CM_INERT_NONE once a client runs. */
    ULONG Inert;
    /* How far the start went (CM_STAGE_*). A failure is cleaned up by the
     * stage reached: before activation there is nothing to undo; after it
     * this driver's rings are stopped and its work drained before the DMA
     * memory is freed, and tunnels are preserved. */
    ULONG Stage;
    /* Platform-power changes this instance made (the integrated hosts'
     * force power: TbtHwPlatformInit's record), kept apart from Stage so
     * an inert or failed instance restores only what it changed itself;
     * and the Bus Master bit, set here only where it was clear. */
    TBT_HW_POWER Power;
    ULONG BusMasterSetHere;
    ULONG Started;
    ULONG Starts;
    ULONG Stops;
    /* Read once per start (cm_reg.c), at the start's first step. */
    ULONG ValuesRead;
    CM_VALUES Values;
    /* Guards Log: the connection-manager thread appends beside the PnP
     * and power paths. */
    KSPIN_LOCK LogLock;
    XHCI_LOG Log;

    /* The NHI as configuration space names it, read at start. */
    ULONG Vendor;
    ULONG Device;
    ULONG ClassCode;
    ULONG SubVendor;
    const TBT_HW_PROFILE *Hw;
    TBT_HW_SELECTION Sel;

    /* BAR0, from the start's translated resources. */
    PHYSICAL_ADDRESS BarPhys;
    ULONG BarLength;
    ULONG HaveBar;
    PUCHAR BarVa;

    /* The ring block (cm_pool.c): one common buffer below 4 GB. */
    PDMA_ADAPTER Dma;
    PVOID CommonVa;
    PHYSICAL_ADDRESS CommonPa;

    /* The pure core's accessors (cm_thr.c) and its state. */
    TBT_HOST Host;
    TBT_CFG Cfg;
    ULONG CfgWriteFailures; /* TBT_CFG.Write32 refused (cm_thr.c)         */
    ULONG ForcePowerWriteRefused; /* a VS_CAP_22 write refused, this call */
    ULONG ClockLast;        /* KeQueryTickCount's low word               */
    ULONG ClockTickUs;      /* KeQueryTimeIncrement, in microseconds     */
    ULONG ClockRest;        /* microseconds not yet a millisecond        */
    ULONG ClockMs;          /* the millisecond clock TBT_HOST.NowMs reads */
    TBT_RING Ring;
    TBT_ICM Icm;
    /* The common buffer's size: the ICM's ten-entry rings, or the USB4
     * profile's sixteen (cm_pool.c). */
    ULONG DmaBytes;

    /* The USB4 software connection manager (design record 22, task
     * 40-A.6), run where TbtHwSelect admitted it: U4On from the selection
     * to the stop, the core over the same Ring and Host as the ICM's.
     * Binds is the domain's port-to-bridge correspondence (tbt_topo.h,
     * record 22 section 7.6), kept for the instance beside the
     * re-enumeration seam's tunnel-port set (Reenum.Topo); ArmOwed asks the
     * next drain to take the learning baseline again after a path proven
     * closed. U4Batch is one drain's actions, off the stack. */
    ULONG U4On;
    TBT_U4CM U4;
    TBT_U4_CONFIG U4Cfg;
    XHCI_TBT_BINDS Binds;
    ULONG ArmOwed;
    ULONG U4BatchCount;
    TBT_U4_ACTION U4Batch[TBT_U4_ACTION_QUEUE];
    /* Per USB4 port, the connection generation whose PCIe path the
     * statement upcall saw proven closed (bit n-1 of U4ClosedValid for
     * port n): a TUNNEL_UP of that generation still queued is stale. */
    ULONG U4ClosedGen[TBT_U4_MAX_PORTS];
    ULONG U4ClosedValid;
    /* Per USB4 port, the connection generation of the last action handed
     * to the re-enumeration seam (or of the tunnel found up at start): an
     * eject is honoured only while the port still carries it. */
    ULONG U4SeamGen[TBT_U4_MAX_PORTS];
    ULONG U4SeamValid;
    /* The USB4 profile's resume bound in ms for the most obligations a
     * domain can hold (TbtU4ResumeBoundUs, rounded up), set before
     * CmReenumSuspend: the hold's watchdog must be at least this plus
     * record 21's 10 s (cm_reenum.h). */
    ULONG U4ResumeBoundMs;
    /* Every port read disconnected (TbtU4AllPortsDisconnected) by the
     * unbind just before it stopped the rings: the fresh evidence
     * CmNhiMayClearPower requires. */
    ULONG U4ClearOk;
    /* Record 22 section 7.7: the driver's half of the containment done
     * (CmNhiU4Contain ran once this start); and, per USB4 port, the
     * connection generation the nvme98 channel was last told UP for and
     * not yet told down (bit n-1 of U4UpValid for port n), kept apart from
     * the core's tunnel records, which a failed teardown clears. */
    ULONG U4ContainDone;
    /* A PnP stop, surprise removal or removal waits for the thread: set by
     * the PnP path before its work is queued, cleared once that work ran.
     * The containment's reconciliation is never requested meanwhile (the
     * 9x seam calls CONFIGMG synchronously; record 22 section 7.7). */
    volatile ULONG PnpStopPending;
    ULONG U4UpGen[TBT_U4_MAX_PORTS];
    ULONG U4UpValid;
    /* The nvme98 channel's two moments (CmNhiSetU4Hooks), NULL until task
     * 40-A.7's cm_nvlink.c registers them. */
    CM_U4_UP_HOOK U4UpHook;
    CM_U4_DOWN_HOOK U4DownHook;
    PVOID U4HookContext;
    /* The resume owed by a suspend this driver ran (cm_power.c). */
    ULONG Suspended;
    /* CmReenumStart was called and CmReenumStop is owed. */
    ULONG ReenumOn;
    CM_REENUM Reenum;

    /* The connection-manager thread (cm_thr.c). WorkLock guards WorkBits,
     * the three IRP slots, Accepting and PowerOutstanding. */
    KSPIN_LOCK WorkLock;
    ULONG WorkBits;
    ULONG Accepting;
    PIRP SystemIrp;
    PIRP D3Irp;
    PIRP D0Irp;
    KEVENT WorkEvent;
    KEVENT PnpDone;
    KEVENT ThreadExited;
    PVOID ThreadObject;
    ULONG ThreadRunning;
    ULONG ThreadReferenceFailures;
    NTSTATUS StartStatus;
    ULONG BindOwed;

    /* The host interface's liveness (cm_live.h, record 21 sections 5.5
     * and 5.6): the monitor's state, the class the BAR accessors are asked
     * under (CM_ACCESS_ORDINARY but inside a recovery), and the identity
     * captured at the healthy start that a recovery must find again: the
     * configuration space's first DW and BAR0's low DW. */
    CM_LIVE Live;
    ULONG AccessClass;
    ULONG LiveIds;
    ULONG LiveBar0;
    ULONG LiveBar0Hi;
    ULONG CfgStrict;        /* cm_thr.c's cmConfig checks the length read */
    ULONG LiveNeedBm;       /* the validation found Bus Master clear      */
    /* A TUNNEL_UP being handed to the re-enumeration seam (under
     * WorkLock); CmNhiRevoke waits on DeliverIdle until none is, so no UP
     * reaches the seam after a revocation returned. */
    ULONG Delivering;
    KEVENT DeliverIdle;

    /* Power (cm_power.c). */
    DEVICE_POWER_STATE DevicePower;
    SYSTEM_POWER_STATE SystemPower;
    PIRP PendingSystemIrp;
    LONG PowerOutstanding;
    KEVENT PowerIdle;
    ULONG Removing;         /* CmPowerDrain closed power admission        */
    /* CM_WORK_DIRECT's target: D0, or a sleep state's D3. */
    DEVICE_POWER_STATE DirectWant;

    CM_COUNTERS Counters;
} CM_NHI, *PCM_NHI;

/* The PDOs a bridge filter's snapshot keeps (record 21 section 7.4). */
#define CM_SNAP_MAX 32

/*
 * Both filter roles (record 21 section 3.2): the tunnel bridge's
 * (CM_KIND_FILTER, from AddDevice) and the child filter safe eject puts on
 * a device newly reported on an active bridge (CM_KIND_CHILD, created by
 * the bridge filter, cm_reenum.c). One structure, so both share the
 * remove lock and the device-usage handling of cm_filt.c.
 */
typedef struct _CM_FILTER {
    CM_COMMON Common;
    ULONG Started;
    /* The filter's remove lock (cm_filt.c): 1 for the attach, plus one per
     * device-usage notification in flight, taken under Lock and refused
     * once Removing is set; removal drops the first and waits on Drained. */
    KSPIN_LOCK Lock;
    ULONG Removing;
    LONG Outstanding;
    KEVENT Drained;

    /* ---- bridge: the registration entry (cm_reenum.c), every field
     * under the registration list's lock but Rundown, the timer and the
     * DPC, which are its objects ---- */
    LIST_ENTRY RegLink;
    ULONG Published;        /* on the list                                 */
    ULONG Serial;           /* this publication's, list-wide               */
    ULONG Bdf;              /* XHCI_TBT_BDF of the bridge                  */
    ULONG Gone;             /* withdrawn: no new call is started on it     */
    ULONG InFlight;         /* calls and holds in progress                 */
    KEVENT Rundown;         /* set by the last release once Gone           */
    ULONG Baseline;         /* its first BusRelations after the start done */
    ULONG Active;           /* an active member of Owner's set             */
    ULONG SafeEject;        /* Owner's XhciCmSafeEject                     */
    struct _CM_NHI *Owner;
    ULONG OwnerIndex;       /* the member's index in Owner's set           */
    /* The resume hold (record 21 section 6.3). */
    ULONG Held;
    struct _CM_NHI *HeldBy;
    ULONG HoldReenum;       /* invalidate on the watchdog's failure path   */
    ULONG HoldLeftS;        /* the watchdog's seconds still to run         */
    PIRP HeldIrp;           /* the retained device D0 IRP                  */
    ULONG TimerArmed;
    KTIMER HoldTimer;
    KDPC HoldDpc;
    /* The snapshot of section 7.4: written by the PnP dispatch alone,
     * under the registration list's lock (the eject's eligibility reads
     * it from a child's removal). */
    ULONG SnapCount;
    PVOID Snap[CM_SNAP_MAX];
    ULONG SnapTruncated;    /* the last answer held more than CM_SNAP_MAX  */
    ULONG RelGen;           /* bumped when the reported population changes */
    ULONG EjectSeen;        /* an orderly child removal since the last
                               eject was posted                           */
    ULONG Unsettled;        /* a tunnel change since its last BusRelations */
    ULONG ChangeSeq;        /* tunnel changes seen, for that settling     */

    /* ---- child ---- */
    struct _CM_FILTER *Bridge;
    ULONG QueryRemoved;     /* a query-remove not cancelled                */
    ULONG SurpriseRemoved;
} CM_FILTER, *PCM_FILTER;

/* cm_entry.c */
extern PDRIVER_OBJECT CmDriverObject;
NTSTATUS CmCompleteIrp(PIRP irp, NTSTATUS status, ULONG_PTR information);
NTSTATUS CmForwardSync(PCM_COMMON common, PIRP irp);
VOID CmZero(PVOID p, ULONG n);

/* cm_nhi.c */
NTSTATUS CmNhiAddDevice(PDRIVER_OBJECT driver, PDEVICE_OBJECT pdo,
                        const CM_IDS *ids);
NTSTATUS CmNhiPnp(PCM_NHI nhi, PIRP irp);
VOID CmNhiStartWork(PCM_NHI nhi);
VOID CmNhiBind(PCM_NHI nhi);
VOID CmNhiStopWork(PCM_NHI nhi);
VOID CmNhiPollPass(PCM_NHI nhi);
VOID CmNhiDrainActions(PCM_NHI nhi);
BOOLEAN CmNhiSetBusMaster(PCM_NHI nhi, BOOLEAN on);
VOID CmNhiQuiesce(PCM_NHI nhi);
VOID CmNhiPlatformRelease(PCM_NHI nhi, BOOLEAN mayClear);
BOOLEAN CmNhiPlatformInit(PCM_NHI nhi);
BOOLEAN CmNhiReinit(PCM_NHI nhi);
BOOLEAN CmNhiFailed(PCM_NHI nhi);
BOOLEAN CmNhiRunning(PCM_NHI nhi);
VOID CmNhiUnbind(PCM_NHI nhi);
BOOLEAN CmNhiMayClearPower(PCM_NHI nhi);
/* The nvme98 channel's registration (CM_U4_UP_HOOK, CM_U4_DOWN_HOOK); on
 * the thread, or before the start. */
VOID CmNhiSetU4Hooks(PCM_NHI nhi, CM_U4_UP_HOOK up, CM_U4_DOWN_HOOK down,
                     PVOID context);
/* For cm_reenum.c, profile-neutral (cm_reenum.h, "THE USB4 PROFILE"). */
VOID CmNhiNamePorts(PCM_NHI nhi);
VOID CmNhiSetChanged(PCM_NHI nhi);
/* cm_power.c's resume: the USB4 batch taken and its UPs published before
 * the held bridges are released (cm_nhi.c, cmU4Drain). */
VOID CmNhiU4Take(PCM_NHI nhi);
VOID CmNhiWaitService(PCM_NHI nhi);
BOOLEAN CmNhiActionLive(PCM_NHI nhi, const TBT_ICM_ACTION *action);
ULONG CmNhiActionMember(PCM_NHI nhi, const TBT_ICM_ACTION *action);

/*
 * The liveness monitor's driver half (cm_live.h). On the thread: the
 * actions a sample or a validation asked for (CmNhiLiveActions), the USB4
 * profile's containment (CmNhiU4Contain, record 22 section 3.5). From the
 * PnP dispatch, before the stop's work is queued: CmNhiRevoke on a surprise
 * removal (the gate closed for good, at once). Anywhere: CmNhiHwAccess,
 * whether ordinary register access and lifecycle work may run now - every
 * exit path (stop, shutdown, suspend, eject, failure cleanup) asks it
 * before touching the host interface.
 */
VOID CmNhiLiveActions(PCM_NHI nhi, ULONG actions);
VOID CmNhiU4Contain(PCM_NHI nhi);
/* The same with the reconciliation chosen: FALSE on the stop's path, which
 * never calls into PnP from inside a PnP request (record 22 section 7.7). */
VOID CmNhiU4ContainEx(PCM_NHI nhi, BOOLEAN reconcile);
/* The value RX ring 0's size register holds under the USB4 profile: what
 * the bind and every resume must read back before the monitor is armed. */
#define CM_U4_RX_SIZE_VALUE \
    (TBT_RING_U4_ENTRIES | (TBT_RING_BUF_BYTES << TBT_RX_BUFSIZE_SHIFT))
VOID CmNhiRevoke(PCM_NHI nhi);
BOOLEAN CmNhiHwAccess(PCM_NHI nhi);
/* The ICM client's arming after a bind or a resume: ACTIVE against RX ring
 * 0's size register read now when it holds what the start programmed;
 * against 0 (all-ones detection only) when it reads otherwise; FALSE (the
 * caller's failure after activation) when it reads all ones or the monitor
 * refuses. */
BOOLEAN CmNhiLiveArm(PCM_NHI nhi);

/* cm_thr.c */
VOID CmThrInit(PCM_NHI nhi);
NTSTATUS CmThrStart(PCM_NHI nhi);
VOID CmThrStop(PCM_NHI nhi);
BOOLEAN CmThrQueue(PCM_NHI nhi, ULONG work, PIRP irp);
BOOLEAN CmThrQueueOrRecord(PCM_NHI nhi, ULONG work, PIRP irp,
                           DEVICE_POWER_STATE state);
VOID CmThrRunPnp(PCM_NHI nhi, ULONG work);
VOID CmThrAccessors(PCM_NHI nhi);
ULONG CmCfgRead32(PCM_NHI nhi, ULONG offset);
/* One BAR0 DW under the PROBE class (the monitor's and the validation's
 * reads): refused, all ones, only once revoked. IRQL: PASSIVE_LEVEL. */
ULONG CmThrProbe32(PCM_NHI nhi, ULONG offset);
/* The monitor's sample and its actions, on the thread between the queued
 * work and the poll. IRQL: PASSIVE_LEVEL. */
VOID CmThrLiveness(PCM_NHI nhi);
BOOLEAN CmCfgWrite16(PCM_NHI nhi, ULONG offset, USHORT value);

/* cm_power.c */
NTSTATUS CmNhiPower(PCM_NHI nhi, PIRP irp);
VOID CmPowerSystemWork(PCM_NHI nhi, PIRP irp);
VOID CmPowerD3Work(PCM_NHI nhi, PIRP irp);
VOID CmPowerD0Work(PCM_NHI nhi, PIRP irp);
VOID CmPowerDirectWork(PCM_NHI nhi, PIRP irp);
BOOLEAN CmPowerHold(PCM_NHI nhi);
VOID CmPowerRelease(PCM_NHI nhi);
VOID CmPowerDrain(PCM_NHI nhi);
VOID CmPowerSetSystem(PCM_NHI nhi, SYSTEM_POWER_STATE state);
BOOLEAN CmPowerSleeping(PCM_NHI nhi);

/* cm_filt.c */
NTSTATUS CmFilterAddDevice(PDRIVER_OBJECT driver, PDEVICE_OBJECT pdo);
NTSTATUS CmFilterPnp(PCM_FILTER filter, PIRP irp);
NTSTATUS CmFilterPower(PCM_FILTER filter, PIRP irp);

/* cm_reg.c */
VOID CmReadValues(PCM_NHI nhi);

/* cm_log.c */
VOID CmLogInit(PCM_NHI nhi);
VOID CmLogApply(PCM_NHI nhi);
VOID CmLogNote(PCM_NHI nhi, const char *label, ULONG value);
VOID CmLogEmit(PCM_NHI nhi, ULONG reason);

/* cm_pool.c */
PVOID CmAlloc(ULONG bytes);
VOID CmFree(PVOID p);
NTSTATUS CmDmaOpen(PCM_NHI nhi);
VOID CmDmaClose(PCM_NHI nhi);

/* The completion routine of a forwarded IRP is called on success, error
 * and cancel alike; IoSetCompletionRoutine's macro spelled out, as uas.h
 * does. */
#define CM_SET_COMPLETION_ALWAYS(irp, routine, context)                      \
    do {                                                                     \
        PIO_STACK_LOCATION cmNext_ = IoGetNextIrpStackLocation(irp);         \
        cmNext_->CompletionRoutine = (routine);                              \
        cmNext_->Context = (context);                                        \
        cmNext_->Control = SL_INVOKE_ON_SUCCESS | SL_INVOKE_ON_ERROR |       \
                           SL_INVOKE_ON_CANCEL;                              \
    } while (0)

#endif /* CM_H */
