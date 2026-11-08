/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, Yeo Kheng Meng
 *
 * rmw_ut - host unit test of 512-byte sectors on a namespace with larger
 * blocks (src/rmw.h; PLAN "4 KiB-sector namespaces", 1.7). Each case names
 * the rule it checks:
 *
 *   plan     every start offset modulo R and every length up to 3 x R + 1,
 *            at R = 1, 2, 4 and 8: every extent non-negative, the native
 *            range [F, E), the caller's bytes covered exactly once in
 *            buffer order, the shared block with no middle.
 *   model    a model of a namespace in native blocks: reads and writes
 *            carried out as the engine does (edge reads through a bounce
 *            page and copied out; unaligned writes pre-read, patched and
 *            written back; the middle direct), compared with a 512-byte
 *            reference disk: the data, every byte outside the request on
 *            the medium, sentinels around the caller's buffer, the bounce
 *            page's own bounds, the media traffic of PLAN "Media
 *            traffic", and the order: every pre-read before the first
 *            write, each phase's commands in buffer order (the engine
 *            itself is not built here: its paths are traced, not run).
 *   count    the exposed count NSZE << S and its 64-bit refusal.
 *   rc16     READ CAPACITY(16) bytes for PLAN's capacity table (LBPPBE,
 *            lowest aligned LBA 0, last LBA, block length 512).
 *   overlap  the two overlap rules, in both arrival orders.
 *
 * Built by tests\unit\build.cmd; the exit code is the number of failed
 * checks.
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "rmw.h"
#include "capdata.h"

static int failures;

static void note(int ok, const char *what)
{
    printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) {
        failures++;
    }
}

/* ------------------------------------------------------------------ plan */

static int plan_ok(ULONGLONG lba, ULONG count, ULONG shift)
{
    RMW_PLAN p;
    ULONG r = 1UL << shift, b = 512UL << shift, i, covered, at, sum;
    UCHAR seen[64 * 512];
    ULONGLONG first = lba / r, end = (lba + count + r - 1) / r;
    ULONGLONG mfirst = (lba + r - 1) / r, mend = (lba + count) / r;
    ULONG mblocks = (mend > mfirst) ? (ULONG)(mend - mfirst) : 0;

    RmwPlan(lba, count, shift, &p);
    if (p.First != first || p.End != end || p.Edges > 2 || p.MidBlocks != mblocks) {
        return 0;
    }
    if (mblocks != 0 && p.MidLba != mfirst) {
        return 0;
    }
    memset(seen, 0, count * 512);
    sum = 0;
    at = 0;
    /* buffer order: head edge, middle, tail edge */
    for (i = 0; i < p.Edges; i++) {
        const RMW_EDGE *e = &p.Edge[i];

        if (e->Bytes == 0 || e->Bytes % 512 || e->Skip % 512 || e->Skip + e->Bytes > b || e->Bytes >= b) {
            return 0;
        }
        if (e->Lba < first || e->Lba >= end || e->Offset + e->Bytes > count * 512) {
            return 0;
        }
        /* the edge's caller bytes are the ones at that exposed LBA */
        if ((ULONGLONG)e->Lba * r + e->Skip / 512 != lba + e->Offset / 512) {
            return 0;
        }
        for (covered = 0; covered < e->Bytes; covered++) {
            if (seen[e->Offset + covered]++) {
                return 0;
            }
        }
        sum += e->Bytes;
    }
    if (mblocks != 0) {
        if ((ULONGLONG)p.MidLba * r != lba + p.MidOffset / 512) {
            return 0;
        }
        for (covered = 0; covered < mblocks * b; covered++) {
            if (p.MidOffset + covered >= count * 512 || seen[p.MidOffset + covered]++) {
                return 0;
            }
        }
        sum += mblocks * b;
    }
    if (sum != count * 512) {
        return 0;
    }
    /* order: the head edge (if any) at offset 0, the tail edge last */
    if (p.Edges == 2 && !(p.Edge[0].Offset == 0 && p.Edge[1].Offset + p.Edge[1].Bytes == count * 512)) {
        return 0;
    }
    /* the edges are exactly the partly covered blocks */
    at = (lba % r != 0) + ((lba + count) % r != 0);
    if (first + 1 == end && at == 2) {
        at = 1;                 /* shared block */
    }
    if (p.Edges != at) {
        return 0;
    }
    if (shift == 0 && p.Edges != 0) {
        return 0;
    }
    return 1;
}

static void test_plan(void)
{
    ULONG shift, s, n, bad = 0, total = 0;
    RMW_PLAN p;
    char line[96];

    for (shift = 0; shift <= 3; shift++) {
        ULONG r = 1UL << shift;

        for (s = 0; s < r; s++) {
            for (n = 1; n <= 3 * r + 1; n++) {
                total++;
                if (!plan_ok(1000 * r + s, n, shift)) {
                    bad++;
                    printf("    plan bad: shift %lu start %lu count %lu\n", shift, 1000 * r + s, n);
                }
            }
        }
    }
    sprintf(line, "plan: %lu cases at R = 1, 2, 4, 8: extents, coverage, order, edges", total);
    note(bad == 0, line);
    /* the shared block: L = 1, N = 1, R = 8 (the review's case) */
    RmwPlan(1, 1, 3, &p);
    note(p.Edges == 1 && p.MidBlocks == 0 && p.Edge[0].Lba == 0 && p.Edge[0].Skip == 512 && p.Edge[0].Bytes == 512,
         "plan: shared block (L 1, N 1, 4 KiB): one edge, skip 512, no middle");
    /* PLAN's example: a 4 KiB write at exposed LBA 63 touches native 7 and 8 */
    RmwPlan(63, 8, 3, &p);
    note(p.Edges == 2 && p.MidBlocks == 0 && p.Edge[0].Lba == 7 && p.Edge[0].Bytes == 512 &&
         p.Edge[1].Lba == 8 && p.Edge[1].Bytes == 3584 && p.Edge[1].Offset == 512,
         "plan: 4 KiB at LBA 63: edges 7 (512 B) and 8 (3584 B), no middle");
    RmwPlan(2048, 128, 3, &p);
    note(p.Edges == 0 && p.MidLba == 256 && p.MidBlocks == 16 && p.MidOffset == 0,
         "plan: 64 KiB at LBA 2048: aligned, 16 native blocks at 256");
    RmwPlan(0xFFFFFFFFFFFFFFF0ui64, 15, 3, &p);
    note(p.First == 0x1FFFFFFFFFFFFFFEui64 && p.End == 0x2000000000000000ui64 && p.Edges == 1 && p.MidBlocks == 1,
         "plan: top of the 64-bit exposed range does not wrap");
}

/* ----------------------------------------------------------------- model */

#define NBLK        24
#define GUARD       64

static UCHAR disk[NBLK * 4096];     /* the namespace, in native blocks */
static UCHAR ref[NBLK * 4096];      /* the 512-byte reference disk */
static UCHAR bounce[2][4096 + 2 * GUARD];  /* one page per edge, as the engine */
static UCHAR cbuf[NBLK * 4096 + 2 * GUARD];
static ULONG traffic;

static void fill(UCHAR *p, ULONG n, ULONG seed)
{
    ULONG i;

    for (i = 0; i < n; i++) {
        seed = seed * 1103515245UL + 12345UL;
        p[i] = (UCHAR)(seed >> 16);
    }
}

/* The commands the model issued, in order: 'R' or 'W', the caller offset
 * the command serves (edge: its caller bytes' offset; middle: its own). */
static char oplog[8];
static ULONG opoff[8];
static ULONG ops;

static void nv_read(ULONGLONG lba, ULONG blocks, ULONG b, UCHAR *to, ULONG off)
{
    memcpy(to, disk + lba * b, blocks * b);
    traffic += blocks * b;
    if (ops < 8) {
        oplog[ops] = 'R';
        opoff[ops++] = off;
    }
}

static void nv_write(ULONGLONG lba, ULONG blocks, ULONG b, const UCHAR *from, ULONG off)
{
    memcpy(disk + lba * b, from, blocks * b);
    traffic += blocks * b;
    if (ops < 8) {
        oplog[ops] = 'W';
        opoff[ops++] = off;
    }
}

static int guards_ok(const UCHAR *p, ULONG n)
{
    ULONG i;

    for (i = 0; i < n; i++) {
        if (p[i] != 0xA5) {
            return 0;
        }
    }
    return 1;
}

/* The engine's buffer order (EngIssueRwChild): the edge whose caller bytes
 * start the buffer, the middle, the other edge. Issues each piece with op. */
static void in_order(const RMW_PLAN *p, ULONG b, UCHAR *buf, int write)
{
    ULONG i;

    for (i = 0; i < p->Edges; i++) {
        if (p->Edge[i].Offset == 0) {
            if (write) {
                nv_write(p->Edge[i].Lba, 1, b, bounce[i] + GUARD, 0);
            } else {
                nv_read(p->Edge[i].Lba, 1, b, bounce[i] + GUARD, 0);
                memcpy(buf, bounce[i] + GUARD + p->Edge[i].Skip, p->Edge[i].Bytes);
            }
        }
    }
    if (p->MidBlocks) {
        if (write) {
            nv_write(p->MidLba, p->MidBlocks, b, buf + p->MidOffset, p->MidOffset);
        } else {
            nv_read(p->MidLba, p->MidBlocks, b, buf + p->MidOffset, p->MidOffset);
        }
    }
    for (i = 0; i < p->Edges; i++) {
        if (p->Edge[i].Offset != 0) {
            if (write) {
                nv_write(p->Edge[i].Lba, 1, b, bounce[i] + GUARD, p->Edge[i].Offset);
            } else {
                nv_read(p->Edge[i].Lba, 1, b, bounce[i] + GUARD, p->Edge[i].Offset);
                memcpy(buf + p->Edge[i].Offset, bounce[i] + GUARD + p->Edge[i].Skip, p->Edge[i].Bytes);
            }
        }
    }
}

/* One request through the engine's steps. Returns 1 when everything held:
 * the data, the medium outside the request, the guards, the traffic, and
 * the order (a write: every pre-read before the first write; the commands
 * of a phase in buffer order). */
static int model_rw(ULONGLONG lba, ULONG count, ULONG shift, int write, ULONG seed)
{
    RMW_PLAN p;
    ULONG b = 512UL << shift, i, want, firstw;
    UCHAR *buf = cbuf + GUARD;

    RmwPlan(lba, count, shift, &p);
    memset(cbuf, 0xA5, sizeof(cbuf));
    memset(bounce, 0xA5, sizeof(bounce));
    traffic = 0;
    ops = 0;
    if (write) {
        fill(buf, count * 512, seed);
        /* phase 1: the pre-reads */
        for (i = 0; i < p.Edges; i++) {
            nv_read(p.Edge[i].Lba, 1, b, bounce[i] + GUARD, p.Edge[i].Offset);
        }
        /* phase 2: every edge patched before the first write, then the
         * write-backs and the middle in buffer order */
        for (i = 0; i < p.Edges; i++) {
            memcpy(bounce[i] + GUARD + p.Edge[i].Skip, buf + p.Edge[i].Offset, p.Edge[i].Bytes);
        }
        in_order(&p, b, buf, 1);
        memcpy(ref + lba * 512, buf, count * 512);
        want = p.MidBlocks * b + 2 * p.Edges * b;
        firstw = p.Edges;
        for (i = 0; i < ops; i++) {
            if ((i < firstw) != (oplog[i] == 'R')) {
                return 0;
            }
        }
    } else {
        in_order(&p, b, buf, 0);
        if (memcmp(buf, ref + lba * 512, count * 512) != 0) {
            return 0;
        }
        want = (ULONG)(p.End - p.First) * b;
        firstw = 0;
    }
    for (i = 1; i < ops; i++) {
        if (i != firstw && opoff[i] <= opoff[i - 1]) {
            return 0;               /* a phase not in buffer order */
        }
    }
    if (ops != firstw + p.Edges + (p.MidBlocks ? 1 : 0)) {
        return 0;
    }
    for (i = 0; i < 2; i++) {
        if (!guards_ok(bounce[i], GUARD) || !guards_ok(bounce[i] + GUARD + 4096, GUARD)) {
            return 0;
        }
    }
    if (!guards_ok(cbuf, GUARD) || !guards_ok(buf + count * 512, GUARD)) {
        return 0;
    }
    if (memcmp(disk, ref, NBLK * b) != 0) {
        return 0;
    }
    return traffic == want;
}

static void test_model(void)
{
    ULONG shift, s, n, bad, total;
    char line[112];

    for (shift = 0; shift <= 3; shift++) {
        ULONG r = 1UL << shift, b = 512UL << shift;

        fill(disk, NBLK * b, 7 + shift);
        memcpy(ref, disk, NBLK * b);
        bad = total = 0;
        for (s = 0; s < 2 * r; s++) {
            for (n = 1; n <= 3 * r + 1 && (s + n) <= (NBLK - 1) * r; n++) {
                total += 3;
                bad += !model_rw(r + s, n, shift, 1, s * 131 + n);
                bad += !model_rw(r + s, n, shift, 0, 0);
                bad += !model_rw(0, (NBLK * r) - 1, shift, 0, 0);
            }
        }
        /* a write over a written neighbour keeps the neighbour */
        total += 3;
        bad += !model_rw(5 * r + (r > 1), 1, shift, 1, 99);
        bad += !model_rw(5 * r, r, shift, 0, 0);
        bad += !model_rw(0, NBLK * r, shift, 0, 0);
        sprintf(line, "model: R = %lu, %lu reads and writes: data, neighbours, sentinels, traffic, order", r, total);
        note(bad == 0, line);
    }
    /* PLAN's traffic examples on a 4 KiB namespace */
    fill(disk, NBLK * 4096, 3);
    memcpy(ref, disk, NBLK * 4096);
    note(model_rw(63, 8, 3, 1, 5) && traffic == 16384, "model: 4 KiB write at LBA 63 moves 16 KiB");
    note(model_rw(9, 1, 3, 1, 6) && traffic == 8192, "model: 512-byte write moves 8 KiB");
    note(model_rw(64, 8, 3, 1, 7) && traffic == 4096, "model: aligned 4 KiB write moves 4 KiB");
    note(model_rw(63, 8, 3, 0, 0) && traffic == 8192, "model: 4 KiB read at LBA 63 moves 8 KiB (rounded out)");
}

/* ----------------------------------------------------------------- count */

static void test_count(void)
{
    ULONGLONG x;

    note(RmwExposedCount(0x1FFFFFFF, 3, &x) && x == 0xFFFFFFF8ui64, "count: NSZE 0x1FFFFFFF x 8 = 2^32 - 8");
    note(RmwExposedCount(1000, 0, &x) && x == 1000, "count: 512-byte namespace unchanged");
    note(RmwExposedCount(0x1FFFFFFFFFFFFFFFui64, 3, &x) && x == 0xFFFFFFFFFFFFFFF8ui64,
         "count: largest 4 KiB namespace that fits");
    note(!RmwExposedCount(0x2000000000000000ui64, 3, &x) && x == 0, "count: 2^61 4 KiB blocks refused");
    note(!RmwExposedCount(0x8000000000000000ui64, 1, &x), "count: 2^63 1 KiB blocks refused");
    note(RmwExposedCount(0xFFFFFFFFFFFFFFFFui64, 0, &x) && x == 0xFFFFFFFFFFFFFFFFui64, "count: shift 0 never refused");
    note(!RmwExposedCount(1, 4, &x), "count: shift above 3 refused");
}

/* ------------------------------------------------------------------ rc16 */

static ULONGLONG be64(const UCHAR *p)
{
    ULONGLONG v = 0;
    ULONG i;

    for (i = 0; i < 8; i++) {
        v = (v << 8) | p[i];
    }
    return v;
}

static int rc16_is(ULONGLONG nsze, ULONG shift, ULONGLONG last)
{
    UCHAR d[CAP16_LEN];
    ULONGLONG x;
    ULONG i;

    if (!RmwExposedCount(nsze, shift, &x)) {
        return 0;
    }
    Cap16Build(x, 512, shift, d);
    if (be64(d) != last || d[8] != 0 || d[9] != 0 || d[10] != 2 || d[11] != 0) {
        return 0;
    }
    if (d[12] != 0 || d[13] != shift || d[14] != 0 || d[15] != 0) {
        return 0;
    }
    for (i = 16; i < CAP16_LEN; i++) {
        if (d[i] != 0) {
            return 0;
        }
    }
    return 1;
}

static void test_rc16(void)
{
    note(rc16_is(0x1FFFFFFF, 3, 0xFFFFFFF7ui64), "rc16: NSZE 0x1FFFFFFF: last 0xFFFFFFF7, LBPPBE 3");
    note(rc16_is(0x20000000, 3, 0xFFFFFFFFui64), "rc16: NSZE 0x20000000: last 0xFFFFFFFF");
    note(rc16_is(0x20000001, 3, 0x100000007ui64), "rc16: NSZE 0x20000001: last 0x100000007");
    note(rc16_is(0xFFFFFFFF, 0, 0xFFFFFFFEui64), "rc16: 512 B, 2^32 - 1 sectors: last 0xFFFFFFFE, LBPPBE 0");
    note(rc16_is(0x100000001ui64, 0, 0x100000000ui64), "rc16: 512 B, 2^32 + 1 sectors: last 0x100000000");
}

/* --------------------------------------------------------------- overlap */

static RMW_RANGE rng(ULONGLONG lba, ULONG count, ULONG shift, int write)
{
    RMW_PLAN p;
    RMW_RANGE r;
    ULONG i;

    RmwPlan(lba, count, shift, &p);
    memset(&r, 0, sizeof(r));
    r.First = p.First;
    r.End = p.End;
    r.Rmw = (UCHAR)(write && p.Edges != 0);
    r.Edges = (UCHAR)p.Edges;
    for (i = 0; i < p.Edges; i++) {
        r.EdgeLba[i] = p.Edge[i].Lba;
    }
    return r;
}

static void test_overlap(void)
{
    RMW_RANGE a, b;

    a = rng(1, 1, 3, 1);            /* RMW of native 0 */
    b = rng(2, 1, 3, 1);            /* RMW of native 0 */
    note(RmwMustWait(&a, &b) && RmwMustWait(&b, &a), "overlap: two RMWs of one block wait, either order");
    a = rng(0, 8, 3, 1);            /* aligned write of native 0 */
    b = rng(3, 1, 3, 1);
    note(RmwMustWait(&a, &b), "overlap: aligned write then RMW: the RMW waits");
    note(RmwMustWait(&b, &a), "overlap: RMW then aligned write: the write waits");
    a = rng(3, 1, 3, 0);            /* unaligned read of native 0 */
    note(RmwMustWait(&b, &a), "overlap: RMW then a read of its edge block: the read waits");
    note(RmwMustWait(&a, &b), "overlap: a read then an RMW of that block: the RMW waits");
    a = rng(0, 8, 3, 0);
    b = rng(8, 8, 3, 1);
    note(!RmwMustWait(&a, &b) && !RmwMustWait(&b, &a), "overlap: disjoint aligned requests do not wait");
    a = rng(0, 16, 3, 1);           /* aligned write of native 0-1 */
    b = rng(8, 16, 3, 1);           /* aligned write of native 1-2 */
    note(!RmwMustWait(&a, &b) && !RmwMustWait(&b, &a), "overlap: overlapping aligned writes do not wait");
    b = rng(8, 8, 3, 0);            /* aligned read of native 1 */
    note(!RmwMustWait(&a, &b) && !RmwMustWait(&b, &a), "overlap: an aligned read inside an aligned write does not wait");
    a = rng(1, 30, 3, 1);           /* edges 0 and 3, middle 1-2 */
    b = rng(8, 16, 3, 1);           /* aligned write of native 1-2 */
    note(!RmwMustWait(&a, &b) && !RmwMustWait(&b, &a), "overlap: aligned write inside an RMW's middle does not wait");
    b = rng(24, 1, 3, 1);           /* RMW of native 3 */
    note(RmwMustWait(&a, &b) && RmwMustWait(&b, &a), "overlap: RMWs sharing a tail edge wait");
    b = rng(40, 1, 3, 1);
    note(!RmwMustWait(&a, &b) && !RmwMustWait(&b, &a), "overlap: disjoint RMWs do not wait");
    a = rng(3, 1, 3, 0);
    b = rng(5, 1, 3, 0);
    note(!RmwMustWait(&a, &b), "overlap: two unaligned reads never wait");
    a = rng(1, 1, 0, 1);
    b = rng(1, 1, 0, 1);
    note(!RmwMustWait(&a, &b), "overlap: 512-byte namespace: nothing waits");
}

static void test_view(void)
{
    RMW_PLAN pl;
    UCHAR d[CAP16_LEN];

    note(!RmwNativeView(0, 10, 0, 12), "view: the 9x build never shows native sectors");
    note(!RmwNativeView(1, 5, 0, 12) && !RmwNativeView(1, 5, 1, 12) && !RmwNativeView(1, 5, 2, 12),
         "view: 2000, XP and XP x64 / 2003 (5.x): 512-byte sectors");
    note(!RmwNativeView(1, 6, 0, 12) && !RmwNativeView(1, 6, 1, 12), "view: Vista and 7 (6.0, 6.1): 512-byte sectors");
    note(RmwNativeView(1, 6, 2, 12) && RmwNativeView(1, 6, 3, 12) && RmwNativeView(1, 10, 0, 12),
         "view: 8, 8.1 and 10 with a 4 KiB namespace: native sectors");
    note(!RmwNativeView(1, 10, 0, 10) && !RmwNativeView(1, 10, 0, 11), "view: 1 and 2 KiB namespaces: 512-byte sectors on 10");
    note(!RmwNativeView(1, 10, 0, 9), "view: a 512-byte namespace: 512-byte sectors (the same either way)");
    {
        ULONGLONG e = 0;
        ULONGLONG nsze = (ULONGLONG)1 << 61;

        note(!RmwExposedCount(nsze, 3, &e), "view: NSZE 2^61 at LBADS 12 translated does not fit 64 bits");
        note(RmwExposedCount(nsze, 0, &e) && e == nsze, "view: NSZE 2^61 at LBADS 12 native fits");
    }
    RmwPlan(1000, 64, 0, &pl);
    note(pl.Edges == 0 && pl.First == 1000 && pl.End == 1064 && pl.MidLba == 1000 && pl.MidBlocks == 64 && pl.MidOffset == 0,
         "view: shift 0 (native 4096-byte sectors): no edges, the request itself");
    Cap16Build(1000, 4096, 0, d);
    note(d[10] == 0x10 && d[11] == 0x00 && d[13] == 0, "view: RC16 native: 4096 bytes, exponent 0");
    Cap16Build(8000, 512, 3, d);
    note(d[10] == 0x02 && d[11] == 0x00 && d[13] == 3, "view: RC16 translated: 512 bytes, exponent 3");
    note(RmwAtaWord106(0, FALSE) == 0x4000, "ATA word 106: a 512-byte namespace 4000h");
    note(RmwAtaWord106(1, FALSE) == 0x6001 && RmwAtaWord106(2, FALSE) == 0x6002 && RmwAtaWord106(3, FALSE) == 0x6003,
         "ATA word 106: translated 1, 2, 4 KiB 6001h, 6002h, 6003h");
    note(RmwAtaWord106(3, TRUE) == 0x5000, "ATA word 106: native 4 KiB 5000h");
    note(RmwAtaLogicalWords(4096) == 0x0800, "ATA words 117-118: native 4 KiB 0800h 0000h (2048 words)");
    note(RmwAtaLogicalWords(512) == 0, "ATA words 117-118: 512-byte sectors, none (word 106 bit 12 clear)");
}

int main(void)
{
    printf("rmw_ut: 512-byte sectors on larger native blocks (src/rmw.h)\n");
    test_plan();
    test_model();
    test_count();
    test_rc16();
    test_overlap();
    test_view();
    printf("rmw_ut: %d failure(s)\n", failures);
    return failures;
}
