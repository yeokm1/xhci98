/*
 * hcd_retry.c - a controller stopped after repeated failures, tried again:
 * the driver half (roadmap-usb4.md task 50.31, design record 38). The pure
 * half, every decision, is xhci_retry.c; this file gathers its inputs under
 * the controller lock, makes the sweep of the root ports in short holds,
 * and carries out what it decides. The commit itself is made inside
 * hcdRecover's own hold (hcd_ctl.c), through HcdRetryCommitLocked.
 *
 * Locking (record 38 section 14): the power gate, then the controller lock,
 * never the reverse; the qemu flavour's InjLock is a leaf taken inside the
 * controller lock where a hook needs it. The retry state (XHCI_EXTENSION's
 * Retry) is written under the controller lock; the port evidence
 * (HCD_CONTROLLER's RetryPort, RetrySweep, RetryManaged, RetryFeed) is the
 * controller thread's alone.
 *
 * IRQL: each function says its own. Those named ...Locked run with the
 * controller lock held, so at DISPATCH_LEVEL; the others are entered at
 * PASSIVE_LEVEL and raise only inside the controller-lock holds they take.
 * No new thread, timer, DPC or work item: the thread's step runs in the
 * controller thread's pass.
 *
 * Task 50.30 step 3's leave signal, CmdWaitLeave, is one of the admission's
 * rows (hcdRetryInputsLocked; record 38 sections 7.1 and 12.2); its hold
 * reason 4 and its debts' and deferred enumerations' obligations are its
 * own code's, which a try's HCRST meets as any HCRST does.
 */

#include "hcd.h"
#include "hcd_svc.h"
#include "xhci_hw.h"
#include "xhci_dbg.h"

/* The resume phase's values are the pure core's (xhci_retry.h). */
XHCI_C_ASSERT(hcd_retry_phase,
              XHCI98_RESUME_RUNNING == XHCI_RETRY_RESUME_RUNNING &&
                  XHCI98_RESUME_PENDING == XHCI_RETRY_RESUME_PENDING &&
                  XHCI98_RESUME_FAILED == XHCI_RETRY_RESUME_FAILED);

/* Nominal seconds of a tick count, rounded up, held to 16 bits. IRQL: any;
 * no lock, no state. */
static ULONG hcdRetrySeconds(ULONG ticks)
{
    ULONG s;

    s = (ticks / 10UL) + ((ticks % 10UL) != 0 ? 1UL : 0UL);
    return (s > 0xFFFFUL) ? 0xFFFFUL : s;
}

/* The system time in units of 2^20 x 100 ns (record 38 section 5.7), from
 * its two halves by shifts alone: no 64-bit arithmetic, wraps after about
 * 14 years. IRQL: any (KeQuerySystemTime is). */
static ULONG hcdRetrySysNow(VOID)
{
    LARGE_INTEGER t;

    KeQuerySystemTime(&t);
    return ((ULONG)t.HighPart << 12) | ((ULONG)t.LowPart >> 20);
}

/*
 * The admission's inputs (record 38 sections 7.1 and 7.2). `scanSlots` 0
 * keeps DevicesLeft as `in` holds it: a sweep's port holds take it from the
 * admission hold, since device records are made and freed on the
 * controller thread alone, which is the one running the sweep. Controller
 * lock held; IRQL DISPATCH_LEVEL.
 */
static VOID hcdRetryInputsLocked(PHCD_CONTROLLER hc, PXHCI_TOL_RETRY_IN in,
                                 ULONG scanSlots)
{
    PXHCI_EXTENSION ext;
    ULONG i;

    ext = &hc->Hc;
    in->Value = ext->Retry.Value;
    in->Terminal = XhciTolTerminalEx(ext->ControllerFailed, ext->Tol.Unreadable,
                                     ext->Tol.Window.Refused,
                                     ext->RecoveryFailuresConsecutive,
                                     XHCI_RECOVERY_MAX_ATTEMPTS,
                                     (hc->Msg4Lost != 0) ? 1UL : 0UL);
    in->Pinned = hc->CommonBufferPinned ? 1UL : 0UL;
    in->Msg4Barred = hc->Msg4Barred ? 1UL : 0UL;
    in->D0 = (hc->Common.DevicePower == PowerDeviceD0) ? 1UL : 0UL;
    in->SuspendedInD0 = hc->SuspendedInD0 ? 1UL : 0UL;
    in->ExtSuspended = (ext->Flags & XHCI_EXT_FLAG_SUSPENDED) != 0 ? 1UL : 0UL;
    in->ShutLatched = hc->ShutLatched ? 1UL : 0UL;
    in->Leave = ext->Retry.Leave;
    /* The window mapped is part of a running lifetime here. */
    in->Started = (hc->ControllerStarted &&
                   (ext->Flags & XHCI_EXT_FLAG_STARTED) != 0 &&
                   hc->BarVa != NULL && ext->ResourceBase != 0)
                      ? 1UL
                      : 0UL;
    in->PnpStarted = (hc->Common.PnpState == HCD_PNP_STARTED) ? 1UL : 0UL;
    in->ThreadStop = hc->ThreadStop ? 1UL : 0UL;
    /* Task 50.30 step 3's leave signal: a route raised while a command is
     * waited for (HcdEnumLeaveRaise, under this lock). */
    in->CmdWaitLeave = (hc->CmdWaitLeave != 0) ? 1UL : 0UL;
    in->SlotsInvalidated = hc->SlotsInvalidated ? 1UL : 0UL;
    in->SlotsUnproven = hc->SlotsUnproven ? 1UL : 0UL;
    in->ReleaseProved = ext->Retry.ReleaseProved;
    if (scanSlots) {
        in->DevicesLeft = 0;
        for (i = 1; i <= XHCI_MAX_SLOTS && !in->DevicesLeft; i++) {
            in->DevicesLeft = (hc->SlotDevice[i] != NULL) ? 1UL : 0UL;
        }
    }
    in->Pad = 0;
}

/* The capability guard's revalidation (record 38 section 7.3): where the
 * guard is set, seven reads derived into a local decode and compared with
 * the lifetime's; nothing written. XHCI_RETRY_REVAL_*. Controller lock
 * held; IRQL DISPATCH_LEVEL. */
static ULONG hcdRetryRevalLocked(PXHCI_EXTENSION ext)
{
    XHCI_HC_INFO recheck;
    ULONG derived;

    if (ext->HcInfoStatus == XHCI_HC_OK) {
        return XHCI_RETRY_REVAL_NOT_NEEDED;
    }
    derived = (XhciDeriveControllerInfo(ext, &recheck) == XHCI_HC_OK) ? 1UL
                                                                       : 0UL;
    return XhciTolRetryRevalOutcome(
        1, derived, derived ? XhciHcInfoEqual(&ext->HcInfo, &recheck) : 0UL);
}

/* The managed root ports and their count, for the sweep's end. The
 * controller thread; IRQL PASSIVE_LEVEL, no lock (the port map is fixed for
 * the lifetime). */
static ULONG hcdRetryManaged(PHCD_CONTROLLER hc)
{
    PXHCI_EXTENSION ext;
    ULONG count;
    ULONG port;

    ext = &hc->Hc;
    count = ext->PortMap.PortCount;
    if (count > XHCI_MAX_ROOT_PORTS) {
        count = XHCI_MAX_ROOT_PORTS;
    }
    for (port = 1; port <= count; port++) {
        hc->RetryManaged[port - 1] =
            XhciPortIsManaged(&ext->PortMap, port) ? (UCHAR)1 : (UCHAR)0;
    }
    return count;
}

/* The sweep's end carried out: the pure core's verdict per port (its
 * evidence already updated) given to the location observers, the drop
 * first, then the sample. The controller thread; IRQL PASSIVE_LEVEL, no
 * lock held (HcdTolLocObserve takes what it needs). */
static VOID hcdRetryFeed(PHCD_CONTROLLER hc, ULONG count)
{
    ULONG i;
    UCHAR f;

    for (i = 0; i < count; i++) {
        f = hc->RetryFeed[i];
        if ((f & XHCI_RETRY_FEED_BREAK) != 0) {
            HcdTolLocObserve(hc, &hc->Ports[i], 0, 0, 0);
        }
        if ((f & XHCI_RETRY_FEED_SAMPLE) != 0) {
            HcdTolLocObserve(hc, &hc->Ports[i],
                             (f & XHCI_RETRY_FEED_CCS) != 0 ? 1UL : 0UL, 1,
                             (f & XHCI_RETRY_FEED_EDGE) != 0 ? 1UL : 0UL);
        }
    }
}

/* Every root port's evidence dropped (record 38 section 6.3), as a
 * discarded sweep drops it. The controller thread; IRQL PASSIVE_LEVEL, no
 * lock held. */
static VOID hcdRetryBreakAll(PHCD_CONTROLLER hc, ULONG now)
{
    ULONG count;

    count = hcdRetryManaged(hc);
    (VOID)XhciTolRetrySweepEnd(hc->RetryPort, hc->RetrySweep, hc->RetryManaged,
                               count, 1, 0, now, XHCI_TOL_STABLE_DISC_TICKS,
                               hc->RetryFeed);
    hcdRetryFeed(hc, count);
    hc->RetryHaveLast = 0;
}

/*
 * One sweep of the root ports (record 38 section 6.1): an admission hold,
 * then a hold per port, the controller lock released between them; a sweep
 * interrupted is discarded whole and is a missed pass. Returns 1 when an
 * episode completed. The power gate held; IRQL PASSIVE_LEVEL, raised inside
 * each hold.
 */
static ULONG hcdRetrySweep(PHCD_CONTROLLER hc, ULONG now)
{
    PXHCI_EXTENSION ext;
    XHCI_TOL_RETRY_IN in;
    KIRQL oldIrql;
    ULONG admitted;
    ULONG gen;
    ULONG usbcmd;
    ULONG usbsts;
    ULONG discard;
    ULONG off;
    ULONG offPort;
    ULONG hold;
    ULONG portsc;
    ULONG port;
    ULONG held;
    ULONG episodes;
    ULONG reval;
    ULONG count;
    ULONG gap;

    ext = &hc->Hc;
    discard = 0;
    off = 0;
    offPort = 0;
    gen = 0;
    usbcmd = 0;
    usbsts = 0;
    XhciControllerLockAcquire(ext, &oldIrql);
    hcdRetryInputsLocked(hc, &in, 1);
    admitted = 0;
    if (XhciTolRetryAdmit(&in, 0) == XHCI_RETRY_ADMIT_OK) {
        reval = hcdRetryRevalLocked(ext);
        admitted = (reval == XHCI_RETRY_REVAL_NOT_NEEDED ||
                    reval == XHCI_RETRY_REVAL_EQUAL)
                       ? 1UL
                       : 0UL;
    }
    if (!admitted) {
        discard = 1;
    } else {
        gen = ext->Retry.LeaveGen;
        usbcmd = XhciReadOp(ext, XHCI_OP_USBCMD);
        usbsts = XhciReadOp(ext, XHCI_OP_USBSTS);
#if defined(XHCI_FLAVOUR_QEMU)
        usbcmd = HcdInjResetAnswer(ext, XHCI_OP_USBCMD, usbcmd);
        usbsts = HcdInjResetAnswer(ext, XHCI_OP_USBSTS, usbsts);
#endif
        if (usbsts == 0xFFFFFFFFUL) {
            discard = 1;
            off = 1;
        } else if (XhciTolResetRunning(usbcmd, usbsts)) {
            discard = 1;
        }
    }
    XhciControllerLockRelease(ext, oldIrql);

    held = 0;
    for (port = 1; !discard && port <= ext->PortMap.PortCount &&
                   port <= XHCI_MAX_ROOT_PORTS; port++) {
        hc->RetrySweep[port - 1] = HCD_RETRY_NOT_SAMPLED;
        if (!XhciPortIsManaged(&ext->PortMap, port)) {
            continue;
        }
#if defined(XHCI_FLAVOUR_QEMU)
        if (held == 1) {
            HcdInjSweepBreak(hc);
        }
#endif
        XhciControllerLockAcquire(ext, &oldIrql);
        hcdRetryInputsLocked(hc, &in, 0);
        admitted = (XhciTolRetryAdmit(&in, 0) == XHCI_RETRY_ADMIT_OK)
                       ? 1UL
                       : 0UL;
        portsc = 0;
        if (admitted && ext->Retry.LeaveGen == gen) {
            portsc = XhciReadPortsc(ext, port);
#if defined(XHCI_FLAVOUR_QEMU)
            portsc = HcdInjRetryPortsc(hc, port, portsc);
#endif
        }
        hold = XhciTolRetrySweepHold(admitted, gen, ext->Retry.LeaveGen,
                                     portsc);
        XhciControllerLockRelease(ext, oldIrql);
        if (hold != XHCI_RETRY_HOLD_READ) {
            discard = 1;
            if (hold == XHCI_RETRY_HOLD_OFF) {
                off = 1;
                offPort = port;
            }
            break;
        }
        hc->RetrySweep[port - 1] = portsc;
        held++;
    }

    if (discard) {
        XhciControllerLockAcquire(ext, &oldIrql);
        if (XhciTolRetrySweepMissed(&ext->Retry, off, offPort)) {
            XhciLogNoteLocked(ext, "ctrl.retry.sample.off", offPort);
        }
        XhciControllerLockRelease(ext, oldIrql);
        /* Discarded whole: no baseline, episode or observer feed from any
         * port of it, every port's evidence dropped. */
        hcdRetryBreakAll(hc, now);
        return 0;
    }

    /* The location observer's only feeder while the controller is failed
     * (record 38 section 6.6): the sampled CCS, an edge where it changed,
     * never CSC. */
    gap = XhciTolRetrySampleGap(hc->RetryHaveLast, hc->RetryLast, now);
    count = hcdRetryManaged(hc);
    episodes = XhciTolRetrySweepEnd(hc->RetryPort, hc->RetrySweep,
                                    hc->RetryManaged, count, 0, gap, now,
                                    XHCI_TOL_STABLE_DISC_TICKS, hc->RetryFeed);
    hcdRetryFeed(hc, count);
    if (episodes != 0) {
        XhciControllerLockAcquire(ext, &oldIrql);
        for (port = 1; port <= count; port++) {
            if ((hc->RetryFeed[port - 1] & XHCI_RETRY_FEED_EPISODE) == 0) {
                continue;
            }
            if (ext->Retry.Episodes != 0xFFFFFFFFUL) {
                ext->Retry.Episodes++;
            }
            ext->Retry.LastEpisodePort = port;
            XhciLogNoteLocked(
                ext, "ctrl.retry.port",
                (port << 8) |
                    ((hc->RetryFeed[port - 1] & XHCI_RETRY_FEED_CCS) != 0
                         ? 1UL
                         : 0UL));
        }
        XhciControllerLockRelease(ext, oldIrql);
    }
    hc->RetryHaveLast = 1;
    hc->RetryLast = now;
    return (episodes != 0) ? 1UL : 0UL;
}

/* IRQL: PASSIVE_LEVEL (the start, before the thread). */
VOID HcdRetryStart(PHCD_CONTROLLER hc, ULONG found, ULONG raw, ULONG scale)
{
    PXHCI_EXTENSION ext;
    ULONG i;

    ext = &hc->Hc;
    XhciTolRetryInit(&ext->Retry, found, raw, scale);
    for (i = 0; i < XHCI_MAX_ROOT_PORTS; i++) {
        XhciTolRetryPortBreak(&hc->RetryPort[i]);
        hc->RetrySweep[i] = HCD_RETRY_NOT_SAMPLED;
    }
    hc->RetryHaveLast = 0;
    hc->RetryLast = 0;
    hc->RetryBreak = 0;
    /* Bit 31 a number, bit 30 another type, 29:16 the number read
     * (held at 16383), 15:0 the value in effect. */
    XhciLogNote(ext, "ctrl.retry.value",
                (found == XHCI_TOL_RETRY_DWORD ? 0x80000000UL
                 : found == XHCI_TOL_RETRY_OTHER_TYPE ? 0x40000000UL
                                                      : 0UL) |
                    (((raw > 16383UL) ? 16383UL : raw) << 16) |
                    ext->Retry.Value);
}

/*
 * The thread's step, after hcdContain (record 38 section 3): the
 * observation of the terminal, the clean run, the sweep, LATCHED to DUE and
 * the arming. Nothing of the latch moves here; the commit is hcdRecover's.
 * IRQL: PASSIVE_LEVEL, the controller thread.
 */
VOID HcdRetryThreadStep(PHCD_CONTROLLER hc)
{
    PXHCI_EXTENSION ext;
    PXHCI_TOL_RETRY r;
    KIRQL oldIrql;
    ULONG now;
    ULONG terminal;
    ULONG ev;
    ULONG wasBurst;
    ULONG healthy;
    ULONG breakAll;
    ULONG sample;
    ULONG episode;
    ULONG cause;
    ULONG refused;
    ULONG devices;
    ULONG i;
    XHCI_TOL_RETRY_HEALTH health;

    ext = &hc->Hc;
    r = &ext->Retry;
    if (r->State == XHCI_RETRY_OFF) {
        return;
    }
    HcdPowerGateEnter(hc);
    now = HcdTolNow(hc);
    XhciControllerLockAcquire(ext, &oldIrql);
    terminal = XhciTolTerminalEx(ext->ControllerFailed, ext->Tol.Unreadable,
                                 ext->Tol.Window.Refused,
                                 ext->RecoveryFailuresConsecutive,
                                 XHCI_RECOVERY_MAX_ATTEMPTS,
                                 (hc->Msg4Lost != 0) ? 1UL : 0UL);
    wasBurst = r->Burst;
    ev = XhciTolRetryObserve(r, terminal, hc->CommonBufferPinned ? 1UL : 0UL,
                             now, hcdRetrySysNow());
    breakAll = hc->RetryBreak;
    hc->RetryBreak = 0;
    if (ev == XHCI_RETRY_EV_LATCHED) {
        breakAll = 1;
        XhciLogNoteLocked(ext, "ctrl.retry.latched",
                          (terminal << 24) | ((r->Level & 0xFFUL) << 16) |
                              (r->LatchWait & 0xFFFFUL));
    } else if (ev == XHCI_RETRY_EV_EXCLUDED) {
        if (wasBurst) {
            XhciLogNoteLocked(ext, "ctrl.retry.burst",
                              (XHCI_RETRY_BURST_EXCLUDED << 16) |
                                  (r->LastBurstAttempts & 0xFFFFUL));
        }
        if (XhciTolRetryNoteOnce(r, XHCI_RETRY_NOTE_EXCLUDED)) {
            XhciLogNoteLocked(ext, "ctrl.retry.excluded", terminal);
        }
    }
    devices = 0;
    for (i = 1; i <= XHCI_MAX_SLOTS && !devices; i++) {
        devices = (hc->SlotDevice[i] != NULL) ? 1UL : 0UL;
    }
    r->ReleaseState = XhciTolRetryReleaseBits(
        hc->SlotsInvalidated ? 1UL : 0UL, hc->SlotsUnproven ? 1UL : 0UL,
        r->ReleaseProved, hc->TerminalReleased ? 1UL : 0UL, devices,
        hc->CommonBufferPinned ? 1UL : 0UL);
    health.Terminal = terminal;
    health.Requested = ext->RecoveryRequested ? 1UL : 0UL;
    health.Initialized = (ext->Flags & XHCI_EXT_FLAG_INITIALIZED) != 0 ? 1UL
                                                                        : 0UL;
    health.ExtSuspended = (ext->Flags & XHCI_EXT_FLAG_SUSPENDED) != 0 ? 1UL
                                                                       : 0UL;
    health.D0 = (hc->Common.DevicePower == PowerDeviceD0) ? 1UL : 0UL;
    health.SuspendedInD0 = hc->SuspendedInD0 ? 1UL : 0UL;
    health.Started = hc->ControllerStarted ? 1UL : 0UL;
    health.Leave = r->Leave;
    health.ShutLatched = hc->ShutLatched ? 1UL : 0UL;
    health.Pad = 0;
    healthy = XhciTolRetryHealthy(&health);
    if (XhciTolRetryCleanPass(r, healthy, ext->ResetControllerCalls, now)) {
        XhciLogNoteLocked(ext, "ctrl.retry.reset", r->BackoffEnds);
    }
    sample = ((r->State == XHCI_RETRY_LATCHED || r->State == XHCI_RETRY_DUE) &&
              !r->SampleOff)
                 ? 1UL
                 : 0UL;
    XhciControllerLockRelease(ext, oldIrql);

    if (breakAll) {
        hcdRetryBreakAll(hc, now);
    }
    episode = sample ? hcdRetrySweep(hc, now) : 0UL;

    XhciControllerLockAcquire(ext, &oldIrql);
    refused = r->StormRefused;
    cause = XhciTolRetryDueStep(r, episode, now, hcdRetrySysNow());
    if (r->StormRefused != refused &&
        XhciTolRetryNoteOnce(r, XHCI_RETRY_NOTE_STORM)) {
        XhciLogNoteLocked(ext, "ctrl.retry.storm", r->LastEpisodePort);
    }
    /* Armed at once; a commit the next pass refuses re-arms here, quietly
     * and without a wake, until it commits or the state moves. */
    (VOID)XhciTolRetryArm(r);
    XhciControllerLockRelease(ext, oldIrql);
    HcdPowerGateLeave(hc);
    if (cause != XHCI_RETRY_CAUSE_NONE) {
        HcdThreadWake(hc);
    }
}

/*
 * The commit (record 38 section 7.4), from hcdRecover's own hold: the power
 * gate and the controller lock held, before its ordinary admission. One
 * decision: every row, the USBSTS read, the capability revalidation; if all
 * pass, the latch is reset and the request taken here, and hcdRecover's
 * admission then runs the recovery; if any fails, nothing of the latch
 * moves. Returns 1 when committed, *usbsts the read its defer takes.
 * IRQL: DISPATCH_LEVEL.
 */
ULONG HcdRetryCommitLocked(PHCD_CONTROLLER hc, PULONG usbsts)
{
    PXHCI_EXTENSION ext;
    PXHCI_TOL_RETRY r;
    XHCI_TOL_RETRY_IN in;
    XHCI_TOL_RETRY_LATCH latch;
    ULONG status;
    ULONG reval;
    ULONG outcome;
    ULONG now;

    ext = &hc->Hc;
    r = &ext->Retry;
    if (!r->Armed) {
        return 0;
    }
#if defined(XHCI_FLAVOUR_QEMU)
    if (HcdInjRetryHeld(hc)) {
        return 0;
    }
#endif
    now = HcdTolNow(hc);
    hcdRetryInputsLocked(hc, &in, 1);
    if (!XhciTolRetryCommitAdmit(r, XhciTolRetryAdmit(&in, 0))) {
        return 0;
    }
    status = XhciReadOp(ext, XHCI_OP_USBSTS);
#if defined(XHCI_FLAVOUR_QEMU)
    status = HcdInjRetryOnes(hc, status);
#endif
    /* Record 38 section 7.3: never an override of the guard; the
     * lifetime's decode restored only when seven reads equal it. No
     * capability read behind a USBSTS of all ones. */
    reval = (status == 0xFFFFFFFFUL) ? XHCI_RETRY_REVAL_NOT_NEEDED
                                     : hcdRetryRevalLocked(ext);
    latch.Failures = ext->RecoveryFailuresConsecutive;
    latch.TerminalReleased = hc->TerminalReleased;
    latch.Requested = ext->RecoveryRequested;
    latch.GuardSet = (ext->HcInfoStatus != XHCI_HC_OK) ? 1UL : 0UL;
    outcome = XhciTolRetryCommitFinish(r, &ext->Tol.Window, &latch, status,
                                       reval, now, hcdRetrySysNow());
    if (outcome == XHCI_RETRY_COMMIT_SKIP_ONES) {
        XhciLogNoteLocked(ext, "ctrl.retry.skip", XHCI_RETRY_SKIP_ONES);
        return 0;
    }
    if (outcome != XHCI_RETRY_COMMIT_DONE) {
        XhciLogNoteLocked(ext, "ctrl.retry.skip", XHCI_RETRY_SKIP_LAYOUT);
        return 0;
    }
    if (!latch.GuardSet) {
        ext->HcInfoStatus = XHCI_HC_OK;
    }
    ext->RecoveryFailuresConsecutive = latch.Failures;
    hc->TerminalReleased = latch.TerminalReleased;
    ext->RecoveryRequested = latch.Requested;
    XhciLogNoteLocked(ext, "ctrl.retry",
                      (r->Tries << 16) | hcdRetrySeconds(r->LastWait));
    XhciLogNoteLocked(ext, "ctrl.retry.why", r->LastCause);
    *usbsts = status;
    return 1;
}

/* hcdRecover's admission of a re-requested attempt: 1 when the burst
 * yields to a leave (record 38 section 7.4). Controller lock held; IRQL
 * DISPATCH_LEVEL. */
ULONG HcdRetryDefersLocked(PHCD_CONTROLLER hc)
{
    return XhciTolRetryBurstDefers(&hc->Hc.Retry);
}

/* An attempt hcdRecover admitted that the commit did not begin: counted in
 * the burst, if one runs. Controller lock held; IRQL DISPATCH_LEVEL. */
VOID HcdRetryAttemptLocked(PHCD_CONTROLLER hc)
{
    XhciTolRetryBurstAttempt(&hc->Hc.Retry);
}

/*
 * A recovery attempt's end (hcdRecover): a success ends the burst
 * (outcome 1) and a latched resume's FAILED phase, before the recovery's
 * re-enumeration presents a device; a run of failures spent ends it
 * latched (outcome 2). Controller lock held; IRQL DISPATCH_LEVEL.
 */
VOID HcdRetryRecoverEndLocked(PHCD_CONTROLLER hc, ULONG recovered,
                              ULONG latched)
{
    PXHCI_EXTENSION ext;
    PXHCI_TOL_RETRY r;
    ULONG phase;
    ULONG advance;

    ext = &hc->Hc;
    r = &ext->Retry;
    if (!r->Burst) {
        return;
    }
    if (recovered) {
        XhciTolRetryBurstEnd(r, XHCI_RETRY_BURST_RECOVERED);
        XhciLogNoteLocked(ext, "ctrl.retry.burst",
                          (XHCI_RETRY_BURST_RECOVERED << 16) |
                              (r->LastBurstAttempts & 0xFFFFUL));
        phase = XhciTolRetryResumeEnd(hc->ResumePhase, r->ResumeFailed, 1,
                                      &advance);
        if (advance) {
            hc->ResumePhase = phase;
            hc->MarkGen++;
            r->ResumeFailed = 0;
        }
    } else if (latched) {
        XhciTolRetryBurstEnd(r, XHCI_RETRY_BURST_LATCHED);
        XhciLogNoteLocked(ext, "ctrl.retry.burst",
                          (XHCI_RETRY_BURST_LATCHED << 16) |
                              (r->LastBurstAttempts & 0xFFFFUL));
    }
}

/* The lifetime's end, after the thread has left (hcdStopBody): a burst
 * still running ends (outcome 4). IRQL: PASSIVE_LEVEL. */
VOID HcdRetryStop(PHCD_CONTROLLER hc)
{
    PXHCI_EXTENSION ext;
    KIRQL oldIrql;

    ext = &hc->Hc;
    XhciControllerLockAcquire(ext, &oldIrql);
    if (ext->Retry.Burst) {
        XhciTolRetryBurstEnd(&ext->Retry, XHCI_RETRY_BURST_ENDED);
        XhciLogNoteLocked(ext, "ctrl.retry.burst",
                          (XHCI_RETRY_BURST_ENDED << 16) |
                              (ext->Retry.LastBurstAttempts & 0xFFFFUL));
    }
    XhciControllerLockRelease(ext, oldIrql);
}

/* A leave flag's write (record 38 section 7.5): the value and its
 * generation, under the controller lock, before the path's own work.
 * IRQL: PASSIVE_LEVEL (the PnP dispatch). */
VOID HcdRetryLeave(PHCD_CONTROLLER hc, ULONG path, ULONG phase,
                   ULONG outcome)
{
    PXHCI_EXTENSION ext;
    KIRQL oldIrql;

    ext = &hc->Hc;
    XhciControllerLockAcquire(ext, &oldIrql);
    XhciTolRetryLeaveWrite(&ext->Retry, path, phase, outcome);
    XhciControllerLockRelease(ext, oldIrql);
}

/* A suspend or a resume: the clean run broken; at a suspend, a latched
 * resume's mark cleared where the phase becomes PENDING. Controller lock
 * held (the power gate too, by the power path); IRQL DISPATCH_LEVEL. */
VOID HcdRetryPowerLocked(PHCD_CONTROLLER hc, ULONG suspend)
{
    XhciTolRetryCleanBreak(&hc->Hc.Retry);
    if (suspend) {
        hc->Hc.Retry.ResumeFailed = 0;
    }
}

/*
 * The latched resume (record 38 section 9), where hcdResumeContained has
 * refused: only with the value not 0 and every row true, the controller is
 * taken to D0 without reinitializing. Returns 1 when it was. Value 0 never
 * reaches past the first row, so today's refusal stands exactly. The power
 * gate held by the D0 path; IRQL PASSIVE_LEVEL.
 */
ULONG HcdRetryResumeLatched(PHCD_CONTROLLER hc)
{
    PXHCI_EXTENSION ext;
    PXHCI_TOL_RETRY r;
    XHCI_TOL_RETRY_IN in;
    KIRQL oldIrql;
    ULONG admit;
    ULONG failed;

    ext = &hc->Hc;
    r = &ext->Retry;
    XhciControllerLockAcquire(ext, &oldIrql);
    hcdRetryInputsLocked(hc, &in, 1);
    admit = XhciTolRetryAdmit(&in, 1);
    if (hc->Msg4QueryReleased || hc->Msg4Removal) {
        admit = XHCI_RETRY_ADMIT_WAIT_LEAVE;
    }
    XhciControllerLockRelease(ext, oldIrql);
    if (admit != XHCI_RETRY_ADMIT_OK) {
        return 0;
    }
    if (HcdMsg4Live(hc)) {
        /* Mode 4's re-arm and open, in their own context, outside the
         * controller lock; a failure latches the loss as hcdResumeMsg4
         * does, terminal 6, excluded for the lifetime. */
        failed = HcdMsg4Resume(hc);
        if (failed != 0) {
            HcdMsg4LostLatch(hc, (failed == 2) ? HCD_MSG4_LOST_BRAKED
                                               : HCD_MSG4_LOST_REARM);
            return 0;
        }
        XhciControllerLockAcquire(ext, &oldIrql);
        hc->Msg4Barred = 0;
        XhciControllerLockRelease(ext, oldIrql);
    }
    hc->SuspendHaltProven = 0;
    XhciControllerLockAcquire(ext, &oldIrql);
    ext->Flags &= ~XHCI_EXT_FLAG_SUSPENDED;
    ext->SavedStateValid = 0;
    hc->ResumeInvalidating = 0;
    hc->ResumeSlotsInvalidated = 0;
    hc->ResumeKeepReady = 0;
    hc->ResumeKeepPending = 0;
    hc->ResumePhase = XHCI98_RESUME_FAILED;
    hc->MarkGen++;
    r->ResumeFailed = 1;
    XhciTolRetryCleanBreak(r);
    hc->RetryBreak = 1;
    XhciLogNoteLocked(ext, "ctrl.retry.resume", in.Terminal);
    XhciControllerLockRelease(ext, oldIrql);
    return 1;
}

/* ReleaseProved (record 38 section 7.2): HcdEnumService proved an
 * unproven invalidation and drained it. IRQL: PASSIVE_LEVEL. */
VOID HcdRetryReleaseProved(PHCD_CONTROLLER hc)
{
    KIRQL oldIrql;

    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    hc->Hc.Retry.ReleaseProved = 1;
    XhciControllerLockRelease(&hc->Hc, oldIrql);
}

/* The recovery-status request's three words (record 38 section 15.1);
 * returns 1 in EXCLUDED (the door's XHR_F_RETRY_NEVER), else 0.
 * Controller lock held (the door's hold); IRQL DISPATCH_LEVEL. */
ULONG HcdRetryStatusLocked(PHCD_CONTROLLER hc, PULONG value, PULONG tries,
                           PULONG nextS)
{
    ULONG never;

    XhciTolRetryStatusWords(&hc->Hc.Retry, hc->Hc.Tol.Clock, hcdRetrySysNow(),
                            value, tries, nextS, &never);
    return never;
}
