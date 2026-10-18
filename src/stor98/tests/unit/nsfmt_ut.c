/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, Yeo Kheng Meng
 *
 * nsfmt_ut - host unit test of the namespace format decode (src/nsfmt.h,
 * PLAN 1.1): the format FLBAS selects (bits 3:0, and bits 6:5 when more
 * than 16 formats exist), block sizes 512 to 4096 served, 4 KiB never
 * refused, larger and smaller sizes refused, NSZE 0, a format index past
 * NLBAF, metadata (interleaved or separate) and protection refused instead
 * of being served as plain blocks. Built by tests\unit\build.cmd; the exit
 * code is the number of failed checks.
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "nsfmt.h"

static int failures;
static UCHAR id[4096];

static void check(int ok, const char *what)
{
    printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) {
        failures++;
    }
}

/* A namespace of nsze blocks with nlbaf (0's based) formats, all 512 bytes
 * without metadata, FLBAS flbas. */
static void ns(ULONGLONG nsze, UCHAR nlbaf, UCHAR flbas)
{
    ULONG i;

    memset(id, 0, sizeof(id));
    for (i = 0; i < 8; i++) {
        id[i] = (UCHAR)(nsze >> (8 * i));
    }
    id[25] = nlbaf;
    id[26] = flbas;
    for (i = 0; i < 64; i++) {
        id[128 + 4 * i + 2] = 9;
    }
}

static void lbaf(ULONG index, USHORT ms, UCHAR lbads)
{
    id[128 + 4 * index] = (UCHAR)ms;
    id[128 + 4 * index + 1] = (UCHAR)(ms >> 8);
    id[128 + 4 * index + 2] = lbads;
}

static ULONG decode(ULONGLONG *nsze, ULONG *bs, UCHAR *lbads)
{
    return NsDecodeFormat(id, nsze, bs, lbads);
}

int main(void)
{
    ULONGLONG nsze;
    ULONG bs, r;
    UCHAR lbads;

    printf("nsfmt_ut\n");

    ns(0x200000, 0, 0);
    r = decode(&nsze, &bs, &lbads);
    check(r == NSF_OK && nsze == 0x200000 && bs == 512 && lbads == 9, "one format, 512 bytes");

    ns(0x40000, 1, 1);
    lbaf(1, 0, 12);
    r = decode(&nsze, &bs, &lbads);
    check(r == NSF_OK && nsze == 0x40000 && bs == 4096, "format 1 of 2: 4096 bytes served, never refused");

    ns(0x40000, 1, 0);
    lbaf(1, 0, 12);
    r = decode(&nsze, &bs, &lbads);
    check(r == NSF_OK && bs == 512, "FLBAS selects format 0, not the largest");

    ns(((ULONGLONG)1 << 32) | 0x23456789, 0, 0);
    r = decode(&nsze, &bs, &lbads);
    check(r == NSF_OK && nsze == (((ULONGLONG)1 << 32) | 0x23456789), "NSZE above 2^32 read whole (little-endian)");

    ns(0x40000, 3, 2);
    lbaf(2, 0, 10);
    r = decode(&nsze, &bs, &lbads);
    check(r == NSF_OK && bs == 1024, "1024 bytes served");
    lbaf(2, 0, 11);
    r = decode(&nsze, &bs, &lbads);
    check(r == NSF_OK && bs == 2048, "2048 bytes served");

    /* Refusals: never served under another size. */
    ns(0, 0, 0);
    r = decode(&nsze, &bs, &lbads);
    check(r == NSF_ZERO_SIZE && nsze == 0 && bs == 0, "NSZE 0 refused (no phantom disk)");

    ns(0x1000, 1, 2);
    r = decode(&nsze, &bs, &lbads);
    check(r == NSF_BAD_INDEX && bs == 0 && lbads == 0, "FLBAS index past NLBAF refused");

    ns(0x1000, 0, 0);
    lbaf(0, 0, 13);
    r = decode(&nsze, &bs, &lbads);
    check(r == NSF_BAD_LBADS && bs == 0 && lbads == 13, "8192 bytes refused (not 512 by default)");
    lbaf(0, 0, 16);
    check(decode(&nsze, &bs, &lbads) == NSF_BAD_LBADS, "65536 bytes refused");
    lbaf(0, 0, 8);
    check(decode(&nsze, &bs, &lbads) == NSF_BAD_LBADS, "256 bytes refused");
    lbaf(0, 0, 0);
    check(decode(&nsze, &bs, &lbads) == NSF_BAD_LBADS, "LBADS 0 (unused format) refused");
    lbaf(0, 0, 40);
    check(decode(&nsze, &bs, &lbads) == NSF_BAD_LBADS, "LBADS 40 refused");

    ns(0x1000, 1, 0x11);                    /* format 1, extended LBA */
    lbaf(1, 8, 12);
    r = decode(&nsze, &bs, &lbads);
    check(r == NSF_METADATA && bs == 0, "4096 + 8 bytes interleaved metadata refused, not plain 4096");
    ns(0x1000, 1, 0x01);                    /* format 1, separate metadata */
    lbaf(1, 8, 9);
    check(decode(&nsze, &bs, &lbads) == NSF_METADATA, "separate metadata refused (no metadata pointer)");
    ns(0x1000, 1, 0x10);                    /* extended bit, format 0 without metadata */
    check(decode(&nsze, &bs, &lbads) == NSF_OK, "FLBAS bit 4 with MS 0 served");

    ns(0x1000, 0, 0);
    id[29] = 0x01;                          /* DPS: protection type 1 */
    r = decode(&nsze, &bs, &lbads);
    check(r == NSF_PROTECTION && bs == 0, "protection information refused");
    id[29] = 0x08;                          /* DPS bit 3 only: no protection type */
    check(decode(&nsze, &bs, &lbads) == NSF_OK, "DPS bit 3 alone served");

    /* More than 16 formats: FLBAS bits 6:5 are the index's high bits. */
    ns(0x1000, 19, 0x22);                   /* index 0x12 = 18 */
    lbaf(18, 0, 12);
    lbaf(2, 0, 9);
    r = decode(&nsze, &bs, &lbads);
    check(r == NSF_OK && bs == 4096, "index 18 from FLBAS bits 6:5 (NLBAF 19)");
    ns(0x1000, 19, 0x24);                   /* index 0x14 = 20 > NLBAF */
    check(decode(&nsze, &bs, &lbads) == NSF_BAD_INDEX, "index 20 past NLBAF 19 refused");
    ns(0x1000, 15, 0x22);                   /* 16 formats: bits 6:5 reserved, index 2 */
    lbaf(2, 0, 12);
    r = decode(&nsze, &bs, &lbads);
    check(r == NSF_OK && bs == 4096, "16 formats: bits 6:5 ignored, index 2");
    ns(0x1000, 63, 0x6F);                   /* index 63, the last */
    lbaf(63, 0, 12);
    r = decode(&nsze, &bs, &lbads);
    check(r == NSF_OK && bs == 4096, "index 63 (64 formats)");

    printf("%s: %d failure(s)\n", failures ? "FAIL" : "passed", failures);
    return failures;
}
