/*
 * hcd_svc.c - the services the adapted controller files call (hcd_svc.h has
 * the contract of each, and what it replaces).
 *
 * IRQL: each function carries its own.
 */

#include "hcd.h"
#include "hcd_svc.h"
#include "xhci_hw.h"
#include "xhci_dbg.h"

/* --------------------------------------------------------------------- */
/* Waits                                                                  */
/* --------------------------------------------------------------------- */

/* A relative due time in 100 ns units, built without 64-bit arithmetic
 * (AGENTS.md): the magnitude fits 32 bits for any wait this driver makes. */
VOID HcdRelativeMs(PLARGE_INTEGER due, ULONG milliseconds)
{
    ULONG ticks;

    if (milliseconds > 400000UL) {
        milliseconds = 400000UL;
    }
    ticks = milliseconds * 10000UL;
    due->LowPart = 0UL - ticks;
    due->HighPart = (ticks == 0) ? 0 : -1;
}

/* IRQL: PASSIVE_LEVEL. */
VOID HcdSvcWaitMs(PXHCI_EXTENSION ext, ULONG milliseconds)
{
    LARGE_INTEGER due;

    UNREFERENCED_PARAMETER(ext);
    HcdRelativeMs(&due, milliseconds);
    (VOID)KeDelayExecutionThread(KernelMode, FALSE, &due);
}

/* PCI configuration space                                                */
/* --------------------------------------------------------------------- */

static NTSTATUS NTAPI hcdConfigDone(PDEVICE_OBJECT DeviceObject, PIRP Irp,
                                    PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);

    (VOID)KeSetEvent((PKEVENT)Context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

/*
 * IRP_MN_READ_CONFIG / IRP_MN_WRITE_CONFIG, built with IoAllocateIrp and
 * sent to the PCI PDO this FDO was added for - not to the object
 * IoAttachDeviceToDeviceStack returned.
 *
 * That is the route Windows 98 SE's own uhcd.sys 4.10.2222 and NUSB's
 * USBPORT.SYS 5.00.2195.5652 take (static, a subagent's read of 2026-10-03,
 * legal-provenance.md section 4): the config routine of each stores the
 * AddDevice PhysicalDeviceObject argument and sends minor 0x0F / 0x10 to it,
 * WhichSpace 0, IoStatus preset to STATUS_NOT_SUPPORTED, a completion routine
 * that signals an event and returns STATUS_MORE_PROCESSING_REQUIRED, and a
 * read zero-fills its buffer first. Neither queries GUID_BUS_INTERFACE_STANDARD
 * nor imports HalGetBusData. The status alone is judged, as usbport does; no
 * reading here says what Windows 98 leaves in IoStatus.Information.
 *
 * Measured 2026-10-03 (26-A.2, QEMU): the same IRP sent to the attached lower
 * object and judged on Information == length passed on Windows 2000 and was
 * refused on Windows 98 SE, whose bus-master gate then refused the start.
 *
 * IRQL: PASSIVE_LEVEL. Refused above it rather than blocking: the in-place
 * recovery runs at DISPATCH_LEVEL and keeps this service out of its sequence
 * (ext->InitBelowPassive).
 */
MPSTATUS HcdSvcConfigSpace(PXHCI_EXTENSION ext, BOOLEAN read, PVOID buffer,
                           ULONG offset, ULONG length)
{
    PHCD_CONTROLLER hc;
    PIO_STACK_LOCATION next;
    KEVENT done;
    PIRP irp;
    PUCHAR p;
    ULONG i;
    NTSTATUS status;

    if (ext == NULL || buffer == NULL || length == 0 ||
        KeGetCurrentIrql() != PASSIVE_LEVEL) {
        return MP_STATUS_FAILURE;
    }
    hc = HcdControllerFromExt(ext);

    if (read) {
        p = (PUCHAR)buffer;
        for (i = 0; i < length; i++) {
            p[i] = 0;
        }
    }

    irp = IoAllocateIrp(hc->Pdo->StackSize, FALSE);
    if (irp == NULL) {
        return MP_STATUS_NO_RESOURCES;
    }
    irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    irp->IoStatus.Information = 0;

    next = IoGetNextIrpStackLocation(irp);
    next->MajorFunction = IRP_MJ_PNP;
    next->MinorFunction = read ? IRP_MN_READ_CONFIG : IRP_MN_WRITE_CONFIG;
    next->Parameters.ReadWriteConfig.WhichSpace = PCI_WHICHSPACE_CONFIG;
    next->Parameters.ReadWriteConfig.Buffer = buffer;
    next->Parameters.ReadWriteConfig.Offset = offset;
    next->Parameters.ReadWriteConfig.Length = length;

    KeInitializeEvent(&done, NotificationEvent, FALSE);
    HCD_SET_COMPLETION_ALWAYS(irp, hcdConfigDone, &done);
    status = IoCallDriver(hc->Pdo, irp);
    if (status == STATUS_PENDING) {
        (VOID)KeWaitForSingleObject(&done, Executive, KernelMode, FALSE, NULL);
    }
    status = irp->IoStatus.Status;
    hc->ConfigLastInformation = (ULONG)irp->IoStatus.Information;
    IoFreeIrp(irp);

    hc->ConfigLastStatus = (ULONG)status;
    return NT_SUCCESS(status) ? MP_STATUS_SUCCESS : MP_STATUS_FAILURE;
}

/* --------------------------------------------------------------------- */
/* The one-shot timer service                                             */
/* --------------------------------------------------------------------- */

static VOID NTAPI hcdTimerDpc(PKDPC Dpc, PVOID Context, PVOID Arg1,
                              PVOID Arg2)
{
    PHCD_TIMER slot;
    PHCD_CONTROLLER hc;
    KIRQL oldIrql;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);

    slot = (PHCD_TIMER)Context;
    hc = slot->Controller;

    slot->Callback(&hc->Hc, slot->Context);

    /* The count and the idle event change together, under the slot lock:
     * decided outside it, a zero seen here could be published after another
     * processor had armed again and cleared the event (Codex review of
     * 26-A.2, round 1, finding 3). */
    KeAcquireSpinLock(&hc->TimerLock, &oldIrql);
    slot->Busy = 0;
    hc->TimersInFlight--;
    if (hc->TimersInFlight == 0) {
        (VOID)KeSetEvent(&hc->TimersIdle, IO_NO_INCREMENT, FALSE);
    }
    KeReleaseSpinLock(&hc->TimerLock, oldIrql);
}

/*
 * MFINDEX sampled every HCD_FRAME_SAMPLE_MS, well inside its 2,048-frame
 * lap, so GET_CURRENT_FRAME_NUMBER (XhciFrameNumber's masked delta) loses
 * no lap while the thread is busy in a long command or control wait: the
 * health poll alone samples only between the thread's passes (Codex review
 * of batch (c), round 12, finding 2). Its own timer, re-armed by its DPC
 * under TimerLock until the service closes (round 13, finding 2).
 * XhciFrameSample reads nothing from a controller not running. IRQL:
 * DISPATCH_LEVEL.
 */
#define HCD_FRAME_SAMPLE_MS 500UL

static VOID hcdFrameDpc(PKDPC dpc, PVOID context, PVOID arg1, PVOID arg2)
{
    PHCD_CONTROLLER hc;
    LARGE_INTEGER due;
    KIRQL oldIrql;

    UNREFERENCED_PARAMETER(dpc);
    UNREFERENCED_PARAMETER(arg1);
    UNREFERENCED_PARAMETER(arg2);
    hc = (PHCD_CONTROLLER)context;
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    XhciFrameSample(&hc->Hc);
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    KeAcquireSpinLock(&hc->TimerLock, &oldIrql);
    if (hc->TimersClosed) {
        /* The last touch of the controller: the drain waits for it. */
        hc->FrameArmed = 0;
    } else {
        HcdRelativeMs(&due, HCD_FRAME_SAMPLE_MS);
        (VOID)KeSetTimer(&hc->FrameTimer, due, &hc->FrameDpc);
    }
    KeReleaseSpinLock(&hc->TimerLock, oldIrql);
}

/* After the controller has started; once per start. IRQL: PASSIVE_LEVEL. */
VOID HcdFrameTimerStart(PHCD_CONTROLLER hc)
{
    LARGE_INTEGER due;
    KIRQL oldIrql;

    KeAcquireSpinLock(&hc->TimerLock, &oldIrql);
    if (!hc->TimersClosed && !hc->FrameArmed) {
        hc->FrameArmed = 1;
        HcdRelativeMs(&due, HCD_FRAME_SAMPLE_MS);
        (VOID)KeSetTimer(&hc->FrameTimer, due, &hc->FrameDpc);
    }
    KeReleaseSpinLock(&hc->TimerLock, oldIrql);
}

/*
 * The tolerance clock's tick (35-T, design record 17 section 4.0): the
 * count advances and the timer is armed again, relative, so a change of the
 * system time does not move it. A tick may come up to one clock period
 * early, which is why xhci_tol.h credits it at 45 ms. Only this DPC writes
 * the count; readers take one aligned ULONG. IRQL: DISPATCH_LEVEL.
 */
static VOID hcdTolDpc(PKDPC dpc, PVOID context, PVOID arg1, PVOID arg2)
{
    PHCD_CONTROLLER hc;
    LARGE_INTEGER due;
    KIRQL oldIrql;

    UNREFERENCED_PARAMETER(dpc);
    UNREFERENCED_PARAMETER(arg1);
    UNREFERENCED_PARAMETER(arg2);
    hc = (PHCD_CONTROLLER)context;
    KeAcquireSpinLock(&hc->TimerLock, &oldIrql);
    if (hc->TimersClosed) {
        hc->TolArmed = 0;
    } else {
        hc->Hc.Tol.Clock++;
        HcdRelativeMs(&due, XHCI_TOL_TICK_MS);
        (VOID)KeSetTimer(&hc->TolTimer, due, &hc->TolDpc);
    }
    KeReleaseSpinLock(&hc->TimerLock, oldIrql);
}

/* After the controller has started; once per start, the count already set
 * to 0 by XhciTolStart. IRQL: PASSIVE_LEVEL. */
VOID HcdTolClockStart(PHCD_CONTROLLER hc)
{
    LARGE_INTEGER due;
    KIRQL oldIrql;

    KeAcquireSpinLock(&hc->TimerLock, &oldIrql);
    if (!hc->TimersClosed && !hc->TolArmed) {
        hc->TolArmed = 1;
        HcdRelativeMs(&due, XHCI_TOL_TICK_MS);
        (VOID)KeSetTimer(&hc->TolTimer, due, &hc->TolDpc);
    }
    KeReleaseSpinLock(&hc->TimerLock, oldIrql);
}

/* The tolerance clock now, in ticks. IRQL: any. */
ULONG HcdTolNow(PHCD_CONTROLLER hc)
{
    return *(volatile ULONG *)&hc->Hc.Tol.Clock;
}

/* IRQL: PASSIVE_LEVEL (start). */
VOID HcdTimersInit(PHCD_CONTROLLER hc)
{
    ULONG i;

    KeInitializeSpinLock(&hc->TimerLock);
    KeInitializeEvent(&hc->TimersIdle, NotificationEvent, TRUE);
    hc->TimersInFlight = 0;
    hc->TimersClosed = 0;
    hc->FrameArmed = 0;
    KeInitializeTimer(&hc->FrameTimer);
    KeInitializeDpc(&hc->FrameDpc, hcdFrameDpc, hc);
    hc->TolArmed = 0;
    KeInitializeTimer(&hc->TolTimer);
    KeInitializeDpc(&hc->TolDpc, hcdTolDpc, hc);
    for (i = 0; i < HCD_TIMER_SLOTS; i++) {
        hc->Timers[i].Controller = hc;
        hc->Timers[i].Busy = 0;
        KeInitializeTimer(&hc->Timers[i].Timer);
        KeInitializeDpc(&hc->Timers[i].Dpc, hcdTimerDpc, &hc->Timers[i]);
    }
}

/*
 * Is an incoming context older than a pending one of the same callback? The
 * only caller arms with an XHCI_COMMAND_TIMEOUT, whose (Epoch, Generation,
 * Phase, Attempt) orders its arms: a command timeout's abort-arm, made after
 * the controller lock is dropped, can land after the next command's own
 * watchdog, and superseding that would leave the newer command unwatched
 * (Codex review round 23, finding 1). Epoch and generation wrap, so they are
 * ordered by signed difference. An older arm is always a stale one: its
 * callback could only count itself stale. IRQL: DISPATCH_LEVEL, TimerLock
 * held.
 */
static ULONG hcdArmIsOlder(const HCD_TIMER *pending, PVOID context,
                           ULONG contextLength)
{
    XHCI_COMMAND_TIMEOUT incoming;
    XHCI_COMMAND_TIMEOUT held;
    PUCHAR in;
    PUCHAR out;
    ULONG i;

    if (contextLength != sizeof(XHCI_COMMAND_TIMEOUT) ||
        pending->ContextLength != sizeof(XHCI_COMMAND_TIMEOUT)) {
        return 0;
    }
    in = (PUCHAR)context;
    out = (PUCHAR)&incoming;
    for (i = 0; i < sizeof(XHCI_COMMAND_TIMEOUT); i++) {
        out[i] = in[i];
    }
    out = (PUCHAR)&held;
    for (i = 0; i < sizeof(XHCI_COMMAND_TIMEOUT); i++) {
        out[i] = pending->Context[i];
    }
    if (incoming.Epoch != held.Epoch) {
        return ((LONG)(incoming.Epoch - held.Epoch) < 0) ? 1UL : 0UL;
    }
    if (incoming.Generation != held.Generation) {
        return ((LONG)(incoming.Generation - held.Generation) < 0) ? 1UL : 0UL;
    }
    if (incoming.Phase != held.Phase) {
        return (incoming.Phase < held.Phase) ? 1UL : 0UL;
    }
    return (incoming.Attempt < held.Attempt) ? 1UL : 0UL;
}

/*
 * Returns 0 when armed, and also when the arm is older than one pending:
 * that is not a refusal, since nothing is owed to it. IRQL: <=
 * DISPATCH_LEVEL, no lock of the caller's taken; the slot lock is innermost.
 */
ULONG HcdSvcArmTimer(PXHCI_EXTENSION ext, ULONG milliseconds, PVOID context,
                     ULONG contextLength, XHCI_ASYNC_TIMER_CALLBACK *callback)
{
    PHCD_CONTROLLER hc;
    PHCD_TIMER slot;
    LARGE_INTEGER due;
    KIRQL oldIrql;
    ULONG reused;
    ULONG stale;
    ULONG i;

    if (ext == NULL || callback == NULL ||
        contextLength > HCD_TIMER_CONTEXT_BYTES) {
        return 1;
    }
    hc = HcdControllerFromExt(ext);

    slot = NULL;
    reused = 0;
    stale = 0;
    KeAcquireSpinLock(&hc->TimerLock, &oldIrql);
    if (!hc->TimersClosed) {
        /*
         * Ordered against every busy slot of this callback, fired or not,
         * before any is cancelled, and under the lock the replacement takes:
         * a check followed by an unlocked arm would keep the race.
         */
        for (i = 0; i < HCD_TIMER_SLOTS && !stale; i++) {
            if (hc->Timers[i].Busy && hc->Timers[i].Callback == callback &&
                hcdArmIsOlder(&hc->Timers[i], context, contextLength)) {
                stale = 1;
            }
        }
        /*
         * A pending arm of the same callback is superseded rather than left
         * to hold its slot until it fires: the command watchdog, the only
         * user, arms per command and its earlier context is stale once the
         * next command is armed (xhci_cmd.c, one command outstanding), so
         * with a 5 s timeout any burst of more than four commands ran
         * unwatched (c16: 21-29 refusals a session). A slot whose timer has
         * already fired keeps it until its DPC frees it.
         */
        for (i = 0; i < HCD_TIMER_SLOTS && slot == NULL && !stale; i++) {
            if (hc->Timers[i].Busy && hc->Timers[i].Callback == callback &&
                KeCancelTimer(&hc->Timers[i].Timer)) {
                slot = &hc->Timers[i];
                reused = 1;
            }
        }
        for (i = 0; i < HCD_TIMER_SLOTS && slot == NULL && !stale; i++) {
            if (!hc->Timers[i].Busy) {
                slot = &hc->Timers[i];
                slot->Busy = 1;
            }
        }
    }
    if (stale) {
        hc->TimerArmsStale++;
    } else if (slot != NULL && reused) {
        hc->TimerArmsSuperseded++;
    } else if (slot != NULL) {
        hc->TimersInFlight++;
        if (hc->TimersInFlight == 1) {
            KeClearEvent(&hc->TimersIdle);
        }
    } else {
        hc->TimerArmsRefused++;
    }
    if (slot != NULL) {
        /* Under the lock: the next arm's ordering reads this context. */
        slot->Callback = callback;
        slot->ContextLength = contextLength;
        for (i = 0; i < HCD_TIMER_CONTEXT_BYTES; i++) {
            slot->Context[i] = (i < contextLength) ? ((PUCHAR)context)[i] : 0;
        }
        HcdRelativeMs(&due, milliseconds);
        (VOID)KeSetTimer(&slot->Timer, due, &slot->Dpc);
    }
    KeReleaseSpinLock(&hc->TimerLock, oldIrql);
    return (slot != NULL || stale) ? 0UL : 1UL;
}

/*
 * Close the service and wait out every armed callback: a timer that has not
 * fired is cancelled, one whose DPC is queued or running is waited for. After
 * this nothing the service armed can run. IRQL: PASSIVE_LEVEL (stop, remove).
 */
VOID HcdTimersDrain(PHCD_CONTROLLER hc)
{
    KIRQL oldIrql;
    ULONG i;

    LARGE_INTEGER due;

    KeAcquireSpinLock(&hc->TimerLock, &oldIrql);
    hc->TimersClosed = 1;
    if (hc->FrameArmed && KeCancelTimer(&hc->FrameTimer)) {
        hc->FrameArmed = 0;
    }
    if (hc->TolArmed && KeCancelTimer(&hc->TolTimer)) {
        hc->TolArmed = 0;
    }
    KeReleaseSpinLock(&hc->TimerLock, oldIrql);
    /* A sampler or clock DPC queued or running clears its flag under
     * TimerLock, closed. */
    while (hc->FrameArmed || hc->TolArmed) {
        HcdRelativeMs(&due, 1);
        (VOID)KeDelayExecutionThread(KernelMode, FALSE, &due);
    }

    for (i = 0; i < HCD_TIMER_SLOTS; i++) {
        KeAcquireSpinLock(&hc->TimerLock, &oldIrql);
        if (hc->Timers[i].Busy && KeCancelTimer(&hc->Timers[i].Timer)) {
            hc->Timers[i].Busy = 0;
            hc->TimersInFlight--;
            if (hc->TimersInFlight == 0) {
                (VOID)KeSetEvent(&hc->TimersIdle, IO_NO_INCREMENT, FALSE);
            }
        }
        KeReleaseSpinLock(&hc->TimerLock, oldIrql);
    }
    (VOID)KeWaitForSingleObject(&hc->TimersIdle, Executive, KernelMode, FALSE,
                                NULL);
    /* The last callback sets the event while it still holds TimerLock, which
     * lives in this object: taking the lock once more waits until it has let
     * go, so nothing of the controller's is touched after this returns (Codex
     * review of 26-A.2, round 2, finding 2). */
    KeAcquireSpinLock(&hc->TimerLock, &oldIrql);
    KeReleaseSpinLock(&hc->TimerLock, oldIrql);
}

/* IRQL: PASSIVE_LEVEL (start, after a drain). */
VOID HcdTimersOpen(PHCD_CONTROLLER hc)
{
    KIRQL oldIrql;

    KeAcquireSpinLock(&hc->TimerLock, &oldIrql);
    hc->TimersClosed = 0;
    KeReleaseSpinLock(&hc->TimerLock, oldIrql);
}

/* --------------------------------------------------------------------- */
/* Reset request and the fail-closed DMA verdict                          */
/* --------------------------------------------------------------------- */

/*
 * What the miniport's ResetController callback did when usbport delivered
 * the request: mask the interrupt enables, mark the controller failed and
 * raise the recovery request - and then, since the HCD has no usbport to
 * route the request through, wake the controller thread, which performs the
 * recovery (hcd_ctl.c).
 *
 * IRQL: <= DISPATCH_LEVEL, controller lock released.
 */
VOID HcdSvcRequestReset(PXHCI_EXTENSION ext)
{
    PHCD_CONTROLLER hc;
    KIRQL oldIrql;

    if (ext == NULL) {
        return;
    }
    hc = HcdControllerFromExt(ext);
    ext->ResetControllerCalls++;
    XHCI_DBG_VALUE("hcd: controller reset requested, calls",
                   ext->ResetControllerCalls);
    XhciControllerLockAcquire(ext, &oldIrql);
    if (!ext->ControllerFailed) {
        if ((ext->Flags & XHCI_EXT_FLAG_INITIALIZED) != 0) {
            XhciMaskInterrupts(ext);
        }
        ext->ControllerFailed = 1;
        XhciLogNoteLocked(ext, "ctrl.failed.here", ext->ResetControllerCalls);
        ext->RecoveryRequested = 1;
    }
    XhciControllerLockRelease(ext, oldIrql);
    HcdThreadWake(hc);
}

/* IRQL: any. */
VOID HcdSvcDmaNotStopped(PXHCI_EXTENSION ext)
{
    PHCD_CONTROLLER hc;

    if (ext == NULL) {
        return;
    }
    hc = HcdControllerFromExt(ext);
    hc->CommonBufferPinned = 1;
}

/* --------------------------------------------------------------------- */
/* The controller lock                                                    */
/* --------------------------------------------------------------------- */

/* IRQL: any. The word is HcdControllerInitObjects', from AddDevice. */
PKSPIN_LOCK HcdSvcControllerLock(PXHCI_EXTENSION ext)
{
    return &HcdControllerFromExt(ext)->ControllerLock;
}
