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
 * The device matrix's counters (xhci_counters.h) are published here too:
 * zeroed and identified at every start (HcdCountersStart), and refreshed
 * and traced from the thread's poll (HcdCountersPoll). The matrix reads the
 * block itself through the QEMU monitor; the trace sites are what
 * scripts\vm-matrix\gen-offsets.ps1 derives its table from, so every field
 * has exactly one, its label a single literal.
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
#include "xhci_dbg.h"

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
    XhciLogAppend(log, "slots.enabled", hc->Counters.SlotsEnabled, 1);
    XhciLogAppend(log, "devices.addressed", hc->Counters.DevicesAddressed, 1);
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

/* ----------------------------------------------------------------------- */
/* The device matrix's counters                                             */
/* ----------------------------------------------------------------------- */

#ifdef XHCI_DBG_TRACE
/*
 * One site per field, under the label matrix-hcd.psd1 names it by
 * (scripts/vm-matrix/README.md lists them; 48 since Phase 29). Change-gated,
 * so an idle poll
 * prints nothing; the matrix reads the block, not these lines.
 * IRQL: PASSIVE_LEVEL (the controller thread).
 */
static VOID hcdCountersTrace(const XHCIHC_COUNTERS *cnt)
{
    XHCI_DBG_VALUE_CHANGED("devices addressed", cnt->DevicesAddressed);
    XHCI_DBG_VALUE_CHANGED("slots enabled", cnt->SlotsEnabled);
    XHCI_DBG_VALUE_CHANGED("port speed decoded - high speed",
                           cnt->PortSpeedHigh);
    XHCI_DBG_VALUE_CHANGED("port speed decoded - full speed",
                           cnt->PortSpeedFull);
    XHCI_DBG_VALUE_CHANGED("port speed decoded - low speed",
                           cnt->PortSpeedLow);
    XHCI_DBG_VALUE_CHANGED("slot context speed - high speed",
                           cnt->SlotSpeedHigh);
    XHCI_DBG_VALUE_CHANGED("slot context speed - full speed",
                           cnt->SlotSpeedFull);
    XHCI_DBG_VALUE_CHANGED("slot context speed - low speed",
                           cnt->SlotSpeedLow);
    XHCI_DBG_VALUE_CHANGED("slot speed disagreeing with port speed",
                           cnt->SpeedDisagreements);

    XHCI_DBG_VALUE_CHANGED("endpoints opened", cnt->EndpointsOpened);
    XHCI_DBG_VALUE_CHANGED("select endpoints requested",
                           cnt->SelectEndpointsRequested);
    XHCI_DBG_VALUE_CHANGED("select endpoints refused",
                           cnt->SelectEndpointsRefused);
    XHCI_DBG_VALUE_CHANGED("endpoint refusals - type",
                           cnt->EndpointRefusalsType);
    XHCI_DBG_VALUE_CHANGED("endpoint refusals - params",
                           cnt->EndpointRefusalsParams);
    XHCI_DBG_VALUE_CHANGED("endpoint refusals - ring pool",
                           cnt->EndpointRefusalsPool);
    XHCI_DBG_VALUE_CHANGED("endpoint configure failures",
                           cnt->EndpointConfigureFailures);
    XHCI_DBG_VALUE_CHANGED("endpoints refused - no bandwidth",
                           cnt->EndpointsNoBandwidth);
    XHCI_DBG_VALUE_CHANGED("endpoints refused - no resources",
                           cnt->EndpointsNoResources);
    XHCI_DBG_VALUE_CHANGED("URBs refused - malformed", cnt->UrbsMalformed);
    XHCI_DBG_VALUE_CHANGED("selects failed", cnt->SelectsFailed);

    XHCI_DBG_VALUE_CHANGED("fatal controller status", cnt->FatalStatus);
    XHCI_DBG_VALUE_CHANGED("transfer events for no open endpoint",
                           cnt->TransferEventsUnclaimed);
    XHCI_DBG_VALUE_CHANGED("interrupt mask failures",
                           cnt->InterruptMaskFailures);
    XHCI_DBG_VALUE_CHANGED("commands the engine gave up on",
                           cnt->CommandsGivenUp);

    XHCI_DBG_VALUE_CHANGED("transfers submitted", cnt->TransfersSubmitted);
    XHCI_DBG_VALUE_CHANGED("transfers completed", cnt->TransfersCompleted);
    XHCI_DBG_VALUE_CHANGED("transfers cancelled", cnt->TransfersCancelled);
    XHCI_DBG_VALUE_CHANGED("iso packets answered", cnt->IsoPacketsAnswered);
    XHCI_DBG_VALUE_CHANGED("iso missed service errors",
                           cnt->IsoMissedService);
    XHCI_DBG_VALUE_CHANGED("iso packet errors", cnt->IsoPacketErrors);

    XHCI_DBG_VALUE_CHANGED("hubs started by the bus", cnt->HubsStarted);
    XHCI_DBG_VALUE_CHANGED("topology: hub descriptors folded",
                           cnt->TopoDescriptors);
    XHCI_DBG_VALUE_CHANGED("topology: hub descriptors malformed",
                           cnt->TopoDescriptorsBad);
    XHCI_DBG_VALUE_CHANGED("topology: hub slots marked",
                           cnt->TopoHubSlotsMarked);
    XHCI_DBG_VALUE_CHANGED("topology: nodes dropped", cnt->TopoNodesDropped);
    XHCI_DBG_VALUE_CHANGED("topology: behind-hub devices addressed",
                           cnt->TopoBehindHubAddressed);
    XHCI_DBG_VALUE_CHANGED("topology: behind-hub opens",
                           cnt->TopoBehindHubOpens);
    XHCI_DBG_VALUE_CHANGED("topology: behind-hub refused - too deep",
                           cnt->TopoBehindHubTooDeep);
    XHCI_DBG_VALUE_CHANGED("topology: TT pairs programmed",
                           cnt->TopoTtProgrammed);

    XHCI_DBG_VALUE_CHANGED("port speed decoded - superspeed",
                           cnt->PortSpeedSuper);
    XHCI_DBG_VALUE_CHANGED("slot context speed - superspeed",
                           cnt->SlotSpeedSuper);
    XHCI_DBG_VALUE_CHANGED("port rate above gen 1 - superspeedplus",
                           cnt->PortSpeedSuperPlus);
    XHCI_DBG_VALUE_CHANGED("superspeed: warm resets", cnt->SsWarmResets);
    XHCI_DBG_VALUE_CHANGED("superspeed: hot resets converted to warm",
                           cnt->SsResetsConverted);
    XHCI_DBG_VALUE_CHANGED("superspeed: links given up",
                           cnt->SsLinksGivenUp);
    XHCI_DBG_VALUE_CHANGED("superspeed: usb 3 devices on usb 2.0",
                           cnt->SsDevicesOnUsb2);
    XHCI_DBG_VALUE_CHANGED("superspeed: BOS reads failed", cnt->SsBosMissing);
    XHCI_DBG_VALUE_CHANGED("superspeed: endpoints refused - ESIT",
                           cnt->SsEndpointsEsitRefused);

    XHCI_DBG_VALUE_CHANGED("superspeed hubs: started", cnt->SsHubsStarted);
    XHCI_DBG_VALUE_CHANGED("superspeed hubs: halves paired",
                           cnt->SsHubPairs);
    XHCI_DBG_VALUE_CHANGED("superspeed hubs: refused - hub depth",
                           cnt->SsHubDepthRefused);
    XHCI_DBG_VALUE_CHANGED("superspeed hubs: port warm resets",
                           cnt->SsHubWarmResets);
    XHCI_DBG_VALUE_CHANGED("superspeed hubs: port links given up",
                           cnt->SsHubLinksGivenUp);
    XHCI_DBG_VALUE_CHANGED("superspeed hubs: port config errors",
                           cnt->SsHubConfigErrors);
    XHCI_DBG_VALUE_CHANGED("superspeed hubs: superspeedplus devices",
                           cnt->SsHubDevicesPlus);
    XHCI_DBG_VALUE_CHANGED("superspeed hubs: rates without an ID",
                           cnt->SsHubRateUnmatched);
}
#endif

/*
 * The start numbers, drawn for every controller this image serves rather
 * than kept per controller: a controller FDO removed and created again can
 * be given the same allocation, and a per-extension count would then start
 * again at 1 and publish an identity the reader has already seen (Codex
 * review round 23, finding 3). Interlocked, because controllers start
 * independently.
 */
static LONG hcdCountersStarts;

/*
 * A controller start: the block zeroed, so a restart inside a matrix window
 * reads as the negative delta the harness voids it on, and the qemu
 * flavour's identity lines, in the order lib\counters.ps1 reads them. The
 * block does not move on a stop and start, so the start number is what
 * tells a restart from the same start. Before anything that counts.
 * IRQL: PASSIVE_LEVEL (the start).
 */
VOID HcdCountersStart(PHCD_CONTROLLER hc)
{
    PUCHAR p;
    ULONG i;

    p = (PUCHAR)&hc->Counters;
    for (i = 0; i < sizeof(XHCIHC_COUNTERS); i++) {
        p[i] = 0;
    }
    hc->CountersStart = (ULONG)InterlockedIncrement(&hcdCountersStarts);
    if (hc->CountersStart == 0) {
        hc->CountersStart = (ULONG)InterlockedIncrement(&hcdCountersStarts);
    }
    XHCI_DBG_VALUE("counters start", hc->CountersStart);
    XHCI_DBG_VALUE("counters size", sizeof(XHCIHC_COUNTERS));
#ifdef _WIN64
    XHCI_DBG_VALUE("counters VA high",
                   (ULONG)(((ULONG_PTR)&hc->Counters) >> 32));
#endif
    XHCI_DBG_VALUE("counters VA low",
                   (ULONG)((ULONG_PTR)&hc->Counters & 0xFFFFFFFFUL));
}

/*
 * The thread's poll: the two counters the kept controller sequence keeps in
 * XHCI_EXTENSION copied in - both zeroed with it at the same start, so a
 * copy is exact - and, in the qemu flavour, the trace. The health poll that
 * moves FatalStatusDetected runs just before this, on the same thread. The
 * stop calls it once more after the thread has gone, since its own
 * XhciDisableInterrupts can count a mask failure (Codex review round 23,
 * finding 4). IRQL: PASSIVE_LEVEL (the controller thread, or the stop).
 */
VOID HcdCountersPoll(PHCD_CONTROLLER hc)
{
    KIRQL oldIrql;

    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    hc->Counters.FatalStatus = hc->Hc.FatalStatusDetected;
    hc->Counters.InterruptMaskFailures = hc->Hc.InterruptMaskFailures;
    XhciControllerLockRelease(&hc->Hc, oldIrql);
#ifdef XHCI_DBG_TRACE
    hcdCountersTrace(&hc->Counters);
#endif
}
