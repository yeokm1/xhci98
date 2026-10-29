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
// Adapter lifecycle: the states of PLAN 0.7 and the recovery contract of
// 0.10 / callback matrix of 0.11, as measured by the gates:
//
// - Deferred bring-up (G1): HwInitialize does everything that can fail
//   before CC.EN, writes CC.EN and returns TRUE; the timer waits for RDY
//   (elapsed time, CAP.TO), creates the I/O queues and identifies. A failure
//   after CC.EN disables the controller and ends in `failed` (disable
//   acknowledged) or `quarantined`; the adapter stays started. HwInitialize
//   never returns FALSE on resume (G7, R6-7).
// - Requests while not running are held with no child issued, and completed
//   BUSY once most of their TimeOutValue has passed (hold-then-BUSY, G1,
//   Q-NT); never a 21st BUSY for one SRB. 9x uses a fixed hold bound, and
//   holds its bus-scan INQUIRY until bring-up ends, never BUSY (Q-9x).
// - Presence loss (all-ones CSTS) enters `gone`: issued SRBs held, new ones
//   fast-failed with SRB_STATUS_NO_DEVICE. Each held SRB completes on
//   isolation evidence or at its own deadline: the earlier of the gone
//   deadline and its start + TimeOutValue - margin on NT (R6-6); the MD-1
//   outcome quarantines its DMA memory. BusChangeDetected follows on NT.
//   Contact regained with CC.EN=1 or RDY=1 (reopen violation) leaves
//   `gone` for the disable wait of `resetting`, so a controller shown
//   present never gets the MD-1 release.
// - HwResetBus is truthful (G4): TRUE only on an acknowledged disable (or
//   with nothing issued); FALSE leaves the SRBs held while the timer waits
//   for RDY=0, after which they complete BUS_RESET and ResetDetected is
//   notified; queues are re-created from the timer.
// - SRB_FUNCTION_SHUTDOWN is the restartable shutdown notification (0.11
//   class 2, Q-NT): Flush, CC.SHN, SHST waited by the timer, resources
//   retained; the next request re-enables the controller first.
// - The stop (ScsiStopAdapter, 9x HwAdapterState(FALSE)) is class 3: Flush
//   if dirty, CC.SHN, CC.EN=0, each polled only within the stall budget.
// - Idle flush (MD-7 (a)): the timer submits a Flush once the adapter is
//   dirty and write-idle for the idle time.
//

#include "nvme2k.h"
#include "utils.h"
#include "dpkeys.h"

#define ENG_ALL_ONES        0xFFFFFFFF
#define ENG_SHST_LIMIT_MS   5000    // SHUTDOWN SRB: SHST wait bound, then logged not clean

static ULONG LcNow(IN PHW_DEVICE_EXTENSION DevExt)
{
    return ClockNow(&DevExt->Clock);
}

static ULONG LcMsSince(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG Since)
{
    return LcNow(DevExt) - Since;
}

// The safe clock (clock.h): never ahead of real time.
static ULONG LcSafe(IN PHW_DEVICE_EXTENSION DevExt)
{
    return ClockSafeMs(&DevExt->Clock);
}

//
// Elapsed time since a stamp pair (TSC estimate ms, safe ms), under the rule
// of clock.h, which keeps two kinds of decision apart:
//
// LcTrustMs, trustworthy elapsed time, a lower bound of the real interval:
// the smaller of the two estimates, the safe one reduced by its resolution.
// It gives permission to release: every deadline whose expiry completes an
// SRB the device may still reach, or releases memory, without isolation
// evidence (the MD-1 gone deadline from entry into `gone` and the per-SRB
// bound of R6-6), and every hardware wait that must not end early (CAP.TO
// for RDY and the disable, the SHST bound). The safe clock never runs ahead
// of real time and its quantisation is taken off, so a fast TSC (a
// calibration of 0.57x makes it read 1.75x) cannot shorten these.
//
// LcEarlyMs, the larger estimate (the earlier deadline): early recovery and
// admission decisions that release nothing (hold-then-BUSY and BUSY
// accounting for requests that never had a child issued, PLAN 0.3). A slow
// estimate cannot push them past the port's timeout; their margins (half of
// TimeOutValue, LcHoldBoundMs) cover a safe clock that runs slow under load.
//
static ULONG LcTrustMs(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG Since, IN ULONG SinceSafe)
{
    ULONG tsc = LcMsSince(DevExt, Since);
    ULONG safe = LcSafe(DevExt) - SinceSafe;
    ULONG res = ClockResolutionMs(&DevExt->Clock);

    safe = (safe > res) ? safe - res : 0;
    return tsc < safe ? tsc : safe;
}

static ULONG LcEarlyMs(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG Since, IN ULONG SinceSafe)
{
    ULONG tsc = LcMsSince(DevExt, Since);
    ULONG safe = LcSafe(DevExt) - SinceSafe;

    return tsc > safe ? tsc : safe;
}

static VOID LcFinishSettled(IN PHW_DEVICE_EXTENSION DevExt, IN BOOLEAN KeepUnissued);
static VOID LcStormUnmaskTrial(IN PHW_DEVICE_EXTENSION DevExt);

static ULONG LcCsts(IN PHW_DEVICE_EXTENSION DevExt)
{
    return NvmeReadReg32(DevExt, NVME_REG_CSTS);
}

static ULONG LcNonFreeParents(IN PHW_DEVICE_EXTENSION DevExt)
{
    return ENG_MAX_PARENTS - EngFreeParents(DevExt);
}

//
// Tunables (PLAN 0.5): parsed once; a NULL ArgumentString (resume) keeps
// what the device extension already holds.
//
static UCHAR LcLower(UCHAR c)
{
    return (UCHAR)((c >= 'A' && c <= 'Z') ? (c - 'A' + 'a') : c);
}

static PCHAR LcFind(IN PCHAR s, IN const char *Key)
{
    ULONG i;

    if (s == NULL) {
        return NULL;
    }
    for (; *s; s++) {
        for (i = 0; Key[i] && s[i] && LcLower((UCHAR)s[i]) == (UCHAR)Key[i]; i++) {
        }
        if (Key[i] == 0) {
            return s + i;
        }
    }
    return NULL;
}

// Decimal value; saturates at 0xFFFFFFFF (as PollParseDec), so a number
// beyond 32 bits is out of every range rather than wrapped into one.
static ULONG LcNum(IN PCHAR s, IN ULONG Default)
{
    ULONG v = 0, d;

    if (s == NULL || *s < '0' || *s > '9') {
        return Default;
    }
    while (*s >= '0' && *s <= '9') {
        d = (ULONG)(*s - '0');
        v = (v > (0xFFFFFFFFUL - d) / 10) ? 0xFFFFFFFFUL : v * 10 + d;
        s++;
    }
    return v;
}

//
// Polling settings (polltun.h, PLAN "Polling mode"). NT: DriverParameter
// keys; a resume brings no ArgumentString and keeps what was parsed. 9x:
// registry values read through VMM at every load (PlatPollRaw).
//
static VOID LcPollField(IN PCHAR ArgumentString, IN const char *Key, IN ULONG Bit, OUT ULONG *Value,
                        IN OUT POLL_RAW *Raw)
{
    if (PollParseDec(LcFind(ArgumentString, Key), Value)) {
        Raw->Present |= Bit;
    }
}

static VOID LcPollTunables(IN PHW_DEVICE_EXTENSION DevExt, IN PCHAR ArgumentString)
{
    ENG_TUNABLES *t = &DevExt->Tun;
    POLL_RAW raw;
    POLL_SETTINGS *s = &t->Poll;
    ULONG busyMs, idleS;

    RtlZeroMemory(&raw, sizeof(raw));
#ifdef NVME2K_W9X
    UNREFERENCED_PARAMETER(ArgumentString);
    PlatPollRaw(&raw);
#else
    if (ArgumentString == NULL) {
        // Resume: the settings parsed at the start stay (PLAN 0.5).
        goto log;
    }
    LcPollField(ArgumentString, DPK_POLLMODE, POLL_CLAMP_MODE, &raw.Mode, &raw);
    LcPollField(ArgumentString, DPK_POLLBUSYUS, POLL_CLAMP_BUSY, &raw.BusyUs, &raw);
    LcPollField(ArgumentString, DPK_POLLIDLEMS, POLL_CLAMP_IDLE, &raw.IdleMs, &raw);
    LcPollField(ArgumentString, DPK_POLLSPINUS, POLL_CLAMP_SPIN, &raw.SpinUs, &raw);
#endif
    PollSettingsApply(s, &raw);
    if (s->Clamped != 0) {
        // The clamp is logged with what was read.
        DBGSTAGE(STG_POLL_RAW, raw.Present | ((raw.Mode > 0xF ? 0xF : raw.Mode) << 4) |
                 ((raw.BusyUs / 1000 > 0xFFFF ? 0xFFFF : raw.BusyUs / 1000) << 8) |
                 ((raw.IdleMs / 100 > 0xFF ? 0xFF : raw.IdleMs / 100) << 24));
    }
#ifndef NVME2K_W9X
log:
#endif
    busyMs = s->BusyUs / 1000;
    idleS = s->IdleMs / 1000;
    DBGSTAGE(STG_POLL_TUNABLES, s->Mode | (s->Clamped << 4) | ((ULONG)DevExt->Resumed << 8) |
             (((s->SpinUs / 10) & 0x3F) << 9) | ((busyMs > 0xFF ? 0xFF : busyMs) << 16) |
             ((idleS > 0xFF ? 0xFF : idleS) << 24));
    UNREFERENCED_PARAMETER(busyMs);
    UNREFERENCED_PARAMETER(idleS);
}

//
// Maximum transfer size (xfertun.h; PLAN "0.4.0.0 plan", owner decision
// 2026-10-16). NT: stormaxtransferkb= in DriverParameter; a resume brings no
// ArgumentString and keeps what was parsed. 9x: StorMaxTransferKB, read
// through VMM at every load (PlatXferRaw). It only lowers the build's
// maximum, STOR98_MAX_TRANSFER, which the compile-time PRP list and NLB
// checks cover; NegotiateDma reports it to the port and LcBringupDone caps
// the child size at it.
//
static VOID LcXferTunables(IN PHW_DEVICE_EXTENSION DevExt, IN PCHAR ArgumentString)
{
    ENG_TUNABLES *t = &DevExt->Tun;
    BOOLEAN present = FALSE, valid = FALSE, parsed = TRUE;
    ULONG raw = 0;

#ifdef NVME2K_W9X
    UNREFERENCED_PARAMETER(ArgumentString);
    PlatXferRaw(&present, &valid, &raw);
#else
    if (ArgumentString == NULL) {
        // Resume: the setting parsed at the start stays (PLAN 0.5).
        parsed = FALSE;
    } else {
        PCHAR p = LcFind(ArgumentString, DPK_MAXTRANSFERKB);

        if (p != NULL) {
            present = TRUE;
            valid = XferParseDec(p, TRUE, &raw);
        }
    }
#endif
    if (parsed) {
        t->MaxXferWhy = XferTunApply(present, valid, raw, STOR98_MAX_TRANSFER / 1024, &t->MaxXferKb);
        if (t->MaxXferWhy == XFER_REFUSED) {
            DBGSTAGE(STG_XFER_REFUSED, valid ? raw : 0);
        }
    }
    DBGSTAGE(STG_XFER_TUNABLE, (t->MaxXferKb & 0xFFFF) | (t->MaxXferWhy << 16) | ((ULONG)DevExt->Resumed << 20) |
             ((ULONG)parsed << 21));
}

VOID EngParseTunables(IN PHW_DEVICE_EXTENSION DevExt, IN PCHAR ArgumentString)
{
    ENG_TUNABLES *t = &DevExt->Tun;
    BOOLEAN parsed = FALSE;

    if (t->Magic != ENG_TUNABLES_MAGIC) {
        t->Magic = ENG_TUNABLES_MAGIC;
        t->GoneMs = 5000;
        t->IdleFlushMs = 1000;
        t->GoneMarginMs = 1500;
        t->HoldMarginMs = 1000;
        t->Hold9xMs = ENG_HOLD9X_DEF_MS;
#if (defined(STOR98_TEST_FRAG_KB) || defined(STOR98_TEST_SG9X_FAIL)) && !defined(STOR98_DBGPORT)
#error STOR98_TEST_FRAG_KB and STOR98_TEST_SG9X_FAIL are test-only: build them with STOR98_DBGPORT=1
#endif
#ifdef STOR98_TEST_FRAG_KB
        t->FragBytes = STOR98_TEST_FRAG_KB * 1024;  // 9x test images: no ArgumentString
#else
        t->FragBytes = 0;
#endif
#ifdef STOR98_TEST_SG9X_FAIL
        t->Sg9xFailEvery = STOR98_TEST_SG9X_FAIL;
#else
        t->Sg9xFailEvery = 0;
#endif
        t->Sg9xFailCount = 0;
#if defined(STOR98_TEST_BOUNCE) && !defined(STOR98_DBGPORT)
#error STOR98_TEST_BOUNCE is test-only: build it with STOR98_DBGPORT=1 (out\<flavor>-dbgport, never staged)
#endif
#ifdef STOR98_TEST_BOUNCE
        t->ForceBounce = 1;     // 9x test images: no ArgumentString (TESTING)
#else
        t->ForceBounce = 0;
#endif
        t->CalPct = 0;
        t->ShnFlush = 0;
        t->StormTest = 0;
        t->StormInts = 0;
        t->IdleFlushToMs = 0;
        PollSettingsDefault(&t->Poll);
        t->MaxXferKb = STOR98_MAX_TRANSFER / 1024;
        t->MaxXferWhy = XFER_DEFAULT;
#ifdef STOR98_TEST_CAPTO_MS
        t->CapToMs = STOR98_TEST_CAPTO_MS;
#else
        t->CapToMs = 0;
#endif
    }
    if (ArgumentString != NULL) {
        parsed = TRUE;
        t->GoneMs = LcNum(LcFind(ArgumentString, DPK_GONE), t->GoneMs);
        t->IdleFlushMs = LcNum(LcFind(ArgumentString, DPK_IDLE), t->IdleFlushMs);
#ifdef STOR98_DBGPORT
        // hold9x= is a test key: the 9x hold bound it sets is used on 9x
        // alone, whose port passes no ArgumentString (dpkeys.h).
        t->Hold9xMs = LcNum(LcFind(ArgumentString, DPK_HOLD9X), t->Hold9xMs);
        // Test-only shapes of the engine's work (test builds only).
        t->FragBytes = LcNum(LcFind(ArgumentString, DPK_FRAG), t->FragBytes / 1024) * 1024;
        t->ForceBounce = LcNum(LcFind(ArgumentString, DPK_BOUNCE), t->ForceBounce);
        // Simulated miscalibration of the TSC against the stall (seen from
        // 0.57x to 2.05x under TCG): calx=57 makes the TSC estimate run
        // 1.75x fast.
        t->CalPct = LcNum(LcFind(ArgumentString, DPK_CALX), t->CalPct);
        // SRB_FUNCTION_FLUSH runs the restartable shutdown (review round 2
        // late-Flush tests: FlushFileBuffers drives the SHUTDOWN path).
        t->ShnFlush = LcNum(LcFind(ArgumentString, DPK_SHNFLUSH), t->ShnFlush);
        t->CapToMs = LcNum(LcFind(ArgumentString, DPK_CAPTO), t->CapToMs);
        // Provoke the QEMU INTx defect (TESTING "INTx model caveat") to
        // exercise the storm recovery (EngInterrupt): one redundant CQ head
        // doorbell after bring-up. Test builds only; never on hardware.
        t->StormTest = LcNum(LcFind(ArgumentString, DPK_STORMTEST), t->StormTest);
        // A lower storm threshold, so that another device on a shared INTx
        // line can reach it in the emulator (review round 3, N1 test).
        t->StormInts = LcNum(LcFind(ArgumentString, DPK_STORMINTS), t->StormInts);
        // A shorter idle Flush deadline, for the held-idle-Flush test
        // (harness: x-faults at=fetch op=flush act=hold).
        t->IdleFlushToMs = LcNum(LcFind(ArgumentString, DPK_IDLETO), t->IdleFlushToMs);
#endif
    }
#ifdef NVME2K_W9X
    // 9x: StorGoneMs and StorWriteIdleMs, read through VMM at every load
    // (the port passes no ArgumentString); the same ranges as NT's below.
    // StorHold9xMs, the 9x hold bound, has no NT key (xhci98 3.3.0.0).
    PlatTunMsRaw(&t->GoneMs, &t->IdleFlushMs, &t->Hold9xMs);
#endif
    if (t->Hold9xMs < ENG_HOLD9X_MIN_MS || t->Hold9xMs > ENG_HOLD9X_MAX_MS) {
        DBGSTAGE(STG_HOLD9X_REFUSED, t->Hold9xMs);
        t->Hold9xMs = ENG_HOLD9X_DEF_MS;
    }
#ifdef NVME2K_W9X
    DBGSTAGE(STG_HOLD9X, t->Hold9xMs);
#endif
    if (t->GoneMs == 0 || t->GoneMs > 600000) {
        t->GoneMs = 5000;
    }
    if (t->IdleFlushMs < 10) {
        t->IdleFlushMs = 10;
    }
    DBGSTAGE(STG_OPTIONS, (t->GoneMs & 0xFFFF) | ((t->IdleFlushMs / 10 > 0xFFFF ? 0xFFFF : t->IdleFlushMs / 10) << 16));
    DBGSTAGE(STG_TUNABLES, ((t->FragBytes / 1024) & 0xFFFF) | ((t->ForceBounce ? 1UL : 0) << 16) |
             ((ULONG)DevExt->Resumed << 17) | ((ULONG)parsed << 18) | ((t->ShnFlush ? 1UL : 0) << 19));
    LcPollTunables(DevExt, ArgumentString);
    LcXferTunables(DevExt, ArgumentString);
}

//
// State changes
//
VOID EngSetState(IN PHW_DEVICE_EXTENSION DevExt, IN UCHAR State, IN ULONG Reason)
{
    DBGSTAGE(STG_STATE, ((ULONG)DevExt->State << 8) | State | ((Reason & 0xFF) << 16) |
             ((ULONG)DevExt->Bringup << 24));
    DevExt->State = State;
    DevExt->StateTime = LcNow(DevExt);
    DevExt->StateSafe = LcSafe(DevExt);
    // Interface 3 R10 (K1, Codex round 1 on fix-0.3.0.1): whatever state is
    // entered, from whichever callback, a CM channel that wants polling gets
    // a timer callback (a resume that ends in `failed` returns from
    // HwInitialize without arming one). EngTimerEnsure ignores the stopped
    // states, and the timer's own re-arm then keeps the period.
    {
        ULONG cm = PlatCmLinkPollUs(DevExt);

        if (cm != 0) {
            EngTimerEnsure(DevExt, cm);
        }
    }
}

static VOID LcSetStep(IN PHW_DEVICE_EXTENSION DevExt, IN UCHAR Step)
{
    DevExt->Bringup = Step;
    DevExt->StateTime = LcNow(DevExt);
    DevExt->StateSafe = LcSafe(DevExt);
    DBGSTAGE(STG_STATE, ((ULONG)DevExt->State << 8) | DevExt->State | ((ULONG)Step << 24));
}

//
// The polling period while work is outstanding: PollBusyUs in the polling
// mode (PLAN "Polling mode"; 1-100 ms, polltun.h), the fast tick after an
// interrupt storm.
//
static ULONG LcPollBusyUs(IN PHW_DEVICE_EXTENSION DevExt)
{
    if (!DevExt->PollAlways) {
        return ENG_TICK_FAST_US;
    }
#ifndef NVME2K_W9X
    // NT: the port's timer expires on the OS clock interrupt (10-15.6 ms), so
    // a shorter period changes nothing but would make every callback look
    // late against its nominal period, and the OS-clock cross-check
    // (clock.c) would reject a healthy clock. The fast tick is the floor.
    if (DevExt->Tun.Poll.BusyUs < ENG_TICK_FAST_US) {
        return ENG_TICK_FAST_US;
    }
#endif
    return DevExt->Tun.Poll.BusyUs;
}

//
// Whether outstanding work is polled at the busy period. In gone and
// quarantined the polling mode keeps the watchdog tick: the device is
// presumed absent or stuck, a late completion there is rare and only
// releases its own request, and coarse ticks keep the OS-clock cross-check
// (clock.c) running while the gone deadline and the per-SRB bounds are
// measured (review round 3). After a storm (PollMode without PollAlways)
// the fast tick applies in every state, as before.
//
static BOOLEAN LcPollFast(IN PHW_DEVICE_EXTENSION DevExt)
{
    if (!DevExt->PollMode) {
        return FALSE;
    }
    if (DevExt->PollAlways && (DevExt->State == ENG_ST_GONE || DevExt->State == ENG_ST_QUARANTINED)) {
        return FALSE;
    }
    return TRUE;
}

//
// The single timer (SCSIport keeps one timer request per adapter: a new
// request replaces the previous one). It is also the one dispatcher of every
// periodic duty (EngTimer): the CM link statement, the state's step, which
// polls the completion queues where they are live, and the hold check; the
// period is the shortest any of them wants (LcWantedPeriod).
//
VOID EngTimerEnsure(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG PeriodUs)
{
    ULONG busy;

    if (DevExt->State == ENG_ST_STOPPED || DevExt->State == ENG_ST_STOPPING || DevExt->State == ENG_ST_NONE) {
        return;
    }
    busy = LcPollBusyUs(DevExt);
    // Polling (no interrupt, or after a storm with the vector masked): the
    // timer is the only completion path, so work submitted now is watched at
    // the polling period, as LcWantedPeriod keeps it; at the watchdog tick a
    // request stream ran at about one completion per 100 ms (W2K stormtest).
    // In the polling mode only while work is outstanding: an idle,
    // dirty-idle or CM-only period is kept (polltun.h, PollEnsurePeriod),
    // and not in gone or quarantined (LcPollFast). After a storm (PollMode
    // without PollAlways) every such request is shortened, as on main.
    PeriodUs = PollEnsurePeriod(PeriodUs, LcPollFast(DevExt),
                                (BOOLEAN)(!DevExt->PollAlways || EngIssuedChildren(DevExt) != 0 ||
                                          LcNonFreeParents(DevExt) != 0), busy);
    if (!DevExt->TimerActive || PeriodUs < DevExt->TimerPeriodUs) {
        if (DevExt->TimerActive) {
            // An armed period is replaced before it expired: ClockTick
            // credits each callback with the period then armed, so the
            // time already spent on the replaced one is never counted as
            // nominal time. The clock's cross-check window is discarded
            // (as for a stopped timer), so this real time does not count
            // against the OS clock (clock.h): a 1 s idle tick replaced by
            // 10 ms at every request would otherwise make a healthy OS
            // clock look 4x fast and reject it for the session. Only
            // nominal time is lost, which makes the safe clock later,
            // never early.
            ClockTimerStopped(&DevExt->Clock);
        }
#ifdef NVME2K_W9X
        if (DevExt->TimerActive) {
            // 9x: a request while one is pending does not replace it
            // (measured in the polling mode, 98 SE: a 10 ms request after an
            // armed 1 s idle tick waited for that tick). Cancel first.
            ScsiPortNotification(RequestTimerCall, (PVOID)DevExt, EngTimer, 0);
        }
#endif
        DevExt->TimerActive = TRUE;
        DevExt->TimerPeriodUs = PeriodUs;
        ScsiPortNotification(RequestTimerCall, (PVOID)DevExt, EngTimer, PeriodUs);
    }
}

static ULONG LcWantedPeriodEngine(IN PHW_DEVICE_EXTENSION DevExt);

// Running with nothing outstanding and nothing unflushed: the period the
// engine wants there is 0 with an interrupt, PollIdleMs when polling.
static BOOLEAN LcEngineIdle(IN PHW_DEVICE_EXTENSION DevExt)
{
    return (BOOLEAN)(EngIssuedChildren(DevExt) == 0 && LcNonFreeParents(DevExt) == 0 &&
                     DevExt->WriteGen == DevExt->CleanGen);
}

//
// The engine's own period, shortened to the CM link-state poll interval
// while a CM channel wants polling (interface 3 R10: a timer callback every
// STOR98LS9X_POLL_MS whenever not stopped, idle or not, so a DOWN_QUIESCED
// statement is seen within the CM's wait even with nothing outstanding;
// PlatCmLinkPollUs is 0 where there is no such channel).
//
static ULONG LcWantedPeriod(IN PHW_DEVICE_EXTENSION DevExt)
{
    ULONG period = LcWantedPeriodEngine(DevExt);
    ULONG cm = PlatCmLinkPollUs(DevExt);

    if (DevExt->State != ENG_ST_STARTING && DevExt->State != ENG_ST_RESETTING) {
        period = PollCombine(period, cm);
    }
    return period;
}

static ULONG LcWantedPeriodEngine(IN PHW_DEVICE_EXTENSION DevExt)
{
    BOOLEAN busy = (BOOLEAN)(EngIssuedChildren(DevExt) != 0 || LcNonFreeParents(DevExt) != 0);
    // Polling (no interrupt, or after an interrupt storm): the timer is the
    // only completion path, so it runs at the polling period while anything
    // is outstanding.
    ULONG watch = LcPollFast(DevExt) ? LcPollBusyUs(DevExt) : ENG_TICK_WATCH_US;

    switch (DevExt->State) {
    case ENG_ST_STARTING:
    case ENG_ST_RESETTING:
        return ENG_TICK_FAST_US;
    case ENG_ST_SHUTDOWN:
        if (DevExt->Bringup == ENG_BU_SHN_WAIT) {
            return ENG_TICK_FAST_US;
        }
        return busy ? ENG_TICK_WATCH_US : 0;
    case ENG_ST_RUNNING:
        // Idle in the polling mode: PollIdleMs, 0 = the timer stops as with
        // an interrupt (the next request re-arms it from HwStartIo).
        return PollRunningPeriod(busy, (BOOLEAN)(DevExt->WriteGen != DevExt->CleanGen), DevExt->PollMode,
                                 DevExt->PollAlways, LcPollBusyUs(DevExt), DevExt->Tun.Poll.IdleMs,
                                 ENG_TICK_WATCH_US);
    case ENG_ST_GONE:
        return busy ? watch : ENG_TICK_SLOW_US;
    case ENG_ST_QUARANTINED:
        return busy ? watch : ENG_TICK_SLOW_US;
    case ENG_ST_FAILED:
        return busy ? ENG_TICK_WATCH_US : 0;
    default:
        return 0;
    }
}

//
// Failing requests in `failed`/`quarantined` (parent error mapping): FLUSH,
// SYNCHRONIZE CACHE and SHUTDOWN succeed only when every write is known
// durable (WriteGen == CleanGen: after a bring-up failure, or with nothing
// written since the last completed Flush); an adapter that failed from
// `running` with writes not yet flushed cannot make them durable any more,
// so they fail like the rest (PLAN 0.10 durability). INQUIRY reports no
// device if identify never succeeded; the rest gets HARDWARE ERROR 04/44/00.
// Nothing was issued for these SRBs.
//
static VOID LcFailSrb(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb)
{
    Srb->DataTransferLength = 0;
    if ((Srb->Function == SRB_FUNCTION_FLUSH || Srb->Function == SRB_FUNCTION_SHUTDOWN ||
         (Srb->Function == SRB_FUNCTION_EXECUTE_SCSI &&
          (Srb->Cdb[0] == SCSIOP_SYNCHRONIZE_CACHE || Srb->Cdb[0] == SCSIOP_SYNCHRONIZE_CACHE16))) &&
        DevExt->WriteGen == DevExt->CleanGen) {
        Srb->SrbStatus = SRB_STATUS_SUCCESS;
    } else if (Srb->Function == SRB_FUNCTION_EXECUTE_SCSI && Srb->Cdb[0] == SCSIOP_INQUIRY &&
               !DevExt->IdentifyDone) {
        Srb->SrbStatus = SRB_STATUS_SELECTION_TIMEOUT;
    } else if (Srb->Function == SRB_FUNCTION_EXECUTE_SCSI) {
        Srb->SrbStatus = SRB_STATUS_ERROR;
        Srb->ScsiStatus = SCSISTAT_CHECK_CONDITION;
        ScsiSetSense(DevExt, Srb, SCSI_SENSE_HARDWARE_ERROR, 0x44, 0x00);
    } else {
        Srb->SrbStatus = SRB_STATUS_ERROR;
    }
    DevExt->FastFails++;
    DBGSTAGE(STG_SRB_FAILED_STATE, (ULONG)Srb->Function | ((ULONG)Srb->Cdb[0] << 8) |
             ((ULONG)DevExt->State << 16) | ((ULONG)Srb->SrbStatus << 24));
    EngCompleteSrb(DevExt, Srb);
}

// Terminal fast-fail in `gone` (PLAN 2.2): never reached the device.
static VOID LcGoneFailSrb(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb)
{
    Srb->SrbStatus = SRB_STATUS_NO_DEVICE;
    Srb->ScsiStatus = SCSISTAT_GOOD;
    Srb->DataTransferLength = 0;
    DevExt->FastFails++;
    if (DevExt->FastFails <= 32 || (DevExt->FastFails & 31) == 0) {
        DBGSTAGE(STG_GONE_FASTFAIL, (STG_SRB_ARG(Srb, DevExt->State) & 0xFFFFFF) |
                 ((ULONG)Srb->SrbStatus << 24));
    }
    EngCompleteSrb(DevExt, Srb);
}

// A parked reset SRB whose disable can no longer be acknowledged (the reset
// failed into `quarantined` or `failed`): ERROR (PLAN 0.10 entry points).
// It issued nothing, so there is no DMA to wait for.
static VOID LcFailParkedResets(IN PHW_DEVICE_EXTENSION DevExt)
{
    ULONG i;
    PENG_PARENT p;

    for (i = 0; i < ENG_MAX_PARENTS; i++) {
        p = &DevExt->Parents[i];
        if (p->State == ENG_PA_PARKED && p->Kind == ENG_K_RESET) {
            p->ErrSrbStatus = SRB_STATUS_ERROR;
            p->ErrScsiStatus = SCSISTAT_GOOD;
            EngFinishParent(DevExt, i);
        }
    }
}

// Watchdog recovery or a reset: CC.EN=0 has been written and the disable
// is not yet acknowledged. Every entry into `resetting` goes through here.
// The recovery is authoritative (review round 2): a SHUTDOWN whose Flush is
// still outstanding will not write CC.SHN any more (its work is cancelled,
// H1), so its late Flush completion cannot take the adapter out of the
// recovery (engine.c, EngChildCompleted).
static VOID LcEnterResetting(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG Reason)
{
    ULONG i;
    PENG_PARENT p;

    EngSetState(DevExt, ENG_ST_RESETTING, Reason);
    for (i = 0; i < ENG_MAX_PARENTS; i++) {
        p = &DevExt->Parents[i];
        if (p->State != ENG_PA_FREE && p->Kind == ENG_K_SHUTDOWN && p->State != ENG_PA_SHN &&
            p->Cancel == ENG_CANCEL_NONE) {
            p->Cancel = ENG_CANCEL_RESET;
            DBGSTAGE(STG_ENG_CANCEL, i | ((ULONG)ENG_CANCEL_RESET << 8) | ((ULONG)p->Outstanding << 16));
        }
    }
}

// Every held (never started) parent fails in the failed state.
static VOID LcFailHeldParents(IN PHW_DEVICE_EXTENSION DevExt)
{
    ULONG i;
    PENG_PARENT p;
    PSCSI_REQUEST_BLOCK srb;

    for (i = 0; i < ENG_MAX_PARENTS; i++) {
        p = &DevExt->Parents[i];
        if (p->State != ENG_PA_WAITING || (p->Flags & ENG_PF_STARTED)) {
            continue;
        }
        srb = p->Srb;
        p->Srb = NULL;
        p->State = ENG_PA_FREE;
        DevExt->ParentsCompleted++;
        LcFailSrb(DevExt, srb);
    }
}

//
// Bring-up (PLAN 0.10 "Bring-up", G1)
//
static VOID LcEnable(IN PHW_DEVICE_EXTENSION DevExt)
{
    ULONG cc, aqa;

    // QEMU's controller reset clears INTMS; mask again before enabling.
    NvmeWriteReg32(DevExt, NVME_REG_INTMS, 0xFFFFFFFF);
    // The blocks become reachable again: RESERVED or ISOLATED -> OWNED
    // before any queue base or CC.EN is written (platform.h, N2).
    PlatDmaReachable(DevExt);

    DevExt->AdminQueue.SubmissionQueueHead = 0;
    DevExt->AdminQueue.SubmissionQueueTail = 0;
    DevExt->AdminQueue.CompletionQueueHead = DevExt->AdminQueue.QueueSize;
    DevExt->IoQueue.SubmissionQueueHead = 0;
    DevExt->IoQueue.SubmissionQueueTail = 0;
    DevExt->IoQueue.CompletionQueueHead = DevExt->IoQueue.QueueSize;
    RtlZeroMemory(DevExt->AdminQueue.SubmissionQueue, PAGE_SIZE);
    RtlZeroMemory(DevExt->AdminQueue.CompletionQueue, PAGE_SIZE);
    RtlZeroMemory(DevExt->IoQueue.SubmissionQueue, PAGE_SIZE);
    RtlZeroMemory(DevExt->IoQueue.CompletionQueue, PAGE_SIZE);
    RtlZeroMemory(DevExt->UtilityBuffer, PAGE_SIZE);
    DevExt->AdminChainDone = FALSE;
    DevExt->AdminError = 0;
    DevExt->AdminCqLive = TRUE;

    aqa = ((ULONG)(DevExt->AdminQueue.QueueSize - 1) << 16) | (DevExt->AdminQueue.QueueSize - 1);
    NvmeWriteReg32(DevExt, NVME_REG_AQA, aqa);
    NvmeWriteReg64(DevExt, NVME_REG_ASQ, DevExt->AdminQueue.SubmissionQueuePhys.QuadPart);
    NvmeWriteReg64(DevExt, NVME_REG_ACQ, DevExt->AdminQueue.CompletionQueuePhys.QuadPart);

    // MPS 0 (4 KiB, validated against CAP.MPSMIN), NVM command set,
    // round robin, 64-byte SQ and 16-byte CQ entries.
    cc = NVME_CC_ENABLE | NVME_CC_CSS_NVM | NVME_CC_AMS_RR | NVME_CC_SHN_NONE |
         NVME_CC_IOSQES | NVME_CC_IOCQES;
    DevExt->CtrlGen++;
    NvmeWriteReg32(DevExt, NVME_REG_CC, cc);
    DBGSTAGE(STG_HWINIT_CC, cc);
    LcSetStep(DevExt, ENG_BU_WAIT_RDY);
}

static VOID LcFailDisable(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG Reason)
{
    ULONG cc;

    DevExt->FailReason = Reason;
    DBGSTAGE(STG_BRINGUP_FAIL, Reason);
    cc = NvmeReadReg32(DevExt, NVME_REG_CC);
    NvmeWriteReg32(DevExt, NVME_REG_CC, cc & ~(NVME_CC_ENABLE | NVME_CC_SHN_MASK));
    LcSetStep(DevExt, ENG_BU_FAIL_DISABLE);
}

static VOID LcBringupDone(IN PHW_DEVICE_EXTENSION DevExt)
{
    ULONG lim;

    DevExt->IdentifyDone = TRUE;
    DevExt->ChildLimitIo = DevExt->IoQueue.QueueSize - 1;
    if (DevExt->ChildLimitIo > ENG_MAX_CHILDREN) {
        DevExt->ChildLimitIo = ENG_MAX_CHILDREN;
    }
    // Child size: the transfer limit reported to the port (the
    // StorMaxTransferKB / stormaxtransferkb= setting, at most
    // STOR98_MAX_TRANSFER), MDTS (1.4, units of CAP.MPSMIN = 4 KiB) and the
    // test fragment size.
    lim = DevExt->Tun.MaxXferKb * 1024;
    if (lim == 0 || lim > STOR98_MAX_TRANSFER) {
        lim = STOR98_MAX_TRANSFER;
    }
    if (DevExt->Mdts != 0 && DevExt->Mdts < 20 && ((ULONG)PAGE_SIZE << DevExt->Mdts) < lim) {
        lim = (ULONG)PAGE_SIZE << DevExt->Mdts;
    }
    if (DevExt->Tun.FragBytes != 0 && DevExt->Tun.FragBytes < lim) {
        lim = DevExt->Tun.FragBytes;
    }
    lim &= ~(PAGE_SIZE - 1);
    if (lim < PAGE_SIZE) {
        lim = PAGE_SIZE;
    }
    DevExt->MaxChildBytes = lim;
    DevExt->Bringup = ENG_BU_NONE;
    EngSetState(DevExt, ENG_ST_RUNNING, 0);
    DBGMARK(STG_BRINGUP_DONE, LcMsSince(DevExt, DevExt->InitTime));
    // Vector 0: INTx was enabled in the PCI command register by
    // HwFindAdapter; unmask it at the controller now, unless an interrupt
    // storm put the adapter on polling (EngInterrupt): then the timer's
    // unmask trial does it after the polling hold.
    if (!DevExt->PollMode) {
        NvmeWriteReg32(DevExt, NVME_REG_INTMC, 0x00000001);
    }
#ifdef STOR98_DBGPORT
    if (DevExt->Tun.StormTest) {
        // Test builds (stormtest=1): a redundant I/O CQ head doorbell on the
        // empty queue. QEMU's model then keeps INTx asserted after the next
        // completion (its pending count goes negative): an interrupt storm
        // with empty queues, for the storm recovery tests. The product never
        // writes a doorbell for entries it has not consumed.
        NvmeRingDoorbell(DevExt, DevExt->IoQueue.QueueId, FALSE,
                         (USHORT)(DevExt->IoQueue.CompletionQueueHead & DevExt->IoQueue.QueueSizeMask));
    }
#endif
}

static VOID LcBringupStep(IN PHW_DEVICE_EXTENSION DevExt)
{
    ULONG csts = LcCsts(DevExt);
    ULONG ms = LcTrustMs(DevExt, DevExt->StateTime, DevExt->StateSafe);

    switch (DevExt->Bringup) {
    case ENG_BU_DISABLING:
        if (csts == ENG_ALL_ONES) {
            EngEnterGone(DevExt, ENG_GONE_BRINGUP);
        } else if (!(csts & NVME_CSTS_RDY)) {
            DBGSTAGE(STG_DISABLE_ACK, ms);
            // The acknowledged disable is evidence 1 (PLAN 0.10): anything
            // still issued on the old queues (an internal Flush left over
            // from before a restartable shutdown) is released now, before
            // LcEnable re-creates the queues; otherwise its slot and pages
            // would stay taken and its CID could be reused under it.
            // Requests that never issued a child (held behind the
            // shutdown, or admitted and blocked by its barrier) are not
            // part of that: no reset happened to them, and they are
            // replayed or dispatched after the bring-up.
            if (EngIssuedChildren(DevExt) != 0 || DevExt->Quarantined != 0) {
                DBGSTAGE(STG_SETTLE_REENABLE, EngIssuedChildren(DevExt) | (DevExt->Quarantined << 16));
                EngChildrenSettle(DevExt, TRUE);
                LcFinishSettled(DevExt, TRUE);
            }
            LcEnable(DevExt);
        } else if (ms > DevExt->CapTimeoutMs) {
            // The previous owner's disable was never acknowledged.
            DevExt->FailReason = ENG_FAIL_DISABLE_TIMEOUT;
            DBGSTAGE(STG_BRINGUP_FAIL, ENG_FAIL_DISABLE_TIMEOUT);
            DevExt->Bringup = ENG_BU_NONE;
            EngSetState(DevExt, ENG_ST_QUARANTINED, ENG_FAIL_DISABLE_TIMEOUT);
            LcFailHeldParents(DevExt);
        }
        break;
    case ENG_BU_WAIT_RDY:
        if (csts == ENG_ALL_ONES) {
            EngEnterGone(DevExt, ENG_GONE_BRINGUP);
        } else if (csts & NVME_CSTS_CFS) {
            LcFailDisable(DevExt, ENG_FAIL_CFS);
        } else if (csts & NVME_CSTS_RDY) {
            DBGSTAGE(STG_RDY_SEEN, ms);
            LcSetStep(DevExt, ENG_BU_ADMIN);
            if (!NvmeCreateIoCQ(DevExt)) {
                LcFailDisable(DevExt, ENG_FAIL_SUBMIT);
            }
        } else if (ms > DevExt->CapTimeoutMs) {
            LcFailDisable(DevExt, ENG_FAIL_RDY_TIMEOUT);
        }
        break;
    case ENG_BU_ADMIN:
        if (csts == ENG_ALL_ONES) {
            EngEnterGone(DevExt, ENG_GONE_BRINGUP);
        } else if (csts & NVME_CSTS_CFS) {
            LcFailDisable(DevExt, ENG_FAIL_CFS);
        } else {
            NvmeProcessAdminCompletion(DevExt);
            if (DevExt->AdminChainDone) {
                LcBringupDone(DevExt);
            } else if (DevExt->AdminError != 0) {
                LcFailDisable(DevExt, ENG_FAIL_ADMIN_STATUS);
            } else if (ms > DevExt->CapTimeoutMs) {
                LcFailDisable(DevExt, ENG_FAIL_ADMIN_TIMEOUT);
            }
        }
        break;
    case ENG_BU_FAIL_DISABLE:
        if (csts == ENG_ALL_ONES) {
            EngEnterGone(DevExt, ENG_GONE_BRINGUP);
        } else if (!(csts & NVME_CSTS_RDY)) {
            DBGSTAGE(STG_DISABLE_ACK, ms);
            DevExt->Bringup = ENG_BU_NONE;
            EngSetState(DevExt, ENG_ST_FAILED, DevExt->FailReason);
            LcFailHeldParents(DevExt);
        } else if (ms > DevExt->CapTimeoutMs) {
            DevExt->Bringup = ENG_BU_NONE;
            EngSetState(DevExt, ENG_ST_QUARANTINED, DevExt->FailReason);
            LcFailHeldParents(DevExt);
        }
        break;
    default:
        break;
    }
}

//
// Queue capability validation (PLAN 0.9) and the checks of the bring-up
// rule that run before CC.EN. Returns 0 or an ENG_FAIL_* reason.
//
static ULONG LcValidateCaps(IN PHW_DEVICE_EXTENSION DevExt)
{
    ULONGLONG cap = DevExt->ControllerCapabilities;
    ULONG capHi = (ULONG)(cap >> 32);
    ULONG mqes = (ULONG)(cap & NVME_CAP_MQES_MASK) + 1;
    ULONG dstrd = capHi & 0xF;
    ULONG mpsmin = (capHi >> 16) & 0xF;
    ULONG css = (capHi >> 5) & 0xFF;        // CAP.CSS, bits 44:37
    ULONG qsize = NVME_MAX_QUEUE_SIZE;
    ULONG lastDoorbell;
    ULONG result = 0;

    if (qsize > mqes) {
        qsize = mqes;
    }
    // Largest power of two not above the limit (the phase bit arithmetic
    // needs one).
    while (qsize & (qsize - 1)) {
        qsize &= qsize - 1;
    }
    DevExt->DoorbellStride = 4UL << dstrd;
    // Highest doorbell used: I/O queue 1 completion head.
    lastDoorbell = NVME_REG_DBS + 3 * DevExt->DoorbellStride + 4;
    if (qsize < 2) {
        result = 1;
    } else if (mpsmin != 0) {
        result = 2;             // 4 KiB host pages not supported
    } else if (!(css & 0x01)) {
        result = 3;             // no NVM command set
    } else if (DevExt->ControllerRegistersLength != 0 && lastDoorbell > DevExt->ControllerRegistersLength) {
        result = 4;             // doorbells outside the BAR
    }
    // MQES is 0-based: 65535 means 65536 entries, which a USHORT cannot hold
    // (informational here; the queue size above is at most NVME_MAX_QUEUE_SIZE).
    DevExt->MaxQueueEntries = (USHORT)(mqes > 0xFFFF ? 0xFFFF : mqes);
    DevExt->AdminQueue.QueueSize = (USHORT)qsize;
    DevExt->AdminQueue.QueueId = 0;
    DevExt->IoQueue.QueueSize = (USHORT)qsize;
    DevExt->IoQueue.QueueId = 1;
    DevExt->AdminQueue.QueueSizeBits = (UCHAR)log2(qsize);
    DevExt->AdminQueue.QueueSizeMask = (USHORT)(qsize - 1);
    DevExt->IoQueue.QueueSizeBits = (UCHAR)log2(qsize);
    DevExt->IoQueue.QueueSizeMask = (USHORT)(qsize - 1);
    DevExt->PageSize = PAGE_SIZE;
    DevExt->CapTimeoutMs = ((((ULONG)cap) >> 24) & 0xFF) * 500;   // CAP.TO, 500 ms units
    if (DevExt->CapTimeoutMs < 1000) {
        DevExt->CapTimeoutMs = 1000;
    }
#ifdef STOR98_DBGPORT
    // Test builds (capto=, or STOR98_TEST_CAPTO_MS where no ArgumentString
    // arrives, 9x): a longer bring-up and disable wait than the emulated
    // controller's CAP.TO (7.5 s), to hold a bring-up past a port's reset
    // threshold. Never shorter than CAP.TO.
    if (DevExt->Tun.CapToMs > DevExt->CapTimeoutMs) {
        DevExt->CapTimeoutMs = DevExt->Tun.CapToMs;
    }
#endif
    DBGSTAGE(STG_QUEUE_CAPS, qsize | (dstrd << 16) | (mpsmin << 20) | (result << 24));
    return result ? ENG_FAIL_CAPS : 0;
}

// Carves the queues and pools once per HwFindAdapter (PLAN 0.5): every
// block page aligned against its physical address.
static BOOLEAN LcCarve(IN PHW_DEVICE_EXTENSION DevExt)
{
    ULONG i;

    if (DevExt->QueuesAllocated) {
        return TRUE;
    }
    if (!PlatDmaAlloc(DevExt, PAGE_SIZE, PLAT_USE_ADMIN_SQ, &DevExt->AdminQueue.SubmissionQueue,
                      &DevExt->AdminQueue.SubmissionQueuePhys) ||
        !PlatDmaAlloc(DevExt, PAGE_SIZE, PLAT_USE_ADMIN_CQ, &DevExt->AdminQueue.CompletionQueue,
                      &DevExt->AdminQueue.CompletionQueuePhys) ||
        !PlatDmaAlloc(DevExt, PAGE_SIZE, PLAT_USE_IO_SQ, &DevExt->IoQueue.SubmissionQueue,
                      &DevExt->IoQueue.SubmissionQueuePhys) ||
        !PlatDmaAlloc(DevExt, PAGE_SIZE, PLAT_USE_IO_CQ, &DevExt->IoQueue.CompletionQueue,
                      &DevExt->IoQueue.CompletionQueuePhys) ||
        !PlatDmaAlloc(DevExt, PAGE_SIZE, PLAT_USE_UTILITY, &DevExt->UtilityBuffer,
                      &DevExt->UtilityBufferPhys) ||
        !PlatDmaAlloc(DevExt, ENG_PRP_PAGES * PAGE_SIZE, PLAT_USE_PRP, &DevExt->PrpListPages,
                      &DevExt->PrpListPagesPhys) ||
        !PlatDmaAlloc(DevExt, ENG_BOUNCE_PAGES * PAGE_SIZE, PLAT_USE_BOUNCE, &DevExt->BouncePages,
                      &DevExt->BouncePagesPhys)) {
        return FALSE;
    }
    // Every pool page translated on its own and validated (PLAN 0.4).
    for (i = 0; i < ENG_PRP_PAGES + ENG_BOUNCE_PAGES; i++) {
        PUCHAR va = (i < ENG_PRP_PAGES) ? (PUCHAR)DevExt->PrpListPages + i * PAGE_SIZE :
                                          (PUCHAR)DevExt->BouncePages + (i - ENG_PRP_PAGES) * PAGE_SIZE;
        PHYSICAL_ADDRESS pa;

        if (!PlatDmaPagePa(DevExt, va, &pa) || (pa.LowPart & (PAGE_SIZE - 1)) != 0) {
            return FALSE;
        }
        if (i < ENG_PRP_PAGES) {
            DevExt->PrpPagePa[i] = pa;
        } else {
            DevExt->BouncePagePa[i - ENG_PRP_PAGES] = pa;
        }
    }
    DevExt->QueuesAllocated = TRUE;
    return TRUE;
}

// Engine and lifecycle software state, reset on every start (PLAN 0.5:
// "lifecycle flags reset on every start").
VOID EngResetSoftState(IN PHW_DEVICE_EXTENSION DevExt)
{
    ULONG i;

    for (i = 0; i < ENG_MAX_PARENTS; i++) {
        DevExt->Parents[i].State = ENG_PA_FREE;
        DevExt->Parents[i].Srb = NULL;
    }
    for (i = 0; i < ENG_MAX_CHILDREN; i++) {
        DevExt->Children[i].State = ENG_CH_FREE;
        DevExt->Children[i].Parent = ENG_NONE;
        DevExt->Children[i].PrpPage = ENG_NONE;
        DevExt->Children[i].BouncePage = ENG_NONE;
        DevExt->Children[i].BounceCount = 0;
    }
    for (i = 0; i < ENG_BUSY_TRACK; i++) {
        DevExt->Busy[i].Srb = NULL;
        DevExt->Busy[i].Count = 0;
    }
    DevExt->PrpListPageBitmap = 0;
    DevExt->PrpQuarantineBitmap = 0;
    DevExt->CurrentPrpListPagesUsed = 0;
    DevExt->BounceBitmap = 0;
    DevExt->BounceQuarantineBitmap = 0;
    DevExt->ChildrenIo = 0;
    DevExt->ChildrenAdmin = 0;
    DevExt->ChildLimitIo = 0;
    DevExt->Quarantined = 0;
    DevExt->QuarantinedAdmin = 0;
    DevExt->AdminCqLive = FALSE;
    DevExt->OutstandingWrites = 0;
    DevExt->IdleFlushOutstanding = FALSE;
    DevExt->StopFlushOutstanding = FALSE;
    DevExt->ShnFlushOk = FALSE;
    DevExt->ShnShstOk = FALSE;
    DevExt->NextOwed = FALSE;
    DevExt->NextLuOwed = FALSE;
    DevExt->InDispatch = FALSE;
    DevExt->ReplayAdmitTime = 0;
    DevExt->CapWaitLogged = FALSE;
    DevExt->TimerActive = FALSE;
    DevExt->TimerFirstSeen = FALSE;
    DevExt->BusChangeSent = FALSE;
    DevExt->ContactLogged = FALSE;
    DevExt->GoneDeadlineTaken = FALSE;
    DevExt->ResetIsFailure = FALSE;
    DevExt->ResetIsReopen = FALSE;
    // The polling mode (no usable interrupt, or PollMode forced) polls from
    // the first tick of every start; a storm's polling ends with the start.
    DevExt->PollMode = DevExt->PollAlways;
    DevExt->PollIdleLogged = FALSE;
    DevExt->PollHoldMs = 0;
    DevExt->EmptyInts = 0;
    DevExt->AdminChainDone = FALSE;
    // Identity comes from this start's bring-up only (PLAN 0.10: no disk
    // is exposed if identify never succeeded): a resume whose bring-up
    // fails must not answer the scan INQUIRY or READ CAPACITY from the
    // previous life.
    DevExt->IdentifyDone = FALSE;
    DevExt->NamespaceSizeInBlocks = 0;
    DevExt->NamespaceBlockSize = 0;
    DevExt->FailReason = 0;
    DevExt->AdminError = 0;
    DevExt->Bringup = ENG_BU_NONE;
    DevExt->State = ENG_ST_NONE;
    // Sense kept for requests of an earlier start does not survive it.
    ScsiSenseReset(DevExt);
    // Nothing was written since the last start: clean (0.10).
    DevExt->CleanGen = DevExt->WriteGen;
}

//
// HwInitialize (G1; resume: G7, R6-7)
//
BOOLEAN EngInitialize(IN PHW_DEVICE_EXTENSION DevExt)
{
    ULONG csts, cc, fail;

    EngResetSoftState(DevExt);
    DevExt->InitTime = LcNow(DevExt);
    csts = LcCsts(DevExt);
    DBGMARK(STG_HWINIT_ENTER, csts);

    if (csts == ENG_ALL_ONES) {
        // Presence lost before CC.EN was written. At boot or start the
        // failure may be returned; on resume never (R6-7): gone instead.
        DBGSTAGE(STG_BRINGUP_FAIL, ENG_FAIL_PRESENCE);
        if (!DevExt->Resumed) {
            DBGSTAGE(STG_HWINIT_RETURN, 0);
            return FALSE;
        }
        EngSetState(DevExt, ENG_ST_STARTING, 0);
        EngEnterGone(DevExt, ENG_GONE_BRINGUP);
        DBGSTAGE(STG_HWINIT_RETURN, 1 | ((ULONG)DevExt->State << 8) | (1UL << 16));
        return TRUE;
    }

    NvmeWriteReg32(DevExt, NVME_REG_INTMS, 0xFFFFFFFF);
    DevExt->ControllerCapabilities = NvmeReadReg64(DevExt, NVME_REG_CAP);
    DevExt->Version = NvmeReadReg32(DevExt, NVME_REG_VS);
    DBGSTAGE(STG_HWINIT_CAP, (ULONG)DevExt->ControllerCapabilities);
    cc = NvmeReadReg32(DevExt, NVME_REG_CC);
    if ((ULONG)DevExt->ControllerCapabilities == ENG_ALL_ONES ||
        (ULONG)(DevExt->ControllerCapabilities >> 32) == ENG_ALL_ONES || cc == ENG_ALL_ONES ||
        LcCsts(DevExt) == ENG_ALL_ONES) {
        // Presence lost between the reads (a bridge stopped decoding, the
        // link went down): never derive a wait from an all-ones CAP (CAP.TO
        // 0xFF = 127.5 s). Gone, exactly as for an all-ones CSTS above.
        DBGSTAGE(STG_BRINGUP_FAIL, ENG_FAIL_PRESENCE);
        if (!DevExt->Resumed) {
            DBGSTAGE(STG_HWINIT_RETURN, 0);
            return FALSE;
        }
        EngSetState(DevExt, ENG_ST_STARTING, 0);
        EngEnterGone(DevExt, ENG_GONE_BRINGUP);
        DBGSTAGE(STG_HWINIT_RETURN, 1 | ((ULONG)DevExt->State << 8) | (1UL << 16));
        return TRUE;
    }

    fail = LcValidateCaps(DevExt);
    if (fail == 0 && (csts & NVME_CSTS_CFS) && !(cc & NVME_CC_ENABLE)) {
        fail = ENG_FAIL_CFS;
    }
    if (fail == 0 && !LcCarve(DevExt)) {
        fail = ENG_FAIL_CAPS;
    }
    if (fail != 0) {
        // Nothing written to CC: failure may be returned at start, but on
        // resume the adapter stays started in `failed` (R6-7).
        DevExt->FailReason = fail;
        DBGSTAGE(STG_BRINGUP_FAIL, fail);
        if (!DevExt->Resumed) {
            DBGSTAGE(STG_HWINIT_RETURN, 0);
            return FALSE;
        }
        EngSetState(DevExt, ENG_ST_FAILED, fail);
        DBGSTAGE(STG_HWINIT_RETURN, 1 | ((ULONG)DevExt->State << 8) | (1UL << 16));
        return TRUE;
    }

    EngSetState(DevExt, ENG_ST_STARTING, 0);
    if ((csts & NVME_CSTS_RDY) || (cc & NVME_CC_ENABLE)) {
        // A previous owner (option ROM, INT 13h, this driver before a
        // resume) left the controller enabled: the disable is acknowledged
        // before anything is reused (PLAN 0.10 reopen invariant, 2.6).
        NvmeWriteReg32(DevExt, NVME_REG_CC, cc & ~(NVME_CC_ENABLE | NVME_CC_SHN_MASK));
        DBGSTAGE(STG_HWINIT_CC, cc & ~(NVME_CC_ENABLE | NVME_CC_SHN_MASK));
        LcSetStep(DevExt, ENG_BU_DISABLING);
    } else {
        LcEnable(DevExt);
    }
    EngTimerEnsure(DevExt, ENG_TICK_FAST_US);
    DBGSTAGE(STG_HWINIT_RETURN, 1 | ((ULONG)DevExt->State << 8) | ((ULONG)DevExt->Resumed << 16));
    return TRUE;
}

//
// Gone state (PLAN 0.10, 2.1, 2.2; G2, G9; MD-1)
//
VOID EngEnterGone(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG Source)
{
    ULONG i;
    PENG_PARENT p;
    UCHAR old = DevExt->State;

    if (old == ENG_ST_GONE || old == ENG_ST_STOPPED || old == ENG_ST_STOPPING) {
        return;
    }
    DBGMARK(STG_GONE_ENTER, (EngIssuedChildren(DevExt) & 0xFFFF) | (Source << 16) | ((ULONG)old << 24));
    DevExt->Bringup = ENG_BU_NONE;
    EngSetState(DevExt, ENG_ST_GONE, Source);
    DevExt->GoneTime = LcNow(DevExt);
    DevExt->GoneSafe = LcSafe(DevExt);
    DevExt->BusChangeSent = FALSE;
    DevExt->ContactLogged = FALSE;
    // A new presence loss: a reopen wait that was running (resetting or
    // quarantined after a reopen violation) is over; the CM statement or
    // the MD-1 deadline of this gone episode decides from here.
    DevExt->ResetIsReopen = FALSE;
    DevExt->GoneDeadlineTaken = FALSE;
    DevExt->GoneSummarySec = 0;
    DevExt->GoneDeadlineLogged = FALSE;
    DevExt->GoneEvidence = FALSE;

    for (i = 0; i < ENG_MAX_PARENTS; i++) {
        p = &DevExt->Parents[i];
        if (p->State == ENG_PA_FREE) {
            continue;
        }
        if (p->State == ENG_PA_PARKED) {
            if (p->Kind == ENG_K_RESET) {
                // A reset that can no longer be acknowledged.
                p->ErrSrbStatus = SRB_STATUS_ERROR;
                p->ErrScsiStatus = SCSISTAT_GOOD;
                EngFinishParent(DevExt, i);
            }
            continue;       // an ABORT waits for its target
        }
        if (p->Outstanding == 0) {
            // Nothing of it reaches the device: terminal status now.
            p->Cancel = ENG_CANCEL_GONE;
            EngFinishParent(DevExt, i);
            continue;
        }
        // Issued children: held, with their buffers owned, until evidence
        // or the deadline; nothing more is dispatched (cancellation, H1),
        // and a SHUTDOWN's CC.SHN step is suppressed with the rest.
        if (p->BlocksLeft != 0 || p->State == ENG_PA_WAITING || p->Kind == ENG_K_SHUTDOWN) {
            p->Cancel = ENG_CANCEL_GONE;
            p->BlocksLeft = 0;
        }
        p->State = ENG_PA_ISSUED;
    }
    EngTimerEnsure(DevExt, ENG_TICK_WATCH_US);
}

#ifndef NVME2K_W9X
static VOID LcBusChange(IN PHW_DEVICE_EXTENSION DevExt)
{
    if (!DevExt->BusChangeSent) {
        DevExt->BusChangeSent = TRUE;
        ScsiPortNotification(BusChangeDetected, DevExt, 0);
        DBGSTAGE(STG_BUS_CHANGE, DevExt->State);
    }
}
#else
// IOS has no BusChangeDetected (G9): CONFIGMG, driven by xhci98's CM, removes
// the volume.
#define LcBusChange(d)
#endif

// The deadline of a held parent: the gone deadline, and on NT also its own
// start + TimeOutValue - margin (R6-6), whichever comes first. TRUE if it
// has passed; *PerSrb tells which. Both release the caller's buffer without
// isolation evidence (MD-1), so both are measured with the trustworthy
// elapsed time (LcTrustMs: never early, clock.h). The per-SRB bound keeps
// its measured margin before the port's timeout; should the trustworthy
// clock run so slow that the port's timer fires first, HwResetBus in `gone`
// returns FALSE with the SRB still held (G4), which is safe where an early
// release is not (review round 2: the larger estimate, used here before,
// let a fast TSC release a buffer about a second after admission).
static BOOLEAN LcGoneDue(IN PHW_DEVICE_EXTENSION DevExt, IN PENG_PARENT p, OUT PBOOLEAN PerSrb)
{
    ULONG goneMs = LcTrustMs(DevExt, DevExt->GoneTime, DevExt->GoneSafe);

    *PerSrb = FALSE;
    if (goneMs >= DevExt->Tun.GoneMs) {
        return TRUE;
    }
#ifndef NVME2K_W9X
    if (p->Srb != NULL && p->Srb->TimeOutValue != 0 && p->Srb->TimeOutValue <= 3600) {
        ULONG limit = p->Srb->TimeOutValue * 1000;
        ULONG margin = limit / 2;

        // Margin as for the hold: at least GoneMarginMs, and half the
        // TimeOutValue against an elapsed-time estimate that can run slow.
        if (margin < DevExt->Tun.GoneMarginMs) {
            margin = DevExt->Tun.GoneMarginMs;
        }
        limit = (limit > margin) ? limit - margin : 0;
        if (LcTrustMs(DevExt, p->AdmitTime, p->AdmitSafe) >= limit) {
            *PerSrb = TRUE;
            return TRUE;
        }
    }
#else
    UNREFERENCED_PARAMETER(p);
#endif
    return FALSE;
}

// Isolation evidence 1 or 2 in `gone` (PLAN 0.10): every held SRB completes
// (acknowledged reset: BUS_RESET; path down: terminal status) and the DMA
// memory is released.
static VOID LcGoneEvidence(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG Evidence)
{
    ULONG i;
    PENG_PARENT p;

    DBGMARK(STG_EVIDENCE, Evidence | ((ULONG)DevExt->State << 8));
    // Evidence cancels the MD-1 gone deadline for good (TESTING "Gone
    // deadline, no evidence", variant: the path-down row applies and the
    // counter is unchanged). LcGoneStep used to keep timing `gone` after a
    // DOWN_QUIESCED and fired STG_GONE_DEADLINE about 11 s later on Me
    // (integration finding): nothing was left to release, but the marker,
    // the quarantine of anything issued since and the rescan claim were
    // wrong.
    DevExt->GoneEvidence = TRUE;
    EngChildrenSettle(DevExt, TRUE);
    for (i = 0; i < ENG_MAX_PARENTS; i++) {
        p = &DevExt->Parents[i];
        if (p->State == ENG_PA_FREE || p->State == ENG_PA_PARKED) {
            continue;
        }
        p->Cancel = (UCHAR)(Evidence == 1 ? ENG_CANCEL_RESET : ENG_CANCEL_GONE);
        EngFinishParent(DevExt, i);
    }
}

static VOID LcGoneStep(IN PHW_DEVICE_EXTENSION DevExt)
{
    ULONG i, csts, cc, done = 0, perSrbDone = 0;
    PENG_PARENT p;
    BOOLEAN perSrb, held = FALSE;
    ULONG goneMs = LcTrustMs(DevExt, DevExt->GoneTime, DevExt->GoneSafe);

    // Completions still posted (the DMA path may be open): each one is the
    // command-level quiescence of its own child.
    EngProcessCompletions(DevExt);

    csts = LcCsts(DevExt);
    if (csts != ENG_ALL_ONES) {
        if (!DevExt->ContactLogged) {
            DevExt->ContactLogged = TRUE;
            DBGSTAGE(STG_GONE_CONTACT, csts);
        }
        // Reopen invariant (R4-3): trust nothing until the controller is
        // shown fresh (CC.EN=0 and RDY=0).
        cc = NvmeReadReg32(DevExt, NVME_REG_CC);
        if (cc != ENG_ALL_ONES) {
            if ((cc & NVME_CC_ENABLE) || (csts & NVME_CSTS_RDY)) {
                // Isolation-contract violation: the controller kept its old
                // queues across the gap and is present, so it may still DMA.
                // The MD-1 deadline (presence lost, no evidence) does not
                // apply to it. CC.EN=0 is written and the RDY=0 wait is
                // carried by `resetting` as for a reset: acknowledged within
                // CAP.TO, the held SRBs complete and the adapter fails
                // (LcResetAcked; reconnect is PLAN 2.6); otherwise the
                // unresponsive row (`quarantined`: SRBs stay owned, memory
                // quarantined, CC.EN=0 written again at the low rate).
                // Nothing is dispatched or re-created before the
                // acknowledgement. Evidence 2 still wins (LcLinkStatement),
                // and a new presence loss starts a new gone episode.
                DBGMARK(STG_ISOLATION_VIOLATION, (cc & 0xFF) | ((csts & 0xFF) << 8));
                NvmeWriteReg32(DevExt, NVME_REG_CC, cc & ~(NVME_CC_ENABLE | NVME_CC_SHN_MASK));
                DevExt->ResetIsFailure = FALSE;
                DevExt->ResetIsReopen = TRUE;
                DevExt->ResetStartTime = LcNow(DevExt);
                DevExt->ResetStartSafe = LcSafe(DevExt);
                LcEnterResetting(DevExt, ENG_FAIL_REOPEN);
                EngTimerEnsure(DevExt, ENG_TICK_FAST_US);
                return;
            } else {
                // Evidence 1. Reconnect with the identity check of PLAN 2.6
                // is phase 2: until then the adapter stays failed.
                LcGoneEvidence(DevExt, 1);
                EngSetState(DevExt, ENG_ST_FAILED, 0);
                return;
            }
        }
    }
    if (DevExt->GoneEvidence) {
        // Path down already applied: no deadline any more. Anything that
        // reached the engine since completes with the terminal status.
        for (i = 0; i < ENG_MAX_PARENTS; i++) {
            p = &DevExt->Parents[i];
            if (p->State != ENG_PA_FREE && p->State != ENG_PA_PARKED && p->Outstanding == 0) {
                p->Cancel = ENG_CANCEL_GONE;
                EngFinishParent(DevExt, i);
            }
        }
        return;
    }
    // A DOWN_QUIESCED statement (evidence 2) is taken by EngTimer before
    // this step (LcLinkStatement, K1).

    for (i = 0; i < ENG_MAX_PARENTS; i++) {
        p = &DevExt->Parents[i];
        if (p->State == ENG_PA_FREE || p->State == ENG_PA_PARKED) {
            continue;
        }
        if (p->Outstanding == 0) {
            p->Cancel = ENG_CANCEL_GONE;
            EngFinishParent(DevExt, i);
            continue;
        }
        if (!LcGoneDue(DevExt, p, &perSrb)) {
            held = TRUE;
            continue;
        }
        // MD-1 outcome: the SRB completes with the terminal status; its DMA
        // memory stays quarantined.
        DBGSTAGE(STG_GONE_HELD_DONE, SRB_STATUS_NO_DEVICE | ((ULONG)p->Srb->Cdb[0] << 8) | (i << 16) |
                 ((ULONG)perSrb << 24));
        EngQuarantineParentChildren(DevExt, i);
        p->Cancel = ENG_CANCEL_GONE;
        EngFinishParent(DevExt, i);
        done++;
        if (perSrb) {
            perSrbDone++;
        }
    }
    if (done != 0 && !DevExt->GoneDeadlineTaken) {
        DevExt->GoneDeadlineTaken = TRUE;
        DevExt->GoneDeadlineCount++;
    }
    if (goneMs >= DevExt->Tun.GoneMs) {
        if (EngIssuedChildren(DevExt) != 0) {
            // Internal commands (idle flush): quarantined too.
            EngChildrenSettle(DevExt, FALSE);
        }
        if (!DevExt->GoneDeadlineLogged) {
            DevExt->GoneDeadlineLogged = TRUE;
            DBGMARK(STG_GONE_DEADLINE, (done & 0xFFFF) | ((DevExt->GoneDeadlineCount & 0xFFFF) << 16));
        }
    }
    if (!held && (done != 0 || goneMs >= DevExt->Tun.GoneMs)) {
        // Nothing held any more: the port may rescan now (2.2).
        LcBusChange(DevExt);
    }
    if (held && goneMs / 1000 != DevExt->GoneSummarySec) {
        DevExt->GoneSummarySec = goneMs / 1000;
        DBGSTAGE(STG_GONE_SUMMARY, (EngIssuedChildren(DevExt) & 0xFF) | ((LcNonFreeParents(DevExt) & 0xFF) << 8) |
                 ((goneMs / 1000) << 16));
    }
    UNREFERENCED_PARAMETER(perSrbDone);
}

//
// Reset (PLAN 0.10 entry points, G4)
//
static VOID LcResetAcked(IN PHW_DEVICE_EXTENSION DevExt, IN BOOLEAN Late)
{
    ULONG i, done = 0;
    PENG_PARENT p;
    ULONG waited = LcMsSince(DevExt, DevExt->ResetStartTime);

    DBGMARK(STG_EVIDENCE, 1 | ((ULONG)DevExt->State << 8));
    EngChildrenSettle(DevExt, TRUE);
    // An acknowledged reset ends whatever sense was kept (sense.c).
    ScsiSenseReset(DevExt);
    for (i = 0; i < ENG_MAX_PARENTS; i++) {
        p = &DevExt->Parents[i];
        if (p->State == ENG_PA_FREE) {
            continue;
        }
        if (p->Kind == ENG_K_ABORT) {
            continue;       // completed with its target
        }
        if (p->Kind == ENG_K_RESET) {
            // The reset SRB itself: SUCCESS (or ERROR for a CFS failure).
            if (DevExt->ResetIsFailure) {
                p->ErrSrbStatus = SRB_STATUS_ERROR;
            }
        } else if (DevExt->ResetIsFailure) {
            // Controller fatal: parent error mapping, HARDWARE ERROR 04/44/00.
            p->ErrSrbStatus = SRB_STATUS_ERROR;
            p->ErrScsiStatus = SCSISTAT_CHECK_CONDITION;
            p->SenseKey = SCSI_SENSE_HARDWARE_ERROR;
            p->Asc = 0x44;
            p->Ascq = 0;
            p->Flags &= ~ENG_PF_PRESET;
        } else {
            p->Cancel = ENG_CANCEL_RESET;
        }
        EngFinishParent(DevExt, i);
        done++;
    }
    if (Late) {
        DBGMARK(STG_RESET_ACK, (waited & 0xFFFF) | (done << 16));
        ScsiPortNotification(ResetDetected, DevExt, NULL);
    }
    if (DevExt->ResetIsReopen) {
        // The disable after a reopen violation in `gone` (LcGoneStep): the
        // controller is fresh now, as one found fresh in `gone`, and the
        // adapter stays failed (reconnect with the identity check of PLAN
        // 2.6 is phase 2). Nothing is re-created.
        DevExt->ResetIsReopen = FALSE;
        EngSetState(DevExt, ENG_ST_FAILED, ENG_FAIL_REOPEN);
        return;
    }
    if (DevExt->ResetIsFailure) {
        DevExt->ResetIsFailure = FALSE;
        EngSetState(DevExt, ENG_ST_FAILED, ENG_FAIL_CFS);
        return;
    }
    // Re-create the queues from the timer (the disable is acknowledged, so
    // nothing of the old controller execution can complete any more).
    DevExt->InitTime = LcNow(DevExt);
    EngSetState(DevExt, ENG_ST_STARTING, 0);
    LcEnable(DevExt);
    EngTimerEnsure(DevExt, ENG_TICK_FAST_US);
}

// Completes every parent with nothing issued (no DMA possible) with BUS_RESET.
static VOID LcResetUnissued(IN PHW_DEVICE_EXTENSION DevExt)
{
    ULONG i;
    PENG_PARENT p;

    for (i = 0; i < ENG_MAX_PARENTS; i++) {
        p = &DevExt->Parents[i];
        if (p->State == ENG_PA_FREE || p->Outstanding != 0 || p->Kind == ENG_K_ABORT) {
            continue;
        }
        if (p->Kind != ENG_K_RESET) {
            p->Cancel = ENG_CANCEL_RESET;
        }
        EngFinishParent(DevExt, i);
    }
}

// Writes CC.EN=0 and polls RDY within the stall budget. Returns the last
// CSTS read.
static ULONG LcDisablePoll(IN PHW_DEVICE_EXTENSION DevExt)
{
    ULONG cc, csts, i;

    cc = NvmeReadReg32(DevExt, NVME_REG_CC);
    NvmeWriteReg32(DevExt, NVME_REG_CC, cc & ~(NVME_CC_ENABLE | NVME_CC_SHN_MASK));
    csts = LcCsts(DevExt);
    for (i = 0; i < ENG_STALL_BUDGET; i++) {
        if (csts == ENG_ALL_ONES || !(csts & NVME_CSTS_RDY)) {
            break;
        }
        ScsiPortStallExecution(ENG_STALL_US);
        csts = LcCsts(DevExt);
    }
    return csts;
}

BOOLEAN EngResetBus(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG PathId)
{
    ULONG csts = 0, cc;
    BOOLEAN ret = FALSE;
    ULONG issued = EngIssuedChildren(DevExt);

    DevExt->ResetCalls++;
    DBGMARK(STG_RESET_BUS, (PathId & 0xFF) | ((issued & 0xFF) << 8) | ((ULONG)DevExt->State << 16) |
            ((DevExt->ResetCalls & 0xFF) << 24));
    // A terminal instance (9x removal request accepted, R9d) never resets
    // or re-enables the controller: FALSE, nothing completed.
    if (PlatCmRemoveActive(DevExt)) {
        return FALSE;
    }

    switch (DevExt->State) {
    case ENG_ST_GONE:
        // No waits on absent hardware and no evidence: FALSE, held SRBs
        // stay owned (G4; on 9x the same, PLAN 0.10).
        csts = ENG_ALL_ONES;
        break;
    case ENG_ST_QUARANTINED:
        csts = LcCsts(DevExt);
        if (csts == ENG_ALL_ONES) {
            EngEnterGone(DevExt, ENG_GONE_RESET);
        } else if (issued == 0 && DevExt->Quarantined == 0) {
            LcResetUnissued(DevExt);
            ret = TRUE;
        }
        break;
    case ENG_ST_RESETTING:
        // Watchdog recovery already running: O(1) reads, then the same rule.
        csts = LcCsts(DevExt);
        cc = NvmeReadReg32(DevExt, NVME_REG_CC);
        if (csts == ENG_ALL_ONES) {
            EngEnterGone(DevExt, ENG_GONE_RESET);
        } else if (!(csts & NVME_CSTS_RDY) && !(cc & NVME_CC_ENABLE)) {
            LcResetAcked(DevExt, FALSE);
            ret = TRUE;
        }
        break;
    case ENG_ST_RUNNING:
        if (issued == 0) {
            LcResetUnissued(DevExt);
            ret = TRUE;
            break;
        }
        DevExt->ResetStartTime = LcNow(DevExt);
        DevExt->ResetStartSafe = LcSafe(DevExt);
        csts = LcDisablePoll(DevExt);
        if (csts == ENG_ALL_ONES) {
            EngEnterGone(DevExt, ENG_GONE_RESET);
        } else if (!(csts & NVME_CSTS_RDY)) {
            LcResetAcked(DevExt, FALSE);
            ret = TRUE;
        } else {
            // Not acknowledged within the budget: SRBs stay owned, the timer
            // continues the disable wait (watchdog recovery).
            LcEnterResetting(DevExt, 0);
            EngTimerEnsure(DevExt, ENG_TICK_FAST_US);
        }
        break;
    default:
#ifdef NVME2K_W9X
        if (DevExt->State == ENG_ST_STARTING) {
            // 9x bring-up (Q-9x, G1 on 9x): FALSE, and nothing completed.
            // The held scan INQUIRY must stay held: completed BUS_RESET
            // after a TRUE reset it makes the port fail ScsiPortInitialize
            // and unload the adapter (measured on Me). The port calls
            // HwResetBus again about every 7.5 s and never completes the
            // held SRB itself (G4 on 9x); bring-up ends within CAP.TO.
            break;
        }
#endif
        // starting, shutdown, failed, stopped: no child is issued in these
        // states, so completing the held SRBs touches no DMA.
        if (issued == 0) {
            LcResetUnissued(DevExt);
            ret = TRUE;
        }
        break;
    }
    // The port withholds requests after a reset; the engine's NextRequest
    // follows its admission rule.
    EngKick(DevExt);
    DBGSTAGE(STG_RESET_RESULT, (ULONG)ret | ((csts & 0xFF) << 8) | ((ULONG)DevExt->State << 16) |
             ((EngIssuedChildren(DevExt) & 0xFF) << 24));
    return ret;
}

static VOID LcResettingStep(IN PHW_DEVICE_EXTENSION DevExt)
{
    ULONG csts = LcCsts(DevExt);
    ULONG cc, i;

    if (csts == ENG_ALL_ONES) {
        EngEnterGone(DevExt, ENG_GONE_TIMER);
        return;
    }
    // Completions posted before the disable took effect (lost interrupts):
    // each is its child's command-level quiescence.
    EngProcessCompletions(DevExt);
    cc = NvmeReadReg32(DevExt, NVME_REG_CC);
    if (!(csts & NVME_CSTS_RDY) && !(cc & NVME_CC_ENABLE)) {
        LcResetAcked(DevExt, TRUE);
    } else if (LcTrustMs(DevExt, DevExt->ResetStartTime, DevExt->ResetStartSafe) > DevExt->CapTimeoutMs) {
        // Unresponsive (PLAN 0.10): SRBs whose buffers the device can still
        // reach stay owned; memory quarantined; a low-rate check continues.
        DevExt->FailReason = ENG_FAIL_RESET_TIMEOUT;
        DBGSTAGE(STG_BRINGUP_FAIL, ENG_FAIL_RESET_TIMEOUT);
        EngChildrenSettle(DevExt, FALSE);
        EngSetState(DevExt, ENG_ST_QUARANTINED, ENG_FAIL_RESET_TIMEOUT);
        LcFailHeldParents(DevExt);
        LcFailParkedResets(DevExt);
        // The reset suppressed whatever the held parents had not dispatched
        // yet (H1): they can no longer complete with success.
        for (i = 0; i < ENG_MAX_PARENTS; i++) {
            if (DevExt->Parents[i].State != ENG_PA_FREE && DevExt->Parents[i].State != ENG_PA_PARKED) {
                EngCancelParent(DevExt, i, ENG_CANCEL_RESET);
            }
        }
    }
}

// Parents whose children were all released by evidence: complete them
// (BUS_RESET for whatever they had not finished). KeepUnissued: a parent
// that never issued a child and is not cancelled stays (the re-enable after
// a restartable shutdown replays it).
static VOID LcFinishSettled(IN PHW_DEVICE_EXTENSION DevExt, IN BOOLEAN KeepUnissued)
{
    ULONG i;
    PENG_PARENT p;

    for (i = 0; i < ENG_MAX_PARENTS; i++) {
        p = &DevExt->Parents[i];
        if (p->State == ENG_PA_FREE || p->State == ENG_PA_PARKED || p->Outstanding != 0) {
            continue;
        }
        if (KeepUnissued && p->Issued == 0 && p->Cancel == ENG_CANCEL_NONE) {
            continue;
        }
        if (p->Cancel == ENG_CANCEL_NONE) {
            p->Cancel = ENG_CANCEL_RESET;
        }
        EngFinishParent(DevExt, i);
    }
}

static VOID LcQuarantineStep(IN PHW_DEVICE_EXTENSION DevExt)
{
    ULONG csts = LcCsts(DevExt);
    ULONG cc;

    if (csts == ENG_ALL_ONES) {
        EngEnterGone(DevExt, ENG_GONE_TIMER);
        return;
    }
    // Late completions of a controller that recovers (also from the ISR):
    // command-level quiescence of their own children, so their parents'
    // counts drop and their memory leaves the quarantine.
    EngProcessCompletions(DevExt);
    cc = NvmeReadReg32(DevExt, NVME_REG_CC);
    if (!(csts & NVME_CSTS_RDY) && !(cc & NVME_CC_ENABLE)) {
        // A late acknowledgement releases the quarantine.
        DBGSTAGE(STG_DISABLE_ACK, LcMsSince(DevExt, DevExt->StateTime));
        if (DevExt->FailReason == ENG_FAIL_RESET_TIMEOUT) {
            LcResetAcked(DevExt, TRUE);
        } else {
            EngChildrenSettle(DevExt, TRUE);
            LcFinishSettled(DevExt, FALSE);
            EngSetState(DevExt, ENG_ST_FAILED, DevExt->FailReason);
        }
    } else if (cc != ENG_ALL_ONES && (cc & NVME_CC_ENABLE)) {
        // The disable we wrote was lost (a hung controller drops register
        // writes): write it again, at the quarantine's low rate, so a
        // controller that recovers can still acknowledge it.
        NvmeWriteReg32(DevExt, NVME_REG_CC, cc & ~(NVME_CC_ENABLE | NVME_CC_SHN_MASK));
        if (DevExt->DisableRewrites++ == 0) {
            DBGSTAGE(STG_DISABLE_REWRITE, cc);
        }
    }
}

//
// Restartable shutdown notification (PLAN 0.11 class 2)
//
VOID EngShutdownFlushed(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG Index, IN USHORT Status)
{
    PENG_PARENT p = &DevExt->Parents[Index];
    ULONG cc;

    if (DevExt->State != ENG_ST_RUNNING) {
        // Only from `running` (the caller checks): never a way out of a
        // recovery state.
        return;
    }

    p->State = ENG_PA_SHN;
    // This shutdown episode's own results, kept for the 9x removal
    // transaction (pool9x.c), which may only credit what succeeded.
    DevExt->ShnFlushOk = (BOOLEAN)((Status & 0x7FF) == 0 && DevExt->WriteGen == DevExt->CleanGen);
    DevExt->ShnShstOk = FALSE;
    DBGMARK(STG_SHN_FLUSH_DONE, (ULONG)(Status & 0xFF) | ((DevExt->ShutdownCount & 0xFF) << 16) |
            ((ULONG)(DevExt->WriteGen != DevExt->CleanGen) << 24));
    // The adapter is quiet (SHUTDOWN is a barrier): mask the vector; the
    // timer carries the SHST wait. Timer and interrupts stay the driver's.
    NvmeWriteReg32(DevExt, NVME_REG_INTMS, 0xFFFFFFFF);
    cc = NvmeReadReg32(DevExt, NVME_REG_CC);
    cc = (cc & ~NVME_CC_SHN_MASK) | NVME_CC_SHN_NORMAL;
    NvmeWriteReg32(DevExt, NVME_REG_CC, cc);
    DBGSTAGE(STG_SHN_WRITE, cc);
    DevExt->ShnTime = LcNow(DevExt);
    DevExt->ShnSafe = LcSafe(DevExt);
    EngSetState(DevExt, ENG_ST_SHUTDOWN, 0);
    LcSetStep(DevExt, ENG_BU_SHN_WAIT);
    EngTimerEnsure(DevExt, ENG_TICK_FAST_US);
}

// Re-enable after a restartable shutdown (or for requests waiting behind
// it): CC.EN=0, RDY=0 waited by the timer, then the bring-up of 0.10 with
// the retained resources.
static VOID LcReenable(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb)
{
    ULONG cc;

    if (Srb != NULL) {
        DBGMARK(STG_REENABLE, STG_SRB_ARG(Srb, DevExt->State));
    } else {
        DBGMARK(STG_REENABLE, 0);
    }
    cc = NvmeReadReg32(DevExt, NVME_REG_CC);
    cc &= ~(NVME_CC_ENABLE | NVME_CC_SHN_MASK);
    NvmeWriteReg32(DevExt, NVME_REG_CC, cc);
    DBGSTAGE(STG_HWINIT_CC, cc);
    DevExt->ShnFlushOk = FALSE;                     // the shutdown episode ends
    DevExt->ShnShstOk = FALSE;
    DevExt->InitTime = LcNow(DevExt);
    EngSetState(DevExt, ENG_ST_STARTING, 0);
    LcSetStep(DevExt, ENG_BU_DISABLING);
    EngTimerEnsure(DevExt, ENG_TICK_FAST_US);
}

// The shutdown notification's wait ends (SHST complete, timed out, or the
// device gone): the SHUTDOWN SRB completes (its Flush has completed; 0.11
// class 2).
static VOID LcShnFinish(IN PHW_DEVICE_EXTENSION DevExt)
{
    ULONG i;

    LcSetStep(DevExt, ENG_BU_SHN_DONE);
    for (i = 0; i < ENG_MAX_PARENTS; i++) {
        if (DevExt->Parents[i].State == ENG_PA_SHN) {
            EngFinishParent(DevExt, i);
        }
    }
}

static VOID LcShnStep(IN PHW_DEVICE_EXTENSION DevExt)
{
    ULONG csts = LcCsts(DevExt);
    ULONG ms = LcTrustMs(DevExt, DevExt->ShnTime, DevExt->ShnSafe);

    if (csts != ENG_ALL_ONES && (csts & NVME_CSTS_SHST_MASK) == NVME_CSTS_SHST_COMPLETE) {
        DBGMARK(STG_SHN_DONE, (ms & 0xFFFF) | ((csts & 0xFF) << 16));
        DevExt->ShnShstOk = TRUE;
    } else if (csts == ENG_ALL_ONES || ms >= ENG_SHST_LIMIT_MS) {
        // Not clean: logged; the SRB still completes (0.11 class 2).
        DBGMARK(STG_SHN_TIMEOUT, (ms & 0xFFFF) | ((csts & 0xFF) << 16));
    } else {
        return;
    }
    LcShnFinish(DevExt);
    if (csts == ENG_ALL_ONES) {
        EngEnterGone(DevExt, ENG_GONE_TIMER);
    } else if (EngParentsWaiting(DevExt)) {
        // Requests that queued behind the SHUTDOWN: re-enable for them.
        LcReenable(DevExt, NULL);
    }
}

//
// CM link statement (evidence 2; interface 3 R12 S3; known issue K1 of
// 0.3.0.0). A DOWN_QUIESCED statement is taken in every engine state, the
// terminal ones (`failed`, `quarantined`, `shutdown` after the notification
// completed) included, and while a removal transaction owns the adapter:
// EngTimer polls it before any state's own step and before the removal
// transaction's, HwStartIo in `running`. Cleanup first, the
// acknowledgement last (R12 S3): every held request is completed, children
// and blocks are released and an open removal's outcome is written
// (PlatCmRemoveLinkDown), then PlatCmLinkAcknowledge relinquishes the pool
// and writes AckGeneration. Before that, completions already posted are
// consumed in the states whose own step consumes them (`running`, `gone`;
// timer only), so commands that did complete keep their result.
//
static BOOLEAN LcLinkStatement(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG Source)
{
    if (PlatCmLinkPoll(DevExt) != PLAT_LINK_DOWN_QUIESCED) {
        return FALSE;
    }
    if (!PlatCmRemoveLinkDown(DevExt)) {
        if (Source == ENG_GONE_TIMER && (DevExt->State == ENG_ST_RUNNING || DevExt->State == ENG_ST_GONE)) {
            EngProcessCompletions(DevExt);
        }
        if (DevExt->State == ENG_ST_SHUTDOWN && DevExt->Bringup == ENG_BU_SHN_WAIT) {
            // The notification can no longer complete; its SRB completes as
            // on an SHST timeout.
            DBGMARK(STG_SHN_TIMEOUT, 0xFFFF);
            LcShnFinish(DevExt);
        }
        EngEnterGone(DevExt, Source);               // nothing when already gone
        if (DevExt->State == ENG_ST_GONE) {
            LcGoneEvidence(DevExt, 2);
        }
    }
    PlatCmLinkAcknowledge(DevExt);
    if (DevExt->State == ENG_ST_GONE) {
        LcBusChange(DevExt);
    }
    return TRUE;
}

//
// Abort and reset SRBs (PLAN 0.10)
//
static VOID LcAbortSrb(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb)
{
    ULONG i;
    PENG_PARENT p, a;
    ULONG target = ENG_NONE;

    for (i = 0; i < ENG_MAX_PARENTS; i++) {
        p = &DevExt->Parents[i];
        if (p->State != ENG_PA_FREE && p->State != ENG_PA_PARKED && p->Srb == Srb->NextSrb) {
            target = i;
            break;
        }
    }
    if (target == ENG_NONE) {
        // Not ours (any more): nothing to abort.
        DBGSTAGE(STG_SRB_ABORT, 0xFF);
        Srb->SrbStatus = SRB_STATUS_ABORT_FAILED;
        EngCompleteSrb(DevExt, Srb);
        return;
    }
    p = &DevExt->Parents[target];
    if (p->Outstanding == 0) {
        // Nothing issued: the target completes ABORTED at once.
        DBGSTAGE(STG_SRB_ABORT, target | (1UL << 8));
        EngCancelParent(DevExt, target, ENG_CANCEL_ABORT);
        Srb->SrbStatus = SRB_STATUS_SUCCESS;
        EngCompleteSrb(DevExt, Srb);
        return;
    }
    // Children live: stop further dispatch; the abort completes once every
    // child the target issued has its own completion consumed (command-level
    // quiescence; no NVMe Abort is sent yet, so none is relied on).
    // Escalation by per-command deadline is PLAN 2.3.
    a = EngParentAlloc(DevExt, Srb, ENG_K_ABORT);
    if (a == NULL) {
        Srb->SrbStatus = SRB_STATUS_ABORT_FAILED;
        EngCompleteSrb(DevExt, Srb);
        return;
    }
    a->State = ENG_PA_PARKED;
    a->Target = (UCHAR)target;
    DBGSTAGE(STG_SRB_ABORT, target | (2UL << 8));
    EngCancelParent(DevExt, target, ENG_CANCEL_ABORT);
}

static VOID LcResetSrb(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb)
{
    PENG_PARENT p;

    DBGSTAGE(Srb->Function == SRB_FUNCTION_RESET_BUS ? STG_SRB_RESET_BUS : STG_SRB_RESET_DEVICE,
             STG_SRB_ARG(Srb, DevExt->State));
    if (EngResetBus(DevExt, Srb->PathId)) {
        Srb->SrbStatus = SRB_STATUS_SUCCESS;
        EngCompleteSrb(DevExt, Srb);
        return;
    }
    if (DevExt->State == ENG_ST_RESETTING) {
        // Pends until the disable is acknowledged (SUCCESS) or the adapter
        // fails (ERROR).
        p = EngParentAlloc(DevExt, Srb, ENG_K_RESET);
        if (p != NULL) {
            p->State = ENG_PA_PARKED;
            return;
        }
    }
    Srb->SrbStatus = SRB_STATUS_ERROR;
    EngCompleteSrb(DevExt, Srb);
}

//
// Polling mode (PLAN "Polling mode"): HwStartIo is a second completion path
// beside the timer. It consumes what is posted (with the phase bit, as the
// ISR would), then, while children are issued, polls again for at most
// PollSpinUs in stalls of POLL_SPIN_STEP_US (at most 500 us, half the
// per-callback stall budget of PLAN 0.10), so a short command completes in
// the callback that issued it rather than at the next tick. Only where the
// ISR would consume: running and gone (resetting and quarantined are the
// timer's), and never inside a 9x removal transaction past its drain.
//
static VOID LcPollStartIo(IN PHW_DEVICE_EXTENSION DevExt)
{
    ULONG spent = 0, step;

    if (!DevExt->PollAlways || (DevExt->State != ENG_ST_RUNNING && DevExt->State != ENG_ST_GONE) ||
        (PlatCmRemoveGate(DevExt) & PLAT_RM_GATE_ISR)) {
        return;
    }
    for (;;) {
        if (EngProcessCompletions(DevExt)) {
            DevExt->PollStartIoHits++;
            EngKick(DevExt);
        }
        if (DevExt->State != ENG_ST_RUNNING || EngIssuedChildren(DevExt) == 0 ||
            spent >= DevExt->Tun.Poll.SpinUs) {
            break;
        }
        step = DevExt->Tun.Poll.SpinUs - spent;
        if (step > POLL_SPIN_STEP_US) {
            step = POLL_SPIN_STEP_US;
        }
        ScsiPortStallExecution(step);
        spent += step;
    }
}

//
// HwStartIo for the device's own LUN (the admission gate of PLAN 2.2)
//
BOOLEAN EngStartIo(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb)
{
    BOOLEAN tagged = (BOOLEAN)((Srb->SrbFlags & SRB_FLAGS_QUEUE_ACTION_ENABLE) && Srb->QueueTag != SP_UNTAGGED);
    ULONG csts;

    DBGSTAGE(STG_STARTIO, STG_SRB_ARG(Srb, DevExt->State));
    ScsiSenseNewCommand(DevExt, Srb);

    // A 9x CM removal request accepted (interface 3 R9d): BUSY until the
    // acknowledgement, then nothing is served again.
    if (PlatCmRemoveStartIo(DevExt, Srb)) {
        EngStartIoTail(DevExt, tagged);
        return TRUE;
    }

    if (Srb->Function == SRB_FUNCTION_EXECUTE_SCSI && Srb->Cdb[0] == SCSIOP_REQUEST_SENSE &&
        (DevExt->State == ENG_ST_FAILED || DevExt->State == ENG_ST_QUARANTINED)) {
        // Answered from memory, no device access: the sense of the request
        // that just failed in this state (R3-9).
        ScsiHandleRequestSense(DevExt, Srb);
        EngStartIoTail(DevExt, tagged);
        return TRUE;
    }

    switch (Srb->Function) {
    case SRB_FUNCTION_ABORT_COMMAND:
        LcAbortSrb(DevExt, Srb);
        EngStartIoTail(DevExt, FALSE);
        return TRUE;
    case SRB_FUNCTION_RESET_BUS:
    case SRB_FUNCTION_RESET_DEVICE:
        LcResetSrb(DevExt, Srb);
        EngStartIoTail(DevExt, FALSE);
        return TRUE;
    default:
        break;
    }

    if (DevExt->State == ENG_ST_RUNNING) {
        // Presence check before any dispatch (2.1); one MMIO read.
        csts = LcCsts(DevExt);
        if (csts == ENG_ALL_ONES) {
            EngEnterGone(DevExt, ENG_GONE_STARTIO);
        } else {
            LcLinkStatement(DevExt, ENG_GONE_STARTIO);
        }
    } else if (DevExt->State != ENG_ST_NONE && DevExt->State != ENG_ST_STOPPED && DevExt->State != ENG_ST_STOPPING) {
        // Every other state takes a statement here too (K1).
        LcLinkStatement(DevExt, ENG_GONE_STARTIO);
    }

    switch (DevExt->State) {
    case ENG_ST_RUNNING:
        ScsiDispatch(DevExt, Srb);
        break;
    case ENG_ST_STARTING:
    case ENG_ST_RESETTING:
        EngHold(DevExt, Srb);
        EngTimerEnsure(DevExt, ENG_TICK_FAST_US);
        break;
    case ENG_ST_SHUTDOWN:
        if (DevExt->Bringup == ENG_BU_SHN_WAIT) {
            EngHold(DevExt, Srb);
        } else if (Srb->Function == SRB_FUNCTION_SHUTDOWN || Srb->Function == SRB_FUNCTION_FLUSH ||
                   (Srb->Function == SRB_FUNCTION_EXECUTE_SCSI &&
                    (Srb->Cdb[0] == SCSIOP_SYNCHRONIZE_CACHE || Srb->Cdb[0] == SCSIOP_SYNCHRONIZE_CACHE16))) {
            // Already shut down with nothing written since.
            if (Srb->Function == SRB_FUNCTION_SHUTDOWN) {
                DevExt->ShutdownCount++;
            }
            DBGSTAGE(STG_SHUT_REPEAT, STG_SRB_ARG(Srb, DevExt->State));
            Srb->DataTransferLength = 0;
            ScsiSuccess(DevExt, Srb);
        } else {
            LcReenable(DevExt, Srb);
            EngHold(DevExt, Srb);
        }
        break;
    case ENG_ST_GONE:
        LcGoneFailSrb(DevExt, Srb);
        break;
    case ENG_ST_FAILED:
    case ENG_ST_QUARANTINED:
        LcFailSrb(DevExt, Srb);
        break;
    default:
        // Not initialised or stopped: nothing was issued.
        DBGSTAGE(STG_SRB_BUSY_STARTING, STG_SRB_ARG(Srb, DevExt->State));
        EngBusyOrFail(DevExt, Srb);
        EngCompleteSrb(DevExt, Srb);
        break;
    }
    LcPollStartIo(DevExt);
    EngStartIoTail(DevExt, tagged);
    return TRUE;
}

//
// Hold-then-BUSY (PLAN 0.3; G1, Q-NT): a request with no child issued is
// held for most of its own TimeOutValue (elapsed time, minus a margin), then
// completed BUSY; 9x uses a fixed bound (TimeOutValue is 0 or garbage, G9).
//
static ULONG LcHoldBoundMs(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb)
{
#ifdef NVME2K_W9X
    UNREFERENCED_PARAMETER(Srb);
    return DevExt->Tun.Hold9xMs;
#else
    ULONG tov = Srb->TimeOutValue;
    ULONG margin;

    UNREFERENCED_PARAMETER(DevExt);

    if (tov == 0 || tov > 3600) {
        return 2000;
    }
    tov *= 1000;
    // Margin: at least HoldMarginMs and half the TimeOutValue. Half, not the
    // "about 3 s of 4 s" of PLAN 0.3, because the elapsed time (LcEarlyMs,
    // the larger of the TSC and safe estimates) can still be off: the safe
    // clock runs slow under load and the TSC is only as good as its last
    // re-calibration. Coverage of CAP.TO through the port's 20 BUSY
    // re-issues (21 x (hold + retry delay) per issue; PLAN 0.3, Q-NT):
    // - initial bus scan (INQUIRY 4 s on 2000, issued 3 times):
    //   21 x (2 s + 1.56 s) x 3 = about 224 s > 127.5 s, the largest CAP.TO;
    // - I/O behind a re-enable (after a restartable shutdown or a reset) has
    //   the class driver's TimeOutValue (10 s for disk reads and writes on
    //   2000, XP x64 and 7 x64): 21 x (5 s + 1.56 s) = about 138 s on 2000
    //   and 21 x (5 s + 1.0 s) = about 126 s on XP x64 and 7 x64 per issue,
    //   so a CAP.TO above about 126 s on XP x64 and 7 is covered only by the
    //   class driver's own retry of the failed request (not measured). The
    //   re-enable itself waits for RDY up to CAP.TO.
    margin = tov / 2;
    if (margin < DevExt->Tun.HoldMarginMs) {
        margin = DevExt->Tun.HoldMarginMs;
    }
    if (tov <= margin + 500) {
        return 500;
    }
    return tov - margin;
#endif
}

static VOID LcHoldCheck(IN PHW_DEVICE_EXTENSION DevExt)
{
    ULONG i, ms;
    PENG_PARENT p;
    PSCSI_REQUEST_BLOCK srb;

    for (i = 0; i < ENG_MAX_PARENTS; i++) {
        p = &DevExt->Parents[i];
        if (p->State != ENG_PA_WAITING || (p->Flags & ENG_PF_STARTED) || p->Srb == NULL) {
            continue;
        }
        srb = p->Srb;
#ifdef NVME2K_W9X
        if (srb->Function == SRB_FUNCTION_EXECUTE_SCSI && srb->Cdb[0] == SCSIOP_INQUIRY) {
            // 9x scan INQUIRY (Q-9x rule): held until bring-up succeeds
            // (replayed) or fails (LcFailHeldParents), never BUSY: IOS
            // never re-sends a scan INQUIRY completed BUSY, and the disk is
            // then not found (98 SE and Me, G1 on 9x and the engine runs).
            continue;
        }
#endif
        ms = LcEarlyMs(DevExt, p->AdmitTime, p->AdmitSafe);
        if (ms < LcHoldBoundMs(DevExt, srb)) {
            continue;
        }
        DBGSTAGE(STG_SRB_HELD_TO_BUSY, (ms << 8) | srb->Cdb[0]);
        EngBusyOrFail(DevExt, srb);
        p->Flags |= ENG_PF_PRESET;
        EngFinishParent(DevExt, i);
    }
}

//
// Idle Flush deadline (review round 4). The idle Flush (MD-7 (a)) is an
// internal command with no SRB, so no port timeout and no HwResetBus ever
// covers it: a present controller that stops completing it would leave
// IdleFlushOutstanding set for good and suppress every later idle Flush.
// It gets its own deadline, measured from its submission with the trusted
// (smaller) elapsed estimate, so it never expires early. On expiry the
// engine runs the same disable recovery a timed-out request gets from
// HwResetBus: CC.EN=0; an acknowledgement within the stall budget re-enables
// at once (LcResetAcked, which releases the Flush child and clears the flag
// through EngChildrenSettle), otherwise `resetting` carries the wait and,
// past CAP.TO, quarantines (EngChildQuarantine clears the flag). The write
// generation stays dirty, so the idle Flush is tried again after the
// re-enable. TRUE if the deadline was taken.
//
static BOOLEAN LcIdleFlushOverdue(IN PHW_DEVICE_EXTENSION DevExt)
{
    ULONG limit = ENG_IDLE_FLUSH_TO_MS;
    ULONG ms, csts;

#ifdef STOR98_DBGPORT
    if (DevExt->Tun.IdleFlushToMs != 0) {
        limit = DevExt->Tun.IdleFlushToMs;
    }
#endif
    if (limit < DevExt->CapTimeoutMs) {
        limit = DevExt->CapTimeoutMs;
    }
    ms = LcTrustMs(DevExt, DevExt->IdleFlushTime, DevExt->IdleFlushSafe);
    if (ms <= limit) {
        return FALSE;
    }
    DevExt->IdleFlushTimeouts++;
    DBGMARK(STG_IDLE_FLUSH_TIMEOUT, (ms & 0xFFFFFF) | ((ULONG)DevExt->State << 24));
    DevExt->ResetStartTime = LcNow(DevExt);
    DevExt->ResetStartSafe = LcSafe(DevExt);
    csts = LcDisablePoll(DevExt);
    if (csts == ENG_ALL_ONES) {
        EngEnterGone(DevExt, ENG_GONE_TIMER);
    } else if (!(csts & NVME_CSTS_RDY)) {
        LcResetAcked(DevExt, TRUE);
    } else {
        LcEnterResetting(DevExt, ENG_FAIL_IDLE_FLUSH);
        EngTimerEnsure(DevExt, ENG_TICK_FAST_US);
    }
    return TRUE;
}

//
// Running: lost-interrupt polling, presence, CFS, idle flush (MD-7 (a))
//
static VOID LcRunningStep(IN PHW_DEVICE_EXTENSION DevExt)
{
    ULONG csts = LcCsts(DevExt);

    if (csts == ENG_ALL_ONES) {
        EngEnterGone(DevExt, ENG_GONE_TIMER);
        return;
    }
    EngProcessCompletions(DevExt);
    if (DevExt->State != ENG_ST_RUNNING) {
        // A completion just processed changed the state (the SHUTDOWN SRB's
        // Flush completed: CC.SHN is written). Nothing below applies any
        // more; in particular no idle Flush to a controller shutting down.
        return;
    }
    if (csts & NVME_CSTS_CFS) {
        // Present but fatal (2.1): disable; the issued SRBs complete with
        // HARDWARE ERROR once the disable is acknowledged; then `failed`.
        DevExt->ResetIsFailure = TRUE;
        DevExt->ResetStartTime = LcNow(DevExt);
        DevExt->ResetStartSafe = LcSafe(DevExt);
        DBGSTAGE(STG_BRINGUP_FAIL, ENG_FAIL_CFS);
        csts = LcDisablePoll(DevExt);
        if (csts == ENG_ALL_ONES) {
            EngEnterGone(DevExt, ENG_GONE_TIMER);
        } else if (!(csts & NVME_CSTS_RDY)) {
            LcResetAcked(DevExt, FALSE);
        } else {
            LcEnterResetting(DevExt, ENG_FAIL_CFS);
        }
        return;
    }
    // A DOWN_QUIESCED statement is taken by EngTimer before this step
    // (LcLinkStatement, K1).
    if (DevExt->IdleFlushOutstanding && LcIdleFlushOverdue(DevExt)) {
        return;
    }
    if (DevExt->WriteGen != DevExt->CleanGen && !DevExt->IdleFlushOutstanding &&
        DevExt->OutstandingWrites == 0 && !EngParentsWaiting(DevExt) &&
        LcMsSince(DevExt, DevExt->LastWriteTime) >= DevExt->Tun.IdleFlushMs) {
        if (EngSubmitInternalFlush(DevExt, ENG_INT_IDLE_FLUSH)) {
            DevExt->IdleFlushOutstanding = TRUE;
            DevExt->IdleFlushTime = LcNow(DevExt);
            DevExt->IdleFlushSafe = LcSafe(DevExt);
            DBGSTAGE(STG_ENG_IDLE_FLUSH, DevExt->WriteGen);
        }
    }
}

VOID EngTimer(IN PVOID DeviceExtension)
{
    PHW_DEVICE_EXTENSION DevExt = (PHW_DEVICE_EXTENSION)DeviceExtension;
    ULONG period;

    DevExt->TimerActive = FALSE;
    if (DevExt->State == ENG_ST_NONE || DevExt->State == ENG_ST_STOPPED || DevExt->State == ENG_ST_STOPPING) {
        return;
    }
    if (!DevExt->TimerFirstSeen) {
        DevExt->TimerFirstSeen = TRUE;
        DBGMARK(STG_TIMER_FIRST, DevExt->State);
    }
    ClockTick(&DevExt->Clock, DevExt->TimerPeriodUs);
    if (DevExt->PollAlways) {
        DevExt->PollTicks++;
    }
    // A CM link statement first, whatever the state and whether or not a
    // removal transaction owns the adapter (K1).
    LcLinkStatement(DevExt, ENG_GONE_TIMER);
    // The 9x CM removal transaction (interface 3 R9-R11, platform.h): while
    // it owns the adapter the engine's own step does not run.
    period = PlatCmRemoveTimer(DevExt);
    if (period != 0) {
        if (period != PLAT_RM_NO_REARM) {
            EngTimerEnsure(DevExt, period);
        }
        return;
    }
    switch (DevExt->State) {
    case ENG_ST_STARTING:
        LcBringupStep(DevExt);
        break;
    case ENG_ST_RUNNING:
        LcRunningStep(DevExt);
        break;
    case ENG_ST_RESETTING:
        LcResettingStep(DevExt);
        break;
    case ENG_ST_SHUTDOWN:
        if (DevExt->Bringup == ENG_BU_SHN_WAIT) {
            LcShnStep(DevExt);
        }
        break;
    case ENG_ST_GONE:
        LcGoneStep(DevExt);
        break;
    case ENG_ST_QUARANTINED:
        LcQuarantineStep(DevExt);
        break;
    default:
        break;
    }
    LcStormUnmaskTrial(DevExt);
    // Software-only replays first (review round 3, N3): a held request that
    // completes without hardware work (INQUIRY, TEST UNIT READY, READ
    // CAPACITY...) is answered in the tick whose bring-up step reached
    // `running`, before the hold bound is checked, so it is never completed
    // BUSY just as the adapter became ready (98 SE: the scan INQUIRY was, at
    // the very tick RDY arrived). Requests that would start DMA keep
    // hold-then-BUSY order (PLAN 0.3): past their hold bound they are
    // completed BUSY before any child is issued, and only the rest are
    // dispatched after the check.
    EngKickSoftware(DevExt);
    LcHoldCheck(DevExt);
    EngKick(DevExt);
    period = LcWantedPeriod(DevExt);
    if (period != 0) {
        EngTimerEnsure(DevExt, period);
    } else {
        ClockTimerStopped(&DevExt->Clock);
    }
    if (DevExt->State == ENG_ST_RUNNING && (DevExt->PollAlways ? LcEngineIdle(DevExt) : period == 0)) {
        // Idle and clean: the accounting of the engine so far (every parent
        // admitted completed once; stale and blocked double completions).
        // As on main when the timer stops; a polling adapter keeps ticking
        // while idle, so there once per idle period.
        if (!DevExt->PollIdleLogged) {
            DBGSTAGE(STG_ENG_ADMITTED, (DevExt->ParentsAdmitted & 0xFFFF) | (DevExt->ChildrenIssued << 16));
            DBGSTAGE(STG_ENG_STATS, (DevExt->ParentsCompleted & 0xFFFF) | ((DevExt->StaleCqes & 0xFF) << 16) |
                     ((DevExt->DoubleBlocked & 0xFF) << 24));
            if (DevExt->PollAlways) {
                DBGSTAGE(STG_POLL_IDLE, (DevExt->PollTicks & 0xFFFF) |
                         ((DevExt->PollStartIoHits > 0xFFFF ? 0xFFFF : DevExt->PollStartIoHits) << 16));
                DevExt->PollIdleLogged = TRUE;
            }
        }
    } else {
        DevExt->PollIdleLogged = FALSE;
    }
}

//
// Completion processing (ISR and timer)
//
//
// The admin queue is drained while engine work can still complete on it:
// a child issued, or one quarantined (failed reset, MD-1 deadline) whose own
// completion is its command-level quiescence (PLAN 0.10, R4-4) and releases
// its parent and PRP page (review round 2: quarantining decremented
// ChildrenAdmin, so a late Get Log Page completion was never consumed).
// Only an initialised admin queue (LcEnable), and never while starting:
// there the bring-up chain owns the admin queue (LcBringupStep), and its
// CIDs are only accepted in that step (nvme2k_cpl.c).
//
BOOLEAN EngProcessCompletions(IN PHW_DEVICE_EXTENSION DevExt)
{
    BOOLEAN done = FALSE;

    if (!DevExt->QueuesAllocated) {
        return FALSE;
    }
    if (NvmeProcessIoCompletion(DevExt)) {
        done = TRUE;
    }
    if (DevExt->AdminCqLive && DevExt->State != ENG_ST_STARTING &&
        (DevExt->ChildrenAdmin != 0 || DevExt->QuarantinedAdmin != 0) &&
        NvmeProcessAdminCompletion(DevExt)) {
        done = TRUE;
    }
    return done;
}

//
// Interrupt storms (review round 2; round 3, N1). A level-triggered INTx line
// that stays asserted with nothing in the queues (a controller that lost a
// completion queue head doorbell, keeps its interrupt status for another
// reason, or another device on a shared line) calls the ISR without end and
// can starve the timer. Writing a head doorbell again is no remedy: NVM
// Express Base 1.4c, Figure 147, lists a write of the previous value as a
// possible Invalid Doorbell Write Value error, and on a shared line an empty
// interrupt does not even show that this controller is the source. The
// engine writes a completion queue head doorbell only for entries it has
// consumed (nvme2k_cpl.c). Recovery:
//
// 1. Mask the vector at the controller (INTMS, NVMe Base 1.4c 3.1.3; with
//    the vector masked a pin-based controller does not assert it) and let
//    the timer poll the queues (PollMode: LcWantedPeriod keeps the fast tick
//    while work is outstanding; every state with live queues polls them).
// 2. If empty interrupts go on with the vector masked, that proves nothing
//    about this controller: another device on a shared line keeps calling
//    the ISR whatever stor98 masks (review round 3, N1: isolating here used
//    to reset a healthy controller once per generation, failing its I/O with
//    BUS_RESET, for another device's interrupts). The engine stays on
//    polling, re-writes the mask and doubles the polling hold (up to 60 s),
//    counted and marked (action 4). It never disables or resets the
//    controller for interrupts. NVMe gives no register that shows a
//    pin-based controller asserting its line, and the PCI Interrupt Status
//    bit is in configuration space, which the SCSIport contract does not
//    let the engine read at run time (PLAN 0.12, R4-2), so no evidence of
//    ownership is available from the ISR. A controller that really ignores
//    the mask and stops completing commands is handled by the evidence that
//    does exist: its commands time out, and the port's HwResetBus runs the
//    watchdog recovery of PLAN 0.10.
// 3. While running, the vector is unmasked on trial after the polling hold
//    (2 s, doubled at every new storm up to 60 s), so a false alarm on a
//    shared line (another device's interrupts) costs only a spell of
//    polling.
//
// Known emulator defect (TESTING, "INTx model caveat"): QEMU's NVMe model
// counts pending completion queues for INTx and keeps the count across a
// controller reset, so a doorbell lost while a queue was pending (the
// harness's x-hang) leaves the line asserted after recovery. Step 1 drops
// the line in that model (its pin follows the unmasked status), so the
// qualification VMs continue on polling after such a test. The model defect
// is tracked in usb4-emu, not worked around with doorbell writes here.
//
static VOID LcStormMask(IN PHW_DEVICE_EXTENSION DevExt, IN BOOLEAN Masked)
{
    NvmeWriteReg32(DevExt, NVME_REG_INTMS, 0xFFFFFFFF);
    DevExt->PollMode = TRUE;
    DevExt->PollSince = LcNow(DevExt);
    if (DevExt->PollHoldMs == 0) {
        DevExt->PollHoldMs = ENG_POLL_HOLD_MS;
    } else if (DevExt->PollHoldMs < ENG_POLL_HOLD_MAX_MS) {
        DevExt->PollHoldMs *= 2;
        if (DevExt->PollHoldMs > ENG_POLL_HOLD_MAX_MS) {
            DevExt->PollHoldMs = ENG_POLL_HOLD_MAX_MS;
        }
    }
    if (Masked) {
        // Already masked: not evidence against this controller (step 2).
        DevExt->StormMaskedBatches++;
        DBGMARK(STG_INT_STORM, 4 | ((ULONG)DevExt->State << 8) | ((DevExt->StormMaskedBatches & 0xFFFF) << 16));
    } else {
        DevExt->StormMasks++;
        DBGMARK(STG_INT_STORM, 1 | ((ULONG)DevExt->State << 8) | ((DevExt->StormMasks & 0xFFFF) << 16));
    }
    EngTimerEnsure(DevExt, ENG_TICK_FAST_US);
}

// Polling hold over (timer, running only): unmask the vector on trial.
static VOID LcStormUnmaskTrial(IN PHW_DEVICE_EXTENSION DevExt)
{
    if (DevExt->PollAlways) {
        return;                     // no interrupt in use: never unmasked
    }
    if (!DevExt->PollMode || DevExt->State != ENG_ST_RUNNING ||
        LcMsSince(DevExt, DevExt->PollSince) < DevExt->PollHoldMs) {
        return;
    }
    DevExt->PollMode = FALSE;
    DevExt->EmptyInts = 0;
    DBGSTAGE(STG_INT_STORM, 3 | ((ULONG)DevExt->State << 8) | ((DevExt->StormMasks & 0xFFFF) << 16));
    NvmeWriteReg32(DevExt, NVME_REG_INTMC, 0x00000001);
}

BOOLEAN EngInterrupt(IN PHW_DEVICE_EXTENSION DevExt)
{
    ULONG now;

    // Vector 0 is unmasked while running and stays so through a reset
    // whose disable was not acknowledged (resetting, quarantined): late
    // completions there must still be consumed. In the other states the
    // timer owns the queues (bring-up polls the admin queue itself).
    switch (DevExt->State) {
    case ENG_ST_RUNNING:
    case ENG_ST_RESETTING:
    case ENG_ST_GONE:
    case ENG_ST_QUARANTINED:
        break;
    default:
        return FALSE;
    }
    // A 9x removal transaction past its drain (platform.h): the queues are
    // the transaction's; nothing is consumed here (a shared line still
    // calls this ISR, with this controller's vector masked).
    if (PlatCmRemoveGate(DevExt) & PLAT_RM_GATE_ISR) {
        return FALSE;
    }
    if (DevExt->PollAlways) {
        // Polling mode: the port connected no interrupt for this adapter,
        // INTx stays disabled in the command register and every vector is
        // masked (INTMS), so a call here is the port polling (a dump port
        // does, PLAN 4.2) or another device's interrupt on a line the port
        // shares. Completions are consumed with the phase bit as always;
        // never claimed when there were none, and no storm accounting.
        if (DevExt->PollIsrCalls++ == 0) {
            DBGSTAGE(STG_POLL_ISR, DevExt->State);
        }
        if (EngProcessCompletions(DevExt)) {
            EngKick(DevExt);
            return TRUE;
        }
        return FALSE;
    }
    if (EngProcessCompletions(DevExt)) {
        DevExt->EmptyInts = 0;
        EngKick(DevExt);
        return TRUE;
    }
    // Nothing consumed: another device on a shared line, or a storm. Not
    // claimed either way, so a shared line's other handlers still run.
    now = LcNow(DevExt);
    if (DevExt->EmptyInts == 0 || now - DevExt->StormStart > ENG_STORM_WINDOW_MS) {
        DevExt->StormStart = now;
        DevExt->EmptyInts = 0;
    }
    if (++DevExt->EmptyInts >= (DevExt->Tun.StormInts != 0 ? DevExt->Tun.StormInts : ENG_STORM_INTS)) {
        DevExt->EmptyInts = 0;
        LcStormMask(DevExt, DevExt->PollMode);
    }
    return FALSE;
}

//
// Stop (PLAN 0.10 "Stop", 0.11 class 3): ScsiStopAdapter, 9x
// HwAdapterState(FALSE). Synchronous; no timer runs after it. Never more than
// the stall budget; never a wait on absent hardware. The SRBs belong to the
// port by now (it has retired them on NT), so none is completed here.
//
// 9x (not measured): inside a CM removal transaction pool9x.c has completed
// every request before the stop, but a native stop (98 SE HwAdapterState
// FALSE at removal or shutdown, no transaction) may still find parents
// held or with children issued. They are forgotten as on NT. Completing
// them here is not shown safe: whether IOS acts on RequestComplete from
// HwAdapterState, and whether it has already retired them (a double
// completion), are not measured, and one with a child issued could
// complete only after the disable below is acknowledged. The counts are
// logged (STG_STOP_RESULT bits 16-31) so that a 98 SE stop with requests
// outstanding shows which case occurs; PLAN 0.11 class 3 ("any owned are
// completed per 0.10") is open on 9x until then.
//
VOID EngStop(IN PHW_DEVICE_EXTENSION DevExt)
{
    ULONG csts, cc, stalls = 0, polls;
    UCHAR old = DevExt->State;
    BOOLEAN dirty = (BOOLEAN)(DevExt->WriteGen != DevExt->CleanGen);
    BOOLEAN acked = FALSE, clean = FALSE;
    ULONG i, ownedHeld = 0, ownedIssued = 0;

    csts = (DevExt->ControllerRegisters != NULL) ? LcCsts(DevExt) : ENG_ALL_ONES;
    DBGMARK(STG_STOP_ENTER, (csts & 0xFF) | ((ULONG)dirty << 8) | ((ULONG)old << 16) |
            ((EngIssuedChildren(DevExt) & 0xFF) << 24));
    EngSetState(DevExt, ENG_ST_STOPPING, 0);
    // The timer cannot rearm from now on (teardown invariant, 0.11).
    ScsiPortNotification(RequestTimerCall, DevExt, EngTimer, 0);
    DevExt->TimerActive = FALSE;
    // The port owns the SRBs now (on NT it has retired them before the
    // stop): forget them first, so that no completion polled below can
    // complete one; their children only release or keep memory.
    for (i = 0; i < ENG_MAX_PARENTS; i++) {
        if (DevExt->Parents[i].State != ENG_PA_FREE) {
            if (DevExt->Parents[i].Outstanding != 0) {
                ownedIssued++;
            } else {
                ownedHeld++;
            }
        }
        DevExt->Parents[i].State = ENG_PA_FREE;
        DevExt->Parents[i].Srb = NULL;
    }
    for (i = 0; i < ENG_MAX_CHILDREN; i++) {
        DevExt->Children[i].Parent = ENG_NONE;
    }

    if (csts == ENG_ALL_ONES || old == ENG_ST_GONE || old == ENG_ST_NONE) {
        // No hardware access: what is issued stays quarantined.
        DBGSTAGE(STG_STOP_SKIP, csts == ENG_ALL_ONES ? 1 : 2);
        DBGSTAGE(STG_STOP_NOHW, EngIssuedChildren(DevExt) | ((ULONG)old << 16));
        EngChildrenSettle(DevExt, FALSE);
        goto out;
    }
    NvmeWriteReg32(DevExt, NVME_REG_INTMS, 0xFFFFFFFF);

    if (old == ENG_ST_RUNNING && (NvmeReadReg32(DevExt, NVME_REG_CC) & NVME_CC_ENABLE) &&
        (csts & NVME_CSTS_RDY)) {
        // Flush if dirty under the conservative tracking (0.10, R6-3).
        if (dirty) {
            if (EngSubmitInternalFlush(DevExt, ENG_INT_STOP_FLUSH)) {
                DevExt->StopFlushOutstanding = TRUE;
                DevExt->StopFlushStatus = 0xFFFF;
                DBGMARK(STG_STOP_FLUSH_SUBMIT, DevExt->WriteGen);
                polls = 0;
                while (DevExt->StopFlushOutstanding && stalls < ENG_STALL_BUDGET - 2) {
                    NvmeProcessIoCompletion(DevExt);
                    if (!DevExt->StopFlushOutstanding) {
                        break;
                    }
                    // Presence lost while the Flush is outstanding (review
                    // round 3): stop polling at once rather than spend the
                    // rest of the stall budget on absent hardware.
                    csts = LcCsts(DevExt);
                    if (csts == ENG_ALL_ONES) {
                        break;
                    }
                    ScsiPortStallExecution(ENG_STALL_US);
                    stalls++;
                    polls++;
                }
                NvmeProcessIoCompletion(DevExt);
                DBGMARK(STG_STOP_FLUSH_DONE, (polls & 0xFFFFF) | (DevExt->StopFlushOutstanding ? 0x00800000 : 0) |
                        ((ULONG)(DevExt->StopFlushStatus & 0xFF) << 24));
                if (csts == ENG_ALL_ONES) {
                    // No hardware access any more: what is issued stays
                    // quarantined, as for a stop that finds CSTS all-ones.
                    DBGSTAGE(STG_STOP_SKIP, 3);
                    DBGSTAGE(STG_STOP_NOHW, EngIssuedChildren(DevExt) | ((ULONG)old << 16));
                    EngChildrenSettle(DevExt, FALSE);
                    goto out;
                }
            }
        } else {
            DBGSTAGE(STG_STOP_FLUSH_DONE, 0xFFFFFFFE);
        }
        cc = NvmeReadReg32(DevExt, NVME_REG_CC);
        cc = (cc & ~NVME_CC_SHN_MASK) | NVME_CC_SHN_NORMAL;
        NvmeWriteReg32(DevExt, NVME_REG_CC, cc);
        DBGMARK(STG_STOP_SHN, cc);
        csts = LcCsts(DevExt);
        polls = 0;
        while (csts != ENG_ALL_ONES && (csts & NVME_CSTS_SHST_MASK) != NVME_CSTS_SHST_COMPLETE &&
               stalls < ENG_STALL_BUDGET - 1) {
            ScsiPortStallExecution(ENG_STALL_US);
            stalls++;
            polls++;
            csts = LcCsts(DevExt);
        }
        clean = (BOOLEAN)(csts != ENG_ALL_ONES && (csts & NVME_CSTS_SHST_MASK) == NVME_CSTS_SHST_COMPLETE);
        DBGMARK(STG_STOP_SHST_DONE, (polls & 0xFFFFF) | (clean ? 0 : 0x00800000) | ((csts & 0xFF) << 24));
    }

    cc = NvmeReadReg32(DevExt, NVME_REG_CC);
    cc &= ~(NVME_CC_ENABLE | NVME_CC_SHN_MASK);
    NvmeWriteReg32(DevExt, NVME_REG_CC, cc);
    DBGMARK(STG_STOP_EN0, cc);
    csts = LcCsts(DevExt);
    polls = 0;
    while (csts != ENG_ALL_ONES && (csts & NVME_CSTS_RDY) && stalls < ENG_STALL_BUDGET) {
        ScsiPortStallExecution(ENG_STALL_US);
        stalls++;
        polls++;
        csts = LcCsts(DevExt);
    }
    acked = (BOOLEAN)(csts != ENG_ALL_ONES && !(csts & NVME_CSTS_RDY));
    DBGMARK(STG_STOP_RDY_DONE, (polls & 0xFFFFF) | (acked ? 0 : 0x00800000) | ((csts & 0xFF) << 24));
    // Evidence 1 releases; otherwise quarantine (with MD-3 (a) the port frees
    // its extension regardless: the accepted window of MD-3).
    EngChildrenSettle(DevExt, acked);

out:
    DevExt->NextOwed = FALSE;
    DevExt->NextLuOwed = FALSE;
    // The engine never touches its DMA blocks again: relinquish them
    // (isolated -> RELEASED, otherwise QUARANTINED; platform.h, N2).
    PlatDmaRelinquish(DevExt);
    DBGMARK(STG_STOP_RESULT, (ULONG)acked | ((ULONG)clean << 1) | ((ULONG)(DevExt->Quarantined != 0) << 2) |
            (stalls << 8) | (ownedHeld << 16) | (ownedIssued << 24));
    UNREFERENCED_PARAMETER(ownedHeld);
    UNREFERENCED_PARAMETER(ownedIssued);
    EngSetState(DevExt, ENG_ST_STOPPED, 0);
}
