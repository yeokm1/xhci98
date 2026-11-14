/*
 * cm.h - xhcicm.sys, the Thunderbolt and USB4 connection manager (design
 * records 21 and 22; roadmap-usb4.md Phases 39 onward), a second binary
 * beside xhci98.sys with its own INFs, allowlists and INF gate, built from
 * src\cm as xhciuas.sys is built from src\uas.
 *
 * THREE ROLES IN ONE BINARY (record 21 section 3.2), told apart in
 * AddDevice by the PDO's hardware ids (cm_vals.c, CmClassifyIds):
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
 *                on any tunnel bridge it filters, and below that by a
 *                child on a PCI-to-PCI bridge (design record 31 sections
 *                4.1 and 4.3: the tunnel id's chain), or (task 42-A.5,
 *                record 21 section 7.4 revision 3) by AddDevice for a
 *                device whose LowerFilters value the mark service wrote:
 *                cm_filt.c.
 *
 * FILES. docs/contributing/source-files.md says what each file of this
 * directory is.
 *
 * Beside the roles, three further kinds of device object. The query
 * device of a started NHI (CM_KIND_QUERY; cm_qryos.c, design record 25
 * section 4), a named control device with no PnP role, which the dispatch
 * routines tell apart by its Kind before anything else; its answer is
 * built by cm_query.c. The network PDO (CM_KIND_NETPDO; cm_net.c, design
 * record 35 section 6): the NHI FDO's one child while XhciCmNet is 1, a
 * bus role. And a member filter (CM_KIND_MEMBER; cm_oscnt.c, design
 * record 32 section 12.8.15.7), NT only: the ASM4242 upstream port's and
 * xHCI's filter of the platform's USB4 handover.
 *
 * IRQL: as each file's header says; every PnP object this driver creates
 * is DO_POWER_PAGABLE (the filter copies its lower device's power flags).
 */

#ifndef CM_H
#define CM_H

#include "../xhci_compat.h"
#include "../xhci_log.h"
#include "cm_vals.h"
#include "tbt_hw.h"
#include "tbt_u4cm.h"
#include "cm_reenum.h"
#include "cm_statb.h"
#include "cm_live.h"
#include "cm_qryb.h"
#include "cm_qnetb.h"
#include "cm_eject.h"
#include "cm_irq.h"
#include "cm_breq.h"
#include "cm_rirqos.h"
#include "cm_oscp.h"

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
#define CM_KIND_QUERY  0x514D4358UL  /* 'XCMQ': the query device, cm_qryos.c */
#define CM_KIND_MEMBER 0x4D4D4358UL  /* 'XCMM': an ASM4242 mailbox member's
                                        filter, cm_oscnt.c (record 32
                                        section 12.8.15.7)                */

/* The connection-manager thread's work (CM_NHI.WorkBits), taken in this
 * order on each wake (cm_thr.c). */
/* PCI Command (PCI 3.0 6.2.2): the configuration word cm_thr.c writes for
 * Bus Master, and a USB4 recovery's preparation for Memory Space too
 * (record 22 section 7.7 part 2). */
#define CM_PCI_COMMAND            0x04UL
#define CM_PCI_COMMAND_BUS_MASTER 0x0004U

#define CM_WORK_SYSTEM 0x01UL  /* complete the system power IRP            */
#define CM_WORK_D3     0x02UL  /* a device power IRP below D0              */
#define CM_WORK_D0     0x04UL  /* a device power IRP to D0, after the bus  */
#define CM_WORK_START  0x08UL  /* the start's selection, then the bind     */
#define CM_WORK_STOP   0x10UL  /* the unbind                               */
#define CM_WORK_EXIT   0x20UL  /* leave the loop                           */
#define CM_WORK_DIRECT 0x40UL  /* the system IRP's transition made without
                                  a device IRP (PoRequestPowerIrp failed),
                                  then its completion                      */
#define CM_WORK_QUIESCE 0x80UL /* a 9x shutdown's quiescence, its system
                                  IRP not retained for it (CM_PWR_DETACH)  */
#define CM_WORK_RING   0x100UL /* the ring interrupt's DPC: RX ring 0 may
                                  hold a frame (cm_rirqos.c, task 45.22)   */
#define CM_WORK_NET    0x200UL /* the network driver's Open or Close to give
                                  the engine (cm_net.c, task 45.59)        */

/* BAR0's extent this driver reads up to: the firmware status at 39944h
 * is the highest register touched (tbt_regs.h). A smaller window is
 * "BAR0 not delivered", record 21 section 1.4.2's Code 10. */
#define CM_BAR0_MIN_BYTES 0x3A000UL

/*
 * The stor98 link-down channel's seam under the USB4 profile (record 22
 * section 10; stor98's include/stor98cm.h and gate G3; task 40-A.7, which
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

/* The head of every extension: Kind is the first field, read by the
 * dispatch routines before they know which kind a device object is. */
typedef struct _CM_COMMON {
    ULONG Kind;
    PDEVICE_OBJECT Self;
    PDEVICE_OBJECT Pdo;
    PDEVICE_OBJECT Lower;
    /* Who detaches Lower (design record 31 section 4.10, K6): the removal
     * or, on a system with the kept child, the I/O manager's
     * FastIoDetachDevice callback: CM_DETACH_*, under CmDetachOnce's lock;
     * DetachDone set once done. CmCommonInit at the object's creation. */
    ULONG Detach;
    KEVENT DetachDone;
} CM_COMMON, *PCM_COMMON;

#define CM_DETACH_NONE      0UL
#define CM_DETACH_CLAIMED   1UL
#define CM_DETACH_DONE      2UL

/*
 * A member of the ASM4242 mailbox's exclusion (cm_oscnt.c; design record
 * 32 section 12.8.15.7), NT only: the upstream port and the xHCI (each a
 * CM_MEMBER of its own) and each downstream port (the bridge filter's
 * Osc). Every field under cm_oscnt.c's list lock but Unused, its event.
 * Role 0 is no member.
 */
typedef struct _CM_OSC_MEMBER {
    ULONG Role;             /* CM_OSCP_R_* (cm_oscp.h)                     */
    ULONG Slot;             /* the list's entry + 1, 0 unlisted            */
    ULONG Started;          /* a start that succeeded below, not stopped   */
    ULONG Closed;           /* a query-stop or query-remove succeeded      */
    ULONG Gone;             /* surprise-removed or removed: for good       */
    ULONG Located;
    ULONG Bdf;              /* XHCI_TBT_BDF, read at its start             */
    ULONG Secondary;        /* a bridge's secondary bus, read with it      */
    LONG InFlight;          /* tracked requests between arrival and return */
    LONG Usage;             /* a member object's device-usage notifications
                               below it (their completion reads it)       */
    ULONG Uses;             /* the exchange's holds on its interface       */
    KEVENT Unused;          /* set while Uses is 0                         */
    PDEVICE_OBJECT Pdo;
    PDEVICE_OBJECT Lower;
    ULONG BusHeld;          /* the upstream port: Bus taken at its start   */
    ULONG BusTaking;        /* a query into Bus under way: its one taker   */
    ULONG BusStatus;        /* ... and its query's last status (logged)    */
    BUS_INTERFACE_STANDARD Bus;
} CM_OSC_MEMBER, *PCM_OSC_MEMBER;

/* The upstream port's and the xHCI's filter (CM_KIND_MEMBER). */
typedef struct _CM_MEMBER {
    CM_COMMON Common;
    CM_OSC_MEMBER Osc;
} CM_MEMBER, *PCM_MEMBER;

/* The Thunderbolt networking service (design record 35, task 45.59): the
 * network PDO's extension and the NHI's share, cm_net.c. */
#include "cm_net.h"

/* cm_entry.c, design record 31 section 4.10 (task 48.29). CmCommonInit:
 * the detach claim's event, at an object's creation, before its attach.
 * CmDetachOnce: Lower detached exactly once, by whichever of the removal
 * and the callback claims it first; TRUE when this caller did it. `wait`:
 * a caller that lost the claim waits until the winner has finished (every
 * caller but a claim's own retry; the winner holds no lock across its
 * IoDetachDevice). PASSIVE_LEVEL. CmKeptOn: the kept
 * child's system (CmPortsKeptOs, 8.1 alone), read once in DriverEntry;
 * only then does the driver carry FastIoDetachDevice. */
VOID CmCommonInit(PCM_COMMON common);
BOOLEAN CmDetachOnce(PCM_COMMON common, BOOLEAN wait);
extern ULONG CmKeptOn;

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
    ULONG U4NoChannel;      /* statements withheld: no stor98 channel      */
    ULONG U4Ejects;         /* the eject hook called                       */
    ULONG U4EjectsUnmapped; /* ... for a bridge no port corresponds to     */
    ULONG U4EjectsFailed;   /* TbtU4Eject not TBT_OK                       */
    ULONG U4ResumeHeld;     /* TbtU4Resume: an obligation not met          */
    ULONG U4ForcePowerKept; /* a router connected at stop: force power kept */
    ULONG U4Contained;      /* the USB4 profile contained (record 22 7.7)  */
    ULONG U4ContainRefused; /* a lifecycle step refused while contained    */
    ULONG U4RecoverProbes;  /* record 22 7.7: probes while contained       */
    ULONG U4RecoverRefused; /* an attempt refused by a step (logged)       */
    ULONG U4RecoverOk;      /* committed                                   */
    ULONG U4RecoverFinal;   /* the verified stop failed: final             */
    ULONG U4RetireRequests; /* removal-only requests made                  */
    ULONG U4RetirementOwed; /* a pass that found a barrier still standing  */
    ULONG U4RecoverRearm;   /* an attempt that ended on an all-ones read   */
    ULONG IsoAsked;         /* record 22 10.5: isolation asked by xhci98   */
    ULONG IsoYes;           /* ... and answered yes                        */
    ULONG IsoNo;            /* ... and refused (the reason in cm.iso)      */
    ULONG LcGo2Sx;          /* record 22 11.1.7: the ICL row's GO2SX sent  */
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
    /* The platform's USB4 handover (cm_oscos.c, record 32 section
     * 12.8.11): this start's tier, 0 before discovery; the upstream port
     * last found, 0 before (bus 0 never holds one), kept across starts. */
    ULONG OscTier;
    ULONG OscUspBdf;
    /* The NT routes (cm_oscnt.c, record 32 section 12.8.15): this start's
     * flags (CM_OSCN_F_*: the guard's skip, the unknown board left at
     * XhciCmAsm4242Reads 0), and the boot guard's marker armed by this
     * start's or this resume's send (its kind, 0 none), cleared by the
     * router's answer or an orderly shutdown. */
    ULONG OscFlags;
    ULONG OscArmed;
    /* The start's activation deferred (record 32 section 12.8.15.16): the
     * start's mode kept for it, its start and last try by the clock. */
    ULONG OscDeferred;
    ULONG OscDeferMode;
    ULONG OscDeferAtMs;
    ULONG OscDeferTryMs;
    /* The board decided once a start (12.8.15.16): what this start
     * decided and for which host, forgotten at the next start. */
    CM_OSCP_KEPT OscKept;
    /* The PnP start's own network step done (CmNhiPnp): a deferred
     * activation waits for it, so the network PDO has one publisher at a
     * time. WorkLock. */
    LONG OscStartDone;
    /* A deferred activation ended active: the network PDO's publication
     * owed to the PnP path's next BusRelations (CmNhiPnp), serialised
     * there with a surprise removal. */
    ULONG OscNetOwed;
    ULONG Stops;
    /* Read once per start (cm_reg.c), at the start's first step. */
    ULONG ValuesRead;
    CM_VALUES Values;
    /* Guards Log: the connection-manager thread appends beside the PnP
     * and power paths. */
    KSPIN_LOCK LogLock;
    XHCI_LOG Log;
    /* Task 46.13 (record 21 section 8.2), under LogLock: the last stamp's
     * clock and the bytes since its start (CM_LOG_STAMP_FORCE after a
     * drain); the head of the first connection, armed at the first
     * u4.connect of the device's life, whole records, never wrapped. */
    ULONG LogStampMs;
    ULONG LogStampBytes;
    ULONG LogHeadArmed;
    ULONG LogHeadFull;
    ULONG LogHeadBytes;
    UCHAR LogHead[CM_LOG_HEAD_BYTES];

    /* The NHI as configuration space names it, read at start. */
    ULONG Vendor;
    ULONG Device;
    ULONG ClassCode;
    ULONG SubVendor;
    ULONG Subsystem;        /* PCI dword 2Ch whole: TbtHwSelect's subsystem */
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
    ULONG StepUs;           /* cmClockUs at the host waits' last step     */
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
    /* Record 22 section 9.4: the earlier instance's record, taken from its
     * slot at the start with the ids it was written under. */
    TBT_U4_CARRY U4Carry;
    ULONG U4CarryIds;
    ULONG U4CarrySub;
    ULONG U4CarryTick;
    XHCI_TBT_BINDS Binds;
    ULONG ArmOwed;
    /* Record 22 section 7.6.5 (task 49.18), the thread's alone: the clock
     * at the last learning arm, each member's filter record
     * (CmRegMemberRecords) taken before the last SHPC reading, and the
     * u4.learn.shpc value last logged. */
    ULONG U4ShpcArmMs;
    ULONG U4ShpcRecord[XHCI_TBT_MAX_TUNNELS];
    ULONG U4ShpcLogged;
    /* Design record 34 section 3.12 (task 48.30): the chained tunnels'
     * structural and occupancy bindings and their transaction (tbt_chain.h),
     * the thread's alone; whether a MAY_CONNECT refusal was logged. */
    XHCI_CHAIN Chain;
    ULONG ChainHeldLogged;
    /* Each binding as last pushed to the registration side
     * (CmReenumChainSet): used, member, chain port, its secondary bus,
     * then the token's CM_MK_TOK_WORDS. */
#define CM_CHAIN_PUSH_WORDS 9UL
    ULONG ChainPushed[XHCI_CHAIN_BINDS][CM_CHAIN_PUSH_WORDS];
    /* Record 22 section 7.6's learning window per pending port (task
     * 44.57): when cmU4LearnPending tries it again, and the ports it
     * no longer tries (still pending). */
    XHCI_TBT_LEARN_CLOCK U4LearnClock;
    /* Record 22 section 7.6.2 (task 48.43): the port of a tunnel found up
     * at start inferred under XhciCmInferAtStart, kept apart from Binds
     * and read by the eject's hand-off alone (cmU4EjectBridge); the
     * read-back after its teardown; the closure it confirmed, for the
     * status record; the ports conflicted for the inference. */
    struct {
        ULONG Valid;
        ULONG Port;
        ULONG Member;
        ULONG Bdf;
        ULONG Buses;
        ULONG Gen;
        ULONG Conflict;     /* bit n-1: no further inference on port n   */
        ULONG Logged;       /* the last u4.infer.refused logged, plus 1  */
        ULONG Checking;
        ULONG CheckPort;
        ULONG CheckBdf;
        ULONG CheckBuses;
        ULONG CheckGen;
        ULONG CheckSince;
        ULONG ClosedPort;   /* 0 for none                                */
        ULONG ClosedBdf;
        ULONG ClosedGen;
    } U4Infer;
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
     * CmReenumSuspend, and logged there with record 21's 10 s and the
     * five-minute cap as diagnostics (cm_reenum.h). */
    ULONG U4ResumeBoundMs;
    /* Every port read disconnected (TbtU4AllPortsDisconnected) by the
     * unbind just before it stopped the rings: the fresh evidence
     * CmNhiMayClearPower requires. */
    ULONG U4ClearOk;
    /* Record 22 section 7.7: the driver's half of the containment done
     * (CmNhiU4Contain ran once this start); and, per USB4 port, the
     * connection generation the stor98 channel was last told UP for and
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
    /* Record 22 section 7.7's recovery (task 42-A.10), the driver's half.
     * The tunnel-port set as last read healthy (each member's location,
     * identity DW and bus-number DW 0x18), for part 3 step 6; the members
     * whose stor98 entry lost UP at the containment (CmNvlinkLost), for
     * R8b; the Command bits the preparation set; the retirement barrier
     * (part 4: bit i, member i), with each barred member's removal-only
     * request - when it was last made and the bridge filter's answer
     * count then (NT) - and the members the recovery's RESYNC skips. */
    ULONG RecSetCount;
    ULONG RecSetBdf[XHCI_TBT_MAX_TUNNELS];
    ULONG RecSetId[XHCI_TBT_MAX_TUNNELS];
    ULONG RecSetBus[XHCI_TBT_MAX_TUNNELS];
    ULONG RecLostUp;
    ULONG RecCmdSet;
    ULONG RecBarred;
    ULONG RecWkHold;        /* 98: the worker's hold last posted (record 21
                               section 6.1.2, CmNhiU4WorkerHold)          */
    ULONG RecDebtMask;      /* the members the attempts' drains owe a
                               removal (accumulated, read while every
                               binding stands), barred at the commit       */
    ULONG RecChanged;       /* the members step 6 found changed (bus, or
                               the entry lost UP): their learned bindings
                               are withdrawn after the commit             */
    ULONG RecRetireSent;
    ULONG RecRetireAtMs[XHCI_TBT_MAX_TUNNELS];
    ULONG RecRetireAns[XHCI_TBT_MAX_TUNNELS];
    ULONG RecUpValid;       /* U4UpValid as the containment found it      */
    ULONG RecOnes;          /* an all-ones read inside the attempt
                               (cm_thr.c latches it under PREPARE and
                               RECOVERY): part 5's Rearm                  */
    ULONG RecBusChanged;    /* members whose buses a committed recovery's
                               step 6 found changed, with every old bus
                               still owed (RecOldBuses, bit b for bus b):
                               their old entries are released once their
                               devnode's retirement was observed after the
                               latest such change (RecRelOk, R8a)          */
    ULONG RecOldBuses[XHCI_TBT_MAX_TUNNELS][8];
    ULONG RecBusPend;       /* the same for the attempt now running, read
                               afresh by each one, taken at its commit    */
    ULONG RecOldBusPend[XHCI_TBT_MAX_TUNNELS];
    ULONG RecRelOk;         /* RecBusChanged members whose barrier was
                               lifted by an observed retirement           */
    /* Decision U27: the bind (1) or the resume (2) the loss interrupted;
     * the stage is then CM_STAGE_FAILED, sampled and recovered as ACTIVE
     * is, never a protocol failure's (0). */
    ULONG U4LossFrom;
    /* The stor98 channel's two moments (CmNhiSetU4Hooks), NULL until task
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
    /* The thread's wait as it records it (CM_Q_WAIT_*, cm_qryb.h): read by
     * the query device's seed (task 42-A.7). */
    volatile ULONG ThreadWait;
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
    /* IoIsWdmVersionAvailable(1, 0x10), read by the power dispatch at
     * PASSIVE_LEVEL for its completion routine (cm_pwrr.h, task 43.65). */
    BOOLEAN PowerOnNt;
    /* 98 SE and ME: the registry's admission (cm_thr.c CmRegEnter, task
     * 46.14), under WorkLock: closed away from S0, the accesses admitted
     * and not yet left, signalled when none is; the closes that found
     * one still running after their wait. */
    ULONG RegClosed;
    ULONG RegBusy;
    KEVENT RegIdle;
    ULONG RegCloseTimeouts;
#if defined(XHCI_FLAVOUR_QEMU)
    /* XhciCmQemuFailD0, the qemu flavour's alone (cm_power.c, task
     * 45.27): which D0 the bus completes successfully is taken as failed,
     * counted from 1 (0: none), and the D0s counted so far. */
    ULONG QemuFailD0;
    LONG QemuD0Seen;
    /* XhciCmQemuFailReenum (cm_cfgwk.c, task 45.27): how many of the 9x
     * worker's member calls are taken as failed before they are made. */
    ULONG QemuFailReenum;
    /* XhciCmQemuOscNoClear, the qemu flavour's alone (cm_oscnt.c, task
     * 46.12): 1 leaves the boot guard's marker as the router's answer
     * finds it, so a QEMU quit after the start stands for a start that
     * never finished (record 32 section 12.8.15.13's legs). */
    ULONG QemuOscNoClear;
    /* XhciCmQemuEcamFault, the qemu flavour's alone (cm_oscnt.c, task
     * 46.25, record 32 section 12.8.15.18's fault legs): 1 the ECAM base
     * taken 1 MB too high (the page one bus off, not the port), 2 MCFG
     * taken as absent. */
    ULONG QemuEcamFault;
    /* XhciCmQemuWakePowerUpForce, the qemu flavour's alone (task 49.46,
     * design record 21 section 6.3.1): nonzero arms every member at every
     * wake as if its wake power-up had failed, for the legs. */
    ULONG QemuWpuForce;
#endif

    CM_COUNTERS Counters;

    /* The ring interrupt (cm_rirqos.h; design record 21 section 4.5, task
     * 45.22). */
    CM_RIRQ_DRV Rirq;

    /* The status record for the install tool's viewer (cm_statos.c,
     * design record 24 section 3). */
    CM_STATUS_PUB StatusPub;
    /* The 9x bus numbering's last attempt (record 21 section 6.1.1),
     * copied in by the CONFIGMG worker and written by the thread to the
     * hardware key's XhciCmBridgeDiag (REG_BINARY, CM_BRDIAG_WORDS
     * dwords) when BrDiag[0], its sequence, changed. */
    ULONG BrDiag[CM_BRDIAG_WORDS];
    ULONG BrDiagSeq;        /* the last sequence given, across workers    */
    ULONG BrDiagWritten;
    /* The same for design record 28's XhciCmNoIrqDiag (CM_NIDIAG_WORDS),
     * under BrDiagLock too. */
    ULONG NiDiag[CM_NIDIAG_WORDS];
    ULONG NiDiagSeq;
    ULONG NiDiagWritten;
    /* Design record 33 section 9.2's XhciCmBridgeReqDiag
     * (CM_BQDIAG_WORDS), posted by the child filters' PnP dispatch under
     * BrDiagLock (CmRegBreqPost) and written by the thread when BqDiagGen
     * changed: the start of an attempt already recorded changes it too. */
    ULONG BqDiag[CM_BQDIAG_WORDS];
    ULONG BqDiagSeq;
    ULONG BqDiagGen;
    ULONG BqDiagWritten;
    KSPIN_LOCK BrDiagLock;  /* BrDiag and BrDiagSeq; taken inside the
                               worker's own lock, never around a call     */

    /* The query device (cm_qryos.c, design record 25 section 4): set and
     * cleared by the PnP path alone, cleared only once the thread has
     * ended; the epoch, location and instance id taken at its creation;
     * QueryBuf and QueryExt the thread's own, the answer it builds before
     * publishing it into the device's extension. */
    PDEVICE_OBJECT QueryDev;
    ULONG QueryIndex;
    ULONG QueryUnlinked;
    ULONG QueryEpochLo;
    ULONG QueryEpochHi;
    ULONG QueryBdf;
    ULONG QueryIdBytes;
    char QueryId[CM_Q_ID_MAX];
    UCHAR QueryExt[CM_ST_MAX_ROUTERS * CM_Q_EXT_BYTES];
    UCHAR QueryBuf[CM_Q_BUILD_MAX];
    ULONG QueryLastBytes;   /* the answer last published, for its content */
    UCHAR QueryLast[CM_Q_BUILD_MAX];    /* generation                    */
    UCHAR MarksBuf[CM_QM_MAX_BYTES];    /* the marks answer, thread-owned */
    UCHAR NetQBuf[CM_QN_MAX_BYTES];     /* the networking answer, the same
                                           (record 35 section 17)       */
    /* The counters' text for the log answer (record 25 section 4.5), the
     * thread's own, built after each pass and published with the answer. */
    ULONG QueryCtrBytes;
    ULONG QueryCtrCut;
    UCHAR QueryCtr[CM_QL_CTR_MAX];
    /* The firmware tables this start's handover discovery kept (task
     * 46.21, cm_oscos.c; record 32 section 12.8.14): the body of the
     * query device's CM_QA_IOCTL answer and its state, set on the start's
     * thread before the device exists and handed to it by CmQueryOpen
     * (QaBody NULL after); freed by CmQueryForgetTables. */
    UCHAR *QaBody;
    ULONG QaBodyBytes;
    ULONG QaCount;
    ULONG QaState;          /* CM_QA_S_*; 0 nothing looked for            */
    ULONG QaOutcome;
    ULONG QaFlags;
    /* Log.BytesDropped at the last drain for emission (cm_log.c), under
     * LogLock; and the ring as that drain took it, the emitter's own (one
     * emitter at a time: the thread, or the PnP path once it has ended). */
    ULONG LogDroppedEmitted;
    UCHAR LogEmit[XHCI_LOG_RING_BYTES];
    /* The 9x eject asked of this driver and the 9x statement decision
     * (cm_ejectos.c, task 43.18): this start's share; the transactions
     * themselves are that file's image-static records. */
    CM_EJ_DOM Ej;
    /* The Thunderbolt networking service (cm_net.h): the network PDO, kept
     * across a stop, and the data rings' state, memory, timer and DPCs. */
    CM_NET Net;
} CM_NHI, *PCM_NHI;

/* The query device's extension (CM_KIND_QUERY): no lower object. Lock
 * guards every field after it. Its one way to the NHI is Log and LogLock
 * (record 25 section 4.5): set at creation, cleared with Connected in
 * CmQueryClose, followed only with Lock held and Connected set, LogLock
 * taken inside Lock and never the other way. */
typedef struct _CM_QUERY_DEV {
    CM_COMMON Common;
    KSPIN_LOCK Lock;
    ULONG Index;
    ULONG Connected;        /* 0 once its NHI stopped or left            */
    ULONG Stopping;         /* a stop or removal began                   */
    ULONG Pass;             /* publishes since this start, past 0 to 1   */
    ULONG PassTick;         /* the tick count's low word at the last     */
    ULONG Bytes;
    UCHAR Answer[CM_Q_BUILD_MAX];
    PXHCI_LOG Log;          /* the NHI's ring, and its lock               */
    PKSPIN_LOCK LogLock;
    const UCHAR *LogHead;   /* the NHI's head (task 46.13), under LogLock */
    const ULONG *LogHeadBytes;
    const ULONG *LogDroppedEmitted;
    ULONG EpochLo;          /* this start's, for the log answer           */
    ULONG EpochHi;
    ULONG CtrBytes;         /* the counters' text of the last publish     */
    ULONG CtrCut;
    UCHAR Ctr[CM_QL_CTR_MAX];
    /* The PCI answer (record 25 section 4.7): the NHI's location and its
     * domain's tunnel-port set as the thread last published them, under
     * Lock; PciGate (a synchronization event) lets one request at a time
     * build into Pci, outside Lock. */
    ULONG NhiBdf;
    ULONG PortCount;
    CM_QP_PORT_IN Ports[CM_QP_MAX_PORTS];
    KEVENT PciGate;
    UCHAR Pci[CM_QP_MAX_BYTES];
    UCHAR PciScratch[CM_QP_CFG_BYTES];  /* one prefix read, under PciGate */
    /* Windows 98 SE and ME (task 42-A.14): the NHI's CONFIGMG worker, which
     * walks below the members, as the thread last published it, and the
     * askers inside CmCfgwkPciAsk on it, both under Lock; the thread clears
     * Wk and waits for WkUse to fall to 0 before it stops the worker
     * (CmQueryWorkerGone). */
    struct _CM_CFGWK *Wk;
    ULONG WkUse;
    /* The marks answer (task 42-A.5): as the thread last built it, under
     * Lock; MarksRead the newest marks generation a whole answer carried
     * out, which the thread's next publish takes back (MarksReadAny). */
    ULONG MarksBytes;
    ULONG MarksRead;
    ULONG MarksReadAny;
    UCHAR Marks[CM_QM_MAX_BYTES];
    /* The networking answer (record 35 section 17, task 48.18): as the
     * thread last built it, under Lock; 0 bytes before the first. */
    ULONG NetQBytes;
    ULONG NetQPass;         /* Pass and PassTick when these bytes went in */
    ULONG NetQTick;
    UCHAR NetQ[CM_QN_MAX_BYTES];
    /* The firmware tables answer (task 46.21): the body the NHI handed
     * over at creation, never changed after, and its state, under Lock.
     * QaReaders counts the requests copying out of QaBody with Lock
     * released; CmQueryClose takes QaBody away and, while a request still
     * reads it, leaves it in QaGone for the last reader to free. */
    UCHAR *QaBody;
    ULONG QaBodyBytes;
    ULONG QaCount;
    ULONG QaState;
    ULONG QaOutcome;
    ULONG QaFlags;
    ULONG QaTier;
    ULONG QaReaders;
    UCHAR *QaGone;
    /* The state snapshot (record 21 section 8.4, task 49.37): the NHI's
     * extension, set at creation and cleared by CmQuerySnapGone, followed
     * only under a SnapUse hold taken with Lock held and Connected set;
     * the identity hashes' salt, drawn once per start, never on the wire. */
    struct _CM_NHI *SnapNhi;
    ULONG SnapUse;
    ULONG SnapSalt;
} CM_QUERY_DEV, *PCM_QUERY_DEV;

/* The PDOs a bridge filter's snapshot keeps (record 21 section 7.4). */
#define CM_SNAP_MAX 32

/*
 * Both filter roles (record 21 section 3.2): the tunnel bridge's
 * (CM_KIND_FILTER, from AddDevice) and the child filter put on a device
 * newly reported on any filtered bridge or, down the tunnel id's chain, on
 * a PCI-to-PCI bridge below one (CM_KIND_CHILD, created by the bridge
 * filter or a chain child, cm_reenum.c; design record 31 sections 4.1 and
 * 4.3), or by AddDevice. One structure, so both share the remove lock and
 * the device-usage handling of cm_filt.c; a chain child keeps its own
 * snapshot in the bridge's Snap fields.
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
    /* The ACPI-named source (record 22 section 7.6.1, NT): the bridge's
     * id from AddDevice; its _DSD read once after the start (AcpiRead,
     * CmDsdRead's result in AcpiRc); a claim, when it named an NHI, with
     * the normalised reference, the firmware's port number (log only) and
     * the publication serial it was made under. Under the list's lock,
     * cleared at withdrawal. */
    ULONG PortId;
    ULONG AcpiRead;
    ULONG AcpiRc;
    ULONG AcpiClaim;
    ULONG AcpiSerial;
    ULONG AcpiPort;
    char AcpiRef[CM_DSD_NAME_MAX + 1];
    /* The resume mark (record 21 section 6.3): set at the NHI's suspend,
     * cleared by its resume's release; no power IRP is retained. */
    ULONG Held;
    struct _CM_NHI *HeldBy;
    /* The snapshot of section 7.4: written by the PnP dispatch alone,
     * under the registration list's lock (the eject's eligibility reads
     * it from the CM thread, CmReenumService). */
    ULONG SnapCount;
    PVOID Snap[CM_SNAP_MAX];
    ULONG SnapTruncated;    /* the last answer held more than CM_SNAP_MAX  */
    /* Beside each snapshot PDO, the location an attachment of this driver
     * read for it (task 42-A.5), kept while the PDO stays in the snapshot,
     * which is the bridge's rule for "the same device": the marks table
     * takes it back when an entry is made again after its member was
     * inactive (CmMarksLocate). Under the list's lock. */
    ULONG SnapLocd[CM_SNAP_MAX];
    ULONG SnapBus[CM_SNAP_MAX];
    ULONG SnapAddr[CM_SNAP_MAX];
    ULONG RelGen;           /* bumped when the reported population changes */
    ULONG EjectSeen;        /* an orderly child removal since the last
                               eject was posted                           */
    ULONG EjectHold;        /* an eject taken for this member, its tunnel
                               teardown not yet returned: no function
                               behind it is started meanwhile
                               (CmRegStartAllowed)                        */
    ULONG Unsettled;        /* a tunnel change since its last BusRelations */
    ULONG ChangeSeq;        /* tunnel changes seen, for that settling     */
    ULONG RelAnswers;       /* successful BusRelations answers seen: the
                               recovery's retirement proof (record 22
                               section 7.7 part 4) wants one after its
                               removal-only request                       */
    ULONG RelEmptyAt;       /* the RelAnswers value of the last settled
                               answer that was complete and reported no
                               PDO, 0 once a later answer reported one   */
    ULONG ChildLive;        /* child filters attached below and not yet
                               removed (CmRegAttLink, CmRegAttRemoved):
                               that proof waits for each one's own
                               removal                                    */

    /* ---- child ---- */
    struct _CM_FILTER *Bridge;  /* the bridge the child sits below: the one
                               whose BusRelations attached it, or for one
                               AddDevice made the one reporting its PDO when
                               it linked (CmRegAttLink); NULL if none     */
    ULONG ChildCounted;     /* counted in that bridge's ChildLive         */
    ULONG Unattributed;     /* AddDevice made, no bridge named for it yet */
    ULONG TruncSeen;        /* a bridge: an answer once held more PDOs than
                               its snapshot keeps (CM_SNAP_MAX)           */
    ULONG QueryRemoved;     /* a query-remove not cancelled (both kinds)   */
    ULONG SurpriseRemoved;

    /* ---- the marks (task 42-A.5): every attachment of this driver on a
     * device, a bridge filter's or a child's, on the registration list's
     * attachment list, every field under its lock ---- */
    LIST_ENTRY AttLink;
    ULONG AttListed;
    ULONG AddDev;           /* made by AddDevice: a LowerFilters child, or
                               a bridge filter (which may also be a unit) */
    ULONG Located;          /* LocBus and LocAddress read                  */
    ULONG LocBus;
    ULONG LocAddress;       /* DevicePropertyAddress                        */
    ULONG Answered;         /* Removable answered set                      */
    /* The marks entry of its PDO this attachment was last bound to (design
     * record 25 section 7.6): its domain, that domain's start count and
     * the entry's serial; MarkDom NULL before any. Compared, never
     * dereferenced unless the domain is still listed. */
    struct _CM_NHI *MarkDom;
    ULONG MarkStarts;
    ULONG MarkSerial;
    ULONG Stray;            /* counted once as cm.reenum.child.stray       */
    /* Set while this object asks IoGetDeviceProperty for its PDO's
     * location: the PnP manager answers DevicePropertyAddress with an
     * IRP_MN_QUERY_CAPABILITIES sent to the top of this very stack, which
     * must pass through this object unlocated (read on Windows 2000: a
     * double fault, the stack overflowed by the recursion). The PnP
     * dispatch alone touches it. */
    ULONG InLocate;

    /* ---- the tunnel id's chain (design record 31 sections 4.2 and 4.3),
     * the PnP dispatch's alone ---- */
    ULONG Depth;            /* 0 a bridge filter; a child one more than the
                               filter whose BusRelations attached it, 1 for
                               one AddDevice made (a unit)                 */
    struct _CM_FILTER *Parent;  /* the filter whose BusRelations attached
                               this child (a bridge filter or a chain
                               child), NULL for the others; it outlives
                               the child, its device's children being
                               removed before it                          */
    ULONG ChainKind;        /* CM_CHAIN_*: what its compatible ids said    */

    /* ---- a child's interrupt (design record 21 section 6.2.1, task
     * 44.53), Windows 2000 on the Standard PC HAL alone; the PnP
     * dispatch's alone ---- */
    CM_IRQ_STATE Irq;       /* PciNoIrq and the Line added (cm_irq.h)      */
    ULONG IrqBdf;           /* the function's location as CmRegIrqWire read
                               it (XHCI_TBT_BDF, 16 bits), for the log;
                               0 before                                  */

    /* ---- a bridge's requirements (design record 33, task 45.12), NT 5.x
     * alone; the PnP dispatch's alone ---- */
    ULONG BreqTimes;        /* lists of this bridge rewritten since the
                               attachment                                 */
    ULONG BreqStart;        /* a rewrite whose start is not yet seen       */
    ULONG BreqBdf;          /* the bridge's location at that rewrite       */
    ULONG BreqPort;         /* its tunnel port's                           */

    /* ---- the isolation statement (design record 22 section 10.5, task
     * 45.35), a child's, NT 5.x alone; written under cmRegLock ---- */
    ULONG IsoBound;         /* the binding was taken (CmRegIsoBind)        */
    ULONG IsoGone;          /* its removal has begun                       */
    PVOID IsoDom;           /* the bound domain's NHI object, compared only */
    ULONG IsoStarts;        /* its Reenum.Starts at the binding            */
    ULONG IsoMember;        /* the member's location (XHCI_TBT_BDF, 16 bits) */
    ULONG IsoUpMask;        /* bit n-1: USB4 port n's PCIe tunnel was up at
                               the binding, of generation IsoGens[n-1]     */
    ULONG IsoGens[TBT_U4_MAX_PORTS];
    LONG IsoRefs;           /* the interface's references held by xhci98,
                               counted under cmRegLock                     */

    /* ---- the chained unit's parent (design record 34 section 3.10, task
     * 45.13), a chain child on a bridge: classified at each of its
     * BusRelations answers, written and read under cmRegLock ---- */
    ULONG ChainLanding;     /* its location | CM_MK_LANDING_VALID when it is
                               a chain port of an active member, else 0    */
    ULONG ChainSec;         /* its secondary bus at that classification    */
    ULONG ChainMember;      /* the member's location (16 bits)             */
    ULONG ChainWhy;         /* the last outcome logged (XHCI_TBT_CPT_* + 1,
                               0 none yet)                                 */
    ULONG ChainMayLog;      /* a chained unit's: its Removable gate's last
                               logged reading (cm.chain.removable)         */
    /* Design record 34 section 3.12 item 5 (task 48.30), a chain child's,
     * under cmRegLock: its PDO's identity, shared by every filter on that
     * PDO (taken at CmRegAttLink); the begin stamp of each snapshot PDO's
     * first answer (0: the filter's first answer, no provenance); whether
     * it has answered once and its last answer's begin stamp; the token
     * it carries while classified at a bound chain port (CM_MK_TOK_WORDS,
     * 0 none); and the hand-off's hold count (EjectHold above). */
    ULONG PdoSeq;
    ULONG AttSeq;           /* this filter's own stamp, never shared       */
    ULONG SnapSeq[CM_SNAP_MAX];
    ULONG ChainAnswered;
    ULONG ChainAnsSeq;
    ULONG ChainTok[5];

    /* ---- an ASM4242 downstream port's mailbox member (cm_oscnt.c,
     * record 32 section 12.8.15.7), a bridge filter's; Role 0 elsewhere */
    CM_OSC_MEMBER Osc;

    /* ---- the kept child (design record 31 section 4.10, task 48.29): a
     * child cmChildCreate made on a system with CmKeptOn; Kept is set at
     * its creation and never changed. KeptState under Lock; the references
     * are Outstanding (the attach holds the first). KeptLogical and
     * KeptDeparted under cmRegLock. ---- */
    ULONG Kept;
    ULONG KeptState;        /* CM_KEPT_* (cm_ports.h)                      */
    ULONG KeptLogical;      /* no presence in the marks (Logical, Detached) */
    ULONG KeptDeparted;     /* its departure recorded, not yet ended        */
    ULONG KeptRemoves;      /* logical removals seen (the log)              */
    WORK_QUEUE_ITEM KeptWork;   /* its destruction above PASSIVE_LEVEL      */

    /* ---- the wake power-up (design record 21 section 6.3.1, task 49.46),
     * a child's, every field under Lock. Epochs are CmWpuEpoch's values,
     * 0 none. Rel: the logical units of disk type its last BusRelations
     * answer carried (route R1), referenced; Disk, with DiskLink and
     * DiskTok: those the disk arrivals matched to its PDO (route R2),
     * referenced, each current while its link's token is. ---- */
    ULONG WpuFailEpoch;     /* the epoch of its wake D0 that failed        */
    ULONG WpuFailStatus;
    ULONG WpuFailLogged;    /* the epoch it was logged in                  */
    ULONG WpuOtherEpoch;    /* a wake D0 failing with another status: never
                               armed on, logged once an epoch             */
    ULONG WpuOtherStatus;
    ULONG WpuOtherLogged;
    ULONG WpuConsumed;      /* the epoch it was asked or skipped in        */
    ULONG WpuClosed;        /* a removal seen, not cancelled or restarted  */
    ULONG WpuSending;       /* the thread between admission and requests  */
    KEVENT WpuSendDone;     /* signalled whenever WpuSending is 0          */
    ULONG WpuStartGen;      /* successful starts seen                      */
    ULONG WpuOwner;         /* CM_WPU_OWN_* from its driver key            */
    ULONG WpuOwnerOwed;     /* started, its owner not read yet (the thread
                               reads it while the recovery is on)          */
    ULONG WpuOwnerWhy;      /* the two readings: install section in 3:0,
                               the top of its stack in 7:4 (CM_WPU_OWN_*) */
    ULONG WpuRelAnswers;    /* BusRelations answers seen since its start   */
    ULONG WpuRelLogged;     /* the unit count last logged, + 1             */
    ULONG WpuRelCount;
    PDEVICE_OBJECT WpuRel[4];
    ULONG WpuDiskCount;
    PDEVICE_OBJECT WpuDisk[4];
    ULONG WpuDiskLink[4];
    ULONG WpuDiskTok[4];
} CM_FILTER, *PCM_FILTER;

/*
 * The wake power-up (design record 21 section 6.3.1, task 49.46). The
 * child filter's half (cm_filt.c): CmWpuChildInit at its creation;
 * CmWpuD0Seen from its D0 completion (<= DISPATCH_LEVEL); the rest at
 * PASSIVE_LEVEL from its PnP dispatch: CmWpuRelations with an answer on
 * its way back, CmWpuStarted after a successful start, CmWpuClose before
 * a removal, stop or query-remove is forwarded (waiting for a request
 * being sent, bounded; `drop` drops the units), CmWpuReopen after a
 * cancelled query-remove. CmWpuD0Context, the context a child's D0 is
 * dispatched with (the epoch and its start count). CmWpuPowerSeen, at the
 * dispatch of every power IRP a filter of this driver receives, and
 * CmWpuNhiPowerSeen at an NHI's (<= DISPATCH_LEVEL): a system sleep or
 * shutdown closes the asks of every domain at once, before any thread
 * runs, and begins a new epoch (once per sleep); a device power-down
 * closes every domain until its own next release, the NHI's own its
 * domain besides. CmWpuDomStop, from the NHI's PnP dispatch at a
 * query-stop, query-remove, stop, surprise removal or removal: that
 * domain latched closed, reopened only by its own cancel (CmWpuDomCancel)
 * or start (CmWpuDomStart). CmWpuResumeBegin, when the NHI's resume work
 * begins: the identity its release is checked against (cm_reenum.c).
 * CmWpuAdded, from the NHI's AddDevice: its value pending. CmWpuCount,
 * from its PnP dispatch: at its start once its values are read (`on`:
 * counted when XhciCmWakePowerUp is 1, no longer pending), and at its
 * stop, surprise removal or removal. CmWpuOn: TRUE while a counted NHI is
 * started, while any NHI's value is pending, or before any NHI was added
 * (so that a drive started before its NHI is not missed); FALSE, the
 * filters record nothing for the wake power-up.
 */
VOID CmWpuChildInit(PCM_FILTER child);
VOID CmWpuD0Seen(PCM_FILTER child, ULONG dispatchEpoch, NTSTATUS status);
VOID CmWpuRelations(PCM_FILTER child, const DEVICE_RELATIONS *rel);
VOID CmWpuStarted(PCM_FILTER child);
VOID CmWpuClose(PCM_FILTER child, BOOLEAN drop);
VOID CmWpuReopen(PCM_FILTER child);
ULONG CmWpuEpochNow(VOID);
/* cm_reg.c: the owner test's install-section reading, CM_WPU_OWN_*
 * (cm_ports.h). PASSIVE_LEVEL. */
ULONG CmRegWpuOwner(PDEVICE_OBJECT pdo);
PVOID CmWpuD0Context(PCM_FILTER child);
VOID CmWpuPowerSeen(UCHAR minor, ULONG type, ULONG state);
VOID CmWpuNhiPowerSeen(PCM_NHI nhi, UCHAR minor, ULONG type, ULONG state);
VOID CmWpuDomStop(PCM_NHI nhi);
VOID CmWpuDomCancel(PCM_NHI nhi);
VOID CmWpuDomStart(PCM_NHI nhi);
VOID CmWpuResumeBegin(PCM_NHI nhi);
BOOLEAN CmWpuOn(VOID);
VOID CmWpuAdded(PCM_NHI nhi);
VOID CmWpuCount(PCM_NHI nhi, BOOLEAN on);

/* cm_filt.c: the kept child (design record 31 section 4.10). The
 * dispatch of every major function on one (admission and the lower
 * object's reference, K3 and K7); the I/O manager's FastIoDetachDevice
 * for a filter or child of this driver (K6); a child cmChildCreate makes
 * on 8.1, made kept before its attach. PASSIVE_LEVEL for the
 * callback; the dispatch at its request's IRQL. */
NTSTATUS CmKeptDispatch(PCM_FILTER filter, PIRP irp);
VOID CmKeptInit(PCM_FILTER child);
VOID CmFilterDetachCb(PCM_FILTER filter, PDEVICE_OBJECT target);
/* cm_reenum.c: the kept child's marks (K4, K5, K8). CmRegAttDepart: the
 * departure recorded once and the child no presence, at a logical
 * removal. CmRegKeptAbsent: no presence (the callback). CmRegKeptAttached:
 * presence back and the departure ended, the entry brought up to date. CmRegKeptNote: a record of the kept child's to the first
 * listed domain, reading neither Parent nor Bridge. Any IRQL <=
 * DISPATCH_LEVEL but CmRegAttDepart, PASSIVE_LEVEL. */
VOID CmRegAttDepart(PCM_FILTER filter, BOOLEAN afterQr);
VOID CmRegKeptAbsent(PCM_FILTER filter);
VOID CmRegKeptAttached(PCM_FILTER filter);
VOID CmRegKeptNote(PCM_FILTER filter, const char *label, ULONG event);

#define CM_CHAIN_UNKNOWN    0UL
#define CM_CHAIN_BRIDGE     1UL /* PCI\CC_0604: reads its BusRelations      */
#define CM_CHAIN_OTHER      2UL

/* cm_entry.c */
extern PDRIVER_OBJECT CmDriverObject;
NTSTATUS CmCompleteIrp(PIRP irp, NTSTATUS status, ULONG_PTR information);
NTSTATUS CmForwardSync(PCM_COMMON common, PIRP irp);
VOID CmZero(PVOID p, ULONG n);
/* 100 ns units in a millisecond: a relative due time's scale. */
#define CM_CLOCK_UNITS_PER_MS 10000UL
/* A relative due time of `ms` milliseconds (0 a poll, held to 400 s),
 * from 32-bit arithmetic. Any IRQL. */
VOID CmRelativeMs(PLARGE_INTEGER due, ULONG ms);
/* The unsatisfied returns a gate's wait (CmWaitEvent) takes before it is
 * abandoned: hcd.h's HCD_GATE_WAIT_BOUND. */
#define CM_GATE_WAIT_BOUND 100UL
/* A wait that returns only once the event satisfied it (bound 0), or
 * after `bound` unsatisfied returns in a row; the count of unsatisfied
 * returns, below `bound` when the event was taken. IRQL: PASSIVE_LEVEL. */
ULONG CmWaitEvent(PKEVENT event, ULONG bound);
/* A wait of at most `ms` milliseconds by the clock that an unsatisfied
 * early return does not end: TRUE when the event satisfied it. IRQL:
 * PASSIVE_LEVEL. */
BOOLEAN CmWaitEventMs(PKEVENT event, ULONG ms);

/* cm_nhi.c */
NTSTATUS CmNhiAddDevice(PDRIVER_OBJECT driver, PDEVICE_OBJECT pdo,
                        const CM_IDS *ids);
NTSTATUS CmNhiPnp(PCM_NHI nhi, PIRP irp);
VOID CmNhiStartWork(PCM_NHI nhi);
VOID CmNhiBind(PCM_NHI nhi);
VOID CmNhiStopWork(PCM_NHI nhi);
VOID CmNhiPollPass(PCM_NHI nhi);
VOID CmNhiDrainActions(PCM_NHI nhi);
/* The ICM client's loss (TbtIcmLost), its actions drained inside the
 * re-enumeration seam's loss bracket (CmReenumLossBegin), then the seam's
 * CONTROLLER_UNAVAILABLE request when `request` (once per episode), else
 * the bracket closed. PASSIVE_LEVEL, the connection-manager thread. */
VOID CmNhiLost(PCM_NHI nhi, ULONG episode, BOOLEAN request);
BOOLEAN CmNhiSetBusMaster(PCM_NHI nhi, BOOLEAN on);
VOID CmNhiQuiesce(PCM_NHI nhi);
VOID CmNhiPlatformRelease(PCM_NHI nhi, BOOLEAN mayClear);
BOOLEAN CmNhiPlatformInit(PCM_NHI nhi);
BOOLEAN CmNhiReinit(PCM_NHI nhi);
BOOLEAN CmNhiFailed(PCM_NHI nhi);
BOOLEAN CmNhiRunning(PCM_NHI nhi);
VOID CmNhiUnbind(PCM_NHI nhi);
/* Record 22 section 9.4's image-static slots: DriverEntry, once. */
VOID CmNhiCarryInit(VOID);
BOOLEAN CmNhiMayClearPower(PCM_NHI nhi);
/* The stor98 channel's registration (CM_U4_UP_HOOK, CM_U4_DOWN_HOOK); on
 * the thread, or before the start. */
VOID CmNhiSetU4Hooks(PCM_NHI nhi, CM_U4_UP_HOOK up, CM_U4_DOWN_HOOK down,
                     PVOID context);
/* For cm_reenum.c, profile-neutral (cm_reenum.h, "THE USB4 PROFILE"). */
VOID CmNhiNamePorts(PCM_NHI nhi);
VOID CmNhiSetChanged(PCM_NHI nhi);
/* The connection generation of the USB4 port member `member` corresponds
 * to (record 22 section 7.2), 0 where none does or under the ICM client;
 * on the thread (task 42-A.5's pending departures). */
ULONG CmNhiMemberGen(PCM_NHI nhi, ULONG member);
/* Task 43.18 (cm_ejectos.c): the USB4 port member `member` corresponds
 * to, 0 where none does or under the ICM client; and that member's PCIe
 * teardown (record 22 section 10.4, the eject hook's own path), TRUE when
 * its read-back proved the path closed. On the thread. */
ULONG CmNhiMemberPort(PCM_NHI nhi, ULONG member);
BOOLEAN CmNhiU4EjectMember(PCM_NHI nhi, ULONG bdf);
/* cm_power.c's resume: the USB4 batch taken and its UPs published before
 * the held bridges are released (cm_nhi.c, cmU4Drain). */
VOID CmNhiU4Take(PCM_NHI nhi);
VOID CmNhiWaitService(PCM_NHI nhi);
BOOLEAN CmNhiActionLive(PCM_NHI nhi, const TBT_ICM_ACTION *action);
ULONG CmNhiActionMember(PCM_NHI nhi, const TBT_ICM_ACTION *action);
ULONG CmNhiPortMember(PCM_NHI nhi, ULONG port);

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
/* Record 22 section 7.7's recovery (task 42-A.10): one probe of a
 * contained USB4 instance at `nowMs` and, as the monitor's table admits,
 * the preparation, the recovery and its commit (cm_nhi.c). The thread,
 * PASSIVE_LEVEL. */
VOID CmNhiU4Probe(PCM_NHI nhi, ULONG nowMs);
/* The retirement barrier of part 4, once per pass of an ACTIVE USB4
 * instance before its tick: each barred member's devnode retirement read,
 * its removal-only request made or retried, and every port's Barred set
 * from it (TbtU4SetBarred). The thread, PASSIVE_LEVEL. */
VOID CmNhiU4Barrier(PCM_NHI nhi);
ULONG CmNhiU4WorkerHold(PCM_NHI nhi);
/* Whether the instance is one the probe serves: a contained USB4 instance
 * ACTIVE, or FAILED by a loss inside its bind or resume (U27). */
BOOLEAN CmNhiU4Probing(PCM_NHI nhi);
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
/* A system thread at `routine` with `context`, its handle closed at once:
 * cm_thr.c is the one site of ZwClose for the threads it does not join by
 * handle (the call workers, cm_callwk.c; the CONFIGMG worker, cm_cfgwk.c).
 * PASSIVE_LEVEL. */
NTSTATUS CmThrSpawn(VOID (NTAPI *routine)(PVOID), PVOID context);
VOID CmThrAccessors(PCM_NHI nhi);
ULONG CmCfgRead32(PCM_NHI nhi, ULONG offset);
/* One BAR0 DW under the PROBE class (the monitor's and the validation's
 * reads): refused, all ones, only once revoked. IRQL: PASSIVE_LEVEL. */
ULONG CmThrProbe32(PCM_NHI nhi, ULONG offset);
/* The monitor's sample and its actions, on the thread between the queued
 * work and the poll. IRQL: PASSIVE_LEVEL. */
VOID CmThrLiveness(PCM_NHI nhi);
/* Whether the thread's next wait has no timeout: no client to poll and no
 * monitored state (the query device's inert flag). Any thread; the fields
 * read are the thread's own and may change after. IRQL: <= DISPATCH_LEVEL. */
BOOLEAN CmThrInertNext(PCM_NHI nhi);
BOOLEAN CmCfgWrite16(PCM_NHI nhi, ULONG offset, USHORT value);

/* cm_power.c */
NTSTATUS CmNhiPower(PCM_NHI nhi, PIRP irp);
VOID CmPowerSystemWork(PCM_NHI nhi, PIRP irp);
VOID CmPowerD3Work(PCM_NHI nhi, PIRP irp);
VOID CmPowerD0Work(PCM_NHI nhi, PIRP irp);
VOID CmPowerDirectWork(PCM_NHI nhi, PIRP irp);
VOID CmPowerQuiesceWork(PCM_NHI nhi);
BOOLEAN CmPowerHold(PCM_NHI nhi);
VOID CmPowerRelease(PCM_NHI nhi);
VOID CmPowerDrain(PCM_NHI nhi);
VOID CmPowerSetSystem(PCM_NHI nhi, SYSTEM_POWER_STATE state);
BOOLEAN CmPowerSleeping(PCM_NHI nhi);
/* The registry's admission on 98 SE and ME (task 46.14): CmRegEnter
 * before each registry access of the connection manager's thread (FALSE:
 * none may be made now), CmRegLeave after it; always admitted on NT.
 * CmRegCloseWait from the power dispatch of a system IRP away from S0;
 * CmRegSync from its completion, once the state is published. IRQL:
 * Enter, Leave and CloseWait PASSIVE_LEVEL; Sync <= DISPATCH_LEVEL. */
BOOLEAN CmRegEnter(PCM_NHI nhi);
/* cm_oscos.c: the platform's USB4 handover on 98 SE and ME (design record
 * 32 section 12.8, task 46.12). CmOscInit at DriverEntry; CmOscHandover
 * in the start, on the thread, before ring 0. PASSIVE_LEVEL. */
VOID CmOscInit(VOID);
VOID CmOscHandover(PCM_NHI nhi);
/* cm_oscnt.c: an NT kernel routine by its name, for any xhcicm caller
 * (record 32 section 12.8.15.15): MmGetSystemRoutineAddress, imported by
 * the amd64 image, handed out on x86 by the helper's channel minor 1
 * (xhcihlpr.sys, asked once a load); NULL on 98 SE and ME, before
 * CmOscntInit, where the helper is absent or answers without it, or when
 * the routine is not exported. PASSIVE_LEVEL; no lock held that a
 * caller of the handover holds. */
PVOID CmRoutine(const WCHAR *name);
/* The read path's discovery on NT (cm_oscos.c, record 32 section
 * 12.8.15.17): the snapshot of the memory the system manages (Ranges,
 * Count), the root pointer (B, D) or the root table and MCFG the system
 * gave (C, each checked by CmOscpFwTable), the host found by bus.
 * CmOscNtScan: E0000h to FFFFFh for the root pointer into Rsdp, a
 * CM_OSCP_S_*; CmOscNtDiscover: a CM_OSC_* reason and, on CM_OSC_OK, the
 * command's data byte; *refused the reads the snapshot refused. The
 * NHI's thread, PASSIVE_LEVEL. */
struct _CM_OSCP_RANGE;
typedef struct _CM_OSC_NTSRC {
    const struct _CM_OSCP_RANGE *Ranges;
    ULONG Count;
    UCHAR Rsdp[36];
    const UCHAR *Root;
    const UCHAR *Mcfg;
    ULONG RootBdf;
    ULONG UspBdf;
    ULONG DspBdf;
} CM_OSC_NTSRC;
ULONG CmOscNtScan(const CM_OSC_NTSRC *s, UCHAR *rsdp, PULONG refused);
ULONG CmOscNtDiscover(PCM_NHI nhi, const CM_OSC_NTSRC *s, PULONG data,
                      PULONG ecamBase, PULONG ecamEnd, PULONG refused);
/* Record 32 section 12.8.15.18: the root table and the MCFG pass alone
 * (the same code as the discovery's, no SSDT read), for the ECAM base on
 * 2000 and 32-bit XP; CM_OSC_OK with *base and *busEnd, else the reason
 * (0x27 a read refused by the page check). PASSIVE_LEVEL. */
ULONG CmOscNtMcfg(PCM_NHI nhi, const CM_OSC_NTSRC *s, PULONG base,
                  PULONG busEnd, PULONG refused);
/* The status record's handover word (section 12.8.11): the tier in bits
 * 2:0 (cm_osc.h's CM_OSC_TIER_*), bit 3 the handover written this boot.
 * The thread, PASSIVE_LEVEL. */
ULONG CmOscReport(PCM_NHI nhi);
/* cm_oscnt.c: the same handover on the NT targets (record 32 section
 * 12.8.15; bit 7 of CmOscntReport: refused, no routine route, 108h): E, a
 * known board's profile, and the mailbox's exclusion by
 * the members' filters. CmOscntInit at DriverEntry; CmOscntHandover in
 * the start beside CmOscHandover; CmOscntSystem from a system SET_POWER's
 * completion (<= DISPATCH_LEVEL); CmOscntResume in the resume before the
 * client's (the S4 send); CmOscntRouter where the host router first
 * answers after a send; CmOscntShutdown at an orderly shutdown's
 * quiesce; CmOscntReport for CmOscReport on NT (bits 2:0 the tier, 3
 * written this boot, 4 the guard's skip, 5 a timeout this boot, 6 an
 * unknown board left at reads 0). The thread, PASSIVE_LEVEL, but
 * CmOscntSystem. Nothing on 98 SE and ME. */
#define CM_OSCN_F_GUARD     0x10UL
#define CM_OSCN_F_TIMEOUT   0x20UL
#define CM_OSCN_F_UNKNOWN   0x40UL
#define CM_OSCN_F_NOROUTE   0x80UL
#define CM_OSCN_F_EXPIRED   0x100UL
#define CM_OSCN_F_ECAMOFF   0x200UL     /* 113h: ECAM needs reads 1      */
/* Every system: the same handover code in another build (tier 3) refused
 * because XhciCmAsm4242Structural is 0 (0Eh; on NT 110h with cm.osc.read
 * 0Eh), set at that refusal alone and cleared at each start (record 32
 * section 12.8.11, task 47.29). */
#define CM_OSCN_F_SAMEOFF   0x400UL
/* CmOscntHandover's and CmOscntRetry's answer: done (sent, refused, or
 * nothing to do), or deferred (record 32 section 12.8.15.16). */
#define CM_OSCN_DONE        0UL
#define CM_OSCN_DEFER       1UL
VOID CmOscntInit(VOID);
ULONG CmOscntHandover(PCM_NHI nhi);
ULONG CmOscntRetry(PCM_NHI nhi);
/* cm_nhi.c: a deferred activation's pass, on the thread. */
VOID CmNhiDeferPass(PCM_NHI nhi);
VOID CmOscntSystem(PCM_NHI nhi, SYSTEM_POWER_STATE state);
VOID CmOscntResume(PCM_NHI nhi);
VOID CmOscntRouter(PCM_NHI nhi);
VOID CmOscntShutdown(PCM_NHI nhi);
ULONG CmOscntReport(PCM_NHI nhi);
/* The members (cm_oscnt.c): AddDevice of the upstream port's and the
 * xHCI's filter, their dispatch, and the bridge filter's member half
 * (a downstream port), from cm_filt.c. PASSIVE_LEVEL, but
 * CmOscmPowerEnter and CmOscmPowerDone (the power context). */
NTSTATUS CmOscmAddDevice(PDRIVER_OBJECT driver, PDEVICE_OBJECT pdo,
                         ULONG role);
NTSTATUS CmOscmPnp(PCM_MEMBER m, PIRP irp);
NTSTATUS CmOscmPower(PCM_MEMBER m, PIRP irp);
VOID CmOscmInit(PCM_OSC_MEMBER m, ULONG role, PDEVICE_OBJECT pdo,
                PDEVICE_OBJECT lower);
BOOLEAN CmOscmTracked(PIRP irp);
VOID CmOscmEnter(PCM_OSC_MEMBER m);
VOID CmOscmLeave(PCM_OSC_MEMBER m);
VOID CmOscmAfter(PCM_OSC_MEMBER m, UCHAR minor, NTSTATUS status);
VOID CmOscmSurprise(PCM_OSC_MEMBER m);
VOID CmOscmRemove(PCM_OSC_MEMBER m);
NTSTATUS CmOscmPowerPass(PCM_OSC_MEMBER m, PDEVICE_OBJECT lower, PIRP irp);
/* cm_statos.c: the boot guard's marker XhciCmOscPending in the NHI's
 * hardware key (record 32 section 12.8.15.13). Read: CM_OSCG_M_*, its
 * kind in *kind. Write: TRUE when written and flushed (*written, *flushed
 * each). Delete: *deleted, *flushed. The thread, PASSIVE_LEVEL. */
ULONG CmStatusMarkerRead(PCM_NHI nhi, PULONG kind);
VOID CmStatusMarkerWrite(PCM_NHI nhi, ULONG kind, BOOLEAN *written,
                         BOOLEAN *flushed);
VOID CmStatusMarkerDelete(PCM_NHI nhi, BOOLEAN *deleted, BOOLEAN *flushed);
/* Whether the hardware key can be flushed now, nothing written (FALSE on
 * x86 until CmStatusMarkerRoutines gave the routine). */
BOOLEAN CmStatusMarkerFlushable(PCM_NHI nhi);
/* x86: ZwFlushKey and ZwDeleteValueKey as cm_oscnt.c found them through
 * the helper's channel (record 32 section 12.8.15.15), set once before
 * any marker access; ignored on amd64, which imports both. PASSIVE_LEVEL,
 * the NHI's thread. */
VOID CmStatusMarkerRoutines(PVOID flushKey, PVOID deleteValueKey);
/* cm_nvlinkos.c: the once-per-boot objects of record 32 section 12.8.13.5,
 * \Callback\Xhci98CmOsc<bdf>[A]: CM_NVO_* of an open (`create` FALSE)
 * or a creation (`create` TRUE) of the permanent named callback object.
 * PASSIVE_LEVEL. */
#define CM_NVO_FOUND    0UL
#define CM_NVO_ABSENT   1UL
#define CM_NVO_MADE     2UL
#define CM_NVO_FAILED   3UL
ULONG CmNvOscObject(ULONG bdf, BOOLEAN success, BOOLEAN create);
VOID CmRegLeave(PCM_NHI nhi);
VOID CmRegCloseWait(PCM_NHI nhi);
VOID CmRegSync(PCM_NHI nhi);

/* cm_filt.c */
/* `portId` is the bridge's CM_PORTS_ID (record 22 section 7.6.1);
 * `oscRole` CM_OSCP_R_DSP for an ASM4242 downstream port (its mailbox
 * member half, cm_oscnt.c), else 0. */
NTSTATUS CmFilterAddDevice(PDRIVER_OBJECT driver, PDEVICE_OBJECT pdo,
                           ULONG portId, ULONG oscRole);

/* cm_dsdos.c (record 22 section 7.6.1): the bridge filter's _DSD read,
 * after its start completed and its entry was published; it returns at
 * once below Vista (WDM 6.0: 98 SE and ME, 2000, XP, XP x64).
 * PASSIVE_LEVEL, no lock held. */
VOID CmDsdFilterRead(PCM_FILTER filter);

/* cm_dsdos.c: the NHI's own ACPI name, normalised into `out` (cap bytes);
 * its length, 0 when not read (an NHI not AMD's, the routine not found by
 * CmRoutine, the property absent). PASSIVE_LEVEL. */
ULONG CmDsdNhiName(struct _CM_NHI *nhi, char *out, ULONG cap);
/* The child by AddDevice (record 21 section 7.4, revision 3): attached to
 * the top of what is on the PDO, passive until associated, and never a
 * failed AddDevice for want of a role (decision M7). */
NTSTATUS CmChildAddDevice(PDRIVER_OBJECT driver, PDEVICE_OBJECT pdo);
/* A child filter's removal reference for the chain action (cm_filt.c,
 * task 45.13): FALSE once its removal has begun. */
BOOLEAN CmFilterTryHold(PCM_FILTER filter);
VOID CmFilterUnhold(PCM_FILTER filter);
NTSTATUS CmFilterPnp(PCM_FILTER filter, PIRP irp);
NTSTATUS CmFilterPower(PCM_FILTER filter, PIRP irp);

/* cm_reg.c */
VOID CmReadValues(PCM_NHI nhi);
/* Design record 21 section 6.2.1's gate: 1 on Windows 2000 (NT 5.0) whose
 * HAL is the Standard PC HAL (Root\PCI_HAL\0000's first HardwareID
 * e_isa_up), else 0; read once by CmReadIrqGate in DriverEntry on the NT
 * role, PASSIVE_LEVEL, and only read after. */
extern ULONG CmIrqGate;
VOID CmReadIrqGate(VOID);
/* Design record 33's gate (task 45.12): the NT minor version plus one on
 * NT 5.x (1 on 2000, 2 on XP, 3 on XP x64), else 0; read once by
 * CmReadBreqGate in DriverEntry on the NT role, and only read after. */
extern ULONG CmBreqGate;
/* Design record 22 section 10.5 (task 45.35), cm_reenum.c: the binding of
 * a child filter to the USB4 port whose PCIe tunnel its device started
 * under (TRUE when taken), the decision for it (cm_iso.h CM_ISO_*), and
 * its end at the child's removal. PASSIVE_LEVEL or below; cmRegLock taken
 * inside. */
BOOLEAN CmRegIsoBind(PCM_FILTER child);
ULONG CmRegIsoAsk(PCM_FILTER child);
VOID CmRegIsoGone(PCM_FILTER child);
/* The interface's references after the first (which CmRegIsoBind takes),
 * counted under cmRegLock; the binding retired with the last, so the next
 * start binds afresh. IRQL <= DISPATCH_LEVEL. */
VOID CmRegIsoRef(PCM_FILTER child);
VOID CmRegIsoDeref(PCM_FILTER child);
VOID CmReadBreqGate(VOID);

/* cm_statos.c: CmStatusOpen in the PnP start's dispatch (the keys and the
 * first record); CmStatusPass once per thread pass; CmStatusStopped after
 * the stop's work; CmStatusClose in the removal (idempotent). */
VOID CmStatusOpen(PCM_NHI nhi);
VOID CmStatusClose(PCM_NHI nhi);
/* The 9x eject's result (`result` TRUE, CM_EJ_RESULT_VALUE) or reply
 * (CM_EJ_REPLY_VALUE) into the NHI's hardware key (task 43.18); FALSE when
 * the key is not open or the write failed. On the thread. */
BOOLEAN CmStatusEjectWrite(PCM_NHI nhi, BOOLEAN result, const UCHAR *bytes,
                           ULONG length);
VOID CmStatusPass(PCM_NHI nhi);
VOID CmStatusStopped(PCM_NHI nhi);
/* XhciCmQueryName (REG_SZ) in the hardware key, empty to withdraw it;
 * PnP path, the key open. */
VOID CmStatusQueryName(PCM_NHI nhi, PCUNICODE_STRING name);

/* cm_qryos.c (design record 25 section 4): CmQueryInit in DriverEntry;
 * CmQueryOpen after a start that succeeded; CmQueryStopping as a stop,
 * surprise removal or removal begins; CmQueryClose once the thread has
 * ended (idempotent); CmQueryPublish after each thread pass, `inert` when
 * the thread will next wait with no timeout; CmQueryDispatch for every
 * request to a CM_KIND_QUERY object. */
VOID CmQueryInit(VOID);
VOID CmQueryOpen(PCM_NHI nhi);
VOID CmQueryStopping(PCM_NHI nhi);
VOID CmQueryClose(PCM_NHI nhi);
VOID CmQueryPublish(PCM_NHI nhi, BOOLEAN inert);
/* Before the 98 worker is stopped (cm_reenum.c): the query device lets go
 * of it, every asker already posting done. On the thread, PASSIVE_LEVEL. */
VOID CmQueryWorkerGone(PCM_NHI nhi);
NTSTATUS CmQueryDispatch(PDEVICE_OBJECT device, PIRP irp);
/* cm_snapos.c (record 21 section 8.4): the salt, at the query device's
 * creation; the hold's end, in CmQueryClose before the NHI can go (it
 * clears SnapNhi and waits until no gather holds it); CM_QS_IOCTL. */
VOID CmQuerySnapSalt(PCM_QUERY_DEV q, const CM_NHI *nhi);
VOID CmQuerySnapGone(PCM_NHI nhi);
NTSTATUS CmQuerySnap(PCM_QUERY_DEV q, PIRP irp, BOOLEAN nt);
/* The firmware tables the NHI still holds (none handed to a query
 * device) freed, its state cleared (task 46.21): at the next handover and
 * at a stop. PASSIVE_LEVEL, the PnP path or the start's thread. */
VOID CmQueryForgetTables(PCM_NHI nhi);

/* cm_nhi.c: the counter blocks the stop dump writes (the NHI's, the
 * ring's, and the ICM's or the USB4 connection manager's), as text lines
 * (CmQueryLines) appended to out at *at; on the thread, PASSIVE_LEVEL. */
VOID CmNhiCounterLines(PCM_NHI nhi, UCHAR *out, ULONG room, PULONG at,
                       PULONG cut);

/* cm_log.c */
VOID CmLogInit(PCM_NHI nhi);
VOID CmLogApply(PCM_NHI nhi);
VOID CmLogNote(PCM_NHI nhi, const char *label, ULONG value);
/* A record whose value is an address (a bus address, a devnode handle):
 * XhciLogAppendAddress under LogLock, so it reaches the ring at
 * XhciCmLogVerbosity 4 alone, as xhci98.sys's address records do (design
 * record 25 revision 12). Defined in cm_nhi.c. <= DISPATCH_LEVEL. */
VOID CmLogNoteAddress(PCM_NHI nhi, const char *label, ULONG value);
VOID CmLogEmit(PCM_NHI nhi, ULONG reason);
/* Task 46.13: the one record funnel, a stamp before it when due and the
 * head's copy (address 1: XhciLogAppendAddress's level 4 rule); the log's
 * millisecond clock (a leaf lock), set up by DriverEntry. <=
 * DISPATCH_LEVEL. */
VOID CmLogRecord(PCM_NHI nhi, const char *label, ULONG value, ULONG address);
VOID CmLogClockInit(VOID);
ULONG CmLogClockMs(VOID);

/* cm_pool.c */
VOID CmPoolInit(VOID);
PVOID CmAlloc(ULONG bytes);
PVOID CmAllocPaged(ULONG bytes);
VOID CmFree(PVOID p);
NTSTATUS CmDmaOpen(PCM_NHI nhi);
VOID CmDmaClose(PCM_NHI nhi);
/* One common buffer of `bytes` on the NHI's adapter, cached, below 4 GB and
 * 16-byte aligned, zeroed; its bus address in *pa. NULL on a failure. The
 * Thunderbolt networking rings' (cm_net.c). PASSIVE_LEVEL. */
PVOID CmDmaCommonAlloc(PCM_NHI nhi, ULONG bytes, PULONG pa);
VOID CmDmaCommonFree(PCM_NHI nhi, ULONG bytes, ULONG pa, PVOID va);

/* cm_net.c (design record 35 sections 5 and 6, task 45.59). */
VOID CmNetInit(PCM_NHI nhi);
VOID CmNetBindOps(PCM_NHI nhi, BOOLEAN fw);
VOID CmNetPass(PCM_NHI nhi);
VOID CmNetAck(PCM_NHI nhi);
VOID CmNetStopWork(PCM_NHI nhi);
/* The ring interrupt's DPC, on a claim while the data ring's cause is
 * open (record 35 section 5.5): the receive service kicked while it is up
 * and not closing. DISPATCH_LEVEL. */
VOID CmNetIrqKick(PCM_NHI nhi);
VOID CmNetLogCounters(PCM_NHI nhi);
/* The networking answer's inputs gathered and built into NetQBuf (record
 * 35 section 17, task 48.18): its bytes. On the thread, PASSIVE_LEVEL, no
 * lock held; calls the network driver's Stats callback. */
ULONG CmNetQueryFill(PCM_NHI nhi);
VOID CmNetStarted(PCM_NHI nhi);
VOID CmNetGone(PCM_NHI nhi);
VOID CmNetRemove(PCM_NHI nhi);
NTSTATUS CmNetBusRelations(PCM_NHI nhi, PIRP irp);
NTSTATUS CmNetPdoPnp(PCM_NETPDO pdo, PIRP irp);
NTSTATUS CmNetPdoPower(PCM_NETPDO pdo, PIRP irp);
NTSTATUS CmNetPdoOther(PCM_NETPDO pdo, PIRP irp);
/* The thread's CM_WORK_NET and its poll pass (cm_nhi.c): CmNetPass where
 * a connection manager runs, then the acknowledgement. */
VOID CmNhiNetWork(PCM_NHI nhi);

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
