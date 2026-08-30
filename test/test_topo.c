/*
 * test_topo.c - the hub topology graph (src/xhci_topo.c), roadmap task 7b-A.1.
 *
 * Pure vectors over XHCI_TOPOLOGY: setup packets are hand-built byte for byte
 * from the values the batch 7b-V0 QEMU trace measured on the wire
 * (vm\win2k-qemu-trace.batch7bv0-run1.log - `req 0xa006, value 0, length 71`,
 * `req 0x2303, value 4/8`, `req 0xa300, value 0, length 4`), so the suite
 * exercises the requests the shipping hub drivers actually send, not the ones
 * the hub-class specification says they should.
 *
 * The route-string vectors are hand-computed nibbles checked against numbers
 * typed out here, never recomputed through the same shift the code uses - the
 * test_ctx.c rule. The nibble order itself is corroborated against two
 * independent implementations (docs/usb-xhci-info/xhci-data-structures.md, "Route String
 * tier order"), because the defining document (USB3 section 8.9) is not in
 * docs/references.
 *
 * The claim identity at the end is task 7b-A.1.0's row-set net applied here:
 * every XhciTopoClaimChild call increments exactly one of
 * Claims/ClaimsUnarmed/ClaimsUnusable, so the three must sum to the calls made
 * - enforced by routing every claim in this file through one wrapper, so a
 * vector written later cannot opt out.
 *
 * Build and run:  test\run-host-tests.cmd
 * Exit code = number of failed checks (0 = pass).
 *
 * C89, no framework.
 */

#include <stdio.h>
#include "../src/xhci.h"
#include "../src/xhci_usbport.h"
#include "../src/xhci_topo.h"

static int failures;
static int checks;

#define CHECK(cond, what) check_impl((cond) ? 1 : 0, (what), __LINE__)

static void check_impl(int cond, const char *what, int line)
{
    checks++;
    if (!cond) {
        failures++;
        printf("FAIL %s:%d: %s\n", "test_topo.c", line, what);
    }
}

#define CHECK_EQ(got, want, what) \
    check_eq_impl((unsigned long)(got), (unsigned long)(want), (what), __LINE__)

static void check_eq_impl(unsigned long got, unsigned long want,
                          const char *what, int line)
{
    checks++;
    if (got != want) {
        failures++;
        printf("FAIL %s:%d: %s (got 0x%08lX, want 0x%08lX)\n",
               "test_topo.c", line, what, got, want);
    }
}

/*
 * A node's fields are read through this, never off a raw pointer.
 *
 * The reason is a mutation: a change that stops the graph learning anything
 * leaves `XhciTopoFind` answering NULL, and a vector that then dereferences it
 * **faults** instead of failing - which a sweep reads as "no result line" and a
 * reader reads as a broken harness. Every field check must be able to report a
 * wrong value, including the wrong value "there is no node".
 */
static const XHCI_TOPO_NODE emptyNode;

static const XHCI_TOPO_NODE *nodeOrEmpty(const XHCI_TOPO_NODE *node)
{
    return (node != NULL) ? node : &emptyNode;
}

/* ------------------------------------------------------------------ */
/* Wire helpers                                                        */
/* ------------------------------------------------------------------ */

static XHCI_SETUP_PACKET setupOf(UCHAR bmRequestType, UCHAR bRequest,
                                 USHORT wValue, USHORT wIndex, USHORT wLength)
{
    XHCI_SETUP_PACKET s;

    s.bmRequestType = bmRequestType;
    s.bRequest = bRequest;
    s.wValue = wValue;
    s.wIndex = wIndex;
    s.wLength = wLength;
    return s;
}

/* The measured GET_DESCRIPTOR(Hub): wValue 0x0000, NOT the spec's 0x2900. */
static XHCI_SETUP_PACKET hubDescRequest(void)
{
    return setupOf(0xA0, 0x06, 0x0000, 0, 71);
}

static XHCI_SETUP_PACKET portReset(USHORT port)
{
    return setupOf(0x23, 0x03, 4, port, 0);
}

static XHCI_SETUP_PACKET portPower(USHORT port)
{
    return setupOf(0x23, 0x03, 8, port, 0);
}

static XHCI_SETUP_PACKET portStatus(USHORT port)
{
    return setupOf(0xA3, 0x00, 0, port, 4);
}

/*
 * The QEMU 8-port hub's descriptor, typed from the USB_HUB_DESCRIPTOR layout in
 * C:\NTDDK\inc\usb100.h: length 9+2 mask bytes = 11 here, type 0x29, 8 ports,
 * wHubCharacteristics with TTT bits (6:5) = 2, PwrOn2PwrGood, current, masks.
 */
static UCHAR hubDescBytes[11] = {
    11, 0x29, 8, 0x40, 0x00, 50, 100, 0x00, 0x00, 0xFF, 0xFF
};

/* wPortStatus | wPortChange, little-endian: connected + powered. */
static UCHAR portConnectedBytes[4] = { 0x01, 0x01, 0x00, 0x00 };
static UCHAR portEmptyBytes[4]     = { 0x00, 0x01, 0x00, 0x00 };
/* Connected + powered with C_PORT_CONNECTION set in wPortChange - the shape a
 * disconnect+reconnect between polls leaves behind (Phase 7 review, B10). */
static UCHAR portReconnectBytes[4] = { 0x01, 0x01, 0x01, 0x00 };

/* ------------------------------------------------------------------ */
/* The one claim wrapper (see the file header)                         */
/* ------------------------------------------------------------------ */

static unsigned long claimCalls;        /* reset with the graph            */
static unsigned long claimCallsEver;    /* never reset - the identity's own
                                         * "the net saw something" witness */

static ULONG claim(PXHCI_TOPOLOGY topo, PXHCI_TOPO_CHILD out)
{
    claimCalls++;
    claimCallsEver++;
    return XhciTopoClaimChild(topo, out);
}

static void checkClaimIdentity(const XHCI_TOPOLOGY *topo, const char *where)
{
    checks++;
    if (topo->Claims + topo->ClaimsUnarmed + topo->ClaimsUnusable !=
        claimCalls) {
        failures++;
        printf("FAIL test_topo.c: claim identity broken at %s "
               "(claims %lu + unarmed %lu + unusable %lu != calls %lu)\n",
               where, topo->Claims, topo->ClaimsUnarmed,
               topo->ClaimsUnusable, claimCalls);
    }
}

/* Every vector resets through this, so the per-reset call counter and the
 * graph's counters can never drift apart between identity checks. */
static XHCI_TOPOLOGY topo;

static void resetTopo(void)
{
    checkClaimIdentity(&topo, "reset");
    XhciTopoReset(&topo);
    claimCalls = 0;
}

/* ------------------------------------------------------------------ */
/* The one reply wrapper (task 7b-A.3)                                 */
/* ------------------------------------------------------------------ */

/*
 * Every fold in this file goes through here, for the reason the claim wrapper
 * above exists: the departure report is an *output* of the fold, and a vector
 * that passed NULL because it did not care about disconnects would be the one
 * vector unable to see a spurious one. `lastGone` is overwritten by every call,
 * so a check on it is a check on the fold that just ran.
 */
static XHCI_TOPO_GONE lastGone;

static ULONG foldReply(PXHCI_TOPOLOGY t,
                       const XHCI_TOPO_SNOOP *s,
                       const UCHAR *data,
                       ULONG length)
{
    return XhciTopoObserveReply(t, s, data, length, &lastGone);
}

/* ------------------------------------------------------------------ */
/* Vectors                                                             */
/* ------------------------------------------------------------------ */

static void testResetIsEmpty(void)
{
    resetTopo();
    CHECK_EQ(topo.Count, 0, "reset: no nodes");
    CHECK_EQ(topo.Pending, 0, "reset: no pending claim");
    CHECK(XhciTopoFind(&topo, 1) == NULL, "reset: nothing findable");
    /* NULL-tolerant across the whole API - a graph the wiring calls before
     * StartController must not fault. */
    XhciTopoReset(NULL);
    XhciTopoDetach(NULL, 1);
    XhciTopoMigrate(NULL, 1, 2);
    XhciTopoSuppressClaim(NULL);
    CHECK(XhciTopoFind(NULL, 1) == NULL, "NULL topo finds nothing");
}

static void testPromotionAndDescriptor(void)
{
    XHCI_SETUP_PACKET s;
    XHCI_TOPO_SNOOP snoop;
    const XHCI_TOPO_NODE *node;

    resetTopo();

    /* The measured request, wValue 0x0000: must promote and arm a reply. */
    s = hubDescRequest();
    XhciTopoObserveSetup(&topo, 2, &s, &snoop);
    CHECK_EQ(snoop.Reply, XHCI_TOPO_REPLY_HUB_DESC, "hub desc reply armed");
    CHECK_EQ(snoop.Address, 2, "snoop carries the address");
    CHECK_EQ(topo.Promotions, 1, "device 2 promoted to hub");
    node = XhciTopoFind(&topo, 2);
    CHECK(node != NULL, "node exists after promotion");
    CHECK(node != NULL && (node->Flags & XHCI_TOPO_F_HUB) != 0,
          "node carries the hub flag");
    CHECK(node != NULL && node->RootPort == 0,
          "promotion alone gives no position");

    /* A spec-shaped request (wValue 0x2900) matches too: the match is on
     * bmRequestType/bRequest only, by design. */
    s = setupOf(0xA0, 0x06, 0x2900, 0, 71);
    XhciTopoObserveSetup(&topo, 2, &s, &snoop);
    CHECK_EQ(snoop.Reply, XHCI_TOPO_REPLY_HUB_DESC,
             "0x2900 wValue still matches - the match is not keyed on it");
    CHECK_EQ(topo.Promotions, 1, "no double promotion");

    /* The reply: numbers land, TTT extracts, the type byte is recorded. */
    CHECK_EQ(foldReply(&topo, &snoop, hubDescBytes, 11), 1,
             "descriptor reply folds");
    node = XhciTopoFind(&topo, 2);
    CHECK(node != NULL && (node->Flags & XHCI_TOPO_F_DESCRIPTOR) != 0,
          "descriptor flag set");
    CHECK(node != NULL && (node->Flags & XHCI_TOPO_F_DESC_TYPE_OK) != 0,
          "type byte 0x29 recorded as matching");
    CHECK_EQ(nodeOrEmpty(node)->PortCount, 8, "bNbrPorts = 8");
    CHECK_EQ(nodeOrEmpty(node)->Characteristics, 0x0040, "wHubCharacteristics little-endian");
    CHECK_EQ(XhciTopoThinkTime(nodeOrEmpty(node)), 2, "TTT = bits 6:5 of characteristics");
    CHECK_EQ(topo.Descriptors, 1, "descriptor counted");
    CHECK_EQ(topo.DescriptorsBad, 0, "nothing bad yet");
    CHECK_EQ(topo.DescriptorsNoPorts, 0, "and it named ports");

    /* An interface-recipient class request must NOT be read as hub traffic -
     * the batch 6-V audio-driver lesson. */
    s = setupOf(0xA1, 0x06, 0x2200, 0, 0x74);
    XhciTopoObserveSetup(&topo, 3, &s, &snoop);
    CHECK_EQ(snoop.Reply, XHCI_TOPO_REPLY_NONE, "interface recipient ignored");
    CHECK(XhciTopoFind(&topo, 3) == NULL, "no node for a non-hub");

    /* Address 0 traffic names no device and must record nothing. */
    s = hubDescRequest();
    XhciTopoObserveSetup(&topo, 0, &s, &snoop);
    CHECK_EQ(snoop.Reply, XHCI_TOPO_REPLY_NONE, "address 0 not snooped");
    CHECK_EQ(topo.Count, 1, "address 0 created nothing");
}

static void testBadDescriptors(void)
{
    XHCI_SETUP_PACKET s;
    XHCI_TOPO_SNOOP snoop;
    UCHAR shortDecl[5] = { 3, 0x29, 8, 0x40, 0x00 };
    UCHAR overDecl[5]  = { 9, 0x29, 8, 0x40, 0x00 };
    UCHAR wrongType[7] = { 7, 0x30, 4, 0x00, 0x00, 50, 100 };
    const XHCI_TOPO_NODE *node;

    resetTopo();
    s = hubDescRequest();
    XhciTopoObserveSetup(&topo, 2, &s, &snoop);

    /* Too short to hold wHubCharacteristics at all. */
    CHECK_EQ(foldReply(&topo, &snoop, hubDescBytes, 4), 0,
             "4-byte reply refused");
    /* Declared length below the minimum. */
    CHECK_EQ(foldReply(&topo, &snoop, shortDecl, 5), 0,
             "declared length 3 refused");
    /* Declares more than arrived - a short packet cut it off. */
    CHECK_EQ(foldReply(&topo, &snoop, overDecl, 5), 0,
             "declared 9 of 5 arrived refused");
    CHECK_EQ(topo.DescriptorsBad, 3, "all three counted bad");
    node = XhciTopoFind(&topo, 2);
    CHECK(node != NULL && (node->Flags & XHCI_TOPO_F_DESCRIPTOR) == 0,
          "no numbers taken from a bad reply");

    /* A self-consistent reply with an unexpected type byte is FOLDED - the
     * type is recorded, not required (the header's provenance note). */
    CHECK_EQ(foldReply(&topo, &snoop, wrongType, 7), 1,
             "wrong type byte still folds");
    node = XhciTopoFind(&topo, 2);
    CHECK(node != NULL && (node->Flags & XHCI_TOPO_F_DESC_TYPE_OK) == 0,
          "type mismatch recorded");
    CHECK_EQ(nodeOrEmpty(node)->DescriptorType, 0x30, "the byte actually seen is kept");
    CHECK_EQ(nodeOrEmpty(node)->PortCount, 4, "numbers taken anyway");

    /* A reply whose device was pruned in between folds nothing. */
    XhciTopoDetach(&topo, 2);
    CHECK_EQ(foldReply(&topo, &snoop, hubDescBytes, 11), 0,
             "reply after prune dropped");
}

static void testPortStatusFold(void)
{
    XHCI_SETUP_PACKET s;
    XHCI_TOPO_SNOOP snoop;
    const XHCI_TOPO_NODE *node;

    resetTopo();

    s = portStatus(1);
    XhciTopoObserveSetup(&topo, 2, &s, &snoop);
    CHECK_EQ(snoop.Reply, XHCI_TOPO_REPLY_PORT_STATUS, "port status armed");
    CHECK_EQ(snoop.Port, 1, "port carried in the snoop");
    CHECK_EQ(topo.Promotions, 1, "GET_STATUS(port) promotes too");

    CHECK_EQ(foldReply(&topo, &snoop, portConnectedBytes, 4), 1,
             "connect folds");
    node = XhciTopoFind(&topo, 2);
    CHECK_EQ(nodeOrEmpty(node)->Connected, 0x1, "port 1 marked connected");
    CHECK_EQ(topo.Disconnects, 0, "no disconnect yet");

    /* Empty on an already-empty port: the ordinary poll answer, no change. */
    s = portStatus(2);
    XhciTopoObserveSetup(&topo, 2, &s, &snoop);
    CHECK_EQ(foldReply(&topo, &snoop, portEmptyBytes, 4), 0,
             "empty on empty is no event");
    CHECK_EQ(topo.Disconnects, 0, "still no disconnect");

    /* Empty on a connected port: the disconnect reading. */
    s = portStatus(1);
    XhciTopoObserveSetup(&topo, 2, &s, &snoop);
    CHECK_EQ(foldReply(&topo, &snoop, portEmptyBytes, 4), 1,
             "1 -> 0 folds");
    node = XhciTopoFind(&topo, 2);
    CHECK_EQ(nodeOrEmpty(node)->Connected, 0, "connect bit cleared");
    CHECK_EQ(nodeOrEmpty(node)->Disconnects, 1, "node disconnect counted");
    CHECK_EQ(topo.Disconnects, 1, "graph disconnect counted");

    /* A port past the bitmask's width: counted, never folded onto bit 0. */
    s = portStatus(1);
    XhciTopoObserveSetup(&topo, 2, &s, &snoop);
    foldReply(&topo, &snoop, portConnectedBytes, 4);
    snoop.Port = XHCI_TOPO_CONNECT_PORTS + 2;
    CHECK_EQ(foldReply(&topo, &snoop, portEmptyBytes, 4), 0,
             "wide port refused");
    CHECK_EQ(topo.PortStatusesWide, 1, "wide port counted");
    node = XhciTopoFind(&topo, 2);
    CHECK_EQ(nodeOrEmpty(node)->Connected, 0x1, "port 1's bit untouched by port 33");

    /* GET_STATUS with wIndex 0 is the hub's own status, not a port's. */
    s = portStatus(0);
    XhciTopoObserveSetup(&topo, 2, &s, &snoop);
    CHECK_EQ(snoop.Reply, XHCI_TOPO_REPLY_NONE, "hub status not snooped");
}

static void testPowerSweepHighWater(void)
{
    XHCI_SETUP_PACKET s;
    XHCI_TOPO_SNOOP snoop;
    const XHCI_TOPO_NODE *node;

    resetTopo();

    /* The measured hub-start shape: SET_FEATURE(PORT_POWER) on ports 1..8. */
    s = portPower(3);
    XhciTopoObserveSetup(&topo, 2, &s, &snoop);
    s = portPower(8);
    XhciTopoObserveSetup(&topo, 2, &s, &snoop);
    s = portPower(5);
    XhciTopoObserveSetup(&topo, 2, &s, &snoop);
    CHECK_EQ(topo.PowerSweeps, 3, "sweeps counted");
    node = XhciTopoFind(&topo, 2);
    CHECK_EQ(nodeOrEmpty(node)->PortCount, 8, "high-water mark = 8");

    /*
     * **A power-on is not an enumeration parent**, and this is the check the
     * selector test exists for. usbhub powers every port at hub start - eight
     * of them on the measured QEMU hub - so a graph that armed a claim on
     * PORT_POWER would leave one lying on the hub's last port for the next
     * address-0 open from anywhere to consume. Exactly the hijack shape task
     * 7b-A.0 bounded a tier up, reintroduced one tier down.
     */
    CHECK_EQ(topo.Pending, 0, "a port power-on arms no parent claim");
    CHECK_EQ(topo.Resets, 0, "and is not counted as a reset");

    /*
     * Nor does any *other* SET_FEATURE on a port. `PORT_SUSPEND` is the one
     * that matters - usbhub sends it for selective suspend on a bus that is
     * otherwise idle - and a graph that armed on it would leave a parent claim
     * lying on a port no device is about to appear on.
     */
    s = setupOf(0x23, 0x03, 2, 1, 0);           /* SET_FEATURE(PORT_SUSPEND) */
    XhciTopoObserveSetup(&topo, 2, &s, &snoop);
    CHECK_EQ(topo.Pending, 0, "a port suspend arms no parent claim either");
    CHECK_EQ(topo.Resets, 0, "and is not a reset");
    CHECK_EQ(snoop.Reply, XHCI_TOPO_REPLY_NONE, "and asks for no reply");

    /* Once a descriptor lands, the sweep may no longer move the count - the
     * descriptor is the authority. */
    s = hubDescRequest();
    XhciTopoObserveSetup(&topo, 2, &s, &snoop);
    foldReply(&topo, &snoop, hubDescBytes, 11);
    s = portPower(15);
    XhciTopoObserveSetup(&topo, 2, &s, &snoop);
    node = XhciTopoFind(&topo, 2);
    CHECK_EQ(nodeOrEmpty(node)->PortCount, 8, "descriptor count survives a wider sweep");
}

static void testResetClaimLifecycle(void)
{
    XHCI_SETUP_PACKET s;
    XHCI_TOPO_SNOOP snoop;
    XHCI_TOPO_CHILD child;

    resetTopo();

    /* An unarmed claim is the root-port answer, not a fault. */
    CHECK_EQ(claim(&topo, &child), 0, "nothing armed, nothing claimed");
    CHECK_EQ(topo.ClaimsUnarmed, 1, "unarmed counted");

    /* Arm: hub 2 on root port 4 resets its port 3. */
    CHECK_EQ(XhciTopoAttachRoot(&topo, 2, 4, 7, XHCI_SPEED_HIGH), 1,
             "hub attaches at the root");
    s = portReset(3);
    XhciTopoObserveSetup(&topo, 2, &s, &snoop);
    CHECK_EQ(topo.Resets, 1, "reset counted");
    CHECK_EQ(topo.Pending, 1, "claim armed");

    /* Spend: the child's position is (hub 2, port 3), tier 1, route 0x3. */
    CHECK_EQ(claim(&topo, &child), 1, "claim spends");
    CHECK_EQ(child.HubAddress, 2, "child's parent hub");
    CHECK_EQ(child.HubPort, 3, "child's parent port");
    CHECK_EQ(child.RootPort, 4, "root port inherited");
    CHECK_EQ(child.Tier, 1, "one tier down");
    CHECK_EQ(child.Route, 0x3, "route = port 3 in nibble 0");
    CHECK_EQ(child.TooDeep, 0, "well inside five tiers");

    /* Spent means spent: the next open gets nothing - the 7b-A.0 hijack
     * shape, closed at this tier by construction. */
    CHECK_EQ(claim(&topo, &child), 0, "claim does not survive its spend");

    /* A second reset before a claim overwrites and is counted. */
    s = portReset(1);
    XhciTopoObserveSetup(&topo, 2, &s, &snoop);
    s = portReset(2);
    XhciTopoObserveSetup(&topo, 2, &s, &snoop);
    CHECK_EQ(topo.ResetsOverwritten, 1, "overwrite observed");
    CHECK_EQ(claim(&topo, &child), 1, "newest reset wins");
    CHECK_EQ(child.HubPort, 2, "the newer port");

    /* A reset on a hub that then vanishes: the claim dies with the node. */
    s = portReset(5);
    XhciTopoObserveSetup(&topo, 2, &s, &snoop);
    XhciTopoDetach(&topo, 2);
    CHECK_EQ(topo.Pending, 0, "detach drops the claim naming it");
    CHECK_EQ(claim(&topo, &child), 0, "no claim against a pruned hub");

    checkClaimIdentity(&topo, "reset/claim lifecycle");
}

static void testRouteArithmetic(void)
{
    XHCI_TOPO_CHILD child;
    XHCI_TOPO_CHILD grand;
    XHCI_TOPO_CHILD great;

    resetTopo();

    /* Tier 0: hub 2 on root port 1. Its own route is 0 - xHCI 4.3.3 footnote
     * 8: the Route String does not include the Root Hub Port Number. */
    XhciTopoAttachRoot(&topo, 2, 1, 1, XHCI_SPEED_HIGH);

    /* A child at port 3: nibble 0. */
    CHECK_EQ(XhciTopoChildOf(&topo, 2, 3, &child), 1, "position derivable");
    CHECK_EQ(child.Route, 0x00003UL, "tier-1 route");
    CHECK_EQ(XhciTopoAttachChild(&topo, 3, &child, XHCI_SPEED_FULL), 1,
             "child hub attaches");

    /* Its child at port 2: nibble 1 - hand-computed 0x23, never recomputed
     * through the same shift. */
    CHECK_EQ(XhciTopoChildOf(&topo, 3, 2, &grand), 1, "tier 2 derivable");
    CHECK_EQ(grand.Route, 0x00023UL, "tier-2 route 0x23");
    CHECK_EQ(grand.Tier, 2, "tier 2");
    CHECK_EQ(grand.RootPort, 1, "root port carried down");
    CHECK_EQ(XhciTopoAttachChild(&topo, 4, &grand, XHCI_SPEED_FULL), 1,
             "grandchild hub attaches");

    /* Footnote 106: port 16 clamps to 15, never masks to 0. */
    CHECK_EQ(XhciTopoChildOf(&topo, 4, 16, &great), 1, "wide port derivable");
    CHECK_EQ(great.Route, 0x00F23UL, "port 16 clamped to nibble F");

    /* An unknown parent, port 0, and a position-less hub all refuse. */
    CHECK_EQ(XhciTopoChildOf(&topo, 9, 1, &child), 0, "unknown parent refused");
    CHECK_EQ(XhciTopoChildOf(&topo, 2, 0, &child), 0, "port 0 refused");

    /*
     * **A hub the graph has identified but never placed cannot give a child a
     * position**, and this is reachable rather than defensive: promotion
     * happens the instant hub-class traffic arrives, which is before anything
     * says where the device sits. Deriving from it would answer root port 0 -
     * an Address Device parameter error at best, and a device addressed onto
     * the wrong port at worst.
     */
    {
        XHCI_SETUP_PACKET s;
        XHCI_TOPO_SNOOP snoop;

        s = portPower(1);
        XhciTopoObserveSetup(&topo, 20, &s, &snoop);
        CHECK(XhciTopoFind(&topo, 20) != NULL, "(the hub was identified)");
        CHECK_EQ(nodeOrEmpty(XhciTopoFind(&topo, 20))->RootPort, 0,
                 "(and has no position)");
        CHECK_EQ(XhciTopoChildOf(&topo, 20, 1, &child), 0,
                 "a hub with no position of its own parents nothing");
    }

    /* Five tiers is the ceiling: build to it, then ask for the sixth. */
    XhciTopoChildOf(&topo, 4, 1, &child);
    XhciTopoAttachChild(&topo, 5, &child, XHCI_SPEED_FULL);        /* tier 3 */
    XhciTopoChildOf(&topo, 5, 1, &child);
    XhciTopoAttachChild(&topo, 6, &child, XHCI_SPEED_FULL);        /* tier 4 */
    XhciTopoChildOf(&topo, 6, 2, &child);
    CHECK_EQ(child.Tier, 5, "fifth tier reachable");
    CHECK_EQ(child.TooDeep, 0, "fifth tier addressable");
    CHECK_EQ(child.Route, 0x21123UL, "five-nibble route hand-checked");
    CHECK_EQ(XhciTopoAttachChild(&topo, 7, &child, XHCI_SPEED_FULL), 1,
             "fifth-tier hub attaches");

    CHECK_EQ(XhciTopoChildOf(&topo, 7, 1, &child), 1,
             "sixth tier still derivable");
    CHECK_EQ(child.TooDeep, 1, "sixth tier flagged too deep");
    CHECK_EQ(child.Route, 0x21123UL,
             "too-deep route left as the parent's, never truncated");
    CHECK_EQ(XhciTopoAttachChild(&topo, 8, &child, XHCI_SPEED_FULL), 0,
             "too-deep attach refused");
    CHECK_EQ(topo.MaxTier, 5, "max tier recorded");

    /* Self-parenting refused: an address reuse cannot build a cycle. */
    XhciTopoChildOf(&topo, 2, 1, &child);
    child.HubAddress = 3;
    CHECK_EQ(XhciTopoAttachChild(&topo, 3, &child, XHCI_SPEED_FULL), 0,
             "a hub cannot be its own parent");
}

/*
 * **Task 7b-A.3: which transaction translator, and whether there is one at
 * all.**
 *
 * The walk is the graph's answer to a question usbport also answers, and the
 * two disagree on exactly the bus batch 7b-V0 measured - a Full-Speed `usb-hub`
 * whose child usbport reports a TT for. So the cases that matter here are the
 * *absences* as much as the hits: an all-Full-Speed path has no translator
 * anywhere, and saying it does would describe hardware that does not exist.
 */
static void testTransactionTranslator(void)
{
    XHCI_TOPO_CHILD child;
    XHCI_TOPO_TT tt;

    resetTopo();

    /* A High-Speed hub on a root port: it is its own children's TT. */
    XhciTopoAttachRoot(&topo, 2, 1, 1, XHCI_SPEED_HIGH);
    CHECK_EQ(XhciTopoTtFor(&topo, 2, 3, &tt), 1, "the HS hub is the TT");
    CHECK_EQ(tt.HubAddress, 2, "named by its usbport address");
    CHECK_EQ(tt.HubPort, 3, "on the port the device is plugged into");
    CHECK_EQ(tt.MultiTt, 0, "single-TT until a SET_INTERFACE says otherwise");

    /*
     * A Full-Speed hub below it. Its children's TT is still hub 2, and **the
     * port is hub 2's port 3, not the child's port 1** - the whole reason this
     * is a walk rather than a lookup, and the same value `USBPORT_GetTt`
     * computes by overwriting its caller's port local on every non-HS step.
     */
    CHECK_EQ(XhciTopoChildOf(&topo, 2, 3, &child), 1, "(a position for hub 3)");
    CHECK_EQ(XhciTopoAttachChild(&topo, 3, &child, XHCI_SPEED_FULL), 1,
             "(an FS hub one tier down)");
    CHECK_EQ(XhciTopoTtFor(&topo, 3, 1, &tt), 1, "the TT is two tiers up");
    CHECK_EQ(tt.HubAddress, 2, "still the High-Speed hub");
    CHECK_EQ(tt.HubPort, 3, "and the port on IT, not the port on hub 3");

    /* Multi-TT follows the flag on the ancestor that carries it. */
    {
        XHCI_SETUP_PACKET s;
        XHCI_TOPO_SNOOP snoop;

        /* Promoted first: `SET_INTERFACE` means MTT only on a device already
         * known to be a hub, and an attach is a *position* rather than an
         * identification - every device the driver places one tier down has had
         * hub-class traffic addressed to it long before. */
        s = portPower(1);
        XhciTopoObserveSetup(&topo, 2, &s, &snoop);
        XhciTopoApplySetInterface(&topo, 2, 1);
        CHECK_EQ(XhciTopoTtFor(&topo, 3, 1, &tt), 1, "(still found)");
        CHECK_EQ(tt.MultiTt, 1, "the multi-TT interface is the ancestor's");
    }

    /*
     * **An all-Full-Speed path has no TT**, which is the QEMU bus: a Full-Speed
     * `usb-hub` on a root port with a device behind it. usbport reports the hub
     * as the translator; the graph knows its speed and reports nothing.
     */
    resetTopo();
    XhciTopoAttachRoot(&topo, 2, 1, 1, XHCI_SPEED_FULL);
    CHECK_EQ(XhciTopoTtFor(&topo, 2, 1, &tt), 0,
             "a Full-Speed hub on a root port is no transaction translator");
    CHECK_EQ(tt.HubAddress, 0, "and the output is zeroed rather than stale");
    CHECK_EQ(tt.HubPort, 0, "both halves");

    /* A hub the graph has never heard of, and the NULL/zero arguments. */
    CHECK_EQ(XhciTopoTtFor(&topo, 9, 1, &tt), 0, "unknown parent has no TT");
    CHECK_EQ(XhciTopoTtFor(&topo, 2, 0, &tt), 0, "port 0 refused");
    CHECK_EQ(XhciTopoTtFor(&topo, 0, 1, &tt), 0, "address 0 refused");
    CHECK_EQ(XhciTopoTtFor(NULL, 2, 1, &tt), 0, "NULL topology refused");
    CHECK_EQ(XhciTopoTtFor(&topo, 2, 1, NULL), 0, "NULL output refused");
}

/*
 * **A root-port reset supersedes a pending hub claim** (task 7b-A.3), which is
 * what keeps the two enumeration entitlements from both being armed. The driver
 * calls this from `XhciSlotPortReset`; here it is the arithmetic.
 */
static void testPendingDropped(void)
{
    XHCI_SETUP_PACKET s;
    XHCI_TOPO_SNOOP snoop;
    XHCI_TOPO_CHILD child;

    resetTopo();
    XhciTopoAttachRoot(&topo, 2, 1, 1, XHCI_SPEED_HIGH);

    s = portReset(4);
    XhciTopoObserveSetup(&topo, 2, &s, &snoop);
    CHECK_EQ(topo.Pending, 1, "(a hub port reset armed a claim)");
    CHECK_EQ(snoop.Armed, 1, "and the snoop says so, which is what the driver "
                             "spends the root-port claim on");

    XhciTopoDropPending(&topo);
    CHECK_EQ(topo.Pending, 0, "the root-port reset dropped it");
    CHECK_EQ(topo.PendingDropped, 1, "and counted");
    CHECK_EQ(claim(&topo, &child), 0, "so nothing is claimable afterwards");

    /* Idempotent: dropping nothing counts nothing, so the reading stays a
     * measurement of superseded brackets rather than of resets. */
    XhciTopoDropPending(&topo);
    CHECK_EQ(topo.PendingDropped, 1, "dropping an unarmed claim counts nothing");
    XhciTopoDropPending(NULL);

    /* And a request that is not a reset arms nothing, so it reports nothing. */
    s = portPower(4);
    XhciTopoObserveSetup(&topo, 2, &s, &snoop);
    CHECK_EQ(snoop.Armed, 0, "a port power sweep arms no claim");
}

/*
 * The departure report (task 7b-A.3): the 1 -> 0 connect transition is the only
 * statement a behind-hub device ever makes that it has gone, so the fold has to
 * hand the caller the pair - and prune the node itself if the departing device
 * was a hub.
 */
static void testDisconnectReport(void)
{
    XHCI_SETUP_PACKET s;
    XHCI_TOPO_SNOOP snoop;
    XHCI_TOPO_CHILD child;

    resetTopo();
    XhciTopoAttachRoot(&topo, 2, 1, 1, XHCI_SPEED_HIGH);

    /* A hub behind the hub, on port 3. */
    CHECK_EQ(XhciTopoChildOf(&topo, 2, 3, &child), 1, "(a position)");
    CHECK_EQ(XhciTopoAttachChild(&topo, 3, &child, XHCI_SPEED_FULL), 1,
             "(a hub one tier down)");
    CHECK_EQ(XhciTopoChildOf(&topo, 3, 1, &child), 1, "(and one below that)");
    CHECK_EQ(XhciTopoAttachChild(&topo, 4, &child, XHCI_SPEED_FULL), 1,
             "(a two-tier chain)");

    /* Port 3 connected, then empty. */
    s = portStatus(3);
    XhciTopoObserveSetup(&topo, 2, &s, &snoop);
    CHECK_EQ(foldReply(&topo, &snoop, portConnectedBytes, 4), 1, "(connected)");
    CHECK_EQ(lastGone.Disconnected, 0, "a connect is not a departure");

    CHECK_EQ(foldReply(&topo, &snoop, portEmptyBytes, 4), 1, "(now empty)");
    CHECK_EQ(lastGone.Disconnected, 1, "the fold reports the departure");
    CHECK_EQ(lastGone.HubAddress, 2, "naming the hub");
    CHECK_EQ(lastGone.HubPort, 3, "and the port on it");
    CHECK(XhciTopoFind(&topo, 3) == NULL,
          "the hub that left took its own node with it");
    CHECK(XhciTopoFind(&topo, 4) == NULL, "...and its subtree");
    CHECK(XhciTopoFind(&topo, 2) != NULL, "while the hub reporting it stays");

    /*
     * A disconnect on a port that never held a hub reports the pair anyway -
     * the caller's device records hold every *leaf*, and the graph holds none
     * of them, so a fold that only reported prunable departures would lose
     * every keyboard.
     */
    s = portStatus(5);
    XhciTopoObserveSetup(&topo, 2, &s, &snoop);
    CHECK_EQ(foldReply(&topo, &snoop, portConnectedBytes, 4), 1, "(connected)");
    CHECK_EQ(foldReply(&topo, &snoop, portEmptyBytes, 4), 1, "(now empty)");
    CHECK_EQ(lastGone.Disconnected, 1, "a leaf's departure is reported too");
    CHECK_EQ(lastGone.HubPort, 5, "on its own port");

    /* And a fold that reports nothing leaves the output zeroed rather than
     * holding the previous call's pair - a caller acting on a stale report
     * would tear down a device that is still there. */
    s = portStatus(6);
    XhciTopoObserveSetup(&topo, 2, &s, &snoop);
    CHECK_EQ(foldReply(&topo, &snoop, portEmptyBytes, 4), 0, "(empty on empty)");
    CHECK_EQ(lastGone.Disconnected, 0, "no departure reported");
    CHECK_EQ(lastGone.HubAddress, 0, "and the pair is cleared, not stale");
    CHECK_EQ(lastGone.HubPort, 0, "both halves");

    /*
     * **Connected-to-connected with C_PORT_CONNECTION set is a departure**
     * (Phase 7 review, B10): a disconnect and reconnect between usbhub polls
     * never shows the graph an empty port, and only the change word carries
     * the proof a device left in between.
     */
    s = portStatus(7);
    XhciTopoObserveSetup(&topo, 2, &s, &snoop);
    CHECK_EQ(foldReply(&topo, &snoop, portConnectedBytes, 4), 1, "(connected)");
    CHECK_EQ(foldReply(&topo, &snoop, portReconnectBytes, 4), 1,
             "(connected again, change bit set)");
    CHECK_EQ(lastGone.Disconnected, 1, "a swap between polls is a departure");
    CHECK_EQ(lastGone.HubPort, 7, "on its own port");
    CHECK_EQ(topo.Reconnects, 1, "counted as one only the change word saw");

    /* The change bit on a port that was empty is the ordinary plug-in - a
     * departure invented there would tear down the device that just arrived. */
    s = portStatus(8);
    XhciTopoObserveSetup(&topo, 2, &s, &snoop);
    CHECK_EQ(foldReply(&topo, &snoop, portReconnectBytes, 4), 1,
             "(first connect, change bit set)");
    CHECK_EQ(lastGone.Disconnected, 0, "a plug-in is not a departure");
    CHECK_EQ(topo.Reconnects, 1, "and not counted as a swap");
}

/*
 * A spent claim that cannot answer is its own reading (Phase 7 review, B9):
 * `ClaimsUnarmed` means "an address-0 open with no pending parent" - the
 * ordinary root-port answer - and a consumed claim whose node was pruned
 * between the reset and the open used to be folded into it, making a channel
 * failure indistinguishable from business as usual.
 */
static void testClaimUnusable(void)
{
    XHCI_SETUP_PACKET s;
    XHCI_TOPO_SNOOP snoop;
    XHCI_TOPO_CHILD child;

    resetTopo();
    XhciTopoAttachRoot(&topo, 2, 1, 1, XHCI_SPEED_HIGH);
    s = portReset(3);
    XhciTopoObserveSetup(&topo, 2, &s, &snoop);
    CHECK_EQ(topo.Pending, 1, "(a claim armed)");

    XhciTopoDetach(&topo, 2);
    /* The detach dropped the claim naming the departed hub, so this open is
     * *unarmed* - the pair was never handed out against a dead node. */
    CHECK_EQ(claim(&topo, &child), 0, "nothing claimable");
    CHECK_EQ(topo.ClaimsUnarmed, 1, "(the detach already dropped the claim)");
    CHECK_EQ(topo.ClaimsUnusable, 0, "so nothing was consumed and wasted");

    /* The consumed-and-wasted shape needs a node with no position: promotion
     * creates one before anything says where it sits, and a reset through it
     * arms a claim XhciTopoChildOf then refuses. */
    resetTopo();
    s = portReset(3);
    XhciTopoObserveSetup(&topo, 9, &s, &snoop);
    CHECK_EQ(topo.Pending, 1, "(armed against a position-less hub)");
    CHECK_EQ(claim(&topo, &child), 0, "the claim cannot answer");
    CHECK_EQ(topo.ClaimsUnusable, 1, "and is counted as consumed-and-wasted");
    CHECK_EQ(topo.ClaimsUnarmed, 0, "not as the ordinary unarmed open");
    CHECK_EQ(topo.Pending, 0, "(and it was spent, not left armed)");
}

static void testPruning(void)
{
    XHCI_TOPO_CHILD child;

    resetTopo();

    /*
     * **The grandchild must sit at a LOWER table index than its own parent**,
     * which is the only arrangement the multi-pass prune is needed for - and
     * getting it takes deliberate construction, because the table hands out
     * the first free slot. A first draft of this vector freed the slots in the
     * other order, produced parent-before-child, and a single-pass mutation
     * passed it.
     *
     * Slots: attach A, B, H; free B then A; the child takes B's old slot and
     * the grandchild takes A's - so index 0 is the grandchild, index 1 its
     * parent, index 2 the hub. A single forward pass then skips index 0 (its
     * parent is still present at index 1), removes index 1, and stops.
     */
    XhciTopoAttachRoot(&topo, 2, 1, 1, XHCI_SPEED_HIGH);   /* index 0: A */
    XhciTopoAttachRoot(&topo, 3, 1, 1, XHCI_SPEED_HIGH);   /* index 1: B */
    XhciTopoAttachRoot(&topo, 9, 2, 1, XHCI_SPEED_HIGH);   /* index 2: H */
    XhciTopoDetach(&topo, 3);                              /* index 1 free */
    XhciTopoChildOf(&topo, 9, 1, &child);
    XhciTopoAttachChild(&topo, 10, &child, XHCI_SPEED_FULL); /* index 1: child */
    XhciTopoDetach(&topo, 2);                              /* index 0 free */
    XhciTopoChildOf(&topo, 10, 1, &child);
    XhciTopoAttachChild(&topo, 11, &child, XHCI_SPEED_FULL); /* index 0: grand */
    CHECK_EQ(topo.Count, 3, "chain of three");
    CHECK_EQ(topo.Node[0].Address, 11, "(grandchild really is at index 0)");
    CHECK_EQ(topo.Node[1].Address, 10, "(its parent above it at index 1)");

    /* Killing the root must take the whole chain, whatever the indices. */
    XhciTopoDetach(&topo, 9);
    CHECK_EQ(topo.Count, 0, "detach prunes the whole subtree");
    CHECK(XhciTopoFind(&topo, 10) == NULL, "child gone");
    CHECK(XhciTopoFind(&topo, 11) == NULL, "grandchild gone");
}

/*
 * XhciTopoMigrate (Phase 7 review, findings A4/A5): the node follows a
 * re-assigned address, a stale node under the new address is pruned with its
 * subtree, and a migrated hub's own children are not carried across.
 */
static void testMigration(void)
{
    XHCI_TOPO_CHILD child;
    XHCI_SETUP_PACKET s;
    XHCI_TOPO_SNOOP snoop;
    const XHCI_TOPO_NODE *node;

    resetTopo();

    /* The node follows its device, position intact. */
    XhciTopoAttachRoot(&topo, 2, 1, 5, XHCI_SPEED_HIGH);
    XhciTopoMigrate(&topo, 2, 6);
    CHECK(XhciTopoFind(&topo, 2) == NULL, "the old key no longer answers");
    node = XhciTopoFind(&topo, 6);
    CHECK(node != NULL, "the node follows the address");
    CHECK_EQ(nodeOrEmpty(node)->RootPort, 1, "with its root port intact");
    CHECK_EQ(nodeOrEmpty(node)->Tier, 0, "and its tier");
    CHECK_EQ(topo.Migrations, 1, "counted");

    /* The same address re-assigned - the common recovery cycle - is a no-op
     * that must not prune the device's own node. */
    XhciTopoMigrate(&topo, 6, 6);
    CHECK(XhciTopoFind(&topo, 6) != NULL, "re-using the address keeps the node");
    CHECK_EQ(topo.Migrations, 1, "and is not a migration");
    CHECK_EQ(topo.MigrationsStale, 0, "nor a stale prune");

    /* A stale node under the newly assigned address is pruned with its
     * subtree before the new device can inherit the departed hub's position. */
    XhciTopoChildOf(&topo, 6, 3, &child);
    XhciTopoAttachChild(&topo, 7, &child, XHCI_SPEED_FULL);
    XhciTopoAttachRoot(&topo, 9, 2, 1, XHCI_SPEED_HIGH);
    XhciTopoMigrate(&topo, 9, 6);   /* usbport hands 9's device the address 6 */
    node = XhciTopoFind(&topo, 6);
    CHECK_EQ(nodeOrEmpty(node)->RootPort, 2,
             "the key now names the live device's node");
    CHECK(XhciTopoFind(&topo, 7) == NULL, "the stale subtree went with it");
    CHECK(XhciTopoFind(&topo, 9) == NULL, "and the old key no longer answers");
    CHECK_EQ(topo.MigrationsStale, 1, "the stale prune is counted");
    CHECK_EQ(topo.Migrations, 2, "beside the migration");

    /* A node with no old key still gets the stale prune: a non-hub device
     * assigned an address a departed hub's node still sits under. */
    resetTopo();
    XhciTopoAttachRoot(&topo, 4, 1, 5, XHCI_SPEED_HIGH);
    XhciTopoMigrate(&topo, 0, 4);
    CHECK(XhciTopoFind(&topo, 4) == NULL, "the stale node is pruned");
    CHECK_EQ(topo.MigrationsStale, 1, "and counted");
    CHECK_EQ(topo.Migrations, 0, "with no migration to count");

    /* A migrated hub's children are not carried across - their ParentAddress
     * names the old key, and the re-enumeration rebuilds them anyway. */
    resetTopo();
    XhciTopoAttachRoot(&topo, 2, 1, 5, XHCI_SPEED_HIGH);
    XhciTopoChildOf(&topo, 2, 1, &child);
    XhciTopoAttachChild(&topo, 3, &child, XHCI_SPEED_FULL);
    XhciTopoMigrate(&topo, 2, 4);
    CHECK(XhciTopoFind(&topo, 4) != NULL, "the hub migrated");
    CHECK(XhciTopoFind(&topo, 3) == NULL, "its children did not follow");

    /* A pending claim naming the old key follows the hub it names. */
    resetTopo();
    XhciTopoAttachRoot(&topo, 2, 1, 5, XHCI_SPEED_HIGH);
    s = portReset(3);
    XhciTopoObserveSetup(&topo, 2, &s, &snoop);
    CHECK_EQ(snoop.Armed, 1, "(a claim armed against the old key)");
    CHECK_EQ(snoop.Port, 3, "(and the snoop reports its port)");
    XhciTopoMigrate(&topo, 2, 6);
    CHECK_EQ(claim(&topo, &child), 1, "the claim is still spendable");
    CHECK_EQ(child.HubAddress, 6, "and names the hub's new address");
}

/*
 * XhciTopoSuppressClaim (Phase 7 review, finding A6): the verb the caller uses
 * when it judges an arm to be usbhub's second reset of a bracket. Counted
 * apart from PendingDropped, because the two readings disagree about whose
 * bracket was live.
 */
static void testSuppressClaim(void)
{
    XHCI_SETUP_PACKET s;
    XHCI_TOPO_SNOOP snoop;
    XHCI_TOPO_CHILD child;

    resetTopo();

    XhciTopoSuppressClaim(&topo);
    CHECK_EQ(topo.ResetsSuppressed, 0, "nothing armed, nothing suppressed");

    XhciTopoAttachRoot(&topo, 2, 1, 5, XHCI_SPEED_HIGH);
    s = portReset(1);
    XhciTopoObserveSetup(&topo, 2, &s, &snoop);
    CHECK_EQ(topo.Pending, 1, "(armed)");
    XhciTopoSuppressClaim(&topo);
    CHECK_EQ(topo.Pending, 0, "the pair is given up");
    CHECK_EQ(topo.ResetsSuppressed, 1, "and counted as a suppression");
    CHECK_EQ(topo.PendingDropped, 0,
             "not as a root-port supersession - the readings differ");
    CHECK_EQ(claim(&topo, &child), 0, "so the next open is unarmed");
}

static void testMultiTt(void)
{
    XHCI_SETUP_PACKET s;
    XHCI_TOPO_SNOOP snoop;
    const XHCI_TOPO_NODE *node;

    resetTopo();

    /* SET_INTERFACE on a device nobody called a hub: ignored. Every device
     * with alternate settings sends this; it means MTT only on a hub. */
    XhciTopoApplySetInterface(&topo, 5, 1);
    CHECK(XhciTopoFind(&topo, 5) == NULL, "SET_INTERFACE creates no node");
    CHECK_EQ(topo.AltSettings, 0, "not counted off a non-hub");

    XhciTopoAttachRoot(&topo, 2, 1, 1, XHCI_SPEED_HIGH);
    s = portPower(1);
    XhciTopoObserveSetup(&topo, 2, &s, &snoop);   /* promote via port traffic */

    node = XhciTopoFind(&topo, 2);
    CHECK(node != NULL && (node->Flags & XHCI_TOPO_F_ALT_SEEN) == 0,
          "no alternate seen yet");

    /*
     * **Observing the request changes nothing** - it is the completion that
     * says the hub enabled the interface (xHCI 4.5.2), and until the post-Phase 13 review rounds
     * this graph applied MTT on placement while the descriptor half of the
     * same packet already waited. A hub that STALLs its SET_INTERFACE would
     * otherwise be programmed as multi-TT.
     */
    s = setupOf(0x01, 0x0B, 1, 0, 0);
    XhciTopoObserveSetup(&topo, 2, &s, &snoop);
    node = XhciTopoFind(&topo, 2);
    CHECK(node != NULL && (node->Flags & XHCI_TOPO_F_MTT) == 0,
          "the setup packet alone does not set MTT");
    CHECK(node != NULL && (node->Flags & XHCI_TOPO_F_ALT_SEEN) == 0,
          "nor records an alternate as seen");
    CHECK_EQ(topo.AltSettings, 0, "nor counts one");

    /* Alternate 1 = the multi-TT interface, applied off the completion. */
    XhciTopoApplySetInterface(&topo, 2, 1);
    node = XhciTopoFind(&topo, 2);
    CHECK(node != NULL && (node->Flags & XHCI_TOPO_F_MTT) != 0, "MTT set");
    CHECK(node != NULL && (node->Flags & XHCI_TOPO_F_ALT_SEEN) != 0,
          "alternate recorded as seen");
    CHECK_EQ(topo.AltSettings, 1, "counted");

    /* Back to alternate 0: MTT follows the currently enabled interface
     * (Table 6-4), so it clears - it is not a latch. */
    XhciTopoApplySetInterface(&topo, 2, 0);
    node = XhciTopoFind(&topo, 2);
    CHECK(node != NULL && (node->Flags & XHCI_TOPO_F_MTT) == 0, "MTT cleared");
    CHECK(node != NULL && (node->Flags & XHCI_TOPO_F_ALT_SEEN) != 0,
          "seen stays seen");

    /* A NULL graph and address 0 are refusals, not crashes - the contract the
     * header states, and the only two arguments a caller can get wrong. */
    XhciTopoApplySetInterface(NULL, 2, 1);
    XhciTopoApplySetInterface(&topo, 0, 1);
    node = XhciTopoFind(&topo, 2);
    CHECK(node != NULL && (node->Flags & XHCI_TOPO_F_MTT) == 0,
          "neither refusal touched the hub");
    CHECK_EQ(topo.AltSettings, 2, "and neither counted");

    /*
     * **A reset returns every interface to alternate 0**, so a re-enumeration
     * has to drop both flags - and `ALT_SEEN` with `MTT`, because nothing has
     * been *asked* of the device that came back. Round 11: the graph kept MTT
     * across a re-enumeration while the descriptor half of the same fact was
     * reset, so the pump could re-derive the previous tenancy's MTT onto a hub
     * that had never been asked to be one.
     */
    XhciTopoApplySetInterface(&topo, 2, 1);
    node = XhciTopoFind(&topo, 2);
    CHECK(node != NULL && (node->Flags & XHCI_TOPO_F_MTT) != 0, "(MTT on)");

    XhciTopoForgetAlternate(&topo, 2);
    node = XhciTopoFind(&topo, 2);
    CHECK(node != NULL && (node->Flags & XHCI_TOPO_F_MTT) == 0,
          "a reset drops MTT");
    CHECK(node != NULL && (node->Flags & XHCI_TOPO_F_ALT_SEEN) == 0,
          "and drops 'an alternate was seen' with it - never asked, not "
          "asked and answered 0");
    CHECK(node != NULL && (node->Flags & XHCI_TOPO_F_HUB) != 0,
          "while the node keeps its hub-ness - same port, same position");
    CHECK_EQ(topo.AltForgotten, 1, "counted");

    /* Idempotent, and counted only when there was something to drop - so a
     * counter reading is a count of resets that lost a selection, not of
     * resets. */
    XhciTopoForgetAlternate(&topo, 2);
    CHECK_EQ(topo.AltForgotten, 1, "a second forget with nothing to drop");
    XhciTopoForgetAlternate(NULL, 2);
    XhciTopoForgetAlternate(&topo, 0);
    XhciTopoForgetAlternate(&topo, 99);
    CHECK_EQ(topo.AltForgotten, 1,
             "and a NULL graph, address 0 and an unknown address are refusals");
}

/*
 * Task 7b-A.2: what a hub's own Slot Context should say.
 *
 * The values are typed out rather than recomputed through the code's own
 * shift - `hubDescBytes` declares wHubCharacteristics 0x0040, whose bits 6:5 are
 * TTT 2 - and every rule is exercised in both directions, because three of the
 * four fields are conditional and a mark that always filled them would be caught
 * by nothing else.
 */
static void testHubMark(void)
{
    XHCI_SETUP_PACKET s;
    XHCI_TOPO_SNOOP snoop;
    XHCI_TOPO_HUBMARK mark;
    /* A self-consistent hub descriptor claiming no downstream ports. */
    static UCHAR noPorts[7] = { 7, 0x29, 0, 0x40, 0x00, 50, 100 };

    resetTopo();

    /* Nothing at all: a NULL node, and a NULL `out` that must not fault. */
    mark.Hub = 0xFF;
    CHECK_EQ(XhciTopoHubMark(NULL, XHCI_SPEED_HIGH, &mark), 0,
             "no node, no marking");
    CHECK_EQ(mark.Hub, 0, "and `out` is cleared rather than left alone");
    CHECK_EQ(XhciTopoHubMark(NULL, XHCI_SPEED_HIGH, NULL), 0,
             "a NULL out is answered, not dereferenced");

    /*
     * **A hub identified but not yet described cannot be marked**, and this is
     * the reachable case rather than a defensive one: promotion happens on the
     * first hub-class port request, and usbhub's power sweep is exactly that -
     * so the node here carries a PortCount from the sweep's high-water mark and
     * still must not be programmed. Number of Ports is a hub descriptor field
     * (Table 6-5) and the sweep's number is a lower bound.
     */
    s = portPower(8);
    XhciTopoObserveSetup(&topo, 2, &s, &snoop);
    CHECK_EQ(nodeOrEmpty(XhciTopoFind(&topo, 2))->PortCount, 8,
             "(the sweep left a port count)");
    CHECK_EQ(XhciTopoHubMark(XhciTopoFind(&topo, 2), XHCI_SPEED_HIGH, &mark), 0,
             "a hub with no descriptor is not marked from the power sweep");

    /* The descriptor lands: High Speed, no alternate setting selected. */
    s = hubDescRequest();
    XhciTopoObserveSetup(&topo, 2, &s, &snoop);
    CHECK_EQ(foldReply(&topo, &snoop, hubDescBytes, 11), 1,
             "(descriptor folds)");
    CHECK_EQ(XhciTopoHubMark(XhciTopoFind(&topo, 2), XHCI_SPEED_HIGH, &mark), 1,
             "a described High-Speed hub is markable");
    CHECK_EQ(mark.Hub, 1, "Hub = 1");
    CHECK_EQ(mark.NumberOfPorts, 8, "Number of Ports = bNbrPorts");
    CHECK_EQ(mark.TtThinkTime, 2, "TTT = wHubCharacteristics bits 6:5");
    CHECK_EQ(mark.MultiTt, 0,
             "MTT = 0 until a Set Interface enables the multi-TT interface");

    /* A SET_INTERFACE(1) that SUCCEEDED is what turns MTT on - spec Table 6-4's
     * "the Multiple TT Interface has been enabled by software", where enabled is
     * what the completion says and the placement does not. */
    XhciTopoApplySetInterface(&topo, 2, 1);
    CHECK_EQ(XhciTopoHubMark(XhciTopoFind(&topo, 2), XHCI_SPEED_HIGH, &mark), 1,
             "(still markable)");
    CHECK_EQ(mark.MultiTt, 1, "MTT follows the enabled alternate setting");
    XhciTopoApplySetInterface(&topo, 2, 0);
    (void)XhciTopoHubMark(XhciTopoFind(&topo, 2), XHCI_SPEED_HIGH, &mark);
    CHECK_EQ(mark.MultiTt, 0, "and back off with alternate 0");

    /*
     * **The Full-Speed hub, which is the shape both target VMs run.** QEMU's
     * `usb-hub` is USB 1.1, and Table 6-6 p.409 is explicit: "If this device is
     * not a High-speed hub (Hub = '0' or Speed != High-speed), then this field
     * shall be '0'." Same node, same descriptor, same enabled alternate - only
     * the speed differs, so this is the check that the two are not conflated.
     */
    XhciTopoApplySetInterface(&topo, 2, 1);
    CHECK_EQ(XhciTopoHubMark(XhciTopoFind(&topo, 2), XHCI_SPEED_FULL, &mark), 1,
             "a Full-Speed hub is still a hub");
    CHECK_EQ(mark.Hub, 1, "Hub = 1 at Full Speed");
    CHECK_EQ(mark.NumberOfPorts, 8, "with its port count");
    CHECK_EQ(mark.TtThinkTime, 0,
             "but no TT think time - it has no transaction translator");
    CHECK_EQ(mark.MultiTt, 0, "and no MTT, whatever the alternate setting says");

    CHECK_EQ(XhciTopoHubMark(XhciTopoFind(&topo, 2), XHCI_SPEED_LOW, &mark), 1,
             "the same at Low Speed");
    CHECK_EQ(mark.TtThinkTime, 0, "TTT still 0");
    CHECK_EQ(XhciTopoHubMark(XhciTopoFind(&topo, 2), XHCI_SPEED_UNKNOWN, &mark),
             1, "and for a speed this driver never decoded");
    CHECK_EQ(mark.TtThinkTime, 0, "which is not High Speed either");

    /* A device nothing identified as a hub. */
    CHECK_EQ(XhciTopoHubMark(XhciTopoFind(&topo, 9), XHCI_SPEED_HIGH, &mark), 0,
             "an unknown device is not a hub");

    /*
     * A self-consistent descriptor claiming no ports. Not malformed, not
     * usable: Table 6-5 defines the field as "the number of downstream facing
     * ports supported by the hub", and 6.2.2.2 p.412 requires it initialized
     * when Hub = 1. Counted so the silence is readable.
     */
    resetTopo();
    s = hubDescRequest();
    XhciTopoObserveSetup(&topo, 3, &s, &snoop);
    CHECK_EQ(foldReply(&topo, &snoop, noPorts, 7), 1,
             "a zero-port descriptor still folds");
    CHECK_EQ(topo.DescriptorsBad, 0, "it is not malformed");
    CHECK_EQ(topo.DescriptorsNoPorts, 1, "but it is counted as unusable");
    CHECK_EQ(XhciTopoHubMark(XhciTopoFind(&topo, 3), XHCI_SPEED_HIGH, &mark), 0,
             "and marks nothing");
}

static void testTableFull(void)
{
    XHCI_SETUP_PACKET s;
    XHCI_TOPO_SNOOP snoop;
    ULONG i;

    resetTopo();

    for (i = 0; i < XHCI_TOPO_NODES; i++) {
        CHECK_EQ(XhciTopoAttachRoot(&topo, 10 + i, 1 + (i % 4), 1,
                                    XHCI_SPEED_HIGH),
                 1, "table fills");
    }
    CHECK_EQ(topo.Count, XHCI_TOPO_NODES, "full");
    CHECK_EQ(topo.Dropped, 0, "nothing dropped yet");

    CHECK_EQ(XhciTopoAttachRoot(&topo, 99, 1, 1, XHCI_SPEED_HIGH), 0,
             "the ninth refused");
    CHECK_EQ(topo.Dropped, 1, "and counted - a full table is not silent");

    /* A promotion of a device already held still works when full. */
    s = portPower(1);
    XhciTopoObserveSetup(&topo, 10, &s, &snoop);
    CHECK_EQ(topo.Promotions, 1, "existing node promotable at capacity");

    /* A reset addressed to a device the full table refused: the parent is
     * lost, and the loss is a counter rather than a silence. */
    s = portReset(1);
    XhciTopoObserveSetup(&topo, 99, &s, &snoop);
    CHECK_EQ(topo.ResetsUnknownHub, 1, "reset on an unheld hub counted");
    CHECK_EQ(topo.Pending, 0, "and arms nothing");
}

/* ------------------------------------------------------------------ */

int main(void)
{
    testResetIsEmpty();
    testPromotionAndDescriptor();
    testBadDescriptors();
    testPortStatusFold();
    testPowerSweepHighWater();
    testResetClaimLifecycle();
    testRouteArithmetic();
    testTransactionTranslator();
    testPendingDropped();
    testDisconnectReport();
    testClaimUnusable();
    testPruning();
    testMigration();
    testSuppressClaim();
    testMultiTt();
    testHubMark();
    testTableFull();

    checkClaimIdentity(&topo, "end of main");
    /* The never-reset twin: an identity that never saw a claim would pass
     * every run as a net over nothing (task 7b-A.1.0's rule). */
    CHECK(claimCallsEver >= 5, "the claim identity measured real claims");

    printf("%d checks, %d failures\n", checks, failures);
    return failures;
}
