/*
 * test_cm_live.c - the host-interface liveness monitor's pure half
 * (src/cm/cm_live.c, design record 21 sections 5.5 and 5.6): sampling by
 * elapsed time, SUSPECT on the first unanswered sample, ABSENT on the
 * third, CONFIG_LOST on a changed register, recovery light and full, its
 * failure and the re-arm rule, transitions, the USB4 profile's
 * containment, revocation and the access gate.
 */

#include <stdio.h>
#include "../src/cm/cm_live.h"
#include "test_harness.h"

#define ONES 0xFFFFFFFFUL
#define BASE 0x0100000AUL

static void arm(CM_LIVE *l, ULONG usb4)
{
    CmLiveInit(l, usb4, 200);
    CHECK(CmLiveArm(l, BASE, 1000), "arm");
    CHECK_EQ(l->State, CM_LIVE_ACTIVE, "armed active");
}

static void test_off_and_cadence(void)
{
    CM_LIVE l;

    CmLiveInit(&l, 0, 0);
    CHECK_EQ(l.State, CM_LIVE_OFF, "init off");
    CHECK_EQ(l.PeriodMs, 200, "default period");
    CHECK(!CmLiveDue(&l, 5000), "off never due");
    CHECK_EQ(CmLiveWaitMs(&l), 0, "off waits for work");
    CHECK_EQ(CmLiveSample(&l, 5000, ONES), 0, "off ignores samples");
    CHECK(CmLiveAdmits(&l, CM_ACCESS_ORDINARY), "off admits ordinary");
    CHECK(!CmLiveAdmits(&l, CM_ACCESS_RECOVERY), "off refuses recovery");

    arm(&l, 0);
    CHECK(!CmLiveDue(&l, 1100), "not due before the period");
    CHECK(CmLiveDue(&l, 1200), "due at the period");
    CHECK_EQ(CmLiveWaitMs(&l), 200, "active cadence");
    CHECK_EQ(CmLiveSample(&l, 1200, BASE), 0, "matching sample: nothing");
    CHECK(!CmLiveDue(&l, 1300), "an early wake adds no sample");
    CHECK_EQ(l.Counters.Samples, 1, "one sample");
    /* the clock's wrap */
    CHECK(CmLiveArm(&l, BASE, 0xFFFFFFF0UL), "rearm near wrap");
    CHECK(CmLiveDue(&l, 0xB8UL), "due across the wrap");
}

static void test_suspect_absent(void)
{
    CM_LIVE l;
    ULONG a;

    arm(&l, 0);
    a = CmLiveSample(&l, 1200, ONES);
    CHECK_EQ(l.State, CM_LIVE_SUSPECT, "first ones: suspect");
    CHECK(a & CM_LIVE_ACT_STOP_TRANSPORT, "suspect stops transport");
    CHECK(!(a & CM_LIVE_ACT_RECONCILE), "suspect does not reconcile yet");
    CHECK(!CmLiveTransport(&l), "no transport in suspect");
    CHECK(CmLiveAdmits(&l, CM_ACCESS_PROBE), "probe in suspect");
    CHECK_EQ(CmLiveEpisode(&l), 1, "episode 1");
    CHECK_EQ(CmLiveWaitMs(&l), 200, "suspect keeps the fast cadence");
    a = CmLiveSample(&l, 1400, ONES);
    CHECK_EQ(l.State, CM_LIVE_SUSPECT, "second ones: still suspect");
    CHECK_EQ(a, 0, "no action on the second");
    a = CmLiveSample(&l, 1600, ONES);
    CHECK_EQ(l.State, CM_LIVE_ABSENT, "third ones: absent");
    CHECK(a & CM_LIVE_ACT_RECONCILE, "absent reconciles");
    CHECK_EQ(CmLiveWaitMs(&l), CM_LIVE_SLOW_MS, "absent: slow probes");
    a = CmLiveSample(&l, 2600, ONES);
    CHECK_EQ(a, 0, "absent stays quiet");
    CHECK_EQ(l.Counters.Absents, 1, "one absent");
    CHECK_EQ(l.Counters.Suspects, 1, "one suspect");
}

static void test_suspect_back_light(void)
{
    CM_LIVE l;
    ULONG a;

    arm(&l, 0);
    (VOID)CmLiveSample(&l, 1200, ONES);
    a = CmLiveSample(&l, 1400, BASE);
    CHECK(a & CM_LIVE_ACT_VALIDATE, "back unchanged: validate");
    CHECK_EQ(l.State, CM_LIVE_SUSPECT, "still suspect until validated");
    CHECK(!CmLiveTransport(&l), "no transport before rediscovery");
    a = CmLiveValidated(&l, TRUE);
    CHECK(a & CM_LIVE_ACT_RECOVER_LIGHT, "light recovery");
    CHECK(!(a & CM_LIVE_ACT_RECOVER_FULL), "not full");
    CHECK_EQ(l.State, CM_LIVE_RECOVERING, "recovering");
    CHECK(CmLiveAdmits(&l, CM_ACCESS_RECOVERY), "recovery access");
    CHECK(!CmLiveAdmits(&l, CM_ACCESS_ORDINARY), "no ordinary access");
    CHECK(!CmLiveDue(&l, 9000), "no samples while recovering");
    CHECK(CmLiveRecovered(&l, 1, TRUE, BASE, 1500), "published");
    CHECK_EQ(l.State, CM_LIVE_ACTIVE, "active again");
    CHECK(CmLiveTransport(&l), "transport again");
}

static void test_config_lost_full(void)
{
    CM_LIVE l;
    ULONG a;

    arm(&l, 0);
    a = CmLiveSample(&l, 1200, 0);
    CHECK_EQ(l.State, CM_LIVE_CONFIG_LOST, "reset register: config lost");
    CHECK(a & CM_LIVE_ACT_STOP_TRANSPORT, "stops transport");
    CHECK(a & CM_LIVE_ACT_RECONCILE, "reconciles at once");
    CHECK(a & CM_LIVE_ACT_VALIDATE, "validates");
    a = CmLiveValidated(&l, TRUE);
    CHECK(a & CM_LIVE_ACT_RECOVER_FULL, "full recovery");
    CHECK(CmLiveRecovered(&l, CmLiveEpisode(&l), TRUE, BASE, 1300), "ok");
    CHECK_EQ(l.Counters.RecoveredOk, 1, "counted");

    /* absent, then the platform restores a reset controller */
    arm(&l, 0);
    (VOID)CmLiveSample(&l, 1200, ONES);
    (VOID)CmLiveSample(&l, 1400, ONES);
    a = CmLiveSample(&l, 1600, ONES);
    CHECK(a & CM_LIVE_ACT_RECONCILE, "absent reconciles");
    a = CmLiveSample(&l, 2600, 0);
    CHECK_EQ(l.State, CM_LIVE_CONFIG_LOST, "restored but reset");
    CHECK(!(a & CM_LIVE_ACT_RECONCILE), "no second reconcile, same episode");
    CHECK(a & CM_LIVE_ACT_VALIDATE, "validate");

    /* a baseline of 0 detects only all ones */
    CmLiveInit(&l, 0, 200);
    CHECK(CmLiveArm(&l, 0, 1000), "arm without baseline");
    CHECK_EQ(CmLiveSample(&l, 1200, 0x1234), 0, "no mismatch detection");
    CmLiveInit(&l, 0, 200);
    CHECK(!CmLiveArm(&l, ONES, 1000), "an all-ones baseline is refused");
    CHECK_EQ(l.State, CM_LIVE_OFF, "still off");

    /* a final sample that is not the programmed one fails the recovery */
    arm(&l, 0);
    (VOID)CmLiveSample(&l, 1200, 0);
    (VOID)CmLiveValidated(&l, TRUE);
    CHECK(!CmLiveRecovered(&l, CmLiveEpisode(&l), TRUE, 0, 1300),
          "a reset register after recovery is a failure");
    CHECK_EQ(l.State, CM_LIVE_RECOVERY_FAILED, "failed");
    CHECK_EQ(CmLiveReconcileOwed(&l), 0, "already reconciled in this episode");
    arm(&l, 0);
    (VOID)CmLiveSample(&l, 1200, ONES);
    (VOID)CmLiveSample(&l, 1400, BASE);
    (VOID)CmLiveValidated(&l, TRUE);
    CHECK(!CmLiveRecovered(&l, CmLiveEpisode(&l), TRUE, ONES, 1500),
          "an unanswered final sample is a failure");
    CHECK_EQ(CmLiveReconcileOwed(&l), CM_LIVE_ACT_RECONCILE,
             "the failed light recovery retires the old generations");
    CHECK_EQ(CmLiveReconcileOwed(&l), 0, "once");
}

static void test_failure_and_rearm(void)
{
    CM_LIVE l;
    ULONG a;

    arm(&l, 0);
    (VOID)CmLiveSample(&l, 1200, ONES);
    a = CmLiveSample(&l, 1400, BASE);
    a = CmLiveValidated(&l, FALSE);
    CHECK(a & CM_LIVE_ACT_RECONCILE, "a refused validation from suspect reconciles");
    arm(&l, 0);
    (VOID)CmLiveSample(&l, 1200, 0);
    a = CmLiveValidated(&l, FALSE);
    CHECK(!(a & CM_LIVE_ACT_RECONCILE), "config lost already reconciled");
    CHECK_EQ(l.State, CM_LIVE_RECOVERY_FAILED, "validation refused");
    CHECK(!CmLiveTransport(&l), "no transport after failure");
    CHECK(!CmLiveAdmits(&l, CM_ACCESS_RECOVERY), "no recovery access");
    CHECK_EQ(CmLiveWaitMs(&l), CM_LIVE_SLOW_MS, "slow monitoring");
    a = CmLiveSample(&l, 2200, 0);
    CHECK_EQ(a, 0, "a persisting mismatch does not re-arm");
    a = CmLiveSample(&l, 3200, BASE);
    CHECK_EQ(a, 0, "nor a sane one without an outage");
    a = CmLiveSample(&l, 4200, ONES);
    CHECK_EQ(a, 0, "the outage is noted");
    a = CmLiveSample(&l, 5200, BASE);
    CHECK(a & CM_LIVE_ACT_VALIDATE, "new episode: validate");
    CHECK(a & CM_LIVE_ACT_RECONCILE, "new episode reconciles");
    CHECK_EQ(CmLiveEpisode(&l), 2, "episode 2");
    a = CmLiveValidated(&l, TRUE);
    CHECK(a & CM_LIVE_ACT_RECOVER_LIGHT, "light");
    CHECK(!CmLiveRecovered(&l, 1, TRUE, BASE, 5300), "stale episode dropped");
    CHECK_EQ(l.Counters.LateCompletions, 1, "late counted");
    CHECK(!CmLiveRecovered(&l, 2, FALSE, 0, 5300), "failed recovery");
    CHECK_EQ(l.State, CM_LIVE_RECOVERY_FAILED, "failed again");
    CHECK_EQ(l.Counters.RecoveryFailed, 2, "two failures");

    /* repeated cycles: active -> absent -> recovered -> absent */
    arm(&l, 0);
    (VOID)CmLiveSample(&l, 1200, ONES);
    (VOID)CmLiveSample(&l, 1400, ONES);
    (VOID)CmLiveSample(&l, 1600, ONES);
    (VOID)CmLiveSample(&l, 2600, BASE);
    (VOID)CmLiveValidated(&l, TRUE);
    CHECK(CmLiveRecovered(&l, CmLiveEpisode(&l), TRUE, BASE, 2700), "cycle 1");
    (VOID)CmLiveSample(&l, 2900, ONES);
    (VOID)CmLiveSample(&l, 3100, ONES);
    a = CmLiveSample(&l, 3300, ONES);
    CHECK(a & CM_LIVE_ACT_RECONCILE, "cycle 2 reconciles again");
    CHECK_EQ(l.State, CM_LIVE_ABSENT, "cycle 2 absent");
}

static void test_transition_and_revoke(void)
{
    CM_LIVE l;
    ULONG a;

    arm(&l, 0);
    (VOID)CmLiveSample(&l, 1200, ONES);
    (VOID)CmLiveSample(&l, 1400, ONES);
    CmLiveTransition(&l, TRUE);
    CHECK(!CmLiveDue(&l, 9000), "no sample in a transition");
    CHECK_EQ(CmLiveSample(&l, 9000, ONES), 0, "a sample in a transition is ignored");
    CmLiveTransition(&l, FALSE);
    CHECK_EQ(l.Ones, 0, "count reset across a transition");
    (VOID)CmLiveSample(&l, 9200, ONES);
    CHECK_EQ(l.State, CM_LIVE_SUSPECT, "not absent after the reset count");

    CmLiveDisarm(&l);
    CHECK_EQ(l.State, CM_LIVE_OFF, "disarm: off");

    arm(&l, 0);
    (VOID)CmLiveSample(&l, 1200, 0);
    (VOID)CmLiveValidated(&l, TRUE);
    CmLiveRevoke(&l);
    CHECK(!CmLiveAdmits(&l, CM_ACCESS_PROBE), "revoked: no probe");
    CHECK(!CmLiveAdmits(&l, CM_ACCESS_RECOVERY), "revoked: no recovery");
    CHECK(!CmLiveAdmits(&l, CM_ACCESS_ORDINARY), "revoked: nothing");
    CHECK(!CmLiveRecovered(&l, CmLiveEpisode(&l), TRUE, BASE, 1300),
          "a late completion cannot publish after revoke");
    CHECK(!CmLiveArm(&l, BASE, 1400), "no re-arm after revoke");
    CmLiveDisarm(&l);
    CHECK(!CmLiveTransport(&l), "revoke survives disarm");
    CHECK_EQ(CmLiveWaitMs(&l), 0, "revoked: no monitoring");
    a = CmLiveValidated(&l, TRUE);
    CHECK_EQ(a, 0, "nothing validated after revoke");
}

static void test_usb4_contain(void)
{
    CM_LIVE l;
    ULONG a;

    arm(&l, 1);
    a = CmLiveSample(&l, 1200, ONES);
    CHECK_EQ(l.State, CM_LIVE_CONTAINED, "usb4: contained at once");
    CHECK(a & CM_LIVE_ACT_CONTAIN, "contain asked");
    CHECK(a & CM_LIVE_ACT_STOP_TRANSPORT, "transport stopped");
    CHECK(!CmLiveTransport(&l), "no transport");
    CHECK_EQ(CmLiveWaitMs(&l), CM_LIVE_SLOW_MS,
             "contained: probed once a second (record 22 7.7 part 1)");
    CHECK_EQ(l.Sub, CM_LIVE_SUB_PROBING, "contained: probing");
    CHECK_EQ(CmLiveSample(&l, 2200, 0x0100000AUL), 0,
             "a matching sample never reactivates it");
    CHECK(!CmLiveArm(&l, BASE, 3000), "an ordinary resume cannot re-arm");
    CmLiveDisarm(&l);
    CHECK_EQ(l.State, CM_LIVE_CONTAINED, "disarm keeps containment");

    arm(&l, 1);
    a = CmLiveSample(&l, 1200, 7);
    CHECK_EQ(l.State, CM_LIVE_CONTAINED, "usb4 mismatch: contained");
    CHECK(a & CM_LIVE_ACT_CONTAIN, "contain on mismatch");
    CHECK_EQ(l.Counters.Contained, 1, "counted");
}

/* Record 22 section 7.7 part 5: every row of the recovery's table. */
static void test_usb4_recover(void)
{
    CM_LIVE l;
    ULONG a;
    ULONG ep;

    /* CONTAINED (probing): all ones or another identity - probing on;
     * the header proven - PREPARING; BAR0 changed - cooldown. */
    arm(&l, 1);
    (VOID)CmLiveSample(&l, 1200, ONES);
    ep = CmLiveEpisode(&l);
    CHECK_EQ(CmLiveProbe(&l, 2200, CM_LIVE_PROBE_ONES), 0, "probe ones: no action");
    CHECK_EQ(l.State, CM_LIVE_CONTAINED, "probe ones: contained");
    CHECK_EQ(CmLiveProbe(&l, 3200, CM_LIVE_PROBE_OTHER), 0, "probe other: no action");
    CHECK_EQ(l.Sub, CM_LIVE_SUB_PROBING, "probe other: still probing");
    CHECK(!CmLiveDue(&l, 3300), "probe: once a second");
    CHECK(CmLiveDue(&l, 4200), "probe: due after a second");
    a = CmLiveProbe(&l, 4200, CM_LIVE_PROBE_OK);
    CHECK(a & CM_LIVE_ACT_PREPARE, "header proven: prepare");
    CHECK_EQ(l.State, CM_LIVE_PREPARING, "PREPARING");
    CHECK(CmLiveAdmits(&l, CM_ACCESS_PREPARE), "PREPARE admitted");
    CHECK(CmLiveAdmits(&l, CM_ACCESS_PROBE), "PROBE admitted");
    CHECK(!CmLiveAdmits(&l, CM_ACCESS_RECOVERY), "PREPARING: no RECOVERY");
    CHECK(!CmLiveTransport(&l), "PREPARING: no transport");
    CHECK_EQ(CmLiveWaitMs(&l), 0, "PREPARING: not sampled");

    /* PREPARING refused (no all ones): cooldown, no rearm. */
    a = CmLivePrepared(&l, FALSE, FALSE);
    CHECK_EQ(l.State, CM_LIVE_CONTAINED, "prepare refused: contained");
    CHECK_EQ(l.Sub, CM_LIVE_SUB_COOLDOWN, "prepare refused: cooldown");
    CHECK_EQ(l.Rearm, 0, "prepare refused: no rearm");
    CHECK_EQ(CmLiveProbe(&l, 5200, CM_LIVE_PROBE_OK), 0,
             "cooldown: a readable probe alone is not an episode");
    CHECK_EQ(CmLiveProbe(&l, 6200, CM_LIVE_PROBE_ONES), 0, "cooldown: ones");
    CHECK_EQ(l.Rearm, 1, "cooldown: rearmed by an outage");
    a = CmLiveProbe(&l, 7200, CM_LIVE_PROBE_OK);
    CHECK(a & CM_LIVE_ACT_SNAPSHOT, "new episode: snapshot asked");
    CHECK_EQ(CmLiveEpisode(&l), ep + 1, "new episode: bumped");
    CHECK_EQ(l.Sub, CM_LIVE_SUB_PROBING, "new episode: probing");

    /* PREPARING refused on an all-ones read: cooldown with Rearm, so the
     * next readable probe is the new episode. */
    a = CmLiveProbe(&l, 8200, CM_LIVE_PROBE_OK);
    CHECK_EQ(l.State, CM_LIVE_PREPARING, "again PREPARING");
    (VOID)CmLivePrepared(&l, FALSE, TRUE);
    CHECK_EQ(l.Rearm, 1, "prepare all ones: rearm");
    a = CmLiveProbe(&l, 9200, CM_LIVE_PROBE_OK);
    CHECK(a & CM_LIVE_ACT_SNAPSHOT, "prepare all ones: next probe a new episode");

    /* PREPARING passed: RECOVERING. A commit for another episode is
     * dropped; a refused one goes to cooldown; the final stop to final. */
    a = CmLiveProbe(&l, 10200, CM_LIVE_PROBE_OK);
    a = CmLivePrepared(&l, TRUE, FALSE);
    CHECK(a & CM_LIVE_ACT_RECOVER_FULL, "prepared: recover");
    CHECK_EQ(l.State, CM_LIVE_RECOVERING, "RECOVERING");
    CHECK(CmLiveAdmits(&l, CM_ACCESS_RECOVERY), "RECOVERY admitted");
    CHECK(!CmLiveTransport(&l), "RECOVERING: no transport");
    ep = CmLiveEpisode(&l);
    CHECK(!CmLiveU4Recovered(&l, ep + 1, TRUE, FALSE, FALSE, BASE, 10300),
          "another episode: dropped");
    CHECK_EQ(l.State, CM_LIVE_RECOVERING, "late result: state unchanged");
    CHECK(!CmLiveU4Recovered(&l, ep, TRUE, FALSE, FALSE, 7, 10300),
          "a sample not the programmed one: no commit");
    CHECK_EQ(l.Sub, CM_LIVE_SUB_COOLDOWN, "sample mismatch: cooldown");
    CHECK_EQ(l.Rearm, 0, "sample mismatch: no rearm");

    (VOID)CmLiveProbe(&l, 11200, CM_LIVE_PROBE_ONES);
    (VOID)CmLiveProbe(&l, 12200, CM_LIVE_PROBE_OK);
    (VOID)CmLiveProbe(&l, 13200, CM_LIVE_PROBE_OK);
    (VOID)CmLivePrepared(&l, TRUE, FALSE);
    ep = CmLiveEpisode(&l);
    CHECK(!CmLiveU4Recovered(&l, ep, FALSE, FALSE, FALSE, ONES, 13300),
          "an all-ones sample: failed");
    CHECK_EQ(l.Rearm, 1, "a second outage seen: rearm");

    (VOID)CmLiveProbe(&l, 14200, CM_LIVE_PROBE_OK);
    (VOID)CmLiveProbe(&l, 15200, CM_LIVE_PROBE_OK);
    (VOID)CmLivePrepared(&l, TRUE, FALSE);
    ep = CmLiveEpisode(&l);
    CHECK(!CmLiveU4Recovered(&l, ep, FALSE, TRUE, FALSE, BASE, 15300),
          "stop unverified: no commit");
    CHECK_EQ(l.Sub, CM_LIVE_SUB_FINAL, "stop unverified: final");
    CHECK_EQ(CmLiveWaitMs(&l), 0, "final: no probes");
    CHECK_EQ(CmLiveProbe(&l, 16200, CM_LIVE_PROBE_ONES), 0, "final: ones nothing");
    CHECK_EQ(CmLiveProbe(&l, 17200, CM_LIVE_PROBE_OK), 0, "final: ok nothing");
    CHECK_EQ(l.Sub, CM_LIVE_SUB_FINAL, "final stays");

    /* The commit: ACTIVE with the fresh sample as the baseline, then a
     * later loss contains it afresh. */
    arm(&l, 1);
    (VOID)CmLiveSample(&l, 1200, 7);
    (VOID)CmLiveProbe(&l, 2200, CM_LIVE_PROBE_OK);
    (VOID)CmLivePrepared(&l, TRUE, FALSE);
    ep = CmLiveEpisode(&l);
    CHECK(CmLiveU4Recovered(&l, ep, TRUE, FALSE, FALSE, BASE, 2300), "committed");
    CHECK_EQ(l.State, CM_LIVE_ACTIVE, "ACTIVE again");
    CHECK(CmLiveTransport(&l), "transport again");
    CHECK_EQ(l.Counters.RecoveredOk, 1, "counted");
    a = CmLiveSample(&l, 2600, ONES);
    CHECK(a & CM_LIVE_ACT_CONTAIN, "a later loss contains again");
    CHECK_EQ(CmLiveEpisode(&l), ep + 1, "a new episode");

    /* BAR0 changed: cooldown at once. */
    arm(&l, 1);
    (VOID)CmLiveSample(&l, 1200, ONES);
    a = CmLiveProbe(&l, 2200, CM_LIVE_PROBE_BAR);
    CHECK_EQ(l.Sub, CM_LIVE_SUB_COOLDOWN, "bar changed: cooldown");
    CHECK(!(a & CM_LIVE_ACT_PREPARE), "bar changed: no preparation");

    /* A suspend inside the attempt: cooldown, and the resume probes at
     * once as a new episode (no second outage asked). */
    arm(&l, 1);
    (VOID)CmLiveSample(&l, 1200, ONES);
    (VOID)CmLiveProbe(&l, 2200, CM_LIVE_PROBE_OK);
    (VOID)CmLivePrepared(&l, TRUE, FALSE);
    ep = CmLiveEpisode(&l);
    CmLiveU4Interrupt(&l);
    CHECK_EQ(l.State, CM_LIVE_CONTAINED, "interrupted: contained");
    CHECK_EQ(l.Sub, CM_LIVE_SUB_COOLDOWN, "interrupted: cooldown");
    a = CmLiveU4Wake(&l);
    CHECK(a & CM_LIVE_ACT_SNAPSHOT, "wake: new episode");
    CHECK_EQ(l.Sub, CM_LIVE_SUB_PROBING, "wake: probing");
    CHECK(CmLiveDue(&l, 2201), "wake: probed at once");
    CHECK_EQ(CmLiveEpisode(&l), ep + 1, "wake: episode bumped");

    /* The driver's own containment (a bind or resume loss), and the
     * revocation, which ends everything. */
    CmLiveInit(&l, 1, 200);
    CmLiveContain(&l, 500);
    CHECK_EQ(l.State, CM_LIVE_CONTAINED, "driver contain");
    CHECK_EQ(CmLiveEpisode(&l), 1, "driver contain: an episode");
    CmLiveContain(&l, 600);
    CHECK_EQ(CmLiveEpisode(&l), 1, "driver contain: once");
    (VOID)CmLiveProbe(&l, 1500, CM_LIVE_PROBE_OK);
    CHECK_EQ(l.State, CM_LIVE_PREPARING, "driver contain: probes");
    CmLiveDisarm(&l);
    CHECK_EQ(l.State, CM_LIVE_PREPARING, "disarm keeps PREPARING");
    CmLiveRevoke(&l);
    CHECK_EQ(CmLivePrepared(&l, TRUE, FALSE), 0, "revoked: nothing");
    CHECK(!CmLiveAdmits(&l, CM_ACCESS_PREPARE), "revoked: no PREPARE");
    CHECK_EQ(CmLiveProbe(&l, 2500, CM_LIVE_PROBE_OK), 0, "revoked: no probe");
}

int main(void)
{
    test_off_and_cadence();
    test_suspect_absent();
    test_suspect_back_light();
    test_config_lost_full();
    test_failure_and_rearm();
    test_transition_and_revoke();
    test_usb4_contain();
    test_usb4_recover();
    printf("%d checks, %d failures\n", checks, failures);
    return failures;
}
