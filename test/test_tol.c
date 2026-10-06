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

    /* Exhaustion: the one route a spent TD takes, counted only. */
    CHECK_EQ(XhciTolRetryExhausted(1, XHCI_CC_USB_TRANSACTION_ERROR, 1, 3),
             1, "the fourth error exhausts");
    CHECK_EQ(XhciTolRetryExhausted(1, XHCI_CC_USB_TRANSACTION_ERROR, 1, 2),
             0, "the third does not");
    CHECK_EQ(XhciTolRetryExhausted(1, XHCI_CC_USB_TRANSACTION_ERROR, 0, 3),
             0, "not the head: not the retry's");
    CHECK_EQ(XhciTolRetryExhausted(0, XHCI_CC_USB_TRANSACTION_ERROR, 1, 3),
             0, "out of scope: not the retry's");
    CHECK_EQ(XhciTolRetryExhausted(1, XHCI_CC_STALL, 1, 3), 0,
             "a stall not");
    CHECK_EQ(XhciTolRetryExhausted(XhciTolRetryScope(0, AMD_ID, 1, 0, 0),
                                   XHCI_CC_USB_TRANSACTION_ERROR, 1, 3),
             0, "tolerance 0 counts no exhaustion");

    /* The thread's first decision: at tolerance 0 and 1. */
    CHECK_EQ(XhciTolRetryDecide(1, 0, 0, 1), XHCI_TOL_RETRY_RESET,
             "nothing pending: Reset Endpoint");
    CHECK_EQ(XhciTolRetryDecide(0, 0, 0, 1), XHCI_TOL_RETRY_REPLAY,
             "tolerance 0: today's path");
    CHECK_EQ(XhciTolRetryDecide(1, 1, 0, 1), XHCI_TOL_RETRY_REPLAY,
             "an operation pending: replay");
    CHECK_EQ(XhciTolRetryDecide(1, 0, 1, 1), XHCI_TOL_RETRY_REPLAY,
             "DrainPending: replay");
    CHECK_EQ(XhciTolRetryDecide(1, 0, 0, 0), XHCI_TOL_RETRY_REPLAY,
             "the head changed: replay, if anything is left");
    CHECK_EQ(XhciTolRetryDecide(1, 1, 1, 0), XHCI_TOL_RETRY_REPLAY,
             "all at once");

    /* The second, after the command. */
    CHECK_EQ(XhciTolRetryAfterReset(1, 7, 7), XHCI_TOL_RETRY_RING,
             "unchanged generation: ring and clear");
    CHECK_EQ(XhciTolRetryAfterReset(1, 8, 7), XHCI_TOL_RETRY_LEAVE,
             "a newer divert: leave its request");
    CHECK_EQ(XhciTolRetryAfterReset(1, 0, 0xFFFFFFFFUL), XHCI_TOL_RETRY_LEAVE,
             "across the generation's wrap");
    CHECK_EQ(XhciTolRetryAfterReset(0, 7, 7), XHCI_TOL_RETRY_FAULT,
             "a failed command: hcdCfgFault");
    CHECK_EQ(XhciTolRetryAfterReset(0, 8, 7), XHCI_TOL_RETRY_FAULT,
             "whatever the generation");
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

    /* The event path's one decision: (tolerance, claimed, unattributed,
     * code, isoch, hasDevice, pipeOpen). */
    CHECK_EQ(XhciTolCycleReason(1, 0, 0, 12, 0, 1, 0),
             XHCI_TOL_CYCLE_REFUSED_CODE,
             "Endpoint Not Enabled on a DCI with no pipe: refused, cycled");
    CHECK_EQ(XhciTolCycleReason(1, 0, 0, 12, 1, 1, 1), XHCI_TOL_CYCLE_NONE,
             "refused on an isochronous pipe: its own path");
    CHECK_EQ(XhciTolCycleReason(1, 0, 0, 12, 0, 0, 0), XHCI_TOL_CYCLE_NONE,
             "refused on a slot with no device: dropped");
    CHECK_EQ(XhciTolCycleReason(1, 1, 1, XHCI_CC_STALL, 0, 1, 1),
             XHCI_TOL_CYCLE_HALT_NO_TD,
             "a stall matching no TD (zero, off the ring, inside none)");
    /* A pointer above 4 GB is refused as Foreign before the queue reads
     * it (35-T.2): unattributed, on the open pipe its slot and DCI name. */
    CHECK_EQ(XhciTolCycleReason(1, 1, 1, XHCI_CC_USB_TRANSACTION_ERROR, 0, 1,
                                1),
             XHCI_TOL_CYCLE_HALT_NO_TD,
             "high-pointer Foreign Transaction Error: a halt candidate");
    CHECK_EQ(XhciTolCycleReason(1, 1, 1, XHCI_CC_SUCCESS, 0, 1, 1),
             XHCI_TOL_CYCLE_NONE, "high-pointer Foreign success: nothing");
    CHECK_EQ(XhciTolCycleReason(1, 1, 1, XHCI_CC_STALL, 1, 1, 1),
             XHCI_TOL_CYCLE_NONE, "high-pointer Foreign on isochronous: not");
    CHECK_EQ(XhciTolCycleReason(1, 1, 0, XHCI_CC_STALL, 0, 1, 1),
             XHCI_TOL_CYCLE_NONE, "a stall on a matched TD: the queue's");
    CHECK_EQ(XhciTolCycleReason(1, 1, 1, XHCI_CC_STALL, 0, 1, 0),
             XHCI_TOL_CYCLE_NONE, "no open pipe: no halt candidate");
    CHECK_EQ(XhciTolCycleReason(0, 0, 1, 12, 0, 1, 1), XHCI_TOL_CYCLE_NONE,
             "tolerance 0: nothing marked");
    CHECK_EQ(XhciTolCycleReason(0, 1, 1, XHCI_CC_STALL, 0, 1, 1),
             XHCI_TOL_CYCLE_NONE, "tolerance 0: no halt candidate");
}

/* 35-T.3/4: the mark the event path sets and the thread resolves, and what
 * the thread does with it (record 17 section 4.3). */
static void test_cycle_mark(void)
{
    XHCI_TOL_MARK m;

    memset(&m, 0, sizeof(m));
    CHECK_EQ(XhciTolMarkPending(&m), 0, "zeroed record: no mark");
    CHECK_EQ(XhciTolMarkResolve(&m, 0xFFFFFFFFUL), XHCI_TOL_CYCLE_NONE,
             "nothing marked resolves to none");

    /* A refused code cycles in any state: no context read decides it. */
    CHECK_EQ(XhciTolMarkSet(&m, XHCI_TOL_CYCLE_REFUSED_CODE, 1, 7), 1,
             "first mark raises it");
    CHECK_EQ(m.Gen, 7, "first mark stamps the generation");
    CHECK_EQ(XhciTolMarkSet(&m, XHCI_TOL_CYCLE_HALT_NO_TD, 3, 9), 0,
             "second mark: already marked");
    CHECK_EQ(m.Gen, 7, "a later mark keeps the first generation");
    CHECK_EQ(XhciTolMarkResolve(&m, 0), XHCI_TOL_CYCLE_REFUSED_CODE,
             "refused wins over a stale halt");
    CHECK_EQ(m.HaltDcis, 0, "the halt reading consumed");
    CHECK_EQ(XhciTolMarkPending(&m), 1, "still pending until acted on");
    XhciTolMarkInit(&m);
    CHECK_EQ(XhciTolMarkPending(&m), 0, "cleared");

    /* A halt with no TD: the context read decides. */
    CHECK_EQ(XhciTolMarkSet(&m, XHCI_TOL_CYCLE_HALT_NO_TD, 4, 2), 1, "halt");
    CHECK_EQ(m.HaltDcis, 1UL << 4, "DCI 4 to read");
    CHECK_EQ(XhciTolMarkResolve(&m, 0), XHCI_TOL_CYCLE_NONE,
             "stale: read Running or Stopped");
    CHECK_EQ(XhciTolMarkPending(&m), 0, "a stale halt clears the mark");
    CHECK_EQ(XhciTolMarkSet(&m, XHCI_TOL_CYCLE_HALT_NO_TD, 4, 2), 1, "again");
    CHECK_EQ(XhciTolMarkSet(&m, XHCI_TOL_CYCLE_HALT_NO_TD, 5, 2), 0, "and 5");
    CHECK_EQ(XhciTolMarkResolve(&m, 1UL << 6), XHCI_TOL_CYCLE_NONE,
             "a halted DCI that was not marked confirms nothing");
    CHECK_EQ(XhciTolMarkSet(&m, XHCI_TOL_CYCLE_HALT_NO_TD, 5, 2), 1, "5");
    CHECK_EQ(XhciTolMarkResolve(&m, 1UL << 5), XHCI_TOL_CYCLE_HALT_NO_TD,
             "Halted or Error: confirmed");
    CHECK_EQ(XhciTolMarkResolve(&m, 0), XHCI_TOL_CYCLE_HALT_NO_TD,
             "confirmed stays confirmed without a second read");
    XhciTolMarkInit(&m);
    CHECK_EQ(XhciTolMarkSet(&m, XHCI_TOL_CYCLE_HALT_NO_TD, 0, 1), 0,
             "DCI 0 is no endpoint");
    CHECK_EQ(XhciTolMarkSet(&m, XHCI_TOL_CYCLE_HALT_NO_TD, 32, 1), 0,
             "DCI 32 is no endpoint");
    CHECK_EQ(XhciTolMarkSet(&m, XHCI_TOL_CYCLE_PED, 1, 1), 0,
             "a PED reconnect is no mark");
    CHECK_EQ(XhciTolMarkPending(&m), 0, "none of them marked");

    CHECK_EQ(XhciTolCycleAct(XHCI_TOL_CYCLE_NONE, 1, 3, 3, 1),
             XHCI_TOL_CYCLE_ACT_NONE, "stale: nothing");
    CHECK_EQ(XhciTolCycleAct(XHCI_TOL_CYCLE_REFUSED_CODE, 1, 3, 3, 1),
             XHCI_TOL_CYCLE_ACT_PUBLISHED, "published: HcdEnumCycle");
    CHECK_EQ(XhciTolCycleAct(XHCI_TOL_CYCLE_HALT_NO_TD, 1, 3, 3, 0),
             XHCI_TOL_CYCLE_ACT_PRE_PDO, "not yet published: pre-PDO");
    CHECK_EQ(XhciTolCycleAct(XHCI_TOL_CYCLE_REFUSED_CODE, 0, 3, 3, 1),
             XHCI_TOL_CYCLE_ACT_DROP, "another device at the location");
    CHECK_EQ(XhciTolCycleAct(XHCI_TOL_CYCLE_REFUSED_CODE, 1, 3, 4, 1),
             XHCI_TOL_CYCLE_ACT_DROP, "a connect or disconnect since");
    CHECK_EQ(XhciTolCycleAct(XHCI_TOL_CYCLE_HALT_NO_TD, 1, 0xFFFFFFFFUL, 0,
                             0),
             XHCI_TOL_CYCLE_ACT_DROP, "generation wrapped: not equal");
    CHECK_EQ(XhciTolCycleAct(XHCI_TOL_CYCLE_PED, 1, 3, 3, 1),
             XHCI_TOL_CYCLE_ACT_NONE, "a PED reconnect is not this path's");

    CHECK_EQ(XhciTolCycleReconnect(1, 1, 1), 1, "charged, Empty, connected");
    CHECK_EQ(XhciTolCycleReconnect(0, 1, 1), 0, "refused: held");
    CHECK_EQ(XhciTolCycleReconnect(1, 0, 1), 0, "machine not Empty");
    CHECK_EQ(XhciTolCycleReconnect(1, 1, 0), 0, "no longer connected");
}

/*
 * The thread's wait on its own control transfer (35-T.3/4), as the driver
 * runs it: each look under the lock (XhciTolWaitStep), the wake event
 * cleared only in a look that saw nothing, one deadline across every wake.
 * The event path is modelled by what it sets under the same lock: Ep0Done,
 * the mark, and the event. `script` gives, per slice, what arrives during
 * it: 0 nothing, 1 a stale halt, 2 a confirmed halt, 3 the completion, 4 a
 * stale halt and then the completion in the same slice. flood: another
 * processor installs a fresh stale mark between every resolution and the
 * next look (the lock released in between), and each resolution takes a
 * slice of the clock. Returns the wait's end (XHCI_TOL_WAIT_DONE, _TIMEOUT
 * or _ABANDON) and the slices it took; 0xFFFFFFFF when it never ended.
 */
static ULONG wait_model(const ULONG *script, ULONG slices, ULONG deadline,
                        ULONG flood, PULONG taken)
{
    XHCI_TOL_MARK m;
    ULONG halted;
    ULONG reason;
    ULONG expired;
    ULONG done;
    ULONG step;
    ULONG now;
    ULONG guard;

    XhciTolMarkInit(&m);
    halted = 0;
    done = 0;
    now = 0;
    for (guard = 0; guard < 1000; guard++) {
        expired = now >= deadline;
        step = XhciTolWaitStep(done, XhciTolMarkPending(&m), expired);
        if (step == XHCI_TOL_WAIT_DONE || step == XHCI_TOL_WAIT_TIMEOUT) {
            *taken = now;
            return step;
        }
        if (step == XHCI_TOL_WAIT_RESOLVE) {
            /* The context read: DCI 2 Halted once the script made it so. */
            reason = XhciTolMarkResolve(&m, halted ? 1UL << 2 : 0);
            if (flood) {
                /* The slice the resolution took: a completion due in it
                 * lands, and so does the next stale mark. */
                if (now < slices && script[now] == 3) {
                    done = 1;
                }
                XhciTolMarkSet(&m, XHCI_TOL_CYCLE_HALT_NO_TD, 2, 0);
                now++;
            }
            step = XhciTolWaitResolved(reason, done, expired);
            if (step != XHCI_TOL_WAIT_AGAIN) {
                *taken = now;
                return step;
            }
            continue;
        }
        /* One slice: what the event path does in it, each under the lock
         * and each setting the wake event. */
        if (now < slices) {
            switch (script[now]) {
            case 1:
                XhciTolMarkSet(&m, XHCI_TOL_CYCLE_HALT_NO_TD, 2, 0);
                break;
            case 2:
                XhciTolMarkSet(&m, XHCI_TOL_CYCLE_HALT_NO_TD, 2, 0);
                halted = 1;
                break;
            case 3:
                done = 1;
                break;
            case 4:
                XhciTolMarkSet(&m, XHCI_TOL_CYCLE_HALT_NO_TD, 2, 0);
                done = 1;
                break;
            default:
                break;
            }
        }
        now++;
    }
    *taken = now;
    return 0xFFFFFFFFUL;
}

/*
 * A soft retry and a halt with no TD on one device (35-T.2 beside 35-T.3/4):
 * passes of the retry service, each preceded by a fresh T4 mark on the
 * retry's endpoint (DCI 3), the context read through XhciTolHaltOwner and
 * the pass decided once (XhciTolJoin). epState: the endpoint's state while
 * the retry is live; headDeferred: the queue's head is still the deferred
 * TD; refused: a refused code marked as well. Returns the pass (1-based) in
 * which something progressed - the retry consumed or a cycle charged - and
 * *what the decision; 0 when none did within `passes`.
 */
static ULONG join_model(ULONG epState, ULONG headDeferred, ULONG refused,
                        ULONG passes, PULONG what)
{
    XHCI_TOL_MARK m;
    XHCI_TOL_LOC loc;
    ULONG retryLive;
    ULONG owner;
    ULONG reason;
    ULONG pass;

    XhciTolMarkInit(&m);
    XhciTolLocInit(&loc);
    retryLive = 1;
    *what = XHCI_TOL_JOIN_NONE;
    for (pass = 1; pass <= passes; pass++) {
        XhciTolMarkSet(&m, XHCI_TOL_CYCLE_HALT_NO_TD, 3, 0);
        if (refused) {
            XhciTolMarkSet(&m, XHCI_TOL_CYCLE_REFUSED_CODE, 1, 0);
        }
        owner = XhciTolHaltOwner(retryLive ? epState : XHCI_EP_STATE_RUNNING,
                                 retryLive && headDeferred);
        reason = XhciTolMarkResolve(&m, owner == XHCI_TOL_HALT_CONFIRMED
                                            ? 1UL << 3 : 0);
        *what = XhciTolJoin(reason, retryLive);
        if (*what == XHCI_TOL_JOIN_RETRY) {
            retryLive = 0;
            return pass;
        }
        if (*what == XHCI_TOL_JOIN_CYCLE) {
            if (XhciTolLocCharge(&loc, XHCI_TOL_CHARGE_REENUM, pass)) {
                return pass;
            }
            return 0;
        }
    }
    return 0;
}

static void test_retry_join(void)
{
    ULONG what;

    CHECK_EQ(XhciTolHaltOwner(XHCI_EP_STATE_ERROR, 1),
             XHCI_TOL_HALT_CONFIRMED,
             "Error confirms even beside a live retry (no Reset Endpoint)");
    CHECK_EQ(XhciTolHaltOwner(XHCI_EP_STATE_ERROR, 0),
             XHCI_TOL_HALT_CONFIRMED, "Error confirms");
    CHECK_EQ(XhciTolHaltOwner(XHCI_EP_STATE_HALTED, 1), XHCI_TOL_HALT_RETRY,
             "Halted with the deferred TD at the head: the retry's");
    CHECK_EQ(XhciTolHaltOwner(XHCI_EP_STATE_HALTED, 0),
             XHCI_TOL_HALT_CONFIRMED,
             "Halted with a retry request that outlived its TD: confirmed");
    CHECK_EQ(XhciTolHaltOwner(XHCI_EP_STATE_RUNNING, 1), XHCI_TOL_HALT_STALE,
             "Running: stale");
    CHECK_EQ(XhciTolHaltOwner(XHCI_EP_STATE_STOPPED, 0), XHCI_TOL_HALT_STALE,
             "Stopped: stale");

    CHECK_EQ(XhciTolJoin(XHCI_TOL_CYCLE_NONE, 1), XHCI_TOL_JOIN_RETRY,
             "no reason, a live retry: the retry now");
    CHECK_EQ(XhciTolJoin(XHCI_TOL_CYCLE_HALT_NO_TD, 1), XHCI_TOL_JOIN_CYCLE,
             "a confirmed halt: the cycle this pass");
    CHECK_EQ(XhciTolJoin(XHCI_TOL_CYCLE_REFUSED_CODE, 1), XHCI_TOL_JOIN_CYCLE,
             "a refused code: the cycle this pass");
    CHECK_EQ(XhciTolJoin(XHCI_TOL_CYCLE_NONE, 0), XHCI_TOL_JOIN_NONE,
             "nothing owed");

    /* A fresh T4 mark before every pass never starves both recoveries. */
    CHECK_EQ(join_model(XHCI_EP_STATE_HALTED, 1, 0, 8, &what), 1,
             "explained Halted: progress in the first pass");
    CHECK_EQ(what, XHCI_TOL_JOIN_RETRY, "the retry consumed");
    CHECK_EQ(join_model(XHCI_EP_STATE_ERROR, 1, 0, 8, &what), 1,
             "Error beside the retry: progress in the first pass");
    CHECK_EQ(what, XHCI_TOL_JOIN_CYCLE, "a cycle charged, no Reset Endpoint");
    CHECK_EQ(join_model(XHCI_EP_STATE_HALTED, 0, 0, 8, &what), 1,
             "stale RetryWanted, a genuine halt: progress in the first pass");
    CHECK_EQ(what, XHCI_TOL_JOIN_CYCLE, "the halt cycled, not suppressed");
    CHECK_EQ(join_model(XHCI_EP_STATE_HALTED, 1, 1, 8, &what), 1,
             "a refused code beside the retry: progress in the first pass");
    CHECK_EQ(what, XHCI_TOL_JOIN_CYCLE, "the cycle");
}

static void test_cycle_wait(void)
{
    static const ULONG staleThenDone[] = { 1, 1, 1, 1, 1, 1, 1, 1, 3 };
    static const ULONG staleOnly[] = { 1, 1, 1, 1, 1, 1 };
    static const ULONG confirmed[] = { 0, 1, 2 };
    static const ULONG together[] = { 0, 4 };
    ULONG taken;

    CHECK_EQ(XhciTolWaitStep(0, 0, 0), XHCI_TOL_WAIT_SLEEP, "nothing: wait");
    CHECK_EQ(XhciTolWaitStep(1, 0, 0), XHCI_TOL_WAIT_DONE, "completed");
    CHECK_EQ(XhciTolWaitStep(1, 1, 1), XHCI_TOL_WAIT_DONE,
             "a completion wins over a mark and the deadline");
    CHECK_EQ(XhciTolWaitStep(0, 1, 0), XHCI_TOL_WAIT_RESOLVE, "a mark: read");
    CHECK_EQ(XhciTolWaitStep(0, 1, 1), XHCI_TOL_WAIT_RESOLVE,
             "a mark at the deadline is read before the timeout");
    CHECK_EQ(XhciTolWaitStep(0, 0, 1), XHCI_TOL_WAIT_TIMEOUT, "expired");

    CHECK_EQ(XhciTolWaitResolved(XHCI_TOL_CYCLE_HALT_NO_TD, 1, 1),
             XHCI_TOL_WAIT_ABANDON, "confirmed: abandoned");
    CHECK_EQ(XhciTolWaitResolved(XHCI_TOL_CYCLE_REFUSED_CODE, 0, 0),
             XHCI_TOL_WAIT_ABANDON, "refused code: abandoned");
    CHECK_EQ(XhciTolWaitResolved(XHCI_TOL_CYCLE_NONE, 1, 1),
             XHCI_TOL_WAIT_DONE, "stale, completed meanwhile: done");
    CHECK_EQ(XhciTolWaitResolved(XHCI_TOL_CYCLE_NONE, 0, 1),
             XHCI_TOL_WAIT_TIMEOUT,
             "stale at an expired deadline: the timeout now, no other look");
    CHECK_EQ(XhciTolWaitResolved(XHCI_TOL_CYCLE_NONE, 0, 0),
             XHCI_TOL_WAIT_AGAIN, "stale before the deadline: look again");

    /* Eight stale halts, more than any count of rewaits, then the
     * completion well inside the deadline: completed, no timeout. */
    CHECK_EQ(wait_model(staleThenDone, 9, 50, 0, &taken), XHCI_TOL_WAIT_DONE,
             "repeated stale events then timely completion: done");
    CHECK_EQ(taken, 9, "at the completion, not before");
    /* Stale halts alone: the wait runs to its one deadline, no sooner. */
    CHECK_EQ(wait_model(staleOnly, 6, 50, 0, &taken), XHCI_TOL_WAIT_TIMEOUT,
             "stale events alone: the deadline");
    CHECK_EQ(taken, 50, "the deadline, not shortened by the stale events");
    CHECK_EQ(wait_model(confirmed, 3, 50, 0, &taken), XHCI_TOL_WAIT_ABANDON,
             "a confirmed halt abandons the wait");
    CHECK_EQ(taken, 3, "at the look after it arrived");
    CHECK_EQ(wait_model(together, 2, 50, 0, &taken), XHCI_TOL_WAIT_DONE,
             "a stale mark and the completion in one slice: done");
    /* A mark that lands in the last slice before the deadline is read,
     * never left behind a timeout. */
    {
        static const ULONG late[] = { 0, 0, 0, 0, 2 };
        CHECK_EQ(wait_model(late, 5, 5, 0, &taken), XHCI_TOL_WAIT_ABANDON,
                 "a mark arriving as the deadline passes: read, abandoned");
    }
    /* Replacement stale marks installed between every resolution and the
     * next look: a mark is pending at every look, yet the wait still ends
     * at its deadline, decided after the resolution that found it passed. */
    CHECK_EQ(wait_model(staleOnly, 1, 20, 1, &taken), XHCI_TOL_WAIT_TIMEOUT,
             "a flood of stale marks: the deadline still ends the wait");
    CHECK_EQ(taken, 21, "the resolution after the deadline, not later");
    /* The same flood with the completion arriving first: done. */
    {
        static const ULONG floodDone[] = { 1, 3 };
        CHECK_EQ(wait_model(floodDone, 2, 20, 1, &taken),
                 XHCI_TOL_WAIT_DONE, "a flood of stale marks, then done");
    }
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

    /* An external hub's port repowered after an over-current (hcd_hub.c,
     * hcd_sshub.c), its budget spent: the repower is marked, so a device
     * that stays away past the stable-disconnect interval before it comes
     * back neither re-arms nor releases the powered hold. */
    for (i = 0; i <= XHCI_TOL_REENUMS; i++) {
        (VOID)XhciTolLocCharge(&l, XHCI_TOL_CHARGE_REENUM, i);
    }
    CHECK_EQ(l.Hold, XHCI_TOL_HOLD_REENUMS, "spent, held powered");
    CHECK_EQ(XhciTolLocObserve(&l, 0, 0, 0, 10), 0, "power lost");
    XhciTolLocRecovery(&l);
    CHECK_EQ(XhciTolLocObserve(&l, 0, 1, 0, 20), 0, "repowered, away");
    CHECK_EQ(XhciTolLocDiscDue(&l, 20 + XHCI_TOL_STABLE_DISC_TICKS), 0,
             "no look owed");
    CHECK_EQ(XhciTolLocObserve(&l, 0, 1, 0, 20 + XHCI_TOL_STABLE_DISC_TICKS),
             0, "away past the interval");
    CHECK_EQ(XhciTolLocObserve(&l, 1, 1, 0, 30 + XHCI_TOL_STABLE_DISC_TICKS),
             0, "back: no re-arm");
    CHECK_EQ(l.Hold, XHCI_TOL_HOLD_REENUMS, "still held");
    CHECK_EQ(l.Reenums, XHCI_TOL_REENUMS, "still spent");
    XhciTolLocInit(&l);

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

/* A slot-fatal teardown's re-enumeration (hcd_enum.c, hcdSlotFatalService)
 * is charged at XhciTolerance 0 too: the location is observed and its hold
 * enforced only once charged, the budget's boundary holds it, a stable
 * disconnect re-arms it, and a start releases it. */
static void test_loc_off(void)
{
    XHCI_TOL_LOC l;
    ULONG i;

    CHECK_EQ(XhciTolLocActive(0, NULL), 0, "NULL inactive");
    CHECK_EQ(XhciTolLocActive(1, NULL), 0, "NULL inactive with tolerance");
    CHECK_EQ(XhciTolLocHeld(1, NULL), 0, "NULL not held");
    XhciTolLocInit(&l);
    CHECK_EQ(XhciTolLocActive(1, &l), 1, "tolerance on: always active");
    CHECK_EQ(XhciTolLocActive(0, &l), 0, "off, never charged: inactive");
    CHECK_EQ(XhciTolLocHeld(0, &l), 0, "off, never charged: not held");

    for (i = 0; i < XHCI_TOL_REENUMS; i++) {
        CHECK_EQ(XhciTolLocCharge(&l, XHCI_TOL_CHARGE_REENUM, 10 + i), 1,
                 "off: a teardown within the budget re-enumerates");
        CHECK_EQ(XhciTolLocActive(0, &l), 1, "off: charged is active");
        CHECK_EQ(XhciTolLocHeld(0, &l), 0, "off: within budget, not held");
    }
    CHECK_EQ(XhciTolLocCharge(&l, XHCI_TOL_CHARGE_REENUM, 20), 0,
             "off: the budget's last plus one is refused");
    CHECK_EQ(XhciTolLocHeld(0, &l), 1, "off: refused holds the location");
    CHECK_EQ(XhciTolLocHeld(1, &l), 1, "the same hold with tolerance on");
    CHECK_EQ(XhciTolLocUnpowered(&l), 0, "a powered hold");

    /* Stable disconnect at its boundary, then a connection. */
    CHECK_EQ(XhciTolLocObserve(&l, 0, 1, 0, 100), 0, "off: disconnect");
    CHECK_EQ(XhciTolLocObserve(&l, 0, 1, 0,
                               100 + XHCI_TOL_STABLE_DISC_TICKS - 1),
             0, "off: one short");
    CHECK_EQ(XhciTolLocObserve(&l, 1, 1, 0,
                               100 + XHCI_TOL_STABLE_DISC_TICKS - 1),
             0, "off: back too soon");
    CHECK_EQ(XhciTolLocHeld(0, &l), 1, "off: still held");
    CHECK_EQ(XhciTolLocObserve(&l, 0, 1, 0, 300), 0, "off: away again");
    CHECK_EQ(XhciTolLocObserve(&l, 0, 1, 0, 300 + XHCI_TOL_STABLE_DISC_TICKS),
             0, "off: stable");
    CHECK_EQ(XhciTolLocObserve(&l, 1, 1, 0, 400), 1, "off: connect re-arms");
    CHECK_EQ(XhciTolLocHeld(0, &l), 0, "off: released");
    CHECK_EQ(XhciTolLocActive(0, &l), 0,
             "off: re-armed, inactive again as if never charged");

    /* Held, then a controller start. */
    for (i = 0; i <= XHCI_TOL_REENUMS; i++) {
        (VOID)XhciTolLocCharge(&l, XHCI_TOL_CHARGE_REENUM, 500 + i);
    }
    CHECK_EQ(XhciTolLocHeld(0, &l), 1, "off: spent again");
    XhciTolLocInit(&l);
    CHECK_EQ(XhciTolLocHeld(0, &l), 0, "a start releases");
    CHECK_EQ(XhciTolLocActive(0, &l), 0, "and leaves it inactive at 0");
}

/* One charge per pending cycle: a RESET_PORT fallback (hcd_cfg.c,
 * hcdCfgCycle) and a slot-fatal teardown (hcdSlotFatalService) asking for
 * the same device's cycle at one connect generation charge once, so two
 * earlier cycles still leave the third permitted re-enumeration. */
static void test_cycle_charge(void)
{
    XHCI_TOL_CYCLE_CHARGE once;
    XHCI_TOL_LOC l;

    XhciTolLocInit(&l);
    XhciTolCycleChargeInit(&once);
    CHECK_EQ(XhciTolCycleCharge(NULL, &l, 0, 0), 0, "NULL once");
    CHECK_EQ(XhciTolCycleCharge(&once, NULL, 0, 0), 0, "NULL location");
    CHECK_EQ(once.Valid, 0, "nothing recorded on a refusal of the call");

    /* Two earlier cycles, each its own generation. */
    CHECK_EQ(XhciTolCycleCharge(&once, &l, 1, 10), 1, "first cycle");
    CHECK_EQ(XhciTolCycleCharge(&once, &l, 3, 20), 1, "second cycle");
    CHECK_EQ(l.Reenums, 2, "two charged");

    /* The third: a fallback, then a slot-fatal request, one generation. */
    CHECK_EQ(XhciTolCycleCharge(&once, &l, 5, 30), 1, "fallback charges");
    CHECK_EQ(XhciTolCycleCharge(&once, &l, 5, 31), 1,
             "slot-fatal at the same generation shares it");
    CHECK_EQ(l.Reenums, 3, "charged once, not twice");
    CHECK_EQ(l.Hold, XHCI_TOL_HOLD_NONE, "the third is permitted, not held");
    CHECK_EQ(l.Charges, 3, "the dump's total counts one");

    /* The cycle ran (the generation moved): the next asks afresh, and is
     * the budget's fourth. */
    CHECK_EQ(XhciTolCycleCharge(&once, &l, 7, 40), 0, "fourth refused");
    CHECK_EQ(l.Hold, XHCI_TOL_HOLD_REENUMS, "held");
    CHECK_EQ(XhciTolCycleCharge(&once, &l, 7, 41), 0,
             "a second producer of it reads the same refusal");
    CHECK_EQ(l.Holds, 1, "held once");

    /* A start forgets the charge. */
    XhciTolCycleChargeInit(&once);
    XhciTolLocInit(&l);
    CHECK_EQ(XhciTolCycleCharge(&once, &l, 7, 50), 1,
             "after a start the same generation charges again");
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

/* A recovery owed when the window stops decoding - a command timed out on
 * it - is deferred, so the episode keeps its stamp and the controller is
 * contained; each pass is the thread's order, recovery then containment,
 * and a recovery begun is a new start generation. */
static void test_dead_recover(void)
{
    XHCI_TOL_DEAD d;
    ULONG c = XHCI_TOL_CONTAIN_TICKS;
    ULONG gen = 7;
    ULONG now;
    ULONG contained = 0;

    CHECK_EQ(XhciTolRecoverDefer(1, 1, 1), 1, "all-ones defers");
    CHECK_EQ(XhciTolRecoverDefer(1, 1, 0), 0, "a good read recovers");
    CHECK_EQ(XhciTolRecoverDefer(1, 0, 1), 0, "not admitted recovers");
    CHECK_EQ(XhciTolRecoverDefer(0, 1, 1), 0, "tolerance 0 recovers");

    d.Armed = 0;
    for (now = 100; now <= 100 + c && !contained; now++) {
        if (!XhciTolRecoverDefer(1, 1, 1)) {
            gen++;
        }
        contained = XhciTolDeadStep(&d, 1, 1, 1, gen, now);
    }
    CHECK_EQ(contained, 1, "contained with a recovery owed");
    CHECK_EQ(gen, 7, "no recovery begun");

    /* What the deferral prevents: a recovery each pass restamps. */
    d.Armed = 0;
    contained = 0;
    for (now = 100; now <= 100 + c + c; now++) {
        gen++;
        contained |= XhciTolDeadStep(&d, 1, 1, 1, gen, now);
    }
    CHECK_EQ(contained, 0, "never contained without it");
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
    /* The unproven-DMA containment shares the latch, not the reason. */
    CHECK_EQ(XhciTolTerminal(1, XHCI_TOL_CONTAINED_UNREADABLE, 0, 0, 3),
             XHCI_TOL_TERMINAL_UNREADABLE, "all ones is unreadable");
    CHECK_EQ(XhciTolTerminal(1, XHCI_TOL_CONTAINED_DMA_UNPROVEN, 0, 0, 3),
             XHCI_TOL_TERMINAL_DMA_UNPROVEN,
             "a readable controller not proven stopped is its own reason");
    CHECK_EQ(XhciTolTerminal(1, XHCI_TOL_CONTAINED_DMA_UNPROVEN, 1, 3, 3),
             XHCI_TOL_TERMINAL_DMA_UNPROVEN,
             "and it outranks the window and the run of failures");
    CHECK_EQ(XhciTolTerminal(0, XHCI_TOL_CONTAINED_DMA_UNPROVEN, 0, 0, 3),
             XHCI_TOL_TERMINAL_DMA_UNPROVEN, "whatever the failure word");
    CHECK_EQ(XHCI_TOL_TERMINAL_DMA_UNPROVEN, 5, "the dump's value");
}

/* 35-V: a terminal no recovery acts on completes what the halted controller
 * holds - the invalidation raised once, with HCH's evidence. */
static void test_terminal_release(void)
{
    XHCI_TOL_WINDOW w;
    ULONG t;
    ULONG i;

    /* The VM leg: three recoveries began, the fourth HCH refused. */
    XhciTolWindowInit(&w);
    for (i = 0; i < XHCI_TOL_RECOVERIES; i++) {
        CHECK_EQ(XhciTolWindowAdmit(&w, 1, 100 + i), 1, "began");
    }
    CHECK_EQ(XhciTolWindowAdmit(&w, 1, 110), 0, "the fourth refused");
    t = XhciTolTerminal(1, 0, w.Refused, 0, 3);
    CHECK_EQ(t, XHCI_TOL_TERMINAL_WINDOW, "window terminal");
    CHECK_EQ(XhciTolTerminalRelease(t, 0, 0, 1), 1,
             "devices held, nothing pending: raised");
    CHECK_EQ(XhciTolTerminalRelease(t, 0, 1, 1), 0, "once per lifetime");
    CHECK_EQ(XhciTolTerminalRelease(t, 1, 0, 1), 0,
             "an invalidation already pending drains them");
    CHECK_EQ(XhciTolTerminalRelease(t, 0, 0, 0), 0, "nothing left to drop");

    /* Tolerance 0: the window never refuses; the run of failures is
     * 2.1.1.0's terminal and is released the same way when no attempt
     * reached its own invalidation (each refused with CNR up). */
    XhciTolWindowInit(&w);
    for (i = 0; i < 10; i++) {
        CHECK_EQ(XhciTolWindowAdmit(&w, 0, 100 + i), 1, "off: admitted");
    }
    t = XhciTolTerminal(1, 0, w.Refused, 3, 3);
    CHECK_EQ(t, XHCI_TOL_TERMINAL_FAILURES, "off: the run's terminal");
    CHECK_EQ(XhciTolTerminalRelease(t, 0, 0, 1), 1, "failures: raised");
    CHECK_EQ(XhciTolTerminalRelease(t, 0, 0, 0), 0,
             "failures: attempts already dropped every device");

    /* Not a terminal of this kind. */
    CHECK_EQ(XhciTolTerminalRelease(XHCI_TOL_TERMINAL_NONE, 0, 0, 1), 0,
             "running");
    CHECK_EQ(XhciTolTerminalRelease(XhciTolTerminal(1, 0, 0, 2, 3), 0, 0, 1),
             0, "a recovery still owed resets it");
    CHECK_EQ(XhciTolTerminalRelease(XHCI_TOL_TERMINAL_UNREADABLE, 0, 0, 1), 0,
             "the all-ones containment drains on its own");
    CHECK_EQ(XhciTolTerminalRelease(XHCI_TOL_TERMINAL_DMA_UNPROVEN, 0, 0, 1),
             0, "so does the unproven one");

    /* The evidence. */
    CHECK_EQ(XhciTolHaltProven(0x00000001UL), 1, "HCH set");
    CHECK_EQ(XhciTolHaltProven(0x00001001UL), 1, "HCH with HCE");
    CHECK_EQ(XhciTolHaltProven(0x00000000UL), 0, "running");
    CHECK_EQ(XhciTolHaltProven(0x00001000UL), 0, "HCE, not halted");
    CHECK_EQ(XhciTolHaltProven(0xFFFFFFFFUL), 0, "all-ones proves nothing");
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
    test_cycle_mark();
    test_cycle_wait();
    test_retry_join();
    test_backstop();
    test_loc();
    test_loc_off();
    test_cycle_charge();
    test_port();
    test_window();
    test_dead();
    test_dead_recover();
    test_terminal();
    test_terminal_release();
    test_start();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
