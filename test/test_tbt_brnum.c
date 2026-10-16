/*
 * test_tbt_brnum.c - host tests for src\cm\tbt_brnum.c (design record 21
 * section 6.1.1; XhciCmBridgeNumbering).
 *
 * Driven over a modelled hierarchy whose configuration cycles route the
 * way bridges route them: a function below the tunnel port answers at
 * (bus, dev, fn) only while its parent bridge's secondary bus is that bus
 * and every bridge above it holds the bus in its secondary..subordinate
 * range. A bridge in reset state (buses 0) therefore hides everything
 * behind it, as on the hardware.
 *
 * The layouts:
 *   - the owner's ThinkPad P14s Gen 1 tunnel port 04:01.0 (secondary 06,
 *     subordinate 2A, as its BIOS programs it: usb4-emu
 *     tests/captures/p14s-gen1/win98/OFF_OFF/PCIDUMP.TXT, tbt_topo_p14s.h)
 *     with the ORICO enclosure's ASM2464 switch (upstream port, one
 *     downstream port, the NVMe), which the BIOS numbers 06/07/08 and
 *     07/08/08 when the enclosure is present at POST (usb4-emu
 *     tests/captures/p14s-gen1/win98-orico/A0P1_OR/PCIDUMP.TXT);
 *   - that switch already numbered (present at boot): nothing written;
 *   - the emulator's i440FX endpoint alone (no bridge): nothing written;
 *   - a three-port dock switch, a multi-function bridge, an exact and a
 *     too-small range, a chain too deep, too many bridges, an
 *     inconsistent chain, failed writes and reads, a write that does not
 *     hold, no writer, the port's own range unusable, a function answering
 *     Configuration Request Retry Status, the go callback refusing midway,
 *     and a bridge replaced before its undo.
 *
 * C89.
 */

#include <stdio.h>
#include "../src/cm/tbt_brnum.h"
#include "test_harness.h"

#define NODE_MAX 40
#define ROOT     0xFFFFFFFFUL

typedef struct _NODE {
    ULONG Parent;           /* index of the parent bridge, ROOT for the port */
    ULONG FixedBus;         /* the port's own bus (Parent == ROOT)           */
    ULONG Dev;
    ULONG Fn;
    ULONG Id;
    ULONG Header;           /* the 0Ch dword                                 */
    ULONG Buses;            /* 18h, bridges only                             */
    ULONG Bridge;
} NODE;

typedef struct _MODEL {
    NODE N[NODE_MAX];
    ULONG Count;
    ULONG Writes;
    ULONG FailWriteAt;      /* the 1-based write that fails, 0 none          */
    ULONG IgnoreWrites;     /* writes accepted but not held                  */
    ULONG FailReadBdf;      /* reads of this function fail                   */
    ULONG StrayWrites;      /* writes not at 18h or not dword                */
} MODEL;

static MODEL m;

static VOID mreset(VOID)
{
    ULONG i;
    PUCHAR p = (PUCHAR)&m;

    for (i = 0; i < sizeof(m); i++) {
        p[i] = 0;
    }
    m.FailReadBdf = XHCI_TBT_NO_BDF;
}

static ULONG madd(ULONG parent, ULONG dev, ULONG fn, ULONG id, ULONG bridge,
                  ULONG mf)
{
    NODE *n = &m.N[m.Count];

    n->Parent = parent;
    n->FixedBus = 0;
    n->Dev = dev;
    n->Fn = fn;
    n->Id = id;
    n->Bridge = bridge;
    n->Header = ((bridge ? 1UL : 0UL) | (mf ? 0x80UL : 0UL)) << 16;
    n->Buses = 0;
    return m.Count++;
}

/* The bus a node answers on now, or ROOT where it is unreachable. */
static ULONG mbus(ULONG i)
{
    ULONG bus;
    ULONG a;
    ULONG sec;
    ULONG sub;

    if (m.N[i].Parent == ROOT) {
        return m.N[i].FixedBus;
    }
    bus = (m.N[m.N[i].Parent].Buses >> 8) & 0xFFUL;
    if (bus == 0) {
        return ROOT;
    }
    for (a = m.N[i].Parent; a != ROOT; a = m.N[a].Parent) {
        sec = (m.N[a].Buses >> 8) & 0xFFUL;
        sub = (m.N[a].Buses >> 16) & 0xFFUL;
        if (bus < sec || bus > sub) {
            return ROOT;
        }
        if (m.N[a].Parent != ROOT && mbus(a) == ROOT) {
            return ROOT;
        }
    }
    return bus;
}

static ULONG mfind(ULONG bdf)
{
    ULONG i;

    for (i = 0; i < m.Count; i++) {
        if (m.N[i].Dev == XHCI_TBT_BDF_DEV(bdf) &&
            m.N[i].Fn == XHCI_TBT_BDF_FN(bdf) &&
            mbus(i) == XHCI_TBT_BDF_BUS(bdf)) {
            return i;
        }
    }
    return ROOT;
}

static ULONG mread(PVOID context, ULONG bdf, ULONG offset, PULONG value)
{
    ULONG i;

    (void)context;
    if (bdf == m.FailReadBdf) {
        return 0;
    }
    i = mfind(bdf);
    if (i == ROOT) {
        *value = 0xFFFFFFFFUL;
        return 1;
    }
    switch (offset) {
    case XHCI_TBT_CFG_ID:
        *value = m.N[i].Id;
        break;
    case XHCI_TBT_CFG_HEADER:
        *value = m.N[i].Header;
        break;
    case XHCI_TBT_CFG_BUSES:
        *value = m.N[i].Bridge ? m.N[i].Buses : 0;
        break;
    default:
        *value = 0;
        break;
    }
    return 1;
}

static ULONG mwrite(PVOID context, ULONG bdf, ULONG offset, ULONG width,
                    ULONG value)
{
    ULONG i;

    (void)context;
    m.Writes++;
    if (m.FailWriteAt != 0 && m.Writes == m.FailWriteAt) {
        return 0;
    }
    if (offset != XHCI_TBT_CFG_BUSES || width != 4) {
        m.StrayWrites++;
        return 1;
    }
    i = mfind(bdf);
    if (i == ROOT || !m.N[i].Bridge) {
        m.StrayWrites++;
        return 1;
    }
    if (!m.IgnoreWrites) {
        m.N[i].Buses = value;
    }
    return 1;
}

static XHCI_TBT_CFG io;

static VOID ioreset(BOOLEAN writer)
{
    io.Read = mread;
    io.Write = writer ? mwrite : NULL;
    io.Context = NULL;
    io.Reads = 0;
    io.Writes = 0;
}

#define PORT     XHCI_TBT_BDF(0x04, 1, 0)
#define ID_PORT  0x15C08086UL
#define ID_ASM   0x24631B21UL
#define ID_NVME  0x174A1C5CUL
#define ID_EMU   0x00101B36UL

/* The tunnel port with its bus range, the latency byte 00h. */
static ULONG mport(ULONG secondary, ULONG subordinate)
{
    ULONG p = madd(ROOT, 1, 0, ID_PORT, 1, 0);

    m.N[p].FixedBus = 0x04;
    m.N[p].Buses = (subordinate << 16) | (secondary << 8) | 0x04;
    return p;
}

/* The ASM2464: upstream port, one downstream port, the NVMe. */
static VOID masm(ULONG port, PULONG usp, PULONG dsp, PULONG nvme)
{
    *usp = madd(port, 0, 0, ID_ASM, 1, 0);
    *dsp = madd(*usp, 0, 0, ID_ASM, 1, 0);
    *nvme = madd(*dsp, 0, 0, ID_NVME, 0, 0);
}

static VOID test_p14s_orico(VOID)
{
    XHCI_TBT_BRNUM r;
    ULONG port, usp, dsp, nvme;

    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    masm(port, &usp, &dsp, &nvme);
    CHECK_EQ(mbus(nvme), ROOT, "the NVMe is unreachable in reset state");
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, NULL, NULL, &r), XHCI_TBT_BRNUM_DONE,
             "the reset chain is numbered");
    CHECK_EQ(m.N[usp].Buses, 0x00080706UL, "upstream port 06/07/08, as the BIOS");
    CHECK_EQ(m.N[dsp].Buses, 0x00080807UL, "downstream port 07/08/08, as the BIOS");
    CHECK_EQ(mbus(nvme), 0x08, "the NVMe answers on bus 08");
    CHECK_EQ(r.Numbered, 2, "two bridges numbered");
    CHECK_EQ(r.Bridges, 2, "two bridges met");
    CHECK_EQ(r.Last, 0x08, "the last bus given");
    CHECK_EQ(r.PortBuses, 0x002A0604UL, "the port's range as read");
    CHECK_EQ(r.Undone, 0, "nothing undone");
    CHECK_EQ(r.Bdf[0], XHCI_TBT_BDF(0x06, 0, 0), "first recorded: the upstream port");
    CHECK_EQ(r.Bdf[1], XHCI_TBT_BDF(0x07, 0, 0), "then the downstream port");
    CHECK_EQ(r.Was[0], 0, "as read: reset state");
    CHECK_EQ(r.Now[1], 0x00080807UL, "as left");
    CHECK_EQ(m.N[port].Buses, 0x002A0604UL, "the port itself is not written");
    CHECK_EQ(m.StrayWrites, 0, "no write but the bus numbers");
    CHECK_EQ(r.Writes, 4, "two writes a bridge (open, then narrowed)");
    CHECK_EQ(r.Where, XHCI_TBT_NO_BDF, "no refusal");

    /* Again: the chain now reads numbered, nothing is written. */
    m.Writes = 0;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, NULL, NULL, &r), XHCI_TBT_BRNUM_NUMBERED,
             "a second call finds it numbered");
    CHECK_EQ(m.Writes, 0, "and writes nothing");
    CHECK_EQ(r.Where, XHCI_TBT_BDF(0x06, 0, 0), "naming the upstream port");
}

static VOID test_boot_present(VOID)
{
    XHCI_TBT_BRNUM r;
    ULONG port, usp, dsp, nvme;

    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    masm(port, &usp, &dsp, &nvme);
    m.N[usp].Buses = 0x00080706UL;
    m.N[dsp].Buses = 0x00080807UL;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, NULL, NULL, &r), XHCI_TBT_BRNUM_NUMBERED,
             "the BIOS's numbering is left alone");
    CHECK_EQ(m.Writes, 0, "nothing written");
    CHECK_EQ(m.N[usp].Buses, 0x00080706UL, "unchanged");

    /* Only the secondary set (a half-programmed bridge) still counts. */
    m.N[usp].Buses = 0x00000700UL;
    m.N[dsp].Buses = 0;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, NULL, NULL, &r), XHCI_TBT_BRNUM_NUMBERED,
             "a secondary alone counts as numbered");
    m.N[usp].Buses = 0x00080000UL;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, NULL, NULL, &r), XHCI_TBT_BRNUM_NUMBERED,
             "a subordinate alone counts as numbered");
    m.N[usp].Buses = 0x00000006UL;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, NULL, NULL, &r), XHCI_TBT_BRNUM_DONE,
             "a primary alone is reset state (secondary and subordinate 0)");
    CHECK_EQ(m.N[usp].Buses, 0x00080706UL, "and is numbered");
}

static VOID test_endpoint_only(VOID)
{
    XHCI_TBT_BRNUM r;
    ULONG port;

    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    (VOID)madd(port, 1, 0, ID_EMU, 0, 0);
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, NULL, NULL, &r), XHCI_TBT_BRNUM_NONE,
             "an endpoint alone: nothing to number");
    CHECK_EQ(m.Writes, 0, "nothing written");

    mreset();
    ioreset(TRUE);
    (VOID)mport(0x06, 0x2A);
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, NULL, NULL, &r), XHCI_TBT_BRNUM_NONE,
             "an empty bus: nothing to number");
    CHECK_EQ(m.Writes, 0, "nothing written");
}

static VOID test_latency_kept(VOID)
{
    XHCI_TBT_BRNUM r;
    ULONG port, usp, dsp, nvme;

    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    masm(port, &usp, &dsp, &nvme);
    m.N[usp].Buses = 0x40000000UL;
    m.N[dsp].Buses = 0x20000000UL;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, NULL, NULL, &r), XHCI_TBT_BRNUM_DONE,
             "numbered with a latency timer set");
    CHECK_EQ(m.N[usp].Buses, 0x40080706UL, "the upstream port's latency kept");
    CHECK_EQ(m.N[dsp].Buses, 0x20080807UL, "the downstream port's latency kept");
}

static VOID test_dock(VOID)
{
    XHCI_TBT_BRNUM r;
    ULONG port, usp, d0, d1, d2, e0, e1, e2;

    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    usp = madd(port, 0, 0, 0x15D38086UL, 1, 0);
    d0 = madd(usp, 0, 0, 0x15D38086UL, 1, 0);
    d1 = madd(usp, 1, 0, 0x15D38086UL, 1, 0);
    d2 = madd(usp, 4, 0, 0x15D38086UL, 1, 0);
    e0 = madd(d0, 0, 0, 0x11001B73UL, 0, 0);
    e1 = madd(d1, 0, 0, 0x12421B21UL, 0, 0);
    e2 = madd(d2, 0, 0, 0x15338086UL, 0, 0);
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, NULL, NULL, &r), XHCI_TBT_BRNUM_DONE,
             "a dock switch is numbered");
    CHECK_EQ(m.N[usp].Buses, 0x000A0706UL, "upstream port 06/07/0A");
    CHECK_EQ(m.N[d0].Buses, 0x00080807UL, "first downstream port 07/08/08");
    CHECK_EQ(m.N[d1].Buses, 0x00090907UL, "second 07/09/09");
    CHECK_EQ(m.N[d2].Buses, 0x000A0A07UL, "third 07/0A/0A");
    CHECK_EQ(mbus(e0), 0x08, "its endpoints reachable: 08");
    CHECK_EQ(mbus(e1), 0x09, "09");
    CHECK_EQ(mbus(e2), 0x0A, "0A");
    CHECK_EQ(r.Numbered, 4, "four bridges");
    CHECK_EQ(r.Last, 0x0A, "last bus 0A");
}

static VOID test_multifunction(VOID)
{
    XHCI_TBT_BRNUM r;
    ULONG port, f0, f1, e;

    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    f0 = madd(port, 0, 0, ID_NVME, 0, 1);
    f1 = madd(port, 0, 1, ID_ASM, 1, 0);
    e = madd(f1, 0, 0, ID_NVME, 0, 0);
    (void)f0;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, NULL, NULL, &r), XHCI_TBT_BRNUM_DONE,
             "a bridge at function 1 of a multi-function device");
    CHECK_EQ(m.N[f1].Buses, 0x00070706UL, "06/07/07");
    CHECK_EQ(mbus(e), 0x07, "its endpoint on 07");

    /* Function 1 of a single-function device is not looked at. */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    f0 = madd(port, 0, 0, ID_NVME, 0, 0);
    f1 = madd(port, 0, 1, ID_ASM, 1, 0);
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, NULL, NULL, &r), XHCI_TBT_BRNUM_NONE,
             "fn 1 of a single-function device is not scanned");
    CHECK_EQ(m.Writes, 0, "nothing written");
}

static VOID test_ranges(VOID)
{
    XHCI_TBT_BRNUM r;
    ULONG port, usp, dsp, nvme;

    /* Exactly the buses the chain needs. */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x08);
    masm(port, &usp, &dsp, &nvme);
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, NULL, NULL, &r), XHCI_TBT_BRNUM_DONE,
             "an exact range fits");
    CHECK_EQ(m.N[dsp].Buses, 0x00080807UL, "07/08/08");

    /* One bus short: refused, everything written back. */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x07);
    masm(port, &usp, &dsp, &nvme);
    m.N[usp].Buses = 0x11000000UL;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, NULL, NULL, &r), XHCI_TBT_BRNUM_NO_ROOM,
             "a range one bus short is refused");
    CHECK_EQ(m.N[usp].Buses, 0x11000000UL, "the upstream port written back as read");
    CHECK_EQ(m.N[dsp].Buses, 0, "the downstream port untouched");
    CHECK_EQ(r.Undone, 1, "one bridge undone");
    CHECK_EQ(r.Numbered, 0, "none left numbered");
    CHECK_EQ(r.Where, XHCI_TBT_BDF(0x07, 0, 0), "stopped at the downstream port");

    /* No bus at all below the port's secondary. */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x06);
    masm(port, &usp, &dsp, &nvme);
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, NULL, NULL, &r), XHCI_TBT_BRNUM_NO_ROOM,
             "no bus to give");
    CHECK_EQ(m.Writes, 0, "nothing written");
    CHECK_EQ(m.N[usp].Buses, 0, "untouched");

    /* The port's own range unusable. */
    mreset();
    ioreset(TRUE);
    port = mport(0x00, 0x00);
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, NULL, NULL, &r), XHCI_TBT_BRNUM_PORT,
             "a port with no secondary bus");
    mreset();
    ioreset(TRUE);
    port = mport(0x04, 0x2A);
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, NULL, NULL, &r), XHCI_TBT_BRNUM_PORT,
             "a port whose secondary is its own bus");
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x05);
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, NULL, NULL, &r), XHCI_TBT_BRNUM_PORT,
             "a port whose subordinate is below its secondary");
    mreset();
    ioreset(TRUE);
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, NULL, NULL, &r), XHCI_TBT_BRNUM_PORT,
             "no port at all (all-ones)");
    CHECK_EQ(m.Writes, 0, "nothing written in any of them");
}

static VOID test_bounds(VOID)
{
    XHCI_TBT_BRNUM r;
    ULONG port, b, k, nodes[10];

    /* A chain of MAX_DEPTH bridges fits; one more is refused. */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    b = port;
    for (k = 0; k < XHCI_TBT_BRNUM_MAX_DEPTH; k++) {
        b = madd(b, 0, 0, ID_ASM, 1, 0);
        nodes[k] = b;
    }
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, NULL, NULL, &r), XHCI_TBT_BRNUM_DONE,
             "a chain of MAX_DEPTH bridges fits");
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    b = port;
    for (k = 0; k < XHCI_TBT_BRNUM_MAX_DEPTH + 1; k++) {
        b = madd(b, 0, 0, ID_ASM, 1, 0);
        nodes[k] = b;
    }
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, NULL, NULL, &r), XHCI_TBT_BRNUM_TOO_DEEP,
             "one deeper is refused");
    for (k = 0; k < XHCI_TBT_BRNUM_MAX_DEPTH + 1; k++) {
        CHECK_EQ(m.N[nodes[k]].Buses, 0, "every bridge written back");
    }
    CHECK_EQ(r.Undone, XHCI_TBT_BRNUM_MAX_DEPTH, "each numbered one undone");

    /* More bridges than MAX_BRIDGES on one bus. */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    for (k = 0; k < XHCI_TBT_BRNUM_MAX_BRIDGES + 1; k++) {
        (VOID)madd(port, k, 0, ID_ASM, 1, 0);
    }
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, NULL, NULL, &r), XHCI_TBT_BRNUM_TOO_MANY,
             "MAX_BRIDGES + 1 bridges are refused");
    CHECK_EQ(r.Undone, XHCI_TBT_BRNUM_MAX_BRIDGES, "all undone");
    for (k = 1; k < m.Count; k++) {
        CHECK_EQ(m.N[k].Buses, 0, "every one back in reset state");
    }
}

static VOID test_inconsistent(VOID)
{
    XHCI_TBT_BRNUM r;
    ULONG port, usp, dsp, nvme;

    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    masm(port, &usp, &dsp, &nvme);
    m.N[dsp].Buses = 0x00090908UL;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, NULL, NULL, &r), XHCI_TBT_BRNUM_INCONSISTENT,
             "a numbered bridge below one in reset state");
    CHECK_EQ(m.N[usp].Buses, 0, "the upstream port written back");
    CHECK_EQ(m.N[dsp].Buses, 0x00090908UL, "the numbered one untouched");
}

static VOID test_failures(VOID)
{
    XHCI_TBT_BRNUM r;
    ULONG port, usp, dsp, nvme;

    /* No writer: nothing read, nothing written. */
    mreset();
    ioreset(FALSE);
    port = mport(0x06, 0x2A);
    masm(port, &usp, &dsp, &nvme);
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, NULL, NULL, &r), XHCI_TBT_BRNUM_NO_WRITER,
             "no writer");
    CHECK_EQ(io.Reads, 0, "nothing read");

    /* The second write (the downstream port's opening) fails. */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    masm(port, &usp, &dsp, &nvme);
    m.FailWriteAt = 2;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, NULL, NULL, &r), XHCI_TBT_BRNUM_ACCESS,
             "a failed write");
    CHECK_EQ(m.N[usp].Buses, 0, "the upstream port written back");
    CHECK_EQ(m.N[dsp].Buses, 0, "the downstream port as it was");
    CHECK_EQ(r.Where, XHCI_TBT_BDF(0x07, 0, 0), "at the downstream port");

    /* The narrowing write of the upstream port fails (the fourth). */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    masm(port, &usp, &dsp, &nvme);
    m.FailWriteAt = 4;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, NULL, NULL, &r), XHCI_TBT_BRNUM_ACCESS,
             "a failed narrowing");
    CHECK_EQ(m.N[usp].Buses, 0, "the upstream port written back");
    CHECK_EQ(m.N[dsp].Buses, 0, "the downstream port written back");
    CHECK_EQ(r.Undone, 2, "both undone");

    /* A write that does not hold (read back differs). */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    masm(port, &usp, &dsp, &nvme);
    m.IgnoreWrites = 1;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, NULL, NULL, &r), XHCI_TBT_BRNUM_ACCESS,
             "a write that does not hold");
    CHECK_EQ(r.Where, XHCI_TBT_BDF(0x06, 0, 0), "at the upstream port");

    /* A read failing in the survey: nothing written. */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    masm(port, &usp, &dsp, &nvme);
    m.FailReadBdf = XHCI_TBT_BDF(0x06, 0, 0);
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, NULL, NULL, &r), XHCI_TBT_BRNUM_ACCESS,
             "a failed read in the survey");
    CHECK_EQ(m.Writes, 0, "nothing written");

    /* A read failing below the first bridge: undone. */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    masm(port, &usp, &dsp, &nvme);
    m.FailReadBdf = XHCI_TBT_BDF(0x07, 0, 0);
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, NULL, NULL, &r), XHCI_TBT_BRNUM_ACCESS,
             "a failed read below");
    CHECK_EQ(m.N[usp].Buses, 0, "the upstream port written back");

    /* The port unreadable. */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    m.FailReadBdf = PORT;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, NULL, NULL, &r), XHCI_TBT_BRNUM_PORT,
             "the port unreadable");
}

/* The go callback: refuses from its Nth call on; at that call it may also
 * replace a node's identity (a different bridge in its place). */
static ULONG goCalls;
static ULONG goRefuseAt;
static ULONG goReplace;

static ULONG gocb(PVOID context)
{
    (void)context;
    goCalls++;
    if (goRefuseAt != 0 && goCalls >= goRefuseAt) {
        if (goReplace != ROOT) {
            m.N[goReplace].Id = 0x12341234UL;
        }
        return 0;
    }
    return 1;
}

static VOID test_go_and_crs(VOID)
{
    XHCI_TBT_BRNUM r;
    ULONG port, usp, dsp, nvme;

    /* The go callback allows every write: as with none. */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    masm(port, &usp, &dsp, &nvme);
    goCalls = 0;
    goRefuseAt = 0;
    goReplace = ROOT;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, gocb, NULL, &r), XHCI_TBT_BRNUM_DONE,
             "numbered with a go callback");
    CHECK_EQ(goCalls, 4, "asked before each of the four writes");
    CHECK_EQ(r.Functions, 1, "one function on the port's secondary bus");
    CHECK_EQ(r.Id[0], ID_ASM, "the identity journalled");

    /* Refused before the downstream port's opening: stale, undone. */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    masm(port, &usp, &dsp, &nvme);
    goCalls = 0;
    goRefuseAt = 2;
    goReplace = ROOT;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, gocb, NULL, &r), XHCI_TBT_BRNUM_STALE,
             "a request gone stale midway");
    CHECK_EQ(m.N[usp].Buses, 0, "the upstream port written back");
    CHECK_EQ(m.N[dsp].Buses, 0, "the downstream port never written");
    CHECK_EQ(r.Undone, 1, "one undone");
    CHECK_EQ(r.UndoFailed, 0, "none failed");

    /* Refused, and the upstream port replaced by another function before
     * the undo: it is not written. */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    masm(port, &usp, &dsp, &nvme);
    goCalls = 0;
    goRefuseAt = 2;
    goReplace = usp;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, gocb, NULL, &r), XHCI_TBT_BRNUM_STALE,
             "stale with the bridge replaced");
    CHECK_EQ(r.UndoFailed, 1, "the replaced bridge is not written back");
    CHECK_EQ(r.Undone, 0, "nothing undone");
    CHECK_EQ(m.N[usp].Buses, 0x002A0706UL, "left as numbered (no write to a changed function)");
    goRefuseAt = 0;
    goReplace = ROOT;

    /* A function answering Configuration Request Retry Status. */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    masm(port, &usp, &dsp, &nvme);
    m.N[usp].Id = 0xFFFF0001UL;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, NULL, NULL, &r), XHCI_TBT_BRNUM_NOT_READY,
             "the upstream port not ready in the survey");
    CHECK_EQ(m.Writes, 0, "nothing written");
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    masm(port, &usp, &dsp, &nvme);
    m.N[dsp].Id = 0xFFFF0001UL;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, NULL, NULL, &r), XHCI_TBT_BRNUM_NOT_READY,
             "the downstream port not ready below");
    CHECK_EQ(m.N[usp].Buses, 0, "the upstream port written back");
    CHECK_EQ(r.Where, XHCI_TBT_BDF(0x07, 0, 0), "at the downstream port");

    /* A write that lands but does not read back as written is undone. */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    masm(port, &usp, &dsp, &nvme);
    m.IgnoreWrites = 1;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, NULL, NULL, &r), XHCI_TBT_BRNUM_ACCESS,
             "an unheld write");
    CHECK_EQ(r.Undone, 1, "journalled before it was issued, so undone");
}

int main(void)
{
    test_p14s_orico();
    test_boot_present();
    test_endpoint_only();
    test_latency_kept();
    test_dock();
    test_multifunction();
    test_ranges();
    test_bounds();
    test_inconsistent();
    test_failures();
    test_go_and_crs();
    printf("test_tbt_brnum: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
