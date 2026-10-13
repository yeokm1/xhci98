/*
 * xhci_poll.h - polled event delivery, its pure half (roadmap-hcd.md tasks
 * 36.6 to 36.8; design record 15).
 *
 * How the bus learns that the controller has written an event: the line
 * interrupt, a message interrupt (MSI or MSI-X, Vista and 7, task 36.15),
 * or a poll of the event ring on a timer whose period adapts to the work
 * outstanding. This file holds every decision of it that needs no kernel:
 *
 *   - the three values (record 15 section 3) and their out-of-range rules;
 *   - the delivery and its reason, settled at start (section 4);
 *   - the watcher's dead-line verdict (section 4.2);
 *   - the poller's ownership machine and its roles - open, take, park,
 *     end, kick, promotion and close (sections 5.2, 5.3 and 6.2) - as
 *     transitions the caller makes under PollLock, with KeCancelTimer and
 *     KeSetTimer left to the caller and their answers handed back;
 *   - the adaptive period (section 6.1), the conversion of a deadline into
 *     an arm (section 6.2), and the interval observation (section 7);
 *   - the finer system timer's request machine and its phase observer
 *     (section 7.2, task 38.3).
 *
 * The executor is hcd_poll.c. Times are the low word of KeQuerySystemTime,
 * in 100 ns units, compared by signed difference: no 64-bit arithmetic, and
 * every deadline the poller makes is within twice the idle bound, far
 * inside the word's lap of about 429 s.
 *
 * DDK-free: part of the pure core. C89. IRQL: any.
 */

#ifndef XHCI_POLL_H
#define XHCI_POLL_H

#include "xhci_compat.h"

/* The three values (record 15 section 3). Modes 1 and 3 name one
 * delivery each and fail the start without it (revision 7, owner
 * 2026-10-11); 4 and above run as 0. */
#define XHCI_POLL_MODE_AUTO             0UL
#define XHCI_POLL_MODE_LINE             1UL     /* the line only           */
#define XHCI_POLL_MODE_POLL             2UL
#define XHCI_POLL_MODE_MESSAGE          3UL     /* a message only          */
#define XHCI_POLL_IDLE_DEFAULT_MS       50UL
#define XHCI_POLL_IDLE_MAX_MS           5000UL
#define XHCI_POLL_ACTIVE_DEFAULT_MS     1UL
#define XHCI_POLL_NOT_FOUND             0xFFFFFFFFUL

/* The watcher (section 4.2): its own period, the stale pairs in a row that
 * prove a line dead, and the least system time they must span. */
#define XHCI_POLL_WATCH_MS              100UL
#define XHCI_POLL_WATCH_PAIRS           3UL
#define XHCI_POLL_WATCH_DEAD_MS         300UL

/* 100 ns units per ms. */
#define XHCI_POLL_TICKS_PER_MS          10000UL

/* The delivery in effect (section 8's codes). */
#define XHCI_POLL_DELIVERY_NONE         0UL     /* the start failed        */
#define XHCI_POLL_DELIVERY_LINE         1UL
#define XHCI_POLL_DELIVERY_POLL         2UL
#define XHCI_POLL_DELIVERY_MSI          3UL
#define XHCI_POLL_DELIVERY_MSIX         4UL
#define XHCI_POLL_DELIVERY_MESSAGE      5UL     /* capability not read     */

/* Its reason. */
#define XHCI_POLL_R_NONE                        0UL
#define XHCI_POLL_R_AUTO                        1UL
#define XHCI_POLL_R_AUTO_PIN_UNREADABLE         2UL
#define XHCI_POLL_R_AUTO_PIN0                   3UL
#define XHCI_POLL_R_AUTO_NO_INTERRUPT           4UL
#define XHCI_POLL_R_FORCED_LINE                 5UL
#define XHCI_POLL_R_FORCED_LINE_PIN_UNREADABLE  6UL
#define XHCI_POLL_R_FORCED_NO_INTERRUPT         7UL
#define XHCI_POLL_R_FORCED_POLL                 8UL
#define XHCI_POLL_R_AUTO_LINE_DEAD              9UL
#define XHCI_POLL_R_AUTO_MESSAGE                10UL
#define XHCI_POLL_R_MESSAGE_LINE_ASKED          11UL
#define XHCI_POLL_R_MESSAGE_NOT_CONNECTED       12UL
/* Mode 1 on Vista or 7 x86 with a message granted, no helper bound and the
 * filter's list offering no line: the start failed (design record 18
 * section 4.3). 11 and 13 are revision 6's and no longer produced (record
 * 15 revision 7); kept so a reader names an earlier build's. */
#define XHCI_POLL_R_FORCED_NO_LINE_OFFERED      13UL
/* Revision 7 (owner, 2026-10-11): mode 1 the line only, mode 3 a message
 * only. Each but 15 fails the start. */
#define XHCI_POLL_R_FORCED_LINE_MESSAGE         14UL    /* 1, a message    */
#define XHCI_POLL_R_FORCED_MESSAGE              15UL    /* 3, connected    */
#define XHCI_POLL_R_FORCED_MESSAGE_NOT_GRANTED  16UL    /* 3, none granted */
#define XHCI_POLL_R_FORCED_MESSAGE_NOT_CONNECTED 17UL   /* 3, not connected */
#define XHCI_POLL_R_FORCED_MESSAGE_NO_SYSTEM    18UL    /* 3, before NT 6  */

/* The pin's read. */
#define XHCI_POLL_PIN_NOT_READ          0UL
#define XHCI_POLL_PIN_READ              1UL
#define XHCI_POLL_PIN_UNREADABLE        2UL

/* The owner states (section 5.2). */
#define XHCI_POLL_S_CLOSED              0UL
#define XHCI_POLL_S_PARKED              1UL
#define XHCI_POLL_S_ARMED               2UL
#define XHCI_POLL_S_FIRING              3UL
#define XHCI_POLL_S_CLOSING             4UL

/* The roles. */
#define XHCI_POLL_ROLE_NONE             0UL
#define XHCI_POLL_ROLE_WATCH            1UL
#define XHCI_POLL_ROLE_SERVE            2UL

/* The bus states of section 6.1. */
#define XHCI_POLL_BUS_NONE              0UL
#define XHCI_POLL_BUS_ACTIVE            1UL
#define XHCI_POLL_BUS_PERIODIC          2UL
#define XHCI_POLL_BUS_IDLE              3UL
#define XHCI_POLL_BUS_WATCH             4UL     /* a watcher's arm         */

/* What the DPC does with the poller it found (XhciPollTake). */
#define XHCI_POLL_TAKE_NONE             0UL     /* not armed: nothing      */
#define XHCI_POLL_TAKE_RUN              1UL     /* owned: run the pass     */
#define XHCI_POLL_TAKE_CLOSING          2UL     /* owned, closing: end it  */

/* A promotion's first step. */
#define XHCI_POLL_P_NONE                0UL
#define XHCI_POLL_P_CANCEL              1UL     /* KeCancelTimer, then Done */
#define XHCI_POLL_P_OWED                2UL     /* recorded for the DPC     */

/* A close's steps. */
#define XHCI_POLL_C_DONE                0UL     /* CLOSED                  */
#define XHCI_POLL_C_CANCEL              1UL     /* KeCancelTimer, then Done */
#define XHCI_POLL_C_WAIT                2UL     /* CLOSING: wait for CLOSED */

typedef struct _XHCI_POLL_VALUES {
    ULONG ModeRead;         /* as read, XHCI_POLL_NOT_FOUND if not          */
    ULONG Mode;             /* the effective mode: 0, 1, 2 or 3             */
    ULONG IdleRead;
    ULONG IdleMs;
    ULONG ActiveRead;
    ULONG ActiveMs;
    ULONG Found;            /* XHCI_POLL_FOUND_*: each value read as a DWORD */
} XHCI_POLL_VALUES, *PXHCI_POLL_VALUES;

/* XHCI_POLL_VALUES.Found, the read status apart from the value. */
#define XHCI_POLL_FOUND_MODE            0x00000001UL
#define XHCI_POLL_FOUND_IDLE            0x00000002UL
#define XHCI_POLL_FOUND_ACTIVE          0x00000004UL

/*
 * The poller (section 5): the owner state and the role, the armed deadline
 * and the one owed by a promotion while the DPC ran, and the watcher's
 * history. Every field under PollLock.
 */
typedef struct _XHCI_POLL_STATE {
    ULONG State;            /* XHCI_POLL_S_*                                */
    ULONG Role;             /* XHCI_POLL_ROLE_*                             */
    ULONG DueValid;
    ULONG Due;              /* the armed deadline                           */
    ULONG OwedValid;
    ULONG Owed;             /* the earliest promotion while FIRING          */
    ULONG ParkPending;      /* the pass decided to park; the epilogue does  */
    ULONG KickOwed;         /* a grant came while ParkPending               */
    ULONG WatchGen;         /* the service generation of the last look      */
    ULONG WatchPrev;        /* the last look was asserted                   */
    ULONG WatchClaims;      /* InterruptsClaimed at it                      */
    ULONG WatchPairs;       /* stale pairs in a row                         */
    ULONG WatchFirst;       /* the time of the first asserted look of them  */
} XHCI_POLL_STATE, *PXHCI_POLL_STATE;

/* The bus as the service DPC reads it (section 6.1). */
typedef struct _XHCI_POLL_BUS {
    ULONG Command;          /* a command on the command ring                */
    ULONG Async;            /* a control, bulk or isochronous TD            */
    ULONG PortsBusy;        /* a port in reset or enumeration               */
    ULONG DrainedRecently;  /* an event drained within the idle period      */
    ULONG Bounded;          /* the drain stopped at its bound, events left  */
    ULONG PeriodicMs;       /* the shortest interrupt TD's interval, 0 none */
} XHCI_POLL_BUS, *PXHCI_POLL_BUS;

/* The poll runs' intervals (section 7). */
typedef struct _XHCI_POLL_OBS {
    ULONG LastValid;
    ULONG Last;
    ULONG MinMs;            /* XHCI_POLL_NOT_FOUND until one is seen         */
    ULONG MaxMs;
    ULONG Discarded;
} XHCI_POLL_OBS, *PXHCI_POLL_OBS;

VOID XhciPollValues(PXHCI_POLL_VALUES v, ULONG modeFound, ULONG mode,
                    ULONG idleFound, ULONG idle, ULONG activeFound,
                    ULONG active);
ULONG XhciPollDecide(ULONG mode, ULONG haveInterrupt, ULONG message,
                     ULONG messageConnected, ULONG pinStatus, ULONG pin,
                     ULONG messageSystem, PULONG reason);
ULONG XhciPollConnected(ULONG mode, ULONG connected, ULONG lineBased,
                        PULONG reason);

ULONG XhciPollEarlier(ULONG a, ULONG b);
ULONG XhciPollArmMs(ULONG due, ULONG now, ULONG activeMs, ULONG idleMs,
                    PULONG displaced);

VOID XhciPollInit(PXHCI_POLL_STATE st);
ULONG XhciPollOpen(PXHCI_POLL_STATE st, ULONG role);
ULONG XhciPollTake(PXHCI_POLL_STATE st);
VOID XhciPollPark(PXHCI_POLL_STATE st);
VOID XhciPollServe(PXHCI_POLL_STATE st);
ULONG XhciPollEnd(PXHCI_POLL_STATE st, ULONG decision, ULONG now,
                  ULONG activeMs, ULONG idleMs, PULONG armMs,
                  PULONG displaced);
ULONG XhciPollKick(PXHCI_POLL_STATE st, ULONG now, ULONG activeMs,
                   PULONG armMs);
ULONG XhciPollPromote(PXHCI_POLL_STATE st, ULONG deadline);
ULONG XhciPollPromoteDone(PXHCI_POLL_STATE st, ULONG deadline,
                          ULONG cancelled, ULONG now, ULONG activeMs,
                          ULONG idleMs, PULONG armMs, PULONG displaced);
ULONG XhciPollCloseBegin(PXHCI_POLL_STATE st);
ULONG XhciPollCloseDone(PXHCI_POLL_STATE st, ULONG cancelled);
ULONG XhciPollPeekAdmit(const XHCI_POLL_STATE *st);

VOID XhciPollWatchReset(PXHCI_POLL_STATE st);
ULONG XhciPollWatchLook(PXHCI_POLL_STATE st, ULONG gen, ULONG asserted,
                        ULONG claims, ULONG now);

ULONG XhciPollIntervalMs(ULONG epInterval);
ULONG XhciPollDecidePeriod(const XHCI_POLL_BUS *bus, ULONG activeMs,
                           ULONG idleMs, PULONG busState);
VOID XhciPollObserve(PXHCI_POLL_OBS obs, ULONG now);
ULONG XhciPollRecent(PULONG seen, ULONG last, ULONG now, ULONG idleMs);

/*
 * A finer system timer while polling (section 7.2; roadmap-hcd.md task
 * 38.3). The executor is hcd_tmr.c's timer thread, one per controller; the
 * decisions are here: whether a request is wanted (XhciPollTmrWant), the
 * two-state request machine stepped by it and the boundary generation
 * (XhciPollTmrStep), the cause a release records, and the phase observer
 * the poll DPC feeds (XhciPollTmrObserve).
 */

/* The request's two states; there is no third. */
#define XHCI_POLL_TMR_RELEASED          0UL
#define XHCI_POLL_TMR_HELD              1UL

/* What a step asks the executor to call. */
#define XHCI_POLL_TMR_NONE              0UL
#define XHCI_POLL_TMR_HOLD              1UL     /* ExSetTimerResolution TRUE */
#define XHCI_POLL_TMR_RELEASE           2UL     /* FALSE, not wanted       */
#define XHCI_POLL_TMR_RELEASE_FORCED    3UL     /* FALSE, a boundary crossed */

/* TmrReleaseCause (section 7.2.9). */
#define XHCI_POLL_TMR_CAUSE_NONE        0UL
#define XHCI_POLL_TMR_CAUSE_IDLE        1UL     /* the poller left ACTIVE  */
#define XHCI_POLL_TMR_CAUSE_REVOKED     2UL     /* parked or revoked       */
#define XHCI_POLL_TMR_CAUSE_BARRED      3UL     /* a power transition      */
#define XHCI_POLL_TMR_CAUSE_STOP        4UL     /* the stop                */

/* TmrSource. */
#define XHCI_POLL_TMR_SRC_NONE          0UL     /* 98 SE, ME, or switch off */
#define XHCI_POLL_TMR_SRC_HELPER        1UL     /* xhcihlpr.sys's channel  */
#define XHCI_POLL_TMR_SRC_SYSTEM        2UL     /* MmGetSystemRoutineAddress */
#define XHCI_POLL_TMR_SRC_NOT_FOUND     3UL

/* TmrThread. */
#define XHCI_POLL_TMR_THREAD_NONE       0UL
#define XHCI_POLL_TMR_THREAD_RUNNING    1UL
#define XHCI_POLL_TMR_THREAD_FAILED     2UL
#define XHCI_POLL_TMR_THREAD_JOINED     3UL

/* The observer's two rules (section 7.2.9): only a run armed for at most
 * this period shows the clock, and one this much longer than its period
 * is late. */
#define XHCI_POLL_TMR_SHORT_MS          5UL
#define XHCI_POLL_TMR_LATE_MS           3UL

/* The inputs of "wanted", read in one controller-lock hold. */
typedef struct _XHCI_POLL_TMR_IN {
    ULONG Switch;           /* XhciPollFineTimer in effect                 */
    ULONG Routine;          /* the routine found, or still to be looked for */
    ULONG Stop;             /* TmrStop                                     */
    ULONG Delivery;         /* XHCI_POLL_DELIVERY_*                        */
    ULONG Role;             /* XHCI_POLL_ROLE_*                            */
    ULONG Bar;              /* TmrBar                                      */
    ULONG PollService;
    ULONG D0;               /* DevicePower is D0                           */
    ULONG SuspendedInD0;
    ULONG Active;           /* TmrActive                                   */
} XHCI_POLL_TMR_IN, *PXHCI_POLL_TMR_IN;

/* The phase observer (section 7.2.9): the last coherent sample, and per
 * phase (index XHCI_POLL_TMR_RELEASED or _HELD) the intervals counted,
 * their sum in whole ms (saturating) and those late. Under PollLock. */
typedef struct _XHCI_POLL_TMR_OBS {
    ULONG BaseValid;
    ULONG BaseTime;
    ULONG BaseEpoch;
    ULONG BasePeriod;
    ULONG BaseStart;
    ULONG Count[2];
    ULONG SumMs[2];
    ULONG Late[2];
    ULONG Crossed;
} XHCI_POLL_TMR_OBS, *PXHCI_POLL_TMR_OBS;

ULONG XhciPollTmrWant(const XHCI_POLL_TMR_IN *in);
ULONG XhciPollTmrStep(PULONG state, PULONG heldGen, ULONG want, ULONG gen);
ULONG XhciPollTmrCause(ULONG stop, ULONG bar, ULONG forced);
VOID XhciPollTmrObsReset(PXHCI_POLL_TMR_OBS obs);
VOID XhciPollTmrObserve(PXHCI_POLL_TMR_OBS obs, ULONG now, ULONG entryEpoch,
                        ULONG obsEpoch, ULONG phase, ULONG periodMs,
                        ULONG start);
ULONG XhciPollTmrElapsedMs(ULONG hiThen, ULONG loThen, ULONG hiNow,
                           ULONG loNow);
ULONG XhciPollTmrAddMs(ULONG sum, ULONG ms);


/*
 * The line's retirement after the move (section 4.2, step 3), sequenced
 * here over the caller's register access; hcd_poll.c runs it inside
 * KeSynchronizeExecution. The bits are xHCI's (xhci.h holds the same,
 * which hcd_poll.c asserts): USBCMD.INTE and HCRST, USBSTS.CNR, IMAN's IP
 * and IE.
 */
#define XHCI_POLL_REG_USBCMD            0UL
#define XHCI_POLL_REG_USBSTS            1UL
#define XHCI_POLL_REG_IMAN              2UL
#define XHCI_POLL_USBCMD_HCRST          0x00000002UL
#define XHCI_POLL_USBCMD_INTE           0x00000004UL
#define XHCI_POLL_USBSTS_CNR            0x00000800UL
#define XHCI_POLL_IMAN_IP               0x00000001UL
#define XHCI_POLL_IMAN_IE               0x00000002UL
#define XHCI_POLL_RETIRE_ATTEMPTS       3UL

/* XhciPollRetire's answers. */
#define XHCI_POLL_RETIRE_PROVEN         0UL     /* IP, IE, INTE read 0     */
#define XHCI_POLL_RETIRE_ENABLES        1UL     /* an enable not proven    */
#define XHCI_POLL_RETIRE_IP             2UL     /* IP not proven           */
#define XHCI_POLL_RETIRE_RESETTING      3UL     /* a reset is under way    */

typedef struct _XHCI_POLL_REGS {
    PVOID Context;
    ULONG (*Read)(PVOID context, ULONG reg);
    /* USBCMD with INTE cleared, by the caller's reserved-preserving
     * write from the value read; never carries HCRST. */
    VOID (*ClearInte)(PVOID context, ULONG usbcmd);
    VOID (*WriteIman)(PVOID context, ULONG value);
} XHCI_POLL_REGS, *PXHCI_POLL_REGS;

ULONG XhciPollRetire(const XHCI_POLL_REGS *io);

/* The retirement's state in the counter block (record 15 section 8). */
#define XHCI_POLL_RETIRED_NONE          0UL     /* nothing owed            */
#define XHCI_POLL_RETIRED_ACK           1UL     /* masked acknowledgement  */
#define XHCI_POLL_RETIRED_INTX_DISABLE  2UL     /* PCI Interrupt Disable   */
#define XHCI_POLL_RETIRED_UNCONTAINED   3UL     /* a stop with it owed     */
#define XHCI_POLL_RETIRED_HCRST         4UL     /* a completed HCRST       */
#define XHCI_POLL_RETIRED_OWED          5UL     /* moved, not yet proven   */

#endif /* XHCI_POLL_H */
