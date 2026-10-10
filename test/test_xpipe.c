/*
 * test_xpipe.c - host vectors for stage 2's pure half (src\xhci_xpipe.c;
 * roadmap-hcd.md task 36.24, design record 13 section 11.6): TDs cut from
 * one wide scatter/gather list of a whole URB.
 *
 * The lists are built here the way hcd_dma.c's mapper builds them - one
 * element per page or part of one, cut at page bounds, in order - over
 * modelled physical layouts (contiguous, scattered, an unaligned start), and
 * every sweep checks the invariants rather than worked bytes: every TD at
 * most 31 elements, every TD but the last a whole number of packets, the
 * TDs together covering the URB once and in order, and the copied per-TD
 * list naming exactly the bytes of the wide list it covers.
 */

#include <stdio.h>
#include <string.h>
#include "../src/xhci_xpipe.h"
#include "test_harness.h"

#define WIDE_MAX    600UL
#define PAGE        4096UL

static USBPORT_SCATTER_GATHER_ELEMENT wide[WIDE_MAX];

static struct {
    USBPORT_SCATTER_GATHER_LIST List;
    USBPORT_SCATTER_GATHER_ELEMENT More[XHCI_XPIPE_TD_ELEMENTS];
} tdList;

/* The physical page behind virtual page v: contiguous, or scattered by a
 * fixed stride so no two neighbours touch. */
static ULONG page_pa(ULONG v, ULONG scattered)
{
    return scattered ? 0x01000000UL + v * 7UL * PAGE + (v % 3UL) * PAGE
                     : 0x00400000UL + v * PAGE;
}

/* The mapper's list for `length` bytes starting `offset` into a page. */
static ULONG build_wide(ULONG offset, ULONG length, ULONG scattered)
{
    ULONG n;
    ULONG done;
    ULONG v;
    ULONG in;
    ULONG piece;

    n = 0;
    done = 0;
    v = 0;
    in = offset;
    while (done < length) {
        piece = PAGE - in;
        if (piece > length - done) {
            piece = length - done;
        }
        wide[n].SgPhysicalAddressLo = page_pa(v, scattered) + in;
        wide[n].SgPhysicalAddressHi = 0;
        wide[n].SgTransferLength = piece;
        wide[n].SgOffset = done;
        n++;
        done += piece;
        v++;
        in = 0;
    }
    return n;
}

/* The physical address of URB byte `b` through the wide list. */
static ULONG wide_pa(ULONG count, ULONG b)
{
    ULONG i;

    for (i = 0; i < count; i++) {
        if (b >= wide[i].SgOffset &&
            b < wide[i].SgOffset + wide[i].SgTransferLength) {
            return wide[i].SgPhysicalAddressLo + (b - wide[i].SgOffset);
        }
    }
    return 0xFFFFFFFFUL;
}

/* Cut a whole URB and check every TD; returns the TD count, 0 on a
 * failure already reported. */
static ULONG sweep(ULONG offset, ULONG length, ULONG mps, ULONG scattered,
                   ULONG maxElements)
{
    XHCI_XPIPE_CURSOR cur;
    XHCI_XPIPE_TD td;
    ULONG count;
    ULONG planned;
    ULONG tds;
    ULONG r;
    ULONG k;
    ULONG b;
    ULONG ok;

    count = build_wide(offset, length, scattered);
    cur.Element = 0;
    cur.Skip = 0;
    planned = 0;
    tds = 0;
    for (;;) {
        r = XhciXpipePlan(wide, count, length, planned, mps, maxElements,
                          &cur, &td);
        if (r == XHCI_XPIPE_DONE) {
            break;
        }
        if (r != XHCI_XPIPE_OK) {
            CHECK_EQ(r, XHCI_XPIPE_OK, "a sweep's cut refused");
            return 0;
        }
        tds++;
        ok = td.Count >= 1 && td.Count <= maxElements && td.Bytes != 0;
        CHECK(ok, "a TD of 1 to max elements, non-empty");
        if (planned + td.Bytes < length) {
            CHECK_EQ(td.Bytes % mps, 0, "a TD not the last ends on a packet");
        }
        CHECK(planned + td.Bytes <= length, "no TD past the URB");
        k = XhciXpipeBuild(wide, count, &td, &tdList.List,
                           XHCI_XPIPE_TD_ELEMENTS);
        CHECK_EQ(k, td.Count, "the TD's own list built");
        if (k != td.Count) {
            return 0;
        }
        /* Every element of the TD's list names the wide list's bytes. */
        ok = 1;
        for (k = 0; k < td.Count; k++) {
            const USBPORT_SCATTER_GATHER_ELEMENT *e = &tdList.List.SgElement[k];
            for (b = 0; b < e->SgTransferLength; b += 511UL) {
                if (e->SgPhysicalAddressLo + b !=
                    wide_pa(count, planned + e->SgOffset + b)) {
                    ok = 0;
                }
            }
            if (e->SgPhysicalAddressLo + e->SgTransferLength - 1UL !=
                wide_pa(count, planned + e->SgOffset +
                                   e->SgTransferLength - 1UL)) {
                ok = 0;
            }
            if (k + 1 < td.Count &&
                e->SgOffset + e->SgTransferLength !=
                    tdList.List.SgElement[k + 1].SgOffset) {
                ok = 0;
            }
            if (((e->SgPhysicalAddressLo & (PAGE - 1UL)) +
                 e->SgTransferLength) > PAGE) {
                ok = 0;     /* an element crossing a page: two TRBs */
            }
        }
        CHECK(ok, "the TD's list tiles its bytes, page by page");
        planned += td.Bytes;
    }
    CHECK_EQ(planned, length, "the TDs cover the URB once");
    return tds;
}

static void test_counts(void)
{
    /* 31 page-aligned elements carry 124 KiB, so 1 MiB is 9 TDs, 2 MiB 17. */
    CHECK_EQ(sweep(0, 0x100000UL, 1024, 0, 31), 9, "1 MiB aligned: 9 TDs");
    CHECK_EQ(sweep(0, 0x200000UL, 1024, 0, 31), 17, "2 MiB aligned: 17 TDs");
    CHECK_EQ(sweep(512, 0x100000UL, 1024, 1, 31), 9,
             "1 MiB at 512 bytes into a page: 9 TDs");
    CHECK_EQ(sweep(512, 0x200000UL, 1024, 1, 31), 18,
             "2 MiB unaligned: 18 TDs, a TD started inside a page "
             "holding a little less");
    CHECK_EQ(sweep(0, 0x10000UL, 1024, 0, 31), 1, "64 KiB is one TD");
    CHECK_EQ(sweep(0, 31UL * PAGE, 1024, 0, 31), 1, "124 KiB fits one TD");
    CHECK_EQ(sweep(0, 31UL * PAGE + 1, 1024, 0, 31), 2,
             "124 KiB and a byte: two");
}

static void test_sweeps(void)
{
    static const ULONG offs[] = { 0, 1, 511, 512, 2048, 4095 };
    static const ULONG lens[] = { 0x10000UL, 0x11000UL, 0x1F000UL,
                                  0x100000UL, 0x100000UL - 3, 0x200000UL,
                                  0x1FF000UL + 7 };
    static const ULONG mpss[] = { 512, 1024 };
    /* Not 1: a TD of one element that starts inside it, after a
     * packet-aligned cut, can hold less than a packet (NO_PACKET,
     * test_cuts); the driver cuts 31. */
    static const ULONG maxes[] = { 2, 3, 30, 31 };
    ULONG a;
    ULONG b;
    ULONG c;
    ULONG d;
    ULONG s;

    for (a = 0; a < sizeof(offs) / sizeof(offs[0]); a++) {
        for (b = 0; b < sizeof(lens) / sizeof(lens[0]); b++) {
            for (c = 0; c < sizeof(mpss) / sizeof(mpss[0]); c++) {
                for (d = 0; d < sizeof(maxes) / sizeof(maxes[0]); d++) {
                    for (s = 0; s < 2; s++) {
                        (VOID)sweep(offs[a], lens[b], mpss[c], s, maxes[d]);
                    }
                }
            }
        }
    }
}

static void test_cuts(void)
{
    XHCI_XPIPE_CURSOR cur;
    XHCI_XPIPE_TD td;
    ULONG count;

    /* A packet that cuts an element: 31 elements of a list starting 512
     * bytes into a page hold 30.875 pages; the TD ends on 1024, inside
     * element 30, and the next starts there. */
    count = build_wide(512, 0x100000UL, 0);
    cur.Element = 0;
    cur.Skip = 0;
    CHECK_EQ(XhciXpipePlan(wide, count, 0x100000UL, 0, 1024, 31, &cur, &td),
             XHCI_XPIPE_OK, "the first cut");
    CHECK_EQ(td.Bytes, 30UL * PAGE + 3072UL, "packet-aligned below 30.875 "
             "pages");
    CHECK_EQ(td.Count, 31, "touching 31 elements");
    CHECK_EQ(cur.Element, 30, "the next starts in element 30");
    CHECK_EQ(cur.Skip, 3584UL, "at the byte the first TD stopped");

    /* One element of less than a packet cannot start a TD that is not the
     * last. */
    count = build_wide(4000, 0x10000UL, 0);
    cur.Element = 0;
    cur.Skip = 0;
    CHECK_EQ(XhciXpipePlan(wide, count, 0x10000UL, 0, 1024, 1, &cur, &td),
             XHCI_XPIPE_NO_PACKET, "96 bytes hold no packet");

    /* Done, and refusals. */
    count = build_wide(0, 0x10000UL, 0);
    cur.Element = 0;
    cur.Skip = 0;
    CHECK_EQ(XhciXpipePlan(wide, count, 0x10000UL, 0x10000UL, 1024, 31, &cur,
                           &td), XHCI_XPIPE_DONE, "nothing left");
    CHECK_EQ(XhciXpipePlan(wide, count, 0x10000UL, 0, 0, 31, &cur, &td),
             XHCI_XPIPE_BAD_LIST, "no packet size");
    CHECK_EQ(XhciXpipePlan(wide, count, 0x10000UL, 0, 1024, 32, &cur, &td),
             XHCI_XPIPE_BAD_LIST, "more than 31 elements a TD");
    CHECK_EQ(XhciXpipePlan(wide, count - 1, 0x10000UL, 0, 1024, 31, &cur,
                           &td), XHCI_XPIPE_BAD_LIST, "a list short of the URB");
    cur.Element = count;
    CHECK_EQ(XhciXpipePlan(wide, count, 0x10000UL, 0, 1024, 31, &cur, &td),
             XHCI_XPIPE_BAD_LIST, "a cursor past the list");
    cur.Element = 0;
    cur.Skip = PAGE;
    CHECK_EQ(XhciXpipePlan(wide, count, 0x10000UL, 0, 1024, 31, &cur, &td),
             XHCI_XPIPE_BAD_LIST, "a cursor past its element");
    cur.Skip = 0;
    wide[3].SgTransferLength = 0;
    CHECK_EQ(XhciXpipePlan(wide, count, 0x10000UL, 0, 1024, 31, &cur, &td),
             XHCI_XPIPE_BAD_LIST, "an empty element");

    /* The build refuses what does not fit. */
    count = build_wide(0, 0x10000UL, 0);
    td.First = 0;
    td.FirstSkip = 0;
    td.Count = 16;
    td.Bytes = 0x10000UL;
    CHECK_EQ(XhciXpipeBuild(wide, count, &td, &tdList.List, 15), 0,
             "no room for 16");
    CHECK_EQ(XhciXpipeBuild(wide, count, &td, &tdList.List, 31), 16,
             "16 built");
    td.Bytes = 0x10001UL;
    CHECK_EQ(XhciXpipeBuild(wide, count, &td, &tdList.List, 31), 0,
             "more bytes than the elements hold");
    td.Bytes = 0x10000UL;
    td.First = 1;
    CHECK_EQ(XhciXpipeBuild(wide, count, &td, &tdList.List, 31), 0,
             "past the list's end");
}

/* ----------------------------------------------------------------------- */
/* Eligibility, promotion, the wide mapper and the register-free check      */
/* ----------------------------------------------------------------------- */

static void test_eligible(void)
{
    const ULONG MB = 0x100000UL;

    CHECK_EQ(XhciXpipeEligible(1, 0, 0, 0x20000UL, MB, 256, 4096, 1),
             XHCI_XPIPE_ELIG_OK, "bulk OUT, 1 MiB, 256 pages");
    CHECK_EQ(XhciXpipeEligible(1, 1, 1, 0x20000UL, MB, 257, 4096, 1),
             XHCI_XPIPE_ELIG_OK, "bulk IN on a stream, unaligned");
    CHECK_EQ(XhciXpipeEligible(0, 0, 0, 0x20000UL, MB, 256, 4096, 1),
             XHCI_XPIPE_ELIG_TYPE, "control, interrupt, isoch");
    CHECK_EQ(XhciXpipeEligible(1, 0, 0, 0x10000UL, 0x10000UL, 16, 4096, 1),
             XHCI_XPIPE_ELIG_ONE_CHUNK, "one chunk: untouched");
    CHECK_EQ(XhciXpipeEligible(1, 1, 0, 0x20000UL, MB, 256, 4096, 1),
             XHCI_XPIPE_ELIG_DIRECTION, "IN without streams: Bulk-Only, "
             "High-Speed UAS");
    CHECK_EQ(XhciXpipeEligible(1, 0, 1, 0x20000UL, 2UL * MB, 513, 4096, 1),
             XHCI_XPIPE_ELIG_OK, "2 MiB at any alignment fits an entry");
    CHECK_EQ(XhciXpipeEligible(1, 0, 1, 0x20000UL, 2UL * MB + 1, 514, 4096,
                               1), XHCI_XPIPE_ELIG_SPAN,
             "a device limit above the value: sequential");
    CHECK_EQ(XhciXpipeEligible(1, 0, 0, 0x20000UL, MB, 256, 255, 1),
             XHCI_XPIPE_ELIG_SPAN, "over the map grant");
    CHECK_EQ(XhciXpipeEligible(1, 0, 0, 0x20000UL, MB, 256, 256, 1),
             XHCI_XPIPE_ELIG_OK, "exactly the grant");
    CHECK_EQ(XhciXpipeEligible(1, 0, 0, 0x20000UL, MB, 256, 4096, 0),
             XHCI_XPIPE_ELIG_NO_ENTRY, "no free entry");

    CHECK_EQ(XhciXpipePromote(1, 0x80001UL, 1), XHCI_XPIPE_PROMOTE,
             "NULL base, the register-free grant, on");
    CHECK_EQ(XhciXpipePromote(0, 0x80001UL, 1), XHCI_XPIPE_PROMOTE_BASE,
             "registers handed: never");
    CHECK_EQ(XhciXpipePromote(1, 16, 1), XHCI_XPIPE_PROMOTE_GRANT,
             "a grant of 16 (2000 PAE, Vista/7 above 4 GB): never, even "
             "with a NULL base");
    CHECK_EQ(XhciXpipePromote(1, 0x80000UL, 1), XHCI_XPIPE_PROMOTE_GRANT,
             "only exactly 0x80001");
    CHECK_EQ(XhciXpipePromote(1, 0x80001UL, 0), XHCI_XPIPE_PROMOTE_OFF,
             "off");
    CHECK_EQ(XhciXpipePromote(0, 16, 0), XHCI_XPIPE_PROMOTE_OFF,
             "off first");
}

static void test_append(void)
{
    USBPORT_SCATTER_GATHER_ELEMENT el[8];
    ULONG n;

    n = 0;
    CHECK_EQ(XhciXpipeAppendRun(el, &n, 8, 0x1000800UL, 0x2000UL, 0), 1,
             "two pages from the middle of one");
    CHECK_EQ(n, 3, "three elements");
    CHECK_EQ(el[0].SgTransferLength, 0x800UL, "the first to its page end");
    CHECK_EQ(el[1].SgPhysicalAddressLo, 0x1001000UL, "the second a page");
    CHECK_EQ(el[2].SgOffset, 0x1800UL, "offsets run on");
    CHECK_EQ(XhciXpipeAppendRun(el, &n, 8, 0x5000000UL, 0x1000UL, 0x2000UL),
             1, "a scattered page");
    CHECK_EQ(n, 4, "four");
    CHECK_EQ(el[3].SgOffset, 0x2000UL, "at the URB's offset");
    CHECK_EQ(XhciXpipeAppendRun(el, &n, 8, 0x6000000UL, 0x5000UL, 0x3000UL),
             0, "five more do not fit eight");
    CHECK_EQ(n, 4, "untouched");
    CHECK_EQ(XhciXpipeAppendRun(el, &n, 8, 0x6000000UL, 0, 0x3000UL), 0,
             "an empty run");
    CHECK_EQ(XhciXpipeAppendRun(el, &n, 8, 0xFFFFF000UL, 0x2000UL, 0x3000UL),
             0, "a run across 4 GiB");
}

static void test_check(void)
{
    USBPORT_SCATTER_GATHER_ELEMENT a[16];
    USBPORT_SCATTER_GATHER_ELEMENT b[16];
    ULONG na;
    ULONG nb;
    ULONG i;

    na = 0;
    nb = 0;
    for (i = 0; i < 16; i++) {
        (VOID)XhciXpipeAppendRun(a, &na, 16, 0x2000000UL + i * 3UL * PAGE,
                                 PAGE, i * PAGE);
        (VOID)XhciXpipeAppendRun(b, &nb, 16, 0x2000000UL + i * 3UL * PAGE,
                                 PAGE, i * PAGE);
    }
    CHECK_EQ(XhciXpipeCheckCompare(a, na, 1, b, nb, 1, 16UL * PAGE),
             XHCI_XPIPE_CHECK_PASS, "the same 16 pages, both NULL");
    CHECK_EQ(XhciXpipeCheckCompare(a, na, 0, b, nb, 1, 16UL * PAGE),
             XHCI_XPIPE_CHECK_BASE, "the 1-register request got a base");
    CHECK_EQ(XhciXpipeCheckCompare(a, na, 1, b, nb, 0, 16UL * PAGE),
             XHCI_XPIPE_CHECK_BASE, "the ordinary one did");
    CHECK_EQ(XhciXpipeCheckCompare(a, 15, 1, b, nb, 1, 16UL * PAGE),
             XHCI_XPIPE_CHECK_SHORT, "a list one page short");
    b[7].SgPhysicalAddressLo += PAGE;
    CHECK_EQ(XhciXpipeCheckCompare(a, na, 1, b, nb, 1, 16UL * PAGE),
             XHCI_XPIPE_CHECK_DIFFER, "a page elsewhere: a bounce");
    b[7].SgPhysicalAddressLo -= PAGE;
    b[3].SgOffset++;
    CHECK_EQ(XhciXpipeCheckCompare(a, na, 1, b, nb, 1, 16UL * PAGE),
             XHCI_XPIPE_CHECK_SHORT, "a list that does not tile");
}

/* ----------------------------------------------------------------------- */
/* The record machine                                                       */
/* ----------------------------------------------------------------------- */

#define ST_OK       0L
#define ST_ERR      ((LONG)0xC0000004L)    /* a stall                     */
#define ST_HC       ((LONG)0x80000800L)    /* the engine's verdict        */
#define ST_CAN      ((LONG)0x00010000L)
#define ST_SHORTX   ((LONG)0xC0000012L)    /* the short the client refused */

static ULONG finals;

static ULONG ret(PXHCI_XPIPE_REC r, ULONG moved, LONG status)
{
    ULONG f;

    f = XhciXpipeRecRetire(r, r->Retired & 1UL, moved, status,
                           status == ST_CAN);
    if (f & XHCI_XPIPE_F_FINAL) {
        finals++;
    }
    return f;
}

static ULONG pub(PXHCI_XPIPE_REC r, ULONG bytes)
{
    return XhciXpipeRecPublish(r, bytes);
}

static void test_rec_flow(void)
{
    XHCI_XPIPE_REC r;
    ULONG f;

    /* In order, with refill, 3 TDs of 100. */
    finals = 0;
    XhciXpipeRecInit(&r, 300, XHCI_XPIPE_MAP_IN_ROUTINE);
    CHECK_EQ(pub(&r, 100), 0, "TD 0 in slot 0");
    CHECK_EQ(pub(&r, 100), 1, "TD 1 in slot 1");
    CHECK_EQ(pub(&r, 100), XHCI_XPIPE_NO_SLOT, "a third waits for room");
    f = ret(&r, 100, ST_OK);
    CHECK(f & XHCI_XPIPE_F_REFILL, "a full TD with bytes left refills");
    CHECK(f & XHCI_XPIPE_F_OVERLAP, "the next was on the ring");
    CHECK(!(f & XHCI_XPIPE_F_FINAL), "not final");
    CHECK_EQ(pub(&r, 100), 0, "the refill reuses slot 0");
    f = ret(&r, 100, ST_OK);
    CHECK(!(f & XHCI_XPIPE_F_REFILL), "all planned: no refill");
    f = ret(&r, 100, ST_OK);
    CHECK(!(f & XHCI_XPIPE_F_FINAL), "the routine has not returned");
    CHECK(!(f & XHCI_XPIPE_F_OVERLAP), "nothing behind the last");
    f = XhciXpipeRecMapState(&r, XHCI_XPIPE_MAP_ALLOCATED);
    CHECK(f & XHCI_XPIPE_F_FINAL, "final once the routine returned");
    CHECK_EQ(r.Committed, 300, "every byte");
    CHECK_EQ(XhciXpipeRecStatus(&r, 0, ST_SHORTX), ST_OK, "success");
    CHECK_EQ(XhciXpipeRecMapState(&r, XHCI_XPIPE_MAP_ALLOCATED), 0,
             "the finalizer fires once");

    /* Both TDs in one pass: the second's retirement right after. */
    finals = 0;
    XhciXpipeRecInit(&r, 200, XHCI_XPIPE_MAP_ALLOCATED);
    (VOID)pub(&r, 100);
    (VOID)pub(&r, 100);
    (VOID)ret(&r, 100, ST_OK);
    (VOID)ret(&r, 100, ST_OK);
    CHECK_EQ(finals, 1, "final after both, once");
    CHECK_EQ(r.Committed, 200, "both counted");

    /* A refill held by a pause or a busy ring: nothing on the ring, bytes
     * left, NoMore clear - not final; published later, it goes on. */
    finals = 0;
    XhciXpipeRecInit(&r, 300, XHCI_XPIPE_MAP_ALLOCATED);
    (VOID)pub(&r, 100);
    f = ret(&r, 100, ST_OK);
    CHECK(f & XHCI_XPIPE_F_REFILL, "wants its refill");
    CHECK_EQ(finals, 0, "held: not final");
    (VOID)pub(&r, 100);
    (VOID)pub(&r, 100);
    (VOID)ret(&r, 100, ST_OK);
    (VOID)ret(&r, 100, ST_OK);
    CHECK_EQ(finals, 1, "released, it finishes");

    /* Order: a retirement with nothing on the ring changes nothing. */
    XhciXpipeRecInit(&r, 100, XHCI_XPIPE_MAP_ALLOCATED);
    CHECK_EQ(XhciXpipeRecRetire(&r, 0, 0, ST_OK, 0), XHCI_XPIPE_F_ORDER,
             "nothing on the ring");
    (VOID)pub(&r, 100);
    f = XhciXpipeRecRetire(&r, 1, 100, ST_OK, 0);
    CHECK(f & XHCI_XPIPE_F_ORDER, "the wrong slot is counted");
    CHECK(f & XHCI_XPIPE_F_FINAL, "and the oldest retired");
}

static void test_rec_early(void)
{
    XHCI_XPIPE_REC r;
    ULONG f;

    /* A short on TD 0 with TD 1 queued. */
    finals = 0;
    XhciXpipeRecInit(&r, 300, XHCI_XPIPE_MAP_ALLOCATED);
    (VOID)pub(&r, 100);
    (VOID)pub(&r, 100);
    f = ret(&r, 40, ST_OK);
    CHECK(f & XHCI_XPIPE_F_SEALED, "the boundary sealed at TD 0");
    CHECK(f & XHCI_XPIPE_F_TRUNCATE, "the successor to the thread");
    CHECK(!(f & XHCI_XPIPE_F_REFILL), "no refill");
    CHECK(r.NoMore && r.Truncating, "NoMore, Truncating");
    CHECK_EQ(r.Limit, 1, "only TD 0 counts");
    CHECK_EQ(pub(&r, 100), XHCI_XPIPE_NO_SLOT, "nothing published after");
    /* The thread takes TD 1 off: retired as truncated, progress 0. */
    f = ret(&r, 0, ST_CAN);
    CHECK(f & XHCI_XPIPE_F_TRUNC_END, "the successor has left");
    CHECK(f & XHCI_XPIPE_F_FINAL, "final");
    CHECK_EQ(r.Committed, 40, "the short's bytes");
    CHECK_EQ(XhciXpipeRecStatus(&r, 1, ST_SHORTX), ST_OK,
             "a short completes as success when allowed");
    CHECK_EQ(XhciXpipeRecStatus(&r, 0, ST_SHORTX), ST_SHORTX,
             "the client's short rule");

    /* The successor already retired when the thread looks: the device fed
     * it - its bytes are past the boundary and never counted. */
    finals = 0;
    XhciXpipeRecInit(&r, 300, XHCI_XPIPE_MAP_ALLOCATED);
    (VOID)pub(&r, 100);
    (VOID)pub(&r, 100);
    (VOID)ret(&r, 40, ST_OK);
    f = ret(&r, 100, ST_OK);
    CHECK(f & XHCI_XPIPE_F_FINAL, "final");
    CHECK_EQ(r.Committed, 40, "TD 1's bytes not counted");
    CHECK_EQ(finals, 1, "once");

    /* An error, and an engine verdict, with the successor queued. */
    XhciXpipeRecInit(&r, 300, XHCI_XPIPE_MAP_ALLOCATED);
    (VOID)pub(&r, 100);
    (VOID)pub(&r, 100);
    f = ret(&r, 60, ST_ERR);
    CHECK(f & XHCI_XPIPE_F_TRUNCATE, "an error hands the successor over");
    (VOID)ret(&r, 0, ST_CAN);
    CHECK_EQ(XhciXpipeRecStatus(&r, 1, ST_SHORTX), ST_ERR, "its status");
    CHECK_EQ(r.Committed, 60, "its bytes");
    XhciXpipeRecInit(&r, 300, XHCI_XPIPE_MAP_ALLOCATED);
    (VOID)pub(&r, 100);
    (VOID)ret(&r, 100, ST_OK);
    (VOID)pub(&r, 100);
    (VOID)pub(&r, 100);
    f = ret(&r, 100, ST_HC);
    CHECK(f & XHCI_XPIPE_F_TRUNCATE, "a verdict on a full TD ends too");
    (VOID)ret(&r, 30, ST_CAN);
    CHECK_EQ(r.Committed, 200, "TD 2's stopped progress not counted");
    CHECK_EQ(XhciXpipeRecStatus(&r, 1, ST_SHORTX), ST_HC, "the verdict");

    /* A cancel racing the early end: the end first, the cancel after. */
    XhciXpipeRecInit(&r, 300, XHCI_XPIPE_MAP_ALLOCATED);
    (VOID)pub(&r, 100);
    (VOID)pub(&r, 100);
    (VOID)ret(&r, 40, ST_OK);
    (VOID)XhciXpipeRecStop(&r, XHCI_XPIPE_OUT_STOP, ST_CAN);
    (VOID)ret(&r, 0, ST_CAN);
    CHECK_EQ(XhciXpipeRecStatus(&r, 1, ST_SHORTX), ST_OK,
             "the end latched first: a short, never cancelled");
    /* The cancel first, the short after. */
    XhciXpipeRecInit(&r, 300, XHCI_XPIPE_MAP_ALLOCATED);
    (VOID)pub(&r, 100);
    (VOID)pub(&r, 100);
    f = XhciXpipeRecStop(&r, XHCI_XPIPE_OUT_STOP, ST_CAN);
    CHECK(!(f & XHCI_XPIPE_F_FINAL), "two still on the ring");
    (VOID)ret(&r, 40, ST_OK);
    (VOID)ret(&r, 0, ST_CAN);
    CHECK_EQ(XhciXpipeRecStatus(&r, 1, ST_SHORTX), ST_CAN,
             "the cancel latched first");
    CHECK_EQ(r.Committed, 40, "with the short's bytes");

    /* A cancel taking two stopped TDs with progress: the first finished
     * nothing whole, so it seals; the second's progress is past it. */
    XhciXpipeRecInit(&r, 400, XHCI_XPIPE_MAP_ALLOCATED);
    (VOID)pub(&r, 100);
    (VOID)ret(&r, 100, ST_OK);
    (VOID)pub(&r, 100);
    (VOID)pub(&r, 100);
    (VOID)XhciXpipeRecStop(&r, XHCI_XPIPE_OUT_STOP, ST_CAN);
    (VOID)ret(&r, 70, ST_CAN);
    f = ret(&r, 0, ST_CAN);
    CHECK(f & XHCI_XPIPE_F_FINAL, "final");
    CHECK_EQ(r.Committed, 170, "TD 0 and TD 1's stopped progress");
    CHECK_EQ(XhciXpipeRecStatus(&r, 1, ST_SHORTX), ST_CAN, "cancelled");
    /* A cancel after everything moved is success. */
    XhciXpipeRecInit(&r, 100, XHCI_XPIPE_MAP_ALLOCATED);
    (VOID)pub(&r, 100);
    (VOID)XhciXpipeRecStop(&r, XHCI_XPIPE_OUT_STOP, ST_CAN);
    (VOID)ret(&r, 100, ST_OK);
    CHECK_EQ(XhciXpipeRecStatus(&r, 0, ST_SHORTX), ST_OK,
             "a stop that moved every byte is success");
}

static void test_rec_prefail(void)
{
    XHCI_XPIPE_REC r;
    ULONG f;

    /* A failed first publication: nothing on the ring, no bytes. */
    finals = 0;
    XhciXpipeRecInit(&r, 300, XHCI_XPIPE_MAP_IN_ROUTINE);
    f = XhciXpipeRecStop(&r, XHCI_XPIPE_OUT_PREFAIL, ST_HC);
    CHECK(!(f & XHCI_XPIPE_F_FINAL), "the routine still runs");
    f = XhciXpipeRecMapState(&r, XHCI_XPIPE_MAP_ALLOCATED);
    CHECK(f & XHCI_XPIPE_F_FINAL, "final as the routine returns");
    CHECK_EQ(XhciXpipeRecStatus(&r, 1, ST_SHORTX), ST_HC, "its status");

    /* A refill that fails after TD 0 completed with TD 1 running: TD 1's
     * bytes still count when it retires. */
    XhciXpipeRecInit(&r, 400, XHCI_XPIPE_MAP_ALLOCATED);
    (VOID)pub(&r, 100);
    (VOID)pub(&r, 100);
    (VOID)ret(&r, 100, ST_OK);
    f = XhciXpipeRecStop(&r, XHCI_XPIPE_OUT_PREFAIL, ST_HC);
    CHECK(!(f & XHCI_XPIPE_F_FINAL), "TD 1 still on the ring");
    CHECK_EQ(r.Limit, 2, "the boundary after the last published");
    f = ret(&r, 100, ST_OK);
    CHECK(f & XHCI_XPIPE_F_FINAL, "final");
    CHECK_EQ(r.Committed, 200, "A's and B's bytes");
    CHECK_EQ(XhciXpipeRecStatus(&r, 1, ST_SHORTX), ST_HC,
             "and the failure's status");

    /* The refill's failure, then its predecessor's short: the device ended
     * the transfer before the failed TD was due, so the short wins. */
    XhciXpipeRecInit(&r, 400, XHCI_XPIPE_MAP_ALLOCATED);
    (VOID)pub(&r, 100);
    (VOID)pub(&r, 100);
    (VOID)ret(&r, 100, ST_OK);
    (VOID)XhciXpipeRecStop(&r, XHCI_XPIPE_OUT_PREFAIL, ST_HC);
    (VOID)ret(&r, 30, ST_OK);
    CHECK_EQ(r.Committed, 130, "the shorter length");
    CHECK_EQ(XhciXpipeRecStatus(&r, 1, ST_SHORTX), ST_OK,
             "the short replaces the failure");
    /* ... and its predecessor's error. */
    XhciXpipeRecInit(&r, 400, XHCI_XPIPE_MAP_ALLOCATED);
    (VOID)pub(&r, 100);
    (VOID)pub(&r, 100);
    (VOID)ret(&r, 100, ST_OK);
    (VOID)XhciXpipeRecStop(&r, XHCI_XPIPE_OUT_PREFAIL, ST_HC);
    (VOID)ret(&r, 10, ST_ERR);
    CHECK_EQ(XhciXpipeRecStatus(&r, 1, ST_SHORTX), ST_ERR,
             "the error replaces the failure");
    CHECK_EQ(r.Committed, 110, "with its bytes");
    /* Review round 1, finding 3: a refill fails with its predecessor still
     * queued, then a cancel; the thread's walk takes the predecessor off
     * with its stopped progress (a removal, not a device's end), so the
     * publication failure stays the status. */
    XhciXpipeRecInit(&r, 400, XHCI_XPIPE_MAP_ALLOCATED);
    (VOID)pub(&r, 100);
    (VOID)pub(&r, 100);
    (VOID)ret(&r, 100, ST_OK);
    (VOID)XhciXpipeRecStop(&r, XHCI_XPIPE_OUT_PREFAIL, ST_HC);
    (VOID)XhciXpipeRecStop(&r, XHCI_XPIPE_OUT_STOP, ST_CAN);
    f = XhciXpipeRecRetire(&r, 1, 30, ST_CAN, 1);
    CHECK(f & XHCI_XPIPE_F_FINAL, "final once taken off");
    CHECK_EQ(r.Committed, 130, "the predecessor's stopped progress counts");
    CHECK_EQ(XhciXpipeRecStatus(&r, 1, ST_SHORTX), ST_HC,
             "the failure stays: a removal is not the device's end");
    /* A removal with nothing latched (a drain at a device's going) gives
     * the drain's status, and hands no successor over. */
    XhciXpipeRecInit(&r, 400, XHCI_XPIPE_MAP_ALLOCATED);
    (VOID)pub(&r, 100);
    (VOID)pub(&r, 100);
    f = XhciXpipeRecRetire(&r, 0, 10, (LONG)0xC0000013L, 1);
    CHECK(!(f & XHCI_XPIPE_F_TRUNCATE), "no hand-off: the drain takes both");
    f = XhciXpipeRecRetire(&r, 1, 0, (LONG)0xC0000013L, 1);
    CHECK(f & XHCI_XPIPE_F_FINAL, "final");
    CHECK_EQ(XhciXpipeRecStatus(&r, 1, ST_SHORTX), (LONG)0xC0000013L,
             "the drain's status");
    CHECK_EQ(r.Committed, 10, "with the bytes moved");

    /* A cancel after a publication failure keeps the failure. */
    XhciXpipeRecInit(&r, 400, XHCI_XPIPE_MAP_ALLOCATED);
    (VOID)pub(&r, 100);
    (VOID)XhciXpipeRecStop(&r, XHCI_XPIPE_OUT_PREFAIL, ST_HC);
    (VOID)XhciXpipeRecStop(&r, XHCI_XPIPE_OUT_STOP, ST_CAN);
    (VOID)ret(&r, 100, ST_OK);
    CHECK_EQ(XhciXpipeRecStatus(&r, 1, ST_SHORTX), ST_HC,
             "the first latched");
}

static void test_rec_mapstates(void)
{
    static const ULONG states[] = {
        XHCI_XPIPE_MAP_NOT_QUEUED, XHCI_XPIPE_MAP_QUEUED,
        XHCI_XPIPE_MAP_IN_ROUTINE, XHCI_XPIPE_MAP_ALLOCATED,
        XHCI_XPIPE_MAP_REFUSED
    };
    XHCI_XPIPE_REC r;
    ULONG i;
    ULONG k;
    ULONG f;
    ULONG fires;

    /* A cancel, and a removal, at each MapState: final at once where the
     * mapping is not in flight, else when it settles - once either way. */
    for (k = 0; k < 2; k++) {
        for (i = 0; i < sizeof(states) / sizeof(states[0]); i++) {
            XhciXpipeRecInit(&r, 300, states[i]);
            fires = 0;
            f = XhciXpipeRecStop(&r, XHCI_XPIPE_OUT_STOP,
                                 k ? (LONG)0xC0000013L : ST_CAN);
            if (f & XHCI_XPIPE_F_FINAL) {
                fires++;
            }
            if (states[i] == XHCI_XPIPE_MAP_QUEUED) {
                CHECK(!(f & XHCI_XPIPE_F_FINAL), "queued: the pump's");
                f = XhciXpipeRecMapState(&r, XHCI_XPIPE_MAP_IN_ROUTINE);
                CHECK(!(f & XHCI_XPIPE_F_FINAL), "in its routine");
                f = XhciXpipeRecMapState(&r, XHCI_XPIPE_MAP_ALLOCATED);
                if (f & XHCI_XPIPE_F_FINAL) {
                    fires++;
                }
            } else if (states[i] == XHCI_XPIPE_MAP_IN_ROUTINE) {
                /* Found by the routine before its first MapTransfer, or a
                 * mapping that failed part-way: it returns allocated. */
                CHECK(!(f & XHCI_XPIPE_F_FINAL), "the routine's");
                f = XhciXpipeRecMapState(&r, XHCI_XPIPE_MAP_ALLOCATED);
                if (f & XHCI_XPIPE_F_FINAL) {
                    fires++;
                }
            }
            CHECK_EQ(fires, 1, "the finalizer fires exactly once");
            f = XhciXpipeRecMapState(&r, XHCI_XPIPE_MAP_ALLOCATED);
            CHECK(!(f & XHCI_XPIPE_F_FINAL), "and never again");
            CHECK_EQ(XhciXpipeRecStatus(&r, 1, ST_SHORTX),
                     k ? (LONG)0xC0000013L : ST_CAN, "the stop's status");
        }
    }

    /* A refusal before the routine: nothing acquired. */
    XhciXpipeRecInit(&r, 300, XHCI_XPIPE_MAP_QUEUED);
    (VOID)XhciXpipeRecStop(&r, XHCI_XPIPE_OUT_PREFAIL, ST_HC);
    f = XhciXpipeRecMapState(&r, XHCI_XPIPE_MAP_REFUSED);
    CHECK(f & XHCI_XPIPE_F_FINAL, "refused: final");
}

/* A whole URB through the planner and the machine, with the early end
 * placed at every TD: the finalizer once, the length the TDs before the
 * boundary moved. */
static void test_rec_sweep(void)
{
    XHCI_XPIPE_CURSOR cur;
    XHCI_XPIPE_TD td;
    XHCI_XPIPE_REC r;
    ULONG count;
    ULONG endAt;
    ULONG want;
    ULONG f;
    ULONG fires;
    ULONG n;
    ULONG length;
    ULONG guard;

    length = 0x100000UL;
    count = build_wide(512, length, 1);
    for (endAt = 0; endAt <= 10; endAt++) {
        cur.Element = 0;
        cur.Skip = 0;
        XhciXpipeRecInit(&r, length, XHCI_XPIPE_MAP_ALLOCATED);
        fires = 0;
        want = 0;
        n = 0;
        guard = 0;
        while (guard++ < 100) {
            while (XhciXpipeOnRing(&r) < 2 && !r.NoMore &&
                   r.Planned < length) {
                if (XhciXpipePlan(wide, count, length, r.Planned, 1024, 31,
                                  &cur, &td) != XHCI_XPIPE_OK) {
                    break;
                }
                (VOID)XhciXpipeRecPublish(&r, td.Bytes);
            }
            if (XhciXpipeOnRing(&r) == 0) {
                break;
            }
            if (n == endAt) {
                f = XhciXpipeRecRetire(&r, r.Retired & 1UL, 1000, ST_OK, 0);
                want += 1000;
            } else if (n > endAt) {
                f = XhciXpipeRecRetire(&r, r.Retired & 1UL, 0, ST_CAN, 1);
            } else {
                want += r.TdBytes[r.Retired & 1UL];
                f = XhciXpipeRecRetire(&r, r.Retired & 1UL,
                                       r.TdBytes[r.Retired & 1UL], ST_OK, 0);
            }
            if (f & XHCI_XPIPE_F_FINAL) {
                fires++;
            }
            n++;
        }
        CHECK_EQ(fires, 1, "the sweep's finalizer once");
        CHECK_EQ(r.Committed, endAt < 9 ? want : length,
                 "the length before the boundary");
        CHECK(r.Planned <= length, "never planned past the URB");
    }
}

int main(void)
{
    test_counts();
    test_sweeps();
    test_cuts();
    test_eligible();
    test_append();
    test_check();
    test_rec_flow();
    test_rec_early();
    test_rec_prefail();
    test_rec_mapstates();
    test_rec_sweep();
    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
