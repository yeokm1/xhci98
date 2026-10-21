/*
 * test_cm_eject.c - the Windows 98 SE and ME eject transaction and the 9x
 * statement decision (src/cm/cm_eject.c; roadmap-usb4.md task 43.18;
 * design record 21 section 7.2, "The 9x half", parts 1 and 2): the ask's
 * acceptance with each check failing, the result's and the reply's
 * layouts, the transaction driven through every branch by a fake
 * configuration manager (clean, plain, not clean, failed under the
 * firmware connection manager and under the USB4 profile, the isolation
 * proven, failed and with a sibling running, R11 O6 with a call still
 * running and its late success, the unit vanishing), the gate, the
 * result's stamps and consumption, a stopped start's record adopted, and
 * the statement decision's identity and deadline.
 */

#include <stdio.h>
#include <string.h>
#include "../src/cm/tbt_topo.h"
#include "../src/cm/cm_nvlink.h"
#include "../src/cm/cm_eject.h"
#include "test_harness.h"

static ULONG get32(const UCHAR *b, ULONG at)
{
    return (ULONG)b[at] | ((ULONG)b[at + 1] << 8) |
           ((ULONG)b[at + 2] << 16) | ((ULONG)b[at + 3] << 24);
}

/* ---- a fake configuration manager ------------------------------------- */

typedef struct _FAKE {
    ULONG Devnode;          /* FIND's answer                             */
    ULONG QueryCr;
    ULONG RemoveCr;         /* every Remove_SubTree's                    */
    ULONG Entry;            /* REQUEST's: CM_EJ_NONE for none            */
    ULONG Pop;              /* CM_EJ_POP_*                               */
    ULONG Closed;           /* the teardown's read-back                  */
    ULONG Marked;
    BOOLEAN Hold;           /* a call stays running (not fed)            */
    ULONG Calls[16];        /* per CM_EJ_DO_*                            */
    ULONG Held;             /* the call kept running                     */
} FAKE;

static CM_EJ_IN mkIn(ULONG now)
{
    CM_EJ_IN in;

    memset(&in, 0, sizeof(in));
    in.NowMs = now;
    in.SlotIdle = TRUE;
    return in;
}

/* One step of the thread: every action until none, each fed at once by
 * the fake unless it holds the call. */
static VOID drive(CM_EJ_TX *t, FAKE *f, CM_EJ_IN *in)
{
    ULONG what;
    ULONG n;

    for (n = 0; n < 32; n++) {
        in->SlotIdle = (BOOLEAN)(t->Pending == 0);
        what = CmEjNext(t, in);
        if (what == CM_EJ_DO_NONE) {
            return;
        }
        f->Calls[what]++;
        switch (what) {
        case CM_EJ_DO_FIND:
        case CM_EJ_DO_QUERY:
        case CM_EJ_DO_REMOVE:
        case CM_EJ_DO_POPULATION:
        case CM_EJ_DO_ADOPT:
            CmEjSubmitted(t, what, TRUE, in->NowMs);
            if (f->Hold) {
                f->Held = what;
                return;
            }
            if (what == CM_EJ_DO_FIND) {
                CmEjFeed(t, CM_EJ_FED_FIND, f->Devnode, 0, in->NowMs);
            } else if (what == CM_EJ_DO_QUERY) {
                CmEjFeed(t, CM_EJ_FED_QUERY, f->QueryCr, 0, in->NowMs);
            } else if (what == CM_EJ_DO_REMOVE) {
                CmEjFeed(t, CM_EJ_FED_REMOVE, f->RemoveCr, in->U4Known,
                         in->NowMs);
            } else if (what == CM_EJ_DO_POPULATION) {
                CmEjFeed(t, CM_EJ_FED_POPULATION, f->Pop, 0, in->NowMs);
            } else {
                CmEjFeed(t, CM_EJ_FED_ADOPT, f->Devnode, 1, in->NowMs);
            }
            break;
        case CM_EJ_DO_REQUEST:
            CmEjFeed(t, CM_EJ_FED_REQUEST, f->Entry, 0x123456UL, in->NowMs);
            break;
        case CM_EJ_DO_WITHDRAW:
            CmEjFeed(t, CM_EJ_FED_WITHDRAW, 0, in->U4Known, in->NowMs);
            break;
        case CM_EJ_DO_TEARDOWN:
            CmEjFeed(t, CM_EJ_FED_TEARDOWN, f->Closed, in->Qualified,
                     in->NowMs);
            break;
        case CM_EJ_DO_MARK:
            CmEjFeed(t, CM_EJ_FED_MARK, f->Marked, 0, in->NowMs);
            break;
        case CM_EJ_DO_REENUM:
            CmEjFeed(t, CM_EJ_FED_REENUM, 0, 0, in->NowMs);
            break;
        default:
            return;
        }
    }
}

static CM_EJ_ASK mkAsk(VOID)
{
    CM_EJ_ASK a;

    a.Nonce = 0x11223344UL;
    a.Member = XHCI_TBT_BDF(0, 0x1C, 0);
    a.Unit = XHCI_TBT_BDF(4, 0, 0);
    a.EpochLo = 0x1000UL;
    a.EpochHi = 0x2000UL;
    a.ContentGen = 7;
    a.Flags = 0;
    return a;
}

static VOID fakeInit(FAKE *f)
{
    memset(f, 0, sizeof(*f));
    f->Devnode = 0xC0DE0001UL;
    f->QueryCr = CM_EJ_CR_SUCCESS;
    f->RemoveCr = CM_EJ_CR_SUCCESS;
    f->Entry = 3;
    f->Pop = CM_EJ_POP_PASSED;
    f->Closed = 1;
    f->Marked = 1;
}

static VOID begin(CM_EJ_TX *t, BOOLEAN nvme, ULONG now)
{
    CM_EJ_ASK a;

    a = mkAsk();
    CmEjBegin(t, &a, 0x00108086UL, nvme, now);
}

/* ---- the ask ------------------------------------------------------------ */

static VOID test_accept(VOID)
{
    CM_EJ_ASK a;
    CM_EJ_ASK b;
    CM_EJ_CHECK c;
    UCHAR buf[CM_EJ_ASK_BYTES];
    UCHAR rep[CM_EJ_REPLY_BYTES];

    a = mkAsk();
    CmEjAskEncode(buf, &a);
    CHECK(CmEjAskDecode(buf, sizeof(buf), &b), "decodes");
    CHECK_EQ(b.Member, a.Member, "member");
    CHECK_EQ(b.Unit, a.Unit, "unit");
    CHECK_EQ(b.ContentGen, 7, "generation");
    CHECK_EQ(get32(buf, CM_EJ_A_EPOCH_HI), 0x2000UL, "epoch high at 16");
    CHECK(!CmEjAskDecode(buf, sizeof(buf) - 1, &b), "short refused");
    memset(buf, 0, 4);
    CHECK(!CmEjAskDecode(buf, sizeof(buf), &b), "nonce 0 refused");

    memset(&c, 0, sizeof(c));
    c.Available = TRUE;
    c.EpochLo = 0x1000UL;
    c.EpochHi = 0x2000UL;
    c.ContentGen = 7;
    c.Member = TRUE;
    c.UnitId = 0x00108086UL;
    c.Below = TRUE;
    CHECK_EQ(CmEjAccept(&a, &c), 0, "accepted");

    b = a;
    b.Flags = 1;
    CHECK_EQ(CmEjAccept(&b, &c), CM_EJ_WHY_BAD, "flags");
    b = a;
    b.Unit = XHCI_TBT_BDF(0, 3, 0);
    CHECK_EQ(CmEjAccept(&b, &c), CM_EJ_WHY_NOT_BELOW,
             "a bus the driver could not resolve");
    b = a;
    b.Unit = 0x00010000UL;
    CHECK_EQ(CmEjAccept(&b, &c), CM_EJ_WHY_BAD, "bits above 15");
    b = a;
    b.EpochLo++;
    CHECK_EQ(CmEjAccept(&b, &c), CM_EJ_WHY_EPOCH, "another start");
    b = a;
    b.ContentGen = 6;
    CHECK_EQ(CmEjAccept(&b, &c), CM_EJ_WHY_GENERATION,
             "a delayed ask that may name a replacement");
    c.Available = FALSE;
    CHECK_EQ(CmEjAccept(&a, &c), CM_EJ_WHY_UNAVAILABLE, "no worker");
    c.Available = TRUE;
    c.Member = FALSE;
    CHECK_EQ(CmEjAccept(&a, &c), CM_EJ_WHY_MEMBER, "not a member");
    c.Member = TRUE;
    c.UnitId = 0xFFFFFFFFUL;
    CHECK_EQ(CmEjAccept(&a, &c), CM_EJ_WHY_NOTHING, "nothing answers");
    c.UnitId = 0x00108086UL;
    c.Below = FALSE;
    CHECK_EQ(CmEjAccept(&a, &c), CM_EJ_WHY_NOT_BELOW, "another bus");
    c.Below = TRUE;
    c.NoChannel = TRUE;
    CHECK_EQ(CmEjAccept(&a, &c), CM_EJ_WHY_NO_CHANNEL,
             "an NVMe function no channel names: refused before the query");
    c.NoChannel = FALSE;
    c.Busy = TRUE;
    CHECK_EQ(CmEjAccept(&a, &c), CM_EJ_WHY_BUSY, "busy");

    CmEjReplyEncode(rep, a.Nonce, CM_EJ_WHY_BUSY);
    CHECK_EQ(get32(rep, CM_EJ_R_NONCE), a.Nonce, "reply nonce");
    CHECK_EQ(get32(rep, CM_EJ_R_REASON), CM_EJ_WHY_BUSY, "reply reason");
}

/* ---- the branches ------------------------------------------------------- */

static VOID test_clean(VOID)
{
    CM_EJ_TX t;
    FAKE f;
    CM_EJ_IN in;
    UCHAR r[CM_EJ_RESULT_BYTES];

    /* Under the firmware connection manager: no teardown. */
    fakeInit(&f);
    begin(&t, TRUE, 100);
    CHECK(CmEjResultDue(&t, 100), "accepted is written at once");
    CmEjResultEncode(&t, r, 100);
    CHECK_EQ(get32(r, CM_EJ_S_ACCEPTED), CM_EJ_ACCEPTED, "acceptance word");
    CHECK_EQ(get32(r, CM_EJ_S_STEP), CM_EJ_STEP_ACCEPTED, "accepted");
    CHECK_EQ(get32(r, CM_EJ_S_QUERY_CR), CM_EJ_NO_CR, "no query yet");
    CHECK(CmEjGate(&t), "the gate up from acceptance");

    in = mkIn(200);
    drive(&t, &f, &in);
    CHECK_EQ(t.Phase, CM_EJ_P_ANSWER, "waiting for Stor98");
    CHECK_EQ(t.Step, CM_EJ_STEP_REQUESTED, "requested");
    CHECK_EQ(t.ReqAt, 200, "T_ack from before the request");
    CHECK_EQ(f.Calls[CM_EJ_DO_FIND], 1, "found");
    CHECK_EQ(f.Calls[CM_EJ_DO_QUERY], 1, "queried once");
    CHECK_EQ(f.Calls[CM_EJ_DO_REMOVE], 0, "nothing removed before the answer");

    /* Read late by the tool: still accepted, the step the third. */
    CmEjResultEncode(&t, r, 250);
    CHECK_EQ(get32(r, CM_EJ_S_ACCEPTED), CM_EJ_ACCEPTED, "still accepted");
    CHECK_EQ(get32(r, CM_EJ_S_ANSWER), (0x123456UL << 8) | CM_EJ_ANS_NONE,
             "the sequence, no answer yet");
    CHECK(!CmEjResultDue(&t, 250 + CM_EJ_RESTAMP_MS - 1), "not yet re-stamped");
    CHECK(CmEjResultDue(&t, 250 + CM_EJ_RESTAMP_MS), "a pending wait re-stamped");

    in = mkIn(400);
    in.Answer = CM_EJ_ANS_CLEAN;
    drive(&t, &f, &in);
    CHECK_EQ(t.Phase, CM_EJ_P_DONE, "retired");
    CHECK_EQ(t.Step, CM_EJ_STEP_REMOVED, "removed");
    CHECK_EQ(t.Outcome, CM_EJ_OUT_CLEAN, "clean");
    CHECK((t.Flags & CM_EJ_F_WITHDRAWN) != 0, "the request withdrawn (O5)");
    CHECK_EQ(f.Calls[CM_EJ_DO_TEARDOWN], 0, "no teardown under the ICM");
    CHECK(!CmEjGate(&t), "the gate down");
    CmEjResultEncode(&t, r, 400);
    CHECK_EQ(get32(r, CM_EJ_S_ANSWER), (0x123456UL << 8) | CM_EJ_ANS_CLEAN,
             "the answer");
    CHECK_EQ(get32(r, CM_EJ_S_REMOVE_CR), 0, "removal CONFIGRET");
    CHECK_EQ(get32(r, CM_EJ_S_CLOCK_MS), 400, "the clock");

    /* Under the USB4 profile, the port known: teardown after the removal;
     * the statement only on a qualified system. */
    fakeInit(&f);
    begin(&t, TRUE, 0);
    in = mkIn(10);
    in.U4Known = TRUE;
    drive(&t, &f, &in);
    in.Answer = CM_EJ_ANS_CLEAN;
    drive(&t, &f, &in);
    CHECK_EQ(f.Calls[CM_EJ_DO_POPULATION], 1, "the population check");
    CHECK_EQ(f.Calls[CM_EJ_DO_TEARDOWN], 1, "torn down");
    CHECK_EQ(f.Calls[CM_EJ_DO_MARK], 0, "no statement unqualified");
    CHECK_EQ(t.Phase, CM_EJ_P_DONE, "done");

    fakeInit(&f);
    begin(&t, TRUE, 0);
    in = mkIn(10);
    in.U4Known = TRUE;
    in.Qualified = TRUE;
    drive(&t, &f, &in);
    in.Answer = CM_EJ_ANS_CLEAN;
    in.Nested = TRUE;
    drive(&t, &f, &in);
    CHECK_EQ(t.Phase, CM_EJ_P_TEARDOWN, "no teardown from a servicing point");
    in.Nested = FALSE;
    drive(&t, &f, &in);
    CHECK_EQ(f.Calls[CM_EJ_DO_MARK], 1, "DOWN_QUIESCED, no wait");
    CHECK_EQ(t.Phase, CM_EJ_P_DONE, "done");
    CHECK_EQ(t.Outcome, CM_EJ_OUT_CLEAN, "still clean");

    /* A sibling running: removed all the same, no teardown, exit 0. */
    fakeInit(&f);
    f.Pop = CM_EJ_POP_SIBLING;
    begin(&t, TRUE, 0);
    in = mkIn(10);
    in.U4Known = TRUE;
    drive(&t, &f, &in);
    in.Answer = CM_EJ_ANS_CLEAN;
    drive(&t, &f, &in);
    CHECK_EQ(t.Step, CM_EJ_STEP_REMOVED, "removed with a sibling running");
    CHECK_EQ(f.Calls[CM_EJ_DO_TEARDOWN], 0, "no teardown");
}

static VOID test_refused_plain(VOID)
{
    CM_EJ_TX t;
    FAKE f;
    CM_EJ_IN in;

    fakeInit(&f);
    f.QueryCr = 0x17UL;
    begin(&t, TRUE, 0);
    in = mkIn(5);
    drive(&t, &f, &in);
    CHECK_EQ(t.Step, CM_EJ_STEP_REFUSED, "query refused");
    CHECK_EQ(t.QueryCr, 0x17UL, "its CONFIGRET");
    CHECK_EQ(f.Calls[CM_EJ_DO_REQUEST], 0, "no request");
    CHECK_EQ(f.Calls[CM_EJ_DO_REMOVE], 0, "nothing removed");
    CHECK(!CmEjGate(&t), "nothing owed");

    fakeInit(&f);
    f.Devnode = 0;
    begin(&t, TRUE, 0);
    drive(&t, &f, &in);
    CHECK_EQ(t.Step, CM_EJ_STEP_REFUSED, "no devnode: nothing called");
    CHECK_EQ(t.QueryCr, CM_EJ_CR_NO_SUCH_DEVNODE, "read as the list changed");

    /* Not an NVMe function (a dock's upstream port): no request. */
    fakeInit(&f);
    f.Entry = CM_EJ_NONE;
    begin(&t, FALSE, 0);
    drive(&t, &f, &in);
    CHECK_EQ(t.Step, CM_EJ_STEP_REMOVED, "removed");
    CHECK_EQ(t.Outcome, CM_EJ_OUT_PLAIN, "with no request");
    CHECK_EQ(f.Calls[CM_EJ_DO_WITHDRAW], 0, "nothing to withdraw");
}

static VOID test_not_clean(VOID)
{
    CM_EJ_TX t;
    FAKE f;
    CM_EJ_IN in;

    fakeInit(&f);
    begin(&t, TRUE, 0);
    in = mkIn(5);
    drive(&t, &f, &in);
    in.Answer = CM_EJ_ANS_ISOLATED;
    drive(&t, &f, &in);
    CHECK_EQ(t.Outcome, CM_EJ_OUT_NOT_CLEAN, "not clean");
    CHECK_EQ(t.Step, CM_EJ_STEP_REMOVED, "removed");
    CHECK_EQ(f.Calls[CM_EJ_DO_REENUM], 1, "re-enumerated on the same tunnel");
    CHECK_EQ(t.Phase, CM_EJ_P_DONE, "done");
}

static VOID test_failed(VOID)
{
    CM_EJ_TX t;
    FAKE f;
    CM_EJ_IN in;
    ULONG answers[3];
    ULONG k;

    answers[0] = CM_EJ_ANS_FAILED;
    answers[1] = CM_EJ_ANS_NONE;
    answers[2] = CM_EJ_ANS_LATE;
    for (k = 0; k < 3; k++) {
        /* The firmware connection manager: no isolation exists. */
        fakeInit(&f);
        begin(&t, TRUE, 0);
        in = mkIn(5);
        drive(&t, &f, &in);
        in.Answer = answers[k];
        drive(&t, &f, &in);
        CHECK_EQ(t.Phase, CM_EJ_P_HELD, "blocked");
        CHECK_EQ(t.Step, CM_EJ_STEP_BLOCKED, "terminal blocked");
        CHECK_EQ(t.Outcome, CM_EJ_OUT_NO_STOP, "the drive did not stop");
        CHECK_EQ(f.Calls[CM_EJ_DO_REMOVE], 0, "never removed unisolated");
        CHECK_EQ(f.Calls[CM_EJ_DO_WITHDRAW], 0, "the request left in place");
        CHECK(CmEjGate(&t), "the member blocked");
        CHECK(!CmEjBusy(&t), "waiting for the unplug alone");
        CHECK(!CmEjOwesRemoval(&t), "nothing authorised");
    }

    /* The unplug retires it: its link-down reset is the isolation. */
    in = mkIn(9000);
    in.Vanished = TRUE;
    drive(&t, &f, &in);
    CHECK_EQ(f.Calls[CM_EJ_DO_REMOVE], 1, "the removal after the unplug");
    CHECK_EQ(t.Phase, CM_EJ_P_DONE, "retired");
    CHECK(!CmEjGate(&t), "the block ends");
    CHECK_EQ(t.Step, CM_EJ_STEP_BLOCKED, "the terminal step stays");

    /* USB4, qualified, check passed, teardown proven: isolated. */
    fakeInit(&f);
    begin(&t, TRUE, 0);
    in = mkIn(5);
    in.U4Known = TRUE;
    in.Qualified = TRUE;
    drive(&t, &f, &in);
    in.Answer = CM_EJ_ANS_FAILED;
    in.AckHeld = TRUE;
    drive(&t, &f, &in);
    CHECK_EQ(t.Phase, CM_EJ_P_ACK, "the statement's wait");
    CHECK_EQ(t.Step, CM_EJ_STEP_ISOLATING, "isolating");
    CHECK_EQ(f.Calls[CM_EJ_DO_REMOVE], 0, "no removal before the wait ends");
    in.AckHeld = FALSE;
    in.NowMs = 3100;
    drive(&t, &f, &in);
    CHECK_EQ(t.Step, CM_EJ_STEP_REMOVED, "removed");
    CHECK_EQ(t.Outcome, CM_EJ_OUT_ISOLATED, "after isolation");

    /* ... the read-back did not prove the path closed: blocked. */
    fakeInit(&f);
    f.Closed = 0;
    begin(&t, TRUE, 0);
    in = mkIn(5);
    in.U4Known = TRUE;
    in.Qualified = TRUE;
    drive(&t, &f, &in);
    in.Answer = CM_EJ_ANS_FAILED;
    drive(&t, &f, &in);
    CHECK_EQ(t.Phase, CM_EJ_P_HELD, "not isolated: blocked");
    CHECK_EQ(f.Calls[CM_EJ_DO_REMOVE], 0, "no removal");

    /* ... a sibling running: no teardown, blocked. */
    fakeInit(&f);
    f.Pop = CM_EJ_POP_SIBLING;
    begin(&t, TRUE, 0);
    drive(&t, &f, &in);
    in.Answer = CM_EJ_ANS_FAILED;
    drive(&t, &f, &in);
    CHECK_EQ(f.Calls[CM_EJ_DO_TEARDOWN], 0, "no teardown past a failed check");
    CHECK_EQ(t.Phase, CM_EJ_P_HELD, "blocked");

    /* ... not qualified: no isolation is synthesised. */
    fakeInit(&f);
    begin(&t, TRUE, 0);
    in = mkIn(5);
    in.U4Known = TRUE;
    drive(&t, &f, &in);
    in.Answer = CM_EJ_ANS_FAILED;
    drive(&t, &f, &in);
    CHECK_EQ(f.Calls[CM_EJ_DO_POPULATION], 0, "no check");
    CHECK_EQ(t.Phase, CM_EJ_P_HELD, "blocked");
}

static VOID test_o6(VOID)
{
    CM_EJ_TX t;
    FAKE f;
    CM_EJ_IN in;
    ULONG now;

    fakeInit(&f);
    f.RemoveCr = 0x17UL;
    begin(&t, TRUE, 0);
    in = mkIn(0);
    drive(&t, &f, &in);
    in.Answer = CM_EJ_ANS_CLEAN;
    drive(&t, &f, &in);
    CHECK_EQ(f.Calls[CM_EJ_DO_REMOVE], 1, "first attempt");
    CHECK_EQ(t.Step, CM_EJ_STEP_REMOVING, "removing");
    for (now = 50; now < 2000; now += 50) {
        in.NowMs = now;
        drive(&t, &f, &in);
    }
    CHECK_EQ(f.Calls[CM_EJ_DO_REMOVE], 1, "not before 2 s");
    in.NowMs = 2000;
    drive(&t, &f, &in);
    CHECK_EQ(f.Calls[CM_EJ_DO_REMOVE], 2, "every 2 s");
    CHECK(CmEjResultDue(&t, 2000), "a retry is written");

    /* The 14th attempt at 26 s is held running past the deadline. */
    for (now = 2050; now < 26000; now += 50) {
        in.NowMs = now;
        drive(&t, &f, &in);
    }
    f.Hold = TRUE;
    in.NowMs = 26000;
    drive(&t, &f, &in);
    CHECK_EQ(f.Held, CM_EJ_DO_REMOVE, "a call running");
    in.NowMs = 30000;
    drive(&t, &f, &in);
    CHECK_EQ(t.Step, CM_EJ_STEP_BLOCKED, "blocked at 30 s, the call running");
    CHECK_EQ(t.Outcome, CM_EJ_OUT_KEEPS_FAILING, "keeps failing");
    CHECK(CmEjGate(&t), "the member blocked");
    /* Its late success: retired, never clean. */
    CmEjFeed(&t, CM_EJ_FED_REMOVE, CM_EJ_CR_SUCCESS, 0, 31000);
    f.Hold = FALSE;
    in.NowMs = 31000;
    drive(&t, &f, &in);
    CHECK_EQ(t.Phase, CM_EJ_P_DONE, "the late success retires it");
    CHECK_EQ(t.Step, CM_EJ_STEP_BLOCKED, "the result stays blocked");
    CHECK((t.Flags & CM_EJ_F_WITHDRAWN) != 0, "the request withdrawn");
    CHECK(!CmEjGate(&t), "the block lifted");

    /* USB4: the tunnel torn down once after the deadline, retries on. */
    fakeInit(&f);
    f.RemoveCr = 0x17UL;
    begin(&t, TRUE, 0);
    in = mkIn(0);
    in.U4Known = TRUE;
    drive(&t, &f, &in);
    in.Answer = CM_EJ_ANS_CLEAN;
    drive(&t, &f, &in);
    for (now = 50; now <= 36000; now += 50) {
        in.NowMs = now;
        drive(&t, &f, &in);
    }
    CHECK_EQ(f.Calls[CM_EJ_DO_TEARDOWN], 1, "torn down once");
    CHECK(f.Calls[CM_EJ_DO_REMOVE] >= 18, "retries go on after the deadline");
    CHECK_EQ(t.Phase, CM_EJ_P_REMOVE, "still removing");
    f.RemoveCr = CM_EJ_CR_SUCCESS;
    in.NowMs = 40000;
    drive(&t, &f, &in);
    CHECK_EQ(t.Phase, CM_EJ_P_DONE, "a later success lifts the block");
}

/* Round-5 review vectors: an NVMe function whose request was not
 * published; a removal success taken after O6's deadline; an isolation's
 * acknowledgement wait at the stop. */
static VOID test_edges(VOID)
{
    CM_EJ_TX t;
    FAKE f;
    CM_EJ_IN in;

    fakeInit(&f);
    f.Entry = CM_EJ_REQ_FAILED;
    begin(&t, TRUE, 0);
    in = mkIn(5);
    drive(&t, &f, &in);
    CHECK_EQ(t.Phase, CM_EJ_P_HELD, "no request published: blocked");
    CHECK_EQ(f.Calls[CM_EJ_DO_REMOVE], 0, "never a plain removal");
    CHECK_EQ(t.Answer, CM_EJ_ANS_NONE, "read as no answer");

    /* The channel no longer naming an NVMe function after its query. */
    fakeInit(&f);
    f.Entry = CM_EJ_NONE;
    begin(&t, TRUE, 0);
    in = mkIn(5);
    drive(&t, &f, &in);
    CHECK_EQ(t.Phase, CM_EJ_P_HELD, "NVMe without a request: blocked");
    CHECK_EQ(f.Calls[CM_EJ_DO_REMOVE], 0, "no plain removal of an NVMe");

    fakeInit(&f);
    f.Entry = CM_EJ_REQ_FAILED;
    begin(&t, TRUE, 0);
    in = mkIn(5);
    in.U4Known = TRUE;
    in.Qualified = TRUE;
    drive(&t, &f, &in);
    CHECK_EQ(t.Phase, CM_EJ_P_HELD,
             "the isolation needs an entry to mark: blocked");

    /* A success taken after the deadline is never clean. */
    fakeInit(&f);
    f.Hold = TRUE;
    begin(&t, TRUE, 0);
    in = mkIn(0);
    CmEjSubmitted(&t, CM_EJ_DO_FIND, TRUE, 0);
    CmEjFeed(&t, CM_EJ_FED_FIND, f.Devnode, 0, 0);
    CmEjSubmitted(&t, CM_EJ_DO_QUERY, TRUE, 0);
    CmEjFeed(&t, CM_EJ_FED_QUERY, 0, 0, 0);
    CmEjFeed(&t, CM_EJ_FED_REQUEST, 3, 9, 0);
    in.Answer = CM_EJ_ANS_CLEAN;
    drive(&t, &f, &in);
    CHECK_EQ(f.Held, CM_EJ_DO_REMOVE, "the first removal running");
    CmEjFeed(&t, CM_EJ_FED_REMOVE, CM_EJ_CR_SUCCESS, 0, 31000);
    CHECK_EQ(t.Step, CM_EJ_STEP_BLOCKED, "taken at 31 s: blocked");
    CHECK_EQ(t.Outcome, CM_EJ_OUT_KEEPS_FAILING, "not clean");
    f.Hold = FALSE;
    in.NowMs = 31000;
    drive(&t, &f, &in);
    CHECK_EQ(t.Phase, CM_EJ_P_DONE, "retired all the same");

    /* Stopped in an isolation's acknowledgement wait: the block. */
    fakeInit(&f);
    begin(&t, TRUE, 0);
    in = mkIn(5);
    in.U4Known = TRUE;
    in.Qualified = TRUE;
    drive(&t, &f, &in);
    in.Answer = CM_EJ_ANS_FAILED;
    in.AckHeld = TRUE;
    drive(&t, &f, &in);
    CHECK_EQ(t.Phase, CM_EJ_P_ACK, "in the wait");
    CmEjAdopt(&t, 100);
    CHECK_EQ(t.Phase, CM_EJ_P_HELD, "adopted as the block");
    CHECK(!CmEjOwesRemoval(&t), "no removal authorised by it");
}

static VOID test_vanish(VOID)
{
    CM_EJ_TX t;
    FAKE f;
    CM_EJ_IN in;

    /* The unit leaves while its answer is awaited. */
    fakeInit(&f);
    begin(&t, TRUE, 0);
    in = mkIn(5);
    drive(&t, &f, &in);
    in.Vanished = TRUE;
    drive(&t, &f, &in);
    CHECK_EQ(t.Step, CM_EJ_STEP_REMOVED, "removed by its unplug");
    CHECK_EQ(t.Outcome, CM_EJ_OUT_GONE, "gone");
    CHECK(!CmEjGate(&t), "retired");

    /* The devnode verified gone by the executor's id check. */
    fakeInit(&f);
    f.RemoveCr = CM_EJ_CR_NO_SUCH_DEVNODE;
    f.Entry = CM_EJ_NONE;
    begin(&t, FALSE, 0);
    in = mkIn(5);
    drive(&t, &f, &in);
    CHECK_EQ(t.Phase, CM_EJ_P_DONE, "retired");
    CHECK_EQ(t.Outcome, CM_EJ_OUT_GONE, "gone");

    /* Vanished before the query was accepted changes nothing yet. */
    fakeInit(&f);
    f.Hold = TRUE;
    begin(&t, TRUE, 0);
    in = mkIn(5);
    drive(&t, &f, &in);
    in.Vanished = TRUE;
    drive(&t, &f, &in);
    CHECK_EQ(t.Phase, CM_EJ_P_IDENT, "waiting for the find");
    CHECK((t.Flags & CM_EJ_F_VANISHED) == 0, "not taken before the query");
}

static VOID test_consume(VOID)
{
    CM_EJ_TX t;
    FAKE f;
    CM_EJ_IN in;
    UCHAR r[CM_EJ_RESULT_BYTES];

    fakeInit(&f);
    f.QueryCr = 0x17UL;
    begin(&t, TRUE, 0);
    in = mkIn(5);
    drive(&t, &f, &in);
    CHECK(!CmEjConsume(&t, FALSE, 6), "not before the terminal step is written");
    CmEjResultEncode(&t, r, 6);
    CHECK(!CmEjConsume(&t, TRUE, 7), "the ask still there");
    CHECK(!CmEjSpent(&t), "held for the tool");
    CHECK(CmEjConsume(&t, TRUE, 6 + CM_EJ_CONSUME_MS), "60 s after");
    CHECK(CmEjSpent(&t), "let go");
    CHECK(!CmEjResultDue(&t, 70000), "consumed: written no more");

    fakeInit(&f);
    f.QueryCr = 0x17UL;
    begin(&t, TRUE, 0);
    drive(&t, &f, &in);
    CmEjResultEncode(&t, r, 6);
    CHECK(CmEjConsume(&t, FALSE, 7), "the ask deleted after a terminal step");
}

static VOID test_adopt(VOID)
{
    CM_EJ_TX t;
    FAKE f;
    CM_EJ_IN in;

    /* Authorised, stopped before its removal: the next start resumes. */
    fakeInit(&f);
    f.Hold = TRUE;
    begin(&t, TRUE, 0);
    in = mkIn(5);
    CmEjSubmitted(&t, CM_EJ_DO_FIND, TRUE, 5);
    CmEjFeed(&t, CM_EJ_FED_FIND, f.Devnode, 0, 5);
    CmEjSubmitted(&t, CM_EJ_DO_QUERY, TRUE, 5);
    CmEjFeed(&t, CM_EJ_FED_QUERY, 0, 0, 5);
    CmEjFeed(&t, CM_EJ_FED_REQUEST, 3, 9, 5);
    in.Answer = CM_EJ_ANS_CLEAN;
    drive(&t, &f, &in);
    CHECK_EQ(f.Held, CM_EJ_DO_REMOVE, "a removal running at the stop");
    CHECK(CmEjOwesRemoval(&t), "authorised: the detached block completes it");
    CmEjAdopt(&t, 100);
    f.Hold = FALSE;
    in = mkIn(100);
    in.SlotIdle = TRUE;
    CHECK_EQ(CmEjNext(&t, &in), CM_EJ_DO_NONE,
             "no lookup while the stopped start's call is out");
    CmEjFeed(&t, CM_EJ_FED_REMOVE, 0x17UL, 0, 120);
    drive(&t, &f, &in);
    CHECK_EQ(f.Calls[CM_EJ_DO_ADOPT], 1, "looked up by its id");
    CHECK_EQ(t.Phase, CM_EJ_P_REMOVE, "resumed where it stood");
    in.NowMs = 2100;
    drive(&t, &f, &in);
    CHECK_EQ(t.Phase, CM_EJ_P_DONE, "resumed and removed");
    CHECK((t.Flags & CM_EJ_F_CONSUMED) != 0, "the slot no longer the tool's");

    /* Gone from the tree: cleared. */
    fakeInit(&f);
    begin(&t, TRUE, 0);
    in = mkIn(5);
    drive(&t, &f, &in);
    CHECK_EQ(t.Phase, CM_EJ_P_ANSWER, "awaiting");
    CmEjAdopt(&t, 50);
    f.Devnode = 0;
    in.NowMs = 60;
    drive(&t, &f, &in);
    CHECK_EQ(t.Phase, CM_EJ_P_DONE, "retired by its disappearance");

    /* A stop during the query, its late result a refusal, the devnode
     * still there: no repeated query, no removal, the blocker released. */
    fakeInit(&f);
    f.Hold = TRUE;
    begin(&t, TRUE, 0);
    in = mkIn(5);
    drive(&t, &f, &in);
    CmEjFeed(&t, CM_EJ_FED_FIND, f.Devnode, 0, 5);
    drive(&t, &f, &in);
    CHECK_EQ(f.Held, CM_EJ_DO_QUERY, "the query running at the stop");
    CmEjAdopt(&t, 50);
    CmEjFeed(&t, CM_EJ_FED_QUERY, 0x17UL, 0, 70);
    f.Hold = FALSE;
    in.NowMs = 80;
    drive(&t, &f, &in);
    CHECK_EQ(t.Phase, CM_EJ_P_DONE, "released");
    CHECK_EQ(f.Calls[CM_EJ_DO_QUERY], 1, "never re-issued");
    CHECK_EQ(f.Calls[CM_EJ_DO_REMOVE], 0, "nothing removed");
}

static VOID test_decision(VOID)
{
    CM_EJ_DECISION d;

    CmEjDecideBegin(&d, 1, 0, XHCI_TBT_BDF(0, 0x1C, 0), 5, 1000, 2000);
    CHECK_EQ(CmEjDecideLeft(&d, 1500), 1500, "time left");
    CHECK(!CmEjDecideTake(&d, 4, XHCI_TBT_PORT_DOWN, TRUE, 1100, 1110),
          "another decision's reading");
    CHECK(!CmEjDecideExpire(&d, 2999), "not yet");
    CHECK(CmEjDecideTake(&d, 5, XHCI_TBT_PORT_DOWN, TRUE, 1100, 1110),
          "on time");
    CHECK_EQ(d.Reading, XHCI_TBT_PORT_DOWN, "the reading");
    CHECK(!CmEjDecideTake(&d, 5, XHCI_TBT_PORT_UP, TRUE, 1200, 1210), "once");
    CHECK_EQ(d.DecidedAt, 1110, "taken at the thread's clock");

    CmEjDecideBegin(&d, 1, 0, 0, 6, 1000, 2000);
    CHECK(CmEjDecideTake(&d, 6, XHCI_TBT_PORT_DOWN, TRUE, 3000, 3010),
          "late");
    CHECK_EQ(d.Reading, XHCI_TBT_PORT_ERROR, "late is no reading");

    /* Read in time, taken after the deadline: not published late. */
    CmEjDecideBegin(&d, 2, 0, 0, 8, 1000, 2000);
    CHECK(CmEjDecideTake(&d, 8, XHCI_TBT_PORT_DOWN, TRUE, 2900, 3000),
          "taken late");
    CHECK_EQ(d.Reading, XHCI_TBT_PORT_ERROR, "taken late is no reading");

    CmEjDecideBegin(&d, 2, 1, 0, 7, 0xFFFFFF00UL, 400);
    CHECK(!CmEjDecideExpire(&d, 0x10UL), "across the clock's wrap");
    CHECK(CmEjDecideExpire(&d, 0x90UL), "expired");
    CHECK_EQ(d.State, CM_EJ_D_DECIDED, "decided");
    CHECK(!CmEjDecideTake(&d, 7, XHCI_TBT_PORT_DOWN, TRUE, 0x91UL, 0x92UL),
          "a reading after expiry changes nothing");
}

/* A configuration space of one function, dword access only. */
static ULONG space[64];
static ULONG writes;

static ULONG rd(PVOID c, ULONG bdf, ULONG off, PULONG v)
{
    (void)c;
    (void)bdf;
    *v = space[(off & 0xFFUL) / 4];
    return 1;
}

static ULONG wr(PVOID c, ULONG bdf, ULONG off, ULONG v)
{
    (void)c;
    (void)bdf;
    writes++;
    /* The SHPC capability's first dword: only DWORD Select (byte 2) is
     * writable. */
    if (off == 0x40) {
        space[0x10] = (space[0x10] & 0xFF00FFFFUL) | (v & 0x00FF0000UL);
    } else {
        space[(off & 0xFFUL) / 4] = v;
    }
    return 1;
}

static VOID test_bytewrite(VOID)
{
    memset(space, 0, sizeof(space));
    space[0x10] = 0x5A00500CUL;      /* status 5Ah, select 00h, next, id */
    CHECK(CmEjByteWrite(rd, wr, NULL, 0, 0x42, 1, 0x07), "select written");
    CHECK_EQ(space[0x10], 0x5A07500CUL, "the other bytes as read");
    CHECK_EQ(writes, 1, "one dword write");
    CHECK(CmEjByteWrite(rd, wr, NULL, 0, 0x42, 1, 0x00), "restored");
    CHECK_EQ(space[0x10], 0x5A00500CUL, "as before");
    CHECK(!CmEjByteWrite(rd, wr, NULL, 0, 0x40, 1, 0x11),
          "a byte that does not take reads back wrong");
    CHECK(!CmEjByteWrite(rd, wr, NULL, 0, 0x42, 2, 0), "width 2 refused");
    space[0x11] = 0;
    CHECK(CmEjByteWrite(rd, wr, NULL, 0, 0x44, 4, 0xDEADBEEFUL), "dword");
    CHECK(!CmEjQualified9x(FALSE), "98 SE not admitted yet");
    CHECK(!CmEjQualified9x(TRUE), "ME not admitted yet");
}

int main(void)
{
    test_accept();
    test_clean();
    test_refused_plain();
    test_not_clean();
    test_failed();
    test_o6();
    test_vanish();
    test_edges();
    test_consume();
    test_adopt();
    test_decision();
    test_bytewrite();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
