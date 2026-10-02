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
 * findings 5 and 6). An IRP that is pended keeps the outstanding-I/O count
 * until it is completed, so a remove waits for it (finding 4).
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
 * Neither primary's test guest sleeps (Windows 2000's runs acpi=off), so the
 * S-to-D path has not run on a guest.
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

/* The resume half of a D0, then the IRP's completion. IRQL: PASSIVE_LEVEL. */
static VOID hcdD0Finish(PHCD_CONTROLLER hc, PIRP irp)
{
    PIO_STACK_LOCATION stack;

    stack = IoGetCurrentIrpStackLocation(irp);
    if (hc->ControllerStarted && hc->Common.DevicePower != PowerDeviceD0) {
        if (XhciResumeController(&hc->Hc) != MP_STATUS_SUCCESS) {
            hc->ResumeFailures++;
            HcdControllerFail(hc);
        }
    }
    hc->Common.DevicePower = PowerDeviceD0;
    (VOID)PoSetPowerState(hc->Common.Self, DevicePowerState,
                          stack->Parameters.Power.State);
    PoStartNextPowerIrp(irp);
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    HcdIoLeave(hc);
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
    } else {
        PoStartNextPowerIrp(irp);
        IoCompleteRequest(irp, IO_NO_INCREMENT);
        HcdIoLeave(hc);
    }
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
        hc->PendingSystemIrp = NULL;
        hc->PowerRequestFailures++;
        PoStartNextPowerIrp(Irp);
        HcdIoLeave(hc);
        return STATUS_SUCCESS;
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
        if (hc->ControllerStarted &&
            hc->Common.DevicePower == PowerDeviceD0) {
            XhciSuspendController(&hc->Hc);
        }
        hc->Common.DevicePower = stack->Parameters.Power.State.DeviceState;
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
