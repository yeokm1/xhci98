/*
 * xhci_poll.c - polled event delivery, its pure half (roadmap-hcd.md tasks
 * 36.6 to 36.8; design record 15). See xhci_poll.h. Host suite test_poll.
 *
 * IRQL: any; the state functions are called with PollLock held.
 */

#include "xhci_poll.h"

/* ------------------------------------------------------------------ */
/* The values and the delivery (record 15 sections 3 and 4)            */
/* ------------------------------------------------------------------ */

/*
 * The effective mode is 0 for 0 and every value above 3, else the value
 * (3 read as 0 until record 15 revision 7); a period of 0 is its default, the idle period is held to 5000 and
 * the active one to the idle one in effect. A value not found is the
 * default and is recorded as XHCI_POLL_NOT_FOUND.
 */
VOID XhciPollValues(PXHCI_POLL_VALUES v, ULONG modeFound, ULONG mode,
                    ULONG idleFound, ULONG idle, ULONG activeFound,
                    ULONG active)
{
    v->Found = (modeFound ? XHCI_POLL_FOUND_MODE : 0UL) |
               (idleFound ? XHCI_POLL_FOUND_IDLE : 0UL) |
               (activeFound ? XHCI_POLL_FOUND_ACTIVE : 0UL);
    v->ModeRead = modeFound ? mode : XHCI_POLL_NOT_FOUND;
    v->Mode = (modeFound &&
               (mode == XHCI_POLL_MODE_LINE || mode == XHCI_POLL_MODE_POLL ||
                mode == XHCI_POLL_MODE_MESSAGE))
                  ? mode
                  : XHCI_POLL_MODE_AUTO;

    v->IdleRead = idleFound ? idle : XHCI_POLL_NOT_FOUND;
    if (!idleFound || idle == 0) {
        v->IdleMs = XHCI_POLL_IDLE_DEFAULT_MS;
    } else if (idle > XHCI_POLL_IDLE_MAX_MS) {
        v->IdleMs = XHCI_POLL_IDLE_MAX_MS;
    } else {
        v->IdleMs = idle;
    }

    v->ActiveRead = activeFound ? active : XHCI_POLL_NOT_FOUND;
    if (!activeFound || active == 0) {
        v->ActiveMs = XHCI_POLL_ACTIVE_DEFAULT_MS;
    } else {
        v->ActiveMs = active;
    }
    if (v->ActiveMs > v->IdleMs) {
        v->ActiveMs = v->IdleMs;
    }
}

/*
 * Section 4's tables. `mode` is the effective mode; `messageConnected` is
 * whether the message's routines were found (a connection that then fails
 * is the caller's to settle, hcdConnect); `messageSystem` is whether the
 * system can grant a message at all (NT 6). A message resource takes the
 * message where it was connected, and polls where it was not, under mode
 * 0; the pin plays no part under it. Mode 1 is the line only and mode 3 a
 * message only (revision 7): each returns XHCI_POLL_DELIVERY_NONE - the
 * start fails - without its delivery. Returns the delivery, and the
 * reason through `reason`.
 */
ULONG XhciPollDecide(ULONG mode, ULONG haveInterrupt, ULONG message,
                     ULONG messageConnected, ULONG pinStatus, ULONG pin,
                     ULONG messageSystem, PULONG reason)
{
    ULONG unreadable;

    unreadable = (pinStatus != XHCI_POLL_PIN_READ) ? 1UL : 0UL;
    if (mode == XHCI_POLL_MODE_POLL) {
        *reason = XHCI_POLL_R_FORCED_POLL;
        return XHCI_POLL_DELIVERY_POLL;
    }
    if (mode == XHCI_POLL_MODE_MESSAGE) {
        if (!messageSystem) {
            *reason = XHCI_POLL_R_FORCED_MESSAGE_NO_SYSTEM;
        } else if (!haveInterrupt || !message) {
            *reason = XHCI_POLL_R_FORCED_MESSAGE_NOT_GRANTED;
        } else if (!messageConnected) {
            *reason = XHCI_POLL_R_FORCED_MESSAGE_NOT_CONNECTED;
        } else {
            *reason = XHCI_POLL_R_FORCED_MESSAGE;
            return XHCI_POLL_DELIVERY_MESSAGE;
        }
        return XHCI_POLL_DELIVERY_NONE;
    }
    if (haveInterrupt && message) {
        if (mode == XHCI_POLL_MODE_LINE) {
            *reason = XHCI_POLL_R_FORCED_LINE_MESSAGE;
            return XHCI_POLL_DELIVERY_NONE;
        }
        if (!messageConnected) {
            *reason = XHCI_POLL_R_MESSAGE_NOT_CONNECTED;
            return XHCI_POLL_DELIVERY_POLL;
        }
        *reason = XHCI_POLL_R_AUTO_MESSAGE;
        return XHCI_POLL_DELIVERY_MESSAGE;
    }
    if (mode == XHCI_POLL_MODE_LINE) {
        if (!haveInterrupt || (!unreadable && pin == 0)) {
            *reason = XHCI_POLL_R_FORCED_NO_INTERRUPT;
            return XHCI_POLL_DELIVERY_NONE;
        }
        *reason = unreadable ? XHCI_POLL_R_FORCED_LINE_PIN_UNREADABLE
                             : XHCI_POLL_R_FORCED_LINE;
        return XHCI_POLL_DELIVERY_LINE;
    }
    if (!haveInterrupt) {
        *reason = XHCI_POLL_R_AUTO_NO_INTERRUPT;
        return XHCI_POLL_DELIVERY_POLL;
    }
    if (unreadable) {
        *reason = XHCI_POLL_R_AUTO_PIN_UNREADABLE;
        return XHCI_POLL_DELIVERY_LINE;
    }
    if (pin == 0) {
        *reason = XHCI_POLL_R_AUTO_PIN0;
        return XHCI_POLL_DELIVERY_POLL;
    }
    *reason = XHCI_POLL_R_AUTO;
    return XHCI_POLL_DELIVERY_LINE;
}

/*
 * A message decided at start meets its connection (section 4.3):
 * `connected` whether IoConnectInterruptEx succeeded, `lineBased` whether
 * Windows connected the line instead. Under mode 3 anything but the
 * message is a refusal (revision 7); otherwise a failure polls and a line
 * fallback is the line, automatic. Returns the delivery - MESSAGE with
 * `reason` left as the start set it - and the reason through `reason`.
 */
ULONG XhciPollConnected(ULONG mode, ULONG connected, ULONG lineBased,
                        PULONG reason)
{
    if (mode == XHCI_POLL_MODE_MESSAGE && (!connected || lineBased)) {
        *reason = XHCI_POLL_R_FORCED_MESSAGE_NOT_CONNECTED;
        return XHCI_POLL_DELIVERY_NONE;
    }
    if (!connected) {
        *reason = XHCI_POLL_R_MESSAGE_NOT_CONNECTED;
        return XHCI_POLL_DELIVERY_POLL;
    }
    if (lineBased) {
        *reason = XHCI_POLL_R_AUTO;
        return XHCI_POLL_DELIVERY_LINE;
    }
    return XHCI_POLL_DELIVERY_MESSAGE;
}

/* ------------------------------------------------------------------ */
/* Deadlines (section 6.2)                                             */
/* ------------------------------------------------------------------ */

/* Whether deadline a falls before b, by signed difference of the words. */
ULONG XhciPollEarlier(ULONG a, ULONG b)
{
    return ((LONG)(a - b) < 0) ? 1UL : 0UL;
}

/*
 * A deadline turned into an arm: at or past due is served at the active
 * period; more than the idle period away, or a difference beyond any the
 * poller makes (twice the idle bound), is a displaced deadline - a clock
 * step - and is armed at the active period, counted through `displaced`;
 * otherwise the remaining time, held between the two periods. Every arm is
 * therefore at least the active period and at most the idle period away.
 */
ULONG XhciPollArmMs(ULONG due, ULONG now, ULONG activeMs, ULONG idleMs,
                    PULONG displaced)
{
    LONG diff;
    ULONG ms;

    *displaced = 0;
    diff = (LONG)(due - now);
    if (diff <= 0) {
        if (diff < -(LONG)(2UL * XHCI_POLL_IDLE_MAX_MS *
                           XHCI_POLL_TICKS_PER_MS)) {
            *displaced = 1;
        }
        return activeMs;
    }
    ms = (ULONG)diff / XHCI_POLL_TICKS_PER_MS;
    if (ms > idleMs) {
        *displaced = 1;
        return activeMs;
    }
    if (ms < activeMs) {
        ms = activeMs;
    }
    return ms;
}

/* ------------------------------------------------------------------ */
/* One owner at a time (section 5.2)                                   */
/* ------------------------------------------------------------------ */

VOID XhciPollInit(PXHCI_POLL_STATE st)
{
    PUCHAR p;
    ULONG i;

    p = (PUCHAR)st;
    for (i = 0; i < sizeof(*st); i++) {
        p[i] = 0;
    }
    st->State = XHCI_POLL_S_CLOSED;
    st->Role = XHCI_POLL_ROLE_NONE;
}

/* The only way out of CLOSED. Returns 1 when opened. */
ULONG XhciPollOpen(PXHCI_POLL_STATE st, ULONG role)
{
    if (st->State != XHCI_POLL_S_CLOSED ||
        (role != XHCI_POLL_ROLE_WATCH && role != XHCI_POLL_ROLE_SERVE)) {
        return 0;
    }
    st->State = XHCI_POLL_S_PARKED;
    st->Role = role;
    st->DueValid = 0;
    st->OwedValid = 0;
    XhciPollWatchReset(st);
    return 1;
}

/* The DPC's first step: ARMED becomes FIRING and PollOwed is cleared. */
ULONG XhciPollTake(PXHCI_POLL_STATE st)
{
    if (st->State == XHCI_POLL_S_ARMED) {
        st->State = XHCI_POLL_S_FIRING;
        st->OwedValid = 0;
        st->DueValid = 0;
        st->ParkPending = 0;
        st->KickOwed = 0;
        return XHCI_POLL_TAKE_RUN;
    }
    if (st->State == XHCI_POLL_S_CLOSING) {
        return XHCI_POLL_TAKE_CLOSING;
    }
    return XHCI_POLL_TAKE_NONE;
}

/*
 * Not admitted: the pass is marked to park and the watcher forgets. The
 * DPC keeps ownership; its epilogue (XhciPollEnd), after both locks are
 * released, publishes PARKED, CLOSED or an arm for a kick owed meanwhile.
 */
VOID XhciPollPark(PXHCI_POLL_STATE st)
{
    if (st->State == XHCI_POLL_S_FIRING || st->State == XHCI_POLL_S_CLOSING) {
        st->ParkPending = 1;
    }
    XhciPollWatchReset(st);
}

/* The move (section 4.2): the owner turns the watcher into a server. */
VOID XhciPollServe(PXHCI_POLL_STATE st)
{
    if (st->Role == XHCI_POLL_ROLE_WATCH) {
        st->Role = XHCI_POLL_ROLE_SERVE;
        st->OwedValid = 0;
        XhciPollWatchReset(st);
    }
}

/*
 * The DPC's last step. CLOSING becomes CLOSED and nothing is armed; PARKED
 * arms nothing. FIRING becomes ARMED: a watcher at its own period, a server
 * at the earlier of its decision (`decisionMs` from now) and PollOwed,
 * converted. Returns 1 when the caller must KeSetTimer for *armMs.
 */
ULONG XhciPollEnd(PXHCI_POLL_STATE st, ULONG decisionMs, ULONG now,
                  ULONG activeMs, ULONG idleMs, PULONG armMs,
                  PULONG displaced)
{
    ULONG due;

    *armMs = 0;
    *displaced = 0;
    if (st->State == XHCI_POLL_S_CLOSING) {
        st->State = XHCI_POLL_S_CLOSED;
        st->DueValid = 0;
        st->OwedValid = 0;
        st->ParkPending = 0;
        st->KickOwed = 0;
        return 0;
    }
    if (st->State != XHCI_POLL_S_FIRING) {
        return 0;
    }
    if (st->ParkPending) {
        st->ParkPending = 0;
        st->OwedValid = 0;
        if (!st->KickOwed) {
            st->State = XHCI_POLL_S_PARKED;
            st->DueValid = 0;
            return 0;
        }
        st->KickOwed = 0;
        *armMs = (st->Role == XHCI_POLL_ROLE_WATCH) ? XHCI_POLL_WATCH_MS
                                                    : activeMs;
    } else if (st->Role == XHCI_POLL_ROLE_WATCH) {
        *armMs = XHCI_POLL_WATCH_MS;
    } else {
        due = now + decisionMs * XHCI_POLL_TICKS_PER_MS;
        if (st->OwedValid && XhciPollEarlier(st->Owed, due)) {
            due = st->Owed;
        }
        *armMs = XhciPollArmMs(due, now, activeMs, idleMs, displaced);
    }
    st->OwedValid = 0;
    st->Due = now + *armMs * XHCI_POLL_TICKS_PER_MS;
    st->DueValid = 1;
    st->State = XHCI_POLL_S_ARMED;
    return 1;
}

/* PollService granted: PARKED becomes ARMED at the role's first period.
 * Returns 1 when the caller must KeSetTimer for *armMs. */
ULONG XhciPollKick(PXHCI_POLL_STATE st, ULONG now, ULONG activeMs,
                   PULONG armMs)
{
    *armMs = 0;
    if (st->State == XHCI_POLL_S_FIRING && st->ParkPending) {
        st->KickOwed = 1;
        return 0;
    }
    if (st->State != XHCI_POLL_S_PARKED) {
        return 0;
    }
    *armMs = (st->Role == XHCI_POLL_ROLE_WATCH) ? XHCI_POLL_WATCH_MS
                                                : activeMs;
    st->Due = now + *armMs * XHCI_POLL_TICKS_PER_MS;
    st->DueValid = 1;
    st->OwedValid = 0;
    st->State = XHCI_POLL_S_ARMED;
    return 1;
}

/*
 * A promotion's first step, servers only: in ARMED a deadline earlier than
 * the armed one asks the caller to take the arm back; in FIRING it is owed
 * to the DPC's re-arm; otherwise nothing.
 */
ULONG XhciPollPromote(PXHCI_POLL_STATE st, ULONG deadline)
{
    if (st->Role != XHCI_POLL_ROLE_SERVE) {
        return XHCI_POLL_P_NONE;
    }
    if (st->State == XHCI_POLL_S_ARMED) {
        if (st->DueValid && !XhciPollEarlier(deadline, st->Due)) {
            return XHCI_POLL_P_NONE;
        }
        return XHCI_POLL_P_CANCEL;
    }
    if (st->State == XHCI_POLL_S_FIRING) {
        if (!st->OwedValid || XhciPollEarlier(deadline, st->Owed)) {
            st->Owed = deadline;
            st->OwedValid = 1;
        }
        return XHCI_POLL_P_OWED;
    }
    return XHCI_POLL_P_NONE;
}

/*
 * After KeCancelTimer: TRUE took the arm back, and the promotion sets its
 * deadline (returns 1: KeSetTimer for *armMs). FALSE means the DPC is
 * queued or running and about to take ownership; its pass serves this
 * submission, so nothing is armed here.
 */
ULONG XhciPollPromoteDone(PXHCI_POLL_STATE st, ULONG deadline,
                          ULONG cancelled, ULONG now, ULONG activeMs,
                          ULONG idleMs, PULONG armMs, PULONG displaced)
{
    *armMs = 0;
    *displaced = 0;
    if (!cancelled || st->State != XHCI_POLL_S_ARMED) {
        return 0;
    }
    *armMs = XhciPollArmMs(deadline, now, activeMs, idleMs, displaced);
    st->Due = now + *armMs * XHCI_POLL_TICKS_PER_MS;
    st->DueValid = 1;
    return 1;
}

/* Close (release, PASSIVE_LEVEL, under PollLock). */
ULONG XhciPollCloseBegin(PXHCI_POLL_STATE st)
{
    switch (st->State) {
    case XHCI_POLL_S_PARKED:
        st->State = XHCI_POLL_S_CLOSED;
        st->DueValid = 0;
        st->OwedValid = 0;
        return XHCI_POLL_C_DONE;
    case XHCI_POLL_S_ARMED:
        return XHCI_POLL_C_CANCEL;
    case XHCI_POLL_S_FIRING:
        st->State = XHCI_POLL_S_CLOSING;
        return XHCI_POLL_C_WAIT;
    case XHCI_POLL_S_CLOSING:
        return XHCI_POLL_C_WAIT;
    default:
        return XHCI_POLL_C_DONE;
    }
}

ULONG XhciPollCloseDone(PXHCI_POLL_STATE st, ULONG cancelled)
{
    if (st->State != XHCI_POLL_S_ARMED) {
        return (st->State == XHCI_POLL_S_CLOSED) ? XHCI_POLL_C_DONE
                                                 : XHCI_POLL_C_WAIT;
    }
    if (cancelled) {
        st->State = XHCI_POLL_S_CLOSED;
        st->DueValid = 0;
        st->OwedValid = 0;
        return XHCI_POLL_C_DONE;
    }
    st->State = XHCI_POLL_S_CLOSING;
    return XHCI_POLL_C_WAIT;
}

/* A peek may queue IsrDpc only while the poller is open (section 5.2). */
ULONG XhciPollPeekAdmit(const XHCI_POLL_STATE *st)
{
    return (st->State != XHCI_POLL_S_CLOSING &&
            st->State != XHCI_POLL_S_CLOSED)
               ? 1UL
               : 0UL;
}

/* ------------------------------------------------------------------ */
/* The watcher (section 4.2)                                           */
/* ------------------------------------------------------------------ */

VOID XhciPollWatchReset(PXHCI_POLL_STATE st)
{
    st->WatchPrev = 0;
    st->WatchClaims = 0;
    st->WatchPairs = 0;
    st->WatchFirst = 0;
}

/*
 * One look: `asserted` is IP, IE and INTE all read 1; `claims` is
 * InterruptsClaimed at the look; `gen` the service generation it was made
 * under. A stale pair is an asserted look after an asserted look of the
 * same generation with the claims unchanged. Returns 1 when the third such
 * pair in a row spans XHCI_POLL_WATCH_DEAD_MS from the first of its looks.
 */
ULONG XhciPollWatchLook(PXHCI_POLL_STATE st, ULONG gen, ULONG asserted,
                        ULONG claims, ULONG now)
{
    if (st->WatchGen != gen) {
        XhciPollWatchReset(st);
        st->WatchGen = gen;
    }
    if (!asserted) {
        XhciPollWatchReset(st);
        return 0;
    }
    if (!st->WatchPrev || st->WatchClaims != claims) {
        st->WatchPrev = 1;
        st->WatchClaims = claims;
        st->WatchPairs = 0;
        st->WatchFirst = now;
        return 0;
    }
    st->WatchPairs++;
    if (st->WatchPairs >= XHCI_POLL_WATCH_PAIRS &&
        (LONG)(now - st->WatchFirst) >=
            (LONG)(XHCI_POLL_WATCH_DEAD_MS * XHCI_POLL_TICKS_PER_MS)) {
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* The adaptive period (section 6.1) and the observation (section 7)   */
/* ------------------------------------------------------------------ */

/* An Endpoint Context Interval, 2^Interval x 125 us, in whole ms rounded
 * down; 0 below 1 ms. Interval is 0 to 15 (Table 6-12). */
ULONG XhciPollIntervalMs(ULONG epInterval)
{
    if (epInterval > 15) {
        epInterval = 15;
    }
    return (1UL << epInterval) / 8UL;
}

ULONG XhciPollDecidePeriod(const XHCI_POLL_BUS *bus, ULONG activeMs,
                           ULONG idleMs, PULONG busState)
{
    ULONG ms;

    if (bus->Command || bus->Async || bus->PortsBusy ||
        bus->DrainedRecently || bus->Bounded) {
        *busState = XHCI_POLL_BUS_ACTIVE;
        return activeMs;
    }
    if (bus->PeriodicMs != 0) {
        ms = bus->PeriodicMs;
        if (ms < activeMs) {
            ms = activeMs;
        }
        if (ms > idleMs) {
            ms = idleMs;
        }
        *busState = XHCI_POLL_BUS_PERIODIC;
        return ms;
    }
    *busState = XHCI_POLL_BUS_IDLE;
    return idleMs;
}

/*
 * "An event drained within the last idle period" (section 6.1): `last` is
 * when one was seen. An age outside [0, idle) - elapsed, or a clock that
 * stepped back, or one so old the low word's signed difference wrapped -
 * expires the stamp for good, so it can never read recent again.
 */
ULONG XhciPollRecent(PULONG seen, ULONG last, ULONG now, ULONG idleMs)
{
    LONG age;

    if (!*seen) {
        return 0;
    }
    age = (LONG)(now - last);
    if (age < 0 || (ULONG)age >= idleMs * XHCI_POLL_TICKS_PER_MS) {
        *seen = 0;
        return 0;
    }
    return 1;
}

/* The interval since the last poll run, quantised to the system clock: a
 * negative one, or one beyond twice the idle bound, is discarded. */
VOID XhciPollObserve(PXHCI_POLL_OBS obs, ULONG now)
{
    LONG diff;
    ULONG ms;

    if (obs->LastValid) {
        diff = (LONG)(now - obs->Last);
        if (diff < 0 ||
            (ULONG)diff > 2UL * XHCI_POLL_IDLE_MAX_MS *
                              XHCI_POLL_TICKS_PER_MS) {
            obs->Discarded++;
        } else {
            ms = (ULONG)diff / XHCI_POLL_TICKS_PER_MS;
            if (obs->MinMs == XHCI_POLL_NOT_FOUND || ms < obs->MinMs) {
                obs->MinMs = ms;
            }
            if (ms > obs->MaxMs) {
                obs->MaxMs = ms;
            }
        }
    }
    obs->Last = now;
    obs->LastValid = 1;
}

/* ------------------------------------------------------------------ */
/* The line's retirement (section 4.2, step 3)                         */
/* ------------------------------------------------------------------ */

/*
 * Both enables down, proven by reading back both, then IP acknowledged and
 * proven by reading it back 0; EINT untouched. Bounded attempts, no wait:
 * the caller holds the interrupt object's lock at DIRQL. A reset under way
 * writes nothing (it is the reset's to clear; a completed HCRST retires
 * the line by itself). IRQL: any.
 */
ULONG XhciPollRetire(const XHCI_POLL_REGS *io)
{
    ULONG usbcmd;
    ULONG usbsts;
    ULONG iman;
    ULONG attempt;
    ULONG down;

    down = 0;
    for (attempt = 0; attempt < XHCI_POLL_RETIRE_ATTEMPTS && !down;
         attempt++) {
        usbcmd = io->Read(io->Context, XHCI_POLL_REG_USBCMD);
        usbsts = io->Read(io->Context, XHCI_POLL_REG_USBSTS);
        if ((usbcmd != 0xFFFFFFFFUL &&
             (usbcmd & XHCI_POLL_USBCMD_HCRST) != 0) ||
            (usbsts != 0xFFFFFFFFUL && (usbsts & XHCI_POLL_USBSTS_CNR) != 0)) {
            return XHCI_POLL_RETIRE_RESETTING;
        }
        iman = io->Read(io->Context, XHCI_POLL_REG_IMAN);
        if (usbcmd != 0xFFFFFFFFUL &&
            (usbcmd & XHCI_POLL_USBCMD_INTE) != 0) {
            io->ClearInte(io->Context, usbcmd);
        }
        if (iman != 0xFFFFFFFFUL && (iman & XHCI_POLL_IMAN_IE) != 0) {
            io->WriteIman(io->Context,
                          iman & ~(XHCI_POLL_IMAN_IE | XHCI_POLL_IMAN_IP));
        }
        usbcmd = io->Read(io->Context, XHCI_POLL_REG_USBCMD);
        iman = io->Read(io->Context, XHCI_POLL_REG_IMAN);
        down = (usbcmd != 0xFFFFFFFFUL && iman != 0xFFFFFFFFUL &&
                (usbcmd & XHCI_POLL_USBCMD_INTE) == 0 &&
                (iman & XHCI_POLL_IMAN_IE) == 0)
                   ? 1UL
                   : 0UL;
    }
    if (!down) {
        return XHCI_POLL_RETIRE_ENABLES;
    }
    for (attempt = 0; attempt < XHCI_POLL_RETIRE_ATTEMPTS; attempt++) {
        iman = io->Read(io->Context, XHCI_POLL_REG_IMAN);
        if (iman == 0xFFFFFFFFUL) {
            continue;
        }
        if ((iman & XHCI_POLL_IMAN_IP) == 0) {
            return XHCI_POLL_RETIRE_PROVEN;
        }
        io->WriteIman(io->Context,
                      (iman & ~XHCI_POLL_IMAN_IE) | XHCI_POLL_IMAN_IP);
    }
    iman = io->Read(io->Context, XHCI_POLL_REG_IMAN);
    if (iman != 0xFFFFFFFFUL && (iman & (XHCI_POLL_IMAN_IP |
                                         XHCI_POLL_IMAN_IE)) == 0) {
        return XHCI_POLL_RETIRE_PROVEN;
    }
    return XHCI_POLL_RETIRE_IP;
}
