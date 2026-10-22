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
 * USB4 (TBT_SEL_SOFTWARE, design record 22 section 3.4's admission; task
 * 40-A.6): the common buffer for sixteen-entry rings, the Bus Master bit,
 * the core (tbt_u4cm.c) initialised with XhciCmApprove, XhciCmUsb3Tunnel,
 * XhciCmLaneBonding and XhciCmPreOsCleanup; the PnP path is released and
 * the bind (TbtU4Start: ring 0 taken over, never reset; the host router;
 * tunnels found at start discovered and preserved; hot plug; cold
 * devices) runs on the thread, then the re-enumeration seam starts with
 * the router-named ports offered to its discovery (CmNhiNamePorts), the
 * eject entry registered (task 40-A.9) and the learning baseline taken.
 * Each idle tick runs TbtU4Poll, drains its actions to the seam and learns
 * the port-to-bridge correspondence (tbt_topo.h); the statement upcall
 * withholds the stor98 statement (no channel yet, task 40-A.7) and ends a
 * port's learning only on a path proven closed.
 *
 * ICM: the common buffer (cm_pool.c), the Bus Master bit, the client
 * initialised with XhciCmApprove (decision D4) and the two timeouts, the
 * re-enumeration seam started; the PnP path is released, and the bind
 * (TbtIcmStart: ring 0, mailbox 0x23 where the row sends it, driver ready,
 * the host-router poll) runs on the thread after it, so a firmware that
 * answers slowly holds no PnP request. Safe mode and every INERT
 * outcome: the inert start - the devnode started, the reason logged and
 * counted, nothing written to the hardware (the integrated family's own
 * power-up of test (b) excepted, which the power record of section 1.4.2
 * owns). Never a failed devnode for any of them.
 *
 * THE STOP (stop, surprise removal, removal; section 5.6's unbind, in its
 * order): the re-enumeration seam stopped, then the client's unbind - ring
 * 0 stopped, its queued actions retired, mailbox 0x07 only where driver
 * ready was sent; under the USB4 profile TbtU4Stop, the rings stopped and
 * the fabric left as it is - the common buffer freed after the rings, the
 * Bus Master bit and the integrated family's force power restored where
 * this driver set them (the USB4 profile's only with no device router
 * connected, record 22 section 4.2), BAR0 unmapped, then the thread ended. An inert instance has
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
#include "cm_nvlink.h"

/* cm.h: an address record, at XhciCmLogVerbosity 4 alone (the shared ring's
 * own gate, XhciLogAppendAddress). Here rather than beside CmLogNote in
 * cm_log.c, whose values and emission another branch owns. IRQL:
 * <= DISPATCH_LEVEL. */
VOID CmLogNoteAddress(PCM_NHI nhi, const char *label, ULONG value)
{
    KIRQL irql;

    KeAcquireSpinLock(&nhi->LogLock, &irql);
    XhciLogAppendAddress(&nhi->Log, label, value);
    KeReleaseSpinLock(&nhi->LogLock, irql);
}

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
    "cm.icm.rediscoveries", "cm.icm.resume-budget-expired", "cm.icm.lost",
    "cm.icm.recoveries", "cm.icm.version.reads", "cm.icm.version.failures"
};

static const char *const cmRingCounterNames[] = {
    "cm.ring.tx", "cm.ring.rx", "cm.ring.rx-usable", "cm.ring.crc-errors",
    "cm.ring.overflows", "cm.ring.malformed", "cm.ring.tx-full",
    "cm.ring.tx-stuck", "cm.ring.ctl.reads", "cm.ring.ctl.retries",
    "cm.ring.ctl.timeouts", "cm.ring.ctl.notify", "cm.ring.ctl.stale",
    "cm.ring.unsolicited", "cm.ring.ctl.writes", "cm.ring.ring0-was-enabled",
    "cm.ring.other-rings-enabled", "cm.ring.stop-unverified"
};

static const char *const cmNhiCounterNames[] = {
    "cm.passes", "cm.polls", "cm.actions.up", "cm.actions.down",
    "cm.actions.resync", "cm.actions.not-live", "cm.reenum.skipped",
    "cm.config.failures", "cm.suspends", "cm.resumes", "cm.resume.failures",
    "cm.shutdowns", "cm.power.request-failures", "cm.power.inline",
    "cm.buffers.kept", "cm.power.wakes-unpowered", "u4.drv.starts",
    "u4.drv.actions.up", "u4.drv.actions.down", "u4.drv.actions.resync",
    "u4.drv.actions.deep", "u4.drv.named", "u4.drv.name-refused",
    "u4.drv.learned", "u4.drv.learn-tries", "u4.drv.linkdowns",
    "u4.drv.paths-closed", "u4.drv.statement.no-channel", "u4.drv.ejects",
    "u4.drv.ejects.unmapped", "u4.drv.ejects.failed", "u4.drv.resume.held",
    "u4.drv.force-power.kept", "u4.drv.contained", "u4.drv.contain-refused",
    "u4.recover.probes", "u4.recover.refused", "u4.recover.ok",
    "u4.recover.final", "u4.recover.retire-requests",
    "u4.recover.retirement-owed", "u4.recover.rearm"
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
        CmLogNote(nhi, "cm.dev.version",
                  ((d->VersionState & 0xFFUL) << 8) |
                      (d->Usb4Version & 0xFFUL));
    }
}

static VOID cmU4LogCounters(PCM_NHI nhi);
static VOID cmU4BindRest(PCM_NHI nhi);
static VOID cmU4Drain(PCM_NHI nhi);
static VOID cmU4LearnPending(PCM_NHI nhi);
static BOOLEAN cmU4Contained(PCM_NHI nhi);

static VOID cmLogCounters(PCM_NHI nhi)
{
    cmLogBlock(nhi, cmNhiCounterNames, (const ULONG *)&nhi->Counters,
               sizeof(CM_COUNTERS) / sizeof(ULONG));
    if (nhi->U4On) {
        cmLogBlock(nhi, cmRingCounterNames,
                   (const ULONG *)&nhi->Ring.Counters,
                   sizeof(TBT_RING_COUNTERS) / sizeof(ULONG));
        cmU4LogCounters(nhi);
        return;
    }
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
 * (cm_reenum.h), unless XhciCmReenum is 0; the USB4 profile's through
 * cmU4Drain. */
VOID CmNhiDrainActions(PCM_NHI nhi)
{
    TBT_ICM_ACTION a;
    BOOLEAN up;
    KIRQL irql;

    if (nhi->U4On) {
        cmU4Drain(nhi);
        return;
    }
    if (nhi->Icm.Ring == NULL) {
        return;
    }
    while (TbtIcmNextAction(&nhi->Icm, &a)) {
        up = (BOOLEAN)(a.Kind == TBT_ICM_ACT_TUNNEL_UP);
        if (up) {
            /* Admitted under WorkLock against CmNhiRevoke, which waits
             * for the hand-over below to end: surprise-removed, no tunnel
             * is published as up any more (cm_live.h). */
            KeAcquireSpinLock(&nhi->WorkLock, &irql);
            if (nhi->Live.Revoked) {
                KeReleaseSpinLock(&nhi->WorkLock, irql);
                nhi->Counters.ActionsNotLive++;
                continue;
            }
            if (nhi->Delivering++ == 0) {
                KeClearEvent(&nhi->DeliverIdle);
            }
            KeReleaseSpinLock(&nhi->WorkLock, irql);
        }
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
        if (up) {
            KeAcquireSpinLock(&nhi->WorkLock, &irql);
            if (--nhi->Delivering == 0) {
                (VOID)KeSetEvent(&nhi->DeliverIdle, IO_NO_INCREMENT, FALSE);
            }
            KeReleaseSpinLock(&nhi->WorkLock, irql);
        }
    }
}

VOID CmNhiPollPass(PCM_NHI nhi)
{
    if (nhi->U4On) {
        if (cmU4Contained(nhi)) {
            return;
        }
        CmNhiU4Barrier(nhi);
        TbtU4Poll(&nhi->U4);
        CmNhiDrainActions(nhi);
        cmU4LearnPending(nhi);
        if (nhi->U4.Failed && nhi->Stage == CM_STAGE_ACTIVE) {
            /* A failure after activation inside the tick (record 22
             * section 3.5): the core stopped its rings. */
            nhi->Stage = CM_STAGE_FAILED;
            CmLogNote(nhi, "u4.failed", nhi->U4.InertReason);
        }
        return;
    }
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

/* ---------------------------------------------------------------------- */
/* The USB4 profile (design record 22, task 40-A.6)                       */
/* ---------------------------------------------------------------------- */

/* Labels in the order of TBT_U4_COUNTERS' fields (tbt_u4cm.h). */
static const char *const cmU4CounterNames[] = {
    "u4.port-checks", "u4.connects", "u4.routers.enumerated",
    "u4.routers.adopted", "u4.routers.refused", "u4.routers.tbt3",
    "u4.rop.issued", "u4.rop.unsupported", "u4.rop.failed",
    "u4.rop.timeouts", "u4.rop.blocked", "u4.drom.reads",
    "u4.drom.crc-failures", "u4.bond.attempts", "u4.bond.x2",
    "u4.bond.single", "u4.lane1-disabled", "u4.config-ready.timeouts",
    "u4.tmu.enabled", "u4.tmu.failures", "u4.pcie.built", "u4.pcie.failed",
    "u4.pcie.torn-down", "u4.pcie.lost", "u4.usb3.built", "u4.usb3.failed",
    "u4.usb3.torn-down", "u4.usb3.disconnected", "u4.detect.timeouts",
    "u4.pending.timeouts", "u4.hca.timeouts", "u4.quarantined",
    "u4.quarantine.lifted", "u4.ports.preserved", "u4.ports.held",
    "u4.connects.deferred", "u4.resume.budgets-spent", "u4.ports.cleaned",
    "u4.dpr", "u4.hrr", "u4.quirks", "u4.statements.sent",
    "u4.statements.withheld",
    "u4.statements.pending", "u4.unplugs", "u4.unplugs.nested",
    "u4.quick-replugs", "u4.hotplug.lane0", "u4.hotplug.other",
    "u4.hotplug.downstream", "u4.links.discovered", "u4.links.removed",
    "u4.action-overflows", "u4.ejects", "u4.contained.refused",
    "u4.contained.retired", "u4.recover.attempts", "u4.recover.committed",
    "u4.recover.stop-unverified", "u4.recover.preserved",
    "u4.recover.rebuilt", "u4.recover.departed", "u4.recover.held"
};

XHCI_C_ASSERT(cm_u4_counter_names,
              sizeof(cmU4CounterNames) / sizeof(cmU4CounterNames[0]) ==
                  sizeof(TBT_U4_COUNTERS) / sizeof(ULONG));

/* ICM-row tag in TBT_ICM_ACTION.Why for an action synthesized from a USB4
 * one (cmU4ToIcm): above every TBT_ICM_WHY_*, the USB4 kind in bits 3:0. */
#define CM_U4_WHY_BASE 0x80UL

/*
 * The USB4 profile contained (record 22 section 7.7): the core marked so,
 * or the liveness gate closed under this profile (a loss the monitor saw,
 * which under the USB4 profile is CONTAINED, or a surprise removal). Every
 * lifecycle step of the profile asks this before it starts, as well as
 * the gate refusing every register access. IRQL: PASSIVE_LEVEL.
 */
static BOOLEAN cmU4Contained(PCM_NHI nhi)
{
    return (BOOLEAN)(nhi->U4On && (nhi->U4.Contained || !CmNhiHwAccess(nhi)));
}

/* Failed after activation, or contained: what the suspend and the resume
 * act on never includes it. */
BOOLEAN CmNhiFailed(PCM_NHI nhi)
{
    return nhi->U4On ? (BOOLEAN)(nhi->U4.Failed || cmU4Contained(nhi))
                     : nhi->Icm.Failed;
}

/* A client the idle poll serves: the ICM bound, or the USB4 profile
 * running (its start done, not failed, not suspended). */
BOOLEAN CmNhiRunning(PCM_NHI nhi)
{
    if (nhi->U4On) {
        return (BOOLEAN)(nhi->U4.Stage == TBT_U4_STAGE_RUNNING &&
                         !nhi->U4.Failed && !nhi->U4.Suspended &&
                         !cmU4Contained(nhi));
    }
    return nhi->Icm.Bound;
}

/* The client's unbind: the ICM's (ring 0 stopped, mailbox 0x07 where
 * driver ready was sent), or the USB4 profile's stop (rings stopped,
 * nothing on the fabric changed: record 22 section 11, decision U7). */
VOID CmNhiUnbind(PCM_NHI nhi)
{
    if (nhi->U4On) {
        if (cmU4Contained(nhi)) {
            /* Record 22 section 7.7: no fresh reading of the ports (a
             * router request) and no ring stop (a write): the core is
             * contained, if the gate closed without it, so its stop
             * writes nothing; force power stays, the buffer follows
             * DmaMayRun (cm_pool.c). */
            nhi->U4ClearOk = 0;
            TbtU4Contain(&nhi->U4);
            TbtU4Stop(&nhi->U4);
            nhi->Counters.U4ContainRefused++;
            CmLogNote(nhi, "u4.stop.contained", nhi->Live.State);
            return;
        }
        /* Record 22 section 4.2's evidence, read fresh on the rings before
         * they stop: only then may force power this instance set be
         * cleared (CmNhiMayClearPower). */
        nhi->U4ClearOk = 0;
        if (nhi->Power.ForcePowerSetHere && nhi->Stage == CM_STAGE_ACTIVE &&
            !nhi->Suspended && TbtU4AllPortsDisconnected(&nhi->U4)) {
            nhi->U4ClearOk = 1;
        }
        TbtU4Stop(&nhi->U4);
        CmLogNote(nhi, "u4.stop", (nhi->U4.Failed ? 1UL : 0UL) |
                                      (nhi->U4ClearOk ? 2UL : 0UL));
        return;
    }
    TbtIcmUnbind(&nhi->Icm);
}

/*
 * Whether an active instance may clear the force power it set, after its
 * unbind (record 22 section 4.2): the ICM's always (Linux pci.c
 * icl_nhi_shutdown after the unbind); the USB4 profile's only when it knew
 * its ports and found no device router connected on any of them (Linux
 * pci.c icl_nhi_suspend returns early while one is), and only while that
 * knowledge is current: the instance still ACTIVE (never one whose start
 * or resume failed - its platform readiness or its rings did not come
 * back, and the port table is the one from before), not suspended, the
 * last firmware-ready poll passed, and the ports' table read on the running
 * rings. Any other case keeps the bit: what is behind the ports is
 * unknown, and an unknown is not "nothing connected".
 */
BOOLEAN CmNhiMayClearPower(PCM_NHI nhi)
{
    const TBT_U4_PORT *p;
    ULONG i;

    if (!nhi->U4On) {
        /* Never from a host interface the liveness gate closed: what
         * depends on its power is unknown (record 21 section 5.6). */
        return CmNhiHwAccess(nhi);
    }
    if (cmU4Contained(nhi) ||
        !nhi->U4ClearOk || nhi->Stage != CM_STAGE_ACTIVE || nhi->Suspended ||
        nhi->U4.Failed || nhi->U4.Suspended || nhi->Power.FwReadyFailed ||
        nhi->U4.PortCount == 0 || TbtU4AnyRouterConnected(&nhi->U4)) {
        return FALSE;
    }
    /* Affirmative disconnect evidence on every port: the last check left
     * it EMPTY (lane 0 in CLd, record 22 section 6.4 step 1) with nothing
     * owed to it. A port FAILED, UNSUPPORTED, in a sequence, or with a
     * check, an unplug or a residual sweep outstanding may have a router
     * on its link that this instance never enumerated. */
    for (i = 1; i <= nhi->U4.PortCount; i++) {
        p = TbtU4Port(&nhi->U4, i);
        if (p == NULL || p->State != TBT_U4_EMPTY || p->Dev.Present ||
            p->CheckPending || p->UnplugPending || p->HostResidual ||
            p->AsFoundOwed) {
            return FALSE;
        }
    }
    return TRUE;
}

/* ---------------------------------------------------------------------- */
/* Liveness (cm_live.h, record 21 sections 5.5 and 5.6)                   */
/* ---------------------------------------------------------------------- */

BOOLEAN CmNhiHwAccess(PCM_NHI nhi)
{
    return CmLiveTransport(&nhi->Live);
}

/* From the PnP dispatch: the gate shut for good before the stop's work is
 * queued, so a sequence the thread is inside ends at its next access or
 * wait (TBT_HOST.Cancelled). IRQL: PASSIVE_LEVEL. */
VOID CmNhiRevoke(PCM_NHI nhi)
{
    KIRQL irql;

    /* Under WorkLock, as the recovery's publication is (cmLiveRecover):
     * a publication either happens before this or not at all. */
    KeAcquireSpinLock(&nhi->WorkLock, &irql);
    CmLiveRevoke(&nhi->Live);
    KeReleaseSpinLock(&nhi->WorkLock, irql);
    /* An UP admitted before the revocation finishes its hand-over first;
     * none is admitted after it. The seam's call does not wait for this
     * PnP dispatch, so the wait ends. */
    (VOID)KeWaitForSingleObject(&nhi->DeliverIdle, Executive, KernelMode,
                                FALSE, NULL);
    CmLogNote(nhi, "cm.live.revoked", nhi->Live.State);
}

BOOLEAN CmNhiLiveArm(PCM_NHI nhi)
{
    ULONG v;
    ULONG want;

    v = CmThrProbe32(nhi, TBT_RX_RING(0) + TBT_RING_SIZE_REG);
    want = nhi->Ring.Entries | (TBT_RING_BUF_BYTES << TBT_RX_BUFSIZE_SHIFT);
    CmLogNote(nhi, "cm.live.baseline", v);
    if (v == 0xFFFFFFFFUL) {
        /* Not answering right after its rings started: a failure, never
         * an unmonitored success. */
        return FALSE;
    }
    if (v != want) {
        /* Readable but not as written: a register that does not read back
         * (not seen on the emulator; unread on hardware) cannot detect a
         * lost configuration, and arming against the programmed value
         * would report one at once. All-ones detection stays; logged. */
        CmLogNote(nhi, "cm.live.no-readback", v);
        v = 0;
    }
    return CmLiveArm(&nhi->Live, v, nhi->Host.NowMs(nhi));
}

/* The USB4 profile's containment (record 22 section 3.5): transport,
 * lifecycle, ejects and deferred work refused until a PnP stop and start;
 * DMA memory, force power, reservations and obligations kept. The gate
 * already refuses every access; the profile's own containment of its
 * queued and deferred work is task u4-live's. */
VOID CmNhiU4Contain(PCM_NHI nhi)
{
    CmNhiU4ContainEx(nhi, TRUE);
}

VOID CmNhiU4ContainEx(PCM_NHI nhi, BOOLEAN reconcile)
{
    TBT_ICM_ACTION a;
    KIRQL irql;
    ULONG before[XHCI_TBT_MAX_TUNNELS];
    ULONG m;

    if (!nhi->U4On || nhi->U4ContainDone) {
        return;
    }
    nhi->U4ContainDone = 1;
    /* 1: the core contained - requests refused, waits ended, actions and
     * deferred tails retired, anchors invalidated - with no access. */
    TbtU4Contain(&nhi->U4);
    /* The gate closed for good too, whatever made the call. A loss the
     * monitor did not record (a resume or a bind that found the host
     * interface gone while it was off) is recorded here as its episode,
     * once; one the monitor recorded, or a revocation, stands as it is. */
    CmLiveContain(&nhi->Live, nhi->Host.NowMs(nhi));
    nhi->Counters.U4Contained++;
    nhi->U4ClearOk = 0;
    CmLogNote(nhi, "u4.contained", (CmLiveEpisode(&nhi->Live) << 8) |
                                       (nhi->Live.State & 0xFFUL));
    /* 2: a batch taken and not delivered never reaches the seam. */
    nhi->U4BatchCount = 0;
    /* 3: the stor98 channel told presence was lost, nothing proven
     * (record 21 section 7.3), through its own loss route (cm_nvlink.h CmNvlinkLost, as the ICM
     * client's unavailable TUNNEL_DOWN takes it on NT): each UP entry of
     * the set's buses as CM_NVLINK_LOST - cleared only where its function
     * reads absent, never DOWN_QUIESCED. On Windows 98 and ME, where the
     * loss reads no bridge, nothing is read and no entry changed: the
     * gone deadline applies. */
    nhi->RecLostUp = 0;
    nhi->RecUpValid = nhi->U4UpValid;
    if (IoIsWdmVersionAvailable(1, 0x10) && nhi->ReenumOn) {
        /* Which members' entries the loss marking takes out of UP: a
         * tunnel preserved behind one is rebuilt, never re-published
         * (record 22 section 7.7 part 4, R8b). */
        for (m = 0; m < nhi->Reenum.Topo.Tunnels && m < XHCI_TBT_MAX_TUNNELS;
             m++) {
            before[m] = CmNvlinkUpCount(nhi, nhi->Reenum.Topo.Set[m]);
        }
        CmNvlinkLost(nhi);
        for (m = 0; m < nhi->Reenum.Topo.Tunnels && m < XHCI_TBT_MAX_TUNNELS;
             m++) {
            if (CmNvlinkUpCount(nhi, nhi->Reenum.Topo.Set[m]) < before[m]) {
                nhi->RecLostUp |= 1UL << m;
            }
        }
        CmLogNote(nhi, "u4.contained.lost-up", nhi->RecLostUp);
    }
    nhi->U4UpValid = 0;
    /* 4: one reconciliation, reason "controller unavailable": every member
     * of the tunnel-port set as changed (CmReenumAction's RESYNC, which
     * also voids the pending ejects), then the seam's own route for the
     * loss (CmReenumUnavailable). XhciCmReenum 0 requests nothing. Not on
     * the stop's path: that runs inside a PnP request, and the seam's
     * CONFIGMG route on Windows 98 and ME calls CONFIGMG synchronously;
     * the removal under way and the next start's discovery stand for it. */
    /* Nor while a PnP request waits for this thread: a surprise removal
     * revoked the gate, or a stop or the exit is queued behind the work
     * running now (the resume, the bind). */
    if (reconcile) {
        KeAcquireSpinLock(&nhi->WorkLock, &irql);
        if (nhi->Live.Revoked || nhi->Live.State == CM_LIVE_REVOKED ||
            nhi->PnpStopPending ||
            (nhi->WorkBits & (CM_WORK_STOP | CM_WORK_EXIT)) != 0) {
            reconcile = FALSE;
        }
        KeReleaseSpinLock(&nhi->WorkLock, irql);
    }
    if (!reconcile) {
        CmLogNote(nhi, "u4.contained.no-reconcile", nhi->Live.State);
    }
    if (nhi->ReenumOn) {
        /* The loss's bracket (cm_reenum.h, as CmNhiLost for the ICM
         * client): on the CONFIGMG route an action handed over inside it
         * only voids the pending ejects, and the worker's
         * CONTROLLER_UNAVAILABLE request is the reconciliation. The RESYNC
         * itself is made on NT only (WDM 1.10): on Windows 98 and ME the
         * seam's first action would open CONFIGMG on this thread, which a
         * PnP stop may be waiting for. On NT its invalidations are
         * asynchronous. */
        CmReenumLossBegin(nhi);
        if (reconcile && IoIsWdmVersionAvailable(1, 0x10) &&
            nhi->Reenum.Route != CM_ROUTE_CONFIGMG) {
            CmZero(&a, sizeof(a));
            a.Kind = TBT_ICM_ACT_RESYNC;
            a.Why = TBT_ICM_WHY_UNAVAILABLE;
            nhi->Counters.U4ActionsResync++;
            if (nhi->Values.Value[CM_VAL_REENUM] == 0) {
                nhi->Counters.ReenumSkipped++;
            }
            CmReenumAction(nhi, &a);
        }
        if (reconcile) {
            CmReenumUnavailable(nhi, CmLiveEpisode(&nhi->Live));
        } else {
            CmReenumLossEnd(nhi);
        }
    }
}

/* The precondition of a recovery: the function still the one the healthy
 * start found (identity and BAR0 through configuration space), the host
 * interface answering, and - for the ICM client - the selection's tests
 * again on reads alone, the outcome unchanged. A read refused or unknown
 * refuses the recovery, never the containment before it. */
static BOOLEAN cmLiveValidate(PCM_NHI nhi)
{
    TBT_HW_SELECTION sel;
    ULONG v;
    ULONG hi;
    ULONG cmd;

    nhi->CfgStrict = 1;
    v = CmCfgRead32(nhi, 0x00);
    if (v != nhi->LiveIds) {
        nhi->CfgStrict = 0;
        CmLogNote(nhi, "cm.live.ids-changed", v);
        return FALSE;
    }
    v = CmCfgRead32(nhi, 0x10);
    hi = 0;
    if ((nhi->LiveBar0 & 0x6UL) == 0x4UL) {
        hi = CmCfgRead32(nhi, 0x14);
    }
    cmd = CmCfgRead32(nhi, 0x04);
    nhi->CfgStrict = 0;
    /* Both from configuration space, the bus address against the one the
     * healthy start read there: attribute bits masked, a 64-bit BAR's
     * high DW too. Reassigned or reset: only a PnP stop and start can map
     * it. */
    if (v == 0xFFFFFFFFUL || (v & ~0xFUL) != (nhi->LiveBar0 & ~0xFUL) ||
        hi != nhi->LiveBar0Hi) {
        CmLogNoteAddress(nhi, "cm.live.bar-changed", v);
        return FALSE;
    }
    if (cmd == 0xFFFFFFFFUL || (cmd & 0x2UL) == 0) {
        /* Memory decoding off: the mapping no longer reaches it. */
        CmLogNote(nhi, "cm.live.decode-off", cmd);
        return FALSE;
    }
    /* Bus Master is restored by the recovery itself, under its own access
     * class, never before the precondition has passed. */
    nhi->LiveNeedBm = (cmd & 0x4UL) == 0 ? 1UL : 0UL;
    if (CmThrProbe32(nhi, TBT_HI_CAPS) == 0xFFFFFFFFUL) {
        return FALSE;
    }
    if (nhi->U4On) {
        return FALSE;
    }
    nhi->AccessClass = CM_ACCESS_PROBE;
    if (!nhi->Live.Full && !TbtRingIntact(&nhi->Ring)) {
        /* A light recovery keeps the rings: both must still hold what this
         * driver programmed, else the full one is owed and not admitted
         * here (a new episode re-arms it). */
        nhi->AccessClass = CM_ACCESS_ORDINARY;
        CmLogNote(nhi, "cm.live.rings-changed", 1);
        return FALSE;
    }
    TbtHwSelect(nhi->Hw, &nhi->Host, TRUE, TbtHwIsApple(nhi->SubVendor),
                FALSE, &sel);
    nhi->AccessClass = CM_ACCESS_ORDINARY;
    CmLogNote(nhi, "cm.live.select", sel.Outcome);
    return (BOOLEAN)(sel.Outcome == TBT_SEL_ICM);
}

/* The recovery the monitor admitted, on the retained buffer, under the
 * RECOVERY class; ACTIVE again only for this episode and only if nothing
 * revoked it meanwhile (CmLiveRecovered). */
static VOID cmLiveRecover(PCM_NHI nhi, BOOLEAN full)
{
    ULONG episode;
    ULONG v;
    LONG rc;
    BOOLEAN ok;
    BOOLEAN published;
    KIRQL irql;

    episode = CmLiveEpisode(&nhi->Live);
    CmLiveTransition(&nhi->Live, TRUE);
    nhi->AccessClass = CM_ACCESS_RECOVERY;
    rc = TBT_OK;
    if (nhi->LiveNeedBm && !CmNhiSetBusMaster(nhi, TRUE)) {
        rc = TBT_E_STATE;
    }
    if (rc == TBT_OK) {
        rc = TbtIcmRecover(&nhi->Icm, full);
    }
    nhi->AccessClass = CM_ACCESS_ORDINARY;
    CmLiveTransition(&nhi->Live, FALSE);
    ok = (BOOLEAN)(rc == TBT_OK);
    CmLogNote(nhi, "cm.live.recover", (full ? 0x100UL : 0) | (ULONG)(-rc));
    v = CmThrProbe32(nhi, TBT_RX_RING(0) + TBT_RING_SIZE_REG);
    KeAcquireSpinLock(&nhi->WorkLock, &irql);
    published = CmLiveRecovered(&nhi->Live, episode, ok, v,
                                nhi->Host.NowMs(nhi));
    KeReleaseSpinLock(&nhi->WorkLock, irql);
    if (published) {
        CmLogNote(nhi, "cm.live.active", episode);
        CmNhiDrainActions(nhi);
        return;
    }
    /* Not published (failed, or revoked meanwhile): whatever the recovery
     * rebuilt is speculative - its queued UPs are dropped and every entry
     * leaves as UNAVAILABLE, unconditionally; the seam's unavailable
     * request stays once per episode. */
    CmNhiLost(nhi, episode,
              (BOOLEAN)(!nhi->Live.Revoked &&
                        (CmLiveReconcileOwed(&nhi->Live) &
                         CM_LIVE_ACT_RECONCILE) != 0));
    CmLogNote(nhi, "cm.live.state", (episode << 8) | nhi->Live.State);
}

VOID CmNhiLost(PCM_NHI nhi, ULONG episode, BOOLEAN request)
{
    TbtIcmLost(&nhi->Icm);
    if (nhi->ReenumOn) {
        CmReenumLossBegin(nhi);
    }
    CmNhiDrainActions(nhi);
    if (nhi->ReenumOn) {
        if (request) {
            CmReenumUnavailable(nhi, episode);
        } else {
            CmReenumLossEnd(nhi);
        }
    }
}

VOID CmNhiLiveActions(PCM_NHI nhi, ULONG actions)
{
    ULONG more;

    if (actions & CM_LIVE_ACT_LOG) {
        CmLogNote(nhi, "cm.live.state",
                  (CmLiveEpisode(&nhi->Live) << 8) | nhi->Live.State);
    }
    if (actions & CM_LIVE_ACT_CONTAIN) {
        CmNhiU4Contain(nhi);
        return;
    }
    if ((actions & CM_LIVE_ACT_RECONCILE) && !nhi->U4On) {
        /* Presence lost, nothing about the endpoint proven: every entry
         * goes as UNAVAILABLE and the seam is told (record 21 section
         * 7.3); no register is touched. */
        CmNhiLost(nhi, CmLiveEpisode(&nhi->Live), TRUE);
    }
    if (actions & CM_LIVE_ACT_VALIDATE) {
        more = CmLiveValidated(&nhi->Live, cmLiveValidate(nhi));
        if (more & (CM_LIVE_ACT_LOG | CM_LIVE_ACT_RECONCILE)) {
            CmNhiLiveActions(nhi, more & (CM_LIVE_ACT_LOG |
                                          CM_LIVE_ACT_RECONCILE));
        }
        if (more & (CM_LIVE_ACT_RECOVER_LIGHT | CM_LIVE_ACT_RECOVER_FULL)) {
            cmLiveRecover(nhi,
                          (BOOLEAN)((more & CM_LIVE_ACT_RECOVER_FULL) != 0));
        }
    }
}

/* The tunnel-port set the re-enumeration seam discovered, readable: its
 * configuration accessor bound and at least one member. */
static BOOLEAN cmTopoUsable(PCM_NHI nhi)
{
    return (BOOLEAN)(nhi->ReenumOn && nhi->Reenum.Cfg.Read != NULL &&
                     nhi->Reenum.Topo.Tunnels != 0);
}

/* ---------------------------------------------------------------------- */
/* The USB4 profile's recovery (record 22 section 7.7, task 42-A.10)      */
/* ---------------------------------------------------------------------- */

/* The tunnel-port set as read now, healthy (the bind, a discovery that
 * changed the set, a commit): each member's identity DW and bus numbers,
 * for part 3 step 6. PASSIVE_LEVEL, the thread. */
static VOID cmU4SetRecord(PCM_NHI nhi)
{
    PCM_REENUM r;
    ULONG i;
    ULONG v;

    r = &nhi->Reenum;
    nhi->RecSetCount = 0;
    if (!cmTopoUsable(nhi)) {
        return;
    }
    for (i = 0; i < r->Topo.Tunnels && i < XHCI_TBT_MAX_TUNNELS; i++) {
        nhi->RecSetBdf[i] = r->Topo.Set[i];
        nhi->RecSetId[i] = 0xFFFFFFFFUL;
        nhi->RecSetBus[i] = 0xFFFFFFFFUL;
        if (r->Cfg.Read(r->Cfg.Context, r->Topo.Set[i], 0x00, &v)) {
            nhi->RecSetId[i] = v;
        }
        if (r->Cfg.Read(r->Cfg.Context, r->Topo.Set[i], 0x18, &v)) {
            nhi->RecSetBus[i] = v & 0x00FFFFFFUL;
        }
    }
    nhi->RecSetCount = i;
}

/* Every member of the set as a mask (bit i, member i). */
static ULONG cmU4AllMembers(PCM_NHI nhi)
{
    ULONG n;

    n = nhi->Reenum.Topo.Tunnels;
    if (n >= 32) {
        return 0xFFFFFFFFUL;
    }
    return (1UL << n) - 1;
}

/* A PnP stop, removal or exit, or a power transition, queued or waiting
 * for the thread: a recovery is refused before it starts and ended
 * before it commits (record 22 section 7.7 part 3). */
static BOOLEAN cmU4StopWaiting(PCM_NHI nhi)
{
    BOOLEAN waiting;
    KIRQL irql;

    KeAcquireSpinLock(&nhi->WorkLock, &irql);
    waiting = (BOOLEAN)(nhi->Live.Revoked || nhi->PnpStopPending ||
                        (nhi->WorkBits & (CM_WORK_STOP | CM_WORK_EXIT |
                                          CM_WORK_D3 | CM_WORK_SYSTEM |
                                          CM_WORK_DIRECT)) != 0);
    KeReleaseSpinLock(&nhi->WorkLock, irql);
    return waiting;
}

/* Part 2, the preparation, in PREPARING under the PREPARE class: the
 * Command register's decoding and Bus Master bits repaired and read back,
 * section 4.2's platform step, the selection's tests again on reads with
 * the outcome unchanged, and the size register read through the BAR.
 * `ones` reports a read that came back all ones. */
static BOOLEAN cmU4Prepare(PCM_NHI nhi, ULONG cmd, BOOLEAN *ones)
{
    TBT_HW_SELECTION sel;
    ULONG v;
    ULONG step;

    *ones = FALSE;
    step = 0;
    nhi->AccessClass = CM_ACCESS_PREPARE;
    if ((cmd & 0x6UL) != 0x6UL) {
        step = 1;
        if (!CmCfgWrite16(nhi, CM_PCI_COMMAND,
                          (USHORT)((cmd & 0xFFFFUL) | 0x6UL))) {
            goto refused;
        }
        nhi->RecCmdSet |= ~cmd & 0x6UL;
        if ((cmd & CM_PCI_COMMAND_BUS_MASTER) == 0) {
            nhi->BusMasterSetHere = 1;
        }
        v = CmCfgRead32(nhi, CM_PCI_COMMAND);
        if (v == 0xFFFFFFFFUL || (v & 0x6UL) != 0x6UL) {
            *ones = (BOOLEAN)(v == 0xFFFFFFFFUL);
            goto refused;
        }
        CmLogNote(nhi, "u4.recover.command", (cmd << 16) | (v & 0xFFFFUL));
    }
    step = 2;
    if (!CmNhiPlatformInit(nhi)) {
        goto refused;
    }
    step = 3;
    nhi->AccessClass = CM_ACCESS_PROBE;
    TbtHwSelect(nhi->Hw, &nhi->Host, TRUE, TbtHwIsApple(nhi->SubVendor),
                nhi->Sel.Forced, &sel);
    CmLogNote(nhi, "u4.recover.select",
              (sel.Outcome << 16) | (sel.Reason & 0xFFFFUL));
    if (sel.Caps == 0xFFFFFFFFUL) {
        *ones = TRUE;
    }
    if (sel.Outcome != TBT_SEL_SOFTWARE ||
        TBT_HI_CAPS_VERSION(sel.Caps) != TBT_HI_CAPS_VERSION(nhi->Sel.Caps)) {
        goto refused;
    }
    step = 4;
    v = CmThrProbe32(nhi, TBT_RX_RING(0) + TBT_RING_SIZE_REG);
    if (v == 0xFFFFFFFFUL) {
        *ones = TRUE;
        goto refused;
    }
    /* Kept or lost, both admitted: the verified stop is the safety
     * argument, never an inference from this value (decision U18). */
    CmLogNote(nhi, v == CM_U4_RX_SIZE_VALUE ? "u4.recover.rings-kept"
                                             : "u4.recover.rings-lost", v);
    nhi->AccessClass = CM_ACCESS_ORDINARY;
    return TRUE;

refused:
    nhi->AccessClass = CM_ACCESS_ORDINARY;
    nhi->Counters.U4RecoverRefused++;
    CmLogNote(nhi, "u4.recover.refused", (2UL << 8) | step);
    return FALSE;
}

/* Part 3 step 6's last rule: a learned correspondence is kept only for a
 * member whose secondary bus is unchanged (`changed`) and whose port's
 * PCIe tunnel this recovery verified and kept (RecVerified, not a HELD
 * one's Up); every other learned binding is withdrawn (the port learns
 * again at its next tunnel up). Named bindings stand: the host router's
 * mapping operation, read again in step 3, is theirs. Made after the
 * commit, once the removal debt was taken by member. */
static VOID cmU4Unlearn(PCM_NHI nhi, ULONG changed)
{
    PXHCI_TBT_BINDS b;
    const TBT_U4_PORT *p;
    ULONG i;
    ULONG k;
    ULONG member;
    BOOLEAN keep;

    b = &nhi->Binds;
    i = 0;
    while (i < b->Count && i < XHCI_TBT_MAX_BINDS) {
        keep = TRUE;
        if (b->How[i] == XHCI_TBT_CORR_LEARNED) {
            p = TbtU4Port(&nhi->U4, b->Port[i]);
            member = XHCI_TBT_NO_MEMBER;
            for (k = 0; k < nhi->Reenum.Topo.Tunnels && k < 32; k++) {
                if (nhi->Reenum.Topo.Set[k] == b->Bdf[i]) {
                    member = k;
                }
            }
            keep = (BOOLEAN)(p != NULL && p->Pcie.Up &&
                             (p->RecVerified & 1) != 0 &&
                             member != XHCI_TBT_NO_MEMBER &&
                             (changed & (1UL << member)) == 0);
        }
        if (keep) {
            i++;
            continue;
        }
        CmLogNote(nhi, "u4.recover.unlearn", (b->Port[i] << 16) | (b->Bdf[i] & 0xFFFFUL));
        for (k = i; k + 1 < b->Count && k + 1 < XHCI_TBT_MAX_BINDS; k++) {
            b->Port[k] = b->Port[k + 1];
            b->Bdf[k] = b->Bdf[k + 1];
            b->How[k] = b->How[k + 1];
        }
        b->Count--;
    }
}

/* Part 3 step 6 (decision U24) and the demotions of part 4 (R8a, R8b,
 * U29), under the RECOVERY class before the commit: every member of the
 * set re-read - the same identity, still a bridge, still this domain's
 * alone - and a member whose secondary or subordinate bus changed, or
 * whose stor98 entry lost UP at the containment, sends every tunnel
 * preserved behind it back to the rebuild (its whole unattributed group
 * where its port is not known). FALSE refuses the recovery. */
static BOOLEAN cmU4Revalidate(PCM_NHI nhi)
{
    PCM_REENUM r;
    const TBT_U4_PORT *p;
    ULONG i;
    ULONG k;
    ULONG v;
    ULONG hdr;
    ULONG bus;
    ULONG changed;
    ULONG member;
    ULONG kept;
    BOOLEAN excl;

    r = &nhi->Reenum;
    nhi->RecBusPend = 0;
    if (!cmTopoUsable(nhi)) {
        return TRUE;
    }
    changed = 0;
    for (i = 0; i < r->Topo.Tunnels && i < XHCI_TBT_MAX_TUNNELS; i++) {
        for (k = 0; k < nhi->RecSetCount; k++) {
            if (nhi->RecSetBdf[k] == r->Topo.Set[i]) {
                break;
            }
        }
        v = 0xFFFFFFFFUL;
        if (k == nhi->RecSetCount ||
            !r->Cfg.Read(r->Cfg.Context, r->Topo.Set[i], 0x00, &v) ||
            !r->Cfg.Read(r->Cfg.Context, r->Topo.Set[i], 0x0C, &hdr) ||
            !r->Cfg.Read(r->Cfg.Context, r->Topo.Set[i], 0x18, &bus) ||
            v == 0xFFFFFFFFUL || v != nhi->RecSetId[k] ||
            ((hdr >> 16) & 0x7FUL) != 1) {
            CmLogNote(nhi, "u4.recover.member", (i << 24) | (v & 0xFFFFFFUL));
            return FALSE;
        }
        excl = FALSE;
        for (member = 0; member < r->Members; member++) {
            if (r->Port[member].Bdf == r->Topo.Set[i] &&
                (r->Port[member].Flags & CM_PORT_EXCLUSIVE) != 0) {
                excl = TRUE;
            }
        }
        if (!excl) {
            CmLogNote(nhi, "u4.recover.member.exclusive", i);
            return FALSE;
        }
        if (((bus >> 8) & 0xFFFFUL) != ((nhi->RecSetBus[k] >> 8) & 0xFFFFUL)) {
            changed |= 1UL << i;
            nhi->RecBusPend |= 1UL << i;
            nhi->RecOldBusPend[i] = (nhi->RecSetBus[k] >> 8) & 0xFFUL;
            CmLogNote(nhi, "u4.recover.member.bus",
                      (i << 24) | (bus & 0xFFFFFFUL));
        }
    }
    if (!CmReenumSourcesHold(nhi)) {
        /* U24: a member no longer named by the source that put it in the
         * set refuses the recovery for this episode. */
        CmLogNote(nhi, "u4.recover.member.source", r->Topo.Tunnels);
        return FALSE;
    }
    changed |= nhi->RecLostUp;
    if (changed != 0) {
        for (i = 1; i <= nhi->U4.PortCount; i++) {
            p = TbtU4Port(&nhi->U4, i);
            if (p == NULL || !p->Pcie.Up) {
                continue;
            }
            member = XhciTbtPortMember(&r->Topo, &nhi->Binds, i);
            if (member == XHCI_TBT_NO_MEMBER || member >= 32 ||
                (changed & (1UL << member)) != 0) {
                (VOID)TbtU4RecoverDemote(&nhi->U4, i);
            }
        }
    }
    /* The removal debt of the drains, by member, while the bindings still
     * say which member each port's endpoint sat behind; any port not
     * known owes it on every member. Accumulated across failed attempts
     * until a commit takes it into the barrier, and the bindings are
     * withdrawn only after that commit (cmU4AfterCommit), so a later
     * attempt still names the member a port's debt was taken against. */
    kept = 0;
    for (i = 1; i <= nhi->U4.PortCount; i++) {
        p = TbtU4Port(&nhi->U4, i);
        if (p == NULL || !p->Pcie.Up || (p->RecVerified & 1) == 0) {
            continue;
        }
        member = XhciTbtPortMember(&r->Topo, &nhi->Binds, i);
        if (member != XHCI_TBT_NO_MEMBER && member < 32) {
            kept |= 1UL << member;
        }
    }
    for (i = 1; i <= nhi->U4.PortCount; i++) {
        p = TbtU4Port(&nhi->U4, i);
        if (p == NULL || !p->RemovalDebt) {
            continue;
        }
        member = XhciTbtPortMember(&r->Topo, &nhi->Binds, i);
        if (member != XHCI_TBT_NO_MEMBER && member < 32) {
            nhi->RecDebtMask |= 1UL << member;
        } else {
            /* Every member but one bound to another port whose PCIe
             * tunnel this attempt verified and kept: a bridge carries one
             * port's tunnel, so that member's endpoint is that port's. */
            nhi->RecDebtMask |= cmU4AllMembers(nhi) & ~kept;
        }
    }
    nhi->RecChanged |= changed;
    return TRUE;
}

/*
 * Part 4's barrier, every pass: each barred member whose devnode is now
 * retired (CmReenumRetired) has mode retire applied to its channel
 * entries and its barrier lifted; one still standing gets its
 * removal-only request again every CM_U4_RETIRE_RETRY_MS (a veto
 * respected, never forced). Then every port's Barred from its member's
 * barrier - any barrier at all, for a port whose member is not known -
 * and an unattributed rebuild only while no other port is pending
 * learning (U29's handoff).
 */
#define CM_U4_RETIRE_RETRY_MS 5000UL

VOID CmNhiU4Barrier(PCM_NHI nhi)
{
    const TBT_U4_PORT *p;
    ULONG i;
    ULONG bit;
    ULONG member;
    ULONG now;
    ULONG bus;
    ULONG left;
    BOOLEAN barred;
    BOOLEAN armed;
    BOOLEAN armOk;

    if (!nhi->U4On) {
        return;
    }
    armed = FALSE;
    armOk = FALSE;
    if (nhi->RecBarred != 0 && !nhi->ReenumOn) {
        /* No seam (never started): no devnode this driver re-enumerates,
         * retires or publishes for. */
        nhi->RecBarred = 0;
    }
    if (nhi->RecBarred != 0 && cmTopoUsable(nhi)) {
        now = nhi->Host.NowMs(nhi);
        for (i = 0; i < nhi->Reenum.Topo.Tunnels && i < XHCI_TBT_MAX_TUNNELS;
             i++) {
            bit = 1UL << i;
            if ((nhi->RecBarred & bit) == 0) {
                continue;
            }
            if ((nhi->RecRetireSent & bit) != 0 &&
                CmReenumRetired(nhi, i, nhi->RecRetireAns[i])) {
                nhi->RecBarred &= ~bit;
                CmReenumHold(nhi, nhi->RecBarred);
                CmNvlinkRetireMember(nhi, nhi->Reenum.Topo.Set[i]);
                if ((nhi->RecBusChanged & bit) != 0) {
                    /* The observed retirement authorises R8a's release. */
                    nhi->RecRelOk |= bit;
                }
                CmLogNote(nhi, "u4.recover.retired", i);
                continue;
            }
            nhi->Counters.U4RetirementOwed++;
            if ((nhi->RecRetireSent & bit) != 0 &&
                now - nhi->RecRetireAtMs[i] < CM_U4_RETIRE_RETRY_MS) {
                continue;
            }
            if ((nhi->RecRetireSent & bit) != 0) {
                CmLogNote(nhi, "u4.recover.retirement-owed", i);
            }
            nhi->RecRetireAtMs[i] = now;
            if (CmReenumRetire(nhi, i, &nhi->RecRetireAns[i])) {
                nhi->RecRetireSent |= bit;
                nhi->Counters.U4RetireRequests++;
            }
        }
    }
    if ((nhi->RecBusChanged & nhi->RecRelOk) != 0 && cmTopoUsable(nhi)) {
        /* R8a: a member whose bus changed has its old entries' IN_USE
         * cleared only once its devnode's retirement was observed (a
         * member never barred has none), retried while one of them cannot
         * be released yet. */
        for (i = 0; i < nhi->Reenum.Topo.Tunnels && i < XHCI_TBT_MAX_TUNNELS;
             i++) {
            bit = 1UL << i;
            if ((nhi->RecBusChanged & nhi->RecRelOk & bit) == 0) {
                continue;
            }
            left = 0;
            for (bus = 1; bus <= 0xFFUL; bus++) {
                if ((nhi->RecOldBuses[i][bus >> 5] & (1UL << (bus & 31UL))) == 0) {
                    continue;
                }
                if (CmNvlinkReleaseBus(nhi, nhi->Reenum.Topo.Set[i], bus) == 0) {
                    nhi->RecOldBuses[i][bus >> 5] &= ~(1UL << (bus & 31UL));
                } else {
                    left++;
                }
            }
            if (left == 0) {
                nhi->RecBusChanged &= ~bit;
                nhi->RecRelOk &= ~bit;
            }
        }
    }
    for (i = 1; i <= nhi->U4.PortCount && i <= TBT_U4_MAX_PORTS; i++) {
        p = TbtU4Port(&nhi->U4, i);
        if (p == NULL) {
            continue;
        }
        member = cmTopoUsable(nhi)
                     ? XhciTbtPortMember(&nhi->Reenum.Topo, &nhi->Binds, i)
                     : XHCI_TBT_NO_MEMBER;
        if (member != XHCI_TBT_NO_MEMBER && member < 32) {
            barred = (BOOLEAN)((nhi->RecBarred & (1UL << member)) != 0);
        } else {
            barred = (BOOLEAN)(nhi->RecBarred != 0 ||
                               (p->RebuildPcie &&
                                (nhi->Binds.Pending & ~(1UL << (i - 1))) != 0));
            if (barred && nhi->RecBarred == 0 && !p->Barred) {
                CmLogNote(nhi, "u4.recover.learning-held", i);
            }
            if (!barred && p->RebuildPcie && cmTopoUsable(nhi)) {
                /* U29's handoff: a fresh occupancy baseline of every
                 * member's bus, taken now - every barrier clear, no other
                 * port pending - right before this pass's rebuild, so its
                 * gain is attributed against what answers now. */
                if (!armed) {
                    armed = TRUE;
                    armOk = (BOOLEAN)(XhciTbtLearnArm(&nhi->Reenum.Cfg,
                                                      &nhi->Reenum.Topo,
                                                      &nhi->Binds) == 0);
                    if (armOk) {
                        nhi->ArmOwed = 0;
                    }
                }
                barred = (BOOLEAN)!armOk;
            }
        }
        if (barred != p->Barred) {
            TbtU4SetBarred(&nhi->U4, i, barred);
            CmLogNote(nhi, "u4.recover.barred", (i << 8) | (barred ? 1UL : 0UL));
        }
    }
}

/* After the commit, with the lock released, admitted (Delivering): the
 * channel's record of what stayed UP, the removal debt taken into the
 * barrier and the removal-only requests made, then the recovery's RESYNC,
 * reason "controller recovered", publication mode none. */
static VOID cmU4AfterCommit(PCM_NHI nhi)
{
    TBT_ICM_ACTION a;
    const TBT_U4_PORT *p;
    ULONG i;

    CmLogNote(nhi, "u4.recover.ok", CmLiveEpisode(&nhi->Live));
    nhi->Counters.U4RecoverOk++;
    for (i = 1; i <= nhi->U4.PortCount && i <= TBT_U4_MAX_PORTS; i++) {
        p = TbtU4Port(&nhi->U4, i);
        if (p == NULL) {
            continue;
        }
        if (p->Pcie.Up && (nhi->RecUpValid & (1UL << (i - 1))) != 0 &&
            nhi->U4UpGen[i - 1] == p->Generation) {
            nhi->U4UpValid |= 1UL << (i - 1);
        }
        if (TbtU4TakeDebt(&nhi->U4, i)) {
            CmLogNote(nhi, "u4.recover.debt", i);
        }
        if (p->RebuildPcie && !p->RecDrainPcie && !p->Pcie.Up &&
            i <= XHCI_TBT_MAX_PORTS) {
            /* Step 5's read-back is the closure the pending mark waits
             * for; a fresh occupancy baseline follows. */
            XhciTbtLearnDown(&nhi->Binds, i);
            nhi->ArmOwed = 1;
        }
    }
    /* Read by step 6 while every binding stood (cmU4Revalidate); step 6's
     * withdrawal of the learned bindings follows it here. */
    nhi->RecBarred |= nhi->RecDebtMask;
    nhi->RecDebtMask = 0;
    cmU4Unlearn(nhi, nhi->RecChanged);
    nhi->RecChanged = 0;
    /* R8a's old entries, owed once each member's retirement is observed
     * after this change: every old bus still owed is kept beside the new
     * one, and an earlier authorisation does not cover it. */
    for (i = 0; i < XHCI_TBT_MAX_TUNNELS; i++) {
        if ((nhi->RecBusPend & (1UL << i)) != 0 &&
            nhi->RecOldBusPend[i] != 0 && nhi->RecOldBusPend[i] <= 0xFFUL) {
            nhi->RecOldBuses[i][nhi->RecOldBusPend[i] >> 5] |=
                1UL << (nhi->RecOldBusPend[i] & 31UL);
            nhi->RecBusChanged |= 1UL << i;
            nhi->RecRelOk &= ~(1UL << i);
        }
    }
    nhi->RecBusPend = 0;
    CmLogNote(nhi, "u4.recover.barrier", nhi->RecBarred);
    nhi->RecRetireSent = 0;
    CmReenumHold(nhi, nhi->RecBarred);
    if (nhi->U4LossFrom == 1) {
        /* The bind the loss interrupted: its remaining steps, as the bind
         * would have run them (the seam's start, the hooks, the first
         * tick). */
        nhi->U4LossFrom = 0;
        CmLogNote(nhi, "u4.recover.bind-rest", 1);
        cmU4BindRest(nhi);
        return;
    }
    nhi->U4LossFrom = 0;
    CmNhiU4Barrier(nhi);
    if (nhi->ReenumOn) {
        CmZero(&a, sizeof(a));
        a.Kind = TBT_ICM_ACT_RESYNC;
        a.Why = TBT_ICM_WHY_RECOVERED;
        nhi->Counters.U4ActionsResync++;
        if (nhi->Values.Value[CM_VAL_REENUM] == 0) {
            nhi->Counters.ReenumSkipped++;
        }
        CmReenumAction(nhi, &a);
        cmU4SetRecord(nhi);
    }
}

/* Part 3, in RECOVERING under the RECOVERY class, and its commit (step
 * 7): the core's work (TbtU4Recover), the PCI side revalidated, one fresh
 * sample, then under the work lock CmLiveU4Recovered's rule and the flag
 * updates alone; anything else is the failure path. */
static VOID cmU4Recover(PCM_NHI nhi)
{
    ULONG episode;
    ULONG sample;
    LONG rc;
    BOOLEAN final;
    BOOLEAN ones;
    BOOLEAN committed;
    BOOLEAN power;
    KIRQL irql;

    episode = CmLiveEpisode(&nhi->Live);
    final = FALSE;
    ones = FALSE;
    rc = TBT_E_STOPPED;
    nhi->AccessClass = CM_ACCESS_RECOVERY;
    if (!cmU4StopWaiting(nhi)) {
        rc = TbtU4Recover(&nhi->U4, &final);
    }
    CmLogNote(nhi, "u4.recover.core", (ULONG)(-rc));
    if (rc == TBT_OK && !cmU4Revalidate(nhi)) {
        rc = TBT_E_STATE;
    }
    if (rc == TBT_OK && cmU4StopWaiting(nhi)) {
        rc = TBT_E_STOPPED;
    }
    if (rc == TBT_OK && nhi->RecOnes) {
        /* Part 5: an all-ones read inside the attempt is a second outage
         * observed, whatever step read it and whether that step's failure
         * was tolerated: the attempt fails, Rearm set. */
        rc = TBT_E_HW;
    }
    if (rc != TBT_OK) {
        ones = (BOOLEAN)(!final &&
                         (nhi->RecOnes ||
                          CmThrProbe32(nhi, TBT_HI_CAPS) == 0xFFFFFFFFUL));
        if (cmU4StopWaiting(nhi)) {
            /* A power-cancelled attempt: the probe at the resume needs
             * no second outage (part 5's suspend row). */
            ones = TRUE;
        }
        TbtU4RecoverFail(&nhi->U4);
    }
    nhi->AccessClass = CM_ACCESS_ORDINARY;
    sample = CmThrProbe32(nhi, TBT_RX_RING(0) + TBT_RING_SIZE_REG);
    if (rc == TBT_OK && sample != CM_U4_RX_SIZE_VALUE) {
        /* The value this instance programs, whatever baseline the monitor
         * holds (a bind the loss interrupted armed none). */
        rc = TBT_E_HW;
        ones = (BOOLEAN)(sample == 0xFFFFFFFFUL || nhi->RecOnes);
        nhi->AccessClass = CM_ACCESS_RECOVERY;
        TbtU4RecoverFail(&nhi->U4);
        nhi->AccessClass = CM_ACCESS_ORDINARY;
    }
    committed = FALSE;
    KeAcquireSpinLock(&nhi->WorkLock, &irql);
    power = (BOOLEAN)((nhi->WorkBits & (CM_WORK_D3 | CM_WORK_SYSTEM |
                                        CM_WORK_DIRECT)) != 0 ||
                      nhi->DevicePower != PowerDeviceD0);
    if (power && !final && nhi->Live.State == CM_LIVE_RECOVERING) {
        /* A power transition queued meanwhile: the attempt ends as part
         * 5's suspend row says (cooldown, a new episode at the resume).
         * Never over a stop that could not be verified, which is final
         * for the boot whatever else is waiting. */
        CmLiveU4Interrupt(&nhi->Live);
    } else if (rc == TBT_OK && !nhi->PnpStopPending &&
        (nhi->WorkBits & (CM_WORK_STOP | CM_WORK_EXIT)) == 0) {
        committed = CmLiveU4Recovered(&nhi->Live, episode, TRUE, FALSE, FALSE,
                                      sample, nhi->Host.NowMs(nhi));
        if (committed) {
            TbtU4RecoverCommit(&nhi->U4);
            nhi->U4ContainDone = 0;
            nhi->Stage = CM_STAGE_ACTIVE;
            if (nhi->Delivering++ == 0) {
                KeClearEvent(&nhi->DeliverIdle);
            }
        }
    } else {
        (VOID)CmLiveU4Recovered(&nhi->Live, episode, FALSE, final, ones,
                                sample, nhi->Host.NowMs(nhi));
    }
    KeReleaseSpinLock(&nhi->WorkLock, irql);
    if (!committed) {
        if (rc == TBT_OK) {
            /* The commit refused (another episode, a revocation, the
             * sample): what the attempt restarted is stopped where the
             * gate still admits it, else the buffer is kept. */
            TbtU4RecoverFail(&nhi->U4);
        }
        nhi->Counters.U4RecoverRefused++;
        if (final) {
            nhi->Counters.U4RecoverFinal++;
            CmLogNote(nhi, "u4.recover.final", episode);
        }
        if (nhi->Live.Rearm) {
            nhi->Counters.U4RecoverRearm++;
        }
        CmLogNote(nhi, "u4.recover.refused",
                  (3UL << 8) | ((ULONG)(-rc) & 0xFFUL));
        CmLogNote(nhi, "cm.live.state", (episode << 8) | nhi->Live.State);
        return;
    }
    cmU4AfterCommit(nhi);
    KeAcquireSpinLock(&nhi->WorkLock, &irql);
    if (--nhi->Delivering == 0) {
        (VOID)KeSetEvent(&nhi->DeliverIdle, IO_NO_INCREMENT, FALSE);
    }
    KeReleaseSpinLock(&nhi->WorkLock, irql);
    CmLogNote(nhi, "cm.live.state", (episode << 8) | nhi->Live.State);
}

BOOLEAN CmNhiU4Probing(PCM_NHI nhi)
{
    return (BOOLEAN)(nhi->U4On && nhi->Stage == CM_STAGE_FAILED &&
                     nhi->U4LossFrom != 0 &&
                     nhi->Live.State == CM_LIVE_CONTAINED);
}

VOID CmNhiU4Probe(PCM_NHI nhi, ULONG nowMs)
{
    ULONG ids;
    ULONG bar;
    ULONG hi;
    ULONG cmd;
    ULONG size;
    ULONG result;
    ULONG act;
    BOOLEAN ones;
    BOOLEAN ok;

    if (cmU4StopWaiting(nhi)) {
        return;
    }
    nhi->Counters.U4RecoverProbes++;
    /* The header first, whatever the BAR says (a function whose memory
     * decoding the platform cleared answers configuration cycles alone),
     * each read strict: a short answer is no answer. */
    nhi->CfgStrict = 1;
    ids = CmCfgRead32(nhi, 0x00);
    bar = CmCfgRead32(nhi, 0x10);
    hi = 0;
    if ((nhi->LiveBar0 & 0x6UL) == 0x4UL) {
        hi = CmCfgRead32(nhi, 0x14);
    }
    cmd = CmCfgRead32(nhi, CM_PCI_COMMAND);
    nhi->CfgStrict = 0;
    if (ids == 0xFFFFFFFFUL) {
        result = CM_LIVE_PROBE_ONES;
    } else if (ids != nhi->LiveIds || cmd == 0xFFFFFFFFUL) {
        result = CM_LIVE_PROBE_OTHER;
    } else if (bar == 0 || bar == 0xFFFFFFFFUL ||
               (bar & ~0xFUL) != (nhi->LiveBar0 & ~0xFUL) ||
               hi != nhi->LiveBar0Hi) {
        result = CM_LIVE_PROBE_BAR;
        CmLogNoteAddress(nhi, "u4.recover.bar-changed", bar);
    } else {
        result = CM_LIVE_PROBE_OK;
    }
    size = CmThrProbe32(nhi, TBT_RX_RING(0) + TBT_RING_SIZE_REG);
    act = CmLiveProbe(&nhi->Live, nowMs, result);
    if (act & CM_LIVE_ACT_SNAPSHOT) {
        /* A new episode: the snapshot of what was changed meanwhile. */
        TbtU4Snapshot(&nhi->U4);
        CmLogNote(nhi, "u4.recover.snapshot", CmLiveEpisode(&nhi->Live));
    }
    if (act & CM_LIVE_ACT_LOG) {
        CmLogNote(nhi, "u4.recover.probe", (result << 24) | (size & 0xFFFFFFUL));
        CmLogNote(nhi, "cm.live.state",
                  (CmLiveEpisode(&nhi->Live) << 8) | nhi->Live.State);
    }
    if ((act & CM_LIVE_ACT_PREPARE) == 0) {
        return;
    }
    nhi->RecOnes = 0;
    ok = cmU4Prepare(nhi, cmd, &ones);
    if (nhi->RecOnes) {
        ones = TRUE;
    }
    if (ok && ones) {
        /* An all-ones read the preparation tolerated is still the second
         * outage part 5 counts: refused, Rearm set. */
        ok = FALSE;
        nhi->Counters.U4RecoverRefused++;
        CmLogNote(nhi, "u4.recover.refused", (2UL << 8) | 5UL);
    }
    if (ok && cmU4StopWaiting(nhi)) {
        ok = FALSE;
        ones = TRUE;
    }
    act = CmLivePrepared(&nhi->Live, ok, ones);
    if (!ok && ones) {
        nhi->Counters.U4RecoverRearm++;
    }
    if (act & CM_LIVE_ACT_RECOVER_FULL) {
        cmU4Recover(nhi);
    } else {
        CmLogNote(nhi, "cm.live.state",
                  (CmLiveEpisode(&nhi->Live) << 8) | nhi->Live.State);
    }
}

/*
 * The router-named source (record 22 sections 6.3 and 7.6): each USB4
 * port whose PCIe Down adapter the host router's mapping operation named
 * offers its bridge to the domain's set (XhciTbtTopoAddNamed). Called by
 * the re-enumeration seam after each discovery it acts on and before it
 * publishes the set's claims (cm_reenum.h); nothing under the ICM client.
 */
VOID CmNhiNamePorts(PCM_NHI nhi)
{
    const TBT_U4_PORT *p;
    ULONG i;
    ULONG rc;

    if (!nhi->U4On || nhi->Reenum.Cfg.Read == NULL || cmU4Contained(nhi)) {
        return;
    }
    for (i = 1; i <= nhi->U4.PortCount; i++) {
        p = TbtU4Port(&nhi->U4, i);
        if (p == NULL || !p->MapValid || p->PcieDown == 0) {
            continue;
        }
        rc = XhciTbtTopoAddNamed(&nhi->Reenum.Cfg, &nhi->Reenum.Topo,
                                 &nhi->Binds, p->Index, p->MapRid);
        if (rc == XHCI_TBT_MAP_ADDED || rc == XHCI_TBT_MAP_KNOWN) {
            nhi->Counters.U4Named++;
        } else {
            nhi->Counters.U4NameRefused++;
        }
        CmLogNote(nhi, "u4.named",
                  (p->Index << 24) | ((p->MapRid & 0xFFFFUL) << 8) |
                      (rc & 0xFFUL));
    }
}

/*
 * Between the readings of a re-enumeration poll (cm_reenum.h): the ICM's
 * frames serviced, so a disconnect inside the wait is seen. Under the USB4
 * profile nothing is sent or taken here - no request is outstanding, the
 * routers retransmit an unacknowledged hot plug event (Guide 4.3) and the
 * next idle tick takes it - so the poll's 2 s bound is all it costs.
 */
VOID CmNhiWaitService(PCM_NHI nhi)
{
    if (!nhi->U4On) {
        TbtIcmService(&nhi->Icm);
    }
    /* The servicing point of every bounded wait inside a pass: the
     * stor98 acknowledgement waits stepped at least as often as the wait
     * reads (task 42-A.12; record 21 section 7.2 part 0). */
    CmNvlinkStep(nhi);
}

/* Whether the device an action names is still the one it named: the
 * ICM's table entry, or the USB4 port's connection generation with its
 * router present (and, for a tunnel up, its PCIe tunnel still up). */
BOOLEAN CmNhiActionLive(PCM_NHI nhi, const TBT_ICM_ACTION *action)
{
    const TBT_U4_PORT *p;

    if (!nhi->U4On) {
        return TbtIcmActionLive(&nhi->Icm, action);
    }
    p = TbtU4Port(&nhi->U4, action->Link);
    return (BOOLEAN)(p != NULL && !nhi->U4.Failed && !cmU4Contained(nhi) &&
                     p->Generation == action->Serial && p->Dev.Present &&
                     (action->Kind != TBT_ICM_ACT_TUNNEL_UP || p->Pcie.Up));
}

/* The index in Reenum.Topo.Set of the member an action's port corresponds
 * to (named or learned, record 22 section 7.6), or XHCI_TBT_NO_MEMBER: the
 * whole set then acts, as it always does under the ICM client. */
ULONG CmNhiActionMember(PCM_NHI nhi, const TBT_ICM_ACTION *action)
{
    if (!nhi->U4On || !cmTopoUsable(nhi) ||
        action->Kind == TBT_ICM_ACT_RESYNC) {
        return XHCI_TBT_NO_MEMBER;
    }
    return XhciTbtPortMember(&nhi->Reenum.Topo, &nhi->Binds, action->Link);
}

/* A USB4 action in the re-enumeration seam's form: Link the USB4 port,
 * Serial its connection generation, Slot the port again (CmNhiActionLive
 * reads the port, never the ICM's table). A router removed below depth 1
 * re-enumerates the whole set, as a RESYNC: its function sat below the
 * depth-1 router's own switch (record 22 section 7.1). */
static VOID cmU4ToIcm(const TBT_U4_ACTION *u, PTBT_ICM_ACTION a)
{
    CmZero(a, sizeof(*a));
    if (u->Kind == TBT_U4_ACT_TUNNEL_UP) {
        a->Kind = TBT_ICM_ACT_TUNNEL_UP;
    } else if (u->Kind == TBT_U4_ACT_TUNNEL_DOWN) {
        a->Kind = TBT_ICM_ACT_TUNNEL_DOWN;
    } else {
        a->Kind = TBT_ICM_ACT_RESYNC;
    }
    a->Why = CM_U4_WHY_BASE | (u->Kind & 0xFUL);
    a->Slot = u->Port;
    a->Serial = u->Generation;
    a->Link = u->Port;
    a->Depth = (u->Kind == TBT_U4_ACT_DEEP_REMOVED) ? 2UL : 1UL;
    a->WasApproved = TRUE;
}

/* Whether `port`'s PCIe path was proven closed for connection
 * `generation` (cmU4LinkDown's record). */
static BOOLEAN cmU4ClosedFor(PCM_NHI nhi, ULONG port, ULONG generation)
{
    return (BOOLEAN)(port >= 1 && port <= TBT_U4_MAX_PORTS &&
                     (nhi->U4ClosedValid & (1UL << (port - 1))) != 0 &&
                     nhi->U4ClosedGen[port - 1] == generation);
}

/* The learning baseline taken again (tbt_topo.h, XhciTbtLearnArm): after a
 * discovery, and after a tunnel down this driver handled. */
static VOID cmU4Arm(PCM_NHI nhi)
{
    ULONG rc;

    nhi->ArmOwed = 0;
    if (!cmTopoUsable(nhi)) {
        return;
    }
    rc = XhciTbtLearnArm(&nhi->Reenum.Cfg, &nhi->Reenum.Topo, &nhi->Binds);
    if (rc != 0) {
        CmLogNote(nhi, "u4.learn.arm", rc);
    }
}

/* The learned correspondence for `port`'s PCIe tunnel (XhciTbtLearn): the
 * members' active marks as the seam keeps them, member by member. */
static VOID cmU4Learn(PCM_NHI nhi, ULONG port)
{
    ULONG active[XHCI_TBT_MAX_TUNNELS];
    ULONG i;
    ULONG rc;

    if (!cmTopoUsable(nhi)) {
        return;
    }
    for (i = 0; i < XHCI_TBT_MAX_TUNNELS; i++) {
        active[i] = (i < nhi->Reenum.Topo.Tunnels &&
                     (nhi->Reenum.Port[i].Flags & CM_PORT_ACTIVE) != 0)
                        ? 1UL
                        : 0UL;
    }
    nhi->Counters.U4LearnTries++;
    rc = XhciTbtLearn(&nhi->Reenum.Cfg, &nhi->Reenum.Topo, &nhi->Binds,
                      port, active);
    if (rc == XHCI_TBT_MAP_LEARNED) {
        nhi->Counters.U4Learned++;
    }
    if (rc != XHCI_TBT_MAP_NOT_GAINED) {
        CmLogNote(nhi, "u4.learn", (port << 8) | (rc & 0xFFUL));
    }
}

/* Whether the core holds a quarantined entry or blocked enable of `port`
 * (record 22 section 7.3): a path not proven invalid and drained. */
static BOOLEAN cmU4Quarantined(PCM_NHI nhi, ULONG port)
{
    ULONG k;

    for (k = 0; k < TBT_U4_QUARANTINE; k++) {
        if (nhi->U4.Quarantine[k].Used && nhi->U4.Quarantine[k].Port == port) {
            return TRUE;
        }
    }
    return FALSE;
}

/*
 * The learning marks reconciled with the core's port state before any
 * attribution of a pass (tbt_topo.h's rule: a gain is attributed only
 * while one port is pending). A port that may carry a live PCIe path not
 * yet attributed - a tunnel up or found, any entry or enable written and
 * not proven closed, an incomplete inventory, a residual, a quarantined
 * entry, a HELD port - is pending, whatever events were or were not seen
 * for it (a tunnel whose activation and rollback both failed queues no
 * UP). A pending port whose state proves no path at all - EMPTY, no
 * router, nothing written, nothing incomplete, residual or quarantined -
 * is released (XhciTbtLearnDown) and the baseline taken again: that state
 * is the core's own read-back evidence that nothing of the port can reach
 * a bridge, never a TUNNEL_DOWN alone.
 */
static VOID cmU4Reconcile(PCM_NHI nhi)
{
    const TBT_U4_PORT *p;
    ULONG port;
    ULONG bit;
    BOOLEAN uncertain;

    for (port = 1; port <= nhi->U4.PortCount && port <= XHCI_TBT_MAX_PORTS;
         port++) {
        p = TbtU4Port(&nhi->U4, port);
        if (p == NULL) {
            continue;
        }
        bit = 1UL << (port - 1);
        /* A PCIe tunnel a recovery drained and proved closed, owed its
         * rebuild, is not pending (record 22 section 7.7 part 4's
         * handoff): its port is HELD for the rebuild alone. */
        uncertain = (BOOLEAN)(p->Pcie.Up || p->Pcie.Written != 0 ||
                              p->Pcie.Incomplete || p->HostResidual ||
                              (p->State == TBT_U4_HELD &&
                               !(p->RebuildPcie && !p->RecDrainPcie)) ||
                              p->AsFoundOwed ||
                              cmU4Quarantined(nhi, port));
        if (uncertain) {
            XhciTbtLearnUp(&nhi->Binds, port);
        } else if ((nhi->Binds.Pending & bit) != 0 &&
                   p->State == TBT_U4_EMPTY && !p->Dev.Present) {
            XhciTbtLearnDown(&nhi->Binds, port);
            nhi->ArmOwed = 1;
            CmLogNote(nhi, "u4.learn.released", port);
        }
    }
}

/* Every port whose tunnel came up and is not attributed yet, tried again
 * at each idle tick: its device may answer Configuration Request Retry for
 * a while (tbt_topo.h, XhciTbtLearn). */
static VOID cmU4LearnPending(PCM_NHI nhi)
{
    ULONG port;

    if (cmU4Contained(nhi)) {
        return;     /* nothing is attributed after the loss */
    }
    cmU4Reconcile(nhi);
    if (nhi->ArmOwed) {
        cmU4Arm(nhi);
    }
    if (nhi->Binds.Pending == 0) {
        return;
    }
    for (port = 1; port <= nhi->U4.PortCount && port <= XHCI_TBT_MAX_PORTS;
         port++) {
        if (nhi->Binds.Pending & (1UL << (port - 1))) {
            cmU4Learn(nhi, port);
        }
    }
}

/* The router a port's action concerns, for the log (task 40-A.8: a
 * Thunderbolt 3 device router behind this USB4 host is named as one). */
static VOID cmU4LogRouter(PCM_NHI nhi, ULONG port)
{
    const TBT_U4_PORT *p;

    p = TbtU4Port(&nhi->U4, port);
    if (p == NULL || !p->Dev.Present) {
        return;
    }
    CmLogNote(nhi, "u4.router.id", (p->Dev.Vendor << 16) | p->Dev.Device);
    CmLogNote(nhi, "u4.router.kind",
              (port << 24) | (p->Dev.Usb4Version & 0xFFUL) |
                  (p->Dev.Tbt3 ? 0x100UL : 0) |
                  (p->Dev.Preserved ? 0x200UL : 0) |
                  (p->Bonded ? 0x400UL : 0) | (p->SingleLane ? 0x800UL : 0) |
                  (p->Usb3.Up ? 0x1000UL : 0));
}

/*
 * One drain of the USB4 profile's actions (record 22 section 7.6, tbt_topo.h
 * learning rules), in two halves. CmNhiU4Take: the batch taken off the
 * core's queue; XhciTbtLearnUp for every tunnel up of it; the stor98
 * channel's UP published for each live one. cmU4Deliver: each action,
 * oldest first, to the re-enumeration seam in its form (unless
 * XhciCmReenum is 0), a tunnel up then learned for its port, a tunnel down
 * arming the baseline again. The resume takes the batch before it
 * releases the bridges' held D0 IRPs and delivers it after, so a rebuilt
 * tunnel's UP is published before any child driver can resume (cm.h,
 * CM_U4_UP_HOOK). A batch taken and not yet delivered is delivered before
 * another is taken.
 */
/* The admission-and-drain protocol of every USB4 delivery (record 22
 * section 7.7 part 3 step 7, as the ICM UP's): admitted under the work
 * lock unless revoked or an ordinary stop or removal has begun
 * (PnpStopPending, raised before the stop's work is queued, or that work
 * queued), the in-flight count raised, so CmNhiRevoke waits for the
 * delivery to end; FALSE when not admitted. Every delivery runs on this
 * thread, which takes the stop's work only after it, so one admitted
 * before a stop has ended before the teardown begins. */
static BOOLEAN cmU4Admit(PCM_NHI nhi)
{
    KIRQL irql;

    KeAcquireSpinLock(&nhi->WorkLock, &irql);
    if (nhi->Live.Revoked || nhi->PnpStopPending ||
        (nhi->WorkBits & (CM_WORK_STOP | CM_WORK_EXIT)) != 0) {
        KeReleaseSpinLock(&nhi->WorkLock, irql);
        nhi->Counters.ActionsNotLive++;
        return FALSE;
    }
    if (nhi->Delivering++ == 0) {
        KeClearEvent(&nhi->DeliverIdle);
    }
    KeReleaseSpinLock(&nhi->WorkLock, irql);
    return TRUE;
}

static VOID cmU4Release(PCM_NHI nhi)
{
    KIRQL irql;

    KeAcquireSpinLock(&nhi->WorkLock, &irql);
    if (--nhi->Delivering == 0) {
        (VOID)KeSetEvent(&nhi->DeliverIdle, IO_NO_INCREMENT, FALSE);
    }
    KeReleaseSpinLock(&nhi->WorkLock, irql);
}

VOID CmNhiU4Take(PCM_NHI nhi)
{
    TBT_ICM_ACTION a;
    PTBT_U4_ACTION u;
    ULONG n;
    ULONG i;

    if (!nhi->U4On || nhi->U4BatchCount != 0 || cmU4Contained(nhi)) {
        return;
    }
    n = 0;
    while (n < TBT_U4_ACTION_QUEUE &&
           TbtU4NextAction(&nhi->U4, &nhi->U4Batch[n])) {
        n++;
    }
    nhi->U4BatchCount = n;
    /* Every tunnel up of the batch marked pending first, whether or not
     * the set can be read now (XhciTbtLearnUp reads nothing): a device
     * whose tunnel came up while the discovery was empty must still block
     * attributing a later gain to another port. Not one whose path this
     * driver already proved closed for that same generation (the
     * statement upcall ran during the tick that queued both): that
     * closure was the port's LearnDown, and the stale UP must not undo
     * it. A TUNNEL_DOWN alone is never closure. */
    for (i = 0; i < n; i++) {
        u = &nhi->U4Batch[i];
        if (u->Kind == TBT_U4_ACT_TUNNEL_UP && u->Port >= 1 &&
            u->Port <= XHCI_TBT_MAX_PORTS &&
            !cmU4ClosedFor(nhi, u->Port, u->Generation)) {
            XhciTbtLearnUp(&nhi->Binds, u->Port);
        }
    }
    if (nhi->U4UpHook != NULL) {
        if (!cmU4Admit(nhi)) {
            /* Revoked or stopping: no UP is published any more
             * (CmNhiRevoke, cmU4Admit). */
            nhi->U4BatchCount = 0;
            return;
        }
        for (i = 0; i < n; i++) {
            u = &nhi->U4Batch[i];
            if (u->Kind != TBT_U4_ACT_TUNNEL_UP) {
                continue;
            }
            cmU4ToIcm(u, &a);
            if (CmNhiActionLive(nhi, &a)) {
                /* The stor98 channel's UP, before the endpoint is exposed;
                 * never for a connection that has already gone. Kept until
                 * the channel is told down (CmNhiU4ContainEx). */
                nhi->U4UpHook(nhi->U4HookContext, u);
                if (u->Port >= 1 && u->Port <= TBT_U4_MAX_PORTS) {
                    nhi->U4UpGen[u->Port - 1] = u->Generation;
                    nhi->U4UpValid |= 1UL << (u->Port - 1);
                }
            }
        }
        cmU4Release(nhi);
    }
}

static VOID cmU4Deliver(PCM_NHI nhi)
{
    TBT_ICM_ACTION a;
    PTBT_U4_ACTION u;
    ULONG n;
    ULONG i;

    n = nhi->U4BatchCount;
    if (cmU4Contained(nhi)) {
        /* Record 22 section 7.7: nothing of a batch taken before the loss
         * reaches the seam; CmNhiU4Contain's reconciliation stands for it. */
        nhi->U4BatchCount = 0;
        return;
    }
    if (!cmU4Admit(nhi)) {
        nhi->U4BatchCount = 0;
        return;
    }
    /* Before any XhciTbtLearn of this batch: every port that may carry a
     * live unattributed path is pending (cmU4Reconcile). */
    cmU4Reconcile(nhi);
    for (i = 0; i < n; i++) {
        u = &nhi->U4Batch[i];
        if (u->Kind == TBT_U4_ACT_TUNNEL_UP) {
            nhi->Counters.U4ActionsUp++;
        } else if (u->Kind == TBT_U4_ACT_TUNNEL_DOWN) {
            nhi->Counters.U4ActionsDown++;
        } else if (u->Kind == TBT_U4_ACT_DEEP_REMOVED) {
            nhi->Counters.U4ActionsDeep++;
        } else {
            nhi->Counters.U4ActionsResync++;
        }
        CmLogNote(nhi, "u4.action",
                  (u->Kind << 24) | ((u->Port & 0xFFUL) << 16) |
                      (u->Generation & 0xFFFFUL));
        CmLogNote(nhi, "u4.action.map",
                  u->MapValid ? (0x80000000UL | (u->MapRid & 0xFFFFUL))
                              : (u->PcieDown & 0xFFUL));
        if (u->Kind == TBT_U4_ACT_TUNNEL_UP) {
            cmU4LogRouter(nhi, u->Port);
        }
        cmU4ToIcm(u, &a);
        /* Every action reaches the seam, XhciCmReenum 0 included (it then
         * re-enumerates nothing but still voids the pending ejects: the
         * port's connection changed). */
        if (nhi->Values.Value[CM_VAL_REENUM] == 0) {
            nhi->Counters.ReenumSkipped++;
        }
        if (nhi->ReenumOn) {
            CmReenumAction(nhi, &a);
        }
        if (u->Port >= 1 && u->Port <= TBT_U4_MAX_PORTS) {
            /* The connection the seam last heard of on this port: an
             * eject is honoured only for it (cmU4Eject). */
            nhi->U4SeamGen[u->Port - 1] = u->Generation;
            nhi->U4SeamValid |= 1UL << (u->Port - 1);
        }
        if (u->Kind == TBT_U4_ACT_TUNNEL_UP && CmNhiActionLive(nhi, &a)) {
            cmU4Learn(nhi, u->Port);
        } else if (u->Kind == TBT_U4_ACT_TUNNEL_DOWN ||
                   u->Kind == TBT_U4_ACT_DEEP_REMOVED) {
            nhi->ArmOwed = 1;
        }
    }
    nhi->U4BatchCount = 0;
    cmU4Release(nhi);
    if (nhi->ArmOwed) {
        cmU4Arm(nhi);
    }
}

static VOID cmU4Drain(PCM_NHI nhi)
{
    if (nhi->U4BatchCount != 0) {
        /* A batch the resume took (its UPs published) comes first. */
        cmU4Deliver(nhi);
    }
    CmNhiU4Take(nhi);
    cmU4Deliver(nhi);
}

/*
 * The re-enumeration seam's discovery changed the set (cm_reenum.h): the
 * learning baseline is taken again over the set as it now is; the
 * correspondence and the pending marks, kept by bridge and port, stand.
 */
VOID CmNhiSetChanged(PCM_NHI nhi)
{
    if (nhi->U4On && !cmU4Contained(nhi)) {
        cmU4Arm(nhi);
        cmU4SetRecord(nhi);
    }
}

/*
 * Record 22 section 7.3 steps 2 and 3, the driver's half, called by the
 * core after step 1 of a PCIe teardown (possibly nested at a safe point;
 * it calls nothing of tbt_u4cm.c's). A path proven closed ends the port's
 * pending learning for that generation (tbt_topo.h: XhciTbtLearnDown only
 * on that evidence, never on a TUNNEL_DOWN alone) and owes a new baseline.
 * The stor98 statement is the channel's (task 40-A.7, CM_U4_DOWN_HOOK in
 * cm.h), handed this moment with PathClosed and the anchor; with no
 * channel registered it is withheld and stor98's MD-1 gone deadline
 * stands.
 */
static ULONG cmU4LinkDown(PVOID context, const TBT_U4_LINKDOWN *info)
{
    PCM_NHI nhi;
    ULONG r;

    nhi = (PCM_NHI)context;
    nhi->Counters.U4LinkDowns++;
    CmLogNote(nhi, "u4.linkdown",
              ((info->Port & 0xFFUL) << 24) |
                  ((info->Generation & 0xFFFFUL) << 8) |
                  (info->PathClosed ? 0x01UL : 0) |
                  (info->AnchorValid ? 0x02UL : 0) |
                  (info->Nested ? 0x04UL : 0) |
                  (info->MapValid ? 0x08UL : 0));
    if (info->PathClosed) {
        nhi->Counters.U4PathsClosed++;
        if (info->Port >= 1 && info->Port <= XHCI_TBT_MAX_PORTS) {
            XhciTbtLearnDown(&nhi->Binds, info->Port);
            nhi->ArmOwed = 1;
        }
        if (info->Port >= 1 && info->Port <= TBT_U4_MAX_PORTS) {
            /* Kept for the drain: a TUNNEL_UP of this generation still
             * queued is stale (cmU4Drain). */
            nhi->U4ClosedGen[info->Port - 1] = info->Generation;
            nhi->U4ClosedValid |= 1UL << (info->Port - 1);
        }
    }
    if (nhi->U4DownHook != NULL) {
        if (info->Port >= 1 && info->Port <= TBT_U4_MAX_PORTS &&
            nhi->U4UpGen[info->Port - 1] == info->Generation) {
            nhi->U4UpValid &= ~(1UL << (info->Port - 1));
        }
        r = nhi->U4DownHook(nhi->U4HookContext, info);
        if (r == TBT_U4_STMT_PENDING) {
            return TBT_U4_STMT_PENDING;
        }
        return r == TBT_U4_STMT_SENT ? TBT_U4_STMT_SENT
                                     : TBT_U4_STMT_WITHHELD;
    }
    nhi->Counters.U4NoChannel++;
    return TBT_U4_STMT_WITHHELD;
}

VOID CmNhiSetU4Hooks(PCM_NHI nhi, CM_U4_UP_HOOK up, CM_U4_DOWN_HOOK down,
                     PVOID context)
{
    nhi->U4UpHook = up;
    nhi->U4DownHook = down;
    nhi->U4HookContext = context;
}

/*
 * The per-port eject (record 22 section 10.4, decision U9; task 40-A.9),
 * the re-enumeration seam's hook: called on the thread once the children
 * of tunnel port `bdf` were removed in an orderly way - on NT a
 * query-remove then a remove, on Windows 98 and ME only where CONFIGMG's
 * Query_Remove_SubTree was granted (a refusal is respected: the seam never
 * forces a removal, the run brief's addendum 10, and this hook is then
 * never called). The port whose bridge corresponds to `bdf` (named or
 * learned) has its tunnels torn down in the orderly way of section 9.3
 * step 1 for PCIe alone (TbtU4EjectPcie: a USB3 tunnel on the port, the
 * host xHCI's devices, stays up), and stays down until its device is
 * unplugged and
 * plugged again; the TUNNEL_DOWN it queues is drained at the next pass, not
 * here (the seam is the caller). A bridge no port corresponds to, or a
 * domain not running: the tunnel stays up and the user unplugs, as under
 * the ICM client (record 21 section 7.4).
 */
ULONG CmNhiMemberGen(PCM_NHI nhi, ULONG member)
{
    const TBT_U4_PORT *p;
    ULONG i;

    if (!nhi->U4On || !cmTopoUsable(nhi)) {
        return 0;
    }
    for (i = 1; i <= nhi->U4.PortCount; i++) {
        if (XhciTbtPortMember(&nhi->Reenum.Topo, &nhi->Binds, i) == member) {
            p = TbtU4Port(&nhi->U4, i);
            return p != NULL ? p->Generation : 0;
        }
    }
    return 0;
}

ULONG CmNhiMemberPort(PCM_NHI nhi, ULONG member)
{
    ULONG i;

    if (!nhi->U4On || !cmTopoUsable(nhi)) {
        return 0;
    }
    for (i = 1; i <= nhi->U4.PortCount; i++) {
        if (XhciTbtPortMember(&nhi->Reenum.Topo, &nhi->Binds, i) == member) {
            return i;
        }
    }
    return 0;
}

/* The teardown itself; TRUE when the PCIe path was torn down and read back
 * closed (TbtU4EjectPcie answered TBT_OK). */
static BOOLEAN cmU4EjectBridge(PCM_NHI nhi, ULONG bdf)
{
    const TBT_U4_PORT *p;
    ULONG member;
    ULONG port;
    ULONG i;
    LONG rc;

    nhi->Counters.U4Ejects++;
    CmLogNote(nhi, "u4.eject.bridge", bdf);
    if (cmU4Contained(nhi)) {
        /* Record 22 section 7.7: no request after the loss. */
        nhi->Counters.U4ContainRefused++;
        CmLogNote(nhi, "u4.eject.contained", nhi->Live.State);
        return FALSE;
    }
    if (!nhi->U4On || !CmNhiRunning(nhi) || nhi->Suspended ||
        nhi->DevicePower != PowerDeviceD0) {
        nhi->Counters.U4EjectsUnmapped++;
        CmLogNote(nhi, "u4.eject.not-running", nhi->Stage);
        return FALSE;
    }
    port = 0;
    member = XHCI_TBT_NO_MEMBER;
    if (cmTopoUsable(nhi)) {
        for (i = 0; i < nhi->Reenum.Topo.Tunnels && i < XHCI_TBT_MAX_TUNNELS;
             i++) {
            if (nhi->Reenum.Topo.Set[i] == bdf) {
                member = i;
            }
        }
    }
    if (member != XHCI_TBT_NO_MEMBER) {
        for (i = 1; i <= nhi->U4.PortCount; i++) {
            if (XhciTbtPortMember(&nhi->Reenum.Topo, &nhi->Binds, i) ==
                member) {
                port = i;
                break;
            }
        }
    }
    if (port == 0) {
        nhi->Counters.U4EjectsUnmapped++;
        CmLogNote(nhi, "u4.eject.unmapped", bdf);
        return FALSE;
    }
    p = TbtU4Port(&nhi->U4, port);
    if (p == NULL || !p->Dev.Present || !p->Pcie.Up ||
        (nhi->U4SeamValid & (1UL << (port - 1))) == 0 ||
        nhi->U4SeamGen[port - 1] != p->Generation) {
        /* The connection whose children were removed is not the one on
         * the port now (an unplug and re-plug, or a change not yet
         * delivered to the seam): its successor is never torn down. */
        nhi->Counters.U4EjectsUnmapped++;
        CmLogNote(nhi, "u4.eject.stale", (port << 16) |
                                             (p != NULL ? (p->Generation & 0xFFFFUL) : 0));
        return FALSE;
    }
    /* The PCIe tunnel alone: a USB3 tunnel on the same port carries the
     * host xHCI's devices, which this eject never asked to stop. */
    rc = TbtU4EjectPcie(&nhi->U4, port);
    CmLogNote(nhi, "u4.eject", (port << 8) | ((ULONG)(-rc) & 0xFFUL));
    if (rc != TBT_OK) {
        nhi->Counters.U4EjectsFailed++;
    }
    return (BOOLEAN)(rc == TBT_OK);
}

static VOID cmU4Eject(PVOID context, ULONG bdf)
{
    (VOID)cmU4EjectBridge((PCM_NHI)context, bdf);
}

BOOLEAN CmNhiU4EjectMember(PCM_NHI nhi, ULONG bdf)
{
    /* The 9x eject transaction's teardown (task 43.18; record 21 section
     * 7.2 part 1): the NT eject hook's own path, its answer kept. */
    if (!nhi->U4On) {
        return FALSE;
    }
    return cmU4EjectBridge(nhi, bdf);
}

/* The domain's state at stop, port by port (record 22 section 12's
 * snapshot through the log): the host router, then each USB4 port's state,
 * reason, generation, router and tunnels. */
static VOID cmU4LogPorts(PCM_NHI nhi)
{
    const TBT_U4_PORT *p;
    ULONG i;

    CmLogNote(nhi, "u4.host.id",
              (nhi->U4.HostRouter.Vendor << 16) | nhi->U4.HostRouter.Device);
    CmLogNote(nhi, "u4.host.version", nhi->U4.HostRouter.Usb4Version);
    CmLogNote(nhi, "u4.host.ports", nhi->U4.PortCount);
    CmLogNote(nhi, "u4.stage", nhi->U4.Stage);
    CmLogNote(nhi, "u4.inert-reason", nhi->U4.InertReason);
    for (i = 1; i <= nhi->U4.PortCount; i++) {
        p = TbtU4Port(&nhi->U4, i);
        if (p == NULL) {
            continue;
        }
        CmLogNote(nhi, "u4.port",
                  (p->Index << 24) | ((p->State & 0xFFUL) << 16) |
                      ((p->Why & 0xFFUL) << 8) | (p->Lane0 & 0xFFUL));
        CmLogNote(nhi, "u4.port.gen", p->Generation);
        CmLogNote(nhi, "u4.port.adapters",
                  ((p->PcieDown & 0xFFUL) << 8) | (p->Usb3Down & 0xFFUL));
        CmLogNote(nhi, "u4.port.map",
                  p->MapValid ? (0x80000000UL | (p->MapRid & 0xFFFFUL)) : 0);
        CmLogNote(nhi, "u4.port.member",
                  cmTopoUsable(nhi)
                      ? XhciTbtPortMember(&nhi->Reenum.Topo, &nhi->Binds, i)
                      : XHCI_TBT_NO_MEMBER);
        CmLogNote(nhi, "u4.port.tunnels",
                  (p->Pcie.Up ? 0x01UL : 0) | (p->Usb3.Up ? 0x02UL : 0) |
                      (p->Pcie.Preserved ? 0x04UL : 0) |
                      (p->Usb3.Preserved ? 0x08UL : 0) |
                      (p->DpPreserved ? 0x10UL : 0) |
                      (p->Ejected ? 0x20UL : 0) |
                      (p->PcieFailed ? 0x40UL : 0));
        cmU4LogRouter(nhi, i);
    }
}

/*
 * The USB4 profile's activation, on the thread, once TbtHwSelect admitted
 * the software connection manager (record 22 section 3.4): the common
 * buffer for its sixteen-entry rings, the Bus Master bit, the core
 * initialised with the values of section 12 and the selection's Host
 * Interface Capabilities, the statement upcall bound, the correspondence
 * cleared. Nothing is written to the host interface here: the ring
 * takeover and everything after it are the bind's (TbtU4Start), after the
 * PnP path is released. A failure here is the inert start before
 * activation.
 */
static VOID cmU4Activate(PCM_NHI nhi)
{
    CmLogNote(nhi, "u4.select.route", nhi->Sel.Route);
    CmLogNote(nhi, "u4.select.experimental", nhi->Sel.Experimental ? 1UL : 0);
    CmLogNote(nhi, "u4.select.forced", nhi->Sel.Forced ? 1UL : 0);
    nhi->DmaBytes = TBT_RING_U4_DMA_BYTES;
    if (!NT_SUCCESS(CmDmaOpen(nhi))) {
        cmInert(nhi, CM_INERT_RESOURCES);
        return;
    }
    if (!CmNhiSetBusMaster(nhi, TRUE)) {
        cmInert(nhi, CM_INERT_BUS_MASTER);
        return;
    }
    TbtU4DefaultConfig(&nhi->U4Cfg);
    nhi->U4Cfg.Approve = (BOOLEAN)(nhi->Values.Value[CM_VAL_APPROVE] != 0);
    nhi->U4Cfg.Usb3Tunnel =
        (BOOLEAN)(nhi->Values.Value[CM_VAL_USB3_TUNNEL] != 0);
    nhi->U4Cfg.LaneBonding =
        (BOOLEAN)(nhi->Values.Value[CM_VAL_LANE_BONDING] != 0);
    nhi->U4Cfg.PreOsCleanup = nhi->Values.Value[CM_VAL_PREOS_CLEANUP];
    nhi->U4Cfg.RingFlags = TbtHwRingFlags(nhi->Hw);
    nhi->U4Cfg.HostInterfaceCaps = nhi->Sel.Caps;
    /* The bind takes the tunnel ports' learning baseline between the
     * start and the first tick, before any cold device is connected. */
    nhi->U4Cfg.DeferFirstPoll = TRUE;
    TbtU4Init(&nhi->U4, &nhi->Ring, &nhi->Host, &nhi->U4Cfg);
    TbtU4SetLinkDown(&nhi->U4, cmU4LinkDown, nhi);
    /* The stor98 channel's two moments (task 40-A.7, cm_nvlink.h). */
    CmNhiSetU4Hooks(nhi, CmNvlinkU4Up, CmNvlinkU4Down, nhi);
    XhciTbtBindsInit(&nhi->Binds);
    nhi->ArmOwed = 0;
    nhi->U4On = 1;
    /* The liveness monitor's USB4 rule (record 22 section 7.7): a loss
     * contains, never recovers. Armed by the bind. */
    nhi->Live.Usb4 = 1;
    nhi->Inert = CM_INERT_NONE;
    nhi->Stage = CM_STAGE_ACTIVE;
    nhi->BindOwed = 1;
}

/*
 * The USB4 profile's bind, on the thread after the PnP path was released
 * (record 22 section 4.4 steps 2 to 6): TbtU4Start - ring 0 taken over,
 * never reset; test (d)'s router half; the host router; the discovery and
 * preservation of section 9.2 (or the user's cleaning of 9.3); hot plug
 * enabled - but no port found empty connected yet (DeferFirstPoll). Then
 * the re-enumeration seam started, now that the router has named what it
 * names (CmNhiNamePorts runs inside its discovery), the eject entry
 * registered, the learning baseline taken while no tunnel this instance
 * builds is up, and only then the first tick, which connects the cold
 * devices, and the drain of what it queued.
 */
static VOID cmU4Bind(PCM_NHI nhi)
{
    const TBT_U4_PORT *p;
    LONG rc;
    ULONG i;
    ULONG v;

    nhi->Counters.U4Starts++;
    rc = TbtU4Start(&nhi->U4, nhi->CommonVa, nhi->CommonPa.LowPart,
                    nhi->DmaBytes);
    CmLogNote(nhi, "u4.start", (ULONG)(-rc));
    if (rc != TBT_OK) {
        if (nhi->U4.Failed || nhi->Ring.Running || nhi->Ring.DmaMayRun) {
            /* Failure after activation (record 22 section 3.5): the core
             * stopped its rings; tunnels are left as found. Unless the
             * host interface itself is gone (section 7.7): all ones, the
             * gate revoked, or a size other than the one programmed -
             * then contained, nothing announced yet. */
            nhi->Stage = CM_STAGE_FAILED;
            CmLogNote(nhi, "u4.failed", nhi->U4.InertReason);
            if (!CmNhiHwAccess(nhi) ||
                CmThrProbe32(nhi, TBT_RX_RING(0) + TBT_RING_SIZE_REG) !=
                    CM_U4_RX_SIZE_VALUE) {
                /* The loss-interrupted bind (decision U27): recovered,
                 * then its remaining steps run after the commit. */
                nhi->U4LossFrom = 1;
                CmNhiU4Contain(nhi);
            }
            return;
        }
        cmInert(nhi, CM_INERT_RING_REFUSED);
        return;
    }
    CmLogNote(nhi, "u4.host.id",
              (nhi->U4.HostRouter.Vendor << 16) | nhi->U4.HostRouter.Device);
    CmLogNote(nhi, "u4.host.ports", nhi->U4.PortCount);
    for (i = 1; i <= nhi->U4.PortCount; i++) {
        p = TbtU4Port(&nhi->U4, i);
        if (p != NULL) {
            CmLogNote(nhi, "u4.port.start",
                      (p->Index << 24) | ((p->State & 0xFFUL) << 16) |
                          ((p->PcieDown & 0xFFUL) << 8) |
                          (p->MapValid ? 1UL : 0));
        }
    }
    /* Monitored from here, against the size register's value now (record
     * 21 section 5.5): sixteen entries, 256-byte buffers. */
    v = CmThrProbe32(nhi, TBT_RX_RING(0) + TBT_RING_SIZE_REG);
    CmLogNote(nhi, "cm.live.baseline", v);
    if (v != CM_U4_RX_SIZE_VALUE) {
        /* Record 22 section 7.7: the host interface went between the ring
         * start and this read - all ones, or not what was programmed.
         * Nothing was announced yet, so nothing is reconciled. The
         * loss-interrupted bind (U27): its remaining steps after a
         * recovery's commit. */
        nhi->U4LossFrom = 1;
        CmNhiU4Contain(nhi);
        return;
    }
    (VOID)CmLiveArm(&nhi->Live, v, nhi->Host.NowMs(nhi));
    cmU4BindRest(nhi);
}

/* The bind's steps after the monitor is armed: the re-enumeration seam
 * started, the eject entry, the learning baseline and marks, the first
 * tick. A recovery whose loss interrupted the bind runs them after its
 * commit (record 22 section 7.7 part 3 step 7). */
static VOID cmU4BindRest(PCM_NHI nhi)
{
    const TBT_U4_PORT *p;
    ULONG i;

    CmReenumStart(nhi);
    nhi->ReenumOn = 1;
    cmU4SetRecord(nhi);
    CmReenumSetEject(nhi, cmU4Eject, nhi);
    cmU4Arm(nhi);
    /* A tunnel found up at start (preserved) queues no TUNNEL_UP, and its
     * endpoint may start answering only after the baseline: unless its
     * bridge is already known, it is pending from here, so its gain is
     * never attributed to another port, until its path is proven closed
     * (record 22 section 7.6; tbt_topo.h). */
    /* The same for any port whose PCIe paths may be live without being
     * verified: HELD (a discovery that could not read or did not verify
     * everything), FAILED, a residual or an incomplete inventory - every
     * port the start did not leave EMPTY. With DeferFirstPoll, a port found
     * empty is still EMPTY here; only those carry no uncertainty. */
    for (i = 1; i <= nhi->U4.PortCount && i <= XHCI_TBT_MAX_PORTS; i++) {
        p = TbtU4Port(&nhi->U4, i);
        if (p == NULL) {
            continue;
        }
        if (p->State != TBT_U4_EMPTY || p->Pcie.Up || p->Pcie.Written != 0 ||
            p->Pcie.Incomplete || p->HostResidual || p->AsFoundOwed) {
            XhciTbtLearnUp(&nhi->Binds, i);
        }
        if (p->Pcie.Up) {
            nhi->U4SeamGen[i - 1] = p->Generation;
            nhi->U4SeamValid |= 1UL << (i - 1);
        }
    }
    CmNhiPollPass(nhi);
}

/* The USB4 profile's counters and port snapshot, at stop. */
static VOID cmU4LogCounters(PCM_NHI nhi)
{
    cmLogBlock(nhi, cmU4CounterNames, (const ULONG *)&nhi->U4.Counters,
               sizeof(TBT_U4_COUNTERS) / sizeof(ULONG));
    cmU4LogPorts(nhi);
}

/* The blocks cmLogCounters writes at stop, as the query device's log
 * answer carries them after each pass (design record 25 section 4.5). */
VOID CmNhiCounterLines(PCM_NHI nhi, UCHAR *out, ULONG room, PULONG at,
                       PULONG cut)
{
    (VOID)CmQueryLines(out, room, at, cmNhiCounterNames,
                       (const ULONG *)&nhi->Counters,
                       sizeof(CM_COUNTERS) / sizeof(ULONG), cut);
    if (!nhi->U4On && nhi->Icm.Ring == NULL) {
        return;
    }
    (VOID)CmQueryLines(out, room, at, cmRingCounterNames,
                       (const ULONG *)&nhi->Ring.Counters,
                       sizeof(TBT_RING_COUNTERS) / sizeof(ULONG), cut);
    if (nhi->U4On) {
        (VOID)CmQueryLines(out, room, at, cmU4CounterNames,
                           (const ULONG *)&nhi->U4.Counters,
                           sizeof(TBT_U4_COUNTERS) / sizeof(ULONG), cut);
    } else {
        (VOID)CmQueryLines(out, room, at, cmIcmCounterNames,
                           (const ULONG *)&nhi->Icm.Counters,
                           sizeof(TBT_ICM_COUNTERS) / sizeof(ULONG), cut);
    }
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
    /* A new start, new resources: the monitor begins OFF, unrevoked. */
    CmLiveInit(&nhi->Live, 0, nhi->Values.Value[CM_VAL_POLL_IDLE_MS]);
    nhi->AccessClass = CM_ACCESS_ORDINARY;
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
    nhi->LiveIds = v;
    CmLogNote(nhi, "cm.cfg.ids", v);
    /* BAR0 as the healthy start found it, for a recovery's precondition:
     * the bus address in configuration space, both DWs of a 64-bit BAR. */
    nhi->LiveBar0 = CmCfgRead32(nhi, 0x10);
    nhi->LiveBar0Hi = 0;
    if ((nhi->LiveBar0 & 0x6UL) == 0x4UL) {
        nhi->LiveBar0Hi = CmCfgRead32(nhi, 0x14);
    }
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
        if (mode == CM_MODE_FORCE_ICM) {
            /* XhciCmMode 2 asks for the ICM client: a host whose ICM is
             * not running is inert, never the software CM. */
            cmInert(nhi, CM_INERT_FORCE_ICM_REFUSED);
            return;
        }
        cmU4Activate(nhi);
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
    /* Record 21 section 5.3: on every row, each announced router's USB4
     * Version is read - the generation is the router's, whatever the
     * host (the ORICO behind Alpine Ridge reads 20h). */
    TbtIcmSetReadVersion(&nhi->Icm, TRUE);
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
    if (nhi->U4On) {
        cmU4Bind(nhi);
        return;
    }
    rc = TbtIcmStart(&nhi->Icm, nhi->CommonVa, nhi->CommonPa.LowPart,
                     TBT_RING_DMA_BYTES);
    CmLogNote(nhi, "cm.bind", (ULONG)(-rc));
    if (rc == TBT_OK && !CmNhiLiveArm(nhi)) {
        /* Bound, then not answering (cm_live.h): the failure after
         * activation; the rings stop with whatever verification they get. */
        TbtIcmUnbind(&nhi->Icm);
        nhi->Icm.Failed = TRUE;
        CmNhiLost(nhi, CmLiveEpisode(&nhi->Live), TRUE);
        rc = TBT_E_HW;
    }
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
    if (cmU4Contained(nhi)) {
        /* A surprise removal (the gate revoked) or a loss not yet acted
         * on: the driver's containment, the stor98 channel told, before
         * the seam closes; no reconciliation from inside the PnP request. */
        CmNhiU4ContainEx(nhi, FALSE);
    }
    if (nhi->ReenumOn) {
        CmReenumStop(nhi);
        nhi->ReenumOn = 0;
    }
    if (nhi->U4On) {
        if (powered && cmU4Contained(nhi)) {
            /* Record 22 section 7.7: the unbind writes nothing, and neither
             * does the rest of the stop - no Bus Master bit, no force
             * power - for a host interface this driver can no longer read. */
            CmNhiUnbind(nhi);
            CmLogNote(nhi, "cm.unbind.no-access", nhi->Live.State);
            powered = FALSE;
        } else if (powered) {
            CmNhiUnbind(nhi);
        } else {
            /* Out of D0 the suspend stopped the rings (TbtU4Suspend). */
            CmLogNote(nhi, "cm.unbind.unpowered", nhi->DevicePower);
        }
        CmNhiDrainActions(nhi);
    } else if (nhi->Icm.Ring != NULL) {
        if (powered && !CmNhiHwAccess(nhi)) {
            /* The liveness gate is closed (record 21 section 5.6): no ring
             * stop and no 0x07 to a host interface that stopped answering
             * or was removed; the buffer is kept unless a stop was
             * verified (cm_pool.c). */
            CmLogNote(nhi, "cm.unbind.no-access", nhi->Live.State);
            powered = FALSE;
        } else if (powered) {
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
    CmLogNote(nhi, "cm.live.final", (CmLiveEpisode(&nhi->Live) << 8) |
                                        nhi->Live.State);
    CmLogNote(nhi, "cm.live.refused",
              (nhi->Live.Counters.RefusedWrites << 16) |
                  (nhi->Live.Counters.RefusedReads & 0xFFFFUL));
    CmDmaClose(nhi);
    if (powered && active) {
        (VOID)CmNhiSetBusMaster(nhi, FALSE);
        CmNhiPlatformRelease(nhi, CmNhiMayClearPower(nhi));
    } else if (nhi->BusMasterSetHere) {
        /* An inert stop writes nothing: a bit set before the start went
         * inert (its verification, or the ring's refusal) stays set. */
        CmLogNote(nhi, "cm.bus-master.kept", nhi->Stage);
    }
    if (nhi->BarVa != NULL) {
        MmUnmapIoSpace(nhi->BarVa, nhi->BarLength);
        nhi->BarVa = NULL;
    }
    CmLiveDisarm(&nhi->Live);
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
    CmStatusOpen(nhi);
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
    CmLogNote(nhi, "cm.usb3.tunnel", nhi->Values.Value[CM_VAL_USB3_TUNNEL]);
    CmLogNote(nhi, "cm.lane.bonding", nhi->Values.Value[CM_VAL_LANE_BONDING]);
    CmLogNote(nhi, "cm.preos.cleanup",
              nhi->Values.Value[CM_VAL_PREOS_CLEANUP]);
    CmLogNote(nhi, "cm.bridge.override", nhi->Values.TunnelBridgeChars);
    CmLogNote(nhi, "cm.bridge.refused", nhi->Values.TunnelBridgeRefused);
    /* XhciCmBridgeNumbering as run with; bit 8 found, bit 9 refused (the
     * value read is then the next note's). 9x only: the NT route ignores
     * it. */
    CmLogNote(nhi, "cm.bridge.numbering",
              nhi->Values.BridgeNumbering |
                  (nhi->Values.BridgeNumberingFound << 8) |
                  (nhi->Values.BridgeNumberingRefused << 9));
    if (nhi->Values.BridgeNumberingRefused) {
        CmLogNote(nhi, "cm.bridge.numbering.raw",
                  nhi->Values.BridgeNumberingRaw);
    }

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
    CmZero(&nhi->U4, sizeof(nhi->U4));
    CmZero(&nhi->Binds, sizeof(nhi->Binds));
    nhi->U4On = 0;
    nhi->ArmOwed = 0;
    nhi->U4ClosedValid = 0;
    nhi->U4SeamValid = 0;
    nhi->U4ResumeBoundMs = 0;
    nhi->U4ClearOk = 0;
    nhi->U4ContainDone = 0;
    nhi->U4UpValid = 0;
    nhi->U4BatchCount = 0;
    nhi->U4LossFrom = 0;
    nhi->RecSetCount = 0;
    nhi->RecLostUp = 0;
    nhi->RecCmdSet = 0;
    nhi->RecBarred = 0;
    nhi->RecDebtMask = 0;
    nhi->RecChanged = 0;
    nhi->RecBusChanged = 0;
    nhi->RecBusPend = 0;
    nhi->RecRelOk = 0;
    CmZero(nhi->RecOldBuses, sizeof(nhi->RecOldBuses));
    nhi->RecRetireSent = 0;
    nhi->RecUpValid = 0;
    nhi->DmaBytes = 0;
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
        CmQueryOpen(nhi);
        return STATUS_SUCCESS;
    }
    CmThrRunPnp(nhi, CM_WORK_START);
    if (!NT_SUCCESS(nhi->StartStatus)) {
        nhi->PnpStopPending = 1;
        CmThrRunPnp(nhi, CM_WORK_STOP);
        CmThrStop(nhi);
        nhi->PnpStopPending = 0;
        nhi->Started = 0;
        return nhi->StartStatus;
    }
    CmQueryOpen(nhi);
    return STATUS_SUCCESS;
}

/* The query device (cm_qryos.c) is told first and deleted once the
 * thread has ended: its answers say "stopping" meanwhile. */
static VOID cmNhiStop(PCM_NHI nhi, ULONG reason)
{
    CmQueryStopping(nhi);
    if (!nhi->Started) {
        CmQueryClose(nhi);
        return;
    }
    if (nhi->ThreadRunning) {
        /* Before the work is queued, so work the thread already took
         * (a resume, a bind) sees the stop waiting (CmNhiU4ContainEx). */
        nhi->PnpStopPending = 1;
        CmThrRunPnp(nhi, CM_WORK_STOP);
        CmThrStop(nhi);
        nhi->PnpStopPending = 0;
    }
    CmQueryClose(nhi);
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
        if (stack->MinorFunction == IRP_MN_SURPRISE_REMOVAL) {
            /* The function is gone: no access of any class from now, even
             * inside a sequence the thread is running (cm_live.h). */
            CmNhiRevoke(nhi);
        }
        cmNhiStop(nhi, stack->MinorFunction);
        irp->IoStatus.Status = STATUS_SUCCESS;
        break;

    case IRP_MN_REMOVE_DEVICE:
        cmNhiStop(nhi, stack->MinorFunction);
        CmStatusClose(nhi);
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
