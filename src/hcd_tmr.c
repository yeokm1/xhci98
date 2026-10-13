/*
 * hcd_tmr.c - a finer system timer while polling, the kernel half (design
 * record 15 section 7.2; roadmap-hcd.md task 38.3). The decisions are
 * xhci_poll.c's (XhciPollTmrWant, XhciPollTmrStep, XhciPollTmrCause); this
 * file owns the timer thread that makes the calls, its creation after a
 * successful start and its join at the stop, the routine's lookup per
 * image, and the producers' entries the poller and the power paths call.
 *
 * Under a poll delivery the active period of 1 ms runs at the clock tick
 * (about 10 or 15.6 ms), so polled storage completes about once a tick.
 * While the poller is ACTIVE the thread asks ExSetTimerResolution for
 * XhciPollActiveMs x 10000 (100 ns units) and gives it back when it is not,
 * at most one request per controller, every TRUE paired with its FALSE.
 *
 * **Microsoft warns that ExSetTimerResolution can deadlock against a power
 * IRP** ("Calling ExSetTimerResolution while processing a power IRP": a
 * driver must not call it from the path that processes a power IRP, since
 * the call can wait on the power manager). So only this thread calls it,
 * and nothing on a power path waits for this thread (record 15 section
 * 7.2.7): a power transition sets a bar, a generation and the event and
 * goes on at once; the thread holds no gate, spin lock, IRP or count
 * anyone waits on while it calls; and its one waiter, the stop's join,
 * comes after every child power IRP is settled and every completion worker
 * has left, holding the door gate, which no power path takes.
 *
 * Windows 98 SE and ME: no thread and no look (the 32-bit image may import
 * neither ExSetTimerResolution nor MmGetSystemRoutineAddress), so the poll
 * keeps the clock tick as before; XhciPollFineTimer is read and does
 * nothing there. The 32-bit image takes the routine from xhcihlpr.sys's
 * timer service (hcd_msi86.c, HcdHlprTimerRoutine), the 64-bit image from
 * MmGetSystemRoutineAddress.
 *
 * Lock order: the controller lock, then PollLock (hcd_poll.c's). The
 * producers' *Locked entries run with the controller lock held, at
 * DISPATCH_LEVEL; KeSetEvent with Wait FALSE is callable there.
 *
 * IRQL: each function carries its own.
 */

#include "hcd.h"
#include "xhci_hw.h"
#include "xhci_dbg.h"

/* Once, beside the controller's other objects (HcdControllerInitObjects).
 * IRQL: PASSIVE_LEVEL (AddDevice). */
VOID HcdTmrInitObjects(PHCD_CONTROLLER hc)
{
    PUCHAR p;
    ULONG i;

    p = (PUCHAR)&hc->Tmr;
    for (i = 0; i < sizeof(hc->Tmr); i++) {
        p[i] = 0;
    }
    KeInitializeEvent(&hc->Tmr.Event, SynchronizationEvent, FALSE);
    XhciPollTmrObsReset(&hc->Tmr.Obs);
}

/*
 * Each start, before anything that produces can run (before the poller is
 * opened): no thread, nothing held, the routine unknown, the look's latch
 * clear, a new start serial. The previous start's thread, if any, was
 * joined by its stop. The counters were zeroed with the block
 * (HcdCountersStart). IRQL: PASSIVE_LEVEL.
 */
VOID HcdTmrStartReset(PHCD_CONTROLLER hc)
{
    PHCD_TMR t;
    KIRQL oldIrql;
    KIRQL pollIrql;

    t = &hc->Tmr;
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    t->Live = 0;
    t->Stop = 0;
    t->Bar = 0;
    t->Active = 0;
    t->Gen = 0;
    t->State = XHCI_POLL_TMR_RELEASED;
    t->HeldGen = 0;
    t->Routine = NULL;
    t->Lookable = 0;
    t->Looked = 0;
    t->Created = 0;
    t->ThreadObject = NULL;
    t->ThreadHandle = NULL;
    t->Calls = 0;
    t->ObsEpoch = 0;
    t->ObsPhase = XHCI_POLL_TMR_RELEASED;
    t->StartSerial++;
    KeAcquireSpinLock(&hc->PollLock, &pollIrql);
    XhciPollTmrObsReset(&t->Obs);
    KeReleaseSpinLock(&hc->PollLock, pollIrql);
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    /* No thread waits on it now; re-initialised rather than reset, since
     * KeResetEvent has no import-gate row. */
    KeInitializeEvent(&t->Event, SynchronizationEvent, FALSE);
}

/* ---- the producers (record 15 section 7.2.6) ------------------------- */

/* The thread told an input changed, while it runs. IRQL: DISPATCH_LEVEL,
 * controller lock held. */
VOID HcdTmrSignalLocked(PHCD_CONTROLLER hc)
{
    if (hc->Tmr.Live) {
        (VOID)KeSetEvent(&hc->Tmr.Event, IO_NO_INCREMENT, FALSE);
    }
}

/* The poller's decision: ACTIVE or not (a promotion to the active period
 * counts as ACTIVE). Signalled when the value changes. IRQL:
 * DISPATCH_LEVEL, controller lock held. */
VOID HcdTmrActiveLocked(PHCD_CONTROLLER hc, ULONG active)
{
    active = active ? 1UL : 0UL;
    if (hc->Tmr.Active != active) {
        hc->Tmr.Active = active;
        HcdTmrSignalLocked(hc);
    }
}

/* A boundary - the DPC's park, a revoke, a bar, the stop: no longer
 * ACTIVE and the generation advanced, so a request held across it is given
 * back whatever "wanted" reads after it. Always signalled. IRQL:
 * DISPATCH_LEVEL, controller lock held. */
VOID HcdTmrBoundaryLocked(PHCD_CONTROLLER hc)
{
    hc->Tmr.Active = 0;
    hc->Tmr.Gen++;
    HcdTmrSignalLocked(hc);
}

/* A grant (every site of HcdPollGrant that grants): the bar cleared and
 * the thread told, always. IRQL: DISPATCH_LEVEL, controller lock held. */
VOID HcdTmrGrantLocked(PHCD_CONTROLLER hc)
{
    hc->Tmr.Bar = 0;
    HcdTmrSignalLocked(hc);
}

/*
 * The bar, from a path that takes the controller away and must not wait
 * for the call: the device SET_POWER to a lower state and the direct
 * transition toward sleep, beside their HcdPollRevoke. A bit, a generation
 * and the event; the release happens later on the thread. IRQL:
 * <= DISPATCH_LEVEL.
 */
VOID HcdTmrBar(PHCD_CONTROLLER hc)
{
    KIRQL oldIrql;

    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    hc->Tmr.Bar = 1;
    HcdTmrBoundaryLocked(hc);
    XhciControllerLockRelease(&hc->Hc, oldIrql);
}

/* ---- the thread (record 15 sections 7.2.5 and 7.2.6) ----------------- */

/* The helper's state after a look, into the counter block. IRQL:
 * DISPATCH_LEVEL, controller lock held. */
static VOID hcdTmrHlprCountersLocked(PHCD_CONTROLLER hc)
{
    PXHCIHC_COUNTERS c;

    c = &hc->Counters;
    c->Msi86State = hc->HlprBind.State;
    c->Msi86Looks = hc->HlprBind.Looks;
    c->Msi86Status = (ULONG)hc->HlprStatus;
    c->Msi86Refusal = hc->HlprRefusal;
    c->Msi86HelperVersion = hc->HlprHelperVersion;
    c->HlprMsiVerdict = hc->HlprMsiVerdict;
    c->HlprTimerVerdict = hc->HlprTimerVerdict;
}

/*
 * The thread: reads the inputs in one controller-lock hold, looks for the
 * routine the first time it would hold without one (32-bit Windows 2000
 * and XP, once per start), steps, calls with no lock held, records, and
 * loops; waits on its event only when a step asks for nothing. Its first
 * pass reconciles without waiting. On TmrStop "wanted" is 0: it gives a
 * held request back and leaves. IRQL: PASSIVE_LEVEL.
 */
static VOID NTAPI hcdTmrThread(PVOID Context)
{
    PHCD_CONTROLLER hc;
    PHCD_TMR t;
    PXHCI_EXTENSION ext;
    XHCI_POLL_TMR_IN in;
    KIRQL oldIrql;
    KIRQL pollIrql;
    LARGE_INTEGER now;
    HCD_SET_TIMER_RESOLUTION routine;
    ULONG want;
    ULONG gen;
    ULONG stop;
    ULONG bar;
    ULONG act;
    ULONG ret;
    ULONG seq;
    ULONG ms;
    BOOLEAN set;
#if defined(XHCI_FLAVOUR_QEMU)
    LARGE_INTEGER due;
#endif

    hc = (PHCD_CONTROLLER)Context;
    t = &hc->Tmr;
    ext = &hc->Hc;
    for (;;) {
        XhciControllerLockAcquire(ext, &oldIrql);
        in.Switch = t->Switch;
        in.Routine = (t->Routine != NULL || (t->Lookable && !t->Looked))
                         ? 1UL
                         : 0UL;
        in.Stop = t->Stop;
        in.Delivery = ext->EventDelivery;
        in.Bar = t->Bar;
        in.PollService = ext->PollService;
        in.D0 = (hc->Common.DevicePower == PowerDeviceD0) ? 1UL : 0UL;
        in.SuspendedInD0 = hc->SuspendedInD0;
        in.Active = t->Active;
        KeAcquireSpinLock(&hc->PollLock, &pollIrql);
        in.Role = hc->Poll.Role;
        KeReleaseSpinLock(&hc->PollLock, pollIrql);
        gen = t->Gen;
        stop = t->Stop;
        bar = t->Bar;
        XhciControllerLockRelease(ext, oldIrql);

        want = XhciPollTmrWant(&in);
        if (want && t->Routine == NULL) {
            /* Record 18 section 15.4: the thread's own look, at most once
             * per start, the first time it would hold. */
            routine = (HCD_SET_TIMER_RESOLUTION)HcdHlprTimerRoutine(hc, 1);
            XhciControllerLockAcquire(ext, &oldIrql);
            t->Looked = 1;
            t->Routine = routine;
            hc->Counters.TmrSource = (routine != NULL)
                                         ? XHCI_POLL_TMR_SRC_HELPER
                                         : XHCI_POLL_TMR_SRC_NOT_FOUND;
            hcdTmrHlprCountersLocked(hc);
            XhciLogNoteLocked(ext, "poll.timer.look", hc->Counters.TmrSource);
            XhciControllerLockRelease(ext, oldIrql);
            continue;
        }

        act = XhciPollTmrStep(&t->State, &t->HeldGen, want, gen);
        if (act == XHCI_POLL_TMR_NONE) {
            if (stop) {
                break;
            }
            (VOID)KeWaitForSingleObject(&t->Event, Executive, KernelMode,
                                        FALSE, NULL);
            continue;
        }

        /* The call, with no lock held; the epoch odd across it so no poll
         * interval that touches it is counted (section 7.2.9). */
        XhciControllerLockAcquire(ext, &oldIrql);
        t->ObsEpoch++;
        t->Calls++;
        seq = t->Calls;
#if defined(XHCI_FLAVOUR_QEMU)
        XhciLogNoteLocked(ext, "poll.timer.call",
                          ((t->StartSerial & 0xFFFFUL) << 16) |
                              (seq & 0xFFFFUL));
#endif
        XhciControllerLockRelease(ext, oldIrql);
#if defined(XHCI_FLAVOUR_QEMU)
        /* Leg T3/T4's delay (record 15 section 7.2.7), before the call. */
        if (t->DelayMs != 0) {
            HcdRelativeMs(&due, t->DelayMs);
            (VOID)KeDelayExecutionThread(KernelMode, FALSE, &due);
        }
#endif
        set = FALSE;
        if (act == XHCI_POLL_TMR_HOLD) {
            set = TRUE;
        }
        ret = t->Routine(t->Desired, set);
        KeQuerySystemTime(&now);

        XhciControllerLockAcquire(ext, &oldIrql);
#if defined(XHCI_FLAVOUR_QEMU)
        XhciLogNoteLocked(ext, "poll.timer.return",
                          ((t->StartSerial & 0xFFFFUL) << 16) |
                              (seq & 0xFFFFUL));
#else
        UNREFERENCED_PARAMETER(seq);
#endif
        t->ObsEpoch++;
        t->ObsPhase = t->State;
        if (act == XHCI_POLL_TMR_HOLD) {
            hc->Counters.TmrHeld = 1;
            hc->Counters.TmrHolds++;
            hc->Counters.TmrActualHeld = ret;
            t->HeldSince = now;
            /* A boundary between the inputs' read and this call: the
             * request was held a moment across it, and the event the
             * boundary set brings its release on the next step. */
            if (t->Gen != t->HeldGen) {
                hc->Counters.TmrHoldsRaced++;
            }
            XhciLogNoteLocked(ext, "poll.timer.hold", ret);
        } else {
            hc->Counters.TmrHeld = 0;
            hc->Counters.TmrReleases++;
            hc->Counters.TmrActualReleased = ret;
            if (act == XHCI_POLL_TMR_RELEASE_FORCED) {
                hc->Counters.TmrReleasesForced++;
            }
            hc->Counters.TmrReleaseCause = XhciPollTmrCause(
                stop, bar, (act == XHCI_POLL_TMR_RELEASE_FORCED) ? 1UL : 0UL);
            ms = XhciPollTmrElapsedMs((ULONG)t->HeldSince.HighPart,
                                      t->HeldSince.LowPart,
                                      (ULONG)now.HighPart, now.LowPart);
            hc->Counters.TmrHeldMs =
                XhciPollTmrAddMs(hc->Counters.TmrHeldMs, ms);
            XhciLogNoteLocked(ext, "poll.timer.release",
                              (hc->Counters.TmrReleaseCause << 24) |
                                  (ret & 0x00FFFFFFUL));
        }
        XhciControllerLockRelease(ext, oldIrql);
    }
    (VOID)PsTerminateSystemThread(STATUS_SUCCESS);
}

/*
 * The thread, created by the start after it has succeeded (hcd_pnp.c,
 * after D0 is published and the poller granted), in the PnP manager's
 * start thread. Only on NT, only with the switch on, and only when the
 * poller was opened (a poll delivery, or the watch role under a line in
 * mode 0, which a dead line may move to polling). The routine is taken
 * first where no look is needed: on 64-bit MmGetSystemRoutineAddress; on
 * 32-bit Vista and 7 the timer service of the binding the filter and the
 * start already made, copied here inside the PnP manager's serialisation.
 * On 32-bit Windows 2000 and XP the thread looks itself. A routine already
 * known to be absent makes no thread. A creation that fails is counted and
 * the start still succeeds: the controller polls at the tick. On an object
 * reference that fails the handle is kept and the reference retried at the
 * join (hcdSuspDoneCreate's rule). IRQL: PASSIVE_LEVEL.
 */
VOID HcdTmrCreate(PHCD_CONTROLLER hc)
{
    PHCD_TMR t;
    KIRQL oldIrql;
    KIRQL pollIrql;
    HANDLE handle;
    ULONG role;
    NTSTATUS status;

    t = &hc->Tmr;
    if (!IoIsWdmVersionAvailable(1, 0x10) || !t->Switch) {
        return;
    }
    KeAcquireSpinLock(&hc->PollLock, &pollIrql);
    role = hc->Poll.Role;
    KeReleaseSpinLock(&hc->PollLock, pollIrql);
    if (role != XHCI_POLL_ROLE_SERVE && role != XHCI_POLL_ROLE_WATCH) {
        return;
    }
#if defined(_WIN64)
    t->Routine = (HCD_SET_TIMER_RESOLUTION)HcdHlprTimerRoutine(hc, 0);
    hc->Counters.TmrSource = (t->Routine != NULL)
                                 ? XHCI_POLL_TMR_SRC_SYSTEM
                                 : XHCI_POLL_TMR_SRC_NOT_FOUND;
#else
    if (IoIsWdmVersionAvailable(6, 0)) {
        t->Routine = (HCD_SET_TIMER_RESOLUTION)HcdHlprTimerRoutine(hc, 0);
        hc->Counters.TmrSource = (t->Routine != NULL)
                                     ? XHCI_POLL_TMR_SRC_HELPER
                                     : XHCI_POLL_TMR_SRC_NOT_FOUND;
    } else {
        t->Lookable = 1;
    }
#endif
    if (t->Routine == NULL && !t->Lookable) {
        XhciLogNote(&hc->Hc, "poll.timer.source", hc->Counters.TmrSource);
        return;
    }

    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    t->Live = 1;
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    status = PsCreateSystemThread(&handle, THREAD_ALL_ACCESS, NULL, NULL,
                                  NULL, hcdTmrThread, hc);
    if (!NT_SUCCESS(status)) {
        XhciControllerLockAcquire(&hc->Hc, &oldIrql);
        t->Live = 0;
        hc->Counters.TmrThread = XHCI_POLL_TMR_THREAD_FAILED;
        XhciControllerLockRelease(&hc->Hc, oldIrql);
        XhciLogNote(&hc->Hc, "poll.timer.thread.failed", (ULONG)status);
        return;
    }
    /* A NULL object type: PsThreadType is a data import (hcdThreadStart). */
    if (NT_SUCCESS(ObReferenceObjectByHandle(handle, THREAD_ALL_ACCESS, NULL,
                                             KernelMode, &t->ThreadObject,
                                             NULL))) {
        (VOID)ZwClose(handle);
    } else {
        t->ThreadObject = NULL;
        t->ThreadHandle = handle;
    }
    t->Created = 1;
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    hc->Counters.TmrThread = XHCI_POLL_TMR_THREAD_RUNNING;
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    XhciLogNote(&hc->Hc, "poll.timer.source", hc->Counters.TmrSource);
}

/*
 * The stop's first step (record 15 section 7.2.5), before the controller
 * thread is joined: TmrStop and the bar, a boundary, the event - so the
 * thread gives a held request back at once and takes none. Waits for
 * nothing. IRQL: PASSIVE_LEVEL.
 */
VOID HcdTmrStopEarly(PHCD_CONTROLLER hc)
{
    KIRQL oldIrql;

    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    hc->Tmr.Stop = 1;
    hc->Tmr.Bar = 1;
    HcdTmrBoundaryLocked(hc);
    XhciControllerLockRelease(&hc->Hc, oldIrql);
}

/*
 * The join (record 15 section 7.2.5): late in the stop, after the devices
 * are dropped (every child D-IRP settled at its unlink), the idle callback
 * hand-off stopped and the completion workers drained, before the release
 * and outside the power gate. By its object, or the kept handle's
 * reference retried until had - never an event, which would leave the
 * thread's last instructions in this image unjoined. IRQL: PASSIVE_LEVEL.
 */
VOID HcdTmrJoin(PHCD_CONTROLLER hc)
{
    PHCD_TMR t;
    LARGE_INTEGER due;
    KIRQL oldIrql;

    t = &hc->Tmr;
    if (!t->Created) {
        return;
    }
    while (t->ThreadHandle != NULL) {
        if (NT_SUCCESS(ObReferenceObjectByHandle(t->ThreadHandle,
                                                 THREAD_ALL_ACCESS, NULL,
                                                 KernelMode, &t->ThreadObject,
                                                 NULL))) {
            (VOID)ZwClose(t->ThreadHandle);
            t->ThreadHandle = NULL;
            break;
        }
        HcdRelativeMs(&due, 10);
        (VOID)KeDelayExecutionThread(KernelMode, FALSE, &due);
    }
    (VOID)KeWaitForSingleObject(t->ThreadObject, Executive, KernelMode,
                                FALSE, NULL);
    ObDereferenceObject(t->ThreadObject);
    t->ThreadObject = NULL;
    t->Created = 0;
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    t->Live = 0;
    hc->Counters.TmrThread = XHCI_POLL_TMR_THREAD_JOINED;
    XhciLogNoteLocked(&hc->Hc, "poll.timer.join", hc->Counters.TmrHolds);
    XhciControllerLockRelease(&hc->Hc, oldIrql);
}
