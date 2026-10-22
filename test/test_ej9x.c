/*
 * test_ej9x.c - the tools' half of the Windows 98 SE and ME eject asked of
 * xhcicm.sys (xhciinst\inst_ej9x.c; roadmap-usb4.md task 43.18; design
 * record 21 section 7.2 part 1): the query token, the ask's layout, the
 * reply and the result read back, the wait's clocks (no answer, the ask
 * deleted and a late result, the budget, the progress rule, a result first
 * read at a later step), and what each end means.
 */

#include <stdio.h>
#include <string.h>
#include "../xhciinst/inst_core.h"
#include "../xhciinst/inst_ej9x.h"
#include "../src/cm/cm_status.h"
#include "../src/cm/cm_query.h"
#include "../src/cm/cm_ejask.h"
#include "test_harness.h"

static void put(unsigned char *b, unsigned long at, INST_U32 v)
{
    b[at] = (unsigned char)(v & 0xFFUL);
    b[at + 1] = (unsigned char)((v >> 8) & 0xFFUL);
    b[at + 2] = (unsigned char)((v >> 16) & 0xFFUL);
    b[at + 3] = (unsigned char)((v >> 24) & 0xFFUL);
}

static INST_U32 get(const unsigned char *b, unsigned long at)
{
    return (INST_U32)b[at] | ((INST_U32)b[at + 1] << 8) |
           ((INST_U32)b[at + 2] << 16) | ((INST_U32)b[at + 3] << 24);
}

static unsigned char answer[CM_Q_HDR_BYTES + CM_ST_HDR_BYTES];

static void mkAnswer(INST_U32 gen)
{
    memset(answer, 0, sizeof(answer));
    put(answer, CM_Q_O_MAGIC, CM_Q_MAGIC);
    put(answer, CM_Q_O_VERSION, CM_Q_VERSION);
    put(answer, CM_Q_O_BYTES, sizeof(answer));
    put(answer, CM_Q_O_PASS, 9);
    put(answer, CM_Q_O_LIVE, 1);
    put(answer, CM_Q_O_REC_OFFSET, CM_Q_HDR_BYTES);
    put(answer, CM_Q_O_EPOCH_LO, 0x1234UL);
    put(answer, CM_Q_O_EPOCH_HI, 0x5678UL);
    put(answer, CM_Q_O_CONTENT_GEN, gen);
    put(answer + CM_Q_HDR_BYTES, CM_ST_O_STAGE, CM_ST_STAGE_ACTIVE);
}

static void test_token(void)
{
    INST_EJ_TOKEN a;
    INST_EJ_TOKEN b;
    unsigned char ask[CM_EJ_ASK_BYTES];

    mkAnswer(5);
    CHECK(inst_ej_token(answer, sizeof(answer), &a), "parsed");
    CHECK(a.Eligible, "eligible");
    CHECK_EQ(a.ContentGen, 5, "generation");
    mkAnswer(6);
    CHECK(inst_ej_token(answer, sizeof(answer), &b), "parsed");
    CHECK(!inst_ej_same_token(&a, &b), "the content changed");
    put(answer, CM_Q_O_FLAGS, CM_Q_F_STOPPING);
    CHECK(inst_ej_token(answer, sizeof(answer), &b), "parsed");
    CHECK(!b.Eligible, "stopping: not eligible");
    mkAnswer(6);
    put(answer, CM_Q_O_LIVE, 2);
    (void)inst_ej_token(answer, sizeof(answer), &b);
    CHECK(!b.Eligible, "not live: not eligible");
    mkAnswer(6);
    put(answer, CM_Q_O_VERSION, CM_Q_VERSION + 1);
    CHECK(!inst_ej_token(answer, sizeof(answer), &b), "unknown version");
    mkAnswer(6);
    put(answer, CM_Q_O_VERSION, CM_Q_VERSION_1);
    CHECK(inst_ej_token(answer, sizeof(answer), &b), "version 1 still read");
    CHECK(!inst_ej_token(answer, CM_Q_HDR_BYTES - 1, &b), "short");

    CHECK_EQ(inst_ej_loc(4, 1, 2), 0x040AUL, "location word");
    inst_ej_ask(ask, 0xA5A5UL, inst_ej_loc(0, 0x1C, 0), inst_ej_loc(4, 0, 0),
                &a);
    CHECK_EQ(get(ask, CM_EJ_A_NONCE), 0xA5A5UL, "nonce at 0");
    CHECK_EQ(get(ask, CM_EJ_A_MEMBER), 0x00E0UL, "member at 4");
    CHECK_EQ(get(ask, CM_EJ_A_UNIT), 0x0400UL, "unit at 8");
    CHECK_EQ(get(ask, CM_EJ_A_EPOCH_LO), 0x1234UL, "epoch at 12");
    CHECK_EQ(get(ask, CM_EJ_A_CONTENT_GEN), 5, "generation at 20");
    CHECK_EQ(get(ask, CM_EJ_A_FLAGS), 0, "flags 0");
    CHECK(inst_ej_nonce(7, 0) != 0, "nonzero");
    CHECK(inst_ej_nonce(7, inst_ej_nonce(7, 0)) != inst_ej_nonce(7, 0),
          "differs from the last");
}

static void mkResult(unsigned char *r, INST_U32 nonce, INST_U32 step,
                     INST_U32 outcome, INST_U32 clock)
{
    memset(r, 0, CM_EJ_RESULT_BYTES);
    put(r, CM_EJ_S_NONCE, nonce);
    put(r, CM_EJ_S_ACCEPTED, CM_EJ_ACCEPTED);
    put(r, CM_EJ_S_STEP, step);
    put(r, CM_EJ_S_QUERY_CR, 0);
    put(r, CM_EJ_S_REMOVE_CR, 0);
    put(r, CM_EJ_S_OUTCOME, outcome);
    put(r, CM_EJ_S_CLOCK_MS, clock);
}

static void test_read(void)
{
    unsigned char b[CM_EJ_RESULT_BYTES];
    unsigned char rep[CM_EJ_REPLY_BYTES];
    INST_EJ_RESULT r;

    mkResult(b, 9, CM_EJ_STEP_ANSWERED, 0, 100);
    CHECK(inst_ej_result(b, sizeof(b), 9, &r), "ours");
    CHECK_EQ(r.Step, CM_EJ_STEP_ANSWERED, "a third step, read late");
    CHECK(!inst_ej_result(b, sizeof(b), 8, &r), "another nonce");
    put(b, CM_EJ_S_ACCEPTED, 0);
    CHECK(!inst_ej_result(b, sizeof(b), 9, &r), "no acceptance word");
    CHECK(!inst_ej_result(b, sizeof(b) - 1, 9, &r), "short");
    put(rep, CM_EJ_R_NONCE, 9);
    put(rep, CM_EJ_R_REASON, CM_EJ_WHY_BUSY);
    CHECK_EQ(inst_ej_reply(rep, sizeof(rep), 9), CM_EJ_WHY_BUSY, "reply");
    CHECK_EQ(inst_ej_reply(rep, sizeof(rep), 8), 0, "another's reply");
    CHECK(inst_ej_terminal(CM_EJ_STEP_REFUSED), "refused terminal");
    CHECK(inst_ej_terminal(CM_EJ_STEP_BLOCKED), "blocked terminal");
    CHECK(!inst_ej_terminal(CM_EJ_STEP_REMOVING), "removing not");
}

static void test_wait(void)
{
    INST_EJ_WAIT w;
    INST_EJ_RESULT r;
    INST_U32 t;

    /* Rejected at once. */
    inst_ej_wait_init(&w, 1000);
    CHECK_EQ(inst_ej_wait_step(&w, 1250, CM_EJ_WHY_EPOCH, NULL),
             INST_EJ_W_REJECTED, "rejected");

    /* No answer: deleted at 3 s, uncertain 3 s later - never "nothing
     * was changed". */
    inst_ej_wait_init(&w, 0);
    for (t = 250; t < CM_EJ_ANSWER_MS; t += 250) {
        CHECK_EQ(inst_ej_wait_step(&w, t, 0, NULL), INST_EJ_W_WAIT, "wait");
    }
    CHECK_EQ(inst_ej_wait_step(&w, CM_EJ_ANSWER_MS, 0, NULL),
             INST_EJ_W_DELETE, "the ask deleted at 3 s");
    CHECK_EQ(inst_ej_wait_step(&w, 5000, 0, NULL), INST_EJ_W_WAIT,
             "and 3 s more");
    CHECK_EQ(inst_ej_wait_step(&w, 6000, 0, NULL), INST_EJ_W_UNCERTAIN,
             "uncertain");

    /* A result after the deletion continues the transaction. */
    inst_ej_wait_init(&w, 0);
    (void)inst_ej_wait_step(&w, 3000, 0, NULL);
    memset(&r, 0, sizeof(r));
    r.Step = CM_EJ_STEP_QUERIED;
    r.Clock = 10;
    CHECK_EQ(inst_ej_wait_step(&w, 4000, 0, &r), INST_EJ_W_WAIT,
             "accepted late");
    r.Step = CM_EJ_STEP_REMOVED;
    r.Clock = 20;
    CHECK_EQ(inst_ej_wait_step(&w, 4250, 0, &r), INST_EJ_W_TERMINAL, "ended");

    /* Progress: a clock standing still 20 s ends the wait. */
    inst_ej_wait_init(&w, 0);
    r.Step = CM_EJ_STEP_REQUESTED;
    r.Clock = 50;
    (void)inst_ej_wait_step(&w, 250, 0, &r);
    CHECK_EQ(inst_ej_wait_step(&w, 250 + CM_EJ_PROGRESS_MS - 1, 0, &r),
             INST_EJ_W_WAIT, "not yet");
    CHECK_EQ(inst_ej_wait_step(&w, 250 + CM_EJ_PROGRESS_MS, 0, &r),
             INST_EJ_W_UNCERTAIN, "no progress");

    /* The budget governs even with progress. */
    inst_ej_wait_init(&w, 0);
    for (t = 250; t < 250 + CM_EJ_BUDGET_MS; t += 250) {
        r.Clock = t;
        CHECK_EQ(inst_ej_wait_step(&w, t, 0, &r), INST_EJ_W_WAIT,
                 "inside the budget");
    }
    r.Clock = t;
    CHECK_EQ(inst_ej_wait_step(&w, t, 0, &r), INST_EJ_W_UNCERTAIN,
             "the budget spent");
}

static void test_outcome(void)
{
    INST_EJ_RESULT r;
    const char *text;
    int nc;

    memset(&r, 0, sizeof(r));
    r.Step = CM_EJ_STEP_REFUSED;
    r.QueryCr = INST_CR_REMOVE_VETOED;
    CHECK_EQ(inst_ej_outcome(&r, &text, &nc), INST_EJ_VETO, "a veto");
    CHECK_EQ(inst_eject_code(INST_EJ_VETO), 4, "exit 4");
    r.QueryCr = INST_CR_NO_SUCH_DEVNODE;
    CHECK_EQ(inst_ej_outcome(&r, &text, &nc), INST_EJ_CHANGED, "changed");
    CHECK_EQ(inst_eject_code(INST_EJ_CHANGED), 1, "exit 1");
    r.QueryCr = INST_CR_CALL_NOT_IMPLEMENTED;
    CHECK_EQ(inst_ej_outcome(&r, &text, &nc), INST_EJ_UNSUPPORTED, "unsup");
    r.QueryCr = 0x22UL;
    CHECK_EQ(inst_ej_outcome(&r, &text, &nc), INST_EJ_VETO, "any other");

    r.Step = CM_EJ_STEP_REMOVED;
    r.Outcome = CM_EJ_OUT_CLEAN;
    CHECK_EQ(inst_ej_outcome(&r, &text, &nc), INST_EJ_DONE, "removed");
    CHECK(!nc, "clean");
    r.Outcome = CM_EJ_OUT_NOT_CLEAN;
    (void)inst_ej_outcome(&r, &text, &nc);
    CHECK(nc, "not clean");
    r.Outcome = CM_EJ_OUT_ISOLATED;
    (void)inst_ej_outcome(&r, &text, &nc);
    CHECK(nc, "after isolation");
    r.Outcome = CM_EJ_OUT_PLAIN;
    (void)inst_ej_outcome(&r, &text, &nc);
    CHECK(!nc, "no request");

    r.Step = CM_EJ_STEP_BLOCKED;
    r.Outcome = CM_EJ_OUT_NO_STOP;
    CHECK_EQ(inst_ej_outcome(&r, &text, &nc), INST_EJ_FAILED, "blocked");
    CHECK_EQ(inst_eject_code(INST_EJ_FAILED), 2, "exit 2");
    CHECK(strstr(text, "shut down Windows") != NULL, "its text");
    r.Outcome = CM_EJ_OUT_KEEPS_FAILING;
    r.RemoveCr = INST_CR_REMOVE_VETOED;
    CHECK_EQ(inst_ej_outcome(&r, &text, &nc), INST_EJ_FAILED, "O6");

    CHECK_EQ(inst_ej_rejected(CM_EJ_WHY_GENERATION, &text), INST_EJ_CHANGED,
             "a stale ask");
    CHECK_EQ(inst_ej_rejected(CM_EJ_WHY_BUSY, &text), INST_EJ_CHANGED,
             "busy: exit 1");
    CHECK_EQ(inst_ej_rejected(CM_EJ_WHY_NO_CHANNEL, &text),
             INST_EJ_UNSUPPORTED, "no channel: exit 1");
    CHECK(strstr(text, "Stor98") != NULL, "says why");
    CHECK(strstr(text, "Shut down Windows before you unplug it") != NULL,
          "says what to do instead");
    CHECK(strstr(inst_ej_uncertain_text(), "did not answer") != NULL,
          "the uncertain text");
}

int main(void)
{
    test_token();
    test_read();
    test_wait();
    test_outcome();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
