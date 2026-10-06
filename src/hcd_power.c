/*
 * hcd_power.c - power for the controller FDO of xhci98.sys (roadmap-hcd.md
 * task 26-A.2; win98-wdm.md, "Power Management").
 *
 * The FDO is the device's power policy owner, and follows the WDM sequence
 * for one:
 *
 *   - A system SET_POWER goes down first; its completion routine requests the
 *     matching device state - S0 to D0, any sleep state to D3 - with
 *     PoRequestPowerIrp, and the system IRP is completed only from that
 *     request's callback, once the device transition is done.
 *   - A device SET_POWER to a lower state suspends the controller
 *     (XhciSuspendController) and then goes down.
 *   - A device SET_POWER to D0 goes down first; the controller resumes
 *     (XhciResumeController) after the bus has powered it, from the
 *     completion routine when that runs at PASSIVE_LEVEL and from a work item
 *     otherwise. A resume that fails declares the controller failed and asks
 *     for the in-place recovery (HcdControllerFail).
 *
 * No dispatch routine waits for its own IRP (Codex review of 26-A.2, round 1,
 * findings 5 and 6). The D0 and system SET_POWER IRPs, which this driver
 * pends, keep the outstanding-I/O count until it completes them, so a remove
 * waits for them (finding 4); a power IRP it only passes down releases the
 * count when PoCallDriver returns (round 3, note 4).
 *
 * The Windows 98 rules of win98-wdm.md, each kept here:
 *   - PoStartNextPowerIrp before a power IRP is completed or passed down;
 *   - PoCallDriver, never IoCallDriver, for a power IRP;
 *   - power IRPs completed at PASSIVE_LEVEL only - the work item is the
 *     route when a completion arrives above it;
 *   - PoSetPowerState's return is never used (Windows 98 returns its
 *     argument);
 *   - no PoRequestPowerIrp for the state the device is already in (on
 *     Windows 98 it returns STATUS_PENDING and never delivers the IRP).
 *
 * Neither primary's test guest sleeps (Windows 2000's runs acpi=off). What has
 * run is a shutdown on both (runs/run-26.md): the system IRP, the D3 it
 * requested, the suspend. A D0 resume, the work-item route and the
 * direct transition have not run on a guest.
 *
 * IRQL: dispatch at PASSIVE_LEVEL (the FDO is DO_POWER_PAGABLE); the
 * completion routines and the request callback at <= DISPATCH_LEVEL; the
 * work item at PASSIVE_LEVEL.
 */

#include "hcd.h"
#include "xhci_hw.h"
#include "xhci_dbg.h"

#define HCD_POWER_WORK_D0     1UL
#define HCD_POWER_WORK_SYSTEM 2UL
#define HCD_POWER_WORK_DIRECT 3UL

/* The resume half of a D0, then the IRP's completion. IRQL: PASSIVE_LEVEL. */
static VOID hcdD0Finish(PHCD_CONTROLLER hc, PIRP irp)
{
    PIO_STACK_LOCATION stack;

    stack = IoGetCurrentIrpStackLocation(irp);
    HcdPowerGateEnter(hc);
    if (hc->ControllerStarted &&
        (hc->Common.DevicePower != PowerDeviceD0 || hc->SuspendedInD0)) {
        hc->SuspendedInD0 = 0;
        /* Task 34.3: firmware may have put the connectors back on EHCI
         * across the sleep; route them before the ports are resumed. */
        HcdPswRoute(hc);
        hc->TolStartGen++;
        if (XhciResumeController(&hc->Hc) != MP_STATUS_SUCCESS) {
            hc->ResumeFailures++;
            HcdControllerFail(hc);
        }
    }
    hc->Common.DevicePower = PowerDeviceD0;
    HcdPowerGateLeave(hc);
    (VOID)PoSetPowerState(hc->Common.Self, DevicePowerState,
                          stack->Parameters.Power.State);
    PoStartNextPowerIrp(irp);
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    HcdIoLeave(hc);
}

/*
 * The device transition a system IRP wanted, when the device IRP that should
 * have carried it could not be requested (PoRequestPowerIrp failed, so no
 * IRP and no callback follow; Codex review of 26-A.2, round 2, finding 3).
 * Without that IRP the bus does not change the function's power, so only
 * what is safe on the bus's present state is done (round 3, finding 1):
 *
 *   - toward sleep, with the function still in D0: the controller is
 *     suspended - its DMA stopped before the system sleeps - and
 *     SuspendedInD0 records that the controller is suspended while the bus
 *     still holds it in D0; the device state recorded stays D0;
 *   - toward S0, when an earlier fallback left it suspended in D0: it is
 *     resumed, the bus never having powered it down;
 *   - toward S0 with the function really in a lower state: nothing touches
 *     the hardware, which the bus has not powered, and the wake is counted.
 *
 * IRQL: PASSIVE_LEVEL.
 */
static VOID hcdDirectTransitionGated(PHCD_CONTROLLER hc);

static VOID hcdDirectTransition(PHCD_CONTROLLER hc)
{
    HcdPowerGateEnter(hc);
    hcdDirectTransitionGated(hc);
    HcdPowerGateLeave(hc);
}

/* The body of hcdDirectTransition, the power gate held. */
static VOID hcdDirectTransitionGated(PHCD_CONTROLLER hc)
{
    if (hc->PowerDirectWant != PowerDeviceD0) {
        if (hc->Common.DevicePower == PowerDeviceD0 && !hc->SuspendedInD0) {
            XhciSuspendController(&hc->Hc);
            hc->SuspendedInD0 = 1;
        }
        if (hc->Common.SystemPower == PowerSystemShutdown) {
            HcdPswRelease(hc);
        }
        return;
    }
    if (hc->SuspendedInD0) {
        hc->SuspendedInD0 = 0;
        HcdPswRoute(hc);
        hc->TolStartGen++;
        if (XhciResumeController(&hc->Hc) != MP_STATUS_SUCCESS) {
            hc->ResumeFailures++;
            HcdControllerFail(hc);
        }
        return;
    }
    hc->WakesWithoutPower++;
}

/* IRQL: PASSIVE_LEVEL (a system worker thread). */
static VOID NTAPI hcdPowerWork(PVOID Context)
{
    PHCD_CONTROLLER hc;
    PIRP irp;
    ULONG kind;

    hc = (PHCD_CONTROLLER)Context;
    irp = hc->PowerWorkIrp;
    kind = hc->PowerWorkKind;
    hc->PowerWorkIrp = NULL;
    hc->PowerWorkKind = 0;

    if (kind == HCD_POWER_WORK_D0) {
        hcdD0Finish(hc, irp);
        return;
    }
    if (kind == HCD_POWER_WORK_DIRECT) {
        hcdDirectTransition(hc);
    }
    PoStartNextPowerIrp(irp);
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    HcdIoLeave(hc);
}

/* One power IRP of each kind is in flight at a time - the power manager
 * serializes them per device - so one work item serves both. IRQL:
 * <= DISPATCH_LEVEL. */
static VOID hcdPowerDefer(PHCD_CONTROLLER hc, PIRP irp, ULONG kind)
{
    hc->PowerWorkIrp = irp;
    hc->PowerWorkKind = kind;
    ExInitializeWorkItem(&hc->PowerWork, hcdPowerWork, hc);
    ExQueueWorkItem(&hc->PowerWork, DelayedWorkQueue);
}

/* IRQL: <= DISPATCH_LEVEL. */
static NTSTATUS NTAPI hcdD0Done(PDEVICE_OBJECT DeviceObject, PIRP Irp,
                                PVOID Context)
{
    PHCD_CONTROLLER hc;

    UNREFERENCED_PARAMETER(DeviceObject);

    hc = (PHCD_CONTROLLER)Context;
    if (!NT_SUCCESS(Irp->IoStatus.Status)) {
        PoStartNextPowerIrp(Irp);
        HcdIoLeave(hc);
        return STATUS_SUCCESS;
    }
    if (KeGetCurrentIrql() == PASSIVE_LEVEL) {
        hcdD0Finish(hc, Irp);
    } else {
        hcdPowerDefer(hc, Irp, HCD_POWER_WORK_D0);
    }
    return STATUS_MORE_PROCESSING_REQUIRED;
}

/* The device IRP the system IRP asked for has finished: the system IRP may
 * now complete. IRQL: <= DISPATCH_LEVEL. */
static VOID NTAPI hcdSystemDeviceDone(PDEVICE_OBJECT DeviceObject,
                                      UCHAR MinorFunction,
                                      POWER_STATE PowerState, PVOID Context,
                                      PIO_STATUS_BLOCK IoStatus)
{
    PHCD_CONTROLLER hc;
    PIRP systemIrp;

    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(MinorFunction);
    UNREFERENCED_PARAMETER(PowerState);
    UNREFERENCED_PARAMETER(IoStatus);

    hc = (PHCD_CONTROLLER)Context;
    systemIrp = hc->PendingSystemIrp;
    hc->PendingSystemIrp = NULL;
    if (KeGetCurrentIrql() == PASSIVE_LEVEL) {
        PoStartNextPowerIrp(systemIrp);
        IoCompleteRequest(systemIrp, IO_NO_INCREMENT);
        HcdIoLeave(hc);
    } else {
        hcdPowerDefer(hc, systemIrp, HCD_POWER_WORK_SYSTEM);
    }
}

/* IRQL: <= DISPATCH_LEVEL. */
static NTSTATUS NTAPI hcdSystemDone(PDEVICE_OBJECT DeviceObject, PIRP Irp,
                                    PVOID Context)
{
    PHCD_CONTROLLER hc;
    PIO_STACK_LOCATION stack;
    DEVICE_POWER_STATE want;
    POWER_STATE state;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(DeviceObject);

    hc = (PHCD_CONTROLLER)Context;
    stack = IoGetCurrentIrpStackLocation(Irp);
    if (NT_SUCCESS(Irp->IoStatus.Status)) {
        hc->Common.SystemPower = stack->Parameters.Power.State.SystemState;
    }
    want = (hc->Common.SystemPower == PowerSystemWorking) ? PowerDeviceD0
                                                           : PowerDeviceD3;
    if (NT_SUCCESS(Irp->IoStatus.Status) && hc->ControllerStarted &&
        want == PowerDeviceD0 && hc->Common.DevicePower == PowerDeviceD0 &&
        hc->SuspendedInD0) {
        /* An earlier fallback suspended it in D0: no device IRP is owed,
         * only the resume. */
        hc->PowerDirectWant = PowerDeviceD0;
        hcdPowerDefer(hc, Irp, HCD_POWER_WORK_DIRECT);
        return STATUS_MORE_PROCESSING_REQUIRED;
    }
    if (!NT_SUCCESS(Irp->IoStatus.Status) || !hc->ControllerStarted ||
        want == hc->Common.DevicePower) {
        PoStartNextPowerIrp(Irp);
        HcdIoLeave(hc);
        return STATUS_SUCCESS;
    }

    hc->PendingSystemIrp = Irp;
    state.DeviceState = want;
    status = PoRequestPowerIrp(hc->Pdo, IRP_MN_SET_POWER, state,
                               hcdSystemDeviceDone, hc, NULL);
    if (!NT_SUCCESS(status)) {
        /* No device IRP and no callback will follow: the work item makes the
         * transition itself and then completes the system IRP. */
        hc->PendingSystemIrp = NULL;
        hc->PowerRequestFailures++;
        hc->PowerDirectWant = want;
        hcdPowerDefer(hc, Irp, HCD_POWER_WORK_DIRECT);
    }
    return STATUS_MORE_PROCESSING_REQUIRED;
}

NTSTATUS HcdControllerPower(PHCD_CONTROLLER hc, PIRP irp)
{
    PIO_STACK_LOCATION stack;
    NTSTATUS status;

    stack = IoGetCurrentIrpStackLocation(irp);
    XHCI_DBG_VALUE("hcd: power minor, type, state",
                   ((ULONG)stack->MinorFunction << 16) |
                       ((ULONG)stack->Parameters.Power.Type << 8) |
                       (ULONG)stack->Parameters.Power.State.DeviceState);

    if (!HcdIoEnter(hc)) {
        PoStartNextPowerIrp(irp);
        return HcdCompleteIrp(irp, STATUS_DELETE_PENDING, 0);
    }

    if (stack->MinorFunction == IRP_MN_SET_POWER &&
        stack->Parameters.Power.Type == DevicePowerState &&
        stack->Parameters.Power.State.DeviceState == PowerDeviceD0) {
        /* The count is held until hcdD0Finish completes the IRP. */
        IoMarkIrpPending(irp);
        IoCopyCurrentIrpStackLocationToNext(irp);
        HCD_SET_COMPLETION_ALWAYS(irp, hcdD0Done, hc);
        (VOID)PoCallDriver(hc->LowerDevice, irp);
        return STATUS_PENDING;
    }

    if (stack->MinorFunction == IRP_MN_SET_POWER &&
        stack->Parameters.Power.Type == SystemPowerState) {
        /* The count is held until the system IRP completes. */
        IoMarkIrpPending(irp);
        IoCopyCurrentIrpStackLocationToNext(irp);
        HCD_SET_COMPLETION_ALWAYS(irp, hcdSystemDone, hc);
        (VOID)PoCallDriver(hc->LowerDevice, irp);
        return STATUS_PENDING;
    }

    if (stack->MinorFunction == IRP_MN_SET_POWER) {
        HcdPowerGateEnter(hc);
        if (hc->ControllerStarted &&
            hc->Common.DevicePower == PowerDeviceD0 && !hc->SuspendedInD0) {
            XhciSuspendController(&hc->Hc);
        }
        /* Task 34.3: the shutdown's D3 hands the connectors back to EHCI,
         * as Linux's xhci_shutdown does on these chipsets; a sleep keeps
         * them, and its D0 routes them again. */
        if (hc->Common.SystemPower == PowerSystemShutdown) {
            HcdPswRelease(hc);
        }
        /* Suspended either way now, and the D0 that follows resumes it. */
        hc->SuspendedInD0 = 0;
        hc->Common.DevicePower = stack->Parameters.Power.State.DeviceState;
        HcdPowerGateLeave(hc);
        (VOID)PoSetPowerState(hc->Common.Self, DevicePowerState,
                              stack->Parameters.Power.State);
        irp->IoStatus.Status = STATUS_SUCCESS;
    } else if (stack->MinorFunction == IRP_MN_QUERY_POWER) {
        irp->IoStatus.Status = STATUS_SUCCESS;
    }

    PoStartNextPowerIrp(irp);
    IoSkipCurrentIrpStackLocation(irp);
    status = PoCallDriver(hc->LowerDevice, irp);
    HcdIoLeave(hc);
    return status;
}
