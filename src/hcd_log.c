/*
 * hcd_log.c - the log ring's one sink under the HCD: the DebugView emit,
 * from the controller thread, continuously (roadmap-hcd.md task 26-A.8;
 * design record 13 sections 5.4 and 5.5; design record 08).
 *
 * The ring, its accounting and the flush decision are the pure core's
 * (src/xhci_log.c). What lives here is the hand-over: measure the IRQL, decide
 * and drain under the controller lock, emit outside it - the order the
 * miniport's xhciLogFlush kept on branch 1.2.0.0, unchanged. What is new is
 * when it runs. The miniport could flush only at StopController, the one
 * PASSIVE_LEVEL callback a Windows 98 machine reached before shutdown; the
 * HCD owns a PASSIVE context, its controller thread, so the ring is emitted
 * every time the thread wakes and holds something, and once more with the
 * counter block at every stop.
 *
 * **This file holds the only DbgPrint outside the qemu flavour's live trace**
 * (AGENTS.md, "Coding Style"; the allowlist's DbgPrint row restricts the
 * import to this object and xhci_dbg.obj). Per-line DbgPrint from a DPC or an
 * ISR at real interrupt rates bugchecks Windows 98 on bare metal on three
 * device classes (design record 08); owning the driver object does not change
 * that, so producers record from any IRQL and this file emits from PASSIVE
 * only, and only when XhciLogDebugView asks for it.
 *
 * IRQL: each function carries its own.
 */

#include "hcd.h"
#include "xhci_hw.h"

/* A stack buffer below a page: MSVC emits a __chkstk probe for a page or
 * more, and the Windows 2000 DDK's libraries do not provide one. */
#define HCD_LOG_CHUNK 256

/*
 * The DebugView sink. "%s" rather than the bytes as the format, because the
 * ring holds fields a device produced and a stray '%' would read the stack.
 * A chunk boundary may split a record across two calls; the capture
 * concatenates. IRQL: PASSIVE_LEVEL, measured by the caller.
 */
static VOID hcdLogEmit(const UCHAR *bytes, ULONG count)
{
    UCHAR line[HCD_LOG_CHUNK + 1];
    ULONG i;

    for (i = 0; i < count && i < HCD_LOG_CHUNK; i++) {
        line[i] = bytes[i];
    }
    line[i] = 0;
    DbgPrint("%s", (const char *)line);
}

/*
 * The counter block, appended under the lock at a stop's flush: the set the
 * miniport published (branch 1.2.0.0, xhciLogCountersLocked), less what the
 * HCD has no field for, plus the HCD's own transfer-path and pool counts.
 * Controller lock held. IRQL: DISPATCH_LEVEL.
 */
static VOID hcdLogCountersLocked(PHCD_CONTROLLER hc)
{
    PXHCI_EXTENSION ext;
    PXHCI_LOG log;

    ext = &hc->Hc;
    log = &ext->Log;
    XhciLogAppend(log, "ext.flags", ext->Flags, 1);
    XhciLogAppend(log, "init.step", ext->InitStep, 1);
    XhciLogAppend(log, "init.status", ext->InitStatus, 1);
    XhciLogAppend(log, "ctrl.failed", ext->ControllerFailed, 1);
    XhciLogAppend(log, "ctrl.reset.calls", ext->ResetControllerCalls, 1);
    XhciLogAppend(log, "ctrl.recover.attempts", ext->RecoveryAttempts, 1);
    XhciLogAppend(log, "ctrl.recover.done", ext->RecoveryCompletions, 1);
    XhciLogAppend(log, "isr.entries", ext->InterruptCount, 1);
    XhciLogAppend(log, "isr.claimed", ext->InterruptsClaimed, 1);
    XhciLogAppend(log, "dpc.count", ext->DpcCount, 1);
    XhciLogAppend(log, "imod.interval", ext->ImodInterval, 1);
    XhciLogAppend(log, "imod.readback", ext->ImodReadback, 1);
    XhciLogAppend(log, "events.total", ext->EventsTotal, 1);
    XhciLogAppend(log, "slots.enabled", ext->SlotsEnabled, 1);
    XhciLogAppend(log, "devices.addressed", ext->DevicesAddressed, 1);
    XhciLogAppend(log, "cmd.issued", ext->CommandsIssued, 1);
    XhciLogAppend(log, "cmd.timedout", ext->CommandsTimedOut, 1);
    XhciLogAppend(log, "cmd.abandoned", ext->CommandsAbandoned, 1);
    XhciLogAppend(log, "quiesce.failures", ext->QuiesceFailures, 1);
    XhciLogAppend(log, "restore.stale.dropped", ext->RestoreEventsDiscarded,
                  1);
    XhciLogAppend(log, "restore.fatal", ext->RestoreEventsFatal, 1);
    XhciLogAppend(log, "restore.lastfatal.kind", ext->RestoreFatalKind, 1);
    XhciLogAppend(log, "restore.lastfatal.code", ext->RestoreFatalCode, 1);
    XhciLogAppend(log, "power.suspends", ext->SuspendCount, 1);
    XhciLogAppend(log, "power.resumes", ext->ResumeReinits, 1);
    XhciLogAppend(log, "urb.completed", hc->UrbsCompleted, 1);
    XhciLogAppend(log, "urb.gone", hc->UrbsGone, 1);
    XhciLogAppend(log, "urb.unknown", hc->UrbUnknown, 1);
    XhciLogAppend(log, "ioctl.unknown", hc->IoctlUnknown, 1);
    XhciLogAppend(log, "enum.cmd.timedout", hc->EnumCommandsTimedOut, 1);
    XhciLogAppend(log, "enum.xfer.timedout", hc->EnumTransfersTimedOut, 1);
    XhciLogAppend(log, "door.requests", hc->DoorRequests, 1);
    XhciLogAppend(log, "pool.outstanding", HcdPoolOutstandingCount(), 1);
    XhciLogAppend(log, "log.dropped", log->BytesDropped, 1);
    XhciLogAppend(log, "log.errors.overbudget", ext->LogErrorsOverBudget, 1);
    XhciLogAppend(log, "log.appends", log->Appends, 1);
    XhciLogAppend(log, "log.suppressed", log->Suppressed, 1);
    XhciLogAppend(log, "log.emits", log->DebugViewEmits, 1);
}

/*
 * Hand the ring to the DebugView sink: from the controller thread on every
 * wake (XHCI_LOG_REASON_PERIODIC, no counter block), and from the stop with
 * the counter block. Costs one load and one branch with the sink off. The
 * ring ends empty whatever the sink does, so a bounded ring never stays full.
 *
 * The extension is the one the current start built: the thread runs only
 * between a start and its stop, and the stop's own flush runs before the
 * next start zeroes it. IRQL: PASSIVE_LEVEL expected; at any other level it
 * counts the refusal and emits nothing.
 */
VOID HcdLogFlush(PHCD_CONTROLLER hc, ULONG reason, ULONG counters)
{
    UCHAR staging[HCD_LOG_CHUNK];
    PXHCI_EXTENSION ext;
    KIRQL oldIrql;
    ULONG decision;
    ULONG chunk;
    ULONG emitted;

    ext = &hc->Hc;
    if (ext->Signature != XHCI_EXTENSION_SIGNATURE ||
        !ext->Log.DebugViewEnabled) {
        return;
    }

    if (KeGetCurrentIrql() != PASSIVE_LEVEL) {
        XhciControllerLockAcquire(ext, &oldIrql);
        XhciLogFlushRefusedIrql(&ext->Log);
        XhciControllerLockRelease(ext, oldIrql);
        return;
    }

    /* Publishing is raised for the counter block only, and cleared in the
     * same hold: it is the one bypass of the recording switch. */
    XhciControllerLockAcquire(ext, &oldIrql);
    if (counters) {
        ext->Log.Publishing = 1;
        hcdLogCountersLocked(hc);
    }
    decision = XhciLogFlushBegin(&ext->Log, reason);
    ext->Log.Publishing = 0;
    XhciControllerLockRelease(ext, oldIrql);

    if (decision != XHCI_LOG_FLUSH_GO) {
        return;
    }

    emitted = 0;
    for (;;) {
        XhciControllerLockAcquire(ext, &oldIrql);
        chunk = XhciLogDrain(&ext->Log, staging, sizeof(staging));
        XhciControllerLockRelease(ext, oldIrql);
        if (chunk == 0) {
            break;
        }
        hcdLogEmit(staging, chunk);
        emitted += chunk;
    }

    XhciControllerLockAcquire(ext, &oldIrql);
    if (emitted != 0) {
        ext->Log.DebugViewEmits++;
        ext->Log.DebugViewBytes += emitted;
    }
    XhciLogFlushEnd(&ext->Log, emitted, 1);
    XhciControllerLockRelease(ext, oldIrql);
}
