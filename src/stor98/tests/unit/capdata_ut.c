/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, Yeo Kheng Meng
 *
 * capdata_ut - host unit test of the READ CAPACITY(16) answer and the
 * length rule of the software answers (src/capdata.h): the last LBA and
 * block length big-endian, the rest zero; a namespace above 2^32 blocks;
 * the allocation length from CDB bytes 10-13; the answer cut at the
 * allocation length and at the data buffer, so a 12-byte request gets the
 * first 12 bytes and an allocation of 0 gets none. Built by
 * tests\unit\build.cmd; the exit code is the number of failed checks.
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "capdata.h"

static int failures;

static void check(int ok, const char *what)
{
    printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) {
        failures++;
    }
}

static int zero(const UCHAR *p, ULONG from, ULONG to)
{
    ULONG i;

    for (i = from; i < to; i++) {
        if (p[i] != 0) {
            return 0;
        }
    }
    return 1;
}

int main(void)
{
    static const UCHAR big[12] = { 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00 };
    static const UCHAR nsmall[12] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F, 0xFF, 0xFF, 0x00, 0x00, 0x10, 0x00 };
    UCHAR out[CAP16_LEN], cdb[16];

    printf("capdata_ut\n");

    memset(out, 0xCC, sizeof(out));
    check(Cap16Build(((ULONGLONG)1 << 32) + 1, 512, 0, out) == CAP16_LEN, "the full answer is 32 bytes");
    check(memcmp(out, big, 12) == 0, "2^32 + 1 blocks of 512: last LBA 1 00000000h, block length 512, big-endian");
    check(zero(out, 12, CAP16_LEN), "bytes 12-31 zero (no protection, one logical block per physical block)");

    memset(out, 0xCC, sizeof(out));
    Cap16Build(0x200000, 4096, 0, out);
    check(memcmp(out, nsmall, 12) == 0 && zero(out, 12, CAP16_LEN), "2^21 blocks of 4096: last LBA 1FFFFFh");

    memset(out, 0xCC, sizeof(out));
    Cap16Build(1, 512, 0, out);
    check(zero(out, 0, 8) && out[10] == 0x02 && out[11] == 0x00, "one block: last LBA 0");

    memset(out, 0xCC, sizeof(out));
    Cap16Build(((ULONGLONG)1 << 32) + 1, 512, 3, out);
    check(memcmp(out, big, 12) == 0 && out[12] == 0 && out[13] == 0x03 && out[14] == 0 && out[15] == 0 &&
          zero(out, 16, CAP16_LEN),
          "512-byte sectors on 4 KiB blocks: LBPPBE 3 in byte 13, lowest aligned LBA 0 (PLAN 4 KiB reporting)");
    Cap16Build(8, 512, 0x13, out);
    check(out[13] == 0x03, "LBPPBE is byte 13 bits 3:0 only");

    memset(cdb, 0, sizeof(cdb));
    cdb[0] = 0x9E;
    cdb[1] = 0x10;
    cdb[13] = 12;
    check(Cap16Alloc(cdb) == 12, "allocation length 12 from byte 13");
    cdb[10] = 0x12;
    cdb[11] = 0x34;
    cdb[12] = 0x56;
    cdb[13] = 0x78;
    check(Cap16Alloc(cdb) == 0x12345678, "allocation length big-endian in bytes 10-13");

    check(ScsiAnswerLen(CAP16_LEN, 32, 32) == 32, "allocation 32, buffer 32: 32 bytes");
    check(ScsiAnswerLen(CAP16_LEN, 12, 12) == 12, "allocation 12, buffer 12: the first 12 bytes, not an error");
    check(ScsiAnswerLen(CAP16_LEN, 12, 512) == 12, "allocation 12, buffer 512: 12 bytes");
    check(ScsiAnswerLen(CAP16_LEN, 32, 8) == 8, "allocation 32, buffer 8: cut at the buffer");
    check(ScsiAnswerLen(CAP16_LEN, 0xFFFFFFFF, 0xFFFFFFFF) == 32, "a large allocation and buffer: 32 bytes");
    check(ScsiAnswerLen(CAP16_LEN, 0, 32) == 0, "allocation 0: no data, not an error");
    check(ScsiAnswerLen(4, 2, 255) == 2, "READ DEFECT DATA header cut at an allocation of 2");

    printf("%s: %d failed checks\n", failures ? "FAIL" : "PASS", failures);
    return failures;
}
