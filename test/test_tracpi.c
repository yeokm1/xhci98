/*
 * test_tracpi.c - the firmware tables (design record 25 section 21,
 * record 32 section 12.8.14; task 46.21): xhcicm.sys's answer builder
 * (src\cm\cm_qacpi.c) - the body, parts of every size, an offset past
 * the body, the bounds - and XHCITR's reader of it (xhcitr\tr_acpi.c)
 * with bad shapes, parts from another epoch or body; the checksums (a
 * good and a bad table, the root pointer of revision 0 and 2, the FACS);
 * the file names (two of one signature, two signatures written alike);
 * the list with counts; the index within 79 columns; every message.
 *
 * Option B (task 46.21): xhci98.sys's reading (src\xhci_acpi.c) over a
 * modelled memory - the root pointer in the BIOS area, a table crossing a
 * page, a 100 KB DSDT read in pieces no mapping of which passes 64 KB, a
 * table moved since its header was read, a bad checksum, a cap, a short
 * FADT, MCFG windows, an entry that cannot be classified, two MCFGs, no
 * root pointer, the timeout - its answer in parts and its generation; the
 * merge with xhcicm.sys's copies; the 2000 and XP registry walk over a
 * modelled key tree and its checks.
 *
 * Every table here is made up for the test (OEM ID "TESTOE", table ids
 * "TESTTBL1" and the like); no board's bytes.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../src/cm/cm_qryb.h"
#include "../xhcitr/tr_acpi.h"
#include "../src/xhci_acpib.h"
#include "test_harness.h"

static void put32(unsigned char *b, unsigned long o, unsigned long v)
{
    b[o] = (unsigned char)(v & 0xFFUL);
    b[o + 1] = (unsigned char)((v >> 8) & 0xFFUL);
    b[o + 2] = (unsigned char)((v >> 16) & 0xFFUL);
    b[o + 3] = (unsigned char)((v >> 24) & 0xFFUL);
}

static unsigned long get32(const unsigned char *b, unsigned long o)
{
    return (unsigned long)b[o] | ((unsigned long)b[o + 1] << 8) |
           ((unsigned long)b[o + 2] << 16) | ((unsigned long)b[o + 3] << 24);
}

static unsigned long sig(const char *s)
{
    return (unsigned long)(unsigned char)s[0] |
           ((unsigned long)(unsigned char)s[1] << 8) |
           ((unsigned long)(unsigned char)s[2] << 16) |
           ((unsigned long)(unsigned char)s[3] << 24);
}

/* A made-up table: signature, length, revision 2, OEM "TESTOE", table id,
 * a filler pattern, its checksum mended. */
static void make_table(unsigned char *t, const char *s, unsigned long len,
                       const char *tid, unsigned char fill)
{
    unsigned long i;
    unsigned int sum = 0;

    memset(t, 0, len);
    memcpy(t, s, 4);
    put32(t, 4, len);
    t[8] = 2;
    memcpy(t + 10, "TESTOE", 6);
    memcpy(t + 16, tid, strlen(tid) < 8 ? strlen(tid) : 8);
    for (i = 36; i < len; i++) {
        t[i] = (unsigned char)(fill + i);
    }
    for (i = 0; i < len; i++) {
        sum += t[i];
    }
    t[9] = (unsigned char)(0x100 - (sum & 0xFF));
}

static void make_rsdp(unsigned char *p, int rev, unsigned long len)
{
    unsigned long i;
    unsigned int sum = 0;

    memset(p, 0, 64);
    memcpy(p, "RSD PTR ", 8);
    memcpy(p + 9, "TESTOE", 6);
    p[15] = (unsigned char)rev;
    for (i = 0; i < 20; i++) {
        sum += p[i];
    }
    p[8] = (unsigned char)(0x100 - (sum & 0xFF));
    if (rev >= 2) {
        put32(p, 20, len);
        sum = 0;
        for (i = 0; i < len; i++) {
            sum += p[i];
        }
        p[32] = (unsigned char)(0x100 - (sum & 0xFF));
    }
}

static unsigned char rsdp[64];
static unsigned char xsdt[52];
static unsigned char facp[116];
static unsigned char ssdt1[300];
static unsigned char ssdt2[201];
static unsigned char mcfg[60];
static unsigned char body[CM_QA_MAX_BODY];
static unsigned char whole[CM_QA_MAX_BODY];
static unsigned char ans[CM_QA_HDR_BYTES + CM_QA_PART_MAX + 64];
static TR_ACPI a;
static char text[64 * 1024];

static unsigned long build_body(CM_QA_ITEM *it, unsigned long *count)
{
    memset(it, 0, sizeof(CM_QA_ITEM) * 8);
    make_rsdp(rsdp, 2, 36);
    make_table(xsdt, "XSDT", sizeof(xsdt), "TESTTBL0", 1);
    make_table(facp, "FACP", sizeof(facp), "TESTTBL1", 2);
    make_table(ssdt1, "SSDT", sizeof(ssdt1), "TESTTBL2", 3);
    make_table(ssdt2, "SSDT", sizeof(ssdt2), "TESTTBL3", 4);
    make_table(mcfg, "MCFG", sizeof(mcfg), "TESTTBL4", 5);
    it[0].Sig = sig("RSDP");
    it[0].Kind = CM_QA_K_RSDP | (CM_QA_H_WHOLE << 8);
    it[0].Length = 36;
    it[0].Data = rsdp;
    it[0].Held = 36;
    it[1].Sig = sig("XSDT");
    it[1].Kind = CM_QA_K_ROOT | (CM_QA_H_WHOLE << 8);
    it[1].Length = sizeof(xsdt);
    it[1].Data = xsdt;
    it[1].Held = sizeof(xsdt);
    it[2].Sig = sig("FACP");
    it[2].Kind = CM_QA_K_ENTRY | (CM_QA_H_HEADER << 8);
    it[2].Length = sizeof(facp);
    it[2].Data = facp;
    it[2].Held = 36;
    /* entry 1 not read: kind 0 */
    it[4].Sig = sig("SSDT");
    it[4].Kind = CM_QA_K_ENTRY | (CM_QA_H_WHOLE << 8) | (2UL << 16);
    it[4].Length = sizeof(ssdt1);
    it[4].Data = ssdt1;
    it[4].Held = sizeof(ssdt1);
    it[5].Sig = sig("SSDT");
    it[5].Kind = CM_QA_K_ENTRY | (CM_QA_H_WHOLE << 8) | (3UL << 16);
    it[5].Length = sizeof(ssdt2);
    it[5].Data = ssdt2;
    it[5].Held = sizeof(ssdt2);         /* odd: padded to a dword */
    it[6].Sig = sig("MCFG");
    it[6].Kind = CM_QA_K_ENTRY | (CM_QA_H_WHOLE << 8) | (4UL << 16);
    it[6].Length = sizeof(mcfg);
    it[6].Data = mcfg;
    it[6].Held = sizeof(mcfg);
    *count = 7;
    return CmQaBody(body, sizeof(body), it, 7);
}

/* The whole body read back through answers of `room` bytes. */
static int read_parts(unsigned long room, unsigned long bytes, CM_QA_IN *in,
                      TR_QAHDR *first)
{
    unsigned long at = 0;
    unsigned long info;
    TR_QAHDR h;
    int n = 0;

    memset(whole, 0, sizeof(whole));
    do {
        if (CmQaAnswer(ans, room, in, at, &info) != CM_Q_ANS_WHOLE ||
            !tr_qa_header(ans, info, &h)) {
            return -1;
        }
        if (n == 0) {
            *first = h;
        } else if (!tr_qa_same(first, &h, at)) {
            return -1;
        }
        memcpy(whole + at, ans + CM_QA_HDR_BYTES, h.PartBytes);
        at += h.PartBytes;
        n++;
        if (h.PartBytes == 0) {
            break;
        }
    } while (at < bytes);
    return at == bytes ? n : -1;
}

static void test_builder(void)
{
    CM_QA_ITEM it[8];
    CM_QA_ITEM big[CM_QA_MAX_ENTRIES + 1];
    CM_QA_IN in;
    TR_QAHDR first;
    unsigned long count;
    unsigned long bytes;
    unsigned long info;
    unsigned long rooms[5];
    int i;

    bytes = build_body(it, &count);
    CHECK(bytes != 0, "body built");
    CHECK_EQ(bytes, CmQaBodyBytes(it, count), "body size");
    CHECK_EQ(get32(body, 4 * CM_QA_ENT_BYTES + CM_QA_E_OFFSET) & 3UL, 0UL,
             "data at a dword");
    CHECK_EQ(get32(body, 3 * CM_QA_ENT_BYTES + CM_QA_E_KIND), 0UL,
             "unread entry kind 0");
    CHECK_EQ(CmQaBody(body, bytes - 1, it, count), 0UL, "too small a room");
    it[3].Held = 4;
    CHECK_EQ(CmQaBodyBytes(it, count), 0UL, "held with no data refused");
    it[3].Held = 0;
    memset(big, 0, sizeof(big));
    CHECK_EQ(CmQaBodyBytes(big, CM_QA_MAX_ENTRIES + 1), 0UL,
             "over the entries");
    big[0].Data = body;
    big[0].Held = CM_QA_DATA_MAX + 1;
    CHECK_EQ(CmQaBodyBytes(big, 1), 0UL, "over the data bound");

    memset(&in, 0, sizeof(in));
    in.State = CM_QA_S_KEPT;
    in.EpochLo = 0x1234;
    in.EpochHi = 0x56;
    in.Body = body;
    in.BodyBytes = bytes;
    in.Count = count;
    in.Tier = 2;
    CHECK_EQ(CmQaAnswer(ans, CM_QA_HDR_BYTES - 1, &in, 0, &info),
             CM_Q_ANS_TOO_SMALL, "under the header");
    rooms[0] = CM_QA_HDR_BYTES + 1;
    rooms[1] = CM_QA_HDR_BYTES + 17;
    rooms[2] = CM_QA_HDR_BYTES + 100;
    rooms[3] = CM_QA_HDR_BYTES + bytes;
    rooms[4] = sizeof(ans);
    for (i = 0; i < 5; i++) {
        CHECK(read_parts(rooms[i], bytes, &in, &first) > 0, "parts read");
        CHECK(memcmp(whole, body, bytes) == 0, "parts fit together");
        CHECK_EQ(first.Tier, 2UL, "tier carried");
    }
    CHECK_EQ(CmQaAnswer(ans, sizeof(ans), &in, bytes + 1, &info),
             CM_Q_ANS_WHOLE, "past the body answered");
    CHECK(tr_qa_header(ans, info, &first), "past: header good");
    CHECK((first.Flags & CM_QA_F_PAST) != 0 && first.PartBytes == 0,
          "past flagged, no part");
    in.Stopping = TRUE;
    (void)CmQaAnswer(ans, sizeof(ans), &in, 0, &info);
    CHECK((get32(ans, CM_QA_O_FLAGS) & CM_QA_F_STOPPING) != 0, "stopping");
    in.Stopping = FALSE;

    /* the reader: an answer cut, a bad magic, a size field off */
    (void)CmQaAnswer(ans, sizeof(ans), &in, 0, &info);
    CHECK(!tr_qa_header(ans, info - 1, &first), "cut answer refused");
    put32(ans, CM_QA_O_MAGIC, 0);
    CHECK(!tr_qa_header(ans, info, &first), "bad magic refused");
    (void)CmQaAnswer(ans, sizeof(ans), &in, 0, &info);
    put32(ans, CM_QA_O_VERSION, 2);
    CHECK(!tr_qa_header(ans, info, &first), "other version refused");
    (void)CmQaAnswer(ans, sizeof(ans), &in, 0, &info);
    put32(ans, CM_QA_O_BODY_BYTES, CM_QA_MAX_BODY + 4);
    CHECK(!tr_qa_header(ans, info, &first), "body over bound refused");

    /* another epoch or body between parts */
    (void)CmQaAnswer(ans, CM_QA_HDR_BYTES + 100, &in, 0, &info);
    CHECK(tr_qa_header(ans, info, &first), "first part");
    in.EpochLo = 0x1235;
    (void)CmQaAnswer(ans, CM_QA_HDR_BYTES + 100, &in, 100, &info);
    {
        TR_QAHDR h;
        CHECK(tr_qa_header(ans, info, &h), "second part header");
        CHECK(!tr_qa_same(&first, &h, 100), "another epoch refused");
        in.EpochLo = 0x1234;
        (void)CmQaAnswer(ans, CM_QA_HDR_BYTES + 100, &in, 100, &info);
        (void)tr_qa_header(ans, info, &h);
        CHECK(tr_qa_same(&first, &h, 100), "same epoch taken");
        CHECK(!tr_qa_same(&first, &h, 96), "a part not at the offset");
    }

    /* the body read */
    memset(&a, 0, sizeof(a));
    CHECK(tr_qa_body(body, bytes, count, &a), "body read");
    CHECK_EQ(a.Count, 6L, "kind 0 skipped");
    CHECK(a.T[0].Rsdp && a.T[0].Held == TR_ACPI_HELD_WHOLE, "rsdp");
    CHECK(a.T[2].Held == TR_ACPI_HELD_HEADER && a.T[2].Bytes == 36,
          "a header");
    CHECK(memcmp(a.T[3].Data, ssdt1, sizeof(ssdt1)) == 0, "a table's bytes");
    /* bad shapes */
    memcpy(whole, body, bytes);
    put32(whole, 1 * CM_QA_ENT_BYTES + CM_QA_E_OFFSET, bytes);
    CHECK(!tr_qa_body(whole, bytes, count, &a), "entry past the body");
    CHECK_EQ(a.Count, 0L, "nothing kept of a bad body");
    memcpy(whole, body, bytes);
    put32(whole, 2 * CM_QA_ENT_BYTES + CM_QA_E_HELD, 40);
    CHECK(!tr_qa_body(whole, bytes, count, &a), "header over 36");
    memcpy(whole, body, bytes);
    put32(whole, 1 * CM_QA_ENT_BYTES + CM_QA_E_HELD, 40);
    CHECK(!tr_qa_body(whole, bytes, count, &a),
          "whole held not its length");
    memcpy(whole, body, bytes);
    put32(whole, 0 * CM_QA_ENT_BYTES + CM_QA_E_HELD, 300);
    CHECK(!tr_qa_body(whole, bytes, count, &a), "rsdp over 255");
    memcpy(whole, body, bytes);
    put32(whole, 3 * CM_QA_ENT_BYTES + CM_QA_E_HELD, 4);
    CHECK(!tr_qa_body(whole, bytes, count, &a), "kind 0 holding bytes");
    CHECK(!tr_qa_body(body, bytes, CM_QA_MAX_ENTRIES + 1, &a),
          "count over bound");
    CHECK(!tr_qa_body(body, 64, count, &a), "entries past the body");
    /* a revision 2 root pointer held at 20 bytes, its length field 36 */
    memcpy(whole, body, bytes);
    put32(whole, 0 * CM_QA_ENT_BYTES + CM_QA_E_HELD, 20);
    put32(whole, 0 * CM_QA_ENT_BYTES + CM_QA_E_LENGTH, 20);
    CHECK(!tr_qa_body(whole, bytes, count, &a), "rsdp rev 2 cut refused");
    memcpy(whole, body, bytes);
    put32(whole, 0 * CM_QA_ENT_BYTES + CM_QA_E_LENGTH, 20);
    CHECK(!tr_qa_body(whole, bytes, count, &a), "rsdp entry length off");
}

static void test_sums_names(void)
{
    unsigned char r0[64];
    unsigned char r2[64];
    unsigned char bad[116];
    unsigned char facs[64];
    unsigned char asf1[48];
    unsigned char asf2[48];
    char list[256];
    TR_TEXT t;
    int i;
    int n;
    const char *p;
    const char *line;

    memset(&a, 0, sizeof(a));
    a.Source = TR_ACPI_SRC_NT;
    make_rsdp(r0, 0, 20);
    make_rsdp(r2, 2, 36);
    make_table(facp, "FACP", sizeof(facp), "TESTTBL1", 2);
    memcpy(bad, facp, sizeof(bad));
    bad[50] ^= 1;
    memset(facs, 0, sizeof(facs));
    memcpy(facs, "FACS", 4);
    put32(facs, 4, 64);
    make_table(ssdt1, "SSDT", sizeof(ssdt1), "TESTTBL2", 3);
    make_table(asf1, "ASF!", sizeof(asf1), "TESTTBL5", 6);
    make_table(asf2, "ASF_", sizeof(asf2), "TESTTBL6", 7);

    CHECK(tr_acpi_add(&a, facp, sizeof(facp), 1), "add");
    CHECK_EQ(tr_acpi_sum(&a.T[0]), TR_ACPI_SUM_OK, "good table");
    (void)tr_acpi_add(&a, bad, sizeof(bad), 1);
    CHECK_EQ(tr_acpi_sum(&a.T[1]), TR_ACPI_SUM_BAD, "bad table");
    (void)tr_acpi_add(&a, facs, sizeof(facs), 1);
    CHECK_EQ(tr_acpi_sum(&a.T[2]), TR_ACPI_SUM_NONE, "facs no checksum");
    (void)tr_acpi_add(&a, ssdt1, sizeof(ssdt1), 3);
    (void)tr_acpi_add(&a, ssdt1, sizeof(ssdt1) - 4, 1);
    CHECK_EQ(tr_acpi_sum(&a.T[4]), TR_ACPI_SUM_BAD, "short table bad");
    (void)tr_acpi_add(&a, asf1, sizeof(asf1), 1);
    (void)tr_acpi_add(&a, asf2, sizeof(asf2), 1);
    a.T[a.Count].Rsdp = 1;
    a.T[a.Count].Held = TR_ACPI_HELD_WHOLE;
    a.T[a.Count].Data = r0;
    a.T[a.Count].Bytes = 20;
    a.T[a.Count].Length = 20;
    a.Count++;
    CHECK_EQ(tr_acpi_sum(&a.T[a.Count - 1]), TR_ACPI_SUM_OK, "rsdp rev 0");
    a.T[a.Count].Rsdp = 1;
    a.T[a.Count].Held = TR_ACPI_HELD_WHOLE;
    a.T[a.Count].Data = r2;
    a.T[a.Count].Bytes = 36;
    a.T[a.Count].Length = 36;
    a.Count++;
    CHECK_EQ(tr_acpi_sum(&a.T[a.Count - 1]), TR_ACPI_SUM_OK, "rsdp rev 2");
    r2[33] ^= 1;
    CHECK_EQ(tr_acpi_sum(&a.T[a.Count - 1]), TR_ACPI_SUM_BAD,
             "rsdp rev 2 extended sum");
    r2[33] ^= 1;
    a.T[a.Count - 1].Bytes = 20;
    CHECK_EQ(tr_acpi_sum(&a.T[a.Count - 1]), TR_ACPI_SUM_BAD,
             "rsdp rev 2 held short");
    a.T[a.Count - 1].Bytes = 36;

    /* an OEM ID with a NUL inside: kept as '.', the padding left out */
    asf1[12] = 0;
    asf1[15] = 0;
    {
        unsigned int sum = 0;
        unsigned long k;
        asf1[9] = 0;
        for (k = 0; k < sizeof(asf1); k++) {
            sum += asf1[k];
        }
        asf1[9] = (unsigned char)(0x100 - (sum & 0xFF));
    }
    n = tr_acpi_names(&a);
    CHECK_EQ(n, a.Count, "every whole table named");
    CHECK(strcmp(a.T[0].File, "FACP1.DAT") == 0, "FACP1");
    CHECK(strcmp(a.T[1].File, "FACP2.DAT") == 0, "FACP2");
    CHECK(strcmp(a.T[2].File, "FACS1.DAT") == 0, "FACS1");
    CHECK(strcmp(a.T[3].File, "SSDT1.DAT") == 0, "SSDT1");
    CHECK(strcmp(a.T[4].File, "SSDT2.DAT") == 0, "SSDT2");
    CHECK(strcmp(a.T[5].File, "ASF_1.DAT") == 0, "ASF! as ASF_1");
    CHECK(strcmp(a.T[6].File, "ASF_2.DAT") == 0, "ASF_ as ASF_2");
    CHECK(strcmp(a.T[7].File, "RSDP1.DAT") == 0, "RSDP1");
    CHECK(strcmp(a.T[8].File, "RSDP2.DAT") == 0, "RSDP2");
    for (i = 0; i < a.Count; i++) {
        CHECK(strlen(a.T[i].File) <= 12, "8.3");
    }
    tr_acpi_list(&a, list, sizeof(list));
    CHECK(strcmp(list, "FACP (2), FACS, SSDT (2), ASF!, ASF_, RSDP (2)") == 0,
          "the list");

    tr_text_init(&t, text, sizeof(text));
    tr_acpi_index(&a, &t);
    CHECK(!t.Overflow, "index fits");
    CHECK(strstr(text, "Windows listed SSDT 3 times") != NULL,
          "the limit said");
    CHECK(strstr(text, "SSDT1.DAT   SSDT  TESTOE  TESTTBL2") != NULL,
          "an index line");
    CHECK(strstr(text, "ASF_1.DAT   ASF!  TE.TO   TESTTBL5") != NULL,
          "a NUL inside an id");
    CHECK(strstr(text, "BAD") != NULL, "a bad checksum shown");
    CHECK(strstr(text, "\r\r") == NULL, "no CR CR LF");
    CHECK(strstr(text, "\r\n") != NULL, "CR LF lines");
    line = text;
    for (p = text; *p != '\0'; p++) {
        if (*p == '\n') {
            CHECK(p - line <= 80, "index line within 79 columns");
            line = p + 1;
        }
        CHECK((unsigned char)*p < 0x80, "index ASCII");
    }
}

static void test_door_index(void)
{
    CM_QA_ITEM it[8];
    unsigned long count;
    unsigned long bytes;
    TR_TEXT t;
    char msg[1024];

    bytes = build_body(it, &count);
    memset(&a, 0, sizeof(a));
    a.Source = TR_ACPI_SRC_CM;
    a.State = CM_QA_S_KEPT;
    a.Outcome = 0x0D;
    a.Tier = 1;
    CHECK(tr_qa_body(body, bytes, count, &a), "door body");
    CHECK_EQ(tr_acpi_names(&a), 5, "five saved, one header");
    CHECK(a.T[2].File[0] == '\0', "header not saved");
    tr_text_init(&t, text, sizeof(text));
    tr_acpi_index(&a, &t);
    CHECK(strstr(text, "not saved   FACP") != NULL, "header listed");
    CHECK(strstr(text, "outcome Dh, tier 1") != NULL, "the driver's state");
    tr_acpi_message(TR_ACPI_M_SAVED, &a, "C:\\T", NULL, NULL, 0, 0, msg,
                    sizeof(msg));
    CHECK(strcmp(msg, "This computer's firmware (ACPI) tables were saved in "
                      "C:\\T: RSDP, XSDT, SSDT (2), MCFG, and TABLES.TXT "
                      "listing them. Windows 98 SE / ME give no access to "
                      "these tables; these are the ones xhcicm.sys read for "
                      "the USB4 handover. xhci98.sys did not read the DSDT "
                      "and the other SSDTs: this xhci98.sys does not read "
                      "them. 1 more are listed in TABLES.TXT, not saved.") == 0,
          "98 saved message");
}

static void test_messages(void)
{
    char msg[1024];

    memset(&a, 0, sizeof(a));
    tr_acpi_message(TR_ACPI_M_NO_API, &a, "C:\\", NULL, NULL, 0, 0, msg,
                    sizeof(msg));
    CHECK(strcmp(msg, "No tables were saved: this Windows gives programs no "
                      "way to read the firmware (ACPI) tables. XP x64, Vista "
                      "and later do.") == 0, "no api");
    tr_acpi_message(TR_ACPI_M_NO_CM, &a, "C:\\", NULL, NULL, 0, 0, msg,
                    sizeof(msg));
    CHECK(strstr(msg, "is not running here.") != NULL, "no cm");
    a.State = CM_QA_S_OFF;
    tr_acpi_message(TR_ACPI_M_CM_NONE, &a, "C:\\", NULL, NULL, 0, 0, msg,
                    sizeof(msg));
    CHECK(strcmp(msg, "No tables were saved: xhcicm.sys did not read them at "
                      "this start: XhciCmAsm4242Handover is 0.") == 0,
          "cm none");
    tr_acpi_message(TR_ACPI_M_CM_NONE, &a, "C:\\", NULL, tr_qa_why(0), 0, 0,
                    msg, sizeof(msg));
    CHECK(strstr(msg, "this xhcicm.sys does not serve them") != NULL,
          "old driver");
    CHECK(strcmp(tr_qa_why(-1), "the driver's answer could not be read") == 0,
          "unreadable");
    CHECK(strcmp(tr_qa_why((long)CM_QA_S_NOT_LOOKED),
                 "it reads them only for an ASM4242 USB4 host") == 0,
          "not looked");
    tr_acpi_message(TR_ACPI_M_NT_NONE, &a, "C:\\", NULL, NULL, 0, 0, msg,
                    sizeof(msg));
    CHECK(strcmp(msg, "No tables were saved: Windows gave none.") == 0,
          "nt none");
    tr_acpi_message(TR_ACPI_M_EXISTS, &a, "C:\\T", "SSDT1.DAT", NULL, 0, 0,
                    msg, sizeof(msg));
    CHECK(strcmp(msg, "Nothing was saved: C:\\T already has SSDT1.DAT. Pick "
                      "an empty folder.") == 0, "exists");
    tr_acpi_message(TR_ACPI_M_WRITE, &a, "C:\\T", "SSDT1.DAT", NULL, 2, 6,
                    msg, sizeof(msg));
    CHECK(strcmp(msg, "Saving stopped at SSDT1.DAT in C:\\T: 2 of 6 files "
                      "were written.") == 0, "write");
    memset(&a, 0, sizeof(a));
    a.Source = TR_ACPI_SRC_NT;
    make_table(ssdt1, "SSDT", sizeof(ssdt1), "TESTTBL2", 3);
    (void)tr_acpi_add(&a, ssdt1, sizeof(ssdt1), 4);
    tr_acpi_message(TR_ACPI_M_SAVED, &a, "C:\\T", NULL, NULL, 0, 0, msg,
                    sizeof(msg));
    CHECK(strcmp(msg, "This computer's firmware (ACPI) tables were saved in "
                      "C:\\T: SSDT, and TABLES.TXT listing them. Windows "
                      "gives one table of each name: of SSDT (4), the first "
                      "was saved. 3 SSDTs were not saved.") == 0,
          "nt saved message");
}

/* ---- xhci98.sys's reading over a modelled memory (option B) ------------- */

#define MEM_LO      0x7FF00000UL
#define MEM_BYTES   0x40000UL
#define A_RSDT      (MEM_LO + 0x0000UL)
#define A_FACP      (MEM_LO + 0x1000UL)
#define A_SSDTA     (MEM_LO + 0x2000UL)
#define A_MCFG      (MEM_LO + 0x3000UL)
#define A_APIC      (MEM_LO + 0x4000UL)
#define A_SSDTB     (MEM_LO + 0x5FF0UL)     /* crosses a page */
#define A_DSDT      (MEM_LO + 0x10234UL)    /* 100 KB, mid-page */
#define DSDT_BYTES  102400UL

static unsigned char bios[0x100000];
static unsigned char mem[MEM_BYTES];
static unsigned long memMaxSpan;
static unsigned long memReads;
static int memStaleSsdtB;
static int memStaleMcfg;
static unsigned long nowMs;
static unsigned long nowStep;
static int allocs;

static BOOLEAN mem_read(PVOID ctx, ULONG phys, UCHAR *out, ULONG bytes)
{
    unsigned long span;

    (void)ctx;
    memReads++;
    span = ((phys & 0xFFFUL) + bytes + 0xFFFUL) & ~0xFFFUL;
    if (span > memMaxSpan) {
        memMaxSpan = span;
    }
    if (phys < 0x100000UL && bytes <= 0x100000UL - phys) {
        memcpy(out, bios + phys, bytes);
        return TRUE;
    }
    if (phys >= MEM_LO && phys - MEM_LO <= MEM_BYTES &&
        bytes <= MEM_BYTES - (phys - MEM_LO)) {
        memcpy(out, mem + (phys - MEM_LO), bytes);
        if (memStaleSsdtB && phys == A_SSDTB && bytes > 36) {
            out[0] = 'X';           /* moved since its header was read */
        }
        /* No longer MCFG after the bootstrap: the header reread and the
         * whole copy both say "MCFX", with a sound checksum. */
        if (memStaleMcfg && phys == A_MCFG) {
            if (memStaleMcfg++ > 1) {
                unsigned int k;
                unsigned int sum = 0;
                out[3] = 'X';
                if (bytes > 36) {
                    out[9] = 0;
                    for (k = 0; k < bytes; k++) {
                        sum += out[k];
                    }
                    out[9] = (unsigned char)(0x100 - (sum & 0xFF));
                }
            }
        }
        return TRUE;
    }
    return FALSE;
}

static PVOID mem_alloc(PVOID ctx, ULONG bytes)
{
    (void)ctx;
    allocs++;
    return malloc(bytes);
}

static void mem_free(PVOID ctx, PVOID p)
{
    (void)ctx;
    if (p != NULL) {
        allocs--;
    }
    free(p);
}

static unsigned long nowCalls;
static unsigned long nowJumpAt;

static ULONG mem_now(PVOID ctx)
{
    (void)ctx;
    nowCalls++;
    nowMs += nowStep;
    if (nowJumpAt != 0 && nowCalls == nowJumpAt) {
        nowMs += 30000;
    }
    return nowMs;
}

static void mem_sum(unsigned char *t, unsigned long len)
{
    unsigned long i;
    unsigned int s = 0;

    t[9] = 0;
    for (i = 0; i < len; i++) {
        s += t[i];
    }
    t[9] = (unsigned char)(0x100 - (s & 0xFF));
}

/* The modelled machine: the root pointer in the BIOS area, an RSDT with
 * FACP, two SSDTs, MCFG and APIC, the DSDT through FACP. */
static void mem_build(void)
{
    unsigned char *p;
    unsigned long i;
    unsigned int s;

    memset(bios, 0, sizeof(bios));
    memset(mem, 0, sizeof(mem));
    memStaleSsdtB = 0;
    memStaleMcfg = 0;
    nowMs = 0;
    nowStep = 1;
    nowCalls = 0;
    nowJumpAt = 0;
    memMaxSpan = 0;
    /* EBDA segment pointing nowhere useful: the E0000h scan finds it. */
    bios[0x40E] = 0xC0;
    bios[0x40F] = 0x9F;
    p = bios + 0xF0010UL;
    memcpy(p, "RSD PTR ", 8);
    memcpy(p + 9, "TESTOE", 6);
    p[15] = 0;
    put32(p, 16, A_RSDT);
    s = 0;
    for (i = 0; i < 20; i++) {
        s += p[i];
    }
    p[8] = (unsigned char)(0x100 - (s & 0xFF));
    /* RSDT: 5 entries */
    make_table(mem + (A_RSDT - MEM_LO), "RSDT", 36 + 20, "TESTTBL0", 0);
    p = mem + (A_RSDT - MEM_LO);
    put32(p, 36, A_FACP);
    put32(p, 40, A_SSDTA);
    put32(p, 44, A_MCFG);
    put32(p, 48, A_APIC);
    put32(p, 52, A_SSDTB);
    mem_sum(p, 56);
    make_table(mem + (A_FACP - MEM_LO), "FACP", 148, "TESTTBL1", 1);
    p = mem + (A_FACP - MEM_LO);
    memset(p + 36, 0, 148 - 36);
    put32(p, 40, A_DSDT);
    mem_sum(p, 148);
    make_table(mem + (A_SSDTA - MEM_LO), "SSDT", 300, "TESTTBL2", 2);
    make_table(mem + (A_MCFG - MEM_LO), "MCFG", 60, "TESTTBL3", 3);
    p = mem + (A_MCFG - MEM_LO);
    memset(p + 36, 0, 24);
    put32(p, 44, 0xB0000000UL);
    p[44 + 10] = 0;
    p[44 + 11] = 255;
    mem_sum(p, 60);
    make_table(mem + (A_APIC - MEM_LO), "APIC", 80, "TESTTBL4", 4);
    make_table(mem + (A_SSDTB - MEM_LO), "SSDT", 200, "TESTTBL5", 5);
    make_table(mem + (A_DSDT - MEM_LO), "DSDT", DSDT_BYTES, "TESTDSDT", 6);
}

static ULONG xha_read(UCHAR **body, ULONG *bytes, ULONG *count, ULONG *flags,
                      ULONG *outcome)
{
    XHA_HOST h;

    h.Read = mem_read;
    h.Alloc = mem_alloc;
    h.Free = mem_free;
    h.NowMs = mem_now;
    h.Context = NULL;
    return XhaReadSet(&h, body, bytes, count, flags, outcome);
}

static TR_ACPI xa;

static void test_xha_read(void)
{
    UCHAR *body;
    ULONG bytes;
    ULONG count;
    ULONG flags;
    ULONG outcome;
    ULONG st;
    unsigned char *p;
    int i;

    mem_build();
    allocs = 0;
    st = xha_read(&body, &bytes, &count, &flags, &outcome);
    CHECK_EQ(st, XHA_S_READ, "read");
    CHECK_EQ(allocs, 1, "one allocation left: the body");
    CHECK_EQ(count, 8UL, "pointer, root, 5 entries, DSDT");
    CHECK_EQ(flags, 0UL, "complete");
    CHECK(memMaxSpan <= XHA_MAP_MAX, "no mapping over 64 KB");
    memset(&xa, 0, sizeof(xa));
    CHECK(tr_xha_body(body, bytes, count, &xa), "body read by the tool");
    CHECK(xa.T[7].Kind == XHA_K_DSDT && xa.T[7].Index == 0 &&
          xa.T[7].Held == TR_ACPI_HELD_WHOLE &&
          xa.T[7].Bytes == DSDT_BYTES, "DSDT whole, the FADT's index");
    CHECK(memcmp(xa.T[7].Data, mem + (A_DSDT - MEM_LO), DSDT_BYTES) == 0,
          "DSDT byte for byte");
    CHECK(xa.T[5].Held == TR_ACPI_HELD_HEADER && xa.T[5].Reason == 0,
          "APIC a header by its kind");
    CHECK(xa.T[3].Held == TR_ACPI_HELD_WHOLE && xa.T[6].Held ==
          TR_ACPI_HELD_WHOLE, "both SSDTs whole");
    for (i = 0; i < xa.Count; i++) {
        if (xa.T[i].Held == TR_ACPI_HELD_WHOLE) {
            CHECK_EQ(tr_acpi_sum(&xa.T[i]), TR_ACPI_SUM_OK, "checksums");
        }
    }
    free(body);

    /* A table moved since its header was read: kept as its header. */
    mem_build();
    memStaleSsdtB = 1;
    st = xha_read(&body, &bytes, &count, &flags, &outcome);
    CHECK_EQ(st, XHA_S_READ, "stale: read");
    CHECK((flags & XHA_F_INCOMPLETE) != 0, "stale: incomplete");
    memset(&xa, 0, sizeof(xa));
    (void)tr_xha_body(body, bytes, count, &xa);
    CHECK(xa.T[6].Held == TR_ACPI_HELD_HEADER &&
          xa.T[6].Reason == XHA_R_SIGNATURE, "stale: no longer a table");
    CHECK(strcmp(tr_acpi_reason(xa.T[6].Reason),
                 "no longer a table at its address") == 0, "stale text");
    free(body);

    /* A bad checksum: kept as its header, never whole. */
    mem_build();
    mem[(A_SSDTA - MEM_LO) + 100] ^= 0x55;
    st = xha_read(&body, &bytes, &count, &flags, &outcome);
    memset(&xa, 0, sizeof(xa));
    (void)tr_xha_body(body, bytes, count, &xa);
    CHECK(xa.T[3].Held == TR_ACPI_HELD_HEADER &&
          xa.T[3].Reason == XHA_R_CHECKSUM, "bad checksum not kept");
    free(body);

    /* A length past the SSDT cap. */
    mem_build();
    put32(mem + (A_SSDTB - MEM_LO), 4, XHA_SSDT_CAP + 4);
    st = xha_read(&body, &bytes, &count, &flags, &outcome);
    memset(&xa, 0, sizeof(xa));
    (void)tr_xha_body(body, bytes, count, &xa);
    CHECK(xa.T[6].Held == TR_ACPI_HELD_HEADER &&
          xa.T[6].Reason == XHA_R_LIMIT, "past the SSDT cap");
    free(body);

    /* A FADT of 43 bytes: no DSDT. */
    mem_build();
    p = mem + (A_FACP - MEM_LO);
    put32(p, 4, 43);
    mem_sum(p, 43);
    st = xha_read(&body, &bytes, &count, &flags, &outcome);
    CHECK_EQ(st, XHA_S_READ, "short FADT read");
    CHECK((flags & XHA_F_NO_DSDT) != 0, "short FADT: no DSDT");
    CHECK_EQ(count, 7UL, "no DSDT entry");
    free(body);

    /* Every table inside an MCFG window of another segment. */
    mem_build();
    p = mem + (A_MCFG - MEM_LO);
    put32(p, 4, 76);
    put32(p, 60, MEM_LO & ~0xFFFFFUL);
    p[60 + 8] = 1;
    p[60 + 10] = 0;
    p[60 + 11] = 0;
    mem_sum(p, 76);
    st = xha_read(&body, &bytes, &count, &flags, &outcome);
    CHECK_EQ(st, XHA_S_READ, "window: read");
    memset(&xa, 0, sizeof(xa));
    (void)tr_xha_body(body, bytes, count, &xa);
    CHECK(xa.T[2].Held == TR_ACPI_HELD_NONE &&
          xa.T[2].Reason == XHA_R_BOUNDS, "inside a window: not read");
    free(body);

    /* An entry whose header cannot be read: the set refused. */
    mem_build();
    put32(mem + (A_RSDT - MEM_LO), 48, 0x12345000UL);
    mem_sum(mem + (A_RSDT - MEM_LO), 56);
    allocs = 0;
    st = xha_read(&body, &bytes, &count, &flags, &outcome);
    CHECK(st == XHA_S_REFUSED && outcome == 3, "unclassified entry refuses");
    CHECK(body == NULL, "nothing kept");
    CHECK_EQ(allocs, 0, "nothing left allocated");

    /* Two MCFGs: refused. */
    mem_build();
    put32(mem + (A_RSDT - MEM_LO), 48, A_MCFG);
    mem_sum(mem + (A_RSDT - MEM_LO), 56);
    st = xha_read(&body, &bytes, &count, &flags, &outcome);
    CHECK(st == XHA_S_REFUSED && outcome == 2, "two MCFGs refuse");

    /* MCFG changed between the bootstrap and its reread: refused. */
    mem_build();
    memStaleMcfg = 1;
    st = xha_read(&body, &bytes, &count, &flags, &outcome);
    CHECK(st == XHA_S_REFUSED && outcome == 2, "MCFG changed refuses");

    /* The clock past 20 s in the bootstrap: refused, outcome 4. */
    mem_build();
    nowJumpAt = 2;
    st = xha_read(&body, &bytes, &count, &flags, &outcome);
    CHECK(st == XHA_S_REFUSED && outcome == 4, "bootstrap timeout");

    /* No root pointer. */
    mem_build();
    memset(bios + 0xF0010UL, 0, 20);
    st = xha_read(&body, &bytes, &count, &flags, &outcome);
    CHECK_EQ(st, XHA_S_NO_RSDP, "no root pointer");

    /* The timeout: the clock past 20 s at the DSDT's whole (the start, the
     * root's, 5 bootstrap, MCFG's, 5 header, FADT's and DSDT's checks,
     * then a check before each whole). */
    mem_build();
    nowJumpAt = 20;
    st = xha_read(&body, &bytes, &count, &flags, &outcome);
    memset(&xa, 0, sizeof(xa));
    (void)tr_xha_body(body, bytes, count, &xa);
    CHECK(xa.T[7].Held == TR_ACPI_HELD_HEADER &&
          xa.T[7].Reason == XHA_R_TIMEOUT, "timeout");
    free(body);
}

static void test_xha_answer(void)
{
    UCHAR *body;
    ULONG bytes;
    ULONG count;
    ULONG flags;
    ULONG outcome;
    ULONG info;
    ULONG at;
    TR_QAHDR first;
    TR_QAHDR h;
    XHA_IN in;
    static unsigned char out[XHA_ASK_BYTES];
    static unsigned char got[XHA_MAX_BODY];

    mem_build();
    (void)xha_read(&body, &bytes, &count, &flags, &outcome);
    memset(&in, 0, sizeof(in));
    in.State = XHA_S_READ;
    in.Body = body;
    in.BodyBytes = bytes;
    in.Count = count;
    in.SetGen = 9;
    CHECK_EQ(XhaAnswer(out, XHA_HDR_BYTES - 1, &in, 0, &info), 1UL,
             "too small");
    (void)XhaAnswer(out, sizeof(out), &in, 0, &info);
    CHECK(tr_xha_header(out, info, &first), "first part");
    CHECK_EQ(first.SetGen, 9UL, "generation");
    CHECK(!tr_qa_header(out, info, &h), "not an XCMA answer");
    memcpy(got, out + XHA_HDR_BYTES, first.PartBytes);
    at = first.PartBytes;
    while (at < bytes) {
        (void)XhaAnswer(out, sizeof(out), &in, at, &info);
        CHECK(tr_xha_header(out, info, &h) && tr_qa_same(&first, &h, at),
              "a later part");
        memcpy(got + at, out + XHA_HDR_BYTES, h.PartBytes);
        at += h.PartBytes;
    }
    CHECK(memcmp(got, body, bytes) == 0, "parts fit");
    (void)XhaAnswer(out, sizeof(out), &in, bytes + 1, &info);
    CHECK(tr_xha_header(out, info, &h) && (h.Flags & XHA_F_PAST) != 0 &&
          h.SetGen == 0, "past: generation 0");
    in.SetGen = 10;
    (void)XhaAnswer(out, sizeof(out), &in, XHA_PART_MAX, &info);
    (void)tr_xha_header(out, info, &h);
    CHECK(!tr_qa_same(&first, &h, XHA_PART_MAX), "another generation");
    memset(&in, 0, sizeof(in));
    in.State = XHA_S_BUSY;
    (void)XhaAnswer(out, sizeof(out), &in, 0, &info);
    CHECK(tr_xha_header(out, info, &h) && h.State == XHA_S_BUSY &&
          h.SetGen == 0 && h.PartBytes == 0, "busy header");
    free(body);
}

static void test_xha_merge(void)
{
    UCHAR *body;
    ULONG bytes;
    ULONG count;
    ULONG flags;
    ULONG outcome;
    static TR_ACPI cm;
    static unsigned char cp[400];
    char msg[2048];
    TR_TEXT t;

    mem_build();
    (void)xha_read(&body, &bytes, &count, &flags, &outcome);
    memset(&xa, 0, sizeof(xa));
    (void)tr_xha_body(body, bytes, count, &xa);
    xa.Source = TR_ACPI_SRC_XHA;
    memset(&cm, 0, sizeof(cm));
    /* xhcicm's copies: the first SSDT the same, the second different. */
    cm.Count = 2;
    cm.T[0] = xa.T[3];
    cm.T[0].From = TR_ACPI_SRC_CM;
    cm.T[1] = xa.T[6];
    cm.T[1].From = TR_ACPI_SRC_CM;
    memcpy(cp, xa.T[6].Data, xa.T[6].Bytes);
    cp[50] ^= 1;
    cm.T[1].Data = cp;
    tr_acpi_merge(&xa, &cm);
    CHECK(xa.T[3].SameAsCm, "identical copy marked");
    CHECK_EQ(xa.Count, 9, "the different copy added");
    CHECK_EQ(tr_acpi_names(&xa), 8, "names");
    CHECK(strcmp(xa.T[8].File, "SSDT1C.DAT") == 0, "the copy's name");
    CHECK(strcmp(xa.T[6].File, "SSDT2.DAT") == 0, "xhci98's second");
    tr_text_init(&t, text, sizeof(text));
    tr_acpi_index(&xa, &t);
    CHECK(strstr(text, "the same as read by xhcicm.sys at its start") != NULL,
          "same noted");
    CHECK(strstr(text, "xhcicm") != NULL && strstr(text, "xhci98") != NULL,
          "From column");
    tr_acpi_message(TR_ACPI_M_SAVED, &xa, "C:\\T", NULL, NULL, 0, 0, msg,
                    sizeof(msg));
    CHECK(strstr(msg, "xhci98.sys read them from memory for this save.") !=
              NULL, "xhci98 message");
    free(body);
}

/* ---- the registry, modelled -------------------------------------------- */

typedef struct _FAKEKEY {
    const char *Path;
    const char *Sub[10];
    const unsigned char *Data;
    unsigned long Bytes;
    unsigned long Type;
} FAKEKEY;

static FAKEKEY reg[16];
static int regN;

static int fake_subkey(void *ctx, const char *path, int index, char *name,
                       size_t cap)
{
    int i;

    (void)ctx;
    for (i = 0; i < regN; i++) {
        if (strcmp(reg[i].Path, path) == 0) {
            if (index >= 10 || reg[i].Sub[index] == NULL) {
                return 0;
            }
            tr_copy(name, reg[i].Sub[index], cap);
            return 1;
        }
    }
    return 0;
}

static int fake_value(void *ctx, const char *path, const char *name,
                      unsigned char *buf, TR_U32 cap, TR_U32 *bytes,
                      TR_U32 *type)
{
    int i;

    (void)ctx;
    (void)name;
    for (i = 0; i < regN; i++) {
        if (strcmp(reg[i].Path, path) == 0 && reg[i].Data != NULL) {
            if (reg[i].Bytes > cap) {
                return 0;
            }
            memcpy(buf, reg[i].Data, reg[i].Bytes);
            *bytes = reg[i].Bytes;
            *type = reg[i].Type;
            return 1;
        }
    }
    return 0;
}

static void reg_add(const char *path, const char *s0, const unsigned char *d,
                    unsigned long bytes, unsigned long type)
{
    memset(&reg[regN], 0, sizeof(reg[regN]));
    reg[regN].Path = path;
    reg[regN].Sub[0] = s0;
    reg[regN].Data = d;
    reg[regN].Bytes = bytes;
    reg[regN].Type = type;
    regN++;
}

static void test_reg(void)
{
    static unsigned char store[0x200000];
    static unsigned char rdsdt[200];
    static unsigned char rfadt[116];
    static unsigned char rrsdt[44];
    static unsigned char rfacs[64];
    TR_REGOPS ops;
    TR_TEXT t;
    char msg[2048];
    int whole;
    int i;

    make_table(rdsdt, "DSDT", sizeof(rdsdt), "TESTDSDT", 1);
    make_table(rfadt, "FACP", sizeof(rfadt), "TESTFACP", 2);
    make_table(rrsdt, "RSDT", sizeof(rrsdt), "TESTRSDT", 0);
    put32(rrsdt, 36, 0x3FFE1000UL);
    put32(rrsdt, 40, 0x3FFE2000UL);
    mem_sum(rrsdt, sizeof(rrsdt));
    memset(rfacs, 0, sizeof(rfacs));
    memcpy(rfacs, "FACS", 4);
    put32(rfacs, 4, 64);
    regN = 0;
    reg_add("HARDWARE\\ACPI\\DSDT", "TESTOE", NULL, 0, 0);
    reg_add("HARDWARE\\ACPI\\DSDT\\TESTOE", "TESTDSDT", NULL, 0, 0);
    reg_add("HARDWARE\\ACPI\\DSDT\\TESTOE\\TESTDSDT", "00000001", NULL, 0, 0);
    reg_add("HARDWARE\\ACPI\\DSDT\\TESTOE\\TESTDSDT\\00000001", NULL, rdsdt,
            sizeof(rdsdt), TR_REG_BINARY);
    reg_add("HARDWARE\\ACPI\\FADT", "TESTOE", NULL, 0, 0);
    reg_add("HARDWARE\\ACPI\\FADT\\TESTOE", "TESTFACP", NULL, 0, 0);
    reg_add("HARDWARE\\ACPI\\FADT\\TESTOE\\TESTFACP", "00000001", NULL, 0, 0);
    reg_add("HARDWARE\\ACPI\\FADT\\TESTOE\\TESTFACP\\00000001", NULL, rfadt,
            sizeof(rfadt), TR_REG_BINARY);
    reg_add("HARDWARE\\ACPI\\RSDT", "TESTOE", NULL, 0, 0);
    reg_add("HARDWARE\\ACPI\\RSDT\\TESTOE", "TESTRSDT", NULL, 0, 0);
    reg_add("HARDWARE\\ACPI\\RSDT\\TESTOE\\TESTRSDT", "00000001", NULL, 0, 0);
    reg_add("HARDWARE\\ACPI\\RSDT\\TESTOE\\TESTRSDT\\00000001", NULL, rrsdt,
            sizeof(rrsdt), TR_REG_BINARY);
    reg_add("HARDWARE\\ACPI\\FACS", NULL, rfacs, sizeof(rfacs),
            TR_REG_BINARY);
    ops.Subkey = fake_subkey;
    ops.Value = fake_value;
    ops.Ctx = NULL;
    memset(&xa, 0, sizeof(xa));
    whole = tr_acpi_reg_walk(&ops, &xa, store, sizeof(store));
    CHECK_EQ(whole, 4, "four tables from the registry");
    CHECK_EQ(xa.Count, 4, "the four tables alone");
    CHECK(memcmp(xa.T[0].Data, rdsdt, sizeof(rdsdt)) == 0, "DSDT bytes");
    CHECK_EQ(tr_acpi_names(&xa), 4, "names");
    CHECK(strcmp(xa.T[1].File, "FACP1.DAT") == 0, "FADT saved as FACP");
    tr_text_init(&t, text, sizeof(text));
    tr_acpi_index(&xa, &t);
    CHECK(strstr(text, "The RSDT lists 2 tables, at these addresses; the "
                       "registry keeps 1 of them,\r\nso 1 was not saved:")
              != NULL, "index: the entries counted, the FACP among them");
    CHECK(strstr(text, "   0  3FFE1000h") != NULL, "index: an entry by address");
    /* Two copies of the FACP in the registry count once. */
    xa.T[xa.Count] = xa.T[1];
    xa.Count++;
    tr_text_init(&t, text, sizeof(text));
    tr_acpi_index(&xa, &t);
    CHECK(strstr(text, "the registry keeps 1 of them,\r\nso 1 was not saved:")
              != NULL, "two FACP copies counted once");
    xa.Count--;
    CHECK(strstr(text, "copies Windows keeps in the registry") != NULL,
          "index: the source");
    for (i = 0; text[i] != '\0'; i++) {
        CHECK((unsigned char)text[i] < 0x80, "ASCII");
    }
    tr_acpi_message(TR_ACPI_M_SAVED, &xa, "C:\\T", NULL, NULL, 0, 0, msg,
                    sizeof(msg));
    CHECK(strcmp(msg, "This computer's firmware (ACPI) tables were saved in "
                      "C:\\T: DSDT, FACP, RSDT, FACS, and TABLES.TXT listing "
                      "them. They come from the copies Windows keeps in the "
                      "registry; the SSDTs cannot be read on this Windows.")
              == 0,
          "registry message");
    /* The checks: a wrong signature, a length off, a bad checksum, a
     * string, a FACS. */
    CHECK_EQ(tr_acpi_reg_check("FADT", TR_REG_BINARY, rdsdt, sizeof(rdsdt)),
             XHA_R_SIGNATURE, "wrong signature");
    CHECK_EQ(tr_acpi_reg_check("DSDT", TR_REG_BINARY, rdsdt,
                               sizeof(rdsdt) - 1), XHA_R_LENGTH, "length off");
    rdsdt[60] ^= 1;
    CHECK_EQ(tr_acpi_reg_check("DSDT", TR_REG_BINARY, rdsdt, sizeof(rdsdt)),
             XHA_R_CHECKSUM, "bad checksum");
    CHECK_EQ(tr_acpi_reg_check("DSDT", 1UL, rdsdt, sizeof(rdsdt)),
             TR_ACPI_R_TYPE, "a string");
    CHECK_EQ(tr_acpi_reg_check("FACS", TR_REG_BINARY, rfacs, sizeof(rfacs)),
             0UL, "FACS, no checksum");
    memset(&xa, 0, sizeof(xa));
    whole = tr_acpi_reg_walk(&ops, &xa, store, sizeof(store));
    CHECK_EQ(whole, 3, "the bad DSDT not saved");
    CHECK(xa.T[0].Held == TR_ACPI_HELD_NONE &&
          xa.T[0].Reason == XHA_R_CHECKSUM, "listed with its reason");
    /* Too many subkeys: no more than TR_REG_SUBKEYS read. */
    for (i = 0; i < 10; i++) {
        reg[0].Sub[i] = "TESTOE";
    }
    memset(&xa, 0, sizeof(xa));
    (void)tr_acpi_reg_walk(&ops, &xa, store, sizeof(store));
    CHECK(xa.Count <= 4 + 3, "a key's tables bounded");
}

/* NT: the root table asked for by name, its entries listed by address,
 * the SSDTs Windows withheld counted. */
static void test_nt_root(void)
{
    static unsigned char xsdt3[36 + 24];
    static unsigned char s1[100];
    char msg[2048];
    TR_TEXT t;

    memset(&a, 0, sizeof(a));
    a.Source = TR_ACPI_SRC_NT;
    make_table(s1, "SSDT", sizeof(s1), "TESTTBL7", 7);
    make_table(xsdt3, "XSDT", sizeof(xsdt3), "TESTTBL8", 0);
    memset(xsdt3 + 36, 0, 24);
    put32(xsdt3, 36, 0x7FFE1000UL);
    put32(xsdt3, 44, 0x7FFE2000UL);
    put32(xsdt3, 52, 0x7FFE3000UL);
    put32(xsdt3, 56, 1);
    mem_sum(xsdt3, sizeof(xsdt3));
    (void)tr_acpi_add(&a, s1, sizeof(s1), 3);
    (void)tr_acpi_add(&a, xsdt3, sizeof(xsdt3), 1);
    a.RootAsked = 1;
    (void)tr_acpi_names(&a);
    tr_text_init(&t, text, sizeof(text));
    tr_acpi_index(&a, &t);
    CHECK(strstr(text, "The XSDT lists 3 tables, at these addresses; Windows "
                       "gave 1 of them") != NULL, "root entries counted");
    CHECK(strstr(text, "so 2 were not saved") != NULL, "not saved counted");
    CHECK(strstr(text, "   1  7FFE2000h") != NULL, "an entry's address");
    CHECK(strstr(text, "   2  000000017FFE3000h") != NULL,
          "an entry above 4 GB");
    CHECK(strstr(text, "asked for by name") != NULL, "asked by name");
    tr_acpi_message(TR_ACPI_M_SAVED, &a, "C:\\T", NULL, NULL, 0, 0, msg,
                    sizeof(msg));
    CHECK(strstr(msg, "of SSDT (3), the first was saved. 2 SSDTs were not "
                      "saved.") != NULL, "withheld SSDTs in the message");
    a.T[0].Listed = 2;
    tr_acpi_message(TR_ACPI_M_SAVED, &a, "C:\\T", NULL, NULL, 0, 0, msg,
                    sizeof(msg));
    CHECK(strstr(msg, "1 SSDT was not saved.") != NULL, "one SSDT, singular");
    a.T[0].Listed = 3;
    put32(xsdt3, 4, 36 + 16);
    mem_sum(xsdt3, 36 + 16);
    a.T[1].Bytes = 36 + 16;
    a.T[1].Length = 36 + 16;
    tr_text_init(&t, text, sizeof(text));
    tr_acpi_index(&a, &t);
    CHECK(strstr(text, "so 1 was not saved") != NULL, "one entry, singular");
}

int main(void)
{
    test_nt_root();
    test_xha_read();
    test_xha_answer();
    test_xha_merge();
    test_reg();
    test_builder();
    test_sums_names();
    test_door_index();
    test_messages();
    printf("test_tracpi: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
