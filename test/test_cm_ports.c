/*
 * test_cm_ports.c - the pure half of xhcicm.sys's re-enumeration and safe
 * eject (src/cm/cm_ports.c; roadmap-usb4.md tasks 39-A.5 and 39-A.8;
 * design record 21 sections 3.3, 6.1, 6.3 and 7.4): the families' id
 * lists, checked against the INFs that match them; Windows 98's PCI
 * device ids; the bridge filter's snapshot; the resume hold's bound.
 */

#include <stdio.h>
#include <string.h>
#include "../src/cm/cm_ports.h"
#include "test_harness.h"

#define ID(dev) CM_PORTS_ID(0x8086, (dev))

/* ---- the families ------------------------------------------------------ */

static ULONG inList(const ULONG *list, ULONG count, ULONG id);

static ULONG inFamily(const CM_FAMILY *f, ULONG id)
{
    return inList(f->SwitchPortIds, f->SwitchPortIdCount, id) ||
           inList(f->RootPortIds, f->RootPortIdCount, id);
}

static void test_families(void)
{
    const CM_FAMILY *f;
    const CM_FAMILY *g;
    ULONG n;
    ULONG i;
    ULONG j;
    ULONG k;
    ULONG twice;

    f = CmPortsFamily(ID(0x15BF));
    CHECK(f != NULL, "15BF has a family");
    CHECK(f != NULL && strcmp(f->Name, "alpine-ridge") == 0, "15BF is Alpine Ridge");
    CHECK(f != NULL && f->SwitchPortIdCount == 5, "five Alpine Ridge bridge ids");
    CHECK(f != NULL && f->RootPortIdCount == 0, "a discrete family lists no root port");
    f = CmPortsFamily(ID(0x9A1B));
    CHECK(f != NULL && strcmp(f->Name, "tiger-lake-lp") == 0, "9A1B is Tiger Lake-LP");
    CHECK(f != NULL && f->NhiIdCount == 2, "Tiger Lake-LP has two NHIs");
    CHECK(f != NULL && f->RootPortIdCount == 4, "and four root ports");
    CHECK(f != NULL && f->SwitchPortIdCount == 0, "and no switch");
    f = CmPortsFamily(ID(0x9A1F));
    CHECK(f != NULL && strcmp(f->Name, "tiger-lake-h") == 0,
          "9A1F is Tiger Lake-H, not -LP: a root port is matched beside its own processor's NHI only");
    f = CmPortsFamily(ID(0x7EC2));
    CHECK(f != NULL && f->RootPortIdCount == 0, "Meteor Lake lists no root port (the override is the way)");
    CHECK(CmPortsFamily(ID(0x15DC)) == NULL, "a USB-only Alpine Ridge NHI has no family");
    CHECK(CmPortsFamily(ID(0x15C0)) == NULL, "a bridge id is no NHI");
    CHECK(CmPortsFamily(CM_PORTS_ID(0x1022, 0x1234)) == NULL, "an unlisted AMD id has no family");
    CHECK(CmPortsFamily(CM_PORTS_ID(0x1234, 0x15BF)) == NULL, "the vendor counts");

    n = CmPortsFamilyCount();
    CHECK_EQ(n, 16, "sixteen families: ten of record 21, six of record 22");
    f = CmPortsFamily(CM_PORTS_ID(0x1022, 0x1669));
    CHECK(f != NULL && f->RootPortIdCount == 3 && f->NhiIdCount == 2,
          "AMD's two routers share their root ports (ambiguous: the override assigns them)");
    f = CmPortsFamily(ID(0x5784));
    CHECK(f != NULL && f->SwitchPortIdCount == 2, "Barlow Ridge has its own switch ports");
    CHECK(CmPortsFamilyAt(n) == NULL, "past the table is NULL");
    /* No id twice anywhere, and every family names its NHIs. */
    twice = 0;
    for (i = 0; i < n; i++) {
        f = CmPortsFamilyAt(i);
        CHECK(f->NhiIdCount > 0, "every family has NHIs");
        CHECK(f->SwitchPortIdCount == 0 || f->RootPortIdCount == 0,
              "a family is discrete or integrated, never both");
        for (j = 0; j < CmPortsUnpairedCount(); j++) {
            CHECK(!inFamily(f, CmPortsUnpairedAt(j)), "an unpaired id is no family's");
        }
        for (j = 0; j < n; j++) {
            g = CmPortsFamilyAt(j);
            for (k = 0; k < g->NhiIdCount; k++) {
                if (CmPortsFamily(g->NhiIds[k]) != g) {
                    twice++;
                }
            }
        }
    }
    CHECK_EQ(twice, 0, "every NHI id finds its own family");
}

/*
 * The INFs match exactly record 21 section 3.3's list, and so do the
 * families: every "DEV_xxxx&CC_0604" of xhcicmf.inf is one family's port
 * id and every family port id is in it; every "VEN_8086&DEV_xxxx" of
 * xhcicm.inf's [CmModels] is one family's NHI.
 */
static ULONG hex4(const char *p)
{
    ULONG v;
    int i;
    char c;

    v = 0;
    for (i = 0; i < 4; i++) {
        c = p[i];
        v <<= 4;
        if (c >= '0' && c <= '9') {
            v |= (ULONG)(c - '0');
        } else if (c >= 'A' && c <= 'F') {
            v |= (ULONG)(c - 'A' + 10);
        } else if (c >= 'a' && c <= 'f') {
            v |= (ULONG)(c - 'a' + 10);
        } else {
            return 0xFFFFFFFFUL;
        }
    }
    return v;
}

static char infText[65536];

static ULONG readInf(const char *path)
{
    FILE *fp;
    size_t n;

    fp = fopen(path, "rb");
    if (fp == NULL) {
        return 0;
    }
    n = fread(infText, 1, sizeof(infText) - 1, fp);
    fclose(fp);
    infText[n] = 0;
    return (ULONG)n;
}

static ULONG inList(const ULONG *list, ULONG count, ULONG id)
{
    ULONG i;

    for (i = 0; i < count; i++) {
        if (list[i] == id) {
            return 1;
        }
    }
    return 0;
}

static void test_infs(void)
{
    ULONG seen[64];
    ULONG seenCount;
    ULONG unknown;
    ULONG missing;
    ULONG dev;
    ULONG ven;
    ULONG i;
    ULONG k;
    const char *p;
    const CM_FAMILY *f;
    ULONG known;

    CHECK(readInf("..\\src\\cm\\xhcicmf.inf") != 0, "xhcicmf.inf read");
    seenCount = 0;
    unknown = 0;
    for (p = strstr(infText, "DEV_"); p != NULL; p = strstr(p + 4, "DEV_")) {
        if (strncmp(p + 8, "&CC_0604", 8) != 0) {
            continue;
        }
        dev = hex4(p + 4);
        ven = hex4(p - 5);
        known = 0;
        for (i = 0; i < CmPortsFamilyCount(); i++) {
            if (inFamily(CmPortsFamilyAt(i), CM_PORTS_ID(ven, dev))) {
                known = 1;
            }
        }
        for (i = 0; i < CmPortsUnpairedCount(); i++) {
            if (CmPortsUnpairedAt(i) == CM_PORTS_ID(ven, dev)) {
                known = 1;
            }
        }
        if (!known) {
            unknown++;
            printf("  xhcicmf.inf names %04lX:%04lX, which no family lists\n", ven, dev);
        }
        if (!inList(seen, seenCount, CM_PORTS_ID(ven, dev)) && seenCount < 64) {
            seen[seenCount++] = CM_PORTS_ID(ven, dev);
        }
    }
    CHECK_EQ(unknown, 0, "every bridge id of xhcicmf.inf is a family's");
    missing = 0;
    for (i = 0; i < CmPortsFamilyCount(); i++) {
        f = CmPortsFamilyAt(i);
        for (k = 0; k < f->SwitchPortIdCount; k++) {
            missing += inList(seen, seenCount, f->SwitchPortIds[k]) ? 0 : 1;
        }
        for (k = 0; k < f->RootPortIdCount; k++) {
            missing += inList(seen, seenCount, f->RootPortIds[k]) ? 0 : 1;
        }
    }
    for (k = 0; k < CmPortsUnpairedCount(); k++) {
        missing += inList(seen, seenCount, CmPortsUnpairedAt(k)) ? 0 : 1;
    }
    CHECK_EQ(missing, 0, "every family port id and every unpaired id is in xhcicmf.inf");
    CHECK_EQ(seenCount, 38, "38 bridge ids: record 21's 30, record 22's 6, the emulator's 2");

    CHECK(readInf("..\\src\\cm\\xhcicm.inf") != 0, "xhcicm.inf read");
    seenCount = 0;
    unknown = 0;
    for (p = strstr(infText, "PCI\\VEN_"); p != NULL; p = strstr(p + 8, "PCI\\VEN_")) {
        if (strncmp(p + 12, "&DEV_", 5) != 0) {
            continue;
        }
        ven = hex4(p + 8);
        dev = hex4(p + 17);
        if (CmPortsFamily(CM_PORTS_ID(ven, dev)) == NULL) {
            unknown++;
            printf("  xhcicm.inf names %04lX:%04lX, which no family lists\n", ven, dev);
        }
        if (!inList(seen, seenCount, CM_PORTS_ID(ven, dev)) && seenCount < 64) {
            seen[seenCount++] = CM_PORTS_ID(ven, dev);
        }
    }
    CHECK_EQ(unknown, 0, "every NHI id of xhcicm.inf is a family's");
    missing = 0;
    for (i = 0; i < CmPortsFamilyCount(); i++) {
        f = CmPortsFamilyAt(i);
        for (k = 0; k < f->NhiIdCount; k++) {
            /* The emulator's host is matched by the class line alone. */
            if ((f->NhiIds[k] & 0xFFFFUL) == 0x1B36UL) {
                continue;
            }
            missing += inList(seen, seenCount, f->NhiIds[k]) ? 0 : 1;
        }
    }
    CHECK_EQ(missing, 0, "every family NHI is in xhcicm.inf");
}

/* ---- Windows 98's device ids ----------------------------------------- */

static void test_ids(void)
{
    ULONG v;
    static const char bridge[] =
        "PCI\\VEN_8086&DEV_15C0&SUBSYS_22D817AA&REV_02\\BUS_04&DEV_01&FUNC_00";
    static const char emu[] = "PCI\\VEN_1B36&DEV_000C&SUBSYS_00000000&REV_00\\BUS_00&DEV_06&FUNC_00";
    static const char lower[] = "pci\\ven_8086&dev_15c0\\bus_0a&dev_1f&func_07";
    static const char hot[] = "PCI\\VEN_144D&DEV_A808&SUBSYS_A801144D&REV_00\\082800";

    v = 0;
    CHECK(CmPortsIdLocation(bridge, sizeof(bridge), &v), "the P14s tunnel port's id parses");
    CHECK_EQ(v, (0x04UL << 8) | (0x01UL << 3) | 0, "04:01.0");
    CHECK(CmPortsIdLocation(emu, sizeof(emu), &v), "the emulator's root bridge parses");
    CHECK_EQ(v, (0x06UL << 3), "00:06.0");
    CHECK(CmPortsIdLocation(lower, sizeof(lower), &v), "case is ignored");
    CHECK_EQ(v, (0x0AUL << 8) | (0x1FUL << 3) | 7, "0A:1F.7");
    CHECK(!CmPortsIdLocation(hot, sizeof(hot), &v), "a hot-added instance form is never a location");
    CHECK(!CmPortsIdLocation("PCI\\VEN_8086&DEV_15C0\\BUS_04&DEV_20&FUNC_00", 64, &v),
          "device 20 is refused");
    CHECK(!CmPortsIdLocation("PCI\\VEN_8086&DEV_15C0\\BUS_04&DEV_01&FUNC_08", 64, &v),
          "function 8 is refused");
    CHECK(!CmPortsIdLocation("PCI\\VEN_8086&DEV_15C0\\BUS_104&DEV_01&FUNC_00", 64, &v),
          "a three-digit bus is refused");
    CHECK(!CmPortsIdLocation("PCI\\VEN_8086&DEV_15C0\\BUS_04&DEV_01&FUNC_00X", 64, &v),
          "trailing text is refused");
    CHECK(!CmPortsIdLocation("ROOT\\PCI\\BUS_04&DEV_01&FUNC_00", 64, &v),
          "an enumerator other than PCI is refused");
    CHECK(!CmPortsIdLocation("PCI\\VEN_8086&DEV_15C0", 64, &v), "no instance part is refused");
    CHECK(!CmPortsIdLocation(bridge, 20, &v), "the length bounds the id");
    CHECK(!CmPortsIdLocation(NULL, 64, &v), "NULL is refused");

    {
        ULONG path[CM_PORTS_PATH_MAX];
        ULONG n;
        static const char port[] =
            "PCI\\VEN_8086&DEV_15C0&SUBSYS_00000000&REV_01\\08083000";
        static const char nhi[] =
            "PCI\\VEN_8086&DEV_15BF&SUBSYS_22B017AA&REV_01\\0000083000";

        n = 0;
        CHECK(CmPortsIdPath(port, sizeof(port), path, CM_PORTS_PATH_MAX, &n),
              "98 SE's path form parses (02:01.0, read on the emulator)");
        CHECK(n == 4 && path[0] == 0x08 && path[1] == 0x08 && path[2] == 0x30 &&
              path[3] == 0x00,
              "02:01.0: its devfn 08, the upstream port's 08, the root bridge's 30, root bus 00");
        CHECK(CmPortsIdPath(nhi, sizeof(nhi), path, CM_PORTS_PATH_MAX, &n) && n == 5 &&
              path[0] == 0x00 && path[1] == 0x00, "03:00.0 behind 02:00.0");
        CHECK(CmPortsIdPath(hot, sizeof(hot), path, CM_PORTS_PATH_MAX, &n) && n == 3 &&
              path[0] == 0x08 && path[1] == 0x28,
              "the experiment's hot-added 082800 is the same form");
        CHECK(!CmPortsIdPath(bridge, sizeof(bridge), path, CM_PORTS_PATH_MAX, &n),
              "the BUS_ form is not a path");
        CHECK(!CmPortsIdPath("PCI\\VEN_8086&DEV_15C0\\08083", 64, path, CM_PORTS_PATH_MAX, &n),
              "an odd digit count is refused");
        CHECK(!CmPortsIdPath("PCI\\VEN_8086&DEV_15C0\\08", 64, path, CM_PORTS_PATH_MAX, &n),
              "one element is refused");
        CHECK(!CmPortsIdPath("PCI\\VEN_8086&DEV_15C0\\0808300G", 64, path, CM_PORTS_PATH_MAX, &n),
              "a non-digit is refused");
        CHECK(!CmPortsIdPath(port, sizeof(port), path, 3, &n),
              "a path longer than the room is refused");
    }
    CHECK(CmPortsIdVenDev(hot, sizeof(hot), &v), "the NVMe's vendor and device parse");
    CHECK_EQ(v, CM_PORTS_ID(0x144D, 0xA808), "144D:A808");
    CHECK(CmPortsIdVenDev(lower, sizeof(lower), &v), "case is ignored");
    CHECK_EQ(v, ID(0x15C0), "8086:15C0");
    CHECK(!CmPortsIdVenDev("PCI\\VEN_8086&DEV_15C", 64, &v), "three digits are refused");
    CHECK(!CmPortsIdVenDev("PCI\\VEN_80861&DEV_15C0", 64, &v), "five digits are refused");
    CHECK(!CmPortsIdVenDev("USB\\VEN_8086&DEV_15C0", 64, &v), "not PCI is refused");
    CHECK(!CmPortsIdVenDev("HTREE\\ROOT\\0", 64, &v), "the root is no PCI id");
}

/* ---- the snapshot ------------------------------------------------------ */

static void test_snapshot(void)
{
    int a;
    int b;
    int c;
    int d;
    PVOID old[3];
    PVOID now[3];
    ULONG isNew[3];

    old[0] = &a;
    old[1] = &b;
    now[0] = &b;
    now[1] = &c;
    now[2] = &d;
    CHECK_EQ(CmPortsNewPdos((PVOID const *)old, 2, (PVOID const *)now, 3, isNew), 2,
             "two new PDOs");
    CHECK(isNew[0] == 0 && isNew[1] == 1 && isNew[2] == 1, "b was there; c and d are new");
    CHECK_EQ(CmPortsNewPdos((PVOID const *)old, 0, (PVOID const *)now, 3, isNew), 3,
             "against an empty snapshot every PDO is new (the baseline attaches nothing)");
    CHECK_EQ(CmPortsNewPdos((PVOID const *)old, 2, (PVOID const *)now, 0, isNew), 0,
             "an empty answer has nothing new");
    CHECK_EQ(CmPortsNewPdos((PVOID const *)now, 3, (PVOID const *)now, 3, isNew), 0,
             "the same answer twice has nothing new");
}

/* ---- the hold's bound -------------------------------------------------- */

static ULONG bound(ULONG readyMs, ULONG readyRetries, ULONG devices, ULONG cmdMs,
                   ULONG approveMs, ULONG retries)
{
    CM_PORTS_RESUME r;

    r.ReadyMs = readyMs;
    r.ReadyRetries = readyRetries;
    r.HostPollTries = 50;
    r.HostPollMs = 100;
    r.ReconcileMs = 5000;
    r.Devices = devices;
    r.CmdMs = cmdMs;
    r.ApproveMs = approveMs;
    r.Retries = retries;
    return CmPortsHoldBound(&r);
}

static void test_bound(void)
{
    CM_PORTS_RESUME r;

    /* Alpine Ridge, no device: the platform's 5 s, four 5 s driver-ready
     * attempts, the 50 x 100 ms host-router poll, the reconciliation's
     * 5 s, plus 10 s. */
    CHECK_EQ(bound(5000, 3, 0, 5000, 10000, 3), 5000 + 20000 + 5000 + 5000 + 10000,
             "the Alpine Ridge bound with no device");
    /* One device re-announced and approved: its route query and its approve,
     * each over four attempts. */
    CHECK_EQ(bound(5000, 3, 1, 5000, 10000, 3),
             5000 + 20000 + 5000 + 5000 + 4 * 5000 + 4 * 10000 + 10000,
             "one device adds its command and its approve, with their retries");
    /* The case a short bound lost: XhciCmApproveTimeoutMs 60000 - an approve
     * that succeeds at 45 s is inside the bound. */
    CHECK(bound(5000, 3, 1, 5000, 60000, 3) > 45000UL + 5000 + 20000 + 5000,
          "a 60 s approve that answers at 45 s is inside the bound");
    /* Ice Lake: 20 s attempts. */
    CHECK_EQ(bound(20000, 2, 0, 5000, 10000, 3), 5000 + 60000 + 5000 + 5000 + 10000,
             "a 20 s row's bound");
    r.ReadyMs = 0;
    r.ReadyRetries = 0;
    r.HostPollTries = 0;
    r.HostPollMs = 0;
    r.ReconcileMs = 0;
    r.Devices = 0;
    r.CmdMs = 0;
    r.ApproveMs = 0;
    r.Retries = 0;
    CHECK_EQ(CmPortsHoldBound(&r), CM_PORTS_HOLD_PLATFORM_MS + CM_PORTS_HOLD_EXTRA_MS,
             "nothing but the platform's 5 s and the 10 s");
    CHECK_EQ(bound(0xFFFFFFFFUL, 0xFFFFFFFFUL, 0, 0, 0, 0), CM_PORTS_HOLD_MAX_MS,
             "a huge timeout is held to the cap, without overflow");
    /* The defaults with no device stay below the cap; sixteen devices at
     * the longest timeouts are held to the operating system's limit. */
    CHECK(bound(5000, 3, 0, 5000, 10000, 3) < CM_PORTS_HOLD_MAX_MS,
          "the Alpine Ridge bound with no device is below the cap");
    CHECK_EQ(bound(60000, 3, 16, 60000, 60000, 3), CM_PORTS_HOLD_MAX_MS,
             "sixteen devices at the longest timeouts are held to five minutes");
    CHECK(CM_PORTS_HOLD_MAX_MS <= 300000UL,
          "the cap stays below the power-IRP watchdog's ten minutes");
    r.ReadyMs = 0xFFFFFFFFUL;
    r.ReadyRetries = 0xFFFFFFFFUL;
    r.HostPollTries = 0xFFFFFFFFUL;
    r.HostPollMs = 0xFFFFFFFFUL;
    r.ReconcileMs = 0xFFFFFFFFUL;
    r.Devices = 0xFFFFFFFFUL;
    r.CmdMs = 0xFFFFFFFFUL;
    r.ApproveMs = 0xFFFFFFFFUL;
    r.Retries = 0xFFFFFFFFUL;
    CHECK_EQ(CmPortsHoldBound(&r), CM_PORTS_HOLD_MAX_MS, "every term at its maximum is the cap");
}

static void test_keep_present(void)
{
    ULONG known[4];
    ULONG present[3];

    /* A child removed by the user (handle 0x20) is forgotten at the next
     * walk, so its handle, reused by a re-plug, reads as new. */
    known[0] = 0x10; known[1] = 0x20; known[2] = 0x30;
    present[0] = 0x30; present[1] = 0x10;
    CHECK_EQ(CmPortsKeepPresent(known, 3, present, 2), 2, "the absent one dropped");
    CHECK_EQ(known[0], 0x10, "order kept");
    CHECK_EQ(known[1], 0x30, "order kept (2)");
    CHECK_EQ(CmPortsKeepPresent(known, 2, present, 0), 0, "none present: none kept");
    known[0] = 0x40;
    present[0] = 0x40;
    CHECK_EQ(CmPortsKeepPresent(known, 1, present, 1), 1, "present: kept");
    CHECK_EQ(CmPortsKeepPresent(known, 0, present, 1), 0, "an empty set stays empty");
}

static void test_watchdog(void)
{
    BOOLEAN over;

    over = TRUE;
    CHECK_EQ(CmPortsHoldWatchdog(0, &over), CM_PORTS_HOLD_MAX_MS, "no client bound: the cap");
    CHECK(!over, "and not over");
    CHECK_EQ(CmPortsHoldWatchdog(60000UL, &over), CM_PORTS_HOLD_MAX_MS,
             "a short USB4 bound: still the fixed cap, never shorter");
    CHECK(!over, "60 s + 10 s inside the cap");
    CHECK_EQ(CmPortsHoldWatchdog(290000UL, &over), CM_PORTS_HOLD_MAX_MS, "290 s: the cap");
    CHECK(!over, "290 s + 10 s is the cap exactly");
    CHECK_EQ(CmPortsHoldWatchdog(290001UL, &over), CM_PORTS_HOLD_MAX_MS,
             "past it: the cap, never more (the owner's outer limit)");
    CHECK(over, "and reported over");
    CHECK_EQ(CmPortsHoldWatchdog(0xFFFFFFFFUL, &over), CM_PORTS_HOLD_MAX_MS, "no wrap");
    CHECK(over, "over");
}

int main(void)
{
    test_families();
    test_infs();
    test_ids();
    test_snapshot();
    test_bound();
    test_watchdog();
    test_keep_present();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
