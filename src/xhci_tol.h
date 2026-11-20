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
/* XhciTiIntervalCap (Linux's XHCI_LIMIT_ENDPOINT_INTERVAL_7; task 38.10,
 * design record 20 section 4.9). */
#define XHCI_TOL_TI_INTERVAL_CAP    6UL     /* 2^6 * 125 us = 8 ms           */
#define XHCI_TOL_AVG_TRB_DEFAULT    1024UL

/* XhciIntervalCap. */
#define XHCI_TOL_CAP_OFF            0UL
#define XHCI_TOL_CAP_GATED          1UL     /* vendor 1022, and 1002:7316    */
#define XHCI_TOL_CAP_ALL            2UL

/* Why a location is held (XHCI_TOL_LOC.Hold); 0 when not held. */
#define XHCI_TOL_HOLD_NONE          0UL
#define XHCI_TOL_HOLD_REENUMS       1UL     /* powered                       */
#define XHCI_TOL_HOLD_REPOWERS      2UL     /* unpowered                     */
#define XHCI_TOL_HOLD_OC_WAIT       3UL     /* unpowered, OCA never cleared  */
/* Task 50.30 step 3 (design record 17 section 4.15.4, item 4): powered,
 * its device stopped answering; released as REENUMS is. */
#define XHCI_TOL_HOLD_UNANSWERED    4UL

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
#define XHCI_TOL_CONTAIN_TUNNEL     3UL     /* released on the USB4 tunnel's
                                               closed path (4.13.6)          */

/* hcdContainProve's verdict (record 17 section 4.13.6, task 45.35). */
#define XHCI_TOL_PROOF_NONE         0UL
#define XHCI_TOL_PROOF_BME          1UL     /* Bus Master Enable read clear  */
#define XHCI_TOL_PROOF_TUNNEL       2UL     /* the tunnel's isolation        */

/* Where the controller stands (35-T.8, the dump's terminal reason). */
#define XHCI_TOL_TERMINAL_NONE      0UL     /* running                       */
#define XHCI_TOL_TERMINAL_OWED      1UL     /* failed, a recovery still owed */
#define XHCI_TOL_TERMINAL_FAILURES  2UL     /* recoveries failed in a row    */
#define XHCI_TOL_TERMINAL_WINDOW    3UL     /* the recovery window refused   */
#define XHCI_TOL_TERMINAL_UNREADABLE 4UL    /* contained as unreadable       */
#define XHCI_TOL_TERMINAL_DMA_UNPROVEN 5UL  /* contained: a readable
                                             * controller not proven to
                                             * have stopped mastering     */
#define XHCI_TOL_TERMINAL_MSG4      6UL     /* mode 4's message lost (design
                                             * record 15 section 4.5.4)    */

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
 * vendor/device dword): at 1 every AMD (1022) controller and, since task
 * 38.10, the ATI Navi10 USB-C controller 1002:7316 (design record 20
 * section 4.10). */
ULONG XhciTolCapApplies(ULONG capMode, ULONG vendorDevice);

/* 38.10, XhciTiIntervalCap (design record 20 section 4.9): an interrupt
 * endpoint of a High-Speed or faster device whose Interval is 7 or more
 * programmed as 6, when the quirk applies; anything else unchanged. Applied
 * after XhciTolCapInterval, as Linux orders the two (xhci-mem.c:1452-1463);
 * isochronous endpoints are never capped (owner, 2026-10-13). */
ULONG XhciTolTiCapInterval(ULONG applies, ULONG isInterrupt,
                           ULONG highOrAbove, ULONG interval);

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

/* 1 when a queue is in the soft retry's scope: tolerance on, bulk or
 * interrupt (bulkOrInt), not behind a TT, the controller not listed. A
 * pipe's own queue and, since 36.54 (record 17 section 4.12.2), a stream's
 * own queue alike; an endpoint's own queue never holds a TD while its
 * streams are open. */
ULONG XhciTolRetryScope(ULONG tolerance, ULONG vendorDevice, ULONG bulkOrInt,
                        ULONG behindTt);

/* 36.54 (record 17 section 4.12.4). 1 for QEMU's xHCI, PCI 1B36:000D
 * exactly (vendorDevice as XHCI_EXTENSION.PciVendorDevice holds it, device
 * << 16 | vendor), whose stream contexts are re-read with CCS forced to 1
 * after a stop or reset; 0 for every other ID. */
#define XHCI_TOL_QEMU_XHCI_ID       0x000D1B36UL
ULONG XhciTolQemuStreamQuirk(ULONG vendorDevice);

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

/* 1 when an event on an endpoint with streams open that no stream's TD
 * claims halts the endpoint and is recovered on it, not by cycling the
 * device: Stall, Transaction, Babble or Split Transaction, not an Event Data
 * event (which names no TRB). The endpoint is Halted with every stream's
 * requests on it; the thread resets it and completes them, as for a Prime
 * Pipe STALL (hcd_dev.c, hcd_cfg.c hcdCfgCancelOne), and the class driver's
 * own recovery (xhciuas: a device reset through RESET_PORT) follows with
 * the device still present. A user report of 2026-11-04 (XP x64, Intel
 * 8086:1E31, a UAS disk): one Babble on the data-in endpoint inside no TD
 * cycled the disk, a surprise removal of a volume in use. Whatever the
 * tolerance setting. */
ULONG XhciTolStreamHaltDrain(ULONG code, ULONG eventData);

/* 1 when a halt candidate's context state (XHCI_EP_STATE_*) cycles. */
ULONG XhciTolHaltConfirmed(ULONG epState);

/*
 * Issue 26 (2026-11-07): an endpoint brought to rest by Stop Endpoint,
 * bounded, as Linux's xhci-ring.c does at a Stop Endpoint's completion. A
 * Stop after which the Endpoint Context reads Running, or one answered
 * Context State Error, raced a restart: a doorbell rung before the
 * caller's pause (some controllers read Stopped for up to a few ms after
 * one, then run) or a device's ERDY on another stream. The Stop is issued
 * again after a short wait, at most XHCI_TOL_STOP_RESTOPS times. An
 * endpoint still Running then, or a Stop that ended with any other code
 * (0: no completion), is given up: the caller faults the device rather
 * than leave the endpoint paused and its requests held (the cancel that
 * returned with the endpoint Running and nothing owed, 3.4.1.0). A
 * Context State Error with the endpoint read Stopped is stopped again too
 * and, once the restops are spent, taken as Stopped. The callbacks run in
 * the caller's context (the controller thread, PASSIVE_LEVEL).
 */
#define XHCI_TOL_STOP_RESTOPS   3UL

#define XHCI_TOL_STOP_REST      0UL     /* *state: the context's state now */
#define XHCI_TOL_STOP_GIVEUP    1UL
/* Task 50.30 step 3 (design record 17 section 4.15.5a, the
 * hcdCfgStopBounded row): a Stop answered aborted (25) or left
 * (XHCI_TOL_CMD_LEFT) is not a give-up: answered at once, with no restop
 * and no state read; the caller requests the mandatory reset itself. */
#define XHCI_TOL_STOP_ABORTED   2UL

/*
 * Task 50.30 step 3 (section 4.15.3): the result the thread's command
 * wait gives a command it left for a stop, a removal or a power transition
 * (the HCD's HCD_CMD_LEFT is this value). Not a completion code (those are
 * 0 to 255). XhciTolCmdVerdict: whether a command's result is an abort
 * verdict - aborted cleanly (25) or left - which section 4.15.5a's rows
 * own as such, never as a refusal. Pure.
 */
#define XHCI_TOL_CMD_LEFT       0x100UL
ULONG XhciTolCmdVerdict(ULONG code);

/*
 * Task 50.30 step 3 (section 4.15.5a, "The failed device is frozen" and
 * "The verdict survives the wrappers"). XhciTolFrozenAdmits: whether a
 * command naming a frozen device's slot is still submitted - the
 * teardown's own Stop Endpoint and Disable Slot alone (`teardown`: issued
 * by the device's teardown, hcdDeviceStop and hcdDisableSlotId, or by the
 * settlement), never a Stop Endpoint of another caller, whatever its type
 * (reviewed and revised: the type alone once admitted them all).
 * XhciTolFaultStep:
 * hcdCfgFault's step before its present-device reset, for a device that
 * carries a mark (`marked`: an abort verdict, a left command, or failed
 * alone already): none without one; a hub keeps the reset (section 4.15.0
 * item 2); any other device fails alone - cycled now if published, OWED
 * when its cycle is already queued (`cycled`), left to its enumeration
 * step if it has no PDO yet. XhciTolLateAlone: whether hcdEp0Late's
 * verdict fails a present device alone (section 4.15.5): the handling
 * applying, the device not found leaving, read present, and the verdict a
 * cycle or, before its PDO, its enumeration step's failure. Pure.
 */
#define XHCI_TOL_FAULT_NONE     0UL     /* no mark: today's rules          */
#define XHCI_TOL_FAULT_RESET    1UL     /* a hub with the mark: the reset  */
#define XHCI_TOL_FAULT_CYCLE    2UL     /* failed alone, cycled now        */
#define XHCI_TOL_FAULT_OWED     3UL     /* failed alone, its cycle queued  */
#define XHCI_TOL_FAULT_STEP     4UL     /* failed alone, its enumeration's */
ULONG XhciTolFrozenAdmits(ULONG type, ULONG teardown);
ULONG XhciTolFaultStep(ULONG marked, ULONG hub, ULONG published,
                       ULONG cycled);
ULONG XhciTolLateAlone(ULONG isolate, ULONG left, ULONG present,
                       ULONG verdict);
/* Whether the thread's pass fails a device a left or aborted command
 * named (section 4.15.3, "Who settles a left command", the first case, and
 * the reconciliation after a Restore State): only once the debt is clear
 * (`debtClear`, ordinary admission open again), a device not gone, not
 * failed alone already, carrying CmdLeft or AbortVerdict, or
 * AdmitRefused (`admitRefused`: a command naming it that admission
 * refused under another command's debt, XhciTolAdmitMarks). Pure. */
ULONG XhciTolMarkedFails(ULONG debtClear, ULONG gone, ULONG failedAlone,
                         ULONG cmdLeft, ULONG abortVerdict,
                         ULONG admitRefused);
/*
 * Task 50.30 step 3 (reviewed and revised): whether an ordinary command
 * that admission refused under a left command's debt marks the device
 * its slot names (AdmitRefused). Nothing of it was submitted, but its
 * caller may already have changed the device's state for it (a pipe
 * paused for a Reset Endpoint), and with the debt standing no reset that
 * caller requests goes out before the clearing; the mark makes the
 * device owned and reconciled as a left command's is. `hasRecord`: a
 * device record holds the slot. Not an Enable Slot or a slotless command
 * (no device), and not a Disable Slot, whose record the teardown
 * quarantines as owed for the settlement. Pure.
 */
ULONG XhciTolAdmitMarks(ULONG trbType, ULONG hasRecord);
/*
 * Task 50.30 step 3 (reviewed and revised): whether such a refusal owes
 * the reset, deferred under the debt (CMD_KEPT, the TRB type): a Stop
 * Endpoint outside the teardown's scope (`teardown`: CmdTeardown), whose
 * caller cannot proceed as though the endpoint were at rest and whose
 * endpoint is left as it was - section 4.15.3's table keeps the reset
 * for a Stop that did not complete, whichever caller issued it, so no
 * caller (selective suspend's stop among them) has to request it
 * itself. The teardown's own refused Stop is not: its record is
 * quarantined as owed, and the settlement stops and disables it. Pure.
 */
ULONG XhciTolAdmitOwesReset(ULONG trbType, ULONG hasRecord,
                            ULONG teardown);

typedef ULONG (*XHCI_TOL_STOP_CMD)(PVOID context);
typedef ULONG (*XHCI_TOL_STOP_STATE)(PVOID context);
typedef VOID (*XHCI_TOL_STOP_WAIT)(PVOID context);

typedef struct _XHCI_TOL_STOP_IO {
    XHCI_TOL_STOP_CMD Stop;         /* issue Stop Endpoint: its code, or 0 */
    XHCI_TOL_STOP_STATE State;      /* the Endpoint Context's EP State     */
    XHCI_TOL_STOP_WAIT Wait;        /* the pause before a restop           */
    PVOID Context;
} XHCI_TOL_STOP_IO, *PXHCI_TOL_STOP_IO;

/* Issues the first Stop and any restops; *state is the state last read
 * and *restops how many Stops followed the first. */
ULONG XhciTolStopRest(const XHCI_TOL_STOP_IO *io, PULONG state,
                      PULONG restops);

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

/*
 * A thread control transfer past its one deadline, the wait not abandoned
 * (task 43.53; design record 13 section 10.5.1). First, once EP0 has been
 * stopped where it read Running: stopCode the Stop Endpoint's completion
 * code (XHCI_CC_SUCCESS when EP0 needed no stop; 0 when none could be sent
 * or it never completed), epState EP0's state read after it (XHCI_EP_
 * STATE_*), done the transfer's completion read after that. A completion
 * that came before the stop is the answer; a stop that failed, or EP0 in
 * any state but Stopped or Error, keeps the controller reset; otherwise
 * the TD is inert - the controller processes it no more, the scratch is
 * safe for reading the departure, and Set TR Dequeue is legal (Halted
 * would want a Reset Endpoint first, and with no completion seen it is
 * not explained).
 */
#define XHCI_TOL_LATE_RESET         0UL     /* the controller recovery     */
#define XHCI_TOL_LATE_DONE          1UL     /* completed before the stop   */
#define XHCI_TOL_LATE_INERT         2UL     /* stopped: read the departure */
#define XHCI_TOL_LATE_LEFT          3UL     /* gone: its teardown takes it */
#define XHCI_TOL_LATE_CYCLE         4UL     /* leaving: its location cycled */

ULONG XhciTolLateStopped(ULONG stopCode, ULONG epState, ULONG done);

/*
 * Then, the TD inert, hcdCfgFault's order: left the record is gone or no
 * longer its port's; present its path reads present (HcdHubPathPresent,
 * any value when left); hub the record is a hub's; published it has its
 * PDO and its group's serial. Gone: LEFT. Present, or a hub: RESET. A
 * published device: CYCLE. One with no PDO yet: LEFT, its enumeration's
 * failed step tearing it down. Task 50.30 step 3 (design record 17 section
 * 4.15.5): `isolate`, the new handling applies - then gone is LEFT, a hub
 * RESET, and any other device, present or not, CYCLE when published and
 * LEFT when not; with `isolate` clear the rows above.
 */
ULONG XhciTolLateVerdict(ULONG left, ULONG present, ULONG hub,
                         ULONG published, ULONG isolate);

/*
 * Last, a LEFT or CYCLE verdict only: retired the stopped record was taken
 * off EP0 and the controller's dequeue moved past it (Set TR Dequeue), so
 * no later doorbell can run it again. Not retired: RESET, as before. Any
 * other verdict is returned as it is.
 */
ULONG XhciTolLateRetired(ULONG verdict, ULONG retired);

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
 * is one per root port and one per hub port in the extension and the
 * snapshot reader keeps the whole extension in a 256 KB image
 * (xhcitr\tr_snap.c, EXT_IMAGE_MAX). */
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

/*
 * Task 49.35 (design record 17 section 4.6.1): a reset still running when the
 * in-place recovery reaches it. The recovery runs at DISPATCH_LEVEL, where
 * every wait is a 10 ms stall; a controller whose HCRST takes longer (an
 * ASMedia ASM1042 read at more than 11 ms) is waited for by the controller
 * thread at PASSIVE_LEVEL instead, for the start's own bound, through
 * XhciTolResetSettle: io->Read gives USBCMD and USBSTS, io->Sleep sleeps.
 * It reads, then sleeps stepMs and reads again, until HCRST and CNR both
 * read 0 (DONE), a read is all ones (DEAD: a window that stopped decoding
 * proves nothing), or timeoutMs has been slept (TIMEOUT). *waited is the
 * time slept, *usbcmd and *usbsts the last reading. IRQL: the callbacks'.
 */
#define XHCI_TOL_RESET_SETTLE_MS    1000UL  /* the start's HCRST bound       */
#define XHCI_TOL_RESET_STEP_MS      10UL
#define XHCI_TOL_SETTLE_DONE        0UL
#define XHCI_TOL_SETTLE_TIMEOUT     1UL
#define XHCI_TOL_SETTLE_DEAD        2UL

typedef VOID (*XHCI_TOL_RESET_READ)(PVOID context, PULONG usbcmd,
                                    PULONG usbsts);
typedef VOID (*XHCI_TOL_RESET_SLEEP)(PVOID context, ULONG ms);

typedef struct _XHCI_TOL_RESET_IO {
    XHCI_TOL_RESET_READ Read;
    XHCI_TOL_RESET_SLEEP Sleep;
    PVOID Context;
} XHCI_TOL_RESET_IO, *PXHCI_TOL_RESET_IO;

ULONG XhciTolResetSettle(const XHCI_TOL_RESET_IO *io, ULONG timeoutMs,
                         ULONG stepMs, PULONG waited, PULONG usbcmd,
                         PULONG usbsts);

/* 1 when USBCMD and USBSTS say a reset is in progress: HCRST or CNR set on
 * a register that decodes. */
ULONG XhciTolResetRunning(ULONG usbcmd, ULONG usbsts);

/*
 * Task 49.35: the pacing of recoveries after a failed one. A recovery may
 * begin when none has failed since the last success (failures 0), or once
 * XHCI_TOL_RECOVER_GAP_TICKS have passed since the last failure's stamp: at
 * least 500 ms, about 1.2 s at the nominal tick. Until 49.35 the next began
 * on the next controller-thread pass, which a wake makes immediate, so
 * three failures could be spent inside one tick. At every XhciTolerance
 * value: the clock runs at 0 too.
 */
#define XHCI_TOL_RECOVER_GAP_MS     500UL
#define XHCI_TOL_RECOVER_GAP_TICKS  XHCI_TOL_TICKS(XHCI_TOL_RECOVER_GAP_MS)

ULONG XhciTolRecoverPaced(ULONG failures, ULONG failStamp, ULONG now);

/* The terminal reason a dump names (XHCI_TOL_TERMINAL_*): failed is
 * ControllerFailed; unreadable, Unreadable (XHCI_TOL_CONTAINED_*, the
 * unproven-DMA containment its own reason); windowRefused, the window's
 * Refused; failures and maxFailures, RecoveryFailuresConsecutive and its
 * bound. The containment outranks the window, the window the run of
 * failures - each latch stops the next from being reached. A failed
 * controller at none of them still has a recovery owed. */
ULONG XhciTolTerminal(ULONG failed, ULONG unreadable, ULONG windowRefused,
                      ULONG failures, ULONG maxFailures);

/* The same with XhciInterruptMode 4's lost connection as an input (design
 * record 15 section 4.5.4, task 49.24): `msg4Lost` set is MSG4, behind the
 * two containments and ahead of the window - no recovery acts on it, and a
 * containment still names its own. XhciTolTerminal is this with 0. */
ULONG XhciTolTerminalEx(ULONG failed, ULONG unreadable, ULONG windowRefused,
                        ULONG failures, ULONG maxFailures, ULONG msg4Lost);

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

/* The save's publication guard. XhciTolSavePublish: 1 when a TD published
 * now spoils a saved image - the controller is not initialized (suspended,
 * or resuming before Run/Stop), so the TD lands on a ring the image may
 * describe and its doorbell is dropped. XhciTolSaveCommit: 1 when the
 * image may be marked valid, nothing having been published since the
 * gate armed. */
ULONG XhciTolSavePublish(ULONG initialized);
ULONG XhciTolSaveCommit(ULONG published);

/* 1 when a resume must not reinitialize the controller: terminal
 * (XhciTolTerminal's value) is a containment, the window's refusal, the
 * spent run of failures or mode 4's lost message - each holds until a stop
 * and start. A failed
 * controller with a recovery still owed resumes as before. */
ULONG XhciTolResumeRefused(ULONG terminal);

/* The run of failed recoveries (RecoveryFailuresConsecutive, `failures`)
 * after a resume that succeeded (design record 17 section 4.6, revision
 * 37): `reinitialized` 1 when it succeeded through a reinitialization,
 * which cleared ControllerFailed as a recovery that succeeds does, so the
 * run is over and the count 0; else, a restore or nothing done, the count
 * as it was. Left counted, a later fault would get fewer recoveries than
 * the run allows. At every XhciTolerance value. */
ULONG XhciTolResumeFailures(ULONG failures, ULONG reinitialized);

/* The shutdown latch (roadmap-hcd 37.3; design record 13 section 5.8). Not
 * a tolerance behaviour: it applies at every XhciTolerance value.
 *
 * XhciTolShutLatch: 1 when the controller leaving D0 latches, its system
 * state (SYSTEM_POWER_STATE's value) PowerSystemShutdown; S1 to S4 keep
 * the hold until the resume.
 *
 * XhciTolShutFlush: what the power path completes after the halt, a mask
 * of XHCI_TOL_SHUT_*. Nothing unless latched, nor on a contained
 * controller (35-T.6 holds its requests itself). The queued commands hold
 * no DMA and are always taken; the transfers only on positive proof that
 * the controller stopped executing - halted is 1 only when USBSTS, read
 * on a function still in D0, has HCH set and is not all-ones
 * (XhciTolHaltProven); the quiesce's own flags are not that proof, since
 * it also clears them on a configuration space that reads all-ones - and
 * the common buffer is not pinned. */
#define XHCI_TOL_SYSTEM_SHUTDOWN    6UL     /* PowerSystemShutdown          */
#define XHCI_TOL_SHUT_CMDS          1UL     /* SlowIrps and SlowDeferred    */
#define XHCI_TOL_SHUT_XFERS         2UL     /* every pipe's transfers       */

ULONG XhciTolShutLatch(ULONG systemPower);
ULONG XhciTolShutFlush(ULONG latched, ULONG halted, ULONG pinned,
                       ULONG unreadable);

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
/* Record 17 section 4.13.6: the proof a containment or an unproven
 * invalidation takes (XHCI_TOL_PROOF_*). `bme`: configuration space
 * answered and Bus Master Enable read back clear; `asked`: the tunnel's
 * statement was asked, which is done only where `bme` failed; `isolated`:
 * its answer. Pure. */
ULONG XhciTolProof(ULONG bme, ULONG asked, ULONG isolated);

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
    /* Task 49.35 (design record 17 section 4.6.1), words 34 to 45: the
     * recovery's resets waited for at PASSIVE_LEVEL, the longest wait in
     * ms, the waits that ran out, the refusals for a reset still running
     * (not charged), the passes the recovery's pacing deferred, the reruns
     * of one recovery its continuations made; then the
     * escalations a release build did not record until 49.35: Host
     * Controller Events and the last one's completion code, interrupter
     * re-arms that failed, command-ring divergences, fatal Command
     * Completion codes, and interrupt masks or unmasks not proven. At
     * every XhciTolerance value. */
    ULONG ResetSettles;
    ULONG ResetSettleMsMax;
    ULONG ResetUnsettled;
    ULONG ResetBusy;
    ULONG RecoverPaced;
    ULONG RecoverReruns;
    ULONG HcEvents;
    ULONG HcEventCode;
    ULONG RearmFails;
    ULONG CmdDiverged;
    ULONG CmdFatal;
    ULONG MaskFails;
    /* Task 50.30 step 3 (design record 17 sections 4.15.3 and 4.15.4),
     * words 46 and 47: the thread's own reset requests a left command's
     * debt suppressed; the link actions a place held with reason 4 did
     * not take (hcdPortChanged, the hub's port path). */
    ULONG ResetLeftSuppressed;
    ULONG HeldLinkSkips;
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

/*
 * Task 50.30, step 1 (design record 17 section 4.15.8): why each controller
 * reset was requested. Every request names a reason, the place and the slot
 * involved where known, and an extra byte; every request is counted by
 * reason, and the one that marks the controller failed - the one a recovery
 * begins from - is also kept in a ring of the last four, which the snapshot,
 * the recovery-status request and the USB Tree read. Recording only: no
 * decision reads any of it. The codes are a protocol (the snapshot and the
 * recovery-status request carry them), so a code is never reused.
 */
#define XHCI_WHY_NONE               0UL
#define XHCI_WHY_CMD_KEPT           1UL
#define XHCI_WHY_CMD_NO_VERDICT     2UL
#define XHCI_WHY_ENGINE_ABORT_STUCK 3UL
#define XHCI_WHY_ENGINE_DIVERGED    4UL
#define XHCI_WHY_HEALTH             5UL
#define XHCI_WHY_EVENT_RING         6UL
#define XHCI_WHY_INTERRUPTS         7UL
#define XHCI_WHY_POLL_RETIRE        8UL
#define XHCI_WHY_DISABLE_SLOT       9UL
#define XHCI_WHY_EP0_RECOVERY       10UL
#define XHCI_WHY_EP0_LATE           11UL
#define XHCI_WHY_CREATE_LATE        12UL
#define XHCI_WHY_SLOT_ID            13UL
#define XHCI_WHY_PORTSC_DEAD        14UL
#define XHCI_WHY_WARM_RESET         15UL
#define XHCI_WHY_TEARDOWN_STOP      16UL
#define XHCI_WHY_CFG_FAULT          17UL
#define XHCI_WHY_STREAMS            18UL
#define XHCI_WHY_BOT_XFER           19UL
#define XHCI_WHY_RESUME_FAILED      20UL
#define XHCI_WHY_INJECTED           21UL
#define XHCI_WHY_HUB_UNANSWERED     22UL
#define XHCI_WHY_CMD_FATAL          23UL
#define XHCI_WHY_XFER_FATAL         24UL
#define XHCI_WHY_COUNT              25UL    /* codes 0 to 24                 */
#define XHCI_WHY_COUNTERS           26UL    /* an even number of ULONGs      */
#define XHCI_WHY_RING               4UL

/* One reset request that began a recovery. Eight ULONGs. */
typedef struct _XHCI_RESET_REC {
    ULONG Reason;           /* XHCI_WHY_*                                  */
    ULONG Extra;            /* the reason's own byte (TRB type, code, ...) */
    ULONG Slot;             /* 0: none named                               */
    ULONG RootPort;         /* 0: none named                               */
    ULONG Route;            /* the Route String below the root port        */
    ULONG VidPid;           /* (idVendor << 16) | idProduct, 0 not read    */
    ULONG Clock;            /* the tolerance clock at the request          */
    ULONG Recoveries;       /* recoveries begun before it, this lifetime   */
} XHCI_RESET_REC, *PXHCI_RESET_REC;

/*
 * The reasons' state, appended to the extension rather than put in
 * XHCI_TOL_STATE, so no offset of the extension the harness reads moves;
 * the snapshot's HCD header gives its place and size. Counts[] by reason;
 * Rec[] the ring, Next the slot the next record takes, Kept how many are
 * valid; Latched 1 + the ring slot of the record named at the latch, 0
 * none; Staged and StagedExtra the reason an engine escalation staged for
 * the drain's one request (XhciTolWhyStage), StagedLost the escalations
 * of the same drain that found one staged already; StagedSlot to
 * StagedVidPid the device the staging site proved (its slot, root port,
 * route and VID:PID, copied under the lock when it staged, all 0 when it
 * proved none), appended after Rec[] so no earlier field moves. An even
 * number of ULONGs.
 *
 * Task 50.30 step 3 (design record 17 section 4.15.6, "The spread of
 * failures, recorded only"): every device failed alone (HcdEnumFailAlone)
 * counted in SpreadCount and kept in a ring of the last
 * XHCI_TOL_SPREAD_RING, SpreadNext the next record written - its place
 * (the port object's number: a root port's, or a hub port's above the
 * root ports), the hub it hangs from (root port in 31:24, the Route String
 * in 19:0, 0 for a root port), its VID:PID where read (0 unknown) and the
 * tolerance clock. Appended after StagedVidPid, so no earlier field moves:
 * a reader that knows the earlier size reads what it knew. Nothing decides
 * anything from it. Eighteen ULONGs, so the count stays even. After them,
 * XhciDeviceIsolation as the start read and decided it (IsolationValue,
 * IsolationState: XhciTolIsolationState below), two ULONGs.
 */
#define XHCI_TOL_SPREAD_RING        4UL

typedef struct _XHCI_TOL_SPREAD_REC {
    ULONG Place;
    ULONG Hub;
    ULONG VidPid;
    ULONG Clock;
} XHCI_TOL_SPREAD_REC, *PXHCI_TOL_SPREAD_REC;

typedef struct _XHCI_TOL_WHY {
    ULONG Counts[XHCI_WHY_COUNTERS];
    ULONG Next;
    ULONG Kept;
    ULONG Latched;
    ULONG Staged;
    ULONG StagedExtra;
    ULONG StagedLost;
    XHCI_RESET_REC Rec[XHCI_WHY_RING];
    ULONG StagedSlot;
    ULONG StagedPort;
    ULONG StagedRoute;
    ULONG StagedVidPid;
    ULONG SpreadCount;
    ULONG SpreadNext;
    XHCI_TOL_SPREAD_REC Spread[XHCI_TOL_SPREAD_RING];
    ULONG IsolationValue;
    ULONG IsolationState;
} XHCI_TOL_WHY, *PXHCI_TOL_WHY;

/* Every field cleared: a start. Pure. */
VOID XhciTolWhyInit(PXHCI_TOL_WHY w);

/* Task 50.30 step 3 (section 4.15.6): one device failed alone recorded in
 * the spread ring and counted; returns the count, for the note tol.spread
 * (place in 31:16, the count in 15:0). Recording only. Pure; the caller
 * holds the controller lock. */
ULONG XhciTolSpreadAdd(PXHCI_TOL_WHY w, ULONG place, ULONG hub, ULONG vidPid,
                       ULONG clock);

/* One request counted; when `begins` (it marked the controller failed),
 * `rec` is also kept in the ring. Returns the note value
 * (XhciTolWhyNote). Pure; the caller holds the controller lock. */
ULONG XhciTolWhyRecord(PXHCI_TOL_WHY w, const XHCI_RESET_REC *rec,
                       ULONG begins);

/* The note form, `ctrl.failed.why` and `ctrl.latched.cause`: reason in
 * 31:24, root port in 23:16, slot in 15:8, extra in 7:0. Pure. */
ULONG XhciTolWhyNote(const XHCI_RESET_REC *rec);

/* An engine escalation's reason, staged under the lock for the drain's
 * request; the first staged stays. `place`, when not NULL, is the device
 * the staging site has proved - an event matched to a queued TD of that
 * device's pipe, or to the outstanding command - and only its Slot,
 * RootPort, Route and VidPid are read; NULL for an event no record owns,
 * whose Slot ID may have been recycled, so nothing is inferred from it.
 * Pure. */
VOID XhciTolWhyStage(PXHCI_TOL_WHY w, ULONG reason, ULONG extra,
                     const XHCI_RESET_REC *place);

/* A transfer engine's verdict on one event, staged: nothing when `fatal` is
 * 0; otherwise XHCI_WHY_XFER_FATAL with the event's `code`, naming `place`
 * only when `matched` - the engine resolved the event onto a TD queued on
 * that device's pipe (XHCI_XFER_EVENT_RESULT.Matched). A fatal code on an
 * event inside no TD still escalates, but names no device, whatever pipe
 * its Slot ID and Endpoint ID reach: the slot may have been recycled.
 * Returns `fatal`. Pure; the caller holds the controller lock. */
ULONG XhciTolWhyStageXfer(PXHCI_TOL_WHY w, ULONG fatal, ULONG matched,
                          ULONG code, const XHCI_RESET_REC *place);

/* The staged reason taken and cleared; XHCI_WHY_EVENT_RING with extra 0
 * when none was staged. `place`, when not NULL, receives the staged
 * device's Slot, RootPort, Route and VidPid (0 when none was proved) and
 * every other field 0. Pure. */
ULONG XhciTolWhyTake(PXHCI_TOL_WHY w, PULONG extra, PXHCI_RESET_REC place);

/* The latch named: the newest kept record marked as the latch's, copied
 * to `rec`. Returns 0 when the ring is empty. Pure. */
ULONG XhciTolWhyLatch(PXHCI_TOL_WHY w, PXHCI_RESET_REC rec);

/* How many kept records name the same place (root port and route, root
 * port not 0) as the newest; 0 when the newest names none. Pure. */
ULONG XhciTolWhyInvolved(const XHCI_TOL_WHY *w);

/* Ring slot of the i-th record, newest first (i < Kept). Pure. */
ULONG XhciTolWhyAt(const XHCI_TOL_WHY *w, ULONG i);

/*
 * Task 50.30 step 3 (design record 17 section 4.15.3, revision 23): a left
 * command's debt and its settlement, the decisions alone. The controller's
 * `CmdLeftOwed` is CLEAR, OWED (the left command may still execute:
 * nothing is submitted, no input context is built) or SETTLING (the engine
 * ended it and the thread's pass took the settlement: ordinary admission
 * still closed, the settlement's own entry admits only the commands on its
 * list).
 */
#define XHCI_LEFT_CLEAR             0UL
#define XHCI_LEFT_OWED              1UL
#define XHCI_LEFT_SETTLING          2UL

/* Whether a submission may go out under the debt `state`. `settle`: made
 * through the settlement's own entry (hcdCmdSettle); `onList`: the command
 * is the next on the settlement's list. An ordinary submission only with
 * no debt; the settlement's only in SETTLING and only for its list. Pure. */
ULONG XhciTolLeftAdmit(ULONG state, ULONG settle, ULONG onList);

/* Whether an input context may be built: only with no debt. Pure. */
ULONG XhciTolLeftInputOk(ULONG state);

/*
 * The returned Slot ID's checks, in the record's order, before a left
 * Enable Slot's cleanup Disable Slot: the completion matched to the left
 * command by address and generation; its code Success; the Slot ID in 1
 * to `maxSlotsEn` (CONFIG.MaxSlotsEn); no record owns it (`owned`: a
 * device, a quarantined or an owed record names it, or SlotDevice[] holds
 * one); its DCBAA entry zero (both words); not on the settlement's list
 * already. Returns XHCI_LEFT_SLOT_OK, or the first check that fails - the
 * value `cmd.left.slot` notes in 7:0. Pure.
 */
#define XHCI_LEFT_SLOT_OK           0UL
#define XHCI_LEFT_SLOT_UNMATCHED    1UL
#define XHCI_LEFT_SLOT_CODE         2UL
#define XHCI_LEFT_SLOT_RANGE        3UL
#define XHCI_LEFT_SLOT_OWNED        4UL
#define XHCI_LEFT_SLOT_DCBAA        5UL
#define XHCI_LEFT_SLOT_LISTED       6UL
ULONG XhciTolLeftSlotCheck(ULONG matched, ULONG code, ULONG slot,
                           ULONG maxSlotsEn, ULONG owned, ULONG dcbaaLo,
                           ULONG dcbaaHi, ULONG listed);

/*
 * How a left command that names no device settles, from its end as the
 * thread's pass reads it: `trbType` the left command's type; `ended` 1
 * when a completion matched to it was recorded under the lock, 0 for a
 * clean abort, a loss, an invalidation before any completion, a suspend's
 * abandonment or a completion the engine could not match; `code` that
 * completion's code; `slotCheck` XhciTolLeftSlotCheck's answer for its
 * Slot ID (read only for an Enable Slot's success). CLEAR: nothing is
 * owned, the debt clears; DISABLE: the returned slot's Disable Slot goes
 * through the settlement's entry before the debt clears; RESET: the
 * reset (CMD_KEPT) is requested as the debt clears and Enable Slot is held
 * until an HCRST or a start. A No Op owns nothing; any other type naming
 * no device is reset, as an Enable Slot with no trusted end is. Pure.
 */
#define XHCI_LEFT_END_CLEAR         0UL
#define XHCI_LEFT_END_DISABLE       1UL
#define XHCI_LEFT_END_RESET         2UL
ULONG XhciTolLeftSlotlessEnd(ULONG trbType, ULONG ended, ULONG code,
                             ULONG slotCheck);

/* The debt after a successful Restore State: cleared for a left command
 * that names a device (its CmdLeft mark carries the rest), kept as
 * SETTLING for one that names none, since the restore rebuilds the command
 * ring but disables no slot. A reinitialization clears every debt instead
 * (no function: it is an HCRST). Pure. */
ULONG XhciTolLeftAfterRestore(ULONG state, ULONG namesDevice);

/* Whether `reason` (XHCI_WHY_*) is one of the thread's own reset sites,
 * which a left command's debt suppresses (section 4.15.8's table, the rows
 * marked "thread"); the engine's, the health poll's and the interrupt
 * paths' report the controller and are never suppressed. Pure. */
ULONG XhciTolWhyThread(ULONG reason);

/*
 * Task 50.30 step 3 (section 4.15.3, revision 23): the debt itself
 * and its transitions, DDK-free so that the host suite drives the code the
 * controller thread runs. The controller holds one (HCD_CONTROLLER.Left),
 * read and written under its lock. Owed: XHCI_LEFT_*. Type, Slot, PA,
 * Gen: the left command's TRB type, Slot ID, TRB and the engine's command
 * generation at its submit. Device: it names a device record, whose mark
 * carries the rest. End: how the engine ended it (XHCI_LEFT_HOW_*), with a
 * matched completion's Code and SlotId. Listed: the settlement's list
 * written; SlotlessDone: a slotless end decided. Reset, ResetSlot,
 * ResetExtra: the reason requested as the debt clears (0 none).
 * Interrupted, InterruptedType, InterruptedSlot: a settlement command a
 * route's bit interrupted (the first one's type and slot), which fails
 * the settlement once every route's bit is clear (XhciLeftDebtPass), or
 * the stop's teardown or an HCRST settles first.
 * Overflow: an entry the list could not hold. List[Head..Count): what the
 * settlement's entry alone may submit. EnableHeld: Enable Slot refused
 * until an HCRST or a start. Deferred, DeferredExtra, DeferredAt: the
 * first of the thread's own reset requests made while the debt stood
 * (XhciLeftDebtDefer) - its reason, extra and place, read when it was
 * made - kept until the pass requests it once the debt has cleared, or an
 * HCRST makes it unnecessary; DeferredCount the requests deferred.
 */
#define XHCI_LEFT_HOW_NONE          0UL
#define XHCI_LEFT_HOW_DONE          1UL     /* its completion, matched       */
#define XHCI_LEFT_HOW_LOST          2UL     /* aborted, lost or invalidated  */
#define XHCI_LEFT_ENTRY_SLOT        1UL     /* a left Enable Slot's slot     */
#define XHCI_LEFT_ENTRY_RECORD      2UL     /* a record quarantined as owed  */
/* XHCI_MAX_SLOTS + 1: every record and one slotless entry (xhci_tol.c
 * holds the relation at compile time). */
#define XHCI_LEFT_LIST_MAX          33UL

typedef struct _XHCI_LEFT_ENTRY {
    ULONG Kind;
    ULONG Slot;
} XHCI_LEFT_ENTRY, *PXHCI_LEFT_ENTRY;

typedef struct _XHCI_LEFT_DEBT {
    ULONG Owed;
    ULONG Type;
    ULONG Slot;
    ULONG PA;
    ULONG Gen;
    ULONG Device;
    ULONG End;
    ULONG Code;
    ULONG SlotId;
    ULONG Listed;
    ULONG SlotlessDone;
    ULONG Reset;
    ULONG ResetSlot;
    ULONG ResetExtra;
    ULONG Interrupted;
    ULONG InterruptedType;
    ULONG InterruptedSlot;
    ULONG Overflow;
    ULONG Head;
    ULONG Count;
    XHCI_LEFT_ENTRY List[XHCI_LEFT_LIST_MAX];
    ULONG EnableHeld;
    ULONG Deferred;
    ULONG DeferredExtra;
    ULONG DeferredCount;
    XHCI_RESET_REC DeferredAt;
} XHCI_LEFT_DEBT, *PXHCI_LEFT_DEBT;

/* The debt cleared with everything that went with it; EnableHeld and the
 * deferred reset kept (XhciLeftDebtClear). Everything, both of those too:
 * an HCRST or a start (XhciLeftDebtHcrst). Pure. */
VOID XhciLeftDebtClear(PXHCI_LEFT_DEBT d);
VOID XhciLeftDebtHcrst(PXHCI_LEFT_DEBT d);

/*
 * A reset request of the thread's own (XhciTolWhyThread) made while a debt
 * stands is deferred, never discarded: a mandatory reset - a Stop Endpoint
 * that failed or was left, a Disable Slot that failed, a tainted scratch -
 * is still owed when the debt clears. The first one's `reason`, `extra`
 * and `place` (its Slot, RootPort, Route and VidPid, read by the caller
 * while the slot's record was valid) are kept, later ones counted. Returns
 * 1 when deferred (the caller requests nothing now), 0 when the request
 * goes ahead: no debt, or a reason that reports the controller.
 * XhciLeftDebtTakeDeferred: the deferred reason taken and cleared (0
 * none), its extra and place copied out. Pure; the caller holds the
 * controller lock.
 */
ULONG XhciLeftDebtDefer(PXHCI_LEFT_DEBT d, ULONG reason, ULONG extra,
                        const XHCI_RESET_REC *place);
ULONG XhciLeftDebtTakeDeferred(PXHCI_LEFT_DEBT d, PULONG extra,
                               PXHCI_RESET_REC place);

/* Task 50.30 step 3 (section 4.15.5a, "Before the Disable Slot, every
 * endpoint that may be active"): whether a teardown stops an endpoint
 * before the Disable Slot - one an open pipe names with work on its ring
 * (`queued`), or one the device's retained objects name (`retained`: the
 * rings kept after an aborted Configure Endpoint or replay, a retired
 * stream block's endpoint), that reads Running (`running`). The ordinary
 * teardown and the settlement's share it. Pure. */
ULONG XhciTolTeardownStops(ULONG queued, ULONG retained, ULONG running);

/* Task 50.30 step 3 (section 4.15.5a, the second HcdThreadReaddress
 * row): the slot whose input context is held (InputHeldSlot, 0 none)
 * after a Disable Slot of `disabled` the controller confirmed - released
 * when it names that slot, whoever issued the command (the ordinary
 * teardown or the settlement), otherwise kept. Pure. */
ULONG XhciTolInputHeldAfter(ULONG held, ULONG disabled);

/* Whether a left command that names a device owes the reset whatever its
 * end: a Stop Endpoint (section 4.15.3's table: an aborted Stop leaves the
 * endpoint as it was, and no teardown can follow it soundly). The list's
 * writing defers it (CMD_KEPT, the TRB type) beside whatever its caller
 * requested. A left Disable Slot's record is quarantined as owed and
 * disabled by the settlement instead. Pure. */
ULONG XhciTolLeftNamedReset(ULONG trbType);

/*
 * A route's bit ended a command's wait. An ordinary command's (`settle` 0
 * and no debt standing) takes the debt OWED with its fields (`lost`: its
 * loss already came, so the engine has ended it). A settlement command's
 * (`settle` 1), or any leave while a debt stands, never touches the debt
 * it settles: Interrupted is set, with the type and slot of the first
 * command it interrupted, and the state, the left command's fields and
 * the list are kept. An ordinary Stop Endpoint that names a device
 * (XhciTolLeftNamedReset) owes the reset whatever its end, so taking
 * the debt also defers it (CMD_KEPT, the TRB type, `place` the device's,
 * read by the caller under the lock): recorded at the leave, never only
 * at the settlement's listing, which a successful Restore State that
 * clears the debt would come before. Returns
 * XHCI_LEFT_TOOK_OWED or XHCI_LEFT_TOOK_INTERRUPTED. Pure.
 */
#define XHCI_LEFT_TOOK_OWED         1UL
#define XHCI_LEFT_TOOK_INTERRUPTED  2UL
ULONG XhciLeftDebtLeave(PXHCI_LEFT_DEBT d, ULONG settle, ULONG type,
                        ULONG slot, ULONG pa, ULONG gen, ULONG device,
                        ULONG lost, const XHCI_RESET_REC *place);

/* An entry put on the list. On a full list nothing is dropped silently:
 * Overflow is set and the reset owed (CMD_KEPT), which the pass turns into
 * a failed settlement. Returns 1 added, 0 not. XhciLeftDebtListed: whether
 * `slot` is on the unsettled part of the list. Pure. */
ULONG XhciLeftDebtAdd(PXHCI_LEFT_DEBT d, ULONG kind, ULONG slot);
/*
 * Task 50.30 step 3 (reviewed and revised): the owed records put on the
 * list child before parent, as the ordinary teardown disables them and
 * design record 13 requires - a hub's Disable Slot never before those of
 * the devices whose slots name it as parent or TT hub. `tier[slot]`, for
 * slots 1 to XHCI_MAX_SLOTS, is the record's own tier (the hubs above it,
 * kept in the device record, so it survives the hub objects' detachment),
 * XHCI_LEFT_TIER_NONE for a slot with no owed record: the deepest tier
 * first, ascending slot within a tier. Returns how many entries the list
 * could not hold (XhciLeftDebtAdd's overflow). Pure.
 */
#define XHCI_LEFT_TIER_NONE         0xFFFFFFFFUL
ULONG XhciLeftDebtAddRecords(PXHCI_LEFT_DEBT d, const ULONG *tier);
ULONG XhciLeftDebtListed(const XHCI_LEFT_DEBT *d, ULONG slot);

/*
 * The settlement pass's next action, read under the lock (`routes`: a
 * route's bit raised; `genMoved`: the command generation is not the left
 * command's). NONE: nothing to do now (no debt, a route's bit, or OWED with
 * the left command not ended). TAKE: OWED and ended - SETTLING from here,
 * the list written in the same hold. FAIL: a settlement command a
 * route's bit interrupted, once every bit is clear (revision 24: it is a
 * failed settlement command, never issued again), or a list that
 * overflowed - the settlement fails (XhciLeftDebtFail). CLEAR: the list
 * is empty - the debt
 * clears, the reset owed requested after it. NEXT: List[Head] goes out.
 * XhciLeftDebtTake: OWED to SETTLING, End LOST where the generation moved
 * with no end recorded. Pure.
 */
#define XHCI_LEFT_PASS_NONE         0UL
#define XHCI_LEFT_PASS_TAKE         1UL
#define XHCI_LEFT_PASS_FAIL         2UL
#define XHCI_LEFT_PASS_CLEAR        3UL
#define XHCI_LEFT_PASS_NEXT         4UL
#define XHCI_LEFT_PASS_TAINTED      5UL
ULONG XhciLeftDebtPass(const XHCI_LEFT_DEBT *d, ULONG routes, ULONG genMoved);
VOID XhciLeftDebtTake(PXHCI_LEFT_DEBT d);

/*
 * The settlement pass's action with the controller's own state: `failed`
 * (ControllerFailed: the recovery's HCRST settles everything) - NONE;
 * `tainted` (ScratchTainted: no hardware work may go out until the HCRST,
 * so no settlement command can) - an entry due to go out (NEXT) is
 * TAINTED instead, which fails the settlement with no command issued
 * (XhciLeftDebtFail), so the pass still reaches CLEAR and requests the
 * reset it owes. Otherwise XhciLeftDebtPass's. With no debt and a
 * deferred reset kept (a successful Restore State cleared the debt), CLEAR
 * once no route's bit is raised: the reset requested, nothing settled. An
 * interrupted settlement is FAIL once the bits are clear, tainted or not
 * (XhciLeftDebtPass). Pure.
 */
ULONG XhciLeftDebtStep(const XHCI_LEFT_DEBT *d, ULONG routes, ULONG genMoved,
                       ULONG failed, ULONG tainted);

/*
 * The settlement fails (an entry's command failed, an overflow, an
 * interrupted settlement command): the first reason is kept (`why`,
 * `slot`, `extra` where none is owed yet; where none is given, CMD_KEPT
 * with the interrupted command's slot and TRB type for an interruption,
 * with `slot` and `extra` otherwise), the interruption ended and the list
 * emptied (Head = Count). Its caller (hcdLeftService, every failure: an
 * entry's command, an interruption, an overflow, a tainted scratch)
 * converts every record still owed to an ordinary quarantine, retained
 * until the HCRST, and clears the debt with EnableHeld set
 * (XhciLeftDebtSettleClear) in the same hold, so no failed settlement's
 * reset waits in a standing debt. No command is retried and nothing is
 * freed. Pure.
 */
VOID XhciLeftDebtFail(PXHCI_LEFT_DEBT d, ULONG why, ULONG slot, ULONG extra);

/*
 * The debt's clearing as the pass makes it, under the lock: with a debt
 * standing, the reset it owes (Reset, ResetSlot, ResetExtra; 0 none)
 * copied out, the debt cleared (XhciLeftDebtClear: EnableHeld and the
 * deferred reset kept) and EnableHeld set when a reset is owed. Returns 1
 * when a debt was cleared, 0 with no debt (the outputs 0).
 * XhciLeftDebtFailClear: a failed settlement's whole transition -
 * XhciLeftDebtFail, then this - which hcdLeftService makes in one hold
 * with the owed records' conversion. Pure.
 */
ULONG XhciLeftDebtSettleClear(PXHCI_LEFT_DEBT d, PULONG why, PULONG slot,
                              PULONG extra);
ULONG XhciLeftDebtFailClear(PXHCI_LEFT_DEBT d, ULONG failWhy,
                            ULONG failSlot, ULONG failExtra, PULONG why,
                            PULONG slot, PULONG extra);

/*
 * The debt after a successful Restore State: cleared for a left command
 * that names a device (its CmdLeft mark carries the rest); otherwise kept
 * SETTLING, its end LOST where none was recorded, and its list written
 * again at the thread's next pass - an owed record listed from its mark,
 * and an unsettled slotless entry checked again (MaxSlotsEn read again,
 * revision 23), so SlotlessDone is reopened for one still on the list. An
 * interruption stays, and keeps the debt SETTLING even for a left command
 * that names a device, so the pass fails the settlement it interrupted
 * (revision 24) rather than leaving its owed records unsettled; and so
 * does a record the debt's teardown quarantined as owed (`owedRecords`,
 * how many records carry OwedQuarantine), which only the settlement
 * stops and disables - cleared, the debt would leave it awaiting a reset
 * nobody owes; and so does a debt with a reset already owed (Reset, or
 * an overflowed list), so that the pass, not the restore, clears it and
 * requests that reset. Returns the state before. Pure.
 */
ULONG XhciLeftDebtRestored(PXHCI_LEFT_DEBT d, ULONG owedRecords);

/*
 * Task 50.30 step 3: the engine's clean end of an abort as the waiter's
 * mark (HCD_CONTROLLER.CmdAbort, under the controller lock): the TRB it
 * abandoned, its command generation, how (1 a Command Aborted event came,
 * 2 only the stop event). Cleared when an idle engine is armed for a
 * submission and at every HCRST, a new lifetime's included - the engine's
 * generation starts again at 0 there, so an old mark could otherwise match
 * a new lifetime's command at the same TRB. XhciCmdAbortMatches: a mark set
 * for this TRB in this generation. Pure.
 */
typedef struct _XHCI_CMD_ABORT_MARK {
    ULONG Set;
    ULONG PA;
    ULONG Gen;
    ULONG How;
} XHCI_CMD_ABORT_MARK, *PXHCI_CMD_ABORT_MARK;
VOID XhciCmdAbortClear(PXHCI_CMD_ABORT_MARK m);
VOID XhciCmdAbortSet(PXHCI_CMD_ABORT_MARK m, ULONG pa, ULONG gen, ULONG how);
ULONG XhciCmdAbortMatches(const XHCI_CMD_ABORT_MARK *m, ULONG pa, ULONG gen);

/*
 * Task 50.30 step 3 (design record 17 section 4.15.3, "Which commands keep
 * the reset"): whether a command of TRB type `trbType` aborted cleanly still
 * requires the controller reset. Address Device, Evaluate Context,
 * Configure Endpoint, Reset Device, Reset Endpoint and Set TR Dequeue
 * Pointer do not (the device is failed alone, section 4.15.5a); Stop
 * Endpoint, Enable Slot, Disable Slot, No Op and every command naming no
 * slot do. Pure.
 */
ULONG XhciTolAbortKeepsReset(ULONG trbType);

/*
 * The thread's command wait under the new handling (section 4.15.3): the
 * verdict of one look, in the record's order. `own` the command's own
 * completion arrived (CmdDonePA its TRB); `aborted` the engine's clean
 * abort named its TRB in its generation (CmdAbortPA); `failed` the
 * controller failed (the engine's or another site's reset); `moved` the
 * command generation moved (an invalidation); `leaving` a route's bit is
 * raised (CmdWaitLeave); `expired` the verdict bound passed. Returns
 * XHCI_WAIT_* (WAIT: look again after a slice). Pure.
 */
#define XHCI_WAIT_WAIT              0UL
#define XHCI_WAIT_OWN               1UL     /* its completion code           */
#define XHCI_WAIT_ABORTED           2UL     /* 25                            */
#define XHCI_WAIT_FAILED            3UL     /* 0, nothing requested          */
#define XHCI_WAIT_LOST              4UL     /* 0, nothing requested          */
#define XHCI_WAIT_LEFT              5UL     /* HCD_CMD_LEFT, the debt OWED   */
#define XHCI_WAIT_NO_VERDICT        6UL     /* 0, the reset CMD_NO_VERDICT   */
ULONG XhciTolWaitVerdict(ULONG own, ULONG aborted, ULONG failed, ULONG moved,
                         ULONG leaving, ULONG expired);

/*
 * Task 50.30 step 3 (design record 17 section 4.15.11): XhciDeviceIsolation.
 * The mode read at a start: 0 today's handling, 1 (absent or any value but
 * 0 and 2) the handling on listed controllers only, 2 on every controller.
 * XhciIsolationListed(vendorDevice): the table of controllers on which
 * mode 1 applies the handling (the PCI ID register as it reads, device
 * << 16 | vendor: 8086:1E31 is 0x1E318086): 8086:1E31 alone, listed by
 * the owner on 2026-11-20 without the reading on real hardware section
 * 4.15.10 asks for. Applies: mode 2, or mode 1 and listed.
 * Pure.
 */
#define XHCI_ISOLATION_OFF          0UL
#define XHCI_ISOLATION_LISTED       1UL
#define XHCI_ISOLATION_ALL          2UL
ULONG XhciTolIsolationMode(ULONG found, ULONG value);
ULONG XhciIsolationListed(ULONG vendorDevice);
ULONG XhciTolIsolationApplies(ULONG mode, ULONG listed);
/*
 * Task 50.30 step 3 (section 4.15.11, "What a report shows"): what the
 * start read and decided, kept in XHCI_TOL_WHY after the spread records
 * (two ULONGs, 88 in all) for the snapshot and the recovery-status
 * request. IsolationValue: the DWORD read, XHCI_TOL_ISO_NONE when absent
 * or of another type. IsolationState (XhciTolIsolationState): the mode in
 * 15:8, listed in bit 1, applies in bit 0. XhciTolIsolationNote: the note
 * tol.isolate - the value read in 15:8 (0xFF when absent, another type or
 * above 0xFE), listed in bit 1, applies in bit 0. XhciQemuIsolationListed:
 * the qemu flavour's table, the emulated controller (section 4.15.9); the
 * release flavour never reads it. Pure.
 */
#define XHCI_TOL_ISO_NONE           0xFFFFFFFFUL
#define XHCI_TOL_ISO_MODE_SHIFT     8
ULONG XhciTolIsolationState(ULONG mode, ULONG listed, ULONG applies);
ULONG XhciTolIsolationNote(ULONG value, ULONG listed, ULONG applies);
ULONG XhciQemuIsolationListed(ULONG vendorDevice);

#endif /* XHCI_TOL_H */
