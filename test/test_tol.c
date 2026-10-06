/*
 * test_tol.c - host vectors for controller tolerance's pure half
 * (src\xhci_tol.c; roadmap-hcd.md task 35-T.9, design record 17): the three
 * values' reading, the tolerance clock's tick arithmetic at each interval's
 * boundary and against the 55 ms early-expiry bound, the interval cap with
 * fast polling, the soft retry's scope and divert, the cycle and halt
 * decisions, the backstop's observation, a location's budget and its
 * re-arms, the recovery window and the all-ones episode - each at
 * XhciTolerance 0 and 1.
 */

#include <stdio.h>
#include <string.h>
#include "../src/xhci.h"
#include "../src/xhci_tol.h"
#include "../src/xhci_pipe.h"
#include "test_harness.h"

#define AMD_ID      0x15B61022UL
#define AMD_NOSR    0x43B91022UL
#define ETRON_NOSR  0x70521B6FUL
#define INTEL_ID    0x9D2F8086UL

static void test_values(void)
{
    CHECK_EQ(XhciTolMode(0, 0), 1, "tolerance absent: 1");
    CHECK_EQ(XhciTolMode(1, 0), 0, "tolerance 0");
    CHECK_EQ(XhciTolMode(1, 1), 1, "tolerance 1");
    CHECK_EQ(XhciTolMode(1, 2), 1, "tolerance 2 reads as 1");
    CHECK_EQ(XhciTolMode(0, 0xFFFFFFFFUL), 1, "tolerance not a DWORD");

    CHECK_EQ(XhciTolCapMode(0, 0), XHCI_TOL_CAP_GATED, "cap absent: 1");
    CHECK_EQ(XhciTolCapMode(1, 0), XHCI_TOL_CAP_OFF, "cap 0");
    CHECK_EQ(XhciTolCapMode(1, 1), XHCI_TOL_CAP_GATED, "cap 1");
    CHECK_EQ(XhciTolCapMode(1, 2), XHCI_TOL_CAP_ALL, "cap 2");
    CHECK_EQ(XhciTolCapMode(1, 3), XHCI_TOL_CAP_GATED, "cap 3 reads as 1");

    CHECK_EQ(XhciTolAvgTrbMode(0, 1), 0, "avg absent: 0");
    CHECK_EQ(XhciTolAvgTrbMode(1, 0), 0, "avg 0");
    CHECK_EQ(XhciTolAvgTrbMode(1, 1), 1, "avg 1");
    CHECK_EQ(XhciTolAvgTrbMode(1, 2), 0, "avg 2 reads as 0");
}

/* Every tick at its shortest is 55 ms less one clock period early at most;
 * the credit (45 ms) must stay under the shortest tick, and an interval's
 * count of shortest ticks must reach its nominal length from any phase. */
static void test_clock(void)
{
    ULONG ms[8];
    ULONG ticks[8];
    ULONG i;

    CHECK_EQ(XHCI_TOL_TICKS(45), 2, "45 ms: one tick plus the phase tick");
    CHECK_EQ(XHCI_TOL_TICKS(46), 3, "46 ms: two plus one");
    CHECK_EQ(XHCI_TOL_TICKS(90), 3, "90 ms");
    CHECK_EQ(XHCI_TOL_TICKS(91), 4, "91 ms");
    CHECK_EQ(XHCI_TOL_BACKSTOP_TICKS, 4, "backstop 100 ms is four ticks");
    CHECK_EQ(XHCI_TOL_STABLE_DISC_TICKS, 24, "1 s");
    CHECK_EQ(XHCI_TOL_CONTAIN_TICKS, 24, "containment 1 s");
    CHECK_EQ(XHCI_TOL_OC_WAIT_TICKS, 113, "5 s");
    CHECK_EQ(XHCI_TOL_STABLE_PROGRESS_TICKS, 1335, "60 s");
    CHECK_EQ(XHCI_TOL_RECOVERY_WINDOW_TICKS, 13335, "10 min");

    ms[0] = XHCI_TOL_BACKSTOP_MS;       ticks[0] = XHCI_TOL_BACKSTOP_TICKS;
    ms[1] = XHCI_TOL_STABLE_PROGRESS_MS; ticks[1] = XHCI_TOL_STABLE_PROGRESS_TICKS;
    ms[2] = XHCI_TOL_STABLE_DISC_MS;    ticks[2] = XHCI_TOL_STABLE_DISC_TICKS;
    ms[3] = XHCI_TOL_OC_SETTLE_MS;      ticks[3] = XHCI_TOL_OC_SETTLE_TICKS;
    ms[4] = XHCI_TOL_OC_WAIT_MS;        ticks[4] = XHCI_TOL_OC_WAIT_TICKS;
    ms[5] = XHCI_TOL_POWER_ON_MS;       ticks[5] = XHCI_TOL_POWER_ON_TICKS;
    ms[6] = XHCI_TOL_CONTAIN_MS;        ticks[6] = XHCI_TOL_CONTAIN_TICKS;
    ms[7] = XHCI_TOL_RECOVERY_WINDOW_MS; ticks[7] = XHCI_TOL_RECOVERY_WINDOW_TICKS;
    for (i = 0; i < 8; i++) {
        /* The stamp is taken at worst just before a tick: one tick of the
         * count is spent at once, the rest each at least 45 ms (a 100 ms
         * period at most 55 ms early). */
        CHECK((ticks[i] - 1) * XHCI_TOL_TICK_CREDIT_MS >= ms[i],
              "an interval never runs short");
        CHECK((ticks[i] - 2) * XHCI_TOL_TICK_CREDIT_MS < ms[i],
              "and is the least count that does not");
    }
    CHECK(XHCI_TOL_TICK_MS - 55 >= XHCI_TOL_TICK_CREDIT_MS,
          "the credit is under the shortest tick");

    CHECK_EQ(XhciTolElapsed(10, 6, 4), 1, "elapsed at the boundary");
    CHECK_EQ(XhciTolElapsed(9, 6, 4), 0, "one short");
    CHECK_EQ(XhciTolElapsed(2, 0xFFFFFFFEUL, 4), 1, "across the wrap");
    CHECK_EQ(XhciTolElapsed(1, 0xFFFFFFFEUL, 4), 0, "across the wrap, short");
}

static void test_cap(void)
{
    CHECK_EQ(XhciTolCapApplies(XHCI_TOL_CAP_GATED, AMD_ID), 1, "AMD gated");
    CHECK_EQ(XhciTolCapApplies(XHCI_TOL_CAP_GATED, INTEL_ID), 0, "Intel not");
    CHECK_EQ(XhciTolCapApplies(XHCI_TOL_CAP_OFF, AMD_ID), 0, "0 off");
    CHECK_EQ(XhciTolCapApplies(XHCI_TOL_CAP_ALL, INTEL_ID), 1, "2 every");
    CHECK_EQ(XhciTolCapApplies(3, AMD_ID), 0, "a raw 3 is not a mode");

    CHECK_EQ(XhciTolCapInterval(1, 1, 9), 8, "interrupt capped");
    CHECK_EQ(XhciTolCapInterval(1, 0, 9), 9, "other endpoints not");
    CHECK_EQ(XhciTolCapInterval(0, 1, 9), 9, "not applied");
    CHECK_EQ(XhciTolCapInterval(1, 1, 8), 8, "8 kept");
    CHECK_EQ(XhciTolCapInterval(1, 1, 15), 8, "15");
    CHECK_EQ(XhciTolCapInterval(1, 1, 3), 3, "3 kept");

    CHECK_EQ(XhciTolAvgTrbLength(0, 1, 8), 1024, "off");
    CHECK_EQ(XhciTolAvgTrbLength(1, 1, 8), 8, "on");
    CHECK_EQ(XhciTolAvgTrbLength(1, 1, 0), 1, "0 to 1");
    CHECK_EQ(XhciTolAvgTrbLength(1, 1, 0x10000UL), 0xFFFF, "held to 65535");
    CHECK_EQ(XhciTolAvgTrbLength(1, 0, 192), 1024,
             "isochronous and others never changed");
    CHECK_EQ(XhciTolAvgTrbLength(2, 1, 8), 1024, "a raw 2 is not on");
}

/* Record 17 section 4.7's ordering, as the executor runs it: the caller's
 * rewritten bInterval, then the cap, then XhciPipeFastPoll only when the cap
 * left the Interval as it was (eligibility judged on the uncapped value);
 * XhciPipeFastRevert, the fallback, restores the capped Interval. Every cap
 * mode against every fast-polling mode, Intervals 3 to 15. */
static void test_cap_fastpoll(void)
{
    ULONG cap;
    ULONG fast;
    ULONG uncapped;

    for (cap = 0; cap <= 2; cap++) {
        for (fast = 0; fast <= 3; fast++) {
            for (uncapped = 3; uncapped <= 15; uncapped++) {
                XHCI_PIPE_EP ep;
                ULONG applies = XhciTolCapApplies(cap, AMD_ID);
                ULONG capped;
                ULONG offered;
                ULONG fastPolled;

                memset(&ep, 0, sizeof(ep));
                ep.TransferType = XHCI_PIPE_XFER_INTERRUPT;
                capped = XhciTolCapInterval(applies, 1, uncapped);
                ep.Interval = capped;
                offered = (capped == uncapped);
                fastPolled = offered ?
                    XhciPipeFastPoll(XhciPipeFastMode(fast),
                                     XHCI_PIPE_SPEED_FULL, 0, &ep) : 0;

                CHECK(capped <= uncapped, "the cap never raises");
                CHECK(!applies || capped <= XHCI_TOL_INTERVAL_CAP,
                      "capped at 8 when applied");
                CHECK(applies || capped == uncapped, "untouched when not");
                CHECK(!fastPolled || uncapped == 7 || uncapped == 8,
                      "the cap makes nothing eligible");
                CHECK(fastPolled == (ULONG)((fast == XHCI_PIPE_FAST_2K4K ||
                                      fast == XHCI_PIPE_FAST_4K8K) &&
                                     (uncapped == 7 || uncapped == 8)),
                      "eligibility is the uncapped value's");
                (VOID)XhciPipeFastRevert(&ep);
                CHECK_EQ(ep.Interval, capped,
                         "the fallback restores the capped Interval");
            }
        }
    }
}

static void test_retry(void)
{
    CHECK_EQ(XhciTolNoSoftRetry(AMD_NOSR), 1, "43B9 listed");
    CHECK_EQ(XhciTolNoSoftRetry(0x43BB1022UL), 1, "43BB listed");
    CHECK_EQ(XhciTolNoSoftRetry(0x70231B6FUL), 1, "7023 listed");
    CHECK_EQ(XhciTolNoSoftRetry(ETRON_NOSR), 1, "7052 listed");
    CHECK_EQ(XhciTolNoSoftRetry(AMD_ID), 0, "other AMD not");

    CHECK_EQ(XhciTolRetryScope(1, AMD_ID, 1, 0, 0), 1,
             "bulk in scope");
    CHECK_EQ(XhciTolRetryScope(1, AMD_ID, 1, 0, 0),
             1, "interrupt in scope");
    CHECK_EQ(XhciTolRetryScope(0, AMD_ID, 1, 0, 0), 0,
             "tolerance 0");
    CHECK_EQ(XhciTolRetryScope(1, AMD_ID, 0, 0, 0), 0,
             "EP0 not");
    CHECK_EQ(XhciTolRetryScope(1, AMD_ID, 0, 0, 0), 0,
             "isochronous not");
    CHECK_EQ(XhciTolRetryScope(1, AMD_ID, 1, 1, 0), 0,
             "streams not");
    CHECK_EQ(XhciTolRetryScope(1, AMD_ID, 1, 0, 1), 0,
             "behind a TT not");
    CHECK_EQ(XhciTolRetryScope(1, AMD_NOSR, 1, 0, 0), 0,
             "listed controller not");

    CHECK_EQ(XhciTolRetryDivert(1, XHCI_CC_USB_TRANSACTION_ERROR, 1, 0), 1,
             "first error diverted");
    CHECK_EQ(XhciTolRetryDivert(1, XHCI_CC_USB_TRANSACTION_ERROR, 1, 2), 1,
             "third diverted");
    CHECK_EQ(XhciTolRetryDivert(1, XHCI_CC_USB_TRANSACTION_ERROR, 1, 3), 0,
             "fourth takes today's path");
    CHECK_EQ(XhciTolRetryDivert(1, XHCI_CC_USB_TRANSACTION_ERROR, 0, 0), 0,
             "not the head");
    CHECK_EQ(XhciTolRetryDivert(1, XHCI_CC_STALL, 1, 0), 0, "a stall not");
    CHECK_EQ(XhciTolRetryDivert(0, XHCI_CC_USB_TRANSACTION_ERROR, 1, 0), 0,
             "out of scope");
}

static void test_cycle(void)
{
    CHECK_EQ(XhciTolCycleOnRefused(1, 0, 0, 1), 1, "refused code cycles");
    CHECK_EQ(XhciTolCycleOnRefused(0, 0, 0, 1), 0, "tolerance 0");
    CHECK_EQ(XhciTolCycleOnRefused(1, 1, 0, 1), 0, "claimed code not");
    CHECK_EQ(XhciTolCycleOnRefused(1, 0, 1, 1), 0, "isochronous not");
    CHECK_EQ(XhciTolCycleOnRefused(1, 0, 0, 0), 0, "no device: dropped");

    CHECK_EQ(XhciTolHaltCandidate(1, XHCI_CC_STALL, 0, 1), 1, "stall");
    CHECK_EQ(XhciTolHaltCandidate(1, XHCI_CC_USB_TRANSACTION_ERROR, 0, 1), 1,
             "transaction");
    CHECK_EQ(XhciTolHaltCandidate(1, XHCI_CC_BABBLE, 0, 1), 1, "babble");
    CHECK_EQ(XhciTolHaltCandidate(1, XHCI_CC_SPLIT_TRANSACTION, 0, 1), 1,
             "split");
    CHECK_EQ(XhciTolHaltCandidate(1, XHCI_CC_SHORT_PACKET, 0, 1), 0, "short");
    CHECK_EQ(XhciTolHaltCandidate(1, XHCI_CC_STALL, 1, 1), 0, "isochronous");
    CHECK_EQ(XhciTolHaltCandidate(1, XHCI_CC_STALL, 0, 0), 0, "no pipe");
    CHECK_EQ(XhciTolHaltCandidate(0, XHCI_CC_STALL, 0, 1), 0, "tolerance 0");

    CHECK_EQ(XhciTolHaltConfirmed(XHCI_EP_STATE_HALTED), 1, "Halted");
    CHECK_EQ(XhciTolHaltConfirmed(XHCI_EP_STATE_ERROR), 1, "Error");
    CHECK_EQ(XhciTolHaltConfirmed(XHCI_EP_STATE_RUNNING), 0, "stale: running");
    CHECK_EQ(XhciTolHaltConfirmed(XHCI_EP_STATE_STOPPED), 0, "stale: stopped");
}

static void test_backstop(void)
{
    XHCI_TOL_OBS o;

    o.Valid = 0;
    CHECK_EQ(XhciTolBackstop(&o, 1, 1, 5, 1, 7, 2, 100), 0, "first seen");
    CHECK_EQ(XhciTolBackstop(&o, 1, 1, 5, 1, 7, 2, 103), 0, "three ticks");
    CHECK_EQ(XhciTolBackstop(&o, 1, 1, 5, 1, 7, 2, 104), 1, "four: queued");
    CHECK_EQ(XhciTolBackstop(&o, 1, 1, 5, 1, 7, 2, 105), 0,
             "once per interval");
    CHECK_EQ(XhciTolBackstop(&o, 1, 1, 5, 1, 7, 2, 108), 1, "again");

    CHECK_EQ(XhciTolBackstop(&o, 1, 1, 5, 1, 8, 2, 120), 0,
             "a drain ran: restarted");
    CHECK_EQ(XhciTolBackstop(&o, 1, 1, 5, 1, 8, 3, 130), 0,
             "a start: restarted");
    CHECK_EQ(XhciTolBackstop(&o, 1, 1, 5, 0, 8, 3, 140), 0, "cycle moved");
    CHECK_EQ(XhciTolBackstop(&o, 1, 1, 6, 0, 8, 3, 150), 0, "index moved");
    CHECK_EQ(XhciTolBackstop(&o, 1, 0, 6, 0, 8, 3, 160), 0, "nothing pending");
    CHECK_EQ(o.Valid, 0, "cleared");
    CHECK_EQ(XhciTolBackstop(&o, 1, 1, 6, 0, 8, 3, 161), 0, "seen anew");
    CHECK_EQ(XhciTolBackstop(&o, 0, 1, 6, 0, 8, 3, 200), 0, "tolerance 0");
    CHECK_EQ(o.Valid, 0, "no observation kept at 0");
    CHECK_EQ(XhciTolBackstop(NULL, 1, 1, 0, 0, 0, 0, 0), 0, "NULL");
}

static void test_loc(void)
{
    XHCI_TOL_LOC l;
    ULONG i;

    XhciTolLocInit(&l);
    for (i = 0; i < XHCI_TOL_REENUMS; i++) {
        CHECK_EQ(XhciTolLocCharge(&l, XHCI_TOL_CHARGE_REENUM, 10 + i), 1,
                 "re-enumeration allowed");
    }
    CHECK_EQ(XhciTolLocCharge(&l, XHCI_TOL_CHARGE_REENUM, 20), 0, "fourth");
    CHECK_EQ(l.Hold, XHCI_TOL_HOLD_REENUMS, "held powered");
    CHECK_EQ(l.Charges, 3, "three charged");
    CHECK_EQ(XhciTolLocCharge(&l, XHCI_TOL_CHARGE_REPOWER, 21), 0,
             "a held location is not charged");
    CHECK_EQ(XhciTolLocProgress(&l, 100000), 0, "no progress while held");

    /* Stable disconnect, then a connection: released and re-armed. */
    CHECK_EQ(XhciTolLocObserve(&l, 0, 1, 0, 100), 0, "disconnect seen");
    CHECK_EQ(XhciTolLocObserve(&l, 0, 1, 0, 100 + XHCI_TOL_STABLE_DISC_TICKS - 1),
             0, "one short");
    CHECK_EQ(XhciTolLocObserve(&l, 1, 1, 0, 100 + XHCI_TOL_STABLE_DISC_TICKS - 1),
             0, "connected too soon: not evidence");
    CHECK_EQ(l.Hold, XHCI_TOL_HOLD_REENUMS, "still held");
    CHECK_EQ(XhciTolLocObserve(&l, 0, 1, 0, 200), 0, "disconnect again");
    CHECK_EQ(XhciTolLocObserve(&l, 0, 1, 0, 200 + XHCI_TOL_STABLE_DISC_TICKS),
             0, "stable");
    CHECK_EQ(XhciTolLocObserve(&l, 1, 1, 0, 300), 1, "connection re-arms");
    CHECK_EQ(l.Hold, XHCI_TOL_HOLD_NONE, "released");
    CHECK_EQ(l.Reenums, 0, "budget back");
    CHECK_EQ(l.Charges, 3, "the dump's total kept");

    /* A disconnect caused by the recovery (PP clear, or a fault) is not
     * evidence. */
    CHECK_EQ(XhciTolLocObserve(&l, 0, 0, 0, 400), 0, "unpowered");
    CHECK_EQ(XhciTolLocObserve(&l, 0, 0, 0, 400 + 100), 0, "still");
    CHECK_EQ(XhciTolLocObserve(&l, 1, 1, 0, 600), 0, "no re-arm");
    CHECK_EQ(XhciTolLocObserve(&l, 0, 1, 1, 700), 0, "fault active");
    CHECK_EQ(XhciTolLocObserve(&l, 0, 1, 1, 700 + 100), 0, "fault active");
    CHECK_EQ(XhciTolLocObserve(&l, 1, 1, 0, 900), 0, "no re-arm");

    /* A charge voids evidence gathered before it. */
    CHECK_EQ(XhciTolLocObserve(&l, 0, 1, 0, 1000), 0, "disconnect");
    CHECK_EQ(XhciTolLocObserve(&l, 0, 1, 0, 1000 + 30), 0, "stable");
    CHECK_EQ(XhciTolLocCharge(&l, XHCI_TOL_CHARGE_REENUM, 1031), 1, "charge");
    CHECK_EQ(XhciTolLocObserve(&l, 1, 1, 0, 1032), 0, "voided");

    /* Repowers: their own three, an unpowered hold no disconnect releases. */
    XhciTolLocInit(&l);
    for (i = 0; i < XHCI_TOL_REPOWERS; i++) {
        CHECK_EQ(XhciTolLocCharge(&l, XHCI_TOL_CHARGE_REPOWER, i), 1,
                 "repower allowed");
    }
    CHECK_EQ(XhciTolLocCharge(&l, XHCI_TOL_CHARGE_REENUM, 5), 1,
             "re-enumerations kept apart");
    CHECK_EQ(XhciTolLocCharge(&l, XHCI_TOL_CHARGE_REPOWER, 6), 0, "fourth");
    CHECK_EQ(l.Hold, XHCI_TOL_HOLD_REPOWERS, "held unpowered");
    CHECK_EQ(XhciTolLocObserve(&l, 0, 1, 0, 10), 0, "disconnect");
    CHECK_EQ(XhciTolLocObserve(&l, 0, 1, 0, 100), 0, "stable");
    CHECK_EQ(XhciTolLocObserve(&l, 1, 1, 0, 200), 1, "re-armed");
    CHECK_EQ(l.Hold, XHCI_TOL_HOLD_REPOWERS, "but only a start releases");
    XhciTolLocInit(&l);
    CHECK_EQ(l.Hold, XHCI_TOL_HOLD_NONE, "a start releases");

    /* Stable progress. */
    XhciTolLocInit(&l);
    CHECK_EQ(XhciTolLocProgress(&l, 99999), 0, "nothing charged");
    CHECK_EQ(XhciTolLocCharge(&l, XHCI_TOL_CHARGE_REENUM, 50), 1, "charge");
    /* A long stall, then one completion: that only starts the interval. */
    CHECK_EQ(XhciTolLocProgress(&l, 50 + 5000), 0, "first completion arms");
    CHECK_EQ(l.Reenums, 1, "a stall is not progress");
    CHECK_EQ(XhciTolLocProgress(&l,
                                50 + 5000 + XHCI_TOL_STABLE_PROGRESS_TICKS - 1),
             0, "one short");
    /* A fault between voids it. */
    CHECK_EQ(XhciTolLocObserve(&l, 1, 1, 1, 5060), 0, "fault");
    CHECK_EQ(XhciTolLocProgress(&l,
                                50 + 5000 + XHCI_TOL_STABLE_PROGRESS_TICKS),
             0, "voided: arms again");
    CHECK_EQ(XhciTolLocProgress(&l,
                                50 + 5000 + 2 * XHCI_TOL_STABLE_PROGRESS_TICKS),
             1, "a sustained interval re-arms");
    CHECK_EQ(l.Reenums, 0, "budget back");
    CHECK_EQ(XhciTolLocProgress(&l, 99999), 0, "nothing left to re-arm");
    /* A disconnect voids it too; a charge as well. */
    CHECK_EQ(XhciTolLocCharge(&l, XHCI_TOL_CHARGE_REENUM, 100000), 1, "charge");
    CHECK_EQ(XhciTolLocProgress(&l, 100001), 0, "arms");
    CHECK_EQ(XhciTolLocObserve(&l, 0, 1, 0, 100002), 0, "disconnect");
    CHECK_EQ(XhciTolLocProgress(&l, 100001 + XHCI_TOL_STABLE_PROGRESS_TICKS),
             0, "voided by the disconnect");
    CHECK_EQ(XhciTolLocCharge(&l, XHCI_TOL_CHARGE_REENUM, 200000), 1, "charge");
    CHECK_EQ(XhciTolLocProgress(&l, 200000 + XHCI_TOL_STABLE_PROGRESS_TICKS),
             0, "voided by the charge: arms");
    CHECK_EQ(XhciTolLocCharge(NULL, 0, 0), 0, "NULL");
}

/* 35-T.5: the over-current episode at each interval's boundary, and the
 * holds an over-current leaves. */
static void test_port_oc(void)
{
    XHCI_TOL_OC oc;
    XHCI_TOL_LOC l;
    ULONG t;
    ULONG s;

    XhciTolOcInit(&oc);
    CHECK_EQ(XhciTolOcStep(&oc, 0, 5), XHCI_TOL_OC_ACT_NONE, "no episode");

    /* OCA set, then clear: the settle interval at its boundary, the repower,
     * then the power-on interval at its boundary. */
    t = 1000;
    XhciTolOcBegin(&oc, t);
    CHECK_EQ(oc.Phase, XHCI_TOL_OC_WAIT, "waiting");
    CHECK_EQ(XhciTolOcStep(&oc, 1, t + 1), XHCI_TOL_OC_ACT_NONE, "OCA set");
    s = t + 2;
    CHECK_EQ(XhciTolOcStep(&oc, 0, s), XHCI_TOL_OC_ACT_NONE, "OCA clear");
    CHECK_EQ(oc.Phase, XHCI_TOL_OC_SETTLE, "settling");
    CHECK_EQ(XhciTolOcStep(&oc, 0, s + XHCI_TOL_OC_SETTLE_TICKS - 1),
             XHCI_TOL_OC_ACT_NONE, "settle not passed");
    CHECK_EQ(XhciTolOcStep(&oc, 0, s + XHCI_TOL_OC_SETTLE_TICKS),
             XHCI_TOL_OC_ACT_REPOWER, "settled: repower");
    CHECK_EQ(oc.Phase, XHCI_TOL_OC_POWER_ON, "powering");
    s = s + XHCI_TOL_OC_SETTLE_TICKS;
    CHECK_EQ(XhciTolOcStep(&oc, 0, s + XHCI_TOL_POWER_ON_TICKS - 1),
             XHCI_TOL_OC_ACT_NONE, "power-on not passed");
    CHECK_EQ(XhciTolOcStep(&oc, 0, s + XHCI_TOL_POWER_ON_TICKS),
             XHCI_TOL_OC_ACT_INSPECT, "powered: inspect");
    CHECK_EQ(oc.Phase, XHCI_TOL_OC_NONE, "episode over");

    /* OCA set again during the settle restarts it; the wait is not
     * extended by a second over-current. */
    t = 5000;
    XhciTolOcBegin(&oc, t);
    CHECK_EQ(XhciTolOcStep(&oc, 0, t + 1), XHCI_TOL_OC_ACT_NONE, "clear");
    CHECK_EQ(XhciTolOcStep(&oc, 1, t + 2), XHCI_TOL_OC_ACT_NONE, "set again");
    CHECK_EQ(oc.Phase, XHCI_TOL_OC_WAIT, "settle restarted");
    XhciTolOcBegin(&oc, t + 3);
    CHECK_EQ(oc.Stamp, t, "wait not extended");
    CHECK_EQ(XhciTolOcStep(&oc, 1, t + XHCI_TOL_OC_WAIT_TICKS - 1),
             XHCI_TOL_OC_ACT_NONE, "wait not passed");
    CHECK_EQ(XhciTolOcStep(&oc, 1, t + XHCI_TOL_OC_WAIT_TICKS),
             XHCI_TOL_OC_ACT_GIVE_UP, "OCA never cleared: give up");
    CHECK_EQ(oc.Phase, XHCI_TOL_OC_NONE, "ended");

    /* A flapping OCA gives up at the wait as well. */
    t = 9000;
    XhciTolOcBegin(&oc, t);
    for (s = 1; s < XHCI_TOL_OC_WAIT_TICKS; s++) {
        CHECK_EQ(XhciTolOcStep(&oc, s & 1, t + s), XHCI_TOL_OC_ACT_NONE,
                 "flapping");
    }
    CHECK_EQ(XhciTolOcStep(&oc, 1, t + XHCI_TOL_OC_WAIT_TICKS),
             XHCI_TOL_OC_ACT_GIVE_UP, "flapping: give up");

    /* A new over-current during the power-on wait starts a new episode. */
    XhciTolOcBegin(&oc, 20000);
    (VOID)XhciTolOcStep(&oc, 0, 20001);
    CHECK_EQ(XhciTolOcStep(&oc, 0, 20001 + XHCI_TOL_OC_SETTLE_TICKS),
             XHCI_TOL_OC_ACT_REPOWER, "repower");
    XhciTolOcBegin(&oc, 20100);
    CHECK_EQ(oc.Phase, XHCI_TOL_OC_WAIT, "new episode");
    CHECK_EQ(oc.Stamp, 20100, "stamped anew");

    /* Holds: an over-current wait that ran out is unpowered; a powered hold
     * made unpowered counts once; neither is released by a disconnect. */
    XhciTolLocInit(&l);
    CHECK_EQ(XhciTolLocUnpowered(&l), 0, "not held");
    XhciTolLocHold(&l, XHCI_TOL_HOLD_OC_WAIT);
    CHECK_EQ(l.Hold, XHCI_TOL_HOLD_OC_WAIT, "held");
    CHECK_EQ(l.Holds, 1, "counted");
    CHECK_EQ(XhciTolLocUnpowered(&l), 1, "unpowered");
    CHECK_EQ(XhciTolLocObserve(&l, 0, 1, 0, 100), 0, "disconnect");
    (VOID)XhciTolLocObserve(&l, 1, 1, 0, 1000);
    CHECK_EQ(l.Hold, XHCI_TOL_HOLD_OC_WAIT, "still held");
    CHECK_EQ(XhciTolLocCharge(&l, XHCI_TOL_CHARGE_REPOWER, 1001), 0,
             "no charge while held");

    XhciTolLocInit(&l);
    for (s = 0; s < XHCI_TOL_REENUMS; s++) {
        (VOID)XhciTolLocCharge(&l, XHCI_TOL_CHARGE_REENUM, s);
    }
    CHECK_EQ(XhciTolLocCharge(&l, XHCI_TOL_CHARGE_REENUM, 10), 0, "spent");
    CHECK_EQ(l.Hold, XHCI_TOL_HOLD_REENUMS, "held powered");
    CHECK_EQ(XhciTolLocUnpowered(&l), 0, "powered hold");
    XhciTolLocHold(&l, XHCI_TOL_HOLD_REPOWERS);
    CHECK_EQ(l.Hold, XHCI_TOL_HOLD_REPOWERS, "now unpowered");
    CHECK_EQ(l.Holds, 1, "one hold");
    CHECK_EQ(XhciTolLocObserve(&l, 0, 1, 0, 100), 0, "disconnect");
    (VOID)XhciTolLocObserve(&l, 1, 1, 0, 1000);
    CHECK_EQ(l.Hold, XHCI_TOL_HOLD_REPOWERS, "not released");
    XhciTolLocHold(NULL, XHCI_TOL_HOLD_OC_WAIT);
    CHECK_EQ(XhciTolLocUnpowered(NULL), 0, "NULL");

    /* Review round 1, finding 4: a late connected look is no evidence that
     * the port stayed disconnected; the stable disconnect stands only on a
     * disconnected look made once the interval has passed (XhciTolLocDiscDue
     * says when one is owed), with no connection change between. */
    XhciTolLocInit(&l);
    (VOID)XhciTolLocCharge(&l, XHCI_TOL_CHARGE_REENUM, 1);
    CHECK_EQ(XhciTolLocObserveChange(&l, 0, 1, 0, 1, 10), 0, "disconnect");
    CHECK_EQ(XhciTolLocObserveChange(&l, 1, 1, 0, 1,
                                     10 + XHCI_TOL_STABLE_DISC_TICKS + 50),
             0, "late connected look: no re-arm");
    CHECK_EQ(l.Reenums, 1, "budget kept");
    CHECK_EQ(XhciTolLocObserveChange(&l, 0, 1, 0, 1, 100), 0, "disconnect");
    CHECK_EQ(XhciTolLocDiscDue(&l, 100 + XHCI_TOL_STABLE_DISC_TICKS - 1), 0,
             "not yet due");
    CHECK_EQ(XhciTolLocDiscDue(&l, 100 + XHCI_TOL_STABLE_DISC_TICKS), 1,
             "a look is due");
    CHECK_EQ(XhciTolLocObserveChange(&l, 0, 1, 0, 1,
                                     100 + XHCI_TOL_STABLE_DISC_TICKS),
             0, "a connection change between the looks restarts");
    CHECK_EQ(l.DiscSeen, 0, "not seen");
    CHECK_EQ(XhciTolLocDiscDue(&l, 100 + XHCI_TOL_STABLE_DISC_TICKS), 0,
             "restarted: not due");
    t = 100 + 2 * XHCI_TOL_STABLE_DISC_TICKS;
    CHECK_EQ(XhciTolLocObserveChange(&l, 0, 1, 0, 0, t), 0, "stable");
    CHECK_EQ(l.DiscSeen, 1, "seen");
    CHECK_EQ(XhciTolLocDiscDue(&l, t), 0, "nothing owed");
    CHECK_EQ(XhciTolLocObserveChange(&l, 1, 1, 0, 1, t + 500), 1,
             "the connection after it re-arms");
    CHECK_EQ(l.Reenums, 0, "budget back");
    CHECK_EQ(l.Rearms, 1, "re-arm counted");

    /* Finding 2: the recovery's own disconnect - an over-current's lost
     * power, its device not back after the power-on wait - is no evidence,
     * through the reconnect that ends it; disconnects after that are. */
    XhciTolLocInit(&l);
    (VOID)XhciTolLocCharge(&l, XHCI_TOL_CHARGE_REPOWER, 1);
    XhciTolLocRecovery(&l);
    CHECK_EQ(XhciTolLocObserve(&l, 0, 0, 1, 2), 0, "unpowered, in fault");
    CHECK_EQ(XhciTolLocObserve(&l, 0, 1, 0, 100), 0, "power back, no device");
    t = 100 + 3 * XHCI_TOL_STABLE_DISC_TICKS;
    CHECK_EQ(XhciTolLocObserve(&l, 0, 1, 0, t), 0, "still none");
    CHECK_EQ(XhciTolLocDiscDue(&l, t), 0, "no look owed");
    CHECK_EQ(XhciTolLocObserve(&l, 1, 1, 0, t + 1), 0,
             "the reconnect re-arms nothing");
    CHECK_EQ(l.Repowers, 1, "repower budget kept");
    CHECK_EQ(l.RecoveryDisc, 0, "finished");
    CHECK_EQ(XhciTolLocObserve(&l, 0, 1, 0, 1000), 0, "an unplug");
    CHECK_EQ(XhciTolLocObserve(&l, 0, 1, 0,
                               1000 + XHCI_TOL_STABLE_DISC_TICKS), 0,
             "stable");
    CHECK_EQ(XhciTolLocObserve(&l, 1, 1, 0, 2000), 1, "then evidence");
    XhciTolLocRecovery(NULL);
    CHECK_EQ(XhciTolLocDiscDue(NULL, 0), 0, "NULL");
    CHECK_EQ(XhciTolOcStep(NULL, 0, 0), XHCI_TOL_OC_ACT_NONE, "NULL");
}

static void test_port(void)
{
    CHECK_EQ(XhciTolPedFault(1, 1, 1, 0, 1, 1), 1, "PED fault");
    CHECK_EQ(XhciTolPedFault(0, 1, 1, 0, 1, 1), 0, "tolerance 0");
    CHECK_EQ(XhciTolPedFault(1, 0, 1, 0, 1, 1), 0, "USB 3 port");
    CHECK_EQ(XhciTolPedFault(1, 1, 0, 0, 1, 1), 0, "no PEC");
    CHECK_EQ(XhciTolPedFault(1, 1, 1, 1, 1, 1), 0, "still enabled");
    CHECK_EQ(XhciTolPedFault(1, 1, 1, 0, 0, 1), 0, "disconnected");
    CHECK_EQ(XhciTolPedFault(1, 1, 1, 0, 1, 0), 0, "no device held");

    CHECK_EQ(XhciTolOcFault(1, 1, 1, 1), 1, "OCC");
    CHECK_EQ(XhciTolOcFault(1, 0, 0, 1), 1, "PP lost");
    CHECK_EQ(XhciTolOcFault(1, 0, 0, 0), 0, "PP clear, never powered");
    CHECK_EQ(XhciTolOcFault(1, 0, 1, 1), 0, "healthy");
    CHECK_EQ(XhciTolOcFault(0, 1, 0, 1), 0, "tolerance 0");
    {
        ULONG lost = 0;
        ULONG n;

        n = XhciTolOffOcCount(1, 0, &lost);
        n += XhciTolOffOcCount(0, 0, &lost);
        n += XhciTolOffOcCount(0, 0, &lost);
        CHECK_EQ(n, 1, "off: OCC then PP still clear counts once");
        n = XhciTolOffOcCount(0, 1, &lost);
        CHECK_EQ(n, 0, "off: power back");
        n = XhciTolOffOcCount(0, 0, &lost);
        n += XhciTolOffOcCount(0, 0, &lost);
        CHECK_EQ(n, 1, "off: a second loss with no OCC counts once");
        n = XhciTolOffOcCount(1, 1, &lost);
        CHECK_EQ(n, 1, "off: OCC with power on");
    }

    test_port_oc();

    CHECK_EQ(XhciTolHchRecover(1, 1, 1), 1, "HCH while running");
    CHECK_EQ(XhciTolHchRecover(1, 1, 0), 0, "HCH expected");
    CHECK_EQ(XhciTolHchRecover(1, 0, 1), 0, "running");
    CHECK_EQ(XhciTolHchRecover(0, 1, 1), 0, "tolerance 0");
}

static void test_window(void)
{
    XHCI_TOL_WINDOW w;
    ULONG span = XHCI_TOL_RECOVERY_WINDOW_TICKS;

    XhciTolWindowInit(&w);
    CHECK_EQ(XhciTolWindowAdmit(&w, 1, 0), 1, "first");
    CHECK_EQ(XhciTolWindowAdmit(&w, 1, 10), 1, "second");
    CHECK_EQ(XhciTolWindowAdmit(&w, 1, 20), 1, "third");
    CHECK_EQ(XhciTolWindowAdmit(&w, 1, 30), 0, "fourth refused");
    CHECK_EQ(w.Refused, 1, "counted");
    CHECK_EQ(XhciTolWindowAdmit(&w, 1, span - 1), 0, "first still inside");
    CHECK_EQ(XhciTolWindowAdmit(&w, 1, span), 1, "first aged out");
    CHECK_EQ(XhciTolWindowAdmit(&w, 1, span + 9), 0, "second still inside");
    CHECK_EQ(XhciTolWindowAdmit(&w, 1, span + 20), 1, "second and third out");
    CHECK_EQ(w.Count, 2, "two inside");

    XhciTolWindowInit(&w);
    CHECK_EQ(XhciTolWindowAdmit(&w, 0, 0), 1, "tolerance 0: always");
    CHECK_EQ(XhciTolWindowAdmit(&w, 0, 0), 1, "always");
    CHECK_EQ(XhciTolWindowAdmit(&w, 0, 0), 1, "always");
    CHECK_EQ(XhciTolWindowAdmit(&w, 0, 0), 1, "always");
    CHECK_EQ(w.Count, 0, "nothing recorded");
}

static void test_dead(void)
{
    XHCI_TOL_DEAD d;
    ULONG c = XHCI_TOL_CONTAIN_TICKS;

    d.Armed = 0;
    CHECK_EQ(XhciTolDeadStep(&d, 1, 1, 1, 7, 100), 0, "first all-ones");
    CHECK_EQ(XhciTolDeadStep(&d, 1, 1, 1, 7, 100 + c - 1), 0, "one short");
    CHECK_EQ(XhciTolDeadStep(&d, 1, 1, 1, 7, 100 + c), 1, "contain");

    d.Armed = 0;
    (VOID)XhciTolDeadStep(&d, 1, 1, 1, 7, 0);
    CHECK_EQ(XhciTolDeadStep(&d, 1, 1, 0, 7, 5), 0, "a good read");
    CHECK_EQ(d.Armed, 0, "clears");
    (VOID)XhciTolDeadStep(&d, 1, 1, 1, 7, 6);
    CHECK_EQ(XhciTolDeadStep(&d, 1, 0, 1, 7, 7), 0, "failed admission");
    CHECK_EQ(d.Armed, 0, "clears");
    (VOID)XhciTolDeadStep(&d, 1, 1, 1, 7, 8);
    CHECK_EQ(XhciTolDeadStep(&d, 1, 1, 1, 8, 8 + c), 0,
             "a new start generation restamps");
    CHECK_EQ(XhciTolDeadStep(&d, 1, 1, 1, 8, 8 + c + c), 1, "then contains");
    d.Armed = 0;
    CHECK_EQ(XhciTolDeadStep(&d, 0, 1, 1, 8, 0), 0, "tolerance 0");
    CHECK_EQ(XhciTolDeadStep(&d, 0, 1, 1, 8, 1000), 0, "never stamped");
    CHECK_EQ(d.Armed, 0, "no stamp at 0");
}

/* 35-T.8: the dump's terminal reason, each latch at its boundary. */
static void test_terminal(void)
{
    CHECK_EQ(XhciTolTerminal(0, 0, 0, 0, 3), XHCI_TOL_TERMINAL_NONE,
             "running");
    CHECK_EQ(XhciTolTerminal(0, 0, 0, 3, 3), XHCI_TOL_TERMINAL_NONE,
             "a spent run, the controller since recovered");
    CHECK_EQ(XhciTolTerminal(1, 0, 0, 0, 3), XHCI_TOL_TERMINAL_OWED,
             "failed, first recovery owed");
    CHECK_EQ(XhciTolTerminal(1, 0, 0, 2, 3), XHCI_TOL_TERMINAL_OWED,
             "two failed: one more owed");
    CHECK_EQ(XhciTolTerminal(1, 0, 0, 3, 3), XHCI_TOL_TERMINAL_FAILURES,
             "three failed in a row");
    CHECK_EQ(XhciTolTerminal(1, 0, 1, 0, 3), XHCI_TOL_TERMINAL_WINDOW,
             "the window refused");
    CHECK_EQ(XhciTolTerminal(1, 0, 1, 3, 3), XHCI_TOL_TERMINAL_WINDOW,
             "the window outranks the run");
    CHECK_EQ(XhciTolTerminal(1, 1, 0, 0, 3), XHCI_TOL_TERMINAL_UNREADABLE,
             "contained");
    CHECK_EQ(XhciTolTerminal(1, 1, 1, 3, 3), XHCI_TOL_TERMINAL_UNREADABLE,
             "the containment outranks both");
}

/* Record 17 section 4.11: a start latches the three values and sets every
 * piece of tolerance state explicitly, whatever the last lifetime left. */
static void test_start(void)
{
    static XHCI_TOL_STATE st;
    PUCHAR b = (PUCHAR)&st;
    ULONG i;

    for (i = 0; i < sizeof(st); i++) {
        b[i] = 0xA5;
    }
    XhciTolStart(&st, AMD_ID, 1, 0, 0, 0, 0, 0);
    CHECK_EQ(st.Stats.Tolerance, 0, "0 latched");
    CHECK_EQ(st.Stats.CapMode, XHCI_TOL_CAP_GATED, "cap default");
    CHECK_EQ(st.Stats.CapApplied, 1, "AMD: applies");
    CHECK_EQ(st.Stats.AvgTrbMode, 0, "avg default");
    CHECK_EQ(st.Stats.BackstopDrains, 0, "counters cleared");
    CHECK_EQ(st.Stats.Codes[255], 0, "histogram cleared");
    CHECK_EQ(st.Obs.Valid, 0, "observation cleared");
    CHECK_EQ(st.Window.Count, 0, "window cleared");
    CHECK_EQ(st.Dead.Armed, 0, "all-ones stamp cleared");
    CHECK_EQ(st.Unreadable, 0, "Unreadable cleared");
    CHECK_EQ(st.Clock, 0, "clock rearmed");
    CHECK_EQ(st.RootLoc[0].Hold, XHCI_TOL_HOLD_NONE, "first port released");
    CHECK_EQ(st.RootLoc[XHCI_TOL_ROOT_PORTS - 1].Reenums, 0, "last port");
    CHECK_EQ(st.RootLoc[XHCI_TOL_ROOT_PORTS - 1].Holds, 0, "last port holds");

    st.RootLoc[3].Hold = XHCI_TOL_HOLD_REPOWERS;
    st.Unreadable = 1;
    XhciTolStart(&st, INTEL_ID, 0, 0, 1, 2, 1, 1);
    CHECK_EQ(st.Stats.Tolerance, 1, "absent reads 1");
    CHECK_EQ(st.Stats.CapMode, XHCI_TOL_CAP_ALL, "cap 2");
    CHECK_EQ(st.Stats.CapApplied, 1, "2 applies on Intel");
    CHECK_EQ(st.Stats.AvgTrbMode, 1, "avg 1");
    CHECK_EQ(st.RootLoc[3].Hold, XHCI_TOL_HOLD_NONE, "unpowered hold ends");
    CHECK_EQ(st.Unreadable, 0, "Unreadable ends");

    XhciTolCountCode(&st.Stats, XHCI_CC_USB_TRANSACTION_ERROR);
    XhciTolCountCode(&st.Stats, 255);
    XhciTolCountCode(&st.Stats, 256);
    CHECK_EQ(st.Stats.Codes[4], 1, "code counted");
    CHECK_EQ(st.Stats.Codes[255], 1, "255 counted");
    CHECK_EQ((sizeof(XHCI_TOL_STATE) / sizeof(ULONG)) % 2, 0,
             "an even number of ULONGs");
}

int main(void)
{
    test_values();
    test_clock();
    test_cap();
    test_cap_fastpoll();
    test_retry();
    test_cycle();
    test_backstop();
    test_loc();
    test_port();
    test_window();
    test_dead();
    test_terminal();
    test_start();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
