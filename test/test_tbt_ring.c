/*
 * test_tbt_ring.c - host vectors for the host interface's ring 0
 * (src\cm\tbt_ring.c; roadmap-usb4.md task 39-A.2, design record 21
 * section 10.1): the USB4 Specification 2.0 Appendix A.2 CRC-32C vectors,
 * the start's register programming, transmit and receive with wrap, the
 * vacant-descriptor rule on both rings (exhaustion), a frame larger than
 * its buffer, malformed lengths, trailer checks, the control-packet read
 * with sequence numbers, stale responses, timeouts, retries, error
 * notifications and unsolicited frames, a stopped clock, a flooding host,
 * and the route-string helpers.
 *
 * The expected values are worked from the specification (Table A-1;
 * Tables 6-2, 12-6 to 12-8, 12-19 to 12-26) and from ICM-PROTOCOL.md 2.1's
 * captured register values, against the model in tbt_fake.h, not from the
 * code under test.
 */

#include <stdio.h>
#include <string.h>
#include "../src/cm/tbt_ring.h"
#include "tbt_fake.h"
#include "test_harness.h"

static TBT_RING g_ring;

static LONG startRing(ULONG flags)
{
    memset(&g_ring, 0, sizeof(g_ring));
    return TbtRingStart(&g_ring, &g_fakeHost, fakeMem(), FAKE_PHYS,
                        FAKE_MEM_BYTES, TBT_RX_PDF_ALL_ONES, flags);
}

/* ------------------------------------------------------------------ */

static void test_crc_vectors(void)
{
    /* Spec Appendix A.2, Table A-1: the CRC covers the Route String and
     * the payload DWs, each DW most significant byte first. */
    static const UCHAR ex1[] = { 0, 0, 0, 0, 0, 0, 0, 0,
                                 0x04, 0x00, 0x20, 0x00 };
    static const UCHAR ex2[] = { 0x80, 0, 0, 0, 0, 0, 0, 0,
                                 0x04, 0x28, 0x20, 0x00,
                                 0x15, 0x78, 0x80, 0x86 };

    CHECK_EQ(TbtCrc32c(ex1, 12), 0x2FC967ACUL, "A.2 example 1, read request");
    CHECK_EQ(TbtCrc32c(ex2, 16), 0x5A9B0181UL, "A.2 example 2, read response");
    CHECK_EQ(fakeCrc(ex1, 12), 0x2FC967ACUL, "the model's own CRC agrees");
    CHECK_EQ(TbtCrc32c(ex1, 0), 0x00000000UL, "empty input: init xor out");
}

static void test_start(void)
{
    LONG rc;
    ULONG i;
    UCHAR *m;

    fakeReset();
    g_fake.Imr[0] = 0x00001001UL;   /* the Linux client's value, ICM 2.1 */
    g_fake.Imr[1] = 0x00000001UL;   /* a vacancy cause left enabled */
    rc = startRing(0);
    CHECK_EQ(rc, TBT_OK, "start");
    CHECK_EQ(g_ring.Paths, 12, "12 paths from 0x0000000c");
    CHECK_EQ(g_fake.TxBaseLo, FAKE_PHYS + TBT_RING_TX_DESC_OFF, "TX base");
    CHECK_EQ(g_fake.TxBaseHi, 0, "TX base high always 0");
    CHECK_EQ(g_fake.TxSize, 0x0000000AUL, "TX size as the capture, 10");
    CHECK_EQ(g_fake.TxCtrl, 0xC0000000UL, "TX valid and raw, as the capture");
    CHECK_EQ(g_fake.RxBaseLo, FAKE_PHYS + TBT_RING_RX_DESC_OFF, "RX base");
    CHECK_EQ(g_fake.RxSizeReg, 0x0100000AUL, "RX size and 256-byte buffers, as the capture");
    CHECK_EQ(g_fake.RxCtrl, 0xC0000000UL, "RX valid and raw");
    CHECK_EQ(g_fake.RxPdf, 0xFFFFFFFFUL, "PDF masks as the capture");
    CHECK_EQ(g_fake.RxCons, 9, "RX consumer one behind: nine descriptors given");
    CHECK_EQ(g_fake.Imr[0], 0, "ring interrupts masked (Intel: mask written)");
    CHECK_EQ(g_fake.Imr[1], 0, "the vacancy causes too");
    CHECK_EQ(g_fake.ImrWrites, 2, "ceil(3N/32) = 2 mask DWs for 12 paths");
    CHECK_EQ(g_fake.ImcWrites, 0, "no Mask Clear on the Intel contract");
    CHECK_EQ(g_fake.HiResetWrites, 0, "no host interface reset unless asked");
    m = fakeMem();
    for (i = 0; i < TBT_RING_ENTRIES; i++) {
        CHECK_EQ(fakeGetLe(m + TBT_RING_RX_DESC_OFF + 16 * i),
                 FAKE_PHYS + TBT_RING_RX_BUF_OFF + 256 * i, "RX buffer address");
        CHECK_EQ(fakeGetLe(m + TBT_RING_RX_DESC_OFF + 16 * i + 8),
                 0x00400000UL, "RX descriptor: RS set, DD clear (Table 12-7)");
    }
    CHECK_EQ(TBT_RING_DMA_BYTES, 5440, "the block's size, record 21 section 4.2");

    fakeReset();
    rc = startRing(TBT_RING_START_MASK_IMC | TBT_RING_START_RESET_HI);
    CHECK_EQ(rc, TBT_OK, "start, generic contract and reset");
    CHECK_EQ(g_fake.ImcWrites, 2, "Mask Clear on the generic contract, both DWs");
    CHECK_EQ(g_fake.ImcLast, 0xFFFFFFFFUL, "every cause cleared");
    CHECK_EQ(g_fake.ImrWrites, 0, "mask register untouched");
    CHECK_EQ(g_fake.HiResetWrites, 1, "reset when asked");
}

static void test_start_refusals(void)
{
    fakeReset();
    g_fake.Caps = 0xFFFFFFFFUL;
    CHECK_EQ(startRing(0), TBT_E_HW, "BAR0 not decoding");
    CHECK_EQ(g_fake.Writes, 0, "nothing written");

    fakeReset();
    g_fake.Caps = 0x00000000UL;
    CHECK_EQ(startRing(0), TBT_E_HW, "no paths");
    fakeReset();
    g_fake.Caps = 22;
    CHECK_EQ(startRing(0), TBT_E_HW, "more than 21 paths (Table 12-11)");

    fakeReset();
    memset(&g_ring, 0, sizeof(g_ring));
    CHECK_EQ(TbtRingStart(&g_ring, &g_fakeHost, fakeMem(), FAKE_PHYS + 8,
                          FAKE_MEM_BYTES, 0xFFFFFFFFUL, 0),
             TBT_E_MEMORY, "ring base not 16-byte aligned");
    CHECK_EQ(TbtRingStart(&g_ring, &g_fakeHost, fakeMem(), FAKE_PHYS,
                          TBT_RING_DMA_BYTES - 1, 0xFFFFFFFFUL, 0),
             TBT_E_MEMORY, "block too small");
    CHECK_EQ(TbtRingStart(&g_ring, &g_fakeHost, fakeMem(), 0xFFFFF000UL,
                          FAKE_MEM_BYTES, 0xFFFFFFFFUL, 0),
             TBT_E_MEMORY, "block crossing 4 GB");
    CHECK_EQ(g_fake.Writes, 0, "a refused block writes nothing");
}

static void test_send(void)
{
    ULONG dw[3];
    UCHAR *m;
    ULONG i;
    LONG rc;

    fakeReset();
    CHECK_EQ(startRing(0), TBT_OK, "start");
    /* Table A-1 example 1's packet: route 0, read 1 DW of router DW 0. */
    dw[0] = 0;
    dw[1] = 0;
    dw[2] = 0x04002000UL;
    rc = TbtRingSend(&g_ring, TBT_PDF_READ, dw, 3);
    CHECK_EQ(rc, TBT_OK, "send");
    m = fakeMem();
    CHECK_EQ(fakeGetLe(m + 0), FAKE_PHYS + TBT_RING_TX_BUF_OFF, "TX address");
    CHECK_EQ(fakeGetLe(m + 8) & 0x00DFFFFFUL, 0x00411010UL,
             "DW2: length 16, EOF and SOF PDF 1, RS (Table 12-6)");
    CHECK(fakeGetLe(m + 8) & 0x00200000UL, "DD written back");
    CHECK_EQ(fakeGetBe(m + TBT_RING_TX_BUF_OFF + 12), 0x2FC967ACUL,
             "trailer = Table A-1's CRC DW, stored big-endian");
    CHECK_EQ(g_fake.TxCount, 1, "the model fetched one");
    CHECK_EQ(g_fake.TxBadCrc, 0, "trailer good");
    CHECK_EQ(g_fake.TxLogPdf[0], 1, "PDF");
    CHECK_EQ(g_fake.TxLogDw[0][2], 0x04002000UL, "payload");

    /* Wrap: more than ten sends round the ring. */
    for (i = 0; i < 25; i++) {
        dw[2] = i;
        CHECK_EQ(TbtRingSend(&g_ring, TBT_PDF_ICM_CMD, dw, 3), TBT_OK, "send across the wrap");
    }
    CHECK_EQ(g_fake.TxCount, 26, "every one fetched");
    CHECK_EQ(g_fake.TxLogDw[25][2], 24, "the last in order");
    CHECK_EQ(g_ring.TxNext, 26 % 10, "producer wrapped");

    /* Refusals. */
    CHECK_EQ(TbtRingSend(&g_ring, 0xF, dw, 3), TBT_E_PARAM, "PDF Fh refused");
    CHECK_EQ(TbtRingSend(&g_ring, 1, dw, 0), TBT_E_PARAM, "empty frame refused");
}

static void test_send_limits(void)
{
    ULONG big[64];

    fakeReset();
    CHECK_EQ(startRing(0), TBT_OK, "start");
    memset(big, 0, sizeof(big));
    CHECK_EQ(TbtRingSend(&g_ring, 1, big, 63), TBT_OK, "63 DWs and the trailer fill 256 bytes");
    CHECK_EQ(TbtRingSend(&g_ring, 1, big, 64), TBT_E_PARAM, "64 DWs and a trailer do not");
}

static void test_tx_exhaustion(void)
{
    ULONG dw[3] = { 0, 0, 0 };
    ULONG i;
    LONG rc;
    ULONG t0;

    fakeReset();
    CHECK_EQ(startRing(0), TBT_OK, "start");
    g_fake.TxConsume = 0;
    for (i = 0; i < 9; i++) {
        t0 = g_fake.NowUs;
        rc = TbtRingSend(&g_ring, 1, dw, 3);
        CHECK_EQ(rc, TBT_E_HW, "unconsumed: the send gives up");
        CHECK(g_fake.NowUs - t0 >= TBT_TX_CONSUME_MS * 1000UL, "after its deadline");
        CHECK(g_fake.NowUs - t0 < TBT_TX_CONSUME_MS * 1000UL + 5000UL, "and not much later");
    }
    rc = TbtRingSend(&g_ring, 1, dw, 3);
    CHECK_EQ(rc, TBT_E_RING_FULL, "the tenth would put PI on CI (Table 12-19)");
    CHECK_EQ(g_ring.Counters.TxFull, 1, "counted");
    CHECK_EQ(g_ring.Counters.TxStuck, 9, "the nine stuck counted");
    g_fake.TxConsume = 1;
    rc = TbtRingSend(&g_ring, 1, dw, 3);
    CHECK_EQ(rc, TBT_OK, "once the host interface moves, the ring works again");
    CHECK_EQ(g_fake.TxCount, 10, "all ten fetched");
}

static void test_receive_wrap(void)
{
    ULONG dw[4];
    ULONG i;
    TBT_FRAME f;

    fakeReset();
    CHECK_EQ(startRing(0), TBT_OK, "start");
    CHECK(!TbtRingReceive(&g_ring, &f), "nothing pending");
    for (i = 0; i < 25; i++) {
        dw[0] = 0x01000003UL;
        dw[1] = i;
        fakeQueue(TBT_PDF_ICM_EVENT, dw, 2, 0, 0);
        fakePump();
        CHECK(TbtRingReceive(&g_ring, &f), "frame taken");
        CHECK_EQ(f.Pdf, TBT_PDF_ICM_EVENT, "PDF from EOF PDF");
        CHECK_EQ(f.Words, 2, "trailer removed");
        CHECK_EQ(f.Dw[1], i, "in order");
        CHECK_EQ(f.Flags, 0, "clean");
        CHECK_EQ(g_fake.RxCons, i % 10, "consumer names the descriptor just taken");
    }
    CHECK_EQ(g_fake.RxDropped, 0, "nothing dropped");
    CHECK_EQ(g_ring.RxNext, 25 % 10, "wrapped");
}

static void test_rx_exhaustion(void)
{
    ULONG dw[2];
    ULONG i;
    TBT_FRAME f;
    ULONG n;

    fakeReset();
    CHECK_EQ(startRing(0), TBT_OK, "start");
    for (i = 0; i < 12; i++) {
        dw[0] = 0x0100000AUL;
        dw[1] = i;
        fakeQueue(TBT_PDF_ICM_EVENT, dw, 2, 0, 0);
    }
    fakePump();
    CHECK_EQ(g_fake.RxPosted, 9, "nine descriptors were the host interface's");
    CHECK_EQ(g_fake.RxDropped, 3, "the rest discarded (Spec 12.4.2.2)");
    n = 0;
    while (TbtRingReceive(&g_ring, &f)) {
        CHECK_EQ(f.Dw[1], n, "the nine in order");
        n++;
    }
    CHECK_EQ(n, 9, "nine taken");
    dw[1] = 99;
    fakeQueue(TBT_PDF_ICM_EVENT, dw, 2, 0, 0);
    fakePump();
    CHECK(TbtRingReceive(&g_ring, &f), "the ring takes frames again");
    CHECK_EQ(f.Dw[1], 99, "the next one");
}

static void test_overflow_and_malformed(void)
{
    ULONG dw[70];
    TBT_FRAME f;
    ULONG i;

    fakeReset();
    CHECK_EQ(startRing(0), TBT_OK, "start");
    for (i = 0; i < 70; i++) {
        dw[i] = i;
    }
    /* 64 DWs and a trailer: 260 bytes into a 256-byte buffer. */
    fakeQueue(TBT_PDF_ICM_EVENT, dw, 64, 0, 0);
    dw[0] = 0x01000004UL;
    dw[1] = 0x00110000UL;
    fakeQueue(TBT_PDF_ICM_EVENT, dw, 2, 0, 0);
    fakePump();
    CHECK(TbtRingReceive(&g_ring, &f), "the oversized frame is taken back");
    CHECK(f.Flags & TBT_FRAME_OVERFLOW, "flagged overflow");
    CHECK_EQ(f.Words, 0, "and not delivered");
    CHECK_EQ(g_ring.Counters.Overflows, 1, "counted");
    CHECK(TbtRingReceive(&g_ring, &f), "the next frame");
    CHECK_EQ(f.Words, 2, "intact");
    CHECK_EQ(f.Dw[1], 0x00110000UL, "its payload");

    /* BO set on a frame that fits. */
    fakeQueue(TBT_PDF_ICM_EVENT, dw, 2, 0, FAKE_FLAG_BO);
    fakePump();
    CHECK(TbtRingReceive(&g_ring, &f), "taken");
    CHECK(f.Flags & TBT_FRAME_OVERFLOW, "BO honoured");

    /* Lengths that are not whole DWs, or shorter than a DW and trailer. */
    fakeQueueRaw(TBT_PDF_ICM_EVENT, 6, 0);
    fakeQueueRaw(TBT_PDF_ICM_EVENT, 10, 0);
    fakeQueueRaw(TBT_PDF_ICM_EVENT, 4, 0);
    fakePump();
    for (i = 0; i < 3; i++) {
        CHECK(TbtRingReceive(&g_ring, &f), "malformed taken back");
        CHECK(f.Flags & TBT_FRAME_MALFORMED, "flagged malformed");
        CHECK_EQ(f.Words, 0, "not delivered");
    }
    CHECK_EQ(g_ring.Counters.Malformed, 3, "counted");
}

static void test_trailer(void)
{
    ULONG dw[4] = { 0, 0, 0x04002000UL, 0x15788086UL };
    TBT_FRAME f;

    fakeReset();
    CHECK_EQ(startRing(0), TBT_OK, "start");
    fakeQueue(TBT_PDF_READ, dw, 4, 0, FAKE_FLAG_BAD_CRC);
    fakeQueue(TBT_PDF_ICM_EVENT, dw, 4, 0, FAKE_FLAG_BAD_CRC);
    fakePump();
    CHECK(TbtRingReceive(&g_ring, &f), "control packet taken");
    CHECK(f.Flags & TBT_FRAME_CRC_BAD, "bad trailer flagged");
    CHECK_EQ(f.Words, 0, "a control packet with a bad trailer is dropped");
    CHECK(TbtRingReceive(&g_ring, &f), "ICM frame taken");
    CHECK(f.Flags & TBT_FRAME_CRC_BAD, "flagged");
    CHECK_EQ(f.Words, 4, "an ICM frame is delivered regardless (record 21 section 5.2)");
    CHECK_EQ(g_ring.Counters.CrcErrors, 2, "both counted");
}

/* ------------------------------------------------------------------ */
/* Control reads                                                      */
/* ------------------------------------------------------------------ */

static int g_answerMode;    /* 0 answer, 1 never, 2 stale then answer, 3 notify,
                               4 answer the second attempt, 5 unsolicited first */
static ULONG g_answerDelayUs;
static ULONG g_reads;
static ULONG g_unsolicited;

static void readResponder(FAKE *fk, unsigned long pdf, const unsigned long *dw,
                          unsigned long words)
{
    unsigned long r[5];

    (void)fk;
    if (pdf != TBT_PDF_READ || words < 3) {
        return;
    }
    g_reads++;
    r[0] = dw[0] | 0x80000000UL;    /* CM bit on a reply (Table 6-1) */
    r[1] = dw[1];
    r[2] = (dw[2] & ~(0x3FUL << 19)) | (3UL << 19);  /* arrival adapter */
    r[3] = 0x15C08086UL;            /* ICM 5.1 [obs] T:15, the router header */
    r[4] = 0x01014305UL;
    switch (g_answerMode) {
    case 0:
        fakeQueue(TBT_PDF_READ, r, 3 + ((dw[2] >> 13) & 0x3F), g_answerDelayUs, 0);
        break;
    case 1:
        break;
    case 2:
        r[2] ^= 1UL << 27;          /* another SN: an earlier attempt's */
        fakeQueue(TBT_PDF_READ, r, 4, 0, 0);
        r[2] ^= 1UL << 27;
        fakeQueue(TBT_PDF_READ, r, 4, 10, 0);
        break;
    case 3:
        r[2] = 2;                   /* error code 2, invalid config space */
        fakeQueue(TBT_PDF_NOTIFY, r, 3, 0, 0);
        break;
    case 4:
        if (g_reads >= 2) {
            fakeQueue(TBT_PDF_READ, r, 4, 0, 0);
        }
        break;
    case 5:
        {
            unsigned long ev[2] = { 0x0100b103UL, 0 };
            fakeQueue(TBT_PDF_ICM_EVENT, ev, 2, 0, 0);
            fakeQueue(TBT_PDF_READ, r, 4, 50, 0);
        }
        break;
    }
}

static void onUnsolicited(PVOID ctx, const TBT_FRAME *f)
{
    (void)ctx;
    if (f->Pdf == TBT_PDF_ICM_EVENT) {
        g_unsolicited++;
    }
}

static void readSetup(int mode)
{
    fakeReset();
    g_fake.OnTx = readResponder;
    g_answerMode = mode;
    g_answerDelayUs = 0;
    g_reads = 0;
    g_unsolicited = 0;
    startRing(0);
    TbtRingSetUnsolicited(&g_ring, onUnsolicited, NULL);
}

static void test_ctl_read(void)
{
    TBT_ROUTE root = { 0, 0 };
    TBT_ROUTE dev = { 0, 1 };
    ULONG out[2];
    ULONG t0;
    LONG rc;

    readSetup(0);
    g_answerDelayUs = 156;  /* the handoff's median round trip */
    rc = TbtRingCtlRead(&g_ring, &root, TBT_CS_ROUTER, 5, 0, 2, out, 100, 0);
    CHECK_EQ(rc, TBT_OK, "router header read");
    CHECK_EQ(out[0], 0x15C08086UL, "vendor and device");
    CHECK_EQ(out[1], 0x01014305UL, "DW 1");
    /* Table 6-2: address 0, size 2, adapter forced 0 for the router space,
     * CS 2, SN 0. */
    CHECK_EQ(g_fake.TxLogDw[0][2], 0x04004000UL, "request DW 3");
    CHECK_EQ(g_fake.TxLogWords[0], 3, "route, route, DW 3");
    rc = TbtRingCtlRead(&g_ring, &dev, TBT_CS_ADAPTER, 3, 0x3F, 1, out, 100, 0);
    CHECK_EQ(rc, TBT_OK, "adapter read on route 1");
    CHECK_EQ(g_fake.TxLogDw[1][1], 1, "route low");
    CHECK_EQ(g_fake.TxLogDw[1][2],
             0x3FUL | (1UL << 13) | (3UL << 19) | (1UL << 25) | (1UL << 27),
             "address, size, adapter, CS 1, SN 1");
    TbtRingCtlRead(&g_ring, &dev, TBT_CS_ADAPTER, 3, 0x3F, 1, out, 100, 0);
    TbtRingCtlRead(&g_ring, &dev, TBT_CS_ADAPTER, 3, 0x3F, 1, out, 100, 0);
    TbtRingCtlRead(&g_ring, &dev, TBT_CS_ADAPTER, 3, 0x3F, 1, out, 100, 0);
    CHECK_EQ((g_fake.TxLogDw[4][2] >> 27) & 3, 0, "SN wraps after 3");

    CHECK_EQ(TbtRingCtlRead(&g_ring, &root, TBT_CS_ROUTER, 0, 0, 0, out, 100, 0),
             TBT_E_PARAM, "size 0 refused");
    CHECK_EQ(TbtRingCtlRead(&g_ring, &root, TBT_CS_ROUTER, 0, 0, 61, out, 100, 0),
             TBT_E_PARAM, "size 61 refused (Table 6-2: up to 60)");

    /* A stale answer (wrong SN) first. */
    readSetup(2);
    rc = TbtRingCtlRead(&g_ring, &root, TBT_CS_ROUTER, 0, 0, 1, out, 100, 0);
    CHECK_EQ(rc, TBT_OK, "the right answer is taken");
    CHECK_EQ(g_ring.Counters.CtlStale, 1, "the stale one dropped");

    /* Never answered: every attempt waits its timeout. */
    readSetup(1);
    t0 = g_fake.NowUs;
    rc = TbtRingCtlRead(&g_ring, &root, TBT_CS_ROUTER, 0, 0, 1, out, 100, 3);
    CHECK_EQ(rc, TBT_E_TIMEOUT, "timeout after the retries");
    CHECK_EQ(g_reads, 4, "one try and three retries");
    CHECK_EQ(g_ring.Counters.CtlRetries, 3, "retries counted");
    CHECK(g_fake.NowUs - t0 >= 400000UL, "four 100 ms waits");
    CHECK(g_fake.NowUs - t0 < 440000UL, "and not much more");

    /* Answered on the second attempt. */
    readSetup(4);
    rc = TbtRingCtlRead(&g_ring, &root, TBT_CS_ROUTER, 0, 0, 1, out, 100, 3);
    CHECK_EQ(rc, TBT_OK, "the retry is answered");
    CHECK_EQ(g_reads, 2, "two sends");

    /* An error notification ends it at once. */
    readSetup(3);
    rc = TbtRingCtlRead(&g_ring, &root, TBT_CS_ROUTER, 0, 0x1B3, 1, out, 100, 3);
    CHECK_EQ(rc, TBT_E_NOTIFY, "error notification");
    CHECK_EQ(g_reads, 1, "not retried");

    /* An ICM event inside the wait goes to the unsolicited receiver. */
    readSetup(5);
    rc = TbtRingCtlRead(&g_ring, &root, TBT_CS_ROUTER, 0, 0, 1, out, 100, 0);
    CHECK_EQ(rc, TBT_OK, "read completes");
    CHECK_EQ(g_unsolicited, 1, "the event was handed over");
}

static void test_stopped_clock_and_flood(void)
{
    TBT_ROUTE root = { 0, 0 };
    ULONG out[1];
    LONG rc;
    ULONG i;
    ULONG ev[2] = { 0x01000099UL, 0 };

    /* A clock that never moves still ends the wait: the waits asked for
     * add up to the limit. */
    readSetup(1);
    g_fake.ClockStopped = 1;
    rc = TbtRingCtlRead(&g_ring, &root, TBT_CS_ROUTER, 0, 0, 1, out, 100, 1);
    CHECK_EQ(rc, TBT_E_TIMEOUT, "stopped clock: still a timeout");
    CHECK_EQ(g_reads, 2, "both attempts");

    /* A host interface that never stops sending unsolicited frames, with a
     * stopped clock: no wait is ever asked for, so only the frame budget
     * can end the read. */
    readSetup(1);
    g_fake.ClockStopped = 1;
    g_fake.FloodPdf = TBT_PDF_ICM_EVENT;
    (void)i;
    (void)ev;
    rc = TbtRingCtlRead(&g_ring, &root, TBT_CS_ROUTER, 0, 0, 1, out, 100, 0);
    CHECK_EQ(rc, TBT_E_TIMEOUT, "flood: the wait still ends");
    CHECK(g_ring.Counters.Rx > TBT_WAIT_FRAME_BUDGET, "ended by the frame budget");
    CHECK_EQ(g_fake.NowUs, 0, "the clock never moved");
    g_fake.FloodPdf = 0;
}

static void test_stop(void)
{
    ULONG dw[3] = { 0, 0, 0 };
    TBT_FRAME f;

    fakeReset();
    CHECK_EQ(startRing(0), TBT_OK, "start");
    TbtRingStop(&g_ring);
    CHECK_EQ(g_fake.TxCtrl, 0, "TX control to its default");
    CHECK_EQ(g_fake.RxCtrl, 0, "RX control to its default");
    CHECK_EQ(g_fake.RxCons, 0, "RX consumer to its default (Spec 12.6.3.3.3)");
    CHECK_EQ(TbtRingSend(&g_ring, 1, dw, 3), TBT_E_STOPPED, "no send when stopped");
    CHECK(!TbtRingReceive(&g_ring, &f), "no receive when stopped");
    g_fake.Writes = 0;
    TbtRingStop(&g_ring);
    CHECK_EQ(g_fake.Writes, 0, "a second stop writes nothing");
    CHECK_EQ(startRing(0), TBT_OK, "restart on the same block");
    CHECK_EQ(TbtRingSend(&g_ring, 1, dw, 3), TBT_OK, "and it works");
}

static void test_routes(void)
{
    TBT_ROUTE r;
    TBT_ROUTE s;

    r.Hi = 0;
    r.Lo = 0;
    CHECK_EQ(TbtRouteDepth(&r), 0, "route 0 is the host router");
    r.Lo = 1;
    CHECK_EQ(TbtRouteDepth(&r), 1, "route 1, depth 1 (ICM 4.2 [obs])");
    CHECK_EQ(TbtRouteLevel(&r, 0), 1, "level 0 adapter 1");
    r.Lo = 0x03030301UL;
    r.Hi = 0x00000203UL;
    CHECK_EQ(TbtRouteDepth(&r), 6, "six levels");
    CHECK_EQ(TbtRouteLevel(&r, 4), 3, "level 4 from the high DW");
    CHECK_EQ(TbtRouteLevel(&r, 5), 2, "level 5");
    r.Lo = 0x0000C101UL;
    CHECK_EQ(TbtRouteLevel(&r, 1), 1, "only bits 5:0 are the adapter");
    s.Hi = 0x80000000UL;
    s.Lo = 1;
    r.Hi = 0;
    r.Lo = 1;
    CHECK(TbtRouteEqual(&r, &s), "the CM bit is not part of the TopologyID");
    s.Lo = 3;
    CHECK(!TbtRouteEqual(&r, &s), "different routes");
}

int main(void)
{
    test_crc_vectors();
    test_start();
    test_start_refusals();
    test_send();
    test_send_limits();
    test_tx_exhaustion();
    test_receive_wrap();
    test_rx_exhaustion();
    test_overflow_and_malformed();
    test_trailer();
    test_ctl_read();
    test_stopped_clock_and_flood();
    test_stop();
    test_routes();
    printf("test_tbt_ring: %d checks, %d failures\n", checks, failures);
    return failures;
}
