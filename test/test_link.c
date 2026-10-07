/*
 * test_link.c - host vectors for the SuperSpeed link of a USB3 root port and
 * 29-A.5's hold (src\xhci_link.c; roadmap-hcd.md tasks 29-A.2 and 29-A.5).
 *
 * PORTSC values are composed from xhci.h's own bit names, so a vector says
 * which state it means rather than which hex it happens to be. The PLS
 * encodings are section 10.3's transcription, marked there for verification
 * against the PDF; these vectors pin the policy over them, not the numbers.
 */

#include <stdio.h>
#include "../src/xhci.h"
#include "../src/xhci_enum.h"
#include "../src/xhci_link.h"
#include "test_harness.h"

static ULONG portsc(ULONG pls, ULONG bits)
{
    return XHCI_PORTSC_PP | (pls << XHCI_PORTSC_PLS_SHIFT) | bits;
}

#define CONN    (XHCI_PORTSC_CCS)
#define ENAB    (XHCI_PORTSC_CCS | XHCI_PORTSC_PED)

static void test_classify(void)
{
    CHECK_EQ(XhciLinkClassify(0), XHCI_LINK_POWERED_OFF, "PP clear");
    CHECK_EQ(XhciLinkClassify(0xFFFFFFFFUL), XHCI_LINK_UNKNOWN,
             "an all-ones read is no state");
    CHECK_EQ(XhciLinkClassify(portsc(XHCI_PLS_RX_DETECT, 0)),
             XHCI_LINK_DISCONNECTED, "RxDetect, nothing connected");
    CHECK_EQ(XhciLinkClassify(portsc(XHCI_PLS_POLLING, 0)),
             XHCI_LINK_POLLING, "Polling");
    CHECK_EQ(XhciLinkClassify(portsc(XHCI_PLS_U0, ENAB)), XHCI_LINK_ENABLED,
             "trained: CCS, PED, U0");
    CHECK_EQ(XhciLinkClassify(portsc(XHCI_PLS_U1, ENAB)), XHCI_LINK_ENABLED,
             "U1 is Enabled");
    CHECK_EQ(XhciLinkClassify(portsc(XHCI_PLS_U2, ENAB)), XHCI_LINK_ENABLED,
             "U2 is Enabled");
    CHECK_EQ(XhciLinkClassify(portsc(XHCI_PLS_RECOVERY, ENAB)),
             XHCI_LINK_ENABLED, "Recovery is Enabled");
    CHECK_EQ(XhciLinkClassify(portsc(XHCI_PLS_U3, ENAB)),
             XHCI_LINK_SUSPENDED, "U3");
    CHECK_EQ(XhciLinkClassify(portsc(XHCI_PLS_U0, CONN)),
             XHCI_LINK_NOT_ENABLED, "trained with PED clear");
    CHECK_EQ(XhciLinkClassify(portsc(XHCI_PLS_U0, ENAB | XHCI_PORTSC_PR)),
             XHCI_LINK_RESETTING, "PR set");
    CHECK_EQ(XhciLinkClassify(portsc(XHCI_PLS_HOT_RESET, CONN)),
             XHCI_LINK_RESETTING, "PLS Hot Reset");
    CHECK_EQ(XhciLinkClassify(portsc(XHCI_PLS_INACTIVE, CONN)),
             XHCI_LINK_ERROR, "SS.Inactive is the Error state");
    CHECK_EQ(XhciLinkClassify(portsc(XHCI_PLS_INACTIVE, 0)),
             XHCI_LINK_ERROR, "with or without CCS");
    CHECK_EQ(XhciLinkClassify(portsc(XHCI_PLS_COMPLIANCE, 0)),
             XHCI_LINK_COMPLIANCE, "Compliance Mode");
    CHECK_EQ(XhciLinkClassify(portsc(XHCI_PLS_DISABLED, 0)),
             XHCI_LINK_DISABLED, "SS.Disabled");
    CHECK_EQ(XhciLinkClassify(portsc(XHCI_PLS_TEST_MODE, 0)),
             XHCI_LINK_UNKNOWN, "Test Mode is not a state the bus acts on");
    CHECK_EQ(XhciLinkClassify(portsc(XHCI_PLS_RESUME, ENAB)),
             XHCI_LINK_UNKNOWN, "Resume is a USB2 encoding");
}

static void test_service(void)
{
    XHCI_LINK_PORT l;
    XHCI_LINK_ACTION a;
    ULONG i;

    XhciLinkInit(&l);
    CHECK_EQ(XhciLinkDecide(&l, portsc(XHCI_PLS_POLLING, 0),
                            XHCI_LINK_WANT_SERVICE, &a),
             XHCI_LINK_ACT_WAIT, "Polling: wait for the training");
    CHECK_EQ(XhciLinkDecide(&l, portsc(XHCI_PLS_U0, ENAB),
                            XHCI_LINK_WANT_SERVICE, &a),
             XHCI_LINK_ACT_READY, "Polling -> U0: enumerate");
    CHECK_EQ(XhciLinkDecide(&l, portsc(XHCI_PLS_RX_DETECT, 0),
                            XHCI_LINK_WANT_SERVICE, &a),
             XHCI_LINK_ACT_NONE, "nothing there: nothing to do");

    /* SS.Inactive: warm-reset recovery, bounded, then given up. */
    for (i = 0; i < XHCI_LINK_MAX_WARM_RESETS; i++) {
        CHECK_EQ(XhciLinkDecide(&l, portsc(XHCI_PLS_INACTIVE, CONN),
                                XHCI_LINK_WANT_SERVICE, &a),
                 XHCI_LINK_ACT_WARM_RESET, "SS.Inactive: warm reset");
        CHECK_EQ(a.Converted, 0, "asked for as such, not converted");
    }
    CHECK_EQ(XhciLinkDecide(&l, portsc(XHCI_PLS_INACTIVE, CONN),
                            XHCI_LINK_WANT_SERVICE, &a),
             XHCI_LINK_ACT_GIVE_UP, "the budget spent: given up once");
    CHECK_EQ(XhciLinkDecide(&l, portsc(XHCI_PLS_INACTIVE, CONN),
                            XHCI_LINK_WANT_SERVICE, &a),
             XHCI_LINK_ACT_NONE, "and only once");
    CHECK_EQ(XhciLinkDecide(&l, portsc(XHCI_PLS_RX_DETECT, 0),
                            XHCI_LINK_WANT_SERVICE, &a),
             XHCI_LINK_ACT_NONE, "a disconnect");
    CHECK_EQ(XhciLinkDecide(&l, portsc(XHCI_PLS_COMPLIANCE, 0),
                            XHCI_LINK_WANT_SERVICE, &a),
             XHCI_LINK_ACT_WARM_RESET,
             "starts the budget again: Compliance Mode recovered");

    /* A link that trains after one warm reset resets the budget too. */
    XhciLinkInit(&l);
    (VOID)XhciLinkDecide(&l, portsc(XHCI_PLS_INACTIVE, CONN),
                         XHCI_LINK_WANT_SERVICE, &a);
    (VOID)XhciLinkDecide(&l, portsc(XHCI_PLS_U0, ENAB),
                         XHCI_LINK_WANT_SERVICE, &a);
    CHECK_EQ(l.WarmResets, 0, "Enabled clears the warm-reset count");

    /* Cold Attach Status on a link that is otherwise Disconnected. */
    XhciLinkInit(&l);
    CHECK_EQ(XhciLinkDecide(&l, portsc(XHCI_PLS_RX_DETECT, XHCI_PORTSC_CAS),
                            XHCI_LINK_WANT_SERVICE, &a),
             XHCI_LINK_ACT_WARM_RESET, "CAS: warm reset");
    CHECK_EQ(XhciLinkDecide(&l, portsc(XHCI_PLS_DISABLED, 0),
                            XHCI_LINK_WANT_SERVICE, &a),
             XHCI_LINK_ACT_NONE, "a held port is not recovered by service");
}

static void test_reset_policy(void)
{
    XHCI_LINK_PORT l;
    XHCI_LINK_ACTION a;

    XhciLinkInit(&l);
    CHECK_EQ(XhciLinkDecide(&l, portsc(XHCI_PLS_U0, ENAB),
                            XHCI_LINK_WANT_RESET, &a),
             XHCI_LINK_ACT_HOT_RESET, "U0: a hot reset");
    CHECK_EQ(a.Converted, 0, "not converted");
    CHECK_EQ(l.LastReset, XHCI_LINK_ACT_HOT_RESET, "and recorded as hot");
    CHECK_EQ(XhciLinkDecide(&l, portsc(XHCI_PLS_U3, ENAB),
                            XHCI_LINK_WANT_RESET, &a),
             XHCI_LINK_ACT_WARM_RESET, "U3: warm");
    CHECK_EQ(a.Converted, 1, "a hot reset converted to warm, and said so");
    CHECK_EQ(XhciLinkDecide(&l, portsc(XHCI_PLS_U2, ENAB),
                            XHCI_LINK_WANT_RESET, &a),
             XHCI_LINK_ACT_WARM_RESET, "U2: warm");
    CHECK_EQ(XhciLinkDecide(&l, portsc(XHCI_PLS_U0, ENAB),
                            XHCI_LINK_WANT_RESET, &a),
             XHCI_LINK_ACT_HOT_RESET, "U0 again: hot, the budget refilled");
    CHECK_EQ(XhciLinkDecide(&l, portsc(XHCI_PLS_INACTIVE, CONN),
                            XHCI_LINK_WANT_RESET, &a),
             XHCI_LINK_ACT_WARM_RESET, "SS.Inactive: warm");
    CHECK_EQ(XhciLinkDecide(&l, portsc(XHCI_PLS_U0, CONN),
                            XHCI_LINK_WANT_RESET, &a),
             XHCI_LINK_ACT_WARM_RESET, "trained, PED clear: warm");
    CHECK_EQ(XhciLinkDecide(&l, portsc(XHCI_PLS_RX_DETECT, 0),
                            XHCI_LINK_WANT_RESET, &a),
             XHCI_LINK_ACT_NONE, "nothing to reset");
    CHECK_EQ(XhciLinkDecide(&l, portsc(XHCI_PLS_DISABLED, 0),
                            XHCI_LINK_WANT_RESET, &a),
             XHCI_LINK_ACT_NONE, "a warm reset does not act on Disabled");
    CHECK_EQ(XhciLinkDecide(&l, portsc(XHCI_PLS_U0, ENAB | XHCI_PORTSC_PR),
                            XHCI_LINK_WANT_RESET, &a),
             XHCI_LINK_ACT_NONE, "nor is a running reset restarted");
    CHECK_EQ(XhciLinkDecide(NULL, portsc(XHCI_PLS_U0, ENAB),
                            XHCI_LINK_WANT_RESET, &a),
             XHCI_LINK_ACT_NONE, "NULL record");
    CHECK_EQ(XhciLinkDecide(&l, portsc(XHCI_PLS_U0, ENAB),
                            XHCI_LINK_WANT_RESET, NULL),
             XHCI_LINK_ACT_HOT_RESET, "a NULL action still answers");
}

static void test_power_and_hold_writes(void)
{
    XHCI_LINK_PORT l;
    XHCI_LINK_ACTION a;
    ULONG warm;

    XhciLinkInit(&l);
    CHECK_EQ(XhciLinkDecide(&l, portsc(XHCI_PLS_U3, ENAB),
                            XHCI_LINK_WANT_RESUME, &a),
             XHCI_LINK_ACT_SET_U0, "a USB3 resume is one PLS = U0 write");
    CHECK_EQ(XhciLinkDecide(&l, portsc(XHCI_PLS_U0, ENAB),
                            XHCI_LINK_WANT_RESUME, &a),
             XHCI_LINK_ACT_NONE, "nothing to resume in U0");
    CHECK_EQ(XhciLinkDecide(&l, portsc(XHCI_PLS_U0, ENAB),
                            XHCI_LINK_WANT_SUSPEND, &a),
             XHCI_LINK_ACT_SET_U3, "suspend: PLS = U3");
    CHECK_EQ(XhciLinkDecide(&l, portsc(XHCI_PLS_INACTIVE, CONN),
                            XHCI_LINK_WANT_SUSPEND, &a),
             XHCI_LINK_ACT_NONE, "not from Error");
    CHECK_EQ(XhciLinkDecide(&l, portsc(XHCI_PLS_U0, ENAB),
                            XHCI_LINK_WANT_HOLD, &a),
             XHCI_LINK_ACT_DISABLE, "a hold writes PED = 1");
    CHECK_EQ(XhciLinkDecide(&l, portsc(XHCI_PLS_POLLING, 0),
                            XHCI_LINK_WANT_HOLD, &a),
             XHCI_LINK_ACT_NONE, "no trained link to hold");
    CHECK_EQ(XhciLinkDecide(&l, portsc(XHCI_PLS_DISABLED, 0),
                            XHCI_LINK_WANT_RELEASE, &a),
             XHCI_LINK_ACT_RX_DETECT, "a release re-arms with RxDetect");
    CHECK_EQ(XhciLinkDecide(&l, portsc(XHCI_PLS_U0, ENAB),
                            XHCI_LINK_WANT_RELEASE, &a),
             XHCI_LINK_ACT_NONE, "only a Disabled port is released");

    /* The writes the actions become (xhci_port.c). */
    CHECK_EQ(XhciPortscWarmReset(portsc(XHCI_PLS_INACTIVE,
                                        CONN | XHCI_PORTSC_CSC)),
             portsc(XHCI_PLS_INACTIVE, CONN | XHCI_PORTSC_WPR),
             "WPR set, the change bit not written back");
    CHECK_EQ(XhciPortscRxDetect(portsc(XHCI_PLS_DISABLED, XHCI_PORTSC_PEC)),
             portsc(XHCI_PLS_RX_DETECT, XHCI_PORTSC_LWS),
             "PLS RxDetect with LWS, nothing else");

    /* A reset's end. */
    CHECK_EQ(XhciLinkResetDone(portsc(XHCI_PLS_U0, ENAB | XHCI_PORTSC_PRC),
                               &warm), 1, "hot reset done");
    CHECK_EQ(warm, 0, "no WRC");
    CHECK_EQ(XhciLinkResetDone(portsc(XHCI_PLS_U0, ENAB | XHCI_PORTSC_PRC |
                                                    XHCI_PORTSC_WRC),
                               &warm), 1, "warm reset done");
    CHECK_EQ(warm, 1, "WRC seen");
    CHECK_EQ(XhciLinkResetDone(portsc(XHCI_PLS_INACTIVE,
                                      CONN | XHCI_PORTSC_PRC), &warm),
             0, "a reset that left SS.Inactive failed");
    CHECK_EQ(XhciLinkResetDone(portsc(XHCI_PLS_U0, ENAB), &warm), 0,
             "no PRC, not done");
    CHECK_EQ(XhciLinkResetDone(0xFFFFFFFFUL, &warm), 0, "all ones");
}

/* ------------------------------------------------------------------ */
/* 29-A.5's hold                                                       */
/* ------------------------------------------------------------------ */

static XHCI_LINK_IDENTITY ident(ULONG vid, ULONG pid, const char *serial)
{
    XHCI_LINK_IDENTITY id;
    ULONG i;

    id.Valid = 1;
    id.Vendor = vid;
    id.Product = pid;
    id.SerialLength = 0;
    for (i = 0; i < XHCI_LINK_SERIAL_CHARS; i++) {
        id.Serial[i] = 0;
    }
    for (i = 0; serial != NULL && serial[i] != 0 &&
                i < XHCI_LINK_SERIAL_CHARS; i++) {
        id.Serial[i] = (USHORT)(UCHAR)serial[i];
        id.SerialLength++;
    }
    return id;
}

static void test_hold_release(void)
{
    XHCI_LINK_HOLD h;
    XHCI_LINK_IDENTITY held;
    XHCI_LINK_IDENTITY other;

    held = ident(0x0781, 0x5581, "4C530001");

    /* The rule: a connect since the hold, the held device, then its
     * disconnect. */
    CHECK_EQ(XhciHoldBegin(&h, 5, 1, &held), XHCI_HOLD_PAIRED,
             "identified and paired");
    XhciHoldCompanionConnect(&h);
    XhciHoldCompanionIdentity(&h, &held);
    CHECK_EQ(XhciHoldCompanionDisconnect(&h), XHCI_HOLD_RELEASE,
             "the held device left the companion: released");
    CHECK_EQ(h.Kind, XHCI_HOLD_NONE, "and the hold is over");

    /* A companion disconnect with no prior connect. */
    (VOID)XhciHoldBegin(&h, 5, 1, &held);
    CHECK_EQ(XhciHoldCompanionDisconnect(&h), XHCI_HOLD_KEEP,
             "a disconnect with no connect since the hold releases nothing");

    /* An identity with no connect seen is not evidence either. */
    XhciHoldCompanionIdentity(&h, &held);
    CHECK_EQ(XhciHoldCompanionDisconnect(&h), XHCI_HOLD_KEEP,
             "an identity read with no connect behind it releases nothing");

    /* An unrelated device's connect and disconnect on a mis-paired
     * companion. */
    other = ident(0x046D, 0xC077, "XYZ");
    (VOID)XhciHoldBegin(&h, 5, 1, &held);
    XhciHoldCompanionConnect(&h);
    XhciHoldCompanionIdentity(&h, &other);
    CHECK_EQ(h.OthersSeen, 1, "another device's connect is counted");
    CHECK_EQ(XhciHoldCompanionDisconnect(&h), XHCI_HOLD_KEEP,
             "and its disconnect releases nothing");
    CHECK_EQ(h.Kind, XHCI_HOLD_PAIRED, "the hold stays");

    /* ...after which the held device itself still releases it. */
    XhciHoldCompanionConnect(&h);
    XhciHoldCompanionIdentity(&h, &held);
    CHECK_EQ(XhciHoldCompanionDisconnect(&h), XHCI_HOLD_RELEASE,
             "the held device's own visit does");

    /* A second unit of the same model with no serial on the companion. */
    other = ident(0x0781, 0x5581, NULL);
    (VOID)XhciHoldBegin(&h, 5, 1, &held);
    XhciHoldCompanionConnect(&h);
    XhciHoldCompanionIdentity(&h, &other);
    CHECK_EQ(XhciHoldCompanionDisconnect(&h), XHCI_HOLD_KEEP,
             "same vendor and product id, no serial: nothing released");

    /* ...or with a different serial. */
    other = ident(0x0781, 0x5581, "4C530002");
    (VOID)XhciHoldBegin(&h, 5, 1, &held);
    XhciHoldCompanionConnect(&h);
    XhciHoldCompanionIdentity(&h, &other);
    CHECK_EQ(XhciHoldCompanionDisconnect(&h), XHCI_HOLD_KEEP,
             "a different serial: nothing released");

    /* A failed identity read on the companion. */
    other = held;
    other.Valid = 0;
    (VOID)XhciHoldBegin(&h, 5, 1, &held);
    XhciHoldCompanionConnect(&h);
    XhciHoldCompanionIdentity(&h, &other);
    CHECK_EQ(XhciHoldCompanionDisconnect(&h), XHCI_HOLD_KEEP,
             "a failed read on the companion proves nothing");
}

static void test_hold_kinds(void)
{
    XHCI_LINK_HOLD h;
    XHCI_LINK_IDENTITY held;
    XHCI_LINK_IDENTITY noSerial;

    held = ident(0x0781, 0x5581, "4C530001");
    noSerial = ident(0x0781, 0x5581, NULL);

    /* No serial: an unidentified hold, until the controller's next start. */
    CHECK_EQ(XhciHoldBegin(&h, 5, 1, &noSerial), XHCI_HOLD_UNIDENTIFIED,
             "no serial is no identity");
    XhciHoldCompanionConnect(&h);
    XhciHoldCompanionIdentity(&h, &noSerial);
    CHECK_EQ(XhciHoldCompanionDisconnect(&h), XHCI_HOLD_KEEP,
             "even its own twin releases nothing");

    /* A hold taken before any descriptor was read (29-A.1's refusal under
     * its value, decided at link training). */
    CHECK_EQ(XhciHoldBegin(&h, 5, 1, NULL), XHCI_HOLD_UNIDENTIFIED,
             "no descriptor read");
    XhciHoldCompanionConnect(&h);
    XhciHoldCompanionIdentity(&h, &held);
    CHECK_EQ(XhciHoldCompanionDisconnect(&h), XHCI_HOLD_KEEP,
             "nothing can match it");

    /* A hold whose identity read failed. */
    noSerial = held;
    noSerial.Valid = 0;
    CHECK_EQ(XhciHoldBegin(&h, 5, 1, &noSerial), XHCI_HOLD_UNIDENTIFIED,
             "a failed identity read");

    /* An orphan: no companion, holds until the next start. */
    CHECK_EQ(XhciHoldBegin(&h, 5, 0, &held), XHCI_HOLD_ORPHAN, "orphan");
    XhciHoldCompanionConnect(&h);
    XhciHoldCompanionIdentity(&h, &held);
    CHECK_EQ(XhciHoldCompanionDisconnect(&h), XHCI_HOLD_KEEP,
             "an orphan has no companion to release it");

    CHECK_EQ(XhciLinkSameDevice(&held, &held), 1, "a device is itself");
    /* A serial longer than the kept prefix is a truncated identity, which
     * neither identifies a hold nor matches one (round 1, note 6). */
    noSerial = held;
    noSerial.SerialLength = XHCI_LINK_SERIAL_CHARS + 1;
    CHECK_EQ(XhciLinkSameDevice(&noSerial, &noSerial), 0,
             "a truncated serial matches nothing, not even itself");
    CHECK_EQ(XhciHoldBegin(&h, 5, 1, &noSerial), XHCI_HOLD_UNIDENTIFIED,
             "and makes an unidentified hold");
    CHECK_EQ(XhciLinkSameDevice(&held, NULL), 0, "NULL is no device");
    CHECK_EQ(XhciHoldBegin(NULL, 5, 1, &held), XHCI_HOLD_NONE, "NULL hold");
    CHECK_EQ(XhciHoldCompanionDisconnect(NULL), XHCI_HOLD_KEEP,
             "NULL hold keeps");
}

/*
 * The port-change wiring as hcd_enum.c's hcdPortChanged runs it - the feed
 * XhciLinkPortFeed decides, applied to a real enumeration machine (Codex
 * review of Phase 29, round 1, findings 2 and 3).
 */
static ULONG feed_apply(PXHCI_ENUM_PORT e, ULONG feed)
{
    XHCI_ENUM_EVENT ev;
    XHCI_ENUM_ACTION a;

    ev.Ok = 1;
    ev.Speed = 0;
    ev.SlotId = 0;
    ev.Bytes = 0;
    ev.Value = 0;
    if ((feed & XHCI_LINK_FEED_DISCONNECT) != 0) {
        ev.Kind = XHCI_ENUM_EV_DISCONNECT;
        (VOID)XhciEnumStep(e, &ev, &a);
    }
    if ((feed & XHCI_LINK_FEED_CONNECT) != 0) {
        ev.Kind = XHCI_ENUM_EV_CONNECT;
        (VOID)XhciEnumStep(e, &ev, &a);
    }
    return e->State;
}

static void test_port_feed(void)
{
    XHCI_LINK_PORT l;
    XHCI_LINK_ACTION a;
    XHCI_ENUM_PORT e;
    XHCI_ENUM_EVENT ev;
    XHCI_ENUM_ACTION ea;
    ULONG feed;
    ULONG i;

    /* A machine left Failed (its reset failed) while the link went to
     * SS.Inactive with CCS still set. */
    XhciLinkInit(&l);
    XhciEnumReset(&e);
    ev.Ok = 1; ev.Speed = 0; ev.SlotId = 0; ev.Bytes = 0; ev.Value = 0;
    ev.Kind = XHCI_ENUM_EV_CONNECT;
    (VOID)XhciEnumStep(&e, &ev, &ea);
    ev.Kind = XHCI_ENUM_EV_DEBOUNCED;
    (VOID)XhciEnumStep(&e, &ev, &ea);
    ev.Kind = XHCI_ENUM_EV_RESET_DONE;
    ev.Ok = 0;
    (VOID)XhciEnumStep(&e, &ev, &ea);
    CHECK_EQ(e.State, XHCI_ENUM_FAILED, "the machine is Failed");

    feed = XhciLinkPortFeed(&l, 1, portsc(XHCI_PLS_INACTIVE,
                                          CONN | XHCI_PORTSC_PLC),
                            e.State, &a);
    CHECK_EQ(a.Kind, XHCI_LINK_ACT_WARM_RESET, "SS.Inactive: warm reset");
    CHECK_EQ(feed, XHCI_LINK_FEED_DISCONNECT,
             "and the Failed machine is fed a disconnect");
    CHECK_EQ(feed_apply(&e, feed), XHCI_ENUM_EMPTY, "which empties it");

    /* The warm reset completes: PRC and WRC, CCS never dropped, no CSC. */
    feed = XhciLinkPortFeed(&l, 1, portsc(XHCI_PLS_U0, ENAB | XHCI_PORTSC_PRC |
                                                       XHCI_PORTSC_WRC),
                            e.State, &a);
    CHECK_EQ(a.Kind, XHCI_LINK_ACT_NONE, "a usable link asks for no write");
    CHECK_EQ(feed, XHCI_LINK_FEED_CONNECT, "and starts an enumeration");
    CHECK_EQ(feed_apply(&e, feed), XHCI_ENUM_DEBOUNCE,
             "the recovered device is enumerated, not left Failed");

    /* The budget spent, then a physical unplug and a new device that also
     * fails to train: the unplug refills the budget. */
    XhciLinkInit(&l);
    XhciEnumReset(&e);
    for (i = 0; i < XHCI_LINK_MAX_WARM_RESETS; i++) {
        (VOID)XhciLinkPortFeed(&l, 1, portsc(XHCI_PLS_COMPLIANCE, 0),
                               e.State, &a);
        CHECK_EQ(a.Kind, XHCI_LINK_ACT_WARM_RESET, "Compliance: warm");
    }
    (VOID)XhciLinkPortFeed(&l, 1, portsc(XHCI_PLS_COMPLIANCE, 0), e.State,
                           &a);
    CHECK_EQ(a.Kind, XHCI_LINK_ACT_GIVE_UP, "given up");
    feed = XhciLinkPortFeed(&l, 1, portsc(XHCI_PLS_RX_DETECT,
                                          XHCI_PORTSC_CSC), e.State, &a);
    CHECK_EQ(feed, 0, "the unplug feeds an Empty machine nothing");
    CHECK_EQ(l.WarmResets, 0, "but refills the warm-reset budget");
    CHECK_EQ(l.GaveUp, 0, "and clears the give-up");
    (VOID)XhciLinkPortFeed(&l, 1, portsc(XHCI_PLS_COMPLIANCE, 0), e.State,
                           &a);
    CHECK_EQ(a.Kind, XHCI_LINK_ACT_WARM_RESET,
             "the next device's failure is recovered again");

    /* USB 2.0 ports: section 5.3's rule, unchanged. */
    CHECK_EQ(XhciLinkPortFeed(&l, 0, XHCI_PORTSC_PP | CONN | XHCI_PORTSC_CSC,
                              XHCI_ENUM_EMPTY, &a),
             XHCI_LINK_FEED_CONNECT, "USB2: CSC with CCS connects");
    CHECK_EQ(XhciLinkPortFeed(&l, 0, XHCI_PORTSC_PP | CONN | XHCI_PORTSC_CSC,
                              XHCI_ENUM_BOUND, &a),
             XHCI_LINK_FEED_DISCONNECT | XHCI_LINK_FEED_CONNECT,
             "USB2: a replug drops and connects");
    CHECK_EQ(XhciLinkPortFeed(&l, 0, XHCI_PORTSC_PP | CONN | XHCI_PORTSC_PRC,
                              XHCI_ENUM_FAILED, &a),
             0, "USB2: a Failed port waits for a new connection");
    CHECK_EQ(XhciLinkPortFeed(&l, 0, XHCI_PORTSC_PP | XHCI_PORTSC_PEC,
                              XHCI_ENUM_BOUND, &a),
             XHCI_LINK_FEED_DISCONNECT, "USB2: no CCS, a disconnect");
    CHECK_EQ(a.Kind, XHCI_LINK_ACT_NONE, "and never a link write");
    /* The same Inactive-looking PLS on a USB2 port is not a link decision. */
    CHECK_EQ(XhciLinkPortFeed(&l, 0, portsc(XHCI_PLS_INACTIVE, CONN),
                              XHCI_ENUM_FAILED, &a),
             0, "a USB2 port's PLS is not read as a SuperSpeed link");
}

/* The companion's PORTSC as the executor feeds it (XhciHoldCompanionPortsc):
 * the same rules, driven by register values. */
static void test_hold_portsc(void)
{
    XHCI_LINK_HOLD h;
    XHCI_LINK_IDENTITY held;
    XHCI_LINK_IDENTITY other;
    ULONG conn;
    ULONG gone;
    ULONG quiet;

    held = ident(0x0781, 0x5581, "4C530001");
    other = ident(0x046D, 0xC077, "XYZ");
    conn = XHCI_PORTSC_PP | XHCI_PORTSC_CCS | XHCI_PORTSC_CSC;
    gone = XHCI_PORTSC_PP | XHCI_PORTSC_CSC;
    quiet = XHCI_PORTSC_PP | XHCI_PORTSC_CCS | XHCI_PORTSC_PRC;

    /* Held device arrives, is identified, leaves: released. */
    (VOID)XhciHoldBegin(&h, 5, 1, &held);
    CHECK_EQ(XhciHoldCompanionPortsc(&h, gone), XHCI_HOLD_KEEP,
             "a disconnect before any connect keeps the hold");
    CHECK_EQ(XhciHoldCompanionPortsc(&h, conn), XHCI_HOLD_KEEP,
             "the connect is recorded");
    CHECK_EQ(h.ConnectSeen, 1, "ConnectSeen");
    CHECK_EQ(XhciHoldCompanionPortsc(&h, quiet), XHCI_HOLD_KEEP,
             "a reset's change on a connected port is neither");
    CHECK_EQ(h.ConnectSeen, 1, "and forgets nothing");
    XhciHoldCompanionIdentity(&h, &held);
    CHECK_EQ(XhciHoldCompanionPortsc(&h, gone), XHCI_HOLD_RELEASE,
             "the held device leaves: released");

    /* A replug of another device in one change: the departure judged, the
     * arrival recorded afresh. */
    (VOID)XhciHoldBegin(&h, 5, 1, &held);
    (VOID)XhciHoldCompanionPortsc(&h, conn);
    XhciHoldCompanionIdentity(&h, &other);
    CHECK_EQ(XhciHoldCompanionPortsc(&h, conn), XHCI_HOLD_KEEP,
             "another device left and one came: kept");
    CHECK_EQ(h.ConnectSeen, 1, "the new arrival is recorded");
    CHECK_EQ(h.MatchSeen, 0, "and not yet identified");
    XhciHoldCompanionIdentity(&h, &held);
    CHECK_EQ(XhciHoldCompanionPortsc(&h, gone), XHCI_HOLD_RELEASE,
             "then the held device's own visit releases");

    /* An unidentified hold never releases. */
    other = ident(0x0781, 0x5581, NULL);
    (VOID)XhciHoldBegin(&h, 5, 1, &other);
    (VOID)XhciHoldCompanionPortsc(&h, conn);
    XhciHoldCompanionIdentity(&h, &other);
    CHECK_EQ(XhciHoldCompanionPortsc(&h, gone), XHCI_HOLD_KEEP,
             "unidentified: kept to the next start");
    CHECK_EQ(XhciHoldCompanionPortsc(NULL, gone), XHCI_HOLD_KEEP, "NULL");
    CHECK_EQ(XhciHoldCompanionPortsc(&h, 0xFFFFFFFFUL), XHCI_HOLD_KEEP,
             "an all-ones read is no event");
}

/*
 * Task 35.1 (issue 11): the E460's USB 3 root port 13 as 35.0 read it,
 * PORTSC 00001203 - CCS, PED, U0, speed ID 4 - through the controller's
 * own PSI table (Sunrise Point-LP, 8086:9D2F: USB 3 lists only SSIC IDs 1
 * to 3; xhciqual/results/e460-2026-10-06/PROBE.LOG) and into the machine,
 * as hcd_enum.c's XHCI_ENUM_ACT_RESET does: the raw ID kept for the Slot
 * Context, the decoded class handed to the machine.
 */
static void e460_map(PXHCI_PORT_MAP m)
{
    PUCHAR p;
    ULONG i;

    p = (PUCHAR)m;
    for (i = 0; i < sizeof(*m); i++) {
        p[i] = 0;
    }
    for (i = 0; i < XHCI_MAX_ROOT_PORTS; i++) {
        m->Protocol[i] = XHCI_PORT_NO_PROTOCOL;
    }
    m->PortCount = 18;
    m->ProtocolCount = 2;
    m->Protocols[0].Major = 2;
    m->Protocols[0].PortOffset = 1;
    m->Protocols[0].PortCount = 12;
    m->Protocols[0].PsiCount = 3;
    m->Protocols[0].Psi[0] = 0x000C0021UL;
    m->Protocols[0].Psi[1] = 0x05DC0012UL;
    m->Protocols[0].Psi[2] = 0x01E00023UL;
    m->Protocols[1].Major = 3;
    m->Protocols[1].PortOffset = 13;
    m->Protocols[1].PortCount = 6;
    m->Protocols[1].PsiCount = 3;
    m->Protocols[1].Psi[0] = 0x04E00121UL;
    m->Protocols[1].Psi[1] = 0x09C00122UL;
    m->Protocols[1].Psi[2] = 0x13800123UL;
    for (i = 0; i < 12; i++) {
        m->Protocol[i] = 0;
    }
    for (i = 12; i < 18; i++) {
        m->Protocol[i] = 1;
    }
}

/* hcd_enum.c's hcdEnumSpeedOf: the class's default ID, 0 for unknown. */
static ULONG enum_speed_of(ULONG speedClass)
{
    switch (speedClass) {
    case XHCI_SPEED_LOW:
        return XHCI_ENUM_SPEED_LOW;
    case XHCI_SPEED_FULL:
        return XHCI_ENUM_SPEED_FULL;
    case XHCI_SPEED_HIGH:
        return XHCI_ENUM_SPEED_HIGH;
    case XHCI_SPEED_SUPER:
        return XHCI_ENUM_SPEED_SUPER;
    default:
        return 0;
    }
}

/* The Speed field of the Slot Context the encoder builds for a device on
 * a root port, as hcd_enum.c builds it from p->LinkPsiv. */
static ULONG slot_speed(ULONG rawPsiv, ULONG rootPort)
{
    static ULONG ctx[XHCI_CONTEXT_SIZE_LARGE / 4];
    XHCI_SLOT_PARAMS sp;
    ULONG i;

    for (i = 0; i < XHCI_CONTEXT_SIZE_LARGE / 4; i++) {
        ctx[i] = 0;
    }
    for (i = 0; i < sizeof(sp) / sizeof(ULONG); i++) {
        ((PULONG)&sp)[i] = 0;
    }
    sp.Psiv = rawPsiv;
    sp.RootHubPort = rootPort;
    sp.ContextEntries = 1;
    if (XhciBuildSlotContext(ctx, &sp) != XHCI_CTX_OK) {
        return 0xFFFFFFFFUL;
    }
    return (ctx[0] & XHCI_SLOT_SPEED_MASK) >> XHCI_SLOT_SPEED_SHIFT;
}

static void enum_to_reset(PXHCI_ENUM_PORT e)
{
    XHCI_ENUM_EVENT ev;
    XHCI_ENUM_ACTION a;

    ev.Ok = 1; ev.Speed = 0; ev.SlotId = 0; ev.Bytes = 0; ev.Value = 0;
    ev.Kind = XHCI_ENUM_EV_CONNECT;
    (VOID)XhciEnumStep(e, &ev, &a);
    ev.Kind = XHCI_ENUM_EV_DEBOUNCED;
    (VOID)XhciEnumStep(e, &ev, &a);
}

static void test_e460_port13(void)
{
    XHCI_PORT_MAP map;
    XHCI_ENUM_PORT e;
    XHCI_ENUM_EVENT ev;
    XHCI_ENUM_ACTION a;
    XHCI_LINK_PORT l;
    XHCI_LINK_ACTION la;
    ULONG psiv;
    ULONG cls;
    ULONG i;

    e460_map(&map);
    psiv = XHCI_PORTSC_GET_SPEED(0x00001203UL);
    CHECK_EQ(psiv, 4, "PORTSC 00001203 reports speed ID 4");
    CHECK_EQ(XhciLinkClassify(0x00001203UL), XHCI_LINK_ENABLED,
             "and a trained link");
    cls = XHCI_SPEED_UNKNOWN;
    CHECK_EQ(XhciPortSpeedClass(&map, 13, psiv, &cls), XHCI_CAPS_OK,
             "decoded through the E460's table");
    CHECK_EQ(cls, XHCI_SPEED_SUPER, "as SuperSpeed (the fallback)");

    XhciEnumReset(&e);
    enum_to_reset(&e);
    CHECK_EQ(e.State, XHCI_ENUM_RESET, "the machine resets the port");
    ev.Ok = 1; ev.SlotId = 0; ev.Bytes = 0; ev.Value = 0;
    ev.Kind = XHCI_ENUM_EV_RESET_DONE;
    ev.Speed = enum_speed_of(cls);
    (VOID)XhciEnumStep(&e, &ev, &a);
    CHECK_EQ(e.State, XHCI_ENUM_ENABLE_SLOT, "to Enable Slot");
    CHECK_EQ(a.Kind, XHCI_ENUM_ACT_ENABLE_SLOT, "Enable Slot is sent");
    CHECK_EQ(e.Mps0, 512, "EP0 at 512");
    ev.Kind = XHCI_ENUM_EV_COMMAND_DONE;
    ev.SlotId = 1;
    (VOID)XhciEnumStep(&e, &ev, &a);
    CHECK_EQ(a.Kind, XHCI_ENUM_ACT_ADDRESS, "Address Device");
    CHECK_EQ(a.Mps0, 512, "with EP0 at 512");
    CHECK_EQ(slot_speed(psiv, 13), 4,
             "the Slot Context carries the raw ID 4");

    /* The raw ID, not the machine's speed, even where the two differ: a
     * USB 2.0 table that reorders its IDs, High Speed at 1. */
    map.Protocols[0].Psi[0] = 0x01E00021UL;
    map.Protocols[0].Psi[2] = 0x000C0023UL;
    cls = XHCI_SPEED_UNKNOWN;
    (VOID)XhciPortSpeedClass(&map, 2, 1, &cls);
    CHECK_EQ(enum_speed_of(cls), XHCI_ENUM_SPEED_HIGH,
             "reordered: the machine is fed High Speed's 3");
    CHECK_EQ(slot_speed(1, 2), 1, "and the Slot Context the raw 1");

    /* What 2.1.1.0 did: the unknown class failed the speed, the retry
     * failed it again, and the port stayed Failed while the link sat in
     * U0 with no change bit - terminal, which 35.1 leaves as it is. */
    XhciEnumReset(&e);
    XhciLinkInit(&l);
    enum_to_reset(&e);
    for (i = 0; i <= XHCI_ENUM_RETRIES; i++) {
        ev.Kind = XHCI_ENUM_EV_RESET_DONE;
        ev.Ok = 1;
        ev.Speed = enum_speed_of(XHCI_SPEED_UNKNOWN);
        (VOID)XhciEnumStep(&e, &ev, &a);
        CHECK_EQ(e.State, XHCI_ENUM_FAILED, "an unknown speed fails");
        CHECK_EQ(e.FailCause, XHCI_ENUM_FAIL_SPEED, "on the speed");
        (VOID)XhciEnumRetry(&e, &a);
    }
    CHECK_EQ(e.State, XHCI_ENUM_FAILED, "and the retry spent, stays Failed");
    CHECK_EQ(XhciLinkPortFeed(&l, 1, 0x00001203UL, e.State, &la), 0,
             "and a trained link with no change bit feeds it nothing");
    CHECK_EQ(la.Kind, XHCI_LINK_ACT_NONE, "nor asks for a write");
}

int main(void)
{
    test_classify();
    test_service();
    test_reset_policy();
    test_power_and_hold_writes();
    test_hold_release();
    test_hold_kinds();
    test_port_feed();
    test_hold_portsc();
    test_e460_port13();

    printf("%d checks, %d failures\n", checks, failures);
    return failures;
}
