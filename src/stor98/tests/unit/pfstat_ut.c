/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, Yeo Kheng Meng
 *
 * pfstat_ut - host unit test of src/pfstat.c, the counter answer's pure
 * half (docs/PLAN.md MD-8, include/stor98pf.h): the buckets at every edge,
 * the 64-bit totals' carry, the READ BUFFER CDB's checks and cut, and the
 * identity page. Built by tests\unit\build.cmd; the exit code is the number
 * of failed checks.
 */

#include <stdio.h>
#include <string.h>
#include "nvme2k.h"
#include "pfstat.c"

static int failures;

static void expect(int ok, const char *what)
{
    printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) {
        failures++;
    }
}

static void cdb(UCHAR *c, UCHAR mode, UCHAR id, ULONG offset, ULONG alloc)
{
    memset(c, 0, 16);
    c[0] = 0x3C;
    c[1] = mode;
    c[2] = id;
    c[3] = (UCHAR)(offset >> 16);
    c[4] = (UCHAR)(offset >> 8);
    c[5] = (UCHAR)offset;
    c[6] = (UCHAR)(alloc >> 16);
    c[7] = (UCHAR)(alloc >> 8);
    c[8] = (UCHAR)alloc;
}

int main(void)
{
    STOR98PF_TOTAL t;
    STOR98PF_ANSWER a;
    UCHAR c[16], desc[4], page[STOR98PF_VPD_LEN];
    const UCHAR *src;
    ULONG n;

    printf("pfstat_ut\n");

    /* sizes */
    expect(PfSizeBucket(0) == 0 && PfSizeBucket(512) == 0 && PfSizeBucket(4095) == 0, "below 4 KiB -> 0");
    expect(PfSizeBucket(4096) == 1, "4 KiB -> 1");
    expect(PfSizeBucket(4097) == 2 && PfSizeBucket(8192) == 2, "to 8 KiB -> 2");
    expect(PfSizeBucket(8193) == 3 && PfSizeBucket(16384) == 3, "to 16 KiB -> 3");
    expect(PfSizeBucket(16385) == 4 && PfSizeBucket(32768) == 4, "to 32 KiB -> 4");
    expect(PfSizeBucket(32769) == 5 && PfSizeBucket(64511) == 5, "below 63 KiB -> 5");
    expect(PfSizeBucket(64512) == 6, "exactly 63 KiB (IOS's piece) -> 6");
    expect(PfSizeBucket(64513) == 7 && PfSizeBucket(65536) == 7, "to 64 KiB -> 7");
    expect(PfSizeBucket(65537) == 8 && PfSizeBucket(131072) == 8, "to 128 KiB -> 8");
    expect(PfSizeBucket(262144) == 9 && PfSizeBucket(524288) == 10, "256 KiB -> 9, 512 KiB -> 10");
    expect(PfSizeBucket(1048576) == 11 && PfSizeBucket(1048577) == 12, "1 MiB -> 11, above -> 12");
    expect(PfSizeBucket(0xFFFFFFFF) == 12, "the largest -> 12");

    /* run classes and buckets */
    expect(PfRunClass(65536) == 0 && PfRunClass(131072) == 1 && PfRunClass(1048576) == 4, "run classes 64 KiB .. 1 MiB");
    expect(PfRunClass(64512) == STOR98PF_RUN_CLASSES && PfRunClass(4096) == STOR98PF_RUN_CLASSES &&
           PfRunClass(2 * 1048576) == STOR98PF_RUN_CLASSES, "no run class below 64 KiB or above 1 MiB");
    expect(PfRunBucket(1, FALSE) == 0 && PfRunBucket(2, FALSE) == 1 && PfRunBucket(3, FALSE) == 2 &&
           PfRunBucket(4, FALSE) == 2, "runs 1, 2, 3-4");
    expect(PfRunBucket(5, FALSE) == 3 && PfRunBucket(8, FALSE) == 3 && PfRunBucket(16, FALSE) == 4, "runs 5-8, 9-16");
    expect(PfRunBucket(17, FALSE) == 5 && PfRunBucket(18, FALSE) == 6 && PfRunBucket(32, FALSE) == 6, "runs 17, 18-32");
    expect(PfRunBucket(64, FALSE) == 7 && PfRunBucket(65, FALSE) == 8, "runs 33-64, above");
    expect(PfRunBucket(3, TRUE) == STOR98PF_RUN_NOT_SEEN && PfRunBucket(0, FALSE) == STOR98PF_RUN_NOT_SEEN,
           "not seen, and no run seen");

    /* depth */
    expect(PfDepthBucket(1) == 0 && PfDepthBucket(2) == 1 && PfDepthBucket(4) == 2 && PfDepthBucket(5) == 3 &&
           PfDepthBucket(16) == 4 && PfDepthBucket(32) == 5 && PfDepthBucket(33) == 6, "depth buckets");

    /* totals */
    t.Lo = 0xFFFFFFFE;
    t.Hi = 7;
    PfAdd(&t, 1);
    expect(t.Lo == 0xFFFFFFFF && t.Hi == 7, "add without carry");
    PfAdd(&t, 1);
    expect(t.Lo == 0 && t.Hi == 8, "add carries into the high word");
    PfAdd(&t, 0xFFFFFFFF);
    expect(t.Lo == 0xFFFFFFFF && t.Hi == 8, "add a large value");
    PfAdd(&t, 0x10);
    expect(t.Lo == 0x0F && t.Hi == 9, "add a large value with carry");

    /* READ BUFFER */
    memset(&a, 0x5A, sizeof(a));
    a.Signature = STOR98PF_SIGNATURE;
    cdb(c, 0x02, 0x53, 0, 0xFFFFFF);
    expect(PfReadBuffer(c, 10, &a, desc, &src, &n) == PF_RB_OK && src == (const UCHAR *)&a && n == sizeof(a),
           "mode 02h: the whole answer");
    cdb(c, 0x02, 0x53, 0, 8);
    expect(PfReadBuffer(c, 10, &a, desc, &src, &n) == PF_RB_OK && n == 8, "mode 02h cut at allocation 8");
    cdb(c, 0x02, 0x53, 0, 0);
    expect(PfReadBuffer(c, 10, &a, desc, &src, &n) == PF_RB_OK && n == 0, "mode 02h, allocation 0: nothing");
    cdb(c, 0x03, 0x53, 0, 4);
    expect(PfReadBuffer(c, 10, &a, desc, &src, &n) == PF_RB_OK && n == 4 && src == desc && desc[0] == 0 &&
           (((ULONG)desc[1] << 16) | ((ULONG)desc[2] << 8) | desc[3]) == sizeof(STOR98PF_ANSWER),
           "mode 03h: the descriptor carries the answer's size");
    cdb(c, 0x03, 0x53, 0, 2);
    expect(PfReadBuffer(c, 10, &a, desc, &src, &n) == PF_RB_OK && n == 2, "mode 03h cut at allocation 2");
    cdb(c, 0x02, 0x54, 0, 255);
    expect(PfReadBuffer(c, 10, &a, desc, &src, &n) == PF_RB_REFUSE && n == 0 && src == NULL, "another buffer ID refused");
    cdb(c, 0x02, 0x53, 1, 255);
    expect(PfReadBuffer(c, 10, &a, desc, &src, &n) == PF_RB_REFUSE, "a nonzero offset refused");
    cdb(c, 0x00, 0x53, 0, 255);
    expect(PfReadBuffer(c, 10, &a, desc, &src, &n) == PF_RB_REFUSE, "mode 00h refused");
    cdb(c, 0x0A, 0x53, 0, 255);
    expect(PfReadBuffer(c, 10, &a, desc, &src, &n) == PF_RB_REFUSE, "mode 0Ah refused");
    cdb(c, 0x22, 0x53, 0, 255);
    expect(PfReadBuffer(c, 10, &a, desc, &src, &n) == PF_RB_OK, "mode field is bits 4:0 (bits 7:5 ignored)");
    cdb(c, 0x02, 0x53, 0, 255);
    expect(PfReadBuffer(c, 6, &a, desc, &src, &n) == PF_RB_REFUSE, "a CDB shorter than 10 bytes refused");
    c[0] = 0x3B;
    expect(PfReadBuffer(c, 10, &a, desc, &src, &n) == PF_RB_REFUSE, "another opcode refused");

    /* identity page */
    memset(page, 0xCC, sizeof(page));
    PfIdentityPage(PfPciLocation(5, 0x41), page);
    expect(page[0] == 0 && page[1] == 0xC9 && page[2] == 0 && page[3] == 12, "identity page header");
    expect(memcmp(page + 4, "S98P", 4) == 0, "identity page signature 'S98P'");
    expect(page[8] == 1 && page[9] == 0 && page[10] == 0 && page[11] == 0, "identity page version 1");
    expect(page[12] == 0x0A && page[13] == 0x00 && page[14] == 0x05 && page[15] == 0x00,
           "identity page location: bus 5, device 1, function 2");
    expect(PfPciLocation(0x1FF, 0xFF) == ((0xFFUL << 16) | (0x1F << 3) | 7), "location fields masked");

    /* layout */
    expect(sizeof(STOR98PF_ANSWER) == 0x90 + 8 * 121, "answer size");

    printf("pfstat_ut: %d failure(s)\n", failures);
    return failures;
}
