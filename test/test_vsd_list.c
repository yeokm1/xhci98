/*
 * test_vsd_list.c - S98VSD.VXD's pure half (src\vsd\vsd_list.c; design
 * record 30 section 9.1): the claim's tests, the function table, and the
 * physical list from page-table entries with its every refusal.
 *
 * A fake page table maps linear page L to the entry pte[L - base]; a fake
 * logical list is an array of (count, buffer) pairs at a linear address.
 */

#include <string.h>
#include "../src/vsd/vsd_list.h"
#include "test_harness.h"

#define FAKE_BASE   0x80000UL       /* first linear page of the fake table */
#define FAKE_PAGES  4096UL

static VSD_U32 pte[FAKE_PAGES];
static int pteFail;
static VSD_U32 pteCalls;
static VSD_U32 descAddr;
static VSD_U32 desc[600][2];

static VSD_U32 fake_pte(void *ctx, VSD_U32 page, VSD_U32 npages, VSD_U32 *out)
{
    VSD_U32 i;
    (void)ctx;
    pteCalls++;
    if (pteFail) {
        return 0;
    }
    for (i = 0; i < npages; i++) {
        if (page + i < FAKE_BASE || page + i - FAKE_BASE >= FAKE_PAGES) {
            out[i] = 0;
        } else {
            out[i] = pte[page + i - FAKE_BASE];
        }
    }
    return 1;
}

static void fake_desc(void *ctx, VSD_U32 addr, VSD_U32 *count, VSD_U32 *buf)
{
    VSD_U32 k = (addr - descAddr) / 8;
    (void)ctx;
    *count = desc[k][0];
    *buf = desc[k][1];
}

static VSD_IO io = { fake_pte, fake_desc, 0 };
static VSD_RUN runs[VSD_ROOM_RUNS + 4];
static VSD_U32 scratch[VSD_SCRATCH + 4];

/* Frames: contiguous from frame f0 (step 1), alternating (step 2), or
 * reversed (step -1). */
static void frames(VSD_U32 f0, int step)
{
    VSD_U32 i;
    for (i = 0; i < FAKE_PAGES; i++) {
        long f = (long)f0 + (long)i * step;
        pte[i] = ((VSD_U32)f << 12) | 0x67UL;
    }
}

static VSD_U32 lin(VSD_U32 pageOff, VSD_U32 byteOff)
{
    return (FAKE_BASE + pageOff) * VSD_PAGE + byteOff;
}

static void req_linear(VSD_REQ *r, VSD_U32 buf, VSD_U32 bytes)
{
    memset(r, 0, sizeof(*r));
    r->BufferPtr = buf;
    r->XferCount = bytes;
    r->Chars = 1;
    r->MaxXfer = VSD_MAX_XFER;
}

/* The list covers exactly the buffer, run by run, per the fake table. */
static int list_matches(VSD_U32 buf, VSD_U32 bytes, VSD_U32 n)
{
    VSD_U32 k = 0;
    VSD_U32 off = 0;
    VSD_U32 i;
    for (i = 0; i < n; i++) {
        VSD_U32 j;
        for (j = 0; j < runs[i].Len; j++, off++) {
            VSD_U32 a = buf + off;
            VSD_U32 p = a / VSD_PAGE - FAKE_BASE;
            VSD_U32 want = (pte[p] & VSD_PTE_FRAME) | (a & (VSD_PAGE - 1));
            if (runs[i].Addr + j != want) {
                return 0;
            }
        }
        k += runs[i].Len;
    }
    return k == bytes && off == bytes;
}

static void test_linear(void)
{
    VSD_REQ r;
    VSD_U32 n;
    VSD_U32 t;
    static const VSD_U32 offs[4] = { 0, 1, 512, 4095 };
    static const VSD_U32 sizes[5] = { 1, 4096, 65536, 1048576, VSD_MAX_XFER };
    int s;
    int o;
    int k;

    for (k = 0; k < 3; k++) {
        frames(k == 2 ? 0x30000UL : 0x1000UL, k == 0 ? 1 : (k == 1 ? 2 : -1));
        for (s = 0; s < 5; s++) {
            for (o = 0; o < 4; o++) {
                VSD_U32 buf = lin(3, offs[o]);
                req_linear(&r, buf, sizes[s]);
                CHECK(VsdBuild(&r, &io, runs, scratch, &n, &t) == VSD_OK &&
                      t == sizes[s] && list_matches(buf, sizes[s], n),
                      "a linear buffer becomes its runs");
                if (k == 0) {
                    CHECK(n == 1, "contiguous frames: one run");
                } else if (sizes[s] > 4096 || offs[o] + sizes[s] > 4096) {
                    CHECK(n > 1, "discontiguous frames: a run a page");
                }
            }
        }
    }
    frames(0x1000UL, 2);
    pteCalls = 0;
    req_linear(&r, lin(0, 0), VSD_MAX_XFER);
    CHECK(VsdBuild(&r, &io, runs, scratch, &n, &t) == VSD_OK && pteCalls == 1,
          "one page-table call for a linear buffer");
    CHECK(n == VSD_MAX_PAGES, "2 MiB on alternating frames: 512 runs");
    req_linear(&r, lin(0, 1), VSD_MAX_XFER);
    CHECK(VsdBuild(&r, &io, runs, scratch, &n, &t) == VSD_OK &&
          n == VSD_MAX_PAGES + 1, "2 MiB at offset 1: 513 runs");
    req_linear(&r, lin(0, 0), VSD_MAX_XFER + 1);
    CHECK(VsdBuild(&r, &io, runs, scratch, &n, &t) == VSD_R_MAX && n == 0,
          "over the maximum refused");
    req_linear(&r, lin(0, 0), 8192);
    r.MaxXfer = 4096;
    CHECK(VsdBuild(&r, &io, runs, scratch, &n, &t) == VSD_R_MAX,
          "the DCB's smaller maximum holds");
}

static void test_counts(void)
{
    VSD_U32 b;
    VSD_REQ r;
    VSD_U32 n;
    VSD_U32 t;

    CHECK(VsdBytes(8, 0, 9, &b) && b == 4096, "8 sectors of 512");
    CHECK(VsdBytes(1, 0, 12, &b) && b == 4096, "1 sector of 4096");
    CHECK(!VsdBytes(0x00800000UL, 0, 9, &b), "a shift that overflows refused");
    CHECK(VsdBytes(0x007FFFFFUL, 0, 9, &b) && b == 0xFFFFFE00UL,
          "the largest count that does not overflow");
    CHECK(VsdBytes(77, 1, 9, &b) && b == 77, "a byte count");
    frames(0x1000UL, 1);
    req_linear(&r, lin(0, 0), 16);
    r.Chars = 0;
    r.Shift = 9;
    CHECK(VsdBuild(&r, &io, runs, scratch, &n, &t) == VSD_OK && t == 8192,
          "sector counts at shift 9");
    r.XferCount = 0x01000000UL;
    CHECK(VsdBuild(&r, &io, runs, scratch, &n, &t) == VSD_R_COUNT,
          "an overflowing sector count refused");
}

static void test_pte(void)
{
    VSD_REQ r;
    VSD_U32 n;
    VSD_U32 t;
    VSD_U32 where[3];
    int i;

    frames(0x1000UL, 1);
    req_linear(&r, lin(0, 0), 16384);
    pteFail = 1;
    CHECK(VsdBuild(&r, &io, runs, scratch, &n, &t) == VSD_R_PTE && n == 0,
          "the page-table service failing: refused, no entry read");
    pteFail = 0;
    where[0] = 0;
    where[1] = 1;
    where[2] = 3;
    for (i = 0; i < 3; i++) {
        frames(0x1000UL, 1);
        pte[where[i]] &= ~VSD_PTE_PRESENT;
        CHECK(VsdBuild(&r, &io, runs, scratch, &n, &t) == VSD_R_PRESENT,
              "a page not present refused (first, middle, last)");
    }
    frames(0x1000UL, 1);
    req_linear(&r, 0xFFFFF000UL, 8192);
    CHECK(VsdBuild(&r, &io, runs, scratch, &n, &t) == VSD_R_WRAP,
          "a linear buffer that wraps refused");
}

static void sg_setup(VSD_U32 at, VSD_U32 nfr)
{
    descAddr = at;
    desc[nfr][0] = 0;
    desc[nfr][1] = 0;
}

static void req_sg(VSD_REQ *r, VSD_U32 count)
{
    memset(r, 0, sizeof(*r));
    r->BufferPtr = descAddr;
    r->XferCount = count;
    r->Sg = 1;
    r->Chars = 1;
    r->MaxXfer = VSD_MAX_XFER;
}

static void test_sg(void)
{
    VSD_REQ r;
    VSD_U32 n;
    VSD_U32 t;
    VSD_U32 i;
    VSD_U32 sum;

    /* Two fragments that merge across their boundary. */
    frames(0x1000UL, 1);
    sg_setup(0x10000UL, 2);
    desc[0][0] = 4096; desc[0][1] = lin(0, 0);
    desc[1][0] = 4096; desc[1][1] = lin(1, 0);
    req_sg(&r, 8192);
    CHECK(VsdBuild(&r, &io, runs, scratch, &n, &t) == VSD_OK && n == 1 &&
          t == 8192, "fragments physically next: one run");
    desc[1][1] = lin(5, 0);
    CHECK(VsdBuild(&r, &io, runs, scratch, &n, &t) == VSD_OK && n == 2,
          "fragments apart: two runs");

    /* The round-1 counterexample: sixteen 512-byte fragments and one of
     * 2 MiB - 8 KiB, each at page offset 4092, discontiguous frames. */
    frames(0x1000UL, 2);
    sg_setup(0x10000UL, 17);
    sum = 0;
    for (i = 0; i < 16; i++) {
        desc[i][0] = 512;
        desc[i][1] = lin(i * 4, 4092);
        sum += 512;
    }
    desc[16][0] = VSD_MAX_XFER - 8192;
    desc[16][1] = lin(100, 4092);
    sum += VSD_MAX_XFER - 8192;
    req_sg(&r, sum);
    CHECK(VsdBuild(&r, &io, runs, scratch, &n, &t) == VSD_OK && n == 543 &&
          t == sum, "543 pages within the room");

    /* 255 fragments then the terminator; a 256th non-zero descriptor. */
    sg_setup(0x10000UL, 255);
    for (i = 0; i < 255; i++) {
        desc[i][0] = 512;
        desc[i][1] = lin(i * 2, 0);
    }
    req_sg(&r, 255UL * 512);
    CHECK(VsdBuild(&r, &io, runs, scratch, &n, &t) == VSD_OK && n == 255,
          "255 fragments and a terminator");
    desc[255][0] = 512;
    desc[255][1] = lin(600, 0);
    desc[256][0] = 0;
    CHECK(VsdBuild(&r, &io, runs, scratch, &n, &t) == VSD_R_FRAGS,
          "a 256th non-zero descriptor refused");

    /* Coverage of the IOR's count. */
    frames(0x1000UL, 1);
    sg_setup(0x10000UL, 1);
    desc[0][0] = 512;
    desc[0][1] = lin(0, 0);
    req_sg(&r, 4096);
    CHECK(VsdBuild(&r, &io, runs, scratch, &n, &t) == VSD_R_COVER,
          "a list covering fewer bytes than the count refused");
    req_sg(&r, 512);
    CHECK(VsdBuild(&r, &io, runs, scratch, &n, &t) == VSD_OK,
          "a list covering as many bytes");
    req_sg(&r, 256);
    CHECK(VsdBuild(&r, &io, runs, scratch, &n, &t) == VSD_OK && t == 512,
          "a list covering more bytes (walked to the terminator)");
    sg_setup(0x10000UL, 0);
    req_sg(&r, 512);
    CHECK(VsdBuild(&r, &io, runs, scratch, &n, &t) == VSD_R_EMPTY,
          "an empty list under a non-zero count refused");

    /* Sums and addresses. */
    sg_setup(0x10000UL, 2);
    desc[0][0] = VSD_MAX_XFER;
    desc[0][1] = lin(0, 0);
    desc[1][0] = 512;
    desc[1][1] = lin(600, 0);
    req_sg(&r, 512);
    CHECK(VsdBuild(&r, &io, runs, scratch, &n, &t) == VSD_R_MAX,
          "a sum over the maximum refused");
    sg_setup(0xFFFFFFFCUL, 1);
    req_sg(&r, 512);
    CHECK(VsdBuild(&r, &io, runs, scratch, &n, &t) == VSD_R_WRAP,
          "a descriptor pointer that wraps refused");
    sg_setup(0xFFFFFFF0UL, 1);
    desc[0][0] = 512;
    desc[0][1] = lin(0, 0);
    req_sg(&r, 512);
    CHECK(VsdBuild(&r, &io, runs, scratch, &n, &t) == VSD_R_WRAP,
          "a terminator that would end at 2^32 refused");
    sg_setup(0x10000UL, 1);
    desc[0][0] = 8192;
    desc[0][1] = 0xFFFFF000UL;
    req_sg(&r, 8192);
    CHECK(VsdBuild(&r, &io, runs, scratch, &n, &t) == VSD_R_WRAP,
          "a fragment that wraps refused");
}

static void test_srb(void)
{
    VSD_REQ r;
    VSD_U32 n;
    VSD_U32 t;

    frames(0x1000UL, 1);
    req_linear(&r, lin(0, 0), 4096);
    r.SrbValid = 1;
    r.SrbBuffer = lin(0, 0);
    r.SrbLength = 4097;
    CHECK(VsdBuild(&r, &io, runs, scratch, &n, &t) == VSD_R_SRB_COVER,
          "an SRB longer than the list refused");
    r.SrbLength = 4096;
    CHECK(VsdBuild(&r, &io, runs, scratch, &n, &t) == VSD_OK,
          "an SRB as long as the list");
    r.SrbLength = 2048;
    CHECK(VsdBuild(&r, &io, runs, scratch, &n, &t) == VSD_OK,
          "an SRB shorter than the list");
    r.SrbBuffer = lin(0, 4);
    CHECK(VsdBuild(&r, &io, runs, scratch, &n, &t) == VSD_OK,
          "an SRB's DataBuffer (set by SCSIPORT below) is not read");
    r.SrbBuffer = 0;
    r.SrbLength = 4096;
    CHECK(VsdBuild(&r, &io, runs, scratch, &n, &t) == VSD_OK,
          "an SRB whose DataBuffer SCSIPORT has yet to set (0)");
    r.SrbLength = 4097;
    CHECK(VsdBuild(&r, &io, runs, scratch, &n, &t) == VSD_R_SRB_COVER,
          "... still held to the list's length");

    /* Fragments each whole while DataBuffer + DataTransferLength wraps. */
    sg_setup(0x10000UL, 2);
    desc[0][0] = 4096;
    desc[0][1] = 0xFFFFF000UL;
    desc[1][0] = 4096;
    desc[1][1] = lin(0, 0);
    req_sg(&r, 4096);
    CHECK(VsdBuild(&r, &io, runs, scratch, &n, &t) == VSD_R_WRAP,
          "a first fragment ending exactly at 2^32 refused");
    CHECK(VsdWraps(0xFFFFFFF8UL, 8) && !VsdWraps(0xFFFFFFF0UL, 8) &&
          !VsdWraps(0, 0) && VsdWraps(1, 0xFFFFFFFFUL),
          "the exclusive end at 2^32 is a wrap");
    req_sg(&r, 8192);
    CHECK(VsdBuild(&r, &io, runs, scratch, &n, &t) == VSD_R_WRAP,
          "the first byte plus the count wrapping refused (no SRB)");
    desc[0][1] = 0xFFFFE000UL;
    req_sg(&r, 4096);
    r.SrbValid = 1;
    r.SrbBuffer = 0xFFFFE000UL;
    r.SrbLength = 8192;
    CHECK(VsdBuild(&r, &io, runs, scratch, &n, &t) == VSD_R_WRAP,
          "the SRB's range ending at 2^32 refused");
}

static void test_claim(void)
{
    static const unsigned char s1[8] = { 's', 't', 'o', 'r', '9', '8', 0, 0 };
    static const unsigned char s2[8] = { 'S', 'T', 'O', 'R', '9', '8', 0, 0 };
    static const unsigned char s3[8] = { 's', 't', 'o', 'r', '9', '8', 'x', 0 };
    static const unsigned char s4[8] = { 'n', 'v', 'm', 'e', '2', 'k', 0, 0 };
    static const unsigned char s5[8] = { 's', 't', 'o', 'r', '9', '8', 'a', 'b' };

    CHECK(VsdClaimTest(s1, 0, VSD_DMD_PHYS_SGD, 0x80000UL) == VSD_CLAIM,
          "stor98 claimed");
    CHECK(VsdClaimTest(s2, 0, VSD_DMD_PHYS_SGD, VSD_MAX_XFER) == VSD_CLAIM,
          "STOR98 claimed, at 2 MiB");
    CHECK(VsdClaimTest(s3, 0, VSD_DMD_PHYS_SGD, 0x80000UL) == VSD_C_PORT,
          "stor98x not claimed");
    CHECK(VsdClaimTest(s4, 0, VSD_DMD_PHYS_SGD, 0x80000UL) == VSD_C_PORT,
          "nvme2k not claimed");
    CHECK(VsdClaimTest(s5, 0, VSD_DMD_PHYS_SGD, 0x80000UL) == VSD_C_PORT,
          "a full eight-character name not claimed");
    CHECK(VsdClaimTest(s1, VSD_DCB_DEV_LOGICAL, VSD_DMD_PHYS_SGD, 0x80000UL) ==
          VSD_C_LOGICAL, "a logical DCB not claimed");
    CHECK(VsdClaimTest(s1, VSD_DCB_DEV_RMM, VSD_DMD_PHYS_SGD, 0x80000UL) ==
          VSD_C_LOGICAL, "an RMM DCB not claimed");
    CHECK(VsdClaimTest(s1, 0, 0x609UL, 0x80000UL) == VSD_C_NOPHYS,
          "no phys_sgd: nothing to lift");
    CHECK(VsdClaimTest(s1, 0, VSD_DMD_PHYS_SGD, VSD_MAX_XFER + 4096) ==
          VSD_C_MAX, "a maximum over 2 MiB left alone");
}

static void test_func(void)
{
    static const unsigned nodata[15] = { 0x03, 0x05, 0x06, 0x07, 0x08, 0x09,
        0x0A, 0x10, 0x11, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28 };
    static const unsigned data[10] = { 0x00, 0x01, 0x02, 0x04, 0x0D, 0x0E,
        0x0F, 0x12, 0x2B, 0x7F };
    int i;
    for (i = 0; i < 15; i++) {
        CHECK(VsdNoDataFunc(nodata[i]), "a no-data function");
    }
    for (i = 0; i < 10; i++) {
        CHECK(!VsdNoDataFunc(data[i]), "a function with data, or undefined");
    }
}

int main(void)
{
    test_linear();
    test_counts();
    test_pte();
    test_sg();
    test_srb();
    test_claim();
    test_func();
    printf("test_vsd_list: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
