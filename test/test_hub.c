/*
 * test_hub.c - host vectors for the hub class's pure half (src\xhci_hub.c;
 * roadmap-hcd.md tasks 27-A.1 and 27-A.4; design record 13 sections 10.1 to
 * 10.3).
 *
 * The first vectors only, written with the code: the descriptor parse and
 * its refusals, the status-change bitmap, the port decision for each change
 * a port can report, the reset progress, the speed bits, the depth rule, the
 * multi-TT rule and the instance key. The fuller tables are 27-A.4's.
 */

#include <stdio.h>
#include "../src/xhci.h"
#include "../src/xhci_enum.h"
#include "../src/xhci_hub.h"
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
    CHECK_EQ(XhciHubPowerWaitMs(0), 20, "lower bound");
    CHECK_EQ(XhciHubPowerWaitMs(100), 100, "bPwrOn2PwrGood x 2");
    CHECK_EQ(XhciHubPowerWaitMs(510), 510, "inside the bounds");
}

int main(void)
{
    test_descriptor();
    test_bitmap();
    test_decide();
    test_reset();
    test_rules();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
