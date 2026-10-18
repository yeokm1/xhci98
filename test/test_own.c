/*
 * test_own.c - host vectors for endpoint ring ownership (src\xhci_own.c;
 * roadmap-hcd.md task 38.10; design record 20 section 5).
 *
 * Three parts. The decision table itself, cell by cell, as section 5.3
 * states it (the owner's one-setting STALL of 2026-10-14 among them). Then
 * every path through an interface switch - each Configure Endpoint outcome
 * (an unknown one in both worlds: the controller applied it, or did not),
 * each SET_INTERFACE outcome, the one-setting clears, the rollback's every
 * outcome and its clears, a step the phase did not wait for - driven over a
 * modelled controller (which ring each Endpoint Context names) and ring
 * pool, with hcd_cfg.c's executor mirrored: after every step no ring is
 * free while a context may name it, no two contexts name one ring, every
 * held ring has exactly one holder, an enabled endpoint with no published
 * pipe is marked stale, and the enabled contexts never pass the rings held
 * (so with 32 pooled rings and 32 slots, never 64). After the end, a later
 * proof and then the slot's going return every ring: nothing leaks. And the
 * one-setting descriptor walk.
 */

#include <stdio.h>
#include "../src/xhci_own.h"
#include "test_harness.h"

#define RINGS       32
#define NONE        (-1)
#define MAX_HELD    64

/* The modelled controller, pool and driver. */
typedef struct _MODEL {
    int Owner[RINGS];       /* 1 held by the device, 0 free in the pool    */
    int Ctx[32];            /* the ring each Endpoint Context names        */
    int Pub[32];            /* published pipes' rings                       */
    int Old[32];            /* the switch's detached pipes                 */
    int New[32];            /* the switch's new pipes                      */
    int HeldRing[MAX_HELD]; /* hcd_cfg.c's Retained list                   */
    int HeldDci[MAX_HELD];
    int HeldPinned[MAX_HELD];
    int Held;
    ULONG Stale;
    ULONG OldMask;
    ULONG NewMask;
    int Bad;                /* a ring freed twice or while named           */
} MODEL;

static void modelInit(MODEL *m)
{
    int i;

    for (i = 0; i < RINGS; i++) {
        m->Owner[i] = 0;
    }
    for (i = 0; i < 32; i++) {
        m->Ctx[i] = NONE;
        m->Pub[i] = NONE;
        m->Old[i] = NONE;
        m->New[i] = NONE;
    }
    m->Held = 0;
    m->Stale = 0;
    m->OldMask = 0;
    m->NewMask = 0;
    m->Bad = 0;
}

static int ringTake(MODEL *m)
{
    int i;

    for (i = 0; i < RINGS; i++) {
        if (m->Owner[i] == 0) {
            m->Owner[i] = 1;
            return i;
        }
    }
    return NONE;
}

static void ringFree(MODEL *m, int r)
{
    int d;

    if (r == NONE) {
        return;
    }
    if (m->Owner[r] == 0) {
        m->Bad = 1;
    }
    for (d = 0; d < 32; d++) {
        if (m->Ctx[d] == r) {
            m->Bad = 1;
        }
    }
    m->Owner[r] = 0;
}

static void holdRing(MODEL *m, int r, int dci, int pinned)
{
    if (m->Held >= MAX_HELD) {
        m->Bad = 1;
        return;
    }
    m->HeldRing[m->Held] = r;
    m->HeldDci[m->Held] = dci;
    m->HeldPinned[m->Held] = pinned;
    m->Held++;
    m->Stale |= 1UL << dci;
}

/* hcdCfgRetainedReclaim: a proof of `dropped`. */
static void reclaim(MODEL *m, ULONG dropped)
{
    int i;
    int j;

    for (i = 0; i < m->Held;) {
        if (!m->HeldPinned[i] &&
            (dropped & (1UL << m->HeldDci[i])) != 0) {
            ringFree(m, m->HeldRing[i]);
            for (j = i; j + 1 < m->Held; j++) {
                m->HeldRing[j] = m->HeldRing[j + 1];
                m->HeldDci[j] = m->HeldDci[j + 1];
                m->HeldPinned[j] = m->HeldPinned[j + 1];
            }
            m->Held--;
        } else {
            i++;
        }
    }
}

/* hcdCfgSwitchApply, on the model. */
static void apply(MODEL *m, ULONG a)
{
    int d;

    if ((a & XHCI_OWN_A_STALE_CLEARED) != 0) {
        m->Stale = 0;
    }
    for (d = 2; d < 32; d++) {
        if ((a & XHCI_OWN_A_FREE_OLD_STREAMS) != 0 && m->Old[d] != NONE &&
            m->Ctx[d] == m->Old[d]) {
            /* A stream block is freed only once no context names the
             * endpoint's old context. */
            m->Bad = 1;
        }
        if (m->New[d] != NONE) {
            if ((a & XHCI_OWN_A_FREE_NEW) != 0) {
                ringFree(m, m->New[d]);
                m->New[d] = NONE;
            } else if ((a & (XHCI_OWN_A_RETAIN_NEW | XHCI_OWN_A_PIN_NEW)) !=
                       0) {
                holdRing(m, m->New[d], d, (a & XHCI_OWN_A_PIN_NEW) != 0);
                m->New[d] = NONE;
            }
        }
        if (m->Old[d] != NONE) {
            if ((a & XHCI_OWN_A_FREE_OLD) != 0) {
                ringFree(m, m->Old[d]);
                m->Old[d] = NONE;
            } else if ((a & (XHCI_OWN_A_RETAIN_OLD | XHCI_OWN_A_PIN_OLD)) !=
                       0) {
                holdRing(m, m->Old[d], d, (a & XHCI_OWN_A_PIN_OLD) != 0);
                m->Old[d] = NONE;
            }
        }
    }
    for (d = 2; d < 32; d++) {
        if ((a & XHCI_OWN_A_PUBLISH_NEW) != 0 && m->New[d] != NONE) {
            m->Pub[d] = m->New[d];
            m->New[d] = NONE;
        }
        if ((a & XHCI_OWN_A_REATTACH_OLD) != 0 && m->Old[d] != NONE) {
            m->Pub[d] = m->Old[d];
            m->Old[d] = NONE;
        }
    }
}

/* The invariants, after every step. */
static void invariants(MODEL *m, const char *what)
{
    int holders[RINGS];
    int named[RINGS];
    int ctxs;
    int held;
    int d;
    int i;

    for (i = 0; i < RINGS; i++) {
        holders[i] = 0;
        named[i] = 0;
    }
    ctxs = 0;
    for (d = 2; d < 32; d++) {
        if (m->Pub[d] != NONE) {
            holders[m->Pub[d]]++;
        }
        if (m->Old[d] != NONE) {
            holders[m->Old[d]]++;
        }
        if (m->New[d] != NONE) {
            holders[m->New[d]]++;
        }
        if (m->Ctx[d] != NONE) {
            ctxs++;
            named[m->Ctx[d]]++;
            check_impl(m->Owner[m->Ctx[d]] != 0, what, __FILE__, __LINE__);
            /* An enabled endpoint with no published pipe is stale, or is
             * pinned: the next command drops it. */
            check_impl(m->Pub[d] != NONE || (m->Stale & (1UL << d)) != 0 ||
                           m->Old[d] != NONE || m->New[d] != NONE,
                       what, __FILE__, __LINE__);
        }
    }
    for (i = 0; i < m->Held; i++) {
        holders[m->HeldRing[i]]++;
        if (!m->HeldPinned[i]) {
            check_impl((m->Stale & (1UL << m->HeldDci[i])) != 0, what,
                       __FILE__, __LINE__);
        }
    }
    held = 0;
    for (i = 0; i < RINGS; i++) {
        if (m->Owner[i] != 0) {
            held++;
            check_impl(holders[i] == 1, what, __FILE__, __LINE__);
        } else {
            check_impl(holders[i] == 0 && named[i] == 0, what, __FILE__,
                       __LINE__);
        }
        check_impl(named[i] <= 1, what, __FILE__, __LINE__);
    }
    check_impl(ctxs <= held, what, __FILE__, __LINE__);
    check_impl(ctxs + 32 <= 64, what, __FILE__, __LINE__);
    check_impl(!m->Bad, what, __FILE__, __LINE__);
}

/* What the controller does with a Configure Endpoint that took effect:
 * `drop` cleared, `add` named from `rings`. */
static void ctxApply(MODEL *m, ULONG drop, ULONG add, const int *rings)
{
    int d;

    for (d = 2; d < 32; d++) {
        if ((drop & (1UL << d)) != 0) {
            m->Ctx[d] = NONE;
        }
    }
    for (d = 2; d < 32; d++) {
        if ((add & (1UL << d)) != 0) {
            m->Ctx[d] = rings[d];
        }
    }
}

/* A start: `keep` and `oldm` published and enabled, `stale` held and
 * enabled from an earlier failure, `newm` to be made. */
static void modelStart(MODEL *m, ULONG keep, ULONG oldm, ULONG stale,
                       ULONG newm)
{
    int d;
    int r;

    modelInit(m);
    for (d = 2; d < 32; d++) {
        if (((keep | oldm) & (1UL << d)) != 0) {
            r = ringTake(m);
            m->Pub[d] = r;
            m->Ctx[d] = r;
        }
        if ((stale & (1UL << d)) != 0) {
            r = ringTake(m);
            m->Ctx[d] = r;
            holdRing(m, r, d, 0);
        }
    }
    /* Step 1: detached. */
    for (d = 2; d < 32; d++) {
        if ((oldm & (1UL << d)) != 0) {
            m->Old[d] = m->Pub[d];
            m->Pub[d] = NONE;
        }
    }
    m->OldMask = oldm;
    /* Step 2: made. */
    for (d = 2; d < 32; d++) {
        if ((newm & (1UL << d)) != 0) {
            m->New[d] = ringTake(m);
        }
    }
    m->NewMask = newm;
}

/* The worlds a command outcome may leave the controller in. */
#define W_NOT_ISSUED    0
#define W_SUCCESS       1
#define W_REFUSED       2
#define W_UNKNOWN_DONE  3   /* no completion seen; it had taken effect   */
#define W_UNKNOWN_NOT   4   /* no completion seen; it had not            */
#define W_COUNT         5

static ULONG worldEvent(int w)
{
    switch (w) {
    case W_NOT_ISSUED:
        return XHCI_OWN_EV_CMD_NOT_ISSUED;
    case W_SUCCESS:
        return XHCI_OWN_EV_CMD_SUCCESS;
    case W_REFUSED:
        return XHCI_OWN_EV_CMD_REFUSED;
    default:
        return XHCI_OWN_EV_CMD_UNKNOWN;
    }
}

static int pathCount;
static int endCount[5];

/* A finished switch: its end checked, then a later Configure Endpoint that
 * drops every stale endpoint, then the slot's going - after which every
 * ring is back in the pool. */
static void finish(MODEL *m, ULONG answer, const char *what)
{
    int d;
    int i;

    invariants(m, what);
    for (d = 2; d < 32; d++) {
        check_impl(m->Old[d] == NONE && m->New[d] == NONE, what, __FILE__,
                   __LINE__);
    }
    if (answer == XHCI_OWN_ANS_SUCCESS) {
        for (d = 2; d < 32; d++) {
            if ((m->NewMask & (1UL << d)) != 0) {
                check_impl(m->Pub[d] != NONE && m->Ctx[d] == m->Pub[d],
                           what, __FILE__, __LINE__);
            }
        }
    }
    if (answer < 5) {
        endCount[answer]++;
    }
    ctxApply(m, m->Stale, 0, NULL);
    reclaim(m, m->Stale);
    m->Stale = 0;
    for (i = 0; i < m->Held; i++) {
        check_impl(m->HeldPinned[i], what, __FILE__, __LINE__);
    }
    invariants(m, what);
    for (d = 2; d < 32; d++) {
        m->Ctx[d] = NONE;
    }
    for (d = 2; d < 32; d++) {
        ringFree(m, m->Pub[d]);
        m->Pub[d] = NONE;
    }
    for (i = 0; i < m->Held; i++) {
        ringFree(m, m->HeldRing[i]);
    }
    m->Held = 0;
    for (i = 0; i < RINGS; i++) {
        check_impl(m->Owner[i] == 0, what, __FILE__, __LINE__);
    }
    check_impl(!m->Bad, what, __FILE__, __LINE__);
    pathCount++;
}

/* Every path from `phase` on: each event the phase waits for, and one it
 * does not (the slip), each command outcome in both unknown worlds. */
static void explore(const MODEL *m0, const XHCI_OWN_SELECT *s0, int depth,
                    const char *what)
{
    static const ULONG setif[] = {XHCI_OWN_EV_SETIF_DONE,
                                  XHCI_OWN_EV_SETIF_STALLED,
                                  XHCI_OWN_EV_SETIF_FAILED,
                                  XHCI_OWN_EV_CLEARS_DONE};
    static const ULONG clears[] = {XHCI_OWN_EV_CLEARS_DONE,
                                   XHCI_OWN_EV_CLEARS_FAILED,
                                   XHCI_OWN_EV_SETIF_DONE};
    MODEL m;
    XHCI_OWN_SELECT s;
    ULONG event;
    ULONG answer;
    ULONG actions;
    ULONG dropped;
    int n;
    int i;

    if (depth > 6) {
        check_impl(0, "a switch that never ends", __FILE__, __LINE__);
        return;
    }
    if (s0->Phase == XHCI_OWN_PH_CONFIGURE ||
        s0->Phase == XHCI_OWN_PH_ROLLBACK) {
        n = W_COUNT + 1;
    } else if (s0->Phase == XHCI_OWN_PH_SETIF) {
        n = 4;
    } else {
        n = 3;
    }
    for (i = 0; i < n; i++) {
        m = *m0;
        s = *s0;
        if (s.Phase == XHCI_OWN_PH_CONFIGURE ||
            s.Phase == XHCI_OWN_PH_ROLLBACK) {
            event = (i < W_COUNT) ? worldEvent(i) : XHCI_OWN_EV_SETIF_DONE;
            if (i == W_SUCCESS || i == W_UNKNOWN_DONE) {
                if (s.Phase == XHCI_OWN_PH_CONFIGURE) {
                    dropped = m.OldMask | m.Stale;
                    ctxApply(&m, dropped, m.NewMask, m.New);
                } else {
                    dropped = m.NewMask;
                    ctxApply(&m, dropped, m.OldMask, m.Old);
                }
                if (i == W_SUCCESS) {
                    /* hcdCfgCommand's reclaim, inside the command. */
                    reclaim(&m, dropped);
                }
            }
        } else if (s.Phase == XHCI_OWN_PH_SETIF) {
            event = setif[i];
        } else {
            event = clears[i];
        }
        actions = XhciOwnSelectStep(&s, event, &answer);
        apply(&m, actions);
        invariants(&m, what);
        if (s.Phase == XHCI_OWN_PH_END) {
            check_impl(answer != XHCI_OWN_ANS_PENDING, what, __FILE__,
                       __LINE__);
            finish(&m, answer, what);
        } else {
            check_impl(answer == XHCI_OWN_ANS_PENDING, what, __FILE__,
                       __LINE__);
            explore(&m, &s, depth + 1, what);
        }
    }
}

static void test_paths(void)
{
    static const ULONG starts[][4] = {
        /* keep, old, stale, new */
        {0x004UL, 0x018UL, 0x020UL, 0x048UL}, /* overlap at 3, a stale 5 */
        {0x000UL, 0x000UL, 0x000UL, 0x008UL}, /* alternate 0 to one     */
        {0x004UL, 0x008UL, 0x000UL, 0x000UL}, /* one to none            */
        {0x000UL, 0x000UL, 0x080UL, 0x000UL}, /* nothing but a stale 7  */
        {0x000UL, 0x018UL, 0x000UL, 0x018UL}, /* the same setting again */
        {0x0C0UL, 0x01CUL, 0x300UL, 0x01CUL}  /* wider, two stale       */
    };
    MODEL m;
    XHCI_OWN_SELECT s;
    ULONG rollback;
    ULONG single;
    ULONG streams;
    ULONG k;

    for (k = 0; k < sizeof(starts) / sizeof(starts[0]); k++) {
        for (rollback = 0; rollback <= 1; rollback++) {
            for (single = 0; single <= 1; single++) {
                for (streams = 0; streams <= 1; streams++) {
                    modelStart(&m, starts[k][0], starts[k][1], starts[k][2],
                               starts[k][3]);
                    invariants(&m, "the start");
                    XhciOwnSelectBegin(&s, rollback, single, streams);
                    explore(&m, &s, 0, "a switch path");
                }
            }
        }
    }
    printf("  %d switch paths explored\n", pathCount);
    /* 540 at the time of writing: six starts, eight flag sets. */
    CHECK(pathCount >= 500, "every path reached");
    CHECK(endCount[XHCI_OWN_ANS_SUCCESS] > 0, "successes reached");
    CHECK(endCount[XHCI_OWN_ANS_COMMAND] > 0, "command refusals reached");
    CHECK(endCount[XHCI_OWN_ANS_STALL] > 0, "STALL ends reached");
    CHECK(endCount[XHCI_OWN_ANS_FAILED] > 0, "not-established ends reached");
}

/* One step from a phase, by its event: actions and answer. */
static ULONG stepFrom(ULONG phase, ULONG rollback, ULONG single,
                      ULONG streams, ULONG event, PULONG answer,
                      PULONG next)
{
    XHCI_OWN_SELECT s;
    ULONG actions;

    XhciOwnSelectBegin(&s, rollback, single, streams);
    s.Phase = phase;
    actions = XhciOwnSelectStep(&s, event, answer);
    *next = s.Phase;
    return actions;
}

static void test_table(void)
{
    ULONG answer;
    ULONG next;
    ULONG a;

    /* Step 3. */
    a = stepFrom(XHCI_OWN_PH_CONFIGURE, 1, 0, 0, XHCI_OWN_EV_CMD_REFUSED,
                 &answer, &next);
    CHECK_EQ(a, XHCI_OWN_A_FREE_NEW | XHCI_OWN_A_REATTACH_OLD,
             "refused: new freed, old put back");
    CHECK_EQ(answer, XHCI_OWN_ANS_COMMAND, "refused: the command's status");
    CHECK_EQ(next, XHCI_OWN_PH_END, "refused ends it");
    a = stepFrom(XHCI_OWN_PH_CONFIGURE, 1, 0, 0, XHCI_OWN_EV_CMD_NOT_ISSUED,
                 &answer, &next);
    CHECK_EQ(a, XHCI_OWN_A_FREE_NEW | XHCI_OWN_A_REATTACH_OLD,
             "not issued: as refused");
    a = stepFrom(XHCI_OWN_PH_CONFIGURE, 1, 0, 0, XHCI_OWN_EV_CMD_UNKNOWN,
                 &answer, &next);
    CHECK_EQ(a, XHCI_OWN_A_PIN_NEW | XHCI_OWN_A_PIN_OLD,
             "unknown: both pinned until the slot goes");
    CHECK_EQ(answer, XHCI_OWN_ANS_COMMAND, "unknown: the command's status");
    a = stepFrom(XHCI_OWN_PH_CONFIGURE, 1, 0, 0, XHCI_OWN_EV_CMD_SUCCESS,
                 &answer, &next);
    CHECK_EQ(a, XHCI_OWN_A_STALE_CLEARED | XHCI_OWN_A_FREE_OLD_STREAMS |
                    XHCI_OWN_A_SET_INTERFACE,
             "success: stale cleared, old streams freed, SET_INTERFACE");
    CHECK_EQ(next, XHCI_OWN_PH_SETIF, "success waits for SET_INTERFACE");
    CHECK_EQ(answer, XHCI_OWN_ANS_PENDING, "not ended");

    /* Step 4. */
    a = stepFrom(XHCI_OWN_PH_SETIF, 1, 0, 0, XHCI_OWN_EV_SETIF_DONE, &answer,
                 &next);
    CHECK_EQ(a, XHCI_OWN_A_FREE_OLD | XHCI_OWN_A_PUBLISH_NEW,
             "completed: published");
    CHECK_EQ(answer, XHCI_OWN_ANS_SUCCESS, "completed: success");
    a = stepFrom(XHCI_OWN_PH_SETIF, 1, 0, 0, XHCI_OWN_EV_SETIF_FAILED,
                 &answer, &next);
    CHECK_EQ(a, XHCI_OWN_A_RETAIN_NEW | XHCI_OWN_A_FREE_OLD,
             "not established: new held stale, old freed");
    CHECK_EQ(answer, XHCI_OWN_ANS_FAILED, "not established: failed");
    /* The owner's decision of 2026-10-14: a one-setting STALL is a
     * success through the clears. */
    a = stepFrom(XHCI_OWN_PH_SETIF, 1, 1, 0, XHCI_OWN_EV_SETIF_STALLED,
                 &answer, &next);
    CHECK_EQ(a, XHCI_OWN_A_CLEAR_NEW, "one setting, STALL: the clears");
    CHECK_EQ(next, XHCI_OWN_PH_CLEARS, "waits for the clears");
    a = stepFrom(XHCI_OWN_PH_SETIF, 1, 1, 1, XHCI_OWN_EV_SETIF_STALLED,
                 &answer, &next);
    CHECK_EQ(a, XHCI_OWN_A_CLEAR_NEW,
             "one setting, STALL, old streams: the clears all the same");
    a = stepFrom(XHCI_OWN_PH_CLEARS, 1, 1, 0, XHCI_OWN_EV_CLEARS_DONE,
                 &answer, &next);
    CHECK_EQ(a, XHCI_OWN_A_FREE_OLD | XHCI_OWN_A_PUBLISH_NEW,
             "the clears done: as completed");
    CHECK_EQ(answer, XHCI_OWN_ANS_SUCCESS, "the clears done: success");
    a = stepFrom(XHCI_OWN_PH_CLEARS, 1, 1, 0, XHCI_OWN_EV_CLEARS_FAILED,
                 &answer, &next);
    CHECK_EQ(a, XHCI_OWN_A_ROLLBACK, "a clear failed: the STALL branch");
    CHECK_EQ(next, XHCI_OWN_PH_ROLLBACK, "waits for the rollback");
    a = stepFrom(XHCI_OWN_PH_CLEARS, 1, 1, 1, XHCI_OWN_EV_CLEARS_FAILED,
                 &answer, &next);
    CHECK_EQ(a, XHCI_OWN_A_RETAIN_NEW | XHCI_OWN_A_FREE_OLD,
             "a clear failed, old streams: as not established");
    CHECK_EQ(answer, XHCI_OWN_ANS_STALL, "answered with the STALL");
    /* A STALL on an interface with other settings: unchanged. */
    a = stepFrom(XHCI_OWN_PH_SETIF, 1, 0, 0, XHCI_OWN_EV_SETIF_STALLED,
                 &answer, &next);
    CHECK_EQ(a, XHCI_OWN_A_ROLLBACK, "other settings, STALL: rollback");
    a = stepFrom(XHCI_OWN_PH_SETIF, 1, 0, 1, XHCI_OWN_EV_SETIF_STALLED,
                 &answer, &next);
    CHECK_EQ(a, XHCI_OWN_A_RETAIN_NEW | XHCI_OWN_A_FREE_OLD,
             "other settings, STALL, old streams: not established");
    CHECK_EQ(answer, XHCI_OWN_ANS_STALL, "answered with the STALL");
    /* A function's select re-instates nothing (section 5.4). */
    a = stepFrom(XHCI_OWN_PH_SETIF, 0, 1, 0, XHCI_OWN_EV_SETIF_STALLED,
                 &answer, &next);
    CHECK_EQ(a, XHCI_OWN_A_RETAIN_NEW | XHCI_OWN_A_FREE_OLD,
             "a function: no rollback, no clears");

    /* The rollback's three outcomes, then its clears. */
    a = stepFrom(XHCI_OWN_PH_ROLLBACK, 1, 0, 0, XHCI_OWN_EV_CMD_REFUSED,
                 &answer, &next);
    CHECK_EQ(a, XHCI_OWN_A_RETAIN_NEW | XHCI_OWN_A_FREE_OLD,
             "rollback refused: new held stale, old freed");
    CHECK_EQ(answer, XHCI_OWN_ANS_STALL, "rollback refused: the STALL");
    a = stepFrom(XHCI_OWN_PH_ROLLBACK, 1, 0, 0, XHCI_OWN_EV_CMD_NOT_ISSUED,
                 &answer, &next);
    CHECK_EQ(a, XHCI_OWN_A_RETAIN_NEW | XHCI_OWN_A_FREE_OLD,
             "rollback not issued: as refused");
    a = stepFrom(XHCI_OWN_PH_ROLLBACK, 1, 0, 0, XHCI_OWN_EV_CMD_UNKNOWN,
                 &answer, &next);
    CHECK_EQ(a, XHCI_OWN_A_PIN_NEW | XHCI_OWN_A_PIN_OLD,
             "rollback unknown: both pinned");
    CHECK_EQ(answer, XHCI_OWN_ANS_STALL, "rollback unknown: the STALL");
    a = stepFrom(XHCI_OWN_PH_ROLLBACK, 1, 0, 0, XHCI_OWN_EV_CMD_SUCCESS,
                 &answer, &next);
    CHECK_EQ(a, XHCI_OWN_A_FREE_NEW | XHCI_OWN_A_CLEAR_OLD,
             "rollback success: new freed, the old's clears");
    CHECK_EQ(next, XHCI_OWN_PH_RECLEARS, "waits for them");
    a = stepFrom(XHCI_OWN_PH_RECLEARS, 1, 0, 0, XHCI_OWN_EV_CLEARS_DONE,
                 &answer, &next);
    CHECK_EQ(a, XHCI_OWN_A_REATTACH_OLD, "old put back, own handles");
    CHECK_EQ(answer, XHCI_OWN_ANS_STALL, "still the STALL");
    a = stepFrom(XHCI_OWN_PH_RECLEARS, 1, 0, 0, XHCI_OWN_EV_CLEARS_FAILED,
                 &answer, &next);
    CHECK_EQ(a, XHCI_OWN_A_RETAIN_OLD,
             "a re-clear failed: old installed, held closed and stale");
    CHECK_EQ(answer, XHCI_OWN_ANS_STALL, "still the STALL");

    /* A step the phase does not wait for, and one after the end. */
    a = stepFrom(XHCI_OWN_PH_SETIF, 1, 0, 0, XHCI_OWN_EV_CMD_SUCCESS,
                 &answer, &next);
    CHECK_EQ(a, XHCI_OWN_A_PIN_NEW | XHCI_OWN_A_PIN_OLD,
             "a slip holds everything");
    CHECK_EQ(next, XHCI_OWN_PH_END, "and ends");
    a = stepFrom(XHCI_OWN_PH_END, 1, 0, 0, XHCI_OWN_EV_SETIF_DONE, &answer,
                 &next);
    CHECK_EQ(a, XHCI_OWN_A_PIN_NEW | XHCI_OWN_A_PIN_OLD,
             "a step after the end holds everything");

    /* The drop-only command of a function's unconfigure or release. */
    CHECK_EQ(XhciOwnDropStep(XHCI_OWN_EV_CMD_SUCCESS),
             XHCI_OWN_A_STALE_CLEARED | XHCI_OWN_A_FREE_OLD_STREAMS |
                 XHCI_OWN_A_FREE_OLD,
             "drop succeeded: freed");
    CHECK_EQ(XhciOwnDropStep(XHCI_OWN_EV_CMD_REFUSED), XHCI_OWN_A_RETAIN_OLD,
             "drop refused: held stale");
    CHECK_EQ(XhciOwnDropStep(XHCI_OWN_EV_CMD_NOT_ISSUED),
             XHCI_OWN_A_RETAIN_OLD, "drop not issued: held stale");
    CHECK_EQ(XhciOwnDropStep(XHCI_OWN_EV_CMD_UNKNOWN), XHCI_OWN_A_PIN_OLD,
             "drop unknown: pinned");

    /* Completion codes. */
    CHECK_EQ(XhciOwnCommandEvent(XHCI_OWN_NOT_ISSUED_CODE),
             XHCI_OWN_EV_CMD_NOT_ISSUED, "the sentinel");
    CHECK_EQ(XhciOwnCommandEvent(0), XHCI_OWN_EV_CMD_UNKNOWN, "0 unknown");
    CHECK_EQ(XhciOwnCommandEvent(1), XHCI_OWN_EV_CMD_SUCCESS, "1 success");
    CHECK_EQ(XhciOwnCommandEvent(7), XHCI_OWN_EV_CMD_REFUSED,
             "Resource Error refused");
    CHECK_EQ(XhciOwnCommandEvent(8), XHCI_OWN_EV_CMD_REFUSED,
             "Bandwidth Error refused");
}

/* The drop-only command over the model: every outcome, both unknown
 * worlds. */
static void test_drop_paths(void)
{
    MODEL m;
    ULONG actions;
    ULONG dropped;
    int w;

    for (w = 0; w < W_COUNT; w++) {
        modelStart(&m, 0x004UL, 0x018UL, 0x020UL, 0);
        if (w == W_SUCCESS || w == W_UNKNOWN_DONE) {
            dropped = m.OldMask | m.Stale;
            ctxApply(&m, dropped, 0, NULL);
            if (w == W_SUCCESS) {
                reclaim(&m, dropped);
            }
        }
        actions = XhciOwnDropStep(worldEvent(w));
        apply(&m, actions);
        invariants(&m, "a drop's outcome");
        finish(&m, 0xFFUL, "a drop's outcome");
    }
}

/* A configuration of `n` interface descriptors: {number, alternate}. */
static ULONG buildConfig(UCHAR *c, const UCHAR (*ifs)[2], ULONG n)
{
    ULONG at;
    ULONG i;

    c[0] = 9;
    c[1] = 2;
    c[4] = (UCHAR)n;
    at = 9;
    for (i = 0; i < n; i++) {
        c[at] = 9;
        c[at + 1] = 4;
        c[at + 2] = ifs[i][0];
        c[at + 3] = ifs[i][1];
        c[at + 4] = 0;
        c[at + 5] = 3;
        c[at + 6] = 0;
        c[at + 7] = 0;
        c[at + 8] = 0;
        at += 9;
        /* An endpoint between, so the walk steps over another kind. */
        c[at] = 7;
        c[at + 1] = 5;
        c[at + 2] = (UCHAR)(0x81 + i);
        c[at + 3] = 3;
        c[at + 4] = 8;
        c[at + 5] = 0;
        c[at + 6] = 10;
        at += 7;
    }
    c[2] = (UCHAR)(at & 0xFF);
    c[3] = (UCHAR)(at >> 8);
    return at;
}

static void test_single_setting(void)
{
    static const UCHAR ifs[][2] = {{0, 0}, {1, 0}, {1, 1}, {2, 0}};
    UCHAR c[256];
    ULONG len;

    len = buildConfig(c, ifs, 4);
    CHECK_EQ(XhciOwnSingleSetting(c, len, 0), 1UL, "interface 0: one");
    CHECK_EQ(XhciOwnSingleSetting(c, len, 1), 0UL, "interface 1: two");
    CHECK_EQ(XhciOwnSingleSetting(c, len, 2), 1UL, "interface 2: one");
    CHECK_EQ(XhciOwnSingleSetting(c, len, 3), 0UL, "interface 3: none");
    /* Walked to wTotalLength, not past it. */
    CHECK_EQ(XhciOwnSingleSetting(c, len + 9, 2), 1UL,
             "bytes past wTotalLength ignored");
    /* A walk that cannot finish answers 0. */
    c[9 + 9 + 7] = 0;
    CHECK_EQ(XhciOwnSingleSetting(c, len, 0), 0UL, "a zero bLength");
    len = buildConfig(c, ifs, 4);
    c[9] = 8;
    CHECK_EQ(XhciOwnSingleSetting(c, len, 2), 0UL,
             "an interface descriptor too short");
    len = buildConfig(c, ifs, 4);
    c[9 + 9] = 200;
    CHECK_EQ(XhciOwnSingleSetting(c, len, 2), 0UL, "an overlong bLength");
    len = buildConfig(c, ifs, 4);
    CHECK_EQ(XhciOwnSingleSetting(c, len - 1, 2), 0UL,
             "a descriptor running past the bytes given");
    /* Bytes ending cleanly after interface 1's alternate 0, its alternate 1
     * past them: not one setting. */
    CHECK_EQ(XhciOwnSingleSetting(c, 9 + 16 + 16, 1), 0UL,
             "bytes short of wTotalLength");
    c[1] = 4;
    CHECK_EQ(XhciOwnSingleSetting(c, len, 0), 0UL, "not a configuration");
    CHECK_EQ(XhciOwnSingleSetting(NULL, len, 0), 0UL, "no descriptor");
    CHECK_EQ(XhciOwnSingleSetting(c, 8, 0), 0UL, "a header too short");
    /* Two descriptors both claiming alternate 0 are not one setting. */
    {
        static const UCHAR dup[][2] = {{0, 0}, {0, 0}};
        len = buildConfig(c, dup, 2);
        CHECK_EQ(XhciOwnSingleSetting(c, len, 0), 0UL,
                 "a repeated descriptor");
    }
}

int main(void)
{
    test_table();
    test_paths();
    test_drop_paths();
    test_single_setting();
    printf("test_own: %d checks, %d failures\n", checks, failures);
    return failures;
}
