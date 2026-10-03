/*
 * test_xport.c - host vectors for the mass-storage transport policy
 * (src\xhci_xport.c; roadmap-hcd.md task 31-A.3).
 *
 * The device vectors are CONSTRUCTED in the shapes test-equipment.md
 * records - a bridge offering Bulk-Only at alternate 0 and UAS at alternate
 * 1 (the ASMedia 174C:5106's shape at both speeds), a UAS-only device (what
 * QEMU's usb-uas is expected to present; 31-0 reads it), a Bulk-Only flash
 * drive - not read off the units. Every expected transport was worked by
 * hand from the roadmap's 31-A.3 rule, every id string from design record 13
 * section 10.7's forms, not from the code under test.
 */

#include <stdio.h>
#include <string.h>
#include "../src/xhci_xport.h"
#include "test_harness.h"

/* 174C:5106, bcdDevice 0210. */
static const UCHAR dualDev[18] = {
    0x12, 0x01, 0x10, 0x02, 0x00, 0x00, 0x00, 0x40,
    0x4C, 0x17, 0x06, 0x51, 0x10, 0x02, 0x01, 0x02, 0x03, 0x01 };

/* Interface 0: alternate 0 Bulk-Only 08/06/50 (two bulk endpoints), then
 * alternate 1 UAS 08/06/62 (four bulk endpoints, each with its Pipe Usage
 * descriptor: command, status, data-in, data-out). */
static const UCHAR dualCfg[85] = {
    0x09, 0x02, 0x55, 0x00, 0x01, 0x01, 0x00, 0x80, 0x32,
    /* 9 */  0x09, 0x04, 0x00, 0x00, 0x02, 0x08, 0x06, 0x50, 0x00,
    0x07, 0x05, 0x81, 0x02, 0x00, 0x02, 0x00,
    0x07, 0x05, 0x02, 0x02, 0x00, 0x02, 0x00,
    /* 32 */ 0x09, 0x04, 0x00, 0x01, 0x04, 0x08, 0x06, 0x62, 0x00,
    0x07, 0x05, 0x01, 0x02, 0x00, 0x02, 0x00, 0x04, 0x24, 0x01, 0x00,
    0x07, 0x05, 0x82, 0x02, 0x00, 0x02, 0x00, 0x04, 0x24, 0x02, 0x00,
    0x07, 0x05, 0x83, 0x02, 0x00, 0x02, 0x00, 0x04, 0x24, 0x03, 0x00,
    0x07, 0x05, 0x04, 0x02, 0x00, 0x02, 0x00, 0x04, 0x24, 0x04, 0x00 };

/* UAS alone at alternate 0. */
static const UCHAR uasCfg[62] = {
    0x09, 0x02, 0x3E, 0x00, 0x01, 0x01, 0x00, 0x80, 0x32,
    0x09, 0x04, 0x00, 0x00, 0x04, 0x08, 0x06, 0x62, 0x00,
    0x07, 0x05, 0x01, 0x02, 0x00, 0x02, 0x00, 0x04, 0x24, 0x01, 0x00,
    0x07, 0x05, 0x82, 0x02, 0x00, 0x02, 0x00, 0x04, 0x24, 0x02, 0x00,
    0x07, 0x05, 0x83, 0x02, 0x00, 0x02, 0x00, 0x04, 0x24, 0x03, 0x00,
    0x07, 0x05, 0x04, 0x02, 0x00, 0x02, 0x00, 0x04, 0x24, 0x04, 0x00 };

/* The order reversed: UAS at alternate 0, Bulk-Only at alternate 1, which
 * usbstor.sys (alternate 0 only) cannot reach. */
static const UCHAR revCfg[85] = {
    0x09, 0x02, 0x55, 0x00, 0x01, 0x01, 0x00, 0x80, 0x32,
    0x09, 0x04, 0x00, 0x00, 0x04, 0x08, 0x06, 0x62, 0x00,
    0x07, 0x05, 0x01, 0x02, 0x00, 0x02, 0x00, 0x04, 0x24, 0x01, 0x00,
    0x07, 0x05, 0x82, 0x02, 0x00, 0x02, 0x00, 0x04, 0x24, 0x02, 0x00,
    0x07, 0x05, 0x83, 0x02, 0x00, 0x02, 0x00, 0x04, 0x24, 0x03, 0x00,
    0x07, 0x05, 0x04, 0x02, 0x00, 0x02, 0x00, 0x04, 0x24, 0x04, 0x00,
    0x09, 0x04, 0x00, 0x01, 0x02, 0x08, 0x06, 0x50, 0x00,
    0x07, 0x05, 0x81, 0x02, 0x00, 0x02, 0x00,
    0x07, 0x05, 0x02, 0x02, 0x00, 0x02, 0x00 };

/* A Bulk-Only flash drive (0781:5408's shape): one setting, 08/06/50. */
static const UCHAR botDev[18] = {
    0x12, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 0x40,
    0x81, 0x07, 0x08, 0x54, 0x00, 0x01, 0x01, 0x02, 0x03, 0x01 };
static const UCHAR botCfg[32] = {
    0x09, 0x02, 0x20, 0x00, 0x01, 0x01, 0x00, 0x80, 0x32,
    0x09, 0x04, 0x00, 0x00, 0x02, 0x08, 0x06, 0x50, 0x00,
    0x07, 0x05, 0x81, 0x02, 0x00, 0x02, 0x00,
    0x07, 0x05, 0x02, 0x02, 0x00, 0x02, 0x00 };

/* A composite (synthetic 1234:5678): HID at interface 0, the dual storage
 * interface at interface 1. */
static const UCHAR compDev[18] = {
    0x12, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 0x40,
    0x34, 0x12, 0x78, 0x56, 0x01, 0x00, 0x01, 0x02, 0x03, 0x01 };
static const UCHAR compCfg[110] = {
    0x09, 0x02, 0x6E, 0x00, 0x02, 0x01, 0x00, 0x80, 0x32,
    0x09, 0x04, 0x00, 0x00, 0x01, 0x03, 0x00, 0x00, 0x00,
    0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22, 0x41, 0x00,
    0x07, 0x05, 0x83, 0x03, 0x08, 0x00, 0x0A,
    0x09, 0x04, 0x01, 0x00, 0x02, 0x08, 0x06, 0x50, 0x00,
    0x07, 0x05, 0x81, 0x02, 0x00, 0x02, 0x00,
    0x07, 0x05, 0x02, 0x02, 0x00, 0x02, 0x00,
    0x09, 0x04, 0x01, 0x01, 0x04, 0x08, 0x06, 0x62, 0x00,
    0x07, 0x05, 0x01, 0x02, 0x00, 0x02, 0x00, 0x04, 0x24, 0x01, 0x00,
    0x07, 0x05, 0x82, 0x02, 0x00, 0x02, 0x00, 0x04, 0x24, 0x02, 0x00,
    0x07, 0x05, 0x84, 0x02, 0x00, 0x02, 0x00, 0x04, 0x24, 0x03, 0x00,
    0x07, 0x05, 0x04, 0x02, 0x00, 0x02, 0x00, 0x04, 0x24, 0x04, 0x00 };

#define SS  XHCI_XPORT_F_SUPERSPEED
#define STR XHCI_XPORT_F_HC_STREAMS
#define FRC XHCI_XPORT_F_FORCE_BOT

static const char hwDualBoth[] =
    "USB\\VID_174C&PID_5106&REV_0210\0USB\\VID_174C&PID_5106\0\0";
static const char ciUas[] =
    "USB\\Class_08&SubClass_06&Prot_62\0USB\\Class_08&SubClass_06\0"
    "USB\\Class_08\0\0";
static const char ciBot[] =
    "USB\\Class_08&SubClass_06&Prot_50\0USB\\Class_08&SubClass_06\0"
    "USB\\Class_08\0\0";

/* The id answer of `which` for `x` is exactly `want` (sizeof, so the NULs
 * inside count; the literal's own terminator is dropped). */
static void check_id(const UCHAR *dev, const XHCI_XPORT *x, ULONG mi,
                     ULONG which, const char *want, ULONG wantLen,
                     const char *what, int line)
{
    char buf[256];
    ULONG used;
    ULONG r;

    memset(buf, 'x', sizeof(buf));
    used = 0xDEAD;
    r = XhciXportId(dev, x, mi, which, buf, sizeof(buf), &used);
    check_eq_impl(r, XHCI_XPORT_OK, what, __FILE__, line);
    check_eq_impl(used, wantLen, what, __FILE__, line);
    check_impl(used == wantLen && memcmp(buf, want, wantLen) == 0, what,
               __FILE__, line);
}

#define CHECK_ID(dev, x, mi, which, lit, what) \
    check_id((dev), (x), (mi), (which), (lit), sizeof(lit) - 1, (what), \
             __LINE__)

static void choose(const UCHAR *cfg, ULONG len, ULONG iface, ULONG flags,
                   PXHCI_XPORT x)
{
    memset(x, 0xA5, sizeof(*x));
    CHECK_EQ(XhciXportChoose(cfg, len, iface, flags, x), XHCI_XPORT_OK,
             "choose: a sound configuration");
}

/* Both transports offered, Bulk-Only at alternate 0: every flag combination. */
static void test_dual(void)
{
    XHCI_XPORT x;

    /* High Speed: UAS, streamless, whatever the controller. */
    choose(dualCfg, sizeof(dualCfg), 0, 0, &x);
    CHECK_EQ(x.Transport, XHCI_XPORT_UAS, "dual HS: UAS");
    CHECK_EQ(x.Why, XHCI_XPORT_WHY_UAS, "dual HS: both offered, UAS chosen");
    CHECK_EQ(x.Alternate, 1, "dual HS: the UAS driver selects alternate 1");
    CHECK_EQ(x.Protocol, 0x62, "dual HS: Prot_62");
    CHECK_EQ(x.ShortHardwareId, 1, "dual HS: VID/PID id kept (residual case)");
    CHECK_ID(dualDev, &x, XHCI_XPORT_NO_MI, XHCI_XPORT_ID_HARDWARE,
             hwDualBoth, "dual HS UAS: hardware ids, VID/PID kept");
    CHECK_ID(dualDev, &x, XHCI_XPORT_NO_MI, XHCI_XPORT_ID_COMPATIBLE, ciUas,
             "dual HS UAS: compatible ids Prot_62, never Prot_50");

    choose(dualCfg, sizeof(dualCfg), 0, STR, &x);
    CHECK_EQ(x.Transport, XHCI_XPORT_UAS, "dual HS, streaming HC: UAS");

    /* The value forces Bulk-Only. */
    choose(dualCfg, sizeof(dualCfg), 0, FRC, &x);
    CHECK_EQ(x.Transport, XHCI_XPORT_BOT, "dual HS forced: Bulk-Only");
    CHECK_EQ(x.Why, XHCI_XPORT_WHY_BOT_FORCED, "dual HS forced: the value");
    CHECK_EQ(x.Alternate, 0, "dual HS forced: usbstor selects alternate 0");
    CHECK_EQ(x.ShortHardwareId, 1, "dual HS forced: VID/PID id kept");
    CHECK_ID(dualDev, &x, XHCI_XPORT_NO_MI, XHCI_XPORT_ID_HARDWARE,
             hwDualBoth, "dual forced: section 10.7's two hardware ids");
    CHECK_ID(dualDev, &x, XHCI_XPORT_NO_MI, XHCI_XPORT_ID_COMPATIBLE, ciBot,
             "dual forced: compatible ids Prot_50, never Prot_62");

    /* SuperSpeed: UAS needs streams. */
    choose(dualCfg, sizeof(dualCfg), 0, SS | STR, &x);
    CHECK_EQ(x.Transport, XHCI_XPORT_UAS, "dual SS streaming: UAS");
    CHECK_EQ(x.Why, XHCI_XPORT_WHY_UAS, "dual SS streaming: chosen");
    CHECK_EQ(x.Alternate, 1, "dual SS streaming: alternate 1");

    choose(dualCfg, sizeof(dualCfg), 0, SS, &x);
    CHECK_EQ(x.Transport, XHCI_XPORT_BOT, "dual SS no streams: Bulk-Only");
    CHECK_EQ(x.Why, XHCI_XPORT_WHY_BOT_NO_STREAMS,
             "dual SS no streams: the controller, not the value");
    CHECK_ID(dualDev, &x, XHCI_XPORT_NO_MI, XHCI_XPORT_ID_COMPATIBLE, ciBot,
             "dual SS no streams: Prot_50");

    choose(dualCfg, sizeof(dualCfg), 0, SS | FRC, &x);
    CHECK_EQ(x.Transport, XHCI_XPORT_BOT, "dual SS no streams, forced");
    CHECK_EQ(x.Why, XHCI_XPORT_WHY_BOT_FORCED,
             "dual SS no streams, forced: counted as the value");

    choose(dualCfg, sizeof(dualCfg), 0, SS | STR | FRC, &x);
    CHECK_EQ(x.Transport, XHCI_XPORT_BOT, "dual SS streaming, forced: BOT");
    CHECK_EQ(x.Why, XHCI_XPORT_WHY_BOT_FORCED, "the value wins at SS");
}

/* UAS alone: the value never moves it; SuperSpeed without streams has no
 * transport at all. */
static void test_uas_only(void)
{
    XHCI_XPORT x;

    choose(uasCfg, sizeof(uasCfg), 0, 0, &x);
    CHECK_EQ(x.Transport, XHCI_XPORT_UAS, "UAS-only HS: UAS");
    CHECK_EQ(x.Why, XHCI_XPORT_WHY_UAS_ONLY, "UAS-only HS: why");
    CHECK_EQ(x.Alternate, 0, "UAS-only: alternate 0");
    CHECK_ID(dualDev, &x, XHCI_XPORT_NO_MI, XHCI_XPORT_ID_HARDWARE,
             hwDualBoth, "UAS-only: hardware ids, VID/PID kept");
    CHECK_ID(dualDev, &x, XHCI_XPORT_NO_MI, XHCI_XPORT_ID_COMPATIBLE, ciUas,
             "UAS-only: Prot_62");

    choose(uasCfg, sizeof(uasCfg), 0, FRC, &x);
    CHECK_EQ(x.Transport, XHCI_XPORT_UAS, "UAS-only forced: stays UAS");
    CHECK_EQ(x.Why, XHCI_XPORT_WHY_UAS_ONLY_FORCED,
             "UAS-only forced: counted apart");

    choose(uasCfg, sizeof(uasCfg), 0, SS | STR, &x);
    CHECK_EQ(x.Transport, XHCI_XPORT_UAS, "UAS-only SS streaming: UAS");

    choose(uasCfg, sizeof(uasCfg), 0, SS | STR | FRC, &x);
    CHECK_EQ(x.Why, XHCI_XPORT_WHY_UAS_ONLY_FORCED,
             "UAS-only SS streaming, forced: stays UAS");

    choose(uasCfg, sizeof(uasCfg), 0, SS, &x);
    CHECK_EQ(x.Transport, XHCI_XPORT_REFUSED, "UAS-only SS no streams");
    CHECK_EQ(x.Why, XHCI_XPORT_WHY_NO_STREAMS, "UAS-only SS no streams: why");
    CHECK_EQ(x.ShortHardwareId, 0, "refused: no VID/PID-only id");
    CHECK_ID(dualDev, &x, XHCI_XPORT_NO_MI, XHCI_XPORT_ID_HARDWARE,
             "USB\\XHCI98_NOXPORT&VID_174C&PID_5106&REV_0210\0\0",
             "refused: the project-owned id alone, no USB\\VID_ form");
    {
        char buf[16];
        ULONG used;

        used = 0xDEAD;
        CHECK_EQ(XhciXportId(dualDev, &x, XHCI_XPORT_NO_MI,
                             XHCI_XPORT_ID_COMPATIBLE, buf, sizeof(buf),
                             &used),
                 XHCI_XPORT_NO_IDS, "refused: no compatible ids at all");
        CHECK_EQ(used, 0, "refused: nothing written");
    }

    choose(uasCfg, sizeof(uasCfg), 0, SS | FRC, &x);
    CHECK_EQ(x.Transport, XHCI_XPORT_REFUSED,
             "UAS-only SS no streams, forced: still no Bulk-Only to honour");
}

/* Where a refused device sits decides send-back or refusal in place. */
static void test_refused_at(void)
{
    CHECK_EQ(XhciXportRefusedAt(0, 5), XHCI_XPORT_AT_ROOT_COMPANION,
             "root port paired with port 5: 29-A.5's send-back");
    CHECK_EQ(XhciXportRefusedAt(0, 0), XHCI_XPORT_AT_ROOT_ALONE,
             "root port with no companion: refused in place");
    CHECK_EQ(XhciXportRefusedAt(0x00003, 5), XHCI_XPORT_AT_BEHIND_HUB,
             "behind a hub on a paired root port: refused in place");
    CHECK_EQ(XhciXportRefusedAt(0x00021, 0), XHCI_XPORT_AT_BEHIND_HUB,
             "two tiers down, unpaired root: refused in place");
}

/* Bulk-Only only at alternate 1: usbstor.sys selects alternate 0, so that
 * setting does not count. */
static void test_reversed(void)
{
    XHCI_XPORT x;

    choose(revCfg, sizeof(revCfg), 0, 0, &x);
    CHECK_EQ(x.Transport, XHCI_XPORT_UAS, "reversed HS: UAS");
    CHECK_EQ(x.Why, XHCI_XPORT_WHY_UAS_ONLY,
             "reversed HS: Bulk-Only off alternate 0 is not offered");
    CHECK_EQ(x.Alternate, 0, "reversed: UAS at alternate 0");

    choose(revCfg, sizeof(revCfg), 0, FRC, &x);
    CHECK_EQ(x.Transport, XHCI_XPORT_UAS, "reversed forced: UAS");
    CHECK_EQ(x.Why, XHCI_XPORT_WHY_UAS_ONLY_FORCED, "reversed forced: why");

    choose(revCfg, sizeof(revCfg), 0, SS, &x);
    CHECK_EQ(x.Transport, XHCI_XPORT_REFUSED,
             "reversed SS no streams: refused, never a Prot_50 id");
}

/* No UAS setting: the policy leaves the ids as section 10.7 has them. */
static void test_untouched(void)
{
    XHCI_XPORT x;

    choose(botCfg, sizeof(botCfg), 0, SS | FRC, &x);
    CHECK_EQ(x.Transport, XHCI_XPORT_NONE, "Bulk-Only drive: untouched");
    CHECK_EQ(x.Why, XHCI_XPORT_WHY_NOT_UAS, "Bulk-Only drive: why");
    CHECK_EQ(x.ShortHardwareId, 1, "Bulk-Only drive: VID/PID id kept");
    CHECK_ID(botDev, &x, XHCI_XPORT_NO_MI, XHCI_XPORT_ID_HARDWARE,
             "USB\\VID_0781&PID_5408&REV_0100\0USB\\VID_0781&PID_5408\0\0",
             "Bulk-Only drive: hardware ids");
    CHECK_ID(botDev, &x, XHCI_XPORT_NO_MI, XHCI_XPORT_ID_COMPATIBLE, ciBot,
             "Bulk-Only drive: compatible ids");

    choose(compCfg, sizeof(compCfg), 0, 0, &x);
    CHECK_EQ(x.Transport, XHCI_XPORT_NONE, "HID interface: untouched");
    CHECK_EQ(x.Class, 0x03, "HID interface: its own class");
    CHECK_ID(compDev, &x, 0, XHCI_XPORT_ID_COMPATIBLE,
             "USB\\Class_03&SubClass_00&Prot_00\0USB\\Class_03&SubClass_00\0"
             "USB\\Class_03\0\0",
             "HID function: compatible ids");

    choose(botCfg, sizeof(botCfg), 7, 0, &x);
    CHECK_EQ(x.Transport, XHCI_XPORT_NONE, "absent interface: NONE");
    CHECK_EQ(x.Class, 0, "absent interface: zero triple");
}

/* A function PDO of a split device follows the same rule, with &MI_. */
static void test_function(void)
{
    XHCI_XPORT x;

    choose(compCfg, sizeof(compCfg), 1, 0, &x);
    CHECK_EQ(x.Transport, XHCI_XPORT_UAS, "storage function HS: UAS");
    CHECK_EQ(x.Alternate, 1, "storage function: alternate 1");
    CHECK_ID(compDev, &x, 1, XHCI_XPORT_ID_HARDWARE,
             "USB\\VID_1234&PID_5678&REV_0001&MI_01\0"
             "USB\\VID_1234&PID_5678&MI_01\0\0",
             "storage function UAS: VID/PID&MI id kept");
    CHECK_ID(compDev, &x, 1, XHCI_XPORT_ID_COMPATIBLE, ciUas,
             "storage function UAS: Prot_62");

    choose(compCfg, sizeof(compCfg), 1, FRC, &x);
    CHECK_EQ(x.Transport, XHCI_XPORT_BOT, "storage function forced: BOT");
    CHECK_ID(compDev, &x, 1, XHCI_XPORT_ID_HARDWARE,
             "USB\\VID_1234&PID_5678&REV_0001&MI_01\0"
             "USB\\VID_1234&PID_5678&MI_01\0\0",
             "storage function forced: both hardware ids");
    CHECK_ID(compDev, &x, 1, XHCI_XPORT_ID_COMPATIBLE, ciBot,
             "storage function forced: Prot_50");

    /* A refused function (the decision is test_uas_only's; only the ids
     * are new here): the project-owned id with its &MI_, nothing else. */
    memset(&x, 0, sizeof(x));
    x.Transport = XHCI_XPORT_REFUSED;
    x.Why = XHCI_XPORT_WHY_NO_STREAMS;
    CHECK_ID(compDev, &x, 1, XHCI_XPORT_ID_HARDWARE,
             "USB\\XHCI98_NOXPORT&VID_1234&PID_5678&REV_0001&MI_01\0\0",
             "refused function: no USB\\VID_ form, &MI_ kept");
    x.ShortHardwareId = 1;
    CHECK_ID(compDev, &x, 1, XHCI_XPORT_ID_HARDWARE,
             "USB\\XHCI98_NOXPORT&VID_1234&PID_5678&REV_0001&MI_01\0\0",
             "refused function: ShortHardwareId cannot bring one back");
}

static void test_single(void)
{
    ULONG iface;

    iface = 99;
    CHECK_EQ(XhciXportSingleInterface(dualCfg, sizeof(dualCfg), &iface), 1,
             "dual: one interface, two alternates");
    CHECK_EQ(iface, 0, "dual: interface 0");
    CHECK_EQ(XhciXportSingleInterface(compCfg, sizeof(compCfg), &iface), 0,
             "composite: two interfaces");
    CHECK_EQ(XhciXportSingleInterface(botCfg, 8, &iface), 0,
             "short header");
    CHECK_EQ(XhciXportSingleInterface(NULL, 32, &iface), 0, "no config");
}

static void test_malformed(void)
{
    UCHAR bad[85];
    XHCI_XPORT x;
    char buf[64];
    ULONG used;

    CHECK_EQ(XhciXportChoose(NULL, 85, 0, 0, &x), XHCI_XPORT_BAD_PARAM,
             "no configuration");
    CHECK_EQ(XhciXportChoose(dualCfg, 85, 0, 0, NULL), XHCI_XPORT_BAD_PARAM,
             "no answer");
    CHECK_EQ(XhciXportChoose(dualCfg, 84, 0, 0, &x), XHCI_XPORT_MALFORMED,
             "wTotalLength past the bytes held");

    memcpy(bad, dualCfg, sizeof(bad));
    bad[1] = 0x04;
    CHECK_EQ(XhciXportChoose(bad, 85, 0, 0, &x), XHCI_XPORT_MALFORMED,
             "not a configuration descriptor");
    CHECK_EQ(x.Transport, XHCI_XPORT_NONE, "malformed: NONE");

    memcpy(bad, dualCfg, sizeof(bad));
    bad[32] = 0x08;                 /* the UAS interface descriptor short */
    CHECK_EQ(XhciXportChoose(bad, 85, 0, 0, &x), XHCI_XPORT_MALFORMED,
             "interface descriptor below 9 bytes");

    memcpy(bad, dualCfg, sizeof(bad));
    bad[18] = 0x00;                 /* an endpoint's bLength 0 */
    CHECK_EQ(XhciXportChoose(bad, 85, 0, 0, &x), XHCI_XPORT_MALFORMED,
             "bLength 0");

    choose(dualCfg, sizeof(dualCfg), 0, 0, &x);
    CHECK_EQ(XhciXportId(dualDev, &x, XHCI_XPORT_NO_MI,
                         XHCI_XPORT_ID_COMPATIBLE, buf, 10, &used),
             XHCI_XPORT_TOO_SMALL, "compatible ids past the capacity");
    CHECK_EQ(used, sizeof(ciUas) - 1, "the whole length reported");
    CHECK_EQ(XhciXportId(dualDev, &x, XHCI_XPORT_NO_MI,
                         XHCI_XPORT_ID_COMPATIBLE, NULL, 0, &used),
             XHCI_XPORT_TOO_SMALL, "size query");
    CHECK_EQ(used, sizeof(ciUas) - 1, "size query answer");
    CHECK_EQ(XhciXportId(dualDev, &x, 0x100, XHCI_XPORT_ID_HARDWARE, buf,
                         sizeof(buf), &used),
             XHCI_XPORT_BAD_PARAM, "an MI past two hex digits");
    CHECK_EQ(XhciXportId(dualDev, &x, XHCI_XPORT_NO_MI, 0, buf, sizeof(buf),
                         &used),
             XHCI_XPORT_BAD_PARAM, "the device id is not this module's");
    CHECK_EQ(XhciXportId(dualCfg, &x, XHCI_XPORT_NO_MI,
                         XHCI_XPORT_ID_HARDWARE, buf, sizeof(buf), &used),
             XHCI_XPORT_MALFORMED, "not a device descriptor");
    CHECK_EQ(XhciXportId(dualDev, NULL, XHCI_XPORT_NO_MI,
                         XHCI_XPORT_ID_HARDWARE, buf, sizeof(buf), &used),
             XHCI_XPORT_BAD_PARAM, "no policy");
    CHECK_EQ(XhciXportId(dualDev, &x, XHCI_XPORT_NO_MI,
                         XHCI_XPORT_ID_HARDWARE, buf, sizeof(buf), NULL),
             XHCI_XPORT_BAD_PARAM, "nowhere to say the size");
}

int main(void)
{
    test_dual();
    test_uas_only();
    test_reversed();
    test_untouched();
    test_function();
    test_refused_at();
    test_single();
    test_malformed();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
