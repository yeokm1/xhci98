/*
 * test_msi.c - host vectors for message-signalled interrupts' pure half
 * (src\xhci_msi.c, src\xhci_hlpr.c; design record 15 section 4.3 and
 * design record 18, section 15 for the helper's revision 13): the
 * capability read, the requirements list's checked walk, its
 * classification, the strip and the qemu no-line rewrite, the filter
 * record, the helper binding's state machine with the timer's event, the
 * channel V2's two halves with each service's verdicts, its minor 1 (the
 * routine service, record 32 section 12.8.15.15), and the trace record.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/xhci_msi.h"
#include "../src/xhci_hlpr.h"
#include "../src/xhci_poll.h"
#include "test_harness.h"

/* ---- a requirements list builder ---------------------------------------- */

#define BUF 2048

/* Descriptor shorthands: B a memory BAR, L a line interrupt, M a message
 * interrupt, P a device-private; the A forms are alternatives. */
#define D_END   0
#define D_B     1
#define D_L     2
#define D_M     3
#define D_P     4
#define D_LA    5
#define D_MA    6
#define D_PA    7
#define D_CFG   8   /* starts the next configuration */
#define D_BA    9   /* a BAR's alternative */
#define D_C     10  /* a configuration's priority (ConfigData) */

static UCHAR in[BUF];
static UCHAR out[BUF];

static void put32(UCHAR *p, ULONG v)
{
    p[0] = (UCHAR)v;
    p[1] = (UCHAR)(v >> 8);
    p[2] = (UCHAR)(v >> 16);
    p[3] = (UCHAR)(v >> 24);
}

static ULONG get32(const UCHAR *p)
{
    return (ULONG)p[0] | ((ULONG)p[1] << 8) | ((ULONG)p[2] << 16) |
           ((ULONG)p[3] << 24);
}

static void put_desc(UCHAR *d, int code, ULONG tag)
{
    int i;

    for (i = 0; i < (int)XHCI_MSI_RD_SIZE; i++) {
        d[i] = 0;
    }
    switch (code) {
    case D_B:
    case D_BA:
        d[XHCI_MSI_RD_TYPE] = 3;
        break;
    case D_L:
    case D_LA:
        d[XHCI_MSI_RD_TYPE] = (UCHAR)XHCI_MSI_TYPE_INTERRUPT;
        break;
    case D_M:
    case D_MA:
        d[XHCI_MSI_RD_TYPE] = (UCHAR)XHCI_MSI_TYPE_INTERRUPT;
        d[XHCI_MSI_RD_FLAGS] = (UCHAR)XHCI_MSI_FLAG_MESSAGE;
        break;
    case D_P:
    case D_PA:
        d[XHCI_MSI_RD_TYPE] = (UCHAR)XHCI_MSI_TYPE_DEVPRIVATE;
        break;
    case D_C:
        d[XHCI_MSI_RD_TYPE] = (UCHAR)XHCI_MSI_TYPE_CONFIGDATA;
        break;
    }
    if (code == D_LA || code == D_MA || code == D_PA || code == D_BA) {
        d[XHCI_MSI_RD_OPTION] = (UCHAR)XHCI_MSI_OPTION_ALTERNATIVE;
    }
    put32(d + XHCI_MSI_RD_U, tag);
}

/* Builds a list from codes; returns its size. Each descriptor's u word 0
 * is its position in the codes, so a test can see which survived. */
static ULONG build(const int *codes)
{
    ULONG size;
    ULONG lists;
    ULONG cfg;
    ULONG count;
    int i;

    for (i = 0; i < BUF; i++) {
        in[i] = 0xCC;
    }
    size = XHCI_MSI_RRL_HEADER;
    for (i = 0; i < (int)XHCI_MSI_RRL_HEADER; i++) {
        in[i] = 0;
    }
    lists = 1;
    cfg = size;
    put32(in + cfg, 0x00010001UL);
    count = 0;
    size += XHCI_MSI_RL_HEADER;
    for (i = 0; codes[i] != D_END; i++) {
        if (codes[i] == D_CFG) {
            put32(in + cfg + XHCI_MSI_RL_COUNT, count);
            cfg = size;
            put32(in + cfg, 0x00010001UL);
            count = 0;
            size += XHCI_MSI_RL_HEADER;
            lists++;
            continue;
        }
        put_desc(in + size, codes[i], (ULONG)i);
        size += XHCI_MSI_RD_SIZE;
        count++;
    }
    put32(in + cfg + XHCI_MSI_RL_COUNT, count);
    put32(in, size);
    put32(in + XHCI_MSI_RRL_ALTERNATIVES, lists);
    return size;
}

/* The descriptor at configuration `c`, index `n` of a list. */
static const UCHAR *desc_at(const UCHAR *list, ULONG c, ULONG n)
{
    const UCHAR *cfg;
    ULONG i;

    cfg = list + XHCI_MSI_RRL_HEADER;
    for (i = 0; i < c; i++) {
        cfg += XHCI_MSI_RL_HEADER +
               get32(cfg + XHCI_MSI_RL_COUNT) * XHCI_MSI_RD_SIZE;
    }
    return cfg + XHCI_MSI_RL_HEADER + n * XHCI_MSI_RD_SIZE;
}

static ULONG count_at(const UCHAR *list, ULONG c)
{
    return get32(desc_at(list, c, 0) - XHCI_MSI_RL_HEADER + XHCI_MSI_RL_COUNT);
}

static ULONG tag_at(const UCHAR *list, ULONG c, ULONG n)
{
    return get32(desc_at(list, c, n) + XHCI_MSI_RD_U);
}

static ULONG opt_at(const UCHAR *list, ULONG c, ULONG n)
{
    return (ULONG)desc_at(list, c, n)[XHCI_MSI_RD_OPTION];
}

/* Walks, classifies and strips `codes`; returns the class. */
static ULONG run(const int *codes, XHCI_MSI_CLASSIFY *c, ULONG *outSize)
{
    XHCI_MSI_WALK w;
    ULONG need;

    (void)build(codes);
    XhciMsiWalk(in, &w);
    XhciMsiClassify(in, &w, c);
    *outSize = 0;
    need = XhciMsiStrip(in, &w, c, NULL, 0);
    if (need != 0) {
        *outSize = XhciMsiStrip(in, &w, c, out, BUF);
        CHECK_EQ(*outSize, need, "the writing pass is the sizing pass's size");
        CHECK_EQ(get32(out), need, "ListSize is what was written");
    }
    return c->Class;
}

/* ---- the capability ----------------------------------------------------- */

static void test_cap(void)
{
    UCHAR cfg[256];
    int i;

    for (i = 0; i < 256; i++) {
        cfg[i] = 0;
    }
    CHECK_EQ(XhciMsiCapRead(cfg), XHCI_POLL_DELIVERY_MESSAGE,
             "no capability list: not read");
    cfg[6] = 0x10;
    cfg[0x34] = 0x50;
    cfg[0x50] = 0x05;
    cfg[0x51] = 0x70;
    cfg[0x70] = 0x11;
    cfg[0x71] = 0x00;
    CHECK_EQ(XhciMsiCapRead(cfg), XHCI_POLL_DELIVERY_MESSAGE,
             "both present, neither enabled");
    cfg[0x52] = 0x01;
    CHECK_EQ(XhciMsiCapRead(cfg), XHCI_POLL_DELIVERY_MSI, "MSI enabled");
    cfg[0x73] = 0x80;
    CHECK_EQ(XhciMsiCapRead(cfg), XHCI_POLL_DELIVERY_MSIX,
             "MSI-X enabled wins");
    cfg[0x52] = 0x00;
    CHECK_EQ(XhciMsiCapRead(cfg), XHCI_POLL_DELIVERY_MSIX, "MSI-X alone");
    /* A loop is bounded; a pointer out of range stops it, and a pointer's
     * reserved low bits are masked. */
    cfg[0x71] = 0x50;
    cfg[0x73] = 0x00;
    cfg[0x52] = 0x01;
    CHECK_EQ(XhciMsiCapRead(cfg), XHCI_POLL_DELIVERY_MSI,
             "a cycle ends at the bound");
    cfg[0x34] = 0x3C;
    CHECK_EQ(XhciMsiCapRead(cfg), XHCI_POLL_DELIVERY_MESSAGE,
             "a pointer below 40h is not followed");
    cfg[0x34] = 0x51;
    CHECK_EQ(XhciMsiCapRead(cfg), XHCI_POLL_DELIVERY_MSI,
             "a pointer's reserved low bits are masked");
    cfg[0x34] = 0x50;
    cfg[0x51] = 0x73;
    cfg[0x52] = 0x00;
    cfg[0x71] = 0x00;
    cfg[0x73] = 0x80;
    CHECK_EQ(XhciMsiCapRead(cfg), XHCI_POLL_DELIVERY_MSIX,
             "a Next pointer's reserved low bits are masked");
}

/* ---- the walk ----------------------------------------------------------- */

static void test_walk(void)
{
    static const int one[] = { D_B, D_M, D_LA, D_END };
    XHCI_MSI_WALK w;
    XHCI_MSI_CLASSIFY c;
    ULONG size;

    size = build(one);
    XhciMsiWalk(in, &w);
    CHECK_EQ(w.Verdict, XHCI_MSI_WALK_OK, "a good list walks");
    CHECK_EQ(w.Lists, 1, "one configuration");
    CHECK_EQ(w.Descriptors, 3, "three descriptors");
    CHECK_EQ(w.Used, size, "all bytes used");

    put32(in, XHCI_MSI_RRL_HEADER - 1);
    XhciMsiWalk(in, &w);
    CHECK_EQ(w.Verdict, XHCI_MSI_WALK_SHORT_HEADER, "ListSize below header");
    XhciMsiClassify(in, &w, &c);
    CHECK_EQ(c.Class, XHCI_MSI_CLASS_INCONSISTENT,
             "a refused walk classifies as inconsistent");

    size = build(one);
    put32(in, XHCI_MSI_RRL_HEADER + 4);
    XhciMsiWalk(in, &w);
    CHECK_EQ(w.Verdict, XHCI_MSI_WALK_ALTERNATIVES,
             "a configuration header cut short is refused before it is read");

    size = build(one);
    put32(in + XHCI_MSI_RRL_ALTERNATIVES, 0x40000000UL);
    XhciMsiWalk(in, &w);
    CHECK_EQ(w.Verdict, XHCI_MSI_WALK_ALTERNATIVES,
             "AlternativeLists larger than the bytes");

    size = build(one);
    put32(in + XHCI_MSI_RRL_HEADER + XHCI_MSI_RL_COUNT, 4);
    XhciMsiWalk(in, &w);
    CHECK_EQ(w.Verdict, XHCI_MSI_WALK_COUNT, "a Count past the bytes");

    size = build(one);
    put32(in + XHCI_MSI_RRL_HEADER + XHCI_MSI_RL_COUNT, 0x08000001UL);
    XhciMsiWalk(in, &w);
    CHECK_EQ(w.Verdict, XHCI_MSI_WALK_COUNT,
             "a Count whose product with 32 overflows");

    size = build(one);
    put32(in + XHCI_MSI_RRL_ALTERNATIVES, 2);
    XhciMsiWalk(in, &w);
    CHECK_EQ(w.Verdict, XHCI_MSI_WALK_LIST_HEADER,
             "a second configuration with no bytes for its header");
    CHECK_EQ(w.FailList, 1, "and where it stopped");

    size = build(one);
    put32(in, size + 24);
    XhciMsiWalk(in, &w);
    CHECK_EQ(w.Verdict, XHCI_MSI_WALK_OK, "trailing bytes are accepted");
    CHECK_EQ(w.Used, size, "and not counted as used");
}

/* ---- the classification and the strip ----------------------------------- */

static void test_strip(void)
{
    static const int msgThenLine[] = { D_B, D_M, D_LA, D_END };
    static const int lineThenMsg[] = { D_B, D_L, D_MA, D_END };
    static const int twoCfg[] = { D_B, D_M, D_CFG, D_B, D_L, D_END };
    static const int twoCfgRev[] = { D_B, D_L, D_CFG, D_B, D_M, D_END };
    static const int msix[] = { D_B, D_M, D_MA, D_LA, D_END };
    static const int twoReq[] = { D_B, D_L, D_M, D_END };
    static const int twoReqBeside[] = { D_B, D_L, D_M, D_CFG, D_B, D_L,
                                        D_END };
    static const int msgOnly[] = { D_B, D_M, D_END };
    static const int noneBeside[] = { D_B, D_CFG, D_B, D_M, D_LA, D_END };
    static const int noneLine[] = { D_B, D_CFG, D_B, D_L, D_END };
    static const int noInt[] = { D_B, D_END };
    static const int lineOnly[] = { D_B, D_L, D_END };
    static const int priv[] = { D_B, D_M, D_LA, D_P, D_END };
    static const int openAlt[] = { D_MA, D_L, D_END };
    static const int mixedType[] = { D_B, D_M, D_PA, D_END };
    XHCI_MSI_CLASSIFY c;
    ULONG size;

    CHECK_EQ(run(msgThenLine, &c, &size), XHCI_MSI_CLASS_REPLACEABLE,
             "message then line: replaceable");
    CHECK_EQ(get32(out + XHCI_MSI_RRL_ALTERNATIVES), 1, "one configuration");
    CHECK_EQ(count_at(out, 0), 2, "BAR and line");
    CHECK_EQ(tag_at(out, 0, 1), 2, "the line kept");
    CHECK_EQ(opt_at(out, 0, 1) & XHCI_MSI_OPTION_ALTERNATIVE, 0,
             "and heads its run");

    CHECK_EQ(run(lineThenMsg, &c, &size), XHCI_MSI_CLASS_REPLACEABLE,
             "line then message: replaceable");
    CHECK_EQ(count_at(out, 0), 2, "BAR and line");
    CHECK_EQ(tag_at(out, 0, 1), 1, "the line, its head");
    CHECK_EQ(opt_at(out, 0, 1), 0, "its option unchanged");

    CHECK_EQ(run(twoCfg, &c, &size), XHCI_MSI_CLASS_REPLACEABLE,
             "[BAR,MSI] and [BAR,INTx]");
    CHECK_EQ(c.Strippable, 1, "one strippable configuration");
    CHECK_EQ(c.MessageBound, 1, "one message-bound");
    CHECK_EQ(get32(out + XHCI_MSI_RRL_ALTERNATIVES), 1,
             "the message configuration dropped");
    CHECK_EQ(tag_at(out, 0, 1), 4, "the INTx configuration kept");

    CHECK_EQ(run(twoCfgRev, &c, &size), XHCI_MSI_CLASS_REPLACEABLE,
             "[BAR,INTx] and [BAR,MSI]");
    CHECK_EQ(get32(out + XHCI_MSI_RRL_ALTERNATIVES), 1,
             "the message configuration dropped, second as first");
    CHECK_EQ(tag_at(out, 0, 1), 1, "the INTx configuration kept");

    CHECK_EQ(run(msix, &c, &size), XHCI_MSI_CLASS_REPLACEABLE,
             "two message members and a line");
    CHECK_EQ(count_at(out, 0), 2, "both messages gone");
    CHECK_EQ(tag_at(out, 0, 1), 3, "the line");
    CHECK_EQ(opt_at(out, 0, 1) & XHCI_MSI_OPTION_ALTERNATIVE, 0,
             "heading the run");

    CHECK_EQ(run(twoReq, &c, &size), XHCI_MSI_CLASS_NO_LINE,
             "[BAR, INTx, MSI] as two requirements: no line offered");
    CHECK_EQ(size, 0, "and not stripped");
    CHECK_EQ(run(twoReqBeside, &c, &size), XHCI_MSI_CLASS_REPLACEABLE,
             "two requirements beside [BAR,INTx]");
    CHECK_EQ(get32(out + XHCI_MSI_RRL_ALTERNATIVES), 1,
             "the two-requirement configuration dropped");
    CHECK_EQ(tag_at(out, 0, 1), 5, "the INTx configuration kept");

    CHECK_EQ(run(msgOnly, &c, &size), XHCI_MSI_CLASS_NO_LINE,
             "message only: no line offered");
    CHECK_EQ(size, 0, "kept as is");

    CHECK_EQ(run(noneBeside, &c, &size), XHCI_MSI_CLASS_REPLACEABLE,
             "[BAR] beside [BAR, MSI|INTx]");
    CHECK_EQ(get32(out + XHCI_MSI_RRL_ALTERNATIVES), 1,
             "the NONE configuration dropped");
    CHECK_EQ(c.NoInterrupt, 1, "counted as NONE");

    CHECK_EQ(run(noneLine, &c, &size), XHCI_MSI_CLASS_NO_MESSAGE,
             "[BAR] beside [BAR, INTx], no message: kept");
    CHECK_EQ(size, 0, "not rewritten");
    CHECK_EQ(run(noInt, &c, &size), XHCI_MSI_CLASS_NO_INTERRUPT,
             "no interrupt at all");
    CHECK_EQ(run(lineOnly, &c, &size), XHCI_MSI_CLASS_NO_MESSAGE,
             "a line-only list");
    CHECK_EQ(XhciMsiOutcome(XHCI_MSI_CLASS_NO_MESSAGE),
             XHCI_MSI_OUT_NO_MESSAGE, "its outcome");

    CHECK_EQ(run(priv, &c, &size), XHCI_MSI_CLASS_REPLACEABLE,
             "a device-private after the run");
    CHECK_EQ(c.DevicePrivate, 1, "counted");
    CHECK_EQ(count_at(out, 0), 3, "copied unchanged");

    CHECK_EQ(run(openAlt, &c, &size), XHCI_MSI_CLASS_UNSUPPORTED,
             "a list opening on an alternative");
    CHECK_EQ(size, 0, "kept");
    CHECK_EQ(run(mixedType, &c, &size), XHCI_MSI_CLASS_UNSUPPORTED,
             "a run mixing types");
    CHECK_EQ(XhciMsiOutcome(XHCI_MSI_CLASS_UNSUPPORTED),
             XHCI_MSI_OUT_UNSUPPORTED, "its outcome");
    CHECK_EQ(XhciMsiOutcome(XHCI_MSI_CLASS_NO_LINE), XHCI_MSI_OUT_NO_LINE,
             "no line's outcome");
    CHECK_EQ(XhciMsiOutcome(XHCI_MSI_CLASS_REPLACEABLE),
             XHCI_MSI_OUT_REPLACED, "replaceable's outcome");

    /* A capacity too small writes nothing usable. */
    {
        XHCI_MSI_WALK w;

        (void)build(msgThenLine);
        XhciMsiWalk(in, &w);
        XhciMsiClassify(in, &w, &c);
        CHECK_EQ(XhciMsiStrip(in, &w, &c, out, 0x30), 0,
                 "a short capacity is refused");
    }
}

/*
 * R1 and R2 (record 18 section 11), as read on Vista SP2 and 7 SP1, x86 and
 * x64, qemu-xhci with msi=on,msix=off, msi=off,msix=on and the default: one
 * configuration of five descriptors - the BAR (preferred) and its
 * alternative, a device-private, then the interrupt run: the message
 * (preferred) heading it and the line its alternative. With MSISupported 0
 * Windows builds the same list without the message, the line heading its
 * run with option 0. The strip of the first must be the second as the
 * trace shows it (each descriptor's Option, Type, ShareDisposition, Flags
 * and u words; the list header and the descriptors' spare bytes are not in
 * the trace and are reconstructed alike): the BAR pair and the
 * device-private kept, the line kept as head.
 */
typedef struct _R1_DESC {
    UCHAR Option;
    UCHAR Type;
    UCHAR Share;
    USHORT Flags;
    ULONG U[6];
} R1_DESC;

static const R1_DESC r1Mixed[] = {
    { 0x01, 0x03, 1, 0x0080,
      { 0x00004000, 0x00000001, 0xFEBF0000, 0, 0xFEBF3FFF, 0 } },
    { 0x08, 0x03, 1, 0x0080,
      { 0x00004000, 0x00004000, 0, 0, 0xFFFFFFFF, 0xFFFFFFFF } },
    { 0x00, 0x81, 1, 0x0000, { 0x00000001, 0, 0, 0, 0, 0 } },
    { 0x01, 0x02, 1, 0x0003, { 0xFFFFFFFE, 0xFFFFFFFE, 0, 0, 0, 0 } },
    { 0x08, 0x02, 3, 0x0000, { 0x00000000, 0xFFFFFFFF, 0, 0, 0, 0 } },
};

static const R1_DESC r1Line[] = {
    { 0x01, 0x03, 1, 0x0080,
      { 0x00004000, 0x00000001, 0xFEBF0000, 0, 0xFEBF3FFF, 0 } },
    { 0x08, 0x03, 1, 0x0080,
      { 0x00004000, 0x00004000, 0, 0, 0xFFFFFFFF, 0xFFFFFFFF } },
    { 0x00, 0x81, 1, 0x0000, { 0x00000001, 0, 0, 0, 0, 0 } },
    { 0x00, 0x02, 3, 0x0000, { 0x00000000, 0xFFFFFFFF, 0, 0, 0, 0 } },
};

static ULONG r1_build(UCHAR *list, const R1_DESC *d, ULONG n)
{
    ULONG size;
    ULONG i;
    ULONG k;
    UCHAR *p;

    for (i = 0; i < BUF; i++) {
        list[i] = 0;
    }
    put32(list + 4, 5);         /* InterfaceType PCIBus */
    put32(list + 12, 0x20);     /* SlotNumber           */
    put32(list + XHCI_MSI_RRL_ALTERNATIVES, 1);
    put32(list + XHCI_MSI_RRL_HEADER, 0x00010001UL);
    put32(list + XHCI_MSI_RRL_HEADER + XHCI_MSI_RL_COUNT, n);
    size = XHCI_MSI_RRL_HEADER + XHCI_MSI_RL_HEADER;
    for (i = 0; i < n; i++) {
        p = list + size;
        p[XHCI_MSI_RD_OPTION] = d[i].Option;
        p[XHCI_MSI_RD_TYPE] = d[i].Type;
        p[XHCI_MSI_RD_SHARE] = d[i].Share;
        p[XHCI_MSI_RD_FLAGS] = (UCHAR)d[i].Flags;
        p[XHCI_MSI_RD_FLAGS + 1] = (UCHAR)(d[i].Flags >> 8);
        for (k = 0; k < 6; k++) {
            put32(p + XHCI_MSI_RD_U + k * 4, d[i].U[k]);
        }
        size += XHCI_MSI_RD_SIZE;
    }
    put32(list, size);
    return size;
}

static void test_r1(void)
{
    static UCHAR expect[BUF];
    XHCI_MSI_WALK w;
    XHCI_MSI_CLASSIFY c;
    ULONG size;
    ULONG want;
    ULONG i;
    ULONG same;

    CHECK_EQ(r1_build(in, r1Mixed, 5), 0xC8, "R1's list is 0xC8 bytes");
    want = r1_build(expect, r1Line, 4);
    CHECK_EQ(want, 0xA8, "MSISupported 0's list is 0xA8 bytes");
    XhciMsiWalk(in, &w);
    CHECK_EQ(w.Verdict, XHCI_MSI_WALK_OK, "R1's list walks");
    XhciMsiClassify(in, &w, &c);
    CHECK_EQ(c.Class, XHCI_MSI_CLASS_REPLACEABLE, "R1's list is replaceable");
    CHECK_EQ(c.MessageMembers, 1, "one message member");
    CHECK_EQ(c.LineMembers, 1, "one line member");
    CHECK_EQ(c.DevicePrivate, 1, "one device-private (R2)");
    CHECK_EQ(c.Unsupported, 0, "an admitted shape");
    CHECK_EQ(XhciMsiOutcome(c.Class), XHCI_MSI_OUT_REPLACED,
             "outcome replaced");
    size = XhciMsiStrip(in, &w, &c, NULL, 0);
    CHECK_EQ(size, want, "sized as Windows' own line-only list");
    for (i = 0; i < BUF; i++) {
        out[i] = 0xCC;
    }
    CHECK_EQ(XhciMsiStrip(in, &w, &c, out, BUF), want, "written");
    same = 1;
    for (i = 0; i < want; i++) {
        if (out[i] != expect[i]) {
            same = 0;
        }
    }
    CHECK(same, "the strip is Windows' MSISupported 0 list as traced");
    CHECK_EQ(out[want], 0xCC, "nothing written past it");
    XhciMsiWalk(out, &w);
    XhciMsiClassify(out, &w, &c);
    CHECK_EQ(c.Class, XHCI_MSI_CLASS_NO_MESSAGE,
             "the stripped list offers the line alone");
    (void)r1_build(in, r1Mixed, 5);
    XhciMsiWalk(in, &w);
    CHECK_EQ(XhciMsiAdmitted(in, &w), 1, "R1's shape is admitted");
}

/* Admitted is R1's shape alone; any other REPLACEABLE list is kept as an
 * unsupported shape by the filter. */
static ULONG admitted(const int *codes)
{
    XHCI_MSI_WALK w;

    (void)build(codes);
    XhciMsiWalk(in, &w);
    return XhciMsiAdmitted(in, &w);
}

static void test_admit(void)
{
    static const int r1[] = { D_B, D_BA, D_P, D_M, D_LA, D_END };
    static const int intAfterBar[] = { D_B, D_MA, D_LA, D_END };
    static const int r1Plain[] = { D_B, D_M, D_LA, D_END };
    static const int privAfter[] = { D_B, D_M, D_LA, D_P, D_END };
    static const int twoMsg[] = { D_B, D_M, D_MA, D_LA, D_END };
    static const int lineThenMsg[] = { D_B, D_L, D_MA, D_END };
    static const int msgLineMsg[] = { D_B, D_M, D_LA, D_MA, D_END };
    static const int twoCfg[] = { D_B, D_M, D_LA, D_CFG, D_B, D_L, D_END };
    static const int twoRuns[] = { D_B, D_M, D_LA, D_L, D_END };
    static const int privInRun[] = { D_B, D_M, D_PA, D_LA, D_END };
    static const int msgOnly[] = { D_B, D_M, D_END };
    static const int lineOnly[] = { D_B, D_L, D_END };
    XHCI_MSI_WALK w;

    CHECK_EQ(admitted(r1), 1, "BAR pair, device-private, message|line");
    CHECK_EQ(admitted(r1Plain), 1, "the same without the extras");
    CHECK_EQ(admitted(privAfter), 1, "a device-private after the run");
    CHECK_EQ(admitted(twoMsg), 1, "two messages heading the line");
    CHECK_EQ(admitted(lineThenMsg), 0, "a line heading the run: not read");
    CHECK_EQ(admitted(msgLineMsg), 0, "a message after the line: not read");
    CHECK_EQ(admitted(twoCfg), 0, "two configurations: not read");
    CHECK_EQ(admitted(twoRuns), 0, "two interrupt runs: not read");
    CHECK_EQ(admitted(privInRun), 0, "another type inside the run");
    CHECK_EQ(admitted(intAfterBar), 0,
             "interrupt alternatives of a BAR's run");
    CHECK_EQ(admitted(msgOnly), 0, "no line");
    CHECK_EQ(admitted(lineOnly), 0, "no message");
    w.Verdict = XHCI_MSI_WALK_COUNT;
    w.Lists = 1;
    CHECK_EQ(XhciMsiAdmitted(in, &w), 0, "a list the walk refused");
}

static void test_inj(void)
{
    static const int mixed[] = { D_B, D_M, D_LA, D_END };
    static const int twoCfg[] = { D_B, D_M, D_LA, D_CFG, D_B, D_L, D_END };
    XHCI_MSI_WALK w;
    XHCI_MSI_CLASSIFY c;
    ULONG size;

    (void)build(mixed);
    XhciMsiWalk(in, &w);
    size = XhciMsiInjNoLine(in, &w, out, BUF);
    CHECK(size != 0, "[BAR, MSI|INTx] rewritten");
    XhciMsiWalk(out, &w);
    XhciMsiClassify(out, &w, &c);
    CHECK_EQ(c.Class, XHCI_MSI_CLASS_NO_LINE, "to [BAR, MSI]: no line");
    CHECK_EQ(count_at(out, 0), 2, "two descriptors");

    (void)build(twoCfg);
    XhciMsiWalk(in, &w);
    size = XhciMsiInjNoLine(in, &w, out, BUF);
    XhciMsiWalk(out, &w);
    XhciMsiClassify(out, &w, &c);
    CHECK_EQ(w.Lists, 1, "the line-only configuration dropped");
    CHECK_EQ(c.Class, XHCI_MSI_CLASS_NO_LINE, "no line left");

    (void)build(mixed);
    XhciMsiWalk(out, &w);
    (void)build(mixed);
    XhciMsiWalk(in, &w);
    (void)XhciMsiInjNoLine(in, &w, out, BUF);
    XhciMsiWalk(out, &w);
    CHECK_EQ(XhciMsiInjNoLine(out, &w, in, BUF), 0,
             "a no-line list is not rewritten again");
}

/* Task 47.33: Windows 2000 / XP / XP x64 - the PCI driver's one
 * configuration kept, then its copy without the line. */
static ULONG nt5(const int *codes, ULONG *need)
{
    XHCI_MSI_WALK w;
    ULONG size;

    (void)build(codes);
    XhciMsiWalk(in, &w);
    *need = XhciMsiAppendNoIntr(in, &w, NULL, 0);
    size = XhciMsiAppendNoIntr(in, &w, out, BUF);
    if (*need != 0) {
        CHECK_EQ(size, *need, "the writing pass is the sizing pass's size");
        CHECK_EQ(get32(out), size, "ListSize is what was written");
        XhciMsiWalk(out, &w);
        CHECK_EQ(w.Verdict, XHCI_MSI_WALK_OK, "the result walks");
        CHECK_EQ(w.Used, size, "and uses all of itself");
    }
    return size;
}

static void test_nt5(void)
{
    /* The list XP x64's PCI driver gave the 8470p's state (a3): memory,
     * device-private, the line. */
    static const int a3[] = { D_B, D_P, D_L, D_END };
    /* Its control: the boot configuration preferred, an alternative BAR. */
    static const int g0[] = { D_B, D_BA, D_P, D_L, D_END };
    static const int lineRun[] = { D_B, D_L, D_LA, D_LA, D_P, D_END };
    static const int two[] = { D_B, D_L, D_CFG, D_B, D_P, D_END };
    static const int twoLine[] = { D_B, D_L, D_CFG, D_B, D_P, D_L, D_END };
    static const int noLine[] = { D_B, D_P, D_END };
    static const int msg[] = { D_B, D_M, D_LA, D_END };
    static const int mixed[] = { D_B, D_L, D_PA, D_END };
    static const int mixed2[] = { D_P, D_LA, D_END };
    static const int lineOnly[] = { D_L, D_END };
    static const int opensAlt[] = { D_BA, D_L, D_END };
    static const int prio[] = { D_C, D_B, D_L, D_END };
    XHCI_MSI_WALK w;
    XHCI_MSI_CLASSIFY c;
    ULONG need;
    ULONG size;

    size = nt5(a3, &need);
    CHECK(size != 0, "a3: rewritten");
    CHECK_EQ(get32(out + XHCI_MSI_RRL_ALTERNATIVES), 2, "a3: two lists");
    CHECK_EQ(count_at(out, 0), 3, "a3: the first as given");
    CHECK_EQ(tag_at(out, 0, 2), 2, "a3: its line kept");
    CHECK_EQ(count_at(out, 1), 2, "a3: the copy without the line");
    CHECK_EQ(tag_at(out, 1, 0), 0, "a3: the BAR first");
    CHECK_EQ(tag_at(out, 1, 1), 1, "a3: then the device-private");
    CHECK_EQ(size, XHCI_MSI_RRL_HEADER + 2 * XHCI_MSI_RL_HEADER +
                       5 * XHCI_MSI_RD_SIZE, "a3: 136 + 72 bytes");
    CHECK(memcmp(out + 4, in + 4, XHCI_MSI_RRL_ALTERNATIVES - 4) == 0,
          "the header's interface, bus and slot kept");
    CHECK(memcmp(out + XHCI_MSI_RRL_HEADER, in + XHCI_MSI_RRL_HEADER,
                 XHCI_MSI_RL_HEADER + 3 * XHCI_MSI_RD_SIZE) == 0,
          "the lower configuration byte for byte");
    CHECK(memcmp(desc_at(out, 1, 0) - XHCI_MSI_RL_HEADER,
                 in + XHCI_MSI_RRL_HEADER, 4) == 0,
          "the copy's version and revision kept");

    size = nt5(g0, &need);
    CHECK_EQ(get32(out + XHCI_MSI_RRL_ALTERNATIVES), 2, "g0: two lists");
    CHECK_EQ(count_at(out, 1), 3, "g0: BAR, its alternative, private");
    CHECK_EQ(opt_at(out, 1, 1), XHCI_MSI_OPTION_ALTERNATIVE,
             "g0: the BAR's alternative stays an alternative");
    CHECK_EQ(tag_at(out, 1, 2), 2, "g0: the private last");

    size = nt5(lineRun, &need);
    CHECK_EQ(count_at(out, 1), 2, "a line run dropped whole");
    CHECK_EQ(tag_at(out, 1, 1), 4, "the private after it kept");
    CHECK_EQ(opt_at(out, 1, 1), 0, "and still a head");

    CHECK_EQ(nt5(two, &need), 0,
             "a configuration already without a line: nothing added");

    CHECK_EQ(nt5(twoLine, &need), 0,
             "two configurations, both with a line: not the PCI shape");
    CHECK_EQ(nt5(prio, &need), 0, "a configuration priority: refused");

    CHECK_EQ(nt5(noLine, &need), 0, "no line: nothing to add");
    CHECK_EQ(nt5(msg, &need), 0, "a message member: refused");
    CHECK_EQ(nt5(mixed, &need), 0, "a line run with another type: refused");
    CHECK_EQ(nt5(mixed2, &need), 0, "a private run with a line: refused");
    CHECK_EQ(nt5(lineOnly, &need), 0, "a copy would be empty: refused");
    CHECK_EQ(nt5(opensAlt, &need), 0, "a list opening on an alternative");

    (void)build(a3);
    XhciMsiWalk(in, &w);
    need = XhciMsiAppendNoIntr(in, &w, NULL, 0);
    CHECK_EQ(XhciMsiAppendNoIntr(in, &w, out, need - 1), 0,
             "one byte short: nothing written");
    w.Verdict = XHCI_MSI_WALK_COUNT;
    CHECK_EQ(XhciMsiAppendNoIntr(in, &w, out, BUF), 0,
             "a refused walk: nothing");

    /* The result classifies as lines only with one configuration without
     * an interrupt, and a second filter on it (a rebalance's) adds
     * nothing. */
    (void)nt5(a3, &need);
    XhciMsiWalk(out, &w);
    XhciMsiClassify(out, &w, &c);
    CHECK_EQ(c.Class, XHCI_MSI_CLASS_NO_MESSAGE, "still lines only");
    CHECK_EQ(c.NoInterrupt, 1, "one configuration without an interrupt");
    CHECK_EQ(XhciMsiAppendNoIntr(out, &w, in, BUF), 0,
             "a rewritten list is not rewritten again");
}

/* Task 47.33, record 18 section 16.4: Windows 2000 on a listed board, the
 * line removed and the copy the only configuration. */
static ULONG nt5s(const int *codes)
{
    XHCI_MSI_WALK w;
    ULONG need;
    ULONG size;

    (void)build(codes);
    XhciMsiWalk(in, &w);
    need = XhciMsiStripNt5(in, &w, NULL, 0);
    size = XhciMsiStripNt5(in, &w, out, BUF);
    CHECK_EQ(size, need, "strip: writing pass = sizing pass");
    if (size != 0) {
        CHECK_EQ(get32(out), size, "strip: ListSize is what was written");
        XhciMsiWalk(out, &w);
        CHECK_EQ(w.Verdict, XHCI_MSI_WALK_OK, "strip: the result walks");
        CHECK_EQ(w.Used, size, "strip: and uses all of itself");
    }
    return size;
}

static void test_nt5_strip(void)
{
    static const int a3[] = { D_B, D_P, D_L, D_END };
    static const int g0[] = { D_B, D_BA, D_P, D_L, D_END };
    static const int lineRun[] = { D_B, D_L, D_LA, D_LA, D_P, D_END };
    static const int twoLine[] = { D_B, D_L, D_CFG, D_B, D_P, D_L, D_END };
    static const int noLine[] = { D_B, D_P, D_END };
    static const int msg[] = { D_B, D_M, D_LA, D_END };
    static const int mixed[] = { D_B, D_L, D_PA, D_END };
    static const int lineOnly[] = { D_L, D_END };
    XHCI_MSI_WALK w;
    XHCI_MSI_CLASSIFY c;
    ULONG size;

    size = nt5s(a3);
    CHECK_EQ(size, XHCI_MSI_RRL_HEADER + XHCI_MSI_RL_HEADER +
                       2 * XHCI_MSI_RD_SIZE, "a3: 32 + 8 + 64 bytes");
    CHECK_EQ(get32(out + XHCI_MSI_RRL_ALTERNATIVES), 1,
             "a3: one configuration, the only one");
    CHECK_EQ(count_at(out, 0), 2, "a3: BAR and private");
    CHECK_EQ(tag_at(out, 0, 0), 0, "a3: the BAR first");
    CHECK_EQ(tag_at(out, 0, 1), 1, "a3: then the private");
    CHECK(memcmp(out + 4, in + 4, XHCI_MSI_RRL_ALTERNATIVES - 4) == 0,
          "the header's interface, bus and slot kept");
    CHECK(memcmp(out + XHCI_MSI_RRL_HEADER, in + XHCI_MSI_RRL_HEADER, 4) ==
              0, "the configuration's version and revision kept");
    XhciMsiWalk(out, &w);
    XhciMsiClassify(out, &w, &c);
    CHECK_EQ(c.Class, XHCI_MSI_CLASS_NO_INTERRUPT,
             "no interrupt anywhere in the result");
    CHECK_EQ(XhciMsiStripNt5(out, &w, in, BUF), 0,
             "a stripped list (a rebalance's filter) is not stripped "
             "again: it passes as it is");

    size = nt5s(g0);
    CHECK_EQ(count_at(out, 0), 3, "g0: BAR, its alternative, private");
    CHECK_EQ(opt_at(out, 0, 1), XHCI_MSI_OPTION_ALTERNATIVE,
             "g0: the BAR's alternative kept");

    size = nt5s(lineRun);
    CHECK_EQ(count_at(out, 0), 2, "a line run removed whole");
    CHECK_EQ(opt_at(out, 0, 1), 0, "the private after it still a head");

    CHECK_EQ(nt5s(noLine), 0, "no line: nothing to remove");
    CHECK_EQ(nt5s(twoLine), 0, "two configurations: refused");
    CHECK_EQ(nt5s(msg), 0, "a message member: refused");
    CHECK_EQ(nt5s(mixed), 0, "a line run with another type: refused");
    CHECK_EQ(nt5s(lineOnly), 0, "nothing would be left: refused");

    (void)build(a3);
    XhciMsiWalk(in, &w);
    size = XhciMsiStripNt5(in, &w, NULL, 0);
    CHECK_EQ(XhciMsiStripNt5(in, &w, out, size - 1), 0,
             "one byte short: nothing written");
    w.Verdict = XHCI_MSI_WALK_COUNT;
    CHECK_EQ(XhciMsiStripNt5(in, &w, out, BUF), 0,
             "a refused walk: nothing");
}

/* Task 47.33, part 2: the clone with the chipset-routed GSI. */
static ULONG nt5g(const int *codes, ULONG gsi, ULONG memory)
{
    XHCI_MSI_WALK w;
    ULONG need;
    ULONG size;

    (void)build(codes);
    XhciMsiWalk(in, &w);
    need = XhciMsiAppendNt5(in, &w, gsi, memory, NULL, 0);
    size = XhciMsiAppendNt5(in, &w, gsi, memory, out, BUF);
    CHECK_EQ(size, need, "writing pass = sizing pass");
    if (size != 0) {
        CHECK_EQ(get32(out), size, "ListSize is what was written");
        XhciMsiWalk(out, &w);
        CHECK_EQ(w.Verdict, XHCI_MSI_WALK_OK, "the result walks");
        CHECK_EQ(w.Used, size, "and uses all of itself");
    }
    return size;
}

static ULONG gmin(const UCHAR *list, ULONG c, ULONG n)
{
    return get32(desc_at(list, c, n) + XHCI_MSI_RD_U);
}

static ULONG gmax(const UCHAR *list, ULONG c, ULONG n)
{
    return get32(desc_at(list, c, n) + XHCI_MSI_RD_U + 4);
}

static void test_nt5_gsi(void)
{
    static const int a3[] = { D_B, D_P, D_L, D_END };
    static const int g0[] = { D_B, D_BA, D_P, D_L, D_END };
    static const int lineRun[] = { D_B, D_L, D_LA, D_LA, D_P, D_END };
    static const int twoRuns[] = { D_B, D_L, D_P, D_L, D_END };
    static const int noLine[] = { D_B, D_P, D_END };
    XHCI_MSI_WALK w;
    XHCI_MSI_CLASSIFY c;
    UCHAR head[XHCI_MSI_RD_SIZE];
    ULONG size;
    ULONG i;

    /* Mode 0 on the 8470p's list: the PCI driver's configuration, then
     * the GSI 16 clone, then memory alone. */
    size = nt5g(a3, 16, 1);
    CHECK(size != 0, "a3: rewritten");
    CHECK_EQ(get32(out + XHCI_MSI_RRL_ALTERNATIVES), 3, "three lists");
    CHECK(memcmp(out + XHCI_MSI_RRL_HEADER, in + XHCI_MSI_RRL_HEADER,
                 XHCI_MSI_RL_HEADER + 3 * XHCI_MSI_RD_SIZE) == 0,
          "the lower configuration first, byte for byte: a firmware route "
          "that can be granted wins");
    CHECK_EQ(count_at(out, 1), 3, "the clone keeps three descriptors");
    CHECK_EQ(tag_at(out, 1, 0), 0, "the BAR");
    CHECK_EQ(tag_at(out, 1, 1), 1, "the private");
    CHECK_EQ((ULONG)desc_at(out, 1, 2)[XHCI_MSI_RD_TYPE],
             XHCI_MSI_TYPE_INTERRUPT, "then the interrupt");
    CHECK_EQ(gmin(out, 1, 2), 16, "minimum 16");
    CHECK_EQ(gmax(out, 1, 2), 16, "maximum 16");
    for (i = 0; i < XHCI_MSI_RD_U; i++) {
        head[i] = desc_at(in, 0, 2)[i];
    }
    CHECK(memcmp(desc_at(out, 1, 2), head, XHCI_MSI_RD_U) == 0,
          "option, type, share and flags as the PCI driver gave them");
    CHECK_EQ(count_at(out, 2), 2, "the memory-only copy last");
    CHECK_EQ(size, XHCI_MSI_RRL_HEADER + 3 * XHCI_MSI_RL_HEADER +
                       8 * XHCI_MSI_RD_SIZE, "136 + 104 + 72 bytes");
    XhciMsiWalk(out, &w);
    XhciMsiClassify(out, &w, &c);
    CHECK_EQ(c.Class, XHCI_MSI_CLASS_NO_MESSAGE, "lines only");
    CHECK_EQ(XhciMsiAppendNt5(out, &w, 16, 1, in, BUF), 0,
             "a rewritten list is not rewritten again");

    /* Mode 1: the clone alone. */
    size = nt5g(a3, 16, 0);
    CHECK_EQ(get32(out + XHCI_MSI_RRL_ALTERNATIVES), 2, "two lists");
    CHECK_EQ(gmin(out, 1, 2), 16, "the clone's GSI");
    /* Mode 2, or no GSI offered: part 1's copy. */
    size = nt5g(a3, XHCI_MSI_NO_GSI, 1);
    CHECK_EQ(get32(out + XHCI_MSI_RRL_ALTERNATIVES), 2, "two lists");
    CHECK_EQ(count_at(out, 1), 2, "memory only");
    CHECK_EQ(nt5g(a3, XHCI_MSI_NO_GSI, 0), 0, "neither asked: nothing");

    /* The control's list (a boot configuration preferred): the clone keeps
     * the BAR's preference and its alternative. */
    size = nt5g(g0, 21, 1);
    CHECK_EQ(count_at(out, 1), 4, "BAR, alternative, private, GSI");
    CHECK_EQ(opt_at(out, 1, 1), XHCI_MSI_OPTION_ALTERNATIVE,
             "the BAR's alternative kept");
    CHECK_EQ(gmin(out, 1, 3), 21, "GSI 21");

    /* A line run of three: one interrupt in the clone, never more. */
    size = nt5g(lineRun, 16, 1);
    CHECK_EQ(count_at(out, 1), 3, "BAR, one interrupt, private");
    CHECK(memcmp(desc_at(out, 1, 1), desc_at(in, 0, 1), XHCI_MSI_RD_U) == 0,
          "the run's head kept (its u narrowed)");
    CHECK_EQ(opt_at(out, 1, 1), 0, "a head");
    CHECK_EQ(gmin(out, 1, 1), 16, "narrowed");
    CHECK_EQ(tag_at(out, 1, 2), 4, "the private after it");
    CHECK_EQ(count_at(out, 2), 2, "memory copy: BAR and private");

    size = nt5g(twoRuns, 16, 1);
    CHECK_EQ(get32(out + XHCI_MSI_RRL_ALTERNATIVES), 2,
             "two interrupts asked: no clone, the memory copy alone");
    CHECK_EQ(count_at(out, 1), 2, "BAR and private");
    CHECK_EQ(nt5g(twoRuns, 16, 0), 0, "and in mode 1 nothing");
    CHECK_EQ(nt5g(noLine, 16, 1), 0, "no interrupt: nothing to clone");

    /* The clone only for a PCI INTx: shared and level-sensitive. */
    (void)build(a3);
    XhciMsiWalk(in, &w);
    ((UCHAR *)desc_at(in, 0, 2))[XHCI_MSI_RD_SHARE] =
        (UCHAR)XHCI_MSI_SHARE_SHARED;
    CHECK_EQ(XhciMsiNt5LineShape(in, &w), 1, "shared level: the shape");
    ((UCHAR *)desc_at(in, 0, 2))[XHCI_MSI_RD_FLAGS] =
        (UCHAR)XHCI_MSI_FLAG_LATCHED;
    CHECK_EQ(XhciMsiNt5LineShape(in, &w), 0, "edge: not the shape");
    ((UCHAR *)desc_at(in, 0, 2))[XHCI_MSI_RD_FLAGS] = 0;
    ((UCHAR *)desc_at(in, 0, 2))[XHCI_MSI_RD_SHARE] = 1;
    CHECK_EQ(XhciMsiNt5LineShape(in, &w), 0, "exclusive: not the shape");
    (void)build(twoRuns);
    XhciMsiWalk(in, &w);
    CHECK_EQ(XhciMsiNt5LineShape(in, &w), 0, "two runs: not the shape");
    (void)build(noLine);
    XhciMsiWalk(in, &w);
    CHECK_EQ(XhciMsiNt5LineShape(in, &w), 0, "no line: not the shape");
}

/* XhciInterruptMode 3's rewrite (record 18 section 4.2, revision 12): R1's
 * list becomes the message alone; any other class is not rewritten. */
static void test_keep(void)
{
    static const int lineOnly[] = { D_B, D_L, D_END };
    static const int msgOnly[] = { D_B, D_M, D_END };
    static const int none[] = { D_B, D_END };
    XHCI_MSI_WALK w;
    XHCI_MSI_CLASSIFY c;
    ULONG size;
    ULONG i;
    ULONG same;

    (void)r1_build(in, r1Mixed, 5);
    XhciMsiWalk(in, &w);
    XhciMsiClassify(in, &w, &c);
    CHECK_EQ(c.Class, XHCI_MSI_CLASS_REPLACEABLE, "R1 replaceable");
    size = XhciMsiKeepMessage(in, &w, &c, NULL, 0);
    CHECK_EQ(size, 0xA8, "sized: R1 less its line member");
    for (i = 0; i < BUF; i++) {
        out[i] = 0xCC;
    }
    CHECK_EQ(XhciMsiKeepMessage(in, &w, &c, out, BUF), 0xA8, "written");
    CHECK_EQ(out[0xA8], 0xCC, "nothing written past it");
    CHECK_EQ(XhciMsiKeepMessage(in, &w, &c, out, 0xA7), 0,
             "a capacity one short refused");
    (void)XhciMsiKeepMessage(in, &w, &c, out, BUF);
    /* R1's line member is its last descriptor, so the four kept are R1's
     * first four, byte for byte. */
    same = 1;
    for (i = 0x28; i < 0xA8; i++) {
        if (out[i] != in[i]) {
            same = 0;
        }
    }
    CHECK(same, "the kept descriptors copied unchanged");
    CHECK_EQ(count_at(out, 0), 4, "four descriptors kept");
    XhciMsiWalk(out, &w);
    CHECK_EQ(w.Verdict, XHCI_MSI_WALK_OK, "the result walks");
    CHECK_EQ(w.ListSize, 0xA8, "ListSize rewritten");
    XhciMsiClassify(out, &w, &c);
    CHECK_EQ(c.Class, XHCI_MSI_CLASS_NO_LINE, "the message alone");
    CHECK_EQ(c.MessageMembers, 1, "the message member kept");
    CHECK_EQ(c.LineMembers, 0, "the line member gone");
    CHECK_EQ(c.DevicePrivate, 1, "the device-private kept");

    (void)build(lineOnly);
    XhciMsiWalk(in, &w);
    XhciMsiClassify(in, &w, &c);
    CHECK_EQ(XhciMsiKeepMessage(in, &w, &c, out, BUF), 0,
             "no message: not rewritten");
    (void)build(msgOnly);
    XhciMsiWalk(in, &w);
    XhciMsiClassify(in, &w, &c);
    CHECK_EQ(c.Class, XHCI_MSI_CLASS_NO_LINE, "message only is NO_LINE");
    CHECK_EQ(XhciMsiKeepMessage(in, &w, &c, out, BUF), 0,
             "already the message alone: not rewritten");
    (void)build(none);
    XhciMsiWalk(in, &w);
    XhciMsiClassify(in, &w, &c);
    CHECK_EQ(XhciMsiKeepMessage(in, &w, &c, out, BUF), 0,
             "no interrupt: not rewritten");
    (void)r1_build(in, r1Mixed, 5);
    XhciMsiWalk(in, &w);
    XhciMsiClassify(in, &w, &c);
    w.Verdict = XHCI_MSI_WALK_COUNT;
    CHECK_EQ(XhciMsiKeepMessage(in, &w, &c, out, BUF), 0,
             "a list the walk refused");
}

/* ---- the filter record and the binding ---------------------------------- */

static void test_record(void)
{
    XHCI_MSI_FILTER_RECORD r;

    XhciMsiFilterInit(&r);
    CHECK_EQ(r.Class, XHCI_MSI_CLASS_UNKNOWN, "UNKNOWN from AddDevice");
    XhciMsiFilterBegin(&r);
    XhciMsiFilterPublish(&r, XHCI_MSI_CLASS_NO_LINE, XHCI_MSI_OUT_NO_LINE);
    CHECK_EQ(r.Sequence, 1, "the first filter");
    CHECK_EQ(r.Class, XHCI_MSI_CLASS_NO_LINE, "published");
    /* A later filter that fails, finds no list, or an inconsistent one,
     * leaves the class UNKNOWN. */
    XhciMsiFilterBegin(&r);
    XhciMsiFilterPublish(&r, XHCI_MSI_CLASS_UNKNOWN,
                         XHCI_MSI_OUT_LOWER_FAILED);
    CHECK_EQ(r.Class, XHCI_MSI_CLASS_UNKNOWN, "after a lower failure");
    XhciMsiFilterBegin(&r);
    XhciMsiFilterPublish(&r, XHCI_MSI_CLASS_NO_LINE, XHCI_MSI_OUT_NO_LINE);
    XhciMsiFilterBegin(&r);
    CHECK_EQ(r.Class, XHCI_MSI_CLASS_UNKNOWN, "after a NULL list");
    XhciMsiFilterBegin(&r);
    XhciMsiFilterPublish(&r, XHCI_MSI_CLASS_INCONSISTENT,
                         XHCI_MSI_OUT_INCONSISTENT);
    CHECK(r.Class != XHCI_MSI_CLASS_NO_LINE, "after an inconsistent list");
    CHECK_EQ(r.Sequence, 5, "every filter counted");
}

static void test_bind(void)
{
    XHCI_HLPR_BIND b;

    XhciHlprBindInit(&b);
    CHECK_EQ(XhciHlprBindStep(&b, XHCI_HLPR_EV_FILTER),
             XHCI_HLPR_ACT_LOOK, "a filter while unbound looks");
    CHECK_EQ(XhciHlprBindStep(&b, XHCI_HLPR_EV_LOOK_ABSENT), 0,
             "absent: nothing to drop, no further look");
    CHECK_EQ(b.State, XHCI_HLPR_STATE_ABSENT, "ABSENT");
    CHECK_EQ(XhciHlprBindStep(&b, XHCI_HLPR_EV_START),
             XHCI_HLPR_ACT_LOOK, "a start while unbound looks");
    CHECK_EQ(XhciHlprBindStep(&b, XHCI_HLPR_EV_LOOK_REFUSED),
             XHCI_HLPR_ACT_DROP_LOOK, "refused: its reference dropped");
    CHECK_EQ(b.State, XHCI_HLPR_STATE_REFUSED, "REFUSED");
    CHECK_EQ(b.Held, 0, "nothing held");
    CHECK_EQ(XhciHlprBindStep(&b, XHCI_HLPR_EV_FILTER),
             XHCI_HLPR_ACT_LOOK, "looks again");
    CHECK_EQ(XhciHlprBindStep(&b, XHCI_HLPR_EV_LOOK_FAILED),
             XHCI_HLPR_ACT_DROP_LOOK, "no IRP: its reference dropped");
    CHECK_EQ(XhciHlprBindStep(&b, XHCI_HLPR_EV_FILTER),
             XHCI_HLPR_ACT_LOOK, "looks again");
    CHECK_EQ(XhciHlprBindStep(&b, XHCI_HLPR_EV_LOOK_ACCEPTED), 0,
             "accepted: the reference kept");
    CHECK_EQ(b.State, XHCI_HLPR_STATE_BOUND, "BOUND");
    CHECK_EQ(b.Held, 1, "held");
    CHECK_EQ(XhciHlprBindStep(&b, XHCI_HLPR_EV_START), 0,
             "a start while bound does not look");
    CHECK_EQ(XhciHlprBindStep(&b, XHCI_HLPR_EV_STOP), 0,
             "stop asks for nothing");
    CHECK_EQ(XhciHlprBindStep(&b, XHCI_HLPR_EV_FILTER), 0,
             "the rebalance's filter does not look");
    CHECK_EQ(XhciHlprBindStep(&b, XHCI_HLPR_EV_START), 0,
             "nor its start; the binding kept");
    CHECK_EQ(XhciHlprBindStep(&b, XHCI_HLPR_EV_SURPRISE), 0,
             "surprise removal asks for nothing");
    CHECK_EQ(XhciHlprBindStep(&b, XHCI_HLPR_EV_REMOVE), XHCI_HLPR_ACT_DROP,
             "remove drops the held reference");
    CHECK_EQ(XhciHlprBindStep(&b, XHCI_HLPR_EV_REMOVE), 0,
             "exactly once");
    CHECK_EQ(XhciHlprBindStep(&b, XHCI_HLPR_EV_FILTER), 0,
             "and acquires none after");
    CHECK_EQ(b.Looks, 4, "four looks in all");

    XhciHlprBindInit(&b);
    CHECK_EQ(XhciHlprBindStep(&b, XHCI_HLPR_EV_STOP), 0, "stop: no look");
    CHECK_EQ(XhciHlprBindStep(&b, XHCI_HLPR_EV_SURPRISE), 0,
             "surprise: no look");
    CHECK_EQ(XhciHlprBindStep(&b, XHCI_HLPR_EV_LOOK_ABSENT), 0,
             "a look result asks for no look");
    CHECK_EQ(XhciHlprBindStep(&b, XHCI_HLPR_EV_REMOVE), 0,
             "remove with nothing held drops nothing");

    /* The timer's event (record 18 section 15.4) in every state. */
    XhciHlprBindInit(&b);
    CHECK_EQ(XhciHlprBindStep(&b, XHCI_HLPR_EV_TIMER), XHCI_HLPR_ACT_LOOK,
             "the timer's event while UNSEEN looks");
    CHECK_EQ(XhciHlprBindStep(&b, XHCI_HLPR_EV_LOOK_ABSENT), 0,
             "an absent helper: no further look asked");
    CHECK_EQ(b.State, XHCI_HLPR_STATE_ABSENT, "ABSENT");
    CHECK_EQ(XhciHlprBindStep(&b, XHCI_HLPR_EV_TIMER), XHCI_HLPR_ACT_LOOK,
             "the timer's event while ABSENT looks (the next start's)");
    CHECK_EQ(XhciHlprBindStep(&b, XHCI_HLPR_EV_LOOK_REFUSED),
             XHCI_HLPR_ACT_DROP_LOOK, "a refused answer: its reference dropped");
    CHECK_EQ(b.State, XHCI_HLPR_STATE_REFUSED, "REFUSED");
    CHECK_EQ(XhciHlprBindStep(&b, XHCI_HLPR_EV_TIMER), XHCI_HLPR_ACT_LOOK,
             "the timer's event while REFUSED looks");
    CHECK_EQ(XhciHlprBindStep(&b, XHCI_HLPR_EV_LOOK_ACCEPTED), 0,
             "accepted: the reference kept");
    CHECK_EQ(XhciHlprBindStep(&b, XHCI_HLPR_EV_TIMER), 0,
             "the timer's event while BOUND does not look");
    CHECK_EQ(XhciHlprBindStep(&b, XHCI_HLPR_EV_REMOVE), XHCI_HLPR_ACT_DROP,
             "the remove drops the binding's reference");
    CHECK_EQ(XhciHlprBindStep(&b, XHCI_HLPR_EV_TIMER), 0,
             "the timer's event while REMOVED does not look");
    CHECK_EQ(b.Looks, 3, "three looks");
}

/* ---- the channel, version 2 (record 18 section 15.3) -------------------- */

static int dummyConnect;
static int dummyDisconnect;
static int dummyTimer;
static int dummyFind;

static void fresh(XHCI_HLPR_QUERY *q)
{
    UCHAR *p;
    ULONG i;

    p = (UCHAR *)q;
    for (i = 0; i < sizeof(*q); i++) {
        p[i] = 0;
    }
    q->Size = XHCI_HLPR_V2_MIN;
    q->CallerMajor = XHCI_HLPR_MAJOR;
    q->CallerVersion = 0x02040000UL;
    q->CallerWants = XHCI_HLPR_SVC_MSI | XHCI_HLPR_SVC_TIMER;
}

/* A helper on NT 6 answering both services, or with `inject`. */
static ULONG answer(XHCI_HLPR_QUERY *q, PVOID c, PVOID d, PVOID t,
                    ULONG inject, ULONG *info)
{
    fresh(q);
    return XhciHlprAnswer(q, XHCI_HLPR_V2_MIN, XHCI_HLPR_V2_MIN, c, d, t,
                          &dummyFind, 0x02040000UL, inject, info);
}

static void test_channel(void)
{
    XHCI_HLPR_QUERY q;
    ULONG info;
    ULONG msi;
    ULONG tmr;
    UCHAR *guard;
    ULONG len;

    CHECK_EQ(sizeof(XHCI_HLPR_QUERY), 0x34, "the V2.0 block is 0x34 bytes");
    CHECK_EQ(XHCI_HLPR_IOCTL_QUERY_V2,
             (0x22UL << 16) | (0x902UL << 2) | 3UL, "function 0x902");

    CHECK_EQ(answer(&q, &dummyConnect, &dummyDisconnect, &dummyTimer, 0,
                    &info),
             XHCI_HLPR_ANS_OK, "a V2 query answered");
    CHECK_EQ(info, XHCI_HLPR_V2_MIN, "the whole block");
    CHECK_EQ(q.Magic, XHCI_HLPR_MAGIC, "magic 'XHLP'");
    CHECK_EQ(q.Returned, XHCI_HLPR_V2_MIN, "returned");
    CHECK_EQ(q.Services, XHCI_HLPR_SVC_MSI | XHCI_HLPR_SVC_TIMER,
             "both services, no routine bit to a V2.0 block");
    CHECK_EQ(q.HelperMinor, 1, "a minor 1 helper");
    CHECK_EQ(q.CallerVersion, 0x02040000UL, "the caller's fields untouched");
    CHECK_EQ(q.CallerWants, 3, "CallerWants untouched");
    CHECK_EQ(XhciHlprAccept(1, info, sizeof(q), &q, 1, &msi, &tmr),
             XHCI_HLPR_ACCEPTED, "accepted on NT 6");
    CHECK_EQ(msi, XHCI_HLPR_SVC_ACCEPTED, "MSI accepted");
    CHECK_EQ(tmr, XHCI_HLPR_SVC_ACCEPTED, "the timer accepted");

    /* The channel's tests, each failing alone: no service judged. */
    CHECK_EQ(XhciHlprAccept(0, info, sizeof(q), &q, 1, &msi, &tmr),
             XHCI_HLPR_REFUSE_STATUS, "status");
    CHECK_EQ(msi + tmr, 0, "a refusal judges no service");
    CHECK_EQ(XhciHlprAccept(1, info - 4, sizeof(q), &q, 1, &msi, &tmr),
             XHCI_HLPR_REFUSE_LENGTH, "Information disagrees");
    CHECK_EQ(XhciHlprAccept(1, info, info - 4, &q, 1, &msi, &tmr),
             XHCI_HLPR_REFUSE_LENGTH, "above the capacity");
    q.Returned = 0x30;
    CHECK_EQ(XhciHlprAccept(1, 0x30, sizeof(q), &q, 1, &msi, &tmr),
             XHCI_HLPR_REFUSE_LENGTH, "0x30 leaves the timer field uncovered");
    (void)answer(&q, &dummyConnect, &dummyDisconnect, &dummyTimer, 0, &info);
    q.Magic = 0x49534D58UL;
    CHECK_EQ(XhciHlprAccept(1, info, sizeof(q), &q, 1, &msi, &tmr),
             XHCI_HLPR_REFUSE_IDENTITY, "the V1 magic is refused");
    (void)answer(&q, &dummyConnect, &dummyDisconnect, &dummyTimer, 0, &info);
    q.HelperMajor = 1;
    CHECK_EQ(XhciHlprAccept(1, info, sizeof(q), &q, 1, &msi, &tmr),
             XHCI_HLPR_REFUSE_IDENTITY, "major 1 is refused");

    /* Windows 2000 and XP: the helper answers the timer alone; the MSI
     * half is not asked whatever the answer says. */
    (void)answer(&q, NULL, NULL, &dummyTimer, 0, &info);
    CHECK_EQ(q.Services, XHCI_HLPR_SVC_TIMER, "a 2000 or XP helper: timer");
    CHECK_EQ(XhciHlprAccept(1, info, sizeof(q), &q, 0, &msi, &tmr),
             XHCI_HLPR_ACCEPTED, "accepted below NT 6");
    CHECK_EQ(msi, XHCI_HLPR_SVC_NOT_ASKED, "MSI not asked");
    CHECK_EQ(tmr, XHCI_HLPR_SVC_ACCEPTED, "the timer accepted");
    (void)answer(&q, &dummyConnect, &dummyDisconnect, &dummyTimer, 0, &info);
    CHECK_EQ(XhciHlprAccept(1, info, sizeof(q), &q, 0, &msi, &tmr),
             XHCI_HLPR_ACCEPTED, "an MSI half offered to a 2000 or XP caller");
    CHECK_EQ(msi, XHCI_HLPR_SVC_NOT_ASKED, "still not asked");

    /* Each service's three verdicts with the other accepted. */
    (void)answer(&q, NULL, NULL, &dummyTimer, 0, &info);
    (void)XhciHlprAccept(1, info, sizeof(q), &q, 1, &msi, &tmr);
    CHECK_EQ(msi, XHCI_HLPR_SVC_NOT_OFFERED, "MSI not offered");
    CHECK_EQ(tmr, XHCI_HLPR_SVC_ACCEPTED, "the timer still accepted");
    (void)answer(&q, &dummyConnect, NULL, &dummyTimer, 0, &info);
    CHECK_EQ(q.Services & XHCI_HLPR_SVC_MSI, 0, "a half pair is not offered");
    (void)answer(&q, &dummyConnect, &dummyDisconnect, &dummyTimer,
                 XHCI_HLPR_INJ_NULL_DISC, &info);
    (void)XhciHlprAccept(1, info, sizeof(q), &q, 1, &msi, &tmr);
    CHECK_EQ(msi, XHCI_HLPR_SVC_MALFORMED, "MSI bit set, a NULL disconnect");
    CHECK_EQ(tmr, XHCI_HLPR_SVC_ACCEPTED, "the timer apart");
    (void)answer(&q, &dummyConnect, &dummyConnect, &dummyTimer, 0, &info);
    (void)XhciHlprAccept(1, info, sizeof(q), &q, 1, &msi, &tmr);
    CHECK_EQ(msi, XHCI_HLPR_SVC_MALFORMED, "an equal pair is malformed");
    (void)answer(&q, &dummyConnect, &dummyDisconnect, NULL, 0, &info);
    (void)XhciHlprAccept(1, info, sizeof(q), &q, 1, &msi, &tmr);
    CHECK_EQ(tmr, XHCI_HLPR_SVC_NOT_OFFERED, "the timer not offered");
    CHECK_EQ(msi, XHCI_HLPR_SVC_ACCEPTED, "MSI apart");
    (void)answer(&q, &dummyConnect, &dummyDisconnect, &dummyTimer, 0, &info);
    q.SetTimerResolution = q.ConnectEx;
    (void)XhciHlprAccept(1, info, sizeof(q), &q, 1, &msi, &tmr);
    CHECK_EQ(tmr, XHCI_HLPR_SVC_MALFORMED, "a timer pointer equal to ConnectEx");
    (void)answer(&q, &dummyConnect, &dummyDisconnect, &dummyTimer, 0, &info);
    q.Services |= 0xFFFFFFFCUL;
    CHECK_EQ(XhciHlprAccept(1, info, sizeof(q), &q, 1, &msi, &tmr),
             XHCI_HLPR_ACCEPTED, "bits above 1 ignored");
    CHECK_EQ(msi + tmr, 2, "both still accepted");

    /* Refusals before any read of the block, at the end of an allocation. */
    CHECK_EQ(XhciHlprAnswer(NULL, 0x34, 0x34, &dummyConnect, &dummyDisconnect,
                            &dummyTimer, &dummyFind, 0, 0, &info),
             XHCI_HLPR_ANS_INVALID, "a NULL buffer");
    for (len = 0; len < 4; len++) {
        guard = (UCHAR *)malloc(len == 0 ? 1 : len);
        CHECK_EQ(XhciHlprAnswer(guard + len, len, len, &dummyConnect,
                                &dummyDisconnect, &dummyTimer, &dummyFind,
                                0, 0, &info),
                 XHCI_HLPR_ANS_TOO_SMALL,
                 "a buffer shorter than Size, at the end of an allocation");
        CHECK_EQ(info, 0, "Information 0");
        free(guard);
    }
    guard = (UCHAR *)malloc(0x33);
    CHECK_EQ(XhciHlprAnswer(guard, 0x33, 0x33, &dummyConnect,
                            &dummyDisconnect, &dummyTimer, &dummyFind,
                            0, 0, &info),
             XHCI_HLPR_ANS_TOO_SMALL, "0x33 bytes");
    free(guard);
    guard = (UCHAR *)malloc(0x28);
    CHECK_EQ(XhciHlprAnswer(guard, 0x28, 0x28, &dummyConnect,
                            &dummyDisconnect, &dummyTimer, &dummyFind,
                            0, 0, &info),
             XHCI_HLPR_ANS_TOO_SMALL, "a V1-sized block");
    free(guard);
    fresh(&q);
    CHECK_EQ(XhciHlprAnswer(&q, 0x34, 0x38, &dummyConnect, &dummyDisconnect,
                            &dummyTimer, &dummyFind, 0, 0, &info),
             XHCI_HLPR_ANS_INVALID, "unequal lengths");
    fresh(&q);
    q.Size = 0x38;
    CHECK_EQ(XhciHlprAnswer(&q, 0x34, 0x34, &dummyConnect, &dummyDisconnect,
                            &dummyTimer, &dummyFind, 0, 0, &info),
             XHCI_HLPR_ANS_INVALID, "Size disagrees with the length");
    fresh(&q);
    q.CallerMajor = 1;
    CHECK_EQ(XhciHlprAnswer(&q, 0x34, 0x34, &dummyConnect, &dummyDisconnect,
                            &dummyTimer, &dummyFind, 0, 0, &info),
             XHCI_HLPR_ANS_REVISION, "a V1 caller refused");

    /* A newer caller's longer block: filled to the helper's own size. */
    {
        UCHAR big[0x40];
        XHCI_HLPR_QUERY *bq;
        ULONG routine;
        ULONG i;

        for (i = 0; i < sizeof(big); i++) {
            big[i] = 0xEE;
        }
        bq = (XHCI_HLPR_QUERY *)big;
        bq->Size = 0x40;
        bq->CallerMajor = XHCI_HLPR_MAJOR;
        CHECK_EQ(XhciHlprAnswer(big, 0x40, 0x40, &dummyConnect,
                                &dummyDisconnect, &dummyTimer, &dummyFind,
                                0, 0, &info),
                 XHCI_HLPR_ANS_OK, "a longer block answered");
        CHECK_EQ(info, XHCI_HLPR_V21_SIZE, "to the helper's own size");
        CHECK_EQ(big[0x38], 0xEE, "nothing written past it");
        CHECK_EQ(XhciHlprAccept(1, info, 0x40, bq, 1, &msi, &tmr),
                 XHCI_HLPR_ACCEPTED, "and accepted by that caller");
        CHECK_EQ(XhciHlprAcceptRoutine(1, info, 0x40,
                                       (XHCI_HLPR_QUERY1 *)big, &routine),
                 XHCI_HLPR_ACCEPTED, "and by a minor 1 caller");
        CHECK_EQ(routine, XHCI_HLPR_SVC_ACCEPTED, "its routine accepted");
    }

    /* The qemu injections (record 18 sections 2.1 and 15.12, N3). */
    (void)answer(&q, &dummyConnect, &dummyDisconnect, &dummyTimer,
                 XHCI_HLPR_INJ_MAJOR, &info);
    CHECK_EQ(XhciHlprAccept(1, info, 0x34, &q, 1, &msi, &tmr),
             XHCI_HLPR_REFUSE_IDENTITY, "another major refused");
    CHECK_EQ(q.Flags, XHCI_HLPR_F_INJECTED, "the injection flagged");
    (void)answer(&q, &dummyConnect, &dummyDisconnect, &dummyTimer,
                 XHCI_HLPR_INJ_SHORT, &info);
    CHECK_EQ(XhciHlprAccept(1, info, 0x34, &q, 1, &msi, &tmr),
             XHCI_HLPR_REFUSE_LENGTH, "a short answer refused");
    (void)answer(&q, &dummyConnect, &dummyDisconnect, &dummyTimer,
                 XHCI_HLPR_INJ_MAGIC, &info);
    CHECK_EQ(XhciHlprAccept(1, info, 0x34, &q, 1, &msi, &tmr),
             XHCI_HLPR_REFUSE_IDENTITY, "a wrong magic refused");
    (void)answer(&q, &dummyConnect, &dummyDisconnect, &dummyTimer,
                 XHCI_HLPR_INJ_TIMER_OFF, &info);
    (void)XhciHlprAccept(1, info, 0x34, &q, 1, &msi, &tmr);
    CHECK_EQ(tmr, XHCI_HLPR_SVC_NOT_OFFERED, "7: the timer service off");
    CHECK_EQ(msi, XHCI_HLPR_SVC_ACCEPTED, "7: MSI unchanged");
    (void)answer(&q, &dummyConnect, &dummyDisconnect, &dummyTimer,
                 XHCI_HLPR_INJ_TIMER_NULL, &info);
    (void)XhciHlprAccept(1, info, 0x34, &q, 1, &msi, &tmr);
    CHECK_EQ(tmr, XHCI_HLPR_SVC_MALFORMED, "8: bit 1 set, pointer NULL");
    CHECK_EQ(msi, XHCI_HLPR_SVC_ACCEPTED, "8: MSI unchanged");
    (void)answer(&q, &dummyConnect, &dummyDisconnect, &dummyTimer,
                 XHCI_HLPR_INJ_MSI_OFF, &info);
    (void)XhciHlprAccept(1, info, 0x34, &q, 1, &msi, &tmr);
    CHECK_EQ(msi, XHCI_HLPR_SVC_NOT_OFFERED, "9: the MSI half off");
    CHECK_EQ(tmr, XHCI_HLPR_SVC_ACCEPTED, "9: the timer unchanged");
    (void)answer(&q, &dummyConnect, &dummyDisconnect, &dummyTimer,
                 XHCI_HLPR_INJ_ROUTINE_OFF, &info);
    CHECK_EQ(XhciHlprAccept(1, info, 0x34, &q, 1, &msi, &tmr),
             XHCI_HLPR_ACCEPTED, "10: a V2.0 caller unaffected");
    CHECK_EQ(msi + tmr, 2, "10: both its services accepted");
}

/* ---- the channel's minor 1, the routine service (record 32 section
 * 12.8.15.15) ------------------------------------------------------------ */

static void fresh1(XHCI_HLPR_QUERY1 *q)
{
    UCHAR *p;
    ULONG i;

    p = (UCHAR *)q;
    for (i = 0; i < sizeof(*q); i++) {
        p[i] = 0;
    }
    q->V2.Size = XHCI_HLPR_V21_SIZE;
    q->V2.CallerMajor = XHCI_HLPR_MAJOR;
    q->V2.CallerMinor = XHCI_HLPR_MINOR;
    q->V2.CallerVersion = 0x03040100UL;
    q->V2.CallerWants = XHCI_HLPR_SVC_ROUTINE;
}

/* A minor 1 helper answering a minor 1 block, or with `inject`. */
static ULONG answer1(XHCI_HLPR_QUERY1 *q, PVOID find, ULONG inject,
                     ULONG *info)
{
    fresh1(q);
    return XhciHlprAnswer(q, XHCI_HLPR_V21_SIZE, XHCI_HLPR_V21_SIZE,
                          &dummyConnect, &dummyDisconnect, &dummyTimer, find,
                          0x03040100UL, inject, info);
}

static void test_channel1(void)
{
    XHCI_HLPR_QUERY1 q;
    ULONG info;
    ULONG routine;

    CHECK_EQ(sizeof(XHCI_HLPR_QUERY1), 0x38, "the V2.1 block is 0x38 bytes");
    CHECK_EQ(XHCI_HLPR_SVC_ROUTINE, 4, "the routine service is bit 2");

    CHECK_EQ(answer1(&q, &dummyFind, 0, &info), XHCI_HLPR_ANS_OK,
             "a minor 1 query answered");
    CHECK_EQ(info, XHCI_HLPR_V21_SIZE, "the whole V2.1 block");
    CHECK_EQ(q.V2.Returned, XHCI_HLPR_V21_SIZE, "returned 0x38");
    CHECK_EQ(q.V2.HelperMinor, 1, "minor 1");
    CHECK_EQ(q.V2.Services & XHCI_HLPR_SVC_ROUTINE, XHCI_HLPR_SVC_ROUTINE,
             "the routine bit");
    CHECK(q.FindRoutine == (PVOID)&dummyFind, "the routine handed out");
    CHECK_EQ(q.V2.CallerWants, XHCI_HLPR_SVC_ROUTINE,
             "CallerWants untouched");
    CHECK_EQ(XhciHlprAcceptRoutine(1, info, sizeof(q), &q, &routine),
             XHCI_HLPR_ACCEPTED, "the channel accepted");
    CHECK_EQ(routine, XHCI_HLPR_SVC_ACCEPTED, "the routine accepted");

    /* A minor 0 helper (every helper before this change): it fills 0x34
     * bytes of the 0x38 block and knows no bit 2. */
    (void)answer1(&q, &dummyFind, 0, &info);
    q.V2.Returned = XHCI_HLPR_V2_MIN;
    q.V2.HelperMinor = 0;
    q.V2.Services &= ~XHCI_HLPR_SVC_ROUTINE;
    q.FindRoutine = NULL;
    CHECK_EQ(XhciHlprAcceptRoutine(1, XHCI_HLPR_V2_MIN, sizeof(q), &q,
                                   &routine),
             XHCI_HLPR_ACCEPTED, "a minor 0 helper's channel accepted");
    CHECK_EQ(routine, XHCI_HLPR_SVC_NOT_OFFERED,
             "a minor 0 helper: the routine not offered");
    q.FindRoutine = (PVOID)&dummyFind;
    (void)XhciHlprAcceptRoutine(1, XHCI_HLPR_V2_MIN, sizeof(q), &q, &routine);
    CHECK_EQ(routine, XHCI_HLPR_SVC_NOT_OFFERED,
             "a field past Returned is not read");
    q.V2.Services |= XHCI_HLPR_SVC_ROUTINE;
    (void)XhciHlprAcceptRoutine(1, XHCI_HLPR_V2_MIN, sizeof(q), &q, &routine);
    CHECK_EQ(routine, XHCI_HLPR_SVC_MALFORMED,
             "bit 2 set, its field uncovered");

    /* A minor 1 helper without the routine. */
    CHECK_EQ(answer1(&q, NULL, 0, &info), XHCI_HLPR_ANS_OK,
             "answered without the routine");
    CHECK_EQ(q.V2.Services & XHCI_HLPR_SVC_ROUTINE, 0, "no bit 2");
    (void)XhciHlprAcceptRoutine(1, info, sizeof(q), &q, &routine);
    CHECK_EQ(routine, XHCI_HLPR_SVC_NOT_OFFERED, "not offered");
    (void)answer1(&q, &dummyFind, XHCI_HLPR_INJ_ROUTINE_OFF, &info);
    CHECK_EQ(q.V2.Flags, XHCI_HLPR_F_INJECTED, "10: flagged");
    CHECK(q.FindRoutine == NULL, "10: no pointer");
    (void)XhciHlprAcceptRoutine(1, info, sizeof(q), &q, &routine);
    CHECK_EQ(routine, XHCI_HLPR_SVC_NOT_OFFERED, "10: the routine off");

    /* Malformed answers. */
    (void)answer1(&q, &dummyFind, 0, &info);
    q.FindRoutine = NULL;
    (void)XhciHlprAcceptRoutine(1, info, sizeof(q), &q, &routine);
    CHECK_EQ(routine, XHCI_HLPR_SVC_MALFORMED, "bit 2 set, pointer NULL");
    (void)answer1(&q, NULL, 0, &info);
    q.FindRoutine = (PVOID)&dummyFind;
    (void)XhciHlprAcceptRoutine(1, info, sizeof(q), &q, &routine);
    CHECK_EQ(routine, XHCI_HLPR_SVC_MALFORMED, "bit 2 clear, a pointer");

    /* The channel's own tests: no service judged. */
    (void)answer1(&q, &dummyFind, 0, &info);
    CHECK_EQ(XhciHlprAcceptRoutine(0, info, sizeof(q), &q, &routine),
             XHCI_HLPR_REFUSE_STATUS, "the IRP failed");
    CHECK_EQ(routine, XHCI_HLPR_SVC_NONE, "nothing judged");
    CHECK_EQ(XhciHlprAcceptRoutine(1, info, XHCI_HLPR_V2_MIN, &q, &routine),
             XHCI_HLPR_REFUSE_LENGTH, "above the capacity");
    CHECK_EQ(XhciHlprAcceptRoutine(1, info - 4, sizeof(q), &q, &routine),
             XHCI_HLPR_REFUSE_LENGTH, "Information disagrees");
    (void)answer1(&q, &dummyFind, XHCI_HLPR_INJ_MAJOR, &info);
    CHECK_EQ(XhciHlprAcceptRoutine(1, info, sizeof(q), &q, &routine),
             XHCI_HLPR_REFUSE_IDENTITY, "another major");
    CHECK_EQ(routine, XHCI_HLPR_SVC_NONE, "nothing judged");
    (void)answer1(&q, &dummyFind, XHCI_HLPR_INJ_SHORT, &info);
    CHECK_EQ(XhciHlprAcceptRoutine(1, info, sizeof(q), &q, &routine),
             XHCI_HLPR_REFUSE_LENGTH, "a short answer");
    CHECK_EQ(q.V2.Services & XHCI_HLPR_SVC_ROUTINE, 0,
             "a short answer sets no bit 2");
    (void)answer1(&q, &dummyFind, XHCI_HLPR_INJ_MAGIC, &info);
    CHECK_EQ(XhciHlprAcceptRoutine(1, info, sizeof(q), &q, &routine),
             XHCI_HLPR_REFUSE_IDENTITY, "a wrong magic");
}

/* ---- the trace ---------------------------------------------------------- */

static void test_trace(void)
{
    char line[XHCI_MSI_TRACE_MAX];
    ULONG f[XHCI_MSI_TRACE_FIELDS];
    ULONG len;
    ULONG i;
    ULONG sum;

    for (i = 0; i < XHCI_MSI_TRACE_FIELDS; i++) {
        f[i] = 0xFFFFFFFFUL;
    }
    len = XhciMsiTraceFormat(line, 'D', 0x1234, 0xABCD, 0xFFFF, f,
                             XHCI_MSI_TRACE_FIELDS);
    CHECK(len < XHCI_MSI_TRACE_MAX, "the longest record fits");
    CHECK(len + 8 + 1 < 128, "with the debug channel's prefix and newline");
    CHECK(line[0] == 'M' && line[4] == 'D', "the record's head");
    sum = 0;
    for (i = 0; i + 4 < len; i++) {
        sum += (ULONG)(UCHAR)line[i];
    }
    CHECK_EQ(strtoul(line + len - 2, NULL, 16), sum & 0xFF,
             "the checksum is the byte sum before it");
    len = XhciMsiTraceFormat(line, 'L', 1, 2, 3, f, 99);
    CHECK(len < XHCI_MSI_TRACE_MAX, "a count past ten is held to ten");
}

static void test_version(void)
{
    CHECK_EQ(XhciHlprPackVersion("2.3.0.0"), 0x02030000UL, "2.3.0.0");
    CHECK_EQ(XhciHlprPackVersion("1.2.300.4"), 0x0102FF04UL,
             "a part held to 255");
    CHECK_EQ(XhciHlprPackVersion("7"), 0x07000000UL, "one part");
}

int main(void)
{
    test_version();
    test_cap();
    test_walk();
    test_strip();
    test_r1();
    test_admit();
    test_inj();
    test_nt5();
    test_nt5_gsi();
    test_nt5_strip();
    test_keep();
    test_record();
    test_bind();
    test_channel();
    test_channel1();
    test_trace();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
