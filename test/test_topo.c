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
 * Roadmap task 27-A.4 adds the vectors the bus's own hub class is held to,
 * since on Windows 2000 they are the only coverage the High-Speed hub and
 * transaction-translator paths have (roadmap-hcd.md, Phase 27 checkpoint):
 * table-driven placements with their Route String, Root Hub Port Number and
 * TT triple, each row citing the design record it was typed from; the depth
 * limit; subtree removal; malformed hub descriptors; and the well-formedness
 * of test\hub_port_vectors.h, the port state-machine table 27-A.1 will run.
 *
 * KNOWN GAPS. Where src\xhci_topo.c does not yet do what a design record
 * says, the vector is a KNOWN_GAP rather than a CHECK: it prints what the
 * record expects and is counted apart, and it does not fail the suite. The
 * expected value is still the record's - a gap is a statement that the code
 * is behind, never that the record is wrong. A gap that starts to hold says
 * so loudly, and should then become a CHECK.
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
#include "test_harness.h"
#include "hub_port_vectors.h"

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
/* Known gaps (see the file header)                                    */
/* ------------------------------------------------------------------ */

static int gapsOpen;
static int gapsHeld;

#define KNOWN_GAP(holds, id, what) \
    knownGapImpl(((holds) != 0), (id), (what), __LINE__)

static void knownGapImpl(int holds, const char *id, const char *what,
                         int line)
{
    if (holds) {
        gapsHeld++;
        printf("NOTE test_topo.c:%d: known gap %s now holds - make it a "
               "CHECK and take it off the list: %s\n", line, id, what);
        return;
    }
    gapsOpen++;
    printf("KNOWN GAP test_topo.c:%d: %s: %s\n", line, id, what);
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

    /*
     * **`CLEAR_TT_BUFFER` must not move the high-water mark**, and neither
     * must the three requests beside it. This is audit finding A7, and the
     * defect it fixed was real on a real bus: usbhub sends `CLEAR_TT_BUFFER`
     * (bRequest 8, recipient Other, the same `0x23` request type as a port
     * feature) to a multi-TT hub whenever a full- or low-speed transaction
     * below it fails, and its `wIndex` is not a port number at all - it packs
     * an endpoint number, device address, endpoint type and direction, so it
     * reads as a port number in the thousands. `src/xhci_topo.c` filters on
     * `bRequest` for exactly this reason, and until the 2026-09-07 audit's G8
     * no packet with one of these requests appeared anywhere in this suite:
     * deleting the filter passed the whole file and let the bug back in.
     *
     * The count is 8 from the sweep above and no descriptor has landed yet,
     * so the guard is the only thing standing between these packets and a
     * `PortCount` of 4,660.
     */
    s = setupOf(0x23, 0x08, 0, 0x1234, 0);      /* CLEAR_TT_BUFFER */
    XhciTopoObserveSetup(&topo, 2, &s, &snoop);
    node = XhciTopoFind(&topo, 2);
    CHECK_EQ(nodeOrEmpty(node)->PortCount, 8,
             "CLEAR_TT_BUFFER's wIndex is not a port number and moves nothing");
    CHECK_EQ(snoop.Reply, XHCI_TOPO_REPLY_NONE, "and asks for no reply");
    CHECK_EQ(topo.Pending, 0, "and arms no parent claim");
    CHECK_EQ(topo.Resets, 0, "and is not a reset");

    s = setupOf(0x23, 0x09, 0, 0x1234, 0);      /* RESET_TT */
    XhciTopoObserveSetup(&topo, 2, &s, &snoop);
    s = setupOf(0xA3, 0x0A, 0, 0x1234, 1);      /* GET_TT_STATE */
    XhciTopoObserveSetup(&topo, 2, &s, &snoop);
    s = setupOf(0x23, 0x0B, 0, 0x1234, 0);      /* STOP_TT */
    XhciTopoObserveSetup(&topo, 2, &s, &snoop);
    node = XhciTopoFind(&topo, 2);
    CHECK_EQ(nodeOrEmpty(node)->PortCount, 8,
             "nor do RESET_TT, GET_TT_STATE or STOP_TT beside it");

    /*
     * The contrast that makes the three above a statement about `bRequest`
     * rather than about `wIndex`: the very same wIndex on a request the
     * filter admits DOES move the mark, because there it really is a port
     * number. Ports are 1-based and this hub has eight, so 12 is out of
     * range for it - which is exactly the reading the high-water mark is
     * allowed to take before a descriptor arrives.
     */
    s = portPower(12);
    XhciTopoObserveSetup(&topo, 2, &s, &snoop);
    node = XhciTopoFind(&topo, 2);
    CHECK_EQ(nodeOrEmpty(node)->PortCount, 12,
             "while SET_FEATURE(PORT_POWER) with a wider port number does");

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
/* Task 27-A.4: placements                                             */
/* ------------------------------------------------------------------ */

/*
 * One row is a bus: up to six hubs, each on a root port or on a port of an
 * earlier hub, and one device below one of them. What the row asserts is
 * what the device's own Slot Context must carry - Route String, Root Hub Port
 * Number, and the TT triple (Parent Hub, Parent Port, MTT) - typed out by hand
 * from the design record the row names, never recomputed through the code's
 * own shift (the test_ctx.c rule).
 *
 * TtHub is the TT hub's address in the graph; turning it into a Slot ID is the
 * caller's (design record 02 section 1). The speed gate is the caller's too:
 * `XhciTopoTtFor` answers for a device that is not High Speed, and design
 * record 13 section 10.4 gives a High-Speed device no TT at all, so the runner
 * applies that gate exactly once, below, and a High-Speed row expects zeros.
 */
#define TV_HUBS 6
#define TV_ROOT 0xFFUL
#define TV_HS   XHCI_SPEED_HIGH
#define TV_FS   XHCI_SPEED_FULL
#define TV_LS   XHCI_SPEED_LOW

typedef struct _TV_HUB {
    ULONG Address;
    ULONG Parent;       /* index into Hub[], or TV_ROOT                  */
    ULONG Port;         /* the root port under TV_ROOT, else the port on
                         * the parent                                    */
    ULONG Speed;
    ULONG MultiTt;      /* a SET_INTERFACE(1) completed on it            */
} TV_HUB;

typedef struct _TV_PLACEMENT {
    const char *Name;   /* the case, and the record it was typed from    */
    ULONG HubCount;
    TV_HUB Hub[TV_HUBS];
    ULONG DevParent;    /* index into Hub[]                              */
    ULONG DevPort;
    ULONG DevSpeed;
    ULONG TooDeep;
    ULONG Tier;
    ULONG Route;
    ULONG RootPort;
    ULONG TtHub;        /* 0: no TT                                      */
    ULONG TtPort;
    ULONG Mtt;
} TV_PLACEMENT;

/* Five hubs from root port 2, hub N on port N + 2 of the one above: the
 * chain buildChain() below makes, typed out for the table. */
#define TV_CHAIN5(sp) \
    { { 2, TV_ROOT, 2, sp, 0 }, { 3, 0, 3, sp, 0 }, { 4, 1, 4, sp, 0 }, \
      { 5, 2, 5, sp, 0 }, { 6, 3, 6, sp, 0 } }

static const TV_PLACEMENT placements[] = {
    { "FS device on a single-TT HS hub on a root port "
      "(DR02 s2 step 3; DR13 s10.4)",
      1, { { 2, TV_ROOT, 3, TV_HS, 0 } }, 0, 2, TV_FS,
      0, 1, 0x00002UL, 3, 2, 2, 0 },
    { "LS device on a single-TT HS hub (DR12 s3.6 and s8: a real HS hub, "
      "Full and Low Speed behind it, single TT)",
      1, { { 2, TV_ROOT, 3, TV_HS, 0 } }, 0, 4, TV_LS,
      0, 1, 0x00004UL, 3, 2, 4, 0 },
    { "FS device on a multi-TT HS hub (DR12 s8, multi TT; DR02 Step 3: "
      "the child's MTT follows the enabled alternate setting)",
      1, { { 2, TV_ROOT, 3, TV_HS, 1 } }, 0, 2, TV_FS,
      0, 1, 0x00002UL, 3, 2, 2, 1 },
    { "LS device on a multi-TT HS hub (DR12 s8, multi TT)",
      1, { { 2, TV_ROOT, 3, TV_HS, 1 } }, 0, 7, TV_LS,
      0, 1, 0x00007UL, 3, 2, 7, 1 },
    { "HS device on a HS hub carries no TT (DR02 s1; DR13 s10.4: "
      "Parent Hub Slot ID only if FS or LS, else 0)",
      1, { { 2, TV_ROOT, 3, TV_HS, 1 } }, 0, 2, TV_HS,
      0, 1, 0x00002UL, 3, 0, 0, 0 },
    { "FS hub on HS hub port 2, FS device on its port 1: the TT port is 2 "
      "(DR13 s10.4, the 2.2.1 example)",
      2, { { 2, TV_ROOT, 2, TV_HS, 0 }, { 3, 0, 2, TV_FS, 0 } }, 1, 1, TV_FS,
      0, 2, 0x00012UL, 2, 2, 2, 0 },
    { "the same below a multi-TT HS hub (DR02 Step 3; DR13 s10.4 MTT row)",
      2, { { 2, TV_ROOT, 2, TV_HS, 1 }, { 3, 0, 2, TV_FS, 0 } }, 1, 1, TV_FS,
      0, 2, 0x00012UL, 2, 2, 2, 1 },
    { "LS device two FS hubs below HS hub port 5: the TT port at any depth "
      "is the one on the HS hub (DR02 s1, PortNumber)",
      3, { { 2, TV_ROOT, 1, TV_HS, 0 }, { 3, 0, 5, TV_FS, 0 },
           { 4, 1, 3, TV_FS, 0 } }, 2, 2, TV_LS,
      0, 3, 0x00235UL, 1, 2, 5, 0 },
    { "HS hub below a multi-TT HS hub: the nearer one is the TT, with its "
      "own single TT (DR02 s1, nearest High-Speed ancestor)",
      2, { { 2, TV_ROOT, 4, TV_HS, 1 }, { 3, 0, 6, TV_HS, 0 } }, 1, 1, TV_FS,
      0, 2, 0x00016UL, 4, 3, 1, 0 },
    { "the measured all-FS bus: tier 2, route 0x00012, root port 6, parent "
      "hub 4 port 1, and no TT although usbport claimed hub 2 port 2 "
      "(DR02 Step 3 observed)",
      2, { { 2, TV_ROOT, 6, TV_FS, 0 }, { 4, 0, 2, TV_FS, 0 } }, 1, 1, TV_FS,
      0, 2, 0x00012UL, 6, 0, 0, 0 },
    { "FS device on FS hub port 1: route 0x00001 (DR02 Step 4 observed)",
      1, { { 2, TV_ROOT, 2, TV_FS, 0 } }, 0, 1, TV_FS,
      0, 1, 0x00001UL, 2, 0, 0, 0 },
    { "...moved to port 3: route rebuilt to 0x00003 (DR02 Step 4 observed)",
      1, { { 2, TV_ROOT, 2, TV_FS, 0 } }, 0, 3, TV_FS,
      0, 1, 0x00003UL, 2, 0, 0, 0 },
    { "LS device on a FS hub on a root port: no TT (roadmap 27-V.1's QEMU "
      "row; DR13 s10.4, an all-FS path)",
      1, { { 2, TV_ROOT, 5, TV_FS, 0 } }, 0, 4, TV_LS,
      0, 1, 0x00004UL, 5, 0, 0, 0 },
    { "five HS hubs, FS device at tier 5: five nibbles, TT the fifth hub "
      "(DR13 s10.3, depth; xhci-data-structures.md, Route String tier order)",
      5, TV_CHAIN5(TV_HS), 4, 7, TV_FS,
      0, 5, 0x76543UL, 2, 6, 7, 0 },
    { "five FS hubs, FS device at tier 5: the 27-V.1 chain, no TT "
      "(DR13 s10.3; roadmap 27-V.1)",
      5, TV_CHAIN5(TV_FS), 4, 7, TV_FS,
      0, 5, 0x76543UL, 2, 0, 0, 0 },
    { "a sixth hub in the chain: its device is too deep and the route is "
      "left as the parent's (DR13 s10.3; xhci_topo.h XHCI_TOPO_MAX_TIER)",
      6, { { 2, TV_ROOT, 2, TV_HS, 0 }, { 3, 0, 3, TV_HS, 0 },
           { 4, 1, 4, TV_HS, 0 }, { 5, 2, 5, TV_HS, 0 },
           { 6, 3, 6, TV_HS, 0 }, { 7, 4, 7, TV_HS, 0 } }, 5, 1, TV_FS,
      1, 6, 0x76543UL, 2, 0, 0, 0 },
    { "port 15 on a HS hub: nibble F (xHCI Table 6-4 footnote 106, as "
      "quoted in xhci-data-structures.md)",
      1, { { 2, TV_ROOT, 1, TV_HS, 0 } }, 0, 15, TV_FS,
      0, 1, 0x0000FUL, 1, 2, 15, 0 },
    { "port 16 clamps to F and the TT port stays 16: only the route clamps "
      "(footnote 106; DR13 s10.4)",
      1, { { 2, TV_ROOT, 1, TV_HS, 0 } }, 0, 16, TV_FS,
      0, 1, 0x0000FUL, 1, 2, 16, 0 },
    { "port 255 clamps to F (footnote 106)",
      1, { { 2, TV_ROOT, 1, TV_HS, 0 } }, 0, 255, TV_LS,
      0, 1, 0x0000FUL, 1, 2, 255, 0 },
    { "a hub on port 20, a device on port 14 below it: 0x000EF "
      "(footnote 106)",
      2, { { 2, TV_ROOT, 1, TV_HS, 0 }, { 3, 0, 20, TV_HS, 0 } }, 1, 14, TV_FS,
      0, 2, 0x000EFUL, 1, 3, 14, 0 }
};

#define TV_PLACEMENTS (sizeof(placements) / sizeof(placements[0]))

/*
 * Identify an attached hub the way the bus does (design record 13 section
 * 10.3 step 2, the descriptor), then select its multi-TT interface where the
 * row asks for it. The order is descriptor first, which is the order the
 * graph supports today; the bus's own order is known gap G1 below.
 */
static void identifyHub(ULONG address, ULONG multiTt)
{
    XHCI_SETUP_PACKET s;
    XHCI_TOPO_SNOOP snoop;

    s = hubDescRequest();
    XhciTopoObserveSetup(&topo, address, &s, &snoop);
    (void)foldReply(&topo, &snoop, hubDescBytes, 11);
    if (multiTt) {
        XhciTopoApplySetInterface(&topo, address, 1);
    }
}

static void runPlacement(const TV_PLACEMENT *v)
{
    XHCI_TOPO_CHILD at;
    XHCI_TOPO_TT tt;
    ULONG i;
    ULONG parent;
    ULONG haveTt;
    ULONG slotTtHub;
    ULONG slotTtPort;
    ULONG slotMtt;
    int before;

    before = failures;
    resetTopo();

    for (i = 0; i < v->HubCount; i++) {
        const TV_HUB *h;

        h = &v->Hub[i];
        if (h->Parent == TV_ROOT) {
            CHECK_EQ(XhciTopoAttachRoot(&topo, h->Address, h->Port, 1,
                                        h->Speed),
                     1, "placement: a hub attaches on its root port");
        } else {
            CHECK_EQ(XhciTopoChildOf(&topo, v->Hub[h->Parent].Address,
                                     h->Port, &at),
                     1, "placement: a hub has a position below its parent");
            CHECK_EQ(XhciTopoAttachChild(&topo, h->Address, &at, h->Speed),
                     1, "placement: a hub attaches below its parent");
        }
        identifyHub(h->Address, h->MultiTt);
    }

    parent = v->Hub[v->DevParent].Address;
    CHECK_EQ(XhciTopoChildOf(&topo, parent, v->DevPort, &at), 1,
             "placement: the device has a position");
    CHECK_EQ(at.TooDeep, v->TooDeep, "placement: too deep or not");
    CHECK_EQ(at.Tier, v->Tier, "placement: tier");
    CHECK_EQ(at.Route, v->Route, "placement: Route String");
    CHECK_EQ(at.RootPort, v->RootPort, "placement: Root Hub Port Number");
    CHECK_EQ(at.HubAddress, parent, "placement: the hub it is plugged into");
    CHECK_EQ(at.HubPort, v->DevPort, "placement: the port it is plugged into");

    if (v->TooDeep) {
        /* Refused, not truncated: nothing is attached and no slot is built,
         * so there is no TT triple to assert. */
        CHECK_EQ(XhciTopoAttachChild(&topo, 99, &at, v->DevSpeed), 0,
                 "placement: a too-deep position is refused");
    } else {
        haveTt = XhciTopoTtFor(&topo, parent, v->DevPort, &tt);
        if (v->DevSpeed == XHCI_SPEED_HIGH || !haveTt) {
            slotTtHub = 0;
            slotTtPort = 0;
            slotMtt = 0;
        } else {
            slotTtHub = tt.HubAddress;
            slotTtPort = tt.HubPort;
            slotMtt = tt.MultiTt;
        }
        CHECK_EQ(slotTtHub, v->TtHub, "placement: Parent (TT) Hub");
        CHECK_EQ(slotTtPort, v->TtPort, "placement: Parent (TT) Port Number");
        CHECK_EQ(slotMtt, v->Mtt, "placement: MTT");
    }

    if (failures != before) {
        printf("  ...in placement \"%s\"\n", v->Name);
    }
}

static void testPlacements(void)
{
    ULONG i;

    for (i = 0; i < TV_PLACEMENTS; i++) {
        runPlacement(&placements[i]);
    }
}

/*
 * A device on a root port is no position of the graph's: Route String 0,
 * Root Hub Port Number N, no TT fields (design record 12 section 3.6, the
 * first rule; xHCI 4.3.3 footnote 8). With no hub address there is nothing
 * to place it under and nothing to walk.
 */
static void testRootPortDevice(void)
{
    XHCI_TOPO_CHILD at;
    XHCI_TOPO_TT tt;

    resetTopo();
    XhciTopoAttachRoot(&topo, 2, 1, 1, XHCI_SPEED_HIGH);
    CHECK_EQ(XhciTopoChildOf(&topo, 0, 3, &at), 0,
             "a root-port device has no hub to be placed under");
    CHECK_EQ(XhciTopoTtFor(&topo, 0, 3, &tt), 0,
             "and no TT, whatever hub sits on another root port");
    CHECK_EQ(tt.HubAddress, 0, "(the TT answer is zeroed)");
}

/* Hub N of a chain on port N + 2 of hub N - 1, as TV_CHAIN5 types it. */
static ULONG buildChain(ULONG base, ULONG rootPort, ULONG count, ULONG speed)
{
    XHCI_TOPO_CHILD at;
    ULONG i;
    ULONG attached;

    attached = 0;
    if (count == 0) {
        return 0;
    }
    if (XhciTopoAttachRoot(&topo, base, rootPort, 1, speed)) {
        attached++;
    }
    for (i = 1; i < count; i++) {
        if (XhciTopoChildOf(&topo, base + i - 1, 2 + i, &at) &&
            XhciTopoAttachChild(&topo, base + i, &at, speed)) {
            attached++;
        }
    }
    return attached;
}

/*
 * The depth limit through the claim path the bus will use (design record 13
 * section 10.3): a sixth hub is placed - "the bus addresses a hub at any depth
 * it can route" - and a reset on one of its ports yields a claim that says
 * too deep, which is what tells the bus not to configure it.
 */
static void testDepthClaim(void)
{
    XHCI_SETUP_PACKET s;
    XHCI_TOPO_SNOOP snoop;
    XHCI_TOPO_CHILD at;

    resetTopo();
    CHECK_EQ(buildChain(2, 2, 5, XHCI_SPEED_HIGH), 5, "(five hubs)");
    CHECK_EQ(XhciTopoChildOf(&topo, 6, 7, &at), 1, "(a sixth position)");
    CHECK_EQ(at.TooDeep, 0, "the sixth hub itself is routable");
    CHECK_EQ(XhciTopoAttachChild(&topo, 7, &at, XHCI_SPEED_HIGH), 1,
             "and is placed");
    CHECK((nodeOrEmpty(XhciTopoFind(&topo, 7))->Flags &
           XHCI_TOPO_F_TOO_DEEP) == 0,
          "and not flagged: only its children are past the Route String");

    s = portReset(1);
    XhciTopoObserveSetup(&topo, 7, &s, &snoop);
    CHECK_EQ(claim(&topo, &at), 1, "a reset below the sixth hub is claimed");
    CHECK_EQ(at.TooDeep, 1, "and the claim says too deep");
    CHECK_EQ(at.Tier, 6, "at tier 6");
    CHECK_EQ(topo.ClaimsTooDeep, 1, "counted");
    CHECK_EQ(XhciTopoAttachChild(&topo, 8, &at, XHCI_SPEED_FULL), 0,
             "and nothing is placed from it");

    s = portReset(7);
    XhciTopoObserveSetup(&topo, 6, &s, &snoop);
    CHECK_EQ(claim(&topo, &at), 1, "a reset below the fifth hub is claimed");
    CHECK_EQ(at.TooDeep, 0, "and is addressable");
    CHECK_EQ(at.Route, 0x76543UL, "with all five nibbles");
    checkClaimIdentity(&topo, "depth claim");
}

/* `Topology.MaxTier` counts hubs only, so it reads 1 while a leaf sits at
 * tier 2 (design record 02, Step 3's last bullet). */
static void testMaxTierHubsOnly(void)
{
    XHCI_TOPO_CHILD at;

    resetTopo();
    XhciTopoAttachRoot(&topo, 2, 1, 1, XHCI_SPEED_HIGH);
    XhciTopoChildOf(&topo, 2, 1, &at);
    XhciTopoAttachChild(&topo, 3, &at, XHCI_SPEED_HIGH);
    CHECK_EQ(XhciTopoChildOf(&topo, 3, 1, &at), 1, "(a leaf position)");
    CHECK_EQ(at.Tier, 2, "the leaf is at tier 2");
    CHECK_EQ(topo.MaxTier, 1, "and MaxTier still reads the deepest hub");
}

/*
 * The FS hub's own Slot Context. Design record 02 Step 3: "A Full-Speed hub
 * behind a multi-TT High-Speed hub carries MTT for both reasons, so the hub
 * marking ORs the bit rather than assigning it"; design record 13 section
 * 10.4's MTT row says the same. The OR is the caller's - `XhciTopoHubMark`
 * gives a Full-Speed hub no MTT of its own (Table 6-4) and `XhciTopoTtFor` at
 * the hub's own position gives the parent's - so this checks the two halves
 * the caller ORs. Then TTT across all four think times, which only a
 * High-Speed hub carries (Table 6-6 p.409).
 */
static void testHubMarkVectors(void)
{
    static const ULONG tttRows[][2] = {
        /* wHubCharacteristics, TTT (bits 6:5) */
        { 0x0000UL, 0 }, { 0x0020UL, 1 }, { 0x0040UL, 2 }, { 0x0060UL, 3 },
        { 0x00E9UL, 3 }, { 0x0189UL, 0 }
    };
    XHCI_SETUP_PACKET s;
    XHCI_TOPO_SNOOP snoop;
    XHCI_TOPO_CHILD at;
    XHCI_TOPO_HUBMARK mark;
    XHCI_TOPO_TT tt;
    UCHAR d[9];
    ULONG i;

    resetTopo();
    XhciTopoAttachRoot(&topo, 2, 1, 1, XHCI_SPEED_HIGH);
    identifyHub(2, 1);
    XhciTopoChildOf(&topo, 2, 4, &at);
    XhciTopoAttachChild(&topo, 3, &at, XHCI_SPEED_FULL);
    identifyHub(3, 0);

    CHECK_EQ(XhciTopoHubMark(XhciTopoFind(&topo, 3), XHCI_SPEED_FULL, &mark),
             1, "the FS hub below the multi-TT hub is markable");
    CHECK_EQ(mark.Hub, 1, "Hub = 1");
    CHECK_EQ(mark.NumberOfPorts, 8, "its own port count");
    CHECK_EQ(mark.TtThinkTime, 0, "no TTT on a FS hub");
    CHECK_EQ(mark.MultiTt, 0, "and no MTT of its own");
    CHECK_EQ(XhciTopoTtFor(&topo, 2, 4, &tt), 1,
             "its own position is behind a TT");
    CHECK_EQ(tt.HubAddress, 2, "the multi-TT hub's");
    CHECK_EQ(tt.HubPort, 4, "on the port the FS hub is plugged into");
    CHECK_EQ(mark.MultiTt | tt.MultiTt, 1,
             "so its Slot Context carries MTT 1 by the OR");

    /* The FS hub on a root port: DR02 Step 1's observed marking, Hub = 1,
     * Number of Ports = 8, TTT = 0, MTT = 0, and no TT for its own slot. */
    resetTopo();
    XhciTopoAttachRoot(&topo, 2, 1, 1, XHCI_SPEED_FULL);
    identifyHub(2, 0);
    CHECK_EQ(XhciTopoHubMark(XhciTopoFind(&topo, 2), XHCI_SPEED_FULL, &mark),
             1, "QEMU's usb-hub shape is markable");
    CHECK_EQ(mark.Hub, 1, "Hub = 1 (DR02 Step 1)");
    CHECK_EQ(mark.NumberOfPorts, 8, "Number of Ports = 8 (DR02 Step 1)");
    CHECK_EQ(mark.TtThinkTime, 0, "TTT = 0 (DR02 Step 1)");
    CHECK_EQ(mark.MultiTt, 0, "MTT = 0 (DR02 Step 1)");

    for (i = 0; i < sizeof(tttRows) / sizeof(tttRows[0]); i++) {
        resetTopo();
        d[0] = 9;
        d[1] = 0x29;
        d[2] = 4;
        d[3] = (UCHAR)(tttRows[i][0] & 0xFF);
        d[4] = (UCHAR)(tttRows[i][0] >> 8);
        d[5] = 50;
        d[6] = 100;
        d[7] = 0x00;
        d[8] = 0xFF;
        XhciTopoAttachRoot(&topo, 2, 1, 1, XHCI_SPEED_HIGH);
        s = hubDescRequest();
        XhciTopoObserveSetup(&topo, 2, &s, &snoop);
        CHECK_EQ(foldReply(&topo, &snoop, d, 9), 1, "(descriptor folds)");
        (void)XhciTopoHubMark(XhciTopoFind(&topo, 2), XHCI_SPEED_HIGH, &mark);
        CHECK_EQ(mark.TtThinkTime, tttRows[i][1],
                 "a HS hub's TTT is wHubCharacteristics bits 6:5");
        (void)XhciTopoHubMark(XhciTopoFind(&topo, 2), XHCI_SPEED_FULL, &mark);
        CHECK_EQ(mark.TtThinkTime, 0, "and the same hub at FS carries none");
    }
}

/*
 * Removal (design record 02 "Step 4 observed"; design record 13 section
 * 10.5). The bus of the observed run: hub 1 (address 2) on root port 2, hub
 * 2 (address 4) on its port 2, live devices on hub 1 ports 1 and 3 and on
 * hub 2 port 1. Leaves are not graph nodes, so "gone 2 -> 5" is the caller's
 * count of records; the graph's reading is its two hub nodes.
 */
static void buildRemovalBus(void)
{
    XHCI_SETUP_PACKET s;
    XHCI_TOPO_SNOOP snoop;
    XHCI_TOPO_CHILD at;
    ULONG p;

    resetTopo();
    XhciTopoAttachRoot(&topo, 2, 2, 1, XHCI_SPEED_FULL);
    XhciTopoChildOf(&topo, 2, 2, &at);
    XhciTopoAttachChild(&topo, 4, &at, XHCI_SPEED_FULL);
    for (p = 1; p <= 3; p++) {
        s = portStatus((USHORT)p);
        XhciTopoObserveSetup(&topo, 2, &s, &snoop);
        (void)foldReply(&topo, &snoop, portConnectedBytes, 4);
    }
    s = portStatus(1);
    XhciTopoObserveSetup(&topo, 4, &s, &snoop);
    (void)foldReply(&topo, &snoop, portConnectedBytes, 4);
}

static void testRemovalVectors(void)
{
    XHCI_SETUP_PACKET s;
    XHCI_TOPO_SNOOP snoop;
    XHCI_TOPO_CHILD at;
    UCHAR shortReconnect[4] = { 0x01, 0x01, 0x01, 0x00 };

    /* The root port goes: everything behind it, both hub nodes. */
    buildRemovalBus();
    CHECK_EQ(topo.Count, 2, "(two hub nodes)");
    XhciTopoDetach(&topo, 2);
    CHECK_EQ(topo.Count, 0, "a root port going down sweeps the subtree");
    CHECK_EQ(topo.Prunes, 2, "both hub nodes pruned (DR02 Step 4)");
    CHECK(XhciTopoFind(&topo, 4) == NULL, "the hub below went too");

    /* Hub 2 pulled from hub 1's port 2, with a reset armed on its own port:
     * the departure names (2, 2), hub 1 stays, and the claim dies with the
     * hub it named rather than being handed to the next open. */
    buildRemovalBus();
    s = portReset(1);
    XhciTopoObserveSetup(&topo, 4, &s, &snoop);
    CHECK_EQ(topo.Pending, 1, "(a reset armed below hub 2)");
    s = portStatus(2);
    XhciTopoObserveSetup(&topo, 2, &s, &snoop);
    CHECK_EQ(foldReply(&topo, &snoop, portEmptyBytes, 4), 1, "(port 2 empty)");
    CHECK_EQ(lastGone.Disconnected, 1, "the departure is reported");
    CHECK_EQ(lastGone.HubAddress, 2, "on hub 1");
    CHECK_EQ(lastGone.HubPort, 2, "port 2");
    CHECK(XhciTopoFind(&topo, 4) == NULL, "hub 2's node pruned");
    CHECK(XhciTopoFind(&topo, 2) != NULL, "hub 1's node kept");
    CHECK_EQ(topo.Prunes, 1, "one node pruned");
    CHECK_EQ(topo.Pending, 0, "the claim naming hub 2 died with it");
    CHECK_EQ(claim(&topo, &at), 0, "so the next open is unarmed");

    /* The same claim, with the root-tier hub going instead: the orphan sweep
     * that takes hub 2 drops the claim as well. */
    buildRemovalBus();
    s = portReset(1);
    XhciTopoObserveSetup(&topo, 4, &s, &snoop);
    XhciTopoDetach(&topo, 2);
    CHECK_EQ(topo.Pending, 0,
             "a claim on a grandchild hub dies in the orphan sweep");

    /* A swap between polls on the port holding hub 2 (DR02's graph table,
     * B10): connected, change bit set - a departure, and the subtree goes. */
    buildRemovalBus();
    s = portStatus(2);
    XhciTopoObserveSetup(&topo, 2, &s, &snoop);
    CHECK_EQ(foldReply(&topo, &snoop, portReconnectBytes, 4), 1,
             "(connected again, change bit set)");
    CHECK_EQ(lastGone.Disconnected, 1, "a hub swapped between polls is gone");
    CHECK(XhciTopoFind(&topo, 4) == NULL, "and its node pruned");

    /* Short GET_STATUS replies: two bytes carry no change word, so a swap
     * cannot be seen and none is invented; one byte is no reading. */
    buildRemovalBus();
    s = portStatus(3);
    XhciTopoObserveSetup(&topo, 2, &s, &snoop);
    CHECK_EQ(foldReply(&topo, &snoop, shortReconnect, 2), 1,
             "(a two-byte reply folds)");
    CHECK_EQ(lastGone.Disconnected, 0,
             "and invents no departure from a change word it never got");
    CHECK_EQ(foldReply(&topo, &snoop, portEmptyBytes, 1), 0,
             "a one-byte reply is no reading");
    CHECK_EQ(lastGone.Disconnected, 0, "and reports nothing");
    CHECK_EQ(nodeOrEmpty(XhciTopoFind(&topo, 2))->Connected, 0x7,
             "ports 1 to 3 still read connected");
    checkClaimIdentity(&topo, "removal vectors");
}

/*
 * Malformed and edge-case hub descriptors, against design record 02's rule:
 * self-consistency, not the type byte - the declared length must cover the
 * fields read (5) and must not claim more than arrived. A refused reply moves
 * no number and leaves the hub unmarkable; an accepted one is marked from
 * `bNbrPorts` unless that is 0. Whether a declared length below
 * 7 + 2 x ceil((ports + 1) / 8) should be refused is design record 13 section
 * 10.1's formula, marked there "to transcribe", so the 5-byte row records
 * today's acceptance and decides nothing.
 */
typedef struct _TV_HUBDESC {
    const char *What;
    UCHAR Bytes[11];
    ULONG Length;
    ULONG Folds;
    ULONG Ports;
} TV_HUBDESC;

static const TV_HUBDESC hubDescRows[] = {
    { "nothing arrived",
      { 11, 0x29, 8, 0x40, 0x00 }, 0, 0, 0 },
    { "four bytes: no wHubCharacteristics",
      { 11, 0x29, 8, 0x40, 0x00 }, 4, 0, 0 },
    { "declared length 0",
      { 0, 0x29, 8, 0x40, 0x00 }, 5, 0, 0 },
    { "declared length 4, short of the fields read",
      { 4, 0x29, 8, 0x40, 0x00 }, 5, 0, 0 },
    { "declared 255 of 11 arrived",
      { 255, 0x29, 8, 0x40, 0x00, 50, 100, 0x00, 0x00, 0xFF, 0xFF }, 11, 0, 0 },
    { "declared 11 of 9 arrived: a short packet",
      { 11, 0x29, 8, 0x40, 0x00, 50, 100, 0x00, 0x00, 0xFF, 0xFF }, 9, 0, 0 },
    { "declared 5, exactly the fields read",
      { 5, 0x29, 4, 0x20, 0x00 }, 5, 1, 4 },
    { "the measured 8-port descriptor, 71 asked and 11 moved",
      { 11, 0x29, 8, 0x40, 0x00, 50, 100, 0x00, 0x00, 0xFF, 0xFF }, 11, 1, 8 },
    { "255 ports",
      { 11, 0x29, 255, 0x00, 0x00, 50, 100, 0x00, 0x00, 0xFF, 0xFF }, 11, 1,
      255 },
    { "type byte 0: recorded, not required",
      { 9, 0x00, 4, 0x00, 0x00, 50, 100, 0x00, 0xFF }, 9, 1, 4 },
    { "no ports: folds, is counted, is never marked",
      { 9, 0x29, 0, 0x00, 0x00, 50, 100, 0x00, 0xFF }, 9, 1, 0 }
};

static void testMalformedDescriptors(void)
{
    XHCI_SETUP_PACKET s;
    XHCI_TOPO_SNOOP snoop;
    XHCI_TOPO_HUBMARK mark;
    const XHCI_TOPO_NODE *node;
    ULONG i;
    int before;

    for (i = 0; i < sizeof(hubDescRows) / sizeof(hubDescRows[0]); i++) {
        const TV_HUBDESC *r;

        r = &hubDescRows[i];
        before = failures;
        resetTopo();
        XhciTopoAttachRoot(&topo, 2, 1, 1, XHCI_SPEED_HIGH);
        s = hubDescRequest();
        XhciTopoObserveSetup(&topo, 2, &s, &snoop);
        CHECK_EQ(foldReply(&topo, &snoop, r->Bytes, r->Length), r->Folds,
                 "descriptor: folds or is refused");
        CHECK_EQ(topo.DescriptorsBad, r->Folds ? 0 : 1,
                 "descriptor: a refusal is counted bad");
        CHECK_EQ(topo.DescriptorsNoPorts, (r->Folds && r->Ports == 0) ? 1 : 0,
                 "descriptor: a zero-port fold is counted apart");
        node = XhciTopoFind(&topo, 2);
        CHECK_EQ((nodeOrEmpty(node)->Flags & XHCI_TOPO_F_DESCRIPTOR) != 0,
                 r->Folds, "descriptor: facts taken only from a fold");
        CHECK_EQ(nodeOrEmpty(node)->PortCount, r->Ports,
                 "descriptor: bNbrPorts, or nothing");
        CHECK_EQ(XhciTopoHubMark(node, XHCI_SPEED_HIGH, &mark),
                 (r->Folds && r->Ports != 0) ? 1 : 0,
                 "descriptor: markable only with ports");
        CHECK_EQ(mark.NumberOfPorts, r->Folds ? r->Ports : 0,
                 "descriptor: Number of Ports");
        if (failures != before) {
            printf("  ...in hub descriptor row \"%s\"\n", r->What);
        }
    }

    /* A reply the snoop did not ask for, and no bytes at all. */
    resetTopo();
    XhciTopoAttachRoot(&topo, 2, 1, 1, XHCI_SPEED_HIGH);
    s = portPower(1);
    XhciTopoObserveSetup(&topo, 2, &s, &snoop);
    CHECK_EQ(foldReply(&topo, &snoop, hubDescBytes, 11), 0,
             "a reply to a request with no reply is not folded");
    s = hubDescRequest();
    XhciTopoObserveSetup(&topo, 2, &s, &snoop);
    CHECK_EQ(foldReply(&topo, &snoop, NULL, 11), 0, "no bytes, no fold");
    CHECK_EQ(topo.Descriptors, 0, "neither counted as a descriptor");
}

/* ------------------------------------------------------------------ */
/* Task 27-A.4: the known gaps                                         */
/* ------------------------------------------------------------------ */

static void testKnownGaps(void)
{
    XHCI_SETUP_PACKET s;
    XHCI_TOPO_SNOOP snoop;
    XHCI_TOPO_CHILD at;
    XHCI_TOPO_HUBMARK mark;
    XHCI_TOPO_TT tt;
    ULONG held;
    UCHAR disabledBytes[4] = { 0x01, 0x01, 0x02, 0x00 };

    /*
     * G1. Design record 13 section 10.3 brings a hub up as SET_CONFIGURATION,
     * then SET_INTERFACE alternate 1 on a multi-TT hub (step 1), then
     * GET_DESCRIPTOR(Hub) (step 2), then the marking (step 3), and feeds the
     * graph "through the same entry points the miniport fed from its snoop".
     * `XhciTopoApplySetInterface` ignores a SET_INTERFACE on a device not yet
     * known as a hub, and nothing before step 2 makes it one, so the multi-TT
     * selection is lost: the hub is marked MTT 0 and every FS/LS child gets
     * MTT 0. Under usbhub the descriptor came first; under the bus it does
     * not. The fix is in xhci_topo.c or in the order the caller reports, and
     * is not this suite's to choose.
     */
    resetTopo();
    XhciTopoAttachRoot(&topo, 2, 1, 1, XHCI_SPEED_HIGH);
    XhciTopoApplySetInterface(&topo, 2, 1);
    s = setupOf(0xA0, 0x06, 0x2900, 0, 71);
    XhciTopoObserveSetup(&topo, 2, &s, &snoop);
    (void)foldReply(&topo, &snoop, hubDescBytes, 11);
    (void)XhciTopoHubMark(XhciTopoFind(&topo, 2), XHCI_SPEED_HIGH, &mark);
    KNOWN_GAP(mark.MultiTt == 1, "G1 (DR13 s10.3 steps 1-3)",
              "a multi-TT hub whose SET_INTERFACE(1) completes before its "
              "hub descriptor is marked MTT 0, want 1");
    (void)XhciTopoTtFor(&topo, 2, 3, &tt);
    KNOWN_GAP(tt.MultiTt == 1, "G1 (DR13 s10.3, s10.4 MTT row)",
              "...and an FS/LS device below it gets MTT 0, want 1");

    /*
     * G2. Design record 13 section 10.3: `XHCI_TOPO_NODES` 8, the miniport's
     * table, "becomes a pool-backed count for a bus that owns every hub".
     * Roadmap 27-V.1 runs hubs behind hubs to the depth limit; two such
     * chains on two root ports are ten hubs, and the table drops two.
     */
    resetTopo();
    held = buildChain(10, 1, 5, XHCI_SPEED_HIGH) +
           buildChain(20, 2, 5, XHCI_SPEED_HIGH);
    KNOWN_GAP(held == 10 && topo.Dropped == 0, "G2 (DR13 s10.3)",
              "two five-hub chains need 10 graph nodes; XHCI_TOPO_NODES is "
              "8 and the rest are dropped");

    /*
     * G3. Design record 13 section 10.5 names three removal triggers for a
     * hub's subtree; one is "the parent disables the hub (C_PORT_ENABLE with
     * enable 0)". The reply fold reports a departure only from the connect
     * bit and C_PORT_CONNECTION (design record 02's disconnect row), so a
     * disabled port that stays connected reports nothing and keeps the hub
     * node below it. Either the fold or the bus's own port machine
     * (test\hub_port_vectors.h has the row) must carry it.
     */
    resetTopo();
    XhciTopoAttachRoot(&topo, 2, 1, 1, XHCI_SPEED_HIGH);
    XhciTopoChildOf(&topo, 2, 2, &at);
    XhciTopoAttachChild(&topo, 3, &at, XHCI_SPEED_FULL);
    s = portStatus(2);
    XhciTopoObserveSetup(&topo, 2, &s, &snoop);
    (void)foldReply(&topo, &snoop, portConnectedBytes, 4);
    (void)foldReply(&topo, &snoop, disabledBytes, 4);
    KNOWN_GAP(lastGone.Disconnected == 1 && XhciTopoFind(&topo, 3) == NULL,
              "G3 (DR13 s10.5)",
              "a GET_STATUS reply with C_PORT_ENABLE set and enable 0 under "
              "a hub reports no departure and prunes nothing");
}

/* ------------------------------------------------------------------ */
/* Task 27-A.4: the hub port vector table is well formed               */
/* ------------------------------------------------------------------ */

/* Written apart from any decoder in src\: LS bit, HS bit, neither is FS
 * (design record 13 section 10.2, hub step 5). */
static ULONG hpvSpeedOf(ULONG status)
{
    if ((status & XHCI_HUB_PORT_LOW_SPEED) != 0) {
        return XHCI_SPEED_LOW;
    }
    if ((status & XHCI_HUB_PORT_HIGH_SPEED) != 0) {
        return XHCI_SPEED_HIGH;
    }
    return XHCI_SPEED_FULL;
}

static ULONG hpvEnumerating(ULONG state)
{
    return (state == HPV_ST_RESETTING || state == HPV_ST_RECOVERY ||
            state == HPV_ST_ADDRESSING) ? 1UL : 0UL;
}

static ULONG hpvHoldsDevice(ULONG state)
{
    return (state == HPV_ST_ENABLED || state == HPV_ST_SUSPENDED ||
            state == HPV_ST_RESUMING || state == HPV_ST_RESUME_RECOVERY) ?
           1UL : 0UL;
}

/* Each change bit and the CLEAR_FEATURE that acknowledges it. */
static const ULONG hpvClears[][2] = {
    { XHCI_HUB_C_PORT_CONNECTION,   HPV_ACT_CLEAR_C_CONNECTION },
    { XHCI_HUB_C_PORT_ENABLE,       HPV_ACT_CLEAR_C_ENABLE },
    { XHCI_HUB_C_PORT_SUSPEND,      HPV_ACT_CLEAR_C_SUSPEND },
    { XHCI_HUB_C_PORT_OVER_CURRENT, HPV_ACT_CLEAR_C_OVER_CURRENT },
    { XHCI_HUB_C_PORT_RESET,        HPV_ACT_CLEAR_C_RESET }
};

static void testHubPortVectorTable(void)
{
    const ULONG knownStatus = XHCI_HUB_PORT_CONNECTION | XHCI_HUB_PORT_ENABLE |
                              XHCI_HUB_PORT_SUSPEND |
                              XHCI_HUB_PORT_OVER_CURRENT |
                              XHCI_HUB_PORT_RESET | XHCI_HUB_PORT_POWER |
                              XHCI_HUB_PORT_LOW_SPEED |
                              XHCI_HUB_PORT_HIGH_SPEED;
    ULONG seen[HPV_ST_COUNT];
    ULONG i;
    ULONG j;
    ULONG k;
    ULONG decodes;
    int before;

    for (i = 0; i < HPV_ST_COUNT; i++) {
        seen[i] = 0;
    }

    for (i = 0; i < HPV_ROWS; i++) {
        const HPV_ROW *r;

        r = &hpvRows[i];
        before = failures;

        CHECK(r->State < HPV_ST_COUNT && r->NextState < HPV_ST_COUNT,
              "hpv: states in range");
        CHECK(r->Event >= 1 && r->Event < HPV_EV_COUNT, "hpv: event in range");
        CHECK(r->Attempt <= 3, "hpv: at most three attempts (10.2 step 7)");
        CHECK((r->PortChange & ~XHCI_HUB_C_PORT_MASK) == 0,
              "hpv: only the five port change bits");
        CHECK((r->PortStatus & ~knownStatus) == 0, "hpv: known status bits");
        CHECK((r->PortStatus & XHCI_HUB_PORT_LOW_SPEED) == 0 ||
              (r->PortStatus & XHCI_HUB_PORT_HIGH_SPEED) == 0,
              "hpv: never both speed bits");

        if (r->Event == HPV_EV_STATUS) {
            CHECK((r->PortStatus & XHCI_HUB_PORT_POWER) != 0,
                  "hpv: a status read is of a powered port");
            /* Design record 13 section 10.1: "clear each with its
             * CLEAR_FEATURE C_*" - and nothing that was not reported. */
            for (k = 0; k < sizeof(hpvClears) / sizeof(hpvClears[0]); k++) {
                CHECK_EQ((r->PortChange & hpvClears[k][0]) != 0,
                         (r->Actions & hpvClears[k][1]) != 0,
                         "hpv: a change bit is cleared exactly when read");
            }
        } else {
            CHECK(r->PortStatus == 0 && r->PortChange == 0,
                  "hpv: only a status read carries status");
            for (k = 0; k < sizeof(hpvClears) / sizeof(hpvClears[0]); k++) {
                CHECK((r->Actions & hpvClears[k][1]) == 0,
                      "hpv: nothing to clear without a status read");
            }
        }

        decodes = (r->Event == HPV_EV_STATUS &&
                   r->State == HPV_ST_RESETTING &&
                   (r->PortChange & XHCI_HUB_C_PORT_RESET) != 0 &&
                   (r->PortStatus & XHCI_HUB_PORT_ENABLE) != 0) ? 1UL : 0UL;
        CHECK_EQ(r->Speed != XHCI_SPEED_UNKNOWN, decodes,
                 "hpv: a speed is decoded exactly where a reset ends enabled");
        if (decodes) {
            CHECK_EQ(r->Speed, hpvSpeedOf(r->PortStatus),
                     "hpv: and it is the speed the status bits say");
        }

        /* 10.2 step 3: the lock is taken entering enumeration and released
         * on every way out of it, including an unplug (10.5 step 1). */
        CHECK_EQ((r->Actions & HPV_ACT_ENUM_LOCK) != 0,
                 !hpvEnumerating(r->State) && hpvEnumerating(r->NextState),
                 "hpv: the enumeration lock is taken on the way in");
        CHECK_EQ((r->Actions & HPV_ACT_ENUM_UNLOCK) != 0,
                 hpvEnumerating(r->State) && !hpvEnumerating(r->NextState),
                 "hpv: and released on every way out");

        if ((r->Actions & HPV_ACT_REPORT_GONE) != 0) {
            CHECK(hpvHoldsDevice(r->State),
                  "hpv: only a port that held a device reports one gone");
        }
        if (hpvHoldsDevice(r->State) && r->Event == HPV_EV_STATUS &&
            (r->PortChange & (XHCI_HUB_C_PORT_CONNECTION |
                              XHCI_HUB_C_PORT_ENABLE)) != 0) {
            CHECK((r->Actions & HPV_ACT_REPORT_GONE) != 0,
                  "hpv: a lost connection or enable reports the device gone");
        }

        CHECK_EQ((r->Actions & HPV_ACT_ADDRESS) != 0,
                 r->NextState == HPV_ST_ADDRESSING &&
                 r->State != HPV_ST_ADDRESSING,
                 "hpv: Address Device exactly on entering ADDRESSING");
        CHECK_EQ((r->Actions & HPV_ACT_SET_RESET) != 0,
                 (r->Actions & HPV_ACT_TIMER_RESET) != 0,
                 "hpv: every port reset arms its time-out");
        if (r->Attempt == 3) {
            CHECK((r->Actions & HPV_ACT_SET_RESET) == 0,
                  "hpv: the third attempt is the last");
        }

        for (j = 0; j < i; j++) {
            CHECK(!(hpvRows[j].State == r->State &&
                    hpvRows[j].Event == r->Event &&
                    hpvRows[j].Attempt == r->Attempt &&
                    hpvRows[j].PortStatus == r->PortStatus &&
                    hpvRows[j].PortChange == r->PortChange),
                  "hpv: one row per input");
        }

        if (r->State < HPV_ST_COUNT) {
            seen[r->State] = 1;
        }
        if (failures != before) {
            printf("  ...in hub port row %lu \"%s\"\n", i, r->What);
        }
    }

    for (i = 0; i < HPV_ST_COUNT; i++) {
        CHECK(seen[i], "hpv: every state has a row leaving it");
    }
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

    /* Task 27-A.4. */
    testPlacements();
    testRootPortDevice();
    testDepthClaim();
    testMaxTierHubsOnly();
    testHubMarkVectors();
    testRemovalVectors();
    testMalformedDescriptors();
    testKnownGaps();
    testHubPortVectorTable();

    checkClaimIdentity(&topo, "end of main");
    /* The never-reset twin: an identity that never saw a claim would pass
     * every run as a net over nothing (task 7b-A.1.0's rule). */
    CHECK(claimCallsEver >= 5, "the claim identity measured real claims");

    /* Reported, never counted as failures (see the file header). */
    printf("%d known gaps open, %d now holding\n", gapsOpen, gapsHeld);
    printf("%d checks, %d failures\n", checks, failures);
    return failures;
}
