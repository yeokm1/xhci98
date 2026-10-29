/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, Yeo Kheng Meng
 *
 * xfer_ut - host unit test of the maximum transfer size setting
 * (src/xfertun.h: StorMaxTransferKB on 9x, stormaxtransferkb= on NT; absent,
 * accepted, refused below 64, above the build's maximum, off the 4 KiB step,
 * not a number, trailing garbage), with the strict decimal parsing the
 * driver uses for it (XferParseDec). PLAN "0.4.0.0 plan", owner decision
 * 2026-10-16. Built by tests\unit\build.cmd; the exit code is the number of
 * failed checks.
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "xfertun.h"
#include "dpkeys.h"

static int failures;

static void check(int ok, const char *what)
{
    printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) {
        failures++;
    }
}

/* The driver's path for a DriverParameter field (NT) or a REG_SZ value (9x). */
static ULONG fromTextF(const char *text, BOOLEAN field, ULONG maxKb, ULONG *kb)
{
    ULONG raw = 0;
    BOOLEAN present = (BOOLEAN)(text != NULL);
    BOOLEAN valid = present ? XferParseDec(text, field, &raw) : FALSE;

    return XferTunApply(present, valid, raw, maxKb, kb);
}

/* Both contexts, where the text has no ';' */
static ULONG fromText(const char *text, ULONG maxKb, ULONG *kb)
{
    ULONG kb2 = 0;
    ULONG a = fromTextF(text, FALSE, maxKb, kb);
    ULONG b = fromTextF(text, TRUE, maxKb, &kb2);

    return (a == b && *kb == kb2) ? a : 0xBAD;
}

static void test_apply(ULONG maxKb, const char *name)
{
    ULONG kb = 0xDEAD;
    ULONG why;
    char what[128];

    printf("apply, %s (maximum %lu KiB)\n", name, maxKb);
    why = XferTunApply(FALSE, FALSE, 0, maxKb, &kb);
    sprintf(what, "absent: the build's maximum (%lu)", maxKb);
    check(why == XFER_DEFAULT && kb == maxKb, what);

    why = XferTunApply(TRUE, TRUE, maxKb, maxKb, &kb);
    check(why == XFER_SET && kb == maxKb, "the maximum itself is accepted");
    why = XferTunApply(TRUE, TRUE, 64, maxKb, &kb);
    check(why == XFER_SET && kb == 64, "64 (the minimum) is accepted");
    why = XferTunApply(TRUE, TRUE, 128, maxKb, &kb);
    check(why == XFER_SET && kb == 128, "128 is accepted");
    why = XferTunApply(TRUE, TRUE, 68, maxKb, &kb);
    check(why == XFER_SET && kb == 68, "68 (a multiple of 4) is accepted");
    why = XferTunApply(TRUE, TRUE, maxKb - 4, maxKb, &kb);
    check(why == XFER_SET && kb == maxKb - 4, "the maximum minus 4 is accepted");

    why = XferTunApply(TRUE, TRUE, 60, maxKb, &kb);
    check(why == XFER_REFUSED && kb == maxKb, "60 (below 64) refused, the maximum stands");
    why = XferTunApply(TRUE, TRUE, 0, maxKb, &kb);
    check(why == XFER_REFUSED && kb == maxKb, "0 refused, the maximum stands");
    why = XferTunApply(TRUE, TRUE, maxKb + 4, maxKb, &kb);
    check(why == XFER_REFUSED && kb == maxKb, "the maximum plus 4 refused (never raised)");
    why = XferTunApply(TRUE, TRUE, maxKb * 2, maxKb, &kb);
    check(why == XFER_REFUSED && kb == maxKb, "twice the maximum refused");
    why = XferTunApply(TRUE, TRUE, 130, maxKb, &kb);
    check(why == XFER_REFUSED && kb == maxKb, "130 (not a multiple of 4) refused");
    why = XferTunApply(TRUE, TRUE, 66, maxKb, &kb);
    check(why == XFER_REFUSED && kb == maxKb, "66 (not a multiple of 4) refused");
    why = XferTunApply(TRUE, TRUE, 0xFFFFFFFFUL, maxKb, &kb);
    check(why == XFER_REFUSED && kb == maxKb, "saturated 32-bit value refused");
    why = XferTunApply(TRUE, FALSE, 128, maxKb, &kb);
    check(why == XFER_REFUSED && kb == maxKb, "present but not a number: refused");
}

static void test_text(void)
{
    ULONG kb = 0;

    printf("text (REG_SZ value or DriverParameter field)\n");
    check(fromText(NULL, 512, &kb) == XFER_DEFAULT && kb == 512, "absent: 512 on 9x");
    check(fromText("256", 512, &kb) == XFER_SET && kb == 256, "\"256\" on 9x: 256");
    check(fromText("  128", 256, &kb) == XFER_SET && kb == 128, "leading blanks: 128");
    check(fromTextF("128;storpollmode=1", TRUE, 256, &kb) == XFER_SET && kb == 128, "NT field stops at ';'");
    check(fromTextF("128;garbage", FALSE, 512, &kb) == XFER_REFUSED && kb == 512, "9x REG_SZ \"128;garbage\": refused");
    check(fromTextF("128;", FALSE, 512, &kb) == XFER_REFUSED && kb == 512, "9x REG_SZ \"128;\": refused");
    check(fromText("512", 256, &kb) == XFER_REFUSED && kb == 256, "\"512\" on NT: refused, 256 stands");
    check(fromText("1000", 512, &kb) == XFER_REFUSED && kb == 512, "\"1000\" on 9x: refused");
    check(fromText("130", 512, &kb) == XFER_REFUSED && kb == 512, "\"130\" on 9x: refused");
    check(fromText("", 512, &kb) == XFER_REFUSED && kb == 512, "empty string: refused");
    check(fromText("abc", 512, &kb) == XFER_REFUSED && kb == 512, "\"abc\": refused");
    check(fromText("99999999999", 512, &kb) == XFER_REFUSED && kb == 512, "overflowing digits: refused");
    check(fromText("128.5", 512, &kb) == XFER_REFUSED && kb == 512, "\"128.5\": refused, not truncated");
    check(fromText("128k", 512, &kb) == XFER_REFUSED && kb == 512, "\"128k\": refused");
    check(fromText("128,256", 256, &kb) == XFER_REFUSED && kb == 256, "\"128,256\": refused");
    check(fromText("128 ", 512, &kb) == XFER_SET && kb == 128, "trailing blanks: 128");
    check(fromTextF("128 ;storwriteidlems=10", TRUE, 256, &kb) == XFER_SET && kb == 128, "NT field, blank then ';': 128");
    check(fromText(" ", 512, &kb) == XFER_REFUSED && kb == 512, "blank only: refused");
}

/*
 * NT finds a DriverParameter key anywhere in the string (lifecycle.c,
 * LcFind; platform.c and cmlinknt.c the same way), so no key may occur
 * inside another. The keys come from the header the parsers use
 * (src/dpkeys.h), not from a copy of their list.
 */
static void test_keys(void)
{
    static const char *keys[] = { DPK_ALL };
    size_t i, j, n = sizeof(keys) / sizeof(keys[0]), ok = 1, mine = 0;

    printf("DriverParameter keys\n");
    for (i = 0; i < n; i++) {
        if (strcmp(keys[i], "stormaxtransferkb=") == 0) {
            mine++;
        }
        for (j = 0; j < n; j++) {
            if (i != j && strstr(keys[i], keys[j]) != NULL) {
                printf("    %s contains %s\n", keys[i], keys[j]);
                ok = 0;
            }
        }
    }
    check(mine == 1, "stormaxtransferkb= is in the key list once");
    check((int)ok, "no DriverParameter key occurs inside another");
}

int main(void)
{
    test_apply(2048, "9x");
    test_apply(512, "9x before 2026-10-22");
    test_apply(256, "NT");
    test_text();
    test_keys();
    printf("%s: %d failure(s)\n", failures ? "FAILED" : "passed", failures);
    return failures;
}
