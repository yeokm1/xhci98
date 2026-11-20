/*
 * xhci_retry.c - a controller stopped after repeated failures, tried again:
 * the pure half (roadmap-usb4.md task 50.31, design record 38). The
 * contracts are xhci_retry.h's.
 *
 * DDK-free: part of the pure core. C89. IRQL: any; no lock, no register.
 */

#include "xhci.h"
#include "xhci_retry.h"

static ULONG xhciRetrySat(ULONG v, ULONG max)
{
    return (v < max) ? v + 1UL : max;
}

ULONG XhciTolRetryValue(ULONG found, ULONG value)
{
    if (found != XHCI_TOL_RETRY_DWORD) {
        return XHCI_TOL_RETRY_DEFAULT_S;
    }
    if (value == 0) {
        return 0;
    }
    if (value >= XHCI_TOL_RETRY_MIN_S && value <= XHCI_TOL_RETRY_MAX_S) {
        return value;
    }
    return XHCI_TOL_RETRY_DEFAULT_S;
}

ULONG XhciTolRetryScale(ULONG found, ULONG value)
{
    if (!found || value <= 1UL) {
        return 1;
    }
    return (value > XHCI_TOL_RETRY_SCALE_MAX) ? XHCI_TOL_RETRY_SCALE_MAX
                                               : value;
}

ULONG XhciTolRetryScaled(ULONG ticks, ULONG scale)
{
    ULONG t;

    if (scale <= 1UL) {
        return (ticks == 0) ? 1UL : ticks;
    }
    t = ticks / scale;
    return (t == 0) ? 1UL : t;
}

VOID XhciTolRetryInit(PXHCI_TOL_RETRY r, ULONG found, ULONG raw,
                      ULONG scale)
{
    PUCHAR p;
    ULONG i;

    p = (PUCHAR)r;
    for (i = 0; i < sizeof(*r); i++) {
        p[i] = 0;
    }
    r->Found = (found <= XHCI_TOL_RETRY_OTHER_TYPE) ? found
                                                 : XHCI_TOL_RETRY_OTHER_TYPE;
    r->Raw = raw;
    r->Value = XhciTolRetryValue(found, raw);
    r->Scale = (scale == 0) ? 1UL : scale;
    if (r->Value != 0) {
        /* V * 1000 is at most 600 000: no overflow, no 64-bit helper. */
        r->Base = XhciTolRetryScaled(
            XHCI_TOL_RETRY_TICKS(r->Value * 1000UL), r->Scale);
        r->Ceiling = XhciTolRetryScaled(XHCI_TOL_RETRY_CEILING_TICKS,
                                        r->Scale);
        if (r->Ceiling < r->Base) {
            r->Ceiling = r->Base;
        }
    }
    r->CleanTicks = XhciTolRetryScaled(XHCI_TOL_RETRY_CLEAN_TICKS, r->Scale);
    r->StormTicks = XhciTolRetryScaled(XHCI_TOL_RETRY_STORM_TICKS, r->Scale);
    r->State = (r->Value == 0) ? XHCI_RETRY_OFF : XHCI_RETRY_WATCHING;
}

/* The wait at `level` in milliseconds (record 38 section 5.7): the value's
 * seconds doubled with the cap checked first, so w + w never overflows. */
static ULONG xhciRetryWaitMs(const XHCI_TOL_RETRY *r, ULONG level)
{
    ULONG w;
    ULONG i;

    w = r->Value * 1000UL;
    for (i = 0; i < level && w < XHCI_TOL_RETRY_CEILING_MS; i++) {
        w = (w > XHCI_TOL_RETRY_CEILING_MS - w) ? XHCI_TOL_RETRY_CEILING_MS
                                                : w + w;
    }
    return (w > XHCI_TOL_RETRY_CEILING_MS) ? XHCI_TOL_RETRY_CEILING_MS : w;
}

ULONG XhciTolRetryWait(const XHCI_TOL_RETRY *r, ULONG level)
{
    ULONG w;

    if (r->Value == 0) {
        return r->Base;
    }
    w = XhciTolRetryScaled(XHCI_TOL_RETRY_TICKS(xhciRetryWaitMs(r, level)),
                           r->Scale);
    if (w < r->Base) {
        w = r->Base;
    }
    return (w > r->Ceiling) ? r->Ceiling : w;
}

ULONG XhciTolRetryLevelUp(const XHCI_TOL_RETRY *r, ULONG level)
{
    if (r->Value == 0 ||
        xhciRetryWaitMs(r, level) >= XHCI_TOL_RETRY_CEILING_MS) {
        return level;
    }
    return level + 1UL;
}

ULONG XhciTolRetryFloor(ULONG ticks)
{
    /* (ticks - 1) x 100 ms: at most 600 000 for the ceiling's 6 001. */
    if (ticks <= 1UL) {
        return 1;
    }
    if (ticks > XHCI_TOL_RETRY_CEILING_TICKS) {
        ticks = XHCI_TOL_RETRY_CEILING_TICKS;
    }
    return XHCI_TOL_RETRY_UNITS((ticks - 1UL) * XHCI_TOL_RETRY_TICK_MS);
}

ULONG XhciTolRetrySysElapsed(ULONG nowSys, ULONG stamp, ULONG units)
{
    return ((ULONG)(nowSys - stamp) >= units) ? 1UL : 0UL;
}

static VOID xhciRetryLatch(PXHCI_TOL_RETRY r, ULONG now, ULONG nowSys)
{
    r->State = XHCI_RETRY_LATCHED;
    r->LatchStamp = now;
    r->LatchSys = nowSys;
    r->LatchWait = XhciTolRetryWait(r, r->Level);
    r->DeadlineValid = 1;
    r->Armed = 0;
    r->PortPending = 0;
    r->SampleOff = 0;
    r->CleanPending = 0;
    r->CleanArmed = 0;
    r->Noted = 0;
}

ULONG XhciTolRetryObserve(PXHCI_TOL_RETRY r, ULONG terminal, ULONG pinned,
                          ULONG now, ULONG nowSys)
{
    if (r->State == XHCI_RETRY_OFF || r->State == XHCI_RETRY_EXCLUDED) {
        return XHCI_RETRY_EV_NONE;
    }
    if (pinned || terminal == XHCI_TOL_TERMINAL_UNREADABLE ||
        terminal == XHCI_TOL_TERMINAL_DMA_UNPROVEN ||
        terminal == XHCI_TOL_TERMINAL_MSG4) {
        if (r->Burst) {
            XhciTolRetryBurstEnd(r, XHCI_RETRY_BURST_EXCLUDED);
        }
        r->State = XHCI_RETRY_EXCLUDED;
        r->Armed = 0;
        r->DeadlineValid = 0;
        return XHCI_RETRY_EV_EXCLUDED;
    }
    if (r->State == XHCI_RETRY_WATCHING &&
        (terminal == XHCI_TOL_TERMINAL_FAILURES ||
         terminal == XHCI_TOL_TERMINAL_WINDOW)) {
        xhciRetryLatch(r, now, nowSys);
        return XHCI_RETRY_EV_LATCHED;
    }
    if ((r->State == XHCI_RETRY_LATCHED || r->State == XHCI_RETRY_DUE) &&
        terminal != XHCI_TOL_TERMINAL_FAILURES &&
        terminal != XHCI_TOL_TERMINAL_WINDOW) {
        /* Only a try leaves a latch in a lifetime; anything else seen here
         * is not a latch to keep counting toward. */
        r->State = XHCI_RETRY_WATCHING;
        r->Armed = 0;
        r->DeadlineValid = 0;
        r->PortPending = 0;
    }
    return XHCI_RETRY_EV_NONE;
}

static ULONG xhciRetryStormFull(const XHCI_TOL_RETRY *r, ULONG now)
{
    if (r->StormCount < XHCI_TOL_RETRY_STORM_MAX) {
        return 0;
    }
    return XhciTolElapsed(now, r->StormStamp[0], r->StormTicks) ? 0UL : 1UL;
}

static VOID xhciRetryStormPush(PXHCI_TOL_RETRY r, ULONG now)
{
    ULONG i;

    if (r->StormCount < XHCI_TOL_RETRY_STORM_MAX) {
        r->StormStamp[r->StormCount] = now;
        r->StormCount++;
        return;
    }
    for (i = 1; i < XHCI_TOL_RETRY_STORM_MAX; i++) {
        r->StormStamp[i - 1] = r->StormStamp[i];
    }
    r->StormStamp[XHCI_TOL_RETRY_STORM_MAX - 1] = now;
}

ULONG XhciTolRetryDueStep(PXHCI_TOL_RETRY r, ULONG episode, ULONG now,
                          ULONG nowSys)
{
    if (r->State != XHCI_RETRY_LATCHED) {
        return XHCI_RETRY_CAUSE_NONE;
    }
    if (episode) {
        r->PortPending = 1;
    }
    /* Section 5.7: both clocks, the ticks at the nominal period and the
     * system time's floor. */
    if (r->DeadlineValid && XhciTolElapsed(now, r->LatchStamp, r->LatchWait) &&
        XhciTolRetrySysElapsed(nowSys, r->LatchSys,
                               XhciTolRetryFloor(r->LatchWait))) {
        /* The deadline wins a tie; the episode is consumed with it, no
         * port-change try counted. */
        r->State = XHCI_RETRY_DUE;
        r->ArmedCause = XHCI_RETRY_CAUSE_WAIT;
        r->PortPending = 0;
        return XHCI_RETRY_CAUSE_WAIT;
    }
    if (!r->PortPending || !XhciTolElapsed(now, r->LatchStamp, r->Base) ||
        !XhciTolRetrySysElapsed(nowSys, r->LatchSys,
                                XhciTolRetryFloor(r->Base))) {
        return XHCI_RETRY_CAUSE_NONE;
    }
    if (xhciRetryStormFull(r, now)) {
        r->PortPending = 0;
        r->StormRefused = xhciRetrySat(r->StormRefused, 0xFFFFFFFFUL);
        return XHCI_RETRY_CAUSE_NONE;
    }
    r->State = XHCI_RETRY_DUE;
    r->ArmedCause = XHCI_RETRY_CAUSE_PORT;
    r->PortPending = 0;
    return XHCI_RETRY_CAUSE_PORT;
}

ULONG XhciTolRetryArm(PXHCI_TOL_RETRY r)
{
    if (r->State != XHCI_RETRY_DUE || r->Armed) {
        return 0;
    }
    r->Armed = 1;
    return 1;
}

ULONG XhciTolRetryCommit(PXHCI_TOL_RETRY r, ULONG now)
{
    r->LastWait = now - r->LatchStamp;
    r->LastCause = r->ArmedCause;
    r->LastStamp = now;
    if (r->ArmedCause == XHCI_RETRY_CAUSE_PORT) {
        xhciRetryStormPush(r, now);
    }
    r->Level = XhciTolRetryLevelUp(r, r->Level);
    r->Tries = xhciRetrySat(r->Tries, XHCI_TOL_RETRY_TRIES_MAX);
    r->TriesTotal = xhciRetrySat(r->TriesTotal, 0xFFFFFFFFUL);
    r->Armed = 0;
    r->DeadlineValid = 0;
    r->PortPending = 0;
    r->Burst = 1;
    r->BurstAttempts = 1;
    r->State = XHCI_RETRY_TRYING;
    return r->Tries;
}

VOID XhciTolRetrySkip(PXHCI_TOL_RETRY r, ULONG reason, ULONG now,
                      ULONG nowSys)
{
    if (reason == XHCI_RETRY_SKIP_ONES) {
        r->SkipOnes = xhciRetrySat(r->SkipOnes, 0xFFFFFFFFUL);
    } else {
        r->SkipLayout = xhciRetrySat(r->SkipLayout, 0xFFFFFFFFUL);
    }
    r->Level = XhciTolRetryLevelUp(r, r->Level);
    r->State = XHCI_RETRY_LATCHED;
    r->LatchStamp = now;
    r->LatchSys = nowSys;
    r->LatchWait = XhciTolRetryWait(r, r->Level);
    r->DeadlineValid = 1;
    r->Armed = 0;
    r->PortPending = 0;
}

ULONG XhciTolRetryRevalOutcome(ULONG guardSet, ULONG deriveOk, ULONG equal)
{
    if (!guardSet) {
        return XHCI_RETRY_REVAL_NOT_NEEDED;
    }
    if (!deriveOk) {
        return XHCI_RETRY_REVAL_FAILED;
    }
    return equal ? XHCI_RETRY_REVAL_EQUAL : XHCI_RETRY_REVAL_DIFFERENT;
}

ULONG XhciTolRetryCommitAdmit(PXHCI_TOL_RETRY r, ULONG admit)
{
    if (!r->Armed) {
        return 0;
    }
    if (admit == XHCI_RETRY_ADMIT_OK) {
        return 1;
    }
    if (admit == XHCI_RETRY_ADMIT_WAIT_LEAVE ||
        admit == XHCI_RETRY_ADMIT_WAIT_POWER ||
        admit == XHCI_RETRY_ADMIT_WAIT_RELEASE) {
        XhciTolRetryUnarm(r, admit == XHCI_RETRY_ADMIT_WAIT_LEAVE ? 1UL : 0UL);
    } else {
        r->Armed = 0;
    }
    return 0;
}

ULONG XhciTolRetryCommitFinish(PXHCI_TOL_RETRY r, PXHCI_TOL_WINDOW window,
                               PXHCI_TOL_RETRY_LATCH latch, ULONG usbsts,
                               ULONG reval, ULONG now, ULONG nowSys)
{
    if (usbsts == 0xFFFFFFFFUL) {
        XhciTolRetrySkip(r, XHCI_RETRY_SKIP_ONES, now, nowSys);
        return XHCI_RETRY_COMMIT_SKIP_ONES;
    }
    if (latch->GuardSet && reval != XHCI_RETRY_REVAL_EQUAL) {
        /* Never an override of the guard (record 38 section 7.3). */
        XhciTolRetrySkip(r, XHCI_RETRY_SKIP_LAYOUT, now, nowSys);
        return XHCI_RETRY_COMMIT_SKIP_LAYOUT;
    }
    XhciTolWindowInit(window);
    latch->Failures = 0;
    latch->TerminalReleased = 0;
    latch->Requested = 1;
    latch->GuardSet = 0;
    (VOID)XhciTolRetryCommit(r, now);
    return XHCI_RETRY_COMMIT_DONE;
}

/* Record 38 section 5.4: the request word decides nothing once the
 * controller is not failed. Terminal 0 means ControllerFailed clear, and a
 * request standing then is one a resume's reinitialization served (it
 * clears ControllerFailed and not the word) or a failed attempt's
 * re-request written after such a resume; on a failed controller, whose
 * terminal is not 0, the pass is unhealthy anyway. So the word is read as
 * the recovery's admission reads it, with ControllerFailed. */
ULONG XhciTolRetryHealthy(const XHCI_TOL_RETRY_HEALTH *h)
{
    return (h->Terminal == XHCI_TOL_TERMINAL_NONE &&
            h->Initialized && !h->ExtSuspended && h->D0 &&
            !h->SuspendedInD0 && h->Started && !h->Leave && !h->ShutLatched)
               ? 1UL
               : 0UL;
}

VOID XhciTolRetryUnarm(PXHCI_TOL_RETRY r, ULONG forLeave)
{
    r->Armed = 0;
    if (forLeave) {
        r->WaitLeave = xhciRetrySat(r->WaitLeave, 0xFFFFFFFFUL);
    } else {
        r->WaitPower = xhciRetrySat(r->WaitPower, 0xFFFFFFFFUL);
    }
}

VOID XhciTolRetryBurstAttempt(PXHCI_TOL_RETRY r)
{
    if (r->Burst) {
        r->BurstAttempts = xhciRetrySat(r->BurstAttempts, 0xFFFFFFFFUL);
    }
}

ULONG XhciTolRetryBurstDefers(const XHCI_TOL_RETRY *r)
{
    return (r->Burst && r->Leave) ? 1UL : 0UL;
}

VOID XhciTolRetryBurstEnd(PXHCI_TOL_RETRY r, ULONG outcome)
{
    if (!r->Burst) {
        return;
    }
    r->Burst = 0;
    r->LastBurstAttempts = r->BurstAttempts;
    r->LastBurstOutcome = outcome;
    switch (outcome) {
    case XHCI_RETRY_BURST_RECOVERED:
        r->TriesRecovered = xhciRetrySat(r->TriesRecovered, 0xFFFFFFFFUL);
        r->State = XHCI_RETRY_WATCHING;
        r->CleanPending = 1;
        r->CleanArmed = 0;
        break;
    case XHCI_RETRY_BURST_LATCHED:
        /* The thread's next observation latches again, at the raised
         * level, from the moment it sees the terminal. */
        r->State = XHCI_RETRY_WATCHING;
        break;
    case XHCI_RETRY_BURST_EXCLUDED:
        r->State = XHCI_RETRY_EXCLUDED;
        break;
    default:
        break;
    }
}

ULONG XhciTolRetryCleanPass(PXHCI_TOL_RETRY r, ULONG healthy, ULONG calls,
                            ULONG now)
{
    ULONG same;

    same = (calls == r->CleanCalls) ? 1UL : 0UL;
    r->CleanCalls = calls;
    if (!r->CleanPending) {
        return 0;
    }
    if (!healthy || !same) {
        r->CleanArmed = 0;
        return 0;
    }
    if (!r->CleanArmed) {
        r->CleanArmed = 1;
        r->CleanStart = now;
        return 0;
    }
    if (!XhciTolElapsed(now, r->CleanStart, r->CleanTicks)) {
        return 0;
    }
    r->CleanPending = 0;
    r->CleanArmed = 0;
    r->Level = 0;
    r->Tries = 0;
    r->BackoffEnds = xhciRetrySat(r->BackoffEnds, 0xFFFFFFFFUL);
    return 1;
}

VOID XhciTolRetryCleanBreak(PXHCI_TOL_RETRY r)
{
    r->CleanArmed = 0;
}

ULONG XhciTolRetryAdmit(const XHCI_TOL_RETRY_IN *in, ULONG forResume)
{
    if (in->Value == 0) {
        return XHCI_RETRY_ADMIT_OFF;
    }
    if (in->Pinned || in->Terminal == XHCI_TOL_TERMINAL_UNREADABLE ||
        in->Terminal == XHCI_TOL_TERMINAL_DMA_UNPROVEN ||
        in->Terminal == XHCI_TOL_TERMINAL_MSG4) {
        return XHCI_RETRY_ADMIT_EXCLUDE;
    }
    if (in->Terminal != XHCI_TOL_TERMINAL_FAILURES &&
        in->Terminal != XHCI_TOL_TERMINAL_WINDOW) {
        return XHCI_RETRY_ADMIT_NOT_LATCHED;
    }
    if (in->Leave || !in->Started || !in->PnpStarted || in->ThreadStop ||
        in->CmdWaitLeave || in->ShutLatched) {
        return XHCI_RETRY_ADMIT_WAIT_LEAVE;
    }
    if (!forResume &&
        (!in->D0 || in->SuspendedInD0 || in->ExtSuspended || in->Msg4Barred)) {
        return XHCI_RETRY_ADMIT_WAIT_POWER;
    }
    if (in->SlotsInvalidated || (in->SlotsUnproven && !in->ReleaseProved) ||
        in->DevicesLeft) {
        return XHCI_RETRY_ADMIT_WAIT_RELEASE;
    }
    return XHCI_RETRY_ADMIT_OK;
}

ULONG XhciTolRetryReleaseBits(ULONG invalidated, ULONG unproven,
                              ULONG proved, ULONG terminalReleased,
                              ULONG devicesLeft, ULONG pinned)
{
    return (invalidated ? XHCI_RETRY_REL_INVALIDATED : 0UL) |
           (unproven ? XHCI_RETRY_REL_UNPROVEN : 0UL) |
           (proved ? XHCI_RETRY_REL_PROVED : 0UL) |
           (terminalReleased ? XHCI_RETRY_REL_TERMINAL : 0UL) |
           (devicesLeft ? XHCI_RETRY_REL_DEVICES : 0UL) |
           (pinned ? XHCI_RETRY_REL_PINNED : 0UL);
}

ULONG XhciTolRetryLeaveNext(ULONG path, ULONG phase, ULONG outcome,
                            ULONG current)
{
    switch (path) {
    case XHCI_RETRY_PNP_QUERY_STOP:
        if (phase == XHCI_RETRY_PHASE_ENTRY) {
            return 1;
        }
        return (outcome == XHCI_RETRY_OUT_OK) ? current : 0UL;
    case XHCI_RETRY_PNP_QUERY_REMOVE:
        /* A refusal below brings the cancel, which clears. */
        return (phase == XHCI_RETRY_PHASE_ENTRY) ? 1UL : current;
    case XHCI_RETRY_PNP_CANCEL_STOP:
    case XHCI_RETRY_PNP_CANCEL_REMOVE:
        return (phase == XHCI_RETRY_PHASE_OUTCOME) ? 0UL : current;
    case XHCI_RETRY_PNP_STOP:
    case XHCI_RETRY_PNP_SURPRISE:
    case XHCI_RETRY_PNP_REMOVE:
    case XHCI_RETRY_PNP_START_LIVE:
        /* Set at the entry and kept: the lifetime ends. */
        return 1;
    case XHCI_RETRY_PNP_NEW_LIFETIME:
        return 0;
    default:
        return current;
    }
}

VOID XhciTolRetryLeaveWrite(PXHCI_TOL_RETRY r, ULONG path, ULONG phase,
                            ULONG outcome)
{
    r->Leave = XhciTolRetryLeaveNext(path, phase, outcome, r->Leave);
    r->LeaveGen++;
}

ULONG XhciTolRetryResumeEnd(ULONG phase, ULONG mark, ULONG recovered,
                            PULONG advance)
{
    *advance = 0;
    if (recovered && mark && phase == XHCI_RETRY_RESUME_FAILED) {
        *advance = 1;
        return XHCI_RETRY_RESUME_RUNNING;
    }
    return phase;
}

ULONG XhciTolRetryPortSample(PXHCI_TOL_RETRY_PORT p, ULONG ccs, ULONG now,
                             ULONG stableTicks)
{
    ccs = ccs ? 1UL : 0UL;
    if (!p->HaveBase) {
        /* The sample that takes a baseline gives no evidence. */
        p->HaveBase = 1;
        p->Base = ccs;
        p->CandArmed = 0;
        return 0;
    }
    if (ccs == p->Base) {
        p->CandArmed = 0;
        return 0;
    }
    if (!p->CandArmed) {
        p->CandArmed = 1;
        p->CandStamp = now;
        return 0;
    }
    if (!XhciTolElapsed(now, p->CandStamp, stableTicks)) {
        return 0;
    }
    p->Base = ccs;
    p->CandArmed = 0;
    return 1;
}

VOID XhciTolRetryPortBreak(PXHCI_TOL_RETRY_PORT p)
{
    p->HaveBase = 0;
    p->Base = 0;
    p->CandArmed = 0;
    p->CandStamp = 0;
    p->HaveLast = 0;
    p->LastCcs = 0;
}

ULONG XhciTolRetrySampleGap(ULONG haveLast, ULONG last, ULONG now)
{
    if (!haveLast) {
        return 0;
    }
    return ((ULONG)(now - last) > XHCI_TOL_RETRY_GAP_TICKS) ? 1UL : 0UL;
}

ULONG XhciTolRetrySweepHold(ULONG admitted, ULONG genStart, ULONG genNow,
                            ULONG portsc)
{
    if (!admitted || genStart != genNow) {
        return XHCI_RETRY_HOLD_DISCARD;
    }
    if (portsc == 0xFFFFFFFFUL) {
        return XHCI_RETRY_HOLD_OFF;
    }
    return XHCI_RETRY_HOLD_READ;
}

ULONG XhciTolRetrySweepEnd(PXHCI_TOL_RETRY_PORT ports, const ULONG *sweep,
                           const UCHAR *managed, ULONG count, ULONG discard,
                           ULONG gap, ULONG now, ULONG stableTicks,
                           PUCHAR feed)
{
    PXHCI_TOL_RETRY_PORT p;
    ULONG episodes;
    ULONG portsc;
    ULONG ccs;
    ULONG i;
    UCHAR f;

    episodes = 0;
    for (i = 0; i < count; i++) {
        feed[i] = 0;
        if (!managed[i]) {
            continue;
        }
        p = &ports[i];
        if (discard || gap) {
            XhciTolRetryPortBreak(p);
            feed[i] = (UCHAR)XHCI_RETRY_FEED_BREAK;
        }
        if (discard) {
            continue;
        }
        portsc = sweep[i];
        if (portsc == XHCI_RETRY_NOT_SAMPLED) {
            continue;
        }
        if ((portsc & XHCI_PORTSC_PP) == 0) {
            /* PP clear is never evidence (record 38 section 6.3). */
            XhciTolRetryPortBreak(p);
            feed[i] = (UCHAR)XHCI_RETRY_FEED_BREAK;
            continue;
        }
        ccs = (portsc & XHCI_PORTSC_CCS) != 0 ? 1UL : 0UL;
        f = (UCHAR)(feed[i] | XHCI_RETRY_FEED_SAMPLE);
        if (ccs) {
            f = (UCHAR)(f | XHCI_RETRY_FEED_CCS);
        }
        /* The location observer's edge: the sampled CCS against the last
         * admitted sample, never CSC (section 6.6). */
        if (p->HaveLast && p->LastCcs != ccs) {
            f = (UCHAR)(f | XHCI_RETRY_FEED_EDGE);
        }
        p->HaveLast = 1;
        p->LastCcs = ccs;
        if (XhciTolRetryPortSample(p, ccs, now, stableTicks)) {
            f = (UCHAR)(f | XHCI_RETRY_FEED_EPISODE);
            episodes++;
        }
        feed[i] = f;
    }
    return episodes;
}

ULONG XhciTolRetrySweepMissed(PXHCI_TOL_RETRY r, ULONG off, ULONG offPort)
{
    r->MissedPasses = xhciRetrySat(r->MissedPasses, 0xFFFFFFFFUL);
    if (!off) {
        return 0;
    }
    r->SampleOff = 1;
    r->SampleOffPort = offPort;
    return XhciTolRetryNoteOnce(r, XHCI_RETRY_NOTE_SAMPLE_OFF);
}

VOID XhciTolRetryStatusWords(const XHCI_TOL_RETRY *r, ULONG now,
                             ULONG nowSys, PULONG value, PULONG tries,
                             PULONG nextS, PULONG never)
{
    *value = r->Value;
    *tries = (r->Tries > XHCI_TOL_RETRY_TRIES_MAX) ? XHCI_TOL_RETRY_TRIES_MAX
                                                   : r->Tries;
    *nextS = XhciTolRetryNextSeconds(r, now, nowSys);
    *never = (r->State == XHCI_RETRY_EXCLUDED) ? 1UL : 0UL;
}

ULONG XhciTolRetryNextSeconds(const XHCI_TOL_RETRY *r, ULONG now,
                              ULONG nowSys)
{
    ULONG gone;
    ULONG left;
    ULONG secs;
    ULONG floor;
    ULONG sysSecs;

    if (r->State == XHCI_RETRY_DUE) {
        return 0;
    }
    if (r->State != XHCI_RETRY_LATCHED || !r->DeadlineValid) {
        return XHCI_RETRY_NEXT_NONE;
    }
    gone = now - r->LatchStamp;
    left = (gone >= r->LatchWait) ? 0UL : r->LatchWait - gone;
    /* Ticks at the nominal 100 ms, rounded up to whole seconds. */
    secs = (left + 9UL) / 10UL;
    /* The floor's units left at 104.8576 ms (131072 / 1250000 s), rounded
     * up: at most 5 724 x 131072, under 2^32. */
    floor = XhciTolRetryFloor(r->LatchWait);
    gone = nowSys - r->LatchSys;
    left = (gone >= floor) ? 0UL : floor - gone;
    sysSecs = (left * 131072UL + 1249999UL) / 1250000UL;
    return (sysSecs > secs) ? sysSecs : secs;
}

ULONG XhciTolRetryNoteOnce(PXHCI_TOL_RETRY r, ULONG bit)
{
    if ((r->Noted & bit) != 0) {
        return 0;
    }
    r->Noted |= bit;
    return 1;
}

ULONG XhciTolRetryBlockShape(ULONG headWords, ULONG needWords, ULONG at,
                             ULONG bytes, ULONG minBytes, ULONG extBytes)
{
    if (headWords < needWords) {
        return XHCI_RETRY_SHAPE_NOT_SERVED;
    }
    if (bytes < minBytes || (at & 3UL) != 0 || at > extBytes ||
        bytes > extBytes - at) {
        return XHCI_RETRY_SHAPE_BAD;
    }
    return XHCI_RETRY_SHAPE_OK;
}
