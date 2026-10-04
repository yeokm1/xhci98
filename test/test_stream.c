/*
 * test_stream.c - host vectors for the pure half of bulk streams
 * (src\xhci_stream.c; roadmap-hcd.md task 31-A.1).
 *
 * Every expected value below was worked by hand from the rule it checks,
 * not taken from the code under test: the HCCPARAMS1 MaxPSASize field and
 * its 2^(MaxPSASize+1) array size (xHCI 1.2c 5.3.6), the Endpoint Context's
 * MaxPStreams and LSA (Table 6-8), the Stream Context (6.2.4.1) and its SCT
 * (Table 6-13), the Set TR Dequeue Pointer command (6.4.3.9), the doorbell
 * register (5.6), Table 6-1's boundaries, and the SuperSpeed Endpoint
 * Companion descriptor (USB 3.2 9.6.7, Table 9-27). The spec PDF was not at
 * hand when these were written; the rows docs\usb-xhci-info\
 * xhci-data-structures.md marks "to verify" are the ones to check first.
 *
 * Policy rather than specification, and tested as policy: the 32-entry cap,
 * the array never under 4 entries, the page-rounded block with each ring at
 * a multiple of its own size, and Stream ID 0 left zero.
 */

#include <stdio.h>
#include "../src/xhci_stream.h"
#include "test_harness.h"

/* ------------------------------------------------------------------ */
/* The controller's limit                                               */
/* ------------------------------------------------------------------ */

static void test_hc_entries(void)
{
    CHECK_EQ(XhciStreamHcEntries(0x00000000UL), 0UL, "MaxPSASize 0: none");
    CHECK_EQ(XhciStreamHcEntries(0xFFFF0FFFUL), 0UL,
             "MaxPSASize 0 among other bits set");
    CHECK_EQ(XhciStreamHcEntries(0x00001000UL), 4UL, "MaxPSASize 1: 2^2");
    CHECK_EQ(XhciStreamHcEntries(0x00003000UL), 16UL, "MaxPSASize 3: 2^4");
    CHECK_EQ(XhciStreamHcEntries(0x00087001UL), 256UL,
             "MaxPSASize 7 with xECP and AC64: 2^8");
    CHECK_EQ(XhciStreamHcEntries(0x0000F000UL), 65536UL,
             "MaxPSASize 15: 2^16");
    CHECK_EQ(XhciStreamHcEntries(0xFFFFFFFFUL), 0UL,
             "an all-ones read is no controller");
}

/* ------------------------------------------------------------------ */
/* The companion descriptor                                             */
/* ------------------------------------------------------------------ */

/* A configuration: header (9), interface (9), bulk IN 0x81 (7), its
 * companion (6, MaxStreams `exp`), bulk OUT 0x02 (7) with no companion. */
static ULONG make_config(UCHAR *c, ULONG exp, ULONG companionLength)
{
    ULONG n;
    ULONG i;

    for (i = 0; i < 64; i++) {
        c[i] = 0;
    }
    n = 0;
    c[n + 0] = 9; c[n + 1] = 2; c[n + 4] = 1; c[n + 5] = 1;
    n += 9;
    c[n + 0] = 9; c[n + 1] = 4; c[n + 2] = 0; c[n + 3] = 0; c[n + 4] = 2;
    c[n + 5] = 8; c[n + 6] = 6; c[n + 7] = 0x62;
    n += 9;
    c[n + 0] = 7; c[n + 1] = 5; c[n + 2] = 0x81; c[n + 3] = 2;
    c[n + 4] = 0x00; c[n + 5] = 0x04;
    n += 7;
    c[n + 0] = (UCHAR)companionLength; c[n + 1] = 0x30; c[n + 2] = 15;
    c[n + 3] = (UCHAR)exp;
    n += companionLength;
    c[n + 0] = 7; c[n + 1] = 5; c[n + 2] = 0x02; c[n + 3] = 2;
    c[n + 4] = 0x00; c[n + 5] = 0x04;
    n += 7;
    c[2] = (UCHAR)(n & 0xFF);
    c[3] = (UCHAR)(n >> 8);
    return n;
}

static void test_companion(void)
{
    UCHAR c[64];
    ULONG n;
    ULONG exp;

    n = make_config(c, 5, 6);
    CHECK_EQ(n, 38UL, "the configuration's length");
    exp = 99;
    CHECK_EQ(XhciStreamCompanionExponent(c, n, 18, &exp), XHCI_STREAM_OK,
             "bulk IN with a companion naming 2^5 streams");
    CHECK_EQ(exp, 5UL, "its exponent");
    exp = 99;
    CHECK_EQ(XhciStreamCompanionExponent(c, n, 31, &exp), XHCI_STREAM_EP_NONE,
             "bulk OUT, the last descriptor, no companion");
    CHECK_EQ(exp, 0UL, "no exponent for it");

    /* bmAttributes bits 7:5 are not MaxStreams. */
    n = make_config(c, 0xE0 | 16, 6);
    CHECK_EQ(XhciStreamCompanionExponent(c, n, 18, &exp), XHCI_STREAM_OK,
             "MaxStreams 16, the largest, with bits 7:5 set");
    CHECK_EQ(exp, 16UL, "2^16");

    n = make_config(c, 0, 6);
    CHECK_EQ(XhciStreamCompanionExponent(c, n, 18, &exp), XHCI_STREAM_EP_NONE,
             "MaxStreams 0: a companion with no streams");
    n = make_config(c, 17, 6);
    CHECK_EQ(XhciStreamCompanionExponent(c, n, 18, &exp),
             XHCI_STREAM_MALFORMED, "MaxStreams 17 is reserved");
    n = make_config(c, 4, 5);
    CHECK_EQ(XhciStreamCompanionExponent(c, n, 18, &exp),
             XHCI_STREAM_MALFORMED, "a companion of 5 bytes");

    /* A companion cut off by wTotalLength. */
    n = make_config(c, 4, 6);
    c[2] = 28;
    CHECK_EQ(XhciStreamCompanionExponent(c, n, 18, &exp),
             XHCI_STREAM_MALFORMED, "a companion past wTotalLength");
    c[2] = 25;
    CHECK_EQ(XhciStreamCompanionExponent(c, n, 18, &exp), XHCI_STREAM_EP_NONE,
             "the endpoint the last descriptor wTotalLength covers");

    n = make_config(c, 4, 6);
    CHECK_EQ(XhciStreamCompanionExponent(c, n, 38, &exp),
             XHCI_STREAM_BAD_PARAM, "an offset at the end");
    CHECK_EQ(XhciStreamCompanionExponent(NULL, n, 18, &exp),
             XHCI_STREAM_BAD_PARAM, "no configuration");
    CHECK_EQ(XhciStreamCompanionExponent(c, n, 18, NULL),
             XHCI_STREAM_BAD_PARAM, "nowhere to write");
}

/* ------------------------------------------------------------------ */
/* The plan                                                             */
/* ------------------------------------------------------------------ */

static void plan_case(ULONG requested, ULONG exp, ULONG hc, ULONG entries,
                      ULONG streams, ULONG maxP, const char *what)
{
    XHCI_STREAM_PLAN plan;

    plan.Entries = plan.Streams = plan.MaxPStreams = 0xDEADUL;
    check_eq_impl(XhciStreamPlan(requested, exp, hc, 32, &plan) ==
                      XHCI_STREAM_OK,
                  1, what, __FILE__, __LINE__);
    check_eq_impl(plan.Entries, entries, what, __FILE__, __LINE__);
    check_eq_impl(plan.Streams, streams, what, __FILE__, __LINE__);
    check_eq_impl(plan.MaxPStreams, maxP, what, __FILE__, __LINE__);
}

static void plan_refused(ULONG requested, ULONG exp, ULONG hc, ULONG cap,
                         ULONG want, const char *what)
{
    XHCI_STREAM_PLAN plan;

    plan.Entries = plan.Streams = plan.MaxPStreams = 0xDEADUL;
    check_eq_impl(XhciStreamPlan(requested, exp, hc, cap, &plan), want, what,
                  __FILE__, __LINE__);
    check_eq_impl(plan.Entries == 0xDEADUL && plan.Streams == 0xDEADUL &&
                      plan.MaxPStreams == 0xDEADUL,
                  1, "nothing written on a refusal", __FILE__, __LINE__);
}

static void test_plan(void)
{
    /* Entries = the next power of two above streams + 1, never under 4;
     * MaxPStreams = log2(Entries) - 1. */
    plan_case(1, 4, 256, 4, 1, 1, "one stream: the smallest array");
    plan_case(3, 4, 256, 4, 3, 1, "three: still 4 entries");
    plan_case(4, 4, 256, 8, 4, 2, "four need 5 entries: 8");
    plan_case(7, 4, 256, 8, 7, 2, "seven fill 8");
    plan_case(15, 4, 256, 16, 15, 3, "fifteen fill 16");
    plan_case(16, 16, 256, 32, 16, 4, "sixteen need 32");
    plan_case(31, 16, 256, 32, 31, 4, "thirty-one fill the cap");
    plan_case(32, 16, 256, 32, 31, 4, "thirty-two: the cap's 31");
    plan_case(1000, 16, 256, 32, 31, 4, "far over the cap");
    plan_case(31, 4, 256, 32, 16, 4, "the device's 2^4 = 16");
    plan_case(31, 1, 256, 4, 2, 1, "the device's 2^1 = 2");
    plan_case(31, 16, 4, 4, 3, 1, "a controller of 4 entries: 3 streams");
    plan_case(31, 16, 8, 8, 7, 2, "a controller of 8 entries: 7 streams");
    plan_case(5, 16, 16, 8, 5, 2, "within a 16-entry controller");

    plan_refused(4, 4, 0, 32, XHCI_STREAM_HC_NONE, "MaxPSASize 0");
    plan_refused(4, 4, 2, 32, XHCI_STREAM_HC_NONE,
                 "an array of 2 holds no LSA stream");
    plan_refused(4, 0, 256, 32, XHCI_STREAM_EP_NONE, "MaxStreams 0");
    plan_refused(4, 17, 256, 32, XHCI_STREAM_MALFORMED, "MaxStreams 17");
    plan_refused(0, 4, 256, 32, XHCI_STREAM_BAD_PARAM, "none requested");
    plan_refused(4, 4, 256, 24, XHCI_STREAM_BAD_PARAM,
                 "a cap that is not a power of two");
    plan_refused(4, 4, 256, 2, XHCI_STREAM_BAD_PARAM, "a cap below 4");
    CHECK_EQ(XhciStreamPlan(4, 4, 256, 32, NULL), XHCI_STREAM_BAD_PARAM,
             "nowhere to write");
}

/* ------------------------------------------------------------------ */
/* The common buffer                                                    */
/* ------------------------------------------------------------------ */

static void test_layout(void)
{
    XHCI_STREAM_LAYOUT l;
    ULONG id;
    ULONG crossing;

    CHECK_EQ(XhciStreamLayout(4, 3, 64, &l), XHCI_STREAM_OK, "4 entries");
    CHECK_EQ(l.ArrayBytes, 64UL, "4 x 16 bytes");
    CHECK_EQ(l.RingBase, 1024UL, "the first ring at its own size");
    CHECK_EQ(l.RingBytes, 1024UL, "64 TRBs");
    CHECK_EQ(l.TotalBytes, 4096UL, "1 KB + 3 KB: one page");
    CHECK_EQ(XhciStreamRingOffset(&l, 1), 1024UL, "stream 1");
    CHECK_EQ(XhciStreamRingOffset(&l, 3), 3072UL, "stream 3");

    CHECK_EQ(XhciStreamLayout(4, 1, 64, &l), XHCI_STREAM_OK, "one stream");
    CHECK_EQ(l.TotalBytes, 4096UL, "2 KB rounded up to a page");

    CHECK_EQ(XhciStreamLayout(8, 7, 64, &l), XHCI_STREAM_OK, "8 entries");
    CHECK_EQ(l.TotalBytes, 8192UL, "1 KB + 7 KB");

    CHECK_EQ(XhciStreamLayout(32, 31, 64, &l), XHCI_STREAM_OK, "the cap");
    CHECK_EQ(l.ArrayBytes, 512UL, "32 x 16 bytes");
    CHECK_EQ(l.TotalBytes, 32768UL, "1 KB + 31 KB");
    crossing = 0;
    for (id = 1; id <= 31; id++) {
        if ((XhciStreamRingOffset(&l, id) % 4096UL) + l.RingBytes > 4096UL) {
            crossing++;
        }
    }
    CHECK_EQ(crossing, 0UL, "no ring crosses a page (nor so 64 KB)");
    CHECK(XhciStreamRingOffset(&l, 31) + l.RingBytes <= l.TotalBytes,
          "the last ring inside the block");

    CHECK_EQ(XhciStreamLayout(32, 31, 256, &l), XHCI_STREAM_OK,
             "page-sized rings");
    CHECK_EQ(l.RingBase, 4096UL, "the array alone in the first page");
    CHECK_EQ(l.TotalBytes, 32UL * 4096UL, "a page per ring after it");

    CHECK_EQ(XhciStreamLayout(6, 3, 64, &l), XHCI_STREAM_BAD_PARAM,
             "6 entries");
    CHECK_EQ(XhciStreamLayout(64, 31, 64, &l), XHCI_STREAM_BAD_PARAM,
             "above the cap");
    CHECK_EQ(XhciStreamLayout(2, 1, 64, &l), XHCI_STREAM_BAD_PARAM,
             "below 4");
    CHECK_EQ(XhciStreamLayout(4, 0, 64, &l), XHCI_STREAM_BAD_PARAM,
             "no stream");
    CHECK_EQ(XhciStreamLayout(4, 4, 64, &l), XHCI_STREAM_BAD_PARAM,
             "as many streams as entries: ID 0 has no room");
    CHECK_EQ(XhciStreamLayout(4, 3, 48, &l), XHCI_STREAM_BAD_PARAM,
             "a ring that is not a power of two");
    CHECK_EQ(XhciStreamLayout(4, 3, 512, &l), XHCI_STREAM_BAD_PARAM,
             "a ring of more than a page");
    CHECK_EQ(XhciStreamLayout(4, 3, 2, &l), XHCI_STREAM_BAD_PARAM,
             "a ring of 2 TRBs");
    CHECK_EQ(XhciStreamLayout(4, 3, 64, NULL), XHCI_STREAM_BAD_PARAM,
             "nowhere to write");
}

/* ------------------------------------------------------------------ */
/* Encoders                                                             */
/* ------------------------------------------------------------------ */

static void fill(volatile ULONG *w, ULONG n, ULONG v)
{
    ULONG i;

    for (i = 0; i < n; i++) {
        w[i] = v;
    }
}

static void test_stream_context(void)
{
    ULONG c[4];

    fill(c, 4, 0xDEADBEEFUL);
    CHECK_EQ(XhciStreamContextBuild(c, 0x00201400UL, 1, 1), XHCI_STREAM_OK,
             "a primary ring, DCS 1");
    CHECK_EQ(c[0], 0x00201403UL, "pointer | SCT 1 << 1 | DCS");
    CHECK_EQ(c[1], 0UL, "the pointer's high half");
    CHECK_EQ(c[2], 0UL, "Stopped EDTLA");
    CHECK_EQ(c[3], 0UL, "reserved");

    fill(c, 4, 0xDEADBEEFUL);
    CHECK_EQ(XhciStreamContextBuild(c, 0x00201400UL, 0, 1), XHCI_STREAM_OK,
             "DCS 0");
    CHECK_EQ(c[0], 0x00201402UL, "SCT alone");

    fill(c, 4, 0xDEADBEEFUL);
    CHECK_EQ(XhciStreamContextBuild(c, 0, 0, 0), XHCI_STREAM_OK,
             "the zero context of Stream ID 0");
    CHECK_EQ(c[0] | c[1] | c[2] | c[3], 0UL, "all zero");

    fill(c, 4, 0x11111111UL);
    CHECK_EQ(XhciStreamContextBuild(c, 0x00201408UL, 1, 1),
             XHCI_STREAM_BAD_PARAM, "a pointer off 16 bytes");
    CHECK_EQ(c[0], 0x11111111UL, "nothing written on a refusal");
    CHECK_EQ(XhciStreamContextBuild(c, 0x00201400UL, 2, 1),
             XHCI_STREAM_BAD_PARAM, "DCS 2");
    CHECK_EQ(XhciStreamContextBuild(c, 0x00201400UL, 1, 8),
             XHCI_STREAM_BAD_PARAM, "SCT 8");
    CHECK_EQ(XhciStreamContextBuild(c, 0, 0, 1), XHCI_STREAM_BAD_PARAM,
             "a type with no ring");
    CHECK_EQ(XhciStreamContextBuild(c, 0, 1, 0), XHCI_STREAM_BAD_PARAM,
             "a cycle state with no ring");
    CHECK_EQ(XhciStreamContextBuild(NULL, 0x00201400UL, 1, 1),
             XHCI_STREAM_BAD_PARAM, "nowhere to write");
}

static void test_endpoint_context(void)
{
    ULONG c[8];

    /* DW0 as XhciBuildEndpointContext leaves a HS-style bulk endpoint:
     * Interval 0, Mult 0, nothing in 15:10; DW1 type and size; DW2 the
     * pointer it was given; DW4 the average TRB length. */
    fill(c, 8, 0);
    c[0] = 0x00030000UL;
    c[1] = 0x04000036UL;
    c[2] = 0x00201000UL;
    c[4] = 0x00000C00UL;
    CHECK_EQ(XhciStreamEndpointContext(c, 4, 0x00201000UL), XHCI_STREAM_OK,
             "32 entries at 0x00201000");
    CHECK_EQ(c[0], 0x00039000UL, "Interval kept, MaxPStreams 4 << 10, LSA");
    CHECK_EQ(c[1], 0x04000036UL, "DW1 untouched");
    CHECK_EQ(c[2], 0x00201000UL, "the array, DCS 0");
    CHECK_EQ(c[3], 0UL, "high half");
    CHECK_EQ(c[4], 0x00000C00UL, "DW4 untouched");

    c[0] = 0x00007C00UL;
    CHECK_EQ(XhciStreamEndpointContext(c, 1, 0x00201010UL), XHCI_STREAM_OK,
             "an old MaxPStreams replaced");
    CHECK_EQ(c[0], 0x00008400UL, "MaxPStreams 1 and LSA only");
    CHECK_EQ(c[2], 0x00201010UL, "16-byte aligned is enough");

    c[0] = 0x12345678UL;
    CHECK_EQ(XhciStreamEndpointContext(c, 0, 0x00201000UL),
             XHCI_STREAM_BAD_PARAM, "MaxPStreams 0 is no streams");
    CHECK_EQ(XhciStreamEndpointContext(c, 16, 0x00201000UL),
             XHCI_STREAM_BAD_PARAM, "MaxPStreams 16");
    CHECK_EQ(XhciStreamEndpointContext(c, 4, 0), XHCI_STREAM_BAD_PARAM,
             "no array");
    CHECK_EQ(XhciStreamEndpointContext(c, 4, 0x00201008UL),
             XHCI_STREAM_BAD_PARAM, "an array off 16 bytes");
    CHECK_EQ(c[0], 0x12345678UL, "nothing written on a refusal");
}

static void test_set_dequeue(void)
{
    XHCI_TRB t;

    CHECK_EQ(XhciStreamSetDequeueTrb(&t, 5, 3, 7, 0x00345670UL, 1, 1),
             XHCI_STREAM_OK, "slot 5, DCI 3, stream 7");
    CHECK_EQ(t.Param0, 0x00345673UL, "pointer | SCT 1 << 1 | DCS 1");
    CHECK_EQ(t.Param1, 0UL, "high half");
    CHECK_EQ(t.Status, 0x00070000UL, "Stream ID in 31:16");
    CHECK_EQ(t.Control, 0x05034000UL,
             "type 16 << 10, EP ID 3 << 16, slot 5 << 24, cycle clear");

    CHECK_EQ(XhciStreamSetDequeueTrb(&t, 32, 31, 0xFFFF, 0x00345680UL, 0, 1),
             XHCI_STREAM_OK, "the widest IDs");
    CHECK_EQ(t.Param0, 0x00345682UL, "DCS 0");
    CHECK_EQ(t.Status, 0xFFFF0000UL, "Stream ID 0xFFFF");
    CHECK_EQ(t.Control, 0x201F4000UL, "slot 32, EP ID 31");

    CHECK_EQ(XhciStreamSetDequeueTrb(&t, 0, 3, 7, 0x00345670UL, 1, 1),
             XHCI_STREAM_BAD_PARAM, "slot 0");
    CHECK_EQ(XhciStreamSetDequeueTrb(&t, 5, 0, 7, 0x00345670UL, 1, 1),
             XHCI_STREAM_BAD_PARAM, "DCI 0");
    CHECK_EQ(XhciStreamSetDequeueTrb(&t, 5, 32, 7, 0x00345670UL, 1, 1),
             XHCI_STREAM_BAD_PARAM, "DCI 32");
    CHECK_EQ(XhciStreamSetDequeueTrb(&t, 5, 3, 0, 0x00345670UL, 1, 1),
             XHCI_STREAM_BAD_PARAM, "Stream ID 0: the non-stream command");
    CHECK_EQ(XhciStreamSetDequeueTrb(&t, 5, 3, 0x10000UL, 0x00345670UL, 1, 1),
             XHCI_STREAM_BAD_PARAM, "Stream ID past 16 bits");
    CHECK_EQ(XhciStreamSetDequeueTrb(&t, 5, 3, 7, 0, 1, 1),
             XHCI_STREAM_BAD_PARAM, "no pointer");
    CHECK_EQ(XhciStreamSetDequeueTrb(&t, 5, 3, 7, 0x00345674UL, 1, 1),
             XHCI_STREAM_BAD_PARAM, "a pointer off 16 bytes");
    CHECK_EQ(XhciStreamSetDequeueTrb(&t, 5, 3, 7, 0x00345670UL, 2, 1),
             XHCI_STREAM_BAD_PARAM, "DCS 2");
    CHECK_EQ(XhciStreamSetDequeueTrb(&t, 5, 3, 7, 0x00345670UL, 1, 0),
             XHCI_STREAM_BAD_PARAM, "SCT 0, a secondary ring");
    CHECK_EQ(XhciStreamSetDequeueTrb(&t, 5, 3, 7, 0x00345670UL, 1, 8),
             XHCI_STREAM_BAD_PARAM, "SCT 8");
    CHECK_EQ(XhciStreamSetDequeueTrb(NULL, 5, 3, 7, 0x00345670UL, 1, 1),
             XHCI_STREAM_BAD_PARAM, "nowhere to write");
}

static void test_doorbell(void)
{
    CHECK_EQ(XhciStreamDoorbell(3, 7), 0x00070003UL, "DCI 3, stream 7");
    CHECK_EQ(XhciStreamDoorbell(31, 0xFFFF), 0xFFFF001FUL, "the widest");
    CHECK_EQ(XhciStreamDoorbell(2, 0), 2UL, "no stream: the DCI alone");
}

/* ------------------------------------------------------------------ */
/* Events                                                               */
/* ------------------------------------------------------------------ */

static void ring_at(XHCI_RING *r, ULONG pa, ULONG trbs)
{
    ULONG i;

    for (i = 0; i < sizeof(*r); i++) {
        ((UCHAR *)r)[i] = 0;
    }
    r->BasePA = pa;
    r->Trbs = trbs;
}

static void test_find(void)
{
    XHCI_RING r1;
    XHCI_RING r2;
    XHCI_RING r4;
    const XHCI_RING *rings[5];

    ring_at(&r1, 0x00010000UL, 64);
    ring_at(&r2, 0x00010400UL, 64);
    ring_at(&r4, 0x00011000UL, 64);
    rings[0] = NULL;
    rings[1] = &r1;
    rings[2] = &r2;
    rings[3] = NULL;
    rings[4] = &r4;

    CHECK_EQ(XhciStreamFind(rings, 5, 0x00010000UL), 1UL, "stream 1's first");
    CHECK_EQ(XhciStreamFind(rings, 5, 0x000103F0UL), 1UL,
             "stream 1's Link TRB");
    CHECK_EQ(XhciStreamFind(rings, 5, 0x00010400UL), 2UL, "stream 2's first");
    CHECK_EQ(XhciStreamFind(rings, 5, 0x000107F0UL), 2UL, "stream 2's last");
    CHECK_EQ(XhciStreamFind(rings, 5, 0x00010800UL), 0UL,
             "past stream 2, where unused stream 3 has no ring");
    CHECK_EQ(XhciStreamFind(rings, 5, 0x00011200UL), 4UL, "inside stream 4");
    CHECK_EQ(XhciStreamFind(rings, 4, 0x00011200UL), 0UL,
             "stream 4 outside the count given");
    CHECK_EQ(XhciStreamFind(rings, 5, 0x0000FFF0UL), 0UL, "below them all");
    CHECK_EQ(XhciStreamFind(rings, 5, 0), 0UL, "a zero pointer");
    CHECK_EQ(XhciStreamFind(rings, 5, 0x00010008UL), 0UL,
             "not on a TRB boundary");
    CHECK_EQ(XhciStreamFind(NULL, 5, 0x00010000UL), 0UL, "no table");
}

int main(void)
{
    test_hc_entries();
    test_companion();
    test_plan();
    test_layout();
    test_stream_context();
    test_endpoint_context();
    test_set_dequeue();
    test_doorbell();
    test_find();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
