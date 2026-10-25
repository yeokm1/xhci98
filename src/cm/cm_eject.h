/*
 * cm_eject.h - the Windows 98 SE and ME eject transaction of the Stor98
 * channel and the 9x link-down statement's decision (roadmap-usb4.md task
 * 43.18; design record 21 section 7.2, "The 9x half", parts 1 and 2;
 * stor98cm.h interface 3's R9, R11 and R12). cm_ejask.h is the registry
 * values' layout the tools share; this file is the connection manager's
 * state for them.
 *
 * THE TRANSACTION (part 1). XHCIINST /eject and XHCITR's Safely Remove of
 * a unit behind a verified tunnel port ask this driver (CM_EJ_ASK_VALUE)
 * instead of calling CM_Query_Remove_SubTree and CM_Remove_SubTree
 * themselves. An ask is checked on the thread (CmEjAccept) before anything
 * is called; accepted, it becomes a transaction on its member, owned and
 * advanced by the connection-manager thread on its clock (CmEjNext,
 * CmEjFeed), each CONFIGMG call made by that member's call worker
 * (cm_call.h). Its course:
 *
 *   IDENT     the unit's devnode found under the member's (a call), its
 *             instance id kept: every later call names that devnode only
 *             while it still carries that id.
 *   QUERY     Query_Remove_SubTree (a call). Refused: terminal, nothing
 *             else is done (REFUSED with its CONFIGRET).
 *   REQUEST   accepted: for an NVMe function (which a channel entry named
 *             at acceptance, or the ask was refused before its query,
 *             CM_EJ_WHY_NO_CHANNEL), REMOVE_PENDING with a fresh sequence
 *             (R9a), T_ack measured from before its publication; an NVMe
 *             function the channel no longer names, or whose request was
 *             not published, takes the branch of no answer (isolation
 *             first, or the block). A unit that is not an NVMe function
 *             gets no request, its removal authorised by the query alone
 *             (record 24 section 7.1's removal).
 *   ANSWER    the answer read on every step, the deadline first (R11),
 *             twice unchanged (R6d) - CmNvlinkRemoveStep's rules.
 *   then R11 O5's branches, kept apart:
 *     CLEAN       REMOVE; withdrawn on success; then, under the USB4
 *                 profile with the port known by correspondence and the
 *                 population check passed, the port's PCIe teardown and,
 *                 on a system qualified for the statement, DOWN_QUIESCED
 *                 with no wait. Removed, clean.
 *     ISOLATED    REMOVE; withdrawn; the member's re-enumeration owed (the
 *                 drive returns as a fresh device on the same tunnel).
 *     FAILED, none within T_ack, late
 *                 the controller may still DMA: isolation first - under the
 *                 USB4 profile, the port known, the population check
 *                 passed and the system qualified, the teardown, its
 *                 read-back proving the path closed, DOWN_QUIESCED and the
 *                 R12 S4 wait, then REMOVE (removed after isolation).
 *                 Anything else is not isolated: no removal, the request
 *                 left in place, the member BLOCKED until the unplug (V6).
 *   REMOVE    Remove_SubTree every CM_NVLINK_RETRY_MS while it fails; at
 *             CM_NVLINK_RETRY_FOR_MS after the first attempt, measured also
 *             while a call still runs (R11 O6), the result BLOCKED and the
 *             member blocked, the retries going on (a veto honoured) until
 *             one succeeds, which lifts the block; under the USB4 profile
 *             with the check passed, the tunnel torn down once then. A
 *             late success is never a clean one.
 *   VANISHED  the unit's function no longer answers after its query was
 *             accepted (the unplug; its link-down reset the endpoint,
 *             assumption A1): the removal is authorised and made, which
 *             retires the devnode and ends any block (GONE where nothing
 *             else was reached).
 *
 * THE GATE. While a transaction is not retired (its Remove_SubTree
 * returned success, a refusal, or the devnode verified gone) its member
 * publishes no UP and is not re-enumerated (R8c, S5): CmEjGate, which
 * the seam reads beside the acknowledgement waits' gate.
 *
 * THE VANISHED CHILD (task 43.47; record 21 section 7.2, "The
 * vanished-child transaction"). A function a tunnel down took away is a
 * record of the same kind with CM_EJ_F_VANISH: no tool, no result slot,
 * no request, its devnode and instance id known from the member's sweep
 * (CM_EJ_SWEEP) before its first query (CmEjBeginVanished). QUERY is asked
 * while the member's removal gate is down; a refusal is not final - it is
 * asked again every CM_NVLINK_RETRY_MS, never forced, with no deadline
 * (R12 S4); accepted, the removal is authorised (A1) and REMOVE runs as
 * above, blocked after R11 O6's 30 s with no teardown under either
 * connection manager (V8), retried until it succeeds or the devnode is
 * verified gone. A stopped start's vanished record is resumed where it
 * stood by the adopting start, a refused query asked again. The domain's
 * CONFIGMG worker claims a devnode in the same records before its own
 * query (CM_EJ_P_CLAIMED, CmEjClaimBegin): a devnode a record names is
 * never queried by it, and a claim whose query was accepted and whose
 * removal failed becomes the vanished transaction (CmEjClaimEnd), never
 * queried again.
 *
 * THE STATEMENT DECISION (part 2). A 9x tunnel down's statement needs the
 * member's SHPC slot read empty, through PCI.VXD's dword write of the
 * capability's DWORD Select on the member's call worker: the decision is
 * entered BEFORE the read is posted, carries the connection's identity and
 * an absolute deadline, holds the member's removal until decided, and
 * takes the reading only when it came from this decision and before its
 * deadline (CM_EJ_DECISION).
 *
 * DDK-FREE PART. Everything declared here is pure; test\test_cm_eject.c
 * runs it on the host. The driver half is cm_ejectos.c.
 *
 * IRQL: any; the driver calls under its record lock.
 */

#ifndef CM_EJECT_H
#define CM_EJECT_H

#include "../xhci_compat.h"
#include "cm_ejask.h"

#define CM_EJ_NONE              0xFFFFFFFFUL
/* CM_EJ_FED_REQUEST's value for a function with an UP entry whose request
 * could not be published. */
#define CM_EJ_REQ_FAILED        0xFFFFFFFEUL

/* The phases of a transaction. */
#define CM_EJ_P_FREE            0UL
#define CM_EJ_P_IDENT           1UL     /* the devnode found (a call)       */
#define CM_EJ_P_QUERY           2UL     /* Query_Remove_SubTree (a call)    */
#define CM_EJ_P_REQUEST         3UL     /* REMOVE_PENDING, or none          */
#define CM_EJ_P_ANSWER          4UL     /* its answer stepped               */
#define CM_EJ_P_POPULATION      5UL     /* the population check (a call)    */
#define CM_EJ_P_TEARDOWN        6UL     /* the port's PCIe teardown         */
#define CM_EJ_P_MARK            7UL     /* DOWN_QUIESCED                    */
#define CM_EJ_P_ACK             8UL     /* ... and its R12 S4 wait          */
#define CM_EJ_P_REMOVE          9UL     /* Remove_SubTree (a call), retried */
#define CM_EJ_P_WITHDRAW        10UL    /* the request cleared (O5)         */
#define CM_EJ_P_REENUM          11UL    /* the member's re-enumeration owed */
#define CM_EJ_P_HELD            12UL    /* blocked, nothing to call: until
                                           the unplug                       */
#define CM_EJ_P_DONE            13UL    /* retired                          */
#define CM_EJ_P_RETIRE          14UL    /* a clean removal with no teardown:
                                           the entry leaves UP, no
                                           statement (task 43.18)           */
#define CM_EJ_P_CLAIMED         15UL    /* the domain's CONFIGMG worker is
                                           querying or removing the devnode
                                           (task 43.47): not stepped       */

/* What the driver is to do next (CmEjNext). */
#define CM_EJ_DO_NONE           0UL
#define CM_EJ_DO_FIND           1UL     /* submit: find the unit's devnode  */
#define CM_EJ_DO_QUERY          2UL     /* submit: Query_Remove_SubTree     */
#define CM_EJ_DO_REMOVE         3UL     /* submit: Remove_SubTree           */
#define CM_EJ_DO_POPULATION     4UL     /* submit: the population check     */
#define CM_EJ_DO_ADOPT          5UL     /* submit: find the devnode by id   */
#define CM_EJ_DO_REQUEST        6UL     /* REMOVE_PENDING, then CmEjFeed    */
#define CM_EJ_DO_WITHDRAW       7UL     /* clear the request                */
#define CM_EJ_DO_TEARDOWN       8UL     /* the port's PCIe teardown         */
#define CM_EJ_DO_MARK           9UL     /* DOWN_QUIESCED on the entry       */
#define CM_EJ_DO_REENUM         10UL    /* owe the member's re-enumeration  */
#define CM_EJ_DO_RETIRE         11UL    /* the entry's UP cleared, no
                                           DOWN_QUIESCED                    */

/* CmEjFeed's kinds: what came back. */
#define CM_EJ_FED_FIND          1UL     /* value: devnode, 0 none           */
#define CM_EJ_FED_QUERY         2UL     /* value: CONFIGRET                 */
#define CM_EJ_FED_REMOVE        3UL     /* value: CONFIGRET; aux 1: the
                                           USB4 profile, the port known     */
#define CM_EJ_FED_POPULATION    4UL     /* value: CM_EJ_POP_*               */
#define CM_EJ_FED_ADOPT         5UL     /* value: devnode, 0 gone; aux 1: the
                                           walk was complete                */
#define CM_EJ_FED_REQUEST       6UL     /* value: entry or CM_EJ_NONE, aux
                                           the sequence                     */
#define CM_EJ_FED_WITHDRAW      7UL     /* aux as CM_EJ_FED_REMOVE's        */
#define CM_EJ_FED_TEARDOWN      8UL     /* value: 1 the path read closed;
                                           aux 1: the system qualified      */
#define CM_EJ_FED_MARK          9UL     /* value: 1 DOWN_QUIESCED written   */
#define CM_EJ_FED_REENUM        10UL
#define CM_EJ_FED_RETIRE        11UL    /* value: 1 the entry left UP       */

/* The population check (record 24 section 5.4; the NT path's
 * cmEjectReady). */
#define CM_EJ_POP_PASSED        1UL     /* every other child of the member
                                           read not started, the list
                                           complete                         */
#define CM_EJ_POP_SIBLING       2UL     /* another child started            */
#define CM_EJ_POP_INCOMPLETE    3UL     /* the reading incomplete           */

/* CONFIGRET values the machine reads (cm_cfgmg.h). */
#define CM_EJ_CR_SUCCESS        0x00UL
#define CM_EJ_CR_NO_SUCH_DEVNODE 0x0DUL

/* Flags. */
#define CM_EJ_F_QUERIED         0x0001UL /* Query_Remove_SubTree accepted   */
#define CM_EJ_F_AUTH            0x0002UL /* the removal is authorised       */
#define CM_EJ_F_BLOCKED         0x0004UL /* the member is blocked           */
#define CM_EJ_F_VANISHED        0x0008UL /* the function left the bus       */
#define CM_EJ_F_TORN            0x0010UL /* the teardown was made           */
#define CM_EJ_F_REMOVED         0x0020UL /* Remove_SubTree returned success */
#define CM_EJ_F_TERMINAL        0x0040UL /* a terminal step was written     */
#define CM_EJ_F_DIRTY           0x0080UL /* the result to be written        */
#define CM_EJ_F_CONSUMED        0x0100UL /* the tool consumed the result    */
#define CM_EJ_F_ISOLATE         0x0200UL /* the population and teardown are
                                           the isolation (FAILED branch)    */
#define CM_EJ_F_O6TEAR          0x0400UL /* ... O6's teardown after the
                                           deadline                         */
#define CM_EJ_F_WITHDRAWN       0x0800UL /* the request was cleared         */
#define CM_EJ_F_POST            0x1000UL /* CLEAN's teardown after removal  */
#define CM_EJ_F_ADOPT           0x2000UL /* a later start looks the devnode
                                           up again by its instance id      */
#define CM_EJ_F_NOREQ           0x4000UL /* an UP entry, its request not
                                           published: no answer can come    */
#define CM_EJ_F_VANISH          0x8000UL /* a vanished child's transaction,
                                           or a CONFIGMG worker's claim
                                           (task 43.47)                     */
#define CM_EJ_F_PARKED          0x10000UL /* a claim whose query was
                                           accepted and whose removal
                                           failed, with no call worker to
                                           take it over: the CONFIGMG
                                           worker's to finish, removal
                                           only                             */

/* The inputs of one step. */
typedef struct _CM_EJ_IN {
    ULONG NowMs;            /* the transactions' clock                     */
    BOOLEAN SlotIdle;       /* the member's call worker can take a call    */
    BOOLEAN Nested;         /* a servicing point inside a wait: no
                               teardown, no mark, no re-enumeration         */
    BOOLEAN U4Known;        /* the USB4 profile, the member's port known by
                               correspondence                               */
    BOOLEAN Qualified;      /* the system admitted to the statement        */
    BOOLEAN AckHeld;        /* an acknowledgement wait pending behind the
                               member (the ACK phase's end)                 */
    BOOLEAN Vanished;       /* the unit's function no longer answers       */
    ULONG Answer;           /* in ANSWER: CmNvlinkRemoveStep's (CM_NVLINK_RM_*) */
} CM_EJ_IN;

typedef struct _CM_EJ_TX {
    ULONG Phase;            /* CM_EJ_P_*                                   */
    ULONG Flags;            /* CM_EJ_F_*                                   */
    ULONG Step;             /* CM_EJ_STEP_*, as last written               */
    ULONG Outcome;          /* CM_EJ_OUT_*                                 */
    ULONG Nonce;            /* the tool's ask                              */
    ULONG Bridge;           /* the member's location                       */
    ULONG Unit;             /* the unit's location                         */
    ULONG UnitId;           /* vendor 15:0, device 31:16 at acceptance     */
    ULONG Nvme;             /* class 01h subclass 08h                      */
    ULONG Devnode;
    ULONG QueryCr;
    ULONG RemoveCr;
    ULONG Entry;            /* the request's entry, CM_EJ_NONE for none    */
    ULONG Sequence;
    ULONG Answer;           /* CM_EJ_ANS_* once taken, 0 before            */
    ULONG ReqAt;            /* T_ack's start                               */
    ULONG FirstRemoveAt;
    ULONG LastRemoveAt;
    ULONG Attempts;
    ULONG Pending;          /* the call submitted: CM_EJ_DO_*, 0 none      */
    ULONG TerminalAt;
    ULONG StampAt;          /* the result's last write                     */
    ULONG Population;       /* CM_EJ_POP_* of the last check               */
    ULONG AdoptAt;          /* the last lookup by id that read the tree
                               incompletely                                 */
    ULONG Generation;       /* the request's LinkGeneration                */
    ULONG QueryAttempts;    /* a vanished child's queries asked            */
    ULONG LastQueryAt;      /* ... and the last one's submission           */
} CM_EJ_TX;

/* ---- the ask and its acceptance --------------------------------------- */

typedef struct _CM_EJ_ASK {
    ULONG Nonce;
    ULONG Member;
    ULONG Unit;
    ULONG EpochLo;
    ULONG EpochHi;
    ULONG ContentGen;
    ULONG Flags;
} CM_EJ_ASK;

/* The ask's bytes, `bytes` long: TRUE when it is CM_EJ_ASK_BYTES with a
 * nonzero nonce (the flags are checked by CmEjAccept). */
BOOLEAN CmEjAskDecode(const UCHAR *in, ULONG bytes, CM_EJ_ASK *ask);
/* The tool's side: the bytes of an ask. */
VOID CmEjAskEncode(UCHAR *out, const CM_EJ_ASK *ask);

/* What the thread read for an ask, before anything is called. */
typedef struct _CM_EJ_CHECK {
    BOOLEAN Available;      /* the 9x route and the member's worker exist,
                               the transport is live, the domain runs       */
    ULONG EpochLo;          /* this start's (CM_Q_O_EPOCH_*)               */
    ULONG EpochHi;
    ULONG ContentGen;       /* the query answer's current                  */
    BOOLEAN Member;         /* the ask's member is an exclusive, active
                               member of this domain's set                 */
    ULONG UnitId;           /* the id read at the unit's location (one
                               read: all ones or 0 when nothing answers)    */
    BOOLEAN Below;          /* the unit's bus is that member's secondary   */
    BOOLEAN Busy;           /* a transaction on that member, or a tool's
                               result not yet consumed in this domain       */
    BOOLEAN NoChannel;      /* an NVMe function no Stor98 entry names, with
                               a receiver that has read the channel         */
} CM_EJ_CHECK;

/* 0 to accept, else the reason (CM_EJ_WHY_*), in this order: bad, the
 * epoch, the generation, unavailable, the member, nothing there, not
 * below, no channel, busy. */
ULONG CmEjAccept(const CM_EJ_ASK *ask, const CM_EJ_CHECK *check);

/* The reply's bytes. */
VOID CmEjReplyEncode(UCHAR *out, ULONG nonce, ULONG reason);

/* ---- the transaction -------------------------------------------------- */

/* An accepted ask begins: IDENT, the step ACCEPTED, the result due. */
VOID CmEjBegin(CM_EJ_TX *t, const CM_EJ_ASK *ask, ULONG unitId,
               BOOLEAN nvme, ULONG nowMs);

/*
 * One step: what the driver is to do next (CM_EJ_DO_*), the state advanced
 * for whatever needs no call or table write. A DO_ submit is made only with
 * `in->SlotIdle`; the driver submits it and keeps it pending until its
 * result comes back through CmEjFeed (CmEjSubmitted records it). Called
 * again after each action, until CM_EJ_DO_NONE (bounded: no action is
 * returned twice without a feed or a passing deadline).
 */
ULONG CmEjNext(CM_EJ_TX *t, const CM_EJ_IN *in);

/* The submission made (or not: `ok` FALSE leaves it to the next step). */
VOID CmEjSubmitted(CM_EJ_TX *t, ULONG what, BOOLEAN ok, ULONG nowMs);

/* What came back for an action (CM_EJ_FED_*). A result of a call the
 * transaction no longer waits for is still taken for what it says (a late
 * success retires the devnode, a late query's CONFIGRET is kept). */
VOID CmEjFeed(CM_EJ_TX *t, ULONG kind, ULONG value, ULONG aux, ULONG nowMs);

/* The gate (R8c, S5): TRUE while the transaction is not retired. */
BOOLEAN CmEjGate(const CM_EJ_TX *t);

/* TRUE while it still has deadlines or calls to make, so the thread's
 * passes run at the waits' cadence (CM_NVLINK_PEND_MS); a blocked member
 * whose removal is not authorised waits for its unplug alone. */
BOOLEAN CmEjBusy(const CM_EJ_TX *t);

/* The result's bytes at `nowMs`, the stamp recorded, DIRTY cleared. */
VOID CmEjResultEncode(CM_EJ_TX *t, UCHAR *out, ULONG nowMs);

/* Whether the result should be written now: a step change, a retry
 * (DIRTY), or a pending wait unstamped for CM_EJ_RESTAMP_MS; never once
 * the tool consumed it. */
BOOLEAN CmEjResultDue(const CM_EJ_TX *t, ULONG nowMs);

/* The tool's consumption (the ask gone after a terminal step, or
 * CM_EJ_CONSUME_MS after it): TRUE when it happened at this call. */
BOOLEAN CmEjConsume(CM_EJ_TX *t, BOOLEAN askPresent, ULONG nowMs);

/* Whether the record can be let go: retired and consumed. */
BOOLEAN CmEjSpent(const CM_EJ_TX *t);

/*
 * A later start found the record of a stopped one (the member blocker,
 * record 21 section 7.2 part 0): the devnode is looked up again by its
 * instance id (CM_EJ_DO_ADOPT, once no call of the stopped start is still
 * outstanding) before anything else, and the transaction then resumes in
 * the phase it stood in; the result slot is no longer the tool's
 * (consumed). The query's recorded outcome is honoured and never
 * re-issued: a refusal, or no query made, owes nothing.
 */
VOID CmEjAdopt(CM_EJ_TX *t, ULONG nowMs);

/* ---- after the stop (the detached block) ------------------------------- */

/* Whether the removal is authorised and still owed: the record's own
 * worker may complete it after the NHI's stop, alone. */
BOOLEAN CmEjOwesRemoval(const CM_EJ_TX *t);

/* ---- the vanished child (task 43.47) ---------------------------------- */

/* A vetoed vanished child's next query (task 43.47f): CM_NVLINK_RETRY_MS
 * after the first refusal, doubled after each further one, at most
 * CM_EJ_VANISH_RETRY_MAX_MS. `attempts` the queries asked so far (>= 1). */
#define CM_EJ_VANISH_RETRY_MAX_MS  64000UL
ULONG CmEjVanishRetryMs(ULONG attempts);

/* An event that may have let the holder go - today a live tunnel up on
 * the member (the re-plug) - makes a vetoed vanished child's query due at
 * once and restarts its backoff. TRUE when it did. */
BOOLEAN CmEjNudge(CM_EJ_TX *t, ULONG nowMs);

/* A vanished child of member `bridge`, its devnode read by the sweep: QUERY
 * owed at once, no tool's result slot (consumed from the start). */
VOID CmEjBeginVanished(CM_EJ_TX *t, ULONG bridge, ULONG devnode,
                       ULONG nowMs);

/* The CONFIGMG worker's claim on member `bridge`'s `devnode`, made before
 * its Query_Remove_SubTree: CM_EJ_P_CLAIMED, which the thread never steps
 * and which holds the member's gate. */
VOID CmEjClaimBegin(CM_EJ_TX *t, ULONG bridge, ULONG devnode, ULONG nowMs);

/*
 * The worker's claim ended: its query's CONFIGRET (CM_EJ_NO_CR: no query
 * was made) and, after an accepted one, its last Remove_SubTree's, the
 * clock at its first and last attempts and their count, and the Stor98
 * request it left in place (`entry` CM_EJ_NONE: none; its sequence and
 * generation). A query not made, a removal that succeeded or found the
 * devnode gone, or a refusal of a loss's or removal-only request's claim
 * (their rule: settled): the record FREE, FALSE. With `takeover` (the
 * member's call worker live): a refusal of a vanished child's claim
 * (`vanished`) becomes the vanished transaction in QUERY, its retries
 * owed, TRUE; an accepted query whose removal failed, the vanished
 * transaction in REMOVE - authorised, never queried again, R11 O6 read
 * from `firstAt`, the request withdrawn by it on success - TRUE. Without
 * it a refusal frees the claim, and an accepted query whose removal
 * failed parks it (CM_EJ_F_PARKED, its request kept in it), FALSE.
 */
BOOLEAN CmEjClaimEnd(CM_EJ_TX *t, ULONG queryCr, ULONG removeCr,
                     ULONG firstAt, ULONG lastAt, ULONG attempts,
                     ULONG entry, ULONG sequence, ULONG generation,
                     BOOLEAN vanished, BOOLEAN takeover, ULONG nowMs);

/* A parked claim (CM_EJ_F_PARKED) taken up again by the CONFIGMG worker
 * for the same devnode: TRUE, the claim in use again, its query accepted
 * already - the worker makes the removal alone, never a second query. */
BOOLEAN CmEjClaimResume(CM_EJ_TX *t);

/* A parked claim's own retry, the CONFIGMG worker's scheduler (no call
 * worker): CM_EJ_DO_QUERY (a vanished child's refused query) or
 * CM_EJ_DO_REMOVE (an accepted one) once CM_NVLINK_RETRY_MS have passed
 * since its last attempt, the claim in use again; CM_EJ_DO_NONE
 * otherwise. Its end is CmEjClaimEnd again. */
ULONG CmEjClaimRetry(CM_EJ_TX *t, ULONG nowMs);

/* A parked claim found by a start whose member's call worker is live: the
 * vanished transaction in REMOVE (or QUERY, a refused one), as a takeover
 * makes it (TRUE). */
BOOLEAN CmEjClaimAdopt(CM_EJ_TX *t, ULONG nowMs);

/* The claim's query accepted and its first removal attempt about to be
 * made at `firstAt`: published before the call, so O6's 30 s are read
 * while it runs (CmEjClaimBlocked). */
VOID CmEjClaimProgress(CM_EJ_TX *t, ULONG firstAt);

/* TRUE when a claim's removal has run past R11 O6's deadline: the
 * member's blocked bit. */
BOOLEAN CmEjClaimBlocked(const CM_EJ_TX *t, ULONG nowMs);

/* The sweep of one member: which of its children vanished, one reading
 * at a time on its call worker (record 21 section 7.2, "The vanished-child
 * transaction"). */
#define CM_EJ_SW_IDLE           0UL
#define CM_EJ_SW_READ           1UL     /* a reading owed now              */
#define CM_EJ_SW_RETRY          2UL     /* ... at LastAt + the retry        */
#define CM_EJ_SW_OUT            3UL     /* submitted                       */

/* How a sweep ended. */
#define CM_EJ_SW_END_NONE       0UL     /* not ended                       */
#define CM_EJ_SW_END_DONE       1UL     /* a complete reading, none left   */

typedef struct _CM_EJ_SWEEP {
    ULONG State;            /* CM_EJ_SW_*                                  */
    ULONG BeganAt;
    ULONG LastAt;           /* the last reading taken                      */
    ULONG Readings;
    ULONG Records;          /* records it made                             */
    ULONG Again;            /* asked again while a reading was out         */
} CM_EJ_SWEEP;

/* A sweep asked: a reading owed at once (a sweep already running reads
 * again from now, its records kept). */
VOID CmEjSweepBegin(CM_EJ_SWEEP *s, ULONG nowMs);

/* TRUE when a reading is to be submitted now (`slotIdle`: the member's
 * worker can take it). */
BOOLEAN CmEjSweepNext(const CM_EJ_SWEEP *s, BOOLEAN slotIdle, ULONG nowMs);

/* A child a reading found when no record was free, kept by the driver,
 * made a record since: the next reading at once. */
VOID CmEjSweepMade(CM_EJ_SWEEP *s);

/* The reading submitted (`ok`), or withdrawn or lost (READ again). */
VOID CmEjSweepSubmitted(CM_EJ_SWEEP *s, BOOLEAN ok);
VOID CmEjSweepLost(CM_EJ_SWEEP *s);

/*
 * A reading taken: `complete` every child and the bus read; `found` a
 * vanished child no record names was reported, and `made` its record was
 * made (FALSE: no free record - the driver keeps the child and makes its
 * record later, CmEjSweepMade - or a record named it since); `claims`
 * vanished children held for now (a CONFIGMG worker's claim, an eject's
 * query not yet accepted). An incomplete reading is made again every
 * CM_NVLINK_RETRY_MS, with no deadline. Returns CM_EJ_SW_END_* (the
 * sweep IDLE unless NONE).
 */
ULONG CmEjSweepTake(CM_EJ_SWEEP *s, BOOLEAN complete, BOOLEAN found,
                    BOOLEAN made, ULONG claims, ULONG nowMs);

/* TRUE while the sweep runs: the member's gate. */
BOOLEAN CmEjSweepHeld(const CM_EJ_SWEEP *s);

/* Who names a child (CmEjSweepPick's `owner`). */
#define CM_EJ_OWN_NONE          0UL
#define CM_EJ_OWN_RECORD        1UL     /* a transaction's record          */
#define CM_EJ_OWN_CLAIM         2UL     /* held for now: a CONFIGMG worker's
                                           claim, an eject's query not yet
                                           accepted                        */

/*
 * The reading's verdict over a member's `n` children: child c is vanished
 * when its vendor and device (`childIds[c]`, read from its instance id; 0
 * not read: never vanished) is none of the `nIds` identities answering on
 * the member's secondary bus. The first vanished child no record or claim
 * names (CM_EJ_NONE for none); *claims the vanished children under a
 * claim.
 */
ULONG CmEjSweepPick(const ULONG *busIds, ULONG nIds, const ULONG *childIds,
                    const ULONG *owner, ULONG n, PULONG claims);

/* ---- the statement decision (part 2) ---------------------------------- */

#define CM_EJ_D_IDLE            0UL
#define CM_EJ_D_POSTED          1UL     /* entered, the reading owed       */
#define CM_EJ_D_DECIDED         2UL

/* The reading's verdict, as XHCI_TBT_PORT_* (tbt_topo.h), with the
 * worker's clock at the read. */
typedef struct _CM_EJ_DECISION {
    ULONG State;
    ULONG Bridge;           /* the member                                  */
    ULONG Member;
    ULONG Kind;             /* 1 ICM, 2 USB4                               */
    ULONG Serial;           /* the connection's identity: the decision's
                               own number, carried by its reading           */
    ULONG BeganAt;
    ULONG DeadlineAt;       /* absolute                                    */
    ULONG Reading;          /* XHCI_TBT_PORT_* once taken                  */
    ULONG Emulator;         /* the bridge read as the emulator's           */
    ULONG ReadAt;
    ULONG DecidedAt;        /* when the thread took it: the publication's
                               time, which the USB4 allowance is read at   */
    BOOLEAN PathClosed;     /* USB4: TBT_U4_LINKDOWN.PathClosed            */
} CM_EJ_DECISION;

/* Entered before the read is posted: the identity and the deadline,
 * `budgetMs` from `nowMs`. */
VOID CmEjDecideBegin(CM_EJ_DECISION *d, ULONG kind, ULONG member,
                     ULONG bridge, ULONG serial, ULONG nowMs,
                     ULONG budgetMs);

/* A reading came back: taken (DECIDED) only for this decision's serial;
 * one read, or taken by the thread (`nowMs`), at or after the deadline is
 * late - DECIDED with the reading XHCI_TBT_PORT_ERROR, so the verdict
 * withholds the statement (CM_NVLINK_NOT_DOWN). TRUE when this reading
 * decided it. */
BOOLEAN CmEjDecideTake(CM_EJ_DECISION *d, ULONG serial, ULONG reading,
                       BOOLEAN emulator, ULONG readAt, ULONG nowMs);

/* No reading before the deadline (the worker busy in another call):
 * DECIDED, the reading an error. TRUE when this call decided it. */
BOOLEAN CmEjDecideExpire(CM_EJ_DECISION *d, ULONG nowMs);

/* Milliseconds left before the deadline, 0 when spent. */
ULONG CmEjDecideLeft(const CM_EJ_DECISION *d, ULONG nowMs);

/*
 * The SHPC DWORD Select written through a dword-only route (PCI.VXD's
 * _PCI_Write_Config): a byte write at `offset` (width 1) becomes the
 * dword read at offset & ~3, that byte replaced and the others written as
 * read, the dword written back and read again; TRUE only when the byte
 * read back is the one written. A dword write passes through, read back.
 */
typedef ULONG (*CM_EJ_READ32)(PVOID context, ULONG bdf, ULONG offset,
                              PULONG value);
typedef ULONG (*CM_EJ_WRITE32)(PVOID context, ULONG bdf, ULONG offset,
                               ULONG value);
BOOLEAN CmEjByteWrite(CM_EJ_READ32 read, CM_EJ_WRITE32 write, PVOID context,
                      ULONG bdf, ULONG offset, ULONG width, ULONG value);

/* Record 21 section 7.2 part 2, decision V4: the systems admitted to the
 * 9x statement, each on its own gate-G3 reading. `me` TRUE for Windows
 * ME. Both are admitted (runs/run-43.md, 2026-10-22). */
BOOLEAN CmEjQualified9x(BOOLEAN me);
/* The 9x eject's own statements (the failed branch's isolation, the clean
 * removal's and O6's teardown under the USB4 profile) take the whole
 * statement qualification, not the systems' admission alone: both systems
 * admitted (until the driver tells them apart) and the member the
 * emulator's bridge (`emulator`: its QEMU vendor id, or the bus-0 bridge
 * above it, an SHPC capability and no PCI Express capability), the one
 * platform whose teardown is followed by the endpoint's reset (record 21
 * section 7.2, record 22: a closed path proves isolation, not the reset).
 * CM_EJ_IN.Qualified is this. */
BOOLEAN CmEjStatementQualified(BOOLEAN emulator);

/* ---- the driver (cm_ejectos.c) ---------------------------------------- */

/* A domain's share (CM_NHI.Ej), reset at each start. */
typedef struct _CM_EJ_DOM {
    ULONG Stepping;         /* CmEjectService in progress                  */
    ULONG AskRead;          /* the ask read once since the start: the nonce
                               found then is never taken                    */
    ULONG AskNonce;         /* the last nonce read                         */
    ULONG AskPresent;       /* ... and whether the value was there         */
    ULONG Owner;            /* the record holding the result slot, or
                               CM_EJ_NONE                                   */
    ULONG DecSerial;        /* the decisions' numbers                      */
    ULONG DecOp[4];         /* the reading submitted, per member           */
    CM_EJ_DECISION Dec[4];  /* per member (XHCI_TBT_MAX_TUNNELS)           */
    ULONG Accepted;
    ULONG Rejected;
    ULONG Writes;
    ULONG WriteFailures;
    ULONG Decided;
    ULONG DecidedLate;
    ULONG DownAt;           /* the tunnel-down action's start (the ICM
                               decision's allowance is counted from it)    */
    ULONG DownAtValid;
} CM_EJ_DOM;

#ifndef XHCI_HOST_TEST

struct _CM_NHI;

/* DriverEntry: the records' lock and the transactions' clock. */
VOID CmEjectInit(VOID);

/* Each start, before its thread's first pass: the domain's share reset;
 * every record this NHI owned is detached (a later start adopts it). */
VOID CmEjectReset(struct _CM_NHI *nhi);

/* The ask as the status pass read it (cm_statos.c, 98 SE and ME): `bytes`
 * of `ask`, or `present` FALSE. Accepts or rejects a new nonce at once,
 * this read being the one immediately before acceptance. PASSIVE_LEVEL,
 * on the thread. */
VOID CmEjectAsk(struct _CM_NHI *nhi, BOOLEAN present, const UCHAR *ask,
                ULONG bytes);

/* Every pass (CmReenumService) and every servicing point (`nested`,
 * from CmNvlinkStep): results taken, deadlines read, calls submitted;
 * a pass proper also tears down, marks, re-enumerates and writes the
 * result. Non-reentrant. PASSIVE_LEVEL, on the thread. */
VOID CmEjectService(struct _CM_NHI *nhi, BOOLEAN nested);

/* Member `bridge`'s gate beside the acknowledgement waits': a transaction
 * not retired (this start's, or a stopped one's not yet adopted), or a
 * statement decision pending. <= DISPATCH_LEVEL. */
BOOLEAN CmEjectHeld(struct _CM_NHI *nhi, ULONG bridge);

/* Any transaction or decision with a deadline or call pending (the
 * thread's 50 ms cadence). Any IRQL. */
BOOLEAN CmEjectPending(VOID);

/* Bits 3:0, member i of this start blocked; bits 7:4, member i with a
 * transaction in progress, not blocked (the status record's flags; a clear
 * bit does not say a removal ended or that nothing changed). */
ULONG CmEjectBlockedMask(struct _CM_NHI *nhi);

/* A tunnel down on member `member` of this start (XHCI_TBT_NO_MEMBER: every
 * member): a transaction there may take its unit's absence, read after it
 * and once the member's statement is decided and acknowledged or timed
 * out, as the unplug's isolation (assumption A1) - never a configuration
 * read's absence alone. On the thread. */
VOID CmEjectDown(struct _CM_NHI *nhi, ULONG member);

/*
 * The 9x statement decision (part 2): entered for member `member` before
 * its slot reading is posted to that member's call worker, `budgetMs`
 * from its anchor - for the ICM (`kind` 1) the tunnel-down action's start
 * (CmEjectDownBegin), so no scheduling delay extends the absolute
 * deadline; otherwise now; TRUE when entered (the caller answers
 * CM_NVLINK_PENDING and
 * the decision's completion publishes or withholds, CmNvlinkDecided).
 * FALSE when no worker can read: the caller decides at once.
 */
BOOLEAN CmEjectDecide(struct _CM_NHI *nhi, ULONG member, ULONG kind,
                      BOOLEAN pathClosed, ULONG budgetMs);

/* A tunnel-down action begins (CmReenumAction, before its poll): the
 * moment the ICM decision's 2 s are counted from. CmEjectDownLeft: what is
 * left of `budgetMs` since then (all of it when none was recorded). On the
 * thread. */
VOID CmEjectDownBegin(struct _CM_NHI *nhi);
ULONG CmEjectDownLeft(struct _CM_NHI *nhi, ULONG budgetMs);

/* The NHI's stop, before its call workers stop: every record it owns is
 * detached, and one whose removal is authorised and owed is handed to its
 * member's worker to complete alone (CM_CALL_K_DETACH). PASSIVE_LEVEL. */
VOID CmEjectStop(struct _CM_NHI *nhi);

/*
 * Task 43.47. The member `member`'s sweep for its vanished children, asked
 * where its re-enumeration would have removed them: TRUE when it was
 * entered (98 SE and ME, the member's call worker live; the caller has
 * read the host interface live) - the member's gate is then up and the
 * caller holds the re-enumeration without the removal, which is the
 * sweep's. FALSE: the caller posts the removal as before. On the thread.
 */
BOOLEAN CmEjectSweep(struct _CM_NHI *nhi, ULONG member);

/* Task 43.47f: a tunnel up on member `member` (XHCI_TBT_NO_MEMBER: every
 * member) makes each of this start's vetoed vanished children on it due
 * for its query at once (CmEjNudge). On the thread. */
VOID CmEjectNudge(struct _CM_NHI *nhi, ULONG member);

/* The transactions' clock (ms), for the CONFIGMG worker's attempts. Any
 * IRQL <= DISPATCH_LEVEL. */
ULONG CmEjectNow(VOID);

/*
 * The CONFIGMG worker's claim on member `bridge`'s `devnode` (instance id
 * `id`) before its Query_Remove_SubTree (record 21 section 7.2, "The
 * takeovers"): a nonzero handle, or 0 when a record already names that
 * devnode with that id (*owned TRUE: never call it) or no record is free
 * (*owned FALSE: not called now). *accepted TRUE when the handle is a
 * parked claim of the same devnode taken up again (CmEjClaimResume): its
 * query was accepted, so the worker makes the removal alone, with the
 * Stor98 request it left in place (*entry, CM_EJ_NONE none, *sequence,
 * *generation). <= DISPATCH_LEVEL; on that worker.
 */
ULONG CmEjectClaim(ULONG bridge, ULONG devnode, const char *id,
                   BOOLEAN *owned, BOOLEAN *accepted, PULONG entry,
                   PULONG sequence, PULONG generation);

/* The claim's query accepted, its first removal attempt about to be made
 * at `firstAt` (CmEjClaimProgress). <= DISPATCH_LEVEL. */
VOID CmEjectClaimAccepted(ULONG claim, ULONG firstAt);

/* The claim's end, as CmEjClaimEnd (`vanished`: a vanished child's
 * removal, not a loss's): TRUE when the record became the vanished
 * transaction - only with `takeover`, the member's call worker live (the
 * worker then leaves its request in place and makes no rescan of its
 * own); without it the claim is freed. <= DISPATCH_LEVEL. */
BOOLEAN CmEjectClaimEnd(ULONG claim, ULONG queryCr, ULONG removeCr,
                        ULONG firstAt, ULONG lastAt, ULONG attempts,
                        ULONG entry, ULONG sequence, ULONG generation,
                        BOOLEAN vanished, BOOLEAN takeover);

/*
 * The CONFIGMG worker's scheduler for its parked claims on member
 * `bridge` (no call worker to take them over): the next one due
 * (CmEjClaimRetry) as a claim in use - its devnode, instance id (at most
 * `idChars`), CM_EJ_DO_QUERY or CM_EJ_DO_REMOVE in *what, and the Stor98
 * request it keeps - or 0. Ended by CmEjectClaimEnd. CmEjectParkedAny:
 * whether member `bridge` has one parked (the worker's timed wait).
 * <= DISPATCH_LEVEL.
 */
ULONG CmEjectParkedNext(ULONG bridge, PULONG devnode, char *id,
                        ULONG idChars, PULONG what, PULONG entry,
                        PULONG sequence, PULONG generation);
BOOLEAN CmEjectParkedAny(ULONG bridge);

/* TRUE while a sweep, a vanished child's record (retired and not yet
 * pruned included) or a claim on member `bridge` stands: the CONFIGMG
 * worker makes no re-enumeration of it. Any IRQL <= DISPATCH_LEVEL; it
 * takes the records' lock, the innermost. */
BOOLEAN CmEjectVanishHeld(ULONG bridge);

#endif /* !XHCI_HOST_TEST */

#endif /* CM_EJECT_H */
