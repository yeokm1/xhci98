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
 * Only NT connects (IoIsWdmVersionAvailable(1, 0x10)): 98 SE and ME poll
 * until their route is read (section 4.5.4), whatever the value says.
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
    ULONG Noted9x;          /* cm.irq.9x logged                            */
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
    LONG Ones;
    CM_RIRQ_BURST Burst;    /* the service routine's alone                 */
    /* The qemu flavour's dead line (XhciCmQemuIrqDead): after DeadAfter
     * claims the routine acknowledges, counts in Dropped alone and queues
     * nothing. Unused in the published flavours. */
    ULONG QemuDeadAfter;
    volatile LONG QemuDead;
    LONG QemuDropped;

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
} CM_RIRQ_DRV;

struct _CM_NHI;

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
/* The counters as cm.irq.* lines, at the stop. */
VOID CmRirqLogCounters(struct _CM_NHI *nhi);

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

#endif /* CM_RIRQOS_H */
