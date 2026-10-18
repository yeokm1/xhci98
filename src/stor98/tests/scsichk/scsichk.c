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
 * scsichk - the driver's own SCSI answers through IOCTL_SCSI_PASS_THROUGH,
 * and the miniport IOCTL path through IOCTL_SCSI_MINIPORT (NT).
 *
 *   scsichk <physical drive number> [wce=0|wce=1] [bs=512|bs=4096]
 *
 * Checks:
 *   - INQUIRY: standard data (36 bytes), a short allocation (8 bytes
 *     returned, not a zeroed buffer); EVPD pages 00h (lists 00h, 80h,
 *     83h), 80h (20-character serial number), 83h (one T10 vendor ID
 *     designator, "NVMe" + model + serial); page B0h and a page code with
 *     EVPD=0 refused with CHECK CONDITION 05/24/00.
 *   - Opcodes the driver does not implement (READ(12), REPORT LUNS): 05/20;
 *     SERVICE ACTION IN other than READ CAPACITY(16): 05/24.
 *   - MODE SENSE: caching page WCE (compared with wce= when given: 1 when
 *     the drive has a volatile write cache, 0 under write-through cacheoff
 *     once verified, 0 without one); changeable values have zero page
 *     bodies and the block descriptor keeps the current block length;
 *     the header describes the whole response, also for a 4-byte buffer
 *     (mode data length as for 255 bytes, descriptor length 8); an
 *     unsupported page is 05/24.
 *   - READ CAPACITY(10)/(16): a real capacity, block size (compared with
 *     bs= when given).
 *   - IOCTL_SCSI_MINIPORT: "NVME2KDB" query (succeeds), "SCSIDISK" SMART
 *     RETURN STATUS and SMART VERSION (refused): each completes within 3 s,
 *     never at the port's timeout.
 *
 * Exit code: number of failed checks. Builds with MSVC 6.0: cl /O2 scsichk.c
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

typedef struct {
    ULONG Length;
    UCHAR PortNumber;
    UCHAR PathId;
    UCHAR TargetId;
    UCHAR Lun;
} SCSIADDR;

typedef struct {
    ULONG HeaderLength;
    UCHAR Signature[8];
    ULONG Timeout;
    ULONG ControlCode;
    ULONG ReturnCode;
    ULONG Length;
} SRBIOC;

#define IOCTL_SCSI_PASS_THROUGH 0x0004D004
#define IOCTL_SCSI_MINIPORT     0x0004D008
#define IOCTL_SCSI_GET_ADDRESS  0x00041018
#define SCSI_IOCTL_DATA_IN      1

typedef struct {
    SPT spt;
    ULONG filler;
    UCHAR sense[32];
    UCHAR data[512];
} SPTB;

static HANDLE h;
static int failures;

static void check(int cond, const char *what)
{
    printf("  %s: %s\n", cond ? "ok  " : "FAIL", what);
    if (!cond) {
        failures++;
    }
}

static void dump(const char *what, UCHAR *p, ULONG n)
{
    ULONG i;

    printf("  %s:", what);
    for (i = 0; i < n; i++) {
        printf(" %02x", p[i]);
    }
    printf("\n");
}

/* Pass-through with an 18-byte sense buffer; returns FALSE if the IOCTL failed. */
static int pt(UCHAR *cdb, int cdbLen, ULONG dataLen, SPTB *b)
{
    DWORD ret = 0;

    memset(b, 0, sizeof(*b));
    memset(b->data, 0xCC, sizeof(b->data));
    b->spt.Length = sizeof(SPT);
    b->spt.CdbLength = (UCHAR)cdbLen;
    b->spt.SenseInfoLength = 18;
    b->spt.DataIn = SCSI_IOCTL_DATA_IN;
    b->spt.DataTransferLength = dataLen;
    b->spt.TimeOutValue = 10;
    b->spt.DataBufferOffset = (ULONG)((UCHAR *)b->data - (UCHAR *)b);
    b->spt.SenseInfoOffset = (ULONG)((UCHAR *)b->sense - (UCHAR *)b);
    memcpy(b->spt.Cdb, cdb, cdbLen);
    if (!DeviceIoControl(h, IOCTL_SCSI_PASS_THROUGH, b, sizeof(*b), b, sizeof(*b), &ret, NULL)) {
        printf("  DeviceIoControl failed: %lu\n", GetLastError());
        return 0;
    }
    return 1;
}

/* A command that must fail with CHECK CONDITION, ILLEGAL REQUEST, asc/00. */
static void refused(const char *what, UCHAR *cdb, int cdbLen, ULONG dataLen, UCHAR asc)
{
    SPTB b;
    char text[128];

    printf("%s\n", what);
    sprintf(text, "CHECK CONDITION, sense 05/%02x/00, no data", asc);
    if (!pt(cdb, cdbLen, dataLen, &b)) {
        check(0, text);
        return;
    }
    dump("sense", b.sense, 18);
    check(b.spt.ScsiStatus == 2 && b.spt.SenseInfoLength >= 14 && (b.sense[2] & 0x0F) == 5 &&
          b.sense[12] == asc && b.sense[13] == 0 && b.spt.DataTransferLength == 0, text);
}

static int printable(UCHAR *p, ULONG n)
{
    ULONG i;

    for (i = 0; i < n; i++) {
        if (p[i] < 0x20 || p[i] > 0x7E) {
            return 0;
        }
    }
    return 1;
}

/* IOCTL_SCSI_MINIPORT with signature sig and control code cc; returns the
 * elapsed milliseconds, *ok TRUE when the IOCTL succeeded with ReturnCode 0. */
static DWORD miniport(HANDLE port, const char *sig, ULONG cc, int *ok)
{
    UCHAR buf[sizeof(SRBIOC) + 600];
    SRBIOC *ioc = (SRBIOC *)buf;
    DWORD ret = 0, t0;
    BOOL r;

    memset(buf, 0, sizeof(buf));
    ioc->HeaderLength = sizeof(SRBIOC);
    memcpy(ioc->Signature, sig, 8);
    ioc->Timeout = 10;
    ioc->ControlCode = cc;
    ioc->Length = sizeof(buf) - sizeof(SRBIOC);
    t0 = GetTickCount();
    r = DeviceIoControl(port, IOCTL_SCSI_MINIPORT, buf, sizeof(buf), buf, sizeof(buf), &ret, NULL);
    t0 = GetTickCount() - t0;
    printf("  %.8s %08lx: %s (error %lu), ReturnCode %lu, %lu ms\n", sig, cc, r ? "succeeded" : "failed",
           r ? 0 : GetLastError(), ioc->ReturnCode, t0);
    *ok = (r && ioc->ReturnCode == 0);
    return t0;
}

int main(int argc, char **argv)
{
    char path[64];
    UCHAR cdb[16], full6;
    SPTB b;
    int i, wce = -1, ok;
    ULONG bs = 0, last, blk;
    SCSIADDR addr;
    DWORD ret;
    HANDLE port;

    if (argc < 2) {
        printf("usage: scsichk <physical drive number> [wce=0|wce=1] [bs=512|bs=4096]\n");
        return 255;
    }
    for (i = 2; i < argc; i++) {
        if (strncmp(argv[i], "wce=", 4) == 0) {
            wce = atoi(argv[i] + 4);
        } else if (strncmp(argv[i], "bs=", 3) == 0) {
            bs = (ULONG)atol(argv[i] + 3);
        }
    }
    sprintf(path, "\\\\.\\PhysicalDrive%s", argv[1]);
    h = CreateFile(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                   OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("open %s failed: %lu\n", path, GetLastError());
        return 254;
    }

    printf("INQUIRY, standard, allocation 36\n");
    memset(cdb, 0, sizeof(cdb));
    cdb[0] = 0x12;
    cdb[4] = 36;
    if (pt(cdb, 6, 36, &b)) {
        dump("data", b.data, 36);
        printf("  [%.8s][%.16s][%.4s]\n", b.data + 8, b.data + 16, b.data + 32);
        check(b.spt.ScsiStatus == 0 && b.spt.DataTransferLength == 36 && b.data[2] == 5 && b.data[4] == 31 &&
              printable(b.data + 8, 28), "36 bytes, SPC-3, printable identity");
    } else {
        check(0, "standard INQUIRY");
    }

    printf("INQUIRY, standard, allocation 8\n");
    cdb[4] = 8;
    if (pt(cdb, 6, 8, &b)) {
        dump("data", b.data, 12);
        check(b.spt.ScsiStatus == 0 && b.spt.DataTransferLength == 8 && b.data[2] == 5 && b.data[4] == 31 &&
              b.data[7] == 2, "the first 8 bytes of the data, not a zeroed buffer");
    } else {
        check(0, "short INQUIRY");
    }

    printf("INQUIRY, EVPD page 00h\n");
    cdb[1] = 0x01;
    cdb[2] = 0x00;
    cdb[4] = 255;
    if (pt(cdb, 6, 255, &b)) {
        dump("data", b.data, b.spt.DataTransferLength < 16 ? b.spt.DataTransferLength : 16);
        check(b.spt.ScsiStatus == 0 && b.spt.DataTransferLength == 7 && b.data[1] == 0x00 && b.data[3] == 3 &&
              b.data[4] == 0x00 && b.data[5] == 0x80 && b.data[6] == 0x83, "supported pages 00h, 80h, 83h");
    } else {
        check(0, "VPD 00h");
    }

    printf("INQUIRY, EVPD page 80h\n");
    cdb[2] = 0x80;
    if (pt(cdb, 6, 255, &b)) {
        printf("  serial [%.20s]\n", b.data + 4);
        check(b.spt.ScsiStatus == 0 && b.spt.DataTransferLength == 24 && b.data[1] == 0x80 && b.data[3] == 20 &&
              printable(b.data + 4, 20), "unit serial number, 20 printable characters");
    } else {
        check(0, "VPD 80h");
    }

    printf("INQUIRY, EVPD page 83h\n");
    cdb[2] = 0x83;
    if (pt(cdb, 6, 255, &b)) {
        dump("header", b.data, 16);
        printf("  designator [%.68s]\n", b.data + 8);
        check(b.spt.ScsiStatus == 0 && b.spt.DataTransferLength == 76 && b.data[1] == 0x83 && b.data[3] == 72 &&
              b.data[4] == 0x02 && b.data[5] == 0x01 && b.data[7] == 68 && memcmp(b.data + 8, "NVMe    ", 8) == 0 &&
              printable(b.data + 8, 68), "one T10 vendor ID designator, ASCII, length 68");
    } else {
        check(0, "VPD 83h");
    }

    cdb[2] = 0xB0;
    refused("INQUIRY, EVPD page B0h", cdb, 6, 255, 0x24);
    cdb[1] = 0x00;
    cdb[2] = 0x83;
    refused("INQUIRY, page code 83h with EVPD=0", cdb, 6, 255, 0x24);

    memset(cdb, 0, sizeof(cdb));
    cdb[0] = 0xA8;      /* READ(12) */
    cdb[9] = 1;
    refused("READ(12)", cdb, 12, 512, 0x20);
    memset(cdb, 0, sizeof(cdb));
    cdb[0] = 0xA0;      /* REPORT LUNS */
    cdb[9] = 16;
    refused("REPORT LUNS", cdb, 12, 16, 0x20);
    memset(cdb, 0, sizeof(cdb));
    cdb[0] = 0x9E;      /* SERVICE ACTION IN, service action 11h */
    cdb[1] = 0x11;
    cdb[13] = 32;
    refused("SERVICE ACTION IN 11h", cdb, 16, 32, 0x24);

    printf("MODE SENSE(10), caching page, current values\n");
    memset(cdb, 0, sizeof(cdb));
    cdb[0] = 0x5A;
    cdb[1] = 0x08;      /* DBD */
    cdb[2] = 0x08;
    cdb[8] = 255;
    if (pt(cdb, 10, 255, &b)) {
        dump("data", b.data, b.spt.DataTransferLength < 28 ? b.spt.DataTransferLength : 28);
        printf("  WCE %d\n", (b.data[8 + 2] >> 2) & 1);
        check(b.spt.ScsiStatus == 0 && b.data[8] == 0x08 && b.data[9] == 18, "caching page returned");
        if (wce >= 0) {
            check(((b.data[8 + 2] >> 2) & 1) == wce, wce ? "WCE 1 (volatile write cache on)" : "WCE 0");
        }
    } else {
        check(0, "MODE SENSE caching page");
    }

    printf("MODE SENSE(10), all pages, changeable values\n");
    cdb[2] = 0x40 | 0x3F;
    if (pt(cdb, 10, 255, &b)) {
        ULONG n = b.spt.DataTransferLength, off = 8, bad = 0, pages = 0;

        dump("data", b.data, n < 40 ? n : 40);
        while (off + 2 <= n) {
            ULONG len = b.data[off + 1] + 2, k;

            for (k = 2; k < len && off + k < n; k++) {
                if (b.data[off + k] != 0) {
                    bad++;
                }
            }
            pages++;
            off += len;
        }
        printf("  %lu pages\n", pages);
        check(b.spt.ScsiStatus == 0 && pages >= 2 && bad == 0, "every page body is zero (nothing changeable)");
    } else {
        check(0, "MODE SENSE changeable values");
    }

    printf("MODE SENSE(6), DBD=0, all pages, allocation 255\n");
    memset(cdb, 0, sizeof(cdb));
    cdb[0] = 0x1A;
    cdb[2] = 0x3F;
    cdb[4] = 255;
    full6 = 0;
    if (pt(cdb, 6, 255, &b)) {
        dump("data", b.data, 12);
        full6 = b.data[0];
        check(b.spt.ScsiStatus == 0 && b.data[3] == 8 && (ULONG)full6 + 1 == b.spt.DataTransferLength,
              "block descriptor length 8, mode data length matches the data");
    } else {
        check(0, "MODE SENSE(6)");
    }
    printf("MODE SENSE(6), DBD=0, all pages, allocation 4 (header only)\n");
    cdb[4] = 4;
    if (pt(cdb, 6, 4, &b)) {
        dump("data", b.data, 4);
        check(b.spt.ScsiStatus == 0 && b.data[3] == 8 && b.data[0] == full6,
              "the header describes the whole response (length and descriptor)");
    } else {
        check(0, "MODE SENSE(6) header only");
    }
    printf("MODE SENSE(6), DBD=0, all pages, changeable values\n");
    cdb[2] = 0x40 | 0x3F;
    cdb[4] = 255;
    if (pt(cdb, 6, 255, &b)) {
        dump("data", b.data, 12);
        check(b.spt.ScsiStatus == 0 && b.data[3] == 8 &&
              (b.data[4 + 5] | b.data[4 + 6] | b.data[4 + 7]) != 0,
              "the block descriptor keeps the current block length");
    } else {
        check(0, "MODE SENSE(6) changeable values");
    }
    cdb[2] = 0x05;
    refused("MODE SENSE(6), page 05h", cdb, 6, 255, 0x24);

    printf("READ CAPACITY(10)\n");
    memset(cdb, 0, sizeof(cdb));
    cdb[0] = 0x25;
    if (pt(cdb, 10, 8, &b) && b.spt.ScsiStatus == 0) {
        last = ((ULONG)b.data[0] << 24) | ((ULONG)b.data[1] << 16) | ((ULONG)b.data[2] << 8) | b.data[3];
        blk = ((ULONG)b.data[4] << 24) | ((ULONG)b.data[5] << 16) | ((ULONG)b.data[6] << 8) | b.data[7];
        printf("  last LBA %lu, block %lu\n", last, blk);
        check(blk == 512 || blk == 1024 || blk == 2048 || blk == 4096, "block size 512 to 4096");
        if (bs != 0) {
            check(blk == bs, "block size as given (bs=)");
        }
    } else {
        check(0, "READ CAPACITY(10)");
    }
    printf("READ CAPACITY(16)\n");
    memset(cdb, 0, sizeof(cdb));
    cdb[0] = 0x9E;
    cdb[1] = 0x10;
    cdb[13] = 32;
    if (pt(cdb, 16, 32, &b) && b.spt.ScsiStatus == 0) {
        dump("data", b.data, 12);
        check(b.data[0] != 0xFF || b.data[4] != 0xFF, "a real last LBA");
    } else {
        check(0, "READ CAPACITY(16)");
    }

    printf("IOCTL_SCSI_MINIPORT\n");
    if (!DeviceIoControl(h, IOCTL_SCSI_GET_ADDRESS, NULL, 0, &addr, sizeof(addr), &ret, NULL)) {
        printf("  IOCTL_SCSI_GET_ADDRESS failed: %lu\n", GetLastError());
        check(0, "SCSI address");
    } else {
        sprintf(path, "\\\\.\\Scsi%u:", addr.PortNumber);
        port = CreateFile(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                          OPEN_EXISTING, 0, NULL);
        if (port == INVALID_HANDLE_VALUE) {
            printf("  open %s failed: %lu\n", path, GetLastError());
            check(0, "SCSI port");
        } else {
            check(miniport(port, "NVME2KDB", 0x1000, &ok) < 3000 && ok, "NVME2KDB query succeeds at once");
            check(miniport(port, "SCSIDISK", 0x001B0507, &ok) < 3000 && !ok,
                  "SCSIDISK SMART RETURN STATUS refused at once (no hang)");
            check(miniport(port, "SCSIDISK", 0x001B0500, &ok) < 3000 && !ok,
                  "SCSIDISK SMART VERSION refused at once (no hang)");
            CloseHandle(port);
        }
    }

    printf("%s: %d failed checks\n", failures ? "FAIL" : "PASS", failures);
    CloseHandle(h);
    return failures;
}
