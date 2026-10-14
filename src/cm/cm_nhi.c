/*
 * cm_nhi.c - the NHI's function driver (record 21 section 3.2, role 1),
 * as the scaffold of task 39-A.1 leaves it: the FDO attaches, its start
 * reads the registry values once and records them, and nothing touches
 * the hardware - no BAR is mapped, no configuration space is read or
 * written, no ring exists. Task 39-A.4 brings the hardware start, the
 * connection-manager thread and the power policy owner's sequence of
 * section 4.4; until then power IRPs pass through as a filter's do.
 *
 * Every start of the scaffold is the INERT start of cm_vals.h - the
 * devnode started, the reason logged (XhciCmMode 0, decision D11; a
 * profile unknown; or, for every profile until task 39-A.4, a client not
 * built), no hardware write - and never a failed devnode.
 *
 * IRQL: PASSIVE_LEVEL throughout (PnP and power IRPs on a
 * DO_POWER_PAGABLE object).
 */

#include "cm.h"

NTSTATUS CmNhiAddDevice(PDRIVER_OBJECT driver, PDEVICE_OBJECT pdo,
                        const CM_IDS *ids)
{
    PDEVICE_OBJECT self;
    PCM_NHI nhi;
    NTSTATUS status;

    status = IoCreateDevice(driver, sizeof(CM_NHI), NULL, FILE_DEVICE_UNKNOWN,
                            0, FALSE, &self);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    nhi = (PCM_NHI)self->DeviceExtension;
    CmZero(nhi, sizeof(CM_NHI));
    nhi->Common.Kind = CM_KIND_NHI;
    nhi->Common.Self = self;
    nhi->Common.Pdo = pdo;
    nhi->Ids = *ids;
    nhi->Profile = CmProfileFor(ids);
    CmValuesDefault(&nhi->Values);
    CmLogInit(nhi);
    nhi->Common.Lower = IoAttachDeviceToDeviceStack(self, pdo);
    if (nhi->Common.Lower == NULL) {
        IoDeleteDevice(self);
        return STATUS_NO_SUCH_DEVICE;
    }
    self->Flags |= DO_POWER_PAGABLE;
    self->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

/* The start, after the bus has started the PDO. Reads and records; never
 * fails, because nothing in it can. */
static VOID cmNhiStart(PCM_NHI nhi)
{
    CmReadValues(nhi);
    CmLogApply(nhi);
    nhi->Starts++;
    nhi->Started = 1;
    CmLogNote(nhi, "cm.start", nhi->Starts);
    CmLogNote(nhi, "cm.nhi.vendor", nhi->Ids.Vendor);
    CmLogNote(nhi, "cm.nhi.device", nhi->Ids.Device);
    CmLogNote(nhi, "cm.nhi.class", nhi->Ids.ClassCode);
    CmLogNote(nhi, "cm.profile", nhi->Profile);
    CmLogNote(nhi, "cm.values.found", nhi->Values.Found);
    CmLogNote(nhi, "cm.values.refused", nhi->Values.Refused);
    CmLogNote(nhi, "cm.values.clamped", nhi->Values.Clamped);
    CmLogNote(nhi, "cm.mode", nhi->Values.Value[CM_VAL_MODE]);
    CmLogNote(nhi, "cm.approve", nhi->Values.Value[CM_VAL_APPROVE]);
    CmLogNote(nhi, "cm.reenum", nhi->Values.Value[CM_VAL_REENUM]);
    CmLogNote(nhi, "cm.poll.idle.ms", nhi->Values.Value[CM_VAL_POLL_IDLE_MS]);
    CmLogNote(nhi, "cm.cmd.timeout.ms",
              nhi->Values.Value[CM_VAL_CMD_TIMEOUT_MS]);
    CmLogNote(nhi, "cm.approve.timeout.ms",
              nhi->Values.Value[CM_VAL_APPROVE_TIMEOUT_MS]);
    CmLogNote(nhi, "cm.bridge.override", nhi->Values.TunnelBridgeChars);
    CmLogNote(nhi, "cm.bridge.refused", nhi->Values.TunnelBridgeRefused);
    nhi->Inert = CmStartDecision(nhi->Values.Value[CM_VAL_MODE], nhi->Profile,
                                 nhi->Ids.Vendor == 0x8086 ? 1UL : 0UL,
                                 CM_PROFILES_BUILT);
    /* The INERT start (cm_vals.h): started, logged, nothing written to
     * the hardware. Task 39-A.4 brings the start that is not inert -
     * BAR0, the readiness, ownership and protocol tests, the thread - and
     * every reason those tests find for staying out lands here too. */
    nhi->Stage = CM_STAGE_INERT;
    CmLogNote(nhi, "cm.inert", nhi->Inert);
    CmLogNote(nhi, "cm.stage", nhi->Stage);
}

static VOID cmNhiStop(PCM_NHI nhi, ULONG reason)
{
    if (nhi->Started) {
        /* From CM_STAGE_INERT there is nothing to undo; task 39-A.4 adds
         * the ACTIVE stage's stop (rings stopped and drained before the
         * DMA memory is freed) and the PowerChanged restore. */
        nhi->Stage = CM_STAGE_STOPPED;
        nhi->Started = 0;
        nhi->Stops++;
        CmLogNote(nhi, "cm.stop", reason);
        CmLogEmit(nhi, XHCI_LOG_REASON_STOP);
    }
}

NTSTATUS CmNhiPnp(PCM_NHI nhi, PIRP irp)
{
    PIO_STACK_LOCATION stack;
    NTSTATUS status;

    stack = IoGetCurrentIrpStackLocation(irp);
    switch (stack->MinorFunction) {
    case IRP_MN_START_DEVICE:
        status = CmForwardSync(&nhi->Common, irp);
        if (NT_SUCCESS(status)) {
            cmNhiStart(nhi);
        }
        return CmCompleteIrp(irp, status, 0);

    case IRP_MN_QUERY_STOP_DEVICE:
    case IRP_MN_QUERY_REMOVE_DEVICE:
    case IRP_MN_CANCEL_STOP_DEVICE:
    case IRP_MN_CANCEL_REMOVE_DEVICE:
        irp->IoStatus.Status = STATUS_SUCCESS;
        break;

    case IRP_MN_STOP_DEVICE:
        cmNhiStop(nhi, stack->MinorFunction);
        irp->IoStatus.Status = STATUS_SUCCESS;
        break;

    case IRP_MN_SURPRISE_REMOVAL:
        cmNhiStop(nhi, stack->MinorFunction);
        irp->IoStatus.Status = STATUS_SUCCESS;
        break;

    case IRP_MN_REMOVE_DEVICE:
        cmNhiStop(nhi, stack->MinorFunction);
        irp->IoStatus.Status = STATUS_SUCCESS;
        IoSkipCurrentIrpStackLocation(irp);
        status = IoCallDriver(nhi->Common.Lower, irp);
        IoDetachDevice(nhi->Common.Lower);
        IoDeleteDevice(nhi->Common.Self);
        return status;

    default:
        break;
    }
    IoSkipCurrentIrpStackLocation(irp);
    return IoCallDriver(nhi->Common.Lower, irp);
}

/* Pass-through until task 39-A.4 makes this FDO the power policy owner
 * (record 21 section 4.4). Nothing here waits. */
NTSTATUS CmNhiPower(PCM_NHI nhi, PIRP irp)
{
    PoStartNextPowerIrp(irp);
    IoSkipCurrentIrpStackLocation(irp);
    return PoCallDriver(nhi->Common.Lower, irp);
}
