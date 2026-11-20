/*
 * xhci_tol.c - controller tolerance, its pure half (roadmap-hcd.md tasks
 * 35-T.1 to 35-T.9, design record 17). See xhci_tol.h.
 *
 * DDK-free: part of the pure core. C89. IRQL: any.
 */

#include "xhci.h"
#include "xhci_tol.h"

#define XHCI_TOL_VENDOR_AMD         0x1022UL
/* Task 38.10 (design record 20 section 4.10): the ATI Navi10 USB-C
 * controller, Linux's one XHCI_LIMIT_ENDPOINT_INTERVAL_9 id outside vendor
 * 1022 (xhci-pci.c:304-306), under XhciIntervalCap's value 1. */
#define XHCI_TOL_ATI_NAVI10_XHCI    0x73161002UL

ULONG XhciTolMode(ULONG found, ULONG value)
{
    return (found && value == 0) ? 0UL : 1UL;
}

ULONG XhciTolCapMode(ULONG found, ULONG value)
{
    if (found && (value == XHCI_TOL_CAP_OFF || value == XHCI_TOL_CAP_ALL)) {
        return value;
    }
    return XHCI_TOL_CAP_GATED;
}

ULONG XhciTolAvgTrbMode(ULONG found, ULONG value)
{
    return (found && value == 1) ? 1UL : 0UL;
}

/* Unsigned subtraction, so a count that wrapped between the two reads still
 * measures; the clock wraps after thirteen years, a stamp never lives so
 * long. */
ULONG XhciTolElapsed(ULONG now, ULONG stamp, ULONG ticks)
{
    return ((ULONG)(now - stamp) >= ticks) ? 1UL : 0UL;
}

ULONG XhciTolCapApplies(ULONG capMode, ULONG vendorDevice)
{
    if (capMode == XHCI_TOL_CAP_ALL) {
        return 1;
    }
    if (capMode == XHCI_TOL_CAP_GATED) {
        return ((vendorDevice & 0xFFFFUL) == XHCI_TOL_VENDOR_AMD ||
                vendorDevice == XHCI_TOL_ATI_NAVI10_XHCI) ? 1UL : 0UL;
    }
    return 0;
}

ULONG XhciTolCapInterval(ULONG applies, ULONG isInterrupt, ULONG interval)
{
    if (applies && isInterrupt &&
        interval > XHCI_TOL_INTERVAL_CAP) {
        return XHCI_TOL_INTERVAL_CAP;
    }
    return interval;
}

ULONG XhciTolTiCapInterval(ULONG applies, ULONG isInterrupt,
                           ULONG highOrAbove, ULONG interval)
{
    if (applies && isInterrupt && highOrAbove &&
        interval > XHCI_TOL_TI_INTERVAL_CAP) {
        return XHCI_TOL_TI_INTERVAL_CAP;
    }
    return interval;
}

ULONG XhciTolAvgTrbLength(ULONG avgMode, ULONG isInterrupt,
                          ULONG maxEsitPayload)
{
    if (avgMode != 1 || !isInterrupt) {
        return XHCI_TOL_AVG_TRB_DEFAULT;
    }
    if (maxEsitPayload == 0) {
        return 1;
    }
    if (maxEsitPayload > 0xFFFFUL) {
        return 0xFFFFUL;
    }
    return maxEsitPayload;
}

/* Linux's XHCI_NO_SOFT_RETRY list (xhci-pci.c): two AMD Promontory-A ids and
 * two Etron ids. PCI offset 0's dword: device 31:16, vendor 15:0. */
ULONG XhciTolNoSoftRetry(ULONG vendorDevice)
{
    switch (vendorDevice) {
    case 0x43B91022UL:
    case 0x43BB1022UL:
    case 0x70231B6FUL:
    case 0x70521B6FUL:
        return 1;
    default:
        return 0;
    }
}

ULONG XhciTolRetryScope(ULONG tolerance, ULONG vendorDevice, ULONG bulkOrInt,
                        ULONG behindTt)
{
    if (!tolerance || !bulkOrInt || behindTt) {
        return 0;
    }
    return XhciTolNoSoftRetry(vendorDevice) ? 0UL : 1UL;
}

ULONG XhciTolQemuStreamQuirk(ULONG vendorDevice)
{
    return (vendorDevice == XHCI_TOL_QEMU_XHCI_ID) ? 1UL : 0UL;
}

ULONG XhciTolRetryDivert(ULONG inScope, ULONG code, ULONG isHead,
                         ULONG retriesUsed)
{
    return (inScope && code == XHCI_CC_USB_TRANSACTION_ERROR && isHead &&
            retriesUsed < XHCI_TOL_SOFT_RETRIES) ? 1UL : 0UL;
}

ULONG XhciTolRetryExhausted(ULONG inScope, ULONG code, ULONG isHead,
                            ULONG retriesUsed)
{
    return (inScope && code == XHCI_CC_USB_TRANSACTION_ERROR && isHead &&
            retriesUsed >= XHCI_TOL_SOFT_RETRIES) ? 1UL : 0UL;
}

ULONG XhciTolRetryDecide(ULONG tolerance, ULONG opPending, ULONG drainPending,
                         ULONG headIsTd)
{
    if (!tolerance || opPending || drainPending || !headIsTd) {
        return XHCI_TOL_RETRY_REPLAY;
    }
    return XHCI_TOL_RETRY_RESET;
}

ULONG XhciTolRetryAfterReset(ULONG commandOk, ULONG genNow,
                             ULONG genDecided)
{
    if (!commandOk) {
        return XHCI_TOL_RETRY_FAULT;
    }
    return (genNow == genDecided) ? XHCI_TOL_RETRY_RING : XHCI_TOL_RETRY_LEAVE;
}

ULONG XhciTolCycleOnRefused(ULONG tolerance, ULONG claimed, ULONG isoch,
                            ULONG hasDevice)
{
    return (tolerance && !claimed && !isoch && hasDevice) ? 1UL : 0UL;
}

ULONG XhciTolHaltCandidate(ULONG tolerance, ULONG code, ULONG isoch,
                           ULONG pipeOpen)
{
    if (!tolerance || isoch || !pipeOpen) {
        return 0;
    }
    switch (code) {
    case XHCI_CC_STALL:
    case XHCI_CC_USB_TRANSACTION_ERROR:
    case XHCI_CC_BABBLE:
    case XHCI_CC_SPLIT_TRANSACTION:
        return 1;
    default:
        return 0;
    }
}

ULONG XhciTolStreamHaltDrain(ULONG code, ULONG eventData)
{
    if (eventData) {
        return 0;
    }
    switch (code) {
    case XHCI_CC_STALL:
    case XHCI_CC_USB_TRANSACTION_ERROR:
    case XHCI_CC_BABBLE:
    case XHCI_CC_SPLIT_TRANSACTION:
    /* A device's ERDY or Prime Pipe for a stream the host holds no ring
     * for (xHCI 4.12.2.1): no TD ran, and Linux recovers the endpoint for
     * both (xhci-ring.c, handle_transferless_tx_event). A user's FL1100
     * gave three on a UAS status pipe that nothing recovered (2026-11-05,
     * issue 24). */
    case XHCI_CC_INVALID_STREAM_ID:
    case XHCI_CC_INVALID_STREAM_TYPE:
        return 1;
    default:
        return 0;
    }
}

ULONG XhciTolHaltConfirmed(ULONG epState)
{
    return (epState == XHCI_EP_STATE_HALTED ||
            epState == XHCI_EP_STATE_ERROR) ? 1UL : 0UL;
}

ULONG XhciTolCmdVerdict(ULONG code)
{
    return (code == XHCI_CC_COMMAND_ABORTED || code == XHCI_TOL_CMD_LEFT)
               ? 1UL
               : 0UL;
}

ULONG XhciTolFrozenAdmits(ULONG type, ULONG teardown)
{
    return (teardown && (type == XHCI_TRB_TYPE_STOP_EP ||
                         type == XHCI_TRB_TYPE_DISABLE_SLOT))
               ? 1UL
               : 0UL;
}

ULONG XhciTolFaultStep(ULONG marked, ULONG hub, ULONG published,
                       ULONG cycled)
{
    if (!marked) {
        return XHCI_TOL_FAULT_NONE;
    }
    if (hub) {
        return XHCI_TOL_FAULT_RESET;
    }
    if (!published) {
        return XHCI_TOL_FAULT_STEP;
    }
    return cycled ? XHCI_TOL_FAULT_OWED : XHCI_TOL_FAULT_CYCLE;
}

ULONG XhciTolLateAlone(ULONG isolate, ULONG left, ULONG present,
                       ULONG verdict)
{
    return (isolate && !left && present &&
            (verdict == XHCI_TOL_LATE_CYCLE ||
             verdict == XHCI_TOL_LATE_LEFT)) ? 1UL : 0UL;
}

ULONG XhciTolMarkedFails(ULONG debtClear, ULONG gone, ULONG failedAlone,
                         ULONG cmdLeft, ULONG abortVerdict,
                         ULONG admitRefused)
{
    return (debtClear && !gone && !failedAlone &&
            (cmdLeft != 0 || abortVerdict != 0 || admitRefused != 0))
               ? 1UL
               : 0UL;
}

ULONG XhciTolAdmitOwesReset(ULONG trbType, ULONG hasRecord,
                            ULONG teardown)
{
    return (hasRecord && !teardown && trbType == XHCI_TRB_TYPE_STOP_EP)
               ? 1UL
               : 0UL;
}

ULONG XhciTolAdmitMarks(ULONG trbType, ULONG hasRecord)
{
    if (!hasRecord || trbType == XHCI_TRB_TYPE_ENABLE_SLOT ||
        trbType == XHCI_TRB_TYPE_DISABLE_SLOT ||
        trbType == XHCI_TRB_TYPE_NOOP_COMMAND) {
        return 0;
    }
    return 1;
}

ULONG XhciTolStopRest(const XHCI_TOL_STOP_IO *io, PULONG state,
                      PULONG restops)
{
    ULONG code;
    ULONG s;
    ULONG again;

    *restops = 0;
    code = io->Stop(io->Context);
    for (;;) {
        if (XhciTolCmdVerdict(code)) {
            *state = XHCI_EP_STATE_RUNNING;
            return XHCI_TOL_STOP_ABORTED;
        }
        s = io->State(io->Context);
        *state = s;
        if (code != XHCI_CC_SUCCESS && code != XHCI_CC_CONTEXT_STATE_ERROR) {
            return XHCI_TOL_STOP_GIVEUP;
        }
        again = (s == XHCI_EP_STATE_RUNNING ||
                 (code == XHCI_CC_CONTEXT_STATE_ERROR &&
                  s == XHCI_EP_STATE_STOPPED)) ? 1UL : 0UL;
        if (!again) {
            return XHCI_TOL_STOP_REST;
        }
        if (*restops >= XHCI_TOL_STOP_RESTOPS) {
            return (s == XHCI_EP_STATE_RUNNING) ? XHCI_TOL_STOP_GIVEUP
                                                : XHCI_TOL_STOP_REST;
        }
        (*restops)++;
        io->Wait(io->Context);
        code = io->Stop(io->Context);
    }
}

ULONG XhciTolCycleReason(ULONG tolerance, ULONG claimed, ULONG unattributed,
                         ULONG code, ULONG isoch, ULONG hasDevice,
                         ULONG pipeOpen)
{
    if (XhciTolCycleOnRefused(tolerance, claimed, isoch, hasDevice)) {
        return XHCI_TOL_CYCLE_REFUSED_CODE;
    }
    if (claimed && unattributed && hasDevice &&
        XhciTolHaltCandidate(tolerance, code, isoch, pipeOpen)) {
        return XHCI_TOL_CYCLE_HALT_NO_TD;
    }
    return XHCI_TOL_CYCLE_NONE;
}

VOID XhciTolMarkInit(PXHCI_TOL_MARK mark)
{
    mark->Refused = 0;
    mark->HaltDcis = 0;
    mark->Confirmed = 0;
    mark->Gen = 0;
}

ULONG XhciTolMarkPending(const XHCI_TOL_MARK *mark)
{
    return (mark->Refused || mark->HaltDcis != 0 || mark->Confirmed) ? 1UL
                                                                     : 0UL;
}

ULONG XhciTolMarkSet(PXHCI_TOL_MARK mark, ULONG reason, ULONG dci, ULONG gen)
{
    ULONG was;

    was = XhciTolMarkPending(mark);
    if (reason == XHCI_TOL_CYCLE_REFUSED_CODE) {
        mark->Refused = 1;
    } else if (reason == XHCI_TOL_CYCLE_HALT_NO_TD && dci >= 1 && dci <= 31) {
        mark->HaltDcis |= 1UL << dci;
    } else {
        return 0;
    }
    if (was) {
        return 0;
    }
    mark->Gen = gen;
    return 1;
}

ULONG XhciTolMarkResolve(PXHCI_TOL_MARK mark, ULONG halted)
{
    if ((halted & mark->HaltDcis) != 0) {
        mark->Confirmed = 1;
    }
    mark->HaltDcis = 0;
    if (mark->Refused) {
        return XHCI_TOL_CYCLE_REFUSED_CODE;
    }
    if (mark->Confirmed) {
        return XHCI_TOL_CYCLE_HALT_NO_TD;
    }
    XhciTolMarkInit(mark);
    return XHCI_TOL_CYCLE_NONE;
}

ULONG XhciTolHaltOwner(ULONG epState, ULONG retryHeadDeferred)
{
    if (epState == XHCI_EP_STATE_ERROR) {
        return XHCI_TOL_HALT_CONFIRMED;
    }
    if (epState == XHCI_EP_STATE_HALTED) {
        return retryHeadDeferred ? XHCI_TOL_HALT_RETRY
                                 : XHCI_TOL_HALT_CONFIRMED;
    }
    return XHCI_TOL_HALT_STALE;
}

ULONG XhciTolJoin(ULONG reason, ULONG retryLive)
{
    if (reason != XHCI_TOL_CYCLE_NONE) {
        return XHCI_TOL_JOIN_CYCLE;
    }
    return retryLive ? XHCI_TOL_JOIN_RETRY : XHCI_TOL_JOIN_NONE;
}

ULONG XhciTolCycleAct(ULONG reason, ULONG sameDevice, ULONG markGen,
                      ULONG locGen, ULONG published)
{
    if (reason != XHCI_TOL_CYCLE_REFUSED_CODE &&
        reason != XHCI_TOL_CYCLE_HALT_NO_TD) {
        return XHCI_TOL_CYCLE_ACT_NONE;
    }
    if (!sameDevice || markGen != locGen) {
        return XHCI_TOL_CYCLE_ACT_DROP;
    }
    return published ? XHCI_TOL_CYCLE_ACT_PUBLISHED
                     : XHCI_TOL_CYCLE_ACT_PRE_PDO;
}

ULONG XhciTolCycleReconnect(ULONG charged, ULONG empty, ULONG connected)
{
    return (charged && empty && connected) ? 1UL : 0UL;
}

ULONG XhciTolWaitStep(ULONG done, ULONG marked, ULONG expired)
{
    if (done) {
        return XHCI_TOL_WAIT_DONE;
    }
    if (marked) {
        return XHCI_TOL_WAIT_RESOLVE;
    }
    return expired ? XHCI_TOL_WAIT_TIMEOUT : XHCI_TOL_WAIT_SLEEP;
}

ULONG XhciTolWaitResolved(ULONG reason, ULONG done, ULONG expired)
{
    if (reason != XHCI_TOL_CYCLE_NONE) {
        return XHCI_TOL_WAIT_ABANDON;
    }
    if (done) {
        return XHCI_TOL_WAIT_DONE;
    }
    return expired ? XHCI_TOL_WAIT_TIMEOUT : XHCI_TOL_WAIT_AGAIN;
}

ULONG XhciTolLateStopped(ULONG stopCode, ULONG epState, ULONG done)
{
    /* Task 50.30 step 3 (section 4.15.5a, the hcdEp0Late row): an aborted
     * or left Stop says nothing of whether it ran, and a completion of the
     * transfer says nothing of it either: classified first. */
    if (XhciTolCmdVerdict(stopCode)) {
        return XHCI_TOL_LATE_RESET;
    }
    if (done) {
        return XHCI_TOL_LATE_DONE;
    }
    if (stopCode != XHCI_CC_SUCCESS &&
        stopCode != XHCI_CC_CONTEXT_STATE_ERROR) {
        return XHCI_TOL_LATE_RESET;
    }
    if (epState == XHCI_EP_STATE_STOPPED || epState == XHCI_EP_STATE_ERROR) {
        return XHCI_TOL_LATE_INERT;
    }
    return XHCI_TOL_LATE_RESET;
}

ULONG XhciTolLateVerdict(ULONG left, ULONG present, ULONG hub,
                         ULONG published, ULONG isolate)
{
    if (left) {
        return XHCI_TOL_LATE_LEFT;
    }
    if (isolate) {
        if (hub) {
            return XHCI_TOL_LATE_RESET;
        }
        return published ? XHCI_TOL_LATE_CYCLE : XHCI_TOL_LATE_LEFT;
    }
    if (present || hub) {
        return XHCI_TOL_LATE_RESET;
    }
    return published ? XHCI_TOL_LATE_CYCLE : XHCI_TOL_LATE_LEFT;
}

ULONG XhciTolLateRetired(ULONG verdict, ULONG retired)
{
    if (verdict != XHCI_TOL_LATE_LEFT && verdict != XHCI_TOL_LATE_CYCLE) {
        return verdict;
    }
    return retired ? verdict : XHCI_TOL_LATE_RESET;
}

ULONG XhciTolBackstop(PXHCI_TOL_OBS obs, ULONG tolerance, ULONG pending,
                      ULONG index, ULONG cycle, ULONG drainGen,
                      ULONG startGen, ULONG now)
{
    if (obs == NULL) {
        return 0;
    }
    if (!tolerance || !pending) {
        obs->Valid = 0;
        return 0;
    }
    if (!obs->Valid || obs->Index != index || obs->Cycle != cycle ||
        obs->DrainGen != drainGen || obs->StartGen != startGen) {
        obs->Valid = 1;
        obs->Index = index;
        obs->Cycle = cycle;
        obs->DrainGen = drainGen;
        obs->StartGen = startGen;
        obs->Stamp = now;
        return 0;
    }
    if (!XhciTolElapsed(now, obs->Stamp, XHCI_TOL_BACKSTOP_TICKS)) {
        return 0;
    }
    /* Restamped rather than cleared: a drain the DPC's admission refused
     * leaves the same observation, and it is queued again only after
     * another interval. */
    obs->Stamp = now;
    return 1;
}

VOID XhciTolLocInit(PXHCI_TOL_LOC loc)
{
    if (loc == NULL) {
        return;
    }
    loc->Reenums = 0;
    loc->Repowers = 0;
    loc->Charges = 0;
    loc->Hold = (UCHAR)XHCI_TOL_HOLD_NONE;
    loc->LastCharge = 0;
    loc->Charged = 0;
    loc->DiscArmed = 0;
    loc->DiscStamp = 0;
    loc->DiscSeen = 0;
    loc->Holds = 0;
    loc->ProgArmed = 0;
    loc->ProgStamp = 0;
    loc->RecoveryDisc = 0;
    loc->Rearms = 0;
}

ULONG XhciTolLocCharge(PXHCI_TOL_LOC loc, ULONG kind, ULONG now)
{
    if (loc == NULL || loc->Hold != XHCI_TOL_HOLD_NONE) {
        return 0;
    }
    if (kind == XHCI_TOL_CHARGE_REPOWER) {
        if (loc->Repowers >= XHCI_TOL_REPOWERS) {
            loc->Hold = (UCHAR)XHCI_TOL_HOLD_REPOWERS;
            loc->Holds++;
            return 0;
        }
        loc->Repowers++;
    } else {
        if (loc->Reenums >= XHCI_TOL_REENUMS) {
            loc->Hold = (UCHAR)XHCI_TOL_HOLD_REENUMS;
            loc->Holds++;
            return 0;
        }
        loc->Reenums++;
    }
    loc->Charges++;
    loc->LastCharge = now;
    loc->Charged = 1;
    /* Evidence gathered before the action does not survive it: the
     * disconnect the action itself causes is never a re-arm. */
    loc->DiscArmed = 0;
    loc->DiscSeen = 0;
    loc->ProgArmed = 0;
    return 1;
}

static VOID xhciTolLocRearm(PXHCI_TOL_LOC loc)
{
    loc->Reenums = 0;
    loc->Repowers = 0;
    loc->Charged = 0;
    loc->Rearms++;
}

ULONG XhciTolLocObserveChange(PXHCI_TOL_LOC loc, ULONG connected,
                              ULONG powered, ULONG fault, ULONG changed,
                              ULONG now)
{
    if (loc == NULL) {
        return 0;
    }
    if (!powered || fault) {
        /* CCS means nothing without PP, and a fault is the recovery's own
         * transition. */
        loc->DiscArmed = 0;
        loc->DiscSeen = 0;
        loc->ProgArmed = 0;
        return 0;
    }
    if (!connected) {
        loc->ProgArmed = 0;
        if (loc->RecoveryDisc) {
            /* The recovery's own disconnect, until its device is back. */
            loc->DiscArmed = 0;
            loc->DiscSeen = 0;
            return 0;
        }
        if (!loc->DiscArmed || changed) {
            /* A connection change since the last look: whatever the port
             * did between the looks, it was not disconnected throughout. */
            loc->DiscArmed = 1;
            loc->DiscStamp = now;
            loc->DiscSeen = 0;
        } else if (XhciTolElapsed(now, loc->DiscStamp,
                                  XHCI_TOL_STABLE_DISC_TICKS)) {
            loc->DiscSeen = 1;
        }
        return 0;
    }
    loc->DiscArmed = 0;
    if (loc->RecoveryDisc) {
        /* The connection that ends the recovery's disconnect re-arms
         * nothing; later disconnects are evidence again. */
        loc->RecoveryDisc = 0;
        loc->DiscSeen = 0;
        return 0;
    }
    if (!loc->DiscSeen) {
        return 0;
    }
    loc->DiscSeen = 0;
    xhciTolLocRearm(loc);
    /* A port held unpowered is released only by a controller start; one
     * held because its device stopped answering, as REENUMS (task 50.30
     * step 3, design record 17 section 4.15.4). */
    if (loc->Hold == XHCI_TOL_HOLD_REENUMS ||
        loc->Hold == XHCI_TOL_HOLD_UNANSWERED) {
        loc->Hold = (UCHAR)XHCI_TOL_HOLD_NONE;
    }
    return 1;
}

ULONG XhciTolLocObserve(PXHCI_TOL_LOC loc, ULONG connected, ULONG powered,
                        ULONG fault, ULONG now)
{
    return XhciTolLocObserveChange(loc, connected, powered, fault, 0, now);
}

VOID XhciTolLocRecovery(PXHCI_TOL_LOC loc)
{
    if (loc == NULL) {
        return;
    }
    loc->RecoveryDisc = 1;
    loc->DiscArmed = 0;
    loc->DiscSeen = 0;
    loc->ProgArmed = 0;
}

ULONG XhciTolLocDiscDue(const XHCI_TOL_LOC *loc, ULONG now)
{
    if (loc == NULL || !loc->DiscArmed || loc->DiscSeen ||
        loc->RecoveryDisc) {
        return 0;
    }
    return XhciTolElapsed(now, loc->DiscStamp, XHCI_TOL_STABLE_DISC_TICKS);
}

VOID XhciTolLocHold(PXHCI_TOL_LOC loc, ULONG reason)
{
    if (loc == NULL || reason == XHCI_TOL_HOLD_NONE ||
        loc->Hold == reason) {
        return;
    }
    if (loc->Hold == XHCI_TOL_HOLD_NONE) {
        loc->Holds++;
    }
    loc->Hold = (UCHAR)reason;
    loc->DiscArmed = 0;
    loc->DiscSeen = 0;
    loc->ProgArmed = 0;
}

ULONG XhciTolLocUnpowered(const XHCI_TOL_LOC *loc)
{
    return (loc != NULL && (loc->Hold == XHCI_TOL_HOLD_REPOWERS ||
                            loc->Hold == XHCI_TOL_HOLD_OC_WAIT)) ? 1UL : 0UL;
}

ULONG XhciTolLocActive(ULONG tolerance, const XHCI_TOL_LOC *loc)
{
    if (loc == NULL) {
        return 0;
    }
    return (tolerance || loc->Charged || loc->Hold != XHCI_TOL_HOLD_NONE)
               ? 1UL
               : 0UL;
}

ULONG XhciTolLocHeld(ULONG tolerance, const XHCI_TOL_LOC *loc)
{
    return (XhciTolLocActive(tolerance, loc) &&
            loc->Hold != XHCI_TOL_HOLD_NONE) ? 1UL : 0UL;
}

VOID XhciTolCycleChargeInit(PXHCI_TOL_CYCLE_CHARGE once)
{
    if (once != NULL) {
        once->Gen = 0;
        once->Valid = 0;
        once->Allowed = 0;
    }
}

ULONG XhciTolCycleCharge(PXHCI_TOL_CYCLE_CHARGE once, PXHCI_TOL_LOC loc,
                         ULONG gen, ULONG now)
{
    ULONG allowed;

    if (once == NULL || loc == NULL) {
        return 0;
    }
    if (once->Valid && once->Gen == gen) {
        return once->Allowed;
    }
    allowed = XhciTolLocCharge(loc, XHCI_TOL_CHARGE_REENUM, now);
    once->Gen = gen;
    once->Valid = 1;
    once->Allowed = allowed;
    return allowed;
}

ULONG XhciTolLocProgress(PXHCI_TOL_LOC loc, ULONG now)
{
    if (loc == NULL || loc->Hold != XHCI_TOL_HOLD_NONE || !loc->Charged) {
        return 0;
    }
    if (!loc->ProgArmed) {
        loc->ProgArmed = 1;
        loc->ProgStamp = now;
        return 0;
    }
    if (!XhciTolElapsed(now, loc->ProgStamp,
                        XHCI_TOL_STABLE_PROGRESS_TICKS)) {
        return 0;
    }
    loc->ProgArmed = 0;
    xhciTolLocRearm(loc);
    return 1;
}

ULONG XhciTolPedFault(ULONG tolerance, ULONG usb2, ULONG pec, ULONG ped,
                      ULONG ccs, ULONG hasDevice)
{
    return (tolerance && usb2 && pec && !ped && ccs && hasDevice) ? 1UL : 0UL;
}

ULONG XhciTolOcFault(ULONG tolerance, ULONG occ, ULONG pp, ULONG powered)
{
    return (tolerance && (occ || (!pp && powered))) ? 1UL : 0UL;
}

ULONG XhciTolOffOcCount(ULONG occ, ULONG pp, PULONG lost)
{
    ULONG count;

    count = (occ || (!pp && !*lost)) ? 1UL : 0UL;
    *lost = pp ? 0UL : 1UL;
    return count;
}

VOID XhciTolOcInit(PXHCI_TOL_OC oc)
{
    if (oc == NULL) {
        return;
    }
    oc->Phase = XHCI_TOL_OC_NONE;
    oc->Stamp = 0;
    oc->SettleStamp = 0;
}

VOID XhciTolOcBegin(PXHCI_TOL_OC oc, ULONG now)
{
    if (oc == NULL || oc->Phase == XHCI_TOL_OC_WAIT ||
        oc->Phase == XHCI_TOL_OC_SETTLE) {
        return;
    }
    oc->Phase = XHCI_TOL_OC_WAIT;
    oc->Stamp = now;
    oc->SettleStamp = 0;
}

ULONG XhciTolOcStep(PXHCI_TOL_OC oc, ULONG oca, ULONG now)
{
    if (oc == NULL) {
        return XHCI_TOL_OC_ACT_NONE;
    }
    switch (oc->Phase) {
    case XHCI_TOL_OC_WAIT:
    case XHCI_TOL_OC_SETTLE:
        if (oca) {
            oc->Phase = XHCI_TOL_OC_WAIT;
        } else if (oc->Phase == XHCI_TOL_OC_WAIT) {
            oc->Phase = XHCI_TOL_OC_SETTLE;
            oc->SettleStamp = now;
        } else if (XhciTolElapsed(now, oc->SettleStamp,
                                  XHCI_TOL_OC_SETTLE_TICKS)) {
            oc->Phase = XHCI_TOL_OC_POWER_ON;
            oc->Stamp = now;
            return XHCI_TOL_OC_ACT_REPOWER;
        }
        /* A flapping OCA restarts the settle but not the wait, so the
         * episode ends either way. */
        if (XhciTolElapsed(now, oc->Stamp, XHCI_TOL_OC_WAIT_TICKS)) {
            XhciTolOcInit(oc);
            return XHCI_TOL_OC_ACT_GIVE_UP;
        }
        return XHCI_TOL_OC_ACT_NONE;
    case XHCI_TOL_OC_POWER_ON:
        if (!XhciTolElapsed(now, oc->Stamp, XHCI_TOL_POWER_ON_TICKS)) {
            return XHCI_TOL_OC_ACT_NONE;
        }
        XhciTolOcInit(oc);
        return XHCI_TOL_OC_ACT_INSPECT;
    default:
        return XHCI_TOL_OC_ACT_NONE;
    }
}

ULONG XhciTolHchRecover(ULONG tolerance, ULONG hch, ULONG runs)
{
    return (tolerance && hch && runs) ? 1UL : 0UL;
}

VOID XhciTolWindowInit(PXHCI_TOL_WINDOW win)
{
    ULONG i;

    if (win == NULL) {
        return;
    }
    win->Count = 0;
    win->Refused = 0;
    for (i = 0; i < XHCI_TOL_RECOVERIES; i++) {
        win->Stamp[i] = 0;
    }
}

ULONG XhciTolWindowAdmit(PXHCI_TOL_WINDOW win, ULONG tolerance, ULONG now)
{
    ULONG i;

    if (win == NULL) {
        return 0;
    }
    if (!tolerance) {
        return 1;
    }
    while (win->Count > 0 &&
           XhciTolElapsed(now, win->Stamp[0],
                          XHCI_TOL_RECOVERY_WINDOW_TICKS)) {
        for (i = 1; i < win->Count; i++) {
            win->Stamp[i - 1] = win->Stamp[i];
        }
        win->Count--;
    }
    if (win->Count >= XHCI_TOL_RECOVERIES) {
        win->Refused++;
        return 0;
    }
    win->Stamp[win->Count] = now;
    win->Count++;
    return 1;
}

ULONG XhciTolResetRunning(ULONG usbcmd, ULONG usbsts)
{
    return ((usbcmd != 0xFFFFFFFFUL && (usbcmd & XHCI_USBCMD_HCRST) != 0) ||
            (usbsts != 0xFFFFFFFFUL && (usbsts & XHCI_USBSTS_CNR) != 0))
               ? 1UL
               : 0UL;
}

ULONG XhciTolResetSettle(const XHCI_TOL_RESET_IO *io, ULONG timeoutMs,
                         ULONG stepMs, PULONG waited, PULONG usbcmd,
                         PULONG usbsts)
{
    ULONG cmd;
    ULONG sts;
    ULONG slept;

    if (stepMs == 0) {
        stepMs = 1;
    }
    slept = 0;
    for (;;) {
        cmd = 0xFFFFFFFFUL;
        sts = 0xFFFFFFFFUL;
        io->Read(io->Context, &cmd, &sts);
        *usbcmd = cmd;
        *usbsts = sts;
        *waited = slept;
        if (cmd == 0xFFFFFFFFUL || sts == 0xFFFFFFFFUL) {
            return XHCI_TOL_SETTLE_DEAD;
        }
        if (!XhciTolResetRunning(cmd, sts)) {
            return XHCI_TOL_SETTLE_DONE;
        }
        if (slept >= timeoutMs) {
            return XHCI_TOL_SETTLE_TIMEOUT;
        }
        io->Sleep(io->Context, stepMs);
        slept += stepMs;
    }
}

ULONG XhciTolRecoverPaced(ULONG failures, ULONG failStamp, ULONG now)
{
    if (failures == 0) {
        return 1;
    }
    return XhciTolElapsed(now, failStamp, XHCI_TOL_RECOVER_GAP_TICKS);
}

ULONG XhciTolTerminal(ULONG failed, ULONG unreadable, ULONG windowRefused,
                      ULONG failures, ULONG maxFailures)
{
    return XhciTolTerminalEx(failed, unreadable, windowRefused, failures,
                             maxFailures, 0);
}

ULONG XhciTolTerminalEx(ULONG failed, ULONG unreadable, ULONG windowRefused,
                        ULONG failures, ULONG maxFailures, ULONG msg4Lost)
{
    if (unreadable == XHCI_TOL_CONTAINED_DMA_UNPROVEN) {
        return XHCI_TOL_TERMINAL_DMA_UNPROVEN;
    }
    if (unreadable) {
        return XHCI_TOL_TERMINAL_UNREADABLE;
    }
    if (msg4Lost) {
        return XHCI_TOL_TERMINAL_MSG4;
    }
    if (windowRefused) {
        return XHCI_TOL_TERMINAL_WINDOW;
    }
    if (!failed) {
        return XHCI_TOL_TERMINAL_NONE;
    }
    if (failures >= maxFailures) {
        return XHCI_TOL_TERMINAL_FAILURES;
    }
    return XHCI_TOL_TERMINAL_OWED;
}

ULONG XhciTolTerminalRelease(ULONG terminal, ULONG pending, ULONG raised,
                             ULONG devices)
{
    if (terminal != XHCI_TOL_TERMINAL_WINDOW &&
        terminal != XHCI_TOL_TERMINAL_FAILURES &&
        terminal != XHCI_TOL_TERMINAL_MSG4) {
        return 0;
    }
    return (!pending && !raised && devices) ? 1UL : 0UL;
}

ULONG XhciTolSaveRefused(ULONG failed)
{
    return failed ? 1UL : 0UL;
}

ULONG XhciTolSavePublish(ULONG initialized)
{
    return initialized ? 0UL : 1UL;
}

ULONG XhciTolSaveCommit(ULONG published)
{
    return published ? 0UL : 1UL;
}

ULONG XhciTolResumeRefused(ULONG terminal)
{
    return (terminal == XHCI_TOL_TERMINAL_FAILURES ||
            terminal == XHCI_TOL_TERMINAL_WINDOW ||
            terminal == XHCI_TOL_TERMINAL_UNREADABLE ||
            terminal == XHCI_TOL_TERMINAL_DMA_UNPROVEN ||
            terminal == XHCI_TOL_TERMINAL_MSG4)
               ? 1UL
               : 0UL;
}

ULONG XhciTolResumeFailures(ULONG failures, ULONG reinitialized)
{
    return reinitialized ? 0UL : failures;
}

ULONG XhciTolShutLatch(ULONG systemPower)
{
    return (systemPower == XHCI_TOL_SYSTEM_SHUTDOWN) ? 1UL : 0UL;
}

ULONG XhciTolShutFlush(ULONG latched, ULONG halted, ULONG pinned,
                       ULONG unreadable)
{
    if (!latched || unreadable) {
        return 0;
    }
    if (!halted || pinned) {
        return XHCI_TOL_SHUT_CMDS;
    }
    return XHCI_TOL_SHUT_CMDS | XHCI_TOL_SHUT_XFERS;
}

ULONG XhciTolHaltProven(ULONG usbsts)
{
    return (usbsts != 0xFFFFFFFFUL && (usbsts & XHCI_USBSTS_HCH) != 0)
               ? 1UL
               : 0UL;
}

ULONG XhciTolDeadStep(PXHCI_TOL_DEAD dead, ULONG tolerance, ULONG admitted,
                      ULONG allOnes, ULONG startGen, ULONG now)
{
    if (dead == NULL) {
        return 0;
    }
    if (!tolerance || !admitted || !allOnes) {
        dead->Armed = 0;
        return 0;
    }
    if (!dead->Armed || dead->StartGen != startGen) {
        dead->Armed = 1;
        dead->StartGen = startGen;
        dead->Stamp = now;
        return 0;
    }
    return XhciTolElapsed(now, dead->Stamp, XHCI_TOL_CONTAIN_TICKS);
}

ULONG XhciTolProof(ULONG bme, ULONG asked, ULONG isolated)
{
    if (bme) {
        return XHCI_TOL_PROOF_BME;
    }
    if (asked && isolated) {
        return XHCI_TOL_PROOF_TUNNEL;
    }
    return XHCI_TOL_PROOF_NONE;
}

ULONG XhciTolRecoverDefer(ULONG tolerance, ULONG admitted, ULONG allOnes)
{
    return (tolerance && admitted && allOnes) ? 1UL : 0UL;
}

/* An even number of ULONGs, so XHCI_EXTENSION's TrailingPad keeps its
 * meaning when this block is appended to it. */
typedef char xhciTolRootPorts[XHCI_TOL_ROOT_PORTS == XHCI_MAX_ROOT_PORTS ?
                              1 : -1];
typedef char xhciTolStateEven[((sizeof(XHCI_TOL_STATE) / sizeof(ULONG)) % 2UL)
                              == 0 ? 1 : -1];

VOID XhciTolStart(PXHCI_TOL_STATE st, ULONG vendorDevice,
                  ULONG tolFound, ULONG tolValue,
                  ULONG capFound, ULONG capValue,
                  ULONG avgFound, ULONG avgValue)
{
    PULONG w;
    ULONG n;
    ULONG i;

    if (st == NULL) {
        return;
    }
    w = (PULONG)&st->Stats;
    n = sizeof(st->Stats) / sizeof(ULONG);
    for (i = 0; i < n; i++) {
        w[i] = 0;
    }
    st->Stats.Tolerance = XhciTolMode(tolFound, tolValue);
    st->Stats.CapMode = XhciTolCapMode(capFound, capValue);
    st->Stats.AvgTrbMode = XhciTolAvgTrbMode(avgFound, avgValue);
    st->Stats.CapApplied = XhciTolCapApplies(st->Stats.CapMode, vendorDevice);
    st->Obs.Valid = 0;
    st->Obs.Index = 0;
    st->Obs.Cycle = 0;
    st->Obs.DrainGen = 0;
    st->Obs.StartGen = 0;
    st->Obs.Stamp = 0;
    XhciTolWindowInit(&st->Window);
    st->Dead.Armed = 0;
    st->Dead.StartGen = 0;
    st->Dead.Stamp = 0;
    st->Unreadable = 0;
    st->Clock = 0;
    for (i = 0; i < XHCI_TOL_ROOT_PORTS; i++) {
        XhciTolLocInit(&st->RootLoc[i]);
    }
    for (i = 0; i < XHCI_TOL_HUB_LOCS; i++) {
        XhciTolLocInit(&st->HubLoc[i]);
    }
}

VOID XhciTolCountCode(PXHCI_TOL_STATS stats, ULONG code)
{
    if (stats != NULL && code < 256) {
        stats->Codes[code]++;
    }
}

/* ------------------------------------------------------------------ */
/* Task 50.30 step 1: the reset reasons (record 17 section 4.15.8)      */
/* ------------------------------------------------------------------ */

static VOID xhciTolRecClear(PXHCI_RESET_REC r)
{
    r->Reason = 0;
    r->Extra = 0;
    r->Slot = 0;
    r->RootPort = 0;
    r->Route = 0;
    r->VidPid = 0;
    r->Clock = 0;
    r->Recoveries = 0;
}

VOID XhciTolWhyInit(PXHCI_TOL_WHY w)
{
    ULONG i;

    if (w == NULL) {
        return;
    }
    for (i = 0; i < XHCI_WHY_COUNTERS; i++) {
        w->Counts[i] = 0;
    }
    w->Next = 0;
    w->Kept = 0;
    w->Latched = 0;
    w->Staged = 0;
    w->StagedExtra = 0;
    w->StagedLost = 0;
    for (i = 0; i < XHCI_WHY_RING; i++) {
        xhciTolRecClear(&w->Rec[i]);
    }
    w->StagedSlot = 0;
    w->StagedPort = 0;
    w->StagedRoute = 0;
    w->StagedVidPid = 0;
    w->SpreadCount = 0;
    w->SpreadNext = 0;
    w->IsolationValue = XHCI_TOL_ISO_NONE;
    w->IsolationState = 0;
    for (i = 0; i < XHCI_TOL_SPREAD_RING; i++) {
        w->Spread[i].Place = 0;
        w->Spread[i].Hub = 0;
        w->Spread[i].VidPid = 0;
        w->Spread[i].Clock = 0;
    }
}

ULONG XhciTolSpreadAdd(PXHCI_TOL_WHY w, ULONG place, ULONG hub, ULONG vidPid,
                       ULONG clock)
{
    PXHCI_TOL_SPREAD_REC r;

    if (w == NULL) {
        return 0;
    }
    if (w->SpreadNext >= XHCI_TOL_SPREAD_RING) {
        w->SpreadNext = 0;
    }
    r = &w->Spread[w->SpreadNext];
    r->Place = place;
    r->Hub = hub;
    r->VidPid = vidPid;
    r->Clock = clock;
    w->SpreadNext = (w->SpreadNext + 1) % XHCI_TOL_SPREAD_RING;
    if (w->SpreadCount != 0xFFFFFFFFUL) {
        w->SpreadCount++;
    }
    return w->SpreadCount;
}

ULONG XhciTolWhyNote(const XHCI_RESET_REC *rec)
{
    if (rec == NULL) {
        return 0;
    }
    return ((rec->Reason & 0xFFUL) << 24) | ((rec->RootPort & 0xFFUL) << 16) |
           ((rec->Slot & 0xFFUL) << 8) | (rec->Extra & 0xFFUL);
}

ULONG XhciTolWhyRecord(PXHCI_TOL_WHY w, const XHCI_RESET_REC *rec,
                       ULONG begins)
{
    ULONG reason;
    ULONG at;

    if (w == NULL || rec == NULL) {
        return 0;
    }
    reason = rec->Reason < XHCI_WHY_COUNT ? rec->Reason : XHCI_WHY_NONE;
    w->Counts[reason]++;
    if (begins) {
        at = w->Next < XHCI_WHY_RING ? w->Next : 0;
        w->Rec[at] = *rec;
        w->Rec[at].Reason = reason;
        w->Next = (at + 1) % XHCI_WHY_RING;
        if (w->Kept < XHCI_WHY_RING) {
            w->Kept++;
        }
        /* A newer recovery: a latch named earlier is no longer the last. */
        w->Latched = 0;
    }
    return XhciTolWhyNote(rec);
}

VOID XhciTolWhyStage(PXHCI_TOL_WHY w, ULONG reason, ULONG extra,
                     const XHCI_RESET_REC *place)
{
    if (w == NULL) {
        return;
    }
    if (w->Staged != 0) {
        w->StagedLost++;
        return;
    }
    w->Staged = reason;
    w->StagedExtra = extra;
    if (place != NULL) {
        w->StagedSlot = place->Slot;
        w->StagedPort = place->RootPort;
        w->StagedRoute = place->Route;
        w->StagedVidPid = place->VidPid;
    } else {
        w->StagedSlot = 0;
        w->StagedPort = 0;
        w->StagedRoute = 0;
        w->StagedVidPid = 0;
    }
}

ULONG XhciTolWhyStageXfer(PXHCI_TOL_WHY w, ULONG fatal, ULONG matched,
                          ULONG code, const XHCI_RESET_REC *place)
{
    if (fatal) {
        XhciTolWhyStage(w, XHCI_WHY_XFER_FATAL, code,
                        matched ? place : NULL);
    }
    return fatal;
}

ULONG XhciTolWhyTake(PXHCI_TOL_WHY w, PULONG extra, PXHCI_RESET_REC place)
{
    ULONG reason;

    if (extra != NULL) {
        *extra = 0;
    }
    if (place != NULL) {
        xhciTolRecClear(place);
    }
    if (w == NULL) {
        return XHCI_WHY_EVENT_RING;
    }
    reason = w->Staged;
    if (reason == 0) {
        reason = XHCI_WHY_EVENT_RING;
    } else {
        if (extra != NULL) {
            *extra = w->StagedExtra;
        }
        if (place != NULL) {
            place->Slot = w->StagedSlot;
            place->RootPort = w->StagedPort;
            place->Route = w->StagedRoute;
            place->VidPid = w->StagedVidPid;
        }
    }
    w->Staged = 0;
    w->StagedExtra = 0;
    w->StagedSlot = 0;
    w->StagedPort = 0;
    w->StagedRoute = 0;
    w->StagedVidPid = 0;
    return reason;
}

ULONG XhciTolLeftAdmit(ULONG state, ULONG settle, ULONG onList)
{
    if (state == XHCI_LEFT_CLEAR) {
        return settle ? 0UL : 1UL;
    }
    if (state == XHCI_LEFT_SETTLING) {
        return (settle && onList) ? 1UL : 0UL;
    }
    return 0;
}

ULONG XhciTolLeftInputOk(ULONG state)
{
    return state == XHCI_LEFT_CLEAR ? 1UL : 0UL;
}

ULONG XhciTolLeftSlotCheck(ULONG matched, ULONG code, ULONG slot,
                           ULONG maxSlotsEn, ULONG owned, ULONG dcbaaLo,
                           ULONG dcbaaHi, ULONG listed)
{
    if (!matched) {
        return XHCI_LEFT_SLOT_UNMATCHED;
    }
    if (code != XHCI_CC_SUCCESS) {
        return XHCI_LEFT_SLOT_CODE;
    }
    if (slot == 0 || slot > maxSlotsEn || slot > 255) {
        return XHCI_LEFT_SLOT_RANGE;
    }
    if (owned) {
        return XHCI_LEFT_SLOT_OWNED;
    }
    if (dcbaaLo != 0 || dcbaaHi != 0) {
        return XHCI_LEFT_SLOT_DCBAA;
    }
    if (listed) {
        return XHCI_LEFT_SLOT_LISTED;
    }
    return XHCI_LEFT_SLOT_OK;
}

ULONG XhciTolLeftSlotlessEnd(ULONG trbType, ULONG ended, ULONG code,
                             ULONG slotCheck)
{
    if (trbType == XHCI_TRB_TYPE_NOOP_COMMAND) {
        return XHCI_LEFT_END_CLEAR;
    }
    if (trbType != XHCI_TRB_TYPE_ENABLE_SLOT || !ended) {
        return XHCI_LEFT_END_RESET;
    }
    if (code != XHCI_CC_SUCCESS) {
        /* An Enable Slot answered with an error code allocated nothing. */
        return XHCI_LEFT_END_CLEAR;
    }
    return slotCheck == XHCI_LEFT_SLOT_OK ? XHCI_LEFT_END_DISABLE
                                          : XHCI_LEFT_END_RESET;
}

ULONG XhciTolLeftAfterRestore(ULONG state, ULONG namesDevice)
{
    if (state == XHCI_LEFT_CLEAR || namesDevice) {
        return XHCI_LEFT_CLEAR;
    }
    return XHCI_LEFT_SETTLING;
}

XHCI_C_ASSERT(leftListMax, XHCI_LEFT_LIST_MAX == XHCI_MAX_SLOTS + 1UL);

VOID XhciLeftDebtClear(PXHCI_LEFT_DEBT d)
{
    ULONG held;
    ULONG deferred;
    ULONG extra;
    ULONG count;
    XHCI_RESET_REC at;
    ULONG i;
    PUCHAR b;

    held = d->EnableHeld;
    deferred = d->Deferred;
    extra = d->DeferredExtra;
    count = d->DeferredCount;
    at = d->DeferredAt;
    b = (PUCHAR)d;
    for (i = 0; i < sizeof(*d); i++) {
        b[i] = 0;
    }
    d->Owed = XHCI_LEFT_CLEAR;
    d->End = XHCI_LEFT_HOW_NONE;
    d->EnableHeld = held;
    d->Deferred = deferred;
    d->DeferredExtra = extra;
    d->DeferredCount = count;
    d->DeferredAt = at;
}

VOID XhciLeftDebtHcrst(PXHCI_LEFT_DEBT d)
{
    ULONG i;
    PUCHAR b;

    b = (PUCHAR)d;
    for (i = 0; i < sizeof(*d); i++) {
        b[i] = 0;
    }
    d->Owed = XHCI_LEFT_CLEAR;
    d->End = XHCI_LEFT_HOW_NONE;
}

ULONG XhciLeftDebtDefer(PXHCI_LEFT_DEBT d, ULONG reason, ULONG extra,
                        const XHCI_RESET_REC *place)
{
    if (d->Owed == XHCI_LEFT_CLEAR || !XhciTolWhyThread(reason)) {
        return 0;
    }
    d->DeferredCount++;
    if (d->Deferred == 0) {
        d->Deferred = reason;
        d->DeferredExtra = extra;
        d->DeferredAt.Reason = reason;
        d->DeferredAt.Extra = extra;
        d->DeferredAt.Slot = place != NULL ? place->Slot : 0UL;
        d->DeferredAt.RootPort = place != NULL ? place->RootPort : 0UL;
        d->DeferredAt.Route = place != NULL ? place->Route : 0UL;
        d->DeferredAt.VidPid = place != NULL ? place->VidPid : 0UL;
        d->DeferredAt.Clock = 0;
        d->DeferredAt.Recoveries = 0;
    }
    return 1;
}

ULONG XhciLeftDebtTakeDeferred(PXHCI_LEFT_DEBT d, PULONG extra,
                               PXHCI_RESET_REC place)
{
    ULONG reason;
    ULONG i;
    PUCHAR b;

    reason = d->Deferred;
    *extra = d->DeferredExtra;
    *place = d->DeferredAt;
    d->Deferred = 0;
    d->DeferredExtra = 0;
    b = (PUCHAR)&d->DeferredAt;
    for (i = 0; i < sizeof(d->DeferredAt); i++) {
        b[i] = 0;
    }
    return reason;
}

ULONG XhciTolTeardownStops(ULONG queued, ULONG retained, ULONG running)
{
    return ((queued || retained) && running) ? 1UL : 0UL;
}

ULONG XhciTolInputHeldAfter(ULONG held, ULONG disabled)
{
    return (held != 0 && held == disabled) ? 0UL : held;
}

ULONG XhciTolLeftNamedReset(ULONG trbType)
{
    return trbType == XHCI_TRB_TYPE_STOP_EP ? 1UL : 0UL;
}

ULONG XhciLeftDebtLeave(PXHCI_LEFT_DEBT d, ULONG settle, ULONG type,
                        ULONG slot, ULONG pa, ULONG gen, ULONG device,
                        ULONG lost, const XHCI_RESET_REC *place)
{
    if (settle || d->Owed != XHCI_LEFT_CLEAR) {
        if (!d->Interrupted) {
            d->InterruptedType = type;
            d->InterruptedSlot = slot;
        }
        d->Interrupted = 1;
        return XHCI_LEFT_TOOK_INTERRUPTED;
    }
    XhciLeftDebtClear(d);
    d->Owed = XHCI_LEFT_OWED;
    d->Type = type;
    d->Slot = slot;
    d->PA = pa;
    d->Gen = gen;
    d->Device = device ? 1UL : 0UL;
    d->End = lost ? XHCI_LEFT_HOW_LOST : XHCI_LEFT_HOW_NONE;
    if (d->Device && XhciTolLeftNamedReset(type)) {
        (VOID)XhciLeftDebtDefer(d, XHCI_WHY_CMD_KEPT, type, place);
    }
    return XHCI_LEFT_TOOK_OWED;
}

ULONG XhciLeftDebtAdd(PXHCI_LEFT_DEBT d, ULONG kind, ULONG slot)
{
    if (d->Count >= XHCI_LEFT_LIST_MAX) {
        d->Overflow = 1;
        if (d->Reset == 0) {
            d->Reset = XHCI_WHY_CMD_KEPT;
            d->ResetSlot = slot;
            d->ResetExtra = d->Type;
        }
        return 0;
    }
    d->List[d->Count].Kind = kind;
    d->List[d->Count].Slot = slot;
    d->Count++;
    return 1;
}

ULONG XhciLeftDebtAddRecords(PXHCI_LEFT_DEBT d, const ULONG *tier)
{
    ULONG deepest;
    ULONG lost;
    ULONG t;
    ULONG i;

    deepest = 0;
    for (i = 1; i <= XHCI_MAX_SLOTS; i++) {
        if (tier[i] != XHCI_LEFT_TIER_NONE && tier[i] > deepest) {
            deepest = tier[i];
        }
    }
    lost = 0;
    t = deepest;
    for (;;) {
        for (i = 1; i <= XHCI_MAX_SLOTS; i++) {
            if (tier[i] == t && !XhciLeftDebtAdd(d, XHCI_LEFT_ENTRY_RECORD,
                                                 i)) {
                lost++;
            }
        }
        if (t == 0) {
            break;
        }
        t--;
    }
    return lost;
}

ULONG XhciLeftDebtListed(const XHCI_LEFT_DEBT *d, ULONG slot)
{
    ULONG i;

    for (i = d->Head; i < d->Count && i < XHCI_LEFT_LIST_MAX; i++) {
        if (d->List[i].Slot == slot) {
            return 1;
        }
    }
    return 0;
}

ULONG XhciLeftDebtPass(const XHCI_LEFT_DEBT *d, ULONG routes, ULONG genMoved)
{
    if (d->Owed == XHCI_LEFT_CLEAR || routes) {
        return XHCI_LEFT_PASS_NONE;
    }
    if (d->Overflow) {
        return XHCI_LEFT_PASS_FAIL;
    }
    if (d->Owed == XHCI_LEFT_OWED) {
        return (d->End != XHCI_LEFT_HOW_NONE || genMoved)
                   ? XHCI_LEFT_PASS_TAKE
                   : XHCI_LEFT_PASS_NONE;
    }
    if (d->Interrupted) {
        /* Revision 24: every route's bit is clear and neither a stop's
         * teardown nor an HCRST has settled it - a failed settlement. */
        return XHCI_LEFT_PASS_FAIL;
    }
    if (!d->Listed) {
        return XHCI_LEFT_PASS_TAKE;
    }
    return d->Head >= d->Count ? XHCI_LEFT_PASS_CLEAR : XHCI_LEFT_PASS_NEXT;
}

ULONG XhciLeftDebtStep(const XHCI_LEFT_DEBT *d, ULONG routes, ULONG genMoved,
                       ULONG failed, ULONG tainted)
{
    ULONG action;

    if (failed) {
        return XHCI_LEFT_PASS_NONE;
    }
    if (d->Owed == XHCI_LEFT_CLEAR) {
        return (d->Deferred != 0 && !routes) ? XHCI_LEFT_PASS_CLEAR
                                             : XHCI_LEFT_PASS_NONE;
    }
    action = XhciLeftDebtPass(d, routes, genMoved);
    if (tainted && action == XHCI_LEFT_PASS_NEXT) {
        return XHCI_LEFT_PASS_TAINTED;
    }
    return action;
}

VOID XhciLeftDebtTake(PXHCI_LEFT_DEBT d)
{
    if (d->Owed == XHCI_LEFT_OWED) {
        if (d->End == XHCI_LEFT_HOW_NONE) {
            d->End = XHCI_LEFT_HOW_LOST;
        }
        d->Owed = XHCI_LEFT_SETTLING;
        d->Listed = 0;
    }
}

VOID XhciLeftDebtFail(PXHCI_LEFT_DEBT d, ULONG why, ULONG slot, ULONG extra)
{
    if (d->Reset == 0) {
        if (why != 0) {
            d->Reset = why;
            d->ResetSlot = slot;
            d->ResetExtra = extra;
        } else if (d->Interrupted) {
            d->Reset = XHCI_WHY_CMD_KEPT;
            d->ResetSlot = d->InterruptedSlot;
            d->ResetExtra = d->InterruptedType;
        } else {
            d->Reset = XHCI_WHY_CMD_KEPT;
            d->ResetSlot = slot;
            d->ResetExtra = extra;
        }
    }
    d->Interrupted = 0;
    d->InterruptedType = 0;
    d->InterruptedSlot = 0;
    d->Overflow = 0;
    d->Listed = 1;
    d->Head = d->Count;
}

ULONG XhciLeftDebtSettleClear(PXHCI_LEFT_DEBT d, PULONG why, PULONG slot,
                              PULONG extra)
{
    *why = 0;
    *slot = 0;
    *extra = 0;
    if (d->Owed == XHCI_LEFT_CLEAR) {
        return 0;
    }
    *why = d->Reset;
    *slot = d->ResetSlot;
    *extra = d->ResetExtra;
    XhciLeftDebtClear(d);
    if (*why != 0) {
        d->EnableHeld = 1;
    }
    return 1;
}

ULONG XhciLeftDebtFailClear(PXHCI_LEFT_DEBT d, ULONG failWhy,
                            ULONG failSlot, ULONG failExtra, PULONG why,
                            PULONG slot, PULONG extra)
{
    XhciLeftDebtFail(d, failWhy, failSlot, failExtra);
    return XhciLeftDebtSettleClear(d, why, slot, extra);
}

ULONG XhciLeftDebtRestored(PXHCI_LEFT_DEBT d, ULONG owedRecords)
{
    ULONG before;
    ULONG i;

    before = d->Owed;
    if (before == XHCI_LEFT_CLEAR) {
        return before;
    }
    if (!d->Interrupted && owedRecords == 0 && d->Reset == 0 &&
        !d->Overflow &&
        XhciTolLeftAfterRestore(before, d->Device) == XHCI_LEFT_CLEAR) {
        XhciLeftDebtClear(d);
        return before;
    }
    d->Owed = XHCI_LEFT_SETTLING;
    if (d->End == XHCI_LEFT_HOW_NONE) {
        d->End = XHCI_LEFT_HOW_LOST;
    }
    for (i = d->Head; i < d->Count && i < XHCI_LEFT_LIST_MAX; i++) {
        if (d->List[i].Kind == XHCI_LEFT_ENTRY_SLOT) {
            d->SlotlessDone = 0;
        }
    }
    d->Listed = 0;
    d->Head = 0;
    d->Count = 0;
    return before;
}

VOID XhciCmdAbortClear(PXHCI_CMD_ABORT_MARK m)
{
    m->Set = 0;
    m->PA = 0;
    m->Gen = 0;
    m->How = 0;
}

VOID XhciCmdAbortSet(PXHCI_CMD_ABORT_MARK m, ULONG pa, ULONG gen, ULONG how)
{
    m->Set = 1;
    m->PA = pa;
    m->Gen = gen;
    m->How = how;
}

ULONG XhciCmdAbortMatches(const XHCI_CMD_ABORT_MARK *m, ULONG pa, ULONG gen)
{
    return (m->Set && m->PA == pa && m->Gen == gen) ? 1UL : 0UL;
}

ULONG XhciTolWhyThread(ULONG reason)
{
    switch (reason) {
    case XHCI_WHY_CMD_KEPT:
    case XHCI_WHY_CMD_NO_VERDICT:
    case XHCI_WHY_DISABLE_SLOT:
    case XHCI_WHY_EP0_RECOVERY:
    case XHCI_WHY_EP0_LATE:
    case XHCI_WHY_CREATE_LATE:
    case XHCI_WHY_SLOT_ID:
    case XHCI_WHY_PORTSC_DEAD:
    case XHCI_WHY_WARM_RESET:
    case XHCI_WHY_TEARDOWN_STOP:
    case XHCI_WHY_CFG_FAULT:
    case XHCI_WHY_STREAMS:
    case XHCI_WHY_BOT_XFER:
    case XHCI_WHY_HUB_UNANSWERED:
        return 1;
    default:
        return 0;
    }
}

ULONG XhciTolAbortKeepsReset(ULONG trbType)
{
    switch (trbType) {
    case XHCI_TRB_TYPE_ADDRESS_DEVICE:
    case XHCI_TRB_TYPE_CONFIGURE_EP:
    case XHCI_TRB_TYPE_EVALUATE_CONTEXT:
    case XHCI_TRB_TYPE_RESET_EP:
    case XHCI_TRB_TYPE_SET_TR_DEQUEUE:
    case XHCI_TRB_TYPE_RESET_DEVICE:
        return 0;
    default:
        return 1;
    }
}

ULONG XhciTolWaitVerdict(ULONG own, ULONG aborted, ULONG failed, ULONG moved,
                         ULONG leaving, ULONG expired)
{
    if (own) {
        return XHCI_WAIT_OWN;
    }
    if (aborted && !failed && !moved) {
        return XHCI_WAIT_ABORTED;
    }
    if (failed) {
        return XHCI_WAIT_FAILED;
    }
    if (moved) {
        return XHCI_WAIT_LOST;
    }
    if (leaving) {
        return XHCI_WAIT_LEFT;
    }
    if (expired) {
        return XHCI_WAIT_NO_VERDICT;
    }
    return XHCI_WAIT_WAIT;
}

ULONG XhciTolIsolationMode(ULONG found, ULONG value)
{
    if (found && (value == XHCI_ISOLATION_OFF || value == XHCI_ISOLATION_ALL)) {
        return value;
    }
    return XHCI_ISOLATION_LISTED;
}

/* The controllers on which the default 1 applies the handling (design
 * record 17 sections 4.15.10 and 4.15.11). The rule lists a controller
 * once its abort has been read on real hardware, the entry citing that
 * run section; the one entry below was listed by the owner's decision of
 * 2026-11-20 without such a reading. Ends at 0. */
static const ULONG xhciIsolationTable[] = {
    0x1E318086UL,   /* Intel 7 Series, 8086:1E31 (the field report of
                     * section 4.15.1): its abort read only in the
                     * emulator's model (section 4.15.9) */
    0
};

ULONG XhciIsolationListed(ULONG vendorDevice)
{
    ULONG i;

    for (i = 0; xhciIsolationTable[i] != 0; i++) {
        if (xhciIsolationTable[i] == vendorDevice) {
            return 1;
        }
    }
    return 0;
}

ULONG XhciTolIsolationApplies(ULONG mode, ULONG listed)
{
    if (mode == XHCI_ISOLATION_ALL) {
        return 1;
    }
    return (mode == XHCI_ISOLATION_LISTED && listed) ? 1UL : 0UL;
}

ULONG XhciTolIsolationState(ULONG mode, ULONG listed, ULONG applies)
{
    return ((mode & 0xFFUL) << XHCI_TOL_ISO_MODE_SHIFT) |
           (listed ? 2UL : 0UL) | (applies ? 1UL : 0UL);
}

ULONG XhciTolIsolationNote(ULONG value, ULONG listed, ULONG applies)
{
    return ((value > 0xFEUL ? 0xFFUL : value) << 8) | (listed ? 2UL : 0UL) |
           (applies ? 1UL : 0UL);
}

ULONG XhciQemuIsolationListed(ULONG vendorDevice)
{
    return (vendorDevice == XHCI_TOL_QEMU_XHCI_ID) ? 1UL : 0UL;
}

ULONG XhciTolWhyAt(const XHCI_TOL_WHY *w, ULONG i)
{
    ULONG next;

    next = (w->Next < XHCI_WHY_RING) ? w->Next : 0;
    return (next + 2UL * XHCI_WHY_RING - 1UL - (i % XHCI_WHY_RING)) %
           XHCI_WHY_RING;
}

ULONG XhciTolWhyLatch(PXHCI_TOL_WHY w, PXHCI_RESET_REC rec)
{
    ULONG at;

    if (w == NULL || w->Kept == 0) {
        return 0;
    }
    at = XhciTolWhyAt(w, 0);
    w->Latched = at + 1;
    if (rec != NULL) {
        *rec = w->Rec[at];
    }
    return 1;
}

ULONG XhciTolWhyInvolved(const XHCI_TOL_WHY *w)
{
    const XHCI_RESET_REC *newest;
    ULONG count;
    ULONG i;
    ULONG at;

    if (w == NULL || w->Kept == 0) {
        return 0;
    }
    newest = &w->Rec[XhciTolWhyAt(w, 0)];
    if (newest->RootPort == 0) {
        return 0;
    }
    count = 0;
    for (i = 0; i < w->Kept && i < XHCI_WHY_RING; i++) {
        at = XhciTolWhyAt(w, i);
        if (w->Rec[at].RootPort == newest->RootPort &&
            w->Rec[at].Route == newest->Route) {
            count++;
        }
    }
    return count;
}
