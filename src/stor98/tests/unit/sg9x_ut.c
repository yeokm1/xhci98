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
#define SG9X_ARENA 0x10000UL   /* host addresses */
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

    /* The production classifier and copy (Sg9xClassify, Sg9xCopy) over a fake
       PORT_SRB / IOP / IOR / DCB laid out at the DDK offsets. */
    {
        static ULONG srb[0x50 / 4];         /* SCSI_REQUEST_BLOCK + SrbIopPointer */
        static ULONG iop[0x100 / 4];
        static UCHAR dcb[0x100];
        static STOR98_SGD9X l[4];
        const STOR98_SGD9X *got;
        UCHAR sh;
        unsigned char bb[4096 + 1024];

        for (i = 0; i < sizeof(pool); i++) pool[i] = (unsigned char)(i * 13 + 5);
        l[0].Count = 1; l[0].Linear = base + 6 * 4096 + 512;   /* 512 bytes */
        l[1].Count = 2; l[1].Linear = base + 1 * 4096;         /* 1 KiB elsewhere */
        l[2].Count = 0;
        dcb[0x3C] = 9;
        srb[0x40 / 4] = (ULONG)iop;
        iop[0x20 / 4] = (ULONG)srb;
        iop[0x04 / 4] = (ULONG)dcb;
        iop[(0x64 + 0x1C) / 4] = (ULONG)l;
        iop[(0x64 + 0x08) / 4] = 0;
        check(Sg9xClassify(srb, (void *)l[0].Linear, &got, &sh) == SG9X_LINEAR, "classify: no SG flag -> linear");
        iop[(0x64 + 0x08) / 4] = SG9X_IORF_SG;
        check(Sg9xClassify(srb, (void *)l[0].Linear, &got, &sh) == SG9X_SG && got == l && sh == 9,
              "classify: SG list found, shift 9");
        check(Sg9xClassify(srb, (void *)(l[0].Linear + 4), &got, &sh) == SG9X_UNKNOWN && got == NULL,
              "classify: DataBuffer not the first fragment -> unknown");
        iop[(0x64 + 0x08) / 4] = SG9X_IORF_SG | SG9X_IORF_CHAR;
        check(Sg9xClassify(srb, (void *)l[0].Linear, &got, &sh) == SG9X_SG && sh == 0,
              "classify: IORF_CHAR_COMMAND -> byte counts");
        iop[(0x64 + 0x08) / 4] = SG9X_IORF_SG;
        iop[0x20 / 4] = (ULONG)srb + 4;
        check(Sg9xClassify(srb, (void *)l[0].Linear, &got, &sh) == SG9X_UNKNOWN, "classify: IOP_srb does not point back -> unknown");
        iop[0x20 / 4] = (ULONG)srb;
        srb[0x40 / 4] = 0;
        check(Sg9xClassify(srb, (void *)l[0].Linear, &got, &sh) == SG9X_UNKNOWN, "classify: no IOP -> unknown");
        {
            /* IOS's splitter: a one-entry list at an odd address is accepted */
            static UCHAR odd[96];
            STOR98_SGD9X *ol = (STOR98_SGD9X *)(odd + 0x39);
            srb[0x40 / 4] = (ULONG)iop;
            ol[0].Count = 2; ol[0].Linear = l[1].Linear; ol[1].Count = 0;
            iop[(0x64 + 0x1C) / 4] = (ULONG)ol;
            check(Sg9xClassify(srb, (void *)ol[0].Linear, &got, &sh) == SG9X_SG && got == ol,
                  "classify: odd-address list (IOS big-request splitter) accepted");
            iop[(0x64 + 0x1C) / 4] = (ULONG)l;
        }
        iop[0x04 / 4] = 0x100;
        check(Sg9xClassify(srb, (void *)l[0].Linear, &got, &sh) == SG9X_UNKNOWN, "classify: DCB outside the arena -> unknown");
        iop[0x04 / 4] = (ULONG)dcb;

        /* read direction: bounce -> two discontiguous fragments, from offset 256 */
        for (i = 0; i < sizeof(bb); i++) bb[i] = (unsigned char)(i * 3 + 1);
        check(Sg9xCopy(l, 9, 256, bb, 256 + 1024, TRUE), "copy to caller: 1280 bytes across two fragments");
        check(memcmp(pool + 6 * 4096 + 512 + 256, bb, 256) == 0 && memcmp(pool + 4096, bb + 256, 1024) == 0,
              "copy to caller: bytes land in each fragment");
        /* write direction */
        memset(bb, 0, sizeof(bb));
        check(Sg9xCopy(l, 9, 0, bb, 1536, FALSE) && memcmp(bb, pool + 6 * 4096 + 512, 512) == 0 &&
              memcmp(bb + 512, pool + 4096, 1024) == 0, "copy from caller: both fragments in order");
        /* a span past the list fails after one good fragment, and copies nothing */
        memset(bb, 0x3C, sizeof(bb));
        memcpy(want, pool + 4096, 1024);
        check(!Sg9xCopy(l, 9, 512, bb, 2048, TRUE), "copy past the list refused");
        check(memcmp(pool + 4096, want, 1024) == 0, "refused copy wrote nothing to the caller");
        check(!Sg9xCopy(l, 9, 512, bb, 2048, FALSE) && bb[0] == 0x3C, "refused copy wrote nothing to the bounce");
    }

    printf("%d failure(s)\n", failures);
    return failures;
}
