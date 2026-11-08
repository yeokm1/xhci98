/*
 * test_pfw.c - host vectors for XHCIPFW.VXD's pure half (src\pfw\pfw_scan.c;
 * roadmap-usb4.md task 44.49, design record 32 section 10.1) over a model
 * of PCI configuration space: each classification of section 4.1 step 2,
 * the close of step 3 with every state checked against its invariant by a
 * check of this file's own (not the code's PfwSafe), each write in turn
 * ignored and its undo, a write reading back a third value and an undo
 * ignored, the scan's bounds, the BAR count over a subtree's bus range,
 * the xHCI controllers with a BAR above 4 GB (section 4.4), the log
 * records and their sizes, the tables' limits, and the emulator's
 * 4G-off layout as the VxD read it; and section 12.10's interrupt-line
 * pass (task 46.11): the switch, the NHI by id or class, its own port by
 * topology, every function left that the pass must leave, the byte
 * write, and the id list against xhcicm.inf.
 */

#include <stdio.h>
#include <string.h>
#include "../src/pfw/pfw_scan.h"
#include "../src/pfw/pfw_ntdec.h"
#include "test_harness.h"

#define MAXFN 96

typedef struct FN {
    PFW_U32 Bus;
    PFW_U32 DevFn;
    PFW_U32 Cfg[64];
    int Writes;         /* writes this function took, in order        */
    int IgnoreAt;       /* that write is ignored (-1: none)           */
    int ThirdAt;        /* that write stores a third value (-1: none) */
    int FlipAfter;      /* 24h changes after so many reads of it (0)  */
    int Reads24;
    int Track;          /* every state after a write checked          */
} FN;

static FN fns[MAXFN];
static int nfn;
static int writes;      /* every write, any function                  */
static int stray;       /* writes to a function the model lacks       */
static int unsafe;      /* tracked states neither closed nor above    */

static FN *find(PFW_U32 bus, PFW_U32 devfn)
{
    int i;

    for (i = 0; i < nfn; i++) {
        if (fns[i].Bus == bus && fns[i].DevFn == devfn) {
            return &fns[i];
        }
    }
    return NULL;
}

/* The invariant of record 32 section 4.1 step 3, written apart from the
 * code's: the upper base not 0 (the window at or above 4 GB), or the
 * upper limit 0 and the low base above the low limit (closed). */
static int state_ok(const FN *f)
{
    PFW_U32 r24 = f->Cfg[0x24 / 4];
    PFW_U32 baselo = (r24 & 0xFFF0UL) << 16;
    PFW_U32 limlo = (r24 & 0xFFF00000UL) | 0xFFFFFUL;

    if (f->Cfg[0x28 / 4] != 0) {
        return 1;
    }
    return f->Cfg[0x2C / 4] == 0 && baselo > limlo;
}

PFW_U32 PfwCfgRead(PFW_U32 bus, PFW_U32 devfn, PFW_U32 off)
{
    FN *f = find(bus, devfn);

    if (f == NULL) {
        return 0xFFFFFFFFUL;
    }
    if (off == 0x24 && f->FlipAfter != 0 && ++f->Reads24 == f->FlipAfter) {
        f->Cfg[0x24 / 4] ^= 0x00100000UL;
    }
    return f->Cfg[off / 4];
}

void PfwCfgWrite(PFW_U32 bus, PFW_U32 devfn, PFW_U32 off, PFW_U32 val)
{
    FN *f = find(bus, devfn);
    int idx;

    writes++;
    if (f == NULL) {
        stray++;
        return;
    }
    idx = f->Writes++;
    if (idx == f->IgnoreAt) {
        return;
    }
    if (idx == f->ThirdAt) {
        val ^= 0x00700070UL;
    }
    if (off == 0x24) {
        /* The type nibbles are read-only: 1h, a 64-bit window. */
        f->Cfg[0x24 / 4] = (val & 0xFFF0FFF0UL) | 0x00010001UL;
    } else {
        f->Cfg[off / 4] = val;
    }
    if (f->Track && !state_ok(f)) {
        unsafe++;
    }
}

static void reset(void)
{
    memset(fns, 0, sizeof(fns));
    nfn = 0;
    writes = 0;
    stray = 0;
    unsafe = 0;
}

static FN *add(PFW_U32 bus, PFW_U32 dev, PFW_U32 fn, PFW_U32 hdr,
               PFW_U32 cls)
{
    FN *f = &fns[nfn++];

    f->Bus = bus;
    f->DevFn = (dev << 3) | fn;
    f->Cfg[0] = 0x24211B21UL;
    f->Cfg[0x08 / 4] = cls << 8;
    f->Cfg[0x0C / 4] = hdr << 16;
    f->IgnoreAt = -1;
    f->ThirdAt = -1;
    return f;
}

static FN *bridge(PFW_U32 bus, PFW_U32 dev, PFW_U32 fn, PFW_U32 sec,
                  PFW_U32 sub, PFW_U32 r24, PFW_U32 r28, PFW_U32 r2c)
{
    FN *f = add(bus, dev, fn, 1, 0x060400UL);

    f->Cfg[0x18 / 4] = bus | (sec << 8) | (sub << 16);
    f->Cfg[0x24 / 4] = r24;
    f->Cfg[0x28 / 4] = r28;
    f->Cfg[0x2C / 4] = r2c;
    return f;
}

static void bar64(FN *f, PFW_U32 off, PFW_U32 hi, PFW_U32 lo)
{
    f->Cfg[off / 4] = lo | 0x0CUL;      /* 64-bit, prefetchable */
    f->Cfg[off / 4 + 1] = hi;
}

static void run(PFW_RESULT *r)
{
    memset(r, 0, sizeof(*r));
    PfwRun(r);
}

static int is_closed_form(const FN *f)
{
    return f->Cfg[0x24 / 4] == 0x0001FFF1UL && f->Cfg[0x28 / 4] == 0 &&
           f->Cfg[0x2C / 4] == 0;
}

static void test_classify(void)
{
    CHECK_EQ(PfwClassify(0x00000000UL, 5, 5), PFW_CLASS_LEAVE,
             "a 32-bit window is left, whatever 28h and 2Ch read");
    CHECK_EQ(PfwClassify(0xFFF10001UL, 0, 0), PFW_CLASS_LEAVE,
             "a 64-bit window with both upper halves 0 is left");
    CHECK_EQ(PfwClassify(0x0001FFF1UL, 0, 0), PFW_CLASS_LEAVE,
             "the closed form is left");
    CHECK_EQ(PfwClassify(0xFFF10001UL, 1, 0x40), PFW_CLASS_ABOVE,
             "an upper base not 0: above 4 GB");
    CHECK_EQ(PfwClassify(0x0001FFF1UL, 1, 0), PFW_CLASS_ABOVE,
             "closed in 64 bits but not in a 32-bit view: above");
    CHECK_EQ(PfwClassify(0xFFF1E001UL, 0, 1), PFW_CLASS_ACROSS,
             "upper base 0, upper limit not 0: across 4 GB");
    CHECK_EQ(PfwSafe(0x0001FFF1UL, 0, 0), 1, "the closed form is safe");
    CHECK_EQ(PfwSafe(0xFFF10001UL, 0, 0), 0, "an open window below 4 GB is not");
    CHECK_EQ(PfwSafe(0x0001FFF1UL, 0, 1), 0, "closed low, upper limit 1: open");
    CHECK_EQ(PfwSafe(0xFFF10001UL, 7, 0), 1, "an upper base not 0 is safe");
}

/* Section 10.1's upper pairs, each under low halves open, closed and
 * equal. */
static const PFW_U32 uppers[5][2] = {
    { 1UL, 0UL }, { 1UL, 1UL }, { 1UL, 0x40UL },
    { 0xFFFFFFFFUL, 0xFFFFFFFFUL }, { 0xB8UL, 0xF7UL }
};
static const PFW_U32 lows[3] = { 0xFFF10001UL, 0x0001FFF1UL, 0x00110011UL };

static void test_close(void)
{
    PFW_U32 after[3];
    int u;
    int l;
    int k;
    int act;
    FN *f;
    char what[96];

    for (u = 0; u < 5; u++) {
        for (l = 0; l < 3; l++) {
            reset();
            f = bridge(0, 3, 0, 1, 1, lows[l], uppers[u][0], uppers[u][1]);
            f->Track = 1;
            act = PfwClose(0, 3 << 3, after);
            sprintf(what, "upper %lx/%lx low %08lx: closed", uppers[u][0],
                    uppers[u][1], lows[l]);
            CHECK(act == PFW_ACT_CLOSED && is_closed_form(f), what);
            CHECK_EQ(f->Writes, 4, "four writes");
            CHECK_EQ(unsafe, 0, "every state closed or above 4 GB");
            CHECK(after[0] == 0x0001FFF1UL && after[1] == 0 && after[2] == 0,
                  "after[] as read at the end");

            /* Each write in turn ignored: refused, every state safe, the
             * registers back as they were. */
            for (k = 0; k < 4; k++) {
                reset();
                f = bridge(0, 3, 0, 1, 1, lows[l], uppers[u][0], uppers[u][1]);
                f->Track = 1;
                f->IgnoreAt = k;
                act = PfwClose(0, 3 << 3, after);
                sprintf(what, "upper %lx/%lx low %08lx write %d ignored",
                        uppers[u][0], uppers[u][1], lows[l], k);
                /* A write that writes the value already there is taken
                 * whether ignored or not (28h = FFFFFFFFh over FFFFFFFFh,
                 * 2Ch = 0 over 0, 24h over the closed form). */
                if (act == PFW_ACT_CLOSED) {
                    CHECK(is_closed_form(f), what);
                } else {
                    CHECK(act == PFW_ACT_REFUSED &&
                          f->Cfg[0x24 / 4] == lows[l] &&
                          f->Cfg[0x28 / 4] == uppers[u][0] &&
                          f->Cfg[0x2C / 4] == uppers[u][1], what);
                }
                CHECK_EQ(unsafe, 0, "every state of the undo safe too");
            }
        }
    }

    /* A write that never takes the value: 2Ch stays 40h. */
    reset();
    f = bridge(0, 3, 0, 1, 1, 0xFFF10001UL, 1, 0x40);
    f->Track = 1;
    f->IgnoreAt = 1;
    CHECK_EQ(PfwClose(0, 3 << 3, after), PFW_ACT_REFUSED, "2Ch ignored: refused");
    CHECK(f->Cfg[0x28 / 4] == 1 && f->Cfg[0x2C / 4] == 0x40 &&
          f->Cfg[0x24 / 4] == 0xFFF10001UL, "2Ch ignored: the bridge as read");
    CHECK_EQ(f->Writes, 4, "2Ch ignored: the write, its undo, 28h's undo");
    CHECK_EQ(unsafe, 0, "2Ch ignored: every state safe");

    /* A third value: refused (its undo taken) or parked (ignored);
     * nothing claimed about the states. */
    for (k = 0; k < 4; k++) {
        reset();
        f = bridge(0, 3, 0, 1, 1, 0xFFF10001UL, 1, 0x40);
        f->ThirdAt = k;
        act = PfwClose(0, 3 << 3, after);
        sprintf(what, "write %d reads back a third value: refused", k);
        CHECK(act == PFW_ACT_REFUSED && f->Cfg[0x24 / 4] == 0xFFF10001UL &&
              f->Cfg[0x28 / 4] == 1 && f->Cfg[0x2C / 4] == 0x40, what);
        CHECK(after[0] == f->Cfg[0x24 / 4] && after[1] == f->Cfg[0x28 / 4] &&
              after[2] == f->Cfg[0x2C / 4], "after[] the registers at the end");

        reset();
        f = bridge(0, 3, 0, 1, 1, 0xFFF10001UL, 1, 0x40);
        f->ThirdAt = k;
        f->IgnoreAt = k + 1;    /* the first undo write */
        act = PfwClose(0, 3 << 3, after);
        sprintf(what, "write %d a third value, its undo ignored: parked", k);
        CHECK_EQ(act, PFW_ACT_PARKED, what);
        CHECK_EQ(f->Writes, k + 2, "parked: no write after the undo that failed");
    }
}

static void test_scan(void)
{
    PFW_RESULT r;
    FN *f;
    FN *g;
    FN *h;
    FN *c;
    FN *z;

    /* Function 1 of a device whose function 0 is absent: not reached. */
    reset();
    f = bridge(0, 4, 1, 1, 1, 0xFFF10001UL, 1, 0x40);
    /* A multi-function device (bit 7) whose function 3 is a bridge. */
    add(0, 5, 0, 0x80, 0x0C0300UL);
    g = bridge(0, 5, 3, 2, 2, 0xFFF10001UL, 1, 0x40);
    /* Function 3 of a device without bit 7: not read. */
    add(0, 6, 0, 0x00, 0x020000UL);
    h = bridge(0, 6, 3, 3, 3, 0xFFF10001UL, 1, 0x40);
    /* A bridge whose header type has bit 7 set (function 0). */
    z = bridge(0, 7, 0, 0, 0, 0xFFF10001UL, 2, 0x40);
    z->Cfg[0x0C / 4] = 0x81UL << 16;
    /* A CardBus bridge (type 2) with those registers: not acted on. */
    c = add(0, 8, 0, 2, 0x060700UL);
    c->Cfg[0x24 / 4] = 0xFFF10001UL;
    c->Cfg[0x28 / 4] = 1;
    c->Cfg[0x2C / 4] = 0x40;
    /* Bus 255, which no bridge leads to. */
    bridge(255, 31, 0, 0, 0, 0xFFF10001UL, 3, 0x40);
    run(&r);
    CHECK(f->Writes == 0 && f->Cfg[0x28 / 4] == 1, "absent function 0: function 1 not reached");
    CHECK(is_closed_form(g), "a multi-function device's function 3 bridge: closed");
    CHECK(h->Writes == 0, "no bit 7: function 3 not read");
    CHECK(is_closed_form(z), "header type 81h: a bridge, closed");
    CHECK(c->Writes == 0, "a CardBus bridge: not written");
    CHECK(is_closed_form(find(255, 31 << 3)), "bus 255 reached, no bridge leading to it");
    CHECK_EQ(r.Closed, 3, "three closed");
    CHECK_EQ(r.Functions, 6, "six functions found");
    CHECK_EQ(r.Result, PFW_RES_DONE, "result: done only");
    CHECK_EQ(stray, 0, "no write to a function the model lacks");

    /* A board with no window above 4 GB: nothing written. */
    reset();
    bridge(0, 1, 0, 1, 1, 0xFFF10001UL, 0, 0);
    bridge(0, 2, 0, 2, 2, 0x00000000UL, 0, 0);
    bridge(0, 3, 0, 3, 3, 0xC011C001UL, 0, 0);
    run(&r);
    CHECK_EQ(writes, 0, "no window above 4 GB: no write at all");
    CHECK_EQ(r.Result, PFW_RES_DONE, "no window above 4 GB: done only");
    CHECK_EQ(r.NBridges, 0, "no window above 4 GB: no record");

    /* Across 4 GB: left and logged, not written. */
    reset();
    f = bridge(0, 1, 0, 1, 1, 0xFFF1E001UL, 0, 1);
    run(&r);
    CHECK_EQ(f->Writes, 0, "across 4 GB: not written");
    CHECK(r.Across == 1 && r.NBridges == 1 &&
          r.Bridges[0].Action == PFW_ACT_ACROSS &&
          (r.Result & PFW_RES_ACROSS) != 0, "across 4 GB: logged");

    /* A close refused and one parked, through the whole step: the counts,
     * the result bits and the records (record 32 section 6). */
    reset();
    f = bridge(0, 1, 0, 1, 1, 0xFFF10001UL, 1, 0x40);
    f->IgnoreAt = 1;                    /* 2Ch not taken: refused */
    g = bridge(0, 2, 0, 2, 2, 0xFFF10001UL, 1, 0x40);
    g->ThirdAt = 2;                     /* 24h a third value ...  */
    g->IgnoreAt = 3;                    /* ... its undo ignored   */
    h = bridge(0, 3, 0, 3, 3, 0xFFF10001UL, 1, 0x40);
    run(&r);
    CHECK(r.Refused == 1 && r.Parked == 1 && r.Closed == 1 &&
          r.Result == (PFW_RES_DONE | PFW_RES_REFUSED | PFW_RES_PARKED),
          "refused and parked: counted, the result bits set");
    CHECK(r.NBridges == 3 && r.Bridges[0].Action == PFW_ACT_REFUSED &&
          r.Bridges[1].Action == PFW_ACT_PARKED &&
          r.Bridges[2].Action == PFW_ACT_CLOSED, "their records, in order");
    CHECK(r.Bridges[0].After[0] == 0xFFF10001UL &&
          r.Bridges[0].After[1] == 1 && r.Bridges[0].After[2] == 0x40,
          "refused: the record's after is the bridge as read, undone");
    CHECK(r.Bridges[1].After[0] == g->Cfg[0x24 / 4] &&
          r.Bridges[1].After[1] == g->Cfg[0x28 / 4] &&
          r.Bridges[1].After[2] == g->Cfg[0x2C / 4] &&
          (g->Cfg[0x24 / 4] & 0xFFF0FFF0UL) != 0xFFF00000UL,
          "parked: the record's after is the registers as they stay");
    CHECK(r.Bridges[1].Before[0] == 0xFFF10001UL &&
          r.Bridges[1].Before[1] == 1 && r.Bridges[1].Before[2] == 0x40,
          "parked: the record's before is the scan's");
    CHECK(is_closed_form(h), "the next bridge closed all the same");

    /* A bridge that reads otherwise between the scan and the close. */
    reset();
    f = bridge(0, 1, 0, 1, 1, 0xFFF10001UL, 1, 0x40);
    f->FlipAfter = 2;
    run(&r);
    CHECK_EQ(f->Writes, 0, "changed mid-way: left");
    CHECK((r.Result & PFW_RES_CHANGED) != 0 && r.Closed == 0, "changed mid-way: logged");
}

static void test_bars(void)
{
    PFW_RESULT r;
    FN *d;
    FN *e;

    reset();
    /* 00:03.0 window 1_0000_0000..40_FFFF_FFFF, buses 1 to 3. */
    bridge(0, 3, 0, 1, 3, 0xFFF10001UL, 1, 0x40);
    d = add(2, 0, 0, 0, 0x010802UL);
    bar64(d, 0x10, 0x2, 0x00000000UL);          /* inside        */
    bar64(d, 0x18, 0x41, 0x00000000UL);         /* above it      */
    d->Cfg[0x20 / 4] = 0xFE000000UL;            /* 32-bit memory */
    d->Cfg[0x24 / 4] = 0x0000E001UL;            /* I/O           */
    e = add(4, 0, 0, 0, 0x010802UL);
    bar64(e, 0x10, 0x2, 0x00000000UL);          /* bus 4: outside the range */
    /* Bus 0, below no window: a BAR above 4 GB is not counted. */
    bar64(add(0, 9, 0, 0, 0x030000UL), 0x10, 0x5, 0);
    /* An across window's subtree: not counted. */
    bridge(0, 10, 0, 5, 5, 0xFFF1E001UL, 0, 1);
    bar64(add(5, 0, 0, 0, 0x030000UL), 0x10, 0, 0xF0000000UL);
    run(&r);
    CHECK_EQ(r.BarsInside, 1, "one BAR inside the window");
    CHECK(r.NBars == 1 && r.Bars[0].Bus == 2 && r.Bars[0].DevFn == 0 &&
          r.Bars[0].Offset == 0x10 && r.Bars[0].Zero == 0 &&
          r.Bars[0].Upper == 2, "its record");
    CHECK(d->Writes == 0 && e->Writes == 0, "BARs read, never written or sized");
    CHECK_EQ(r.Closed, 1, "the window still closed");
}

static void test_xhci(void)
{
    PFW_RESULT r;
    FN *x;
    FN *y;
    FN *n;

    reset();
    x = add(3, 0, 0, 0, PFW_CLASS_XHCI);
    x->Cfg[0] = 0x24261B21UL;
    bar64(x, 0x10, 0x7F, 0x80000000UL);
    x->Cfg[0x10 / 4] = 0x80000004UL;            /* 64-bit, not prefetchable */
    y = add(3, 1, 0, 0, PFW_CLASS_XHCI);
    bar64(y, 0x10, 0, 0xFE000000UL);            /* below 4 GB */
    n = add(3, 2, 0, 0, 0x0C0320UL);            /* EHCI */
    bar64(n, 0x10, 1, 0);
    run(&r);
    CHECK(r.XhciHigh == 1 && r.NXhci == 1 &&
          (r.Result & PFW_RES_XHCI_HIGH) != 0, "one xHCI above 4 GB");
    CHECK(r.Xhci[0].Bus == 3 && r.Xhci[0].DevFn == 0 &&
          r.Xhci[0].Offset == 0x10 && r.Xhci[0].Zero == 0 &&
          r.Xhci[0].Id == 0x24261B21UL && r.Xhci[0].Hi == 0x7F &&
          r.Xhci[0].Lo == 0x80000000UL, "its record");
    CHECK_EQ(writes, 0, "nothing written to any controller");
}

static void test_limits(void)
{
    PFW_RESULT r;
    int i;

    reset();
    for (i = 0; i < 17; i++) {
        bridge((PFW_U32)i + 1, 0, 0, 0, 0, 0xFFF10001UL, 1, 0x40);
    }
    run(&r);
    CHECK(r.Closed == 17 && r.NBridges == PFW_MAX_BRIDGES &&
          (r.Result & PFW_RES_FULL) != 0, "17 bridges: all closed, 16 records, full");

    reset();
    for (i = 0; i < PFW_MAX_SUBJ + 1; i++) {
        bridge((PFW_U32)i + 1, 0, 0, 0, 0, 0xFFF10001UL, 1, 0x40);
    }
    run(&r);
    CHECK(r.Closed == PFW_MAX_SUBJ && (r.Result & PFW_RES_FULL) != 0,
          "one bridge more than the scan holds: the rest closed, full");
    CHECK(fns[PFW_MAX_SUBJ].Writes == 0, "the one beyond: left");

    /* 17 BARs inside one window: all counted, 16 recorded, full. */
    reset();
    bridge(0, 3, 0, 1, 1, 0xFFF10001UL, 1, 0x40);
    for (i = 0; i < 17; i++) {
        bar64(add(1, (PFW_U32)i, 0, 0, 0x010802UL), 0x10, 2, 0);
    }
    run(&r);
    CHECK(r.BarsInside == 17 && r.NBars == PFW_MAX_BARS &&
          (r.Result & PFW_RES_FULL) != 0 && r.Closed == 1,
          "17 BARs inside: counted, 16 records, full, the window closed");

    reset();
    for (i = 0; i < PFW_MAX_XHCI + 1; i++) {
        bar64(add(1, (PFW_U32)i, 0, 0, PFW_CLASS_XHCI), 0x10, 1, 0);
    }
    run(&r);
    CHECK(r.XhciHigh == PFW_MAX_XHCI + 1 && r.NXhci == PFW_MAX_XHCI &&
          (r.Result & PFW_RES_FULL) != 0, "xHCI table full");

    CHECK_EQ(sizeof(PFW_BRIDGE_REC), 28, "a bridge record is 28 bytes");
    CHECK_EQ(sizeof(PFW_BAR_REC), 8, "a BAR record is 8 bytes");
    CHECK_EQ(sizeof(PFW_XHCI_REC), 16, "an xHCI record is 16 bytes");
}

/* The emulator's 4G-off layout on the pentium3 bases as the VxD
 * read it (runs/run-44.md, "44.49"): nine bridges, each closed. */
static void test_emulator(void)
{
    static const PFW_U32 b[9][6] = {
        /* bus dev fn  24h          28h  2Ch */
        { 0, 0x03, 0, 0xFFF10001UL, 1, 0x40 },
        { 0, 0x1C, 0, 0x00110001UL, 0x41, 0x41 },
        { 1, 0, 0, 0x00310001UL, 1, 1 },
        { 2, 0, 0, 0x00310021UL, 1, 1 },
        { 2, 1, 0, 0x00110001UL, 1, 1 },
        { 2, 2, 0, 0x00310041UL, 1, 1 },
        { 2, 3, 0, 0x00310041UL, 1, 1 },
        { 3, 0, 0, 0x00310021UL, 1, 1 },
        { 4, 0, 0, 0x00310021UL, 1, 1 }
    };
    PFW_RESULT r;
    int i;

    reset();
    add(0, 0, 0, 0, 0x060000UL);
    for (i = 0; i < 9; i++) {
        bridge(b[i][0], b[i][1], b[i][2], b[i][0] + 1, b[i][0] + 1,
               b[i][3], b[i][4], b[i][5])->Track = 1;
    }
    run(&r);
    CHECK(r.Closed == 9 && r.NBridges == 9 && r.Result == PFW_RES_DONE,
          "the emulator's nine bridges closed");
    for (i = 0; i < 9; i++) {
        CHECK(r.Bridges[i].Action == PFW_ACT_CLOSED &&
              r.Bridges[i].After[0] == 0x0001FFF1UL &&
              r.Bridges[i].After[1] == 0 && r.Bridges[i].After[2] == 0 &&
              r.Bridges[i].Before[0] == b[i][3], "each record");
    }
    CHECK_EQ(unsafe, 0, "every state closed or above 4 GB");
}

/* XHCIPFW.SYS's decision (src\pfw\pfw_ntdec.c; record 32 section 11.2). */
static void test_ntdec(void)
{
    PFW_U32 s;

    CHECK(PfwNtDecide(5, 0, 1, 0, 0, 0, &s) == PFW_NT_RUN_UP && s == 0,
          "2000, one processor: the step at HIGH_LEVEL");
    CHECK(PfwNtDecide(5, 0, 2, 0, 0, 0, &s) == PFW_NT_SKIP &&
          s == PFW_SKIP_UNHELD, "2000, two processors: nothing, unheld");
    CHECK(PfwNtDecide(5, 0, 2, 0, 0, 1, &s) == PFW_NT_SKIP &&
          s == PFW_SKIP_UNHELD,
          "2000, two processors, even were the routine found: unheld");
    CHECK(PfwNtDecide(5, 1, 1, 0, 0, 0, &s) == PFW_NT_RUN_UP && s == 0,
          "XP, one processor: the step at HIGH_LEVEL");
    CHECK(PfwNtDecide(5, 1, 2, 0, 0, 0, &s) == PFW_NT_SKIP &&
          s == PFW_SKIP_UNHELD, "XP, two processors: nothing, unheld");
    CHECK(PfwNtDecide(5, 1, 2, 0, 0, 1, &s) == PFW_NT_SKIP &&
          s == PFW_SKIP_UNHELD,
          "XP, two processors, even were the routine found: unheld");
    CHECK(PfwNtDecide(5, 1, 1, 0, 1, 0, &s) == PFW_NT_SKIP &&
          s == PFW_SKIP_LATE, "XP, late: nothing");
    CHECK(PfwNtDecide(5, 2, 1, 0, 0, 1, &s) == PFW_NT_SKIP &&
          s == PFW_SKIP_SYSTEM, "Server 2003: not a system it acts on");
    CHECK(PfwNtDecide(4, 0, 1, 0, 0, 0, &s) == PFW_NT_SKIP &&
          s == PFW_SKIP_SYSTEM, "NT 4: not a system it acts on");
    CHECK(PfwNtDecide(6, 0, 1, 0, 0, 0, &s) == PFW_NT_RUN_UP,
          "Vista, one processor: HIGH_LEVEL, no routine needed");
    CHECK(PfwNtDecide(6, 1, 2, 0, 0, 1, &s) == PFW_NT_RUN_IPI && s == 0,
          "7, two processors, the routine found: inside the IPI");
    CHECK(PfwNtDecide(10, 0, 4, 0, 0, 1, &s) == PFW_NT_RUN_IPI,
          "10, four processors: inside the IPI");
    CHECK(PfwNtDecide(6, 2, 2, 0, 0, 0, &s) == PFW_NT_SKIP &&
          s == PFW_SKIP_UNHELD, "8, two processors, no routine: unheld");
    CHECK(PfwNtDecide(6, 1, 1, 1, 0, 1, &s) == PFW_NT_SKIP &&
          s == PFW_SKIP_SAFEMODE, "Safe Mode: nothing");
    CHECK(PfwNtDecide(6, 1, 1, 0, 1, 1, &s) == PFW_NT_SKIP &&
          s == PFW_SKIP_LATE, "after the boot drivers' time: late, nothing");
    CHECK(PfwNtDecide(5, 0, 1, 1, 1, 0, &s) == PFW_NT_SKIP &&
          s == (PFW_SKIP_SAFEMODE | PFW_SKIP_LATE),
          "Safe Mode and late: both reasons");
    CHECK(PfwNtDecide(6, 3, 2, 1, 0, 0, &s) == PFW_NT_SKIP &&
          s == PFW_SKIP_SAFEMODE,
          "Safe Mode on several processors: Safe Mode, not unheld");
}

/* One root bus's range (record 32 section 11.5.2): a bridge on a bus
 * outside it is neither read nor closed. */
static void test_range(void)
{
    PFW_RESULT r;
    FN *a;
    FN *b;

    reset();
    a = bridge(0, 3, 0, 1, 2, 0xFFF10001UL, 1, 0x40);
    b = bridge(0x80, 3, 0, 0x81, 0x81, 0xFFF10001UL, 1, 0x40);
    memset(&r, 0, sizeof(r));
    PfwRunBuses(&r, 0, 0x7F);
    CHECK(is_closed_form(a) && b->Writes == 0 && b->Cfg[0x28 / 4] == 1 &&
          r.Closed == 1, "buses 0 to 7Fh: the bridge on 80h left");
    memset(&r, 0, sizeof(r));
    PfwRunBuses(&r, 0x80, 0xFF);
    CHECK(is_closed_form(b) && r.Closed == 1,
          "buses 80h to FFh: that bridge closed");
}

/* XHCIPFW.SYS's role by version (record 32 section 11.5.2). */
static void test_ntrole(void)
{
    CHECK_EQ(PfwNtRole(5, 0), PFW_ROLE_ENTRY, "2000: the entry role");
    CHECK_EQ(PfwNtRole(5, 1), PFW_ROLE_ENTRY, "XP: the entry role");
    CHECK_EQ(PfwNtRole(5, 2), PFW_ROLE_NONE, "Server 2003: neither");
    CHECK_EQ(PfwNtRole(6, 0), PFW_ROLE_ENTRY, "Vista: the entry role");
    CHECK_EQ(PfwNtRole(6, 1), PFW_ROLE_ENTRY, "7: the entry role");
    CHECK_EQ(PfwNtRole(6, 2), PFW_ROLE_FILTER, "8: the filter role");
    CHECK_EQ(PfwNtRole(6, 3), PFW_ROLE_FILTER, "8.1: the filter role");
    CHECK_EQ(PfwNtRole(6, 4), PFW_ROLE_NONE, "6.4: neither");
    CHECK_EQ(PfwNtRole(10, 0), PFW_ROLE_NONE, "10: neither");
    CHECK_EQ(PfwNtRole(4, 0), PFW_ROLE_NONE, "NT 4: neither");
}

/* The filter role's account of several root buses (record 32 section
 * 11.5.2): a later bus's record does not hide an earlier one's. */
static void test_ntsum(void)
{
    PFW_NT_SUM s;

    memset(&s, 0, sizeof(s));
    PfwNtSumAdd(&s, 0, 0x01UL, 9);
    CHECK(s.RootBuses == 1 && s.SkippedAny == 0 && s.ResultAny == 0x01UL &&
          s.ClosedAll == 9, "one root bus run: its values");
    PfwNtSumAdd(&s, PFW_SKIP_LATE, 0, 0);
    CHECK(s.RootBuses == 2 && s.SkippedAny == PFW_SKIP_LATE &&
          s.ResultAny == 0x01UL && s.ClosedAll == 9,
          "a second bus late: the first's closes kept, its reason added");
    PfwNtSumAdd(&s, PFW_SKIP_UNATTACHED, 0, 0);
    PfwNtSumAdd(&s, 0, 0x05UL, 2);
    CHECK(s.RootBuses == 4 &&
          s.SkippedAny == (PFW_SKIP_LATE | PFW_SKIP_UNATTACHED) &&
          s.ResultAny == 0x05UL && s.ClosedAll == 11,
          "an unattached bus and a third run: every reason, result and close");
    s.RootBuses = 0xFFFFFFFFUL;
    s.ClosedAll = 0xFFFFFFF0UL;
    PfwNtSumAdd(&s, 0, 0, 0x20);
    CHECK(s.RootBuses == 0xFFFFFFFFUL && s.ClosedAll == 0xFFFFFFFFUL,
          "the counts held at their top");
}

/* ---- the interrupt-line pass (record 32 section 12.10, task 46.11) ---- */

static int bytes8;      /* byte writes, any function                  */
static int lineLock;    /* the model's line byte ignores writes       */

void PfwCfgWrite8(PFW_U32 bus, PFW_U32 devfn, PFW_U32 off, PFW_U32 val)
{
    FN *f = find(bus, devfn);

    bytes8++;
    if (f == NULL || (off & 3UL) != 0) {
        stray++;
        return;
    }
    if (lineLock && off == 0x3C) {
        return;
    }
    f->Cfg[off / 4] = (f->Cfg[off / 4] & 0xFFFFFF00UL) | (val & 0xFFUL);
}

/* A function with its line and pin, decode on. */
static FN *lined(FN *f, PFW_U32 line, PFW_U32 pin)
{
    f->Cfg[0x3C / 4] = 0x12340000UL | (pin << 8) | line;
    f->Cfg[0x04 / 4] = 0x00100006UL;
    return f;
}

/* The ASM4242's shape: root port 00:03.0 -> upstream port 01:00.0 ->
 * downstream ports 02:00.0 (a tunnel port, its bus empty), 02:02.0 (the
 * xHCI's) and 02:03.0 (the NHI's, bus 64h). */
static FN *nhiOf;
static FN *dspOf;
static FN *tunOf;
static FN *xhcOf;

static void asm4242(void)
{
    FN *f;

    reset();
    bridge(0, 3, 0, 1, 0x70, 0, 0, 0);
    f = bridge(1, 0, 0, 2, 0x70, 0, 0, 0);
    f->Cfg[0] = 0x24211B21UL;
    tunOf = lined(bridge(2, 0, 0, 3, 0x30, 0, 0, 0), 0x0B, 1);
    tunOf->Cfg[0] = 0x24231B21UL;
    f = lined(bridge(2, 2, 0, 0x60, 0x60, 0, 0, 0), 0x0B, 1);
    f->Cfg[0] = 0x24231B21UL;
    dspOf = lined(bridge(2, 3, 0, 0x64, 0x64, 0, 0, 0), 0x04, 1);
    dspOf->Cfg[0] = 0x24231B21UL;
    dspOf->Cfg[0x3C / 4] |= 0x00010000UL;    /* a Bridge Control bit */
    xhcOf = lined(add(0x60, 0, 0, 0, 0x0C0330UL), 0x0B, 1);
    xhcOf->Cfg[0] = 0x24261B21UL;
    nhiOf = lined(add(0x64, 0, 0, 0, 0x0C0340UL), 0x04, 1);
    nhiOf->Cfg[0] = 0x24251B21UL;
}

static void lines(PFW_RESULT *r, PFW_U32 setting)
{
    memset(r, 0, sizeof(*r));
    r->Switch.Setting = setting;
    bytes8 = 0;
    PfwLines(r);
}

static void test_lines(void)
{
    static PFW_RESULT r;
    FN *f;
    PFW_U32 tun;
    PFW_U32 xhc;

    CHECK_EQ(sizeof(PFW_LINE_REC), 8, "a line record is 8 bytes");
    CHECK_EQ(sizeof(PFW_SWITCH), 24, "the switch record six dwords");

    /* The switch (step 1). */
    CHECK_EQ(PfwSwitchSetting(0, 0, 4, 4, 0), PFW_SW_OFF, "0 read whole: off");
    CHECK_EQ(PfwSwitchSetting(0, 0, 4, 4, 1), PFW_SW_ON, "1: on");
    CHECK_EQ(PfwSwitchSetting(0, 0, 4, 4, 7), PFW_SW_ON, "another value refused: the default");
    CHECK_EQ(PfwSwitchSetting(0, 2, 0, 4, 0), PFW_SW_ON, "missing: the default");
    CHECK_EQ(PfwSwitchSetting(0, 234, 4, 4, 0), PFW_SW_FAILED, "another query code: lines left");
    CHECK_EQ(PfwSwitchSetting(0, 0, 3, 4, 0), PFW_SW_FAILED, "a binary value: left");
    CHECK_EQ(PfwSwitchSetting(0, 0, 4, 2, 0), PFW_SW_FAILED, "two bytes: left");
    CHECK_EQ(PfwSwitchSetting(1011, 0, 4, 4, 1), PFW_SW_FAILED, "the key not opened: left");

    /* The NHI by id or class (step 2). */
    CHECK(PfwIsNhi(0x24251B21UL, 0), "the ASM4242 NHI by its id");
    CHECK(PfwIsNhi(0x12345678UL, 0x0C034000UL), "any USB4 host interface by class");
    CHECK(!PfwIsNhi(0x24261B21UL, 0x0C033000UL), "the xHCI is not one");
    CHECK(!PfwIsNhi(0x24231B21UL, 0x06040000UL), "nor a downstream port");

    asm4242();
    tun = tunOf->Cfg[0x3C / 4];
    xhc = xhcOf->Cfg[0x3C / 4];
    lines(&r, PFW_SW_ON);
    CHECK_EQ(r.NLines, 2, "the NHI and its own port");
    CHECK_EQ(nhiOf->Cfg[0x3C / 4], 0x123401FFUL, "the NHI's line FFh, the rest kept");
    CHECK_EQ(dspOf->Cfg[0x3C / 4], 0x123501FFUL, "the port's line FFh, Bridge Control kept");
    CHECK_EQ(tunOf->Cfg[0x3C / 4], tun, "the tunnel port never touched");
    CHECK_EQ(xhcOf->Cfg[0x3C / 4], xhc, "the xHCI never touched");
    CHECK_EQ(bytes8, 2, "two byte writes");
    CHECK_EQ(writes, 0, "no dword write");
    CHECK_EQ(stray, 0, "nothing written elsewhere");
    CHECK_EQ(r.Lines[0].Role, PFW_ROLE_NHI, "first the NHI");
    CHECK_EQ(r.Lines[0].Outcome, PFW_LN_WRITTEN, "written");
    CHECK_EQ(r.Lines[0].Before, 0x04, "its line before");
    CHECK_EQ(r.Lines[0].After, 0xFF, "and after");
    CHECK_EQ(r.Lines[1].Role, PFW_ROLE_PORT, "then its port");
    CHECK_EQ(r.Lines[1].Bus, 2, "02:03.0");
    CHECK_EQ(r.Lines[1].DevFn, 3 << 3, "02:03.0's devfn");
    CHECK_EQ(r.Lines[1].Hdr, 1, "a type 1 header");
    CHECK(r.Result & PFW_RES_LINE, "a line written");

    lines(&r, PFW_SW_ON);
    CHECK_EQ(r.Lines[0].Outcome, PFW_LN_ALREADY, "the next start: FFh already");
    CHECK_EQ(bytes8, 0, "and nothing written");

    asm4242();
    lines(&r, PFW_SW_OFF);
    CHECK_EQ(r.NLines, 2, "the switch 0: both recorded");
    CHECK_EQ(r.Lines[0].Outcome, PFW_LN_LEFT, "left");
    CHECK_EQ(bytes8, 0, "nothing written");
    CHECK(r.Result & PFW_RES_LINE_OFF, "the switch 0 in the result");
    lines(&r, PFW_SW_FAILED);
    CHECK_EQ(bytes8, 0, "the switch not read: nothing written");
    CHECK(r.Result & PFW_RES_LINE_NOREAD, "and said");

    /* The port's bus holds something else: the NHI alone. */
    asm4242();
    lined(add(0x64, 1, 0, 0, 0x020000UL), 0x0B, 1);
    lines(&r, PFW_SW_ON);
    CHECK_EQ(r.NLines, 1, "a second device on the NHI's bus: no port");
    CHECK_EQ(dspOf->Cfg[0x3C / 4] & 0xFF, 0x04, "the port left");
    asm4242();
    lined(add(0x64, 0, 1, 0, 0x020000UL), 0x0B, 1);
    lines(&r, PFW_SW_ON);
    CHECK_EQ(r.NLines, 1, "a second function of the NHI's device: no port");

    /* Two bridges claiming the NHI's bus: none taken. */
    asm4242();
    bridge(0x50, 0, 0, 0x64, 0x64, 0, 0, 0);
    lines(&r, PFW_SW_ON);
    CHECK_EQ(r.NLines, 1, "two ports claim the bus: none taken");

    /* Decode off; pin 0; a line that does not read back. */
    asm4242();
    dspOf->Cfg[0x04 / 4] = 0x00100000UL;
    lines(&r, PFW_SW_ON);
    CHECK_EQ(r.Lines[1].Outcome, PFW_LN_NODECODE, "decode off: left");
    CHECK_EQ(dspOf->Cfg[0x3C / 4] & 0xFF, 0x04, "its line kept");
    asm4242();
    nhiOf->Cfg[0x3C / 4] &= 0xFFFF00FFUL;
    lines(&r, PFW_SW_ON);
    CHECK_EQ(r.NLines, 0, "an NHI with pin 0 is not selected");
    CHECK_EQ(dspOf->Cfg[0x3C / 4] & 0xFF, 0x04, "nor its port");
    asm4242();
    lineLock = 1;
    lines(&r, PFW_SW_ON);
    lineLock = 0;
    CHECK_EQ(r.Lines[0].Outcome, PFW_LN_REFUSED, "a line that does not read back: refused");
    CHECK(r.Result & PFW_RES_LINE_BAD, "and said");

    /* An integrated NHI on the root bus: alone, no port. */
    reset();
    lined(add(0, 0x0D, 0, 0x80, 0x0C0330UL), 0x0A, 1);
    f = lined(add(0, 0x0D, 2, 0, 0x0C0340UL), 0x0A, 1);
    f->Cfg[0] = 0x9A1B8086UL;
    lines(&r, PFW_SW_ON);
    CHECK_EQ(r.NLines, 1, "an NHI on the root bus: itself alone");
    CHECK_EQ(f->Cfg[0x3C / 4] & 0xFF, 0xFF, "its line FFh");

    /* No NHI: nothing. */
    reset();
    lined(add(0, 2, 0, 0, 0x0C0330UL), 0x0B, 1);
    lines(&r, PFW_SW_ON);
    CHECK_EQ(r.NLines, 0, "no host interface: nothing");
    CHECK_EQ(bytes8, 0, "and nothing written");
}

/* The id list equal to xhcicm.inf's [CmModels] Cm.Dev rows, the class row
 * included (record 32 section 12.10 step 2). */
static void test_line_ids(void)
{
    static char inf[65536];
    static unsigned char hit[256];
    FILE *fp;
    size_t n;
    char *sec;
    char *end;
    char *p;
    PFW_U32 seen;
    PFW_U32 i;
    unsigned v;
    unsigned d;
    int cls;

    fp = fopen("..\\src\\cm\\xhcicm.inf", "rb");
    CHECK(fp != NULL, "xhcicm.inf read");
    if (fp == NULL) {
        return;
    }
    n = fread(inf, 1, sizeof(inf) - 1, fp);
    fclose(fp);
    inf[n] = '\0';
    CHECK(n < sizeof(inf) - 1, "xhcicm.inf read whole, not cut at the buffer");
    sec = strstr(inf, "\r\n[CmModels]\r\n");
    CHECK(sec != NULL, "its [CmModels]");
    if (sec == NULL) {
        return;
    }
    sec += 2;
    end = strstr(sec + 1, "\r\n[");
    if (end != NULL) {
        *end = '\0';
    }
    CHECK(PfwNhiIdCount <= sizeof(hit), "the list fits this test's table");
    if (PfwNhiIdCount > sizeof(hit)) {
        return;
    }
    memset(hit, 0, sizeof(hit));
    seen = 0;
    cls = 0;
    for (p = strstr(sec, "=Cm.Dev,PCI\\"); p != NULL;
         p = strstr(p + 1, "=Cm.Dev,PCI\\")) {
        if (strncmp(p + 12, "CC_0C0340", 9) == 0) {
            cls = 1;
            continue;
        }
        CHECK(sscanf(p + 12, "VEN_%4x&DEV_%4x", &v, &d) == 2, "an id row");
        for (i = 0; i < PfwNhiIdCount; i++) {
            if (PfwNhiIds[i] == (((PFW_U32)d << 16) | v)) {
                break;
            }
        }
        CHECK(i < PfwNhiIdCount, "each INF id in the VxD's list");
        if (i < PfwNhiIdCount) {
            CHECK(!hit[i], "no INF id row twice");
            hit[i] = 1;
        }
        seen++;
    }
    CHECK_EQ(seen, PfwNhiIdCount, "as many ids as the list holds");
    /* Both directions: every id of the VxD's list is an INF row, so a
     * duplicated row cannot stand in for a missing one. */
    for (i = 0; i < PfwNhiIdCount; i++) {
        CHECK(hit[i], "each id of the VxD's list in the INF");
    }
    CHECK(cls, "the class row there, PFW_CLASS_USB4_NHI");
}

int main(void)
{
    test_classify();
    test_ntrole();
    test_ntsum();
    test_range();
    test_close();
    test_scan();
    test_bars();
    test_xhci();
    test_limits();
    test_emulator();
    test_ntdec();
    test_lines();
    test_line_ids();
    printf("test_pfw: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
