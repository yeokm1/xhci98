/*
 * test_ring.c - host tests for TRB encoding and the ring state machines
 * (src/xhci_ring.c).
 *
 * This is the suite design doc 03 exists for. Cycle-bit and wrap handling is
 * the most error-prone code in the driver and the least observable in a VM: a
 * ring that toggles one lap early looks like a controller that stops
 * responding, several seconds and one reboot after the mistake. Here three
 * full laps run in microseconds and every intermediate cycle bit is checked.
 *
 * Per docs/contributing/design/03-host-unit-tests.md, expected values are transcribed
 * by hand from docs/usb-xhci-info/xhci-data-structures.md section 7 - never produced by the
 * code under test. The TRB type codes are spelled as literal shifted values
 * below for exactly that reason: a test that says XHCI_TRB_TYPE(23) would
 * agree with the driver even if both were wrong.
 *
 * What this suite deliberately cannot check is *ordering* - that each TRB's
 * Cycle Bit reaches memory after its payload. That rests on the volatile
 * accesses in xhci_ring.c and is a review property, not a testable one on the
 * host (design doc 03 section 4).
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
        printf("FAIL %s:%d: %s\n", "test_ring.c", line, what);
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
               "test_ring.c", line, what, got, got, want, want);
    }
}

/* Plausible common-buffer physical bases: page aligned, below 4 GB. */
#define RING_PA  0x0F001000UL
#define EVENT_PA 0x0F002000UL

/* ------------------------------------------------------------------ */
/* Shared helpers                                                      */
/* ------------------------------------------------------------------ */

/*
 * A three-TRB TD shaped the way spec 4.11.7 requires: the Chain flag set in
 * every TRB except the last, IOC on the last only. Built here rather than by
 * the code under test.
 */
static void build_td(XHCI_TRB *td, ULONG count)
{
    ULONG i;

    for (i = 0; i < count; i++) {
        XhciTrbClear(&td[i]);
        td[i].Param0 = 0x0E000000UL + i * 0x1000UL;
        td[i].Status = 0x1000UL;
        td[i].Control = XHCI_TRB_TYPE(XHCI_TRB_TYPE_NORMAL);
        if (i + 1 < count) {
            td[i].Control |= XHCI_TRB_CH;
        } else {
            td[i].Control |= XHCI_TRB_IOC;
        }
    }
}

/*
 * The ordinary completion path: an event reporting Success against a TRB, put
 * through the classifier and then retired. Returns the first status that is
 * not XHCI_RING_OK, so a caller can assert on either step.
 */
static ULONG retire_success(XHCI_RING *ring, ULONG pa)
{
    XHCI_TD_COMPLETION completion;
    ULONG status;

    status = XhciRingClassifyEvent(ring, pa, XHCI_CC_SUCCESS, &completion);
    if (status != XHCI_RING_OK) {
        return status;
    }
    return XhciRingRetireTd(ring, &completion);
}

/* The same for an explicit completion code. */
static ULONG retire_with_code(XHCI_RING *ring, ULONG pa, ULONG code)
{
    XHCI_TD_COMPLETION completion;
    ULONG status;

    status = XhciRingClassifyEvent(ring, pa, code, &completion);
    if (status != XHCI_RING_OK) {
        return status;
    }
    return XhciRingRetireTd(ring, &completion);
}

/* ------------------------------------------------------------------ */
/* 1. TRB field macros and builders                                    */
/* ------------------------------------------------------------------ */

static void test_trb_fields(void)
{
    XHCI_TRB trb;

    /* Type is bits 15:10. 23 << 10 = 0x5C00; 6 << 10 = 0x1800. */
    CHECK_EQ(XHCI_TRB_TYPE(23), 0x00005C00UL, "No Op Command type field");
    CHECK_EQ(XHCI_TRB_TYPE(6), 0x00001800UL, "Link type field");
    CHECK_EQ(XHCI_TRB_TYPE(32), 0x00008000UL, "Transfer Event type field");
    CHECK_EQ(XHCI_TRB_GET_TYPE(0x00005C01UL), 23, "type decoded past cycle");
    CHECK_EQ(XHCI_TRB_GET_TYPE(0xFFFFFFFFUL), 63, "type field is six bits");

    CHECK_EQ(XHCI_TRB_SLOT_ID(5), 0x05000000UL, "slot ID is bits 31:24");
    CHECK_EQ(XHCI_TRB_GET_SLOT_ID(0x05038001UL), 5, "slot ID decoded");
    CHECK_EQ(XHCI_TRB_EP_ID(3), 0x00030000UL, "endpoint ID is bits 20:16");
    CHECK_EQ(XHCI_TRB_GET_EP_ID(0x05038001UL), 3, "endpoint ID decoded");
    CHECK_EQ(XHCI_TRB_SLOT_TYPE(9), 0x00090000UL, "slot type is bits 20:16");

    /* DW2 of an event: residual 23:0, completion code 31:24. */
    CHECK_EQ(XHCI_TRB_GET_COMPLETION(0x0D000010UL), 13, "Short Packet code");
    CHECK_EQ(XHCI_TRB_GET_RESIDUAL(0x0D000010UL), 0x10UL, "residual bytes");
    CHECK_EQ(XHCI_TRB_GET_COMPLETION(0x01000000UL), 1, "Success code");
    /* DW0 of a Port Status Change Event: Port ID 31:24, 1-based. */
    CHECK_EQ(XHCI_TRB_GET_PORT_ID(0x04000000UL), 4, "port ID");

    /* TC and ENT share bit 1 and must stay separately named. */
    CHECK_EQ(XHCI_TRB_LINK_TC, 0x00000002UL, "Link Toggle Cycle is bit 1");
    CHECK_EQ(XHCI_TRB_ENT, XHCI_TRB_LINK_TC, "ENT occupies the same bit");
    CHECK_EQ(XHCI_TRB_CH, 0x00000010UL, "Chain is bit 4");
    CHECK_EQ(XHCI_TRB_IOC, 0x00000020UL, "IOC is bit 5");
    CHECK_EQ(XHCI_TRB_IDT, 0x00000040UL, "Immediate Data is bit 6");
    CHECK_EQ(XHCI_TRB_BSR, 0x00000200UL, "Block Set Address is bit 9");
    CHECK_EQ(XHCI_TRB_DIR_IN, 0x00010000UL, "direction is bit 16");

    /* Golden vectors: the two TRBs Phase 4 actually produces. */
    XhciTrbNoOpCommand(&trb);
    CHECK_EQ(trb.Param0, 0, "No Op DW0");
    CHECK_EQ(trb.Param1, 0, "No Op DW1");
    CHECK_EQ(trb.Status, 0, "No Op DW2");
    CHECK_EQ(trb.Control, 0x00005C00UL, "No Op DW3 (type 23, cycle unset)");

    XhciTrbLink(&trb, 0x0F001000UL, 1, 1);
    CHECK_EQ(trb.Param0, 0x0F001000UL, "Link DW0 is the segment base");
    CHECK_EQ(trb.Param1, 0, "Link DW1 is zero - no 64-bit DMA");
    CHECK_EQ(trb.Status, 0, "Link DW2");
    CHECK_EQ(trb.Control, 0x00001803UL, "Link DW3 (type 6, TC, cycle)");

    XhciTrbLink(&trb, 0x0F001000UL, 1, 0);
    CHECK_EQ(trb.Control, 0x00001802UL, "Link DW3 with cycle clear");
    XhciTrbLink(&trb, 0x0F001000UL, 0, 0);
    CHECK_EQ(trb.Control, 0x00001800UL, "Link DW3 without Toggle Cycle");
}

/* ------------------------------------------------------------------ */
/* 2. Producer ring initialization                                     */
/* ------------------------------------------------------------------ */

static void test_ring_init(void)
{
    XHCI_TRB mem[8];
    XHCI_RING ring;
    ULONG i;

    for (i = 0; i < 8; i++) {
        mem[i].Param0 = 0xAAAAAAAAUL;
        mem[i].Param1 = 0xAAAAAAAAUL;
        mem[i].Status = 0xAAAAAAAAUL;
        mem[i].Control = 0xAAAAAAAAUL;
    }

    CHECK_EQ(XhciRingInit(&ring, mem, RING_PA, 8, XHCI_RING_KIND_ENDPOINT),
             XHCI_RING_OK,
             "8-TRB ring accepted");
    CHECK_EQ(ring.Enqueue, 0, "enqueue starts at 0");
    CHECK_EQ(ring.Dequeue, 0, "dequeue starts at 0");
    CHECK_EQ(ring.Cycle, 1, "producer cycle starts at 1");
    CHECK_EQ(ring.Trbs, 8, "TRB count retained");

    /* One slot is the Link TRB and one stays permanently empty. */
    CHECK_EQ(XhciRingCapacity(&ring), 6, "capacity is TRBs - 2");
    CHECK_EQ(XhciRingFree(&ring), 6, "a fresh ring is entirely free");
    CHECK_EQ(XhciRingHasRoom(&ring, 6), 1, "room for a full ring");
    CHECK_EQ(XhciRingHasRoom(&ring, 7), 0, "no room past capacity");

    for (i = 0; i < 7; i++) {
        CHECK_EQ(mem[i].Param0, 0, "segment zeroed");
        CHECK_EQ(mem[i].Control, 0, "segment zeroed (control)");
    }
    /*
     * The Link TRB carries the *opposite* of the producer cycle. That is what
     * stops the hardware at the end of the segment until software has filled
     * a lap: type 6, TC set, cycle 0 while the producer is on cycle 1.
     */
    CHECK_EQ(mem[7].Param0, RING_PA, "link points back at the segment base");
    CHECK_EQ(mem[7].Param1, 0, "link high DWORD is zero");
    CHECK_EQ(mem[7].Control, 0x00001802UL, "link is type 6, TC set, cycle 0");

    CHECK_EQ(XhciRingTrbPA(&ring, 0), RING_PA, "TRB 0 physical address");
    CHECK_EQ(XhciRingTrbPA(&ring, 3), RING_PA + 48, "TRB 3 physical address");
    CHECK_EQ(XhciRingTrbPA(&ring, 8), 0, "index past the segment has no PA");
}

static void test_ring_init_refusals(void)
{
    XHCI_TRB mem[8];
    XHCI_RING ring;

    CHECK_EQ(XhciRingInit(&ring, NULL, RING_PA, 8, XHCI_RING_KIND_ENDPOINT),
             XHCI_RING_BAD_PARAM,
             "null segment refused");
    CHECK_EQ(XhciRingInit(&ring, mem, 0, 8, XHCI_RING_KIND_ENDPOINT),
             XHCI_RING_BAD_PARAM,
             "zero physical base refused");
    /* Two TRBs would be a Link plus the one slot that must stay empty. */
    CHECK_EQ(XhciRingInit(&ring, mem, RING_PA, 2, XHCI_RING_KIND_ENDPOINT),
             XHCI_RING_BAD_PARAM,
             "two-TRB ring has no usable slot");
    CHECK_EQ(XhciRingInit(&ring, mem, RING_PA, 3, XHCI_RING_KIND_ENDPOINT),
             XHCI_RING_OK,
             "three-TRB ring is the minimum");
    CHECK_EQ(XhciRingCapacity(&ring), 1, "minimum ring holds one TRB");
    CHECK_EQ(XhciRingInit(&ring, mem, RING_PA + 8, 8, XHCI_RING_KIND_ENDPOINT),
             XHCI_RING_BAD_PARAM,
             "8-byte-aligned base refused (TRBs are 16-byte aligned)");
    /* Refused before anything is written, which is why passing an 8-TRB
     * array with a 64-TRB count is safe here. */
    CHECK_EQ(XhciRingInit(&ring, mem, 0xFFFFFF00UL, 64, XHCI_RING_KIND_ENDPOINT),
             XHCI_RING_BAD_PARAM,
             "segment running past 4 GB refused");

    /* The kind is what every completion classification keys on, so an
     * unrecognised one is refused rather than silently read as COMMAND. */
    CHECK_EQ(XhciRingInit(&ring, mem, RING_PA, 8, XHCI_RING_KIND_ISOCH + 1),
             XHCI_RING_BAD_PARAM, "unknown ring kind refused");
    CHECK_EQ(XhciRingInit(&ring, mem, RING_PA, 8, XHCI_RING_KIND_COMMAND),
             XHCI_RING_OK, "command ring accepted");
    CHECK_EQ(ring.Kind, XHCI_RING_KIND_COMMAND, "and the kind is recorded");
    CHECK_EQ(XhciRingInit(&ring, mem, RING_PA, 8, XHCI_RING_KIND_ISOCH),
             XHCI_RING_OK, "isoch ring accepted");
    CHECK_EQ(ring.Kind, XHCI_RING_KIND_ISOCH, "and recorded");
}

/*
 * Table 6-1 again: a ring segment is at most 64 KB *and* must not span a
 * 64 KB boundary. They are separate rules - a 48-byte segment is comfortably
 * under the size cap and can still straddle the boundary - and the second one
 * is the one a caller satisfies by accident and then loses.
 */
static void test_segment_boundaries(void)
{
    static XHCI_TRB big[4097];
    XHCI_RING ring;
    XHCI_EVENT_RING er;

    /* Producer ring, 3 TRBs = 48 bytes. 0x10000 - 48 = 0xFFD0. */
    CHECK_EQ(XhciRingInit(&ring, big, 0x0000FFD0UL, 3, XHCI_RING_KIND_ENDPOINT),
             XHCI_RING_OK,
             "a segment ending exactly on the 64 KB boundary is fine");
    CHECK_EQ(XhciRingInit(&ring, big, 0x0000FFE0UL, 3, XHCI_RING_KIND_ENDPOINT),
             XHCI_RING_BAD_PARAM,
             "one TRB past the boundary is refused");
    CHECK_EQ(XhciRingInit(&ring, big, 0x0000FFF0UL, 3, XHCI_RING_KIND_ENDPOINT),
             XHCI_RING_BAD_PARAM,
             "a segment straddling 64 KB is refused");
    CHECK_EQ(XhciRingInit(&ring, big, 0x00010000UL, 3, XHCI_RING_KIND_ENDPOINT),
             XHCI_RING_OK,
             "the next 64 KB block starts clean");
    /* The last byte is at base + bytes - 1, so a segment ending exactly at
     * 0xFFFFFFFF is legal and must not be lost to an off-by-one. */
    CHECK_EQ(XhciRingInit(&ring, big, 0xFFFFFC00UL, 64, XHCI_RING_KIND_ENDPOINT),
             XHCI_RING_OK,
             "a segment ending exactly at 0xFFFFFFFF is accepted");
    CHECK_EQ(XhciRingInit(&ring, big, 0xFFFFFC10UL, 64, XHCI_RING_KIND_ENDPOINT),
             XHCI_RING_BAD_PARAM,
             "one TRB higher runs off the top of the address space");

    /* Event ring, 16 TRBs = 256 bytes. 0x10000 - 256 = 0xFF00. */
    CHECK_EQ(XhciEventRingInit(&er, big, 0x0000FF00UL, 16), XHCI_RING_OK,
             "an event segment ending on the boundary is fine");
    CHECK_EQ(XhciEventRingInit(&er, big, 0x0000FFC0UL, 16), XHCI_RING_BAD_PARAM,
             "an aligned event segment straddling 64 KB is still refused");

    /* The size cap itself: 4096 TRBs is exactly 64 KB. */
    CHECK_EQ(XhciEventRingInit(&er, big, 0x00010000UL, 4096), XHCI_RING_OK,
             "a full 64 KB event segment at a 64 KB boundary is legal");
    CHECK_EQ(XhciEventRingInit(&er, big, 0x00010040UL, 4096),
             XHCI_RING_BAD_PARAM, "the same segment 64 bytes higher is not");
    CHECK_EQ(XhciRingInit(&ring, big, 0x00010000UL, 4097, XHCI_RING_KIND_ENDPOINT),
             XHCI_RING_BAD_PARAM,
             "a producer segment larger than 64 KB is refused");
}

/* ------------------------------------------------------------------ */
/* 3. Enqueue, wrap, and the cycle bit across laps                     */
/* ------------------------------------------------------------------ */

/*
 * Walk a small ring through more than three complete laps, retiring each TRB
 * immediately so the producer never blocks. After every enqueue this checks
 * the written cycle bit, the enqueue index, and - at each crossing - that the
 * Link TRB was handed the cycle the hardware is consuming *before* the
 * producer toggled.
 */
static void test_ring_wrap(void)
{
    XHCI_TRB mem[8];
    XHCI_TRB trb;
    XHCI_RING ring;
    ULONG lap;
    ULONG i;
    ULONG pa;
    ULONG expectCycle;

    CHECK_EQ(XhciRingInit(&ring, mem, RING_PA, 8, XHCI_RING_KIND_ENDPOINT),
             XHCI_RING_OK, "ring init");

    for (lap = 0; lap < 4; lap++) {
        expectCycle = (lap % 2 == 0) ? 1 : 0;
        CHECK_EQ(ring.Cycle, expectCycle, "producer cycle at lap start");

        /* Seven real slots per lap (index 7 is the Link TRB). */
        for (i = 0; i < 7; i++) {
            XhciTrbNoOpCommand(&trb);
            /* A template's own cycle bit is not the producer's: set it to the
             * wrong value and require the ring to overwrite it. */
            trb.Control |= XHCI_TRB_CYCLE;

            CHECK_EQ(ring.Enqueue, i, "enqueue index walks the segment");
            CHECK_EQ(XhciRingEnqueue(&ring, &trb, &pa), XHCI_RING_OK,
                     "enqueue accepted");
            CHECK_EQ(pa, RING_PA + i * 16, "reported TRB physical address");
            CHECK_EQ(mem[i].Control & XHCI_TRB_CYCLE, expectCycle,
                     "TRB carries the producer cycle, not the template's");
            CHECK_EQ(XHCI_TRB_GET_TYPE(mem[i].Control), 23,
                     "TRB type survived the cycle stamp");

            CHECK_EQ(retire_success(&ring, pa), XHCI_RING_OK, "retired");
        }

        /* Crossing the link: it now carries the cycle the hardware is still
         * consuming, and the producer has toggled for the next lap. */
        CHECK_EQ(mem[7].Control & XHCI_TRB_CYCLE, expectCycle,
                 "link TRB handed the outgoing lap's cycle");
        CHECK_EQ(mem[7].Control & XHCI_TRB_LINK_TC, XHCI_TRB_LINK_TC,
                 "link keeps Toggle Cycle set");
        CHECK_EQ(XHCI_TRB_GET_TYPE(mem[7].Control), 6, "link stays type 6");
        CHECK_EQ(ring.Cycle, expectCycle ^ 1, "producer toggled at the link");
        CHECK_EQ(ring.Enqueue, 0, "enqueue wrapped to the segment base");
    }
}

/* ------------------------------------------------------------------ */
/* 4. Ring-full detection                                              */
/* ------------------------------------------------------------------ */

/*
 * Spec 4.9.2.2: the ring is full when advancing Enqueue would reach Dequeue.
 * With the Link TRB that leaves TRBs - 2 usable, and a refused enqueue must
 * leave the ring exactly as it was - never a partial write
 * (docs/contributing/implementation-invariants.md, "Ring Full and Backpressure").
 */
static void test_ring_full(void)
{
    XHCI_TRB mem[8];
    XHCI_TRB trb;
    XHCI_RING ring;
    ULONG i;
    ULONG enqueueBefore;
    ULONG cycleBefore;

    CHECK_EQ(XhciRingInit(&ring, mem, RING_PA, 8, XHCI_RING_KIND_ENDPOINT),
             XHCI_RING_OK, "ring init");
    XhciTrbNoOpCommand(&trb);

    for (i = 0; i < 6; i++) {
        CHECK_EQ(XhciRingFree(&ring), 6 - i, "free count counts down");
        CHECK_EQ(XhciRingEnqueue(&ring, &trb, NULL), XHCI_RING_OK,
                 "enqueue within capacity");
    }

    CHECK_EQ(XhciRingFree(&ring), 0, "ring reports full");
    CHECK_EQ(XhciRingHasRoom(&ring, 1), 0, "no room for one more");
    enqueueBefore = ring.Enqueue;
    cycleBefore = ring.Cycle;
    mem[enqueueBefore].Control = 0xDEADBEEFUL;

    CHECK_EQ(XhciRingEnqueue(&ring, &trb, NULL), XHCI_RING_FULL,
             "enqueue past capacity refused");
    CHECK_EQ(ring.Enqueue, enqueueBefore, "refused enqueue did not advance");
    CHECK_EQ(ring.Cycle, cycleBefore, "refused enqueue did not toggle");
    CHECK_EQ(mem[enqueueBefore].Control, 0xDEADBEEFUL,
             "refused enqueue wrote nothing");

    /* One retirement frees exactly one slot. */
    CHECK_EQ(retire_success(&ring, RING_PA), XHCI_RING_OK, "retire TRB 0");
    CHECK_EQ(XhciRingFree(&ring), 1, "one slot freed");
    CHECK_EQ(XhciRingEnqueue(&ring, &trb, NULL), XHCI_RING_OK,
             "the freed slot is usable");
    CHECK_EQ(XhciRingFree(&ring), 0, "and the ring is full again");
}

/* ------------------------------------------------------------------ */
/* 5. Multi-TRB TDs                                                    */
/* ------------------------------------------------------------------ */

/*
 * A TD that does not reach the Link TRB. The observable part of the ordering
 * rule is not testable on the host - a test sees finished bytes, never the
 * order they were stored in - so what is pinned here is everything around it:
 * the whole TD landed, each TRB carries the producer cycle, and the ring
 * advanced by exactly the TD's length.
 */
static void test_td_no_wrap(void)
{
    XHCI_TRB mem[8];
    XHCI_TRB td[3];
    XHCI_RING ring;
    ULONG pa;

    CHECK_EQ(XhciRingInit(&ring, mem, RING_PA, 8, XHCI_RING_KIND_ENDPOINT),
             XHCI_RING_OK, "ring init");
    build_td(td, 3);

    CHECK_EQ(XhciRingEnqueueTd(&ring, td, 3, &pa), XHCI_RING_OK,
             "three-TRB TD accepted");
    CHECK_EQ(pa, RING_PA, "the reported address is the TD's head TRB");

    CHECK_EQ(mem[0].Param0, 0x0E000000UL, "TRB 0 payload");
    CHECK_EQ(mem[1].Param0, 0x0E001000UL, "TRB 1 payload");
    CHECK_EQ(mem[2].Param0, 0x0E002000UL, "TRB 2 payload");
    CHECK_EQ(mem[0].Control & XHCI_TRB_CYCLE, 1, "head TRB is valid");
    CHECK_EQ(mem[1].Control & XHCI_TRB_CYCLE, 1, "middle TRB is valid");
    CHECK_EQ(mem[2].Control & XHCI_TRB_CYCLE, 1, "tail TRB is valid");
    CHECK_EQ(mem[0].Control & XHCI_TRB_CH, XHCI_TRB_CH, "head is chained");
    CHECK_EQ(mem[2].Control & XHCI_TRB_CH, 0, "the last TRB is not chained");
    CHECK_EQ(mem[2].Control & XHCI_TRB_IOC, XHCI_TRB_IOC, "IOC on the last");
    CHECK_EQ(mem[3].Control, 0, "the TD wrote nothing past its own length");

    CHECK_EQ(ring.Enqueue, 3, "enqueue advanced by the TD length");
    CHECK_EQ(ring.Cycle, 1, "no crossing, so no toggle");
    CHECK_EQ(XhciRingFree(&ring), 3, "three slots consumed");
    /* Untouched: this TD never reached the Link TRB. */
    CHECK_EQ(mem[7].Control, 0x00001802UL, "link TRB unchanged");
}

/*
 * A TD that spans the Link TRB. Two things here are what a loop over
 * single-TRB enqueues gets wrong:
 *
 *   - the TRBs after the crossing carry the *toggled* cycle, because the Link
 *     TRB's TC bit toggles the consumer as it passes;
 *   - the Link TRB is inside the TD, so its Chain flag must be set (spec
 *     4.11.7: the flag is set in every TRB of a TD except the last).
 */
static void test_td_spanning_link(void)
{
    XHCI_TRB mem[8];
    XHCI_TRB td[3];
    XHCI_TRB one;
    XHCI_RING ring;
    ULONG pa;
    ULONG i;

    CHECK_EQ(XhciRingInit(&ring, mem, RING_PA, 8, XHCI_RING_KIND_ENDPOINT),
             XHCI_RING_OK, "ring init");

    /* Walk the enqueue pointer to index 5 without leaving work outstanding,
     * so the TD below starts two slots short of the Link TRB. */
    XhciTrbNoOpCommand(&one);
    for (i = 0; i < 5; i++) {
        CHECK_EQ(XhciRingEnqueue(&ring, &one, &pa), XHCI_RING_OK, "filler");
        CHECK_EQ(retire_success(&ring, pa), XHCI_RING_OK, "retired");
    }
    CHECK_EQ(ring.Enqueue, 5, "enqueue is two slots before the link");
    CHECK_EQ(XhciRingFree(&ring), 6, "and the ring is empty again");

    build_td(td, 3);
    CHECK_EQ(XhciRingEnqueueTd(&ring, td, 3, &pa), XHCI_RING_OK,
             "TD spanning the link accepted");
    CHECK_EQ(pa, RING_PA + 5 * 16, "head TRB address");

    CHECK_EQ(mem[5].Param0, 0x0E000000UL, "TRB 0 before the link");
    CHECK_EQ(mem[6].Param0, 0x0E001000UL, "TRB 1 before the link");
    CHECK_EQ(mem[0].Param0, 0x0E002000UL, "TRB 2 after the wrap");

    CHECK_EQ(mem[5].Control & XHCI_TRB_CYCLE, 1, "head carries the old cycle");
    CHECK_EQ(mem[6].Control & XHCI_TRB_CYCLE, 1, "so does TRB 1");
    CHECK_EQ(mem[0].Control & XHCI_TRB_CYCLE, 0,
             "TRB 2 carries the toggled cycle - it is on the next lap");

    CHECK_EQ(mem[7].Control & XHCI_TRB_CYCLE, 1,
             "link handed the outgoing lap's cycle");
    CHECK_EQ(mem[7].Control & XHCI_TRB_CH, XHCI_TRB_CH,
             "link inside a TD carries the Chain flag (spec 4.11.7)");
    CHECK_EQ(mem[7].Control & XHCI_TRB_LINK_TC, XHCI_TRB_LINK_TC,
             "and still toggles the cycle");
    CHECK_EQ(XHCI_TRB_GET_TYPE(mem[7].Control), 6, "and is still a Link TRB");

    CHECK_EQ(ring.Enqueue, 1, "enqueue wrapped past the TD's tail");
    CHECK_EQ(ring.Cycle, 0, "producer toggled once");
    CHECK_EQ(XhciRingFree(&ring), 3, "three slots consumed");

    /*
     * The Link TRB is permanent, so a later crossing *between* TDs has to put
     * its Chain flag back to 0 - the flag describes this lap's TD, not the
     * last one's. A one-shot "set CH on the link" would leave every subsequent
     * single-TRB TD falsely chained into whatever followed.
     */
    for (i = 0; i < 6; i++) {
        CHECK_EQ(XhciRingEnqueue(&ring, &one, &pa), XHCI_RING_OK, "filler");
        CHECK_EQ(retire_success(&ring, pa), XHCI_RING_OK, "retired");
    }
    CHECK_EQ(ring.Enqueue, 0, "crossed the link again");
    CHECK_EQ(ring.Cycle, 1, "and toggled back");
    CHECK_EQ(mem[7].Control & XHCI_TRB_CH, 0,
             "a crossing between TDs clears the Chain flag again");
    CHECK_EQ(mem[7].Control & XHCI_TRB_CYCLE, 0,
             "link carries the cycle of the lap that just ended");
}

/*
 * A group of TDs published as one, which is what a control transfer is:
 * "Control transfers require two or three TDs to define them" (spec 6.4.1.2,
 * p.430). The property that matters is that the *group's* head is the single
 * publishing store - not each TD's - because the alternative lets the xHC begin
 * a control transfer whose Status Stage TRB does not exist yet.
 */
static void test_td_group(void)
{
    XHCI_TRB mem[16];
    XHCI_TRB trbs[4];
    XHCI_TD_GROUP_PLACEMENT placement;
    XHCI_RING ring;
    ULONG lengths[3];
    ULONG head;
    ULONG tail;
    ULONG i;

    CHECK_EQ(XhciRingInit(&ring, mem, RING_PA, 16, XHCI_RING_KIND_ENDPOINT),
             XHCI_RING_OK, "ring init");

    /* Setup(1) + Data(2) + Status(1), chained the way spec 4.11.7 requires
     * *within* each TD and not across the boundaries. */
    for (i = 0; i < 4; i++) {
        XhciTrbClear(&trbs[i]);
        trbs[i].Param0 = 0x0D000000UL + i;
        trbs[i].Control = XHCI_TRB_TYPE(XHCI_TRB_TYPE_NORMAL);
    }
    trbs[1].Control |= XHCI_TRB_CH;     /* first data TRB chains to the second */
    lengths[0] = 1;
    lengths[1] = 2;
    lengths[2] = 1;

    CHECK_EQ(XhciRingEnqueueTdGroup(&ring, trbs, 4, lengths, 3, &placement),
             XHCI_RING_OK, "three-TD group accepted");
    CHECK_EQ(placement.FirstIndex, 0, "group starts at index 0");
    CHECK_EQ(placement.LastIndex, 3, "and ends at index 3");
    CHECK_EQ(placement.TrbCount, 4, "four TRBs");
    CHECK_EQ(placement.FirstTrbPA, RING_PA, "head TRB address");
    CHECK_EQ(ring.Enqueue, 4, "enqueue advanced by the whole group");
    CHECK_EQ(XhciRingFree(&ring), 10, "and four slots are outstanding");

    /* Each TD is recoverable from the Chain flags on the ring, separately -
     * which is what makes an event naming any TRB resolvable to its own TD. */
    CHECK_EQ(XhciRingTdBounds(&ring, 0, &head, &tail), XHCI_RING_OK, "TD 0");
    CHECK_EQ(head, 0, "setup TD head");
    CHECK_EQ(tail, 0, "setup TD tail - a single TRB");
    CHECK_EQ(XhciRingTdBounds(&ring, 2, &head, &tail), XHCI_RING_OK, "TD 1");
    CHECK_EQ(head, 1, "data TD head found from its second TRB");
    CHECK_EQ(tail, 2, "data TD tail");
    CHECK_EQ(XhciRingTdBounds(&ring, 3, &head, &tail), XHCI_RING_OK, "TD 2");
    CHECK_EQ(head, 3, "status TD head");
    CHECK_EQ(tail, 3, "status TD tail");

    /* One event naming the last TRB retires all three TDs, because the retire
     * jumps the dequeue pointer past the matched tail rather than walking one
     * TD at a time. That is what makes IOC-on-the-Status-TRB-only work. */
    CHECK_EQ(retire_success(&ring, RING_PA + 3 * 16), XHCI_RING_OK,
             "status TRB event retires the group");
    CHECK_EQ(XhciRingFree(&ring), 14, "the whole group came back");
}

/*
 * The Link TRB's Chain flag at a crossing is the *preceding* TRB's, not "is
 * there more of this call to write". The two are the same thing only while a
 * group is a single TD, which is exactly the assumption Phase 6 broke: a
 * crossing that falls between a group's TDs must clear it.
 */
static void test_td_group_spanning_link(void)
{
    XHCI_TRB mem[8];
    XHCI_TRB trbs[3];
    XHCI_TRB one;
    XHCI_TD_GROUP_PLACEMENT placement;
    XHCI_RING ring;
    ULONG lengths[2];
    ULONG pa;
    ULONG i;

    CHECK_EQ(XhciRingInit(&ring, mem, RING_PA, 8, XHCI_RING_KIND_ENDPOINT),
             XHCI_RING_OK, "ring init");

    /* Park the enqueue pointer one slot before the Link TRB. */
    XhciTrbNoOpCommand(&one);
    for (i = 0; i < 6; i++) {
        CHECK_EQ(XhciRingEnqueue(&ring, &one, &pa), XHCI_RING_OK, "filler");
        CHECK_EQ(retire_success(&ring, pa), XHCI_RING_OK, "retired");
    }
    CHECK_EQ(ring.Enqueue, 6, "enqueue is one slot before the link");

    /* Two TDs of one TRB each: the crossing lands between them. */
    for (i = 0; i < 2; i++) {
        XhciTrbClear(&trbs[i]);
        trbs[i].Param0 = 0x0C000000UL + i;
        trbs[i].Control = XHCI_TRB_TYPE(XHCI_TRB_TYPE_NORMAL);
    }
    lengths[0] = 1;
    lengths[1] = 1;
    CHECK_EQ(XhciRingEnqueueTdGroup(&ring, trbs, 2, lengths, 2, &placement),
             XHCI_RING_OK, "group across the link accepted");
    CHECK_EQ(placement.FirstIndex, 6, "starts before the link");
    CHECK_EQ(placement.LastIndex, 0, "and ends after the wrap");
    CHECK_EQ(mem[7].Control & XHCI_TRB_CH, 0,
             "a crossing between two TDs of one group clears Chain");

    /* And the same crossing, mid-TD this time, sets it. */
    CHECK_EQ(retire_success(&ring, RING_PA + 0 * 16), XHCI_RING_OK, "drained");
    CHECK_EQ(ring.Enqueue, 1, "the group left the enqueue pointer past the wrap");
    for (i = 0; i < 5; i++) {
        CHECK_EQ(XhciRingEnqueue(&ring, &one, &pa), XHCI_RING_OK, "filler");
        CHECK_EQ(retire_success(&ring, pa), XHCI_RING_OK, "retired");
    }
    CHECK_EQ(ring.Enqueue, 6, "back to one slot before the link");
    trbs[0].Control |= XHCI_TRB_CH;
    lengths[0] = 2;
    CHECK_EQ(XhciRingEnqueueTdGroup(&ring, trbs, 2, lengths, 1, &placement),
             XHCI_RING_OK, "one two-TRB TD across the link accepted");
    CHECK_EQ(mem[7].Control & XHCI_TRB_CH, XHCI_TRB_CH,
             "a crossing inside a TD carries Chain (spec 4.11.7)");
}

static void test_td_group_refusals(void)
{
    XHCI_TRB mem[16];
    XHCI_TRB trbs[4];
    XHCI_TD_GROUP_PLACEMENT placement;
    XHCI_RING ring;
    ULONG lengths[3];
    ULONG i;

    CHECK_EQ(XhciRingInit(&ring, mem, RING_PA, 16, XHCI_RING_KIND_ENDPOINT),
             XHCI_RING_OK, "ring init");
    for (i = 0; i < 4; i++) {
        XhciTrbClear(&trbs[i]);
        trbs[i].Control = XHCI_TRB_TYPE(XHCI_TRB_TYPE_NORMAL);
    }

    lengths[0] = 2;
    lengths[1] = 2;
    /* No Chain flags at all: the declared TDs say TRB 0 continues into TRB 1. */
    CHECK_EQ(XhciRingEnqueueTdGroup(&ring, trbs, 4, lengths, 2, &placement),
             XHCI_RING_BAD_CHAIN, "declared TD boundary not marked by Chain");

    trbs[0].Control |= XHCI_TRB_CH;
    trbs[2].Control |= XHCI_TRB_CH;
    CHECK_EQ(XhciRingEnqueueTdGroup(&ring, trbs, 4, lengths, 2, &placement),
             XHCI_RING_OK, "and accepted once they agree");
    CHECK_EQ(ring.Enqueue, 4, "four TRBs written");

    /* The partition has to account for exactly the TRBs supplied. */
    lengths[0] = 1;
    lengths[1] = 1;
    CHECK_EQ(XhciRingEnqueueTdGroup(&ring, trbs, 4, lengths, 2, &placement),
             XHCI_RING_BAD_CHAIN, "lengths that do not sum to count");
    lengths[0] = 0;
    CHECK_EQ(XhciRingEnqueueTdGroup(&ring, trbs, 4, lengths, 2, &placement),
             XHCI_RING_BAD_CHAIN, "a zero-length TD");
    CHECK_EQ(XhciRingEnqueueTdGroup(&ring, trbs, 4, NULL, 2, &placement),
             XHCI_RING_BAD_PARAM, "no partition at all");
    CHECK_EQ(ring.Enqueue, 4, "no refusal wrote anything");
}

/*
 * The spec's own arithmetic for how many bytes a multi-TRB TD moved, which is
 * not "requested - residual": "the total number of received bytes for a Short
 * Packet TD is the sum of the TRB Transfer Length fields in all Transfer TRBs
 * up to and including the one that generated the Short Packet Event, minus the
 * residue value" (p.175).
 */
static void test_sum_trb_lengths(void)
{
    XHCI_TRB mem[8];
    XHCI_TRB td[3];
    XHCI_RING ring;
    ULONG bytes;
    ULONG pa;
    ULONG i;

    CHECK_EQ(XhciRingInit(&ring, mem, RING_PA, 8, XHCI_RING_KIND_ENDPOINT),
             XHCI_RING_OK, "ring init");

    build_td(td, 3);
    td[0].Status = 128;
    td[1].Status = 128;
    td[2].Status = 64;
    CHECK_EQ(XhciRingEnqueueTd(&ring, td, 3, NULL), XHCI_RING_OK, "TD queued");

    CHECK_EQ(XhciRingSumTrbLengths(&ring, 0, 0, &bytes), XHCI_RING_OK, "one");
    CHECK_EQ(bytes, 128, "first TRB alone");
    CHECK_EQ(XhciRingSumTrbLengths(&ring, 0, 1, &bytes), XHCI_RING_OK, "two");
    CHECK_EQ(bytes, 256, "through the second TRB");
    CHECK_EQ(XhciRingSumTrbLengths(&ring, 0, 2, &bytes), XHCI_RING_OK, "all");
    CHECK_EQ(bytes, 320, "the whole TD");

    /* Only the length field, not whatever else DW2 carries. */
    mem[1].Status |= XHCI_TRB_TD_SIZE(31) | XHCI_TRB_INTERRUPTER(3);
    CHECK_EQ(XhciRingSumTrbLengths(&ring, 0, 1, &bytes), XHCI_RING_OK, "masked");
    CHECK_EQ(bytes, 256, "TD Size and Interrupter Target are not length");

    CHECK_EQ(XhciRingSumTrbLengths(&ring, 2, 0, &bytes), XHCI_RING_NOT_ON_RING,
             "backwards is a caller error, not a walk the long way round");
    CHECK_EQ(XhciRingSumTrbLengths(&ring, 0, 7, &bytes), XHCI_RING_NOT_ON_RING,
             "the Link TRB is never a transfer TRB");
    CHECK_EQ(XhciRingSumTrbLengths(&ring, 0, 3, &bytes), XHCI_RING_NOT_ON_RING,
             "an index past the outstanding work");

    /* A retired TRB holds a previous lap's leftovers, so summing across one is
     * refused rather than answered. */
    CHECK_EQ(retire_success(&ring, RING_PA + 2 * 16), XHCI_RING_OK, "retired");
    CHECK_EQ(XhciRingSumTrbLengths(&ring, 0, 2, &bytes), XHCI_RING_NOT_ON_RING,
             "nothing is outstanding any more");

    /* Across the wrap the Link TRB is stepped over, not added. */
    for (i = 0; i < 3; i++) {
        CHECK_EQ(XhciRingEnqueue(&ring, &td[2], &pa), XHCI_RING_OK, "filler");
        CHECK_EQ(retire_success(&ring, pa), XHCI_RING_OK, "retired");
    }
    CHECK_EQ(ring.Enqueue, 6, "one slot before the link");
    CHECK_EQ(XhciRingEnqueueTd(&ring, td, 3, NULL), XHCI_RING_OK, "wrapped TD");
    CHECK_EQ(XhciRingSumTrbLengths(&ring, 6, 1, &bytes), XHCI_RING_OK, "wrap");
    CHECK_EQ(bytes, 320,
             "128 + 128 + 64 with the Link TRB's DW2 excluded");

    CHECK_EQ(XhciRingNextIndex(&ring, 6), 0,
             "the step after the last usable slot skips the Link TRB");
}

/*
 * A TD that does not fit must write *nothing*. usbport requeues the transfer
 * and retries; a partially written TD is not something the hardware can be
 * told to ignore later (docs/contributing/implementation-invariants.md).
 */
static void test_td_all_or_nothing(void)
{
    XHCI_TRB mem[8];
    XHCI_TRB td[8];
    XHCI_TRB one;
    XHCI_RING ring;
    ULONG i;
    ULONG enqueueBefore;
    ULONG cycleBefore;

    CHECK_EQ(XhciRingInit(&ring, mem, RING_PA, 8, XHCI_RING_KIND_ENDPOINT),
             XHCI_RING_OK, "ring init");
    build_td(td, 3);
    XhciTrbNoOpCommand(&one);

    /* Leave room for exactly two TRBs. */
    for (i = 0; i < 4; i++) {
        CHECK_EQ(XhciRingEnqueue(&ring, &one, NULL), XHCI_RING_OK, "filler");
    }
    CHECK_EQ(XhciRingFree(&ring), 2, "two slots left");

    enqueueBefore = ring.Enqueue;
    cycleBefore = ring.Cycle;
    mem[enqueueBefore].Control = 0xDEADBEEFUL;
    mem[enqueueBefore + 1].Control = 0xDEADBEEFUL;

    CHECK_EQ(XhciRingEnqueueTd(&ring, td, 3, NULL), XHCI_RING_FULL,
             "a TD one TRB too long is refused");
    CHECK_EQ(ring.Enqueue, enqueueBefore, "enqueue did not move");
    CHECK_EQ(ring.Cycle, cycleBefore, "cycle did not toggle");
    CHECK_EQ(mem[enqueueBefore].Control, 0xDEADBEEFUL,
             "not even the first TRB of the refused TD was written");
    CHECK_EQ(mem[enqueueBefore + 1].Control, 0xDEADBEEFUL, "nor the second");

    /* A two-TRB TD needs its own templates: the last TRB of a TD must not be
     * chained, so the first two TRBs of a three-TRB TD are not a valid TD. */
    build_td(td, 2);
    CHECK_EQ(XhciRingEnqueueTd(&ring, td, 2, NULL), XHCI_RING_OK,
             "a TD that exactly fits is accepted");
    CHECK_EQ(XhciRingFree(&ring), 0, "and fills the ring");

    /* A TD longer than the ring can ever hold is refused, not wrapped. */
    CHECK_EQ(XhciRingInit(&ring, mem, RING_PA, 8, XHCI_RING_KIND_ENDPOINT),
             XHCI_RING_OK, "fresh ring");
    build_td(td, 7);
    CHECK_EQ(XhciRingEnqueueTd(&ring, td, 7, NULL), XHCI_RING_FULL,
             "a TD longer than the ring capacity is refused");
    CHECK_EQ(ring.Enqueue, 0, "on an empty ring, still nothing written");

    CHECK_EQ(XhciRingEnqueueTd(&ring, td, 0, NULL), XHCI_RING_BAD_PARAM,
             "a zero-length TD is a caller bug");
    CHECK_EQ(XhciRingEnqueueTd(&ring, NULL, 3, NULL), XHCI_RING_BAD_PARAM,
             "a null TD is a caller bug");
}

/* ------------------------------------------------------------------ */
/* 6. Matching completions back to the ring and its TDs                */
/* ------------------------------------------------------------------ */

static void test_ring_retire(void)
{
    XHCI_TRB mem[8];
    XHCI_TRB trb;
    XHCI_RING ring;
    ULONG index;
    ULONG pa0;
    ULONG pa1;

    CHECK_EQ(XhciRingInit(&ring, mem, RING_PA, 8, XHCI_RING_KIND_ENDPOINT),
             XHCI_RING_OK, "ring init");
    XhciTrbNoOpCommand(&trb);
    CHECK_EQ(XhciRingEnqueue(&ring, &trb, &pa0), XHCI_RING_OK, "first");
    CHECK_EQ(XhciRingEnqueue(&ring, &trb, &pa1), XHCI_RING_OK, "second");

    CHECK_EQ(XhciRingIndexFromPA(&ring, RING_PA, &index), XHCI_RING_OK,
             "base address maps to index 0");
    CHECK_EQ(index, 0, "index 0");
    CHECK_EQ(XhciRingIndexFromPA(&ring, RING_PA + 112, &index), XHCI_RING_OK,
             "last TRB maps to index 7");
    CHECK_EQ(index, 7, "index 7");
    CHECK_EQ(XhciRingIndexFromPA(&ring, RING_PA - 16, &index),
             XHCI_RING_NOT_ON_RING, "address below the segment rejected");
    CHECK_EQ(XhciRingIndexFromPA(&ring, RING_PA + 128, &index),
             XHCI_RING_NOT_ON_RING, "address past the segment rejected");
    CHECK_EQ(XhciRingIndexFromPA(&ring, RING_PA + 8, &index),
             XHCI_RING_NOT_ON_RING, "unaligned address rejected");

    /*
     * A Command Completion Event carries the physical address of the command
     * TRB, and that is the only thing worth matching on. Everything that is
     * not an outstanding TRB of this ring - the Link TRB, a stale duplicate,
     * an address from somewhere else - is rejected rather than silently
     * moving the dequeue pointer.
     */
    CHECK_EQ(retire_success(&ring, RING_PA + 112), XHCI_RING_NOT_ON_RING,
             "the Link TRB never completes");
    CHECK_EQ(retire_success(&ring, pa1), XHCI_RING_OK,
             "the second TRB completes");
    /* Retiring TRB n means the hardware has consumed everything up to and
     * including n - the dequeue pointer is a position, not a set. */
    CHECK_EQ(ring.Dequeue, 2, "dequeue moves past the completed TRB");
    CHECK_EQ(retire_success(&ring, pa1), XHCI_RING_NOT_ON_RING,
             "a duplicate event is rejected");
    CHECK_EQ(retire_success(&ring, pa0), XHCI_RING_NOT_ON_RING,
             "an already-passed TRB is rejected");
    CHECK_EQ(ring.Dequeue, 2, "rejected retirement left dequeue alone");
    CHECK_EQ(retire_success(&ring, 0x0F009000UL), XHCI_RING_NOT_ON_RING,
             "an address on another ring is rejected");
    /*
     * Errors the xHC cannot attribute to a TRB - Ring Overrun, Ring Underrun -
     * arrive with the TRB Pointer set to zero, which "software shall treat as
     * invalid" (spec 4.11.3.1). It must not resolve to index 0.
     */
    CHECK_EQ(retire_success(&ring, 0), XHCI_RING_NOT_ON_RING,
             "a zero TRB pointer is not ring index 0");
}

/*
 * A Transfer Event reports "the address of the TRB that generated this event"
 * (spec Table 6-37), which for a multi-TRB TD is the last TRB via IOC, or an
 * earlier one when a short packet with ISP or an error happened there
 * (4.11.3.1). So the event pointer is not the TD's head, and matching it
 * against the head by equality would reject a legitimate completion. What the
 * ring layer must do instead is resolve the pointer to the TD that owns it.
 */
static void test_td_ownership(void)
{
    XHCI_TRB mem[8];
    XHCI_TRB td[3];
    XHCI_TRB one;
    XHCI_RING ring;
    XHCI_TD_COMPLETION completion;
    ULONG head;
    ULONG tail;
    ULONG headPA;

    CHECK_EQ(XhciRingInit(&ring, mem, RING_PA, 8, XHCI_RING_KIND_ENDPOINT),
             XHCI_RING_OK, "ring init");
    build_td(td, 3);
    CHECK_EQ(XhciRingEnqueueTd(&ring, td, 3, &headPA), XHCI_RING_OK, "TD 0-2");
    XhciTrbNoOpCommand(&one);
    CHECK_EQ(XhciRingEnqueue(&ring, &one, NULL), XHCI_RING_OK, "TD at 3");

    /* Every TRB of the TD resolves to the same pair of bounds. */
    CHECK_EQ(XhciRingTdBounds(&ring, 0, &head, &tail), XHCI_RING_OK, "from 0");
    CHECK_EQ(head, 0, "head from the head");
    CHECK_EQ(tail, 2, "tail from the head");
    CHECK_EQ(XhciRingTdBounds(&ring, 1, &head, &tail), XHCI_RING_OK, "from 1");
    CHECK_EQ(head, 0, "head from the middle - the walk goes backwards too");
    CHECK_EQ(tail, 2, "tail from the middle");
    CHECK_EQ(XhciRingTdBounds(&ring, 2, &head, &tail), XHCI_RING_OK, "from 2");
    CHECK_EQ(head, 0, "head from the tail");
    CHECK_EQ(tail, 2, "tail from the tail");

    /* The next TD is its own TD, and the walk back must stop at its boundary
     * rather than running into the chained TD in front of it. */
    CHECK_EQ(XhciRingTdBounds(&ring, 3, &head, &tail), XHCI_RING_OK, "from 3");
    CHECK_EQ(head, 3, "a single-TRB TD is its own head");
    CHECK_EQ(tail, 3, "and its own tail");

    /* Not outstanding: past the enqueue pointer, and the Link TRB. */
    CHECK_EQ(XhciRingTdBounds(&ring, 4, &head, &tail), XHCI_RING_NOT_ON_RING,
             "an unwritten slot owns nothing");
    CHECK_EQ(XhciRingTdBounds(&ring, 7, &head, &tail), XHCI_RING_NOT_ON_RING,
             "the Link TRB belongs to no TD");
    CHECK_EQ(XhciRingTdBounds(&ring, 0, NULL, NULL), XHCI_RING_OK,
             "both outputs are optional");

    /*
     * A *short packet* on TRB 1 does not retire anything. The xHC advances to
     * the next TD by itself, but that is not the same as relinquishing the
     * TRBs: "software shall not interpret a Short Packet Event as indicating
     * that the TD that it is associated with is 'complete', unless the TRB
     * Pointer field of the Transfer Event references the last TRB of the TD"
     * (4.10.1.1.2 p.175). Reclaiming here would free TRBs the controller is
     * still walking, and the next enqueue would overwrite them.
     *
     * **This rule stayed put when batch 8-V.2's receive fix landed.** The fix
     * completes a short packet mid-TD, but only for a TD the *transfer* layer
     * knows is entirely data - this layer cannot tell one from a control
     * transfer's Data Stage, whose Status Stage TD still has to run. The first
     * attempt relaxed `CanRetire` here and these checks are what caught it. The
     * departure has its own entry point instead; see
     * test_retire_advanced_td below.
     */
    CHECK_EQ(XhciRingClassifyEvent(&ring, RING_PA + 1 * 16,
                                   XHCI_CC_SHORT_PACKET, &completion),
             XHCI_RING_OK, "classified");
    CHECK_EQ(completion.CanRetire, 0, "a mid-TD short packet does not retire");
    CHECK_EQ(completion.NeedsRecovery, 0,
             "and does not recover either - the controller is still running");
    CHECK_EQ(retire_with_code(&ring, RING_PA + 1 * 16, XHCI_CC_SHORT_PACKET),
             XHCI_RING_NOT_COMPLETE, "and retiring on it is refused outright");
    CHECK_EQ(ring.Dequeue, 0, "not one slot was freed");
    CHECK_EQ(XhciRingFree(&ring), 2, "free count unchanged");

    /*
     * The tail event is the one that retires it, and the spec guarantees it
     * arrives: with ISP on the earlier TRBs and IOC on the last, "two events
     * shall be generated for the transfer ... a second for the last TRB with
     * the IOC flag set", itself reporting Short Packet (p.175).
     */
    CHECK_EQ(retire_with_code(&ring, RING_PA + 2 * 16, XHCI_CC_SHORT_PACKET),
             XHCI_RING_OK, "the trailing tail event retires the TD");
    CHECK_EQ(ring.Dequeue, 3, "the whole three-TRB TD went at once");
    CHECK_EQ(XhciRingFree(&ring), 5, "and its three slots came back");

    /*
     * One TD can generate several events: while advancing to the end of a TD
     * the xHC raises one for every further TRB whose IOC flag is set (spec
     * 4.11.3.1). Those trailing pointers land on TRBs that are no longer
     * outstanding and must be rejected without moving anything.
     */
    CHECK_EQ(retire_success(&ring, RING_PA + 2 * 16), XHCI_RING_NOT_ON_RING,
             "a trailing event for the retired TD changes nothing");
    CHECK_EQ(ring.Dequeue, 3, "dequeue held still");

    CHECK_EQ(retire_success(&ring, RING_PA + 3 * 16), XHCI_RING_OK,
             "the following single-TRB TD retires on its own");
    CHECK_EQ(ring.Dequeue, 4, "by exactly one slot");
    CHECK_EQ(XhciRingFree(&ring), 6, "ring empty again");
}

/*
 * `XhciRingRetireAdvancedTd` - the caller's "I know the xHC has finished with
 * this TD" retire, added for batch 8-V.2's receive fix.
 *
 * What it may do is jump the dequeue pointer past a TD the event did *not* name
 * the last TRB of. What keeps that from being the ring corruption `CanRetire`
 * exists to prevent is 4.11.5.2 p.210: on a short packet the xHC "shall advance to
 * the first TRB of the next TD or the Enqueue Pointer ... whichever is
 * encountered first", so it is provably no longer executing this TD. Nothing
 * else licenses it, which is why Short Packet on an endpoint ring is the only
 * combination accepted here - the *shape* test that decides when a short packet
 * really ends the transfer belongs to the transfer layer.
 */
static void test_retire_advanced_td(void)
{
    XHCI_TRB mem[8];
    XHCI_TRB td[3];
    XHCI_TRB one;
    XHCI_RING ring;
    XHCI_RING cmd;
    XHCI_TD_COMPLETION completion;
    ULONG headPA;

    CHECK_EQ(XhciRingInit(&ring, mem, RING_PA, 8, XHCI_RING_KIND_ENDPOINT),
             XHCI_RING_OK, "ring init");
    build_td(td, 3);
    CHECK_EQ(XhciRingEnqueueTd(&ring, td, 3, &headPA), XHCI_RING_OK, "TD 0-2");
    XhciTrbNoOpCommand(&one);
    CHECK_EQ(XhciRingEnqueue(&ring, &one, NULL), XHCI_RING_OK, "TD at 3");

    /* A short packet on TRB 1 - mid-TD, so the ordinary retire refuses it. */
    CHECK_EQ(XhciRingClassifyEvent(&ring, RING_PA + 1 * 16,
                                   XHCI_CC_SHORT_PACKET, &completion),
             XHCI_RING_OK, "classified");
    CHECK_EQ(completion.CanRetire, 0, "(mid-TD, as the positional rule says)");
    CHECK_EQ(XhciRingRetireTd(&ring, &completion), XHCI_RING_NOT_COMPLETE,
             "the positional retire still refuses it");

    /* Every code that is not Short Packet is refused, including the other two
     * that let the controller keep running by itself. */
    CHECK_EQ(XhciRingRetireAdvancedTd(&ring, &completion, XHCI_CC_SUCCESS),
             XHCI_RING_NOT_COMPLETE, "Success is not the licensed code");
    CHECK_EQ(XhciRingRetireAdvancedTd(&ring, &completion,
                                      XHCI_CC_MISSED_SERVICE),
             XHCI_RING_NOT_COMPLETE,
             "nor Missed Service, whose skipped TDs a later tail event sweeps");
    CHECK_EQ(XhciRingRetireAdvancedTd(&ring, &completion, XHCI_CC_STALL),
             XHCI_RING_NOT_COMPLETE, "nor an error, which halts the endpoint");
    CHECK_EQ(XhciRingRetireAdvancedTd(&ring, &completion, XHCI_CC_STOPPED),
             XHCI_RING_NOT_COMPLETE, "nor a stop, where software picks the "
             "resume position itself");
    CHECK_EQ(ring.Dequeue, 0, "and not one of those moved the dequeue pointer");
    CHECK_EQ(XhciRingRetireAdvancedTd(NULL, &completion, XHCI_CC_SHORT_PACKET),
             XHCI_RING_BAD_PARAM, "no ring");
    CHECK_EQ(XhciRingRetireAdvancedTd(&ring, NULL, XHCI_CC_SHORT_PACKET),
             XHCI_RING_BAD_PARAM, "no completion");

    /* The one accepted combination, and it advances past the TD's **tail**, not
     * past the TRB the event named - so the whole TD is reclaimed at once. */
    CHECK_EQ(completion.ReportedIndex, 1, "(the event named TRB 1)");
    CHECK_EQ(completion.TailIndex, 2, "(whose TD ends at TRB 2)");
    CHECK_EQ(XhciRingRetireAdvancedTd(&ring, &completion,
                                      XHCI_CC_SHORT_PACKET),
             XHCI_RING_OK, "a short packet on an endpoint ring retires");
    CHECK_EQ(ring.Dequeue, 3, "past the tail, not past the reported TRB");
    CHECK_EQ(XhciRingFree(&ring), 5, "all three slots came back");

    /* And a second call on the same, now stale, classification is refused
     * rather than dragging the dequeue pointer backwards. */
    CHECK_EQ(XhciRingRetireAdvancedTd(&ring, &completion,
                                      XHCI_CC_SHORT_PACKET),
             XHCI_RING_NOT_ON_RING, "the classification has gone stale");
    CHECK_EQ(ring.Dequeue, 3, "dequeue held still");

    /*
     * Not on a command ring: a command TD is one TRB, so "mid-TD" cannot arise,
     * and Table 6-90 gives Short Packet to transfer events only. Not on an
     * isochronous ring either - those retire by **group**
     * (`XhciRingRetireIsoGroup`), and a mid-TD isoch short packet reaching this
     * path would be a decision nobody has made. A refusal costs an
     * `OrphanedGroups` reading; the alternative costs a silently reclaimed TRB.
     *
     * *(The isoch half read "nothing submits on one until Phase 9" until
     * a later review. Phase 9 landed - this same file enqueues and retires
     * isochronous TDs a few vectors below - so the exclusion needed its real
     * reason rather than a deferral that had expired.)*
     */
    CHECK_EQ(XhciRingInit(&cmd, mem, RING_PA, 8, XHCI_RING_KIND_COMMAND),
             XHCI_RING_OK, "command ring init");
    build_td(td, 3);
    CHECK_EQ(XhciRingEnqueueTd(&cmd, td, 3, &headPA), XHCI_RING_OK, "TD 0-2");
    CHECK_EQ(XhciRingClassifyEvent(&cmd, RING_PA + 1 * 16,
                                   XHCI_CC_SUCCESS, &completion),
             XHCI_RING_OK, "classified on the command ring");
    CHECK_EQ(XhciRingRetireAdvancedTd(&cmd, &completion,
                                      XHCI_CC_SHORT_PACKET),
             XHCI_RING_NOT_COMPLETE, "refused on a command ring");
    CHECK_EQ(cmd.Dequeue, 0, "which moved nothing");

    CHECK_EQ(XhciRingInit(&cmd, mem, RING_PA, 8, XHCI_RING_KIND_ISOCH),
             XHCI_RING_OK, "isoch ring init");
    build_td(td, 3);
    CHECK_EQ(XhciRingEnqueueTd(&cmd, td, 3, &headPA), XHCI_RING_OK, "TD 0-2");
    CHECK_EQ(XhciRingClassifyEvent(&cmd, RING_PA + 1 * 16,
                                   XHCI_CC_SHORT_PACKET, &completion),
             XHCI_RING_OK, "classified on the isoch ring");
    CHECK_EQ(XhciRingRetireAdvancedTd(&cmd, &completion,
                                      XHCI_CC_SHORT_PACKET),
             XHCI_RING_NOT_COMPLETE, "refused on an isochronous ring");
    CHECK_EQ(cmd.Dequeue, 0, "which moved nothing either");
}

/* The same resolution across the Link TRB, from both sides of it. */
static void test_td_ownership_wrapped(void)
{
    XHCI_TRB mem[8];
    XHCI_TRB td[3];
    XHCI_TRB one;
    XHCI_RING ring;
    ULONG head;
    ULONG tail;
    ULONG pa;
    ULONG i;

    CHECK_EQ(XhciRingInit(&ring, mem, RING_PA, 8, XHCI_RING_KIND_ENDPOINT),
             XHCI_RING_OK, "ring init");
    XhciTrbNoOpCommand(&one);
    for (i = 0; i < 5; i++) {
        CHECK_EQ(XhciRingEnqueue(&ring, &one, &pa), XHCI_RING_OK, "filler");
        CHECK_EQ(retire_success(&ring, pa), XHCI_RING_OK, "retired");
    }
    build_td(td, 3);
    CHECK_EQ(XhciRingEnqueueTd(&ring, td, 3, &pa), XHCI_RING_OK,
             "TD at 5, 6, wrap, 0");
    CHECK_EQ(pa, RING_PA + 5 * 16, "head is at index 5");

    /* Indices 5, 6 and 0 are one TD; the walk has to cross the Link TRB in
     * both directions without treating it as a member. */
    CHECK_EQ(XhciRingTdBounds(&ring, 5, &head, &tail), XHCI_RING_OK, "from 5");
    CHECK_EQ(head, 5, "head");
    CHECK_EQ(tail, 0, "tail is on the far side of the link");
    CHECK_EQ(XhciRingTdBounds(&ring, 6, &head, &tail), XHCI_RING_OK, "from 6");
    CHECK_EQ(head, 5, "head from the middle");
    CHECK_EQ(tail, 0, "tail from the middle");
    CHECK_EQ(XhciRingTdBounds(&ring, 0, &head, &tail), XHCI_RING_OK, "from 0");
    CHECK_EQ(head, 5, "the walk back crossed the link to reach the head");
    CHECK_EQ(tail, 0, "tail from the tail");

    /* A completion reported against the tail, which is past the wrap. */
    CHECK_EQ(retire_success(&ring, RING_PA + 0 * 16), XHCI_RING_OK,
             "wrapped TD retires from its tail");
    CHECK_EQ(ring.Dequeue, 1, "dequeue is past the wrapped tail");
    CHECK_EQ(XhciRingFree(&ring), 6, "all three slots came back");

    /* And once more with a short packet reported against the TRB *before* the
     * link, so the deferral and then the retirement both straddle the wrap. */
    for (i = 0; i < 4; i++) {
        CHECK_EQ(XhciRingEnqueue(&ring, &one, &pa), XHCI_RING_OK, "filler");
        CHECK_EQ(retire_success(&ring, pa), XHCI_RING_OK, "retired");
    }
    CHECK_EQ(ring.Enqueue, 5, "back at index 5");
    CHECK_EQ(XhciRingEnqueueTd(&ring, td, 3, &pa), XHCI_RING_OK, "second TD");
    CHECK_EQ(retire_with_code(&ring, RING_PA + 6 * 16, XHCI_CC_SHORT_PACKET),
             XHCI_RING_NOT_COMPLETE, "reported against the TRB before the link");
    CHECK_EQ(ring.Dequeue, 5, "deferred: the wrapped TD is still outstanding");
    CHECK_EQ(retire_with_code(&ring, RING_PA + 0 * 16, XHCI_CC_SHORT_PACKET),
             XHCI_RING_OK, "the tail event arrives past the wrap");
    CHECK_EQ(ring.Dequeue, 1, "and retires through the wrapped tail");
}

/*
 * The case that makes retirement conditional rather than automatic: a TD with
 * IOC on an intermediate TRB *and* on its last.
 *
 * The spec offers those intermediate events as a way for software to "update
 * its Dequeue Pointer and reuse the TRBs that have been consumed by the xHC"
 * (4.11.7 p.214), and says the intermediate ones report Success. So a
 * successful intermediate event means the controller has passed that TRB - not
 * that it has finished the TD. Retiring the TD there would hand back TRBs the
 * controller is still executing, which is silent data corruption: the next
 * enqueue overwrites live work.
 *
 * This driver sets IOC only on a TD's last TRB, so an intermediate Success
 * event should never arrive - but "should never arrive" is exactly the input
 * worth pinning, because the failure is invisible.
 */
static void test_td_intermediate_ioc(void)
{
    XHCI_TRB mem[8];
    XHCI_TRB td[3];
    XHCI_RING ring;
    XHCI_TD_COMPLETION completion;
    ULONG i;

    CHECK_EQ(XhciRingInit(&ring, mem, RING_PA, 8, XHCI_RING_KIND_ENDPOINT),
             XHCI_RING_OK, "ring init");

    /* A three-TRB TD with IOC on TRB 1 as well as TRB 2. Chain flags stay
     * spec-shaped: set in all but the last. */
    for (i = 0; i < 3; i++) {
        XhciTrbClear(&td[i]);
        td[i].Control = XHCI_TRB_TYPE(XHCI_TRB_TYPE_NORMAL);
        if (i + 1 < 3) {
            td[i].Control |= XHCI_TRB_CH;
        }
    }
    td[1].Control |= XHCI_TRB_IOC;
    td[2].Control |= XHCI_TRB_IOC;
    CHECK_EQ(XhciRingEnqueueTd(&ring, td, 3, NULL), XHCI_RING_OK, "TD enqueued");
    CHECK_EQ(ring.Dequeue, 0, "dequeue starts at the TD head");

    /* The intermediate event: Success, reported against TRB 1. */
    CHECK_EQ(XhciRingClassifyEvent(&ring, RING_PA + 1 * 16, XHCI_CC_SUCCESS,
                                   &completion), XHCI_RING_OK, "classified");
    CHECK_EQ(completion.ReportedIndex, 1, "reported index");
    CHECK_EQ(completion.HeadIndex, 0, "owning TD's head");
    CHECK_EQ(completion.TailIndex, 2, "owning TD's tail");
    CHECK_EQ(completion.IsTail, 0, "TRB 1 is not the TD's last");
    CHECK_EQ(completion.CanRetire, 0,
             "an intermediate Success is progress, not completion");
    CHECK_EQ(completion.NeedsRecovery, 0, "and nothing is wrong");

    CHECK_EQ(XhciRingRetireTd(&ring, &completion), XHCI_RING_NOT_COMPLETE,
             "and cannot be retired even if a caller tries");
    CHECK_EQ(ring.Dequeue, 0, "so the dequeue pointer did not move");
    CHECK_EQ(XhciRingFree(&ring), 3, "and no slot came back");

    /* The tail event for the same TD: now the TD is complete. */
    CHECK_EQ(XhciRingClassifyEvent(&ring, RING_PA + 2 * 16, XHCI_CC_SUCCESS,
                                   &completion), XHCI_RING_OK, "classified");
    CHECK_EQ(completion.IsTail, 1, "TRB 2 is the TD's last");
    CHECK_EQ(completion.CanRetire, 1, "so it retires");
    CHECK_EQ(completion.NeedsRecovery, 0, "with nothing to recover");
    CHECK_EQ(XhciRingRetireTd(&ring, &completion), XHCI_RING_OK, "retired");
    CHECK_EQ(ring.Dequeue, 3, "past the whole TD");
    CHECK_EQ(XhciRingFree(&ring), 6, "all three slots came back");
}

/*
 * Errors and stops. Spec 4.11.7 p.214: "Software shall not interpret an error
 * Event as indicating that the TD that it is associated with is 'complete'
 * ... unless the TRB Pointer field of the error Transfer Event references the
 * last TRB of the TD." And a Stop Endpoint command "transfer[s] ownership of
 * all the TDs on the associated Transfer Ring to software", which then chooses
 * the resume position itself.
 */
static void test_completion_codes(void)
{
    XHCI_TRB mem[8];
    XHCI_TRB td[3];
    XHCI_RING ring;
    XHCI_TD_COMPLETION completion;

    build_td(td, 3);

    /* An error on an intermediate TRB: the xHC stopped on it, so the endpoint
     * needs recovery and the TD's slots are still the controller's. */
    CHECK_EQ(XhciRingInit(&ring, mem, RING_PA, 8, XHCI_RING_KIND_ENDPOINT),
             XHCI_RING_OK, "ring init");
    CHECK_EQ(XhciRingEnqueueTd(&ring, td, 3, NULL), XHCI_RING_OK, "TD");
    CHECK_EQ(XhciRingClassifyEvent(&ring, RING_PA + 1 * 16,
                                   XHCI_CC_USB_TRANSACTION_ERROR, &completion),
             XHCI_RING_OK, "classified");
    CHECK_EQ(completion.NeedsRecovery, 1, "a mid-TD error halts the endpoint");
    CHECK_EQ(completion.CanRetire, 0, "and reclaims nothing");
    CHECK_EQ(XhciRingRetireTd(&ring, &completion), XHCI_RING_NOT_COMPLETE,
             "so it refuses to be retired");
    CHECK_EQ(ring.Dequeue, 0, "dequeue held");

    /*
     * The same error on the TD's last TRB - the combination a single action
     * could not express. Both are true at once: the TD is complete, because
     * "ownership of all the TRBs of the TD have been relinquished by the xHC"
     * once the event names the last TRB (p.214), *and* the endpoint is halted,
     * because "all Transfer Ring error conditions force the state of the
     * associated endpoint to Halted and require system software intervention
     * to recover" (p.176). Reporting only the retire loses the Reset Endpoint;
     * reporting only the recovery strands three slots.
     */
    CHECK_EQ(XhciRingClassifyEvent(&ring, RING_PA + 2 * 16,
                                   XHCI_CC_USB_TRANSACTION_ERROR, &completion),
             XHCI_RING_OK, "classified");
    CHECK_EQ(completion.CanRetire, 1,
             "an error at the TD's last TRB does complete it");
    CHECK_EQ(completion.NeedsRecovery, 1,
             "and the endpoint is halted all the same");
    CHECK_EQ(XhciRingRetireTd(&ring, &completion), XHCI_RING_OK,
             "retiring is not gated on the recovery flag");
    CHECK_EQ(ring.Dequeue, 3, "past the whole TD");

    /*
     * And retiring first leaves the dequeue pointer exactly where the recovery
     * sequence has to point the hardware: "software shall use a Set TR Dequeue
     * Pointer Command to advance the Transfer Ring to the next TD" (p.172).
     */
    CHECK_EQ(XhciRingDequeuePA(&ring), RING_PA + 3 * 16,
             "which is the address Set TR Dequeue Pointer must carry");
    CHECK_EQ(XhciRingSetDequeue(&ring, XhciRingDequeuePA(&ring)), XHCI_RING_OK,
             "so programming it back is consistent, not a second move");
    CHECK_EQ(ring.Dequeue, 3, "and idempotent");

    /* A stall on a single-TRB TD: always its own last TRB, so it retires - and
     * a stall halts the endpoint too. */
    CHECK_EQ(XhciRingInit(&ring, mem, RING_PA, 8, XHCI_RING_KIND_ENDPOINT),
             XHCI_RING_OK, "ring init");
    build_td(td, 1);
    CHECK_EQ(XhciRingEnqueueTd(&ring, td, 1, NULL), XHCI_RING_OK, "one-TRB TD");
    CHECK_EQ(XhciRingClassifyEvent(&ring, RING_PA, XHCI_CC_STALL, &completion),
             XHCI_RING_OK, "classified");
    CHECK_EQ(completion.CanRetire, 1, "a single-TRB TD is its own last TRB");
    CHECK_EQ(completion.NeedsRecovery, 1, "and a stall still needs a reset");
    CHECK_EQ(XhciRingRetireTd(&ring, &completion), XHCI_RING_OK, "retired");
    CHECK_EQ(ring.Dequeue, 1, "by one slot");

    /* Stopped transfer events: software owns the ring and repositions it.
     * These never retire, even at a tail - where the dequeue pointer lands is
     * software's decision, not one event's. */
    CHECK_EQ(XhciRingInit(&ring, mem, RING_PA, 8, XHCI_RING_KIND_ENDPOINT),
             XHCI_RING_OK, "ring init");
    build_td(td, 3);
    CHECK_EQ(XhciRingEnqueueTd(&ring, td, 3, NULL), XHCI_RING_OK, "TD");
    CHECK_EQ(XhciRingClassifyEvent(&ring, RING_PA + 2 * 16, XHCI_CC_STOPPED,
                                   &completion), XHCI_RING_OK, "classified");
    CHECK_EQ(completion.IsTail, 1, "reported against the TD's last TRB");
    CHECK_EQ(completion.NeedsRecovery, 1,
             "Stopped at the tail still means software repositions the ring");
    CHECK_EQ(completion.CanRetire, 0, "and does not retire it on the way");
    CHECK_EQ(XhciRingClassifyEvent(&ring, RING_PA, XHCI_CC_STOPPED_SHORT_PACKET,
                                   &completion), XHCI_RING_OK, "classified");
    CHECK_EQ(completion.NeedsRecovery, 1, "and so does 28");
    CHECK_EQ(XhciRingClassifyEvent(&ring, RING_PA, XHCI_CC_COMMAND_ABORTED,
                                   &completion), XHCI_RING_BAD_COMPLETION,
             "Command Aborted cannot describe a transfer event");
    CHECK_EQ(XhciRingClassifyEvent(&ring, RING_PA,
                                   XHCI_CC_COMMAND_RING_STOPPED, &completion),
             XHCI_RING_BAD_COMPLETION,
             "Command Ring Stopped cannot describe a transfer event");
    CHECK_EQ(ring.Dequeue, 0, "none of them moved the dequeue pointer");

    /* Classification itself never mutates the ring. */
    CHECK_EQ(ring.Enqueue, 3, "enqueue untouched by classification");
    CHECK_EQ(XhciRingClassifyEvent(&ring, RING_PA, XHCI_CC_SUCCESS, NULL),
             XHCI_RING_BAD_PARAM, "a null output is a caller bug");
}

/*
 * Missed Service is not an endpoint in trouble - it is an isoch pipe
 * resynchronizing, and spec p.172 gives it the *same sentence* as a short
 * packet: "If a Missed Service Error occurs on an intermediate TRB of a TD of
 * an Isoch endpoint the xHC shall advance to the first TRB of the next TD or
 * the Enqueue Pointer ... whichever is encountered first". So it is not a
 * recovery case: software must not reset an endpoint or reprogram a dequeue
 * pointer while the controller is still advancing through the ring on its own.
 *
 * But "the xHC advances" is not "software may reclaim". Those are different
 * claims, and reading the first as the second is the bug this test exists to
 * hold down. The TRBs stay outstanding until an event names the TD's last TRB
 * (p.214, and p.188 for this exact case: the xHC "may automatically
 * advance to the next TD", yet software "will have to wait until the next IOC
 * flag is encountered by the endpoint before it can reclaim" the TD).
 */
static void test_missed_service(void)
{
    XHCI_TRB mem[8];
    XHCI_TRB td[3];
    XHCI_RING ring;
    XHCI_TD_COMPLETION completion;

    CHECK_EQ(XhciRingInit(&ring, mem, RING_PA, 8, XHCI_RING_KIND_ISOCH),
             XHCI_RING_OK, "ring init");
    build_td(td, 3);
    CHECK_EQ(XhciRingEnqueueTd(&ring, td, 3, NULL), XHCI_RING_OK, "isoch TD");

    CHECK_EQ(XhciRingClassifyEvent(&ring, RING_PA + 1 * 16,
                                   XHCI_CC_MISSED_SERVICE, &completion),
             XHCI_RING_OK, "classified");
    CHECK_EQ(completion.IsTail, 0, "reported against an intermediate TRB");
    CHECK_EQ(completion.CanRetire, 0,
             "an intermediate Missed Service does not retire - that would free "
             "TRBs the xHC is still walking");
    CHECK_EQ(completion.NeedsRecovery, 0,
             "and does not recover - it would reset a pipe that never halted");
    CHECK_EQ(XhciRingRetireTd(&ring, &completion), XHCI_RING_NOT_COMPLETE,
             "retiring on it is refused");
    CHECK_EQ(ring.Dequeue, 0, "no slots were freed by the intermediate event");
    CHECK_EQ(XhciRingFree(&ring), 3, "free count unchanged");

    /*
     * "The xHC shall not drop Events associated with TRBs as it attempts to
     * resynchronize an Isoch pipe, e.g. ... if IOC = '1' in an Event Data or
     * Normal TRB then it returns Missed Service Error" (p.201). This driver
     * sets IOC on every TD's last TRB, so the tail event does arrive, and it
     * is the one that retires the TD.
     */
    CHECK_EQ(retire_with_code(&ring, RING_PA + 2 * 16, XHCI_CC_MISSED_SERVICE),
             XHCI_RING_OK, "the tail event retires the skipped TD");
    CHECK_EQ(ring.Dequeue, 3, "past the whole TD");
    CHECK_EQ(XhciRingFree(&ring), 6, "all three slots came back at once");

    /* And a Missed Service that lands on the tail first retires immediately. */
    CHECK_EQ(XhciRingInit(&ring, mem, RING_PA, 8, XHCI_RING_KIND_ISOCH),
             XHCI_RING_OK, "ring init");
    CHECK_EQ(XhciRingEnqueueTd(&ring, td, 3, NULL), XHCI_RING_OK, "isoch TD");
    CHECK_EQ(retire_with_code(&ring, RING_PA + 2 * 16, XHCI_CC_MISSED_SERVICE),
             XHCI_RING_OK, "Missed Service at the tail retires by itself");
    CHECK_EQ(ring.Dequeue, 3, "past the whole TD");
}

/*
 * Isoch endpoints never halt, so no *transfer error* on an isoch ring asks for
 * recovery. **TRB Error is the exception and it has its own vector** below
 * (`test_isoch_trb_error_still_recovers`): it is not a transfer error at all
 * but the xHC reporting a malformed TRB, so p.177's isoch exemption does not
 * reach it and recovery is still owed. *(This sentence said "no completion code
 * on an isoch ring asks for recovery" until the post-Phase 13 review rounds, which this file's own
 * next vector contradicts.)* "An isoch end point never halts because there is no handshake to
 * report a halt condition. Errors are reported as a Completion Code associated
 * with a TRB for an isochronous transfer, but an isoch pipe is not halted in an
 * error case. If an error is detected, the xHC shall continue to process the
 * data associated with the next ESIT of the transfer" (p.177), and again at
 * 4.10.2.8 p.184: "an Isoch endpoint shall not halt due to a Data Transaction
 * error, but instead shall advance to the next Isoch TD and attempt to execute
 * it during the next ESIT".
 *
 * That is the exception to p.177's own preceding sentence - "all Transfer Ring
 * error conditions force the state of the associated endpoint to Halted" - so
 * the two ring kinds must answer differently for the *same* completion code.
 * Resetting an isoch endpoint on a USB Transaction Error would reposition a
 * pipe the controller is still running; p.188 spells out that case: the error
 * generates an event "where the Isoch pipe does not stall, but advances to the
 * next Isoch TD in preparation for the next Interval".
 */
static void test_isoch_errors_never_halt(void)
{
    /* TRB Error is deliberately **not** in this list - see
     * `test_isoch_trb_error_still_recovers`. It is the one error that does not
     * halt an endpoint of any type, so p.177's isoch exemption does not reach
     * it. */
    static const ULONG commonCodes[3] = {
        XHCI_CC_USB_TRANSACTION_ERROR,
        XHCI_CC_DATA_BUFFER_ERROR,
        XHCI_CC_BABBLE
    };
    static const ULONG isochCodes[2] = {
        XHCI_CC_BANDWIDTH_OVERRUN,
        XHCI_CC_ISOCH_BUFFER_OVERRUN
    };
    XHCI_TRB mem[8];
    XHCI_TRB td[3];
    XHCI_RING isoch;
    XHCI_RING endpoint;
    XHCI_TD_COMPLETION completion;
    ULONG i;

    build_td(td, 3);

    for (i = 0; i < 3; i++) {
        CHECK_EQ(XhciRingInit(&isoch, mem, RING_PA, 8, XHCI_RING_KIND_ISOCH),
                 XHCI_RING_OK, "isoch ring init");
        CHECK_EQ(XhciRingEnqueueTd(&isoch, td, 3, NULL), XHCI_RING_OK, "TD");

        /* Mid-TD: defer, exactly like Missed Service. The controller carries
         * on to the next Isoch TD by itself. */
        CHECK_EQ(XhciRingClassifyEvent(&isoch, RING_PA + 1 * 16,
                                       commonCodes[i],
                                       &completion),
                 XHCI_RING_OK, "classified");
        CHECK_EQ(completion.NeedsRecovery, 0,
                 "an isoch error never halts the pipe, so nothing to recover");
        CHECK_EQ(completion.CanRetire, 0, "and mid-TD it retires nothing");
        CHECK_EQ(XhciRingRetireTd(&isoch, &completion), XHCI_RING_NOT_COMPLETE,
                 "refused");
        CHECK_EQ(isoch.Dequeue, 0, "dequeue held");

        /* At the tail: retire, still with no recovery. */
        CHECK_EQ(XhciRingClassifyEvent(&isoch, RING_PA + 2 * 16,
                                       commonCodes[i],
                                       &completion),
                 XHCI_RING_OK, "classified");
        CHECK_EQ(completion.CanRetire, 1, "the tail event completes the TD");
        CHECK_EQ(completion.NeedsRecovery, 0, "and still asks for no reset");
        CHECK_EQ(XhciRingRetireTd(&isoch, &completion), XHCI_RING_OK, "retired");
        CHECK_EQ(isoch.Dequeue, 3, "past the whole TD");

        /*
         * The same code on a control/bulk/interrupt ring, in the same shape,
         * must answer the other way. This is the pair that a kind-blind
         * classifier gets wrong in one direction or the other.
         */
        CHECK_EQ(XhciRingInit(&endpoint, mem, RING_PA, 8,
                              XHCI_RING_KIND_ENDPOINT),
                 XHCI_RING_OK, "endpoint ring init");
        CHECK_EQ(XhciRingEnqueueTd(&endpoint, td, 3, NULL), XHCI_RING_OK, "TD");
        CHECK_EQ(XhciRingClassifyEvent(&endpoint, RING_PA + 1 * 16,
                                       commonCodes[i],
                                       &completion),
                 XHCI_RING_OK, "classified");
        CHECK_EQ(completion.NeedsRecovery, 1,
                 "the same error does halt a non-isoch endpoint");
        CHECK_EQ(completion.CanRetire, 0, "and mid-TD it still retires nothing");
        CHECK_EQ(XhciRingClassifyEvent(&endpoint, RING_PA + 2 * 16,
                                       commonCodes[i],
                                       &completion),
                 XHCI_RING_OK, "classified");
        CHECK_EQ(completion.CanRetire, 1, "at the tail the TD is complete");
        CHECK_EQ(completion.NeedsRecovery, 1, "and the endpoint is still halted");
    }

    /*
     * Codes 18 and 31 prove the other half of the type boundary: they have the
     * same non-halting isoch semantics, but cannot be reinterpreted as errors
     * on a control, bulk or interrupt endpoint.
     */
    for (i = 0; i < 2; i++) {
        CHECK_EQ(XhciRingInit(&isoch, mem, RING_PA, 8, XHCI_RING_KIND_ISOCH),
                 XHCI_RING_OK, "isoch ring init");
        CHECK_EQ(XhciRingEnqueueTd(&isoch, td, 3, NULL), XHCI_RING_OK, "TD");
        CHECK_EQ(XhciRingClassifyEvent(&isoch, RING_PA + 2 * 16,
                                       isochCodes[i], &completion),
                 XHCI_RING_OK, "isoch-only code accepted");
        CHECK_EQ(completion.CanRetire, 1, "tail ownership returned");
        CHECK_EQ(completion.NeedsRecovery, 0, "isoch pipe did not halt");

        CHECK_EQ(XhciRingInit(&endpoint, mem, RING_PA, 8,
                              XHCI_RING_KIND_ENDPOINT),
                 XHCI_RING_OK, "endpoint ring init");
        CHECK_EQ(XhciRingEnqueueTd(&endpoint, td, 3, NULL), XHCI_RING_OK, "TD");
        CHECK_EQ(XhciRingClassifyEvent(&endpoint, RING_PA + 2 * 16,
                                       isochCodes[i], &completion),
                 XHCI_RING_BAD_COMPLETION,
                 "isoch-only code refused on a non-isoch endpoint");
    }

    /* Stop Endpoint reaches isoch endpoints too - that is software stopping
     * the ring, not the pipe halting itself. */
    CHECK_EQ(XhciRingInit(&isoch, mem, RING_PA, 8, XHCI_RING_KIND_ISOCH),
             XHCI_RING_OK, "isoch ring init");
    CHECK_EQ(XhciRingEnqueueTd(&isoch, td, 3, NULL), XHCI_RING_OK, "TD");
    CHECK_EQ(XhciRingClassifyEvent(&isoch, RING_PA + 2 * 16, XHCI_CC_STOPPED,
                                   &completion), XHCI_RING_OK, "classified");
    CHECK_EQ(completion.NeedsRecovery, 1,
             "transfer stopped codes apply to every transfer ring kind");
    CHECK_EQ(completion.CanRetire, 0, "and software chooses the resume point");
    CHECK_EQ(XhciRingClassifyEvent(&isoch, RING_PA,
                                   XHCI_CC_COMMAND_RING_STOPPED, &completion),
             XHCI_RING_BAD_COMPLETION,
             "command stopped code refused on an isoch transfer ring");
}

/*
 * The exception to the exception, and the ninth review round's finding: an
 * isoch endpoint is exempt from *halting*, not from the Error state.
 *
 * p.177 exempts an isoch pipe because it "never halts because there is no
 * handshake to report a halt condition" - a statement about the Halted state,
 * which is what the preceding sentence ("all Transfer Ring error conditions
 * force the state of the associated endpoint to Halted") puts every other error
 * in. A TRB Error was never in that sentence: 4.8.3 p.149 says it "should cause
 * a Running Endpoint to transition to the Error state", with no qualification by
 * endpoint type, and "a Set TR Dequeue Pointer Command shall be used to
 * transition the endpoint to the Stopped state".
 *
 * Table 6-8 encodes Halted and Error separately, so the two claims are about
 * different states and both hold. An isoch endpoint that took a TRB Error is
 * therefore sitting in Error, running nothing, and no ESIT advances it out -
 * which is the one isoch case where the classifier must ask for recovery. The
 * classifier answering 0 here is what left it stuck, and the reason the
 * behaviour is *only* visible as a code-level defect today is that this phase
 * refuses to open isoch endpoints at all.
 *
 * Position is not part of the question - a state is a state wherever the event
 * landed - so both a mid-TD and a tail TRB Error ask for it, while CanRetire
 * stays positional exactly as everywhere else.
 */
static void test_isoch_trb_error_still_recovers(void)
{
    XHCI_TRB mem[8];
    XHCI_TRB td[3];
    XHCI_RING isoch;
    XHCI_RING endpoint;
    XHCI_TD_COMPLETION completion;

    build_td(td, 3);

    CHECK_EQ(XhciRingInit(&isoch, mem, RING_PA, 8, XHCI_RING_KIND_ISOCH),
             XHCI_RING_OK, "isoch ring init");
    CHECK_EQ(XhciRingEnqueueTd(&isoch, td, 3, NULL), XHCI_RING_OK, "isoch TD");

    CHECK_EQ(XhciRingClassifyEvent(&isoch, RING_PA + 1 * 16, XHCI_CC_TRB_ERROR,
                                   &completion),
             XHCI_RING_OK, "classified");
    CHECK_EQ(completion.NeedsRecovery, 1,
             "a TRB Error puts an isoch endpoint in Error, which needs a Set TR "
             "Dequeue Pointer");
    CHECK_EQ(completion.CanRetire, 0, "and mid-TD it still retires nothing");
    CHECK_EQ(XhciRingRetireTd(&isoch, &completion), XHCI_RING_NOT_COMPLETE,
             "refused");
    CHECK_EQ(isoch.Dequeue, 0, "dequeue held");

    CHECK_EQ(XhciRingClassifyEvent(&isoch, RING_PA + 2 * 16, XHCI_CC_TRB_ERROR,
                                   &completion),
             XHCI_RING_OK, "classified");
    CHECK_EQ(completion.CanRetire, 1, "the tail event completes the TD");
    CHECK_EQ(completion.NeedsRecovery, 1, "and still asks for recovery");
    CHECK_EQ(XhciRingRetireTd(&isoch, &completion), XHCI_RING_OK, "retired");
    CHECK_EQ(isoch.Dequeue, 3, "past the whole TD");

    /* The same code on a control/bulk/interrupt ring answers the same way, and
     * that is the point: this is the one error code where the two ring kinds
     * agree. Every other one differs - `test_isoch_errors_never_halt` is that
     * pair. */
    CHECK_EQ(XhciRingInit(&endpoint, mem, RING_PA, 8, XHCI_RING_KIND_ENDPOINT),
             XHCI_RING_OK, "endpoint ring init");
    CHECK_EQ(XhciRingEnqueueTd(&endpoint, td, 3, NULL), XHCI_RING_OK, "TD");
    CHECK_EQ(XhciRingClassifyEvent(&endpoint, RING_PA + 1 * 16,
                                   XHCI_CC_TRB_ERROR, &completion),
             XHCI_RING_OK, "classified");
    CHECK_EQ(completion.NeedsRecovery, 1, "the same answer on a non-isoch ring");
    CHECK_EQ(completion.CanRetire, 0, "and mid-TD it retires nothing there too");

    /* Bandwidth Overrun and Isoch Buffer Overrun are still not recovery cases:
     * the correction is TRB Error's alone, not "isoch errors recover now". */
    CHECK_EQ(XhciRingInit(&isoch, mem, RING_PA, 8, XHCI_RING_KIND_ISOCH),
             XHCI_RING_OK, "isoch ring init");
    CHECK_EQ(XhciRingEnqueueTd(&isoch, td, 3, NULL), XHCI_RING_OK, "TD");
    CHECK_EQ(XhciRingClassifyEvent(&isoch, RING_PA + 2 * 16,
                                   XHCI_CC_ISOCH_BUFFER_OVERRUN, &completion),
             XHCI_RING_OK, "classified");
    CHECK_EQ(completion.NeedsRecovery, 0, "an overrun still halts nothing");
}

/*
 * A command ring has no endpoint, so a failed command is a result rather than
 * a fault: the ring keeps running and the next command executes. Only an abort
 * or a stop hands it back to software. Getting this wrong the other way would
 * have the command engine try to "recover" an endpoint that does not exist
 * every time a command returned a non-Success code.
 */
static void test_command_ring_errors(void)
{
    XHCI_TRB mem[8];
    XHCI_TRB td[1];
    XHCI_RING ring;
    XHCI_TD_COMPLETION completion;

    build_td(td, 1);
    CHECK_EQ(XhciRingInit(&ring, mem, RING_PA, 8, XHCI_RING_KIND_COMMAND),
             XHCI_RING_OK, "command ring init");
    CHECK_EQ(XhciRingEnqueueTd(&ring, td, 1, NULL), XHCI_RING_OK, "command");

    CHECK_EQ(XhciRingClassifyEvent(&ring, RING_PA, XHCI_CC_TRB_ERROR,
                                   &completion), XHCI_RING_OK, "classified");
    CHECK_EQ(completion.CanRetire, 1,
             "a command is a single-TRB TD, so its slot comes back");
    CHECK_EQ(completion.NeedsRecovery, 0,
             "and a failed command does not halt a command ring");
    CHECK_EQ(XhciRingRetireTd(&ring, &completion), XHCI_RING_OK, "retired");
    CHECK_EQ(ring.Dequeue, 1, "by one slot");

    /* An abort is different: software placed the command ring's dequeue
     * pointer itself, via CRCR.CA and then a restart. */
    CHECK_EQ(XhciRingEnqueueTd(&ring, td, 1, NULL), XHCI_RING_OK, "command");
    CHECK_EQ(XhciRingClassifyEvent(&ring, RING_PA + 1 * 16,
                                   XHCI_CC_COMMAND_ABORTED, &completion),
             XHCI_RING_OK, "classified");
    CHECK_EQ(completion.NeedsRecovery, 1, "an aborted command ring is stopped");
    CHECK_EQ(completion.CanRetire, 0, "and does not retire on this event");
    CHECK_EQ(XhciRingClassifyEvent(&ring, RING_PA + 1 * 16, XHCI_CC_STOPPED,
                                   &completion), XHCI_RING_BAD_COMPLETION,
             "transfer stopped code refused on a command ring");
}

/*
 * Table 6-90 is also an event-family contract. Rejecting an impossible code is
 * safer than translating it into ownership or recovery action on an unrelated
 * ring. The two vendor ranges are the intentional exception: unknown vendor
 * information is Success, while an unknown vendor error is Undefined Error.
 */
static void test_completion_code_domains(void)
{
    static const ULONG invalidEndpointCodes[13] = {
        XHCI_CC_INVALID,
        XHCI_CC_RESOURCE_ERROR,
        XHCI_CC_RING_UNDERRUN,
        XHCI_CC_RING_OVERRUN,
        XHCI_CC_VF_EVENT_RING_FULL,
        XHCI_CC_BANDWIDTH_OVERRUN,
        XHCI_CC_EVENT_RING_FULL,
        XHCI_CC_MISSED_SERVICE,
        XHCI_CC_COMMAND_RING_STOPPED,
        XHCI_CC_ISOCH_BUFFER_OVERRUN,
        37,
        191,
        256
    };
    static const ULONG invalidCommandCodes[5] = {
        XHCI_CC_DATA_BUFFER_ERROR,
        XHCI_CC_SHORT_PACKET,
        XHCI_CC_MISSED_SERVICE,
        XHCI_CC_STOPPED,
        XHCI_CC_ISOCH_BUFFER_OVERRUN
    };
    XHCI_TRB mem[8];
    XHCI_TRB td[3];
    XHCI_RING ring;
    XHCI_TD_COMPLETION completion;
    ULONG i;

    build_td(td, 3);
    CHECK_EQ(XhciRingInit(&ring, mem, RING_PA, 8, XHCI_RING_KIND_ENDPOINT),
             XHCI_RING_OK, "endpoint ring init");
    CHECK_EQ(XhciRingEnqueueTd(&ring, td, 3, NULL), XHCI_RING_OK, "TD");
    for (i = 0; i < 13; i++) {
        CHECK_EQ(XhciRingClassifyEvent(&ring, RING_PA + 2 * 16,
                                       invalidEndpointCodes[i], &completion),
                 XHCI_RING_BAD_COMPLETION,
                 "non-transfer completion refused on endpoint ring");
    }
    CHECK_EQ(ring.Dequeue, 0, "rejected codes changed no ownership");

    CHECK_EQ(XhciRingClassifyEvent(&ring, RING_PA + 1 * 16,
                                   XHCI_CC_VENDOR_INFO_MIN, &completion),
             XHCI_RING_OK, "vendor info accepted");
    CHECK_EQ(completion.CanRetire, 0, "mid-TD vendor info is progress");
    CHECK_EQ(completion.NeedsRecovery, 0, "vendor info means Success");
    CHECK_EQ(XhciRingClassifyEvent(&ring, RING_PA + 2 * 16,
                                   XHCI_CC_VENDOR_INFO_MAX, &completion),
             XHCI_RING_OK, "top vendor info accepted");
    CHECK_EQ(completion.CanRetire, 1, "tail vendor info retires");
    CHECK_EQ(completion.NeedsRecovery, 0, "top vendor info also means Success");

    CHECK_EQ(XhciRingInit(&ring, mem, RING_PA, 8, XHCI_RING_KIND_ENDPOINT),
             XHCI_RING_OK, "endpoint ring init");
    CHECK_EQ(XhciRingEnqueueTd(&ring, td, 3, NULL), XHCI_RING_OK, "TD");
    CHECK_EQ(XhciRingClassifyEvent(&ring, RING_PA + 2 * 16,
                                   XHCI_CC_VENDOR_ERROR_MIN, &completion),
             XHCI_RING_OK, "vendor error accepted");
    CHECK_EQ(completion.CanRetire, 1, "tail vendor error relinquishes the TD");
    CHECK_EQ(completion.NeedsRecovery, 1,
             "unknown vendor error means fatal Undefined Error");
    CHECK_EQ(XhciRingClassifyEvent(&ring, RING_PA + 2 * 16,
                                   XHCI_CC_VENDOR_ERROR_MAX, &completion),
             XHCI_RING_OK, "top vendor error accepted");
    CHECK_EQ(completion.CanRetire, 1, "top vendor error relinquishes the TD");
    CHECK_EQ(completion.NeedsRecovery, 1,
             "top vendor error also means fatal Undefined Error");

    build_td(td, 1);
    CHECK_EQ(XhciRingInit(&ring, mem, RING_PA, 8, XHCI_RING_KIND_COMMAND),
             XHCI_RING_OK, "command ring init");
    CHECK_EQ(XhciRingEnqueueTd(&ring, td, 1, NULL), XHCI_RING_OK, "command");
    for (i = 0; i < 5; i++) {
        CHECK_EQ(XhciRingClassifyEvent(&ring, RING_PA,
                                       invalidCommandCodes[i], &completion),
                 XHCI_RING_BAD_COMPLETION,
                 "transfer completion refused on command ring");
    }
    CHECK_EQ(XhciRingClassifyEvent(&ring, RING_PA,
                                   XHCI_CC_VENDOR_INFO_MIN, &completion),
             XHCI_RING_OK, "vendor info accepted on command ring");
    CHECK_EQ(completion.CanRetire, 1, "vendor info completes the command");
    CHECK_EQ(completion.NeedsRecovery, 0, "vendor info does not stop commands");
    CHECK_EQ(XhciRingClassifyEvent(&ring, RING_PA,
                                   XHCI_CC_USB_TRANSACTION_ERROR, &completion),
             XHCI_RING_OK, "Address Device transaction error is a command result");
    CHECK_EQ(completion.CanRetire, 1, "failed Address Device command completes");
    CHECK_EQ(completion.NeedsRecovery, 0,
             "Address Device failure does not stop the command ring");
    CHECK_EQ(XhciRingClassifyEvent(&ring, RING_PA,
                                   XHCI_CC_VF_EVENT_RING_FULL, &completion),
             XHCI_RING_OK, "Force Event failure is a command result");
    CHECK_EQ(completion.CanRetire, 1, "failed Force Event command completes");
    CHECK_EQ(completion.NeedsRecovery, 0,
             "Force Event failure does not stop the command ring");
}

/*
 * The other half of deferral: what happens when the tail event never comes.
 *
 * Some controllers drop it. Without the Contiguous Frame ID Capability an xHC
 * "may not generate a Missed Service Error Transfer Event for every ESIT
 * missed" (p.187), and an Event Ring full condition suppresses them too. A
 * deferred TD must not become a permanent hole in the ring, so retiring a
 * *later* TD has to sweep it up - which it does, because XhciRingRetireTd
 * jumps the dequeue pointer to just past the retired TD's tail rather than
 * walking one TD at a time. Reaching an event for a later TD is proof the
 * controller is past the earlier one.
 */
static void test_deferred_td_swept_by_later_event(void)
{
    XHCI_TRB mem[8];
    XHCI_TRB td[2];
    XHCI_RING ring;
    XHCI_TD_COMPLETION completion;

    CHECK_EQ(XhciRingInit(&ring, mem, RING_PA, 8, XHCI_RING_KIND_ISOCH),
             XHCI_RING_OK, "ring init");
    build_td(td, 2);
    CHECK_EQ(XhciRingEnqueueTd(&ring, td, 2, NULL), XHCI_RING_OK, "TD at 0-1");
    CHECK_EQ(XhciRingEnqueueTd(&ring, td, 2, NULL), XHCI_RING_OK, "TD at 2-3");
    CHECK_EQ(XhciRingEnqueueTd(&ring, td, 2, NULL), XHCI_RING_OK, "TD at 4-5");

    /* TD 0-1 is skipped mid-TD and its tail event is dropped by the xHC. */
    CHECK_EQ(XhciRingClassifyEvent(&ring, RING_PA + 0 * 16,
                                   XHCI_CC_MISSED_SERVICE, &completion),
             XHCI_RING_OK, "classified");
    CHECK_EQ(completion.CanRetire, 0, "deferred");
    CHECK_EQ(completion.NeedsRecovery, 0, "and no reset asked for");
    CHECK_EQ(ring.Dequeue, 0, "still outstanding");

    /* TD 2-3 is skipped too, and its tail event *is* posted. Retiring it
     * reclaims the deferred TD ahead of it in the same store. */
    CHECK_EQ(retire_with_code(&ring, RING_PA + 3 * 16, XHCI_CC_MISSED_SERVICE),
             XHCI_RING_OK, "a later TD's tail event arrives");
    CHECK_EQ(ring.Dequeue, 4, "it swept the deferred TD up with it");
    CHECK_EQ(XhciRingFree(&ring), 4, "all four slots came back");

    /* The deferred TD's own TRBs are no longer resolvable, so a late event for
     * it is rejected rather than moving the dequeue pointer backwards. */
    CHECK_EQ(XhciRingClassifyEvent(&ring, RING_PA + 1 * 16,
                                   XHCI_CC_MISSED_SERVICE, &completion),
             XHCI_RING_NOT_ON_RING, "a late tail event for the swept TD");
    CHECK_EQ(ring.Dequeue, 4, "dequeue held");

    CHECK_EQ(retire_success(&ring, RING_PA + 5 * 16), XHCI_RING_OK,
             "the surviving TD retires normally");
    CHECK_EQ(ring.Dequeue, 6, "by exactly its own length");
}

/*
 * The RECOVER path's other half: after Reset Endpoint or Stop Endpoint,
 * software chooses where execution resumes and programs the same address into
 * a Set TR Dequeue Pointer command (spec p.116). XhciRingDequeuePA is the one
 * place that value comes from, so the two pointers cannot drift apart.
 */
static void test_set_dequeue(void)
{
    XHCI_TRB mem[8];
    XHCI_TRB td[3];
    XHCI_RING ring;

    CHECK_EQ(XhciRingInit(&ring, mem, RING_PA, 8, XHCI_RING_KIND_ENDPOINT),
             XHCI_RING_OK, "ring init");
    CHECK_EQ(XhciRingDequeuePA(&ring), RING_PA, "a fresh ring dequeues at 0");

    build_td(td, 3);
    CHECK_EQ(XhciRingEnqueueTd(&ring, td, 3, NULL), XHCI_RING_OK, "TD");
    CHECK_EQ(XhciRingEnqueueTd(&ring, td, 3, NULL), XHCI_RING_OK, "another");
    CHECK_EQ(XhciRingFree(&ring), 0, "ring full");

    /* Recovery abandons the first TD and resumes at the second. */
    CHECK_EQ(XhciRingSetDequeue(&ring, RING_PA + 3 * 16), XHCI_RING_OK,
             "dequeue placed at the second TD");
    CHECK_EQ(ring.Dequeue, 3, "index moved");
    CHECK_EQ(XhciRingDequeuePA(&ring), RING_PA + 3 * 16,
             "and reads back as the address to program");
    CHECK_EQ(XhciRingFree(&ring), 3, "the abandoned TD's slots came back");

    CHECK_EQ(XhciRingSetDequeue(&ring, RING_PA + 7 * 16), XHCI_RING_NOT_ON_RING,
             "the Link TRB is not a resume position");
    CHECK_EQ(XhciRingSetDequeue(&ring, RING_PA + 8), XHCI_RING_NOT_ON_RING,
             "nor is an unaligned address");
    CHECK_EQ(XhciRingSetDequeue(&ring, 0x0F009000UL), XHCI_RING_NOT_ON_RING,
             "nor one on another ring");
    CHECK_EQ(ring.Dequeue, 3, "and a refused placement changes nothing");

    /*
     * Backwards, into slots already reclaimed. Accepting this would make those
     * slots outstanding again: the free count would drop with nothing left to
     * retire them, and the ring would report full for good.
     */
    CHECK_EQ(XhciRingSetDequeue(&ring, RING_PA), XHCI_RING_NOT_ON_RING,
             "a position behind the dequeue pointer is refused");
    CHECK_EQ(ring.Dequeue, 3, "dequeue held");
    CHECK_EQ(XhciRingFree(&ring), 3, "and the free count did not shrink");

    /* Mid-TD. "The xHC shall assume that the modified Dequeue Pointer
     * references the first TRB of a TD" (spec p.172). */
    CHECK_EQ(XhciRingSetDequeue(&ring, RING_PA + 4 * 16), XHCI_RING_NOT_TD_HEAD,
             "the middle of a TD is not a resume position");
    CHECK_EQ(XhciRingSetDequeue(&ring, RING_PA + 5 * 16), XHCI_RING_NOT_TD_HEAD,
             "nor is its last TRB");
    CHECK_EQ(ring.Dequeue, 3, "dequeue held");

    /* Flushing to the enqueue position discards the rest and empties the ring. */
    CHECK_EQ(XhciRingSetDequeue(&ring, RING_PA + 6 * 16), XHCI_RING_OK,
             "the enqueue position is always a legal target");
    CHECK_EQ(ring.Dequeue, ring.Enqueue, "ring emptied");
    CHECK_EQ(XhciRingFree(&ring), 6, "every slot came back");

    /*
     * On an empty ring the enqueue position is the *only* legal target. Any
     * other would invent outstanding work out of stale TRBs from earlier laps,
     * which no event would ever retire.
     */
    CHECK_EQ(XhciRingSetDequeue(&ring, RING_PA), XHCI_RING_NOT_ON_RING,
             "an empty ring cannot be moved off its enqueue position");
    CHECK_EQ(XhciRingSetDequeue(&ring, RING_PA + 3 * 16), XHCI_RING_NOT_ON_RING,
             "in either direction");
    CHECK_EQ(XhciRingFree(&ring), 6, "so an empty ring stays empty");
    CHECK_EQ(XhciRingSetDequeue(&ring, XhciRingDequeuePA(&ring)), XHCI_RING_OK,
             "placing it where it already is, is a no-op");
    CHECK_EQ(XhciRingFree(&ring), 6, "still empty");

    /*
     * Flushing a *full* ring, where the enqueue pointer sits one slot behind
     * the dequeue pointer rather than ahead of it. Same rule, but the modular
     * arithmetic runs the other way round, so it is worth pinning.
     */
    CHECK_EQ(XhciRingInit(&ring, mem, RING_PA, 8, XHCI_RING_KIND_ENDPOINT),
             XHCI_RING_OK, "ring init");
    build_td(td, 3);
    CHECK_EQ(XhciRingEnqueueTd(&ring, td, 3, NULL), XHCI_RING_OK, "TD");
    CHECK_EQ(XhciRingEnqueueTd(&ring, td, 3, NULL), XHCI_RING_OK, "another");
    CHECK_EQ(XhciRingFree(&ring), 0, "ring full");
    CHECK_EQ(ring.Enqueue, 6, "enqueue one slot behind dequeue, the long way");
    CHECK_EQ(ring.Dequeue, 0, "dequeue at the first TD's head");

    CHECK_EQ(XhciRingSetDequeue(&ring, XhciRingTrbPA(&ring, 6)), XHCI_RING_OK,
             "a full ring flushes to its enqueue position");
    CHECK_EQ(ring.Dequeue, 6, "dequeue moved to the enqueue position");
    CHECK_EQ(XhciRingFree(&ring), 6, "and the whole ring came back");
    CHECK_EQ(XhciRingEnqueueTd(&ring, td, 3, NULL), XHCI_RING_OK,
             "which is immediately usable again");
}

/* The same rules once the outstanding window wraps past the Link TRB. */
static void test_set_dequeue_wrapped(void)
{
    XHCI_TRB mem[8];
    XHCI_TRB td[3];
    XHCI_TRB one;
    XHCI_RING ring;
    ULONG pa;
    ULONG i;

    CHECK_EQ(XhciRingInit(&ring, mem, RING_PA, 8, XHCI_RING_KIND_ENDPOINT),
             XHCI_RING_OK, "ring init");
    XhciTrbNoOpCommand(&one);
    for (i = 0; i < 5; i++) {
        CHECK_EQ(XhciRingEnqueue(&ring, &one, &pa), XHCI_RING_OK, "filler");
        CHECK_EQ(retire_success(&ring, pa), XHCI_RING_OK, "retired");
    }

    /* A TD at 5, 6, wrap, 0 - then a second TD at 1, 2, 3. */
    build_td(td, 3);
    CHECK_EQ(XhciRingEnqueueTd(&ring, td, 3, &pa), XHCI_RING_OK, "wrapped TD");
    CHECK_EQ(pa, RING_PA + 5 * 16, "head at 5");
    CHECK_EQ(XhciRingEnqueueTd(&ring, td, 3, NULL), XHCI_RING_OK, "second TD");
    CHECK_EQ(ring.Dequeue, 5, "dequeue at the first TD's head");
    CHECK_EQ(ring.Enqueue, 4, "enqueue past the second");

    /* Inside the wrapped window, only the two TD heads are legal. */
    CHECK_EQ(XhciRingSetDequeue(&ring, RING_PA + 6 * 16), XHCI_RING_NOT_TD_HEAD,
             "middle of the wrapped TD");
    CHECK_EQ(XhciRingSetDequeue(&ring, RING_PA + 0 * 16), XHCI_RING_NOT_TD_HEAD,
             "its tail, on the far side of the link");
    CHECK_EQ(XhciRingSetDequeue(&ring, RING_PA + 2 * 16), XHCI_RING_NOT_TD_HEAD,
             "middle of the second TD");
    CHECK_EQ(XhciRingSetDequeue(&ring, RING_PA + 1 * 16), XHCI_RING_OK,
             "the second TD's head, reached across the wrap");
    CHECK_EQ(ring.Dequeue, 1, "moved");
    CHECK_EQ(XhciRingFree(&ring), 3, "the abandoned wrapped TD came back");

    /* Outside it - the slots the first TD used to occupy - is refused. */
    CHECK_EQ(XhciRingSetDequeue(&ring, RING_PA + 5 * 16), XHCI_RING_NOT_ON_RING,
             "the reclaimed head is behind the dequeue pointer now");
    CHECK_EQ(ring.Dequeue, 1, "dequeue held");
}

/*
 * A Set TR Dequeue Pointer command carries a *pair*: the address and the
 * Dequeue Cycle State that goes in bit 0 of the same field. DCS "identifies
 * the value of the xHC Consumer Cycle State (CCS) flag for the TRB referenced
 * by the TR Dequeue Pointer" (Table 6-67 p.455; 4.6.10 p.127 says the same in
 * prose), so it is a function of which lap the dequeue pointer is on - never a
 * constant. Hand the controller the wrong one and it reads a live TD as
 * unproduced and stops dead, or reads stale TRBs as work and runs off into
 * them; neither reports an error.
 *
 * The oracle here is the ring itself: for any non-empty ring the answer must
 * equal the Cycle Bit actually stored at the dequeue index, because that TRB
 * was written with the producer cycle of its own lap.
 */
static void test_dequeue_cycle(void)
{
    XHCI_TRB mem[8];
    XHCI_TRB one;
    XHCI_RING ring;
    ULONG pas[3];
    ULONG pa;
    ULONG i;
    ULONG expected;

    CHECK_EQ(XhciRingInit(&ring, mem, RING_PA, 8, XHCI_RING_KIND_ENDPOINT),
             XHCI_RING_OK, "ring init");
    CHECK_EQ(XhciRingDequeueCycle(&ring), 1, "a fresh ring's DCS is 1");
    CHECK_EQ(ring.Cycle, 1, "which is also the producer cycle");

    XhciTrbNoOpCommand(&one);

    /* Four whole laps with one TRB outstanding at a time - seven usable slots,
     * so 28 TRBs - checked against the ring at every position. */
    for (i = 0; i < 28; i++) {
        CHECK_EQ(XhciRingEnqueue(&ring, &one, &pa), XHCI_RING_OK, "enqueued");
        expected = mem[ring.Dequeue].Control & XHCI_TRB_CYCLE;
        CHECK_EQ(XhciRingDequeueCycle(&ring), expected,
                 "DCS is the Cycle Bit the ring actually holds there");
        CHECK_EQ(retire_success(&ring, pa), XHCI_RING_OK, "retired");
        CHECK_EQ(XhciRingDequeueCycle(&ring), ring.Cycle,
                 "an empty ring reports the producer cycle");
    }
    CHECK_EQ(ring.Cycle, 1, "four whole laps, back where it started");

    /*
     * Now a window that straddles the Link TRB, so the two pointers are on
     * different laps. Three single-TRB TDs at 5, 6 and - past the wrap - 0,
     * retired one at a time to watch DCS turn over exactly at the crossing.
     */
    CHECK_EQ(XhciRingInit(&ring, mem, RING_PA, 8, XHCI_RING_KIND_ENDPOINT),
             XHCI_RING_OK, "ring init");
    for (i = 0; i < 5; i++) {
        CHECK_EQ(XhciRingEnqueue(&ring, &one, &pa), XHCI_RING_OK, "filler");
        CHECK_EQ(retire_success(&ring, pa), XHCI_RING_OK, "retired");
    }
    for (i = 0; i < 3; i++) {
        CHECK_EQ(XhciRingEnqueue(&ring, &one, &pas[i]), XHCI_RING_OK, "TD");
    }
    CHECK_EQ(ring.Dequeue, 5, "dequeue behind the link");
    CHECK_EQ(ring.Enqueue, 1, "enqueue past it");
    CHECK_EQ(ring.Cycle, 0, "the producer toggled on the crossing");

    CHECK_EQ(XhciRingDequeueCycle(&ring), 1,
             "DCS is the outgoing lap's cycle, not the producer's");
    CHECK_EQ(XhciRingDequeueCycle(&ring), mem[5].Control & XHCI_TRB_CYCLE,
             "which is what TRB 5 carries");
    CHECK_EQ(retire_success(&ring, pas[0]), XHCI_RING_OK, "retire TRB 5");
    CHECK_EQ(ring.Dequeue, 6, "still behind the link");
    CHECK_EQ(XhciRingDequeueCycle(&ring), 1, "so DCS has not turned over yet");
    CHECK_EQ(retire_success(&ring, pas[1]), XHCI_RING_OK, "retire TRB 6");
    CHECK_EQ(ring.Dequeue, 0, "across the link");
    CHECK_EQ(XhciRingDequeueCycle(&ring), 0, "and DCS turns over here");
    CHECK_EQ(XhciRingDequeueCycle(&ring), mem[0].Control & XHCI_TRB_CYCLE,
             "matching the TRB written after the wrap");
    CHECK_EQ(retire_success(&ring, pas[2]), XHCI_RING_OK, "retire TRB 0");
    CHECK_EQ(ring.Dequeue, 1, "empty again");
    CHECK_EQ(XhciRingDequeueCycle(&ring), 0, "and stays on the producer cycle");

    /*
     * A flush to the enqueue position from a wrapped window - the RECOVER path
     * after Stop Endpoint. This is the case that rules out deriving DCS by
     * reading the ring back: the TRB at the enqueue position still holds a
     * stale Cycle Bit from the previous lap, and the answer has to be the
     * producer's, because that is what the next TRB written there will carry.
     */
    CHECK_EQ(XhciRingInit(&ring, mem, RING_PA, 8, XHCI_RING_KIND_ENDPOINT),
             XHCI_RING_OK, "ring init");
    for (i = 0; i < 5; i++) {
        CHECK_EQ(XhciRingEnqueue(&ring, &one, &pa), XHCI_RING_OK, "filler");
        CHECK_EQ(retire_success(&ring, pa), XHCI_RING_OK, "retired");
    }
    for (i = 0; i < 3; i++) {
        CHECK_EQ(XhciRingEnqueue(&ring, &one, &pas[i]), XHCI_RING_OK, "TD");
    }
    CHECK_EQ(mem[1].Control & XHCI_TRB_CYCLE, 1,
             "TRB 1 still carries the previous lap's Cycle Bit");
    CHECK_EQ(XhciRingSetDequeue(&ring, RING_PA + 1 * 16), XHCI_RING_OK,
             "flush to the enqueue position");
    CHECK_EQ(ring.Dequeue, 1, "the wrapped window was discarded");
    CHECK_EQ(XhciRingDequeueCycle(&ring), 0,
             "DCS follows the producer, not the stale bit on the ring");

    /* Explicit placement onto a TD head that is still outstanding takes that
     * TRB's cycle, wherever the wrap falls relative to it. */
    CHECK_EQ(XhciRingInit(&ring, mem, RING_PA, 8, XHCI_RING_KIND_ENDPOINT),
             XHCI_RING_OK, "ring init");
    for (i = 0; i < 5; i++) {
        CHECK_EQ(XhciRingEnqueue(&ring, &one, &pa), XHCI_RING_OK, "filler");
        CHECK_EQ(retire_success(&ring, pa), XHCI_RING_OK, "retired");
    }
    for (i = 0; i < 3; i++) {
        CHECK_EQ(XhciRingEnqueue(&ring, &one, &pas[i]), XHCI_RING_OK, "TD");
    }
    CHECK_EQ(XhciRingSetDequeue(&ring, pas[1]), XHCI_RING_OK, "onto TRB 6");
    CHECK_EQ(XhciRingDequeueCycle(&ring), mem[6].Control & XHCI_TRB_CYCLE,
             "before the link: the outgoing lap's cycle");
    CHECK_EQ(XhciRingSetDequeue(&ring, pas[2]), XHCI_RING_OK, "onto TRB 0");
    CHECK_EQ(XhciRingDequeueCycle(&ring), mem[0].Control & XHCI_TRB_CYCLE,
             "after the link: the new lap's cycle");
}

/*
 * The Chain flags on the ring are what the ownership walk reads, so a TD whose
 * flags do not describe one TD is refused at enqueue rather than left to
 * corrupt a later walk. Spec 4.11.7: set in every TRB of a TD except the last.
 */
static void test_td_chain_validation(void)
{
    XHCI_TRB mem[8];
    XHCI_TRB td[3];
    XHCI_RING ring;

    CHECK_EQ(XhciRingInit(&ring, mem, RING_PA, 8, XHCI_RING_KIND_ENDPOINT),
             XHCI_RING_OK, "ring init");

    build_td(td, 3);
    td[2].Control |= XHCI_TRB_CH;
    CHECK_EQ(XhciRingEnqueueTd(&ring, td, 3, NULL), XHCI_RING_BAD_CHAIN,
             "a chained last TRB is refused");
    CHECK_EQ(ring.Enqueue, 0, "and nothing was written");

    build_td(td, 3);
    td[1].Control &= ~XHCI_TRB_CH;
    CHECK_EQ(XhciRingEnqueueTd(&ring, td, 3, NULL), XHCI_RING_BAD_CHAIN,
             "an unchained middle TRB is refused");

    build_td(td, 1);
    td[0].Control |= XHCI_TRB_CH;
    CHECK_EQ(XhciRingEnqueueTd(&ring, td, 1, NULL), XHCI_RING_BAD_CHAIN,
             "a single-TRB TD may not be chained");

    build_td(td, 3);
    CHECK_EQ(XhciRingEnqueueTd(&ring, td, 3, NULL), XHCI_RING_OK,
             "the well-formed TD is accepted");
}

/*
 * ED = 1 means the event's parameter is "64 bits of Event Data" from an Event
 * Data TRB, not a ring address (spec Tables 6-37 and 6-39). This driver places
 * no Event Data TRBs, so such an event is unexpected and must be discarded by
 * the caller - which needs a way to tell.
 */
static void test_event_data_flag(void)
{
    CHECK_EQ(XHCI_TRB_ED, 0x00000004UL, "Event Data is bit 2");
    CHECK_EQ(XHCI_TRB_ED, XHCI_TRB_ISP,
             "and shares its position with a transfer TRB's ISP");

    /* A Transfer Event, cycle set, type 32, slot 5, EP 3: ED clear. */
    CHECK_EQ(XHCI_EVENT_IS_EVENT_DATA(0x05038001UL), 0, "ordinary event");
    /* The same with ED set. */
    CHECK_EQ(XHCI_EVENT_IS_EVENT_DATA(0x05038005UL), 1, "Event Data event");
}

/* ------------------------------------------------------------------ */
/* 7. Event ring: the scripted-hardware drain                          */
/* ------------------------------------------------------------------ */

/*
 * Stand in for the xHC: write one event into the segment at `index` with
 * cycle bit `cycle`, exactly as the controller would. The consumer under test
 * never learns how many events exist - only the cycle bit tells it.
 */
static void hw_write_event(XHCI_TRB *seg, ULONG index, ULONG cycle,
                           ULONG type, ULONG param0, ULONG status)
{
    seg[index].Param0 = param0;
    seg[index].Param1 = 0;
    seg[index].Status = status;
    seg[index].Control = XHCI_TRB_TYPE(type) | (cycle ? XHCI_TRB_CYCLE : 0);
}

static void test_event_ring(void)
{
    XHCI_TRB seg[16];
    XHCI_EVENT_RING er;
    XHCI_TRB event;
    ULONG i;

    for (i = 0; i < 16; i++) {
        seg[i].Param0 = 0x55555555UL;
        seg[i].Control = 0x55555555UL;
    }

    CHECK_EQ(XhciEventRingInit(&er, seg, EVENT_PA, 16), XHCI_RING_OK,
             "16-TRB event ring accepted");
    CHECK_EQ(er.Dequeue, 0, "dequeue starts at 0");
    CHECK_EQ(er.Ccs, 1, "consumer cycle state starts at 1");
    CHECK_EQ(seg[0].Control, 0, "segment zeroed");
    CHECK_EQ(seg[15].Control, 0, "segment zeroed to the end");

    /* Zeroed memory has Cycle = 0 against a CCS of 1: nothing produced yet. */
    CHECK_EQ(XhciEventRingPending(&er), 0, "empty ring is not pending");
    CHECK_EQ(XhciEventRingDequeue(&er, &event), XHCI_RING_EMPTY,
             "dequeue from an empty ring reports empty");
    CHECK_EQ(er.Dequeue, 0, "an empty dequeue does not advance");

    /* Three events arrive. */
    hw_write_event(seg, 0, 1, XHCI_TRB_TYPE_COMMAND_COMPLETION,
                   RING_PA, 0x01000000UL);
    hw_write_event(seg, 1, 1, XHCI_TRB_TYPE_PORT_STATUS_CHANGE,
                   0x04000000UL, 0x01000000UL);
    hw_write_event(seg, 2, 1, XHCI_TRB_TYPE_TRANSFER_EVENT,
                   0x0F001230UL, 0x0D000010UL);

    CHECK_EQ(XhciEventRingPending(&er), 1, "produced event is pending");
    CHECK_EQ(XhciEventRingDequeue(&er, &event), XHCI_RING_OK, "first event");
    CHECK_EQ(XHCI_TRB_GET_TYPE(event.Control), 33, "Command Completion Event");
    CHECK_EQ(event.Param0, RING_PA, "command TRB pointer");
    CHECK_EQ(XHCI_TRB_GET_COMPLETION(event.Status), 1, "completion Success");

    CHECK_EQ(XhciEventRingDequeue(&er, &event), XHCI_RING_OK, "second event");
    CHECK_EQ(XHCI_TRB_GET_TYPE(event.Control), 34, "Port Status Change Event");
    CHECK_EQ(XHCI_TRB_GET_PORT_ID(event.Param0), 4, "port 4 changed");

    CHECK_EQ(XhciEventRingDequeue(&er, &event), XHCI_RING_OK, "third event");
    CHECK_EQ(XHCI_TRB_GET_TYPE(event.Control), 32, "Transfer Event");
    CHECK_EQ(XHCI_TRB_GET_COMPLETION(event.Status), 13, "Short Packet");
    CHECK_EQ(XHCI_TRB_GET_RESIDUAL(event.Status), 0x10UL, "residual bytes");

    CHECK_EQ(XhciEventRingDequeue(&er, &event), XHCI_RING_EMPTY,
             "drain stops when the cycle bit says empty");
    CHECK_EQ(er.Dequeue, 3, "dequeue advanced by exactly three");
    CHECK_EQ(er.Ccs, 1, "no wrap yet, so CCS is unchanged");
}

/*
 * Wrap the event ring past its end. The lap-2 events carry Cycle = 0, and the
 * lap-1 leftovers - still sitting in memory with Cycle = 1 - must read as
 * "not produced yet". Getting this inverted is the classic event-ring bug:
 * the DPC re-processes a whole segment of stale events.
 */
static void test_event_ring_wrap(void)
{
    XHCI_TRB seg[16];
    XHCI_EVENT_RING er;
    XHCI_TRB event;
    ULONG lap;
    ULONG i;
    ULONG cycle;

    CHECK_EQ(XhciEventRingInit(&er, seg, EVENT_PA, 16), XHCI_RING_OK, "init");

    for (lap = 0; lap < 3; lap++) {
        cycle = (lap % 2 == 0) ? 1 : 0;
        CHECK_EQ(er.Ccs, cycle, "CCS at lap start");

        for (i = 0; i < 16; i++) {
            hw_write_event(seg, i, cycle, XHCI_TRB_TYPE_PORT_STATUS_CHANGE,
                           ((i + 1) << 24), 0x01000000UL);
        }

        for (i = 0; i < 16; i++) {
            CHECK_EQ(XhciEventRingPending(&er), 1, "event pending");
            CHECK_EQ(XhciEventRingDequeue(&er, &event), XHCI_RING_OK,
                     "event dequeued");
            CHECK_EQ(XHCI_TRB_GET_PORT_ID(event.Param0), i + 1,
                     "events come out in segment order");
        }

        CHECK_EQ(er.Dequeue, 0, "dequeue wrapped to the segment base");
        CHECK_EQ(er.Ccs, cycle ^ 1, "CCS toggled on the wrap");
        /* The whole segment still holds last lap's TRBs. */
        CHECK_EQ(XhciEventRingPending(&er), 0,
                 "stale TRBs from the previous lap read as empty");
        CHECK_EQ(XhciEventRingDequeue(&er, &event), XHCI_RING_EMPTY,
                 "and are not handed to the DPC again");
    }
}

static void test_event_ring_erdp(void)
{
    XHCI_TRB seg[16];
    XHCI_EVENT_RING er;
    XHCI_TRB event;

    CHECK_EQ(XhciEventRingInit(&er, seg, EVENT_PA, 16), XHCI_RING_OK, "init");

    CHECK_EQ(XhciEventRingDequeuePA(&er), EVENT_PA, "ERDP at the base");
    /*
     * EHB is RW1C in bit 3: an intermediate write during a long drain carries
     * 0 there, which *preserves* Event Handler Busy and keeps the interrupter
     * suppressed. Only the final write after the ring reads empty carries 1
     * (docs/usb-xhci-info/xhci-data-structures.md, ISR/DPC rules).
     */
    CHECK_EQ(XhciEventRingErdpValue(&er, 0), EVENT_PA,
             "intermediate ERDP leaves EHB alone");
    CHECK_EQ(XhciEventRingErdpValue(&er, 1), EVENT_PA | 0x8UL,
             "final ERDP write sets EHB");

    hw_write_event(seg, 0, 1, XHCI_TRB_TYPE_PORT_STATUS_CHANGE, 0, 0);
    hw_write_event(seg, 1, 1, XHCI_TRB_TYPE_PORT_STATUS_CHANGE, 0, 0);
    CHECK_EQ(XhciEventRingDequeue(&er, &event), XHCI_RING_OK, "one");
    CHECK_EQ(XhciEventRingDequeue(&er, &event), XHCI_RING_OK, "two");

    CHECK_EQ(XhciEventRingDequeuePA(&er), EVENT_PA + 32,
             "ERDP follows the dequeue pointer");
    CHECK_EQ(XhciEventRingErdpValue(&er, 1), (EVENT_PA + 32) | 0x8UL,
             "final ERDP after two events");
    /* One segment, so DESI (bits 2:0) is always zero. */
    CHECK_EQ(XhciEventRingErdpValue(&er, 1) & 0x7UL, 0, "DESI is zero");
}

static void test_event_ring_refusals(void)
{
    XHCI_TRB seg[16];
    XHCI_EVENT_RING er;

    CHECK_EQ(XhciEventRingInit(&er, NULL, EVENT_PA, 16), XHCI_RING_BAD_PARAM,
             "null segment refused");
    CHECK_EQ(XhciEventRingInit(&er, seg, 0, 16), XHCI_RING_BAD_PARAM,
             "zero physical base refused");
    /* Spec 6.5: an event ring segment holds 16 to 4096 TRBs. */
    CHECK_EQ(XhciEventRingInit(&er, seg, EVENT_PA, 15), XHCI_RING_BAD_PARAM,
             "fewer than 16 TRBs refused");
    CHECK_EQ(XhciEventRingInit(&er, seg, EVENT_PA, 4097), XHCI_RING_BAD_PARAM,
             "more than 4096 TRBs refused");
    /* Table 6-1: event ring segments are 64-byte aligned, not 16. */
    CHECK_EQ(XhciEventRingInit(&er, seg, EVENT_PA + 16, 16),
             XHCI_RING_BAD_PARAM, "16-byte-aligned base refused");
}

/* ------------------------------------------------------------------ */
/* 8. The ring sizes the driver actually carves                        */
/* ------------------------------------------------------------------ */

/*
 * The command ring and EP0 rings are carved from the fixed common buffer at
 * the sizes xhci.h declares, so run the real geometry once rather than only
 * the 8-TRB toy above.
 */
static void test_declared_ring_sizes(void)
{
    static XHCI_TRB cmd[XHCI_CMD_RING_TRBS];
    static XHCI_TRB evt[XHCI_EVENT_RING_TRBS];
    XHCI_RING ring;
    XHCI_EVENT_RING er;
    XHCI_TRB trb;
    ULONG i;

    CHECK_EQ(XhciRingInit(&ring, cmd, RING_PA, XHCI_CMD_RING_TRBS,
                          XHCI_RING_KIND_COMMAND),
             XHCI_RING_OK, "declared command ring accepted");
    CHECK_EQ(XhciRingCapacity(&ring), 62,
             "a 64-TRB command ring holds 62 commands");
    CHECK_EQ(cmd[XHCI_CMD_RING_TRBS - 1].Control, 0x00001802UL,
             "command ring link TRB");

    /* One outstanding command at a time is the policy
     * (docs/contributing/implementation-invariants.md), so command-ring-full is
     * unreachable - prove the headroom rather than assuming it. */
    XhciTrbNoOpCommand(&trb);
    for (i = 0; i < 62; i++) {
        CHECK_EQ(XhciRingEnqueue(&ring, &trb, NULL), XHCI_RING_OK,
                 "command enqueued");
    }
    CHECK_EQ(XhciRingEnqueue(&ring, &trb, NULL), XHCI_RING_FULL,
             "63rd command refused");

    CHECK_EQ(XhciEventRingInit(&er, evt, EVENT_PA, XHCI_EVENT_RING_TRBS),
             XHCI_RING_OK, "declared event ring accepted");
    CHECK_EQ(er.Trbs, 256, "event ring is one 256-TRB segment");
}

/*
 * The command TRB builders, by hand from the TRB table in
 * docs/usb-xhci-info/xhci-data-structures.md section 5.
 *
 * These had **no direct vectors at all** before batch 7a-A - Enable Slot,
 * Address Device and Evaluate Context were only ever exercised through
 * test_init driving the real command engine, which checks that the engine
 * submits *something* rather than that the something is encoded right. The
 * Configure Endpoint builder is added here with vectors, and the three it sits
 * beside get them too, because a builder whose only test is an integration path
 * is one whose field positions nothing has ever read back.
 */
static void test_command_trb_encoding(void)
{
    XHCI_TRB t;

    /* Configure Endpoint: type 12 at 15:10 = 0x3000, Slot ID 7 at 31:24. */
    CHECK_EQ(XhciTrbConfigureEndpoint(&t, 7UL, 0x00203000UL, 0UL),
             XHCI_RING_OK, "configure endpoint accepted");
    CHECK_EQ(t.Param0, 0x00203000UL, "input context pointer");
    CHECK_EQ(t.Param1, 0UL, "high DWORD zero - no 64-bit DMA");
    CHECK_EQ(t.Control, 0x07003000UL, "type 12 + slot 7, DC clear");
    CHECK_EQ(t.Status, 0UL, "status word untouched");

    /* Deconfigure: DC at bit 9, and the context pointer is ignored by the xHC
     * (6.4.3.5), so a zero one is accepted here and nowhere else. */
    CHECK_EQ(XhciTrbConfigureEndpoint(&t, 7UL, 0UL, 1UL), XHCI_RING_OK,
             "deconfigure accepted with no input context");
    CHECK_EQ(t.Param0, 0UL, "deconfigure carries no pointer");
    CHECK_EQ(t.Control, 0x07003200UL, "type 12 + slot 7 + DC");
    /* DC is the same bit as Address Device's BSR; the two must not be one
     * macro, and this pins that they encode to the same place on purpose. */
    CHECK_EQ(XHCI_TRB_DC, XHCI_TRB_BSR, "DC and BSR share bit 9");

    CHECK_EQ(XhciTrbConfigureEndpoint(NULL, 7UL, 0x00203000UL, 0UL),
             XHCI_RING_BAD_PARAM, "null TRB refused");
    CHECK_EQ(XhciTrbConfigureEndpoint(&t, 0UL, 0x00203000UL, 0UL),
             XHCI_RING_BAD_PARAM, "slot 0 refused - Slot IDs are 1-based");
    CHECK_EQ(XhciTrbConfigureEndpoint(&t, 256UL, 0x00203000UL, 0UL),
             XHCI_RING_BAD_PARAM, "slot 256 refused - the field is 8 bits");
    CHECK_EQ(XhciTrbConfigureEndpoint(&t, 7UL, 0UL, 0UL),
             XHCI_RING_BAD_PARAM,
             "a configure with no input context refused");
    CHECK_EQ(XhciTrbConfigureEndpoint(&t, 7UL, 0x00203004UL, 0UL),
             XHCI_RING_BAD_PARAM, "misaligned input context refused");

    /* The neighbours, previously untested at this level. */
    CHECK_EQ(XhciTrbEnableSlot(&t, 0UL), XHCI_RING_OK, "enable slot type 0");
    CHECK_EQ(t.Control, 0x00002400UL, "type 9, slot type 0 at 20:16");
    CHECK_EQ(XhciTrbEnableSlot(&t, 3UL), XHCI_RING_OK, "enable slot type 3");
    CHECK_EQ(t.Control, 0x00032400UL, "type 9 + slot type 3");

    CHECK_EQ(XhciTrbDisableSlot(&t, 5UL), XHCI_RING_OK, "disable slot 5");
    CHECK_EQ(t.Control, 0x05002800UL, "type 10 + slot 5");

    CHECK_EQ(XhciTrbAddressDevice(&t, 2UL, 0x00203000UL, 0UL), XHCI_RING_OK,
             "address device BSR=0");
    CHECK_EQ(t.Control, 0x02002C00UL, "type 11 + slot 2, BSR clear");
    CHECK_EQ(XhciTrbAddressDevice(&t, 2UL, 0x00203000UL, 1UL), XHCI_RING_OK,
             "address device BSR=1");
    CHECK_EQ(t.Control, 0x02002E00UL, "type 11 + slot 2 + BSR");

    CHECK_EQ(XhciTrbEvaluateContext(&t, 9UL, 0x00203000UL), XHCI_RING_OK,
             "evaluate context");
    CHECK_EQ(t.Control, 0x09003400UL, "type 13 + slot 9");
}

/*
 * Batch 7a-B's three. The field positions are section 5's command table -
 * Endpoint ID at 20:16, Slot ID at 31:24, TSP at bit 9, SP at bit 23 - and each
 * is spelled as a literal Control word rather than rebuilt from the macros,
 * because a test that computes the answer the same way the code does agrees with
 * a wrong shift.
 */
static void test_endpoint_command_trb_encoding(void)
{
    XHCI_TRB t;

    /* Stop Endpoint: type 15 -> 0x3C00. */
    CHECK_EQ(XhciTrbStopEndpoint(&t, 5UL, 3UL, 0UL), XHCI_RING_OK,
             "stop endpoint accepted");
    CHECK_EQ(t.Param0, 0UL, "no parameter");
    CHECK_EQ(t.Param1, 0UL, "high DWORD zero");
    CHECK_EQ(t.Status, 0UL, "status word zero");
    CHECK_EQ(t.Control, 0x05033C00UL, "type 15 + DCI 3 + slot 5, SP clear");
    CHECK_EQ(XhciTrbStopEndpoint(&t, 5UL, 3UL, 1UL), XHCI_RING_OK,
             "stop endpoint with SP");
    CHECK_EQ(t.Control, 0x05833C00UL, "SP at bit 23");
    CHECK_EQ(XHCI_TRB_SP, 0x00800000UL, "which is where section 5 puts it");

    /* Reset Endpoint: type 14 -> 0x3800. */
    CHECK_EQ(XhciTrbResetEndpoint(&t, 5UL, 1UL, 0UL), XHCI_RING_OK,
             "reset endpoint accepted");
    CHECK_EQ(t.Control, 0x05013800UL, "type 14 + DCI 1 + slot 5, TSP clear");
    CHECK_EQ(XhciTrbResetEndpoint(&t, 5UL, 1UL, 1UL), XHCI_RING_OK,
             "reset endpoint with TSP");
    CHECK_EQ(t.Control, 0x05013A00UL, "TSP at bit 9");
    /* TSP shares bit 9 with BSR and DC, which is why each has its own name -
     * one macro for three commands is how a Soft Retry gets issued as a
     * Deconfigure. */
    CHECK_EQ(XHCI_TRB_TSP, XHCI_TRB_BSR, "TSP, BSR and DC share bit 9");

    /* Set TR Dequeue Pointer: type 16 -> 0x4000, DCS in bit 0 of the pointer. */
    CHECK_EQ(XhciTrbSetTrDequeue(&t, 5UL, 3UL, 0x00203010UL, 0UL), XHCI_RING_OK,
             "set TR dequeue accepted, DCS 0");
    CHECK_EQ(t.Param0, 0x00203010UL, "pointer with DCS clear");
    CHECK_EQ(t.Param1, 0UL, "high DWORD zero");
    CHECK_EQ(t.Status, 0UL, "Stream ID zero - MaxPStreams is 0 here");
    CHECK_EQ(t.Control, 0x05034000UL, "type 16 + DCI 3 + slot 5");
    CHECK_EQ(XhciTrbSetTrDequeue(&t, 5UL, 3UL, 0x00203010UL, 1UL), XHCI_RING_OK,
             "and with DCS 1");
    CHECK_EQ(t.Param0, 0x00203011UL, "DCS is bit 0 of the pointer field");

    /* The refusals. A DCI outside 1..31 names no endpoint - and 0 is the value
     * a zeroed record holds, so masking it in would aim the command at the Slot
     * Context. */
    CHECK_EQ(XhciTrbStopEndpoint(NULL, 5UL, 3UL, 0UL), XHCI_RING_BAD_PARAM,
             "null TRB refused");
    CHECK_EQ(XhciTrbStopEndpoint(&t, 0UL, 3UL, 0UL), XHCI_RING_BAD_PARAM,
             "slot 0 refused");
    CHECK_EQ(XhciTrbStopEndpoint(&t, 5UL, 0UL, 0UL), XHCI_RING_BAD_PARAM,
             "DCI 0 refused - that is the Slot Context");
    CHECK_EQ(XhciTrbStopEndpoint(&t, 5UL, 32UL, 0UL), XHCI_RING_BAD_PARAM,
             "DCI 32 refused - 31 is the last endpoint");
    CHECK_EQ(XhciTrbResetEndpoint(&t, 5UL, 0UL, 0UL), XHCI_RING_BAD_PARAM,
             "and the same two bounds on Reset Endpoint");
    CHECK_EQ(XhciTrbResetEndpoint(&t, 5UL, 32UL, 0UL), XHCI_RING_BAD_PARAM,
             "at both ends");
    CHECK_EQ(XhciTrbSetTrDequeue(&t, 5UL, 32UL, 0x00203010UL, 0UL),
             XHCI_RING_BAD_PARAM, "and on Set TR Dequeue Pointer");
    /* The pointer's low four bits are DCS and SCT, not address: an address
     * carrying any of them would encode a different command. */
    CHECK_EQ(XhciTrbSetTrDequeue(&t, 5UL, 3UL, 0x00203018UL, 0UL),
             XHCI_RING_BAD_PARAM, "a pointer with SCT bits set is refused");
    CHECK_EQ(XhciTrbSetTrDequeue(&t, 5UL, 3UL, 0UL, 0UL), XHCI_RING_BAD_PARAM,
             "and so is a null one");
    CHECK_EQ(XhciTrbSetTrDequeue(&t, 5UL, 3UL, 0x00203010UL, 2UL),
             XHCI_RING_BAD_PARAM, "DCS is one bit");
}

/*
 * Rewriting a cancelled TD as No Ops - what a Stop Endpoint's "software may add,
 * delete, or otherwise rearrange TDs" (4.6.9 p.119) costs in this ring layer.
 */
static void test_ring_noop_rewrite(void)
{
    XHCI_TRB mem[8];
    XHCI_TRB td[3];
    XHCI_RING ring;
    ULONG head;
    ULONG tail;

    CHECK_EQ(XhciRingInit(&ring, mem, RING_PA, 8, XHCI_RING_KIND_ENDPOINT),
             XHCI_RING_OK, "ring init");
    build_td(td, 3);
    CHECK_EQ(XhciRingEnqueueTd(&ring, td, 3, NULL), XHCI_RING_OK, "first TD");
    CHECK_EQ(XhciRingEnqueueTd(&ring, td, 3, NULL), XHCI_RING_OK, "second TD");

    /* The first TD's three TRBs become three single-TRB No Op TDs. */
    CHECK_EQ(XhciRingNoOpAt(&ring, 0), XHCI_RING_OK, "TRB 0 rewritten");
    CHECK_EQ(XhciRingNoOpAt(&ring, 1), XHCI_RING_OK, "TRB 1 rewritten");
    CHECK_EQ(XhciRingNoOpAt(&ring, 2), XHCI_RING_OK, "TRB 2 rewritten");
    CHECK_EQ(XHCI_TRB_GET_TYPE(mem[0].Control), XHCI_TRB_TYPE_NOOP,
             "type 8, the transfer No Op");
    CHECK_EQ(mem[0].Control & XHCI_TRB_CYCLE, XHCI_TRB_CYCLE,
             "with the produced Cycle Bit preserved");
    CHECK_EQ(mem[0].Control & XHCI_TRB_CH, 0UL, "Chain cleared");
    CHECK_EQ(mem[0].Control & XHCI_TRB_IOC, 0UL,
             "and IOC, so no event is asked for on a transfer that is gone");
    CHECK_EQ(mem[0].Param0, 0UL, "buffer pointer cleared");
    CHECK_EQ(mem[0].Status, 0UL, "and the length word");
    CHECK_EQ(mem[1].Control & XHCI_TRB_CH, 0UL,
             "the middle TRB is no longer chained into the next");

    /* Each is now its own TD, which is what lets the dequeue pointer land on
     * the surviving TD's head - the backwards Chain walk stops at the No Op. */
    CHECK_EQ(XhciRingTdBounds(&ring, 3, &head, &tail), XHCI_RING_OK,
             "the surviving TD still resolves");
    CHECK_EQ(head, 3, "with its own head");
    CHECK_EQ(tail, 5, "and tail");
    CHECK_EQ(XhciRingSetDequeue(&ring, XhciRingTrbPA(&ring, 3)), XHCI_RING_OK,
             "so the survivor's head is a legal resume position");
    CHECK_EQ(ring.Dequeue, 3, "and the leftovers are reclaimed by the move");

    /* Refusals. Both would corrupt the ring rather than edit it. */
    CHECK_EQ(XhciRingNoOpAt(&ring, 7), XHCI_RING_NOT_ON_RING,
             "the Link TRB is never rewritten");
    CHECK_EQ(XHCI_TRB_GET_TYPE(mem[7].Control), XHCI_TRB_TYPE_LINK,
             "and is still a Link TRB");
    CHECK_EQ(XhciRingNoOpAt(&ring, 0), XHCI_RING_NOT_ON_RING,
             "a reclaimed slot is not this driver's to edit");
    CHECK_EQ(XhciRingNoOpAt(&ring, 6), XHCI_RING_NOT_ON_RING,
             "nor is one the producer has not written on this lap");
    CHECK_EQ(XhciRingNoOpAt(NULL, 0), XHCI_RING_BAD_PARAM, "null ring refused");

    /* The Cycle Bit is read back rather than recomputed, so a ring on its second
     * lap keeps whichever value the producer wrote. */
    CHECK_EQ(XhciRingEnqueueTd(&ring, td, 3, NULL), XHCI_RING_OK,
             "a TD across the wrap");
    CHECK_EQ(ring.Cycle, 0UL, "(the producer has toggled)");
    CHECK_EQ(XhciRingNoOpAt(&ring, 0), XHCI_RING_OK, "rewritten on lap two");
    CHECK_EQ(mem[0].Control & XHCI_TRB_CYCLE, 0UL,
             "with the second lap's Cycle Bit, not a constant");
}

int main(void)
{
    test_trb_fields();
    test_ring_init();
    test_ring_init_refusals();
    test_segment_boundaries();
    test_ring_wrap();
    test_ring_full();
    test_td_no_wrap();
    test_td_spanning_link();
    test_td_group();
    test_td_group_spanning_link();
    test_td_group_refusals();
    test_sum_trb_lengths();
    test_td_all_or_nothing();
    test_ring_retire();
    test_td_ownership();
    test_retire_advanced_td();
    test_td_ownership_wrapped();
    test_td_intermediate_ioc();
    test_completion_codes();
    test_missed_service();
    test_isoch_errors_never_halt();
    test_isoch_trb_error_still_recovers();
    test_command_ring_errors();
    test_completion_code_domains();
    test_deferred_td_swept_by_later_event();
    test_set_dequeue();
    test_set_dequeue_wrapped();
    test_dequeue_cycle();
    test_td_chain_validation();
    test_event_data_flag();
    test_event_ring();
    test_event_ring_wrap();
    test_event_ring_erdp();
    test_event_ring_refusals();
    test_declared_ring_sizes();
    test_command_trb_encoding();
    test_endpoint_command_trb_encoding();
    test_ring_noop_rewrite();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
