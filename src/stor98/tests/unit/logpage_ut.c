/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, Yeo Kheng Meng
 *
 * logpage_ut - host unit test of LOG SENSE page 2Fh (src/logpage.h): the
 * header and its page length; parameter 0000h as SPC defines it (binary
 * list, length 4, ASC/ASCQ 00h/00h or 5Dh/00h with a critical warning, the
 * temperature in degrees Celsius in one byte, FFh with no reading); the
 * vendor parameters 0001h-0007h at their offsets with the log's bytes.
 * Built by tests\unit\build.cmd; the exit code is the number of failed
 * checks.
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "logpage.h"

static int failures;

static void check(int ok, const char *what)
{
    printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) {
        failures++;
    }
}

int main(void)
{
    UCHAR smart[512], page[LOG2F_MAX];
    ULONG n, i;

    printf("logpage_ut\n");

    memset(smart, 0, sizeof(smart));
    smart[1] = (UCHAR)(310 & 0xFF);         /* 310 K = 37 C */
    smart[2] = (UCHAR)(310 >> 8);
    smart[3] = 100;                         /* available spare */
    smart[5] = 7;                           /* percentage used */
    for (i = 0; i < 8; i++) {
        smart[128 + i] = (UCHAR)(0x10 + i); /* power-on hours */
        smart[32 + i] = (UCHAR)(0x20 + i);  /* data units read */
        smart[48 + i] = (UCHAR)(0x30 + i);  /* data units written */
        smart[160 + i] = (UCHAR)(0x40 + i); /* media errors */
    }
    memset(page, 0xCC, sizeof(page));
    n = Log2fBuild(smart, page);
    check(n == LOG2F_MAX && n == 75, "the page is 75 bytes");
    check(page[0] == 0x2F && page[1] == 0 && page[2] == 0 && page[3] == 71,
          "header: page 2Fh, page length 71");
    check(page[4] == 0 && page[5] == 0 && page[6] == 0x03 && page[7] == 4,
          "parameter 0000h: binary list, length 4 (SPC's minimum)");
    check(page[8] == 0 && page[9] == 0, "no critical warning: ASC/ASCQ 00h/00h");
    check(page[10] == 37, "temperature 310 K: 37 C in one byte");
    check(page[11] == 0, "the vendor-specific byte of 0000h is 0");
    check(page[12] == 0 && page[13] == 0x01 && page[14] == 0x03 && page[15] == 1 && page[16] == 100,
          "parameter 0001h: available spare");
    check(page[17] == 0 && page[18] == 0x02 && page[20] == 1 && page[21] == 7, "parameter 0002h: percentage used");
    check(page[23] == 0x03 && page[25] == 1 && page[26] == 0, "parameter 0003h: critical warning");
    check(page[28] == 0x04 && page[29] == 0x03 && page[30] == 8 && page[31] == 0x10 && page[38] == 0x17,
          "parameter 0004h: power-on hours, 8 bytes little-endian");
    check(page[40] == 0x05 && page[43] == 0x20, "parameter 0005h: data units read");
    check(page[52] == 0x06 && page[55] == 0x30, "parameter 0006h: data units written");
    check(page[64] == 0x07 && page[67] == 0x40 && page[74] == 0x47, "parameter 0007h: media errors, the last byte");

    smart[0] = 0x04;                        /* reliability degraded */
    Log2fBuild(smart, page);
    check(page[8] == 0x5D && page[9] == 0 && page[26] == 0x04, "a critical warning: 5Dh/00h, and 0003h carries it");

    smart[1] = 0;
    smart[2] = 0;
    Log2fBuild(smart, page);
    check(page[10] == 0xFF, "no temperature reading: FFh");
    smart[1] = 200;
    Log2fBuild(smart, page);
    check(page[10] == 0, "below 0 C: 0");
    smart[1] = (UCHAR)(1000 & 0xFF);
    smart[2] = (UCHAR)(1000 >> 8);
    Log2fBuild(smart, page);
    check(page[10] == 0xFE, "above 254 C: FEh, never the FFh of no reading");

    printf("%s: %d failed checks\n", failures ? "FAIL" : "PASS", failures);
    return failures;
}
