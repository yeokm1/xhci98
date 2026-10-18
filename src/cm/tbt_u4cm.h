/*
 * tbt_u4cm.h - the USB4 software connection manager, its pure half
 * (roadmap-usb4.md tasks 40-A.3 and 40-A.8's pure part; design record 22
 * sections 6, 7.1 to 7.5, 8 and 9, with 7.6's resume obligations and 10's
 * statement hand-off).
 *
 * What is here: the host router (enumerated or verified, adapters found
 * from their capability chains, USB4 ports and their protocol adapters
 * paired by the rule of Spec 5.2.5); one device router per host USB4 port
 * (depth 1), its DROM and UUID, Get Capabilities and the Buffer Allocation
 * Request with every router operation possibly unsupported, failing or
 * never completing; lane bonding with the verified fallback; the TMU per
 * decision U16; the Configuration Valid handshake; the PCIe tunnel's four
 * path entries behind the Detect gate; the optional USB3 tunnel with its
 * bandwidth handshake; credit accounting; HopIDs allocated around every
 * reserved and quarantined entry; the quirk table; tunnels found at start
 * discovered and preserved (every type), held, or cleaned on the user's
 * request; hot plug and unplug, including inside every wait; the per-port
 * state machine; Thunderbolt 3 device routers at depth 1, PCIe first;
 * resume obligations; the per-port eject. What is not: the registry, PnP,
 * power, the thread, the common buffer, the PCI side (tunnel-port sets,
 * re-enumeration, the PCI-side reading, the stor98 delivery) - the driver
 * (task 40-A.6) does those and reaches this file through the TBT_HOST
 * accessor, an action queue it drains, and two upcalls.
 *
 * SOURCES. "Spec" is the USB4 Specification 2.0 and "Guide" the USB4
 * Connection Manager Guide 2.0 (November 2025 bundle,
 * docs/references/usb4_202511/). The sequence starts from usb4-emu's
 * tools/usb4cm/core/u4cm.c (main 3c74189, GPL-2.0-only), adapted under the
 * owner's rule of 2026-10-15 (the run brief's addendum 7) and changed as
 * design record 22 section 2.2's table says; Linux drivers/thunderbolt
 * (c6859eed, GPL-2.0: tb.c, switch.c, tunnel.c, path.c, usb4.c, tmu.c,
 * lc.c, quirks.c, cap.c) is the cross-check and the source of what usb4cm
 * lacks, translated to C89 for this driver, its notices kept in
 * tbt_u4cm.c. Behaviour taken from Linux alone is [linux]: unconfirmed
 * until a hardware capture shows it. Every real USB4 host is untested; the
 * emulator's two host models are the only fabrics this has met.
 *
 * Use (record 22 section 4.4, after tbt_hw.h's TbtHwSelect returned
 * TBT_SEL_SOFTWARE): TbtU4Init, then TbtU4Start over the common buffer;
 * then on the thread every idle period TbtU4Poll followed by
 * TbtU4NextAction until it returns FALSE, re-enumerating the tunnel ports
 * each action names; TbtU4PresenceSample for each cached recipient's
 * configuration read; TbtU4Eject for an eject; TbtU4Suspend and
 * TbtU4Resume around a system sleep; TbtU4Stop at stop, remove, disable
 * and shutdown.
 *
 * IRQL and locking: as tbt_ring.h - PASSIVE_LEVEL, on the connection-
 * manager thread, no lock held, for every function that takes a TBT_U4CM.
 * TbtU4QuirkMatch and TbtU4LaneClass are pure and callable at any IRQL.
 * The upcalls run on that thread, possibly inside a teardown nested at a
 * safe point, and must not call back into this file.
 *
 * DDK-free: part of the pure core. C89, no 64-bit arithmetic, no floating
 * point.
 */

#ifndef TBT_U4CM_H
#define TBT_U4CM_H

#include "../xhci_compat.h"
#include "tbt_regs.h"
#include "tbt_ring.h"

/* ---------------------------------------------------------------------- */
/* Limits and bounds (record 22 section 5.2)                              */
/* ---------------------------------------------------------------------- */

#define TBT_U4_MAX_PORTS            4UL     /* host USB4 ports handled */
#define TBT_U4_MAX_ADP              64UL    /* adapter numbers 0..63, Spec 8.2.1 */
#define TBT_U4_MAX_LINKS            8UL     /* discovered links below depth 1 */
#define TBT_U4_QUARANTINE           32UL
#define TBT_U4_ACTION_QUEUE         32UL
#define TBT_U4_OBLIGATIONS          (2UL * TBT_U4_MAX_PORTS)
#define TBT_U4_NAME_LEN             32UL
#define TBT_U4_DROM_MAX             512UL
#define TBT_U4_DEPTH_MAX            5UL     /* Guide 3.2.1 step 2 */

#define TBT_U4_ROUTER_READY_US      500000UL    /* Guide 3.2.1 note; Linux usb4_switch_setup */
#define TBT_U4_ENUM_TRIES           20UL        /* usb4cm u4cm_connect */
#define TBT_U4_ENUM_GAP_US          50000UL
#define TBT_U4_ROP_US               500000UL    /* Linux usb4.c usb4_native_switch_op */
#define TBT_U4_CONFIG_READY_US      500000UL    /* Guide 3.2.3 note */
#define TBT_U4_LANE1_CL0_US         1000000UL   /* Linux switch.c tb_wait_for_port */
#define TBT_U4_BONDING_US           500000UL    /* Guide 7.1 tBonding */
#define TBT_U4_LANE_DISABLE_US      500000UL
#define TBT_U4_DETECT_US            500000UL    /* Guide 5.3.1 step 1 */
#define TBT_U4_L0_US                1000000UL   /* usb4cm u4cm_pcie_tunnel */
#define TBT_U4_DRAIN_US             500000UL    /* Guide 5.2.2 */
#define TBT_U4_TEARDOWN_GAP_US      2UL         /* Spec Table 5-16 tTeardown, a minimum */
#define TBT_U4_HCA_US               1500000UL   /* Linux usb4.c usb4_usb3_port_cm_request */
#define TBT_U4_DPR_HOLD_US          10000UL     /* Spec 6.9 tClearDPR, a minimum */
#define TBT_U4_DPR_CL0_US           2000000UL
#define TBT_U4_CONNECT_US           2000000UL   /* usb4cm connect_wait_us */
#define TBT_U4_TRANSITIONAL_US      2000000UL   /* record 22 section 7.1 item 4 */
#define TBT_U4_TMU_POST_US          100000UL    /* record 22 section 6.6, a project bound */
#define TBT_U4_TMU_SETTLE_US        50000UL     /* Guide 7.4.3.1 step 4, a minimum */
#define TBT_U4_USB3_GAP_US          500000UL    /* Guide 5.5.1 step 1, a minimum */
#define TBT_U4_HRR_HOLD_US          50000UL     /* Guide 3.5, a minimum */
#define TBT_U4_HRR_DONE_US          500000UL
#define TBT_U4_POLL_STEP_US         1000UL      /* a register poll's pace */
#define TBT_U4_STATEMENT_US         3000000UL   /* record 22 section 10.3 */
/* Section 7.6: the resume's own budgets, enforced on the clock (the
 * domain's host router again, then each obligation), and the margin for
 * the one wait or request already running when a budget runs out. */
#define TBT_U4_RESUME_HOST_US       10000000UL
#define TBT_U4_RESUME_EACH_US       20000000UL
#define TBT_U4_SERVICE_BATCH        16UL        /* frames a budgeted safe point takes */
#define TBT_U4_USB3_MAX_MBPS        80000UL     /* above any USB4 link: unrepresentable */

/* USB3 bandwidth (record 22 section 6.9; Linux usb4.c, tunnel.c, quirks.c). */
#define TBT_U4_USB3_QUIRK_MBPS      16376UL
#define TBT_U4_USB3_KEEP_MBPS       900UL       /* usb4_usb3_port_release_bandwidth */
#define TBT_U4_PCIE_RESERVE_MBPS    1500UL

/* ---------------------------------------------------------------------- */
/* Configuration (record 22 section 12)                                   */
/* ---------------------------------------------------------------------- */

typedef struct _TBT_U4_CONFIG {
    BOOLEAN Approve;        /* XhciCmApprove, default 1 (decision U5) */
    BOOLEAN Usb3Tunnel;     /* XhciCmUsb3Tunnel, default 1 (U3) */
    BOOLEAN LaneBonding;    /* XhciCmLaneBonding, default 1 (U4) */
    ULONG PreOsCleanup;     /* XhciCmPreOsCleanup 0, 1 or 2 (U2) */
    ULONG RingFlags;        /* tbt_hw.c TbtHwRingFlags; TAKEOVER is added */
    ULONG HostInterfaceCaps;/* as TbtHwSelect read it (Spec Table 12-11) */
    /* TbtU4Start ends without its first TbtU4Poll, so no port found empty
     * at start is connected (and no tunnel built) until the caller's own
     * first TbtU4Poll: the driver takes its learning baseline of the
     * tunnel ports between the two (record 22 section 7.6). Default FALSE. */
    BOOLEAN DeferFirstPoll;
} TBT_U4_CONFIG, *PTBT_U4_CONFIG;

VOID TbtU4DefaultConfig(PTBT_U4_CONFIG cfg);

/* ---------------------------------------------------------------------- */
/* Routers and adapters                                                   */
/* ---------------------------------------------------------------------- */

typedef struct _TBT_U4_ADP {
    ULONG Type;             /* ADP_CS_2 Adapter Type, TBT_ADP_TYPE_*; 0 unused */
    UCHAR CapLane;          /* capability offsets, 0 for none */
    UCHAR CapPort;
    UCHAR CapAdp;           /* Adapter Configuration Capability: PCIe, USB3, DP */
    UCHAR CapTmu;
    UCHAR MaxInHop;         /* ADP_CS_5 6:0 */
    UCHAR MaxOutHop;        /* ADP_CS_5 17:11 */
    UCHAR CtlCredits;       /* the HopID 0 entry's credits (Linux tb_init_port) */
    UCHAR Present;
} TBT_U4_ADP, *PTBT_U4_ADP;

/* Get Capabilities' answer (record 22 section 6.4). */
#define TBT_U4_CAPS_PENDING     0UL     /* not asked yet: acknowledgements held */
#define TBT_U4_CAPS_DEFAULT     1UL     /* enumerated here, or Not Supported: all disabled */
#define TBT_U4_CAPS_KNOWN       2UL     /* adopted from a successful answer */
#define TBT_U4_CAPS_UNKNOWN     3UL     /* any other answer: no BA, no acknowledgement */

typedef struct _TBT_U4_ROUTER {
    TBT_ROUTE Route;
    ULONG Depth;
    ULONG Vendor;
    ULONG Device;
    ULONG UpAdp;            /* ROUTER_CS_1 Upstream Adapter */
    ULONG MaxAdp;
    ULONG Usb4Version;      /* ROUTER_CS_4 31:24 */
    BOOLEAN Present;
    BOOLEAN Tbt3;           /* USB4 Version 0: record 22 section 8 */
    BOOLEAN EnumeratedHere;
    BOOLEAN Preserved;      /* found enumerated at start or resume */
    BOOLEAN OpBlocked;      /* Operation Valid never cleared (section 6.7) */
    ULONG CapTmu;           /* router capability offsets, 0 for none */
    ULONG CapPlugEvents;
    ULONG CapLc;
    ULONG CapsState;        /* TBT_U4_CAPS_* */
    ULONG CapsSupported;    /* bit n: capability index n */
    ULONG CapsEnabled;
    BOOLEAN BaValid;
    ULONG BaMaxPcie;        /* Buffer Allocation answer, 0 when none */
    ULONG BaMaxUsb3;
    ULONG Uuid[4];          /* USB4: ROUTER_CS_7, 8; Thunderbolt 3: the LC fuse */
    BOOLEAN UuidValid;      /* read successfully: compared, never a failed sample */
    ULONG Quirks;           /* TBT_U4_QUIRK_* */
    char VendorName[TBT_U4_NAME_LEN];
    char ModelName[TBT_U4_NAME_LEN];
    ULONG DromRevision;     /* DROM byte 13: 1 Thunderbolt 3 layout, 3 USB4; 0 unread */
    ULONG DromUnused[2];    /* bit n: the DROM's adapter entry n marks it unused */
    BOOLEAN DromOwed;       /* Thunderbolt 3: adopted, or rebuilt under a resume
                               budget, its EEPROM not yet read: its names read
                               by TbtU4Poll once no budget runs (section 8) */
    TBT_U4_ADP Adp[TBT_U4_MAX_ADP];
} TBT_U4_ROUTER, *PTBT_U4_ROUTER;

/* ---------------------------------------------------------------------- */
/* Tunnels and ports                                                      */
/* ---------------------------------------------------------------------- */

/* One tunnel's four entries and what of it was written or found. */
#define TBT_U4_E_HOST_PROTO     0x01UL  /* host protocol adapter, HopID 8 */
#define TBT_U4_E_DEV_LANE       0x02UL  /* device lane 0, HopID Hd */
#define TBT_U4_E_DEV_PROTO      0x04UL  /* device protocol adapter, HopID 8 */
#define TBT_U4_E_HOST_LANE      0x08UL  /* host lane 0, HopID Hu */
#define TBT_U4_E_HOST_ENABLE    0x10UL  /* host adapter Path Enable */
#define TBT_U4_E_DEV_ENABLE     0x20UL  /* device adapter Path Enable */
#define TBT_U4_E_BANDWIDTH      0x40UL  /* USB3: an allocation written */

typedef struct _TBT_U4_TUNNEL {
    BOOLEAN Up;
    BOOLEAN Preserved;      /* found at start, never built here */
    BOOLEAN Incomplete;     /* an entry could not be read: never proven closed */
    ULONG Hd;               /* lane HopID downstream */
    ULONG Hu;               /* lane HopID upstream */
    ULONG Written;          /* TBT_U4_E_* written (or found, when preserved) */
    ULONG CreditsHost;
    ULONG CreditsDev;
} TBT_U4_TUNNEL, *PTBT_U4_TUNNEL;

/* Port states, record 22 section 7.2. */
#define TBT_U4_EMPTY            0UL
#define TBT_U4_CONNECTING       1UL
#define TBT_U4_BONDING          2UL
#define TBT_U4_CONFIGURING      3UL
#define TBT_U4_TUNNELLING       4UL
#define TBT_U4_UP               5UL
#define TBT_U4_HELD             6UL
#define TBT_U4_FAILED           7UL
#define TBT_U4_UNSUPPORTED      8UL
#define TBT_U4_TEARDOWN         9UL

/* Why a port is FAILED, HELD or UNSUPPORTED (the log). */
#define TBT_U4_WHY_NONE             0UL
#define TBT_U4_WHY_ENUMERATION      1UL
#define TBT_U4_WHY_ROUTER_READY     2UL
#define TBT_U4_WHY_VERSION          3UL     /* a reserved USB4 Version */
#define TBT_U4_WHY_ENUM_ERROR       4UL     /* ERR_ENUM or ERR_NUA: someone else's */
#define TBT_U4_WHY_BONDING          5UL
#define TBT_U4_WHY_CONFIG_READY     6UL
#define TBT_U4_WHY_DETECT           7UL
#define TBT_U4_WHY_CREDITS          8UL
#define TBT_U4_WHY_HOPID            9UL
#define TBT_U4_WHY_PATH             10UL
#define TBT_U4_WHY_L0               11UL
#define TBT_U4_WHY_NO_ADAPTERS      12UL
#define TBT_U4_WHY_PLUG_EVENTS      13UL    /* a Thunderbolt 3 router without them */
#define TBT_U4_WHY_DISCOVERY        14UL    /* HELD: a half-built configuration */
#define TBT_U4_WHY_BLOCKED          15UL    /* a quarantined or blocked adapter */
#define TBT_U4_WHY_LOST             16UL    /* the PCIe tunnel went down */
#define TBT_U4_WHY_EJECTED          17UL
#define TBT_U4_WHY_RESUME           18UL    /* HELD: a resume obligation not met */
#define TBT_U4_WHY_LANE_STATE       19UL
#define TBT_U4_WHY_CONTAINED        20UL    /* a sequence the containment stopped */
#define TBT_U4_WHY_RECOVER          21UL    /* HELD by a recovery (section 7.7):
                                               unclassified, or drained and
                                               waiting for its rebuild */

typedef struct _TBT_U4_PORT {
    ULONG Index;            /* 1-based */
    ULONG Lane0;            /* host lane 0 adapter; lane 1 is Lane0 + 1 */
    ULONG PcieDown;         /* 0 for none */
    ULONG Usb3Down;
    BOOLEAN MapValid;       /* the mapping operation named this adapter */
    ULONG MapRid;           /* its non-FPB Routing ID (bus 15:8, dev 7:3, fn 2:0) */
    BOOLEAN HotPlugEnabled;

    ULONG State;            /* TBT_U4_* */
    ULONG Why;
    ULONG Generation;       /* incremented at every entry to CONNECTING */
    BOOLEAN UnplugPending;
    BOOLEAN CheckPending;
    BOOLEAN Unlocked;       /* this driver cleared Lock on lane 0 */
    BOOLEAN PcieFailed;     /* no PCIe until a re-plug */
    BOOLEAN Ejected;
    BOOLEAN DpPreserved;    /* a DisplayPort tunnel found at start */
    ULONG HostBaPcie;       /* the host router's Buffer Allocation for this port */
    ULONG HostBaUsb3;
    BOOLEAN RemovalOwed;    /* a PCIe teardown's step 6 is owed */
    BOOLEAN HostResidual;   /* host entries never read at a teardown: swept
                             * before the port connects again */
    BOOLEAN BwReleaseOwed;  /* a USB3 allocation not yet released: retried */
    BOOLEAN RelockSeen;     /* lane 0's Lock found set again (and cleared):
                             * removal evidence kept until it is acted on */
    BOOLEAN HostCloseOwed;  /* a resume's host closure not yet proven */
    BOOLEAN ClassifyPending; /* a resume left it HELD unclassified: the
                              * next readable check classifies it first */
    BOOLEAN LostKnown;      /* a resume found its configuration lost (TopologyID
                             * Valid clear): that alone is no new evidence */
    BOOLEAN AsFoundOwed;    /* DeferFirstPoll: the start found only tunnels
                             * this driver does not build (DpPreserved); the
                             * first poll connects it on the link as found */
    TBT_U4_ROUTER Dev;

    /* Lane fields this driver changed (section 6.5), restored at unplug. */
    BOOLEAN Bonded;
    BOOLEAN SingleLane;     /* verified single (section 6.5 step 6) */
    BOOLEAN LaneWritten;
    ULONG LaneSaved[4];     /* LANE_ADP_CS_1 before step 4: host 0, host 1, dev 0, dev 1 */
    BOOLEAN Lane1Disabled;  /* section 6.5 step 6.4 set Lane Disable */
    BOOLEAN PcSetHost;
    BOOLEAN PcSetDev;

    /* TMU (section 6.6) */
    BOOLEAN TmuConfigured;  /* this driver enabled HiFi-Bi on this link */
    BOOLEAN TmuOk;          /* the link has accuracy for a USB3 tunnel */
    BOOLEAN TmuQuarantine;

    TBT_U4_TUNNEL Pcie;
    TBT_U4_TUNNEL Usb3;
    BOOLEAN Usb3Failed;
    BOOLEAN BwQuarantine;   /* CMR or HCA stuck (section 6.9 step 5) */
    BOOLEAN Usb3GapValid;
    ULONG LastUsb3TeardownUs;   /* kept per port, outside the generation */

    /* Section 7.1 item 4 */
    BOOLEAN Transitional;
    ULONG TransitionalSinceUs;

    /* Section 10.3: the anchor */
    BOOLEAN AnchorValid;
    ULONG AnchorUs;
    ULONG AnchorGeneration;

    /* Host lane 0's LANE_ADP_CS_1 as this driver last read it on its own
     * path (tbtU4LaneRd), for xhcicm.sys's query device (design record 25
     * section 4.3), which reads no register of its own: valid while
     * LinkGeneration is the port's Generation. */
    BOOLEAN LinkSeen;
    ULONG LinkLane1;
    ULONG LinkGeneration;

    /* Record 22 section 7.7's recovery (task 42-A.10). Rebuild*: a tunnel
     * drained by the recovery at both ends, rebuilt after the commit by
     * the per-tunnel rebuild transition once Barred is clear. RemovalDebt:
     * a PCIe tunnel drained by the recovery (departed, or to be rebuilt)
     * whose endpoint devnode the operating system may still have; the
     * driver takes it (TbtU4TakeDebt) and raises the member's barrier.
     * Barred: the driver's retirement barrier on this port - no tunnel is
     * built and no connect is made while it is set. */
    BOOLEAN RebuildPcie;
    BOOLEAN RebuildUsb3;
    BOOLEAN RemovalDebt;
    BOOLEAN Barred;
    /* RecDrain*: a recovery's drain of that tunnel did not complete (HELD,
     * quarantined): the next attempt owes it again. RecVerified: bit 0
     * PCIe, bit 1 USB3 - verified and kept by this attempt, the only
     * tunnels TbtU4RecoverDemote may drain. Discovered: section 9.2's
     * discovery ran to its end for the port, nothing cutting it short (a
     * recovery discovers one whose discovery never completed). */
    BOOLEAN RecDrainPcie;
    BOOLEAN RecDrainUsb3;
    UCHAR RecVerified;
    BOOLEAN Discovered;
    /* DiscoverOwed: the link of a departed router held by another one at
     * the recovery - section 9.2's discovery after the commit, its state
     * as found, nothing of the departed router's cleanup applied. */
    BOOLEAN DiscoverOwed;
    /* RecDeparted: a departed router's recorded host-side drain did not
     * complete (HELD): retried alone, never an ordinary check or teardown
     * against what now holds the link (tbtU4Departed). */
    BOOLEAN RecDeparted;
    /* RecCleanup: a departed router's lane, TMU or tail cleanup left owed
     * on a link that read disconnected; the link is read again before any
     * of it is made (tbtU4RecCleanupGate). */
    BOOLEAN RecCleanup;
} TBT_U4_PORT, *PTBT_U4_PORT;

/* A router below depth 1 a predecessor configured (section 7.1). */
typedef struct _TBT_U4_LINK {
    BOOLEAN Used;
    ULONG Port;             /* the host port it hangs off */
    TBT_ROUTE Parent;
    ULONG ParentLane0;      /* the parent's downstream lane 0 adapter */
    ULONG ParentLaneCap;    /* its Lane Adapter Configuration Capability */
    TBT_ROUTE Child;
    BOOLEAN ChildTbt3;      /* a Thunderbolt 3 router: no router operations */
    BOOLEAN ChildUuidValid; /* ChildUuid read: compared, never a failed sample */
    BOOLEAN RemovalPending; /* removed, its parent's paths not yet all closed */
    ULONG ChildLc;          /* its link controller, for the fused UUID */
    ULONG ChildUuid[4];
    ULONG CapsState;        /* TBT_U4_CAPS_* of the child (section 6.4) */
    ULONG CapsEnabled;
    BOOLEAN ChildDromRead;  /* the child's DROM asked once (a USB4 child) */
    ULONG ChildDromUnused[2];   /* its unused adapters (TBT_U4_ROUTER.DromUnused) */
} TBT_U4_LINK, *PTBT_U4_LINK;

/* A path entry not proven invalid and drained, or a protocol adapter
 * whose Path Enable would not clear (section 7.3). */
typedef struct _TBT_U4_QENTRY {
    BOOLEAN Used;
    BOOLEAN Enable;         /* a Path Enable, not an entry */
    ULONG Port;             /* 0 for the host router alone */
    ULONG Generation;       /* a device router's entries: its connection's */
    TBT_ROUTE Route;
    ULONG Adapter;
    ULONG Hop;
    ULONG OutAdapter;
    ULONG OutHop;
    ULONG Credits;
    ULONG EnableBit;        /* TBT_PCIE0_PE or TBT_USB3_0_PE */
    ULONG EnableCap;
} TBT_U4_QENTRY, *PTBT_U4_QENTRY;

/* ---------------------------------------------------------------------- */
/* Actions, upcalls, obligations                                          */
/* ---------------------------------------------------------------------- */

#define TBT_U4_ACT_TUNNEL_UP        1UL     /* a PCIe tunnel came up: re-enumerate */
#define TBT_U4_ACT_TUNNEL_DOWN      2UL     /* a PCIe tunnel went down: removal */
#define TBT_U4_ACT_RESYNC           3UL     /* every tunnel port as changed */
#define TBT_U4_ACT_DEEP_REMOVED     4UL     /* a router below depth 1 left */

typedef struct _TBT_U4_ACTION {
    ULONG Kind;             /* TBT_U4_ACT_* */
    ULONG Port;
    ULONG Generation;
    ULONG PcieDown;         /* the host adapter, for the fabric mapping */
    BOOLEAN MapValid;       /* router-named: MapRid is the tunnel port */
    ULONG MapRid;
} TBT_U4_ACTION, *PTBT_U4_ACTION;

/*
 * Section 7.3 steps 2 and 3, the driver's half: called after step 1 of a
 * PCIe teardown (a tunnel this driver built or preserved), with whether
 * its read-back proved the path closed, and the anchor and the time left
 * of section 10.3's 3 s. The driver reads the port's member of the
 * tunnel-port set within the time left and sends the stor98 statement
 * only when every condition of section 7.3 step 3 holds. Returns one of
 * TBT_U4_STMT_*, for the counters. May be NULL: no statement.
 */
#define TBT_U4_STMT_SENT            1UL
#define TBT_U4_STMT_WITHHELD        2UL

typedef struct _TBT_U4_LINKDOWN {
    ULONG Port;
    ULONG Generation;
    ULONG PcieDown;
    BOOLEAN MapValid;
    ULONG MapRid;
    BOOLEAN PathClosed;     /* step 1 succeeded and its read-back matched */
    BOOLEAN AnchorValid;
    ULONG AnchorUs;
    ULONG RemainingUs;      /* of the 3 s, 0 when none */
    BOOLEAN Nested;         /* inside another sequence's safe point */
} TBT_U4_LINKDOWN, *PTBT_U4_LINKDOWN;

typedef ULONG (*TBT_U4_LINKDOWN_FN)(PVOID context, const TBT_U4_LINKDOWN *info);

/* A tunnel up at suspend (section 7.6). */
typedef struct _TBT_U4_OBLIGATION {
    BOOLEAN Used;
    ULONG Port;
    ULONG Generation;
    BOOLEAN Usb3;           /* FALSE: PCIe */
    BOOLEAN BuiltHere;
    ULONG Hd;
    ULONG Hu;
} TBT_U4_OBLIGATION, *PTBT_U4_OBLIGATION;

/* ---------------------------------------------------------------------- */
/* The quirk table (section 6.10)                                         */
/* ---------------------------------------------------------------------- */

#define TBT_U4_QUIRK_USB3_MAX_BW    0x01UL  /* quirk_usb3_maximum_bandwidth */
#define TBT_U4_QUIRK_NO_CLX         0x02UL  /* quirk_clx_disable: satisfied by default */
#define TBT_U4_QUIRK_DP_CREDITS     0x04UL  /* quirk_dp_credit_allocation: nothing */
#define TBT_U4_QUIRK_BLOCK_RPM      0x08UL  /* quirk_block_rpm_in_redrive: nothing */
#define TBT_U4_QUIRK_FORCE_POWER    0x10UL  /* quirk_force_power_link: nothing */

/* The router configuration space's vendor and device (Linux quirks.c
 * tb_check_quirks); DROM ids for the one row matched by them. Any IRQL. */
ULONG TbtU4QuirkMatch(ULONG vendor, ULONG device);
ULONG TbtU4QuirkMatchDrom(ULONG dromVendor, ULONG dromDevice);

/* Section 6.4 step 1's classification of an Adapter State. Any IRQL. */
#define TBT_U4_LANE_CONNECTED       1UL
#define TBT_U4_LANE_TRANSITIONAL    2UL
#define TBT_U4_LANE_DISCONNECTED    3UL     /* CLd */
#define TBT_U4_LANE_DISABLED        4UL
#define TBT_U4_LANE_UNKNOWN         5UL
ULONG TbtU4LaneClass(ULONG adapterState);

/* ---------------------------------------------------------------------- */
/* The connection manager                                                 */
/* ---------------------------------------------------------------------- */

typedef struct _TBT_U4_COUNTERS {
    ULONG PortChecks;
    ULONG Connects;
    ULONG RoutersEnumerated;
    ULONG RoutersAdopted;
    ULONG RoutersRefused;
    ULONG Tbt3Routers;
    ULONG RopIssued;
    ULONG RopUnsupported;
    ULONG RopFailed;
    ULONG RopTimeouts;
    ULONG RopBlocked;
    ULONG DromReads;
    ULONG DromCrcFailures;
    ULONG BondAttempts;
    ULONG BondX2;
    ULONG BondSingle;
    ULONG Lane1Disabled;
    ULONG ConfigReadyTimeouts;
    ULONG TmuEnabled;
    ULONG TmuFailures;
    ULONG PcieBuilt;
    ULONG PcieFailed;
    ULONG PcieTornDown;
    ULONG PcieLost;
    ULONG Usb3Built;
    ULONG Usb3Failed;
    ULONG Usb3TornDown;
    ULONG Usb3Disconnected;
    ULONG DetectTimeouts;
    ULONG PendingTimeouts;
    ULONG HcaTimeouts;
    ULONG Quarantined;
    ULONG QuarantineLifted;
    ULONG PortsPreserved;
    ULONG PortsHeld;
    ULONG ConnectsDeferred;
    ULONG ResumeBudgetsSpent;
    ULONG PortsCleaned;
    ULONG DownstreamPortResets;
    ULONG HostRouterResets;
    ULONG QuirksFired;
    ULONG StatementsSent;
    ULONG StatementsWithheld;
    ULONG Unplugs;
    ULONG UnplugsNested;
    ULONG QuickReplugs;
    ULONG HotPlugLane0;
    ULONG HotPlugOther;     /* lane 1, DP, others: counted only */
    ULONG HotPlugDownstream;
    ULONG LinksDiscovered;
    ULONG LinksRemoved;
    ULONG ActionOverflows;
    ULONG Ejects;
    ULONG ContainedRefused; /* requests and entry points refused, contained */
    ULONG ContainedRetired; /* actions and deferred tails retired at containment */
    ULONG RecoverAttempts;  /* TbtU4Recover entered (section 7.7) */
    ULONG RecoverOk;        /* committed */
    ULONG RecoverFinal;     /* the verified stop failed: final for the boot */
    ULONG RecoverPreserved; /* tunnels verified and kept */
    ULONG RecoverRebuilt;   /* tunnels drained, then rebuilt after the commit */
    ULONG RecoverDeparted;  /* tunnels whose router left: host side drained */
    ULONG RecoverHeld;      /* tunnels HELD for anything short of the rules */
} TBT_U4_COUNTERS;

/* The start stages (section 3.5's two inert forms). */
#define TBT_U4_STAGE_NONE           0UL
#define TBT_U4_STAGE_RINGS          1UL     /* rings run: activation */
#define TBT_U4_STAGE_HOST           2UL     /* the host router answered */
#define TBT_U4_STAGE_RUNNING        3UL

typedef struct _TBT_U4CM {
    const TBT_HOST *Host;
    PTBT_RING Ring;
    TBT_CTL Ctl;
    TBT_U4_CONFIG Config;

    PVOID Va;
    ULONG Phys;
    ULONG Bytes;
    ULONG Stage;            /* TBT_U4_STAGE_* */
    BOOLEAN Failed;         /* failure after activation: inert, rings stopped */
    ULONG InertReason;      /* tbt_hw.h TBT_INERT_* */
    BOOLEAN Suspended;
    BOOLEAN Contained;      /* the host interface lost: TbtU4Contain */

    TBT_U4_LINKDOWN_FN LinkDown;
    PVOID LinkDownContext;

    TBT_U4_ROUTER HostRouter;
    ULONG PortCount;
    TBT_U4_PORT Ports[TBT_U4_MAX_PORTS];
    TBT_U4_PORT ResumeSaved[TBT_U4_MAX_PORTS];  /* TbtU4Resume's copy: kept
                                                 * off the kernel stack */
    TBT_U4_LINK Links[TBT_U4_MAX_LINKS];
    TBT_U4_QENTRY Quarantine[TBT_U4_QUARANTINE];

    /* Section 7.4: the port whose sequence is running, and the nested
     * teardown in progress. */
    ULONG ActivePort;       /* 0 for none */
    BOOLEAN InTeardown;
    ULONG DeferredTail[TBT_U4_MAX_PORTS];   /* steps 4 to 6 owed, by port */

    TBT_U4_OBLIGATION Obligations[TBT_U4_OBLIGATIONS];
    BOOLEAN ResumeHeld;     /* the last resume left an obligation unmet */
    BOOLEAN Budgeted;       /* a resume budget is running */
    BOOLEAN BudgetSpent;    /* and ran out: requests refused, ports HELD */
    ULONG BudgetStartUs;
    ULONG BudgetUs;
    ULONG BudgetWaitedUs;   /* the waits known to have passed: the backstop */
    ULONG ResumeHostUs;     /* the budgets, TBT_U4_RESUME_* unless a test sets them */
    ULONG ResumeEachUs;
    ULONG BudgetRingBase;   /* the ring's waited sum when the budget began */
    BOOLEAN HostTmuOk;      /* the host router's TMU usable (or none) */
    BOOLEAN HostTmuTdOwed;  /* its Time Disruption set by this driver, not
                             * yet read back clear */

    TBT_U4_ACTION Actions[TBT_U4_ACTION_QUEUE];
    ULONG ActionHead;
    ULONG ActionCount;
    BOOLEAN ResyncPending;

    TBT_U4_ROUTER Scratch;  /* a router below depth 1, read in turn */

    /* Section 7.7's recovery snapshot (decision U19), taken by
     * TbtU4Contain and again by TbtU4Snapshot at each new episode: the
     * port, host router and link records copied whole, the stage. The
     * port records themselves are the journal of an attempt that failed:
     * what it drained reads down there, and a later snapshot copies it. */
    BOOLEAN SnapValid;
    ULONG SnapStage;
    ULONG SnapPortCount;
    TBT_U4_PORT SnapPorts[TBT_U4_MAX_PORTS];
    TBT_U4_ROUTER SnapHost;
    TBT_U4_LINK SnapLinks[TBT_U4_MAX_LINKS];
    BOOLEAN Recovering;     /* TbtU4Recover's work: the core's gate open
                               for it alone (decision U26) */
    BOOLEAN RecoverRings;   /* this attempt restarted the rings */
    ULONG SnapDeferred[TBT_U4_MAX_PORTS];   /* tails abandoned at containment */
    BOOLEAN RelockExplained;/* the host router was enumerated afresh by the
                               recovery: its lane 0 Lock set again is its
                               own reset, not removal evidence */
    BOOLEAN RecRestored;    /* this attempt put the snapshot's port records
                               back over the host router's fresh walk: until
                               then a failure restores them whole, so the
                               next snapshot copies no half-built table */
    BOOLEAN RecForeign;     /* this attempt read a recorded entry (or a
                               quarantined one) as another path's: the
                               fabric is not as this driver left it, and
                               the attempt is refused for the episode      */
    ULONG HwErrors;         /* the core's requests that ended with no answer
                               (the ring not moving or stopped, or no reply
                               at all): a section 9.2 discovery that saw one
                               is not recorded complete, and a recovery
                               discovers the port again                     */

    TBT_U4_COUNTERS Counters;
} TBT_U4CM, *PTBT_U4CM;

/* Bind `cm` to its ring, the accessor and the configuration. */
VOID TbtU4Init(PTBT_U4CM cm, PTBT_RING ring, const TBT_HOST *host,
               const TBT_U4_CONFIG *config);
VOID TbtU4SetLinkDown(PTBT_U4CM cm, TBT_U4_LINKDOWN_FN fn, PVOID context);

/*
 * Record 22 section 4.4 steps 1 to 6 over the common buffer the driver
 * allocated (`va`, `phys`, `bytes` of at least TBT_RING_U4_DMA_BYTES): ring
 * 0 taken over and started, test (d)'s router half, the host router, the
 * discovery of section 9.2 (and the cleaning of 9.3 where configured), hot
 * plug enabled, cold devices connected. TBT_OK, or a negative result: a
 * failure before the rings start changes nothing; one after (section 3.5,
 * "after activation") stops the rings - nothing of theirs is pending when
 * it returns, so the block may be freed - leaves the fabric as it is and
 * the instance Failed with InertReason set.
 */
LONG TbtU4Start(PTBT_U4CM cm, PVOID va, ULONG phys, ULONG bytes);

/* The idle tick (section 7.1): every queued event and note, the port scan
 * with the PCIe-loss check, quarantine retries, and every port check they
 * queue. Bounded; what is left waits for the next call. */
VOID TbtU4Poll(PTBT_U4CM cm);

/* The next queued tunnel change, oldest first. */
BOOLEAN TbtU4NextAction(PTBT_U4CM cm, PTBT_U4_ACTION out);

/* Section 10.3: one configuration read of the port's cached recipient,
 * issued at `issuedUs` on the TBT_HOST clock, `matched` when it returned
 * that function's ids. The latest matching sample of the current
 * generation is the anchor. */
VOID TbtU4PresenceSample(PTBT_U4CM cm, ULONG port, ULONG generation,
                         ULONG issuedUs, BOOLEAN matched);

/* Section 10.4: the orderly teardown of one port's tunnels (section 9.3
 * step 1, both ends), the port staying down until a re-plug. */
LONG TbtU4Eject(PTBT_U4CM cm, ULONG port);
/* The same for the PCIe tunnel alone: a USB3 tunnel on the port stays up,
 * its devices being the host xHCI's, which the PCI-side eject never asked
 * to stop (task 40-A.9's entry point). */
LONG TbtU4EjectPcie(PTBT_U4CM cm, ULONG port);

/* Section 7.6 and 11: record every tunnel up as an obligation, then stop
 * the rings. */
VOID TbtU4Suspend(PTBT_U4CM cm);
/* Resume to D0 (after the platform preparation): the rings and the host
 * router again, then every obligation verified, rebuilt, torn down on
 * removal evidence or HELD. TBT_OK when every obligation was met (the NT
 * hold is released as succeeded), TBT_E_STATE when one was not (the
 * failure path), another negative result when the domain failed. */
LONG TbtU4Resume(PTBT_U4CM cm);
/* The resume bound of section 7.6 in microseconds for `obligations`
 * tunnels at the clock's tick: the platform preparation (1.05 s and a
 * tick), the budgets TbtU4Resume enforces on the clock (the host router,
 * then each obligation), and the margin for the one wait or request
 * running when a budget runs out - after which TbtU4Resume stops, marks
 * what is left HELD and returns the failure path. */
ULONG TbtU4ResumeBoundUs(ULONG obligations, ULONG tickUs);

/* Stop (section 11): rings stopped, nothing on the fabric changed.
 * Idempotent. */
VOID TbtU4Stop(PTBT_U4CM cm);

/*
 * The host interface lost (record 22 section 7.7, record 21 section 5.5's
 * monitor): the instance contained for good, with no register access of
 * any kind - every request refused before the transport, every wait of
 * the core's own ended, the queued actions retired, the deferred teardown
 * tails abandoned, every anchor invalidated. Ports, tunnels, quarantine
 * and obligations are kept as the last reading left them, never acted on
 * again. From then on TbtU4Poll, TbtU4Eject, TbtU4EjectPcie, TbtU4Suspend
 * and TbtU4Resume do nothing (the last two refuse), TbtU4Stop writes
 * nothing (the rings are never proven stopped), TbtU4NextAction returns
 * FALSE, TbtU4AllPortsDisconnected FALSE and TbtU4AnyRouterConnected TRUE.
 * Idempotent; only a fresh TbtU4Init clears it. It may be called from
 * inside a sequence (the host's Cancelled callback being the usual way a
 * running sequence learns of the loss); the sequence then stops at its
 * next request or wait.
 */
VOID TbtU4Contain(PTBT_U4CM cm);

/*
 * Record 22 section 7.7's recovery (task 42-A.10, decisions U19 to U28).
 *
 * TbtU4Snapshot: the recovery snapshot taken again from the records as
 * they now are (a new episode after a failed attempt); TbtU4Contain takes
 * the first. No access.
 *
 * TbtU4Recover: part 3 steps 1 to 5 on a contained instance, under the
 * driver's RECOVERY access class: the rings stopped and verified stopped
 * in the kept buffer (a stop not verified sets *final: no recovery for the
 * boot, the buffer kept), restarted there and the size register read back
 * as programmed; the host router enumerated afresh or adopted (section
 * 6.1, test (d)'s router half); every tunnel up at the loss classified by
 * reading alone - verified and preserved, to be rebuilt (the router the
 * recorded one, its side of the tunnel intact, the host side gone),
 * departed (removal evidence, or a router that gave up its enumeration),
 * or HELD - and the rebuilt and departed ones drained by their recorded
 * teardown, the device side only on the positively identified router.
 * Builds nothing and queues no action. TBT_OK, or a negative result: the
 * caller then calls TbtU4RecoverFail.
 *
 * TbtU4RecoverDemote: before the commit, a tunnel TbtU4Recover preserved
 * sent back to the rebuild classification by the driver (its member's
 * secondary bus changed, or its channel entry lost UP at containment:
 * R8a, R8b): its PCIe tunnel drained at both ends, RebuildPcie and
 * RemovalDebt set. TRUE when drained; FALSE leaves the port HELD.
 *
 * TbtU4RecoverCommit: part 3 step 7's flag updates alone, no access, any
 * IRQL (the driver holds its work lock): Contained, Recovering and
 * Suspended cleared, the stage RUNNING, every port to be checked. The
 * rebuilds then run from TbtU4Poll (the per-tunnel rebuild transition,
 * decision U28) on ports not Barred.
 *
 * TbtU4RecoverFail: the failure path: the rings this attempt restarted
 * stopped (verified where the gate still admits it), Recovering cleared,
 * the instance contained as before. PASSIVE_LEVEL.
 *
 * TbtU4TakeDebt: RemovalDebt read and cleared for `port`.
 * TbtU4SetBarred: the driver's barrier for `port`.
 */
VOID TbtU4Snapshot(PTBT_U4CM cm);
LONG TbtU4Recover(PTBT_U4CM cm, BOOLEAN *final);
BOOLEAN TbtU4RecoverDemote(PTBT_U4CM cm, ULONG port);
VOID TbtU4RecoverCommit(PTBT_U4CM cm);
VOID TbtU4RecoverFail(PTBT_U4CM cm);
BOOLEAN TbtU4TakeDebt(PTBT_U4CM cm, ULONG port);
VOID TbtU4SetBarred(PTBT_U4CM cm, ULONG port, BOOLEAN barred);

/* Whether a device router is connected on any port (record 22 section
 * 4.2's condition for clearing force power at stop). */
BOOLEAN TbtU4AnyRouterConnected(const TBT_U4CM *cm);

/* A fresh reading, on the running rings, before TbtU4Stop: TRUE only when
 * every host USB4 port's lane 0 reads its Adapter State as CLd (record 22
 * section 6.4 step 1) - each read answered, none connected, transitional,
 * disabled or reserved. FALSE when failed, suspended or not running. The
 * evidence record 22 section 4.2 asks before force power is cleared. */
BOOLEAN TbtU4AllPortsDisconnected(PTBT_U4CM cm);

/* A port by its 1-based index, for the log and the snapshot. */
const TBT_U4_PORT *TbtU4Port(const TBT_U4CM *cm, ULONG port);

#endif /* TBT_U4CM_H */
