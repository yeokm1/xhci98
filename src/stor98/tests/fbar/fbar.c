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
 * fbar - a flush is a barrier for the writes before it (PLAN 0.11 class 1,
 * 1.6; TESTING "I/O after barriers").
 *
 *   fbar <physical drive number> [writes] [startMB] [flush path]
 *
 * Finds unpartitioned space after the last MBR partition (refuses to run
 * without 1 MiB of it), or, with startMB, writes at that offset whatever is
 * there (a test disk only: it overwrites those sectors, which on a test
 * volume should be free space), issues <writes> (default 4) overlapped 64 KiB
 * writes there, then FlushFileBuffers on the same handle while they are in
 * flight (the harness holds every write at fetch for some seconds:
 * x-faults "at=fetch op=write act=hold delay=3000 repeat=on"). The flush
 * must not return before every earlier write has completed: a write still
 * pending when FlushFileBuffers returns is a FAIL. Exit code: number of
 * writes still pending at the flush's return (0 = pass), or 250+ for setup
 * errors. Builds with MSVC 6.0: cl /O2 fbar.c
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#define MAXW    16
#define WBYTES  65536

typedef struct {
    USHORT Length;
    UCHAR ScsiStatus, PathId, TargetId, Lun, CdbLength, SenseInfoLength, DataIn;
    ULONG DataTransferLength, TimeOutValue, DataBufferOffset, SenseInfoOffset;
    UCHAR Cdb[16];
} SPT;

typedef struct {
    SPT spt;
    ULONG filler;
    UCHAR sense[32];
    UCHAR data[512];
} SPTB;

#define IOCTL_SCSI_PASS_THROUGH 0x0004D004

int main(int argc, char **argv)
{
    char path[64];
    HANDLE h, hs;
    SPTB b;
    DWORD ret, t0, tf, n;
    ULONG last, end = 0, start;
    UCHAR mbr[512];
    OVERLAPPED ov[MAXW];
    HANDLE ev[MAXW];
    UCHAR *buf;
    int writes = 4, i, pending = 0, flushFailed = 0;
    HANDLE hf;

    if (argc < 2) {
        printf("usage: fbar <physical drive number> [writes]\n");
        return 255;
    }
    if (argc > 2) {
        writes = atoi(argv[2]);
    }
    if (writes < 1 || writes > MAXW) {
        return 255;
    }
    sprintf(path, "\\\\.\\PhysicalDrive%s", argv[1]);

    /* Capacity (pass-through READ CAPACITY) and the MBR, synchronously. */
    hs = CreateFile(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                    OPEN_EXISTING, 0, NULL);
    if (hs == INVALID_HANDLE_VALUE) {
        printf("open %s failed: %lu\n", path, GetLastError());
        return 254;
    }
    memset(&b, 0, sizeof(b));
    b.spt.Length = sizeof(SPT);
    b.spt.CdbLength = 10;
    b.spt.SenseInfoLength = 18;
    b.spt.DataIn = 1;
    b.spt.DataTransferLength = 8;
    b.spt.TimeOutValue = 10;
    b.spt.DataBufferOffset = (ULONG)((UCHAR *)b.data - (UCHAR *)&b);
    b.spt.SenseInfoOffset = (ULONG)((UCHAR *)b.sense - (UCHAR *)&b);
    b.spt.Cdb[0] = 0x25;
    if (!DeviceIoControl(hs, IOCTL_SCSI_PASS_THROUGH, &b, sizeof(b), &b, sizeof(b), &ret, NULL) ||
        b.spt.ScsiStatus != 0) {
        printf("READ CAPACITY failed\n");
        return 253;
    }
    last = ((ULONG)b.data[0] << 24) | ((ULONG)b.data[1] << 16) | ((ULONG)b.data[2] << 8) | b.data[3];
    if (!ReadFile(hs, mbr, 512, &n, NULL) || n != 512 || mbr[510] != 0x55 || mbr[511] != 0xAA) {
        printf("no MBR\n");
        return 252;
    }
    for (i = 0; i < 4; i++) {
        UCHAR *e = mbr + 0x1BE + 16 * i;
        ULONG s = *(ULONG *)(e + 8), c = *(ULONG *)(e + 12);

        if (e[4] != 0 && s + c > end) {
            end = s + c;
        }
    }
    CloseHandle(hs);
    start = (end + 2047) & ~2047UL;
    if (argc > 3) {
        start = strtoul(argv[3], NULL, 0) * 2048;
        printf("writing at %lu MB as asked (overwrites what is there)\n", start / 2048);
    }
    printf("%s: last LBA %lu, partitions end at %lu, writes from %lu\n", path, last, end, start);
    if (start + 2048 > last + 1 || (ULONG)writes * (WBYTES / 512) > last + 1 - start) {
        printf("not enough unpartitioned space\n");
        return 251;
    }

    h = CreateFile(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                   OPEN_EXISTING, FILE_FLAG_OVERLAPPED | FILE_FLAG_NO_BUFFERING, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("open %s failed: %lu\n", path, GetLastError());
        return 254;
    }
    hf = h;
    buf = (UCHAR *)VirtualAlloc(NULL, WBYTES * MAXW, MEM_COMMIT, PAGE_READWRITE);
    if (buf == NULL) {
        return 250;
    }
    t0 = GetTickCount();
    for (i = 0; i < writes; i++) {
        ULONG lba = start + (ULONG)i * (WBYTES / 512);

        memset(buf + i * WBYTES, 0x40 + i, WBYTES);
        ev[i] = CreateEvent(NULL, TRUE, FALSE, NULL);
        ZeroMemory(&ov[i], sizeof(ov[i]));
        ov[i].hEvent = ev[i];
        ov[i].Offset = lba << 9;
        ov[i].OffsetHigh = lba >> 23;
        if (!WriteFile(h, buf + i * WBYTES, WBYTES, &n, &ov[i]) && GetLastError() != ERROR_IO_PENDING) {
            printf("write %d failed: %lu\n", i, GetLastError());
            return 249;
        }
    }
    Sleep(200);     /* let every write reach the device */
    if (argc > 4) {
        /* The disk's own handle does not take IRP_MJ_FLUSH_BUFFERS on
         * every OS (2000: ERROR_INVALID_FUNCTION); flush through another
         * path instead, e.g. the volume (\\.\F:), whose file system passes
         * the flush down to the disk as SYNCHRONIZE CACHE. */
        hf = CreateFile(argv[4], GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                        OPEN_EXISTING, 0, NULL);
        if (hf == INVALID_HANDLE_VALUE) {
            printf("open %s failed: %lu\n", argv[4], GetLastError());
            return 248;
        }
    }
    printf("%6lu ms: %d writes issued, flushing %s\n", GetTickCount() - t0, writes, argc > 4 ? argv[4] : path);
    if (!FlushFileBuffers(hf)) {
        printf("FlushFileBuffers failed: %lu\n", GetLastError());
        flushFailed = 1;
    }
    tf = GetTickCount();
    for (i = 0; i < writes; i++) {
        if (WaitForSingleObject(ev[i], 0) != WAIT_OBJECT_0) {
            pending++;
        }
    }
    printf("%6lu ms: flush returned with %d of %d earlier writes still pending\n", tf - t0, pending, writes);
    WaitForMultipleObjects(writes, ev, TRUE, 120000);
    printf("%6lu ms: all writes done\n", GetTickCount() - t0);
    if (flushFailed) {
        printf("INCONCLUSIVE: the flush did not reach the disk\n");
        return 247;
    }
    printf("%s\n", pending ? "FAIL: flush completed before earlier writes" : "PASS");
    CloseHandle(h);
    return pending;
}
