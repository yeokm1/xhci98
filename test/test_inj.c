/*
 * test_inj.c - host vectors for 35-T.9's fault injection, the pure half
 * (src\xhci_inj.c; the qemu flavour's alone in the driver).
 *
 * The trigger's encoding and its sequence rule, the port a PED or an
 * over-current may be aimed at, and every register answer the layer gives:
 * PEC until its acknowledgement, the over-current's PP, OCA and OCC through
 * the release and the repower, the PP the real port is given, all-ones
 * USBSTS spent read by read, and the proof's read-back.
 */

#include <stdio.h>
#include "../src/xhci.h"
#include "../src/xhci_inj.h"
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

    /* Every code: built, reserved, unknown. A refused one is consumed. */
    for (f = 0; f < 256; f++) {
        ULONG want;
        char what[64];

        if ((f >= XHCI_INJ_LOST_IRQ && f <= XHCI_INJ_DEAD_NOPROOF) ||
            f == XHCI_INJ_CLEAR) {
            want = XHCI_INJ_TAKE_FIRE;
        } else if (f >= XHCI_INJ_TRANSACTION && f <= XHCI_INJ_RESERVED_LAST) {
            want = XHCI_INJ_TAKE_UNBUILT;
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

static void test_dead(void)
{
    XHCI_INJ_REGS r;

    XhciInjRegsClear(&r);
    CHECK_EQ(XhciInjUsbsts(&r, 0x18UL), 0x18UL, "not armed");
    XhciInjArmDead(&r, 3, 0);
    CHECK_EQ(XhciInjUsbsts(&r, 0x18UL), 0xFFFFFFFFUL, "read 1");
    CHECK_EQ(XhciInjUsbsts(&r, 0x18UL), 0xFFFFFFFFUL, "read 2");
    CHECK_EQ(XhciInjUsbsts(&r, 0x18UL), 0xFFFFFFFFUL, "read 3");
    CHECK_EQ(XhciInjUsbsts(&r, 0x18UL), 0x18UL, "spent: the real read");
    CHECK_EQ(XhciInjPciCommand(&r, 0x0002UL), 0x0002UL, "proof real");

    XhciInjArmDead(&r, 0, 1);
    CHECK_EQ(r.DeadLeft, XHCI_INJ_DEAD_FOREVER, "0 is until CLEAR");
    CHECK_EQ(XhciInjUsbsts(&r, 0x18UL), 0xFFFFFFFFUL, "all-ones");
    CHECK_EQ(r.DeadLeft, XHCI_INJ_DEAD_FOREVER, "never spent");
    CHECK_EQ(XhciInjPciCommand(&r, 0x0002UL), 0x0006UL, "BME reads set");
    CHECK_EQ(XhciInjPciCommand(&r, 0x0006UL), 0x0006UL, "set stays set");
    CHECK_EQ(XhciInjPciCommand(&r, 0xFFFFUL), 0xFFFFUL,
             "a function that does not answer is left so");
    CHECK_EQ(XHCI_INJ_PCI_BME, 0x0004UL, "PCI Command bit 2");
    XhciInjRegsClear(&r);
    CHECK_EQ(XhciInjUsbsts(&r, 0x18UL), 0x18UL, "CLEAR");
    CHECK_EQ(XhciInjPciCommand(&r, 0x0002UL), 0x0002UL, "CLEAR, proof real");
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

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
