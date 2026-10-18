/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, Yeo Kheng Meng
 *
 * p9lock_ut - host unit test of src/p9lock.h, the 9x DMA pool's boot-block
 * lock attempt and its per-callback stall budget (known issue K2 of
 * 0.3.0.0, PLAN 0.10 stall budget). The lock and the stall are mocked: the
 * lock stays held for a given number of attempts, every stall is added to
 * the callback's total. Built by tests\unit\build.cmd; the exit code is the
 * number of failed checks.
 */

#include <stdio.h>
#include "nvme2k.h"

static ULONG busyFor;           /* attempts that still find the lock held */
static ULONG attempts;
static ULONG stallUs;           /* total stalled in the current "callback" */
static ULONG maxStallCall;      /* largest single stall */

static ULONG MockCas(volatile ULONG *p, ULONG cmp, ULONG val)
{
    ULONG old = *p;

    attempts++;
    if (busyFor != 0) {
        busyFor--;
        return 0xFFFFFFFF;      /* held by someone else */
    }
    if (old == cmp) {
        *p = val;
    }
    return old;
}

static void MockStall(ULONG us)
{
    stallUs += us;
    if (us > maxStallCall) {
        maxStallCall = us;
    }
}

#define P9LOCK_CAS(p, cmp, val)     MockCas((p), (cmp), (val))
#define P9LOCK_STALL(us)            MockStall(us)
#include "p9lock.h"

static int failures;

static void check(int ok, const char *what)
{
    printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) {
        failures++;
    }
}

static void callback(void)
{
    attempts = 0;
    stallUs = 0;
    maxStallCall = 0;
}

int main(void)
{
    volatile ULONG lock;
    P9_STALLS b;
    ULONG i, got;

    printf("p9lock_ut\n");

    /* Runtime callback (NULL budget): exactly one attempt, never a stall. */
    callback();
    lock = 0;
    busyFor = 0;
    check(P9LockAttempt(&lock, 7, NULL) && lock == 7 && attempts == 1 && stallUs == 0,
          "runtime, lock free: taken in one attempt, 0 us");
    callback();
    lock = 0;
    busyFor = 1000;
    check(!P9LockAttempt(&lock, 7, NULL) && lock == 0 && attempts == 1 && stallUs == 0,
          "runtime, lock held: one attempt, 0 us, not taken");

    /* PlatDmaReachable: seven records turn OWNED in one callback, each one a
     * P9Note -> P9Work; with the lock held throughout, the callback stalls
     * 0 us (0.3.0.0: up to 7 x 200 us). */
    callback();
    busyFor = 1000;
    got = 0;
    for (i = 0; i < 7; i++) {
        lock = 0;
        got += P9LockAttempt(&lock, 7, NULL) ? 1 : 0;
    }
    check(got == 0 && attempts == 7 && stallUs == 0, "seven OWNED transitions, lock held: 7 attempts, 0 us");

    /* The relinquish (0.3.0.0: up to 9 x 100 us on top of EngStop's own
     * stalls): one attempt. */
    callback();
    lock = 0;
    busyFor = 1000;
    check(!P9LockAttempt(&lock, 7, NULL) && stallUs == 0, "relinquish, lock held: 0 us");

    /* A shared budget: every attempt in one callback draws on it, and the
     * total never exceeds it, however many locks the callback takes. */
    callback();
    P9StallsInit(&b, 9);
    busyFor = 1000;
    for (i = 0; i < 5; i++) {
        lock = 0;
        P9LockAttempt(&lock, 7, &b);
    }
    check(stallUs == 900 && b.Used == 9 && b.Left == 0 && maxStallCall <= 100 && attempts == 5 + 9,
          "budget 9, five locks, lock held: 900 us in total, 100 us per stall");

    /* Released after two failed attempts: taken, the rest of the budget left
     * for the callback's next lock. */
    callback();
    P9StallsInit(&b, 3);
    lock = 0;
    busyFor = 2;
    check(P9LockAttempt(&lock, 7, &b) && lock == 7 && stallUs == 200 && b.Left == 1,
          "budget 3, held for two attempts: taken after 200 us, 1 stall left");
    lock = 0;
    busyFor = 1000;
    check(!P9LockAttempt(&lock, 7, &b) && stallUs == 300 && b.Left == 0,
          "same callback, held: the last stall spent, 300 us in total");
    lock = 0;
    busyFor = 1000;
    check(!P9LockAttempt(&lock, 7, &b) && stallUs == 300, "same callback, budget spent: no further stall");

    /* An empty budget behaves as a runtime callback. */
    callback();
    P9StallsInit(&b, 0);
    lock = 0;
    busyFor = 1000;
    check(!P9LockAttempt(&lock, 7, &b) && attempts == 1 && stallUs == 0, "budget 0: one attempt, 0 us");

    /* HwFindAdapter: the pool shares the callback's budget with the clock
     * calibration, so the two together stay within ENG_STALL_BUDGET. */
    check(P9_FIND_STALLS + CLOCK_CAL_STALLS <= ENG_STALL_BUDGET &&
          (P9_FIND_STALLS + CLOCK_CAL_STALLS) * P9_LOCK_STALL_US < 1000,
          "HwFindAdapter: calibration + pool stalls under 1 ms");

    printf("%d failure(s)\n", failures);
    return failures;
}
