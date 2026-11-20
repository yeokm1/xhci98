/*
 * test_msg4.c - host vectors for XhciInterruptMode 4's pure half
 * (src\xhci_msg4.c; roadmap-usb4 task 49.24; design record 15 section
 * 4.5): the resource-map list walk of the veto (x86 and amd64 descriptor
 * sizes, several full descriptors, device-specific data skipped by its
 * size, every bound and truncation refused), the names of the map's
 * values, the image's refusal table and its latch, the stray-entry brake,
 * the mode-4 decision row by row (the system classes, the table, the
 * quirk, every value's set, the pools, the range and the budget, the line
 * strategy 1 needs), XhciMsiShare's reading, the stranded-event judge,
 * the packing of a refused claim's reasons, and a refused start's record
 * (task 50.4, section 4.5.20): which start is one, the record refused and
 * cleared, and what a value read holds.
 */
#include <stdio.h>
#include "../src/xhci_msg4.h"
#include "../src/xhci_poll.h"
#include "test_harness.h"

static void put32(UCHAR *b, ULONG off, ULONG v)
{
    b[off] = (UCHAR)v;
    b[off + 1] = (UCHAR)(v >> 8);
    b[off + 2] = (UCHAR)(v >> 16);
    b[off + 3] = (UCHAR)(v >> 24);
}

/* A partial descriptor of `pd` bytes at `off`: its type, and a vector at
 * 8 or a DataSize at 4. Returns the offset after it. */
static ULONG pdesc(UCHAR *b, ULONG off, ULONG pd, ULONG type, ULONG value)
{
    ULONG i;

    for (i = 0; i < pd; i++) {
        b[off + i] = 0;
    }
    b[off] = (UCHAR)type;
    if (type == XHCI_MSG4_TYPE_INTERRUPT) {
        put32(b, off + 4, 0x10);
        put32(b, off + 8, value);
    } else if (type == XHCI_MSG4_TYPE_DEVSPECIFIC) {
        put32(b, off + 4, value);
    } else {
        put32(b, off + 4, value);
    }
    return off + pd;
}

static ULONG fullHead(UCHAR *b, ULONG off, ULONG count)
{
    put32(b, off, 5);          /* PCIBus */
    put32(b, off + 4, 0);
    put32(b, off + 8, 1);      /* Version 1, Revision 0 */
    put32(b, off + 12, count);
    return off + XHCI_MSG4_FULL_HEADER;
}

static void test_scan(ULONG pd)
{
    UCHAR b[512];
    ULONG off;
    ULONG ints;
    ULONG len;
    ULONG i;

    for (i = 0; i < sizeof(b); i++) {
        b[i] = 0xCC;
    }
    put32(b, 0, 2);
    off = fullHead(b, 4, 3);
    off = pdesc(b, off, pd, 3, 0xF0000000UL);           /* memory */
    off = pdesc(b, off, pd, XHCI_MSG4_TYPE_DEVSPECIFIC, 6);
    for (i = 0; i < 6; i++) {
        b[off + i] = XHCI_MSG4_TYPE_INTERRUPT;           /* not a descriptor */
    }
    off += 6;
    off = pdesc(b, off, pd, XHCI_MSG4_TYPE_INTERRUPT, 0x81);
    off = fullHead(b, off, 1);
    off = pdesc(b, off, pd, XHCI_MSG4_TYPE_INTERRUPT, 0x183);
    len = off;

    ints = 0;
    CHECK_EQ(XhciMsg4ShareScan(b, len, pd, 0x183, &ints),
             XHCI_MSG4_SHARE_FOUND, "the second full descriptor's vector");
    CHECK_EQ(ints, 2, "two interrupts, the device data skipped");
    ints = 0;
    CHECK_EQ(XhciMsg4ShareScan(b, len, pd, 0x81, &ints),
             XHCI_MSG4_SHARE_FOUND, "the first's");
    CHECK_EQ(XhciMsg4ShareScan(b, len, pd, 0x91, NULL), XHCI_MSG4_SHARE_NONE,
             "another vector: none");
    CHECK_EQ(XhciMsg4ShareScan(b, len, pd, 2, NULL), XHCI_MSG4_SHARE_NONE,
             "device data bytes never read as a descriptor");
    CHECK_EQ(XhciMsg4ShareScan(b, len - 1, pd, 0x91, NULL),
             XHCI_MSG4_SHARE_MALFORMED, "a list one byte short");
    CHECK_EQ(XhciMsg4ShareScan(b, 3, pd, 0x91, NULL),
             XHCI_MSG4_SHARE_MALFORMED, "no count");
    CHECK_EQ(XhciMsg4ShareScan(b, len, pd + 1, 0x91, NULL),
             XHCI_MSG4_SHARE_MALFORMED, "a descriptor size of neither");
    CHECK_EQ(XhciMsg4ShareScan(NULL, len, pd, 0x91, NULL),
             XHCI_MSG4_SHARE_MALFORMED, "no data");

    /* Device data running past the end. */
    put32(b, 4 + XHCI_MSG4_FULL_HEADER + pd + 4, 0x7FFFFFF0UL);
    CHECK_EQ(XhciMsg4ShareScan(b, len, pd, 0x91, NULL),
             XHCI_MSG4_SHARE_MALFORMED, "device data past the end");
    put32(b, 4 + XHCI_MSG4_FULL_HEADER + pd + 4, 6);

    put32(b, 0, XHCI_MSG4_MAX_FULL + 1);
    CHECK_EQ(XhciMsg4ShareScan(b, len, pd, 0x91, NULL),
             XHCI_MSG4_SHARE_MALFORMED, "too many full descriptors");
    put32(b, 0, 3);
    CHECK_EQ(XhciMsg4ShareScan(b, len, pd, 0x91, NULL),
             XHCI_MSG4_SHARE_MALFORMED, "a third full descriptor missing");
    put32(b, 0, 2);
    put32(b, 4 + 12, XHCI_MSG4_MAX_PARTIAL + 1);
    CHECK_EQ(XhciMsg4ShareScan(b, len, pd, 0x91, NULL),
             XHCI_MSG4_SHARE_MALFORMED, "too many partial descriptors");
    put32(b, 4 + 12, 3);

    put32(b, 0, 0);
    CHECK_EQ(XhciMsg4ShareScan(b, 4, pd, 0x91, NULL), XHCI_MSG4_SHARE_NONE,
             "an empty list");
}

static void wide(USHORT *w, const char *s, ULONG *n)
{
    ULONG i;

    for (i = 0; s[i] != '\0'; i++) {
        w[i] = (USHORT)(UCHAR)s[i];
    }
    *n = i;
}

static void test_names(void)
{
    USHORT own[64];
    USHORT name[64];
    ULONG ownN;
    ULONG n;

    wide(own, "\\Device\\NTPNP_PCI0012", &ownN);
    wide(name, "\\Device\\NTPNP_PCI0012.Translated", &n);
    CHECK_EQ(XhciMsg4NameIsTranslated(name, n), 1, ".Translated");
    CHECK_EQ(XhciMsg4NameIsOwn(name, n, own, ownN), 1, "this controller's");
    wide(name, "\\Device\\NTPNP_PCI0012.Raw", &n);
    CHECK_EQ(XhciMsg4NameIsTranslated(name, n), 0, ".Raw is not");
    CHECK_EQ(XhciMsg4NameIsOwn(name, n, own, ownN), 0, "nor its own");
    wide(name, "\\Device\\NTPNP_PCI0013.Translated", &n);
    CHECK_EQ(XhciMsg4NameIsOwn(name, n, own, ownN), 0, "another device");
    wide(name, "\\Device\\NTPNP_PCI00121.Translated", &n);
    CHECK_EQ(XhciMsg4NameIsOwn(name, n, own, ownN), 0, "a longer name");
    CHECK_EQ(XhciMsg4NameIsOwn(name, n, own, 0), 0, "no own name");
    CHECK_EQ(XhciMsg4NameIsTranslated(name, 5), 0, "too short");
}

static void test_stray(void)
{
    ULONG run;
    ULONG i;
    ULONG trips;

    run = 0;
    CHECK_EQ(XhciMsg4StrayStep(&run, 1, 0), 0, "claimed: no stray");
    CHECK_EQ(run, 0, "claimed: run 0");
    CHECK_EQ(XhciMsg4StrayStep(&run, 0, 0), 0, "one stray");
    CHECK_EQ(run, 1, "run 1");
    CHECK_EQ(XhciMsg4StrayStep(&run, 0, 1), 0, "an event pending ends it");
    CHECK_EQ(run, 0, "pending: run 0");
    trips = 0;
    for (i = 0; i < XHCI_MSG4_STRAY_LIMIT - 1; i++) {
        trips += XhciMsg4StrayStep(&run, 0, 0);
    }
    CHECK_EQ(trips, 0, "below the limit: no trip");
    CHECK_EQ(XhciMsg4StrayStep(&run, 0, 0), 1, "the limit: tripped");
    CHECK_EQ(XhciMsg4StrayStep(&run, 0, 0), 0, "once only");
    CHECK_EQ(run, XHCI_MSG4_STRAY_LIMIT, "held at the limit");
    CHECK_EQ(XhciMsg4StrayStep(&run, 1, 0), 0, "a claim resets");
    CHECK_EQ(run, 0, "reset");
    for (i = 0; i < XHCI_MSG4_STRAY_LIMIT - 1; i++) {
        (VOID)XhciMsg4StrayStep(&run, 0, 0);
    }
    (VOID)XhciMsg4StrayStep(&run, 1, 0);
    CHECK_EQ(XhciMsg4StrayStep(&run, 0, 0), 0, "a claim one short: none");
}

static void test_refusals(void)
{
    XHCI_MSG4_REFUSALS t;
    ULONG i;

    t.Count = 0;
    t.Latched = 0;
    CHECK_EQ(XhciMsg4Bdf(2, 0x1F, 7), 0x02FFUL, "bus 15:8, dev 7:3, fn 2:0");
    CHECK_EQ(XhciMsg4Refused(&t, 1, 0x18), 0, "empty");
    CHECK_EQ(XhciMsg4Refused(&t, 0, 0x18), 0, "empty, location unknown");
    CHECK_EQ(XhciMsg4RefusalAdd(&t, 1, 0x18), 1, "added");
    CHECK_EQ(XhciMsg4RefusalAdd(&t, 1, 0x18), 1, "again, once");
    CHECK_EQ(t.Count, 1, "one entry");
    CHECK_EQ(XhciMsg4Refused(&t, 1, 0x18), 1, "found");
    CHECK_EQ(XhciMsg4Refused(&t, 1, 0x19), 0, "another function");
    CHECK_EQ(XhciMsg4Refused(&t, 0, 0x18), 0, "unknown location: table not asked");
    CHECK_EQ(XhciMsg4RefusalCount(&t), 1, "count 1");
    for (i = 1; i < XHCI_MSG4_REFUSAL_SLOTS; i++) {
        (VOID)XhciMsg4RefusalAdd(&t, 1, 0x100 + i);
    }
    CHECK_EQ(t.Count, XHCI_MSG4_REFUSAL_SLOTS, "full");
    CHECK_EQ(t.Latched, 1, "full: the latch at once");
    CHECK_EQ(XhciMsg4Refused(&t, 1, 0x200), 1, "full: another refused");
    CHECK_EQ(XhciMsg4RefusalAdd(&t, 1, 0x200), 0, "full: not added");
    CHECK_EQ(t.Count, XHCI_MSG4_REFUSAL_SLOTS, "full: count kept");
    CHECK_EQ(XhciMsg4Refused(&t, 1, 0x300), 1, "latched: every function");
    CHECK_EQ(XhciMsg4Refused(&t, 0, 0), 1, "latched: unknown too");
    CHECK_EQ(XhciMsg4RefusalCount(&t), XHCI_MSG4_REFUSAL_SLOTS + 1,
             "count with the latch");

    t.Count = 0;
    t.Latched = 0;
    CHECK_EQ(XhciMsg4RefusalAdd(&t, 0, 0), 1, "location never read");
    CHECK_EQ(t.Latched, 1, "it latches");
    CHECK_EQ(t.Count, 0, "nothing entered");
    CHECK_EQ(XhciMsg4Refused(&t, 1, 0x18), 1, "every function refused");
}

static void inInit(XHCI_MSG4_IN *in, ULONG system)
{
    in->System = system;
    in->Quarantined = 0;
    in->Avoided = 0;
    in->Strategy = XHCI_MSG4_NOT_FOUND;
    in->IsaIrq = XHCI_MSG4_NOT_FOUND;
    in->PoolsOffered = 1;
    in->Pools = XHCI_MSG4_NOT_FOUND;
    in->InputFirst = XHCI_MSG4_NOT_FOUND;
    in->InputLast = XHCI_MSG4_NOT_FOUND;
    in->Budget = XHCI_MSG4_NOT_FOUND;
    in->HaveInterrupt = 1;
    in->Message = 0;
    in->PinStatus = XHCI_POLL_PIN_READ;
    in->Pin = 1;
}

#define DECIDE(in, d, r, what)                                         \
    do {                                                               \
        ULONG reason_ = 0xEEEEEEEEUL;                                  \
        CHECK_EQ(XhciMsg4Decide(&(in), &c, &reason_), (d), what);      \
        CHECK_EQ(reason_, (r), what " (reason)");                      \
    } while (0)

static void test_decide(void)
{
    XHCI_MSG4_IN in;
    XHCI_MSG4_CLAIM c;
    ULONG irqs[] = { 3, 4, 5, 6, 7, 10, 11 };
    ULONG bad[] = { 1, 2, 8, 9, 12, 13, 14, 15, 16, 23, 24, 255, 256 };
    ULONG i;

    /* 98 SE and ME: no message (the driver runs mode 4 as 0 there and
     * never asks; the decision answers no system all the same). */
    inInit(&in, XHCI_MSG4_SYS_9X);
    in.Strategy = 0;
    in.HaveInterrupt = 1;
    DECIDE(in, XHCI_POLL_DELIVERY_NONE, XHCI_POLL_R_MSG4_NO_SYSTEM,
           "9x: no system");
    in.Quarantined = 1;
    in.Avoided = 1;
    DECIDE(in, XHCI_POLL_DELIVERY_NONE, XHCI_POLL_R_MSG4_NO_SYSTEM,
           "9x: no system, before the table and the quirk");

    /* Vista and later first of all. */
    inInit(&in, XHCI_MSG4_SYS_NT6);
    in.Quarantined = 1;
    in.Strategy = 9;
    DECIDE(in, XHCI_POLL_DELIVERY_NONE, XHCI_POLL_R_MSG4_NO_SYSTEM,
           "Vista: no system, before all");
    inInit(&in, 7);
    DECIDE(in, XHCI_POLL_DELIVERY_NONE, XHCI_POLL_R_MSG4_NO_SYSTEM,
           "an unknown system class: none");

    /* NT 5: the defaults are the claim on the ISA pool, the library's
     * choice. */
    inInit(&in, XHCI_MSG4_SYS_NT5);
    in.HaveInterrupt = 0;
    DECIDE(in, XHCI_POLL_DELIVERY_MESSAGE, XHCI_POLL_R_FORCED_MSG4,
           "NT5 defaults: claimed, no line needed");
    CHECK_EQ(c.Strategy, 0, "strategy 0");
    CHECK_EQ(c.Irq, 0, "the library's choice");
    CHECK_EQ(c.Pools, XHCI_MSG4_POOLS_ISA, "the ISA pool");
    CHECK_EQ(c.LibPools, 1, "MSGIRQ_POOL_ISA");
    CHECK_EQ(c.HighFirst + c.HighLast + c.Budget, 0, "no upper fields");
    in.Quarantined = 1;
    in.Avoided = 1;
    DECIDE(in, XHCI_POLL_DELIVERY_NONE, XHCI_POLL_R_MSG4_QUARANTINED,
           "NT5: the table first");
    in.Quarantined = 0;
    DECIDE(in, XHCI_POLL_DELIVERY_NONE, XHCI_POLL_R_MSG4_AVOIDED,
           "NT5: the quirk");
    in.Avoided = 0;
    in.Strategy = 2;
    DECIDE(in, XHCI_POLL_DELIVERY_NONE, XHCI_POLL_R_MSG4_VALUE_INVALID,
           "strategy 2: invalid");
    in.Strategy = 0xFFFFFFFEUL;
    DECIDE(in, XHCI_POLL_DELIVERY_NONE, XHCI_POLL_R_MSG4_VALUE_INVALID,
           "strategy huge: invalid");

    /* The forced IRQ's set. */
    for (i = 0; i < sizeof(irqs) / sizeof(irqs[0]); i++) {
        inInit(&in, XHCI_MSG4_SYS_NT5);
        in.Strategy = 0;
        in.IsaIrq = irqs[i];
        DECIDE(in, XHCI_POLL_DELIVERY_MESSAGE, XHCI_POLL_R_FORCED_MSG4,
               "a forced ISA IRQ of the seven");
        CHECK_EQ(c.Irq, irqs[i], "forced IRQ passed");
    }
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        inInit(&in, XHCI_MSG4_SYS_NT5);
        in.IsaIrq = bad[i];
        DECIDE(in, XHCI_POLL_DELIVERY_NONE, XHCI_POLL_R_MSG4_VALUE_INVALID,
               "an IRQ outside the ISA set, ISA pool");
    }

    /* Strategy 1: the IRQ is not read, the line is needed. */
    inInit(&in, XHCI_MSG4_SYS_NT5);
    in.Strategy = 1;
    in.IsaIrq = 99;
    in.Pools = 9;
    DECIDE(in, XHCI_POLL_DELIVERY_MESSAGE, XHCI_POLL_R_FORCED_MSG4,
           "strategy 1: the claim's values not read");
    CHECK_EQ(c.Strategy, 1, "strategy 1 passed");
    in.PinStatus = XHCI_POLL_PIN_UNREADABLE;
    in.Pin = 0;
    DECIDE(in, XHCI_POLL_DELIVERY_MESSAGE, XHCI_POLL_R_FORCED_MSG4,
           "strategy 1: an unreadable pin let through");
    in.PinStatus = XHCI_POLL_PIN_READ;
    DECIDE(in, XHCI_POLL_DELIVERY_NONE, XHCI_POLL_R_MSG4_NO_LINE,
           "strategy 1: pin 0");
    in.Pin = 1;
    in.Message = 1;
    DECIDE(in, XHCI_POLL_DELIVERY_NONE, XHCI_POLL_R_MSG4_NO_LINE,
           "strategy 1: a message resource");
    in.Message = 0;
    in.HaveInterrupt = 0;
    DECIDE(in, XHCI_POLL_DELIVERY_NONE, XHCI_POLL_R_MSG4_NO_LINE,
           "strategy 1: no resource");
    in.Avoided = 1;
    DECIDE(in, XHCI_POLL_DELIVERY_NONE, XHCI_POLL_R_MSG4_AVOIDED,
           "the quirk before the line");

    /* The pools (owner, 2026-11-11). */
    inInit(&in, XHCI_MSG4_SYS_NT5);
    in.Pools = 3;
    DECIDE(in, XHCI_POLL_DELIVERY_NONE, XHCI_POLL_R_MSG4_VALUE_INVALID,
           "pools 3: invalid");
    in.Pools = 1;
    DECIDE(in, XHCI_POLL_DELIVERY_MESSAGE, XHCI_POLL_R_FORCED_MSG4,
           "pools 1: the upper pool alone");
    CHECK_EQ(c.LibPools, 2, "MSGIRQ_POOL_HIGH");
    CHECK_EQ(c.HighFirst, 0, "the library's range");
    CHECK_EQ(c.Budget, 0, "the library's budget");
    in.Pools = 2;
    DECIDE(in, XHCI_POLL_DELIVERY_MESSAGE, XHCI_POLL_R_FORCED_MSG4,
           "pools 2: both");
    CHECK_EQ(c.LibPools, 3, "both pools");
    in.IsaIrq = 10;
    DECIDE(in, XHCI_POLL_DELIVERY_MESSAGE, XHCI_POLL_R_FORCED_MSG4,
           "both: an ISA IRQ forced");
    in.IsaIrq = 40;
    DECIDE(in, XHCI_POLL_DELIVERY_MESSAGE, XHCI_POLL_R_FORCED_MSG4,
           "both: an upper input forced");
    CHECK_EQ(c.Irq, 40, "input 40 passed");
    in.IsaIrq = 23;
    DECIDE(in, XHCI_POLL_DELIVERY_NONE, XHCI_POLL_R_MSG4_VALUE_INVALID,
           "both: 23 is neither pool");
    in.Pools = 1;
    in.IsaIrq = 10;
    DECIDE(in, XHCI_POLL_DELIVERY_NONE, XHCI_POLL_R_MSG4_VALUE_INVALID,
           "the upper pool alone: an ISA IRQ is outside it");
    in.Pools = 0;
    in.IsaIrq = 40;
    DECIDE(in, XHCI_POLL_DELIVERY_NONE, XHCI_POLL_R_MSG4_VALUE_INVALID,
           "the ISA pool: an upper input is outside it");

    /* The range. */
    inInit(&in, XHCI_MSG4_SYS_NT5);
    in.Pools = 1;
    in.InputFirst = 30;
    in.InputLast = 40;
    in.IsaIrq = 35;
    DECIDE(in, XHCI_POLL_DELIVERY_MESSAGE, XHCI_POLL_R_FORCED_MSG4,
           "a range, an input inside it");
    CHECK_EQ(c.HighFirst, 30, "first passed");
    CHECK_EQ(c.HighLast, 40, "last passed");
    in.IsaIrq = 41;
    DECIDE(in, XHCI_POLL_DELIVERY_NONE, XHCI_POLL_R_MSG4_VALUE_INVALID,
           "an input past the range");
    in.IsaIrq = 0;
    in.InputFirst = 23;
    DECIDE(in, XHCI_POLL_DELIVERY_NONE, XHCI_POLL_R_MSG4_VALUE_INVALID,
           "first below 24");
    in.InputFirst = 41;
    DECIDE(in, XHCI_POLL_DELIVERY_NONE, XHCI_POLL_R_MSG4_VALUE_INVALID,
           "first above last");
    in.InputFirst = 0;
    DECIDE(in, XHCI_POLL_DELIVERY_NONE, XHCI_POLL_R_MSG4_VALUE_INVALID,
           "first 0 with last set");
    in.InputFirst = 24;
    in.InputLast = 256;
    DECIDE(in, XHCI_POLL_DELIVERY_NONE, XHCI_POLL_R_MSG4_VALUE_INVALID,
           "last above 255");
    in.InputLast = 255;
    DECIDE(in, XHCI_POLL_DELIVERY_MESSAGE, XHCI_POLL_R_FORCED_MSG4,
           "24 to 255 written out");

    /* The budget. */
    inInit(&in, XHCI_MSG4_SYS_NT5);
    in.Pools = 2;
    in.Budget = 32;
    DECIDE(in, XHCI_POLL_DELIVERY_MESSAGE, XHCI_POLL_R_FORCED_MSG4,
           "budget 32");
    CHECK_EQ(c.Budget, 32, "budget passed");
    in.Budget = 33;
    DECIDE(in, XHCI_POLL_DELIVERY_NONE, XHCI_POLL_R_MSG4_VALUE_INVALID,
           "budget 33");

    /* Without the upper pool its values are neither read nor passed. */
    inInit(&in, XHCI_MSG4_SYS_NT5);
    in.Pools = 0;
    in.InputFirst = 5;
    in.InputLast = 1;
    in.Budget = 99;
    DECIDE(in, XHCI_POLL_DELIVERY_MESSAGE, XHCI_POLL_R_FORCED_MSG4,
           "ISA pool: range and budget not read");
    CHECK_EQ(c.HighFirst + c.HighLast + c.Budget, 0, "nothing passed");

    /* Not offered (XP x64): the pool values are their defaults. */
    inInit(&in, XHCI_MSG4_SYS_NT5);
    in.PoolsOffered = 0;
    in.Pools = 2;
    in.Budget = 99;
    DECIDE(in, XHCI_POLL_DELIVERY_MESSAGE, XHCI_POLL_R_FORCED_MSG4,
           "XP x64: the pool not read");
    CHECK_EQ(c.LibPools, 1, "XP x64: ISA alone");
    in.IsaIrq = 40;
    DECIDE(in, XHCI_POLL_DELIVERY_NONE, XHCI_POLL_R_MSG4_VALUE_INVALID,
           "XP x64: an upper input forced is outside the ISA pool");

    /* NULL claim is allowed. */
    inInit(&in, XHCI_MSG4_SYS_NT5);
    {
        ULONG r;

        CHECK_EQ(XhciMsg4Decide(&in, NULL, &r), XHCI_POLL_DELIVERY_MESSAGE,
                 "no claim asked for");
    }
}

static void test_share(void)
{
    CHECK_EQ(XhciMsg4ShareValue(XHCI_MSG4_NOT_FOUND), 8,
             "absent: 8, sharing on (the owner, 2026-11-13)");
    CHECK_EQ(XhciMsg4ShareValue(1), 0, "1: one to an IRQ is no sharing");
    CHECK_EQ(XhciMsg4ShareValue(0), 0, "0, not offered, runs as 1");
    CHECK_EQ(XhciMsg4ShareValue(2), 2, "2");
    CHECK_EQ(XhciMsg4ShareValue(5), 5, "5");
    CHECK_EQ(XhciMsg4ShareValue(8), 8, "8");
    CHECK_EQ(XhciMsg4ShareValue(9), 9, "9");
    CHECK_EQ(XhciMsg4ShareValue(16), 16, "16, the cap");
    CHECK_EQ(XhciMsg4ShareValue(17), 16, "17 runs as 16");
    CHECK_EQ(XhciMsg4ShareValue(0xFFFFFFFEUL), 16, "huge runs as 16");
    CHECK_EQ(XhciMsg4MaxMembers(0), 0, "off: 0, the library's default");
    CHECK_EQ(XhciMsg4MaxMembers(6), 6, "the cap");
    CHECK_EQ(XhciMsg4MaxMembers(XhciMsg4ShareValue(XHCI_MSG4_NOT_FOUND)), 8,
             "absent: a cap of 8");
}

static void test_judge(void)
{
    XHCI_MSG4_JUDGE j;
    ULONG i;
    ULONG trips;

    j.Stranded = 0;
    j.Tripped = 0;
    XhciMsg4JudgeReset(&j, 100);

    /* Stranded events: eight inside 600 ticks trip, whatever was claimed. */
    trips = 0;
    for (i = 0; i < 7; i++) {
        trips += XhciMsg4JudgeStranded(&j, 1000 + i * 80);
    }
    CHECK_EQ(trips, 0, "seven: no trip");
    CHECK_EQ(j.Stranded, 7, "counted");
    CHECK_EQ(XhciMsg4JudgeStranded(&j, 1000 + 7 * 80 + 41), 0,
             "the eighth 601 ticks after the first: no trip");
    CHECK_EQ(XhciMsg4JudgeStranded(&j, 1000 + 8 * 80), 1,
             "the ninth: eight within 600 of the second");
    CHECK_EQ(j.Tripped, 1, "latched");
    CHECK_EQ(XhciMsg4JudgeStranded(&j, 1700), 0, "once only");
    CHECK_EQ(j.Stranded, 10, "still counted");

    /* Across the clock's wrap. */
    j.Stranded = 0;
    j.Tripped = 0;
    XhciMsg4JudgeReset(&j, 0);
    trips = 0;
    for (i = 0; i < 8; i++) {
        trips += XhciMsg4JudgeStranded(&j, 0xFFFFFFF0UL + i * 10);
    }
    CHECK_EQ(trips, 1, "eight across the wrap: a trip");

    /* A reset starts the window afresh. */
    j.Stranded = 0;
    j.Tripped = 0;
    XhciMsg4JudgeReset(&j, 0);
    for (i = 0; i < 7; i++) {
        (VOID)XhciMsg4JudgeStranded(&j, 10 + i);
    }
    XhciMsg4JudgeReset(&j, 0);
    CHECK_EQ(XhciMsg4JudgeStranded(&j, 20), 0, "after a reset: one");
    CHECK_EQ(j.Stranded, 8, "the lifetime's count kept");

    /* The run of unclaimed drains with events. */
    j.Stranded = 0;
    j.Tripped = 0;
    XhciMsg4JudgeReset(&j, 50);
    for (i = 0; i < 7; i++) {
        XhciMsg4JudgeDrain(&j, 1, 50, 200);
    }
    CHECK_EQ(j.DeadlineValid, 0, "seven drains: no deadline");
    XhciMsg4JudgeDrain(&j, 0, 50, 200);
    CHECK_EQ(j.Run, 7, "a drain that took nothing judges nothing");
    XhciMsg4JudgeDrain(&j, 3, 50, 200);
    CHECK_EQ(j.DeadlineValid, 1, "the eighth fixes a deadline");
    CHECK_EQ(j.Deadline, 201, "one tick ahead");
    XhciMsg4JudgeDrain(&j, 1, 50, 205);
    CHECK_EQ(j.Deadline, 201, "later drains do not move it");
    CHECK_EQ(XhciMsg4JudgeTick(&j, 50, 200), 0, "before the deadline");
    CHECK_EQ(XhciMsg4JudgeTick(&j, 50, 201), 1, "the deadline: a trip");
    CHECK_EQ(XhciMsg4JudgeTick(&j, 50, 202), 0, "once only");

    /* A claimed entry ends the run and the deadline. */
    j.Stranded = 0;
    j.Tripped = 0;
    XhciMsg4JudgeReset(&j, 50);
    for (i = 0; i < 8; i++) {
        XhciMsg4JudgeDrain(&j, 1, 50, 300);
    }
    CHECK_EQ(j.DeadlineValid, 1, "deadline fixed");
    CHECK_EQ(XhciMsg4JudgeTick(&j, 51, 300), 0, "a claim before it");
    CHECK_EQ(j.DeadlineValid, 0, "the deadline cancelled");
    CHECK_EQ(j.Run, 0, "the run ended");
    CHECK_EQ(XhciMsg4JudgeTick(&j, 51, 400), 0, "no trip later");
    for (i = 0; i < 7; i++) {
        XhciMsg4JudgeDrain(&j, 1, 51, 500);
    }
    XhciMsg4JudgeDrain(&j, 1, 52, 500);
    CHECK_EQ(j.Run, 0, "a moved count in a drain ends the run");
    CHECK_EQ(j.DeadlineValid, 0, "and fixes nothing");

    /* Across the clock's wrap. */
    j.Stranded = 0;
    j.Tripped = 0;
    XhciMsg4JudgeReset(&j, 0);
    for (i = 0; i < 8; i++) {
        XhciMsg4JudgeDrain(&j, 1, 0, 0xFFFFFFFFUL);
    }
    CHECK_EQ(j.Deadline, 0, "deadline wraps to 0");
    CHECK_EQ(XhciMsg4JudgeTick(&j, 0, 0xFFFFFFFFUL), 0, "not yet");
    CHECK_EQ(XhciMsg4JudgeTick(&j, 0, 0), 1, "reached across the wrap");
}

static void test_pack(void)
{
    UCHAR r[16];
    ULONG w[4];
    ULONG i;

    for (i = 0; i < 16; i++) {
        r[i] = (UCHAR)(i + 1);
    }
    XhciMsg4PackReasons(r, w);
    CHECK_EQ(w[0], 0x04030201UL, "IRQs 0 to 3");
    CHECK_EQ(w[2], 0x0C0B0A09UL, "IRQs 8 to 11");
    CHECK_EQ(w[3], 0x100F0E0DUL, "IRQs 12 to 15");
}

/* Task 50.4 (record 15 section 4.5.20): which start is a mode-4 refusal,
 * the record's layout refused and cleared, and the reader's state. */
static void test_record(void)
{
    static const ULONG refusals[10] = { 22, 23, 24, 25, 26, 27, 30, 31,
                                        33, 34 };
    ULONG poll[XHCI_M4REC_POLL_WORDS];
    ULONG msg4[XHCI_M4REC_MSG4_WORDS];
    ULONG w[XHCI_M4REC_WORDS];
    UCHAR b[XHCI_M4REC_BYTES + 8];
    XHCI_M4REC_IN in;
    ULONG i;
    ULONG n;

    for (i = 0; i < 10; i++) {
        CHECK_EQ(XhciMsg4IsStartRefusal(1, XHCI_POLL_DELIVERY_NONE,
                                        refusals[i]), 1,
                 "every mode-4 refusal reason");
        CHECK_EQ(XhciMsg4IsStartRefusal(0, XHCI_POLL_DELIVERY_NONE,
                                        refusals[i]), 0,
                 "not when the start succeeded");
        CHECK_EQ(XhciMsg4IsStartRefusal(1, XHCI_POLL_DELIVERY_MSIX,
                                        refusals[i]), 0,
                 "not while a delivery stands");
    }
    n = 0;
    for (i = 0; i < 64; i++) {
        n += XhciMsg4IsStartRefusal(1, XHCI_POLL_DELIVERY_NONE, i);
    }
    CHECK_EQ(n, 10, "no other reason, 21 and the reserved 28, 29, 32 included");
    CHECK_EQ(XhciMsg4IsStartRefusal(1, XHCI_POLL_DELIVERY_NONE,
                                    XHCI_POLL_R_FORCED_MESSAGE_NOT_CONNECTED),
             0, "mode 3's refusal is not mode 4's");
    CHECK_EQ(XhciMsg4IsStartRefusal(1, XHCI_POLL_DELIVERY_NONE,
                                    XHCI_POLL_R_NONE),
             0, "a start failed before the delivery was settled");

    for (i = 0; i < XHCI_M4REC_POLL_WORDS; i++) {
        poll[i] = 0x100UL + i;
    }
    for (i = 0; i < XHCI_M4REC_MSG4_WORDS; i++) {
        msg4[i] = 0x200UL + i;
    }
    in.State = XHCI_M4REC_REFUSED;
    in.TimeLo = 0x11223344UL;
    in.TimeHi = 0x01DC5566UL;
    in.Status = 0xC0000182UL;
    in.System = XHCI_MSG4_SYS_NT5;
    in.Flavour = 1;
    in.DriverVersion = 0x03050100UL;
    in.Location = 0x80000000UL | XhciMsg4Bdf(0, 4, 0);
    in.Poll = poll;
    in.ConnectStatus = 4;
    in.Msg4 = msg4;
    XhciMsg4RecordBuild(&in, w);
    CHECK_EQ(w[0], XHCI_M4REC_MAGIC, "the magic");
    CHECK_EQ(w[1], 0x00370001UL, "version 1, 55 words");
    CHECK_EQ(w[2], XHCI_M4REC_REFUSED, "refused");
    CHECK_EQ(w[3], 0x11223344UL, "time low");
    CHECK_EQ(w[4], 0x01DC5566UL, "time high");
    CHECK_EQ(w[5], 0xC0000182UL, "the start's status");
    CHECK_EQ(w[6], 0x0101UL, "the system and the flavour");
    CHECK_EQ(w[7], 0x03050100UL, "the driver's version");
    CHECK_EQ(w[8], 0x80000020UL, "the location");
    CHECK_EQ(w[9], 0, "reserved");
    CHECK_EQ(w[10], 0x100UL, "IntrModeRead first");
    CHECK_EQ(w[15], 0x105UL, "IntrPin last");
    CHECK_EQ(w[16], 4, "the connection's status");
    CHECK_EQ(w[17], 0x200UL, "Msg4Strategy first");
    CHECK_EQ(w[54], 0x225UL, "Msg4HighFirstRefused last");

    for (i = 0; i < XHCI_M4REC_WORDS; i++) {
        put32(b, i * 4, w[i]);
    }
    CHECK_EQ(XhciMsg4RecordState(1, 1, b, XHCI_M4REC_BYTES),
             XHCI_M4REC_IS_REFUSED, "a refused record reads refused");
    CHECK_EQ(XhciMsg4RecordState(1, 1, b, XHCI_M4REC_BYTES + 8),
             XHCI_M4REC_IS_REFUSED, "a longer value too");
    CHECK_EQ(XhciMsg4RecordState(1, 1, b, XHCI_M4REC_BYTES - 1),
             XHCI_M4REC_FOREIGN, "a shorter value is foreign");
    CHECK_EQ(XhciMsg4RecordState(1, 0, b, XHCI_M4REC_BYTES),
             XHCI_M4REC_FOREIGN, "another type is foreign");
    CHECK_EQ(XhciMsg4RecordState(1, 1, NULL, 0), XHCI_M4REC_FOREIGN,
             "an empty value is foreign");
    CHECK_EQ(XhciMsg4RecordState(0, 1, b, XHCI_M4REC_BYTES),
             XHCI_M4REC_ABSENT, "absent");
    put32(b, 8, 7);
    CHECK_EQ(XhciMsg4RecordState(1, 1, b, XHCI_M4REC_BYTES),
             XHCI_M4REC_FOREIGN, "an unknown state is foreign");
    put32(b, 8, 1);
    put32(b, 4, 0x00370002UL);
    CHECK_EQ(XhciMsg4RecordState(1, 1, b, XHCI_M4REC_BYTES),
             XHCI_M4REC_FOREIGN, "another version is foreign");
    put32(b, 4, 0x00370001UL);
    put32(b, 0, 0x46523459UL);
    CHECK_EQ(XhciMsg4RecordState(1, 1, b, XHCI_M4REC_BYTES),
             XHCI_M4REC_FOREIGN, "another magic is foreign");

    in.State = XHCI_M4REC_CLEARED;
    XhciMsg4RecordBuild(&in, w);
    CHECK_EQ(w[0], XHCI_M4REC_MAGIC, "cleared: the magic kept");
    CHECK_EQ(w[1], 0x00370001UL, "cleared: version and count kept");
    CHECK_EQ(w[2], XHCI_M4REC_CLEARED, "cleared");
    CHECK_EQ(w[3], 0x11223344UL, "cleared: the time kept");
    CHECK_EQ(w[6], 0x0101UL, "cleared: the system and flavour kept");
    CHECK_EQ(w[7], 0x03050100UL, "cleared: the version kept");
    n = w[5] | w[8];
    for (i = XHCI_M4REC_AT_POLL; i < XHCI_M4REC_WORDS; i++) {
        n |= w[i];
    }
    CHECK_EQ(n, 0, "cleared: the status, location and payload 0");
    for (i = 0; i < XHCI_M4REC_WORDS; i++) {
        put32(b, i * 4, w[i]);
    }
    CHECK_EQ(XhciMsg4RecordState(1, 1, b, XHCI_M4REC_BYTES),
             XHCI_M4REC_IS_CLEARED, "a cleared record reads cleared");

    in.State = 5;
    in.Poll = NULL;
    in.Msg4 = NULL;
    XhciMsg4RecordBuild(&in, w);
    CHECK_EQ(w[2], XHCI_M4REC_CLEARED, "an unknown state builds cleared");
    in.State = XHCI_M4REC_REFUSED;
    XhciMsg4RecordBuild(&in, w);
    CHECK_EQ(w[10] | w[17] | w[54], 0, "no counter words given: 0");
}

int main(void)
{
    test_scan(16);
    test_scan(20);
    test_names();
    test_refusals();
    test_stray();
    test_decide();
    test_share();
    test_judge();
    test_pack();
    test_record();
    printf("test_msg4: %d checks, %d failures\n", checks, failures);
    return failures;
}
