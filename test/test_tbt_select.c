/*
 * test_tbt_select.c - host vectors for the connection manager's selection
 * as design record 22 section 3 has it (src\cm\tbt_hw.c TbtHwSelect,
 * TbtHwSoftwareNhiVersion, TbtHwRouterVersion; roadmap-usb4.md task
 * 40-A.2; record 22 section 14.1, "test_tbt_select").
 *
 * The four tests as a pure function of the matched id, the subsystem
 * vendor and the register readings, row by row of record 22 section 3.1's
 * table, and every negative case the owner's coverage plan names: an
 * unsupported Host Interface Version, a reserved router USB4 Version, a
 * failed FW_READY, the firmware status all-ones, ICM_EN set with
 * XhciCmMode 3, XhciCmMode 3 on an Alpine Ridge id, an Apple subsystem, a
 * USB-only id - each inert, never the other profile. For the Tiger Lake to
 * Meteor Lake and Maple Ridge rows every combination of a valid
 * NVM_AUTH_DONE / ICM_EN read: 0/0 admits the software connection manager,
 * 1/0 admits it marked experimental, x/1 goes to the ICM admission (whose
 * NVM_AUTH_DONE poll failing is inert), and no valid read is inert. On a
 * non-Intel candidate the Intel status register is never read.
 *
 * Expected values are worked from record 22 section 3 and the
 * specification (Table 12-11, ROUTER_CS_4), against the model in
 * tbt_fake.h, not from the code under test.
 */

#include <stdio.h>
#include <string.h>
#include "../src/cm/tbt_hw.h"
#include "tbt_fake.h"
#include "test_harness.h"

#define FW_ICM_EN   0x00000001UL
#define FW_AUTH     0x80000000UL

static void sel(ULONG ven, ULONG dev, ULONG cls, BOOLEAN ready, BOOLEAN apple,
                BOOLEAN force, TBT_HW_SELECTION *s)
{
    const TBT_HW_PROFILE *hw;

    hw = TbtHwMatch(ven, dev, cls, FALSE);
    TbtHwSelect(hw, &g_fakeHost, ready, apple, force, s);
}

/* Record 22 section 3.1: each row of the table. */
static void test_rows(void)
{
    static const ULONG icmOnly[] = {
        0x156A, 0x156C, 0x1575, 0x1577, 0x15BF, 0x15D2, 0x15D9, 0x15E8, 0x15EB,
        0x8A17, 0x8A0D
    };
    static const ULONG either[] = {
        0x9A1B, 0x9A1D, 0x9A1F, 0x9A21, 0x463E, 0x466D, 0xA73E, 0xA76D,
        0x7EB2, 0x7EC2, 0x7EC3, 0x1134, 0x1137
    };
    static const ULONG usb4Only[] = {
        0xA833, 0xA834, 0xE333, 0xE334, 0xE433, 0xE434, 0x4D33, 0x5781, 0x5784
    };
    static const ULONG unclaimed[] = { 0x15DC, 0x15DD, 0x15DE, 0x1513, 0x1547, 0x157D };
    TBT_HW_SELECTION s;
    ULONG i;

    /* ICM only: a running ICM is ICM; a stopped one is inert, never the
     * software CM. */
    for (i = 0; i < sizeof(icmOnly) / sizeof(icmOnly[0]); i++) {
        fakeReset();
        sel(0x8086, icmOnly[i], 0x088000, TRUE, FALSE, FALSE, &s);
        CHECK_EQ(s.Outcome, TBT_SEL_ICM, "ICM-only row, ICM running: ICM");
        fakeReset();
        g_fake.FwStatus = FW_AUTH;
        sel(0x8086, icmOnly[i], 0x088000, TRUE, FALSE, FALSE, &s);
        CHECK_EQ(s.Outcome, TBT_SEL_INERT, "ICM-only row, ICM stopped: inert");
        CHECK_EQ(s.Reason, TBT_INERT_ICM_NOT_RUNNING, "for that reason");
        CHECK_EQ(g_fake.Writes, 0, "and nothing written");
    }
    /* ICM or USB4. */
    for (i = 0; i < sizeof(either) / sizeof(either[0]); i++) {
        fakeReset();
        sel(0x8086, either[i], 0x0C0340, TRUE, FALSE, FALSE, &s);
        CHECK_EQ(s.Outcome, TBT_SEL_ICM, "either row, ICM running: ICM");
        fakeReset();
        g_fake.FwStatus = 0;
        sel(0x8086, either[i], 0x0C0340, TRUE, FALSE, FALSE, &s);
        CHECK_EQ(s.Outcome, TBT_SEL_SOFTWARE, "either row, valid ICM_EN 0: software CM");
        CHECK_EQ(s.Route, TBT_SW_ROUTE_ICM_DISABLED, "route 2");
        CHECK_EQ(g_fake.Writes, 0, "nothing written by the selection");
    }
    /* USB4 only: admitted by identity, the Intel status never read. */
    for (i = 0; i < sizeof(usb4Only) / sizeof(usb4Only[0]); i++) {
        fakeReset();
        sel(0x8086, usb4Only[i], 0x0C0340, TRUE, FALSE, FALSE, &s);
        CHECK_EQ(s.Outcome, TBT_SEL_SOFTWARE, "USB4-only row: software CM");
        CHECK_EQ(s.Route, TBT_SW_ROUTE_IDENTITY, "route 1");
        CHECK_EQ(g_fake.FwStatusReads, 0, "no firmware status read");
    }
    /* Generic: AMD named, ASMedia, the emulator's 1B36:00F4, an unlisted
     * Intel id of class 0C0340. */
    fakeReset();
    sel(0x1022, 0x1668, 0x0C0340, TRUE, FALSE, FALSE, &s);
    CHECK_EQ(s.Outcome, TBT_SEL_SOFTWARE, "AMD Pink Sardine #1");
    CHECK_EQ(g_fake.FwStatusReads, 0, "the Intel register is not read on AMD");
    fakeReset();
    sel(0x1022, 0x1669, 0x0C0340, TRUE, FALSE, FALSE, &s);
    CHECK_EQ(s.Outcome, TBT_SEL_SOFTWARE, "AMD Pink Sardine #2");
    fakeReset();
    sel(0x1B21, 0x2425, 0x0C0340, TRUE, FALSE, FALSE, &s);
    CHECK_EQ(s.Outcome, TBT_SEL_SOFTWARE, "ASMedia by class");
    CHECK_EQ(g_fake.FwStatusReads, 0, "no Intel register on ASMedia");
    fakeReset();
    sel(0x1B36, 0x00F4, 0x0C0340, TRUE, FALSE, FALSE, &s);
    CHECK_EQ(s.Outcome, TBT_SEL_SOFTWARE, "the emulator's usb4-generic");
    CHECK_EQ(g_fake.FwStatusReads, 0, "no Intel register on the emulator");
    fakeReset();
    sel(0x8086, 0x1234, 0x0C0340, TRUE, FALSE, FALSE, &s);
    CHECK_EQ(s.Outcome, TBT_SEL_SOFTWARE, "an unlisted Intel id by class");
    /* USB-only NHIs and the unclaimed generations: no candidate. */
    for (i = 0; i < sizeof(unclaimed) / sizeof(unclaimed[0]); i++) {
        CHECK(TbtHwMatch(0x8086, unclaimed[i], 0x088000, FALSE) == NULL,
              "USB-only or unclaimed id: not a candidate");
        fakeReset();
        sel(0x8086, unclaimed[i], 0x088000, TRUE, FALSE, FALSE, &s);
        CHECK_EQ(s.Outcome, TBT_SEL_INERT, "and inert if started anyway");
        CHECK_EQ(g_fake.FwStatusReads, 0, "with nothing read");
    }
}

/* Every NVM_AUTH_DONE / ICM_EN combination on a Tiger Lake class row and
 * on Maple Ridge. */
static void test_combinations(void)
{
    static const ULONG ids[] = { 0x463E, 0x1137 };
    TBT_HW_SELECTION s;
    ULONG i;

    for (i = 0; i < 2; i++) {
        /* 0/0 */
        fakeReset();
        g_fake.FwStatus = 0;
        sel(0x8086, ids[i], 0x0C0340, TRUE, FALSE, FALSE, &s);
        CHECK_EQ(s.Outcome, TBT_SEL_SOFTWARE, "0/0: software CM");
        CHECK(!s.Experimental, "not the experimental case");
        /* 1/0 */
        fakeReset();
        g_fake.FwStatus = FW_AUTH;
        sel(0x8086, ids[i], 0x0C0340, TRUE, FALSE, FALSE, &s);
        CHECK_EQ(s.Outcome, TBT_SEL_SOFTWARE, "1/0: software CM");
        CHECK(s.Experimental, "recorded as the experimental case");
        CHECK_EQ(s.FwStatus, FW_AUTH, "with the reading");
        /* 1/1: the ICM admission. */
        fakeReset();
        g_fake.FwStatus = FW_AUTH | FW_ICM_EN;
        sel(0x8086, ids[i], 0x0C0340, TRUE, FALSE, FALSE, &s);
        CHECK_EQ(s.Outcome, TBT_SEL_ICM, "1/1: ICM");
        /* 0/1: the ICM candidate predicate polls NVM_AUTH_DONE 10 ms and
         * fails - inert, never the software CM. */
        fakeReset();
        g_fake.FwStatus = FW_ICM_EN;
        sel(0x8086, ids[i], 0x0C0340, TRUE, FALSE, FALSE, &s);
        CHECK_EQ(s.Outcome, TBT_SEL_INERT, "0/1: inert");
        CHECK_EQ(s.Reason, TBT_INERT_NOT_AUTHENTICATED, "the predicate failed");
        /* 0/1 that authenticates within the 10 ms: ICM. */
        fakeReset();
        g_fake.FwStatus = FW_ICM_EN;
        g_fake.FwStatusNextAfter = 3;
        g_fake.FwStatusNext = FW_AUTH | FW_ICM_EN;
        sel(0x8086, ids[i], 0x0C0340, TRUE, FALSE, FALSE, &s);
        CHECK_EQ(s.Outcome, TBT_SEL_ICM, "0/1 then authenticated: ICM");
        /* No valid read. */
        fakeReset();
        g_fake.FwStatus = 0xFFFFFFFFUL;
        sel(0x8086, ids[i], 0x0C0340, TRUE, FALSE, FALSE, &s);
        CHECK_EQ(s.Outcome, TBT_SEL_INERT, "all ones: inert");
        CHECK_EQ(s.Reason, TBT_INERT_NOT_DECODING, "not a disabled ICM");
        /* Readiness failed: no read at all. */
        fakeReset();
        g_fake.FwStatus = 0;
        sel(0x8086, ids[i], 0x0C0340, FALSE, FALSE, FALSE, &s);
        CHECK_EQ(s.Reason, TBT_INERT_PLATFORM, "readiness failed: inert");
        CHECK_EQ(g_fake.FwStatusReads, 0, "with no status read");
    }
}

static void test_negatives(void)
{
    TBT_HW_SELECTION s;

    /* An unsupported Host Interface Version: 20h is neither 00h nor 40h. */
    fakeReset();
    g_fake.Caps = 0x0020000CUL;
    sel(0x1022, 0x1668, 0x0C0340, TRUE, FALSE, FALSE, &s);
    CHECK_EQ(s.Outcome, TBT_SEL_INERT, "interface version 20h: inert");
    CHECK_EQ(s.Reason, TBT_INERT_NHI_VERSION, "test (d)");
    fakeReset();
    g_fake.Caps = 0x0040000CUL;
    sel(0x1022, 0x1668, 0x0C0340, TRUE, FALSE, FALSE, &s);
    CHECK_EQ(s.Outcome, TBT_SEL_SOFTWARE, "interface version 40h: USB4 2.0, admitted");
    fakeReset();
    g_fake.Caps = 0x0041000CUL;
    g_fake.FwStatus = 0;
    sel(0x8086, 0x7EC2, 0x0C0340, TRUE, FALSE, FALSE, &s);
    CHECK_EQ(s.Reason, TBT_INERT_NHI_VERSION, "route 2 is checked too");
    CHECK(!TbtHwSoftwareNhiVersion(0x00010000UL), "01h refused");
    CHECK(TbtHwSoftwareNhiVersion(0x0000000CUL), "00h taken");
    CHECK(TbtHwSoftwareNhiVersion(0x0040000CUL), "40h taken");

    /* The router USB4 Version by its major alone (31:29; Linux
     * usb4_switch_version): major 0 Thunderbolt 3 whatever the minor,
     * 1 and 2 USB4, 3 to 7 reserved. */
    CHECK_EQ(TbtHwRouterVersion(0x00), TBT_HW_ROUTER_TBT3, "00h Thunderbolt 3");
    CHECK_EQ(TbtHwRouterVersion(0x02), TBT_HW_ROUTER_TBT3, "02h Thunderbolt 3 (TS3 Plus)");
    CHECK_EQ(TbtHwRouterVersion(0x03), TBT_HW_ROUTER_TBT3, "03h Thunderbolt 3");
    CHECK_EQ(TbtHwRouterVersion(0x10), TBT_HW_ROUTER_TBT3, "10h Thunderbolt 3");
    CHECK_EQ(TbtHwRouterVersion(0x1F), TBT_HW_ROUTER_TBT3, "1Fh Thunderbolt 3");
    CHECK_EQ(TbtHwRouterVersion(0x20), TBT_HW_ROUTER_USB4, "20h USB4 1.0");
    CHECK_EQ(TbtHwRouterVersion(0x21), TBT_HW_ROUTER_USB4, "21h USB4 1.1");
    CHECK_EQ(TbtHwRouterVersion(0x3F), TBT_HW_ROUTER_USB4, "3Fh USB4 1.x");
    CHECK_EQ(TbtHwRouterVersion(0x40), TBT_HW_ROUTER_USB4, "40h USB4 2.0");
    CHECK_EQ(TbtHwRouterVersion(0x41), TBT_HW_ROUTER_USB4, "41h USB4 2.1");
    CHECK_EQ(TbtHwRouterVersion(0x5F), TBT_HW_ROUTER_USB4, "5Fh USB4 2.x");
    CHECK_EQ(TbtHwRouterVersion(0x60), TBT_HW_ROUTER_RESERVED, "60h major 3 reserved");
    CHECK_EQ(TbtHwRouterVersion(0x80), TBT_HW_ROUTER_RESERVED, "80h major 4 reserved");
    CHECK_EQ(TbtHwRouterVersion(0xE0), TBT_HW_ROUTER_RESERVED, "E0h major 7 reserved");
    CHECK_EQ(TbtHwRouterVersion(0xFF), TBT_HW_ROUTER_RESERVED, "FFh reserved");
    CHECK_EQ(TbtHwRouterVersion(0x120), TBT_HW_ROUTER_RESERVED, "wider than a byte");
    CHECK_EQ(TBT_USB4_VERSION_MAJOR(0x41), TBT_USB4_MAJOR_2, "41h major");
    CHECK_EQ(TBT_USB4_VERSION_MINOR(0x41), 1UL, "41h minor");
    CHECK_EQ(TBT_USB4_VERSION_MINOR(0x02), 2UL, "02h minor");

    /* A failed FW_READY is a failed platform readiness. */
    fakeReset();
    sel(0x8086, 0xA833, 0x0C0340, FALSE, FALSE, FALSE, &s);
    CHECK_EQ(s.Reason, TBT_INERT_PLATFORM, "Lunar Lake, FW_READY never set: inert");
    CHECK_EQ(g_fake.Writes, 0, "nothing written");

    /* ICM_EN set with XhciCmMode 3. */
    fakeReset();
    sel(0x8086, 0xA73E, 0x0C0340, TRUE, FALSE, TRUE, &s);
    CHECK_EQ(s.Reason, TBT_INERT_FORCED_ICM_EN, "mode 3 with the ICM running: inert");
    /* XhciCmMode 3 with ICM_EN clear: admitted, marked forced. */
    fakeReset();
    g_fake.FwStatus = 0;
    sel(0x8086, 0xA73E, 0x0C0340, TRUE, FALSE, TRUE, &s);
    CHECK_EQ(s.Outcome, TBT_SEL_SOFTWARE, "mode 3, ICM_EN 0: software CM");
    CHECK(s.Forced, "marked forced");
    /* XhciCmMode 3 on Alpine, Falcon and Titan Ridge and Ice Lake. */
    fakeReset();
    g_fake.FwStatus = 0;
    sel(0x8086, 0x15BF, 0x088000, TRUE, FALSE, TRUE, &s);
    CHECK_EQ(s.Reason, TBT_INERT_FORCED_UNSUPPORTED, "mode 3 on Alpine Ridge: inert");
    CHECK_EQ(g_fake.FwStatusReads, 0, "decided by the row alone");
    fakeReset();
    sel(0x8086, 0x156A, 0x088000, TRUE, FALSE, TRUE, &s);
    CHECK_EQ(s.Reason, TBT_INERT_FORCED_UNSUPPORTED, "mode 3 on Falcon Ridge: inert");
    fakeReset();
    sel(0x8086, 0x15E8, 0x088000, TRUE, FALSE, TRUE, &s);
    CHECK_EQ(s.Reason, TBT_INERT_FORCED_UNSUPPORTED, "mode 3 on Titan Ridge: inert");
    fakeReset();
    sel(0x8086, 0x8A17, 0x0C0340, TRUE, FALSE, TRUE, &s);
    CHECK_EQ(s.Reason, TBT_INERT_FORCED_UNSUPPORTED, "mode 3 on Ice Lake: inert");
    /* XhciCmMode 3 on a software-only candidate changes nothing. */
    fakeReset();
    sel(0x1022, 0x1668, 0x0C0340, TRUE, FALSE, TRUE, &s);
    CHECK_EQ(s.Outcome, TBT_SEL_SOFTWARE, "mode 3 on AMD: as without it");
    CHECK_EQ(s.Route, TBT_SW_ROUTE_IDENTITY, "by identity");
    /* Mode 3 cannot pass a dead BAR0 or an unsupported interface. */
    fakeReset();
    g_fake.Caps = 0xFFFFFFFFUL;
    sel(0x1022, 0x1668, 0x0C0340, TRUE, FALSE, TRUE, &s);
    CHECK_EQ(s.Reason, TBT_INERT_NOT_DECODING, "mode 3, BAR0 dead: inert");
    fakeReset();
    g_fake.Caps = 0x0010000CUL;
    sel(0x1022, 0x1668, 0x0C0340, TRUE, FALSE, TRUE, &s);
    CHECK_EQ(s.Reason, TBT_INERT_NHI_VERSION, "mode 3, version 10h: inert");

    /* An Apple subsystem, on every kind of row. */
    fakeReset();
    sel(0x8086, 0x15BF, 0x088000, TRUE, TRUE, FALSE, &s);
    CHECK_EQ(s.Reason, TBT_INERT_APPLE, "Apple, Alpine Ridge: inert");
    fakeReset();
    g_fake.FwStatus = 0;
    sel(0x8086, 0x463E, 0x0C0340, TRUE, TRUE, FALSE, &s);
    CHECK_EQ(s.Reason, TBT_INERT_APPLE, "Apple, Alder Lake: inert");
    fakeReset();
    sel(0x1022, 0x1668, 0x0C0340, TRUE, TRUE, FALSE, &s);
    CHECK_EQ(s.Reason, TBT_INERT_APPLE, "Apple, AMD: inert");
    CHECK_EQ(g_fake.Writes, 0, "nothing written");
}

int main(void)
{
    test_rows();
    test_combinations();
    test_negatives();
    printf("test_tbt_select: %d checks, %d failures\n", checks, failures);
    return failures;
}
