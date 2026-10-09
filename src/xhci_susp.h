/*
 * xhci_susp.h - selective suspend's pure half (roadmap-hcd.md task 36.29;
 * design record 14, section 3.9 for this file's split, sections 3 to 6
 * for the rules).
 *
 * The bus suspends a USB 2.0 device when Windows asks - a function's
 * D-state in S0, or the idle notification on XP onward - and wakes it on a
 * D0 or the device's remote wake. Three states are kept apart: the
 * device's link (one per device, XHCI_SUSP_LINK), each function PDO's power
 * (its slot, XHCI_SUSP_FUNC) and the device's wake episode. Everything
 * here is a decision over values: the executors (hcd_enum.c, hcd_hub.c,
 * hcd_pdo.c, hcd_urb.c) read the values under the controller lock, ask,
 * and perform the one step asked.
 *
 * DDK-free: part of the pure core. C89. IRQL: any. Nothing here takes a
 * lock, touches a register or calls a kernel service; the link record is
 * the enum thread's alone (record 14 section 4.1).
 */

#ifndef XHCI_SUSP_H
#define XHCI_SUSP_H

#include "xhci_compat.h"

/* ------------------------------------------------------------------ */
/* Values                                                              */
/* ------------------------------------------------------------------ */

/* Link states (record 14 section 3). */
#define XHCI_SUSP_ACTIVE            0UL
#define XHCI_SUSP_SUSPENDING        1UL
#define XHCI_SUSP_SUSPENDED         2UL
#define XHCI_SUSP_RESUMING          3UL
#define XHCI_SUSP_RECOVERING        4UL /* the existing recovery owns it  */
#define XHCI_SUSP_GONE              5UL

/* D-states: DEVICE_POWER_STATE's values (PowerDeviceD0 is 1). */
#define XHCI_SUSP_D0                1UL
#define XHCI_SUSP_D1                2UL
#define XHCI_SUSP_D2                3UL
#define XHCI_SUSP_D3                4UL

/* Answers the executor maps to NTSTATUS. */
#define XHCI_SUSP_ST_NONE           0UL /* nothing to complete            */
#define XHCI_SUSP_ST_SUCCESS        1UL
#define XHCI_SUSP_ST_CANCELLED      2UL
#define XHCI_SUSP_ST_NOT_SUPPORTED  3UL
#define XHCI_SUSP_ST_INVALID_STATE  4UL /* STATUS_INVALID_DEVICE_STATE    */
#define XHCI_SUSP_ST_BUSY           5UL /* STATUS_DEVICE_BUSY             */
#define XHCI_SUSP_ST_NO_DEVICE      6UL /* STATUS_NO_SUCH_DEVICE          */
#define XHCI_SUSP_ST_POWER_INVALID  7UL /* STATUS_POWER_STATE_INVALID     */
#define XHCI_SUSP_ST_PENDING        8UL /* held: no completion now        */
#define XHCI_SUSP_ST_TODAY          9UL /* the switch at 0 or not
                                         * eligible: today's handling     */

/* Timings, in milliseconds (record 14 sections 3.4 to 3.6). */
#define XHCI_SUSP_OBSERVE_MS        50UL  /* U3 observed, bus policy      */
#define XHCI_SUSP_STOP_MIN_MS       10UL  /* SP=1's minimum before a
                                           * restart (3.5)                */
#define XHCI_SUSP_BUSY_RETRY_MS     100UL /* NOT_SENT's one retry         */
#define XHCI_SUSP_U3_DWELL_MS       10UL  /* xHCI 1.2c 6.4.3.8: no resume
                                           * sooner after U3              */
#define XHCI_SUSP_TDRSMDN_MS        20UL  /* resume signalling            */
#define XHCI_SUSP_VERIFY_MS         50UL  /* U0 verified, bus policy      */
#define XHCI_SUSP_TRSMRCY_MS        10UL  /* resume recovery              */

/* ------------------------------------------------------------------ */
/* The switch and eligibility                                          */
/* ------------------------------------------------------------------ */

/* XhciSelectiveSuspend (record 14 section 7): absent or not a DWORD is 0,
 * the default; 1 turns stage A on; any other value reads as 0. */
ULONG XhciSuspSwitch(ULONG present, ULONG value);

/* Whether a device PDO is stage A's: the switch on, a USB 2.0 device (not
 * SuperSpeed), not a hub (5.1, 6.1). */
ULONG XhciSuspEligible(ULONG switchOn, ULONG superSpeed, ULONG hub);

/* ------------------------------------------------------------------ */
/* Capabilities (5.2)                                                  */
/* ------------------------------------------------------------------ */

typedef struct _XHCI_SUSP_CAPS {
    ULONG Changed;          /* 0: today's values exactly, nothing else
                             * here applies                              */
    ULONG DeviceD1;
    ULONG DeviceD2;
    ULONG WakeFromD1;
    ULONG WakeFromD2;
    ULONG WakeFromD3;
    ULONG DeviceWake;       /* XHCI_SUSP_D*, 0 for unspecified           */
    ULONG SystemWakeS0;     /* SystemWake PowerSystemWorking             */
} XHCI_SUSP_CAPS, *PXHCI_SUSP_CAPS;

/* `remoteWake` the configuration's bmAttributes bit 5; `wakeOff9x` wake
 * disabled on 9x (5.5). */
VOID XhciSuspCaps(ULONG eligible, ULONG remoteWake, ULONG wakeOff9x,
                  PXHCI_SUSP_CAPS out);

/* ------------------------------------------------------------------ */
/* A function PDO's slot, as the thread and the dispatch read it       */
/* ------------------------------------------------------------------ */

typedef struct _XHCI_SUSP_FUNC {
    /* inputs, read under the controller lock */
    ULONG PwrState;         /* the D-state the bus acts on               */
    ULONG PwrWanted;        /* the D-state asked                         */
    ULONG DxPended;         /* a Dx IRP is in its slot                   */
    ULONG DxEpoch;          /* ...accepted in this controller epoch      */
    ULONG D0Pended;         /* a D0 IRP is in its slot                   */
    ULONG WaitWake;         /* a WAIT_WAKE is held                       */
    ULONG Closed;           /* PwrClosed: torn down, or not started      */
    ULONG Seq;              /* the slot's change count: dispatch adds one
                             * for every D-IRP, WAIT_WAKE and START it
                             * accepts and every cancel it settles, never
                             * for the machine's own completions (3.9)   */
    /* in and out: the executor writes it back into the slot */
    ULONG WokeHold;         /* a wake completed this PDO's WAIT_WAKE: the
                             * device is not suspended again until it is
                             * D0 or closed (3.9); set by the executor at
                             * the claim, cleared here                   */
    /* outputs of XhciSuspDecide */
    ULONG CompleteDx;       /* claim and complete the pended Dx, SUCCESS */
    ULONG ToD0;             /* write PwrState D0: the link is ACTIVE and
                             * D0 is wanted (a pended D0, or a START that
                             * reopened the PDO, 3.9)                    */
    ULONG CompleteD0;       /* ...and claim and complete the pended D0,
                             * SUCCESS                                   */
} XHCI_SUSP_FUNC, *PXHCI_SUSP_FUNC;

/* ------------------------------------------------------------------ */
/* D-IRP admission at dispatch (4.2 to 4.4)                            */
/* ------------------------------------------------------------------ */

#define XHCI_SUSP_ADMIT_COMPLETE    1UL /* complete now, SUCCESS          */
#define XHCI_SUSP_ADMIT_PEND        2UL /* into the slot                  */
#define XHCI_SUSP_ADMIT_TODAY       3UL /* record and complete, as today  */

typedef struct _XHCI_SUSP_ADMIT_IN {
    ULONG Eligible;         /* XhciSuspEligible                          */
    ULONG Closed;           /* PwrClosed, or the controller released     */
    ULONG PowerDown;        /* hc->PowerDown                             */
    ULONG Link;             /* the device's XHCI_SUSP_*; GONE when the
                             * PDO is unlinked                           */
    ULONG SiblingD0;        /* another PDO of the device has PwrState D0 */
    ULONG Requested;        /* XHCI_SUSP_D*                              */
} XHCI_SUSP_ADMIT_IN, *PXHCI_SUSP_ADMIT_IN;

/* The admission and the PwrState written with it (*newState; unchanged
 * when the answer is PEND for a D0, which the thread writes at ACTIVE). */
ULONG XhciSuspAdmit(const XHCI_SUSP_ADMIT_IN *in, ULONG curState,
                    PULONG newState);

/* Whether a pended Dx stamped `dxEpoch` is from an older controller
 * session than `epoch` (4.4): it is then completed SUCCESS by the next
 * PASSIVE walker. */
ULONG XhciSuspDxStale(ULONG dxEpoch, ULONG epoch);

/* ------------------------------------------------------------------ */
/* The link record and the thread's decision (3.1, 3.9)                */
/* ------------------------------------------------------------------ */

typedef struct _XHCI_SUSP_LINK {
    ULONG State;            /* XHCI_SUSP_*                               */
    ULONG Root;             /* at a root port (else behind a USB 2.0 hub) */
    ULONG Armed;            /* DEVICE_REMOTE_WAKEUP set on the device    */
    ULONG Generation;       /* the device generation its port held       */
    ULONG U3Ms;             /* when U3 was observed                      */
    ULONG StopMs;           /* when the suspend's Stop Endpoint ran      */
    ULONG StepStartMs;      /* when the current step began               */
    ULONG Step;             /* XHCI_SUSP_STEP_*, 0 outside a sequence    */
    ULONG Kind;             /* XHCI_SUSP_RUN_* of the running sequence   */
    ULONG ArmWanted;        /* the sequence's arming intent              */
    ULONG Stopped;          /* endpoints were stopped by this sequence   */
    ULONG BusyRetried;      /* NOT_SENT's one retry is spent             */
    ULONG WakeKept;         /* wake evidence kept while SUSPENDING       */
    ULONG WakeTaken;        /* this suspension's wake event is recorded  */
    ULONG GoneKept;         /* a disconnect reported while a sequence ran */
    ULONG Internal;         /* the sequence is an internal cycle (5.4)   */
    ULONG LatchDue;         /* a refusal: latch at the next decision     */
    ULONG Latched;          /* no suspend until the slots' digest moves  */
    ULONG LatchDigest;
    ULONG ClaimDue;         /* the wake event was just taken: claim the
                             * WAIT_WAKEs now, then XhciSuspWakeCount    */
    ULONG Outcome;          /* XHCI_SUSP_OUT_* of the last sequence      */
    ULONG Reason;           /* XHCI_SUSP_WHY_* of the last refusal       */
} XHCI_SUSP_LINK, *PXHCI_SUSP_LINK;

/* A new device's record: ACTIVE, unarmed. */
VOID XhciSuspLinkInit(PXHCI_SUSP_LINK link, ULONG root, ULONG generation);

/* The slots' digest the refusal latch compares (3.9): each PDO's Seq and
 * the state it changed. */
ULONG XhciSuspDigest(const XHCI_SUSP_FUNC *funcs, ULONG count);

/* What the thread starts for the device this pass. */
#define XHCI_SUSP_DO_NONE           0UL
#define XHCI_SUSP_DO_SUSPEND        1UL /* begin 3.4                       */
#define XHCI_SUSP_DO_RESUME         2UL /* begin 3.6, host-initiated       */
#define XHCI_SUSP_DO_REARM          3UL /* resume to arm, then suspend
                                         * again: an internal cycle (5.4)  */

typedef struct _XHCI_SUSP_DECISION {
    ULONG Do;               /* XHCI_SUSP_DO_*                            */
    ULONG AllDx;            /* every PDO's PwrState is below D0          */
    ULONG WantD0;           /* some PDO wants D0                         */
    ULONG ArmIntent;        /* 5.4: a WAIT_WAKE held by a D1/D2 function */
    ULONG Completions;      /* CompleteDx/CompleteD0 flags set           */
} XHCI_SUSP_DECISION, *PXHCI_SUSP_DECISION;

/*
 * One pass over one device's slots, read in one controller-lock hold with
 * `epoch` the controller's PowerEpoch. Sets each slot's CompleteDx and
 * CompleteD0 and returns what to begin. A DO_SUSPEND is to be begun
 * (XhciSuspBegin, which writes SUSPENDING) in the same hold.
 * `powerDown`: the controller is leaving D0 - nothing is begun.
 */
ULONG XhciSuspDecide(PXHCI_SUSP_LINK link, PXHCI_SUSP_FUNC funcs,
                     ULONG count, ULONG epoch, ULONG powerDown,
                     PXHCI_SUSP_DECISION out);

/* ------------------------------------------------------------------ */
/* The sequences as steps (3.4 to 3.6)                                 */
/* ------------------------------------------------------------------ */

#define XHCI_SUSP_RUN_SUSPEND       1UL
#define XHCI_SUSP_RUN_RESUME        2UL /* host-initiated                 */
#define XHCI_SUSP_RUN_WAKE          3UL /* device-initiated               */

/* The steps the executor performs. Each names its wait in *waitMs from
 * XhciSuspBegin or XhciSuspAdvance: the executor waits that long before
 * performing it - except OBSERVE and VERIFY, which poll for up to it, and
 * a repeated ARM, for which it is the bound on EP0's drain: the executor
 * waits for the ring to drain for at most *waitMs, its EP0-quiet wait
 * included, and reports NOT_SENT again if it has not (3.5). */
#define XHCI_SUSP_STEP_GATE         1UL /* close the gate (3.2)           */
#define XHCI_SUSP_STEP_ARM          2UL /* SET_FEATURE(DEVICE_REMOTE_WAKEUP) */
#define XHCI_SUSP_STEP_STOP         3UL /* Stop Endpoint SP=1, every one  */
#define XHCI_SUSP_STEP_U3           4UL /* root: PLS=U3; hub:
                                         * SetPortFeature(PORT_SUSPEND)   */
#define XHCI_SUSP_STEP_OBSERVE      5UL /* poll, bounded *waitMs          */
#define XHCI_SUSP_STEP_SIGNAL       6UL /* root: PLS=Resume; hub:
                                         * ClearPortFeature(PORT_SUSPEND),
                                         * after *waitMs (the dwell)      */
#define XHCI_SUSP_STEP_U0           7UL /* root: PLS=U0, after *waitMs
                                         * (TDRSMDN)                      */
#define XHCI_SUSP_STEP_VERIFY       8UL /* poll, bounded *waitMs          */
#define XHCI_SUSP_STEP_RECOVERY     9UL /* wait *waitMs (TRSMRCY)         */
#define XHCI_SUSP_STEP_DISARM       10UL /* CLEAR_FEATURE(DEVICE_REMOTE_WAKEUP) */
#define XHCI_SUSP_STEP_RESTART      11UL /* after *waitMs: doorbells for
                                          * the rings holding TDs (3.2),
                                          * then open the gate            */
#define XHCI_SUSP_STEP_END          12UL /* the sequence is over          */

/* A step's result, as the executor reports it. */
#define XHCI_SUSP_RES_OK            0UL
#define XHCI_SUSP_RES_STALL         1UL /* the device answered STALL      */
#define XHCI_SUSP_RES_NOT_SENT      2UL /* EP0 busy with client TDs       */
#define XHCI_SUSP_RES_FAILED        3UL /* failed with every endpoint's
                                         * state read back, nothing left
                                         * U0: the hardware is usable     */
#define XHCI_SUSP_RES_TIMEOUT       4UL /* an EP0 or command timeout, or a
                                         * bound run out unrefuted        */
#define XHCI_SUSP_RES_DISCONNECT    5UL /* CSC, lost CCS, a port error    */
#define XHCI_SUSP_RES_WAKE          6UL /* OBSERVE: the device is waking  */

/* How a sequence ended (link->Outcome). */
#define XHCI_SUSP_OUT_NONE          0UL
#define XHCI_SUSP_OUT_SUSPENDED     1UL
#define XHCI_SUSP_OUT_REFUSED       2UL /* back ACTIVE, link->Reason      */
#define XHCI_SUSP_OUT_UNCERTAIN     3UL /* RECOVERING                     */
#define XHCI_SUSP_OUT_GONE          4UL
#define XHCI_SUSP_OUT_ACTIVE        5UL /* a resume reached ACTIVE        */

/* A refusal's reason (3.5). */
#define XHCI_SUSP_WHY_NONE          0UL
#define XHCI_SUSP_WHY_STALL         1UL /* the device declines wake: the
                                         * held WAIT_WAKEs complete
                                         * NOT_SUPPORTED                  */
#define XHCI_SUSP_WHY_BUSY          2UL /* NOT_SENT twice: they stay      */
#define XHCI_SUSP_WHY_FAILED        3UL /* a stop or the U3 write failed  */

/*
 * Begin a sequence of `kind` at `nowMs`; `armWanted` is the decision's
 * ArmIntent (a suspend arms only with it), `internal` marks 5.4's cycles.
 * Writes SUSPENDING or RESUMING and returns the first step.
 */
ULONG XhciSuspBegin(PXHCI_SUSP_LINK link, ULONG kind, ULONG armWanted,
                    ULONG internal, ULONG nowMs, PULONG waitMs);

/* The current step's result at `nowMs`: the next step, or END with
 * link->State and link->Outcome written. */
ULONG XhciSuspAdvance(PXHCI_SUSP_LINK link, ULONG result, ULONG nowMs,
                      PULONG waitMs);

/* The existing recovery has finished with a RECOVERING device: `ok` it
 * re-established it (ACTIVE: 3.2's restart and the gate are the
 * recovery's caller's), else GONE. */
VOID XhciSuspRecovered(PXHCI_SUSP_LINK link, ULONG ok);

/* The device is gone (unplug, invalidation): GONE from any state. */
VOID XhciSuspGone(PXHCI_SUSP_LINK link);

/* ------------------------------------------------------------------ */
/* Wake evidence (3.7, 3.8)                                            */
/* ------------------------------------------------------------------ */

#define XHCI_SUSP_EV_IGNORED        0UL /* stale or meaningless           */
#define XHCI_SUSP_EV_DISCONNECT     1UL
#define XHCI_SUSP_EV_STALE          2UL /* another device generation      */
#define XHCI_SUSP_EV_SUSPEND_ENTRY  3UL /* the port reached U3            */
#define XHCI_SUSP_EV_WAKE           4UL /* the device signalled wake      */
#define XHCI_SUSP_EV_RESUME_DONE    5UL /* the host's resume finished     */

/* A root USB 2.0 port's sample: PORTSC read before the acknowledgement,
 * the change mask acknowledged, the generation the port held. */
ULONG XhciSuspRootEvidence(const XHCI_SUSP_LINK *link, ULONG portsc,
                           ULONG acked, ULONG generation);

/* A USB 2.0 hub port's sample: wPortStatus, the wPortChange bits
 * acknowledged, the generation. */
ULONG XhciSuspHubEvidence(const XHCI_SUSP_LINK *link, ULONG status,
                          ULONG acked, ULONG generation);

/*
 * The thread hands the machine one classified sample. Returns 1 when it
 * is the suspension's wake event (5.4), with ClaimDue set: the executor
 * then, in the same hold, reads hc->WakeSeq as the watermark, claims the
 * WAIT_WAKEs XhciSuspWakeClaims names (setting each claimed PDO's
 * WokeHold) and reports how many with XhciSuspWakeCount, and begins
 * RUN_WAKE. Evidence while SUSPENDING is kept (WakeKept) and taken once
 * the U3 request is done - without waiting to see it again - which sets
 * ClaimDue the same way. Evidence while RESUMING is ignored. A DISCONNECT
 * makes the link GONE at once outside a sequence; inside one it is kept
 * (GoneKept) and the sequence's next step ends GONE, ahead of any wake.
 */
ULONG XhciSuspEvidence(PXHCI_SUSP_LINK link, ULONG event);

/* Whether a WAIT_WAKE stamped `stamp` is completed by a wake whose
 * watermark is `watermark`: stamped before it (signed 32-bit difference)
 * and its function D1 or D2. */
ULONG XhciSuspWakeClaims(ULONG stamp, ULONG watermark, ULONG pwrState);

/* The wake event's claims: `claimed` WAIT_WAKEs completed SUCCESS. */
VOID XhciSuspWakeCount(PXHCI_SUSP_LINK link, ULONG claimed);

/* The observation at step 5, from one sample: OK (U3 or PORT_SUSPEND
 * set), WAKE, DISCONNECT, or XHCI_SUSP_POLL_AGAIN. */
#define XHCI_SUSP_POLL_AGAIN        0xFFUL
ULONG XhciSuspObserveRoot(ULONG portsc);
ULONG XhciSuspObserveHub(ULONG status, ULONG change);

/* The verification at 3.6: OK (U0; at a hub PORT_SUSPEND clear on an
 * enabled port), DISCONNECT, or XHCI_SUSP_POLL_AGAIN. */
ULONG XhciSuspVerifyRoot(ULONG portsc);
ULONG XhciSuspVerifyHub(ULONG status, ULONG change);

/* ------------------------------------------------------------------ */
/* WAIT_WAKE (5.3)                                                     */
/* ------------------------------------------------------------------ */

typedef struct _XHCI_SUSP_WW_IN {
    ULONG Released;         /* the controller is released (4.2)          */
    ULONG Eligible;
    ULONG RemoteWake;       /* the configuration's bit 5                 */
    ULONG WakeOff9x;
    ULONG Closed;           /* PwrClosed                                 */
    ULONG SystemState;      /* the IRP's: 1 = S0 (PowerSystemWorking)    */
    ULONG PwrState;
    ULONG Held;             /* a WAIT_WAKE is already held               */
} XHCI_SUSP_WW_IN, *PXHCI_SUSP_WW_IN;

/* PENDING (accept and stamp), or the refusal's status. */
ULONG XhciSuspWaitWakeAdmit(const XHCI_SUSP_WW_IN *in);

/* ------------------------------------------------------------------ */
/* The idle notification (section 6)                                   */
/* ------------------------------------------------------------------ */

#define XHCI_SUSP_IDLE_NONE         0UL
#define XHCI_SUSP_IDLE_HELD         1UL
#define XHCI_SUSP_IDLE_QUEUED       2UL
#define XHCI_SUSP_IDLE_RUNNING      3UL
#define XHCI_SUSP_IDLE_RETURNED     4UL

/* A submission: `released` the controller, `closed` PwrClosed, `owned`
 * the PDO's previous IRP or entry is still owned. PENDING (held), TODAY
 * (held as today, never called: not eligible), or a refusal. */
ULONG XhciSuspIdleSubmit(ULONG released, ULONG eligible, ULONG closed,
                         ULONG owned);

typedef struct _XHCI_SUSP_IDLE_FUNC {
    ULONG Started;          /* the PDO is started                        */
    ULONG Closed;
    ULONG IdleState;        /* XHCI_SUSP_IDLE_*                          */
    ULONG PwrState;
} XHCI_SUSP_IDLE_FUNC, *PXHCI_SUSP_IDLE_FUNC;

/* 6.2: every started function PDO of the device holds an idle IRP in HELD
 * with PwrState D0, at least one is started, the link is ACTIVE and the
 * system in S0 - and no PDO outside the group (not started, or closed)
 * is D0, since such a PDO holds the device awake (3.9) and a callback
 * would send its function to Dx on a link that cannot suspend. */
ULONG XhciSuspIdleBatchReady(const XHCI_SUSP_IDLE_FUNC *funcs, ULONG count,
                             ULONG link, ULONG systemS0);

/* The claim at call time (6.3). */
#define XHCI_SUSP_CLAIM_LET_GO      1UL /* only let go of the entry        */
#define XHCI_SUSP_CLAIM_RUN         2UL /* RUNNING: call the callback      */
#define XHCI_SUSP_CLAIM_SKIP        3UL /* owned and ineligible: clear the
                                         * slot, complete CANCELLED, the
                                         * episode ends                   */

typedef struct _XHCI_SUSP_CLAIM_IN {
    ULONG IdleState;
    ULONG EntryBatch;
    ULONG BatchCur;         /* the PDO's IdleBatchCur                    */
    ULONG Started;
    ULONG Closed;
    ULONG PwrState;
    ULONG SystemS0;
    ULONG EntryEpoch;
    ULONG Epoch;
    ULONG SlotFull;         /* the slot names an IRP                     */
    ULONG Owned;            /* IoSetCancelRoutine(irp, NULL) was non-NULL */
} XHCI_SUSP_CLAIM_IN, *PXHCI_SUSP_CLAIM_IN;

ULONG XhciSuspIdleClaim(const XHCI_SUSP_CLAIM_IN *in);

/* Settlement triggers (6.5), in the table's order of precedence. */
#define XHCI_SUSP_SET_CANCEL        1UL /* its cancel routine             */
#define XHCI_SUSP_SET_GONE          2UL /* torn down, or its device GONE  */
#define XHCI_SUSP_SET_SYSTEM        3UL /* a system IRP for a state not S0 */
#define XHCI_SUSP_SET_D3            4UL /* its PDO accepts D3             */
#define XHCI_SUSP_SET_D0            5UL /* having been Dx, D0 at ACTIVE   */
#define XHCI_SUSP_SET_EPISODE       6UL /* the episode ended, link ACTIVE */
#define XHCI_SUSP_SET_SKIP          7UL /* a skip at claim                */

/* The status a trigger settles an idle IRP in `idleState` with, its PDO
 * in `pwrState`: ST_NONE when the trigger does not reach it, ST_PENDING
 * when it reaches it only after its callback returns (RUNNING). */
ULONG XhciSuspIdleSettle(ULONG trigger, ULONG idleState, ULONG pwrState);

/* Events that may end an episode (6.5). */
#define XHCI_SUSP_EPI_CANCEL        1UL
#define XHCI_SUSP_EPI_OWN_D0        2UL
#define XHCI_SUSP_EPI_SKIP          3UL
#define XHCI_SUSP_EPI_REFUSED       4UL /* a refusal or an uncertain
                                         * outcome that ended ACTIVE      */
#define XHCI_SUSP_EPI_SYSTEM        5UL
#define XHCI_SUSP_EPI_GONE          6UL
#define XHCI_SUSP_EPI_FUNC_D3       7UL
#define XHCI_SUSP_EPI_INTERNAL      8UL /* 5.4's rearm or unclaimed wake  */

ULONG XhciSuspEpisodeEnds(ULONG event);

/* ------------------------------------------------------------------ */
/* Cancellable IRPs: publication and claim (4.2)                       */
/* ------------------------------------------------------------------ */

#define XHCI_SUSP_PUB_PUBLISHED     1UL /* cancellable in its slot        */
#define XHCI_SUSP_PUB_TAKE_BACK     2UL /* take it out of the slot,
                                         * complete CANCELLED             */
#define XHCI_SUSP_PUB_ROUTINE       3UL /* the routine runs and completes */

/* After IoSetCancelRoutine(irp, routine): `cancel` irp->Cancel, and, when
 * set, `cleared` whether IoSetCancelRoutine(irp, NULL) returned non-NULL. */
ULONG XhciSuspPublish(ULONG cancel, ULONG cleared);

/* A claimer under the controller lock: 1 when it owns the IRP (clear the
 * slot in the same hold), 0 when the cancel routine owns it (leave the
 * slot). `cleared` as above. */
ULONG XhciSuspClaim(ULONG slotFull, ULONG cleared);

/* ------------------------------------------------------------------ */
/* The callback thread's context (4.4)                                 */
/* ------------------------------------------------------------------ */

#define XHCI_SUSP_PH_IDLE           0UL
#define XHCI_SUSP_PH_CLAIMING       1UL
#define XHCI_SUSP_PH_IN_CALLBACK    2UL
#define XHCI_SUSP_PH_RETURNING      3UL
#define XHCI_SUSP_PH_ABANDONED      4UL
#define XHCI_SUSP_PH_EXITING        5UL

#define XHCI_SUSP_ACTOR_THREAD      1UL
#define XHCI_SUSP_ACTOR_TEARDOWN    2UL

/* Whether `actor` may compare-exchange the phase from `from` to `to`. */
ULONG XhciSuspPhaseAllowed(ULONG actor, ULONG from, ULONG to);

/* Teardown's handoff, steps (2) to (4), for the phase it observed. */
#define XHCI_SUSP_HAND_EXIT         1UL /* try IDLE -> EXITING; join      */
#define XHCI_SUSP_HAND_ABANDON      2UL /* try IN_CALLBACK -> ABANDONED   */
#define XHCI_SUSP_HAND_WAIT         3UL /* CLAIMING or RETURNING: wait for
                                         * it to leave, retry from (1)    */
#define XHCI_SUSP_HAND_DONE         4UL /* EXITING or ABANDONED already   */

/* After teardown wins EXITING or ABANDONED it runs its step (1) once more:
 * the thread may have put an entry back as unclaimed after the first (1)
 * and gone IDLE before teardown read the phase, and once teardown has won
 * the thread touches the queue no more (4.4). */

ULONG XhciSuspHandoff(ULONG phase);

/* ------------------------------------------------------------------ */
/* Finalization of a PDO's teardown (4.4 (b) finish, (e))              */
/* ------------------------------------------------------------------ */

#define XHCI_SUSP_FIN_NOW           1UL
#define XHCI_SUSP_FIN_WAIT          2UL /* a PnP caller, the thread alive */
#define XHCI_SUSP_FIN_DEFER         3UL /* the enum thread as caller      */
#define XHCI_SUSP_FIN_QUARANTINE    4UL /* no DMA-halt proof              */

typedef struct _XHCI_SUSP_FIN_IN {
    ULONG CallerIsThread;   /* the enum thread (hcdDeletePdo)            */
    ULONG ThreadExited;     /* the enum thread has exited                */
    ULONG UrbsPending;      /* nonzero                                   */
    ULONG CallbackRunning;  /* the idle rundown is not finished          */
    ULONG PowerPending;     /* a D-IRP or WAIT_WAKE whose completion has
                             * not returned (4.2) - a cancel routine's
                             * included                                  */
    ULONG DmaHalted;        /* the DMA-halt proof holds                  */
    ULONG Pinned;           /* the proof failed and the common buffer is
                             * pinned (CommonBufferPinned), the thread
                             * alive or not: the pending URBs never end  */
} XHCI_SUSP_FIN_IN, *PXHCI_SUSP_FIN_IN;

ULONG XhciSuspFinalize(const XHCI_SUSP_FIN_IN *in);

#endif /* XHCI_SUSP_H */
