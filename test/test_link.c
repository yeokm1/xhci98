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
    CHECK_EQ(XhciLinkSameDevice(&held, NULL), 0, "NULL is no device");
    CHECK_EQ(XhciHoldBegin(NULL, 5, 1, &held), XHCI_HOLD_NONE, "NULL hold");
    CHECK_EQ(XhciHoldCompanionDisconnect(NULL), XHCI_HOLD_KEEP,
             "NULL hold keeps");
}

int main(void)
{
    test_classify();
    test_service();
    test_reset_policy();
    test_power_and_hold_writes();
    test_hold_release();
    test_hold_kinds();

    printf("%d checks, %d failures\n", checks, failures);
    return failures;
}
