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
    ULONG AllAsIrq;         /* the every-type read reports IRQ types     */
    ULONG UseStr;           /* BINDING from Dl and Pd, by the op's kind  */
    const char *Dl;
    const char *Pd;
    ULONG LastFnKind;       /* the kind the last BINDING asked for       */
    ULONG OtherDn;          /* devnodes from here (64 K) answer OtherHdr2 */
    ULONG OtherHdr2;        /* ... as their class dword (a bridge, ...)  */
    ULONG Dn2;              /* a second function of the same header ...  */
    ULONG Hash2;
    ULONG Status2;          /* ... with a status of its own ...          */
    ULONG Problem2;
    ULONG NoBind2;          /* ... and its binding not written yet       */
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
        if (f.OtherDn != 0 && op->Dn >= f.OtherDn &&
            op->Dn < f.OtherDn + 0x10000UL) {
            res->Val[2] = f.OtherHdr2;
            res->Val[3] = 0x00010000UL;     /* a type 1 header */
        }
        break;
    case CM_NI_OP_STATUS:
        if (f.Dn2 != 0 && op->Dn == f.Dn2) {
            res->Status = f.Status2;
            res->Problem = f.Problem2;
            res->Hash = f.Hash2;
            break;
        }
        if (!f.Present) {
            res->Cr = CM_NI_CR_NO_SUCH_DEVNODE;
            break;
        }
        res->Status = f.Status;
        res->Problem = f.Problem;
        res->Hash = f.Hash;
        break;
    case CM_NI_OP_BINDING:
        f.LastFnKind = op->FnKind;
        res->Bind = f.Bind;
        if (f.UseStr) {
            res->Bind = CmNiBindFlagsKind(op->FnKind, f.Dl != NULL, f.Dl,
                                          f.Dl != NULL ? strlen(f.Dl) : 0,
                                          f.Pd != NULL, f.Pd,
                                          f.Pd != NULL ? strlen(f.Pd) : 0);
        }
        if (f.Dn2 != 0 && op->Dn == f.Dn2 && f.NoBind2) {
            res->Bind = 0;
        }
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
        /* As CONFIGMG reads one type: the next descriptor of it. */
        while (i < f.N && k < f.Lc[i].N && op->ResType != 0 &&
               f.Lc[i].Type[k] != op->ResType) {
            k++;
        }
        if (i >= f.N || k >= f.Lc[i].N) {
            res->Cr = CM_NI_CR_NO_MORE_RES_DES;
            break;
        }
        res->Rd = RDH(i, k);
        res->Type = f.Lc[i].Type[k];
        if (op->ResType == 0 && f.AllAsIrq) {
            res->Type = CM_NI_RES_IRQ;
        }
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
        if (f.Dn2 != 0 && op->Dn == f.Dn2) {
            if (f.StartOnReprocess && allocatable()) {
                f.Status2 = CM_NI_DN_STARTED;
                f.Problem2 = 0;
            }
            break;
        }
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

    /* The window closes on a pending entry (its installer record not
     * read: not waiting for its driver, record 28 section 14.3). */
    model();
    ctx.Stor98 = CM_NI_STOR98_FAILED;
    start();
    pass();
    ctx.Now += CM_NOIRQ_WINDOW_MS;
    pass();
    CHECK_EQ(why(), CM_NI_W_NOT_IN_TIME, "not read in time");
    CHECK(!CmNiWindow(&mem), "the window closed");
    /* ... and on one whose header read fails. */
    model();
    f.FailKind = CM_NI_OP_HEADER;
    f.FailCr = CM_NI_CR_FAILURE;
    start();
    pass();
    ctx.Now += CM_NOIRQ_WINDOW_MS;
    pass();
    CHECK_EQ(why(), CM_NI_W_NOT_IN_TIME, "header failing: not read in time");
    CHECK(!CmNiWindow(&mem), "header failing: the window closed");
    /* ... and on one whose binding read fails. */
    model();
    f.FailKind = CM_NI_OP_BINDING;
    f.FailCr = CM_NI_CR_FAILURE;
    start();
    pass();
    CHECK_EQ(mem.Dev[0].WaitDrv, 0, "binding failing: not waiting");
    ctx.Now += CM_NOIRQ_WINDOW_MS;
    pass();
    CHECK_EQ(why(), CM_NI_W_NOT_IN_TIME, "binding failing: not read in time");

    /* Section 14: an entry PENDING for its binding holds the window up to
     * CM_NOIRQ_HOLD_MS from its opening, and no further. */
    model();
    f.Bind = 0;
    start();
    pass();
    CHECK_EQ(mem.Dev[0].WaitDrv, 1, "its binding not written: installing");
    for (ctx.Now = 1000 + 5000; ctx.Now <= 1000 + 30000; ctx.Now += 5000) {
        pass();
    }
    CHECK_EQ(st(), CM_NI_S_PENDING, "held past 10 s, read again each pass");
    CHECK(CmNiWindow(&mem), "the window open");
    CHECK_EQ(mem.C.Held, 1, "the window counted held once");
    for (; ctx.Now <= 1000 + CM_NOIRQ_HOLD_MS - 1; ctx.Now += 5000) {
        pass();
    }
    ctx.Now = 1000 + CM_NOIRQ_HOLD_MS - 1;
    pass();
    CHECK_EQ(st(), CM_NI_S_PENDING, "still held just before the bound");
    ctx.Now = 1000 + CM_NOIRQ_HOLD_MS;
    pass();
    CHECK_EQ(why(), CM_NI_W_NOT_IN_TIME, "the bound: not read in time");
    CHECK(!CmNiWindow(&mem), "closed at the bound, the last hold notwithstanding");
    CHECK_EQ(mem.C.Held, 1, "still one window held");

    /* A pass delayed past 10 s (a call that blocked) with no walk reading:
     * the entry's own last reading holds the window, and once bound it is
     * admitted. */
    model();
    f.Bind = 0;
    start();
    pass();
    ctx.Now += CM_NOIRQ_WINDOW_MS + 5000;
    pass();
    CHECK_EQ(st(), CM_NI_S_PENDING, "a delayed pass: still pending");
    CHECK(CmNiWindow(&mem), "... the window held by its last reading");
    f.Bind = CM_NI_BIND_OK;
    ctx.Now += CM_NOIRQ_WINDOW_MS + 5000;
    pass();
    CHECK_EQ(st(), CM_NI_S_ATTEMPTED, "bound at last: admitted");
}

static VOID memonly(VOID);

/* An xHCI controller on xhci98.sys (record 28 section 14). */
static VOID xhci(VOID)
{
    model();
    f.Lc[0].N = 1;                  /* the memory descriptor alone */
    f.Hdr[0] = 0x11001B73UL;        /* an FL1100                   */
    f.Hdr[2] = 0x0C033010UL;        /* 0C0330h, revision 10h       */
    f.Hdr[15] = 0x000001FFUL;       /* pin INTA, line FFh          */
    f.UseStr = 1;
    f.Dl = "*NTKERN";
    f.Pd = "xhci98.sys";
    ctx.Stor98 = CM_NI_STOR98_NONE; /* no installer record read    */
}

static VOID test_xhci(VOID)
{
    ULONG d[CM_NI_DIAG_WORDS];
    ULONG t0;
    ULONG k;

    /* The memory-only form for an xHCI function, no installer record. */
    xhci();
    start();
    pass();
    CHECK_EQ(st(), CM_NI_S_ATTEMPTED, "xhci: reprocessed");
    CHECK_EQ(mem.Dev[0].Kind, CM_NI_KIND_XHCI, "xhci: its kind");
    CHECK_EQ(f.LastFnKind, CM_NI_KIND_XHCI, "xhci: the binding by kind");
    CHECK_EQ(f.Calls[CM_NI_OP_BINDING], 2, "xhci: rule 4 read, then again");
    CHECK_EQ(f.Calls[CM_NI_OP_ADD_EMPTY], 0, "xhci: nothing added");
    CHECK_EQ(f.Reprocessed, 1, "xhci: one Reprocess_DevNode");
    CHECK_EQ(mem.C.MemOnly, 1, "xhci: the memory-only form");
    ctx.Now += 200;
    pass();
    CHECK_EQ(st(), CM_NI_S_DONE, "xhci: started");
    CHECK_EQ(mem.Dev[0].Line, 0xFF, "xhci: line FFh kept");
    CmNiDiag(&mem, d);
    CHECK_EQ(d[4] >> 24, CM_NI_KIND_XHCI, "xhci: the kind in the diagnostic");
    CHECK_EQ((d[4] >> 8) & 0xFF, CM_NI_S_DONE, "xhci: done in the diagnostic");

    /* An NVMe's diagnostic carries kind 0. */
    memonly();
    start();
    pass();
    CmNiDiag(&mem, d);
    CHECK_EQ(d[4] >> 24, CM_NI_KIND_NVME, "nvme: kind 0");

    /* Not its driver: Stor98's pair on an xHCI function, xhci98.sys's on
     * an NVMe one, another NTMPDriver. */
    xhci();
    f.Dl = "*IOS";
    f.Pd = "stor98.mpd";
    start();
    pass();
    CHECK_EQ(why(), CM_NI_W_NOT_STOR98, "xhci: Stor98's pair refused");
    CHECK_EQ(f.Reprocessed, 0, "xhci: not reprocessed");
    memonly();
    f.UseStr = 1;
    f.Dl = "*NTKERN";
    f.Pd = "xhci98.sys";
    start();
    pass();
    CHECK_EQ(why(), CM_NI_W_NOT_STOR98, "nvme: xhci98.sys's pair refused");
    xhci();
    f.Pd = "other.sys";
    start();
    pass();
    CHECK_EQ(why(), CM_NI_W_NOT_STOR98, "xhci: another NTMPDriver");
    xhci();
    f.Pd = NULL;
    start();
    pass();
    CHECK_EQ(st(), CM_NI_S_PENDING, "xhci: one value alone: pending");

    /* An IRQ descriptor: the added configuration is not taken. */
    xhci();
    f.Lc[0].N = 2;
    start();
    pass();
    CHECK_EQ(why(), CM_NI_W_CONFIG, "xhci: an IRQ descriptor refused");
    CHECK_EQ(f.Calls[CM_NI_OP_ADD_EMPTY], 0, "xhci: nothing created");
    CHECK_EQ(mem.C.Attempts, 0, "xhci: no attempt counted");
    /* Memory only at another configuration problem. */
    xhci();
    f.Problem = 0x0C;
    start();
    pass();
    CHECK_EQ(why(), CM_NI_W_CONFIG, "xhci: memory only at 12 refused");

    /* On the port's pin: line set, header; line FFh, interrupt. */
    xhci();
    f.Hdr[15] = 0x0000010AUL;
    CmNiOpenWindow(&mem, 1, ctx.Now);
    CmNiSeen(&mem, DN, HASH, BDF(8, 0, 0), 0);
    pass();
    CHECK_EQ(why(), CM_NI_W_HEADER, "xhci: routed, line set: header");
    xhci();
    CmNiOpenWindow(&mem, 1, ctx.Now);
    CmNiSeen(&mem, DN, HASH, BDF(8, 0, 0), 0);
    pass();
    CHECK_EQ(why(), CM_NI_W_INTERRUPT, "xhci: on the port's pin: interrupt");
    CHECK_EQ(f.Reprocessed, 0, "xhci: routable, not reprocessed");

    /* Another class (the dock's I210): header. */
    xhci();
    f.Hdr[2] = 0x02000003UL;
    start();
    pass();
    CHECK_EQ(why(), CM_NI_W_HEADER, "020000h: header");

    /* The binding read again before the reprocess, now another driver. */
    xhci();
    start();
    CmNiPass(&mem, &ctx);
    for (k = 0; k < 64; k++) {
        CM_NI_OP op;
        CM_NI_RES res;

        if (!CmNiNext(&mem, &ctx, &op)) {
            break;
        }
        if (op.Kind == CM_NI_OP_BINDING && mem.Dev[0].Step != 3) {
            f.Pd = "other.sys";
        }
        exec(&op, &res);
        CmNiTake(&mem, &ctx, &res);
    }
    CHECK_EQ(why(), CM_NI_W_ADMISSION, "xhci: another driver meanwhile");
    CHECK_EQ(f.Reprocessed, 0, "xhci: not reprocessed meanwhile");

    /* Section 14.3: the window held while the driver is installed; the
     * binding at 40 s, then the admission over further passes. */
    xhci();
    f.Dl = NULL;
    f.Pd = NULL;
    t0 = ctx.Now;
    start();
    pass();
    CHECK_EQ(st(), CM_NI_S_PENDING, "hold: no driver yet");
    CHECK_EQ(mem.Dev[0].WaitDrv, 1, "hold: waiting for its driver");
    for (k = 0; k < 40; k++) {
        ctx.Now += 1000;
        pass();
    }
    CHECK(CmNiWindow(&mem), "hold: the window open at 40 s");
    CHECK_EQ(st(), CM_NI_S_PENDING, "hold: still pending at 40 s");
    f.Dl = "*NTKERN";
    f.Pd = "xhci98.sys";
    f.FailKind = CM_NI_OP_LC_LIST;  /* the admission spans a pass */
    f.FailCr = CM_NI_CR_FAILURE;
    ctx.Now += 1000;
    pass();
    CHECK_EQ(st(), CM_NI_S_PENDING, "hold: bound, a list read failed");
    CHECK_EQ(mem.Dev[0].WaitDrv, 0, "hold: no longer waiting");
    f.FailKind = 0;
    ctx.Now += 5000;
    pass();
    CHECK_EQ(st(), CM_NI_S_ATTEMPTED, "hold: admitted 6 s later");
    CHECK_EQ(f.Reprocessed, 1, "hold: reprocessed");
    /* Past the last waiting pass's 10 s, the window closes. */
    ctx.Now = t0 + 41000 + CM_NOIRQ_WINDOW_MS;
    pass();
    CHECK(!CmNiWindow(&mem), "hold: closed 10 s after the last wait");

    /* The binding late, and the next pass past those 10 s: not in time. */
    xhci();
    f.Dl = NULL;
    f.Pd = NULL;
    start();
    pass();
    ctx.Now += 30000;
    pass();
    f.Dl = "*NTKERN";
    f.Pd = "xhci98.sys";
    f.FailKind = CM_NI_OP_LC_LIST;
    f.FailCr = CM_NI_CR_FAILURE;
    ctx.Now += 1000;
    pass();
    f.FailKind = 0;
    ctx.Now += CM_NOIRQ_WINDOW_MS;
    pass();
    CHECK_EQ(why(), CM_NI_W_NOT_IN_TIME, "hold: admitted too late");

    /* A driver that never comes: held to the bound, then not in time. */
    xhci();
    f.Dl = NULL;
    f.Pd = NULL;
    t0 = ctx.Now;
    start();
    pass();
    while (ctx.Now - t0 < CM_NOIRQ_HOLD_MS - 1000) {
        ctx.Now += 1000;
        pass();
    }
    CHECK_EQ(st(), CM_NI_S_PENDING, "never: pending a second before the bound");
    CHECK(CmNiWindow(&mem), "never: open a second before the bound");
    ctx.Now += 1000;
    pass();
    CHECK_EQ(why(), CM_NI_W_NOT_IN_TIME, "never: not in time at the bound");
    CHECK(!CmNiWindow(&mem), "never: closed at the bound");

    /* An installation problem holds it too (an NVMe drive's wizard). */
    model();
    f.Problem = 0x1C;
    start();
    pass();
    CHECK_EQ(mem.Dev[0].WaitDrv, 1, "problem 28: waiting");
    ctx.Now += CM_NOIRQ_WINDOW_MS + 1000;
    pass();
    CHECK(CmNiWindow(&mem), "problem 28: held past 10 s");
    f.Problem = 0x1D;
    ctx.Now += 1000;
    pass();
    CHECK_EQ(st(), CM_NI_S_ATTEMPTED, "problem 28 then 29: admitted");

    /* The deadline reset by a reopening and by a new generation. */
    xhci();
    f.Dl = NULL;
    f.Pd = NULL;
    start();
    pass();
    ctx.Now += 50000;
    pass();
    CHECK_EQ(mem.HaveHold, 1, "reset: a waiting pass recorded");
    CmNiOpenWindow(&mem, 1, ctx.Now);
    CHECK_EQ(mem.HaveHold, 0, "reset: by a reopening");
    CHECK_EQ(mem.WindowFrom, ctx.Now, "reset: from the reopening");
    ctx.Now += 1000;
    pass();
    CHECK_EQ(mem.HaveHold, 1, "reset: still waiting, recorded again");
    CmNiOpenWindow(&mem, 2, ctx.Now);
    CHECK_EQ(mem.HaveHold, 0, "reset: by a new generation");
    CHECK_EQ(mem.Dev[0].WaitDrv, 0, "reset: the entry pending afresh");

    /* The hold across the clock's wrap. */
    xhci();
    f.Dl = NULL;
    f.Pd = NULL;
    ctx.Now = 0xFFFFC000UL;
    start();
    pass();
    for (k = 0; k < 30; k++) {
        ctx.Now += 1000;
        pass();
    }
    CHECK(CmNiWindow(&mem), "wrap: held across the wrap");
    f.Dl = "*NTKERN";
    f.Pd = "xhci98.sys";
    ctx.Now += 1000;
    pass();
    CHECK_EQ(st(), CM_NI_S_ATTEMPTED, "wrap: admitted after the wrap");
    ctx.Now += CM_NOIRQ_WINDOW_MS;
    pass();
    CHECK(!CmNiWindow(&mem), "wrap: closed 10 s after the last wait");

    /* Section 14.5: ten devnodes below one member, nine of them refused
     * by their headers; the function still reached. */
    {
        ULONG w;
        ULONG hdr;
        ULONG e;

        xhci();
        f.OtherDn = 0x5000UL;
        f.OtherHdr2 = 0x06040002UL;     /* a PCI-to-PCI bridge */
        CmNiOpenWindow(&mem, 1, ctx.Now);
        for (w = 0; w < 4; w++) {
            CmNiWalkBegin(&mem);
            for (k = 0; k < 9; k++) {
                CmNiSeen(&mem, 0x5000UL + k, 0x9000UL + k, BDF(5, k, 0), 0);
            }
            CmNiSeen(&mem, DN, HASH, FN, 0);
            CmNiWalkDone(&mem, 1);
            pass();
            ctx.Now += 200;
        }
        CHECK(mem.C.NoRoom != 0, "room: the entries ran out at first");
        e = CM_NOIRQ_DEVICES;
        for (k = 0; k < CM_NOIRQ_DEVICES; k++) {
            if (mem.Dev[k].Used && mem.Dev[k].Dn == DN) {
                e = k;
            }
        }
        CHECK(e < CM_NOIRQ_DEVICES, "room: the function has an entry");
        CHECK(e < CM_NOIRQ_DEVICES &&
              (mem.Dev[e].State == CM_NI_S_ATTEMPTED ||
               mem.Dev[e].State == CM_NI_S_DONE),
              "room: the function reprocessed");
        CHECK_EQ(f.Reprocessed, 1, "room: one reprocess");
        hdr = f.Calls[CM_NI_OP_HEADER];
        CmNiWalkBegin(&mem);
        for (k = 0; k < 9; k++) {
            CmNiSeen(&mem, 0x5000UL + k, 0x9000UL + k, BDF(5, k, 0), 0);
        }
        CmNiWalkDone(&mem, 1);
        pass();
        CHECK_EQ(f.Calls[CM_NI_OP_HEADER], hdr,
                 "room: a refused devnode not read again");
        /* A reused handle is judged afresh. */
        CmNiWalkBegin(&mem);
        CmNiSeen(&mem, 0x5000UL, 0x7777UL, BDF(5, 0, 0), 0);
        CmNiWalkDone(&mem, 0);
        pass();
        CHECK_EQ(f.Calls[CM_NI_OP_HEADER], hdr + 1,
                 "room: a reused handle read afresh");
        /* A new generation reads every devnode once more. */
        CmNiOpenWindow(&mem, 2, ctx.Now);
        CHECK_EQ(mem.SkipDn[0], 0, "room: the list emptied");
        hdr = f.Calls[CM_NI_OP_HEADER];
        CmNiWalkBegin(&mem);
        CmNiSeen(&mem, 0x5001UL, 0x9001UL, BDF(5, 1, 0), 0);
        CmNiWalkDone(&mem, 0);
        pass();
        CHECK(f.Calls[CM_NI_OP_HEADER] > hdr,
              "room: read again at a new generation");
        /* An incomplete walk keeps a line it did not meet; a complete one
         * takes it off, and the devnode is read afresh when it is back. */
        pass();
        CmNiWalkBegin(&mem);
        CmNiWalkDone(&mem, 0);
        k = 0;
        for (w = 0; w < CM_NOIRQ_SKIPS; w++) {
            if (mem.SkipDn[w] == 0x5001UL) {
                k = 1;
            }
        }
        CHECK_EQ(k, 1, "room: kept after an incomplete walk");
        CmNiWalkBegin(&mem);
        CmNiWalkDone(&mem, 1);
        k = 0;
        for (w = 0; w < CM_NOIRQ_SKIPS; w++) {
            if (mem.SkipDn[w] == 0x5001UL) {
                k = 1;
            }
        }
        CHECK_EQ(k, 0, "room: taken off by a complete walk");
        hdr = f.Calls[CM_NI_OP_HEADER];
        CmNiWalkBegin(&mem);
        CmNiSeen(&mem, 0x5001UL, 0x9001UL, BDF(5, 1, 0), 0);
        CmNiWalkDone(&mem, 0);
        pass();
        CHECK_EQ(f.Calls[CM_NI_OP_HEADER], hdr + 1, "room: back, read afresh");
        /* A full list overwrites its oldest line. */
        for (k = 0; k < CM_NOIRQ_SKIPS + 2; k++) {
            CmNiWalkBegin(&mem);
            CmNiSeen(&mem, 0x6000UL + k, 0xA000UL + k, BDF(5, k & 31, 0), 0);
            CmNiWalkDone(&mem, 0);
            pass();
            pass();
        }
        CHECK(mem.SkipNext < CM_NOIRQ_SKIPS, "room: the next line in range");
        k = 0;
        for (w = 0; w < CM_NOIRQ_SKIPS; w++) {
            if (mem.SkipDn[w] == 0x6000UL + CM_NOIRQ_SKIPS + 1) {
                k = 1;
            }
        }
        CHECK_EQ(k, 1, "room: the newest kept over the oldest");
        /* After a complete walk keeps one line, a refusal takes a free
         * line, not the kept one. */
        CmNiWalkBegin(&mem);
        CmNiSeen(&mem, 0x6000UL + CM_NOIRQ_SKIPS + 1,
                 0xA000UL + CM_NOIRQ_SKIPS + 1, BDF(5, 1, 0), 0);
        CmNiWalkDone(&mem, 1);
        CmNiWalkBegin(&mem);
        CmNiSeen(&mem, 0x7000UL, 0xB000UL, BDF(5, 2, 0), 0);
        CmNiWalkDone(&mem, 0);
        pass();
        pass();
        k = 0;
        for (w = 0; w < CM_NOIRQ_SKIPS; w++) {
            if (mem.SkipDn[w] == 0x6000UL + CM_NOIRQ_SKIPS + 1 ||
                mem.SkipDn[w] == 0x7000UL) {
                k++;
            }
        }
        CHECK_EQ(k, 2, "room: a free line taken after pruning");
    }

    /* An entry with a journal refused by its header keeps its entry. */
    xhci();
    start();
    pass();
    ctx.Now += 200;
    pass();
    CHECK_EQ(st(), CM_NI_S_DONE, "journal: started first");
    f.Hdr[15] = 0x0000010AUL;       /* its line reads set since */
    CmNiOpenWindow(&mem, 2, ctx.Now);
    pass();
    CHECK_EQ(why(), CM_NI_W_HEADER, "journal: refused by its header");
    ctx.Now += 200;
    pass();
    CHECK(mem.Dev[0].Used, "journal: its entry kept");
    CHECK_EQ(mem.SkipDn[0], 0, "journal: not on the refused list");

    /* Section 14.6: the reprocess held 80 s (a wizard open), then read
     * started: DONE, not timeout. */
    xhci();
    f.ClockAtKind = CM_NI_OP_REPROCESS;
    f.ClockBy = 80000UL;
    start();
    pass();
    CHECK_EQ(st(), CM_NI_S_ATTEMPTED, "held: attempted after the hold");
    CHECK_EQ(mem.Dev[0].Credit, 80000UL, "held: 80 s credited");
    CHECK(CmNiWindow(&mem), "held: the window credited too");
    ctx.Now += 200;
    pass();
    CHECK_EQ(st(), CM_NI_S_DONE, "held: started, done");
    /* The window's deadline moved by the hold: still open 9 s after it,
     * closed 10 s after. */
    xhci();
    f.ClockAtKind = CM_NI_OP_REPROCESS;
    f.ClockBy = 80000UL;
    start();
    pass();
    f.ClockAtKind = 0;
    ctx.Now += 9000UL;
    pass();
    CHECK(CmNiWindow(&mem), "held: the window open 9 s after the hold");
    ctx.Now += 1200UL;
    pass();
    CHECK(!CmNiWindow(&mem), "held: closed 10 s after the hold");
    /* The bound: a hold past CM_NOIRQ_HOLD_MS counts as before. */
    xhci();
    f.ClockAtKind = CM_NI_OP_REPROCESS;
    f.ClockBy = CM_NOIRQ_HOLD_MS + CM_NOIRQ_TXN_MS + 1;
    start();
    pass();
    CHECK_EQ(why(), CM_NI_W_TIMEOUT, "held: past the bound, timeout");
    CHECK_EQ(mem.Credit <= CM_NOIRQ_HOLD_MS, 1, "held: window bound");
    /* A call held less than 2 s is not credited. */
    xhci();
    f.ClockAtKind = CM_NI_OP_REPROCESS;
    f.ClockBy = CM_NI_BLOCK_MS;
    start();
    pass();
    CHECK_EQ(mem.Dev[0].Credit, 0, "held: 2 s not credited");
    CHECK_EQ(mem.Credit, 0, "held: nor the window");
    /* The walk held: credited through CmNiCredit; a new window starts with
     * none. */
    xhci();
    f.Bind = 0;
    f.UseStr = 0;
    start();
    pass();
    ctx.Now += 50000UL;
    CmNiCredit(&mem, 50000UL, ctx.Now);
    CHECK_EQ(mem.Credit, 50000UL, "held: a walk's hold credited");
    pass();
    CHECK(CmNiWindow(&mem), "held: the window open after it");
    CmNiOpenWindow(&mem, 2, ctx.Now);
    CHECK_EQ(mem.Credit, 0, "held: a new window has no credit");
    CmNiCredit(&mem, 50000UL, ctx.Now);
    CHECK_EQ(mem.Credit, 0, "held: nothing before the opening");

    /* Each attempt has its own allowance: a second generation's attempt
     * on the same entry is credited again. */
    xhci();
    f.StartOnReprocess = 0;
    f.ClockAtKind = CM_NI_OP_REPROCESS;
    f.ClockBy = 80000UL;
    start();
    pass();
    CHECK_EQ(mem.Dev[0].Credit, 80000UL, "allowance: the first attempt's");
    ctx.Now += CM_NOIRQ_TXN_MS + 1000UL;
    pass();
    CHECK_EQ(why(), CM_NI_W_TIMEOUT, "allowance: the first runs out");
    f.ClockAtKind = CM_NI_OP_REPROCESS;
    f.ClockBy = 80000UL;
    CmNiOpenWindow(&mem, 2, ctx.Now);
    pass();
    CHECK_EQ(st(), CM_NI_S_ATTEMPTED, "allowance: a second attempt");
    CHECK_EQ(mem.Dev[0].Credit, 80000UL, "allowance: credited afresh");
    /* A waiting pass made while a call was outstanding is not moved. */
    xhci();
    start();
    mem.HaveHold = 1;
    mem.HoldAt = mem.WindowFrom + 70000UL;
    CmNiCredit(&mem, 80000UL, mem.WindowFrom + 81000UL);
    CHECK_EQ(mem.HoldAt, mem.WindowFrom,
             "waitat: not before the moved opening");
    ctx.Now = mem.WindowFrom + CM_NOIRQ_WINDOW_MS - 100UL;
    pass();
    CHECK(CmNiWindow(&mem), "waitat: open within 10 s of the opening");
    ctx.Now += 200UL;
    pass();
    CHECK(!CmNiWindow(&mem), "waitat: closed after, not held");
    CHECK_EQ(mem.Credit, 80000UL, "waitat: the window credited");
    /* A call begun before the window's opening: a waiting pass made
     * during it is not moved past the new opening. */
    xhci();
    ctx.Now = 0xFFFF0000UL;
    CmNiOpenWindow(&mem, 1, ctx.Now + 100000UL);
    mem.HaveHold = 1;
    mem.HoldAt = ctx.Now + CM_NOIRQ_HOLD_MS + 90000UL;
    CmNiCredit(&mem, CM_NOIRQ_HOLD_MS + 200000UL,
               ctx.Now + CM_NOIRQ_HOLD_MS + 200000UL);
    CHECK_EQ(mem.Credit, CM_NOIRQ_HOLD_MS, "waitat: credit saturated");
    CHECK_EQ(mem.HoldAt, mem.WindowFrom,
             "waitat: a pre-opening call keeps the wait at the opening");
    ctx.Now = 0;

    /* The binding flags by kind. */
    CHECK_EQ(CmNiBindFlagsKind(CM_NI_KIND_XHCI, 1, "*NTKERN", 8, 1,
                               "XHCI98.SYS", 11), CM_NI_BIND_OK,
             "xhci pair, case not significant");
    CHECK_EQ(CmNiBindFlagsKind(CM_NI_KIND_XHCI, 1, "*IOS", 5, 1,
                               "stor98.mpd", 11),
             CM_NI_BIND_DL | CM_NI_BIND_PD, "Stor98's pair for xhci");
    CHECK_EQ(CmNiBindFlagsKind(CM_NI_KIND_NVME, 1, "*IOS", 5, 1,
                               "stor98.mpd", 11), CM_NI_BIND_OK,
             "Stor98's pair for nvme");
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
    f.Problem = 0x0C;
    start();
    pass();
    CHECK_EQ(why(), CM_NI_W_CONFIG, "no IRQ descriptor, problem 12");
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
    addrd(0, 0x8001, 8, 0xD2);
    start();
    pass();
    CHECK_EQ(why(), CM_NI_W_CONFIG, "an ignored-bit type, read by the last phase");
    model();
    addrd(0, 0x21, 8, 0xD3);
    start();
    pass();
    CHECK_EQ(why(), CM_NI_W_CONFIG, "an OEM type");
    /* An every-type read that does not meet what the typed reads found:
     * the absence of other types unproven, refused. */
    model();
    f.FailKind = CM_NI_OP_RD_READ;
    f.FailAfter = 4;            /* mem, mem end, irq, irq end ... */
    f.FailCr = CM_NI_CR_NO_MORE_RES_DES;
    start();
    pass();
    CHECK_EQ(why(), CM_NI_W_CONFIG, "an empty every-type read refused");
    CHECK_EQ(f.Calls[CM_NI_OP_ADD_EMPTY], 0, "nothing created");
    /* PCI.VXD's configuration as read on 98 SE (2026-10-19): memory, no
     * IRQ descriptor - the memory-only form at problem 29 (test_memonly),
     * refused at the other configuration problems. */
    model();
    f.Lc[0].N = 1;
    f.Problem = 0x10;
    start();
    pass();
    CHECK_EQ(why(), CM_NI_W_CONFIG, "memory only at 16: refused");
    model();
    f.Lc[0].N = 1;
    f.Problem = 0x1B;
    start();
    pass();
    CHECK_EQ(why(), CM_NI_W_CONFIG, "memory only at 27: refused");
    model();
    f.Lc[0].N = 0;
    addrd(0, CM_NI_RES_IRQ, 8, 0xB1);
    start();
    pass();
    CHECK_EQ(why(), CM_NI_W_CONFIG, "an IRQ and no memory: refused");
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

/* The memory-only form (record 28 section 12, task 44.30): PCI.VXD's own
 * configuration holds the memory alone and the problem is 29; nothing is
 * added, the devnode is reprocessed once a generation. */
static VOID memonly(VOID)
{
    model();
    f.Lc[0].N = 1;              /* the memory descriptor alone */
}

static VOID test_memonly(VOID)
{
    ULONG d[CM_NI_DIAG_WORDS];

    memonly();
    start();
    pass();
    CHECK_EQ(st(), CM_NI_S_ATTEMPTED, "memory only: reprocessed");
    CHECK_EQ(f.Calls[CM_NI_OP_ADD_EMPTY], 0, "nothing added");
    CHECK_EQ(f.Calls[CM_NI_OP_ADD_RD], 0, "no descriptor written");
    CHECK_EQ(f.Calls[CM_NI_OP_SET_PRI], 0, "no priority set");
    CHECK_EQ(f.Calls[CM_NI_OP_PLACE], 0, "no handle to place");
    CHECK_EQ(f.N, 1, "the list as PCI.VXD made it");
    CHECK_EQ(f.Lc[0].Pri, 0x3000, "its priority untouched");
    CHECK_EQ(f.Reprocessed, 1, "one Reprocess_DevNode");
    CHECK_EQ(f.Calls[CM_NI_OP_STATUS], 2, "rule 2 read, then again");
    CHECK_EQ(f.Calls[CM_NI_OP_BINDING], 2, "rule 4 read, then again");
    CHECK_EQ(mem.C.Candidates, 1, "a candidate");
    CHECK_EQ(mem.C.Attempts, 0, "no configuration created");
    CHECK_EQ(mem.C.Reprocesses, 1, "a reprocess-only attempt");
    CHECK_EQ(mem.C.MemOnly, 1, "of the memory-only form");
    CHECK_EQ(mem.C.Leftovers, 0, "no leftover");
    ctx.Now += 200;
    pass();
    CHECK_EQ(st(), CM_NI_S_DONE, "started");
    CHECK_EQ(f.Calls[CM_NI_OP_LINE], 1, "the line read once");
    CHECK_EQ(mem.Dev[0].Line, 0, "line 0, as Stor98 reads it");
    CHECK_EQ(mem.C.Started, 1, "counted");
    CHECK_EQ(f.Readies, 0, "no READY");
    CmNiDiag(&mem, d);
    CHECK_EQ(d[5], 0x1D, "Code 29 before");
    CHECK_EQ(d[7], CM_NI_DN_STARTED, "started after");
    CHECK_EQ(d[9], 0UL | (1UL << 16), "none created, one candidate");
    CHECK_EQ(mem.Jrn[0].Created, 0, "the journal holds no configuration");
    CHECK_EQ(mem.Jrn[0].ReprocGen, 2, "... its reprocess mark");

    /* Unstarted after the reprocess: the one READY after
     * CM_NI_READY_AFTER_MS, then the deadline; nothing added. */
    memonly();
    f.StartOnReprocess = 0;
    start();
    pass();
    ctx.Now += CM_NI_READY_AFTER_MS + 1;
    pass();
    CHECK_EQ(f.Readies, 1, "the READY fallback");
    ctx.Now += CM_NOIRQ_TXN_MS;
    pass();
    CHECK_EQ(why(), CM_NI_W_TIMEOUT, "timeout");
    CHECK_EQ(f.Calls[CM_NI_OP_ADD_EMPTY], 0, "still nothing added");
    CHECK_EQ(mem.C.Leftovers, 0, "no leftover");
    /* The same generation again: not reprocessed twice. */
    CmNiOpenWindow(&mem, 1, ctx.Now);
    CmNiSeen(&mem, DN, HASH, FN, 0);
    pass();
    CHECK_EQ(st(), CM_NI_S_FAILED, "FAILED stands for the generation");
    CHECK_EQ(f.Reprocessed, 1, "one reprocess");
    /* A new generation: read again and reprocessed once more. */
    CmNiOpenWindow(&mem, 2, ctx.Now);
    pass();
    CHECK_EQ(st(), CM_NI_S_ATTEMPTED, "a new generation: once more");
    CHECK_EQ(f.Reprocessed, 2, "a second reprocess");
    CHECK_EQ(mem.C.Attempts, 0, "the attempts bound untouched");

    /* The reprocess mark of this generation already taken (a devnode met
     * again as PENDING within it): EXCLUDED, "attempts". */
    memonly();
    f.StartOnReprocess = 0;
    start();
    pass();
    mem.Dev[0].State = CM_NI_S_PENDING;
    mem.Dev[0].Step = 1;
    pass();
    CHECK_EQ(why(), CM_NI_W_ATTEMPTS, "one reprocess a generation");
    CHECK_EQ(f.Reprocessed, 1, "not reprocessed again");

    /* The devnode disabled by the user before the reprocess (problem 22):
     * FAILED "admission", never reprocessed. */
    memonly();
    f.StatusAtKind = CM_NI_OP_RD_READ;
    f.StatusTo = CM_NI_DN_HAS_PROBLEM;
    f.ProblemTo = 0x16;
    start();
    pass();
    CHECK_EQ(why(), CM_NI_W_ADMISSION, "disabled meanwhile");
    CHECK_EQ(f.Reprocessed, 0, "not reprocessed");
    /* Its driver changed before the reprocess: the same. */
    memonly();
    start();
    {
        ULONG k;

        /* Admitted, then the binding read again answers another driver. */
        CmNiPass(&mem, &ctx);
        for (k = 0; k < 64; k++) {
            CM_NI_OP op;
            CM_NI_RES res;

            if (!CmNiNext(&mem, &ctx, &op)) {
                break;
            }
            if (op.Kind == CM_NI_OP_BINDING && mem.Dev[0].Step != 3) {
                f.Bind = CM_NI_BIND_DL | CM_NI_BIND_PD;
            }
            exec(&op, &res);
            CmNiTake(&mem, &ctx, &res);
        }
    }
    CHECK_EQ(why(), CM_NI_W_ADMISSION, "another driver meanwhile");
    CHECK_EQ(f.Reprocessed, 0, "not reprocessed");

    /* The window closed before the form is reached: not in time. */
    memonly();
    f.ClockAtKind = CM_NI_OP_RD_READ;
    f.ClockBy = CM_NOIRQ_HOLD_MS + CM_NOIRQ_WINDOW_MS + 1;
    start();
    pass();
    CHECK_EQ(why(), CM_NI_W_NOT_IN_TIME, "not read in time");
    CHECK_EQ(f.Reprocessed, 0, "not reprocessed");

    /* Two memory descriptors, no IRQ: the same form. */
    memonly();
    addrd(0, CM_NI_RES_MEM, 36, 0xA2);
    start();
    pass();
    CHECK_EQ(st(), CM_NI_S_ATTEMPTED, "two memory descriptors");
    CHECK_EQ(f.Calls[CM_NI_OP_ADD_EMPTY], 0, "nothing added");

    /* The switch 0: nothing. */
    memonly();
    ctx.Switch = 0;
    start();
    pass();
    CHECK_EQ(why(), CM_NI_W_OFF, "the switch 0");
    CHECK_EQ(f.Reprocessed, 0, "not reprocessed");

    /* Three configurations created earlier, none committed: the creation
     * bound does not stop the form, which creates none. */
    memonly();
    start();
    mem.Jrn[0].Used = 1;
    mem.Jrn[0].Dn = DN;
    mem.Jrn[0].Hash = HASH;
    mem.Jrn[0].Created = CM_NOIRQ_ATTEMPTS;
    mem.Jrn[0].E[0].Lc = 0x900;
    mem.Jrn[0].E[1].Lc = 0x901;
    mem.Jrn[0].E[1].Gone = 1;
    mem.Jrn[0].E[2].Lc = 0x902;
    pass();
    CHECK_EQ(st(), CM_NI_S_ATTEMPTED, "the bound full: still reprocessed");
    CHECK_EQ(f.Calls[CM_NI_OP_ADD_EMPTY], 0, "nothing created");
    /* ... and the basic form with the bound full: refused, after its
     * descriptors were read. */
    model();
    start();
    mem.Jrn[0].Used = 1;
    mem.Jrn[0].Dn = DN;
    mem.Jrn[0].Hash = HASH;
    mem.Jrn[0].Created = CM_NOIRQ_ATTEMPTS;
    pass();
    CHECK_EQ(why(), CM_NI_W_ATTEMPTS, "the basic form: the bound");
    CHECK_EQ(f.Calls[CM_NI_OP_ADD_EMPTY], 0, "nothing created");

    /* The typed reads find the memory alone, the every-type read an IRQ
     * in its place: contradictory, refused. */
    memonly();
    f.AllAsIrq = 1;
    start();
    pass();
    CHECK_EQ(why(), CM_NI_W_CONFIG, "an IRQ met only by the every-type read");
    CHECK_EQ(f.Reprocessed, 0, "not reprocessed");

    /* Not Stor98's record: nothing. */
    memonly();
    ctx.Stor98 = CM_NI_STOR98_NONE;
    start();
    pass();
    CHECK_EQ(why(), CM_NI_W_NOT_STOR98, "no installer record");
    CHECK_EQ(f.Reprocessed, 0, "not reprocessed");
}

static VOID test_budgets_and_late(VOID)
{
    CM_NI_OP op;
    CM_NI_OP op2;
    CM_NI_RES res;
    ULONG credited;
    ULONG late;

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
    /* A READY refused: FAILED at that step, its code kept. */
    model();
    f.StartOnReprocess = 0;
    f.FailKind = CM_NI_OP_READY;
    f.FailCr = CM_NI_CR_FAILURE;
    start();
    pass();
    ctx.Now += CM_NI_READY_AFTER_MS + 1;
    pass();
    CHECK_EQ(why(), CM_NI_W_STEP, "READY refused: failed");
    CHECK_EQ(mem.Dev[0].LastStep, CM_NI_OP_READY, "at READY");
    CHECK_EQ(mem.Dev[0].LastCr, CM_NI_CR_FAILURE, "its code");
    /* A line read refused after the start: DONE, the line not taken. */
    model();
    f.FailKind = CM_NI_OP_LINE;
    f.FailCr = CM_NI_CR_FAILURE;
    f.Hdr[15] = 0x000001FFUL;
    start();
    pass();
    ctx.Now += 200;
    pass();
    CHECK_EQ(st(), CM_NI_S_DONE, "started");
    CHECK_EQ(mem.Dev[0].LastStep, CM_NI_OP_LINE, "the line step");
    CHECK_EQ(mem.Dev[0].LastCr, CM_NI_CR_FAILURE, "not read");
    CHECK_EQ(mem.Dev[0].Line, 0xFF, "the header's line kept");
    /* A status read that blocks past the deadline and returns "gone":
     * the disappearance first - forgotten, not timed out. */
    model();
    f.StartOnReprocess = 0;
    start();
    pass();
    f.ClockAtKind = CM_NI_OP_STATUS;
    f.ClockBy = CM_NOIRQ_TXN_MS + 1;
    f.GoneAtKind = CM_NI_OP_STATUS;
    pass();
    CHECK_EQ(mem.Dev[0].Used, 0, "gone after the deadline: forgotten");
    CHECK_EQ(mem.C.Timeouts, 0, "not a timeout");
    /* READY answering "no such devnode" with the gate closed: forgotten. */
    model();
    f.StartOnReprocess = 0;
    f.FailKind = CM_NI_OP_READY;
    f.FailCr = CM_NI_CR_NO_SUCH_DEVNODE;
    f.GateOffAtKind = CM_NI_OP_READY;
    start();
    pass();
    ctx.Now += CM_NI_READY_AFTER_MS + 1;
    pass();
    CHECK_EQ(mem.Dev[0].Used, 0, "gone at READY: forgotten");
    CHECK_EQ(mem.Jrn[0].Used, 0, "journal retired");
    ctx.Gate = 1;
    /* The gate closing during the line read: FAILED "controller". */
    model();
    f.GateOffAtKind = CM_NI_OP_LINE;
    start();
    pass();
    ctx.Now += 200;
    pass();
    CHECK_EQ(why(), CM_NI_W_CONTROLLER, "the loss during the last read");
    CHECK_EQ(mem.C.Started, 0, "not counted started");
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
    f.ClockBy = CM_NOIRQ_HOLD_MS + CM_NOIRQ_TXN_MS + 1;
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
    f.ClockBy = CM_NOIRQ_HOLD_MS + CM_NOIRQ_TXN_MS + 1;
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
    /* Record 21 section 6.1.3: the operation a suspend held at its
     * admission (not made, not taken) stays outstanding through the wait,
     * is handed out again with its id, and its result is taken - nothing
     * dropped, the wait not credited by the take. */
    ctx.Now += 30000UL;
    CHECK(!CmNiNext(&mem, &ctx, &op2), "held: nothing new while it waits");
    CHECK(CmNiAgain(&mem, &ctx, &op2), "held: handed out again");
    CHECK_EQ(op2.Id, op.Id, "held: the same id");
    CHECK_EQ(op2.Kind, op.Kind, "held: the same kind");
    CHECK_EQ(op2.Dev, op.Dev, "held: the same entry");
    credited = mem.C.Credited;
    memset(&res, 0, sizeof(res));
    res.Id = op2.Id;
    CmNiTake(&mem, &ctx, &res);
    CHECK_EQ(mem.C.Dropped, 2, "held: its result taken, not dropped");
    CHECK_EQ(mem.Out.Id, 0, "held: none outstanding after");
    CHECK_EQ(mem.C.Credited, credited, "held: the 30 s wait not credited by the take");
    CHECK(!CmNiAgain(&mem, &ctx, &op2), "held: nothing to hand out again");

    /* ... an attempt's operation held across the wait: handed out again
     * while its deadline stands; past it (the parked time's credit spent),
     * taken as not made - the attempt failed by timeout, nothing handed
     * out, so the caller makes nothing after the attempt stopped. */
    model();
    f.StartOnReprocess = 0;
    start();
    pass();
    CHECK_EQ(st(), CM_NI_S_ATTEMPTED, "held attempt: attempted");
    ctx.Now += 200;
    CmNiPass(&mem, &ctx);
    CHECK(CmNiNext(&mem, &ctx, &op), "held attempt: an operation of the attempt");
    ctx.Now = mem.Dev[0].Deadline - 1;
    CHECK(CmNiAgain(&mem, &ctx, &op2), "held attempt: before its deadline, handed out again");
    CHECK_EQ(op2.Id, op.Id, "held attempt: the same id");
    ctx.Now = mem.Dev[0].Deadline + 1;
    late = mem.C.Late;
    CHECK(!CmNiAgain(&mem, &ctx, &op2), "held attempt: past its deadline, not handed out");
    CHECK_EQ(mem.Out.Id, 0, "held attempt: taken");
    CHECK_EQ(st(), CM_NI_S_FAILED, "held attempt: failed");
    CHECK_EQ(why(), CM_NI_W_TIMEOUT, "held attempt: by timeout");
    CHECK_EQ(mem.C.Late, late + 1, "held attempt: its result accounted as late");

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
    f.ClockBy = CM_NOIRQ_HOLD_MS + CM_NOIRQ_WINDOW_MS + 1;
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
    r.IrqPin = 1;
    r.IrqLine = 0xFF;
    CmNiIrqFromBrnum(&r, &irq);
    CHECK(irq.Valid && irq.PortNoLine, "the port with no line");
    r.IrqPin = 0xFF;
    CmNiIrqFromBrnum(&r, &irq);
    CHECK_EQ(irq.Valid, 0, "a port read all ones: no record");
    r.IrqOutcome = XHCI_TBT_BRIRQ_NONE;
    CmNiIrqFromBrnum(&r, &irq);
    CHECK_EQ(irq.Valid, 0, "the stage not run: no record");
    r.Outcome = XHCI_TBT_BRNUM_NUMBERED;
    r.IrqOutcome = XHCI_TBT_BRIRQ_DONE;
    CmNiIrqFromBrnum(&r, &irq);
    CHECK_EQ(irq.Valid, 0, "nothing numbered: no record");

    /* Design record 28 section 13: no bridge below the port, the line
     * stage run on the port's own bus. */
    r.Outcome = XHCI_TBT_BRNUM_NONE;
    r.IrqOutcome = XHCI_TBT_BRIRQ_DONE;
    r.IrqFnCount = 1;
    r.IrqFnBdf[0] = 5;
    r.IrqFnOut[0] = XHCI_TBT_BRFN_WRITTEN;
    CmNiIrqFromBrnum(&r, &irq);
    CHECK(irq.Valid && irq.Count == 1 && irq.Out[0] == CM_NI_IRQ_WRITTEN,
          "NONE with the stage: the record handed on");
    r.IrqOutcome = XHCI_TBT_BRIRQ_ACCESS;
    CmNiIrqFromBrnum(&r, &irq);
    CHECK_EQ(irq.Valid, 1, "NONE with a read failed: a record");
    r.IrqOutcome = XHCI_TBT_BRIRQ_PORT_NONE;
    r.IrqPin = 1;
    r.IrqLine = 0;
    CmNiIrqFromBrnum(&r, &irq);
    CHECK(irq.Valid && irq.PortNoLine, "NONE, the port with no line");
    r.IrqOutcome = XHCI_TBT_BRIRQ_STALE;
    CmNiIrqFromBrnum(&r, &irq);
    CHECK_EQ(irq.Valid, 0, "NONE, stale: no record");
    r.IrqOutcome = XHCI_TBT_BRIRQ_NONE;
    CmNiIrqFromBrnum(&r, &irq);
    CHECK_EQ(irq.Valid, 0, "NONE with no stage: no record, as before");
}

/* Record 28 section 14: the window held while a descendant is being
 * installed. */
static VOID test_hold(VOID)
{
    ULONG t0;

    CHECK(CmNiInstalling(CM_NI_DN_HAS_PROBLEM, 0x01), "problem 1 installing");
    CHECK(CmNiInstalling(CM_NI_DN_HAS_PROBLEM, 0x08), "problem 8 installing");
    CHECK(CmNiInstalling(CM_NI_DN_HAS_PROBLEM, 0x12), "problem 18 installing");
    CHECK(CmNiInstalling(CM_NI_DN_HAS_PROBLEM, 0x1C), "problem 28 installing");
    CHECK(!CmNiInstalling(CM_NI_DN_HAS_PROBLEM, 0x1D), "problem 29 is not");
    CHECK(!CmNiInstalling(CM_NI_DN_HAS_PROBLEM, 0), "no problem number: not");
    CHECK(!CmNiInstalling(0, 0x1C), "28 without DN_HAS_PROBLEM: not");
    CHECK(!CmNiInstalling(CM_NI_DN_STARTED, 0), "started: not");
    CHECK(CmNiWalkInstalling(0, CM_NI_DN_HAS_PROBLEM | CM_NI_DN_STARTED, 1,
                             0),
          "ME's new bridge during its wizard (problem 1, started)");
    CHECK(CmNiWalkInstalling(0, 0, 0, CM_NI_CR_NO_SUCH_VALUE),
          "no problem yet, no Driver value: installing");
    CHECK(!CmNiWalkInstalling(0, CM_NI_DN_STARTED, 0, 0),
          "started, its Driver value read: not");
    CHECK(!CmNiWalkInstalling(0x13, 0, 0, 0x13),
          "both reads failed: nothing said");
    CHECK(!CmNiWalkInstalling(0, CM_NI_DN_HAS_PROBLEM, 0x1D, 0x1A),
          "problem 29, a Driver value too long for the room: not");

    /* The walk meets a bridge being installed: the window held, no entry
     * of its own needed. */
    model();
    CmNiOpenWindow(&mem, 1, ctx.Now);
    t0 = ctx.Now;
    ctx.Now = t0 + 9000;
    ctx.WalkHold = CM_NI_HOLD_BRIDGE | 0x01FFUL;
    pass();
    ctx.WalkHold = 0;
    ctx.Now = t0 + 15000;
    pass();
    CHECK(CmNiWindow(&mem), "held at 9 s: open at 15 s");
    ctx.Now = t0 + 9000 + CM_NOIRQ_WINDOW_MS - 1;
    pass();
    CHECK(CmNiWindow(&mem), "open until 10 s after the last hold");
    ctx.Now = t0 + 9000 + CM_NOIRQ_WINDOW_MS;
    pass();
    CHECK(!CmNiWindow(&mem), "closed 10 s after the last hold");
    CHECK_EQ(mem.C.Held, 1, "held once");

    /* A hold with the window closed does nothing. */
    ctx.WalkHold = CM_NI_HOLD_BRIDGE | 0x01FFUL;
    ctx.Now += 1000;
    pass();
    CHECK(!CmNiWindow(&mem), "a closed window is not reopened by a hold");
    CHECK_EQ(mem.C.Held, 1, "not counted");

    /* The function appears late (its parent bridge installed meanwhile):
     * seen in the held window, admitted and started. */
    model();
    CmNiOpenWindow(&mem, 1, ctx.Now);
    t0 = ctx.Now;
    ctx.WalkHold = CM_NI_HOLD_BRIDGE | 0x01FFUL;
    for (ctx.Now = t0; ctx.Now < t0 + 120000; ctx.Now += 5000) {
        pass();
    }
    CHECK(CmNiWindow(&mem), "held for two minutes of an install");
    ctx.WalkHold = 0;
    CmNiSeen(&mem, DN, HASH, FN, 0);
    pass();
    CHECK_EQ(st(), CM_NI_S_ATTEMPTED, "the late function admitted");
    ctx.Now += 200;
    pass();
    CHECK_EQ(st(), CM_NI_S_DONE, "and started");

    /* Each opening has its own bound. */
    model();
    CmNiOpenWindow(&mem, 1, ctx.Now);
    t0 = ctx.Now;
    ctx.WalkHold = CM_NI_HOLD_BRIDGE | 0x01FFUL;
    ctx.Now = t0 + CM_NOIRQ_HOLD_MS - 1;
    pass();
    CHECK(CmNiWindow(&mem), "open just before the bound");
    ctx.Now = t0 + CM_NOIRQ_HOLD_MS;
    pass();
    CHECK(!CmNiWindow(&mem), "closed at the bound");
    CmNiOpenWindow(&mem, 2, ctx.Now);
    ctx.Now += 30000;
    pass();
    CHECK(CmNiWindow(&mem), "a new opening: held again");
    CHECK_EQ(mem.C.Held, 2, "two windows held");
    ctx.WalkHold = 0;

    /* Problem 29 with Stor98 bound and no installation reading holds
     * nothing: admitted at once as before (test_started). An entry whose
     * rule 2 read an installation problem holds it. */
    model();
    f.Problem = 0x1C;
    start();
    pass();
    CHECK_EQ(st(), CM_NI_S_PENDING, "problem 28: pending");
    CHECK_EQ(mem.Dev[0].WaitDrv, 1, "... for an installation");
    t0 = ctx.Now;
    for (ctx.Now = t0 + 5000; ctx.Now <= t0 + 15000; ctx.Now += 5000) {
        pass();
    }
    CHECK_EQ(st(), CM_NI_S_PENDING, "held past 10 s");
    f.Problem = 0x1D;
    ctx.Now += 200;
    pass();
    CHECK_EQ(st(), CM_NI_S_ATTEMPTED, "its driver bound, problem 29: admitted");
}

/* The entry of devnode `dn`, or CM_NOIRQ_DEVICES. */
static ULONG entryOf(ULONG dn)
{
    ULONG k;

    for (k = 0; k < CM_NOIRQ_DEVICES; k++) {
        if (mem.Dev[k].Used && mem.Dev[k].Dn == dn) {
            return k;
        }
    }
    return CM_NOIRQ_DEVICES;
}

/* Record 28 section 14: the cases of the two readings together. */
static VOID test_combined(VOID)
{
    ULONG t0;
    ULONG k;
    ULONG e;
    ULONG e2;
    ULONG what;
    ULONG value;
    ULONG lines;

    /* Only a bridge's installation is the walk's to read (section 14.3
     * item 1). */
    CHECK(CmNiIsBridge(0x06040002UL), "060400h: a bridge");
    CHECK(CmNiIsBridge(0x06040102UL), "060401h (subtractive): a bridge");
    CHECK(!CmNiIsBridge(0x02000003UL), "020000h (the I210): not");
    CHECK(!CmNiIsBridge(0x0C033010UL), "0C0330h: not");
    CHECK(!CmNiIsBridge(0x06090000UL), "060900h: not");
    CHECK(!CmNiIsBridge(0xFFFFFFFFUL), "a read of all ones: not");

    /* A chained NVMe behind two bridges being installed: the walk holds
     * while the bridges' wizard runs (no function devnode yet), the
     * function appears after a minute and waits for Stor98's binding (its
     * entry holds), then is admitted and started; one hold logged, by the
     * bridge. */
    memonly();
    f.Bind = 0;
    t0 = ctx.Now;
    CmNiOpenWindow(&mem, 1, t0);
    ctx.WalkHold = CM_NI_HOLD_BRIDGE | (2UL << 8) | 0x1CUL;
    for (ctx.Now = t0; ctx.Now < t0 + 60000; ctx.Now += 5000) {
        pass();
    }
    CHECK(CmNiWindow(&mem), "chain: held through the bridges' wizard");
    ctx.WalkHold = 0;
    CmNiSeen(&mem, DN, HASH, FN, 0);
    pass();
    CHECK_EQ(st(), CM_NI_S_PENDING, "chain: the NVMe met, not bound yet");
    CHECK_EQ(mem.Dev[0].WaitDrv, 1, "chain: waiting for Stor98");
    for (k = 0; k < 6; k++) {
        ctx.Now += 5000;
        pass();
    }
    CHECK(CmNiWindow(&mem), "chain: held by the entry past the walk's 10 s");
    CHECK_EQ(st(), CM_NI_S_PENDING, "chain: still pending");
    f.Bind = CM_NI_BIND_OK;
    ctx.Now += 5000;
    pass();
    CHECK_EQ(st(), CM_NI_S_ATTEMPTED, "chain: bound, admitted");
    ctx.Now += 200;
    pass();
    CHECK_EQ(st(), CM_NI_S_DONE, "chain: started");
    CHECK_EQ(mem.C.Held, 1, "chain: one window held");
    lines = 0;
    while (CmNiLogNext(&mem, &what, &value)) {
        lines++;
        CHECK_EQ(what, CM_NI_LOG_HOLD, "chain: the hold's line");
        CHECK_EQ(value, CM_NI_HOLD_BRIDGE | 0x021CUL,
                 "chain: by a bridge, depth 2, problem 28");
    }
    CHECK_EQ(lines, 1, "chain: one line");
    ctx.Now += CM_NOIRQ_WINDOW_MS;
    pass();
    CHECK(!CmNiWindow(&mem), "chain: closed 10 s after the last hold");

    /* The same with the NVMe's devnode never appearing: the bridges' hold
     * alone, to the bound. */
    memonly();
    t0 = ctx.Now;
    CmNiOpenWindow(&mem, 1, t0);
    ctx.WalkHold = CM_NI_HOLD_BRIDGE | (1UL << 8) | CM_NI_HOLD_NO_DRIVER;
    for (ctx.Now = t0; ctx.Now < t0 + CM_NOIRQ_HOLD_MS; ctx.Now += 10000) {
        pass();
    }
    CHECK(CmNiWindow(&mem), "chain: open just before the bound");
    ctx.Now = t0 + CM_NOIRQ_HOLD_MS;
    pass();
    CHECK(!CmNiWindow(&mem), "chain: closed at the bound");
    ctx.WalkHold = 0;

    /* Two xHCI controllers of one member (the TS3 Plus's second FL1100 and
     * its ASM1142): the first bound and attempted while the second still
     * waits for xhci98.inf's binding and holds the window; both started.
     * The hold logged once, by the entry. */
    xhci();
    f.Dn2 = DN + 1;
    f.Hash2 = HASH + 1;
    f.Status2 = CM_NI_DN_HAS_PROBLEM;
    f.Problem2 = 0x1D;
    f.NoBind2 = 1;
    t0 = ctx.Now;
    CmNiOpenWindow(&mem, 1, t0);
    CmNiSeen(&mem, DN, HASH, FN, 0);
    CmNiSeen(&mem, f.Dn2, f.Hash2, BDF(10, 2, 0), 0);
    pass();
    e = entryOf(DN);
    e2 = entryOf(f.Dn2);
    CHECK(e < CM_NOIRQ_DEVICES && e2 < CM_NOIRQ_DEVICES, "two: two entries");
    CHECK_EQ(mem.Dev[e].State, CM_NI_S_ATTEMPTED, "two: the first attempted");
    CHECK_EQ(mem.Dev[e2].State, CM_NI_S_PENDING, "two: the second pending");
    CHECK_EQ(mem.Dev[e2].WaitDrv, 1, "two: ... waiting for its driver");
    for (k = 0; k < 8; k++) {
        ctx.Now += 5000;
        pass();
    }
    CHECK_EQ(mem.Dev[e].State, CM_NI_S_DONE, "two: the first started");
    CHECK(CmNiWindow(&mem), "two: held by the second at 40 s");
    CHECK_EQ(mem.Dev[e2].State, CM_NI_S_PENDING, "two: the second waits");
    f.NoBind2 = 0;
    ctx.Now += 5000;
    pass();
    CHECK_EQ(mem.Dev[e2].State, CM_NI_S_ATTEMPTED,
             "two: the second bound, admitted");
    ctx.Now += 200;
    pass();
    CHECK_EQ(mem.Dev[e2].State, CM_NI_S_DONE, "two: the second started");
    CHECK_EQ(f.Reprocessed, 2, "two: two reprocesses");
    CHECK_EQ(mem.C.MemOnly, 2, "two: both the memory-only form");
    CHECK(CmNiLogNext(&mem, &what, &value), "two: a line owed");
    CHECK_EQ(what, CM_NI_LOG_HOLD, "two: the hold's line");
    CHECK_EQ(value, CM_NI_HOLD_ENTRY | BDF(10, 2, 0),
             "two: by the entry, at its location");
    CHECK(!CmNiLogNext(&mem, &what, &value), "two: one line");

    /* The second's wait crossed by a held call (the I210's wizard): the
     * call's time credited, the window still held afterwards, and the
     * credit's line. */
    xhci();
    f.Dn2 = DN + 1;
    f.Hash2 = HASH + 1;
    f.Status2 = CM_NI_DN_HAS_PROBLEM;
    f.Problem2 = 0x1D;
    f.NoBind2 = 1;
    t0 = ctx.Now;
    CmNiOpenWindow(&mem, 1, t0);
    CmNiSeen(&mem, f.Dn2, f.Hash2, BDF(10, 2, 0), 0);
    pass();
    CHECK_EQ(mem.Dev[0].WaitDrv, 1, "credit: waiting");
    while (CmNiLogNext(&mem, &what, &value)) {
    }
    ctx.Now += 80000UL;
    CmNiCredit(&mem, 80000UL, ctx.Now);
    CHECK_EQ(mem.C.Credited, 1, "credit: counted");
    CHECK(CmNiLogNext(&mem, &what, &value) && what == CM_NI_LOG_CREDIT &&
          value == 80000UL, "credit: its line, the held ms");
    CHECK_EQ(mem.Credit, 80000UL, "credit: the window credited");
    pass();
    CHECK(CmNiWindow(&mem), "credit: open after the held call");
    /* The bound is counted on the window's own time: the opening moved by
     * the credit. */
    while (ctx.Now - t0 < 80000UL + CM_NOIRQ_HOLD_MS - 10000UL) {
        ctx.Now += 10000;
        pass();
    }
    CHECK(CmNiWindow(&mem), "credit: open at the bound plus the credit, less");
    ctx.Now = t0 + 80000UL + CM_NOIRQ_HOLD_MS;
    pass();
    CHECK(!CmNiWindow(&mem), "credit: closed at the bound plus the credit");
    CHECK_EQ(mem.Dev[0].Why, CM_NI_W_NOT_IN_TIME, "credit: not in time");

    /* The worker's other work blocked 80 s between two ticks (the
     * marking, held by the I210's wizard): handed over as a credit, the
     * first controller's attempt survives and the second, met only after,
     * is admitted inside the window. */
    xhci();
    f.StartOnReprocess = 0;
    f.Dn2 = DN + 1;
    f.Hash2 = HASH + 1;
    f.Status2 = CM_NI_DN_HAS_PROBLEM;
    f.Problem2 = 0x1D;
    t0 = ctx.Now;
    CmNiOpenWindow(&mem, 1, t0);
    CmNiSeen(&mem, DN, HASH, FN, 0);
    pass();
    e = entryOf(DN);
    CHECK_EQ(mem.Dev[e].State, CM_NI_S_ATTEMPTED, "gap: the first attempted");
    ctx.Now += 80000UL;
    CmNiCredit(&mem, 80000UL - 200UL, ctx.Now);
    CmNiSeen(&mem, f.Dn2, f.Hash2, BDF(10, 2, 0), 0);
    f.StartOnReprocess = 1;
    pass();
    e2 = entryOf(f.Dn2);
    CHECK(mem.Dev[e].State == CM_NI_S_ATTEMPTED ||
          mem.Dev[e].State == CM_NI_S_DONE, "gap: the first not timed out");
    CHECK(e2 < CM_NOIRQ_DEVICES &&
          mem.Dev[e2].State == CM_NI_S_ATTEMPTED, "gap: the second admitted");

    /* A member whose only unfinished devnode is a function of another
     * class with no driver (the I210): refused by its header, the walk
     * holds nothing for it, and the window closes at 10 s. */
    xhci();
    f.OtherDn = 0x5000UL;
    f.OtherHdr2 = 0x02000003UL;
    t0 = ctx.Now;
    CmNiOpenWindow(&mem, 1, t0);
    CmNiWalkBegin(&mem);
    CmNiSeen(&mem, 0x5000UL, 0x9000UL, BDF(11, 3, 0), 0);
    CmNiWalkDone(&mem, 1);
    pass();
    CHECK_EQ(mem.C.Refused[CM_NI_W_HEADER], 1, "i210: refused by its header");
    ctx.Now = t0 + CM_NOIRQ_WINDOW_MS;
    pass();
    CHECK(!CmNiWindow(&mem), "i210: closed at 10 s");
    CHECK_EQ(mem.C.Held, 0, "i210: never held");
}

/* Record 28 section 14.3 item 1, 98 SE's one-at-a-time enumeration
 * (runs/run-48.md, "48.36, the dock's first plug on 98 SE"): a device
 * answering on a walked secondary bus with no devnode placed holds; and
 * section 14.4's close lines. */
static VOID test_unenumerated(VOID)
{
    ULONG met[4];
    ULONG what;
    ULONG value;
    ULONG t0;

    /* The dock's switch: five downstream ports answer on bus 5. */
    met[0] = BDF(5, 0, 0);
    CHECK(CmNiUnenumerated(5, 0x1FUL, met, 1, 4),
          "bus 5: ports 1 to 4 answer with no devnode");
    met[1] = BDF(5, 1, 0);
    met[2] = BDF(5, 2, 0);
    met[3] = BDF(5, 3, 0);
    CHECK(CmNiUnenumerated(5, 0x1FUL, met, 4, 4),
          "bus 5: port 4 still without one");
    CHECK(!CmNiUnenumerated(5, 0x0FUL, met, 4, 4),
          "bus 5: every answering device placed");
    met[3] = BDF(5, 3, 2);
    CHECK(!CmNiUnenumerated(5, 0x0FUL, met, 4, 4),
          "another function of the device counts for it");
    met[3] = BDF(6, 3, 0);
    CHECK(CmNiUnenumerated(5, 0x0FUL, met, 4, 4),
          "the same device on another bus does not");
    CHECK(!CmNiUnenumerated(5, 0, met, 4, 4), "nothing answers: no hold");
    CHECK(!CmNiUnenumerated(0, 0x1FUL, met, 1, 4), "bus 0: not read");
    CHECK(!CmNiUnenumerated(5, 0x1FUL, met, 5, 4),
          "the walk's locations not kept whole: no hold");
    CHECK(CmNiUnenumerated(5, 0x80000000UL, met, 0, 4),
          "device 31, nothing met");
    CHECK(CmNiAwaited(0x06040000UL) && CmNiAwaited(0x06040100UL),
          "a bridge is waited for");
    CHECK(CmNiAwaited(0x01080200UL) && CmNiAwaited(0x0C033001UL),
          "an NVMe drive and an xHCI controller are waited for");
    CHECK(!CmNiAwaited(0x02000003UL), "a network function is not (the I210)");
    CHECK(!CmNiAwaited(0x0C032000UL), "nor an EHCI controller");
    CHECK(!CmNiAwaited(0xFFFFFFFFUL), "nor a read that failed");

    /* The first plug as read: 04:00 being installed for 60 s, then its
     * first child alone placed and the others not yet enumerated for 90
     * s (the FL1100's wizard), held throughout; then nothing holds and
     * the window closes 10 s after the last hold, its lines owed. */
    model();
    CmNiOpenWindow(&mem, 1, ctx.Now);
    t0 = ctx.Now;
    ctx.WalkHold = CM_NI_HOLD_BRIDGE | 0x0101UL;
    for (ctx.Now = t0; ctx.Now <= t0 + 60000UL; ctx.Now += 5000) {
        pass();
    }
    ctx.WalkHold = CM_NI_HOLD_BRIDGE | CM_NI_HOLD_NO_CHILD;
    for (; ctx.Now <= t0 + 150000UL; ctx.Now += 5000) {
        pass();
    }
    CHECK(CmNiWindow(&mem), "plug: held past two minutes");
    while (CmNiLogNext(&mem, &what, &value)) {
    }
    ctx.WalkHold = 0;
    ctx.Now = t0 + 150000UL + CM_NOIRQ_WINDOW_MS - 1;
    pass();
    CHECK(CmNiWindow(&mem), "plug: open until 10 s after the last hold");
    ctx.Now = t0 + 150000UL + CM_NOIRQ_WINDOW_MS;
    pass();
    CHECK(!CmNiWindow(&mem), "plug: closed 10 s after the last hold");
    CHECK(CmNiLogNext(&mem, &what, &value) && what == CM_NI_LOG_CLOSE,
          "plug: the close's line");
    CHECK_EQ(value, (CM_NI_CLOSE_AFTER_HOLD << 24) | (160UL << 12) | 10UL,
             "plug: after a hold, 160 s from the opening, 10 from the hold");
    CHECK(CmNiLogNext(&mem, &what, &value) && what == CM_NI_LOG_CLOSE_HOLD,
          "plug: the last hold's line");
    CHECK_EQ(value, CM_NI_HOLD_BRIDGE | CM_NI_HOLD_NO_CHILD,
             "plug: the last hold an unenumerated device");
    CHECK(!CmNiLogNext(&mem, &what, &value), "plug: two lines");

    /* Never held: closed at 10 s, the hold line 0. */
    model();
    CmNiOpenWindow(&mem, 1, ctx.Now);
    t0 = ctx.Now;
    ctx.Now = t0 + CM_NOIRQ_WINDOW_MS;
    pass();
    CHECK(!CmNiWindow(&mem), "unheld: closed at 10 s");
    CHECK(CmNiLogNext(&mem, &what, &value) && what == CM_NI_LOG_CLOSE &&
          value == ((CM_NI_CLOSE_UNHELD << 24) | (10UL << 12) | 10UL),
          "unheld: its close line");
    CHECK(CmNiLogNext(&mem, &what, &value) &&
          what == CM_NI_LOG_CLOSE_HOLD && value == 0,
          "unheld: no hold");

    /* Held to the bound: the bound's reason, the seconds held at FFFh. */
    model();
    CmNiOpenWindow(&mem, 1, ctx.Now);
    t0 = ctx.Now;
    ctx.WalkHold = CM_NI_HOLD_BRIDGE | 0x02FFUL;
    for (ctx.Now = t0; ctx.Now < t0 + CM_NOIRQ_HOLD_MS; ctx.Now += 5000UL) {
        pass();
    }
    while (CmNiLogNext(&mem, &what, &value)) {
    }
    ctx.Now = t0 + CM_NOIRQ_HOLD_MS;
    pass();
    ctx.WalkHold = 0;
    CHECK(!CmNiWindow(&mem), "bound: closed");
    CHECK(CmNiLogNext(&mem, &what, &value) && what == CM_NI_LOG_CLOSE &&
          (value >> 24) == CM_NI_CLOSE_BOUND &&
          ((value >> 12) & 0xFFFUL) == 0x708UL,
          "bound: its reason, 1800 s from the opening");
    CHECK(CmNiLogNext(&mem, &what, &value) &&
          what == CM_NI_LOG_CLOSE_HOLD &&
          value == (CM_NI_HOLD_BRIDGE | 0x02FFUL),
          "bound: the last hold a bridge with no Driver value");
}

int main(void)
{
    test_started();
    test_rules();
    test_interrupt();
    test_gate();
    test_memonly();
    test_leftovers_and_generations();
    test_handles_and_admission();
    test_budgets_and_late();
    test_window_walk_port();
    test_readings();
    test_hold();
    test_xhci();
    test_combined();
    test_unenumerated();
    printf("test_cm_noirq: %d checks, %d failures\n", checks, failures);
    return failures;
}
