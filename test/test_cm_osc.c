/*
 * test_cm_osc.c - the pure half of the platform's USB4 handover on 98 SE
 * and ME (src/cm/cm_osc.c; roadmap-usb4.md task 46.12; design record 32
 * section 12.8): the root pointer and the tables' checks, MCFG, the AML
 * parse of the firmware's data region, and the match; the second tier's
 * lookups and canonical form (src/cm/cm_oscc.c, section 12.8.10).
 */

#include <stdio.h>
#include <string.h>
#include "../src/cm/cm_osc.h"
#include "test_harness.h"

static void put32(UCHAR *p, ULONG v)
{
    p[0] = (UCHAR)v;
    p[1] = (UCHAR)(v >> 8);
    p[2] = (UCHAR)(v >> 16);
    p[3] = (UCHAR)(v >> 24);
}

static void fixsum(UCHAR *p, ULONG n, ULONG at)
{
    UCHAR s;
    ULONG i;

    p[at] = 0;
    s = 0;
    for (i = 0; i < n; i++) {
        s = (UCHAR)(s + p[i]);
    }
    p[at] = (UCHAR)(0x100 - s);
}

/* ---- the root pointer ------------------------------------------------ */

static void test_rsdp(void)
{
    UCHAR area[256];
    ULONG rsdt;
    ULONG xsdt;

    memset(area, 0, sizeof(area));
    memcpy(area + 0x40, "RSD PTR ", 8);
    put32(area + 0x40 + 16, 0x7FFE0000UL);
    fixsum(area + 0x40, 20, 8);
    CHECK_EQ(CmOscFindRsdp(area, sizeof(area)), 0x40, "revision 0 found on 16 bytes");
    CmOscRsdpTables(area + 0x40, &rsdt, &xsdt);
    CHECK_EQ(rsdt, 0x7FFE0000UL, "its RSDT");
    CHECK_EQ(xsdt, 0, "no XSDT before revision 2");
    area[0x40 + 16] ^= 1;
    CHECK_EQ(CmOscFindRsdp(area, sizeof(area)), 0xFFFFFFFFUL, "a bad checksum is no root pointer");

    memset(area, 0, sizeof(area));
    memcpy(area + 0x48, "RSD PTR ", 8);
    fixsum(area + 0x48, 20, 8);
    CHECK_EQ(CmOscFindRsdp(area, sizeof(area)), 0xFFFFFFFFUL, "not on a 16-byte boundary: not looked at");

    memset(area, 0, sizeof(area));
    memcpy(area + 0x10, "RSD PTR ", 8);
    area[0x10 + 15] = 2;
    put32(area + 0x10 + 16, 0x7FFE0000UL);
    put32(area + 0x10 + 20, 36);
    put32(area + 0x10 + 24, 0x7FFE1000UL);
    fixsum(area + 0x10, 20, 8);
    fixsum(area + 0x10, 36, 32);
    CHECK_EQ(CmOscFindRsdp(area, sizeof(area)), 0x10, "revision 2 with both checksums");
    CmOscRsdpTables(area + 0x10, &rsdt, &xsdt);
    CHECK_EQ(xsdt, 0x7FFE1000UL, "its XSDT below 4 GB");
    area[0x10 + 30] = 1;
    fixsum(area + 0x10, 36, 32);
    CmOscRsdpTables(area + 0x10, &rsdt, &xsdt);
    CHECK_EQ(xsdt, 0, "an XSDT above 4 GB is not taken");
    area[0x10 + 33] ^= 0x55;
    CHECK_EQ(CmOscFindRsdp(area, sizeof(area)), 0xFFFFFFFFUL, "revision 2: a bad extended checksum refuses it");
}

/* ---- root tables and MCFG -------------------------------------------- */

static void test_tables(void)
{
    UCHAR t[36 + 24];
    UCHAR m[44 + 32];
    ULONG phys;
    ULONG base;
    ULONG end;

    memset(t, 0, sizeof(t));
    put32(t + 36, 0x11111000UL);
    put32(t + 40, 0);
    put32(t + 44, 0x22222000UL);
    put32(t + 48, 1);
    CHECK(CmOscRootEntry(t, 36 + 24, TRUE, 0, &phys) && phys == 0x11111000UL, "XSDT entry 0");
    CHECK(CmOscRootEntry(t, 36 + 24, TRUE, 1, &phys) && phys == 0, "an XSDT entry above 4 GB reads 0");
    CHECK(!CmOscRootEntry(t, 36 + 24, TRUE, 3, &phys), "past the XSDT's end");
    CHECK(CmOscRootEntry(t, 36 + 24, FALSE, 5, &phys), "RSDT has six 4-byte entries");
    CHECK(!CmOscRootEntry(t, 36 + 24, FALSE, 6, &phys), "and no seventh");

    memset(m, 0, sizeof(m));
    put32(m + 44, 0xC0000000UL);    /* segment 1 */
    m[44 + 8] = 1;
    m[44 + 10] = 0;
    m[44 + 11] = 0xFF;
    put32(m + 60, 0xE0000000UL);    /* segment 0, buses 10h-7Fh */
    m[60 + 10] = 0x10;
    m[60 + 11] = 0x7F;
    CHECK(CmOscMcfg(m, sizeof(m), 0x10, 0x12, &base, &end) && base == 0xE0000000UL && end == 0x7F,
          "segment 0's allocation, not segment 1's; the base is bus 0's");
    CHECK(!CmOscMcfg(m, sizeof(m), 0x0F, 0x12, &base, &end), "a bus below the range: none");
    CHECK(!CmOscMcfg(m, sizeof(m), 0x10, 0x80, &base, &end), "a bus above the range: none");
    CHECK(!CmOscMcfg(m, sizeof(m) - 4, 0x10, 0x12, &base, &end), "a ragged length: none");
    put32(m + 60, 0xE0080000UL);
    CHECK(!CmOscMcfg(m, sizeof(m), 0x10, 0x12, &base, &end), "a base off 1 MB: none");
    put32(m + 60, 0xE0000000UL);
    put32(m + 64, 1);
    CHECK(!CmOscMcfg(m, sizeof(m), 0x10, 0x12, &base, &end), "a base above 4 GB: none");
    put32(m + 64, 0);
    m[44 + 8] = 0;              /* the other one segment 0 too */
    CHECK(!CmOscMcfg(m, sizeof(m), 0x10, 0x12, &base, &end), "two allocations covering the buses: none");

    CHECK(CmOscEcam(0xE0000000UL, 0x7F, (0x11 << 8) | (2 << 3) | 1, 0x420, &phys) &&
          phys == 0xE0000000UL + (0x11UL << 20) + (2UL << 15) + (1UL << 12) + 0x420, "the ECAM formula");
    CHECK(!CmOscEcam(0xE0000000UL, 0x7F, 0x8000, 0, &phys), "a bus past the allocation");
    CHECK(!CmOscEcam(0xF8000000UL, 0xFF, 0xFF00, 0, &phys), "a page past 4 GB");
    CHECK(!CmOscEcam(0xE0000000UL, 0x7F, 0x1100, 0x421, &phys), "an offset not a dword");

    CHECK(CmOscInRam(0x40E, 2, 0x80000000UL), "the BIOS data area");
    CHECK(CmOscInRam(0xE0000, 0x20000, 0x80000000UL), "the BIOS area");
    CHECK(!CmOscInRam(0xA0000, 4, 0x80000000UL), "video memory is not RAM");
    CHECK(!CmOscInRam(0x9FFFE, 4, 0x80000000UL), "nor a read across into it");
    CHECK(CmOscInRam(0x7FFFFFFCUL, 4, 0x80000000UL), "the last dword below TOP_MEM");
    CHECK(!CmOscInRam(0x7FFFFFFEUL, 4, 0x80000000UL), "a read across TOP_MEM");
    CHECK(!CmOscInRam(0xFFFFFFFEUL, 4, 0xFFFFFFFFUL), "a wrap");
    CHECK(!CmOscInRam(0x100000, 0, 0x80000000UL), "an empty read");
}

/* ---- the AML ---------------------------------------------------------- */

/* An OperationRegion (CPNV, SystemMemory, base, len) with dword constants. */
static ULONG region(UCHAR *p, const char *name, ULONG base, ULONG len)
{
    p[0] = 0x5B;
    p[1] = 0x80;
    memcpy(p + 2, name, 4);
    p[6] = 0x00;
    p[7] = 0x0C;
    put32(p + 8, base);
    p[12] = 0x0C;
    put32(p + 13, len);
    return 17;
}

/* A PkgLength of v (with its own bytes) at p, 2 bytes always. */
static ULONG pkg2(UCHAR *p, ULONG v)
{
    p[0] = (UCHAR)(0x40 | (v & 0x0F));
    p[1] = (UCHAR)(v >> 4);
    return 2;
}

/* A field element: a name and its bit length (one or two-byte PkgLength). */
static ULONG named(UCHAR *p, const char *name, ULONG bits)
{
    memcpy(p, name, 4);
    if (bits < 64) {
        p[4] = (UCHAR)bits;
        return 5;
    }
    return 4 + pkg2(p + 4, bits);
}

/* A Field (CPNV, AnyAcc, Lock, Preserve) over `list` (n bytes). */
static ULONG field(UCHAR *p, const char *on, const UCHAR *list, ULONG n)
{
    ULONG total;

    p[0] = 0x5B;
    p[1] = 0x81;
    total = 2 + 4 + 1 + n;          /* PkgLength (2), name, flags, list */
    pkg2(p + 2, total);
    memcpy(p + 4, on, 4);
    p[8] = 0x10;
    memcpy(p + 9, list, n);
    return 2 + total;
}

/* A package (opcode bytes `op`, then a 2-byte PkgLength over `body`). */
static ULONG pkgop(UCHAR *p, const UCHAR *op, ULONG nop, const UCHAR *body,
                   ULONG n)
{
    memcpy(p, op, nop);
    pkg2(p + nop, n + 2);
    memcpy(p + nop + 2, body, n);
    return nop + 2 + n;
}

static const UCHAR OP_SCOPE[] = { 0x10 };
static const UCHAR OP_DEVICE[] = { 0x5B, 0x82 };
static const UCHAR OP_METHOD[] = { 0x14 };

/* The pointer fields' list: M082 32, M083 64, to byte 100h, an access
 * field, M251 32, an extended access field, M620 32. */
static ULONG ptrlist(UCHAR *list)
{
    ULONG l;

    l = 0;
    l += named(list + l, "M082", 32);
    l += named(list + l, "M083", 64);
    list[l++] = 0x00;
    l += pkg2(list + l, 0x800 - 96);
    list[l++] = 0x01;
    list[l++] = 0x01;
    list[l++] = 0x00;
    l += named(list + l, "M251", 32);
    list[l++] = 0x03;
    list[l++] = 0x01;
    list[l++] = 0x0B;
    list[l++] = 0x04;
    l += named(list + l, "M620", 32);
    return l;
}

static ULONG walkplace(const UCHAR *aml, ULONG n, CM_OSC_WALK *w,
                       PULONG o620, PULONG o251, BOOLEAN *walked)
{
    ULONG base;
    ULONG len;

    memset(w, 0, sizeof(*w));
    *walked = CmOscAmlWalk(aml, n, w);
    return CmOscCpnvPlace(w, &base, &len, o620, o251);
}

static void test_aml(void)
{
    static UCHAR aml[1024];
    static UCHAR body[512];
    static CM_OSC_WALK w;
    UCHAR list[128];
    UCHAR mb[64];
    ULONG n;
    ULONG b;
    ULONG l;
    ULONG o620;
    ULONG o251;
    BOOLEAN walked;

    l = ptrlist(list);

    /* At the root: the Field before its region, then the region. */
    n = field(aml, "CPNV", list, l);
    n += region(aml + n, "CPNV", 0x40000000UL, 0x200);
    CHECK_EQ(walkplace(aml, n, &w, &o620, &o251, &walked), CM_OSC_OK, "placed at the root");
    CHECK(walked, "walked whole");
    CHECK_EQ(o251, 0x100, "M251 at 100h");
    CHECK_EQ(o620, 0x104, "M620 at 104h: access fields take no bits");

    /* The Field inside Scope (\_SB) naming CPNV alone: searched upward. */
    b = 0;
    body[b++] = 0x5C;
    memcpy(body + b, "_SB_", 4);
    b += 4;
    b += field(body + b, "CPNV", list, l);
    n = region(aml, "CPNV", 0x40000000UL, 0x200);
    n += pkgop(aml + n, OP_SCOPE, 1, body, b);
    CHECK_EQ(walkplace(aml, n, &w, &o620, &o251, &walked), CM_OSC_NO_CPNV,
             "a Field in \\_SB declares \\_SB.M620, not the \\M620 the methods read");
    CHECK(walked, "and is walked past");

    /* ... but not with another CPNV in a device on the way. */
    b = 0;
    memcpy(body + b, "DEV0", 4);
    b += 4;
    b += region(body + b, "CPNV", 0x50000000UL, 0x200);
    b += field(body + b, "CPNV", list, l);
    n = region(aml, "CPNV", 0x40000000UL, 0x200);
    n += pkgop(aml + n, OP_DEVICE, 2, body, b);
    CHECK_EQ(walkplace(aml, n, &w, &o620, &o251, &walked), CM_OSC_NO_CPNV,
             "a field over \\DEV0.CPNV is not over \\CPNV");
    CHECK_EQ(w.CpnvAnywhere, 2, "two regions named CPNV");

    /* A method whose body holds a region's and a field's bytes: skipped. */
    b = 0;
    memcpy(body + b, "MTHD", 4);
    b += 4;
    body[b++] = 0x00;
    b += region(body + b, "CPNV", 0x60000000UL, 0x200);
    b += field(body + b, "CPNV", list, l);
    n = pkgop(aml, OP_METHOD, 1, body, b);
    CHECK_EQ(walkplace(aml, n, &w, &o620, &o251, &walked), CM_OSC_NO_CPNV,
             "nothing inside a method is taken");
    CHECK(walked && w.CpnvAnywhere == 0, "and the method was skipped whole");

    /* An unknown term after a good region and field: nothing kept. */
    n = region(aml, "CPNV", 0x40000000UL, 0x200);
    n += field(aml + n, "CPNV", list, l);
    aml[n++] = 0x70;            /* Store, not a term this walk knows */
    aml[n++] = 0x00;
    aml[n++] = 0x60;
    CHECK_EQ(walkplace(aml, n, &w, &o620, &o251, &walked), CM_OSC_NO_CPNV,
             "an unknown term: the table is not used");
    CHECK(!walked, "and the walk says so");

    /* Names, externals, a mutex and a buffer before the region. */
    n = 0;
    aml[n++] = 0x08;
    memcpy(aml + n, "NAM1", 4);
    n += 4;
    aml[n++] = 0x0C;
    put32(aml + n, 0x12345678UL);
    n += 4;
    aml[n++] = 0x08;
    memcpy(aml + n, "NAM2", 4);
    n += 4;
    aml[n++] = 0x11;            /* Buffer with a region's bytes inside */
    b = region(body, "CPNV", 0x70000000UL, 0x200);
    pkg2(aml + n, b + 2);
    n += 2;
    memcpy(aml + n, body, b);
    n += b;
    aml[n++] = 0x15;
    aml[n++] = 0x5C;
    memcpy(aml + n, "M999", 4);
    n += 4;
    aml[n++] = 0x01;
    aml[n++] = 0x00;
    aml[n++] = 0x5B;
    aml[n++] = 0x01;
    memcpy(aml + n, "MUTX", 4);
    n += 4;
    aml[n++] = 0x00;
    n += region(aml + n, "CPNV", 0x40000000UL, 0x200);
    n += field(aml + n, "CPNV", list, l);
    CHECK_EQ(walkplace(aml, n, &w, &o620, &o251, &walked), CM_OSC_OK,
             "names, a buffer, an external and a mutex walked past");
    CHECK_EQ(w.Base, 0x40000000UL, "the buffer's bytes were not taken for a region");

    /* Pointer fields off a byte, too wide, past the region's end. */
    l = 0;
    l += named(list + l, "M082", 3);
    l += named(list + l, "M620", 32);
    l += named(list + l, "M251", 32);
    n = region(aml, "CPNV", 0x40000000UL, 0x200);
    n += field(aml + n, "CPNV", list, l);
    CHECK_EQ(walkplace(aml, n, &w, &o620, &o251, &walked), CM_OSC_NO_CPNV, "off a byte: not placed");
    l = 0;
    l += named(list + l, "M620", 64);
    l += named(list + l, "M251", 32);
    n = region(aml, "CPNV", 0x40000000UL, 0x200);
    n += field(aml + n, "CPNV", list, l);
    CHECK_EQ(walkplace(aml, n, &w, &o620, &o251, &walked), CM_OSC_NO_CPNV, "64 bits wide: not placed");
    l = ptrlist(list);
    n = region(aml, "CPNV", 0x40000000UL, 0x104);
    n += field(aml + n, "CPNV", list, l);
    CHECK_EQ(walkplace(aml, n, &w, &o620, &o251, &walked), CM_OSC_NO_CPNV, "past the region's end: refused");
    n = region(aml, "CPNV", 0x40000000UL, 0x108);
    n += field(aml + n, "CPNV", list, l);
    CHECK_EQ(walkplace(aml, n, &w, &o620, &o251, &walked), CM_OSC_OK, "ending at the region's end: placed");

    /* The same name twice in one list. */
    l = 0;
    l += named(list + l, "M620", 32);
    l += named(list + l, "M251", 32);
    l += named(list + l, "M620", 32);
    n = region(aml, "CPNV", 0x40000000UL, 0x200);
    n += field(aml + n, "CPNV", list, l);
    CHECK_EQ(walkplace(aml, n, &w, &o620, &o251, &walked), CM_OSC_NO_CPNV, "M620 twice in a list refuses it");

    /* Regions A, B, A: still ambiguous. */
    n = region(aml, "CPNV", 0x40000000UL, 0x200);
    n += region(aml + n, "CPNV", 0x50000000UL, 0x200);
    n += region(aml + n, "CPNV", 0x40000000UL, 0x200);
    CHECK_EQ(walkplace(aml, n, &w, &o620, &o251, &walked), CM_OSC_CPNV_TWICE, "an ambiguity is not forgotten");

    /* An unknown element in the field list (a connect field). */
    l = 0;
    l += named(list + l, "M620", 32);
    list[l++] = 0x02;
    list[l++] = 0x00;
    l += named(list + l, "M251", 32);
    n = region(aml, "CPNV", 0x40000000UL, 0x200);
    n += field(aml + n, "CPNV", list, l);
    CHECK_EQ(walkplace(aml, n, &w, &o620, &o251, &walked), CM_OSC_NO_CPNV, "a connect field refuses the field");

    /* Two placements, two regions that disagree. */
    l = 0;
    l += named(list + l, "M620", 32);
    l += named(list + l, "M251", 32);
    n = region(aml, "CPNV", 0x40000000UL, 0x200);
    n += field(aml + n, "CPNV", list, l);
    l = 0;
    l += named(list + l, "M251", 32);
    l += named(list + l, "M620", 32);
    n += field(aml + n, "CPNV", list, l);
    CHECK_EQ(walkplace(aml, n, &w, &o620, &o251, &walked), CM_OSC_CPNV_TWICE, "two placements that disagree");
    n = region(aml, "CPNV", 0x40000000UL, 0x200);
    n += region(aml + n, "CPNV", 0x50000000UL, 0x200);
    CHECK_EQ(walkplace(aml, n, &w, &o620, &o251, &walked), CM_OSC_CPNV_TWICE, "two regions that disagree");

    /* A region whose address is a name, or in another space. */
    l = ptrlist(list);
    n = 0;
    aml[n++] = 0x5B;
    aml[n++] = 0x80;
    memcpy(aml + n, "CPNV", 4);
    n += 4;
    aml[n++] = 0x00;
    memcpy(aml + n, "M083", 4);
    n += 4;
    aml[n++] = 0x0B;
    aml[n++] = 0x00;
    aml[n++] = 0x02;
    n += field(aml + n, "CPNV", list, l);
    CHECK_EQ(walkplace(aml, n, &w, &o620, &o251, &walked), CM_OSC_NO_CPNV,
             "an address given by name: the table is not used");
    CHECK(!walked, "a name as a region argument ends the walk");
    n = region(aml, "CPNV", 0x40000000UL, 0x200);
    aml[6] = 0x01;
    n += field(aml + n, "CPNV", list, l);
    CHECK_EQ(walkplace(aml, n, &w, &o620, &o251, &walked), CM_OSC_CPNV_TWICE, "a region in I/O space: not usable");

    /* A cut table: refused, nothing read past its end. */
    n = region(aml, "CPNV", 0x40000000UL, 0x200);
    CHECK_EQ(walkplace(aml, n - 3, &w, &o620, &o251, &walked), CM_OSC_NO_CPNV, "a cut region");
    CHECK(!walked, "a cut table is not walked whole");

    /* The methods by canonical path, hashed, in a scope. */
    b = 0;
    body[b++] = 0x5C;
    memcpy(body + b, "_SB_", 4);
    b += 4;
    l = 0;
    memcpy(mb + l, "M625", 4);
    l += 4;
    mb[l++] = 0x0A;
    mb[l++] = 0x0B; mb[l++] = 0x20; mb[l++] = 0x04;
    b += pkgop(body + b, OP_METHOD, 1, mb, l);
    n = pkgop(aml, OP_SCOPE, 1, body, b);
    memset(&w, 0, sizeof(w));
    CHECK(CmOscAmlWalk(aml, n, &w) && w.MethodSeen[0] == 1, "\\_SB.M625 found");
    CHECK_EQ(w.MethodHash[0], CmOscHash(mb + 4, l - 4), "and hashed over its flags and body");
    CHECK_EQ(CmOscCodeKnown(&w), CM_OSC_UNKNOWN_CODE, "an unknown build");
    mb[6] ^= 1;
    CHECK(CmOscHash(mb + 4, l - 4) != w.MethodHash[0], "one byte changes the hash");
}

/* ---- the match -------------------------------------------------------- */

static void test_decide(void)
{
    UCHAR d[CM_OSC_DUSB4_BYTES];
    UCHAR e[8];
    ULONG data;

    memset(d, 0, sizeof(d));
    memset(e, 0, sizeof(e));
    d[CM_OSC_D_FITTED] = 2;
    d[CM_OSC_D_ROOT_INDEX] = 0;
    d[CM_OSC_D_ROOT_DEV] = 3;
    d[CM_OSC_D_ROOT_FN] = 0;
    d[CM_OSC_D_DATA] = 0x5A;
    d[CM_OSC_D_GRANT] = 1;
    e[0] = 0;
    e[3] = 0;
    data = 0;
    CHECK_EQ(CmOscDecide(d, e, (0 << 8) | (3 << 3) | 0, &data), CM_OSC_OK, "a match");
    CHECK_EQ(data, 0x5A, "and its data byte");
    CHECK_EQ(CmOscDecide(d, e, (0 << 8) | (2 << 3) | 0, &data), CM_OSC_OTHER_ROOT, "another root port");
    e[3] = 0x20;
    CHECK_EQ(CmOscDecide(d, e, (0 << 8) | (3 << 3) | 0, &data), CM_OSC_OTHER_ROOT, "another bus");
    CHECK_EQ(CmOscDecide(d, e, (0x20 << 8) | (3 << 3) | 0, &data), CM_OSC_OK, "the root on bus 20h");
    e[4] = 1;
    CHECK_EQ(CmOscDecide(d, e, (0x20 << 8) | (3 << 3) | 0, &data), CM_OSC_OTHER_ROOT, "a bus above FFh");
    e[4] = 0;
    e[0] = 0xFF;
    CHECK_EQ(CmOscDecide(d, e, (0x20 << 8) | (3 << 3) | 0, &data), CM_OSC_NO_ROOT_ENTRY, "no roots entry");
    e[0] = 0;
    d[CM_OSC_D_GRANT] = 0;
    CHECK_EQ(CmOscDecide(d, e, (0x20 << 8) | (3 << 3) | 0, &data), CM_OSC_NOT_GRANTED, "not granted");
    d[CM_OSC_D_GRANT] = 1;
    d[CM_OSC_D_FITTED] = 0;
    CHECK_EQ(CmOscDecide(d, e, (0x20 << 8) | (3 << 3) | 0, &data), CM_OSC_NOT_FITTED, "no host fitted");
    d[CM_OSC_D_FITTED] = 1;
    d[CM_OSC_D_ROOT_DEV] = 0x20;
    CHECK_EQ(CmOscDecide(d, e, (0x20 << 8) | (3 << 3) | 0, &data), CM_OSC_OTHER_ROOT, "a device above 1Fh");
    CHECK(CM_OSC_MB_BUSY(0x00000102UL) && !CM_OSC_MB_BUSY(0x00000202UL), "busy is bits 11:8 = 1, the command as written");
    CHECK(CM_OSC_MB_READY(0x00000100UL) && !CM_OSC_MB_READY(0x000F00FFUL), "ready is bits 11:8 not 0");
}

/* ---- the known builds ------------------------------------------------- */

static void test_known(void)
{
    static CM_OSC_WALK w;
    ULONG i;

    memset(&w, 0, sizeof(w));
    for (i = 0; i < CM_OSC_METHODS; i++) {
        w.MethodSeen[i] = 1;
        w.MethodHash[i] = CmOscKnownCode[0][i];
    }
    CHECK(CmOscKnownCount >= 1, "at least one build is known");
    CHECK_EQ(CmOscCodeKnown(&w), CM_OSC_OK, "the build read is recognised");
    w.MethodSeen[2] = 2;
    CHECK_EQ(CmOscCodeKnown(&w), CM_OSC_UNKNOWN_CODE, "a method found twice is not");
    w.MethodSeen[2] = 1;
    w.MethodSeen[4] = 0;
    CHECK_EQ(CmOscCodeKnown(&w), CM_OSC_UNKNOWN_CODE, "nor one missing");
    w.MethodSeen[4] = 1;
    w.MethodHash[1] ^= 1;
    CHECK_EQ(CmOscCodeKnown(&w), CM_OSC_UNKNOWN_CODE, "nor one method of another build");
}

/* ---- the second tier: lookups and the canonical form (record 32 section
 * 12.8.10) ---------------------------------------------------------------- */

/* A table holding `decls` and Method (M049, flags) { body } at the root. */
static ULONG tmethod(UCHAR *aml, const UCHAR *decls, ULONG nd,
                     const UCHAR *body, ULONG nb, UCHAR flags)
{
    UCHAR inner[1600];
    ULONG l;

    memcpy(aml, decls, nd);
    memcpy(inner, "M049", 4);
    inner[4] = flags;
    memcpy(inner + 5, body, nb);
    l = nd + pkgop(aml + nd, OP_METHOD, 1, inner, nb + 5);
    return l;
}

/* The form of M049's body: the reason (0 or CM_OSCC_W_*), the hashes. */
static ULONG tform(const UCHAR *decls, ULONG nd, const UCHAR *body, ULONG nb,
                   PULONG fnv, PULONG crc)
{
    static UCHAR aml[2048];
    static CM_OSC_WALK w;
    static CM_OSCC c;
    CM_OSC_TBL t;
    ULONG n;

    n = tmethod(aml, decls, nd, body, nb, 0x0A);
    memset(&w, 0, sizeof(w));
    if (!CmOscAmlWalk(aml, n, &w)) {
        return 0xFFUL;
    }
    t.Aml = aml;
    t.Length = n;
    return CmOscCanon(&c, &t, 1, &w, 2, fnv, crc);
}

/* TRUE when two bodies have the same form (both parsed). */
static int tsame(const UCHAR *decls, ULONG nd, const UCHAR *a, ULONG na,
                 const UCHAR *b, ULONG nb)
{
    ULONG f1, c1, f2, c2;

    if (tform(decls, nd, a, na, &f1, &c1) != 0 ||
        tform(decls, nd, b, nb, &f2, &c2) != 0) {
        return -1;
    }
    return f1 == f2 && c1 == c2;
}

#define NB(a) (a), sizeof(a)

static const UCHAR D_NONE[] = { 0x00 };     /* unused: nd 0 */
/* External (\M460, MethodObj, 7), External (\M620, FieldUnitObj, 0),
 * External (\FUNC, MethodObj, 2), Name (YYYY, Zero) */
static const UCHAR D_STD[] = {
    0x15, 0x5C, 'M', '4', '6', '0', 0x08, 0x07,
    0x15, 0x5C, 'M', '6', '2', '0', 0x05, 0x00,
    0x15, 0x5C, 'F', 'U', 'N', 'C', 0x08, 0x02,
    0x08, 'Y', 'Y', 'Y', 'Y', 0x00
};

/* A table header's fields (made-up ids). */
static void test_header(void)
{
    UCHAR t[36];
    CM_OSC_HDR h;

    memset(t, 0, sizeof(t));
    memcpy(t, "SSDT", 4);
    put32(t + 4, 0x1234);
    memcpy(t + 10, "OEMXYZ", 6);
    memcpy(t + 16, "TABLEID1", 8);
    put32(t + 24, 0x00010203UL);
    CmOscHeader(t, &h);
    CHECK_EQ(h.Signature, CM_OSC_SIG('S', 'S', 'D', 'T'), "the signature");
    CHECK_EQ(h.Length, 0x1234, "the length");
    CHECK(memcmp(h.OemId, "OEMXYZ", 6) == 0, "the OEM ID");
    CHECK(memcmp(h.OemTableId, "TABLEID1", 8) == 0, "the OEM table ID");
    CHECK_EQ(h.OemRevision, 0x00010203UL, "the OEM revision");
}

static void test_find(void)
{
    static UCHAR aml[512];
    static CM_OSC_WALK w;
    static CM_OSC_WALK s;
    CM_OSC_SCOPE p;
    UCHAR body[128];
    UCHAR inner[64];
    ULONG n;
    ULONG l;
    ULONG args;

    /* Scope (\_SB) { Method (M625, 2) { Return (One) } Name (ABCD, 1) },
     * Field (CPNV) { XFLD, 8 }, External (\XYZW, MethodObj, 3) */
    l = 0;
    memcpy(inner, "M625", 4);
    inner[4] = 0x0A;
    inner[5] = 0xA4;
    inner[6] = 0x01;
    body[l++] = 0x5C;
    memcpy(body + l, "_SB_", 4);
    l += 4;
    l += pkgop(body + l, OP_METHOD, 1, inner, 7);
    body[l++] = 0x08;
    memcpy(body + l, "ABCD", 4);
    l += 4;
    body[l++] = 0x01;
    n = pkgop(aml, OP_SCOPE, 1, body, l);
    n += region(aml + n, "CPNV", 0x100000, 0x100);
    l = named(inner, "XFLD", 8);
    n += field(aml + n, "CPNV", inner, l);
    aml[n++] = 0x15;
    aml[n++] = 0x5C;
    memcpy(aml + n, "XYZW", 4);
    n += 4;
    aml[n++] = 0x08;
    aml[n++] = 0x03;
    memset(&w, 0, sizeof(w));
    w.Table = 2;
    CHECK(CmOscAmlWalk(aml, n, &w), "the lookup's table walks");
    CHECK_EQ(w.MethodSeen[0], 1, "M625 found");
    CHECK_EQ(w.MethodTable[0], 2, "in the table the caller named");
    CHECK_EQ(w.MethodLen[0], 3, "its flags and body: 3 bytes");
    CHECK_EQ(aml[w.MethodAt[0]], 0x0A, "the offset is its flags byte's");

    p.Segs = 2;
    memcpy(p.Seg[0], "_SB_", 4);
    memcpy(p.Seg[1], "M625", 4);
    CHECK_EQ(CmOscFind(aml, n, &p, &s, &args), CM_OSC_FIND_METHOD, "a Method is a method");
    CHECK_EQ(args, 2, "with its argument count");
    memcpy(p.Seg[1], "ABCD", 4);
    CHECK_EQ(CmOscFind(aml, n, &p, &s, &args), CM_OSC_FIND_VALUE, "a Name is a value");
    p.Segs = 1;
    memcpy(p.Seg[0], "XFLD", 4);
    CHECK_EQ(CmOscFind(aml, n, &p, &s, &args), CM_OSC_FIND_VALUE, "a Field element is a value");
    memcpy(p.Seg[0], "XYZW", 4);
    CHECK_EQ(CmOscFind(aml, n, &p, &s, &args), CM_OSC_FIND_METHOD, "an External of type method is a method");
    CHECK_EQ(args, 3, "with the External's count");
    memcpy(p.Seg[0], "CPNV", 4);
    CHECK_EQ(CmOscFind(aml, n, &p, &s, &args), CM_OSC_FIND_VALUE, "a region is a value");
    memcpy(p.Seg[0], "ABCD", 4);
    CHECK_EQ(CmOscFind(aml, n, &p, &s, &args), 0, "a name in another scope is not found");
}

static void test_canon(void)
{
    static const UCHAR base[] = { 0x70, 0x00, 0x60, 0xA4, 0x60 };
    static const UCHAR enc[] = { 0x70, 0x0A, 0x00, 0x61, 0xA3, 0xA4, 0x61 };
    static const UCHAR one[] = { 0x70, 0x01, 0x60, 0xA4, 0x60 };
    static const UCHAR cmp1[] = { 0x72, 0x60, 0x01, 0x60, 0xA4, 0x60 };
    static const UCHAR cmp2[] = { 0x70, 0x72, 0x60, 0x01, 0x00, 0x60,
                                  0xA4, 0x60 };
    static const UCHAR exp1[] = { 0xA4, 0x72, 0x60, 0x01, 0x60 };
    static const UCHAR exp2[] = { 0xA4, 0x70, 0x72, 0x60, 0x01, 0x00, 0x60 };
    static const UCHAR dbg1[] = { 'M', '4', '6', '0', 0x0D, 'a', 0x00,
                                  0x60, 0, 0, 0, 0, 0, 0xA4, 0x60 };
    static const UCHAR dbg2[] = { 'M', '4', '6', '0', 0x0D, 'b', 'b', 0x00,
                                  0x60, 0, 0, 0, 0, 0, 0xA4, 0x60 };
    static const UCHAR dbg3[] = { 0xA4, 0x60 };
    static const UCHAR dbg4[] = { 'M', '4', '6', '0', 0x0D, 'a', 0x00,
                                  0x61, 0, 0, 0, 0, 0, 0xA4, 0x60 };
    static const UCHAR rel1[] = { 0x70, 'M', '6', '2', '0', 0x60, 0xA4, 0x60 };
    static const UCHAR rel2[] = { 0x70, 0x5C, 'M', '6', '2', '0', 0x60,
                                  0xA4, 0x60 };
    static const UCHAR msk1[] = { 0x70, 0x7B, 0x68, 0x0B, 0x00, 0x0F, 0x00,
                                  0x60, 0xA4, 0x60 };
    static const UCHAR msk2[] = { 0x70, 0x7B, 0x68, 0x0B, 0x01, 0x0F, 0x00,
                                  0x60, 0xA4, 0x60 };
    static const UCHAR ord2[] = { 0x70, 0x7B, 0x0B, 0x00, 0x0F, 0x68, 0x00,
                                  0x60, 0xA4, 0x60 };
    static const UCHAR und1[] = { 0x70, 0x5C, 'F', 'O', 'O', 'O', 0x60 };
    static const UCHAR und2[] = { 0x70, 0x5C, 'D', 'I', 'M', 'S', 0x60 };
    static const UCHAR und3[] = { 0x5B, 0x12, 'F', 'O', 'O', 'O', 0x00 };
    static const UCHAR und4[] = { 0x5B, 0x12, 0x5C, 'F', 'O', 'O', 'O', 0x00 };
    static const UCHAR und5[] = { 0x70, 0x60, 0x5C, 'F', 'O', 'O', 'O' };
    static const UCHAR und6[] = { 0x70, 'D', 'I', 'M', 'S', 0x60 };
    static const UCHAR own1[] = { 0x08, 'X', 'X', 'X', 'X', 0x00,
                                  0x08, 'X', 'X', 'X', 'X', 0x01 };
    static const UCHAR own2[] = { 0x08, 'Y', 'Y', 'Y', 'Y', 0x00 };
    static const UCHAR own3[] = { 0x08, 0x5C, 'X', 'X', 'X', 'X', 0x00 };
    static const UCHAR own4[] = { 0x08, 'X', 'X', 'X', 'X', 0x00,
                                  0x70, 'X', 'X', 'X', 'X', 0x60 };
    static const UCHAR own5[] = { 0x08, 'Z', 'Z', 'Z', 'Z', 0x00,
                                  0x70, 'Z', 'Z', 'Z', 'Z', 0x60 };
    static const UCHAR bad1[] = { 0x86, 0x60, 0x01 };
    static const UCHAR bad2[] = { 0x08, 'P', 'P', 'P', 'P', 0x12, 0x02, 0x00 };
    static const UCHAR bad3[] = { 0xA1, 0x01 };
    static const UCHAR call1[] = { 0x70, 'F', 'U', 'N', 'C', 0x60, 0x01, 0x61 };
    static const UCHAR call2[] = { 0x70, 'F', 'U', 'N', 'C', 0x60, 0x00, 0x61 };
    static const UCHAR fld1[] = { 0x5B, 0x80, 'V', 'A', 'R', 'M', 0x00, 0x68,
                                  0x01, 0x5B, 0x81, 0x0B, 'V', 'A', 'R', 'M',
                                  0x01, 'V', 'A', 'R', 'R', 0x08,
                                  0x70, 'V', 'A', 'R', 'R', 0x60 };
    static const UCHAR fld2[] = { 0x5B, 0x80, 'V', 'A', 'R', 'M', 0x00, 0x68,
                                  0x01, 0x5B, 0x81, 0x0B, 'V', 'A', 'R', 'M',
                                  0x01, 'V', 'A', 'R', 'R', 0x10,
                                  0x70, 'V', 'A', 'R', 'R', 0x60 };
    static const UCHAR fld3[] = { 0x5B, 0x80, 'V', 'A', 'R', 'M', 0x00, 0x68,
                                  0x01, 0x5B, 0x81, 0x0B, 'V', 'A', 'R', 'M',
                                  0x03, 'V', 'A', 'R', 'R', 0x08,
                                  0x70, 'V', 'A', 'R', 'R', 0x60 };
    static const UCHAR kind1[] = { 0x08, 'F', 'U', 'N', 'C', 0x00 };
    static const UCHAR alias1[] = { 0x06, 0x5C, 'F', 'U', 'N', 'C',
                                    'A', 'L', 'I', 'A' };
    static const UCHAR alias2[] = { 0x70, 'A', 'L', 'I', 'A', 0x60 };
    static const UCHAR inc1[] = { 0x75, 0x5C, 'F', 'O', 'O', 'O' };
    static const UCHAR inc2[] = { 0x75, 0x5C, 'D', 'I', 'M', 'S' };
    static UCHAR deep[64];
    static UCHAR mixed[64];
    static UCHAR aml[256];
    static UCHAR aml2[64];
    static CM_OSC_WALK w;
    static CM_OSCC c;
    CM_OSC_TBL t[2];
    ULONG f;
    ULONG r;
    ULONG n;
    ULONG i;
    ULONG fnv[CM_OSC_METHODS];
    ULONG crc[CM_OSC_METHODS];

    (void)D_NONE;
    CHECK_EQ(tform(NB(D_STD), NB(base), &f, &r), 0, "a plain body has a form");
    CHECK_EQ(tsame(NB(D_STD), NB(base), NB(enc)), 1, "integer encodings, Locals renumbered and a Noop: the same form");
    CHECK_EQ(tsame(NB(D_STD), NB(base), NB(one)), 0, "another integer: another form");
    CHECK_EQ(tsame(NB(D_STD), NB(cmp1), NB(cmp2)), 1, "Add with a target as a statement is its Store");
    CHECK_EQ(tsame(NB(D_STD), NB(exp1), NB(exp2)), 0, "not inside an expression");
    CHECK_EQ(tsame(NB(D_STD), NB(dbg1), NB(dbg2)), 1, "the debug print's text is not compared");
    CHECK_EQ(tsame(NB(D_STD), NB(dbg1), NB(dbg3)), 0, "a debug call dropped is another form");
    CHECK_EQ(tsame(NB(D_STD), NB(dbg1), NB(dbg4)), 0, "and its other arguments are compared");
    CHECK_EQ(tsame(NB(D_STD), NB(rel1), NB(rel2)), 0, "a name's spelling is compared");
    CHECK_EQ(tsame(NB(D_STD), NB(msk1), NB(msk2)), 0, "a mask changed: another form");
    CHECK_EQ(tsame(NB(D_STD), NB(msk1), NB(ord2)), 0, "an operand order changed: another form");
    CHECK_EQ(tform(NB(D_STD), NB(und1), &f, &r), CM_OSCC_W_KIND, "an undeclared name evaluated declines");
    CHECK_EQ(tform(NB(D_STD), NB(und2), &f, &r), 0, "but one of the four listed");
    CHECK_EQ(tform(NB(D_STD), NB(und3), &f, &r), CM_OSCC_W_KIND, "CondRefOf of an undeclared name without the root prefix declines");
    CHECK_EQ(tform(NB(D_STD), NB(und4), &f, &r), 0, "with it, it does not");
    CHECK_EQ(tform(NB(D_STD), NB(und5), &f, &r), 0, "an undeclared rooted name as a target");
    CHECK_EQ(tform(NB(D_STD), NB(und6), &f, &r), CM_OSCC_W_KIND, "a listed name without its root prefix declines");
    CHECK_EQ(tform(NB(D_STD), NB(own1), &f, &r), CM_OSCC_W_OWN, "a body's name declared twice");
    CHECK_EQ(tform(NB(D_STD), NB(own2), &f, &r), CM_OSCC_W_OWN, "a body's name the tables declare upward");
    CHECK_EQ(tform(NB(D_STD), NB(own3), &f, &r), CM_OSCC_W_OWN, "a body's name with a prefix");
    CHECK_EQ(tsame(NB(D_STD), NB(own4), NB(own5)), 0, "a body's name renamed: another form");
    CHECK_EQ(tform(NB(D_STD), NB(bad1), &f, &r), CM_OSCC_W_TERM, "Notify is outside the set");
    CHECK_EQ(tform(NB(D_STD), NB(bad2), &f, &r), CM_OSCC_W_TERM, "a Package is outside the set");
    CHECK_EQ(tform(NB(D_STD), NB(bad3), &f, &r), CM_OSCC_W_TERM, "an Else with no If");
    CHECK_EQ(tform(NB(D_STD), NB(call1), &f, &r), 0, "a call takes its External's two arguments");
    CHECK_EQ(tsame(NB(D_STD), NB(call1), NB(call2)), 0, "an argument changed: another form");
    CHECK_EQ(tform(NB(D_STD), NB(fld1), &f, &r), 0, "a body's own region and field");
    CHECK_EQ(tsame(NB(D_STD), NB(fld1), NB(fld2)), 0, "a field's width changed");
    CHECK_EQ(tsame(NB(D_STD), NB(fld1), NB(fld3)), 0, "a field's access changed");
    memcpy(aml2, D_STD, sizeof(D_STD));
    memcpy(aml2 + sizeof(D_STD), kind1, sizeof(kind1));
    CHECK_EQ(tform(aml2, sizeof(D_STD) + sizeof(kind1), NB(call1), &f, &r), CM_OSCC_W_KIND, "a name both a method and a value declines");
    memcpy(aml2, D_STD, sizeof(D_STD));
    memcpy(aml2 + sizeof(D_STD), alias1, sizeof(alias1));
    CHECK_EQ(tform(aml2, sizeof(D_STD) + sizeof(alias1), NB(alias2), &f, &r), CM_OSCC_W_KIND, "a name an Alias declares declines");
    CHECK_EQ(tform(NB(D_STD), NB(inc1), &f, &r), CM_OSCC_W_KIND, "Increment of an undeclared rooted name declines");
    CHECK_EQ(tform(NB(D_STD), NB(inc2), &f, &r), 0, "but of one of the four listed");

    /* Statements nested 17 deep: If (One) { If (One) { ... } }, built from
     * the innermost out at the end of deep[]. */
    n = 0;
    for (i = 0; i < 17UL; i++) {
        memmove(deep + 3, deep, n);
        deep[0] = 0xA0;
        deep[1] = (UCHAR)(2 + n);
        deep[2] = 0x01;
        n += 3;
    }
    CHECK_EQ(tform(NB(D_STD), deep, n, &f, &r), CM_OSCC_W_BUDGET, "nesting past 16 declines");

    /* Two tables whose Externals give \FUNC two argument counts. */
    n = tmethod(aml, D_STD, sizeof(D_STD), call1, sizeof(call1), 0x0A);
    mixed[0] = 0x15;
    mixed[1] = 0x5C;
    memcpy(mixed + 2, "FUNC", 4);
    mixed[6] = 0x08;
    mixed[7] = 0x03;
    memset(&w, 0, sizeof(w));
    CHECK(CmOscAmlWalk(aml, n, &w), "the first table walks");
    w.Table = 1;
    CHECK(CmOscAmlWalk(mixed, 8, &w), "the second table walks");
    t[0].Aml = aml;
    t[0].Length = n;
    t[1].Aml = mixed;
    t[1].Length = 8;
    CHECK_EQ(CmOscCanon(&c, t, 2, &w, 2, &f, &r), CM_OSCC_W_KIND, "a method with two argument counts declines");
    CHECK_EQ(CmOscCanon(&c, t, 1, &w, 2, &f, &r), 0, "with the first table alone it parses");
    CHECK_EQ(CmOscCanon(&c, t, 1, &w, 0, &f, &r), CM_OSCC_W_LENGTH, "a method not found has no form");

    /* The list. */
    for (i = 0; i < CM_OSC_METHODS; i++) {
        fnv[i] = CmOscKnownForm[0][i][0];
        crc[i] = CmOscKnownForm[0][i][1];
    }
    CHECK(CmOscKnownFormCount >= 1, "one canonical form at least is known");
    CHECK_EQ(CmOscFormKnown(fnv, crc), CM_OSC_OK, "the build read's forms are recognised");
    crc[3] ^= 1;
    CHECK_EQ(CmOscFormKnown(fnv, crc), CM_OSC_UNKNOWN_CODE, "one CRC off is not");
    crc[3] ^= 1;
    fnv[1] ^= 0x100;
    CHECK_EQ(CmOscFormKnown(fnv, crc), CM_OSC_UNKNOWN_CODE, "nor one FNV off");
    CHECK_EQ(CmOscCrc((const UCHAR *)"123456789", 9), 0xCBF43926UL, "CRC-32's check value");
}

/* ---- the emulator's --osi tables on NT (record 32 section 12.8.15.17) - */

/* usb4-emu scripts/asm-fw-ssdt.py 0x9FF00000 <dir> --build 1 --osi
 * --externals-ifzero (main 189d459): its three tables, whole. */
static const UCHAR osi1Cmn[312] = {
    0x53, 0x53, 0x44, 0x54, 0x38, 0x01, 0x00, 0x00, 0x02, 0x3D, 0x41, 0x4D,
    0x44, 0x20, 0x20, 0x20, 0x43, 0x50, 0x4D, 0x43, 0x4D, 0x4E, 0x20, 0x20,
    0x01, 0x00, 0x00, 0x00, 0x55, 0x45, 0x4D, 0x55, 0x01, 0x00, 0x00, 0x00,
    0x5B, 0x80, 0x43, 0x50, 0x4E, 0x56, 0x00, 0x0C, 0x00, 0x00, 0xF0, 0x9F,
    0x0C, 0x00, 0x01, 0x00, 0x00, 0x5B, 0x81, 0x13, 0x43, 0x50, 0x4E, 0x56,
    0x10, 0x00, 0x40, 0x20, 0x4D, 0x32, 0x35, 0x31, 0x20, 0x4D, 0x36, 0x32,
    0x30, 0x20, 0x08, 0x4D, 0x30, 0x38, 0x33, 0x0C, 0x00, 0x00, 0x00, 0xB0,
    0x08, 0x4D, 0x30, 0x34, 0x36, 0x00, 0x14, 0x28, 0x4D, 0x30, 0x34, 0x35,
    0x00, 0x70, 0x00, 0x4D, 0x30, 0x34, 0x36, 0xA0, 0x1B, 0x5C, 0x5F, 0x4F,
    0x53, 0x49, 0x0D, 0x57, 0x69, 0x6E, 0x64, 0x6F, 0x77, 0x73, 0x20, 0x32,
    0x30, 0x32, 0x31, 0x00, 0x70, 0x0A, 0x0B, 0x4D, 0x30, 0x34, 0x36, 0x14,
    0x30, 0x4D, 0x30, 0x34, 0x39, 0x0A, 0x70, 0x00, 0x60, 0xA0, 0x24, 0x92,
    0x93, 0x68, 0x00, 0x5B, 0x80, 0x56, 0x41, 0x52, 0x4D, 0x00, 0x72, 0x68,
    0x69, 0x00, 0x01, 0x5B, 0x81, 0x0B, 0x56, 0x41, 0x52, 0x4D, 0x01, 0x56,
    0x41, 0x52, 0x52, 0x08, 0x70, 0x56, 0x41, 0x52, 0x52, 0x60, 0xA4, 0x60,
    0x14, 0x42, 0x05, 0x4D, 0x36, 0x32, 0x34, 0x09, 0x70, 0x00, 0x60, 0xA0,
    0x45, 0x04, 0x92, 0x93, 0x4D, 0x32, 0x35, 0x31, 0x00, 0x70, 0x72, 0x0A,
    0x10, 0x77, 0x68, 0x0A, 0x08, 0x00, 0x00, 0x61, 0xA0, 0x30, 0x92, 0x93,
    0x4D, 0x30, 0x34, 0x39, 0x4D, 0x32, 0x35, 0x31, 0x61, 0x0A, 0xFF, 0x70,
    0x7D, 0x4D, 0x30, 0x34, 0x39, 0x4D, 0x32, 0x35, 0x31, 0x72, 0x61, 0x0A,
    0x03, 0x00, 0x79, 0x4D, 0x30, 0x34, 0x39, 0x4D, 0x32, 0x35, 0x31, 0x72,
    0x61, 0x0A, 0x04, 0x00, 0x0A, 0x08, 0x00, 0x00, 0x60, 0xA4, 0x60, 0x14,
    0x1E, 0x4D, 0x30, 0x31, 0x31, 0x0C, 0x70, 0x4D, 0x30, 0x34, 0x39, 0x68,
    0x69, 0x60, 0xA4, 0x7B, 0x7A, 0x60, 0x6A, 0x00, 0x7A, 0x0A, 0xFF, 0x74,
    0x0A, 0x08, 0x6B, 0x00, 0x00, 0x00, 0x14, 0x08, 0x4D, 0x36, 0x35, 0x30,
    0x0A, 0xA4, 0x00, 0x14, 0x08, 0x4D, 0x34, 0x36, 0x30, 0x07, 0xA4, 0x00
};
static const UCHAR osi1Dus[455] = {
    0x53, 0x53, 0x44, 0x54, 0xC7, 0x01, 0x00, 0x00, 0x02, 0xAE, 0x41, 0x4D,
    0x44, 0x20, 0x20, 0x20, 0x44, 0x49, 0x53, 0x43, 0x55, 0x53, 0x42, 0x34,
    0x01, 0x00, 0x00, 0x00, 0x55, 0x45, 0x4D, 0x55, 0x01, 0x00, 0x00, 0x00,
    0xA0, 0x3A, 0x00, 0x15, 0x5C, 0x4D, 0x36, 0x32, 0x30, 0x05, 0x00, 0x15,
    0x5C, 0x4D, 0x30, 0x34, 0x39, 0x08, 0x02, 0x15, 0x5C, 0x4D, 0x36, 0x32,
    0x34, 0x08, 0x01, 0x15, 0x5C, 0x4D, 0x30, 0x31, 0x31, 0x08, 0x04, 0x15,
    0x5C, 0x4D, 0x36, 0x35, 0x30, 0x08, 0x02, 0x15, 0x5C, 0x4D, 0x34, 0x36,
    0x30, 0x08, 0x07, 0x15, 0x5C, 0x4D, 0x30, 0x38, 0x33, 0x01, 0x00, 0x10,
    0x47, 0x16, 0x5C, 0x5F, 0x53, 0x42, 0x5F, 0x5B, 0x01, 0x4D, 0x36, 0x32,
    0x37, 0x00, 0x14, 0x48, 0x15, 0x4D, 0x36, 0x32, 0x35, 0x0A, 0x5B, 0x23,
    0x4D, 0x36, 0x32, 0x37, 0xFF, 0xFF, 0x70, 0x01, 0x67, 0xA0, 0x4D, 0x13,
    0x92, 0x93, 0x4D, 0x36, 0x32, 0x30, 0x00, 0x70, 0x4D, 0x30, 0x34, 0x39,
    0x4D, 0x36, 0x32, 0x30, 0x0A, 0x12, 0x60, 0x70, 0x4D, 0x30, 0x34, 0x39,
    0x4D, 0x36, 0x32, 0x30, 0x0A, 0x13, 0x61, 0x70, 0x4D, 0x30, 0x34, 0x39,
    0x4D, 0x36, 0x32, 0x30, 0x0A, 0x14, 0x62, 0x70, 0x4D, 0x36, 0x32, 0x34,
    0x60, 0x63, 0x70, 0x72, 0x4D, 0x30, 0x38, 0x33, 0x79, 0x63, 0x0A, 0x14,
    0x00, 0x00, 0x64, 0x72, 0x64, 0x79, 0x61, 0x0A, 0x0F, 0x00, 0x64, 0x72,
    0x64, 0x79, 0x62, 0x0A, 0x0C, 0x00, 0x64, 0x70, 0x4D, 0x30, 0x31, 0x31,
    0x64, 0x0A, 0x19, 0x00, 0x0A, 0x08, 0x65, 0xA0, 0x4F, 0x0D, 0x92, 0x93,
    0x65, 0x00, 0x70, 0x72, 0x4D, 0x30, 0x38, 0x33, 0x79, 0x65, 0x0A, 0x14,
    0x00, 0x00, 0x66, 0x72, 0x66, 0x0B, 0x20, 0x04, 0x66, 0x70, 0x4D, 0x36,
    0x35, 0x30, 0x74, 0x66, 0x0B, 0x20, 0x04, 0x00, 0x00, 0x64, 0x5B, 0x80,
    0x56, 0x41, 0x52, 0x4D, 0x00, 0x66, 0x0A, 0x14, 0x5B, 0x81, 0x13, 0x56,
    0x41, 0x52, 0x4D, 0x03, 0x4D, 0x36, 0x32, 0x31, 0x20, 0x00, 0x40, 0x06,
    0x4D, 0x36, 0x32, 0x32, 0x20, 0x70, 0x68, 0x4D, 0x36, 0x32, 0x31, 0x70,
    0x00, 0x60, 0xA2, 0x25, 0x90, 0x95, 0x60, 0x69, 0x93, 0x7B, 0x4D, 0x36,
    0x32, 0x31, 0x0B, 0x00, 0x0F, 0x00, 0x0B, 0x00, 0x01, 0x75, 0x60, 0x70,
    0x0A, 0x0A, 0x65, 0xA2, 0x0C, 0x94, 0x65, 0x00, 0x74, 0x65, 0x01, 0x65,
    0x5B, 0x21, 0x0A, 0x63, 0xA0, 0x3F, 0x95, 0x60, 0x69, 0x70, 0x00, 0x62,
    0xA2, 0x23, 0x90, 0x95, 0x62, 0x69, 0x93, 0x7B, 0x4D, 0x36, 0x32, 0x32,
    0x0B, 0x00, 0x0F, 0x00, 0x00, 0x75, 0x62, 0x70, 0x0A, 0x0A, 0x65, 0xA2,
    0x0C, 0x94, 0x65, 0x00, 0x74, 0x65, 0x01, 0x65, 0x5B, 0x21, 0x0A, 0x63,
    0x70, 0x4D, 0x36, 0x32, 0x32, 0x63, 0x70, 0x00, 0x4D, 0x36, 0x32, 0x32,
    0xA0, 0x07, 0x95, 0x62, 0x69, 0x70, 0x63, 0x67, 0x70, 0x4D, 0x36, 0x35,
    0x30, 0x74, 0x66, 0x0B, 0x20, 0x04, 0x00, 0x64, 0x64, 0x4D, 0x34, 0x36,
    0x30, 0x0D, 0x6D, 0x6F, 0x64, 0x65, 0x6C, 0x20, 0x4D, 0x36, 0x32, 0x35,
    0x20, 0x25, 0x78, 0x20, 0x3D, 0x20, 0x25, 0x78, 0x00, 0x68, 0x67, 0x00,
    0x00, 0x00, 0x00, 0x5B, 0x27, 0x4D, 0x36, 0x32, 0x37, 0xA4, 0x67
};
static const UCHAR osi1Osc[415] = {
    0x53, 0x53, 0x44, 0x54, 0x9F, 0x01, 0x00, 0x00, 0x02, 0xCE, 0x41, 0x4D,
    0x44, 0x20, 0x20, 0x20, 0x43, 0x50, 0x4D, 0x4D, 0x53, 0x4F, 0x53, 0x43,
    0x01, 0x00, 0x00, 0x00, 0x55, 0x45, 0x4D, 0x55, 0x01, 0x00, 0x00, 0x00,
    0xA0, 0x37, 0x00, 0x15, 0x5C, 0x2E, 0x5F, 0x53, 0x42, 0x5F, 0x4D, 0x36,
    0x32, 0x35, 0x08, 0x02, 0x15, 0x5C, 0x4D, 0x36, 0x32, 0x30, 0x05, 0x00,
    0x15, 0x5C, 0x4D, 0x30, 0x34, 0x39, 0x08, 0x02, 0x15, 0x5C, 0x4D, 0x34,
    0x36, 0x30, 0x08, 0x07, 0x15, 0x5C, 0x4D, 0x30, 0x34, 0x35, 0x08, 0x00,
    0x15, 0x5C, 0x4D, 0x30, 0x34, 0x36, 0x01, 0x00, 0x10, 0x42, 0x14, 0x5C,
    0x5F, 0x53, 0x42, 0x5F, 0x14, 0x4A, 0x13, 0x5F, 0x4F, 0x53, 0x43, 0x0C,
    0x8A, 0x6B, 0x00, 0x43, 0x44, 0x57, 0x31, 0x5C, 0x4D, 0x30, 0x34, 0x35,
    0xA0, 0x46, 0x08, 0x93, 0x68, 0x11, 0x13, 0x0A, 0x10, 0x3A, 0xD1, 0xA0,
    0x23, 0xAB, 0x26, 0x6C, 0x48, 0x9C, 0x5F, 0x0F, 0xFA, 0x52, 0x5A, 0x57,
    0x5A, 0xA0, 0x40, 0x06, 0x93, 0x69, 0x01, 0xA0, 0x4A, 0x05, 0x90, 0x92,
    0x93, 0x4D, 0x36, 0x32, 0x30, 0x00, 0x90, 0x92, 0x93, 0x4D, 0x30, 0x34,
    0x39, 0x4D, 0x36, 0x32, 0x30, 0x0A, 0x10, 0x00, 0x92, 0x95, 0x5C, 0x4D,
    0x30, 0x34, 0x36, 0x0A, 0x0B, 0x70, 0x5C, 0x2E, 0x5F, 0x53, 0x42, 0x5F,
    0x4D, 0x36, 0x32, 0x35, 0x0B, 0x80, 0x01, 0x0B, 0xE8, 0x03, 0x61, 0xA0,
    0x26, 0x93, 0x7B, 0x61, 0x0A, 0xFF, 0x00, 0x00, 0x4D, 0x34, 0x36, 0x30,
    0x0D, 0x6D, 0x6F, 0x64, 0x65, 0x6C, 0x20, 0x5F, 0x4F, 0x53, 0x43, 0x20,
    0x63, 0x61, 0x70, 0x73, 0x20, 0x25, 0x78, 0x00, 0x61, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xA1, 0x0C, 0x7D, 0x43, 0x44, 0x57, 0x31, 0x0A, 0x08, 0x43,
    0x44, 0x57, 0x31, 0xA1, 0x4D, 0x09, 0xA0, 0x4D, 0x08, 0x93, 0x68, 0x11,
    0x13, 0x0A, 0x10, 0x6E, 0xB0, 0x11, 0x08, 0x27, 0x4A, 0xF9, 0x44, 0x8D,
    0x60, 0x3C, 0xBB, 0xC2, 0x2E, 0x7B, 0x48, 0xA0, 0x47, 0x06, 0x93, 0x69,
    0x01, 0xA0, 0x41, 0x06, 0x90, 0x92, 0x93, 0x7B, 0x43, 0x44, 0x57, 0x31,
    0x01, 0x00, 0x01, 0x92, 0x93, 0x4D, 0x36, 0x32, 0x30, 0x00, 0xA0, 0x4C,
    0x04, 0x90, 0x90, 0x92, 0x93, 0x4D, 0x30, 0x34, 0x39, 0x4D, 0x36, 0x32,
    0x30, 0x0A, 0x10, 0x00, 0x92, 0x95, 0x5C, 0x4D, 0x30, 0x34, 0x36, 0x0A,
    0x0B, 0x92, 0x93, 0x4D, 0x30, 0x34, 0x39, 0x4D, 0x36, 0x32, 0x30, 0x0A,
    0x4D, 0x00, 0x70, 0x4D, 0x30, 0x34, 0x39, 0x4D, 0x36, 0x32, 0x30, 0x0A,
    0x4B, 0x64, 0x70, 0x5C, 0x2E, 0x5F, 0x53, 0x42, 0x5F, 0x4D, 0x36, 0x32,
    0x35, 0x72, 0x0B, 0x02, 0x01, 0x79, 0x64, 0x0A, 0x10, 0x00, 0x00, 0x0B,
    0xE8, 0x03, 0x61, 0xA1, 0x0C, 0x7D, 0x43, 0x44, 0x57, 0x31, 0x0A, 0x08,
    0x43, 0x44, 0x57, 0x31, 0xA1, 0x0C, 0x7D, 0x43, 0x44, 0x57, 0x31, 0x0A,
    0x04, 0x43, 0x44, 0x57, 0x31, 0xA4, 0x6B
};

static CM_OSCC osiCc;

static void test_osi1(void)
{
    static const ULONG code[CM_OSC_METHODS] = {
        0x43EB7CDCUL, 0x2FF061F8UL, 0xCDA93C5FUL, 0xFE35E156UL, 0x408891DFUL
    };
    static const ULONG form[CM_OSC_METHODS][2] = {
        { 0x921E64CCUL, 0x0C073A4BUL }, { 0x9616AF17UL, 0x652CB991UL },
        { 0x4383C182UL, 0x78D55DC3UL }, { 0x5238222EUL, 0xE0E054ACUL },
        { 0xA4FF774BUL, 0x7BAE4E78UL }
    };
    static CM_OSC_WALK w;
    CM_OSC_TBL tbl[3];
    const UCHAR *t[3];
    ULONG len[3];
    ULONG fnv;
    ULONG crc;
    ULONG i;

    t[0] = osi1Cmn;
    len[0] = sizeof(osi1Cmn);
    t[1] = osi1Dus;
    len[1] = sizeof(osi1Dus);
    t[2] = osi1Osc;
    len[2] = sizeof(osi1Osc);
    memset(&w, 0, sizeof(w));
    for (i = 0; i < 3; i++) {
        CHECK(CmOscSumZero(t[i], len[i]), "an --osi table's checksum");
        tbl[i].Aml = t[i] + 36;
        tbl[i].Length = len[i] - 36;
        w.Table = i;
        CHECK(CmOscAmlWalk(t[i] + 36, len[i] - 36, &w),
              "an --osi table walked whole, its If (Zero) passed");
    }
    for (i = 0; i < CM_OSC_METHODS; i++) {
        CHECK_EQ(w.MethodSeen[i], 1, "each method once");
        CHECK_EQ(w.MethodHash[i], code[i],
                 "build 1 --osi: the qemu flavour's hash");
        CHECK_EQ(CmOscCanon(&osiCc, tbl, 3, &w, i, &fnv, &crc), 0,
                 "its form made");
        CHECK_EQ(fnv, form[i][0], "build 1 --osi: the form's FNV-1a");
        CHECK_EQ(crc, form[i][1], "build 1 --osi: the form's CRC-32");
    }
}

int main(void)
{
    test_rsdp();
    test_tables();
    test_aml();
    test_decide();
    test_known();
    test_header();
    test_find();
    test_canon();
    test_osi1();
    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
