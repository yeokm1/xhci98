// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright (c) 2026, Yeo Kheng Meng
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
// 3. Neither the name of the copyright holder nor the names of its
//    contributors may be used to endorse or promote products derived from
//    this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// Elapsed-time clock; see clock.h.
//

#include "nvme2k.h"

#if defined(CLOCK_HOST_TEST)
// tests/unit/clock_ut.c supplies ClockReadTsc (a simulated counter).
#elif defined(_M_AMD64) || defined(_AMD64_)
unsigned __int64 __rdtsc(void);
#pragma intrinsic(__rdtsc)

VOID ClockReadTsc(OUT PULONG Lo, OUT PULONG Hi)
{
    unsigned __int64 t = __rdtsc();

    *Lo = (ULONG)t;
    *Hi = (ULONG)(t >> 32);
}
#else
VOID ClockReadTsc(OUT PULONG Lo, OUT PULONG Hi)
{
    ULONG lo, hi;

    __asm {
        _emit 0x0F
        _emit 0x31
        mov lo, eax
        mov hi, edx
    }
    *Lo = lo;
    *Hi = hi;
}
#endif

//
// OS clock (see clock.h). Returns milliseconds, monotonic for the session.
//
#ifdef NVME2K_W9X
ULONG _cdecl Get_System_Time(VOID);     // utils.c: VMM service, ms since Windows started

static ULONG ClockOsMs(IN OUT PENG_CLOCK Clock)
{
    Clock->OsMs = Get_System_Time();
    return Clock->OsMs;
}
#else
// KUSER_SHARED_DATA (ntddk.h): mapped at this fixed kernel address on every
// NT version, never paged; InterruptTime is the KSYSTEM_TIME at offset 8,
// 100 ns units since boot, written by the clock interrupt as LowPart with
// High1Time and High2Time on either side (read High1, Low, High2 and retry
// until the two high parts agree).
#if defined(_M_AMD64) || defined(_AMD64_)
#define CLK_SHARED_DATA     ((volatile UCHAR *)0xFFFFF78000000000ULL)
#else
#define CLK_SHARED_DATA     ((volatile UCHAR *)0xFFDF0000)
#endif
#define CLK_INTERRUPT_TIME  0x08

static VOID ClockReadInterruptTime(OUT PULONG Lo, OUT PULONG Hi)
{
    volatile LONG *t = (volatile LONG *)(CLK_SHARED_DATA + CLK_INTERRUPT_TIME);
    LONG h1, h2;
    ULONG lo;

    do {
        h1 = t[1];          // High1Time
        lo = (ULONG)t[0];   // LowPart
        h2 = t[2];          // High2Time
    } while (h1 != h2);
    *Lo = lo;
    *Hi = (ULONG)h1;
}

static ULONG ClockOsMs(IN OUT PENG_CLOCK Clock)
{
    ULONG lo, hi, dlo, dhi, d;

    ClockReadInterruptTime(&lo, &hi);
    // 64-bit difference from the last read, in 32-bit halves.
    dlo = lo - Clock->OsLastLo;
    dhi = hi - Clock->OsLastHi - (lo < Clock->OsLastLo ? 1 : 0);
    Clock->OsLastLo = lo;
    Clock->OsLastHi = hi;
    if (dhi != 0) {
        // More than 429 s since the last read (or the first read): nothing
        // is counted for the gap, so the clock can only run slow.
        Clock->OsRem = 0;
        return Clock->OsMs;
    }
    d = dlo;
    if (d > 0xFFFFFFFF - Clock->OsRem) {
        d = 0xFFFFFFFF - Clock->OsRem;
    }
    d += Clock->OsRem;
    Clock->OsMs += d / 10000;
    Clock->OsRem = d % 10000;
    return Clock->OsMs;
}
#endif


// TSC units (2^Shift ticks) from raw (Lo0, Hi0) to raw (Lo1, Hi1), in 32-bit
// halves (no 64-bit helper routines). Returns CLK_SPAN_OK with *Units set,
// CLK_SPAN_LONG if the span does not fit 32 bits of units, or
// CLK_SPAN_BACK if the counter went backwards (TSCs of two processors that
// are not in step).
#define CLK_SPAN_OK         0
#define CLK_SPAN_LONG       1
#define CLK_SPAN_BACK       2

static ULONG ClockSpan(IN PENG_CLOCK Clock, IN ULONG Lo0, IN ULONG Hi0, IN ULONG Lo1, IN ULONG Hi1,
                       OUT PULONG Units)
{
    ULONG dlo = Lo1 - Lo0;
    ULONG dhi = Hi1 - Hi0 - (Lo1 < Lo0 ? 1 : 0);

    *Units = 0;
    if (dhi & 0x80000000UL) {
        return CLK_SPAN_BACK;
    }
    if (Clock->Shift == 0) {
        if (dhi != 0) {
            return CLK_SPAN_LONG;
        }
        *Units = dlo;
        return CLK_SPAN_OK;
    }
    if ((dhi >> Clock->Shift) != 0) {
        return CLK_SPAN_LONG;
    }
    *Units = (dlo >> Clock->Shift) | (dhi << (32 - Clock->Shift));
    return CLK_SPAN_OK;
}

VOID ClockCalibrate(IN OUT PENG_CLOCK Clock)
{
    ULONG lo0, hi0, lo1, hi1, d, shift, i;

    // OS clock: on trial again for this session (a resume re-runs
    // HwFindAdapter on the same extension); the first read sets the base.
    Clock->OsState = CLK_OS_TRIAL;
    ClockOsMs(Clock);
    Clock->SafeOsBase = ClockOsMs(Clock);
    DBGSTAGE(STG_CLOCK_OS, CLK_OS_TRIAL | ((Clock->OsMs & 0xFFFFFF) << 8));

    // 9 stalls of 100 us: within the stall budget of PLAN 0.10 (100 us per
    // call, under 1 ms per callback).
    ClockReadTsc(&lo0, &hi0);
    for (i = 0; i < CLOCK_CAL_STALLS; i++) {
        ScsiPortStallExecution(100);
    }
    ClockReadTsc(&lo1, &hi1);
    d = lo1 - lo0;      // 0.9 ms of TSC fits 32 bits below 4 GHz
    d = d / CLOCK_CAL_STALLS * 10;
    if (Clock->CalPct != 0 && Clock->CalPct <= 400) {
        // Test builds only (calx=): a deliberately wrong calibration.
        d = d / 100 * Clock->CalPct;
    }
    Clock->TscPerMs = d;
    if (hi1 - hi0 > 1 || d < 100000) {
        // No usable TSC (below 100 MHz or a wrap we cannot account for).
        Clock->UnitsPerMs = 0;
        Clock->Shift = 0;
    } else {
        // Pick the shift that leaves 500-1000 units per millisecond.
        shift = 0;
        while ((d >> shift) > 1000) {
            shift++;
        }
        Clock->Shift = shift;
        Clock->UnitsPerMs = d >> shift;
    }
    // The accumulated estimates continue from where they were (a resume
    // re-calibrates the same extension): monotonic, never re-priced. They
    // start above 0 so that no stamp is 0.
    if (!Clock->AccInit) {
        Clock->AccInit = TRUE;
        Clock->AccMs = 1000;
        Clock->Fallback = 1000;
    }
    Clock->AccLo = lo1;
    Clock->AccHi = hi1;
    Clock->AccSafe = Clock->SafeMs;
    Clock->WinBroken = TRUE;
}

//
// The TSC estimate (clock.h): accumulated milliseconds. The units elapsed
// since the last fold are priced at the current rate and added; the
// remainder below one millisecond stays in the raw base for the next fold.
//
ULONG ClockNow(IN OUT PENG_CLOCK Clock)
{
    ULONG lo, hi, units, ms, used, addLo, addHi;

    if (Clock->UnitsPerMs == 0) {
        return Clock->Fallback;
    }
    ClockReadTsc(&lo, &hi);
    switch (ClockSpan(Clock, Clock->AccLo, Clock->AccHi, lo, hi, &units)) {
    case CLK_SPAN_BACK:
        // Another processor's counter, behind this base: nothing elapsed.
        return Clock->AccMs;
    case CLK_SPAN_LONG:
        // Over an hour without a reading (the timer was stopped): fold the
        // gap with the safe clock, which never runs ahead.
        Clock->LongGaps++;
        Clock->AccMs += ClockSafeMs(Clock) - Clock->AccSafe;
        Clock->AccSafe = Clock->SafeMs;
        Clock->AccLo = lo;
        Clock->AccHi = hi;
        return Clock->AccMs;
    default:
        break;
    }
    ms = units / Clock->UnitsPerMs;
    if (ms != 0) {
        used = ms * Clock->UnitsPerMs;      // at most units: fits 32 bits
        if (Clock->Shift == 0) {
            addLo = used;
            addHi = 0;
        } else {
            addLo = used << Clock->Shift;
            addHi = used >> (32 - Clock->Shift);
        }
        Clock->AccLo += addLo;
        Clock->AccHi += addHi + (Clock->AccLo < addLo ? 1 : 0);
        Clock->AccMs += ms;
    }
    return Clock->AccMs;
}

static BOOLEAN ClockOsUsable(IN PENG_CLOCK Clock)
{
    return (BOOLEAN)(Clock->OsState == CLK_OS_TRIAL || Clock->OsState == CLK_OS_QUALIFIED);
}

// The safe clock (clock.h): never ahead of real time.
ULONG ClockSafeMs(IN OUT PENG_CLOCK Clock)
{
    ULONG os;

    if (ClockOsUsable(Clock)) {
        os = ClockOsMs(Clock);
        Clock->SafeMs += os - Clock->SafeOsBase;
        Clock->SafeOsBase = os;
    }
    return Clock->SafeMs;
}

// How much a difference of two safe-clock readings can overstate the real
// interval (clock.h): one OS clock period while the OS clock is in use, one
// timer period (at most the slow tick) once it has been rejected.
ULONG ClockResolutionMs(IN PENG_CLOCK Clock)
{
    return ClockOsUsable(Clock) ? CLK_OS_RES_MS : CLK_NOMINAL_RES_MS;
}

// The engine's timer stopped (nothing to watch), or an armed period was
// replaced before it expired (EngTimerEnsure): the nominal time does not
// count that interval, so the current cross-check window is discarded.
VOID ClockTimerStopped(IN OUT PENG_CLOCK Clock)
{
    Clock->WinBroken = TRUE;
}

//
// Every timer callback: the nominal period (the safe clock's source while
// the OS clock is not usable, and the TSC estimate's if there is no usable
// TSC), then the cross-check window of clock.h.
//
VOID ClockTick(IN OUT PENG_CLOCK Clock, IN ULONG PeriodUs)
{
    ULONG ms = PeriodUs / 1000;
    // What one callback adds to the safe and fallback clocks is capped at
    // the slow tick (CLK_NOMINAL_RES_MS): a reading taken between two
    // callbacks lags by up to the period then running, and
    // ClockResolutionMs allows for one slow tick only. The polling mode's
    // idle period can be longer (up to 60 s, PLAN "Polling mode"); the cap
    // only makes those estimates later. The cross-check windows use the full
    // nominal period.
    ULONG credit = (ms > CLK_NOMINAL_RES_MS) ? CLK_NOMINAL_RES_MS : ms;
    ULONG wn, wo, wu, rate, old, lo, hi;

    Clock->Nominal += ms;
    if (Clock->UnitsPerMs == 0) {
        Clock->Fallback += credit;
    }
    if (!ClockOsUsable(Clock)) {
        Clock->SafeMs += credit;
        goto fold;
    }
    ClockSafeMs(Clock);
    ClockReadTsc(&lo, &hi);
    if (Clock->WinBroken) {
        // First tick of a running timer: the window starts here.
        Clock->WinBroken = FALSE;
        Clock->WinLo = lo;
        Clock->WinHi = hi;
        Clock->WinOs = Clock->OsMs;
        Clock->WinNominal = Clock->Nominal;
        goto fold;
    }
    wn = Clock->Nominal - Clock->WinNominal;
    if (wn < CLK_WINDOW_MS) {
        goto fold;
    }
    wo = Clock->OsMs - Clock->WinOs;
    if (wo == 0 || wo > 4 * wn + 1000) {
        // Frozen, or far ahead of the nominal periods (which can only be
        // late): the OS clock is not used again in this session, and the
        // safe clock continues from the nominal periods.
        Clock->OsState = CLK_OS_REJECTED;
        Clock->Rejects++;
        DBGMARK(STG_CLOCK_OS, CLK_OS_REJECTED | ((wo > 0xFFFFFF ? 0xFFFFFF : wo) << 8));
        goto fold;
    }
    if (Clock->OsState == CLK_OS_TRIAL) {
        Clock->OsState = CLK_OS_QUALIFIED;
        DBGMARK(STG_CLOCK_OS, CLK_OS_QUALIFIED | ((wo > 0xFFFFFF ? 0xFFFFFF : wo) << 8));
    }
    if (Clock->UnitsPerMs != 0) {
        if (ClockSpan(Clock, Clock->WinLo, Clock->WinHi, lo, hi, &wu) != CLK_SPAN_OK) {
            // Not measurable (counter behind the window start): no sample.
            Clock->Rejects++;
        } else {
            // Re-calibrate the TSC against the OS clock over this window.
            // What has elapsed so far is folded at the old rate first, so
            // the new rate prices only what comes after (never re-pricing).
            ClockNow(Clock);
            rate = wu / wo;
            old = Clock->UnitsPerMs;
            if (rate == 0 || rate < old / 4 || rate > old * 4) {
                Clock->Rejects++;
                DBGSTAGE(STG_CLOCK_RECAL, (old & 0xFFFF) | ((rate > 0x7FFF ? 0x7FFF : rate) << 16) | 0x80000000UL);
            } else {
                Clock->UnitsPerMs = rate;
                if (++Clock->Recals <= 4 || rate * 8 < old * 7 || rate * 8 > old * 9) {
                    // The first ones, and any change above 1/8, are logged.
                    DBGSTAGE(STG_CLOCK_RECAL, (old & 0xFFFF) | ((rate & 0x7FFF) << 16));
                }
            }
        }
    }
    Clock->WinLo = lo;
    Clock->WinHi = hi;
    Clock->WinOs = Clock->OsMs;
    Clock->WinNominal = Clock->Nominal;
fold:
    // Fold the TSC estimate at every tick, so the gap to the next reading
    // stays short, and note the safe clock for the long-gap fallback.
    ClockNow(Clock);
    Clock->AccSafe = Clock->SafeMs;
}
