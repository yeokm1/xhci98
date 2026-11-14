/*
 * test_bot.c - host vectors for the Bulk-Only transport machine and its
 * parsers (src\xhci_bot.c; roadmap-usb4 task 49.34, design record 20
 * section 4.3.4, M3 and M5).
 *
 * Every CBW, CSW, sense block and capacity answer below is built by this
 * file from Bulk-Only Transport 1.0 sections 5.1 and 5.2 and SPC/SBC's
 * layouts, and every expected state worked by hand from M3's transitions,
 * not from the code under test. The command shapes follow the two captures
 * of record 20 section 4.3.4 M15 (Windows 2000's usbstor: Get Max LUN after
 * the configuration, WRITE(10) of 64 KiB, READ CAPACITY, SYNCHRONIZE CACHE).
 */

#include <stdio.h>
#include <string.h>
#include "../src/xhci_bot.h"
#include "test_harness.h"

static UCHAR cbw[31];
static UCHAR csw[13];
static UCHAR data[64];

static void put32(UCHAR *p, ULONG v)
{
    p[0] = (UCHAR)(v & 0xFF);
    p[1] = (UCHAR)((v >> 8) & 0xFF);
    p[2] = (UCHAR)((v >> 16) & 0xFF);
    p[3] = (UCHAR)((v >> 24) & 0xFF);
}

static void putbe32(UCHAR *p, ULONG v)
{
    p[0] = (UCHAR)((v >> 24) & 0xFF);
    p[1] = (UCHAR)((v >> 16) & 0xFF);
    p[2] = (UCHAR)((v >> 8) & 0xFF);
    p[3] = (UCHAR)(v & 0xFF);
}

/* A CBW by hand: tag, length, IN or OUT, LUN, CDB opcode, CDB length,
 * the CDB's second byte. */
static void mkcbw(ULONG tag, ULONG len, int in, int lun, int op, int cblen,
                  int b1)
{
    memset(cbw, 0, sizeof(cbw));
    put32(cbw, 0x43425355UL);
    put32(cbw + 4, tag);
    put32(cbw + 8, len);
    cbw[12] = (UCHAR)(in ? 0x80 : 0x00);
    cbw[13] = (UCHAR)lun;
    cbw[14] = (UCHAR)cblen;
    cbw[15] = (UCHAR)op;
    cbw[16] = (UCHAR)b1;
}

static void mkcsw(ULONG tag, ULONG residue, int status)
{
    memset(csw, 0, sizeof(csw));
    put32(csw, 0x53425355UL);
    put32(csw + 4, tag);
    put32(csw + 8, residue);
    csw[12] = (UCHAR)status;
}

/* A drive configured and its max LUN read as 0, as Windows 2000's usbstor
 * does at its start. */
static void fresh(XHCI_BOT *b)
{
    XhciBotInit(b);
    XhciBotSelected(b, 1, 0);
    XhciBotClassRequest(b, XHCI_BOT_REQ_MAX_LUN, 1, 0, 0, 1);
}

/* One whole command: CBW, its data in `chunks` transfers, the CSW. */
static void command(XHCI_BOT *b, ULONG tag, ULONG len, int in, int op,
                    int cblen, int b1, const UCHAR *payload, ULONG chunks,
                    ULONG residue, int status)
{
    ULONG each;
    ULONG done;
    ULONG n;
    ULONG i;

    mkcbw(tag, len, in, 0, op, cblen, b1);
    XhciBotSubmit(b, 0, 31, cbw);
    XhciBotComplete(b, 0, XHCI_BOT_XFER_OK, 31, 31, cbw);
    done = 0;
    if (len != 0) {
        each = (len + chunks - 1) / chunks;
        for (i = 0; i < chunks && done < len; i++) {
            n = (len - done) < each ? (len - done) : each;
            XhciBotSubmit(b, (ULONG)in, n, in ? NULL : payload);
            XhciBotComplete(b, (ULONG)in, XHCI_BOT_XFER_OK, n, n,
                            payload != NULL ? payload + done : NULL);
            done += n;
        }
    }
    mkcsw(tag, residue, status);
    XhciBotSubmit(b, 1, 13, NULL);
    XhciBotComplete(b, 1, XHCI_BOT_XFER_OK, 13, 13, csw);
}

static void test_init_and_select(void)
{
    XHCI_BOT b;

    XhciBotInit(&b);
    CHECK_EQ(b.State, XHCI_BOT_UNKNOWN, "init: UNKNOWN");
    CHECK(!XhciBotKeepable(&b), "init: not keepable");
    XhciBotSelected(&b, 1, 0);
    CHECK_EQ(b.State, XHCI_BOT_SYNC, "first configuration: SYNC");
    CHECK(!XhciBotKeepable(&b), "SYNC, max LUN unknown: not keepable");
    XhciBotClassRequest(&b, XHCI_BOT_REQ_MAX_LUN, 1, 0, 0, 1);
    CHECK_EQ(b.MaxLun, 0, "Get Max LUN 0 recorded");
    CHECK(XhciBotKeepable(&b), "SYNC, LUN 0: keepable");
    /* The class driver selecting the alternate again before any command. */
    XhciBotSelected(&b, 0, 1);
    CHECK_EQ(b.State, XHCI_BOT_SYNC, "SELECT_INTERFACE before a CBW: SYNC");
    command(&b, 1, 0, 0, 0x00, 6, 0, NULL, 1, 0, 0);
    CHECK_EQ(b.State, XHCI_BOT_IDLE, "TEST UNIT READY: IDLE");
    XhciBotSelected(&b, 0, 1);
    CHECK_EQ(b.State, XHCI_BOT_UNKNOWN, "SELECT_INTERFACE after a CBW");
    CHECK_EQ(b.LastUnknown, XHCI_BOT_WHY_SELECT, "why select");
    fresh(&b);
    XhciBotSelected(&b, 0, 0);
    CHECK_EQ(b.State, XHCI_BOT_UNKNOWN, "a second SELECT_CONFIGURATION");
    /* Max LUN by STALL, 2, a change, a failure. */
    XhciBotInit(&b);
    XhciBotSelected(&b, 1, 0);
    XhciBotClassRequest(&b, XHCI_BOT_REQ_MAX_LUN, 0, 1, 0, 0);
    CHECK_EQ(b.MaxLun, 0, "Get Max LUN STALL: 0");
    CHECK(XhciBotKeepable(&b), "STALLed max LUN: keepable");
    XhciBotClassRequest(&b, XHCI_BOT_REQ_MAX_LUN, 1, 0, 1, 1);
    CHECK_EQ(b.State, XHCI_BOT_UNKNOWN, "max LUN changed: UNKNOWN");
    CHECK_EQ(b.MaxLun, XHCI_BOT_LUN_UNKNOWN, "max LUN changed: unknown");
    /* A STALL after the first answer, whichever the first was. */
    fresh(&b);
    XhciBotClassRequest(&b, XHCI_BOT_REQ_MAX_LUN, 1, 0, 0, 1);
    CHECK(XhciBotKeepable(&b), "the same answer again: keepable");
    XhciBotClassRequest(&b, XHCI_BOT_REQ_MAX_LUN, 0, 1, 0, 0);
    CHECK_EQ(b.LastUnknown, XHCI_BOT_WHY_MAX_LUN, "STALL after an answer");
    CHECK(!XhciBotKeepable(&b), "STALL after an answer: not keepable");
    XhciBotInit(&b);
    XhciBotSelected(&b, 1, 0);
    XhciBotClassRequest(&b, XHCI_BOT_REQ_MAX_LUN, 0, 1, 0, 0);
    XhciBotClassRequest(&b, XHCI_BOT_REQ_MAX_LUN, 0, 1, 0, 0);
    CHECK_EQ(b.LastUnknown, XHCI_BOT_WHY_MAX_LUN, "STALL after a STALL");
    /* A first request that failed, a recovery, then a STALL. */
    XhciBotInit(&b);
    XhciBotSelected(&b, 1, 0);
    XhciBotClassRequest(&b, XHCI_BOT_REQ_MAX_LUN, 0, 0, 0, 0);
    XhciBotClassRequest(&b, XHCI_BOT_REQ_RESET, 1, 0, 0, 0);
    XhciBotClearHalt(&b, 1, 1);
    XhciBotClearHalt(&b, 0, 1);
    XhciBotClassRequest(&b, XHCI_BOT_REQ_MAX_LUN, 0, 1, 0, 0);
    CHECK_EQ(b.LastUnknown, XHCI_BOT_WHY_MAX_LUN, "failed first, then STALL");
    CHECK(!XhciBotKeepable(&b), "failed first, then STALL: not keepable");
    XhciBotInit(&b);
    XhciBotSelected(&b, 1, 0);
    XhciBotClassRequest(&b, XHCI_BOT_REQ_MAX_LUN, 1, 0, 0, 0);
    XhciBotClassRequest(&b, XHCI_BOT_REQ_RESET, 1, 0, 0, 0);
    XhciBotClearHalt(&b, 1, 1);
    XhciBotClearHalt(&b, 0, 1);
    XhciBotClassRequest(&b, XHCI_BOT_REQ_MAX_LUN, 0, 1, 0, 0);
    CHECK(!XhciBotKeepable(&b), "empty first answer, then STALL");
    XhciBotInit(&b);
    XhciBotSelected(&b, 1, 0);
    XhciBotClassRequest(&b, XHCI_BOT_REQ_MAX_LUN, 1, 0, 20, 1);
    XhciBotClassRequest(&b, XHCI_BOT_REQ_RESET, 1, 0, 0, 0);
    XhciBotClearHalt(&b, 1, 1);
    XhciBotClearHalt(&b, 0, 1);
    XhciBotClassRequest(&b, XHCI_BOT_REQ_MAX_LUN, 0, 1, 0, 0);
    CHECK(!XhciBotKeepable(&b), "invalid first answer, then STALL");
    /* A new configuration gives the exception back. */
    XhciBotSelected(&b, 1, 0);
    XhciBotClassRequest(&b, XHCI_BOT_REQ_MAX_LUN, 0, 1, 0, 0);
    CHECK(XhciBotKeepable(&b), "first request of a new configuration");
    /* Mid-command. */
    fresh(&b);
    mkcbw(1, 512, 1, 0, 0x28, 10, 0);
    XhciBotSubmit(&b, 0, 31, cbw);
    XhciBotClassRequest(&b, XHCI_BOT_REQ_MAX_LUN, 1, 0, 0, 1);
    CHECK_EQ(b.LastUnknown, XHCI_BOT_WHY_MAX_LUN, "Get Max LUN mid-command");
    XhciBotInit(&b);
    XhciBotSelected(&b, 1, 0);
    XhciBotClassRequest(&b, XHCI_BOT_REQ_MAX_LUN, 1, 0, 1, 1);
    CHECK_EQ(b.MaxLun, 1, "two LUNs recorded");
    CHECK(!XhciBotKeepable(&b), "two LUNs: not keepable");
    XhciBotInit(&b);
    XhciBotSelected(&b, 1, 0);
    XhciBotClassRequest(&b, XHCI_BOT_REQ_MAX_LUN, 0, 0, 0, 0);
    CHECK_EQ(b.State, XHCI_BOT_UNKNOWN, "Get Max LUN error: UNKNOWN");
    XhciBotInit(&b);
    XhciBotSelected(&b, 1, 0);
    XhciBotClassRequest(&b, XHCI_BOT_REQ_MAX_LUN, 1, 0, 16, 1);
    CHECK_EQ(b.State, XHCI_BOT_UNKNOWN, "Get Max LUN 16: UNKNOWN");
    XhciBotInit(&b);
    XhciBotSelected(&b, 1, 0);
    XhciBotClassRequest(&b, 0x42, 1, 0, 0, 0);
    CHECK_EQ(b.State, XHCI_BOT_UNKNOWN, "an unknown class request");
}

static void test_ordinary_commands(void)
{
    XHCI_BOT b;
    static UCHAR big[16];

    fresh(&b);
    /* WRITE(10) of 64 KiB in four pieces, then SYNCHRONIZE CACHE. */
    command(&b, 0xB8030E28UL, 65536, 0, 0x2A, 10, 0, big, 4, 0, 0);
    CHECK_EQ(b.State, XHCI_BOT_IDLE, "WRITE(10) 64 KiB: IDLE");
    CHECK(XhciBotKeepable(&b), "after a WRITE: keepable");
    command(&b, 0xB8052ED8UL, 0, 0, 0x35, 10, 0, NULL, 1, 0, 0);
    CHECK_EQ(b.State, XHCI_BOT_IDLE, "SYNCHRONIZE CACHE: IDLE");
    /* READ(10) of 4 KiB. */
    command(&b, 7, 4096, 1, 0x28, 10, 0, NULL, 1, 0, 0);
    CHECK_EQ(b.State, XHCI_BOT_IDLE, "READ(10): IDLE");

    /* Mid-command at every phase: not keepable. */
    fresh(&b);
    mkcbw(9, 512, 1, 0, 0x28, 10, 0);
    XhciBotSubmit(&b, 0, 31, cbw);
    CHECK_EQ(b.State, XHCI_BOT_CBW_SENT, "CBW submitted");
    CHECK(!XhciBotKeepable(&b), "CBW_SENT: not keepable");
    XhciBotComplete(&b, 0, XHCI_BOT_XFER_OK, 31, 31, cbw);
    CHECK_EQ(b.State, XHCI_BOT_DATA, "CBW completed: DATA");
    CHECK(!XhciBotKeepable(&b), "the case K4 named: CBW done, no URB");
    XhciBotSubmit(&b, 1, 512, NULL);
    XhciBotComplete(&b, 1, XHCI_BOT_XFER_OK, 512, 512, NULL);
    CHECK_EQ(b.State, XHCI_BOT_STATUS, "data done: STATUS");
    CHECK(!XhciBotKeepable(&b), "STATUS: not keepable");
    mkcsw(9, 0, 0);
    XhciBotComplete(&b, 1, XHCI_BOT_XFER_OK, 13, 13, csw);
    CHECK_EQ(b.State, XHCI_BOT_IDLE, "CSW: IDLE");

    /* A short data phase ends it early; the CSW then carries a residue. */
    fresh(&b);
    mkcbw(10, 36, 1, 0, 0x12, 6, 0);
    XhciBotSubmit(&b, 0, 31, cbw);
    XhciBotComplete(&b, 0, XHCI_BOT_XFER_OK, 31, 31, cbw);
    XhciBotSubmit(&b, 1, 36, NULL);
    XhciBotComplete(&b, 1, XHCI_BOT_XFER_OK, 20, 36, NULL);
    CHECK_EQ(b.State, XHCI_BOT_STATUS, "short data: STATUS");
    mkcsw(10, 16, 0);
    XhciBotComplete(&b, 1, XHCI_BOT_XFER_OK, 13, 13, csw);
    CHECK_EQ(b.State, XHCI_BOT_IDLE, "residue 16 of 36: IDLE");
}

/* A data phase's payload that looks like a CSW or a CBW is data. */
static void test_signatures_in_data(void)
{
    XHCI_BOT b;
    UCHAR fake[13];
    UCHAR fakeCbw[31];

    fresh(&b);
    /* A 13-byte data-in phase whose bytes are a perfect CSW for the tag. */
    mkcbw(0x55, 13, 1, 0, 0x12, 6, 0);
    XhciBotSubmit(&b, 0, 31, cbw);
    XhciBotComplete(&b, 0, XHCI_BOT_XFER_OK, 31, 31, cbw);
    mkcsw(0x55, 0, 0);
    memcpy(fake, csw, 13);
    XhciBotSubmit(&b, 1, 13, NULL);
    XhciBotComplete(&b, 1, XHCI_BOT_XFER_OK, 13, 13, fake);
    CHECK_EQ(b.State, XHCI_BOT_STATUS, "a CSW-shaped data phase is data");
    CHECK(!XhciBotKeepable(&b), "still owed the real CSW");
    XhciBotComplete(&b, 1, XHCI_BOT_XFER_OK, 13, 13, csw);
    CHECK_EQ(b.State, XHCI_BOT_IDLE, "the real CSW");

    /* A 31-byte data-out phase whose bytes are a CBW. */
    fresh(&b);
    mkcbw(0x66, 31, 0, 0, 0x15, 6, 0);
    XhciBotSubmit(&b, 0, 31, cbw);
    XhciBotComplete(&b, 0, XHCI_BOT_XFER_OK, 31, 31, cbw);
    mkcbw(0x77, 0, 0, 0, 0x00, 6, 0);
    memcpy(fakeCbw, cbw, 31);
    XhciBotSubmit(&b, 0, 31, fakeCbw);
    CHECK_EQ(b.State, XHCI_BOT_DATA, "a CBW-shaped data-out is data");
    XhciBotComplete(&b, 0, XHCI_BOT_XFER_OK, 31, 31, fakeCbw);
    CHECK_EQ(b.State, XHCI_BOT_STATUS, "31 bytes moved: STATUS");
    mkcsw(0x77, 0, 0);
    XhciBotComplete(&b, 1, XHCI_BOT_XFER_OK, 13, 13, csw);
    CHECK_EQ(b.State, XHCI_BOT_UNKNOWN, "the fake CBW's tag is not the CSW's");
    CHECK_EQ(b.LastUnknown, XHCI_BOT_WHY_BAD_CSW, "why bad CSW");
}

static void test_bad_cbws(void)
{
    XHCI_BOT b;

    fresh(&b);
    XhciBotSubmit(&b, 0, 31, NULL);
    CHECK_EQ(b.LastUnknown, XHCI_BOT_WHY_BAD_CBW, "unreadable CBW");
    fresh(&b);
    mkcbw(1, 0, 0, 0, 0, 6, 0);
    cbw[0] = 0x56;
    XhciBotSubmit(&b, 0, 31, cbw);
    CHECK_EQ(b.LastUnknown, XHCI_BOT_WHY_BAD_CBW, "wrong signature");
    fresh(&b);
    mkcbw(1, 0, 0, 0, 0, 0, 0);
    XhciBotSubmit(&b, 0, 31, cbw);
    CHECK_EQ(b.LastUnknown, XHCI_BOT_WHY_BAD_CBW, "bCBWCBLength 0");
    fresh(&b);
    mkcbw(1, 0, 0, 0, 0, 17, 0);
    XhciBotSubmit(&b, 0, 31, cbw);
    CHECK_EQ(b.LastUnknown, XHCI_BOT_WHY_BAD_CBW, "bCBWCBLength 17");
    fresh(&b);
    mkcbw(1, 0, 0, 1, 0, 6, 0);
    XhciBotSubmit(&b, 0, 31, cbw);
    CHECK_EQ(b.LastUnknown, XHCI_BOT_WHY_BAD_CBW, "LUN above max LUN");
    fresh(&b);
    mkcbw(1, 0, 0, 0, 0, 6, 0);
    cbw[12] = 0x01;
    XhciBotSubmit(&b, 0, 31, cbw);
    CHECK_EQ(b.LastUnknown, XHCI_BOT_WHY_BAD_CBW, "reserved flag bit");
    fresh(&b);
    mkcbw(1, 0, 0, 0, 0, 6, 0);
    XhciBotSubmit(&b, 0, 32, cbw);
    CHECK_EQ(b.LastUnknown, XHCI_BOT_WHY_NOT_CBW, "32 bytes out");
    fresh(&b);
    XhciBotSubmit(&b, 1, 13, NULL);
    CHECK_EQ(b.LastUnknown, XHCI_BOT_WHY_NOT_CBW, "an IN while IDLE");
    fresh(&b);
    XhciBotComplete(&b, 1, XHCI_BOT_XFER_OK, 13, 13, NULL);
    CHECK_EQ(b.LastUnknown, XHCI_BOT_WHY_OTHER, "a completion with none open");
}

static void test_cbw_end_and_early(void)
{
    XHCI_BOT b;

    fresh(&b);
    mkcbw(1, 512, 1, 0, 0x28, 10, 0);
    XhciBotSubmit(&b, 0, 31, cbw);
    XhciBotComplete(&b, 0, XHCI_BOT_XFER_STALL, 0, 31, cbw);
    CHECK_EQ(b.LastUnknown, XHCI_BOT_WHY_CBW_END, "CBW STALLed");
    fresh(&b);
    XhciBotSubmit(&b, 0, 31, cbw);
    XhciBotComplete(&b, 0, XHCI_BOT_XFER_CANCEL, 0, 31, cbw);
    CHECK_EQ(b.LastUnknown, XHCI_BOT_WHY_CBW_END, "CBW cancelled");
    fresh(&b);
    XhciBotSubmit(&b, 0, 31, cbw);
    XhciBotComplete(&b, 0, XHCI_BOT_XFER_OK, 30, 31, cbw);
    CHECK_EQ(b.LastUnknown, XHCI_BOT_WHY_CBW_END, "CBW short");
    fresh(&b);
    XhciBotSubmit(&b, 0, 31, cbw);
    XhciBotSubmit(&b, 1, 512, NULL);
    CHECK_EQ(b.LastUnknown, XHCI_BOT_WHY_EARLY, "data posted before the CBW ended");
}

static void test_data_faults(void)
{
    XHCI_BOT b;

    fresh(&b);
    mkcbw(1, 512, 1, 0, 0x28, 10, 0);
    XhciBotSubmit(&b, 0, 31, cbw);
    XhciBotComplete(&b, 0, XHCI_BOT_XFER_OK, 31, 31, cbw);
    XhciBotSubmit(&b, 0, 512, data);
    CHECK_EQ(b.LastUnknown, XHCI_BOT_WHY_DIRECTION, "OUT in an IN data phase");
    fresh(&b);
    XhciBotSubmit(&b, 0, 31, cbw);
    XhciBotComplete(&b, 0, XHCI_BOT_XFER_OK, 31, 31, cbw);
    XhciBotSubmit(&b, 1, 1024, NULL);
    XhciBotComplete(&b, 1, XHCI_BOT_XFER_OK, 1024, 1024, NULL);
    CHECK_EQ(b.LastUnknown, XHCI_BOT_WHY_OVERRUN, "more than the CBW asked");
    fresh(&b);
    XhciBotSubmit(&b, 0, 31, cbw);
    XhciBotComplete(&b, 0, XHCI_BOT_XFER_OK, 31, 31, cbw);
    XhciBotSubmit(&b, 1, 512, NULL);
    XhciBotComplete(&b, 1, XHCI_BOT_XFER_ERROR, 0, 512, NULL);
    CHECK_EQ(b.LastUnknown, XHCI_BOT_WHY_DATA_END, "data transaction error");
    fresh(&b);
    XhciBotSubmit(&b, 0, 31, cbw);
    XhciBotComplete(&b, 0, XHCI_BOT_XFER_OK, 31, 31, cbw);
    XhciBotSubmit(&b, 1, 512, NULL);
    XhciBotComplete(&b, 1, XHCI_BOT_XFER_CANCEL, 0, 512, NULL);
    CHECK_EQ(b.LastUnknown, XHCI_BOT_WHY_DATA_END, "data cancelled");

    /* A data STALL, the class driver's clear, then the CSW. */
    fresh(&b);
    XhciBotSubmit(&b, 0, 31, cbw);
    XhciBotComplete(&b, 0, XHCI_BOT_XFER_OK, 31, 31, cbw);
    XhciBotSubmit(&b, 1, 512, NULL);
    XhciBotComplete(&b, 1, XHCI_BOT_XFER_STALL, 0, 512, NULL);
    CHECK_EQ(b.State, XHCI_BOT_DATA_STALL, "data STALL");
    CHECK(!XhciBotKeepable(&b), "DATA_STALL: not keepable");
    XhciBotSubmit(&b, 1, 13, NULL);
    CHECK_EQ(b.LastUnknown, XHCI_BOT_WHY_EARLY, "CSW read before the clear");
    fresh(&b);
    XhciBotSubmit(&b, 0, 31, cbw);
    XhciBotComplete(&b, 0, XHCI_BOT_XFER_OK, 31, 31, cbw);
    XhciBotSubmit(&b, 1, 512, NULL);
    XhciBotComplete(&b, 1, XHCI_BOT_XFER_STALL, 0, 512, NULL);
    XhciBotClearHalt(&b, 0, 1);
    CHECK_EQ(b.LastUnknown, XHCI_BOT_WHY_CLEAR, "the other endpoint cleared");
    fresh(&b);
    XhciBotSubmit(&b, 0, 31, cbw);
    XhciBotComplete(&b, 0, XHCI_BOT_XFER_OK, 31, 31, cbw);
    XhciBotSubmit(&b, 1, 512, NULL);
    XhciBotComplete(&b, 1, XHCI_BOT_XFER_STALL, 0, 512, NULL);
    XhciBotClearHalt(&b, 1, 1);
    CHECK_EQ(b.State, XHCI_BOT_STATUS, "clear after data STALL: STATUS");
    mkcsw(1, 512, 1);
    XhciBotComplete(&b, 1, XHCI_BOT_XFER_OK, 13, 13, csw);
    CHECK_EQ(b.State, XHCI_BOT_SENSE, "the failed command's CSW: SENSE");
    fresh(&b);
    XhciBotClearHalt(&b, 1, 1);
    CHECK_EQ(b.LastUnknown, XHCI_BOT_WHY_CLEAR, "a clear owed nothing");
    fresh(&b);
    XhciBotSubmit(&b, 0, 31, cbw);
    XhciBotComplete(&b, 0, XHCI_BOT_XFER_OK, 31, 31, cbw);
    XhciBotSubmit(&b, 1, 512, NULL);
    XhciBotComplete(&b, 1, XHCI_BOT_XFER_STALL, 0, 512, NULL);
    XhciBotClearHalt(&b, 1, 0);
    CHECK_EQ(b.LastUnknown, XHCI_BOT_WHY_CLEAR, "the clear failed");
}

static void test_csw_faults(void)
{
    XHCI_BOT b;

    /* Set up a no-data command waiting for its CSW. */
#define TO_STATUS(b) do { fresh(&(b)); mkcbw(3, 0, 0, 0, 0x00, 6, 0); \
    XhciBotSubmit(&(b), 0, 31, cbw); \
    XhciBotComplete(&(b), 0, XHCI_BOT_XFER_OK, 31, 31, cbw); } while (0)

    TO_STATUS(b);
    CHECK_EQ(b.State, XHCI_BOT_STATUS, "no data: STATUS at once");
    mkcsw(4, 0, 0);
    XhciBotComplete(&b, 1, XHCI_BOT_XFER_OK, 13, 13, csw);
    CHECK_EQ(b.LastUnknown, XHCI_BOT_WHY_BAD_CSW, "wrong tag");
    TO_STATUS(b);
    mkcsw(3, 0, 0);
    csw[0] = 0;
    XhciBotComplete(&b, 1, XHCI_BOT_XFER_OK, 13, 13, csw);
    CHECK_EQ(b.LastUnknown, XHCI_BOT_WHY_BAD_CSW, "wrong signature");
    TO_STATUS(b);
    mkcsw(3, 0, 0);
    XhciBotComplete(&b, 1, XHCI_BOT_XFER_OK, 12, 13, csw);
    CHECK_EQ(b.LastUnknown, XHCI_BOT_WHY_BAD_CSW, "12 bytes");
    TO_STATUS(b);
    mkcsw(3, 0, 0);
    XhciBotComplete(&b, 1, XHCI_BOT_XFER_OK, 13, 13, NULL);
    CHECK_EQ(b.LastUnknown, XHCI_BOT_WHY_BAD_CSW, "unreadable CSW");
    TO_STATUS(b);
    mkcsw(3, 1, 0);
    XhciBotComplete(&b, 1, XHCI_BOT_XFER_OK, 13, 13, csw);
    CHECK_EQ(b.LastUnknown, XHCI_BOT_WHY_BAD_CSW, "residue above L");
    TO_STATUS(b);
    mkcsw(3, 0, 3);
    XhciBotComplete(&b, 1, XHCI_BOT_XFER_OK, 13, 13, csw);
    CHECK_EQ(b.LastUnknown, XHCI_BOT_WHY_BAD_CSW, "status 3");
    TO_STATUS(b);
    mkcsw(3, 0, 2);
    XhciBotComplete(&b, 1, XHCI_BOT_XFER_OK, 13, 13, csw);
    CHECK_EQ(b.LastUnknown, XHCI_BOT_WHY_PHASE, "phase error");
    TO_STATUS(b);
    XhciBotComplete(&b, 1, XHCI_BOT_XFER_ERROR, 0, 13, NULL);
    CHECK_EQ(b.LastUnknown, XHCI_BOT_WHY_CSW_END, "CSW read error");
    TO_STATUS(b);
    XhciBotSubmit(&b, 0, 31, cbw);
    CHECK_EQ(b.LastUnknown, XHCI_BOT_WHY_DIRECTION, "OUT while the CSW is owed");
    /* One STALL and clear on the CSW read is allowed, a second is not. */
    TO_STATUS(b);
    XhciBotComplete(&b, 1, XHCI_BOT_XFER_STALL, 0, 13, NULL);
    CHECK_EQ(b.State, XHCI_BOT_STATUS_STALL, "CSW STALL");
    XhciBotClearHalt(&b, 1, 1);
    CHECK_EQ(b.State, XHCI_BOT_STATUS, "cleared: STATUS");
    mkcsw(3, 0, 0);
    XhciBotComplete(&b, 1, XHCI_BOT_XFER_OK, 13, 13, csw);
    CHECK_EQ(b.State, XHCI_BOT_IDLE, "CSW after one clear: IDLE");
    TO_STATUS(b);
    XhciBotComplete(&b, 1, XHCI_BOT_XFER_STALL, 0, 13, NULL);
    XhciBotClearHalt(&b, 1, 1);
    XhciBotComplete(&b, 1, XHCI_BOT_XFER_STALL, 0, 13, NULL);
    CHECK_EQ(b.LastUnknown, XHCI_BOT_WHY_CSW_END, "second CSW STALL");
#undef TO_STATUS
}

static void test_sense(void)
{
    XHCI_BOT b;

    fresh(&b);
    command(&b, 1, 0, 0, 0x00, 6, 0, NULL, 1, 0, 1);
    CHECK_EQ(b.State, XHCI_BOT_SENSE, "TUR CHECK CONDITION: SENSE");
    CHECK(!XhciBotKeepable(&b), "SENSE: not keepable");
    memset(data, 0, sizeof(data));
    data[0] = 0x70;
    data[2] = 0x06;
    data[7] = 10;
    data[12] = 0x29;
    command(&b, 2, 18, 1, 0x03, 6, 0, data, 1, 0, 0);
    CHECK_EQ(b.State, XHCI_BOT_IDLE, "REQUEST SENSE discharges SENSE");
    CHECK(XhciBotKeepable(&b), "after the sense: keepable");
    fresh(&b);
    command(&b, 1, 0, 0, 0x00, 6, 0, NULL, 1, 0, 1);
    command(&b, 2, 512, 1, 0x28, 10, 0, NULL, 1, 0, 0);
    CHECK_EQ(b.LastUnknown, XHCI_BOT_WHY_SENSE, "another command after SENSE");
    fresh(&b);
    command(&b, 1, 0, 0, 0x00, 6, 0, NULL, 1, 0, 1);
    command(&b, 2, 18, 1, 0x03, 6, 0, data, 1, 0, 1);
    CHECK_EQ(b.LastUnknown, XHCI_BOT_WHY_SENSE, "REQUEST SENSE failing");
    /* A spontaneous REQUEST SENSE from IDLE is an ordinary command. */
    fresh(&b);
    command(&b, 5, 18, 1, 0x03, 6, 0, data, 1, 0, 0);
    CHECK_EQ(b.State, XHCI_BOT_IDLE, "REQUEST SENSE from IDLE");
}

static void test_recovery_and_other(void)
{
    XHCI_BOT b;

    /* The reset recovery in order resynchronizes even from UNKNOWN. */
    fresh(&b);
    XhciBotOther(&b);
    CHECK_EQ(b.State, XHCI_BOT_UNKNOWN, "an abort: UNKNOWN");
    XhciBotSubmit(&b, 0, 31, cbw);
    CHECK_EQ(b.State, XHCI_BOT_UNKNOWN, "UNKNOWN is sticky");
    XhciBotClassRequest(&b, XHCI_BOT_REQ_RESET, 1, 0, 0, 0);
    CHECK_EQ(b.State, XHCI_BOT_RECOVERY, "Bulk-Only reset");
    CHECK(!XhciBotKeepable(&b), "RECOVERY: not keepable");
    XhciBotClearHalt(&b, 1, 1);
    CHECK_EQ(b.State, XHCI_BOT_RECOVERY, "Bulk-In cleared");
    XhciBotClearHalt(&b, 0, 1);
    CHECK_EQ(b.State, XHCI_BOT_SYNC, "Bulk-Out cleared: SYNC");
    CHECK(XhciBotKeepable(&b), "recovered: keepable");
    /* Out of order. */
    fresh(&b);
    XhciBotClassRequest(&b, XHCI_BOT_REQ_RESET, 1, 0, 0, 0);
    XhciBotClearHalt(&b, 0, 1);
    CHECK_EQ(b.LastUnknown, XHCI_BOT_WHY_RECOVERY, "Bulk-Out first");
    fresh(&b);
    XhciBotClassRequest(&b, XHCI_BOT_REQ_RESET, 1, 0, 0, 0);
    mkcbw(1, 0, 0, 0, 0, 6, 0);
    XhciBotSubmit(&b, 0, 31, cbw);
    CHECK_EQ(b.LastUnknown, XHCI_BOT_WHY_EARLY, "a CBW before the clears");
    fresh(&b);
    XhciBotClassRequest(&b, XHCI_BOT_REQ_RESET, 0, 1, 0, 0);
    CHECK_EQ(b.LastUnknown, XHCI_BOT_WHY_RECOVERY, "reset failed");
    /* Get Max LUN inside the recovery, before and after the Bulk-In clear. */
    fresh(&b);
    XhciBotClassRequest(&b, XHCI_BOT_REQ_RESET, 1, 0, 0, 0);
    XhciBotClassRequest(&b, XHCI_BOT_REQ_MAX_LUN, 1, 0, 0, 1);
    XhciBotClearHalt(&b, 1, 1);
    XhciBotClearHalt(&b, 0, 1);
    CHECK_EQ(b.State, XHCI_BOT_UNKNOWN, "Get Max LUN before the clears");
    CHECK(!XhciBotKeepable(&b), "Get Max LUN before the clears: not keepable");
    fresh(&b);
    XhciBotClassRequest(&b, XHCI_BOT_REQ_RESET, 1, 0, 0, 0);
    XhciBotClearHalt(&b, 1, 1);
    XhciBotClassRequest(&b, XHCI_BOT_REQ_MAX_LUN, 1, 0, 0, 1);
    XhciBotClearHalt(&b, 0, 1);
    CHECK_EQ(b.State, XHCI_BOT_UNKNOWN, "Get Max LUN between the clears");
    fresh(&b);
    XhciBotClassRequest(&b, XHCI_BOT_REQ_RESET, 1, 0, 0, 0);
    XhciBotClearHalt(&b, 1, 1);
    XhciBotClearHalt(&b, 1, 1);
    CHECK_EQ(b.LastUnknown, XHCI_BOT_WHY_RECOVERY, "Bulk-In twice");
    /* The bus's own port reset. */
    fresh(&b);
    XhciBotOther(&b);
    XhciBotPortReset(&b);
    CHECK_EQ(b.State, XHCI_BOT_SYNC, "port reset replay: SYNC");
    CHECK(XhciBotKeepable(&b), "port reset keeps max LUN");
}

static void test_capacity_record(void)
{
    XHCI_BOT b;
    XHCI_BOT_CAP now;

    /* READ CAPACITY (10), 8 bytes. */
    fresh(&b);
    memset(data, 0, sizeof(data));
    putbe32(data, 0x0003FFFFUL);
    putbe32(data + 4, 512);
    command(&b, 1, 8, 1, 0x25, 10, 0, data, 1, 0, 0);
    CHECK_EQ(b.Cap.Kind, XHCI_BOT_CAP_SIZE, "capacity recorded");
    CHECK_EQ(b.Cap.LastLbaLo, 0x0003FFFFUL, "last LBA");
    CHECK_EQ(b.Cap.BlockLength, 512, "block length");
    now = b.Cap;
    CHECK(XhciBotSameMedium(&b.Cap, &now), "same medium");
    now.LastLbaLo++;
    CHECK(!XhciBotSameMedium(&b.Cap, &now), "another size");
    now = b.Cap;
    now.BlockLength = 4096;
    CHECK(!XhciBotSameMedium(&b.Cap, &now), "another block length");
    now.Kind = XHCI_BOT_CAP_NO_MEDIUM;
    CHECK(!XhciBotSameMedium(&b.Cap, &now), "medium then none");
    /* Short by residue: 8 moved, residue 4 - only 4 relevant. */
    fresh(&b);
    command(&b, 1, 8, 1, 0x25, 10, 0, data, 1, 4, 0);
    CHECK_EQ(b.Cap.Kind, XHCI_BOT_CAP_NONE, "residue leaves 4: none");
    CHECK_EQ(b.State, XHCI_BOT_IDLE, "but the transport is IDLE");
    /* A data phase of 6 bytes, short. */
    fresh(&b);
    mkcbw(2, 8, 1, 0, 0x25, 10, 0);
    XhciBotSubmit(&b, 0, 31, cbw);
    XhciBotComplete(&b, 0, XHCI_BOT_XFER_OK, 31, 31, cbw);
    XhciBotSubmit(&b, 1, 8, NULL);
    XhciBotComplete(&b, 1, XHCI_BOT_XFER_OK, 6, 8, data);
    mkcsw(2, 2, 0);
    XhciBotComplete(&b, 1, XHCI_BOT_XFER_OK, 13, 13, csw);
    CHECK_EQ(b.Cap.Kind, XHCI_BOT_CAP_NONE, "6 bytes moved: none");
    /* Unreadable data. */
    fresh(&b);
    command(&b, 1, 8, 1, 0x25, 10, 0, NULL, 1, 0, 0);
    CHECK_EQ(b.Cap.Kind, XHCI_BOT_CAP_NONE, "unreadable: none");
    /* READ CAPACITY (16): 32 asked, 32 moved, residue 24 -> 8: none; 12. */
    fresh(&b);
    memset(data, 0, sizeof(data));
    putbe32(data, 1);
    putbe32(data + 4, 0x00000010UL);
    putbe32(data + 8, 512);
    command(&b, 1, 32, 1, 0x9E, 16, 0x10, data, 1, 24, 0);
    CHECK_EQ(b.Cap.Kind, XHCI_BOT_CAP_NONE, "RC16 residue leaves 8: none");
    command(&b, 2, 32, 1, 0x9E, 16, 0x10, data, 1, 20, 0);
    CHECK_EQ(b.Cap.Kind, XHCI_BOT_CAP_SIZE, "RC16 12 usable");
    CHECK_EQ(b.Cap.LastLbaHi, 1, "RC16 LBA high");
    CHECK_EQ(b.Cap.LastLbaLo, 0x10, "RC16 LBA low");
    /* Another service action of 9Eh is not a capacity. */
    fresh(&b);
    command(&b, 3, 32, 1, 0x9E, 16, 0x12, data, 1, 0, 0);
    CHECK_EQ(b.Cap.Kind, XHCI_BOT_CAP_NONE, "9Eh/12h: not a capacity");
    /* No medium: READ CAPACITY fails, REQUEST SENSE says 02/3A. */
    fresh(&b);
    command(&b, 1, 8, 1, 0x25, 10, 0, NULL, 1, 8, 1);
    CHECK_EQ(b.State, XHCI_BOT_SENSE, "capacity failed: SENSE");
    memset(data, 0, sizeof(data));
    data[0] = 0x70;
    data[2] = 0x02;
    data[7] = 10;
    data[12] = 0x3A;
    command(&b, 2, 18, 1, 0x03, 6, 0, data, 1, 0, 0);
    CHECK_EQ(b.Cap.Kind, XHCI_BOT_CAP_NO_MEDIUM, "no medium recorded");
    now.Kind = XHCI_BOT_CAP_NO_MEDIUM;
    CHECK(XhciBotSameMedium(&b.Cap, &now), "no medium both times");
    /* The same with sense too short to read: none. */
    fresh(&b);
    command(&b, 1, 8, 1, 0x25, 10, 0, NULL, 1, 8, 1);
    command(&b, 2, 18, 1, 0x03, 6, 0, data, 1, 6, 0);
    CHECK_EQ(b.Cap.Kind, XHCI_BOT_CAP_NONE, "sense 12 usable: none");
    /* Nothing recorded never matches. */
    XhciBotInit(&b);
    CHECK(!XhciBotSameMedium(&b.Cap, &now), "no record: no match");
}

static void test_parsers(void)
{
    ULONG key;
    ULONG asc;
    ULONG ascq;
    XHCI_BOT_CAP cap;
    UCHAR built[31];
    UCHAR cdb[10];
    ULONG tag;
    ULONG len;
    ULONG in;
    ULONG op;
    ULONG sa;

    memset(data, 0, sizeof(data));
    data[0] = 0x72;
    data[1] = 0x06;
    data[2] = 0x28;
    data[3] = 0x00;
    CHECK(XhciBotParseSense(data, 4, &key, &asc, &ascq), "descriptor sense");
    CHECK_EQ(asc, 0x28, "descriptor ASC");
    CHECK(!XhciBotParseSense(data, 3, &key, &asc, &ascq), "descriptor short");
    data[0] = 0x70;
    data[7] = 5;
    CHECK(!XhciBotParseSense(data, 18, &key, &asc, &ascq),
          "fixed with additional length 5");
    data[7] = 10;
    CHECK(!XhciBotParseSense(data, 13, &key, &asc, &ascq), "fixed 13 bytes");
    data[0] = 0x00;
    CHECK(!XhciBotParseSense(data, 18, &key, &asc, &ascq), "response 00h");
    CHECK(!XhciBotParseCapacity(data, 7, 0, &cap), "RC10 7 bytes");
    CHECK(!XhciBotParseCapacity(data, 11, 1, &cap), "RC16 11 bytes");
    CHECK_EQ(XhciBotUsable(32, 32, 24), 8, "usable by residue");
    CHECK_EQ(XhciBotUsable(6, 8, 0), 6, "usable by bytes moved");
    CHECK_EQ(XhciBotUsable(8, 8, 9), 0, "residue above length");

    memset(cdb, 0, sizeof(cdb));
    cdb[0] = 0x25;
    XhciBotBuildCbw(built, 0x12345678UL, 8, 1, cdb, 10);
    CHECK(XhciBotParseCbw(built, 31, 0, &tag, &len, &in, &op, &sa),
          "built CBW parses");
    CHECK_EQ(tag, 0x12345678UL, "built tag");
    CHECK_EQ(len, 8, "built length");
    CHECK_EQ(in, 1, "built direction");
    CHECK_EQ(op, 0x25, "built opcode");
    CHECK_EQ(built[14], 10, "built CB length");

    CHECK_EQ(XhciBotTurSense(0x06, 0x29, 0x00), XHCI_BOT_TUR_RETRY,
             "29h read past");
    CHECK_EQ(XhciBotTurSense(0x06, 0x29, 0x03), XHCI_BOT_TUR_RETRY,
             "29h/03h read past");
    CHECK_EQ(XhciBotTurSense(0x06, 0x28, 0x00), XHCI_BOT_TUR_FAIL,
             "28h fails");
    CHECK_EQ(XhciBotTurSense(0x06, 0x2A, 0x01), XHCI_BOT_TUR_FAIL,
             "2Ah fails");
    CHECK_EQ(XhciBotTurSense(0x06, 0x3F, 0x03), XHCI_BOT_TUR_FAIL,
             "3Fh fails");
    CHECK_EQ(XhciBotTurSense(0x02, 0x04, 0x01), XHCI_BOT_TUR_BECOMING,
             "becoming ready");
    CHECK_EQ(XhciBotTurSense(0x02, 0x04, 0x02), XHCI_BOT_TUR_FAIL,
             "initializing command required fails");
    CHECK_EQ(XhciBotTurSense(0x02, 0x3A, 0x00), XHCI_BOT_TUR_NO_MEDIUM,
             "medium not present");
    CHECK_EQ(XhciBotTurSense(0x03, 0x11, 0x00), XHCI_BOT_TUR_FAIL,
             "medium error fails");
}

/* M18 (owner, 2026-11-13): XhciResumeKeepBot absent keeps a Bulk-Only
 * drive after a hibernation on Windows 2000 alone. */
static void test_keep_default(void)
{
    CHECK_EQ(XhciBotKeepDefault(1), XHCI_BOT_KEEP_HIBERNATE,
             "2000: after a hibernation");
    CHECK_EQ(XhciBotKeepDefault(0), 0, "any other Windows: none");
    CHECK_EQ(XhciBotKeepDefault(1) & XHCI_BOT_KEEP_STANDBY, 0,
             "2000: not after a standby");
    CHECK_EQ(XHCI_BOT_KEEP_MASK,
             XHCI_BOT_KEEP_HIBERNATE | XHCI_BOT_KEEP_STANDBY,
             "the mask is the two bits");
}

int main(void)
{
    test_init_and_select();
    test_ordinary_commands();
    test_signatures_in_data();
    test_bad_cbws();
    test_cbw_end_and_early();
    test_data_faults();
    test_csw_faults();
    test_sense();
    test_recovery_and_other();
    test_capacity_record();
    test_parsers();
    test_keep_default();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
