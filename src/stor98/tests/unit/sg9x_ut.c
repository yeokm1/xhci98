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
#include "prpchk.h"
#include <stdlib.h>

static int failures;

static void check(int ok, const char *what)
{
    printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) {
        failures++;
    }
}

static unsigned char pool[8 * 4096];

/* Odd and unaligned caller buffers (xhci98 roadmap 43.79): the PRP rules
   bounce every offset that is not DWORD aligned, and the bounce copy moves
   the bytes in both directions at offsets 1, 2, 3, 511 and page straddles,
   for 512 B to 2 MiB, in 32 KiB children (ENG_BOUNCE_PAGES), through a
   one-fragment byte list and a list of fragments out of order. */
/* Where byte Off of the request lies in the pool, from the layout the test
   builds, not through Sg9xResolve: one fragment at Start, or fragments of
   Fs bytes (the first Fs - Start) placed in reverse order from page Pg - 1. */
static ULONG where(int Shape, ULONG Start, ULONG Fs, ULONG Pg, ULONG Off)
{
    ULONG p = Off + Start;

    if (Shape == 0) {
        return p;
    }
    return (Pg - 1 - p / Fs) * Fs + p % Fs;
}

static void unaligned(void)
{
    static const ULONG offs[6] = { 1, 2, 3, 511, 4094, 4095 };
    static const ULONG sizes[5] = { 512, 4096, 65536, 131072, 2097152 };
    ULONG total = 2097152 + 32768, o, s, k, i, nfr;
    unsigned char *mem = (unsigned char *)malloc(total + 4096);
    unsigned char *ref = (unsigned char *)malloc(total);
    unsigned char *bnc = (unsigned char *)malloc(32768);
    STOR98_SGD9X *fl = (STOR98_SGD9X *)malloc(sizeof(STOR98_SGD9X) * 600);
    STOR98_SGD9X one[2];
    unsigned char *page;
    int ok;

    if (mem == NULL || ref == NULL || bnc == NULL || fl == NULL) {
        check(0, "unaligned: host allocation");
        return;
    }
    page = mem + (4096 - ((ULONG)mem & 4095));
    check(!PrpFirstOk(0x12345001, 0xC0001001) && !PrpFirstOk(0x12345002, 0xC0001002) &&
          !PrpFirstOk(0x12345003, 0xC0001003) && !PrpFirstOk(0x123451FF, 0xC00011FF) &&
          !PrpFirstOk(0x12345FFE, 0xC0001FFE), "PRP1: offsets 1, 2, 3, 511, 4094 refused (bounced)");
    check(PrpFirstOk(0x12345000, 0xC0001000) && PrpFirstOk(0x12345004, 0xC0001004) &&
          PrpFirstOk(0x12345E00, 0xC0001E00), "PRP1: offsets 0, 4, 3584 accepted");
    check(!PrpFirstOk(0x12345004, 0xC0001008), "PRP1: a page offset that moved refused");
    check(PrpLaterOk(0x12346000) && !PrpLaterOk(0x12346001) && !PrpLaterOk(0x12346004),
          "PRP2 and list entries: page aligned only");
    for (o = 0; o < 6; o++) {
        for (s = 0; s < 5; s++) {
            ULONG sz = sizes[s], start = offs[o];
            int dir;
            for (dir = 0; dir < 2; dir++) {
                int shape;
                for (shape = 0; shape < 2; shape++) {
                    ULONG fs = sz > 1048576 ? 16384 : 4096;
                    ULONG pg = (sz + start + fs - 1) / fs;
                    for (i = 0; i < total; i++) page[i] = (unsigned char)(i * 7 + (i >> 12) + 1);
                    memcpy(ref, page, total);
                    if (shape == 0) {
                        one[0].Count = sz; one[0].Linear = (ULONG)page + start;
                        one[1].Count = 0;
                        nfr = 1;
                    } else {
                        /* 4 KiB fragments (16 KiB at 2 MiB, within IOS's 255),
                           the first one start bytes in, in reverse order; byte
                           counts */
                        ULONG left = sz, f = 0;
                        nfr = 0;
                        while (left != 0) {
                            ULONG at = (pg - 1 - f) * fs + (f == 0 ? start : 0);
                            ULONG n = fs - (f == 0 ? start : 0);
                            if (n > left) n = left;
                            fl[nfr].Count = n; fl[nfr].Linear = (ULONG)page + at;
                            nfr++; f++; left -= n;
                        }
                        fl[nfr].Count = 0;
                    }
                    ok = 1;
                    for (k = 0; k < sz && ok; k += 32768) {
                        ULONG n = sz - k < 32768 ? sz - k : 32768;
                        const STOR98_SGD9X *l = shape == 0 ? one : fl;
                        ULONG at, j;
                        if (dir == 0) {
                            /* write: caller -> bounce, checked byte by byte */
                            memset(bnc, 0xEE, 32768);
                            ok = Sg9xCopy(l, 0, k, bnc, n, FALSE);
                            for (j = 0; j < n && ok; j++) {
                                at = where(shape, start, fs, pg, k + j);
                                ok = bnc[j] == page[at];
                            }
                        } else {
                            /* read: bounce -> caller; only the caller's bytes change */
                            for (j = 0; j < n; j++) bnc[j] = (unsigned char)((k + j) ^ 0x5A);
                            ok = Sg9xCopy(l, 0, k, bnc, n, TRUE);
                            for (j = 0; j < n; j++) {
                                ref[where(shape, start, fs, pg, k + j)] = bnc[j];
                            }
                        }
                    }
                    if (ok) {
                        ok = memcmp(page, ref, total) == 0;
                    }
                    if (!ok) {
                        printf("  offset %lu size %lu dir %d shape %d\n", start, sz, dir, shape);
                    }
                    check(ok, dir == 0 ? "bounce copy from an unaligned caller (write)" :
                                         "bounce copy to an unaligned caller (read), nothing else touched");
                }
            }
        }
    }
    free(fl); free(bnc); free(ref); free(mem);
}

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

        /* The counter answer's DCB facts (Sg9xDcbFacts), linear and SG alike */
        {
            ULONG mx = 0, sg = 0, dmd = 0, fl = 0;
            *(ULONG *)(dcb + 0x1C) = 0x00040600;
            *(ULONG *)(dcb + 0x50) = 0x80000;
            dcb[0x77] = 17;
            iop[(0x64 + 0x08) / 4] = SG9X_IORF_DBLBUF;
            check(Sg9xDcbFacts(srb, &mx, &sg, &dmd, &fl) && mx == 0x80000 && sg == 17 && dmd == 0x00040600 &&
                  fl == SG9X_IORF_DBLBUF, "DCB facts on a linear request");
            iop[(0x64 + 0x08) / 4] = SG9X_IORF_SG;
            mx = sg = dmd = fl = 0;
            check(Sg9xDcbFacts(srb, &mx, &sg, &dmd, &fl) && mx == 0x80000 && sg == 17 && fl == SG9X_IORF_SG,
                  "DCB facts on a scatter/gather request");
            iop[0x04 / 4] = 0x100;
            mx = 7;
            check(!Sg9xDcbFacts(srb, &mx, &sg, &dmd, &fl) && mx == 7, "DCB facts: DCB outside the arena -> none");
            iop[0x04 / 4] = (ULONG)dcb;
            iop[0x20 / 4] = (ULONG)srb + 4;
            check(!Sg9xDcbFacts(srb, &mx, &sg, &dmd, &fl), "DCB facts: IOP_srb does not point back -> none");
            iop[0x20 / 4] = (ULONG)srb;
            srb[0x40 / 4] = 0;
            check(!Sg9xDcbFacts(srb, &mx, &sg, &dmd, &fl), "DCB facts: no IOP -> none");
            srb[0x40 / 4] = (ULONG)iop;
            iop[(0x64 + 0x08) / 4] = SG9X_IORF_SG;
        }
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

    unaligned();
    printf("%d failure(s)\n", failures);
    return failures;
}
