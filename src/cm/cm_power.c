/*
 * cm_power.c - power for the NHI's FDO (design record 21 section 4.4), the
 * device's power policy owner, on hcd_power.c's model:
 *
 *   - A system SET_POWER is marked pending and passed down; its completion
 *     routine requests the matching device state (S0 to D0, any sleep
 *     state to D3) with PoRequestPowerIrp, and the system IRP is completed
 *     only once that device IRP has finished - on the thread, with one
 *     PoStartNextPowerIrp before the completion.
 *   - A device SET_POWER below D0 is marked pending and queued: the thread
 *     runs the quiescence of section 5.6 (the system suspend's sequence,
 *     or at shutdown the unbind), then forwards it.
 *   - A device SET_POWER to D0 is marked pending and forwarded first; its
 *     completion routine queues the reinitialisation to the thread and
 *     RETAINS the IRP until that work has finished, and only then
 *     PoStartNextPowerIrp and the completion (hcd_power.c's hcdD0Done and
 *     hcdD0Finish, with the difference that this driver's hardware work is
 *     always the thread's, never the completion routine's).
 *
 * No dispatch routine waits. Where the thread is not there to take the
 * work (not started, or its exit already queued) the IRP goes on as a
 * filter's would: a device IRP passed down or left to complete, a system
 * IRP with no device IRP requested. That is also the inert instance's
 * case for the hardware: its D3 and D0 work does nothing. Where
 * PoRequestPowerIrp fails, the transition is made on the thread without a
 * device IRP (CmPowerDirectWork, hcd_power.c's direct transition). Once a
 * removal has closed admission (CmPowerDrain), a SET_POWER passes down
 * untouched.
 *
 * Windows 98 rules (win98-wdm.md, "Power Management"), each kept as
 * hcd_power.c keeps them: PoStartNextPowerIrp before a power IRP is
 * completed or passed down; PoCallDriver, never IoCallDriver; power IRPs
 * completed at PASSIVE_LEVEL (the thread) where this driver completes
 * them; PoSetPowerState's return never used; no PoRequestPowerIrp for the
 * state the device is already in. Windows 98's power implementation is
 * mostly a no-op, so only the NT guests read this ordering.
 *
 * What the thread does at each (record 21 section 5.6):
 *   D3, system sleep: the held marks (CmReenumSuspend), then the client's
 *     suspend - rings stopped, then mailbox 0x05 and 0x07 where the row
 *     sends them. An integrated controller's sleep is unsupported (section
 *     1.4.3): its rings are stopped and nothing is sent. Under the USB4
 *     profile (record 22 sections 7.6 and 11) TbtU4Suspend instead: every
 *     tunnel up recorded as a resume obligation, then the rings stopped;
 *     nothing is sent to the fabric (no Guide chapter 8 sleep flow).
 *   D3, shutdown: the unbind, if the stop has not already run it, and the
 *     Bus Master bit and platform power restored as at a stop.
 *   D0 after a suspend this driver ran: the integrated family's force
 *     power, firmware-ready poll and LTR again, the Bus Master bit, then
 *     the client's resume and reconciliation (the USB4 profile: TbtU4Resume,
 *     every obligation verified, rebuilt, torn down on removal evidence or
 *     held), then the held marks released (CmReenumResumed) - by success
 *     only when every obligation was met, else by the failure path (record
 *     22 section 7.6, decision U15). A resume that fails is the failure
 *     after activation of section 1.4.2.
 *
 * IRQL: CmNhiPower at PASSIVE_LEVEL (the FDO is DO_POWER_PAGABLE); the
 * completion routines and the request callback at <= DISPATCH_LEVEL; the
 * *Work functions on the connection-manager thread at PASSIVE_LEVEL.
 */

#include "cm.h"

/* ---------------------------------------------------------------------- */
/* The thread's half                                                    */
/* ---------------------------------------------------------------------- */

/*
 * TbtHwPlatformInit through the driver's configuration route, its
 * refusals counted: FALSE unless every step finished and every write was
 * taken. The ownership record follows the force-power write itself: a
 * claim this call made (ForcePowerSetHere newly TRUE) stands only where
 * that write to VS_CAP_22 was taken (cm_thr.c's cmCfgWrite records its
 * refusal), so a refused write never leaves a claim a later stop would act
 * on, and a claim whose write was taken is kept whatever a later step
 * (LTR) did. IRQL: PASSIVE_LEVEL, the thread.
 */
BOOLEAN CmNhiPlatformInit(PCM_NHI nhi)
{
    ULONG before;
    BOOLEAN owned;
    LONG rc;

    before = nhi->CfgWriteFailures;
    owned = nhi->Power.ForcePowerSetHere;
    nhi->ForcePowerWriteRefused = 0;
    rc = TbtHwPlatformInit(nhi->Hw, &nhi->Cfg, &nhi->Host, &nhi->Power);
    if (!owned && nhi->Power.ForcePowerSetHere &&
        nhi->ForcePowerWriteRefused) {
        nhi->Power.ForcePowerSetHere = FALSE;
        CmLogNote(nhi, "cm.force-power.not-taken", nhi->CfgWriteFailures);
    }
    return (BOOLEAN)(rc == TBT_OK && nhi->CfgWriteFailures == before);
}

/* The system IRP whose device transition has finished. IRQL:
 * PASSIVE_LEVEL on the thread, or <= DISPATCH_LEVEL from the request
 * callback when the thread is not there (cmSystemDeviceDone). */
VOID CmPowerSystemWork(PCM_NHI nhi, PIRP irp)
{
    PoStartNextPowerIrp(irp);
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    CmPowerRelease(nhi);
}

/* An active, unfailed client in D0: the one a suspend acts on. */
static BOOLEAN cmLive(PCM_NHI nhi)
{
    return (BOOLEAN)(nhi->Stage == CM_STAGE_ACTIVE && !CmNhiFailed(nhi) &&
                     nhi->DevicePower == PowerDeviceD0 && !nhi->Suspended);
}

/*
 * Force power given back by an active instance (TbtHwPlatformShutdown),
 * where `mayClear` allows (CmNhiMayClearPower: the ICM's unbind done, or a
 * USB4 instance with its rings stopped and no device router connected,
 * record 22 section 4.2): where the configuration write is refused, the
 * ownership record is kept for a later attempt rather than taken as
 * discharged. IRQL: PASSIVE_LEVEL, the thread.
 */
VOID CmNhiPlatformRelease(PCM_NHI nhi, BOOLEAN mayClear)
{
    ULONG before;
    BOOLEAN owned;

    before = nhi->CfgWriteFailures;
    owned = nhi->Power.ForcePowerSetHere;
    if (owned && !mayClear) {
        /* Record 22 section 4.2: a router is still connected on a port
         * of this domain; the bit stays set and the record with it. */
        nhi->Counters.U4ForcePowerKept++;
        CmLogNote(nhi, "u4.force-power.kept", 1);
    }
    (VOID)TbtHwPlatformShutdown(nhi->Hw, &nhi->Cfg, &nhi->Power, mayClear);
    if (owned && nhi->CfgWriteFailures != before) {
        nhi->Power.ForcePowerSetHere = TRUE;
        CmLogNote(nhi, "cm.force-power.kept", nhi->CfgWriteFailures);
    }
}

/*
 * The USB4 profile's suspend (record 22 sections 7.6 and 11): the
 * obligations recorded and the rings stopped (TbtU4Suspend), and the
 * resume bound for them logged at the tick read now - the hold's own
 * watchdog is the re-enumeration seam's (cm_reenum.h, CmReenumSuspend).
 * IRQL: PASSIVE_LEVEL, the thread.
 */
static VOID cmU4Suspend(PCM_NHI nhi)
{
    ULONG i;
    ULONG n;

    TbtU4Suspend(&nhi->U4);
    n = 0;
    for (i = 0; i < TBT_U4_OBLIGATIONS; i++) {
        if (nhi->U4.Obligations[i].Used) {
            n++;
        }
    }
    CmLogNote(nhi, "u4.suspend.obligations", n);
    CmLogNote(nhi, "u4.resume.bound-ms",
              TbtU4ResumeBoundUs(n, nhi->ClockTickUs) / 1000UL);
}

/*
 * The quiescence of record 21 section 5.6 before the function leaves D0
 * (or, PoRequestPowerIrp having failed, before the system sleeps with it
 * still in D0): at shutdown the unbind, with the Bus Master bit and the
 * force power this driver set restored (an inert instance leaves force
 * power as it is, even after its own readiness timeout: orchestrator
 * decision of 2026-10-15, owner to confirm); at a sleep the held marks,
 * then the client's suspend - rings stopped, then mailbox 0x05 and 0x07
 * where the row sends them. IRQL: PASSIVE_LEVEL, the thread.
 */
VOID CmNhiQuiesce(PCM_NHI nhi)
{
    if (nhi->DevicePower != PowerDeviceD0) {
        return;
    }
    /* At shutdown a client already suspended in D0 (the direct
     * transition) still owes its unbind and its ownership cleanup. */
    if (nhi->SystemPower == PowerSystemShutdown) {
        if (nhi->Stage != CM_STAGE_ACTIVE && nhi->Stage != CM_STAGE_FAILED) {
            return;
        }
        nhi->Counters.Shutdowns++;
        CmLogNote(nhi, "cm.shutdown", nhi->Stage);
        if (nhi->ReenumOn) {
            CmReenumStop(nhi);
            nhi->ReenumOn = 0;
        }
        if (nhi->U4On || CmNhiHwAccess(nhi)) {
            CmNhiUnbind(nhi);
            CmNhiDrainActions(nhi);
            (VOID)CmNhiSetBusMaster(nhi, FALSE);
        } else {
            /* The liveness gate is closed: nothing is written to a host
             * interface that stopped answering (record 21 section 5.6). */
            CmLogNote(nhi, "cm.shutdown.no-access", nhi->Live.State);
            CmNhiDrainActions(nhi);
        }
        CmNhiPlatformRelease(nhi, CmNhiMayClearPower(nhi));
        return;
    }
    if (!cmLive(nhi)) {
        return;
    }
    nhi->Counters.Suspends++;
    CmLogNote(nhi, "cm.suspend", (ULONG)nhi->SystemPower);
    if (nhi->U4On) {
        /* The bound the hold's watchdog must cover, for the most
         * obligations a domain can hold, before the hold is published
         * (cm_reenum.h, CmReenumSuspend). */
        nhi->U4ResumeBoundMs =
            (TbtU4ResumeBoundUs(TBT_U4_OBLIGATIONS, nhi->ClockTickUs) +
             999UL) / 1000UL;
    }
    if (nhi->ReenumOn) {
        CmReenumSuspend(nhi);
    }
    /* A monitored instance stops being sampled; one the gate closed stays
     * closed, so its suspend and its resume touch nothing and the resume
     * takes the failure path (U15's order). */
    if (CmNhiHwAccess(nhi)) {
        CmLiveDisarm(&nhi->Live);
    }
    if (nhi->U4On) {
        cmU4Suspend(nhi);
    } else {
        TbtIcmSuspend(&nhi->Icm);
    }
    nhi->Suspended = 1;
    CmNhiDrainActions(nhi);
}

/*
 * The reinitialisation once the function is in D0 again after a suspend
 * this driver ran: the integrated family's force power, firmware-ready poll
 * and LTR (decision D: its .init is its resume), the Bus Master bit, the
 * client's resume and reconciliation, then the held marks released. A
 * resume that fails is the failure after activation of section 1.4.2.
 * Returns FALSE for that. IRQL: PASSIVE_LEVEL, the thread.
 */
BOOLEAN CmNhiReinit(PCM_NHI nhi)
{
    BOOLEAN ok;
    BOOLEAN held;
    LONG rc;

    if (!nhi->Suspended || nhi->DevicePower != PowerDeviceD0) {
        return TRUE;
    }
    nhi->Suspended = 0;
    if (nhi->Stage != CM_STAGE_ACTIVE || CmNhiFailed(nhi)) {
        return TRUE;
    }
    nhi->Counters.Resumes++;
    CmLogNote(nhi, "cm.resume", (ULONG)nhi->SystemPower);
    if (!nhi->U4On && !CmNhiHwAccess(nhi)) {
        /* Suspended while the liveness gate was closed: nothing is written
         * to it on the way back; the failure path below (U15's order). */
        CmLogNote(nhi, "cm.resume.no-access", nhi->Live.State);
        ok = FALSE;
        held = FALSE;
        goto resumed;
    }
    ok = CmNhiPlatformInit(nhi);
    if (ok && !CmNhiSetBusMaster(nhi, TRUE)) {
        ok = FALSE;
    }
    held = FALSE;
    if (ok && nhi->U4On) {
        rc = TbtU4Resume(&nhi->U4);
        if (rc == TBT_E_STATE && !nhi->U4.Failed) {
            /* Record 22 section 7.6: the domain runs, but an obligation
             * was not met (a port HELD, a budget spent): the hold is
             * released by the failure path, nothing is torn down. */
            nhi->Counters.U4ResumeHeld++;
            CmLogNote(nhi, "u4.resume.held", 1);
            held = TRUE;
        } else if (rc != TBT_OK) {
            CmLogNote(nhi, "cm.resume-failed", (ULONG)(-rc));
            CmLogNote(nhi, "u4.inert-reason", nhi->U4.InertReason);
            ok = FALSE;
        }
    } else if (ok) {
        rc = TbtIcmResume(&nhi->Icm);
        if (rc != TBT_OK) {
            CmLogNote(nhi, "cm.resume-failed", (ULONG)(-rc));
            ok = FALSE;
        } else if (!CmNhiLiveArm(nhi)) {
            CmLogNote(nhi, "cm.resume-failed", 0x100UL);
            ok = FALSE;
        }
    }
resumed:
    if (!ok) {
        /* The client stopped its rings (TbtIcmResume's or TbtU4Resume's
         * failure), or never restarted them (the platform or Bus Master
         * step; the USB4 profile's rings stay stopped since its suspend,
         * and its common buffer is freed only at the stop): nothing of
         * this driver's runs on the hardware, and the stop sends 0x07 only
         * where the session was left open. */
        nhi->Counters.ResumeFailures++;
        nhi->Stage = CM_STAGE_FAILED;
    }
    /* A rebuilt tunnel's UP published to the nvme98 channel before any
     * held bridge is released (CmNhiU4Take; the batch is delivered to the
     * re-enumeration seam by the drain below). */
    CmNhiU4Take(nhi);
    if (nhi->ReenumOn) {
        CmReenumResumed(nhi, (BOOLEAN)(ok && !held));
    }
    CmNhiDrainActions(nhi);
    return ok;
}

VOID CmPowerD3Work(PCM_NHI nhi, PIRP irp)
{
    PIO_STACK_LOCATION stack;

    stack = IoGetCurrentIrpStackLocation(irp);
    CmNhiQuiesce(nhi);
    nhi->DevicePower = stack->Parameters.Power.State.DeviceState;
    (VOID)PoSetPowerState(nhi->Common.Self, DevicePowerState,
                          stack->Parameters.Power.State);
    PoStartNextPowerIrp(irp);
    /* Copied, not skipped: this driver marked the IRP pending in its own
     * stack location, which the lower driver must not reuse. */
    IoCopyCurrentIrpStackLocationToNext(irp);
    (VOID)PoCallDriver(nhi->Common.Lower, irp);
    CmPowerRelease(nhi);
}

VOID CmPowerD0Work(PCM_NHI nhi, PIRP irp)
{
    PIO_STACK_LOCATION stack;

    stack = IoGetCurrentIrpStackLocation(irp);
    nhi->DevicePower = PowerDeviceD0;
    (VOID)CmNhiReinit(nhi);
    (VOID)PoSetPowerState(nhi->Common.Self, DevicePowerState,
                          stack->Parameters.Power.State);
    PoStartNextPowerIrp(irp);
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    CmPowerRelease(nhi);
}

/*
 * A system IRP whose device IRP could not be requested (hcd_power.c's
 * direct transition): toward sleep the quiescence while the bus still
 * holds the function in D0 - Suspended then records a client suspended in
 * D0, and polling stops; toward S0 the reinitialisation of such a client.
 * The device state recorded stays D0 either way. Then the system IRP
 * completes. IRQL: PASSIVE_LEVEL, the thread.
 */
VOID CmPowerDirectWork(PCM_NHI nhi, PIRP irp)
{
    CmLogNote(nhi, "cm.power.direct", (ULONG)nhi->DirectWant);
    if (nhi->DirectWant == PowerDeviceD0) {
        /* CmNhiReinit acts only on a client suspended in D0; a function
         * the bus has not powered keeps its suspension (cmSystemDone never
         * queues that case). */
        (VOID)CmNhiReinit(nhi);
    } else {
        CmNhiQuiesce(nhi);
    }
    CmPowerSystemWork(nhi, irp);
}

/* ---------------------------------------------------------------------- */
/* Dispatch and completion                                                */
/* ---------------------------------------------------------------------- */

/* IRQL: <= DISPATCH_LEVEL (a completion routine). */
static NTSTATUS NTAPI cmD0Done(PDEVICE_OBJECT DeviceObject, PIRP Irp,
                               PVOID Context)
{
    PCM_NHI nhi;
    PIO_STACK_LOCATION stack;

    UNREFERENCED_PARAMETER(DeviceObject);

    nhi = (PCM_NHI)Context;
    if (NT_SUCCESS(Irp->IoStatus.Status)) {
        if (CmThrQueueOrRecord(nhi, CM_WORK_D0, Irp, PowerDeviceD0)) {
            return STATUS_MORE_PROCESSING_REQUIRED;
        }
        /* No thread to reinitialise anything: the IRP completes as it came
         * back, its state recorded (under WorkLock, by the call above). */
        stack = IoGetCurrentIrpStackLocation(Irp);
        (VOID)PoSetPowerState(nhi->Common.Self, DevicePowerState,
                              stack->Parameters.Power.State);
        nhi->Counters.PowerInline++;
    }
    /* No reinitialisation will run for this D0 (refused below, or no
     * thread): a suspend this driver ran, or a domain still holding its
     * tunnel bridges, is owed their release now, through the failure path,
     * rather than at the bridge filters' watchdog (cm_reenum.h). */
    if (nhi->Suspended || nhi->ReenumOn) {
        CmReenumResumeFailed(nhi);
    }
    PoStartNextPowerIrp(Irp);
    CmPowerRelease(nhi);
    return STATUS_SUCCESS;
}

/* The device IRP a system IRP asked for has finished. IRQL:
 * <= DISPATCH_LEVEL. */
static VOID NTAPI cmSystemDeviceDone(PDEVICE_OBJECT DeviceObject,
                                     UCHAR MinorFunction,
                                     POWER_STATE PowerState, PVOID Context,
                                     PIO_STATUS_BLOCK IoStatus)
{
    PCM_NHI nhi;
    PIRP systemIrp;

    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(MinorFunction);
    UNREFERENCED_PARAMETER(PowerState);
    UNREFERENCED_PARAMETER(IoStatus);

    /* One system IRP is in flight at a time (the power manager's rule),
     * so one slot holds it, as hcd_power.c's PendingSystemIrp does. */
    nhi = (PCM_NHI)Context;
    systemIrp = nhi->PendingSystemIrp;
    nhi->PendingSystemIrp = NULL;
    if (KeGetCurrentIrql() == PASSIVE_LEVEL ||
        !CmThrQueue(nhi, CM_WORK_SYSTEM, systemIrp)) {
        if (KeGetCurrentIrql() != PASSIVE_LEVEL) {
            nhi->Counters.PowerInline++;
        }
        CmPowerSystemWork(nhi, systemIrp);
    }
}

/* The system IRP goes on with no device transition of this driver's.
 * IRQL: <= DISPATCH_LEVEL (cmSystemDone's). */
static NTSTATUS cmSystemPass(PCM_NHI nhi, PIRP irp)
{
    PoStartNextPowerIrp(irp);
    CmPowerRelease(nhi);
    return STATUS_SUCCESS;
}

/* The direct transition, on the thread; FALSE when there is none. IRQL:
 * <= DISPATCH_LEVEL (cmSystemDone's). */
static BOOLEAN cmSystemDirect(PCM_NHI nhi, PIRP irp, DEVICE_POWER_STATE want)
{
    nhi->DirectWant = want;
    return CmThrQueue(nhi, CM_WORK_DIRECT, irp);
}

/* IRQL: <= DISPATCH_LEVEL (a completion routine). */
static NTSTATUS NTAPI cmSystemDone(PDEVICE_OBJECT DeviceObject, PIRP Irp,
                                   PVOID Context)
{
    PCM_NHI nhi;
    PIO_STACK_LOCATION stack;
    DEVICE_POWER_STATE want;
    POWER_STATE state;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(DeviceObject);

    nhi = (PCM_NHI)Context;
    stack = IoGetCurrentIrpStackLocation(Irp);
    if (NT_SUCCESS(Irp->IoStatus.Status)) {
        /* Published under WorkLock: the sleep gate a start in progress or
         * about to open the queue reads (CmNhiStartWork, CmNhiBind), so no
         * client is activated once a sleep or shutdown has been taken. */
        CmPowerSetSystem(nhi, stack->Parameters.Power.State.SystemState);
    }
    want = (nhi->SystemPower == PowerSystemWorking) ? PowerDeviceD0
                                                     : PowerDeviceD3;
    if (!NT_SUCCESS(Irp->IoStatus.Status) || !nhi->Started) {
        return cmSystemPass(nhi, Irp);
    }
    if (want == nhi->DevicePower) {
        /* An earlier direct transition left the client suspended in D0:
         * no device IRP is owed, only its reinitialisation. */
        if (want == PowerDeviceD0 && nhi->Suspended &&
            cmSystemDirect(nhi, Irp, want)) {
            return STATUS_MORE_PROCESSING_REQUIRED;
        }
        return cmSystemPass(nhi, Irp);
    }
    state.DeviceState = want;
    nhi->PendingSystemIrp = Irp;
    status = PoRequestPowerIrp(nhi->Common.Pdo, IRP_MN_SET_POWER, state,
                               cmSystemDeviceDone, nhi, NULL);
    if (!NT_SUCCESS(status)) {
        /* No device IRP and no callback: the transition is made without
         * one, on the thread, before the system IRP completes. */
        nhi->PendingSystemIrp = NULL;
        nhi->Counters.PowerRequestFailures++;
        if (want == PowerDeviceD0) {
            /* Toward S0 with the function really out of D0: nothing
             * touches hardware the bus has not powered (hcd_power.c's
             * rule); the client stays suspended, and the wake is counted. */
            nhi->Counters.WakesUnpowered++;
            return cmSystemPass(nhi, Irp);
        }
        if (cmSystemDirect(nhi, Irp, want)) {
            return STATUS_MORE_PROCESSING_REQUIRED;
        }
        return cmSystemPass(nhi, Irp);
    }
    return STATUS_MORE_PROCESSING_REQUIRED;
}

NTSTATUS CmNhiPower(PCM_NHI nhi, PIRP irp)
{
    PIO_STACK_LOCATION stack;

    NTSTATUS status;

    stack = IoGetCurrentIrpStackLocation(irp);
    if (!CmPowerHold(nhi)) {
        /* The removal has closed admission and may free the extension at
         * any moment: only the IRP is touched. */
        PoStartNextPowerIrp(irp);
        irp->IoStatus.Status = STATUS_DELETE_PENDING;
        irp->IoStatus.Information = 0;
        IoCompleteRequest(irp, IO_NO_INCREMENT);
        return STATUS_DELETE_PENDING;
    }
    if (stack->MinorFunction != IRP_MN_SET_POWER) {
        /* Every request but SET_POWER passes down as a filter's would,
         * held until PoCallDriver returns. */
        PoStartNextPowerIrp(irp);
        IoSkipCurrentIrpStackLocation(irp);
        status = PoCallDriver(nhi->Common.Lower, irp);
        CmPowerRelease(nhi);
        return status;
    }
    if (stack->Parameters.Power.Type == SystemPowerState) {
        IoMarkIrpPending(irp);
        IoCopyCurrentIrpStackLocationToNext(irp);
        CM_SET_COMPLETION_ALWAYS(irp, cmSystemDone, nhi);
        (VOID)PoCallDriver(nhi->Common.Lower, irp);
        return STATUS_PENDING;
    }
    if (stack->Parameters.Power.State.DeviceState == PowerDeviceD0) {
        IoMarkIrpPending(irp);
        IoCopyCurrentIrpStackLocationToNext(irp);
        CM_SET_COMPLETION_ALWAYS(irp, cmD0Done, nhi);
        (VOID)PoCallDriver(nhi->Common.Lower, irp);
        return STATUS_PENDING;
    }
    IoMarkIrpPending(irp);
    if (CmThrQueueOrRecord(nhi, CM_WORK_D3, irp,
                           stack->Parameters.Power.State.DeviceState)) {
        return STATUS_PENDING;
    }
    /* No thread: nothing of this driver's runs on the hardware, so the
     * transition needs nothing but its record, published by the call
     * above under WorkLock before a start could open the queue. */
    (VOID)PoSetPowerState(nhi->Common.Self, DevicePowerState,
                          stack->Parameters.Power.State);
    PoStartNextPowerIrp(irp);
    IoCopyCurrentIrpStackLocationToNext(irp);
    (VOID)PoCallDriver(nhi->Common.Lower, irp);
    CmPowerRelease(nhi);
    return STATUS_PENDING;
}
