/*
 * xhci_tol.c - controller tolerance, its pure half (roadmap-hcd.md tasks
 * 35-T.1 to 35-T.9, design record 17). See xhci_tol.h.
 *
 * DDK-free: part of the pure core. C89. IRQL: any.
 */

#include "xhci.h"
#include "xhci_tol.h"

#define XHCI_TOL_VENDOR_AMD         0x1022UL

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
        return ((vendorDevice & 0xFFFFUL) == XHCI_TOL_VENDOR_AMD) ? 1UL : 0UL;
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
                        ULONG streams, ULONG behindTt)
{
    if (!tolerance || !bulkOrInt || streams || behindTt) {
        return 0;
    }
    return XhciTolNoSoftRetry(vendorDevice) ? 0UL : 1UL;
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

ULONG XhciTolHaltConfirmed(ULONG epState)
{
    return (epState == XHCI_EP_STATE_HALTED ||
            epState == XHCI_EP_STATE_ERROR) ? 1UL : 0UL;
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
    /* A port held unpowered is released only by a controller start. */
    if (loc->Hold == XHCI_TOL_HOLD_REENUMS) {
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

ULONG XhciTolTerminal(ULONG failed, ULONG unreadable, ULONG windowRefused,
                      ULONG failures, ULONG maxFailures)
{
    if (unreadable) {
        return XHCI_TOL_TERMINAL_UNREADABLE;
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
