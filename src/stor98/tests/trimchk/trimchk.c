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
 * trimchk - regression test for the removed magic-pattern TRIM (PLAN 1.5,
 * TESTING "TRIM removed"). NT only.
 *
 *   trimchk <file on an stor98 volume> [port]
 *
 * 1. Finds the SCSI port of the file's volume (IOCTL_SCSI_GET_ADDRESS on the
 *    volume; the inquiry data cannot identify stor98, since its vendor and
 *    product come from the drive's model number), or takes [port] as given,
 *    and sends the old NVME2KDB IOCTLs through IOCTL_SCSI_MINIPORT: QUERY_INFO
 *    (0x1000, must still succeed: the IOCTL reaches stor98) and TRIM_MODE_ON
 *    (0x1001, with a 4 KiB pattern: must now be rejected).
 * 2. Writes, unbuffered and write-through, an 8 KiB buffer whose bytes
 *    16-4095 match the pattern and whose first 16 bytes differ, checks the
 *    caller's buffer is unchanged after the write completed, and reads the
 *    file back unbuffered to check the disk contents.
 * Exit code 0 = pass. Builds with MSVC 6.0: cl /O2 trimchk.c
 */

#include <windows.h>
#include <winioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IOCTL_SCSI_MINIPORT_X         0x0004D008
#define IOCTL_SCSI_GET_ADDRESS_X      0x00041018

typedef struct {
    ULONG HeaderLength;
    UCHAR Signature[8];
    ULONG Timeout;
    ULONG ControlCode;
    ULONG ReturnCode;
    ULONG Length;
} SIO;

typedef struct {                /* ntddscsi.h SCSI_ADDRESS */
    ULONG Length;
    UCHAR PortNumber;
    UCHAR PathId;
    UCHAR TargetId;
    UCHAR Lun;
} SCSI_ADDR;

static unsigned char pattern[4096];

/* The SCSI port number of the volume that holds Path, -1 if unknown. */
static int volume_port(const char *path)
{
    char full[MAX_PATH], vol[8], *part;
    HANDLE v;
    SCSI_ADDR a;
    DWORD n = 0;
    BOOL ok;

    if (GetFullPathName(path, sizeof(full), full, &part) == 0 || full[1] != ':') {
        return -1;
    }
    sprintf(vol, "\\\\.\\%c:", full[0]);
    v = CreateFile(vol, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (v == INVALID_HANDLE_VALUE) {
        return -1;
    }
    memset(&a, 0, sizeof(a));
    a.Length = sizeof(a);
    ok = DeviceIoControl(v, IOCTL_SCSI_GET_ADDRESS_X, NULL, 0, &a, sizeof(a), &n, NULL);
    CloseHandle(v);
    return ok && n >= sizeof(a) ? a.PortNumber : -1;
}

static int ioctl(HANDLE h, ULONG code, const unsigned char *data, ULONG len, ULONG *ret)
{
    static unsigned char buf[sizeof(SIO) + 4096];
    SIO *s = (SIO *)buf;
    DWORD n = 0;
    BOOL ok;

    memset(buf, 0, sizeof(buf));
    s->HeaderLength = sizeof(SIO);
    memcpy(s->Signature, "NVME2KDB", 8);
    s->Timeout = 10;
    s->ControlCode = code;
    s->Length = len;
    if (len) {
        memcpy(buf + sizeof(SIO), data, len);
    }
    ok = DeviceIoControl(h, IOCTL_SCSI_MINIPORT_X, buf, sizeof(SIO) + len, buf, sizeof(SIO) + len, &n, NULL);
    *ret = ok ? s->ReturnCode : GetLastError();
    return ok ? 1 : 0;
}

int main(int argc, char **argv)
{
    char name[32];
    unsigned char *mem, *buf, *ref, *rd;
    HANDLE h, f;
    int port, fails = 0;
    DWORD n, i;
    ULONG ret;

    if (argc < 2) {
        printf("usage: trimchk <file on an stor98 volume> [port]\n");
        return 255;
    }
    for (i = 0; i < sizeof(pattern); i++) {
        pattern[i] = (unsigned char)(i * 7 + 3);
    }
    port = argc > 2 ? atoi(argv[2]) : volume_port(argv[1]);
    if (port < 0) {
        printf("cannot find the SCSI port of %s's volume (give it as the second argument)\n", argv[1]);
        return 254;
    }
    sprintf(name, "\\\\.\\Scsi%d:", port);
    h = CreateFile(name, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                   OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("open %s failed: %lu\n", name, GetLastError());
        return 254;
    }
    printf("stor98 port: %s\n", name);

    if (ioctl(h, 0x1000, NULL, 0, &ret) && ret == 0) {
        printf("QUERY_INFO: accepted (the IOCTL reaches stor98)\n");
    } else {
        printf("QUERY_INFO: FAILED (%lu) - the IOCTL does not reach stor98\n", ret);
        fails++;
    }
    if (ioctl(h, 0x1001, pattern, sizeof(pattern), &ret) && ret == 0) {
        printf("TRIM_MODE_ON: ACCEPTED - FAIL (the magic-pattern TRIM is still there)\n");
        fails++;
    } else {
        printf("TRIM_MODE_ON: rejected (%lu) - ok\n", ret);
    }
    CloseHandle(h);

    mem = (unsigned char *)VirtualAlloc(NULL, 3 * 8192, MEM_COMMIT, PAGE_READWRITE);
    if (mem == NULL) {
        printf("alloc failed: %lu\n", GetLastError());
        return 252;
    }
    buf = mem;
    ref = mem + 8192;
    rd = mem + 16384;
    memcpy(buf, "stor98 trimchk!!", 16);
    memcpy(buf + 16, pattern + 16, 4096 - 16);
    for (i = 4096; i < 8192; i++) {
        buf[i] = (unsigned char)(i ^ 0x5A);
    }
    memcpy(ref, buf, 8192);

    f = CreateFile(argv[1], GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                   FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        printf("open %s failed: %lu\n", argv[1], GetLastError());
        return 253;
    }
    if (!WriteFile(f, buf, 8192, &n, NULL) || n != 8192) {
        printf("write failed: %lu\n", GetLastError());
        fails++;
    }
    if (memcmp(buf, ref, 8192) != 0) {
        printf("caller buffer CHANGED by the write - FAIL\n");
        fails++;
    } else {
        printf("caller buffer unchanged - ok\n");
    }
    CloseHandle(f);
    f = CreateFile(argv[1], GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, NULL);
    if (f == INVALID_HANDLE_VALUE || !ReadFile(f, rd, 8192, &n, NULL) || n != 8192) {
        printf("read back failed: %lu\n", GetLastError());
        fails++;
    } else if (memcmp(rd, ref, 8192) != 0) {
        printf("disk contents DIFFER - FAIL\n");
        fails++;
    } else {
        printf("disk contents correct - ok\n");
    }
    if (f != INVALID_HANDLE_VALUE) {
        CloseHandle(f);
    }
    printf("%s\n", fails ? "FAIL" : "PASS");
    return fails;
}
