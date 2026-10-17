/*
 * test_trlog.c - XHCITR's "Driver log" readers (xhcitr\tr_log.c; design
 * record 25 sections 3.5, 4.5 and 4.6, revision 9): xhcicm.sys's log
 * answer as the driver's builder makes it (src\cm\cm_query.c, linked) -
 * the parse, its bounds, the text with recording on and off and the
 * advice; xhci98.sys's pass-through window - the request's bytes, the
 * header, the channel off, the ring decoded with its own window's header,
 * a full ring's cut, level 4 withheld; the HCD region's counters, and
 * their names held to src\xhci_counters.h field by field.
 */

#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include "../src/xhci_counters.h"
#include "../src/cm/cm_qryb.h"
#include "../xhcitr/tr_log.h"
#include "test_harness.h"

static unsigned char buf[TR_SNAP_BUFFER_BYTES];
static unsigned char ans[CM_QL_ASK_BYTES];
static char text[65536];
static UCHAR ring[CM_QL_LOG_MAX];

static TR_U32 get32(const unsigned char *b, TR_U32 o)
{
    return (TR_U32)b[o] | ((TR_U32)b[o + 1] << 8) | ((TR_U32)b[o + 2] << 16) |
           ((TR_U32)b[o + 3] << 24);
}

static void put32(unsigned char *b, TR_U32 o, TR_U32 v)
{
    b[o] = (unsigned char)v;
    b[o + 1] = (unsigned char)(v >> 8);
    b[o + 2] = (unsigned char)(v >> 16);
    b[o + 3] = (unsigned char)(v >> 24);
}

/* ---- the counter names against the driver's structure ------------------ */

static const size_t hcOffsets[] = {
#define TR_HC(n) offsetof(XHCIHC_COUNTERS, n),
#include "../xhcitr/tr_hcctr.h"
#undef TR_HC
    0
};

static void test_names(void)
{
    TR_U32 i;
    TR_U32 n = tr_hc_count();

    CHECK_EQ(n * 4, sizeof(XHCIHC_COUNTERS), "one name per counter word");
    CHECK_EQ(n, sizeof(hcOffsets) / sizeof(hcOffsets[0]) - 1, "the table");
    for (i = 0; i < n; i++) {
        if (hcOffsets[i] != i * 4) {
            printf("word %lu: %s\n", (unsigned long)i, tr_hc_name(i));
        }
        CHECK_EQ(hcOffsets[i], i * 4, "each name at its own word");
    }
    CHECK(strcmp(tr_hc_name(0), "SlotsEnabled") == 0, "the first");
    CHECK(tr_hc_name(n) == NULL, "none past the table");
}

/* ---- xhcicm ------------------------------------------------------------- */

static size_t cm_answer(ULONG flags, const char *ctr, const char *log,
                        ULONG verbosity)
{
    CM_QLOG_IN in;
    ULONG info = 0;
    ULONG head = 0;
    ULONG used = 0;

    memset(&in, 0, sizeof(in));
    while (*log != 0) {
        ring[head++] = (UCHAR)*log++;
        used++;
    }
    in.Flags = flags;
    in.Verbosity = verbosity;
    in.VerbosityRead = verbosity;
    in.EpochLo = 0x11111111UL;
    in.EpochHi = 0x01DC0000UL;
    in.Ctr = (const UCHAR *)ctr;
    in.CtrBytes = (ULONG)strlen(ctr);
    in.Ring = ring;
    in.RingBytes = CM_QL_LOG_MAX;
    in.Head = head;
    in.Used = used;
    in.Appends = 3;
    in.Pass = 9;
    (VOID)CmQueryLogAnswer(ans, sizeof(ans), &in, &info);
    CmQueryLogSetAge(ans, info, 9, 250);
    return info;
}

static void test_cm(void)
{
    TR_QLOG l;
    TR_TEXT t;
    size_t n;
    static const char *key =
        "SYSTEM\\CurrentControlSet\\Control\\Class\\{4D36E97D-E325-11CE-BFC1-08002BE10318}\\0012";

    n = cm_answer(CM_QL_F_RECORDING,
                  "cm.passes=0000000A\r\ncm.polls=00000000\r\n"
                  "cm.reenum.child.attached=00000002\r\n",
                  "cm.start=00000001\r\ncm.live.bar-changed=E0000004\r\n", 2);
    CHECK_EQ(tr_qlog_parse(ans, n, &l), 1, "a whole answer");
    CHECK_EQ(l.Pass, 9, "pass");
    CHECK_EQ(l.AgeMs, 250, "age");
    CHECK_EQ(l.EpochLo, 0x11111111UL, "epoch");
    tr_text_init(&t, text, sizeof(text));
    tr_qlog_text(&t, &l, key, 1);
    CHECK(strstr(text, "Recording:  on (XhciCmLogVerbosity 2)") != NULL, "on");
    CHECK(strstr(text, "To record") == NULL, "no advice when on");
    CHECK(strstr(text, "cm.passes                            10") != NULL,
          "a counter in decimal");
    CHECK(strstr(text, "cm.polls ") == NULL, "a zero counter not shown");
    CHECK(strstr(text, "cm.reenum.child.attached") != NULL, "the child");
    CHECK(strstr(text, "(2 of 3 nonzero.)") != NULL, "the count");
    CHECK(strstr(text, "pass 9, 250 ms ago") != NULL, "the pass and age");
    CHECK(strstr(text, "cm.start=00000001\r\n") != NULL, "the log, CR LF");
    CHECK(strstr(text, "cm.live.bar-changed=--------") != NULL, "withheld");
    CHECK(strstr(text, "E0000004") == NULL, "no address anywhere");
    CHECK(strstr(text, "are addresses, withheld") != NULL, "said so");

    /* Recording off: the advice with the key. */
    n = cm_answer(0, "cm.passes=00000001\r\n", "", 0);
    CHECK_EQ(tr_qlog_parse(ans, n, &l), 1, "off: whole");
    tr_text_init(&t, text, sizeof(text));
    tr_qlog_text(&t, &l, key, 1);
    CHECK(strstr(text, "Recording:  off (XhciCmLogVerbosity 0") != NULL, "off");
    CHECK(strstr(text, "XhciCmLogVerbosity to\r\n  2 (the default)") != NULL, "the value");
    CHECK(strstr(text, "(as an administrator)") != NULL, "NT: admin");
    CHECK(strstr(text, "HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control"
                       "\\Class\\{4D36E97D") != NULL, "the whole key");
    CHECK(strstr(text, "restart Windows") != NULL, "restart");
    CHECK(strstr(text, "(empty)") != NULL, "an empty log");
    tr_text_init(&t, text, sizeof(text));
    tr_qlog_text(&t, &l, "System\\CurrentControlSet\\Services\\Class\\System\\0005", 0);
    CHECK(strstr(text, "administrator") == NULL, "98: no admin");
    CHECK(strstr(text, "Services\\Class\\System\\0005") != NULL, "98: key");

    /* Refused. */
    n = cm_answer(CM_QL_F_REFUSED, "", "", 0);
    put32(ans, CM_QL_O_VERBOSITY_READ, 9);
    CHECK_EQ(tr_qlog_parse(ans, n, &l), 1, "refused: whole");
    tr_text_init(&t, text, sizeof(text));
    tr_qlog_text(&t, &l, key, 1);
    CHECK(strstr(text, "XhciCmLogVerbosity is 9, which the driver refused") !=
          NULL, "refused said");

    /* Sizing and bounds. */
    n = cm_answer(CM_QL_F_RECORDING, "a=00000001\r\n", "b=00000002\r\n", 2);
    CHECK_EQ(tr_qlog_parse(ans, 63, &l), 0, "short of a header");
    CHECK_EQ(tr_qlog_parse(ans, n - 1, &l), -1, "ask again");
    CHECK_EQ(l.Bytes, n, "with this size");
    put32(ans, CM_QL_O_LOG_BYTES, 13);
    CHECK_EQ(tr_qlog_parse(ans, n, &l), 0, "log past the answer");
    put32(ans, CM_QL_O_LOG_BYTES, 12);
    put32(ans, CM_QL_O_CTR_OFFSET, 0xFFFFFFF0UL);
    CHECK_EQ(tr_qlog_parse(ans, n, &l), 0, "offset that would wrap");
    put32(ans, CM_QL_O_CTR_OFFSET, 64);
    put32(ans, CM_QL_O_BYTES, CM_QL_ASK_BYTES + 1);
    CHECK_EQ(tr_qlog_parse(ans, sizeof(ans), &l), 0, "too large");
    put32(ans, CM_QL_O_BYTES, (TR_U32)n);
    put32(ans, CM_QL_O_VERSION, 2);
    CHECK_EQ(tr_qlog_parse(ans, n, &l), 0, "another version");
    put32(ans, CM_QL_O_VERSION, 1);
    put32(ans, CM_QL_O_MAGIC, CM_Q_MAGIC);
    CHECK_EQ(tr_qlog_parse(ans, n, &l), 0, "the query's magic");
}

/* ---- xhci98 ------------------------------------------------------------- */

static void test_request(void)
{
    tr_snap_request(buf, TR_SNAP_REGION_EXT, 0x1234);
    CHECK_EQ(get32(buf, 0), 3, "PASS_THRU");
    CHECK_EQ(get32(buf, 8), TR_SNAP_BUFFER_BYTES, "RequestBufferLength");
    CHECK_EQ(get32(buf, 0x10), 0x34F57942UL, "the GUID");
    CHECK_EQ(get32(buf, 0x1C), 0x20BC448EUL, "its last word");
    CHECK_EQ(get32(buf, 0x20), 0xF000UL, "ParameterLength");
    CHECK_EQ(get32(buf, 0x24), TR_SNAP_REQ_SIGNATURE, "XSNQ");
    CHECK_EQ(get32(buf, 0x28), 0, "region");
    CHECK_EQ(get32(buf, 0x2C), 0x1234, "offset");
    CHECK_EQ(TR_SNAP_HDR_BYTES, 38 * 4, "schema 5's header");
}

/* A window as the door answers it: header and payload. */
static size_t window(TR_U32 region, TR_U32 offset, TR_U32 level,
                     TR_U32 ringOffset, TR_U32 ringBytes, TR_U32 head,
                     TR_U32 used, const unsigned char *payload, TR_U32 pn)
{
    unsigned char *h = buf + TR_UU_PARAMETERS;

    memset(buf, 0, sizeof(buf));
    put32(h, 0, TR_SNAP_SIGNATURE);
    put32(h, 4, TR_SNAP_SCHEMA);
    put32(h, 8, TR_SNAP_HDR_BYTES);
    put32(h, 16, region);
    put32(h, 20, offset);
    put32(h, 24, 0x10000);
    put32(h, 28, pn);
    put32(h, 48, 1);
    put32(h, 52, level);
    put32(h, 56, level);
    put32(h, 72, ringOffset);
    put32(h, 76, ringBytes);
    put32(h, 80, head);
    put32(h, 84, used);
    memcpy(h + TR_SNAP_HDR_BYTES, payload, pn);
    return TR_UU_PARAMETERS + TR_SNAP_HDR_BYTES + pn;
}

static unsigned char pay[4096];

static void test_window(void)
{
    TR_SNAPW w;
    TR_TEXT t;
    size_t n;
    const char *recs = "slot.addressed=00010203\r\nenum.port=00000001\r\n";
    TR_U32 len = (TR_U32)strlen(recs);
    TR_U32 i;

    memset(buf, 0, sizeof(buf));
    put32(buf, TR_UU_STATUS, 6);
    CHECK_EQ(tr_snap_window(buf, TR_UU_PARAMETERS, &w), TR_SW_OFF, "off");
    put32(buf, TR_UU_STATUS, 2);
    CHECK_EQ(tr_snap_window(buf, TR_UU_PARAMETERS, &w), TR_SW_REFUSED,
             "another status");
    CHECK_EQ(tr_snap_window(buf, 8, &w), TR_SW_BAD, "too short");

    /* A ring of 4096 at +0x500, written from 0: its window at 0x500. */
    memset(pay, 0, sizeof(pay));
    memcpy(pay, recs, len);
    n = window(0, 0x500, 2, 0x500, 4096, len, len, pay, 4096);
    CHECK_EQ(tr_snap_window(buf, n, &w), TR_SW_OK, "a window");
    CHECK_EQ(w.RingUsed, len, "used");
    tr_text_init(&t, text, sizeof(text));
    tr_hcd_head_text(&t, &w, 1);
    CHECK(strstr(text, "Build:      release") != NULL, "flavour");
    CHECK(strstr(text, "on (XhciLogVerbosity 2)") != NULL, "on");
    CHECK_EQ(tr_hcd_ring_text(&t, &w, 0x500), TR_RING_SHOWN, "shown");
    CHECK(strstr(text, "slot.addressed=00010203\r\nenum.port=00000001\r\n") !=
          NULL, "the records");
    CHECK(strstr(text, "(45 bytes)") != NULL, "its length");
    CHECK_EQ(tr_snap_window(buf, n - 1, &w), TR_SW_BAD, "short of its payload");

    /* The window must be the ring's own. */
    n = window(0, 0x400, 2, 0x500, 4096, len, len, pay, 4096);
    (VOID)tr_snap_window(buf, n, &w);
    CHECK_EQ(tr_hcd_ring_text(&t, &w, 0x500), TR_RING_BAD, "another offset");
    n = window(0, 0x500, 2, 0x600, 4096, len, len, pay, 4096);
    (VOID)tr_snap_window(buf, n, &w);
    CHECK_EQ(tr_hcd_ring_text(&t, &w, 0x500), TR_RING_BAD, "the ring moved");
    n = window(0, 0x500, 2, 0x500, 4096, len, len, pay, 4095);
    (VOID)tr_snap_window(buf, n, &w);
    CHECK_EQ(tr_hcd_ring_text(&t, &w, 0x500), TR_RING_BAD, "a short window");
    n = window(0, 0x500, 2, 0x500, 3000, len, len, pay, 3000);
    (VOID)tr_snap_window(buf, n, &w);
    CHECK_EQ(tr_hcd_ring_text(&t, &w, 0x500), TR_RING_BAD, "not a power of 2");
    n = window(0, 0x500, 2, 0x500, 4096, len, 4097, pay, 4096);
    (VOID)tr_snap_window(buf, n, &w);
    CHECK_EQ(tr_hcd_ring_text(&t, &w, 0x500), TR_RING_BAD, "used past size");
    n = window(3, 0x500, 2, 0x500, 4096, len, len, pay, 4096);
    (VOID)tr_snap_window(buf, n, &w);
    CHECK_EQ(tr_hcd_ring_text(&t, &w, 0x500), TR_RING_BAD, "another region");

    /* Level 4: withheld, the head says why; level 1: off and the advice. */
    n = window(0, 0x500, 4, 0x500, 4096, len, len, pay, 4096);
    (VOID)tr_snap_window(buf, n, &w);
    tr_text_init(&t, text, sizeof(text));
    tr_hcd_head_text(&t, &w, 1);
    CHECK_EQ(tr_hcd_ring_text(&t, &w, 0x500), TR_RING_WITHHELD, "level 4");
    CHECK(strstr(text, "slot.addressed") == NULL, "no record shown");
    CHECK(strstr(text, "does not show the log") != NULL, "said so");
    n = window(0, 0x500, 1, 0x500, 4096, 0, 0, pay, 4096);
    (VOID)tr_snap_window(buf, n, &w);
    tr_text_init(&t, text, sizeof(text));
    tr_hcd_head_text(&t, &w, 1);
    CHECK_EQ(tr_hcd_ring_text(&t, &w, 0x500), TR_RING_OFF, "level 1");
    CHECK(strstr(text, "off (XhciLogVerbosity 1: counters only)") != NULL,
          "off");
    CHECK(strstr(text, "SNAPON.BAT") != NULL, "the advice");
    CHECK(strstr(text, "Run as administrator") != NULL, "NT: admin");
    tr_text_init(&t, text, sizeof(text));
    tr_hcd_off_text(&t, 0);
    CHECK(strstr(text, "XhciLogVerbosity 0") != NULL, "level 0");
    CHECK(strstr(text, "administrator") == NULL, "98: no admin");

    /* A full ring that wrapped: from the first whole record, bytes outside
     * printable ASCII as '.'. */
    for (i = 0; i < 4096; i++) {
        pay[i] = (unsigned char)"abc=0000000\x01\r\n"[i % 14];
    }
    n = window(0, 0x500, 3, 0x500, 4096, 7, 4096, pay, 4096);
    (VOID)tr_snap_window(buf, n, &w);
    tr_text_init(&t, text, sizeof(text));
    CHECK_EQ(tr_hcd_ring_text(&t, &w, 0x500), TR_RING_SHOWN, "full");
    CHECK(strncmp(text, "\r\n  Log, oldest record first (4096 bytes):\r\n"
                        "abc=0000000.\r\n", 57) == 0, "cut to a record");
}

static unsigned char img[2048];

static void test_counters(void)
{
    TR_TEXT t;
    TR_U32 at = 32 + 2 * 64;

    memset(img, 0, sizeof(img));
    put32(img, 0, 1);
    put32(img, 4, 32);
    put32(img, 8, 2);
    put32(img, 12, 64);
    put32(img, 16, 32);
    put32(img, 20, 250);
    put32(img, 24, at);
    put32(img, at + 0, 5);              /* SlotsEnabled */
    put32(img, at + 4 * 3, 7);          /* PortSpeedFull */
    put32(img, at + 4 * 249, 9);        /* past the table */
    tr_text_init(&t, text, sizeof(text));
    CHECK_EQ(tr_hcd_counters_text(&t, img, at + 250 * 4), 1, "decoded");
    CHECK(strstr(text, "SlotsEnabled                         5") != NULL,
          "named");
    CHECK(strstr(text, "PortSpeedFull") != NULL, "named 3");
    CHECK(strstr(text, "DevicesAddressed") == NULL, "zero not shown");
    CHECK(strstr(text, "counter 249") != NULL, "past the table by number");
    CHECK(strstr(text, "(3 of 250 nonzero.)") != NULL, "count");
    put32(img, 20, 251);
    tr_text_init(&t, text, sizeof(text));
    CHECK_EQ(tr_hcd_counters_text(&t, img, at + 250 * 4), 0,
             "counters past the image");
    put32(img, 20, 250);
    put32(img, 0, 2);
    tr_text_init(&t, text, sizeof(text));
    CHECK_EQ(tr_hcd_counters_text(&t, img, at + 250 * 4), 0, "version 2");
    CHECK(strstr(text, "does not read") != NULL, "said so");
    CHECK_EQ(tr_hcd_counters_text(&t, NULL, 0), 0, "none");
}

int main(void)
{
    test_names();
    test_cm();
    test_request();
    test_window();
    test_counters();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
