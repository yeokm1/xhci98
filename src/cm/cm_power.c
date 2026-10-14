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
 *     1.4.3): its rings are stopped and nothing is sent.
 *   D3, shutdown: the unbind, if the stop has not already run it, and the
 *     Bus Master bit and platform power restored as at a stop.
 *   D0 after a suspend this driver ran: the integrated family's force
 *     power, firmware-ready poll and LTR again, the Bus Master bit, then
 *     the client's resume and reconciliation, then the held marks released
 *     (CmReenumResumed). A resume that fails is the failure after
 *     activation of section 1.4.2.
 *
 * IRQL: CmNhiPower at PASSIVE_LEVEL (the FDO is DO_POWER_PAGABLE); the
 * completion routines and the request callback at <= DISPATCH_LEVEL; the
 * *Work functions on the connection-manager thread at PASSIVE_LEVEL.
 */

#include "cm.h"

/* ---------------------------------------------------------------------- */
/* The thread's half                                                    */
/* ---------------------------------------------------------------------- */

/* The system IRP whose device transition has finished. */
VOID CmPowerSystemWork(PCM_NHI nhi, PIRP irp)
{
    PoStartNextPowerIrp(irp);
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    CmPowerRelease(nhi);
}

/* An active, unfailed client in D0: the one a suspend acts on. */
static BOOLEAN cmLive(PCM_NHI nhi)
{
    return (BOOLEAN)(nhi->Stage == CM_STAGE_ACTIVE && !nhi->Icm.Failed &&
                     nhi->DevicePower == PowerDeviceD0 && !nhi->Suspended);
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
    if (nhi->DevicePower != PowerDeviceD0 || nhi->Suspended) {
        return;
    }
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
        TbtIcmUnbind(&nhi->Icm);
        CmNhiDrainActions(nhi);
        (VOID)CmNhiSetBusMaster(nhi, FALSE);
        (VOID)TbtHwPlatformShutdown(nhi->Hw, &nhi->Cfg, &nhi->Power, TRUE);
        return;
    }
    if (!cmLive(nhi)) {
        return;
    }
    nhi->Counters.Suspends++;
    CmLogNote(nhi, "cm.suspend", (ULONG)nhi->SystemPower);
    if (nhi->ReenumOn) {
        CmReenumSuspend(nhi);
    }
    TbtIcmSuspend(&nhi->Icm);
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
    LONG rc;

    if (!nhi->Suspended) {
        return TRUE;
    }
    nhi->Suspended = 0;
    if (nhi->Stage != CM_STAGE_ACTIVE || nhi->Icm.Failed) {
        return TRUE;
    }
    nhi->Counters.Resumes++;
    CmLogNote(nhi, "cm.resume", (ULONG)nhi->SystemPower);
    ok = TRUE;
    if (TbtHwPlatformInit(nhi->Hw, &nhi->Cfg, &nhi->Host, &nhi->Power) !=
        TBT_OK) {
        ok = FALSE;
    }
    if (ok && !CmNhiSetBusMaster(nhi, TRUE)) {
        ok = FALSE;
    }
    if (ok) {
        rc = TbtIcmResume(&nhi->Icm);
        if (rc != TBT_OK) {
            CmLogNote(nhi, "cm.resume-failed", (ULONG)(-rc));
            ok = FALSE;
        }
    }
    if (!ok) {
        /* The client stopped its rings (TbtIcmResume's failure), or never
         * restarted them (the platform or Bus Master step): nothing of
         * this driver's runs on the hardware, and the stop sends 0x07 only
         * where the session was left open. */
        nhi->Counters.ResumeFailures++;
        nhi->Stage = CM_STAGE_FAILED;
    }
    if (nhi->ReenumOn) {
        CmReenumResumed(nhi, ok);
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
        (VOID)CmNhiReinit(nhi);
    } else {
        CmNhiQuiesce(nhi);
    }
    CmPowerSystemWork(nhi, irp);
}

/* ---------------------------------------------------------------------- */
/* Dispatch and completion                                                */
/* ---------------------------------------------------------------------- */

static NTSTATUS NTAPI cmD0Done(PDEVICE_OBJECT DeviceObject, PIRP Irp,
                               PVOID Context)
{
    PCM_NHI nhi;
    PIO_STACK_LOCATION stack;

    UNREFERENCED_PARAMETER(DeviceObject);

    nhi = (PCM_NHI)Context;
    if (NT_SUCCESS(Irp->IoStatus.Status) &&
        CmThrQueue(nhi, CM_WORK_D0, Irp)) {
        return STATUS_MORE_PROCESSING_REQUIRED;
    }
    /* Refused below, or no thread to reinitialise anything: the IRP
     * completes as it came back, the state recorded where it succeeded. */
    if (NT_SUCCESS(Irp->IoStatus.Status)) {
        stack = IoGetCurrentIrpStackLocation(Irp);
        nhi->DevicePower = PowerDeviceD0;
        (VOID)PoSetPowerState(nhi->Common.Self, DevicePowerState,
                              stack->Parameters.Power.State);
        nhi->Counters.PowerInline++;
    }
    PoStartNextPowerIrp(Irp);
    CmPowerRelease(nhi);
    return STATUS_SUCCESS;
}

/* The device IRP a system IRP asked for has finished. */
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

/* The system IRP goes on with no device transition of this driver's. */
static NTSTATUS cmSystemPass(PCM_NHI nhi, PIRP irp)
{
    PoStartNextPowerIrp(irp);
    CmPowerRelease(nhi);
    return STATUS_SUCCESS;
}

/* The direct transition, on the thread; FALSE when there is none. */
static BOOLEAN cmSystemDirect(PCM_NHI nhi, PIRP irp, DEVICE_POWER_STATE want)
{
    nhi->DirectWant = want;
    return CmThrQueue(nhi, CM_WORK_DIRECT, irp);
}

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
        nhi->SystemPower = stack->Parameters.Power.State.SystemState;
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

    stack = IoGetCurrentIrpStackLocation(irp);
    if (stack->MinorFunction != IRP_MN_SET_POWER || !CmPowerHold(nhi)) {
        /* Every request but SET_POWER, and any once the removal has
         * closed admission, passes down as a filter's would. */
        PoStartNextPowerIrp(irp);
        IoSkipCurrentIrpStackLocation(irp);
        return PoCallDriver(nhi->Common.Lower, irp);
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
    if (CmThrQueue(nhi, CM_WORK_D3, irp)) {
        return STATUS_PENDING;
    }
    /* No thread: nothing of this driver's runs on the hardware, so the
     * transition needs nothing but its record. */
    nhi->DevicePower = stack->Parameters.Power.State.DeviceState;
    (VOID)PoSetPowerState(nhi->Common.Self, DevicePowerState,
                          stack->Parameters.Power.State);
    PoStartNextPowerIrp(irp);
    IoCopyCurrentIrpStackLocationToNext(irp);
    (VOID)PoCallDriver(nhi->Common.Lower, irp);
    CmPowerRelease(nhi);
    return STATUS_PENDING;
}
