/*
 * hcd_ctl.c - the controller FDO's start and stop, its interrupt, and its
 * PASSIVE-level thread (roadmap-hcd.md task 26-A.2; design record 13
 * sections 5.4 and 11).
 *
 * The controller sequence itself is the miniport's, kept in xhci_init.c,
 * xhci_cmd.c and xhci_evt.c. What this file replaces is what usbport did
 * around it: translate the PnP resources, map the BAR, connect the interrupt,
 * hand over a common buffer, and call the sequence in usbport's order -
 * StartController then EnableInterrupts at start, DisableInterrupts then
 * StopController at stop - and the 500 ms CheckController poll, which here is
 * the controller thread's.
 *
 * The thread (design record 13 section 5.4, layer 1) is the PASSIVE-level
 * context the HCD owns: a system thread rather than work items, because it
 * waits between steps. In this task it runs the health poll and the in-place
 * recovery; the enumeration machines of 26-A.4 and the log flusher of 26-A.8
 * join it.
 *
 * IRQL: each function carries its own.
 */

#include "hcd.h"
#include "xhci_dbg.h"
#include "hcd_svc.h"
#include "xhci_hw.h"
#include "xhci_usbport.h"

/* The resource mask XhciInitController holds the start to. The miniport
 * settled it from the registration packet's interface version, because NT 6.x
 * usbport named the two resources with different bits; the HCD fills the
 * USBPORT_RESOURCES copy itself, with the NT 5.x bits, on every target. */
ULONG XhciResourcesRequired = USBPORT_RESOURCES_MEMORY |
                              USBPORT_RESOURCES_INTERRUPT;

/* The three device-key values the HCD reads (design record 13 section 5.5).
 * The miniport's three XhciVirtualHSHub* values are not read. */
#define HCD_VALUE_LOG_VERBOSITY L"XhciLogVerbosity"
#define HCD_VALUE_LOG_DEBUGVIEW L"XhciLogDebugView"
#define HCD_VALUE_IMOD          L"XhciImodInterval250ns"

/* The health poll's period. usbport's CheckController was nominally 500 ms
 * and measured at 36-80 ms on the E460 (run-13e, Finding V); the poll's
 * thresholds run on the frame clock, not on this period, which only bounds
 * how late a verdict is acted on. */
#define HCD_POLL_MS 100UL

/* --------------------------------------------------------------------- */
/* Registry                                                               */
/* --------------------------------------------------------------------- */

/*
 * One REG_DWORD from the controller's driver (software) key, the key the
 * miniport's values lived in. Returns the NTSTATUS; *value is untouched on
 * failure. IRQL: PASSIVE_LEVEL.
 */
static NTSTATUS hcdReadDword(PHCD_CONTROLLER hc, PCWSTR name, PULONG value)
{
    RTL_QUERY_REGISTRY_TABLE table[2];
    HANDLE key;
    ULONG found;
    ULONG fallback;
    NTSTATUS status;
    PUCHAR p;
    ULONG i;

    status = IoOpenDeviceRegistryKey(hc->Pdo, PLUGPLAY_REGKEY_DRIVER, KEY_READ,
                                     &key);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    p = (PUCHAR)table;
    for (i = 0; i < sizeof(table); i++) {
        p[i] = 0;
    }
    found = 0;
    fallback = 0;
    table[0].Flags = RTL_QUERY_REGISTRY_DIRECT | RTL_QUERY_REGISTRY_REQUIRED;
    table[0].Name = (PWSTR)name;
    table[0].EntryContext = &found;
    table[0].DefaultType = REG_DWORD;
    table[0].DefaultData = &fallback;
    table[0].DefaultLength = sizeof(ULONG);

    status = RtlQueryRegistryValues(RTL_REGISTRY_HANDLE, (PWSTR)key, table,
                                    NULL, NULL);
    ZwClose(key);
    if (NT_SUCCESS(status)) {
        *value = found;
    }
    return status;
}

/* IRQL: PASSIVE_LEVEL. */
static VOID hcdReadValues(PHCD_CONTROLLER hc)
{
    PXHCI_EXTENSION ext;
    ULONG verbosity;
    ULONG debugView;
    ULONG imod;
    NTSTATUS status;

    ext = &hc->Hc;

    verbosity = 0;
    debugView = 0;
    (VOID)hcdReadDword(hc, HCD_VALUE_LOG_VERBOSITY, &verbosity);
    (VOID)hcdReadDword(hc, HCD_VALUE_LOG_DEBUGVIEW, &debugView);
    (VOID)XhciLogApplySwitches(&ext->Log, verbosity, debugView);
    XhciLogNote(ext, "log.verbosity", ext->Log.Verbosity);
    XhciLogNote(ext, "log.verbosity.read", ext->Log.VerbosityRead);
    XhciLogNote(ext, "log.verbosity.refused", ext->Log.VerbosityRefused);
    XhciLogNote(ext, "log.debugview", ext->Log.DebugViewEnabled);

    imod = 0;
    status = hcdReadDword(hc, HCD_VALUE_IMOD, &imod);
    ext->ImodStatus = NT_SUCCESS(status) ? MP_STATUS_SUCCESS
                                         : MP_STATUS_FAILURE;
    ext->ImodRequested = NT_SUCCESS(status) ? imod : 0;
    XhciLogNote(ext, "imod.status", ext->ImodStatus);
    XhciLogNote(ext, "imod.requested", ext->ImodRequested);
}

/* --------------------------------------------------------------------- */
/* Resources                                                              */
/* --------------------------------------------------------------------- */

/* The first memory window and the first interrupt of the translated list,
 * and the raw memory window's bus address. IRQL: PASSIVE_LEVEL. */
static NTSTATUS hcdParseResources(PHCD_CONTROLLER hc, PIRP irp)
{
    PIO_STACK_LOCATION stack;
    PCM_RESOURCE_LIST translated;
    PCM_RESOURCE_LIST raw;
    PCM_PARTIAL_RESOURCE_LIST list;
    PCM_PARTIAL_RESOURCE_LIST rawList;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR d;
    ULONG i;

    hc->HaveMemory = 0;
    hc->HaveInterrupt = 0;

    stack = IoGetCurrentIrpStackLocation(irp);
    translated = stack->Parameters.StartDevice.AllocatedResourcesTranslated;
    raw = stack->Parameters.StartDevice.AllocatedResources;
    if (translated == NULL || raw == NULL || translated->Count == 0 ||
        raw->Count == 0) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    list = &translated->List[0].PartialResourceList;
    rawList = &raw->List[0].PartialResourceList;

    for (i = 0; i < list->Count; i++) {
        d = &list->PartialDescriptors[i];
        if (d->Type == CmResourceTypeMemory && !hc->HaveMemory) {
            hc->BarTranslated = d->u.Memory.Start;
            hc->BarLength = d->u.Memory.Length;
            hc->HaveMemory = 1;
        } else if (d->Type == CmResourceTypeInterrupt && !hc->HaveInterrupt) {
            hc->IntVector = d->u.Interrupt.Vector;
            hc->IntIrql = (KIRQL)d->u.Interrupt.Level;
            hc->IntAffinity = d->u.Interrupt.Affinity;
            hc->IntMode = (d->Flags & CM_RESOURCE_INTERRUPT_LATCHED) != 0
                              ? Latched
                              : LevelSensitive;
            hc->IntShared = (d->ShareDisposition == CmResourceShareShared)
                                ? TRUE
                                : FALSE;
            hc->HaveInterrupt = 1;
        }
    }
    for (i = 0; i < rawList->Count; i++) {
        d = &rawList->PartialDescriptors[i];
        if (d->Type == CmResourceTypeMemory) {
            hc->BarRaw = d->u.Memory.Start;
            break;
        }
    }

    if (!hc->HaveMemory || !hc->HaveInterrupt) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    return STATUS_SUCCESS;
}

/* --------------------------------------------------------------------- */
/* Interrupt                                                              */
/* --------------------------------------------------------------------- */

/* IRQL: DIRQL. XhciIsr is the miniport's one-shot ISR, unchanged: it claims
 * on USBSTS.EINT, acknowledges and returns, and the DPC drains. */
static BOOLEAN NTAPI hcdIsr(PKINTERRUPT Interrupt, PVOID Context)
{
    PHCD_CONTROLLER hc;

    UNREFERENCED_PARAMETER(Interrupt);

    hc = (PHCD_CONTROLLER)Context;
    if (!XhciIsr(&hc->Hc)) {
        return FALSE;
    }
    /* Counted when queued, not when the DPC starts: a DPC another processor
     * has dequeued but not yet entered is then still in the count the
     * teardown waits on (Codex review of 26-A.2, round 1, finding 2). */
    (VOID)InterlockedIncrement(&hc->DpcsInFlight);
    if (!KeInsertQueueDpc(&hc->IsrDpc, NULL, NULL)) {
        (VOID)InterlockedDecrement(&hc->DpcsInFlight);
    }
    return TRUE;
}

/* IRQL: DISPATCH_LEVEL. The event drain, with the interrupter re-armed as
 * usbport's InterruptDpc(ext, TRUE) asked. A port change wakes the thread,
 * which owns the ports from task 26-A.3. */
static VOID NTAPI hcdIsrDpc(PKDPC Dpc, PVOID Context, PVOID Arg1, PVOID Arg2)
{
    PHCD_CONTROLLER hc;
    ULONG portEvents;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);

    hc = (PHCD_CONTROLLER)Context;
    if (!hc->DpcClosed) {
        /* XhciEventDpc's own return is the Version 300 report route, which
         * the HCD never takes (RootHubReportThroughDpc is 0). */
        portEvents = hc->PortEvents;
        (VOID)XhciEventDpc(&hc->Hc, TRUE);
        if (hc->PortEvents != portEvents) {
            HcdThreadWake(hc);
        }
    }
    (VOID)InterlockedDecrement(&hc->DpcsInFlight);
}

/* --------------------------------------------------------------------- */
/* The controller thread                                                  */
/* --------------------------------------------------------------------- */

/* The objects start and stop reuse, initialised once so a start that fails
 * early can run the same release as a stop. IRQL: PASSIVE_LEVEL (AddDevice). */
VOID HcdControllerInitObjects(PHCD_CONTROLLER hc)
{
    KeInitializeSpinLock(&hc->ControllerLock);
    HcdTimersInit(hc);
    KeInitializeDpc(&hc->IsrDpc, hcdIsrDpc, hc);
    hc->DpcsInFlight = 0;
    hc->DpcClosed = 1;
    KeInitializeEvent(&hc->WorkEvent, SynchronizationEvent, FALSE);
    KeInitializeEvent(&hc->CmdDoneEvent, SynchronizationEvent, FALSE);
}

/* IRQL: <= DISPATCH_LEVEL. */
VOID HcdThreadWake(PHCD_CONTROLLER hc)
{
    (VOID)KeSetEvent(&hc->WorkEvent, IO_NO_INCREMENT, FALSE);
}

/*
 * The in-place recovery, as the miniport's recovery timer callback ran it,
 * but from this thread instead of a usbport timer DPC: the request is taken
 * under the lock, the recovery itself at DISPATCH_LEVEL, because
 * XhciRecoverController's contract is DISPATCH_LEVEL (it sets
 * InitBelowPassive and keeps every wait a stall). IRQL: PASSIVE_LEVEL.
 */
static VOID hcdRecover(PHCD_CONTROLLER hc)
{
    PXHCI_EXTENSION ext;
    KIRQL oldIrql;
    KIRQL raised;
    ULONG go;
    ULONG ok;

    ext = &hc->Hc;
    go = 0;
    XhciControllerLockAcquire(ext, &oldIrql);
    if (ext->RecoveryRequested && ext->ControllerFailed &&
        (ext->Flags & XHCI_EXT_FLAG_SUSPENDED) == 0 &&
        ext->RecoveryFailuresConsecutive < XHCI_RECOVERY_MAX_ATTEMPTS) {
        ext->RecoveryRequested = 0;
        go = 1;
    }
    XhciControllerLockRelease(ext, oldIrql);
    if (!go) {
        return;
    }

    KeRaiseIrql(DISPATCH_LEVEL, &raised);
    ok = XhciRecoverController(ext);
    KeLowerIrql(raised);

    if (!ok) {
        XhciControllerLockAcquire(ext, &oldIrql);
        if (ext->RecoveryFailuresConsecutive < XHCI_RECOVERY_MAX_ATTEMPTS) {
            ext->RecoveryRequested = 1;
        }
        XhciControllerLockRelease(ext, oldIrql);
    }
}

/*
 * Declare the controller failed and ask the thread for the in-place
 * recovery: the HCD's answer to a failed resume, which under usbport ended in
 * the stop/start a failed ResumeController asked usbport for (Codex review of
 * 26-A.2, round 1, finding 7). IRQL: <= DISPATCH_LEVEL, controller lock
 * released.
 */
VOID HcdControllerFail(PHCD_CONTROLLER hc)
{
    PXHCI_EXTENSION ext;
    KIRQL oldIrql;

    ext = &hc->Hc;
    XhciControllerLockAcquire(ext, &oldIrql);
    if (!ext->ControllerFailed) {
        ext->ControllerFailed = 1;
        XhciLogNoteLocked(ext, "ctrl.failed.resume", 1);
    }
    ext->RecoveryRequested = 1;
    XhciControllerLockRelease(ext, oldIrql);
    HcdThreadWake(hc);
}

/* IRQL: PASSIVE_LEVEL. */
static VOID hcdPoll(PHCD_CONTROLLER hc)
{
    PXHCI_EXTENSION ext;
    KIRQL raised;
    ULONG escalate;

    ext = &hc->Hc;
    if ((ext->Flags & XHCI_EXT_FLAG_STARTED) != 0) {
        ext->CheckCallbacks++;

        /* XhciControllerHealthPoll's contract is DISPATCH_LEVEL: usbport
         * called CheckController under its MiniportSpinLock. */
        KeRaiseIrql(DISPATCH_LEVEL, &raised);
        escalate = XhciControllerHealthPoll(ext);
        KeLowerIrql(raised);
        if (escalate) {
            XhciRequestControllerReset(ext);
        }
    }
    /* Outside the STARTED gate: a failed resume (HcdControllerFail) is a
     * recovery request on a controller whose flags no longer say it runs. */
    hcdRecover(hc);
}

/* IRQL: PASSIVE_LEVEL (a system thread). */
static VOID NTAPI hcdThread(PVOID Context)
{
    PHCD_CONTROLLER hc;
    LARGE_INTEGER due;

    hc = (PHCD_CONTROLLER)Context;
    for (;;) {
        HcdRelativeMs(&due, HCD_POLL_MS);
        (VOID)KeWaitForSingleObject(&hc->WorkEvent, Executive, KernelMode,
                                    FALSE, &due);
        if (hc->ThreadStop) {
            break;
        }
        hcdPoll(hc);
    }
    XHCI_DBG_TEXT("hcd: controller thread leaving its loop");
    (VOID)KeSetEvent(&hc->ThreadExited, IO_NO_INCREMENT, FALSE);
    (VOID)PsTerminateSystemThread(STATUS_SUCCESS);
}

/*
 * How the stop waits for the thread differs by target, and each half is the
 * one measured to work there (26-A.2, 2026-10-03, QEMU; runs/run-26.md):
 *
 *   - NT (WDM 1.10 and later; Windows 98 reports 1.00 and ME 1.05,
 *     win98-wdm.md): on the thread's object, referenced from the
 *     handle. A disable on Windows 2000 is QUERY_REMOVE then REMOVE, and the
 *     remove unloads the image; only the thread object's signal says the
 *     thread has left it. With the event wait instead, the trace ran through
 *     the whole teardown and the guest then rebooted, twice - read as the
 *     thread still executing its last instructions here when the image went
 *     (inferred; no dump was taken). With the thread-object wait the same
 *     disable passed two cycles.
 *   - Windows 98 and ME: on that event. A KeWaitForSingleObject on the
 *     thread's object faulted Windows 98 SE ("A fatal exception 0E has
 *     occurred at 0028:C00312EE"), between the traces either side of the
 *     wait. The event leaves the thread's call into PsTerminateSystemThread
 *     after it; a Windows 98 disable is QUERY_STOP and STOP, which unloads
 *     nothing, and the remove that would unload the image has not been
 *     measured against that window on Windows 98 (26-V.1's).
 *
 * IRQL: PASSIVE_LEVEL.
 */
static NTSTATUS hcdThreadStart(PHCD_CONTROLLER hc)
{
    HANDLE handle;
    NTSTATUS status;

    hc->ThreadStop = 0;
    hc->ThreadRunning = 0;
    hc->ThreadObject = NULL;
    KeInitializeEvent(&hc->WorkEvent, SynchronizationEvent, FALSE);
    KeInitializeEvent(&hc->ThreadExited, NotificationEvent, FALSE);
    status = PsCreateSystemThread(&handle, THREAD_ALL_ACCESS, NULL, NULL, NULL,
                                  hcdThread, hc);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    status = STATUS_SUCCESS;
    if (IoIsWdmVersionAvailable(1, 0x10)) {
        /* A NULL object type: PsThreadType is a data import, and the handle
         * came from the call above. */
        status = ObReferenceObjectByHandle(handle, THREAD_ALL_ACCESS, NULL,
                                           KernelMode, &hc->ThreadObject,
                                           NULL);
    }
    (VOID)ZwClose(handle);
    if (!NT_SUCCESS(status)) {
        /*
         * On NT the event alone cannot cover a remove's unload, so a start
         * that could not reference its thread does not proceed (Codex review
         * of 26-A.2, round 1, note 11). The thread is told to leave and its
         * event waited for; what it still runs after that is its call into
         * PsTerminateSystemThread from this image. **That window is not
         * closed** (round 2, finding 5): with the object unreferenced there
         * is nothing to join, and a remove that unloaded the image inside it
         * would unload it under the thread. It is the residual of a failure
         * no run has seen - a reference to a handle PsCreateSystemThread has
         * just returned - and it is counted.
         */
        hc->ThreadReferenceFailures++;
        hc->ThreadObject = NULL;
        hc->ThreadStop = 1;
        HcdThreadWake(hc);
        (VOID)KeWaitForSingleObject(&hc->ThreadExited, Executive,
                                    KernelMode, FALSE, NULL);
        return status;
    }
    hc->ThreadRunning = 1;
    return STATUS_SUCCESS;
}

/* IRQL: PASSIVE_LEVEL. */
static VOID hcdThreadStop(PHCD_CONTROLLER hc)
{
    if (!hc->ThreadRunning) {
        return;
    }
    XHCI_DBG_TEXT("hcd: stopping the controller thread");
    hc->ThreadStop = 1;
    HcdThreadWake(hc);
    if (hc->ThreadObject != NULL) {
        (VOID)KeWaitForSingleObject(hc->ThreadObject, Executive, KernelMode,
                                    FALSE, NULL);
        ObDereferenceObject(hc->ThreadObject);
        hc->ThreadObject = NULL;
    } else {
        (VOID)KeWaitForSingleObject(&hc->ThreadExited, Executive, KernelMode,
                                    FALSE, NULL);
    }
    XHCI_DBG_TEXT("hcd: controller thread has exited");
    hc->ThreadRunning = 0;
}

/* --------------------------------------------------------------------- */
/* Start and stop                                                         */
/* --------------------------------------------------------------------- */

/* IRQL: PASSIVE_LEVEL. Undo whatever hcdStartController got as far as. */
static VOID hcdRelease(PHCD_CONTROLLER hc)
{
    LARGE_INTEGER due;

    XHCI_DBG_TEXT("hcd: release: interrupt");
    if (hc->Interrupt != NULL) {
        IoDisconnectInterrupt(hc->Interrupt);
        hc->Interrupt = NULL;
    }
    /* No ISR can queue it now. A DPC already queued is let run - closed, it
     * does nothing but retire its count - rather than dequeued:
     * KeRemoveQueueDpc's TRUE does not promise, before Vista SP1, that the
     * DPC will not run, and retiring its count here as well would retire it
     * twice (Codex review of 26-A.2, round 2, finding 1). */
    XHCI_DBG_TEXT("hcd: release: DPC");
    hc->DpcClosed = 1;
    while (hc->DpcsInFlight != 0) {
        HcdRelativeMs(&due, 1);
        (VOID)KeDelayExecutionThread(KernelMode, FALSE, &due);
    }
    XHCI_DBG_TEXT("hcd: release: timers");
    HcdTimersDrain(hc);
    XHCI_DBG_TEXT("hcd: release: DMA");
    HcdDmaClose(hc);
    XHCI_DBG_TEXT("hcd: release: BAR");
    if (hc->BarVa != NULL) {
        MmUnmapIoSpace(hc->BarVa, hc->BarLength);
        hc->BarVa = NULL;
    }
}

/*
 * Bring the controller up: resources, BAR, adapter and common buffer, the
 * interrupt, XhciInitController, the interrupt enables, the thread. Called
 * after the PCI stack has started (hcd_pnp.c). IRQL: PASSIVE_LEVEL.
 */
NTSTATUS HcdStartController(PHCD_CONTROLLER hc, PIRP irp)
{
    PXHCI_EXTENSION ext;
    KIRQL raised;
    USBPORT_RESOURCES res;
    PUCHAR p;
    ULONG i;
    MPSTATUS mp;
    NTSTATUS status;

    ext = &hc->Hc;

    /* usbport zeroed the miniport extension before every StartController,
     * and the kept sequence relies on it: XhciStopController's RH_CLOSED, for
     * one, is never cleared again because "a start rebuilds the extension from
     * the zero usbport writes over it". The HCD writes the zero itself. */
    p = (PUCHAR)ext;
    for (i = 0; i < sizeof(XHCI_EXTENSION); i++) {
        p[i] = 0;
    }
    /* The power fallback's marker describes the controller this start
     * rebuilds, so it is reset with it (Codex review of 26-A.2, round 4,
     * finding 1). */
    hc->SuspendedInD0 = 0;

    status = hcdParseResources(hc, irp);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    hc->BarVa = MmMapIoSpace(hc->BarTranslated, hc->BarLength, MmNonCached);
    if (hc->BarVa == NULL) {
        hcdRelease(hc);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    status = HcdDmaOpen(hc);
    if (!NT_SUCCESS(status)) {
        hcdRelease(hc);
        return status;
    }

    XhciCommandInit(ext);
    ext->DeliverUnderUsbportLockOnly = 0;
    ext->DeliverPerEndpointOnly = 0;
    ext->ArmThroughExOnly = 0;
    ext->RootHubReportThroughDpc = 0;
    ext->Signature = XHCI_EXTENSION_SIGNATURE;
    ext->TrailingSignature = XHCI_EXTENSION_TRAILING;
    hcdReadValues(hc);

    ext->ResourcesTypes = XhciResourcesRequired;
    ext->ResourceBase = (ULONG_PTR)hc->BarVa;
    ext->IoSpaceLength = hc->BarLength;
    ext->StartVA = (ULONG_PTR)hc->CommonVa;
    ext->StartPA = hc->CommonPa.LowPart;
    ext->InterruptVector = hc->IntVector;
    ext->InterruptLevel = (ULONG)hc->IntIrql;
    ext->InterruptMode = (ULONG)hc->IntMode;
    XhciLogNoteAddress(ext, "start.bar", hc->BarRaw.LowPart);
    XhciLogNote(ext, "start.common.pa", ext->StartPA);
    XhciLogNote(ext, "start.mapregs", hc->MapRegisters);

    p = (PUCHAR)&res;
    for (i = 0; i < sizeof(res); i++) {
        p[i] = 0;
    }
    res.ResourcesTypes = XhciResourcesRequired;
    res.InterruptVector = hc->IntVector;
    res.InterruptLevel = (UCHAR)hc->IntIrql;
    res.InterruptAffinity = (ULONG_PTR)hc->IntAffinity;
    res.ShareVector = (UCHAR)hc->IntShared;
    res.InterruptMode = (ULONG)hc->IntMode;
    res.ResourceBase = hc->BarVa;
    res.IoSpaceLength = hc->BarLength;
    res.StartVA = ext->StartVA;
    res.StartPA = ext->StartPA;

    HcdTimersOpen(hc);
    hc->DpcClosed = 0;

    /* Connected before the sequence, as usbport connected it before
     * StartController: XhciIsr claims nothing until the controller is
     * initialized, and the No Op self-test's completion needs the DPC. */
    status = IoConnectInterrupt(&hc->Interrupt, hcdIsr, hc, NULL,
                                hc->IntVector, hc->IntIrql, hc->IntIrql,
                                hc->IntMode, hc->IntShared, hc->IntAffinity,
                                FALSE);
    if (!NT_SUCCESS(status)) {
        hc->Interrupt = NULL;
        hcdRelease(hc);
        return status;
    }

    mp = XhciInitController(ext, &res);
    XhciLogNote(ext, "pci.cfg.status", hc->ConfigLastStatus);
    XhciLogNote(ext, "pci.cfg.info", hc->ConfigLastInformation);
    if (mp != MP_STATUS_SUCCESS) {
        XhciLogNote(ext, "start.refused.step", ext->InitStep);
        XhciLogNote(ext, "start.refused.status", ext->InitStatus);
        if (!XhciStopController(ext)) {
            XhciFailClosedDma(ext);
        }
        hcdRelease(hc);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    (VOID)XhciControllerUpdateFlags(ext, 0, XHCI_EXT_FLAG_STARTED);
    ext->InterruptEnables++;
    KeRaiseIrql(DISPATCH_LEVEL, &raised);
    XhciEnableInterrupts(ext);
    KeLowerIrql(raised);
    XhciLogNote(ext, "imod.interval", ext->ImodInterval);
    XhciLogNote(ext, "imod.readback", ext->ImodReadback);
    XhciLogNoteAddress(ext, "start.ok", (ULONG)ext->ResourceBase);

    status = hcdThreadStart(hc);
    if (!NT_SUCCESS(status)) {
        HcdStopController(hc);
        return status;
    }
    hc->ControllerStarted = 1;
    return STATUS_SUCCESS;
}

/*
 * usbport's stop order: DisableInterrupts, then StopController (which halts,
 * proves DMA stopped or says it could not), then the resources. Safe to call
 * on a controller that is not started. IRQL: PASSIVE_LEVEL.
 */
VOID HcdStopController(PHCD_CONTROLLER hc)
{
    PXHCI_EXTENSION ext;
    KIRQL raised;

    XHCI_DBG_TEXT("hcd: stop controller");
    ext = &hc->Hc;
    hcdThreadStop(hc);
    /*
     * Whenever a register window is mapped, not only when INITIALIZED is
     * set: the quiesce and a failed in-place recovery clear INITIALIZED with
     * the controller possibly still running, and XhciStopController carries
     * the admission checks of its own steps (Codex review of 26-A.2, round
     * 1, finding 1). XhciDisableInterrupts' contract is DISPATCH_LEVEL - the
     * level usbport called it at.
     */
    if (hc->BarVa != NULL && ext->ResourceBase != 0) {
        KeRaiseIrql(DISPATCH_LEVEL, &raised);
        XhciDisableInterrupts(ext);
        KeLowerIrql(raised);
        if (!XhciStopController(ext)) {
            XhciFailClosedDma(ext);
        }
    }
    (VOID)XhciControllerUpdateFlags(ext,
                                    XHCI_EXT_FLAG_STARTED |
                                        XHCI_EXT_FLAG_INTERRUPTS |
                                        XHCI_EXT_FLAG_INITIALIZED,
                                    0);
    if (hc->BarVa != NULL || hc->Dma != NULL || hc->Interrupt != NULL) {
        hcdRelease(hc);
    }
    hc->ControllerStarted = 0;
}
