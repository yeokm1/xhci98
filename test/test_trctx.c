/*
 * test_trctx.c - host vectors for XHCITR's stream context reading
 * (xhcitr\tr_ctx.c; task 49.39, design record 17 section 4.12.9). C89.
 */

#include <stdio.h>
#include <string.h>
#include "../xhcitr/tr_ctx.h"
#include "test_harness.h"

#define BASE 0x10000000UL
#define TRB(i) (BASE + (i) * 16UL)

static char last[512];
static int lines;

static void sink(const char *line)
{
    lines++;
    strncpy(last, line, sizeof(last) - 1);
    last[sizeof(last) - 1] = '\0';
}

static void test_place(void)
{
    /* A 64-TRB ring, the Link at 63; dequeue at 4, enqueue at 10, cycle 1. */
    CHECK_EQ(tr_ctx_place(TRB(4) | 1, BASE, 64, TRB(4) | 1, TRB(10) | 1),
             TR_CTX_AT_DEQ, "at the driver's dequeue");
    CHECK_EQ(tr_ctx_place(TRB(7) | 1, BASE, 64, TRB(4) | 1, TRB(10) | 1),
             TR_CTX_INSIDE, "inside the queued window");
    CHECK_EQ(tr_ctx_place(TRB(10) | 1, BASE, 64, TRB(4) | 1, TRB(10) | 1),
             TR_CTX_INSIDE, "at the enqueue");
    CHECK_EQ(tr_ctx_place(TRB(7) | 0, BASE, 64, TRB(4) | 1, TRB(10) | 1),
             TR_CTX_WRONG_CYCLE, "inside, the cycle flipped");
    CHECK_EQ(tr_ctx_place(TRB(2) | 1, BASE, 64, TRB(4) | 1, TRB(10) | 1),
             TR_CTX_BEHIND, "on consumed TRBs of this lap: behind");
    CHECK_EQ(tr_ctx_place(TRB(30) | 0, BASE, 64, TRB(4) | 1, TRB(10) | 1),
             TR_CTX_BEHIND, "on the last lap's TRBs past the enqueue");
    CHECK_EQ(tr_ctx_place(TRB(30) | 1, BASE, 64, TRB(4) | 1, TRB(10) | 1),
             TR_CTX_AHEAD, "past the enqueue with this lap's cycle: ahead");
    CHECK_EQ(tr_ctx_place(TRB(64) | 1, BASE, 64, TRB(4) | 1, TRB(10) | 1),
             TR_CTX_OFF_RING, "past the ring");
    CHECK_EQ(tr_ctx_place(BASE - 16, BASE, 64, TRB(4) | 1, TRB(10) | 1),
             TR_CTX_OFF_RING, "before the ring");
    /* Wrapped: dequeue at 60 (cycle 1), enqueue at 3 (cycle 0). */
    CHECK_EQ(tr_ctx_place(TRB(61) | 1, BASE, 64, TRB(60) | 1, TRB(3) | 0),
             TR_CTX_INSIDE, "wrapped, before the Link");
    CHECK_EQ(tr_ctx_place(TRB(1) | 0, BASE, 64, TRB(60) | 1, TRB(3) | 0),
             TR_CTX_INSIDE, "wrapped, after the Link");
    CHECK_EQ(tr_ctx_place(TRB(63) | 1, BASE, 64, TRB(60) | 1, TRB(3) | 0),
             TR_CTX_INSIDE, "on the Link: its target, the cycle toggled");
    CHECK_EQ(tr_ctx_place(TRB(20) | 1, BASE, 64, TRB(60) | 1, TRB(3) | 0),
             TR_CTX_BEHIND, "wrapped, between the enqueue and the dequeue");
    CHECK_EQ(tr_ctx_place(TRB(20) | 0, BASE, 64, TRB(60) | 1, TRB(3) | 0),
             TR_CTX_AHEAD, "wrapped, past the enqueue on the new lap");
}

static void test_print(void)
{
    unsigned char b[64];

    memset(b, 0, sizeof(b));
    lines = 0;
    tr_ctx_print(b, 0, sink);
    CHECK_EQ(lines >= 1, 1, "an empty region prints its heading");
    CHECK_EQ(strstr(last, "no endpoint with streams") != NULL, 1,
             "and says none");
    lines = 0;
    tr_trace_print(b, 4, sink);
    CHECK_EQ(strstr(last, "too short") != NULL, 1, "a short trace is said");
}

static void test_ms(void)
{
    CHECK_EQ(tr_trace_ms(0), 0, "no time");
    CHECK_EQ(tr_trace_ms(10), 1, "10 units, 1.024 ms");
    CHECK_EQ(tr_trace_ms(5000000UL), 512000UL,
             "8.5 minutes: no wrap past 429 s");
    CHECK_EQ(tr_trace_ms(0xFFFFFFFFUL) > 400000000UL, 1,
             "the whole span, still in range");
}

int main(void)
{
    test_ms();
    test_place();
    test_print();
    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
