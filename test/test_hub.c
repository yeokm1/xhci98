/*
 * test_hub.c - host vectors for the hub class's pure half (src\xhci_hub.c;
 * roadmap-hcd.md tasks 27-A.1 and 27-A.4; design record 13 sections 10.1 to
 * 10.3).
 *
 * The first vectors only, written with the code: the descriptor parse and
 * its refusals, the status-change bitmap, the port decision for each change
 * a port can report, the reset progress, the speed bits, the depth rule, the
 * multi-TT rule and the instance key; then 27-A.3's CLEAR_TT_BUFFER fields
 * and a departing subtree's release order. The fuller tables are 27-A.4's.
 */

#include <stdio.h>
#include "../src/xhci.h"
#include "../src/xhci_enum.h"
#include "../src/xhci_hub.h"
#include "../src/xhci_pipe.h"
#include "test_harness.h"

static void test_descriptor(void)
{
    /* QEMU's usb-hub shape: 8 ports, individual power switching. */
    UCHAR good[9] = { 9, 0x29, 8, 0x09, 0x00, 50, 0, 0x00, 0xFF };
    UCHAR hs[9] = { 9, 0x29, 4, 0x60, 0x00, 1, 100, 0x00, 0xFF };
    UCHAR wide[9] = { 9, 0x29, 20, 0, 0, 0, 0, 0, 0 };
    UCHAR bad[9];
    XHCI_HUB_DESC d;
    ULONG i;

    CHECK_EQ(XhciHubParseDescriptor(good, 9, &d), XHCI_HUB_OK, "parsed");
    CHECK_EQ(d.Ports, 8, "bNbrPorts");
    CHECK_EQ(d.Managed, 8, "all managed");
    CHECK_EQ(d.Characteristics, 0x0009, "wHubCharacteristics");
    CHECK_EQ(d.PowerGoodMs, 100, "bPwrOn2PwrGood x 2");
    CHECK_EQ(d.ThinkTime, 0, "TTT 0");

    CHECK_EQ(XhciHubParseDescriptor(hs, 9, &d), XHCI_HUB_OK, "HS parsed");
    CHECK_EQ(d.ThinkTime, 3, "TTT from bits 6:5");
    CHECK_EQ(d.ControllerCurrent, 100, "bHubContrCurrent");

    CHECK_EQ(XhciHubParseDescriptor(wide, 9, &d), XHCI_HUB_OK, "wide hub");
    CHECK_EQ(d.Managed, XHCI_HUB_MAX_PORTS, "managed ports capped");

    CHECK_EQ(XhciHubParseDescriptor(good, 6, &d), XHCI_HUB_MALFORMED,
             "shorter than the header");
    for (i = 0; i < 9; i++) {
        bad[i] = good[i];
    }
    bad[0] = 12;
    CHECK_EQ(XhciHubParseDescriptor(bad, 9, &d), XHCI_HUB_MALFORMED,
             "bLength past what arrived");
    bad[0] = 9;
    bad[1] = 0x00;
    CHECK_EQ(XhciHubParseDescriptor(bad, 9, &d), XHCI_HUB_MALFORMED,
             "another type byte");
    bad[1] = 0x29;
    bad[2] = 0;
    CHECK_EQ(XhciHubParseDescriptor(bad, 9, &d), XHCI_HUB_MALFORMED,
             "no ports");
    CHECK_EQ(XhciHubParseDescriptor(NULL, 9, &d), XHCI_HUB_BAD_PARAM,
             "NULL data");
}

static void test_bitmap(void)
{
    UCHAR r[2] = { 0x05, 0x01 };    /* hub, port 2, port 8 */

    CHECK_EQ(XhciHubStatusBytes(8), 2, "8 ports: 9 bits, 2 bytes");
    CHECK_EQ(XhciHubStatusBytes(7), 1, "7 ports: 8 bits, 1 byte");
    CHECK_EQ(XhciHubStatusBytes(255), XHCI_HUB_STATUS_MAX_BYTES, "capped");
    CHECK_EQ(XhciHubStatusBitmap(r, 2, 8), 0x105, "hub, 2 and 8");
    CHECK_EQ(XhciHubStatusBitmap(r, 1, 8), 0x005, "short report");
    CHECK_EQ(XhciHubStatusBitmap(r, 2, 4), 0x005, "past managed ignored");
    CHECK_EQ(XhciHubAllBits(4), 0x1F, "hub and four ports");
    {
        UCHAR w[2] = { 0x00, 0x80 };    /* port 15 alone */

        CHECK(XhciHubReportHas(w, 2, 15), "port 15 seen, unmanaged");
        CHECK_EQ(XhciHubStatusBitmap(w, 2, 14), 0, "and not acted on");
        CHECK(!XhciHubReportHas(w, 1, 15), "past the bytes that arrived");
        CHECK(!XhciHubReportHas(w, 2, 14), "port 14 clear");
        CHECK(!XhciHubReportHas(NULL, 2, 15), "NULL report");
    }
}

static void test_decide(void)
{
    XHCI_HUB_PORT_DECISION d;

    XhciHubPortDecide(XHCI_ENUM_EMPTY,
                      XHCI_HUB_PORT_POWER | XHCI_HUB_PORT_CONNECTION,
                      XHCI_HUB_C_PORT_CONNECTION, &d);
    CHECK(d.Connect && !d.Disconnect, "connect change on an empty port");
    CHECK_EQ(d.Clear, XHCI_HUB_C_PORT_CONNECTION, "cleared");

    XhciHubPortDecide(XHCI_ENUM_EMPTY,
                      XHCI_HUB_PORT_POWER | XHCI_HUB_PORT_CONNECTION, 0, &d);
    CHECK(d.Connect && d.Clear == 0, "present at power-on, no change");

    XhciHubPortDecide(XHCI_ENUM_BOUND, XHCI_HUB_PORT_POWER,
                      XHCI_HUB_C_PORT_CONNECTION, &d);
    CHECK(d.Disconnect && !d.Connect, "unplug");

    XhciHubPortDecide(XHCI_ENUM_BOUND,
                      XHCI_HUB_PORT_POWER | XHCI_HUB_PORT_CONNECTION |
                          XHCI_HUB_PORT_ENABLE,
                      XHCI_HUB_C_PORT_CONNECTION, &d);
    CHECK(d.Disconnect && d.Connect, "a swap between polls");

    XhciHubPortDecide(XHCI_ENUM_BOUND,
                      XHCI_HUB_PORT_POWER | XHCI_HUB_PORT_CONNECTION,
                      XHCI_HUB_C_PORT_ENABLE, &d);
    CHECK(d.Disconnect && d.Connect, "disabled by the hub: enumerate again");

    XhciHubPortDecide(XHCI_ENUM_BOUND,
                      XHCI_HUB_PORT_POWER | XHCI_HUB_PORT_CONNECTION |
                          XHCI_HUB_PORT_ENABLE,
                      0, &d);
    CHECK(!d.Disconnect && !d.Connect, "nothing changed");

    XhciHubPortDecide(XHCI_ENUM_GONE, 0, 0, &d);
    CHECK(!d.Disconnect && !d.Connect, "an empty Gone port asks nothing");

    XhciHubPortDecide(XHCI_ENUM_BOUND, XHCI_HUB_PORT_CONNECTION,
                      XHCI_HUB_C_PORT_OVER_CURRENT, &d);
    CHECK(d.OverCurrent && d.Repower, "over-current, power lost");

    CHECK_EQ(XhciHubClearSelector(XHCI_HUB_C_PORT_RESET),
             XHCI_HUB_FEAT_C_PORT_RESET, "C_PORT_RESET 20");
    CHECK_EQ(XhciHubClearSelector(0x20), 0, "not a change bit");
}

static void test_reset(void)
{
    ULONG c;
    ULONG e;

    c = XHCI_HUB_PORT_CONNECTION | XHCI_HUB_PORT_POWER;
    e = c | XHCI_HUB_PORT_ENABLE;
    CHECK_EQ(XhciHubResetProgress(c | XHCI_HUB_PORT_RESET, 0),
             XHCI_HUB_RESET_PENDING, "still resetting");
    CHECK_EQ(XhciHubResetProgress(e, XHCI_HUB_C_PORT_RESET),
             XHCI_HUB_RESET_ENABLED, "done, enabled");
    CHECK_EQ(XhciHubResetProgress(c, XHCI_HUB_C_PORT_RESET),
             XHCI_HUB_RESET_FAILED, "done, not enabled");
    CHECK_EQ(XhciHubResetProgress(XHCI_HUB_PORT_POWER, 0),
             XHCI_HUB_RESET_FAILED, "device left");
    CHECK_EQ(XhciHubResetProgress(e, 0), XHCI_HUB_RESET_PENDING,
             "enabled with no fresh change: not this reset's end");
    CHECK_EQ(XhciHubResetProgress(e | XHCI_HUB_PORT_RESET,
                                  XHCI_HUB_C_PORT_RESET),
             XHCI_HUB_RESET_PENDING, "a change with the reset bit still set");
    CHECK_EQ(XhciHubPortSpeedClass(e | XHCI_HUB_PORT_HIGH_SPEED),
             XHCI_SPEED_HIGH, "HS");
    CHECK_EQ(XhciHubPortSpeedClass(e | XHCI_HUB_PORT_LOW_SPEED),
             XHCI_SPEED_LOW, "LS");
    CHECK_EQ(XhciHubPortSpeedClass(e), XHCI_SPEED_FULL, "neither: FS");
    CHECK_EQ(XhciHubEnumSpeed(XHCI_SPEED_FULL), XHCI_ENUM_SPEED_FULL,
             "the machine's FS");
    CHECK_EQ(XhciHubEnumSpeed(XHCI_SPEED_UNKNOWN), 0, "no speed");
}

static void test_rules(void)
{
    CHECK(XhciHubTierServable(0), "a hub on a root port");
    CHECK(XhciHubTierServable(4), "the fifth hub: children at tier 5");
    CHECK(!XhciHubTierServable(5), "a sixth hub's children are unroutable");
    CHECK(XhciHubWantsMultiTt(XHCI_SPEED_HIGH, 2, 1), "multi-TT");
    CHECK(!XhciHubWantsMultiTt(XHCI_SPEED_HIGH, 1, 1), "single-TT");
    CHECK(!XhciHubWantsMultiTt(XHCI_SPEED_FULL, 2, 1), "FS hub");
    CHECK(!XhciHubWantsMultiTt(XHCI_SPEED_HIGH, 2, 0), "no alternate 1");
    CHECK_EQ(XhciHubInstanceKey(3, 0), 3, "root port device unchanged");
    CHECK_EQ(XhciHubInstanceKey(1, 0x21), 0x2101, "behind two hubs");
    CHECK_EQ(XhciHubPdoAddress(3, 0, 0), 3, "address: root port");
    CHECK_EQ(XhciHubPdoAddress(3, 0, 1), 3, "address: root port, flag moot");
    CHECK_EQ(XhciHubPdoAddress(3, 0x1, 1), 1,
             "address: port 1 of a hub on root port 3, not 0x103");
    CHECK_EQ(XhciHubPdoAddress(1, 0x2E, 1), 2,
             "address: two tiers, the nearer hub's port");
    CHECK_EQ(XhciHubPdoAddress(2, 0x3E4, 1), 3, "address: three tiers");
    CHECK_EQ(XhciHubPdoAddress(1, 0xE4321, 1), 0xE, "address: five tiers");
    CHECK_EQ(XhciHubPdoAddress(3, 0x1, 0), 0x103,
             "address: own hub has no PDO, no connection index: instance key");
    CHECK_EQ(XhciHubPdoAddress(1, 0x21, 0), 0x2101,
             "address: re-parented two tiers up: instance key");
    CHECK_EQ(XhciHubPdoAddress(0x103, 0x100001, 1), 1,
             "address: only the route's five tiers and the root port's byte");
    CHECK_EQ(XhciHubPowerWaitMs(0), 20, "lower bound");
    CHECK_EQ(XhciHubPowerWaitMs(100), 100, "bPwrOn2PwrGood x 2");
    CHECK_EQ(XhciHubPowerWaitMs(510), 510, "inside the bounds");
}

/* CLEAR_TT_BUFFER's fields (27-A.3; xhci_hub.h): the packing, the TT port,
 * and the endpoint types that clear nothing. */
static void test_clear_tt(void)
{
    ULONG v;

    CHECK(XhciHubClearTtValue(5, 2, XHCI_HUB_TT_EP_BULK, 1, &v),
          "bulk IN clears");
    CHECK_EQ(v, 0x8000UL | (2UL << 11) | (5UL << 4) | 2UL,
             "EP 2 IN, address 5, bulk");
    CHECK(XhciHubClearTtValue(127, 15, XHCI_HUB_TT_EP_BULK, 0, &v),
          "the widest fields");
    CHECK_EQ(v, (2UL << 11) | (127UL << 4) | 15UL, "OUT has bit 15 clear");
    CHECK(XhciHubClearTtValue(0, 0, XHCI_HUB_TT_EP_CONTROL, 0, &v),
          "the default pipe at address 0 (Address Device failed)");
    CHECK_EQ(v, 0, "all zero");
    CHECK(!XhciHubClearTtValue(5, 1, 3, 1, &v), "interrupt clears nothing");
    CHECK(!XhciHubClearTtValue(5, 1, 1, 1, &v), "isochronous neither");
    CHECK(!XhciHubClearTtValue(128, 1, XHCI_HUB_TT_EP_BULK, 0, &v),
          "an address past 127");
    CHECK(!XhciHubClearTtValue(5, 16, XHCI_HUB_TT_EP_BULK, 0, &v),
          "an endpoint past 15");
    CHECK(!XhciHubClearTtValue(5, 1, XHCI_HUB_TT_EP_BULK, 0, NULL),
          "NULL out");
    CHECK_EQ(XhciHubClearTtPort(1, 3), 3, "multi-TT: the device's TT port");
    CHECK_EQ(XhciHubClearTtPort(0, 3), 1, "single TT: 1");
}

/* The release order of a departing subtree (27-A.3; design record 13 section
 * 10.5 step 4): each hub after every hub below it, nothing outside the
 * subtree, loops and detached objects left out. */
static ULONG position(const ULONG *order, ULONG n, ULONG hub)
{
    ULONG i;

    for (i = 0; i < n; i++) {
        if (order[i] == hub) {
            return i;
        }
    }
    return 0xFFFFFFFFUL;
}

static void test_release_order(void)
{
    /*
     * root - 0 - 1 - 3 - 4          four below hub 0, three deep
     *          + 2                  a second branch, on hub 0
     * root - 5 - 6                  another root port's chain
     * 7 detached (departing), 8 loops with 9.
     */
    ULONG parent[10];
    ULONG order[10];
    ULONG n;

    parent[0] = XHCI_HUB_NO_PARENT;
    parent[1] = 0;
    parent[2] = 0;
    parent[3] = 1;
    parent[4] = 3;
    parent[5] = XHCI_HUB_NO_PARENT;
    parent[6] = 5;
    parent[7] = XHCI_HUB_DETACHED;
    parent[8] = 9;
    parent[9] = 8;

    n = XhciHubReleaseOrder(parent, 10, 0, order);
    CHECK_EQ(n, 5, "hub 0 and the four below it");
    CHECK_EQ(order[0], 4, "the deepest first");
    CHECK_EQ(order[n - 1], 0, "the departing hub last");
    CHECK(position(order, n, 4) < position(order, n, 3) &&
              position(order, n, 3) < position(order, n, 1) &&
              position(order, n, 1) < position(order, n, 0) &&
              position(order, n, 2) < position(order, n, 0),
          "every hub after each one below it");
    CHECK_EQ(position(order, n, 5), 0xFFFFFFFFUL, "another chain untouched");
    CHECK_EQ(position(order, n, 7), 0xFFFFFFFFUL, "a departing one too");

    n = XhciHubReleaseOrder(parent, 10, 3, order);
    CHECK_EQ(n, 2, "a mid-chain hub: itself and the one below");
    CHECK(order[0] == 4 && order[1] == 3, "leaf first");

    n = XhciHubReleaseOrder(parent, 10, 6, order);
    CHECK(n == 1 && order[0] == 6, "a hub with nothing below");

    n = XhciHubReleaseOrder(parent, 10, 8, order);
    CHECK(n == 2 && order[1] == 8, "a loop through the top ends there");
    n = XhciHubReleaseOrder(parent, 10, 0, order);
    CHECK_EQ(position(order, n, 9), 0xFFFFFFFFUL,
             "a loop elsewhere is left out");

    CHECK_EQ(XhciHubReleaseOrder(parent, 10, 7, order), 0,
             "a detached top gives nothing");
    CHECK_EQ(XhciHubReleaseOrder(parent, 10, 10, order), 0,
             "a top out of range neither");
    CHECK_EQ(XhciHubReleaseOrder(NULL, 10, 0, order), 0, "NULL parent");
}

/* Suspend and resume, handled and never initiated (the owner's decision of
 * 2026-10-04; xhci_hub.h): a port the hub reports suspended is resumed, a
 * finished resume or a remote wake needs no re-enumeration, and a
 * suspended port is resumed before its reset. */
static void test_suspend(void)
{
    XHCI_HUB_PORT_DECISION d;
    ULONG on;

    on = XHCI_HUB_PORT_POWER | XHCI_HUB_PORT_CONNECTION |
         XHCI_HUB_PORT_ENABLE;

    XhciHubPortDecide(XHCI_ENUM_BOUND, on, XHCI_HUB_C_PORT_SUSPEND, &d);
    CHECK(d.Suspended && !d.Resume, "a remote wake: resume finished");
    CHECK(!d.Disconnect && !d.Connect, "and no re-enumeration");
    CHECK_EQ(d.Clear, XHCI_HUB_C_PORT_SUSPEND, "its change cleared");

    XhciHubPortDecide(XHCI_ENUM_BOUND, on | XHCI_HUB_PORT_SUSPEND, 0, &d);
    CHECK(d.Resume, "a bound device reported suspended is resumed");
    CHECK(!d.Disconnect && !d.Connect, "in place");

    XhciHubPortDecide(XHCI_ENUM_EMPTY,
                      XHCI_HUB_PORT_POWER | XHCI_HUB_PORT_CONNECTION |
                          XHCI_HUB_PORT_SUSPEND,
                      0, &d);
    CHECK(d.Resume && d.Connect, "an empty port found suspended: resume, "
                                 "then enumerate");

    XhciHubPortDecide(XHCI_ENUM_BOUND,
                      XHCI_HUB_PORT_POWER | XHCI_HUB_PORT_SUSPEND, 0, &d);
    CHECK(!d.Resume && d.Disconnect, "a disconnected port is not resumed");

    CHECK(XhciHubResumeBeforeReset(on | XHCI_HUB_PORT_SUSPEND),
          "resume before a reset");
    CHECK(!XhciHubResumeBeforeReset(on), "not when awake");
    CHECK(!XhciHubResumeBeforeReset(XHCI_HUB_PORT_SUSPEND),
          "not when nothing is connected");

    CHECK_EQ(XhciHubResumeProgress(on | XHCI_HUB_PORT_SUSPEND),
             XHCI_HUB_RESUME_PENDING, "still resuming");
    CHECK_EQ(XhciHubResumeProgress(on), XHCI_HUB_RESUME_DONE, "resumed");
    CHECK_EQ(XhciHubResumeProgress(XHCI_HUB_PORT_POWER |
                                   XHCI_HUB_PORT_CONNECTION),
             XHCI_HUB_RESUME_DISABLED,
             "suspend and enable both cleared: a port error, not a resume");
    CHECK_EQ(XhciHubResumeProgress(XHCI_HUB_PORT_POWER),
             XHCI_HUB_RESUME_GONE, "the device left");
    CHECK(XHCI_HUB_RESUME_FIRST_MS >= 20UL, "TDRSMDN at least 20 ms");
    CHECK_EQ(XHCI_HUB_RESUME_RECOVERY_MS, 10UL, "TRSMRCY 10 ms");
}

/* What a resume's outcome makes of the port (Codex review of the Phase 27
 * integration, round 4): kept for a retry, given up after its tries,
 * re-enumerated after a port error, torn down when the device left. */
static void test_resume_outcome(void)
{
    XHCI_HUB_PORT_DECISION d;
    ULONG on;
    ULONG tries;

    on = XHCI_HUB_PORT_POWER | XHCI_HUB_PORT_CONNECTION |
         XHCI_HUB_PORT_ENABLE | XHCI_HUB_PORT_SUSPEND;
    tries = 0;

    /* A device replaced during the resume (Codex review of the Phase 28-31
     * merge, finding 1): its last reading DONE with C_PORT_CONNECTION is
     * re-enumerated, never let go; at SuperSpeed the bit is the same. */
    CHECK_EQ(XhciHubResumeSettle(XHCI_HUB_RESUME_DONE,
                                 XHCI_HUB_C_PORT_CONNECTION),
             XHCI_HUB_RESUME_DISABLED, "resumed, but a connect change: afresh");
    CHECK_EQ(XhciHubResumeSettle(XHCI_HUB_RESUME_DONE,
                                 XHCI_HUB_C_PORT_SUSPEND),
             XHCI_HUB_RESUME_DONE, "resumed with C_PORT_SUSPEND: done");
    CHECK_EQ(XhciHubResumeSettle(XHCI_HUB_RESUME_DONE, 0x0040UL),
             XHCI_HUB_RESUME_DONE, "SuperSpeed C_PORT_LINK_STATE: done");
    CHECK_EQ(XhciHubResumeSettle(XHCI_HUB_RESUME_DONE, 0x0041UL),
             XHCI_HUB_RESUME_DISABLED,
             "SuperSpeed U0 with C_PORT_CONNECTION: afresh");
    CHECK_EQ(XhciHubResumeSettle(XHCI_HUB_RESUME_GONE,
                                 XHCI_HUB_C_PORT_CONNECTION),
             XHCI_HUB_RESUME_GONE, "gone stays gone");
    CHECK_EQ(XhciHubResumeSettle(XHCI_HUB_RESUME_STUCK,
                                 XHCI_HUB_C_PORT_CONNECTION),
             XHCI_HUB_RESUME_STUCK, "stuck stays stuck");
    XhciHubPortDecide(XHCI_ENUM_BOUND, on, 0, &d);
    XhciHubResumeOutcome(XHCI_ENUM_BOUND,
                         XhciHubResumeSettle(XHCI_HUB_RESUME_DONE,
                                             XHCI_HUB_C_PORT_CONNECTION),
                         1, &tries, &d);
    CHECK(d.Disconnect && d.Connect && !d.Retry,
          "a held device replaced mid-resume is torn down and re-enumerated");

    XhciHubPortDecide(XHCI_ENUM_BOUND, on, 0, &d);
    XhciHubResumeOutcome(XHCI_ENUM_BOUND, XHCI_HUB_RESUME_DONE, 1, &tries, &d);
    CHECK(!d.Disconnect && !d.Connect && !d.Retry && !d.GaveUp,
          "resumed: the bound device stays as it is");

    XhciHubPortDecide(XHCI_ENUM_EMPTY, on & ~XHCI_HUB_PORT_ENABLE, 0, &d);
    XhciHubResumeOutcome(XHCI_ENUM_EMPTY, XHCI_HUB_RESUME_DONE, 0, &tries, &d);
    CHECK(d.Connect, "resumed: an empty port goes on to enumerate");

    XhciHubPortDecide(XHCI_ENUM_BOUND, on, 0, &d);
    XhciHubResumeOutcome(XHCI_ENUM_EMPTY, XHCI_HUB_RESUME_STUCK, 0,
                         &tries, &d);
    CHECK(d.Retry && !d.Disconnect && !d.Connect,
          "nothing held, first failure: retry");
    CHECK_EQ(tries, 1, "counted");
    XhciHubResumeOutcome(XHCI_ENUM_EMPTY, XHCI_HUB_RESUME_STUCK, 0,
                         &tries, &d);
    CHECK(d.Retry && !d.GaveUp, "second failure: retry");
    XhciHubResumeOutcome(XHCI_ENUM_EMPTY, XHCI_HUB_RESUME_STUCK, 0,
                         &tries, &d);
    CHECK(d.GaveUp && !d.Retry, "third failure: given up");
    CHECK(d.Connect, "and the port enumerated afresh");
    CHECK_EQ(tries, 0, "the count restarts");

    /* A device held for the resume is never let go on a failure: torn
     * down at once (round 5, finding 1). */
    tries = 1;
    XhciHubPortDecide(XHCI_ENUM_BOUND, on, 0, &d);
    XhciHubResumeOutcome(XHCI_ENUM_BOUND, XHCI_HUB_RESUME_STUCK, 1, &tries,
                         &d);
    CHECK(d.GaveUp && !d.Retry, "held and stuck: no retry");
    CHECK(d.Disconnect && d.Connect, "held and stuck: torn down, then "
                                     "enumerated afresh");
    CHECK_EQ(tries, 0, "and the count restarts");

    tries = 2;
    XhciHubPortDecide(XHCI_ENUM_BOUND, on, 0, &d);
    XhciHubResumeOutcome(XHCI_ENUM_BOUND, XHCI_HUB_RESUME_DONE, 1, &tries, &d);
    CHECK_EQ(tries, 0, "a resume that finishes restarts the count");

    XhciHubPortDecide(XHCI_ENUM_BOUND, on, 0, &d);
    XhciHubResumeOutcome(XHCI_ENUM_BOUND, XHCI_HUB_RESUME_DISABLED, 1, &tries,
                         &d);
    CHECK(d.Disconnect && d.Connect, "a port error: enumerate afresh");

    XhciHubPortDecide(XHCI_ENUM_BOUND, on, 0, &d);
    XhciHubResumeOutcome(XHCI_ENUM_BOUND, XHCI_HUB_RESUME_GONE, 1, &tries, &d);
    CHECK(d.Disconnect && !d.Connect, "the device left: torn down");

    XhciHubPortDecide(XHCI_ENUM_EMPTY, on, 0, &d);
    XhciHubResumeOutcome(XHCI_ENUM_EMPTY, XHCI_HUB_RESUME_GONE, 0, &tries, &d);
    CHECK(!d.Disconnect && !d.Connect, "nothing held, nothing to tear down");
}

/*
 * The Low-Speed mouse at bInterval 10 polled every 8 ms (27-V.1's clause,
 * moved to 28-E.1's bench by the owner's decision of 2026-10-04): its
 * interrupt endpoint is programmed from the device's own speed class - on a
 * root port, and behind QEMU's Full-Speed hub, whose port reports it Low
 * Speed - to xHCI Interval 6, 2^6 x 125 us = 8 ms (xHCI 6.2.3.6 for Full
 * and Low Speed: bInterval ms rounded down to a power of two in 125 us
 * frames). The hub's Full Speed is not what the endpoint is given: a bulk
 * endpoint, which Low Speed has not, is refused on that device.
 */
static void test_low_speed_mouse(void)
{
    UCHAR ep[7] = { 7, 5, 0x81, 0x03, 8, 0, 10 };
    UCHAR bulk[7] = { 7, 5, 0x82, 0x02, 8, 0, 0 };
    XHCI_PIPE_EP p;
    ULONG speed;

    /* On a root port: the class PORTSC decoded, Low Speed. */
    speed = XhciPipeSpeedFromClass(XHCI_SPEED_LOW);
    CHECK_EQ(speed, XHCI_PIPE_SPEED_LOW, "root port: the endpoint is LS");
    CHECK_EQ(XhciPipeEndpointParams(ep, speed, &p), XHCI_PIPE_OK,
             "root port: LS interrupt IN accepted");
    CHECK_EQ(p.Interval, 6, "root port: bInterval 10 -> Interval 6");
    CHECK_EQ((1UL << p.Interval) * 125UL, 8000UL, "root port: every 8 ms");

    /* Behind a Full-Speed hub: the hub's port reports Low Speed. */
    speed = XhciPipeSpeedFromClass(
        XhciHubPortSpeedClass(XHCI_HUB_PORT_CONNECTION |
                              XHCI_HUB_PORT_ENABLE | XHCI_HUB_PORT_POWER |
                              XHCI_HUB_PORT_LOW_SPEED));
    CHECK_EQ(speed, XHCI_PIPE_SPEED_LOW, "behind the FS hub: still LS");
    CHECK_EQ(XhciPipeEndpointParams(ep, speed, &p), XHCI_PIPE_OK,
             "behind the FS hub: LS interrupt IN accepted");
    CHECK_EQ(p.Interval, 6, "behind the FS hub: Interval 6");
    CHECK_EQ((1UL << p.Interval) * 125UL, 8000UL,
             "behind the FS hub: every 8 ms");
    CHECK(XhciPipeEndpointParams(bulk, speed, &p) != XHCI_PIPE_OK,
          "the device's speed, not the hub's: LS bulk refused");
    CHECK_EQ(XhciPipeSpeedFromClass(XHCI_SPEED_UNKNOWN), 0,
             "an unknown class gives no speed");
}


/* ---- task 33.4: a hub as a devnode (design record 13 section 10.11) ---- */

static int same_text(const char *got, ULONG used, const char *want,
                     ULONG wantLen)
{
    ULONG i;

    if (used != wantLen) {
        return 0;
    }
    for (i = 0; i < used; i++) {
        if (got[i] != want[i]) {
            return 0;
        }
    }
    return 1;
}

static void test_hub_pdo_ids(void)
{
    /* A VIA VL813-style USB 2.0 half 2109:2813 rev 0x9093, and its
     * SuperSpeed half 2109:0813 rev 0x9093. */
    UCHAR hs[18] = { 18, 1, 0x00, 0x02, 9, 0, 1, 64,
                     0x09, 0x21, 0x13, 0x28, 0x93, 0x90, 0, 0, 0, 1 };
    UCHAR ss[18] = { 18, 1, 0x00, 0x03, 9, 0, 3, 9,
                     0x09, 0x21, 0x13, 0x08, 0x93, 0x90, 0, 0, 0, 1 };
    static const char devHs[] = "XHCI98\\HUB&VID_2109&PID_2813";
    static const char hwHs[] = "XHCI98\\HUB&VID_2109&PID_2813&REV_9093\0"
                               "XHCI98\\HUB&VID_2109&PID_2813\0"
                               "XHCI98\\HUB\0";
    static const char devSs[] = "XHCI98\\HUB30&VID_2109&PID_0813";
    static const char hwSs[] = "XHCI98\\HUB30&VID_2109&PID_0813&REV_9093\0"
                               "XHCI98\\HUB30&VID_2109&PID_0813\0"
                               "XHCI98\\HUB30\0";
    char out[160];
    ULONG used;
    ULONG i;

    CHECK_EQ(XhciHubPdoId(hs, 0, 0, XHCI_HUBPDO_ID_DEVICE, out, sizeof(out),
                          &used), XHCI_HUBPDO_OK, "hub device id");
    CHECK(same_text(out, used, devHs, sizeof(devHs)), "hub device id text");
    CHECK_EQ(XhciHubPdoId(hs, 0, 0, XHCI_HUBPDO_ID_HARDWARE, out,
                          sizeof(out), &used), XHCI_HUBPDO_OK, "hub hw ids");
    CHECK(same_text(out, used, hwHs, sizeof(hwHs)),
          "hub hardware ids: REV, VID/PID, bare, double NUL");
    CHECK_EQ(XhciHubPdoId(ss, 1, 0, XHCI_HUBPDO_ID_DEVICE, out, sizeof(out),
                          &used), XHCI_HUBPDO_OK, "SS hub device id");
    CHECK(same_text(out, used, devSs, sizeof(devSs)), "SS device id text");
    CHECK_EQ(XhciHubPdoId(ss, 1, 0, XHCI_HUBPDO_ID_HARDWARE, out,
                          sizeof(out), &used), XHCI_HUBPDO_OK, "SS hw ids");
    CHECK(same_text(out, used, hwSs, sizeof(hwSs)), "SS hardware ids");

    /* Nothing under the USB\ enumerator, ever: no OS hub INF may match. */
    for (i = 0; i + 4 <= used; i++) {
        CHECK(!(out[i] == 'U' && out[i + 1] == 'S' && out[i + 2] == 'B' &&
                out[i + 3] == '\\'),
              "no USB\\ id on a hub");
    }

    CHECK_EQ(XhciHubPdoId(hs, 0, 0x0102UL, XHCI_HUBPDO_ID_INSTANCE, out,
                          sizeof(out), &used), XHCI_HUBPDO_OK, "instance");
    CHECK(same_text(out, used, "258", 4), "instance key in decimal");
    CHECK_EQ(XhciHubPdoId(hs, 0, 0, XHCI_HUBPDO_ID_COMPATIBLE, out,
                          sizeof(out), &used), XHCI_HUBPDO_BAD_PARAM,
             "no compatible id");
    CHECK_EQ(used, 0, "no compatible id: nothing used");

    /* The two-call size answer: too small reports the whole length. */
    CHECK_EQ(XhciHubPdoId(hs, 0, 0, XHCI_HUBPDO_ID_HARDWARE, out, 10, &used),
             XHCI_HUBPDO_TOO_SMALL, "short buffer");
    CHECK_EQ(used, sizeof(hwHs), "short buffer: whole length");
    CHECK_EQ(XhciHubPdoId(hs, 0, 0, XHCI_HUBPDO_ID_HARDWARE, NULL, 0, &used),
             XHCI_HUBPDO_TOO_SMALL, "size query");
    CHECK_EQ(XhciHubPdoId(NULL, 0, 0, XHCI_HUBPDO_ID_DEVICE, out, 10, &used),
             XHCI_HUBPDO_BAD_PARAM, "no descriptor");
    CHECK_EQ(XhciHubPdoId(hs, 0, 0, 9, out, 10, &used),
             XHCI_HUBPDO_BAD_PARAM, "unknown query");
}

static void test_hub_presented_parent(void)
{
    /* Hub 0 on a root port (serial 40), hub 1 on hub 0 with no PDO, hub 2
     * on hub 1 (serial 42), hub 3 on a root port with no PDO, hub 4 a
     * loop with hub 5, hub 6 detached. */
    ULONG parent[7] = { XHCI_HUB_NO_PARENT, 0, 1, XHCI_HUB_NO_PARENT,
                        5, 4, XHCI_HUB_DETACHED };
    ULONG serial[7] = { 40, 0, 42, 0, 0, 0, 0 };

    CHECK_EQ(XhciHubPresentedParent(parent, serial, 7, XHCI_HUB_NO_PARENT), 0,
             "root port: the root hub");
    CHECK_EQ(XhciHubPresentedParent(parent, serial, 7, 0), 40,
             "behind hub 0: hub 0");
    CHECK_EQ(XhciHubPresentedParent(parent, serial, 7, 2), 42,
             "two-tier: the nearer hub");
    CHECK_EQ(XhciHubPresentedParent(parent, serial, 7, 1), 40,
             "behind a hub with no PDO: the next one up");
    CHECK_EQ(XhciHubPresentedParent(parent, serial, 7, 3), 0,
             "no PDO anywhere above: the root hub");
    CHECK_EQ(XhciHubPresentedParent(parent, serial, 7, 4), 0,
             "a loop ends at the root hub");
    CHECK_EQ(XhciHubPresentedParent(parent, serial, 7, 6), 0,
             "detached: the root hub");
    CHECK_EQ(XhciHubPresentedParent(NULL, serial, 7, 0), 0, "no table");
}

static void test_hub_port_location(void)
{
    /* hcd.h: XHCI_MAX_ROOT_PORTS root ports, then 14 locations a hub. */
    CHECK_EQ(XhciHubPortLocation(255, 14, 0, 1), 256, "hub 0 port 1");
    CHECK_EQ(XhciHubPortLocation(255, 14, 2, 14), 255 + 28 + 14,
             "hub 2 port 14");
    CHECK_EQ(XhciHubPortLocation(255, 14, 0, 0), 0, "port 0");
    CHECK_EQ(XhciHubPortLocation(255, 14, 0, 15), 0, "past the hub");
}

static void test_hub_node_info(void)
{
    UCHAR good[9] = { 9, 0x29, 4, 0x89, 0x00, 50, 100, 0x00, 0xFF };
    XHCI_HUB_DESC d;
    UCHAR out[XHCI_HUB_NODE_INFO_BYTES];
    ULONG i;
    ULONG zero;

    CHECK_EQ(XhciHubParseDescriptor(good, 9, &d), XHCI_HUB_OK, "parsed");
    for (i = 0; i < sizeof(out); i++) {
        out[i] = 0xAA;
    }
    XhciHubNodeInfo(&d, 1, out);
    CHECK_EQ(out[0] | out[1] | out[2] | out[3], 0, "NodeType UsbHub");
    CHECK_EQ(out[4], 9, "bDescriptorLength for 4 ports");
    CHECK_EQ(out[5], 0x29, "hub descriptor type");
    CHECK_EQ(out[6], 4, "bNumberOfPorts");
    CHECK_EQ(out[7], 0x89, "wHubCharacteristics low");
    CHECK_EQ(out[8], 0x00, "wHubCharacteristics high");
    CHECK_EQ(out[9], 50, "bPowerOnToPowerGood, 2 ms units");
    CHECK_EQ(out[10], 100, "bHubControlCurrent");
    zero = 0;
    for (i = 11; i < 75; i++) {
        zero |= out[i];
    }
    CHECK_EQ(zero, 0, "masks clear");
    CHECK_EQ(out[75], 1, "HubIsBusPowered");

    d.Ports = 14;
    XhciHubNodeInfo(&d, 0, out);
    CHECK_EQ(out[4], 11, "bDescriptorLength for 14 ports");
    CHECK_EQ(out[75], 0, "self-powered");

    XhciHubNodeInfo(NULL, 0, out);
    CHECK_EQ(out[6], 0, "an undescribed hub: no ports");
    CHECK_EQ(out[4], 9, "an undescribed hub: the shortest descriptor");
}

static void test_hub_caps_ex(void)
{
    CHECK_EQ(XhciHubCapsEx(XHCI_SPEED_HIGH, 0, 0), 0x3, "single-TT HS");
    CHECK_EQ(XhciHubCapsEx(XHCI_SPEED_HIGH, 1, 0), 0x7, "MTT capable");
    CHECK_EQ(XhciHubCapsEx(XHCI_SPEED_HIGH, 1, 1), 0xF, "MTT on");
    CHECK_EQ(XhciHubCapsEx(XHCI_SPEED_FULL, 1, 1), 0, "a FS hub");
    CHECK_EQ(XhciHubCapsEx(XHCI_SPEED_SUPER, 0, 0), 0,
             "a SuperSpeed half: never HubIsRoot");
}
int main(void)
{
    test_hub_pdo_ids();
    test_hub_presented_parent();
    test_hub_port_location();
    test_hub_node_info();
    test_hub_caps_ex();
    test_suspend();
    test_resume_outcome();
    test_low_speed_mouse();
    test_descriptor();
    test_bitmap();
    test_decide();
    test_reset();
    test_rules();
    test_clear_tt();
    test_release_order();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
