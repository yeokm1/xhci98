/*
 * test_silent.c - host vectors for the Windows 98 SE and ME silent install's
 * pure half (src\xhci_silent.c; roadmap-hcd.md task 38.2, design record 13
 * section 6.11.7).
 *
 * The configurations are CONSTRUCTED here, one interface descriptor and one
 * endpoint per setting, in the shapes the section names (hubs, boot HID,
 * Bulk-Only, CBI, a dual-mode drive); none is read off a unit. Every
 * expectation was worked by hand from section 6.11's rule, not from the code
 * under test. The list is the package's six (6.11.8, SILENT.REG).
 */

#include <stdio.h>
#include <string.h>
#include "../src/xhci_silent.h"
#include "test_harness.h"

#define REG_NONE_T   0UL
#define REG_SZ_T     1UL
#define REG_BINARY_T 3UL
#define REG_DWORD_T  4UL

static const UCHAR six[6][4] = {
    { 0x80, 0x09, 0x00, 0x00 },
    { 0x43, 0x03, 0x01, 0x01 },
    { 0x43, 0x03, 0x01, 0x02 },
    { 0xC3, 0x08, 0x06, 0x50 },
    { 0x83, 0x03, 0x01, 0x01 },
    { 0x83, 0x03, 0x01, 0x02 } };

static UCHAR cfg[512];

static ULONG hdr(void)
{
    memset(cfg, 0xEE, sizeof(cfg));
    cfg[0] = 9; cfg[1] = 2; cfg[2] = 0; cfg[3] = 0;
    cfg[4] = 1; cfg[5] = 1; cfg[6] = 0; cfg[7] = 0x80; cfg[8] = 50;
    return 9;
}

/* An interface descriptor and one interrupt endpoint after it. */
static ULONG iface(ULONG o, UCHAR num, UCHAR alt, UCHAR cls, UCHAR sub,
                   UCHAR proto)
{
    UCHAR d[16] = { 9, 4, 0, 0, 1, 0, 0, 0, 0,
                    7, 5, 0x81, 3, 8, 0, 10 };
    d[2] = num; d[3] = alt; d[5] = cls; d[6] = sub; d[7] = proto;
    memcpy(cfg + o, d, sizeof(d));
    return o + sizeof(d);
}

static ULONG raw(ULONG o, const UCHAR *bytes, ULONG n)
{
    memcpy(cfg + o, bytes, n);
    return o + n;
}

static void total(ULONG t)
{
    cfg[2] = (UCHAR)(t & 0xFF);
    cfg[3] = (UCHAR)(t >> 8);
}

/* A one-setting configuration of the given triple; returns its length. */
static ULONG one(UCHAR cls, UCHAR sub, UCHAR proto)
{
    ULONG o;

    o = iface(hdr(), 0, 0, cls, sub, proto);
    total(o);
    return o;
}

static ULONG walk6(ULONG len, ULONG composite)
{
    return XhciSilentWalk(six, 6, cfg, len, composite);
}

static void test_gate(void)
{
    CHECK_EQ(XhciSilentGate(0, 0), 0, "gate: absent");
    CHECK_EQ(XhciSilentGate(0, 1), 0, "gate: another type (not found)");
    CHECK_EQ(XhciSilentGate(1, 0), 0, "gate: 0");
    CHECK_EQ(XhciSilentGate(1, 2), 0, "gate: 2");
    CHECK_EQ(XhciSilentGate(1, 0xFFFFFFFFUL), 0, "gate: 0xFFFFFFFF");
    CHECK_EQ(XhciSilentGate(1, 1), 1, "gate: a DWORD 1");
}

static void test_admission(void)
{
    XHCI_SILENT t;
    UCHAR v[5];
    UCHAR e[4];
    ULONG i;
    static const UCHAR ign[5] = { 0x00, 0x01, 0x02, 0x03, 0x3F };

    XhciSilentReset(&t);
    v[1] = 3; v[2] = 1; v[3] = 1; v[4] = 0;
    v[0] = 0x80;
    CHECK_EQ(XhciSilentAdd(&t, REG_BINARY_T, v, 4), XHCI_SILENT_KEPT, "0x80 kept");
    v[0] = 0x40;
    CHECK_EQ(XhciSilentAdd(&t, REG_BINARY_T, v, 4), XHCI_SILENT_KEPT, "0x40 kept");
    v[0] = 0xC0;
    CHECK_EQ(XhciSilentAdd(&t, REG_BINARY_T, v, 4), XHCI_SILENT_KEPT, "0xC0 kept");
    CHECK_EQ(t.Count, 3, "three kept");
    for (i = 0; i < 5; i++) {
        v[0] = ign[i];
        CHECK_EQ(XhciSilentAdd(&t, REG_BINARY_T, v, 4), XHCI_SILENT_IGNORED,
                 "no 0x80/0x40: ignored");
    }
    v[0] = 0x80;
    CHECK_EQ(XhciSilentAdd(&t, REG_DWORD_T, v, 4), XHCI_SILENT_IGNORED,
             "REG_DWORD of 4 bytes ignored");
    CHECK_EQ(XhciSilentAdd(&t, REG_SZ_T, v, 4), XHCI_SILENT_IGNORED,
             "REG_SZ ignored");
    CHECK_EQ(XhciSilentAdd(&t, REG_NONE_T, v, 4), XHCI_SILENT_IGNORED,
             "REG_NONE ignored");
    CHECK_EQ(XhciSilentAdd(&t, REG_BINARY_T, v, 0), XHCI_SILENT_IGNORED,
             "length 0 ignored");
    CHECK_EQ(XhciSilentAdd(&t, REG_BINARY_T, v, 3), XHCI_SILENT_IGNORED,
             "length 3 ignored");
    CHECK_EQ(XhciSilentAdd(&t, REG_BINARY_T, v, 5), XHCI_SILENT_IGNORED,
             "length 5 ignored");
    CHECK_EQ(XhciSilentAdd(&t, REG_BINARY_T, NULL, 4), XHCI_SILENT_IGNORED,
             "NULL data ignored");
    CHECK_EQ(t.Count, 3, "an ignored entry takes no slot");
    CHECK_EQ(t.Ignored, 12, "every ignored value counted");
    CHECK_EQ(t.Dropped, 0, "nothing dropped yet");
    CHECK_EQ(t.Entry[0][0], 0x80, "slot 0 is the first kept");
    CHECK_EQ(t.Entry[1][0], 0x40, "slot 1 the second");
    CHECK_EQ(t.Entry[2][0], 0xC0, "slot 2 the third");

    XhciSilentReset(&t);
    for (i = 0; i < 33; i++) {
        e[0] = 0x80; e[1] = (UCHAR)i; e[2] = 0; e[3] = 0;
        CHECK_EQ(XhciSilentAdd(&t, REG_BINARY_T, e, 4),
                 (i < 32) ? XHCI_SILENT_KEPT : XHCI_SILENT_DROPPED,
                 "the first 32 kept, the 33rd dropped");
    }
    CHECK_EQ(t.Count, 32, "the cap");
    CHECK_EQ(t.Dropped, 1, "the 33rd counted");
    CHECK_EQ(t.Ignored, 0, "none ignored");
    for (i = 0; i < 32; i++) {
        CHECK_EQ(t.Entry[i][1], i, "kept in order");
    }
    XhciSilentReset(&t);
    CHECK_EQ(t.Count, 0, "reset: no entries");
    CHECK_EQ(t.Dropped, 0, "reset: no drops");
    CHECK_EQ(t.Ignored, 0, "reset: nothing ignored");
    CHECK_EQ(t.Entry[5][1], 0, "reset: the entries cleared");
}

static void test_failed_enum(void)
{
    XHCI_SILENT t;
    UCHAR e[4] = { 0x83, 0x03, 0x01, 0x02 };
    UCHAR bad[4] = { 0x03, 0x03, 0x01, 0x02 };
    ULONG len;

    len = one(3, 1, 2);
    XhciSilentReset(&t);
    XhciSilentAdd(&t, REG_BINARY_T, e, 4);
    XhciSilentAdd(&t, REG_BINARY_T, bad, 4);
    XhciSilentEnd(&t, 0);
    CHECK_EQ(t.Count, 0, "failed query: Count 0");
    CHECK_EQ(t.Ignored, 1, "failed query: Ignored kept");
    CHECK_EQ(XhciSilentMatch((const UCHAR (*)[4])t.Entry, t.Count, cfg, len, 0),
             0, "failed query: the kept entry no longer matches");

    XhciSilentReset(&t);
    XhciSilentAdd(&t, REG_BINARY_T, e, 4);
    XhciSilentAdd(&t, REG_BINARY_T, bad, 4);
    XhciSilentEnd(&t, 1);
    CHECK_EQ(t.Count, 1, "good query: the entry kept");
    CHECK_EQ(XhciSilentMatch((const UCHAR (*)[4])t.Entry, t.Count, cfg, len, 0),
             1, "good query: it matches");
}

static ULONG m1(UCHAR c0, UCHAR c1, UCHAR c2, UCHAR c3, ULONG len,
                ULONG composite)
{
    UCHAR e[1][4];

    e[0][0] = c0; e[0][1] = c1; e[0][2] = c2; e[0][3] = c3;
    return XhciSilentMatch((const UCHAR (*)[4])e, 1, cfg, len, composite);
}

static void test_control_bits(void)
{
    ULONG len;

    len = one(0x03, 0x05, 0x07);
    CHECK_EQ(m1(0x80, 0x03, 0x00, 0x00, len, 0), 1, "0x80: class only");
    CHECK_EQ(m1(0x81, 0x03, 0x05, 0x00, len, 0), 1, "0x81: subclass, not protocol");
    CHECK_EQ(m1(0x81, 0x03, 0x06, 0x07, len, 0), 0, "0x81: subclass compared");
    CHECK_EQ(m1(0x82, 0x03, 0x00, 0x07, len, 0), 1, "0x82: protocol, not subclass");
    CHECK_EQ(m1(0x82, 0x03, 0x05, 0x08, len, 0), 0, "0x82: protocol compared");
    CHECK_EQ(m1(0x83, 0x03, 0x05, 0x07, len, 0), 1, "0x83: both");
    CHECK_EQ(m1(0x83, 0x03, 0x05, 0x08, len, 0), 0, "0x83: protocol differs");
    CHECK_EQ(m1(0x83, 0x03, 0x04, 0x07, len, 0), 0, "0x83: subclass differs");
    CHECK_EQ(m1(0x80, 0x04, 0x05, 0x07, len, 0), 0, "class mismatch, 0x80");
    CHECK_EQ(m1(0x83, 0x04, 0x05, 0x07, len, 0), 0, "class mismatch, 0x83");
}

static void test_composite(void)
{
    ULONG len;

    len = one(0x03, 0x01, 0x01);
    CHECK_EQ(m1(0x80, 0x03, 0, 0, len, 1), 0, "0x80 not on a composite");
    CHECK_EQ(m1(0x40, 0x03, 0, 0, len, 0), 0, "0x40 not on a single device");
    CHECK_EQ(m1(0x40, 0x03, 0, 0, len, 1), 1, "0x40 on a composite");
    CHECK_EQ(m1(0xC3, 0x03, 0x01, 0x01, len, 0), 1, "0xC3 single");
    CHECK_EQ(m1(0xC3, 0x03, 0x01, 0x01, len, 1), 1, "0xC3 composite");
}

static void test_six(void)
{
    ULONG len;
    ULONG o;

    len = one(0x09, 0x00, 0x00);
    CHECK_EQ(walk6(len, 0), XHCI_SILENT_MATCH, "FS hub");
    o = iface(hdr(), 0, 0, 0x09, 0x00, 0x01);
    o = iface(o, 0, 1, 0x09, 0x00, 0x02);
    total(o);
    CHECK_EQ(walk6(o, 0), XHCI_SILENT_MATCH, "HS multi-TT hub");
    len = one(0x09, 0x00, 0x03);
    CHECK_EQ(walk6(len, 0), XHCI_SILENT_MATCH, "SS hub");

    len = one(0x03, 0x01, 0x01);
    CHECK_EQ(walk6(len, 0), XHCI_SILENT_MATCH, "boot keyboard alone");
    CHECK_EQ(walk6(len, 1), XHCI_SILENT_MATCH, "boot keyboard function");
    len = one(0x03, 0x01, 0x02);
    CHECK_EQ(walk6(len, 0), XHCI_SILENT_MATCH, "boot mouse alone");
    CHECK_EQ(walk6(len, 1), XHCI_SILENT_MATCH, "boot mouse function");
    len = one(0x03, 0x00, 0x00);
    CHECK_EQ(walk6(len, 0), XHCI_SILENT_NOMATCH, "non-boot HID alone");
    CHECK_EQ(walk6(len, 1), XHCI_SILENT_NOMATCH, "non-boot HID function");
    len = one(0x08, 0x06, 0x50);
    CHECK_EQ(walk6(len, 0), XHCI_SILENT_MATCH, "Bulk-Only stick alone");
    CHECK_EQ(walk6(len, 1), XHCI_SILENT_MATCH, "Bulk-Only function");
    len = one(0x08, 0x04, 0x00);
    CHECK_EQ(walk6(len, 0), XHCI_SILENT_NOMATCH, "CBI floppy");
    len = one(0x08, 0x06, 0x62);
    CHECK_EQ(walk6(len, 0), XHCI_SILENT_NOMATCH, "UAS alone: not listed");
    o = iface(hdr(), 0, 0, 0x08, 0x06, 0x50);
    o = iface(o, 0, 1, 0x08, 0x06, 0x62);
    total(o);
    CHECK_EQ(walk6(o, 0), XHCI_SILENT_MATCH, "dual-mode: alternate 0 matches");
    /* A hub is never composite, so the hub entry does not apply to one. */
    len = one(0x09, 0x00, 0x00);
    CHECK_EQ(walk6(len, 1), XHCI_SILENT_NOMATCH, "hub entry: single only");
}

static void test_sub_proto(void)
{
    ULONG o;

    o = iface(hdr(), 0, 0, 0x03, 0x00, 0x00);
    o = iface(o, 0, 1, 0x03, 0x01, 0x01);
    total(o);
    CHECK_EQ(walk6(o, 0), XHCI_SILENT_MATCH, "match on a non-zero alternate");
    o = iface(hdr(), 0, 0, 0x01, 0x01, 0x00);
    o = iface(o, 1, 0, 0x01, 0x02, 0x00);
    o = iface(o, 2, 0, 0x03, 0x00, 0x00);
    o = iface(o, 3, 0, 0x08, 0x06, 0x50);
    total(o);
    CHECK_EQ(walk6(o, 0), XHCI_SILENT_MATCH, "match on the last of several");
}

static void test_transport(void)
{
    ULONG o;

    o = iface(hdr(), 0, 0, 0x08, 0x06, 0x50);
    o = iface(o, 0, 1, 0x08, 0x06, 0x62);
    total(o);
    CHECK_EQ(XhciSilentPdo(1, six, 6, cfg, o, 0, 1), XHCI_SILENT_BARRED,
             "UAS or refused: barred");
    CHECK_EQ(XhciSilentPdo(1, six, 6, cfg, o, 1, 1), XHCI_SILENT_BARRED,
             "barred as a function too");
    CHECK_EQ(XhciSilentPdo(1, six, 6, cfg, o, 0, 0), XHCI_SILENT_MATCH,
             "Bulk-Only: the rule");
    CHECK_EQ(XhciSilentPdo(0, six, 6, cfg, o, 0, 0), XHCI_SILENT_OFF,
             "gate 0: off with a matching table");
    CHECK_EQ(XhciSilentPdo(1, six, 0, cfg, o, 0, 0), XHCI_SILENT_EMPTY,
             "gate 1, no entries: empty");
    CHECK_EQ(XhciSilentPdo(1, NULL, 3, cfg, o, 0, 0), XHCI_SILENT_EMPTY,
             "gate 1, NULL table: empty");
    CHECK_EQ(XhciSilentPdo(1, NULL, 0, cfg, o, 0, 1), XHCI_SILENT_EMPTY,
             "empty before barred");
}

static void test_empty(void)
{
    ULONG len;

    len = one(0x09, 0x00, 0x00);
    CHECK_EQ(XhciSilentWalk(six, 0, cfg, len, 0), XHCI_SILENT_NOMATCH,
             "count 0 with a real table");
    CHECK_EQ(XhciSilentWalk(NULL, 0, cfg, len, 0), XHCI_SILENT_NOMATCH,
             "NULL table, count 0");
    CHECK_EQ(XhciSilentWalk(NULL, 3, cfg, len, 0), XHCI_SILENT_NOMATCH,
             "NULL table, count 3");
    CHECK_EQ(XhciSilentMatch(NULL, 3, cfg, len, 0), 0, "NULL table: 0");
    CHECK_EQ(XhciSilentWalk(NULL, 3, NULL, 0, 0), XHCI_SILENT_NOMATCH,
             "NULL table before the configuration is read");
}

static void test_header(void)
{
    ULONG len;
    ULONG o;

    len = one(0x09, 0x00, 0x00);
    cfg[0] = 8;
    CHECK_EQ(walk6(len, 0), XHCI_SILENT_MALFORMED, "header bLength 8");
    len = one(0x09, 0x00, 0x00);
    cfg[1] = 4;
    CHECK_EQ(walk6(len, 0), XHCI_SILENT_MALFORMED, "bDescriptorType 4 at 0");
    len = one(0x09, 0x00, 0x00);
    total(8);
    CHECK_EQ(walk6(len, 0), XHCI_SILENT_MALFORMED, "wTotalLength 8");
    /* A 12-byte header and wTotalLength 10. */
    hdr();
    cfg[0] = 12;
    o = iface(12, 0, 0, 0x09, 0x00, 0x00);
    total(10);
    CHECK_EQ(walk6(o, 0), XHCI_SILENT_MALFORMED, "wTotalLength below bLength");
    /* The same header with a sound wTotalLength walks. */
    total(o);
    CHECK_EQ(walk6(o, 0), XHCI_SILENT_MATCH, "a 12-byte header walks");
    len = one(0x09, 0x00, 0x00);
    CHECK_EQ(walk6(8, 0), XHCI_SILENT_MALFORMED, "length below 9");
    CHECK_EQ(XhciSilentWalk(six, 6, NULL, len, 0), XHCI_SILENT_MALFORMED,
             "config NULL");
    CHECK_EQ(walk6(0, 0), XHCI_SILENT_MALFORMED, "length 0");
    CHECK_EQ(XhciSilentMatch(six, 6, NULL, len, 0), 0, "config NULL: 0");
}

static void test_lengths(void)
{
    ULONG o;
    ULONG t;

    o = one(0x09, 0x00, 0x00);
    total(o + 1);
    CHECK_EQ(walk6(o, 0), XHCI_SILENT_MALFORMED,
             "wTotalLength past length, match inside: malformed");
    CHECK_EQ(XhciSilentMatch(six, 6, cfg, o, 0), 0, "... and 0");

    t = iface(hdr(), 0, 0, 0x03, 0x00, 0x00);
    o = iface(t, 1, 0, 0x09, 0x00, 0x00);
    total(t);
    CHECK_EQ(walk6(o, 0), XHCI_SILENT_NOMATCH,
             "a match only past wTotalLength: no match");
    CHECK_EQ(XhciSilentMatch(six, 6, cfg, o, 0), 0, "... and 0");

    t = one(0x09, 0x00, 0x00);
    CHECK_EQ(walk6(t + 20, 0), XHCI_SILENT_MATCH,
             "the match inside, junk past wTotalLength");
    CHECK_EQ(XhciSilentMatch(six, 6, cfg, t + 20, 0), 1, "... and 1");
}

static void test_malformed(void)
{
    static const UCHAR zero[2] = { 0x00, 0x24 };
    static const UCHAR oneb[2] = { 0x01, 0x24 };
    static const UCHAR past[4] = { 0x08, 0x24, 0x00, 0x00 };
    static const UCHAR short4[5] = { 0x05, 0x04, 0x00, 0x00, 0x00 };
    static const UCHAR iad7[7] = { 0x07, 0x0B, 0x00, 0x02, 0x03, 0x01, 0x01 };
    static const UCHAR iad8[8] = { 0x08, 0x0B, 0x00, 0x02, 0x03, 0x01, 0x01,
                                   0x00 };
    UCHAR longcs[64];
    ULONG o;

    o = raw(hdr(), zero, 2);
    o = iface(o, 0, 0, 0x09, 0x00, 0x00);
    total(o);
    CHECK_EQ(walk6(o, 0), XHCI_SILENT_MALFORMED, "bLength 0");
    o = raw(hdr(), oneb, 2);
    o = iface(o, 0, 0, 0x09, 0x00, 0x00);
    total(o);
    CHECK_EQ(walk6(o, 0), XHCI_SILENT_MALFORMED, "bLength 1");
    o = iface(hdr(), 0, 0, 0x09, 0x00, 0x00);
    o = raw(o, past, 4);
    total(o);
    CHECK_EQ(walk6(o + 8, 0), XHCI_SILENT_MALFORMED,
             "a descriptor running past wTotalLength");
    o = iface(hdr(), 0, 0, 0x09, 0x00, 0x00);
    cfg[o] = 0x02;
    total(o + 1);
    CHECK_EQ(walk6(o + 1, 0), XHCI_SILENT_MALFORMED,
             "one trailing byte after a matching interface");
    o = raw(hdr(), short4, 5);
    o = iface(o, 0, 0, 0x09, 0x00, 0x00);
    total(o);
    CHECK_EQ(walk6(o, 0), XHCI_SILENT_MALFORMED, "an interface of 5 bytes");
    o = raw(hdr(), iad7, 7);
    o = iface(o, 0, 0, 0x03, 0x01, 0x01);
    total(o);
    CHECK_EQ(walk6(o, 1), XHCI_SILENT_MALFORMED, "an IAD of 7 bytes");
    o = raw(hdr(), iad8, 8);
    o = iface(o, 0, 0, 0x03, 0x01, 0x01);
    total(o);
    CHECK_EQ(walk6(o, 1), XHCI_SILENT_MATCH, "an IAD of 8 bytes walks");
    o = iface(hdr(), 0, 0, 0x09, 0x00, 0x00);
    o = raw(o, zero, 2);
    total(o);
    CHECK_EQ(walk6(o, 0), XHCI_SILENT_MALFORMED,
             "a match before a malformed descriptor");
    CHECK_EQ(XhciSilentMatch(six, 6, cfg, o, 0), 0, "... is 0 (O4)");
    memset(longcs, 0, sizeof(longcs));
    longcs[0] = sizeof(longcs);
    longcs[1] = 0x24;
    o = raw(hdr(), longcs, sizeof(longcs));
    o = iface(o, 0, 0, 0x09, 0x00, 0x00);
    total(o);
    CHECK_EQ(XhciSilentMatch(six, 6, cfg, o, 0), 1,
             "a match after a long class-specific descriptor");
}

static void test_boolean_trap(void)
{
    ULONG len;
    ULONG r;
    ULONG c;

    len = one(0x03, 0x01, 0x01);
    r = m1(0x83, 0x03, 0x01, 0x01, len, 0);
    CHECK_EQ(r, 1, "0x83 match is exactly 1");
    r = m1(0x01, 0x03, 0x01, 0x01, len, 0);
    CHECK_EQ(r, 0, "0x01 single: 0");
    r = m1(0x01, 0x03, 0x01, 0x01, len, 1);
    CHECK_EQ(r, 0, "0x01 composite: 0");
    for (c = 0; c < 256; c++) {
        r = m1((UCHAR)c, 0x03, 0x01, 0x01, len, 0);
        CHECK(r == 0 || r == 1, "every result is 0 or 1 (single)");
        CHECK_EQ(r, ((c & 0x80) != 0) ? 1UL : 0UL, "single: 0x80 decides");
        r = m1((UCHAR)c, 0x03, 0x01, 0x01, len, 1);
        CHECK(r == 0 || r == 1, "every result is 0 or 1 (composite)");
        CHECK_EQ(r, ((c & 0x40) != 0) ? 1UL : 0UL, "composite: 0x40 decides");
    }
}

int main(void)
{
    test_gate();
    test_admission();
    test_failed_enum();
    test_control_bits();
    test_composite();
    test_six();
    test_sub_proto();
    test_transport();
    test_empty();
    test_header();
    test_lengths();
    test_malformed();
    test_boolean_trap();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
