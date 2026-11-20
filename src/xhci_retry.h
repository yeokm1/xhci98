/*
 * xhci_retry.h - a controller stopped after repeated failures, tried again:
 * the pure half (roadmap-usb4.md task 50.31, design record 38).
 *
 * The value's reading, the waits on the tolerance clock and their backing
 * off, the clean run that ends it, the machine from a latched controller
 * (terminals 2 and 3) to a try and its burst, the root ports' episodes and
 * the storm limit, the admission's rows, the leave flag's table, the end of
 * a latched resume's FAILED phase, the status words and the snapshot
 * block's shape. The executors are hcd_ctl.c, hcd_power.c and hcd_pnp.c.
 *
 * DDK-free: part of the pure core. C89. IRQL: any; nothing here takes a lock
 * or touches a register - the caller holds whatever lock the words it passes
 * belong to (record 38 section 14).
 */

#ifndef XHCI_RETRY_H
#define XHCI_RETRY_H

#include "xhci_compat.h"
#include "xhci_tol.h"

/* XhciFailedRetrySec (record 38 section 4): 0 off; 2 to 600 the first
 * wait in seconds; anything else, absent included, 5. How it was found
 * (XHCI_TOL_RETRY.Found): absent, a REG_DWORD, or another type. */
#define XHCI_TOL_RETRY_ABSENT      0UL
#define XHCI_TOL_RETRY_DWORD       1UL
#define XHCI_TOL_RETRY_OTHER_TYPE  2UL
#define XHCI_TOL_RETRY_DEFAULT_S    5UL
#define XHCI_TOL_RETRY_MIN_S        2UL
#define XHCI_TOL_RETRY_MAX_S        600UL

/* The waits in seconds (record 38 section 5.7, the owner's decision of
 * 2026-11-19): the retry's own intervals count a tick at the timer's
 * nominal 100 ms, not at the tolerance clock's 45 ms credit, and the
 * waits are floored on the system time in units of 2^20 x 100 ns
 * (104.8576 ms): T ms is ceil(T * 625 / 65536) + 1 units, T * 625 at most
 * 375 000 000 for T up to ten minutes. The recovery window keeps record
 * 17's credit. */
#define XHCI_TOL_RETRY_TICK_MS      100UL
#define XHCI_TOL_RETRY_TICKS(ms) \
    ((((ms) + XHCI_TOL_RETRY_TICK_MS - 1UL) / XHCI_TOL_RETRY_TICK_MS) + 1UL)
#define XHCI_TOL_RETRY_UNITS(ms)    ((((ms) * 625UL) + 65535UL) / 65536UL + 1UL)

/* The intervals (record 38 sections 5 and 6), in ticks of the tolerance
 * clock: the waits' ceiling, the clean run and the storm limit's window
 * are ten minutes at the nominal tick (section 5.7); the continuity gap is
 * a starved thread's, at record 17's credit. */
#define XHCI_TOL_RETRY_CEILING_MS   600000UL
#define XHCI_TOL_RETRY_CEILING_TICKS \
    XHCI_TOL_RETRY_TICKS(XHCI_TOL_RETRY_CEILING_MS)
#define XHCI_TOL_RETRY_CLEAN_TICKS  XHCI_TOL_RETRY_CEILING_TICKS
#define XHCI_TOL_RETRY_STORM_TICKS  XHCI_TOL_RETRY_CEILING_TICKS
#define XHCI_TOL_RETRY_GAP_MS       500UL
#define XHCI_TOL_RETRY_GAP_TICKS    XHCI_TOL_TICKS(XHCI_TOL_RETRY_GAP_MS)
#define XHCI_TOL_RETRY_STORM_MAX    3UL

/* The one saturation rule (record 38 section 15.3): Tries is held here so
 * that the next try's number fits 16 bits; every other counter is held at
 * 0xFFFFFFFF. */
#define XHCI_TOL_RETRY_TRIES_MAX    65534UL

/* The qemu flavour's XhciQemuRetryScale (record 38 section 17): 0 and 1
 * none, held to 600. */
#define XHCI_TOL_RETRY_SCALE_MAX    600UL

/* The machine's states (record 38 section 3). */
#define XHCI_RETRY_OFF              0UL
#define XHCI_RETRY_WATCHING         1UL
#define XHCI_RETRY_LATCHED          2UL
#define XHCI_RETRY_DUE              3UL
#define XHCI_RETRY_TRYING           4UL
#define XHCI_RETRY_EXCLUDED         5UL

/* What brought a try (ctrl.retry.why). */
#define XHCI_RETRY_CAUSE_NONE       0UL
#define XHCI_RETRY_CAUSE_WAIT       1UL
#define XHCI_RETRY_CAUSE_PORT       2UL

/* How a burst ended (ctrl.retry.burst, 31:16). */
#define XHCI_RETRY_BURST_NONE       0UL
#define XHCI_RETRY_BURST_RECOVERED  1UL
#define XHCI_RETRY_BURST_LATCHED    2UL
#define XHCI_RETRY_BURST_EXCLUDED   3UL
#define XHCI_RETRY_BURST_ENDED      4UL

/* Why a committed decision was a skip (ctrl.retry.skip). */
#define XHCI_RETRY_SKIP_ONES        1UL
#define XHCI_RETRY_SKIP_LAYOUT      2UL

/* What XhciTolRetryObserve did. */
#define XHCI_RETRY_EV_NONE          0UL
#define XHCI_RETRY_EV_LATCHED       1UL
#define XHCI_RETRY_EV_EXCLUDED      2UL

/* The admission's answer (record 38 sections 7.1 and 7.2). */
#define XHCI_RETRY_ADMIT_OK         0UL
#define XHCI_RETRY_ADMIT_OFF        1UL
#define XHCI_RETRY_ADMIT_EXCLUDE    2UL     /* ends only at a start         */
#define XHCI_RETRY_ADMIT_NOT_LATCHED 3UL
#define XHCI_RETRY_ADMIT_WAIT_LEAVE 4UL
#define XHCI_RETRY_ADMIT_WAIT_POWER 5UL
#define XHCI_RETRY_ADMIT_WAIT_RELEASE 6UL

/* Once-per-episode notes (XHCI_TOL_RETRY.Noted), cleared at each latch. */
#define XHCI_RETRY_NOTE_STORM       0x01UL
#define XHCI_RETRY_NOTE_SAMPLE_OFF  0x02UL
#define XHCI_RETRY_NOTE_EXCLUDED    0x04UL

/* ReleaseState's bits (record 38 section 15.3). */
#define XHCI_RETRY_REL_INVALIDATED  0x01UL
#define XHCI_RETRY_REL_UNPROVEN     0x02UL
#define XHCI_RETRY_REL_PROVED       0x04UL
#define XHCI_RETRY_REL_TERMINAL     0x08UL
#define XHCI_RETRY_REL_DEVICES      0x10UL
#define XHCI_RETRY_REL_PINNED       0x20UL

/* The leave flag's table (record 38 section 7.5): the path, the phase, the
 * outcome. The paths are this file's own codes, not the IRP minors, so the
 * table stays DDK-free; hcd_pnp.c maps each minor. */
#define XHCI_RETRY_PNP_QUERY_STOP   1UL
#define XHCI_RETRY_PNP_QUERY_REMOVE 2UL
#define XHCI_RETRY_PNP_CANCEL_STOP  3UL
#define XHCI_RETRY_PNP_CANCEL_REMOVE 4UL
#define XHCI_RETRY_PNP_STOP         5UL
#define XHCI_RETRY_PNP_SURPRISE     6UL
#define XHCI_RETRY_PNP_REMOVE       7UL
#define XHCI_RETRY_PNP_START_LIVE   8UL     /* a START to a started one    */
#define XHCI_RETRY_PNP_NEW_LIFETIME 9UL     /* a start's initialization    */
#define XHCI_RETRY_PHASE_ENTRY      0UL
#define XHCI_RETRY_PHASE_OUTCOME    1UL
#define XHCI_RETRY_OUT_OK           0UL
#define XHCI_RETRY_OUT_REFUSED_LOCAL 1UL
#define XHCI_RETRY_OUT_REFUSED_BELOW 2UL

/* The resume phase's values, equal to XHCI98_RESUME_RUNNING, _PENDING and
 * _FAILED (xhci98_xfer.h; hcd_power.c asserts it). */
#define XHCI_RETRY_RESUME_RUNNING   0UL
#define XHCI_RETRY_RESUME_PENDING   1UL
#define XHCI_RETRY_RESUME_FAILED    2UL

/* XHR_NONE (xhci_qry.h), restated so this header stays free of it. */
#define XHCI_RETRY_NEXT_NONE        0xFFFFFFFFUL

/* A sweep's port hold (record 38 section 6.1). */
#define XHCI_RETRY_HOLD_READ        0UL     /* go on                        */
#define XHCI_RETRY_HOLD_DISCARD     1UL     /* the sweep is a missed pass   */
#define XHCI_RETRY_HOLD_OFF         2UL     /* and the sampling stops       */

/* The snapshot block's shape, as a reader judges it. */
#define XHCI_RETRY_SHAPE_NOT_SERVED 0UL
#define XHCI_RETRY_SHAPE_OK         1UL
#define XHCI_RETRY_SHAPE_BAD        2UL

/*
 * The retry state: in the extension, appended after every field the
 * snapshot's harness reads, so none of them moves; the snapshot's HCD
 * header gives its place and size. An even number of ULONGs (52). Written
 * under the controller lock by the thread, the commit, the PnP writers
 * (Leave, LeaveGen), the power path (CleanArmed, ResumeFailed) and
 * HcdEnumService (ReleaseProved); read by the door and the snapshot.
 */
typedef struct _XHCI_TOL_RETRY {
    ULONG Found;            /* XHCI_TOL_RETRY_ABSENT, _DWORD, _OTHER_TYPE  */
    ULONG Raw;              /* the value as read                           */
    ULONG Value;            /* in effect: 0, or 2 to 600                   */
    ULONG Scale;            /* the qemu flavour's divisor; 1 otherwise     */
    ULONG Base;             /* B, ticks                                    */
    ULONG Ceiling;          /* C, ticks                                    */
    ULONG CleanTicks;
    ULONG StormTicks;
    ULONG State;            /* XHCI_RETRY_*                                */
    ULONG Level;
    ULONG Tries;            /* since the backing off last ended            */
    ULONG TriesTotal;       /* this lifetime                               */
    ULONG TriesRecovered;   /* bursts that ended in a success              */
    ULONG LatchStamp;
    ULONG LatchWait;        /* W(level) at the latch: the deadline's wait  */
    ULONG DeadlineValid;
    ULONG LastCause;
    ULONG LastStamp;
    ULONG LastWait;         /* ticks from the latch to the last commit     */
    ULONG Armed;            /* RetryArmed                                  */
    ULONG ArmedCause;
    ULONG Leave;            /* RetryLeave                                  */
    ULONG LeaveGen;         /* RetryLeaveGen: every write of the table     */
    ULONG ResumeFailed;     /* RetryResumeFailed                           */
    ULONG Burst;            /* RetryBurst                                  */
    ULONG BurstAttempts;
    ULONG LastBurstAttempts;
    ULONG LastBurstOutcome;
    ULONG SkipOnes;
    ULONG SkipLayout;
    ULONG WaitPower;
    ULONG WaitLeave;
    ULONG Episodes;
    ULONG MissedPasses;
    ULONG SampleOff;        /* sampling stopped for this latched episode   */
    ULONG SampleOffPort;
    ULONG LastEpisodePort;
    ULONG PortPending;      /* an episode completed and not yet consumed   */
    ULONG StormStamp[XHCI_TOL_RETRY_STORM_MAX]; /* oldest first           */
    ULONG StormCount;
    ULONG StormRefused;
    ULONG CleanPending;     /* a try recovered: a clean run is owed        */
    ULONG CleanArmed;       /* the clean run is being timed from CleanStart */
    ULONG CleanStart;
    ULONG CleanCalls;       /* ResetControllerCalls at the last pass       */
    ULONG BackoffEnds;
    ULONG ReleaseProved;
    ULONG ReleaseState;     /* XHCI_RETRY_REL_*                            */
    ULONG Noted;            /* XHCI_RETRY_NOTE_*                           */
    ULONG LatchSys;         /* the system time at the latch, in units
                             * (section 5.7); keeps the count even         */
} XHCI_TOL_RETRY, *PXHCI_TOL_RETRY;

XHCI_C_ASSERT(xhci_tol_retry_even, (sizeof(XHCI_TOL_RETRY) % 8) == 0);

/* One root port's evidence (record 38 section 6.2): the thread's alone.
 * HaveLast and LastCcs are the last admitted sample, the edge the
 * location observer is fed (section 6.6). */
typedef struct _XHCI_TOL_RETRY_PORT {
    ULONG HaveBase;
    ULONG Base;             /* CCS, 0 or 1                                 */
    ULONG CandArmed;
    ULONG CandStamp;
    ULONG HaveLast;
    ULONG LastCcs;
} XHCI_TOL_RETRY_PORT, *PXHCI_TOL_RETRY_PORT;

/* The admission's inputs, read in one controller-lock hold. */
typedef struct _XHCI_TOL_RETRY_IN {
    ULONG Value;
    ULONG Terminal;         /* XhciTolTerminalEx's                         */
    ULONG Pinned;           /* CommonBufferPinned                          */
    ULONG Msg4Barred;
    ULONG D0;               /* DevicePower D0, published by the D0 path    */
    ULONG SuspendedInD0;
    ULONG ExtSuspended;     /* XHCI_EXT_FLAG_SUSPENDED                     */
    ULONG ShutLatched;
    ULONG Leave;
    ULONG Started;          /* ControllerStarted and XHCI_EXT_FLAG_STARTED */
    ULONG PnpStarted;
    ULONG ThreadStop;
    ULONG CmdWaitLeave;     /* task 50.30 step 3's, hc->CmdWaitLeave != 0  */
    ULONG SlotsInvalidated;
    ULONG SlotsUnproven;
    ULONG ReleaseProved;
    ULONG DevicesLeft;
    ULONG Pad;
} XHCI_TOL_RETRY_IN, *PXHCI_TOL_RETRY_IN;

/* The capability guard's revalidation at the commit (record 38 section
 * 7.3): not needed where the guard is not set; otherwise the seven reads'
 * derivation equal to the lifetime's decode, different, or failed. */
#define XHCI_RETRY_REVAL_NOT_NEEDED 0UL
#define XHCI_RETRY_REVAL_EQUAL      1UL
#define XHCI_RETRY_REVAL_DIFFERENT  2UL
#define XHCI_RETRY_REVAL_FAILED     3UL

/* What the commit's second half did (record 38 section 7.4). */
#define XHCI_RETRY_COMMIT_SKIP_ONES   1UL
#define XHCI_RETRY_COMMIT_SKIP_LAYOUT 2UL
#define XHCI_RETRY_COMMIT_DONE        3UL

/* The latch's words outside the retry state that a commit resets, copied
 * in and back out by the driver in the commit's one controller-lock hold:
 * RecoveryFailuresConsecutive, TerminalReleased, RecoveryRequested, and
 * whether the capability guard is set (HcInfoStatus not XHCI_HC_OK). */
typedef struct _XHCI_TOL_RETRY_LATCH {
    ULONG Failures;
    ULONG TerminalReleased;
    ULONG Requested;
    ULONG GuardSet;
} XHCI_TOL_RETRY_LATCH, *PXHCI_TOL_RETRY_LATCH;

/* The clean run's healthy pass (record 38 section 5.4): every condition
 * read by the thread in one controller-lock hold. */
typedef struct _XHCI_TOL_RETRY_HEALTH {
    ULONG Terminal;         /* XhciTolTerminalEx's                         */
    ULONG Requested;        /* RecoveryRequested                           */
    ULONG Initialized;      /* XHCI_EXT_FLAG_INITIALIZED                   */
    ULONG ExtSuspended;     /* XHCI_EXT_FLAG_SUSPENDED                     */
    ULONG D0;
    ULONG SuspendedInD0;
    ULONG Started;          /* ControllerStarted                           */
    ULONG Leave;
    ULONG ShutLatched;
    ULONG Pad;
} XHCI_TOL_RETRY_HEALTH, *PXHCI_TOL_RETRY_HEALTH;

/* A sweep's PORTSC slot no hold read: a managed port the sweep did not
 * reach, or one not managed. An all-ones PORTSC is never kept (it ends the
 * sweep), so the value cannot be a sample. */
#define XHCI_RETRY_NOT_SAMPLED      0xFFFFFFFFUL

/* What a sweep's end gives one root port (record 38 sections 6.2, 6.3 and
 * 6.6), in this order: its evidence and its location observer's dropped,
 * then the sampled CCS fed to the observer, with an edge where it changed
 * since the last admitted sample, and an episode completed. */
#define XHCI_RETRY_FEED_BREAK       0x01U
#define XHCI_RETRY_FEED_SAMPLE      0x02U
#define XHCI_RETRY_FEED_CCS         0x04U
#define XHCI_RETRY_FEED_EDGE        0x08U
#define XHCI_RETRY_FEED_EPISODE     0x10U

/* The value (0 off, 2 to 600, else 5; `found` XHCI_TOL_RETRY_DWORD for a
 * number, anything else read as absent) and the qemu scale (1 to 600;
 * `found` nonzero for a number). */
ULONG XhciTolRetryValue(ULONG found, ULONG value);
ULONG XhciTolRetryScale(ULONG found, ULONG value);

/* ticks / scale, at least 1. */
ULONG XhciTolRetryScaled(ULONG ticks, ULONG scale);

/* A start: every field set from the reading, OFF or WATCHING. */
VOID XhciTolRetryInit(PXHCI_TOL_RETRY r, ULONG found, ULONG raw,
                      ULONG scale);

/* W(level), and the level after a try or a skip, held at the first level
 * whose wait is the ceiling. */
ULONG XhciTolRetryWait(const XHCI_TOL_RETRY *r, ULONG level);
ULONG XhciTolRetryLevelUp(const XHCI_TOL_RETRY *r, ULONG level);

/* The terminal as the thread computed it this pass, and the pinned buffer:
 * the latch (WATCHING to LATCHED) or an exclusion. XHCI_RETRY_EV_*. */
ULONG XhciTolRetryObserve(PXHCI_TOL_RETRY r, ULONG terminal, ULONG pinned,
                          ULONG now, ULONG nowSys);

/* The system-time floor of a wait of `ticks` (record 38 section 5.7), in
 * units: XHCI_TOL_RETRY_UNITS of (ticks - 1) x 100 ms, at least 1. */
ULONG XhciTolRetryFloor(ULONG ticks);

/* 1 when the units since `stamp` reach `units`; unsigned, so a time set
 * back reads as a very long interval. */
ULONG XhciTolRetrySysElapsed(ULONG nowSys, ULONG stamp, ULONG units);

/* LATCHED to DUE: the deadline, or a completed port episode within the
 * value's floor and the storm limit; the deadline wins a tie. Returns the
 * cause made due, 0 for none. */
ULONG XhciTolRetryDueStep(PXHCI_TOL_RETRY r, ULONG episode, ULONG now,
                          ULONG nowSys);

/* DUE: the thread arms it once. Returns 1 when it armed now. */
ULONG XhciTolRetryArm(PXHCI_TOL_RETRY r);

/* The commit (record 38 section 7.4): the level raised, the tries counted,
 * the deadline cleared, the burst begun with its first attempt. Returns the
 * try number, Tries after the count. */
ULONG XhciTolRetryCommit(PXHCI_TOL_RETRY r, ULONG now);

/* The revalidation's outcome (XHCI_RETRY_REVAL_*): guardSet 0 needs none;
 * otherwise deriveOk 0 is a failed derivation, and equal says whether the
 * derivation equals the lifetime's decode. */
ULONG XhciTolRetryRevalOutcome(ULONG guardSet, ULONG deriveOk, ULONG equal);

/* The commit's first half, from the admission's answer
 * (XHCI_RETRY_ADMIT_*): 1 to go on to the USBSTS read; 0 with the arming
 * dropped (a waiting answer counted, still DUE) or nothing done (not
 * armed). */
ULONG XhciTolRetryCommitAdmit(PXHCI_TOL_RETRY r, ULONG admit);

/* The commit's second half, all or nothing: USBSTS all ones or a
 * revalidation not EQUAL where the guard is set is a skip, which moves the
 * level and the deadline and nothing of `window` or `latch`; otherwise the
 * window started afresh, the latch's words reset (the guard cleared only
 * by an EQUAL revalidation, the request raised) and the try committed.
 * XHCI_RETRY_COMMIT_*. */
ULONG XhciTolRetryCommitFinish(PXHCI_TOL_RETRY r, PXHCI_TOL_WINDOW window,
                               PXHCI_TOL_RETRY_LATCH latch, ULONG usbsts,
                               ULONG reval, ULONG now, ULONG nowSys);

/* The clean run's healthy pass: 1 only when every condition holds. */
ULONG XhciTolRetryHealthy(const XHCI_TOL_RETRY_HEALTH *h);

/* A skip: the level raised, a new wait from now, back to LATCHED. */
VOID XhciTolRetrySkip(PXHCI_TOL_RETRY r, ULONG reason, ULONG now,
                      ULONG nowSys);

/* An armed try the commit refused for a passing reason: still DUE. */
VOID XhciTolRetryUnarm(PXHCI_TOL_RETRY r, ULONG forLeave);

/* The burst: an attempt it admitted, whether a leave defers the next, and
 * its end (XHCI_RETRY_BURST_*). */
VOID XhciTolRetryBurstAttempt(PXHCI_TOL_RETRY r);
ULONG XhciTolRetryBurstDefers(const XHCI_TOL_RETRY *r);
VOID XhciTolRetryBurstEnd(PXHCI_TOL_RETRY r, ULONG outcome);

/* The clean run (record 38 section 5.4): one thread pass, healthy or not,
 * with the reset requests' count. Returns 1 when it ended the backing off.
 * CleanBreak is a suspend's or a resume's. */
ULONG XhciTolRetryCleanPass(PXHCI_TOL_RETRY r, ULONG healthy, ULONG calls,
                            ULONG now);
VOID XhciTolRetryCleanBreak(PXHCI_TOL_RETRY r);

/* The admission's rows. forResume 1 is the latched resume's predicate: the
 * power rows and mode 4's bar are its own to settle. XHCI_RETRY_ADMIT_*. */
ULONG XhciTolRetryAdmit(const XHCI_TOL_RETRY_IN *in, ULONG forResume);

/* ReleaseState from its inputs. */
ULONG XhciTolRetryReleaseBits(ULONG invalidated, ULONG unproven,
                              ULONG proved, ULONG terminalReleased,
                              ULONG devicesLeft, ULONG pinned);

/* The leave flag's next value (record 38 section 7.5), and its write: the
 * value and the generation, advanced at every write. */
ULONG XhciTolRetryLeaveNext(ULONG path, ULONG phase, ULONG outcome,
                            ULONG current);
VOID XhciTolRetryLeaveWrite(PXHCI_TOL_RETRY r, ULONG path, ULONG phase,
                            ULONG outcome);

/* A latched resume's FAILED phase at a burst's end (record 38 section 9):
 * the next phase, and *advance 1 when MarkGen is to advance. */
ULONG XhciTolRetryResumeEnd(ULONG phase, ULONG mark, ULONG recovered,
                            PULONG advance);

/* A port's evidence. Sample takes an admitted sample's CCS (PP set, not all
 * ones) and returns 1 when it completed an episode; Break drops every
 * piece of it. */
ULONG XhciTolRetryPortSample(PXHCI_TOL_RETRY_PORT p, ULONG ccs, ULONG now,
                             ULONG stableTicks);
VOID XhciTolRetryPortBreak(PXHCI_TOL_RETRY_PORT p);

/* 1 when the gap since the last admitted sample breaks continuity. */
ULONG XhciTolRetrySampleGap(ULONG haveLast, ULONG last, ULONG now);

/* One port hold of a sweep: whether the admission still passes, the leave
 * generation captured and now, and the PORTSC read. XHCI_RETRY_HOLD_*. */
ULONG XhciTolRetrySweepHold(ULONG admitted, ULONG genStart, ULONG genNow,
                            ULONG portsc);

/* A sweep's end (record 38 sections 6.1 to 6.3). `count` ports; sweep[i]
 * the PORTSC port i + 1's hold read (XHCI_RETRY_NOT_SAMPLED for none),
 * managed[i] nonzero for a port the driver manages. A sweep discarded
 * (`discard`) drops every managed port's evidence and judges no sample:
 * no baseline, no episode, no observer feed. A gap that breaks continuity
 * (`gap`) drops every managed port's evidence first, then this sweep's
 * samples take fresh baselines. A sample with PP clear drops its port's
 * evidence. feed[i] gets XHCI_RETRY_FEED_* for each port. Returns the
 * episodes completed. */
ULONG XhciTolRetrySweepEnd(PXHCI_TOL_RETRY_PORT ports, const ULONG *sweep,
                           const UCHAR *managed, ULONG count, ULONG discard,
                           ULONG gap, ULONG now, ULONG stableTicks,
                           PUCHAR feed);

/* A sweep discarded: a missed pass counted and, where `off` (an all-ones
 * read), the sampling stopped for this latched episode at `offPort`.
 * Returns 1 when the once-per-episode note is due. */
ULONG XhciTolRetrySweepMissed(PXHCI_TOL_RETRY r, ULONG off, ULONG offPort);

/* XHR_O_RETRY_NEXT_S: XHCI_RETRY_NEXT_NONE but in LATCHED (the seconds to
 * the deadline, the larger of the ticks left at the nominal tick and the
 * floor's units left, rounded up) and DUE (0). */
ULONG XhciTolRetryNextSeconds(const XHCI_TOL_RETRY *r, ULONG now,
                              ULONG nowSys);

/* The recovery-status request's three words (record 38 section 15.1): the
 * value in effect, Tries (held at 65534), XHR_O_RETRY_NEXT_S; and `never`,
 * 1 in EXCLUDED (XHR_F_RETRY_NEVER). */
VOID XhciTolRetryStatusWords(const XHCI_TOL_RETRY *r, ULONG now,
                             ULONG nowSys, PULONG value, PULONG tries,
                             PULONG nextS, PULONG never);

/* 1 the first time a once-per-episode note is due. */
ULONG XhciTolRetryNoteOnce(PXHCI_TOL_RETRY r, ULONG bit);

/* A reader's judgment of the block's descriptor: the header's words, the
 * words the descriptor needs, its offset and size, the size the reader
 * knows and the extension image's size. XHCI_RETRY_SHAPE_*. */
ULONG XhciTolRetryBlockShape(ULONG headWords, ULONG needWords, ULONG at,
                             ULONG bytes, ULONG minBytes, ULONG extBytes);

#endif /* XHCI_RETRY_H */
