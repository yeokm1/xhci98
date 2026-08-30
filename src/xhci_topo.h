/*
 * xhci_topo.h - the hub topology graph (src/xhci_topo.c), roadmap task 7b-A.1.
 *
 * usbport hands the miniport four topology-bearing values and no route at all
 * (`DeviceAddress`, `DeviceSpeed`, `HubAddr`, `PortNumber` -
 * docs/contributing/design/02-hub-topology-route-string.md section 1, binary-confirmed
 * by batch 7a-0). Two of those name the *transaction translator* a device sits
 * behind; none of them names the path from the root port, the hub's port count,
 * its think time, or its multi-TT mode. So the tree has to be reconstructed by
 * watching the hub-class traffic `usbhub.sys` sends through this driver's own
 * `SubmitTransfer`, which batch 7b-V0 confirmed at runtime on both shipping
 * builds.
 *
 * ## What this file is, and what it deliberately is not
 *
 * It is the **graph**: what has been learned about which devices are hubs,
 * where they sit, and what is plugged into them. It decides nothing *itself* -
 * every function mutates only the topology it is handed and returns a reading.
 * Task 7b-A.2 consumes it through `XhciTopoHubMark` to mark a hub's own Slot
 * Context, and task 7b-A.3 consumes it to build a downstream device's Route
 * String and TT fields *(written as a task that "will consume" it until
 * a later review; it has been landed since Phase 7b)*. Beside those, the release-build counters are what
 * say whether the reconstruction *works* - the question 7b-A.1's own stop
 * rule turns on ("if neither direct fields nor snooping can reconstruct the
 * path reliably on both shipping usbport builds, stop this subphase").
 *
 * It is **pure core** in the design doc 03 section 2 sense: computation over
 * caller-supplied state, no MMIO, no DDK, no IRQL, no usbport service, no
 * lock. Like src/xhci_xfer.c it speaks the usbport transfer ABI, so it includes
 * src/xhci_usbport.h - itself DDK-free. The caller owns the lock discipline:
 * every function here mutates only the XHCI_TOPOLOGY it is handed, and the
 * driver keeps that inside the controller lock.
 *
 * It never dereferences a pointer usbport owns. The one place a reply's bytes
 * are needed, the *caller* reads them out of the mapped transfer buffer and
 * passes a byte pointer plus a length; nothing here holds a pointer across a
 * callback.
 *
 * ## The channel, as measured rather than assumed
 *
 * Batch 7b-V0 and the batch 7b-A.1.0 run recorded the
 * exact hub-class traffic on both targets. `vm\win2k-qemu-trace.batch7bv0-run1`
 * is the primary evidence for every constant below, because it is the bus's own
 * view rather than this driver's:
 *
 *   dev 2, req 0x2303, value 8,  index 1..8   SET_FEATURE(PORT_POWER) per port
 *   dev 2, req 0x2301, value 16, index 1..8   CLEAR_FEATURE(C_PORT_CONNECTION)
 *   dev 2, req 0x2303, value 4,  index 1      SET_FEATURE(PORT_RESET)
 *   dev 2, req 0x2301, value 17 / 20, index 1 CLEAR_FEATURE(C_PORT_ENABLE / _RESET)
 *   dev 2, req 0xa300, value 0,  index N, length 4    GET_STATUS(port N)
 *   dev 2, req 0xa006, value 0,  index 0, length 71   GET_DESCRIPTOR(Hub)
 *
 * Two things in that list are the reason a graph written from the USB 2.0 hub
 * specification alone would find nothing:
 *
 *   - **`GET_DESCRIPTOR(Hub)` carries `wValue = 0x0000`**, not the `0x2900` the
 *     spec says a caller should send - on *both* shipping hub drivers. The
 *     request is identified by `bmRequestType` and `bRequest`; the descriptor
 *     type is read from the reply.
 *   - **The port feature selector lives in the low half of `wValue`**, which
 *     this driver's own probe key drops, so `PORT_POWER` and `PORT_RESET` are
 *     indistinguishable there. This file reads `wValue` whole.
 *
 * ## Where the numbers come from
 *
 * `bNbrPorts` at descriptor offset 2 and `wHubCharacteristics` at 3:4 are the
 * Win2000 DDK's own `USB_HUB_DESCRIPTOR` (`C:\NTDDK\inc\usb100.h`, inside
 * `PSHPACK1.H`), corroborated by the measured `wLength = 71` = a 7-byte header
 * plus the 64-byte removable/power mask. The descriptor *type* value is not in
 * the DDK header of this era, so it is **recorded and checked rather than
 * required**: the fold accepts a self-consistent descriptor and reports what
 * type byte it actually saw, so a run measures the constant instead of a wrong
 * constant silently refusing every hub.
 *
 * The Route String's nibble order is **not** in the local spec PDF - xHCI 6.2.2
 * Table 6-4 defers the format to USB3 section 8.9, which is not in
 * docs/references. See docs/usb-xhci-info/xhci-data-structures.md, "Route String tier order",
 * for where the order came from and for the batch 7b-A runs that now anchor it:
 * the controller itself resolved this driver's own route strings to the devices
 * physically there, two tiers deep, which outranks the mirrored implementations
 * the order was first read off.
 *
 * C89 only. Every function is callable at any IRQL.
 */

#ifndef XHCI_TOPO_H
#define XHCI_TOPO_H

/*
 * Only the compat shim, deliberately: XHCI_EXTENSION embeds XHCI_TOPOLOGY, so
 * src/xhci.h includes this header the way it includes src/xhci_probe.h - which
 * means nothing here may include xhci.h back, and the usbport ABI must stay out
 * of it too (src/xhci_xfer.h's rule: every pure file includes xhci.h, and the
 * ABI declarations do not belong in all of them). The setup packet is taken
 * through a forward declaration; src/xhci_topo.c includes the real headers.
 */
#include "xhci_compat.h"

struct _XHCI_SETUP_PACKET;

/* ------------------------------------------------------------------ */
/* The wire vocabulary                                                 */
/* ------------------------------------------------------------------ */

/*
 * `bmRequestType` values, whole rather than by field. A hub is identified by
 * the *recipient* as well as the type: batch 6-V's classifier read eleven
 * "hub-class setups" on a bus with no hub on it because an audio class driver's
 * requests are class-type at an interface recipient. Only a device recipient
 * (`0x?0`) and an other recipient (`0x?3`) carry hub traffic.
 */
#define XHCI_TOPO_RT_HUB_DESC       0xA0U   /* IN  | class | device recipient */
#define XHCI_TOPO_RT_PORT_OUT       0x23U   /* OUT | class | other recipient  */
#define XHCI_TOPO_RT_PORT_IN        0xA3U   /* IN  | class | other recipient  */
#define XHCI_TOPO_RT_SET_INTERFACE  0x01U   /* OUT | standard | interface     */

/* bRequest, from C:\NTDDK\inc\usb100.h (USB_REQUEST_*). */
#define XHCI_TOPO_REQ_GET_STATUS    0x00U
#define XHCI_TOPO_REQ_CLEAR_FEATURE 0x01U
#define XHCI_TOPO_REQ_SET_FEATURE   0x03U
#define XHCI_TOPO_REQ_GET_DESCRIPTOR 0x06U
#define XHCI_TOPO_REQ_SET_INTERFACE 0x0BU

/*
 * Port feature selectors, measured on the bus (see the file header) rather than
 * transcribed from a specification this repository does not hold. Only the four
 * the graph acts on are named; the rest are deliberately absent, because a
 * constant nothing reads is a constant nobody checks.
 */
#define XHCI_TOPO_SEL_PORT_RESET    4U
#define XHCI_TOPO_SEL_PORT_POWER    8U

/*
 * The hub descriptor's own type byte. **Checked, not required** - see the file
 * header. USB 2.0 section 11.23.2.1 assigns 0x29; that document is not in
 * docs/references, and no run has yet read the byte back, so this is the value
 * a match is reported against and never the gate a fold passes through.
 */
#define XHCI_TOPO_DESC_TYPE_HUB     0x29U

/* Offsets inside the reply, from the DDK's packed USB_HUB_DESCRIPTOR. */
#define XHCI_TOPO_HUBD_LENGTH       0U
#define XHCI_TOPO_HUBD_TYPE         1U
#define XHCI_TOPO_HUBD_PORTS        2U
#define XHCI_TOPO_HUBD_CHAR_LO      3U
#define XHCI_TOPO_HUBD_CHAR_HI      4U
/* A descriptor shorter than this cannot carry wHubCharacteristics at all. */
#define XHCI_TOPO_HUBD_MIN          5U

/*
 * `wHubCharacteristics` bits 6:5 are the TT Think Time
 * (docs/usb-xhci-info/xhci-data-structures.md section 8, Slot Context DW2 `TTT`). Kept here
 * as the shift/mask pair the Slot Context builder will want in task 7b-A.2,
 * beside the field it is extracted from.
 */
#define XHCI_TOPO_CHAR_TTT_SHIFT    5U
#define XHCI_TOPO_CHAR_TTT_MASK     0x3U

/* GET_STATUS(port) answers wPortStatus then wPortChange, little-endian, and
 * XHCI_HUB_PORT_CONNECTION (src/xhci.h) is bit 0 of the first. */
#define XHCI_TOPO_PORTSTAT_MIN      2U

/*
 * How many bytes of a reply the folds ever read. The caller may hand
 * XhciTopoObserveReply a buffer holding only this many bytes while passing the
 * *full* transferred count as `length` - the length is what the
 * self-consistency checks reason about, the buffer is what gets indexed, and
 * the asserts at the bottom of this header pin every indexed offset inside the
 * window so the two cannot drift apart.
 */
#define XHCI_TOPO_REPLY_BYTES       8U

/* ------------------------------------------------------------------ */
/* Shape                                                               */
/* ------------------------------------------------------------------ */

/*
 * Hubs only, never leaves.
 *
 * A leaf device's position is held by its own XHCI_DEVICE record, which the
 * driver already keeps one of per addressed device; putting every device here
 * as well would be two statements of one fact, and would spend the table on the
 * HID keyboards that can never be anybody's parent. What the graph must hold is
 * exactly what a *child* cannot derive for itself: where its parent hub sits.
 *
 * Five tiers is xHCI's own limit on the Route String, so a bus that needs more
 * than five hub nodes on one path is already unrepresentable. Eight is that
 * with room for hubs on other root ports, and `Dropped` is what says a ninth
 * was met - a full table silently forgetting one is the failure mode this
 * project has paid for twice (batch 6-0's `PassThru`, batch 7b-A.1.0's
 * accepted-open).
 */
#define XHCI_TOPO_NODES         8

/*
 * The Route String holds five 4-bit tiers (Slot Context DW0 `19:0`,
 * docs/usb-xhci-info/xhci-data-structures.md section 8). Tier 0 is a device on a root port,
 * so a device at tier 6 has no representable route and must be refused rather
 * than truncated - task 7b-A.3's clause, computed here.
 */
#define XHCI_TOPO_MAX_TIER      5

/*
 * "If HS or FS hub in the path supports more than 14 ports the associated Route
 * String Port field shall be set to 15" - xHCI 6.2.2 Table 6-4 footnote 106,
 * read from the local spec PDF. So a port number is clamped, not masked: 16
 * would otherwise alias onto tier 0.
 */
#define XHCI_TOPO_ROUTE_MAX_PORT 15

/*
 * How many downstream ports of one hub the connect bitmask covers. USB allows
 * 255; the measured QEMU hub has 8 and a bus-powered hub has 4 or 7. Ports
 * above this are still counted in `PortCount` and still claimable as a parent -
 * only the *disconnect* reading is unavailable for them, which is why the
 * shortfall has a counter of its own rather than being clamped silently.
 */
#define XHCI_TOPO_CONNECT_PORTS 31

/* Node flags */
#define XHCI_TOPO_F_USED        0x00000001UL
/* Hub-class port traffic was addressed to this device. Only a hub receives it,
 * and usbport answers the *root* hub itself through the RH_ callbacks, so this
 * is positive evidence of an external hub and needs no descriptor. */
#define XHCI_TOPO_F_HUB         0x00000002UL
/* PortCount and Characteristics came from a hub descriptor reply rather than
 * from the port-power sweep's high-water mark. */
#define XHCI_TOPO_F_DESCRIPTOR  0x00000004UL
/* The reply's type byte was XHCI_TOPO_DESC_TYPE_HUB. Recorded, not required. */
#define XHCI_TOPO_F_DESC_TYPE_OK 0x00000008UL
/* A SET_INTERFACE **completed successfully** selecting a nonzero alternate
 * setting on this hub. Table 6-4 scopes MTT to the *currently enabled*
 * interface, so this follows the request and is not decided once
 * (docs/usb-xhci-info/xhci-data-structures.md section 8) - and "enabled" is why
 * it follows the request's completion rather than its placement
 * (`XhciTopoApplySetInterface`). */
#define XHCI_TOPO_F_MTT         0x00000010UL
/* ...and a SET_INTERFACE succeeded at all, which is what separates "single-TT"
 * from "never asked". A hub whose multi-TT interface was never selected is
 * MTT = 0 either way; the pair is what says which reading it is - so a stalled
 * request must not set this one either, or a hub that refused would read as a
 * hub that answered "alternate 0". */
#define XHCI_TOPO_F_ALT_SEEN    0x00000020UL
/* This node's own path is deeper than the Route String can express. Kept as a
 * node rather than refused, so the reading is "the graph knows where it is and
 * cannot address it" rather than a silence. */
#define XHCI_TOPO_F_TOO_DEEP    0x00000040UL

typedef struct _XHCI_TOPO_NODE {
    ULONG Flags;
    ULONG Address;          /* usbport's device address, 1..127 */
    ULONG Speed;            /* XHCI_SPEED_*, as this driver decoded the port */

    /* Position. Tier 0 is a hub on a root port; Route is the path *to* this
     * hub, so a tier-0 hub's Route is 0 (xHCI 4.3.3 footnote 8: "the Route
     * String does not include the Root Hub Port Number"). */
    ULONG Tier;
    ULONG Route;
    ULONG RootPort;         /* 1-based xHCI port the path starts at */
    ULONG Generation;       /* that root port's shadow generation when learned */
    ULONG ParentAddress;    /* the hub above this one, or 0 for the root hub */
    ULONG ParentPort;       /* the port on it - the root port when parent is 0 */

    ULONG PortCount;        /* bNbrPorts, or the port-power sweep's high water */
    ULONG Characteristics;  /* wHubCharacteristics, 0 until a descriptor lands */
    ULONG DescriptorType;   /* the type byte the reply actually carried */

    /* Bit N-1: port N was last seen connected. Only the low
     * XHCI_TOPO_CONNECT_PORTS ports have one. */
    ULONG Connected;
    ULONG Disconnects;      /* 1 -> 0 transitions observed on this hub */
} XHCI_TOPO_NODE;

/*
 * What a claimed enumeration parent says about the device about to appear at
 * address 0. Everything the Address Device command will need except the TT
 * pair, which usbport supplies directly (batch 7a-0).
 */
typedef struct _XHCI_TOPO_CHILD {
    ULONG HubAddress;       /* the hub whose port was reset */
    ULONG HubPort;
    ULONG RootPort;
    ULONG Tier;             /* the child's, = parent tier + 1 */
    ULONG Route;            /* the child's */
    ULONG TooDeep;          /* nonzero: Tier > XHCI_TOPO_MAX_TIER, do not use */
} XHCI_TOPO_CHILD, *PXHCI_TOPO_CHILD;

/*
 * The transaction translator a non-High-Speed device sits behind (task
 * 7b-A.3), as the **graph** answers it.
 *
 * usbport answers the same question directly - `HubAddr`/`PortNumber`, batch
 * 7a-0 - and this is deliberately not a replacement for that reading but the
 * authority over it, for one measured reason: batch 7b-V0 attached a Full-Speed
 * `usb-hub` to a root port with a Full-Speed device behind it and usbport
 * reported `HubAddr` = the hub's own address, i.e. a transaction translator for
 * a hub that physically has none. There is no TT anywhere on that path, and
 * Table 6-6's condition is "connected through a **High-speed** hub"; programming
 * the pair usbport gave would describe hardware that does not exist. The graph
 * carries each hub's speed **as this driver decoded it from PORTSC** (or from
 * usbport's `DeviceSpeed` for a hub that is itself behind a hub), so it can
 * answer whether an HS ancestor exists at all. usbport's pair is then the
 * cross-check, and a disagreement is a counter rather than a coin toss.
 */
typedef struct _XHCI_TOPO_TT {
    ULONG HubAddress;       /* the nearest High-Speed ancestor hub */
    ULONG HubPort;          /* its downstream port the path leaves by */
    ULONG MultiTt;          /* that hub's multi-TT interface is enabled */
} XHCI_TOPO_TT, *PXHCI_TOPO_TT;

/*
 * What a folded reply says has **gone**: the graph learns a disconnect from the
 * `GET_STATUS(port)` payload and nothing else can (`CLEAR_FEATURE`
 * (`C_PORT_CONNECTION`) is sent for a connect and a disconnect alike), so this
 * is the only channel through which a device behind a hub is ever known to have
 * left. The graph prunes its own node; the caller owns the device record.
 */
typedef struct _XHCI_TOPO_GONE {
    ULONG Disconnected;     /* nonzero: HubAddress/HubPort named a departure */
    ULONG HubAddress;
    ULONG HubPort;
} XHCI_TOPO_GONE, *PXHCI_TOPO_GONE;

/*
 * What a hub's **own** Slot Context has to say about it (task 7b-A.2).
 *
 * The four fields spec 6.2.2.2 p.412 names as the ones a Configure Endpoint
 * initializes beyond Context Entries: "The Hub field shall also be initialized.
 * If Hub = '1' and Speed = High-Speed, then the TT Think Time (TTT) and
 * Multi-TT (MTT) fields shall be initialized... If Hub = '1', then the Number of
 * Ports field shall be initialized, else Number of Ports = '0'."
 *
 * They are grouped rather than read one at a time because they are decided
 * together - three of the four are conditional on the other two - and because
 * the driver compares one marking against another to decide whether the xHC
 * already holds it.
 */
typedef struct _XHCI_TOPO_HUBMARK {
    ULONG Hub;              /* Slot Context DW0 bit 26 */
    ULONG NumberOfPorts;    /* DW1 31:24, bNbrPorts */
    ULONG MultiTt;          /* DW0 bit 25 */
    ULONG TtThinkTime;      /* DW2 17:16 */
} XHCI_TOPO_HUBMARK, *PXHCI_TOPO_HUBMARK;

typedef struct _XHCI_TOPOLOGY {
    ULONG Count;            /* nodes in use */
    ULONG Dropped;          /* promotions refused because the table was full */

    /*
     * The pending enumeration parent - design doc 02 section 2 step 2. usbhub
     * resets one downstream port and then creates one device at address 0 on
     * it, so a single slot is enough; what a second reset arriving first means
     * is recorded rather than hidden, because "enumeration is serialized" is an
     * assumption this graph would otherwise be silently built on.
     */
    ULONG Pending;          /* nonzero: PendingHub/PendingPort are armed */
    ULONG PendingHub;
    ULONG PendingPort;

    /* Release-build readings. Each names something none of the others can. */
    ULONG Promotions;       /* devices first identified as hubs */
    ULONG Resets;           /* SET_FEATURE(PORT_RESET) observed on a hub */
    ULONG ResetsUnknownHub; /* ...on a device with no node - the parent is lost */
    ULONG ResetsOverwritten;/* a second reset before the first was claimed */
    ULONG Claims;           /* pending parents handed to an address-0 open */
    ULONG ClaimsUnarmed;    /* an address-0 open with no pending parent */
    /*
     * A claim that was armed and spent but could not answer - the node pruned
     * between the reset and the open, or its position never learned (Phase 7
     * review, B9). Apart from ClaimsUnarmed because the two mean opposite
     * things: unarmed is the ordinary root-port answer, this is a consumed
     * claim that produced nothing, i.e. a channel failure or a lifetime race.
     */
    ULONG ClaimsUnusable;
    ULONG ClaimsTooDeep;    /* ...armed, but past the Route String's five tiers */
    /*
     * Pending parents dropped because a **root**-port reset armed an
     * enumeration of its own (task 7b-A.3). At most one enumeration bracket is
     * open at a time - measured, design doc 02 section 2 - so the newer reset
     * is the live one and the older claim will never be spent. Dropping it is
     * what keeps the two entitlements from both being armed, which is what
     * removes the question of which one an address-0 open should believe.
     */
    ULONG PendingDropped;
    /*
     * ...and ones given up because the caller judged the arming reset to be
     * usbhub's second per bracket, re-arming a claim the address-0 open had
     * already spent (Phase 7 review, finding A6). Apart from PendingDropped
     * because the two readings disagree about whose bracket was live: a
     * dropped claim was superseded by a root-port enumeration, a suppressed
     * one belonged to the hub-tier bracket still running.
     */
    ULONG ResetsSuppressed;
    /*
     * Nodes re-keyed when the SET_ADDRESS interception assigned their device a
     * different address (finding A4), and stale nodes pruned from under a
     * newly assigned address before they could hand the new device a departed
     * hub's position (finding A5). Distinct counters because the first is the
     * ordinary re-enumeration and the second is evidence a detach was missed.
     */
    ULONG Migrations;
    ULONG MigrationsStale;
    ULONG PowerSweeps;      /* SET_FEATURE(PORT_POWER) observed */
    ULONG Descriptors;      /* hub descriptor replies folded */
    ULONG DescriptorsBad;   /* ...and ones that were not self-consistent */
    /*
     * ...and self-consistent ones claiming `bNbrPorts` = 0. Its own counter
     * because such a descriptor is not malformed and is not usable either:
     * XhciTopoHubMark refuses it (Table 6-5 defines Number of Ports as "the
     * number of downstream facing ports supported by the hub"), so without this
     * the hub would silently never be marked - the class of silence task
     * 7b-A.1.0 exists to remove.
     */
    ULONG DescriptorsNoPorts;
    ULONG PortStatuses;     /* GET_STATUS(port) replies folded */
    ULONG PortStatusesWide; /* ...naming a port past XHCI_TOPO_CONNECT_PORTS */
    ULONG Disconnects;      /* connect bit 1 -> 0 on some hub port */
    /*
     * Departures read out of `wPortChange` rather than out of the connect bit
     * (Phase 7 review, B10): a disconnect+reconnect between usbhub polls
     * reads connected -> connected, and only the C_PORT_CONNECTION change bit
     * in bytes 2-3 of the reply proves a device left in between. Counted in
     * `Disconnects` too; this line says how many were only visible this way.
     */
    ULONG Reconnects;
    /*
     * SET_INTERFACE **applied** to a hub node - one that completed successfully
     * and resolved to a node already known to be a hub. A stalled request, one
     * whose record no longer answers to its address, one for a device with no
     * node, and one for a node that is not a hub all reach nothing and count
     * nothing. *(This read "SET_INTERFACE observed on a hub" until the post-Phase 13 review rounds,
     * when the apply moved from the placement to the completion and "observed"
     * stopped being what it counts.)*
     */
    ULONG AltSettings;
    /* Alternate-setting state dropped because the device was reset out from
     * under it (`XhciTopoForgetAlternate`), counted only when there was
     * something to drop. */
    ULONG AltForgotten;
    ULONG Prunes;           /* nodes removed */
    ULONG MaxTier;          /* the deepest tier any node reached */

    XHCI_TOPO_NODE Node[XHCI_TOPO_NODES];
} XHCI_TOPOLOGY, *PXHCI_TOPOLOGY;

/* ------------------------------------------------------------------ */
/* What one setup packet asks the caller to do about its reply         */
/* ------------------------------------------------------------------ */

#define XHCI_TOPO_REPLY_NONE        0
#define XHCI_TOPO_REPLY_HUB_DESC    1
#define XHCI_TOPO_REPLY_PORT_STATUS 2

/*
 * What XhciTopoObserveSetup decided. `Reply` is the only field a caller must
 * act on; the rest is what it has to hand back when the reply arrives, so that
 * nothing is re-derived from a setup packet usbport has since freed.
 */
typedef struct _XHCI_TOPO_SNOOP {
    ULONG Reply;            /* XHCI_TOPO_REPLY_* */
    ULONG Address;          /* the device the request went to */
    ULONG Port;             /* wIndex, for a port-status reply */
    /*
     * This packet armed the pending enumeration parent (task 7b-A.3). The
     * caller needs it because the *other* entitlement - one address-0 open per
     * root-port reset - lives in the driver and not here: a hub-port reset means
     * the next address-0 open is a device behind that hub, so a root-port claim
     * still lying unspent has to be given up in the same breath. Reported rather
     * than acted on, like everything else in this file.
     */
    ULONG Armed;
} XHCI_TOPO_SNOOP, *PXHCI_TOPO_SNOOP;

/* ------------------------------------------------------------------ */
/* API                                                                 */
/* ------------------------------------------------------------------ */

/*
 * Empty the graph. Called at StartController, and again at StopController so a
 * stop that is not followed by usbport zeroing the extension cannot leave a
 * tree describing devices that are gone.
 */
VOID XhciTopoReset(PXHCI_TOPOLOGY topo);

/*
 * Observe one EP0 SETUP packet sent to a device whose usbport address is
 * `address`, and record whatever it says about the tree.
 *
 * `address` must be the address usbport is using *now* - the graph is keyed on
 * it, and an address-0 transfer names no device, so the caller passes only
 * addressed traffic. Returns through `snoop` whether the reply is worth
 * reading; a caller that ignores the return loses the descriptor and the
 * disconnects and nothing else.
 *
 * Records nothing and answers XHCI_TOPO_REPLY_NONE for a NULL argument.
 */
VOID XhciTopoObserveSetup(PXHCI_TOPOLOGY topo,
                          ULONG address,
                          const struct _XHCI_SETUP_PACKET *setup,
                          PXHCI_TOPO_SNOOP snoop);

/*
 * Apply a SET_INTERFACE that **completed successfully**: the hub's Multi-TT bit
 * and the "an alternate setting was seen at all" reading beside it.
 *
 * Separate from `XhciTopoObserveSetup` because those two are the only things
 * this graph learns from a request with no reply, and a request with no reply
 * has nothing to fold - so the success is the caller's to report. xHCI 4.5.2
 * scopes MTT to an interface that "has been enabled", and a stalled request
 * enabled nothing; the caller therefore calls this from the completion, beside
 * `XhciDescSelectInterface`, which reads the same packet under the same rule.
 *
 * `alternate` is the setup packet's `wValue`. Records nothing for a NULL graph,
 * for address 0, for a device with no node, or for a node that is not a hub -
 * this promotes nothing, because every device with an alternate setting sends
 * this request and only a hub's means MTT.
 */
VOID XhciTopoApplySetInterface(PXHCI_TOPOLOGY topo,
                               ULONG address,
                               ULONG alternate);

/*
 * Forget the alternate setting recorded for `address`, because the device there
 * has been reset and a reset returns every interface to alternate 0.
 *
 * Clears `XHCI_TOPO_F_MTT` **and** `XHCI_TOPO_F_ALT_SEEN`: nothing has been
 * asked of the device that came back, so the pair must read as "never asked"
 * rather than as an explicit single-TT selection. The rest of the node - its
 * position, its hub-ness, its descriptor fields - is left alone, because a
 * re-enumeration on the same port is the same position.
 *
 * Records nothing for a NULL graph, for address 0, or for a device with no
 * node. Counted in `AltForgotten` only when something was actually there.
 */
VOID XhciTopoForgetAlternate(PXHCI_TOPOLOGY topo, ULONG address);

/*
 * Fold the bytes a snooped reply carried. `data` is the caller's own read of
 * the transfer buffer and `length` is what the transfer actually moved, which
 * for a short packet is less than was asked for. Returns 1 if the reply changed
 * anything.
 *
 * A reply for a device with no node is dropped: a hub descriptor read from a
 * device nothing has identified as a hub is a request this graph should not
 * have snooped, and inventing a node for it would put a device in the tree with
 * no position at all.
 */
ULONG XhciTopoObserveReply(PXHCI_TOPOLOGY topo,
                           const XHCI_TOPO_SNOOP *snoop,
                           const UCHAR *data,
                           ULONG length,
                           PXHCI_TOPO_GONE gone);

/*
 * Record that a device at `address`, sitting on root-hub port `rootPort` of
 * generation `generation` at speed `speed`, is a hub. Idempotent: a device
 * already in the graph keeps its learned facts and has its position refreshed.
 *
 * This is the **tier-0** entry point; `XhciTopoAttachChild` below is the one for
 * every deeper tier, and `src/xhci_slot.c` picks between them by whether the
 * record carries a root-hub port or a claimed parent. Returns 1 if the graph
 * holds the node afterwards. *(This said it was "the only one usable while task
 * 7b-A.3 has not landed, because a device behind a hub does not enumerate yet"
 * until the post-Phase 13 review rounds; 7b-A.3 landed in Phase 7b and both entry points are live.)*
 */
ULONG XhciTopoAttachRoot(PXHCI_TOPOLOGY topo,
                         ULONG address,
                         ULONG rootPort,
                         ULONG generation,
                         ULONG speed);

/*
 * The same for a device that enumerated behind a hub, at the position a claim
 * returned. Refuses a position past the Route String's five tiers rather than
 * truncating it.
 */
ULONG XhciTopoAttachChild(PXHCI_TOPOLOGY topo,
                          ULONG address,
                          const XHCI_TOPO_CHILD *at,
                          ULONG speed);

/*
 * Spend the pending enumeration parent, if one is armed. Returns 1 and fills
 * `out` when it was, 0 otherwise - and the 0 is the ordinary answer for a
 * device on a root port, where the root-hub reset this driver performed itself
 * is the entitlement (`xhciDevEnumeratingPort`, task 7b-A.0) rather than
 * anything snooped.
 *
 * Spent, not peeked: a claim that stayed armed would be handed to the next
 * address-0 open as well, which is the exact shape of the hijack task 7b-A.0
 * bounded one tier up.
 */
ULONG XhciTopoClaimChild(PXHCI_TOPOLOGY topo, PXHCI_TOPO_CHILD out);

/*
 * Give up a pending parent that a **root**-port reset has just superseded
 * (task 7b-A.3). Counted in `PendingDropped`; does nothing when none is armed,
 * so the caller need not ask first.
 */
VOID XhciTopoDropPending(PXHCI_TOPOLOGY topo);

/* The node for one address, or NULL. */
const XHCI_TOPO_NODE *XhciTopoFind(const XHCI_TOPOLOGY *topo, ULONG address);

/*
 * Remove the node for `address` and every node below it. Called when a device
 * record is released - a hub that has gone takes its whole subtree with it, and
 * a subtree left behind would give a later device a parent that no longer
 * exists.
 */
VOID XhciTopoDetach(PXHCI_TOPOLOGY topo, ULONG address);

/*
 * usbport has just assigned `newAddress` to the device the graph knew as
 * `oldAddress` (Phase 7 review, findings A4/A5). Two obligations, and the
 * second does not depend on the first:
 *
 *   - the node follows its device: a re-enumeration clears the record's
 *     address at the address-0 open while the graph deliberately keeps the
 *     node, so an address change would otherwise strand it under a key
 *     nothing will ever detach;
 *   - whatever *else* sits under `newAddress` is stale by construction -
 *     usbport's addresses are unique among live devices - and left there it
 *     would hand this device, and every child claimed through it, the
 *     departed hub's position. It is pruned with its subtree.
 *
 * A migrated hub's own children are not carried across: their ParentAddress
 * names the old key, and the re-enumeration that produces an address change
 * destroys and rebuilds everything below the hub anyway.
 *
 * `oldAddress` == `newAddress` (usbport reusing the address, the common
 * recovery cycle) is a no-op: the node under it is this device's own.
 */
VOID XhciTopoMigrate(PXHCI_TOPOLOGY topo,
                     ULONG oldAddress,
                     ULONG newAddress);

/*
 * Give up a pending parent that the caller has judged to be usbhub's second
 * port reset of an enumeration bracket re-arming a claim nothing will spend
 * (Phase 7 review, finding A6 - task 7b-A.1.1's rule one tier down). The
 * judgement lives in the caller because it needs the device records; this is
 * the verb and the counter (`ResetsSuppressed`). Does nothing when no claim is
 * armed.
 */
VOID XhciTopoSuppressClaim(PXHCI_TOPOLOGY topo);

/*
 * The position a device plugged into `hubAddress` port `hubPort` would have.
 * Returns 1 when the parent is known; `out->TooDeep` then says whether the
 * result is addressable. This is the whole of the route arithmetic, in one
 * place, so the claim path and any later consumer cannot spell it differently.
 */
ULONG XhciTopoChildOf(const XHCI_TOPOLOGY *topo,
                      ULONG hubAddress,
                      ULONG hubPort,
                      PXHCI_TOPO_CHILD out);

/*
 * The TT Think Time this hub's own Slot Context should carry (task 7b-A.2), or
 * 0 when no descriptor has been read. Separated from the raw field so the
 * shift/mask is written once.
 */
ULONG XhciTopoThinkTime(const XHCI_TOPO_NODE *node);

/*
 * What this node's own Slot Context should say about it, given the speed **this
 * driver decoded for the device** - task 7b-A.2's whole rule, in one place.
 *
 * Returns 1 and fills `out` when the node is a hub whose descriptor facts have
 * arrived; 0 with `out` zeroed otherwise, which covers a device that is not a
 * hub, a hub identified by port traffic before its descriptor was read, and a
 * descriptor claiming no ports at all.
 *
 * `speedClass` is the decoded XHCI_SPEED_*, not the raw Protocol Speed ID the
 * Slot Context encodes. Table 6-6 p.409 conditions TTT on "Hub = '1' and
 * Speed = High-Speed" and Table 6-4 p.410 conditions MTT on "this is a
 * High-speed hub that supports Multiple TTs and the Multiple TT Interface has
 * been enabled by software" - both are questions about the speed, and only the
 * decoded class answers them on a controller whose Protocol Speed ID table
 * assigns its own values (docs/contributing/implementation-invariants.md, "Port Speed
 * Decoding"). A Full-Speed hub therefore gets Hub and Number of Ports and
 * nothing else, which is exactly the QEMU `usb-hub` shape both target VMs run.
 */
ULONG XhciTopoHubMark(const XHCI_TOPO_NODE *node,
                      ULONG speedClass,
                      PXHCI_TOPO_HUBMARK out);

/*
 * The transaction translator a device plugged into `hubAddress` port `hubPort`
 * would sit behind: the nearest High-Speed ancestor of that hub, the port on it
 * the path leaves by, and whether its multi-TT interface is enabled.
 *
 * Returns 1 and fills `out` when such an ancestor exists, 0 with `out` zeroed
 * otherwise - and the 0 is the ordinary answer for two quite different buses:
 * an all-High-Speed path (where no split transaction is needed) and an
 * all-Full-Speed one (where none is possible). Neither may carry a TT triple;
 * Table 6-6 p.409 conditions both DW2 fields on "connected through a High-speed
 * hub", and 6.2.2.1 p.411 then clears every field it does not name.
 *
 * The hub itself counts: a Full-Speed device on an HS hub's own port has that
 * hub as its TT. The walk is bounded by the table size, so a graph that somehow
 * held a cycle terminates rather than hanging the caller.
 */
ULONG XhciTopoTtFor(const XHCI_TOPOLOGY *topo,
                    ULONG hubAddress,
                    ULONG hubPort,
                    PXHCI_TOPO_TT out);

/* Every reply offset a fold indexes must sit inside the window the caller is
 * promised to have copied (XHCI_TOPO_REPLY_BYTES above). */
XHCI_C_ASSERT(topo_hub_descriptor_fits_reply_window,
              XHCI_TOPO_HUBD_CHAR_HI < XHCI_TOPO_REPLY_BYTES);
/* The fold indexes data[3] (the wChange high byte), so that is the offset the
 * window has to hold, not the two-byte minimum the length gate asks for. */
XHCI_C_ASSERT(topo_port_status_fits_reply_window,
              3U < XHCI_TOPO_REPLY_BYTES);

#endif /* XHCI_TOPO_H */
