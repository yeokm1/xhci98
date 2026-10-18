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
// Elapsed-time clocks for deadlines (PLAN 0.10 "Gone state and its
// deadline": deadlines are elapsed time against a clock, never counts of
// timer ticks, which ran up to 55% late on NT and 3x late on 9x, G2, G9).
//
// Three sources, none of them trusted alone:
//
//   TSC      the processor's time-stamp counter; fine-grained and cheap, but
//            its rate comes from a calibration against
//            ScsiPortStallExecution (9 x 100 us, inside the stall budget),
//            which under TCG ranged from 0.57x to 2.05x the true rate
//            between boots. It is re-calibrated against the OS clock (below)
//            once that is qualified.
//   OS       the operating system's own monotonic time. NT: the interrupt
//            time in the shared user data page (KUSER_SHARED_DATA, the
//            fixed kernel address that the DDK's KeQueryInterruptTime and
//            KeQueryTickCount macros read; no import, any IRQL). 9x: the VMM
//            service Get_System_Time (INT 20h, ms since Windows started,
//            asynchronous). Both are kept by the OS from its clock interrupt,
//            so they can run slow under load, never fast; a reading lags
//            real time by up to one clock-interrupt period (CLK_OS_RES_MS).
//   nominal  the count of nominal timer periods. Timer callbacks only come
//            late, so it never runs ahead of real time; a reading lags by up
//            to one timer period.
//
// The TSC estimate (ClockNow) is a monotonic accumulated millisecond count:
// every reading folds the TSC units elapsed since the previous one into the
// count at the rate in force at that moment, so a re-calibration changes
// only how later TSC units are priced and never re-prices an interval that
// has already elapsed (review round 2: re-pricing old stamps with a new rate
// made elapsed estimates jump). A gap too long for 32-bit TSC units (over
// about an hour without a reading, timer stopped) is folded with the safe
// clock instead.
//
// The safe clock (ClockSafeMs) never runs ahead of real time: it advances
// with the OS clock while that is usable, and with the nominal periods
// otherwise. The OS clock is used on trial from the start and checked over
// windows of at least 2 s of nominal time while the timer runs without a
// break (ClockTick; a window spanning a stopped timer, or an armed period
// replaced before it expired, ClockTimerStopped, is discarded, since the
// nominal time does not count that interval): an OS clock
// that did not move in such a window (frozen, as with interrupts off) or
// moved more than 4x the nominal time plus 1 s is rejected for the rest of
// the session; a plausible window qualifies it and re-calibrates the TSC
// rate to TSC units per OS millisecond over that window (a sample outside
// 1/4x to 4x of the current rate is rejected and logged).
//
// Rule for deadlines (lifecycle.c: LcTrustMs and LcEarlyMs). Two kinds of
// decision are kept apart:
//
//   - Permission to release. A deadline whose expiry completes an SRB whose
//     buffer the device may still reach, or releases driver memory, without
//     isolation evidence (the MD-1 gone deadline and the per-SRB bound of
//     R6-6 in `gone`), and every hardware wait that must never end early
//     (CAP.TO for RDY and the disable, the SHST bound), uses the
//     TRUSTWORTHY elapsed time: the smaller of the TSC and safe estimates,
//     with the safe estimate reduced by its resolution (ClockResolutionMs),
//     so that it is a lower bound of real elapsed time. A fast-running TSC
//     cannot shorten it, a slow safe clock only makes it later, and the
//     quantisation of the OS clock cannot make it early.
//   - Early recovery and admission decisions, which release nothing: the
//     hold-then-BUSY bound and BUSY accounting for requests that never had
//     a child issued (PLAN 0.3). They must come before the port's own SRB
//     timer, so they use the LARGER estimate (the earlier deadline); their
//     margin covers the remaining error.
//
// A release deadline that must also beat a port timeout (the per-SRB bound)
// keeps the measured margin (half the TimeOutValue, at least GoneMarginMs);
// if the trustworthy clock runs so slow that the port's timer fires first,
// the outcome is the measured-safe one (HwResetBus in `gone` returns FALSE
// with the SRB still held, G4), never an early release.
//
// Times are 32-bit millisecond counts compared by unsigned difference
// (valid for 49 days). No 64-bit division or variable 64-bit shift is used,
// so the 32-bit images need no compiler helper routines.
//
// If the TSC calibration fails (no usable TSC), the TSC estimate falls back
// to the nominal periods too.
//

#ifndef _STOR98_CLOCK_H_
#define _STOR98_CLOCK_H_

// OS clock state
#define CLK_OS_NONE         0   // not available on this platform
#define CLK_OS_TRIAL        1   // used, not yet checked over a window
#define CLK_OS_QUALIFIED    2   // checked against the nominal periods
#define CLK_OS_REJECTED     3   // frozen or implausible: not used again

#define CLK_WINDOW_MS       2000    // nominal ms per cross-check window

// Resolution of the safe clock: how far one reading can lag real time, so
// how much a difference of two readings can overstate the interval.
// NT: the interrupt time advances once per clock interrupt, at most every
// 15.625 ms on the x86 and x64 HALs. 9x: VMM's system time advances with
// the timer interrupt, whose period is at most the PC default of 54.9 ms.
// Nominal periods (OS clock rejected): one timer period, at most the slow
// tick of the engine (1 s).
#ifdef NVME2K_W9X
#define CLK_OS_RES_MS       55
#else
#define CLK_OS_RES_MS       16
#endif
#define CLK_NOMINAL_RES_MS  1000

typedef struct _ENG_CLOCK {
    ULONG Shift;            // TSC right shift (units of 2^Shift TSC ticks)
    ULONG UnitsPerMs;       // TSC units per millisecond (0: no TSC, fallback clock)
    ULONG Fallback;         // fallback clock: nominal ms advanced by the timer
    ULONG TscPerMs;         // calibration result, for the log
    ULONG CalPct;           // test builds: scale the calibration to this percent (0: none)
    ULONG AccMs;            // TSC estimate: accumulated ms (monotonic)
    ULONG AccLo;            // raw TSC at the last fold (low, high)
    ULONG AccHi;
    ULONG AccSafe;          // safe ms at the last timer fold (long-gap fallback)
    ULONG AccInit;          // AccMs has a value (kept across a re-calibration)
    ULONG OsState;          // CLK_OS_*
    ULONG OsMs;             // OS clock, milliseconds (NT: accumulated from the interrupt time)
    ULONG OsLastLo;         // NT: last interrupt time read (100 ns units)
    ULONG OsLastHi;
    ULONG OsRem;            // NT: 100 ns remainder not yet counted in OsMs
    ULONG SafeMs;           // safe clock: never ahead of real time
    ULONG SafeOsBase;       // OsMs already counted into SafeMs
    ULONG Nominal;          // nominal timer milliseconds
    ULONG WinLo;            // cross-check window start: raw TSC,
    ULONG WinHi;
    ULONG WinOs;            //   OS ms,
    ULONG WinNominal;       //   nominal ms
    ULONG Recals;           // TSC re-calibrations accepted
    ULONG Rejects;          // samples rejected
    ULONG LongGaps;         // folds that used the safe clock (gap beyond 32-bit TSC units)
    ULONG WinBroken;        // the timer stopped since the window began: restart it
} ENG_CLOCK, *PENG_CLOCK;

VOID ClockReadTsc(OUT PULONG Lo, OUT PULONG Hi);
// ClockCalibrate stalls this many times 100 us (HwFindAdapter); the rest of
// that callback's stall budget (PLAN 0.10) is what the 9x pool may use there.
#define CLOCK_CAL_STALLS    9
VOID ClockCalibrate(IN OUT PENG_CLOCK Clock);
ULONG ClockNow(IN OUT PENG_CLOCK Clock);
ULONG ClockSafeMs(IN OUT PENG_CLOCK Clock);
ULONG ClockResolutionMs(IN PENG_CLOCK Clock);
VOID ClockTick(IN OUT PENG_CLOCK Clock, IN ULONG PeriodUs);
VOID ClockTimerStopped(IN OUT PENG_CLOCK Clock);

// Elapsed milliseconds of the TSC estimate since Since (a ClockNow value)
#define ClockElapsedMs(c, since) (ClockNow(c) - (since))

#endif // _STOR98_CLOCK_H_
