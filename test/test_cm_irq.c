/*
 * test_cm_irq.c - the pure half of the interrupt a child filter gives a
 * function hot-added behind a tunnel on Windows 2000's Standard PC HAL
 * (src/cm/cm_irq.c; design record 21 section 6.2.1; task 44.53): the
 * HAL's id, the wire's swizzle, the requirements list with an interrupt
 * added, and the start's lists with it taken out again.
 */

#include <stdio.h>
#include <string.h>
#include "../src/cm/cm_irq.h"
#include "test_harness.h"

static void put32(UCHAR *p, ULONG v)
{
    p[0] = (UCHAR)(v & 0xFF);
    p[1] = (UCHAR)((v >> 8) & 0xFF);
    p[2] = (UCHAR)((v >> 16) & 0xFF);
    p[3] = (UCHAR)((v >> 24) & 0xFF);
}

static ULONG get32(const UCHAR *p)
{
    return (ULONG)p[0] | ((ULONG)p[1] << 8) | ((ULONG)p[2] << 16) |
           ((ULONG)p[3] << 24);
}

/* ---- the HAL ---------------------------------------------------------- */

static void wide(USHORT *to, const char *from, ULONG chars)
{
    ULONG i;

    for (i = 0; i < chars; i++) {
        to[i] = (USHORT)(UCHAR)from[i];
    }
}

static void test_hal(void)
{
    USHORT buf[32];

    wide(buf, "e_isa_up\0\0", 10);
    CHECK(CmIrqHalStdPc(buf, 10), "e_isa_up is the Standard PC HAL");
    wide(buf, "E_ISA_UP\0\0", 10);
    CHECK(CmIrqHalStdPc(buf, 10), "case is ignored");
    wide(buf, "e_isa_up\0", 9);
    CHECK(CmIrqHalStdPc(buf, 9), "a plain string reads the same");
    wide(buf, "e_isa_up", 8);
    CHECK(!CmIrqHalStdPc(buf, 8), "an unterminated string is no");
    wide(buf, "e_isa_upx\0\0", 11);
    CHECK(!CmIrqHalStdPc(buf, 11), "a longer string is no");
    wide(buf, "acpipic_up\0\0", 12);
    CHECK(!CmIrqHalStdPc(buf, 12), "the ACPI PIC HAL is no");
    wide(buf, "mps_up\0\0", 8);
    CHECK(!CmIrqHalStdPc(buf, 8), "an MPS HAL is no");
    wide(buf, "acpiapic_up\0e_isa_up\0\0", 22);
    CHECK(!CmIrqHalStdPc(buf, 22), "only the first string counts");
    wide(buf, "\0\0", 2);
    CHECK(!CmIrqHalStdPc(buf, 2), "an empty list is no");
    CHECK(!CmIrqHalStdPc(NULL, 10), "no list is no");
}

/* ---- the wire --------------------------------------------------------- */

static void test_wire(void)
{
    ULONG devs[CM_IRQ_PATH_MAX + 1];
    ULONG why;
    ULONG pin;
    ULONG port;
    ULONG d;
    ULONG n;
    ULONG line;
    ULONG want;
    ULONG fails;

    /* 44.50's chain: the drive 05:00.0 pin A below 04:00.0 and 03:00.0,
     * the tunnel port 02:00.0 pin A, Line 0Bh. */
    devs[0] = 0;
    devs[1] = 0;
    devs[2] = 0;
    CHECK_EQ(CmIrqWire(1, devs, 3, 1, 0x0B, &why), 0x0B, "44.50's drive gets IRQ 11");
    CHECK_EQ(why, CM_IRQ_WHY_GIVEN, "given");
    CHECK_EQ(CmIrqWire(1, devs, 1, 1, 0x0B, &why), 0x0B, "the direct shape, depth 1");

    /* Every pin pair and device number at depth 1 to the bound. */
    fails = 0;
    for (n = 1; n <= CM_IRQ_PATH_MAX; n++) {
        for (d = 0; d < 32; d++) {
            for (pin = 1; pin <= 4; pin++) {
                for (port = 1; port <= 4; port++) {
                    ULONG k;
                    for (k = 0; k < n; k++) {
                        devs[k] = (k == n - 1) ? d : (k + 1) % 32;
                    }
                    {
                        ULONG sum = 0;
                        for (k = 0; k < n; k++) {
                            sum += devs[k];
                        }
                        want = (((pin - 1 + sum) & 3) == port - 1) ? 10 : 0;
                    }
                    line = CmIrqWire(pin, devs, n, port, 10, &why);
                    if (line != want ||
                        why != (want ? CM_IRQ_WHY_GIVEN : CM_IRQ_WHY_OTHER_PIN)) {
                        fails++;
                    }
                }
            }
        }
    }
    CHECK_EQ(fails, 0, "the swizzle over depths 1 to 8, devices 0 to 31, every pin pair");

    devs[0] = 1;
    CHECK_EQ(CmIrqWire(1, devs, 1, 1, 11, &why), 0, "device 1 pin A arrives as B");
    CHECK_EQ(why, CM_IRQ_WHY_OTHER_PIN, "another of the port's pins");
    CHECK_EQ(CmIrqWire(4, devs, 1, 1, 11, &why), 11, "device 1 pin D arrives as A");
    devs[0] = 0;
    CHECK_EQ(CmIrqWire(0, devs, 1, 1, 11, &why), 0, "pin 0: no INTx");
    CHECK_EQ(why, CM_IRQ_WHY_NO_PIN, "no pin");
    CHECK_EQ(CmIrqWire(5, devs, 1, 1, 11, &why), 0, "pin 5 is no pin");
    CHECK_EQ(CmIrqWire(1, devs, 1, 0, 11, &why), 0, "the port without a pin");
    CHECK_EQ(why, CM_IRQ_WHY_PORT_PIN, "the port's pin");
    CHECK_EQ(CmIrqWire(1, devs, 1, 1, 0, &why), 0, "Line 0");
    CHECK_EQ(why, CM_IRQ_WHY_PORT_LINE, "the port's Line");
    CHECK_EQ(CmIrqWire(1, devs, 1, 1, 0xFF, &why), 0, "Line FFh");
    CHECK_EQ(CmIrqWire(1, devs, 1, 1, 2, &why), 0, "Line 2, the cascade");
    CHECK_EQ(CmIrqWire(1, devs, 1, 1, 8, &why), 0, "Line 8, the clock");
    CHECK_EQ(CmIrqWire(1, devs, 1, 1, 13, &why), 0, "Line 13, the coprocessor");
    CHECK_EQ(CmIrqWire(1, devs, 1, 1, 16, &why), 0, "Line 16 is no ISA IRQ");
    fails = 0;
    for (line = 3; line <= 15; line++) {
        if (line == 8 || line == 13) {
            continue;
        }
        if (CmIrqWire(1, devs, 1, 1, line, &why) != line) {
            fails++;
        }
    }
    CHECK_EQ(fails, 0, "Lines 3 to 15 but 8 and 13 are taken");
    CHECK_EQ(CmIrqWire(1, devs, 0, 1, 11, &why), 0, "an empty path");
    CHECK_EQ(why, CM_IRQ_WHY_PATH, "the path");
    CHECK_EQ(CmIrqWire(1, devs, CM_IRQ_PATH_MAX + 1, 1, 11, &why), 0, "a path over the bound");
    CHECK_EQ(CmIrqWire(1, NULL, 1, 1, 11, &why), 0, "no path");
    devs[0] = 32;
    CHECK_EQ(CmIrqWire(1, devs, 1, 1, 11, &why), 0, "device 32 is no device");
    CHECK_EQ(why, CM_IRQ_WHY_PATH, "a bad device number");
}

/* ---- the requirements ------------------------------------------------- */

/* A requirements list of `alts` alternatives with `count[a]` descriptors
 * each, every descriptor of type `type` (the first of each list of type
 * `first`), into buf; returns its size. */
static ULONG reqList(UCHAR *buf, ULONG alts, const ULONG *count, UCHAR type,
                     UCHAR first)
{
    ULONG at;
    ULONG a;
    ULONG d;

    memset(buf, 0, 4096);
    at = CM_IRQ_REQ_HDR;
    put32(buf + 4, 5);              /* InterfaceType PCIBus */
    put32(buf + 8, 5);              /* BusNumber            */
    put32(buf + CM_IRQ_REQ_ALTS_AT, alts);
    for (a = 0; a < alts; a++) {
        buf[at] = 1;                /* Version              */
        buf[at + 2] = 1;            /* Revision             */
        put32(buf + at + CM_IRQ_IOL_COUNT_AT, count[a]);
        at += CM_IRQ_IOL_HDR;
        for (d = 0; d < count[a]; d++) {
            buf[at] = 0;
            buf[at + CM_IRQ_IOD_TYPE_AT] = (d == 0) ? first : type;
            put32(buf + at + 8, 0x4000);    /* a length         */
            put32(buf + at + 12, 0x1000 + d);
            at += CM_IRQ_IOD_SIZE;
        }
    }
    put32(buf, at);
    return at;
}

static void test_req(void)
{
    static UCHAR in[4096];
    static UCHAR out[4200];
    ULONG count[4];
    ULONG size;
    ULONG want;
    ULONG got;
    ULONG at;
    ULONG fails;
    ULONG a;

    /* 44.50's drive: one alternative, a memory BAR and a device-private
     * descriptor (type 81h), no interrupt. */
    count[0] = 2;
    size = reqList(in, 1, count, 0x81, 3);
    CHECK_EQ(CmIrqReqScan(in, &want), CM_IRQ_REQ_NONE, "no interrupt");
    CHECK_EQ(want, size + CM_IRQ_IOD_SIZE, "one descriptor more");
    memset(out, 0xCC, sizeof(out));
    got = CmIrqReqAdd(in, out, want, 0x0B);
    CHECK_EQ(got, want, "the new list written");
    CHECK_EQ(get32(out), want, "its ListSize");
    CHECK_EQ(get32(out + CM_IRQ_REQ_ALTS_AT), 1, "one alternative still");
    CHECK_EQ(get32(out + CM_IRQ_REQ_HDR + CM_IRQ_IOL_COUNT_AT), 3, "three descriptors");
    CHECK(memcmp(out + 4, in + 4, CM_IRQ_REQ_HDR - 4) == 0, "the header kept");
    CHECK(memcmp(out + CM_IRQ_REQ_HDR + CM_IRQ_IOL_HDR, in + CM_IRQ_REQ_HDR + CM_IRQ_IOL_HDR,
                 2 * CM_IRQ_IOD_SIZE) == 0, "the old descriptors kept, in order");
    at = CM_IRQ_REQ_HDR + CM_IRQ_IOL_HDR + 2 * CM_IRQ_IOD_SIZE;
    CHECK_EQ(out[at], 0, "Option 0, required");
    CHECK_EQ(out[at + CM_IRQ_IOD_TYPE_AT], CM_IRQ_TYPE_INTERRUPT, "an interrupt");
    CHECK_EQ(out[at + CM_IRQ_IOD_SHARE_AT], CM_IRQ_SHARE_SHARED, "shared");
    CHECK_EQ(out[at + 3], 0, "Spare1 zero");
    CHECK_EQ(out[at + CM_IRQ_IOD_FLAGS_AT] | (out[at + CM_IRQ_IOD_FLAGS_AT + 1] << 8),
             CM_IRQ_FLAGS_LEVEL, "level-sensitive");
    CHECK_EQ(out[at + 6] | out[at + 7], 0, "Spare2 zero");
    CHECK_EQ(get32(out + at + CM_IRQ_IOD_MIN_AT), 0x0B, "MinimumVector the Line");
    CHECK_EQ(get32(out + at + CM_IRQ_IOD_MAX_AT), 0x0B, "MaximumVector the Line");
    CHECK_EQ(get32(out + at + 16) | get32(out + at + 20) | get32(out + at + 24) |
             get32(out + at + 28), 0, "the rest of the union zero");
    CHECK_EQ(CmIrqReqScan(out, &want), CM_IRQ_REQ_HAS, "the new list holds an interrupt");
    CHECK_EQ(want, 0, "and no new size");

    /* Several alternatives, one descriptor added to each. */
    count[0] = 2;
    count[1] = 0;
    count[2] = 3;
    size = reqList(in, 3, count, 3, 3);
    CHECK_EQ(CmIrqReqScan(in, &want), CM_IRQ_REQ_NONE, "three alternatives, no interrupt");
    CHECK_EQ(want, size + 3 * CM_IRQ_IOD_SIZE, "three descriptors more");
    got = CmIrqReqAdd(in, out, want, 10);
    CHECK_EQ(got, want, "written");
    at = CM_IRQ_REQ_HDR;
    fails = 0;
    for (a = 0; a < 3; a++) {
        ULONG n = get32(out + at + CM_IRQ_IOL_COUNT_AT);
        if (n != count[a] + 1) {
            fails++;
        }
        at += CM_IRQ_IOL_HDR + n * CM_IRQ_IOD_SIZE;
        if (out[at - CM_IRQ_IOD_SIZE + CM_IRQ_IOD_TYPE_AT] != CM_IRQ_TYPE_INTERRUPT ||
            get32(out + at - CM_IRQ_IOD_SIZE + CM_IRQ_IOD_MIN_AT) != 10) {
            fails++;
        }
    }
    CHECK_EQ(fails, 0, "each alternative ends with the interrupt");
    CHECK_EQ(at, want, "and the walk ends at ListSize");

    /* An interrupt already present, in any alternative. */
    count[0] = 2;
    count[1] = 1;
    reqList(in, 2, count, 3, 3);
    in[CM_IRQ_REQ_HDR + CM_IRQ_IOL_HDR + 2 * CM_IRQ_IOD_SIZE + CM_IRQ_IOL_HDR +
       CM_IRQ_IOD_TYPE_AT] = (UCHAR)CM_IRQ_TYPE_INTERRUPT;
    CHECK_EQ(CmIrqReqScan(in, &want), CM_IRQ_REQ_HAS, "an interrupt in the second alternative");
    CHECK_EQ(CmIrqReqAdd(in, out, 4096, 11), 0, "nothing added");

    /* Malformed lists are left. */
    count[0] = 2;
    size = reqList(in, 1, count, 3, 3);
    put32(in, size + 4);
    CHECK_EQ(CmIrqReqScan(in, &want), CM_IRQ_REQ_BAD, "a ListSize past the lists");
    put32(in, size - 4);
    CHECK_EQ(CmIrqReqScan(in, &want), CM_IRQ_REQ_BAD, "a ListSize short of them");
    put32(in, size);
    put32(in + CM_IRQ_REQ_ALTS_AT, 0);
    CHECK_EQ(CmIrqReqScan(in, &want), CM_IRQ_REQ_BAD, "no alternative list");
    put32(in + CM_IRQ_REQ_ALTS_AT, 2);
    CHECK_EQ(CmIrqReqScan(in, &want), CM_IRQ_REQ_BAD, "an alternative count past ListSize");
    put32(in + CM_IRQ_REQ_ALTS_AT, 1);
    put32(in + CM_IRQ_REQ_HDR + CM_IRQ_IOL_COUNT_AT, 0x40000000UL);
    CHECK_EQ(CmIrqReqScan(in, &want), CM_IRQ_REQ_BAD, "a huge descriptor count");
    put32(in, 16);
    CHECK_EQ(CmIrqReqScan(in, &want), CM_IRQ_REQ_BAD, "a ListSize under the header");
    count[0] = 127;
    size = reqList(in, 1, count, 3, 3);
    CHECK(size > CM_IRQ_REQ_MAX_BYTES, "a list over 4 KiB");
    CHECK_EQ(CmIrqReqScan(in, &want), CM_IRQ_REQ_BAD, "is left");
    CHECK_EQ(CmIrqReqScan(NULL, &want), CM_IRQ_REQ_BAD, "no list");
    count[0] = 0;
    size = reqList(in, 1, count, 3, 3);
    CHECK_EQ(CmIrqReqScan(in, &want), CM_IRQ_REQ_NONE, "an empty alternative takes one");
    CHECK_EQ(want, size + CM_IRQ_IOD_SIZE, "its new size");

    /* The add's own checks. */
    count[0] = 2;
    size = reqList(in, 1, count, 3, 3);
    CHECK_EQ(CmIrqReqAdd(in, out, size, 11), 0, "an output of the old size is refused");
    CHECK_EQ(CmIrqReqAdd(in, NULL, size + CM_IRQ_IOD_SIZE, 11), 0, "no output");
}

/* ---- the start -------------------------------------------------------- */

/* A CM_RESOURCE_LIST of `fulls` full descriptors with parts[f] partial
 * descriptors each; types[] gives each partial descriptor's type in order
 * across the list, levels[] its level. Returns its size. */
static ULONG cmList(UCHAR *buf, ULONG fulls, const ULONG *parts,
                    const UCHAR *types, const ULONG *levels)
{
    ULONG at;
    ULONG f;
    ULONG p;
    ULONG k;

    memset(buf, 0, 2048);
    put32(buf, fulls);
    at = CM_IRQ_CML_HDR;
    k = 0;
    for (f = 0; f < fulls; f++) {
        put32(buf + at, 5);
        put32(buf + at + 4, f);
        buf[at + 8] = 1;
        buf[at + 10] = 1;
        put32(buf + at + CM_IRQ_CMF_COUNT_AT, parts[f]);
        at += CM_IRQ_CMF_HDR;
        for (p = 0; p < parts[f]; p++) {
            buf[at] = types[k];
            buf[at + 1] = 3;
            put32(buf + at + CM_IRQ_CMD_LEVEL_AT, levels[k]);
            put32(buf + at + 8, 0x100 + k);
            at += CM_IRQ_CMD_SIZE;
            k++;
        }
    }
    return at;
}

static void test_start(void)
{
    static UCHAR raw[2048];
    static UCHAR tr[2048];
    static UCHAR out[2048];
    CM_IRQ_STRIP s;
    ULONG parts[3];
    UCHAR types[8];
    ULONG levels[8];
    ULONG rsize;
    ULONG got;

    /* 44.50's drive: memory, device-private, the added interrupt last. */
    parts[0] = 3;
    types[0] = 3; types[1] = 0x81; types[2] = (UCHAR)CM_IRQ_TYPE_INTERRUPT;
    levels[0] = 0; levels[1] = 0; levels[2] = 11;
    rsize = cmList(raw, 1, parts, types, levels);
    levels[2] = 0x3B;               /* a translated vector differs */
    cmList(tr, 1, parts, types, levels);
    CHECK_EQ(CmIrqStartScan(raw, tr, 11, &s), CM_IRQ_START_STRIP, "the added interrupt last");
    CHECK_EQ(s.Bytes, rsize, "the list's size");
    CHECK_EQ(s.At, CM_IRQ_CML_HDR + CM_IRQ_CMF_HDR + 2 * CM_IRQ_CMD_SIZE, "its offset");
    got = CmIrqStartStrip(raw, out, rsize - CM_IRQ_CMD_SIZE, &s);
    CHECK_EQ(got, rsize - CM_IRQ_CMD_SIZE, "the raw copy");
    CHECK_EQ(get32(out + CM_IRQ_CML_HDR + CM_IRQ_CMF_COUNT_AT), 2, "two partial descriptors left");
    CHECK(memcmp(out, raw, CM_IRQ_CML_HDR + CM_IRQ_CMF_COUNT_AT) == 0, "the header kept");
    CHECK(memcmp(out + CM_IRQ_CML_HDR + CM_IRQ_CMF_HDR, raw + CM_IRQ_CML_HDR + CM_IRQ_CMF_HDR,
                 2 * CM_IRQ_CMD_SIZE) == 0, "the others kept");
    got = CmIrqStartStrip(tr, out, rsize - CM_IRQ_CMD_SIZE, &s);
    CHECK_EQ(got, rsize - CM_IRQ_CMD_SIZE, "the translated copy, by position");
    CHECK(out[CM_IRQ_CML_HDR + CM_IRQ_CMF_HDR + CM_IRQ_CMD_SIZE] == 0x81, "its second kept");

    /* First, and alone. */
    types[0] = (UCHAR)CM_IRQ_TYPE_INTERRUPT; types[1] = 3; types[2] = 0x81;
    levels[0] = 11; levels[1] = 0; levels[2] = 0;
    rsize = cmList(raw, 1, parts, types, levels);
    levels[0] = 0x3B;
    cmList(tr, 1, parts, types, levels);
    CHECK_EQ(CmIrqStartScan(raw, tr, 11, &s), CM_IRQ_START_STRIP, "the interrupt first");
    got = CmIrqStartStrip(raw, out, rsize - CM_IRQ_CMD_SIZE, &s);
    CHECK_EQ(got, rsize - CM_IRQ_CMD_SIZE, "stripped");
    CHECK_EQ(out[CM_IRQ_CML_HDR + CM_IRQ_CMF_HDR], 3, "the memory descriptor moves up");
    parts[0] = 1;
    rsize = cmList(raw, 1, parts, types, levels);
    levels[0] = 11;
    rsize = cmList(raw, 1, parts, types, levels);
    levels[0] = 0x3B;
    cmList(tr, 1, parts, types, levels);
    CHECK_EQ(CmIrqStartScan(raw, tr, 11, &s), CM_IRQ_START_STRIP, "the interrupt alone");
    got = CmIrqStartStrip(raw, out, rsize - CM_IRQ_CMD_SIZE, &s);
    CHECK_EQ(got, CM_IRQ_CML_HDR + CM_IRQ_CMF_HDR, "a full descriptor with none left");
    CHECK_EQ(get32(out + CM_IRQ_CML_HDR + CM_IRQ_CMF_COUNT_AT), 0, "its Count 0");

    /* Two full descriptors, the interrupt in the second. */
    parts[0] = 2; parts[1] = 2;
    types[0] = 3; types[1] = 3; types[2] = 0x81; types[3] = (UCHAR)CM_IRQ_TYPE_INTERRUPT;
    levels[0] = 0; levels[1] = 0; levels[2] = 0; levels[3] = 11;
    rsize = cmList(raw, 2, parts, types, levels);
    cmList(tr, 2, parts, types, levels);
    CHECK_EQ(CmIrqStartScan(raw, tr, 11, &s), CM_IRQ_START_STRIP, "in the second full descriptor");
    CHECK_EQ(s.CountAt, CM_IRQ_CML_HDR + CM_IRQ_CMF_HDR + 2 * CM_IRQ_CMD_SIZE + CM_IRQ_CMF_COUNT_AT,
             "its own Count");
    got = CmIrqStartStrip(raw, out, rsize - CM_IRQ_CMD_SIZE, &s);
    CHECK_EQ(got, rsize - CM_IRQ_CMD_SIZE, "stripped");
    CHECK_EQ(get32(out + CM_IRQ_CML_HDR + CM_IRQ_CMF_COUNT_AT), 2, "the first full descriptor untouched");
    CHECK_EQ(get32(out + s.CountAt), 1, "the second one less");

    /* No interrupt: unchanged; NULL: unchanged. */
    parts[0] = 2;
    types[0] = 3; types[1] = 0x81;
    levels[0] = 0; levels[1] = 0;
    cmList(raw, 1, parts, types, levels);
    CHECK_EQ(CmIrqStartScan(raw, NULL, 11, &s), CM_IRQ_START_NONE, "no interrupt: unchanged");
    CHECK_EQ(CmIrqStartScan(NULL, NULL, 11, &s), CM_IRQ_START_NONE, "no resources: unchanged");

    /* Refusals. */
    parts[0] = 3;
    types[0] = 3; types[1] = (UCHAR)CM_IRQ_TYPE_INTERRUPT; types[2] = (UCHAR)CM_IRQ_TYPE_INTERRUPT;
    levels[0] = 0; levels[1] = 11; levels[2] = 10;
    cmList(raw, 1, parts, types, levels);
    cmList(tr, 1, parts, types, levels);
    CHECK_EQ(CmIrqStartScan(raw, tr, 11, &s), CM_IRQ_START_REFUSE, "two interrupts");
    parts[0] = 2;
    types[1] = (UCHAR)CM_IRQ_TYPE_INTERRUPT;
    levels[1] = 10;
    cmList(raw, 1, parts, types, levels);
    cmList(tr, 1, parts, types, levels);
    CHECK_EQ(CmIrqStartScan(raw, tr, 11, &s), CM_IRQ_START_REFUSE,
             "one at another level (a held configuration's)");
    levels[1] = 11;
    cmList(raw, 1, parts, types, levels);
    CHECK_EQ(CmIrqStartScan(raw, tr, 0, &s), CM_IRQ_START_REFUSE, "a level of 0 is never matched");
    CHECK_EQ(CmIrqStartScan(raw, NULL, 11, &s), CM_IRQ_START_REFUSE, "no translated list");
    types[1] = 3;
    cmList(tr, 1, parts, types, levels);
    CHECK_EQ(CmIrqStartScan(raw, tr, 11, &s), CM_IRQ_START_REFUSE,
             "no interrupt at its position in the translated list");
    types[1] = (UCHAR)CM_IRQ_TYPE_INTERRUPT;
    parts[0] = 3;
    types[2] = 3;
    cmList(tr, 1, parts, types, levels);
    CHECK_EQ(CmIrqStartScan(raw, tr, 11, &s), CM_IRQ_START_REFUSE, "a translated list of another shape");
    parts[0] = 1; parts[1] = 1;
    cmList(tr, 2, parts, types, levels);
    CHECK_EQ(CmIrqStartScan(raw, tr, 11, &s), CM_IRQ_START_REFUSE, "another count of full descriptors");
    parts[0] = 2;
    types[0] = (UCHAR)CM_IRQ_TYPE_DEVSPECIFIC;
    cmList(raw, 1, parts, types, levels);
    cmList(tr, 1, parts, types, levels);
    CHECK_EQ(CmIrqStartScan(raw, tr, 11, &s), CM_IRQ_START_REFUSE, "a device-specific descriptor");
    types[0] = 3;
    cmList(raw, 1, parts, types, levels);
    put32(raw, 9);
    CHECK_EQ(CmIrqStartScan(raw, tr, 11, &s), CM_IRQ_START_REFUSE, "too many full descriptors");
    put32(raw, 1);
    put32(raw + CM_IRQ_CML_HDR + CM_IRQ_CMF_COUNT_AT, 65);
    CHECK_EQ(CmIrqStartScan(raw, tr, 11, &s), CM_IRQ_START_REFUSE, "too many partial descriptors");

    /* The strip's own checks. */
    parts[0] = 2;
    types[0] = 3; types[1] = (UCHAR)CM_IRQ_TYPE_INTERRUPT;
    levels[0] = 0; levels[1] = 11;
    rsize = cmList(raw, 1, parts, types, levels);
    cmList(tr, 1, parts, types, levels);
    CHECK_EQ(CmIrqStartScan(raw, tr, 11, &s), CM_IRQ_START_STRIP, "a plain one");
    CHECK_EQ(CmIrqStartStrip(raw, out, rsize, &s), 0, "an output of the old size is refused");
    CHECK_EQ(CmIrqStartStrip(raw, NULL, rsize - CM_IRQ_CMD_SIZE, &s), 0, "no output");
    s.At = rsize;
    CHECK_EQ(CmIrqStartStrip(raw, out, rsize - CM_IRQ_CMD_SIZE, &s), 0, "a position past the list");
}

/* ---- the records' sequences ------------------------------------------- */

static void test_sequences(void)
{
    CM_IRQ_STATE st;
    ULONG line;

    /* Query adds, the filter request carries it, the start is stripped. */
    st.PciNoIrq = 0;
    st.Added = 0;
    CHECK_EQ(CmIrqQueryStep(&st, 1, CM_IRQ_REQ_NONE, 11, &line), CM_IRQ_ACT_ADD, "the query adds");
    CHECK_EQ(line, 11, "IRQ 11");
    CmIrqQueryAdded(&st, line);
    CHECK_EQ(CmIrqFilterStep(&st, 1, CM_IRQ_REQ_HAS, &line), CM_IRQ_ACT_LEAVE,
             "the filter request carries the added interrupt: left");
    CHECK(st.PciNoIrq && st.Added == 11, "and the records stay");
    CHECK_EQ(CmIrqStartStep(&st, CM_IRQ_START_STRIP), CM_IRQ_ACT_STRIP, "the start is stripped");

    /* A stop, then a start without another query: the same. */
    CHECK_EQ(CmIrqStartStep(&st, CM_IRQ_START_STRIP), CM_IRQ_ACT_STRIP, "a start after a stop too");

    /* A later query fails: the records stay, the next start is stripped. */
    CHECK_EQ(CmIrqQueryStep(&st, 0, CM_IRQ_REQ_BAD, 0, &line), CM_IRQ_ACT_LEAVE, "a failed query");
    CHECK(st.PciNoIrq && st.Added == 11, "keeps both records");
    CHECK_EQ(CmIrqStartStep(&st, CM_IRQ_START_STRIP), CM_IRQ_ACT_STRIP, "the start after it stripped");

    /* A malformed answer changes nothing either. */
    CHECK_EQ(CmIrqQueryStep(&st, 1, CM_IRQ_REQ_BAD, 11, &line), CM_IRQ_ACT_LEAVE, "a malformed answer");
    CHECK(st.PciNoIrq && st.Added == 11, "keeps both records");

    /* A filter request with a held configuration lacking it: added. */
    CHECK_EQ(CmIrqFilterStep(&st, 1, CM_IRQ_REQ_NONE, &line), CM_IRQ_ACT_ADD,
             "a held configuration without it gets it");
    CHECK_EQ(line, 11, "the recorded Line");
    CHECK_EQ(CmIrqFilterStep(&st, 0, CM_IRQ_REQ_NONE, &line), CM_IRQ_ACT_LEAVE,
             "a filter request that failed below: left");

    /* A start holding another level or two interrupts: failed; none: passed. */
    CHECK_EQ(CmIrqStartStep(&st, CM_IRQ_START_REFUSE), CM_IRQ_ACT_FAIL, "another level or two: failed");
    CHECK_EQ(CmIrqStartStep(&st, CM_IRQ_START_NONE), CM_IRQ_ACT_PASS, "a start without it: unchanged");

    /* A query with no wire now keeps the old Line for a held configuration. */
    CHECK_EQ(CmIrqQueryStep(&st, 1, CM_IRQ_REQ_NONE, 0, &line), CM_IRQ_ACT_LEAVE, "no wire now");
    CHECK(st.PciNoIrq && st.Added == 11, "the earlier Line stays");

    /* The query finds pci.sys's own interrupt: both cleared, starts pass. */
    CHECK_EQ(CmIrqQueryStep(&st, 1, CM_IRQ_REQ_HAS, 11, &line), CM_IRQ_ACT_LEAVE, "pci.sys's own interrupt");
    CHECK(!st.PciNoIrq && st.Added == 0, "both records cleared");
    CHECK(!CmIrqStartNeedsScan(&st), "the start is not read");
    CHECK_EQ(CmIrqStartStep(&st, CM_IRQ_START_REFUSE), CM_IRQ_ACT_PASS, "its interrupts are pci.sys's");
    CHECK_EQ(CmIrqFilterStep(&st, 1, CM_IRQ_REQ_NONE, &line), CM_IRQ_ACT_LEAVE, "no filter addition then");

    /* An eligible system that added nothing: a no-interrupt query, then
     * an interrupt-bearing start: unchanged. */
    st.PciNoIrq = 0;
    st.Added = 0;
    CHECK_EQ(CmIrqQueryStep(&st, 1, CM_IRQ_REQ_NONE, 0, &line), CM_IRQ_ACT_LEAVE, "no wire: nothing added");
    CHECK(st.PciNoIrq && st.Added == 0, "PciNoIrq set, nothing added");
    CHECK_EQ(CmIrqStartStep(&st, CM_IRQ_START_REFUSE), CM_IRQ_ACT_PASS,
             "an interrupt-bearing start passes: not this filter's");

    /* An excluded system keeps no record (the driver never calls these):
     * a fresh record passes every start. */
    st.PciNoIrq = 0;
    st.Added = 0;
    CHECK_EQ(CmIrqStartStep(&st, CM_IRQ_START_REFUSE), CM_IRQ_ACT_PASS, "no record: every start passes");
}

/* ---- the layouts ------------------------------------------------------ */

static void test_layout(void)
{
#ifdef _WIN64
    CHECK_EQ(CM_IRQ_CMD_SIZE, 20, "a partial descriptor is 20 bytes on amd64");
#else
    CHECK_EQ(CM_IRQ_CMD_SIZE, 16, "a partial descriptor is 16 bytes on x86");
#endif
    CHECK_EQ(CM_IRQ_IOD_SIZE, 32, "an IO_RESOURCE_DESCRIPTOR is 32 bytes");
    CHECK_EQ(CM_IRQ_REQ_HDR, 32, "the requirements header is 32 bytes");
}

int main(void)
{
    test_hal();
    test_wire();
    test_req();
    test_start();
    test_sequences();
    test_layout();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
