/*
 * xhci_tol.h - controller tolerance, its pure half (roadmap-hcd.md tasks
 * 35-T.1 to 35-T.9, design record 17).
 *
 * The decisions each tolerant behaviour takes, the three registry values
 * read at a start, the tolerance clock's arithmetic, and the budgets and
 * intervals of record 17 section 4.9 as named constants (owner, 2026-10-06:
 * constants, not registry values; retuned in a build against a reading).
 *
 * The tolerance clock (record 17 section 4.0) is a count of 100 ms ticks
 * advanced by a relative kernel timer. A relative timer can expire up to one
 * clock period early, cumulatively, and the period cannot be read on Windows
 * 98's import evidence, so each tick is credited at 45 ms, under the 8254's
 * power-on period: an interval of T ms is ceil(T / 45) + 1 ticks, the extra
 * tick because a stamp may be taken just before one. An interval counted so
 * can run long, never short.
 *
 * XhciTolerance (record 17 section 4.11) is the input of every decision of
 * sections 4.1 to 4.6: at 0 each answers as the 2.1.1.0 driver did, so the
 * gate sits at the producers. The counters are not gated.
 *
 * DDK-free: part of the pure core. C89. IRQL: any; nothing here takes a lock
 * or touches a register. The executors are hcd_ctl.c, hcd_enum.c and the
 * transfer engine.
 */

#ifndef XHCI_TOL_H
#define XHCI_TOL_H

#include "xhci_compat.h"

/* The clock. */
#define XHCI_TOL_TICK_MS            100UL   /* the timer's period            */
#define XHCI_TOL_TICK_CREDIT_MS     45UL    /* what one tick is credited     */

/* An interval of ms milliseconds as a tick count; a constant expression. */
#define XHCI_TOL_TICKS(ms) \
    ((((ms) + XHCI_TOL_TICK_CREDIT_MS - 1UL) / XHCI_TOL_TICK_CREDIT_MS) + 1UL)

/* The intervals of record 17 section 4.9, in milliseconds and in ticks. */
#define XHCI_TOL_BACKSTOP_MS        100UL
#define XHCI_TOL_STABLE_PROGRESS_MS 60000UL
#define XHCI_TOL_STABLE_DISC_MS     1000UL
#define XHCI_TOL_OC_SETTLE_MS       100UL
#define XHCI_TOL_OC_WAIT_MS         5000UL
#define XHCI_TOL_POWER_ON_MS        100UL
#define XHCI_TOL_CONTAIN_MS         1000UL
#define XHCI_TOL_RECOVERY_WINDOW_MS 600000UL

#define XHCI_TOL_BACKSTOP_TICKS     XHCI_TOL_TICKS(XHCI_TOL_BACKSTOP_MS)
#define XHCI_TOL_STABLE_PROGRESS_TICKS XHCI_TOL_TICKS(XHCI_TOL_STABLE_PROGRESS_MS)
#define XHCI_TOL_STABLE_DISC_TICKS  XHCI_TOL_TICKS(XHCI_TOL_STABLE_DISC_MS)
#define XHCI_TOL_OC_SETTLE_TICKS    XHCI_TOL_TICKS(XHCI_TOL_OC_SETTLE_MS)
#define XHCI_TOL_OC_WAIT_TICKS      XHCI_TOL_TICKS(XHCI_TOL_OC_WAIT_MS)
#define XHCI_TOL_POWER_ON_TICKS     XHCI_TOL_TICKS(XHCI_TOL_POWER_ON_MS)
#define XHCI_TOL_CONTAIN_TICKS      XHCI_TOL_TICKS(XHCI_TOL_CONTAIN_MS)
#define XHCI_TOL_RECOVERY_WINDOW_TICKS XHCI_TOL_TICKS(XHCI_TOL_RECOVERY_WINDOW_MS)

/* The budgets of record 17 section 4.9. */
#define XHCI_TOL_SOFT_RETRIES       3UL     /* per TD                        */
#define XHCI_TOL_REENUMS            3UL     /* per location                  */
#define XHCI_TOL_REPOWERS           3UL     /* per root port                 */
#define XHCI_TOL_RECOVERIES         3UL     /* begun per recovery window     */

/* Root ports with a location budget in the extension; XHCI_MAX_ROOT_PORTS
 * (xhci.h), restated so this header stays free of xhci.h. */
#define XHCI_TOL_ROOT_PORTS         255

/* Interrupt Interval cap (Linux's XHCI_LIMIT_ENDPOINT_INTERVAL_9). */
#define XHCI_TOL_INTERVAL_CAP       8UL     /* 2^8 * 125 us = 32 ms          */
#define XHCI_TOL_AVG_TRB_DEFAULT    1024UL

/* XhciIntervalCap. */
#define XHCI_TOL_CAP_OFF            0UL
#define XHCI_TOL_CAP_GATED          1UL     /* vendor 1022                   */
#define XHCI_TOL_CAP_ALL            2UL

/* Why a location is held (XHCI_TOL_LOC.Hold); 0 when not held. */
#define XHCI_TOL_HOLD_NONE          0UL
#define XHCI_TOL_HOLD_REENUMS       1UL     /* powered                       */
#define XHCI_TOL_HOLD_REPOWERS      2UL     /* unpowered                     */
#define XHCI_TOL_HOLD_OC_WAIT       3UL     /* unpowered, OCA never cleared  */

/* What a location's charge was for (XhciTolLocCharge). */
#define XHCI_TOL_CHARGE_REENUM      0UL
#define XHCI_TOL_CHARGE_REPOWER     1UL

/* Why a device is cycled (record 17 section 4.3), for the counters. */
#define XHCI_TOL_CYCLE_NONE         0UL
#define XHCI_TOL_CYCLE_REFUSED_CODE 1UL     /* T3                            */
#define XHCI_TOL_CYCLE_HALT_NO_TD   2UL     /* T4                            */
#define XHCI_TOL_CYCLE_PED          3UL     /* 4.5's reconnect               */

/* The containment's branch (record 17 section 4.6). */
#define XHCI_TOL_CONTAIN_NONE       0UL
#define XHCI_TOL_CONTAIN_RELEASED   1UL     /* Bus Master Enable read clear  */
#define XHCI_TOL_CONTAIN_PINNED     2UL     /* no proof                      */

/* Registry values, read at each start: found is 1 when the value was found
 * as a REG_DWORD. Absent, another type or another number takes the
 * default. */
ULONG XhciTolMode(ULONG found, ULONG value);         /* XhciTolerance: 0/1   */
ULONG XhciTolCapMode(ULONG found, ULONG value);      /* XhciIntervalCap      */
ULONG XhciTolAvgTrbMode(ULONG found, ULONG value);   /* XhciAvgTrbEsit: 0/1  */

/* The clock: 1 once at least ticks have passed from stamp to now. */
ULONG XhciTolElapsed(ULONG now, ULONG stamp, ULONG ticks);

/* 35-T.7. 1 when the cap applies on this controller (PCI offset 0's
 * vendor/device dword). */
ULONG XhciTolCapApplies(ULONG capMode, ULONG vendorDevice);

/* The Interval programmed: interval capped at 8 for an interrupt endpoint
 * when the cap applies; any other endpoint unchanged. The executor offers
 * fast polling only when this left the Interval as it was, so the cap never
 * makes an endpoint eligible (record 17 section 4.7). */
ULONG XhciTolCapInterval(ULONG applies, ULONG isInterrupt, ULONG interval);

/* The Average TRB Length of a periodic endpoint: 1024, or with the switch
 * on and an interrupt endpoint its Max ESIT Payload held to 1..65535. */
ULONG XhciTolAvgTrbLength(ULONG avgMode, ULONG isInterrupt,
                          ULONG maxEsitPayload);

/* 35-T.2. 1 when the controller is on Linux's XHCI_NO_SOFT_RETRY list. */
ULONG XhciTolNoSoftRetry(ULONG vendorDevice);

/* 1 when a pipe is in the soft retry's scope: tolerance on, bulk or
 * interrupt (bulkOrInt), no streams, not behind a TT, the controller not
 * listed. */
ULONG XhciTolRetryScope(ULONG tolerance, ULONG vendorDevice, ULONG bulkOrInt,
                        ULONG streams, ULONG behindTt);

/* 1 when a matched event is diverted: a Transaction Error on the queue's
 * head, the TD's retries not spent, the pipe in scope. */
ULONG XhciTolRetryDivert(ULONG inScope, ULONG code, ULONG isHead,
                         ULONG retriesUsed);

/* 35-T.3. 1 when a Transfer Event's code cycles the device: tolerance on,
 * the code not claimed by XhciXferCodeInfo (claimed is 1 when it returned
 * XHCI_XFER_OK; the caller asks it, so the two never drift), the endpoint
 * not isochronous, and the slot naming a device. */
ULONG XhciTolCycleOnRefused(ULONG tolerance, ULONG claimed, ULONG isoch,
                            ULONG hasDevice);

/* 1 when an unmatched event is a halt candidate: Stall, Transaction,
 * Babble or Split Transaction on a non-isochronous open pipe, tolerance
 * on. The thread then reads the context. */
ULONG XhciTolHaltCandidate(ULONG tolerance, ULONG code, ULONG isoch,
                           ULONG pipeOpen);

/* 1 when a halt candidate's context state (XHCI_EP_STATE_*) cycles. */
ULONG XhciTolHaltConfirmed(ULONG epState);

/* The backstop's one observation (record 17 section 4.1). */
typedef struct _XHCI_TOL_OBS {
    ULONG Valid;
    ULONG Index;        /* event ring dequeue index                         */
    ULONG Cycle;        /* consumer cycle state                             */
    ULONG DrainGen;     /* drain passes                                     */
    ULONG StartGen;     /* starts and in-place recoveries                   */
    ULONG Stamp;        /* tolerance clock when first seen                  */
} XHCI_TOL_OBS, *PXHCI_TOL_OBS;

/* One pass's peek. pending is 1 when the TRB at the dequeue carries the
 * consumer's cycle. Returns 1 when the drain is to be queued; the
 * observation is then restamped, so one drain per interval it stands. */
ULONG XhciTolBackstop(PXHCI_TOL_OBS obs, ULONG tolerance, ULONG pending,
                      ULONG index, ULONG cycle, ULONG drainGen,
                      ULONG startGen, ULONG now);

/* One location's budget (record 17 sections 4.5 and 4.9). */
typedef struct _XHCI_TOL_LOC {
    ULONG Reenums;      /* charged re-enumerations                          */
    ULONG Repowers;     /* charged repowers                                 */
    ULONG Charges;      /* every charge, never re-armed (the dump)          */
    ULONG Hold;         /* XHCI_TOL_HOLD_*                                  */
    ULONG LastCharge;   /* tolerance clock at the last charge               */
    ULONG Charged;      /* 1 once any charge was made                       */
    ULONG DiscArmed;    /* a stable disconnect is being timed               */
    ULONG DiscStamp;
    ULONG DiscSeen;     /* stable-disconnect evidence stands                */
    ULONG Holds;        /* times held, never re-armed (the dump)            */
} XHCI_TOL_LOC, *PXHCI_TOL_LOC;

/* A controller start: every field cleared. */
VOID XhciTolLocInit(PXHCI_TOL_LOC loc);

/* Charge one action of kind (XHCI_TOL_CHARGE_*) at now. Returns 1 when the
 * budget allowed it; otherwise the location is held and 0 returned. A held
 * location is never charged. */
ULONG XhciTolLocCharge(PXHCI_TOL_LOC loc, ULONG kind, ULONG now);

/* A pass's view of the port: connected (CCS, or the hub's port status),
 * powered (PP, 1 at a hub's port), fault (an over-current or a recovery
 * the driver itself caused is active). Times the stable disconnect; a
 * connection after one re-arms the budget and releases a powered hold.
 * Returns 1 when it re-armed. */
ULONG XhciTolLocObserve(PXHCI_TOL_LOC loc, ULONG connected, ULONG powered,
                        ULONG fault, ULONG now);

/* A device at the location completing transfers: re-arms when the
 * stable-progress interval has passed with no charge. Returns 1 when it
 * re-armed. */
ULONG XhciTolLocProgress(PXHCI_TOL_LOC loc, ULONG now);

/* 35-T.5. 1 when a root port's change is the PED fault: tolerance on,
 * USB 2.0, PEC set, PED clear, CCS set, a device held. */
ULONG XhciTolPedFault(ULONG tolerance, ULONG usb2, ULONG pec, ULONG ped,
                      ULONG ccs, ULONG hasDevice);

/* 1 when a root port is in over-current: tolerance on and OCC set, or PP
 * clear while the driver's own state says it powered the port. */
ULONG XhciTolOcFault(ULONG tolerance, ULONG occ, ULONG pp, ULONG powered);

/* 35-T.6. 1 when HCH requests the in-place recovery: tolerance on, HCH
 * set, and the driver's state says the controller runs (D0, started, not
 * stopping, not recovering, no power transition, Run/Stop last written 1;
 * the caller folds those into runs). */
ULONG XhciTolHchRecover(ULONG tolerance, ULONG hch, ULONG runs);

/* The recovery window: three recoveries begun in ten minutes. */
typedef struct _XHCI_TOL_WINDOW {
    ULONG Count;                            /* stamps held, 0..3            */
    ULONG Stamp[XHCI_TOL_RECOVERIES];       /* oldest first                 */
    ULONG Refused;                          /* recoveries not begun         */
} XHCI_TOL_WINDOW, *PXHCI_TOL_WINDOW;

VOID XhciTolWindowInit(PXHCI_TOL_WINDOW win);

/* A recovery about to begin at now. Returns 1 and records it when fewer
 * than three began inside the window; 0 (counted) otherwise. At tolerance
 * 0 it always returns 1 and records nothing. */
ULONG XhciTolWindowAdmit(PXHCI_TOL_WINDOW win, ULONG tolerance, ULONG now);

/* The all-ones episode (record 17 section 4.6). */
typedef struct _XHCI_TOL_DEAD {
    ULONG Armed;
    ULONG StartGen;
    ULONG Stamp;
} XHCI_TOL_DEAD, *PXHCI_TOL_DEAD;

/* One admitted or refused pass of the containment step. admitted is 0 for
 * a failed admission or a power transition; allOnes is USBSTS's read.
 * Returns 1 when the controller is to be contained. */
ULONG XhciTolDeadStep(PXHCI_TOL_DEAD dead, ULONG tolerance, ULONG admitted,
                      ULONG allOnes, ULONG startGen, ULONG now);

/*
 * The counters a user can send (35-T.8, record 17 section 4.8), in the
 * extension the snapshot carries. Never gated by XhciTolerance: a dump
 * taken at 0 still shows what the behaviours would have answered. The
 * per-queue counts are kept on each queue as before and summed here as they
 * are counted; the note ring carries the first records of each code with
 * their slot and endpoint (XhciLogErrorBudget). An even number of ULONGs,
 * for XHCI_EXTENSION's TrailingPad.
 */
#define XHCI_TOL_CYCLE_REASONS      4UL

typedef struct _XHCI_TOL_STATS {
    ULONG Tolerance;        /* XhciTolerance in effect                      */
    ULONG CapMode;          /* XhciIntervalCap in effect                    */
    ULONG AvgTrbMode;       /* XhciAvgTrbEsit in effect                     */
    ULONG CapApplied;       /* the cap applies on this controller           */
    ULONG CapIntervals;     /* endpoints whose Interval the cap lowered     */
    ULONG BackstopDrains;   /* 4.1                                          */
    ULONG QueueErrors;      /* the queues' Errors, summed                   */
    ULONG QueueBadCodes;    /* BadCodes                                     */
    ULONG QueueUnmatched;   /* UnmatchedEvents                              */
    ULONG QueueForeign;     /* ForeignEvents                                */
    ULONG QueueHalts;       /* halting completions on a matched TD          */
    ULONG RetryDiverts;     /* 4.2: Transaction Errors intercepted          */
    ULONG RetryResets;      /* Reset Endpoint (TSP 1) issued                */
    ULONG RetryRecovered;   /* a diverted TD later completed with success   */
    ULONG RetryExhausted;   /* a fourth error, today's path                 */
    ULONG RetryReplayed;    /* deferred outcome applied by the thread       */
    ULONG RetryResetFailed; /* Reset Endpoint refused: hcdCfgFault          */
    ULONG Cycles[XHCI_TOL_CYCLE_REASONS];   /* 4.3/4.5, by XHCI_TOL_CYCLE_*  */
    ULONG CyclesPrePdo;     /* ...of which before the PDO                   */
    ULONG CyclesDropped;    /* the mark was stale: device gone or replaced  */
    ULONG CyclesRefused;    /* the location's budget refused                */
    ULONG HaltReads;        /* 4.3: context reads for a halt with no TD     */
    ULONG HaltStale;        /* ...that read neither Halted nor Error        */
    ULONG PedFaults;        /* 4.5                                          */
    ULONG OcFaults;
    ULONG Repowers;
    ULONG Holds;
    ULONG HchRecoveries;    /* 4.6                                          */
    ULONG WindowRefused;
    ULONG DeadEpisodes;     /* all-ones stamps                              */
    ULONG Contained;        /* XHCI_TOL_CONTAIN_*, 0 when not               */
    ULONG Codes[256];       /* Transfer Event completion codes, by count    */
} XHCI_TOL_STATS, *PXHCI_TOL_STATS;

/* The tolerance state a start initializes (record 17 section 4.11): in the
 * extension, so the dump shows each root port's budget and hold, and set
 * explicitly by XhciTolStart rather than left to the start's zeroing. An
 * external hub's ports keep their XHCI_TOL_LOC on the hub. */
typedef struct _XHCI_TOL_STATE {
    XHCI_TOL_STATS Stats;
    XHCI_TOL_OBS Obs;
    XHCI_TOL_WINDOW Window;
    XHCI_TOL_DEAD Dead;
    ULONG Unreadable;       /* 4.6: submissions park                        */
    ULONG Clock;            /* the tolerance clock, in ticks                */
    XHCI_TOL_LOC RootLoc[XHCI_TOL_ROOT_PORTS];
} XHCI_TOL_STATE, *PXHCI_TOL_STATE;

/* A start, before admission reopens: every field set, the three values
 * latched from their registry reads (found and value each). */
VOID XhciTolStart(PXHCI_TOL_STATE st, ULONG vendorDevice,
                  ULONG tolFound, ULONG tolValue,
                  ULONG capFound, ULONG capValue,
                  ULONG avgFound, ULONG avgValue);

/* Count one Transfer Event completion code. */
VOID XhciTolCountCode(PXHCI_TOL_STATS stats, ULONG code);

#endif /* XHCI_TOL_H */
