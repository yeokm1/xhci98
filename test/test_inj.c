/*
 * test_inj.c - host vectors for 35-T.9's fault injection, the pure half
 * (src\xhci_inj.c; the qemu flavour's alone in the driver).
 *
 * The trigger's encoding and its sequence rule, the port a PED or an
 * over-current may be aimed at, and every register answer the layer gives:
 * PEC until its acknowledgement, the over-current's PP, OCA and OCC through
 * the release and the repower, the PP the real port is given, all-ones
 * USBSTS spent read by read, and the proof's read-back. The endpoint
 * faults' decisions (35-T.2 to 35-T.4): the target pipe, the injected
 * Transfer Event, the swallowed Stopped event, the dropped doorbell, the
 * answered EP State, every command against the answered endpoint and each
 * hold, where a stop left the TD, and the EP0 faults' firing.
 */

#include <stdio.h>
#include "../src/xhci.h"
#include "../src/xhci_inj.h"
#include "../src/xhci_pipe.h"
#include "test_harness.h"

#define CCS XHCI_PORTSC_CCS
#define PED XHCI_PORTSC_PED
#define PP  XHCI_PORTSC_PP
#define OCA XHCI_PORTSC_OCA
#define OCC XHCI_PORTSC_OCC
#define PEC XHCI_PORTSC_PEC
#define CSC XHCI_PORTSC_CSC

static void test_encoding(void)
{
    ULONG v;

    v = XHCI_INJ_VALUE(0x12, XHCI_INJ_OC, 3, 0x45);
    CHECK_EQ(v, 0x12030345UL, "seq 31:24, fault 23:16, port 15:8, arg 7:0");
    CHECK_EQ(XHCI_INJ_GET_SEQ(v), 0x12, "seq");
    CHECK_EQ(XHCI_INJ_GET_FAULT(v), XHCI_INJ_OC, "fault");
    CHECK_EQ(XHCI_INJ_GET_PORT(v), 3, "port");
    CHECK_EQ(XHCI_INJ_GET_ARG(v), 0x45, "arg");
    CHECK_EQ(XHCI_INJ_VALUE(0x1FF, 0x100, 0x101, 0x102), 0xFF000102UL,
             "every field held to its byte");
}

static void test_trigger(void)
{
    XHCI_INJ_TRIGGER t;
    ULONG f;

    /* A value left in the key at a start is latched, not fired. */
    XhciInjTriggerStart(&t, 1, XHCI_INJ_VALUE(7, XHCI_INJ_HCH, 0, 0));
    CHECK_EQ(XhciInjTake(&t, 1, XHCI_INJ_VALUE(7, XHCI_INJ_HCH, 0, 0)),
             XHCI_INJ_TAKE_NONE, "the start's value does not fire");
    CHECK_EQ(XhciInjTake(&t, 1, XHCI_INJ_VALUE(7, XHCI_INJ_PED, 0, 0)),
             XHCI_INJ_TAKE_NONE, "the same sequence never fires");
    CHECK_EQ(XhciInjTake(&t, 1, XHCI_INJ_VALUE(8, XHCI_INJ_HCH, 0, 0)),
             XHCI_INJ_TAKE_FIRE, "a new sequence fires");
    CHECK_EQ(XhciInjTake(&t, 1, XHCI_INJ_VALUE(8, XHCI_INJ_HCH, 0, 0)),
             XHCI_INJ_TAKE_NONE, "once");
    CHECK_EQ(XhciInjTake(&t, 0, XHCI_INJ_VALUE(9, XHCI_INJ_HCH, 0, 0)),
             XHCI_INJ_TAKE_NONE, "an absent value never fires");
    CHECK_EQ(XhciInjTake(&t, 1, XHCI_INJ_VALUE(7, XHCI_INJ_HCH, 0, 0)),
             XHCI_INJ_TAKE_FIRE, "any change fires, back to 7 included");

    /* No value at the start: the first one seen fires, sequence 0 too. */
    XhciInjTriggerStart(&t, 0, 0x12345678UL);
    CHECK_EQ(XhciInjTake(&t, 1, XHCI_INJ_VALUE(0, XHCI_INJ_LOST_IRQ, 0, 1)),
             XHCI_INJ_TAKE_FIRE, "first value after an absent one");

    /* Every code: built (01 to 12 hex, FF) or unknown. A refused one is
     * consumed. */
    for (f = 0; f < 256; f++) {
        ULONG want;
        char what[64];

        if ((f >= 1 && f <= 0x12) || f == XHCI_INJ_CLEAR) {
            want = XHCI_INJ_TAKE_FIRE;
        } else {
            want = XHCI_INJ_TAKE_UNKNOWN;
        }
        XhciInjTriggerStart(&t, 0, 0);
        sprintf(what, "fault code %lu", f);
        CHECK_EQ(XhciInjTake(&t, 1, XHCI_INJ_VALUE(1, f, 0, 0)), want, what);
        CHECK_EQ(XhciInjTake(&t, 1, XHCI_INJ_VALUE(1, f, 0, 0)),
                 XHCI_INJ_TAKE_NONE, "consumed whatever its verdict");
    }
    CHECK_EQ(XHCI_INJ_TRANSACTION, XHCI_INJ_DEAD_NOPROOF + 1,
             "the reserved codes follow the built ones");
}

static void test_lost_count(void)
{
    CHECK_EQ(XhciInjLostCount(0), 1, "0 is one interrupt");
    CHECK_EQ(XhciInjLostCount(1), 1, "1");
    CHECK_EQ(XhciInjLostCount(254), 254, "254");
    CHECK_EQ(XhciInjLostCount(255), 0xFFFFFFFFUL, "255 until CLEAR");
}

/* QEMU's shape: USB 2.0 on ports 1-4, USB 3.0 on 5-8. */
static void qemu_map(XHCI_PORT_MAP *map)
{
    ULONG i;
    UCHAR *b;

    b = (UCHAR *)map;
    for (i = 0; i < sizeof(*map); i++) {
        b[i] = 0;
    }
    map->PortCount = 8;
    map->ProtocolCount = 2;
    map->Protocols[0].Major = 2;
    map->Protocols[0].PortOffset = 1;
    map->Protocols[0].PortCount = 4;
    map->Protocols[1].Major = 3;
    map->Protocols[1].PortOffset = 5;
    map->Protocols[1].PortCount = 4;
    for (i = 0; i < 4; i++) {
        map->Protocol[i] = 0;
        map->Class[i] = XHCI_PORT_CLASS_USB2_COMPANION;
        map->Protocol[i + 4] = 1;
        map->Class[i + 4] = XHCI_PORT_CLASS_USB3_COMPANION;
    }
}

static void test_port_fits(void)
{
    XHCI_PORT_MAP map;
    ULONG live;

    qemu_map(&map);
    live = CCS | PED | PP;
    CHECK_EQ(XhciInjPortFits(&map, 1, 0, live), 1, "USB 2.0, enabled");
    CHECK_EQ(XhciInjPortFits(&map, 2, 2, live), 1, "the port asked for");
    CHECK_EQ(XhciInjPortFits(&map, 1, 2, live), 0, "not the port asked for");
    CHECK_EQ(XhciInjPortFits(&map, 5, 0, live), 0, "never a USB 3 port");
    CHECK_EQ(XhciInjPortFits(&map, 9, 0, live), 0, "not a port");
    CHECK_EQ(XhciInjPortFits(&map, 0, 0, live), 0, "port 0");
    CHECK_EQ(XhciInjPortFits(&map, 1, 0, CCS | PP), 0, "not enabled");
    CHECK_EQ(XhciInjPortFits(&map, 1, 0, PED | PP), 0, "nothing connected");
    CHECK_EQ(XhciInjPortFits(&map, 1, 0, CCS | PED), 0, "unpowered");
    CHECK_EQ(XhciInjPortFits(&map, 1, 0, 0xFFFFFFFFUL), 0, "unreadable");
}

static void test_pec(void)
{
    XHCI_INJ_REGS r;
    ULONG raw;

    XhciInjRegsClear(&r);
    raw = CCS | PP;                 /* after the real PED write */
    CHECK_EQ(XhciInjPortscRead(&r, 2, raw), raw, "nothing armed");
    XhciInjArmPec(&r, 2);
    CHECK_EQ(XhciInjPortscRead(&r, 2, raw), raw | PEC, "PEC answered");
    CHECK_EQ(XhciInjPortscRead(&r, 1, raw), raw, "on that port only");
    CHECK_EQ(XhciInjPortscRead(&r, 2, 0xFFFFFFFFUL), 0xFFFFFFFFUL,
             "unreadable passes");
    /* A write without PEC is no acknowledgement. */
    CHECK_EQ(XhciInjPortscWrite(&r, 2, PP | CSC), PP | CSC,
             "the write passes unchanged");
    CHECK_EQ(XhciInjPortscRead(&r, 2, raw), raw | PEC, "still answered");
    CHECK_EQ(XhciInjPortscWrite(&r, 1, PP | PEC), PP | PEC, "other port");
    CHECK_EQ(XhciInjPortscRead(&r, 2, raw), raw | PEC, "still answered");
    CHECK_EQ(XhciInjPortscWrite(&r, 2, PP | PEC), PP | PEC,
             "the acknowledgement passes too");
    CHECK_EQ(XhciInjPortscRead(&r, 2, raw), raw, "and ends it");
    CHECK_EQ(r.PedPort, 0, "disarmed");
}

static void test_oc(void)
{
    XHCI_INJ_REGS r;
    ULONG raw;

    XhciInjRegsClear(&r);
    raw = CCS | PED | PP;
    XhciInjArmOc(&r, 3);
    CHECK_EQ(XhciInjPortscRead(&r, 3, raw), CCS | PED | OCA | OCC,
             "PP clear, OCA and OCC set");
    CHECK_EQ(XhciInjPortscRead(&r, 4, raw), raw, "on that port only");
    /* The driver's OCC acknowledgement; PP never leaves the real port. */
    CHECK_EQ(XhciInjPortscWrite(&r, 3, OCC), OCC | PP,
             "the real port keeps PP");
    CHECK_EQ(XhciInjPortscRead(&r, 3, raw), CCS | PED | OCA, "OCC taken");
    /* An unpower write while held: recorded nowhere, PP kept for real. */
    CHECK_EQ(XhciInjPortscWrite(&r, 3, 0), PP, "PP 0 does not reach it");
    /* A PP write before the release is recorded, the emulation stays. */
    CHECK_EQ(XhciInjPortscWrite(&r, 3, PP), PP, "PP while held");
    CHECK_EQ(r.OcPpWrites, 1, "recorded");
    CHECK_EQ(XhciInjPortscRead(&r, 3, raw), CCS | PED | OCA, "still held");
    /* The release: OCA clear, PP still answered clear. */
    XhciInjReleaseOc(&r);
    CHECK_EQ(XhciInjPortscRead(&r, 3, raw), CCS | PED, "OCA clear, PP clear");
    /* The driver's repower ends the emulation. */
    CHECK_EQ(XhciInjPortscWrite(&r, 3, PP), PP, "the repower");
    CHECK_EQ(r.OcPpWrites, 2, "recorded");
    CHECK_EQ(XhciInjPortscRead(&r, 3, raw), raw, "the real port from now on");
    CHECK_EQ(XhciInjPortscWrite(&r, 3, 0), 0, "writes pass unchanged");
    CHECK_EQ(r.OcActive, 0, "inactive");

    /* An OCC still pending at the repower goes with it. */
    XhciInjArmOc(&r, 1);
    XhciInjReleaseOc(&r);
    CHECK_EQ(XhciInjPortscRead(&r, 1, raw), CCS | PED | OCC, "OCC pending");
    (VOID)XhciInjPortscWrite(&r, 1, PP);
    CHECK_EQ(XhciInjPortscRead(&r, 1, raw), raw, "gone with the repower");

    /* Persistent: never released, every read held. */
    XhciInjArmOc(&r, 2);
    (VOID)XhciInjPortscWrite(&r, 2, OCC);
    (VOID)XhciInjPortscWrite(&r, 2, PP);
    CHECK_EQ(XhciInjPortscRead(&r, 2, raw), CCS | PED | OCA, "held");
    XhciInjRegsClear(&r);
    CHECK_EQ(XhciInjPortscRead(&r, 2, raw), raw, "CLEAR ends it");
    CHECK_EQ(XhciInjPortscWrite(&r, 0, OCC), OCC, "port 0 untouched");
}

/* One thread pass: the snapshot taken first (HcdInjPoll), then the health
 * poll's read when it runs (not on a failed controller) and the containment
 * step's at XhciTolerance 1. */
static void dead_pass(PXHCI_INJ_REGS r, ULONG tolerance, ULONG healthRuns,
                      ULONG *health, ULONG *contain)
{
    (VOID)XhciInjDeadPass(r);
    *health = healthRuns ? XhciInjUsbsts(r, 0x18UL) : 0x18UL;
    *contain = tolerance ? XhciInjUsbsts(r, 0x18UL) : 0x18UL;
}

static void test_dead(void)
{
    XHCI_INJ_REGS r;
    ULONG tol;
    ULONG pass;
    ULONG health;
    ULONG contain;
    char what[80];

    XhciInjRegsClear(&r);
    CHECK_EQ(XhciInjDeadPass(&r), 0, "no pass active unarmed");
    CHECK_EQ(XhciInjUsbsts(&r, 0x18UL), 0x18UL, "not armed");

    /* N passes to both readers, at tolerance 0 and 1. */
    for (tol = 0; tol <= 1; tol++) {
        XhciInjArmDead(&r, 3, 0);
        CHECK_EQ(XhciInjUsbsts(&r, 0x18UL), 0x18UL, "armed, not yet a pass");
        for (pass = 1; pass <= 4; pass++) {
            dead_pass(&r, tol, 1, &health, &contain);
            sprintf(what, "tolerance %lu pass %lu, health poll", tol, pass);
            CHECK_EQ(health, pass <= 3 ? 0xFFFFFFFFUL : 0x18UL, what);
            sprintf(what, "tolerance %lu pass %lu, containment", tol, pass);
            CHECK_EQ(contain, (tol && pass <= 3) ? 0xFFFFFFFFUL : 0x18UL, what);
        }
        CHECK_EQ(r.DeadLeft, 0, "spent");
    }

    /* Skipped health reads (a failed controller's poll reads nothing): the
     * pass still spends, and the containment step sees the fault end. */
    XhciInjArmDead(&r, 2, 1);
    for (pass = 1; pass <= 5; pass++) {
        dead_pass(&r, 1, 0, &health, &contain);
        sprintf(what, "no health read, pass %lu, containment", pass);
        CHECK_EQ(contain, pass <= 2 ? 0xFFFFFFFFUL : 0x18UL, what);
    }
    CHECK_EQ(r.DeadLeft, 0, "spent with no health read at all");
    /* Reads alone never spend it. */
    XhciInjArmDead(&r, 1, 0);
    (VOID)XhciInjDeadPass(&r);
    for (pass = 0; pass < 10; pass++) {
        CHECK_EQ(XhciInjUsbsts(&r, 0x18UL), 0xFFFFFFFFUL, "reads in one pass");
    }
    CHECK_EQ(XhciInjDeadPass(&r), 0, "the next pass ends it");
    CHECK_EQ(XhciInjUsbsts(&r, 0x18UL), 0x18UL, "real again");

    /* The controller fails during an active finite injection: the health
     * reads stop from pass 2, and the fault still ends on time. */
    XhciInjArmDead(&r, 4, 1);
    for (pass = 1; pass <= 6; pass++) {
        dead_pass(&r, 1, pass < 2, &health, &contain);
        sprintf(what, "failed at pass 2, pass %lu, containment", pass);
        CHECK_EQ(contain, pass <= 4 ? 0xFFFFFFFFUL : 0x18UL, what);
    }

    /* Persistent: every pass, never spent, until CLEAR. */
    XhciInjArmDead(&r, 0, 1);
    CHECK_EQ(r.DeadLeft, XHCI_INJ_DEAD_FOREVER, "0 is until CLEAR");
    for (tol = 0; tol <= 1; tol++) {
        dead_pass(&r, tol, 1, &health, &contain);
        CHECK_EQ(health, 0xFFFFFFFFUL, "persistent, health poll");
        CHECK_EQ(contain, tol ? 0xFFFFFFFFUL : 0x18UL,
                 "persistent, containment where it runs");
    }
    CHECK_EQ(r.DeadLeft, XHCI_INJ_DEAD_FOREVER, "never spent");
    CHECK_EQ(XhciInjPciCommand(&r, 0x0002UL), 0x0006UL, "BME reads set");
    CHECK_EQ(XhciInjPciCommand(&r, 0x0006UL), 0x0006UL, "set stays set");
    CHECK_EQ(XhciInjPciCommand(&r, 0xFFFFUL), 0xFFFFUL,
             "a function that does not answer is left so");
    CHECK_EQ(XHCI_INJ_PCI_BME, 0x0004UL, "PCI Command bit 2");
    XhciInjRegsClear(&r);
    CHECK_EQ(XhciInjUsbsts(&r, 0x18UL), 0x18UL, "CLEAR ends it at once");
    CHECK_EQ(XhciInjDeadPass(&r), 0, "and stays ended");
    CHECK_EQ(XhciInjPciCommand(&r, 0x0002UL), 0x0002UL, "CLEAR, proof real");
}

/* Which commands a failed controller refuses. */
static void test_needs_live(void)
{
    ULONG f;
    ULONG want;
    char what[64];

    for (f = 0; f < 256; f++) {
        want = (f == XHCI_INJ_LOST_IRQ || f == XHCI_INJ_PED ||
                f == XHCI_INJ_OC || f == XHCI_INJ_HCH ||
                f == XHCI_INJ_DEAD || f == XHCI_INJ_DEAD_NOPROOF ||
                (f >= 0x08 && f <= 0x12)) ? 1UL : 0UL;
        sprintf(what, "fault %lu needs a live controller", f);
        CHECK_EQ(XhciInjNeedsLive(f), want, what);
    }
    CHECK_EQ(XhciInjNeedsLive(XHCI_INJ_CLEAR), 0, "CLEAR always taken");
    CHECK_EQ(XhciInjNeedsLive(XHCI_INJ_OC_RELEASE), 0, "the release too");
}
/* The lost-interrupt window: the ISR counts `taken` only while armed. */
static void test_irq_window(void)
{
    ULONG n;
    ULONG wrap[4];
    ULONG i;

    /* Inactive: nothing drops, whatever the counts - the review's case of
     * a count at 0x80000001 included. */
    wrap[0] = 0;
    wrap[1] = 1;
    wrap[2] = 0x7FFFFFFFUL;
    wrap[3] = 0x80000001UL;
    for (i = 0; i < 4; i++) {
        CHECK_EQ(XhciInjIrqDrop(0, 0, wrap[i], 0), 0, "disarmed");
        CHECK_EQ(XhciInjIrqDrop(0, 0, wrap[i], 5), 0, "disarmed, budget");
        CHECK_EQ(XhciInjIrqDrop(0, 1, wrap[i], 0), 0, "disarmed, forever");
        CHECK_EQ(XhciInjIrqSpent(0, 0, wrap[i], 5), 0, "nothing to disarm");
    }

    /* Armed for 3: the first three claimed are dropped, then exhaustion. */
    for (n = 1; n <= 6; n++) {
        CHECK_EQ(XhciInjIrqDrop(1, 0, n, 3), n <= 3 ? 1UL : 0UL,
                 "budget 3");
        CHECK_EQ(XhciInjIrqSpent(1, 0, n, 3), n >= 3 ? 1UL : 0UL,
                 "spent from the third");
    }
    CHECK_EQ(XhciInjIrqDrop(1, 0, 0, 3), 0, "no count yet, no drop");
    CHECK_EQ(XhciInjIrqSpent(1, 0, 2, 3), 0, "not yet spent");

    /* Wraparound while armed: a count past 0x7FFFFFFF is past any budget. */
    CHECK_EQ(XhciInjIrqDrop(1, 0, 0x80000001UL, 254), 0, "wrapped count");
    CHECK_EQ(XhciInjIrqDrop(1, 0, 0xFFFFFFFFUL, 254), 0, "top count");
    CHECK_EQ(XhciInjIrqSpent(1, 0, 0x80000001UL, 254), 1, "and spent");

    /* Forever: every one dropped, never spent; CLEAR is disarmed. */
    CHECK_EQ(XhciInjIrqDrop(1, 1, 1, 0), 1, "forever, first");
    CHECK_EQ(XhciInjIrqDrop(1, 1, 0x80000001UL, 0), 1, "forever, wrapped");
    CHECK_EQ(XhciInjIrqSpent(1, 1, 0x80000001UL, 0), 0, "forever is never spent");
    CHECK_EQ(XhciInjIrqDrop(0, 0, 0, 0), 0, "CLEAR drops nothing");
}
/* The codes' numbers are the guest's interface (build-and-test.md). */
static void test_codes(void)
{
    CHECK_EQ(XHCI_INJ_TRANSACTION, 0x08, "08 Transaction Error");
    CHECK_EQ(XHCI_INJ_RESET_EP_FAIL, 0x09, "09 Reset Endpoint failing");
    CHECK_EQ(XHCI_INJ_REFUSED_CODE, 0x0A, "0A refused code");
    CHECK_EQ(XHCI_INJ_HALT_HALTED, 0x0B, "0B halt, Halted");
    CHECK_EQ(XHCI_INJ_HALT_ERROR, 0x0C, "0C halt, Error");
    CHECK_EQ(XHCI_INJ_HALT_STALE, 0x0D, "0D halt, stale");
    CHECK_EQ(XHCI_INJ_EP_NOT_ENABLED, 0x0E, "0E Endpoint Not Enabled");
    CHECK_EQ(XHCI_INJ_EP0_THREAD, 0x0F, "0F EP0, the thread's transfer");
    CHECK_EQ(XHCI_INJ_EP0_PREPDO, 0x10, "10 EP0 before the PDO");
    CHECK_EQ(XHCI_INJ_RACE_RING, 0x11, "11 race, a ring");
    CHECK_EQ(XHCI_INJ_RACE_SECOND, 0x12, "12 race, a second error");
    CHECK_EQ(XHCI_INJ_REFUSED_CC, 18, "Bandwidth Overrun");
}

static void test_kinds(void)
{
    ULONG f;
    char what[64];

    for (f = 0; f < 256; f++) {
        ULONG kind;

        switch (f) {
        case 0x08: case 0x09: case 0x11: case 0x12:
            kind = XHCI_INJ_KIND_RETRY;
            break;
        case 0x0B: case 0x0C: case 0x0D:
            kind = XHCI_INJ_KIND_HALT;
            break;
        case 0x0A: case 0x0E:
            kind = XHCI_INJ_KIND_REFUSED;
            break;
        case 0x0F: case 0x10:
            kind = XHCI_INJ_KIND_EP0;
            break;
        default:
            kind = XHCI_INJ_KIND_OTHER;
            break;
        }
        sprintf(what, "kind of fault %lu", f);
        CHECK_EQ(XhciInjKind(f), kind, what);
    }
    CHECK_EQ(XhciInjHoldOf(XHCI_INJ_TRANSACTION), XHCI_INJ_HOLD_NONE, "hold");
    CHECK_EQ(XhciInjHoldOf(XHCI_INJ_RESET_EP_FAIL), XHCI_INJ_HOLD_FAIL,
             "hold, failing");
    CHECK_EQ(XhciInjHoldOf(XHCI_INJ_RACE_RING), XHCI_INJ_HOLD_RING,
             "hold, ring");
    CHECK_EQ(XhciInjHoldOf(XHCI_INJ_RACE_SECOND), XHCI_INJ_HOLD_SECOND,
             "hold, second");
    CHECK_EQ(XhciInjHoldOf(XHCI_INJ_HALT_HALTED), XHCI_INJ_HOLD_NONE,
             "a halt holds nothing");
    CHECK_EQ(XhciInjHaltStateOf(XHCI_INJ_TRANSACTION), XHCI_EP_STATE_HALTED,
             "Transaction reads Halted");
    CHECK_EQ(XhciInjHaltStateOf(XHCI_INJ_RESET_EP_FAIL),
             XHCI_EP_STATE_HALTED, "so does the failing reset");
    CHECK_EQ(XhciInjHaltStateOf(XHCI_INJ_RACE_RING), XHCI_EP_STATE_HALTED,
             "and the races");
    CHECK_EQ(XhciInjHaltStateOf(XHCI_INJ_RACE_SECOND), XHCI_EP_STATE_HALTED,
             "both");
    CHECK_EQ(XhciInjHaltStateOf(XHCI_INJ_HALT_HALTED), XHCI_EP_STATE_HALTED,
             "halt, Halted");
    CHECK_EQ(XhciInjHaltStateOf(XHCI_INJ_HALT_ERROR), XHCI_EP_STATE_ERROR,
             "halt, Error");
    CHECK_EQ(XhciInjHaltStateOf(XHCI_INJ_HALT_STALE), 0,
             "stale: the real state");
    CHECK_EQ(XhciInjHaltStateOf(XHCI_INJ_REFUSED_CODE), 0,
             "a refused code answers nothing");
}

static void test_spend(void)
{
    ULONG left;

    left = 2;
    CHECK_EQ(XhciInjSpend(&left), 1, "first of two");
    CHECK_EQ(left, 1, "one left");
    CHECK_EQ(XhciInjSpend(&left), 1, "second of two");
    CHECK_EQ(XhciInjSpend(&left), 0, "none left");
    CHECK_EQ(left, 0, "stays 0");
    left = XhciInjLostCount(XHCI_INJ_ARG_FOREVER);
    CHECK_EQ(XhciInjSpend(&left), 1, "until CLEAR");
    CHECK_EQ(left, 0xFFFFFFFFUL, "never runs out");
    left = XhciInjLostCount(0);
    CHECK_EQ(left, 1, "arg 0 is one injection");
}

#define IN_EP 0x81UL
#define INTR  XHCI_PIPE_XFER_INTERRUPT

static void test_pipe_fits(void)
{
    CHECK_EQ(XhciInjPipeFits(0, 3, 1, 0, 0, INTR, IN_EP, 0, 0, 1, 0), 1,
             "an idle interrupt IN with a TD, any port");
    CHECK_EQ(XhciInjPipeFits(3, 3, 1, 0, 0, INTR, IN_EP, 0, 0, 1, 0), 1,
             "at its port");
    CHECK_EQ(XhciInjPipeFits(2, 3, 1, 0, 0, INTR, IN_EP, 0, 0, 1, 0), 0,
             "another port");
    CHECK_EQ(XhciInjPipeFits(0, 3, 0, 0, 0, INTR, IN_EP, 0, 0, 1, 0), 0,
             "not published");
    CHECK_EQ(XhciInjPipeFits(0, 3, 1, 1, 0, INTR, IN_EP, 0, 0, 1, 0), 0,
             "a hub's");
    CHECK_EQ(XhciInjPipeFits(0, 3, 1, 0, 1, INTR, IN_EP, 0, 0, 1, 0), 0,
             "leaving");
    CHECK_EQ(XhciInjPipeFits(0, 3, 1, 0, 0, XHCI_PIPE_XFER_BULK, IN_EP, 0, 0,
                             1, 0), 0, "bulk");
    CHECK_EQ(XhciInjPipeFits(0, 3, 1, 0, 0, XHCI_PIPE_XFER_ISOCH, IN_EP, 0,
                             0, 1, 0), 0, "isochronous");
    CHECK_EQ(XhciInjPipeFits(0, 3, 1, 0, 0, INTR, 0x02UL, 0, 0, 1, 0), 0,
             "interrupt OUT");
    CHECK_EQ(XhciInjPipeFits(0, 3, 1, 0, 0, INTR, IN_EP, 1, 0, 1, 0), 0,
             "streams");
    CHECK_EQ(XhciInjPipeFits(0, 3, 1, 0, 0, INTR, IN_EP, 0, 1, 1, 0), 0,
             "an operation owed");
    CHECK_EQ(XhciInjPipeFits(0, 3, 1, 0, 0, INTR, IN_EP, 0, 0, 0, 0), 0,
             "no TD");
    CHECK_EQ(XhciInjPipeFits(0, 3, 1, 0, 0, INTR, IN_EP, 0, 0, 1, 1), 0,
             "a retry already owed");
}

static void test_transfer_event(void)
{
    XHCI_TRB ev;

    XhciInjTransferEvent(&ev, 0x12345670UL, 8, XHCI_CC_USB_TRANSACTION_ERROR,
                         5, 3);
    CHECK_EQ(ev.Param0, 0x12345670UL, "pointer");
    CHECK_EQ(ev.Param1, 0, "high dword 0");
    CHECK_EQ(XHCI_TRB_GET_COMPLETION(ev.Status),
             XHCI_CC_USB_TRANSACTION_ERROR, "code");
    CHECK_EQ(XHCI_TRB_GET_RESIDUAL(ev.Status), 8, "residual");
    CHECK_EQ(XHCI_TRB_GET_TYPE(ev.Control), XHCI_TRB_TYPE_TRANSFER_EVENT,
             "a Transfer Event");
    CHECK_EQ(XHCI_TRB_GET_SLOT_ID(ev.Control), 5, "slot");
    CHECK_EQ(XHCI_TRB_GET_EP_ID(ev.Control), 3, "endpoint");
    CHECK_EQ(XHCI_EVENT_IS_EVENT_DATA(ev.Control), 0, "ED 0");
    XhciInjTransferEvent(&ev, 0, 0x1FFFFFFUL, XHCI_CC_EP_NOT_ENABLED, 1, 1);
    CHECK_EQ(XHCI_TRB_GET_RESIDUAL(ev.Status), 0xFFFFFFUL,
             "residual held to 24 bits");
    CHECK_EQ(XHCI_TRB_GET_COMPLETION(ev.Status), XHCI_CC_EP_NOT_ENABLED,
             "code kept");
}

static void stopped(XHCI_TRB *ev, ULONG slot, ULONG dci, ULONG cc, ULONG pa,
                    ULONG residual)
{
    XhciInjTransferEvent(ev, pa, residual, cc, slot, dci);
}

static void test_swallow_doorbell_state(void)
{
    XHCI_INJ_EP e;
    XHCI_TRB ev;

    XhciInjEpClear(&e);
    stopped(&ev, 4, 3, XHCI_CC_STOPPED, 0x1000, 8);
    CHECK_EQ(XhciInjEpSwallow(&e, &ev), 0, "nothing watched");
    CHECK_EQ(XhciInjEpDoorbell(&e, 4, 3), 0, "nothing blocked");
    CHECK_EQ(XhciInjEpState(&e, 4, 3, XHCI_EP_STATE_RUNNING),
             XHCI_EP_STATE_RUNNING, "nothing answered");

    XhciInjEpWatch(&e, 4, 3);
    stopped(&ev, 4, 2, XHCI_CC_STOPPED, 0x1000, 8);
    CHECK_EQ(XhciInjEpSwallow(&e, &ev), 0, "another endpoint's");
    stopped(&ev, 5, 3, XHCI_CC_STOPPED, 0x1000, 8);
    CHECK_EQ(XhciInjEpSwallow(&e, &ev), 0, "another slot's");
    stopped(&ev, 4, 3, XHCI_CC_SUCCESS, 0x1000, 0);
    CHECK_EQ(XhciInjEpSwallow(&e, &ev), 0, "a completion is the driver's");
    ev.Control = XHCI_TRB_TYPE(XHCI_TRB_TYPE_COMMAND_COMPLETION) |
                 XHCI_TRB_SLOT_ID(4) | XHCI_TRB_EP_ID(3);
    ev.Status = XHCI_CC_STOPPED << 24;
    CHECK_EQ(XhciInjEpSwallow(&e, &ev), 0, "a command completion");
    CHECK_EQ(e.StopSeen, 0, "nothing kept");
    stopped(&ev, 4, 3, XHCI_CC_STOPPED_LENGTH_INVALID, 0x1010, 0);
    CHECK_EQ(XhciInjEpSwallow(&e, &ev), 1, "Stopped, length invalid");
    stopped(&ev, 4, 3, XHCI_CC_STOPPED, 0x1000, 8);
    CHECK_EQ(XhciInjEpSwallow(&e, &ev), 1, "Stopped");
    CHECK_EQ(e.StopSeen, 1, "seen");
    CHECK_EQ(e.StopPA, 0x1000, "its pointer");
    CHECK_EQ(e.StopCode, XHCI_CC_STOPPED, "its code");
    CHECK_EQ(e.StopResidual, 8, "its residual");

    CHECK_EQ(XhciInjEpDoorbell(&e, 4, 3), 1, "dropped during the stop");
    CHECK_EQ(XhciInjEpDoorbell(&e, 4, 0x00050003UL), 1,
             "the target byte alone");
    CHECK_EQ(XhciInjEpDoorbell(&e, 4, 2), 0, "another endpoint rings");
    CHECK_EQ(XhciInjEpDoorbell(&e, 3, 3), 0, "another slot rings");
    CHECK_EQ(e.Dropped, 2, "counted");
    CHECK_EQ(XhciInjEpState(&e, 4, 3, XHCI_EP_STATE_STOPPED),
             XHCI_EP_STATE_STOPPED, "the stop window reads real");

    e.Watch = 0;
    e.State = XHCI_EP_STATE_HALTED;
    CHECK_EQ(XhciInjEpSwallow(&e, &ev), 0, "the watch is over");
    CHECK_EQ(XhciInjEpState(&e, 4, 3, XHCI_EP_STATE_STOPPED),
             XHCI_EP_STATE_HALTED, "Halted answered");
    CHECK_EQ(XhciInjEpState(&e, 4, 2, XHCI_EP_STATE_RUNNING),
             XHCI_EP_STATE_RUNNING, "not another endpoint");
    CHECK_EQ(XhciInjEpState(&e, 5, 3, XHCI_EP_STATE_RUNNING),
             XHCI_EP_STATE_RUNNING, "not another slot");
    e.State = XHCI_EP_STATE_ERROR;
    CHECK_EQ(XhciInjEpState(&e, 4, 3, XHCI_EP_STATE_STOPPED),
             XHCI_EP_STATE_ERROR, "Error answered");

    e.EvPending = 1;
    e.Ep0Want = XHCI_INJ_EP0_THREAD;
    e.Abandoned = 2;
    XhciInjEpEnd(&e);
    CHECK_EQ(e.Slot, 0, "ended");
    CHECK_EQ(XhciInjEpState(&e, 4, 3, XHCI_EP_STATE_STOPPED),
             XHCI_EP_STATE_STOPPED, "the real state again");
    CHECK_EQ(XhciInjEpDoorbell(&e, 4, 3), 0, "the doorbell reaches it");
    CHECK_EQ(e.EvPending, 1, "an event waiting is not the answers'");
    CHECK_EQ(e.Ep0Want, XHCI_INJ_EP0_THREAD, "nor the EP0 arming");
    CHECK_EQ(e.Abandoned, 2, "nor the counts");
    XhciInjEpClear(&e);
    CHECK_EQ(e.EvPending + e.Ep0Want + e.Abandoned + e.Dropped, 0,
             "a start clears everything");
}

static void cmd(XHCI_TRB *t, ULONG type, ULONG slot, ULONG dci)
{
    t->Param0 = 0;
    t->Param1 = 0;
    t->Status = 0;
    t->Control = XHCI_TRB_TYPE(type) | XHCI_TRB_SLOT_ID(slot) |
                 XHCI_TRB_EP_ID(dci);
}

static void answered(PXHCI_INJ_EP e, ULONG state, ULONG hold)
{
    XhciInjEpClear(e);
    XhciInjEpWatch(e, 4, 3);
    e->Watch = 0;
    e->State = state;
    e->Hold = hold;
}

/* A command the soft retry itself sends: no Input Control Context flags,
 * its TD surviving. */
static ULONG command(PXHCI_INJ_EP e, const XHCI_TRB *t, PULONG code)
{
    return XhciInjCommand(e, t, 0, 0, 1, code);
}

static void test_command(void)
{
    XHCI_INJ_EP e;
    XHCI_TRB t;
    ULONG code;
    ULONG types[4];
    ULONG i;

    XhciInjEpClear(&e);
    cmd(&t, XHCI_TRB_TYPE_RESET_EP, 4, 3);
    CHECK_EQ(command(&e, &t, &code), XHCI_INJ_CMD_PASS,
             "nothing answered: sent");

    answered(&e, XHCI_EP_STATE_HALTED, XHCI_INJ_HOLD_NONE);
    code = 0;
    cmd(&t, XHCI_TRB_TYPE_RESET_EP, 4, 2);
    CHECK_EQ(command(&e, &t, &code), XHCI_INJ_CMD_PASS,
             "another endpoint's reset is sent");
    cmd(&t, XHCI_TRB_TYPE_RESET_EP, 5, 3);
    CHECK_EQ(command(&e, &t, &code), XHCI_INJ_CMD_PASS,
             "another slot's too");
    cmd(&t, XHCI_TRB_TYPE_STOP_EP, 4, 3);
    CHECK_EQ(command(&e, &t, &code), XHCI_INJ_CMD_PASS,
             "a stop is sent");
    CHECK_EQ(e.State, XHCI_EP_STATE_HALTED, "and leaves Halted");
    cmd(&t, XHCI_TRB_TYPE_SET_TR_DEQUEUE, 4, 3);
    CHECK_EQ(command(&e, &t, &code), XHCI_INJ_CMD_ANSWER,
             "Set TR Dequeue on Halted answered");
    CHECK_EQ(code, XHCI_CC_CONTEXT_STATE_ERROR, "Context State Error");
    CHECK_EQ(e.State, XHCI_EP_STATE_HALTED, "still Halted");
    code = 0;
    cmd(&t, XHCI_TRB_TYPE_RESET_EP, 4, 3);
    CHECK_EQ(command(&e, &t, &code), XHCI_INJ_CMD_ANSWER,
             "the Reset Endpoint answered, not sent");
    CHECK_EQ(code, XHCI_CC_SUCCESS, "Success");
    CHECK_EQ(e.Slot, 0, "the answers end");
    CHECK_EQ(e.Block, 0, "the doorbell reaches it again");
    CHECK_EQ(command(&e, &t, &code), XHCI_INJ_CMD_PASS,
             "a second reset is sent");

    answered(&e, XHCI_EP_STATE_HALTED, XHCI_INJ_HOLD_FAIL);
    cmd(&t, XHCI_TRB_TYPE_RESET_EP, 4, 3);
    CHECK_EQ(command(&e, &t, &code), XHCI_INJ_CMD_ANSWER,
             "the failing reset answered");
    CHECK_EQ(code, XHCI_CC_CONTEXT_STATE_ERROR, "Context State Error");
    CHECK_EQ(e.State, XHCI_EP_STATE_HALTED, "still Halted");
    CHECK_EQ(e.Hold, XHCI_INJ_HOLD_NONE, "once");
    CHECK_EQ(command(&e, &t, &code), XHCI_INJ_CMD_ANSWER,
             "the next reset");
    CHECK_EQ(code, XHCI_CC_SUCCESS, "succeeds");

    answered(&e, XHCI_EP_STATE_HALTED, XHCI_INJ_HOLD_RING);
    code = 0x55;
    CHECK_EQ(command(&e, &t, &code), XHCI_INJ_CMD_RING, "held, ring");
    CHECK_EQ(code, 0x55, "no code written for a hold");
    CHECK_EQ(e.Hold, XHCI_INJ_HOLD_NONE, "the hold spent");
    CHECK_EQ(e.Slot, 4, "the caller ends the answers");
    answered(&e, XHCI_EP_STATE_HALTED, XHCI_INJ_HOLD_SECOND);
    CHECK_EQ(command(&e, &t, &code), XHCI_INJ_CMD_SECOND,
             "held, second");
    CHECK_EQ(command(&e, &t, &code), XHCI_INJ_CMD_ANSWER,
             "after the second error, the next reset");
    CHECK_EQ(code, XHCI_CC_SUCCESS, "succeeds");

    answered(&e, XHCI_EP_STATE_ERROR, XHCI_INJ_HOLD_NONE);
    cmd(&t, XHCI_TRB_TYPE_RESET_EP, 4, 3);
    CHECK_EQ(command(&e, &t, &code), XHCI_INJ_CMD_ANSWER,
             "Reset Endpoint on Error");
    CHECK_EQ(code, XHCI_CC_CONTEXT_STATE_ERROR, "refused: Halted only");
    CHECK_EQ(e.State, XHCI_EP_STATE_ERROR, "still Error");
    cmd(&t, XHCI_TRB_TYPE_SET_TR_DEQUEUE, 4, 3);
    CHECK_EQ(command(&e, &t, &code), XHCI_INJ_CMD_SEND_END,
             "Set TR Dequeue on Error is sent");
    XhciInjCommandSent(&e, 4, XHCI_CC_CONTEXT_STATE_ERROR);
    CHECK_EQ(e.State, XHCI_EP_STATE_ERROR, "a failed one leaves Error");
    XhciInjCommandSent(&e, 4, XHCI_CC_SUCCESS);
    CHECK_EQ(e.Slot, 0, "a successful one leaves it");

    /* The stale halt and the stop window answer nothing: sent. */
    answered(&e, 0, XHCI_INJ_HOLD_NONE);
    cmd(&t, XHCI_TRB_TYPE_RESET_EP, 4, 3);
    CHECK_EQ(command(&e, &t, &code), XHCI_INJ_CMD_PASS,
             "no answer, no emulation");

    types[0] = XHCI_TRB_TYPE_DISABLE_SLOT;
    types[1] = XHCI_TRB_TYPE_ADDRESS_DEVICE;
    types[2] = XHCI_TRB_TYPE_RESET_DEVICE;
    for (i = 0; i < 3; i++) {
        answered(&e, XHCI_EP_STATE_HALTED, XHCI_INJ_HOLD_SECOND);
        cmd(&t, types[i], 5, 0);
        CHECK_EQ(command(&e, &t, &code), XHCI_INJ_CMD_PASS,
                 "another slot's slot command");
        CHECK_EQ(e.Slot, 4, "leaves the answers");
        cmd(&t, types[i], 4, 0);
        CHECK_EQ(command(&e, &t, &code), XHCI_INJ_CMD_SEND_END,
                 "the slot's own is sent");
        CHECK_EQ(e.Slot, 4, "the answers stand until it completes");
        XhciInjCommandSent(&e, 5, XHCI_CC_SUCCESS);
        CHECK_EQ(e.Slot, 4, "another slot's completion");
        XhciInjCommandSent(&e, 4, 0);
        CHECK_EQ(e.State, XHCI_EP_STATE_HALTED, "never completed: stands");
        XhciInjCommandSent(&e, 4, XHCI_CC_CONTEXT_STATE_ERROR);
        CHECK_EQ(e.State, XHCI_EP_STATE_HALTED, "refused: stands");
        XhciInjCommandSent(&e, 4, XHCI_CC_SUCCESS);
        CHECK_EQ(e.Slot, 0, "succeeded: the answers end");
    }

    /* Configure Endpoint: only one that rewrites this endpoint's context
     * ends the answers - a composite device's other function's does not. */
    answered(&e, XHCI_EP_STATE_HALTED, XHCI_INJ_HOLD_NONE);
    cmd(&t, XHCI_TRB_TYPE_CONFIGURE_EP, 4, 0);
    CHECK_EQ(XhciInjCommand(&e, &t, 1UL << 5, (1UL << 5) | 1UL, 0, &code),
             XHCI_INJ_CMD_PASS, "another endpoint dropped and added");
    CHECK_EQ(XhciInjCommand(&e, &t, 0, (1UL << 4) | (1UL << 2) | 1UL, 0,
                            &code),
             XHCI_INJ_CMD_PASS, "the endpoints beside it added");
    CHECK_EQ(XhciInjCommand(&e, &t, 0, 0, 0, &code), XHCI_INJ_CMD_PASS,
             "no flags at all");
    CHECK_EQ(e.State, XHCI_EP_STATE_HALTED, "Halted stands through them");
    CHECK_EQ(XhciInjCommand(&e, &t, 1UL << 3, 0, 0, &code),
             XHCI_INJ_CMD_SEND_END, "a Drop of the endpoint");
    CHECK_EQ(XhciInjCommand(&e, &t, 0, (1UL << 3) | 1UL, 0, &code),
             XHCI_INJ_CMD_SEND_END, "an Add of the endpoint");
    t.Control |= XHCI_TRB_DC;
    CHECK_EQ(XhciInjCommand(&e, &t, 0, 0, 0, &code), XHCI_INJ_CMD_SEND_END,
             "a deconfigure");
    XhciInjCommandSent(&e, 4, XHCI_CC_RESOURCE_ERROR);
    CHECK_EQ(e.State, XHCI_EP_STATE_HALTED, "a failed one: Halted stands");
    XhciInjCommandSent(&e, 4, XHCI_CC_SUCCESS);
    CHECK_EQ(e.Slot, 0, "a successful one ends the answers");

    answered(&e, XHCI_EP_STATE_HALTED, XHCI_INJ_HOLD_NONE);
    cmd(&t, XHCI_TRB_TYPE_EVALUATE_CONTEXT, 4, 0);
    CHECK_EQ(command(&e, &t, &code), XHCI_INJ_CMD_PASS,
             "Evaluate Context sent");
    CHECK_EQ(e.Slot, 4, "and changes nothing");
}

/* The races run only for the soft retry's own reset of the TD it holds
 * (retrySurvives): a reset an abort, a cancel or a client's reset issues
 * on the paused pipe, one after the TD was retired, and one at
 * XhciTolerance 0 (no RetryWanted) are answered Success with nothing
 * rung, the hold spent. */
static void test_hold_survives(void)
{
    XHCI_INJ_EP e;
    XHCI_TRB t;
    ULONG code;

    cmd(&t, XHCI_TRB_TYPE_RESET_EP, 4, 3);
    answered(&e, XHCI_EP_STATE_HALTED, XHCI_INJ_HOLD_RING);
    code = 0;
    CHECK_EQ(XhciInjCommand(&e, &t, 0, 0, 0, &code), XHCI_INJ_CMD_ANSWER,
             "an abort's reset: no ring race");
    CHECK_EQ(code, XHCI_CC_SUCCESS, "answered Success");
    CHECK_EQ(e.Slot, 0, "the answers end, nothing held");
    CHECK_EQ(e.Hold, XHCI_INJ_HOLD_NONE, "the hold spent");

    answered(&e, XHCI_EP_STATE_HALTED, XHCI_INJ_HOLD_SECOND);
    code = 0;
    CHECK_EQ(XhciInjCommand(&e, &t, 0, 0, 0, &code), XHCI_INJ_CMD_ANSWER,
             "a cancel's reset: no second-error race");
    CHECK_EQ(code, XHCI_CC_SUCCESS, "answered Success");
    CHECK_EQ(e.Slot, 0, "ended");

    /* Tolerance off: the error took today's path, no retry survives; the
     * client's reset meets the hold. */
    answered(&e, XHCI_EP_STATE_HALTED, XHCI_INJ_HOLD_SECOND);
    CHECK_EQ(XhciInjCommand(&e, &t, 0, 0, 0, &code), XHCI_INJ_CMD_ANSWER,
             "XhciTolerance 0: answered plainly");
    answered(&e, XHCI_EP_STATE_HALTED, XHCI_INJ_HOLD_RING);
    CHECK_EQ(XhciInjCommand(&e, &t, 0, 0, 1, &code), XHCI_INJ_CMD_RING,
             "the retry's own reset runs the race");
    answered(&e, XHCI_EP_STATE_HALTED, XHCI_INJ_HOLD_FAIL);
    CHECK_EQ(XhciInjCommand(&e, &t, 0, 0, 0, &code), XHCI_INJ_CMD_ANSWER,
             "the failing reset fails whoever sends it");
    CHECK_EQ(code, XHCI_CC_CONTEXT_STATE_ERROR, "Context State Error");
}

static void test_stop_verdict(void)
{
    const ULONG first = 0x2000;
    const ULONG after = 0x2010;

    CHECK_EQ(XhciInjStopVerdict(first, first, after, 1, 8, 0, 0, 0, 0),
             XHCI_INJ_STOP_OK, "never fetched: at the TD, no event");
    CHECK_EQ(XhciInjStopVerdict(first | 1, first, after, 1, 8, 0, 0, 0, 0),
             XHCI_INJ_STOP_OK, "DCS ignored");
    CHECK_EQ(XhciInjStopVerdict(first, first, after, 1, 8, 1, first,
                                XHCI_CC_STOPPED, 8),
             XHCI_INJ_STOP_OK, "real hardware: stopped in it, nothing moved");
    CHECK_EQ(XhciInjStopVerdict(first, first, after, 1, 8, 1, first,
                                XHCI_CC_STOPPED, 4),
             XHCI_INJ_STOP_BROKEN, "part of it moved");
    CHECK_EQ(XhciInjStopVerdict(first, first, after, 1, 8, 1, first,
                                XHCI_CC_STOPPED_LENGTH_INVALID, 0),
             XHCI_INJ_STOP_BROKEN, "no length to trust");
    CHECK_EQ(XhciInjStopVerdict(after | 1, first, after, 1, 8, 1, first,
                                XHCI_CC_STOPPED, 8),
             XHCI_INJ_STOP_REPOINT, "QEMU's fetch-ahead, nothing moved");
    CHECK_EQ(XhciInjStopVerdict(after, first, after, 0, 8, 1, first,
                                XHCI_CC_STOPPED, 8),
             XHCI_INJ_STOP_BROKEN, "not for a TD of several TRBs");
    CHECK_EQ(XhciInjStopVerdict(after, first, after, 1, 8, 0, 0, 0, 0),
             XHCI_INJ_STOP_BROKEN, "past it with no event: completed?");
    CHECK_EQ(XhciInjStopVerdict(after, first, after, 1, 8, 1, first,
                                XHCI_CC_STOPPED, 0),
             XHCI_INJ_STOP_BROKEN, "past it, everything moved");
    CHECK_EQ(XhciInjStopVerdict(after, first, after, 1, 8, 1, 0x3000,
                                XHCI_CC_STOPPED, 8),
             XHCI_INJ_STOP_BROKEN, "the event names another TRB");
    CHECK_EQ(XhciInjStopVerdict(0x3000, first, after, 1, 8, 0, 0, 0, 0),
             XHCI_INJ_STOP_BROKEN, "somewhere else entirely");
}

static void test_ep0(void)
{
    XHCI_INJ_EP e;

    XhciInjEpClear(&e);
    CHECK_EQ(XhciInjEp0Fires(&e, 1, 1), 0, "nothing armed");
    XhciInjEp0Arm(&e, XHCI_INJ_EP0_THREAD, 2, 2);
    CHECK_EQ(XhciInjEp0Fires(&e, 1, 1), 0, "another port");
    CHECK_EQ(XhciInjEp0Fires(&e, 2, 0), 0, "THREAD wants a published device");
    e.EvPending = 1;
    CHECK_EQ(XhciInjEp0Fires(&e, 2, 1), 0, "not while an event waits");
    e.EvPending = 0;
    CHECK_EQ(XhciInjEp0Fires(&e, 2, 1), 1, "fires");
    CHECK_EQ(e.Ep0Want, XHCI_INJ_EP0_THREAD, "one left, still armed");
    CHECK_EQ(XhciInjEp0Fires(&e, 2, 1), 1, "fires again");
    CHECK_EQ(e.Ep0Want, 0, "spent, disarmed");
    CHECK_EQ(XhciInjEp0Fires(&e, 2, 1), 0, "nothing more");

    XhciInjEp0Arm(&e, XHCI_INJ_EP0_PREPDO, 0,
                  XhciInjLostCount(XHCI_INJ_ARG_FOREVER));
    CHECK_EQ(XhciInjEp0Fires(&e, 3, 1), 0, "PREPDO wants no PDO yet");
    CHECK_EQ(XhciInjEp0Fires(&e, 3, 0), 1, "any port, before the PDO");
    CHECK_EQ(XhciInjEp0Fires(&e, 5, 0), 1, "until CLEAR");
    CHECK_EQ(e.Ep0Want, XHCI_INJ_EP0_PREPDO, "still armed");
    XhciInjEp0Arm(&e, 0, 0, 0);
    CHECK_EQ(XhciInjEp0Fires(&e, 3, 0), 0, "CLEAR disarms");
    XhciInjEp0Arm(&e, XHCI_INJ_EP0_PREPDO, 0, 0);
    CHECK_EQ(e.Ep0Want, 0, "a count of none arms nothing");
}

int main(void)
{
    test_encoding();
    test_trigger();
    test_lost_count();
    test_port_fits();
    test_pec();
    test_oc();
    test_dead();
    test_irq_window();
    test_needs_live();
    test_codes();
    test_kinds();
    test_spend();
    test_pipe_fits();
    test_transfer_event();
    test_swallow_doorbell_state();
    test_command();
    test_hold_survives();
    test_stop_verdict();
    test_ep0();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
