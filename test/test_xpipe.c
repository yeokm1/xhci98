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

int main(void)
{
    test_counts();
    test_sweeps();
    test_cuts();
    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
