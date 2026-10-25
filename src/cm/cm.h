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
 *                on an active tunnel bridge, or (task 42-A.5, record 21
 *                section 7.4 revision 3) by AddDevice for a device whose
 *                LowerFilters value the mark service wrote: cm_filt.c.
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
 * Beside the roles, a fourth kind of device object, no PnP role: the
 * query device of a started NHI (CM_KIND_QUERY; cm_qryos.c, design record
 * 25 section 4), a named control device the dispatch routines tell apart
 * by its Kind before anything else; its answer is built by cm_query.c.
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
#include "cm_eject.h"

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

    CM_COUNTERS Counters;

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
    /* The counters' text for the log answer (record 25 section 4.5), the
     * thread's own, built after each pass and published with the answer. */
    ULONG QueryCtrBytes;
    ULONG QueryCtrCut;
    UCHAR QueryCtr[CM_QL_CTR_MAX];
    /* Log.BytesDropped at the last drain for emission (cm_log.c), under
     * LogLock; and the ring as that drain took it, the emitter's own (one
     * emitter at a time: the thread, or the PnP path once it has ended). */
    ULONG LogDroppedEmitted;
    UCHAR LogEmit[XHCI_LOG_RING_BYTES];
    /* The 9x eject asked of this driver and the 9x statement decision
     * (cm_ejectos.c, task 43.18): this start's share; the transactions
     * themselves are that file's image-static records. */
    CM_EJ_DOM Ej;
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
} CM_QUERY_DEV, *PCM_QUERY_DEV;

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
    /* The resume mark (record 21 section 6.3): set at the NHI's suspend,
     * cleared by its resume's release; no power IRP is retained. */
    ULONG Held;
    struct _CM_NHI *HeldBy;
    /* The snapshot of section 7.4: written by the PnP dispatch alone,
     * under the registration list's lock (the eject's eligibility reads
     * it from a child's removal). */
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
BOOLEAN CmPowerHold(PCM_NHI nhi);
VOID CmPowerRelease(PCM_NHI nhi);
VOID CmPowerDrain(PCM_NHI nhi);
VOID CmPowerSetSystem(PCM_NHI nhi, SYSTEM_POWER_STATE state);
BOOLEAN CmPowerSleeping(PCM_NHI nhi);

/* cm_filt.c */
NTSTATUS CmFilterAddDevice(PDRIVER_OBJECT driver, PDEVICE_OBJECT pdo);
/* The child by AddDevice (record 21 section 7.4, revision 3): attached to
 * the top of what is on the PDO, passive until associated, and never a
 * failed AddDevice for want of a role (decision M7). */
NTSTATUS CmChildAddDevice(PDRIVER_OBJECT driver, PDEVICE_OBJECT pdo);
NTSTATUS CmFilterPnp(PCM_FILTER filter, PIRP irp);
NTSTATUS CmFilterPower(PCM_FILTER filter, PIRP irp);

/* cm_reg.c */
VOID CmReadValues(PCM_NHI nhi);

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
