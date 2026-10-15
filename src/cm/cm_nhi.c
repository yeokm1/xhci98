/*
 * cm_nhi.c - the NHI's function driver (record 21 section 3.2, role 1):
 * PnP, the start's selection, the bind, the idle poll's actions and the
 * stop (task 39-A.4). The thread is cm_thr.c's, power cm_power.c's.
 *
 * THE START. IRP_MN_START_DEVICE goes down first; then, at PASSIVE_LEVEL
 * in the PnP path, the registry values are read once, BAR0 is found in the
 * translated resources (none, or one smaller than the registers this
 * driver reads: the Code 10 refusal of section 1.4.2, the one failure a
 * start reports), the thread is created and the selection queued to it
 * and waited for. On the thread (CmNhiStartWork), the four tests of
 * section 1.4.2 in order:
 *
 *   (a) the candidate: the NHI's ids read from configuration space on
 *       every target (Windows 98 gives AddDevice none), TbtHwMatch over
 *       tbt_hw.c's table (XhciCmMode 2 adds an unlisted Intel id as an
 *       Alpine Ridge ICM), and CmStartDecision for what the mode and the
 *       build already decide;
 *   (b) platform readiness: BAR0 mapped, then TbtHwPlatformInit (the
 *       integrated family's force power, firmware-ready poll and LTR);
 *   (c) and (d): TbtHwSelect - ownership by the row's own predicate, the
 *       host interface version.
 *
 * ICM: the common buffer (cm_pool.c), the Bus Master bit, the client
 * initialised with XhciCmApprove (decision D4) and the two timeouts, the
 * re-enumeration seam started; the PnP path is released, and the bind
 * (TbtIcmStart: ring 0, mailbox 0x23 where the row sends it, driver ready,
 * the host-router poll) runs on the thread after it, so a firmware that
 * answers slowly holds no PnP request. Safe mode, the USB4 software
 * connection manager (Phase 40, not in this build) and every INERT
 * outcome: the inert start - the devnode started, the reason logged and
 * counted, nothing written to the hardware (the integrated family's own
 * power-up of test (b) excepted, which the power record of section 1.4.2
 * owns). Never a failed devnode for any of them.
 *
 * THE STOP (stop, surprise removal, removal; section 5.6's unbind, in its
 * order): the re-enumeration seam stopped, then the client's unbind - ring
 * 0 stopped, its queued actions retired, mailbox 0x07 only where driver
 * ready was sent - the common buffer freed after the rings, the Bus Master
 * bit and the integrated family's force power restored where this driver
 * set them, BAR0 unmapped, then the thread ended. An inert instance has
 * nothing to undo: it never clears force power, even after its own
 * readiness timeout (a timeout is not evidence that nothing depends on the
 * controller; orchestrator decision of 2026-10-15, owner to confirm). Tunnels and
 * approvals survive every stop. The device table and the counters go to
 * the log, which is emitted at each stop (decision D12: debug and qemu
 * only).
 *
 * IRQL: PASSIVE_LEVEL throughout - PnP IRPs on a DO_POWER_PAGABLE object,
 * and the *Work, Bind, PollPass and DrainActions functions on the
 * connection-manager thread.
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
    nhi->DevicePower = PowerDeviceD0;
    nhi->SystemPower = PowerSystemWorking;
    CmValuesDefault(&nhi->Values);
    CmLogInit(nhi);
    CmThrInit(nhi);
    nhi->Common.Lower = IoAttachDeviceToDeviceStack(self, pdo);
    if (nhi->Common.Lower == NULL) {
        IoDeleteDevice(self);
        return STATUS_NO_SUCH_DEVICE;
    }
    self->Flags |= DO_POWER_PAGABLE;
    self->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

/* ---------------------------------------------------------------------- */
/* The log's views of the client                                          */
/* ---------------------------------------------------------------------- */

/* Labels in the order of each structure's ULONG fields. */
static const char *const cmIcmCounterNames[] = {
    "cm.icm.commands", "cm.icm.responses", "cm.icm.timeouts",
    "cm.icm.retries", "cm.icm.error-flags", "cm.icm.strays",
    "cm.icm.ev.connected", "cm.icm.ev.disconnected", "cm.icm.ev.ignored",
    "cm.icm.ev.unknown", "cm.icm.ev.malformed", "cm.icm.ev.queued",
    "cm.icm.queue-overflows", "cm.icm.action-overflows", "cm.icm.table-full",
    "cm.icm.rejected", "cm.icm.refreshes", "cm.icm.disconnect-unknown",
    "cm.icm.route-failures", "cm.icm.approve.sent", "cm.icm.approve.ok",
    "cm.icm.approve.failed", "cm.icm.approve.abandoned",
    "cm.icm.approve.withheld", "cm.icm.approve.unsupported",
    "cm.icm.approve.unknown", "cm.icm.tunnel-ups", "cm.icm.tunnel-downs",
    "cm.icm.reconciled", "cm.icm.mailbox.sent", "cm.icm.mailbox.ok",
    "cm.icm.mailbox.errors", "cm.icm.mailbox.timeouts",
    "cm.icm.hostpoll.tries", "cm.icm.hostpoll.answered", "cm.icm.idle-polls",
    "cm.icm.rediscoveries"
};

static const char *const cmRingCounterNames[] = {
    "cm.ring.tx", "cm.ring.rx", "cm.ring.rx-usable", "cm.ring.crc-errors",
    "cm.ring.overflows", "cm.ring.malformed", "cm.ring.tx-full",
    "cm.ring.tx-stuck", "cm.ring.ctl.reads", "cm.ring.ctl.retries",
    "cm.ring.ctl.timeouts", "cm.ring.ctl.notify", "cm.ring.ctl.stale",
    "cm.ring.unsolicited", "cm.ring.ctl.writes", "cm.ring.ring0-was-enabled",
    "cm.ring.other-rings-enabled"
};

static const char *const cmNhiCounterNames[] = {
    "cm.passes", "cm.polls", "cm.actions.up", "cm.actions.down",
    "cm.actions.resync", "cm.actions.not-live", "cm.reenum.skipped",
    "cm.config.failures", "cm.suspends", "cm.resumes", "cm.resume.failures",
    "cm.shutdowns", "cm.power.request-failures", "cm.power.inline",
    "cm.buffers.kept", "cm.power.wakes-unpowered"
};

XHCI_C_ASSERT(cm_icm_counter_names,
              sizeof(cmIcmCounterNames) / sizeof(cmIcmCounterNames[0]) ==
                  sizeof(TBT_ICM_COUNTERS) / sizeof(ULONG));
XHCI_C_ASSERT(cm_ring_counter_names,
              sizeof(cmRingCounterNames) / sizeof(cmRingCounterNames[0]) ==
                  sizeof(TBT_RING_COUNTERS) / sizeof(ULONG));
XHCI_C_ASSERT(cm_nhi_counter_names,
              sizeof(cmNhiCounterNames) / sizeof(cmNhiCounterNames[0]) ==
                  sizeof(CM_COUNTERS) / sizeof(ULONG));

static VOID cmLogBlock(PCM_NHI nhi, const char *const *names,
                       const ULONG *values, ULONG count)
{
    ULONG i;

    for (i = 0; i < count; i++) {
        CmLogNote(nhi, names[i], values[i]);
    }
}

/* The device table (record 21 section 5.3), one line set per entry: the
 * snapshot of section 8 this first cut gives, through the log. */
static VOID cmLogTable(PCM_NHI nhi)
{
    const TBT_ICM_DEVICE *d;
    ULONG slot;

    if (nhi->Icm.Ring == NULL) {
        return;
    }
    CmLogNote(nhi, "cm.dev.count", TbtIcmDeviceCount(&nhi->Icm));
    for (slot = 0; slot < TBT_ICM_MAX_DEVICES; slot++) {
        d = TbtIcmDevice(&nhi->Icm, slot);
        if (d == NULL) {
            continue;
        }
        CmLogNote(nhi, "cm.dev.slot", slot);
        CmLogNote(nhi, "cm.dev.uuid0", d->Uuid[0]);
        CmLogNote(nhi, "cm.dev.uuid1", d->Uuid[1]);
        CmLogNote(nhi, "cm.dev.state",
                  (d->Approved ? 0x01UL : 0) | (d->Boot ? 0x02UL : 0) |
                      (d->ApproveUnknown ? 0x04UL : 0) |
                      (d->ApproveFailed ? 0x08UL : 0) |
                      (d->RouteValid ? 0x10UL : 0) |
                      ((d->Link & 0xFUL) << 8) | ((d->Depth & 0xFUL) << 12));
        CmLogNote(nhi, "cm.dev.route.lo", d->Route.Lo);
    }
}

static VOID cmLogCounters(PCM_NHI nhi)
{
    cmLogBlock(nhi, cmNhiCounterNames, (const ULONG *)&nhi->Counters,
               sizeof(CM_COUNTERS) / sizeof(ULONG));
    if (nhi->Icm.Ring != NULL) {
        cmLogBlock(nhi, cmRingCounterNames,
                   (const ULONG *)&nhi->Ring.Counters,
                   sizeof(TBT_RING_COUNTERS) / sizeof(ULONG));
        cmLogBlock(nhi, cmIcmCounterNames,
                   (const ULONG *)&nhi->Icm.Counters,
                   sizeof(TBT_ICM_COUNTERS) / sizeof(ULONG));
    }
}

/* ---------------------------------------------------------------------- */
/* The actions                                                            */
/* ---------------------------------------------------------------------- */

/* Every queued tunnel change, oldest first, to the re-enumeration seam
 * (cm_reenum.h), unless XhciCmReenum is 0. */
VOID CmNhiDrainActions(PCM_NHI nhi)
{
    TBT_ICM_ACTION a;

    if (nhi->Icm.Ring == NULL) {
        return;
    }
    while (TbtIcmNextAction(&nhi->Icm, &a)) {
        if (a.Kind == TBT_ICM_ACT_TUNNEL_UP) {
            nhi->Counters.ActionsUp++;
            CmLogNote(nhi, "cm.action.tunnel-up", a.Why);
        } else if (a.Kind == TBT_ICM_ACT_TUNNEL_DOWN) {
            nhi->Counters.ActionsDown++;
            CmLogNote(nhi, "cm.action.tunnel-down", a.Why);
        } else {
            nhi->Counters.ActionsResync++;
            CmLogNote(nhi, "cm.action.resync", a.Why);
        }
        if (a.Kind != TBT_ICM_ACT_RESYNC) {
            CmLogNote(nhi, "cm.action.uuid0", a.Uuid[0]);
            CmLogNote(nhi, "cm.action.place",
                      ((a.Link & 0xFUL) << 8) | ((a.Depth & 0xFUL) << 12) |
                          (a.WasApproved ? 1UL : 0) | (a.Boot ? 2UL : 0));
            if (!TbtIcmActionLive(&nhi->Icm, &a)) {
                nhi->Counters.ActionsNotLive++;
            }
        }
        /* Every action reaches the seam, XhciCmReenum 0 included: it
         * voids safe eject's pending ejects on any tunnel change, and
         * makes no re-enumeration call itself while the value is 0
         * (cm_reenum.h). */
        if (nhi->Values.Value[CM_VAL_REENUM] == 0) {
            nhi->Counters.ReenumSkipped++;
        }
        CmReenumAction(nhi, &a);
    }
}

VOID CmNhiPollPass(PCM_NHI nhi)
{
    TbtIcmPoll(&nhi->Icm);
    CmNhiDrainActions(nhi);
}

/* ---------------------------------------------------------------------- */
/* The start                                                              */
/* ---------------------------------------------------------------------- */

/* The inert start, before anything is activated. A domain the
 * re-enumeration seam was already started for withdraws its claims at
 * once (CmReenumStop writes nothing to the NHI), so an instance that never
 * bound holds no tunnel port. */
static VOID cmInert(PCM_NHI nhi, ULONG reason)
{
    nhi->Inert = reason;
    nhi->Stage = CM_STAGE_INERT;
    CmLogNote(nhi, "cm.inert", reason);
    if (nhi->ReenumOn) {
        CmReenumStop(nhi);
        nhi->ReenumOn = 0;
    }
}

/* Which CM_PROFILE_* a hardware row is, for CmStartDecision. */
static ULONG cmProfileOfHw(const TBT_HW_PROFILE *hw)
{
    if (hw == NULL) {
        return CM_PROFILE_UNKNOWN;
    }
    if (hw->Icm == &TbtIcmProfileFalconRidge) {
        return CM_PROFILE_ICM_FR;
    }
    if (hw->Icm == &TbtIcmProfileAlpineRidge) {
        return CM_PROFILE_ICM_AR;
    }
    /* Maple Ridge and Tiger Lake to Meteor Lake: the Titan Ridge and Ice
     * Lake messages under the NVM-authentication predicate. */
    if (hw->Icm == &TbtIcmProfileTitanRidge) {
        return hw->Ownership == TBT_HW_OWN_AUTH_DONE ? CM_PROFILE_ICM_MR
                                                     : CM_PROFILE_ICM_TR;
    }
    if (hw->Icm == &TbtIcmProfileIceLake) {
        return hw->Ownership == TBT_HW_OWN_AUTH_DONE ? CM_PROFILE_ICM_TGL
                                                     : CM_PROFILE_ICM_ICL;
    }
    return CM_PROFILE_USB4;
}

/* The selection, on the thread. Sets StartStatus (a Code 10 refusal only
 * for BAR0 that cannot be mapped) and BindOwed. */
VOID CmNhiStartWork(PCM_NHI nhi)
{
    ULONG mode;
    ULONG v;
    ULONG profile;
    BOOLEAN ready;

    nhi->StartStatus = STATUS_SUCCESS;
    nhi->BindOwed = 0;
    mode = nhi->Values.Value[CM_VAL_MODE];
    if (mode == CM_MODE_OFF) {
        cmInert(nhi, CM_INERT_MODE_OFF);
        return;
    }

    /* (a) */
    v = CmCfgRead32(nhi, 0x00);
    if (v == 0xFFFFFFFFUL || (v & 0xFFFFUL) == 0xFFFFUL || v == 0) {
        cmInert(nhi, CM_INERT_CONFIG);
        return;
    }
    nhi->Vendor = v & 0xFFFFUL;
    nhi->Device = (v >> 16) & 0xFFFFUL;
    CmLogNote(nhi, "cm.cfg.ids", v);
    /* The class and the subsystem ids, each read whole: a read refused or
     * all ones decides nothing, and the Apple policy of test (a) needs the
     * subsystem vendor established, not assumed. */
    v = CmCfgRead32(nhi, 0x08);
    if (v == 0xFFFFFFFFUL) {
        cmInert(nhi, CM_INERT_CONFIG);
        return;
    }
    nhi->ClassCode = (v >> 8) & 0xFFFFFFUL;
    v = CmCfgRead32(nhi, 0x2C);
    if (v == 0xFFFFFFFFUL) {
        cmInert(nhi, CM_INERT_CONFIG);
        return;
    }
    nhi->SubVendor = v & 0xFFFFUL;
    CmLogNote(nhi, "cm.cfg.class", nhi->ClassCode);
    CmLogNote(nhi, "cm.cfg.subsystem", v);
    nhi->Hw = TbtHwMatch(nhi->Vendor, nhi->Device, nhi->ClassCode,
                         (BOOLEAN)(mode == CM_MODE_FORCE_ICM));
    profile = cmProfileOfHw(nhi->Hw);
    CmLogNote(nhi, "cm.hw.profile", profile);
    CmLogNote(nhi, "cm.hw.flags", nhi->Hw != NULL ? nhi->Hw->Flags : 0);
    v = CmStartDecision(mode, profile,
                        nhi->Vendor == TBT_HW_VENDOR_INTEL ? 1UL : 0UL,
                        CM_PROFILES_BUILT);
    if (v != CM_INERT_NONE) {
        cmInert(nhi, v);
        return;
    }

    /* (b) Nothing is activated out of D0: a D3 queued beside the start
     * runs after it and quiesces what it made (cm_thr.c). */
    if (nhi->DevicePower != PowerDeviceD0 || nhi->Suspended ||
        CmPowerSleeping(nhi)) {
        cmInert(nhi, CM_INERT_POWER);
        return;
    }
    nhi->BarVa = (PUCHAR)MmMapIoSpace(nhi->BarPhys, nhi->BarLength,
                                      MmNonCached);
    if (nhi->BarVa == NULL) {
        CmLogNote(nhi, "cm.bar.unmapped", nhi->BarLength);
        nhi->StartStatus = STATUS_INSUFFICIENT_RESOURCES;
        return;
    }
    ready = CmNhiPlatformInit(nhi);
    CmLogNote(nhi, "cm.platform.ready", ready ? 1UL : 0UL);

    /* (c), (d) */
    TbtHwSelect(nhi->Hw, &nhi->Host, ready, TbtHwIsApple(nhi->SubVendor),
                (BOOLEAN)(mode == CM_MODE_FORCE_SW), &nhi->Sel);
    CmLogNote(nhi, "cm.select.outcome", nhi->Sel.Outcome);
    CmLogNote(nhi, "cm.select.reason", nhi->Sel.Reason);
    CmLogNote(nhi, "cm.select.caps", nhi->Sel.Caps);
    CmLogNote(nhi, "cm.select.fw-status", nhi->Sel.FwStatus);
    CmLogNote(nhi, "cm.select.mode", nhi->Sel.Mode);
    if (nhi->Sel.Outcome == TBT_SEL_SAFE_MODE) {
        cmInert(nhi, CM_INERT_SAFE_MODE);
        return;
    }
    if (nhi->Sel.Outcome == TBT_SEL_SOFTWARE) {
        cmInert(nhi, CM_INERT_SOFTWARE_CM);
        return;
    }
    if (nhi->Sel.Outcome != TBT_SEL_ICM) {
        cmInert(nhi, CM_INERT_SELECTION);
        return;
    }

    if (!NT_SUCCESS(CmDmaOpen(nhi))) {
        cmInert(nhi, CM_INERT_RESOURCES);
        return;
    }
    if (!CmNhiSetBusMaster(nhi, TRUE)) {
        /* No ring may be started without it; the buffer and a bit set
         * here are given back at the stop. */
        cmInert(nhi, CM_INERT_BUS_MASTER);
        return;
    }
    TbtIcmInit(&nhi->Icm, nhi->Hw->Icm, &nhi->Ring, &nhi->Host,
               TbtHwRingFlags(nhi->Hw));
    TbtIcmSetApprovePolicy(&nhi->Icm,
                           (BOOLEAN)(nhi->Values.Value[CM_VAL_APPROVE] != 0));
    TbtIcmSetTimeouts(&nhi->Icm, nhi->Values.Value[CM_VAL_CMD_TIMEOUT_MS],
                      nhi->Values.Value[CM_VAL_APPROVE_TIMEOUT_MS]);
    CmLogNote(nhi, "cm.icm.row", nhi->Hw->Icm->Generation);
    CmReenumStart(nhi);
    nhi->ReenumOn = 1;
    nhi->Inert = CM_INERT_NONE;
    nhi->Stage = CM_STAGE_ACTIVE;
    nhi->BindOwed = 1;
}

/* The bind, on the thread after the PnP path was released. */
VOID CmNhiBind(PCM_NHI nhi)
{
    LONG rc;

    if (nhi->DevicePower != PowerDeviceD0 || nhi->Suspended ||
        CmPowerSleeping(nhi)) {
        /* The sleep or shutdown was taken while the selection ran: the
         * rings are never started; the stop gives the buffer back. */
        cmInert(nhi, CM_INERT_POWER);
        return;
    }
    rc = TbtIcmStart(&nhi->Icm, nhi->CommonVa, nhi->CommonPa.LowPart,
                     TBT_RING_DMA_BYTES);
    CmLogNote(nhi, "cm.bind", (ULONG)(-rc));
    if (rc == TBT_OK) {
        CmLogNote(nhi, "cm.driver-ready", nhi->Icm.Ready.Flags);
        CmLogNote(nhi, "cm.dev.count", TbtIcmDeviceCount(&nhi->Icm));
        CmNhiDrainActions(nhi);
        return;
    }
    if (nhi->Icm.Failed) {
        /* Failure after activation (section 1.4.2): the client stopped
         * its rings; the stop sends 0x07 only if driver ready went out. */
        nhi->Stage = CM_STAGE_FAILED;
        CmLogNote(nhi, "cm.failed", (ULONG)(-rc));
        return;
    }
    /* Refused before a ring register was written: nothing to undo. */
    cmInert(nhi, CM_INERT_RING_REFUSED);
}

/* ---------------------------------------------------------------------- */
/* The stop                                                               */
/* ---------------------------------------------------------------------- */

VOID CmNhiStopWork(PCM_NHI nhi)
{
    BOOLEAN active;
    BOOLEAN powered;

    active = (BOOLEAN)(nhi->Stage == CM_STAGE_ACTIVE ||
                       nhi->Stage == CM_STAGE_FAILED);
    /* The bus's state, not the client's: a client suspended while the
     * function stayed in D0 (cm_power.c's direct transition) is still
     * unbound and its ownership still discharged. */
    powered = (BOOLEAN)(nhi->DevicePower == PowerDeviceD0);
    if (nhi->ReenumOn) {
        CmReenumStop(nhi);
        nhi->ReenumOn = 0;
    }
    if (nhi->Icm.Ring != NULL) {
        if (powered) {
            TbtIcmUnbind(&nhi->Icm);
        } else {
            /* Out of D0 the rings were stopped by the suspend's sequence
             * and nothing is sent to a function the bus has powered down. */
            CmLogNote(nhi, "cm.unbind.unpowered", nhi->DevicePower);
        }
        CmNhiDrainActions(nhi);
    }
    cmLogTable(nhi);
    cmLogCounters(nhi);
    CmDmaClose(nhi);
    if (powered && active) {
        (VOID)CmNhiSetBusMaster(nhi, FALSE);
        CmNhiPlatformRelease(nhi);
    } else if (nhi->BusMasterSetHere) {
        /* An inert stop writes nothing: a bit set before the start went
         * inert (its verification, or the ring's refusal) stays set. */
        CmLogNote(nhi, "cm.bus-master.kept", nhi->Stage);
    }
    if (nhi->BarVa != NULL) {
        MmUnmapIoSpace(nhi->BarVa, nhi->BarLength);
        nhi->BarVa = NULL;
    }
    nhi->Stage = CM_STAGE_STOPPED;
}

/* BAR0 from the translated resources: the first memory range. */
static VOID cmFindBar(PCM_NHI nhi, PIRP irp)
{
    PIO_STACK_LOCATION stack;
    PCM_RESOURCE_LIST translated;
    PCM_PARTIAL_RESOURCE_LIST list;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR d;
    ULONG i;

    nhi->HaveBar = 0;
    nhi->BarLength = 0;
    stack = IoGetCurrentIrpStackLocation(irp);
    translated = stack->Parameters.StartDevice.AllocatedResourcesTranslated;
    if (translated == NULL || translated->Count == 0) {
        return;
    }
    list = &translated->List[0].PartialResourceList;
    for (i = 0; i < list->Count; i++) {
        d = &list->PartialDescriptors[i];
        if (d->Type == CmResourceTypeMemory) {
            nhi->BarPhys = d->u.Memory.Start;
            nhi->BarLength = d->u.Memory.Length;
            nhi->HaveBar = 1;
            return;
        }
    }
}

/* The start, after the bus has started the PDO. */
static NTSTATUS cmNhiStart(PCM_NHI nhi, PIRP irp)
{
    NTSTATUS status;
    POWER_STATE d0;

    CmReadValues(nhi);
    CmLogApply(nhi);
    nhi->Starts++;
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
    CmLogNote(nhi, "cm.safe.eject", nhi->Values.Value[CM_VAL_SAFE_EJECT]);
    CmLogNote(nhi, "cm.bridge.override", nhi->Values.TunnelBridgeChars);
    CmLogNote(nhi, "cm.bridge.refused", nhi->Values.TunnelBridgeRefused);

    cmFindBar(nhi, irp);
    CmLogNote(nhi, "cm.bar.length", nhi->BarLength);
    if (!nhi->HaveBar || nhi->BarLength < CM_BAR0_MIN_BYTES) {
        /* Section 1.4.2: BAR0 not delivered is the one Code 10. */
        CmLogNote(nhi, "cm.bar.refused", nhi->BarLength);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    /* A fresh client per start: nothing of a previous one is reused. */
    CmZero(&nhi->Icm, sizeof(nhi->Icm));
    CmZero(&nhi->Ring, sizeof(nhi->Ring));
    CmZero(&nhi->Sel, sizeof(nhi->Sel));
    CmZero(&nhi->Counters, sizeof(nhi->Counters));
    nhi->Hw = NULL;
    nhi->Inert = CM_INERT_NONE;
    nhi->Stage = CM_STAGE_INERT;
    nhi->Suspended = 0;
    /* The state the power path recorded stands: a D3 taken before the
     * queue opened was published under WorkLock (CmThrQueueOrRecord), and
     * the selection activates nothing out of D0. */
    if (nhi->DevicePower == PowerDeviceD0) {
        d0.DeviceState = PowerDeviceD0;
        (VOID)PoSetPowerState(nhi->Common.Self, DevicePowerState, d0);
    }
    CmThrAccessors(nhi);
    nhi->Started = 1;

    status = CmThrStart(nhi);
    if (!NT_SUCCESS(status)) {
        /* No thread, no client: started and inert. */
        cmInert(nhi, CM_INERT_RESOURCES);
        return STATUS_SUCCESS;
    }
    CmThrRunPnp(nhi, CM_WORK_START);
    if (!NT_SUCCESS(nhi->StartStatus)) {
        CmThrRunPnp(nhi, CM_WORK_STOP);
        CmThrStop(nhi);
        nhi->Started = 0;
        return nhi->StartStatus;
    }
    return STATUS_SUCCESS;
}

static VOID cmNhiStop(PCM_NHI nhi, ULONG reason)
{
    if (!nhi->Started) {
        return;
    }
    if (nhi->ThreadRunning) {
        CmThrRunPnp(nhi, CM_WORK_STOP);
        CmThrStop(nhi);
    }
    nhi->Stage = CM_STAGE_STOPPED;
    nhi->Started = 0;
    nhi->Stops++;
    CmLogNote(nhi, "cm.stop", reason);
    CmLogEmit(nhi, XHCI_LOG_REASON_STOP);
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
            status = cmNhiStart(nhi, irp);
        }
        return CmCompleteIrp(irp, status, 0);

    case IRP_MN_QUERY_STOP_DEVICE:
    case IRP_MN_QUERY_REMOVE_DEVICE:
    case IRP_MN_CANCEL_STOP_DEVICE:
    case IRP_MN_CANCEL_REMOVE_DEVICE:
        irp->IoStatus.Status = STATUS_SUCCESS;
        break;

    case IRP_MN_STOP_DEVICE:
    case IRP_MN_SURPRISE_REMOVAL:
        cmNhiStop(nhi, stack->MinorFunction);
        irp->IoStatus.Status = STATUS_SUCCESS;
        break;

    case IRP_MN_REMOVE_DEVICE:
        cmNhiStop(nhi, stack->MinorFunction);
        /* Power admission closed, and every power IRP this driver still
         * holds finished (without the thread, cm_power.c): none may outlive
         * the device object. */
        CmPowerDrain(nhi);
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
