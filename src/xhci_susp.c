/*
 * xhci_susp.c - selective suspend's pure half (roadmap-hcd.md task 36.29;
 * design record 14 section 3.9). The contract of every function is in
 * xhci_susp.h; the rules are the record's, each cited where it is taken.
 *
 * DDK-free: part of the pure core. C89. IRQL: any.
 */

#include "xhci_susp.h"
#include "xhci.h"

/* ------------------------------------------------------------------ */
/* The switch, eligibility, capabilities                               */
/* ------------------------------------------------------------------ */

ULONG XhciSuspSwitch(ULONG present, ULONG value)
{
    return (present && value == 1UL) ? 1UL : 0UL;
}

ULONG XhciSuspEligible(ULONG switchOn, ULONG superSpeed, ULONG hub)
{
    return (switchOn && !superSpeed && !hub) ? 1UL : 0UL;
}

VOID XhciSuspCaps(ULONG eligible, ULONG remoteWake, ULONG wakeOff9x,
                  PXHCI_SUSP_CAPS out)
{
    out->Changed = 0;
    out->DeviceD1 = 0;
    out->DeviceD2 = 0;
    out->WakeFromD1 = 0;
    out->WakeFromD2 = 0;
    out->WakeFromD3 = 0;
    out->DeviceWake = 0;
    out->SystemWakeS0 = 0;
    if (!eligible) {
        return;
    }
    out->Changed = 1;
    out->DeviceD2 = 1;
    if (!remoteWake) {
        return;
    }
    out->DeviceD1 = 1;
    if (wakeOff9x) {
        /* 5.5: no wake on 9x if the WAIT_WAKE completion defect holds. */
        return;
    }
    out->WakeFromD1 = 1;
    out->WakeFromD2 = 1;
    out->DeviceWake = XHCI_SUSP_D2;
    out->SystemWakeS0 = 1;
}

/* ------------------------------------------------------------------ */
/* D-IRP admission                                                     */
/* ------------------------------------------------------------------ */

ULONG XhciSuspAdmit(const XHCI_SUSP_ADMIT_IN *in, ULONG curState,
                    PULONG newState)
{
    ULONG dx;

    *newState = curState;
    dx = (in->Requested != XHCI_SUSP_D0) ? 1UL : 0UL;
    if (in->Closed) {
        /* 4.4 (a), 4.2: closed or released, complete at once. A D0 does
         * not move PwrState: only the thread writes D0, at ACTIVE. */
        if (dx) {
            *newState = in->Requested;
        }
        return XHCI_SUSP_ADMIT_COMPLETE;
    }
    if (!in->Eligible) {
        *newState = in->Requested;
        return XHCI_SUSP_ADMIT_TODAY;
    }
    if (dx) {
        /* A Dx is effective at once (4.1). */
        *newState = in->Requested;
        if (in->PowerDown || in->SiblingD0 ||
            in->Link != XHCI_SUSP_ACTIVE) {
            return XHCI_SUSP_ADMIT_COMPLETE;
        }
        return XHCI_SUSP_ADMIT_PEND;
    }
    if (in->Link == XHCI_SUSP_ACTIVE && !in->PowerDown) {
        *newState = XHCI_SUSP_D0;
        return XHCI_SUSP_ADMIT_COMPLETE;
    }
    return XHCI_SUSP_ADMIT_PEND;
}

ULONG XhciSuspDxStale(ULONG dxEpoch, ULONG epoch)
{
    return (dxEpoch != epoch) ? 1UL : 0UL;
}

/* ------------------------------------------------------------------ */
/* The link record and the decision                                    */
/* ------------------------------------------------------------------ */

VOID XhciSuspLinkInit(PXHCI_SUSP_LINK link, ULONG root, ULONG generation)
{
    link->State = XHCI_SUSP_ACTIVE;
    link->Root = root ? 1UL : 0UL;
    link->Armed = 0;
    link->Generation = generation;
    link->U3Ms = 0;
    link->StopMs = 0;
    link->StepStartMs = 0;
    link->Step = 0;
    link->Kind = 0;
    link->ArmWanted = 0;
    link->Stopped = 0;
    link->BusyRetried = 0;
    link->WakeKept = 0;
    link->WakeTaken = 0;
    link->WakeClaimed = 0;
    link->Internal = 0;
    link->LatchDue = 0;
    link->Latched = 0;
    link->LatchDigest = 0;
    link->ClaimDue = 0;
    link->Outcome = XHCI_SUSP_OUT_NONE;
    link->Reason = XHCI_SUSP_WHY_NONE;
}

ULONG XhciSuspDigest(const XHCI_SUSP_FUNC *funcs, ULONG count)
{
    ULONG d;
    ULONG i;

    /* A pended Dx is not in it: its PwrState already is (4.1), and the
     * Dx's completion at a sequence's end must not read as a change. */
    d = 0x811C9DC5UL ^ count;
    for (i = 0; i < count; i++) {
        d = (d ^ ((funcs[i].PwrState & 7UL) |
                  ((funcs[i].PwrWanted & 7UL) << 3) |
                  ((funcs[i].D0Pended ? 1UL : 0UL) << 6) |
                  ((funcs[i].WaitWake ? 1UL : 0UL) << 7) |
                  (i << 8))) * 0x01000193UL;
    }
    return d;
}

static ULONG xhciSuspIsWakeState(ULONG pwrState)
{
    return (pwrState == XHCI_SUSP_D1 || pwrState == XHCI_SUSP_D2) ? 1UL
                                                                  : 0UL;
}

ULONG XhciSuspDecide(PXHCI_SUSP_LINK link, PXHCI_SUSP_FUNC funcs,
                     ULONG count, ULONG epoch, ULONG powerDown,
                     PXHCI_SUSP_DECISION out)
{
    ULONG i;
    ULONG active;
    ULONG allDx;
    ULONG digest;

    out->Do = XHCI_SUSP_DO_NONE;
    out->AllDx = 0;
    out->WantD0 = 0;
    out->ArmIntent = 0;
    out->Completions = 0;

    active = (link->State == XHCI_SUSP_ACTIVE) ? 1UL : 0UL;
    for (i = 0; i < count; i++) {
        funcs[i].ToD0 = 0;
        funcs[i].CompleteDx = 0;
        funcs[i].CompleteD0 = 0;
        if (funcs[i].PwrWanted == XHCI_SUSP_D0 &&
            (funcs[i].PwrState != XHCI_SUSP_D0 || funcs[i].D0Pended)) {
            out->WantD0 = 1;
            /* 4.3: D0 only at ACTIVE, or on GONE (completed, never
             * powered). */
            if (active) {
                funcs[i].ToD0 = 1;
            }
            if (funcs[i].D0Pended &&
                (active || link->State == XHCI_SUSP_GONE)) {
                funcs[i].CompleteD0 = 1;
            }
        }
        if (funcs[i].WaitWake && xhciSuspIsWakeState(funcs[i].PwrState)) {
            out->ArmIntent = 1;
        }
    }

    allDx = (count != 0) ? 1UL : 0UL;
    for (i = 0; i < count; i++) {
        if (funcs[i].PwrState == XHCI_SUSP_D0 || funcs[i].ToD0) {
            allDx = 0;
        }
    }
    out->AllDx = allDx;

    /* Which pended Dx this pass completes (3.5, 4.3, 4.4). */
    for (i = 0; i < count; i++) {
        if (!funcs[i].DxPended) {
            continue;
        }
        if (XhciSuspDxStale(funcs[i].DxEpoch, epoch) || !active ||
            !allDx) {
            funcs[i].CompleteDx = 1;
        }
    }

    switch (link->State) {
    case XHCI_SUSP_ACTIVE:
        if (link->LatchDue) {
            /* The slots at the end of a refusal, an uncertain outcome
             * ended ACTIVE, or a claimed wake: no suspend on them. */
            link->LatchDue = 0;
            link->Latched = 1;
            link->LatchDigest = XhciSuspDigest(funcs, count);
        }
        digest = XhciSuspDigest(funcs, count);
        if (link->Latched && digest != link->LatchDigest) {
            /* The slots changed since: the latch is spent, whatever
             * they now ask. */
            link->Latched = 0;
        }
        if (!allDx || powerDown) {
            break;
        }
        if (link->Latched) {
            /* Not this time: logical Dx holds for a pended one. */
            for (i = 0; i < count; i++) {
                if (funcs[i].DxPended) {
                    funcs[i].CompleteDx = 1;
                }
            }
            break;
        }
        link->Latched = 0;
        out->Do = XHCI_SUSP_DO_SUSPEND;
        break;
    case XHCI_SUSP_SUSPENDED:
        if (powerDown) {
            break;
        }
        if (out->WantD0) {
            out->Do = XHCI_SUSP_DO_RESUME;
        } else if (out->ArmIntent && !link->Armed) {
            out->Do = XHCI_SUSP_DO_REARM;
        }
        break;
    default:
        /* SUSPENDING and RESUMING run inside one call of the thread and
         * are never decided on; RECOVERING is the recovery's; GONE was
         * settled at the unlink. */
        break;
    }

    for (i = 0; i < count; i++) {
        if (funcs[i].CompleteDx || funcs[i].CompleteD0) {
            out->Completions = 1;
        }
    }
    return out->Do;
}

/* ------------------------------------------------------------------ */
/* The sequences                                                       */
/* ------------------------------------------------------------------ */

/* Milliseconds from `nowMs` until `fromMs` + `spanMs`, 0 when past; the
 * clock may wrap. */
static ULONG xhciSuspRemaining(ULONG fromMs, ULONG spanMs, ULONG nowMs)
{
    LONG left;

    left = (LONG)((fromMs + spanMs) - nowMs);
    if (left <= 0) {
        return 0;
    }
    if ((ULONG)left > spanMs) {
        return spanMs;
    }
    return (ULONG)left;
}

static ULONG xhciSuspEnd(PXHCI_SUSP_LINK link, ULONG state, ULONG outcome,
                         PULONG waitMs)
{
    link->State = state;
    link->Outcome = outcome;
    link->Step = XHCI_SUSP_STEP_END;
    if (state == XHCI_SUSP_GONE || state == XHCI_SUSP_RECOVERING) {
        /* The device's feature does not outlive a reset or an unplug. */
        link->Armed = 0;
    }
    *waitMs = 0;
    return XHCI_SUSP_STEP_END;
}

static ULONG xhciSuspStep(PXHCI_SUSP_LINK link, ULONG step, ULONG wait,
                          ULONG nowMs, PULONG waitMs)
{
    link->Step = step;
    link->StepStartMs = nowMs;
    *waitMs = wait;
    return step;
}

/* A refusal: back to ACTIVE through the restart, which waits out SP=1's
 * minimum when endpoints were stopped (3.5). */
static ULONG xhciSuspRefuse(PXHCI_SUSP_LINK link, ULONG why, ULONG nowMs,
                            PULONG waitMs)
{
    ULONG wait;

    link->Reason = why;
    wait = link->Stopped ? xhciSuspRemaining(link->StopMs,
                                             XHCI_SUSP_STOP_MIN_MS, nowMs)
                         : 0;
    return xhciSuspStep(link, XHCI_SUSP_STEP_RESTART, wait, nowMs, waitMs);
}

/* The device is waking (3.4 (5), 3.6 device-initiated): RESUMING. */
static ULONG xhciSuspWakeBranch(PXHCI_SUSP_LINK link, ULONG nowMs,
                                PULONG waitMs)
{
    link->State = XHCI_SUSP_RESUMING;
    link->Kind = XHCI_SUSP_RUN_WAKE;
    if (!link->WakeTaken) {
        link->WakeTaken = 1;
        link->ClaimDue = 1;
    }
    if (link->Root) {
        return xhciSuspStep(link, XHCI_SUSP_STEP_U0, XHCI_SUSP_TDRSMDN_MS,
                            nowMs, waitMs);
    }
    return xhciSuspStep(link, XHCI_SUSP_STEP_RECOVERY, XHCI_SUSP_TRSMRCY_MS,
                        nowMs, waitMs);
}

ULONG XhciSuspBegin(PXHCI_SUSP_LINK link, ULONG kind, ULONG armWanted,
                    ULONG internal, ULONG nowMs, PULONG waitMs)
{
    link->Kind = kind;
    link->Internal = internal ? 1UL : 0UL;
    link->Outcome = XHCI_SUSP_OUT_NONE;
    link->Reason = XHCI_SUSP_WHY_NONE;
    if (kind == XHCI_SUSP_RUN_SUSPEND) {
        link->ClaimDue = 0;
        link->State = XHCI_SUSP_SUSPENDING;
        link->ArmWanted = armWanted ? 1UL : 0UL;
        link->Stopped = 0;
        link->BusyRetried = 0;
        link->WakeKept = 0;
        link->WakeTaken = 0;
        link->WakeClaimed = 0;
        return xhciSuspStep(link, XHCI_SUSP_STEP_GATE, 0, nowMs, waitMs);
    }
    if (kind == XHCI_SUSP_RUN_WAKE) {
        /* After XhciSuspEvidence took the event, whose ClaimDue stands
         * until XhciSuspWakeCount. */
        return xhciSuspWakeBranch(link, nowMs, waitMs);
    }
    link->State = XHCI_SUSP_RESUMING;
    /* Not before 10 ms after the observed U3 (xHCI 1.2c 6.4.3.8). */
    return xhciSuspStep(link, XHCI_SUSP_STEP_SIGNAL,
                        xhciSuspRemaining(link->U3Ms, XHCI_SUSP_U3_DWELL_MS,
                                          nowMs),
                        nowMs, waitMs);
}

/* After a verified U0 (3.6): the recovery wait has run. */
static ULONG xhciSuspAfterRecovery(PXHCI_SUSP_LINK link, ULONG nowMs,
                                   PULONG waitMs)
{
    if (link->Armed) {
        return xhciSuspStep(link, XHCI_SUSP_STEP_DISARM, 0, nowMs, waitMs);
    }
    return xhciSuspStep(link, XHCI_SUSP_STEP_RESTART, 0, nowMs, waitMs);
}

ULONG XhciSuspAdvance(PXHCI_SUSP_LINK link, ULONG result, ULONG nowMs,
                      PULONG waitMs)
{
    ULONG step;

    step = link->Step;
    *waitMs = 0;
    if (step == 0 || step == XHCI_SUSP_STEP_END) {
        return XHCI_SUSP_STEP_END;
    }
    if (result == XHCI_SUSP_RES_DISCONNECT) {
        return xhciSuspEnd(link, XHCI_SUSP_GONE, XHCI_SUSP_OUT_GONE, waitMs);
    }

    switch (step) {
    case XHCI_SUSP_STEP_GATE:
        if (result != XHCI_SUSP_RES_OK) {
            return xhciSuspRefuse(link, XHCI_SUSP_WHY_FAILED, nowMs, waitMs);
        }
        if (link->ArmWanted) {
            return xhciSuspStep(link, XHCI_SUSP_STEP_ARM, 0, nowMs, waitMs);
        }
        return xhciSuspStep(link, XHCI_SUSP_STEP_STOP, 0, nowMs, waitMs);

    case XHCI_SUSP_STEP_ARM:
        if (result == XHCI_SUSP_RES_OK) {
            link->Armed = 1;
            return xhciSuspStep(link, XHCI_SUSP_STEP_STOP, 0, nowMs, waitMs);
        }
        if (result == XHCI_SUSP_RES_STALL) {
            return xhciSuspRefuse(link, XHCI_SUSP_WHY_STALL, nowMs, waitMs);
        }
        if (result == XHCI_SUSP_RES_NOT_SENT) {
            if (!link->BusyRetried) {
                /* Once more, after the ring drains, within 100 ms. */
                link->BusyRetried = 1;
                return xhciSuspStep(link, XHCI_SUSP_STEP_ARM,
                                    XHCI_SUSP_BUSY_RETRY_MS, nowMs, waitMs);
            }
            return xhciSuspRefuse(link, XHCI_SUSP_WHY_BUSY, nowMs, waitMs);
        }
        if (result == XHCI_SUSP_RES_FAILED) {
            return xhciSuspRefuse(link, XHCI_SUSP_WHY_FAILED, nowMs, waitMs);
        }
        return xhciSuspEnd(link, XHCI_SUSP_RECOVERING,
                           XHCI_SUSP_OUT_UNCERTAIN, waitMs);

    case XHCI_SUSP_STEP_STOP:
        link->Stopped = 1;
        link->StopMs = nowMs;
        if (result == XHCI_SUSP_RES_OK) {
            return xhciSuspStep(link, XHCI_SUSP_STEP_U3, 0, nowMs, waitMs);
        }
        if (result == XHCI_SUSP_RES_TIMEOUT) {
            return xhciSuspEnd(link, XHCI_SUSP_RECOVERING,
                               XHCI_SUSP_OUT_UNCERTAIN, waitMs);
        }
        return xhciSuspRefuse(link, XHCI_SUSP_WHY_FAILED, nowMs, waitMs);

    case XHCI_SUSP_STEP_U3:
        if (result == XHCI_SUSP_RES_OK) {
            return xhciSuspStep(link, XHCI_SUSP_STEP_OBSERVE,
                                XHCI_SUSP_OBSERVE_MS, nowMs, waitMs);
        }
        if (result == XHCI_SUSP_RES_TIMEOUT) {
            return xhciSuspEnd(link, XHCI_SUSP_RECOVERING,
                               XHCI_SUSP_OUT_UNCERTAIN, waitMs);
        }
        return xhciSuspRefuse(link, XHCI_SUSP_WHY_FAILED, nowMs, waitMs);

    case XHCI_SUSP_STEP_OBSERVE:
        if (result == XHCI_SUSP_RES_OK && !link->WakeKept) {
            link->U3Ms = nowMs;
            return xhciSuspEnd(link, XHCI_SUSP_SUSPENDED,
                               XHCI_SUSP_OUT_SUSPENDED, waitMs);
        }
        if (result == XHCI_SUSP_RES_OK || result == XHCI_SUSP_RES_WAKE) {
            /* Suspension happened and the device is already waking. */
            link->U3Ms = nowMs;
            return xhciSuspWakeBranch(link, nowMs, waitMs);
        }
        return xhciSuspEnd(link, XHCI_SUSP_RECOVERING,
                           XHCI_SUSP_OUT_UNCERTAIN, waitMs);

    case XHCI_SUSP_STEP_SIGNAL:
        if (result != XHCI_SUSP_RES_OK) {
            return xhciSuspEnd(link, XHCI_SUSP_RECOVERING,
                               XHCI_SUSP_OUT_UNCERTAIN, waitMs);
        }
        if (link->Root) {
            return xhciSuspStep(link, XHCI_SUSP_STEP_U0,
                                XHCI_SUSP_TDRSMDN_MS, nowMs, waitMs);
        }
        return xhciSuspStep(link, XHCI_SUSP_STEP_VERIFY, XHCI_SUSP_VERIFY_MS,
                            nowMs, waitMs);

    case XHCI_SUSP_STEP_U0:
        if (result != XHCI_SUSP_RES_OK) {
            return xhciSuspEnd(link, XHCI_SUSP_RECOVERING,
                               XHCI_SUSP_OUT_UNCERTAIN, waitMs);
        }
        return xhciSuspStep(link, XHCI_SUSP_STEP_VERIFY, XHCI_SUSP_VERIFY_MS,
                            nowMs, waitMs);

    case XHCI_SUSP_STEP_VERIFY:
        if (result != XHCI_SUSP_RES_OK) {
            return xhciSuspEnd(link, XHCI_SUSP_RECOVERING,
                               XHCI_SUSP_OUT_UNCERTAIN, waitMs);
        }
        return xhciSuspStep(link, XHCI_SUSP_STEP_RECOVERY,
                            XHCI_SUSP_TRSMRCY_MS, nowMs, waitMs);

    case XHCI_SUSP_STEP_RECOVERY:
        return xhciSuspAfterRecovery(link, nowMs, waitMs);

    case XHCI_SUSP_STEP_DISARM:
        if (result == XHCI_SUSP_RES_TIMEOUT) {
            return xhciSuspEnd(link, XHCI_SUSP_RECOVERING,
                               XHCI_SUSP_OUT_UNCERTAIN, waitMs);
        }
        /* Best effort on a usable link (3.9): a refusal is counted. */
        link->Armed = 0;
        return xhciSuspStep(link, XHCI_SUSP_STEP_RESTART, 0, nowMs, waitMs);

    case XHCI_SUSP_STEP_RESTART:
        if (link->Reason != XHCI_SUSP_WHY_NONE) {
            link->LatchDue = 1;
            return xhciSuspEnd(link, XHCI_SUSP_ACTIVE, XHCI_SUSP_OUT_REFUSED,
                               waitMs);
        }
        if (link->Kind == XHCI_SUSP_RUN_WAKE && link->WakeClaimed) {
            link->LatchDue = 1;
        }
        return xhciSuspEnd(link, XHCI_SUSP_ACTIVE, XHCI_SUSP_OUT_ACTIVE,
                           waitMs);

    default:
        break;
    }
    return xhciSuspEnd(link, XHCI_SUSP_RECOVERING, XHCI_SUSP_OUT_UNCERTAIN,
                       waitMs);
}

VOID XhciSuspRecovered(PXHCI_SUSP_LINK link, ULONG ok)
{
    link->Step = 0;
    link->Armed = 0;
    if (ok) {
        link->State = XHCI_SUSP_ACTIVE;
        link->Outcome = XHCI_SUSP_OUT_ACTIVE;
        /* 3.5: the uncertain outcome that ends ACTIVE ends as a refusal. */
        link->LatchDue = 1;
        return;
    }
    link->State = XHCI_SUSP_GONE;
    link->Outcome = XHCI_SUSP_OUT_GONE;
}

VOID XhciSuspGone(PXHCI_SUSP_LINK link)
{
    link->State = XHCI_SUSP_GONE;
    link->Step = 0;
    link->Armed = 0;
    link->ClaimDue = 0;
}

/* ------------------------------------------------------------------ */
/* Wake evidence                                                       */
/* ------------------------------------------------------------------ */

ULONG XhciSuspRootEvidence(const XHCI_SUSP_LINK *link, ULONG portsc,
                           ULONG acked, ULONG generation)
{
    ULONG pls;

    /* 3.7's precedence: a disconnect first (Resume with CSC is one). */
    if ((acked & XHCI_PORTSC_CSC) != 0 || (portsc & XHCI_PORTSC_CCS) == 0 ||
        (acked & XHCI_PORTSC_PEC) != 0 || (portsc & XHCI_PORTSC_PED) == 0) {
        return XHCI_SUSP_EV_DISCONNECT;
    }
    if (generation != link->Generation) {
        return XHCI_SUSP_EV_STALE;
    }
    if ((acked & XHCI_PORTSC_PLC) == 0) {
        return XHCI_SUSP_EV_IGNORED;
    }
    pls = XHCI_PORTSC_GET_PLS(portsc);
    if (pls == XHCI_PLS_U3) {
        return XHCI_SUSP_EV_SUSPEND_ENTRY;
    }
    if (pls == XHCI_PLS_RESUME &&
        (link->State == XHCI_SUSP_SUSPENDING ||
         link->State == XHCI_SUSP_SUSPENDED)) {
        return XHCI_SUSP_EV_WAKE;
    }
    if (pls == XHCI_PLS_U0 && link->State == XHCI_SUSP_RESUMING) {
        return XHCI_SUSP_EV_RESUME_DONE;
    }
    return XHCI_SUSP_EV_IGNORED;
}

ULONG XhciSuspHubEvidence(const XHCI_SUSP_LINK *link, ULONG status,
                          ULONG acked, ULONG generation)
{
    if ((acked & XHCI_HUB_C_PORT_CONNECTION) != 0 ||
        (status & XHCI_HUB_PORT_CONNECTION) == 0 ||
        (status & XHCI_HUB_PORT_ENABLE) == 0) {
        return XHCI_SUSP_EV_DISCONNECT;
    }
    if (generation != link->Generation) {
        return XHCI_SUSP_EV_STALE;
    }
    if ((acked & XHCI_HUB_C_PORT_SUSPEND) != 0 &&
        (status & XHCI_HUB_PORT_SUSPEND) == 0) {
        /* A resume that finished: the device's remote wake while the bus
         * held it suspended (3.4 (5), 3.8), the host's own while it
         * resumed. */
        if (link->State == XHCI_SUSP_SUSPENDING ||
            link->State == XHCI_SUSP_SUSPENDED) {
            return XHCI_SUSP_EV_WAKE;
        }
        if (link->State == XHCI_SUSP_RESUMING) {
            return XHCI_SUSP_EV_RESUME_DONE;
        }
        return XHCI_SUSP_EV_IGNORED;
    }
    if ((status & XHCI_HUB_PORT_SUSPEND) != 0 &&
        link->State == XHCI_SUSP_SUSPENDING) {
        return XHCI_SUSP_EV_SUSPEND_ENTRY;
    }
    return XHCI_SUSP_EV_IGNORED;
}

ULONG XhciSuspEvidence(PXHCI_SUSP_LINK link, ULONG event)
{
    if (event == XHCI_SUSP_EV_DISCONNECT) {
        if (link->Step == 0 || link->Step == XHCI_SUSP_STEP_END) {
            XhciSuspGone(link);
        }
        return 0;
    }
    if (event != XHCI_SUSP_EV_WAKE) {
        return 0;
    }
    if (link->State == XHCI_SUSP_SUSPENDING) {
        link->WakeKept = 1;
        return 0;
    }
    if (link->State == XHCI_SUSP_SUSPENDED && !link->WakeTaken) {
        link->WakeTaken = 1;
        link->ClaimDue = 1;
        return 1;
    }
    return 0;
}

ULONG XhciSuspWakeClaims(ULONG stamp, ULONG watermark, ULONG pwrState)
{
    if (!xhciSuspIsWakeState(pwrState)) {
        return 0;
    }
    return ((LONG)(stamp - watermark) < 0) ? 1UL : 0UL;
}

VOID XhciSuspWakeCount(PXHCI_SUSP_LINK link, ULONG claimed)
{
    link->ClaimDue = 0;
    if (claimed != 0) {
        link->WakeClaimed = 1;
    }
}

static ULONG xhciSuspRootGone(ULONG portsc)
{
    return ((portsc & XHCI_PORTSC_CCS) == 0 ||
            (portsc & XHCI_PORTSC_CSC) != 0 ||
            (portsc & XHCI_PORTSC_PED) == 0) ? 1UL : 0UL;
}

static ULONG xhciSuspHubGone(ULONG status, ULONG change)
{
    return ((status & XHCI_HUB_PORT_CONNECTION) == 0 ||
            (change & XHCI_HUB_C_PORT_CONNECTION) != 0 ||
            (status & XHCI_HUB_PORT_ENABLE) == 0) ? 1UL : 0UL;
}

ULONG XhciSuspObserveRoot(ULONG portsc)
{
    ULONG pls;

    if (xhciSuspRootGone(portsc)) {
        return XHCI_SUSP_RES_DISCONNECT;
    }
    pls = XHCI_PORTSC_GET_PLS(portsc);
    if (pls == XHCI_PLS_U3) {
        return XHCI_SUSP_RES_OK;
    }
    if (pls == XHCI_PLS_RESUME) {
        return XHCI_SUSP_RES_WAKE;
    }
    return XHCI_SUSP_POLL_AGAIN;
}

ULONG XhciSuspObserveHub(ULONG status, ULONG change)
{
    if (xhciSuspHubGone(status, change)) {
        return XHCI_SUSP_RES_DISCONNECT;
    }
    if ((status & XHCI_HUB_PORT_SUSPEND) != 0) {
        return XHCI_SUSP_RES_OK;
    }
    if ((change & XHCI_HUB_C_PORT_SUSPEND) != 0) {
        /* PORT_SUSPEND clear with C_PORT_SUSPEND: a remote wake that
         * already finished (3.4 (5)). */
        return XHCI_SUSP_RES_WAKE;
    }
    return XHCI_SUSP_POLL_AGAIN;
}

ULONG XhciSuspVerifyRoot(ULONG portsc)
{
    if (xhciSuspRootGone(portsc)) {
        return XHCI_SUSP_RES_DISCONNECT;
    }
    if (XHCI_PORTSC_GET_PLS(portsc) == XHCI_PLS_U0) {
        return XHCI_SUSP_RES_OK;
    }
    return XHCI_SUSP_POLL_AGAIN;
}

ULONG XhciSuspVerifyHub(ULONG status, ULONG change)
{
    if (xhciSuspHubGone(status, change)) {
        return XHCI_SUSP_RES_DISCONNECT;
    }
    if ((status & XHCI_HUB_PORT_SUSPEND) == 0) {
        return XHCI_SUSP_RES_OK;
    }
    return XHCI_SUSP_POLL_AGAIN;
}

/* ------------------------------------------------------------------ */
/* WAIT_WAKE                                                           */
/* ------------------------------------------------------------------ */

ULONG XhciSuspWaitWakeAdmit(const XHCI_SUSP_WW_IN *in)
{
    if (in->Released) {
        return XHCI_SUSP_ST_NO_DEVICE;
    }
    if (!in->Eligible || !in->RemoteWake || in->WakeOff9x || in->Closed) {
        return XHCI_SUSP_ST_NOT_SUPPORTED;
    }
    /* SystemWake is S0 (5.2): a deeper system state is refused. */
    if (in->SystemState != 1UL || in->PwrState == XHCI_SUSP_D3) {
        return XHCI_SUSP_ST_INVALID_STATE;
    }
    if (in->Held) {
        return XHCI_SUSP_ST_BUSY;
    }
    return XHCI_SUSP_ST_PENDING;
}

/* ------------------------------------------------------------------ */
/* The idle notification                                               */
/* ------------------------------------------------------------------ */

ULONG XhciSuspIdleSubmit(ULONG released, ULONG eligible, ULONG closed,
                         ULONG owned)
{
    if (released || closed) {
        return XHCI_SUSP_ST_NO_DEVICE;
    }
    if (owned) {
        return XHCI_SUSP_ST_BUSY;
    }
    return eligible ? XHCI_SUSP_ST_PENDING : XHCI_SUSP_ST_TODAY;
}

ULONG XhciSuspIdleBatchReady(const XHCI_SUSP_IDLE_FUNC *funcs, ULONG count,
                             ULONG link, ULONG systemS0)
{
    ULONG i;
    ULONG started;

    if (link != XHCI_SUSP_ACTIVE || !systemS0) {
        return 0;
    }
    started = 0;
    for (i = 0; i < count; i++) {
        if (!funcs[i].Started || funcs[i].Closed) {
            continue;
        }
        started++;
        if (funcs[i].IdleState != XHCI_SUSP_IDLE_HELD ||
            funcs[i].PwrState != XHCI_SUSP_D0) {
            return 0;
        }
    }
    return (started != 0) ? 1UL : 0UL;
}

ULONG XhciSuspIdleClaim(const XHCI_SUSP_CLAIM_IN *in)
{
    if (!in->SlotFull || !in->Owned) {
        /* Empty: cancelled and completed. Not owned: the cancel routine
         * completes it. Either way only the entry is let go. */
        return XHCI_SUSP_CLAIM_LET_GO;
    }
    if (in->IdleState == XHCI_SUSP_IDLE_QUEUED && in->BatchCur != 0 &&
        in->EntryBatch == in->BatchCur && in->Started && !in->Closed &&
        in->PwrState == XHCI_SUSP_D0 && in->SystemS0 &&
        in->EntryEpoch == in->Epoch) {
        return XHCI_SUSP_CLAIM_RUN;
    }
    return XHCI_SUSP_CLAIM_SKIP;
}

ULONG XhciSuspIdleSettle(ULONG trigger, ULONG idleState, ULONG pwrState)
{
    if (idleState == XHCI_SUSP_IDLE_NONE) {
        return XHCI_SUSP_ST_NONE;
    }
    if (idleState == XHCI_SUSP_IDLE_RUNNING) {
        /* Claimed only after its callback returns (6.5). */
        return XHCI_SUSP_ST_PENDING;
    }
    switch (trigger) {
    case XHCI_SUSP_SET_CANCEL:
    case XHCI_SUSP_SET_GONE:
    case XHCI_SUSP_SET_SYSTEM:
    case XHCI_SUSP_SET_SKIP:
        return XHCI_SUSP_ST_CANCELLED;
    case XHCI_SUSP_SET_D3:
        return XHCI_SUSP_ST_POWER_INVALID;
    case XHCI_SUSP_SET_D0:
        return (idleState != XHCI_SUSP_IDLE_HELD) ? XHCI_SUSP_ST_SUCCESS
                                                  : XHCI_SUSP_ST_NONE;
    case XHCI_SUSP_SET_EPISODE:
        return (idleState == XHCI_SUSP_IDLE_RETURNED &&
                xhciSuspIsWakeState(pwrState)) ? XHCI_SUSP_ST_SUCCESS
                                               : XHCI_SUSP_ST_NONE;
    default:
        break;
    }
    return XHCI_SUSP_ST_NONE;
}

ULONG XhciSuspEpisodeEnds(ULONG event)
{
    switch (event) {
    case XHCI_SUSP_EPI_CANCEL:
    case XHCI_SUSP_EPI_OWN_D0:
    case XHCI_SUSP_EPI_SKIP:
    case XHCI_SUSP_EPI_REFUSED:
    case XHCI_SUSP_EPI_SYSTEM:
    case XHCI_SUSP_EPI_GONE:
        return 1;
    default:
        break;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Cancellable IRPs                                                    */
/* ------------------------------------------------------------------ */

ULONG XhciSuspPublish(ULONG cancel, ULONG cleared)
{
    if (!cancel) {
        return XHCI_SUSP_PUB_PUBLISHED;
    }
    return cleared ? XHCI_SUSP_PUB_TAKE_BACK : XHCI_SUSP_PUB_ROUTINE;
}

ULONG XhciSuspClaim(ULONG slotFull, ULONG cleared)
{
    return (slotFull && cleared) ? 1UL : 0UL;
}

/* ------------------------------------------------------------------ */
/* The callback context's phases                                       */
/* ------------------------------------------------------------------ */

ULONG XhciSuspPhaseAllowed(ULONG actor, ULONG from, ULONG to)
{
    if (actor == XHCI_SUSP_ACTOR_THREAD) {
        return ((from == XHCI_SUSP_PH_IDLE && to == XHCI_SUSP_PH_CLAIMING) ||
                (from == XHCI_SUSP_PH_CLAIMING &&
                 (to == XHCI_SUSP_PH_IN_CALLBACK ||
                  to == XHCI_SUSP_PH_IDLE)) ||
                (from == XHCI_SUSP_PH_IN_CALLBACK &&
                 to == XHCI_SUSP_PH_RETURNING) ||
                (from == XHCI_SUSP_PH_RETURNING &&
                 to == XHCI_SUSP_PH_IDLE)) ? 1UL : 0UL;
    }
    if (actor == XHCI_SUSP_ACTOR_TEARDOWN) {
        return ((from == XHCI_SUSP_PH_IN_CALLBACK &&
                 to == XHCI_SUSP_PH_ABANDONED) ||
                (from == XHCI_SUSP_PH_IDLE &&
                 to == XHCI_SUSP_PH_EXITING)) ? 1UL : 0UL;
    }
    return 0;
}

ULONG XhciSuspHandoff(ULONG phase)
{
    switch (phase) {
    case XHCI_SUSP_PH_IDLE:
        return XHCI_SUSP_HAND_EXIT;
    case XHCI_SUSP_PH_IN_CALLBACK:
        return XHCI_SUSP_HAND_ABANDON;
    case XHCI_SUSP_PH_CLAIMING:
    case XHCI_SUSP_PH_RETURNING:
        return XHCI_SUSP_HAND_WAIT;
    default:
        break;
    }
    return XHCI_SUSP_HAND_DONE;
}

/* ------------------------------------------------------------------ */
/* Finalization                                                        */
/* ------------------------------------------------------------------ */

ULONG XhciSuspFinalize(const XHCI_SUSP_FIN_IN *in)
{
    ULONG owed;

    owed = (in->UrbsPending || in->CallbackRunning) ? 1UL : 0UL;
    if (!owed) {
        return XHCI_SUSP_FIN_NOW;
    }
    if (in->CallerIsThread) {
        /* Never a wait on the enum thread by itself (4.4 (b) finish). */
        return XHCI_SUSP_FIN_DEFER;
    }
    if (in->ThreadExited && !in->DmaHalted) {
        return XHCI_SUSP_FIN_QUARANTINE;
    }
    return XHCI_SUSP_FIN_WAIT;
}
