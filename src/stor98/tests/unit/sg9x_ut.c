/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, Yeo Kheng Meng
 *
 * sg9x_ut - host unit test of the 9x logical scatter/gather walk
 * (src/sg9x.h, Sg9xResolve): offsets in the first, a middle and the last
 * fragment, fragment edges, 512-byte and 4 KiB block shifts, past the end,
 * a broken list, and a bounce-style copy assembled fragment by fragment from
 * fragments that are not next to each other in memory (the case where
 * DataBuffer + offset was wrong). Built by tests\unit\build.cmd; the exit
 * code is the number of failed checks.
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "sg9x.h"

static int failures;

static void check(int ok, const char *what)
{
    printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) {
        failures++;
    }
}

static unsigned char pool[8 * 4096];

int main(void)
{
    STOR98_SGD9X sg[8];
    ULONG lin, run, off, i, n;
    unsigned char out[3 * 4096 + 512], want[3 * 4096 + 512];
    ULONG base = (ULONG)pool;

    printf("sg9x_ut\n");
    /* three fragments, out of order in memory: 1 sector at +7 pages,
       8 sectors at +0, 16 sectors at +3 pages */
    sg[0].Count = 1;  sg[0].Linear = base + 7 * 4096;
    sg[1].Count = 8;  sg[1].Linear = base;
    sg[2].Count = 16; sg[2].Linear = base + 3 * 4096;
    sg[3].Count = 0;  sg[3].Linear = 0;

    check(Sg9xResolve(sg, 9, 0, &lin, &run) && lin == base + 7 * 4096 && run == 512, "offset 0: first fragment");
    check(Sg9xResolve(sg, 9, 511, &lin, &run) && lin == base + 7 * 4096 + 511 && run == 1, "offset 511: last byte of first");
    check(Sg9xResolve(sg, 9, 512, &lin, &run) && lin == base && run == 4096, "offset 512: second fragment start");
    check(Sg9xResolve(sg, 9, 512 + 4095, &lin, &run) && lin == base + 4095 && run == 1, "end of second");
    check(Sg9xResolve(sg, 9, 512 + 4096, &lin, &run) && lin == base + 3 * 4096 && run == 8192, "third fragment start");
    check(Sg9xResolve(sg, 9, 512 + 4096 + 8191, &lin, &run) && run == 1, "last byte");
    check(!Sg9xResolve(sg, 9, 512 + 4096 + 8192, &lin, &run) && lin == 0 && run == 0, "past the end refused");
    check(!Sg9xResolve(NULL, 9, 0, &lin, &run), "no list refused");
    check(!Sg9xResolve(sg, 17, 0, &lin, &run), "absurd shift refused");

    /* 4 KiB block shift: counts in 4 KiB blocks */
    sg[0].Count = 1; sg[0].Linear = base + 5 * 4096;
    sg[1].Count = 2; sg[1].Linear = base + 1 * 4096;
    sg[2].Count = 0;
    check(Sg9xResolve(sg, 12, 4096, &lin, &run) && lin == base + 4096 && run == 8192, "shift 12: second fragment");

    /* overflow of count << shift refused */
    sg[0].Count = 0x80000000UL; sg[0].Linear = base; sg[1].Count = 0;
    check(!Sg9xResolve(sg, 9, 0, &lin, &run), "count overflow refused");

    /* unterminated list stops at SG9X_MAX_WALK */
    {
        static STOR98_SGD9X big[SG9X_MAX_WALK + 4];
        for (i = 0; i < SG9X_MAX_WALK + 4; i++) { big[i].Count = 1; big[i].Linear = base; }
        check(Sg9xResolve(big, 9, (SG9X_MAX_WALK - 1) * 512, &lin, &run), "entry MAX_WALK-1 found");
        check(!Sg9xResolve(big, 9, SG9X_MAX_WALK * 512, &lin, &run), "walk bounded");
    }

    /* bounce-style assembly across non-adjacent fragments, from a mid-fragment offset */
    for (i = 0; i < sizeof(pool); i++) pool[i] = (unsigned char)(i * 7 + (i >> 9));
    sg[0].Count = 1;  sg[0].Linear = base + 7 * 4096;
    sg[1].Count = 8;  sg[1].Linear = base;
    sg[2].Count = 16; sg[2].Linear = base + 3 * 4096;
    sg[3].Count = 0;
    memcpy(want, pool + 7 * 4096 + 256, 256);
    memcpy(want + 256, pool, 4096);
    memcpy(want + 256 + 4096, pool + 3 * 4096, 8192);
    off = 256; n = 0;
    while (n < 256 + 4096 + 8192 && Sg9xResolve(sg, 9, off, &lin, &run)) {
        ULONG k = run < (256 + 4096 + 8192 - n) ? run : (256 + 4096 + 8192 - n);
        memcpy(out + n, (void *)lin, k);
        off += k; n += k;
    }
    check(n == 256 + 4096 + 8192 && memcmp(out, want, n) == 0, "assembled copy matches the fragments");
    check(memcmp(out + 256, pool + 7 * 4096 + 512, 64) != 0, "linear DataBuffer+offset would differ");

    printf("%d failure(s)\n", failures);
    return failures;
}
