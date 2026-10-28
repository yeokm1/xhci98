/*
 * test_tbt_ring.c - host vectors for the host interface's ring 0
 * (src\cm\tbt_ring.c; roadmap-usb4.md task 39-A.2, design record 21
 * section 10.1): the USB4 Specification 2.0 Appendix A.2 CRC-32C vectors,
 * the start's register programming, transmit and receive with wrap, the
 * vacant-descriptor rule on both rings (exhaustion), a frame larger than
 * its buffer, malformed lengths, trailer checks, the control-packet read
 * with sequence numbers, stale responses, timeouts, retries, error
 * notifications and unsolicited frames, a stopped clock, a flooding host,
 * and the route-string helpers; and the USB4 transport of task 40-A.1
 * (design record 22 sections 4.4, 5.1 to 5.3, 7.4): the 16-entry geometry
 * and the ring 0 takeover, write requests and their echo, sequence numbers
 * across retries, record 22's 100 ms by 4 attempts, the notification
 * classes (a link error during a request does not fail it, ERR_CONN does),
 * plain, enhanced, held and withheld acknowledgements, hot plug
 * acknowledgement with PG, duplicate suppression and overflow, maximum and
 * minimum waits, the clock's wrap, the safe point and route extension.
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
    CHECK_EQ(g_ring.Counters.Rx, TBT_WAIT_FRAME_BUDGET,
             "ended by the frame budget, the next frame left on the ring");
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

/* ------------------------------------------------------------------ */
/* The USB4 transport (task 40-A.1; record 22 sections 4.4, 5.1-5.3)  */
/* ------------------------------------------------------------------ */

static TBT_CTL g_ctl;
static ULONG g_u4Mode;      /* 0 answer, 1 a link error first, 2 ERR_CONN,
                               3 never, 4 hot plug first, 5 answer write second */
static ULONG g_u4Writes;
static ULONG g_u4Reads;
static ULONG g_u4LastWriteDw[8];
static ULONG g_ackForm;

static void u4Responder(FAKE *fk, unsigned long pdf, const unsigned long *dw,
                        unsigned long words)
{
    unsigned long r[8];
    unsigned long n;
    unsigned long k;

    (void)fk;
    if ((pdf != TBT_PDF_READ && pdf != TBT_PDF_WRITE) || words < 3) {
        return;
    }
    r[0] = dw[0] | 0x80000000UL;
    r[1] = dw[1];
    r[2] = dw[2];
    n = (dw[2] >> 13) & 0x3FUL;
    if (pdf == TBT_PDF_WRITE) {
        g_u4Writes++;
        for (k = 0; k < 8 && k + 3 < words; k++) {
            g_u4LastWriteDw[k] = dw[3 + k];
        }
    } else {
        g_u4Reads++;
    }
    if (g_u4Mode == 1) {
        unsigned long e[3];
        e[0] = 0x80000000UL;
        e[1] = 0;
        e[2] = TBT_EV_ERR_LINK | (3UL << 8);
        fakeQueue(TBT_PDF_NOTIFY, e, 3, 0, 0);
    } else if (g_u4Mode == 2) {
        unsigned long e[3];
        e[0] = 0x80000000UL;
        e[1] = 0;
        e[2] = TBT_EV_ERR_CONN | (1UL << 8);
        fakeQueue(TBT_PDF_NOTIFY, e, 3, 0, 0);
        return;
    } else if (g_u4Mode == 3) {
        return;
    } else if (g_u4Mode == 4) {
        unsigned long e[3];
        e[0] = 0x80000000UL;
        e[1] = 0;
        e[2] = 1UL;         /* adapter 1, plug */
        fakeQueue(TBT_PDF_HOTPLUG, e, 3, 0, 0);
        fakeQueue(TBT_PDF_HOTPLUG, e, 3, 0, 0);     /* retransmitted */
    } else if (g_u4Mode == 5 && pdf == TBT_PDF_WRITE && g_u4Writes < 2) {
        return;
    }
    if (pdf == TBT_PDF_READ) {
        for (k = 0; k < n && k < 5; k++) {
            r[3 + k] = 0x11110000UL + k;
        }
        fakeQueue(TBT_PDF_READ, r, 3 + n, 50, 0);
    } else {
        fakeQueue(TBT_PDF_WRITE, r, 3, 50, 0);
    }
}

static ULONG ackFormCb(PVOID ctx, const TBT_ROUTE *route)
{
    (void)ctx;
    (void)route;
    return g_ackForm;
}

static ULONG g_safeCalls;
static ULONG g_safeNested;

static void safeCb(PVOID ctx)
{
    g_safeCalls++;
    if (g_safeCalls == 1) {
        TbtCtlSafePoint((PTBT_CTL)ctx);     /* must not nest */
    } else {
        g_safeNested++;
    }
}

static LONG u4Setup(ULONG mode)
{
    LONG rc;

    fakeReset();
    g_fake.OnTx = u4Responder;
    g_u4Mode = mode;
    g_u4Writes = 0;
    g_u4Reads = 0;
    g_ackForm = TBT_ACK_PLAIN;
    memset(&g_ring, 0, sizeof(g_ring));
    rc = TbtRingStartEx(&g_ring, &g_fakeHost, fakeMem(), FAKE_PHYS,
                        FAKE_MEM_BYTES, TBT_RX_PDF_ALL_ONES,
                        TBT_RING_START_MASK_IMC | TBT_RING_START_TAKEOVER,
                        TBT_RING_U4_ENTRIES);
    TbtCtlInit(&g_ctl, &g_ring);
    TbtCtlSetAckForm(&g_ctl, ackFormCb, NULL);
    return rc;
}

/* The last frame the host interface took of `pdf`, newest first. */
static long findTx(unsigned long pdf)
{
    long i;

    for (i = (long)g_fake.TxCount - 1; i >= 0; i--) {
        if (i < FAKE_TXLOG && g_fake.TxLogPdf[i] == pdf) {
            return i;
        }
    }
    return -1;
}

static ULONG countTx(unsigned long pdf)
{
    ULONG i;
    ULONG n;

    n = 0;
    for (i = 0; i < g_fake.TxCount && i < FAKE_TXLOG; i++) {
        if (g_fake.TxLogPdf[i] == pdf) {
            n++;
        }
    }
    return n;
}

static void test_u4_geometry_and_takeover(void)
{
    LONG rc;

    CHECK_EQ(TBT_RING_U4_DMA_BYTES, 8704UL, "16 each way: 8704 bytes, about 9 KiB");
    CHECK_EQ(TBT_RING_DMA_BYTES_N(10), TBT_RING_DMA_BYTES, "the same formula as ten");

    fakeReset();
    g_fake.TxCtrl = 0xC0000000UL;       /* a predecessor's ring 0, still valid */
    g_fake.RxCtrl = 0xC0000000UL;
    g_fake.TxBaseLo = 0x00ABC000UL;
    g_fake.Ring1Ctrl = 0x80000000UL;    /* someone else's ring 1 */
    memset(&g_ring, 0, sizeof(g_ring));
    rc = TbtRingStartEx(&g_ring, &g_fakeHost, fakeMem(), FAKE_PHYS,
                        FAKE_MEM_BYTES, TBT_RX_PDF_SPEC,
                        TBT_RING_START_MASK_IMC | TBT_RING_START_TAKEOVER,
                        TBT_RING_U4_ENTRIES);
    CHECK_EQ(rc, TBT_OK, "start with the takeover");
    CHECK_EQ(g_ring.Counters.Ring0WasEnabled, 1, "ring 0 read enabled, counted");
    CHECK_EQ(g_ring.Counters.OtherRingsEnabled, 1, "ring 1 seen, counted");
    {
        /* The write order of record 22 section 4.4 steps 1 and 2: the
         * masks, then ring 0's control, base, size and index to 0, then
         * the settle, then the new ring; nothing written to ring 1. */
        ULONG k;
        ULONG lastMask = 0xFFFFFFFFUL;
        ULONG firstDisable = 0xFFFFFFFFUL;
        ULONG lastDisable = 0;
        ULONG firstProgram = 0xFFFFFFFFUL;
        ULONG ring1 = 0;
        for (k = 0; k < g_fake.Writes && k < FAKE_WRLOG; k++) {
            ULONG off = g_fake.WrLogOff[k];
            if (off == 0x38208UL || off == 0x3820CUL) {
                lastMask = k;
            }
            if ((off == 0x19800UL || off == 0x29800UL || off == 0x00000UL ||
                 off == 0x00004UL || off == 0x0000CUL || off == 0x00008UL ||
                 off == 0x08000UL || off == 0x08004UL || off == 0x0800CUL ||
                 off == 0x08008UL) && g_fake.WrLogVal[k] == 0 &&
                firstProgram == 0xFFFFFFFFUL) {
                if (firstDisable == 0xFFFFFFFFUL) {
                    firstDisable = k;
                }
                lastDisable = k;
            }
            if (off == 0x00000UL && g_fake.WrLogVal[k] != 0 &&
                firstProgram == 0xFFFFFFFFUL) {
                firstProgram = k;
            }
            if (off == 0x19820UL || off == 0x29820UL || off == 0x00010UL ||
                off == 0x08010UL) {
                ring1++;
            }
        }
        CHECK(lastMask != 0xFFFFFFFFUL && lastMask < firstDisable,
              "interrupts masked before the takeover");
        CHECK(firstDisable != 0xFFFFFFFFUL && g_fake.WrLogOff[firstDisable] == 0x19800UL,
              "TX ring 0's control cleared first");
        CHECK_EQ(lastDisable - firstDisable + 1, 10,
                 "both controls, bases, sizes and indexes written 0");
        CHECK(firstProgram != 0xFFFFFFFFUL && firstProgram > lastDisable, "then programmed");
        CHECK(firstProgram != 0xFFFFFFFFUL &&
              g_fake.WrLogAtUs[firstProgram] - g_fake.WrLogAtUs[lastDisable] >=
              TBT_RING_SETTLE_US, "at least 1 ms after the last disabling write");
        CHECK_EQ(ring1, 0, "no write to ring 1");
    }
    CHECK_EQ(g_fake.HiResetWrites, 0, "no Host Interface Reset");
    CHECK_EQ(g_fake.TxSize, 16, "TX ring of 16");
    CHECK_EQ(g_fake.RxSizeReg, 0x01000010UL, "RX ring of 16, 256-byte buffers");
    CHECK_EQ(g_fake.RxBaseLo, FAKE_PHYS + 16UL * 16UL, "RX ring after 16 TX descriptors");
    CHECK_EQ(g_fake.RxCons, 15, "fifteen RX descriptors given");
    CHECK_EQ(g_fake.ImcWrites, 2, "Mask Clear: two DWs for 12 paths' 36 causes");
    CHECK_EQ(TbtRingStartEx(&g_ring, &g_fakeHost, fakeMem(), FAKE_PHYS,
                            TBT_RING_U4_DMA_BYTES - 1, 0, 0, TBT_RING_U4_ENTRIES),
             TBT_E_MEMORY, "a 16-entry block too small");
    CHECK_EQ(TbtRingStartEx(&g_ring, &g_fakeHost, fakeMem(), FAKE_PHYS,
                            FAKE_MEM_BYTES, 0, 0, 17), TBT_E_PARAM, "17 entries refused");

    /* Without the flag the takeover never runs: the ICM client's start. */
    fakeReset();
    g_fake.Ring1Ctrl = 0x80000000UL;
    memset(&g_ring, 0, sizeof(g_ring));
    CHECK_EQ(startRing(0), TBT_OK, "ten-entry start");
    CHECK_EQ(g_ring.Counters.OtherRingsEnabled, 0, "no ring looked at");
    CHECK_EQ(g_fake.NowUs, 0, "and no settle");
}

static void test_u4_write_and_read(void)
{
    TBT_ROUTE dev = { 0, 1 };
    ULONG in[3] = { 0xAAAA0001UL, 0xBBBB0002UL, 0xCCCC0003UL };
    ULONG out[3];
    long i;
    LONG rc;

    CHECK_EQ(u4Setup(0), TBT_OK, "start");
    rc = TbtCtlWrite(&g_ctl, &dev, TBT_CS_ADAPTER, 3, 0x1C, 3, in);
    CHECK_EQ(rc, TBT_OK, "write answered");
    i = findTx(TBT_PDF_WRITE);
    CHECK(i >= 0, "a write request sent");
    if (i >= 0) {
        CHECK_EQ(g_fake.TxLogWords[i], 6, "header, route, DW 3 and three data DWs");
        /* Spec Table 6-4: address 1Ch, size 3, adapter 3, CS 1, SN. */
        CHECK_EQ(g_fake.TxLogDw[i][2] & 0x07FFFFFFUL,
                 0x1CUL | (3UL << 13) | (3UL << 19) | (1UL << 25), "DW 3 encoded");
        CHECK_EQ(g_fake.TxLogDw[i][3], 0xAAAA0001UL, "data in order");
        CHECK_EQ(g_fake.TxLogDw[i][5], 0xCCCC0003UL, "last data DW");
    }
    CHECK_EQ(g_ring.Counters.CtlWrites, 1, "counted as a write");
    rc = TbtCtlRead(&g_ctl, &dev, TBT_CS_ROUTER, 9, 0, 3, out);
    CHECK_EQ(rc, TBT_OK, "read answered");
    CHECK_EQ(out[2], 0x11110002UL, "three DWs read");
    i = findTx(TBT_PDF_READ);
    CHECK(i >= 0 && ((g_fake.TxLogDw[i][2] >> 19) & 0x3FUL) == 0,
          "adapter 0 in a router-space request");

    /* A write answered only on its second attempt: a fresh SN each time. */
    CHECK_EQ(u4Setup(5), TBT_OK, "start");
    rc = TbtCtlWrite1(&g_ctl, &dev, TBT_CS_ROUTER, 0, 5, 0x80000000UL);
    CHECK_EQ(rc, TBT_OK, "second attempt answered");
    CHECK_EQ(g_u4Writes, 2, "two attempts");
    CHECK(((g_fake.TxLogDw[0][2] ^ g_fake.TxLogDw[1][2]) >> 27 & 3UL) != 0,
          "different sequence numbers");
    CHECK_EQ(g_ctl.Counters.Retries, 1, "one retry counted");
    CHECK(g_fake.TxLogAtUs[1] - g_fake.TxLogAtUs[0] >= TBT_CTL_TIMEOUT_US,
          "after 100 ms");

    /* A request descriptor never consumed: 10 ms on the USB4 transport
     * (record 22 section 5.2), not the ICM client's 100 ms. */
    CHECK_EQ(u4Setup(0), TBT_OK, "start");
    g_fake.TxConsume = 0;
    {
        ULONG t0 = g_fake.NowUs;
        rc = TbtCtlRead1(&g_ctl, &dev, TBT_CS_ROUTER, 0, 0, out);
        CHECK_EQ(rc, TBT_E_HW, "a stuck ring fails the request");
        CHECK(g_fake.NowUs - t0 >= TBT_TX_CONSUME_U4_US &&
              g_fake.NowUs - t0 < TBT_TX_CONSUME_MS * 1000UL, "after 10 ms, not 100");
    }
    g_fake.TxConsume = 1;

    /* Never answered: 4 attempts of 100 ms. */
    CHECK_EQ(u4Setup(3), TBT_OK, "start");
    rc = TbtCtlRead1(&g_ctl, &dev, TBT_CS_ROUTER, 0, 0, out);
    CHECK_EQ(rc, TBT_E_TIMEOUT, "timeout");
    CHECK_EQ(g_u4Reads, TBT_CTL_ATTEMPTS, "four attempts");
    CHECK(g_fake.NowUs >= 4UL * TBT_CTL_TIMEOUT_US, "at least 400 ms");
    CHECK(g_fake.NowUs < 4UL * TBT_CTL_TIMEOUT_US + 50000UL, "and not much more");
    CHECK_EQ(g_ctl.Counters.Timeouts, 1, "one timed-out request");

    CHECK_EQ(TbtCtlWrite(&g_ctl, &dev, TBT_CS_ROUTER, 0, 0, 0, in), TBT_E_PARAM,
             "a write of nothing refused");
    CHECK_EQ(TbtRingCtlTransfer(&g_ring, TBT_PDF_NOTIFY, &dev, 0, 0, 0, 1, out,
                                1000, 1), TBT_E_PARAM, "only reads and writes");
}

static ULONG g_echoMode;

static void echoResponder(FAKE *fk, unsigned long pdf, const unsigned long *dw,
                          unsigned long words)
{
    unsigned long r[4];

    (void)fk;
    if (pdf != TBT_PDF_WRITE && pdf != TBT_PDF_READ) {
        return;
    }
    (void)words;
    r[0] = dw[0] | 0x80000000UL;
    r[1] = dw[1];
    /* another adapter in the echo, then (mode 1) the right one */
    r[2] = (dw[2] & ~(0x3FUL << 19)) | (((dw[2] >> 19) + 1UL) & 0x3FUL) << 19;
    r[3] = 0x12345678UL;
    fakeQueue(pdf, r, pdf == TBT_PDF_READ ? 4 : 3, 0, 0);
    if (g_echoMode == 1) {
        r[2] = dw[2];
        fakeQueue(pdf, r, pdf == TBT_PDF_READ ? 4 : 3, 10, 0);
    }
}

static void test_u4_echo(void)
{
    TBT_ROUTE dev = { 0, 1 };
    ULONG v[1];
    LONG rc;

    /* Adapter space: a response naming another adapter is not ours. */
    CHECK_EQ(u4Setup(0), TBT_OK, "start");
    g_fake.OnTx = echoResponder;
    g_echoMode = 0;
    rc = TbtCtlWrite1(&g_ctl, &dev, TBT_CS_ADAPTER, 3, 4, 0);
    CHECK_EQ(rc, TBT_E_TIMEOUT, "a write response for another adapter is refused");
    CHECK(g_ring.Counters.CtlStale >= 1, "counted as stale");
    g_echoMode = 1;
    rc = TbtCtlWrite1(&g_ctl, &dev, TBT_CS_ADAPTER, 3, 4, 0);
    CHECK_EQ(rc, TBT_OK, "the right echo is taken");
    g_echoMode = 0;
    rc = TbtCtlRead1(&g_ctl, &dev, TBT_CS_PATH, 3, 16, v);
    CHECK_EQ(rc, TBT_E_TIMEOUT, "a path-space read response for another adapter refused");
    /* Router space: the arrival adapter is accepted (Table 6-3, and
     * usb4-emu's write responses). */
    g_echoMode = 0;
    rc = TbtCtlWrite1(&g_ctl, &dev, TBT_CS_ROUTER, 0, 4, 0);
    CHECK_EQ(rc, TBT_OK, "router space: arrival adapter accepted");
}

static void test_u4_budget(void)
{
    TBT_ROUTE root = { 0, 0 };
    TBT_HP_EVENT e;
    ULONG out[1];
    LONG rc;

    /* A flood with a stopped clock: the wait ends by the budget, and the
     * frame that would pass it is left on the ring, not lost. */
    CHECK_EQ(u4Setup(3), TBT_OK, "start");
    g_fake.ClockStopped = 1;
    g_fake.FloodPdf = TBT_PDF_HOTPLUG;
    g_fake.FloodWords = 3;
    g_fake.FloodDw[0] = 0x80000000UL;
    g_fake.FloodDw[1] = 0;
    g_fake.FloodDw[2] = 1;
    g_ctl.Attempts = 1;
    rc = TbtCtlRead1(&g_ctl, &root, TBT_CS_ROUTER, 0, 0, out);
    CHECK_EQ(rc, TBT_E_TIMEOUT, "flood: the wait ends");
    CHECK_EQ(g_ring.Counters.Rx, TBT_WAIT_FRAME_BUDGET, "exactly the budget taken");
    CHECK_EQ(g_ring.Counters.Unsolicited, TBT_WAIT_FRAME_BUDGET,
             "every frame taken was handed over, none dropped");
    CHECK_EQ(g_ctl.Counters.HotPlugDuplicates, TBT_WAIT_FRAME_BUDGET - 1,
             "each acknowledged and the copies dropped");
    CHECK(TbtCtlNextHotPlug(&g_ctl, &e), "the event queued (once)");
    g_fake.FloodPdf = 0;
    g_fake.ClockStopped = 0;
}

static void test_u4_notifications(void)
{
    TBT_ROUTE dev = { 0, 1 };
    TBT_ROUTE root = { 0, 0 };
    TBT_NOTE n;
    ULONG out[1];
    long i;
    LONG rc;

    /* A link error inside the response wait does not fail the request; it
     * is acknowledged, plainly. */
    CHECK_EQ(u4Setup(1), TBT_OK, "start");
    rc = TbtCtlRead1(&g_ctl, &dev, TBT_CS_ROUTER, 0, 0, out);
    CHECK_EQ(rc, TBT_OK, "ERR_LINK is an event: the read completes");
    CHECK_EQ(g_ctl.Counters.Events[TBT_EV_ERR_LINK], 1, "counted by code");
    CHECK_EQ(g_ctl.Counters.AcksPlain, 1, "acknowledged");
    i = findTx(TBT_PDF_NOTIFY_ACK);
    CHECK(i >= 0 && g_fake.TxLogWords[i] == 2, "Spec 6.4.2.8: the route alone");
    CHECK(i >= 0 && (g_fake.TxLogDw[i][0] & 0x80000000UL) == 0,
          "to the router, CM bit clear");

    /* ERR_CONN fails it at once, recorded for the port check. */
    CHECK_EQ(u4Setup(2), TBT_OK, "start");
    rc = TbtCtlRead1(&g_ctl, &dev, TBT_CS_ROUTER, 0, 0, out);
    CHECK_EQ(rc, TBT_E_NOTIFY, "ERR_CONN fails the request");
    CHECK_EQ(g_ring.LastEvent, TBT_EV_ERR_CONN, "with its code");
    CHECK_EQ(g_ring.LastEventInfo, 1, "and its adapter");
    CHECK_EQ(g_u4Reads, 1, "not retried");
    CHECK_EQ(countTx(TBT_PDF_NOTIFY_ACK), 0, "a request error is not acknowledged");

    /* Enhanced acknowledgement where the router's capability says so. */
    CHECK_EQ(u4Setup(0), TBT_OK, "start");
    g_ackForm = TBT_ACK_ENHANCED;
    {
        unsigned long e[3];
        e[0] = 0x80000000UL;
        e[1] = 1;
        e[2] = TBT_EV_ERR_HEC | (1UL << 8) | TBT_NOTIFY_SQ;
        fakeQueue(TBT_PDF_NOTIFY, e, 3, 0, 0);
    }
    fakePump();
    TbtCtlService(&g_ctl);
    i = findTx(TBT_PDF_NOTIFY_ACK_ENH);
    CHECK(i >= 0, "PDF 8h sent");
    CHECK(i >= 0 && g_fake.TxLogWords[i] == 3, "route and the Sequence DW");
    CHECK(i >= 0 && g_fake.TxLogDw[i][2] == 1, "Sequence echoed in bit 0");
    CHECK(i >= 0 && g_fake.TxLogDw[i][1] == 1, "to the router that sent it");

    /* Held until the form is known, then sent; withheld when unknown. */
    CHECK_EQ(u4Setup(0), TBT_OK, "start");
    g_ackForm = TBT_ACK_PENDING;
    {
        unsigned long e[3];
        e[0] = 0x80000000UL;
        e[1] = 1;
        e[2] = TBT_EV_PCIE_WAKE;
        fakeQueue(TBT_PDF_NOTIFY, e, 3, 0, 0);
        e[1] = 3;
        fakeQueue(TBT_PDF_NOTIFY, e, 3, 0, 0);
    }
    fakePump();
    TbtCtlService(&g_ctl);
    CHECK_EQ(g_ctl.DeferCount, 2, "both held");
    CHECK_EQ(countTx(TBT_PDF_NOTIFY_ACK) + countTx(TBT_PDF_NOTIFY_ACK_ENH), 0,
             "nothing sent yet");
    g_ackForm = TBT_ACK_PLAIN;
    TbtCtlSettle(&g_ctl, &dev);
    CHECK_EQ(countTx(TBT_PDF_NOTIFY_ACK), 1, "route 1's sent once settled");
    CHECK_EQ(g_ctl.DeferCount, 1, "route 3's still held");
    g_ackForm = TBT_ACK_NONE;
    {
        TBT_ROUTE r3 = { 0, 3 };
        TbtCtlSettle(&g_ctl, &r3);
    }
    CHECK_EQ(g_ctl.DeferCount, 0, "unknown state: dropped");
    CHECK_EQ(g_ctl.Counters.AcksWithheld, 1, "counted as withheld");
    CHECK_EQ(countTx(TBT_PDF_NOTIFY_ACK), 1, "and never acknowledged");

    /* Forget drops what is held for a router that left. */
    CHECK_EQ(u4Setup(0), TBT_OK, "start");
    g_ackForm = TBT_ACK_PENDING;
    {
        unsigned long e[3];
        e[0] = 0x80000000UL;
        e[1] = 0x0301UL;            /* depth 2 below adapter 1 */
        e[2] = TBT_EV_DP_BW;
        fakeQueue(TBT_PDF_NOTIFY, e, 3, 0, 0);
    }
    fakePump();
    TbtCtlService(&g_ctl);
    TbtCtlForget(&g_ctl, &dev);
    CHECK_EQ(g_ctl.DeferCount, 0, "a route below the unplugged one forgotten");

    /* The silent codes, an unknown one and a stray request error. */
    CHECK_EQ(u4Setup(0), TBT_OK, "start");
    {
        unsigned long e[3];
        e[0] = 0x80000000UL;
        e[1] = 0;
        e[2] = TBT_EV_ROP_CMPLT;
        fakeQueue(TBT_PDF_NOTIFY, e, 3, 0, 0);
        e[2] = TBT_EV_LINK_RECOVERY;
        fakeQueue(TBT_PDF_NOTIFY, e, 3, 0, 0);
        e[2] = 50;
        fakeQueue(TBT_PDF_NOTIFY, e, 3, 0, 0);
        e[2] = TBT_EV_ERR_LOCK | (1UL << 8);
        fakeQueue(TBT_PDF_NOTIFY, e, 3, 0, 0);
        e[2] = TBT_EV_ERR_PLUG | (1UL << 8);
        fakeQueue(TBT_PDF_NOTIFY, e, 3, 0, 0);
    }
    fakePump();
    TbtCtlService(&g_ctl);
    CHECK_EQ(countTx(TBT_PDF_NOTIFY_ACK), 1, "only ERR_PLUG acknowledged (Guide 7.7.2)");
    CHECK(TbtCtlNextNote(&g_ctl, &n), "a note queued");
    CHECK_EQ(n.Code, TBT_EV_ERR_LOCK, "ERR_LOCK first, for the port check");
    CHECK(TbtCtlNextNote(&g_ctl, &n), "another");
    CHECK_EQ(n.Code, TBT_EV_ERR_PLUG, "ERR_PLUG");
    CHECK(!TbtCtlNextNote(&g_ctl, &n), "no more");
    CHECK_EQ(g_ctl.Counters.EventsOther, 1, "code 50 counted apart");
    (void)root;

    CHECK_EQ(TbtCtlClassify(TBT_EV_ERR_ADDR), TBT_NOTE_REQUEST, "ERR_ADDR");
    CHECK_EQ(TbtCtlClassify(TBT_EV_ERR_LEN), TBT_NOTE_REQUEST, "ERR_LEN");
    CHECK_EQ(TbtCtlClassify(TBT_EV_ERR_NUA), TBT_NOTE_REQUEST, "ERR_NUA");
    CHECK_EQ(TbtCtlClassify(TBT_EV_ERR_FC), TBT_NOTE_EVENT, "ERR_FC");
    CHECK_EQ(TbtCtlClassify(TBT_EV_DPTX_DISCOVERY), TBT_NOTE_ACK_IGNORE, "DPTX");
    CHECK_EQ(TbtCtlClassify(TBT_EV_ASYM_LINK), TBT_NOTE_SILENT, "ASYM_LINK");
    CHECK_EQ(TbtCtlClassify(TBT_EV_POP_CMPLT), TBT_NOTE_SILENT, "POP_CMPLT");
    CHECK_EQ(TbtCtlClassify(TBT_EV_HP_ACK), TBT_NOTE_HP_ACK, "HP_ACK");
    CHECK_EQ(TbtCtlClassify(3), TBT_NOTE_UNKNOWN, "a reserved code");
}

static void test_u4_hotplug(void)
{
    TBT_ROUTE dev = { 0, 1 };
    TBT_HP_EVENT e;
    ULONG out[1];
    ULONG i;
    long t;
    LONG rc;

    /* A hot plug event inside a response wait: acknowledged at once,
     * queued once though retransmitted, and the read completes. */
    CHECK_EQ(u4Setup(4), TBT_OK, "start");
    rc = TbtCtlRead1(&g_ctl, &dev, TBT_CS_ROUTER, 0, 0, out);
    CHECK_EQ(rc, TBT_OK, "read completes");
    CHECK_EQ(countTx(TBT_PDF_NOTIFY), 2, "each copy acknowledged");
    t = findTx(TBT_PDF_NOTIFY);
    CHECK(t >= 0 && g_fake.TxLogDw[t][2] ==
          (TBT_EV_HP_ACK | (1UL << 8) | (2UL << 30)),
          "HP_ACK, adapter 1, PG 10b (Spec Table 6-6)");
    CHECK(TbtCtlNextHotPlug(&g_ctl, &e), "queued");
    CHECK_EQ(e.Adapter, 1, "adapter 1");
    CHECK(!e.Unplug, "a plug");
    CHECK(!TbtCtlNextHotPlug(&g_ctl, &e), "the retransmission was dropped");
    CHECK_EQ(g_ctl.Counters.HotPlugDuplicates, 1, "and counted");

    /* An unplug: PG 11b. */
    CHECK_EQ(u4Setup(0), TBT_OK, "start");
    {
        unsigned long f[3];
        f[0] = 0x80000000UL;
        f[1] = 0;
        f[2] = 0x80000003UL;
        fakeQueue(TBT_PDF_HOTPLUG, f, 3, 0, 0);
    }
    fakePump();
    TbtCtlService(&g_ctl);
    t = findTx(TBT_PDF_NOTIFY);
    CHECK(t >= 0 && g_fake.TxLogDw[t][2] ==
          (TBT_EV_HP_ACK | (3UL << 8) | (3UL << 30)), "PG 11b for an unplug");
    CHECK(TbtCtlNextHotPlug(&g_ctl, &e) && e.Unplug && e.Adapter == 3, "queued as unplug");

    /* Overflow: the loss is recorded so every port gets a check. */
    CHECK_EQ(u4Setup(0), TBT_OK, "start");
    for (i = 0; i < TBT_CTL_HP_QUEUE + 2; i++) {
        unsigned long f[3];
        f[0] = 0x80000000UL;
        f[1] = 0;
        f[2] = (i % 60) + 1;
        fakeQueue(TBT_PDF_HOTPLUG, f, 3, 0, 0);
        if ((i % 8) == 7) {
            fakePump();
            TbtCtlService(&g_ctl);
        }
    }
    fakePump();
    TbtCtlService(&g_ctl);
    CHECK(g_ctl.HpLost, "overflow recorded");
    CHECK_EQ(g_ctl.Counters.HotPlugOverflows, 2, "two lost");
    CHECK_EQ(countTx(TBT_PDF_NOTIFY), TBT_CTL_HP_QUEUE + 2, "every one acknowledged anyway");
}

static void test_u4_waits(void)
{
    TBT_WAIT w;
    TBT_HOST h;

    h = g_fakeHost;
    fakeReset();

    /* The clock wraps: compared by difference only. */
    g_fake.NowUs = 0xFFFFFF00UL;
    TbtWaitStart(&w, &h, 1000, FALSE);
    CHECK(!TbtWaitDone(&w, &h), "not yet");
    g_fake.NowUs = 0x00000080UL;
    CHECK(!TbtWaitDone(&w, &h), "384 us, across the wrap: not yet");
    g_fake.NowUs = 0x000002E8UL;
    CHECK(TbtWaitDone(&w, &h), "1000 us across the wrap: passed");

    /* A maximum ends when the difference reaches it; a minimum needs a
     * tick more. */
    h.TickUs = 10000;
    g_fake.NowUs = 0;
    TbtWaitStart(&w, &h, 50000, FALSE);
    g_fake.NowUs = 50000;
    CHECK(TbtWaitDone(&w, &h), "maximum: at the bound");
    g_fake.NowUs = 0;
    TbtWaitStart(&w, &h, 50000, TRUE);
    g_fake.NowUs = 50000;
    CHECK(!TbtWaitDone(&w, &h), "minimum: not at the bound with a 10 ms tick");
    g_fake.NowUs = 60000;
    CHECK(TbtWaitDone(&w, &h), "minimum: the bound plus one tick");

    /* A stopped clock: the waits asked for end both kinds. */
    g_fake.NowUs = 0;
    g_fake.ClockStopped = 1;
    g_fake.AskedUs = 0;
    TbtWaitMin(&h, 10000);
    CHECK_EQ(g_fake.NowUs, 0, "the clock stood still and the minimum still ended");
    CHECK(g_fake.AskedUs >= 10000UL, "only after 10 ms of waits were asked for");
    CHECK(g_fake.AskedUs < 10000UL + 1000UL, "and not much more");
    TbtWaitStart(&w, &h, 10000, TRUE);
    w.WaitedUs = 9999;
    CHECK(!TbtWaitDone(&w, &h), "1 us short of the sum: not satisfied");
    w.WaitedUs = 10000;
    CHECK(TbtWaitDone(&w, &h), "the full sum: satisfied");
    g_fake.ClockStopped = 0;

    /* A sub-millisecond minimum is stalled, not slept. */
    g_fake.NowUs = 0;
    h.TickUs = 0;
    TbtWaitMin(&h, 2);
    CHECK(g_fake.NowUs >= 2 && g_fake.NowUs < 1000, "2 us stalled");

    /* Without ClockUs, NowMs stands in. */
    h.ClockUs = NULL;
    g_fake.NowUs = 7000;
    CHECK_EQ(TbtClockUs(&h), 7000UL, "NowMs times 1000");
}

static void test_u4_safe_point_and_routes(void)
{
    TBT_ROUTE a = { 0, 0 };
    TBT_ROUTE b;
    TBT_ROUTE c;

    CHECK_EQ(u4Setup(0), TBT_OK, "start");
    TbtCtlSafePoint(&g_ctl);
    CHECK_EQ(g_safeCalls, 0, "no callback set: nothing");
    TbtCtlSetSafePoint(&g_ctl, safeCb, &g_ctl);
    TbtCtlSafePoint(&g_ctl);
    CHECK_EQ(g_safeCalls, 1, "called once");
    CHECK_EQ(g_safeNested, 0, "a safe point inside one does not nest");
    TbtCtlSafePoint(&g_ctl);
    CHECK_EQ(g_safeCalls, 2, "called again afterwards");

    CHECK(TbtRouteChild(&a, 3, &b), "host router to adapter 3");
    CHECK(b.Lo == 3 && b.Hi == 0, "depth 1");
    CHECK(TbtRouteChild(&b, 5, &c), "and below");
    CHECK(c.Lo == 0x0503UL, "level 1 is byte 1");
    b.Lo = 0x01010101UL;
    b.Hi = 0x00000101UL;
    CHECK(TbtRouteChild(&b, 2, &c) && c.Hi == 0x00020101UL, "level 6 in the high DW");
    b.Hi = 0x00010101UL;
    CHECK(!TbtRouteChild(&b, 2, &c), "no eighth level");
    CHECK(!TbtRouteChild(&a, 0, &c), "adapter 0 is no hop");
    CHECK(!TbtRouteChild(&a, 64, &c), "nor 64");
}

/* Each vector named in the request trace (tbt_fake.h, task 44.38). */
#define RUN_VEC(fn) do { fakeTraceMark(#fn); fn(); } while (0)

int main(void)
{
    RUN_VEC(test_crc_vectors);
    RUN_VEC(test_start);
    RUN_VEC(test_start_refusals);
    RUN_VEC(test_send);
    RUN_VEC(test_send_limits);
    RUN_VEC(test_tx_exhaustion);
    RUN_VEC(test_receive_wrap);
    RUN_VEC(test_rx_exhaustion);
    RUN_VEC(test_overflow_and_malformed);
    RUN_VEC(test_trailer);
    RUN_VEC(test_ctl_read);
    RUN_VEC(test_stopped_clock_and_flood);
    RUN_VEC(test_stop);
    RUN_VEC(test_routes);
    RUN_VEC(test_u4_geometry_and_takeover);
    RUN_VEC(test_u4_write_and_read);
    RUN_VEC(test_u4_echo);
    RUN_VEC(test_u4_budget);
    RUN_VEC(test_u4_notifications);
    RUN_VEC(test_u4_hotplug);
    RUN_VEC(test_u4_waits);
    RUN_VEC(test_u4_safe_point_and_routes);
    fakeTraceMark("end");
    printf("test_tbt_ring: %d checks, %d failures\n", checks, failures);
    return failures;
}
