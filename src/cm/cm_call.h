/*
 * cm_call.h - the per-member call workers of Windows 98 SE and ME and
 * their handoff word (roadmap-usb4.md task 42-A.12; design record 21
 * section 7.2, "The 9x half", part 0; stor98cm.h interface 3's R6e and
 * R6f).
 *
 * THE SHAPE. Interface 3's R6e: a connection manager serving several
 * ports keeps every wait as per-port state advanced by its one polling
 * loop, and makes the CONFIGMG calls that can wait on the operating
 * system or on other drivers off that loop, from a per-port worker,
 * taking each result into the port's state when it arrives. So the
 * transactions (their deadlines, retries, bounds and results) are state
 * the connection-manager thread owns and advances on its passes, read on
 * its clock, and the calls are made by one call worker per member (at
 * most XHCI_TBT_MAX_TUNNELS a domain), one call at a time: a member whose
 * worker is inside a call blocked on the configuration manager (a wizard
 * open, a removal taking its time) delays that member alone.
 *
 * THE HANDOFF WORD (R6f), one per member, CM_CALL.State:
 *   IDLE -> SUBMITTED   the thread, after storing the operation, its
 *                       executor, its context and a new operation id
 *                       (CmCallSubmit);
 *   SUBMITTED -> RUNNING  the worker, as a compare-exchange, so an
 *                       operation is taken once (CmCallClaim);
 *   RUNNING -> DONE     the worker, after storing the result and the id
 *                       it ran (CmCallFinish);
 *   DONE -> IDLE        the thread, taking the result only for the id it
 *                       names, discarding any other (CmCallTake);
 *   SUBMITTED -> IDLE   the thread, withdrawing an operation no worker
 *                       has taken (CmCallWithdraw).
 * One operation at a time per member. A transaction whose deadline passes
 * while its call runs moves on (R11 O6: "an elapsed deadline, also when a
 * call is still running"); the call stays the worker's until it returns,
 * and its late result is still taken by its id - the client decides what
 * a late result counts as (an O6 retry's outcome, never a clean one).
 *
 * THE CLIENTS. Each operation names its executor (CM_CALL_RUN) and a
 * context that outlives the call (the client's own state in the domain's
 * block, never the NHI's extension), and carries its arguments in the
 * slot itself (at most CM_CALL_OP_BYTES), its result coming back in the
 * slot (at most CM_CALL_RES_BYTES); the executor runs on the worker with
 * that worker's own CONFIGMG route. Design record 28's state machine
 * (task 42-A.15, cm_noirq.h: CmNiNext hands out one CM_NI_OP, CmNiTake
 * takes its CM_NI_RES) is one such client; the 9x removal transaction
 * and the statement's slot reading (task 42-A.13) are the others.
 *
 * DDK-FREE PART. The functions under "pure" touch nothing but the slot;
 * the driver calls each under the block's spin lock, which is what makes
 * the claim a compare-exchange (no interlocked import beyond the ones the
 * Windows 98 allowlist has). test\test_cm_call.c runs them on the host.
 *
 * IRQL: the pure functions at any IRQL. The driver half as stated below.
 */

#ifndef CM_CALL_H
#define CM_CALL_H

#include "../xhci_compat.h"

/* The handoff word (R6f). */
#define CM_CALL_IDLE        0UL
#define CM_CALL_SUBMITTED   1UL
#define CM_CALL_RUNNING     2UL
#define CM_CALL_DONE        3UL

/* The room an operation's arguments and its result have in the slot. */
#define CM_CALL_OP_BYTES    64UL
#define CM_CALL_RES_BYTES   256UL

/* CmCallTake's answer. */
#define CM_CALL_T_NONE      0UL /* idle: nothing submitted                */
#define CM_CALL_T_BUSY      1UL /* submitted or running                   */
#define CM_CALL_T_TAKEN     2UL /* the named id's result, copied; idle    */
#define CM_CALL_T_STALE     3UL /* another id's result, discarded; idle   */

/* An operation's executor, on the member's worker: `route` that worker's
 * CONFIGMG route (a PCM_CFGMG), `context` the client's, `op` the
 * arguments as submitted, `res` the slot's result room (zeroed first).
 * Returns the result's length in bytes (at most CM_CALL_RES_BYTES).
 * PASSIVE_LEVEL, no lock held; it may block in the configuration manager
 * for as long as that takes. */
typedef ULONG (*CM_CALL_RUN)(PVOID route, PVOID context, const VOID *op,
                             VOID *res);

/*
 * AFTER THE STOP (record 21 section 7.2 part 0, "Who owns a transaction").
 * The NHI's stop never waits for a call. An operation submitted and not yet
 * taken is withdrawn by the stop, unless its kind carries CM_CALL_K_DETACH:
 * then the worker still takes and runs it after the stop (a removal its
 * transaction was already authorised to make). Every operation the thread
 * has not taken back by the stop ends at the operation's `late` callback
 * (CmCallSetLate), called by the member's worker before it leaves -
 * PASSIVE_LEVEL, no lock held, never on the stopping thread, with that
 * worker's own CONFIGMG route as `route` (a detached transaction's removal
 * is made on it, task 43.18) - with
 * CM_CALL_L_RESULT and the result (one that finished before the stop and
 * was not taken, or that finished after it: a detached one, or one running
 * when the stop came) or with CM_CALL_L_WITHDRAWN and no result (never
 * run). So a transaction's persistent record (an accepted query's
 * CONFIGRET, a removal's success, a query never made: the member blocker)
 * learns how every operation it submitted ended; with no callback the
 * outcome is dropped with the block. A client that sets one keeps the
 * operation's context valid until its callback returns: image-static or
 * never-freed state, never the NHI's extension.
 */
#define CM_CALL_K_DETACH    0x80000000UL

#define CM_CALL_L_RESULT    1UL /* the operation ran: its result          */
#define CM_CALL_L_WITHDRAWN 2UL /* withdrawn by the stop, never run       */

typedef VOID (*CM_CALL_LATE)(PVOID route, PVOID context, ULONG kind,
                             ULONG id, ULONG outcome, const VOID *res,
                             ULONG bytes);

typedef struct _CM_CALL {
    ULONG State;            /* CM_CALL_*: the handoff word               */
    ULONG Id;               /* the operation submitted: the thread's     */
    ULONG Ran;              /* the id the worker ran: the worker's       */
    ULONG NextId;           /* the thread's: ids handed out, never 0     */
    ULONG Kind;             /* the client's operation kind (the log)     */
    ULONG SubmittedAt;      /* the thread's clock at the submission, ms  */
    CM_CALL_RUN Run;
    CM_CALL_LATE Late;      /* the outcome's taker after the stop, or NULL */
    ULONG Withdrawn;        /* DONE by the stop's withdrawal, never run    */
    PVOID Context;
    ULONG OpBytes;
    ULONG ResBytes;
    ULONG Op[CM_CALL_OP_BYTES / 4];
    ULONG Res[CM_CALL_RES_BYTES / 4];
} CM_CALL, *PCM_CALL;

/* ---- pure ------------------------------------------------------------- */

/* Idle, no id handed out yet. */
VOID CmCallInit(PCM_CALL c);

/* The thread: from IDLE only, `bytes` (at most CM_CALL_OP_BYTES) of `op`
 * stored with `run`, `context`, `kind` and `nowMs`, a new nonzero id, then
 * SUBMITTED. Returns the id, or 0 (not idle, no executor, too long). */
ULONG CmCallSubmit(PCM_CALL c, ULONG kind, CM_CALL_RUN run, PVOID context,
                   const VOID *op, ULONG bytes, ULONG nowMs);

/* The thread, right after CmCallSubmit and under the same lock: the
 * operation's taker after a stop (NULL: none). */
VOID CmCallSetLate(PCM_CALL c, CM_CALL_LATE late);

/* The stop, on one slot: an operation submitted and not taken, unless its
 * kind carries CM_CALL_K_DETACH, is withdrawn into DONE with Withdrawn set
 * and no result, for the worker to hand to its late taker (TRUE when one
 * was). CmCallRunsAfterStop: whether the worker still has an operation to
 * take after the stop (a detached one submitted). CmCallLateDue: whether
 * the slot holds an outcome for the late taker (DONE), and which
 * (CM_CALL_L_*). */
BOOLEAN CmCallStopSlot(PCM_CALL c);
BOOLEAN CmCallRunsAfterStop(const CM_CALL *c);
BOOLEAN CmCallLateDue(const CM_CALL *c, PULONG outcome);

/* The worker: SUBMITTED -> RUNNING, TRUE once per operation; the result
 * room zeroed. */
BOOLEAN CmCallClaim(PCM_CALL c);

/* The worker: RUNNING -> DONE with the result's length (held to the
 * room) and the id it ran. Nothing for a slot not RUNNING. */
VOID CmCallFinish(PCM_CALL c, ULONG resBytes);

/* The thread: a DONE slot's result for operation `id` copied into `res`
 * (at most `bytes`; *got its length, `got` may be NULL), the slot IDLE,
 * CM_CALL_T_TAKEN; a DONE slot for another id discarded, IDLE,
 * CM_CALL_T_STALE; SUBMITTED or RUNNING: CM_CALL_T_BUSY; IDLE:
 * CM_CALL_T_NONE. */
ULONG CmCallTake(PCM_CALL c, ULONG id, VOID *res, ULONG bytes, PULONG got);

/* The thread: an operation `id` no worker has taken withdrawn (SUBMITTED
 * -> IDLE). FALSE when it was taken, finished, or another id. */
BOOLEAN CmCallWithdraw(PCM_CALL c, ULONG id);

/* Milliseconds since the operation in the slot was submitted (0 when
 * idle): what a transaction's deadline is read against while its call
 * runs. */
ULONG CmCallAge(const CM_CALL *c, ULONG nowMs);

/* ---- the driver (cm_callwk.c) ----------------------------------------- */

#ifndef XHCI_HOST_TEST

struct _CM_NHI;
struct _CM_CALLWK;

/* Windows 98 SE and ME (x86): the domain's call workers, one per member
 * (at most XHCI_TBT_MAX_TUNNELS), each a system thread with its own
 * CONFIGMG route, its own block in non-paged pool (never the NHI's
 * extension); each opens its route as its first operation, and the
 * thread reads each opening back (CmCallwkService). NULL where they could
 * not be made, or on NT (whose calls stay the bridge filter's, on the
 * thread). PASSIVE_LEVEL, on the connection-manager thread, after the
 * NHI's start completed. */
struct _CM_CALLWK *CmCallwkStart(struct _CM_NHI *nhi, ULONG members);

/* The thread: an operation for member `member`'s worker (CmCallSubmit),
 * stamped with the thread's clock `nowMs`; 0 when the member's worker is
 * not idle, is leaving, or does not exist. <= DISPATCH_LEVEL. */
ULONG CmCallwkSubmit(struct _CM_CALLWK *b, ULONG member, ULONG kind,
                     CM_CALL_RUN run, CM_CALL_LATE late, PVOID context,
                     const VOID *op, ULONG bytes, ULONG nowMs);

/* The thread: CmCallTake on member `member`'s slot; CM_CALL_T_NONE for no
 * such member. <= DISPATCH_LEVEL. */
ULONG CmCallwkTake(struct _CM_CALLWK *b, ULONG member, ULONG id,
                   VOID *res, ULONG bytes, PULONG got);

/* The thread: whether member `member`'s worker has an operation submitted
 * or running ("busy" to the thread). <= DISPATCH_LEVEL. */
BOOLEAN CmCallwkBusy(struct _CM_CALLWK *b, ULONG member);

/* Once per pass: each worker's opening, taken and logged once
 * (cm.callwk.open: member 31:24, bit 23 opened, CM_CFGMG_WHY_* 22:16,
 * Get_Version's AX 15:0, or FFFFh for a stale result). PASSIVE_LEVEL. */
VOID CmCallwkService(struct _CM_NHI *nhi, struct _CM_CALLWK *b);

/* The NHI's stop: every worker asked to leave and its wake signalled;
 * nothing is waited for (a worker inside a call that is waiting on this
 * very removal would deadlock it, record 21 section 7.2 part 0), and the
 * last of the stop and the workers to let go frees the block and closes
 * the routes. Operations are withdrawn, run on and handed over as "AFTER
 * THE STOP" above says. PASSIVE_LEVEL. */
VOID CmCallwkStop(struct _CM_CALLWK *b);

/* A system thread at `routine` with `context`, its handle closed at once
 * (cm_thr.c, the one site of ZwClose for threads). PASSIVE_LEVEL. */
NTSTATUS CmThrSpawn(VOID (NTAPI *routine)(PVOID), PVOID context);

#endif /* !XHCI_HOST_TEST */

#endif /* CM_CALL_H */
