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
 * sensechk - sense retention and REQUEST SENSE through IOCTL_SCSI_PASS_THROUGH
 * (PLAN "Sense rules", R3-9; TESTING "Sense construction", the pass-through
 * part of cases (b)-(d)).
 *
 *   sensechk <physical drive number>
 *
 * Reads the capacity, then issues READ(10) one block past the last LBA
 * (ILLEGAL REQUEST, LOGICAL BLOCK ADDRESS OUT OF RANGE, 05/21/00) with a
 * sense length of 0, 14 and 18, each followed by REQUEST SENSE (allocation
 * length 18). Expected:
 *   - length 0 and 14: CHECK CONDITION (or the IOCTL fails with
 *     ERROR_IO_DEVICE), no sense bytes returned for the READ; the following
 *     REQUEST SENSE returns 70h, key 5, ASC 21h. If the port substitutes
 *     its own full-size sense buffer (Windows 2000 does for 14), the READ
 *     returns the sense and REQUEST SENSE then returns NO SENSE.
 *   - length 18: the READ returns the sense itself (autosense); the
 *     following REQUEST SENSE returns NO SENSE (it was delivered).
 * Also a REQUEST SENSE with allocation length 8 (bounded: 8 bytes). Exit
 * code: number of failed checks. Builds with MSVC 6.0: cl /O2 sensechk.c
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

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
#define SCSI_IOCTL_DATA_IN      1

typedef struct {
    SPT spt;
    ULONG filler;
    UCHAR sense[32];
    UCHAR data[512];
} SPTB;

static HANDLE h;
static int failures;
static DWORD ioErr;

static int pt(UCHAR *cdb, int cdbLen, int senseLen, ULONG dataLen, SPTB *b)
{
    DWORD ret = 0;

    memset(b, 0, sizeof(*b));
    memset(b->sense, 0xCC, sizeof(b->sense));
    memset(b->data, 0xCC, sizeof(b->data));
    b->spt.Length = sizeof(SPT);
    b->spt.CdbLength = (UCHAR)cdbLen;
    b->spt.SenseInfoLength = (UCHAR)senseLen;
    b->spt.DataIn = SCSI_IOCTL_DATA_IN;
    b->spt.DataTransferLength = dataLen;
    b->spt.TimeOutValue = 10;
    b->spt.DataBufferOffset = (ULONG)((UCHAR *)b->data - (UCHAR *)b);
    b->spt.SenseInfoOffset = (ULONG)((UCHAR *)b->sense - (UCHAR *)b);
    memcpy(b->spt.Cdb, cdb, cdbLen);
    ioErr = 0;
    if (!DeviceIoControl(h, IOCTL_SCSI_PASS_THROUGH, b, sizeof(*b), b, sizeof(*b), &ret, NULL)) {
        ioErr = GetLastError();
        printf("  DeviceIoControl failed: %lu\n", ioErr);
        return 0;
    }
    return 1;
}

static void dump(const char *what, UCHAR *p, int n)
{
    int i;

    printf("  %s:", what);
    for (i = 0; i < n; i++) {
        printf(" %02x", p[i]);
    }
    printf("\n");
}

static void check(int cond, const char *what)
{
    printf("  %s: %s\n", cond ? "ok  " : "FAIL", what);
    if (!cond) {
        failures++;
    }
}

/* REQUEST SENSE with allocation length alloc; returns key, -1 on error */
static int reqsense(int alloc, UCHAR *asc, ULONG *len)
{
    UCHAR cdb[6] = { 0x03, 0, 0, 0, 0, 0 };
    SPTB b;

    cdb[4] = (UCHAR)alloc;
    if (!pt(cdb, 6, 0, alloc, &b)) {
        return -1;
    }
    printf("  REQUEST SENSE(%d): status %02x, %lu bytes\n", alloc, b.spt.ScsiStatus, b.spt.DataTransferLength);
    dump("data", b.data, 20);
    *asc = b.data[12];
    *len = b.spt.DataTransferLength;
    if (b.spt.ScsiStatus != 0) {
        return -1;
    }
    return b.data[2] & 0x0F;
}

int main(int argc, char **argv)
{
    char path[64];
    UCHAR cdb[10];
    SPTB b;
    ULONG last, len;
    UCHAR asc;
    int key, i;
    static const int lens[3] = { 0, 14, 18 };

    if (argc < 2) {
        printf("usage: sensechk <physical drive number>\n");
        return 255;
    }
    sprintf(path, "\\\\.\\PhysicalDrive%s", argv[1]);
    h = CreateFile(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                   OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("open %s failed: %lu\n", path, GetLastError());
        return 254;
    }

    memset(cdb, 0, sizeof(cdb));
    cdb[0] = 0x25;      /* READ CAPACITY(10) */
    if (!pt(cdb, 10, 18, 8, &b) || b.spt.ScsiStatus != 0) {
        printf("READ CAPACITY failed\n");
        return 253;
    }
    last = ((ULONG)b.data[0] << 24) | ((ULONG)b.data[1] << 16) | ((ULONG)b.data[2] << 8) | b.data[3];
    printf("%s: last LBA %lu, block %lu\n", path, last,
           ((ULONG)b.data[4] << 24) | ((ULONG)b.data[5] << 16) | ((ULONG)b.data[6] << 8) | b.data[7]);

    /* Clear anything kept from before. */
    reqsense(18, &asc, &len);

    for (i = 0; i < 3; i++) {
        ULONG lba = last + 1;

        printf("READ(10) LBA %lu, sense length %d\n", lba, lens[i]);
        memset(cdb, 0, sizeof(cdb));
        cdb[0] = 0x28;
        cdb[2] = (UCHAR)(lba >> 24);
        cdb[3] = (UCHAR)(lba >> 16);
        cdb[4] = (UCHAR)(lba >> 8);
        cdb[5] = (UCHAR)lba;
        cdb[8] = 1;
        if (!pt(cdb, 10, lens[i], 512, &b)) {
            /* The port fails the IOCTL itself (ERROR_IO_DEVICE) when the
             * request failed and it has no sense to return. */
            check(ioErr == ERROR_IO_DEVICE && lens[i] < 18, "READ failed (no sense returned)");
            b.spt.SenseInfoLength = 0;
        } else {
            printf("  status %02x, sense length returned %u\n", b.spt.ScsiStatus, b.spt.SenseInfoLength);
            dump("sense buffer", b.sense, 20);
            check(b.spt.ScsiStatus == 2, "CHECK CONDITION");
        }
        key = reqsense(18, &asc, &len);
        if (lens[i] < 18 && b.spt.SenseInfoLength != 0) {
            /* The port gave the miniport a full-size sense buffer of its
             * own and copied part of it back (sense-buffer substitution,
             * recorded): the sense was delivered, nothing is kept. */
            printf("  port substituted its own sense buffer (%u bytes returned)\n", b.spt.SenseInfoLength);
            check((b.sense[2] & 0x0F) == 5 && b.sense[12] == 0x21, "returned sense is 05/21");
            check(key == 0, "REQUEST SENSE afterwards: NO SENSE");
        } else if (lens[i] < 18) {
            check(key == 5 && asc == 0x21 && len == 18, "REQUEST SENSE returns the kept 05/21 (18 bytes)");
        } else {
            check(b.spt.SenseInfoLength >= 18 && (b.sense[2] & 0x0F) == 5 && b.sense[12] == 0x21,
                  "autosense 05/21 with the READ");
            check(key == 0, "REQUEST SENSE afterwards: NO SENSE");
        }
    }

    printf("bounded REQUEST SENSE (allocation length 8) after a failed READ\n");
    memset(cdb, 0, sizeof(cdb));
    cdb[0] = 0x28;
    last++;
    cdb[2] = (UCHAR)(last >> 24);
    cdb[3] = (UCHAR)(last >> 16);
    cdb[4] = (UCHAR)(last >> 8);
    cdb[5] = (UCHAR)last;
    cdb[8] = 1;
    pt(cdb, 10, 0, 512, &b);
    key = reqsense(8, &asc, &len);
    check(len == 8 && key == 5, "8 bytes, key 5");

    printf("%s: %d failed checks\n", failures ? "FAIL" : "PASS", failures);
    CloseHandle(h);
    return failures > 250 ? 250 : failures;     /* a count of 256 would exit 0 */
}
