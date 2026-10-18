/*
 * cm_live.c - the host-interface liveness monitor's pure half (cm_live.h,
 * design record 21 sections 5.5 and 5.6). DDK-free and host-tested
 * (test\test_cm_live.c).
 *
 * IRQL: any. Takes no lock (see cm_live.h for the one concurrent call).
 */

#include "cm_live.h"

#define CM_LIVE_ONES 0xFFFFFFFFUL

static BOOLEAN cmLiveRevoked(const CM_LIVE *live)
{
    return (BOOLEAN)(live->Revoked != 0 || live->State == CM_LIVE_REVOKED);
}

/* The cadence of a monitored state; 0 for one that is not sampled. */
static ULONG cmLivePeriod(const CM_LIVE *live)
{
    switch (live->State) {
    case CM_LIVE_ACTIVE:
    case CM_LIVE_SUSPECT:
        return live->PeriodMs;
    case CM_LIVE_ABSENT:
    case CM_LIVE_CONFIG_LOST:
    case CM_LIVE_RECOVERY_FAILED:
        return CM_LIVE_SLOW_MS;
    case CM_LIVE_CONTAINED:
        /* Record 22 section 7.7: probed once a second, final excepted. */
        return live->Sub == CM_LIVE_SUB_FINAL ? 0 : CM_LIVE_SLOW_MS;
    default:
        return 0;
    }
}

VOID CmLiveInit(PCM_LIVE live, ULONG usb4, ULONG periodMs)
{
    ULONG i;
    PUCHAR p;

    p = (PUCHAR)live;
    for (i = 0; i < sizeof(*live); i++) {
        p[i] = 0;
    }
    live->State = CM_LIVE_OFF;
    live->Usb4 = usb4 ? 1UL : 0UL;
    live->PeriodMs = periodMs != 0 ? periodMs : 200UL;
}

BOOLEAN CmLiveArm(PCM_LIVE live, ULONG baseline, ULONG nowMs)
{
    if (cmLiveRevoked(live) || live->State == CM_LIVE_CONTAINED ||
        baseline == CM_LIVE_ONES) {
        return FALSE;
    }
    live->State = CM_LIVE_ACTIVE;
    live->Baseline = baseline;
    live->Ones = 0;
    live->LastMs = nowMs;
    live->HaveLast = 1;
    live->Rearm = 0;
    return TRUE;
}

VOID CmLiveDisarm(PCM_LIVE live)
{
    if (live->State != CM_LIVE_REVOKED && live->State != CM_LIVE_CONTAINED &&
        live->State != CM_LIVE_PREPARING &&
        !(live->Usb4 && live->State == CM_LIVE_RECOVERING)) {
        live->State = CM_LIVE_OFF;
    }
    live->Ones = 0;
    live->HaveLast = 0;
}

VOID CmLiveTransition(PCM_LIVE live, BOOLEAN begin)
{
    live->InTransition = begin ? 1UL : 0UL;
    live->Ones = 0;
    live->HaveLast = 0;
}

BOOLEAN CmLiveDue(const CM_LIVE *live, ULONG nowMs)
{
    ULONG period;

    if (cmLiveRevoked(live) || live->InTransition) {
        return FALSE;
    }
    period = cmLivePeriod(live);
    if (period == 0) {
        return FALSE;
    }
    if (!live->HaveLast) {
        return TRUE;
    }
    return (BOOLEAN)(nowMs - live->LastMs >= period);
}

ULONG CmLiveWaitMs(const CM_LIVE *live)
{
    if (cmLiveRevoked(live) || live->InTransition) {
        return 0;
    }
    return cmLivePeriod(live);
}

/* A loss seen: the USB4 profile is contained at once (record 22: no
 * recovery path), the ICM's stops its transport. */
static ULONG cmLiveLoss(PCM_LIVE live, ULONG state)
{
    live->Episode++;
    if (live->Usb4) {
        live->State = CM_LIVE_CONTAINED;
        live->Sub = CM_LIVE_SUB_PROBING;
        live->Rearm = 0;
        live->Counters.Contained++;
        return CM_LIVE_ACT_STOP_TRANSPORT | CM_LIVE_ACT_CONTAIN |
               CM_LIVE_ACT_LOG;
    }
    live->State = state;
    return CM_LIVE_ACT_STOP_TRANSPORT | CM_LIVE_ACT_LOG;
}

/* RECONCILE, once per episode. */
static ULONG cmLiveReconcile(PCM_LIVE live)
{
    if (live->Reconciled == live->Episode) {
        return 0;
    }
    live->Reconciled = live->Episode;
    return CM_LIVE_ACT_RECONCILE;
}

static ULONG cmLiveConfigLost(PCM_LIVE live)
{
    live->State = CM_LIVE_CONFIG_LOST;
    live->Counters.ConfigLost++;
    live->Full = 1;
    live->Ones = 0;
    return cmLiveReconcile(live) | CM_LIVE_ACT_VALIDATE | CM_LIVE_ACT_LOG;
}

ULONG CmLiveSample(PCM_LIVE live, ULONG nowMs, ULONG value)
{
    BOOLEAN ones;
    BOOLEAN mismatch;
    ULONG act;

    if (cmLiveRevoked(live) || live->InTransition ||
        cmLivePeriod(live) == 0) {
        return 0;
    }
    live->Counters.Samples++;
    live->LastMs = nowMs;
    live->HaveLast = 1;
    ones = (BOOLEAN)(value == CM_LIVE_ONES);
    mismatch = (BOOLEAN)(!ones && live->Baseline != 0 &&
                         value != live->Baseline);
    act = 0;

    switch (live->State) {
    case CM_LIVE_ACTIVE:
        if (ones) {
            live->Ones = 1;
            act = cmLiveLoss(live, CM_LIVE_SUSPECT);
            if (live->State == CM_LIVE_SUSPECT) {
                live->Counters.Suspects++;
            }
        } else if (mismatch) {
            act = cmLiveLoss(live, CM_LIVE_CONFIG_LOST);
            if (live->State == CM_LIVE_CONFIG_LOST) {
                act |= cmLiveConfigLost(live);
            }
        }
        break;

    case CM_LIVE_SUSPECT:
        if (ones) {
            live->Ones++;
            if (live->Ones >= CM_LIVE_ONES_FOR_ABSENT) {
                live->State = CM_LIVE_ABSENT;
                live->Counters.Absents++;
                act = cmLiveReconcile(live) | CM_LIVE_ACT_LOG;
            }
        } else if (mismatch) {
            act = cmLiveConfigLost(live);
        } else {
            /* Back, unchanged: a bounded rediscovery on the retained
             * rings, never a return to ACTIVE without it. */
            live->Ones = 0;
            live->Full = 0;
            act = CM_LIVE_ACT_VALIDATE | CM_LIVE_ACT_LOG;
        }
        break;

    case CM_LIVE_ABSENT:
    case CM_LIVE_CONFIG_LOST:
        if (ones) {
            if (live->State == CM_LIVE_CONFIG_LOST) {
                live->State = CM_LIVE_ABSENT;
                live->Counters.Absents++;
                act = cmLiveReconcile(live) | CM_LIVE_ACT_LOG;
            }
            live->Ones++;
        } else if (mismatch) {
            act = cmLiveConfigLost(live);
        } else {
            live->Ones = 0;
            live->Full = 0;
            act = CM_LIVE_ACT_VALIDATE | CM_LIVE_ACT_LOG;
        }
        break;

    case CM_LIVE_RECOVERY_FAILED:
        /* Only a new episode - an unanswered sample, then an answered
         * one - re-arms a recovery; a persisting mismatch does not. */
        if (ones) {
            live->Rearm = 1;
            live->Ones++;
        } else if (live->Rearm) {
            live->Rearm = 0;
            live->Episode++;
            live->Ones = 0;
            if (mismatch) {
                act = cmLiveConfigLost(live);
            } else {
                live->State = CM_LIVE_ABSENT;
                live->Full = 0;
                act = cmLiveReconcile(live) | CM_LIVE_ACT_VALIDATE |
                      CM_LIVE_ACT_LOG;
            }
        }
        break;

    default:
        break;
    }
    return act;
}

ULONG CmLiveValidated(PCM_LIVE live, BOOLEAN ok)
{
    if (cmLiveRevoked(live)) {
        return 0;
    }
    if (live->State != CM_LIVE_SUSPECT && live->State != CM_LIVE_ABSENT &&
        live->State != CM_LIVE_CONFIG_LOST) {
        return 0;
    }
    if (!ok) {
        live->State = CM_LIVE_RECOVERY_FAILED;
        live->Rearm = 0;
        live->Counters.RecoveryFailed++;
        /* The old generations are retired whichever way recovery failed,
         * once per episode, so none stays advertised. */
        return cmLiveReconcile(live) | CM_LIVE_ACT_LOG;
    }
    live->State = CM_LIVE_RECOVERING;
    live->Counters.Recoveries++;
    return (live->Full ? CM_LIVE_ACT_RECOVER_FULL : CM_LIVE_ACT_RECOVER_LIGHT) |
           CM_LIVE_ACT_LOG;
}

BOOLEAN CmLiveRecovered(PCM_LIVE live, ULONG episode, BOOLEAN ok,
                        ULONG baseline, ULONG nowMs)
{
    if (cmLiveRevoked(live) || live->State != CM_LIVE_RECOVERING ||
        episode != live->Episode) {
        live->Counters.LateCompletions++;
        return FALSE;
    }
    if (!ok || baseline == CM_LIVE_ONES ||
        (live->Baseline != 0 && baseline != live->Baseline)) {
        /* Not answered, or the final sample is not what the recovery
         * programmed: a failure, never an ACTIVE without detection. */
        live->State = CM_LIVE_RECOVERY_FAILED;
        live->Rearm = 0;
        live->Counters.RecoveryFailed++;
        return FALSE;
    }
    live->State = CM_LIVE_ACTIVE;
    live->Baseline = baseline;
    live->Ones = 0;
    live->LastMs = nowMs;
    live->HaveLast = 1;
    live->Counters.RecoveredOk++;
    return TRUE;
}

VOID CmLiveRevoke(PCM_LIVE live)
{
    live->Revoked = 1;
    live->Counters.Revokes++;
}

BOOLEAN CmLiveAdmits(const CM_LIVE *live, ULONG cls)
{
    if (cmLiveRevoked(live)) {
        return FALSE;
    }
    switch (cls) {
    case CM_ACCESS_PROBE:
        return TRUE;
    case CM_ACCESS_RECOVERY:
        return (BOOLEAN)(live->State == CM_LIVE_RECOVERING);
    case CM_ACCESS_PREPARE:
        return (BOOLEAN)(live->State == CM_LIVE_PREPARING);
    default:
        return (BOOLEAN)(live->State == CM_LIVE_OFF ||
                         live->State == CM_LIVE_ACTIVE);
    }
}

BOOLEAN CmLiveTransport(const CM_LIVE *live)
{
    return CmLiveAdmits(live, CM_ACCESS_ORDINARY);
}

ULONG CmLiveEpisode(const CM_LIVE *live)
{
    return live->Episode;
}

ULONG CmLiveReconcileOwed(PCM_LIVE live)
{
    if (cmLiveRevoked(live) || live->State != CM_LIVE_RECOVERY_FAILED) {
        return 0;
    }
    return cmLiveReconcile(live);
}

/* ---- the USB4 profile's recovery (record 22 section 7.7 part 5) ---- */

VOID CmLiveContain(PCM_LIVE live, ULONG nowMs)
{
    if (cmLiveRevoked(live) || live->State == CM_LIVE_CONTAINED) {
        return;
    }
    live->Episode++;
    live->Counters.Contained++;
    live->State = CM_LIVE_CONTAINED;
    live->Sub = CM_LIVE_SUB_PROBING;
    live->Rearm = 0;
    live->Ones = 0;
    live->LastMs = nowMs;
    live->HaveLast = 1;
}

ULONG CmLiveProbe(PCM_LIVE live, ULONG nowMs, ULONG result)
{
    if (cmLiveRevoked(live) || live->State != CM_LIVE_CONTAINED ||
        live->InTransition) {
        return 0;
    }
    live->Counters.Samples++;
    live->LastMs = nowMs;
    live->HaveLast = 1;
    switch (live->Sub) {
    case CM_LIVE_SUB_PROBING:
        if (result == CM_LIVE_PROBE_OK) {
            live->State = CM_LIVE_PREPARING;
            return CM_LIVE_ACT_PREPARE | CM_LIVE_ACT_LOG;
        }
        if (result == CM_LIVE_PROBE_BAR) {
            /* A base address is the bus driver's to assign: this episode
             * is over, a new one is the trigger. */
            live->Sub = CM_LIVE_SUB_COOLDOWN;
            live->Rearm = 0;
            live->Counters.RecoveryFailed++;
            return CM_LIVE_ACT_LOG;
        }
        return 0;
    case CM_LIVE_SUB_COOLDOWN:
        if (result == CM_LIVE_PROBE_ONES) {
            live->Rearm = 1;
            return 0;
        }
        if (live->Rearm) {
            live->Rearm = 0;
            live->Episode++;
            live->Sub = CM_LIVE_SUB_PROBING;
            return CM_LIVE_ACT_SNAPSHOT | CM_LIVE_ACT_LOG;
        }
        return 0;
    default:
        return 0;
    }
}

static VOID cmLiveCooldown(PCM_LIVE live, BOOLEAN ones)
{
    live->State = CM_LIVE_CONTAINED;
    live->Sub = CM_LIVE_SUB_COOLDOWN;
    live->Rearm = ones ? 1UL : 0UL;
    live->Ones = 0;
    live->Counters.RecoveryFailed++;
}

ULONG CmLivePrepared(PCM_LIVE live, BOOLEAN ok, BOOLEAN ones)
{
    if (cmLiveRevoked(live) || live->State != CM_LIVE_PREPARING) {
        return 0;
    }
    if (!ok) {
        cmLiveCooldown(live, ones);
        return CM_LIVE_ACT_LOG;
    }
    live->State = CM_LIVE_RECOVERING;
    live->Counters.Recoveries++;
    return CM_LIVE_ACT_RECOVER_FULL | CM_LIVE_ACT_LOG;
}

BOOLEAN CmLiveU4Recovered(PCM_LIVE live, ULONG episode, BOOLEAN ok,
                          BOOLEAN final, BOOLEAN ones, ULONG sample,
                          ULONG nowMs)
{
    if (cmLiveRevoked(live) || live->State != CM_LIVE_RECOVERING ||
        episode != live->Episode) {
        live->Counters.LateCompletions++;
        return FALSE;
    }
    if (final) {
        live->State = CM_LIVE_CONTAINED;
        live->Sub = CM_LIVE_SUB_FINAL;
        live->Counters.RecoveryFailed++;
        return FALSE;
    }
    if (!ok || sample == CM_LIVE_ONES ||
        (live->Baseline != 0 && sample != live->Baseline)) {
        cmLiveCooldown(live, (BOOLEAN)(ones || sample == CM_LIVE_ONES));
        return FALSE;
    }
    live->State = CM_LIVE_ACTIVE;
    live->Sub = CM_LIVE_SUB_PROBING;
    live->Baseline = sample;
    live->Ones = 0;
    live->Rearm = 0;
    live->LastMs = nowMs;
    live->HaveLast = 1;
    live->Counters.RecoveredOk++;
    return TRUE;
}

VOID CmLiveU4Interrupt(PCM_LIVE live)
{
    if (cmLiveRevoked(live) ||
        (live->State != CM_LIVE_PREPARING &&
         !(live->Usb4 && live->State == CM_LIVE_RECOVERING))) {
        return;
    }
    cmLiveCooldown(live, TRUE);
}

ULONG CmLiveU4Wake(PCM_LIVE live)
{
    if (cmLiveRevoked(live) || live->State != CM_LIVE_CONTAINED ||
        live->Sub == CM_LIVE_SUB_FINAL) {
        return 0;
    }
    live->HaveLast = 0;
    if (live->Sub == CM_LIVE_SUB_COOLDOWN && live->Rearm) {
        live->Rearm = 0;
        live->Episode++;
        live->Sub = CM_LIVE_SUB_PROBING;
        return CM_LIVE_ACT_SNAPSHOT | CM_LIVE_ACT_LOG;
    }
    return 0;
}
