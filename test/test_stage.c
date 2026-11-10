/*
 * test_stage.c - host vectors for staging's pure half (src\xhci_stage.c;
 * task 48.33, design record 13 section 11.11): the shared pool's size from
 * XhciStagePoolKiB, the elements of a run of staging cut at 64 KB lines, a
 * chunk's length from its room, an isochronous share, the boot ledger's
 * cap and the page rounding.
 *
 * The run sweeps check invariants rather than worked bytes: the elements
 * tile the run in order from the TD offset given, none crosses a 64 KB
 * line, none is empty, and their count is the lines crossed plus one.
 */

#include <stdio.h>
#include <string.h>
#include "../src/xhci_stage.h"
#include "test_harness.h"

#define EL_MAX      40UL

static USBPORT_SCATTER_GATHER_ELEMENT el[EL_MAX];

static void test_pool(void)
{
    CHECK_EQ(XhciStagePoolSlabs(0, 0), 32UL, "absent: 2048 KiB, 32 slabs");
    CHECK_EQ(XhciStagePoolSlabs(0, 99999UL), 32UL, "absent ignores value");
    CHECK_EQ(XhciStagePoolSlabs(1, 2048UL), 32UL, "2048 KiB");
    CHECK_EQ(XhciStagePoolSlabs(1, 256UL), 4UL, "the minimum");
    CHECK_EQ(XhciStagePoolSlabs(1, 0UL), 4UL, "0 held to the minimum");
    CHECK_EQ(XhciStagePoolSlabs(1, 255UL), 4UL, "below held to it");
    CHECK_EQ(XhciStagePoolSlabs(1, 16384UL), 256UL, "the maximum");
    CHECK_EQ(XhciStagePoolSlabs(1, 16385UL), 256UL, "above held to it");
    CHECK_EQ(XhciStagePoolSlabs(1, 0xFFFFFFFFUL), 256UL, "all ones");
    CHECK_EQ(XhciStagePoolSlabs(1, 300UL), 4UL, "whole slabs only");
    CHECK_EQ(XhciStagePoolSlabs(1, 4095UL), 63UL, "4095 KiB");
}

/* One run's elements, checked whole. */
/* XhciDmaStaging (owner, 2026-11-10): off unless a DWORD other than 0. */
static void test_switch(void)
{
    CHECK_EQ(XhciStagingOn(0, 0), 0UL, "absent: direct, the default");
    CHECK_EQ(XhciStagingOn(0, 7UL), 0UL, "absent ignores value");
    CHECK_EQ(XhciStagingOn(1, 1UL), 1UL, "1: on");
    CHECK_EQ(XhciStagingOn(1, 0UL), 0UL, "0: direct, as 3.4.2.0");
    CHECK_EQ(XhciStagingOn(1, 2UL), 1UL, "2: on");
    CHECK_EQ(XhciStagingOn(1, 0xFFFFFFFFUL), 1UL, "all ones: on");
}

/* The quiesce's reading of the PCI command word under each setting. */
static void test_proof(void)
{
    CHECK_EQ(XhciStageCommandProof(0x0006UL, 1), 0UL, "BME set, staging");
    CHECK_EQ(XhciStageCommandProof(0x0006UL, 0), 0UL, "BME set, direct");
    CHECK_EQ(XhciStageCommandProof(0x0002UL, 1), 1UL, "BME clear, staging");
    CHECK_EQ(XhciStageCommandProof(0x0002UL, 0), 1UL, "BME clear, direct");
    CHECK_EQ(XhciStageCommandProof(0xFFFFUL, 1), 0UL,
             "all ones proves nothing with staging");
    CHECK_EQ(XhciStageCommandProof(0xFFFFUL, 0), 1UL,
             "all ones is off the bus, direct (3.4.2.0)");
    CHECK_EQ(XhciStageCommandProof(0xFFFBUL, 0), 1UL,
             "all but BME: clear, direct");
    CHECK_EQ(XhciStageCommandProof(0x0000UL, 1), 1UL, "zero, staging");
}

static void check_run(ULONG pa, ULONG bytes, ULONG offset, ULONG first)
{
    ULONG n;
    ULONG i;
    ULONG walk;
    ULONG off;
    ULONG want;
    ULONG ok;

    for (i = 0; i < EL_MAX; i++) {
        el[i].SgPhysicalAddressLo = 0xDEADBEEFUL;
        el[i].SgTransferLength = 0xDEADBEEFUL;
    }
    n = first;
    ok = XhciStageRuns(el, &n, EL_MAX, pa, bytes, offset);
    check_impl(ok != 0, "run accepted", __FILE__, __LINE__);
    if (!ok) {
        return;
    }
    want = ((pa & 0xFFFFUL) + bytes - 1UL) / 0x10000UL + 1UL;
    check_eq_impl(n - first, want, "elements: lines crossed plus one",
                  __FILE__, __LINE__);
    walk = pa;
    off = offset;
    for (i = first; i < n; i++) {
        check_impl(el[i].SgPhysicalAddressLo == walk, "in order, no gap",
                   __FILE__, __LINE__);
        check_impl(el[i].SgOffset == off, "offsets run on", __FILE__,
                   __LINE__);
        check_impl(el[i].SgTransferLength != 0, "no empty element",
                   __FILE__, __LINE__);
        check_impl((walk & 0xFFFF0000UL) ==
                       ((walk + el[i].SgTransferLength - 1UL) &
                        0xFFFF0000UL),
                   "no element crosses a 64 KB line", __FILE__, __LINE__);
        check_impl(el[i].SgPhysicalAddressHi == 0, "high half 0", __FILE__,
                   __LINE__);
        walk += el[i].SgTransferLength;
        off += el[i].SgTransferLength;
    }
    check_impl(walk - pa == bytes, "the run covered exactly", __FILE__,
               __LINE__);
    for (i = 0; i < first; i++) {
        check_impl(el[i].SgPhysicalAddressLo == 0xDEADBEEFUL,
                   "earlier elements untouched", __FILE__, __LINE__);
    }
    if (n < EL_MAX) {
        check_impl(el[n].SgPhysicalAddressLo == 0xDEADBEEFUL,
                   "nothing written past the count", __FILE__, __LINE__);
    }
}

static void test_runs(void)
{
    static const ULONG bases[] = {
        0x00100000UL, 0x00101000UL, 0x0010F000UL, 0x0011F400UL,
        0x7FFF0000UL, 0x12345000UL, 0xFFF00000UL
    };
    static const ULONG sizes[] = {
        1UL, 1024UL, 4096UL, 0xFFFFUL, 0x10000UL, 0x10001UL, 0x20000UL,
        0x30000UL - 1UL, 0x40000UL
    };
    USBPORT_SCATTER_GATHER_ELEMENT probe[2];
    ULONG i;
    ULONG j;
    ULONG n;

    for (i = 0; i < sizeof(bases) / sizeof(bases[0]); i++) {
        for (j = 0; j < sizeof(sizes) / sizeof(sizes[0]); j++) {
            check_run(bases[i], sizes[j], 0, 0);
            check_run(bases[i], sizes[j], 0x10000UL * j, 3);
        }
    }
    /* A slab aligned to 64 KB is one element; one that is not, two. */
    n = 0;
    CHECK(XhciStageRuns(el, &n, EL_MAX, 0x00200000UL, 0x10000UL, 0),
          "aligned slab");
    CHECK_EQ(n, 1UL, "aligned slab: one element");
    n = 0;
    CHECK(XhciStageRuns(el, &n, EL_MAX, 0x00203000UL, 0x10000UL, 0),
          "unaligned slab");
    CHECK_EQ(n, 2UL, "unaligned slab: two elements");
    CHECK_EQ(el[0].SgTransferLength, 0xD000UL, "first to the line");
    CHECK_EQ(el[1].SgTransferLength, 0x3000UL, "the rest after it");
    CHECK_EQ(el[1].SgPhysicalAddressLo, 0x00210000UL, "the line itself");
    /* Sixteen unaligned slabs fill thirty-two elements exactly. */
    n = 0;
    for (i = 0; i < 16; i++) {
        CHECK(XhciStageRuns(el, &n, 32, 0x01001000UL + i * 0x20000UL,
                            0x10000UL, i * 0x10000UL),
              "slab into 32");
    }
    CHECK_EQ(n, 32UL, "sixteen unaligned slabs: thirty-two elements");
    /* Refusals, nothing written. */
    n = 0;
    CHECK(!XhciStageRuns(el, &n, EL_MAX, 0x1000UL, 0, 0), "zero bytes");
    CHECK_EQ(n, 0UL, "zero bytes: count unchanged");
    CHECK(!XhciStageRuns(el, &n, EL_MAX, 0xFFFFF000UL, 0x2000UL, 0),
          "wraps the 32-bit space");
    CHECK(XhciStageRuns(el, &n, EL_MAX, 0xFFFFF000UL, 0x1000UL, 0),
          "ends at the top exactly");
    CHECK_EQ(n, 1UL, "the top: one element");
    probe[0].SgPhysicalAddressLo = 0x11111111UL;
    probe[1].SgPhysicalAddressLo = 0x22222222UL;
    n = 1;
    CHECK(!XhciStageRuns(probe, &n, 2, 0x0000F000UL, 0x2000UL, 0),
          "two needed, one free");
    CHECK_EQ(n, 1UL, "refused: count unchanged");
    CHECK_EQ(probe[1].SgPhysicalAddressLo, 0x22222222UL,
             "refused: nothing written");
    n = 3;
    CHECK(!XhciStageRuns(probe, &n, 2, 0x1000UL, 0x10UL, 0),
          "count past cap");
    n = 1;
    CHECK(XhciStageRuns(probe, &n, 2, 0x0000E000UL, 0x2000UL, 0),
          "one needed, one free");
    CHECK_EQ(n, 2UL, "filled to the cap");
}

static void test_chunk(void)
{
    CHECK_EQ(XhciStageChunk(100UL, 1024UL, 512UL, 0), 100UL, "fits whole");
    CHECK_EQ(XhciStageChunk(1024UL, 1024UL, 512UL, 0), 1024UL,
             "fits exactly");
    CHECK_EQ(XhciStageChunk(0x30000UL, 0x10000UL, 512UL, 0), 0x10000UL,
             "a slab of whole packets");
    CHECK_EQ(XhciStageChunk(0x30000UL, 0x10000UL, 1000UL, 0), 65000UL,
             "cut to whole packets");
    CHECK_EQ(XhciStageChunk(0x30000UL, 0x1000UL, 1023UL, 0), 4092UL,
             "a page of 1023-byte packets");
    CHECK_EQ(XhciStageChunk(0x30000UL, 0x10000UL, 512UL, 1), 0UL,
             "a control transfer is never split");
    CHECK_EQ(XhciStageChunk(0x30000UL, 0x10000UL, 0, 0), 0UL, "mps 0");
    CHECK_EQ(XhciStageChunk(0x30000UL, 500UL, 512UL, 0), 0UL,
             "room below one packet");
    CHECK_EQ(XhciStageChunk(5000UL, 4096UL, 512UL, 1), 0UL,
             "control larger than the room");
    CHECK_EQ(XhciStageChunk(0, 4096UL, 512UL, 1), 0UL, "nothing left");
}

static void test_iso(void)
{
    CHECK_EQ(XhciStageIsoShare(8UL, 192UL), 1536UL, "a full-speed stream");
    CHECK_EQ(XhciStageIsoShare(62UL, 1024UL), 63488UL, "pool ring bound");
    CHECK_EQ(XhciStageIsoShare(128UL, 1024UL), 0x20000UL, "page bound");
    CHECK_EQ(XhciStageIsoShare(128UL, 3072UL), 0x20000UL, "held to 128 KiB");
    CHECK_EQ(XhciStageIsoShare(0xFFFFFFFFUL, 4096UL), 0x20000UL,
             "no overflow");
    CHECK_EQ(XhciStageIsoShare(0, 1024UL), 0UL, "no packets");
    CHECK_EQ(XhciStageIsoShare(8UL, 0), 0UL, "zero bandwidth");
    CHECK_EQ(XhciStageIsoShare(8UL, 0x10000UL), 0x20000UL,
             "a slab a packet, held");
    CHECK_EQ(XhciStageIsoShare(1UL, 0x10000UL), 0x10000UL, "one slab");
    CHECK_EQ(XhciStageIsoShare(8UL, 0x10001UL), 0UL, "above a slab");
}

static void test_ledger(void)
{
    ULONG cap;

    cap = XHCI_STAGE_LEDGER_CAP;
    CHECK(XhciStageLedgerFits(0, 0, cap, cap), "the cap exactly");
    CHECK(!XhciStageLedgerFits(0, 0, cap + 1UL, cap), "one past");
    CHECK(XhciStageLedgerFits(cap / 2UL, cap / 4UL, cap / 4UL, cap),
          "live and kept together");
    CHECK(!XhciStageLedgerFits(cap / 2UL, cap / 4UL, cap / 4UL + 1UL, cap),
          "live and kept together, one past");
    CHECK(!XhciStageLedgerFits(cap + 1UL, 0, 0, cap), "live already past");
    CHECK(!XhciStageLedgerFits(cap, 1UL, 0, cap), "kept past what is left");
    CHECK(!XhciStageLedgerFits(0xFFFFFFFFUL, 0xFFFFFFFFUL, 1UL,
                               0xFFFFFFFFUL),
          "no overflow");
    CHECK(XhciStageLedgerFits(0, 0, 0, 0), "nothing into nothing");
}

static void test_pages(void)
{
    CHECK_EQ(XhciStagePages(0), 0UL, "zero");
    CHECK_EQ(XhciStagePages(1UL), 0x1000UL, "one byte");
    CHECK_EQ(XhciStagePages(0x1000UL), 0x1000UL, "a page");
    CHECK_EQ(XhciStagePages(0x1001UL), 0x2000UL, "a page and a byte");
    CHECK_EQ(XhciStagePages(0xFFFFF000UL), 0xFFFFF000UL, "the top page");
    CHECK_EQ(XhciStagePages(0xFFFFF001UL), 0UL, "would wrap");
}

int main(void)
{
    test_pool();
    test_switch();
    test_proof();
    test_runs();
    test_chunk();
    test_iso();
    test_ledger();
    test_pages();
    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
