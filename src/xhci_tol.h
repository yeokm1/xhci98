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

/* Hub ports with a location budget in the extension: HCD_MAX_HUBS hub
 * objects (XHCI_TOPO_NODES) of HCD_HUB_MAX_PORTS (XHCI_HUB_MAX_PORTS) each,
 * in port-object order; restated, and checked where both are seen
 * (hcd_enum.c). */
#define XHCI_TOL_HUB_LOCS           (16 * 14)
#define XHCI_TOL_HUB_LOC_PORTS      14UL    /* per hub object, for the dump */

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

/* Where the controller stands (35-T.8, the dump's terminal reason). */
#define XHCI_TOL_TERMINAL_NONE      0UL     /* running                       */
#define XHCI_TOL_TERMINAL_OWED      1UL     /* failed, a recovery still owed */
#define XHCI_TOL_TERMINAL_FAILURES  2UL     /* recoveries failed in a row    */
#define XHCI_TOL_TERMINAL_WINDOW    3UL     /* the recovery window refused   */
#define XHCI_TOL_TERMINAL_UNREADABLE 4UL    /* contained as unreadable       */
#define XHCI_TOL_TERMINAL_DMA_UNPROVEN 5UL  /* contained: a readable
                                             * controller not proven to
                                             * have stopped mastering     */

/* Why a controller is contained (XHCI_TOL_STATE.Unreadable, nonzero for
 * either): the one latch every submission path, the recovery and the
 * resume read, its value the reason the dump names. */
#define XHCI_TOL_CONTAINED_UNREADABLE   1UL /* USBSTS all ones (35-T.6)      */
#define XHCI_TOL_CONTAINED_DMA_UNPROVEN 2UL /* an invalidation no halt, HCRST
                                             * or Bus Master Enable clear
                                             * proved (hcd_enum.c)        */

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

/* 1 when a matched event is the retry's exhaustion: a Transaction Error on
 * the head of a queue in scope whose TD has spent its three retries. It
 * takes today's path whole; this only counts it. */
ULONG XhciTolRetryExhausted(ULONG inScope, ULONG code, ULONG isHead,
                            ULONG retriesUsed);

/* The thread's first decision on a pipe with RetryWanted, under the
 * controller lock: REPLAY applies the deferred outcome (today's error path)
 * when tolerance is off, an operation is pending on the pipe or its device
 * (a cancel, ABORT_PIPE, RESET_PIPE, SYNC_RESET_PIPE or RESET_PORT, folded
 * by the caller into opPending), DrainPending is set, or the queue's head is
 * no longer the diverted TD; RESET issues Reset Endpoint with TSP 1. */
#define XHCI_TOL_RETRY_REPLAY       0UL
#define XHCI_TOL_RETRY_RESET        1UL

ULONG XhciTolRetryDecide(ULONG tolerance, ULONG opPending, ULONG drainPending,
                         ULONG headIsTd);

/* The second, once the Reset Endpoint has completed: RING the doorbell and
 * clear RetryWanted when the retry generation still reads the one decided
 * on; LEAVE RetryWanted set when a newer divert re-armed it meanwhile; and
 * FAULT (hcdCfgFault) when the command failed, whatever the generation. */
#define XHCI_TOL_RETRY_RING         0UL
#define XHCI_TOL_RETRY_LEAVE        1UL
#define XHCI_TOL_RETRY_FAULT        2UL

ULONG XhciTolRetryAfterReset(ULONG commandOk, ULONG genNow,
                             ULONG genDecided);

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

/* The event path's one decision (record 17 section 4.3): the reason a
 * Transfer Event marks its device, XHCI_TOL_CYCLE_NONE for none. claimed:
 * XhciXferCodeInfo took the code; unattributed: the queue could match no TD
 * to it - a zero pointer, one on no ring of the endpoint (a stream ring's
 * or a pointer above 4 GB, Foreign by pointer included) or inside no TD.
 * A refused code (T3) first, then a halt candidate (T4) on an open pipe. */
ULONG XhciTolCycleReason(ULONG tolerance, ULONG claimed, ULONG unattributed,
                         ULONG code, ULONG isoch, ULONG hasDevice,
                         ULONG pipeOpen);

/*
 * A device's cycle mark (record 17 section 4.3), kept on its record: set by
 * the event path under the controller lock, read and cleared by the thread
 * under it. Refused: a refused code (T3), which cycles in any endpoint
 * state. HaltDcis: the endpoints (DCI bits) a halt with no TD named (T4),
 * whose contexts the thread has yet to read. Confirmed: one of them read
 * Halted or Error. Gen: the location's connect generation when first
 * marked. A record is zeroed when it is made, which is the empty mark.
 */
typedef struct _XHCI_TOL_MARK {
    ULONG Refused;
    ULONG HaltDcis;
    ULONG Confirmed;
    ULONG Gen;
} XHCI_TOL_MARK, *PXHCI_TOL_MARK;

VOID XhciTolMarkInit(PXHCI_TOL_MARK mark);

/* Mark for reason (XHCI_TOL_CYCLE_REFUSED_CODE or _HALT_NO_TD; any other is
 * ignored) on endpoint dci (1..31; a halt naming another is ignored), at the
 * location's connect generation gen. The first mark stamps gen; a later one
 * keeps it. Returns 1 when the device was not marked before. */
ULONG XhciTolMarkSet(PXHCI_TOL_MARK mark, ULONG reason, ULONG dci, ULONG gen);

/* 1 while anything is marked, confirmed or not. */
ULONG XhciTolMarkPending(const XHCI_TOL_MARK *mark);

/* The thread's reading: halted holds the DCI bits of HaltDcis whose contexts
 * it read Halted or Error (XhciTolHaltConfirmed). HaltDcis is consumed.
 * Returns the reason the device is cycled for - a refused code first, then
 * a confirmed halt - or XHCI_TOL_CYCLE_NONE, when every halt read stale and
 * the mark is cleared: a stale event costs its context read and nothing
 * more. */
ULONG XhciTolMarkResolve(PXHCI_TOL_MARK mark, ULONG halted);

/*
 * One marked endpoint's context read, beside a soft retry (35-T.2/3/4):
 * Error always confirms the halt with no TD (a Reset Endpoint, the retry's
 * command, is illegal there); Halted confirms it unless a live retry
 * explains it - the queue's head is still the deferred TD RetryWanted
 * names (retryHeadDeferred), whose Transaction Error halted the endpoint
 * and whose Reset Endpoint is owed - and then the retry owns it; any other
 * state is stale.
 */
#define XHCI_TOL_HALT_STALE         0UL
#define XHCI_TOL_HALT_CONFIRMED     1UL
#define XHCI_TOL_HALT_RETRY         2UL

ULONG XhciTolHaltOwner(ULONG epState, ULONG retryHeadDeferred);

/*
 * Who recovers a device this pass, decided once, by the soft retry's
 * service, from the mark as XhciTolMarkResolve left it (reason) and whether
 * a pipe of it holds a live retry: a reason commits the pass to the cycle
 * - charged to the location's budget and taken by this pass's cycle
 * service, the retry left for the teardown - and otherwise the retry
 * proceeds now. So each pass either consumes a retry or charges a cycle,
 * however often a new mark arrives between passes.
 */
#define XHCI_TOL_JOIN_NONE          0UL
#define XHCI_TOL_JOIN_RETRY         1UL
#define XHCI_TOL_JOIN_CYCLE         2UL

ULONG XhciTolJoin(ULONG reason, ULONG retryLive);

/* What the thread does with a resolved mark (record 17 section 4.3). */
#define XHCI_TOL_CYCLE_ACT_NONE     0UL     /* nothing marked, or stale      */
#define XHCI_TOL_CYCLE_ACT_DROP     1UL     /* gone or replaced: counted     */
#define XHCI_TOL_CYCLE_ACT_PUBLISHED 2UL    /* charged, then HcdEnumCycle    */
#define XHCI_TOL_CYCLE_ACT_PRE_PDO  3UL     /* the pre-PDO cycle             */

/* reason from XhciTolMarkResolve; sameDevice 1 when the device is still the
 * one at its location; markGen the mark's generation, locGen the
 * location's now; published 1 when the device has its PDO and its group's
 * serial (HcdEnumCycle's own condition). */
ULONG XhciTolCycleAct(ULONG reason, ULONG sameDevice, ULONG markGen,
                      ULONG locGen, ULONG published);

/* After the pre-PDO cycle's teardown, with the location's machine Empty
 * (empty): 1 when a CONNECT is fed - the charge allowed (charged; a held
 * location is never charged) and the location still reads connected. */
ULONG XhciTolCycleReconnect(ULONG charged, ULONG empty, ULONG connected);

/*
 * The thread's wait on its own control transfer (record 17 section 4.3),
 * one look at a time under the controller lock: done the transfer retired
 * (Ep0Done), marked a cycle mark pending on the device, expired the one
 * deadline the transfer has (the original 5000 ms, across every wake). A
 * completion wins; a pending mark is read before anything else, a timeout
 * included, so a mark is never left unread behind a reset; only an expired
 * deadline with neither is a timeout. A stale mark resolves to nothing and
 * the wait goes on to the same deadline: it costs its context read alone.
 * SLEEP: the caller clears the wake event in the same lock hold, then waits.
 */
#define XHCI_TOL_WAIT_SLEEP         0UL
#define XHCI_TOL_WAIT_DONE          1UL
#define XHCI_TOL_WAIT_RESOLVE       2UL
#define XHCI_TOL_WAIT_TIMEOUT       3UL

ULONG XhciTolWaitStep(ULONG done, ULONG marked, ULONG expired);

/*
 * After a RESOLVE: reason is what the mark resolved to, done and expired
 * read with it. A confirmed reason abandons; otherwise a completion is
 * taken, and a deadline already passed is the timeout here and now - not
 * after another look, which a new stale mark installed on another
 * processor between the resolution and that look could defer for ever.
 * AGAIN: look again (the deadline still runs). The submission's own check
 * takes the same answer with done 0, TIMEOUT meaning nothing was sent.
 */
#define XHCI_TOL_WAIT_ABANDON       4UL
#define XHCI_TOL_WAIT_AGAIN         5UL

ULONG XhciTolWaitResolved(ULONG reason, ULONG done, ULONG expired);

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
/* Eight ULONGs: the clock stamps and the dump's counts whole, the budgets,
 * the hold and the flags a byte each (none exceeds 3, or 1), because there
 * is one per root port and one per hub port in the extension and XHCISNAP
 * keeps the whole extension in a 256 KB image (xhcisnap.c,
 * EXT_IMAGE_MAX). */
typedef struct _XHCI_TOL_LOC {
    ULONG Charges;      /* every charge, never re-armed (the dump)          */
    ULONG Holds;        /* times held, never re-armed (the dump)            */
    ULONG Rearms;       /* re-arms, never cleared but by a start (the dump) */
    ULONG LastCharge;   /* tolerance clock at the last charge               */
    ULONG DiscStamp;    /* ...when the stable disconnect began              */
    ULONG ProgStamp;    /* ...when the stable progress began                */
    UCHAR Reenums;      /* charged re-enumerations                          */
    UCHAR Repowers;     /* charged repowers                                 */
    UCHAR Hold;         /* XHCI_TOL_HOLD_*                                  */
    UCHAR Charged;      /* 1 once any charge was made                       */
    UCHAR DiscArmed;    /* a stable disconnect is being timed               */
    UCHAR DiscSeen;     /* stable-disconnect evidence stands                */
    UCHAR ProgArmed;    /* a completion since the last charge or fault      */
    UCHAR RecoveryDisc; /* a disconnect the driver's own recovery causes is
                         * expected: no disconnect is evidence until a
                         * connection has been observed after it          */
} XHCI_TOL_LOC, *PXHCI_TOL_LOC;

/* A controller start: every field cleared. */
VOID XhciTolLocInit(PXHCI_TOL_LOC loc);

/* Charge one action of kind (XHCI_TOL_CHARGE_*) at now. Returns 1 when the
 * budget allowed it; otherwise the location is held and 0 returned. A held
 * location is never charged. */
ULONG XhciTolLocCharge(PXHCI_TOL_LOC loc, ULONG kind, ULONG now);

/* A look at the port: connected (CCS, or the hub's port status), powered
 * (PP, or the hub's PORT_POWER), fault (an over-current or another recovery
 * the driver itself runs is active), changed (a connection change since the
 * previous look: CSC, or the hub's C_PORT_CONNECTION). The stable
 * disconnect is timed from the first disconnected look and stands only
 * when a later disconnected look finds the interval passed with no
 * connection change between; a connection after that re-arms the budget
 * and releases a powered hold. A disconnect the driver's recovery causes
 * (RecoveryDisc) is never evidence, through the connection that follows
 * it. Duration is never inferred from a late look: the executor schedules a
 * look when one is due (XhciTolLocDiscDue). Returns 1 when it re-armed. */
ULONG XhciTolLocObserveChange(PXHCI_TOL_LOC loc, ULONG connected,
                              ULONG powered, ULONG fault, ULONG changed,
                              ULONG now);

/* XhciTolLocObserveChange with no connection change. */
ULONG XhciTolLocObserve(PXHCI_TOL_LOC loc, ULONG connected, ULONG powered,
                        ULONG fault, ULONG now);

/* A recovery of the driver's own is about to take the port's connection
 * away (an over-current's lost power, a warm reset): the disconnect and the
 * reconnect after it are not evidence. A charge does not mark it: a
 * re-enumeration's disconnect is software only and CCS stays set. */
VOID XhciTolLocRecovery(PXHCI_TOL_LOC loc);

/* 1 when a disconnected look is owed: the stable disconnect is being timed
 * and its interval has passed, so a look now can confirm it. */
ULONG XhciTolLocDiscDue(const XHCI_TOL_LOC *loc, ULONG now);

/* Hold the location for reason (XHCI_TOL_HOLD_*): an over-current wait
 * that ran out, or a powered hold made unpowered by an over-current whose
 * repower it cannot charge. Holds counts a location newly held. */
VOID XhciTolLocHold(PXHCI_TOL_LOC loc, ULONG reason);

/* 1 when the hold leaves the port unpowered: released only by a start. */
ULONG XhciTolLocUnpowered(const XHCI_TOL_LOC *loc);

/* 1 when the location's budget is observed and its hold enforced: always
 * with tolerance on; at XhciTolerance 0 only once something charged it,
 * which there is a slot-fatal teardown alone (hcd_enum.c,
 * hcdSlotFatalService) - so a location it never charged behaves as it
 * always has. */
ULONG XhciTolLocActive(ULONG tolerance, const XHCI_TOL_LOC *loc);

/* 1 when the location enumerates nothing: active and held. */
ULONG XhciTolLocHeld(ULONG tolerance, const XHCI_TOL_LOC *loc);

/* The one charge of a location's pending cycle: every producer that asks
 * for the cycle of the device at one connect generation - a configuration
 * fallback, a slot-fatal teardown, 35-T.3/4's cycle, a PED or hub-port
 * disable - shares the charge the first one made, since one cycle runs.
 * A connect or a disconnect fed at the location moves the generation and
 * so begins the next. Thread only, in the port. */
typedef struct _XHCI_TOL_CYCLE_CHARGE {
    ULONG Gen;          /* the connect generation charged               */
    ULONG Valid;        /* a charge was made at Gen                     */
    ULONG Allowed;      /* ...and what the budget answered              */
} XHCI_TOL_CYCLE_CHARGE, *PXHCI_TOL_CYCLE_CHARGE;

/* No charge made (a start). */
VOID XhciTolCycleChargeInit(PXHCI_TOL_CYCLE_CHARGE once);

/* The re-enumeration charge for the cycle at connect generation gen: made
 * once (XhciTolLocCharge, XHCI_TOL_CHARGE_REENUM) and that answer returned
 * to every later producer at the same generation, which charges nothing. */
ULONG XhciTolCycleCharge(PXHCI_TOL_CYCLE_CHARGE once, PXHCI_TOL_LOC loc,
                         ULONG gen, ULONG now);

/* A device at the location completed a transfer. The first completion
 * after a charge, a fault or a disconnect starts the stable-progress
 * interval; a completion once it has passed, with none of those between,
 * re-arms - so one completion after a long stall is not progress. Returns
 * 1 when it re-armed. */
ULONG XhciTolLocProgress(PXHCI_TOL_LOC loc, ULONG now);

/* 35-T.5. 1 when a root port's change is the PED fault: tolerance on,
 * USB 2.0, PEC set, PED clear, CCS set, a device held. */
ULONG XhciTolPedFault(ULONG tolerance, ULONG usb2, ULONG pec, ULONG ped,
                      ULONG ccs, ULONG hasDevice);

/* 1 when a root port is in over-current: tolerance on and OCC set, or PP
 * clear while the driver's own state says it powered the port. */
ULONG XhciTolOcFault(ULONG tolerance, ULONG occ, ULONG pp, ULONG powered);

/* At XhciTolerance 0 (record 17 section 4.11), what the dump counts and
 * nothing acts on: 1 for OCC, or for PP clear when *lost is clear; *lost
 * follows PP, so one power loss counts once whether OCC came with it. */
ULONG XhciTolOffOcCount(ULONG occ, ULONG pp, PULONG lost);

/* A root port's over-current episode (record 17 section 4.5): OCA read on
 * each pass until it has read clear for the settle interval, then PP set
 * and the power-on interval waited before PORTSC is read afresh; an OCA
 * that never clears for the settle interval within the over-current wait
 * gives up. */
#define XHCI_TOL_OC_NONE            0UL
#define XHCI_TOL_OC_WAIT            1UL     /* OCA set, or not yet read     */
#define XHCI_TOL_OC_SETTLE          2UL     /* OCA read clear, timing       */
#define XHCI_TOL_OC_POWER_ON        3UL     /* PP set, waiting              */

#define XHCI_TOL_OC_ACT_NONE        0UL
#define XHCI_TOL_OC_ACT_REPOWER     1UL     /* charge it, then set PP       */
#define XHCI_TOL_OC_ACT_INSPECT     2UL     /* read PORTSC afresh           */
#define XHCI_TOL_OC_ACT_GIVE_UP     3UL     /* hold the port unpowered      */

typedef struct _XHCI_TOL_OC {
    ULONG Phase;        /* XHCI_TOL_OC_*                                    */
    ULONG Stamp;        /* the episode's start; in POWER_ON, the repower's  */
    ULONG SettleStamp;  /* OCA first read clear                             */
} XHCI_TOL_OC, *PXHCI_TOL_OC;

VOID XhciTolOcInit(PXHCI_TOL_OC oc);

/* An over-current found at now: the episode starts, unless one is already
 * waiting for OCA, whose wait it does not extend. */
VOID XhciTolOcBegin(PXHCI_TOL_OC oc, ULONG now);

/* One pass, oca the OCA bit read. REPOWER moves the episode to POWER_ON
 * stamped now - the caller ends it (XhciTolOcInit) if the charge is
 * refused; INSPECT and GIVE_UP end it. */
ULONG XhciTolOcStep(PXHCI_TOL_OC oc, ULONG oca, ULONG now);

/* 35-T.6. 1 when HCH requests the in-place recovery: tolerance on, HCH
 * set, and the driver's state says the controller runs (D0, started, not
 * stopping, not recovering, no power transition, Run/Stop last written 1;
 * the caller folds those into runs). */
ULONG XhciTolHchRecover(ULONG tolerance, ULONG hch, ULONG runs);

/* The recovery window: three recoveries begun in ten minutes. */
typedef struct _XHCI_TOL_WINDOW {
    ULONG Count;                            /* stamps held, 0..3            */
    ULONG Stamp[XHCI_TOL_RECOVERIES];       /* oldest first                 */
    ULONG Refused;                          /* recoveries not begun; nonzero
                                             * latches the terminal until
                                             * the next start               */
} XHCI_TOL_WINDOW, *PXHCI_TOL_WINDOW;

VOID XhciTolWindowInit(PXHCI_TOL_WINDOW win);

/* A recovery about to begin at now. Returns 1 and records it when fewer
 * than three began inside the window; 0 (counted) otherwise. At tolerance
 * 0 it always returns 1 and records nothing. */
ULONG XhciTolWindowAdmit(PXHCI_TOL_WINDOW win, ULONG tolerance, ULONG now);

/* The terminal reason a dump names (XHCI_TOL_TERMINAL_*): failed is
 * ControllerFailed; unreadable, Unreadable (XHCI_TOL_CONTAINED_*, the
 * unproven-DMA containment its own reason); windowRefused, the window's
 * Refused; failures and maxFailures, RecoveryFailuresConsecutive and its
 * bound. The containment outranks the window, the window the run of
 * failures - each latch stops the next from being reached. A failed
 * controller at none of them still has a recovery owed. */
ULONG XhciTolTerminal(ULONG failed, ULONG unreadable, ULONG windowRefused,
                      ULONG failures, ULONG maxFailures);

/* A terminal no recovery will act on - the window refused one, or the run
 * of failures is spent - still holds every transfer the halted controller
 * was given, and no recovery's invalidation is coming to complete them, so
 * no class driver's request ends and no stop is ever sent. 1 when the
 * thread is to raise that invalidation itself: terminal (XhciTolTerminal's
 * value) WINDOW or FAILURES, none already pending, not yet raised in this
 * lifetime, and a device record left to drop. A containment drains on its
 * own (hcdContain, HcdEnumService). Not a tolerance behaviour: at
 * XhciTolerance 0 the window never refuses, and the run of failures applies
 * as at 1. */
ULONG XhciTolTerminalRelease(ULONG terminal, ULONG pending, ULONG raised,
                             ULONG devices);

/* USBSTS's evidence that the controller stopped executing: HCH set on a
 * window that decodes (an all-ones read carries HCH and proves nothing). */
ULONG XhciTolHaltProven(ULONG usbsts);

/* 1 when a Save State (CSS) or Restore State (CRS) must not be issued: the
 * controller is latched failed (ControllerFailed). CSS writes cached
 * contexts to memory while halted, and a failed controller's slots may
 * name buffers already given back; only a HCRST, which clears the latch,
 * retires them. At every XhciTolerance value. */
ULONG XhciTolSaveRefused(ULONG failed);

/* 1 when a resume must not reinitialize the controller: terminal
 * (XhciTolTerminal's value) is a containment, the window's refusal or the
 * spent run of failures - each holds until a stop and start. A failed
 * controller with a recovery still owed resumes as before. */
ULONG XhciTolResumeRefused(ULONG terminal);

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

/* 1 when an owed recovery waits: tolerance on, the containment step's own
 * admission held, and USBSTS read all-ones. A reinitialization cannot run
 * through a window that stopped decoding, and each attempt would begin a
 * new start generation that restamps the episode, so the recovery is
 * deferred, uncharged, until the window answers again or the controller is
 * contained. */
ULONG XhciTolRecoverDefer(ULONG tolerance, ULONG admitted, ULONG allOnes);

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
 * external hub's ports have theirs here too, HubLoc in port-object order,
 * so the dump shows every location's budget and hold; each is set again
 * when its hub object is brought up. */
typedef struct _XHCI_TOL_STATE {
    XHCI_TOL_STATS Stats;
    XHCI_TOL_OBS Obs;
    XHCI_TOL_WINDOW Window;
    XHCI_TOL_DEAD Dead;
    ULONG Unreadable;       /* 4.6: submissions park; XHCI_TOL_CONTAINED_* */
    ULONG Clock;            /* the tolerance clock, in ticks                */
    XHCI_TOL_LOC RootLoc[XHCI_TOL_ROOT_PORTS];
    XHCI_TOL_LOC HubLoc[XHCI_TOL_HUB_LOCS];
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
