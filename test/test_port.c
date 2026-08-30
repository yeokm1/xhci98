/*
 * test_port.c - host tests for PORTSC write construction (src/xhci_port.c).
 *
 * PORTSC is the register where the obvious read-modify-write is a bug, and
 * every one of its bugs is silent: a port that disables itself, a reset that
 * restarts, a connect change that disappears before the hub driver sees it.
 * None of those produce an error anywhere - the port simply stops working -
 * so the masks are checked here bit by bit rather than in a guest.
 *
 * Per docs/contributing/design/03-host-unit-tests.md, every expected value is
 * transcribed by hand from the PORTSC table in docs/usb-xhci-info/xhci-data-structures.md
 * section 3 - never produced by the code under test. The bit positions below
 * are therefore written as literal hex, not as the driver's own macros,
 * wherever the point is the position itself.
 *
 * Build and run:  test\run-host-tests.cmd
 * Exit code = number of failed checks (0 = pass).
 *
 * C89, no framework.
 */

#include <stdio.h>
#include "../src/xhci.h"

static int failures;
static int checks;

#define CHECK(cond, what) check_impl((cond), (what), __LINE__)

static void check_impl(int cond, const char *what, int line)
{
    checks++;
    if (!cond) {
        failures++;
        printf("FAIL %s:%d: %s\n", "test_port.c", line, what);
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
        printf("FAIL %s:%d: %s (got %lu / 0x%lX, want %lu / 0x%lX)\n",
               "test_port.c", line, what, got, got, want, want);
    }
}

/* ------------------------------------------------------------------ */
/* 1. Bit positions, transcribed from spec Table 5-27                  */
/* ------------------------------------------------------------------ */

static void test_bit_positions(void)
{
    CHECK_EQ(XHCI_PORTSC_CCS, 0x00000001UL, "CCS is bit 0");
    CHECK_EQ(XHCI_PORTSC_PED, 0x00000002UL, "PED is bit 1");
    CHECK_EQ(XHCI_PORTSC_OCA, 0x00000008UL, "OCA is bit 3");
    CHECK_EQ(XHCI_PORTSC_PR, 0x00000010UL, "PR is bit 4");
    CHECK_EQ(XHCI_PORTSC_PLS_MASK, 0x000001E0UL, "PLS is bits 8:5");
    CHECK_EQ(XHCI_PORTSC_PP, 0x00000200UL, "PP is bit 9");
    CHECK_EQ(XHCI_PORTSC_SPEED_MASK, 0x00003C00UL, "Port Speed is bits 13:10");
    CHECK_EQ(XHCI_PORTSC_LWS, 0x00010000UL, "LWS is bit 16");
    CHECK_EQ(XHCI_PORTSC_CSC, 0x00020000UL, "CSC is bit 17");
    CHECK_EQ(XHCI_PORTSC_PEC, 0x00040000UL, "PEC is bit 18");
    CHECK_EQ(XHCI_PORTSC_WRC, 0x00080000UL, "WRC is bit 19");
    CHECK_EQ(XHCI_PORTSC_OCC, 0x00100000UL, "OCC is bit 20");
    CHECK_EQ(XHCI_PORTSC_PRC, 0x00200000UL, "PRC is bit 21");
    CHECK_EQ(XHCI_PORTSC_PLC, 0x00400000UL, "PLC is bit 22");
    CHECK_EQ(XHCI_PORTSC_CEC, 0x00800000UL, "CEC is bit 23");
    CHECK_EQ(XHCI_PORTSC_WPR, 0x80000000UL, "WPR is bit 31");

    /* All seven RW1C bits, 23:17, and nothing else. */
    CHECK_EQ(XHCI_PORTSC_CHANGE_MASK, 0x00FE0000UL, "change bits are 23:17");

    CHECK_EQ(XHCI_PORTSC_GET_SPEED(0x00000C00UL), 3, "speed 3 = High Speed");
    CHECK_EQ(XHCI_PORTSC_GET_SPEED(0xFFFFFFFFUL), 15, "speed field is 4 bits");
    CHECK_EQ(XHCI_PORTSC_GET_PLS(0x00000060UL), 3, "PLS 3 = U3/suspend");
    CHECK_EQ(XHCI_PORTSC_GET_PLS(0xFFFFFFFFUL), 15, "PLS field is 4 bits");
}

/* ------------------------------------------------------------------ */
/* 2. The neutral value                                                */
/* ------------------------------------------------------------------ */

/*
 * A port that is connected, enabled, powered, at High Speed, with a connect
 * change and a reset change pending - the ordinary state right after a reset
 * completes, and the one where a naive read-modify-write does the most damage.
 *
 *   CCS  bit 0   = 1
 *   PED  bit 1   = 1
 *   PLS  8:5     = 0 (U0)
 *   PP   bit 9   = 1
 *   Speed 13:10  = 3 (HS)  -> 0x0C00
 *   CSC  bit 17  = 1
 *   PRC  bit 21  = 1
 *                = 1 | 2 | 0x200 | 0xC00 | 0x20000 | 0x200000 = 0x00220E03
 */
#define PORTSC_BUSY 0x00220E03UL

static void test_neutral(void)
{
    ULONG neutral;

    neutral = XhciPortscNeutral(PORTSC_BUSY);

    /* What must survive: connection, power, speed, link state. */
    CHECK_EQ(neutral & XHCI_PORTSC_CCS, XHCI_PORTSC_CCS, "CCS preserved");
    CHECK_EQ(neutral & XHCI_PORTSC_PP, XHCI_PORTSC_PP, "port power preserved");
    CHECK_EQ(XHCI_PORTSC_GET_SPEED(neutral), 3, "speed preserved");

    /* What must not: writing any of these acts on the port. */
    CHECK_EQ(neutral & XHCI_PORTSC_PED, 0,
             "PED dropped - writing it back disables the port");
    CHECK_EQ(neutral & XHCI_PORTSC_PR, 0, "PR dropped");
    CHECK_EQ(neutral & XHCI_PORTSC_WPR, 0, "WPR dropped");
    CHECK_EQ(neutral & XHCI_PORTSC_LWS, 0,
             "LWS dropped - it would arm the preserved PLS field");
    CHECK_EQ(neutral & XHCI_PORTSC_CHANGE_MASK, 0,
             "every change bit dropped - they are RW1C");
    CHECK_EQ(neutral & 0x30000004UL, 0, "RsvdZ bits written as zero");

    CHECK_EQ(neutral, 0x00000E01UL, "the whole neutral value");

    /* An all-ones read is the device not decoding; the builder must still not
     * turn it into a port-disabling write if a caller gets that far. */
    CHECK_EQ(XhciPortscNeutral(0xFFFFFFFFUL) & XHCI_PORTSC_UNSAFE_MASK, 0,
             "even an all-ones read produces an inert value");

    /* A port mid-reset: PR reads 1, and writing it back would restart. */
    CHECK_EQ(XhciPortscNeutral(0x00000211UL), 0x00000201UL,
             "a reset in progress is not restarted");
}

/* ------------------------------------------------------------------ */
/* 3. Setting one bit                                                  */
/* ------------------------------------------------------------------ */

static void test_set(void)
{
    /* Powering an unpowered, disconnected port: nothing else changes. */
    CHECK_EQ(XhciPortscWith(0x00000000UL, XHCI_PORTSC_PP), 0x00000200UL,
             "power a cold port");

    /* Powering a port whose connect change is pending must not clear it -
     * usbport has not been told about that connect yet. */
    CHECK_EQ(XhciPortscWith(0x00020001UL, XHCI_PORTSC_PP), 0x00000201UL,
             "CSC survives a port-power write");

    /* Starting a reset on the busy port: PR set, everything else inert. */
    CHECK_EQ(XhciPortscWith(PORTSC_BUSY, XHCI_PORTSC_PR), 0x00000E11UL,
             "reset request carries only PR");

    /* Two bits at once is legal and still drops the unsafe ones. */
    CHECK_EQ(XhciPortscWith(PORTSC_BUSY, XHCI_PORTSC_PP | XHCI_PORTSC_WCE),
             0x02000E01UL, "power plus wake-on-connect");
}

/* ------------------------------------------------------------------ */
/* 4. Clearing change bits                                             */
/* ------------------------------------------------------------------ */

/*
 * RW1C means the write carries a 1 in each bit being cleared - the exact
 * shape that accidentally clears the neighbours. The busy port has both CSC
 * and PRC pending; clearing one must leave the other.
 */
static void test_clear_changes(void)
{
    ULONG value;

    value = XhciPortscClearChanges(PORTSC_BUSY, XHCI_PORTSC_CSC);
    CHECK_EQ(value & XHCI_PORTSC_CSC, XHCI_PORTSC_CSC, "CSC written as 1");
    CHECK_EQ(value & XHCI_PORTSC_PRC, 0, "PRC not written, so not cleared");
    CHECK_EQ(value, 0x00020E01UL, "the whole clear-CSC value");

    value = XhciPortscClearChanges(PORTSC_BUSY,
                                   XHCI_PORTSC_CSC | XHCI_PORTSC_PRC);
    CHECK_EQ(value, 0x00220E01UL, "clearing both named changes");

    /*
     * A caller that passes the raw register value asks to clear exactly the
     * changes that were set - and nothing outside the change field.
     */
    value = XhciPortscClearChanges(PORTSC_BUSY, PORTSC_BUSY);
    CHECK_EQ(value, 0x00220E01UL, "raw value clears only its own change bits");
    CHECK_EQ(value & XHCI_PORTSC_PED, 0, "and does not carry PED back");

    /* Clearing nothing is a neutral write. */
    CHECK_EQ(XhciPortscClearChanges(PORTSC_BUSY, 0),
             XhciPortscNeutral(PORTSC_BUSY), "clearing no change bits");
}

/* ------------------------------------------------------------------ */
/* 5. Link state writes                                                */
/* ------------------------------------------------------------------ */

/*
 * PLS only takes effect when LWS is set in the same write, which is why LWS
 * is unsafe everywhere else and deliberate here. U3 (suspend) is PLS 3 -> the
 * field value 0x60 - and U0 (resume target) is 0.
 */
static void test_link_state(void)
{
    ULONG value;

    value = XhciPortscSetLinkState(PORTSC_BUSY, 3);
    CHECK_EQ(XHCI_PORTSC_GET_PLS(value), 3, "PLS set to U3");
    CHECK_EQ(value & XHCI_PORTSC_LWS, XHCI_PORTSC_LWS, "LWS armed");
    CHECK_EQ(value & XHCI_PORTSC_CHANGE_MASK, 0, "no change bit cleared");
    CHECK_EQ(value & XHCI_PORTSC_PED, 0, "PED still dropped");
    CHECK_EQ(value, 0x00010E61UL, "the whole suspend write");

    /* The old PLS is replaced, not ORed into. */
    value = XhciPortscSetLinkState(0x000001E1UL, 0);
    CHECK_EQ(XHCI_PORTSC_GET_PLS(value), 0, "PLS 15 replaced by U0");
    CHECK_EQ(value, 0x00010001UL, "the whole resume-to-U0 write");

    /* A PLS argument wider than the field cannot spill into its neighbours. */
    value = XhciPortscSetLinkState(0, 0xFF);
    CHECK_EQ(value & ~(XHCI_PORTSC_PLS_MASK | XHCI_PORTSC_LWS), 0,
             "an oversized PLS is masked to the field");
}

/* ------------------------------------------------------------------ */
/* 6. The five named operations (roadmap Phase 5 task 3)               */
/* ------------------------------------------------------------------ */

/*
 * A golden value per operation, each written out by hand from the busy port
 * above, because the point of these functions is that the operations are *not*
 * interchangeable and a shared expression would prove nothing.
 *
 * PORTSC_BUSY is 0x00220E03: CCS | PED | PP | speed 3 | CSC | PRC, so its
 * neutral value is 0x00000E01 (CCS | PP | speed 3) and every vector below is
 * that plus exactly one thing.
 */
static void test_operations(void)
{
    /* Power on: PP set on top of the neutral value - which already has it, so
     * this one is the case where the operation changes nothing and must still
     * not carry a change bit back. */
    CHECK_EQ(XhciPortscPower(PORTSC_BUSY, 1), 0x00000E01UL,
             "power on a port that is already powered");
    CHECK_EQ(XhciPortscPower(0x00020001UL, 1), 0x00000201UL,
             "power on a cold port with a connect change pending");

    /* Power off: PP is RW, so this is a written zero. */
    CHECK_EQ(XhciPortscPower(PORTSC_BUSY, 0), 0x00000C01UL,
             "power off drops PP and nothing else");
    CHECK_EQ(XhciPortscPower(PORTSC_BUSY, 0) & XHCI_PORTSC_PP, 0,
             "PP really is clear in that value");

    /* Reset: PR is RW1S, so this is a written one. */
    CHECK_EQ(XhciPortscReset(PORTSC_BUSY), 0x00000E11UL,
             "reset carries PR and the neutral value");
    CHECK_EQ(XhciPortscReset(PORTSC_BUSY) & XHCI_PORTSC_WPR, 0,
             "and never the SuperSpeed warm reset");

    /*
     * Disable: PED is RW1C, so this is a written *one* - the exact opposite
     * shape from power off, on a bit that reads 1 when the port is working.
     * This is the single vector that would catch "clearing is clearing".
     */
    CHECK_EQ(XhciPortscDisable(PORTSC_BUSY), 0x00000E03UL,
             "disable writes PED as 1");
    CHECK_EQ(XhciPortscDisable(PORTSC_BUSY) & XHCI_PORTSC_PED,
             XHCI_PORTSC_PED, "which is what disables the port");
    CHECK(XhciPortscPower(PORTSC_BUSY, 0) != XhciPortscDisable(PORTSC_BUSY),
          "power off and disable are different writes");

    /* Suspend and resume are PLS writes, so they are the only ones that carry
     * LWS - and they must carry it, or the PLS field is inert. */
    CHECK_EQ(XhciPortscSuspend(PORTSC_BUSY), 0x00010E61UL,
             "suspend writes PLS = U3 with LWS");
    CHECK_EQ(XHCI_PORTSC_GET_PLS(XhciPortscSuspend(PORTSC_BUSY)), 3,
             "U3 is PLS 3");

    /*
     * **A USB 2.0 resume is two writes with 20 ms between them, and the first
     * one is not U0.** "For a USB2 protocol port, software shall write a '15'
     * (Resume) to the PLS field to initiate resume signaling ... After TDRSMDN
     * is complete, software shall write a '0' (U0) to the PLS field" (4.15.2.2,
     * p.257) - while the USB 3.x port in the same list writes '0' to *initiate*.
     * The first version of this driver wrote U0 to start, which is the USB 3.x
     * value on a driver that serves only USB 2.0 ports: a resume that reports
     * success and leaves the port in U3.
     *
     * A connected, powered High Speed port in U3 is
     * CCS | PLS 3 | PP | speed 3 = 1 | 0x60 | 0x200 | 0xC00 = 0x00000E61, and
     * the same port in Resume replaces PLS 3 with PLS 15 (0x1E0) = 0x00000FE1.
     */
    CHECK_EQ(XhciPortscResumeSignal(0x00000E61UL), 0x00010FE1UL,
             "resume step 1 writes PLS = Resume (15) with LWS");
    CHECK_EQ(XHCI_PORTSC_GET_PLS(XhciPortscResumeSignal(PORTSC_BUSY)), 15,
             "Resume is PLS 15");
    CHECK_EQ(XhciPortscResumeDone(0x00000FE1UL), 0x00010E01UL,
             "resume step 2 writes PLS = U0 with LWS");
    CHECK_EQ(XHCI_PORTSC_GET_PLS(XhciPortscResumeDone(PORTSC_BUSY)), 0,
             "U0 is PLS 0");
    CHECK(XhciPortscResumeSignal(PORTSC_BUSY) !=
              XhciPortscResumeDone(PORTSC_BUSY),
          "the two halves of a resume are different writes");
    CHECK(XhciPortscResumeSignal(PORTSC_BUSY) != XhciPortscSuspend(PORTSC_BUSY),
          "and neither of them is the suspend write");

    /* Every one of the five leaves both pending changes alone. */
    CHECK_EQ(XhciPortscPower(PORTSC_BUSY, 1) & XHCI_PORTSC_CHANGE_MASK, 0,
             "power on acknowledges no change");
    CHECK_EQ(XhciPortscPower(PORTSC_BUSY, 0) & XHCI_PORTSC_CHANGE_MASK, 0,
             "power off acknowledges no change");
    CHECK_EQ(XhciPortscReset(PORTSC_BUSY) & XHCI_PORTSC_CHANGE_MASK, 0,
             "reset acknowledges no change");
    CHECK_EQ(XhciPortscDisable(PORTSC_BUSY) & XHCI_PORTSC_CHANGE_MASK, 0,
             "disable acknowledges no change");
    CHECK_EQ(XhciPortscSuspend(PORTSC_BUSY) & XHCI_PORTSC_CHANGE_MASK, 0,
             "suspend acknowledges no change");
    CHECK_EQ(XhciPortscResumeSignal(PORTSC_BUSY) & XHCI_PORTSC_CHANGE_MASK, 0,
             "resume step 1 acknowledges no change");
    CHECK_EQ(XhciPortscResumeDone(PORTSC_BUSY) & XHCI_PORTSC_CHANGE_MASK, 0,
             "resume step 2 acknowledges no change");

    /* And none of them but disable carries PED, none but reset carries PR. */
    CHECK_EQ(XhciPortscPower(PORTSC_BUSY, 1) & XHCI_PORTSC_PED, 0,
             "power on does not disable the port");
    CHECK_EQ(XhciPortscSuspend(PORTSC_BUSY) & XHCI_PORTSC_PED, 0,
             "suspend does not disable the port");
    CHECK_EQ(XhciPortscResumeSignal(PORTSC_BUSY) & XHCI_PORTSC_PED, 0,
             "resume step 1 does not disable the port");
    CHECK_EQ(XhciPortscResumeDone(PORTSC_BUSY) & XHCI_PORTSC_PED, 0,
             "resume step 2 does not disable the port");
    CHECK_EQ(XhciPortscDisable(PORTSC_BUSY) & XHCI_PORTSC_PR, 0,
             "disable does not start a reset");
    CHECK_EQ(XhciPortscPower(PORTSC_BUSY, 0) & XHCI_PORTSC_LWS, 0,
             "power off does not arm the link state field");
    CHECK_EQ(XhciPortscReset(PORTSC_BUSY) & XHCI_PORTSC_LWS, 0,
             "nor does reset");
}

/* ------------------------------------------------------------------ */
/* 7. The logical-port map (roadmap Phase 5 task 2)                    */
/* ------------------------------------------------------------------ */

/*
 * The map exists because hub port n is not PORTSC n on any controller that
 * interleaves its protocol groups - so the vector that matters is the one where
 * the USB 3.x group comes *first*, which no arithmetic shortcut survives.
 */
static XHCI_PORT_MAP map;
static XHCI_ROOT_HUB rh;

static void map_reset(ULONG ports)
{
    ULONG i;

    for (i = 0; i < sizeof(map); i++) {
        ((unsigned char *)&map)[i] = 0;
    }
    map.PortCount = ports;
    map.ProtocolCount = 0;
    for (i = 0; i < XHCI_MAX_ROOT_PORTS; i++) {
        map.Class[i] = XHCI_PORT_CLASS_NONE;
        map.Protocol[i] = XHCI_PORT_NO_PROTOCOL;
        map.Companion[i] = XHCI_PORT_NO_COMPANION;
    }
}

static void test_root_hub_map(void)
{
    /* USB 3.x on 1-2, USB 2.0 on 3-6: hub ports 1..4 are PORTSC 3..6. */
    map_reset(6);
    map.Class[0] = XHCI_PORT_CLASS_USB3_COMPANION;
    map.Class[1] = XHCI_PORT_CLASS_USB3_COMPANION;
    map.Class[2] = XHCI_PORT_CLASS_USB2_COMPANION;
    map.Class[3] = XHCI_PORT_CLASS_USB2_COMPANION;
    map.Class[4] = XHCI_PORT_CLASS_USB2_ONLY;
    map.Class[5] = XHCI_PORT_CLASS_USB2_ONLY;

    CHECK_EQ(XhciRootHubBuild(&map, &rh), XHCI_RH_OK, "a map with USB2 ports");
    CHECK_EQ(rh.PortCount, 4, "four managed ports become four hub ports");
    CHECK_EQ(XhciRootHubPortOf(&rh, 1), 3, "hub port 1 is PORTSC 3");
    CHECK_EQ(XhciRootHubPortOf(&rh, 4), 6, "hub port 4 is PORTSC 6");
    CHECK_EQ(XhciRootHubHubPortOf(&rh, 6), 4, "and back again");
    CHECK_EQ(XhciRootHubHubPortOf(&rh, 1), 0,
             "a USB 3.x port has no hub port at all");
    CHECK_EQ(XhciRootHubPortOf(&rh, 0), 0, "hub port 0 does not exist");
    CHECK_EQ(XhciRootHubPortOf(&rh, 5), 0, "nor does one past the last");
    CHECK_EQ(XhciRootHubHubPortOf(&rh, 7), 0, "nor a port past MaxPorts");

    /* Every shadow starts clear, including the speed - a rebuild follows a
     * reset, so nothing from the previous reading may survive it. */
    CHECK_EQ(rh.Ports[0].Portsc, 0, "shadows are cleared by a build");
    CHECK_EQ(rh.Ports[0].Changes, 0, "including the latched changes");
    CHECK_EQ(rh.Ports[3].Speed, XHCI_SPEED_UNKNOWN, "and the decoded speed");

    /* A controller with nothing this driver can serve is refused rather than
     * reported: zero ports is a ~1 GB nonpaged allocation inside usbport. */
    map_reset(4);
    map.Class[0] = XHCI_PORT_CLASS_USB3_ORPHAN;
    map.Class[1] = XHCI_PORT_CLASS_USB3_ORPHAN;
    CHECK_EQ(XhciRootHubBuild(&map, &rh), XHCI_RH_NO_PORTS,
             "no USB 2.0 port refuses the build");
    CHECK_EQ(rh.PortCount, 0, "and reports no ports");
    CHECK_EQ(XhciRootHubPortOf(&rh, 1), 0,
             "a refused map answers no port for every index");

    CHECK_EQ(XhciRootHubBuild(NULL, &rh), XHCI_RH_BAD_PARAM, "NULL map");
    CHECK_EQ(XhciRootHubBuild(&map, NULL), XHCI_RH_BAD_PARAM, "NULL root hub");
}

/* ------------------------------------------------------------------ */
/* 8. The shadow (roadmap Phase 5 task 2)                              */
/* ------------------------------------------------------------------ */

/*
 * The two halves that have to be kept apart: what the driver acknowledges in
 * hardware (immediately, or the port stops reporting) and what it latches for
 * the hub class (until the clear-feature callback takes it down).
 */
static void test_shadow_latch_and_ack(void)
{
    XHCI_PORT_SHADOW shadow;
    ULONG ack;
    ULONG status;
    ULONG change;
    ULONG latched;

    shadow.Portsc = 0;
    shadow.Changes = 0;
    shadow.Speed = XHCI_SPEED_UNKNOWN;

    /* A device arrives: CCS and CSC, nothing else. */
    latched = XhciPortShadowUpdate(&shadow, 0x00020201UL, XHCI_SPEED_UNKNOWN,
                                   &ack);
    CHECK_EQ(latched, XHCI_HUB_C_PORT_CONNECTION, "a connect latches C_CONNECT");
    CHECK_EQ(ack, XHCI_PORTSC_CSC, "and asks for CSC to be acknowledged");
    XhciPortShadowReport(&shadow, &status, &change);
    /* High Speed rides along on every connection whatever the speed decoded to
     * - Phase 5 task 7, asserted on its own in test_shadow_report_translation. */
    CHECK_EQ(status,
             XHCI_HUB_PORT_CONNECTION | XHCI_HUB_PORT_POWER |
                 XHCI_HUB_PORT_HIGH_SPEED,
             "reported as connected and powered");
    CHECK_EQ(change, XHCI_HUB_C_PORT_CONNECTION, "with the connect change");

    /*
     * The hardware bit has been acknowledged by now, so the next read has CSC
     * clear - and the latched change must survive that, because usbport may not
     * poll for milliseconds. This is the half that a driver acknowledging
     * without latching loses.
     */
    latched = XhciPortShadowUpdate(&shadow, 0x00000203UL, XHCI_SPEED_UNKNOWN,
                                   &ack);
    CHECK_EQ(latched, 0, "a quiet read latches nothing new");
    CHECK_EQ(ack, 0, "and acknowledges nothing");
    XhciPortShadowReport(&shadow, &status, &change);
    CHECK_EQ(change, XHCI_HUB_C_PORT_CONNECTION,
             "the latched connect survives the acknowledgement");

    /* Only the matching clear-feature takes it down. */
    CHECK_EQ(XhciPortShadowClearChange(&shadow, XHCI_HUB_C_PORT_RESET), 0,
             "clearing a change that is not set reports nothing cleared");
    XhciPortShadowReport(&shadow, &status, &change);
    CHECK_EQ(change, XHCI_HUB_C_PORT_CONNECTION, "and leaves the connect");
    CHECK_EQ(XhciPortShadowClearChange(&shadow, XHCI_HUB_C_PORT_CONNECTION), 1,
             "clearing the connect reports it was set");
    XhciPortShadowReport(&shadow, &status, &change);
    CHECK_EQ(change, 0, "and the port has no changes left");

    /* Repeated polling neither loses nor resurrects it. */
    latched = XhciPortShadowUpdate(&shadow, 0x00000203UL, XHCI_SPEED_UNKNOWN,
                                   &ack);
    XhciPortShadowReport(&shadow, &status, &change);
    CHECK_EQ(change, 0, "a poll after the clear resurrects nothing");
    CHECK_EQ(latched, 0, "and latches nothing");
}

static void test_shadow_change_bits(void)
{
    XHCI_PORT_SHADOW shadow;
    ULONG ack;
    ULONG latched;

    shadow.Portsc = 0;
    shadow.Changes = 0;
    shadow.Speed = XHCI_SPEED_UNKNOWN;

    /* Each xHCI change bit becomes the hub-class change it means, and only
     * that one. PEC -> C_ENABLE, OCC -> C_OVER_CURRENT, PRC -> C_RESET. */
    latched = XhciPortShadowUpdate(&shadow, XHCI_PORTSC_PEC, 0, &ack);
    CHECK_EQ(latched, XHCI_HUB_C_PORT_ENABLE, "PEC latches C_ENABLE");
    shadow.Changes = 0;
    latched = XhciPortShadowUpdate(&shadow, XHCI_PORTSC_OCC, 0, &ack);
    CHECK_EQ(latched, XHCI_HUB_C_PORT_OVER_CURRENT, "OCC latches C_OVER_CURRENT");
    shadow.Changes = 0;
    latched = XhciPortShadowUpdate(&shadow, XHCI_PORTSC_PRC, 0, &ack);
    CHECK_EQ(latched, XHCI_HUB_C_PORT_RESET, "PRC latches C_RESET");

    /*
     * WRC and CEC are SuperSpeed-only: they latch nothing, and they are still
     * acknowledged. An unacknowledged change bit suppresses the controller's
     * next Port Status Change Event for the port, so "not our concern" and
     * "leave it set" are different decisions.
     */
    shadow.Changes = 0;
    latched = XhciPortShadowUpdate(&shadow, XHCI_PORTSC_WRC | XHCI_PORTSC_CEC,
                                   0, &ack);
    CHECK_EQ(latched, 0, "the SuperSpeed change bits mean nothing to a hub port");
    CHECK_EQ(ack, XHCI_PORTSC_WRC | XHCI_PORTSC_CEC,
             "and are acknowledged all the same");

    /* Several at once, and the ack is exactly the set that was present. */
    shadow.Portsc = 0;
    shadow.Changes = 0;
    latched = XhciPortShadowUpdate(&shadow,
                                   XHCI_PORTSC_CSC | XHCI_PORTSC_PEC |
                                       XHCI_PORTSC_PRC | XHCI_PORTSC_CCS,
                                   0, &ack);
    CHECK_EQ(latched, XHCI_HUB_C_PORT_CONNECTION | XHCI_HUB_C_PORT_ENABLE |
                          XHCI_HUB_C_PORT_RESET,
             "three changes in one read");
    CHECK_EQ(ack, XHCI_PORTSC_CSC | XHCI_PORTSC_PEC | XHCI_PORTSC_PRC,
             "acknowledged together, and no state bit with them");

    /*
     * An all-ones read is a window that has stopped decoding, not a port with
     * every change pending. Latching it would report a connected, enabled,
     * over-current, resetting port and ask for a write to a register that is
     * not there.
     */
    shadow.Portsc = 0x00000203UL;
    shadow.Changes = 0;
    latched = XhciPortShadowUpdate(&shadow, 0xFFFFFFFFUL, 0, &ack);
    CHECK_EQ(latched, 0, "an all-ones PORTSC latches nothing");
    CHECK_EQ(ack, 0, "and asks for no write");
    CHECK_EQ(shadow.Portsc, 0x00000203UL, "the shadow keeps what it knew");
}

/*
 * C_PORT_SUSPEND is the hub class's "the resume finished", and xHCI has no such
 * bit - only PLC, which says the link state changed. The rule is therefore a
 * comparison against the previous state, which is the reason the shadow keeps
 * the whole previous PORTSC.
 */
static void test_shadow_suspend_resume(void)
{
    XHCI_PORT_SHADOW shadow;
    ULONG ack;
    ULONG status;
    ULONG change;
    ULONG latched;
    ULONG u3;

    /*
     * CCS | PED | PP. A suspended port is an *enabled* port with something
     * attached - which the first version of these vectors left out, and which
     * matters since a completed resume is only completed on such a port.
     * PLS = U3 is 3 << 5 = 0x60.
     */
    u3 = 0x00000203UL | (3UL << XHCI_PORTSC_PLS_SHIFT);

    shadow.Portsc = 0;
    shadow.Changes = 0;
    shadow.Speed = XHCI_SPEED_UNKNOWN;

    /* Suspended: reported as suspended, and no change is owed for going down. */
    latched = XhciPortShadowUpdate(&shadow, u3, XHCI_SPEED_HIGH, &ack);
    CHECK_EQ(latched, 0, "entering suspend latches no change");
    XhciPortShadowReport(&shadow, &status, &change);
    CHECK_EQ(status & XHCI_HUB_PORT_SUSPEND, XHCI_HUB_PORT_SUSPEND,
             "a port in U3 reports suspended");

    /* Resume signalling: still suspended, still no change. */
    latched = XhciPortShadowUpdate(&shadow,
                                   0x00000203UL |
                                       (15UL << XHCI_PORTSC_PLS_SHIFT),
                                   XHCI_SPEED_HIGH, &ack);
    CHECK_EQ(latched, 0, "resume signalling is not a completed resume");
    XhciPortShadowReport(&shadow, &status, &change);
    CHECK_EQ(status & XHCI_HUB_PORT_SUSPEND, XHCI_HUB_PORT_SUSPEND,
             "and the port still reports suspended");

    /* Back in U0 with PLC: that is the completed resume. */
    latched = XhciPortShadowUpdate(&shadow, 0x00000203UL | XHCI_PORTSC_PLC,
                                   XHCI_SPEED_HIGH, &ack);
    CHECK_EQ(latched, XHCI_HUB_C_PORT_SUSPEND,
             "leaving U3 with PLC set latches C_SUSPEND");
    CHECK_EQ(ack, XHCI_PORTSC_PLC, "and acknowledges PLC");
    XhciPortShadowReport(&shadow, &status, &change);
    CHECK_EQ(status & XHCI_HUB_PORT_SUSPEND, 0, "the port is awake");
    CHECK_EQ(change & XHCI_HUB_C_PORT_SUSPEND, XHCI_HUB_C_PORT_SUSPEND,
             "with the resume reported");

    /*
     * **A port that left Resume because it was *disabled* has not completed a
     * resume**, and from the register the two look identical: PLC's own
     * exclusion covers only "software setting PP to '0'" (Table 5-27, p.378), so
     * a disable really does set it. Reporting C_PORT_SUSPEND there would tell
     * usbhub a sleeping device woke up when its port was taken out of service
     * under it - which is exactly what an interrupted resume produces.
     */
    shadow.Portsc = 0x00000203UL | (15UL << XHCI_PORTSC_PLS_SHIFT);
    shadow.Changes = 0;
    latched = XhciPortShadowUpdate(&shadow,
                                   0x00000201UL | XHCI_PORTSC_PLC,
                                   XHCI_SPEED_HIGH, &ack);
    CHECK_EQ(latched, 0,
             "leaving Resume on a port that is no longer enabled is not a "
             "completed resume");
    CHECK_EQ(ack, XHCI_PORTSC_PLC,
             "though the change bit is still acknowledged - an unacknowledged "
             "one silences the port");

    /* Nor is it one on a port whose device has gone. */
    shadow.Portsc = 0x00000203UL | (3UL << XHCI_PORTSC_PLS_SHIFT);
    shadow.Changes = 0;
    latched = XhciPortShadowUpdate(&shadow, 0x00000202UL | XHCI_PORTSC_PLC,
                                   XHCI_SPEED_HIGH, &ack);
    CHECK_EQ(latched & XHCI_HUB_C_PORT_SUSPEND, 0,
             "nor is one whose device left mid-resume");

    /*
     * A PLC on a port that was never suspended is some other link-state change:
     * acknowledged, but it is not a resume and must not be reported as one.
     */
    shadow.Portsc = 0x00000203UL;
    shadow.Changes = 0;
    latched = XhciPortShadowUpdate(&shadow, 0x00000203UL | XHCI_PORTSC_PLC,
                                   XHCI_SPEED_HIGH, &ack);
    CHECK_EQ(latched, 0, "a PLC without a suspend behind it is not a resume");
    CHECK_EQ(ack, XHCI_PORTSC_PLC, "and is acknowledged anyway");
}

/*
 * The reporting rule task 2 states in one line - "never expose raw xHCI bit
 * positions as USB hub-class status bits" - checked in the only way that means
 * anything: a PORTSC whose raw bits would be wrong in every field if they were
 * passed through.
 */
static void test_shadow_report_translation(void)
{
    XHCI_PORT_SHADOW shadow;
    ULONG status;
    ULONG change;

    /* Connected, enabled, powered, over-current, resetting, High Speed. */
    shadow.Portsc = XHCI_PORTSC_CCS | XHCI_PORTSC_PED | XHCI_PORTSC_OCA |
                    XHCI_PORTSC_PR | XHCI_PORTSC_PP;
    shadow.Changes = 0;
    shadow.Speed = XHCI_SPEED_HIGH;

    XhciPortShadowReport(&shadow, &status, &change);
    CHECK_EQ(status,
             XHCI_HUB_PORT_CONNECTION | XHCI_HUB_PORT_ENABLE |
                 XHCI_HUB_PORT_OVER_CURRENT | XHCI_HUB_PORT_RESET |
                 XHCI_HUB_PORT_POWER | XHCI_HUB_PORT_HIGH_SPEED,
             "every state bit translated to its hub-class position");
    CHECK(status != shadow.Portsc,
          "which is not the raw register value - the positions differ");

    /*
     * Phase 5 task 7: every connected port reports High Speed whatever it
     * decoded, and XHCI_HUB_PORT_LOW_SPEED is never set at all. usbport applies
     * the EHCI model, in which a non-HS device under a root hub sends
     * USBPORT_GetTt into an unguarded CONTAINING_RECORD over an empty TtList
     * (docs/usb-xhci-info/usbport-miniport-abi.md section 8). Each speed is asserted
     * separately rather than in a loop so a regression names the speed.
     */
    shadow.Speed = XHCI_SPEED_LOW;
    XhciPortShadowReport(&shadow, &status, &change);
    CHECK_EQ(status & (XHCI_HUB_PORT_LOW_SPEED | XHCI_HUB_PORT_HIGH_SPEED),
             XHCI_HUB_PORT_HIGH_SPEED, "a Low Speed device is reported as HS");
    shadow.Speed = XHCI_SPEED_FULL;
    XhciPortShadowReport(&shadow, &status, &change);
    CHECK_EQ(status & (XHCI_HUB_PORT_LOW_SPEED | XHCI_HUB_PORT_HIGH_SPEED),
             XHCI_HUB_PORT_HIGH_SPEED, "and so is a Full Speed one");
    shadow.Speed = XHCI_SPEED_UNKNOWN;
    XhciPortShadowReport(&shadow, &status, &change);
    CHECK_EQ(status & (XHCI_HUB_PORT_LOW_SPEED | XHCI_HUB_PORT_HIGH_SPEED),
             XHCI_HUB_PORT_HIGH_SPEED, "and an undecodable one");
    shadow.Speed = XHCI_SPEED_SUPER;
    XhciPortShadowReport(&shadow, &status, &change);
    CHECK_EQ(status & (XHCI_HUB_PORT_LOW_SPEED | XHCI_HUB_PORT_HIGH_SPEED),
             XHCI_HUB_PORT_HIGH_SPEED,
             "a SuperSpeed port should not be managed, but still reports HS");

    /*
     * The truth survives the untruth: the report must not write back through
     * the shadow, because Speed is what the slot context gets programmed from.
     */
    CHECK_EQ(shadow.Speed, XHCI_SPEED_SUPER,
             "reporting does not overwrite the decoded speed");

    /*
     * The override is gated on a connection - the speed bits mean nothing
     * without one, and an unpowered empty port claiming High Speed would be a
     * new untruth rather than the one that was argued for.
     */
    {
        XHCI_PORT_SHADOW empty;
        empty.Portsc = XHCI_PORTSC_PP;
        empty.Changes = 0;
        empty.Speed = XHCI_SPEED_UNKNOWN;
        XhciPortShadowReport(&empty, &status, &change);
        CHECK_EQ(status & (XHCI_HUB_PORT_LOW_SPEED | XHCI_HUB_PORT_HIGH_SPEED),
                 0, "a port with nothing attached reports no speed at all");
        CHECK_EQ(status, XHCI_HUB_PORT_POWER,
                 "and nothing but power");
    }

    /* A disconnected port forgets the speed of the device that left. */
    shadow.Portsc = XHCI_PORTSC_PP;
    shadow.Speed = XHCI_SPEED_HIGH;
    {
        ULONG ack;
        (VOID)XhciPortShadowUpdate(&shadow, XHCI_PORTSC_PP, XHCI_SPEED_HIGH,
                                   &ack);
    }
    CHECK_EQ(shadow.Speed, XHCI_SPEED_UNKNOWN,
             "an unconnected port has no speed");

    /* The change field never carries anything outside the five bits usbport's
     * scan reads - anything else is invisible to it anyway. */
    shadow.Changes = 0xFF;
    XhciPortShadowReport(&shadow, &status, &change);
    CHECK_EQ(change & ~XHCI_HUB_C_PORT_MASK, 0,
             "the reported change set is confined to the scanned mask");

    /* NULL is answered, not dereferenced. */
    status = 0xDEAD;
    change = 0xBEEF;
    XhciPortShadowReport(NULL, &status, &change);
    CHECK_EQ(status, 0, "a NULL shadow reports no status");
    CHECK_EQ(change, 0, "and no change");
}

/* ------------------------------------------------------------------ */
/* 9. The armed operations (roadmap Phase 5 task 4)                     */
/* ------------------------------------------------------------------ */

/*
 * Arming, claiming and disarming - the half of an asynchronous port operation
 * that has no hardware in it, and the half that decides whether an uncancellable
 * timer is safe. Reset and resume share exactly one copy of it; what they do not
 * share is the completion rule, which lives at the call sites.
 */
static void test_shadow_armed_operations(void)
{
    XHCI_PORT_SHADOW shadow;
    ULONG first;
    ULONG second;

    shadow.Portsc = 0;
    shadow.Generation = 0;
    shadow.Changes = 0;
    shadow.Speed = XHCI_SPEED_UNKNOWN;
    shadow.Armed = XHCI_PORT_OP_NONE;
    shadow.ArmPending = 0;
    shadow.AgeArmed = 0;
    shadow.AgeStamp = 0;

    first = XhciPortShadowArm(&shadow, XHCI_PORT_OP_RESET);
    CHECK(first != 0, "arming a reset issues a generation");
    CHECK_EQ(shadow.Armed, XHCI_PORT_OP_RESET, "and marks the port armed");

    /* One operation per port. Two timers believing they own one port is the
     * state this exclusion exists to prevent, and a resume issued onto a port
     * mid-reset would write PLS into a register the controller is still acting
     * on. */
    CHECK_EQ(XhciPortShadowArm(&shadow, XHCI_PORT_OP_RESUME), 0,
             "a second operation on an armed port is refused");
    CHECK_EQ(shadow.Armed, XHCI_PORT_OP_RESET, "leaving the first in place");

    /* The claim is what makes a stale callback a comparison rather than a race:
     * the wrong operation and the wrong generation both answer no. */
    CHECK_EQ(XhciPortShadowClaim(&shadow, XHCI_PORT_OP_RESUME, first), 0,
             "a claim naming the wrong operation fails");
    CHECK_EQ(XhciPortShadowClaim(&shadow, XHCI_PORT_OP_RESET, first + 1000UL), 0,
             "and so does one naming a generation that was never issued");
    CHECK_EQ(XhciPortShadowClaim(&shadow, XHCI_PORT_OP_RESET, 0), 0,
             "generation 0 is never live - a zeroed shadow matches no timer");
    CHECK_EQ(shadow.Armed, XHCI_PORT_OP_RESET, "none of which disarmed it");

    CHECK_EQ(XhciPortShadowClaim(&shadow, XHCI_PORT_OP_RESET, first), 1,
             "the matching claim succeeds");
    CHECK_EQ(shadow.Armed, XHCI_PORT_OP_NONE, "and disarms the port");
    CHECK_EQ(XhciPortShadowClaim(&shadow, XHCI_PORT_OP_RESET, first), 0,
             "a second claim of the same generation finds nothing");

    /* Generations are never reused, so a callback armed before a claim cannot
     * match the operation that follows it. */
    second = XhciPortShadowArm(&shadow, XHCI_PORT_OP_RESUME);
    CHECK(second != 0 && second != first,
          "the next operation gets a generation of its own");
    CHECK_EQ(XhciPortShadowClaim(&shadow, XHCI_PORT_OP_RESUME, first), 0,
             "so the previous operation's timer claims nothing");

    /*
     * Disarming is the lifecycle's half: a stop, a suspend or a reinitialization
     * makes every outstanding callback stale at once. **The generation moves
     * even when nothing was armed**, because a timer armed a moment ago against
     * a port that has since been claimed and re-armed would otherwise match
     * again.
     */
    CHECK_EQ(XhciPortShadowDisarm(&shadow), 1, "disarming reports what it took");
    CHECK_EQ(shadow.Armed, XHCI_PORT_OP_NONE, "and leaves nothing armed");
    CHECK_EQ(XhciPortShadowClaim(&shadow, XHCI_PORT_OP_RESUME, second), 0,
             "the retired operation's timer claims nothing");
    first = shadow.Generation;
    CHECK_EQ(XhciPortShadowDisarm(&shadow), 0,
             "disarming an idle port reports nothing taken");
    CHECK(shadow.Generation != first, "and still moves the generation");

    /* The one change nothing in PORTSC produces: the reset watchdog's report
     * that a reset ended without one. */
    shadow.Changes = 0;
    XhciPortShadowLatchChange(&shadow, XHCI_HUB_C_PORT_RESET);
    CHECK_EQ(shadow.Changes, XHCI_HUB_C_PORT_RESET,
             "a change can be latched without a register bit behind it");
    XhciPortShadowLatchChange(&shadow, 0xFFFFUL);
    CHECK_EQ(shadow.Changes & ~XHCI_HUB_C_PORT_MASK, 0,
             "and is confined to the mask usbport's scan reads");

    /*
     * The age, which exists for the one failure the timer service cannot report:
     * `UsbPortRequestAsyncCallback` answers 0 whether it armed a callback or
     * failed to allocate one, so a port can be armed with nothing ever scheduled
     * to disarm it - and only a callback disarms a port.
     *
     * **Driven against a model clock rather than against a count of calls**
     * since task 13-R.3.5. The two arguments are a `PollClockMs` reading and a
     * budget in milliseconds, so the vectors below step the reading by hand -
     * which is the point of the change: nothing here can be satisfied by calling
     * the function more often, and a caller polling a thousand times a second
     * gets the same 300 ms as one polling twice.
     */
    shadow.Armed = XHCI_PORT_OP_NONE;
    shadow.AgeArmed = 1;
    shadow.AgeStamp = 7;
    CHECK_EQ(XhciPortShadowAge(&shadow, 1000, 300), 0,
             "an idle port never ages out");
    CHECK_EQ(shadow.AgeArmed, 0, "and its age is disarmed");

    CHECK(XhciPortShadowArm(&shadow, XHCI_PORT_OP_RESET) != 0, "(armed)");
    CHECK_EQ(shadow.AgeArmed, 0,
             "arming disarms the age, which is what lets the stamp stand alone "
             "with no copy of the generation beside it");
    CHECK_EQ(XhciPortShadowAge(&shadow, 1000, 300), 0,
             "the first poll takes the stamp rather than answering");
    CHECK_EQ(shadow.AgeStamp, 1000, "which is the clock reading it was given");
    CHECK_EQ(XhciPortShadowAge(&shadow, 1299, 300), 0,
             "one millisecond short of the budget is not a timeout");
    CHECK_EQ(XhciPortShadowAge(&shadow, 1300, 300), 1,
             "and the budget itself crosses it");
    CHECK_EQ(XhciPortShadowAge(&shadow, 5000, 300), 0,
             "only the crossing answers - one lost timer is one retirement, "
             "not one per poll from then on - and a clock that stepped far past "
             "the threshold rather than landing on it does not change that");

    /*
     * **Polling faster does not shorten the budget**, which is the whole defect
     * task 13-R.3.5 repaired: as sixteen *polls* this was 0.6-1.3 s on a
     * machine that polls at 36-80 ms, against a reset that is allowed 500
     * (XHCI_PORT_RESET_TIMEOUT_MS) - a margin of 1.2-2.6x where 16x was
     * intended.
     */
    (VOID)XhciPortShadowDisarm(&shadow);
    CHECK(XhciPortShadowArm(&shadow, XHCI_PORT_OP_RESET) != 0, "(re-armed)");
    {
        ULONG tick;

        for (tick = 0; tick < 500UL; tick++) {
            CHECK_EQ(XhciPortShadowAge(&shadow, 20000UL + tick, 8000UL), 0,
                     "five hundred polls inside one 500 ms reset deadline retire "
                     "nothing");
        }
    }

    /*
     * A second operation starts its own age rather than inheriting the first's -
     * the defect the command engine needed a generation copy to avoid, and this
     * one avoids by construction.
     *
     * **The whole second cycle has to be walked**, and that is measured rather
     * than tidy: asserting only that the first poll after re-arming answers 0
     * passes even when the stamp is inherited, because an inherited stamp is
     * already *past* the budget and would answer on the very next poll. The
     * re-arm therefore has to be shown taking a fresh stamp and spending a
     * fresh budget.
     */
    (VOID)XhciPortShadowDisarm(&shadow);
    CHECK(XhciPortShadowArm(&shadow, XHCI_PORT_OP_RESUME) != 0, "(re-armed)");
    CHECK_EQ(XhciPortShadowAge(&shadow, 90000UL, 300), 0,
             "the next operation does not inherit the previous one's age");
    CHECK_EQ(XhciPortShadowAge(&shadow, 90299UL, 300), 0,
             "(nor any part of its budget)");
    CHECK_EQ(XhciPortShadowAge(&shadow, 90300UL, 300), 1,
             "and gets a full budget of its own, measured from its own arming");

    /*
     * The clock is 32 bits and wraps after about 49 days of a controller that
     * has stayed up, so the elapsed test is an unsigned difference. A `<`
     * between two absolutes would make every threshold unreachable for a whole
     * lap after the wrap - which is the same class of defect as the one this
     * task repaired, reached from the other side.
     */
    (VOID)XhciPortShadowDisarm(&shadow);
    CHECK(XhciPortShadowArm(&shadow, XHCI_PORT_OP_RESET) != 0, "(re-armed)");
    CHECK_EQ(XhciPortShadowAge(&shadow, 0xFFFFFF00UL, 300), 0, "(stamped just "
             "below the wrap)");
    CHECK_EQ(XhciPortShadowAge(&shadow, 0x0000002BUL, 300), 0,
             "an age that spans the clock wrap is measured, not skipped");
    CHECK_EQ(XhciPortShadowAge(&shadow, 0x0000002CUL, 300), 1,
             "and crosses exactly where it should");

    CHECK_EQ(XhciPortShadowAge(NULL, 1000, 300), 0, "NULL age");
    CHECK_EQ(XhciPortShadowArm(NULL, XHCI_PORT_OP_RESET), 0, "NULL shadow");
    CHECK_EQ(XhciPortShadowArm(&shadow, XHCI_PORT_OP_NONE), 0,
             "and arming 'nothing' is not an operation");
    CHECK_EQ(XhciPortShadowClaim(NULL, XHCI_PORT_OP_RESET, 1), 0, "NULL claim");
    CHECK_EQ(XhciPortShadowDisarm(NULL), 0, "NULL disarm");
    XhciPortShadowLatchChange(NULL, XHCI_HUB_C_PORT_RESET);
}

/*
 * **The one field a rebuild carries across**, and the reason it is not obvious:
 * a resume rebuilds the root hub without getting a new start epoch, because
 * usbport zeroes the miniport extension before StartController and not before
 * ResumeController. If the per-port generations restarted at 0 here, the first
 * operation armed after a resume would carry the same epoch, hub port and
 * generation as one armed just before the suspend - and an uncancellable timer
 * from before would claim it.
 */
static void test_root_hub_rebuild_generations(void)
{
    ULONG before;
    ULONG armed;

    map_reset(4);
    map.Class[0] = XHCI_PORT_CLASS_USB2_ONLY;
    map.Class[1] = XHCI_PORT_CLASS_USB2_ONLY;
    map.Class[2] = XHCI_PORT_CLASS_USB2_ONLY;
    map.Class[3] = XHCI_PORT_CLASS_USB2_ONLY;

    CHECK_EQ(XhciRootHubBuild(&map, &rh), XHCI_RH_OK, "(a built root hub)");
    armed = XhciPortShadowArm(&rh.Ports[0], XHCI_PORT_OP_RESET);
    CHECK(armed != 0, "(with an operation armed on hub port 1)");
    before = rh.Ports[0].Generation;

    CHECK_EQ(XhciRootHubBuild(&map, &rh), XHCI_RH_OK, "a rebuild succeeds");
    CHECK_EQ(rh.Ports[0].Portsc, 0, "clearing the shadow's reading");
    CHECK_EQ(rh.Ports[0].Armed, XHCI_PORT_OP_NONE, "and its armed state");
    CHECK(rh.Ports[0].Generation != before,
          "but advancing the generation rather than restarting it");
    CHECK_EQ(XhciPortShadowClaim(&rh.Ports[0], XHCI_PORT_OP_RESET, armed), 0,
             "so a timer armed before the rebuild claims nothing after it");

    /* And the operation armed after the rebuild is not the pre-rebuild one
     * either, which is the case a plain reset-to-zero would have produced. */
    CHECK(XhciPortShadowArm(&rh.Ports[0], XHCI_PORT_OP_RESET) != armed,
          "nor does the next operation reuse its generation");
}

/*
 * **The disown debt is per-tenancy, so a rebuild must not carry it** (repo
 * audit B1). It was the one pending-operation field the rebuild did
 * not clear, and the sequence that reaches it is ordinary on Win98: a port is
 * disabled or powered off, the target bit lags (footnote 91), the ~0.5 s idle
 * suspend arrives before the next 500 ms health poll, and the resume's
 * XhciRootHubInit rebuilds the shadow. A surviving obligation is then confirmed
 * against the *new* run's PORTSC - a re-powered unconnected or mid-reset port
 * reads PED = 0 - and fires XhciSlotPortDisabled at whatever record the new run
 * holds on that port, which on the restore-success path is a live device.
 *
 * Asserted beside the generation rather than in its own function because the
 * property is the same one: what a rebuild keeps and what it must not.
 */
static void test_root_hub_rebuild_disown_debt(void)
{
    map_reset(4);
    map.Class[0] = XHCI_PORT_CLASS_USB2_ONLY;
    map.Class[1] = XHCI_PORT_CLASS_USB2_ONLY;

    CHECK_EQ(XhciRootHubBuild(&map, &rh), XHCI_RH_OK, "(a built root hub)");
    rh.Ports[0].DisownPending = 1;
    rh.Ports[0].DisownWantsPp = 1;
    rh.Ports[1].DisownPending = 1;
    rh.Ports[1].DisownWantsPp = 0;

    CHECK_EQ(XhciRootHubBuild(&map, &rh), XHCI_RH_OK, "a rebuild succeeds");
    CHECK_EQ(rh.Ports[0].DisownPending, 0,
             "and clears a standing disown obligation");
    CHECK_EQ(rh.Ports[0].DisownWantsPp, 0, "along with the bit it named");
    CHECK_EQ(rh.Ports[1].DisownPending, 0, "on every port, not just the first");
    CHECK_EQ(rh.Ports[1].DisownWantsPp, 0, NULL);
}

int main(void)
{
    test_bit_positions();
    test_neutral();
    test_set();
    test_clear_changes();
    test_link_state();
    test_operations();
    test_root_hub_map();
    test_root_hub_rebuild_disown_debt();
    test_shadow_latch_and_ack();
    test_shadow_change_bits();
    test_shadow_suspend_resume();
    test_shadow_report_translation();
    test_shadow_armed_operations();
    test_root_hub_rebuild_generations();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
