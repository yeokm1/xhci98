/*
 * test_xhq.c - xhci98.sys's query request (src\xhci_qry.c, design record
 * 13 section 8.12, design record 25 revision 12): the answer composer at
 * each XhciLogVerbosity - counters from 1, the ring from 2, the ring
 * withheld at 4, the PCI configuration at every level - the ring's text
 * (oldest first, bytes replaced, the cut after a wrap or a drain), the PCI
 * states, the sizing answers; XHCITR's reader of it (xhcitr\tr_log.c
 * tr_xhq_parse) with bad shapes, and the log and PCI texts it makes
 * (tr_xhq_log_text, tr_pci.c tr_xhq_pci_text), the "not served" text for a
 * driver that refuses the request; and the shared default level.
 */

#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include "../src/xhci_qryb.h"
#include "../src/xhci_verpk.h"
#include "../src/xhci.h"
#include "../src/xhci_xport.h"
#include "../src/xhci_log.h"
#include "../src/xhci_tol.h"
#include "../xhcitr/tr_log.h"
#include "../xhcitr/tr_pci.h"
#include "test_harness.h"

static UCHAR ring[16384];
static ULONG ctr[252];
static UCHAR pci[256];
static UCHAR out[XHQ_MAX_BYTES + 64];
static char text[65536];

static ULONG get32(const UCHAR *b, ULONG o)
{
    return (ULONG)b[o] | ((ULONG)b[o + 1] << 8) | ((ULONG)b[o + 2] << 16) |
           ((ULONG)b[o + 3] << 24);
}

static void put32(UCHAR *b, ULONG o, ULONG v)
{
    b[o] = (UCHAR)v;
    b[o + 1] = (UCHAR)(v >> 8);
    b[o + 2] = (UCHAR)(v >> 16);
    b[o + 3] = (UCHAR)(v >> 24);
}

/* A ring holding `s` from offset `at`, Used = its length. */
static void in_base(XHCI_QRY_IN *in, ULONG level, const char *s, ULONG at)
{
    ULONG n = (ULONG)strlen(s);
    ULONG i;

    memset(ring, 0, sizeof(ring));
    for (i = 0; i < n; i++) {
        ring[(at + i) & 16383UL] = (UCHAR)s[i];
    }
    memset(in, 0, sizeof(*in));
    in->Verbosity = level;
    in->VerbosityRead = level;
    in->Ring = ring;
    in->RingBytes = sizeof(ring);
    in->Head = (at + n) & 16383UL;
    in->Used = n;
    in->Appends = 3;
    in->Counters = ctr;
    in->CounterWords = 252;
    in->Pci = pci;
    in->PciState = XHQ_PCI_READ;
    in->Flavour = 1;
    in->Started = 1;
}

static void test_layout(void)
{
    CHECK_EQ(XHQ_IOCTL, (0x22UL << 16) | (0xA48UL << 2), "the control code");
    CHECK_EQ(XHQ_MAGIC, 'X' | ('H' << 8) | ('C' << 16) | ((ULONG)'Q' << 24),
             "'XHCQ' in byte order");
    CHECK_EQ(XHQ_O_FLAVOUR + 4, XHQ_HDR_BYTES_V1, "version 1's header");
    CHECK_EQ(XHQ_O_XPORT_RECORD + 4, XHQ_HDR_BYTES_V2, "version 2's header");
    CHECK_EQ(XHQ_O_PORTS_BYTES + 4, XHQ_HDR_BYTES, "the header is full");
    CHECK(XHQ_MAX_BYTES <= XHQ_ASK_BYTES, "one ask holds the largest");
    CHECK(252 * 4 <= XHQ_CTR_MAX, "the counter block fits");
    CHECK_EQ(XHQ_LOG_MAX, XHCI_LOG_RING_BYTES, "the ring's size");
    /* The owner's decision of 2026-10-16: both drivers default to 2. */
    CHECK_EQ(XHCI_LOG_VERBOSITY_DEFAULT, XHCI_LOG_VERBOSITY_RING,
             "the default level is the ring rung");
}

static void test_levels(void)
{
    XHCI_QRY_IN in;
    ULONG info;
    ULONG want;
    const char *s = "a=00000001\r\nb=00000002\r\n";

    ctr[0] = 5;
    ctr[3] = 7;
    pci[0] = 0x86;
    pci[1] = 0x80;

    /* Level 0: the PCI configuration alone. */
    in_base(&in, 0, s, 0);
    CHECK_EQ(XhciQryAnswer(out, sizeof(out), &in, &info), XHQ_ANS_WHOLE,
             "level 0 answers");
    CHECK_EQ(info, XHQ_HDR_BYTES + 256, "header and PCI");
    CHECK_EQ(get32(out, XHQ_O_CTR_BYTES), 0, "no counters at 0");
    CHECK_EQ(get32(out, XHQ_O_LOG_BYTES), 0, "no log at 0");
    CHECK_EQ(get32(out, XHQ_O_PCI_BYTES), 256, "PCI at 0");
    CHECK_EQ(out[get32(out, XHQ_O_PCI_OFFSET)], 0x86, "PCI bytes");
    CHECK_EQ(get32(out, XHQ_O_FLAGS), XHQ_F_STARTED, "flags at 0");

    /* Level 1: counters. */
    in_base(&in, 1, s, 0);
    CHECK_EQ(XhciQryAnswer(out, sizeof(out), &in, &info), XHQ_ANS_WHOLE, "1");
    CHECK_EQ(get32(out, XHQ_O_CTR_BYTES), 252 * 4, "counters at 1");
    CHECK_EQ(get32(out, XHQ_HDR_BYTES + 12), 7, "counter word 3");
    CHECK_EQ(get32(out, XHQ_O_LOG_BYTES), 0, "no log at 1");
    CHECK(get32(out, XHQ_O_FLAGS) & XHQ_F_COUNTERS, "counters flag");

    /* Level 2 and 3: the ring as text. */
    in_base(&in, 2, s, 0);
    CHECK_EQ(XhciQryAnswer(out, sizeof(out), &in, &info), XHQ_ANS_WHOLE, "2");
    want = (ULONG)strlen(s);
    CHECK_EQ(get32(out, XHQ_O_LOG_BYTES), want, "the log at 2");
    CHECK(memcmp(out + get32(out, XHQ_O_LOG_OFFSET), s, want) == 0, "as text");
    CHECK_EQ(info, XHQ_HDR_BYTES + 1008 + want + 256, "the whole");
    CHECK(get32(out, XHQ_O_FLAGS) & XHQ_F_RECORDING, "recording");
    in_base(&in, 3, s, 0);
    (void)XhciQryAnswer(out, sizeof(out), &in, &info);
    CHECK_EQ(get32(out, XHQ_O_LOG_BYTES), want, "the log at 3");

    /* Level 4: withheld. */
    in_base(&in, 4, s, 0);
    (void)XhciQryAnswer(out, sizeof(out), &in, &info);
    CHECK_EQ(get32(out, XHQ_O_LOG_BYTES), 0, "no log at 4");
    CHECK(get32(out, XHQ_O_FLAGS) & XHQ_F_WITHHELD, "withheld");
    CHECK_EQ(get32(out, XHQ_O_CTR_BYTES), 1008, "counters at 4");
}

static void test_ring(void)
{
    XHCI_QRY_IN in;
    ULONG info;
    const char *s = "ead=00000001\r\nb=00000002\r\n";
    ULONG at;

    /* Wrapped across the end, the oldest a record's tail: cut. */
    in_base(&in, 2, s, 16380);
    in.Dropped = 40;
    CHECK_EQ(XhciQryAnswer(out, sizeof(out), &in, &info), XHQ_ANS_WHOLE, "w");
    at = get32(out, XHQ_O_LOG_OFFSET);
    CHECK_EQ(get32(out, XHQ_O_LOG_BYTES), 12, "from the next record");
    CHECK(memcmp(out + at, "b=00000002\r\n", 12) == 0, "across the wrap");
    CHECK(get32(out, XHQ_O_FLAGS) & XHQ_F_LOG_CUT, "cut");
    /* A drain (DebugView) cuts too. */
    in_base(&in, 2, s, 0);
    in.Emitted = 1;
    (void)XhciQryAnswer(out, sizeof(out), &in, &info);
    CHECK_EQ(get32(out, XHQ_O_LOG_BYTES), 12, "drain cut");
    CHECK(get32(out, XHQ_O_FLAGS) & XHQ_F_EMITTED, "emitted");
    /* Bytes outside text replaced. */
    in_base(&in, 2, "x=\x01\x80\tZ\r\n", 0);
    (void)XhciQryAnswer(out, sizeof(out), &in, &info);
    CHECK(memcmp(out + get32(out, XHQ_O_LOG_OFFSET), "x=...Z\r\n", 8) == 0,
          "replaced");
    /* A ring of a wrong shape is empty. */
    in_base(&in, 2, "a=1\n", 0);
    in.RingBytes = 1000;
    (void)XhciQryAnswer(out, sizeof(out), &in, &info);
    CHECK_EQ(get32(out, XHQ_O_LOG_BYTES), 0, "not a power of two");
    in_base(&in, 2, "a=1\n", 0);
    in.Used = 20000;
    (void)XhciQryAnswer(out, sizeof(out), &in, &info);
    CHECK_EQ(get32(out, XHQ_O_LOG_BYTES), 0, "used past the ring");
    /* A full ring. */
    in_base(&in, 2, "", 0);
    memset(ring, 'a', sizeof(ring));
    in.Used = 16384;
    in.Head = 100;
    CHECK_EQ(XhciQryAnswer(out, sizeof(out), &in, &info), XHQ_ANS_WHOLE,
             "the largest fits the most");
    CHECK(info <= XHQ_MAX_BYTES, "within the bound");
}

static void test_pci_sizing(void)
{
    XHCI_QRY_IN in;
    ULONG info;

    in_base(&in, 2, "a=00000001\r\n", 0);
    in.PciState = XHQ_PCI_FAILED;
    (void)XhciQryAnswer(out, sizeof(out), &in, &info);
    CHECK_EQ(get32(out, XHQ_O_PCI_BYTES), 0, "refused: no bytes");
    CHECK_EQ(get32(out, XHQ_O_PCI_STATE), XHQ_PCI_FAILED, "and the state");
    in.PciState = XHQ_PCI_ABSENT;
    (void)XhciQryAnswer(out, sizeof(out), &in, &info);
    CHECK_EQ(get32(out, XHQ_O_PCI_BYTES), 0, "absent: no bytes");
    in.PciState = XHQ_PCI_READ;
    in.Pci = NULL;
    (void)XhciQryAnswer(out, sizeof(out), &in, &info);
    CHECK_EQ(get32(out, XHQ_O_PCI_BYTES), 0, "no buffer: no bytes");

    in_base(&in, 2, "a=00000001\r\n", 0);
    CHECK_EQ(XhciQryAnswer(out, 63, &in, &info), XHQ_ANS_TOO_SMALL, "63");
    CHECK_EQ(info, 0, "nothing");
    memset(out, 0xA5, 128);
    CHECK_EQ(XhciQryAnswer(out, 64, &in, &info), XHQ_ANS_HEADER, "64");
    CHECK_EQ(info, 64, "version 1's header, for a version 1 reader");
    CHECK_EQ(get32(out, XHQ_O_BYTES), 88 + 1008 + 12 + 256, "the size needed");
    CHECK_EQ(get32(out, XHQ_O_VERSION), XHQ_VERSION, "version 3 said");
    CHECK_EQ(get32(out, XHQ_O_DRIVER_VERSION), 0xA5A5A5A5UL,
             "nothing written past 64");
    CHECK_EQ(XhciQryAnswer(out, 79, &in, &info), XHQ_ANS_HEADER, "79");
    CHECK_EQ(info, 64, "79: still 64");
    CHECK_EQ(XhciQryAnswer(out, 87, &in, &info), XHQ_ANS_HEADER, "87");
    CHECK_EQ(info, 64, "87, a version 2 reader's room: still 64");
    CHECK_EQ(XhciQryAnswer(out, 88, &in, &info), XHQ_ANS_HEADER, "88");
    CHECK_EQ(info, 88, "the whole header");
    CHECK_EQ(XhciQryAnswer(NULL, 64, &in, &info), XHQ_ANS_TOO_SMALL, "NULL");
}

static void test_reader(void)
{
    XHCI_QRY_IN in;
    ULONG info;
    TR_XHQ x;
    TR_TEXT t;
    unsigned char save[4];

    memset(ctr, 0, sizeof(ctr));
    ctr[0] = 5;
    memset(pci, 0, sizeof(pci));
    put32(pci, 0, 0x15C18086UL);
    put32(pci, 8, 0x0C033002UL);
    pci[0x3C] = 11;
    pci[0x3D] = 1;
    in_base(&in, 2, "hcd.start=00000001\r\n", 0);
    (void)XhciQryAnswer(out, sizeof(out), &in, &info);
    CHECK_EQ(tr_xhq_parse(out, info, &x), 1, "parsed");
    CHECK_EQ(x.Verbosity, 2, "level");
    CHECK_EQ(x.CtrBytes, 1008, "counters");
    CHECK_EQ(x.PciBytes, 256, "pci");
    tr_text_init(&t, text, sizeof(text));
    tr_xhq_log_text(&t, &x, 1);
    CHECK(strstr(text, "Recording:  on (XhciLogVerbosity 2)") != NULL, "on");
    CHECK(strstr(text, "hcd.start=00000001") != NULL, "the log");
    CHECK(strstr(text, "(1 of 252 nonzero.)") != NULL, "counters named");
    tr_text_init(&t, text, sizeof(text));
    CHECK_EQ(tr_xhq_pci_text(&t, &x), 1, "decoded");
    CHECK(strstr(text, "Vendor:device       8086:15C1, revision 02") != NULL,
          "ids");
    CHECK(strstr(text, "Interrupt           pin INTA, line 11") != NULL,
          "pin and line");
    CHECK(strstr(text, "(USB xHCI controller)") != NULL, "class");
    tr_text_init(&t, text, sizeof(text));
    CHECK_EQ(tr_xhq_pci_text(&t, NULL), 0, "older driver");
    CHECK(strstr(text, "Not served by this xhci98.sys") != NULL, "said");

    /* Level 0: off, the advice; PCI still read. */
    in_base(&in, 0, "", 0);
    (void)XhciQryAnswer(out, sizeof(out), &in, &info);
    CHECK_EQ(tr_xhq_parse(out, info, &x), 1, "level 0 parsed");
    tr_text_init(&t, text, sizeof(text));
    tr_xhq_log_text(&t, &x, 1);
    CHECK(strstr(text, "Recording:  off (XhciLogVerbosity 0)") != NULL, "off");
    CHECK(strstr(text, "View > Logging level") != NULL, "advice");
    CHECK(strstr(text, "Counters") == NULL, "no counters");
    tr_text_init(&t, text, sizeof(text));
    CHECK_EQ(tr_xhq_pci_text(&t, &x), 1, "PCI at level 0");
    /* Level 4: withheld. */
    in_base(&in, 4, "start.bar=FEDC0000\r\n", 0);
    (void)XhciQryAnswer(out, sizeof(out), &in, &info);
    CHECK_EQ(tr_xhq_parse(out, info, &x), 1, "level 4 parsed");
    tr_text_init(&t, text, sizeof(text));
    tr_xhq_log_text(&t, &x, 1);
    CHECK(strstr(text, "FEDC0000") == NULL, "no address shown");
    CHECK(strstr(text, "Level 4 records internal addresses") != NULL, "said 4");
    /* Unread PCI. */
    in_base(&in, 2, "", 0);
    in.PciState = XHQ_PCI_FAILED;
    (void)XhciQryAnswer(out, sizeof(out), &in, &info);
    CHECK_EQ(tr_xhq_parse(out, info, &x), 1, "parsed, unread");
    tr_text_init(&t, text, sizeof(text));
    CHECK_EQ(tr_xhq_pci_text(&t, &x), 0, "unread");
    CHECK(strstr(text, "Unread") != NULL, "said unread");

    /* Bad shapes. */
    in_base(&in, 2, "a=00000001\r\n", 0);
    (void)XhciQryAnswer(out, sizeof(out), &in, &info);
    CHECK_EQ(tr_xhq_parse(out, 88, &x), -1, "header alone");
    CHECK_EQ(tr_xhq_parse(out, 64, &x), 0, "a version 3 header cut to 64");
    CHECK_EQ(tr_xhq_parse(out, 80, &x), 0, "a version 3 header cut to 80");
    CHECK_EQ(tr_xhq_parse(out, info - 1, &x), -1, "short");
    memcpy(save, out + XHQ_O_CTR_BYTES, 4);
    put32(out, XHQ_O_CTR_BYTES, 983);
    CHECK_EQ(tr_xhq_parse(out, info, &x), 0, "counters not whole words");
    put32(out, XHQ_O_CTR_BYTES, XHQ_CTR_MAX + 4);
    CHECK_EQ(tr_xhq_parse(out, info, &x), 0, "counters past the bound");
    memcpy(out + XHQ_O_CTR_BYTES, save, 4);
    put32(out, XHQ_O_PCI_BYTES, 128);
    CHECK_EQ(tr_xhq_parse(out, info, &x), 0, "pci not 256");
    put32(out, XHQ_O_PCI_BYTES, 256);
    put32(out, XHQ_O_LOG_OFFSET, 32);
    CHECK_EQ(tr_xhq_parse(out, info, &x), 0, "log in the header");
    put32(out, XHQ_O_LOG_OFFSET, info);
    CHECK_EQ(tr_xhq_parse(out, info, &x), 0, "log past the end");
    put32(out, XHQ_O_LOG_OFFSET, 88 + 1008);
    put32(out, XHQ_O_VERSION, 4);
    CHECK_EQ(tr_xhq_parse(out, info, &x), 0, "version");
    put32(out, XHQ_O_VERSION, 3);
    put32(out, XHQ_O_MAGIC, 0);
    CHECK_EQ(tr_xhq_parse(out, info, &x), 0, "magic");
    CHECK_EQ(tr_xhq_parse(NULL, 0, &x), 0, "nothing");
}

/* Roadmap-usb4 43.57: the failed flag from the terminal reason, at every
 * level, the rest of the answer unchanged. */
static void test_failed(void)
{
    XHCI_QRY_IN in;
    UCHAR out[XHQ_MAX_BYTES];
    ULONG info;
    ULONG term;
    ULONG lv;
    ULONG plain;

    CHECK_EQ(XHQ_TERMINAL_LATCHED, XHCI_TOL_TERMINAL_FAILURES, "the shared value");
    CHECK_EQ(XHCI_TOL_TERMINAL_DMA_UNPROVEN, 5, "the last terminal");
    for (lv = 0; lv <= 4; lv++) {
        for (term = 0; term <= 5; term++) {
            in_base(&in, lv, "a=1\r\n", 0);
            CHECK_EQ(XhciQryAnswer(out, sizeof(out), &in, &info), XHQ_ANS_WHOLE,
                     "answers");
            plain = get32(out, XHQ_O_FLAGS);
            CHECK(!(plain & XHQ_F_FAILED), "not failed at none");
            in.Terminal = term;
            CHECK_EQ(XhciQryAnswer(out, sizeof(out), &in, &info), XHQ_ANS_WHOLE,
                     "answers failed");
            CHECK_EQ(get32(out, XHQ_O_FLAGS),
                     plain | (term >= XHCI_TOL_TERMINAL_FAILURES ? XHQ_F_FAILED : 0),
                     "the flag by the terminal");
            CHECK_EQ(get32(out, XHQ_O_VERSION), XHQ_VERSION, "the version");
        }
    }
    /* The header alone carries it too: the tray asks for 64 bytes. */
    in_base(&in, 2, "a=1\r\n", 0);
    in.Terminal = XHCI_TOL_TERMINAL_UNREADABLE;
    CHECK_EQ(XhciQryAnswer(out, XHQ_HDR_BYTES, &in, &info), XHQ_ANS_HEADER,
             "header alone");
    CHECK(get32(out, XHQ_O_FLAGS) & XHQ_F_FAILED, "flag in the header");
    CHECK_EQ(XhciQryAnswer(out, XHQ_HDR_BYTES_V1, &in, &info), XHQ_ANS_HEADER,
             "3.2.0.0's 64-byte ask");
    CHECK(get32(out, XHQ_O_FLAGS) & XHQ_F_FAILED, "flag in its 64 bytes");
    in.Terminal = XHCI_TOL_TERMINAL_OWED;
    CHECK_EQ(XhciQryAnswer(out, XHQ_HDR_BYTES, &in, &info), XHQ_ANS_HEADER,
             "header alone, owed");
    CHECK(!(get32(out, XHQ_O_FLAGS) & XHQ_F_FAILED), "a recovery owed is not failed");
}

/* Roadmap-usb4 44.8 and 44.35: version 2's driver version and transport
 * part, the reader of both versions, the records' match and words. */
static void test_v2(void)
{
    XHCI_QRY_IN in;
    ULONG info;
    ULONG at;
    ULONG lv;
    TR_XHQ x;
    TR_XPORT r;
    TR_U32 packed;
    TR_U32 want;
    char line[200];
    const char *s;

    /* The packed version against the string, field by field. */
    want = 0;
    at = 0;
    for (s = XHCI_VER_STR;; s++) {
        if (*s >= '0' && *s <= '9') {
            at = at * 10 + (ULONG)(*s - '0');
            continue;
        }
        want = (want << 8) | at;
        at = 0;
        if (*s == '\0') {
            break;
        }
    }
    CHECK_EQ(XHCI_VER_PACKED, want, "XHCI_VER_PACKED is XHCI_VER_STR");
    CHECK_EQ(XHCI_VER_PACK4(3, 3, 0, 0), 0x03030000UL, "3.3.0.0 packed");

    CHECK_EQ(XHQ_X_SLOT, XHCI_SNAPSHOT_HCD_X_SLOT * 4, "slot");
    CHECK_EQ(XHQ_X_PORT, XHCI_SNAPSHOT_HCD_X_PORT * 4, "port");
    CHECK_EQ(XHQ_X_ROUTE, XHCI_SNAPSHOT_HCD_X_ROUTE * 4, "route");
    CHECK_EQ(XHQ_X_IFACE, XHCI_SNAPSHOT_HCD_X_IFACE * 4, "iface");
    CHECK_EQ(XHQ_X_TRANSPORT, XHCI_SNAPSHOT_HCD_X_TRANSPORT * 4, "transport");
    CHECK_EQ(XHQ_X_WHY, XHCI_SNAPSHOT_HCD_X_WHY * 4, "why");
    CHECK_EQ(XHQ_X_ALTERNATE, XHCI_SNAPSHOT_HCD_X_ALTERNATE * 4, "alternate");
    CHECK_EQ(XHQ_X_INPUTS, XHCI_SNAPSHOT_HCD_X_INPUTS * 4, "inputs");
    CHECK_EQ(XHQ_X_TRIPLE, XHCI_SNAPSHOT_HCD_X_TRIPLE * 4, "triple");
    CHECK_EQ(XHQ_X_COUNT, XHCI_SNAPSHOT_HCD_X_COUNT * 4, "count");
    CHECK_EQ(XHQ_XPORT_RECORD_BYTES, XHCI_SNAPSHOT_HCD_X_RECORD_WORDS * 4,
             "the record");
    CHECK(XHQ_XPORT_MAX >= XHCI_MAX_SLOTS, "a record a slot");
    CHECK_EQ(TR_XPORT_UAS, XHCI_XPORT_UAS, "UAS");
    CHECK_EQ(TR_XPORT_BOT, XHCI_XPORT_BOT, "BOT");
    CHECK_EQ(TR_XPORT_REFUSED, XHCI_XPORT_REFUSED, "refused");
    CHECK_EQ(TR_XPORT_WHY_UAS, XHCI_XPORT_WHY_UAS, "why UAS");
    CHECK_EQ(TR_XPORT_WHY_UAS_ONLY, XHCI_XPORT_WHY_UAS_ONLY, "why UAS only");
    CHECK_EQ(TR_XPORT_WHY_UAS_ONLY_FORCED, XHCI_XPORT_WHY_UAS_ONLY_FORCED,
             "why UAS only forced");
    CHECK_EQ(TR_XPORT_WHY_BOT_FORCED, XHCI_XPORT_WHY_BOT_FORCED, "why forced");
    CHECK_EQ(TR_XPORT_WHY_BOT_NO_STREAMS, XHCI_XPORT_WHY_BOT_NO_STREAMS,
             "why no streams");
    CHECK_EQ(TR_XPORT_WHY_NO_STREAMS, XHCI_XPORT_WHY_NO_STREAMS, "why none");

    /* The part at every level, zero for the caller to fill. */
    for (lv = 0; lv <= 4; lv++) {
        in_base(&in, lv, "a=00000001\r\n", 0);
        in.DriverVersion = 0x03030000UL;
        in.XportRecords = 2;
        CHECK_EQ(XhciQryAnswer(out, sizeof(out), &in, &info), XHQ_ANS_WHOLE,
                 "answers with records");
        CHECK_EQ(get32(out, XHQ_O_DRIVER_VERSION), 0x03030000UL, "version");
        CHECK_EQ(get32(out, XHQ_O_XPORT_BYTES), 80, "two records");
        CHECK_EQ(get32(out, XHQ_O_XPORT_RECORD), 40, "of 40 bytes");
        at = get32(out, XHQ_O_XPORT_OFFSET);
        CHECK_EQ(at + 80, info, "the part last");
        CHECK_EQ(get32(out, at), 0, "written zero");
    }
    in.XportRecords = 40;
    (void)XhciQryAnswer(out, sizeof(out), &in, &info);
    CHECK_EQ(get32(out, XHQ_O_XPORT_BYTES), XHQ_XPORT_MAX * 40, "held to 32");
    CHECK(info <= XHQ_MAX_BYTES, "within the bound");

    /* The reader: a UAS drive at root 3 and a forced stick behind a hub. */
    in_base(&in, 2, "a=00000001\r\n", 0);
    in.DriverVersion = 0x03030000UL;
    in.XportRecords = 2;
    (void)XhciQryAnswer(out, sizeof(out), &in, &info);
    at = get32(out, XHQ_O_XPORT_OFFSET);
    put32(out, at + XHQ_X_SLOT, 2);
    put32(out, at + XHQ_X_PORT, 3);
    put32(out, at + XHQ_X_ROUTE, 0);
    put32(out, at + XHQ_X_IFACE, 0xFF);
    put32(out, at + XHQ_X_TRANSPORT, XHCI_XPORT_UAS);
    put32(out, at + XHQ_X_WHY, XHCI_XPORT_WHY_UAS);
    put32(out, at + XHQ_X_TRIPLE, 0x620608);
    put32(out, at + XHQ_X_COUNT, 1);
    at += 40;
    put32(out, at + XHQ_X_SLOT, 5);
    put32(out, at + XHQ_X_PORT, 20);
    put32(out, at + XHQ_X_ROUTE, 0x21);
    put32(out, at + XHQ_X_IFACE, 1);
    put32(out, at + XHQ_X_TRANSPORT, XHCI_XPORT_BOT);
    put32(out, at + XHQ_X_WHY, XHCI_XPORT_WHY_BOT_FORCED);
    put32(out, at + XHQ_X_INPUTS, XHCI_XPORT_F_FORCE_BOT);
    put32(out, at + XHQ_X_TRIPLE, 0x500608);
    put32(out, at + XHQ_X_COUNT, 3);
    CHECK_EQ(tr_xhq_parse(out, info, &x), 1, "version 2 parsed");
    CHECK_EQ(x.Version, XHQ_VERSION, "its version");
    CHECK_EQ(x.DriverVersion, 0x03030000UL, "the driver's");
    CHECK_EQ(x.XportCount, 2, "two records");
    CHECK_EQ(tr_xhq_xport_find(&x, 3, 0, &r), 1, "root 3");
    CHECK_EQ(r.Slot, 2, "its slot");
    tr_xport_text(&r, line, sizeof(line));
    CHECK(strcmp(line, "UAS (the device offers Bulk-Only and UAS; UAS chosen)")
              == 0, "UAS chosen");
    CHECK(tr_xport_stale(&r, "USBSTOR"), "UAS bound to usbstor: stale");
    CHECK(tr_xport_stale(&r, "usbstor"), "98's usbstor");
    CHECK(tr_xport_stale(&r, "USBSTOR.SYS"), "with .sys");
    CHECK(!tr_xport_stale(&r, "xhciuas"), "the UAS driver");
    CHECK(!tr_xport_stale(&r, "uaspstor"), "Windows 8's UAS driver");
    CHECK(!tr_xport_stale(&r, ""), "no driver");
    CHECK(!tr_xport_stale(&r, NULL), "no devnode");
    CHECK(!tr_xport_stale(&r, "usbstor2"), "another name");
    CHECK_EQ(tr_xhq_xport_find(&x, 20, 0x21, &r), 1, "root 20, behind a hub");
    tr_xport_text(&r, line, sizeof(line));
    CHECK(strcmp(line, "Bulk-Only (interface 1) (the device offers UAS too; "
                       "XhciForceBulkOnly is set on this controller); and 2 "
                       "more storage interfaces") == 0, "forced, composite");
    CHECK(!tr_xport_stale(&r, "USBSTOR"), "Bulk-Only on usbstor is right");
    CHECK_EQ(tr_xhq_xport_find(&x, 3, 1, &r), 0, "no such route");
    CHECK_EQ(tr_xhq_xport_find(&x, 4, 0, &r), 0, "no such port");

    /* Each why's words. */
    memset(&r, 0, sizeof(r));
    r.Iface = 0xFF;
    r.Count = 1;
    r.Why = XHCI_XPORT_WHY_UAS_ONLY;
    tr_xport_text(&r, line, sizeof(line));
    CHECK(strcmp(line, "UAS (the only transport the device offers)") == 0,
          "UAS only");
    r.Why = XHCI_XPORT_WHY_UAS_ONLY_FORCED;
    tr_xport_text(&r, line, sizeof(line));
    CHECK(strcmp(line, "UAS (the only transport the device offers; "
                       "XhciForceBulkOnly does not apply to it)") == 0,
          "UAS only, forced");
    r.Why = XHCI_XPORT_WHY_BOT_NO_STREAMS;
    tr_xport_text(&r, line, sizeof(line));
    CHECK(strcmp(line, "Bulk-Only (the device offers UAS too; this controller "
                       "cannot stream at SuperSpeed)") == 0, "no streams");
    r.Why = XHCI_XPORT_WHY_NO_STREAMS;
    tr_xport_text(&r, line, sizeof(line));
    CHECK(strcmp(line, "none (the device offers UAS alone, at SuperSpeed on a "
                       "controller that cannot stream)") == 0, "refused");
    r.Why = XHCI_XPORT_WHY_NOT_UAS;
    r.Triple = 0x500608;
    tr_xport_text(&r, line, sizeof(line));
    CHECK(strcmp(line, "Bulk-Only (the device offers no UAS setting)") == 0,
          "Bulk-Only alone");
    r.Triple = 0x010208;
    tr_xport_text(&r, line, sizeof(line));
    CHECK(strcmp(line, "class 08, subclass 02, protocol 01 (no UAS setting)")
              == 0, "another triple");

    /* The header's driver word. */
    in_base(&in, 2, "", 0);
    in.DriverVersion = 0x03030000UL;
    (void)XhciQryAnswer(out, 88, &in, &info);
    CHECK_EQ(tr_xhq_driver(out, 88, &packed), 1, "version 3 says");
    CHECK_EQ(packed, 0x03030000UL, "what");
    CHECK_EQ(tr_xhq_driver(out, 64, &packed), -1, "cut to 64: not read");
    CHECK_EQ(tr_xhq_driver(out, 80, &packed), -1, "version 3 cut to 80");
    put32(out, XHQ_O_VERSION, 2);
    CHECK_EQ(tr_xhq_driver(out, 80, &packed), 1, "version 2 of 80 says");
    CHECK_EQ(packed, 0x03030000UL, "what version 2 says");
    CHECK_EQ(tr_xhq_driver(out, 79, &packed), -1, "version 2 cut to 79");
    put32(out, XHQ_O_VERSION, 1);
    CHECK_EQ(tr_xhq_driver(out, 64, &packed), 0, "version 1: before 3.3.0.0");
    CHECK_EQ(packed, 0, "0 said");
    put32(out, XHQ_O_MAGIC, 0);
    CHECK_EQ(tr_xhq_driver(out, 80, &packed), -1, "not this driver");

    /* A version 1 answer, as 3.2.0.0 sends it: read, with no records. */
    memset(out, 0, 2048);
    put32(out, XHQ_O_MAGIC, XHQ_MAGIC);
    put32(out, XHQ_O_VERSION, 1);
    put32(out, XHQ_O_BYTES, 64 + 8);
    put32(out, XHQ_O_CTR_OFFSET, 64);
    put32(out, XHQ_O_CTR_BYTES, 8);
    put32(out, XHQ_O_LOG_OFFSET, 72);
    put32(out, XHQ_O_PCI_OFFSET, 72);
    CHECK_EQ(tr_xhq_parse(out, 72, &x), 1, "version 1 parsed");
    CHECK_EQ(x.Version, 1, "version 1");
    CHECK(x.Xport == NULL && x.XportCount == 0, "no records");
    CHECK_EQ(tr_xhq_xport_find(&x, 3, 0, &r), 0, "none found");

    /* Bad transport parts. */
    in_base(&in, 2, "a=00000001\r\n", 0);
    in.XportRecords = 2;
    (void)XhciQryAnswer(out, sizeof(out), &in, &info);
    put32(out, XHQ_O_XPORT_RECORD, 36);
    CHECK_EQ(tr_xhq_parse(out, info, &x), 0, "a record under 40");
    put32(out, XHQ_O_XPORT_RECORD, 48);
    CHECK_EQ(tr_xhq_parse(out, info, &x), 0, "not whole records");
    put32(out, XHQ_O_XPORT_RECORD, 40);
    put32(out, XHQ_O_XPORT_BYTES, 120);
    CHECK_EQ(tr_xhq_parse(out, info, &x), 0, "past the answer");
    put32(out, XHQ_O_XPORT_BYTES, 80);
    put32(out, XHQ_O_XPORT_RECORD, 80);
    CHECK_EQ(tr_xhq_parse(out, info, &x), 1, "a later, larger record");
    CHECK_EQ(x.XportCount, 1, "walked by its size");
}

/* Roadmap-usb4 44.80: version 3's ports part, filled by the caller, and
 * the reader of it; a version 2 answer still read. */
static void test_v3(void)
{
    XHCI_QRY_IN in;
    ULONG info;
    ULONG at;
    ULONG lv;
    TR_XHQ x;

    CHECK_EQ(XHQ_PT_NONE, XHCI_PORT_TOP_NONE, "pt: none");
    CHECK_EQ(XHQ_PT_USB2, XHCI_PORT_TOP_USB2, "pt: USB 2.0");
    CHECK_EQ(XHQ_PT_SS, XHCI_PORT_TOP_SS, "pt: SuperSpeed");
    CHECK_EQ(XHQ_PT_SSP, XHCI_PORT_TOP_SSP, "pt: SuperSpeedPlus");
    CHECK(XHQ_PORTS_MAX >= XHCI_MAX_ROOT_PORTS, "a byte a root port");

    for (lv = 0; lv <= 4; lv++) {
        in_base(&in, lv, "a=00000001\r\n", 0);
        in.XportRecords = 1;
        in.Ports = 4;
        CHECK_EQ(XhciQryAnswer(out, sizeof(out), &in, &info), XHQ_ANS_WHOLE,
                 "answers with ports");
        CHECK_EQ(get32(out, XHQ_O_PORTS_BYTES), 4, "four ports at every level");
        at = get32(out, XHQ_O_PORTS_OFFSET);
        CHECK_EQ(at, get32(out, XHQ_O_XPORT_OFFSET) + 40,
                 "after the transport part");
        CHECK_EQ(at + 4, info, "the part last");
        CHECK(out[at] == 0 && out[at + 3] == 0, "written zero");
    }
    in.Ports = 300;
    (void)XhciQryAnswer(out, sizeof(out), &in, &info);
    CHECK_EQ(get32(out, XHQ_O_PORTS_BYTES), XHQ_PORTS_MAX, "held to 255");
    CHECK(info <= XHQ_MAX_BYTES, "within the bound");

    /* The reader. */
    in_base(&in, 2, "a=00000001\r\n", 0);
    in.Ports = 4;
    (void)XhciQryAnswer(out, sizeof(out), &in, &info);
    at = get32(out, XHQ_O_PORTS_OFFSET);
    out[at] = XHQ_PT_USB2;
    out[at + 1] = XHQ_PT_USB2;
    out[at + 2] = XHQ_PT_SS;
    out[at + 3] = XHQ_PT_SSP;
    CHECK_EQ(tr_xhq_parse(out, info, &x), 1, "version 3 parsed");
    CHECK_EQ(x.Version, 3, "version 3");
    CHECK_EQ(x.PortCount, 4, "four ports");
    CHECK_EQ(tr_xhq_port_top(&x, 1), XHQ_PT_USB2, "port 1");
    CHECK_EQ(tr_xhq_port_top(&x, 3), XHQ_PT_SS, "port 3");
    CHECK_EQ(tr_xhq_port_top(&x, 4), XHQ_PT_SSP, "port 4");
    CHECK_EQ(tr_xhq_port_top(&x, 5), -1, "past the part");
    CHECK_EQ(tr_xhq_port_top(&x, 0), -1, "port 0");
    CHECK_EQ(tr_xhq_port_top(NULL, 1), -1, "no answer");
    put32(out, XHQ_O_PORTS_BYTES, 5);
    CHECK_EQ(tr_xhq_parse(out, info, &x), 0, "ports past the answer");
    put32(out, XHQ_O_PORTS_BYTES, 4);
    put32(out, XHQ_O_PORTS_OFFSET, 40);
    CHECK_EQ(tr_xhq_parse(out, info, &x), 0, "ports in the header");
    put32(out, XHQ_O_PORTS_OFFSET, at);

    /* A stopped controller: no ports. */
    in_base(&in, 2, "", 0);
    (void)XhciQryAnswer(out, sizeof(out), &in, &info);
    CHECK_EQ(tr_xhq_parse(out, info, &x), 1, "no ports parsed");
    CHECK(x.Ports == NULL && x.PortCount == 0, "none");
    CHECK_EQ(tr_xhq_port_top(&x, 1), -1, "port 1 not served");

    /* A version 2 answer (the test cuts before 44.80): read, no ports. */
    in_base(&in, 2, "", 0);
    in.XportRecords = 1;
    (void)XhciQryAnswer(out, sizeof(out), &in, &info);
    /* Shape it as version 2 did: the 80-byte header, the parts moved up. */
    memmove(out + 80, out + 88, info - 88);
    info -= 8;
    put32(out, XHQ_O_VERSION, 2);
    put32(out, XHQ_O_BYTES, info);
    put32(out, XHQ_O_CTR_OFFSET, get32(out, XHQ_O_CTR_OFFSET) - 8);
    put32(out, XHQ_O_LOG_OFFSET, get32(out, XHQ_O_LOG_OFFSET) - 8);
    put32(out, XHQ_O_PCI_OFFSET, get32(out, XHQ_O_PCI_OFFSET) - 8);
    put32(out, XHQ_O_XPORT_OFFSET, get32(out, XHQ_O_XPORT_OFFSET) - 8);
    CHECK_EQ(tr_xhq_parse(out, info, &x), 1, "version 2 parsed");
    CHECK_EQ(x.Version, 2, "version 2");
    CHECK_EQ(x.XportCount, 1, "its record");
    CHECK(x.Ports == NULL, "no ports part");
    CHECK_EQ(tr_xhq_port_top(&x, 1), -1, "not served by version 2");
    put32(out, XHQ_O_DRIVER_VERSION, 0x03030000UL);
    CHECK_EQ(tr_xhq_parse(out, info, &x), 1, "version 2 parsed again");
    {
        TR_TEXT t;
        tr_text_init(&t, text, sizeof(text));
        tr_xhq_log_text(&t, &x, 1);
        CHECK(strstr(text, "Version:    3.3.0.0") != NULL,
              "version 2's driver version in the log text");
    }
}

int main(void)
{
    test_layout();
    test_v2();
    test_v3();
    test_failed();
    test_levels();
    test_ring();
    test_pci_sizing();
    test_reader();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
