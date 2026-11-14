/*
 * hcd_msg4.c - XhciInterruptMode 4's lifetime (design record 15 sections
 * 4.5.3 to 4.5.9 and 4.5.16; roadmap-usb4 task 49.24): the refusal table
 * and the image kept, the connection's outcome, the readiness flag's one
 * wrapper and the bar on re-enabling, the start's No Op by interrupt, the
 * release, the D3 and D0 steps, the lost connection and the
 * stranded-event watchdog's judge. The seam is hcd_msgnt.c (msi4nt, 2000
 * to XP x64); this file does not include its header. On 98 SE and ME mode
 * 4 runs as 0 (hcd_ctl.c, hcdSettleDelivery).
 *
 * Every function names its IRQL; the controller lock is
 * XhciControllerLockAcquire's, the image lock HcdMsg4Lock (a
 * synchronization event used as a mutex, PASSIVE_LEVEL only).
 */

#include "hcd.h"
#include "xhci_hw.h"

/* msi4nt's statuses this file names. */
#define HCD_MSG4_OK                 0UL
#define HCD_MSG4_BRAKED             13UL

/* The image's lock and refusal table (section 4.5.4). The table outlives
 * every device object: the image is kept when an entry is made. */
static KEVENT HcdMsg4Lock;
static XHCI_MSG4_REFUSALS HcdMsg4Table;

/* IRQL: PASSIVE_LEVEL, DriverEntry. */
VOID HcdMsg4Init(VOID)
{
    KeInitializeEvent(&HcdMsg4Lock, SynchronizationEvent, TRUE);
    HcdMsg4Table.Count = 0;
    HcdMsg4Table.Latched = 0;
}

/* IRQL: PASSIVE_LEVEL. */
static VOID hcdMsg4LockEnter(VOID)
{
    (VOID)HcdWaitEvent(&HcdMsg4Lock, 0);
}

/* IRQL: <= DISPATCH_LEVEL. */
static VOID hcdMsg4LockLeave(VOID)
{
    (VOID)KeSetEvent(&HcdMsg4Lock, IO_NO_INCREMENT, FALSE);
}

/* IRQL: PASSIVE_LEVEL. */
ULONG HcdMsg4SystemClass(VOID)
{
    if (!IoIsWdmVersionAvailable(1, 0x10)) {
        return XHCI_MSG4_SYS_9X;
    }
    if (IoIsWdmVersionAvailable(6, 0)) {
        return XHCI_MSG4_SYS_NT6;
    }
    return XHCI_MSG4_SYS_NT5;
}

/* A connection, or on NT 5 a claim the query-stop's release left without
 * one. IRQL: any. */
ULONG HcdMsg4Live(PHCD_CONTROLLER hc)
{
    return (hc->Msg4Info != NULL || hc->Msg4ClaimHeld) ? 1UL : 0UL;
}

/* Enters this function in the table and keeps the image (section 4.5.4).
 * IRQL: PASSIVE_LEVEL. */
VOID HcdMsg4EnterRefusal(PHCD_CONTROLLER hc)
{
    ULONG full;

    hcdMsg4LockEnter();
    full = XhciMsg4RefusalAdd(&HcdMsg4Table, hc->Msg4BdfKnown, hc->Msg4Bdf)
               ? 0UL
               : 1UL;
    hc->Counters.Msg4Refused = XhciMsg4RefusalCount(&HcdMsg4Table);
    hcdMsg4LockLeave();
    HcdDmaKeepImage();
    XhciLogNote(&hc->Hc, "msg4.quarantine",
                (full << 31) | (hc->Msg4BdfKnown << 30) | hc->Msg4Bdf);
}

/* IRQL: PASSIVE_LEVEL. */
ULONG HcdMsg4StartRefused(PHCD_CONTROLLER hc)
{
    ULONG empty;
    ULONG known;
    ULONG bdf;
    ULONG matches;
    ULONG refused;

    hcdMsg4LockEnter();
    empty = (HcdMsg4Table.Count == 0 && !HcdMsg4Table.Latched) ? 1UL : 0UL;
    hc->Counters.Msg4Refused = XhciMsg4RefusalCount(&HcdMsg4Table);
    hcdMsg4LockLeave();
    if (empty) {
        return 0;
    }
    bdf = 0;
    matches = 0;
    known = HcdCtlMsg4Location(hc, &bdf, &matches);
    hcdMsg4LockEnter();
    /* A location that cannot be read now could be the refused one. */
    refused = known ? XhciMsg4Refused(&HcdMsg4Table, 1, bdf) : 1UL;
    hcdMsg4LockLeave();
    if (refused) {
        XhciLogNote(&hc->Hc, "msg4.refused", (known << 30) | bdf);
    }
    return refused;
}

/* ------------------------------------------------------------------ */
/* The readiness flag and the bar (sections 4.5.4 and 4.5.5)            */
/* ------------------------------------------------------------------ */

/* The flag's one write, under the ISR's exclusion. 0 when a set could
 * not be made (a clear is always published). IRQL: PASSIVE_LEVEL. */
static ULONG hcdMsg4SetReady(PHCD_CONTROLLER hc, LONG ready)
{
    return HcdMsgNtSync(hc, ready);
}

/* 0 when the flag could not be set: the caller then enables nothing.
 * IRQL: PASSIVE_LEVEL. */
ULONG HcdMsg4Open(PHCD_CONTROLLER hc)
{
    KIRQL oldIrql;

    if (!HcdMsg4Live(hc) || hc->Msg4Lost != HCD_MSG4_LOST_NONE ||
        hc->Msg4Gone) {
        return 0;
    }
    if (!hcdMsg4SetReady(hc, 1)) {
        XhciLogNote(&hc->Hc, "msg4.ready.failed", 1);
        return 0;
    }
    if (hc->Msg4Gone) {
        /* A removal came between the test above and the set: the sync
         * routine left the flag clear. */
        XhciLogNote(&hc->Hc, "msg4.ready.failed", 2);
        return 0;
    }
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    if (hc->Hc.IntrBarred) {
        /* A removal finished its close after the tests above: the bar it
         * published stands. */
        XhciControllerLockRelease(&hc->Hc, oldIrql);
        XhciLogNote(&hc->Hc, "msg4.ready.failed", 3);
        return 0;
    }
    hc->Msg4Barred = 0;
    hc->Hc.Flags |= XHCI_EXT_FLAG_INTERRUPTS;
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    return 1;
}

/* IRQL: PASSIVE_LEVEL. */
VOID HcdMsg4StartReset(PHCD_CONTROLLER hc)
{
    hc->Msg4StrayTripped = 0;
    hc->Msg4LookOwed = 0;
    hc->Msg4LookAt = 0;
    hc->Msg4InjectNow = 0;
    hc->Msg4IdImplemented = 0;
    hc->Msg4Ready = 0;
    hc->Msg4Barred = 0;
    hc->Msg4Lost = HCD_MSG4_LOST_NONE;
    hc->Msg4LostDone = 0;
    hc->Msg4QueryReleased = 0;
    hc->Msg4Halted = 0;
    hc->Msg4NoOpPassed = 0;
    hc->Msg4StrayRun = 0;
    hc->Msg4Removal = 0;
    hc->Msg4Gone = 0;
    hc->Hc.IntrBarred = 0;
    hc->Msg4Judge.Stranded = 0;
    hc->Msg4Judge.Tripped = 0;
    XhciMsg4JudgeReset(&hc->Msg4Judge, 0);
    hc->Msg4JudgeGen++;
}

/* The count and the window start afresh, and a run's deadline is
 * dropped, with the start generation (section 4.5.6). IRQL: <=
 * DISPATCH_LEVEL, the controller lock not held. */
VOID HcdMsg4JudgeRestart(PHCD_CONTROLLER hc)
{
    KIRQL oldIrql;

    if (!HcdMsg4Live(hc)) {
        return;
    }
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    XhciMsg4JudgeReset(&hc->Msg4Judge, hc->Hc.InterruptsClaimed);
    hc->Msg4JudgeGen++;
    hc->Msg4JudgeEvents = hc->Hc.EventsTotal;
    XhciControllerLockRelease(&hc->Hc, oldIrql);
}

/* IRQL: PASSIVE_LEVEL. */
VOID HcdMsg4Close(PHCD_CONTROLLER hc)
{
    KIRQL oldIrql;

    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    hc->Msg4Barred = 1;
    hc->Hc.Flags &= ~XHCI_EXT_FLAG_INTERRUPTS;
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    (VOID)hcdMsg4SetReady(hc, 0);
}

/* ------------------------------------------------------------------ */
/* The connection (sections 4.5.3 and 4.5.7)                           */
/* ------------------------------------------------------------------ */

/* A start refused: the delivery none, the reason and the status kept.
 * IRQL: PASSIVE_LEVEL. */
static NTSTATUS hcdMsg4Refuse(PHCD_CONTROLLER hc, ULONG reason)
{
    hc->PollDelivery = XHCI_POLL_DELIVERY_NONE;
    hc->PollReason = reason;
    hc->Counters.IntrDelivery = hc->PollDelivery;
    hc->Counters.IntrReason = reason;
    hc->MsgConnectStatus = (NTSTATUS)hc->Msg4Status;
    HcdMsiCountersStart(hc);
    XhciLogNote(&hc->Hc, "msg4.status", hc->Msg4Status);
    XhciLogNote(&hc->Hc, "intr.delivery", hc->PollDelivery);
    XhciLogNote(&hc->Hc, "intr.reason", hc->PollReason);
    return STATUS_DEVICE_CONFIGURATION_ERROR;
}

/* IRQL: PASSIVE_LEVEL. */
NTSTATUS HcdMsg4RefuseStart(PHCD_CONTROLLER hc, ULONG reason, ULONG status)
{
    hc->Msg4Status = status;
    return hcdMsg4Refuse(hc, reason);
}

/* IRQL: PASSIVE_LEVEL. */
NTSTATUS HcdMsg4Connect(PHCD_CONTROLLER hc)
{
    PXHCI_EXTENSION ext;
    NTSTATUS status;
    ULONG step;
    ULONG detail;
    ULONG reason;

    ext = &hc->Hc;
#if defined(XHCI_FLAVOUR_QEMU)
    hc->QemuMsg4Fail = 0;
    (VOID)HcdCtlQemuReadDword(hc, L"XhciQemuMsg4Fail", &hc->QemuMsg4Fail);
    XhciLogNote(ext, "qemu.inj.msg4", hc->QemuMsg4Fail);
#endif
    hc->Msg4Info = NULL;
    hc->Msg4SyncObject = NULL;
    hc->Msg4Ready = 0;
    hc->Msg4Barred = 1;
    hc->Msg4Lost = HCD_MSG4_LOST_NONE;
    hc->Msg4LostDone = 0;
    hc->Msg4QueryReleased = 0;
    hc->Msg4Halted = 0;
    hc->Msg4NoOpPassed = 0;
    hc->Msg4StrayRun = 0;
    hc->Msg4Status = 0;
    hc->Msg4BdfKnown = 0;
    hc->Msg4Bdf = 0;
    hc->Msg4Judge.Stranded = 0;
    hc->Msg4Judge.Tripped = 0;
    XhciMsg4JudgeReset(&hc->Msg4Judge, 0);
    hc->Msg4JudgeGen++;

    /* The quiet step (section 4.5.3): the decode, the handoff and the
     * mask before anything is connected. */
    detail = 0;
    step = XhciQuietController(ext, &detail);
    XhciLogNote(ext, "msg4.quiet", (step << 24) | (detail & 0x00FFFFFFUL));
    if (step != 0) {
        hc->Msg4Status = HCD_MSG4_ST_QUIET | (step & 0xFFUL);
        return hcdMsg4Refuse(hc, XHCI_POLL_R_MSG4_REFUSED);
    }

    switch (hc->Msg4System) {
    case XHCI_MSG4_SYS_NT5:
        XhciLogNote(ext, "msg4.system", XHCI_MSG4_SYS_NT5);
        reason = XHCI_POLL_R_MSG4_REFUSED;
        hc->Msg4Status = HcdMsgNtConnect(hc, 0, &reason);
        if (hc->Msg4Status != HCD_MSG4_OK) {
            if (hc->Msg4Info != NULL) {
                XhciLogNote(ext, "msg4.connect.live", hc->Msg4Status);
            }
            status = hcdMsg4Refuse(hc, reason);
            break;
        }
        hc->MsgRoutines = HCD_MSI_ROUTINES_MSI4NT;
        status = STATUS_SUCCESS;
        break;
    default:
        status = hcdMsg4Refuse(hc, XHCI_POLL_R_MSG4_NO_SYSTEM);
        break;
    }
    if (!NT_SUCCESS(status)) {
        return status;
    }
    /* The outcome: the capability read gives the delivery, reason 21
     * kept; the message's route is not the line's (task 36.3 leaves it
     * alone); the core's start waives the interrupt resource. */
    hc->MsgConnected = 1;
    hc->MsgConnectStatus = STATUS_SUCCESS;
    hc->MsgCapability = HcdCtlMsgCapability(hc);
    hc->PollDelivery = hc->MsgCapability;
    hc->IntDelivery = XHCI_PIRQ_DELIVERY_MESSAGE;
    ext->Msg4NoLine = 1;
    hc->Counters.IntrDelivery = hc->PollDelivery;
    hc->Counters.IntrReason = hc->PollReason;
    HcdMsiCountersStart(hc);
    XhciLogNote(ext, "msg4.status", hc->Msg4Status);
    XhciLogNote(ext, "intr.delivery", hc->PollDelivery);
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ */
/* The No Op by interrupt (section 4.5.6)                              */
/* ------------------------------------------------------------------ */

#define HCD_MSG4_NOOP_MS            500UL
#define HCD_MSG4_NOOP_STEP_MS       10UL

/* IRQL: PASSIVE_LEVEL. */
NTSTATUS HcdMsg4NoOpSeen(PHCD_CONTROLLER hc)
{
    PXHCI_EXTENSION ext;
    LARGE_INTEGER due;
    KIRQL oldIrql;
    ULONG waited;
    ULONG witness;
    ULONG claims;

    ext = &hc->Hc;
    if (hc->PollValues.Mode != XHCI_POLL_MODE_MSG4 || !HcdMsg4Live(hc)) {
        return STATUS_SUCCESS;
    }
    for (waited = 0;; waited += HCD_MSG4_NOOP_STEP_MS) {
        witness = *(volatile ULONG *)&ext->NoOpWitnessFired;
        claims = *(volatile ULONG *)&ext->InterruptsClaimed;
        if ((witness != 0 && claims != 0) || waited >= HCD_MSG4_NOOP_MS) {
            break;
        }
        HcdRelativeMs(&due, HCD_MSG4_NOOP_STEP_MS);
        (VOID)KeDelayExecutionThread(KernelMode, FALSE, &due);
    }
    XhciLogNote(ext, "msg4.noop",
                (waited & 0xFFFFUL) | ((witness != 0 ? 1UL : 0UL) << 16) |
                    ((claims > 0xFFUL ? 0xFFUL : claims) << 24));
    if (witness == 0 || claims == 0) {
        return hcdMsg4Refuse(hc, XHCI_POLL_R_MSG4_NO_INTERRUPT);
    }
    XhciControllerLockAcquire(ext, &oldIrql);
    hc->Msg4NoOpPassed = 1;
    XhciMsg4JudgeReset(&hc->Msg4Judge, ext->InterruptsClaimed);
    hc->Msg4JudgeGen++;
    hc->Msg4JudgeEvents = ext->EventsTotal;
    XhciControllerLockRelease(ext, oldIrql);
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ */
/* The release (sections 4.5.4 and 4.5.7)                              */
/* ------------------------------------------------------------------ */

/* Bus Master Enable cleared through the configuration route and read back
 * (a failure noted, not fatal). IRQL: PASSIVE_LEVEL. */
VOID HcdMsg4ClearBusMaster(PHCD_CONTROLLER hc)
{
    USHORT command;

    command = 0xFFFFU;
    if (XhciReadPciConfig(&hc->Hc, XHCI_PCI_COMMAND, &command,
                          sizeof(USHORT)) != MP_STATUS_SUCCESS ||
        command == 0xFFFFU) {
        XhciLogNote(&hc->Hc, "msg4.bme", 0xFFFFFFFFUL);
        return;
    }
    command = (USHORT)(command & ~XHCI_PCI_COMMAND_BME);
    (VOID)XhciWritePciConfig(&hc->Hc, XHCI_PCI_COMMAND, &command,
                             sizeof(USHORT));
    command = 0xFFFFU;
    (VOID)XhciReadPciConfig(&hc->Hc, XHCI_PCI_COMMAND, &command,
                            sizeof(USHORT));
    XhciLogNote(&hc->Hc, "msg4.bme", (ULONG)command);
}

/* Whether the function's configuration space reads all ones (gone).
 * IRQL: PASSIVE_LEVEL. */
static ULONG hcdMsg4Gone(PHCD_CONTROLLER hc)
{
    ULONG id;

    id = 0xFFFFFFFFUL;
    if (XhciReadPciConfig(&hc->Hc, XHCI_PCI_VENDOR_DEVICE, &id,
                          sizeof(ULONG)) != MP_STATUS_SUCCESS) {
        return 0;
    }
    return (id == 0xFFFFFFFFUL) ? 1UL : 0UL;
}

/* IRQL: PASSIVE_LEVEL. */
VOID HcdMsg4Release(PHCD_CONTROLLER hc)
{
    ULONG silent;

    if (!HcdMsg4Live(hc)) {
        return;
    }
    /* Under the power gate (section 4.5.8). The callers hold no gate. */
    HcdPowerGateEnter(hc);
    if (!HcdMsg4Live(hc)) {
        HcdPowerGateLeave(hc);
        return;
    }
    HcdMsg4Close(hc);
    /* Proven silent: the mask's read-back, or the stop's halt. */
    silent = (hc->Hc.InterruptDeliverySuppressed || hc->Msg4Halted) ? 1UL
                                                                    : 0UL;
    if (hc->Msg4Info != NULL || hc->Msg4ClaimHeld) {
        HcdMsgNtRelease(hc, silent);
    }
    hc->MsgConnected = 0;
    HcdPowerGateLeave(hc);
}

/* ------------------------------------------------------------------ */
/* Power (sections 4.5.4 and 4.5.7)                                    */
/* ------------------------------------------------------------------ */

/* IRQL: <= DISPATCH_LEVEL (see hcd.h). */
VOID HcdMsg4Suspend(PHCD_CONTROLLER hc)
{
    KIRQL oldIrql;

    if (!HcdMsg4Live(hc)) {
        return;
    }
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    hc->Msg4Barred = 1;
    hc->Hc.Flags &= ~XHCI_EXT_FLAG_INTERRUPTS;
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    (VOID)hcdMsg4SetReady(hc, 0);
}

/* IRQL: PASSIVE_LEVEL, the power gate held. */
ULONG HcdMsg4Resume(PHCD_CONTROLLER hc)
{
    KIRQL oldIrql;
    ULONG status;

    if (!HcdMsg4Live(hc) || hc->Msg4Lost != HCD_MSG4_LOST_NONE) {
        return 0;
    }
    status = 0xFFFFFFFFUL;
    if (hc->Msg4QueryReleased) {
        return 0;
    }
    if (hc->Msg4Info != NULL) {
        status = HcdMsgNtReArm(hc);
        if (status != HCD_MSG4_OK && status != HCD_MSG4_BRAKED) {
            status = HcdMsgNtReArm(hc);
        }
    }
    XhciLogNote(&hc->Hc, "msg4.rearm", status);
    if (status == HCD_MSG4_BRAKED) {
        /* Nothing written: the brake's case (section 4.5.6). */
        XhciControllerLockAcquire(&hc->Hc, &oldIrql);
        hc->Msg4Barred = 1;
        hc->Hc.Flags &= ~XHCI_EXT_FLAG_INTERRUPTS;
        XhciControllerLockRelease(&hc->Hc, oldIrql);
        return 2;
    }
    if (status != HCD_MSG4_OK) {
        /* The resume still runs (it clears SUSPENDED) and enables nothing;
         * the caller latches the loss after it. */
        XhciControllerLockAcquire(&hc->Hc, &oldIrql);
        hc->Msg4Barred = 1;
        hc->Hc.Flags &= ~XHCI_EXT_FLAG_INTERRUPTS;
        XhciControllerLockRelease(&hc->Hc, oldIrql);
        return 1;
    }
    HcdMsg4JudgeRestart(hc);
    if (!HcdMsg4Open(hc)) {
        XhciControllerLockAcquire(&hc->Hc, &oldIrql);
        hc->Hc.Flags &= ~XHCI_EXT_FLAG_INTERRUPTS;
        XhciControllerLockRelease(&hc->Hc, oldIrql);
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* A connection lost while running (section 4.5.4)                     */
/* ------------------------------------------------------------------ */

/* IRQL: <= DISPATCH_LEVEL, no lock held. */
VOID HcdMsg4LostLatch(PHCD_CONTROLLER hc, LONG cause)
{
    PXHCI_EXTENSION ext;
    KIRQL oldIrql;
    ULONG latched;

    ext = &hc->Hc;
    latched = 0;
    XhciControllerLockAcquire(ext, &oldIrql);
    if (hc->Msg4Lost == HCD_MSG4_LOST_NONE &&
        hc->PollValues.Mode == XHCI_POLL_MODE_MSG4) {
        hc->Msg4Lost = cause;
        hc->Counters.Msg4Lost = (ULONG)cause;
        ext->ControllerFailed = 1;
        ext->RecoveryRequested = 0;
        hc->Msg4Barred = 1;
        ext->Flags &= ~XHCI_EXT_FLAG_INTERRUPTS;
        if ((ext->Flags & XHCI_EXT_FLAG_STARTED) != 0 &&
            (ext->Flags & XHCI_EXT_FLAG_SUSPENDED) == 0 &&
            hc->Common.DevicePower == PowerDeviceD0 && !hc->SuspendedInD0 &&
            hc->BarVa != NULL && ext->ResourceBase != 0) {
            XhciMaskInterrupts(ext);
        }
        XhciLogNoteLocked(ext, "msg4.lost", (ULONG)cause);
        latched = 1;
    }
    XhciControllerLockRelease(ext, oldIrql);
    if (latched) {
        HcdThreadWake(hc);
    }
}

/* IRQL: PASSIVE_LEVEL, the controller thread. */
VOID HcdMsg4LostThread(PHCD_CONTROLLER hc)
{
    if (hc->Msg4Lost == HCD_MSG4_LOST_NONE || hc->Msg4LostDone) {
        return;
    }
    HcdPowerGateEnter(hc);
    if (HcdMsg4Live(hc)) {
        (VOID)hcdMsg4SetReady(hc, 0);
    }
    hc->Msg4LostDone = 1;
    XhciLogNote(&hc->Hc, "msg4.lost.counts",
                (hc->Msg4Judge.Stranded << 16) |
                    (hc->Hc.InterruptDeliverySuppressed ? 1UL : 0UL));
    /* 2000, XP and XP x64: Windows is told the device failed, and the
     * removal it sends runs the release (decision 12). */
    if (hc->Msg4System == XHCI_MSG4_SYS_NT5) {
        HcdMsgNtReportFailed(hc);
    }
    HcdPowerGateLeave(hc);
}

/* IRQL: PASSIVE_LEVEL. */
ULONG HcdMsg4Failed(PHCD_CONTROLLER hc)
{
    return (hc->Msg4Lost != HCD_MSG4_LOST_NONE &&
            hc->Msg4System == XHCI_MSG4_SYS_NT5)
               ? 1UL
               : 0UL;
}

/* Strategy 1's brake, tripped by the routine: latched here, the drain's
 * first act. IRQL: DISPATCH_LEVEL. */
VOID HcdMsg4DpcBrake(PHCD_CONTROLLER hc)
{
    if (hc->Msg4StrayTripped && hc->Msg4Lost == HCD_MSG4_LOST_NONE) {
        XhciLogNote(&hc->Hc, "msg4.brake.line", hc->Msg4StrayRun);
        HcdMsg4LostLatch(hc, HCD_MSG4_LOST_BRAKED);
    }
}

/* The library's brake on a claim (section 4.5.6): looked for once a
 * second by the tolerance clock, and at once after a stranded event; only
 * while the backstop is admitted, the power gate held. IRQL:
 * PASSIVE_LEVEL, the controller thread. */
VOID HcdMsg4Look(PHCD_CONTROLLER hc)
{
    ULONG now;
    ULONG braked;

    if (hc->Msg4Info == NULL ||
        hc->Msg4Claim.Strategy != XHCI_MSG4_STRATEGY_CLAIM ||
        hc->Msg4Lost != HCD_MSG4_LOST_NONE) {
        return;
    }
    now = HcdTolNow(hc);
    if (!hc->Msg4LookOwed && now - hc->Msg4LookAt < 10) {
        return;
    }
    HcdPowerGateEnter(hc);
    braked = 0;
    if (hc->Msg4Info != NULL && hc->ControllerStarted &&
        hc->Common.DevicePower == PowerDeviceD0 && !hc->SuspendedInD0 &&
        (hc->Hc.Flags & XHCI_EXT_FLAG_SUSPENDED) == 0) {
        (VOID)InterlockedExchange(&hc->Msg4LookOwed, 0);
        hc->Msg4LookAt = now;
        braked = HcdMsgNtLook(hc);
    }
    HcdPowerGateLeave(hc);
    if (braked) {
        HcdMsg4LostLatch(hc, HCD_MSG4_LOST_BRAKED);
    }
}

/* ------------------------------------------------------------------ */
/* NT 5: the query-stop and its cancel (section 4.5.4)                  */
/* ------------------------------------------------------------------ */

/* STATUS_SUCCESS to send the query down (released, or nothing of mode 4
 * held), else the status to complete it with. IRQL: PASSIVE_LEVEL, the
 * PnP IRP's thread. */
NTSTATUS HcdMsg4QueryStop(PHCD_CONTROLLER hc)
{
    PXHCI_EXTENSION ext;
    KIRQL oldIrql;
    KIRQL raised;
    ULONG status;

    ext = &hc->Hc;
    if (hc->Msg4System != XHCI_MSG4_SYS_NT5 || hc->Msg4Info == NULL ||
        !hc->ControllerStarted) {
        return STATUS_SUCCESS;
    }
    if (hc->Msg4Lost != HCD_MSG4_LOST_NONE) {
        XhciLogNote(ext, "msg4.querystop", 0xFFFFFFFDUL);
        return STATUS_UNSUCCESSFUL;
    }
    HcdPowerGateEnter(hc);
    XhciControllerLockAcquire(ext, &oldIrql);
    hc->Msg4Barred = 1;
    ext->Flags &= ~XHCI_EXT_FLAG_INTERRUPTS;
    XhciControllerLockRelease(ext, oldIrql);
    KeRaiseIrql(DISPATCH_LEVEL, &raised);
    XhciDisableInterrupts(ext);
    KeLowerIrql(raised);
    if (!ext->InterruptDeliverySuppressed) {
        /* Not proven: nothing disconnected, the message still arrives. */
        XhciControllerLockAcquire(ext, &oldIrql);
        hc->Msg4Barred = 0;
        ext->Flags |= XHCI_EXT_FLAG_INTERRUPTS;
        XhciControllerLockRelease(ext, oldIrql);
        KeRaiseIrql(DISPATCH_LEVEL, &raised);
        XhciEnableInterrupts(ext);
        KeLowerIrql(raised);
        HcdPowerGateLeave(hc);
        XhciLogNote(ext, "msg4.querystop", 0xFFFFFFFFUL);
        return STATUS_UNSUCCESSFUL;
    }
    (VOID)HcdMsgNtSync(hc, 0);
    status = HcdMsgNtDisconnect(hc, 0, 0x4UL);
    XhciLogNote(ext, "msg4.querystop", status);
    if (status == HCD_MSG4_OK) {
        hc->Msg4QueryReleased = 1;
        HcdPowerGateLeave(hc);
        return STATUS_SUCCESS;
    }
    /* Never quarantined here: the connection stays, re-armed. */
    status = HcdMsgNtReArm(hc);
    if (status != HCD_MSG4_OK && status != HCD_MSG4_BRAKED) {
        status = HcdMsgNtReArm(hc);
    }
    if (status == HCD_MSG4_OK && HcdMsg4Open(hc)) {
        KeRaiseIrql(DISPATCH_LEVEL, &raised);
        XhciEnableInterrupts(ext);
        KeLowerIrql(raised);
    } else {
        HcdMsg4LostLatch(hc, status == HCD_MSG4_BRAKED ? HCD_MSG4_LOST_BRAKED
                                                       : HCD_MSG4_LOST_REARM);
    }
    HcdPowerGateLeave(hc);
    return STATUS_UNSUCCESSFUL;
}

/* After a query that released the connection - the cancel, or a lower
 * driver that failed the query: connected again as at start on the claim
 * still held (strategy 1 with the veto again), then the flag, the bar
 * lifted and the interrupter on. The cancel's own No Op test is not made
 * here; the watchdog judges the reconnect (section 4.5.16). A refusal is
 * a connection lost. IRQL: PASSIVE_LEVEL, the PnP IRP's thread. */
VOID HcdMsg4CancelStop(PHCD_CONTROLLER hc)
{
    KIRQL raised;
    ULONG status;
    ULONG reason;

    if (!hc->Msg4QueryReleased) {
        return;
    }
    HcdPowerGateEnter(hc);
    hc->Msg4QueryReleased = 0;
    status = HcdMsgNtConnect(hc, 1, &reason);
    XhciLogNote(&hc->Hc, "msg4.cancelstop", status);
    if (status == HCD_MSG4_OK && HcdMsg4Open(hc)) {
        HcdMsg4JudgeRestart(hc);
        if (hc->ControllerStarted) {
            KeRaiseIrql(DISPATCH_LEVEL, &raised);
            XhciEnableInterrupts(&hc->Hc);
            KeLowerIrql(raised);
        }
        XhciLogNote(&hc->Hc, "msg4.noop", 0xFFFFFFFEUL);
    } else {
        HcdMsg4LostLatch(hc, HCD_MSG4_LOST_RECONNECT);
    }
    HcdPowerGateLeave(hc);
}

/* ------------------------------------------------------------------ */
/* The watchdog's judge (section 4.5.6)                                */
/* ------------------------------------------------------------------ */

/* IRQL: DISPATCH_LEVEL, the controller lock held. */
ULONG HcdMsg4WatchedLocked(PHCD_CONTROLLER hc)
{
    return (HcdMsg4Live(hc) && hc->Msg4Ready && hc->Msg4NoOpPassed &&
            !hc->Msg4QueryReleased && hc->Msg4Lost == HCD_MSG4_LOST_NONE)
               ? 1UL
               : 0UL;
}

/* Whether the backstop keeps draining whatever XhciTolerance says: a
 * watched lifetime, and a query-stop's released interval, in which the
 * controller has no interrupt and the backstop is its only service
 * (section 4.5.4, "The query-stop"); only a watched lifetime charges the
 * judge. IRQL: DISPATCH_LEVEL, the controller lock held. */
ULONG HcdMsg4ServedLocked(PHCD_CONTROLLER hc)
{
    if (HcdMsg4WatchedLocked(hc)) {
        return 1;
    }
    return (hc->PollValues.Mode == XHCI_POLL_MODE_MSG4 &&
            hc->Msg4QueryReleased && hc->Msg4Lost == HCD_MSG4_LOST_NONE)
               ? 1UL
               : 0UL;
}

/* A removal Windows reported, surprise or direct (section 4.5.4): the
 * barrier published, then the flag cleared under the ISR's exclusion; at
 * a surprise removal no register is touched, at a remove the causes are
 * masked first where the controller is proven there (section 4.5.17). The
 * sync routine sets the flag only while the
 * barrier is clear, under the same exclusion, so an open racing this one
 * (a D0 worker that re-armed) either ran first, and the clear follows it,
 * or leaves the flag clear; no gate is needed. IRQL: PASSIVE_LEVEL, a PnP
 * IRP's thread. */
VOID HcdMsg4Gone(PHCD_CONTROLLER hc, ULONG surprise)
{
    KIRQL oldIrql;
    ULONG present;

    if (hc->PollValues.Mode != XHCI_POLL_MODE_MSG4) {
        return;
    }
    /* Record 15 section 4.5.17, item 2: at a remove (not a surprise
     * removal) of a controller still started, the causes masked before
     * the flag is cleared, where a configuration read proves it there;
     * whether it runs is read under the lock, in the bar's hold, from the
     * suspend's own flag - set by XhciSuspendController before the function
     * leaves D0 and cleared by XhciResumeController once it is back, before
     * anything enables - not from the published power state, which a
     * resume publishes only after it has enabled. */
    present = (!surprise && HcdMsg4Live(hc) && !hcdMsg4Gone(hc))
                  ? 1UL
                  : 0UL;
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    if (present && (hc->Hc.Flags & XHCI_EXT_FLAG_STARTED) != 0 &&
        (hc->Hc.Flags & XHCI_EXT_FLAG_SUSPENDED) == 0 &&
        hc->BarVa != NULL && hc->Hc.ResourceBase != 0) {
        XhciMaskInterrupts(&hc->Hc);
    }
    /* The bar in the same hold as the mask: an open that passed its tests
     * finds it there before it lifts Msg4Barred, and an enable (a D0
     * worker's, a stranded event's re-arm) either came before the mask or
     * meets the bar and unmasks nothing (XhciEnableInterrupts). */
    hc->Hc.IntrBarred = 1;
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    hc->Msg4Gone = 1;
    if (HcdMsg4Live(hc)) {
        HcdMsg4Close(hc);
    }
}

/* IRQL: DISPATCH_LEVEL, the controller lock held. */
VOID HcdMsg4StrandedLocked(PHCD_CONTROLLER hc, PULONG lost)
{
    *lost = (XhciMsg4JudgeStranded(&hc->Msg4Judge, HcdTolNow(hc)) ==
             XHCI_MSG4_JUDGE_TRIP)
                ? 1UL
                : 0UL;
    hc->Counters.Msg4Stranded = hc->Msg4Judge.Stranded;
    hc->Msg4LookOwed = 1;
    XhciLogNoteLocked(&hc->Hc, "msg4.stranded", hc->Msg4Judge.Stranded);
}

/* IRQL: DISPATCH_LEVEL, the controller lock held. */
VOID HcdMsg4DrainLocked(PHCD_CONTROLLER hc, PULONG lost)
{
    ULONG events;

    *lost = 0;
    events = hc->Hc.EventsTotal - hc->Msg4JudgeEvents;
    hc->Msg4JudgeEvents = hc->Hc.EventsTotal;
    if (!HcdMsg4WatchedLocked(hc)) {
        return;
    }
    XhciMsg4JudgeDrain(&hc->Msg4Judge, events, hc->Hc.InterruptsClaimed,
                       HcdTolNow(hc));
}

/* IRQL: DISPATCH_LEVEL, the controller lock held. */
VOID HcdMsg4TickLocked(PHCD_CONTROLLER hc, PULONG lost)
{
    *lost = 0;
    if (!HcdMsg4WatchedLocked(hc)) {
        return;
    }
    if (XhciMsg4JudgeTick(&hc->Msg4Judge, hc->Hc.InterruptsClaimed,
                          HcdTolNow(hc)) == XHCI_MSG4_JUDGE_TRIP) {
        *lost = 1;
        XhciLogNoteLocked(&hc->Hc, "msg4.stranded.run", hc->Msg4Judge.Run);
    }
}
