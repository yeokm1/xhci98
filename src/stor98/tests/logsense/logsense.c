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
 * logsense - an admin command on demand: LOG SENSE of the informational
 * exceptions page (2Fh) through IOCTL_SCSI_PASS_THROUGH, which the engine
 * turns into an NVMe Get Log Page child on the admin queue (review round 2
 * P1-6 tests: a late admin completion of a quarantined child).
 *
 *   logsense <physical drive number> [timeout s, default 10] [count, default 1]
 *
 * Prints, per request, the elapsed time, whether the IOCTL succeeded (and
 * its error), ScsiStatus and the first bytes returned. Exit code: number of
 * failed requests. Builds with MSVC 6.0: cl /O2 logsense.c
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

int main(int argc, char **argv)
{
    char path[64];
    HANDLE h;
    SPTB b;
    DWORD ret, t0, err;
    ULONG timeout = 10;
    int count = 1, i, failed = 0;

    if (argc < 2) {
        printf("usage: logsense <drive number> [timeout s] [count]\n");
        return 255;
    }
    if (argc > 2) timeout = strtoul(argv[2], NULL, 0);
    if (argc > 3) count = atoi(argv[3]);
    sprintf(path, "\\\\.\\PhysicalDrive%s", argv[1]);
    h = CreateFile(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                   OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("open %s failed: %lu\n", path, GetLastError());
        return 254;
    }
    for (i = 0; i < count; i++) {
        memset(&b, 0, sizeof(b));
        b.spt.Length = sizeof(SPT);
        b.spt.CdbLength = 10;
        b.spt.SenseInfoLength = 18;
        b.spt.DataIn = SCSI_IOCTL_DATA_IN;
        b.spt.DataTransferLength = 512;
        b.spt.TimeOutValue = timeout;
        b.spt.DataBufferOffset = (ULONG)((UCHAR *)b.data - (UCHAR *)&b);
        b.spt.SenseInfoOffset = (ULONG)((UCHAR *)b.sense - (UCHAR *)&b);
        b.spt.Cdb[0] = 0x4D;                /* LOG SENSE */
        b.spt.Cdb[2] = 0x40 | 0x2F;         /* PC = cumulative, page 2Fh */
        b.spt.Cdb[7] = 0x02;                /* allocation length 512 */
        b.spt.Cdb[8] = 0x00;
        t0 = GetTickCount();
        err = 0;
        if (!DeviceIoControl(h, IOCTL_SCSI_PASS_THROUGH, &b, sizeof(b), &b, sizeof(b), &ret, NULL)) {
            err = GetLastError();
        }
        printf("%d: %lu ms ioctl %s (%lu) scsi %02x len %lu data %02x %02x %02x %02x sense %02x/%02x\n", i,
               (unsigned long)(GetTickCount() - t0), err ? "FAILED" : "ok", err, b.spt.ScsiStatus,
               b.spt.DataTransferLength, b.data[0], b.data[1], b.data[2], b.data[3], b.sense[2] & 0x0F,
               b.sense[12]);
        if (err || b.spt.ScsiStatus != 0) {
            failed++;
        }
    }
    CloseHandle(h);
    return failed;
}
