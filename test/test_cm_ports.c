/*
 * test_cm_ports.c - the pure half of xhcicm.sys's re-enumeration and safe
 * eject (src/cm/cm_ports.c; roadmap-usb4.md tasks 39-A.5 and 39-A.8;
 * design record 21 sections 3.3, 6.1, 6.3 and 7.4): the families' id
 * lists, checked against the INFs that match them; Windows 98's PCI
 * device ids; the bridge filter's snapshot; the resume hold's bound; and
 * design record 31's tunnel id: the compatible-id rewrite, the chain's
 * depth bound and its re-attach; design record 34 section 3.3.7 item 1's
 * admission of a chain operation against a loss posted in between; and the
 * kept child's systems, state and reference rule (record 31 section 4.10).
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
    CHECK_EQ(n, 23, "23 families: ten of record 21, nine of record 22 (section 19: Light and Cactus Ridge), four of task 46.23");
    f = CmPortsFamily(ID(0x1513));
    CHECK(f != NULL && f->SwitchPortIdCount == 1 && f->SwitchPortIds[0] == ID(0x1513),
          "Light Ridge: its switch ports carry the NHI's id (record 22 section 19.4 item 4)");
    f = CmPortsFamily(CM_PORTS_ID(0x1022, 0x1669));
    CHECK(f != NULL && f->RootPortIdCount == 3 && f->NhiIdCount == 2,
          "AMD's two routers share their root ports (ambiguous: the override assigns them)");
    /* Task 46.23: AMD's other integrated hosts, by processor, each with
     * the PCIe USB4 bridge pci.ids names for it. */
    {
        static const struct {
            ULONG Nhi;
            const char *Name;
            ULONG Nhis;
            ULONG Root;
        } amd[] = {
            { 0x162E, "amd-rembrandt", 2, 0x14CD }, { 0x162F, "amd-rembrandt", 2, 0x14CD },
            { 0x15C4, "amd-phoenix", 2, 0x14EF },   { 0x15C5, "amd-phoenix", 2, 0x14EF },
            { 0x151C, "amd-strix", 4, 0x150A },     { 0x151D, "amd-strix", 4, 0x150A },
            { 0x158D, "amd-strix", 4, 0x150A },     { 0x158E, "amd-strix", 4, 0x150A },
            { 0x1120, "amd-krackan", 4, 0x1125 },   { 0x1121, "amd-krackan", 4, 0x1125 },
            { 0x113B, "amd-krackan", 4, 0x1125 },   { 0x113C, "amd-krackan", 4, 0x1125 }
        };
        ULONG a;
        for (a = 0; a < sizeof(amd) / sizeof(amd[0]); a++) {
            f = CmPortsFamily(CM_PORTS_ID(0x1022, amd[a].Nhi));
            CHECK(f != NULL && strcmp(f->Name, amd[a].Name) == 0 &&
                  f->NhiIdCount == amd[a].Nhis && f->SwitchPortIdCount == 0 &&
                  f->RootPortIdCount == 1 &&
                  f->RootPortIds[0] == CM_PORTS_ID(0x1022, amd[a].Root),
                  "an AMD host: its processor's family and its one root port id");
        }
        CHECK(CmPortsFilterId(CM_PORTS_ID(0x1022, 0x1125)), "the filter takes Krackan's 1125");
        CHECK(CmPortsAcpiPort(CM_PORTS_ID(0x1022, 0x150A)) &&
              CmPortsAcpiPort(CM_PORTS_ID(0x1022, 0x14CD)) &&
              CmPortsAcpiPort(CM_PORTS_ID(0x1022, 0x1125)),
              "an AMD family's root port reads _DSD (record 22 section 7.6.1)");
        CHECK(!CmPortsAcpiPort(CM_PORTS_ID(0x8086, 0x9A23)) &&
              !CmPortsAcpiPort(CM_PORTS_ID(0x1B36, 0x000C)) &&
              !CmPortsAcpiPort(CM_PORTS_ID(0x1B21, 0x2423)) &&
              !CmPortsAcpiPort(CM_PORTS_ID(0x1022, 0x158D)),
              "an Intel, emulator or ASMedia port, or an NHI, does not");
        CHECK(CmPortsFamily(CM_PORTS_ID(0x1022, 0x1125)) == NULL, "1125 is no NHI");
    }
    f = CmPortsFamily(ID(0x5784));
    CHECK(f != NULL && f->SwitchPortIdCount == 2, "Barlow Ridge has its own switch ports");
    /* Task 44.40, record 22 section 18: the ASM4242's NHI, matched by the
     * INFs' class line alone, pairs with its own switch's ports. */
    f = CmPortsFamily(CM_PORTS_ID(0x1B21, 0x2425));
    CHECK(f != NULL && strcmp(f->Name, "asmedia-asm4242") == 0, "1B21:2425 is the ASM4242");
    CHECK(f != NULL && f->NhiIdCount == 1 && f->SwitchPortIdCount == 1 &&
          f->SwitchPortIds[0] == CM_PORTS_ID(0x1B21, 0x2423) && f->RootPortIdCount == 0,
          "the ASM4242: its downstream port 2423, no root port");
    CHECK(CmPortsFamily(CM_PORTS_ID(0x1B21, 0x2423)) == NULL, "the ASM4242's port is no NHI");
    CHECK(CmPortsFamily(CM_PORTS_ID(0x1B21, 0x2426)) == NULL, "nor is its xHCI");
    CHECK(CmPortsFilterId(CM_PORTS_ID(0x1B21, 0x2423)), "the filter still takes 2423");
    CHECK_EQ(CmPortsUnpairedCount(), 1, "one unpaired id left, the emulator's bridge");
    CHECK_EQ(CmPortsUnpairedAt(0), CM_PORTS_ID(0x1B36, 0x0001), "1B36:0001");
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
    /* A file that fills the buffer may be longer than it: refused, so a
     * truncated INF is never checked as if whole. */
    if (n == sizeof(infText) - 1) {
        return 0;
    }
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
    ULONG usp;

    CHECK(readInf("..\\src\\cm\\xhcicmf.inf") != 0, "xhcicmf.inf read");
    seenCount = 0;
    unknown = 0;
    usp = 0;
    for (p = strstr(infText, "DEV_"); p != NULL; p = strstr(p + 4, "DEV_")) {
        if (strncmp(p + 8, "&CC_0604", 8) != 0) {
            continue;
        }
        dev = hex4(p + 4);
        ven = hex4(p - 5);
        if (CM_PORTS_ID(ven, dev) == CM_PORTS_ID(0x1B21, 0x2421)) {
            /* The ASM4242's upstream port: the mailbox's member (record 32
             * section 12.8.13.4), never a tunnel port. */
            usp++;
            continue;
        }
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
    missing = 0;
    for (i = 0; i < seenCount; i++) {
        missing += CmPortsFilterId(seen[i]) ? 0 : 1;
    }
    CHECK_EQ(missing, 0, "CmPortsFilterId takes every bridge id of xhcicmf.inf (task 42-A.5)");
    CHECK_EQ(usp, 2, "the ASM4242's upstream port named once in each models section");
    CHECK(!CmPortsFilterId(CM_PORTS_ID(0x1B21, 0x2421)),
          "and not that upstream port, which is no tunnel port");
    CHECK(!CmPortsFilterId(CM_PORTS_ID(0x8086, 0x15BF)), "nor an NHI");
    CHECK_EQ(seenCount, 41, "41 bridge ids: record 21's 30, record 22's 8, task 46.23's 1, the emulator's 2");

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
            /* The emulator's host and the ASM4242 are matched by the
             * class line alone (task 44.40 left the INFs unchanged). */
            if ((f->NhiIds[k] & 0xFFFFUL) == 0x1B36UL ||
                f->NhiIds[k] == CM_PORTS_ID(0x1B21, 0x2425)) {
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

/* ---- the population generation an eject is checked against (43.70) ---- */

static void test_generation(void)
{
    int a;
    int b;
    int c;
    PVOID old[2];
    PVOID now[2];
    ULONG isNew[2];
    ULONG fresh;

    old[0] = &a;
    old[1] = &b;
    /* the ejected function leaving after Windows turned the slot off */
    now[0] = &a;
    fresh = CmPortsNewPdos((PVOID const *)old, 2, (PVOID const *)now, 1, isNew);
    CHECK_EQ(CmPortsNewGeneration(fresh, 2, 1), 0,
             "a departure alone keeps the generation");
    CHECK_EQ(CmPortsNewGeneration(0, 1, 0), 0,
             "the last function leaving keeps the generation");
    CHECK_EQ(CmPortsNewGeneration(0, 2, 2), 0,
             "the same population keeps the generation");
    /* a replacement: the old one gone, a new one in its place */
    now[0] = &a;
    now[1] = &c;
    fresh = CmPortsNewPdos((PVOID const *)old, 2, (PVOID const *)now, 2, isNew);
    CHECK_EQ(CmPortsNewGeneration(fresh, 2, 2), 1,
             "a new function in a departed one's place starts a new generation");
    CHECK_EQ(CmPortsNewGeneration(1, 0, 1), 1,
             "a first function starts a new generation");
    CHECK_EQ(CmPortsNewGeneration(0, 1, 2), 1,
             "a longer answer with nothing new (a PDO twice) starts one too");

    CHECK_EQ(CmPortsStaleWhy(0, 0, 0, 0, 0), 0, "nothing refused, generations 0");
    CHECK_EQ(CmPortsStaleWhy(0, 1, 0, 5, 4), CM_EJ_STALE_GEN | 0x0500UL | 0x040000UL,
             "the generation check, both generations recorded");
    CHECK_EQ(CmPortsStaleWhy(1, 0, 1, 0x1FF, 0x2AB),
             CM_EJ_STALE_SERIAL | CM_EJ_STALE_STACK | 0xFF00UL | 0xAB0000UL,
             "the publication and the stack checks, generations truncated to a byte");
}

/* ---- the child filter's attach decision (section 7.4, revision 2) ------ */

static void test_child_attach(void)
{
    int pci;
    int acpi;
    int cm;
    int other;
    PVOID chain[CM_CHILD_CHAIN_MAX + 1];
    ULONG i;

    CHECK_EQ(CmPortsStackless(0, 0, 0, 0), 1,
             "stackless: nothing attached (an eject removed the stack)");
    CHECK_EQ(CmPortsStackless(1, 1, 0, 0), 1,
             "stackless: only the BusRelations child, nothing above it (no driver, "
             "Code 28)");
    CHECK_EQ(CmPortsStackless(1, 1, 0, 1), 0,
             "stackless: a function driver above the child is a stack");
    CHECK_EQ(CmPortsStackless(1, 1, 1, 0), 0,
             "stackless: an AddDevice-made object of this driver's is a stack");
    CHECK_EQ(CmPortsStackless(1, 0, 1, 0), 0,
             "stackless: an AddDevice-made object alone is a stack");
    CHECK_EQ(CmPortsStackless(1, 0, 0, 0), 0,
             "stackless: another driver's objects and no child of this one's: a "
             "stack, not read");

    CHECK_EQ(CmPortsChildFirst(0, 0, 1, 0), CM_CHILD_ATTACH,
             "the first step: nothing attached attaches with no walk");
    CHECK_EQ(CmPortsChildFirst(1, 0, 1, 0), CM_CHILD_ATTACH,
             "the first step: nothing attached, truncated or not");
    CHECK_EQ(CmPortsChildFirst(0, 1, 1, 0), CM_CHILD_WALK,
             "the first step: something attached after a whole snapshot: walk it");
    CHECK_EQ(CmPortsChildFirst(1, 1, 1, 0), CM_CHILD_SKIP,
             "the first step: something attached after a truncated snapshot: skip "
             "without touching another driver's objects");

    /* Record 31 section 4.3: the re-attach of a PDO already in the
     * snapshot whose child was lost to a rebuild of its stack. */
    CHECK_EQ(CmPortsChildFirst(0, 0, 0, 0), CM_CHILD_ATTACH,
             "re-attach: in the snapshot, nothing on it and none of ours: attach again");
    CHECK_EQ(CmPortsChildFirst(1, 0, 0, 0), CM_CHILD_ATTACH,
             "re-attach: the same after a truncated snapshot (it was kept in it)");
    CHECK_EQ(CmPortsChildFirst(0, 0, 0, 1), CM_CHILD_KEEP,
             "re-attach: our child still there: nothing");
    CHECK_EQ(CmPortsChildFirst(0, 1, 0, 1), CM_CHILD_KEEP,
             "re-attach: our child under a built stack: nothing");
    CHECK_EQ(CmPortsChildFirst(0, 1, 0, 0), CM_CHILD_KEEP,
             "re-attach: a stack already built without ours: never walked, nothing");
    CHECK_EQ(CmPortsChildFirst(1, 1, 0, 0), CM_CHILD_KEEP,
             "re-attach: a built stack after a truncated snapshot: nothing");

    CHECK_EQ(CmPortsChildAttach(0, (PVOID const *)chain, 0, &pci, &cm), CM_CHILD_ATTACH,
             "nothing attached: directly above the PDO, as revision 1 did");
    CHECK_EQ(CmPortsChildAttach(1, (PVOID const *)chain, 0, &pci, &cm), CM_CHILD_ATTACH,
             "nothing attached: a truncated snapshot changes nothing");

    chain[0] = &acpi;
    CHECK_EQ(CmPortsChildAttach(0, (PVOID const *)chain, 1, &pci, &cm), CM_CHILD_ATTACH_OVER,
             "a bus filter of another driver (ACPI on a PCI-to-PCI bridge): above it");
    CHECK_EQ(CmPortsChildAttach(1, (PVOID const *)chain, 1, &pci, &cm), CM_CHILD_SKIP,
             "after a truncated snapshot 'new' is unreliable: nothing goes above anything");

    chain[0] = &pci;
    CHECK_EQ(CmPortsChildAttach(0, (PVOID const *)chain, 1, &pci, &cm), CM_CHILD_SKIP,
             "an object of the PDO's own driver (pci.sys, a bridge's FDO): a built stack");
    chain[0] = &acpi;
    chain[1] = &pci;
    CHECK_EQ(CmPortsChildAttach(0, (PVOID const *)chain, 2, &pci, &cm), CM_CHILD_SKIP,
             "pci.sys anywhere in the chain: skipped");

    chain[1] = &cm;
    CHECK_EQ(CmPortsChildAttach(0, (PVOID const *)chain, 2, &pci, &cm), CM_CHILD_SKIP,
             "this driver's own object already there: never a second child");
    chain[1] = NULL;
    CHECK_EQ(CmPortsChildAttach(0, (PVOID const *)chain, 2, &pci, &cm), CM_CHILD_SKIP,
             "an object without a driver: skipped");

    chain[1] = &other;
    CHECK_EQ(CmPortsChildAttach(0, (PVOID const *)chain, 2, &pci, &cm), CM_CHILD_ATTACH_OVER,
             "two bus filters of other drivers: above both");
    for (i = 0; i < CM_CHILD_CHAIN_MAX; i++) {
        chain[i] = &acpi;
    }
    CHECK_EQ(CmPortsChildAttach(0, (PVOID const *)chain, CM_CHILD_CHAIN_MAX, &pci, &cm),
             CM_CHILD_ATTACH_OVER, "a chain at the walk's limit: above it");
    chain[CM_CHILD_CHAIN_MAX] = &acpi;
    CHECK_EQ(CmPortsChildAttach(0, (PVOID const *)chain, CM_CHILD_CHAIN_MAX + 1, &pci, &cm),
             CM_CHILD_SKIP, "a chain longer than the walk: skipped");
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

/* Task 43.24h: a marked member is released and invalidated whatever the
 * resume's outcome (record 21 section 6.3). */
static void test_release(void)
{
    CHECK_EQ(CmPortsRelease(1, 1), CM_PORTS_REL_CLEAR | CM_PORTS_REL_INVALIDATE,
             "marked, re-enumeration on: cleared and invalidated, whatever the resume did");
    CHECK_EQ(CmPortsRelease(1, 0), CM_PORTS_REL_CLEAR,
             "re-enumeration off (XhciCmReenum 0): cleared, no invalidation");
    CHECK_EQ(CmPortsRelease(0, 1), 0, "not marked by this NHI: left alone");
    CHECK_EQ(CmPortsRelease(0, 0), 0, "neither: nothing");
    CHECK_EQ(CmPortsRelease(1, 7), CM_PORTS_REL_CLEAR | CM_PORTS_REL_INVALIDATE,
             "any nonzero XhciCmReenum invalidates");
    CHECK_EQ(CmPortsRelease(7, 1), CM_PORTS_REL_CLEAR | CM_PORTS_REL_INVALIDATE,
             "any nonzero mark counts");
}

/* ---- the tunnel id (design record 31 sections 3 and 4) ----------------- */

static USHORT tidIn[CM_TID_MAX_CHARS + 64];
static USHORT tidOut[CM_TID_MAX_CHARS + 128];

/* A MULTI_SZ from `strs`, '|' between strings, into tidIn; its length. */
static ULONG tidMake(const char *strs)
{
    ULONG n = 0;
    ULONG i;

    if (strs[0] == 0) {
        tidIn[0] = 0;
        return 1;
    }
    for (i = 0; strs[i] != 0; i++) {
        tidIn[n++] = (USHORT)(strs[i] == '|' ? 0 : (UCHAR)strs[i]);
    }
    tidIn[n++] = 0;
    tidIn[n++] = 0;
    return n;
}

/* 1 when tidOut[0..chars) is the MULTI_SZ of `strs`. */
static int tidIs(ULONG chars, const char *strs)
{
    ULONG i;
    ULONG n = 0;

    for (i = 0; strs[i] != 0; i++, n++) {
        if (n >= chars ||
            tidOut[n] != (USHORT)(strs[i] == '|' ? 0 : (UCHAR)strs[i])) {
            return 0;
        }
    }
    return n + 2 == chars && tidOut[n] == 0 && tidOut[n + 1] == 0;
}

#define NVME_HW "PCI\\VEN_1B36&DEV_0010&SUBSYS_11001AF4&REV_02|PCI\\VEN_1B36&DEV_0010"
#define NVME_CC "PCI\\VEN_1B36&DEV_0010&REV_02|PCI\\VEN_1B36&DEV_0010|" \
                "PCI\\VEN_1B36&CC_010802|PCI\\VEN_1B36&CC_0108|PCI\\VEN_1B36|" \
                "PCI\\CC_010802|PCI\\CC_0108"

static void test_tunnel_ids(void)
{
    ULONG len;
    ULONG chars;
    ULONG kind;
    ULONG i;
    ULONG v;

    CHECK_EQ(sizeof(CM_TUNNEL_ID), CM_TID_ID_CHARS, "the id's length with its NUL");
    CHECK(strstr(CM_TUNNEL_ID, "NVMe") == NULL && strstr(CM_TUNNEL_ID, "NVME") == NULL,
          "no NVMe word in the id (Stor98's naming rule)");

    len = tidMake(NVME_CC);
    v = CmPortsTunnelIds(tidIn, NULL, 0, &chars, &kind);
    CHECK_EQ(v, CM_TID_TAG, "an NVMe function's compatible ids are tagged");
    CHECK_EQ(chars, len + CM_TID_ID_CHARS, "the new length: the id and its NUL more");
    CHECK_EQ(kind, CM_TID_KIND_NVME, "the kind: NVMe, no bridge");
    for (i = 0; i < sizeof(tidOut) / sizeof(tidOut[0]); i++) {
        tidOut[i] = 0xEEEE;
    }
    v = CmPortsTunnelIds(tidIn, tidOut, chars - 1, &chars, &kind);
    CHECK_EQ(v, CM_TID_TAG, "a buffer one short: still TAG");
    CHECK_EQ(tidOut[0], 0xEEEE, "and nothing written into it");
    v = CmPortsTunnelIds(tidIn, tidOut, chars, &chars, &kind);
    CHECK_EQ(v, CM_TID_TAG, "a buffer that holds it: TAG");
    CHECK(tidIs(chars, "PCI\\XHCI98_TUNNEL&CC_010802|" NVME_CC),
          "the id first, every old string after it in its order, the double terminator");
    CHECK_EQ(tidOut[chars], 0xEEEE, "not a character past the new list");

    /* Idempotent: the new list again is left. */
    for (i = 0; i < chars; i++) {
        tidIn[i] = tidOut[i];
    }
    v = CmPortsTunnelIds(tidIn, NULL, 0, &chars, &kind);
    CHECK_EQ(v, CM_TID_PRESENT, "an answer that has the id already is left");
    CHECK_EQ(kind, CM_TID_KIND_NVME, "and still read as NVMe");
    len = tidMake("pci\\xhci98_tunnel&cc_010802|PCI\\CC_010802");
    v = CmPortsTunnelIds(tidIn, NULL, 0, &chars, &kind);
    CHECK_EQ(v, CM_TID_PRESENT, "the id in another case is the id");
    CHECK_EQ(chars, len, "the old length when left");

    /* Not the class: left. */
    len = tidMake("PCI\\VEN_8086&DEV_15C0&REV_02|PCI\\VEN_8086&CC_060400|"
                  "PCI\\VEN_8086&CC_0604|PCI\\VEN_8086|PCI\\CC_060400|PCI\\CC_0604");
    v = CmPortsTunnelIds(tidIn, NULL, 0, &chars, &kind);
    CHECK_EQ(v, CM_TID_LEAVE, "a bridge's answer is left");
    CHECK_EQ(kind, CM_TID_KIND_BRIDGE, "and read as a bridge (PCI\\CC_0604)");
    CHECK_EQ(chars, len, "with its own length");
    tidMake("PCI\\VEN_1B36&DEV_000D&REV_01|PCI\\VEN_1B36&CC_0C0330|PCI\\CC_0C0330|"
            "PCI\\CC_0C03");
    v = CmPortsTunnelIds(tidIn, NULL, 0, &chars, &kind);
    CHECK_EQ(v, CM_TID_LEAVE, "a USB controller's answer is left");
    CHECK_EQ(kind, 0, "neither NVMe nor a bridge");
    tidMake("PCI\\VEN_8086&CC_0108020|PCI\\CC_0108020|PCI\\CC_01080|XPCI\\CC_010802");
    v = CmPortsTunnelIds(tidIn, NULL, 0, &chars, &kind);
    CHECK_EQ(v, CM_TID_LEAVE, "only the whole string PCI\\CC_010802 is the class");
    tidMake("PCI\\CC_06040|PCI\\CC_060");
    (VOID)CmPortsTunnelIds(tidIn, NULL, 0, &chars, &kind);
    CHECK_EQ(kind, 0, "only the whole string PCI\\CC_0604 is a bridge");

    len = tidMake("");
    v = CmPortsTunnelIds(tidIn, NULL, 0, &chars, &kind);
    CHECK_EQ(v, CM_TID_LEAVE, "an empty list is left");
    CHECK_EQ(chars, 1, "an empty list is one NUL");
    v = CmPortsTunnelIds(NULL, NULL, 0, &chars, &kind);
    CHECK_EQ(v, CM_TID_LEAVE, "a null buffer is left");
    CHECK_EQ(chars, 0, "a null buffer has no length");
    CHECK_EQ(kind, 0, "and no kind");

    /* Case, and the class as the last string. */
    len = tidMake("pci\\ven_1b36&dev_0010|pci\\cc_010802");
    v = CmPortsTunnelIds(tidIn, tidOut, sizeof(tidOut) / sizeof(tidOut[0]), &chars, &kind);
    CHECK_EQ(v, CM_TID_TAG, "case is ignored, and the class may be the last string");
    CHECK(tidIs(chars, "PCI\\XHCI98_TUNNEL&CC_010802|pci\\ven_1b36&dev_0010|pci\\cc_010802"),
          "the id in its own case, then the old strings untouched");
    len = tidMake("PCI\\CC_010802");
    v = CmPortsTunnelIds(tidIn, tidOut, sizeof(tidOut) / sizeof(tidOut[0]), &chars, &kind);
    CHECK(v == CM_TID_TAG && tidIs(chars, "PCI\\XHCI98_TUNNEL&CC_010802|PCI\\CC_010802"),
          "the class alone gains the id");

    /* The size bound: a list ending exactly at the bound is read, one
     * character more is over, and a list with no terminator is over. */
    for (i = 0; i < CM_TID_MAX_CHARS; i++) {
        tidIn[i] = 'A';
    }
    for (i = 0; i < 13; i++) {
        tidIn[i] = (USHORT)"PCI\\CC_010802"[i];
    }
    tidIn[13] = 0;
    tidIn[CM_TID_MAX_CHARS - 2] = 0;
    tidIn[CM_TID_MAX_CHARS - 1] = 0;
    v = CmPortsTunnelIds(tidIn, NULL, 0, &chars, &kind);
    CHECK_EQ(v, CM_TID_TAG, "a list ending at the bound is read");
    CHECK_EQ(chars, CM_TID_MAX_CHARS + CM_TID_ID_CHARS, "and its new length");
    v = CmPortsTunnelIds(tidIn, tidOut, sizeof(tidOut) / sizeof(tidOut[0]), &chars, &kind);
    CHECK(v == CM_TID_TAG && tidOut[CM_TID_ID_CHARS] == 'P' &&
              tidOut[chars - 1] == 0 && tidOut[chars - 2] == 0,
          "and built whole");
    tidIn[CM_TID_MAX_CHARS - 2] = 'A';
    tidIn[CM_TID_MAX_CHARS - 1] = 0;
    tidIn[CM_TID_MAX_CHARS] = 0;
    v = CmPortsTunnelIds(tidIn, NULL, 0, &chars, &kind);
    CHECK_EQ(v, CM_TID_OVER, "one character past the bound: over, left");
    CHECK_EQ(chars, 0, "over: no length");
    CHECK_EQ(kind, 0, "over: no kind (nothing is trusted of it)");
    tidIn[CM_TID_MAX_CHARS - 1] = 'A';
    tidIn[CM_TID_MAX_CHARS] = 'A';
    v = CmPortsTunnelIds(tidIn, NULL, 0, &chars, &kind);
    CHECK_EQ(v, CM_TID_OVER, "no terminator within the bound: over, never read past it");
}

static void test_chain_depth(void)
{
    ULONG d;
    ULONG n;

    CHECK_EQ(CM_TUNNEL_DEPTH_MAX, 8, "the depth bound is 8 (record 31 section 4.3)");
    n = 0;
    for (d = 0; d <= 10; d++) {
        n += CmPortsChainReads(d, 1);
    }
    CHECK_EQ(n, CM_TUNNEL_DEPTH_MAX, "a bridge reads its BusRelations at depths 0 to 7 alone");
    CHECK_EQ(CmPortsChainReads(1, 1), 1, "an enclosure's upstream port (depth 1) reads");
    CHECK_EQ(CmPortsChainReads(4, 1), 1,
             "a dock's and an enclosure's ports: the enclosure's downstream port (4) reads");
    CHECK_EQ(CmPortsChainReads(7, 1), 1, "the spare level reads");
    CHECK_EQ(CmPortsChainReads(8, 1), 0, "a bridge at the bound attaches nothing");
    CHECK_EQ(CmPortsChainReads(9, 1), 0, "nor past it");
    CHECK_EQ(CmPortsChainReads(1, 0), 0, "a function that is not a bridge never reads");
}

/* ---- record 34 section 3.3.7 item 1: a chain operation's admission ----- */

/* The CONFIGMG worker's request words, as cm_cfgwk.c keeps them under its
 * lock, with the posts that change them. */
typedef struct {
    ULONG Exit;
    ULONG Active;
    ULONG ReqGone;
    ULONG NoScan;
    ULONG Hold;
    ULONG Gen[4];
} WK_WORDS;

/* cmWkGoneReq: CmCfgwkUnavailable and CmCfgwkGone. */
static void wkPostGone(WK_WORDS *w, ULONG members)
{
    ULONG i;

    members &= w->Active;
    w->ReqGone |= members;
    w->NoScan |= members;
    for (i = 0; i < 4; i++) {
        if (members & (1UL << i)) {
            w->Gen[i]++;
        }
    }
}

/* CmCfgwkPost: a healthy request clears the no-scan mode. */
static void wkPostReenum(WK_WORDS *w, ULONG members)
{
    ULONG i;

    w->NoScan &= ~members;
    for (i = 0; i < 4; i++) {
        if (members & (1UL << i)) {
            w->Gen[i]++;
        }
    }
}

/* The gates the worker reads before the operation (the lock-held ones). */
static ULONG wkGates(const WK_WORDS *w, ULONG i)
{
    return ((w->ReqGone | w->NoScan | w->Hold) & (1UL << i)) == 0;
}

static BOOLEAN wkCurrent(const WK_WORDS *w, ULONG i, ULONG taken)
{
    return CmPortsChainCurrent((BOOLEAN)(w->Exit != 0), w->Active,
                               w->ReqGone | w->NoScan | w->Hold, w->Gen[i],
                               taken, i);
}

static void test_chain_admit(void)
{
    WK_WORDS w;
    ULONG taken;

    memset(&w, 0, sizeof(w));
    w.Active = 0x3;
    w.Gen[1] = 7;
    taken = w.Gen[1];
    CHECK(wkGates(&w, 1), "healthy: the gates pass");
    CHECK(wkCurrent(&w, 1, taken), "healthy: admitted");

    /* The race: the generation taken first, the gates read clear, then a
     * CONTROLLER_UNAVAILABLE posted before the admission. */
    taken = w.Gen[1];
    CHECK(wkGates(&w, 1), "the gates read clear");
    wkPostGone(&w, 0x2);
    CHECK(!wkCurrent(&w, 1, taken), "a loss between the gates and the admission: refused");
    /* The old order took the generation after the post: the loss's own
     * generation, which the no-configuration words still refuse. */
    CHECK(!wkCurrent(&w, 1, w.Gen[1]), "the loss's own generation: still refused while owed");
    /* The removal done and its no-scan re-enumeration done: clear again,
     * but an operation taken before the loss stays stale. */
    w.ReqGone = 0;
    w.NoScan = 0;
    CHECK(!wkCurrent(&w, 1, taken), "taken before the loss: stale after it, whatever follows");
    CHECK(wkCurrent(&w, 1, w.Gen[1]), "taken after the loss was dealt with: admitted");

    /* A loss then a healthy post before the admission: NoScan cleared
     * again, the generation moved twice: refused. */
    taken = w.Gen[1];
    wkPostGone(&w, 0x2);
    w.ReqGone = 0;
    wkPostReenum(&w, 0x2);
    CHECK_EQ(w.NoScan & 0x2, 0, "the healthy post cleared the no-scan mode");
    CHECK(!wkCurrent(&w, 1, taken), "a loss and a healthy post in between: refused");

    /* A loss on another member leaves this one admitted. */
    taken = w.Gen[1];
    wkPostGone(&w, 0x1);
    CHECK(wkCurrent(&w, 1, taken), "another member's loss: this member still admitted");
    CHECK(!wkCurrent(&w, 0, w.Gen[0]), "the lost member: refused");

    /* A retirement barrier raised (no generation step) refuses it too. */
    taken = w.Gen[1];
    w.Hold = 0x2;
    CHECK(!wkCurrent(&w, 1, taken), "a barrier on the member: refused");
    w.Hold = 0;
    CHECK(wkCurrent(&w, 1, taken), "the barrier lifted: admitted again");

    /* Ownership lost, the worker leaving, a member out of range. */
    w.Active = 0x1;
    CHECK(!wkCurrent(&w, 1, taken), "the member no longer owned: refused");
    w.Active = 0x3;
    w.Exit = 1;
    CHECK(!wkCurrent(&w, 1, taken), "the worker leaving: refused");
    w.Exit = 0;
    CHECK(!CmPortsChainCurrent(FALSE, 0xFFFFFFFFUL, 0, 0, 0, 32),
          "a member index past the word: refused");
}

/* ---- the kept child (design record 31 section 4.10) -------------------- */

static void test_kept_os(void)
{
    CHECK_EQ(CmPortsTagOs(5, 0), 1, "2000: the id");
    CHECK_EQ(CmPortsTagOs(5, 2), 1, "XP x64 and 2003: the id");
    CHECK_EQ(CmPortsTagOs(6, 0), 1, "Vista: the id");
    CHECK_EQ(CmPortsTagOs(6, 2), 1, "8: the id");
    CHECK_EQ(CmPortsTagOs(6, 3), 1, "8.1: the id, with the kept child");
    CHECK_EQ(CmPortsTagOs(10, 0), 0, "10: no id");
    CHECK_EQ(CmPortsTagOs(6, 4), 0, "6.4 (a 10 preview): no id");
    CHECK_EQ(CmPortsTagOs(4, 0), 0, "below 2000: no id");
    CHECK_EQ(CmPortsKeptOs(6, 3), 1, "8.1: kept");
    CHECK_EQ(CmPortsKeptOs(6, 2), 0, "8: not kept");
    CHECK_EQ(CmPortsKeptOs(6, 1), 0, "7: not kept");
    CHECK_EQ(CmPortsKeptOs(5, 1), 0, "XP: not kept");
    CHECK_EQ(CmPortsKeptOs(10, 0), 0, "10: not kept");
    CHECK_EQ(CmPortsKeptOs(6, 4), 0, "6.4: not kept");
}

/* A model of one kept child over the pure functions: the state, the
 * references (the attach's first), whether its departure is recorded,
 * how many were, and who destroyed it. */
typedef struct _KEPT_MODEL {
    ULONG State;
    ULONG Refs;
    ULONG Departed;
    ULONG Departures;
    ULONG Present;
    ULONG Attached;     /* still attached to the object below          */
    ULONG Destroyed;    /* 0 not, 1 at PASSIVE_LEVEL, 2 by the work item */
    ULONG Uses;         /* a use of the child after its destruction    */
} KEPT_MODEL;

static void kmInit(KEPT_MODEL *m)
{
    memset(m, 0, sizeof(*m));
    m->State = CM_KEPT_ATTACHED;
    m->Refs = 1;
    m->Present = 1;
    m->Attached = 1;
}

static void kmRelease(KEPT_MODEL *m, ULONG aboveP)
{
    ULONG l;

    if (m->Destroyed) {
        m->Uses++;
        return;
    }
    m->Refs--;
    l = CmPortsKeptLast(m->Refs, m->State, aboveP);
    if (l == CM_KEPT_L_DESTROY) {
        m->Destroyed = 1;
    } else if (l == CM_KEPT_L_QUEUE) {
        /* The work item runs later, at PASSIVE_LEVEL. */
        m->Destroyed = 2;
    }
}

static void kmEvent(KEPT_MODEL *m, ULONG event)
{
    ULONG a;

    if (m->Destroyed) {
        m->Uses++;
        return;
    }
    m->State = CmPortsKeptStep(m->State, event, &a);
    if ((a & CM_KEPT_A_DEPART) != 0 && !m->Departed) {
        m->Departed = 1;
        m->Departures++;
    }
    if ((a & CM_KEPT_A_PRESENT) != 0) {
        m->Present = 1;
    }
    if ((a & CM_KEPT_A_ABSENT) != 0) {
        m->Present = 0;
    }
    if ((a & CM_KEPT_A_END_DEPART) != 0) {
        m->Departed = 0;
    }
    if ((a & CM_KEPT_A_DETACH) != 0) {
        m->Attached = 0;
    }
    if ((a & CM_KEPT_A_DROP_ATTACH) != 0) {
        kmRelease(m, 0);
    }
}

/* A dispatch's admission: a reference, or refused. */
static ULONG kmEnter(KEPT_MODEL *m)
{
    if (m->Destroyed) {
        m->Uses++;
        return 0;
    }
    if (!CmPortsKeptAdmits(m->State)) {
        return 0;
    }
    m->Refs++;
    return 1;
}

static void kmRemove(KEPT_MODEL *m, ULONG callbackInside)
{
    if (!kmEnter(m)) {
        return;
    }
    if (callbackInside) {
        kmEvent(m, CM_KEPT_E_CALLBACK);
    }
    kmEvent(m, CM_KEPT_E_REMOVED);
    kmRelease(m, 0);
}

static void kmStart(KEPT_MODEL *m, ULONG ok)
{
    if (!kmEnter(m)) {
        return;
    }
    kmEvent(m, CM_KEPT_E_START);
    kmEvent(m, ok ? CM_KEPT_E_START_OK : CM_KEPT_E_START_FAIL);
    kmRelease(m, 0);
}

static void test_kept_step(void)
{
    ULONG a;
    ULONG s;

    s = CmPortsKeptStep(CM_KEPT_ATTACHED, CM_KEPT_E_REMOVED, &a);
    CHECK_EQ(s, CM_KEPT_LOGICAL, "attached, removed: logical");
    CHECK_EQ(a, CM_KEPT_A_DEPART | CM_KEPT_A_ABSENT, "the departure, no presence");
    s = CmPortsKeptStep(CM_KEPT_STARTING, CM_KEPT_E_REMOVED, &a);
    CHECK_EQ(s, CM_KEPT_LOGICAL, "starting, removed: logical");
    CHECK_EQ(a, CM_KEPT_A_DEPART | CM_KEPT_A_ABSENT, "the departure");
    s = CmPortsKeptStep(CM_KEPT_LOGICAL, CM_KEPT_E_REMOVED, &a);
    CHECK_EQ(s, CM_KEPT_LOGICAL, "a second removal: logical");
    CHECK_EQ(a, 0, "nothing more");
    s = CmPortsKeptStep(CM_KEPT_LOGICAL, CM_KEPT_E_START, &a);
    CHECK_EQ(s, CM_KEPT_STARTING, "logical, a start: starting");
    CHECK_EQ(a, 0, "no presence yet");
    s = CmPortsKeptStep(CM_KEPT_STARTING, CM_KEPT_E_START_OK, &a);
    CHECK_EQ(s, CM_KEPT_ATTACHED, "the start done: attached");
    CHECK_EQ(a, CM_KEPT_A_PRESENT | CM_KEPT_A_END_DEPART,
             "presence back, the departure ended");
    s = CmPortsKeptStep(CM_KEPT_STARTING, CM_KEPT_E_START_FAIL, &a);
    CHECK_EQ(s, CM_KEPT_LOGICAL, "a failed start: logical");
    CHECK_EQ(a, 0, "still no presence");
    s = CmPortsKeptStep(CM_KEPT_ATTACHED, CM_KEPT_E_START, &a);
    CHECK_EQ(s, CM_KEPT_ATTACHED, "a start while attached (its first): no change");
    CHECK_EQ(a, 0, "nothing");
    s = CmPortsKeptStep(CM_KEPT_ATTACHED, CM_KEPT_E_START_OK, &a);
    CHECK_EQ(s, CM_KEPT_ATTACHED, "its success: attached");
    CHECK_EQ(a, 0, "nothing");
    s = CmPortsKeptStep(CM_KEPT_LOGICAL, CM_KEPT_E_START_OK, &a);
    CHECK_EQ(s, CM_KEPT_LOGICAL, "a success not after a start: no change");
    for (s = CM_KEPT_ATTACHED; s <= CM_KEPT_LOGICAL; s++) {
        CHECK_EQ(CmPortsKeptStep(s, CM_KEPT_E_CALLBACK, &a), CM_KEPT_DETACHED,
                 "the callback from any state: detached");
        CHECK_EQ(a, CM_KEPT_A_DETACH | CM_KEPT_A_DROP_ATTACH | CM_KEPT_A_ABSENT,
                 "detach, the attach's reference dropped, no presence");
        CHECK_EQ(CmPortsKeptAdmits(s), 1, "admitted before detached");
    }
    CHECK_EQ(CmPortsKeptStep(CM_KEPT_DETACHED, CM_KEPT_E_CALLBACK, &a),
             CM_KEPT_DETACHED, "a second callback: detached");
    CHECK_EQ(a, 0, "nothing twice");
    CHECK_EQ(CmPortsKeptStep(CM_KEPT_DETACHED, CM_KEPT_E_START, &a),
             CM_KEPT_DETACHED, "detached is final");
    CHECK_EQ(a, 0, "nothing");
    CHECK_EQ(CmPortsKeptStep(CM_KEPT_DETACHED, CM_KEPT_E_REMOVED, &a),
             CM_KEPT_DETACHED, "a removal after the callback inside it");
    CHECK_EQ(a, CM_KEPT_A_DEPART, "its departure recorded");
    CHECK_EQ(CmPortsKeptAdmits(CM_KEPT_DETACHED), 0, "nothing admitted once detached");
    CHECK_EQ(CmPortsKeptLast(0, CM_KEPT_DETACHED, 0), CM_KEPT_L_DESTROY,
             "the last release, detached, passive: destroyed now");
    CHECK_EQ(CmPortsKeptLast(0, CM_KEPT_DETACHED, 1), CM_KEPT_L_QUEUE,
             "above passive: the work item");
    CHECK_EQ(CmPortsKeptLast(1, CM_KEPT_DETACHED, 0), CM_KEPT_L_NONE,
             "a reference left: nothing");
    CHECK_EQ(CmPortsKeptLast(0, CM_KEPT_LOGICAL, 0), CM_KEPT_L_NONE,
             "never destroyed before detached");
}

static void test_kept_orders(void)
{
    KEPT_MODEL m;
    ULONG held;

    /* A disable and enable: logical, re-armed, never destroyed. */
    kmInit(&m);
    kmRemove(&m, 0);
    CHECK_EQ(m.State, CM_KEPT_LOGICAL, "disable: logical");
    CHECK_EQ(m.Departures, 1, "disable: one departure");
    CHECK_EQ(m.Present, 0, "disable: no presence");
    CHECK_EQ(m.Attached, 1, "disable: still attached");
    kmStart(&m, 1);
    CHECK_EQ(m.State, CM_KEPT_ATTACHED, "enable: attached");
    CHECK_EQ(m.Present, 1, "enable: presence");
    CHECK_EQ(m.Departed, 0, "enable: the departure ended");
    CHECK_EQ(m.Refs, 1, "enable: the attach's reference alone");
    CHECK_EQ(m.Destroyed, 0, "enable: not destroyed");
    kmRemove(&m, 0);
    CHECK_EQ(m.Departures, 2, "a second disable: a new departure");

    /* A parent's removal: the removal, then the callback outside any
     * dispatch, which destroys. */
    kmInit(&m);
    kmRemove(&m, 0);
    kmEvent(&m, CM_KEPT_E_CALLBACK);
    CHECK_EQ(m.State, CM_KEPT_DETACHED, "parent: detached");
    CHECK_EQ(m.Destroyed, 1, "parent: destroyed by the callback, passive");
    CHECK_EQ(m.Departures, 1, "parent: one departure");
    CHECK_EQ(m.Uses, 0, "parent: no use after");

    /* A surprise unplug: the callback inside the forwarded removal; the
     * removal's own release destroys. */
    kmInit(&m);
    kmRemove(&m, 1);
    CHECK_EQ(m.Destroyed, 1, "unplug: destroyed by the removal's release");
    CHECK_EQ(m.Departures, 1, "unplug: its departure recorded");
    CHECK_EQ(m.Attached, 0, "unplug: detached");
    CHECK_EQ(m.Uses, 0, "unplug: no use after");

    /* A removal, a refused start (nothing forwarded: no step), then the
     * callback. */
    kmInit(&m);
    kmRemove(&m, 0);
    held = kmEnter(&m);
    kmRelease(&m, 0);
    CHECK_EQ(held, 1, "refused start: admitted, nothing stepped");
    CHECK_EQ(m.State, CM_KEPT_LOGICAL, "refused start: logical");
    kmEvent(&m, CM_KEPT_E_CALLBACK);
    CHECK_EQ(m.Destroyed, 1, "refused start, callback: destroyed");
    CHECK_EQ(m.Departures, 1, "refused start: one departure");

    /* A failed start: logical, no presence, the departure kept. */
    kmInit(&m);
    kmRemove(&m, 0);
    kmStart(&m, 0);
    CHECK_EQ(m.State, CM_KEPT_LOGICAL, "failed start: logical");
    CHECK_EQ(m.Present, 0, "failed start: no presence");
    CHECK_EQ(m.Departed, 1, "failed start: still departed");
    kmRemove(&m, 0);
    CHECK_EQ(m.Departures, 1, "failed start, removal: no second departure");

    /* A repeated removal, then the callback inside the second. */
    kmInit(&m);
    kmRemove(&m, 0);
    kmRemove(&m, 1);
    CHECK_EQ(m.Departures, 1, "repeated: one departure");
    CHECK_EQ(m.Destroyed, 1, "repeated: destroyed by the second removal's release");

    /* The callback while a usage notification is in flight: the
     * completion's release, above passive, hands it to the work item. */
    kmInit(&m);
    held = kmEnter(&m);
    kmEvent(&m, CM_KEPT_E_CALLBACK);
    CHECK_EQ(m.Destroyed, 0, "usage: not destroyed under it");
    CHECK_EQ(kmEnter(&m), 0, "usage: a later dispatch refused");
    kmRelease(&m, 1);
    CHECK_EQ(held, 1, "usage: it held a reference");
    CHECK_EQ(m.Destroyed, 2, "usage: the work item destroys");
    CHECK_EQ(m.Departures, 0, "usage: no removal stepped (the destruction records it)");
    CHECK_EQ(m.Uses, 0, "usage: no use after");

    /* The uninstall: the stack removed (logical), then the PDO deleted. */
    kmInit(&m);
    kmRemove(&m, 0);
    kmEvent(&m, CM_KEPT_E_CALLBACK);
    CHECK_EQ(m.Destroyed, 1, "uninstall: destroyed by its callback");
    CHECK_EQ(m.Departures, 1, "uninstall: one departure");
    CHECK_EQ(m.Uses, 0, "uninstall: no use after");
}

int main(void)
{
    test_families();
    test_infs();
    test_ids();
    test_snapshot();
    test_generation();
    test_child_attach();
    test_bound();
    test_watchdog();
    test_release();
    test_keep_present();
    test_tunnel_ids();
    test_chain_depth();
    test_chain_admit();
    test_kept_os();
    test_kept_step();
    test_kept_orders();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
