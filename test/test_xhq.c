/*
 * test_xhq.c - xhci98.sys's query request (src\xhci_qry.c, design record
 * 13 section 8.13, design record 25 revision 12): the answer composer at
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
#include "../src/xhci_log.h"
#include "../xhcitr/tr_log.h"
#include "../xhcitr/tr_pci.h"
#include "test_harness.h"

static UCHAR ring[16384];
static ULONG ctr[246];
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
    in->CounterWords = 246;
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
    CHECK_EQ(XHQ_O_FLAVOUR + 4, XHQ_HDR_BYTES, "the header is full");
    CHECK(XHQ_MAX_BYTES <= XHQ_ASK_BYTES, "one ask holds the largest");
    CHECK(246 * 4 <= XHQ_CTR_MAX, "the counter block fits");
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
    CHECK_EQ(get32(out, XHQ_O_CTR_BYTES), 246 * 4, "counters at 1");
    CHECK_EQ(get32(out, XHQ_HDR_BYTES + 12), 7, "counter word 3");
    CHECK_EQ(get32(out, XHQ_O_LOG_BYTES), 0, "no log at 1");
    CHECK(get32(out, XHQ_O_FLAGS) & XHQ_F_COUNTERS, "counters flag");

    /* Level 2 and 3: the ring as text. */
    in_base(&in, 2, s, 0);
    CHECK_EQ(XhciQryAnswer(out, sizeof(out), &in, &info), XHQ_ANS_WHOLE, "2");
    want = (ULONG)strlen(s);
    CHECK_EQ(get32(out, XHQ_O_LOG_BYTES), want, "the log at 2");
    CHECK(memcmp(out + get32(out, XHQ_O_LOG_OFFSET), s, want) == 0, "as text");
    CHECK_EQ(info, XHQ_HDR_BYTES + 984 + want + 256, "the whole");
    CHECK(get32(out, XHQ_O_FLAGS) & XHQ_F_RECORDING, "recording");
    in_base(&in, 3, s, 0);
    (void)XhciQryAnswer(out, sizeof(out), &in, &info);
    CHECK_EQ(get32(out, XHQ_O_LOG_BYTES), want, "the log at 3");

    /* Level 4: withheld. */
    in_base(&in, 4, s, 0);
    (void)XhciQryAnswer(out, sizeof(out), &in, &info);
    CHECK_EQ(get32(out, XHQ_O_LOG_BYTES), 0, "no log at 4");
    CHECK(get32(out, XHQ_O_FLAGS) & XHQ_F_WITHHELD, "withheld");
    CHECK_EQ(get32(out, XHQ_O_CTR_BYTES), 984, "counters at 4");
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
    CHECK_EQ(XhciQryAnswer(out, 64, &in, &info), XHQ_ANS_HEADER, "64");
    CHECK_EQ(info, 64, "the header");
    CHECK_EQ(get32(out, XHQ_O_BYTES), 64 + 984 + 12 + 256, "the size needed");
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
    CHECK_EQ(x.CtrBytes, 984, "counters");
    CHECK_EQ(x.PciBytes, 256, "pci");
    tr_text_init(&t, text, sizeof(text));
    tr_xhq_log_text(&t, &x, 1);
    CHECK(strstr(text, "Recording:  on (XhciLogVerbosity 2)") != NULL, "on");
    CHECK(strstr(text, "hcd.start=00000001") != NULL, "the log");
    CHECK(strstr(text, "(1 of 246 nonzero.)") != NULL, "counters named");
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
    CHECK(strstr(text, "SNAPON.BAT") != NULL, "advice");
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
    CHECK_EQ(tr_xhq_parse(out, 64, &x), -1, "header alone");
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
    put32(out, XHQ_O_LOG_OFFSET, 64 + 984);
    put32(out, XHQ_O_VERSION, 2);
    CHECK_EQ(tr_xhq_parse(out, info, &x), 0, "version");
    put32(out, XHQ_O_VERSION, 1);
    put32(out, XHQ_O_MAGIC, 0);
    CHECK_EQ(tr_xhq_parse(out, info, &x), 0, "magic");
    CHECK_EQ(tr_xhq_parse(NULL, 0, &x), 0, "nothing");
}

int main(void)
{
    test_layout();
    test_levels();
    test_ring();
    test_pci_sizing();
    test_reader();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
