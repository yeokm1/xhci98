/*
 * test_cm_ports.c - the pure half of xhcicm.sys's re-enumeration and safe
 * eject (src/cm/cm_ports.c; roadmap-usb4.md tasks 39-A.5 and 39-A.8;
 * design record 21 sections 3.3, 6.1, 6.3 and 7.4): the families' id
 * lists, checked against the INFs that match them; Windows 98's PCI
 * device ids; the bridge filter's snapshot; the resume hold's bound; and
 * design record 31's tunnel id: the compatible-id rewrite, the chain's
 * depth bound and its re-attach.
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
    CHECK_EQ(n, 17, "seventeen families: ten of record 21, seven of record 22");
    f = CmPortsFamily(CM_PORTS_ID(0x1022, 0x1669));
    CHECK(f != NULL && f->RootPortIdCount == 3 && f->NhiIdCount == 2,
          "AMD's two routers share their root ports (ambiguous: the override assigns them)");
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
    missing = 0;
    for (i = 0; i < seenCount; i++) {
        missing += CmPortsFilterId(seen[i]) ? 0 : 1;
    }
    CHECK_EQ(missing, 0, "CmPortsFilterId takes every bridge id of xhcicmf.inf (task 42-A.5)");
    CHECK(!CmPortsFilterId(CM_PORTS_ID(0x1B21, 0x2421)),
          "and not the emulator's chained upstream port, which the INF does not name");
    CHECK(!CmPortsFilterId(CM_PORTS_ID(0x8086, 0x15BF)), "nor an NHI");
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

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
