/*
 * test_sshub.c - host vectors for the SuperSpeed hub class's pure half
 * (src\xhci_sshub.c; roadmap-hcd.md tasks 30-A.1 and 30-A.2;
 * xhci-data-structures.md section 11).
 *
 * No QEMU device models a SuperSpeed hub, so until the bench session reads
 * 30-E.1 these vectors are the only evidence the SuperSpeed hub path has.
 * They are tables where the decision is a function of (state, wPortStatus,
 * wPortChange), so each row is one port reading and what the bus must do
 * with it: the descriptor and its refusals, the link-state field, the port
 * decision over every change bit SuperSpeed has and the two USB 2.0 has
 * that SuperSpeed does not, the clear selectors, the reset kind over every
 * link state with and without a connection, the reset's progress and what a
 * finished one leaves to clear, the extended port status and the hub's
 * sublink attributes, the Protocol Speed ID a downstream SuperSpeedPlus
 * link is given with and without a PSI table, and the pairing counter's
 * rule. The values are xhci_sshub.h's, which are section 11's, which are to
 * verify against the USB 3.2 specification: a row here proves the code
 * does what the transcription says, not that the transcription is right.
 */

#include <stdio.h>
#include "../src/xhci.h"
#include "../src/xhci_enum.h"
#include "../src/xhci_hub.h"
#include "../src/xhci_pipe.h"
#include "../src/xhci_sshub.h"
#include "test_harness.h"

#define LINK(ls)    ((ULONG)(ls) << XHCI_SSHUB_PORT_LINK_SHIFT)
#define CONN        XHCI_SSHUB_PORT_CONNECTION
#define ENA         XHCI_SSHUB_PORT_ENABLE
#define PWR         XHCI_SSHUB_PORT_POWER
#define RST         XHCI_SSHUB_PORT_RESET
/* A trained, enabled port in U0: the steady state of a device. */
#define UP          (PWR | CONN | ENA | LINK(XHCI_SSHUB_LINK_U0))

/* ----------------------------------------------------------------------- */

static void test_descriptor(void)
{
    /* A 4-port SuperSpeed half: per-port power, bPwrOn2PwrGood 50 (100 ms),
     * bHubContrCurrent 0, bHubHdrDecLat 4, wHubDelay 0x0190 (400 ns),
     * DeviceRemovable 0. */
    UCHAR good[12] = { 12, 0x2A, 4, 0x09, 0x00, 50, 0, 4, 0x90, 0x01,
                       0x00, 0x00 };
    UCHAR bad[12];
    XHCI_SSHUB_DESC d;
    ULONG i;

    CHECK_EQ(XhciSsHubParseDescriptor(good, 12, &d), XHCI_SSHUB_OK, "parsed");
    CHECK_EQ(d.Ports, 4, "bNbrPorts");
    CHECK_EQ(d.Managed, 4, "all managed");
    CHECK_EQ(d.Characteristics, 0x0009, "wHubCharacteristics");
    CHECK_EQ(d.PowerGoodMs, 100, "bPwrOn2PwrGood x 2");
    CHECK_EQ(d.HeaderDecodeLatency, 4, "bHubHdrDecLat");
    CHECK_EQ(d.HubDelayNs, 400, "wHubDelay little-endian");
    CHECK_EQ(d.Removable, 0, "DeviceRemovable");

    for (i = 0; i < 12; i++) {
        bad[i] = good[i];
    }
    bad[10] = 0x06;
    bad[11] = 0x80;
    CHECK_EQ(XhciSsHubParseDescriptor(bad, 12, &d), XHCI_SSHUB_OK,
             "removable map");
    CHECK_EQ(d.Removable, 0x8006, "DeviceRemovable 16 bits");

    bad[2] = 15;
    CHECK_EQ(XhciSsHubParseDescriptor(bad, 12, &d), XHCI_SSHUB_OK,
             "15 ports: the SuperSpeed maximum");
    CHECK_EQ(d.Ports, 15, "declared 15");
    CHECK_EQ(d.Managed, XHCI_HUB_MAX_PORTS, "managed capped at 14");
    bad[2] = 16;
    CHECK_EQ(XhciSsHubParseDescriptor(bad, 12, &d), XHCI_SSHUB_MALFORMED,
             "16 ports: no route nibble at SuperSpeed");
    CHECK_EQ(d.Ports, 0, "output cleared on refusal");
    bad[2] = 0;
    CHECK_EQ(XhciSsHubParseDescriptor(bad, 12, &d), XHCI_SSHUB_MALFORMED,
             "no ports");
    bad[2] = 4;
    bad[1] = 0x29;
    CHECK_EQ(XhciSsHubParseDescriptor(bad, 12, &d), XHCI_SSHUB_MALFORMED,
             "a USB 2.0 hub descriptor is not this one");
    bad[1] = 0x2A;
    bad[0] = 9;
    CHECK_EQ(XhciSsHubParseDescriptor(bad, 12, &d), XHCI_SSHUB_MALFORMED,
             "bLength below 12");
    bad[0] = 13;
    CHECK_EQ(XhciSsHubParseDescriptor(bad, 12, &d), XHCI_SSHUB_MALFORMED,
             "bLength past what arrived");
    CHECK_EQ(XhciSsHubParseDescriptor(good, 11, &d), XHCI_SSHUB_MALFORMED,
             "short reply");
    CHECK_EQ(XhciSsHubParseDescriptor(good, 9, &d), XHCI_SSHUB_MALFORMED,
             "the USB 2.0 length");
    CHECK_EQ(XhciSsHubParseDescriptor(NULL, 12, &d), XHCI_SSHUB_BAD_PARAM,
             "NULL data");
    CHECK_EQ(XhciSsHubParseDescriptor(good, 12, NULL), XHCI_SSHUB_BAD_PARAM,
             "NULL out");
    /* The status-change report a 4-port and a 15-port hub send. */
    CHECK_EQ(XhciHubStatusBytes(4), 1, "4 ports: one byte");
    CHECK_EQ(XhciHubStatusBytes(15), 2, "15 ports: two bytes");
}

static void test_link_field(void)
{
    ULONG ls;

    for (ls = 0; ls <= 0xF; ls++) {
        CHECK_EQ(XhciSsHubLinkState(LINK(ls) | CONN | PWR | ENA), ls,
                 "PORT_LINK_STATE 8:5 round trip");
    }
    CHECK_EQ(LINK(XHCI_SSHUB_LINK_RX_DETECT), 0x00A0, "Rx.Detect 0x00A0");
    CHECK_EQ(LINK(XHCI_SSHUB_LINK_INACTIVE), 0x00C0, "SS.Inactive 0x00C0");
    CHECK_EQ(LINK(XHCI_SSHUB_LINK_COMPLIANCE), 0x0140, "Compliance 0x0140");
    CHECK_EQ(XhciSsHubLinkState(XHCI_SSHUB_PORT_SPEED_MASK | PWR), 0,
             "the speed and power bits are not the link field");
    CHECK_EQ(XHCI_SSHUB_PORT_POWER, 0x0200, "power is bit 9 at SuperSpeed");
}

/* ----------------------------------------------------------------------- */
/* The port decision                                                        */
/* ----------------------------------------------------------------------- */

typedef struct _DECIDE_ROW {
    const char *what;
    ULONG state;
    ULONG status;
    ULONG change;
    ULONG clear;
    ULONG disconnect;
    ULONG connect;
    ULONG overCurrent;
    ULONG repower;
    ULONG warm;
    ULONG configError;
    ULONG linkChange;
} DECIDE_ROW;

#define E XHCI_ENUM_EMPTY
#define B XHCI_ENUM_BOUND
#define G XHCI_ENUM_GONE
#define F XHCI_ENUM_FAILED
#define C_CONN  XHCI_SSHUB_C_PORT_CONNECTION
#define C_OC    XHCI_SSHUB_C_PORT_OVER_CURRENT
#define C_RST   XHCI_SSHUB_C_PORT_RESET
#define C_BH    XHCI_SSHUB_C_BH_PORT_RESET
#define C_LS    XHCI_SSHUB_C_PORT_LINK_STATE
#define C_CFG   XHCI_SSHUB_C_PORT_CONFIG_ERROR

static const DECIDE_ROW decideRows[] = {
    /* what                                   state status change
     *                                        clear  dis con oc rp wr cfg ls */
    { "connect change on an empty port", E, UP, C_CONN,
      C_CONN, 0, 1, 0, 0, 0, 0, 0 },
    { "present at power-on, no change", E, UP, 0,
      0, 0, 1, 0, 0, 0, 0, 0 },
    { "connected, link still training", E,
      PWR | CONN | LINK(XHCI_SSHUB_LINK_POLLING), 0,
      0, 0, 1, 0, 0, 0, 0, 0 },
    { "unplug", B, PWR | LINK(XHCI_SSHUB_LINK_RX_DETECT), C_CONN,
      C_CONN, 1, 0, 0, 0, 0, 0, 0 },
    { "unplug with a link change beside it", B,
      PWR | LINK(XHCI_SSHUB_LINK_RX_DETECT), C_CONN | C_LS,
      C_CONN | C_LS, 1, 0, 0, 0, 0, 0, 1 },
    { "swap between looks", B, UP, C_CONN,
      C_CONN, 1, 1, 0, 0, 0, 0, 0 },
    { "nothing changed", B, UP, 0,
      0, 0, 0, 0, 0, 0, 0, 0 },
    { "suspended in U3, nothing changed (resumed: test_resume)", B,
      PWR | CONN | ENA | LINK(XHCI_SSHUB_LINK_U3), 0,
      0, 0, 0, 0, 0, 0, 0, 0 },
    { "U3 to U0 completed", B, UP, C_LS,
      C_LS, 0, 0, 0, 0, 0, 0, 1 },
    { "held device, port no longer enabled", B,
      PWR | CONN | LINK(XHCI_SSHUB_LINK_U0), 0,
      0, 1, 1, 0, 0, 0, 0, 0 },
    { "no connect change, disconnected", B,
      PWR | LINK(XHCI_SSHUB_LINK_RX_DETECT), 0,
      0, 1, 0, 0, 0, 0, 0, 0 },
    { "a failed port that empties", F,
      PWR | LINK(XHCI_SSHUB_LINK_RX_DETECT), 0,
      0, 1, 0, 0, 0, 0, 0, 0 },
    { "empty and stays empty", E,
      PWR | LINK(XHCI_SSHUB_LINK_RX_DETECT), 0,
      0, 0, 0, 0, 0, 0, 0, 0 },
    { "an empty Gone port asks nothing", G,
      PWR | LINK(XHCI_SSHUB_LINK_RX_DETECT), 0,
      0, 0, 0, 0, 0, 0, 0, 0 },
    { "SS.Inactive under a device", B,
      PWR | CONN | LINK(XHCI_SSHUB_LINK_INACTIVE), C_LS,
      C_LS, 1, 0, 0, 0, 1, 0, 1 },
    { "SS.Inactive on an empty port, no connection", E,
      PWR | LINK(XHCI_SSHUB_LINK_INACTIVE), 0,
      0, 0, 0, 0, 0, 1, 0, 0 },
    { "SS.Inactive with a connect change", E,
      PWR | CONN | LINK(XHCI_SSHUB_LINK_INACTIVE), C_CONN,
      C_CONN, 0, 0, 0, 0, 1, 0, 0 },
    { "Compliance Mode under a device", B,
      PWR | CONN | LINK(XHCI_SSHUB_LINK_COMPLIANCE), 0,
      0, 1, 0, 0, 0, 1, 0, 0 },
    { "Compliance Mode on an empty port", E,
      PWR | LINK(XHCI_SSHUB_LINK_COMPLIANCE), 0,
      0, 0, 0, 0, 0, 1, 0, 0 },
    { "config error under a device", B, UP, C_CFG,
      C_CFG, 1, 0, 0, 0, 0, 1, 0 },
    { "config error with the connect change", E, UP, C_CONN | C_CFG,
      C_CONN | C_CFG, 0, 0, 0, 0, 0, 1, 0 },
    { "config error on an empty port", E, UP, C_CFG,
      C_CFG, 0, 0, 0, 0, 0, 1, 0 },
    { "over-current, power lost, device gone", B,
      LINK(XHCI_SSHUB_LINK_DISABLED), C_OC,
      C_OC, 1, 0, 1, 1, 0, 0, 0 },
    { "over-current reported, power kept", B, UP, C_OC,
      C_OC, 0, 0, 1, 0, 0, 0, 0 },
    { "over-current on an empty unpowered port", E, 0, C_OC,
      C_OC, 0, 0, 1, 1, 0, 0, 0 },
    { "stray C_PORT_RESET", B, UP, C_RST,
      C_RST, 0, 0, 0, 0, 0, 0, 0 },
    { "stray C_BH_PORT_RESET", B, UP, C_BH,
      C_BH, 0, 0, 0, 0, 0, 0, 0 },
    { "USB 2.0's C_PORT_ENABLE and C_PORT_SUSPEND are reserved here", B,
      UP, 0x0006, 0, 0, 0, 0, 0, 0, 0, 0 },
    { "bits above C_PORT_CONFIG_ERROR are not cleared", B, UP, 0xFF00,
      0, 0, 0, 0, 0, 0, 0, 0 },
    { "every change bit at once on a held port", B, UP, 0x00FF,
      C_CONN | C_OC | C_RST | C_BH | C_LS | C_CFG,
      1, 0, 1, 0, 0, 1, 1 },
};

static void test_decide(void)
{
    XHCI_SSHUB_PORT_DECISION d;
    ULONG i;

    for (i = 0; i < sizeof(decideRows) / sizeof(decideRows[0]); i++) {
        const DECIDE_ROW *r = &decideRows[i];

        XhciSsHubPortDecide(r->state, r->status, r->change, &d);
        check_eq_impl(d.Clear, r->clear, r->what, __FILE__, __LINE__);
        check_eq_impl(d.Disconnect, r->disconnect, r->what, __FILE__,
                      __LINE__);
        check_eq_impl(d.Connect, r->connect, r->what, __FILE__, __LINE__);
        check_eq_impl(d.OverCurrent, r->overCurrent, r->what, __FILE__,
                      __LINE__);
        check_eq_impl(d.Repower, r->repower, r->what, __FILE__, __LINE__);
        check_eq_impl(d.WarmReset, r->warm, r->what, __FILE__, __LINE__);
        check_eq_impl(d.ConfigError, r->configError, r->what, __FILE__,
                      __LINE__);
        check_eq_impl(d.LinkChange, r->linkChange, r->what, __FILE__,
                      __LINE__);
    }
    XhciSsHubPortDecide(E, UP, 0, NULL);
    CHECK(1, "a NULL decision is answered, not dereferenced");
}

static void test_selectors(void)
{
    CHECK_EQ(XhciSsHubClearSelector(C_CONN), 16, "C_PORT_CONNECTION");
    CHECK_EQ(XhciSsHubClearSelector(C_OC), 19, "C_PORT_OVER_CURRENT");
    CHECK_EQ(XhciSsHubClearSelector(C_RST), 20, "C_PORT_RESET");
    CHECK_EQ(XhciSsHubClearSelector(C_BH), 29, "C_BH_PORT_RESET");
    CHECK_EQ(XhciSsHubClearSelector(C_LS), 25, "C_PORT_LINK_STATE");
    CHECK_EQ(XhciSsHubClearSelector(C_CFG), 26, "C_PORT_CONFIG_ERROR");
    CHECK_EQ(XhciSsHubClearSelector(0x0002), 0, "no C_PORT_ENABLE");
    CHECK_EQ(XhciSsHubClearSelector(0x0004), 0, "no C_PORT_SUSPEND");
    CHECK_EQ(XhciSsHubClearSelector(0x0100), 0, "not a change bit");
    CHECK_EQ(XHCI_SSHUB_FEAT_BH_PORT_RESET, 28, "BH_PORT_RESET 28");
    CHECK_EQ(XHCI_SSHUB_FEAT_PORT_LINK_STATE, 5, "PORT_LINK_STATE 5");
    CHECK_EQ(XHCI_SSHUB_FEAT_PORT_REMOTE_WAKE_MASK, 27,
             "PORT_REMOTE_WAKE_MASK 27");
    CHECK_EQ(XHCI_SSHUB_REQ_SET_HUB_DEPTH, 12, "SET_HUB_DEPTH 12");
    CHECK_EQ(XHCI_SSHUB_DESC_VALUE, 0x2A00, "GET_DESCRIPTOR(SS hub)");
}

/* ----------------------------------------------------------------------- */
/* Reset                                                                    */
/* ----------------------------------------------------------------------- */

typedef struct _KIND_ROW {
    const char *what;
    ULONG status;
    ULONG kind;
} KIND_ROW;

static const KIND_ROW kindRows[] = {
    { "U0, connected: hot", PWR | CONN | ENA | LINK(XHCI_SSHUB_LINK_U0),
      XHCI_SSHUB_RESET_HOT },
    { "U0 not yet enabled: hot", PWR | CONN | LINK(XHCI_SSHUB_LINK_U0),
      XHCI_SSHUB_RESET_HOT },
    { "U1: hot", PWR | CONN | ENA | LINK(XHCI_SSHUB_LINK_U1),
      XHCI_SSHUB_RESET_HOT },
    { "U2: hot", PWR | CONN | ENA | LINK(XHCI_SSHUB_LINK_U2),
      XHCI_SSHUB_RESET_HOT },
    { "Recovery: hot", PWR | CONN | ENA | LINK(XHCI_SSHUB_LINK_RECOVERY),
      XHCI_SSHUB_RESET_HOT },
    { "U3: warm", PWR | CONN | ENA | LINK(XHCI_SSHUB_LINK_U3),
      XHCI_SSHUB_RESET_WARM },
    { "SS.Inactive, connected: warm",
      PWR | CONN | LINK(XHCI_SSHUB_LINK_INACTIVE), XHCI_SSHUB_RESET_WARM },
    { "SS.Inactive, no connection: warm",
      PWR | LINK(XHCI_SSHUB_LINK_INACTIVE), XHCI_SSHUB_RESET_WARM },
    { "Compliance: warm", PWR | LINK(XHCI_SSHUB_LINK_COMPLIANCE),
      XHCI_SSHUB_RESET_WARM },
    { "Polling stuck with a connection: warm",
      PWR | CONN | LINK(XHCI_SSHUB_LINK_POLLING), XHCI_SSHUB_RESET_WARM },
    { "Hot Reset stuck: warm", PWR | CONN | LINK(XHCI_SSHUB_LINK_HOT_RESET),
      XHCI_SSHUB_RESET_WARM },
    { "Loopback: warm", PWR | CONN | LINK(XHCI_SSHUB_LINK_LOOPBACK),
      XHCI_SSHUB_RESET_WARM },
    { "Rx.Detect with a connection: warm",
      PWR | CONN | LINK(XHCI_SSHUB_LINK_RX_DETECT), XHCI_SSHUB_RESET_WARM },
    { "SS.Disabled with a connection: warm",
      PWR | CONN | LINK(XHCI_SSHUB_LINK_DISABLED), XHCI_SSHUB_RESET_WARM },
    { "Rx.Detect, nothing there: none",
      PWR | LINK(XHCI_SSHUB_LINK_RX_DETECT), XHCI_SSHUB_RESET_NONE },
    { "SS.Disabled, nothing there: none",
      PWR | LINK(XHCI_SSHUB_LINK_DISABLED), XHCI_SSHUB_RESET_NONE },
    { "Polling, no connection yet: none",
      PWR | LINK(XHCI_SSHUB_LINK_POLLING), XHCI_SSHUB_RESET_NONE },
    { "unpowered: none", 0, XHCI_SSHUB_RESET_NONE },
};

static void test_reset_kind(void)
{
    ULONG converted;
    ULONG i;

    for (i = 0; i < sizeof(kindRows) / sizeof(kindRows[0]); i++) {
        converted = 0xFF;
        check_eq_impl(XhciSsHubResetKind(kindRows[i].status, &converted),
                      kindRows[i].kind, kindRows[i].what, __FILE__,
                      __LINE__);
        check_eq_impl(converted,
                      kindRows[i].kind == XHCI_SSHUB_RESET_WARM ? 1UL : 0UL,
                      kindRows[i].what, __FILE__, __LINE__);
    }
    CHECK_EQ(XhciSsHubResetKind(UP, NULL), XHCI_SSHUB_RESET_HOT,
             "a NULL converted is not dereferenced");
}

typedef struct _PROGRESS_ROW {
    const char *what;
    ULONG status;
    ULONG change;
    ULONG progress;
    ULONG warmSeen;
} PROGRESS_ROW;

static const PROGRESS_ROW progressRows[] = {
    { "hot reset still driven", PWR | CONN | RST |
          LINK(XHCI_SSHUB_LINK_HOT_RESET), 0, XHCI_HUB_RESET_PENDING, 0 },
    { "warm reset, connection dropped while it runs", PWR | RST |
          LINK(XHCI_SSHUB_LINK_POLLING), 0, XHCI_HUB_RESET_PENDING, 0 },
    { "hot reset done, U0 enabled", UP, C_RST, XHCI_HUB_RESET_ENABLED, 0 },
    { "warm reset done, U0 enabled", UP, C_BH, XHCI_HUB_RESET_ENABLED, 1 },
    { "hot asked, the hub ran warm", UP, C_RST | C_BH,
      XHCI_HUB_RESET_ENABLED, 1 },
    { "done before the change, U0 enabled", UP, 0,
      XHCI_HUB_RESET_ENABLED, 0 },
    { "done, not enabled", PWR | CONN | LINK(XHCI_SSHUB_LINK_U0), C_RST,
      XHCI_HUB_RESET_FAILED, 0 },
    { "done, link fell to SS.Inactive", PWR | CONN | ENA |
          LINK(XHCI_SSHUB_LINK_INACTIVE), C_BH, XHCI_HUB_RESET_FAILED, 1 },
    { "done, enabled but in Recovery", PWR | CONN | ENA |
          LINK(XHCI_SSHUB_LINK_RECOVERY), C_RST, XHCI_HUB_RESET_FAILED, 0 },
    { "not started yet", PWR | CONN | LINK(XHCI_SSHUB_LINK_U0), 0,
      XHCI_HUB_RESET_PENDING, 0 },
    { "device left", PWR | LINK(XHCI_SSHUB_LINK_RX_DETECT), C_RST,
      XHCI_HUB_RESET_FAILED, 0 },
    { "device left, no change", PWR | LINK(XHCI_SSHUB_LINK_RX_DETECT), 0,
      XHCI_HUB_RESET_FAILED, 0 },
};

static void test_reset_progress(void)
{
    ULONG warm;
    ULONG i;

    for (i = 0; i < sizeof(progressRows) / sizeof(progressRows[0]); i++) {
        warm = 0xFF;
        check_eq_impl(XhciSsHubResetProgress(progressRows[i].status,
                                             progressRows[i].change, &warm),
                      progressRows[i].progress, progressRows[i].what,
                      __FILE__, __LINE__);
        check_eq_impl(warm, progressRows[i].warmSeen, progressRows[i].what,
                      __FILE__, __LINE__);
    }
    CHECK_EQ(XhciSsHubResetProgress(UP, C_RST, NULL), XHCI_HUB_RESET_ENABLED,
             "a NULL warmSeen is not dereferenced");

    CHECK_EQ(XhciSsHubResetClears(C_RST | C_CONN | C_LS, 0), C_RST,
             "a hot reset clears its own change only");
    CHECK_EQ(XhciSsHubResetClears(C_BH | C_LS | C_CONN | C_OC, 1),
             C_BH | C_LS | C_CONN,
             "a warm reset's retrain is no new device; over-current stays");
    CHECK_EQ(XhciSsHubResetClears(C_RST | C_BH | C_CONN, 0),
             C_RST | C_BH | C_CONN, "a hot reset the hub ran warm");
    CHECK_EQ(XhciSsHubResetClears(C_BH, 1), C_BH, "only what is set");
    CHECK_EQ(XhciSsHubResetClears(0, 1), 0, "nothing set, nothing cleared");
    CHECK_EQ(XhciSsHubResetClears(C_CFG | C_RST, 1), C_RST,
             "a config error is the look's to see");
}

/*
 * Suspend handled, never initiated, at a SuperSpeed hub port (the Phase 27
 * and Phase 30 merge): a held device on a connected, enabled port whose link
 * reads U3 is resumed (Resume), and a C_PORT_LINK_STATE with the link back
 * in U0 under a held device is a finished U3 exit (Resumed); every other
 * rule comes first. The resume's progress over every link state.
 */
typedef struct _RESUME_ROW {
    const char *what;
    ULONG state;
    ULONG status;
    ULONG change;
    ULONG resume;
    ULONG resumed;
    ULONG disconnect;
    ULONG connect;
} RESUME_ROW;

static const RESUME_ROW resumeRows[] = {
    /* what                                  state status change
     *                                       resume resumed dis con */
    { "held in U3: resumed", B, PWR | CONN | ENA | LINK(XHCI_SSHUB_LINK_U3),
      0, 1, 0, 0, 0 },
    { "held in U3 with a stray link change", B,
      PWR | CONN | ENA | LINK(XHCI_SSHUB_LINK_U3), C_LS, 1, 0, 0, 0 },
    { "empty port in U3: a new device, no resume", E,
      PWR | CONN | ENA | LINK(XHCI_SSHUB_LINK_U3), 0, 0, 0, 0, 1 },
    { "failed port in U3: no resume", F,
      PWR | CONN | ENA | LINK(XHCI_SSHUB_LINK_U3), 0, 0, 0, 0, 0 },
    { "held in U3, not enabled: afresh", B,
      PWR | CONN | LINK(XHCI_SSHUB_LINK_U3), 0, 0, 0, 1, 1 },
    { "held in U3 with a connect change: a new device", B,
      PWR | CONN | ENA | LINK(XHCI_SSHUB_LINK_U3), C_CONN, 0, 0, 1, 1 },
    { "U3 exit finished under a held device", B, UP, C_LS, 0, 1, 0, 0 },
    { "U0 with no link change: nothing", B, UP, 0, 0, 0, 0, 0 },
    { "link change into U1: not a U3 exit", B,
      PWR | CONN | ENA | LINK(XHCI_SSHUB_LINK_U1), C_LS, 0, 0, 0, 0 },
    { "link change on an empty port", E, UP, C_LS, 0, 0, 0, 1 },
    { "SS.Inactive from U3 under a device: the warm reset's", B,
      PWR | CONN | ENA | LINK(XHCI_SSHUB_LINK_INACTIVE), C_LS, 0, 0, 1, 0 },
};

typedef struct _RPROGRESS_ROW {
    const char *what;
    ULONG status;
    ULONG progress;
} RPROGRESS_ROW;

static const RPROGRESS_ROW rprogressRows[] = {
    { "still in U3", PWR | CONN | ENA | LINK(XHCI_SSHUB_LINK_U3),
      XHCI_HUB_RESUME_PENDING },
    { "reserved 0xF is no Resume state on a hub port",
      PWR | CONN | ENA | LINK(0xF), XHCI_HUB_RESUME_DISABLED },
    { "reserved 0xC", PWR | CONN | ENA | LINK(0xC),
      XHCI_HUB_RESUME_DISABLED },
    { "in Recovery", PWR | CONN | ENA | LINK(XHCI_SSHUB_LINK_RECOVERY),
      XHCI_HUB_RESUME_PENDING },
    { "back in U0", UP, XHCI_HUB_RESUME_DONE },
    { "U1 already", PWR | CONN | ENA | LINK(XHCI_SSHUB_LINK_U1),
      XHCI_HUB_RESUME_DONE },
    { "U2 already", PWR | CONN | ENA | LINK(XHCI_SSHUB_LINK_U2),
      XHCI_HUB_RESUME_DONE },
    { "U0 but not enabled", PWR | CONN | LINK(XHCI_SSHUB_LINK_U0),
      XHCI_HUB_RESUME_DISABLED },
    { "fell to SS.Inactive", PWR | CONN | ENA |
          LINK(XHCI_SSHUB_LINK_INACTIVE), XHCI_HUB_RESUME_DISABLED },
    { "fell to SS.Disabled", PWR | CONN | LINK(XHCI_SSHUB_LINK_DISABLED),
      XHCI_HUB_RESUME_DISABLED },
    { "Compliance Mode", PWR | CONN | LINK(XHCI_SSHUB_LINK_COMPLIANCE),
      XHCI_HUB_RESUME_DISABLED },
    { "device left", PWR | LINK(XHCI_SSHUB_LINK_RX_DETECT),
      XHCI_HUB_RESUME_GONE },
    { "device left mid-resume", PWR | LINK(XHCI_SSHUB_LINK_U3),
      XHCI_HUB_RESUME_GONE },
};

static void test_resume(void)
{
    XHCI_SSHUB_PORT_DECISION d;
    ULONG i;

    for (i = 0; i < sizeof(resumeRows) / sizeof(resumeRows[0]); i++) {
        const RESUME_ROW *r = &resumeRows[i];

        XhciSsHubPortDecide(r->state, r->status, r->change, &d);
        check_eq_impl(d.Resume, r->resume, r->what, __FILE__, __LINE__);
        check_eq_impl(d.Resumed, r->resumed, r->what, __FILE__, __LINE__);
        check_eq_impl(d.Disconnect, r->disconnect, r->what, __FILE__,
                      __LINE__);
        check_eq_impl(d.Connect, r->connect, r->what, __FILE__, __LINE__);
    }
    for (i = 0; i < sizeof(rprogressRows) / sizeof(rprogressRows[0]); i++) {
        check_eq_impl(XhciSsHubResumeProgress(rprogressRows[i].status),
                      rprogressRows[i].progress, rprogressRows[i].what,
                      __FILE__, __LINE__);
    }
}

/* ----------------------------------------------------------------------- */
/* SuperSpeedPlus                                                           */
/* ----------------------------------------------------------------------- */

static ULONG ssa(ULONG ssid, ULONG lse, ULONG st, ULONG lp, ULONG lsm)
{
    return ssid | (lse << 4) | (st << 6) | (lp << 14) | (lsm << 16);
}

static void bosZero(PXHCI_PIPE_BOS b)
{
    PUCHAR p;
    ULONG i;

    p = (PUCHAR)b;
    for (i = 0; i < sizeof(*b); i++) {
        p[i] = 0;
    }
}

/* A Gen 2 hub's capability: SSID 0 Gen 1 (5 Gb/s, LP SuperSpeed) Rx and
 * Tx, SSID 1 Gen 2 (10 Gb/s, LP SuperSpeedPlus) Rx and Tx, SSID 2 an
 * Rx-only 10000 Mb/s by LSE 2, SSID 3 a rate under 1 kb/s, SSID 4 a rate
 * past a ULONG of kb/s, SSID 5 a Tx attribute alone. */
static void bosGen2Hub(PXHCI_PIPE_BOS b)
{
    bosZero(b);
    b->HasSuperSpeedPlus = 1;
    b->SspSublinks = 8;
    b->SspSublink[0] = ssa(0, 3, 0, 0, 5);
    b->SspSublink[1] = ssa(0, 3, 2, 0, 5);
    b->SspSublink[2] = ssa(1, 3, 0, 1, 10);
    b->SspSublink[3] = ssa(1, 3, 2, 1, 10);
    b->SspSublink[4] = ssa(2, 2, 1, 1, 10000);
    b->SspSublink[5] = ssa(3, 0, 0, 1, 500);
    b->SspSublink[6] = ssa(4, 3, 0, 1, 5000);
    b->SspSublink[7] = ssa(5, 3, 2, 1, 10);
}

static void test_ext_status(void)
{
    XHCI_PIPE_BOS bos;
    XHCI_SSHUB_LINK l;
    ULONG kbps;
    ULONG plus;

    /* dwExtPortStatus fields. */
    CHECK_EQ(XHCI_SSHUB_EXT_RX_SSID(0x00001021UL), 1, "Rx SSID 3:0");
    CHECK_EQ(XHCI_SSHUB_EXT_TX_SSID(0x00001021UL), 2, "Tx SSID 7:4");
    CHECK_EQ(XHCI_SSHUB_EXT_RX_LANES(0x00001021UL), 1, "Rx lanes 0 = one");
    CHECK_EQ(XHCI_SSHUB_EXT_TX_LANES(0x00001021UL), 2, "Tx lanes 1 = two");

    bosGen2Hub(&bos);
    CHECK_EQ(XhciSsHubHasExtStatus(0x0310, &bos), 1, "3.1 hub with SSP");
    CHECK_EQ(XhciSsHubHasExtStatus(0x0320, &bos), 1, "3.2 hub with SSP");
    CHECK_EQ(XhciSsHubHasExtStatus(0x0300, &bos), 0, "3.0 hub: no ext");
    CHECK_EQ(XhciSsHubHasExtStatus(0x0310, NULL), 0, "no BOS read");
    bos.HasSuperSpeedPlus = 0;
    CHECK_EQ(XhciSsHubHasExtStatus(0x0310, &bos), 0, "no SSP capability");

    bosGen2Hub(&bos);
    CHECK_EQ(XhciSsHubSublinkRate(&bos, 0, &kbps, &plus), XHCI_SSHUB_OK,
             "SSID 0");
    CHECK_EQ(kbps, 5000000UL, "Gen 1: 5 Gb/s");
    CHECK_EQ(plus, 0, "LP SuperSpeed");
    CHECK_EQ(XhciSsHubSublinkRate(&bos, 1, &kbps, &plus), XHCI_SSHUB_OK,
             "SSID 1");
    CHECK_EQ(kbps, 10000000UL, "Gen 2: 10 Gb/s");
    CHECK_EQ(plus, 1, "LP SuperSpeedPlus");
    CHECK_EQ(XhciSsHubSublinkRate(&bos, 2, &kbps, &plus), XHCI_SSHUB_OK,
             "SSID 2, an asymmetric Rx attribute");
    CHECK_EQ(kbps, 10000000UL, "10000 Mb/s by LSE 2");
    CHECK_EQ(XhciSsHubSublinkRate(&bos, 3, &kbps, &plus),
             XHCI_SSHUB_NOT_FOUND, "a rate under 1 kb/s names nothing");
    CHECK_EQ(XhciSsHubSublinkRate(&bos, 4, &kbps, &plus),
             XHCI_SSHUB_NOT_FOUND, "a rate past a ULONG of kb/s");
    CHECK_EQ(XhciSsHubSublinkRate(&bos, 5, &kbps, &plus),
             XHCI_SSHUB_NOT_FOUND, "a Tx attribute alone is not the Rx rate");
    CHECK_EQ(XhciSsHubSublinkRate(&bos, 9, &kbps, &plus),
             XHCI_SSHUB_NOT_FOUND, "an SSID the hub does not list");
    CHECK_EQ(kbps, 0, "output cleared on refusal");
    bos.SspSublinks = 2;
    CHECK_EQ(XhciSsHubSublinkRate(&bos, 1, &kbps, &plus),
             XHCI_SSHUB_NOT_FOUND, "only the attributes kept are read");
    bos.SspSublinks = 200;
    CHECK_EQ(XhciSsHubSublinkRate(&bos, 1, &kbps, &plus), XHCI_SSHUB_OK,
             "a count past the array is bounded by it");
    CHECK_EQ(XhciSsHubSublinkRate(NULL, 1, &kbps, &plus),
             XHCI_SSHUB_NOT_FOUND, "no BOS");
    CHECK_EQ(XhciSsHubSublinkRate(&bos, 1, NULL, &plus),
             XHCI_SSHUB_BAD_PARAM, "NULL rate");

    bosGen2Hub(&bos);
    CHECK_EQ(XhciSsHubDownstream(&bos, 0x00000011UL, &l), XHCI_SSHUB_OK,
             "Gen 2x1");
    CHECK(l.LaneKbps == 10000000UL && l.Lanes == 1 && l.Kbps == 10000000UL
              && l.Plus, "Gen 2x1: 10 Gb/s, one lane, plus");
    CHECK_EQ(XhciSsHubDownstream(&bos, 0x00001100UL, &l), XHCI_SSHUB_OK,
             "Gen 1x2");
    CHECK(l.LaneKbps == 5000000UL && l.Lanes == 2 && l.Kbps == 10000000UL
              && l.Plus, "Gen 1x2: two Gen 1 lanes are SuperSpeedPlus");
    CHECK_EQ(XhciSsHubDownstream(&bos, 0x00001111UL, &l), XHCI_SSHUB_OK,
             "Gen 2x2");
    CHECK(l.Kbps == 20000000UL && l.Lanes == 2 && l.Plus,
          "Gen 2x2: 20 Gb/s");
    CHECK_EQ(XhciSsHubDownstream(&bos, 0x00000000UL, &l), XHCI_SSHUB_OK,
             "Gen 1x1 behind an SSP hub");
    CHECK(l.Kbps == 5000000UL && l.Lanes == 1 && !l.Plus,
          "Gen 1x1 is not plus");
    CHECK_EQ(XhciSsHubDownstream(&bos, 0x00000009UL, &l),
             XHCI_SSHUB_NOT_FOUND, "an unlisted Rx SSID");
    CHECK_EQ(l.Kbps, 0, "cleared");
    CHECK_EQ(XhciSsHubDownstream(NULL, 0x11UL, &l), XHCI_SSHUB_NOT_FOUND,
             "no BOS");
    CHECK_EQ(XhciSsHubDownstream(&bos, 0x11UL, NULL), XHCI_SSHUB_BAD_PARAM,
             "NULL out");
}

static ULONG psi(ULONG psiv, ULONG psie, ULONG lp, ULONG psim)
{
    return psiv | (psie << 4) | (lp << 14) | (psim << 16);
}

/* Root port 1: a USB3 group with no PSI table (the defaults, as qemu-xhci
 * reports). Root port 2: a USB3 group whose table names Gen 1 at PSIV 4,
 * 10 Gb/s SuperSpeedPlus at 9 and 20 Gb/s at 12. Root port 3: a USB3
 * group whose table names Gen 1 alone. Root port 4: no protocol. */
static void mapBuild(PXHCI_PORT_MAP m)
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
    m->PortCount = 4;
    m->ProtocolCount = 3;
    m->Protocols[0].Major = 3;
    m->Protocols[0].PortOffset = 1;
    m->Protocols[0].PortCount = 1;
    m->Protocols[1].Major = 3;
    m->Protocols[1].Minor = 0x10;
    m->Protocols[1].PortOffset = 2;
    m->Protocols[1].PortCount = 1;
    m->Protocols[1].PsiCount = 3;
    m->Protocols[1].Psi[0] = psi(4, 3, 0, 5);
    m->Protocols[1].Psi[1] = psi(9, 3, 1, 10);
    m->Protocols[1].Psi[2] = psi(12, 3, 1, 20);
    m->Protocols[2].Major = 3;
    m->Protocols[2].PortOffset = 3;
    m->Protocols[2].PortCount = 1;
    m->Protocols[2].PsiCount = 1;
    m->Protocols[2].Psi[0] = psi(4, 3, 0, 5);
    m->Protocol[0] = 0;
    m->Protocol[1] = 1;
    m->Protocol[2] = 2;
}

static void linkOf(PXHCI_SSHUB_LINK l, ULONG laneGbps, ULONG lanes)
{
    l->LaneKbps = laneGbps * 1000000UL;
    l->Lanes = lanes;
    l->Kbps = l->LaneKbps * lanes;
    l->Plus = (lanes > 1 || l->Kbps > XHCI_RATE_GEN1_KBPS) ? 1UL : 0UL;
}

static void test_psiv(void)
{
    XHCI_PORT_MAP m;
    XHCI_SSHUB_LINK l;
    ULONG psiv;
    ULONG matched;

    mapBuild(&m);

    CHECK_EQ(XhciSsHubPsiv(&m, 1, NULL, &psiv, &matched), XHCI_SSHUB_OK,
             "no link read: SuperSpeed");
    CHECK(psiv == 4 && matched, "the default Gen 1 ID");
    linkOf(&l, 5, 1);
    CHECK_EQ(XhciSsHubPsiv(&m, 1, &l, &psiv, &matched), XHCI_SSHUB_OK,
             "Gen 1x1");
    CHECK(psiv == 4 && matched, "Gen 1x1 is SuperSpeed's ID");
    linkOf(&l, 10, 1);
    (VOID)XhciSsHubPsiv(&m, 1, &l, &psiv, &matched);
    CHECK(psiv == XHCI_PSIV_SSP_GEN2X1 && matched, "defaults: Gen 2x1 is 5");
    linkOf(&l, 5, 2);
    (VOID)XhciSsHubPsiv(&m, 1, &l, &psiv, &matched);
    CHECK(psiv == XHCI_PSIV_SSP_GEN1X2 && matched,
          "defaults: Gen 1x2 is 6, told apart by its lanes");
    linkOf(&l, 10, 2);
    (VOID)XhciSsHubPsiv(&m, 1, &l, &psiv, &matched);
    CHECK(psiv == XHCI_PSIV_SSP_GEN2X2 && matched, "defaults: Gen 2x2 is 7");
    linkOf(&l, 20, 2);
    (VOID)XhciSsHubPsiv(&m, 1, &l, &psiv, &matched);
    CHECK(psiv == 4 && !matched,
          "defaults: 40 Gb/s has no ID - SuperSpeed's, counted unmatched");

    linkOf(&l, 10, 1);
    (VOID)XhciSsHubPsiv(&m, 2, &l, &psiv, &matched);
    CHECK(psiv == 9 && matched, "table: 10 Gb/s is the table's 9");
    linkOf(&l, 10, 2);
    (VOID)XhciSsHubPsiv(&m, 2, &l, &psiv, &matched);
    CHECK(psiv == 12 && matched, "table: 20 Gb/s is the table's 12");
    linkOf(&l, 5, 2);
    (VOID)XhciSsHubPsiv(&m, 2, &l, &psiv, &matched);
    CHECK(psiv == 9 && matched,
          "table: Gen 1x2 takes the 10 Gb/s entry (one rate, two modes)");
    linkOf(&l, 20, 2);
    (VOID)XhciSsHubPsiv(&m, 2, &l, &psiv, &matched);
    CHECK(psiv == 12 && !matched,
          "table: no 40 Gb/s entry - the 20 Gb/s lane rate's ID, a guess, "
          "counted unmatched");
    linkOf(&l, 5, 1);
    (VOID)XhciSsHubPsiv(&m, 2, &l, &psiv, &matched);
    CHECK(psiv == 4 && matched, "table: Gen 1x1 is the table's Gen 1 ID");

    linkOf(&l, 10, 1);
    CHECK_EQ(XhciSsHubPsiv(&m, 3, &l, &psiv, &matched), XHCI_SSHUB_OK,
             "a Gen 1-only root port behind an SSP hub");
    CHECK(psiv == 4 && !matched, "SuperSpeed's ID, counted unmatched");

    CHECK_EQ(XhciSsHubPsiv(&m, 4, NULL, &psiv, &matched),
             XHCI_SSHUB_NOT_FOUND, "a port no protocol claims");

    /* After Address Device the controller's output Slot Context speed is
     * authoritative where it names a SuperSpeed rate. */
    CHECK_EQ(XhciSsHubAdoptSpeed(&m, 2, 12, 9), 9,
             "the controller corrected a guessed rate");
    CHECK_EQ(XhciSsHubAdoptSpeed(&m, 2, 12, 12), 12, "agreement");
    CHECK_EQ(XhciSsHubAdoptSpeed(&m, 2, 12, 0), 12,
             "an output of 0 is no answer");
    CHECK_EQ(XhciSsHubAdoptSpeed(&m, 2, 12, 3), 12,
             "a PSIV the table does not name is not adopted");
    CHECK_EQ(XhciSsHubAdoptSpeed(&m, 1, 4, 5), 5,
             "defaults: Gen 2x1 adopted over Gen 1");
    CHECK_EQ(XhciSsHubAdoptSpeed(&m, 1, 4, 3), 4,
             "defaults: a High-Speed ID is not a SuperSpeed answer");
    CHECK_EQ(XhciSsHubAdoptSpeed(&m, 4, 4, 5), 4,
             "a port no protocol claims keeps what was given");
    CHECK_EQ(XhciSsHubAdoptSpeed(NULL, 1, 4, 5), 4, "NULL map");
    CHECK_EQ(XhciSsHubPsiv(&m, 0, NULL, &psiv, &matched),
             XHCI_SSHUB_NOT_FOUND, "port 0");
    CHECK_EQ(XhciSsHubPsiv(&m, 9, NULL, &psiv, &matched),
             XHCI_SSHUB_NOT_FOUND, "past PortCount");
    CHECK_EQ(XhciSsHubPsiv(NULL, 1, NULL, &psiv, &matched),
             XHCI_SSHUB_BAD_PARAM, "NULL map");

    CHECK_EQ(XhciHubEnumSpeed(XHCI_SPEED_SUPER), XHCI_ENUM_SPEED_SUPER,
             "a SuperSpeed hub port feeds the machine SuperSpeed");
}

static void test_pairing(void)
{
    /* The SuperSpeed half on root port 5, its USB 2.0 half on root port 1,
     * which the controller pairs with 5. */
    CHECK(XhciSsHubLooksPaired(0x05E3, 5, 0, 0, 0x05E3, 1, 0, 0, 1),
          "the bench unit's two halves on one connector");
    CHECK(XhciSsHubLooksPaired(0x05E3, 5, 1, 0x3, 0x05E3, 1, 1, 0x3, 1),
          "one tier down, on the same port of each parent half");
    CHECK(!XhciSsHubLooksPaired(0x05E3, 5, 0, 0, 0x2109, 1, 0, 0, 1),
          "another vendor's hub on the companion");
    CHECK(!XhciSsHubLooksPaired(0x05E3, 5, 0, 0, 0x05E3, 2, 0, 0, 1),
          "not on the companion port");
    CHECK(!XhciSsHubLooksPaired(0x05E3, 5, 0, 0, 0x05E3, 1, 0, 0, 0),
          "a SuperSpeed port with no companion pairs nothing");
    CHECK(!XhciSsHubLooksPaired(0x05E3, 5, 1, 0x3, 0x05E3, 1, 1, 0x4, 1),
          "another port of the parent");
    CHECK(!XhciSsHubLooksPaired(0x05E3, 5, 0, 0, 0x05E3, 1, 1, 0, 1),
          "another tier");
}

int main(void)
{
    test_descriptor();
    test_link_field();
    test_decide();
    test_selectors();
    test_reset_kind();
    test_reset_progress();
    test_resume();
    test_ext_status();
    test_psiv();
    test_pairing();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
