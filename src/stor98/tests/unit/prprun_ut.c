/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, Yeo Kheng Meng
 *
 * prprun_ut - host unit test of the PRP run reuse (src/prprun.h; design
 * record 30 section 13.6, C2): a span wholly inside the run the port
 * stated takes its address by addition; a span that leaves the run, a run
 * shorter than the span, an offset before the run, an empty run and an
 * address that would overflow call the port instead; the run is clamped to
 * the rest of the child. Also replays EngMapCaller's walk of PRP entries
 * over a fake port for whole, fragmented, short and unaligned buffers and
 * checks every entry against the port's own per-page answer and the PRP
 * rules (src/prpchk.h). Built by tests\unit\build.cmd; the exit code is the
 * number of failed checks.
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "prpchk.h"
#include "prprun.h"

#define PG 4096UL

static int failures;

static void check(int ok, const char *what)
{
    printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) {
        failures++;
    }
}

/* A fake port: the child's buffer is made of physical extents; a call at
 * child offset Off asking for Len bytes answers the physical address of
 * that byte and the contiguous bytes from it, at most Len (and at most the
 * rest of the page when PageOnly, as the 9x port for some buffers). */
typedef struct {
    ULONG Bytes;            /* the extent's length */
    ULONGLONG Pa;           /* its physical start */
} EXTENT;

static EXTENT ext[8];
static ULONG nExt;
static int pageOnly;
static int overAnswer;      /* the port answers the whole contiguous run, past what was asked */
static ULONG calls;
static ULONG callerVa = 0x00400000;  /* the caller's linear address of the child's first byte */

static ULONGLONG FakeGpa(ULONG Off, ULONG *Len)
{
    ULONG i, base = 0, run;

    calls++;
    for (i = 0; i < nExt; i++) {
        if (Off < base + ext[i].Bytes) {
            ULONGLONG pa = ext[i].Pa + (Off - base);

            run = base + ext[i].Bytes - Off;
            /* physically adjacent extents merge, as a port's list may */
            while (i + 1 < nExt && ext[i].Pa + ext[i].Bytes == ext[i + 1].Pa) {
                i++;
                run += ext[i].Bytes;
            }
            if (pageOnly && run > PG - (ULONG)(pa & (PG - 1))) {
                run = PG - (ULONG)(pa & (PG - 1));
            }
            *Len = (overAnswer || run < *Len) ? run : *Len;
            return pa;
        }
        base += ext[i].Bytes;
    }
    *Len = 0;
    return 0;
}

/* EngMapCaller's walk (engine.c) with the fake port: fills prp[] with
 * entry 1 and the later entries; returns 0, or the refusal reason. Each
 * entry is then checked against a per-page call of the fake port. */
static ULONG Walk(ULONG Bytes, ULONGLONG *prp, ULONG *nPrp)
{
    ULONG len = Bytes, offsetInPage, firstBytes, pages, i, need, runOff, runLen, avail;
    ULONGLONG pa, runPa, d;

    pa = FakeGpa(0, &len);
    if (len == 0) {
        return 1;
    }
    if (pa == 0) {
        return 3;
    }
    if (!PrpFirstOk((ULONG)pa, callerVa)) {
        return 2;
    }
    offsetInPage = (ULONG)(pa & (PG - 1));
    firstBytes = PG - offsetInPage;
    need = Bytes < firstBytes ? Bytes : firstBytes;
    if (len < need) {
        return 4;
    }
    prp[0] = pa;
    *nPrp = 1;
    runOff = 0;
    runPa = pa;
    runLen = PrpRunClamp(0, len, Bytes);
    if (Bytes <= firstBytes) {
        return 0;
    }
    pages = (offsetInPage + Bytes + PG - 1) / PG;
    for (i = 1; i < pages; i++) {
        ULONG off = firstBytes + (i - 1) * PG;

        need = Bytes - off;
        if (need > PG) {
            need = PG;
        }
        if (PrpRunDerive(runOff, runPa, runLen, off, need, &d, &avail)) {
            pa = d;
            len = avail;
        } else {
            len = Bytes - off;
            pa = FakeGpa(off, &len);
            runOff = off;
            runPa = pa;
            runLen = PrpRunClamp(off, len, Bytes);
        }
        if (len == 0) {
            return 1;
        }
        if (!PrpLaterOk((ULONG)pa)) {
            return 2;
        }
        if (pa == 0 || len < need) {
            return 4;
        }
        prp[(*nPrp)++] = pa;
    }
    return 0;
}

/* Every entry equals the fake port's own answer for that span's first byte
 * (what a per-page call would give). */
static int SameAsPerPage(ULONG Bytes, ULONGLONG *prp, ULONG nPrp)
{
    ULONG i, off, len, firstBytes;
    int savedPageOnly = pageOnly;
    ULONGLONG pa;

    pageOnly = 1;
    len = Bytes;
    pa = FakeGpa(0, &len);
    firstBytes = PG - (ULONG)(pa & (PG - 1));
    if (pa != prp[0]) {
        pageOnly = savedPageOnly;
        return 0;
    }
    for (i = 1; i < nPrp; i++) {
        off = firstBytes + (i - 1) * PG;
        len = Bytes - off;
        if (FakeGpa(off, &len) != prp[i]) {
            pageOnly = savedPageOnly;
            return 0;
        }
    }
    pageOnly = savedPageOnly;
    return 1;
}

static ULONGLONG prp[600];

static void Case(const char *what, ULONG Bytes, ULONG expectCallsMax, ULONG expectCallsMin)
{
    ULONG n = 0, why;
    char text[200];

    calls = 0;
    why = Walk(Bytes, prp, &n);
    sprintf(text, "%s: mapped (%lu calls for %lu entries)", what, calls, n);
    check(why == 0, text);
    sprintf(text, "%s: calls between %lu and %lu", what, expectCallsMin, expectCallsMax);
    check(calls >= expectCallsMin && calls <= expectCallsMax, text);
    sprintf(text, "%s: every entry as a per-page translation gives", what);
    check(why == 0 && SameAsPerPage(Bytes, prp, n), text);
}

int main(void)
{
    ULONGLONG pa;
    ULONG avail;

    printf("prprun_ut\n");

    /* PrpRunDerive */
    check(PrpRunDerive(0, 0x100000, 8 * PG, PG, PG, &pa, &avail) && pa == 0x101000 && avail == 7 * PG,
          "inside the run: address by addition, the rest of the run left");
    check(PrpRunDerive(0, 0x100000, 2 * PG, PG, PG, &pa, &avail) && pa == 0x101000 && avail == PG,
          "a span ending exactly at the run's end is derived");
    check(!PrpRunDerive(0, 0x100000, 2 * PG - 1, PG, PG, &pa, &avail),
          "a run one byte short of the span: call the port");
    check(!PrpRunDerive(0, 0x100000, PG, PG, PG, &pa, &avail), "a span starting at the run's end: call the port");
    check(!PrpRunDerive(2 * PG, 0x100000, 4 * PG, PG, PG, &pa, &avail), "an offset before the run: call the port");
    check(!PrpRunDerive(0, 0x100000, 0, 0, PG, &pa, &avail), "an empty run: call the port");
    check(!PrpRunDerive(0, 0, 8 * PG, PG, PG, &pa, &avail), "a run at physical address 0: call the port");
    check(!PrpRunDerive(0, 0x100000, 8 * PG, PG, 0, &pa, &avail), "an empty span: call the port");
    check(!PrpRunDerive(0, (ULONGLONG)-1 - PG, 8 * PG, 2 * PG, PG, &pa, &avail),
          "an address that would overflow: call the port");
    check(PrpRunDerive(0, 0x100200, 4 * PG, PG, PG, &pa, &avail) && pa == 0x101200 &&
          !PrpLaterOk((ULONG)pa), "a derived address off the page is returned; PrpLaterOk refuses it");
    check(PrpRunDerive(0, 0x100200, 4 * PG, PG - 0x200, PG, &pa, &avail) && pa == 0x101000 && PrpLaterOk((ULONG)pa),
          "an unaligned start: the next span is page aligned by addition");

    /* PrpRunClamp */
    check(PrpRunClamp(0, 16 * PG, 8 * PG) == 8 * PG, "a run longer than the child is clamped to the child");
    check(PrpRunClamp(3 * PG, 16 * PG, 8 * PG) == 5 * PG, "and to the rest of the child from its offset");
    check(PrpRunClamp(8 * PG, PG, 8 * PG) == 0, "nothing left of the child: an empty run");
    check(PrpRunClamp(PG, 100, 8 * PG) == 100, "a short answer is kept as it is");

    /* EngMapCaller's walk over the fake port */
    nExt = 1; ext[0].Bytes = 128 * PG; ext[0].Pa = 0x40000000; pageOnly = 0;
    Case("one contiguous run of 128 pages", 128 * PG, 1, 1);
    pageOnly = 1;
    Case("the same buffer, a port stating only the page", 128 * PG, 128, 128);
    pageOnly = 0;
    nExt = 3; ext[0].Bytes = 3 * PG; ext[0].Pa = 0x10000000; ext[1].Bytes = 2 * PG; ext[1].Pa = 0x30000000;
    ext[2].Bytes = 11 * PG; ext[2].Pa = 0x20000000;
    Case("fragmented: runs of 3, 2 and 11 pages", 16 * PG, 3, 3);
    nExt = 2; ext[0].Bytes = 3 * PG; ext[0].Pa = 0x10000000; ext[1].Bytes = 5 * PG; ext[1].Pa = 0x10003000;
    Case("two extents physically adjacent: one run", 8 * PG, 1, 1);
    nExt = 2; ext[0].Bytes = PG - 0x200; ext[0].Pa = 0x50000200; ext[1].Bytes = 7 * PG + 0x200;
    ext[1].Pa = 0x60000000; callerVa = 0x00400200;
    Case("unaligned start, then a run", 8 * PG, 2, 2);
    callerVa = 0x00400000;
    nExt = 1; ext[0].Bytes = 64 * PG; ext[0].Pa = 0x70000000; overAnswer = 1;
    {
        ULONG n = 0, why, k;
        int inside = 1;

        calls = 0;
        why = Walk(8 * PG, prp, &n);
        for (k = 0; k < n; k++) {
            if (prp[k] < 0x70000000 || prp[k] >= 0x70000000 + 8 * PG) {
                inside = 0;
            }
        }
        check(why == 0 && n == 8 && calls == 1 && inside && SameAsPerPage(8 * PG, prp, n),
              "a port answering past the request: the run is clamped to the child, every entry inside it");
    }
    overAnswer = 0;
    nExt = 1; ext[0].Bytes = 8 * PG; ext[0].Pa = 0x70000001;
    {
        ULONG n = 0;

        check(Walk(8 * PG, prp, &n) == 2, "entry 1 not DWORD aligned: refused (2), as EngMapCaller");
    }
    nExt = 1; ext[0].Bytes = 8 * PG; ext[0].Pa = 0;
    {
        ULONG n = 0;

        check(Walk(8 * PG, prp, &n) == 3, "entry 1 at physical address 0: refused (3), as EngMapCaller");
    }
    nExt = 1; ext[0].Bytes = 2 * PG; ext[0].Pa = 0x70000000;
    Case("two pages (PRP2, no list)", 2 * PG, 1, 1);
    nExt = 2; ext[0].Bytes = PG; ext[0].Pa = 0x70000000; ext[1].Bytes = PG; ext[1].Pa = 0x90000000;
    Case("two pages in two runs", 2 * PG, 2, 2);
    nExt = 1; ext[0].Bytes = 512 * PG; ext[0].Pa = ((ULONGLONG)1 << 32) - 0x10000;
    Case("a run crossing 4 GiB", 512 * PG, 1, 1);
    nExt = 2; ext[0].Bytes = 6 * PG + 100; ext[0].Pa = 0x80000000; ext[1].Bytes = PG; ext[1].Pa = 0xA0000000;
    {
        ULONG n = 0, why;

        calls = 0;
        why = Walk(6 * PG + 100 + PG / 2, prp, &n);
        check(why == 4, "a run ending inside a page: that span is called, found short and refused (bounced), as before");
    }

    printf("%d failure(s)\n", failures);
    return failures;
}
