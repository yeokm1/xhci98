/*
 * cm_rirqos.h - the NHI's ring interrupt, its driver half (design record
 * 21 section 4.5, record 22 section 5.4; roadmap-usb4.md task 45.22):
 * cm_rirqos.c. The pure half is cm_rirq.h.
 *
 * WHO TOUCHES WHAT. The connection-manager thread alone connects, arms,
 * masks, tests and disconnects, and owns every field below but the five
 * the service routine and its DPC share with it (Connected, Armed,
 * StormMasked, DpcSeq and DpcsOwed, with the claim counters): those are
 * aligned LONGs, written inside KeSynchronizeExecution or the routine,
 * or by an interlocked operation, and read anywhere. The routine writes Burst
 * alone. No spin lock of the driver's is taken at DIRQL, and WorkLock is
 * never taken inside a synchronize routine (record 21 section 4.5.3).
 *
 * Every system connects the line its start was given, 98 SE and ME
 * through NTKERN's IoConnectInterrupt as on NT (section 4.5.4; task
 * 48.26, after their route was read in task 45.22). XhciCmRingInterrupt 2
 * is a message on 2000, XP and XP x64 alone (section 4.5.14, task 49.27;
 * cm_rirqnt.c); on 98 SE and ME it is not tried (CmRirqHow).
 */

#ifndef CM_RIRQOS_H
#define CM_RIRQOS_H

#include "cm_rirq.h"

typedef struct _CM_RIRQ_DRV {
    /* The start's line resource (the translated list's first interrupt
     * descriptor). A message grant is recorded and never connected: the
     * line step only (owner, Q3). */
    ULONG HaveLine;
    ULONG HaveMessage;
    ULONG Vector;
    KIRQL Irql;
    KAFFINITY Affinity;
    KINTERRUPT_MODE Mode;
    BOOLEAN Shared;

    /* This activation (reset at each start). */
    ULONG Value;            /* XhciCmRingInterrupt as run with             */
    ULONG Failed;           /* IrqFailed: polling until the next start     */
    ULONG Why;              /* CM_Q_IRQ_WHY_*: why polled (section 4.5.17) */
    volatile LONG HwGate;   /* CM_RIRQ_GATE_*: closing reasons, written
                               under WorkLock (section 4.5.18)            */
    ULONG SelfTestOwed;     /* the activation's self-test not yet passed   */
    ULONG IrqMode;          /* CM_RIRQ_MODE_*                              */
    ULONG Intel;            /* the mask register written directly          */
    ULONG AutoClearOff;     /* Disable ISR Auto-Clear found set            */
    ULONG CommandRead;      /* Command bit 10 read at this activation      */
    ULONG IntxOurs;         /* this driver set Command bit 10              */
    ULONG Retained;         /* connected, unretired, acknowledge-only      */
    ULONG MaintLast;        /* the last maintenance pass, NowMs            */
    ULONG MaintOwed;        /* the maintenance due at once (an Up)         */
    ULONG RetryDue;         /* a retained one's next retirement attempt    */
    CM_RIRQ_GEOM Geom;
    CM_RIRQ_WATCH Watch;
    PKINTERRUPT Object;
    KDPC Dpc;

    /* Shared with the service routine and the DPC. The last four change by
     * interlocked operations alone (the Windows 2000 DDK's take a plain
     * PLONG); every wait that reads them sleeps between reads, through a
     * call. */
    volatile LONG Connected;
    volatile LONG Armed;
    volatile LONG StormMasked;
    LONG DpcSeq;
    LONG DpcsOwed;
    LONG Claims;
    LONG Claims0;           /* of them with ring 0's cause set             */
    LONG ClaimsData;        /* of them with the data ring's cause set      */
    LONG Ones;
    CM_RIRQ_BURST Burst;    /* the service routine's alone                 */
    /* The qemu flavour's dead line (XhciCmQemuIrqDead): after DeadAfter
     * claims the routine acknowledges, counts in Dropped alone and queues
     * nothing. Unused in the published flavours. */
    ULONG QemuDeadAfter;
    volatile LONG QemuDead;
    LONG QemuDropped;

    /* The networking data ring's cause (record 35 section 5.5, task
     * 48.38), the thread's but DataOpen, which the DPC reads: DataWant the
     * ring whose cause the network service asks for (0 none), opened at
     * once where armed and at every later arm; DataOpen its cause open and
     * proven. The data bit stays in Geom while its mask is not proven, so
     * the routine still claims and acknowledges it. */
    ULONG DataWant;
    volatile LONG DataOpen;
    ULONG DataOpens;
    ULONG DataUnproven;
    ULONG DataStorms;

    /* Counters (cm.irq.*), the thread's. */
    ULONG Connects;
    ULONG ConnectFailures;
    ULONG ArmFailures;
    ULONG Wakes;
    ULONG SelfTests;
    ULONG SelfTestFailures;
    ULONG Suspects;
    ULONG Lost;
    ULONG Storms;
    ULONG Downs;
    ULONG Unretired;
    ULONG Disconnects;
    ULONG DpcSlow;

    /*
     * The message (record 21 section 4.5.14, task 49.27), the thread's but
     * Reach: How this activation's CM_RIRQ_HOW_*; Message 1 while the
     * connection is msi4nt's; MsgInfo the library's block; ReArmOwed a
     * connection kept across a sleep; MsgBdf the location connected.
     * Reach, written only inside the connection's exclusion or with
     * nothing connected, is "the function in D0, BAR0 mapped, the cause
     * armed or being armed": the routine touches nothing while it is
     * clear.
     */
    ULONG How;
    ULONG Sys;              /* CM_RIRQ_SYS_*, taken at each start          */
    ULONG Yield;            /* NT 5: a machine-wide XhciInterruptMode 4    */
    ULONG Message;
    PVOID MsgInfo;
    ULONG ReArmOwed;
    ULONG MsgBdf;
    ULONG MsgVector;
    volatile LONG Reach;
    ULONG MsgConnects;
    ULONG MsgConnectFailures;
    ULONG MsgRearms;
    ULONG MsgRearmFailures;
    ULONG MsgForced;
    ULONG MsgHandoffFailures;
    ULONG MsgStatus;
    /* "When the library says no": the masks not read back 0 (the first DW
     * read, low bit set), read again once a pass; 0 when proven. */
    ULONG MsgMaskWatch;
    /* NT (section 4.5.14): msi4nt's claim held, its IRQ; the stop's
     * removal (a surprise removal or a removal Windows reported);
     * Interrupt Disable read set after the connect (the absence proof's
     * condition); ProofRead the one call during which the configuration
     * route admits reads at offset 0 after a revocation. */
    ULONG MsgClaimHeld;
    ULONG MsgIrq;
    ULONG Removal;
    ULONG MsgIdSet;
    volatile LONG ProofRead;
    ULONG MsgBraked;        /* connections the library braked             */
} CM_RIRQ_DRV;

struct _CM_NHI;

/* msi4nt's statuses this driver names (msi4nt.h's MSGIRQ_STATUS_*;
 * cm_rirqnt.c holds them equal). */
#define CM_MB_OK                 0UL
#define CM_MB_IN_USE             6UL
#define CM_MB_INVALID            9UL
#define CM_MB_CONFIG_ACCESS      10UL
#define CM_MN_CLAIM_KEPT         12UL   /* msi4nt's MSGIRQ_STATUS_CLAIM_KEPT */
#define CM_MB_BRAKED             13UL
#define CM_MN_NO_VECTORS         4UL
/* The networking report's interrupt kind beyond IrqMode's (task 49.27):
 * the line, with value 2 yielding to a machine-wide mode 4. */
#define CM_RIRQ_KIND_LINE_YIELD  3UL

/* The service routine's body (section 4.5.3), shared by the line's routine
 * and the message's (cm_rirqnt.c). IRQL: DIRQL. */
BOOLEAN CmRirqIsrBody(struct _CM_NHI *nhi);

/* cm_rirqnt.c, both images: the image-wide gate and the refusal table
 * (section 4.5.14). PASSIVE_LEVEL on the threads. */
VOID CmRirqGateEnter(struct _CM_NHI *nhi);
VOID CmRirqGateLeave(VOID);
BOOLEAN CmRirqRefused(struct _CM_NHI *nhi, ULONG bdf);
BOOLEAN CmRirqRefuse(struct _CM_NHI *nhi, ULONG bdf);

/*
 * cm_rirqnt.c, the msi4nt seam (section 4.5.14; 2000, XP, Server 2003 and
 * XP x64; the one file compiling ..\msi4nt.h's implementation, hosted):
 * the library once per image, the ISA claim with the library's defaults
 * (its candidates reversed) and its release, the connect, the re-arm, the
 * disconnect (`how` 0 plain, 1 with the absence proof, 2 the quarantine),
 * the vector. MSGIRQ_STATUS_* values. PASSIVE_LEVEL, the thread.
 */
ULONG CmRirqNtInit(struct _CM_NHI *nhi);
ULONG CmRirqNtClaim(struct _CM_NHI *nhi, PULONG irq);
ULONG CmRirqNtRelease(struct _CM_NHI *nhi);
ULONG CmRirqNtConnect(struct _CM_NHI *nhi);
ULONG CmRirqNtReArm(struct _CM_NHI *nhi);
ULONG CmRirqNtDisconnect(struct _CM_NHI *nhi, ULONG how);
ULONG CmRirqNtVector(struct _CM_NHI *nhi, PULONG data);
/* cm_thr.c: `length` bytes of configuration space at `offset` through the
 * NHI's own route, the liveness gate's rules (with the absence proof's
 * one admission, ProofRead): the bytes moved, 0 when refused. */
ULONG CmCfgBytes(struct _CM_NHI *nhi, BOOLEAN read, PVOID buffer,
                 ULONG offset, ULONG length);

/* The NHI's AddDevice: the DPC. PASSIVE_LEVEL. */
VOID CmRirqInit(struct _CM_NHI *nhi);
/* The PnP start, before the thread: the value taken, the activation's
 * state reset, the line resource read from the translated partial list
 * (NULL: none). Never with an interrupt connected (every stop disconnects
 * it). PASSIVE_LEVEL. */
VOID CmRirqStart(struct _CM_NHI *nhi, PCM_PARTIAL_RESOURCE_LIST translated);

/* The rest on the connection-manager thread, PASSIVE_LEVEL, no lock held. */

/* Before a ring start (an activation's bind, a resume, a recovery): an
 * armed interrupt taken down (the start masks every cause), then RX
 * descriptors posted with Interrupt Enable where the interrupt will be
 * tried (section 4.5.2, "the intent"), without it otherwise. */
VOID CmRirqIntent(struct _CM_NHI *nhi);
/* Whether the thread's waits run to deadlines (armed) rather than for a
 * fresh period; CmRirqLeft the wait to the maintenance pass, the last one
 * plus `periodMs` (the period in force now); CmRirqMaintenance whether
 * this pass runs it. */
BOOLEAN CmRirqTimed(struct _CM_NHI *nhi);
ULONG CmRirqLeft(struct _CM_NHI *nhi, ULONG periodMs);
BOOLEAN CmRirqMaintenance(struct _CM_NHI *nhi, ULONG periodMs);
/* Around the maintenance pass's poll: the watchdog (which may test again
 * or fall back), then the drain's claims and the next deadline. */
VOID CmRirqBeforeMaint(struct _CM_NHI *nhi);
VOID CmRirqAfterMaint(struct _CM_NHI *nhi, ULONG periodMs);
/* CM_WORK_RING: RX ring 0 drained, nothing else (section 4.5.1). */
VOID CmRirqWake(struct _CM_NHI *nhi);
/* Once a pass, after the work and the poll: up where the client polls and
 * the interrupt is wanted; down where it no longer polls; a storm the
 * routine masked taken down for good; a retained one's retirement tried
 * again once a period. */
VOID CmRirqService(struct _CM_NHI *nhi, BOOLEAN polling, ULONG periodMs);
/* The order down of section 4.5.6 for `why` (CM_RIRQ_DOWN_*): before the
 * ring stop of a D3 or a stop. Nothing when nothing is connected. */
VOID CmRirqDown(struct _CM_NHI *nhi, ULONG why);

/* CM_Q_RM_IRQ's word for the query answer (record 21 section 4.5.17).
 * The thread. */
ULONG CmRirqQueryWord(struct _CM_NHI *nhi);

/* The routine's hardware gate (record 21 section 4.5.18).
 * CmRirqHwGateCloseLocked: a closing reason added, the caller holding
 * WorkLock (CmNhiRevoke). CmRirqHwGateClose: the same, taking WorkLock,
 * no fence (any IRQL <= DISPATCH_LEVEL; the detached shutdown).
 * CmRirqHwGateOpen: POWER taken away where CmRirqGateMayOpen admits it,
 * the test and the clear in one WorkLock hold; the thread alone. */
VOID CmRirqHwGateCloseLocked(struct _CM_NHI *nhi, ULONG why);
VOID CmRirqHwGateClose(struct _CM_NHI *nhi, ULONG why);
VOID CmRirqHwGateOpen(struct _CM_NHI *nhi);
/* The counters as cm.irq.* lines, at the stop. */
VOID CmRirqLogCounters(struct _CM_NHI *nhi);

/*
 * The networking data ring's cause (record 35 section 5.5), on the thread:
 * CmRirqDataOpen asks for ring `ring`'s Receive Data Buffer cause, opened
 * now where the interrupt is armed (ITR[0] CM_RIRQ_ITR_DATA, the mask read
 * back) and at every later arm while asked for: CM_RIRQ_DATA_*.
 * CmRirqDataClose withdraws the ask and masks the cause, ITR[0] back to 0:
 * TRUE when it is proven masked or was never open (the host interface
 * lost: nothing written, TRUE). CmRirqDataState for the query: 0 polled, 1
 * open, 2 asked for and latched to polling (the activation fell back).
 */
#define CM_RIRQ_DATA_POLL        0UL    /* not armed: the poll alone       */
#define CM_RIRQ_DATA_OPEN        1UL
#define CM_RIRQ_DATA_UNPROVEN    2UL    /* the mask not read back: masked  */
ULONG CmRirqDataOpen(struct _CM_NHI *nhi, ULONG ring, BOOLEAN lost);
BOOLEAN CmRirqDataClose(struct _CM_NHI *nhi, BOOLEAN lost);
ULONG CmRirqDataState(struct _CM_NHI *nhi);

/*
 * cm_thr.c: the one configuration access the retirement adds (record 21
 * section 4.5.6.1), past the liveness gate's recoverable states: the
 * identity DW read and matched against the healthy start's, then the
 * Command word read, written with bit 10 (Interrupt Disable) set and
 * nothing else changed, and read back. Refused once revoked. TRUE when the
 * bit reads set afterwards; *ours set to 1 when this call set it.
 * PASSIVE_LEVEL, the thread.
 */
BOOLEAN CmCfgIntxRetire(struct _CM_NHI *nhi, PULONG ours);
/* The same class for Bus Master Enable, cleared before an NT quarantine
 * (record 21 section 4.5.14 item 4): TRUE when it reads clear. */
BOOLEAN CmCfgBusMasterRetire(struct _CM_NHI *nhi);

#endif /* CM_RIRQOS_H */
