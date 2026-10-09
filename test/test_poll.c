/*
 * test_poll.c - host vectors for polled event delivery's pure half
 * (src\xhci_poll.c; roadmap-hcd.md tasks 36.6 to 36.8; design record 15):
 * the three values, the delivery and its reason, the deadline arithmetic
 * across the word's wrap and clock steps, the poller's ownership machine
 * with KeCancelTimer's two answers at each state, the roles, park and
 * kick in both orders, close against expiry, the watcher's dead-line
 * verdict, the adaptive period and the interval observation, and the
 * line's retirement over a modelled register file.
 */

#include <stdio.h>
#include "../src/xhci_poll.h"
#include "test_harness.h"

#define MS(x) ((ULONG)(x) * XHCI_POLL_TICKS_PER_MS)

/* ------------------------------------------------------------------ */

static void test_values(void)
{
    XHCI_POLL_VALUES v;

    XhciPollValues(&v, 0, 0, 0, 0, 0, 0);
    CHECK_EQ(v.Found, 0, "nothing found");
    CHECK_EQ(v.ModeRead, XHCI_POLL_NOT_FOUND, "absent mode recorded");
    CHECK_EQ(v.Mode, 0, "absent mode is automatic");
    CHECK_EQ(v.IdleMs, 50, "absent idle is 50");
    CHECK_EQ(v.ActiveMs, 1, "absent active is 1");
    CHECK_EQ(v.IdleRead, XHCI_POLL_NOT_FOUND, "absent idle recorded");

    XhciPollValues(&v, 1, 1, 1, 0, 1, 0);
    CHECK_EQ(v.Mode, 1, "mode 1");
    CHECK_EQ(v.IdleMs, 50, "idle 0 runs as 50");
    CHECK_EQ(v.ActiveMs, 1, "active 0 runs as 1");
    CHECK_EQ(v.IdleRead, 0, "idle 0 recorded as read");

    XhciPollValues(&v, 1, 2, 1, 9000, 1, 7000);
    CHECK_EQ(v.Mode, 2, "mode 2");
    CHECK_EQ(v.IdleMs, 5000, "idle held to 5000");
    CHECK_EQ(v.ActiveMs, 5000, "active held to the idle in effect");
    CHECK_EQ(v.ActiveRead, 7000, "active as read");

    XhciPollValues(&v, 1, 3, 1, 20, 1, 30);
    CHECK_EQ(v.Mode, 0, "3 runs as 0");
    CHECK_EQ(v.ModeRead, 3, "3 recorded");
    CHECK_EQ(v.ActiveMs, 20, "active above idle held to it");

    XhciPollValues(&v, 1, 4, 1, 5000, 1, 1);
    CHECK_EQ(v.Mode, 0, "4 runs as 0");
    CHECK_EQ(v.ModeRead, 4, "4 recorded");
    CHECK_EQ(v.IdleMs, 5000, "5000 kept");

    XhciPollValues(&v, 1, 0xFFFFFFFFUL, 0, 0, 1, 1);
    CHECK_EQ(v.Found, XHCI_POLL_FOUND_MODE | XHCI_POLL_FOUND_ACTIVE,
             "a set 0xFFFFFFFF is found, apart from its value");
    CHECK_EQ(v.ModeRead, 0xFFFFFFFFUL, "as read");
    XhciPollValues(&v, 1, 0xFFFFFFFEUL, 1, 1, 1, 1);
    CHECK_EQ(v.Mode, 0, "a huge value runs as 0");
    CHECK_EQ(v.IdleMs, 1, "idle 1 kept");
    CHECK_EQ(v.ActiveMs, 1, "active 1 kept");
}

static void decide(ULONG mode, ULONG have, ULONG msg, ULONG conn,
                   ULONG pinStatus, ULONG pin, ULONG wantD, ULONG wantR,
                   const char *what)
{
    ULONG reason;
    ULONG d;

    reason = 0xEEEEEEEEUL;
    d = XhciPollDecide(mode, have, msg, conn, pinStatus, pin, &reason);
    check_eq_impl(d, wantD, what, __FILE__, __LINE__);
    check_eq_impl(reason, wantR, what, __FILE__, __LINE__);
}

static void test_decide(void)
{
    /* Section 4's line table. */
    decide(0, 1, 0, 0, XHCI_POLL_PIN_READ, 1, XHCI_POLL_DELIVERY_LINE,
           XHCI_POLL_R_AUTO, "0, resource, pin 1");
    decide(0, 1, 0, 0, XHCI_POLL_PIN_UNREADABLE, 0, XHCI_POLL_DELIVERY_LINE,
           XHCI_POLL_R_AUTO_PIN_UNREADABLE, "0, resource, pin unreadable");
    decide(0, 1, 0, 0, XHCI_POLL_PIN_NOT_READ, 0, XHCI_POLL_DELIVERY_LINE,
           XHCI_POLL_R_AUTO_PIN_UNREADABLE, "0, resource, pin not read");
    decide(0, 1, 0, 0, XHCI_POLL_PIN_READ, 0, XHCI_POLL_DELIVERY_POLL,
           XHCI_POLL_R_AUTO_PIN0, "0, resource, pin 0");
    decide(0, 0, 0, 0, XHCI_POLL_PIN_READ, 1, XHCI_POLL_DELIVERY_POLL,
           XHCI_POLL_R_AUTO_NO_INTERRUPT, "0, no resource, pin 1");
    decide(0, 0, 0, 0, XHCI_POLL_PIN_READ, 0, XHCI_POLL_DELIVERY_POLL,
           XHCI_POLL_R_AUTO_NO_INTERRUPT, "0, no resource, pin 0");
    decide(1, 1, 0, 0, XHCI_POLL_PIN_READ, 2, XHCI_POLL_DELIVERY_LINE,
           XHCI_POLL_R_FORCED_LINE, "1, resource, pin 2");
    decide(1, 1, 0, 0, XHCI_POLL_PIN_UNREADABLE, 0, XHCI_POLL_DELIVERY_LINE,
           XHCI_POLL_R_FORCED_LINE_PIN_UNREADABLE, "1, pin unreadable");
    decide(1, 1, 0, 0, XHCI_POLL_PIN_READ, 0, XHCI_POLL_DELIVERY_NONE,
           XHCI_POLL_R_FORCED_NO_INTERRUPT, "1, resource, pin 0");
    decide(1, 0, 0, 0, XHCI_POLL_PIN_READ, 1, XHCI_POLL_DELIVERY_NONE,
           XHCI_POLL_R_FORCED_NO_INTERRUPT, "1, no resource");
    decide(2, 1, 0, 0, XHCI_POLL_PIN_READ, 1, XHCI_POLL_DELIVERY_POLL,
           XHCI_POLL_R_FORCED_POLL, "2, resource");
    decide(2, 0, 0, 0, XHCI_POLL_PIN_READ, 0, XHCI_POLL_DELIVERY_POLL,
           XHCI_POLL_R_FORCED_POLL, "2, nothing");
    /* The message table. */
    decide(0, 1, 1, 1, XHCI_POLL_PIN_READ, 0, XHCI_POLL_DELIVERY_MESSAGE,
           XHCI_POLL_R_AUTO_MESSAGE, "0, message connected, pin 0");
    decide(1, 1, 1, 1, XHCI_POLL_PIN_READ, 1, XHCI_POLL_DELIVERY_MESSAGE,
           XHCI_POLL_R_MESSAGE_LINE_ASKED, "1, message connected");
    decide(0, 1, 1, 0, XHCI_POLL_PIN_READ, 1, XHCI_POLL_DELIVERY_POLL,
           XHCI_POLL_R_MESSAGE_NOT_CONNECTED, "0, message not connected");
    decide(1, 1, 1, 0, XHCI_POLL_PIN_READ, 1, XHCI_POLL_DELIVERY_POLL,
           XHCI_POLL_R_MESSAGE_NOT_CONNECTED, "1, message not connected");
    decide(2, 1, 1, 1, XHCI_POLL_PIN_READ, 1, XHCI_POLL_DELIVERY_POLL,
           XHCI_POLL_R_FORCED_POLL, "2, message");
}

/* ------------------------------------------------------------------ */

static void test_deadlines(void)
{
    ULONG d;
    ULONG ms;

    CHECK(XhciPollEarlier(1, 2), "1 before 2");
    CHECK(!XhciPollEarlier(2, 1), "2 not before 1");
    CHECK(!XhciPollEarlier(5, 5), "equal is not earlier");
    CHECK(XhciPollEarlier(0xFFFFFFF0UL, 0x10), "across the wrap");
    CHECK(!XhciPollEarlier(0x10, 0xFFFFFFF0UL), "across the wrap, reversed");

    ms = XhciPollArmMs(1000 + MS(20), 1000, 1, 50, &d);
    CHECK_EQ(ms, 20, "remaining 20 ms");
    CHECK_EQ(d, 0, "not displaced");
    ms = XhciPollArmMs(1000, 1000 + MS(3), 1, 50, &d);
    CHECK_EQ(ms, 1, "expired: the active period");
    CHECK_EQ(d, 0, "expired is not displaced");
    ms = XhciPollArmMs(1000 + MS(51), 1000, 1, 50, &d);
    CHECK_EQ(ms, 1, "beyond idle: active");
    CHECK_EQ(d, 1, "beyond idle: displaced");
    ms = XhciPollArmMs(1000, 1000 + MS(10001), 1, 50, &d);
    CHECK_EQ(ms, 1, "clock stepped forward far: active");
    CHECK_EQ(d, 1, "clock stepped forward far: displaced");
    ms = XhciPollArmMs(0x10, 0xFFFFFFF0UL - MS(5), 1, 50, &d);
    CHECK_EQ(ms, 5, "across the wrap");
    CHECK_EQ(d, 0, "across the wrap, not displaced");
    ms = XhciPollArmMs(1000 + 5000, 1000, 8, 50, &d);
    CHECK_EQ(ms, 8, "below active: held to active");
    ms = XhciPollArmMs(1000 + MS(50), 1000, 1, 50, &d);
    CHECK_EQ(ms, 50, "exactly idle kept");
    CHECK_EQ(d, 0, "exactly idle not displaced");
}

/* ------------------------------------------------------------------ */

static void test_open_close(void)
{
    XHCI_POLL_STATE st;
    ULONG arm;
    ULONG d;

    XhciPollInit(&st);
    CHECK_EQ(st.State, XHCI_POLL_S_CLOSED, "starts closed");
    CHECK_EQ(XhciPollKick(&st, 0, 1, &arm), 0, "no kick while closed");
    CHECK_EQ(XhciPollCloseBegin(&st), XHCI_POLL_C_DONE, "close of closed");
    CHECK_EQ(XhciPollOpen(&st, XHCI_POLL_ROLE_NONE), 0, "no role, no open");
    CHECK_EQ(XhciPollOpen(&st, XHCI_POLL_ROLE_SERVE), 1, "opened");
    CHECK_EQ(st.State, XHCI_POLL_S_PARKED, "parked");
    CHECK_EQ(XhciPollOpen(&st, XHCI_POLL_ROLE_SERVE), 0, "open once");
    CHECK(XhciPollPeekAdmit(&st), "peek admitted when parked");

    /* Close a parked poller. */
    CHECK_EQ(XhciPollCloseBegin(&st), XHCI_POLL_C_DONE, "parked closes");
    CHECK_EQ(st.State, XHCI_POLL_S_CLOSED, "closed");
    CHECK(!XhciPollPeekAdmit(&st), "no peek when closed");

    /* Stop and start: reopened from CLOSED, armed by the kick. */
    CHECK_EQ(XhciPollOpen(&st, XHCI_POLL_ROLE_SERVE), 1, "reopened");
    CHECK_EQ(XhciPollKick(&st, 100, 1, &arm), 1, "kick arms");
    CHECK_EQ(arm, 1, "kick at the active period");
    CHECK_EQ(st.State, XHCI_POLL_S_ARMED, "armed");
    CHECK_EQ(XhciPollKick(&st, 100, 1, &arm), 0, "kick when armed: no");

    /* Close an armed one: KeCancelTimer TRUE. */
    CHECK_EQ(XhciPollCloseBegin(&st), XHCI_POLL_C_CANCEL, "armed: cancel");
    CHECK_EQ(XhciPollCloseDone(&st, 1), XHCI_POLL_C_DONE, "cancelled: done");
    CHECK_EQ(st.State, XHCI_POLL_S_CLOSED, "closed after the cancel");
    CHECK_EQ(XhciPollTake(&st), XHCI_POLL_TAKE_NONE, "a stray DPC: none");

    /* Close an armed one whose timer expired: FALSE, the DPC ends it. */
    (VOID)XhciPollOpen(&st, XHCI_POLL_ROLE_SERVE);
    (VOID)XhciPollKick(&st, 0, 1, &arm);
    CHECK_EQ(XhciPollCloseBegin(&st), XHCI_POLL_C_CANCEL, "cancel asked");
    CHECK_EQ(XhciPollCloseDone(&st, 0), XHCI_POLL_C_WAIT, "expired: wait");
    CHECK_EQ(st.State, XHCI_POLL_S_CLOSING, "closing");
    CHECK(!XhciPollPeekAdmit(&st), "no peek when closing");
    CHECK_EQ(XhciPollTake(&st), XHCI_POLL_TAKE_CLOSING, "DPC sees closing");
    CHECK_EQ(XhciPollEnd(&st, 5, 0, 1, 50, &arm, &d), 0, "nothing armed");
    CHECK_EQ(st.State, XHCI_POLL_S_CLOSED, "the DPC closed it");

    /* Close while the DPC runs. */
    (VOID)XhciPollOpen(&st, XHCI_POLL_ROLE_SERVE);
    (VOID)XhciPollKick(&st, 0, 1, &arm);
    CHECK_EQ(XhciPollTake(&st), XHCI_POLL_TAKE_RUN, "DPC runs");
    CHECK_EQ(XhciPollCloseBegin(&st), XHCI_POLL_C_WAIT, "firing: wait");
    CHECK_EQ(XhciPollCloseBegin(&st), XHCI_POLL_C_WAIT, "asked twice");
    CHECK_EQ(XhciPollEnd(&st, 5, 0, 1, 50, &arm, &d), 0, "ends closed");
    CHECK_EQ(st.State, XHCI_POLL_S_CLOSED, "closed by the DPC");

    /* A start that failed: open, never kicked, closed by the release. */
    (VOID)XhciPollOpen(&st, XHCI_POLL_ROLE_WATCH);
    CHECK_EQ(XhciPollCloseBegin(&st), XHCI_POLL_C_DONE, "failed start");
    CHECK_EQ(XhciPollOpen(&st, XHCI_POLL_ROLE_SERVE), 1, "retried start");
    CHECK_EQ(st.Role, XHCI_POLL_ROLE_SERVE, "the retry's role");
}

static void test_dpc_and_promotion(void)
{
    XHCI_POLL_STATE st;
    ULONG arm;
    ULONG d;
    ULONG now;

    XhciPollInit(&st);
    (VOID)XhciPollOpen(&st, XHCI_POLL_ROLE_SERVE);
    now = 0x1000;
    (VOID)XhciPollKick(&st, now, 1, &arm);

    /* An expiry, the pass, a decision of 50 ms. */
    now += MS(1);
    CHECK_EQ(XhciPollTake(&st), XHCI_POLL_TAKE_RUN, "run");
    CHECK_EQ(XhciPollEnd(&st, 50, now, 1, 50, &arm, &d), 1, "re-armed");
    CHECK_EQ(arm, 50, "decision kept");
    CHECK_EQ(st.State, XHCI_POLL_S_ARMED, "armed");

    /* A promotion to 1 ms in ARMED: cancel; TRUE sets the new deadline. */
    CHECK_EQ(XhciPollPromote(&st, now + MS(1)), XHCI_POLL_P_CANCEL,
             "earlier: take the arm back");
    CHECK_EQ(XhciPollPromoteDone(&st, now + MS(1), 1, now, 1, 50, &arm, &d),
             1, "cancelled: re-armed");
    CHECK_EQ(arm, 1, "at the need");
    /* A later need never pushes it later. */
    CHECK_EQ(XhciPollPromote(&st, now + MS(8)), XHCI_POLL_P_NONE,
             "not earlier: nothing");
    CHECK_EQ(XhciPollPromote(&st, now + MS(1)), XHCI_POLL_P_NONE,
             "equal: nothing");

    /* A promotion just before expiry: KeCancelTimer FALSE - the DPC is
     * about to run; nothing armed by the promotion. */
    CHECK_EQ(XhciPollPromote(&st, now), XHCI_POLL_P_CANCEL, "cancel asked");
    CHECK_EQ(XhciPollPromoteDone(&st, now, 0, now, 1, 50, &arm, &d), 0,
             "expired: the DPC serves it");
    CHECK_EQ(st.State, XHCI_POLL_S_ARMED, "still the timer's");
    CHECK_EQ(XhciPollTake(&st), XHCI_POLL_TAKE_RUN, "DPC takes it");

    /* During a long drain: owed, and the re-arm honours it. */
    CHECK_EQ(XhciPollPromote(&st, now + MS(3)), XHCI_POLL_P_OWED, "owed");
    CHECK_EQ(XhciPollPromote(&st, now + MS(7)), XHCI_POLL_P_OWED,
             "a later one owed too");
    CHECK_EQ(st.Owed, now + MS(3), "the earliest owed kept");
    CHECK_EQ(XhciPollEnd(&st, 50, now, 1, 50, &arm, &d), 1, "re-armed");
    CHECK_EQ(arm, 3, "the owed deadline wins over the decision");
    CHECK(!st.OwedValid, "owed consumed");

    /* An owed deadline already expired: served at once (active). */
    CHECK_EQ(XhciPollTake(&st), XHCI_POLL_TAKE_RUN, "run");
    (VOID)XhciPollPromote(&st, now + MS(2));
    now += MS(5);
    CHECK_EQ(XhciPollEnd(&st, 50, now, 1, 50, &arm, &d), 1, "re-armed");
    CHECK_EQ(arm, 1, "expired owed: the active period");

    /* Take clears an owed deadline left from before. */
    CHECK_EQ(XhciPollTake(&st), XHCI_POLL_TAKE_RUN, "run");
    (VOID)XhciPollPromote(&st, now + MS(2));
    XhciPollPark(&st);
    CHECK_EQ(st.State, XHCI_POLL_S_FIRING, "still owned until the epilogue");
    CHECK_EQ(XhciPollEnd(&st, 50, now, 1, 50, &arm, &d), 0,
             "the epilogue parks, arms nothing");
    CHECK_EQ(st.State, XHCI_POLL_S_PARKED, "parked");
    CHECK_EQ(XhciPollPromote(&st, now + MS(1)), XHCI_POLL_P_NONE,
             "no promotion while parked");
    CHECK_EQ(XhciPollKick(&st, now, 1, &arm), 1, "kicked");
    CHECK(!st.OwedValid, "the kick drops a stale owed");
}

static void test_park_kick_orders(void)
{
    XHCI_POLL_STATE st;
    ULONG arm;
    ULONG d;

    /* Park first, then the kick: armed. */
    XhciPollInit(&st);
    (VOID)XhciPollOpen(&st, XHCI_POLL_ROLE_SERVE);
    (VOID)XhciPollKick(&st, 0, 1, &arm);
    (VOID)XhciPollTake(&st);
    XhciPollPark(&st);
    (VOID)XhciPollEnd(&st, 4, 0, 1, 50, &arm, &d);
    CHECK_EQ(XhciPollKick(&st, 0, 1, &arm), 1, "park then kick: armed");

    /* A grant between the parking decision and the epilogue: owed, armed
     * by the epilogue, never twice. */
    (VOID)XhciPollTake(&st);
    XhciPollPark(&st);
    CHECK_EQ(XhciPollKick(&st, 0, 1, &arm), 0, "kick while parking: owed");
    CHECK(st.KickOwed, "owed recorded");
    CHECK_EQ(XhciPollEnd(&st, 4, 0, 1, 50, &arm, &d), 1, "epilogue arms");
    CHECK_EQ(arm, 1, "at the role's first period");
    CHECK_EQ(st.State, XHCI_POLL_S_ARMED, "armed once");

    /* A close between the parking decision and the epilogue. */
    (VOID)XhciPollTake(&st);
    XhciPollPark(&st);
    CHECK_EQ(XhciPollCloseBegin(&st), XHCI_POLL_C_WAIT, "close waits");
    CHECK_EQ(XhciPollKick(&st, 0, 1, &arm), 0, "no kick while closing");
    CHECK_EQ(XhciPollEnd(&st, 4, 0, 1, 50, &arm, &d), 0, "nothing armed");
    CHECK_EQ(st.State, XHCI_POLL_S_CLOSED, "closed by the epilogue");
    (VOID)XhciPollOpen(&st, XHCI_POLL_ROLE_SERVE);
    (VOID)XhciPollKick(&st, 0, 1, &arm);

    /* Kick first (service already granted): the kick does nothing, the
     * DPC sees the service and re-arms itself. */
    (VOID)XhciPollTake(&st);
    CHECK_EQ(XhciPollKick(&st, 0, 1, &arm), 0, "kick while firing: nothing");
    CHECK_EQ(XhciPollEnd(&st, 4, 0, 1, 50, &arm, &d), 1, "the DPC re-arms");
    CHECK_EQ(arm, 4, "at its decision");

    /* Park while closing keeps closing. */
    (VOID)XhciPollTake(&st);
    (VOID)XhciPollCloseBegin(&st);
    XhciPollPark(&st);
    CHECK_EQ(st.State, XHCI_POLL_S_CLOSING, "closing kept");
    CHECK_EQ(XhciPollKick(&st, 0, 1, &arm), 0, "no kick while closing");
    (VOID)XhciPollEnd(&st, 4, 0, 1, 50, &arm, &d);
    CHECK_EQ(st.State, XHCI_POLL_S_CLOSED, "closed");
}

static void test_watch_role(void)
{
    XHCI_POLL_STATE st;
    ULONG arm;
    ULONG d;

    XhciPollInit(&st);
    (VOID)XhciPollOpen(&st, XHCI_POLL_ROLE_WATCH);
    CHECK_EQ(XhciPollKick(&st, 0, 1, &arm), 1, "watcher kicked");
    CHECK_EQ(arm, XHCI_POLL_WATCH_MS, "at its own period");
    CHECK_EQ(XhciPollPromote(&st, 1), XHCI_POLL_P_NONE,
             "a watcher is never promoted");
    (VOID)XhciPollTake(&st);
    CHECK_EQ(XhciPollPromote(&st, 1), XHCI_POLL_P_NONE,
             "nor while it runs");
    /* Its arm ignores the registry's extremes and any decision. */
    CHECK_EQ(XhciPollEnd(&st, 1, 0, 1, 1, &arm, &d), 1, "re-armed");
    CHECK_EQ(arm, XHCI_POLL_WATCH_MS, "idle 1, active 1: still 100");
    (VOID)XhciPollTake(&st);
    CHECK_EQ(XhciPollEnd(&st, 5000, 0, 5000, 5000, &arm, &d), 1, "re-armed");
    CHECK_EQ(arm, XHCI_POLL_WATCH_MS, "idle 5000, active 5000: still 100");
    CHECK_EQ(d, 0, "never displaced");

    /* The move: the owner turns into a server, ownership unchanged. */
    (VOID)XhciPollTake(&st);
    XhciPollServe(&st);
    CHECK_EQ(st.Role, XHCI_POLL_ROLE_SERVE, "served");
    CHECK_EQ(st.State, XHCI_POLL_S_FIRING, "still the DPC's");
    CHECK_EQ(XhciPollEnd(&st, 1, 0, 1, 50, &arm, &d), 1, "re-armed");
    CHECK_EQ(arm, 1, "now at the service period");

    /* A move made in the same pass as a close: recorded, nothing armed. */
    XhciPollInit(&st);
    (VOID)XhciPollOpen(&st, XHCI_POLL_ROLE_WATCH);
    (VOID)XhciPollKick(&st, 0, 1, &arm);
    (VOID)XhciPollTake(&st);
    (VOID)XhciPollCloseBegin(&st);
    XhciPollServe(&st);
    CHECK_EQ(st.Role, XHCI_POLL_ROLE_SERVE, "the move recorded");
    CHECK_EQ(XhciPollEnd(&st, 1, 0, 1, 50, &arm, &d), 0, "nothing armed");
    CHECK_EQ(st.State, XHCI_POLL_S_CLOSED, "closed");

    /* A watcher's close against its own expiry. */
    XhciPollInit(&st);
    (VOID)XhciPollOpen(&st, XHCI_POLL_ROLE_WATCH);
    (VOID)XhciPollKick(&st, 0, 1, &arm);
    CHECK_EQ(XhciPollCloseBegin(&st), XHCI_POLL_C_CANCEL, "cancel asked");
    CHECK_EQ(XhciPollCloseDone(&st, 0), XHCI_POLL_C_WAIT, "expired");
    CHECK_EQ(XhciPollTake(&st), XHCI_POLL_TAKE_CLOSING, "the DPC ends it");
    (VOID)XhciPollEnd(&st, 1, 0, 1, 50, &arm, &d);
    CHECK_EQ(st.State, XHCI_POLL_S_CLOSED, "closed");

    /* A watcher across a recovery: parked (history gone), kicked back at
     * its period. */
    XhciPollInit(&st);
    (VOID)XhciPollOpen(&st, XHCI_POLL_ROLE_WATCH);
    (VOID)XhciPollKick(&st, 0, 1, &arm);
    (VOID)XhciPollTake(&st);
    (VOID)XhciPollWatchLook(&st, 1, 1, 7, MS(0));
    (VOID)XhciPollWatchLook(&st, 1, 1, 7, MS(100));
    CHECK_EQ(st.WatchPairs, 1, "a pair before the recovery");
    XhciPollPark(&st);
    CHECK_EQ(st.WatchPairs, 0, "the park forgets it");
    (VOID)XhciPollEnd(&st, 1, 0, 1, 50, &arm, &d);
    CHECK_EQ(XhciPollKick(&st, 0, 1, &arm), 1, "kicked");
    CHECK_EQ(arm, XHCI_POLL_WATCH_MS, "at the watch period");
}

static void test_watch_look(void)
{
    XHCI_POLL_STATE st;
    ULONG t;

    XhciPollInit(&st);
    (VOID)XhciPollOpen(&st, XHCI_POLL_ROLE_WATCH);
    /* Four asserted looks, 100 ms apart, no claim: dead at the fourth. */
    t = 0x7FFFFF00UL;
    CHECK_EQ(XhciPollWatchLook(&st, 5, 1, 9, t), 0, "first asserted");
    CHECK_EQ(XhciPollWatchLook(&st, 5, 1, 9, t + MS(100)), 0, "pair 1");
    CHECK_EQ(XhciPollWatchLook(&st, 5, 1, 9, t + MS(200)), 0, "pair 2");
    CHECK_EQ(XhciPollWatchLook(&st, 5, 1, 9, t + MS(300)), 1, "pair 3: dead");

    /* A claim between looks starts afresh. */
    XhciPollWatchReset(&st);
    (VOID)XhciPollWatchLook(&st, 5, 1, 9, 0);
    (VOID)XhciPollWatchLook(&st, 5, 1, 9, MS(100));
    (VOID)XhciPollWatchLook(&st, 5, 1, 10, MS(200));
    CHECK_EQ(st.WatchPairs, 0, "a claim resets");
    (VOID)XhciPollWatchLook(&st, 5, 1, 10, MS(300));
    (VOID)XhciPollWatchLook(&st, 5, 1, 10, MS(400));
    CHECK_EQ(XhciPollWatchLook(&st, 5, 1, 10, MS(500)), 1,
             "three pairs after the claim");

    /* A clear look starts afresh. */
    XhciPollWatchReset(&st);
    (VOID)XhciPollWatchLook(&st, 5, 1, 9, 0);
    (VOID)XhciPollWatchLook(&st, 5, 1, 9, MS(100));
    (VOID)XhciPollWatchLook(&st, 5, 1, 9, MS(200));
    (VOID)XhciPollWatchLook(&st, 5, 0, 9, MS(300));
    CHECK_EQ(XhciPollWatchLook(&st, 5, 1, 9, MS(400)), 0, "after a clear");
    CHECK_EQ(st.WatchPairs, 0, "no pair yet");

    /* A generation change between looks starts afresh: a recovery
     * completed wholly between two looks. */
    XhciPollWatchReset(&st);
    (VOID)XhciPollWatchLook(&st, 5, 1, 9, 0);
    (VOID)XhciPollWatchLook(&st, 5, 1, 9, MS(100));
    (VOID)XhciPollWatchLook(&st, 5, 1, 9, MS(200));
    CHECK_EQ(XhciPollWatchLook(&st, 7, 1, 9, MS(300)), 0, "new generation");
    CHECK_EQ(st.WatchPairs, 0, "history dropped");
    CHECK_EQ(st.WatchGen, 7, "generation taken");

    /* Early expiries: four looks inside 300 ms do not prove it. */
    XhciPollWatchReset(&st);
    (VOID)XhciPollWatchLook(&st, 7, 1, 9, 0);
    (VOID)XhciPollWatchLook(&st, 7, 1, 9, MS(10));
    (VOID)XhciPollWatchLook(&st, 7, 1, 9, MS(20));
    CHECK_EQ(XhciPollWatchLook(&st, 7, 1, 9, MS(30)), 0, "too soon");
    CHECK_EQ(XhciPollWatchLook(&st, 7, 1, 9, MS(299)), 0, "still too soon");
    CHECK_EQ(XhciPollWatchLook(&st, 7, 1, 9, MS(300)), 1, "300 ms: dead");

    /* A clock stepped back mid-run cannot make the window. */
    XhciPollWatchReset(&st);
    (VOID)XhciPollWatchLook(&st, 7, 1, 9, MS(1000));
    (VOID)XhciPollWatchLook(&st, 7, 1, 9, MS(1100));
    (VOID)XhciPollWatchLook(&st, 7, 1, 9, MS(1200));
    CHECK_EQ(XhciPollWatchLook(&st, 7, 1, 9, MS(100)), 0,
             "stepped back: not proven");
}

/* ------------------------------------------------------------------ */

static void test_period(void)
{
    XHCI_POLL_BUS bus;
    ULONG state;
    ULONG ms;
    ULONG i;
    PUCHAR p;

    p = (PUCHAR)&bus;
    for (i = 0; i < sizeof(bus); i++) {
        p[i] = 0;
    }
    ms = XhciPollDecidePeriod(&bus, 1, 50, &state);
    CHECK_EQ(ms, 50, "nothing: idle");
    CHECK_EQ(state, XHCI_POLL_BUS_IDLE, "idle");

    bus.PeriodicMs = 8;
    ms = XhciPollDecidePeriod(&bus, 1, 50, &state);
    CHECK_EQ(ms, 8, "a mouse at 8 ms");
    CHECK_EQ(state, XHCI_POLL_BUS_PERIODIC, "periodic");
    bus.PeriodicMs = 256;
    CHECK_EQ(XhciPollDecidePeriod(&bus, 1, 50, &state), 50,
             "a hub's interval held to idle");
    bus.PeriodicMs = 2;
    CHECK_EQ(XhciPollDecidePeriod(&bus, 4, 50, &state), 4,
             "held up to active");

    bus.Async = 1;
    CHECK_EQ(XhciPollDecidePeriod(&bus, 1, 50, &state), 1, "bulk: active");
    CHECK_EQ(state, XHCI_POLL_BUS_ACTIVE, "active");
    bus.Async = 0;
    bus.Command = 1;
    CHECK_EQ(XhciPollDecidePeriod(&bus, 1, 50, &state), 1, "command");
    bus.Command = 0;
    bus.PortsBusy = 1;
    CHECK_EQ(XhciPollDecidePeriod(&bus, 1, 50, &state), 1, "port busy");
    bus.PortsBusy = 0;
    bus.DrainedRecently = 1;
    CHECK_EQ(XhciPollDecidePeriod(&bus, 1, 50, &state), 1, "recent drain");
    bus.DrainedRecently = 0;
    bus.Bounded = 1;
    CHECK_EQ(XhciPollDecidePeriod(&bus, 1, 50, &state), 1, "bounded drain");

    CHECK_EQ(XhciPollIntervalMs(0), 0, "125 us: below 1 ms");
    CHECK_EQ(XhciPollIntervalMs(3), 1, "1 ms");
    CHECK_EQ(XhciPollIntervalMs(6), 8, "8 ms");
    CHECK_EQ(XhciPollIntervalMs(4), 2, "2 ms");
    CHECK_EQ(XhciPollIntervalMs(15), 4096, "the largest");
    CHECK_EQ(XhciPollIntervalMs(99), 4096, "held to 15");
}

static void test_recent(void)
{
    ULONG seen;
    ULONG late;

    seen = 0;
    CHECK(!XhciPollRecent(&seen, 0, 0, 50), "nothing seen");
    seen = 1;
    CHECK(XhciPollRecent(&seen, MS(100), MS(120), 50), "20 ms ago: recent");
    CHECK(seen, "still seen");
    CHECK(!XhciPollRecent(&seen, MS(100), MS(150), 50), "50 ms ago: not");
    CHECK(!seen, "expired");
    CHECK(!XhciPollRecent(&seen, MS(100), MS(120), 50),
          "expired for good, even at a recent-looking time");
    seen = 1;
    CHECK(!XhciPollRecent(&seen, MS(100), MS(90), 50), "clock stepped back");
    CHECK(!seen, "expired by the step");
    seen = 1;
    late = 0x10;
    CHECK(!XhciPollRecent(&seen, late, late + 0x80000010UL, 50),
          "past the signed half-range: not recent");
    seen = 1;
    late = 0xFFFFFFF0UL;
    CHECK(XhciPollRecent(&seen, late, late + MS(10), 50),
          "across the wrap, 10 ms: recent");
}

static void test_observe(void)
{
    XHCI_POLL_OBS o;

    o.LastValid = 0;
    o.Last = 0;
    o.MinMs = XHCI_POLL_NOT_FOUND;
    o.MaxMs = 0;
    o.Discarded = 0;
    XhciPollObserve(&o, MS(100));
    CHECK_EQ(o.MinMs, XHCI_POLL_NOT_FOUND, "first run: no interval");
    XhciPollObserve(&o, MS(116));
    CHECK_EQ(o.MinMs, 16, "16 ms");
    CHECK_EQ(o.MaxMs, 16, "16 ms max");
    XhciPollObserve(&o, MS(126));
    CHECK_EQ(o.MinMs, 10, "10 ms min");
    XhciPollObserve(&o, MS(176));
    CHECK_EQ(o.MaxMs, 50, "50 ms max");
    XhciPollObserve(&o, MS(100));
    CHECK_EQ(o.Discarded, 1, "negative discarded");
    XhciPollObserve(&o, MS(100) + MS(10001));
    CHECK_EQ(o.Discarded, 2, "beyond twice the bound discarded");
    CHECK_EQ(o.MaxMs, 50, "max unchanged");
    XhciPollObserve(&o, MS(100) + MS(10021));
    CHECK_EQ(o.MinMs, 10, "min unchanged at 20");
}


/* ------------------------------------------------------------------ */
/* The retirement over a modelled register file                        */
/* ------------------------------------------------------------------ */

typedef struct _FAKE_REGS {
    ULONG Usbcmd;
    ULONG Usbsts;
    ULONG Iman;
    ULONG SwallowInte;      /* INTE writes ignored this many times       */
    ULONG SwallowIe;        /* IE-clearing IMAN writes ignored           */
    ULONG ImanDeadReads;    /* IMAN reads answering all ones             */
    ULONG IpReraise;        /* IP set again after this many acks          */
    ULONG IpStuck;          /* IP never clears                           */
    ULONG Writes;
} FAKE_REGS;

static ULONG fakeRead(PVOID c, ULONG reg)
{
    FAKE_REGS *f = (FAKE_REGS *)c;

    if (reg == XHCI_POLL_REG_USBCMD) {
        return f->Usbcmd;
    }
    if (reg == XHCI_POLL_REG_USBSTS) {
        return f->Usbsts;
    }
    if (f->ImanDeadReads != 0) {
        f->ImanDeadReads--;
        return 0xFFFFFFFFUL;
    }
    return f->Iman;
}

static VOID fakeClearInte(PVOID c, ULONG usbcmd)
{
    FAKE_REGS *f = (FAKE_REGS *)c;

    f->Writes++;
    CHECK((usbcmd & XHCI_POLL_USBCMD_HCRST) == 0, "never carries HCRST");
    if (f->SwallowInte != 0) {
        f->SwallowInte--;
        return;
    }
    f->Usbcmd = usbcmd & ~XHCI_POLL_USBCMD_INTE;
}

static VOID fakeWriteIman(PVOID c, ULONG value)
{
    FAKE_REGS *f = (FAKE_REGS *)c;

    f->Writes++;
    if ((value & XHCI_POLL_IMAN_IE) == 0 && (f->Iman & XHCI_POLL_IMAN_IE)) {
        if (f->SwallowIe != 0) {
            f->SwallowIe--;
        } else {
            f->Iman &= ~XHCI_POLL_IMAN_IE;
        }
    }
    if ((value & XHCI_POLL_IMAN_IP) != 0 && !f->IpStuck) {
        f->Iman &= ~XHCI_POLL_IMAN_IP;
        if (f->IpReraise != 0) {
            f->IpReraise--;
            f->Iman |= XHCI_POLL_IMAN_IP;
        }
    }
}

static ULONG retire(FAKE_REGS *f)
{
    XHCI_POLL_REGS io;

    io.Context = f;
    io.Read = fakeRead;
    io.ClearInte = fakeClearInte;
    io.WriteIman = fakeWriteIman;
    return XhciPollRetire(&io);
}

static void fake_reset(FAKE_REGS *f)
{
    PUCHAR p;
    ULONG i;

    p = (PUCHAR)f;
    for (i = 0; i < sizeof(*f); i++) {
        p[i] = 0;
    }
    f->Usbcmd = 0x00000005UL;               /* R/S, INTE                  */
    f->Usbsts = 0x00000008UL;               /* EINT                       */
    f->Iman = 0xF0000003UL;                 /* IP, IE, reserved bits     */
}

static void test_retire(void)
{
    FAKE_REGS f;

    fake_reset(&f);
    CHECK_EQ(retire(&f), XHCI_POLL_RETIRE_PROVEN, "the standing line");
    CHECK_EQ(f.Usbcmd, 1, "INTE cleared, R/S kept");
    CHECK_EQ(f.Iman, 0xF0000000UL, "IP and IE 0, reserved kept");
    CHECK_EQ(f.Usbsts, 8, "EINT untouched");

    fake_reset(&f);
    f.SwallowInte = 2;
    CHECK_EQ(retire(&f), XHCI_POLL_RETIRE_PROVEN, "INTE on the third try");
    fake_reset(&f);
    f.SwallowInte = 3;
    CHECK_EQ(retire(&f), XHCI_POLL_RETIRE_ENABLES, "INTE never proven");
    CHECK((f.Iman & XHCI_POLL_IMAN_IP) != 0, "IP left for the ISR");
    CHECK_EQ(f.Usbsts, 8, "EINT left for the ISR");

    fake_reset(&f);
    f.SwallowIe = 3;
    CHECK_EQ(retire(&f), XHCI_POLL_RETIRE_ENABLES, "IE never proven");
    CHECK((f.Iman & XHCI_POLL_IMAN_IP) != 0, "IP not acknowledged");

    fake_reset(&f);
    f.IpReraise = 1;
    CHECK_EQ(retire(&f), XHCI_POLL_RETIRE_PROVEN,
             "IP set again by a pending event: the second ack stays");
    fake_reset(&f);
    f.IpStuck = 1;
    CHECK_EQ(retire(&f), XHCI_POLL_RETIRE_IP, "IP that never clears");

    fake_reset(&f);
    f.ImanDeadReads = 1;
    CHECK_EQ(retire(&f), XHCI_POLL_RETIRE_PROVEN, "one dead IMAN read");
    fake_reset(&f);
    f.ImanDeadReads = 100;
    CHECK_EQ(retire(&f), XHCI_POLL_RETIRE_ENABLES, "a dead window");
    CHECK_EQ(f.Writes, 1, "only INTE written, once, from a valid read");

    fake_reset(&f);
    f.Usbcmd |= XHCI_POLL_USBCMD_HCRST;
    CHECK_EQ(retire(&f), XHCI_POLL_RETIRE_RESETTING, "HCRST under way");
    CHECK_EQ(f.Writes, 0, "nothing written");
    fake_reset(&f);
    f.Usbsts |= XHCI_POLL_USBSTS_CNR;
    CHECK_EQ(retire(&f), XHCI_POLL_RETIRE_RESETTING, "CNR");

    fake_reset(&f);
    f.Usbcmd = 1;
    f.Iman = 0x1;                           /* already masked, IP standing */
    CHECK_EQ(retire(&f), XHCI_POLL_RETIRE_PROVEN, "masked already");
    CHECK_EQ(f.Iman, 0, "IP retired");
}

int main(void)
{
    test_values();
    test_decide();
    test_deadlines();
    test_open_close();
    test_dpc_and_promotion();
    test_park_kick_orders();
    test_watch_role();
    test_watch_look();
    test_period();
    test_observe();
    test_recent();
    test_retire();
    printf("test_poll: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
