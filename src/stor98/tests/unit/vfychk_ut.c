/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, Yeo Kheng Meng
 *
 * vfychk_ut - host unit test of the VERIFY(10)/(16) checks (src/vfychk.h):
 * a range inside the namespace passes, to its last block; one block past
 * it, an LBA at or past the end and a count that wraps are out of range;
 * a count of 0 passes anywhere; BYTCHK and VRPROTECT are refused, DPO is
 * not; VERIFY(16) reads its 64-bit LBA and 32-bit count; more than 65535
 * sectors is refused; the native range, the Verify Size Limit and the
 * child sizes of a VERIFY that reads the medium (47.22). Built by
 * tests\unit\build.cmd; the exit code is the number of failed checks.
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "vfychk.h"

static int failures;
static UCHAR cdb[16];

static void check(int ok, const char *what)
{
    printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) {
        failures++;
    }
}

static void v10(UCHAR flags, ULONG lba, USHORT count)
{
    memset(cdb, 0, sizeof(cdb));
    cdb[0] = VFY_OP_10;
    cdb[1] = flags;
    cdb[2] = (UCHAR)(lba >> 24);
    cdb[3] = (UCHAR)(lba >> 16);
    cdb[4] = (UCHAR)(lba >> 8);
    cdb[5] = (UCHAR)lba;
    cdb[7] = (UCHAR)(count >> 8);
    cdb[8] = (UCHAR)count;
}

static void v16(UCHAR flags, ULONGLONG lba, ULONG count)
{
    ULONG i;

    memset(cdb, 0, sizeof(cdb));
    cdb[0] = VFY_OP_16;
    cdb[1] = flags;
    for (i = 0; i < 8; i++) {
        cdb[9 - i] = (UCHAR)(lba >> (8 * i));
    }
    for (i = 0; i < 4; i++) {
        cdb[13 - i] = (UCHAR)(count >> (8 * i));
    }
}

int main(void)
{
    ULONGLONG big = ((ULONGLONG)1 << 32) + 1;

    printf("vfychk_ut\n");

    v10(0, 0, 8);
    check(VfyCheck(cdb, 0x1000, VFY_MAX_SECTORS) == VFY_OK, "VERIFY(10) of blocks 0-7 of 4096");
    v10(0, 0x1000 - 8, 8);
    check(VfyCheck(cdb, 0x1000, VFY_MAX_SECTORS) == VFY_OK, "VERIFY(10) up to the last block");
    v10(0, 0x1000 - 8, 9);
    check(VfyCheck(cdb, 0x1000, VFY_MAX_SECTORS) == VFY_OUT_OF_RANGE, "one block past the end: out of range");
    v10(0, 0x1000 - 1, 2);
    check(VfyCheck(cdb, 0x1000, VFY_MAX_SECTORS) == VFY_OUT_OF_RANGE, "from the last block, partly in range: out of range");
    v10(0, 0x1000 - 1, 1);
    check(VfyCheck(cdb, 0x1000, VFY_MAX_SECTORS) == VFY_OK, "the last block alone: in range");
    v10(0, 0x1000, 1);
    check(VfyCheck(cdb, 0x1000, VFY_MAX_SECTORS) == VFY_PAST_END, "an LBA at the end: wholly past the end");
    v10(0, 0x1003, 32);
    check(VfyCheck(cdb, 0x1000, VFY_MAX_SECTORS) == VFY_PAST_END, "an LBA past the end: wholly past the end");
    v10(0, 0xFFFFFFFF, 0xFFFF);
    check(VfyCheck(cdb, 0x1000, VFY_MAX_SECTORS) == VFY_PAST_END, "LBA FFFFFFFFh: wholly past the end, no wrap");
    v10(0, 0x5000, 0);
    check(VfyCheck(cdb, 0x1000, VFY_MAX_SECTORS) == VFY_OK, "a count of 0 verifies nothing and passes");
    v10(0x10, 0, 1);
    check(VfyCheck(cdb, 0x1000, VFY_MAX_SECTORS) == VFY_OK, "DPO is accepted");
    v10(0x02, 0, 1);
    check(VfyCheck(cdb, 0x1000, VFY_MAX_SECTORS) == VFY_BAD_FIELD, "BYTCHK 01b (compare) refused");
    v10(0x04, 0, 1);
    check(VfyCheck(cdb, 0x1000, VFY_MAX_SECTORS) == VFY_BAD_FIELD, "BYTCHK 10b refused");
    v10(0x20, 0, 1);
    check(VfyCheck(cdb, 0x1000, VFY_MAX_SECTORS) == VFY_BAD_FIELD, "VRPROTECT refused");
    v10(0x02, 0x5000, 1);
    check(VfyCheck(cdb, 0x1000, VFY_MAX_SECTORS) == VFY_BAD_FIELD, "a bad field is refused before the range");

    v16(0, big - 1, 1);
    check(VfyCheck(cdb, big, VFY_MAX_SECTORS) == VFY_OK, "VERIFY(16) of the last block above 2^32");
    v16(0, big - 1, 2);
    check(VfyCheck(cdb, big, VFY_MAX_SECTORS) == VFY_OUT_OF_RANGE, "VERIFY(16) one past the end above 2^32");
    v16(0, (ULONGLONG)-1, 0xFFFFFFFF);
    check(VfyCheck(cdb, big, VFY_MAX_SECTORS) == VFY_PAST_END, "VERIFY(16) at LBA 2^64 - 1: wholly past the end, no wrap");
    v16(0, big, 1);
    check(VfyCheck(cdb, big, VFY_MAX_SECTORS) == VFY_PAST_END, "VERIFY(16) at the end above 2^32: wholly past the end");
    v16(0, big + 5, 0x10000);
    check(VfyCheck(cdb, big, VFY_MAX_SECTORS) == VFY_PAST_END, "wholly past the end and over the bound: past the end first");
    v16(0x02, big + 5, 1);
    check(VfyCheck(cdb, big, VFY_MAX_SECTORS) == VFY_BAD_FIELD, "BYTCHK past the end: the field is refused first");
    v16(0, 0, 0xFFFF);
    check(VfyCheck(cdb, big, VFY_MAX_SECTORS) == VFY_OK, "VERIFY(16) count from bytes 10-13, at the work bound");
    v16(0, 0, 0x10000);
    check(VfyCheck(cdb, big, VFY_MAX_SECTORS) == VFY_BAD_FIELD, "VERIFY(16) of more than 65535 sectors: the work bound, refused");
    v16(0, 0, 0x100000);
    check(VfyCheck(cdb, big, VFY_MAX_SECTORS) == VFY_BAD_FIELD, "VERIFY(16) of 1 Mi sectors: refused before any work");
    v16(0, big - 1, 0x10000);
    check(VfyCheck(cdb, big, VFY_MAX_SECTORS) == VFY_OUT_OF_RANGE, "past the end and over the bound: out of range first");
    v16(0x02, 0, 1);
    check(VfyCheck(cdb, big, VFY_MAX_SECTORS) == VFY_BAD_FIELD, "VERIFY(16) BYTCHK refused");

    /* The decode (47.22) */
    {
        ULONGLONG lba, first;
        ULONG count, blocks;

        v10(0, 0x12345678, 0xABCD);
        VfyDecode(cdb, &lba, &count);
        check(lba == 0x12345678 && count == 0xABCD, "VfyDecode: VERIFY(10) LBA and count");
        v16(0, big + 7, 0x1234);
        VfyDecode(cdb, &lba, &count);
        check(lba == big + 7 && count == 0x1234, "VfyDecode: VERIFY(16) 64-bit LBA and count");

        /* The native range: whole blocks, 512 B (shift 0) and 4 KiB (shift 3) */
        VfyNativeRange(100, 8, 0, &first, &blocks);
        check(first == 100 && blocks == 8, "512-byte blocks: the range itself");
        VfyNativeRange(0, 8, 3, &first, &blocks);
        check(first == 0 && blocks == 1, "4 KiB: an aligned 8 sectors, one block");
        VfyNativeRange(3, 1, 3, &first, &blocks);
        check(first == 0 && blocks == 1, "4 KiB: one sector inside a block, that block");
        VfyNativeRange(7, 2, 3, &first, &blocks);
        check(first == 0 && blocks == 2, "4 KiB: two sectors across a boundary, both blocks");
        VfyNativeRange(3, 65535, 3, &first, &blocks);
        check(first == 0 && blocks == 8193, "4 KiB: LBA 3 and 65535 sectors round to 8193 blocks");

        /* The bound by the view (47.31): 32 MiB, at most 65535 sectors */
        check(VfyMaxSectors(512) == 65535, "512-byte sectors: 65535 (VERIFY(10)'s maximum)");
        check(VfyMaxSectors(4096) == 8192, "4096-byte sectors: 8192 (32 MiB)");
        v16(0, 0, 8192);
        check(VfyCheck(cdb, big, VfyMaxSectors(4096)) == VFY_OK, "4096-byte sectors: 8192 accepted");
        v16(0, 0, 8193);
        check(VfyCheck(cdb, big, VfyMaxSectors(4096)) == VFY_BAD_FIELD, "4096-byte sectors: 8193 refused");
        v10(0, 0, 0);
        check(VfyCheck(cdb, 0x1000, VfyMaxSectors(4096)) == VFY_OK, "4096-byte sectors: a count of 0 passes");
        VfyNativeRange(100, 8, 0, &first, &blocks);
        check(first == 100 && blocks == 8, "native view (shift 0): LBAs are native blocks");

        /* The CNS 06h decode: VSL is byte 0, WZSL byte 1 */
        {
            UCHAR id[4];

            id[0] = 7; id[1] = 3; id[2] = 0; id[3] = 0;
            check(VfyIocsVsl(id) == 7, "CNS 06h: VSL from byte 0, not WZSL");
            id[0] = 0; id[1] = 5;
            check(VfyIocsVsl(id) == 0, "CNS 06h: VSL 0 with WZSL 5: no Verify by VSL");
            id[0] = 4; id[1] = 0;
            check(VfyIocsVsl(id) == 4, "CNS 06h: VSL 4 with WZSL 0");
        }

        /* Verify Size Limit in native blocks */
        check(VfyVslBlocks(0, 512) == 0, "VSL 0: no limit");
        check(VfyVslBlocks(7, 512) == 1024, "VSL 7 (512 KiB) in 512-byte blocks");
        check(VfyVslBlocks(7, 4096) == 128, "VSL 7 in 4 KiB blocks");
        check(VfyVslBlocks(31, 512) == 0xFFFFFFFFUL, "VSL 31: capped, no overflow");
        check(VfyVslBlocks(255, 4096) == 0xFFFFFFFFUL, "VSL 255: capped, no shift overflow");

        /* Child sizes */
        check(VfyChildBlocks(1, 65535, 524288, 512, 0, 32768) == 1024,
              "Verify: the read child size bounds it (512 KiB)");
        check(VfyChildBlocks(1, 65535, 524288, 512, 64, 32768) == 64, "Verify: a smaller VSL bounds it");
        check(VfyChildBlocks(1, 100000, 0x10000000, 512, 0, 32768) == 65536, "Verify: at most 65536 blocks");
        check(VfyChildBlocks(1, 5, 524288, 512, 64, 32768) == 5, "Verify: the remaining blocks");
        check(VfyChildBlocks(0, 65535, 524288, 512, 0, 32768) == 64, "read: 32 KiB of 512-byte blocks");
        check(VfyChildBlocks(0, 8193, 524288, 4096, 0, 32768) == 8, "read: 32 KiB of 4 KiB blocks");
        check(VfyChildBlocks(0, 3, 524288, 4096, 0, 32768) == 3, "read: the remaining blocks");
        check(VfyChildBlocks(0, 100, 16384, 512, 0, 32768) == 32, "read: a smaller read child size bounds it");
        check(VfyChildBlocks(1, 0, 524288, 512, 0, 32768) == 0, "nothing left: no child");
    }

    printf("%s: %d failed checks\n", failures ? "FAIL" : "PASS", failures);
    return failures;
}
