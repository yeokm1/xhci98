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
    ULONG Cmd;              /* 04h                                           */
    ULONG Io;               /* 1Ch, bridges                                  */
    ULONG Mem;              /* 20h, bridges                                  */
    ULONG Pf;               /* 24h, bridges                                  */
    ULONG PfType;           /* 24h 3:0 and 19:16, read-only: 1 for 64-bit    */
    ULONG PfUp;             /* 28h, a 64-bit prefetchable window's           */
    ULONG PfLimit;          /* 2Ch                                           */
    ULONG Bar[6];           /* 10h-24h (an endpoint's six, a bridge's two)   */
    ULONG BarSize[6];       /* the size each decodes, 0 unimplemented; a
                               64-bit BAR's upper half has size 0            */
    ULONG BarType[6];       /* the read-only low bits                        */
    ULONG Sizing;           /* BARs read as all-ones-masked while set        */
    ULONG Intr;             /* 3Ch: line 7:0, pin 15:8 (read-only), and on a
                               bridge Bridge Control 31:16                   */
    ULONG PcieCaps;         /* a PCI Express capability at 40h: its
                               capabilities register (31:16), 0 for none    */
    ULONG SlotCap;          /* ... and Slot Capabilities (40h + 14h)        */
} NODE;

typedef struct _MODEL {
    NODE N[NODE_MAX];
    ULONG Count;
    ULONG Writes;
    ULONG FailWriteAt;      /* the 1-based write that fails, 0 none          */
    ULONG FailWriteBdf;     /* writes to this function ...                   */
    ULONG FailWriteOff;     /* ... at this offset fail                       */
    ULONG IgnoreWrites;     /* writes accepted but not held                  */
    ULONG FailReadBdf;      /* reads of this function fail ...               */
    ULONG FailReadOff;      /* ... at this offset only, where nonzero        */
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
    m.FailWriteBdf = XHCI_TBT_NO_BDF;
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
    if (bdf == m.FailReadBdf &&
        (m.FailReadOff == 0 || offset == m.FailReadOff)) {
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
    case XHCI_TBT_CFG_COMMAND:
        *value = m.N[i].Cmd |
                 (m.N[i].PcieCaps != 0 ? XHCI_TBT_CFG_STATUS_CAPLIST : 0UL);
        break;
    case XHCI_TBT_CFG_HEADER:
        *value = m.N[i].Header;
        break;
    case 0x3CUL:
        *value = m.N[i].Intr;
        break;
    default:
        if (m.N[i].PcieCaps != 0 && offset == XHCI_TBT_CFG_CAPPTR) {
            *value = 0x40UL;
            break;
        }
        if (m.N[i].PcieCaps != 0 && offset == 0x40UL) {
            *value = XHCI_TBT_CAP_PCIE | (m.N[i].PcieCaps << 16);
            break;
        }
        if (m.N[i].PcieCaps != 0 && offset == 0x40UL + XHCI_TBT_PCIE_SLOTCAP) {
            *value = m.N[i].SlotCap;
            break;
        }
        if (offset == XHCI_TBT_CFG_BUSES && m.N[i].Bridge) {
            *value = m.N[i].Buses;
            break;
        }
        if (m.N[i].Bridge && offset == XHCI_TBT_CFG_IOWIN) {
            *value = m.N[i].Io;
        } else if (m.N[i].Bridge && offset == XHCI_TBT_CFG_MEMWIN) {
            *value = m.N[i].Mem;
        } else if (m.N[i].Bridge && offset == XHCI_TBT_CFG_PFWIN) {
            *value = m.N[i].Pf | m.N[i].PfType | (m.N[i].PfType << 16);
        } else if (m.N[i].Bridge && offset == XHCI_TBT_CFG_PFUPBASE) {
            *value = m.N[i].PfType != 0 ? m.N[i].PfUp : 0;
        } else if (m.N[i].Bridge && offset == XHCI_TBT_CFG_PFUPLIMIT) {
            *value = m.N[i].PfType != 0 ? m.N[i].PfLimit : 0;
        } else if (offset >= 0x10UL && offset <= 0x24UL &&
                   (!m.N[i].Bridge || offset <= 0x14UL)) {
            *value = m.N[i].Bar[(offset - 0x10UL) / 4];
        } else {
            *value = 0;
        }
        break;
    }
    return 1;
}

/* A BAR write: the size bits a BAR implements hold, the low bits are its
 * type; an all-ones write reads back as the size mask. */
static VOID mbar(ULONG i, ULONG k, ULONG value)
{
    ULONG size = m.N[i].BarSize[k];

    if (size == 0) {
        /* Unimplemented (type 0), or 4 GiB or more: no address bits in
         * the low half. */
        m.N[i].Bar[k] = m.N[i].BarType[k];
        return;
    }
    m.N[i].Bar[k] = (value & ~(size - 1UL) & 0xFFFFFFF0UL) | m.N[i].BarType[k];
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
    if (bdf == m.FailWriteBdf && offset == m.FailWriteOff) {
        return 0;
    }
    i = mfind(bdf);
    if (i == ROOT || width != 4) {
        m.StrayWrites++;
        return 1;
    }
    if (m.IgnoreWrites) {
        return 1;
    }
    if (offset == 0x3CUL) {
        /* The pin and (on an endpoint) Min_Gnt/Max_Lat are read-only. */
        m.N[i].Intr = (m.N[i].Intr & 0x0000FF00UL) | (value & 0xFFUL) |
                      (m.N[i].Bridge ? (value & 0xFFFF0000UL)
                                     : (m.N[i].Intr & 0xFFFF0000UL));
    } else if (offset == XHCI_TBT_CFG_COMMAND) {
        m.N[i].Cmd = value & 0xFFFFUL;
    } else if (offset >= 0x10UL && offset <= 0x24UL &&
               (!m.N[i].Bridge || offset <= 0x14UL)) {
        mbar(i, (offset - 0x10UL) / 4, value);
    } else if (m.N[i].Bridge && offset == XHCI_TBT_CFG_BUSES) {
        m.N[i].Buses = value;
    } else if (m.N[i].Bridge && offset == XHCI_TBT_CFG_IOWIN) {
        m.N[i].Io = value & 0xFFFFUL;
    } else if (m.N[i].Bridge && offset == XHCI_TBT_CFG_MEMWIN) {
        m.N[i].Mem = value & 0xFFF0FFF0UL;
    } else if (m.N[i].Bridge && offset == XHCI_TBT_CFG_PFWIN) {
        m.N[i].Pf = value & 0xFFF0FFF0UL;
    } else if (m.N[i].Bridge && m.N[i].PfType != 0 &&
               offset == XHCI_TBT_CFG_PFUPBASE) {
        m.N[i].PfUp = value;
    } else if (m.N[i].Bridge && m.N[i].PfType != 0 &&
               offset == XHCI_TBT_CFG_PFUPLIMIT) {
        m.N[i].PfLimit = value;
    } else {
        m.StrayWrites++;
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
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, 0, NULL, NULL, &r), XHCI_TBT_BRNUM_DONE,
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
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, 0, NULL, NULL, &r), XHCI_TBT_BRNUM_NUMBERED,
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
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, 0, NULL, NULL, &r), XHCI_TBT_BRNUM_NUMBERED,
             "the BIOS's numbering is left alone");
    CHECK_EQ(m.Writes, 0, "nothing written");
    CHECK_EQ(m.N[usp].Buses, 0x00080706UL, "unchanged");

    /* Only the secondary set (a half-programmed bridge) still counts. */
    m.N[usp].Buses = 0x00000700UL;
    m.N[dsp].Buses = 0;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, 0, NULL, NULL, &r), XHCI_TBT_BRNUM_NUMBERED,
             "a secondary alone counts as numbered");
    m.N[usp].Buses = 0x00080000UL;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, 0, NULL, NULL, &r), XHCI_TBT_BRNUM_NUMBERED,
             "a subordinate alone counts as numbered");
    m.N[usp].Buses = 0x00000006UL;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, 0, NULL, NULL, &r), XHCI_TBT_BRNUM_DONE,
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
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, 0, NULL, NULL, &r), XHCI_TBT_BRNUM_NONE,
             "an endpoint alone: nothing to number");
    CHECK_EQ(m.Writes, 0, "nothing written");

    mreset();
    ioreset(TRUE);
    (VOID)mport(0x06, 0x2A);
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, 0, NULL, NULL, &r), XHCI_TBT_BRNUM_NONE,
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
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, 0, NULL, NULL, &r), XHCI_TBT_BRNUM_DONE,
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
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, 0, NULL, NULL, &r), XHCI_TBT_BRNUM_DONE,
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
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, 0, NULL, NULL, &r), XHCI_TBT_BRNUM_DONE,
             "a bridge at function 1 of a multi-function device");
    CHECK_EQ(m.N[f1].Buses, 0x00070706UL, "06/07/07");
    CHECK_EQ(mbus(e), 0x07, "its endpoint on 07");

    /* Function 1 of a single-function device is not looked at. */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    f0 = madd(port, 0, 0, ID_NVME, 0, 0);
    f1 = madd(port, 0, 1, ID_ASM, 1, 0);
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, 0, NULL, NULL, &r), XHCI_TBT_BRNUM_NONE,
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
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, 0, NULL, NULL, &r), XHCI_TBT_BRNUM_DONE,
             "an exact range fits");
    CHECK_EQ(m.N[dsp].Buses, 0x00080807UL, "07/08/08");

    /* One bus short: refused, everything written back. */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x07);
    masm(port, &usp, &dsp, &nvme);
    m.N[usp].Buses = 0x11000000UL;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, 0, NULL, NULL, &r), XHCI_TBT_BRNUM_NO_ROOM,
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
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, 0, NULL, NULL, &r), XHCI_TBT_BRNUM_NO_ROOM,
             "no bus to give");
    CHECK_EQ(m.Writes, 0, "nothing written");
    CHECK_EQ(m.N[usp].Buses, 0, "untouched");

    /* The port's own range unusable. */
    mreset();
    ioreset(TRUE);
    port = mport(0x00, 0x00);
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, 0, NULL, NULL, &r), XHCI_TBT_BRNUM_PORT,
             "a port with no secondary bus");
    mreset();
    ioreset(TRUE);
    port = mport(0x04, 0x2A);
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, 0, NULL, NULL, &r), XHCI_TBT_BRNUM_PORT,
             "a port whose secondary is its own bus");
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x05);
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, 0, NULL, NULL, &r), XHCI_TBT_BRNUM_PORT,
             "a port whose subordinate is below its secondary");
    mreset();
    ioreset(TRUE);
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, 0, NULL, NULL, &r), XHCI_TBT_BRNUM_PORT,
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
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, 0, NULL, NULL, &r), XHCI_TBT_BRNUM_DONE,
             "a chain of MAX_DEPTH bridges fits");
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    b = port;
    for (k = 0; k < XHCI_TBT_BRNUM_MAX_DEPTH + 1; k++) {
        b = madd(b, 0, 0, ID_ASM, 1, 0);
        nodes[k] = b;
    }
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, 0, NULL, NULL, &r), XHCI_TBT_BRNUM_TOO_DEEP,
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
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, 0, NULL, NULL, &r), XHCI_TBT_BRNUM_TOO_MANY,
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
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, 0, NULL, NULL, &r), XHCI_TBT_BRNUM_INCONSISTENT,
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
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, 0, NULL, NULL, &r), XHCI_TBT_BRNUM_NO_WRITER,
             "no writer");
    CHECK_EQ(io.Reads, 0, "nothing read");

    /* The second write (the downstream port's opening) fails. */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    masm(port, &usp, &dsp, &nvme);
    m.FailWriteAt = 2;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, 0, NULL, NULL, &r), XHCI_TBT_BRNUM_ACCESS,
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
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, 0, NULL, NULL, &r), XHCI_TBT_BRNUM_ACCESS,
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
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, 0, NULL, NULL, &r), XHCI_TBT_BRNUM_ACCESS,
             "a write that does not hold");
    CHECK_EQ(r.Where, XHCI_TBT_BDF(0x06, 0, 0), "at the upstream port");

    /* A read failing in the survey: nothing written. */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    masm(port, &usp, &dsp, &nvme);
    m.FailReadBdf = XHCI_TBT_BDF(0x06, 0, 0);
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, 0, NULL, NULL, &r), XHCI_TBT_BRNUM_ACCESS,
             "a failed read in the survey");
    CHECK_EQ(m.Writes, 0, "nothing written");

    /* A read failing below the first bridge: undone. */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    masm(port, &usp, &dsp, &nvme);
    m.FailReadBdf = XHCI_TBT_BDF(0x07, 0, 0);
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, 0, NULL, NULL, &r), XHCI_TBT_BRNUM_ACCESS,
             "a failed read below");
    CHECK_EQ(m.N[usp].Buses, 0, "the upstream port written back");

    /* The port unreadable. */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    m.FailReadBdf = PORT;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, 0, NULL, NULL, &r), XHCI_TBT_BRNUM_PORT,
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
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, 0, gocb, NULL, &r), XHCI_TBT_BRNUM_DONE,
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
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, 0, gocb, NULL, &r), XHCI_TBT_BRNUM_STALE,
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
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, 0, gocb, NULL, &r), XHCI_TBT_BRNUM_STALE,
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
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, 0, NULL, NULL, &r), XHCI_TBT_BRNUM_NOT_READY,
             "the upstream port not ready in the survey");
    CHECK_EQ(m.Writes, 0, "nothing written");
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    masm(port, &usp, &dsp, &nvme);
    m.N[dsp].Id = 0xFFFF0001UL;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, 0, NULL, NULL, &r), XHCI_TBT_BRNUM_NOT_READY,
             "the downstream port not ready below");
    CHECK_EQ(m.N[usp].Buses, 0, "the upstream port written back");
    CHECK_EQ(r.Where, XHCI_TBT_BDF(0x07, 0, 0), "at the downstream port");

    /* A write that lands but does not read back as written is undone. */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    masm(port, &usp, &dsp, &nvme);
    m.IgnoreWrites = 1;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, 0, NULL, NULL, &r), XHCI_TBT_BRNUM_ACCESS,
             "an unheld write");
    CHECK_EQ(r.Undone, 1, "journalled before it was issued, so undone");
}

/* The ORICO's NVMe as the BIOS left it under DOS: BAR0/1 a 64-bit 16 KiB
 * BAR, BAR2 and BAR3 4 KiB each (usb4-emu A0P1_OR PCIDUMP, 08:00.0; the
 * three ranges Windows 98 listed in that dump). */
static VOID mnvmebars(ULONG n)
{
    m.N[n].BarSize[0] = 0x4000UL;
    m.N[n].BarType[0] = 0x4UL;
    m.N[n].Bar[0] = 0x4UL;
    m.N[n].BarSize[2] = 0x1000UL;
    m.N[n].BarSize[3] = 0x1000UL;
}

static VOID test_windows(VOID)
{
    XHCI_TBT_BRNUM r;
    ULONG port, usp, dsp, nvme, k;
    ULONG d0, d1, d2, e0, e1, e2, sib;

    /* The P14s: the port's window D4100000-E00FFFFF as its BIOS left it. */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    m.N[port].Mem = 0xE000D410UL;
    masm(port, &usp, &dsp, &nvme);
    mnvmebars(nvme);
    m.N[usp].Io = 0;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, XHCI_TBT_BRNUM_F_WINDOWS, NULL, NULL, &r),
             XHCI_TBT_BRNUM_DONE, "numbered with windows");
    CHECK_EQ(r.WinOutcome, XHCI_TBT_BRWIN_DONE, "the windows written");
    CHECK_EQ(r.WinNeed, 0x00100000UL, "the chain needs one 1 MiB window");
    CHECK_EQ(r.Need[0], 0x00100000UL, "the upstream port 1 MiB");
    CHECK_EQ(r.Need[1], 0x00100000UL, "the downstream port 1 MiB (24 KiB of BARs)");
    CHECK_EQ(m.N[usp].Mem, 0xE000E000UL, "upstream port E0000000-E00FFFFF, the port window's top");
    CHECK_EQ(m.N[dsp].Mem, 0xE000E000UL, "downstream port the same, inside it");
    CHECK_EQ(m.N[usp].Io, 0x00F0UL, "I/O window closed");
    CHECK_EQ(m.N[usp].Pf, 0x0000FFF0UL, "prefetchable window closed");
    CHECK_EQ(m.N[dsp].Pf, 0x0000FFF0UL, "prefetchable window closed below too");
    CHECK_EQ(m.N[usp].Cmd, 0x0006UL, "memory and bus master on");
    CHECK_EQ(m.N[dsp].Cmd, 0x0006UL, "on the downstream port too");
    CHECK_EQ(m.N[nvme].Cmd, 0, "the NVMe's command untouched");
    CHECK_EQ(m.N[nvme].Bar[0], 0x00000004UL, "its BAR sized and put back");
    CHECK_EQ(m.N[nvme].Bar[2], 0, "and the others");
    CHECK_EQ(m.N[port].Mem, 0xE000D410UL, "the port's own window untouched");
    CHECK_EQ(m.StrayWrites, 0, "no stray write");

    /* Present at boot: nothing, windows included. */
    m.Writes = 0;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, XHCI_TBT_BRNUM_F_WINDOWS, NULL, NULL, &r),
             XHCI_TBT_BRNUM_NUMBERED, "a numbered chain is left alone");
    CHECK_EQ(r.WinOutcome, XHCI_TBT_BRWIN_NONE, "no window asked");
    CHECK_EQ(m.Writes, 0, "nothing written");

    /* No window on the port: numbered, no window written. */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    m.N[port].Mem = 0x0000FFF0UL;
    masm(port, &usp, &dsp, &nvme);
    mnvmebars(nvme);
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, XHCI_TBT_BRNUM_F_WINDOWS, NULL, NULL, &r),
             XHCI_TBT_BRNUM_DONE, "numbered");
    CHECK_EQ(r.WinOutcome, XHCI_TBT_BRWIN_PORT_CLOSED, "the port has no window");
    CHECK_EQ(m.N[usp].Mem, 0, "no window written");
    CHECK_EQ(m.N[usp].Buses, 0x00080706UL, "the numbering stands");

    /* A port window ending at 4 GiB is open, not closed: 1 MiB of it is
     * no room once its top unit is left out; 16 MiB is room, the windows
     * below FFF00000. */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    m.N[port].Mem = 0xFFF0FFF0UL;
    masm(port, &usp, &dsp, &nvme);
    mnvmebars(nvme);
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, XHCI_TBT_BRNUM_F_WINDOWS, NULL, NULL, &r),
             XHCI_TBT_BRNUM_DONE, "numbered");
    CHECK_EQ(r.WinOutcome, XHCI_TBT_BRWIN_NO_ROOM, "a 1 MiB window at 4 GiB: no room");
    CHECK_EQ(m.N[usp].Mem, 0, "no window written at 4 GiB");
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    m.N[port].Mem = 0xFFF0FF00UL;
    masm(port, &usp, &dsp, &nvme);
    mnvmebars(nvme);
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, XHCI_TBT_BRNUM_F_WINDOWS, NULL, NULL, &r),
             XHCI_TBT_BRNUM_DONE, "numbered");
    CHECK_EQ(r.WinOutcome, XHCI_TBT_BRWIN_DONE, "a 16 MiB window at 4 GiB: windowed");
    CHECK((m.N[usp].Mem & 0xFFF0UL) >= 0xFF00UL &&
          ((m.N[usp].Mem >> 16) & 0xFFF0UL) < 0xFFF0UL,
          "the upstream port's window inside it, below FFF00000");

    /* Too small: a 1 MiB port window, the chain plus a sibling endpoint. */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    m.N[port].Mem = 0xD410D410UL;
    masm(port, &usp, &dsp, &nvme);
    mnvmebars(nvme);
    sib = madd(port, 1, 0, ID_EMU, 0, 0);
    m.N[sib].BarSize[0] = 0x4000UL;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, XHCI_TBT_BRNUM_F_WINDOWS, NULL, NULL, &r),
             XHCI_TBT_BRNUM_DONE, "numbered");
    CHECK_EQ(r.WinOutcome, XHCI_TBT_BRWIN_NO_ROOM, "the window is too small");
    CHECK_EQ(m.N[usp].Mem, 0, "no window written");
    CHECK_EQ(m.N[usp].Cmd, 0, "no command written");

    /* The same with a 2 MiB window: the bridges take the top, the
     * sibling's 16 KiB is left the bottom. */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    m.N[port].Mem = 0xD420D410UL;
    masm(port, &usp, &dsp, &nvme);
    mnvmebars(nvme);
    sib = madd(port, 1, 0, ID_EMU, 0, 0);
    m.N[sib].BarSize[0] = 0x4000UL;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, XHCI_TBT_BRNUM_F_WINDOWS, NULL, NULL, &r),
             XHCI_TBT_BRNUM_DONE, "numbered");
    CHECK_EQ(r.WinOutcome, XHCI_TBT_BRWIN_DONE, "windows written");
    CHECK_EQ(m.N[usp].Mem, 0xD420D420UL, "the upstream port takes D4200000-D42FFFFF");
    CHECK_EQ(m.N[sib].Cmd, 0, "the sibling untouched");

    /* A function below already decoding memory: no window. */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    m.N[port].Mem = 0xE000D410UL;
    masm(port, &usp, &dsp, &nvme);
    mnvmebars(nvme);
    m.N[nvme].Cmd = 0x0002UL;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, XHCI_TBT_BRNUM_F_WINDOWS, NULL, NULL, &r),
             XHCI_TBT_BRNUM_DONE, "numbered");
    CHECK_EQ(r.WinOutcome, XHCI_TBT_BRWIN_ACTIVE, "an active function refuses the windows");
    CHECK_EQ(r.WinWhere, XHCI_TBT_BDF(0x08, 0, 0), "at the NVMe");
    CHECK_EQ(m.N[usp].Mem, 0, "no window written");

    /* A window write failing: every window written put back. The writes
     * of the P14s case: 4 numbering, 6 BAR sizing (3 BARs), then per
     * bridge I/O, prefetchable, memory ... */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    m.N[port].Mem = 0xE000D410UL;
    masm(port, &usp, &dsp, &nvme);
    mnvmebars(nvme);
    m.FailWriteBdf = XHCI_TBT_BDF(0x07, 0, 0);
    m.FailWriteOff = XHCI_TBT_CFG_MEMWIN;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, XHCI_TBT_BRNUM_F_WINDOWS, NULL, NULL, &r),
             XHCI_TBT_BRNUM_DONE, "numbered");
    CHECK_EQ(r.WinOutcome, XHCI_TBT_BRWIN_ACCESS, "a failed window write");
    CHECK_EQ(r.WinWhere, XHCI_TBT_BDF(0x07, 0, 0), "at the downstream port's window");
    CHECK_EQ(r.Undone >= 1, 1, "the upstream port's window had been written and was undone");
    CHECK_EQ(m.N[usp].Cmd, 0, "its command put back");
    CHECK_EQ(m.N[usp].Mem, 0, "the upstream port's window put back");
    CHECK_EQ(m.N[usp].Pf, 0, "and its prefetchable window");
    CHECK_EQ(m.N[usp].Io, 0, "and its I/O window");
    CHECK_EQ(m.N[dsp].Mem, 0, "the downstream port's too");
    CHECK_EQ(m.N[usp].Buses, 0x00080706UL, "the numbering stands");

    /* A dock: three downstream ports, an endpoint each. */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    m.N[port].Mem = 0xE000D410UL;
    usp = madd(port, 0, 0, 0x15D38086UL, 1, 0);
    d0 = madd(usp, 0, 0, 0x15D38086UL, 1, 0);
    d1 = madd(usp, 1, 0, 0x15D38086UL, 1, 0);
    d2 = madd(usp, 4, 0, 0x15D38086UL, 1, 0);
    e0 = madd(d0, 0, 0, 0x11001B73UL, 0, 0);
    e1 = madd(d1, 0, 0, 0x12421B21UL, 0, 0);
    e2 = madd(d2, 0, 0, 0x15338086UL, 0, 0);
    m.N[e0].BarSize[0] = 0x10000UL;
    m.N[e1].BarSize[0] = 0x8000UL;
    (void)e2;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, XHCI_TBT_BRNUM_F_WINDOWS, NULL, NULL, &r),
             XHCI_TBT_BRNUM_DONE, "a dock numbered");
    CHECK_EQ(r.WinOutcome, XHCI_TBT_BRWIN_DONE, "and windowed");
    CHECK_EQ(m.N[usp].Mem, 0xE000DFF0UL, "the upstream port 2 MiB at the top (DFF00000-E00FFFFF)");
    CHECK_EQ(m.N[d0].Mem, 0xE000E000UL, "the first downstream port its top 1 MiB");
    CHECK_EQ(m.N[d1].Mem, 0xDFF0DFF0UL, "the second the next 1 MiB down");
    CHECK_EQ(m.N[d2].Mem, 0x0000FFF0UL, "the third, with nothing decoding behind it, closed");
    for (k = 0; k < 4; k++) {
        CHECK_EQ(r.WinDone[k], 1, "each bridge's window registers written");
    }
}

static VOID test_windows_more(VOID)
{
    XHCI_TBT_BRNUM r;
    ULONG port, usp, dsp, nvme;

    /* A 2 MiB BAR: the windows aligned to 2 MiB, inside the port's. */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    m.N[port].Mem = 0xE000D410UL;
    masm(port, &usp, &dsp, &nvme);
    m.N[nvme].BarSize[0] = 0x200000UL;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, XHCI_TBT_BRNUM_F_WINDOWS, NULL, NULL, &r),
             XHCI_TBT_BRNUM_DONE, "numbered");
    CHECK_EQ(r.WinOutcome, XHCI_TBT_BRWIN_DONE, "windowed");
    CHECK_EQ(r.Align[1], 0x200000UL, "the downstream port aligned to 2 MiB");
    CHECK_EQ(r.Need[1], 0x200000UL, "2 MiB, a multiple of its own alignment");
    CHECK_EQ((((m.N[dsp].Mem & 0xFFF0UL) << 16) & 0x1FFFFFUL), 0, "its base 2 MiB aligned");
    CHECK_EQ((m.N[dsp].Mem & 0xFFF0UL) >= (m.N[usp].Mem & 0xFFF0UL), 1, "inside the upstream port window");
    CHECK_EQ((m.N[usp].Mem & 0xFFF0UL) >= 0xD410UL, 1, "inside the port window");

    /* Sums that would pass 4 GiB are refused, not wrapped. */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    m.N[port].Mem = 0xE000D410UL;
    masm(port, &usp, &dsp, &nvme);
    m.N[nvme].BarSize[0] = 0x40000000UL;
    m.N[nvme].BarSize[2] = 0x40000000UL;
    m.N[nvme].BarSize[3] = 0x40000000UL;
    m.N[nvme].BarSize[4] = 0x40000000UL;
    m.N[nvme].BarType[0] = 0;
    m.N[nvme].Bar[0] = 0;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, XHCI_TBT_BRNUM_F_WINDOWS, NULL, NULL, &r),
             XHCI_TBT_BRNUM_DONE, "numbered");
    CHECK_EQ(r.WinOutcome, XHCI_TBT_BRWIN_NO_ROOM, "4 GiB of BARs refused");
    CHECK_EQ(m.N[usp].Mem, 0, "nothing written");

    /* A 64-bit BAR whose low half sizes nothing (4 GiB or more). */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    m.N[port].Mem = 0xE000D410UL;
    masm(port, &usp, &dsp, &nvme);
    m.N[nvme].BarSize[0] = 0;
    m.N[nvme].BarType[0] = 0x4UL;
    m.N[nvme].Bar[0] = 0x4UL;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, XHCI_TBT_BRNUM_F_WINDOWS, NULL, NULL, &r),
             XHCI_TBT_BRNUM_DONE, "numbered");
    CHECK_EQ(r.WinOutcome, XHCI_TBT_BRWIN_NO_ROOM, "a 64-bit BAR of 4 GiB or more refused");

    /* A failed sizing write: the BAR still put back. */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    m.N[port].Mem = 0xE000D410UL;
    masm(port, &usp, &dsp, &nvme);
    mnvmebars(nvme);
    m.FailWriteBdf = XHCI_TBT_BDF(0x08, 0, 0);
    m.FailWriteOff = 0x18UL;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, XHCI_TBT_BRNUM_F_WINDOWS, NULL, NULL, &r),
             XHCI_TBT_BRNUM_DONE, "numbered");
    CHECK_EQ(r.WinOutcome, XHCI_TBT_BRWIN_ACCESS, "the sizing write refused");
    CHECK_EQ(m.N[nvme].Bar[2], 0, "the BAR as read");
}

static VOID test_irq(VOID)
{
    XHCI_TBT_BRNUM r;
    ULONG port, usp, dsp, nvme, sib;

    /* The P14s: the port's INTA on IRQ 10 (its BIOS's line, 0Ah, and the
     * $PIR's link 61 for 04:01 INTA); the chain's pins all INTA on device
     * 0, so each reaches the port as INTA and takes line 10, as the
     * BIOS's boot-present configuration had it (a.prn: IRQ 10 for both
     * bridges and the NVMe). */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    m.N[port].Mem = 0xE000D410UL;
    m.N[port].Intr = 0x0000010AUL;
    masm(port, &usp, &dsp, &nvme);
    mnvmebars(nvme);
    m.N[usp].Intr = 0x00000100UL;
    m.N[dsp].Intr = 0x000201FFUL;
    m.N[nvme].Intr = 0x00000100UL;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, XHCI_TBT_BRNUM_F_WINDOWS | XHCI_TBT_BRNUM_F_IRQ,
                                  NULL, NULL, &r), XHCI_TBT_BRNUM_DONE, "numbered");
    CHECK_EQ(r.IrqOutcome, XHCI_TBT_BRIRQ_DONE, "lines given");
    CHECK_EQ(r.IrqLine, 10, "the port's line");
    CHECK_EQ(r.IrqPin, 1, "the port's pin");
    CHECK_EQ(m.N[usp].Intr, 0x0000010AUL, "upstream port line 10");
    CHECK_EQ(m.N[dsp].Intr, 0x0002010AUL, "downstream port line 10, its Bridge Control kept");
    CHECK_EQ(m.N[nvme].Intr, 0x0000010AUL, "the NVMe line 10");
    CHECK_EQ(r.IrqFunctions, 3, "three functions with a pin");
    CHECK_EQ(r.IrqSet, 3, "all three set");
    /* Record 28 rule 5's per-function record. */
    CHECK_EQ(r.IrqFnCount, 3, "three outcomes");
    CHECK_EQ(r.IrqFnBdf[2], XHCI_TBT_BDF(0x08, 0, 0), "the NVMe's");
    CHECK_EQ(r.IrqFnOut[2], XHCI_TBT_BRFN_WRITTEN, "written");

    /* A sibling endpoint on device 1 with INTA reaches the port as INTB:
     * not the port's pin, so no line is written; one on device 0 of
     * another function... and a line already set is kept. */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    m.N[port].Mem = 0xE000D410UL;
    m.N[port].Intr = 0x0000010AUL;
    masm(port, &usp, &dsp, &nvme);
    mnvmebars(nvme);
    m.N[usp].Intr = 0x00000100UL;
    m.N[dsp].Intr = 0x00000100UL;
    m.N[nvme].Intr = 0x0000010BUL;
    sib = madd(port, 1, 0, ID_EMU, 0, 0);
    m.N[sib].Intr = 0x00000100UL;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, XHCI_TBT_BRNUM_F_WINDOWS | XHCI_TBT_BRNUM_F_IRQ,
                                  NULL, NULL, &r), XHCI_TBT_BRNUM_DONE, "numbered");
    CHECK_EQ(m.N[sib].Intr, 0x00000100UL, "the device-1 sibling arrives on INTB: left alone");
    CHECK_EQ(r.IrqOther, 1, "counted as another pin");
    CHECK_EQ(m.N[nvme].Intr, 0x0000010BUL, "a line already set is kept");
    CHECK_EQ(r.IrqKept, 1, "counted as kept");
    CHECK_EQ(r.IrqSet, 2, "the two bridges set");
    {
        ULONG k, other, already;

        other = 0;
        already = 0;
        for (k = 0; k < r.IrqFnCount; k++) {
            if (r.IrqFnBdf[k] == XHCI_TBT_BDF(0x06, 1, 0) &&
                r.IrqFnOut[k] == XHCI_TBT_BRFN_OTHER) {
                other++;
            }
            if (r.IrqFnBdf[k] == XHCI_TBT_BDF(0x08, 0, 0) &&
                r.IrqFnOut[k] == XHCI_TBT_BRFN_ALREADY) {
                already++;
            }
        }
        CHECK_EQ(other, 1, "the sibling recorded unroutable");
        CHECK_EQ(already, 1, "the NVMe recorded already set");
    }

    /* An NVMe on INTB behind device-0 bridges arrives on INTB: left. */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    m.N[port].Mem = 0xE000D410UL;
    m.N[port].Intr = 0x0000010AUL;
    masm(port, &usp, &dsp, &nvme);
    mnvmebars(nvme);
    m.N[nvme].Intr = 0x00000200UL;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, XHCI_TBT_BRNUM_F_WINDOWS | XHCI_TBT_BRNUM_F_IRQ,
                                  NULL, NULL, &r), XHCI_TBT_BRNUM_DONE, "numbered");
    CHECK_EQ(m.N[nvme].Intr, 0x00000200UL, "INTB not written");
    CHECK_EQ(r.IrqFunctions, 1, "the bridges here have no pin");

    /* The port with no line of its own: nothing written. */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    m.N[port].Mem = 0xE000D410UL;
    m.N[port].Intr = 0x000001FFUL;
    masm(port, &usp, &dsp, &nvme);
    mnvmebars(nvme);
    m.N[nvme].Intr = 0x00000100UL;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, XHCI_TBT_BRNUM_F_WINDOWS | XHCI_TBT_BRNUM_F_IRQ,
                                  NULL, NULL, &r), XHCI_TBT_BRNUM_DONE, "numbered");
    CHECK_EQ(r.IrqOutcome, XHCI_TBT_BRIRQ_PORT_NONE, "the port has no line");
    CHECK_EQ(m.N[nvme].Intr, 0x00000100UL, "nothing written");
    CHECK_EQ(r.IrqFnCount, 0, "no per-function record: the port's outcome");

    /* Windows refused: no lines either. */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    m.N[port].Mem = 0x0000FFF0UL;
    m.N[port].Intr = 0x0000010AUL;
    masm(port, &usp, &dsp, &nvme);
    m.N[nvme].Intr = 0x00000100UL;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, XHCI_TBT_BRNUM_F_WINDOWS | XHCI_TBT_BRNUM_F_IRQ,
                                  NULL, NULL, &r), XHCI_TBT_BRNUM_DONE, "numbered");
    CHECK_EQ(r.IrqOutcome, XHCI_TBT_BRIRQ_NONE, "no lines without windows");
    CHECK_EQ(m.N[nvme].Intr, 0x00000100UL, "nothing written");

    /* A function whose 3Ch cannot be read: its line not set up, so the
     * stage does not answer DONE. */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    m.N[port].Mem = 0xE000D410UL;
    m.N[port].Intr = 0x0000010AUL;
    masm(port, &usp, &dsp, &nvme);
    mnvmebars(nvme);
    m.N[usp].Intr = 0x00000100UL;
    m.N[dsp].Intr = 0x00000100UL;
    m.N[nvme].Intr = 0x00000100UL;
    m.FailReadBdf = XHCI_TBT_BDF(0x08, 0, 0);
    m.FailReadOff = 0x3CUL;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, XHCI_TBT_BRNUM_F_WINDOWS | XHCI_TBT_BRNUM_F_IRQ,
                                  NULL, NULL, &r), XHCI_TBT_BRNUM_DONE, "numbered");
    CHECK_EQ(r.IrqOutcome, XHCI_TBT_BRIRQ_ACCESS, "a line not read: not DONE");
    CHECK_EQ(r.IrqFailed, 1, "counted");
    CHECK_EQ(r.IrqSet, 2, "the two bridges set");
    CHECK_EQ(r.IrqFnCount, 3, "three outcomes");
    CHECK_EQ(r.IrqFnOut[2], XHCI_TBT_BRFN_NOT_READ, "the NVMe not read");
}

/* Design record 28 section 13: no bridge below the port (the emulator's
 * plain enclosure on a q35 root port): the functions on the port's own
 * bus get its line; the outcome stays NONE. */
static VOID test_irq_nobridge(VOID)
{
    XHCI_TBT_BRNUM r;
    ULONG port, nvme, sib;

    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    m.N[port].Intr = 0x0000010AUL;
    nvme = madd(port, 0, 0, ID_EMU, 0, 0);
    mnvmebars(nvme);
    m.N[nvme].Intr = 0x00000100UL;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, XHCI_TBT_BRNUM_F_WINDOWS | XHCI_TBT_BRNUM_F_IRQ,
                                  NULL, NULL, &r), XHCI_TBT_BRNUM_NONE, "no bridge: NONE");
    CHECK_EQ(r.IrqOutcome, XHCI_TBT_BRIRQ_DONE, "the line stage ran");
    CHECK_EQ(m.N[nvme].Intr, 0x0000010AUL, "the NVMe given line 10");
    CHECK_EQ(r.IrqSet, 1, "one set");
    CHECK_EQ(r.IrqFnCount, 1, "one outcome");
    CHECK_EQ(r.IrqFnBdf[0], XHCI_TBT_BDF(0x06, 0, 0), "the NVMe's");
    CHECK_EQ(r.IrqFnOut[0], XHCI_TBT_BRFN_WRITTEN, "written");
    CHECK_EQ(m.N[port].Buses, (0x2AUL << 16) | (0x06UL << 8) | 0x04UL,
             "the port's buses untouched");
    CHECK_EQ(r.Writes, 1, "the line its one write: no bus, BAR, command or window");
    CHECK_EQ(m.N[nvme].Cmd, 0, "the command register untouched");
    CHECK_EQ(r.WinOutcome, XHCI_TBT_BRWIN_NONE, "no window stage");

    /* A refusal after a line was written: two functions on the port's pin
     * (device 0 INTA, device 1 INTD), the second refused: the first put
     * back. */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    m.N[port].Intr = 0x0000010AUL;
    nvme = madd(port, 0, 0, ID_EMU, 0, 0);
    m.N[nvme].Intr = 0x00000100UL;
    sib = madd(port, 1, 0, ID_EMU, 0, 0);
    m.N[sib].Intr = 0x00000400UL;
    goCalls = 0;
    goRefuseAt = 2;
    goReplace = ROOT;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, XHCI_TBT_BRNUM_F_IRQ, gocb, NULL, &r),
             XHCI_TBT_BRNUM_NONE, "NONE");
    CHECK_EQ(r.IrqOutcome, XHCI_TBT_BRIRQ_STALE, "stale after a write");
    CHECK_EQ(m.N[nvme].Intr, 0x00000100UL, "the written line put back");
    CHECK_EQ(m.N[sib].Intr, 0x00000400UL, "the second never written");
    CHECK_EQ(r.Undone, 1, "one undone");
    goRefuseAt = 0;

    /* A line write not held: ACCESS, recorded NOT_HELD. */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    m.N[port].Intr = 0x0000010AUL;
    nvme = madd(port, 0, 0, ID_EMU, 0, 0);
    m.N[nvme].Intr = 0x00000100UL;
    m.FailWriteBdf = XHCI_TBT_BDF(0x06, 0, 0);
    m.FailWriteOff = 0x3CUL;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, XHCI_TBT_BRNUM_F_IRQ, NULL, NULL, &r),
             XHCI_TBT_BRNUM_NONE, "NONE");
    CHECK_EQ(r.IrqOutcome, XHCI_TBT_BRIRQ_ACCESS, "a write not held");
    CHECK_EQ(r.IrqFnOut[0], XHCI_TBT_BRFN_NOT_HELD, "recorded not held");

    /* A second function on device 1, INTA: arrives on INTB, left. A line
     * already set is kept. */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    m.N[port].Intr = 0x0000010AUL;
    nvme = madd(port, 0, 0, ID_EMU, 0, 0);
    m.N[nvme].Intr = 0x0000010BUL;
    sib = madd(port, 1, 0, ID_EMU, 0, 0);
    m.N[sib].Intr = 0x00000100UL;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, XHCI_TBT_BRNUM_F_IRQ, NULL, NULL, &r),
             XHCI_TBT_BRNUM_NONE, "NONE");
    CHECK_EQ(m.N[nvme].Intr, 0x0000010BUL, "a line already set kept");
    CHECK_EQ(m.N[sib].Intr, 0x00000100UL, "device 1 on INTB left");
    CHECK_EQ(r.IrqKept, 1, "kept");
    CHECK_EQ(r.IrqOther, 1, "another pin");

    /* The port with no line: nothing written. */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    m.N[port].Intr = 0x000001FFUL;
    nvme = madd(port, 0, 0, ID_EMU, 0, 0);
    m.N[nvme].Intr = 0x00000100UL;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, XHCI_TBT_BRNUM_F_IRQ, NULL, NULL, &r),
             XHCI_TBT_BRNUM_NONE, "NONE");
    CHECK_EQ(r.IrqOutcome, XHCI_TBT_BRIRQ_PORT_NONE, "the port has no line");
    CHECK_EQ(m.N[nvme].Intr, 0x00000100UL, "nothing written");

    /* F_IRQ not asked: nothing written, no stage. */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    m.N[port].Intr = 0x0000010AUL;
    nvme = madd(port, 0, 0, ID_EMU, 0, 0);
    m.N[nvme].Intr = 0x00000100UL;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, XHCI_TBT_BRNUM_F_WINDOWS, NULL, NULL, &r),
             XHCI_TBT_BRNUM_NONE, "NONE");
    CHECK_EQ(r.IrqOutcome, XHCI_TBT_BRIRQ_NONE, "no stage");
    CHECK_EQ(m.N[nvme].Intr, 0x00000100UL, "nothing written");

    /* Nothing on the bus: no stage, nothing written. */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    m.N[port].Intr = 0x0000010AUL;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, XHCI_TBT_BRNUM_F_IRQ, NULL, NULL, &r),
             XHCI_TBT_BRNUM_NONE, "NONE");
    CHECK_EQ(r.IrqOutcome, XHCI_TBT_BRIRQ_NONE, "an empty bus: no stage");

    /* The go callback refusing at the line's write: put back. */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    m.N[port].Intr = 0x0000010AUL;
    nvme = madd(port, 0, 0, ID_EMU, 0, 0);
    m.N[nvme].Intr = 0x00000100UL;
    goCalls = 0;
    goRefuseAt = 1;
    goReplace = ROOT;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, XHCI_TBT_BRNUM_F_IRQ, gocb, NULL, &r),
             XHCI_TBT_BRNUM_NONE, "NONE");
    CHECK_EQ(r.IrqOutcome, XHCI_TBT_BRIRQ_STALE, "stale");
    CHECK_EQ(m.N[nvme].Intr, 0x00000100UL, "nothing left written");
}

/* A 64-bit prefetchable window is closed through 28h and 2Ch as well as
 * 24h, and a refusal puts all three back. */
static VOID test_pf64(VOID)
{
    XHCI_TBT_BRNUM r;
    ULONG port, usp, dsp, nvme;

    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    m.N[port].Mem = 0xE000D410UL;
    masm(port, &usp, &dsp, &nvme);
    mnvmebars(nvme);
    m.N[usp].PfType = 1;
    m.N[usp].PfUp = 0;
    m.N[usp].PfLimit = 1;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, XHCI_TBT_BRNUM_F_WINDOWS, NULL, NULL, &r),
             XHCI_TBT_BRNUM_DONE, "numbered");
    CHECK_EQ(r.WinOutcome, XHCI_TBT_BRWIN_DONE, "the windows written");
    CHECK_EQ(m.N[usp].Pf, 0x0000FFF0UL, "24h closed");
    CHECK_EQ(m.N[usp].PfUp, 0, "28h 0");
    CHECK_EQ(m.N[usp].PfLimit, 0, "2Ch 0: the 64-bit window closed");
    CHECK_EQ(m.StrayWrites, 0, "no stray write (28h, 2Ch only where 64-bit)");

    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    m.N[port].Mem = 0xE000D410UL;
    masm(port, &usp, &dsp, &nvme);
    mnvmebars(nvme);
    m.N[usp].PfType = 1;
    m.N[usp].PfUp = 0x12UL;
    m.N[usp].PfLimit = 0x34UL;
    m.FailWriteBdf = XHCI_TBT_BDF(0x07, 0, 0);
    m.FailWriteOff = XHCI_TBT_CFG_MEMWIN;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, XHCI_TBT_BRNUM_F_WINDOWS, NULL, NULL, &r),
             XHCI_TBT_BRNUM_DONE, "numbered");
    CHECK_EQ(r.WinOutcome, XHCI_TBT_BRWIN_ACCESS, "the downstream window refused");
    CHECK_EQ(m.N[usp].PfUp, 0x12UL, "28h put back");
    CHECK_EQ(m.N[usp].PfLimit, 0x34UL, "2Ch put back");
    CHECK_EQ(m.N[usp].Pf, 0, "24h put back");
}

/* A window's base and limit from its 20h dword. */
static ULONG wbase(ULONG v)
{
    return (v & 0xFFF0UL) << 16;
}

static ULONG wlimit(ULONG v)
{
    return (((v >> 16) & 0xFFF0UL) << 16) | 0x000FFFFFUL;
}

static VOID test_windows_order_and_stale(VOID)
{
    XHCI_TBT_BRNUM r;
    ULONG port, usp, nvme, k, j;
    ULONG d[8];
    ULONG e[8];

    /* Eight downstream ports alternating a 1 MiB BAR (a 1 MiB window) and
     * a 2 MiB BAR (a 4 MiB window aligned to 2 MiB): placed largest
     * alignment first they pack inside the upstream port's window. */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    m.N[port].Mem = 0xE000D410UL;
    usp = madd(port, 0, 0, 0x15D38086UL, 1, 0);
    for (k = 0; k < 8; k++) {
        d[k] = madd(usp, k, 0, 0x15D38086UL, 1, 0);
        e[k] = madd(d[k], 0, 0, ID_NVME, 0, 0);
        m.N[e[k]].BarSize[0] = (k & 1) ? 0x200000UL : 0x100000UL;
    }
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, XHCI_TBT_BRNUM_F_WINDOWS, NULL, NULL, &r),
             XHCI_TBT_BRNUM_DONE, "numbered");
    CHECK_EQ(r.WinOutcome, XHCI_TBT_BRWIN_DONE, "mixed alignments placed");
    for (k = 0; k < 8; k++) {
        CHECK_EQ(wbase(m.N[d[k]].Mem) >= wbase(m.N[usp].Mem) &&
                 wlimit(m.N[d[k]].Mem) <= wlimit(m.N[usp].Mem), 1,
                 "each downstream window inside the upstream port's");
        if (k & 1) {
            CHECK_EQ(wbase(m.N[d[k]].Mem) & 0x1FFFFFUL, 0, "a 2 MiB one aligned");
        }
        for (j = 0; j < k; j++) {
            CHECK_EQ(wlimit(m.N[d[k]].Mem) < wbase(m.N[d[j]].Mem) ||
                     wlimit(m.N[d[j]].Mem) < wbase(m.N[d[k]].Mem), 1,
                     "no two windows overlap");
        }
    }

    /* Superseded during the window stage: the bus numbers go too, and a
     * later request sets everything up. */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    m.N[port].Mem = 0xE000D410UL;
    masm(port, &usp, &d[0], &nvme);
    mnvmebars(nvme);
    goCalls = 0;
    goRefuseAt = 5;
    goReplace = ROOT;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, XHCI_TBT_BRNUM_F_WINDOWS, gocb, NULL, &r),
             XHCI_TBT_BRNUM_STALE, "superseded in the window stage");
    CHECK_EQ(r.WinOutcome, XHCI_TBT_BRWIN_STALE, "the windows stale");
    CHECK_EQ(m.N[usp].Buses, 0, "the bus numbers undone too");
    CHECK_EQ(m.N[d[0]].Buses, 0, "both");
    goRefuseAt = 0;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, XHCI_TBT_BRNUM_F_WINDOWS, gocb, NULL, &r),
             XHCI_TBT_BRNUM_DONE, "the newer request numbers it");
    CHECK_EQ(r.WinOutcome, XHCI_TBT_BRWIN_DONE, "and windows it");

    /* An I/O window write back that does not hold is counted. */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    m.N[port].Mem = 0xE000D410UL;
    masm(port, &usp, &d[0], &nvme);
    mnvmebars(nvme);
    m.N[usp].Io = 0x0101UL;
    m.FailWriteBdf = XHCI_TBT_BDF(0x07, 0, 0);
    m.FailWriteOff = XHCI_TBT_CFG_MEMWIN;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, XHCI_TBT_BRNUM_F_WINDOWS, NULL, NULL, &r),
             XHCI_TBT_BRNUM_DONE, "numbered");
    CHECK_EQ(m.N[usp].Io, 0x0101UL, "the I/O window put back as read");
}

static VOID test_irq_stale(VOID)
{
    XHCI_TBT_BRNUM r;
    ULONG port, usp, dsp, nvme, at;
    ULONG done;

    /* Superseded at each point of the interrupt-line stage in turn: every
     * line, window and bus number written is undone, and the outcome is
     * stale; once the refusal comes after the last write, it is done. */
    done = 0;
    for (at = 15; at < 30 && !done; at++) {
        mreset();
        ioreset(TRUE);
        port = mport(0x06, 0x2A);
        m.N[port].Mem = 0xE000D410UL;
        m.N[port].Intr = 0x0000010AUL;
        masm(port, &usp, &dsp, &nvme);
        mnvmebars(nvme);
        m.N[usp].Intr = 0x00000100UL;
        m.N[dsp].Intr = 0x00000100UL;
        m.N[nvme].Intr = 0x00000100UL;
        goCalls = 0;
        goRefuseAt = at;
        goReplace = ROOT;
        (VOID)XhciTbtNumberBridges(&io, PORT,
                                   XHCI_TBT_BRNUM_F_WINDOWS | XHCI_TBT_BRNUM_F_IRQ,
                                   gocb, NULL, &r);
        if (r.Outcome == XHCI_TBT_BRNUM_DONE) {
            done = 1;
            CHECK_EQ(m.N[nvme].Intr, 0x0000010AUL, "past the last write: done");
            break;
        }
        CHECK_EQ(r.Outcome, XHCI_TBT_BRNUM_STALE, "superseded: stale");
        CHECK_EQ(m.N[usp].Buses, 0, "the bus numbers undone");
        CHECK_EQ(m.N[dsp].Buses, 0, "both");
        CHECK_EQ(m.N[usp].Mem, 0, "the windows undone");
        CHECK_EQ(m.N[usp].Cmd, 0, "the command undone");
        CHECK_EQ(m.N[usp].Intr, 0x00000100UL, "the lines undone");
        CHECK_EQ(m.N[nvme].Intr, 0x00000100UL, "every line");
        CHECK_EQ(r.UndoFailed, 0, "nothing failed to undo");
    }
    CHECK_EQ(done, 1, "a late enough refusal leaves it done");
    goRefuseAt = 0;
}

static VOID test_empty_bus_and_alignment(VOID)
{
    XHCI_TBT_BRNUM r;
    ULONG port, usp, dsp, sib;

    /* A downstream port with nothing behind it yet: with
     * XHCI_TBT_BRNUM_F_WAIT_EMPTY not ready, and everything undone;
     * without it, numbered as it is. */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    m.N[port].Mem = 0xE000D410UL;
    usp = madd(port, 0, 0, ID_ASM, 1, 0);
    dsp = madd(usp, 0, 0, ID_ASM, 1, 0);
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT,
                                  XHCI_TBT_BRNUM_F_WINDOWS | XHCI_TBT_BRNUM_F_WAIT_EMPTY,
                                  NULL, NULL, &r),
             XHCI_TBT_BRNUM_NOT_READY, "an empty bus behind a new bridge: not ready");
    CHECK_EQ(r.Where, XHCI_TBT_BDF(0x07, 0, 0), "at the downstream port");
    CHECK_EQ(m.N[usp].Buses, 0, "undone");
    CHECK_EQ(m.N[dsp].Buses, 0, "undone below too");
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, XHCI_TBT_BRNUM_F_WINDOWS, NULL, NULL, &r),
             XHCI_TBT_BRNUM_DONE, "the last attempt takes what answers");
    CHECK_EQ(m.N[dsp].Buses, 0x00080807UL, "the downstream port numbered all the same");
    CHECK_EQ(m.N[dsp].Mem, 0x0000FFF0UL, "with its window closed");

    /* A window of 6 MiB (10100000-106FFFFF), a bridge needing 2 MiB
     * aligned to 2 MiB and a 2 MiB BAR beside it: the usable part is
     * 10200000-105FFFFF, the BAR's region at its bottom, the bridge's
     * window at its top. */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    m.N[port].Mem = 0x10601010UL;
    usp = madd(port, 0, 0, ID_ASM, 1, 0);
    dsp = madd(usp, 0, 0, ID_NVME, 0, 0);
    m.N[dsp].BarSize[0] = 0x200000UL;
    sib = madd(port, 1, 0, ID_EMU, 0, 0);
    m.N[sib].BarSize[0] = 0x200000UL;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, XHCI_TBT_BRNUM_F_WINDOWS, NULL, NULL, &r),
             XHCI_TBT_BRNUM_DONE, "numbered");
    CHECK_EQ(r.WinOutcome, XHCI_TBT_BRWIN_DONE, "it fits");
    CHECK_EQ(m.N[usp].Mem, 0x10501040UL, "the bridge at 10400000-105FFFFF, above the BAR's region");

    /* A 33 MiB window 10100000-121FFFFF; a bridge needing 16 MiB aligned
     * to 8 MiB, one needing 1 MiB, and an 8 MiB BAR beside them: the
     * A-aligned part is 10800000-11FFFFFF, 24 MiB, short of 8 + 16 + 1. */
    {
        ULONG b1, b2, e1, e2, e3;

        mreset();
        ioreset(TRUE);
        port = mport(0x06, 0x2A);
        m.N[port].Mem = 0x121F1010UL;
        b1 = madd(port, 0, 0, ID_ASM, 1, 0);
        e1 = madd(b1, 0, 0, ID_NVME, 0, 0);
        e2 = madd(b1, 1, 0, ID_NVME, 0, 0);
        m.N[e1].BarSize[0] = 0x800000UL;
        m.N[e2].BarSize[0] = 0x800000UL;
        b2 = madd(port, 1, 0, ID_ASM, 1, 0);
        e3 = madd(b2, 0, 0, ID_NVME, 0, 0);
        m.N[e3].BarSize[0] = 0x100000UL;
        sib = madd(port, 2, 0, ID_EMU, 0, 0);
        m.N[sib].BarSize[0] = 0x800000UL;
        CHECK_EQ(XhciTbtNumberBridges(&io, PORT, XHCI_TBT_BRNUM_F_WINDOWS, NULL, NULL, &r),
                 XHCI_TBT_BRNUM_DONE, "numbered");
        CHECK_EQ(r.WinOutcome, XHCI_TBT_BRWIN_NO_ROOM, "the aligned part is too small");
        CHECK_EQ(m.N[b1].Mem, 0, "nothing written");

        /* With 8 MiB more at the top it fits: the 8 MiB-aligned window
         * first from the top, the 1 MiB one below it, the BAR's 8 MiB
         * region at the bottom. */
        mreset();
        ioreset(TRUE);
        port = mport(0x06, 0x2A);
        m.N[port].Mem = 0x129F1010UL;
        b1 = madd(port, 0, 0, ID_ASM, 1, 0);
        e1 = madd(b1, 0, 0, ID_NVME, 0, 0);
        e2 = madd(b1, 1, 0, ID_NVME, 0, 0);
        m.N[e1].BarSize[0] = 0x800000UL;
        m.N[e2].BarSize[0] = 0x800000UL;
        b2 = madd(port, 1, 0, ID_ASM, 1, 0);
        e3 = madd(b2, 0, 0, ID_NVME, 0, 0);
        m.N[e3].BarSize[0] = 0x100000UL;
        sib = madd(port, 2, 0, ID_EMU, 0, 0);
        m.N[sib].BarSize[0] = 0x800000UL;
        CHECK_EQ(XhciTbtNumberBridges(&io, PORT, XHCI_TBT_BRNUM_F_WINDOWS, NULL, NULL, &r),
                 XHCI_TBT_BRNUM_DONE, "numbered");
        CHECK_EQ(r.WinOutcome, XHCI_TBT_BRWIN_DONE, "fits");
        CHECK_EQ(m.N[b1].Mem, 0x12701180UL, "the 16 MiB window at 11800000-127FFFFF");
        CHECK_EQ(m.N[b2].Mem, 0x11701170UL, "the 1 MiB window just below it");
        CHECK_EQ(wbase(m.N[b2].Mem) >= 0x10800000UL + 0x800000UL, 1, "above the BAR's 8 MiB region");
    }
}

/* The P14s chain as a Safely Remove leaves it: bus numbers kept, every
 * window closed, the NVMe's BARs unassigned. */
static VOID mstopped(PULONG port, PULONG usp, PULONG dsp, PULONG nvme)
{
    mreset();
    ioreset(TRUE);
    *port = mport(0x06, 0x2A);
    m.N[*port].Mem = 0xE000D410UL;
    m.N[*port].Intr = 0x0000010AUL;
    masm(*port, usp, dsp, nvme);
    mnvmebars(*nvme);
    m.N[*usp].Buses = 0x00080706UL;
    m.N[*dsp].Buses = 0x00080807UL;
    m.N[*usp].Mem = 0x0000FFF0UL;
    m.N[*dsp].Mem = 0x0000FFF0UL;
    m.N[*usp].Intr = 0x0000010AUL;
    m.N[*dsp].Intr = 0x0000010AUL;
    m.N[*nvme].Intr = 0x0000010AUL;
}

static VOID test_reopen(VOID)
{
    XHCI_TBT_BRNUM r;
    ULONG port, usp, dsp, nvme;
    ULONG flags = XHCI_TBT_BRNUM_F_WINDOWS | XHCI_TBT_BRNUM_F_IRQ;

    /* Numbered, windows closed: the windows set up again, the bus numbers
     * untouched, the lines kept as Windows left them. */
    mstopped(&port, &usp, &dsp, &nvme);
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, flags, NULL, NULL, &r),
             XHCI_TBT_BRNUM_REOPENED, "a stopped chain is re-opened");
    CHECK_EQ(r.WinOutcome, XHCI_TBT_BRWIN_DONE, "its windows written");
    CHECK_EQ(m.N[usp].Mem, 0xE000E000UL, "the upstream port E0000000-E00FFFFF again");
    CHECK_EQ(m.N[dsp].Mem, 0xE000E000UL, "the downstream port too");
    CHECK_EQ(m.N[usp].Cmd, 0x0006UL, "memory and bus master on");
    CHECK_EQ(m.N[usp].Buses, 0x00080706UL, "the bus numbers as they were");
    CHECK_EQ(m.N[dsp].Buses, 0x00080807UL, "both");
    CHECK_EQ(r.Numbered, 2, "two bridges journalled");
    CHECK_EQ(r.IrqKept, 3, "the lines Windows left are kept");
    CHECK_EQ(m.N[nvme].Bar[0], 0x4UL, "the NVMe's BAR put back after sizing");
    CHECK_EQ(m.StrayWrites, 0, "no stray write");

    /* With Memory Space still on in the stopped bridges (no BAR of
     * theirs in use): re-opened as well. */
    mstopped(&port, &usp, &dsp, &nvme);
    m.N[usp].Cmd = 0x0006UL;
    m.N[dsp].Cmd = 0x0006UL;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, flags, NULL, NULL, &r),
             XHCI_TBT_BRNUM_REOPENED, "re-opened with the bridges' Memory Space on");
    CHECK_EQ(r.WinOutcome, XHCI_TBT_BRWIN_DONE, "windows written");

    /* Numbered, windows open (present at boot, or still started): left
     * alone. */
    mstopped(&port, &usp, &dsp, &nvme);
    m.N[usp].Mem = 0xD410D410UL;
    m.N[dsp].Mem = 0xD410D410UL;
    m.Writes = 0;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, flags, NULL, NULL, &r),
             XHCI_TBT_BRNUM_NUMBERED, "open windows: left alone");
    CHECK_EQ(m.Writes, 0, "nothing written");
    CHECK_EQ(m.N[usp].Mem, 0xD410D410UL, "unchanged");

    /* One window open, one closed: the whole chain left alone. */
    mstopped(&port, &usp, &dsp, &nvme);
    m.N[usp].Mem = 0xD410D410UL;
    m.Writes = 0;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, flags, NULL, NULL, &r),
             XHCI_TBT_BRNUM_NUMBERED, "a mixed chain: left alone");
    CHECK_EQ(m.Writes, 0, "nothing written");

    /* The NVMe still decoding: the windows refused, nothing written. */
    mstopped(&port, &usp, &dsp, &nvme);
    m.N[nvme].Cmd = 0x0002UL;
    m.N[nvme].Bar[0] = 0xD4100004UL;
    m.Writes = 0;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, flags, NULL, NULL, &r),
             XHCI_TBT_BRNUM_REOPENED, "journalled");
    CHECK_EQ(r.WinOutcome, XHCI_TBT_BRWIN_ACTIVE, "a decoding function refuses the windows");
    CHECK_EQ(m.N[usp].Cmd | m.N[dsp].Cmd, 0, "no command written (only BAR probes, put back)");
    CHECK_EQ(m.N[nvme].Bar[0], 0xD4100004UL, "the decoding BAR untouched");
    CHECK_EQ(m.N[usp].Mem, 0x0000FFF0UL, "still closed");

    /* A stopped bridge with a BAR holding an address and Memory Space on:
     * refused, nothing written. */
    mstopped(&port, &usp, &dsp, &nvme);
    m.N[usp].Cmd = 0x0006UL;
    m.N[usp].BarSize[0] = 0x100UL;
    m.N[usp].Bar[0] = 0xE0100000UL;
    m.Writes = 0;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, flags, NULL, NULL, &r),
             XHCI_TBT_BRNUM_REOPENED, "journalled");
    CHECK_EQ(r.WinOutcome, XHCI_TBT_BRWIN_ACTIVE, "a bridge BAR in use refuses the windows");
    CHECK_EQ(m.Writes, 0, "nothing written");

    /* Without the windows flag a numbered chain is never re-opened. */
    mstopped(&port, &usp, &dsp, &nvme);
    m.Writes = 0;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, 0, NULL, NULL, &r),
             XHCI_TBT_BRNUM_NUMBERED, "no windows flag: numbered, nothing more");
    CHECK_EQ(m.Writes, 0, "nothing written");

    /* A numbered bridge outside the port's range: inconsistent, left. */
    mstopped(&port, &usp, &dsp, &nvme);
    m.N[usp].Buses = 0x00300706UL;
    m.Writes = 0;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, flags, NULL, NULL, &r),
             XHCI_TBT_BRNUM_NUMBERED, "a subordinate past the port's: left alone");
    CHECK_EQ(m.Writes, 0, "nothing written");

    /* Superseded during the re-open: the windows put back, the bus
     * numbers never touched. */
    mstopped(&port, &usp, &dsp, &nvme);
    goCalls = 0;
    goRefuseAt = 12;
    goReplace = ROOT;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, flags, gocb, NULL, &r),
             XHCI_TBT_BRNUM_STALE, "superseded: stale");
    CHECK_EQ(m.N[usp].Mem, 0x0000FFF0UL, "the window put back closed");
    CHECK_EQ(m.N[usp].Buses, 0x00080706UL, "the bus numbers as they were");
    goRefuseAt = 0;

    /* Replaced while its decode is paused for sizing: the replacement
     * is never given the old command; the decode stays off, counted. */
    mstopped(&port, &usp, &dsp, &nvme);
    m.N[usp].Cmd = 0x0006UL;
    m.N[usp].BarSize[0] = 0x1000UL;
    goCalls = 0;
    goRefuseAt = 2;
    goReplace = usp;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, flags, gocb, NULL, &r),
             XHCI_TBT_BRNUM_STALE, "replaced: stale");
    CHECK_EQ(m.N[usp].Cmd, 0x0004UL, "the replacement left with decode off");
    CHECK_EQ(r.UndoFailed >= 1, 1, "the restore not made, counted");
    goRefuseAt = 0;
    goReplace = ROOT;

    /* The NVMe not answering yet behind the stopped chain: with
     * WAIT_EMPTY not ready, nothing written; and answering Retry Status
     * the same. */
    mstopped(&port, &usp, &dsp, &nvme);
    m.N[nvme].Id = 0xFFFFFFFFUL;
    m.Writes = 0;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, flags | XHCI_TBT_BRNUM_F_WAIT_EMPTY,
                                  NULL, NULL, &r),
             XHCI_TBT_BRNUM_NOT_READY, "an empty bus behind a stopped bridge: not ready");
    CHECK_EQ(m.Writes, 0, "nothing written");
    mstopped(&port, &usp, &dsp, &nvme);
    m.N[nvme].Id = 0xFFFF0001UL;
    m.Writes = 0;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, flags, NULL, NULL, &r),
             XHCI_TBT_BRNUM_NOT_READY, "Retry Status behind a stopped chain: not ready");
    CHECK_EQ(m.Writes, 0, "nothing written");

    /* Two bridges of one bus claiming overlapping buses: left alone. */
    {
        ULONG b1, b2;

        mreset();
        ioreset(TRUE);
        port = mport(0x06, 0x2A);
        m.N[port].Mem = 0xE000D410UL;
        b1 = madd(port, 0, 0, ID_ASM, 1, 0);
        b2 = madd(port, 1, 0, ID_ASM, 1, 0);
        m.N[b1].Buses = 0x00080706UL;
        m.N[b2].Buses = 0x00090806UL;
        m.N[b1].Mem = 0x0000FFF0UL;
        m.N[b2].Mem = 0x0000FFF0UL;
        m.Writes = 0;
        CHECK_EQ(XhciTbtNumberBridges(&io, PORT, flags, NULL, NULL, &r),
                 XHCI_TBT_BRNUM_NUMBERED, "overlapping sibling ranges: left alone");
        CHECK_EQ(m.Writes, 0, "nothing written");
    }

    /* A stopped downstream port with Memory Space on and an implemented but
     * empty 2 MiB BAR: sized with its decode paused, counted, and its
     * command put back before the windows set it again. */
    mstopped(&port, &usp, &dsp, &nvme);
    m.N[dsp].Cmd = 0x0006UL;
    m.N[dsp].BarSize[0] = 0x200000UL;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, flags, NULL, NULL, &r),
             XHCI_TBT_BRNUM_REOPENED, "re-opened");
    CHECK_EQ(r.WinOutcome, XHCI_TBT_BRWIN_DONE, "windowed");
    CHECK_EQ(r.Need[0] >= 0x300000UL, 1, "the upstream window holds the bridge BAR and the NVMe's window");
    CHECK_EQ(m.N[dsp].Cmd, 0x0006UL, "its command as it was");
    CHECK_EQ(m.N[dsp].Bar[0], 0, "its BAR put back empty");

    /* Memory Space on, BAR0 empty but BAR1 holding an address: in use;
     * read only, nothing written. */
    mstopped(&port, &usp, &dsp, &nvme);
    m.N[usp].Cmd = 0x0006UL;
    m.N[usp].BarSize[0] = 0x1000UL;
    m.N[usp].BarSize[1] = 0x1000UL;
    m.N[usp].Bar[1] = 0xE0200000UL;
    m.Writes = 0;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, flags, NULL, NULL, &r),
             XHCI_TBT_BRNUM_REOPENED, "journalled");
    CHECK_EQ(r.WinOutcome, XHCI_TBT_BRWIN_ACTIVE, "BAR1 in use refuses");
    CHECK_EQ(m.Writes, 0, "nothing written, its decode never paused");

    /* The BAR's restore write failing while decode is paused: the decode
     * stays off, the failure counted. */
    mstopped(&port, &usp, &dsp, &nvme);
    m.N[usp].Cmd = 0x0006UL;
    m.N[usp].BarSize[0] = 0x1000UL;
    m.FailWriteBdf = XHCI_TBT_BDF(0x06, 0, 0);
    m.FailWriteOff = 0x10UL;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, flags, NULL, NULL, &r),
             XHCI_TBT_BRNUM_REOPENED, "journalled");
    CHECK_EQ(r.WinOutcome, XHCI_TBT_BRWIN_ACCESS, "the sizing failed");
    CHECK_EQ(m.N[usp].Cmd & 0x2UL, 0, "its memory decode left off");
    CHECK_EQ(r.UndoFailed >= 1, 1, "the failure counted");
    m.FailWriteBdf = XHCI_TBT_NO_BDF;

    /* Two bridges of one bus with disjoint ranges in reverse order: fine. */
    {
        ULONG b1, b2, e1, e2;

        mreset();
        ioreset(TRUE);
        port = mport(0x06, 0x2A);
        m.N[port].Mem = 0xE000D410UL;
        b1 = madd(port, 0, 0, ID_ASM, 1, 0);
        b2 = madd(port, 1, 0, ID_ASM, 1, 0);
        e1 = madd(b1, 0, 0, ID_NVME, 0, 0);
        e2 = madd(b2, 0, 0, ID_NVME, 0, 0);
        m.N[e1].BarSize[0] = 0x4000UL;
        m.N[e2].BarSize[0] = 0x4000UL;
        m.N[b1].Buses = 0x000A0906UL;
        m.N[b2].Buses = 0x00080706UL;
        m.N[b1].Mem = 0x0000FFF0UL;
        m.N[b2].Mem = 0x0000FFF0UL;
        CHECK_EQ(XhciTbtNumberBridges(&io, PORT, flags, NULL, NULL, &r),
                 XHCI_TBT_BRNUM_REOPENED, "reversed disjoint siblings: re-opened");
        CHECK_EQ(r.WinOutcome, XHCI_TBT_BRWIN_DONE, "windowed");
        CHECK_EQ(m.N[b1].Buses, 0x000A0906UL, "bus numbers as found");
    }

    /* A stopped bridge, Memory Space on, a 64-bit BAR whose upper half
     * holds an address: in use, refused, nothing written. */
    mstopped(&port, &usp, &dsp, &nvme);
    m.N[usp].Cmd = 0x0006UL;
    m.N[usp].BarSize[0] = 0x100UL;
    m.N[usp].BarType[0] = 0x4UL;
    m.N[usp].Bar[0] = 0x4UL;
    m.N[usp].Bar[1] = 0x1UL;
    m.Writes = 0;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, flags, NULL, NULL, &r),
             XHCI_TBT_BRNUM_REOPENED, "journalled");
    CHECK_EQ(r.WinOutcome, XHCI_TBT_BRWIN_ACTIVE, "a 64-bit BAR above 4 GiB in use refuses");
    CHECK_EQ(m.Writes, 0, "nothing written");
}

/* ---- the padding (record 34 section 3.3.6) ---------------------------- */

#define PCIE_DOWN_SLOT  ((XHCI_TBT_PCIE_TYPE_DOWN << 4) | XHCI_TBT_PCIE_SLOT_IMPL | 2UL)
#define PCIE_DOWN_FIXED ((XHCI_TBT_PCIE_TYPE_DOWN << 4) | 2UL)
#define PCIE_UP         ((0x5UL << 4) | 2UL)

/* A chain-port candidate: a downstream port with a hot-plug slot. */
static VOID mcand(ULONG i)
{
    m.N[i].PcieCaps = PCIE_DOWN_SLOT;
    m.N[i].SlotCap = XHCI_TBT_SLOTCAP_HPC;
}

/* The emulator's dock: its upstream port, a fixed port to its NVMe, the
 * chain port; optionally a device chained behind it. */
static VOID mdock(ULONG port, ULONG chained, PULONG usp, PULONG fix,
                  PULONG cp, PULONG nv, PULONG cnv)
{
    *usp = madd(port, 0, 0, 0x8232104CUL, 1, 0);
    m.N[*usp].PcieCaps = PCIE_UP;
    *fix = madd(*usp, 0, 0, 0x8233104CUL, 1, 0);
    m.N[*fix].PcieCaps = PCIE_DOWN_FIXED;
    *cp = madd(*usp, 1, 0, 0x8233104CUL, 1, 0);
    mcand(*cp);
    *nv = madd(*fix, 0, 0, ID_NVME, 0, 0);
    *cnv = chained ? madd(*cp, 0, 0, ID_NVME, 0, 0) : ROOT;
}

static VOID test_pad_buses(VOID)
{
    XHCI_TBT_BRNUM r;
    ULONG port, usp, fix, cp, nv, cnv, cp2, fix2, a, b, c2;

    /* The dock in a wide port (06 to 2A): the chain port 08, padded 8. */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    mdock(port, 0, &usp, &fix, &cp, &nv, &cnv);
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, XHCI_TBT_BRNUM_F_PAD, NULL, NULL, &r),
             XHCI_TBT_BRNUM_DONE, "pad: the dock numbered");
    CHECK_EQ(m.N[usp].Buses, 0x00110706UL, "pad: upstream port 06/07/11");
    CHECK_EQ(m.N[fix].Buses, 0x00080807UL, "pad: fixed port 07/08/08, not padded");
    CHECK_EQ(m.N[cp].Buses, 0x00110907UL, "pad: chain port 07/09/11 (8 past its secondary)");
    CHECK_EQ(r.Padded, 1, "pad: one candidate padded");
    CHECK_EQ(r.Pad[2], 8, "pad: its pad 8");
    CHECK_EQ(r.Last, 0x11, "pad: last bus 11");
    CHECK_EQ(r.PadNoBus, 0, "pad: none short");

    /* Without the flag: as today. */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    mdock(port, 0, &usp, &fix, &cp, &nv, &cnv);
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, 0, NULL, NULL, &r), XHCI_TBT_BRNUM_DONE,
             "no pad: numbered");
    CHECK_EQ(m.N[cp].Buses, 0x00090907UL, "no pad: chain port 07/09/09");
    CHECK_EQ(r.Padded, 0, "no pad: nothing padded");

    /* A narrow port (06 to 0D): 4 buses left at the chain port, pad 2. */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x0D);
    mdock(port, 0, &usp, &fix, &cp, &nv, &cnv);
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, XHCI_TBT_BRNUM_F_PAD, NULL, NULL, &r),
             XHCI_TBT_BRNUM_DONE, "narrow: numbered");
    CHECK_EQ(m.N[cp].Buses, 0x000B0907UL, "narrow: chain port 07/09/0B (pad 2)");
    CHECK_EQ(r.Pad[2], 2, "narrow: pad 2");

    /* Too narrow (06 to 0B): 2 left, pad 1 is below 2: none. */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x0B);
    mdock(port, 0, &usp, &fix, &cp, &nv, &cnv);
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, XHCI_TBT_BRNUM_F_PAD, NULL, NULL, &r),
             XHCI_TBT_BRNUM_DONE, "too narrow: numbered");
    CHECK_EQ(m.N[cp].Buses, 0x00090907UL, "too narrow: chain port 07/09/09");
    CHECK_EQ(r.Padded, 0, "too narrow: not padded");
    CHECK_EQ(r.PadNoBus, 1, "too narrow: counted");

    /* A device behind the chain port at the first numbering: padded past
     * its subtree (an ASM2464 there: 0A and 0B, then 8 more). */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    mdock(port, 0, &usp, &fix, &cp, &nv, &cnv);
    masm(cp, &a, &b, &c2);
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, XHCI_TBT_BRNUM_F_PAD, NULL, NULL, &r),
             XHCI_TBT_BRNUM_DONE, "behind: numbered");
    CHECK_EQ(m.N[a].Buses, 0x000B0A09UL, "behind: its upstream port 09/0A/0B");
    CHECK_EQ(m.N[cp].Buses, 0x00130907UL, "behind: chain port 07/09/13 (0B + 8)");
    CHECK_EQ(mbus(c2), 0x0B, "behind: the chained NVMe on 0B");

    /* Two candidates: the second gets half of what the first left. */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x16);
    mdock(port, 0, &usp, &fix, &cp, &nv, &cnv);
    cp2 = madd(usp, 2, 0, 0x8233104CUL, 1, 0);
    mcand(cp2);
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, XHCI_TBT_BRNUM_F_PAD, NULL, NULL, &r),
             XHCI_TBT_BRNUM_DONE, "two: numbered");
    CHECK_EQ(m.N[cp].Buses, 0x000F0907UL, "two: first 07/09/0F (13 left, pad 6)");
    CHECK_EQ(m.N[cp2].Buses, 0x00131007UL, "two: second 07/10/13 (6 left, pad 3)");
    CHECK_EQ(r.Padded, 2, "two: both padded");

    /* Not candidates: a downstream port without a slot (the fixed one),
     * a slot without Hot-Plug Capable. */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    mdock(port, 0, &usp, &fix, &cp, &nv, &cnv);
    m.N[cp].SlotCap = 0;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, XHCI_TBT_BRNUM_F_PAD, NULL, NULL, &r),
             XHCI_TBT_BRNUM_DONE, "no hpc: numbered");
    CHECK_EQ(m.N[cp].Buses, 0x00090907UL, "no hpc: not padded");

    /* A failed capability read: undone. */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    mdock(port, 0, &usp, &fix, &cp, &nv, &cnv);
    m.FailReadBdf = XHCI_TBT_BDF(7, 1, 0);
    m.FailReadOff = 0x40UL;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, XHCI_TBT_BRNUM_F_PAD, NULL, NULL, &r),
             XHCI_TBT_BRNUM_ACCESS, "cap read failed: refused");
    CHECK_EQ(m.N[usp].Buses, 0UL, "cap read failed: undone");
    m.FailReadBdf = 0;
    m.FailReadOff = 0;

    /* A dock that fits only without padding: the port 06 to 0C, the
     * chain port first (08, pad 2 of 4 left), then three fixed ports, the
     * last of which finds no bus: NO_ROOM, numbered again without. */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x0C);
    usp = madd(port, 0, 0, 0x8232104CUL, 1, 0);
    cp = madd(usp, 0, 0, 0x8233104CUL, 1, 0);
    mcand(cp);
    fix = madd(usp, 1, 0, 0x8233104CUL, 1, 0);
    fix2 = madd(usp, 2, 0, 0x8233104CUL, 1, 0);
    a = madd(usp, 3, 0, 0x8233104CUL, 1, 0);
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, XHCI_TBT_BRNUM_F_PAD, NULL, NULL, &r),
             XHCI_TBT_BRNUM_DONE, "dropped: numbered without padding");
    CHECK_EQ(r.PadDropped, 1, "dropped: counted");
    CHECK_EQ(r.Padded, 0, "dropped: nothing padded");
    CHECK_EQ(m.N[cp].Buses, 0x00080807UL, "dropped: chain port 07/08/08");
    CHECK_EQ(m.N[a].Buses, 0x000B0B07UL, "dropped: the last fixed port 07/0B/0B");
    CHECK_EQ(m.N[fix].Buses, 0x00090907UL, "dropped: 07/09/09");
    CHECK_EQ(m.N[fix2].Buses, 0x000A0A07UL, "dropped: 07/0A/0A");

    /* ... and with the undo failing (its first write, the eighth): not
     * numbered again. */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x0C);
    usp = madd(port, 0, 0, 0x8232104CUL, 1, 0);
    cp = madd(usp, 0, 0, 0x8233104CUL, 1, 0);
    mcand(cp);
    fix = madd(usp, 1, 0, 0x8233104CUL, 1, 0);
    fix2 = madd(usp, 2, 0, 0x8233104CUL, 1, 0);
    a = madd(usp, 3, 0, 0x8233104CUL, 1, 0);
    m.FailWriteAt = 8;
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, XHCI_TBT_BRNUM_F_PAD, NULL, NULL, &r),
             XHCI_TBT_BRNUM_NO_ROOM, "undo failed: NO_ROOM");
    CHECK_EQ(r.PadDropped, 0, "undo failed: not made again");
    CHECK(r.UndoFailed != 0, "undo failed: counted");
    m.FailWriteAt = 0;
}

static VOID test_pad_memory(VOID)
{
    XHCI_TBT_BRNUM r;
    ULONG port, usp, fix, cp, nv, cnv, cp2;

    /* The dock in a 192 MiB port window (C0000000-CBFFFFFF): one 1 MiB
     * window for the fixed port's NVMe, the slack shared: 64 MiB (cap). */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    m.N[port].Mem = 0xCBF0C000UL;
    mdock(port, 0, &usp, &fix, &cp, &nv, &cnv);
    mnvmebars(nv);
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, XHCI_TBT_BRNUM_F_PAD | XHCI_TBT_BRNUM_F_WINDOWS,
                                  NULL, NULL, &r), XHCI_TBT_BRNUM_DONE, "mem: numbered");
    CHECK_EQ(r.WinOutcome, XHCI_TBT_BRWIN_DONE, "mem: windows written");
    CHECK_EQ(r.PadMem, 0x04000000UL, "mem: 64 MiB (the cap)");
    CHECK_EQ(r.Need[2], 0x04000000UL, "mem: the chain port's window 64 MiB");
    CHECK(wbase(m.N[cp].Mem) <= wlimit(m.N[cp].Mem), "mem: the chain port's window open");
    CHECK_EQ(r.Need[1], 0x00100000UL, "mem: the fixed port 1 MiB");

    /* Without the flag: the chain port's window closed, as today. */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    m.N[port].Mem = 0xCBF0C000UL;
    mdock(port, 0, &usp, &fix, &cp, &nv, &cnv);
    mnvmebars(nv);
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, XHCI_TBT_BRNUM_F_WINDOWS, NULL, NULL, &r),
             XHCI_TBT_BRNUM_DONE, "no mem pad: numbered");
    CHECK_EQ(r.Need[2], 0UL, "no mem pad: the chain port needs nothing");
    CHECK(wbase(m.N[cp].Mem) > wlimit(m.N[cp].Mem), "no mem pad: its window closed");

    /* A 16 MiB port window: 1 MiB used, 15 left: 8 MiB. */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    m.N[port].Mem = 0xC0F0C000UL;
    mdock(port, 0, &usp, &fix, &cp, &nv, &cnv);
    mnvmebars(nv);
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, XHCI_TBT_BRNUM_F_PAD | XHCI_TBT_BRNUM_F_WINDOWS,
                                  NULL, NULL, &r), XHCI_TBT_BRNUM_DONE, "mem 16: numbered");
    CHECK_EQ(r.PadMem, 0x00800000UL, "mem 16: 8 MiB");
    CHECK_EQ(r.WinOutcome, XHCI_TBT_BRWIN_DONE, "mem 16: written");

    /* Two candidates share it: 15 MiB / 2 = 7.5, rounded down to 4 MiB. */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    m.N[port].Mem = 0xC0F0C000UL;
    mdock(port, 0, &usp, &fix, &cp, &nv, &cnv);
    cp2 = madd(usp, 2, 0, 0x8233104CUL, 1, 0);
    mcand(cp2);
    mnvmebars(nv);
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, XHCI_TBT_BRNUM_F_PAD | XHCI_TBT_BRNUM_F_WINDOWS,
                                  NULL, NULL, &r), XHCI_TBT_BRNUM_DONE, "mem two: numbered");
    CHECK_EQ(r.PadMem, 0x00400000UL, "mem two: 4 MiB each");
    CHECK_EQ(r.WinOutcome, XHCI_TBT_BRWIN_DONE, "mem two: written");

    /* A 2 MiB port window: 1 MiB left, below 2 MiB: no memory padding. */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    m.N[port].Mem = 0xC010C000UL;
    mdock(port, 0, &usp, &fix, &cp, &nv, &cnv);
    mnvmebars(nv);
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, XHCI_TBT_BRNUM_F_PAD | XHCI_TBT_BRNUM_F_WINDOWS,
                                  NULL, NULL, &r), XHCI_TBT_BRNUM_DONE, "mem small: numbered");
    CHECK_EQ(r.PadMem, 0UL, "mem small: none");
    CHECK_EQ(r.PadNoMem, 1, "mem small: counted");
    CHECK_EQ(r.WinOutcome, XHCI_TBT_BRWIN_DONE, "mem small: the rest written");
    CHECK_EQ(r.Need[2], 0UL, "mem small: the chain port's need unpadded");

    /* A device behind the candidate: its need + M (a 16 KiB BAR, 1 MiB,
     * + 8 MiB = 9 MiB). */
    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x2A);
    m.N[port].Mem = 0xC1F0C000UL;
    mdock(port, 1, &usp, &fix, &cp, &nv, &cnv);
    mnvmebars(nv);
    mnvmebars(cnv);
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, XHCI_TBT_BRNUM_F_PAD | XHCI_TBT_BRNUM_F_WINDOWS,
                                  NULL, NULL, &r), XHCI_TBT_BRNUM_DONE, "mem behind: numbered");
    CHECK_EQ(r.PadMem, 0x01000000UL, "mem behind: 16 MiB (30 MiB left)");
    CHECK_EQ(r.Need[2], 0x01100000UL, "mem behind: its 1 MiB need + 16 MiB");
    CHECK_EQ(r.WinOutcome, XHCI_TBT_BRWIN_DONE, "mem behind: written");
}

/* One level: a dock chained behind a chain port at the first numbering
 * has its own chain port, which is not padded; a later first-level chain
 * port still is. */
static VOID test_pad_nested(VOID)
{
    XHCI_TBT_BRNUM r;
    ULONG port, usp, fix, cp, nv, cnv, cusp, ccp, cp2;

    mreset();
    ioreset(TRUE);
    port = mport(0x06, 0x16);
    mdock(port, 0, &usp, &fix, &cp, &nv, &cnv);
    cusp = madd(cp, 0, 0, 0x8232104CUL, 1, 0);
    ccp = madd(cusp, 1, 0, 0x8233104CUL, 1, 0);
    mcand(ccp);
    cp2 = madd(usp, 2, 0, 0x8233104CUL, 1, 0);
    mcand(cp2);
    CHECK_EQ(XhciTbtNumberBridges(&io, PORT, XHCI_TBT_BRNUM_F_PAD, NULL, NULL, &r),
             XHCI_TBT_BRNUM_DONE, "nested: numbered");
    CHECK_EQ(m.N[ccp].Buses, 0x000B0B0AUL, "nested: the chained dock's chain port 0A/0B/0B, not padded");
    CHECK_EQ(m.N[cp].Buses, 0x00100907UL, "nested: the first chain port 07/09/10 (11 left, pad 5)");
    CHECK_EQ(m.N[cp2].Buses, 0x00131107UL, "nested: the second 07/11/13 (5 left, pad 2)");
    CHECK_EQ(r.Padded, 2, "nested: two padded, the nested one not");
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
    test_windows();
    test_windows_more();
    test_irq();
    test_irq_nobridge();
    test_pf64();
    test_windows_order_and_stale();
    test_irq_stale();
    test_empty_bus_and_alignment();
    test_reopen();
    test_pad_buses();
    test_pad_memory();
    test_pad_nested();
    printf("test_tbt_brnum: %d checks, %d failures\n", checks, failures);
    return failures;
}
