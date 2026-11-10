/*
 * test_cm_rirq.c - the pure half of the NHI's ring interrupt
 * (src/cm/cm_rirq.c; design record 21 section 4.5, record 22 section 5.4;
 * task 45.22): the cause's geometry, the service routine's outcomes, the
 * storm count, the mask by host contract and its proof, the watchdog's
 * suspicion and slower storm, the order down's end and failure latch,
 * whether the interrupt is wanted, and the waits to a deadline; and the
 * networking data ring's cause beside ring 0's (record 35 section 5.5,
 * task 48.38): its bit for every N, the causes a status shows, the mask
 * of a data bit by both contracts in ring 0's DW and the next, both
 * causes masked at a fast storm, the acknowledgement with auto-clear
 * disabled, and the slow-storm watchdog per cause.
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

static ULONG cls(const CM_RIRQ_GEOM *g, ULONG v0, ULONG v1)
{
    ULONG v[CM_RIRQ_MAX_DWORDS];

    v[0] = v0;
    v[1] = v1;
    return CmRirqClassify(g, v);
}

static void test_classify(void)
{
    CM_RIRQ_GEOM g;

    (void)CmRirqGeometry(12, &g);
    CHECK_EQ(cls(&g, 0xFFFFFFFFUL, 0xFFFFFFFFUL), CM_RIRQ_ISR_ONES,
             "all ones: no answer, not claimed");
    CHECK_EQ(cls(&g, 0, 0), CM_RIRQ_ISR_OTHER, "nothing set: another device's");
    CHECK_EQ(cls(&g, 0x0001, 0), CM_RIRQ_ISR_OTHER,
             "TX ring 0's masked cause alone: not claimed");
    CHECK_EQ(cls(&g, 0x1000, 0), CM_RIRQ_ISR_MINE, "bit 12: claimed");
    CHECK_EQ(cls(&g, 0x1001, 0), CM_RIRQ_ISR_MINE,
             "bit 12 with masked causes beside it: claimed");
    CHECK_EQ(cls(&g, 0xFFFFFFFEUL, 0), CM_RIRQ_ISR_MINE,
             "a readable DW with the bit: claimed");
    CHECK_EQ(cls(&g, 0x2000, 0), CM_RIRQ_ISR_OTHER,
             "ring 1's receive bit while its cause is not open: not claimed");
}

/* Record 35 section 5.5: the data ring's cause. */
static void test_data(void)
{
    CM_RIRQ_GEOM g;
    CM_RIRQ_WRITE w;
    CM_RIRQ_WRITE m[2];
    ULONG v[CM_RIRQ_MAX_DWORDS];
    ULONG n;
    ULONG r;

    /* The bit, N + r, for ring 1 and 2 at every N. */
    (void)CmRirqGeometry(12, &g);
    CHECK_EQ(g.DataBit | g.DataRing, 0, "no data cause by default");
    CHECK(CmRirqGeomData(&g, 1), "Alpine Ridge's ring 1");
    CHECK(g.DataDw == 0 && g.DataBit == 0x2000 && g.DataRing == 1, "bit 13 of DW 0");
    CHECK(CmRirqGeomData(&g, 2), "ring 2 (Falcon Ridge's)");
    CHECK(g.DataDw == 0 && g.DataBit == 0x4000, "bit 14");
    CHECK(!CmRirqGeomData(&g, 0), "ring 0 is not a data ring");
    CHECK_EQ(g.DataBit, 0, "and clears it");
    CHECK(!CmRirqGeomData(&g, 12), "ring N: none");
    for (n = 1; n <= TBT_HI_CAPS_PATHS_MAX; n++) {
        (void)CmRirqGeometry(n, &g);
        for (r = 1; r <= 2; r++) {
            if (r >= n) {
                CHECK(!CmRirqGeomData(&g, r), "no such ring at this N");
                continue;
            }
            CHECK(CmRirqGeomData(&g, r) && g.DataDw == (n + r) / 32UL &&
                      g.DataBit == (1UL << ((n + r) % 32UL)) && g.DataDw < g.Dwords,
                  "every N: the data bit inside the status");
        }
    }
    (void)CmRirqGeometry(21, &g);
    CHECK(CmRirqGeomData(&g, 11) && g.DataDw == 1 && g.DataBit == 0x1, "bit 32: the next DW");

    /* The causes and the classification with the data cause open. */
    (void)CmRirqGeometry(12, &g);
    (void)CmRirqGeomData(&g, 1);
    v[0] = 0x1000; v[1] = 0;
    CHECK_EQ(CmRirqCauses(&g, v), CM_RIRQ_CAUSE_RING0, "ring 0's bit");
    v[0] = 0x2000;
    CHECK_EQ(CmRirqCauses(&g, v), CM_RIRQ_CAUSE_DATA, "the data bit");
    CHECK_EQ(CmRirqClassify(&g, v), CM_RIRQ_ISR_MINE, "the data bit alone: claimed");
    v[0] = 0x3000;
    CHECK_EQ(CmRirqCauses(&g, v), CM_RIRQ_CAUSE_RING0 | CM_RIRQ_CAUSE_DATA, "both");
    v[0] = 0x0001;
    CHECK_EQ(CmRirqClassify(&g, v), CM_RIRQ_ISR_OTHER, "neither: not claimed");
    v[0] = 0xFFFFFFFFUL;
    CHECK_EQ(CmRirqClassify(&g, v), CM_RIRQ_ISR_ONES, "all ones");
    (void)CmRirqGeometry(21, &g);
    (void)CmRirqGeomData(&g, 11);
    v[0] = 0; v[1] = 0x1;
    CHECK_EQ(CmRirqCauses(&g, v), CM_RIRQ_CAUSE_DATA, "the data bit in the next DW");

    /* The data bit's mask by both contracts, in ring 0's DW and the next. */
    (void)CmRirqGeometry(12, &g);
    (void)CmRirqGeomData(&g, 1);
    CmRirqDataMaskWrite(&g, FALSE, TRUE, 0, &w);
    CHECK(w.Write && w.Offset == TBT_HI_IMS && w.Value == 0x2000, "IMS, the bit alone");
    CmRirqDataMaskWrite(&g, FALSE, FALSE, 0, &w);
    CHECK(w.Write && w.Offset == TBT_HI_IMC && w.Value == 0x2000, "IMC, the bit alone");
    CmRirqDataMaskWrite(&g, TRUE, TRUE, 0x1000, &w);
    CHECK(w.Write && w.Offset == TBT_HI_IMR && w.Value == 0x3000,
          "Intel: ring 0's enable kept");
    CmRirqDataMaskWrite(&g, TRUE, FALSE, 0x3000, &w);
    CHECK(w.Write && w.Offset == TBT_HI_IMR && w.Value == 0x1000,
          "Intel: the data bit alone cleared, ring 0's kept");
    CmRirqDataMaskWrite(&g, TRUE, FALSE, 0xFFFFFFFFUL, &w);
    CHECK(!w.Write, "Intel: all ones never written back");
    CHECK(CmRirqDataMaskProven(&g, TRUE, 0x3000), "open, read back");
    CHECK(!CmRirqDataMaskProven(&g, TRUE, 0x1000), "open, read back clear: unproven");
    CHECK(CmRirqDataMaskProven(&g, FALSE, 0x1000), "masked, ring 0's still open");
    CHECK(!CmRirqDataMaskProven(&g, FALSE, 0xFFFFFFFFUL), "all ones proves nothing");
    (void)CmRirqGeometry(21, &g);
    (void)CmRirqGeomData(&g, 11);
    CmRirqDataMaskWrite(&g, FALSE, TRUE, 0, &w);
    CHECK(w.Write && w.Offset == TBT_HI_IMS + 4 && w.Value == 0x1, "the next DW's IMS");
    CmRirqDataMaskWrite(&g, TRUE, TRUE, 0x80, &w);
    CHECK(w.Write && w.Offset == TBT_HI_IMR + 4 && w.Value == 0x81,
          "Intel: the next DW's own value kept");
    CmRirqDataMaskWrite(&g, FALSE, FALSE, 0, &w);
    CHECK(w.Write && w.Offset == TBT_HI_IMC + 4 && w.Value == 0x1, "the next DW's IMC");

    /* Both masked at once (the fast storm, the order down). */
    (void)CmRirqGeometry(12, &g);
    CHECK_EQ(CmRirqMaskBoth(&g, FALSE, 0, 0, m), 1, "no data cause: ring 0's alone");
    CHECK(m[0].Offset == TBT_HI_IMC && m[0].Value == 0x1000, "IMC, ring 0's bit");
    (void)CmRirqGeomData(&g, 1);
    CHECK_EQ(CmRirqMaskBoth(&g, FALSE, 0, 0, m), 1, "one DW: one write");
    CHECK(m[0].Offset == TBT_HI_IMC && m[0].Value == 0x3000, "both bits to IMC");
    CHECK_EQ(CmRirqMaskBoth(&g, TRUE, 0x3001, 0x3001, m), 1, "Intel, one DW: one write");
    CHECK(m[0].Offset == TBT_HI_IMR && m[0].Value == 0x0001,
          "both cleared in the one write, the rest kept");
    CHECK(CmRirqBothMasked(&g, 0x0001, 0x0001), "proven");
    CHECK(!CmRirqBothMasked(&g, 0x2001, 0x2001), "the data bit still set: unproven");
    CHECK(!CmRirqBothMasked(&g, 0x1001, 0x1001), "ring 0's still set: unproven");
    (void)CmRirqGeometry(21, &g);
    (void)CmRirqGeomData(&g, 11);
    CHECK_EQ(CmRirqMaskBoth(&g, FALSE, 0, 0, m), 2, "two DWs: two writes");
    CHECK(m[0].Offset == TBT_HI_IMC && m[0].Value == 0x200000 &&
              m[1].Offset == TBT_HI_IMC + 4 && m[1].Value == 0x1,
          "each DW's bit to its IMC");
    CHECK_EQ(CmRirqMaskBoth(&g, TRUE, 0x200000, 0x81, m), 2, "Intel, two DWs");
    CHECK(m[0].Offset == TBT_HI_IMR && m[0].Value == 0 &&
              m[1].Offset == TBT_HI_IMR + 4 && m[1].Value == 0x80,
          "each DW read-modify-written");
    CHECK_EQ(CmRirqMaskBoth(&g, TRUE, 0xFFFFFFFFUL, 0x81, m), 1,
             "Intel: an all-ones DW not written, the other still");
    CHECK(m[0].Offset == TBT_HI_IMR + 4, "the data DW");
    CHECK(CmRirqBothMasked(&g, 0, 0x80), "both DWs proven");
    CHECK(!CmRirqBothMasked(&g, 0, 0x81), "the next DW's bit set: unproven");
    CHECK(!CmRirqBothMasked(&g, 0, 0xFFFFFFFFUL), "all ones: unproven");
}

/* Record 21 section 4.5.2, record 35 section 5.5: auto-clear disabled. */
static void test_ack(void)
{
    CM_RIRQ_GEOM g;
    CM_RIRQ_WRITE w[CM_RIRQ_MAX_DWORDS];
    ULONG v[CM_RIRQ_MAX_DWORDS];

    (void)CmRirqGeometry(12, &g);
    (void)CmRirqGeomData(&g, 1);
    v[0] = 0x3000; v[1] = 0x5;
    CHECK_EQ(CmRirqAckWrites(&g, FALSE, v, w, 2), 0,
             "auto-clear on: the read is the acknowledgement, nothing written");
    v[0] = 0x1000; v[1] = 0;
    CHECK_EQ(CmRirqAckWrites(&g, TRUE, v, w, 2), 1, "ring 0's bit alone: one write");
    CHECK(w[0].Write && w[0].Offset == TBT_HI_ISC && w[0].Value == 0x1000, "to ISC");
    v[0] = 0x2000;
    CHECK_EQ(CmRirqAckWrites(&g, TRUE, v, w, 2), 1, "the data bit alone");
    CHECK(w[0].Offset == TBT_HI_ISC && w[0].Value == 0x2000, "its bit to ISC");
    v[0] = 0x3000;
    CHECK_EQ(CmRirqAckWrites(&g, TRUE, v, w, 2), 1, "both in one DW: one write");
    CHECK(w[0].Value == 0x3000, "both bits");
    v[0] = 0x3001; v[1] = 0x4;
    CHECK_EQ(CmRirqAckWrites(&g, TRUE, v, w, 2), 2, "every DW read non-zero");
    CHECK(w[0].Offset == TBT_HI_ISC && w[0].Value == 0x3001 &&
              w[1].Offset == TBT_HI_ISC + 4 && w[1].Value == 0x4,
          "each DW's bits as read at its own offset");
    v[0] = 0; v[1] = 0;
    CHECK_EQ(CmRirqAckWrites(&g, TRUE, v, w, 2), 0, "DWs reading 0: no write");
    v[0] = 0xFFFFFFFFUL; v[1] = 0xFFFFFFFFUL;
    CHECK_EQ(CmRirqAckWrites(&g, TRUE, v, w, 2), 0, "all ones: no write");
    (void)CmRirqGeometry(21, &g);
    (void)CmRirqGeomData(&g, 11);
    v[0] = 0x200000; v[1] = 0x1;
    CHECK_EQ(CmRirqAckWrites(&g, TRUE, v, w, 2), 2, "the data bit in the next DW");
    CHECK(w[0].Value == 0x200000 && w[1].Offset == TBT_HI_ISC + 4 && w[1].Value == 0x1,
          "two writes, each DW's own");
    v[0] = 0; v[1] = 0x1;
    CHECK_EQ(CmRirqAckWrites(&g, TRUE, v, w, 2), 1, "the data bit alone, next DW");
    CHECK(w[0].Offset == TBT_HI_ISC + 4, "at ISC + 4");
    CHECK_EQ(CmRirqAckWrites(&g, TRUE, v, w, 0), 0, "no room: nothing");
    (void)CmRirqGeometry(10, &g);
    v[0] = 0x400; v[1] = 0x1;
    CHECK_EQ(CmRirqAckWrites(&g, TRUE, v, w, 2), 1,
             "one status DW: the second never read or written");
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

    /* Per cause (record 35 section 5.5). The DPC runs between claims in
     * each of these, so the fast count never fires: the slow watch alone. */
    CmRirqWatchStart(&w, 0, 50);
    CHECK(!CmRirqWatchData(&w, 5000, 0), "the data cause not watched: never a storm");
    CmRirqWatchDataStart(&w, TRUE, 0, 0);
    CHECK_EQ(CmRirqWatchPass(&w, 0, 50, FALSE), CM_RIRQ_WATCH_OK,
             "ring 0 quiet: no claims of its own, no storm");
    CHECK(!CmRirqWatchData(&w, 1560, 1560),
          "1,560 data claims in a period with data progress: healthy load");
    CHECK(CmRirqWatchData(&w, 3120, 1560),
          "1,560 data claims with no data progress: a storm");
    CmRirqWatchStart(&w, 0, 50);
    CmRirqWatchDataStart(&w, TRUE, 0, 0);
    CHECK_EQ(CmRirqWatchPass(&w, 1100, 50, FALSE), CM_RIRQ_WATCH_STORM,
             "a stuck ring 0 cause (1,100 claims, no ring 0 frame)...");
    CHECK(!CmRirqWatchData(&w, 900, 900), "...beside a progressing data ring: a storm");
    CmRirqWatchStart(&w, 0, 50);
    CmRirqWatchDataStart(&w, TRUE, 0, 0);
    CHECK_EQ(CmRirqWatchPass(&w, 200, 51, FALSE), CM_RIRQ_WATCH_OK,
             "a progressing ring 0...");
    CHECK(CmRirqWatchData(&w, 1100, 0), "...beside a stuck data cause: a storm");
    CmRirqWatchDataStart(&w, FALSE, 0, 0);
    CHECK(!CmRirqWatchData(&w, 9000, 0), "masked again: no more watched");
    CmRirqWatchDataStart(&w, TRUE, 0xFFFFFF00UL, 3);
    CHECK(!CmRirqWatchData(&w, 0x10UL, 3), "the data claims' wrap: 0x110, no storm");
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
    CHECK(CmRirqWanted(CM_RIRQ_VALUE_LINE, TRUE, FALSE),
          "1, a line, no failure: tried (98 SE and ME too, task 48.26)");
    CHECK(!CmRirqWanted(CM_RIRQ_VALUE_OFF, TRUE, FALSE), "0: polling as before");
    CHECK(!CmRirqWanted(CM_RIRQ_VALUE_LINE, FALSE, FALSE),
          "no line resource (none, or a message): polling");
    CHECK(!CmRirqWanted(CM_RIRQ_VALUE_LINE, TRUE, TRUE),
          "the failure latch: polling until the next start");
    CHECK(!CmRirqWanted(2, TRUE, FALSE), "2 is not the line step");
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
    test_data();
    test_ack();
    test_watch();
    test_down();
    test_wanted();
    test_waits();
    printf("test_cm_rirq: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
