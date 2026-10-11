/*
 * test_msi.c - host vectors for message-signalled interrupts' pure half
 * (src\xhci_msi.c, src\xhci_msi86.c; design record 15 section 4.3 and
 * design record 18): the capability read, the requirements list's checked
 * walk, its classification, the strip and the qemu no-line rewrite, the
 * filter record, the helper binding's state machine, the channel's two
 * halves and the trace record.
 */

#include <stdio.h>
#include <stdlib.h>
#include "../src/xhci_msi.h"
#include "../src/xhci_msi86.h"
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
    /* A loop is bounded; a pointer out of range or unaligned stops it. */
    cfg[0x71] = 0x50;
    cfg[0x73] = 0x00;
    cfg[0x52] = 0x01;
    CHECK_EQ(XhciMsiCapRead(cfg), XHCI_POLL_DELIVERY_MSI,
             "a cycle ends at the bound");
    cfg[0x34] = 0x3C;
    CHECK_EQ(XhciMsiCapRead(cfg), XHCI_POLL_DELIVERY_MESSAGE,
             "a pointer below 40h is not followed");
    cfg[0x34] = 0x51;
    CHECK_EQ(XhciMsiCapRead(cfg), XHCI_POLL_DELIVERY_MESSAGE,
             "an unaligned pointer is not followed");
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
    XHCI_MSI86_BIND b;

    XhciMsi86BindInit(&b);
    CHECK_EQ(XhciMsi86BindStep(&b, XHCI_MSI86_EV_FILTER),
             XHCI_MSI86_ACT_LOOK, "a filter while unbound looks");
    CHECK_EQ(XhciMsi86BindStep(&b, XHCI_MSI86_EV_LOOK_ABSENT), 0,
             "absent: nothing to drop, no further look");
    CHECK_EQ(b.State, XHCI_MSI86_STATE_ABSENT, "ABSENT");
    CHECK_EQ(XhciMsi86BindStep(&b, XHCI_MSI86_EV_START),
             XHCI_MSI86_ACT_LOOK, "a start while unbound looks");
    CHECK_EQ(XhciMsi86BindStep(&b, XHCI_MSI86_EV_LOOK_REFUSED),
             XHCI_MSI86_ACT_DROP_LOOK, "refused: its reference dropped");
    CHECK_EQ(b.State, XHCI_MSI86_STATE_REFUSED, "REFUSED");
    CHECK_EQ(b.Held, 0, "nothing held");
    CHECK_EQ(XhciMsi86BindStep(&b, XHCI_MSI86_EV_FILTER),
             XHCI_MSI86_ACT_LOOK, "looks again");
    CHECK_EQ(XhciMsi86BindStep(&b, XHCI_MSI86_EV_LOOK_FAILED),
             XHCI_MSI86_ACT_DROP_LOOK, "no IRP: its reference dropped");
    CHECK_EQ(XhciMsi86BindStep(&b, XHCI_MSI86_EV_FILTER),
             XHCI_MSI86_ACT_LOOK, "looks again");
    CHECK_EQ(XhciMsi86BindStep(&b, XHCI_MSI86_EV_LOOK_ACCEPTED), 0,
             "accepted: the reference kept");
    CHECK_EQ(b.State, XHCI_MSI86_STATE_BOUND, "BOUND");
    CHECK_EQ(b.Held, 1, "held");
    CHECK_EQ(XhciMsi86BindStep(&b, XHCI_MSI86_EV_START), 0,
             "a start while bound does not look");
    CHECK_EQ(XhciMsi86BindStep(&b, XHCI_MSI86_EV_STOP), 0,
             "stop asks for nothing");
    CHECK_EQ(XhciMsi86BindStep(&b, XHCI_MSI86_EV_FILTER), 0,
             "the rebalance's filter does not look");
    CHECK_EQ(XhciMsi86BindStep(&b, XHCI_MSI86_EV_START), 0,
             "nor its start; the binding kept");
    CHECK_EQ(XhciMsi86BindStep(&b, XHCI_MSI86_EV_SURPRISE), 0,
             "surprise removal asks for nothing");
    CHECK_EQ(XhciMsi86BindStep(&b, XHCI_MSI86_EV_REMOVE), XHCI_MSI86_ACT_DROP,
             "remove drops the held reference");
    CHECK_EQ(XhciMsi86BindStep(&b, XHCI_MSI86_EV_REMOVE), 0,
             "exactly once");
    CHECK_EQ(XhciMsi86BindStep(&b, XHCI_MSI86_EV_FILTER), 0,
             "and acquires none after");
    CHECK_EQ(b.Looks, 4, "four looks in all");

    XhciMsi86BindInit(&b);
    CHECK_EQ(XhciMsi86BindStep(&b, XHCI_MSI86_EV_STOP), 0, "stop: no look");
    CHECK_EQ(XhciMsi86BindStep(&b, XHCI_MSI86_EV_SURPRISE), 0,
             "surprise: no look");
    CHECK_EQ(XhciMsi86BindStep(&b, XHCI_MSI86_EV_LOOK_ABSENT), 0,
             "a look result asks for no look");
    CHECK_EQ(XhciMsi86BindStep(&b, XHCI_MSI86_EV_REMOVE), 0,
             "remove with nothing held drops nothing");
}

/* ---- the channel -------------------------------------------------------- */

static int dummyConnect;
static int dummyDisconnect;

static void fresh(XHCI_MSI86_QUERY *q)
{
    UCHAR *p;
    ULONG i;

    p = (UCHAR *)q;
    for (i = 0; i < sizeof(*q); i++) {
        p[i] = 0;
    }
    q->Size = XHCI_MSI86_V1_MIN;
    q->CallerMajor = XHCI_MSI86_MAJOR;
    q->CallerVersion = 0x02030000UL;
}

static void test_channel(void)
{
    XHCI_MSI86_QUERY q;
    ULONG info;
    UCHAR *guard;
    ULONG len;

    fresh(&q);
    CHECK_EQ(XhciMsi86Answer(&q, sizeof(q), sizeof(q), &dummyConnect,
                             &dummyDisconnect, 0x02030000UL, 0, &info),
             XHCI_MSI86_ANS_OK, "a V1 query answered");
    CHECK_EQ(info, XHCI_MSI86_V1_MIN, "the whole block");
    CHECK_EQ(q.Magic, XHCI_MSI86_MAGIC, "magic");
    CHECK_EQ(q.Returned, XHCI_MSI86_V1_MIN, "returned");
    CHECK_EQ(q.CallerVersion, 0x02030000UL, "the caller's field untouched");
    CHECK_EQ(XhciMsi86Accept(1, info, sizeof(q), &q), XHCI_MSI86_ACCEPTED,
             "accepted");
    CHECK_EQ(XhciMsi86Accept(0, info, sizeof(q), &q),
             XHCI_MSI86_REFUSE_STATUS, "test 1: status");
    CHECK_EQ(XhciMsi86Accept(1, info - 4, sizeof(q), &q),
             XHCI_MSI86_REFUSE_LENGTH, "test 2: Information disagrees");
    CHECK_EQ(XhciMsi86Accept(1, info, info - 4, &q),
             XHCI_MSI86_REFUSE_LENGTH, "test 2: above the capacity");
    q.Returned = 0x24;
    CHECK_EQ(XhciMsi86Accept(1, 0x24, sizeof(q), &q),
             XHCI_MSI86_REFUSE_LENGTH, "test 2: 0x24 leaves Flags uncovered");

    /* Refusals before any read of the block. */
    CHECK_EQ(XhciMsi86Answer(NULL, 0x28, 0x28, &dummyConnect,
                             &dummyDisconnect, 0, 0, &info),
             XHCI_MSI86_ANS_INVALID, "a NULL buffer");
    for (len = 0; len < 4; len++) {
        guard = (UCHAR *)malloc(len == 0 ? 1 : len);
        CHECK_EQ(XhciMsi86Answer(guard + len, len, len, &dummyConnect,
                                 &dummyDisconnect, 0, 0, &info),
                 XHCI_MSI86_ANS_TOO_SMALL,
                 "a buffer shorter than Size, at the end of an allocation");
        CHECK_EQ(info, 0, "Information 0");
        free(guard);
    }
    guard = (UCHAR *)malloc(0x27);
    CHECK_EQ(XhciMsi86Answer(guard, 0x27, 0x27, &dummyConnect,
                             &dummyDisconnect, 0, 0, &info),
             XHCI_MSI86_ANS_TOO_SMALL, "0x27 bytes");
    free(guard);
    fresh(&q);
    CHECK_EQ(XhciMsi86Answer(&q, 0x28, 0x2C, &dummyConnect,
                             &dummyDisconnect, 0, 0, &info),
             XHCI_MSI86_ANS_INVALID, "unequal lengths");
    fresh(&q);
    q.Size = 0x30;
    CHECK_EQ(XhciMsi86Answer(&q, 0x28, 0x28, &dummyConnect,
                             &dummyDisconnect, 0, 0, &info),
             XHCI_MSI86_ANS_INVALID, "Size disagrees with the length");
    fresh(&q);
    q.CallerMajor = 2;
    CHECK_EQ(XhciMsi86Answer(&q, 0x28, 0x28, &dummyConnect,
                             &dummyDisconnect, 0, 0, &info),
             XHCI_MSI86_ANS_REVISION, "an unknown major");

    /* A newer caller's longer block: filled to the helper's own size. */
    {
        UCHAR big[0x30];
        XHCI_MSI86_QUERY *b;
        ULONG i;

        for (i = 0; i < sizeof(big); i++) {
            big[i] = 0xEE;
        }
        b = (XHCI_MSI86_QUERY *)big;
        b->Size = 0x30;
        b->CallerMajor = XHCI_MSI86_MAJOR;
        CHECK_EQ(XhciMsi86Answer(big, 0x30, 0x30, &dummyConnect,
                                 &dummyDisconnect, 0, 0, &info),
                 XHCI_MSI86_ANS_OK, "a longer block answered");
        CHECK_EQ(info, XHCI_MSI86_V1_MIN, "to the helper's own size");
        CHECK_EQ(big[0x2C], 0xEE, "nothing written past it");
        CHECK_EQ(XhciMsi86Accept(1, info, 0x30, b), XHCI_MSI86_ACCEPTED,
                 "and accepted by that caller");
    }

    /* The qemu injections. */
    fresh(&q);
    (void)XhciMsi86Answer(&q, 0x28, 0x28, &dummyConnect, &dummyDisconnect, 0,
                          XHCI_MSI86_INJ_MAJOR2, &info);
    CHECK_EQ(XhciMsi86Accept(1, info, 0x28, &q), XHCI_MSI86_REFUSE_IDENTITY,
             "another major refused");
    CHECK_EQ(q.Flags, XHCI_MSI86_F_INJECTED, "the injection flagged");
    fresh(&q);
    (void)XhciMsi86Answer(&q, 0x28, 0x28, &dummyConnect, &dummyDisconnect, 0,
                          XHCI_MSI86_INJ_SHORT, &info);
    CHECK_EQ(XhciMsi86Accept(1, info, 0x28, &q), XHCI_MSI86_REFUSE_LENGTH,
             "a short answer refused");
    fresh(&q);
    (void)XhciMsi86Answer(&q, 0x28, 0x28, &dummyConnect, &dummyDisconnect, 0,
                          XHCI_MSI86_INJ_NULL_DISC, &info);
    CHECK_EQ(XhciMsi86Accept(1, info, 0x28, &q), XHCI_MSI86_REFUSE_ROUTINES,
             "a NULL disconnect refused");
    fresh(&q);
    (void)XhciMsi86Answer(&q, 0x28, 0x28, &dummyConnect, &dummyDisconnect, 0,
                          XHCI_MSI86_INJ_MAGIC, &info);
    CHECK_EQ(XhciMsi86Accept(1, info, 0x28, &q), XHCI_MSI86_REFUSE_IDENTITY,
             "a wrong magic refused");
    fresh(&q);
    (void)XhciMsi86Answer(&q, 0x28, 0x28, &dummyConnect, &dummyConnect, 0, 0,
                          &info);
    CHECK_EQ(XhciMsi86Accept(1, info, 0x28, &q), XHCI_MSI86_REFUSE_ROUTINES,
             "an equal pair refused");
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
    CHECK_EQ(XhciMsi86PackVersion("2.3.0.0"), 0x02030000UL, "2.3.0.0");
    CHECK_EQ(XhciMsi86PackVersion("1.2.300.4"), 0x0102FF04UL,
             "a part held to 255");
    CHECK_EQ(XhciMsi86PackVersion("7"), 0x07000000UL, "one part");
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
    test_keep();
    test_record();
    test_bind();
    test_channel();
    test_trace();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
