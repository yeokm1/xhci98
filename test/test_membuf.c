/*
 * test_membuf.c - host tests for the controller common-buffer layout.
 *
 * Covers src/xhci_mem.c and the constants in src/xhci.h, which together fix
 * USBPORT_REGISTRATION_PACKET.MiniPortResourcesSize. That number is committed
 * in DriverEntry and can never be revised at StartController time, so a
 * mistake here is not a bug that shows up in a log - it is a controller that
 * refuses to run, or worse, DMA into the wrong page. Checking it costs
 * seconds here and a reboot loop on either guest.
 *
 * Per docs/contributing/design/03-host-unit-tests.md, every expected value below is
 * transcribed by hand from docs/usb-xhci-info/xhci-data-structures.md Table 6-1 and
 * docs/contributing/design/04-controller-common-buffer.md - never produced by the code
 * under test.
 *
 * Build and run:  test\run-host-tests.cmd
 * Exit code = number of failed checks (0 = pass).
 *
 * C89, no framework.
 */

#include <stdio.h>
#include "../src/xhci.h"

static int failures;
static int checks;

#define CHECK(cond, what) check_impl((cond), (what), __LINE__)

static void check_impl(int cond, const char *what, int line)
{
    checks++;
    if (!cond) {
        failures++;
        printf("FAIL %s:%d: %s\n", "test_membuf.c", line, what);
    }
}

#define CHECK_EQ(got, want, what) \
    check_eq_impl((unsigned long)(got), (unsigned long)(want), (what), __LINE__)

static void check_eq_impl(unsigned long got, unsigned long want,
                          const char *what, int line)
{
    checks++;
    if (got != want) {
        failures++;
        printf("FAIL %s:%d: %s (got %lu / 0x%lX, want %lu / 0x%lX)\n",
               "test_membuf.c", line, what, got, got, want, want);
    }
}

/* Independent restatement of the spec rule, not a call into the code. */
static int spans(unsigned long offset, unsigned long size,
                 unsigned long boundary)
{
    if (size == 0) {
        return 0;
    }
    return (offset / boundary) != ((offset + size - 1) / boundary);
}

/* ------------------------------------------------------------------ */
/* 1. The declared size and the region map                             */
/* ------------------------------------------------------------------ */

/*
 * Hand arithmetic, docs/contributing/design/04-controller-common-buffer.md section 4:
 *   4 single-page regions                  4 * 4096  =  16384  (0x04000)
 *   32 device contexts, stride 2048        32 * 2048 =  65536  (0x10000)
 *   32 EP0 rings, stride 1024              32 * 1024 =  32768  (0x08000)
 *   32 pooled non-EP0 rings, stride 1024   32 * 1024 =  32768  (0x08000)
 *   64 scratchpad pages                    64 * 4096 = 262144  (0x40000)
 *                                          total     = 409600  (0x64000)
 */
static void test_declared_size(void)
{
    CHECK_EQ(XHCI_MAX_SLOTS, 32, "declared slot cap");
    CHECK_EQ(XHCI_MAX_SCRATCHPAD, 64, "declared scratchpad cap");
    CHECK_EQ(XHCI_MAX_POOL_RINGS, 32, "declared pooled-ring cap");
    CHECK_EQ(XHCI_MAX_DEVICE_ENDPOINTS, 4, "declared per-device endpoint cap");

    /*
     * The ring and reservation policies (power-of-two counts, the 16-4096
     * event-ring range, segments within one page) are XHCI_C_ASSERTs in
     * xhci.h. Restating those same expressions here would add checks that can
     * never fail - a violation stops the compile, so control never reaches
     * them. What is worth pinning at run time is the other thing design doc 03
     * asks for: the resulting values, hand-transcribed from design doc 04
     * section 4, so a policy-conforming edit that silently moves an object is
     * still caught.
     */
    CHECK_EQ(XHCI_CMD_RING_TRBS, 64, "command ring TRB count");
    CHECK_EQ(XHCI_CMD_RING_BYTES, 1024, "command ring bytes");
    CHECK_EQ(XHCI_EVENT_RING_TRBS, 256, "event ring TRB count");
    CHECK_EQ(XHCI_EVENT_RING_BYTES, 4096, "event ring bytes");
    CHECK_EQ(XHCI_EP0_RING_TRBS, 64, "EP0 ring TRB count");
    CHECK_EQ(XHCI_EP0_RING_STRIDE, 1024, "EP0 ring stride");
    CHECK_EQ(XHCI_POOL_RING_TRBS, 64, "pooled ring TRB count");
    CHECK_EQ(XHCI_POOL_RING_STRIDE, 1024, "pooled ring stride");
    CHECK_EQ(XHCI_DCBAA_RESERVED, 2048, "DCBAA reservation");
    CHECK_EQ(XHCI_SPA_RESERVED, 2048, "scratchpad array reservation");
    CHECK_EQ(XHCI_ERST_RESERVED, 64, "ERST reservation");
    CHECK_EQ(XHCI_INPUT_CONTEXT_RESERVED, 4096, "input context reservation");
    CHECK_EQ(XHCI_DEVICE_CONTEXT_STRIDE, 2048, "device context stride");

    /* Sub-object offsets inside the two packed regions (design doc 04
     * section 4: DCBAA at +0x000 and the array at +0x800; command ring at
     * +0x000 of its page and the ERST at +0x400). */
    CHECK_EQ(XHCI_DCBAA_OFFSET, 0x00000UL, "DCBAA offset");
    CHECK_EQ(XHCI_SPA_OFFSET, 0x00800UL, "scratchpad array offset");
    CHECK_EQ(XHCI_CMD_RING_OFFSET, 0x01000UL, "command ring offset");
    CHECK_EQ(XHCI_ERST_OFFSET, 0x01400UL, "ERST offset");

    CHECK_EQ(XHCI_REGION_SMALL_OBJECTS, 0x00000UL, "small-objects region");
    CHECK_EQ(XHCI_REGION_CMD_RING, 0x01000UL, "command-ring region");
    CHECK_EQ(XHCI_REGION_EVENT_RING, 0x02000UL, "event-ring region");
    CHECK_EQ(XHCI_REGION_INPUT_CONTEXT, 0x03000UL, "input-context region");
    CHECK_EQ(XHCI_REGION_DEVICE_CONTEXTS, 0x04000UL, "device-context region");
    CHECK_EQ(XHCI_REGION_EP0_RINGS, 0x14000UL, "EP0 ring region");
    CHECK_EQ(XHCI_REGION_POOL_RINGS, 0x1C000UL, "pooled ring region");
    CHECK_EQ(XHCI_REGION_SCRATCHPAD_PAGES, 0x24000UL, "scratchpad page region");

    CHECK_EQ(XHCI_HC_RESOURCES_SIZE, 0x64000UL, "MiniPortResourcesSize");
    CHECK_EQ(XHCI_HC_RESOURCES_SIZE, 409600UL, "MiniPortResourcesSize decimal");

    /* Every region boundary is an xHC page boundary. */
    CHECK(XHCI_REGION_DEVICE_CONTEXTS % XHCI_PAGE_SIZE == 0, "dc page aligned");
    CHECK(XHCI_REGION_EP0_RINGS % XHCI_PAGE_SIZE == 0, "ep0 page aligned");
    CHECK(XHCI_REGION_POOL_RINGS % XHCI_PAGE_SIZE == 0, "pool page aligned");
    CHECK(XHCI_REGION_SCRATCHPAD_PAGES % XHCI_PAGE_SIZE == 0, "sp page aligned");
    CHECK(XHCI_HC_RESOURCES_SIZE % XHCI_PAGE_SIZE == 0, "total page multiple");
}

/* ------------------------------------------------------------------ */
/* 2. What usbport actually allocates                                  */
/* ------------------------------------------------------------------ */

/*
 * Read out of both shipping binaries: ROUND_TO_PAGES(BufferLength + 0x30).
 * 409600 + 48 = 409648, which rounds up to 101 pages = 413696. The odd page is
 * the price of not contorting the layout to make room for usbport's header -
 * see the design doc.
 */
static void test_allocation_cost(void)
{
    CHECK_EQ(XHCI_USBPORT_CB_HEADER_BYTES, 48, "usbport header size");

    CHECK_EQ(XhciCommonBufferAllocationBytes(XHCI_HC_RESOURCES_SIZE),
             413696UL, "bytes usbport asks the DMA adapter for");
    CHECK_EQ(XhciCommonBufferAllocationBytes(XHCI_HC_RESOURCES_SIZE) /
             XHCI_PAGE_SIZE, 101UL, "pages usbport asks for");

    /* usbport's own second allocation is 0xFD0 - exactly one page once its
     * 48-byte header is added. That is the corroboration for the +48 rule. */
    CHECK_EQ(XhciCommonBufferAllocationBytes(0xFD0UL), 4096UL,
             "usbport's own 0xFD0 request costs exactly one page");

    CHECK_EQ(XhciCommonBufferAllocationBytes(0), 0UL, "zero request");
    CHECK_EQ(XhciCommonBufferAllocationBytes(1), 4096UL, "1 byte costs a page");
    CHECK_EQ(XhciCommonBufferAllocationBytes(4049UL), 8192UL,
             "4049 crosses into a second page");
    CHECK_EQ(XhciCommonBufferAllocationBytes(4048UL), 4096UL,
             "4048 is the largest single-page request");
    /* Overflow guard rather than a wrapped, plausible-looking small number. */
    CHECK_EQ(XhciCommonBufferAllocationBytes(0xFFFFFFF0UL), 0UL,
             "near-ULONG_MAX request is refused, not wrapped");
}

/* ------------------------------------------------------------------ */
/* 3. Layout computation for the measured fleet controller             */
/* ------------------------------------------------------------------ */

/*
 * ThinkPad E460 (8086:9D2F) and P14s Gen 1 (8086:02ED), both measured
 * by the Phase 0 qualifier: slots=64, scratch=34, csz=32,
 * PAGESIZE register 0x00000001 -> 4096.
 */
static void test_fleet_controller(void)
{
    XHCI_HC_LAYOUT l;
    ULONG off;

    CHECK_EQ(XhciComputeLayout(32, 64, 34, 0x00000001UL, &l), XHCI_LAYOUT_OK,
             "fleet controller accepted");

    CHECK_EQ(l.MaxSlotsEn, 32, "64 hardware slots clamped to the declared cap");
    CHECK_EQ(l.ScratchpadCount, 34, "scratchpad count taken as reported");
    CHECK_EQ(l.ContextSize, 32, "CSZ=0 context size");

    CHECK_EQ(l.DcbaaBytes, 33UL * 8UL, "DCBAA is MaxSlotsEn+1 entries");
    CHECK_EQ(l.ScratchpadArrayBytes, 34UL * 8UL, "scratchpad array bytes");
    CHECK_EQ(l.DeviceContextBytes, 32UL * 32UL, "device context at CSZ=0");
    CHECK_EQ(l.InputContextBytes, 33UL * 32UL, "input context at CSZ=0");
    CHECK_EQ(l.TotalBytes, XHCI_HC_RESOURCES_SIZE, "total is the declared size");

    /* Reserved stride is the CSZ=1 worst case even when CSZ=0 is in force. */
    CHECK_EQ(l.DeviceContextStride, 2048, "stride stays worst-case");

    CHECK_EQ(XhciDeviceContextOffset(&l, 1, &off), XHCI_LAYOUT_OK, "slot 1 dc");
    CHECK_EQ(off, 0x04000UL, "slot 1 device context offset");
    CHECK_EQ(XhciDeviceContextOffset(&l, 32, &off), XHCI_LAYOUT_OK, "slot 32 dc");
    CHECK_EQ(off, 0x04000UL + 31UL * 2048UL, "slot 32 device context offset");

    CHECK_EQ(XhciEp0RingOffset(&l, 1, &off), XHCI_LAYOUT_OK, "slot 1 ep0");
    CHECK_EQ(off, 0x14000UL, "slot 1 EP0 ring offset");
    CHECK_EQ(XhciEp0RingOffset(&l, 32, &off), XHCI_LAYOUT_OK, "slot 32 ep0");
    CHECK_EQ(off, 0x14000UL + 31UL * 1024UL, "slot 32 EP0 ring offset");

    /*
     * The pool is 0-based and is *not* clamped to MaxSlotsEn, so index 31 must
     * resolve on a controller declaring 32 slots and index 32 must refuse. The
     * pair is the point: a pool indexed as if it were a Slot ID would put the
     * last entry one stride past the region, and only the refusal catches it.
     */
    CHECK_EQ(l.PoolRingCount, 32, "pool ring count is the declared cap");
    CHECK_EQ(XhciPoolRingOffset(&l, 0, &off), XHCI_LAYOUT_OK, "pool 0");
    CHECK_EQ(off, 0x1C000UL, "pool ring 0 offset");
    CHECK_EQ(XhciPoolRingOffset(&l, 31, &off), XHCI_LAYOUT_OK, "pool 31");
    CHECK_EQ(off, 0x1C000UL + 31UL * 1024UL, "pool ring 31 offset");
    CHECK(off + 1024UL <= XHCI_REGION_SCRATCHPAD_PAGES,
          "last pool ring stays inside its region");
    CHECK_EQ(XhciPoolRingOffset(&l, 32, &off), XHCI_LAYOUT_BAD_INDEX,
             "pool index 32 refused - the pool is 0-based, not a Slot ID");

    CHECK_EQ(XhciScratchpadPageOffset(&l, 0, &off), XHCI_LAYOUT_OK, "sp 0");
    CHECK_EQ(off, 0x24000UL, "scratchpad page 0 offset");
    CHECK_EQ(XhciScratchpadPageOffset(&l, 33, &off), XHCI_LAYOUT_OK, "sp 33");
    CHECK_EQ(off, 0x24000UL + 33UL * 4096UL, "scratchpad page 33 offset");
    CHECK(off + 4096UL <= XHCI_HC_RESOURCES_SIZE, "last fleet page fits");
}

/* ------------------------------------------------------------------ */
/* 4. Table 6-1 alignment and no-cross-boundary rules                  */
/* ------------------------------------------------------------------ */

/*
 * docs/usb-xhci-info/xhci-data-structures.md section 1, restated here by hand:
 *   DCBAA                 64 B align, must not cross PAGESIZE, max 2048 B
 *   Device Context        64 B align, must not cross PAGESIZE, max 2048 B
 *   Input Context         64 B align, must not cross PAGESIZE
 *   Command Ring segment  64 B align, must not cross 64 KB
 *   Event Ring segment    64 B align, must not cross 64 KB
 *   ERST                  64 B align, no boundary rule
 *   Transfer Ring segment 16 B align, must not cross 64 KB
 *   Scratchpad array      64 B align, must not cross PAGESIZE
 *   Scratchpad pages      PAGESIZE align
 */
static void check_layout_rules(const XHCI_HC_LAYOUT *l, const char *tag)
{
    ULONG i;
    ULONG off;

    CHECK(l->DcbaaOffset % 64 == 0, tag);
    CHECK(!spans(l->DcbaaOffset, l->DcbaaBytes, XHCI_PAGE_SIZE), tag);
    CHECK(l->DcbaaBytes <= 2048, tag);

    CHECK(l->ScratchpadArrayOffset % 64 == 0, tag);
    CHECK(!spans(l->ScratchpadArrayOffset, l->ScratchpadArrayBytes,
                 XHCI_PAGE_SIZE), tag);

    CHECK(l->CommandRingOffset % 64 == 0, tag);
    CHECK(!spans(l->CommandRingOffset, l->CommandRingTrbs * 16, 65536UL), tag);
    CHECK(l->CommandRingTrbs * 16 <= 65536UL, tag);
    CHECK(l->CommandRingOffset + l->CommandRingTrbs * 16 <= l->ErstOffset, tag);

    CHECK(l->ErstOffset % 64 == 0, tag);
    CHECK(l->ErstOffset + XHCI_ERST_RESERVED <= l->EventRingOffset, tag);

    CHECK(l->EventRingOffset % 64 == 0, tag);
    CHECK(!spans(l->EventRingOffset, l->EventRingTrbs * 16, 65536UL), tag);
    CHECK(l->EventRingOffset + l->EventRingTrbs * 16 <=
          l->InputContextOffset, tag);

    CHECK(l->InputContextOffset % 64 == 0, tag);
    CHECK(!spans(l->InputContextOffset, l->InputContextBytes,
                 XHCI_PAGE_SIZE), tag);
    CHECK(l->InputContextOffset + XHCI_INPUT_CONTEXT_RESERVED <=
          l->DeviceContextOffset, tag);

    /* DCBAA and the scratchpad array share a page; likewise. */
    CHECK(l->ScratchpadArrayOffset >= l->DcbaaOffset + l->DcbaaBytes, tag);

    for (i = 1; i <= l->MaxSlotsEn; i++) {
        CHECK(XhciDeviceContextOffset(l, i, &off) == XHCI_LAYOUT_OK, tag);
        CHECK(off % 64 == 0, tag);
        CHECK(!spans(off, l->DeviceContextBytes, XHCI_PAGE_SIZE), tag);
        CHECK(off + l->DeviceContextStride <= XHCI_REGION_EP0_RINGS, tag);

        CHECK(XhciEp0RingOffset(l, i, &off) == XHCI_LAYOUT_OK, tag);
        CHECK(off % 64 == 0, tag);
        CHECK(!spans(off, l->Ep0RingStride, 65536UL), tag);
        CHECK(off + l->Ep0RingStride <= XHCI_REGION_POOL_RINGS, tag);
    }

    /*
     * Bounded by PoolRingCount, deliberately not by MaxSlotsEn - the minimal
     * controller below declares one slot and still exposes all 32 pool entries,
     * so a loop written against MaxSlotsEn would leave 31 of them unchecked on
     * the very layout most likely to expose an arithmetic error.
     */
    for (i = 0; i < l->PoolRingCount; i++) {
        CHECK(XhciPoolRingOffset(l, i, &off) == XHCI_LAYOUT_OK, tag);
        CHECK(off % 64 == 0, tag);
        CHECK(!spans(off, l->PoolRingStride, 65536UL), tag);
        CHECK(off >= XHCI_REGION_POOL_RINGS, tag);
        CHECK(off + l->PoolRingStride <= XHCI_REGION_SCRATCHPAD_PAGES, tag);
    }
    CHECK(XhciPoolRingOffset(l, l->PoolRingCount, &off) ==
          XHCI_LAYOUT_BAD_INDEX, tag);

    for (i = 0; i < l->ScratchpadCount; i++) {
        CHECK(XhciScratchpadPageOffset(l, i, &off) == XHCI_LAYOUT_OK, tag);
        CHECK(off % XHCI_PAGE_SIZE == 0, tag);
        CHECK(off + XHCI_PAGE_SIZE <= l->TotalBytes, tag);
    }
}

static void test_boundary_rules(void)
{
    XHCI_HC_LAYOUT l;

    /* Worst case: 64-byte contexts, every declared slot, every declared page. */
    CHECK_EQ(XhciComputeLayout(64, 255, 64, 0x00000001UL, &l), XHCI_LAYOUT_OK,
             "worst-case controller accepted");
    CHECK_EQ(l.MaxSlotsEn, 32, "255 hardware slots clamped");
    CHECK_EQ(l.DeviceContextBytes, 2048, "device context at CSZ=1 is 2048 B");
    CHECK_EQ(l.InputContextBytes, 2112, "input context at CSZ=1 is 2112 B");
    check_layout_rules(&l, "worst-case layout obeys Table 6-1");

    /* Smallest useful controller. */
    CHECK_EQ(XhciComputeLayout(32, 1, 0, 0x00000001UL, &l), XHCI_LAYOUT_OK,
             "single-slot, no-scratchpad controller accepted");
    CHECK_EQ(l.MaxSlotsEn, 1, "single slot kept");
    CHECK_EQ(l.ScratchpadArrayBytes, 0, "no scratchpad array bytes");
    check_layout_rules(&l, "minimal layout obeys Table 6-1");

    /* QEMU's qemu-xhci shape: contexts small, no scratchpad. */
    CHECK_EQ(XhciComputeLayout(32, 64, 0, 0x00000001UL, &l), XHCI_LAYOUT_OK,
             "QEMU-shaped controller accepted");
    check_layout_rules(&l, "QEMU-shaped layout obeys Table 6-1");
}

/* ------------------------------------------------------------------ */
/* 5. Refusal paths                                                    */
/* ------------------------------------------------------------------ */

static void test_refusals(void)
{
    XHCI_HC_LAYOUT l;

    CHECK_EQ(XhciComputeLayout(48, 64, 0, 0x00000001UL, &l),
             XHCI_LAYOUT_BAD_CONTEXT_SIZE, "context size must be 32 or 64");
    CHECK_EQ(XhciComputeLayout(0, 64, 0, 0x00000001UL, &l),
             XHCI_LAYOUT_BAD_CONTEXT_SIZE, "zero context size refused");

    CHECK_EQ(XhciComputeLayout(32, 64, 0, 0x00000003UL, &l),
             XHCI_LAYOUT_OK, "4 KB selected when multiple sizes supported");
    CHECK_EQ(XhciComputeLayout(32, 64, 0, 0x00000002UL, &l),
             XHCI_LAYOUT_BAD_PAGE_SIZE, "controller without 4 KB refused");
    CHECK_EQ(XhciComputeLayout(32, 64, 0, 0, &l),
             XHCI_LAYOUT_BAD_PAGE_SIZE, "empty PAGESIZE bitmap refused");

    CHECK_EQ(XhciComputeLayout(32, 0, 0, 0x00000001UL, &l),
             XHCI_LAYOUT_NO_SLOTS, "controller with no slots refused");

    /* One past the declared cap is the whole point of the cap. */
    CHECK_EQ(XhciComputeLayout(32, 64, XHCI_MAX_SCRATCHPAD + 1,
                              0x00000001UL, &l),
             XHCI_LAYOUT_TOO_MANY_SCRATCHPAD, "65 scratchpad buffers refused");
    CHECK_EQ(XhciComputeLayout(32, 64, XHCI_MAX_SCRATCHPAD,
                              0x00000001UL, &l),
             XHCI_LAYOUT_OK, "64 scratchpad buffers accepted");
    CHECK_EQ(XhciComputeLayout(32, 64, 1023, 0x00000001UL, &l),
             XHCI_LAYOUT_TOO_MANY_SCRATCHPAD,
             "spec-maximum 1023 scratchpad buffers refused");

    /* Slots clamp, they do not refuse - MaxSlotsEn is ours to choose. */
    CHECK_EQ(XhciComputeLayout(32, 255, 0, 0x00000001UL, &l), XHCI_LAYOUT_OK,
             "255 hardware slots accepted by clamping");
}

static void test_index_bounds(void)
{
    XHCI_HC_LAYOUT l;
    ULONG off = 0xDEADBEEFUL;

    CHECK_EQ(XhciComputeLayout(32, 4, 2, 0x00000001UL, &l), XHCI_LAYOUT_OK,
             "4-slot controller accepted");

    /* Slot IDs are 1-based; 0 is "no slot" and must never index the array. */
    CHECK_EQ(XhciDeviceContextOffset(&l, 0, &off), XHCI_LAYOUT_BAD_INDEX,
             "slot 0 rejected");
    CHECK_EQ(off, 0xDEADBEEFUL, "rejected slot leaves the output untouched");
    CHECK_EQ(XhciDeviceContextOffset(&l, 5, &off), XHCI_LAYOUT_BAD_INDEX,
             "slot past MaxSlotsEn rejected");
    CHECK_EQ(XhciEp0RingOffset(&l, 0, &off), XHCI_LAYOUT_BAD_INDEX,
             "EP0 ring for slot 0 rejected");
    CHECK_EQ(XhciEp0RingOffset(&l, 5, &off), XHCI_LAYOUT_BAD_INDEX,
             "EP0 ring past MaxSlotsEn rejected");
    CHECK_EQ(XhciScratchpadPageOffset(&l, 2, &off), XHCI_LAYOUT_BAD_INDEX,
             "scratchpad page past the reported count rejected");
    CHECK_EQ(off, 0xDEADBEEFUL, "still untouched");

    CHECK_EQ(XhciDeviceContextOffset(&l, 4, &off), XHCI_LAYOUT_OK,
             "last valid slot accepted");
    CHECK_EQ(XhciScratchpadPageOffset(&l, 1, &off), XHCI_LAYOUT_OK,
             "last valid scratchpad page accepted");
}

/* ------------------------------------------------------------------ */
/* 6. The base address usbport hands over                              */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* 6a. The pooled-ring allocator and its fairness guarantee            */
/* ------------------------------------------------------------------ */

/*
 * The property design doc 04 section 3.6 claims, and the reason it needs an
 * admission rule rather than a bigger pool: **no device is ever refused its
 * first ring while a ring is free.**
 *
 * The first draft of that section said the pool *size* guaranteed this. It does
 * not - eight devices at the per-device cap of 4 hold all 32 rings, and the
 * ninth is refused a first endpoint. This vector is the counterexample, run
 * against the rule that fixes it: the greedy devices must be held to one ring
 * each until every other live device has had its chance.
 */
static void test_pool_first_ring_guarantee(void)
{
    XHCI_RING_POOL pool;
    ULONG idx;
    ULONG dev;
    ULONG live;
    ULONG granted;

    XhciPoolInit(&pool);
    CHECK_EQ(XhciPoolFree(&pool), 32UL, "fresh pool has every ring free");
    CHECK_EQ(pool.InUse, 0UL, "fresh pool holds nothing");

    /*
     * 32 live devices, each asking for a second ring before the others have a
     * first. `ringless` is the number of *other* live records holding nothing.
     * Without the rule the first eight would take everything.
     */
    live = 32;
    for (dev = 1; dev <= live; dev++) {
        CHECK_EQ(XhciPoolAcquire(&pool, dev, live - dev, &idx), XHCI_POOL_OK,
                 "first ring granted to every live device");
        /* Immediately ask for a second. Every one of these must be refused
         * while any other device is still ringless. */
        if (dev < live) {
            CHECK_EQ(XhciPoolAcquire(&pool, dev, live - dev, &idx),
                     XHCI_POOL_UNFAIR,
                     "second ring refused while another device holds none");
        }
    }
    CHECK_EQ(pool.InUse, 32UL, "every device got exactly one ring");
    CHECK_EQ(pool.AcquireFailuresFairness, 31UL, "31 fairness refusals");
    CHECK_EQ(pool.AcquireFailuresEmpty, 0UL,
             "no exhaustion refusal - the pool was never actually empty");

    /* With everyone served, the pool is genuinely full and the next ask is an
     * exhaustion rather than a fairness refusal. The two are counted apart
     * because they mean opposite things. */
    CHECK_EQ(XhciPoolAcquire(&pool, 1UL, 0UL, &idx), XHCI_POOL_EMPTY,
             "a full pool refuses as empty, not as unfair");

    /*
     * Now the greedy case with room: 4 live devices on a fresh pool. Each may
     * reach the cap, because `ringless` falls to zero once all four hold one.
     */
    XhciPoolInit(&pool);
    granted = 0;
    for (dev = 1; dev <= 4; dev++) {
        CHECK_EQ(XhciPoolAcquire(&pool, dev, 4UL - dev, &idx), XHCI_POOL_OK,
                 "first ring for each of four devices");
        granted++;
    }
    for (dev = 1; dev <= 4; dev++) {
        ULONG n;
        for (n = 1; n < XHCI_MAX_DEVICE_ENDPOINTS; n++) {
            CHECK_EQ(XhciPoolAcquire(&pool, dev, 0UL, &idx), XHCI_POOL_OK,
                     "later rings granted once nobody is ringless");
            granted++;
        }
        CHECK_EQ(XhciPoolAcquire(&pool, dev, 0UL, &idx), XHCI_POOL_AT_CAP,
                 "the per-device cap refuses the fifth");
    }
    CHECK_EQ(granted, 16UL, "four devices at the cap of four");
    CHECK_EQ(pool.InUse, 16UL, "pool accounting agrees");
    CHECK_EQ(pool.AcquireFailuresCap, 4UL, "one cap refusal per device");
    CHECK_EQ(pool.PeakInUse, 16UL, "peak recorded");
}

/*
 * The cases the first version of these vectors could not distinguish, and the
 * boundary of the guarantee.
 *
 * A review pointed out that a *defective* rule - "refuse every later ring
 * whenever `ringless > 0`" - would have passed the vector above, because that
 * vector only ever asks for a later ring when the pool is exactly tight. So the
 * first case here has slack and the later ring must be **granted**: it is the
 * one that tells a fairness rule from a blanket refusal.
 */
static void test_pool_admission_boundaries(void)
{
    XHCI_RING_POOL pool;
    ULONG idx;
    ULONG dev;

    /* Slack: 3 live devices, 32 rings. Device 1 holds one, two others hold
     * none, so ringless = 2 and free = 31. A later ring must succeed - there is
     * plenty for the other two. */
    XhciPoolInit(&pool);
    CHECK_EQ(XhciPoolAcquire(&pool, 1UL, 2UL, &idx), XHCI_POOL_OK, "first ring");
    CHECK_EQ(XhciPoolAcquire(&pool, 1UL, 2UL, &idx), XHCI_POOL_OK,
             "later ring GRANTED when there is slack - not a blanket refusal");
    CHECK_EQ(XhciPoolDeviceCount(&pool, 1UL), 2UL, "device 1 holds two");
    CHECK_EQ(pool.AcquireFailuresFairness, 0UL, "no fairness refusal recorded");

    /* Exactly tight: drive the pool down until free == ringless, then the next
     * later ring must be refused and a first ring must still succeed. */
    XhciPoolInit(&pool);
    for (dev = 1; dev <= 30; dev++) {
        CHECK_EQ(XhciPoolAcquire(&pool, dev, 0UL, &idx), XHCI_POOL_OK, "fill");
    }
    /* free = 2. With ringless = 2, a later ring for an existing holder must go. */
    CHECK_EQ(XhciPoolFree(&pool), 2UL, "two rings left");
    CHECK_EQ(XhciPoolAcquire(&pool, 1UL, 2UL, &idx), XHCI_POOL_UNFAIR,
             "later ring refused at free == ringless");
    /* ...but a ringless device's first ring is granted from the same state. */
    CHECK_EQ(XhciPoolAcquire(&pool, 31UL, 1UL, &idx), XHCI_POOL_OK,
             "a first ring is still granted at free == ringless");
    /* One left, one ringless device: still granted. */
    CHECK_EQ(XhciPoolAcquire(&pool, 32UL, 0UL, &idx), XHCI_POOL_OK,
             "the last ring goes to the last ringless device");
    CHECK_EQ(XhciPoolFree(&pool), 0UL, "pool now empty");

    /*
     * `ringless >= free` on a *first* request. The rule deliberately does not
     * gate first rings, so this succeeds while any ring is free - and that is
     * the documented limit of the guarantee, not an oversight: refusing here
     * would starve the very endpoint the rule exists to protect.
     */
    XhciPoolInit(&pool);
    CHECK_EQ(XhciPoolAcquire(&pool, 1UL, 99UL, &idx), XHCI_POOL_OK,
             "a first ring ignores ringless - refusing it would defeat the rule");

    /*
     * Devices introduced *after* the pool is consumed. This is the case the
     * guarantee explicitly does not cover (design doc 04 section 3.6): one
     * device takes its cap while nothing else is live, then more devices
     * appear. No acquire-time rule can reserve against a record that did not
     * exist, and the arithmetic there shows a pool equal to the slot count
     * cannot buy the stronger property at all.
     */
    XhciPoolInit(&pool);
    for (dev = 1; dev <= 8; dev++) {
        ULONG n;
        for (n = 0; n < XHCI_MAX_DEVICE_ENDPOINTS; n++) {
            CHECK_EQ(XhciPoolAcquire(&pool, dev, 0UL, &idx), XHCI_POOL_OK,
                     "eight devices reach the cap with nothing else live");
        }
    }
    CHECK_EQ(pool.InUse, 32UL, "eight devices at the cap hold the whole pool");
    CHECK_EQ(XhciPoolAcquire(&pool, 9UL, 0UL, &idx), XHCI_POOL_EMPTY,
             "a device arriving later IS refused its first ring - the "
             "documented limit, not a bug");
}

/*
 * `Owner[]` is the single source of truth for occupancy. A stale `InUse` must
 * not be able to refuse an allocation the array can satisfy, which is why
 * XhciPoolFree counts the array rather than returning the running total.
 */
static void test_pool_accounting_disagreement(void)
{
    XHCI_RING_POOL pool;
    ULONG idx;

    XhciPoolInit(&pool);
    CHECK_EQ(XhciPoolAcquire(&pool, 1UL, 0UL, &idx), XHCI_POOL_OK, "one ring");

    /* Corrupt the running total upward and confirm allocation still works. */
    pool.InUse = XHCI_MAX_POOL_RINGS;
    CHECK_EQ(XhciPoolFree(&pool), 31UL,
             "free is counted from Owner[], not from InUse");
    CHECK_EQ(XhciPoolAcquire(&pool, 2UL, 0UL, &idx), XHCI_POOL_OK,
             "a stale InUse cannot refuse a ring the array has");

    /* Release underflow: drive InUse to 0 with entries still owned, then
     * release twice. Neither may wrap the counter. */
    pool.InUse = 0;
    CHECK_EQ(XhciPoolReleaseDevice(&pool, 1UL), 1UL, "device 1 released");
    CHECK_EQ(pool.InUse, 0UL, "InUse floors at zero rather than wrapping");
    CHECK_EQ(XhciPoolRelease(&pool, 2UL, idx), XHCI_POOL_OK, "device 2 released");
    CHECK_EQ(pool.InUse, 0UL, "still zero, not 0xFFFFFFFF");
    CHECK_EQ(XhciPoolFree(&pool), 32UL, "and the array agrees the pool is free");
}

static void test_pool_release(void)
{
    XHCI_RING_POOL pool;
    ULONG a;
    ULONG b;

    XhciPoolInit(&pool);
    CHECK_EQ(XhciPoolAcquire(&pool, 7UL, 0UL, &a), XHCI_POOL_OK, "dev 7 ring a");
    CHECK_EQ(XhciPoolAcquire(&pool, 7UL, 0UL, &b), XHCI_POOL_OK, "dev 7 ring b");
    CHECK(a != b, "two acquires give two different rings");
    CHECK_EQ(XhciPoolDeviceCount(&pool, 7UL), 2UL, "dev 7 holds two");
    CHECK_EQ(XhciPoolFree(&pool), 30UL, "30 rings left");

    /* A release naming the wrong owner is a bookkeeping error, and repairing it
     * would hand a live ring to a second endpoint. */
    CHECK_EQ(XhciPoolRelease(&pool, 8UL, a), XHCI_POOL_BAD_PARAM,
             "release by the wrong device refused");
    CHECK_EQ(XhciPoolDeviceCount(&pool, 7UL), 2UL, "and changed nothing");
    CHECK_EQ(XhciPoolRelease(&pool, 7UL, a), XHCI_POOL_OK, "correct release");
    CHECK_EQ(XhciPoolRelease(&pool, 7UL, a), XHCI_POOL_BAD_PARAM,
             "double release refused");
    CHECK_EQ(XhciPoolRelease(&pool, 7UL, 32UL), XHCI_POOL_BAD_PARAM,
             "out-of-range index refused");
    CHECK_EQ(pool.InUse, 1UL, "one ring still held after the refusals");

    /* Teardown releases whatever a device still holds, without needing the
     * endpoint records that named them. */
    CHECK_EQ(XhciPoolAcquire(&pool, 9UL, 0UL, &a), XHCI_POOL_OK, "dev 9");
    CHECK_EQ(XhciPoolReleaseDevice(&pool, 7UL), 1UL, "dev 7 had one left");
    CHECK_EQ(XhciPoolDeviceCount(&pool, 7UL), 0UL, "dev 7 holds nothing");
    CHECK_EQ(XhciPoolDeviceCount(&pool, 9UL), 1UL, "dev 9 untouched");
    CHECK_EQ(XhciPoolReleaseDevice(&pool, 7UL), 0UL, "releasing again frees 0");

    CHECK_EQ(XhciPoolAcquire(&pool, 0UL, 0UL, &a), XHCI_POOL_BAD_PARAM,
             "device ref 0 refused - it is the free marker");
    CHECK_EQ(XhciPoolAcquire(&pool, 1UL, 0UL, NULL), XHCI_POOL_BAD_PARAM,
             "null out pointer refused");

    /*
     * **The last entry too**, which the vectors above cannot reach: they use the
     * low indices, so a release loop bounded at `XHCI_MAX_POOL_RINGS - 1` would
     * leak ring 31 on every ordinary teardown and pass everything. Fill the pool
     * with eight devices at the per-device cap, then release the one that owns
     * the top of it.
     */
    {
        ULONG last;
        ULONG i;

        XhciPoolInit(&pool);
        for (i = 0; i < XHCI_MAX_POOL_RINGS; i++) {
            CHECK_EQ(XhciPoolAcquire(&pool,
                                     (i / XHCI_MAX_DEVICE_ENDPOINTS) + 1UL,
                                     0UL, &a),
                     XHCI_POOL_OK, "the whole pool, four rings per device");
        }
        last = XHCI_MAX_POOL_RINGS / XHCI_MAX_DEVICE_ENDPOINTS;
        CHECK_EQ(pool.Owner[XHCI_MAX_POOL_RINGS - 1], last,
                 "the last entry belongs to the last device");
        CHECK_EQ(XhciPoolReleaseDevice(&pool, last), XHCI_MAX_DEVICE_ENDPOINTS,
                 "and all four of its rings come back");
        CHECK_EQ(pool.Owner[XHCI_MAX_POOL_RINGS - 1], 0UL,
                 "including the one at the very top of the pool");
        CHECK_EQ(XhciPoolFree(&pool), XHCI_MAX_DEVICE_ENDPOINTS,
                 "so exactly four are free again");
    }
}

static void test_resource_base(void)
{
    CHECK_EQ(XhciCheckResourceBase(0x81000000UL, 0x0F000000UL),
             XHCI_LAYOUT_OK, "page-aligned VA and PA accepted");

    /* A 64-byte-aligned StartPA would satisfy every xHCI alignment rule and
     * still break the device-context page rule; refuse it explicitly. */
    CHECK_EQ(XhciCheckResourceBase(0x81000000UL, 0x0F000040UL),
             XHCI_LAYOUT_UNALIGNED_BASE, "64-byte-aligned PA refused");
    CHECK_EQ(XhciCheckResourceBase(0x81000040UL, 0x0F000000UL),
             XHCI_LAYOUT_UNALIGNED_BASE, "64-byte-aligned VA refused");
    CHECK_EQ(XhciCheckResourceBase(0, 0x0F000000UL),
             XHCI_LAYOUT_UNALIGNED_BASE, "null VA refused");
    CHECK_EQ(XhciCheckResourceBase(0x81000000UL, 0),
             XHCI_LAYOUT_UNALIGNED_BASE, "null PA refused");

    /* No 64-bit DMA: the whole block must sit below 4 GB. */
    CHECK_EQ(XhciCheckResourceBase(0x81000000UL, 0xFFFF0000UL),
             XHCI_LAYOUT_OVERFLOW, "block running past 4 GB refused");
    /* 0x100000000 - 0x64000. Transcribed by hand rather than computed from
     * XHCI_HC_RESOURCES_SIZE, so that a change to the declared size has to be
     * re-derived here instead of silently agreeing with itself. */
    CHECK_EQ(XhciCheckResourceBase(0x81000000UL, 0xFFF9C000UL),
             XHCI_LAYOUT_OK, "block ending at 4 GB boundary accepted");
    CHECK_EQ(XhciCheckResourceBase(0x81000000UL, 0xFFF9D000UL),
             XHCI_LAYOUT_OVERFLOW, "block one page too high refused");
}

/* ------------------------------------------------------------------ */
/* 7. Context strides                                                  */
/* ------------------------------------------------------------------ */

/*
 * docs/usb-xhci-info/xhci-data-structures.md section 8: the byte offset of context index i
 * is i * ContextSize, and the two flavours index differently -
 *
 *   Device Context:  0 = Slot Context,          i = Endpoint Context DCI i
 *   Input Context:   0 = Input Control Context, 1 = Slot Context,
 *                    i + 1 = Endpoint Context DCI i
 *
 * so an Input Context endpoint sits one stride further along than the same
 * DCI in the Device Context. Both strides are exercised: CSZ = 0 (32 bytes)
 * is what the fleet reports, CSZ = 1 (64) is what the layout reserves for.
 */
static void check_context_strides(ULONG contextSize, const char *tag)
{
    XHCI_HC_LAYOUT l;
    ULONG off;
    ULONG base;
    ULONG dci;

    CHECK_EQ(XhciComputeLayout(contextSize, 64, 34, 0x00000001UL, &l),
             XHCI_LAYOUT_OK, tag);

    /* Device Context: slot context first, then DCI 1..31. */
    CHECK_EQ(XhciSlotContextOffset(&l, 1, &base), XHCI_LAYOUT_OK, tag);
    CHECK_EQ(base, 0x04000UL, tag);
    for (dci = 1; dci <= 31; dci++) {
        CHECK_EQ(XhciEndpointContextOffset(&l, 1, dci, &off), XHCI_LAYOUT_OK,
                 tag);
        CHECK_EQ(off, base + dci * contextSize, tag);
        /* Every endpoint context stays inside the slot's reservation. */
        CHECK(off + contextSize <= base + l.DeviceContextStride, tag);
    }

    /* The last slot's last endpoint is the tightest case in the region. */
    CHECK_EQ(XhciSlotContextOffset(&l, 32, &base), XHCI_LAYOUT_OK, tag);
    CHECK_EQ(XhciEndpointContextOffset(&l, 32, 31, &off), XHCI_LAYOUT_OK, tag);
    CHECK_EQ(off, base + 31UL * contextSize, tag);
    CHECK(off + contextSize <= XHCI_REGION_EP0_RINGS, tag);

    /* Input Context: everything shifted down by the control context. */
    CHECK_EQ(XhciInputControlContextOffset(&l, &off), XHCI_LAYOUT_OK, tag);
    CHECK_EQ(off, 0x03000UL, tag);
    CHECK_EQ(XhciInputSlotContextOffset(&l, &off), XHCI_LAYOUT_OK, tag);
    CHECK_EQ(off, 0x03000UL + contextSize, tag);
    for (dci = 1; dci <= 31; dci++) {
        CHECK_EQ(XhciInputEndpointContextOffset(&l, dci, &off),
                 XHCI_LAYOUT_OK, tag);
        CHECK_EQ(off, 0x03000UL + (dci + 1) * contextSize, tag);
        CHECK(off + contextSize <=
              0x03000UL + XHCI_INPUT_CONTEXT_RESERVED, tag);
    }
}

static void test_context_strides(void)
{
    XHCI_HC_LAYOUT l;
    ULONG off = 0xDEADBEEFUL;

    check_context_strides(32, "CSZ=0 context strides");
    check_context_strides(64, "CSZ=1 context strides");

    /* Hand-transcribed spot values, so a stride bug cannot hide behind the
     * multiplication above being wrong in the same direction. */
    CHECK_EQ(XhciComputeLayout(64, 64, 0, 0x00000001UL, &l), XHCI_LAYOUT_OK,
             "64-byte-context controller");
    CHECK_EQ(XhciEndpointContextOffset(&l, 1, 1, &off), XHCI_LAYOUT_OK, "EP0");
    CHECK_EQ(off, 0x04040UL, "slot 1 EP0 context at CSZ=1");
    CHECK_EQ(XhciInputEndpointContextOffset(&l, 1, &off), XHCI_LAYOUT_OK, "in");
    CHECK_EQ(off, 0x03080UL, "input EP0 context at CSZ=1");
    CHECK_EQ(XhciInputSlotContextOffset(&l, &off), XHCI_LAYOUT_OK, "in slot");
    CHECK_EQ(off, 0x03040UL, "input slot context at CSZ=1");

    /* DCI 0 is the Slot Context and DCI 31 is the last one a Device Context
     * can describe (spec 4.5.1) - neither end may be walked past. */
    off = 0xDEADBEEFUL;
    CHECK_EQ(XhciEndpointContextOffset(&l, 1, 0, &off), XHCI_LAYOUT_BAD_INDEX,
             "DCI 0 is not an endpoint");
    CHECK_EQ(XhciEndpointContextOffset(&l, 1, 32, &off), XHCI_LAYOUT_BAD_INDEX,
             "DCI 32 does not exist");
    CHECK_EQ(XhciInputEndpointContextOffset(&l, 0, &off), XHCI_LAYOUT_BAD_INDEX,
             "input DCI 0 is not an endpoint");
    CHECK_EQ(XhciInputEndpointContextOffset(&l, 32, &off),
             XHCI_LAYOUT_BAD_INDEX, "input DCI 32 does not exist");
    CHECK_EQ(off, 0xDEADBEEFUL, "a rejected index leaves the output untouched");

    /* The slot bounds still apply through the endpoint accessor. */
    CHECK_EQ(XhciEndpointContextOffset(&l, 0, 1, &off), XHCI_LAYOUT_BAD_INDEX,
             "slot 0 rejected");
    CHECK_EQ(XhciEndpointContextOffset(&l, XHCI_MAX_SLOTS + 1, 1, &off),
             XHCI_LAYOUT_BAD_INDEX, "slot past MaxSlotsEn rejected");
}

int main(void)
{
    test_declared_size();
    test_allocation_cost();
    test_fleet_controller();
    test_boundary_rules();
    test_refusals();
    test_index_bounds();
    test_pool_first_ring_guarantee();
    test_pool_admission_boundaries();
    test_pool_accounting_disagreement();
    test_pool_release();
    test_resource_base();
    test_context_strides();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
