/*
 * hcd_power.c - power for the controller FDO of xhci98.sys (roadmap-hcd.md
 * task 26-A.2; win98-wdm.md, "Power Management").
 *
 * The FDO is the device's power policy owner. A system SET_POWER is mapped
 * to a device state - S0 to D0, any sleep state to D3 - and requested with
 * PoRequestPowerIrp before the system IRP goes down; the device SET_POWER
 * that results does the work: XhciSuspendController before a Dx IRP goes
 * down, XhciResumeController after a D0 IRP has come back up. Both are the
 * miniport's own, kept in xhci_init.c, PASSIVE_LEVEL.
 *
 * The Windows 98 rules of win98-wdm.md, each kept here:
 *   - PoStartNextPowerIrp before a power IRP is completed or passed down;
 *   - PoCallDriver, never IoCallDriver, for a power IRP;
 *   - PoSetPowerState's return is never used (Windows 98 returns its
 *     argument);
 *   - no PoRequestPowerIrp for the state the device is already in (on
 *     Windows 98 it returns STATUS_PENDING and never delivers the IRP).
 *
 * The system IRP does not wait for the device IRP it requested. That is a
 * simplification against the WDM sample's pend-and-complete chain, taken
 * because neither primary target's test VM sleeps (Windows 2000's runs
 * acpi=off) and Windows 98 sends power IRPs with no ordering guarantee to
 * lean on; the suspend and resume themselves are the kept sequence.
 *
 * IRQL: PASSIVE_LEVEL; the FDO is DO_POWER_PAGABLE.
 */

#include "hcd.h"
#include "xhci_dbg.h"
#include "xhci_hw.h"

static NTSTATUS NTAPI hcdPowerSignal(PDEVICE_OBJECT DeviceObject, PIRP Irp,
                                     PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);

    (VOID)KeSetEvent((PKEVENT)Context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

/* A D0 IRP goes down first and the controller resumes after it is back. */
static NTSTATUS hcdPowerUp(PHCD_CONTROLLER hc, PIRP irp,
                           PIO_STACK_LOCATION stack)
{
    KEVENT done;
    NTSTATUS status;

    KeInitializeEvent(&done, NotificationEvent, FALSE);
    IoCopyCurrentIrpStackLocationToNext(irp);
    HCD_SET_COMPLETION_ALWAYS(irp, hcdPowerSignal, &done);
    status = PoCallDriver(hc->LowerDevice, irp);
    if (status == STATUS_PENDING) {
        (VOID)KeWaitForSingleObject(&done, Executive, KernelMode, FALSE, NULL);
    }
    status = irp->IoStatus.Status;

    if (NT_SUCCESS(status)) {
        if (hc->ControllerStarted &&
            hc->Common.DevicePower != PowerDeviceD0) {
            if (XhciResumeController(&hc->Hc) != MP_STATUS_SUCCESS) {
                hc->ResumeFailures++;
            }
        }
        hc->Common.DevicePower = PowerDeviceD0;
        (VOID)PoSetPowerState(hc->Common.Self, DevicePowerState,
                              stack->Parameters.Power.State);
    }
    PoStartNextPowerIrp(irp);
    return HcdCompleteIrp(irp, status, irp->IoStatus.Information);
}

static VOID hcdRequestDevicePower(PHCD_CONTROLLER hc, DEVICE_POWER_STATE want)
{
    POWER_STATE state;

    if (want == hc->Common.DevicePower) {
        return;
    }
    state.DeviceState = want;
    (VOID)PoRequestPowerIrp(hc->Pdo, IRP_MN_SET_POWER, state, NULL, NULL,
                            NULL);
}

NTSTATUS HcdControllerPower(PHCD_CONTROLLER hc, PIRP irp)
{
    PIO_STACK_LOCATION stack;
    DEVICE_POWER_STATE device;
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
        stack->Parameters.Power.Type == DevicePowerState) {
        device = stack->Parameters.Power.State.DeviceState;
        if (device == PowerDeviceD0) {
            status = hcdPowerUp(hc, irp, stack);
            HcdIoLeave(hc);
            return status;
        }
        if (hc->ControllerStarted &&
            hc->Common.DevicePower == PowerDeviceD0) {
            XhciSuspendController(&hc->Hc);
        }
        hc->Common.DevicePower = device;
        (VOID)PoSetPowerState(hc->Common.Self, DevicePowerState,
                              stack->Parameters.Power.State);
        irp->IoStatus.Status = STATUS_SUCCESS;
    } else if (stack->MinorFunction == IRP_MN_SET_POWER) {
        hc->Common.SystemPower = stack->Parameters.Power.State.SystemState;
        if (hc->ControllerStarted) {
            hcdRequestDevicePower(hc,
                                  (hc->Common.SystemPower == PowerSystemWorking)
                                      ? PowerDeviceD0
                                      : PowerDeviceD3);
        }
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
