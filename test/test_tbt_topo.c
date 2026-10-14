/*
 * test_tbt_topo.c - host tests for src\cm\tbt_topo.c (roadmap-usb4.md task
 * 39-A.5; design record 21 sections 6.3, 6.5 and 10.1).
 *
 * Driven over a modelled machine: every function's 256 bytes of
 * configuration space, and for a bridge with a Standard Hot-Plug
 * Controller its DWORD Select/DWORD Data window onto the SHPC registers.
 * The layouts:
 *
 *   - the ThinkPad P14s Gen 1 as captured under Windows 98 (tbt_topo_p14s.h:
 *     Assist off with pre-boot off and on; switch upstream 03:00.0 8086:15c0,
 *     downstream ports 04:00.0 to the NHI on bus 5, 04:01.0 the hot-plug
 *     tunnel port, 04:02.0 to the xHCI on bus 2b);
 *   - the emulator's i440FX stand-in of that switch with plain bridges
 *     (usb4-emu scripts/run-win98.sh BIOS=..., docs/WIN98-TEST.md "P14s
 *     BIOS matrix": root bridge 00:06.0 with SHPC, switch ports 01:01.0,
 *     02:00.0, 02:01.0 the SHPC tunnel bridge, 02:02.0);
 *   - the emulator's generic layouts with no switch: i440FX (NHI 00:04.0,
 *     SHPC tunnel bridge 00:05.0) and q35 (NHI 00:04.0, PCI Express
 *     tunnel root port 00:05.0), whose root-bus candidate is learned at a
 *     tunnel up rather than taken on the rule;
 *   - the emulator's q35 switch (scripts/run-linux.sh TOPOLOGY=switch),
 *     whose plain downstream ports keep QEMU's default hotplug=on, so the
 *     set holds two ports of the switch, and the override that narrows it;
 *   - an integrated host's shape (an NHI on bus 0, its tunnel ports root
 *     ports beside it with an unrelated hot-plug root port of the
 *     platform's), with no table, a table, an override list and learning;
 *   - several candidates, none, a port without the capability bits, the
 *     override, the parent search off bus 0's hierarchy, sets and their
 *     fold, and the poll.
 *
 * The emulator layouts are modelled from usb4-emu main at a68bc74 (the run
 * scripts and the device types its QEMU patch defines); their capability
 * offsets are the model's own, not read from a guest.
 *
 * C89.
 */

#include <stdio.h>
#include "../src/cm/tbt_topo.h"
#include "test_harness.h"

typedef struct _TBT_VEC_FN {
    UCHAR Bus;
    UCHAR Dev;
    UCHAR Fn;
    UCHAR Cfg[256];
} TBT_VEC_FN;

#include "tbt_topo_p14s.h"

/* ---- the modelled machine ---------------------------------------------- */

#define MODEL_MAX 64

typedef struct _MODEL_FN {
    ULONG Bdf;
    UCHAR Cfg[256];
    ULONG ShpcCap;
    ULONG Shpc[64];
} MODEL_FN;

typedef struct _MODEL {
    MODEL_FN Fn[MODEL_MAX];
    ULONG Count;
    ULONG FailBdf;          /* accesses here fail ...                       */
    ULONG FailOffset;       /* ... at this offset, or all when NO_BDF       */
    ULONG FailWrites;       /* every write fails                            */
    ULONG FailWriteAt;      /* the write with this 1-based ordinal fails    */
    ULONG StrayWrites;      /* writes anywhere but an SHPC DWORD Select     */
    ULONG Reads;
    ULONG Writes;
    ULONG MaxBusRead;       /* the highest bus number read                  */
} MODEL;

static MODEL model;

static VOID mreset(VOID)
{
    ULONG i;
    PUCHAR p = (PUCHAR)&model;

    for (i = 0; i < sizeof(model); i++) {
        p[i] = 0;
    }
    model.FailBdf = XHCI_TBT_NO_BDF;
    model.FailOffset = XHCI_TBT_NO_BDF;
}

static MODEL_FN *mfind(ULONG bdf)
{
    ULONG i;

    for (i = 0; i < model.Count; i++) {
        if (model.Fn[i].Bdf == bdf) {
            return &model.Fn[i];
        }
    }
    return NULL;
}

static ULONG mread(PVOID context, ULONG bdf, ULONG offset, PULONG value)
{
    MODEL_FN *f;
    ULONG sel;

    (void)context;
    model.Reads++;
    if (XHCI_TBT_BDF_BUS(bdf) > model.MaxBusRead) {
        model.MaxBusRead = XHCI_TBT_BDF_BUS(bdf);
    }
    if ((bdf == model.FailBdf && (model.FailOffset == XHCI_TBT_NO_BDF ||
                                  model.FailOffset == offset)) ||
        (offset & 3) != 0 || offset > 0xFC) {
        return 0;
    }
    f = mfind(bdf);
    if (f == NULL) {
        *value = 0xFFFFFFFFUL;
        return 1;
    }
    if (f->ShpcCap != 0 && offset == f->ShpcCap + XHCI_TBT_SHPC_DATA) {
        sel = f->Cfg[f->ShpcCap + XHCI_TBT_SHPC_SELECT];
        *value = sel < 64 ? f->Shpc[sel] : 0;
        return 1;
    }
    *value = (ULONG)f->Cfg[offset] | ((ULONG)f->Cfg[offset + 1] << 8) |
             ((ULONG)f->Cfg[offset + 2] << 16) |
             ((ULONG)f->Cfg[offset + 3] << 24);
    return 1;
}

static ULONG mwrite(PVOID context, ULONG bdf, ULONG offset, ULONG width,
                    ULONG value)
{
    MODEL_FN *f;

    (void)context;
    model.Writes++;
    if (bdf == model.FailBdf || model.FailWrites ||
        model.Writes == model.FailWriteAt) {
        return 0;
    }
    f = mfind(bdf);
    if (f == NULL || f->ShpcCap == 0 || width != 1 ||
        offset != f->ShpcCap + XHCI_TBT_SHPC_SELECT) {
        model.StrayWrites++;
        return 1;
    }
    f->Cfg[offset] = (UCHAR)value;
    return 1;
}

static XHCI_TBT_CFG io;

static VOID ioreset(VOID)
{
    io.Read = mread;
    io.Write = mwrite;
    io.Context = &model;
    io.Reads = 0;
    io.Writes = 0;
}

static VOID put16(MODEL_FN *f, ULONG off, ULONG v)
{
    f->Cfg[off] = (UCHAR)(v & 0xFF);
    f->Cfg[off + 1] = (UCHAR)((v >> 8) & 0xFF);
}

static VOID put32(MODEL_FN *f, ULONG off, ULONG v)
{
    put16(f, off, v & 0xFFFF);
    put16(f, off + 2, (v >> 16) & 0xFFFF);
}

static ULONG get32(MODEL_FN *f, ULONG off)
{
    return (ULONG)f->Cfg[off] | ((ULONG)f->Cfg[off + 1] << 8) |
           ((ULONG)f->Cfg[off + 2] << 16) | ((ULONG)f->Cfg[off + 3] << 24);
}

static VOID mload(const TBT_VEC_FN *v, ULONG count)
{
    ULONG i;
    ULONG j;
    MODEL_FN *f;

    for (i = 0; i < count; i++) {
        f = &model.Fn[model.Count++];
        f->Bdf = XHCI_TBT_BDF(v[i].Bus, v[i].Dev, v[i].Fn);
        for (j = 0; j < 256; j++) {
            f->Cfg[j] = v[i].Cfg[j];
        }
    }
}

/* An endpoint (type 0) function. */
static MODEL_FN *mdev(ULONG b, ULONG d, ULONG fn, ULONG vid, ULONG did,
                      ULONG classCode)
{
    MODEL_FN *f = &model.Fn[model.Count++];

    f->Bdf = XHCI_TBT_BDF(b, d, fn);
    put16(f, 0x00, vid);
    put16(f, 0x02, did);
    put32(f, 0x08, classCode << 8);
    return f;
}

/* A type 1 function with its bus numbers. */
static MODEL_FN *mbridge(ULONG b, ULONG d, ULONG fn, ULONG vid, ULONG did,
                         ULONG sec, ULONG sub)
{
    MODEL_FN *f = mdev(b, d, fn, vid, did, 0x060400UL);

    f->Cfg[0x0E] = 0x01;
    f->Cfg[0x18] = (UCHAR)b;
    f->Cfg[0x19] = (UCHAR)sec;
    f->Cfg[0x1A] = (UCHAR)sub;
    return f;
}

static VOID mmulti(ULONG b, ULONG d)
{
    MODEL_FN *f = mfind(XHCI_TBT_BDF(b, d, 0));

    if (f != NULL) {
        f->Cfg[0x0E] |= 0x80;
    }
}

/* Appends a capability with `id` at `off` to the function's list. */
static VOID mcap(MODEL_FN *f, ULONG off, ULONG id)
{
    ULONG p;

    f->Cfg[0x06] |= 0x10;
    f->Cfg[off] = (UCHAR)id;
    f->Cfg[off + 1] = 0;
    if (f->Cfg[0x34] == 0) {
        f->Cfg[0x34] = (UCHAR)off;
        return;
    }
    p = f->Cfg[0x34];
    while (f->Cfg[p + 1] != 0) {
        p = f->Cfg[p + 1];
    }
    f->Cfg[p + 1] = (UCHAR)off;
}

/* A PCI Express capability (version 2) at `off`. */
static VOID mpcie(MODEL_FN *f, ULONG off, ULONG type, ULONG slotImpl,
                  ULONG linkCap, ULONG linkSta, ULONG slotCap,
                  ULONG slotSta)
{
    mcap(f, off, XHCI_TBT_CAP_PCIE);
    put16(f, off + 2, 0x2UL | (type << 4) |
                      (slotImpl ? XHCI_TBT_PCIE_SLOT_IMPL : 0));
    put32(f, off + XHCI_TBT_PCIE_LINKCAP, linkCap);
    put16(f, off + 0x12, linkSta);
    put32(f, off + XHCI_TBT_PCIE_SLOTCAP, slotCap);
    put16(f, off + 0x1A, slotSta);
}

#define SHPC_EMPTY  0x00000C03UL    /* PRSNT 11b, slot state Disabled       */
#define SHPC_CARD   0x00000402UL    /* PRSNT 01b, slot state Enabled        */

/* An SHPC capability at `off` with `slots` slots, all empty. */
static VOID mshpc(MODEL_FN *f, ULONG off, ULONG slots, ULONG select)
{
    ULONG n;

    mcap(f, off, XHCI_TBT_CAP_SHPC);
    f->ShpcCap = off;
    f->Cfg[off + XHCI_TBT_SHPC_SELECT] = (UCHAR)select;
    f->Shpc[XHCI_TBT_SHPC_SLOTCFG] = slots | (1UL << 8);
    for (n = 0; n < slots; n++) {
        f->Shpc[XHCI_TBT_SHPC_SLOT0 + n] = SHPC_EMPTY;
    }
}

/* QEMU's slot-identification capability on a pci-bridge with chassis_nr. */
static VOID mslotid(MODEL_FN *f, ULONG off)
{
    mcap(f, off, 0x04);
}

static VOID wtext(const char *s, WCHAR *w, ULONG cap)
{
    ULONG i;

    for (i = 0; i + 1 < cap && s[i] != 0; i++) {
        w[i] = (WCHAR)(UCHAR)s[i];
    }
    w[i] = 0;
}

static ULONG list[XHCI_TBT_MAX_TUNNELS + 1];

/* A value of exactly one location. */
static ULONG parse(const char *s, PULONG bdf)
{
    WCHAR w[64];
    ULONG n;

    wtext(s, w, 64);
    n = XhciTbtParseBdfList(w, 64, list, XHCI_TBT_MAX_TUNNELS);
    *bdf = n == 1 ? list[0] : XHCI_TBT_NO_BDF;
    return n == 1 ? 1UL : 0UL;
}

static ULONG parsen(const char *s)
{
    WCHAR w[64];

    wtext(s, w, 64);
    return XhciTbtParseBdfList(w, 64, list, XHCI_TBT_MAX_TUNNELS);
}

static XHCI_TBT_TOPO topo;
static XHCI_TBT_PORT port;

static XHCI_TBT_TOPO_IN in;

static ULONG findin(ULONG nhi, const ULONG *over, ULONG overs,
                    const XHCI_TBT_TABLE_ENTRY *table, ULONG tableCount)
{
    ULONG i;

    ioreset();
    in.Nhi = nhi;
    in.Overrides = overs;
    for (i = 0; i < XHCI_TBT_MAX_TUNNELS; i++) {
        in.Override[i] = i < overs ? over[i] : 0;
    }
    in.Table = table;
    in.TableCount = tableCount;
    return XhciTbtTopoFind(&io, &in, &topo);
}

static ULONG find(ULONG nhi, ULONG override)
{
    return findin(nhi, &override, override == XHCI_TBT_NO_BDF ? 0UL : 1UL,
                  NULL, 0);
}

static const XHCI_TBT_BRIDGE *sibling(ULONG bdf)
{
    ULONG i;

    for (i = 0; i < topo.Siblings && i < XHCI_TBT_MAX_SIBLINGS; i++) {
        if (topo.Sibling[i].Bdf == bdf) {
            return &topo.Sibling[i];
        }
    }
    return NULL;
}

#define B XHCI_TBT_BDF

/* ---- decoders ---------------------------------------------------------- */

static void test_decoders(void)
{
    ULONG valid;
    ULONG active;
    ULONG present;
    ULONG changed;
    ULONG dll;

    /* The P14s tunnel port 04:01.0: PCIe Capabilities 0162h, Link
     * Capabilities 01715C41h, Slot Capabilities 000C0060h. */
    CHECK_EQ(XhciTbtPcieDownstream(0x0162), 1, "type 6 is downstream");
    CHECK_EQ(XhciTbtPcieDownstream(0x0142), 1, "type 4 is downstream");
    CHECK_EQ(XhciTbtPcieDownstream(0x0052), 0, "type 5 is not");
    CHECK_EQ(XhciTbtPcieDownstream(0x0172), 0, "type 7 is not");
    CHECK_EQ(XhciTbtPcieSlotImplemented(0x0162), 1, "slot implemented");
    CHECK_EQ(XhciTbtPcieSlotImplemented(0x0062), 0, "slot bit clear");
    CHECK_EQ(XhciTbtPcieSlotImplemented(0x0152), 0,
             "slot bit on an upstream port means nothing");
    CHECK_EQ(XhciTbtPcieHotPlug(0x0162, 0x000C0060UL), 1, "P14s 04:01.0 HPC");
    CHECK_EQ(XhciTbtPcieHotPlug(0x0162, 0x00040000UL), 0, "P14s 04:00.0 not");
    CHECK_EQ(XhciTbtPcieHotPlug(0x0062, 0x000C0060UL), 0,
             "HPC without an implemented slot");

    XhciTbtDecodeLink(0x0162, 0x01715C41UL, 0x1041, &valid, &active);
    CHECK(valid == 1 && active == 0, "P14s OFF_OFF link down, reported");
    XhciTbtDecodeLink(0x0162, 0x01715C41UL, 0x3041, &valid, &active);
    CHECK(valid == 1 && active == 1, "P14s OFF_ON link active");
    XhciTbtDecodeLink(0x0162, 0x00615C41UL, 0x3041, &valid, &active);
    CHECK(valid == 0 && active == 0, "no DLLLARC: no reading");
    XhciTbtDecodeLink(0x0052, 0x01715C41UL, 0x3041, &valid, &active);
    CHECK(valid == 0 && active == 0, "upstream port: no reading");

    XhciTbtDecodeSlot(0x0162, 0x0000, &valid, &present, &changed, &dll);
    CHECK(valid == 1 && present == 0 && changed == 0 && dll == 0,
          "P14s OFF_OFF slot empty");
    XhciTbtDecodeSlot(0x0162, 0x0148, &valid, &present, &changed, &dll);
    CHECK(valid == 1 && present == 1 && changed == 1 && dll == 1,
          "P14s OFF_ON slot present, PDC and DLLSC");
    XhciTbtDecodeSlot(0x0062, 0x0148, &valid, &present, &changed, &dll);
    CHECK(valid == 0 && present == 0 && changed == 0 && dll == 0,
          "no slot: no reading");

    CHECK_EQ(XhciTbtShpcSlotPresent(SHPC_EMPTY), 0, "SHPC empty");
    CHECK_EQ(XhciTbtShpcSlotPresent(SHPC_CARD), 1, "SHPC 25 W card");
    CHECK_EQ(XhciTbtShpcSlotPresent(0x00000002UL), 1, "SHPC 7.5 W card");
    CHECK_EQ(XhciTbtShpcSlotPresent(0x00000802UL), 1, "SHPC 15 W card");
}

/* ---- capability walk --------------------------------------------------- */

static void test_findcap(void)
{
    ULONG off;
    MODEL_FN *f;

    mreset();
    mload(tbt_p14s_off_off, sizeof(tbt_p14s_off_off) /
                            sizeof(tbt_p14s_off_off[0]));
    ioreset();
    CHECK(XhciTbtFindCap(&io, B(4, 1, 0), XHCI_TBT_CAP_PCIE, &off) &&
          off == 0xC0, "P14s 04:01.0 PCIe at C0h past PM, MSI, SSID");
    CHECK(XhciTbtFindCap(&io, B(0, 0x1C, 4), XHCI_TBT_CAP_PCIE, &off) &&
          off == 0x40, "P14s 00:1c.4 PCIe at 40h");
    CHECK(!XhciTbtFindCap(&io, B(4, 1, 0), XHCI_TBT_CAP_SHPC, &off) &&
          off == 0, "P14s 04:01.0 has no SHPC");
    CHECK(!XhciTbtFindCap(&io, B(9, 9, 0), XHCI_TBT_CAP_PCIE, &off),
          "absent function");

    mreset();
    f = mdev(1, 0, 0, 0x1234, 0x5678, 0x010000UL);
    ioreset();
    CHECK(!XhciTbtFindCap(&io, B(1, 0, 0), XHCI_TBT_CAP_PCIE, &off),
          "no capabilities list bit");
    f->Cfg[0x06] = 0x10;
    f->Cfg[0x34] = 0x40;
    f->Cfg[0x40] = 0x01;
    f->Cfg[0x41] = 0x40;
    CHECK(!XhciTbtFindCap(&io, B(1, 0, 0), XHCI_TBT_CAP_PCIE, &off),
          "a list that loops ends");
    CHECK_EQ(model.Reads, 1 + 2 + 48, "the loop costs 48 reads, no more");
    f->Cfg[0x41] = 0x3C;
    CHECK(!XhciTbtFindCap(&io, B(1, 0, 0), XHCI_TBT_CAP_PCIE, &off),
          "a pointer below 40h ends the walk");
    f->Cfg[0x41] = 0x53;
    f->Cfg[0x50] = 0x10;
    CHECK(XhciTbtFindCap(&io, B(1, 0, 0), XHCI_TBT_CAP_PCIE, &off) &&
          off == 0x50, "a pointer's reserved bits are masked");
    model.FailBdf = B(1, 0, 0);
    CHECK(!XhciTbtFindCap(&io, B(1, 0, 0), XHCI_TBT_CAP_PCIE, &off),
          "a failed access finds nothing");
    CHECK(!XhciTbtFindCap(NULL, B(1, 0, 0), XHCI_TBT_CAP_PCIE, &off),
          "no accessors");
}

/* ---- the P14s, captured ------------------------------------------------ */

static void test_p14s(void)
{
    const XHCI_TBT_BRIDGE *br;
    ULONG pass;

    for (pass = 0; pass < 2; pass++) {
        mreset();
        if (pass == 0) {
            mload(tbt_p14s_off_off, sizeof(tbt_p14s_off_off) /
                                    sizeof(tbt_p14s_off_off[0]));
        } else {
            mload(tbt_p14s_off_on, sizeof(tbt_p14s_off_on) /
                                   sizeof(tbt_p14s_off_on[0]));
        }
        CHECK_EQ(find(B(5, 0, 0), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_SET,
                 "P14s: one tunnel bridge");
        CHECK_EQ(topo.Parent, B(4, 0, 0), "P14s: the NHI's parent 04:00.0");
        CHECK_EQ(topo.SearchBus, 4, "P14s: siblings on bus 4");
        CHECK_EQ(topo.Tunnel, B(4, 1, 0), "P14s: the tunnel 04:01.0");
        CHECK_EQ(topo.Siblings, 2, "P14s: 04:01.0 and 04:02.0");
        CHECK_EQ(topo.Candidates, 1, "P14s: one candidate");
        CHECK(topo.Tunnels == 1 && topo.Source[0] == XHCI_TBT_SRC_SWITCH &&
              topo.Switch == 1, "P14s: verified by the switch");
        CHECK(topo.ParentType == XHCI_TBT_PCIE_TYPE_DOWN &&
              topo.Upstream == B(3, 0, 0) &&
              topo.UpstreamType == XHCI_TBT_PCIE_TYPE_UP,
              "P14s: downstream port under upstream port 03:00.0");
        CHECK_EQ(topo.Pick.Bdf, B(4, 1, 0), "P14s: the pick's reading");
        CHECK_EQ(topo.Pick.Id, 0x15C08086UL, "P14s: 8086:15c0");
        CHECK_EQ(topo.Pick.Flags,
                 XHCI_TBT_BR_PCIE | XHCI_TBT_BR_SLOT | XHCI_TBT_BR_HOTPLUG |
                 XHCI_TBT_BR_SURPRISE | XHCI_TBT_BR_LINKREPORT |
                 XHCI_TBT_BR_CANDIDATE,
                 "P14s: hot-plug and surprise capable, link reported");
        CHECK_EQ((topo.Pick.Buses >> 8) & 0xFFFF, 0x2A06,
                 "P14s: tunnel buses 06-2a");
        br = sibling(B(4, 2, 0));
        CHECK(br != NULL && br->Flags == (XHCI_TBT_BR_PCIE | XHCI_TBT_BR_SLOT),
              "P14s: 04:02.0 a slot without hot-plug");
        CHECK(sibling(B(4, 0, 0)) == NULL, "P14s: the parent is no sibling");
        CHECK_EQ(io.Writes, 0, "P14s: discovery writes nothing");
        CHECK(model.MaxBusRead <= 0x2E,
              "P14s: the walk reads only buses that exist");

        ioreset();
        CHECK_EQ(XhciTbtPortRead(&io, B(4, 1, 0), &port),
                 pass == 0 ? XHCI_TBT_PORT_DOWN : XHCI_TBT_PORT_UP,
                 "P14s: the tunnel port's reading");
        CHECK(port.LinkValid == 1 && port.SlotValid == 1 &&
              port.ShpcCap == 0 && port.PcieCap == 0xC0,
              "P14s: link and slot readings, no SHPC");
        CHECK_EQ(port.LinkActive, pass, "P14s: Link Active");
        CHECK_EQ(port.Presence, pass, "P14s: Presence Detect State");
        CHECK_EQ(port.DllChanged, pass, "P14s: DLL State Changed latched");
        CHECK_EQ(port.LinkStatus, pass == 0 ? 0x1041 : 0x3041,
                 "P14s: Link Status as captured");
        CHECK_EQ(port.SlotStatus, pass == 0 ? 0x0000 : 0x0148,
                 "P14s: Slot Status as captured");

        /* The NHI's own port: a slot with a card and no Link Active
         * report, so presence alone. The upstream port: nothing. */
        CHECK_EQ(XhciTbtPortRead(&io, B(4, 0, 0), &port), XHCI_TBT_PORT_UP,
                 "P14s: 04:00.0 present by Slot Status alone");
        CHECK(port.LinkValid == 0 && port.SlotValid == 1,
              "P14s: 04:00.0 reports no Link Active");
        CHECK_EQ(XhciTbtPortRead(&io, B(3, 0, 0), &port), XHCI_TBT_PORT_NONE,
                 "P14s: the upstream port has nothing to read");
    }

    /* The override on the captured machine. */
    mreset();
    mload(tbt_p14s_off_off, sizeof(tbt_p14s_off_off) /
                            sizeof(tbt_p14s_off_off[0]));
    CHECK_EQ(find(B(5, 0, 0), B(4, 2, 0)), XHCI_TBT_TOPO_OVERRIDE,
             "P14s: the override wins over a unique pick");
    CHECK_EQ(topo.Tunnel, B(4, 2, 0), "P14s: overridden to 04:02.0");
    CHECK_EQ(topo.Candidates, 1, "P14s: the discovery still ran");
    CHECK_EQ(topo.Pick.Bdf, B(4, 2, 0), "P14s: the pick is the override's");
    CHECK_EQ(find(B(5, 0, 0), B(5, 0, 0)), XHCI_TBT_TOPO_OVERRIDE_BAD,
             "P14s: an override naming the NHI is refused");
    CHECK_EQ(topo.Tunnel, XHCI_TBT_NO_BDF, "P14s: no fallback");
    CHECK_EQ(find(B(5, 0, 0), B(4, 3, 0)), XHCI_TBT_TOPO_OVERRIDE_BAD,
             "P14s: an override naming nothing is refused");
    CHECK_EQ(find(B(5, 0, 1), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_NHI_ABSENT,
             "P14s: nothing at the NHI's location");
    CHECK_EQ(find(0x10000UL, XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_BAD_PARAM,
             "a location past 16 bits");
    find(B(5, 0, 0), XHCI_TBT_NO_BDF);
    CHECK_EQ(XhciTbtTopoFind(NULL, &in, &topo),
             XHCI_TBT_TOPO_BAD_PARAM, "no accessors");
    CHECK_EQ(XhciTbtTopoFind(&io, &in, NULL),
             XHCI_TBT_TOPO_BAD_PARAM, "no result");
    CHECK_EQ(XhciTbtTopoFind(&io, NULL, &topo),
             XHCI_TBT_TOPO_BAD_PARAM, "no question");
    CHECK(topo.Tunnel == XHCI_TBT_NO_BDF && topo.Tunnels == 0,
          "and no tunnel bridge");
    in.Overrides = XHCI_TBT_MAX_TUNNELS + 1;
    CHECK_EQ(XhciTbtTopoFind(&io, &in, &topo),
             XHCI_TBT_TOPO_BAD_PARAM, "too many overrides");
    in.Overrides = 1;
    in.Override[0] = 0x10000UL;
    CHECK_EQ(XhciTbtTopoFind(&io, &in, &topo),
             XHCI_TBT_TOPO_BAD_PARAM, "an override past 16 bits");
}

/* ---- the emulator's layouts -------------------------------------------- */

/* scripts/run-win98.sh with BIOS=<Assist>_<PreBoot> on i440FX. */
static VOID emu_i440fx_switch(ULONG assist, ULONG nvme)
{
    MODEL_FN *f;

    mreset();
    mdev(0, 0, 0, 0x8086, 0x1237, 0x060000UL);
    mdev(0, 1, 0, 0x8086, 0x7000, 0x060100UL);
    mdev(0, 1, 1, 0x8086, 0x7010, 0x010180UL);
    mdev(0, 1, 3, 0x8086, 0x7113, 0x068000UL);
    mmulti(0, 1);
    mdev(0, 2, 0, 0x1234, 0x1111, 0x030000UL);
    /* usb4-tunnel-bridge, the gated root bridge: SHPC as any pci-bridge */
    f = mbridge(0, 6, 0, 0x1B36, 0x0001, 1, 5);
    mshpc(f, 0x48, 31, 0);
    mslotid(f, 0x40);
    f = mbridge(1, 1, 0, 0x8086, 0x15C0, 2, 5);         /* upstream, shpc=off */
    mslotid(f, 0x40);
    f = mbridge(2, 0, 0, 0x8086, 0x15C0, 3, 3);         /* to the NHI */
    mslotid(f, 0x40);
    f = mbridge(2, 1, 0, 0x8086, 0x15C0, 4, 4);         /* the tunnel */
    mshpc(f, 0x48, 31, 0);
    mslotid(f, 0x40);
    f = mbridge(2, 2, 0, 0x8086, 0x15C0, 5, 5);         /* to the xHCI */
    mslotid(f, 0x40);
    mdev(3, 0, 0, 0x8086, 0x15BF, 0x088000UL);
    if (!assist) {
        mdev(5, 0, 0, 0x8086, 0x15C1, 0x0C0330UL);
    }
    if (nvme) {
        mdev(4, 1, 0, 0x1B36, 0x0010, 0x010802UL);
        mfind(B(2, 1, 0))->Shpc[XHCI_TBT_SHPC_SLOT0] = SHPC_CARD;
    }
}

static void test_emu_i440fx(void)
{
    const XHCI_TBT_BRIDGE *br;
    MODEL_FN *t;
    ULONG pass;

    for (pass = 0; pass < 4; pass++) {
        emu_i440fx_switch(pass & 1, (pass >> 1) & 1);
        /* Conventional bridges carry no switch evidence: learned. */
        CHECK_EQ(find(B(3, 0, 0), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_LEARN,
                 "i440FX switch: a candidate, not verified");
        CHECK_EQ(topo.Parent, B(2, 0, 0), "i440FX switch: parent 02:00.0");
        CHECK(topo.Switch == 0 && topo.ParentType == 0 &&
              topo.Upstream == B(1, 1, 0), "i440FX switch: no PCIe types");
        CHECK(topo.Candidates == 1 && topo.Cand[0] == B(2, 1, 0),
              "i440FX switch: 02:01.0 the candidate");
        CHECK_EQ(topo.Siblings, 2, "i440FX switch: two siblings");
        CHECK_EQ(sibling(B(2, 1, 0))->Flags,
                 XHCI_TBT_BR_SHPC | XHCI_TBT_BR_CANDIDATE,
                 "i440FX switch: by its SHPC");
        XhciTbtLearnArm(&io, &topo);
        CHECK_EQ(topo.LearnArmed, 1, "i440FX switch: armed");
        mfind(B(2, 1, 0))->Shpc[XHCI_TBT_SHPC_SLOT0] = SHPC_CARD;
        if ((pass >> 1) & 1) {
            CHECK_EQ(XhciTbtLearn(&io, &topo), 0,
                     "i440FX switch: a slot full before is not learned");
        } else {
            CHECK_EQ(XhciTbtLearn(&io, &topo), 1,
                     "i440FX switch: learned at the tunnel up");
            CHECK(topo.Tunnel == B(2, 1, 0) &&
                  topo.Source[0] == XHCI_TBT_SRC_LEARNED,
                  "i440FX switch: 02:01.0, learned");
        }
        br = sibling(B(2, 2, 0));
        CHECK(br != NULL && br->Flags == 0,
              "i440FX switch: the shpc=off stand-in carries nothing");
    }

    /* The tunnel bridge's slot 0 (device 1), empty and then a card. */
    emu_i440fx_switch(0, 0);
    t = mfind(B(2, 1, 0));
    t->Cfg[0x48 + XHCI_TBT_SHPC_SELECT] = 0x05;
    ioreset();
    CHECK_EQ(XhciTbtPortRead(&io, B(2, 1, 0), &port), XHCI_TBT_PORT_DOWN,
             "i440FX: SHPC slots empty");
    CHECK(port.ShpcValid == 1 && port.ShpcSlots == 31 &&
          port.ShpcPresent == 0 && port.LinkValid == 0 &&
          port.SlotValid == 0 && port.PcieCap == 0,
          "i440FX: the SHPC reading alone");
    CHECK_EQ(t->Cfg[0x48 + XHCI_TBT_SHPC_SELECT], 0x05,
             "i440FX: DWORD Select put back");
    CHECK_EQ(model.StrayWrites, 0, "i440FX: no write but DWORD Select");
    CHECK_EQ(io.Writes, 1 + 31 + 1, "i440FX: select, each slot, restore");
    t->Shpc[XHCI_TBT_SHPC_SLOT0] = SHPC_CARD;
    CHECK_EQ(XhciTbtPortRead(&io, B(2, 1, 0), &port), XHCI_TBT_PORT_UP,
             "i440FX: a card in slot 0");
    CHECK(port.ShpcPresent == 1 && port.ShpcFirst == 1,
          "i440FX: slot 0 is the first present");
    t->Shpc[XHCI_TBT_SHPC_SLOT0] = SHPC_EMPTY;
    t->Shpc[XHCI_TBT_SHPC_SLOT0 + 4] = SHPC_CARD;
    CHECK_EQ(XhciTbtPortRead(&io, B(2, 1, 0), &port), XHCI_TBT_PORT_UP,
             "i440FX: a card in another slot counts");
    CHECK_EQ(port.ShpcFirst, 5, "i440FX: slot 4 first");

    /* Without a write accessor no SHPC register is read. */
    t->Shpc[XHCI_TBT_SHPC_SLOT0 + 4] = SHPC_EMPTY;
    ioreset();
    io.Write = NULL;
    CHECK_EQ(XhciTbtPortRead(&io, B(2, 1, 0), &port), XHCI_TBT_PORT_NONE,
             "i440FX: no Write, no SHPC reading");
    CHECK(port.ShpcCap == 0x48 && port.ShpcValid == 0,
          "i440FX: the capability is still named");
    CHECK_EQ(io.Writes, 0, "i440FX: nothing written");

    /* A failed write is an error, and the select byte is still restored. */
    ioreset();
    model.FailWrites = 1;
    CHECK_EQ(XhciTbtPortRead(&io, B(2, 1, 0), &port), XHCI_TBT_PORT_ERROR,
             "i440FX: a failed select write");
    model.FailWrites = 0;

    /* An SHPC with no slots has nothing to read. */
    t->Shpc[XHCI_TBT_SHPC_SLOTCFG] = 0;
    CHECK_EQ(XhciTbtPortRead(&io, B(2, 1, 0), &port), XHCI_TBT_PORT_NONE,
             "i440FX: zero slots");
}

/* scripts/run-win98.sh without BIOS, and run-linux.sh TOPOLOGY=bridge. */
static void test_emu_generic(void)
{
    MODEL_FN *f;
    MODEL_FN *t;

    /* i440FX: usb4-host 00:04.0, usb4-tunnel-bridge 00:05.0 */
    mreset();
    mdev(0, 0, 0, 0x8086, 0x1237, 0x060000UL);
    mdev(0, 1, 0, 0x8086, 0x7000, 0x060100UL);
    mdev(0, 1, 1, 0x8086, 0x7010, 0x010180UL);
    mmulti(0, 1);
    mdev(0, 4, 0, 0x1B36, 0x00F4, 0x0C0340UL);
    f = mbridge(0, 5, 0, 0x1B36, 0x0001, 1, 1);
    mshpc(f, 0x48, 31, 0);
    mslotid(f, 0x40);
    CHECK_EQ(find(B(0, 4, 0), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_LEARN,
             "i440FX generic: a root-bus candidate is not taken on the rule");
    CHECK_EQ(topo.Parent, XHCI_TBT_NO_BDF, "i440FX generic: a root bus");
    CHECK_EQ(topo.Switch, 0, "i440FX generic: no switch");
    CHECK_EQ(topo.SearchBus, 0, "i440FX generic: bus 0's bridges");
    CHECK(topo.Candidates == 1 && topo.Tunnels == 0 &&
          topo.Tunnel == XHCI_TBT_NO_BDF, "i440FX generic: nothing verified");
    CHECK_EQ(model.MaxBusRead, 0, "i440FX generic: bus 0 alone is read");

    /* Learned at the first tunnel up: the SHPC slot fills. */
    XhciTbtLearnArm(&io, &topo);
    CHECK_EQ(topo.LearnArmed, 1, "i440FX generic: armed");
    CHECK_EQ(topo.Before[0], XHCI_TBT_PORT_DOWN, "i440FX generic: empty");
    f->Shpc[XHCI_TBT_SHPC_SLOT0] = SHPC_CARD;
    CHECK_EQ(XhciTbtLearn(&io, &topo), 1, "i440FX generic: learned");
    CHECK(topo.Outcome == XHCI_TBT_TOPO_SET && topo.Tunnels == 1 &&
          topo.Set[0] == B(0, 5, 0) &&
          topo.Source[0] == XHCI_TBT_SRC_LEARNED,
          "i440FX generic: 00:05.0, learned");
    CHECK_EQ(topo.LearnArmed, 0, "i440FX generic: disarmed");
    CHECK_EQ(XhciTbtLearn(&io, &topo), 0, "i440FX generic: not armed again");
    CHECK_EQ(XhciTbtSetRead(&io, &topo, XHCI_TBT_WANT_UP), XHCI_TBT_PORT_UP,
             "i440FX generic: the set reads up");
    CHECK_EQ(topo.Last[0], XHCI_TBT_PORT_UP, "i440FX generic: per-port state");
    XhciTbtLearnArm(&io, &topo);
    CHECK_EQ(XhciTbtLearn(&io, &topo), 0,
             "i440FX generic: a port already learned is not learned twice");
    CHECK_EQ(topo.Tunnels, 1, "i440FX generic: still one");

    /* q35: usb4-host 00:04.0, usb4-tunnel-port 00:05.0 (pcie-root-port) */
    mreset();
    mdev(0, 0, 0, 0x8086, 0x29C0, 0x060000UL);
    mdev(0, 0x1F, 0, 0x8086, 0x2918, 0x060100UL);
    mdev(0, 0x1F, 2, 0x8086, 0x2922, 0x010601UL);
    mdev(0, 0x1F, 3, 0x8086, 0x2930, 0x0C0500UL);
    mmulti(0, 0x1F);
    mdev(0, 4, 0, 0x8086, 0x15BF, 0x088000UL);
    f = mbridge(0, 5, 0, 0x1B36, 0x000C, 1, 1);
    mpcie(f, 0x40, XHCI_TBT_PCIE_TYPE_ROOT, 1, XHCI_TBT_LINKCAP_DLLLARC, 0,
          XHCI_TBT_SLOTCAP_HPC | XHCI_TBT_SLOTCAP_HPS, 0);
    CHECK_EQ(find(B(0, 4, 0), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_LEARN,
             "q35 generic: one candidate root port, to be learned");
    CHECK_EQ(topo.Sibling[0].Flags,
             XHCI_TBT_BR_PCIE | XHCI_TBT_BR_SLOT | XHCI_TBT_BR_HOTPLUG |
             XHCI_TBT_BR_SURPRISE | XHCI_TBT_BR_LINKREPORT |
             XHCI_TBT_BR_CANDIDATE, "q35 generic: a hot-plug root port");
    ioreset();
    CHECK_EQ(XhciTbtPortRead(&io, B(0, 5, 0), &port), XHCI_TBT_PORT_DOWN,
             "q35 generic: empty");
    put16(f, 0x40 + 0x12, XHCI_TBT_LINKSTA_DLLLA);
    CHECK_EQ(XhciTbtPortRead(&io, B(0, 5, 0), &port), XHCI_TBT_PORT_MIXED,
             "q35 generic: link up before presence");
    put16(f, 0x40 + 0x1A, XHCI_TBT_SLOTSTA_PDS);
    CHECK_EQ(XhciTbtPortRead(&io, B(0, 5, 0), &port), XHCI_TBT_PORT_UP,
             "q35 generic: both up");

    /* A second hot-plug root port on bus 0 (a NIC's). The tunnel port
     * is learned as the one that comes up; the NIC's, present all along,
     * is not. */
    put16(f, 0x40 + 0x12, 0);
    put16(f, 0x40 + 0x1A, 0);
    t = mbridge(0, 6, 0, 0x1B36, 0x000C, 2, 2);
    mpcie(t, 0x40, XHCI_TBT_PCIE_TYPE_ROOT, 1, 0, 0,
          XHCI_TBT_SLOTCAP_HPC | XHCI_TBT_SLOTCAP_HPS, XHCI_TBT_SLOTSTA_PDS);
    CHECK_EQ(find(B(0, 4, 0), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_LEARN,
             "q35 generic + a second root port: two candidates");
    CHECK_EQ(topo.Candidates, 2, "two counted");
    CHECK_EQ(topo.Tunnel, XHCI_TBT_NO_BDF, "no tunnel bridge");
    CHECK_EQ(topo.Pick.Bdf, 0, "no pick kept");
    XhciTbtLearnArm(&io, &topo);
    CHECK_EQ(XhciTbtLearn(&io, &topo), 0, "nothing changed: nothing learned");
    XhciTbtLearnArm(&io, &topo);
    put16(f, 0x40 + 0x12, XHCI_TBT_LINKSTA_DLLLA);
    put16(f, 0x40 + 0x1A, XHCI_TBT_SLOTSTA_PDS);
    CHECK_EQ(XhciTbtLearn(&io, &topo), 1, "the port that came up");
    CHECK(topo.Tunnels == 1 && topo.Set[0] == B(0, 5, 0),
          "learned 00:05.0, not the NIC's");
    /* Two coming up at once cannot be told apart. */
    CHECK_EQ(find(B(0, 4, 0), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_LEARN,
             "again");
    put16(f, 0x40 + 0x1A, 0);
    put16(f, 0x40 + 0x12, 0);
    put16(t, 0x40 + 0x1A, 0);
    XhciTbtLearnArm(&io, &topo);
    put16(f, 0x40 + 0x12, XHCI_TBT_LINKSTA_DLLLA);
    put16(f, 0x40 + 0x1A, XHCI_TBT_SLOTSTA_PDS);
    put16(t, 0x40 + 0x1A, XHCI_TBT_SLOTSTA_PDS);
    CHECK_EQ(XhciTbtLearn(&io, &topo), 0, "two at once: none");
    CHECK_EQ(topo.Tunnels, 0, "the set still empty");
    CHECK_EQ(find(B(0, 4, 0), B(0, 5, 0)), XHCI_TBT_TOPO_OVERRIDE,
             "the override resolves it");
    CHECK_EQ(topo.Tunnel, B(0, 5, 0), "to 00:05.0");
}

/* scripts/run-linux.sh TOPOLOGY=switch on q35. */
static VOID emu_q35_switch(ULONG dspHotplug)
{
    MODEL_FN *f;
    ULONG hp = XHCI_TBT_SLOTCAP_HPC | XHCI_TBT_SLOTCAP_HPS;
    ULONG plain = dspHotplug ? hp : 0;

    mreset();
    mdev(0, 0, 0, 0x8086, 0x29C0, 0x060000UL);
    mdev(0, 0x1F, 0, 0x8086, 0x2918, 0x060100UL);
    mmulti(0, 0x1F);
    mdev(0, 0x1F, 2, 0x8086, 0x2922, 0x010601UL);
    f = mbridge(0, 0x1C, 0, 0x1B36, 0x000C, 1, 5);     /* usb4-tunnel-port */
    mpcie(f, 0x40, XHCI_TBT_PCIE_TYPE_ROOT, 1, XHCI_TBT_LINKCAP_DLLLARC,
          XHCI_TBT_LINKSTA_DLLLA, hp, XHCI_TBT_SLOTSTA_PDS);
    f = mbridge(1, 0, 0, 0x8086, 0x15C0, 2, 5);         /* usb4-tb3-usp */
    mpcie(f, 0x80, XHCI_TBT_PCIE_TYPE_UP, 0, 0, 0, 0, 0);
    f = mbridge(2, 0, 0, 0x8086, 0x15C0, 3, 3);         /* usb4-tb3-dsp */
    mpcie(f, 0x80, XHCI_TBT_PCIE_TYPE_DOWN, 1, XHCI_TBT_LINKCAP_DLLLARC,
          XHCI_TBT_LINKSTA_DLLLA, plain, XHCI_TBT_SLOTSTA_PDS);
    f = mbridge(2, 1, 0, 0x8086, 0x15C0, 4, 4);         /* tunnel-dsp */
    mpcie(f, 0x80, XHCI_TBT_PCIE_TYPE_DOWN, 1, XHCI_TBT_LINKCAP_DLLLARC,
          0, hp, 0);
    f = mbridge(2, 2, 0, 0x8086, 0x15C0, 5, 5);         /* usb4-tb3-dsp */
    mpcie(f, 0x80, XHCI_TBT_PCIE_TYPE_DOWN, 1, XHCI_TBT_LINKCAP_DLLLARC,
          XHCI_TBT_LINKSTA_DLLLA, plain, XHCI_TBT_SLOTSTA_PDS);
    mdev(3, 0, 0, 0x8086, 0x15BF, 0x088000UL);
    mdev(5, 0, 0, 0x8086, 0x15C1, 0x0C0330UL);
}

static void test_emu_q35_switch(void)
{
    ULONG bdf;
    WCHAR w[16];

    /*
     * Every downstream port hot-plug capable: behind a switch they are
     * the controller's own ports, so both are taken as a set (the plain
     * one to the xHCI as well, which a re-enumeration leaves alone).
     */
    emu_q35_switch(1);
    CHECK_EQ(find(B(3, 0, 0), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_SET,
             "q35 switch: two hot-plug ports of one switch");
    CHECK(topo.Source[0] == XHCI_TBT_SRC_SWITCH &&
          topo.Source[1] == XHCI_TBT_SRC_SWITCH, "q35 switch: by the switch");
    CHECK_EQ(topo.Switch, 1, "q35 switch: a PCI Express switch");
    CHECK_EQ(topo.Parent, B(2, 0, 0), "q35 switch: parent 02:00.0");
    CHECK_EQ(topo.ParentType, XHCI_TBT_PCIE_TYPE_DOWN,
             "q35 switch: the parent a downstream port");
    CHECK_EQ(topo.Candidates, 2, "q35 switch: 02:01.0 and 02:02.0");
    CHECK(topo.Tunnels == 2 && topo.Set[0] == B(2, 1, 0) &&
          topo.Set[1] == B(2, 2, 0) && topo.Set[2] == XHCI_TBT_NO_BDF,
          "q35 switch: the set in bus order");
    CHECK_EQ(topo.Tunnel, B(2, 1, 0), "q35 switch: the first");

    /* XhciCmTunnelBridge as the registry hands it over: REG_SZ with its
     * terminating NUL counted. */
    wtext("02:01.0", w, 16);
    CHECK(XhciTbtParseBdfList(w, 8, &bdf, 1) == 1 && bdf == B(2, 1, 0),
          "q35 switch: the value parsed");
    CHECK_EQ(find(B(3, 0, 0), bdf), XHCI_TBT_TOPO_OVERRIDE,
             "q35 switch: the override narrows it");
    CHECK(topo.Tunnels == 1 && topo.Tunnel == B(2, 1, 0) &&
          topo.Source[0] == XHCI_TBT_SRC_OVERRIDE,
          "q35 switch: tunnel 02:01.0 alone, by the override");
    XhciTbtLearnArm(&io, &topo);
    CHECK_EQ(topo.LearnArmed, 0, "an override's set is not learned into");
    CHECK_EQ(topo.Candidates, 2, "q35 switch: candidates still logged");

    emu_q35_switch(0);
    CHECK_EQ(find(B(3, 0, 0), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_SET,
             "q35 switch with hotplug=off on the plain ports: unique");
    CHECK_EQ(topo.Tunnel, B(2, 1, 0), "q35 switch: then 02:01.0");
}

/* ---- the generic rule's edges ------------------------------------------ */

static void test_generic_edges(void)
{
    MODEL_FN *f;
    ULONG i;

    /* Two SHPC bridges beside the NHI on bus 0: no unique pick. */
    mreset();
    mdev(0, 4, 0, 0x1B36, 0x00F4, 0x0C0340UL);
    f = mbridge(0, 5, 0, 0x1B36, 0x0001, 1, 1);
    mshpc(f, 0x48, 31, 0);
    f = mbridge(0, 7, 0, 0x1B36, 0x0001, 2, 2);
    mshpc(f, 0x48, 31, 0);
    CHECK_EQ(find(B(0, 4, 0), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_LEARN,
             "two SHPC siblings on a root bus: none taken");
    CHECK_EQ(topo.Tunnel, XHCI_TBT_NO_BDF, "no tunnel bridge");

    /* A bridge with no capability at all: none. */
    mreset();
    mdev(0, 4, 0, 0x1B36, 0x00F4, 0x0C0340UL);
    f = mbridge(0, 5, 0, 0x1B36, 0x0001, 1, 1);
    mslotid(f, 0x40);
    CHECK_EQ(find(B(0, 4, 0), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_NO_CANDIDATE,
             "a plain bridge: no candidate");
    CHECK_EQ(topo.Siblings, 1, "but it is logged");
    CHECK_EQ(find(B(0, 4, 0), B(0, 5, 0)), XHCI_TBT_TOPO_OVERRIDE,
             "the override takes a bridge without the bits");

    /* A PCI Express downstream port without either bit: no slot, no Link
     * Active report. Not a candidate, and nothing to read. */
    mreset();
    mdev(0, 4, 0, 0x1B36, 0x00F4, 0x0C0340UL);
    f = mbridge(0, 5, 0, 0x8086, 0x1234, 1, 1);
    mpcie(f, 0x40, XHCI_TBT_PCIE_TYPE_DOWN, 0, 0x00615C41UL,
          XHCI_TBT_LINKSTA_DLLLA, XHCI_TBT_SLOTCAP_HPC, XHCI_TBT_SLOTSTA_PDS);
    CHECK_EQ(find(B(0, 4, 0), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_NO_CANDIDATE,
             "HPC without Slot Implemented does not qualify");
    CHECK_EQ(topo.Sibling[0].Flags, XHCI_TBT_BR_PCIE,
             "a PCI Express port, nothing more");
    ioreset();
    CHECK_EQ(XhciTbtPortRead(&io, B(0, 5, 0), &port), XHCI_TBT_PORT_NONE,
             "no capability bits: nothing to read");
    CHECK(port.LinkValid == 0 && port.SlotValid == 0 && port.LinkActive == 0 &&
          port.Presence == 0, "neither reading is taken");

    /* A multi-function bridge's second function is seen; the parent is
     * excluded even when it is a candidate itself. */
    mreset();
    f = mbridge(0, 0x1C, 0, 0x8086, 0x1111, 1, 1);
    mpcie(f, 0x40, XHCI_TBT_PCIE_TYPE_ROOT, 1, 0, 0, XHCI_TBT_SLOTCAP_HPC, 0);
    f = mbridge(0, 0x1C, 4, 0x8086, 0x2222, 2, 2);
    mpcie(f, 0x40, XHCI_TBT_PCIE_TYPE_ROOT, 1, 0, 0, XHCI_TBT_SLOTCAP_HPC, 0);
    mmulti(0, 0x1C);
    mdev(1, 0, 0, 0x8086, 0x15BF, 0x088000UL);
    CHECK_EQ(find(B(1, 0, 0), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_LEARN,
             "parent 00:1c.0 excluded, sibling root port 00:1c.4 a candidate");
    CHECK_EQ(topo.Parent, B(0, 0x1C, 0), "parent");
    CHECK(topo.Candidates == 1 && topo.Sibling[0].Bdf == B(0, 0x1C, 4),
          "function 4 of a multi-function device");
    /* Without the multi-function bit, function 4 is not looked at. */
    mfind(B(0, 0x1C, 0))->Cfg[0x0E] = 0x01;
    CHECK_EQ(find(B(1, 0, 0), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_NO_CANDIDATE,
             "a single-function device's other functions are not read");

    /* More siblings than the log keeps: counted, the first kept. */
    mreset();
    mdev(0, 0, 0, 0x1B36, 0x00F4, 0x0C0340UL);
    for (i = 1; i < 21; i++) {
        mbridge(0, i, 0, 0x1B36, 0x0001, i, i);
    }
    f = mbridge(0, 21, 0, 0x1B36, 0x0001, 21, 21);
    mshpc(f, 0x48, 1, 0);
    CHECK_EQ(find(B(0, 0, 0), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_LEARN,
             "21 siblings, one candidate past the kept 16");
    CHECK_EQ(topo.Siblings, 21, "all counted");
    CHECK_EQ(topo.Candidates, 1, "the candidate counted");
    CHECK_EQ(topo.Sibling[15].Bdf, B(0, 16, 0), "the sixteenth kept");
    XhciTbtLearnArm(&io, &topo);
    f->Shpc[XHCI_TBT_SHPC_SLOT0] = SHPC_CARD;
    CHECK_EQ(XhciTbtLearn(&io, &topo), 1,
             "candidates are kept apart from the sixteen logged siblings");
    CHECK_EQ(topo.Tunnel, B(0, 21, 0), "00:21.0 learned");

    /* A vendor of zero reads as no function. */
    mreset();
    mdev(0, 4, 0, 0x1B36, 0x00F4, 0x0C0340UL);
    f = mbridge(0, 5, 0, 0x0000, 0x0000, 1, 1);
    mshpc(f, 0x48, 1, 0);
    CHECK_EQ(find(B(0, 4, 0), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_NO_CANDIDATE,
             "vendor 0 is absent");
}

/* ---- the parent search ------------------------------------------------- */

static void test_parent_search(void)
{
    MODEL_FN *f;

    /* A second root bus 0x80 that bus 0's bridges never reach. */
    mreset();
    mdev(0, 0, 0, 0x8086, 0x29C0, 0x060000UL);
    mbridge(0, 1, 0, 0x8086, 0x1111, 1, 1);
    mbridge(0x80, 0, 0, 0x8086, 0x2222, 0x81, 0x83);
    f = mbridge(0x80, 1, 0, 0x8086, 0x3333, 0x84, 0x84);
    mshpc(f, 0x48, 1, 0);
    mdev(0x81, 0, 0, 0x8086, 0x15BF, 0x088000UL);
    CHECK_EQ(find(B(0x81, 0, 0), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_LEARN,
             "a parent off bus 0's hierarchy is still found");
    CHECK_EQ(topo.Parent, B(0x80, 0, 0), "parent 80:00.0");
    CHECK(topo.Candidates == 1 && topo.Cand[0] == B(0x80, 1, 0),
          "candidate 80:01.0, not verified beside a conventional bridge");

    /* The NHI on that root bus itself: its own bus's bridges. */
    mreset();
    mdev(0x40, 4, 0, 0x1B36, 0x00F4, 0x0C0340UL);
    f = mbridge(0x40, 5, 0, 0x1B36, 0x0001, 0x41, 0x41);
    mshpc(f, 0x48, 1, 0);
    CHECK_EQ(find(B(0x40, 4, 0), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_LEARN,
             "a NHI on a root bus other than 0");
    CHECK_EQ(topo.Parent, XHCI_TBT_NO_BDF, "no parent");
    CHECK_EQ(topo.SearchBus, 0x40, "its own bus");
    CHECK_EQ(model.MaxBusRead, 0xFF, "every bus was read to know it");

    /* Bridges that point back up or at themselves do not loop. */
    mreset();
    mbridge(0, 1, 0, 0x8086, 0x1111, 1, 2);
    mbridge(1, 0, 0, 0x8086, 0x1111, 1, 1);             /* itself */
    mbridge(1, 1, 0, 0x8086, 0x1111, 0, 0);             /* unconfigured */
    mbridge(1, 2, 0, 0x8086, 0x1111, 2, 2);
    mbridge(2, 0, 0, 0x8086, 0x1111, 1, 1);             /* back up */
    f = mbridge(2, 1, 0, 0x8086, 0x1111, 3, 3);
    mshpc(f, 0x48, 1, 0);
    mbridge(2, 2, 0, 0x8086, 0x1111, 4, 4);
    mdev(4, 0, 0, 0x8086, 0x15BF, 0x088000UL);
    CHECK_EQ(find(B(4, 0, 0), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_LEARN,
             "loops and an unconfigured bridge are walked past");
    CHECK_EQ(topo.Parent, B(2, 2, 0), "parent 02:02.0");
    CHECK_EQ(topo.Cand[0], B(2, 1, 0), "candidate 02:01.0");
}

/* ---- XhciCmTunnelBridge ------------------------------------------------ */

static void test_parse(void)
{
    ULONG bdf;
    WCHAR w[16];

    CHECK(parse("04:01.0", &bdf) && bdf == B(4, 1, 0), "04:01.0");
    CHECK(parse("4:1.0", &bdf) && bdf == B(4, 1, 0), "one-digit fields");
    CHECK(parse("2b:00.0", &bdf) && bdf == B(0x2B, 0, 0), "hex bus");
    CHECK(parse("2B:1F.7", &bdf) && bdf == B(0x2B, 0x1F, 7), "upper case");
    CHECK(parse("ff:1f.7", &bdf) && bdf == 0xFFFFUL, "the last location");
    CHECK(!parse("04:20.0", &bdf) && bdf == XHCI_TBT_NO_BDF, "device 20h");
    CHECK(!parse("04:01.8", &bdf), "function 8");
    CHECK(!parse("004:01.0", &bdf), "three-digit bus");
    CHECK(!parse("04:001.0", &bdf), "three-digit device");
    CHECK(!parse("04:01.00", &bdf), "two-digit function");
    CHECK(!parse("04-01.0", &bdf), "wrong separator");
    CHECK(!parse("04:01", &bdf), "no function");
    CHECK(!parse("04:01.0 ", &bdf), "trailing space");
    CHECK(!parse(" 04:01.0", &bdf), "leading space");
    CHECK(!parse("0000:04:01.0", &bdf), "a domain");
    CHECK(!parse("", &bdf), "empty");
    CHECK(!parse("g4:01.0", &bdf), "not hex");
    wtext("04:01.0", w, 16);
    CHECK(!XhciTbtParseBdfList(w, 6, &bdf, 1), "cut short by the length");
    CHECK(XhciTbtParseBdfList(w, 7, &bdf, 1) == 1 && bdf == B(4, 1, 0),
          "exactly the length, no NUL");
    CHECK(XhciTbtParseBdfList(w, 16, &bdf, 1) == 1 && bdf == B(4, 1, 0),
          "a NUL inside the length ends it");
    CHECK(!XhciTbtParseBdfList(NULL, 8, &bdf, 1), "NULL");
    CHECK(!XhciTbtParseBdfList(w, 8, NULL, 1), "no result");
    CHECK(!XhciTbtParseBdfList(w, 8, &bdf, 0), "no room");

    CHECK_EQ(parsen("00:07.0,00:07.1"), 2, "two locations");
    CHECK(list[0] == B(0, 7, 0) && list[1] == B(0, 7, 1), "in order");
    CHECK_EQ(parsen("00:07.0,00:07.1,00:07.2,00:07.3"), 4, "four");
    CHECK_EQ(list[3], B(0, 7, 3), "the fourth");
    CHECK_EQ(parsen("00:07.0,00:07.1,00:07.2,00:07.3,00:07.4"), 0,
             "five is past XHCI_TBT_MAX_TUNNELS");
    CHECK_EQ(parsen("00:07.0,"), 0, "a trailing comma");
    CHECK_EQ(parsen(",00:07.0"), 0, "a leading comma");
    CHECK_EQ(parsen("00:07.0, 00:07.1"), 0, "a space after the comma");
    CHECK_EQ(parsen("00:07.0;00:07.1"), 0, "another separator");
    wtext("00:07.0,00:07.1", w, 16);
    CHECK_EQ(XhciTbtParseBdfList(w, 16, list, 1), 0,
             "more than the caller's room");
}

/* ---- the bounded poll -------------------------------------------------- */

static void test_poll(void)
{
    XHCI_TBT_POLL p;
    ULONG t0 = 0xFFFFFF00UL;
    MODEL_FN *t;

    /* Up: reached at once (the firmware brings the link up first). */
    CHECK_EQ(XhciTbtPollBegin(&p, XHCI_TBT_WANT_UP, 100, 0),
             XHCI_TBT_POLL_RUNNING, "up begins");
    CHECK_EQ(p.Budget, XHCI_TBT_POLL_BUDGET_MS, "2 s by default");
    CHECK_EQ(XhciTbtPollStep(&p, XHCI_TBT_PORT_UP, 100),
             XHCI_TBT_POLL_REACHED, "up at the first reading");
    CHECK(p.Steps == 1 && p.Elapsed == 0, "one step, no time");
    CHECK_EQ(XhciTbtPollStep(&p, XHCI_TBT_PORT_DOWN, 5000),
             XHCI_TBT_POLL_REACHED, "an ended poll keeps its state");
    CHECK_EQ(p.Steps, 1, "and does not count the step");
    CHECK_EQ(XhciTbtPollWait(&p, 100), 0, "nothing to wait for");

    /* Up across a clock wrap: down, mixed, error, then up. */
    XhciTbtPollBegin(&p, XHCI_TBT_WANT_UP, t0, 0);
    CHECK_EQ(XhciTbtPollWait(&p, t0), XHCI_TBT_POLL_STEP_MS, "a step");
    CHECK_EQ(XhciTbtPollStep(&p, XHCI_TBT_PORT_DOWN, t0 + 50),
             XHCI_TBT_POLL_RUNNING, "down: wait on");
    CHECK_EQ(XhciTbtPollStep(&p, XHCI_TBT_PORT_MIXED, t0 + 300),
             XHCI_TBT_POLL_RUNNING, "mixed: wait on, across the wrap");
    CHECK_EQ(XhciTbtPollStep(&p, XHCI_TBT_PORT_ERROR, t0 + 350),
             XHCI_TBT_POLL_RUNNING, "a failed access: wait on");
    CHECK_EQ(XhciTbtPollStep(&p, XHCI_TBT_PORT_GONE, t0 + 400),
             XHCI_TBT_POLL_RUNNING, "gone: an up poll waits on");
    CHECK_EQ(XhciTbtPollStep(&p, XHCI_TBT_PORT_UP, t0 + 450),
             XHCI_TBT_POLL_REACHED, "up after 450 ms");
    CHECK(p.Elapsed == 450 && p.Steps == 5 && p.Last == XHCI_TBT_PORT_UP,
          "elapsed modulo 2^32");

    /* Up: the budget passes. */
    XhciTbtPollBegin(&p, XHCI_TBT_WANT_UP, t0, 0);
    CHECK_EQ(XhciTbtPollStep(&p, XHCI_TBT_PORT_DOWN, t0 + 1999),
             XHCI_TBT_POLL_RUNNING, "1999 ms: still running");
    CHECK_EQ(XhciTbtPollWait(&p, t0 + 1999), 1, "1 ms left");
    CHECK_EQ(XhciTbtPollWait(&p, t0 + 2000), 0, "none left");
    CHECK_EQ(XhciTbtPollStep(&p, XHCI_TBT_PORT_DOWN, t0 + 2000),
             XHCI_TBT_POLL_TIMEOUT, "2000 ms: timeout");
    CHECK_EQ(p.Elapsed, 2000, "at the budget");
    XhciTbtPollBegin(&p, XHCI_TBT_WANT_UP, 0, 0);
    CHECK_EQ(XhciTbtPollStep(&p, XHCI_TBT_PORT_UP, 2600),
             XHCI_TBT_POLL_REACHED, "a late reading that says up counts");

    /* Down: reached, gone, nothing to read, abandoned. */
    XhciTbtPollBegin(&p, XHCI_TBT_WANT_DOWN, 0, 500);
    CHECK_EQ(p.Budget, 500, "a caller's budget");
    CHECK_EQ(XhciTbtPollStep(&p, XHCI_TBT_PORT_UP, 0),
             XHCI_TBT_POLL_RUNNING, "still up");
    CHECK_EQ(XhciTbtPollStep(&p, XHCI_TBT_PORT_DOWN, 20),
             XHCI_TBT_POLL_REACHED, "down");
    XhciTbtPollBegin(&p, XHCI_TBT_WANT_DOWN, 0, 0);
    CHECK_EQ(XhciTbtPollStep(&p, XHCI_TBT_PORT_GONE, 0),
             XHCI_TBT_POLL_GONE, "the port vanished");
    XhciTbtPollBegin(&p, XHCI_TBT_WANT_DOWN, 0, 0);
    CHECK_EQ(XhciTbtPollStep(&p, XHCI_TBT_PORT_NONE, 0),
             XHCI_TBT_POLL_NO_READING, "a port without the bits, at once");
    XhciTbtPollBegin(&p, XHCI_TBT_WANT_UP, 0, 0);
    CHECK_EQ(XhciTbtPollStep(&p, XHCI_TBT_PORT_NONE, 0),
             XHCI_TBT_POLL_NO_READING, "up the same");
    XhciTbtPollBegin(&p, XHCI_TBT_WANT_UP, 0, 0);
    XhciTbtPollStep(&p, XHCI_TBT_PORT_DOWN, 0);
    CHECK_EQ(XhciTbtPollAbandon(&p, 300), XHCI_TBT_POLL_ABANDONED,
             "a disconnect abandons the up poll");
    CHECK_EQ(p.Elapsed, 300, "when");
    CHECK_EQ(XhciTbtPollAbandon(&p, 400), XHCI_TBT_POLL_ABANDONED,
             "abandoning twice changes nothing");
    CHECK_EQ(p.Elapsed, 300, "nor the time");
    CHECK_EQ(XhciTbtPollStep(&p, XHCI_TBT_PORT_UP, 500),
             XHCI_TBT_POLL_ABANDONED, "a reading after it is ignored");

    CHECK_EQ(XhciTbtPollBegin(&p, 3, 0, 0), XHCI_TBT_POLL_BAD_PARAM,
             "a bad want");
    CHECK_EQ(XhciTbtPollStep(&p, XHCI_TBT_PORT_UP, 0),
             XHCI_TBT_POLL_BAD_PARAM, "stays bad");
    CHECK_EQ(XhciTbtPollBegin(NULL, XHCI_TBT_WANT_UP, 0, 0),
             XHCI_TBT_POLL_BAD_PARAM, "no poll");
    CHECK_EQ(XhciTbtPollStep(NULL, XHCI_TBT_PORT_UP, 0),
             XHCI_TBT_POLL_BAD_PARAM, "no poll to step");
    CHECK_EQ(XhciTbtPollWait(NULL, 0), 0, "no poll to wait on");

    /* Over the model: the P14s tunnel port coming up while polled. */
    mreset();
    mload(tbt_p14s_off_off, sizeof(tbt_p14s_off_off) /
                            sizeof(tbt_p14s_off_off[0]));
    t = mfind(B(4, 1, 0));
    ioreset();
    XhciTbtPollBegin(&p, XHCI_TBT_WANT_UP, 1000, 0);
    CHECK_EQ(XhciTbtPollPort(&io, B(4, 1, 0), &p, &port, 1000),
             XHCI_TBT_POLL_RUNNING, "P14s: down at the first read");
    put16(t, 0xD2, 0x3041);
    CHECK_EQ(XhciTbtPollPort(&io, B(4, 1, 0), &p, &port, 1050),
             XHCI_TBT_POLL_RUNNING, "P14s: link up, slot not yet");
    CHECK_EQ(p.Last, XHCI_TBT_PORT_MIXED, "P14s: mixed");
    put16(t, 0xDA, 0x0148);
    CHECK_EQ(XhciTbtPollPort(&io, B(4, 1, 0), &p, &port, 1100),
             XHCI_TBT_POLL_REACHED, "P14s: both up");
    CHECK_EQ(XhciTbtPollPort(&io, B(4, 1, 0), &p, &port, 1150),
             XHCI_TBT_POLL_REACHED, "an ended poll reads nothing more");

    /* And going down: the domain powered off reads all-ones. */
    XhciTbtPollBegin(&p, XHCI_TBT_WANT_DOWN, 0, 0);
    CHECK_EQ(XhciTbtPollPort(&io, B(4, 1, 0), &p, &port, 0),
             XHCI_TBT_POLL_RUNNING, "P14s: still up");
    put32(t, 0x00, 0xFFFFFFFFUL);
    CHECK_EQ(XhciTbtPollPort(&io, B(4, 1, 0), &p, &port, 10),
             XHCI_TBT_POLL_GONE, "P14s: the port gone");
    put32(t, 0x00, 0x15C08086UL);

    /* A failed access is an ERROR reading and the poll runs on. */
    XhciTbtPollBegin(&p, XHCI_TBT_WANT_DOWN, 0, 0);
    model.FailBdf = B(4, 1, 0);
    CHECK_EQ(XhciTbtPollPort(&io, B(4, 1, 0), &p, &port, 0),
             XHCI_TBT_POLL_RUNNING, "an accessor failure");
    CHECK_EQ(port.Verdict, XHCI_TBT_PORT_ERROR, "reads as ERROR");
    model.FailBdf = XHCI_TBT_NO_BDF;
    CHECK_EQ(get32(t, 0xD0) >> 16, 0x3041, "the model kept its link");
    CHECK_EQ(XhciTbtPollPort(&io, B(4, 1, 0), NULL, &port, 0),
             XHCI_TBT_POLL_BAD_PARAM, "no poll");
    CHECK_EQ(XhciTbtPortRead(&io, B(4, 1, 0), NULL), XHCI_TBT_PORT_ERROR,
             "no port");
    CHECK_EQ(XhciTbtPortRead(NULL, B(4, 1, 0), &port), XHCI_TBT_PORT_ERROR,
             "no accessors");
    CHECK_EQ(XhciTbtPortRead(&io, B(4, 9, 0), &port), XHCI_TBT_PORT_GONE,
             "nothing at the location");
}

/* ---- an integrated host, no switch ------------------------------------ */

/*
 * The shape of an integrated controller (Ice Lake onward): the NHI a root
 * complex function on bus 0 and its tunnel ports PCIe root ports beside it,
 * with the platform's own root ports, one of them hot-plug capable, on the
 * same bus. The ids are this suite's own placeholders, not any part's.
 */
#define TEST_TBT_RP     0xFFF08086UL
#define TEST_PCH_RP     0xFFF18086UL
#define TEST_NHI_ID     0xFFF38086UL

static VOID integrated(ULONG ports)
{
    MODEL_FN *f;
    ULONG i;
    ULONG hp = XHCI_TBT_SLOTCAP_HPC | XHCI_TBT_SLOTCAP_HPS;

    mreset();
    mdev(0, 0, 0, 0x8086, 0x9A14, 0x060000UL);
    for (i = 0; i < ports; i++) {
        f = mbridge(0, 7, i, 0x8086, TEST_TBT_RP >> 16, 1 + i * 0x20,
                    0x1F + i * 0x20);
        mpcie(f, 0x40, XHCI_TBT_PCIE_TYPE_ROOT, 1, XHCI_TBT_LINKCAP_DLLLARC,
              0, hp, 0);
    }
    if (ports > 1) {
        mmulti(0, 7);
    }
    mdev(0, 0x0D, 0, 0x8086, 0x9A13, 0x0C0330UL);
    mdev(0, 0x0D, 2, 0x8086, 0x9A1B, 0x0C0340UL);
    mmulti(0, 0x0D);
    f = mbridge(0, 0x1C, 0, 0x8086, TEST_PCH_RP >> 16, 0x90, 0x90);
    mpcie(f, 0x40, XHCI_TBT_PCIE_TYPE_ROOT, 1, 0, 0, hp, 0);
    f = mbridge(0, 0x1D, 0, 0x8086, 0xFFF2, 0x91, 0x91);
    mpcie(f, 0x40, XHCI_TBT_PCIE_TYPE_ROOT, 1, 0, 0, 0, XHCI_TBT_SLOTSTA_PDS);
}

static void test_integrated(void)
{
    static const XHCI_TBT_TABLE_ENTRY ids[1] = {
        { 0, XHCI_TBT_NO_BDF, TEST_TBT_RP, XHCI_TBT_NO_BDF }
    };
    static const XHCI_TBT_TABLE_ENTRY pch[1] = {
        { 0, XHCI_TBT_NO_BDF, TEST_PCH_RP, XHCI_TBT_NO_BDF }
    };
    static const XHCI_TBT_TABLE_ENTRY other[1] = {
        { TEST_NHI_ID, XHCI_TBT_NO_BDF, TEST_TBT_RP, XHCI_TBT_NO_BDF }
    };
    ULONG over[2];

    integrated(4);
    CHECK_EQ(find(B(0, 0x0D, 2), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_LEARN,
             "integrated: five hot-plug root ports, no id list");
    CHECK(topo.Parent == XHCI_TBT_NO_BDF && topo.SearchBus == 0,
          "integrated: no parent, no switch assumed");
    CHECK(topo.Candidates == 5 && topo.Tunnels == 0 &&
          topo.Tunnel == XHCI_TBT_NO_BDF, "integrated: nothing taken");

    CHECK_EQ(findin(B(0, 0x0D, 2), NULL, 0, ids, 1), XHCI_TBT_TOPO_SET,
             "integrated: the listed root ports form the set");
    CHECK(topo.Tunnels == 4 && topo.Listed == 4 &&
          topo.Set[0] == B(0, 7, 0) && topo.Set[3] == B(0, 7, 3),
          "integrated: 00:07.0 to 00:07.3");
    CHECK(topo.Source[0] == XHCI_TBT_SRC_TABLE &&
          topo.Source[3] == XHCI_TBT_SRC_TABLE, "integrated: by the table");
    XhciTbtLearnArm(&io, &topo);
    CHECK_EQ(topo.LearnArmed, 0, "a table's set is not learned into");
    CHECK(sibling(B(0, 0x1C, 0)) != NULL &&
          !(sibling(B(0, 0x1C, 0))->Flags & XHCI_TBT_BR_LISTED),
          "integrated: the PCH's hot-plug port logged, not taken");
    CHECK(topo.Pick.Flags & XHCI_TBT_BR_LISTED,
          "integrated: a listed port flagged");
    CHECK_EQ(findin(B(0, 0x0D, 2), NULL, 0, other, 1), XHCI_TBT_TOPO_LEARN,
             "integrated: an entry for another NHI id does not match");

    CHECK_EQ(findin(B(0, 0x0D, 2), NULL, 0, pch, 1), XHCI_TBT_TOPO_SET,
             "integrated: a table naming one port takes it");
    CHECK_EQ(topo.Tunnel, B(0, 0x1C, 0), "integrated: the listed one");

    over[0] = B(0, 7, 0);
    over[1] = B(0, 7, 1);
    CHECK_EQ(findin(B(0, 0x0D, 2), over, 2, NULL, 0),
             XHCI_TBT_TOPO_OVERRIDE, "integrated: an override of two");
    CHECK(topo.Tunnels == 2 && topo.Set[1] == B(0, 7, 1),
          "integrated: both taken");
    over[1] = B(0, 0x0D, 0);
    CHECK_EQ(findin(B(0, 0x0D, 2), over, 2, NULL, 0),
             XHCI_TBT_TOPO_OVERRIDE_BAD,
             "integrated: one bad location refuses the whole value");
    CHECK(topo.Tunnels == 0 && topo.Tunnel == XHCI_TBT_NO_BDF,
          "integrated: nothing kept");

    /* A two-port host whose platform has no other hot-plug port. */
    integrated(2);
    mfind(B(0, 0x1C, 0))->Cfg[0x40 + XHCI_TBT_PCIE_SLOTCAP] = 0;
    CHECK_EQ(find(B(0, 0x0D, 2), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_LEARN,
             "integrated, two ports: not a set without the table");
    CHECK_EQ(findin(B(0, 0x0D, 2), NULL, 0, ids, 1), XHCI_TBT_TOPO_SET,
             "integrated, two ports: the set with it");
    CHECK_EQ(topo.Tunnels, 2, "integrated, two ports: two");

    /* Learning both ports of a two-port host, one tunnel at a time. */
    find(B(0, 0x0D, 2), XHCI_TBT_NO_BDF);
    XhciTbtLearnArm(&io, &topo);
    put16(mfind(B(0, 7, 1)), 0x40 + 0x1A, XHCI_TBT_SLOTSTA_PDS);
    put16(mfind(B(0, 7, 1)), 0x40 + 0x12, XHCI_TBT_LINKSTA_DLLLA);
    CHECK_EQ(XhciTbtLearn(&io, &topo), 1, "integrated: 00:07.1 learned");
    XhciTbtLearnArm(&io, &topo);
    put16(mfind(B(0, 7, 0)), 0x40 + 0x1A, XHCI_TBT_SLOTSTA_PDS);
    put16(mfind(B(0, 7, 0)), 0x40 + 0x12, XHCI_TBT_LINKSTA_DLLLA);
    CHECK_EQ(XhciTbtLearn(&io, &topo), 1, "integrated: 00:07.0 learned too");
    CHECK(topo.Tunnels == 2 && topo.Set[0] == B(0, 7, 1) &&
          topo.Set[1] == B(0, 7, 0), "integrated: both, in learning order");
    CHECK_EQ(XhciTbtSetRead(&io, &topo, XHCI_TBT_WANT_DOWN),
             XHCI_TBT_PORT_MIXED, "integrated: the set not down");
    put16(mfind(B(0, 7, 1)), 0x40 + 0x1A, 0);
    put16(mfind(B(0, 7, 1)), 0x40 + 0x12, 0);
    /* A second tunnel coming up beside an existing one: the existing
     * tunnel on 00:07.0 does not end the settling window. */
    XhciTbtSetArm(&io, &topo);
    CHECK(topo.BaseArmed && topo.Base[0] == XHCI_TBT_PORT_DOWN &&
          topo.Base[1] == XHCI_TBT_PORT_UP, "integrated: the baseline");
    CHECK_EQ(XhciTbtSetRead(&io, &topo, XHCI_TBT_WANT_UP),
             XHCI_TBT_PORT_MIXED, "integrated: an old tunnel is no progress");
    CHECK(topo.Last[0] == XHCI_TBT_PORT_DOWN &&
          topo.Last[1] == XHCI_TBT_PORT_UP, "integrated: per-port state");
    put16(mfind(B(0, 7, 1)), 0x40 + 0x1A, XHCI_TBT_SLOTSTA_PDS);
    put16(mfind(B(0, 7, 1)), 0x40 + 0x12, XHCI_TBT_LINKSTA_DLLLA);
    CHECK_EQ(XhciTbtSetRead(&io, &topo, XHCI_TBT_WANT_UP), XHCI_TBT_PORT_UP,
             "integrated: the new port made progress");
    topo.BaseArmed = 0;
    put16(mfind(B(0, 7, 0)), 0x40 + 0x1A, 0);
    put16(mfind(B(0, 7, 0)), 0x40 + 0x12, 0);
    CHECK_EQ(XhciTbtSetRead(&io, &topo, XHCI_TBT_WANT_UP),
             XHCI_TBT_PORT_MIXED,
             "integrated: with no baseline, up only when all are");

    /* One port: not taken on the rule either. */
    integrated(1);
    mfind(B(0, 0x1C, 0))->Cfg[0x40 + XHCI_TBT_PCIE_SLOTCAP] = 0;
    CHECK_EQ(find(B(0, 0x0D, 2), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_LEARN,
             "integrated, one port: learned, not assumed");
    CHECK_EQ(findin(B(0, 0x0D, 2), NULL, 0, ids, 1), XHCI_TBT_TOPO_SET,
             "integrated, one port: the table takes it");
    CHECK_EQ(topo.Tunnel, B(0, 7, 0), "integrated, one port: 00:07.0");
}

/* ---- sets ---------------------------------------------------------------- */

static void test_sets(void)
{
    MODEL_FN *f;
    ULONG i;
    ULONG v[3];
    ULONG b[3];

    /* A switch with five hot-plug downstream ports beside the NHI's:
     * more than XHCI_TBT_MAX_TUNNELS, so no set. */
    mreset();
    f = mbridge(0, 1, 0, 0x8086, 0x1111, 1, 9);
    mpcie(f, 0x40, XHCI_TBT_PCIE_TYPE_ROOT, 1, 0, 0, 0, 0);
    f = mbridge(1, 0, 0, 0x8086, 0x15D3, 2, 9);
    mpcie(f, 0x80, XHCI_TBT_PCIE_TYPE_UP, 0, 0, 0, 0, 0);
    for (i = 0; i < 6; i++) {
        f = mbridge(2, i, 0, 0x8086, 0x15D3, 3 + i, 3 + i);
        mpcie(f, 0x80, XHCI_TBT_PCIE_TYPE_DOWN, 1, 0, 0,
              i == 0 ? 0 : XHCI_TBT_SLOTCAP_HPC, 0);
    }
    mdev(3, 0, 0, 0x8086, 0x15D2, 0x088000UL);
    CHECK_EQ(find(B(3, 0, 0), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_MANY,
             "five hot-plug siblings behind a switch: too many");
    CHECK(topo.Candidates == 5 && topo.Tunnels == 0, "none taken");
    mfind(B(2, 5, 0))->Cfg[0x80 + XHCI_TBT_PCIE_SLOTCAP] = 0;
    CHECK_EQ(find(B(3, 0, 0), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_SET,
             "four: a set");
    CHECK_EQ(topo.Tunnels, 4, "four taken");

    /* A NHI behind a root port alone: its siblings are root ports. */
    mreset();
    f = mbridge(0, 0x1C, 0, 0x8086, 0x1111, 1, 1);
    mpcie(f, 0x40, XHCI_TBT_PCIE_TYPE_ROOT, 1, 0, 0, 0, 0);
    f = mbridge(0, 0x1C, 4, 0x8086, 0x2222, 2, 2);
    mpcie(f, 0x40, XHCI_TBT_PCIE_TYPE_ROOT, 1, 0, 0, XHCI_TBT_SLOTCAP_HPC, 0);
    f = mbridge(0, 0x1D, 0, 0x8086, 0x3333, 3, 3);
    mpcie(f, 0x40, XHCI_TBT_PCIE_TYPE_ROOT, 1, 0, 0, XHCI_TBT_SLOTCAP_HPC, 0);
    mmulti(0, 0x1C);
    mdev(1, 0, 0, 0x8086, 0x15BF, 0x088000UL);
    CHECK_EQ(find(B(1, 0, 0), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_LEARN,
             "beside a root port two hot-plug root ports are not a set");
    CHECK_EQ(topo.ParentType, XHCI_TBT_PCIE_TYPE_ROOT, "the parent's type");

    /* The fold over a set's readings. */
    v[0] = XHCI_TBT_PORT_DOWN;
    v[1] = XHCI_TBT_PORT_UP;
    v[2] = XHCI_TBT_PORT_DOWN;
    b[0] = XHCI_TBT_PORT_DOWN;
    b[1] = XHCI_TBT_PORT_UP;
    b[2] = XHCI_TBT_PORT_DOWN;
    CHECK_EQ(XhciTbtSetVerdict(v, NULL, 3, XHCI_TBT_WANT_UP),
             XHCI_TBT_PORT_MIXED, "up, no baseline: not every member up");
    CHECK_EQ(XhciTbtSetVerdict(v, b, 3, XHCI_TBT_WANT_UP),
             XHCI_TBT_PORT_MIXED, "up: the member up was up already");
    b[1] = XHCI_TBT_PORT_DOWN;
    CHECK_EQ(XhciTbtSetVerdict(v, b, 3, XHCI_TBT_WANT_UP), XHCI_TBT_PORT_UP,
             "up: a member came up");
    b[1] = XHCI_TBT_PORT_ERROR;
    CHECK_EQ(XhciTbtSetVerdict(v, b, 3, XHCI_TBT_WANT_UP),
             XHCI_TBT_PORT_MIXED, "up: an unread baseline is no progress");
    v[0] = XHCI_TBT_PORT_UP;
    v[2] = XHCI_TBT_PORT_UP;
    CHECK_EQ(XhciTbtSetVerdict(v, NULL, 3, XHCI_TBT_WANT_UP),
             XHCI_TBT_PORT_UP, "up: every member up");
    v[0] = XHCI_TBT_PORT_DOWN;
    v[1] = XHCI_TBT_PORT_UP;
    v[2] = XHCI_TBT_PORT_DOWN;
    CHECK_EQ(XhciTbtSetVerdict(v, NULL, 3, XHCI_TBT_WANT_DOWN),
             XHCI_TBT_PORT_MIXED, "down: one still up");
    v[1] = XHCI_TBT_PORT_NONE;
    CHECK_EQ(XhciTbtSetVerdict(v, NULL, 3, XHCI_TBT_WANT_DOWN),
             XHCI_TBT_PORT_MIXED, "down: a member with nothing to read");
    v[1] = XHCI_TBT_PORT_DOWN;
    CHECK_EQ(XhciTbtSetVerdict(v, NULL, 3, XHCI_TBT_WANT_DOWN),
             XHCI_TBT_PORT_DOWN, "down: every member down");
    v[1] = XHCI_TBT_PORT_GONE;
    CHECK_EQ(XhciTbtSetVerdict(v, NULL, 3, XHCI_TBT_WANT_DOWN),
             XHCI_TBT_PORT_GONE, "down: down or gone, one gone");
    CHECK_EQ(XhciTbtSetVerdict(v, NULL, 3, XHCI_TBT_WANT_UP),
             XHCI_TBT_PORT_MIXED, "up: none up");
    v[0] = XHCI_TBT_PORT_ERROR;
    CHECK_EQ(XhciTbtSetVerdict(v, NULL, 3, XHCI_TBT_WANT_DOWN),
             XHCI_TBT_PORT_ERROR, "down: a failed access");
    v[0] = XHCI_TBT_PORT_NONE;
    v[1] = XHCI_TBT_PORT_NONE;
    v[2] = XHCI_TBT_PORT_NONE;
    CHECK_EQ(XhciTbtSetVerdict(v, NULL, 3, XHCI_TBT_WANT_DOWN),
             XHCI_TBT_PORT_NONE, "nothing to read anywhere");
    CHECK_EQ(XhciTbtSetVerdict(v, NULL, 0, XHCI_TBT_WANT_UP),
             XHCI_TBT_PORT_NONE, "an empty set");
    CHECK_EQ(XhciTbtSetVerdict(NULL, NULL, 3, XHCI_TBT_WANT_UP),
             XHCI_TBT_PORT_NONE, "no set");
}

/* ---- failures, ownership and the learning's limits -------------------- */

/* Two SHPC bridges beside a root-bus NHI, both empty. */
static VOID two_shpc(VOID)
{
    MODEL_FN *f;

    mreset();
    mdev(0, 4, 0, 0x1B36, 0x00F4, 0x0C0340UL);
    f = mbridge(0, 5, 0, 0x1B36, 0x0001, 1, 1);
    mshpc(f, 0x48, 1, 0);
    f = mbridge(0, 6, 0, 0x1B36, 0x0001, 2, 2);
    mshpc(f, 0x48, 1, 0);
}

static void test_failures(void)
{
    MODEL_FN *f;
    MODEL_FN *t;
    ULONG off;
    ULONG i;
    XHCI_TBT_POLL p;
    static const XHCI_TBT_TABLE_ENTRY wild[1] = {
        { 0, XHCI_TBT_NO_BDF, TEST_TBT_RP, XHCI_TBT_NO_BDF }
    };
    static const XHCI_TBT_TABLE_ENTRY two[2] = {
        { TEST_NHI_ID, XHCI_TBT_NO_BDF, TEST_TBT_RP, XHCI_TBT_NO_BDF },
        { 0xFFF48086UL, XHCI_TBT_NO_BDF, TEST_TBT_RP, XHCI_TBT_NO_BDF }
    };
    static const XHCI_TBT_TABLE_ENTRY placed[2] = {
        { TEST_NHI_ID, XHCI_TBT_NO_BDF, TEST_TBT_RP, XHCI_TBT_BDF(0, 7, 0) },
        { 0xFFF48086UL, XHCI_TBT_NO_BDF, TEST_TBT_RP, XHCI_TBT_BDF(0, 7, 1) }
    };

    /* A baseline read that failed spoils the learning: the port already
     * up all along must not be learned when its access recovers. */
    two_shpc();
    t = mfind(B(0, 6, 0));
    t->Shpc[XHCI_TBT_SHPC_SLOT0] = SHPC_CARD;
    find(B(0, 4, 0), XHCI_TBT_NO_BDF);
    model.FailBdf = B(0, 6, 0);
    XhciTbtLearnArm(&io, &topo);
    CHECK_EQ(topo.LearnArmed, 0, "a failed baseline read does not arm");
    model.FailBdf = XHCI_TBT_NO_BDF;
    CHECK_EQ(XhciTbtLearn(&io, &topo), 0, "nothing learned unarmed");
    XhciTbtLearnArm(&io, &topo);
    CHECK_EQ(topo.LearnArmed, 1, "armed once reads work");
    model.FailBdf = B(0, 6, 0);
    mfind(B(0, 5, 0))->Shpc[XHCI_TBT_SHPC_SLOT0] = SHPC_CARD;
    CHECK_EQ(XhciTbtLearn(&io, &topo), 0, "a failed read at the end: none");
    model.FailBdf = XHCI_TBT_NO_BDF;
    CHECK_EQ(topo.Tunnels, 0, "the set still empty");

    /* A capability walk that failed at discovery disables learning. */
    two_shpc();
    model.FailBdf = B(0, 6, 0);
    model.FailOffset = 0x48;
    CHECK_EQ(find(B(0, 4, 0), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_LEARN,
             "an unread sibling");
    CHECK(topo.Unread == 1 && (sibling(B(0, 6, 0))->Flags &
                               XHCI_TBT_BR_UNREAD), "flagged unread");
    model.FailBdf = XHCI_TBT_NO_BDF;
    model.FailOffset = XHCI_TBT_NO_BDF;
    XhciTbtLearnArm(&io, &topo);
    CHECK_EQ(topo.LearnArmed, 0, "learning refused with an unread sibling");

    /* More candidates than are kept: no learning, so two ports coming up
     * either side of the limit cannot make the kept one look unique. */
    mreset();
    mdev(0, 0, 0, 0x1B36, 0x00F4, 0x0C0340UL);
    for (i = 1; i < 19; i++) {
        f = mbridge(0, i, 0, 0x1B36, 0x0001, i, i);
        mshpc(f, 0x48, 1, 0);
    }
    CHECK_EQ(find(B(0, 0, 0), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_LEARN,
             "eighteen candidates");
    CHECK_EQ(topo.Candidates, 18, "all counted");
    XhciTbtLearnArm(&io, &topo);
    CHECK_EQ(topo.LearnArmed, 0, "more than kept: not armed");
    mfind(B(0, 1, 0))->Shpc[XHCI_TBT_SHPC_SLOT0] = SHPC_CARD;
    mfind(B(0, 18, 0))->Shpc[XHCI_TBT_SHPC_SLOT0] = SHPC_CARD;
    CHECK_EQ(XhciTbtLearn(&io, &topo), 0, "and nothing learned");

    /* A capability walk failing in a port read is ERROR, not NONE, and a
     * poll runs on past it. */
    mreset();
    mload(tbt_p14s_off_off, sizeof(tbt_p14s_off_off) /
                            sizeof(tbt_p14s_off_off[0]));
    ioreset();
    model.FailBdf = B(4, 1, 0);
    model.FailOffset = 0xAC;
    CHECK_EQ(XhciTbtFindCapStatus(&io, B(4, 1, 0), XHCI_TBT_CAP_PCIE, &off),
             XHCI_TBT_CAP_FAILED, "a walk failing midway");
    CHECK_EQ(XhciTbtPortRead(&io, B(4, 1, 0), &port), XHCI_TBT_PORT_ERROR,
             "the port read says ERROR");
    XhciTbtPollBegin(&p, XHCI_TBT_WANT_DOWN, 0, 0);
    CHECK_EQ(XhciTbtPollPort(&io, B(4, 1, 0), &p, &port, 0),
             XHCI_TBT_POLL_RUNNING, "and the poll does not end NO_READING");
    model.FailOffset = 0x34;
    CHECK_EQ(XhciTbtPortRead(&io, B(4, 1, 0), &port), XHCI_TBT_PORT_ERROR,
             "the capability pointer unread");
    model.FailBdf = XHCI_TBT_NO_BDF;
    model.FailOffset = XHCI_TBT_NO_BDF;
    put32(mfind(B(4, 1, 0)), 0xAC, 0xFFFFFFFFUL);
    CHECK_EQ(XhciTbtFindCapStatus(&io, B(4, 1, 0), XHCI_TBT_CAP_PCIE, &off),
             XHCI_TBT_CAP_GONE, "an all-ones entry");
    CHECK_EQ(XhciTbtPortRead(&io, B(4, 1, 0), &port), XHCI_TBT_PORT_GONE,
             "reads GONE");
    CHECK_EQ(XhciTbtFindCapStatus(&io, B(9, 0, 0), XHCI_TBT_CAP_PCIE, &off),
             XHCI_TBT_CAP_GONE, "nothing there");
    CHECK_EQ(XhciTbtFindCapStatus(NULL, B(4, 1, 0), XHCI_TBT_CAP_PCIE, &off),
             XHCI_TBT_CAP_FAILED, "no accessors");

    /* The SHPC DWORD Select not put back is ERROR. */
    emu_i440fx_switch(0, 1);
    ioreset();
    model.FailWriteAt = model.Writes + 1 + 31 + 1;
    CHECK_EQ(XhciTbtPortRead(&io, B(2, 1, 0), &port), XHCI_TBT_PORT_ERROR,
             "the restoring write failed");
    CHECK_EQ(port.ShpcValid, 0, "and the reading is not kept");
    model.FailWriteAt = 0;
    CHECK_EQ(XhciTbtPortRead(&io, B(2, 1, 0), &port), XHCI_TBT_PORT_UP,
             "next time it reads");

    /* Two domains on one bus sharing root port ids: a table entry
     * without a location is not used; one with a location is. */
    integrated(2);
    mfind(B(0, 0x1C, 0))->Cfg[0x40 + XHCI_TBT_PCIE_SLOTCAP] = 0;
    put16(mfind(B(0, 0x0D, 2)), 0x02, TEST_NHI_ID >> 16);
    mdev(0, 0x0D, 3, 0x8086, 0xFFF4, 0x0C0340UL);
    CHECK_EQ(findin(B(0, 0x0D, 2), NULL, 0, wild, 1), XHCI_TBT_TOPO_SET,
             "an id-only entry with no other domain's NHI id named");
    CHECK_EQ(findin(B(0, 0x0D, 2), NULL, 0, two, 2), XHCI_TBT_TOPO_LEARN,
             "two domains' NHIs on the bus: id-only entries are ambiguous");
    CHECK_EQ(topo.Ambiguous, 1, "said so");
    CHECK_EQ(findin(B(0, 0x0D, 2), NULL, 0, placed, 2), XHCI_TBT_TOPO_SET,
             "entries with locations settle it");
    CHECK(topo.Tunnels == 1 && topo.Set[0] == B(0, 7, 0),
          "this domain's port alone");
    CHECK_EQ(findin(B(0, 0x0D, 3), NULL, 0, placed, 2), XHCI_TBT_TOPO_SET,
             "the other domain");
    CHECK(topo.Tunnels == 1 && topo.Set[0] == B(0, 7, 1),
          "its port alone");
    in.Table = NULL;
    in.TableCount = 1;
    CHECK_EQ(XhciTbtTopoFind(&io, &in, &topo), XHCI_TBT_TOPO_BAD_PARAM,
             "a count without a table");
}

int main(void)
{
    test_decoders();
    test_findcap();
    test_p14s();
    test_emu_i440fx();
    test_emu_generic();
    test_emu_q35_switch();
    test_generic_edges();
    test_parent_search();
    test_parse();
    test_integrated();
    test_sets();
    test_failures();
    test_poll();
    printf("test_tbt_topo: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
