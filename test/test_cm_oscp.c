/*
 * test_cm_oscp.c - the ASM4242 handover's known-board route on NT, its
 * pure half (src/cm/cm_oscp.c; roadmap-usb4.md task 46.12; design record
 * 32 sections 12.8.15.11, 12.8.15.13, 12.8.15.16 and 12.8.15.18): the WMI
 * block's chain, the SMBIOS identity, the profiles' match, the boot guard,
 * the board decided once a start and ECAM on 2000, XP and XP x64. Made-up
 * strings alone (TESTVND, TESTBRD ...), and the profiles by their hashes.
 */

#include <stdio.h>
#include <string.h>
#include "../src/cm/cm_oscp.h"
#include "test_harness.h"

static void put32(UCHAR *p, ULONG v)
{
    p[0] = (UCHAR)v;
    p[1] = (UCHAR)(v >> 8);
    p[2] = (UCHAR)(v >> 16);
    p[3] = (UCHAR)(v >> 24);
}

/* Appends a structure of `type`, formatted length `fl` (its bytes 4 on
 * given by `f`), and the strings (NULL-ended list) at `t` + *at. */
static void add(UCHAR *t, ULONG *at, UCHAR type, UCHAR fl, const UCHAR *f,
                const char *const *str)
{
    ULONG n;

    t[*at] = type;
    t[*at + 1] = fl;
    t[*at + 2] = 0;
    t[*at + 3] = 0;
    if (fl > 4) {
        memcpy(t + *at + 4, f, (size_t)(fl - 4));
    }
    *at += fl;
    if (str == NULL || str[0] == NULL) {
        t[(*at)++] = 0;
        t[(*at)++] = 0;
        return;
    }
    for (; *str != NULL; str++) {
        n = (ULONG)strlen(*str);
        memcpy(t + *at, *str, n + 1);
        *at += n + 1;
    }
    t[(*at)++] = 0;
}

static const char *const s0[] = { "TESTVND", "1.0", "01/02/2026", NULL };
static const char *const s2[] = { " TESTMKR ", "TESTBRD", NULL };
static const UCHAR f0[] = { 1, 2, 0, 0, 3 };     /* 04h..08h */
static const UCHAR f2[] = { 1, 2 };              /* 04h..05h */

static ULONG build(UCHAR *t, int variant)
{
    ULONG at;
    UCHAR g[8];

    at = 0;
    add(t, &at, 1, 4, NULL, NULL);
    if (variant == 1) {                 /* a type 0 short of 08h */
        add(t, &at, 0, 8, f0, s0);
    } else {
        add(t, &at, 0, 9, f0, s0);
    }
    if (variant == 2) {                 /* a type 2 short of 05h */
        add(t, &at, 2, 5, f2, s2);
    } else if (variant == 3) {          /* a bad string index */
        g[0] = 1;
        g[1] = 3;
        add(t, &at, 2, 6, g, s2);
    } else if (variant != 4) {          /* 4: no type 2 */
        add(t, &at, 2, 6, f2, s2);
    }
    if (variant == 5) {                 /* a second type 2 */
        add(t, &at, 2, 6, f2, s2);
    }
    add(t, &at, 127, 4, NULL, NULL);
    return at;
}

static void test_smbios(void)
{
    UCHAR t[512];
    CM_OSCP_SMBIOS sm;
    ULONG n;

    n = build(t, 0);
    CHECK_EQ(CmOscpSmbios(t, n, &sm), CM_OSCP_OK, "a whole identity");
    CHECK_EQ(sm.BiosVendor, CmOscpHash((const UCHAR *)"TESTVND", 7), "BIOS vendor");
    CHECK_EQ(sm.BoardMaker, CmOscpHash((const UCHAR *)"TESTMKR", 7), "board maker, spaces trimmed");
    CHECK_EQ(sm.BoardProduct, CmOscpHash((const UCHAR *)"TESTBRD", 7), "board product");
    CHECK_EQ(sm.BiosDate, CmOscpHash((const UCHAR *)"01/02/2026", 10), "BIOS date (logged)");
    n = build(t, 1);
    CHECK_EQ(CmOscpSmbios(t, n, &sm), CM_OSCP_SM_FIELD, "type 0 short of the date field");
    n = build(t, 2);
    CHECK_EQ(CmOscpSmbios(t, n, &sm), CM_OSCP_SM_FIELD, "type 2 short of the product field");
    n = build(t, 3);
    CHECK_EQ(CmOscpSmbios(t, n, &sm), CM_OSCP_SM_INDEX, "a string index past the set");
    n = build(t, 4);
    CHECK_EQ(CmOscpSmbios(t, n, &sm), CM_OSCP_SM_MISSING, "no type 2");
    n = build(t, 5);
    CHECK_EQ(CmOscpSmbios(t, n, &sm), CM_OSCP_SM_TWICE, "a second type 2");
    n = build(t, 0);
    CHECK_EQ(CmOscpSmbios(t, n - 7, &sm), CM_OSCP_SM_STRINGS, "a set cut before its end");
    t[1] = 200;
    CHECK_EQ(CmOscpSmbios(t, n, &sm), CM_OSCP_SM_STRUCT, "a length past the end");
    t[1] = 3;
    CHECK_EQ(CmOscpSmbios(t, n, &sm), CM_OSCP_SM_STRUCT, "a length below 4");
    {
        char longs[80];
        const char *ls[2];
        ULONG at;

        memset(longs, 'A', 70);
        longs[70] = 0;
        ls[0] = longs;
        ls[1] = NULL;
        {
            static const char *const one[] = { "TESTMKR", NULL };

            at = 0;
            add(t, &at, 0, 9, f0, s0);
            add(t, &at, 2, 6, f2, one);
            CHECK_EQ(CmOscpSmbios(t, at, &sm), CM_OSCP_SM_INDEX, "the product index past a one-string set");
        }
        {
            UCHAR g[2];

            g[0] = 1;
            g[1] = 1;
            at = 0;
            add(t, &at, 0, 9, f0, s0);
            add(t, &at, 2, 6, g, ls);
            CHECK_EQ(CmOscpSmbios(t, at, &sm), CM_OSCP_SM_LONG, "a string over 64 bytes");
        }
    }
}

/* A WNODE_ALL_DATA of one instance around `tables`. */
static ULONG wnode(UCHAR *b, const UCHAR *tables, ULONG tlen, int fixed,
                   ULONG count, ULONG next)
{
    ULONG data;
    ULONG size;

    memset(b, 0, 96);
    data = 72;
    size = data + 8 + tlen;
    put32(b + 0, size);
    put32(b + 12, next);
    put32(b + 44, 1UL | (fixed ? 0x10UL : 0));
    put32(b + 48, data);
    put32(b + 52, count);
    if (fixed) {
        put32(b + 60, 8 + tlen);
    } else {
        put32(b + 60, data);
        put32(b + 64, 8 + tlen);
    }
    b[data] = 0;
    b[data + 1] = 3;
    b[data + 2] = 0;
    put32(b + data + 4, tlen);
    memcpy(b + data + 8, tables, tlen);
    return size;
}

/* More vectors: zero indexes, string lengths, type 0's count. */
static void test_smbios2(void)
{
    UCHAR t[512];
    CM_OSCP_SMBIOS sm;
    ULONG at;
    UCHAR g0[5];
    UCHAR g2[2];
    char s64[65];
    char s65[66];
    const char *l2[4];
    int k;

    for (k = 0; k < 5; k++) {
        memcpy(g0, f0, 5);
        memcpy(g2, f2, 2);
        if (k < 3) {
            g0[k == 2 ? 4 : k] = 0;     /* 04h, 05h, 08h */
        } else {
            g2[k - 3] = 0;              /* 04h, 05h */
        }
        at = 0;
        add(t, &at, 0, 9, g0, s0);
        add(t, &at, 2, 6, g2, s2);
        CHECK_EQ(CmOscpSmbios(t, at, &sm), CM_OSCP_SM_INDEX, "a zero string index refused");
    }
    memset(s64, 'B', 64);
    s64[64] = 0;
    memset(s65, 'C', 65);
    s65[65] = 0;
    l2[0] = s64;
    l2[1] = "TESTBRD";
    l2[2] = NULL;
    at = 0;
    add(t, &at, 0, 9, f0, s0);
    add(t, &at, 2, 6, f2, l2);
    CHECK_EQ(CmOscpSmbios(t, at, &sm), CM_OSCP_OK, "a 64-byte string taken");
    l2[0] = s65;
    at = 0;
    add(t, &at, 0, 9, f0, s0);
    add(t, &at, 2, 6, f2, l2);
    CHECK_EQ(CmOscpSmbios(t, at, &sm), CM_OSCP_SM_LONG, "a 65-byte string refused");
    l2[0] = "TESTMKR";
    l2[1] = "TESTBRD";
    l2[2] = s65;
    l2[3] = NULL;
    at = 0;
    add(t, &at, 0, 9, f0, s0);
    add(t, &at, 2, 6, f2, l2);
    CHECK_EQ(CmOscpSmbios(t, at, &sm), CM_OSCP_SM_LONG, "an unread long string refused");
    at = 0;
    add(t, &at, 0, 9, f0, s0);
    add(t, &at, 0, 9, f0, s0);
    add(t, &at, 2, 6, f2, s2);
    CHECK_EQ(CmOscpSmbios(t, at, &sm), CM_OSCP_SM_TWICE, "a second type 0");
    at = 0;
    add(t, &at, 2, 6, f2, s2);
    CHECK_EQ(CmOscpSmbios(t, at, &sm), CM_OSCP_SM_MISSING, "no type 0");
}

static void test_wmi(void)
{
    UCHAR t[512];
    UCHAR b[2048];
    const UCHAR *r;
    ULONG n;
    ULONG rl;
    ULONG size;
    ULONG size2;

    n = build(t, 0);
    size = wnode(b, t, n, 1, 1, 0);
    CHECK_EQ(CmOscpWmiRaw(b, size, &r, &rl), CM_OSCP_OK, "one node, fixed size");
    CHECK(r == b + 80 && rl == n, "the tables found");
    size = wnode(b, t, n, 0, 1, 0);
    CHECK_EQ(CmOscpWmiRaw(b, size, &r, &rl), CM_OSCP_OK, "one node, offset and length");
    size = wnode(b, t, n, 1, 2, 0);
    CHECK_EQ(CmOscpWmiRaw(b, size, &r, &rl), CM_OSCP_WNODE_COUNT, "two instances in a node");
    size = wnode(b, t, n, 1, 0, 0);
    CHECK_EQ(CmOscpWmiRaw(b, size, &r, &rl), CM_OSCP_WNODE_COUNT, "no instance");
    size = wnode(b, t, n, 1, 1, 0);
    CHECK_EQ(CmOscpWmiRaw(b, size - 1, &r, &rl), CM_OSCP_WNODE_SHORT, "a node past the buffer");
    /* Two nodes of one instance each. */
    size = (wnode(b, t, n, 1, 1, 0) + 7UL) & ~7UL;
    size2 = wnode(b + size, t, n, 1, 1, 0);
    put32(b + 12, size);
    CHECK_EQ(CmOscpWmiRaw(b, size + size2, &r, &rl), CM_OSCP_WNODE_COUNT, "an instance in each of two nodes");
    put32(b + size + 52, 0);
    CHECK_EQ(CmOscpWmiRaw(b, size + size2, &r, &rl), CM_OSCP_OK, "a second node with none");
    put32(b + 12, 8);
    CHECK_EQ(CmOscpWmiRaw(b, size + size2, &r, &rl), CM_OSCP_WNODE_LINK, "a next offset inside the header");
    put32(b + 12, size);
    put32(b + size + 12, 0xFFFFFFF0UL);
    CHECK_EQ(CmOscpWmiRaw(b, size + size2, &r, &rl), CM_OSCP_WNODE_LINK, "a second node's next offset that would wrap");
    put32(b + size + 12, 0);
    size = wnode(b, t, n, 1, 1, 0);
    CHECK_EQ(CmOscpWmiRaw(b, 63, &r, &rl), CM_OSCP_WNODE_SHORT, "a buffer shorter than a node");
    put32(b + 48, size);
    CHECK_EQ(CmOscpWmiRaw(b, size, &r, &rl), CM_OSCP_WNODE_DATA, "a fixed instance's offset past its node");
    size = wnode(b, t, n, 0, 1, 0);
    put32(b + 64, 0x10000UL);
    CHECK_EQ(CmOscpWmiRaw(b, size, &r, &rl), CM_OSCP_WNODE_DATA, "a variable instance's length past its node");
    size = wnode(b, t, n, 0, 1, 0);
    put32(b + 0, 66);
    CHECK_EQ(CmOscpWmiRaw(b, size, &r, &rl), CM_OSCP_WNODE_SHORT, "a variable node too short for its pair");
    size = wnode(b, t, n, 1, 1, 0);
    put32(b + 76, 0x1000);
    CHECK_EQ(CmOscpWmiRaw(b, size, &r, &rl), CM_OSCP_RAW_LENGTH, "the raw length past the instance");
}

static void test_match(void)
{
    CM_OSCP_PCI pci;
    CM_OSCP_SMBIOS sm;
    ULONG data;
    const CM_OSCP_PROFILE *p;

#ifdef CM_OSCP_RELEASE
    CHECK(CmOscpProfileCount == 1, "the bench board alone in the installer's build");
#else
    CHECK(CmOscpProfileCount >= 2, "the bench board and the emulator in the host build");
#endif
    p = &CmOscpProfiles[0];
    pci = p->Pci;
    memset(&sm, 0, sizeof(sm));
    sm.BiosVendor = p->BiosVendor;
    sm.BoardMaker = p->BoardMaker;
    sm.BoardProduct = p->BoardProduct;
    data = 0xFF;
    CHECK_EQ(CmOscpMatch(&pci, &sm, &data), 1UL, "the bench board known");
    CHECK_EQ(data, 0UL, "its data byte");
    sm.BiosVersion = 0x1234;
    CHECK_EQ(CmOscpMatch(&pci, &sm, &data), 1UL, "a BIOS update keeps it known");
    sm.BoardProduct ^= 1;
    CHECK_EQ(CmOscpMatch(&pci, &sm, &data), 0UL, "another product unknown");
    sm.BoardProduct ^= 1;
    sm.BiosVendor ^= 1;
    CHECK_EQ(CmOscpMatch(&pci, &sm, &data), 0UL, "another BIOS vendor unknown");
    sm.BiosVendor ^= 1;
    pci.RootId ^= 0x10000;
    CHECK_EQ(CmOscpMatch(&pci, &sm, &data), 0UL, "another root port unknown");
    pci.RootId ^= 0x10000;
    pci.NhiSubsystem = 0;
    CHECK_EQ(CmOscpMatch(&pci, &sm, &data), 0UL, "another subsystem unknown");
    pci = p->Pci;
    pci.UspId ^= 1;
    CHECK_EQ(CmOscpMatch(&pci, &sm, &data), 0UL, "another upstream port unknown");
    pci = p->Pci;
    pci.DspId ^= 1;
    CHECK_EQ(CmOscpMatch(&pci, &sm, &data), 0UL, "another downstream port unknown");
    pci = p->Pci;
    pci.NhiId ^= 1;
    CHECK_EQ(CmOscpMatch(&pci, &sm, &data), 0UL, "another NHI unknown");
    pci = p->Pci;
    sm.BoardMaker ^= 1;
    CHECK_EQ(CmOscpMatch(&pci, &sm, &data), 0UL, "another board maker unknown");
    sm.BoardMaker ^= 1;
    /* The emulator's identity: its root port 1B36:000C, the ASM4242's
     * three functions, its strings. */
    pci = p->Pci;
    pci.RootId = 0x000C1B36UL;
    sm.BiosVendor = CmOscpHash((const UCHAR *)"SeaBIOS", 7);
    sm.BoardMaker = CmOscpHash((const UCHAR *)"usb4-emu", 8);
    sm.BoardProduct = CmOscpHash((const UCHAR *)"asm4242-model", 13);
#ifdef CM_OSCP_RELEASE
    CHECK_EQ(CmOscpMatch(&pci, &sm, &data), 0UL, "the emulator unknown in the installer's build");
#else
    CHECK_EQ(CmOscpMatch(&pci, &sm, &data), 2UL, "the emulator's identity, by its strings");
#endif
}

static void test_guard(void)
{
    CHECK_EQ(CmOscgAtBoot(CM_OSCG_M_ABSENT), CM_OSCG_GO, "no marker: may send");
    CHECK_EQ(CmOscgAtBoot(CM_OSCG_M_PRESENT), CM_OSCG_SKIP_CLEAR, "a marker: skip once, clear");
    CHECK_EQ(CmOscgAtBoot(CM_OSCG_M_UNREADABLE), CM_OSCG_SKIP, "unreadable: skip");
    CHECK(CmOscgClears(CM_OSCG_E_ROUTER), "the router answering clears");
    CHECK(CmOscgClears(CM_OSCG_E_SHUTDOWN), "an orderly shutdown clears");
    CHECK(!CmOscgClears(CM_OSCG_E_INERT), "an inert start keeps it");
    CHECK(!CmOscgClears(CM_OSCG_E_RESTART), "a disable and enable keeps it");
    CHECK_EQ(CmOscgCleared(TRUE, TRUE), CM_OSCG_C_DONE, "deleted and flushed");
    CHECK_EQ(CmOscgCleared(FALSE, TRUE), CM_OSCG_C_KEPT, "the delete failed");
    CHECK_EQ(CmOscgCleared(TRUE, FALSE), CM_OSCG_C_LATCH, "deleted, the flush failed: latch");
    CHECK(CmOscgMaySend(FALSE, TRUE, TRUE), "armed: send");
    CHECK(!CmOscgMaySend(FALSE, FALSE, TRUE), "the marker's write failed: no send");
    CHECK(!CmOscgMaySend(FALSE, TRUE, FALSE), "its flush failed: no send");
    CHECK(!CmOscgMaySend(TRUE, TRUE, TRUE), "the latch (after an unknown clear): no S4 resend");
    /* Two hangs in a row: skip, send, skip. */
    CHECK_EQ(CmOscgAtBoot(CM_OSCG_M_PRESENT), CM_OSCG_SKIP_CLEAR, "boot 2 skips");
    CHECK_EQ(CmOscgAtBoot(CM_OSCG_M_ABSENT), CM_OSCG_GO, "boot 3 sends");
    CHECK_EQ(CmOscgAtBoot(CM_OSCG_M_PRESENT), CM_OSCG_SKIP_CLEAR, "boot 4 skips");
}

static void test_members(void)
{
    CM_OSCP_MEMBER have[3];
    ULONG want[3];
    ULONG role[3];
    ULONG missing;

    CHECK_EQ(CmOscpMemberRole(0x1B21, 0x2421, TRUE, TRUE, TRUE), CM_OSCP_R_USP,
             "the upstream port above the PCI driver");
    CHECK_EQ(CmOscpMemberRole(0x1B21, 0x2421, TRUE, FALSE, FALSE), CM_OSCP_R_NONE,
             "the upstream port below it: no member");
    CHECK_EQ(CmOscpMemberRole(0x1B21, 0x2421, FALSE, TRUE, TRUE), CM_OSCP_R_NONE,
             "its ids with another class: no member");
    CHECK_EQ(CmOscpMemberRole(0x1B21, 0x2423, TRUE, TRUE, TRUE), CM_OSCP_R_DSP,
             "a downstream port");
    CHECK_EQ(CmOscpMemberRole(0x1B21, 0x2426, FALSE, FALSE, TRUE), CM_OSCP_R_XHCI,
             "the xHCI above its function driver");
    CHECK_EQ(CmOscpMemberRole(0x1B21, 0x2426, FALSE, FALSE, FALSE), CM_OSCP_R_NONE,
             "the xHCI with nothing above: no member");
    CHECK_EQ(CmOscpMemberRole(0x1B21, 0x2425, FALSE, FALSE, TRUE), CM_OSCP_R_NONE,
             "the NHI is the initiator, no member");
    CHECK_EQ(CmOscpMemberRole(0x8086, 0x2421, TRUE, TRUE, TRUE), CM_OSCP_R_NONE,
             "another vendor");

    memset(have, 0, sizeof(have));
    have[0].Role = CM_OSCP_R_USP;  have[0].Bdf = 0x1300; have[0].Located = 1;
    have[0].Started = 1;
    have[1].Role = CM_OSCP_R_DSP;  have[1].Bdf = 0x1410; have[1].Located = 1;
    have[1].Started = 1;
    have[2].Role = CM_OSCP_R_XHCI; have[2].Bdf = 0x7900; have[2].Located = 1;
    have[2].Started = 1;
    want[0] = 0x1300; role[0] = CM_OSCP_R_USP;
    want[1] = 0x1410; role[1] = CM_OSCP_R_DSP;
    want[2] = 0x7900; role[2] = CM_OSCP_R_XHCI;
    CHECK(CmOscpCovered(want, role, 3, have, 3, &missing), "all three covered");
    have[2].Started = 0;
    CHECK(!CmOscpCovered(want, role, 3, have, 3, &missing) && missing == 2,
          "the xHCI not started: missing");
    have[2].Started = 1;
    have[1].Closed = 1;
    CHECK(!CmOscpCovered(want, role, 3, have, 3, &missing) && missing == 1,
          "a query-remove succeeded: missing");
    have[1].Closed = 0;
    have[0].Gone = 1;
    CHECK(!CmOscpCovered(want, role, 3, have, 3, &missing) && missing == 0,
          "a removal: missing");
    have[0].Gone = 0;
    have[0].Located = 0;
    CHECK(!CmOscpCovered(want, role, 3, have, 3, &missing) && missing == 0,
          "not located: missing");
    have[0].Located = 1;
    CHECK(!CmOscpCovered(want, role, 3, have, 2, &missing) && missing == 2,
          "the xHCI's filter absent: missing");
    role[1] = CM_OSCP_R_XHCI;
    CHECK(!CmOscpCovered(want, role, 3, have, 3, &missing) && missing == 1,
          "another role at that location: missing");

    CHECK_EQ(CmOscpOwed(0, 5), 1UL, "a hibernate request: owed");
    CHECK_EQ(CmOscpOwed(1, 3), 0UL, "then a sleep: not owed");
    CHECK_EQ(CmOscpOwed(1, 1), 1UL, "S0 changes nothing");
    CHECK_EQ(CmOscpOwed(0, 1), 0UL, "S0 owes nothing");
    CHECK_EQ(CmOscpOwed(1, 6), 1UL, "a shutdown changes nothing");
    CHECK_EQ(CmOscpOwed(1, 2), 0UL, "S1: not owed");
}

/* ---- the read path (record 32 section 12.8.15.17) -------------------- */

static void rng(CM_OSCP_RANGE *r, ULONG baseLo, ULONG baseHi, ULONG bytesLo,
                ULONG bytesHi)
{
    r->BaseLo = baseLo;
    r->BaseHi = baseHi;
    r->BytesLo = bytesLo;
    r->BytesHi = bytesHi;
}

static void test_ranges(void)
{
    CM_OSCP_RANGE r[4];

    /* RAM 1 MB to 7FF00000h (exclusive). */
    rng(&r[0], 0x00100000UL, 0, 0x7FE00000UL, 0);
    CHECK(CmOscpRangeHit(r, 1, 0x00200000UL, 16), "a read inside");
    CHECK(CmOscpRangeHit(r, 1, 0x000FFFF0UL, 0x20), "over the start");
    CHECK(CmOscpRangeHit(r, 1, 0x7FEFFFF0UL, 0x20), "over the end");
    CHECK(!CmOscpRangeHit(r, 1, 0x7FF00000UL, 16), "touching, outside");
    CHECK(!CmOscpRangeHit(r, 1, 0x000F0000UL, 0x10000UL),
          "the BIOS area below it");
    /* A range starting mid-page: the read's own bytes outside it, its
     * page not. */
    rng(&r[1], 0x9FF00800UL, 0, 0x800UL, 0);
    CHECK(CmOscpRangeHit(r + 1, 1, 0x9FF00010UL, 4),
          "outside the range, its page inside");
    CHECK(!CmOscpRangeHit(r + 1, 1, 0x9FEFFFF0UL, 4), "the page before");
    CHECK(CmOscpRangeHit(r + 1, 1, 0x9FEFFFFEUL, 4),
          "across a page boundary into it");
    /* Above 4 GB: ignored; ending past 4 GB: to 4 GB. */
    rng(&r[2], 0x00000000UL, 1, 0x40000000UL, 0);
    CHECK(!CmOscpRangeHit(r + 2, 1, 0x00100000UL, 4), "a range above 4 GB");
    rng(&r[3], 0xC0000000UL, 0, 0x80000000UL, 0);
    CHECK(CmOscpRangeHit(r + 3, 1, 0xFFFFFFF0UL, 0x10),
          "a range ending past 4 GB, a read ending at FFFFFFFFh");
    CHECK(!CmOscpRangeHit(r, 1, 0xFFFFFFF0UL, 0x10),
          "a read ending exactly at FFFFFFFFh, outside");
    CHECK(CmOscpRangeHit(r, 1, 0xFFFFFFF0UL, 0x11), "a read past 4 GB");
    CHECK(CmOscpRangeHit(r, 1, 0x00500000UL, 0), "a read of no bytes");
    rng(&r[3], 0xC0000000UL, 0, 0, 1);
    CHECK(CmOscpRangeHit(r + 3, 1, 0xE0000000UL, 4),
          "a length of 4 GB from C0000000h");
    CHECK(!CmOscpRangeHit(r + 3, 1, 0xBFFFF000UL, 0x1000),
          "the page below it");
    rng(&r[3], 0, 0, 0, 0);
    CHECK(!CmOscpRangeHit(r + 3, 1, 0x00100000UL, 4),
          "the zero pair holds nothing");
    CHECK_EQ(CM_OSCP_RANGES_MAX, 256, "256 pairs read at most");
}

static void test_route(void)
{
    ULONG need;

    CHECK_EQ(CmOscpReadRoute(CM_OSCP_HAVE_EX2 | CM_OSCP_SYS_130, &need),
             CM_OSCP_RT_B, "Ex2: B");
    CHECK_EQ(need, CM_OSCP_HAVE_EX2 | CM_OSCP_HAVE_FWENV,
             "B needs the boot test");
    CHECK_EQ(CmOscpReadRoute(CM_OSCP_SYS_130 | CM_OSCP_HAVE_RANGES |
                             CM_OSCP_HAVE_QUERY, &need),
             CM_OSCP_RT_C, "no Ex2, 1.30: C");
    CHECK_EQ(need, CM_OSCP_HAVE_RANGES | CM_OSCP_HAVE_QUERY, "C's routines");
    CHECK_EQ(CmOscpReadRoute(CM_OSCP_SYS_130 | CM_OSCP_SYS_X86, &need),
             CM_OSCP_RT_C, "1.30 x86 without its routines: still C (refused)");
    CHECK_EQ(CmOscpReadRoute(CM_OSCP_SYS_X86 | CM_OSCP_HAVE_RANGES, &need),
             CM_OSCP_RT_D, "below 1.30 on x86: D");
    CHECK_EQ(need, CM_OSCP_HAVE_RANGES, "D's routine");
    CHECK_EQ(CmOscpReadRoute(CM_OSCP_HAVE_RANGES, &need), CM_OSCP_RT_NONE,
             "amd64 below 1.30: none, never D");

    CHECK_EQ(CmOscpAfterB(CM_OSCP_S_OBTAINED, CM_OSCP_S_OBTAINED),
             CM_OSCP_RT_B, "BIOS boot, pointer: B");
    CHECK_EQ(CmOscpAfterB(CM_OSCP_S_OBTAINED, CM_OSCP_S_ABSENT),
             CM_OSCP_RT_C, "BIOS boot, no pointer: C");
    CHECK_EQ(CmOscpAfterB(CM_OSCP_S_OBTAINED, CM_OSCP_S_REFUSED),
             CM_OSCP_RT_NONE, "a page refused: no fallback");
    CHECK_EQ(CmOscpAfterB(CM_OSCP_S_ABSENT, CM_OSCP_S_REFUSED),
             CM_OSCP_RT_C, "UEFI: C, nothing scanned");
    CHECK_EQ(CmOscpAfterB(CM_OSCP_S_REFUSED, CM_OSCP_S_OBTAINED),
             CM_OSCP_RT_NONE, "the boot type not read: refused");

    CHECK(CmOscpSystem(TRUE, 19045, TRUE), "10 22H2");
    CHECK(!CmOscpSystem(TRUE, 22000, TRUE), "11");
    CHECK(CmOscpSystem(FALSE, 0, FALSE), "2000, no RtlGetVersion");
    CHECK(!CmOscpSystem(FALSE, 0, TRUE), "XP or later without it");
}

static void fwt(UCHAR *t, ULONG len)
{
    ULONG i;
    ULONG sum;

    memset(t, 0, len);
    memcpy(t, "MCFG", 4);
    put32(t + 4, len);
    for (i = 36; i < len; i++) {
        t[i] = (UCHAR)i;
    }
    sum = 0;
    for (i = 0; i < len; i++) {
        sum += t[i];
    }
    t[9] = (UCHAR)(0x100UL - (sum & 0xFFUL));
}

static void test_fwtable(void)
{
    UCHAR t[0x80];

    fwt(t, 0x3C);
    CHECK_EQ(CmOscpFwTable(TRUE, FALSE, 0x3C, 0x1000, t), CM_OSCP_S_OBTAINED,
             "a table whole");
    CHECK_EQ(CmOscpFwTable(TRUE, FALSE, 0x3C, 0x3B, t), CM_OSCP_S_REFUSED,
             "longer than the bound");
    CHECK_EQ(CmOscpFwTable(TRUE, FALSE, 0x20, 0x1000, t), CM_OSCP_S_REFUSED,
             "shorter than a header");
    CHECK_EQ(CmOscpFwTable(TRUE, FALSE, 0x40, 0x1000, t), CM_OSCP_S_REFUSED,
             "unequal to its Length");
    t[40] ^= 1;
    CHECK_EQ(CmOscpFwTable(TRUE, FALSE, 0x3C, 0x1000, t), CM_OSCP_S_REFUSED,
             "a bad checksum");
    CHECK_EQ(CmOscpFwTable(FALSE, TRUE, 0, 0x1000, t), CM_OSCP_S_ABSENT,
             "not found: absent");
    CHECK_EQ(CmOscpFwTable(FALSE, FALSE, 0, 0x1000, t), CM_OSCP_S_REFUSED,
             "another failure: refused");
}

/* The board decided once a start (record 32 section 12.8.15.16). */
static void test_board(void)
{
    ULONG s;
    ULONG f;
    ULONG reads;
    ULONG kept;
    ULONG mailbox;

    CHECK_EQ(CmOscpBoardNext(CM_OSCP_K_NONE, FALSE, FALSE, FALSE,
                             CM_OSCP_F_UNTESTED), CM_OSCP_B_MATCH,
             "nothing kept: E's match first");
    CHECK_EQ(CmOscpBoardNext(CM_OSCP_K_NONE, TRUE, TRUE, TRUE, CM_OSCP_F_OK),
             CM_OSCP_B_MATCH, "the match before the latch and the key");
    CHECK_EQ(CmOscpBoardNext(CM_OSCP_K_UNKNOWN, FALSE, FALSE, FALSE,
                             CM_OSCP_F_UNTESTED), CM_OSCP_B_UNKNOWN,
             "unknown, reads 0: done, never the key's test");
    CHECK_EQ(CmOscpBoardNext(CM_OSCP_K_UNKNOWN, FALSE, TRUE, FALSE,
                             CM_OSCP_F_FAILED), CM_OSCP_B_UNKNOWN,
             "unknown, reads 0, latched, unflushable: done, never deferred");
    CHECK_EQ(CmOscpBoardNext(CM_OSCP_K_SEND, FALSE, TRUE, TRUE,
                             CM_OSCP_F_UNTESTED), CM_OSCP_B_LATCHED,
             "a known board latched: refused before the key");
    CHECK_EQ(CmOscpBoardNext(CM_OSCP_K_SEND, FALSE, TRUE, TRUE,
                             CM_OSCP_F_FAILED), CM_OSCP_B_LATCHED,
             "latched and unflushable: refused, never deferred");
    CHECK_EQ(CmOscpBoardNext(CM_OSCP_K_UNKNOWN, TRUE, TRUE, FALSE,
                             CM_OSCP_F_UNTESTED), CM_OSCP_B_LATCHED,
             "reads 1 latched: refused before the probe and the read");
    CHECK_EQ(CmOscpBoardNext(CM_OSCP_K_SEND, FALSE, FALSE, FALSE,
                             CM_OSCP_F_UNTESTED), CM_OSCP_B_PROBE,
             "a known board: the probe before the marker");
    CHECK_EQ(CmOscpBoardNext(CM_OSCP_K_UNKNOWN, TRUE, FALSE, FALSE,
                             CM_OSCP_F_OK), CM_OSCP_B_PROBE,
             "reads 1: the probe before the read");
    CHECK_EQ(CmOscpBoardNext(CM_OSCP_K_UNKNOWN, TRUE, FALSE, TRUE,
                             CM_OSCP_F_UNTESTED), CM_OSCP_B_FLUSH,
             "reads 1, probed: the key before the read");
    CHECK_EQ(CmOscpBoardNext(CM_OSCP_K_UNKNOWN, TRUE, FALSE, TRUE,
                             CM_OSCP_F_FAILED), CM_OSCP_B_DEFER,
             "reads 1, the key not flushable: deferred, nothing read");
    CHECK_EQ(CmOscpBoardNext(CM_OSCP_K_UNKNOWN, TRUE, FALSE, TRUE,
                             CM_OSCP_F_OK), CM_OSCP_B_READ,
             "reads 1, the key flushable: the read");
    CHECK_EQ(CmOscpBoardNext(CM_OSCP_K_SEND, TRUE, FALSE, TRUE,
                             CM_OSCP_F_OK), CM_OSCP_B_SEND,
             "read and kept, the key tested this try: the send");
    CHECK_EQ(CmOscpBoardNext(CM_OSCP_K_SEND, FALSE, FALSE, TRUE,
                             CM_OSCP_F_UNTESTED), CM_OSCP_B_FLUSH,
             "a kept send: the key tested once this try");
    CHECK_EQ(CmOscpBoardNext(CM_OSCP_K_SEND, FALSE, FALSE, TRUE,
                             CM_OSCP_F_FAILED), CM_OSCP_B_DEFER,
             "a kept send, the key not flushable: deferred");
    CHECK_EQ(CmOscpBoardNext(3UL, TRUE, FALSE, TRUE, CM_OSCP_F_OK),
             CM_OSCP_B_MATCH, "a state not known: decided afresh");

    /* Every input: a read only after the probe and a flushable key, and
     * never with reads 0; a deferral never when latched. */
    for (kept = 0; kept < 3UL; kept++) {
        for (reads = 0; reads < 2UL; reads++) {
            for (mailbox = 0; mailbox < 4UL; mailbox++) {
                for (f = 0; f < 3UL; f++) {
                    s = CmOscpBoardNext(kept, (BOOLEAN)reads,
                                        (BOOLEAN)(mailbox & 1UL),
                                        (BOOLEAN)(mailbox >> 1), f);
                    CHECK(s != CM_OSCP_B_READ ||
                          (reads && (mailbox >> 1) && !(mailbox & 1UL) &&
                           f == CM_OSCP_F_OK && kept == CM_OSCP_K_UNKNOWN),
                          "a read only probed, flushable, reads 1");
                    CHECK(s != CM_OSCP_B_DEFER || !(mailbox & 1UL),
                          "latched: never deferred");
                    CHECK(s != CM_OSCP_B_SEND ||
                          (f == CM_OSCP_F_OK && (mailbox >> 1) &&
                           !(mailbox & 1UL)),
                          "a send only probed, flushable, unlatched");
                }
            }
        }
    }
}

static void kept_host(CM_OSCP_KEPT *k)
{
    memset(k, 0, sizeof(*k));
    k->NhiBdf = 0x6400UL;
    k->DspBdf = 0x6318UL;
    k->UspBdf = 0x0300UL;
    k->RootBdf = 0x0018UL;
    k->Pci.RootId = 0x000C1B36UL;
    k->Pci.UspId = 0x24211B21UL;
    k->Pci.DspId = 0x24231B21UL;
    k->Pci.NhiId = 0x24251B21UL;
    k->Pci.NhiSubsystem = 0x24211B21UL;
}

static void test_kept(void)
{
    CM_OSCP_KEPT k;
    CM_OSCP_KEPT now;
    ULONG i;
    ULONG *field;
    ULONG step;
    ULONG reads;

    kept_host(&now);
    memset(&k, 0, sizeof(k));
    k.NhiBdf = 0x1234UL;
    CHECK(CmOscpKept(&k, &now), "nothing kept: any host");
    kept_host(&k);
    k.State = CM_OSCP_K_SEND;
    k.Tier = 2;
    k.Data = 0x4B;
    k.Probed = 1;
    CHECK(CmOscpKept(&k, &now), "the same host: kept");
    now.Tier = 9;
    now.Data = 7;
    now.Probed = 0;
    now.State = CM_OSCP_K_NONE;
    CHECK(CmOscpKept(&k, &now), "the host alone compared");
    k.State = CM_OSCP_K_UNKNOWN;
    CHECK(CmOscpKept(&k, &now), "an unknown board kept: the same host");
    k.State = CM_OSCP_K_SEND;
    for (i = 0; i < 9UL; i++) {
        kept_host(&now);
        switch (i) {
        case 0: field = &now.NhiBdf; break;
        case 1: field = &now.DspBdf; break;
        case 2: field = &now.UspBdf; break;
        case 3: field = &now.RootBdf; break;
        case 4: field = &now.Pci.RootId; break;
        case 5: field = &now.Pci.UspId; break;
        case 6: field = &now.Pci.DspId; break;
        case 7: field = &now.Pci.NhiId; break;
        default: field = &now.Pci.NhiSubsystem; break;
        }
        *field ^= 0x100UL;
        CHECK(!CmOscpKept(&k, &now), "one address or id changed: refused");
        *field = 0xFFFFFFFFUL;
        CHECK(!CmOscpKept(&k, &now), "one read as all ones: refused");
    }

    /* A start: read, a coverage deferral (the send's), then a try whose
     * host changed: refused, no second read. */
    kept_host(&now);
    memset(&k, 0, sizeof(k));
    reads = 0;
    step = CmOscpBoardNext(k.State, TRUE, FALSE, FALSE, CM_OSCP_F_UNTESTED);
    CHECK_EQ(step, CM_OSCP_B_MATCH, "try 1: the match");
    k = now;
    k.State = CM_OSCP_K_UNKNOWN;
    step = CmOscpBoardNext(k.State, TRUE, FALSE, FALSE, CM_OSCP_F_UNTESTED);
    CHECK_EQ(step, CM_OSCP_B_PROBE, "try 1: the probe");
    k.Probed = 1;
    step = CmOscpBoardNext(k.State, TRUE, FALSE, TRUE, CM_OSCP_F_UNTESTED);
    CHECK_EQ(step, CM_OSCP_B_FLUSH, "try 1: the key");
    step = CmOscpBoardNext(k.State, TRUE, FALSE, TRUE, CM_OSCP_F_OK);
    CHECK_EQ(step, CM_OSCP_B_READ, "try 1: the read");
    reads++;
    k.State = CM_OSCP_K_SEND;
    step = CmOscpBoardNext(k.State, TRUE, FALSE, TRUE, CM_OSCP_F_OK);
    CHECK_EQ(step, CM_OSCP_B_SEND, "try 1: the send (coverage defers)");
    CHECK(CmOscpKept(&k, &now), "try 2: the same host");
    step = CmOscpBoardNext(k.State, TRUE, FALSE, TRUE, CM_OSCP_F_UNTESTED);
    CHECK_EQ(step, CM_OSCP_B_FLUSH, "try 2: the key, no match, no probe");
    step = CmOscpBoardNext(k.State, TRUE, FALSE, TRUE, CM_OSCP_F_OK);
    CHECK_EQ(step, CM_OSCP_B_SEND, "try 2: the send, no read");
    now.UspBdf = 0x0400UL;
    CHECK(!CmOscpKept(&k, &now), "try 3: the host changed: refused");
    CHECK_EQ(reads, 1, "one read in the start");
}

/* The mailbox probe (record 32 section 12.8.15.16). */
static void test_probe(void)
{
    ULONG why;
    ULONG r;
    ULONG step;
    ULONG kept;
    BOOLEAN probed;

    why = 1;
    CHECK_EQ(CmOscpProbe(TRUE, TRUE, FALSE, TRUE, TRUE, FALSE, TRUE, 0x00000000UL,
                         &why), 0, "an idle mailbox: passed");
    CHECK_EQ(why, 0, "passed: no reason");
    CHECK_EQ(CmOscpProbe(TRUE, TRUE, FALSE, TRUE, TRUE, FALSE, TRUE, 0x00000100UL,
                         &why), 0, "a command there: passed (the send decides)");
    CHECK_EQ(CmOscpProbe(FALSE, TRUE, FALSE, TRUE, TRUE, FALSE, TRUE, 0, &why),
             CM_OSCP_P_DEFER, "the hold not had: deferred");
    CHECK_EQ(why, 0x10EUL, "the hold not had: 10Eh");
    CHECK_EQ(CmOscpProbe(TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE,
                         0xFFFFFFFFUL, &why), CM_OSCP_P_DEFER,
             "the member not usable: deferred");
    CHECK_EQ(why, 0x10AUL, "the member not usable: 10Ah");
    CHECK_EQ(CmOscpProbe(TRUE, TRUE, TRUE, FALSE, FALSE, FALSE, FALSE, 0xFFFFFFFFUL,
                         &why), CM_OSCP_P_DEFER,
             "a removal during it (reads failed): deferred, not 10Bh");
    CHECK_EQ(why, 0x10BUL, "aborted: logged 10Bh");
    CHECK_EQ(CmOscpProbe(TRUE, TRUE, TRUE, TRUE, TRUE, FALSE, TRUE, 0, &why),
             CM_OSCP_P_DEFER, "aborted after good reads: deferred");
    CHECK_EQ(CmOscpProbe(TRUE, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE,
                         0xFFFFFFFFUL, &why), 0x10BUL,
             "the route reaches another function: refused 10Bh");
    CHECK_EQ(CmOscpProbe(TRUE, TRUE, FALSE, TRUE, FALSE, FALSE, FALSE,
                         0xFFFFFFFFUL, &why), 0x104UL,
             "the port not in D0: refused 104h");
    CHECK_EQ(CmOscpProbe(TRUE, TRUE, FALSE, TRUE, TRUE, FALSE, FALSE, 0, &why),
             0x109UL, "a short mailbox read: refused 109h");
    CHECK_EQ(CmOscpProbe(TRUE, TRUE, FALSE, TRUE, TRUE, FALSE, TRUE, 0xFFFFFFFFUL,
                         &why), 0x109UL,
             "the mailbox all ones (XP): refused 109h");
    CHECK(CM_OSCP_P_DEFER != 0x109UL && CM_OSCP_P_DEFER != 0x10BUL &&
          CM_OSCP_P_DEFER != 0x104UL, "a deferral is no refusal code");

    /* A probe deferred, then passed on the next try: the key, then the
     * read; a probe refused ends the sequence before the key and the
     * read (the caller's break), so no discovery and no marker. */
    kept = CM_OSCP_K_UNKNOWN;
    probed = FALSE;
    step = CmOscpBoardNext(kept, TRUE, FALSE, probed, CM_OSCP_F_UNTESTED);
    CHECK_EQ(step, CM_OSCP_B_PROBE, "try 1: the probe");
    r = CmOscpProbe(TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, 0, &why);
    CHECK_EQ(r, CM_OSCP_P_DEFER, "try 1: deferred, nothing kept");
    step = CmOscpBoardNext(kept, TRUE, FALSE, probed, CM_OSCP_F_UNTESTED);
    CHECK_EQ(step, CM_OSCP_B_PROBE, "try 2: the probe again");
    r = CmOscpProbe(TRUE, TRUE, FALSE, TRUE, TRUE, FALSE, TRUE, 0, &why);
    CHECK_EQ(r, 0, "try 2: passed");
    probed = TRUE;
    step = CmOscpBoardNext(kept, TRUE, FALSE, probed, CM_OSCP_F_UNTESTED);
    CHECK_EQ(step, CM_OSCP_B_FLUSH, "try 2: the key next");
    step = CmOscpBoardNext(CM_OSCP_K_SEND, FALSE, FALSE, FALSE,
                           CM_OSCP_F_UNTESTED);
    CHECK_EQ(step, CM_OSCP_B_PROBE, "a known board: the probe first");
    r = CmOscpProbe(TRUE, TRUE, FALSE, TRUE, TRUE, FALSE, TRUE, 0xFFFFFFFFUL, &why);
    CHECK(r != 0 && r != CM_OSCP_P_DEFER,
          "XP: refused before the key and the marker");
}

/* The alias test and the system gate (record 32 section 12.8.15.16). */
static void test_alias(void)
{
    ULONG why;

    /* Windows 2000 on the emulator: 100h read dword 0, 420h dword 20h. */
    CHECK(CmOscpAliased(0x24211B21UL, 0x24211B21UL, 0xFE70FA80UL,
                        0xFE70FA80UL), "2000: both aliased");
    CHECK(CmOscpAliased(0x24211B21UL, 0x24211B21UL, 0xFE70FA80UL, 0),
          "100h reads the id: aliased");
    CHECK(CmOscpAliased(0x24211B21UL, 0x00010001UL, 0xFE70FA80UL,
                        0xFE70FA80UL), "420h reads dword 20h: aliased");
    CHECK(!CmOscpAliased(0x24211B21UL, 0x14010001UL, 0xFE70FA80UL, 0),
          "an extended capability at 100h, an idle mailbox: reached");
    CHECK(!CmOscpAliased(0x24211B21UL, 0x14010001UL, 0xFE70FA80UL,
                         0x00000200UL), "a mailbox done: reached");
    CHECK(!CmOscpAliased(0x24211B21UL, 0xFFFFFFFFUL, 0xFE70FA80UL,
                         0xFFFFFFFFUL), "XP: all ones, not an alias");
    CHECK(CmOscpAliased(0, 0, 0, 0), "all zero: refused (fail safe)");

    CHECK_EQ(CmOscpProbe(TRUE, TRUE, FALSE, TRUE, TRUE, TRUE, TRUE,
                         0xFE70FA80UL, &why), 0x112UL,
             "aliased: refused 112h, never written");
    CHECK_EQ(why, 0x112UL, "aliased: 112h");
    CHECK_EQ(CmOscpProbe(TRUE, TRUE, FALSE, TRUE, TRUE, TRUE, TRUE,
                         0xFFFFFFFFUL, &why), 0x109UL,
             "all ones first: 109h");
    CHECK_EQ(CmOscpProbe(TRUE, TRUE, FALSE, TRUE, TRUE, FALSE, TRUE, 0, &why),
             0, "reached, idle: passed");
    CHECK_EQ(CmOscpProbe(TRUE, TRUE, TRUE, TRUE, TRUE, TRUE, TRUE, 0, &why),
             CM_OSCP_P_DEFER, "aliased but aborted: deferred");
    CHECK_EQ(CmOscpProbe(TRUE, TRUE, FALSE, TRUE, FALSE, TRUE, TRUE, 0, &why),
             0x104UL, "not in D0 before the alias test");

    CHECK(!CmOscpExtAllowed(FALSE), "Windows 2000: no extended access");
    CHECK(CmOscpExtAllowed(TRUE), "XP and later: the probe decides");
}

/* Record 32 section 12.8.15.18: ECAM on 2000, 32-bit XP and XP x64. */
static void test_ecam(void)
{
    ULONG route[4];
    ULONG ecam[4];
    ULONG why;
    ULONG off;
    ULONG i;
    ULONG kept;
    BOOLEAN reads;
    BOOLEAN latched;
    BOOLEAN probed;
    ULONG flush;

    /* The systems. */
    CHECK(CmOscpEcamSystem(FALSE, 0, 0, FALSE), "2000 (no RtlGetVersion)");
    CHECK(CmOscpEcamSystem(TRUE, 5, 1, FALSE), "32-bit XP");
    CHECK(!CmOscpEcamSystem(TRUE, 5, 1, TRUE), "5.1 on amd64: none");
    CHECK(CmOscpEcamSystem(TRUE, 5, 2, TRUE), "XP x64 / Server 2003 x64");
    CHECK(!CmOscpEcamSystem(TRUE, 5, 2, FALSE), "Server 2003 x86: no");
    CHECK(!CmOscpEcamSystem(TRUE, 5, 0, FALSE), "5.0 with RtlGetVersion: no");
    CHECK(!CmOscpEcamSystem(TRUE, 6, 0, FALSE), "Vista x86: no");
    CHECK(!CmOscpEcamSystem(TRUE, 6, 0, TRUE), "Vista x64: no");
    CHECK(!CmOscpEcamSystem(TRUE, 6, 1, TRUE), "7: no");
    CHECK(!CmOscpEcamSystem(TRUE, 10, 0, TRUE), "10: no");
    CHECK(!CmOscpEcamSystem(TRUE, 10, 0, FALSE), "10 x86: no");

    /* The offsets. */
    CHECK(CmOscpEcamOffset(0x420UL, TRUE), "write 420h");
    CHECK(CmOscpEcamOffset(0x430UL, TRUE), "write 430h");
    CHECK(CmOscpEcamOffset(0x420UL, FALSE), "read 420h");
    CHECK(CmOscpEcamOffset(0x430UL, FALSE), "read 430h");
    for (off = 0; off < 0x1000UL; off += 4UL) {
        BOOLEAN w;
        BOOLEAN r;

        w = (BOOLEAN)(off == 0x420UL || off == 0x430UL);
        r = (BOOLEAN)(w || off == 0 || off == 0x08UL || off == 0x18UL ||
                      off == 0x20UL || off == 0x100UL);
        if (CmOscpEcamOffset(off, TRUE) != w ||
            CmOscpEcamOffset(off, FALSE) != r) {
            CHECK(0, "an offset admitted wrongly");
            break;
        }
    }
    CHECK(!CmOscpEcamOffset(0x41CUL, TRUE), "write 41Ch refused");
    CHECK(!CmOscpEcamOffset(0x424UL, TRUE), "write 424h refused");
    CHECK(!CmOscpEcamOffset(0x42CUL, TRUE), "write 42Ch refused");
    CHECK(!CmOscpEcamOffset(0x434UL, TRUE), "write 434h refused");
    CHECK(!CmOscpEcamOffset(0x20UL, TRUE), "write 20h refused");
    CHECK(!CmOscpEcamOffset(0x00UL, TRUE), "write 0 refused");
    CHECK(!CmOscpEcamOffset(0x04UL, FALSE), "read 04h refused");
    CHECK(!CmOscpEcamOffset(0x104UL, FALSE), "read 104h refused");
    CHECK(!CmOscpEcamOffset(0x422UL, FALSE), "an unaligned 422h refused");

    /* The page's check. */
    route[0] = 0x24211B21UL;
    route[1] = 0x06040001UL;
    route[2] = 0x00030201UL;
    route[3] = 0xFE70FA80UL;
    for (i = 0; i < 4UL; i++) {
        ecam[i] = route[i];
    }
    CHECK_EQ(CmOscpEcamCheck(route, ecam, 2, 0x00020001UL, 0), CM_OSCP_E_OK,
             "the same port: passed");
    CHECK_EQ(CmOscpEcamCheck(route, ecam, 2, 0x00020001UL, 0x200UL),
             CM_OSCP_E_OK, "a mailbox done: passed");
    for (i = 1; i < 4UL; i++) {
        ecam[i] ^= 0x00000100UL;
        CHECK_EQ(CmOscpEcamCheck(route, ecam, 2, 0x00020001UL, 0),
                 CM_OSCP_E_DIFFERS, "a dword differing alone");
        ecam[i] = route[i];
    }
    ecam[0] = 0x24231B21UL;
    route[0] = 0x24231B21UL;
    CHECK_EQ(CmOscpEcamCheck(route, ecam, 2, 0x00020001UL, 0), CM_OSCP_E_ID,
             "a downstream port's page, even if the route agrees");
    route[0] = 0x24211B21UL;
    CHECK_EQ(CmOscpEcamCheck(route, ecam, 2, 0x00020001UL, 0), CM_OSCP_E_ID,
             "the page one bus off: the id");
    ecam[0] = 0xFFFFFFFFUL;
    CHECK_EQ(CmOscpEcamCheck(route, ecam, 2, 0x00020001UL, 0), CM_OSCP_E_ID,
             "the page all ones");
    ecam[0] = 0x24211B21UL;
    route[1] = 0x0C030001UL;
    ecam[1] = 0x0C030001UL;
    CHECK_EQ(CmOscpEcamCheck(route, ecam, 2, 0x00020001UL, 0),
             CM_OSCP_E_CLASS, "not a bridge");
    route[1] = 0x06040001UL;
    ecam[1] = 0x06040001UL;
    CHECK_EQ(CmOscpEcamCheck(route, ecam, 3, 0x00020001UL, 0), CM_OSCP_E_BUS,
             "another secondary bus");
    CHECK_EQ(CmOscpEcamCheck(route, ecam, 2, 0x24211B21UL, 0),
             CM_OSCP_E_ALIAS, "100h reads the id");
    CHECK_EQ(CmOscpEcamCheck(route, ecam, 2, 0x00020001UL, 0xFE70FA80UL),
             CM_OSCP_E_ALIAS, "420h reads dword 20h");

    /* The probe with the page's outcome. */
    CHECK_EQ(CmOscpProbeEx(TRUE, TRUE, FALSE, TRUE, TRUE, 0, FALSE, TRUE, 0,
                           &why), 0, "ECAM, idle: passed");
    CHECK_EQ(CmOscpProbeEx(TRUE, TRUE, FALSE, TRUE, TRUE, 0x114UL, FALSE,
                           FALSE, 0xFFFFFFFFUL, &why), 0x114UL,
             "no page: 114h");
    CHECK_EQ(why, 0x114UL, "114h");
    CHECK_EQ(CmOscpProbeEx(TRUE, TRUE, FALSE, TRUE, TRUE, 0x115UL, FALSE,
                           FALSE, 0xFFFFFFFFUL, &why), 0x115UL,
             "not the port: 115h");
    CHECK_EQ(CmOscpProbeEx(TRUE, TRUE, FALSE, TRUE, TRUE, 0, FALSE, TRUE,
                           0xFFFFFFFFUL, &why), 0x109UL,
             "the page's mailbox all ones: 109h");
    CHECK_EQ(CmOscpProbeEx(TRUE, TRUE, FALSE, TRUE, FALSE, 0x115UL, FALSE,
                           FALSE, 0, &why), 0x104UL, "D0 first");
    CHECK_EQ(CmOscpProbeEx(TRUE, TRUE, FALSE, FALSE, FALSE, 0x114UL, FALSE,
                           FALSE, 0, &why), 0x10BUL, "the route first");
    CHECK_EQ(CmOscpProbeEx(TRUE, TRUE, TRUE, TRUE, TRUE, 0x115UL, FALSE,
                           FALSE, 0, &why), CM_OSCP_P_DEFER,
             "aborted: deferred");

    /* The sequence with ECAM. */
    CHECK_EQ(CmOscpBoardNextEx(CM_OSCP_K_NONE, TRUE, FALSE, TRUE, FALSE,
                               FALSE, CM_OSCP_F_UNTESTED), CM_OSCP_B_MATCH,
             "match first");
    CHECK_EQ(CmOscpBoardNextEx(CM_OSCP_K_SEND, FALSE, FALSE, TRUE, FALSE,
                               FALSE, CM_OSCP_F_UNTESTED), CM_OSCP_B_NOECAM,
             "a known board at reads 0: 113h");
    CHECK_EQ(CmOscpBoardNextEx(CM_OSCP_K_SEND, FALSE, TRUE, TRUE, FALSE,
                               FALSE, CM_OSCP_F_UNTESTED), CM_OSCP_B_NOECAM,
             "reads 0, latched: 113h");
    CHECK_EQ(CmOscpBoardNextEx(CM_OSCP_K_UNKNOWN, FALSE, FALSE, TRUE, FALSE,
                               FALSE, CM_OSCP_F_UNTESTED), CM_OSCP_B_UNKNOWN,
             "an unknown board at reads 0: the report's line");
    CHECK_EQ(CmOscpBoardNextEx(CM_OSCP_K_SEND, TRUE, TRUE, TRUE, FALSE,
                               FALSE, CM_OSCP_F_UNTESTED), CM_OSCP_B_LATCHED,
             "latched");
    CHECK_EQ(CmOscpBoardNextEx(CM_OSCP_K_SEND, TRUE, FALSE, TRUE, FALSE,
                               FALSE, CM_OSCP_F_UNTESTED), CM_OSCP_B_FLUSH,
             "the key's test before the base");
    CHECK_EQ(CmOscpBoardNextEx(CM_OSCP_K_UNKNOWN, TRUE, FALSE, TRUE, FALSE,
                               FALSE, CM_OSCP_F_FAILED), CM_OSCP_B_DEFER,
             "not flushable: deferred, nothing read");
    CHECK_EQ(CmOscpBoardNextEx(CM_OSCP_K_SEND, TRUE, FALSE, TRUE, FALSE,
                               FALSE, CM_OSCP_F_OK), CM_OSCP_B_ECAM,
             "the base");
    CHECK_EQ(CmOscpBoardNextEx(CM_OSCP_K_SEND, TRUE, FALSE, TRUE, TRUE,
                               FALSE, CM_OSCP_F_OK), CM_OSCP_B_PROBE,
             "the probe after the base");
    CHECK_EQ(CmOscpBoardNextEx(CM_OSCP_K_SEND, TRUE, FALSE, TRUE, TRUE,
                               TRUE, CM_OSCP_F_OK), CM_OSCP_B_SEND,
             "a known board: the send");
    CHECK_EQ(CmOscpBoardNextEx(CM_OSCP_K_UNKNOWN, TRUE, FALSE, TRUE, TRUE,
                               TRUE, CM_OSCP_F_OK), CM_OSCP_B_READ,
             "an unknown board: the read path");
    CHECK_EQ(CmOscpBoardNextEx(CM_OSCP_K_SEND, TRUE, FALSE, TRUE, TRUE,
                               TRUE, CM_OSCP_F_UNTESTED), CM_OSCP_B_FLUSH,
             "a later try: the key tested again, the base kept");

    /* Without ECAM every answer is CmOscpBoardNext's. */
    for (kept = 0; kept < 4UL; kept++) {
        for (i = 0; i < 16UL; i++) {
            reads = (BOOLEAN)((i & 1UL) != 0);
            latched = (BOOLEAN)((i & 2UL) != 0);
            probed = (BOOLEAN)((i & 4UL) != 0);
            for (flush = 0; flush < 3UL; flush++) {
                if (CmOscpBoardNextEx(kept, reads, latched, FALSE,
                                      (BOOLEAN)((i & 8UL) != 0), probed,
                                      flush) !=
                    CmOscpBoardNext(kept, reads, latched, probed, flush)) {
                    CHECK(0, "Ex without ECAM differs");
                }
            }
        }
    }
    CHECK(1, "Ex without ECAM: CmOscpBoardNext's answers");

    /* With ECAM, a send or a read never comes before the base and the
     * probe, nor the base before the key's test. */
    for (kept = 0; kept < 4UL; kept++) {
        for (i = 0; i < 16UL; i++) {
            ULONG st;

            reads = (BOOLEAN)((i & 1UL) != 0);
            latched = (BOOLEAN)((i & 2UL) != 0);
            probed = (BOOLEAN)((i & 4UL) != 0);
            for (flush = 0; flush < 3UL; flush++) {
                st = CmOscpBoardNextEx(kept, reads, latched, TRUE,
                                       (BOOLEAN)((i & 8UL) != 0), probed,
                                       flush);
                if ((st == CM_OSCP_B_SEND || st == CM_OSCP_B_READ) &&
                    (!reads || (i & 8UL) == 0 || !probed ||
                     flush != CM_OSCP_F_OK || latched)) {
                    CHECK(0, "ECAM: a send or read out of order");
                }
                if (st == CM_OSCP_B_ECAM &&
                    (!reads || latched || flush != CM_OSCP_F_OK)) {
                    CHECK(0, "ECAM: the base before its gates");
                }
                if (st == CM_OSCP_B_PROBE && (i & 8UL) == 0) {
                    CHECK(0, "ECAM: the probe without the base");
                }
            }
        }
    }
    CHECK(1, "ECAM: the order held over every input");
}

int main(void)
{
    test_members();
    test_smbios();
    test_smbios2();
    test_wmi();
    test_match();
    test_guard();
    test_ranges();
    test_route();
    test_fwtable();
    test_board();
    test_kept();
    test_probe();
    test_alias();
    test_ecam();
    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
