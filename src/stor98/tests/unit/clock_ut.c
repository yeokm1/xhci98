/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, Yeo Kheng Meng
 *
 * clock_ut - host unit test of the OS-clock cross-check in src/clock.c
 * (ClockTick) under the polling mode's timer periods (PLAN "Polling mode",
 * "Clock"): main's rule, the NT floor, the credit cap, gone deadlines.
 * Built against the 9x headers (the 9x OS clock is VMM Get_System_Time,
 * mocked here; CLK_OS_RES_MS = 55 ms), with
 * a simulated TSC and stall. Each case runs timer callbacks with a nominal
 * period and a real delivery interval and checks the OS clock's state, the
 * TSC rate and that the trustworthy elapsed time (the smaller of the TSC and
 * safe estimates, the safe one less its resolution, as LcTrustMs) is never
 * ahead of real time. The exit code is the number of failed checks.
 */

#include <stdio.h>
#include "nvme2k.h"

/* simulated machine */
static double gRealUs;          /* real time */
static double gTscPerUs;        /* true TSC rate */
static double gStallScale;      /* real us per requested stall us (calibration error) */
static int gOsMode;             /* 0 healthy (55 ms steps), 1 frozen, 2 fast x gOsFast */
static double gOsFast;
static ULONG gOsFrozenAt;

ULONG _cdecl Get_System_Time(VOID)
{
    ULONG ms = (ULONG)(gRealUs / 1000.0);
    switch (gOsMode) {
    case 1: return gOsFrozenAt;
    case 2: return (ULONG)(gRealUs * gOsFast / 1000.0);
    default: return ms - ms % 55;   /* advances once per 55 ms timer interrupt */
    }
}

VOID ClockReadTsc(OUT PULONG Lo, OUT PULONG Hi)
{
    unsigned __int64 t = (unsigned __int64)(gRealUs * gTscPerUs);
    *Lo = (ULONG)t;
    *Hi = (ULONG)(t >> 32);
}

static VOID MockStall(ULONG Us)
{
    gRealUs += Us * gStallScale;
}

#define ScsiPortStallExecution(us) MockStall(us)
#include "../../src/clock.c"

static int failures;
static void check(int ok, const char *what)
{
    printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) failures++;
}

static ENG_CLOCK c;

static void setup(double stallScale)
{
    memset(&c, 0, sizeof(c));
    gRealUs = 5e6;
    gTscPerUs = 2000.0;         /* 2 GHz */
    gStallScale = stallScale;
    gOsMode = 0;
    ClockCalibrate(&c);
}

/* n callbacks of nominal period periodUs, each delivered after deliverUs real */
static void run(ULONG n, ULONG periodUs, double deliverUs)
{
    ULONG i;
    for (i = 0; i < n; i++) {
        gRealUs += deliverUs;
        ClockTick(&c, periodUs);
    }
}

/*
 * n idle-to-busy transitions of the polling mode (NT: idle period 1 s,
 * busy 10 ms): the 1 s idle tick is armed, a request 900 ms later replaces
 * it with a 10 ms tick, delivered 15.6 ms later. The 900 ms spent on the
 * replaced period is never credited as nominal time. discard: what
 * EngTimerEnsure does at such a replacement (ClockTimerStopped).
 */
static void replaced(ULONG n, int discard)
{
    ULONG i;
    for (i = 0; i < n; i++) {
        gRealUs += 900000.0;
        if (discard) {
            ClockTimerStopped(&c);
        }
        run(1, 10000, 15600.0);
    }
}

/* LcTrustMs over [t0, now]: never more than real elapsed */
static ULONG trust(ULONG tsc0, ULONG safe0)
{
    ULONG tsc = ClockNow(&c) - tsc0;
    ULONG safe = ClockSafeMs(&c) - safe0;
    ULONG res = ClockResolutionMs(&c);
    safe = safe > res ? safe - res : 0;
    return tsc < safe ? tsc : safe;
}

/*
 * A gone deadline measured as LcTrustMs does, stamped at entry into gone
 * (now), with the gone state's watchdog tick (100 ms nominal, delivered
 * every deliverUs): the real ms until the trustworthy estimate reaches
 * limitMs, checking at every tick that it is never ahead of real time
 * (beyond the TSC rate's quantisation, 500-1000 units per ms: 0.2 %).
 */
static int gEarly;
static ULONG deadline(ULONG limitMs, double deliverUs)
{
    ULONG t0 = ClockNow(&c), s0 = ClockSafeMs(&c), ms, real;
    double r0 = gRealUs;
    for (;;) {
        gRealUs += deliverUs;
        ClockTick(&c, 100000);
        ms = trust(t0, s0);
        real = (ULONG)((gRealUs - r0) / 1000.0);
        if (ms > (ULONG)(real * 1.002) + 1) gEarly++;
        if (ms >= limitMs || real > 600000) return real;
    }
}

int main(void)
{
    ULONG ms, rate;

    printf("1. 9x, polling at 1 ms delivered every 1.1 ms (as measured on 98 SE), healthy OS clock\n");
    setup(1.0);
    run(20000, 1000, 1100.0);
    check(c.OsState == CLK_OS_QUALIFIED, "qualified");
    gEarly = 0; ms = deadline(5000, 100000.0);
    check(gEarly == 0 && ms >= 5000 && ms <= 5200, "gone deadline near 5 real s");

    printf("2. 9x, 1 ms delivered every 55 ms (a coarse VMM timer): main's rule rejects the OS clock\n");
    setup(1.0);
    run(2200, 1000, 55000.0);
    check(c.OsState == CLK_OS_REJECTED, "rejected (as main does for any window far ahead)");
    gEarly = 0; ms = deadline(5000, 100000.0);
    printf("     gone deadline 5 s with 100 ms ticks on time: %lu real ms\n", ms);
    check(gEarly == 0 && ms >= 5000 && ms <= 6200, "never early, at most one slow tick late");
    gEarly = 0; ms = deadline(5000, 300000.0);
    printf("     gone deadline 5 s with 100 ms ticks 3x late: %lu real ms\n", ms);
    check(gEarly == 0 && ms >= 5000 && ms <= 18000, "never early, late by the ticks' lateness");

    printf("3. short ticks with the OS clock frozen\n");
    setup(1.0);
    gOsFrozenAt = Get_System_Time();    /* healthy reading now, then frozen */
    gOsMode = 1;
    run(4000, 1000, 1100.0);
    check(c.OsState == CLK_OS_REJECTED, "frozen OS clock rejected");
    gEarly = 0; ms = deadline(5000, 100000.0);
    check(gEarly == 0 && ms >= 5000 && ms <= 7000, "gone deadline on the nominal ticks: never early, not stalled");

    printf("4. short ticks, OS clock 8x fast, TSC calibrated 1.75x fast (0.57x stalls)\n");
    setup(0.57);
    rate = c.UnitsPerMs;
    gOsMode = 2; gOsFast = 8.0; c.WinBroken = TRUE;
    run(4000, 1000, 1000.0);
    check(c.OsState == CLK_OS_REJECTED, "fast OS clock rejected");
    check(c.UnitsPerMs == rate, "TSC not re-calibrated against it");

    printf("5. NT: the 10 ms floor delivered every 15.6 ms, TSC calibrated 2x slow\n");
    setup(2.0);
    rate = c.UnitsPerMs;
    run(600, 10000, 15600.0);
    check(c.OsState == CLK_OS_QUALIFIED, "qualified");
    check(c.UnitsPerMs * 20 > rate * 9 && c.UnitsPerMs * 20 < rate * 11, "TSC re-calibrated to the true rate");
    gEarly = 0; ms = deadline(5000, 100000.0);
    check(gEarly == 0 && ms >= 5000 && ms <= 5200, "gone deadline near 5 real s");

    printf("6. NT: 1 ms periods would be delivered every 15.6 ms (why NT has the 10 ms floor)\n");
    setup(1.0);
    run(2200, 1000, 15600.0);
    check(c.OsState == CLK_OS_REJECTED, "a healthy clock is rejected: hence the floor in LcPollBusyUs");

    printf("7. stopped timer between short and coarse ticks\n");
    setup(1.0);
    run(1000, 1000, 1100.0);
    ClockTimerStopped(&c);
    gRealUs += 30e6;
    run(60, 100000, 100000.0);
    check(c.OsState == CLK_OS_QUALIFIED, "after a stop, coarse windows qualify the healthy clock");

    printf("8. the polling idle period: a 60 s tick credits at most 1 s, and does not reject a healthy clock\n");
    setup(1.0);
    run(3, 60000000, 60000000.0);
    check(c.OsState == CLK_OS_QUALIFIED, "60 s windows qualify a healthy clock");
    setup(1.0);
    gOsFrozenAt = Get_System_Time(); gOsMode = 1;
    run(60, 100000, 100000.0);
    check(c.OsState == CLK_OS_REJECTED, "rejected (frozen)");
    {
        ULONG s0 = ClockSafeMs(&c);
        gRealUs += 60e6; ClockTick(&c, 60000000);
        check(ClockSafeMs(&c) - s0 <= CLK_NOMINAL_RES_MS, "safe clock credit capped at the slow tick");
    }

    printf("9. a long armed period replaced by a short one at each request (polling idle 1 s -> 10 ms)\n");
    setup(1.0);
    replaced(250, 0);
    check(c.OsState == CLK_OS_REJECTED, "without the window discard a healthy clock is rejected");
    setup(1.0);
    replaced(250, 1);
    check(c.OsState != CLK_OS_REJECTED, "with the window discarded at each replacement it is kept");
    run(3, 1000000, 1000000.0);
    check(c.OsState == CLK_OS_QUALIFIED, "a quiet spell of idle ticks then qualifies it");
    gEarly = 0; ms = deadline(5000, 100000.0);
    check(gEarly == 0 && ms >= 5000 && ms <= 5200, "gone deadline near 5 real s");
    setup(1.0);
    gOsFrozenAt = Get_System_Time(); gOsMode = 1;
    replaced(250, 1);
    run(30, 100000, 100000.0);
    check(c.OsState == CLK_OS_REJECTED, "a frozen clock is still rejected once the timer runs a window");

    printf("%s: %d failure(s)\n", failures ? "FAILED" : "passed", failures);
    return failures;
}
