/*
 * hcd_power.c - power for the controller FDO of xhci98.sys (roadmap-hcd.md
 * task 26-A.2; win98-wdm.md, "Power Management").
 *
 * Every power IRP is passed down with PoStartNextPowerIrp first and
 * PoCallDriver, never IoCallDriver: the two are the same on Windows 98 and
 * not on Windows 2000. PoSetPowerState's return is never used - Windows 98
 * returns its argument.
 *
 * IRQL: PASSIVE_LEVEL; the FDO is DO_POWER_PAGABLE.
 */

#include "hcd.h"

NTSTATUS HcdControllerPower(PHCD_CONTROLLER hc, PIRP irp)
{
    PIO_STACK_LOCATION stack;
    NTSTATUS status;

    stack = IoGetCurrentIrpStackLocation(irp);

    if (!HcdIoEnter(hc)) {
        PoStartNextPowerIrp(irp);
        return HcdCompleteIrp(irp, STATUS_DELETE_PENDING, 0);
    }

    if (stack->MinorFunction == IRP_MN_SET_POWER) {
        if (stack->Parameters.Power.Type == SystemPowerState) {
            hc->Common.SystemPower = stack->Parameters.Power.State.SystemState;
        } else {
            hc->Common.DevicePower = stack->Parameters.Power.State.DeviceState;
            (VOID)PoSetPowerState(hc->Common.Self, DevicePowerState,
                                  stack->Parameters.Power.State);
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
