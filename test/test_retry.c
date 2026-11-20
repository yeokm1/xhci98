/*
 * test_retry.c - host vectors for a controller stopped after repeated
 * failures, tried again (src\xhci_retry.c; roadmap-usb4.md task 50.31,
 * design record 38): the value's reading, the waits and their backing off,
 * the clean run, the machine and its burst, the port episodes and their
 * continuity, the storm limit and the deadline's precedence, the
 * admission's rows, the leave flag's table, the end of a latched resume's
 * FAILED phase, the commit's one decision and the revalidation, the
 * healthy pass, the sweep's holds and its end, the status words and the
 * snapshot block's shape.
 */

#include <stdio.h>
#include <string.h>
#include "../src/xhci.h"
#include "../src/xhci_retry.h"
#include "test_harness.h"

/* The system time in units, moving 100 times as fast as the ticks: the
 * floor of record 38 section 5.7 is met wherever the ticks are, so the
 * tick vectors read as before; test_floor holds the floor itself. */
#define TS(now) ((ULONG)(now) * 100UL)

/* A latched default controller at the clock `now`. */
static void latched(PXHCI_TOL_RETRY r, ULONG value, ULONG terminal,
                    ULONG now)
{
    XhciTolRetryInit(r, 1, value, 1);
    CHECK_EQ(XhciTolRetryObserve(r, terminal, 0, now, TS(now)), XHCI_RETRY_EV_LATCHED,
             "latched");
}

static void test_value(void)
{
    CHECK_EQ(XhciTolRetryValue(0, 0), 5, "absent: 5");
    CHECK_EQ(XhciTolRetryValue(0, 77), 5, "absent: 5 whatever is passed");
    CHECK_EQ(XhciTolRetryValue(XHCI_TOL_RETRY_OTHER_TYPE, 7), 5,
             "another type: 5");
    CHECK_EQ(XhciTolRetryValue(XHCI_TOL_RETRY_OTHER_TYPE, 0), 5,
             "another type is never off");
    CHECK_EQ(XhciTolRetryValue(1, 0), 0, "0: off");
    CHECK_EQ(XhciTolRetryValue(1, 1), 5, "1 invalid: 5");
    CHECK_EQ(XhciTolRetryValue(1, 2), 2, "2");
    CHECK_EQ(XhciTolRetryValue(1, 5), 5, "5");
    CHECK_EQ(XhciTolRetryValue(1, 600), 600, "600");
    CHECK_EQ(XhciTolRetryValue(1, 601), 5, "601 invalid: 5");
    CHECK_EQ(XhciTolRetryValue(1, 3600), 5, "3600 invalid: 5");
    CHECK_EQ(XhciTolRetryValue(1, 0xFFFFFFFFUL), 5, "all ones invalid: 5");

    CHECK_EQ(XhciTolRetryScale(0, 9), 1, "scale absent: 1");
    CHECK_EQ(XhciTolRetryScale(1, 0), 1, "scale 0: 1");
    CHECK_EQ(XhciTolRetryScale(1, 1), 1, "scale 1: 1");
    CHECK_EQ(XhciTolRetryScale(1, 60), 60, "scale 60");
    CHECK_EQ(XhciTolRetryScale(1, 601), 600, "scale held to 600");
    CHECK_EQ(XhciTolRetryScaled(13335, 600), 22, "scaled ceiling");
    CHECK_EQ(XhciTolRetryScaled(113, 600), 1, "scaled floor of one tick");
    CHECK_EQ(XhciTolRetryScaled(113, 1), 113, "unscaled");
}

static void test_init(void)
{
    XHCI_TOL_RETRY r;

    XhciTolRetryInit(&r, 1, 0, 1);
    CHECK_EQ(r.State, XHCI_RETRY_OFF, "value 0: OFF");
    CHECK_EQ(XhciTolRetryObserve(&r, XHCI_TOL_TERMINAL_WINDOW, 0, 5, TS(5)),
             XHCI_RETRY_EV_NONE, "OFF never latches");
    CHECK_EQ(r.State, XHCI_RETRY_OFF, "OFF stays");

    XhciTolRetryInit(&r, XHCI_TOL_RETRY_OTHER_TYPE, 0, 1);
    CHECK_EQ(r.Found, XHCI_TOL_RETRY_OTHER_TYPE, "another type kept");
    CHECK_EQ(r.Value, 5, "another type: 5");
    XhciTolRetryInit(&r, 0, 0, 1);
    CHECK_EQ(r.State, XHCI_RETRY_WATCHING, "absent: WATCHING");
    CHECK_EQ(r.Value, 5, "absent: 5");
    CHECK_EQ(r.Base, 51, "B at 5 s: 51 ticks (section 5.7)");
    CHECK_EQ(r.Ceiling, 6001, "C: 6001 ticks");
    CHECK_EQ(r.CleanTicks, 6001, "clean run: ten minutes at 100 ms");
    CHECK_EQ(r.StormTicks, 6001, "storm window: ten minutes at 100 ms");
    CHECK_EQ(sizeof(XHCI_TOL_RETRY) % 8, 0, "an even number of ULONGs");

    XhciTolRetryInit(&r, 1, 2, 1);
    CHECK_EQ(r.Base, 21, "B at 2 s: 21 ticks");
    XhciTolRetryInit(&r, 1, 600, 1);
    CHECK_EQ(r.Base, 6001, "B at 600 s: the ceiling");
    CHECK_EQ(r.Ceiling, 6001, "C at 600 s");

    XhciTolRetryInit(&r, 1, 5, 600);
    CHECK_EQ(r.Base, 1, "scaled B");
    CHECK_EQ(r.Ceiling, 10, "scaled C");
    CHECK_EQ(r.CleanTicks, 10, "scaled clean run");
    CHECK_EQ(r.StormTicks, 10, "scaled storm window");
}

static void test_waits(void)
{
    XHCI_TOL_RETRY r;
    ULONG want[8];
    ULONG l;

    XhciTolRetryInit(&r, 1, 5, 1);
    /* Doubled in milliseconds, then converted (section 5.7). */
    want[0] = 51;
    want[1] = 101;
    want[2] = 201;
    want[3] = 401;
    want[4] = 801;
    want[5] = 1601;
    want[6] = 3201;
    want[7] = 6001;
    for (l = 0; l < 8; l++) {
        CHECK_EQ(XhciTolRetryWait(&r, l), want[l], "W(L) at 5 s");
    }
    CHECK_EQ(XhciTolRetryWait(&r, 30), 6001, "W held at the ceiling");
    CHECK_EQ(XhciTolRetryWait(&r, 0xFFFFFFFFUL), 6001, "W, a huge level");
    CHECK_EQ(XhciTolRetryLevelUp(&r, 6), 7, "level 6 to 7");
    CHECK_EQ(XhciTolRetryLevelUp(&r, 7), 7, "level held at its cap");

    XhciTolRetryInit(&r, 1, 2, 1);
    for (l = 0; l < 20 && XhciTolRetryWait(&r, l) < r.Ceiling; l++) {
        ;
    }
    CHECK_EQ(l, 9, "the cap level at 2 s is 9");
    CHECK_EQ(XhciTolRetryLevelUp(&r, 9), 9, "held at 9");

    XhciTolRetryInit(&r, 1, 600, 1);
    CHECK_EQ(XhciTolRetryWait(&r, 0), 6001, "600: no doubling");
    CHECK_EQ(XhciTolRetryWait(&r, 3), 6001, "600: no doubling at 3");
    CHECK_EQ(XhciTolRetryLevelUp(&r, 0), 0, "600: the level stays 0");
}

static void test_latch_and_timed_try(void)
{
    XHCI_TOL_RETRY r;
    ULONG now;
    ULONG n;

    latched(&r, 5, XHCI_TOL_TERMINAL_WINDOW, 1000);
    CHECK_EQ(r.State, XHCI_RETRY_LATCHED, "LATCHED");
    CHECK_EQ(r.LatchWait, 51, "the first wait");
    CHECK_EQ(XhciTolRetryNextSeconds(&r, 1000, TS(1000)), 6, "NEXT_S 51 ticks: 6");
    CHECK_EQ(XhciTolRetryNextSeconds(&r, 1040, TS(1040)), 2, "NEXT_S 11 ticks: 2");
    CHECK_EQ(XhciTolRetryDueStep(&r, 0, 1050, TS(1050)), XHCI_RETRY_CAUSE_NONE,
             "one tick short");
    CHECK_EQ(XhciTolRetryDueStep(&r, 0, 1051, TS(1051)), XHCI_RETRY_CAUSE_WAIT,
             "the deadline");
    CHECK_EQ(r.State, XHCI_RETRY_DUE, "DUE");
    CHECK_EQ(XhciTolRetryNextSeconds(&r, 1051, TS(1051)), 0, "NEXT_S 0 when due");
    CHECK_EQ(XhciTolRetryArm(&r), 1, "armed");
    CHECK_EQ(XhciTolRetryArm(&r), 0, "armed once");
    now = 1120;
    n = XhciTolRetryCommit(&r, now);
    CHECK_EQ(n, 1, "try 1");
    CHECK_EQ(r.State, XHCI_RETRY_TRYING, "TRYING");
    CHECK_EQ(r.Level, 1, "level raised");
    CHECK_EQ(r.LastWait, 120, "the wait just ended");
    CHECK_EQ(r.LastCause, XHCI_RETRY_CAUSE_WAIT, "cause the wait");
    CHECK_EQ(r.Burst, 1, "the burst begun");
    CHECK_EQ(r.BurstAttempts, 1, "its first attempt");
    CHECK_EQ(r.Armed, 0, "disarmed");
    CHECK_EQ(XhciTolRetryNextSeconds(&r, now, TS(now)), XHCI_RETRY_NEXT_NONE,
             "NEXT_S none while TRYING");

    /* The burst: two more failed attempts, then the run spent. */
    XhciTolRetryBurstAttempt(&r);
    XhciTolRetryBurstAttempt(&r);
    CHECK_EQ(r.BurstAttempts, 3, "three attempts");
    XhciTolRetryBurstEnd(&r, XHCI_RETRY_BURST_LATCHED);
    CHECK_EQ(r.LastBurstAttempts, 3, "the burst's attempts kept");
    CHECK_EQ(r.LastBurstOutcome, XHCI_RETRY_BURST_LATCHED, "outcome 2");
    CHECK_EQ(r.Burst, 0, "the burst ended");
    CHECK_EQ(XhciTolRetryObserve(&r, XHCI_TOL_TERMINAL_FAILURES, 0, 2000, TS(2000)),
             XHCI_RETRY_EV_LATCHED, "latched again");
    CHECK_EQ(r.LatchWait, 101, "the second wait doubled");
    CHECK_EQ(r.LatchStamp, 2000, "anchored at the relatch");
    XhciTolRetryBurstAttempt(&r);
    CHECK_EQ(r.BurstAttempts, 3, "no counting outside a burst");
}

/* A latched controller made due and armed at the deadline, with a window
 * that refused a fourth recovery and a latch's words as the driver holds
 * them. */
static void armed_due(PXHCI_TOL_RETRY r, PXHCI_TOL_WINDOW w,
                      PXHCI_TOL_RETRY_LATCH l, ULONG guardSet)
{
    latched(r, 5, XHCI_TOL_TERMINAL_WINDOW, 0);
    CHECK_EQ(XhciTolRetryDueStep(r, 0, 113, TS(113)), XHCI_RETRY_CAUSE_WAIT, "due");
    CHECK_EQ(XhciTolRetryArm(r), 1, "armed");
    w->Count = 3;
    w->Stamp[0] = 10;
    w->Stamp[1] = 20;
    w->Stamp[2] = 30;
    w->Refused = 1;
    l->Failures = 3;
    l->TerminalReleased = 1;
    l->Requested = 0;
    l->GuardSet = guardSet;
}

/* Record 38 sections 7.3 and 7.4: the commit's one decision, all or
 * nothing; the revalidation's three outcomes, the refusing two reachable
 * only here. */
static void test_commit(void)
{
    XHCI_TOL_RETRY r;
    XHCI_TOL_RETRY before;
    XHCI_TOL_WINDOW w;
    XHCI_TOL_WINDOW w0;
    XHCI_TOL_WINDOW fresh;
    XHCI_TOL_RETRY_LATCH l;
    XHCI_TOL_RETRY_LATCH l0;
    ULONG reval;
    ULONG i;

    CHECK_EQ(XhciTolRetryRevalOutcome(0, 0, 0), XHCI_RETRY_REVAL_NOT_NEEDED,
             "guard not set: no revalidation");
    CHECK_EQ(XhciTolRetryRevalOutcome(1, 1, 1), XHCI_RETRY_REVAL_EQUAL,
             "seven reads equal the decode");
    CHECK_EQ(XhciTolRetryRevalOutcome(1, 1, 0), XHCI_RETRY_REVAL_DIFFERENT,
             "seven reads differ");
    CHECK_EQ(XhciTolRetryRevalOutcome(1, 0, 1), XHCI_RETRY_REVAL_FAILED,
             "the derivation failed");

    /* Not armed: nothing at all. */
    latched(&r, 5, XHCI_TOL_TERMINAL_WINDOW, 0);
    before = r;
    CHECK_EQ(XhciTolRetryCommitAdmit(&r, XHCI_RETRY_ADMIT_OK), 0,
             "not armed: no commit");
    CHECK(memcmp(&r, &before, sizeof(r)) == 0, "... nothing moved");

    /* The admission's answers: a waiting one keeps the try due and counts
     * the wait; an off, excluded or not-latched one drops the arming. */
    armed_due(&r, &w, &l, 0);
    CHECK_EQ(XhciTolRetryCommitAdmit(&r, XHCI_RETRY_ADMIT_WAIT_LEAVE), 0,
             "a leave: no commit");
    CHECK_EQ(r.State, XHCI_RETRY_DUE, "... still due");
    CHECK_EQ(r.Armed, 0, "... re-armed by the thread");
    CHECK_EQ(r.WaitLeave, 1, "... WaitLeave counted");
    CHECK_EQ(XhciTolRetryArm(&r), 1, "re-armed");
    CHECK_EQ(XhciTolRetryCommitAdmit(&r, XHCI_RETRY_ADMIT_WAIT_POWER), 0,
             "not in D0: no commit");
    CHECK_EQ(r.WaitPower, 1, "... WaitPower counted");
    CHECK_EQ(XhciTolRetryArm(&r), 1, "re-armed");
    CHECK_EQ(XhciTolRetryCommitAdmit(&r, XHCI_RETRY_ADMIT_WAIT_RELEASE), 0,
             "a release pending: no commit");
    CHECK_EQ(r.WaitPower, 2, "... counted with the power waits");
    CHECK_EQ(r.State, XHCI_RETRY_DUE, "... still due");
    for (i = XHCI_RETRY_ADMIT_OFF; i <= XHCI_RETRY_ADMIT_NOT_LATCHED; i++) {
        armed_due(&r, &w, &l, 0);
        CHECK_EQ(XhciTolRetryCommitAdmit(&r, i), 0, "refused: no commit");
        CHECK_EQ(r.Armed, 0, "... disarmed");
        CHECK_EQ(r.WaitLeave + r.WaitPower, 0, "... no wait counted");
    }
    armed_due(&r, &w, &l, 0);
    CHECK_EQ(XhciTolRetryCommitAdmit(&r, XHCI_RETRY_ADMIT_OK), 1,
             "admitted: on to USBSTS");
    CHECK_EQ(r.Armed, 1, "... still armed");

    /* All ones: a skip; the window, the latch and the guard untouched. */
    armed_due(&r, &w, &l, 1);
    w0 = w;
    l0 = l;
    CHECK_EQ(XhciTolRetryCommitFinish(&r, &w, &l, 0xFFFFFFFFUL,
                                      XHCI_RETRY_REVAL_NOT_NEEDED, 500, TS(500)),
             XHCI_RETRY_COMMIT_SKIP_ONES, "all ones: a skip");
    CHECK(memcmp(&w, &w0, sizeof(w)) == 0, "... the window untouched");
    CHECK(memcmp(&l, &l0, sizeof(l)) == 0, "... the latch untouched");
    CHECK_EQ(r.State, XHCI_RETRY_LATCHED, "... latched again");
    CHECK_EQ(r.Level, 1, "... the level raised");
    CHECK_EQ(r.LatchStamp, 500, "... a new wait from now");
    CHECK_EQ(r.LatchWait, 101, "... at the raised level");
    CHECK_EQ(r.SkipOnes, 1, "... counted");
    CHECK_EQ(r.Tries, 0, "... no try counted");
    CHECK_EQ(r.Burst, 0, "... no burst");

    /* The guard set: a different or failed derivation is a skip, and so
     * is a revalidation not made; the guard stays. */
    for (reval = XHCI_RETRY_REVAL_NOT_NEEDED;
         reval <= XHCI_RETRY_REVAL_FAILED; reval++) {
        if (reval == XHCI_RETRY_REVAL_EQUAL) {
            continue;
        }
        armed_due(&r, &w, &l, 1);
        w0 = w;
        l0 = l;
        CHECK_EQ(XhciTolRetryCommitFinish(&r, &w, &l, 0x00000001UL, reval,
                                          500, TS(500)),
                 XHCI_RETRY_COMMIT_SKIP_LAYOUT, "the layout: a skip");
        CHECK(memcmp(&w, &w0, sizeof(w)) == 0, "... the window untouched");
        CHECK(memcmp(&l, &l0, sizeof(l)) == 0,
              "... the latch and the guard untouched");
        CHECK_EQ(r.SkipLayout, 1, "... counted");
        CHECK_EQ(r.SkipOnes, 0, "... not as all ones");
        CHECK_EQ(r.State, XHCI_RETRY_LATCHED, "... latched again");
        CHECK_EQ(r.Level, 1, "... the level raised");
        CHECK_EQ(r.Tries, 0, "... no try counted");
    }

    /* The guard set and the reads equal: the guard cleared, the latch
     * reset, the try committed. */
    XhciTolWindowInit(&fresh);
    armed_due(&r, &w, &l, 1);
    CHECK_EQ(XhciTolRetryCommitFinish(&r, &w, &l, 0x00000001UL,
                                      XHCI_RETRY_REVAL_EQUAL, 500, TS(500)),
             XHCI_RETRY_COMMIT_DONE, "revalidated: committed");
    CHECK(memcmp(&w, &fresh, sizeof(w)) == 0, "... the window afresh");
    CHECK_EQ(l.Failures, 0, "... the run of failures reset");
    CHECK_EQ(l.TerminalReleased, 0, "... the terminal release reset");
    CHECK_EQ(l.Requested, 1, "... the recovery requested");
    CHECK_EQ(l.GuardSet, 0, "... the guard cleared");
    CHECK_EQ(r.State, XHCI_RETRY_TRYING, "... TRYING");
    CHECK_EQ(r.Tries, 1, "... try 1");
    CHECK_EQ(r.Burst, 1, "... the burst begun");
    CHECK_EQ(r.Armed, 0, "... disarmed");

    /* The guard not set: committed, the guard never touched. */
    armed_due(&r, &w, &l, 0);
    CHECK_EQ(XhciTolRetryCommitFinish(&r, &w, &l, 0x00000001UL,
                                      XHCI_RETRY_REVAL_NOT_NEEDED, 500, TS(500)),
             XHCI_RETRY_COMMIT_DONE, "no guard: committed");
    CHECK_EQ(l.GuardSet, 0, "... the guard still clear");
    CHECK_EQ(l.Requested, 1, "... the recovery requested");
    CHECK_EQ(w.Refused, 0, "... the window's refusal gone");
}

/* Record 38 section 5.4: the clean run's healthy pass, each condition false
 * in turn restarting the run. */
static void test_healthy(void)
{
    XHCI_TOL_RETRY_HEALTH ok;
    XHCI_TOL_RETRY_HEALTH h;
    XHCI_TOL_RETRY r;
    ULONG i;
    ULONG t;

    ok.Terminal = XHCI_TOL_TERMINAL_NONE;
    ok.Requested = 0;
    ok.Initialized = 1;
    ok.ExtSuspended = 0;
    ok.D0 = 1;
    ok.SuspendedInD0 = 0;
    ok.Started = 1;
    ok.Leave = 0;
    ok.ShutLatched = 0;
    ok.Pad = 0;
    CHECK_EQ(XhciTolRetryHealthy(&ok), 1, "every condition: healthy");
    for (i = 0; i < 9; i++) {
        h = ok;
        switch (i) {
        case 0: h.Terminal = XHCI_TOL_TERMINAL_FAILURES; break;
        case 1:
            /* An owed request on a failed controller. */
            h.Terminal = XHCI_TOL_TERMINAL_OWED;
            h.Requested = 1;
            break;
        case 2: h.Initialized = 0; break;
        case 3: h.ExtSuspended = 1; break;
        case 4: h.D0 = 0; break;
        case 5: h.SuspendedInD0 = 1; break;
        case 6: h.Started = 0; break;
        case 7: h.Leave = 1; break;
        default: h.ShutLatched = 1; break;
        }
        CHECK_EQ(XhciTolRetryHealthy(&h), 0, "one condition false");

        /* A run being timed: that pass restarts it. */
        latched(&r, 5, XHCI_TOL_TERMINAL_WINDOW, 0);
        (VOID)XhciTolRetryDueStep(&r, 0, 113, TS(113));
        (VOID)XhciTolRetryArm(&r);
        (VOID)XhciTolRetryCommit(&r, 120);
        XhciTolRetryBurstEnd(&r, XHCI_RETRY_BURST_RECOVERED);
        t = 200;
        CHECK_EQ(XhciTolRetryCleanPass(&r, XhciTolRetryHealthy(&ok), 0, t), 0,
                 "armed");
        CHECK_EQ(XhciTolRetryCleanPass(&r, XhciTolRetryHealthy(&h), 0,
                                       t + r.CleanTicks), 0,
                 "an unhealthy pass at the end: no reset");
        CHECK_EQ(r.CleanArmed, 0, "... the run restarted");
        CHECK_EQ(r.Level, 1, "... the level kept");
    }
    /* 50.31 (stale request): a request left standing with the controller
     * not failed (a resume's reinitialization served it) is not one owed. */
    h = ok;
    h.Requested = 1;
    CHECK_EQ(XhciTolRetryHealthy(&h), 1,
             "a served request at terminal 0: healthy");
    t = 200;
    CHECK_EQ(XhciTolRetryCleanPass(&r, XhciTolRetryHealthy(&h), 0, t), 0,
             "armed again with the served request standing");
    CHECK_EQ(XhciTolRetryCleanPass(&r, XhciTolRetryHealthy(&h), 0,
                                   t + r.CleanTicks), 1,
             "... the backing off ends");
    CHECK_EQ(r.Level, 0, "... the level back to 0");
    latched(&r, 5, XHCI_TOL_TERMINAL_WINDOW, 0);
    (VOID)XhciTolRetryDueStep(&r, 0, 113, TS(113));
    (VOID)XhciTolRetryArm(&r);
    (VOID)XhciTolRetryCommit(&r, 120);
    XhciTolRetryBurstEnd(&r, XHCI_RETRY_BURST_RECOVERED);
    t = 200;
    CHECK_EQ(XhciTolRetryCleanPass(&r, XhciTolRetryHealthy(&ok), 0, t), 0,
             "armed again");
    CHECK_EQ(XhciTolRetryCleanPass(&r, XhciTolRetryHealthy(&ok), 0,
                                   t + r.CleanTicks), 1,
             "healthy throughout: the backing off ends");
}

/* Record 38 sections 6.1 to 6.3: the sweep's end - a discarded sweep gives
 * no baseline, episode or observer feed and drops every managed port's
 * evidence; a gap drops it and then takes baselines; PP clear drops one
 * port's; the edge and the episode. */
static void test_sweep_end(void)
{
    XHCI_TOL_RETRY_PORT ports[4];
    ULONG sweep[4];
    UCHAR managed[4];
    UCHAR feed[4];
    XHCI_TOL_RETRY r;
    ULONG i;
    ULONG gen;
    ULONG hold;
    ULONG stable;

    stable = XHCI_TOL_STABLE_DISC_TICKS;
    for (i = 0; i < 4; i++) {
        XhciTolRetryPortBreak(&ports[i]);
        sweep[i] = XHCI_PORTSC_PP | XHCI_PORTSC_CCS;
        managed[i] = (UCHAR)(i != 2);
    }
    /* Baselines: no evidence from the first sample. */
    CHECK_EQ(XhciTolRetrySweepEnd(ports, sweep, managed, 4, 0, 0, 100, stable,
                                  feed), 0, "baselines: no episode");
    CHECK_EQ(ports[0].HaveBase, 1, "... port 1's baseline");
    CHECK_EQ(ports[0].Base, 1, "... connected");
    CHECK_EQ(feed[0], XHCI_RETRY_FEED_SAMPLE | XHCI_RETRY_FEED_CCS,
             "... the observer fed CCS, no edge");
    CHECK_EQ(feed[2], 0, "... an unmanaged port: nothing");
    CHECK_EQ(ports[2].HaveBase, 0, "... nor its evidence");
    /* Port 1 unplugged: an edge and a candidate. */
    sweep[0] = XHCI_PORTSC_PP;
    CHECK_EQ(XhciTolRetrySweepEnd(ports, sweep, managed, 4, 0, 0, 101, stable,
                                  feed), 0, "a candidate: no episode yet");
    CHECK_EQ(feed[0], XHCI_RETRY_FEED_SAMPLE | XHCI_RETRY_FEED_EDGE,
             "... the edge fed");
    CHECK_EQ(ports[0].CandArmed, 1, "... the candidate armed");

    /* The sweep discarded: every managed port's evidence dropped and the
     * observer told so; nothing judged, though port 1 has been stable. */
    CHECK_EQ(XhciTolRetrySweepEnd(ports, sweep, managed, 4, 1, 0,
                                  101 + stable, stable, feed), 0,
             "discarded: no episode");
    for (i = 0; i < 4; i++) {
        if (i == 2) {
            CHECK_EQ(feed[i], 0, "... unmanaged: nothing");
            continue;
        }
        CHECK_EQ(feed[i], XHCI_RETRY_FEED_BREAK, "... dropped, no feed");
        CHECK_EQ(ports[i].HaveBase, 0, "... no baseline");
        CHECK_EQ(ports[i].CandArmed, 0, "... no candidate");
        CHECK_EQ(ports[i].HaveLast, 0, "... no last sample");
    }
    /* The next sweep whole: baselines afresh, port 1 now disconnected. */
    CHECK_EQ(XhciTolRetrySweepEnd(ports, sweep, managed, 4, 0, 0, 200, stable,
                                  feed), 0, "afresh: no episode");
    CHECK_EQ(ports[0].Base, 0, "... the baseline the unplugged state");
    CHECK_EQ(feed[0], XHCI_RETRY_FEED_SAMPLE, "... fed, no edge after a drop");
    /* A replug held for the stable interval: the episode. */
    sweep[0] = XHCI_PORTSC_PP | XHCI_PORTSC_CCS;
    CHECK_EQ(XhciTolRetrySweepEnd(ports, sweep, managed, 4, 0, 0, 201, stable,
                                  feed), 0, "a replug: a candidate");
    CHECK_EQ(XhciTolRetrySweepEnd(ports, sweep, managed, 4, 0, 0,
                                  201 + stable, stable, feed), 1,
             "held: one episode");
    CHECK_EQ(feed[0], XHCI_RETRY_FEED_SAMPLE | XHCI_RETRY_FEED_CCS |
                          XHCI_RETRY_FEED_EPISODE, "... at port 1");

    /* A gap: dropped first, then baselines from this sweep, no episode. */
    sweep[0] = XHCI_PORTSC_PP;
    CHECK_EQ(XhciTolRetrySweepEnd(ports, sweep, managed, 4, 0, 0, 300, stable,
                                  feed), 0, "a candidate");
    CHECK_EQ(XhciTolRetrySweepEnd(ports, sweep, managed, 4, 0, 1,
                                  300 + stable, stable, feed), 0,
             "a gap: no episode across it");
    CHECK_EQ(feed[0], XHCI_RETRY_FEED_BREAK | XHCI_RETRY_FEED_SAMPLE,
             "... dropped, then fed");
    CHECK_EQ(ports[0].HaveBase, 1, "... a fresh baseline");
    CHECK_EQ(ports[0].CandArmed, 0, "... no candidate");

    /* PP clear: that port's evidence dropped, never an episode. */
    sweep[1] = 0;
    CHECK_EQ(XhciTolRetrySweepEnd(ports, sweep, managed, 4, 0, 0, 400, stable,
                                  feed), 0, "PP clear: no episode");
    CHECK_EQ(feed[1], XHCI_RETRY_FEED_BREAK, "... dropped");
    CHECK_EQ(ports[1].HaveBase, 0, "... no baseline");
    CHECK_EQ(feed[3], XHCI_RETRY_FEED_SAMPLE | XHCI_RETRY_FEED_CCS,
             "... the others sampled");

    /* A port the sweep did not reach. */
    sweep[3] = XHCI_RETRY_NOT_SAMPLED;
    (VOID)XhciTolRetrySweepEnd(ports, sweep, managed, 4, 0, 0, 401, stable,
                               feed);
    CHECK_EQ(feed[3], 0, "not sampled: nothing");

    /* The driver's sequence: the leave generation moved between two port
     * holds discards the sweep whole, a missed pass counted. */
    latched(&r, 5, XHCI_TOL_TERMINAL_WINDOW, 0);
    gen = r.LeaveGen;
    CHECK_EQ(XhciTolRetrySweepHold(1, gen, r.LeaveGen, XHCI_PORTSC_PP),
             XHCI_RETRY_HOLD_READ, "the first port read");
    XhciTolRetryLeaveWrite(&r, XHCI_RETRY_PNP_QUERY_STOP,
                           XHCI_RETRY_PHASE_ENTRY, XHCI_RETRY_OUT_OK);
    XhciTolRetryLeaveWrite(&r, XHCI_RETRY_PNP_CANCEL_STOP,
                           XHCI_RETRY_PHASE_OUTCOME, XHCI_RETRY_OUT_OK);
    CHECK_EQ(r.Leave, 0, "a raise and a clear");
    hold = XhciTolRetrySweepHold(1, gen, r.LeaveGen, XHCI_PORTSC_PP);
    CHECK_EQ(hold, XHCI_RETRY_HOLD_DISCARD, "... the second hold discards");
    CHECK_EQ(XhciTolRetrySweepMissed(&r, 0, 0), 0, "a missed pass, no note");
    CHECK_EQ(r.MissedPasses, 1, "... counted");
    CHECK_EQ(r.SampleOff, 0, "... the sampling goes on");
    CHECK_EQ(XhciTolRetrySweepMissed(&r, 1, 2), 1, "all ones: the note");
    CHECK_EQ(r.SampleOff, 1, "... the sampling stopped");
    CHECK_EQ(r.SampleOffPort, 2, "... at port 2");
    CHECK_EQ(XhciTolRetrySweepMissed(&r, 1, 2), 0, "... noted once");
    CHECK_EQ(r.MissedPasses, 3, "... every one counted");
}

/* Record 38 section 15.1: the three words per state. */
static void test_status_words(void)
{
    XHCI_TOL_RETRY r;
    ULONG v;
    ULONG t;
    ULONG n;
    ULONG nv;

    XhciTolRetryInit(&r, 1, 0, 1);
    XhciTolRetryStatusWords(&r, 0, TS(0), &v, &t, &n, &nv);
    CHECK_EQ(v, 0, "OFF: value 0");
    CHECK_EQ(n, XHCI_RETRY_NEXT_NONE, "OFF: none");
    XhciTolRetryInit(&r, 1, 7, 1);
    XhciTolRetryStatusWords(&r, 0, TS(0), &v, &t, &n, &nv);
    CHECK_EQ(v, 7, "WATCHING: the value");
    CHECK_EQ(t, 0, "... no try yet");
    CHECK_EQ(n, XHCI_RETRY_NEXT_NONE, "WATCHING: none");
    latched(&r, 5, XHCI_TOL_TERMINAL_WINDOW, 1000);
    XhciTolRetryStatusWords(&r, 1001, TS(1001), &v, &t, &n, &nv);
    CHECK_EQ(n, 5, "LATCHED: 50 ticks rounded up to 5 s");
    CHECK_EQ(nv, 0, "LATCHED: not never");
    XhciTolRetryStatusWords(&r, 1042, TS(1042), &v, &t, &n, &nv);
    CHECK_EQ(n, 1, "LATCHED: 9 ticks, 1 s");
    XhciTolRetryStatusWords(&r, 0xFFFFFFFFUL, 0xFFFFFFFFUL, &v, &t, &n, &nv);
    CHECK_EQ(n, 0, "LATCHED past the deadline: 0");
    (VOID)XhciTolRetryDueStep(&r, 0, 1113, TS(1113));
    XhciTolRetryStatusWords(&r, 1113, TS(1113), &v, &t, &n, &nv);
    CHECK_EQ(n, 0, "DUE: 0");
    (VOID)XhciTolRetryArm(&r);
    (VOID)XhciTolRetryCommit(&r, 1114);
    XhciTolRetryStatusWords(&r, 1114, TS(1114), &v, &t, &n, &nv);
    CHECK_EQ(t, 1, "TRYING: try 1 made");
    CHECK_EQ(n, XHCI_RETRY_NEXT_NONE, "TRYING: none");
    r.Tries = 70000UL;
    XhciTolRetryStatusWords(&r, 1114, TS(1114), &v, &t, &n, &nv);
    CHECK_EQ(t, XHCI_TOL_RETRY_TRIES_MAX, "TRY held at 65534");
    XhciTolRetryInit(&r, 1, 5, 1);
    (VOID)XhciTolRetryObserve(&r, XHCI_TOL_TERMINAL_UNREADABLE, 0, 5, TS(5));
    XhciTolRetryStatusWords(&r, 5, TS(5), &v, &t, &n, &nv);
    CHECK_EQ(r.State, XHCI_RETRY_EXCLUDED, "EXCLUDED");
    CHECK_EQ(n, XHCI_RETRY_NEXT_NONE, "EXCLUDED: none");
    CHECK_EQ(nv, 1, "EXCLUDED: XHR_F_RETRY_NEVER");
}

static void test_burst_success_and_clean_run(void)
{
    XHCI_TOL_RETRY r;
    ULONG t;
    ULONG i;

    latched(&r, 5, XHCI_TOL_TERMINAL_WINDOW, 0);
    (VOID)XhciTolRetryDueStep(&r, 0, 113, TS(113));
    (VOID)XhciTolRetryArm(&r);
    (VOID)XhciTolRetryCommit(&r, 113);
    XhciTolRetryBurstEnd(&r, XHCI_RETRY_BURST_RECOVERED);
    CHECK_EQ(r.State, XHCI_RETRY_WATCHING, "a success: WATCHING");
    CHECK_EQ(r.TriesRecovered, 1, "counted recovered");
    CHECK_EQ(r.LastBurstAttempts, 1, "one attempt");
    CHECK_EQ(r.LastBurstOutcome, XHCI_RETRY_BURST_RECOVERED, "outcome 1");
    CHECK_EQ(r.CleanPending, 1, "a clean run owed");
    XhciTolRetryBurstEnd(&r, XHCI_RETRY_BURST_LATCHED);
    CHECK_EQ(r.LastBurstOutcome, XHCI_RETRY_BURST_RECOVERED,
             "no second end of one burst");
    CHECK_EQ(XhciTolRetryBurstDefers(&r), 0,
             "ordinary recoveries after a success are not deferred");

    /* An unhealthy pass, then the run timed from the first healthy one. */
    CHECK_EQ(XhciTolRetryCleanPass(&r, 0, 7, 200), 0, "unhealthy");
    CHECK_EQ(r.CleanArmed, 0, "not armed while unhealthy");
    CHECK_EQ(XhciTolRetryCleanPass(&r, 1, 7, 300), 0, "first healthy");
    CHECK_EQ(r.CleanArmed, 1, "armed");
    CHECK_EQ(r.CleanStart, 300, "from that pass");
    t = 300 + r.CleanTicks;
    CHECK_EQ(XhciTolRetryCleanPass(&r, 1, 7, t - 1), 0, "one tick short");
    /* A reset request restarts it. */
    CHECK_EQ(XhciTolRetryCleanPass(&r, 1, 8, t), 0, "a reset request");
    CHECK_EQ(r.CleanArmed, 0, "restarted");
    CHECK_EQ(XhciTolRetryCleanPass(&r, 1, 8, t + 1), 0, "re-armed");
    /* A suspend and a resume with no pass between them break it too. */
    XhciTolRetryCleanBreak(&r);
    CHECK_EQ(r.CleanArmed, 0, "a sleep breaks it");
    CHECK_EQ(r.CleanPending, 1, "still owed after a sleep");
    CHECK_EQ(XhciTolRetryCleanPass(&r, 1, 8, t + 50), 0, "armed after D0");
    CHECK_EQ(XhciTolRetryCleanPass(&r, 1, 8, t + 50 + r.CleanTicks), 1,
             "ten minutes healthy: the backing off ends");
    CHECK_EQ(r.Level, 0, "level 0");
    CHECK_EQ(r.Tries, 0, "tries 0");
    CHECK_EQ(r.BackoffEnds, 1, "counted");
    CHECK_EQ(r.CleanPending, 0, "no run owed");
    CHECK_EQ(XhciTolRetryCleanPass(&r, 1, 8, t + 99999), 0, "nothing more");

    /* No clean run without a recovered try. */
    latched(&r, 5, XHCI_TOL_TERMINAL_WINDOW, 0);
    for (i = 0; i < 3; i++) {
        CHECK_EQ(XhciTolRetryCleanPass(&r, 1, 0, 100000 * i), 0,
                 "no run owed");
    }
    /* A relatch drops an owed run. */
    (VOID)XhciTolRetryDueStep(&r, 0, 200, TS(200));
    (VOID)XhciTolRetryCommit(&r, 200);
    XhciTolRetryBurstEnd(&r, XHCI_RETRY_BURST_RECOVERED);
    (VOID)XhciTolRetryObserve(&r, XHCI_TOL_TERMINAL_WINDOW, 0, 300, TS(300));
    CHECK_EQ(r.CleanPending, 0, "a relatch drops the owed run");
    CHECK_EQ(r.LatchWait, 101, "relatch at the raised level");
}

static void test_skip_unarm(void)
{
    XHCI_TOL_RETRY r;

    latched(&r, 5, XHCI_TOL_TERMINAL_FAILURES, 0);
    (VOID)XhciTolRetryDueStep(&r, 0, 113, TS(113));
    (VOID)XhciTolRetryArm(&r);
    XhciTolRetrySkip(&r, XHCI_RETRY_SKIP_ONES, 120, TS(120));
    CHECK_EQ(r.State, XHCI_RETRY_LATCHED, "a skip: LATCHED");
    CHECK_EQ(r.Level, 1, "the level raised");
    CHECK_EQ(r.LatchStamp, 120, "a new wait from now");
    CHECK_EQ(r.LatchWait, 101, "the doubled wait");
    CHECK_EQ(r.SkipOnes, 1, "counted all ones");
    CHECK_EQ(r.Tries, 0, "a skip is not a try");
    CHECK_EQ(r.Burst, 0, "no burst");
    (VOID)XhciTolRetryDueStep(&r, 0, 346, TS(346));
    (VOID)XhciTolRetryArm(&r);
    XhciTolRetrySkip(&r, XHCI_RETRY_SKIP_LAYOUT, 400, TS(400));
    CHECK_EQ(r.SkipLayout, 1, "counted the layout");
    CHECK_EQ(r.Level, 2, "raised again");

    (VOID)XhciTolRetryDueStep(&r, 0, 400 + 452, TS(400 + 452));
    (VOID)XhciTolRetryArm(&r);
    XhciTolRetryUnarm(&r, 1);
    CHECK_EQ(r.State, XHCI_RETRY_DUE, "a refusal leaves DUE");
    CHECK_EQ(r.Armed, 0, "disarmed");
    CHECK_EQ(r.WaitLeave, 1, "counted a leave's wait");
    CHECK_EQ(r.Level, 2, "the level unchanged");
    CHECK_EQ(XhciTolRetryArm(&r), 1, "re-armed later");
    XhciTolRetryUnarm(&r, 0);
    CHECK_EQ(r.WaitPower, 1, "counted a power wait");
}

static void test_exclusion(void)
{
    XHCI_TOL_RETRY r;
    ULONG t;

    for (t = XHCI_TOL_TERMINAL_UNREADABLE; t <= XHCI_TOL_TERMINAL_MSG4;
         t++) {
        latched(&r, 5, XHCI_TOL_TERMINAL_WINDOW, 0);
        CHECK_EQ(XhciTolRetryObserve(&r, t, 0, 10, TS(10)), XHCI_RETRY_EV_EXCLUDED,
                 "terminals 4 to 6 exclude");
        CHECK_EQ(r.State, XHCI_RETRY_EXCLUDED, "EXCLUDED");
        CHECK_EQ(XhciTolRetryObserve(&r, XHCI_TOL_TERMINAL_WINDOW, 0, 20, TS(20)),
                 XHCI_RETRY_EV_NONE, "never again in the lifetime");
        CHECK_EQ(XhciTolRetryNextSeconds(&r, 20, TS(20)), XHCI_RETRY_NEXT_NONE,
                 "NEXT_S none excluded");
    }
    for (t = XHCI_TOL_TERMINAL_NONE; t <= XHCI_TOL_TERMINAL_MSG4; t++) {
        XhciTolRetryInit(&r, 1, 5, 1);
        CHECK_EQ(XhciTolRetryObserve(&r, t, 1, 0, TS(0)), XHCI_RETRY_EV_EXCLUDED,
                 "pinned excludes at every terminal");
    }
    /* A burst ending in a containment. */
    latched(&r, 5, XHCI_TOL_TERMINAL_WINDOW, 0);
    (VOID)XhciTolRetryDueStep(&r, 0, 113, TS(113));
    (VOID)XhciTolRetryCommit(&r, 113);
    CHECK_EQ(XhciTolRetryObserve(&r, XHCI_TOL_TERMINAL_DMA_UNPROVEN, 0, 200, TS(200)),
             XHCI_RETRY_EV_EXCLUDED, "a try ending contained");
    CHECK_EQ(r.LastBurstOutcome, XHCI_RETRY_BURST_EXCLUDED, "outcome 3");
    CHECK_EQ(r.Burst, 0, "the burst ended");

    /* Terminals 0 and 1 while watching latch nothing. */
    XhciTolRetryInit(&r, 1, 5, 1);
    CHECK_EQ(XhciTolRetryObserve(&r, XHCI_TOL_TERMINAL_OWED, 0, 0, TS(0)),
             XHCI_RETRY_EV_NONE, "terminal 1 is not a latch");
    CHECK_EQ(XhciTolRetryObserve(&r, XHCI_TOL_TERMINAL_NONE, 0, 0, TS(0)),
             XHCI_RETRY_EV_NONE, "terminal 0 is not a latch");
    CHECK_EQ(r.State, XHCI_RETRY_WATCHING, "still WATCHING");
}

static void test_port_episodes(void)
{
    XHCI_TOL_RETRY_PORT p;
    ULONG s;

    s = XHCI_TOL_STABLE_DISC_TICKS;
    p.HaveLast = 1;
    p.LastCcs = 1;
    XhciTolRetryPortBreak(&p);
    CHECK(!p.HaveLast && !p.LastCcs, "a break drops the last sample too");
    CHECK_EQ(XhciTolRetryPortSample(&p, 1, 0, s), 0, "the baseline sample");
    CHECK_EQ(p.HaveBase, 1, "baseline taken");
    CHECK_EQ(p.Base, 1, "connected");
    CHECK_EQ(XhciTolRetryPortSample(&p, 0, 10, s), 0, "a change begins");
    CHECK_EQ(XhciTolRetryPortSample(&p, 0, 10 + s - 1, s), 0, "one short");
    CHECK_EQ(XhciTolRetryPortSample(&p, 0, 10 + s, s), 1,
             "stable for the interval: an episode");
    CHECK_EQ(p.Base, 0, "the new baseline");
    CHECK_EQ(XhciTolRetryPortSample(&p, 0, 10 + 2 * s, s), 0,
             "consumed once");
    /* A flap restarts the interval. */
    CHECK_EQ(XhciTolRetryPortSample(&p, 1, 100, s), 0, "connect begins");
    CHECK_EQ(XhciTolRetryPortSample(&p, 0, 100 + s - 2, s), 0, "flap back");
    CHECK_EQ(XhciTolRetryPortSample(&p, 1, 100 + s, s), 0,
             "interval restarted");
    CHECK_EQ(XhciTolRetryPortSample(&p, 1, 100 + 2 * s, s), 1,
             "the connection, stable");
    /* A break drops everything, a matured candidate included. */
    CHECK_EQ(XhciTolRetryPortSample(&p, 0, 500, s), 0, "a change begins");
    XhciTolRetryPortBreak(&p);
    CHECK_EQ(XhciTolRetryPortSample(&p, 0, 500 + s, s), 0,
             "after a break: a baseline, no evidence");
    CHECK_EQ(p.Base, 0, "the baseline is the present state");
    CHECK_EQ(XhciTolRetryPortSample(&p, 0, 500 + 5 * s, s), 0,
             "no episode from the unobserved interval");

    CHECK_EQ(XhciTolRetrySampleGap(0, 0, 9999), 0, "no last: no gap");
    CHECK_EQ(XhciTolRetrySampleGap(1, 100, 100 + XHCI_TOL_RETRY_GAP_TICKS),
             0, "a gap of 13 ticks is not a break");
    CHECK_EQ(XhciTolRetrySampleGap(1, 100,
                                   100 + XHCI_TOL_RETRY_GAP_TICKS + 1),
             1, "more than 13 ticks breaks");
    CHECK_EQ(XHCI_TOL_RETRY_GAP_TICKS, 13, "the gap is 13 ticks");
    CHECK_EQ(XhciTolRetrySampleGap(1, 0xFFFFFFF8UL, 2), 0,
             "across the wrap: 10 ticks");
    CHECK_EQ(XhciTolRetrySampleGap(1, 0xFFFFFFF0UL, 2), 1,
             "across the wrap: 18 ticks");
}

static void test_sweep_hold(void)
{
    CHECK_EQ(XhciTolRetrySweepHold(1, 7, 7, 0x00000201UL),
             XHCI_RETRY_HOLD_READ, "admitted, same generation: read");
    CHECK_EQ(XhciTolRetrySweepHold(0, 7, 7, 0x00000201UL),
             XHCI_RETRY_HOLD_DISCARD, "a condition failed: discard");
    CHECK_EQ(XhciTolRetrySweepHold(1, 7, 8, 0x00000201UL),
             XHCI_RETRY_HOLD_DISCARD, "the leave generation moved");
    CHECK_EQ(XhciTolRetrySweepHold(1, 7, 9, 0x00000201UL),
             XHCI_RETRY_HOLD_DISCARD, "a raise and a clear between holds");
    CHECK_EQ(XhciTolRetrySweepHold(1, 7, 7, 0xFFFFFFFFUL),
             XHCI_RETRY_HOLD_OFF, "all ones: discard and stop");
    CHECK_EQ(XhciTolRetrySweepHold(0, 7, 8, 0xFFFFFFFFUL),
             XHCI_RETRY_HOLD_DISCARD, "admission first");
}

static void test_port_try_and_storm(void)
{
    XHCI_TOL_RETRY r;
    ULONG now;
    ULONG k;

    /* An episode before the value's floor waits for it. */
    latched(&r, 5, XHCI_TOL_TERMINAL_WINDOW, 1000);
    CHECK_EQ(XhciTolRetryDueStep(&r, 1, 1030, TS(1030)), XHCI_RETRY_CAUSE_NONE,
             "not before B after the latch");
    CHECK_EQ(r.PortPending, 1, "kept");
    CHECK_EQ(XhciTolRetryDueStep(&r, 0, 1050, TS(1050)), XHCI_RETRY_CAUSE_NONE,
             "still before B");
    /* B is 51 and the first wait too: at the floor the deadline wins. */
    CHECK_EQ(XhciTolRetryDueStep(&r, 0, 1051, TS(1051)), XHCI_RETRY_CAUSE_WAIT,
             "the deadline wins a tie");
    CHECK_EQ(r.PortPending, 0, "the episode consumed");
    (VOID)XhciTolRetryCommit(&r, 1051);
    CHECK_EQ(r.StormCount, 0, "a timed try is not a port try");
    XhciTolRetryBurstEnd(&r, XHCI_RETRY_BURST_LATCHED);
    (VOID)XhciTolRetryObserve(&r, XHCI_TOL_TERMINAL_FAILURES, 0, 2000, TS(2000));
    /* At level 1 the wait is 101: an episode at B is a port try. */
    CHECK_EQ(XhciTolRetryDueStep(&r, 1, 2030, TS(2030)), XHCI_RETRY_CAUSE_NONE,
             "kept until B");
    CHECK_EQ(XhciTolRetryDueStep(&r, 0, 2051, TS(2051)), XHCI_RETRY_CAUSE_PORT,
             "a port try at B");
    (VOID)XhciTolRetryCommit(&r, 2051);
    CHECK_EQ(r.LastCause, XHCI_RETRY_CAUSE_PORT, "cause 2");
    CHECK_EQ(r.Level, 2, "it is the step's try: the level raised");
    CHECK_EQ(r.StormCount, 1, "one port try stamped");

    /* The storm limit: three in ten minutes, across relatches, even at the
     * saturated level. */
    r.Level = 7;
    now = 2051;
    for (k = 0; k < 2; k++) {
        XhciTolRetryBurstEnd(&r, XHCI_RETRY_BURST_LATCHED);
        now += 200;
        (VOID)XhciTolRetryObserve(&r, XHCI_TOL_TERMINAL_FAILURES, 0, now, TS(now));
        CHECK_EQ(XhciTolRetryDueStep(&r, 1, now + 51, TS(now + 51)),
                 XHCI_RETRY_CAUSE_PORT, "port tries at the saturated level");
        (VOID)XhciTolRetryCommit(&r, now + 51);
    }
    CHECK_EQ(r.StormCount, 3, "three stamped");
    XhciTolRetryBurstEnd(&r, XHCI_RETRY_BURST_LATCHED);
    now += 200;
    (VOID)XhciTolRetryObserve(&r, XHCI_TOL_TERMINAL_FAILURES, 0, now, TS(now));
    CHECK_EQ(XhciTolRetryDueStep(&r, 1, now + 51, TS(now + 51)), XHCI_RETRY_CAUSE_NONE,
             "a fourth inside ten minutes refused");
    CHECK_EQ(r.StormRefused, 1, "counted");
    CHECK_EQ(r.PortPending, 0, "the episode consumed without a try");
    CHECK_EQ(r.State, XHCI_RETRY_LATCHED, "the timed deadline stands");
    CHECK_EQ(XhciTolRetryNoteOnce(&r, XHCI_RETRY_NOTE_STORM), 1,
             "noted once");
    CHECK_EQ(XhciTolRetryNoteOnce(&r, XHCI_RETRY_NOTE_STORM), 0,
             "not twice in the episode");
    /* The oldest ages out: a port try again. */
    now = r.StormStamp[0] + r.StormTicks;
    CHECK_EQ(XhciTolRetryDueStep(&r, 1, now, TS(now)), XHCI_RETRY_CAUSE_PORT,
             "the window aged: allowed");
    (VOID)XhciTolRetryCommit(&r, now);
    CHECK_EQ(r.StormCount, 3, "still three held");
    CHECK_EQ(r.StormStamp[2], now, "the newest last");
    /* A clean run does not clear the storm history. */
    XhciTolRetryBurstEnd(&r, XHCI_RETRY_BURST_RECOVERED);
    (VOID)XhciTolRetryCleanPass(&r, 1, 0, now + 1);
    (VOID)XhciTolRetryCleanPass(&r, 1, 0, now + 1 + r.CleanTicks);
    CHECK_EQ(r.Level, 0, "the backing off ended");
    CHECK_EQ(r.StormCount, 3, "the storm history kept");
    /* A relatch clears the once-per-episode notes. */
    (VOID)XhciTolRetryObserve(&r, XHCI_TOL_TERMINAL_WINDOW, 0, now + 99999, TS(now + 99999));
    CHECK_EQ(r.Noted, 0, "notes cleared at the latch");
    /* Only a start clears it. */
    XhciTolRetryInit(&r, 1, 5, 1);
    CHECK_EQ(r.StormCount, 0, "a start clears the storm history");
}

static void in_ok(PXHCI_TOL_RETRY_IN in)
{
    memset(in, 0, sizeof(*in));
    in->Value = 5;
    in->Terminal = XHCI_TOL_TERMINAL_WINDOW;
    in->D0 = 1;
    in->Started = 1;
    in->PnpStarted = 1;
}

static void test_admit(void)
{
    XHCI_TOL_RETRY_IN in;
    ULONG t;

    in_ok(&in);
    CHECK_EQ(XhciTolRetryAdmit(&in, 0), XHCI_RETRY_ADMIT_OK, "admitted");
    in.Terminal = XHCI_TOL_TERMINAL_FAILURES;
    CHECK_EQ(XhciTolRetryAdmit(&in, 0), XHCI_RETRY_ADMIT_OK, "terminal 2");
    for (t = XHCI_TOL_TERMINAL_NONE; t <= XHCI_TOL_TERMINAL_MSG4; t++) {
        in_ok(&in);
        in.Terminal = t;
        if (t == XHCI_TOL_TERMINAL_FAILURES ||
            t == XHCI_TOL_TERMINAL_WINDOW) {
            CHECK_EQ(XhciTolRetryAdmit(&in, 0), XHCI_RETRY_ADMIT_OK,
                     "terminals 2 and 3");
        } else if (t >= XHCI_TOL_TERMINAL_UNREADABLE) {
            CHECK_EQ(XhciTolRetryAdmit(&in, 0), XHCI_RETRY_ADMIT_EXCLUDE,
                     "terminals 4 to 6");
        } else {
            CHECK_EQ(XhciTolRetryAdmit(&in, 0), XHCI_RETRY_ADMIT_NOT_LATCHED,
                     "terminals 0 and 1");
        }
        in.Pinned = 1;
        CHECK_EQ(XhciTolRetryAdmit(&in, 0), XHCI_RETRY_ADMIT_EXCLUDE,
                 "pinned at every terminal");
    }
    in_ok(&in);
    in.Value = 0;
    CHECK_EQ(XhciTolRetryAdmit(&in, 0), XHCI_RETRY_ADMIT_OFF, "value 0");
    in_ok(&in); in.Leave = 1;
    CHECK_EQ(XhciTolRetryAdmit(&in, 0), XHCI_RETRY_ADMIT_WAIT_LEAVE, "leave");
    in_ok(&in); in.Started = 0;
    CHECK_EQ(XhciTolRetryAdmit(&in, 0), XHCI_RETRY_ADMIT_WAIT_LEAVE,
             "not started");
    in_ok(&in); in.PnpStarted = 0;
    CHECK_EQ(XhciTolRetryAdmit(&in, 0), XHCI_RETRY_ADMIT_WAIT_LEAVE,
             "PnP not started");
    in_ok(&in); in.ThreadStop = 1;
    CHECK_EQ(XhciTolRetryAdmit(&in, 0), XHCI_RETRY_ADMIT_WAIT_LEAVE,
             "thread stopping");
    in_ok(&in); in.CmdWaitLeave = 1;
    CHECK_EQ(XhciTolRetryAdmit(&in, 0), XHCI_RETRY_ADMIT_WAIT_LEAVE,
             "the command wait's leave signal");
    in_ok(&in); in.ShutLatched = 1;
    CHECK_EQ(XhciTolRetryAdmit(&in, 0), XHCI_RETRY_ADMIT_WAIT_LEAVE,
             "shutting down");
    in_ok(&in); in.D0 = 0;
    CHECK_EQ(XhciTolRetryAdmit(&in, 0), XHCI_RETRY_ADMIT_WAIT_POWER, "not D0");
    CHECK_EQ(XhciTolRetryAdmit(&in, 1), XHCI_RETRY_ADMIT_OK,
             "the resume settles power itself");
    in_ok(&in); in.SuspendedInD0 = 1;
    CHECK_EQ(XhciTolRetryAdmit(&in, 0), XHCI_RETRY_ADMIT_WAIT_POWER,
             "suspended in D0");
    in_ok(&in); in.ExtSuspended = 1;
    CHECK_EQ(XhciTolRetryAdmit(&in, 0), XHCI_RETRY_ADMIT_WAIT_POWER,
             "suspended flag");
    in_ok(&in); in.Msg4Barred = 1;
    CHECK_EQ(XhciTolRetryAdmit(&in, 0), XHCI_RETRY_ADMIT_WAIT_POWER,
             "mode 4 barred");
    CHECK_EQ(XhciTolRetryAdmit(&in, 1), XHCI_RETRY_ADMIT_OK,
             "the resume re-arms mode 4 itself");
    in_ok(&in); in.SlotsInvalidated = 1;
    CHECK_EQ(XhciTolRetryAdmit(&in, 0), XHCI_RETRY_ADMIT_WAIT_RELEASE,
             "an invalidation pending");
    in_ok(&in); in.SlotsUnproven = 1;
    CHECK_EQ(XhciTolRetryAdmit(&in, 0), XHCI_RETRY_ADMIT_WAIT_RELEASE,
             "unproven and unproved");
    in.ReleaseProved = 1;
    CHECK_EQ(XhciTolRetryAdmit(&in, 0), XHCI_RETRY_ADMIT_OK,
             "unproven, then proved");
    in_ok(&in); in.DevicesLeft = 1;
    CHECK_EQ(XhciTolRetryAdmit(&in, 0), XHCI_RETRY_ADMIT_WAIT_RELEASE,
             "a device record left");
    in_ok(&in); in.Leave = 1; in.D0 = 0; in.SlotsInvalidated = 1;
    CHECK_EQ(XhciTolRetryAdmit(&in, 0), XHCI_RETRY_ADMIT_WAIT_LEAVE,
             "a leave ranks first among the waits");

    CHECK_EQ(XhciTolRetryReleaseBits(1, 1, 1, 1, 1, 1), 0x3F, "all bits");
    CHECK_EQ(XhciTolRetryReleaseBits(0, 1, 1, 1, 0, 0),
             XHCI_RETRY_REL_UNPROVEN | XHCI_RETRY_REL_PROVED |
                 XHCI_RETRY_REL_TERMINAL, "leg (p)'s state");
    CHECK_EQ(XhciTolRetryReleaseBits(0, 0, 0, 0, 0, 0), 0, "none");
}

static void test_leave(void)
{
    XHCI_TOL_RETRY r;
    ULONG c;

    for (c = 0; c < 2; c++) {
        CHECK_EQ(XhciTolRetryLeaveNext(XHCI_RETRY_PNP_QUERY_STOP,
                                       XHCI_RETRY_PHASE_ENTRY, 0, c), 1,
                 "query-stop entry sets");
        CHECK_EQ(XhciTolRetryLeaveNext(XHCI_RETRY_PNP_QUERY_REMOVE,
                                       XHCI_RETRY_PHASE_ENTRY, 0, c), 1,
                 "query-remove entry sets");
        CHECK_EQ(XhciTolRetryLeaveNext(XHCI_RETRY_PNP_STOP,
                                       XHCI_RETRY_PHASE_ENTRY, 0, c), 1,
                 "stop sets");
        CHECK_EQ(XhciTolRetryLeaveNext(XHCI_RETRY_PNP_STOP,
                                       XHCI_RETRY_PHASE_OUTCOME, 0, c), 1,
                 "stop keeps");
        CHECK_EQ(XhciTolRetryLeaveNext(XHCI_RETRY_PNP_SURPRISE,
                                       XHCI_RETRY_PHASE_ENTRY, 0, c), 1,
                 "surprise removal sets");
        CHECK_EQ(XhciTolRetryLeaveNext(XHCI_RETRY_PNP_REMOVE,
                                       XHCI_RETRY_PHASE_ENTRY, 0, c), 1,
                 "remove sets");
        CHECK_EQ(XhciTolRetryLeaveNext(XHCI_RETRY_PNP_START_LIVE,
                                       XHCI_RETRY_PHASE_ENTRY, 0, c), 1,
                 "a START to a started controller sets");
        CHECK_EQ(XhciTolRetryLeaveNext(XHCI_RETRY_PNP_START_LIVE,
                                       XHCI_RETRY_PHASE_OUTCOME, 0, c), 1,
                 "and keeps");
        CHECK_EQ(XhciTolRetryLeaveNext(XHCI_RETRY_PNP_NEW_LIFETIME,
                                       XHCI_RETRY_PHASE_ENTRY, 0, c), 0,
                 "a new lifetime clears");
        CHECK_EQ(XhciTolRetryLeaveNext(XHCI_RETRY_PNP_CANCEL_STOP,
                                       XHCI_RETRY_PHASE_OUTCOME, 0, c), 0,
                 "cancel-stop clears");
        CHECK_EQ(XhciTolRetryLeaveNext(XHCI_RETRY_PNP_CANCEL_REMOVE,
                                       XHCI_RETRY_PHASE_OUTCOME, 0, c), 0,
                 "cancel-remove clears");
        CHECK_EQ(XhciTolRetryLeaveNext(XHCI_RETRY_PNP_CANCEL_STOP,
                                       XHCI_RETRY_PHASE_ENTRY, 0, c), c,
                 "cancel entry keeps");
        CHECK_EQ(XhciTolRetryLeaveNext(XHCI_RETRY_PNP_QUERY_REMOVE,
                                       XHCI_RETRY_PHASE_OUTCOME,
                                       XHCI_RETRY_OUT_REFUSED_BELOW, c), c,
                 "query-remove refused below: the cancel clears");
        CHECK_EQ(XhciTolRetryLeaveNext(99, XHCI_RETRY_PHASE_ENTRY, 0, c), c,
                 "an unknown path changes nothing");
    }
    CHECK_EQ(XhciTolRetryLeaveNext(XHCI_RETRY_PNP_QUERY_STOP,
                                   XHCI_RETRY_PHASE_OUTCOME,
                                   XHCI_RETRY_OUT_OK, 1), 1,
             "query-stop succeeded: kept");
    CHECK_EQ(XhciTolRetryLeaveNext(XHCI_RETRY_PNP_QUERY_STOP,
                                   XHCI_RETRY_PHASE_OUTCOME,
                                   XHCI_RETRY_OUT_REFUSED_LOCAL, 1), 0,
             "query-stop refused locally under mode 4: cleared");
    CHECK_EQ(XhciTolRetryLeaveNext(XHCI_RETRY_PNP_QUERY_STOP,
                                   XHCI_RETRY_PHASE_OUTCOME,
                                   XHCI_RETRY_OUT_REFUSED_BELOW, 1), 0,
             "query-stop refused below: cleared");

    XhciTolRetryInit(&r, 1, 5, 1);
    XhciTolRetryLeaveWrite(&r, XHCI_RETRY_PNP_QUERY_REMOVE,
                           XHCI_RETRY_PHASE_ENTRY, 0);
    CHECK_EQ(r.Leave, 1, "written");
    CHECK_EQ(r.LeaveGen, 1, "generation 1");
    CHECK_EQ(XhciTolRetryBurstDefers(&r), 0, "no burst, nothing deferred");
    r.Burst = 1;
    CHECK_EQ(XhciTolRetryBurstDefers(&r), 1, "the burst yields to a leave");
    XhciTolRetryLeaveWrite(&r, XHCI_RETRY_PNP_CANCEL_REMOVE,
                           XHCI_RETRY_PHASE_OUTCOME, 0);
    CHECK_EQ(r.Leave, 0, "cleared");
    CHECK_EQ(r.LeaveGen, 2, "every write advances it, a clear included");
    CHECK_EQ(XhciTolRetryBurstDefers(&r), 0, "the burst goes on");
    XhciTolRetryLeaveWrite(&r, XHCI_RETRY_PNP_CANCEL_REMOVE,
                           XHCI_RETRY_PHASE_ENTRY, 0);
    CHECK_EQ(r.LeaveGen, 3, "an unchanging write advances it too");
}

static void test_resume_end(void)
{
    ULONG adv;

    CHECK_EQ(XhciTolRetryResumeEnd(XHCI_RETRY_RESUME_FAILED, 1, 1, &adv),
             XHCI_RETRY_RESUME_RUNNING, "marked FAILED and a success");
    CHECK_EQ(adv, 1, "MarkGen advances");
    CHECK_EQ(XhciTolRetryResumeEnd(XHCI_RETRY_RESUME_FAILED, 0, 1, &adv),
             XHCI_RETRY_RESUME_FAILED, "an ordinary failed resume: left");
    CHECK_EQ(adv, 0, "no advance");
    CHECK_EQ(XhciTolRetryResumeEnd(XHCI_RETRY_RESUME_FAILED, 1, 0, &adv),
             XHCI_RETRY_RESUME_FAILED, "no success: left FAILED");
    CHECK_EQ(adv, 0, "no advance");
    CHECK_EQ(XhciTolRetryResumeEnd(XHCI_RETRY_RESUME_PENDING, 1, 1, &adv),
             XHCI_RETRY_RESUME_PENDING, "a suspend's PENDING: its own rules");
    CHECK_EQ(adv, 0, "no advance");
    CHECK_EQ(XhciTolRetryResumeEnd(XHCI_RETRY_RESUME_RUNNING, 1, 1, &adv),
             XHCI_RETRY_RESUME_RUNNING, "RUNNING unchanged");
    CHECK_EQ(adv, 0, "no advance");
}

static void test_tries_saturate(void)
{
    XHCI_TOL_RETRY r;

    latched(&r, 5, XHCI_TOL_TERMINAL_WINDOW, 0);
    r.Tries = XHCI_TOL_RETRY_TRIES_MAX - 1;
    r.TriesTotal = 0xFFFFFFFEUL;
    (VOID)XhciTolRetryDueStep(&r, 0, 113, TS(113));
    CHECK_EQ(XhciTolRetryCommit(&r, 113), XHCI_TOL_RETRY_TRIES_MAX,
             "up to 65534");
    CHECK_EQ(r.TriesTotal, 0xFFFFFFFFUL, "the total to all ones");
    XhciTolRetryBurstEnd(&r, XHCI_RETRY_BURST_LATCHED);
    (VOID)XhciTolRetryObserve(&r, XHCI_TOL_TERMINAL_FAILURES, 0, 1000, TS(1000));
    (VOID)XhciTolRetryDueStep(&r, 0, 1000 + r.LatchWait, TS(1000 + r.LatchWait));
    CHECK_EQ(XhciTolRetryCommit(&r, 1000 + r.LatchWait),
             XHCI_TOL_RETRY_TRIES_MAX, "held at 65534");
    CHECK_EQ(r.TriesTotal, 0xFFFFFFFFUL, "held, never wrapped");
    CHECK(r.Tries + 1UL <= 0xFFFFUL, "try K fits 16 bits");
}

static void test_wrap(void)
{
    XHCI_TOL_RETRY r;
    ULONG start;

    start = 0xFFFFFFF0UL;
    latched(&r, 5, XHCI_TOL_TERMINAL_WINDOW, start);
    CHECK_EQ(XhciTolRetryDueStep(&r, 0, start + 50, TS(start + 50)), XHCI_RETRY_CAUSE_NONE,
             "across the wrap: short");
    CHECK_EQ(XhciTolRetryDueStep(&r, 0, start + 51, TS(start + 51)), XHCI_RETRY_CAUSE_WAIT,
             "across the wrap: due");
    CHECK_EQ(XhciTolRetryNextSeconds(&r, start + 51, TS(start + 51)), 0, "due");
}

static void test_shape(void)
{
    /* A new tool reading an old snapshot: the header ends before the
     * block's two words. */
    CHECK_EQ(XhciTolRetryBlockShape(954, 956, 0, 0, 208, 4096),
             XHCI_RETRY_SHAPE_NOT_SERVED, "an earlier driver's header");
    CHECK_EQ(XhciTolRetryBlockShape(955, 956, 0, 0, 208, 4096),
             XHCI_RETRY_SHAPE_NOT_SERVED, "a truncated header");
    CHECK_EQ(XhciTolRetryBlockShape(956, 956, 1024, 208, 208, 4096),
             XHCI_RETRY_SHAPE_OK, "a sound descriptor");
    CHECK_EQ(XhciTolRetryBlockShape(960, 956, 1024, 256, 208, 4096),
             XHCI_RETRY_SHAPE_OK, "a longer header and block (a later "
             "driver read by this tool)");
    CHECK_EQ(XhciTolRetryBlockShape(956, 956, 4097, 208, 208, 4096),
             XHCI_RETRY_SHAPE_BAD, "an offset past the image");
    CHECK_EQ(XhciTolRetryBlockShape(956, 956, 4000, 208, 208, 4096),
             XHCI_RETRY_SHAPE_BAD, "running past the image");
    CHECK_EQ(XhciTolRetryBlockShape(956, 956, 1024, 100, 208, 4096),
             XHCI_RETRY_SHAPE_BAD, "a size below the known fields");
    CHECK_EQ(XhciTolRetryBlockShape(956, 956, 1026, 208, 208, 4096),
             XHCI_RETRY_SHAPE_BAD, "an unaligned offset");
    CHECK_EQ(XhciTolRetryBlockShape(956, 956, 0xFFFFFFFCUL, 208, 208, 4096),
             XHCI_RETRY_SHAPE_BAD, "an offset near the top: no wrap");
    CHECK_EQ(sizeof(XHCI_TOL_RETRY), 208, "the block is 52 ULONGs");
}

/* Record 38 section 5.7: the waits in seconds - the ticks at the nominal
 * 100 ms, the doubling in milliseconds, and the system time's floor. */
static void test_floor(void)
{
    XHCI_TOL_RETRY r;

    CHECK_EQ(XHCI_TOL_RETRY_TICKS(5000UL), 51, "5 s: 51 ticks");
    CHECK_EQ(XHCI_TOL_RETRY_TICKS(600000UL), 6001, "ten minutes: 6001");
    CHECK_EQ(XHCI_TOL_RETRY_UNITS(5000UL), 49, "5 s: 49 units");
    CHECK_EQ(XHCI_TOL_RETRY_UNITS(600000UL), 5724, "ten minutes: 5724");
    CHECK_EQ(XhciTolRetryFloor(51), 49, "the floor of B");
    CHECK_EQ(XhciTolRetryFloor(6001), 5724, "the floor of C");
    CHECK_EQ(XhciTolRetryFloor(1), 1, "a scaled tick: one unit");
    CHECK_EQ(XhciTolRetryFloor(0), 1, "zero: one unit");
    CHECK_EQ(XhciTolRetryFloor(0xFFFFFFFFUL), 5724, "held to the ceiling");
    CHECK_EQ(XhciTolRetrySysElapsed(5049, 5000, 49), 1, "49 units: met");
    CHECK_EQ(XhciTolRetrySysElapsed(5048, 5000, 49), 0, "48: not met");
    CHECK_EQ(XhciTolRetrySysElapsed(10, 0xFFFFFFF0UL, 26), 1,
             "across the wrap: met");
    CHECK_EQ(XhciTolRetrySysElapsed(4000, 5000, 49), 1,
             "set back past the stamp: a very long interval");

    /* V = 2 at level 8: 512 s doubled in milliseconds, not 537.5. */
    XhciTolRetryInit(&r, 1, 2, 1);
    CHECK_EQ(XhciTolRetryWait(&r, 8), 5121, "2 s at level 8: 512 s");
    CHECK_EQ(XhciTolRetryWait(&r, 9), 6001, "2 s at level 9: the cap");

    /* Both clocks: the ticks met and the floor not, no try. */
    XhciTolRetryInit(&r, 1, 5, 1);
    CHECK_EQ(XhciTolRetryObserve(&r, XHCI_TOL_TERMINAL_WINDOW, 0, 1000, 5000),
             XHCI_RETRY_EV_LATCHED, "latched at tick 1000, unit 5000");
    CHECK_EQ(r.LatchSys, 5000, "the system time stamped");
    CHECK_EQ(XhciTolRetryNextSeconds(&r, 1051, 5000), 6,
             "the ticks spent, the floor's 49 units: 6 s");
    CHECK_EQ(XhciTolRetryDueStep(&r, 0, 1051, 5048), XHCI_RETRY_CAUSE_NONE,
             "ticks met, floor short: not due");
    CHECK_EQ(XhciTolRetryDueStep(&r, 0, 1050, 9000), XHCI_RETRY_CAUSE_NONE,
             "floor met, ticks short: not due");
    CHECK_EQ(XhciTolRetryDueStep(&r, 0, 1051, 5049), XHCI_RETRY_CAUSE_WAIT,
             "both met: due");

    /* A clock set back by less than the time since the latch delays it. */
    XhciTolRetryInit(&r, 1, 5, 1);
    (VOID)XhciTolRetryObserve(&r, XHCI_TOL_TERMINAL_WINDOW, 0, 1000, 5000);
    CHECK_EQ(XhciTolRetryDueStep(&r, 0, 1051, 5010), XHCI_RETRY_CAUSE_NONE,
             "set back by less: later");
    /* Set back by more: a very long interval, the ticks decide. */
    CHECK_EQ(XhciTolRetryDueStep(&r, 0, 1051, 4000), XHCI_RETRY_CAUSE_WAIT,
             "set back by more: the ticks decide");

    /* A port change waits for the floor of B too. */
    XhciTolRetryInit(&r, 1, 5, 1);
    r.Level = 1;
    (VOID)XhciTolRetryObserve(&r, XHCI_TOL_TERMINAL_WINDOW, 0, 1000, 5000);
    CHECK_EQ(r.LatchWait, 101, "level 1: 101 ticks");
    CHECK_EQ(XhciTolRetryDueStep(&r, 1, 1051, 5020), XHCI_RETRY_CAUSE_NONE,
             "a port change at B's ticks, the floor short: kept");
    CHECK_EQ(r.PortPending, 1, "the episode kept");
    CHECK_EQ(XhciTolRetryDueStep(&r, 0, 1052, 5049), XHCI_RETRY_CAUSE_PORT,
             "the floor of B met: a port try");

    /* A skip stamps the system time too. */
    XhciTolRetrySkip(&r, XHCI_RETRY_SKIP_ONES, 1200, 7777);
    CHECK_EQ(r.LatchSys, 7777, "a skip's new wait stamped");
}

int main(void)
{
    test_value();
    test_init();
    test_waits();
    test_latch_and_timed_try();
    test_burst_success_and_clean_run();
    test_skip_unarm();
    test_commit();
    test_healthy();
    test_exclusion();
    test_port_episodes();
    test_sweep_hold();
    test_sweep_end();
    test_port_try_and_storm();
    test_admit();
    test_leave();
    test_resume_end();
    test_tries_saturate();
    test_wrap();
    test_shape();
    test_status_words();
    test_floor();

    printf("\ntest_retry: %d checks, %d failures\n", checks, failures);
    return failures;
}
