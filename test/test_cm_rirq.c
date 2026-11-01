/*
 * test_cm_rirq.c - the pure half of the NHI's ring interrupt
 * (src/cm/cm_rirq.c; design record 21 section 4.5, record 22 section 5.4;
 * task 45.22): the cause's geometry, the service routine's outcomes, the
 * storm count, the mask by host contract and its proof, the watchdog's
 * suspicion and slower storm, the order down's end and failure latch,
 * whether the interrupt is wanted, and the waits to a deadline.
 */

#include <stdio.h>
#include "../src/cm/cm_rirq.h"
#include "test_harness.h"

static void test_geometry(void)
{
    CM_RIRQ_GEOM g;
    ULONG n;

    CHECK(CmRirqGeometry(12, &g), "Alpine Ridge's 12 paths");
    CHECK_EQ(g.Paths, 12, "N");
    CHECK_EQ(g.Dwords, 2, "36 causes: two status DWs");
    CHECK_EQ(g.CauseDw, 0, "RX ring 0's cause in DW 0");
    CHECK_EQ(g.CauseBit, 0x1000, "bit 12 (N + 0)");
    CHECK(CmRirqGeometry(1, &g), "one path");
    CHECK_EQ(g.Dwords, 1, "3 causes: one DW");
    CHECK_EQ(g.CauseBit, 0x2, "bit 1");
    CHECK(CmRirqGeometry(10, &g), "ten paths");
    CHECK_EQ(g.Dwords, 1, "30 causes: one DW");
    CHECK_EQ(g.CauseBit, 0x400, "bit 10");
    CHECK(CmRirqGeometry(11, &g), "eleven paths");
    CHECK_EQ(g.Dwords, 2, "33 causes: two DWs");
    CHECK(CmRirqGeometry(21, &g), "the most a host has (Table 12-11)");
    CHECK_EQ(g.Dwords, 2, "63 causes: two DWs");
    CHECK_EQ(g.CauseBit, 0x200000, "bit 21");
    CHECK_EQ(CM_RIRQ_MAX_DWORDS, 2, "two DWs at most");
    CHECK(!CmRirqGeometry(0, &g), "no paths: refused");
    CHECK_EQ(g.Dwords | g.CauseBit, 0, "and zeroed");
    CHECK(!CmRirqGeometry(22, &g), "22 paths: refused");
    CHECK(!CmRirqGeometry(0x7FF, &g), "the field's most: refused");
    for (n = 1; n <= TBT_HI_CAPS_PATHS_MAX; n++) {
        (void)CmRirqGeometry(n, &g);
        CHECK(g.Dwords == TBT_HI_IMR_DWORDS(n) && g.Dwords <= CM_RIRQ_MAX_DWORDS &&
                  g.CauseDw < g.Dwords && g.CauseBit == (1UL << n),
              "every N: the cause inside the status the mask covers");
    }
}

static void test_classify(void)
{
    CM_RIRQ_GEOM g;

    (void)CmRirqGeometry(12, &g);
    CHECK_EQ(CmRirqClassify(&g, 0xFFFFFFFFUL, 0xFFFFFFFFUL), CM_RIRQ_ISR_ONES,
             "all ones: no answer, not claimed");
    CHECK_EQ(CmRirqClassify(&g, 0, 0), CM_RIRQ_ISR_OTHER, "nothing set: another device's");
    CHECK_EQ(CmRirqClassify(&g, 0x0001, 0x0001), CM_RIRQ_ISR_OTHER,
             "TX ring 0's masked cause alone: not claimed");
    CHECK_EQ(CmRirqClassify(&g, 0x1000, 0x1000), CM_RIRQ_ISR_MINE, "bit 12: claimed");
    CHECK_EQ(CmRirqClassify(&g, 0x1001, 0x1001), CM_RIRQ_ISR_MINE,
             "bit 12 with masked causes beside it: claimed");
    CHECK_EQ(CmRirqClassify(&g, 0xFFFFFFFEUL, 0xFFFFFFFEUL), CM_RIRQ_ISR_MINE,
             "a readable DW with the bit: claimed");
}

static void test_burst(void)
{
    CM_RIRQ_BURST b;
    ULONG i;
    BOOLEAN hit;

    CmRirqBurstReset(&b, 5);
    hit = FALSE;
    for (i = 1; i < CM_RIRQ_STORM_CLAIMS; i++) {
        hit = (BOOLEAN)(hit || CmRirqBurstClaim(&b, 5));
    }
    CHECK(!hit, "63 claims with no DPC between: no storm");
    CHECK(CmRirqBurstClaim(&b, 5), "the 64th is the storm");
    CHECK(CmRirqBurstClaim(&b, 5), "and stays one while no DPC runs");
    CHECK(!CmRirqBurstClaim(&b, 6), "a DPC ran: the count starts again");
    CHECK_EQ(b.Burst, 1, "at 1");
    for (i = 0; i < 1000; i++) {
        hit = CmRirqBurstClaim(&b, (LONG)(7 + i));
        if (hit) {
            break;
        }
    }
    CHECK(!hit, "a DPC between every claim: never a storm here (the thread's)");
    CmRirqBurstReset(&b, 9);
    CHECK_EQ(b.Burst, 0, "reset at the arm");
}

static void test_mask(void)
{
    CM_RIRQ_GEOM g;
    CM_RIRQ_WRITE w;

    (void)CmRirqGeometry(12, &g);
    CmRirqMaskWrite(&g, FALSE, TRUE, 0, &w);
    CHECK(w.Write && w.Offset == TBT_HI_IMS && w.Value == 0x1000,
          "other hosts: the bit to Interrupt Mask Set");
    CmRirqMaskWrite(&g, FALSE, FALSE, 0xFFFFFFFFUL, &w);
    CHECK(w.Write && w.Offset == TBT_HI_IMC && w.Value == 0x1000,
          "other hosts: the bit to Mask Clear, whatever was read");
    CmRirqMaskWrite(&g, TRUE, TRUE, 0x0001, &w);
    CHECK(w.Write && w.Offset == TBT_HI_IMR && w.Value == 0x1001,
          "Intel: the mask register itself, others kept");
    CmRirqMaskWrite(&g, TRUE, FALSE, 0x1001, &w);
    CHECK(w.Write && w.Offset == TBT_HI_IMR && w.Value == 0x0001,
          "Intel: the bit alone cleared");
    CmRirqMaskWrite(&g, TRUE, FALSE, 0xFFFFFFFFUL, &w);
    CHECK(!w.Write, "Intel: an all-ones read is never written back");
    CHECK(CmRirqMaskProven(&g, TRUE, 0x1000), "open, read back set");
    CHECK(!CmRirqMaskProven(&g, TRUE, 0x0000), "open, read back clear: a failed arm");
    CHECK(CmRirqMaskProven(&g, FALSE, 0x0001), "masked, read back clear");
    CHECK(!CmRirqMaskProven(&g, FALSE, 0x1000), "masked, read back set: unproven");
    CHECK(!CmRirqMaskProven(&g, FALSE, 0xFFFFFFFFUL), "all ones proves nothing");
    CHECK(!CmRirqMaskProven(&g, TRUE, 0xFFFFFFFFUL), "either way");
    CHECK_EQ(TBT_HI_IMS - TBT_HI_IMR, 0x10, "Spec 12.6.3: IMS 38210h");
    CHECK_EQ(TBT_HI_ISS - TBT_HI_ISR, 0x10, "ISS 37810h");
    CHECK_EQ(TBT_HI_ISC - TBT_HI_ISR, 0x08, "ISC 37808h");
    CHECK_EQ(TBT_HI_ITR0, 0x38C00, "ITR[0] 38C00h");
}

static void test_watch(void)
{
    CM_RIRQ_WATCH w;

    CmRirqWatchStart(&w, 10, 100);
    CHECK_EQ(CmRirqWatchPass(&w, 10, 100, FALSE), CM_RIRQ_WATCH_OK,
             "nothing pending: no suspicion");
    CHECK_EQ(CmRirqWatchPass(&w, 10, 100, TRUE), CM_RIRQ_WATCH_OK,
             "a frame pending, no claim since the drain: the first suspicion");
    CHECK_EQ(w.Suspects, 1, "one");
    CHECK_EQ(CmRirqWatchPass(&w, 10, 100, TRUE), CM_RIRQ_WATCH_SELFTEST,
             "the second in a row: the self-test again");
    CHECK_EQ(w.Suspects, 0, "and the run starts again");
    CHECK_EQ(CmRirqWatchPass(&w, 10, 100, TRUE), CM_RIRQ_WATCH_OK, "one");
    CHECK_EQ(CmRirqWatchPass(&w, 10, 100, FALSE), CM_RIRQ_WATCH_OK,
             "nothing pending between");
    CHECK_EQ(CmRirqWatchPass(&w, 10, 100, TRUE), CM_RIRQ_WATCH_OK,
             "not in a row: one again");
    CmRirqWatchDrained(&w, 11);
    CHECK_EQ(CmRirqWatchPass(&w, 12, 101, TRUE), CM_RIRQ_WATCH_OK,
             "a claim since the drain: its interrupt in flight, no suspicion");
    CHECK_EQ(w.Suspects, 0, "the run reset");
    CmRirqWatchDrained(&w, 12);
    CHECK_EQ(CmRirqWatchPass(&w, 12, 102, TRUE), CM_RIRQ_WATCH_OK, "one again");

    CmRirqWatchStart(&w, 0, 50);
    CHECK_EQ(CmRirqWatchPass(&w, 1000, 50, FALSE), CM_RIRQ_WATCH_OK,
             "1000 claims, no frame: not yet the slower storm");
    CHECK_EQ(CmRirqWatchPass(&w, 2001, 50, FALSE), CM_RIRQ_WATCH_STORM,
             "1001 claims in one period with no frame: the slower storm");
    CmRirqWatchStart(&w, 0, 50);
    CHECK_EQ(CmRirqWatchPass(&w, 5000, 51, FALSE), CM_RIRQ_WATCH_OK,
             "many claims with a frame taken: an enumeration, no storm");
    CmRirqWatchStart(&w, 0xFFFFFF00UL, 7);
    CHECK_EQ(CmRirqWatchPass(&w, 0x10UL, 7, FALSE), CM_RIRQ_WATCH_OK,
             "the claims' wrap: 0x110 claims, no storm");
}

static void test_down(void)
{
    CHECK_EQ(CmRirqDownEnd(CM_RIRQ_DOWN_FALLBACK, TRUE), CM_RIRQ_END_DISCONNECT,
             "fall back, retired: disconnected");
    CHECK_EQ(CmRirqDownEnd(CM_RIRQ_DOWN_FALLBACK, FALSE), CM_RIRQ_END_RETAIN,
             "fall back, unretired: retained");
    CHECK_EQ(CmRirqDownEnd(CM_RIRQ_DOWN_CONTAIN, TRUE), CM_RIRQ_END_DISCONNECT,
             "containment, retired: disconnected");
    CHECK_EQ(CmRirqDownEnd(CM_RIRQ_DOWN_CONTAIN, FALSE), CM_RIRQ_END_RETAIN,
             "containment, unretired: retained (D0 may assert)");
    CHECK_EQ(CmRirqDownEnd(CM_RIRQ_DOWN_D3, TRUE), CM_RIRQ_END_DISCONNECT,
             "D3, retired: disconnected");
    CHECK_EQ(CmRirqDownEnd(CM_RIRQ_DOWN_D3, FALSE), CM_RIRQ_END_UNPROVEN,
             "D3, unretired: disconnected unproven before it is forwarded");
    CHECK_EQ(CmRirqDownEnd(CM_RIRQ_DOWN_STOP, TRUE), CM_RIRQ_END_DISCONNECT,
             "stop, retired: disconnected");
    CHECK_EQ(CmRirqDownEnd(CM_RIRQ_DOWN_STOP, FALSE), CM_RIRQ_END_UNPROVEN,
             "stop, unretired: disconnected unproven (the resources go back)");
    CHECK(CmRirqDownFails(CM_RIRQ_DOWN_FALLBACK, CM_RIRQ_END_DISCONNECT),
          "every fall back sets the latch");
    CHECK(CmRirqDownFails(CM_RIRQ_DOWN_FALLBACK, CM_RIRQ_END_RETAIN), "retained too");
    CHECK(!CmRirqDownFails(CM_RIRQ_DOWN_CONTAIN, CM_RIRQ_END_DISCONNECT),
          "a containment's retirement does not: the recovery re-arms");
    CHECK(!CmRirqDownFails(CM_RIRQ_DOWN_CONTAIN, CM_RIRQ_END_RETAIN),
          "nor its retention");
    CHECK(!CmRirqDownFails(CM_RIRQ_DOWN_D3, CM_RIRQ_END_DISCONNECT),
          "a D3 retired: the resume re-arms");
    CHECK(CmRirqDownFails(CM_RIRQ_DOWN_D3, CM_RIRQ_END_UNPROVEN),
          "a D3 unproven: polling until the next start");
    CHECK(CmRirqDownFails(CM_RIRQ_DOWN_STOP, CM_RIRQ_END_UNPROVEN), "a stop unproven");
}

static void test_wanted(void)
{
    CHECK(CmRirqWanted(CM_RIRQ_VALUE_LINE, TRUE, TRUE, FALSE),
          "1, NT, a line, no failure: tried");
    CHECK(!CmRirqWanted(CM_RIRQ_VALUE_OFF, TRUE, TRUE, FALSE), "0: polling as before");
    CHECK(!CmRirqWanted(CM_RIRQ_VALUE_LINE, FALSE, TRUE, FALSE),
          "98 SE and ME: polling until the route is read");
    CHECK(!CmRirqWanted(CM_RIRQ_VALUE_LINE, TRUE, FALSE, FALSE),
          "no line resource (none, or a message): polling");
    CHECK(!CmRirqWanted(CM_RIRQ_VALUE_LINE, TRUE, TRUE, TRUE),
          "the failure latch: polling until the next start");
    CHECK(!CmRirqWanted(2, TRUE, TRUE, FALSE), "2 is not the line step");
    CHECK_EQ(CM_RIRQ_VALUE_DEFAULT, CM_RIRQ_VALUE_OFF, "off by default (owner, Q1, Q2)");
}

static void test_waits(void)
{
    CHECK_EQ(CmRirqLeftMs(1000, 1200, 200), 200, "a whole period left");
    CHECK_EQ(CmRirqLeftMs(1199, 1200, 200), 1, "a drain at 199 ms waits 1 ms, not 200");
    CHECK_EQ(CmRirqLeftMs(1200, 1200, 200), 0, "due: no wait");
    CHECK_EQ(CmRirqLeftMs(1300, 1200, 200), 0, "overdue: no wait");
    CHECK_EQ(CmRirqLeftMs(1000, 5000, 200), 200, "held to the period");
    CHECK_EQ(CmRirqLeftMs(0xFFFFFFF0UL, 0x10UL, 200), 0x20, "across the clock's wrap");
    CHECK_EQ(CmRirqLeftMs(0x10UL, 0xFFFFFFF0UL, 200), 0, "a deadline behind across the wrap");
    CHECK(CmRirqDue(1200, 1200), "due at the deadline");
    CHECK(!CmRirqDue(1199, 1200), "not before");
    CHECK(CmRirqDue(0x10UL, 0xFFFFFFF0UL), "after it across the wrap");
    CHECK(!CmRirqDue(0xFFFFFFF0UL, 0x10UL), "before it across the wrap");
}

int main(void)
{
    test_geometry();
    test_classify();
    test_burst();
    test_mask();
    test_watch();
    test_down();
    test_wanted();
    test_waits();
    printf("test_cm_rirq: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
