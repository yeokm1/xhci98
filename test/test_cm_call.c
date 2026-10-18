/*
 * test_cm_call.c - the per-member call workers' handoff word (src/cm/
 * cm_call.c; roadmap-usb4.md task 42-A.12; design record 21 section 7.2
 * part 0, stor98cm.h interface 3's R6f): the transitions, a stale id,
 * a withdrawal, a deadline that passes while a call runs and the late
 * result taken by its id, two members' slots independent of each other,
 * and a "next operation / take result" client driven through one slot as
 * design record 28's state machine (task 42-A.15) is to be.
 */

#include <stdio.h>
#include <string.h>
#include "../src/cm/cm_call.h"
#include "test_harness.h"

/* A fake executor: the result is the operation's first word plus one,
 * and the number of runs is counted through the context. */
static ULONG fakeRun(PVOID route, PVOID context, const VOID *op, VOID *res)
{
    const ULONG *in;
    PULONG out;

    (void)route;
    in = (const ULONG *)op;
    out = (PULONG)res;
    out[0] = in[0] + 1;
    out[1] = 0xC0DEUL;
    if (context != NULL) {
        *(PULONG)context = *(PULONG)context + 1;
    }
    return 8;
}

/* What the member's worker does with the slot, one operation. */
static BOOLEAN workerStep(PCM_CALL c)
{
    ULONG n;

    if (!CmCallClaim(c)) {
        return FALSE;
    }
    n = c->Run(NULL, c->Context, c->Op, c->Res);
    CmCallFinish(c, n);
    return TRUE;
}

static void test_transitions(void)
{
    CM_CALL c;
    ULONG op[2];
    ULONG res[4];
    ULONG got;
    ULONG id;
    ULONG id2;
    ULONG runs;

    CmCallInit(&c);
    CHECK_EQ(c.State, CM_CALL_IDLE, "idle at first");
    CHECK_EQ(CmCallTake(&c, 1, res, sizeof(res), &got), CM_CALL_T_NONE,
             "nothing to take");
    CHECK(!CmCallClaim(&c), "nothing to claim");
    runs = 0;
    op[0] = 41;
    op[1] = 0;
    id = CmCallSubmit(&c, 7, fakeRun, &runs, op, sizeof(op), 1000);
    CHECK(id != 0, "submitted: an id");
    CHECK_EQ(c.State, CM_CALL_SUBMITTED, "IDLE -> SUBMITTED");
    CHECK_EQ(c.Kind, 7, "its kind");
    CHECK_EQ(CmCallSubmit(&c, 7, fakeRun, &runs, op, sizeof(op), 1001), 0,
             "one operation at a time per member");
    CHECK_EQ(CmCallTake(&c, id, res, sizeof(res), &got), CM_CALL_T_BUSY,
             "submitted: busy");
    CHECK(CmCallClaim(&c), "SUBMITTED -> RUNNING");
    CHECK(!CmCallClaim(&c), "taken once (R6f's compare-exchange)");
    CHECK(!CmCallWithdraw(&c, id), "a running operation is not withdrawn");
    CHECK_EQ(CmCallTake(&c, id, res, sizeof(res), &got), CM_CALL_T_BUSY,
             "running: busy");
    CHECK_EQ(c.Run(NULL, c.Context, c.Op, c.Res), 8, "the call");
    CmCallFinish(&c, 8);
    CHECK_EQ(c.State, CM_CALL_DONE, "RUNNING -> DONE");
    CHECK_EQ(c.Ran, id, "the id it ran stored with the result");
    CmCallFinish(&c, 4);
    CHECK_EQ(c.ResBytes, 8, "a second finish changes nothing");
    memset(res, 0, sizeof(res));
    CHECK_EQ(CmCallTake(&c, id, res, sizeof(res), &got), CM_CALL_T_TAKEN,
             "DONE -> IDLE, taken for its id");
    CHECK_EQ(got, 8, "its length");
    CHECK_EQ(res[0], 42, "its result");
    CHECK_EQ(c.State, CM_CALL_IDLE, "idle again");
    CHECK_EQ(runs, 1, "run once");

    /* A stale id: the result is not the one asked for, dropped. */
    id2 = CmCallSubmit(&c, 7, fakeRun, &runs, op, sizeof(op), 1100);
    CHECK(id2 != 0 && id2 != id, "a new id");
    CHECK(workerStep(&c), "run");
    memset(res, 0, sizeof(res));
    CHECK_EQ(CmCallTake(&c, id, res, sizeof(res), &got), CM_CALL_T_STALE,
             "another id's result: stale, discarded");
    CHECK_EQ(res[0], 0, "nothing copied");
    CHECK_EQ(got, 0, "no length");
    CHECK_EQ(c.State, CM_CALL_IDLE, "and the slot idle");
    CHECK_EQ(CmCallTake(&c, id2, res, sizeof(res), &got), CM_CALL_T_NONE,
             "the discarded result is gone");
    CHECK(workerStep(&c) == FALSE, "nothing more to run");

    /* A withdrawal before any worker took it. */
    id = CmCallSubmit(&c, 7, fakeRun, &runs, op, sizeof(op), 1200);
    CHECK(!CmCallWithdraw(&c, id + 1), "another id: not withdrawn");
    CHECK(CmCallWithdraw(&c, id), "SUBMITTED -> IDLE");
    CHECK(!CmCallClaim(&c), "and no worker takes it");
    CHECK_EQ(runs, 2, "never run");

    /* Refusals. */
    CHECK_EQ(CmCallSubmit(&c, 7, NULL, NULL, op, sizeof(op), 0), 0,
             "no executor");
    CHECK_EQ(CmCallSubmit(&c, 7, fakeRun, NULL, op, CM_CALL_OP_BYTES + 4, 0),
             0, "arguments longer than the slot");
    CHECK_EQ(CmCallSubmit(&c, 7, fakeRun, NULL, NULL, 4, 0), 0,
             "a length with no arguments");
    CHECK(CmCallSubmit(&c, 7, fakeRun, NULL, op, CM_CALL_OP_BYTES, 0) != 0,
          "exactly the room");
    CHECK(CmCallClaim(&c), "claimed");
    CmCallFinish(&c, CM_CALL_RES_BYTES + 100);
    CHECK_EQ(c.ResBytes, CM_CALL_RES_BYTES, "a result held to its room");
    CHECK_EQ(CmCallTake(&c, c.Id, res, sizeof(res), &got), CM_CALL_T_TAKEN,
             "taken");
    CHECK_EQ(got, sizeof(res), "copied up to the caller's room");

    /* Ids are never 0, also across the wrap. */
    c.NextId = 0xFFFFFFFFUL;
    id = CmCallSubmit(&c, 7, fakeRun, NULL, op, sizeof(op), 0);
    CHECK_EQ(id, 1, "after the wrap: 1, never 0");
    CHECK(CmCallWithdraw(&c, id), "withdrawn");
    CHECK_EQ(CmCallTake(&c, 0, res, sizeof(res), &got), CM_CALL_T_NONE,
             "idle");
    CHECK(!CmCallWithdraw(&c, 0), "id 0 names nothing");
}

/*
 * A deadline that passes while the call runs (R11 O6, "an elapsed
 * deadline, also when a call is still running"): the client moves on at
 * its deadline on its own clock, the call stays the worker's, and its late
 * result is still taken by its id when it comes - the client counts it as
 * late. Another member's slot is unaffected throughout.
 */
static void test_deadline(void)
{
    CM_CALL a;
    CM_CALL b;
    ULONG op[1];
    ULONG res[2];
    ULONG got;
    ULONG ida;
    ULONG idb;
    ULONG now;
    ULONG late;
    ULONG moved;

    CmCallInit(&a);
    CmCallInit(&b);
    op[0] = 1;
    ida = CmCallSubmit(&a, 1, fakeRun, NULL, op, sizeof(op), 5000);
    CHECK(CmCallClaim(&a), "member A's worker inside its call");
    moved = 0;
    late = 0;
    for (now = 5000; now <= 9000; now += 50) {
        /* The thread's pass: member A's deadline is 2 s from submission. */
        if (!moved && CmCallAge(&a, now) >= 2000) {
            moved = now;
        }
        /* Member B works on meanwhile, one operation a pass. */
        if (b.State == CM_CALL_IDLE) {
            idb = CmCallSubmit(&b, 1, fakeRun, NULL, op, sizeof(op), now);
            (void)workerStep(&b);
            CHECK_EQ(CmCallTake(&b, idb, res, sizeof(res), &got),
                     CM_CALL_T_TAKEN, "member B: every pass, undelayed");
        }
    }
    CHECK_EQ(moved, 7000, "member A moved on at its deadline, its call running");
    CHECK_EQ(a.State, CM_CALL_RUNNING, "the call is still the worker's");
    CHECK_EQ(CmCallTake(&a, ida, res, sizeof(res), &got), CM_CALL_T_BUSY,
             "nothing to take yet");
    /* The call returns at last. */
    CHECK_EQ(a.Run(NULL, NULL, a.Op, a.Res), 8, "the call returns");
    CmCallFinish(&a, 8);
    if (CmCallTake(&a, ida, res, sizeof(res), &got) == CM_CALL_T_TAKEN) {
        late = 1;
    }
    CHECK(late, "the late result taken by its id (a retry's outcome)");
    CHECK_EQ(CmCallAge(&a, 99999), 0, "idle: no age");
}

/*
 * The attachment of a "next operation / take result" client (design
 * record 28's CmNiNext and CmNiTake, task 42-A.15): on each pass the
 * thread takes a finished operation's result into the machine by its id,
 * then submits the machine's next operation if the slot is idle; the
 * worker runs it in between. A model machine of three steps.
 */
typedef struct _MODEL {
    ULONG Step;         /* 0..3, 3 done            */
    ULONG Out;          /* the id outstanding      */
    ULONG Sum;
    ULONG Dropped;
} MODEL;

static ULONG modelNext(MODEL *m, PULONG op)
{
    if (m->Step >= 3 || m->Out != 0) {
        return 0;
    }
    op[0] = m->Step * 10;
    return 1;
}

static void modelTake(MODEL *m, ULONG id, const ULONG *res)
{
    if (id != m->Out) {
        m->Dropped++;
        return;
    }
    m->Sum += res[0];
    m->Out = 0;
    m->Step++;
}

static void test_client(void)
{
    CM_CALL c;
    MODEL m;
    ULONG op[1];
    ULONG res[2];
    ULONG got;
    ULONG passes;
    ULONG id;

    CmCallInit(&c);
    memset(&m, 0, sizeof(m));
    for (passes = 0; passes < 10 && m.Step < 3; passes++) {
        if (m.Out != 0 &&
            CmCallTake(&c, m.Out, res, sizeof(res), &got) == CM_CALL_T_TAKEN) {
            modelTake(&m, m.Out, res);
        }
        if (c.State == CM_CALL_IDLE && modelNext(&m, op)) {
            id = CmCallSubmit(&c, 2, fakeRun, NULL, op, sizeof(op), passes);
            CHECK(id != 0, "the machine's operation submitted");
            m.Out = id;
        }
        (void)workerStep(&c);   /* the worker, between passes */
    }
    CHECK_EQ(m.Step, 3, "the machine ran to its end through the slot");
    CHECK_EQ(m.Sum, 1 + 11 + 21, "each result taken once, in order");
    CHECK_EQ(m.Dropped, 0, "nothing dropped");
    CHECK(passes <= 4, "one operation a pass");
}

int main(void)
{
    test_transitions();
    test_deadline();
    test_client();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
