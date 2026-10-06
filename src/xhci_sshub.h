/*
 * xhci_sshub.h - the SuperSpeed hub class inside the bus, its pure half
 * (roadmap-hcd.md tasks 30-A.1 and 30-A.2; xhci-data-structures.md section
 * 11, the USB 3.2 hub class as transcribed by task 30-0, every row of which
 * is verified against the USB 3.2 specification, revision 1.1; every
 * "USB 3.2 p.N" is the page that copy prints).
 *
 * A USB 3 hub is two hubs: its USB 2.0 half on a High-Speed port is a
 * Phase 27 hub (xhci_hub.c) and its SuperSpeed half on a SuperSpeed port is
 * this file's. The halves are independent hubs of the bus; nothing pairs
 * them beyond a counter (XhciSsHubLooksPaired).
 *
 * What the SuperSpeed half changes, and therefore what is decided here with
 * no register, lock or kernel service, so a host suite drives each:
 *
 *   - the hub descriptor, type 0x2A, 12 bytes, with bHubHdrDecLat and
 *     wHubDelay, at most 15 ports;
 *   - SET_HUB_DEPTH, the request that tells the hub which Route String
 *     nibble is its own, owed before any port is read;
 *   - wPortStatus and wPortChange at SuperSpeed: a link-state field where
 *     USB 2.0 has suspend and the speed bits, PORT_POWER at bit 9 rather
 *     than 8, no C_PORT_ENABLE or C_PORT_SUSPEND, and three change bits of
 *     its own (C_BH_PORT_RESET, C_PORT_LINK_STATE, C_PORT_CONFIG_ERROR);
 *   - a port's decision (the root port's rules and the USB 2.0 hub's, with
 *     the link's): a link in SS.Inactive or Compliance Mode is recovered by
 *     a warm reset (BH_PORT_RESET) within xhci_link.h's budget;
 *   - a port reset's kind, hot (PORT_RESET) from U0 and warm otherwise, its
 *     progress, and the change bits a finished one leaves to clear;
 *   - a SuperSpeedPlus hub's extended port status (GET_PORT_STATUS with
 *     wValue 2): a downstream port's sublink speed ids and lane counts, the
 *     rate they mean from the hub's own SuperSpeedPlus capability, and the
 *     Protocol Speed ID the root port's protocol has for that rate.
 *
 * The Route String, root port and depth are xhci_topo.c's as for any hub;
 * a SuperSpeed path has no transaction translator, and xhci_topo.c's TT
 * walk finds none on it since no node of it is High-Speed.
 *
 * DDK-free: part of the pure core. C89. IRQL: any.
 */

#ifndef XHCI_SSHUB_H
#define XHCI_SSHUB_H

#include "xhci_compat.h"

struct _XHCI_PORT_MAP;
struct _XHCI_PIPE_BOS;

#define XHCI_SSHUB_OK           0UL
#define XHCI_SSHUB_MALFORMED    1UL
#define XHCI_SSHUB_BAD_PARAM    2UL
#define XHCI_SSHUB_NOT_FOUND    3UL

/* bDeviceProtocol of a SuperSpeed hub (USB 3.2 10.15.1, USB 3.2 p.432;
 * verified). */
#define XHCI_SSHUB_PROTOCOL             0x03UL

/* Hub class requests a USB 3 hub adds (USB 3.2 Tables 10-7 and 10-8, USB 3.2
 * p.440; verified): SET_HUB_DEPTH (bmRequestType 0x20, wValue the depth,
 * wIndex 0, no data; a depth above 4 is a Request Error, and before the hub
 * is configured the answer is undefined, 10.16.2.9, p.451-452) and
 * GET_PORT_ERR_COUNT (not used). */
#define XHCI_SSHUB_REQ_SET_HUB_DEPTH    0x0CU
#define XHCI_SSHUB_REQ_GET_PORT_ERR_COUNT 0x0DU

/* The Enhanced SuperSpeed hub descriptor (USB 3.2 10.15.2.1, Table 10-5,
 * USB 3.2 p.437-438; verified): GET_DESCRIPTOR with wValue 0x2A00, 12
 * bytes. */
#define XHCI_SSHUB_DESC_TYPE            0x2AUL
#define XHCI_SSHUB_DESC_VALUE           0x2A00U
#define XHCI_SSHUB_DESC_BYTES           12UL
/* A SuperSpeed hub has at most 15 downstream ports, and its Route String
 * nibble names each exactly - the "above 14 is written 15" clamp of xHCI
 * Table 6-4 footnote 106 is for High- and Full-Speed hubs only. */
#define XHCI_SSHUB_MAX_PORTS            15UL

/* wPortStatus at SuperSpeed (USB 3.2 Table 10-13, USB 3.2 p.445-446;
 * verified). PORT_CONNECTION keeps its previous value in DSPORT.Resetting
 * and DSPORT.Error (p.446). */
#define XHCI_SSHUB_PORT_CONNECTION      0x0001UL
#define XHCI_SSHUB_PORT_ENABLE          0x0002UL
#define XHCI_SSHUB_PORT_OVER_CURRENT    0x0008UL
#define XHCI_SSHUB_PORT_RESET           0x0010UL
#define XHCI_SSHUB_PORT_LINK_MASK       0x01E0UL
#define XHCI_SSHUB_PORT_LINK_SHIFT      5
#define XHCI_SSHUB_PORT_POWER           0x0200UL
#define XHCI_SSHUB_PORT_SPEED_MASK      0x1C00UL
#define XHCI_SSHUB_PORT_SPEED_SHIFT     10

/* wPortChange at SuperSpeed (USB 3.2 Table 10-14, USB 3.2 p.448-450;
 * verified). Bits 1 and 2, USB 2.0's C_PORT_ENABLE and C_PORT_SUSPEND, are
 * reserved. C_PORT_RESET and C_BH_PORT_RESET are set only on a reset that
 * succeeds (Resetting to Enabled); a failed one ends in DSPORT.Disconnected
 * with neither (10.3.1.6, p.388-389). */
#define XHCI_SSHUB_C_PORT_CONNECTION    0x0001UL
#define XHCI_SSHUB_C_PORT_OVER_CURRENT  0x0008UL
#define XHCI_SSHUB_C_PORT_RESET         0x0010UL
#define XHCI_SSHUB_C_BH_PORT_RESET      0x0020UL
#define XHCI_SSHUB_C_PORT_LINK_STATE    0x0040UL
#define XHCI_SSHUB_C_PORT_CONFIG_ERROR  0x0080UL
#define XHCI_SSHUB_C_PORT_MASK          0x00F9UL

/* PORT_LINK_STATE values (USB 3.2 Table 10-13, USB 3.2 p.446; verified;
 * 0xC-0xF reserved). */
#define XHCI_SSHUB_LINK_U0              0x0UL
#define XHCI_SSHUB_LINK_U1              0x1UL
#define XHCI_SSHUB_LINK_U2              0x2UL
#define XHCI_SSHUB_LINK_U3              0x3UL
#define XHCI_SSHUB_LINK_DISABLED        0x4UL
#define XHCI_SSHUB_LINK_RX_DETECT       0x5UL
#define XHCI_SSHUB_LINK_INACTIVE        0x6UL
#define XHCI_SSHUB_LINK_POLLING         0x7UL
#define XHCI_SSHUB_LINK_RECOVERY        0x8UL
#define XHCI_SSHUB_LINK_HOT_RESET       0x9UL
#define XHCI_SSHUB_LINK_COMPLIANCE      0xAUL
#define XHCI_SSHUB_LINK_LOOPBACK        0xBUL

/* Port feature selectors at SuperSpeed (USB 3.2 Table 10-9, USB 3.2 p.441;
 * verified; 21 is reserved, used by USB 2.0).
 * PORT_RESET 4, PORT_POWER 8, C_PORT_CONNECTION 16, C_PORT_OVER_CURRENT 19
 * and C_PORT_RESET 20 are USB 2.0's numbers and xhci_hub.h's names. */
#define XHCI_SSHUB_FEAT_PORT_LINK_STATE     5UL
#define XHCI_SSHUB_FEAT_PORT_U1_TIMEOUT     23UL
#define XHCI_SSHUB_FEAT_PORT_U2_TIMEOUT     24UL
#define XHCI_SSHUB_FEAT_C_PORT_LINK_STATE   25UL
#define XHCI_SSHUB_FEAT_C_PORT_CONFIG_ERROR 26UL
#define XHCI_SSHUB_FEAT_PORT_REMOTE_WAKE_MASK 27UL
#define XHCI_SSHUB_FEAT_BH_PORT_RESET       28UL
#define XHCI_SSHUB_FEAT_C_BH_PORT_RESET     29UL
#define XHCI_SSHUB_FEAT_FORCE_LINKPM_ACCEPT 30UL

/* PORT_REMOTE_WAKE_MASK's mask, in wIndex 15:8 (USB 3.2 10.16.2.10, Table
 * 10-18, USB 3.2 p.454-455; verified): Conn_RWEnable, Disconn_RWEnable,
 * OC_RWEnable, bits 7:3 reserved, all zero after power-on or a hub reset.
 * The bus suspends no hub port, so it sets none of them. */
#define XHCI_SSHUB_WAKE_CONNECT         0x01UL
#define XHCI_SSHUB_WAKE_DISCONNECT      0x02UL
#define XHCI_SSHUB_WAKE_OVER_CURRENT    0x04UL

/* GET_PORT_STATUS's status type in wValue 7:0 (USB 3.2 10.16.2.6, Table
 * 10-12, USB 3.2 p.444-445; verified), and the extended answer's length:
 * wPortStatus, wPortChange and dwExtPortStatus. Type 1 (PD_STATUS) is
 * deprecated and "shall not be used". Type 2 is a Request Error to a hub
 * that defines no SuperSpeedPlus USB Capability; the specification keys it
 * to the capability alone, and XhciSsHubHasExtStatus also asks bcdUSB
 * 0x0310 - narrower, so never a request a hub refuses. */
#define XHCI_SSHUB_STATUS_STANDARD      0U
#define XHCI_SSHUB_STATUS_PD            1U
#define XHCI_SSHUB_STATUS_EXT           2U
#define XHCI_SSHUB_STATUS_EXT_BYTES     8UL
#define XHCI_SSHUB_BCD_EXT_STATUS       0x0310UL

/* dwExtPortStatus (USB 3.2 Table 10-15, USB 3.2 p.450; verified): the Rx
 * and Tx sublink speed ids, and the lane counts, each zero-based (the count
 * minus one); valid with PORT_ENABLE. The port's speed is the sublink's
 * lane speed times the lane count (10.16.2.6.3). */
#define XHCI_SSHUB_EXT_RX_SSID(dw)      (((ULONG)(dw)) & 0xFUL)
#define XHCI_SSHUB_EXT_TX_SSID(dw)      ((((ULONG)(dw)) >> 4) & 0xFUL)
#define XHCI_SSHUB_EXT_RX_LANES(dw)     (((((ULONG)(dw)) >> 8) & 0xFUL) + 1UL)
#define XHCI_SSHUB_EXT_TX_LANES(dw)     (((((ULONG)(dw)) >> 12) & 0xFUL) + 1UL)

/* A bmSublinkSpeedAttr of the SuperSpeedPlus capability (USB 3.2 9.6.2.5,
 * Table 9-19, USB 3.2 p.357-358; xhci-data-structures.md 10.7; verified).
 * ST is two bits of their own: bit 6 asymmetric, bit 7 transmit. */
#define XHCI_SSHUB_SSA_SSID(dw)         (((ULONG)(dw)) & 0xFUL)
#define XHCI_SSHUB_SSA_LSE(dw)          ((((ULONG)(dw)) >> 4) & 0x3UL)
#define XHCI_SSHUB_SSA_ST(dw)           ((((ULONG)(dw)) >> 6) & 0x3UL)
#define XHCI_SSHUB_SSA_LP(dw)           ((((ULONG)(dw)) >> 14) & 0x3UL)
#define XHCI_SSHUB_SSA_LSM(dw)          ((((ULONG)(dw)) >> 16) & 0xFFFFUL)
/* ST bit 7: 1 a Tx attribute, 0 an Rx (or symmetric) one. */
#define XHCI_SSHUB_SSA_ST_TX            0x2UL
#define XHCI_SSHUB_SSA_LP_SSP           1UL

/* ----------------------------------------------------------------------- */

typedef struct _XHCI_SSHUB_DESC {
    ULONG Ports;            /* bNbrPorts                                  */
    ULONG Characteristics;  /* wHubCharacteristics                        */
    ULONG PowerGoodMs;      /* bPwrOn2PwrGood x 2                         */
    ULONG ControllerCurrent;/* bHubContrCurrent (in 4 mA units at SS)     */
    ULONG HeaderDecodeLatency; /* bHubHdrDecLat                           */
    ULONG HubDelayNs;       /* wHubDelay                                  */
    ULONG Removable;        /* DeviceRemovable, bit n port n              */
    ULONG Managed;          /* min(Ports, xhci_hub.h's XHCI_HUB_MAX_PORTS) */
} XHCI_SSHUB_DESC, *PXHCI_SSHUB_DESC;

/*
 * Parse a SuperSpeed hub descriptor reply of `length` bytes.
 * XHCI_SSHUB_MALFORMED for a reply shorter than 12 bytes, a bLength below
 * 12 or past what arrived, a type other than 0x2A, no ports, or more than
 * 15 - a port above 15 has no Route String nibble at SuperSpeed. The bus
 * manages the first XHCI_HUB_MAX_PORTS (14) of them, as it does on a USB
 * 2.0 hub, since every hub object has that many port objects: a fifteenth
 * port is left unpowered.
 */
ULONG XhciSsHubParseDescriptor(const UCHAR *data, ULONG length,
                               PXHCI_SSHUB_DESC out);

/* The PORT_LINK_STATE field of a wPortStatus. */
ULONG XhciSsHubLinkState(ULONG status);

/*
 * One port's GET_STATUS answer, decided, into xhci_hub.h's decision shape
 * (Clear holding XHCI_SSHUB_C_* bits here) plus what is SuperSpeed's own:
 *
 *   C_PORT_CONNECTION   whatever the port held goes, and a connection now
 *                       is a new device - the USB 2.0 hub's rule;
 *   no connect change   a port that reads disconnected goes; an Empty one
 *                       that reads connected starts; a connected port under
 *                       a device the machine holds that reads not enabled
 *                       is enumerated afresh (SuperSpeed has no
 *                       C_PORT_ENABLE to say the hub disabled it);
 *   link SS.Inactive or Compliance Mode, whatever the change bits:
 *                       WarmReset - checked before every other rule. The
 *                       device the machine holds goes first (Disconnect,
 *                       carried out to the end of its teardown), only then
 *                       does the executor issue BH_PORT_RESET within the
 *                       port's warm-reset budget, and the port is decided
 *                       again from what the reset left. A config error
 *                       seen with such a link takes this branch too, so
 *                       its recovery is the warm reset's;
 *   C_PORT_CONFIG_ERROR on a link in any other state: ConfigError, a
 *                       device the machine holds goes, and the port stays
 *                       down until its next connect change. A hub that
 *                       follows USB 3.2 never reaches this rule: a config
 *                       error always puts the port in DSPORT.Error, its
 *                       link in eSS.Inactive (10.3.1.4, USB 3.2 p.387;
 *                       p.450), which the rule above takes;
 *   C_PORT_OVER_CURRENT with PORT_POWER clear: OverCurrent and Repower;
 *   C_PORT_LINK_STATE, C_PORT_RESET, C_BH_PORT_RESET outside a reset:
 *                       cleared, nothing else - except, when no rule above
 *                       took the port and the machine holds a device on a
 *                       connected, enabled port:
 *   link U3             Resume: the bus brings the link to U0 before
 *                       anything else is asked of the device - suspend
 *                       handled and never initiated, as at a USB 2.0 hub
 *                       port (xhci_hub.h; the Phase 27 and Phase 30 merge);
 *   C_PORT_LINK_STATE with the link in U0: Resumed, a host-requested U3
 *                       exit finished (USB 3.2 10.16.2.6.2, printed p.449:
 *                       a remote-wake U3 exit sets no C_PORT_LINK_STATE,
 *                       so it is never seen here) - the device is
 *                       held through the resume recovery as at USB 2.0.
 * A connected port under a held device that reads not enabled is the rule
 * above it (enumerated afresh), whatever its link.
 */
typedef struct _XHCI_SSHUB_PORT_DECISION {
    ULONG Clear;            /* XHCI_SSHUB_C_* bits to clear               */
    ULONG Disconnect;
    ULONG Connect;
    ULONG OverCurrent;
    ULONG Repower;
    ULONG WarmReset;        /* recover the link with BH_PORT_RESET        */
    ULONG ConfigError;
    ULONG LinkChange;       /* C_PORT_LINK_STATE was set                  */
    ULONG Resume;           /* held device, link in U3: resume to U0      */
    ULONG Resumed;          /* held device, a U3 exit finished            */
    ULONG Disabled;         /* a held device's port no longer enabled: a
                             * re-enumeration charged to the location    */
} XHCI_SSHUB_PORT_DECISION, *PXHCI_SSHUB_PORT_DECISION;

VOID XhciSsHubPortDecide(ULONG state, ULONG status, ULONG change,
                         PXHCI_SSHUB_PORT_DECISION out);

/* The C_ feature selector that clears one XHCI_SSHUB_C_* bit, or 0. */
ULONG XhciSsHubClearSelector(ULONG changeBit);

/*
 * The reset an enumeration owes a SuperSpeed hub port (the root port's
 * policy, xhci_link.c, carried to the hub): HOT (PORT_RESET) from U0, U1,
 * U2 or Recovery with the port connected; WARM (BH_PORT_RESET) from
 * SS.Inactive, Compliance Mode, Loopback, U3, a stuck Polling or Hot Reset
 * with a connection, or a connected port whose link state says nothing
 * trained; NONE with nothing connected and the link in Rx.Detect or
 * SS.Disabled - there is nothing to reset. *converted is 1 when a hot
 * reset was the want and a warm one is decided. USB 3.2 7.4.2 (p.158-159)
 * has a hub answer PORT_RESET with a warm reset in exactly U3, Loopback,
 * Compliance Mode and eSS.Inactive and a hot one otherwise; the warm reset
 * from Polling and Hot Reset is this driver's choice, which BH_PORT_RESET
 * (valid in every link state but eSS.Disabled) allows.
 */
#define XHCI_SSHUB_RESET_NONE   0UL
#define XHCI_SSHUB_RESET_HOT    1UL
#define XHCI_SSHUB_RESET_WARM   2UL

ULONG XhciSsHubResetKind(ULONG status, PULONG converted);

/*
 * A reset's progress from one GET_STATUS answer: xhci_hub.h's
 * XHCI_HUB_RESET_PENDING, _ENABLED or _FAILED. Done is C_PORT_RESET or
 * C_BH_PORT_RESET - a hub may finish a hot reset as a warm one (*warmSeen)
 * - or, with neither yet, the reset bit clear with the port enabled in U0.
 * Enabled is PORT_ENABLE with the link in U0 and the port connected.
 */
ULONG XhciSsHubResetProgress(ULONG status, ULONG change, PULONG warmSeen);

/*
 * A resume's progress from one GET_STATUS answer (xhci_hub.h's
 * XHCI_HUB_RESUME_*): GONE when the port reads disconnected; PENDING while
 * the link is still in U3 or Recovery; DONE once it is in U0, U1 or U2 with
 * the port enabled; DISABLED for anything else - the link fell elsewhere
 * (SS.Inactive, SS.Disabled, Compliance Mode, ...), a reserved link state
 * (0xC to 0xF, USB 3.2 Table 10-13, printed p.446), or the port
 * reads not enabled - and the device is enumerated afresh, its reset a warm
 * one where the link needs it (XhciSsHubResetKind).
 */
ULONG XhciSsHubResumeProgress(ULONG status);

/* The change bits a finished reset leaves for the executor to clear:
 * C_PORT_RESET and C_BH_PORT_RESET where set, and after a warm reset also
 * C_PORT_LINK_STATE and C_PORT_CONNECTION - a warm reset retrains the link,
 * which the hub reports as a connect change that is not a new device and
 * would otherwise start the device over. */
ULONG XhciSsHubResetClears(ULONG change, ULONG warm);

/* ----------------------------------------------------------------------- */
/* SuperSpeedPlus downstream rates                                          */
/* ----------------------------------------------------------------------- */

/* Whether the bus asks a hub for the extended port status: bcdUSB 0x0310 or
 * above and a SuperSpeedPlus capability in its BOS descriptor. The
 * specification's condition is the capability alone (Table 10-12's rule,
 * USB 3.2 p.445); also asking bcdUSB is a leniency in the safe direction -
 * it never sends the request to a hub that would refuse it, and skips only
 * a hub that has the capability with an older bcdUSB. */
ULONG XhciSsHubHasExtStatus(ULONG bcdUsb, const struct _XHCI_PIPE_BOS *bos);

/*
 * The rate a sublink speed id names on a hub, from its SuperSpeedPlus
 * capability's attributes: the Rx (or symmetric) attribute whose SSID
 * matches, its lane rate LSM x 10^(3 x LSE) bit/s in kbit/s, and *plus 1
 * when its LP says SuperSpeedPlus. XHCI_SSHUB_NOT_FOUND when no attribute
 * names it or the rate does not fit a ULONG of kbit/s.
 */
ULONG XhciSsHubSublinkRate(const struct _XHCI_PIPE_BOS *bos, ULONG ssid,
                           PULONG laneKbps, PULONG plus);

/*
 * A downstream port's link from dwExtPortStatus: the Rx sublink's lane
 * rate, the Rx lane count and the aggregate (lane rate x lanes), and
 * whether it is SuperSpeedPlus - a SuperSpeedPlus LP, or more than one
 * lane, or an aggregate above Gen 1x1's 5 Gbit/s.
 */
typedef struct _XHCI_SSHUB_LINK {
    ULONG LaneKbps;
    ULONG Lanes;
    ULONG Kbps;             /* aggregate                                  */
    ULONG Plus;
} XHCI_SSHUB_LINK, *PXHCI_SSHUB_LINK;

ULONG XhciSsHubDownstream(const struct _XHCI_PIPE_BOS *bos, ULONG extStatus,
                          PXHCI_SSHUB_LINK out);

/* ----------------------------------------------------------------------- */
/* Link rank and the SuperSpeed parent hub (xHCI Table 6-6)                 */
/* ----------------------------------------------------------------------- */

/*
 * An Enhanced SuperSpeed link's rank, in USB 3.2's order: "The port shall
 * rank its PHY capability in the order of Gen 2x2, Gen 2x1, Gen 1x2, and
 * Gen 1x1" (7.5.4.5, USB 3.2 p.176). Not the aggregate rate: Gen 2x1 and
 * Gen 1x2 both carry 10 Gbit/s and Gen 2x1 ranks higher. 0 is unknown.
 */
#define XHCI_SS_RANK_UNKNOWN    0UL
#define XHCI_SS_RANK_GEN1X1     1UL
#define XHCI_SS_RANK_GEN1X2     2UL
#define XHCI_SS_RANK_GEN2X1     3UL
#define XHCI_SS_RANK_GEN2X2     4UL

/* A lane above Gen 1's 5 Gbit/s is a Gen 2 lane. */
#define XHCI_SS_GEN1_LANE_KBPS  5000000UL

/*
 * The rank of a link of `lanes` lanes each at `laneKbps` kbit/s: Gen 1 for
 * a lane rate of at most 5 Gbit/s, Gen 2 above it, x1 or x2.
 * XHCI_SS_RANK_UNKNOWN for a rate of 0 or a lane count other than 1 or 2
 * (USB 3.2 defines no other).
 */
ULONG XhciSsLinkRank(ULONG laneKbps, ULONG lanes);

/*
 * A root port's link rank from its PSI rate - `aggregateKbps`, what
 * XhciPortRate gives, the whole link's rate (xHCI Table 7-13's PSIM column,
 * p.485) - and its PORTLI: the Rx Lane Count, bits 19:16, is zero-based,
 * "0 to 15 represents Lane Counts of 1 to 16" (Table 5-31, p.385), so the
 * lane rate is the aggregate over RLC + 1. An all-ones read (a window that
 * stopped decoding) is 16 lanes, which ranks UNKNOWN.
 */
ULONG XhciSsRootRank(ULONG aggregateKbps, ULONG portli);

/*
 * The rank of the link between a SuperSpeed hub's port and the device on
 * it. A hub with no SuperSpeedPlus capability (`hubSsp` 0) is a SuperSpeed
 * hub - every SuperSpeedPlus device "shall" carry that capability (USB 3.2
 * 9.6.2.5, p.357) - so its links are Gen 1x1. A SuperSpeedPlus hub's link
 * is the one its extended port status named (`link`, Kbps nonzero once
 * read, XhciSsHubDownstream); not read, it is UNKNOWN.
 */
ULONG XhciSsHubChildRank(ULONG hubSsp, const XHCI_SSHUB_LINK *link);

/*
 * Whether xHCI Table 6-6 (p.409-410) wants the Parent Hub Slot ID and
 * Parent Port Number of an SS/SSP device behind a hub: the device is
 * "connected through a higher rank hub" - "a Gen1 x1 connected behind a
 * Gen1 x2 hub, or Gen1 x2 device connected behind Gen2 x2 hub", footnote
 * 110's hub whose downstream port isolates the signalling between its
 * upstream and downstream ports. `hubRank` is the hub's own upstream link,
 * `childRank` the device's. 1 when both are known and the hub's ranks
 * higher, 0 otherwise - and then this hub is not the boundary, though one
 * further up may be (XhciSsHubParentOf). With either rank unknown the
 * answer is 0, a best-effort fallback rather than a rule the specification
 * gives: it invents no boundary, and is right on every path whose links
 * rank alike, which every Gen 1x1 hub's do, but it still leaves both fields
 * 0 where an unobserved boundary exists - an unresolved case, and the one
 * this driver had for every device before this rule.
 */
ULONG XhciSsParentNeeded(ULONG hubRank, ULONG childRank);

/*
 * What HcdHubPlace writes for a device placed on port `hubPort` of the hub
 * in slot `hubSlot`. Only a SuperSpeed hub (`hubUsb3`) is decided here:
 * *childRank is the device's link rank (XhciSsHubChildRank, from `hubSsp`
 * and `link`), and *parentSlot / *parentPort are:
 *
 *   - the hub's own slot and port, when XhciSsParentNeeded(hubRank,
 *     *childRank) - the hub is the boundary;
 *   - the hub's own parent pair (`hubParentSlot`, `hubParentPort`, what
 *     this rule gave the hub when it was placed), when the device's link
 *     ranks the same as the hub's: the boundary is further up and the
 *     device sits behind it as the hub does - root, a Gen 2x1 hub A, a
 *     Gen 1x1 hub B behind it (B's pair names A), and a Gen 1x1 device
 *     behind B, which crosses A's boundary too (footnote 110; Codex review
 *     of b6e569e, finding 1), as a Full-Speed device behind a Full-Speed hub
 *     keeps the High-Speed hub above both as its TT;
 *   - 0 otherwise, including any unknown rank.
 *
 * A USB 2.0 hub - including a USB 3 hub's USB 2.0 half, whose TT is
 * xhci_topo.c's (XhciTopoTtFor) - gets 0 in all three: the TT fields are
 * not this rule's. Returns *parentSlot != 0.
 */
ULONG XhciSsHubParentOf(ULONG hubUsb3, ULONG hubSsp, ULONG hubRank,
                        ULONG hubParentSlot, ULONG hubParentPort,
                        const XHCI_SSHUB_LINK *link, ULONG hubSlot,
                        ULONG hubPort, PULONG childRank, PULONG parentSlot,
                        PULONG parentPort);

/*
 * The Protocol Speed ID to write into the Slot Context of a device behind a
 * SuperSpeed hub on root port `rootPort`: for a Gen 1x1 link (link NULL or
 * not Plus) the SuperSpeed class's ID (XhciPortPsivForSpeed); for a
 * SuperSpeedPlus link the ID whose rate the root port's protocol names as
 * SuperSpeedPlus at the aggregate rate - and, where the protocol advertises
 * no PSI table, the default IDs: 5 for 10 Gbit/s on one lane, 6 on two, 7
 * for 20 Gbit/s (xHCI Table 7-13, p.485; a leniency kept as xhci_caps.c
 * keeps it: 7.2.2.1.2 defines 5 only on a USB 3.1 or 3.2 group and 6 and 7
 * only on a USB 3.2 group, and this does not narrow the choice by the
 * group's Minor Revision). Only those are matches. Failing them, the ID
 * named at the lane rate, else the SuperSpeed class's, is given with *matched 0: the
 * device is still addressed, the caller counts it, and the controller's
 * output Slot Context decides after Address Device (XhciSsHubAdoptSpeed).
 */
ULONG XhciSsHubPsiv(const struct _XHCI_PORT_MAP *map, ULONG rootPort,
                    const XHCI_SSHUB_LINK *link, PULONG psiv,
                    PULONG matched);

/*
 * The Protocol Speed ID a device behind a SuperSpeed hub keeps after its
 * Address Device: `output`, the speed the controller wrote into the output
 * Slot Context, when it differs from `given` (what the bus asked for) and
 * names a SuperSpeed-class rate on `rootPort`'s protocol - the controller
 * is authoritative where the bus guessed (an unmatched SuperSpeedPlus rate,
 * Codex review of 034a119, finding 2) - as xHCI 4.19.9 (p.304-305) has it:
 * a SuperSpeedPlus device sends a Sublink Speed Device Notification after
 * SET_ADDRESS, and the xHC writes the speed it reports into the output Slot
 * Context, ignoring the input's; otherwise `given`, including for an
 * output of 0 or a speed the protocol does not name.
 */
ULONG XhciSsHubAdoptSpeed(const struct _XHCI_PORT_MAP *map, ULONG rootPort,
                          ULONG given, ULONG output);

/*
 * Whether a SuperSpeed hub and a USB 2.0 hub look like the two halves of
 * one unit - counted, never acted on: the same idVendor, at the same tier
 * and Route String, on root ports the controller pairs as companions.
 * `companionOfSs` is the port map's companion of the SuperSpeed half's
 * root port (0 for none). The pairing is a convention, so two units of
 * one vendor on a mis-paired connector can read as a pair; nothing rests
 * on it.
 */
ULONG XhciSsHubLooksPaired(ULONG ssVendor, ULONG ssRoot, ULONG ssTier,
                           ULONG ssRoute, ULONG hsVendor, ULONG hsRoot,
                           ULONG hsTier, ULONG hsRoute, ULONG companionOfSs);

#endif /* XHCI_SSHUB_H */
