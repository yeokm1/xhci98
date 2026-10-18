/*
 * test_cm_noirq.c - a Windows 98 SE or ME function with no interrupt,
 * started: the pure half (src\cm\cm_noirq.c; design record 28 section
 * 8.1, task 42-A.15), over a modelled configuration manager - one devnode
 * whose status, header, binding, logical configurations and descriptors
 * the operations read and write as CONFIGMG would, with failures,
 * disappearances, re-creations and a clock under the test's hand.
 */

#include <stdio.h>
#include <string.h>
#include "../src/cm/cm_noirq.h"
#include "test_harness.h"

#define BDF(b, d, f) ((((ULONG)(b)) << 8) | ((ULONG)(d) << 3) | (ULONG)(f))
#define PORT BDF(4, 1, 0)
#define FN   BDF(8, 1, 0)       /* device 1: INTA arrives on INTB: unroutable */
#define DN   0x00C0FFEEUL
#define HASH 0x12345678UL

/* ---- the model -------------------------------------------------------- */

#define FLC_MAX 8
#define FRD_MAX 12

typedef struct {
    ULONG Pri;
    ULONG Kind;
    ULONG N;
    ULONG Type[FRD_MAX];
    ULONG Size[FRD_MAX];
    UCHAR Data[FRD_MAX][CM_NI_DESC_BYTES];
} FLC;

static struct {
    ULONG Present;
    ULONG Hash;
    ULONG Status;
    ULONG Problem;
    ULONG Hdr[16];
    ULONG Bind;
    ULONG PortIntr;
    ULONG PortId;
    ULONG N;
    FLC Lc[FLC_MAX];
    ULONG FailKind;         /* an operation of this kind answers FailCr  */
    ULONG FailCr;
    ULONG FailAfter;        /* ... after this many of its kind succeed   */
    ULONG StartOnReprocess;
    ULONG Calls[16];
    ULONG Reprocessed;
    ULONG Readies;
    ULONG GoneAtKind;       /* the devnode vanishes at this kind         */
    ULONG StatusAtKind;     /* the status changes at this kind ...       */
    ULONG StatusTo;
    ULONG ProblemTo;
    ULONG GateOffAtKind;    /* the gate closes after this kind           */
    ULONG ClockAtKind;      /* the clock jumps at this kind ...          */
    ULONG ClockBy;          /* ... by this much                          */
} f;

static CM_NI_MEMBER mem;
static CM_NI_CTX ctx;

#define LCH(i)      (0x100UL + (i))
#define RDH(i, k)   (0x10000UL + ((i) << 8) + (k) + 1UL)

static VOID fput32(UCHAR *b, ULONG v)
{
    b[0] = (UCHAR)v;
    b[1] = (UCHAR)(v >> 8);
    b[2] = (UCHAR)(v >> 16);
    b[3] = (UCHAR)(v >> 24);
}

static ULONG addlc(ULONG pri, ULONG kind)
{
    f.Lc[f.N].Pri = pri;
    f.Lc[f.N].Kind = kind;
    f.Lc[f.N].N = 0;
    return f.N++;
}

static VOID addrd(ULONG lc, ULONG type, ULONG size, ULONG tag)
{
    FLC *l = &f.Lc[lc];

    l->Type[l->N] = type;
    l->Size[l->N] = size;
    memset(l->Data[l->N], (int)(tag & 0xFF), CM_NI_DESC_BYTES);
    fput32(l->Data[l->N], tag);
    l->N++;
}

/* The P14s-like NVMe at Code 29: PCI.VXD's configuration (one memory
 * descriptor and its IRQ) at LCPRI_NORMAL. */
static VOID model(VOID)
{
    memset(&f, 0, sizeof(f));
    f.Present = 1;
    f.Hash = HASH;
    f.Status = CM_NI_DN_HAS_PROBLEM;
    f.Problem = 0x1D;
    f.Hdr[0] = 0x174A1C5CUL;
    f.Hdr[2] = 0x01080200UL;
    f.Hdr[3] = 0;
    f.Hdr[4] = 0xE0000004UL;    /* 64-bit memory BAR ...                 */
    f.Hdr[5] = 0x00000001UL;    /* ... its upper half (bit 0 an address) */
    f.Hdr[15] = 0x00000100UL;   /* pin INTA, line 0                      */
    f.Bind = CM_NI_BIND_OK;
    f.PortIntr = 0x0000010AUL;  /* the port: INTA, IRQ 10                */
    f.PortId = 0x00011B36UL;
    addlc(0x3000, CM_NI_BASIC_LOG_CONF);
    addrd(0, CM_NI_RES_MEM, 36, 0xA1);
    addrd(0, CM_NI_RES_IRQ, 8, 0xB1);
    f.StartOnReprocess = 1;
    CmNiInit(&mem, PORT);
    memset(&ctx, 0, sizeof(ctx));
    ctx.Now = 1000;
    ctx.Switch = 1;
    ctx.Stor98 = CM_NI_STOR98_OK;
    ctx.Gate = 1;
}

static ULONG lcIndex(ULONG h)
{
    return h - 0x100UL;
}

/* A configuration the reprocess can allocate: eligible, memory, no IRQ. */
static ULONG allocatable(VOID)
{
    ULONG i, k, irq, memn;

    for (i = 0; i < f.N; i++) {
        if (f.Lc[i].Pri == CM_NI_LCPRI_DISABLED || f.Lc[i].Kind != 0) {
            continue;
        }
        irq = 0;
        memn = 0;
        for (k = 0; k < f.Lc[i].N; k++) {
            if (f.Lc[i].Type[k] == CM_NI_RES_IRQ) {
                irq = 1;
            }
            if (f.Lc[i].Type[k] == CM_NI_RES_MEM) {
                memn = 1;
            }
        }
        if (!irq && memn) {
            return 1;
        }
    }
    return 0;
}

static VOID exec(const CM_NI_OP *op, CM_NI_RES *res)
{
    ULONG i, k, n;

    memset(res, 0, sizeof(*res));
    res->Id = op->Id;
    if (op->Kind < 16) {
        f.Calls[op->Kind]++;
    }
    if (f.ClockAtKind == op->Kind && f.ClockBy != 0) {
        ctx.Now += f.ClockBy;
        f.ClockBy = 0;
    }
    if (f.GoneAtKind == op->Kind) {
        f.Present = 0;
    }
    if (f.StatusAtKind == op->Kind) {
        f.Status = f.StatusTo;
        f.Problem = f.ProblemTo;
        f.StatusAtKind = 0;
    }
    if (f.GateOffAtKind == op->Kind) {
        ctx.Gate = 0;
    }
    if (f.FailKind == op->Kind) {
        if (f.FailAfter == 0) {
            res->Cr = f.FailCr;
            res->Place = CM_NI_PLACE_FAILED;
            return;
        }
        f.FailAfter--;
    }
    switch (op->Kind) {
    case CM_NI_OP_HEADER:
        for (i = 0; i < 16; i++) {
            res->Val[i] = f.Hdr[i];
        }
        break;
    case CM_NI_OP_STATUS:
        if (!f.Present) {
            res->Cr = CM_NI_CR_NO_SUCH_DEVNODE;
            break;
        }
        res->Status = f.Status;
        res->Problem = f.Problem;
        res->Hash = f.Hash;
        break;
    case CM_NI_OP_BINDING:
        res->Bind = f.Bind;
        break;
    case CM_NI_OP_PORT_INTR:
        res->Val[0] = f.PortIntr;
        res->Val[1] = f.PortId;
        break;
    case CM_NI_OP_LC_LIST:
        n = 0;
        for (i = 0; i < f.N; i++) {
            if (f.Lc[i].Kind != op->LcKind) {
                continue;
            }
            if (n == CM_NI_LC_MAX) {
                res->Cut = 1;
                break;
            }
            res->Lc[n] = LCH(i);
            res->Pri[n] = f.Lc[i].Pri;
            n++;
        }
        res->Count = n;
        if (n == 0) {
            res->Cr = CM_NI_CR_NO_MORE_LOG_CONF;
        }
        break;
    case CM_NI_OP_RD_READ:
        if (op->Lc >= 0x10000UL) {
            i = (op->Lc - 0x10000UL) >> 8;
            k = (op->Lc & 0xFFUL);          /* the next one's index */
        } else {
            i = lcIndex(op->Lc);
            k = 0;
        }
        if (i >= f.N || k >= f.Lc[i].N) {
            res->Cr = CM_NI_CR_NO_MORE_RES_DES;
            break;
        }
        res->Rd = RDH(i, k);
        res->Type = f.Lc[i].Type[k];
        res->Size = f.Lc[i].Size[k];
        if (res->Size <= op->Len) {
            memcpy(mem.Desc + op->Off, f.Lc[i].Data[k], res->Size);
        }
        break;
    case CM_NI_OP_ADD_EMPTY:
        if (!f.Present) {
            res->Cr = CM_NI_CR_NO_SUCH_DEVNODE;
            break;
        }
        res->Handle = LCH(addlc(CM_NI_LCPRI_DISABLED, op->LcKind));
        break;
    case CM_NI_OP_PLACE:
        if (!f.Present) {
            res->Place = CM_NI_PLACE_GONE;
            res->Cr = CM_NI_CR_NO_SUCH_DEVNODE;
            break;
        }
        if (f.Hash != op->Hash) {
            res->Place = CM_NI_PLACE_OTHER;
            break;
        }
        i = lcIndex(op->Lc);
        if (i >= f.N || f.Lc[i].Kind != op->LcKind) {
            res->Place = CM_NI_PLACE_MISSING;
            break;
        }
        res->Place = CM_NI_PLACE_AT;
        res->PlacePri = f.Lc[i].Pri;
        break;
    case CM_NI_OP_ADD_RD:
        i = lcIndex(op->Lc);
        addrd(i, CM_NI_RES_MEM, op->Len, 0);
        memcpy(f.Lc[i].Data[f.Lc[i].N - 1], mem.Desc + op->Off, op->Len);
        break;
    case CM_NI_OP_SET_PRI:
        f.Lc[lcIndex(op->Lc)].Pri = op->Pri;
        break;
    case CM_NI_OP_REPROCESS:
        f.Reprocessed++;
        if (f.StartOnReprocess && allocatable()) {
            f.Status = CM_NI_DN_STARTED;
            f.Problem = 0;
        }
        break;
    case CM_NI_OP_READY:
        f.Readies++;
        break;
    case CM_NI_OP_LINE:
        res->Val[0] = f.Hdr[15];
        break;
    default:
        break;
    }
}

static VOID pass(VOID)
{
    CM_NI_OP op;
    CM_NI_RES res;
    ULONG guard;

    CmNiPass(&mem, &ctx);
    guard = 0;
    while (CmNiNext(&mem, &ctx, &op) && guard++ < 1000) {
        exec(&op, &res);
        CmNiTake(&mem, &ctx, &res);
    }
}

static VOID start(VOID)
{
    CmNiOpenWindow(&mem, 1, ctx.Now);
    CmNiSeen(&mem, DN, HASH, FN, 0);
}

static ULONG st(VOID)
{
    return mem.Dev[0].State;
}

static ULONG why(VOID)
{
    return mem.Dev[0].Why;
}

/* ---- vectors ---------------------------------------------------------- */

static VOID test_started(VOID)
{
    ULONG alt;

    model();
    start();
    pass();
    CHECK_EQ(st(), CM_NI_S_ATTEMPTED, "admitted and reprocessed");
    CHECK_EQ(f.N, 2, "one configuration added");
    alt = 1;
    CHECK_EQ(f.Lc[alt].Pri, CM_NI_LCPRI_SUBOPTIMAL, "committed at SUBOPTIMAL");
    CHECK_EQ(f.Lc[alt].N, 1, "one descriptor: the memory alone");
    CHECK_EQ(f.Lc[alt].Type[0], CM_NI_RES_MEM, "a memory descriptor");
    CHECK_EQ(f.Lc[alt].Size[0], 36, "its size as read");
    CHECK(memcmp(f.Lc[alt].Data[0], f.Lc[0].Data[0], 36) == 0,
          "copied byte for byte");
    CHECK_EQ(f.Lc[0].Pri, 0x3000, "the enumerator's untouched");
    CHECK_EQ(f.Lc[0].N, 2, "... and its descriptors");
    CHECK_EQ(f.Reprocessed, 1, "reprocessed");
    CHECK_EQ(f.Calls[CM_NI_OP_PLACE], 2, "the place read before each write");
    CHECK_EQ(f.Calls[CM_NI_OP_STATUS], 3, "rule 2 read, then again twice");
    CHECK_EQ(f.Calls[CM_NI_OP_BINDING], 3, "rule 4 read, then again twice");
    CHECK_EQ(mem.Dev[0].Irq, CM_NI_IRQ_UNROUTABLE, "classified afresh");
    CHECK_EQ(mem.C.Candidates, 1, "a candidate");
    CHECK_EQ(mem.C.Attempts, 1, "one created");
    ctx.Now += 200;
    pass();
    CHECK_EQ(st(), CM_NI_S_DONE, "started");
    CHECK_EQ(mem.C.Started, 1, "counted");
    CHECK(!CmNiBusy(&mem) || mem.WindowOpen, "only the window keeps it busy");
    CHECK_EQ(mem.Jrn[0].Created, 1, "journalled");
    CHECK_EQ(mem.Jrn[0].E[0].Committed, 1, "committed");
    {
        ULONG d[CM_NI_DIAG_WORDS];

        CmNiDiag(&mem, d);
        CHECK(d[0] != 0, "a diagnostic");
        CHECK_EQ(d[1], PORT, "the member");
        CHECK_EQ(d[2], FN, "the function");
        CHECK_EQ(d[3], 0x0001UL, "pin 1 line 0");
        CHECK_EQ(d[4] & 0xFF, CM_NI_IRQ_UNROUTABLE, "the outcome used");
        CHECK_EQ(d[5], 0x1D, "Code 29 before");
        CHECK_EQ(d[7], CM_NI_DN_STARTED, "started after");
        CHECK_EQ(d[9], 1UL | (1UL << 16), "one created, one candidate");
    }
    /* A new generation: read again, started: nothing more done. */
    CmNiOpenWindow(&mem, 2, ctx.Now);
    pass();
    CHECK_EQ(st(), CM_NI_S_EXCLUDED, "judged again");
    CHECK_EQ(why(), CM_NI_W_NOT_CONFIG, "started: not a configuration problem");
    CHECK_EQ(f.N, 2, "nothing added");
}

static VOID test_rules(VOID)
{
    /* Rule 1. */
    model();
    ctx.Switch = 0;
    start();
    pass();
    CHECK_EQ(why(), CM_NI_W_OFF, "the switch 0");
    CHECK_EQ(f.Calls[CM_NI_OP_HEADER], 0, "nothing read");

    /* Rule 3: class 010801h, a type 1 header, an I/O register, a line. */
    model();
    f.Hdr[2] = 0x01080100UL;
    start();
    pass();
    CHECK_EQ(why(), CM_NI_W_HEADER, "another class");
    model();
    f.Hdr[3] = 0x00010000UL;
    start();
    pass();
    CHECK_EQ(why(), CM_NI_W_HEADER, "a type 1 header");
    model();
    f.Hdr[6] = 0x0000E001UL;
    start();
    pass();
    CHECK_EQ(why(), CM_NI_W_HEADER, "an I/O register");
    model();
    f.Hdr[15] = 0x0000010BUL;
    start();
    pass();
    CHECK_EQ(why(), CM_NI_W_HEADER, "a line set");
    model();
    f.Hdr[15] = 0x000001FFUL;
    start();
    pass();
    CHECK(st() == CM_NI_S_ATTEMPTED, "line FFh is unset");
    model();
    f.Hdr[0] = 0x00000001UL;
    start();
    pass();
    CHECK_EQ(st(), CM_NI_S_PENDING, "Retry Status: read again");

    /* Pin 0 (section 5.4): not a candidate, counted with its status. */
    model();
    f.Hdr[15] = 0;
    f.Status = CM_NI_DN_STARTED;
    f.Problem = 0;
    start();
    pass();
    CHECK_EQ(why(), CM_NI_W_NO_PIN, "pin 0: not a candidate");
    CHECK_EQ(mem.C.Pin0Seen, 1, "seen");
    CHECK_EQ(mem.C.Pin0Started, 1, "started");
    CHECK_EQ(f.N, 1, "nothing added");

    /* Rule 2. */
    model();
    f.Status = 0;
    f.Problem = 0;
    start();
    pass();
    CHECK_EQ(st(), CM_NI_S_PENDING, "no problem yet: pending");
    f.Status = CM_NI_DN_HAS_PROBLEM;
    f.Problem = 0x01;
    ctx.Now += 200;
    pass();
    CHECK_EQ(st(), CM_NI_S_PENDING, "not configured: pending");
    f.Problem = 0x1D;
    ctx.Now += 200;
    pass();
    CHECK_EQ(st(), CM_NI_S_ATTEMPTED, "Code 29 within the window: admitted");
    model();
    f.Problem = 0x16;
    start();
    pass();
    CHECK_EQ(why(), CM_NI_W_NOT_CONFIG, "disabled: another problem");
    model();
    f.Problem = 0x0C;
    start();
    pass();
    CHECK_EQ(st(), CM_NI_S_ATTEMPTED, "Code 12 admitted");
    model();
    f.FailKind = CM_NI_OP_STATUS;
    f.FailCr = CM_NI_CR_FAILURE;
    start();
    pass();
    CHECK_EQ(st(), CM_NI_S_PENDING, "a status read failed: pending");

    /* Rule 4. */
    model();
    f.Bind = 0;
    start();
    pass();
    CHECK_EQ(st(), CM_NI_S_PENDING, "no driver yet: pending");
    f.Bind = CM_NI_BIND_DL | CM_NI_BIND_DL_IOS;
    ctx.Now += 200;
    pass();
    CHECK_EQ(st(), CM_NI_S_PENDING, "one value: pending");
    f.Bind = CM_NI_BIND_OK;
    ctx.Now += 200;
    pass();
    CHECK_EQ(st(), CM_NI_S_ATTEMPTED, "no driver, stor98.mpd, Code 29");
    model();
    f.Bind = CM_NI_BIND_DL | CM_NI_BIND_DL_IOS | CM_NI_BIND_PD;
    start();
    pass();
    CHECK_EQ(why(), CM_NI_W_NOT_STOR98, "another PortDriver");
    model();
    ctx.Stor98 = CM_NI_STOR98_NONE;
    start();
    pass();
    CHECK_EQ(why(), CM_NI_W_NOT_STOR98, "no installer record");
    model();
    ctx.Stor98 = CM_NI_STOR98_OLD;
    start();
    pass();
    CHECK_EQ(why(), CM_NI_W_NOT_STOR98, "0.3.x recorded");
    model();
    ctx.Stor98 = CM_NI_STOR98_FAILED;
    start();
    pass();
    CHECK_EQ(st(), CM_NI_S_PENDING, "the record not read: pending");

    /* The window closes on a pending entry. */
    model();
    f.Bind = 0;
    start();
    pass();
    ctx.Now += CM_NOIRQ_WINDOW_MS;
    pass();
    CHECK_EQ(why(), CM_NI_W_NOT_IN_TIME, "not read in time");
    CHECK(!CmNiWindow(&mem), "the window closed");
}

static VOID irqcase(ULONG out, ULONG portNone, ULONG wantState,
                    const char *what)
{
    CM_NI_IRQ irq;

    model();
    memset(&irq, 0, sizeof(irq));
    irq.Valid = 1;
    irq.PortNoLine = portNone;
    irq.Count = 1;
    irq.Bdf[0] = FN;
    irq.Out[0] = out;
    CmNiSetIrq(&mem, 1, &irq);
    start();
    pass();
    CHECK_EQ(st(), wantState, what);
}

static VOID test_interrupt(VOID)
{
    irqcase(CM_NI_IRQ_UNROUTABLE, 0, CM_NI_S_ATTEMPTED, "UNROUTABLE admitted");
    CHECK_EQ(f.Calls[CM_NI_OP_PORT_INTR], 0, "the record used");
    irqcase(0, 1, CM_NI_S_ATTEMPTED, "PORT_NO_LINE admitted");
    irqcase(CM_NI_IRQ_WRITTEN, 0, CM_NI_S_EXCLUDED, "ROUTED_WRITTEN excluded");
    CHECK_EQ(why(), CM_NI_W_INTERRUPT, "interrupt");
    irqcase(CM_NI_IRQ_ALREADY, 0, CM_NI_S_EXCLUDED, "ROUTED_ALREADY excluded");
    irqcase(CM_NI_IRQ_UNKNOWN_HELD, 0, CM_NI_S_EXCLUDED, "a write not held");
    irqcase(CM_NI_IRQ_UNKNOWN_READ, 0, CM_NI_S_ATTEMPTED,
            "a failed read: afresh, admitted");
    CHECK_EQ(f.Calls[CM_NI_OP_PORT_INTR], 1, "the port read");

    /* No record, the swizzle on the port's pin: routable, excluded. */
    model();
    CmNiOpenWindow(&mem, 1, ctx.Now);
    CmNiSeen(&mem, DN, HASH, BDF(8, 0, 0), 0);
    pass();
    CHECK_EQ(why(), CM_NI_W_INTERRUPT, "routable afresh");
    /* ... a bridge at device 3 above it moves it off the port's pin. */
    model();
    CmNiOpenWindow(&mem, 1, ctx.Now);
    CmNiSeen(&mem, DN, HASH, BDF(8, 0, 0), 3);
    pass();
    CHECK_EQ(st(), CM_NI_S_ATTEMPTED, "the swizzle elsewhere: admitted");
    /* ... the port with no line. */
    model();
    f.PortIntr = 0x000001FFUL;
    CmNiOpenWindow(&mem, 1, ctx.Now);
    CmNiSeen(&mem, DN, HASH, BDF(8, 0, 0), 0);
    pass();
    CHECK_EQ(mem.Dev[0].Irq, CM_NI_IRQ_PORT_NO_LINE, "port no line afresh");
    /* ... a failed port read: pending. */
    model();
    f.FailKind = CM_NI_OP_PORT_INTR;
    f.FailCr = CM_NI_CR_FAILURE;
    start();
    pass();
    CHECK_EQ(st(), CM_NI_S_PENDING, "a fresh read failed: pending");
    /* A record of another generation is not used. */
    model();
    {
        CM_NI_IRQ irq;

        memset(&irq, 0, sizeof(irq));
        irq.Valid = 1;
        irq.Count = 1;
        irq.Bdf[0] = FN;
        irq.Out[0] = CM_NI_IRQ_WRITTEN;
        CmNiSetIrq(&mem, 7, &irq);
    }
    start();
    pass();
    CHECK_EQ(st(), CM_NI_S_ATTEMPTED, "another generation's record ignored");
}

static VOID test_gate(VOID)
{
    ULONG lc;

    model();
    f.Lc[0].Pri = CM_NI_LCPRI_SUBOPTIMAL;
    start();
    pass();
    CHECK_EQ(why(), CM_NI_W_LEFTOVER, "one at 0x5000 not the journal's");
    CHECK_EQ(mem.C.LeftOver, 1, "refused.left-over");
    model();
    f.Lc[0].Pri = 0x9000;
    start();
    pass();
    CHECK_EQ(why(), CM_NI_W_CONFIG, "0x9000: not of this form");
    model();
    lc = addlc(0x2000, 0);
    addrd(lc, CM_NI_RES_MEM, 36, 0xC1);
    addrd(lc, CM_NI_RES_IRQ, 8, 0xC2);
    start();
    pass();
    CHECK_EQ(st(), CM_NI_S_ATTEMPTED, "two below 0x5000: passes");
    model();
    lc = addlc(CM_NI_LCPRI_DISABLED, 0);
    addrd(lc, CM_NI_RES_IRQ, 8, 0xC2);
    start();
    pass();
    CHECK_EQ(st(), CM_NI_S_ATTEMPTED, "a disabled one ignored");
    model();
    f.Lc[0].Pri = CM_NI_LCPRI_DISABLED;
    start();
    pass();
    CHECK_EQ(why(), CM_NI_W_CONFIG, "disabled alone: no source");
    CHECK_EQ(f.Calls[CM_NI_OP_RD_READ], 0, "nothing of it read");
    model();
    f.N = 0;
    start();
    pass();
    CHECK_EQ(why(), CM_NI_W_CONFIG, "no basic configuration");
    model();
    f.Lc[0].N = 1;
    start();
    pass();
    CHECK_EQ(why(), CM_NI_W_CONFIG, "no IRQ descriptor");
    model();
    f.Lc[0].N = 0;
    for (lc = 0; lc < 10; lc++) {
        addrd(0, CM_NI_RES_MEM, 36, lc);
    }
    addrd(0, CM_NI_RES_IRQ, 8, 0xB1);
    start();
    pass();
    CHECK_EQ(why(), CM_NI_W_CONFIG, "ten memory descriptors");
    model();
    f.Lc[0].N = 0;
    for (lc = 0; lc < 9; lc++) {
        addrd(0, CM_NI_RES_MEM, 36, lc);
    }
    addrd(0, CM_NI_RES_IRQ, 8, 0xB1);
    start();
    pass();
    CHECK_EQ(st(), CM_NI_S_ATTEMPTED, "nine memory descriptors");
    CHECK_EQ(f.Lc[1].N, 9, "all nine copied");
    model();
    f.Lc[0].Size[0] = CM_NI_DESC_BYTES + 4;
    start();
    pass();
    CHECK_EQ(why(), CM_NI_W_CONFIG, "a descriptor larger than the buffer");
    model();
    addrd(0, 2, 8, 0xD1);
    start();
    pass();
    CHECK_EQ(why(), CM_NI_W_CONFIG, "an I/O descriptor");
    model();
    f.FailKind = CM_NI_OP_LC_LIST;
    f.FailCr = CM_NI_CR_FAILURE;
    start();
    pass();
    CHECK_EQ(st(), CM_NI_S_PENDING, "a list read that failed: pending");
}

/* The transaction stopped at a step, then the next generation. */
static VOID test_leftovers_and_generations(VOID)
{
    /* After step 1 (an Add_Res_Des refused): a disabled leftover. */
    model();
    f.FailKind = CM_NI_OP_ADD_RD;
    f.FailCr = CM_NI_CR_FAILURE;
    start();
    pass();
    CHECK_EQ(st(), CM_NI_S_FAILED, "failed");
    CHECK_EQ(why(), CM_NI_W_STEP, "the step");
    CHECK_EQ(mem.Dev[0].LastStep, CM_NI_OP_ADD_RD, "at Add_Res_Des");
    CHECK_EQ(f.Lc[1].Pri, CM_NI_LCPRI_DISABLED, "left disabled");
    CHECK_EQ(mem.C.Leftovers, 1, "a leftover counted");
    /* A second pass in the same generation: none begun. */
    f.FailKind = 0;
    ctx.Now += 200;
    pass();
    CHECK_EQ(f.N, 2, "none begun in the same generation");
    /* The next generation ignores it and adds its own. */
    CmNiOpenWindow(&mem, 2, ctx.Now);
    pass();
    CHECK_EQ(st(), CM_NI_S_ATTEMPTED, "a new transaction");
    CHECK_EQ(f.N, 3, "a second configuration");
    CHECK_EQ(f.Lc[1].Pri, CM_NI_LCPRI_DISABLED, "the leftover never enabled");
    CHECK_EQ(f.Lc[2].Pri, CM_NI_LCPRI_SUBOPTIMAL, "the new one committed");
    CHECK_EQ(mem.Jrn[0].Created, 2, "two created");

    /* Three created, a fourth wanted: refused. */
    model();
    f.FailKind = CM_NI_OP_ADD_RD;
    f.FailCr = CM_NI_CR_FAILURE;
    start();
    pass();
    CmNiOpenWindow(&mem, 2, ctx.Now);
    pass();
    CmNiOpenWindow(&mem, 3, ctx.Now);
    pass();
    CHECK_EQ(mem.Jrn[0].Created, 3, "three created");
    CmNiOpenWindow(&mem, 4, ctx.Now);
    pass();
    CHECK_EQ(why(), CM_NI_W_ATTEMPTS, "a fourth refused");
    CHECK_EQ(f.N, 4, "no fourth");

    /* Committed and stopped before step 4: the next generation
     * reprocesses it, no second configuration, whatever the count. */
    model();
    f.FailKind = CM_NI_OP_REPROCESS;
    f.FailCr = CM_NI_CR_FAILURE;
    start();
    pass();
    CHECK_EQ(why(), CM_NI_W_STEP, "the reprocess refused");
    CHECK_EQ(f.Lc[1].Pri, CM_NI_LCPRI_SUBOPTIMAL, "committed and kept");
    f.FailKind = 0;
    CmNiOpenWindow(&mem, 2, ctx.Now);
    pass();
    CHECK_EQ(st(), CM_NI_S_ATTEMPTED, "reprocess-only");
    CHECK_EQ(mem.Dev[0].ReprocOnly, 1, "no transaction");
    CHECK_EQ(f.N, 2, "no second configuration");
    CHECK_EQ(f.Reprocessed, 1, "reprocessed once");
    ctx.Now += 200;
    pass();
    CHECK_EQ(st(), CM_NI_S_DONE, "started");

    /* Two abandoned, then a committed one stopped before step 4: the
     * reprocess is still made (no "attempts" refusal). */
    model();
    f.FailKind = CM_NI_OP_ADD_RD;
    f.FailCr = CM_NI_CR_FAILURE;
    start();
    pass();
    CmNiOpenWindow(&mem, 2, ctx.Now);
    pass();
    f.FailKind = CM_NI_OP_REPROCESS;
    CmNiOpenWindow(&mem, 3, ctx.Now);
    pass();
    CHECK_EQ(mem.Jrn[0].Created, 3, "three created, the last committed");
    f.FailKind = 0;
    CmNiOpenWindow(&mem, 4, ctx.Now);
    pass();
    CHECK_EQ(mem.Dev[0].ReprocOnly, 1, "the owed reprocess made");
    CHECK_EQ(f.N, 4, "nothing created");

    /* The committed handle found at another priority: gone, judged as new. */
    model();
    f.FailKind = CM_NI_OP_REPROCESS;
    f.FailCr = CM_NI_CR_FAILURE;
    start();
    pass();
    f.FailKind = 0;
    f.Lc[1].Pri = CM_NI_LCPRI_DISABLED;
    CmNiOpenWindow(&mem, 2, ctx.Now);
    pass();
    CHECK_EQ(mem.C.Gone, 1, "marked gone");
    CHECK_EQ(f.N, 3, "judged as new: another added");
    model();
    f.FailKind = CM_NI_OP_REPROCESS;
    f.FailCr = CM_NI_CR_FAILURE;
    start();
    pass();
    f.FailKind = 0;
    f.Lc[1].Pri = 0x9000;
    CmNiOpenWindow(&mem, 2, ctx.Now);
    pass();
    CHECK_EQ(why(), CM_NI_W_CONFIG, "at 0x9000: treated as any other");
    /* Not found at all (the handle gone from the list). */
    model();
    f.FailKind = CM_NI_OP_REPROCESS;
    f.FailCr = CM_NI_CR_FAILURE;
    start();
    pass();
    f.FailKind = 0;
    f.Lc[1].Kind = CM_NI_FORCED_LOG_CONF;   /* not on the basic list */
    CmNiOpenWindow(&mem, 2, ctx.Now);
    pass();
    CHECK_EQ(mem.C.Gone, 1, "not found: gone");
    CHECK_EQ(st(), CM_NI_S_ATTEMPTED, "judged as new");

    /* A new start (no journal) meets a committed one at 0x5000. */
    model();
    addlc(CM_NI_LCPRI_SUBOPTIMAL, 0);
    addrd(1, CM_NI_RES_MEM, 36, 0xA1);
    start();
    pass();
    CHECK_EQ(why(), CM_NI_W_LEFTOVER, "left over by an earlier start");
    /* ... and a disabled one: ignored, a new transaction. */
    model();
    addlc(CM_NI_LCPRI_DISABLED, 0);
    start();
    pass();
    CHECK_EQ(st(), CM_NI_S_ATTEMPTED, "a disabled leftover ignored");
}

static VOID test_handles_and_admission(VOID)
{
    /* The devnode gone at a place read: forgotten, journal retired. */
    model();
    f.GoneAtKind = CM_NI_OP_PLACE;
    start();
    pass();
    CHECK_EQ(mem.Dev[0].Used, 0, "forgotten");
    CHECK_EQ(mem.Jrn[0].Used, 0, "journal retired");
    CHECK_EQ(f.Calls[CM_NI_OP_ADD_RD], 0, "nothing written");

    /* Re-created with another hash before the write. */
    model();
    start();
    f.Hash = 0x99999999UL;
    f.StatusAtKind = 0;
    {
        /* Only from the first PLACE on: rule 2's STATUS read already
         * answered with the old hash. */
        f.GoneAtKind = 0;
    }
    /* Admission's STATUS sees the new hash first: forgotten at once. */
    pass();
    CHECK_EQ(mem.Dev[0].Used, 0, "another hash: forgotten");

    /* The same hash, its configuration's place changed: FAILED handle. */
    model();
    f.FailKind = CM_NI_OP_PLACE;
    f.FailAfter = 1;
    f.FailCr = CM_NI_CR_SUCCESS;
    start();
    pass();
    CHECK_EQ(why(), CM_NI_W_HANDLE, "the place not as journalled");
    CHECK_EQ(f.Calls[CM_NI_OP_SET_PRI], 0, "never committed");

    /* The device disabled between steps 2 and 3. */
    model();
    f.StatusAtKind = CM_NI_OP_ADD_RD;
    f.StatusTo = CM_NI_DN_HAS_PROBLEM;
    f.ProblemTo = 0x16;
    start();
    pass();
    CHECK_EQ(why(), CM_NI_W_ADMISSION, "admission no longer met");
    CHECK_EQ(f.Lc[1].Pri, CM_NI_LCPRI_DISABLED, "a disabled leftover");
    CHECK_EQ(f.Readies, 0, "never made ready");
    CHECK_EQ(f.Reprocessed, 0, "never reprocessed");

    /* ... between steps 3 and 4: committed, not reprocessed. */
    model();
    f.StatusAtKind = CM_NI_OP_SET_PRI;
    f.StatusTo = CM_NI_DN_HAS_PROBLEM;
    f.ProblemTo = 0x16;
    start();
    pass();
    CHECK_EQ(why(), CM_NI_W_ADMISSION, "admission no longer met");
    CHECK_EQ(f.Lc[1].Pri, CM_NI_LCPRI_SUBOPTIMAL, "committed, left eligible");
    CHECK_EQ(f.Reprocessed, 0, "not reprocessed");

    /* The binding changed before the commit. */
    model();
    start();
    f.StatusAtKind = 0;
    pass();
    model();
    {
        /* Bind fails the second time it is read. */
        f.FailKind = CM_NI_OP_BINDING;
        f.FailAfter = 1;
        f.FailCr = CM_NI_CR_FAILURE;
    }
    start();
    pass();
    CHECK_EQ(why(), CM_NI_W_ADMISSION, "the binding not read again");

    /* A reused handle met with another hash: a new device. */
    model();
    f.FailKind = CM_NI_OP_REPROCESS;
    f.FailCr = CM_NI_CR_FAILURE;
    start();
    pass();
    CmNiSeen(&mem, DN, 0x77777777UL, FN, 0);
    CHECK_EQ(mem.Dev[0].Used + mem.Dev[1].Used, 1, "one entry");
    CHECK(mem.Jrn[0].Used == 0 || mem.Jrn[0].Hash == 0x77777777UL,
          "the old journal dropped");
}

static VOID test_budgets_and_late(VOID)
{
    CM_NI_OP op;
    CM_NI_RES res;

    /* The problem stays: one READY after CM_NI_READY_AFTER_MS, then the
     * deadline. */
    model();
    f.StartOnReprocess = 0;
    start();
    pass();
    CHECK_EQ(st(), CM_NI_S_ATTEMPTED, "attempted");
    ctx.Now += 200;
    pass();
    CHECK_EQ(st(), CM_NI_S_ATTEMPTED, "read again");
    CHECK_EQ(f.Readies, 0, "no READY yet");
    ctx.Now += CM_NI_READY_AFTER_MS;
    pass();
    CHECK_EQ(f.Readies, 1, "the READY fallback");
    ctx.Now += 200;
    pass();
    CHECK_EQ(f.Readies, 1, "once");
    ctx.Now += CM_NOIRQ_TXN_MS;
    pass();
    CHECK_EQ(why(), CM_NI_W_TIMEOUT, "timeout");
    CHECK_EQ(mem.C.Unstarted, 1, "still unstarted");
    /* The window closing does not end an ATTEMPTED entry. */
    model();
    f.StartOnReprocess = 0;
    start();
    pass();
    ctx.Now += CM_NOIRQ_WINDOW_MS;
    pass();
    CHECK_EQ(st(), CM_NI_S_ATTEMPTED, "runs to its own deadline");

    /* A call that returns past the budget (a wizard open): the attempt
     * FAILED "timeout", its result accounted - a late priority change
     * marks the journalled configuration committed. */
    model();
    f.ClockAtKind = CM_NI_OP_SET_PRI;
    f.ClockBy = CM_NOIRQ_TXN_MS + 1;
    start();
    pass();
    CHECK_EQ(why(), CM_NI_W_TIMEOUT, "the budget spent");
    CHECK_EQ(mem.C.Late, 1, "the result accounted");
    CHECK_EQ(mem.Jrn[0].E[0].Committed, 1, "the late commit journalled");
    CHECK_EQ(f.Reprocessed, 0, "not acted on");
    /* ... and reprocessed by the next generation. */
    CmNiOpenWindow(&mem, 2, ctx.Now);
    pass();
    CHECK_EQ(mem.Dev[0].ReprocOnly, 1, "reprocessed next generation");
    CHECK_EQ(f.N, 2, "no second one");

    /* A late Add_Empty_Log_Conf: journalled uncommitted, ignored. */
    model();
    f.ClockAtKind = CM_NI_OP_ADD_EMPTY;
    f.ClockBy = CM_NOIRQ_TXN_MS + 1;
    start();
    pass();
    CHECK_EQ(why(), CM_NI_W_TIMEOUT, "timeout at step 1");
    CHECK_EQ(mem.Jrn[0].Created, 1, "the late handle journalled");
    CHECK_EQ(mem.Jrn[0].E[0].Committed, 0, "uncommitted");
    CHECK_EQ(f.Calls[CM_NI_OP_ADD_RD], 0, "nothing more written");

    /* A result inside the budget after the window closed: the next step
     * submitted. */
    model();
    f.ClockAtKind = CM_NI_OP_ADD_EMPTY;
    f.ClockBy = CM_NOIRQ_WINDOW_MS + 1;
    start();
    pass();
    CHECK_EQ(st(), CM_NI_S_ATTEMPTED, "went on inside its budget");

    /* The gate closes before a submission: FAILED "controller". */
    model();
    f.GateOffAtKind = CM_NI_OP_ADD_EMPTY;
    start();
    pass();
    CHECK_EQ(why(), CM_NI_W_CONTROLLER, "the loss");
    CHECK_EQ(f.Calls[CM_NI_OP_PLACE], 0, "nothing submitted");
    CHECK_EQ(mem.C.Leftovers, 1, "the disabled leftover");
    /* ... the device judged anew once it reopens and a window opens. */
    ctx.Gate = 1;
    f.GateOffAtKind = 0;
    CmNiOpenWindow(&mem, 2, ctx.Now);
    pass();
    CHECK_EQ(st(), CM_NI_S_ATTEMPTED, "judged anew");

    /* A result for no outstanding operation, and one with another id. */
    model();
    start();
    memset(&res, 0, sizeof(res));
    res.Id = 77;
    CmNiTake(&mem, &ctx, &res);
    CHECK_EQ(mem.C.Dropped, 1, "dropped");
    CmNiPass(&mem, &ctx);
    CHECK(CmNiNext(&mem, &ctx, &op), "an operation");
    CHECK(!CmNiNext(&mem, &ctx, &op), "one outstanding at most");
    res.Id = op.Id + 1;
    CmNiTake(&mem, &ctx, &res);
    CHECK_EQ(mem.C.Dropped, 2, "another id dropped");
    CHECK(mem.Out.Id != 0, "still outstanding");

    /* Two devices on one member advance one at a time. */
    model();
    CmNiOpenWindow(&mem, 1, ctx.Now);
    CmNiSeen(&mem, DN, HASH, FN, 0);
    CmNiSeen(&mem, DN + 1, HASH + 1, BDF(9, 1, 0), 0);
    CHECK_EQ(mem.Dev[1].Used, 1, "two entries");
}

/* Record 28's admission window, the walk's departures, the port read. */
static VOID test_window_walk_port(VOID)
{
    ULONG k;

    /* An admission read that returns after the window closed admits
     * nothing. */
    model();
    f.ClockAtKind = CM_NI_OP_LC_LIST;
    f.ClockBy = CM_NOIRQ_WINDOW_MS + 1;
    start();
    pass();
    CHECK_EQ(why(), CM_NI_W_NOT_IN_TIME, "the window closed meanwhile");
    CHECK_EQ(f.Calls[CM_NI_OP_ADD_EMPTY], 0, "nothing created");

    /* A refusal is in the diagnostic. */
    {
        ULONG d[CM_NI_DIAG_WORDS];

        CmNiDiag(&mem, d);
        CHECK(d[0] != 0, "a diagnostic for a refusal");
        CHECK_EQ((d[4] >> 8) & 0xFF, CM_NI_S_EXCLUDED, "its state");
        CHECK_EQ((d[4] >> 16) & 0xFF, CM_NI_W_NOT_IN_TIME, "its reason");
    }

    /* The port not answering, or Retry Status: pending, not "no line". */
    model();
    f.PortId = 0xFFFFFFFFUL;
    f.PortIntr = 0xFFFFFFFFUL;
    start();
    pass();
    CHECK_EQ(st(), CM_NI_S_PENDING, "the port absent: pending");
    model();
    f.PortId = 0x00000001UL;
    start();
    pass();
    CHECK_EQ(st(), CM_NI_S_PENDING, "the port in Retry Status: pending");

    /* A complete walk forgets the entries it did not meet; an incomplete
     * one forgets nothing; an attempt in progress is kept. */
    model();
    CmNiOpenWindow(&mem, 1, ctx.Now);
    CmNiWalkBegin(&mem);
    for (k = 0; k < CM_NOIRQ_DEVICES; k++) {
        CmNiSeen(&mem, 0x100 + k, 0x200 + k, BDF(9, k, 0), 0);
    }
    CmNiWalkDone(&mem, 1);
    CHECK_EQ(mem.Dev[CM_NOIRQ_DEVICES - 1].Used, 1, "eight entries");
    CmNiSeen(&mem, 0x999, 0x999, BDF(9, 9, 0), 0);
    CHECK_EQ(mem.C.NoRoom, 1, "no room for a ninth");
    CmNiWalkBegin(&mem);
    CmNiSeen(&mem, 0x100, 0x200, BDF(9, 0, 0), 0);
    CmNiWalkDone(&mem, 0);
    CHECK_EQ(mem.Dev[3].Used, 1, "an incomplete walk forgets nothing");
    mem.Dev[3].State = CM_NI_S_ATTEMPTED;
    CmNiWalkBegin(&mem);
    CmNiSeen(&mem, 0x100, 0x200, BDF(9, 0, 0), 0);
    CmNiWalkDone(&mem, 1);
    CHECK_EQ(mem.Dev[1].Used, 0, "a departed entry forgotten");
    CHECK_EQ(mem.Dev[0].Used, 1, "the one met kept");
    CHECK_EQ(mem.Dev[3].Used, 1, "an attempt in progress kept");
    CHECK(CmNiInTransaction(&mem), "in a transaction");
    CmNiSeen(&mem, 0x999, 0x999, BDF(9, 9, 0), 0);
    CHECK_EQ(mem.C.NoRoom, 1, "room again");

    /* Full of header refusals: one makes room. */
    model();
    f.Hdr[2] = 0x06040000UL;        /* every entry a bridge */
    CmNiOpenWindow(&mem, 1, ctx.Now);
    for (k = 0; k < CM_NOIRQ_DEVICES; k++) {
        CmNiSeen(&mem, 0x100 + k, 0x200 + k, BDF(9, k, 0), 0);
    }
    pass();
    CHECK_EQ(mem.Dev[0].Why, CM_NI_W_HEADER, "bridges refused");
    CmNiWalkBegin(&mem);
    CmNiSeen(&mem, 0x999, 0x999, BDF(9, 9, 0), 0);
    CHECK_EQ(mem.C.NoRoom, 0, "a header refusal made room");
    CHECK(!CmNiInTransaction(&mem), "no transaction");
}

static VOID test_readings(VOID)
{
    XHCI_TBT_BRNUM r;
    CM_NI_IRQ irq;

    CHECK_EQ(CmNiStor98Version("0.4.0.0", 8), CM_NI_STOR98_OK, "0.4.0.0");
    CHECK_EQ(CmNiStor98Version("0.4.1.0", 8), CM_NI_STOR98_OK, "0.4.1.0");
    CHECK_EQ(CmNiStor98Version("1.0.0.0", 8), CM_NI_STOR98_OK, "1.0.0.0");
    CHECK_EQ(CmNiStor98Version("0.3.0.1", 8), CM_NI_STOR98_OLD, "0.3.0.1");
    CHECK_EQ(CmNiStor98Version("0.10.0.0", 9), CM_NI_STOR98_OK, "0.10");
    CHECK_EQ(CmNiStor98Version("0.4.0", 6), CM_NI_STOR98_OLD, "three parts");
    CHECK_EQ(CmNiStor98Version("0.4.0.0x", 9), CM_NI_STOR98_OLD, "junk");
    CHECK_EQ(CmNiStor98Version("", 1), CM_NI_STOR98_OLD, "empty");
    CHECK_EQ(CmNiStor98Version("0..4.0", 7), CM_NI_STOR98_OLD, "empty part");
    CHECK_EQ(CmNiBindFlags(1, "*IOS", 5, 1, "STOR98.MPD", 11),
             CM_NI_BIND_OK, "case not significant");
    CHECK_EQ(CmNiBindFlags(1, "*IOS", 5, 1, "stor98.mpdx", 12),
             CM_NI_BIND_DL | CM_NI_BIND_DL_IOS | CM_NI_BIND_PD, "a longer name");
    CHECK_EQ(CmNiBindFlags(1, "*NTKERN", 8, 0, NULL, 0), CM_NI_BIND_DL,
             "another loader, no port driver");
    CHECK_EQ(CmNiClassify(1, 0, 0, 0x010A), CM_NI_IRQ_ROUTABLE, "INTA dev 0");
    CHECK_EQ(CmNiClassify(1, 1, 0, 0x010A), CM_NI_IRQ_UNROUTABLE, "dev 1");
    CHECK_EQ(CmNiClassify(2, 3, 0, 0x010A), CM_NI_IRQ_ROUTABLE, "wraps");
    CHECK_EQ(CmNiClassify(2, 3, 3, 0x010A), CM_NI_IRQ_UNROUTABLE, "rot");
    CHECK_EQ(CmNiClassify(1, 0, 0, 0x0100), CM_NI_IRQ_PORT_NO_LINE, "line 0");
    CHECK_EQ(CmNiClassify(1, 0, 0, 0x01FF), CM_NI_IRQ_PORT_NO_LINE, "line FF");
    CHECK_EQ(CmNiClassify(1, 0, 0, 0x000A), CM_NI_IRQ_PORT_NO_LINE, "no pin");

    memset(&r, 0, sizeof(r));
    r.Outcome = XHCI_TBT_BRNUM_DONE;
    r.IrqOutcome = XHCI_TBT_BRIRQ_DONE;
    r.IrqFnCount = 4;
    r.IrqFnBdf[0] = 1;
    r.IrqFnOut[0] = XHCI_TBT_BRFN_WRITTEN;
    r.IrqFnBdf[1] = 2;
    r.IrqFnOut[1] = XHCI_TBT_BRFN_OTHER;
    r.IrqFnBdf[2] = 3;
    r.IrqFnOut[2] = XHCI_TBT_BRFN_NOT_HELD;
    r.IrqFnBdf[3] = 4;
    r.IrqFnOut[3] = XHCI_TBT_BRFN_NOT_READ;
    CmNiIrqFromBrnum(&r, &irq);
    CHECK_EQ(irq.Valid, 1, "a record");
    CHECK_EQ(irq.Count, 4, "four");
    CHECK_EQ(irq.Out[0], CM_NI_IRQ_WRITTEN, "written");
    CHECK_EQ(irq.Out[1], CM_NI_IRQ_UNROUTABLE, "unroutable");
    CHECK_EQ(irq.Out[2], CM_NI_IRQ_UNKNOWN_HELD, "not held");
    CHECK_EQ(irq.Out[3], CM_NI_IRQ_UNKNOWN_READ, "not read");
    r.IrqOutcome = XHCI_TBT_BRIRQ_PORT_NONE;
    CmNiIrqFromBrnum(&r, &irq);
    CHECK(irq.Valid && irq.PortNoLine, "the port with no line");
    r.IrqOutcome = XHCI_TBT_BRIRQ_NONE;
    CmNiIrqFromBrnum(&r, &irq);
    CHECK_EQ(irq.Valid, 0, "the stage not run: no record");
    r.Outcome = XHCI_TBT_BRNUM_NUMBERED;
    r.IrqOutcome = XHCI_TBT_BRIRQ_DONE;
    CmNiIrqFromBrnum(&r, &irq);
    CHECK_EQ(irq.Valid, 0, "nothing numbered: no record");
}

int main(void)
{
    test_started();
    test_rules();
    test_interrupt();
    test_gate();
    test_leftovers_and_generations();
    test_handles_and_admission();
    test_budgets_and_late();
    test_window_walk_port();
    test_readings();
    printf("test_cm_noirq: %d checks, %d failures\n", checks, failures);
    return failures;
}
