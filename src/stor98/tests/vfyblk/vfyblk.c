/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026, Yeo Kheng Meng
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * vfyblk - VERIFY and the last blocks of a disk through IOCTL_SCSI_PASS_THROUGH
 * (NT; docs/PLAN.md "VERIFY" and "4 KiB-sector namespaces", "Two views").
 *
 *   vfyblk <physical drive number> edge [bs=512|bs=4096]
 *   vfyblk <physical drive number> v6
 *   vfyblk <physical drive number> v10 <lba> <count> [to=<s>]
 *   vfyblk <physical drive number> v16 <lba> <count> [to=<s>]
 *   vfyblk <physical drive number> r10 <lba> <count> [bs=512|bs=4096]
 *
 * edge: READ CAPACITY(10) and (16) (block length compared with bs= when
 * given; the checks below stop unless both succeed with the same last LBA,
 * below 0xFFFFFFFF, and the same block length, 512 or 4096); READ(10) and
 * READ(16) of the last block GOOD with one block of data, of the block past
 * it 05/21/00; VERIFY(10) and (16) of the last block
 * GOOD; VERIFY(10) of two blocks from the last one and VERIFY(16) of the
 * block past it 05/21/00; VERIFY(10) with a count of 0 GOOD; VERIFY(6)
 * 05/20/00; VERIFY(10) with BYTCHK 05/24/00; VERIFY(16) of one block more
 * than the most one VERIFY takes (65535 sectors of 512 bytes, 8192 of 4096;
 * PLAN "VERIFY") 05/24/00, where the disk has that many; a raw one-block
 * WRITE(10) of the last block: a pattern written, read back and compared,
 * then the block's old contents written back and compared; WRITE(10) of the
 * block past the last 05/21/00.
 *
 * v6, v10, v16, r10: one command; prints the SCSI status, the sense key /
 * ASC / ASCQ and the elapsed time (r10: at most 64 KiB, bs= the block
 * length, default 512). to= is the
 * pass-through time-out in seconds (default 10, NT's disk time-out).
 *
 * Exit code: edge, the number of failed checks; the others, the SCSI status.
 * Builds with MSVC 6.0: cl /O2 vfyblk.c
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ntddscsi.h layout (the MSVC 6.0 headers do not have it) */
typedef struct {
    USHORT Length;
    UCHAR ScsiStatus;
    UCHAR PathId;
    UCHAR TargetId;
    UCHAR Lun;
    UCHAR CdbLength;
    UCHAR SenseInfoLength;
    UCHAR DataIn;
    ULONG DataTransferLength;
    ULONG TimeOutValue;
    ULONG DataBufferOffset;     /* ULONG_PTR; 32-bit build */
    ULONG SenseInfoOffset;
    UCHAR Cdb[16];
} SPT;

#define IOCTL_SCSI_PASS_THROUGH 0x0004D004
#define SCSI_IOCTL_DATA_OUT     0
#define SCSI_IOCTL_DATA_IN      1
#define SCSI_IOCTL_DATA_UNSPECIFIED 2

#define MAXDATA 65536

typedef struct {
    SPT spt;
    ULONG filler;
    UCHAR sense[32];
    UCHAR data[MAXDATA];
} SPTB;

static HANDLE h;
static int failures;
static SPTB *b;
static ULONG timeoutS = 10;
static DWORD elapsed;

static void check(int cond, const char *what)
{
    printf("  %s: %s\n", cond ? "ok  " : "FAIL", what);
    if (!cond) {
        failures++;
    }
}

/* One pass-through. dir is SCSI_IOCTL_DATA_IN, _OUT or _UNSPECIFIED; for
 * _OUT the caller has filled b->data. Returns FALSE if the IOCTL failed. */
static int pt(UCHAR *cdb, int cdbLen, UCHAR dir, ULONG dataLen)
{
    DWORD ret = 0, t0;
    BOOL r;

    memset(&b->spt, 0, sizeof(b->spt));
    memset(b->sense, 0, sizeof(b->sense));
    if (dir == SCSI_IOCTL_DATA_IN) {
        memset(b->data, 0xCC, dataLen);
    }
    b->spt.Length = sizeof(SPT);
    b->spt.CdbLength = (UCHAR)cdbLen;
    b->spt.SenseInfoLength = 18;
    b->spt.DataIn = dir;
    b->spt.DataTransferLength = dataLen;
    b->spt.TimeOutValue = timeoutS;
    b->spt.DataBufferOffset = (ULONG)((UCHAR *)b->data - (UCHAR *)b);
    b->spt.SenseInfoOffset = (ULONG)((UCHAR *)b->sense - (UCHAR *)b);
    memcpy(b->spt.Cdb, cdb, cdbLen);
    t0 = GetTickCount();
    r = DeviceIoControl(h, IOCTL_SCSI_PASS_THROUGH, b, sizeof(*b), b, sizeof(*b), &ret, NULL);
    elapsed = GetTickCount() - t0;
    if (!r) {
        printf("  DeviceIoControl failed: %lu after %lu ms\n", GetLastError(), elapsed);
        return 0;
    }
    return 1;
}

static void report(void)
{
    printf("  status %02x, sense %x/%02x/%02x, data %lu bytes, %lu ms\n", b->spt.ScsiStatus,
           b->sense[2] & 0x0F, b->sense[12], b->sense[13], b->spt.DataTransferLength, elapsed);
}

static int isGood(ULONG dataLen)
{
    return b->spt.ScsiStatus == 0 && b->spt.DataTransferLength == dataLen;
}

static int isSense(UCHAR key, UCHAR asc)
{
    return b->spt.ScsiStatus == 2 && b->spt.SenseInfoLength >= 14 && (b->sense[2] & 0x0F) == key &&
           b->sense[12] == asc && b->sense[13] == 0;
}

static void put32(UCHAR *p, ULONG v)
{
    p[0] = (UCHAR)(v >> 24);
    p[1] = (UCHAR)(v >> 16);
    p[2] = (UCHAR)(v >> 8);
    p[3] = (UCHAR)v;
}

static ULONG get32(UCHAR *p)
{
    return ((ULONG)p[0] << 24) | ((ULONG)p[1] << 16) | ((ULONG)p[2] << 8) | p[3];
}

/* A (10) CDB: opcode, flags byte 1, a 32-bit LBA and a 16-bit count. */
static void cdb10(UCHAR *c, UCHAR op, UCHAR flags, ULONG lba, ULONG count)
{
    memset(c, 0, 16);
    c[0] = op;
    c[1] = flags;
    put32(c + 2, lba);
    c[7] = (UCHAR)(count >> 8);
    c[8] = (UCHAR)count;
}

/* A (16) CDB: opcode, flags, a 64-bit LBA (high, low) and a 32-bit count. */
static void cdb16(UCHAR *c, UCHAR op, UCHAR flags, ULONG lbaHi, ULONG lbaLo, ULONG count)
{
    memset(c, 0, 16);
    c[0] = op;
    c[1] = flags;
    put32(c + 2, lbaHi);
    put32(c + 6, lbaLo);
    put32(c + 10, count);
}

static ULONG arg(const char *s)
{
    return (ULONG)strtoul(s, NULL, 0);
}

static int edge(ULONG bs)
{
    UCHAR c[16];
    ULONG last, blk, lastHi, lastLo, i, maxSectors;
    UCHAR *old;
    char text[160];

    printf("READ CAPACITY(10)\n");
    memset(c, 0, sizeof(c));
    c[0] = 0x25;
    if (!pt(c, 10, SCSI_IOCTL_DATA_IN, 8)) {
        check(0, "READ CAPACITY(10)");
        return failures;
    }
    report();
    last = get32(b->data);
    blk = get32(b->data + 4);
    printf("  last LBA %lu (0x%08lx), block length %lu\n", last, last, blk);
    check(isGood(8) && (blk == 512 || blk == 4096) && (bs == 0 || blk == bs), "a block length of 512 or 4096 (bs=)");
    if (!isGood(8) || (blk != 512 && blk != 4096)) {
        printf("the checks need a good READ CAPACITY(10) with a block length of 512 or 4096: stopped\n");
        return failures;
    }

    printf("READ CAPACITY(16)\n");
    memset(c, 0, sizeof(c));
    c[0] = 0x9E;
    c[1] = 0x10;
    c[13] = 32;
    if (pt(c, 16, SCSI_IOCTL_DATA_IN, 32)) {
        report();
        lastHi = get32(b->data);
        lastLo = get32(b->data + 4);
        printf("  last LBA 0x%08lx%08lx, block length %lu, exponent %u\n", lastHi, lastLo,
               get32(b->data + 8), b->data[13] & 0x0F);
        check(isGood(32) && lastHi == 0 && lastLo == last && get32(b->data + 8) == blk,
              "the same last LBA and block length as READ CAPACITY(10)");
        if (!isGood(32) || lastHi != 0 || lastLo != last || get32(b->data + 8) != blk) {
            printf("the boundary checks need the same last LBA and block length in both answers: stopped\n");
            return failures;
        }
    } else {
        check(0, "READ CAPACITY(16)");
        return failures;
    }
    if (last == 0xFFFFFFFFUL) {
        printf("the last LBA is 0xFFFFFFFF: one past it does not fit a (10) CDB: stopped\n");
        return failures;
    }

    printf("READ(10) of the last block\n");
    cdb10(c, 0x28, 0, last, 1);
    check(pt(c, 10, SCSI_IOCTL_DATA_IN, blk) && (report(), isGood(blk)), "GOOD, one block of data");
    printf("READ(16) of the last block\n");
    cdb16(c, 0x88, 0, 0, last, 1);
    check(pt(c, 16, SCSI_IOCTL_DATA_IN, blk) && (report(), isGood(blk)), "GOOD, one block of data");
    printf("READ(10) of the block past the last\n");
    cdb10(c, 0x28, 0, last + 1, 1);
    check(pt(c, 10, SCSI_IOCTL_DATA_IN, blk) && (report(), isSense(5, 0x21)), "05/21/00");
    printf("READ(16) of the block past the last\n");
    cdb16(c, 0x88, 0, 0, last + 1, 1);
    check(pt(c, 16, SCSI_IOCTL_DATA_IN, blk) && (report(), isSense(5, 0x21)), "05/21/00");

    printf("VERIFY(10) of the last block\n");
    cdb10(c, 0x2F, 0, last, 1);
    check(pt(c, 10, SCSI_IOCTL_DATA_UNSPECIFIED, 0) && (report(), isGood(0)), "GOOD, no data");
    printf("VERIFY(16) of the last block\n");
    cdb16(c, 0x8F, 0, 0, last, 1);
    check(pt(c, 16, SCSI_IOCTL_DATA_UNSPECIFIED, 0) && (report(), isGood(0)), "GOOD, no data");
    printf("VERIFY(10) of two blocks from the last\n");
    cdb10(c, 0x2F, 0, last, 2);
    check(pt(c, 10, SCSI_IOCTL_DATA_UNSPECIFIED, 0) && (report(), isSense(5, 0x21)), "05/21/00");
    printf("VERIFY(16) of the block past the last\n");
    cdb16(c, 0x8F, 0, 0, last + 1, 1);
    check(pt(c, 16, SCSI_IOCTL_DATA_UNSPECIFIED, 0) && (report(), isSense(5, 0x21)), "05/21/00");
    printf("VERIFY(10) with a count of 0\n");
    cdb10(c, 0x2F, 0, 0, 0);
    check(pt(c, 10, SCSI_IOCTL_DATA_UNSPECIFIED, 0) && (report(), isGood(0)), "GOOD, no data");
    printf("VERIFY(6)\n");
    memset(c, 0, sizeof(c));
    c[0] = 0x13;
    c[4] = 1;
    check(pt(c, 6, SCSI_IOCTL_DATA_UNSPECIFIED, 0) && (report(), isSense(5, 0x20)), "05/20/00");
    printf("VERIFY(10) with BYTCHK\n");
    cdb10(c, 0x2F, 0x02, 0, 1);
    check(pt(c, 10, SCSI_IOCTL_DATA_UNSPECIFIED, 0) && (report(), isSense(5, 0x24)), "05/24/00");
    maxSectors = (32UL << 20) / blk;
    if (maxSectors > 65535) {
        maxSectors = 65535;
    }
    sprintf(text, "VERIFY(16) of %lu blocks (one more than %lu)", maxSectors + 1, maxSectors);
    printf("%s\n", text);
    if (last >= maxSectors) {
        cdb16(c, 0x8F, 0, 0, 0, maxSectors + 1);
        check(pt(c, 16, SCSI_IOCTL_DATA_UNSPECIFIED, 0) && (report(), isSense(5, 0x24)), "05/24/00");
    } else {
        printf("  skipped: the disk has fewer blocks\n");
    }

    printf("Raw one-block WRITE(10) of the last block\n");
    old = (UCHAR *)malloc(blk);
    if (old == NULL) {
        check(0, "memory");
        return failures;
    }
    cdb10(c, 0x28, 0, last, 1);
    if (pt(c, 10, SCSI_IOCTL_DATA_IN, blk) && isGood(blk)) {
        memcpy(old, b->data, blk);
        for (i = 0; i < blk; i++) {
            b->data[i] = (UCHAR)(i * 7 + (i >> 9) + 0x5A);
        }
        cdb10(c, 0x2A, 0, last, 1);
        check(pt(c, 10, SCSI_IOCTL_DATA_OUT, blk) && (report(), isGood(blk)), "the pattern written, GOOD");
        cdb10(c, 0x28, 0, last, 1);
        if (pt(c, 10, SCSI_IOCTL_DATA_IN, blk) && isGood(blk)) {
            for (i = 0; i < blk; i++) {
                if (b->data[i] != (UCHAR)(i * 7 + (i >> 9) + 0x5A)) {
                    break;
                }
            }
            check(i == blk, "the pattern read back");
        } else {
            check(0, "the pattern read back");
        }
        memcpy(b->data, old, blk);
        cdb10(c, 0x2A, 0, last, 1);
        check(pt(c, 10, SCSI_IOCTL_DATA_OUT, blk) && isGood(blk), "the old contents written back");
        cdb10(c, 0x28, 0, last, 1);
        check(pt(c, 10, SCSI_IOCTL_DATA_IN, blk) && isGood(blk) && memcmp(b->data, old, blk) == 0,
              "the old contents read back");
    } else {
        check(0, "the last block read before the write");
    }
    free(old);
    printf("WRITE(10) of the block past the last\n");
    memset(b->data, 0, blk);
    cdb10(c, 0x2A, 0, last + 1, 1);
    check(pt(c, 10, SCSI_IOCTL_DATA_OUT, blk) && (report(), isSense(5, 0x21)), "05/21/00");

    printf("%d failed\n", failures);
    return failures;
}

int main(int argc, char **argv)
{
    char path[64];
    UCHAR c[16];
    ULONG bs = 0, count, lba;
    int i, nargs = argc;

    for (i = 3; i < argc; i++) {
        if (strncmp(argv[i], "to=", 3) == 0) {
            timeoutS = arg(argv[i] + 3);
            nargs = i;
        } else if (strncmp(argv[i], "bs=", 3) == 0) {
            bs = arg(argv[i] + 3);
            nargs = i;
        }
    }
    if (nargs < 3) {
        printf("usage: vfyblk <physical drive number> edge [bs=512|bs=4096] | v6 |\n"
               "       v10 <lba> <count> [to=<s>] | v16 <lba> <count> [to=<s>] | r10 <lba> <count>\n");
        return 255;
    }
    b = (SPTB *)VirtualAlloc(NULL, sizeof(SPTB), MEM_COMMIT, PAGE_READWRITE);
    if (b == NULL) {
        printf("VirtualAlloc failed\n");
        return 254;
    }
    sprintf(path, "\\\\.\\PhysicalDrive%s", argv[1]);
    h = CreateFile(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                   OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("open %s failed: %lu\n", path, GetLastError());
        return 254;
    }
    if (strcmp(argv[2], "edge") == 0) {
        return edge(bs);
    }
    if (strcmp(argv[2], "v6") == 0) {
        printf("VERIFY(6) of 1 block at 0\n");
        memset(c, 0, sizeof(c));
        c[0] = 0x13;
        c[4] = 1;
        if (!pt(c, 6, SCSI_IOCTL_DATA_UNSPECIFIED, 0)) {
            return 253;
        }
        report();
        return b->spt.ScsiStatus;
    }
    if (nargs < 5) {
        printf("%s needs <lba> <count>\n", argv[2]);
        return 255;
    }
    lba = arg(argv[3]);
    count = arg(argv[4]);
    if (strcmp(argv[2], "v10") == 0 || strcmp(argv[2], "v16") == 0) {
        printf("VERIFY(%s) of %lu blocks at %lu, time-out %lu s\n", argv[2] + 1, count, lba, timeoutS);
        if (argv[2][1] == '1' && argv[2][2] == '0') {
            if (count > 65535) {
                printf("VERIFY(10) counts at most 65535 blocks\n");
                return 255;
            }
            cdb10(c, 0x2F, 0, lba, count);
            i = pt(c, 10, SCSI_IOCTL_DATA_UNSPECIFIED, 0);
        } else {
            cdb16(c, 0x8F, 0, 0, lba, count);
            i = pt(c, 16, SCSI_IOCTL_DATA_UNSPECIFIED, 0);
        }
    } else if (strcmp(argv[2], "r10") == 0) {
        printf("READ(10) of %lu blocks at %lu\n", count, lba);
        if (bs == 0) {
            bs = 512;
        }
        if (count == 0 || count > MAXDATA / bs) {
            printf("READ(10) here counts 1 to %lu blocks of %lu bytes (bs=)\n", MAXDATA / bs, bs);
            return 255;
        }
        cdb10(c, 0x28, 0, lba, count);
        i = pt(c, 10, SCSI_IOCTL_DATA_IN, count * bs);
    } else {
        printf("unknown command %s\n", argv[2]);
        return 255;
    }
    if (!i) {
        return 253;
    }
    report();
    return b->spt.ScsiStatus;
}
