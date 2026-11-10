/*
 * test_tbt_topo.c - host tests for src\cm\tbt_topo.c (roadmap-usb4.md tasks
 * 39-A.5 and 40-A.4; design record 21 sections 6.3, 6.5 and 10.1, design
 * record 22 section 7.6).
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
 *     SHPC tunnel bridge 00:05.0, a member by its SHPC) and q35 (NHI
 *     00:04.0, PCI Express tunnel root port 00:05.0, unlisted, so named
 *     by the override alone);
 *   - the emulator's q35 switch (scripts/run-linux.sh TOPOLOGY=switch),
 *     whose plain downstream ports keep QEMU's default hotplug=on and carry
 *     the switch's id, so the set holds two ports, and the override that
 *     narrows it;
 *   - an integrated host's shape (an NHI on bus 0, its tunnel root ports
 *     beside it with an unrelated hot-plug root port of the platform's):
 *     with and without the family's root port ids, with a second NHI of
 *     the family (ambiguous) and of another, override lists, and
 *     exclusive ownership across domains;
 *   - a foreign hot-plug bridge beside the switch, a port without the
 *     capability bits, the parent search off bus 0's hierarchy, sets and
 *     their fold, failures at every read, and the poll;
 *   - the USB4 profile's correspondence: router-named members on the
 *     generic hosts (added, bound, refused, conflicting, past the
 *     override, beside an incomplete discovery, beside the P14s's
 *     parent, among ambiguous root ports) and the learned one (a
 *     secondary bus gaining a function, none, several, inactive,
 *     unreadable, moved, conflicting with a naming).
 *
 * The emulator layouts are modelled from usb4-emu main at a68bc74 (the run
 * scripts and the device types its QEMU patch defines); their capability
 * offsets are the model's own, not read from a guest.
 *
 * C89.
 */

#include <stdio.h>
#include <string.h>
#include "../src/cm/tbt_topo.h"
#include "../src/cm/tbt_chain.h"
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
    ULONG FailNth;          /* only the Nth access at FailBdf/FailOffset
                               fails, 0 for every one                       */
    ULONG FailSeen;         /* accesses counted for FailNth                 */
    ULONG StrayWrites;      /* writes anywhere but an SHPC DWORD Select     */
    ULONG Reads;
    ULONG Writes;
    ULONG MaxBusRead;       /* the highest bus number read                  */
    ULONG FlipBdf;          /* its ID dword answers a device only after ... */
    ULONG FlipAfter;        /* ... this many reads of it (record 34 3.12)   */
    ULONG FlipSeen;
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
    model.FlipBdf = XHCI_TBT_NO_BDF;
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
    if (bdf == model.FailBdf && (model.FailOffset == XHCI_TBT_NO_BDF ||
                                 model.FailOffset == offset)) {
        model.FailSeen++;
        if (model.FailNth == 0 || model.FailNth == model.FailSeen) {
            return 0;
        }
    }
    if ((offset & 3) != 0 || offset > 0xFC) {
        return 0;
    }
    if (bdf == model.FlipBdf && offset == 0) {
        model.FlipSeen++;
        *value = model.FlipSeen > model.FlipAfter ? 0x00101B36UL : 0xFFFFFFFFUL;
        return 1;
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

/*
 * The family lists a discovery is given (record 21 section 3.3's tables
 * stand behind the real ones). The Alpine Ridge family's by default: the
 * P14s's NHI and switch ids, and the 4C part's port id for the synthetic
 * switches; the integrated family's are this suite's own placeholders.
 */
#define TEST_TBT_RP     0xFFF08086UL    /* an integrated tunnel root port */
#define TEST_PCH_RP     0xFFF18086UL    /* a platform root port           */
#define TEST_NHI_ID     0x9A1B8086UL    /* the integrated NHI             */
#define TEST_NHI_ID2    0x9A1D8086UL    /* its family's second NHI        */

static const ULONG arNhi[2] = { 0x15BF8086UL, 0x15D28086UL };
static const ULONG arPort[2] = { 0x15C08086UL, 0x15D38086UL };
static const ULONG icNhi[2] = { TEST_NHI_ID, TEST_NHI_ID2 };
static const ULONG icPort[1] = { TEST_TBT_RP };

static const ULONG *famNhi = arNhi;
static ULONG famNhiN = 2;
static const ULONG *famSw = arPort;
static ULONG famSwN = 2;
static const ULONG *famRp = NULL;
static ULONG famRpN = 0;

static VOID family_ar(VOID)
{
    famNhi = arNhi;
    famNhiN = 2;
    famSw = arPort;
    famSwN = 2;
    famRp = NULL;
    famRpN = 0;
}

static VOID family_ic(ULONG withPorts)
{
    famNhi = icNhi;
    famNhiN = 2;
    famSw = NULL;
    famSwN = 0;
    famRp = withPorts ? icPort : NULL;
    famRpN = withPorts ? 1UL : 0UL;
}

static ULONG findin(ULONG nhi, const ULONG *over, ULONG overs)
{
    ULONG i;

    ioreset();
    in.Nhi = nhi;
    in.Overrides = overs;
    for (i = 0; i < XHCI_TBT_MAX_TUNNELS; i++) {
        in.Override[i] = i < overs ? over[i] : 0;
    }
    in.FamilyNhiIds = famNhi;
    in.FamilyNhiIdCount = famNhiN;
    in.SwitchPortIds = famSw;
    in.SwitchPortIdCount = famSwN;
    in.RootPortIds = famRp;
    in.RootPortIdCount = famRpN;
    return XhciTbtTopoFind(&io, &in, &topo);
}

static ULONG find(ULONG nhi, ULONG override)
{
    return findin(nhi, &override, override == XHCI_TBT_NO_BDF ? 0UL : 1UL);
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
    MODEL_FN *f;
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
        CHECK_EQ(topo.Candidates, 2, "P14s: two listed ports seen");
        CHECK(topo.Tunnels == 1 && topo.Source[0] == XHCI_TBT_SRC_SWITCH,
              "P14s: by the switch's id and its hot-plug slot");
        CHECK_EQ(topo.ParentType, XHCI_TBT_PCIE_TYPE_DOWN,
                 "P14s: the parent a downstream port");
        CHECK_EQ(topo.Pick.Bdf, B(4, 1, 0), "P14s: the pick's reading");
        CHECK_EQ(topo.Pick.Id, 0x15C08086UL, "P14s: 8086:15c0");
        CHECK_EQ(topo.Pick.Flags,
                 XHCI_TBT_BR_PCIE | XHCI_TBT_BR_SLOT | XHCI_TBT_BR_HOTPLUG |
                 XHCI_TBT_BR_SURPRISE | XHCI_TBT_BR_LINKREPORT |
                 XHCI_TBT_BR_CANDIDATE | XHCI_TBT_BR_LISTED |
                 XHCI_TBT_BR_MEMBER,
                 "P14s: hot-plug and surprise capable, link reported");
        CHECK_EQ((topo.Pick.Buses >> 8) & 0xFFFF, 0x2A06,
                 "P14s: tunnel buses 06-2a");
        br = sibling(B(4, 2, 0));
        CHECK(br != NULL && br->Flags == (XHCI_TBT_BR_PCIE | XHCI_TBT_BR_SLOT |
                                          XHCI_TBT_BR_LISTED |
                                          XHCI_TBT_BR_CANDIDATE),
              "P14s: 04:02.0 listed, a slot without hot-plug: no member");
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
    CHECK_EQ(topo.Candidates, 2, "P14s: the discovery still ran");
    CHECK_EQ(topo.Pick.Bdf, B(4, 2, 0), "P14s: the pick is the override's");
    CHECK_EQ(find(B(5, 0, 0), B(5, 0, 0)), XHCI_TBT_TOPO_OVERRIDE_BAD,
             "P14s: an override naming the NHI alone names no bridge");
    CHECK_EQ(topo.Dropped, 1, "P14s: the entry dropped");
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
    /* Without the family's ids the switch rule takes nothing. */
    famSw = NULL;
    famSwN = 0;
    CHECK_EQ(find(B(5, 0, 0), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_UNLISTED,
             "P14s: no switch ids, no member");
    family_ar();
    /* A hot-plug bridge of another id beside the switch's ports. */
    f = &model.Fn[model.Count++];
    *f = *mfind(B(4, 1, 0));
    f->Bdf = B(4, 3, 0);
    put16(f, 0x02, 0x1234);
    CHECK_EQ(find(B(5, 0, 0), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_SET,
             "P14s + a foreign hot-plug bridge");
    CHECK(topo.Tunnels == 1 && topo.Set[0] == B(4, 1, 0),
          "P14s: the foreign bridge is no member");
    CHECK(sibling(B(4, 3, 0)) != NULL &&
          (sibling(B(4, 3, 0))->Flags & XHCI_TBT_BR_CANDIDATE) &&
          !(sibling(B(4, 3, 0))->Flags & XHCI_TBT_BR_MEMBER),
          "P14s: logged as a hot-plug candidate");
    model.Count--;
    find(B(5, 0, 0), XHCI_TBT_NO_BDF);
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
        CHECK_EQ(find(B(3, 0, 0), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_SET,
                 "i440FX switch: the emulator's SHPC bridge");
        CHECK_EQ(topo.Parent, B(2, 0, 0), "i440FX switch: parent 02:00.0");
        CHECK_EQ(topo.ParentType, 0, "i440FX switch: no PCI Express");
        CHECK(topo.Tunnels == 1 && topo.Set[0] == B(2, 1, 0) &&
              topo.Source[0] == XHCI_TBT_SRC_SHPC,
              "i440FX switch: 02:01.0, by its SHPC");
        CHECK_EQ(topo.Siblings, 2, "i440FX switch: two siblings");
        br = sibling(B(2, 2, 0));
        CHECK(br != NULL &&
              br->Flags == (XHCI_TBT_BR_LISTED | XHCI_TBT_BR_CANDIDATE),
              "i440FX switch: the shpc=off stand-in, listed, no slot");
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
    CHECK_EQ(find(B(0, 4, 0), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_SET,
             "i440FX generic: the SHPC bridge beside the NHI on bus 0");
    CHECK_EQ(topo.Parent, XHCI_TBT_NO_BDF, "i440FX generic: a root bus");
    CHECK_EQ(topo.SearchBus, 0, "i440FX generic: bus 0's bridges");
    CHECK(topo.Tunnels == 1 && topo.Set[0] == B(0, 5, 0) &&
          topo.Source[0] == XHCI_TBT_SRC_SHPC, "i440FX generic: 00:05.0");
    CHECK_EQ(model.MaxBusRead, 0, "i440FX generic: bus 0 alone is read");
    CHECK_EQ(XhciTbtSetRead(&io, &topo, XHCI_TBT_WANT_DOWN),
             XHCI_TBT_PORT_DOWN, "i440FX generic: the set reads down");
    f->Shpc[XHCI_TBT_SHPC_SLOT0] = SHPC_CARD;
    CHECK_EQ(XhciTbtSetRead(&io, &topo, XHCI_TBT_WANT_UP), XHCI_TBT_PORT_UP,
             "i440FX generic: and up");
    CHECK_EQ(topo.Last[0], XHCI_TBT_PORT_UP, "i440FX generic: per-port state");

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
    CHECK_EQ(find(B(0, 4, 0), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_UNLISTED,
             "q35 generic: an unlisted hot-plug root port is no member");
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

    /* A second hot-plug root port on bus 0 (a NIC's): neither is a member;
     * the override names the tunnel port. */
    put16(f, 0x40 + 0x12, 0);
    put16(f, 0x40 + 0x1A, 0);
    t = mbridge(0, 6, 0, 0x1B36, 0x000C, 2, 2);
    mpcie(t, 0x40, XHCI_TBT_PCIE_TYPE_ROOT, 1, 0, 0,
          XHCI_TBT_SLOTCAP_HPC | XHCI_TBT_SLOTCAP_HPS, XHCI_TBT_SLOTSTA_PDS);
    CHECK_EQ(find(B(0, 4, 0), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_UNLISTED,
             "q35 generic + a second root port: two candidates");
    CHECK_EQ(topo.Candidates, 2, "two counted");
    CHECK_EQ(topo.Tunnel, XHCI_TBT_NO_BDF, "no tunnel bridge");
    CHECK_EQ(topo.Pick.Bdf, 0, "no pick kept");
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
    CHECK_EQ(topo.Candidates, 2, "q35 switch: candidates still logged");

    emu_q35_switch(0);
    CHECK_EQ(find(B(3, 0, 0), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_SET,
             "q35 switch with hotplug=off on the plain ports");
    CHECK(topo.Tunnels == 1 && topo.Tunnel == B(2, 1, 0),
          "q35 switch: then 02:01.0 alone");
}

/* ---- the ASM4242 (task 44.40) ------------------------------------------ */

/*
 * The complex as the emulator's asm4242 model lays it out (design
 * record 22 section 18.1): CPU root port 00:02.2 (no slot), the ASM4242's
 * upstream port 13:00.0, its downstream ports 14:00.0 and 14:01.0 (the
 * tunnel ports, Hot-Plug Capable and Surprise, link-active reporting),
 * 14:02.0 to the xHCI 79:00.0 and 14:03.0 to the NHI 7A:00.0 (no slot);
 * `drive` puts the firmware's tunnel (the ASM2464's two bridges and the
 * NVMe) behind 14:01.0.
 */
static const ULONG asmNhi[1] = { 0x24251B21UL };
static const ULONG asmPort[1] = { 0x24231B21UL };

static VOID family_asm(ULONG listed)
{
    famNhi = asmNhi;
    famNhiN = listed ? 1UL : 0UL;
    famSw = listed ? asmPort : NULL;
    famSwN = listed ? 1UL : 0UL;
    famRp = NULL;
    famRpN = 0;
}

static VOID asmHost(ULONG drive)
{
    MODEL_FN *f;
    ULONG hp = XHCI_TBT_SLOTCAP_HPC | XHCI_TBT_SLOTCAP_HPS;

    mreset();
    mdev(0, 0, 0, 0x1022, 0x14D8, 0x060000UL);
    f = mbridge(0, 2, 2, 0x1022, 0x14DB, 0x13, 0x7A);
    mpcie(f, 0x58, XHCI_TBT_PCIE_TYPE_ROOT, 1, 0, 0, 0, 0);
    mdev(0, 2, 0, 0x1022, 0x14DA, 0x060000UL);
    mmulti(0, 2);
    f = mbridge(0x13, 0, 0, 0x1B21, 0x2421, 0x14, 0x7A);
    mpcie(f, 0x80, XHCI_TBT_PCIE_TYPE_UP, 0, 0, 0, 0, 0);
    f = mbridge(0x14, 0, 0, 0x1B21, 0x2423, 0x19, 0x48);
    mpcie(f, 0x80, XHCI_TBT_PCIE_TYPE_DOWN, 1, XHCI_TBT_LINKCAP_DLLLARC,
          0, hp, 0);
    f = mbridge(0x14, 1, 0, 0x1B21, 0x2423, 0x49, 0x78);
    mpcie(f, 0x80, XHCI_TBT_PCIE_TYPE_DOWN, 1, XHCI_TBT_LINKCAP_DLLLARC,
          drive ? XHCI_TBT_LINKSTA_DLLLA : 0, hp,
          drive ? XHCI_TBT_SLOTSTA_PDS : 0);
    f = mbridge(0x14, 2, 0, 0x1B21, 0x2423, 0x79, 0x79);
    mpcie(f, 0x80, XHCI_TBT_PCIE_TYPE_DOWN, 0, 0, 0, 0, 0);
    f = mbridge(0x14, 3, 0, 0x1B21, 0x2423, 0x7A, 0x7A);
    mpcie(f, 0x80, XHCI_TBT_PCIE_TYPE_DOWN, 0, 0, 0, 0, 0);
    mdev(0x79, 0, 0, 0x1B21, 0x2426, 0x0C0330UL);
    mdev(0x7A, 0, 0, 0x1B21, 0x2425, 0x0C0340UL);
    if (drive) {
        f = mbridge(0x49, 0, 0, 0x1B21, 0x2463, 0x4A, 0x4B);
        mpcie(f, 0x80, XHCI_TBT_PCIE_TYPE_UP, 0, 0, 0, 0, 0);
        f = mbridge(0x4A, 0, 0, 0x1B21, 0x2463, 0x4B, 0x4B);
        mpcie(f, 0x80, XHCI_TBT_PCIE_TYPE_DOWN, 1, 0, 0, 0, 0);
        mdev(0x4B, 0, 0, 0x1179, 0x011A, 0x010802UL);
    }
}

static void test_asm4242_host(void)
{
    ULONG drive;

    for (drive = 0; drive < 2; drive++) {
        /* Before task 44.40: no family, so the two hot-plug ports are
         * candidates that no source names. */
        asmHost(drive);
        family_asm(0);
        CHECK_EQ(find(B(0x7A, 0, 0), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_UNLISTED,
                 "ASM4242 host without the family: UNLISTED");
        CHECK_EQ(topo.Tunnels, 0, "ASM4242 host without the family: no member");
        CHECK(topo.Candidates == 2 && topo.Cand[0] == B(0x14, 0, 0) &&
              topo.Cand[1] == B(0x14, 1, 0),
              "ASM4242 host without the family: 14:00.0 and 14:01.0 the candidates");

        /* With it: the switch source takes exactly the two tunnel ports. */
        family_asm(1);
        CHECK_EQ(find(B(0x7A, 0, 0), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_SET,
                 "ASM4242 host: a set");
        CHECK_EQ(topo.Parent, B(0x14, 3, 0), "ASM4242 host: the NHI's parent 14:03.0");
        CHECK_EQ(topo.ParentType, XHCI_TBT_PCIE_TYPE_DOWN,
                 "ASM4242 host: the parent a downstream port");
        CHECK(topo.Tunnels == 2 && topo.Set[0] == B(0x14, 0, 0) &&
              topo.Set[1] == B(0x14, 1, 0),
              "ASM4242 host: 14:00.0 and 14:01.0, in bus order");
        CHECK(topo.Source[0] == XHCI_TBT_SRC_SWITCH &&
              topo.Source[1] == XHCI_TBT_SRC_SWITCH, "ASM4242 host: by the switch");
        CHECK(sibling(B(0x14, 2, 0)) != NULL &&
              !(sibling(B(0x14, 2, 0))->Flags & XHCI_TBT_BR_MEMBER),
              "ASM4242 host: the xHCI's port, with no slot, is no member");
        CHECK(sibling(B(0x14, 3, 0)) == NULL, "ASM4242 host: the NHI's parent is not a sibling");
        CHECK_EQ(topo.Tunnel, B(0x14, 0, 0), "ASM4242 host: the first");
    }

    /* The override still narrows it. */
    asmHost(1);
    family_asm(1);
    CHECK_EQ(find(B(0x7A, 0, 0), B(0x14, 1, 0)), XHCI_TBT_TOPO_OVERRIDE,
             "ASM4242 host: the override");
    CHECK(topo.Tunnels == 1 && topo.Tunnel == B(0x14, 1, 0),
          "ASM4242 host: 14:01.0 alone");
    family_ar();
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
    CHECK_EQ(find(B(0, 4, 0), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_SET,
             "two SHPC siblings: a set of two");
    CHECK(topo.Tunnels == 2 && topo.Source[1] == XHCI_TBT_SRC_SHPC,
          "both by their SHPC");

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
    CHECK_EQ(find(B(1, 0, 0), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_UNLISTED,
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
    CHECK_EQ(find(B(0, 0, 0), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_SET,
             "21 siblings, the member past the kept 16");
    CHECK_EQ(topo.Siblings, 21, "all counted");
    CHECK_EQ(topo.Candidates, 1, "the candidate counted");
    CHECK_EQ(topo.Cand[0], B(0, 21, 0), "and kept apart from the log");
    CHECK_EQ(topo.Sibling[15].Bdf, B(0, 16, 0), "the sixteenth kept");
    CHECK_EQ(topo.Tunnel, B(0, 21, 0), "00:21.0 the member");

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
    CHECK_EQ(find(B(0x81, 0, 0), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_SET,
             "a parent off bus 0's hierarchy is still found");
    CHECK_EQ(topo.Parent, B(0x80, 0, 0), "parent 80:00.0");
    CHECK_EQ(topo.Tunnel, B(0x80, 1, 0), "its SHPC sibling 80:01.0");

    /* The NHI on that root bus itself: its own bus's bridges. */
    mreset();
    mdev(0x40, 4, 0, 0x1B36, 0x00F4, 0x0C0340UL);
    f = mbridge(0x40, 5, 0, 0x1B36, 0x0001, 0x41, 0x41);
    mshpc(f, 0x48, 1, 0);
    CHECK_EQ(find(B(0x40, 4, 0), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_SET,
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
    CHECK_EQ(find(B(4, 0, 0), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_SET,
             "loops and an unconfigured bridge are walked past");
    CHECK_EQ(topo.Parent, B(2, 2, 0), "parent 02:02.0");
    CHECK_EQ(topo.Tunnel, B(2, 1, 0), "member 02:01.0");
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
 * same bus. The root port ids are this suite's own placeholders.
 */
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
    mdev(0, 0x0D, 2, 0x8086, TEST_NHI_ID >> 16, 0x0C0340UL);
    mmulti(0, 0x0D);
    f = mbridge(0, 0x1C, 0, 0x8086, TEST_PCH_RP >> 16, 0x90, 0x90);
    mpcie(f, 0x40, XHCI_TBT_PCIE_TYPE_ROOT, 1, 0, 0, hp, 0);
    f = mbridge(0, 0x1D, 0, 0x8086, 0xFFF2, 0x91, 0x91);
    mpcie(f, 0x40, XHCI_TBT_PCIE_TYPE_ROOT, 1, 0, 0, 0, XHCI_TBT_SLOTSTA_PDS);
}

static void test_integrated(void)
{
    ULONG over[2];
    MODEL_FN *f;
    MODEL_FN *t;

    integrated(4);
    family_ic(0);
    CHECK_EQ(find(B(0, 0x0D, 2), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_UNLISTED,
             "integrated: five hot-plug root ports, no root port ids");
    CHECK(topo.Parent == XHCI_TBT_NO_BDF && topo.SearchBus == 0,
          "integrated: no parent, no switch assumed");
    CHECK(topo.Candidates == 5 && topo.Tunnels == 0 &&
          topo.Tunnel == XHCI_TBT_NO_BDF, "integrated: nothing taken");
    CHECK_EQ(topo.FamilyNhis, 1, "integrated: one NHI of the family");

    family_ic(1);
    CHECK_EQ(find(B(0, 0x0D, 2), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_SET,
             "integrated: the listed root ports form the set");
    CHECK(topo.Tunnels == 4 && topo.Set[0] == B(0, 7, 0) &&
          topo.Set[3] == B(0, 7, 3), "integrated: 00:07.0 to 00:07.3");
    CHECK(topo.Source[0] == XHCI_TBT_SRC_ROOTPORT &&
          topo.Source[3] == XHCI_TBT_SRC_ROOTPORT,
          "integrated: as listed root ports");
    CHECK(sibling(B(0, 0x1C, 0)) != NULL &&
          !(sibling(B(0, 0x1C, 0))->Flags & XHCI_TBT_BR_MEMBER),
          "integrated: the platform's hot-plug root port is never a member");
    CHECK(topo.Pick.Flags & XHCI_TBT_BR_LISTED,
          "integrated: a listed port flagged");

    /* A listed root port need not be hot-plug capable. */
    mfind(B(0, 7, 3))->Cfg[0x40 + XHCI_TBT_PCIE_SLOTCAP] = 0;
    CHECK_EQ(find(B(0, 0x0D, 2), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_SET,
             "integrated: still four");
    CHECK_EQ(topo.Tunnels, 4, "integrated: 00:07.3 by its id alone");

    over[0] = B(0, 7, 0);
    over[1] = B(0, 7, 1);
    CHECK_EQ(findin(B(0, 0x0D, 2), over, 2), XHCI_TBT_TOPO_OVERRIDE,
             "integrated: an override of two is the whole set");
    CHECK(topo.Tunnels == 2 && topo.Set[1] == B(0, 7, 1) &&
          topo.Source[0] == XHCI_TBT_SRC_OVERRIDE, "integrated: both taken");
    over[1] = B(0, 0x0D, 0);
    CHECK_EQ(findin(B(0, 0x0D, 2), over, 2), XHCI_TBT_TOPO_OVERRIDE,
             "integrated: a non-bridge entry is dropped, the rest kept");
    CHECK(topo.Tunnels == 1 && topo.Set[0] == B(0, 7, 0) &&
          topo.Dropped == 1, "integrated: 00:07.0 kept, one dropped");
    over[1] = B(0, 7, 0);
    CHECK_EQ(findin(B(0, 0x0D, 2), over, 2), XHCI_TBT_TOPO_OVERRIDE,
             "integrated: a repeated entry");
    CHECK(topo.Tunnels == 1 && topo.Dropped == 1, "integrated: once");

    /* Two NHIs of the family: the second disabled answers config all the
     * same, and the listed ports are whose, nobody here can say. */
    t = mdev(0, 0x0D, 3, 0x8086, TEST_NHI_ID2 >> 16, 0x0C0340UL);
    CHECK_EQ(find(B(0, 0x0D, 2), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_AMBIGUOUS,
             "integrated, two NHIs: ambiguous");
    CHECK(topo.FamilyNhis == 2 && topo.Ambiguous == 4 && topo.Tunnels == 0,
          "integrated, two NHIs: four listed ports, none taken");
    CHECK(sibling(B(0, 7, 1))->Flags & XHCI_TBT_BR_AMBIGUOUS,
          "integrated, two NHIs: flagged");
    family_ic(1);
    famNhiN = 0;
    CHECK_EQ(find(B(0, 0x0D, 2), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_AMBIGUOUS,
             "integrated, no family NHI ids: fails closed");
    CHECK_EQ(topo.Tunnels, 0, "integrated, no family list: no member");
    famNhiN = 1;
    famNhi = icNhi + 1;
    CHECK_EQ(find(B(0, 0x0D, 2), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_AMBIGUOUS,
             "integrated, a family list without this NHI's id");
    family_ic(1);
    CHECK_EQ(find(B(0, 0x0D, 3), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_AMBIGUOUS,
             "integrated, two NHIs: for the other domain too");
    over[0] = B(0, 7, 2);
    over[1] = B(0, 7, 3);
    CHECK_EQ(findin(B(0, 0x0D, 3), over, 2), XHCI_TBT_TOPO_OVERRIDE,
             "integrated, two NHIs: the override assigns");
    CHECK_EQ(topo.Tunnels, 2, "integrated, two NHIs: two");
    /* Another family's NHI on the bus does not count. */
    put16(t, 0x02, 0x1234);
    CHECK_EQ(find(B(0, 0x0D, 2), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_SET,
             "integrated: a function of another id is no second NHI");

    /* A two-port host: without root port ids no member; with them both
     * ports, and the set's state per port. */
    integrated(2);
    family_ic(0);
    mfind(B(0, 0x1C, 0))->Cfg[0x40 + XHCI_TBT_PCIE_SLOTCAP] = 0;
    CHECK_EQ(find(B(0, 0x0D, 2), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_UNLISTED,
             "integrated, two ports, no ids: unlisted");
    CHECK(topo.Tunnels == 0 && topo.Candidates == 2,
          "integrated, two ports, no ids: logged only");
    family_ic(1);
    CHECK_EQ(find(B(0, 0x0D, 2), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_SET,
             "integrated, two ports");
    f = mfind(B(0, 7, 0));
    t = mfind(B(0, 7, 1));
    put16(t, 0x40 + 0x1A, XHCI_TBT_SLOTSTA_PDS);
    put16(t, 0x40 + 0x12, XHCI_TBT_LINKSTA_DLLLA);
    put16(f, 0x40 + 0x1A, XHCI_TBT_SLOTSTA_PDS);
    put16(f, 0x40 + 0x12, XHCI_TBT_LINKSTA_DLLLA);
    CHECK(topo.Tunnels == 2 && topo.Set[0] == B(0, 7, 0) &&
          topo.Set[1] == B(0, 7, 1), "integrated: both, in bus order");
    CHECK_EQ(XhciTbtSetRead(&io, &topo, XHCI_TBT_WANT_UP), XHCI_TBT_PORT_UP,
             "integrated: both up");
    CHECK_EQ(XhciTbtSetRead(&io, &topo, XHCI_TBT_WANT_DOWN),
             XHCI_TBT_PORT_MIXED, "integrated: the set not down");
    f = mfind(B(0, 7, 1));
    t = mfind(B(0, 7, 0));
    put16(t, 0x40 + 0x1A, 0);
    put16(t, 0x40 + 0x12, 0);
    /* A second tunnel coming up beside an existing one: the existing
     * tunnel on 00:07.0 does not end the settling window. */
    XhciTbtSetArm(&io, &topo);
    CHECK(topo.BaseArmed && topo.Base[0] == XHCI_TBT_PORT_DOWN &&
          topo.Base[1] == XHCI_TBT_PORT_UP, "integrated: the baseline");
    CHECK_EQ(XhciTbtSetRead(&io, &topo, XHCI_TBT_WANT_UP),
             XHCI_TBT_PORT_MIXED, "integrated: an old tunnel is no progress");
    CHECK(topo.Last[0] == XHCI_TBT_PORT_DOWN &&
          topo.Last[1] == XHCI_TBT_PORT_UP, "integrated: per-port state");
    put16(t, 0x40 + 0x1A, XHCI_TBT_SLOTSTA_PDS);
    put16(t, 0x40 + 0x12, XHCI_TBT_LINKSTA_DLLLA);
    CHECK_EQ(XhciTbtSetRead(&io, &topo, XHCI_TBT_WANT_UP), XHCI_TBT_PORT_UP,
             "integrated: the new port made progress");
    topo.BaseArmed = 0;
    put16(f, 0x40 + 0x1A, 0);
    put16(f, 0x40 + 0x12, 0);
    CHECK_EQ(XhciTbtSetRead(&io, &topo, XHCI_TBT_WANT_UP),
             XHCI_TBT_PORT_MIXED,
             "integrated: with no baseline, up only when all are");

    /* Exclusive ownership across domains. */
    {
        ULONG mine[3];
        ULONG theirs[2];
        ULONG act[3];

        mine[0] = B(0, 7, 0);
        mine[1] = B(0, 7, 1);
        mine[2] = B(0, 7, 2);
        theirs[0] = B(0, 7, 1);
        theirs[1] = B(0, 7, 3);
        CHECK_EQ(XhciTbtExclusive(mine, 3, theirs, 2, act), 1,
                 "one port claimed by both domains");
        CHECK(act[0] == 1 && act[1] == 0 && act[2] == 1,
              "it is inactive, the others active");
        CHECK_EQ(XhciTbtExclusive(mine, 3, NULL, 0, act), 0,
                 "no other claims");
        CHECK(act[1] == 1, "all active");
        CHECK_EQ(XhciTbtExclusive(NULL, 3, theirs, 2, act), 0, "no set");
    }
    family_ar();
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
    CHECK_EQ(find(B(1, 0, 0), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_UNLISTED,
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

/* ---- failures ---------------------------------------------------------- */

/* Two unlisted hot-plug root ports beside a root-bus NHI, both empty. */
static VOID two_hp(VOID)
{
    MODEL_FN *f;
    ULONG hp = XHCI_TBT_SLOTCAP_HPC | XHCI_TBT_SLOTCAP_HPS;

    mreset();
    mdev(0, 4, 0, 0x1B36, 0x00F4, 0x0C0340UL);
    f = mbridge(0, 5, 0, 0x1B36, 0x000C, 1, 1);
    mpcie(f, 0x40, XHCI_TBT_PCIE_TYPE_ROOT, 1, XHCI_TBT_LINKCAP_DLLLARC, 0,
          hp, 0);
    f = mbridge(0, 6, 0, 0x1B36, 0x000C, 2, 2);
    mpcie(f, 0x40, XHCI_TBT_PCIE_TYPE_ROOT, 1, XHCI_TBT_LINKCAP_DLLLARC, 0,
          hp, 0);
}

static VOID port_up(MODEL_FN *f, ULONG up)
{
    put16(f, 0x40 + 0x12, up ? XHCI_TBT_LINKSTA_DLLLA : 0);
    put16(f, 0x40 + 0x1A, up ? XHCI_TBT_SLOTSTA_PDS : 0);
}

static void test_failures(void)
{
    MODEL_FN *f;
    ULONG off;
    ULONG i;
    ULONG over[2];
    XHCI_TBT_POLL p;

    /* Discovery reads that fail: incomplete, nothing taken. The second
     * root port is up all along, as a NIC's would be. */
    two_hp();
    port_up(mfind(B(0, 6, 0)), 1);
    model.FailBdf = B(0, 6, 0);
    model.FailOffset = 0x40;
    CHECK_EQ(find(B(0, 4, 0), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_INCOMPLETE,
             "a capability walk failing");
    CHECK(topo.Unread == 1 && (sibling(B(0, 6, 0))->Flags &
                               XHCI_TBT_BR_UNREAD), "flagged unread");
    model.FailBdf = B(0, 6, 0);
    model.FailOffset = XHCI_TBT_CFG_HEADER;
    CHECK_EQ(find(B(0, 4, 0), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_INCOMPLETE,
             "a header unread");
    model.FailOffset = XHCI_TBT_CFG_ID;
    CHECK_EQ(find(B(0, 4, 0), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_INCOMPLETE,
             "an identity unread");
    CHECK(topo.Unread != 0 && topo.Tunnels == 0, "counted, nothing taken");
    model.FailBdf = XHCI_TBT_NO_BDF;
    model.FailOffset = XHCI_TBT_NO_BDF;

    /* An identity that fails only on the second pass over a root bus (the
     * first counts the family's NHIs): incomplete, never a partial set. */
    integrated(2);
    family_ic(1);
    mfind(B(0, 0x1C, 0))->Cfg[0x40 + XHCI_TBT_PCIE_SLOTCAP] = 0;
    model.FailBdf = B(0, 7, 1);
    model.FailOffset = XHCI_TBT_CFG_ID;
    model.FailNth = 2;
    model.FailSeen = 0;
    CHECK_EQ(find(B(0, 0x0D, 2), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_INCOMPLETE,
             "a listed port unread on the second pass");
    CHECK_EQ(topo.Tunnels, 0, "no partial set");
    model.FailBdf = XHCI_TBT_NO_BDF;
    model.FailOffset = XHCI_TBT_NO_BDF;
    model.FailNth = 0;
    CHECK_EQ(find(B(0, 0x0D, 2), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_SET,
             "and whole on the next try");
    CHECK_EQ(topo.Tunnels, 2, "both ports");
    family_ar();

    /* More candidates than are kept: all counted, none a member. */
    mreset();
    mdev(0, 0, 0, 0x1B36, 0x00F4, 0x0C0340UL);
    for (i = 1; i < 19; i++) {
        f = mbridge(0, i, 0, 0x1B36, 0x000C, i, i);
        mpcie(f, 0x40, XHCI_TBT_PCIE_TYPE_ROOT, 1, XHCI_TBT_LINKCAP_DLLLARC,
              0, XHCI_TBT_SLOTCAP_HPC, 0);
    }
    CHECK_EQ(find(B(0, 0, 0), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_UNLISTED,
             "eighteen candidates");
    CHECK_EQ(topo.Candidates, 18, "all counted");
    CHECK(topo.Cand[15] == B(0, 16, 0) && topo.Tunnels == 0,
          "sixteen kept, no member");

    /* An override entry that cannot be read: incomplete, not dropped. */
    integrated(2);
    over[0] = B(0, 7, 0);
    over[1] = B(0, 7, 1);
    model.FailBdf = B(0, 7, 1);
    model.FailOffset = XHCI_TBT_CFG_ID;
    model.FailNth = 3;      /* the walk's two probes pass, the override's
                               fails */
    model.FailSeen = 0;
    CHECK_EQ(findin(B(0, 0x0D, 2), over, 2), XHCI_TBT_TOPO_INCOMPLETE,
             "an override entry's identity unread");
    CHECK(topo.Tunnels == 0 && topo.Dropped == 0, "nothing kept or dropped");
    model.FailOffset = XHCI_TBT_CFG_HEADER;
    model.FailNth = 0;
    CHECK_EQ(findin(B(0, 0x0D, 2), over, 2), XHCI_TBT_TOPO_INCOMPLETE,
             "an override entry's header unread");
    model.FailBdf = XHCI_TBT_NO_BDF;
    model.FailOffset = XHCI_TBT_NO_BDF;
    CHECK_EQ(findin(B(0, 0x0D, 2), over, 2), XHCI_TBT_TOPO_OVERRIDE,
             "read, both kept");
    CHECK_EQ(topo.Tunnels, 2, "two");
    /* The log's MEMBER flags follow the final set. */
    family_ic(1);
    over[0] = B(0, 7, 0);
    CHECK_EQ(findin(B(0, 0x0D, 2), over, 1), XHCI_TBT_TOPO_OVERRIDE,
             "an override of one over a discovered set of two");
    CHECK(!(sibling(B(0, 7, 1))->Flags & XHCI_TBT_BR_MEMBER) &&
          (sibling(B(0, 7, 0))->Flags & XHCI_TBT_BR_MEMBER),
          "only the override's port flagged a member");
    family_ar();

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
    CHECK_EQ(port.ShpcSeen, 1, "though every slot was read");
    model.FailWriteAt = 0;
    CHECK_EQ(XhciTbtPortRead(&io, B(2, 1, 0), &port), XHCI_TBT_PORT_UP,
             "next time it reads");

    /* Record 22 section 7.6.4: every slot read empty, then only the
     * restoring write failing, rejects an endpoint's gain as the whole
     * reading (DOWN) would; the same failure with a card present is a
     * reading not made; a whole reading up admits it. */
    emu_i440fx_switch(0, 0);
    ioreset();
    mdev(4, 1, 0, 0x1B36, 0x0010, 0x010802UL);
    model.FailWriteAt = model.Writes + 1 + 31 + 1;
    CHECK_EQ(XhciTbtGainAdmits(&io, B(2, 1, 0), 4, 1UL << 1, 1),
             XHCI_TBT_GAIN_NO, "gain: slots empty, the restore failed: NO");
    mfind(B(2, 1, 0))->Shpc[XHCI_TBT_SHPC_SLOT0] = SHPC_CARD;
    model.FailWriteAt = model.Writes + 1 + 31 + 1;
    CHECK_EQ(XhciTbtGainAdmits(&io, B(2, 1, 0), 4, 1UL << 1, 1),
             XHCI_TBT_GAIN_UNREAD, "gain: a card, the restore failed: UNREAD");
    model.FailWriteAt = 0;
    CHECK_EQ(XhciTbtGainAdmits(&io, B(2, 1, 0), 4, 1UL << 1, 1),
             XHCI_TBT_GAIN_YES, "gain: a card, read whole: YES");
}

/* ---- the USB4 profile's correspondence (record 22 section 7.6) --------- */

static XHCI_TBT_BINDS binds;

/*
 * The bridge whose secondary bus is `bus`: its Link Active and Presence
 * Detect State (and an SHPC's slot 0) follow whether anything answers
 * there, as a hot plug moves them.
 */
static VOID link_follow(ULONG bus)
{
    ULONG i;
    ULONG d;
    ULONG p;
    ULONG any = 0;
    MODEL_FN *f;
    MODEL_FN *g;

    for (d = 0; d < 32; d++) {
        g = mfind(B(bus, d, 0));
        if (g != NULL && (g->Cfg[0] != 0xFF || g->Cfg[1] != 0xFF)) {
            any = 1;
        }
    }
    for (i = 0; i < model.Count; i++) {
        f = &model.Fn[i];
        if ((f->Cfg[0x0E] & 0x7F) != 1 || f->Cfg[0x19] != bus ||
            XHCI_TBT_BDF_BUS(f->Bdf) == bus) {
            continue;
        }
        for (p = f->Cfg[0x34]; p != 0; p = f->Cfg[p + 1]) {
            if (f->Cfg[p] == XHCI_TBT_CAP_PCIE) {
                put16(f, p + 0x12, any ? XHCI_TBT_LINKSTA_DLLLA : 0);
                put16(f, p + 0x1A, any ? XHCI_TBT_SLOTSTA_PDS : 0);
                break;
            }
        }
        if (f->ShpcCap != 0) {
            f->Shpc[XHCI_TBT_SHPC_SLOT0] = any ? SHPC_CARD : SHPC_EMPTY;
        }
    }
}

/* A device behind a tunnel port, as a hot plug brings it: function 0 of
 * device `dev` on `bus`, the port's link and presence following
 * (link_follow). */
static VOID dev_at(ULONG bus, ULONG dev, ULONG on)
{
    MODEL_FN *f = mfind(B(bus, dev, 0));

    if (f == NULL) {
        if (!on) {
            return;
        }
        f = mdev(bus, dev, 0, 0x144D, 0xA808, 0x010802UL);
    }
    put16(f, 0x00, on ? 0x144D : 0xFFFF);
    link_follow(bus);
}

#define dev_on(bus)     dev_at((bus), 0, 1)
#define dev_off(bus)    dev_at((bus), 0, 0)

/* An endpoint at device 0 of `bus` answering with no change to its port's
 * link or presence (a function answering late, no hot plug). */
static VOID dev_quiet(ULONG bus)
{
    MODEL_FN *f = mfind(B(bus, 0, 0));

    if (f == NULL) {
        f = mdev(bus, 0, 0, 0x144D, 0xA808, 0x010802UL);
    }
    put16(f, 0x00, 0x144D);
}

static ULONG named(ULONG port, ULONG bdf)
{
    return XhciTbtTopoAddNamed(&io, &topo, &binds, port, bdf);
}

static ULONG member(ULONG port)
{
    return XhciTbtPortMember(&topo, &binds, port);
}

static ULONG learn(ULONG port, const ULONG *active)
{
    return XhciTbtLearn(&io, &topo, &binds, port, active);
}

static VOID up(ULONG port)
{
    XhciTbtLearnUp(&binds, port);
}

static ULONG arm(VOID)
{
    return XhciTbtLearnArm(&io, &topo, &binds);
}

static void test_named(void)
{
    MODEL_FN *f;
    ULONG over[2];
    ULONG i;
    ULONG reads;

    /* The emulator's q35 generic host with two unlisted hot-plug root
     * ports: discovery takes neither; the minimal host model answers the
     * mapping operation with each tunnel port's routing id. */
    two_hp();
    mdev(0, 0, 0, 0x8086, 0x29C0, 0x060000UL);
    XhciTbtBindsInit(&binds);
    CHECK_EQ(find(B(0, 4, 0), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_UNLISTED,
             "named: the generic host's discovery takes nothing");
    CHECK_EQ(topo.FindOutcome, XHCI_TBT_TOPO_UNLISTED,
             "named: FindOutcome is the discovery's");
    CHECK_EQ(member(1), XHCI_TBT_NO_MEMBER,
             "named: no port known before any naming");
    CHECK_EQ(named(1, B(0, 5, 0)), XHCI_TBT_MAP_ADDED,
             "named: port 1's bridge joins the set");
    CHECK_EQ(topo.Outcome, XHCI_TBT_TOPO_SET, "named: the outcome is SET");
    CHECK_EQ(topo.FindOutcome, XHCI_TBT_TOPO_UNLISTED,
             "named: the discovery's outcome kept");
    CHECK(topo.Tunnels == 1 && topo.Set[0] == B(0, 5, 0) &&
          topo.Source[0] == XHCI_TBT_SRC_ROUTER &&
          topo.Tunnel == B(0, 5, 0), "named: 00:05.0, source ROUTER");
    CHECK(binds.Count == 1 && binds.Port[0] == 1 &&
          binds.Bdf[0] == B(0, 5, 0) &&
          binds.How[0] == XHCI_TBT_CORR_NAMED && topo.Named == 1,
          "named: port 1's, by naming");
    CHECK(topo.Pick.Bdf == B(0, 5, 0) &&
          (topo.Pick.Flags & XHCI_TBT_BR_HOTPLUG) &&
          (topo.Pick.Flags & XHCI_TBT_BR_MEMBER), "named: the pick read");
    CHECK(sibling(B(0, 5, 0)) != NULL &&
          (sibling(B(0, 5, 0))->Flags & XHCI_TBT_BR_MEMBER) &&
          !(sibling(B(0, 6, 0))->Flags & XHCI_TBT_BR_MEMBER),
          "named: the log's MEMBER flag follows");
    CHECK_EQ(member(1), 0, "named: port 1 -> member 0");
    CHECK_EQ(member(2), XHCI_TBT_NO_MEMBER, "named: port 2 not known");
    CHECK_EQ(named(1, B(0, 5, 0)), XHCI_TBT_MAP_KNOWN,
             "named: the same naming again is KNOWN");
    CHECK(topo.Tunnels == 1 && binds.Count == 1, "named: not added twice");
    CHECK_EQ(named(2, B(0, 6, 0)), XHCI_TBT_MAP_ADDED,
             "named: port 2's bridge");
    CHECK(topo.Tunnels == 2 && topo.Named == 2 && member(2) == 1,
          "named: port 2 -> member 1");
    CHECK_EQ(XhciTbtSetRead(&io, &topo, XHCI_TBT_WANT_DOWN),
             XHCI_TBT_PORT_DOWN, "named: the set reads as any set");

    /* A rediscovery keeps the bindings; naming again restores the set. */
    CHECK_EQ(find(B(0, 4, 0), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_UNLISTED,
             "named: rediscovered");
    CHECK(topo.Tunnels == 0 && member(1) == XHCI_TBT_NO_MEMBER,
          "named: no member answers until the set holds it");
    CHECK_EQ(named(2, B(0, 6, 0)), XHCI_TBT_MAP_ADDED,
             "named: port 2 named again");
    CHECK_EQ(named(1, B(0, 5, 0)), XHCI_TBT_MAP_ADDED,
             "named: port 1 named again");
    CHECK(binds.Count == 2 && member(1) == 1 && member(2) == 0,
          "named: the bindings by location, the order the set's");

    /* A bridge named for two ports: neither correspondence is kept. */
    CHECK_EQ(named(3, B(0, 5, 0)), XHCI_TBT_MAP_CONFLICT,
             "named: 00:05.0 for port 3 too");
    CHECK(member(1) == XHCI_TBT_NO_MEMBER && member(3) == XHCI_TBT_NO_MEMBER,
          "named: ports 1 and 3 unknown");
    CHECK_EQ(member(2), 0, "named: port 2 untouched");
    CHECK(topo.Tunnels == 2 && topo.Source[1] == XHCI_TBT_SRC_ROUTER,
          "named: the member stays in the set");
    CHECK_EQ(named(1, B(0, 5, 0)), XHCI_TBT_MAP_CONFLICT,
             "named: never known again (port 1)");
    CHECK_EQ(named(4, B(0, 5, 0)), XHCI_TBT_MAP_CONFLICT,
             "named: never known again (the bridge)");
    CHECK_EQ(member(4), XHCI_TBT_NO_MEMBER, "named: port 4 unknown");
    find(B(0, 4, 0), XHCI_TBT_NO_BDF);
    CHECK_EQ(named(1, B(0, 5, 0)), XHCI_TBT_MAP_CONFLICT,
             "named: a conflict outlives a rediscovery");
    CHECK(topo.Tunnels == 1 && topo.Set[0] == B(0, 5, 0) &&
          member(1) == XHCI_TBT_NO_MEMBER,
          "named: the bridge still joins the set, its port unknown");
    over[0] = B(0, 5, 0);
    CHECK_EQ(XhciTbtExclusive(topo.Set, topo.Tunnels, over, 1, list), 1,
             "named: and another domain's claim on it still counts");

    /* A port named for two bridges: the first withdrawn, the second not
     * added. */
    two_hp();
    f = mbridge(0, 8, 0, 0x1B36, 0x000C, 3, 3);
    mpcie(f, 0x40, XHCI_TBT_PCIE_TYPE_ROOT, 1, XHCI_TBT_LINKCAP_DLLLARC, 0,
          XHCI_TBT_SLOTCAP_HPC, 0);
    XhciTbtBindsInit(&binds);
    find(B(0, 4, 0), XHCI_TBT_NO_BDF);
    CHECK_EQ(named(1, B(0, 5, 0)), XHCI_TBT_MAP_ADDED,
             "named: port 1 -> 00:05.0");
    CHECK_EQ(named(1, B(0, 6, 0)), XHCI_TBT_MAP_CONFLICT,
             "named: port 1 -> 00:06.0 as well");
    CHECK(topo.Tunnels == 2 && topo.Set[1] == B(0, 6, 0) &&
          topo.Source[1] == XHCI_TBT_SRC_ROUTER &&
          member(1) == XHCI_TBT_NO_MEMBER,
          "named: 00:06.0 a member, port 1 unknown");
    CHECK_EQ(named(2, B(0, 6, 0)), XHCI_TBT_MAP_CONFLICT,
             "named: 00:06.0 conflicted too");
    CHECK_EQ(named(3, B(0, 8, 0)), XHCI_TBT_MAP_ADDED,
             "named: another port still named");
    CHECK_EQ(member(3), 2, "named: port 3 -> member 2");

    /* What is not a bridge, or cannot be read. */
    two_hp();
    mdev(0, 0, 0, 0x8086, 0x29C0, 0x060000UL);
    XhciTbtBindsInit(&binds);
    find(B(0, 4, 0), XHCI_TBT_NO_BDF);
    CHECK_EQ(named(1, B(0, 9, 0)), XHCI_TBT_MAP_NO_BRIDGE,
             "named: nothing answers there");
    CHECK_EQ(named(1, B(0, 0, 0)), XHCI_TBT_MAP_NO_BRIDGE,
             "named: a type 0 function");
    CHECK_EQ(named(1, B(0, 4, 0)), XHCI_TBT_MAP_REFUSED,
             "named: the NHI itself");
    CHECK(binds.Count == 0 && topo.Tunnels == 0 &&
          topo.Outcome == XHCI_TBT_TOPO_UNLISTED, "named: nothing taken");
    CHECK_EQ(named(1, B(0, 5, 0)), XHCI_TBT_MAP_ADDED,
             "named: port 1 taken");
    model.FailBdf = B(0, 6, 0);
    CHECK_EQ(named(2, B(0, 6, 0)), XHCI_TBT_MAP_UNREAD,
             "named: a failed identity read after one taken");
    CHECK(topo.Outcome == XHCI_TBT_TOPO_INCOMPLETE && topo.Tunnels == 0 &&
          topo.Tunnel == XHCI_TBT_NO_BDF && member(1) == XHCI_TBT_NO_MEMBER,
          "named: the discovery voided, never a partial set");
    CHECK_EQ(named(1, B(0, 5, 0)), XHCI_TBT_MAP_REFUSED,
             "named: nothing more until a rediscovery");
    model.FailBdf = XHCI_TBT_NO_BDF;
    find(B(0, 4, 0), XHCI_TBT_NO_BDF);
    model.FailBdf = B(0, 6, 0);
    model.FailOffset = XHCI_TBT_CFG_HEADER;
    CHECK_EQ(named(2, B(0, 6, 0)), XHCI_TBT_MAP_UNREAD,
             "named: a failed header read");
    CHECK_EQ(topo.Outcome, XHCI_TBT_TOPO_INCOMPLETE, "named: voided too");
    model.FailBdf = XHCI_TBT_NO_BDF;
    model.FailOffset = XHCI_TBT_NO_BDF;
    find(B(0, 4, 0), XHCI_TBT_NO_BDF);
    CHECK_EQ(named(1, B(0, 5, 0)), XHCI_TBT_MAP_ADDED,
             "named: port 1 again after the rediscovery");
    CHECK_EQ(named(2, B(0, 6, 0)), XHCI_TBT_MAP_ADDED,
             "named: port 2 once it reads");
    CHECK_EQ(named(0, B(0, 6, 0)), XHCI_TBT_MAP_BAD_PARAM, "named: port 0");
    CHECK_EQ(named(XHCI_TBT_MAX_PORTS + 1, B(0, 6, 0)),
             XHCI_TBT_MAP_BAD_PARAM, "named: port 33");
    CHECK_EQ(named(2, 0x10000UL), XHCI_TBT_MAP_BAD_PARAM,
             "named: a location past 16 bits");
    CHECK_EQ(XhciTbtTopoAddNamed(&io, NULL, &binds, 2, B(0, 6, 0)),
             XHCI_TBT_MAP_BAD_PARAM, "named: no topo");
    CHECK_EQ(XhciTbtTopoAddNamed(&io, &topo, NULL, 2, B(0, 6, 0)),
             XHCI_TBT_MAP_BAD_PARAM, "named: no binds");
    CHECK_EQ(XhciTbtTopoAddNamed(NULL, &topo, &binds, 2, B(0, 6, 0)),
             XHCI_TBT_MAP_BAD_PARAM, "named: no accessors");
    CHECK_EQ(member(0), XHCI_TBT_NO_MEMBER, "named: port 0, no member");
    CHECK_EQ(XhciTbtPortMember(NULL, &binds, 1), XHCI_TBT_NO_MEMBER,
             "named: no topo, no member");
    CHECK_EQ(XhciTbtPortMember(&topo, NULL, 1), XHCI_TBT_NO_MEMBER,
             "named: no binds, no member");

    /* Port 32 is a port. */
    XhciTbtBindsInit(&binds);
    find(B(0, 4, 0), XHCI_TBT_NO_BDF);
    CHECK_EQ(named(XHCI_TBT_MAX_PORTS, B(0, 6, 0)), XHCI_TBT_MAP_ADDED,
             "named: port 32");
    CHECK_EQ(member(XHCI_TBT_MAX_PORTS), 0, "named: port 32 -> member 0");

    /* The override is the whole set: a named member is bound, anything
     * else refused. */
    XhciTbtBindsInit(&binds);
    over[0] = B(0, 5, 0);
    CHECK_EQ(findin(B(0, 4, 0), over, 1), XHCI_TBT_TOPO_OVERRIDE,
             "named: an override of 00:05.0");
    CHECK_EQ(named(1, B(0, 5, 0)), XHCI_TBT_MAP_KNOWN,
             "named: the override's member is port 1's");
    CHECK(topo.Source[0] == XHCI_TBT_SRC_OVERRIDE && member(1) == 0 &&
          topo.Named == 0, "named: its source stays OVERRIDE");
    CHECK_EQ(named(2, B(0, 6, 0)), XHCI_TBT_MAP_REFUSED,
             "named: no member past the override");
    CHECK_EQ(topo.Tunnels, 1, "named: the override's set unchanged");
    over[0] = B(0, 9, 0);
    CHECK_EQ(findin(B(0, 4, 0), over, 1), XHCI_TBT_TOPO_OVERRIDE_BAD,
             "named: an override naming nothing");
    CHECK_EQ(named(1, B(0, 5, 0)), XHCI_TBT_MAP_REFUSED,
             "named: refused under OVERRIDE_BAD");

    /* An incomplete or overfull discovery takes nothing. */
    model.FailBdf = B(0, 6, 0);
    model.FailOffset = XHCI_TBT_CFG_ID;
    CHECK_EQ(find(B(0, 4, 0), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_INCOMPLETE,
             "named: an unread function on the bus");
    model.FailBdf = XHCI_TBT_NO_BDF;
    model.FailOffset = XHCI_TBT_NO_BDF;
    CHECK_EQ(named(1, B(0, 5, 0)), XHCI_TBT_MAP_REFUSED,
             "named: refused under INCOMPLETE");
    CHECK_EQ(topo.Tunnels, 0, "named: still empty");
    find(B(0, 4, 0), XHCI_TBT_NO_BDF);
    topo.Outcome = XHCI_TBT_TOPO_MANY;
    CHECK_EQ(named(1, B(0, 5, 0)), XHCI_TBT_MAP_REFUSED,
             "named: refused under MANY");
    topo.Outcome = XHCI_TBT_TOPO_NONE;
    CHECK_EQ(named(1, B(0, 5, 0)), XHCI_TBT_MAP_REFUSED,
             "named: refused with no discovery");

    /* A member by discovery keeps its source; the naming binds it. The
     * i440FX generic layout: the SHPC bridge 00:05.0. */
    mreset();
    XhciTbtBindsInit(&binds);
    mdev(0, 4, 0, 0x1B36, 0x00F4, 0x0C0340UL);
    f = mbridge(0, 5, 0, 0x1B36, 0x0001, 1, 1);
    mshpc(f, 0x48, 31, 0);
    CHECK_EQ(find(B(0, 4, 0), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_SET,
             "named: the SHPC bridge by discovery");
    CHECK_EQ(named(1, B(0, 5, 0)), XHCI_TBT_MAP_KNOWN, "named: and named");
    CHECK(topo.Source[0] == XHCI_TBT_SRC_SHPC && member(1) == 0 &&
          topo.Named == 0, "named: still SHPC, now port 1's");

    /* No room: four SHPC members, a fifth bridge named voids the set. */
    for (i = 0; i < 3; i++) {
        f = mbridge(0, 6 + i, 0, 0x1B36, 0x0001, 2 + i, 2 + i);
        mshpc(f, 0x48, 1, 0);
    }
    f = mbridge(0, 0x0A, 0, 0x1B36, 0x000C, 6, 6);
    mpcie(f, 0x40, XHCI_TBT_PCIE_TYPE_ROOT, 1, 0, 0, XHCI_TBT_SLOTCAP_HPC, 0);
    CHECK_EQ(find(B(0, 4, 0), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_SET,
             "named: four SHPC members");
    CHECK_EQ(named(2, B(0, 0x0A, 0)), XHCI_TBT_MAP_FULL,
             "named: no room for a fifth");
    CHECK(topo.Outcome == XHCI_TBT_TOPO_MANY && topo.Tunnels == 0 &&
          member(1) == XHCI_TBT_NO_MEMBER,
          "named: voided MANY, never a truncated set");

    /* The NHI's parent is never a tunnel port: the P14s, Assist on. */
    mreset();
    XhciTbtBindsInit(&binds);
    mload(tbt_p14s_off_on,
          sizeof(tbt_p14s_off_on) / sizeof(tbt_p14s_off_on[0]));
    CHECK_EQ(find(B(5, 0, 0), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_SET,
             "named: the P14s's switch port");
    CHECK_EQ(named(1, B(4, 0, 0)), XHCI_TBT_MAP_REFUSED,
             "named: the NHI's parent refused");
    CHECK_EQ(named(1, B(4, 1, 0)), XHCI_TBT_MAP_KNOWN,
             "named: the switch member bound");
    CHECK(topo.Source[0] == XHCI_TBT_SRC_SWITCH && member(1) == 0,
          "named: still SWITCH");

    /* Two NHIs of an integrated family: the listed root ports are
     * ambiguous; the one the router names joins this domain's set, the
     * other stays out, and exclusive ownership decides as for any claim. */
    integrated(2);
    family_ic(1);
    XhciTbtBindsInit(&binds);
    mdev(0, 0x0D, 3, 0x8086, TEST_NHI_ID2 >> 16, 0x0C0340UL);
    CHECK_EQ(find(B(0, 0x0D, 2), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_AMBIGUOUS,
             "named: two NHIs, ambiguous ports");
    CHECK_EQ(named(1, B(0, 7, 1)), XHCI_TBT_MAP_ADDED,
             "named: the router names 00:07.1");
    CHECK(topo.Outcome == XHCI_TBT_TOPO_SET &&
          topo.FindOutcome == XHCI_TBT_TOPO_AMBIGUOUS &&
          topo.Tunnels == 1 && topo.Set[0] == B(0, 7, 1) &&
          topo.Source[0] == XHCI_TBT_SRC_ROUTER,
          "named: one member, ROUTER, 00:07.0 still out");
    over[0] = B(0, 7, 1);
    CHECK_EQ(XhciTbtExclusive(topo.Set, topo.Tunnels, over, 1, list), 1,
             "named: the other NHI's router names it too: inactive");
    CHECK_EQ(list[0], 0, "named: inactive for this domain");
    family_ar();

    /* XhciTbtLearn does not read for a named port. */
    two_hp();
    XhciTbtBindsInit(&binds);
    find(B(0, 4, 0), XHCI_TBT_NO_BDF);
    named(1, B(0, 5, 0));
    reads = io.Reads;
    up(1);
    CHECK_EQ(binds.Pending, 0, "named: a named port is never pending");
    CHECK_EQ(learn(1, NULL), XHCI_TBT_MAP_KNOWN,
             "named: learning a named port is KNOWN");
    CHECK_EQ(io.Reads, reads, "named: with no read");
}

static void test_learned(void)
{
    ULONG over[2];
    ULONG active[2];
    ULONG reads;

    /* Two override members, 00:05.0 (bus 1) and 00:06.0 (bus 2); the
     * router answered no mapping. */
    two_hp();
    XhciTbtBindsInit(&binds);
    over[0] = B(0, 5, 0);
    over[1] = B(0, 6, 0);
    CHECK_EQ(findin(B(0, 4, 0), over, 2), XHCI_TBT_TOPO_OVERRIDE,
             "learned: two members by the override");
    CHECK_EQ(learn(1, NULL), XHCI_TBT_MAP_NOT_UP,
             "learned: no tunnel came up on port 1");
    up(1);
    CHECK_EQ(binds.Pending, 1, "learned: port 1 pending");
    CHECK_EQ(learn(1, NULL), XHCI_TBT_MAP_NOT_ARMED,
             "learned: nothing armed: armed now");
    CHECK(binds.Armed && binds.BaseCount == 2 && binds.BaseBus[0] == 1 &&
          binds.BaseBus[1] == 2 && binds.BaseSeen[0] == 0 &&
          binds.BaseSeen[1] == 0, "learned: both secondary buses empty");
    CHECK_EQ(learn(1, NULL), XHCI_TBT_MAP_NOT_GAINED,
             "learned: nothing yet");
    dev_on(1);
    CHECK_EQ(learn(1, NULL), XHCI_TBT_MAP_LEARNED,
             "learned: bus 1 gained a device: 00:05.0 is port 1's");
    CHECK(member(1) == 0 && binds.How[0] == XHCI_TBT_CORR_LEARNED &&
          binds.BaseSeen[0] == 1 && binds.Pending == 0,
          "learned: bound, the baseline moved, no longer pending");
    CHECK_EQ(topo.Source[0], XHCI_TBT_SRC_OVERRIDE,
             "learned: the source unchanged");
    reads = io.Reads;
    CHECK_EQ(learn(1, NULL), XHCI_TBT_MAP_KNOWN,
             "learned: for the life of the instance");
    CHECK_EQ(io.Reads, reads, "learned: with no read");

    /* Port 2, with port 1's device still there. */
    up(2);
    dev_on(2);
    CHECK_EQ(learn(2, NULL), XHCI_TBT_MAP_LEARNED,
             "learned: 00:06.0 is port 2's");
    CHECK_EQ(member(2), 1, "learned: port 2 -> member 1");

    /* A rediscovery keeps what was learned, by location. */
    over[0] = B(0, 6, 0);
    over[1] = B(0, 5, 0);
    CHECK_EQ(findin(B(0, 4, 0), over, 2), XHCI_TBT_TOPO_OVERRIDE,
             "learned: rediscovered, the order turned round");
    CHECK(member(1) == 1 && member(2) == 0,
          "learned: still known, at their new indices");
    up(3);
    CHECK_EQ(learn(3, NULL), XHCI_TBT_MAP_NOT_ARMED,
             "learned: a baseline over another set is retaken");
    CHECK(binds.BaseBdf[0] == B(0, 6, 0) && binds.BaseSeen[0] == 1 &&
          binds.BaseSeen[1] == 1, "learned: over the set as it is");
    XhciTbtLearnDown(&binds, 3);
    CHECK_EQ(binds.Pending, 0, "learned: port 3 down, not pending");
    over[0] = B(0, 5, 0);
    over[1] = B(0, 6, 0);
    findin(B(0, 4, 0), over, 2);

    /* A device returning on a bound bridge is its own port's: it explains
     * nothing for another port. */
    dev_off(1);
    CHECK_EQ(arm(), 0, "learned: armed after port 1's tunnel went down");
    up(3);
    dev_on(1);
    CHECK_EQ(learn(3, NULL), XHCI_TBT_MAP_NOT_GAINED,
             "learned: port 1's bridge regaining is no gain for port 3");
    CHECK(member(1) == 0 && member(2) == 1 &&
          member(3) == XHCI_TBT_NO_MEMBER, "learned: nothing changed");
    XhciTbtLearnDown(&binds, 3);

    /* A device on another device number than 0 (the emulator's SHPC
     * slots: the NVMe at 01:01.0). */
    two_hp();
    XhciTbtBindsInit(&binds);
    findin(B(0, 4, 0), over, 2);
    arm();
    up(1);
    dev_at(1, 1, 1);
    CHECK_EQ(learn(1, NULL), XHCI_TBT_MAP_LEARNED,
             "learned: a device at 01:01.0");
    CHECK_EQ(binds.BaseSeen[0], 2, "learned: device 1 in the baseline");

    /* Two tunnels drained in one batch: nothing learned while both are
     * pending. */
    two_hp();
    XhciTbtBindsInit(&binds);
    findin(B(0, 4, 0), over, 2);
    arm();
    up(1);
    up(2);
    dev_on(1);
    CHECK_EQ(learn(1, NULL), XHCI_TBT_MAP_SEVERAL,
             "learned: one gain, two tunnels pending");
    CHECK(binds.Count == 0 && binds.Pending == 3,
          "learned: not attributed, both still pending");
    dev_on(2);
    CHECK_EQ(learn(2, NULL), XHCI_TBT_MAP_SEVERAL,
             "learned: nor the second");
    XhciTbtLearnDown(&binds, 1);
    XhciTbtLearnDown(&binds, 2);
    arm();
    CHECK_EQ(binds.Pending, 0, "learned: both down");

    /* Two buses gain at once. */
    two_hp();
    XhciTbtBindsInit(&binds);
    findin(B(0, 4, 0), over, 2);
    arm();
    up(1);
    dev_on(1);
    dev_on(2);
    CHECK_EQ(learn(1, NULL), XHCI_TBT_MAP_SEVERAL,
             "learned: two members gained");
    CHECK(member(1) == XHCI_TBT_NO_MEMBER && binds.Count == 0 &&
          binds.Pending == 1, "learned: neither bound, port 1 pending");

    /* A delayed endpoint: port 1's device answers only after port 2's
     * tunnel came up. Port 1 still pending, nothing is attributed. */
    two_hp();
    XhciTbtBindsInit(&binds);
    findin(B(0, 4, 0), over, 2);
    arm();
    up(1);
    CHECK_EQ(learn(1, NULL), XHCI_TBT_MAP_NOT_GAINED,
             "learned: port 1's device not answering yet");
    up(2);
    dev_on(1);
    CHECK_EQ(learn(2, NULL), XHCI_TBT_MAP_SEVERAL,
             "learned: port 1's late device is never port 2's");
    CHECK(member(2) == XHCI_TBT_NO_MEMBER && member(1) ==
          XHCI_TBT_NO_MEMBER, "learned: nothing bound");
    dev_on(2);
    CHECK_EQ(learn(2, NULL), XHCI_TBT_MAP_SEVERAL,
             "learned: nor later, while port 1 is pending");
    XhciTbtLearnDown(&binds, 1);
    dev_off(1);
    arm();
    dev_off(2);
    CHECK_EQ(learn(2, NULL), XHCI_TBT_MAP_NOT_GAINED,
             "learned: port 1 down, port 2 alone pending");
    dev_on(2);
    CHECK_EQ(learn(2, NULL), XHCI_TBT_MAP_LEARNED,
             "learned: then port 2's own device is learned");
    CHECK_EQ(member(2), 1, "learned: port 2 -> member 1");

    /* The member that gained is another domain's. */
    two_hp();
    XhciTbtBindsInit(&binds);
    findin(B(0, 4, 0), over, 2);
    active[0] = 0;
    active[1] = 1;
    arm();
    up(1);
    dev_on(1);
    CHECK_EQ(learn(1, active), XHCI_TBT_MAP_INACTIVE,
             "learned: an inactive member is never learned");
    CHECK(member(1) == XHCI_TBT_NO_MEMBER && binds.Pending == 1,
          "learned: port 1 unknown, still pending");
    up(2);
    dev_on(2);
    CHECK_EQ(learn(2, active), XHCI_TBT_MAP_SEVERAL,
             "learned: port 1's live tunnel still blocks port 2");
    CHECK_EQ(member(2), XHCI_TBT_NO_MEMBER, "learned: port 2 unknown");
    XhciTbtLearnDown(&binds, 1);
    dev_off(2);
    arm();
    CHECK_EQ(learn(2, active), XHCI_TBT_MAP_NOT_GAINED,
             "learned: port 1 proven down");
    dev_on(2);
    CHECK_EQ(learn(2, active), XHCI_TBT_MAP_LEARNED,
             "learned: port 2 then learned");

    /* Unreadable: a failed access, a bus number that moved, a bridge with
     * no secondary bus. */
    two_hp();
    XhciTbtBindsInit(&binds);
    findin(B(0, 4, 0), over, 2);
    arm();
    up(1);
    dev_on(1);
    model.FailBdf = B(0, 6, 0);
    model.FailOffset = XHCI_TBT_CFG_BUSES;
    CHECK_EQ(learn(1, NULL), XHCI_TBT_MAP_UNREAD,
             "learned: the other member's bus numbers unread");
    model.FailBdf = B(2, 5, 0);
    model.FailOffset = XHCI_TBT_CFG_ID;
    CHECK_EQ(learn(1, NULL), XHCI_TBT_MAP_UNREAD,
             "learned: a function on the other bus unread");
    model.FailBdf = XHCI_TBT_NO_BDF;
    model.FailOffset = XHCI_TBT_NO_BDF;
    CHECK_EQ(learn(1, NULL), XHCI_TBT_MAP_LEARNED,
             "learned: the baseline survives an unread reading");

    two_hp();
    XhciTbtBindsInit(&binds);
    findin(B(0, 4, 0), over, 2);
    arm();
    up(1);
    mfind(B(0, 5, 0))->Cfg[0x19] = 7;
    mfind(B(0, 5, 0))->Cfg[0x1A] = 7;
    dev_on(7);
    CHECK_EQ(learn(1, NULL), XHCI_TBT_MAP_UNREAD,
             "learned: a secondary bus that changed since the arm");

    two_hp();
    XhciTbtBindsInit(&binds);
    mfind(B(0, 6, 0))->Cfg[0x19] = 0;
    findin(B(0, 4, 0), over, 2);
    CHECK_EQ(arm(), XHCI_TBT_MAP_UNREAD,
             "learned: a member with no secondary bus cannot be armed");
    CHECK_EQ(binds.Armed, 0, "learned: not armed");
    up(1);
    dev_on(1);
    CHECK_EQ(learn(1, NULL), XHCI_TBT_MAP_NOT_ARMED,
             "learned: and nothing is learned beside it");

    /* A gain on a conflicted bridge: the port conflicts too. */
    two_hp();
    XhciTbtBindsInit(&binds);
    find(B(0, 4, 0), XHCI_TBT_NO_BDF);
    named(2, B(0, 5, 0));
    named(3, B(0, 5, 0));
    CHECK_EQ(named(4, B(0, 6, 0)), XHCI_TBT_MAP_ADDED,
             "learned: port 4 named on 00:06.0");
    arm();
    up(1);
    dev_on(1);
    CHECK_EQ(learn(1, NULL), XHCI_TBT_MAP_CONFLICT,
             "learned: port 1 gained on the conflicted 00:05.0");
    CHECK(member(1) == XHCI_TBT_NO_MEMBER && binds.Pending == 1,
          "learned: port 1 unknown for good, its tunnel still pending");
    CHECK_EQ(member(4), 1, "learned: port 4 kept");
    CHECK_EQ(learn(1, NULL), XHCI_TBT_MAP_CONFLICT,
             "learned: a conflicted port is never learned");
    XhciTbtLearnDown(&binds, 1);
    up(1);
    CHECK_EQ(binds.Pending, 1,
             "learned: a conflicted port's next tunnel is recorded");
    up(4);
    CHECK_EQ(binds.Pending, 1, "learned: a named port's is not");

    /* The table's room: past it, nothing is known any more. */
    two_hp();
    XhciTbtBindsInit(&binds);
    findin(B(0, 4, 0), over, 2);
    binds.Count = XHCI_TBT_MAX_BINDS;
    arm();
    up(1);
    dev_on(1);
    CHECK_EQ(learn(1, NULL), XHCI_TBT_MAP_CONFLICT,
             "learned: no room for the binding");
    CHECK(binds.Overflow && member(1) == XHCI_TBT_NO_MEMBER,
          "learned: overflowed, nothing known");
    up(2);
    CHECK_EQ(learn(2, NULL), XHCI_TBT_MAP_CONFLICT,
             "learned: and nothing learned after");

    /* Bad parameters; an empty set. */
    two_hp();
    XhciTbtBindsInit(&binds);
    findin(B(0, 4, 0), over, 1);
    CHECK_EQ(XhciTbtLearnArm(NULL, &topo, &binds), XHCI_TBT_MAP_BAD_PARAM,
             "learned: arm with no accessors");
    CHECK_EQ(XhciTbtLearnArm(&io, NULL, &binds), XHCI_TBT_MAP_BAD_PARAM,
             "learned: arm with no topo");
    CHECK_EQ(XhciTbtLearnArm(&io, &topo, NULL), XHCI_TBT_MAP_BAD_PARAM,
             "learned: arm with no binds");
    CHECK_EQ(learn(0, NULL), XHCI_TBT_MAP_BAD_PARAM, "learned: port 0");
    CHECK_EQ(learn(XHCI_TBT_MAX_PORTS + 1, NULL), XHCI_TBT_MAP_BAD_PARAM,
             "learned: port 33");
    CHECK_EQ(XhciTbtLearn(&io, &topo, NULL, 1, NULL),
             XHCI_TBT_MAP_BAD_PARAM, "learned: no binds");
    up(0);
    up(XHCI_TBT_MAX_PORTS + 1);
    XhciTbtLearnUp(NULL, 1);
    XhciTbtLearnDown(NULL, 1);
    CHECK_EQ(binds.Pending, 0, "learned: bad ports never pending");
    up(XHCI_TBT_MAX_PORTS);
    CHECK_EQ(binds.Pending, 0x80000000UL, "learned: port 32 pending");
    XhciTbtLearnDown(&binds, XHCI_TBT_MAX_PORTS);
    CHECK_EQ(arm(), 0, "learned: one member");
    up(1);
    dev_on(1);
    CHECK_EQ(learn(1, NULL), XHCI_TBT_MAP_LEARNED,
             "learned: one member learned");
    find(B(0, 4, 0), XHCI_TBT_NO_BDF);
    CHECK_EQ(arm(), XHCI_TBT_MAP_BAD_PARAM,
             "learned: an empty set has nothing to arm");
    up(2);
    CHECK_EQ(learn(2, NULL), XHCI_TBT_MAP_NOT_ARMED,
             "learned: and nothing to learn");
    CHECK_EQ(member(1), XHCI_TBT_NO_MEMBER,
             "learned: a bridge out of the set answers for no port");
}

/*
 * The learning's window (record 22 section 7.6, task 44.57): every pass
 * for the first second, a gap doubling from the poll period to 5 s, the
 * window over at 60 s, closed with no more tries until XhciTbtLearnOpen,
 * forgotten by XhciTbtLearnEnd; wrap-safe times; bad ports and a NULL
 * clock ignored.
 */
static void test_learn_window(void)
{
    XHCI_TBT_LEARN_CLOCK clk;
    XHCI_TBT_BINDS bb;
    ULONG t;
    ULONG tries;
    ULONG last;

    memset(&clk, 0, sizeof(clk));
    CHECK_EQ(XhciTbtLearnDue(&clk, 1, 0), XHCI_TBT_LEARN_TRY,
             "window: a port whose window is not open is tried");
    XhciTbtLearnOpen(&clk, 1, 1000, 0);
    CHECK_EQ(clk.Open, 1, "window: opened");
    CHECK_EQ(XhciTbtLearnDue(&clk, 1, 1000), XHCI_TBT_LEARN_TRY,
             "window: the first try at once");

    /* The first second: every 200 ms pass. */
    for (t = 1000; t < 2000; t += 200) {
        CHECK_EQ(XhciTbtLearnDue(&clk, 1, t), XHCI_TBT_LEARN_TRY,
                 "window: eager pass due");
        XhciTbtLearnMissed(&clk, 1, t, 200);
        XhciTbtLearnPass(&clk);
        CHECK_EQ(clk.Gap[0], 0, "window: no gap while eager");
    }
    /* Then 200, 400, 800, 1600, 3200, 5000, 5000 ... */
    XhciTbtLearnMissed(&clk, 1, 2000, 200);
    XhciTbtLearnPass(&clk);
    CHECK_EQ(clk.Gap[0], 200, "window: the gap starts at one period");
    CHECK_EQ(XhciTbtLearnDue(&clk, 1, 2199), XHCI_TBT_LEARN_WAIT,
             "window: not before the gap");
    CHECK_EQ(XhciTbtLearnDue(&clk, 1, 2200), XHCI_TBT_LEARN_TRY,
             "window: due at the gap");
    XhciTbtLearnMissed(&clk, 1, 2200, 200);
    XhciTbtLearnPass(&clk);
    CHECK_EQ(clk.Gap[0], 400, "window: doubled");
    XhciTbtLearnMissed(&clk, 1, 2600, 200);
    XhciTbtLearnPass(&clk);
    CHECK_EQ(clk.Gap[0], 800, "window: doubled again");
    XhciTbtLearnMissed(&clk, 1, 3400, 200);
    XhciTbtLearnPass(&clk);
    CHECK_EQ(clk.Gap[0], 1600, "window: 1600");
    XhciTbtLearnMissed(&clk, 1, 5000, 200);
    XhciTbtLearnPass(&clk);
    CHECK_EQ(clk.Gap[0], 3200, "window: 3200");
    XhciTbtLearnMissed(&clk, 1, 8200, 200);
    XhciTbtLearnPass(&clk);
    CHECK_EQ(clk.Gap[0], 5000, "window: held to 5 s");
    XhciTbtLearnMissed(&clk, 1, 13200, 200);
    XhciTbtLearnPass(&clk);
    CHECK_EQ(clk.Gap[0], 5000, "window: stays at 5 s");
    CHECK_EQ(XhciTbtLearnDue(&clk, 1, 60999), XHCI_TBT_LEARN_TRY,
             "window: open until 60 s");
    CHECK_EQ(XhciTbtLearnDue(&clk, 1, 61000), XHCI_TBT_LEARN_OVER,
             "window: over at 60 s");

    /* How many tries a whole window costs at 200 ms passes. */
    XhciTbtLearnOpen(&clk, 2, 0, 0);
    tries = 0;
    for (t = 0; XhciTbtLearnDue(&clk, 2, t) != XHCI_TBT_LEARN_OVER;
         t += 200) {
        if (XhciTbtLearnDue(&clk, 2, t) == XHCI_TBT_LEARN_TRY) {
            tries++;
            XhciTbtLearnMissed(&clk, 2, t, 200);
            XhciTbtLearnPass(&clk);
        }
    }
    CHECK_EQ(t, XHCI_TBT_LEARN_WINDOW_MS, "window: over at the 60 s pass");
    CHECK(tries >= 10 && tries <= 30, "window: a bounded count of tries");

    /* Other poll periods: 50 ms and 5000 ms; a period of 0. */
    XhciTbtLearnOpen(&clk, 3, 0, 0);
    XhciTbtLearnMissed(&clk, 3, 1000, 50);
    XhciTbtLearnPass(&clk);
    CHECK_EQ(clk.Gap[2], 50, "window: 50 ms period starts the gap");
    XhciTbtLearnOpen(&clk, 3, 0, 0);
    XhciTbtLearnMissed(&clk, 3, 1000, 5000);
    XhciTbtLearnPass(&clk);
    CHECK_EQ(clk.Gap[2], 5000, "window: 5 s period is the cap at once");
    XhciTbtLearnMissed(&clk, 3, 6000, 5000);
    XhciTbtLearnPass(&clk);
    CHECK_EQ(clk.Gap[2], 5000, "window: and stays there");
    XhciTbtLearnOpen(&clk, 3, 0, 0);
    XhciTbtLearnMissed(&clk, 3, 1000, 0);
    XhciTbtLearnPass(&clk);
    CHECK_EQ(clk.Gap[2], 1, "window: a period of 0 counts as 1 ms");
    XhciTbtLearnOpen(&clk, 3, 0, 0);
    XhciTbtLearnMissed(&clk, 3, 1000, 9000);
    XhciTbtLearnPass(&clk);
    CHECK_EQ(clk.Gap[2], 5000, "window: a period above the cap is held");

    /* Wrap: a window opened just before the counter wraps. */
    t = 0xFFFFFF00UL;
    XhciTbtLearnOpen(&clk, 4, t, 0);
    last = t + 1500UL;      /* past the wrap */
    XhciTbtLearnMissed(&clk, 4, last, 200);
    XhciTbtLearnPass(&clk);
    CHECK_EQ(XhciTbtLearnDue(&clk, 4, last + 199UL), XHCI_TBT_LEARN_WAIT,
             "window: wrap, not yet");
    CHECK_EQ(XhciTbtLearnDue(&clk, 4, last + 200UL), XHCI_TBT_LEARN_TRY,
             "window: wrap, due");
    CHECK_EQ(XhciTbtLearnDue(&clk, 4, t + 60000UL),
             XHCI_TBT_LEARN_OVER, "window: wrap, over at 60 s");

    /* Close: no more tries until opened again; End forgets it. */
    XhciTbtLearnClose(&clk, 1);
    CHECK_EQ(clk.Open & 1UL, 0, "close: the window is not open");
    CHECK_EQ(XhciTbtLearnDue(&clk, 1, 61000), XHCI_TBT_LEARN_CLOSED,
             "close: no more tries");
    XhciTbtLearnMissed(&clk, 1, 61000, 200);
    XhciTbtLearnPass(&clk);
    CHECK_EQ(XhciTbtLearnDue(&clk, 1, 99000), XHCI_TBT_LEARN_CLOSED,
             "close: a miss does not reopen it");
    XhciTbtLearnOpen(&clk, 1, 100000, 0);
    CHECK_EQ(XhciTbtLearnDue(&clk, 1, 100000), XHCI_TBT_LEARN_TRY,
             "close: a TUNNEL_UP's open makes it due at once");
    CHECK_EQ(clk.Closed & 1UL, 0, "close: and clears the closure");
    XhciTbtLearnClose(&clk, 1);
    XhciTbtLearnEnd(&clk, 1);
    CHECK_EQ((clk.Open | clk.Closed) & 1UL, 0, "end: forgotten");
    CHECK_EQ(XhciTbtLearnDue(&clk, 1, 0), XHCI_TBT_LEARN_TRY,
             "end: a port with no window is tried (opened first)");
    XhciTbtLearnMissed(&clk, 1, 5000, 200);
    XhciTbtLearnPass(&clk);
    CHECK_EQ(clk.Gap[0], 0, "missed: a window not open is left alone");

    /* A binding made after the window closed: its mark consumed, no read. */
    XhciTbtBindsInit(&bb);
    XhciTbtLearnUp(&bb, 2);
    CHECK_EQ(XhciTbtLearnBound(&bb, 2), 0, "bound: not bound yet");
    CHECK_EQ(bb.Pending, 2, "bound: still pending");
    bb.Count = 1;
    bb.Port[0] = 2;
    bb.Bdf[0] = B(0, 4, 0);
    bb.How[0] = XHCI_TBT_CORR_NAMED;
    CHECK_EQ(XhciTbtLearnBound(&bb, 2), 1, "bound: named now");
    CHECK_EQ(bb.Pending, 0, "bound: the mark consumed");
    bb.How[0] = XHCI_TBT_CORR_LEARNED;
    XhciTbtLearnUp(&bb, 3);
    CHECK_EQ(XhciTbtLearnBound(&bb, 3), 0, "bound: another port's binding");
    CHECK_EQ(bb.Pending, 4, "bound: port 3 stays pending");
    CHECK_EQ(XhciTbtLearnBound(&bb, 0), 0, "bound: port 0");
    CHECK_EQ(XhciTbtLearnBound(&bb, XHCI_TBT_MAX_PORTS + 1), 0,
             "bound: port 33");
    CHECK_EQ(XhciTbtLearnBound(NULL, 2), 0, "bound: no binds");

    /* The generation kept for the caller. */
    XhciTbtLearnOpen(&clk, 5, 0, 9);
    CHECK_EQ(clk.Tag[4], 9, "open: the generation kept");

    /* Once a pass: a port tried (a delivery's miss) is not tried again
     * in the same pass, eager or not; the next pass may. */
    XhciTbtLearnOpen(&clk, 6, 0, 0);
    XhciTbtLearnMissed(&clk, 6, 0, 200);
    CHECK_EQ(XhciTbtLearnDue(&clk, 6, 0), XHCI_TBT_LEARN_WAIT,
             "pass: not twice in one pass");
    CHECK_EQ(XhciTbtLearnDue(&clk, 7, 0), XHCI_TBT_LEARN_TRY,
             "pass: another port is not held");
    XhciTbtLearnPass(&clk);
    CHECK_EQ(XhciTbtLearnDue(&clk, 6, 200), XHCI_TBT_LEARN_TRY,
             "pass: due at the next pass");
    XhciTbtLearnMissed(&clk, 8, 0, 200);
    CHECK_EQ(clk.Tried & 0x80UL, 0x80UL,
             "pass: a miss with no window still marks the pass");
    XhciTbtLearnOpen(&clk, 8, 0, 0);
    XhciTbtLearnPass(&clk);
    XhciTbtLearnPass(NULL);
    CHECK_EQ(clk.Tried, 0, "pass: cleared");
    /* A miss before a suspend, then the resume's pass and a rebuilt
     * tunnel's window: its delivery is tried. */
    XhciTbtLearnMissed(&clk, 6, 300000, 200);
    XhciTbtLearnPass(&clk);
    XhciTbtLearnOpen(&clk, 6, 900000, 1);
    CHECK_EQ(XhciTbtLearnDue(&clk, 6, 900000), XHCI_TBT_LEARN_TRY,
             "pass: a resume's delivery after a new pass is tried");

    /* Port 32 is the last; 0, 33 and a NULL clock are ignored. */
    XhciTbtLearnOpen(&clk, XHCI_TBT_MAX_PORTS, 0, 0);
    CHECK_EQ(clk.Open & 0x80000000UL, 0x80000000UL, "window: port 32");
    XhciTbtLearnClose(&clk, XHCI_TBT_MAX_PORTS);
    CHECK_EQ(clk.Closed & 0x80000000UL, 0x80000000UL, "close: port 32");
    t = clk.Open;
    last = clk.Closed;
    XhciTbtLearnOpen(&clk, 0, 0, 0);
    XhciTbtLearnOpen(&clk, XHCI_TBT_MAX_PORTS + 1, 0, 0);
    XhciTbtLearnClose(&clk, 0);
    XhciTbtLearnClose(&clk, XHCI_TBT_MAX_PORTS + 1);
    XhciTbtLearnEnd(&clk, 0);
    XhciTbtLearnEnd(&clk, XHCI_TBT_MAX_PORTS + 1);
    XhciTbtLearnMissed(&clk, 0, 0, 200);
    XhciTbtLearnPass(&clk);
    CHECK(clk.Open == t && clk.Closed == last,
          "window: bad ports change nothing");
    CHECK_EQ(XhciTbtLearnDue(&clk, 0, 0), XHCI_TBT_LEARN_CLOSED,
             "window: port 0 never due");
    CHECK_EQ(XhciTbtLearnDue(&clk, XHCI_TBT_MAX_PORTS + 1, 0),
             XHCI_TBT_LEARN_CLOSED, "window: port 33 never due");
    XhciTbtLearnOpen(NULL, 1, 0, 0);
    XhciTbtLearnMissed(NULL, 1, 0, 200);
    XhciTbtLearnClose(NULL, 1);
    XhciTbtLearnEnd(NULL, 1);
    CHECK_EQ(XhciTbtLearnDue(NULL, 1, 0), XHCI_TBT_LEARN_CLOSED,
             "window: NULL clock never due");
}

/* ---- chain ports (record 34 sections 3.1, 3.9 and 9.1; task 45.13) ---- */

#define CH_UP       0x5UL
#define CH_DOWN     0x6UL
#define CH_ROOT     0x4UL
#define CH_HPC      XHCI_TBT_SLOTCAP_HPC
#define CH_PCP      XHCI_TBT_SLOTCAP_PCP

/* A PCI Express bridge of `type`; `slotCap` 0 for no slot. */
static MODEL_FN *chbr(ULONG b, ULONG d, ULONG fn, ULONG did, ULONG type,
                      ULONG sec, ULONG sub, ULONG slotCap)
{
    MODEL_FN *f = mbridge(b, d, fn, 0x104C, did, sec, sub);

    mpcie(f, 0x40, type, (type == CH_DOWN || type == CH_ROOT) ? 1UL : 0UL,
          0, 0, slotCap, 0);
    return f;
}

/*
 * The emulator's Thunderbolt 3 chain as 45.13's leg r2 read it on q35: the
 * tunnel port 02:01.0 (buses 4 to 7); the dock's upstream port 04:00.0
 * (8232, buses 5 to 7); its fixed port 05:00.0 to the dock's NVMe on bus 6;
 * its chain port 05:01.0 (8233, hot-plug, bus 7).
 */
static VOID chdock(ULONG chained)
{
    MODEL_FN *tp;

    mreset();
    ioreset();
    tp = mbridge(2, 1, 0, 0x8086, 0x15C0, 4, 7);
    mpcie(tp, 0x40, CH_DOWN, 1, 0, 0, CH_HPC, 0);
    chbr(4, 0, 0, 0x8232, CH_UP, 5, 7, 0);
    chbr(5, 0, 0, 0x8233, CH_DOWN, 6, 6, 0);
    mdev(6, 0, 0, 0x1B36, 0x0010, 0x010802UL);
    chbr(5, 1, 0, 0x8233, CH_DOWN, 7, 7, CH_HPC);
    if (chained) {
        mdev(7, 0, 0, 0x1B36, 0x0010, 0x010802UL);
    }
}

static XHCI_TBT_CHAIN_SET cs;

static VOID test_chain_ports(VOID)
{
    MODEL_FN *f;
    ULONG r;
    ULONG d;

    /* The dock, nothing chained, then something chained. */
    chdock(0);
    r = XhciTbtChainPorts(&io, XHCI_TBT_BDF(2, 1, 0), 2, &cs);
    CHECK(r == XHCI_TBT_CHAIN_COMPLETE && !cs.Incomplete, "chain dock ok");
    CHECK(cs.Count == 1 && cs.Over == 0, "chain dock one");
    CHECK(cs.Port[0].Bdf == XHCI_TBT_BDF(5, 1, 0), "chain dock where");
    CHECK(cs.Port[0].Id == 0x8233104CUL, "chain dock id");
    CHECK(cs.Port[0].Secondary == 7 && cs.Port[0].Member == 2,
          "chain dock sec member");
    CHECK(cs.Port[0].Depth == 2 && cs.Port[0].Unpowered == 0,
          "chain dock depth");
    CHECK(model.Writes == 0, "chain reads only");
    chdock(1);
    r = XhciTbtChainPorts(&io, XHCI_TBT_BDF(2, 1, 0), 0, &cs);
    CHECK(r == XHCI_TBT_CHAIN_COMPLETE && cs.Count == 1 &&
          cs.Port[0].Bdf == XHCI_TBT_BDF(5, 1, 0), "chain dock chained");
    /* No descent below a candidate: the chained bus is never read. */
    CHECK(model.MaxBusRead == 6, "chain no descent");

    /* A power controller reading power off: flagged, recorded. */
    chdock(0);
    f = mfind(XHCI_TBT_BDF(5, 1, 0));
    put32(f, 0x40 + XHCI_TBT_PCIE_SLOTCAP, CH_HPC | CH_PCP);
    put16(f, 0x40 + XHCI_TBT_PCIE_SLOTSTA, XHCI_TBT_SLOTCTL_PCC_OFF);
    r = XhciTbtChainPorts(&io, XHCI_TBT_BDF(2, 1, 0), 0, &cs);
    CHECK(r == XHCI_TBT_CHAIN_COMPLETE && cs.Count == 1 &&
          cs.Port[0].Unpowered == 1, "chain unpowered");
    put16(f, 0x40 + XHCI_TBT_PCIE_SLOTSTA, 0);
    r = XhciTbtChainPorts(&io, XHCI_TBT_BDF(2, 1, 0), 0, &cs);
    CHECK(r == XHCI_TBT_CHAIN_COMPLETE && cs.Port[0].Unpowered == 0,
          "chain powered");

    /* A five-port switch with one hot-plug port (the TS3 Plus's shape),
     * multi-function upstream port included. */
    mreset();
    ioreset();
    f = mbridge(2, 1, 0, 0x8086, 0x15C0, 4, 0x0A);
    mpcie(f, 0x40, CH_DOWN, 1, 0, 0, CH_HPC, 0);
    chbr(4, 0, 0, 0x8232, CH_UP, 5, 0x0A, 0);
    for (d = 0; d < 5; d++) {
        chbr(5, d, 0, 0x8233, CH_DOWN, 6 + d, 6 + d, d == 4 ? CH_HPC : 0);
    }
    mdev(5, 0, 1, 0x104C, 0x8240, 0x088000UL);
    mmulti(5, 0);
    r = XhciTbtChainPorts(&io, XHCI_TBT_BDF(2, 1, 0), 1, &cs);
    CHECK(r == XHCI_TBT_CHAIN_COMPLETE && cs.Count == 1 &&
          cs.Port[0].Bdf == XHCI_TBT_BDF(5, 4, 0) &&
          cs.Port[0].Secondary == 0x0A, "chain five ports");

    /* None: an enclosure's NVMe directly on the member's bus. */
    mreset();
    ioreset();
    f = mbridge(2, 1, 0, 0x8086, 0x15C0, 4, 4);
    mpcie(f, 0x40, CH_DOWN, 1, 0, 0, CH_HPC, 0);
    mdev(4, 0, 0, 0x1B36, 0x0010, 0x010802UL);
    r = XhciTbtChainPorts(&io, XHCI_TBT_BDF(2, 1, 0), 0, &cs);
    CHECK(r == XHCI_TBT_CHAIN_COMPLETE && cs.Count == 0, "chain none");

    /* Not candidates: a hot-plug root-port type, an upstream port with a
     * slot bit, a downstream port without Slot Implemented, a plain
     * conventional bridge. */
    mreset();
    ioreset();
    f = mbridge(2, 1, 0, 0x8086, 0x15C0, 4, 9);
    mpcie(f, 0x40, CH_DOWN, 1, 0, 0, CH_HPC, 0);
    chbr(4, 0, 0, 0x8232, CH_UP, 5, 9, CH_HPC);
    chbr(5, 0, 0, 0x8233, CH_ROOT, 6, 6, CH_HPC);
    f = mbridge(5, 1, 0, 0x104C, 0x8233, 7, 7);
    mpcie(f, 0x40, CH_DOWN, 0, 0, 0, CH_HPC, 0);
    mbridge(5, 2, 0, 0x104C, 0x8233, 8, 9);
    r = XhciTbtChainPorts(&io, XHCI_TBT_BDF(2, 1, 0), 0, &cs);
    CHECK(r == XHCI_TBT_CHAIN_COMPLETE && cs.Count == 0, "chain not cand");

    /* The count bound: six hot-plug ports, four recorded. */
    mreset();
    ioreset();
    f = mbridge(2, 1, 0, 0x8086, 0x15C0, 4, 0x0B);
    mpcie(f, 0x40, CH_DOWN, 1, 0, 0, CH_HPC, 0);
    chbr(4, 0, 0, 0x8232, CH_UP, 5, 0x0B, 0);
    for (d = 0; d < 6; d++) {
        chbr(5, d, 0, 0x8233, CH_DOWN, 6 + d, 6 + d, CH_HPC);
    }
    r = XhciTbtChainPorts(&io, XHCI_TBT_BDF(2, 1, 0), 0, &cs);
    CHECK(r == XHCI_TBT_CHAIN_COMPLETE && cs.Count == XHCI_TBT_CHAIN_MAX &&
          cs.Over == 2, "chain count bound");

    /* The level bound: five bridges below the member, a hot-plug port at
     * level 6 is found, one at level 7 is not walked. */
    mreset();
    ioreset();
    f = mbridge(2, 1, 0, 0x8086, 0x15C0, 4, 0x20);
    mpcie(f, 0x40, CH_DOWN, 1, 0, 0, CH_HPC, 0);
    for (d = 0; d < 5; d++) {
        chbr(4 + d, 0, 0, 0x8232, CH_UP, 5 + d, 0x20, 0);
    }
    chbr(9, 0, 0, 0x8233, CH_DOWN, 0x0A, 0x0A, CH_HPC);
    r = XhciTbtChainPorts(&io, XHCI_TBT_BDF(2, 1, 0), 0, &cs);
    CHECK(r == XHCI_TBT_CHAIN_COMPLETE && cs.Count == 1 &&
          cs.Port[0].Depth == XHCI_TBT_CHAIN_LEVELS, "chain level 6");
    f = mfind(XHCI_TBT_BDF(9, 0, 0));
    f->Cfg[0x40 + 2] = (UCHAR)(0x2UL | (CH_UP << 4));
    f->Cfg[0x19] = 0x0A;
    chbr(0x0A, 0, 0, 0x8233, CH_DOWN, 0x0B, 0x0B, CH_HPC);
    r = XhciTbtChainPorts(&io, XHCI_TBT_BDF(2, 1, 0), 0, &cs);
    CHECK(r == XHCI_TBT_CHAIN_COMPLETE && cs.Count == 0 &&
          model.MaxBusRead == 9, "chain level 7 not walked");

    /* A bridge numbered outside the member's range is not walked. */
    chdock(0);
    f = mfind(XHCI_TBT_BDF(5, 0, 0));
    f->Cfg[0x19] = 0x30;
    f->Cfg[0x1A] = 0x30;
    mdev(0x30, 0, 0, 0x104C, 0x8233, 0x060400UL);
    r = XhciTbtChainPorts(&io, XHCI_TBT_BDF(2, 1, 0), 0, &cs);
    CHECK(r == XHCI_TBT_CHAIN_COMPLETE && cs.Count == 1 &&
          model.MaxBusRead == 5, "chain out of range");

    /* The member unnumbered, all ones, unread. */
    chdock(0);
    f = mfind(XHCI_TBT_BDF(2, 1, 0));
    f->Cfg[0x19] = 0;
    f->Cfg[0x1A] = 0;
    r = XhciTbtChainPorts(&io, XHCI_TBT_BDF(2, 1, 0), 0, &cs);
    CHECK(r == XHCI_TBT_CHAIN_INCOMPLETE &&
          cs.Why == XHCI_TBT_CHAIN_WHY_MEMBER, "chain member unnumbered");
    r = XhciTbtChainPorts(&io, XHCI_TBT_BDF(3, 0, 0), 0, &cs);
    CHECK(r == XHCI_TBT_CHAIN_INCOMPLETE &&
          cs.Why == XHCI_TBT_CHAIN_WHY_MEMBER, "chain member absent");
    chdock(0);
    model.FailBdf = XHCI_TBT_BDF(2, 1, 0);
    r = XhciTbtChainPorts(&io, XHCI_TBT_BDF(2, 1, 0), 0, &cs);
    CHECK(r == XHCI_TBT_CHAIN_INCOMPLETE &&
          cs.FailBdf == XHCI_TBT_BDF(2, 1, 0), "chain member unread");

    /* A failed read, a failed capability walk, a retry status. */
    chdock(0);
    model.FailBdf = XHCI_TBT_BDF(5, 1, 0);
    model.FailOffset = XHCI_TBT_CFG_ID;
    r = XhciTbtChainPorts(&io, XHCI_TBT_BDF(2, 1, 0), 0, &cs);
    CHECK(r == XHCI_TBT_CHAIN_INCOMPLETE && cs.Incomplete &&
          cs.Why == XHCI_TBT_CHAIN_WHY_READ &&
          cs.FailBdf == XHCI_TBT_BDF(5, 1, 0), "chain read fails");
    chdock(0);
    model.FailBdf = XHCI_TBT_BDF(5, 1, 0);
    model.FailOffset = 0x40;
    r = XhciTbtChainPorts(&io, XHCI_TBT_BDF(2, 1, 0), 0, &cs);
    CHECK(r == XHCI_TBT_CHAIN_INCOMPLETE &&
          cs.Why == XHCI_TBT_CHAIN_WHY_CAP, "chain cap fails");
    chdock(0);
    model.FailBdf = XHCI_TBT_BDF(5, 1, 0);
    model.FailOffset = 0x40 + XHCI_TBT_PCIE_SLOTCAP;
    r = XhciTbtChainPorts(&io, XHCI_TBT_BDF(2, 1, 0), 0, &cs);
    CHECK(r == XHCI_TBT_CHAIN_INCOMPLETE &&
          cs.Why == XHCI_TBT_CHAIN_WHY_READ, "chain slot cap fails");
    chdock(0);
    f = mfind(XHCI_TBT_BDF(5, 0, 0));
    put16(f, 0x00, 0x0001);
    r = XhciTbtChainPorts(&io, XHCI_TBT_BDF(2, 1, 0), 0, &cs);
    CHECK(r == XHCI_TBT_CHAIN_INCOMPLETE &&
          cs.Why == XHCI_TBT_CHAIN_WHY_RETRY &&
          cs.FailBdf == XHCI_TBT_BDF(5, 0, 0), "chain retry status");

    /* Parameters. */
    CHECK(XhciTbtChainPorts(NULL, XHCI_TBT_BDF(2, 1, 0), 0, &cs) ==
          XHCI_TBT_CHAIN_BAD_PARAM, "chain no io");
    CHECK(XhciTbtChainPorts(&io, XHCI_TBT_BDF(2, 1, 0), 0, NULL) ==
          XHCI_TBT_CHAIN_BAD_PARAM, "chain no out");
}

static VOID test_chain_owed(VOID)
{
    XHCI_TBT_CHAIN_OWED owed[XHCI_TBT_CHAIN_MAX];
    ULONG i;
    ULONG n;

    for (i = 0; i < sizeof(owed) / sizeof(ULONG); i++) {
        ((PULONG)owed)[i] = 0;
    }
    n = XHCI_TBT_CHAIN_MAX;
    /* Owed at 1000 for the device of table entry 5: a delayed
     * enumeration retried, still the first time. */
    CHECK(XhciTbtChainOwe(owed, n, 5, 7, XHCI_TBT_CHAIN_MISS_NONE,
                          1000) == 0, "owe first");
    CHECK(owed[0].Used && owed[0].Key == 5 && owed[0].Since == 1000 &&
          owed[0].Serial == 7 && owed[0].DoneCount == 0, "owe kept");
    /* One candidate done of two: the second stays owed. */
    XhciTbtChainMark(&owed[0], XHCI_TBT_BDF(5, 1, 0));
    XhciTbtChainMark(&owed[0], XHCI_TBT_BDF(5, 1, 0));
    CHECK(owed[0].DoneCount == 1 &&
          XhciTbtChainDone(&owed[0], XHCI_TBT_BDF(5, 1, 0)) &&
          !XhciTbtChainDone(&owed[0], XHCI_TBT_BDF(5, 2, 0)),
          "owe one done");
    CHECK(XhciTbtChainOwe(owed, n, 5, 7, XHCI_TBT_CHAIN_MISS_FILTER,
                          5000) == 0 && owed[0].Since == 1000 &&
          owed[0].DoneCount == 1 &&
          owed[0].Missing == XHCI_TBT_CHAIN_MISS_FILTER, "owe retried");
    CHECK(!XhciTbtChainOwedExpired(&owed[0], 10999), "owe not expired");
    CHECK(XhciTbtChainOwedExpired(&owed[0], 11000), "owe expired 10 s");
    /* A new tunnel for the same device: the clock restarts, nothing done. */
    CHECK(XhciTbtChainOwe(owed, n, 5, 8, XHCI_TBT_CHAIN_MISS_NONE,
                          9000) == 0 && owed[0].Since == 9000 &&
          owed[0].Serial == 8 && owed[0].DoneCount == 0, "owe new serial");
    CHECK(!XhciTbtChainOwedExpired(&owed[0], 11000), "owe restarted");
    /* Two children at the same link and depth (Titan Ridge routes 0303
     * and 0503) are two keys: neither replaces the other. */
    CHECK(XhciTbtChainOwe(owed, n, 6, 1, 0, 0) == 1, "owe second");
    CHECK(owed[0].Key == 5 && owed[1].Key == 6, "owe two children");
    CHECK(XhciTbtChainOwe(owed, n, 7, 1, 0, 0) == 2, "owe third");
    CHECK(XhciTbtChainOwe(owed, n, 8, 1, 0, 0) == 3, "owe fourth");
    CHECK(XhciTbtChainOwe(owed, n, 9, 1, 0, 0) == n, "owe full");
    owed[1].Used = 0;
    CHECK(XhciTbtChainOwe(owed, n, 9, 1, 0, 0) == 1 &&
          owed[1].DoneCount == 0, "owe reused");
    /* The done list's bound. */
    for (i = 0; i < 40; i++) {
        XhciTbtChainMark(&owed[2], XHCI_TBT_BDF(5, i & 31, i >> 5));
    }
    CHECK(owed[2].DoneCount == XHCI_TBT_CHAIN_MAX * XHCI_TBT_MAX_TUNNELS,
          "owe done bound");
    /* The millisecond counter wrapping. */
    CHECK(XhciTbtChainOwe(owed, n, 5, 9, 0, 0xFFFFF000UL) == 0,
          "owe near wrap");
    CHECK(!XhciTbtChainOwedExpired(&owed[0], 0x00001000UL), "owe wrap 8 s");
    CHECK(XhciTbtChainOwedExpired(&owed[0], 0x00002000UL), "owe wrap 12 s");
    CHECK(!XhciTbtChainOwedExpired(NULL, 0), "owe null");
    CHECK(!XhciTbtChainDone(NULL, 0), "owe done null");
    XhciTbtChainMark(NULL, 0);
    CHECK(XhciTbtChainOwe(NULL, n, 5, 9, 0, 0) == n, "owe no table");
}

/* Record 34 section 3.10: the chained unit's parent, by the stricter test. */
static VOID test_chain_port_test(VOID)
{
    MODEL_FN *f;
    ULONG sec;
    ULONG r;
    ULONG mb = XHCI_TBT_BDF(2, 1, 0);
    ULONG cp = XHCI_TBT_BDF(5, 1, 0);

    /* The emulator's chain port, with and without a device behind it. */
    chdock(0);
    r = XhciTbtChainPortTest(&io, cp, mb, &sec);
    CHECK(r == XHCI_TBT_CPT_YES && sec == 7, "cpt dock");
    CHECK(model.Writes == 0, "cpt reads only");
    chdock(1);
    r = XhciTbtChainPortTest(&io, cp, mb, &sec);
    CHECK(r == XHCI_TBT_CPT_YES && sec == 7, "cpt dock chained");

    /* Numbered bridges that are no chain port: the fixed downstream port,
     * the upstream port; and what is no bridge in the range: the NVMe
     * (type 0), the member itself (outside its own range). */
    sec = 99;
    r = XhciTbtChainPortTest(&io, XHCI_TBT_BDF(5, 0, 0), mb, &sec);
    CHECK(r == XHCI_TBT_CPT_NOT && sec == 0, "cpt fixed port");
    r = XhciTbtChainPortTest(&io, XHCI_TBT_BDF(4, 0, 0), mb, &sec);
    CHECK(r == XHCI_TBT_CPT_NOT, "cpt upstream port");
    r = XhciTbtChainPortTest(&io, XHCI_TBT_BDF(6, 0, 0), mb, &sec);
    CHECK(r == XHCI_TBT_CPT_OUTSIDE, "cpt type 0");
    r = XhciTbtChainPortTest(&io, mb, mb, &sec);
    CHECK(r == XHCI_TBT_CPT_OUTSIDE, "cpt the member");

    /* An unnumbered upstream or fixed port is not NOT (an ancestor read so
     * withholds its chain port). */
    f = mfind(XHCI_TBT_BDF(4, 0, 0));
    put32(f, XHCI_TBT_CFG_BUSES, 0x00000004UL);
    r = XhciTbtChainPortTest(&io, XHCI_TBT_BDF(4, 0, 0), mb, &sec);
    CHECK(r == XHCI_TBT_CPT_UNNUMBERED, "cpt upstream unnumbered");
    put32(f, XHCI_TBT_CFG_BUSES, 0x00080504UL);
    r = XhciTbtChainPortTest(&io, XHCI_TBT_BDF(4, 0, 0), mb, &sec);
    CHECK(r == XHCI_TBT_CPT_UNNUMBERED, "cpt upstream past member");
    f = mfind(XHCI_TBT_BDF(5, 0, 0));
    put32(f, XHCI_TBT_CFG_BUSES, 0x00050505UL);
    r = XhciTbtChainPortTest(&io, XHCI_TBT_BDF(5, 0, 0), mb, &sec);
    CHECK(r == XHCI_TBT_CPT_UNNUMBERED, "cpt fixed unnumbered");
    r = XhciTbtChainPortTest(&io, XHCI_TBT_BDF(5, 9, 0), mb, &sec);
    CHECK(r == XHCI_TBT_CPT_FAILED, "cpt absent");

    /* A root-port type with a slot; Slot Implemented clear; Hot-Plug
     * Capable clear; no PCI Express capability. */
    chdock(0);
    f = mfind(cp);
    put16(f, 0x40 + 2, (CH_ROOT << 4) | 0x0100UL | 2UL);
    r = XhciTbtChainPortTest(&io, cp, mb, &sec);
    CHECK(r == XHCI_TBT_CPT_NOT, "cpt root type");
    chdock(0);
    f = mfind(cp);
    put16(f, 0x40 + 2, (CH_DOWN << 4) | 2UL);
    r = XhciTbtChainPortTest(&io, cp, mb, &sec);
    CHECK(r == XHCI_TBT_CPT_NOT, "cpt no slot");
    chdock(0);
    f = mfind(cp);
    put32(f, 0x40 + XHCI_TBT_PCIE_SLOTCAP, 0);
    r = XhciTbtChainPortTest(&io, cp, mb, &sec);
    CHECK(r == XHCI_TBT_CPT_NOT, "cpt no hpc");
    chdock(0);
    f = mfind(cp);
    put16(f, XHCI_TBT_CFG_STATUS + 2, 0);
    f->Cfg[XHCI_TBT_CFG_CAPPTR] = 0;
    r = XhciTbtChainPortTest(&io, cp, mb, &sec);
    CHECK(r == XHCI_TBT_CPT_NOT, "cpt no pcie cap");

    /* The bus numbers: unnumbered; secondary at its own bus; at the
     * member's secondary; subordinate below secondary; past the member's
     * subordinate. */
    chdock(0);
    f = mfind(cp);
    put32(f, XHCI_TBT_CFG_BUSES, 0x00000005UL);
    sec = 99;
    r = XhciTbtChainPortTest(&io, cp, mb, &sec);
    CHECK(r == XHCI_TBT_CPT_UNNUMBERED && sec == 0, "cpt unnumbered");
    put32(f, XHCI_TBT_CFG_BUSES, 0x00050505UL);
    r = XhciTbtChainPortTest(&io, cp, mb, &sec);
    CHECK(r == XHCI_TBT_CPT_UNNUMBERED, "cpt sec at own bus");
    put32(f, XHCI_TBT_CFG_BUSES, 0x00070405UL);
    r = XhciTbtChainPortTest(&io, cp, mb, &sec);
    CHECK(r == XHCI_TBT_CPT_UNNUMBERED, "cpt sec at member sec");
    put32(f, XHCI_TBT_CFG_BUSES, 0x00060705UL);
    r = XhciTbtChainPortTest(&io, cp, mb, &sec);
    CHECK(r == XHCI_TBT_CPT_UNNUMBERED, "cpt sub below sec");
    put32(f, XHCI_TBT_CFG_BUSES, 0x00080705UL);
    r = XhciTbtChainPortTest(&io, cp, mb, &sec);
    CHECK(r == XHCI_TBT_CPT_UNNUMBERED, "cpt past member sub");
    put32(f, XHCI_TBT_CFG_BUSES, 0x00070705UL);
    r = XhciTbtChainPortTest(&io, cp, mb, &sec);
    CHECK(r == XHCI_TBT_CPT_YES && sec == 7, "cpt restored");

    /* The port on a bus outside the member's range. */
    chdock(0);
    f = mfind(mb);
    put32(f, XHCI_TBT_CFG_BUSES, 0x00040402UL);
    r = XhciTbtChainPortTest(&io, cp, mb, &sec);
    CHECK(r == XHCI_TBT_CPT_OUTSIDE, "cpt outside member");

    /* The member: unnumbered, unread, absent (all ones). */
    put32(f, XHCI_TBT_CFG_BUSES, 0x00000002UL);
    r = XhciTbtChainPortTest(&io, cp, mb, &sec);
    CHECK(r == XHCI_TBT_CPT_MEMBER, "cpt member unnumbered");
    put32(f, XHCI_TBT_CFG_BUSES, 0x00070402UL);
    model.FailBdf = mb;
    r = XhciTbtChainPortTest(&io, cp, mb, &sec);
    CHECK(r == XHCI_TBT_CPT_MEMBER, "cpt member unread");
    model.FailBdf = XHCI_TBT_NO_BDF;
    chdock(0);
    mfind(mb)->Bdf = XHCI_TBT_BDF(0x30, 0, 0);
    r = XhciTbtChainPortTest(&io, cp, mb, &sec);
    CHECK(r == XHCI_TBT_CPT_MEMBER, "cpt member all ones");

    /* The port: a failed read at each step, Retry Status. */
    chdock(0);
    model.FailBdf = cp;
    model.FailOffset = XHCI_TBT_CFG_ID;
    r = XhciTbtChainPortTest(&io, cp, mb, &sec);
    CHECK(r == XHCI_TBT_CPT_FAILED, "cpt id unread");
    model.FailOffset = XHCI_TBT_CFG_HEADER;
    r = XhciTbtChainPortTest(&io, cp, mb, &sec);
    CHECK(r == XHCI_TBT_CPT_FAILED, "cpt header unread");
    model.FailOffset = XHCI_TBT_CFG_BUSES;
    r = XhciTbtChainPortTest(&io, cp, mb, &sec);
    CHECK(r == XHCI_TBT_CPT_FAILED, "cpt buses unread");
    model.FailOffset = 0x40;
    r = XhciTbtChainPortTest(&io, cp, mb, &sec);
    CHECK(r == XHCI_TBT_CPT_FAILED, "cpt cap walk failed");
    model.FailOffset = 0x40 + XHCI_TBT_PCIE_SLOTCAP;
    r = XhciTbtChainPortTest(&io, cp, mb, &sec);
    CHECK(r == XHCI_TBT_CPT_FAILED, "cpt slotcap unread");
    model.FailBdf = XHCI_TBT_NO_BDF;
    model.FailOffset = XHCI_TBT_NO_BDF;
    f = mfind(cp);
    put16(f, XHCI_TBT_CFG_ID, 0x0001UL);
    r = XhciTbtChainPortTest(&io, cp, mb, &sec);
    CHECK(r == XHCI_TBT_CPT_FAILED, "cpt retry status");

    /* A dword of the capability reading all ones through an access that
     * succeeds (the function stopped answering). */
    chdock(0);
    f = mfind(cp);
    put32(f, 0x40 + XHCI_TBT_PCIE_SLOTCAP, 0xFFFFFFFFUL);
    sec = 99;
    r = XhciTbtChainPortTest(&io, cp, mb, &sec);
    CHECK(r == XHCI_TBT_CPT_FAILED && sec == 0, "cpt slotcap all ones");
    chdock(0);
    f = mfind(cp);
    put32(f, 0x40 + XHCI_TBT_PCIE_CAPS, 0xFFFFFFFFUL);
    r = XhciTbtChainPortTest(&io, cp, mb, &sec);
    CHECK(r == XHCI_TBT_CPT_FAILED, "cpt caps all ones");

    /* Bad parameters. */
    CHECK(XhciTbtChainPortTest(NULL, cp, mb, &sec) == XHCI_TBT_CPT_FAILED,
          "cpt no io");
    CHECK(XhciTbtChainPortTest(&io, cp, mb, NULL) == XHCI_TBT_CPT_FAILED,
          "cpt no out");
    CHECK(XhciTbtChainPortTest(&io, 0x10000UL, mb, &sec) ==
          XHCI_TBT_CPT_FAILED, "cpt bad bdf");
}

/* Record 22 section 7.6.1 (task 46.23): the ACPI-named source, under the
 * router-named source's rules, unbound. */
static void test_acpi(void)
{
    ULONG over[1];

    two_hp();
    mdev(0, 0, 0, 0x8086, 0x29C0, 0x060000UL);
    XhciTbtBindsInit(&binds);
    CHECK_EQ(find(B(0, 4, 0), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_UNLISTED,
             "acpi: the discovery takes nothing");
    CHECK_EQ(XhciTbtTopoAddAcpi(NULL, &topo, B(0, 5, 0)),
             XHCI_TBT_MAP_BAD_PARAM, "acpi: no accessor");
    CHECK_EQ(XhciTbtTopoAddAcpi(&io, NULL, B(0, 5, 0)),
             XHCI_TBT_MAP_BAD_PARAM, "acpi: no discovery");
    CHECK_EQ(XhciTbtTopoAddAcpi(&io, &topo, 0x10000UL),
             XHCI_TBT_MAP_BAD_PARAM, "acpi: a location past 16 bits");
    CHECK_EQ(XhciTbtTopoAddAcpi(&io, &topo, B(0, 4, 0)), XHCI_TBT_MAP_REFUSED,
             "acpi: never the NHI");
    CHECK_EQ(XhciTbtTopoAddAcpi(&io, &topo, B(0, 0, 0)),
             XHCI_TBT_MAP_NO_BRIDGE, "acpi: not a bridge");
    CHECK_EQ(XhciTbtTopoAddAcpi(&io, &topo, B(0, 0x1F, 0)),
             XHCI_TBT_MAP_NO_BRIDGE, "acpi: nothing there");
    CHECK_EQ(XhciTbtTopoAddAcpi(&io, &topo, B(0, 5, 0)), XHCI_TBT_MAP_ADDED,
             "acpi: the claimed root port joins");
    CHECK(topo.Outcome == XHCI_TBT_TOPO_SET &&
          topo.FindOutcome == XHCI_TBT_TOPO_UNLISTED &&
          topo.Tunnels == 1 && topo.Set[0] == B(0, 5, 0) &&
          topo.Source[0] == XHCI_TBT_SRC_ACPI && topo.AcpiNamed == 1 &&
          topo.Named == 0, "acpi: SET, source ACPI");
    CHECK(binds.Count == 0 && member(1) == XHCI_TBT_NO_MEMBER &&
          member(2) == XHCI_TBT_NO_MEMBER,
          "acpi: unbound - the firmware's port number binds nothing");
    CHECK_EQ(XhciTbtTopoAddAcpi(&io, &topo, B(0, 5, 0)), XHCI_TBT_MAP_KNOWN,
             "acpi: the same claim again is KNOWN");
    CHECK_EQ(topo.Tunnels, 1, "acpi: not added twice");
    CHECK_EQ(named(2, B(0, 5, 0)), XHCI_TBT_MAP_KNOWN,
             "acpi: the router names it: bound, KNOWN");
    CHECK(topo.Source[0] == XHCI_TBT_SRC_ACPI && member(2) == 0,
          "acpi: the source kept, the binding the router's");
    CHECK_EQ(XhciTbtTopoAddAcpi(&io, &topo, B(0, 6, 0)), XHCI_TBT_MAP_ADDED,
             "acpi: a second port");
    CHECK(topo.Tunnels == 2 && topo.AcpiNamed == 2, "acpi: two members");

    /* A bridge the router named first keeps ROUTER. */
    CHECK_EQ(find(B(0, 4, 0), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_UNLISTED,
             "acpi: rediscovered");
    CHECK_EQ(named(1, B(0, 6, 0)), XHCI_TBT_MAP_ADDED, "acpi: router-named");
    CHECK_EQ(XhciTbtTopoAddAcpi(&io, &topo, B(0, 6, 0)), XHCI_TBT_MAP_KNOWN,
             "acpi: then claimed: KNOWN");
    CHECK(topo.Source[0] == XHCI_TBT_SRC_ROUTER && topo.AcpiNamed == 0,
          "acpi: ROUTER kept");

    /* The override is the whole set. */
    over[0] = B(0, 5, 0);
    CHECK_EQ(findin(B(0, 4, 0), over, 1), XHCI_TBT_TOPO_OVERRIDE,
             "acpi: an override");
    CHECK_EQ(XhciTbtTopoAddAcpi(&io, &topo, B(0, 6, 0)), XHCI_TBT_MAP_REFUSED,
             "acpi: another bridge refused under the override");
    CHECK_EQ(XhciTbtTopoAddAcpi(&io, &topo, B(0, 5, 0)), XHCI_TBT_MAP_KNOWN,
             "acpi: the override's own member is KNOWN");
    CHECK_EQ(topo.Source[0], XHCI_TBT_SRC_OVERRIDE, "acpi: OVERRIDE kept");
}

/* ---- the chained tunnels' bindings (record 34 section 3.12; task 48.30) */

/* A function hidden from the model (unplugged). */
static VOID mgone(ULONG bdf)
{
    MODEL_FN *f = mfind(bdf);

    if (f != NULL) {
        f->Bdf = 0x7FFFFFFFUL;
    }
}

/*
 * The emulator's dock with two chain ports: the tunnel port 02:01.0 (buses
 * 4 to 8), the dock's upstream port 04:00.0 (5 to 8), its fixed port
 * 05:00.0 to its NVMe on bus 6, chain ports 05:01.0 (bus 7) and 05:02.0
 * (bus 8).
 */
static VOID chdock2(VOID)
{
    MODEL_FN *tp;

    mreset();
    ioreset();
    tp = mbridge(2, 1, 0, 0x8086, 0x15C0, 4, 8);
    mpcie(tp, 0x40, CH_DOWN, 1, 0, 0, CH_HPC, 0);
    chbr(4, 0, 0, 0x8232, CH_UP, 5, 8, 0);
    chbr(5, 0, 0, 0x8233, CH_DOWN, 6, 6, 0);
    mdev(6, 0, 0, 0x1B36, 0x0010, 0x010802UL);
    chbr(5, 1, 0, 0x8233, CH_DOWN, 7, 7, CH_HPC);
    chbr(5, 2, 0, 0x8233, CH_DOWN, 8, 8, CH_HPC);
}

static XHCI_CHAIN ch;

/* A kid's function on the chain port's bus `bus`, as its tunnel brings it:
 * the port's presence follows (link_follow). */
static VOID kid_on(ULONG bus, ULONG dev)
{
    mdev(bus, dev, 0, 0x1B36, 0x0010, 0x010802UL);
    link_follow(bus);
}

static VOID kid_gone(ULONG bus)
{
    mgone(XHCI_TBT_BDF(bus, 0, 0));
    link_follow(bus);
}

/* A kid's upstream switch port at device 0 of `bus`, its chain port's
 * presence left as it is. */
static VOID kid_usp(ULONG bus)
{
    MODEL_FN *f = mbridge(bus, 0, 0, 0x1B21, 0x2463, bus + 1, bus + 1);

    mpcie(f, 0x80, XHCI_TBT_PCIE_TYPE_UP, 0, 0, 0, 0, 0);
}

static VOID chkid(XHCI_CHAIN_KID *k, ULONG kid, ULONG gen)
{
    memset(k, 0, sizeof(*k));
    k->Port = 2;
    k->Kid = kid;
    k->KidGen = gen;
    k->ParentGen = 9;
    k->ParentUuid[0] = 0x11;
    k->ParentUuid[1] = 0x22;
    k->ParentPcieDown = 7;
    k->KidRoute[0] = 0x0302;
    k->KidUuid[0] = 0x33 + gen;
    k->Member = 0;
    k->MemberBdf = XHCI_TBT_BDF(2, 1, 0);
}

static VOID test_chain_bind(VOID)
{
    XHCI_CHAIN_BASE b;
    XHCI_CHAIN_KID k;
    ULONG tok[XHCI_CHAIN_TOK_WORDS];
    ULONG why;
    ULONG r;
    ULONG i;
    ULONG j;
    ULONG epoch;
    MODEL_FN *f;

    /* Record 34 section 3.3.7 item 2: each candidate's rotation, the
     * device numbers from the member's secondary bus summed mod 4. */
    chdock(0);
    CHECK(XhciTbtChainPorts(&io, XHCI_TBT_BDF(2, 1, 0), 0, &cs) == XHCI_TBT_CHAIN_COMPLETE &&
          cs.Port[0].Rot == 1, "rotation: the dock's port 0 then device 1");
    chdock2();
    CHECK(XhciTbtChainPorts(&io, XHCI_TBT_BDF(2, 1, 0), 0, &cs) == XHCI_TBT_CHAIN_COMPLETE &&
          cs.Count == 2 && cs.Port[0].Rot == 1 && cs.Port[1].Rot == 2,
          "rotation: devices 1 and 2");

    /* The reading: the dock, nothing chained, then something chained. */
    chdock(0);
    r = XhciChainRead(&io, XHCI_TBT_BDF(2, 1, 0), 0, &b);
    CHECK(r == XHCI_CHAIN_READ_OK && b.Count == 1, "chain read: one port");
    CHECK(b.Bdf[0] == XHCI_TBT_BDF(5, 1, 0) && b.Buses[0] == 0x0707UL &&
          b.Seen[0] == 0, "chain read: the port, its buses, empty");
    CHECK(model.Writes == 0, "chain read: reads only");
    mdev(7, 0, 0, 0x1B36, 0x0010, 0x010802UL);
    r = XhciChainRead(&io, XHCI_TBT_BDF(2, 1, 0), 0, &b);
    CHECK(r == XHCI_CHAIN_READ_OK && b.Seen[0] == 1UL, "chain read: device 0");
    /* Failures. */
    model.FailBdf = XHCI_TBT_BDF(7, 0, 0);
    model.FailOffset = 0;
    CHECK(XhciChainRead(&io, XHCI_TBT_BDF(2, 1, 0), 0, &b) == XHCI_CHAIN_READ_UNREAD,
          "chain read: a failed read is unread");
    model.FailBdf = XHCI_TBT_NO_BDF;
    f = mfind(XHCI_TBT_BDF(7, 0, 0));
    put16(f, 0, 0x0001);
    CHECK(XhciChainRead(&io, XHCI_TBT_BDF(2, 1, 0), 0, &b) == XHCI_CHAIN_READ_UNREAD,
          "chain read: Retry Status is unread");
    put16(f, 0, 0x1B36);
    f = mfind(XHCI_TBT_BDF(5, 1, 0));
    f->Cfg[0x1A] = 6;
    CHECK(XhciChainRead(&io, XHCI_TBT_BDF(2, 1, 0), 0, &b) == XHCI_CHAIN_READ_NUMBER,
          "chain read: a port not numbered inside the member's range");
    chdock(0);
    f = mfind(XHCI_TBT_BDF(5, 1, 0));
    put32(f, 0x40 + XHCI_TBT_PCIE_SLOTCAP, 0);
    CHECK(XhciChainRead(&io, XHCI_TBT_BDF(2, 1, 0), 0, &b) == XHCI_CHAIN_READ_NONE,
          "chain read: no candidate");

    /* A transaction learnt: the arm on an empty port, the kid's device
     * answering after, one gain. */
    chdock(0);
    XhciChainInit(&ch, FALSE);
    chkid(&k, 1, 5);
    CHECK(XhciChainMayConnect(&ch, 0), "nothing open: a connect may start");
    r = XhciChainArm(&ch, &io, &k, FALSE, 1000, 77, &why);
    CHECK(r == XHCI_CHAIN_ARM_OPEN && ch.Txn.Open && ch.Txn.ArmSeq == 77,
          "arm: open, the stamp kept");
    CHECK(!XhciChainMayConnect(&ch, 1500), "a transaction open: a connect waits");
    CHECK(XhciChainTry(&ch, &io, TRUE, FALSE, 1100, &i) == XHCI_CHAIN_TRY_WAIT,
          "try: nothing gained yet waits");
    kid_on(7, 0);
    r = XhciChainTry(&ch, &io, TRUE, FALSE, 1200, &i);
    CHECK(r == XHCI_CHAIN_TRY_LEARNED && i < XHCI_CHAIN_BINDS, "try: learnt");
    CHECK(ch.Bind[i].Bdf == XHCI_TBT_BDF(5, 1, 0) && ch.Bind[i].Port == 2 &&
          ch.Bind[i].ParentGen == 9 && ch.Bind[i].ParentPcieDown == 7 &&
          ch.Bind[i].MemberBdf == XHCI_TBT_BDF(2, 1, 0) &&
          ch.Bind[i].Buses == 0x0707UL, "S: the link and the chain port");
    CHECK(ch.Bind[i].Occupied && ch.Bind[i].Kid == 1 && ch.Bind[i].KidGen == 5 &&
          ch.Bind[i].ArmSeq == 77 && ch.Bind[i].Epoch == 1, "O: the kid");
    CHECK(!ch.Txn.Open && XhciChainMayConnect(&ch, 1300),
          "learnt: closed, the kid settled, a connect may start");
    CHECK(XhciChainOfKid(&ch, 1, 5) == i && XhciChainOfKid(&ch, 1, 6) == XHCI_CHAIN_BINDS,
          "of the kid: its generation only");
    CHECK(XhciChainOfPort(&ch, XHCI_TBT_BDF(2, 1, 0), XHCI_TBT_BDF(5, 1, 0)) == i,
          "of the port");
    XhciChainToken(&ch, i, tok);
    CHECK(tok[0] == (XHCI_CHAIN_TOK_VALID | 2UL | (1UL << 8)) && tok[1] == 5 &&
          tok[2] == 1, "the token");
    CHECK(XhciChainTokenLive(&ch, i, tok), "the token live");
    CHECK(XHCI_CHAIN_TOK_PORT(tok) == 2 && XHCI_CHAIN_TOK_KID(tok) == 1, "the token's fields");
    /* The incarnation: the first fixes it, the same keeps it, another
     * withdraws S. */
    CHECK(XhciChainIncarnation(&ch, i, 40) && ch.Bind[i].Inc == 40, "incarnation fixed");
    CHECK(XhciChainIncarnation(&ch, i, 40), "the same incarnation");
    epoch = ch.Bind[i].Epoch;

    /* The child unplugged (its slot freed): O withdrawn, S kept; plugged
     * again (a new generation), learnt again on the same S. */
    kid_gone(7);
    CHECK(XhciChainKidCheck(&ch, 1, FALSE, 0) == 1 && ch.Bind[i].Used &&
          !ch.Bind[i].Occupied, "the kid gone: O withdrawn, S kept");
    CHECK(!XhciChainTokenLive(&ch, i, tok), "the old token dead");
    chkid(&k, 1, 6);
    CHECK(XhciChainArm(&ch, &io, &k, FALSE, 2000, 80, &why) == XHCI_CHAIN_ARM_OPEN,
          "re-plug: armed");
    kid_on(7, 0);
    r = XhciChainTry(&ch, &io, TRUE, FALSE, 2100, &j);
    CHECK(r == XHCI_CHAIN_TRY_LEARNED && j == i && ch.Bind[i].Epoch == epoch &&
          ch.Bind[i].KidGen == 6, "re-plug: learnt on the same S");
    CHECK(XhciChainIncarnation(&ch, i, 41) == 0 && !ch.Bind[i].Used,
          "another incarnation: S withdrawn");

    /* Not opened: no member, blocked, another kid unsettled, busy, the
     * baseline unread; each kid left unsettled. */
    chdock(0);
    XhciChainInit(&ch, FALSE);
    chkid(&k, 1, 5);
    k.Member = XHCI_TBT_NO_MEMBER;
    CHECK(XhciChainArm(&ch, &io, &k, FALSE, 0, 1, &why) == XHCI_CHAIN_ARM_MEMBER,
          "arm: no member");
    CHECK(!ch.Txn.Open && !XhciChainMayConnect(&ch, 9999), "unsettled: a connect waits");
    CHECK(XhciChainMayConnect(&ch, 10000), "... for 10 s at most");
    XhciChainInit(&ch, FALSE);
    chkid(&k, 1, 5);
    CHECK(XhciChainArm(&ch, &io, &k, TRUE, 0, 1, &why) == XHCI_CHAIN_ARM_OTHER,
          "arm: blocked by an unowned path");
    chkid(&k, 2, 7);
    CHECK(XhciChainArm(&ch, &io, &k, FALSE, 0, 1, &why) == XHCI_CHAIN_ARM_OTHER,
          "arm: another kid unsettled");
    CHECK(XhciChainKidGone(&ch, 1, 5) == 0, "kid 1 resolved");
    XhciChainInit(&ch, FALSE);
    chkid(&k, 1, 5);
    model.FailBdf = XHCI_TBT_BDF(7, 0, 0);
    model.FailOffset = 0;
    CHECK(XhciChainArm(&ch, &io, &k, FALSE, 0, 1, &why) == XHCI_CHAIN_ARM_READ &&
          why == XHCI_CHAIN_READ_UNREAD, "arm: the baseline unread");
    model.FailBdf = XHCI_TBT_NO_BDF;
    XhciChainInit(&ch, FALSE);
    CHECK(XhciChainArm(&ch, &io, &k, FALSE, 0, 1, &why) == XHCI_CHAIN_ARM_OPEN,
          "arm: open");
    chkid(&k, 2, 7);
    CHECK(XhciChainArm(&ch, &io, &k, FALSE, 0, 1, &why) == XHCI_CHAIN_ARM_BUSY,
          "arm: another transaction open");
    kid_on(7, 0);
    CHECK(XhciChainTry(&ch, &io, TRUE, FALSE, 100, &i) == XHCI_CHAIN_TRY_SEVERAL,
          "try: the second kid unsettled spoils it");

    /* Two readings that differ (a device answering between them): not
     * stable, not opened. */
    chdock(0);
    XhciChainInit(&ch, FALSE);
    chkid(&k, 1, 5);
    model.FlipBdf = XHCI_TBT_BDF(7, 0, 0);
    model.FlipAfter = 1;
    model.FlipSeen = 0;
    CHECK(XhciChainArm(&ch, &io, &k, FALSE, 0, 1, &why) == XHCI_CHAIN_ARM_UNSTABLE &&
          !ch.Txn.Open, "arm: two readings that differ");
    model.FlipBdf = XHCI_TBT_NO_BDF;
    /* A chain port that appears after the arm: the set changed. */
    chdock2();
    f = mfind(XHCI_TBT_BDF(5, 2, 0));
    put32(f, 0x40 + XHCI_TBT_PCIE_SLOTCAP, 0);
    XhciChainInit(&ch, FALSE);
    CHECK(XhciChainArm(&ch, &io, &k, FALSE, 0, 1, &why) == XHCI_CHAIN_ARM_OPEN &&
          ch.Txn.Base.Count == 1, "armed with one port");
    put32(f, 0x40 + XHCI_TBT_PCIE_SLOTCAP, CH_HPC);
    kid_on(7, 0);
    CHECK(XhciChainTry(&ch, &io, TRUE, FALSE, 10, &i) == XHCI_CHAIN_TRY_SPOILED,
          "try: a new chain port spoils it");

    /* The try's other outcomes. */
    chdock(0);
    XhciChainInit(&ch, FALSE);
    chkid(&k, 1, 5);
    (VOID)XhciChainArm(&ch, &io, &k, FALSE, 0, 1, &why);
    CHECK(XhciChainTry(&ch, &io, FALSE, FALSE, 10, &i) == XHCI_CHAIN_TRY_CANCELLED &&
          !ch.Txn.Open, "try: not live, cancelled");
    (VOID)XhciChainArm(&ch, &io, &k, FALSE, 0, 1, &why);
    CHECK(XhciChainTry(&ch, &io, TRUE, FALSE, 10000, &i) == XHCI_CHAIN_TRY_EXPIRED,
          "try: expired at 10 s");
    (VOID)XhciChainArm(&ch, &io, &k, FALSE, 0, 1, &why);
    kid_on(7, 0);
    CHECK(XhciChainTry(&ch, &io, TRUE, TRUE, 10, &i) == XHCI_CHAIN_TRY_SEVERAL,
          "try: blocked by an unowned path");
    /* A device lost spoils it. */
    (VOID)XhciChainArm(&ch, &io, &k, FALSE, 0, 1, &why);
    kid_gone(7);
    CHECK(XhciChainTry(&ch, &io, TRUE, FALSE, 10, &i) == XHCI_CHAIN_TRY_SPOILED,
          "try: a device lost");
    /* The gain on a port busy at the baseline. */
    kid_on(7, 0);
    (VOID)XhciChainArm(&ch, &io, &k, FALSE, 0, 1, &why);
    kid_on(7, 1);
    CHECK(XhciChainTry(&ch, &io, TRUE, FALSE, 10, &i) == XHCI_CHAIN_TRY_SEVERAL,
          "try: a gain on a port busy at the baseline");
    /* The candidate set changed. */
    chdock(0);
    (VOID)XhciChainArm(&ch, &io, &k, FALSE, 0, 1, &why);
    f = mfind(XHCI_TBT_BDF(5, 1, 0));
    f->Cfg[0x19] = 8;
    f->Cfg[0x1A] = 8;
    f = mfind(XHCI_TBT_BDF(2, 1, 0));
    f->Cfg[0x1A] = 8;
    f = mfind(XHCI_TBT_BDF(4, 0, 0));
    f->Cfg[0x1A] = 8;
    CHECK(XhciChainTry(&ch, &io, TRUE, FALSE, 10, &i) == XHCI_CHAIN_TRY_SPOILED,
          "try: a port's buses moved");

    /* Two chain ports: two gains spoil; a learnt S naming one port and
     * a later gain on the other is a conflict that withdraws it. */
    chdock2();
    XhciChainInit(&ch, FALSE);
    chkid(&k, 1, 5);
    CHECK(XhciChainArm(&ch, &io, &k, FALSE, 0, 1, &why) == XHCI_CHAIN_ARM_OPEN &&
          ch.Txn.Base.Count == 2, "two ports: armed");
    kid_on(7, 0);
    kid_on(8, 0);
    CHECK(XhciChainTry(&ch, &io, TRUE, FALSE, 10, &i) == XHCI_CHAIN_TRY_SPOILED,
          "two gains spoil");
    chdock2();
    XhciChainInit(&ch, FALSE);
    (VOID)XhciChainArm(&ch, &io, &k, FALSE, 0, 1, &why);
    kid_on(7, 0);
    CHECK(XhciChainTry(&ch, &io, TRUE, FALSE, 10, &i) == XHCI_CHAIN_TRY_LEARNED &&
          ch.Bind[i].Bdf == XHCI_TBT_BDF(5, 1, 0), "learnt on the first port");
    kid_gone(7);
    (VOID)XhciChainKidGone(&ch, 1, 5);
    chkid(&k, 1, 6);
    (VOID)XhciChainArm(&ch, &io, &k, FALSE, 20, 2, &why);
    kid_on(8, 0);
    CHECK(XhciChainTry(&ch, &io, TRUE, FALSE, 30, &j) == XHCI_CHAIN_TRY_CONFLICT &&
          !ch.Bind[i].Used && ch.Conflicts == 1, "the same link on another port: a conflict");
    /* A port check: another parent generation withdraws S. */
    chdock(0);
    XhciChainInit(&ch, FALSE);
    chkid(&k, 1, 5);
    (VOID)XhciChainArm(&ch, &io, &k, FALSE, 0, 1, &why);
    kid_on(7, 0);
    (VOID)XhciChainTry(&ch, &io, TRUE, FALSE, 10, &i);
    CHECK(XhciChainPortCheck(&ch, 2, TRUE, 9) == 0 && ch.Bind[i].Used,
          "the same parent: kept");
    CHECK(XhciChainPortCheck(&ch, 3, TRUE, 1) == 0 && ch.Bind[i].Used,
          "another port: kept");
    CHECK(XhciChainKidCheck(&ch, 1, TRUE, 5) == 0 && ch.Bind[i].Occupied,
          "the kid the same: kept");
    CHECK(XhciChainKidCheck(&ch, 1, TRUE, 8) == 1 && !ch.Bind[i].Occupied,
          "the kid connected again: O withdrawn");
    CHECK(XhciChainPortCheck(&ch, 2, TRUE, 10) == (1UL << i) && !ch.Bind[i].Used,
          "the parent connected again: S withdrawn");
    /* Record 22 section 7.6.4: what the chain port gained must be a
     * tunnel's far end, or come with its port read down at the arm and up
     * now; a function answering with no presence change (one of the
     * parent's own, answering late) spoils it and nothing is bound. */
    chdock(0);
    XhciChainInit(&ch, FALSE);
    chkid(&k, 1, 5);
    CHECK(XhciChainArm(&ch, &io, &k, FALSE, 0, 1, &why) == XHCI_CHAIN_ARM_OPEN &&
          ch.Txn.Base.Down == 1, "shape: armed, the chain port read down");
    mdev(7, 0, 0, 0x1B36, 0x0010, 0x010802UL);
    CHECK(XhciChainTry(&ch, &io, TRUE, FALSE, 10, &i) == XHCI_CHAIN_TRY_SHAPE &&
          !ch.Txn.Open && XhciChainOfKid(&ch, 1, 5) == XHCI_CHAIN_BINDS,
          "shape: an endpoint, its port still down: spoiled, nothing bound");
    chdock(0);
    XhciChainInit(&ch, FALSE);
    put16(mfind(XHCI_TBT_BDF(5, 1, 0)), 0x40 + 0x1A, XHCI_TBT_SLOTSTA_PDS);
    CHECK(XhciChainArm(&ch, &io, &k, FALSE, 0, 1, &why) == XHCI_CHAIN_ARM_OPEN &&
          ch.Txn.Base.Down == 0, "shape: the port present at the arm");
    kid_on(7, 0);
    CHECK(XhciChainTry(&ch, &io, TRUE, FALSE, 10, &i) == XHCI_CHAIN_TRY_SHAPE,
          "shape: no down reading, the endpoint refused");
    chdock(0);
    XhciChainInit(&ch, FALSE);
    (VOID)XhciChainArm(&ch, &io, &k, FALSE, 0, 1, &why);
    kid_usp(7);
    CHECK(XhciChainTry(&ch, &io, TRUE, FALSE, 10, &i) == XHCI_CHAIN_TRY_LEARNED,
          "shape: a far end learned with no presence change");
    chdock(0);
    XhciChainInit(&ch, FALSE);
    (VOID)XhciChainArm(&ch, &io, &k, FALSE, 0, 1, &why);
    kid_usp(7);
    model.FailBdf = XHCI_TBT_BDF(7, 0, 0);
    model.FailOffset = XHCI_TBT_CFG_HEADER;
    CHECK(XhciChainTry(&ch, &io, TRUE, FALSE, 10, &i) == XHCI_CHAIN_TRY_WAIT &&
          ch.Txn.Open, "shape: unread, the transaction waits");
    model.FailBdf = XHCI_TBT_NO_BDF;
    model.FailOffset = XHCI_TBT_NO_BDF;
    CHECK(XhciChainTry(&ch, &io, TRUE, FALSE, 20, &i) == XHCI_CHAIN_TRY_LEARNED,
          "shape: then learned");
    /* A gain whose port reading failed waits on B's arm-time evidence;
     * B empty again before it is decided spoils the transaction, so a
     * later answer never reuses that evidence. */
    chdock(0);
    XhciChainInit(&ch, FALSE);
    (VOID)XhciChainArm(&ch, &io, &k, FALSE, 0, 1, &why);
    kid_on(7, 0);
    model.FailBdf = XHCI_TBT_BDF(5, 1, 0);
    model.FailOffset = 0x40 + XHCI_TBT_PCIE_SLOTSTA;
    CHECK(XhciChainTry(&ch, &io, TRUE, FALSE, 10, &i) == XHCI_CHAIN_TRY_WAIT &&
          ch.Txn.Answered == 1, "answered: the port unread, waits");
    model.FailBdf = XHCI_TBT_NO_BDF;
    model.FailOffset = XHCI_TBT_NO_BDF;
    mgone(XHCI_TBT_BDF(7, 0, 0));
    CHECK(XhciChainTry(&ch, &io, TRUE, FALSE, 20, &i) == XHCI_CHAIN_TRY_SPOILED &&
          !ch.Txn.Open, "answered: empty again, spoiled");
    /* A port reading that fails part-way keeps what it read whole: the
     * chain port's Link Active read inactive, then its Slot Status read
     * failing, spoils as the whole reading (DOWN) would; the port up
     * later, the occupancy unchanged, learns nothing. */
    chdock(0);
    put32(mfind(XHCI_TBT_BDF(5, 1, 0)), 0x40 + XHCI_TBT_PCIE_LINKCAP,
          XHCI_TBT_LINKCAP_DLLLARC);
    XhciChainInit(&ch, FALSE);
    CHECK(XhciChainArm(&ch, &io, &k, FALSE, 0, 1, &why) == XHCI_CHAIN_ARM_OPEN &&
          ch.Txn.Base.Down == 1, "partial port: armed, the port read down");
    mdev(7, 0, 0, 0x1B36, 0x0010, 0x010802UL);
    model.FailBdf = XHCI_TBT_BDF(5, 1, 0);
    model.FailOffset = 0x40 + XHCI_TBT_PCIE_SLOTSTA;
    CHECK(XhciChainTry(&ch, &io, TRUE, FALSE, 10, &i) == XHCI_CHAIN_TRY_SHAPE &&
          !ch.Txn.Open && XhciChainOfKid(&ch, 1, 5) == XHCI_CHAIN_BINDS,
          "partial port: link inactive, then Slot Status failing: spoiled");
    model.FailBdf = XHCI_TBT_NO_BDF;
    model.FailOffset = XHCI_TBT_NO_BDF;
    link_follow(7);
    CHECK(XhciChainTry(&ch, &io, TRUE, FALSE, 20, &i) != XHCI_CHAIN_TRY_LEARNED &&
          XhciChainOfKid(&ch, 1, 5) == XHCI_CHAIN_BINDS,
          "partial port: the port up later, nothing learned");
    /* A failed read decides nothing: a device seen gained before it
     * failed spoils the transaction at once, never left to a later read
     * (a link coming up after it cannot admit it). */
    chdock(0);
    XhciChainInit(&ch, FALSE);
    (VOID)XhciChainArm(&ch, &io, &k, FALSE, 0, 1, &why);
    mdev(7, 0, 0, 0x1B36, 0x0010, 0x010802UL);
    model.FailBdf = XHCI_TBT_BDF(7, 1, 0);
    model.FailOffset = 0;
    CHECK(XhciChainTry(&ch, &io, TRUE, FALSE, 10, &i) == XHCI_CHAIN_TRY_SPOILED &&
          !ch.Txn.Open, "partial: a gain seen before the read failed, spoiled");
    model.FailBdf = XHCI_TBT_NO_BDF;
    model.FailOffset = XHCI_TBT_NO_BDF;
    link_follow(7);
    CHECK(XhciChainTry(&ch, &io, TRUE, FALSE, 20, &i) == XHCI_CHAIN_TRY_NONE &&
          XhciChainOfKid(&ch, 1, 5) == XHCI_CHAIN_BINDS,
          "partial: the link up after it, nothing learned");
    /* An answered candidate whose scan completes empty at a read that
     * then fails on another candidate spoils it all the same. */
    chdock2();
    XhciChainInit(&ch, FALSE);
    chkid(&k, 1, 5);
    (VOID)XhciChainArm(&ch, &io, &k, FALSE, 0, 1, &why);
    kid_on(7, 0);
    model.FailBdf = XHCI_TBT_BDF(5, 1, 0);
    model.FailOffset = 0x40 + XHCI_TBT_PCIE_SLOTSTA;
    CHECK(XhciChainTry(&ch, &io, TRUE, FALSE, 10, &i) == XHCI_CHAIN_TRY_WAIT &&
          ch.Txn.Answered == 1, "answered: A gained, its port unread");
    mgone(XHCI_TBT_BDF(7, 0, 0));
    model.FailBdf = XHCI_TBT_BDF(8, 0, 0);
    model.FailOffset = 0;
    CHECK(XhciChainTry(&ch, &io, TRUE, FALSE, 20, &i) == XHCI_CHAIN_TRY_SPOILED &&
          !ch.Txn.Open, "answered: A empty while B fails, spoiled");
    model.FailBdf = XHCI_TBT_NO_BDF;
    model.FailOffset = XHCI_TBT_NO_BDF;
    /* A partial read that finds the candidate set changed spoils it: A,
     * B and C armed, B gained and waits; A no candidate any more, B's scan
     * complete and empty, C's failing - B read at A's place. */
    chdock2();
    mfind(XHCI_TBT_BDF(2, 1, 0))->Cfg[0x1A] = 9;
    mfind(XHCI_TBT_BDF(4, 0, 0))->Cfg[0x1A] = 9;
    chbr(5, 3, 0, 0x8233, CH_DOWN, 9, 9, CH_HPC);
    XhciChainInit(&ch, FALSE);
    chkid(&k, 1, 5);
    CHECK(XhciChainArm(&ch, &io, &k, FALSE, 0, 1, &why) == XHCI_CHAIN_ARM_OPEN &&
          ch.Txn.Base.Count == 3 && ch.Txn.Base.Down == 7,
          "moved: three ports armed, all down");
    kid_on(8, 0);
    model.FailBdf = XHCI_TBT_BDF(5, 2, 0);
    model.FailOffset = 0x40 + XHCI_TBT_PCIE_SLOTSTA;
    CHECK(XhciChainTry(&ch, &io, TRUE, FALSE, 10, &i) == XHCI_CHAIN_TRY_WAIT &&
          ch.Txn.Answered == 2, "moved: B gained, its port unread");
    put32(mfind(XHCI_TBT_BDF(5, 1, 0)), 0x40 + XHCI_TBT_PCIE_SLOTCAP, 0);
    mgone(XHCI_TBT_BDF(8, 0, 0));
    model.FailBdf = XHCI_TBT_BDF(9, 0, 0);
    model.FailOffset = 0;
    CHECK(XhciChainTry(&ch, &io, TRUE, FALSE, 20, &i) == XHCI_CHAIN_TRY_SPOILED &&
          !ch.Txn.Open, "moved: the set changed under a partial read, spoiled");
    model.FailBdf = XHCI_TBT_NO_BDF;
    model.FailOffset = XHCI_TBT_NO_BDF;
    /* A partial read is held to the whole reading's rules. A device of
     * the baseline lost on a candidate scanned whole, while B's read
     * fails: spoiled, so A restored and B then gaining commits nothing. */
    chdock2();
    mdev(7, 0, 0, 0x1B36, 0x0010, 0x010802UL);
    XhciChainInit(&ch, FALSE);
    chkid(&k, 1, 5);
    CHECK(XhciChainArm(&ch, &io, &k, FALSE, 0, 1, &why) == XHCI_CHAIN_ARM_OPEN,
          "partial: armed, A occupied, B empty");
    put16(mfind(XHCI_TBT_BDF(7, 0, 0)), 0x00, 0xFFFF);
    model.FailBdf = XHCI_TBT_BDF(8, 0, 0);
    model.FailOffset = 0;
    CHECK(XhciChainTry(&ch, &io, TRUE, FALSE, 10, &i) == XHCI_CHAIN_TRY_SPOILED &&
          !ch.Txn.Open, "partial: A's device lost, B unread: spoiled");
    model.FailBdf = XHCI_TBT_NO_BDF;
    model.FailOffset = XHCI_TBT_NO_BDF;
    put16(mfind(XHCI_TBT_BDF(7, 0, 0)), 0x00, 0x1B36);
    kid_usp(8);
    CHECK(XhciChainTry(&ch, &io, TRUE, FALSE, 20, &i) == XHCI_CHAIN_TRY_NONE &&
          XhciChainOfKid(&ch, 1, 5) == XHCI_CHAIN_BINDS,
          "partial: A restored, B gaining: nothing commits");
    /* A device lost among those read of a candidate whose scan failed. */
    chdock(0);
    mdev(7, 0, 0, 0x1B36, 0x0010, 0x010802UL);
    XhciChainInit(&ch, FALSE);
    (VOID)XhciChainArm(&ch, &io, &k, FALSE, 0, 1, &why);
    put16(mfind(XHCI_TBT_BDF(7, 0, 0)), 0x00, 0xFFFF);
    model.FailBdf = XHCI_TBT_BDF(7, 5, 0);
    model.FailOffset = 0;
    CHECK(XhciChainTry(&ch, &io, TRUE, FALSE, 10, &i) == XHCI_CHAIN_TRY_SPOILED,
          "partial: a device lost before the scan failed: spoiled");
    model.FailBdf = XHCI_TBT_NO_BDF;
    model.FailOffset = XHCI_TBT_NO_BDF;
    /* Two candidates gaining, then a failure: spoiled; one gaining beside
     * an unowned path: spoiled (SEVERAL). */
    chdock2();
    XhciChainInit(&ch, FALSE);
    (VOID)XhciChainArm(&ch, &io, &k, FALSE, 0, 1, &why);
    kid_on(7, 0);
    kid_on(8, 0);
    model.FailBdf = XHCI_TBT_BDF(8, 1, 0);
    model.FailOffset = 0;
    CHECK(XhciChainTry(&ch, &io, TRUE, FALSE, 10, &i) == XHCI_CHAIN_TRY_SPOILED,
          "partial: two gains: spoiled");
    model.FailBdf = XHCI_TBT_NO_BDF;
    model.FailOffset = XHCI_TBT_NO_BDF;
    chdock2();
    XhciChainInit(&ch, FALSE);
    (VOID)XhciChainArm(&ch, &io, &k, FALSE, 0, 1, &why);
    kid_on(7, 0);
    model.FailBdf = XHCI_TBT_BDF(8, 0, 0);
    model.FailOffset = 0;
    CHECK(XhciChainTry(&ch, &io, TRUE, TRUE, 10, &i) == XHCI_CHAIN_TRY_SPOILED,
          "partial: a gain beside an unowned path: spoiled");
    model.FailBdf = XHCI_TBT_NO_BDF;
    model.FailOffset = XHCI_TBT_NO_BDF;
    /* A candidate added to the set, then a failure reading it: spoiled,
     * so the original set restored learns nothing. */
    chdock(0);
    XhciChainInit(&ch, FALSE);
    (VOID)XhciChainArm(&ch, &io, &k, FALSE, 0, 1, &why);
    mfind(XHCI_TBT_BDF(2, 1, 0))->Cfg[0x1A] = 8;
    mfind(XHCI_TBT_BDF(4, 0, 0))->Cfg[0x1A] = 8;
    f = chbr(5, 2, 0, 0x8233, CH_DOWN, 8, 8, CH_HPC);
    model.FailBdf = XHCI_TBT_BDF(8, 0, 0);
    model.FailOffset = 0;
    CHECK(XhciChainTry(&ch, &io, TRUE, FALSE, 10, &i) == XHCI_CHAIN_TRY_SPOILED &&
          !ch.Txn.Open, "partial: a candidate added, its read failing: spoiled");
    model.FailBdf = XHCI_TBT_NO_BDF;
    model.FailOffset = XHCI_TBT_NO_BDF;
    /* An answered candidate still answering at a read that fails on
     * another: spoiled, so its port reading down then up decides
     * nothing. */
    chdock2();
    XhciChainInit(&ch, FALSE);
    chkid(&k, 1, 5);
    (VOID)XhciChainArm(&ch, &io, &k, FALSE, 0, 1, &why);
    kid_on(7, 0);
    model.FailBdf = XHCI_TBT_BDF(5, 1, 0);
    model.FailOffset = 0x40 + XHCI_TBT_PCIE_SLOTSTA;
    CHECK(XhciChainTry(&ch, &io, TRUE, FALSE, 10, &i) == XHCI_CHAIN_TRY_WAIT &&
          ch.Txn.Answered == 1, "answered: A gained, its port unread");
    put16(mfind(XHCI_TBT_BDF(5, 1, 0)), 0x40 + 0x1A, 0);
    model.FailBdf = XHCI_TBT_BDF(8, 0, 0);
    model.FailOffset = 0;
    CHECK(XhciChainTry(&ch, &io, TRUE, FALSE, 20, &i) == XHCI_CHAIN_TRY_SPOILED &&
          !ch.Txn.Open, "answered: still answering, B unread: spoiled");
    model.FailBdf = XHCI_TBT_NO_BDF;
    model.FailOffset = XHCI_TBT_NO_BDF;
    link_follow(7);
    CHECK(XhciChainTry(&ch, &io, TRUE, FALSE, 30, &i) == XHCI_CHAIN_TRY_NONE &&
          XhciChainOfKid(&ch, 1, 5) == XHCI_CHAIN_BINDS,
          "answered: its port up later, nothing learned");
    /* A candidate found by a walk that then fails: a changed set,
     * spoiled. */
    chdock(0);
    XhciChainInit(&ch, FALSE);
    chkid(&k, 1, 5);
    (VOID)XhciChainArm(&ch, &io, &k, FALSE, 0, 1, &why);
    mfind(XHCI_TBT_BDF(2, 1, 0))->Cfg[0x1A] = 8;
    mfind(XHCI_TBT_BDF(4, 0, 0))->Cfg[0x1A] = 8;
    f = chbr(5, 2, 0, 0x8233, CH_DOWN, 8, 8, CH_HPC);
    model.FailBdf = XHCI_TBT_BDF(6, 0, 0);
    model.FailOffset = 0;
    CHECK(XhciChainTry(&ch, &io, TRUE, FALSE, 10, &i) == XHCI_CHAIN_TRY_SPOILED &&
          !ch.Txn.Open, "walk: a candidate added, the walk failing: spoiled");
    model.FailBdf = XHCI_TBT_NO_BDF;
    model.FailOffset = XHCI_TBT_NO_BDF;
    /* A candidate's subordinate bus changed, the walk failing elsewhere:
     * spoiled. */
    chdock(0);
    XhciChainInit(&ch, FALSE);
    (VOID)XhciChainArm(&ch, &io, &k, FALSE, 0, 1, &why);
    mfind(XHCI_TBT_BDF(2, 1, 0))->Cfg[0x1A] = 8;
    mfind(XHCI_TBT_BDF(4, 0, 0))->Cfg[0x1A] = 8;
    mfind(XHCI_TBT_BDF(5, 1, 0))->Cfg[0x1A] = 8;
    model.FailBdf = XHCI_TBT_BDF(6, 0, 0);
    model.FailOffset = 0;
    CHECK(XhciChainTry(&ch, &io, TRUE, FALSE, 10, &i) == XHCI_CHAIN_TRY_SPOILED &&
          !ch.Txn.Open, "walk: a subordinate bus changed, the walk failing: spoiled");
    model.FailBdf = XHCI_TBT_NO_BDF;
    model.FailOffset = XHCI_TBT_NO_BDF;
    /* A walk that completes having seen a candidate's buses changed,
     * then a candidate's bus register unread: spoiled. */
    chdock(0);
    XhciChainInit(&ch, FALSE);
    (VOID)XhciChainArm(&ch, &io, &k, FALSE, 0, 1, &why);
    mfind(XHCI_TBT_BDF(2, 1, 0))->Cfg[0x1A] = 8;
    mfind(XHCI_TBT_BDF(4, 0, 0))->Cfg[0x1A] = 8;
    mfind(XHCI_TBT_BDF(5, 1, 0))->Cfg[0x1A] = 8;
    model.FailBdf = XHCI_TBT_BDF(5, 1, 0);
    model.FailOffset = XHCI_TBT_CFG_BUSES;
    model.FailNth = 2;
    model.FailSeen = 0;
    CHECK(XhciChainTry(&ch, &io, TRUE, FALSE, 10, &i) == XHCI_CHAIN_TRY_SPOILED &&
          !ch.Txn.Open, "walk: complete, buses changed, the reread failing: spoiled");
    model.FailBdf = XHCI_TBT_NO_BDF;
    model.FailOffset = XHCI_TBT_NO_BDF;
    model.FailNth = 0;
    /* A walk that fails is never read as an unchanged set: a candidate
     * gone, the walk failing, the candidate back and a gain: nothing is
     * learned. */
    chdock(0);
    XhciChainInit(&ch, FALSE);
    (VOID)XhciChainArm(&ch, &io, &k, FALSE, 0, 1, &why);
    put16(mfind(XHCI_TBT_BDF(5, 1, 0)), 0x00, 0xFFFF);
    model.FailBdf = XHCI_TBT_BDF(6, 0, 0);
    model.FailOffset = 0;
    CHECK(XhciChainTry(&ch, &io, TRUE, FALSE, 10, &i) == XHCI_CHAIN_TRY_SPOILED &&
          !ch.Txn.Open, "walk: a candidate gone, the walk failing: spoiled");
    model.FailBdf = XHCI_TBT_NO_BDF;
    model.FailOffset = XHCI_TBT_NO_BDF;
    put16(mfind(XHCI_TBT_BDF(5, 1, 0)), 0x00, 0x104C);
    kid_usp(7);
    CHECK(XhciChainTry(&ch, &io, TRUE, FALSE, 20, &i) == XHCI_CHAIN_TRY_NONE &&
          XhciChainOfKid(&ch, 1, 5) == XHCI_CHAIN_BINDS,
          "walk: restored, a gain: nothing learned");
    model.FailBdf = XHCI_TBT_NO_BDF;
    model.FailOffset = XHCI_TBT_NO_BDF;
    /* Withdrawn all: nothing open, nothing bound, nothing unsettled. */
    (VOID)XhciChainArm(&ch, &io, &k, FALSE, 0, 1, &why);
    XhciChainWithdrawAll(&ch);
    CHECK(!ch.Txn.Open && XhciChainMayConnect(&ch, 1), "withdrawn all");
    /* The epochs never repeat in the domain's life. */
    epoch = ch.NextEpoch;
    XhciChainInit(&ch, TRUE);
    CHECK(ch.NextEpoch == epoch, "the epochs kept across a new start");
}

/* ---- record 22 sections 7.6.2 and 7.6.3 (task 48.43) ------------------- */

static XHCI_TBT_INFER_IN iin;
static XHCI_TBT_INFER_OUT iout;

/*
 * The ASM4242 host with its two tunnel ports 14:00.0 (bus 19, empty) and
 * 14:01.0 (bus 49, the enclosure when `drive`), both reporting Link
 * Active; port 1's tunnel found up at start, pending, marked at start;
 * the baseline taken; the router reading port 1 linked and port 2 down.
 */
static VOID inf_setup(ULONG drive)
{
    ULONG i;

    asmHost(drive);
    family_asm(1);
    ioreset();
    find(B(0x7A, 0, 0), XHCI_TBT_NO_BDF);
    XhciTbtBindsInit(&binds);
    up(1);
    XhciTbtLearnAtStart(&binds, 1);
    arm();
    for (i = 0; i < sizeof(iin) / sizeof(ULONG); i++) {
        ((PULONG)&iin)[i] = 0;
    }
    iin.Usb4Host = 1;
    iin.Ports = 2;
    iin.UpMask = 1;
    iin.RouterLink[0] = XHCI_TBT_RLINK_UP;
    iin.RouterLink[1] = XHCI_TBT_RLINK_DOWN;
    iin.Active = NULL;
}

static ULONG infer(VOID)
{
    return XhciTbtInfer(&io, &topo, &binds, &iin, &iout);
}

static VOID link_at(ULONG bdf, ULONG on)
{
    MODEL_FN *f = mfind(bdf);

    put16(f, 0x80 + 0x12, on ? XHCI_TBT_LINKSTA_DLLLA : 0);
}

/* A tunnel's far end behind a member: an Upstream Switch Port at device 0
 * of `bus`, as the enclosures present it. */
static VOID usp_on(ULONG bus)
{
    MODEL_FN *f = mfind(B(bus, 0, 0));

    if (f == NULL) {
        f = mbridge(bus, 0, 0, 0x1B21, 0x2463, bus + 1, bus + 1);
        mpcie(f, 0x80, XHCI_TBT_PCIE_TYPE_UP, 0, 0, 0, 0, 0);
    }
    put16(f, 0x00, 0x1B21);
}

/* The ASM4242's own xHCI's downstream port 14:02.0 made hot-plug capable
 * and reporting Link Active (set: its xHCI answers), as the emulator's
 * model had it: a third member of source SWITCH. */
static VOID asm_xhci_member(VOID)
{
    MODEL_FN *f = mfind(B(0x14, 2, 0));

    put16(f, 0x80 + 2, 0x2UL | (XHCI_TBT_PCIE_TYPE_DOWN << 4) |
                       XHCI_TBT_PCIE_SLOT_IMPL);
    put32(f, 0x80 + XHCI_TBT_PCIE_LINKCAP, XHCI_TBT_LINKCAP_DLLLARC);
    put16(f, 0x80 + 0x12, XHCI_TBT_LINKSTA_DLLLA);
    put32(f, 0x80 + XHCI_TBT_PCIE_SLOTCAP,
          XHCI_TBT_SLOTCAP_HPC | XHCI_TBT_SLOTCAP_HPS);
}

/* inf_setup over the ASM4242 with its xHCI's port a member. */
static VOID inf_setup_xhci(ULONG drive)
{
    inf_setup(drive);
    asm_xhci_member();
    find(B(0x7A, 0, 0), XHCI_TBT_NO_BDF);
    XhciTbtBindsInit(&binds);
    up(1);
    XhciTbtLearnAtStart(&binds, 1);
    arm();
}

static void test_infer(void)
{
    static const ULONG oneOff[2] = { 1, 0 };
    ULONG k;
    ULONG save;

    /* Made: one tunnel, one occupied member, both links agreeing. */
    inf_setup(1);
    CHECK_EQ(topo.Tunnels, 2, "infer: the ASM4242 set of two");
    CHECK_EQ(binds.AtStart, 1, "infer: port 1 marked at start");
    CHECK_EQ(infer(), XHCI_TBT_INFER_MADE, "infer: made");
    CHECK(iout.Port == 1 && iout.Member == 1 && iout.Bdf == B(0x14, 1, 0) &&
          iout.Buses == get32(mfind(B(0x14, 1, 0)), 0x18),
          "infer: port 1 is 14:01.0, its bus numbers kept");
    /* Kept apart: the shared table, the member lookup and the pending
     * mark are what they were. */
    CHECK_EQ(binds.Count, 0, "infer: nothing entered in the shared table");
    CHECK_EQ(member(1), XHCI_TBT_NO_MEMBER, "infer: port 1 still unbound");
    CHECK_EQ(binds.Pending, 1, "infer: port 1 still pending");
    CHECK_EQ(learn(1, NULL), XHCI_TBT_MAP_NOT_GAINED,
             "infer: the learning of port 1 goes on");
    CHECK_EQ(infer(), XHCI_TBT_INFER_MADE, "infer: made again (the eject)");

    /* I2 to I5. */
    inf_setup(1);
    iin.Usb4Host = 0;
    CHECK_EQ(infer(), XHCI_TBT_INFER_HOST, "infer: a Thunderbolt 3 host");
    inf_setup(1);
    CHECK_EQ(XhciTbtInfer(&io, &topo, &binds, &iin, NULL),
             XHCI_TBT_INFER_BAD_PARAM, "infer: no out");
    iin.Ports = 0;
    CHECK_EQ(infer(), XHCI_TBT_INFER_BAD_PARAM, "infer: no ports");
    for (k = 0; k < 4; k++) {
        inf_setup(1);
        save = k == 0 ? XHCI_TBT_SRC_OVERRIDE
             : k == 1 ? XHCI_TBT_SRC_ROOTPORT
             : k == 2 ? XHCI_TBT_SRC_ROUTER : XHCI_TBT_SRC_SHPC;
        topo.Source[0] = save;
        CHECK_EQ(infer(), XHCI_TBT_INFER_SET,
                 "infer: an override, listed root port, router-named or "
                 "SHPC member refused");
    }
    inf_setup(1);
    topo.Outcome = XHCI_TBT_TOPO_OVERRIDE;
    CHECK_EQ(infer(), XHCI_TBT_INFER_SET, "infer: the override's set");
    inf_setup(1);
    topo.FindOutcome = XHCI_TBT_TOPO_UNLISTED;
    CHECK_EQ(infer(), XHCI_TBT_INFER_SET,
             "infer: a set the discovery did not make");
    inf_setup(1);
    iin.Active = oneOff;
    CHECK_EQ(infer(), XHCI_TBT_INFER_INACTIVE, "infer: an inactive member");
    inf_setup(1);
    CHECK_EQ(named(2, B(0x14, 0, 0)), XHCI_TBT_MAP_KNOWN,
             "infer: port 2 named 14:00.0");
    CHECK_EQ(infer(), XHCI_TBT_INFER_BOUND,
             "infer: never beside a named binding");
    inf_setup(1);
    binds.PortConflict = 2;
    CHECK_EQ(infer(), XHCI_TBT_INFER_BOUND, "infer: a conflicted port");
    inf_setup(1);
    iin.Conflict = 1;
    CHECK_EQ(infer(), XHCI_TBT_INFER_CONFLICTED,
             "infer: the port conflicted for the inference");
    inf_setup(1);
    iin.UpMask = 0;
    CHECK_EQ(infer(), XHCI_TBT_INFER_NOT_ONE, "infer: no tunnel up");
    iin.UpMask = 3;
    CHECK_EQ(infer(), XHCI_TBT_INFER_NOT_ONE, "infer: two tunnels up");
    inf_setup(1);
    XhciTbtLearnNotAtStart(&binds, 1);
    CHECK_EQ(infer(), XHCI_TBT_INFER_NOT_AT_START,
             "infer: a tunnel brought up since the start");
    inf_setup(1);
    up(2);
    CHECK_EQ(infer(), XHCI_TBT_INFER_PENDING, "infer: another port pending");

    /* I6: the router's links. */
    for (k = 0; k < 6; k++) {
        inf_setup(1);
        if (k < 3) {
            iin.RouterLink[0] = k == 0 ? XHCI_TBT_RLINK_DOWN
                              : k == 1 ? XHCI_TBT_RLINK_UNKNOWN
                                       : XHCI_TBT_RLINK_NONE;
        } else {
            iin.RouterLink[1] = k == 3 ? XHCI_TBT_RLINK_UP
                              : k == 4 ? XHCI_TBT_RLINK_UNKNOWN
                                       : XHCI_TBT_RLINK_NONE;
        }
        CHECK_EQ(infer(), XHCI_TBT_INFER_ROUTER,
                 "infer: the router reads the port not linked, or another "
                 "port linked or unknown");
    }

    /* I7: the PCI reading. */
    inf_setup(1);
    binds.Armed = 0;
    CHECK_EQ(infer(), XHCI_TBT_INFER_NOT_ARMED, "infer: no baseline");
    inf_setup(1);
    model.FailBdf = B(0x14, 0, 0);
    CHECK_EQ(infer(), XHCI_TBT_INFER_UNREAD, "infer: a member unreadable");
    inf_setup(1);
    model.FailBdf = B(0x49, 0, 0);
    CHECK_EQ(infer(), XHCI_TBT_INFER_UNREAD,
             "infer: the occupied bus unreadable");
    inf_setup(1);
    put32(mfind(B(0x14, 0, 0)), 0x80 + XHCI_TBT_PCIE_LINKCAP, 0);
    CHECK_EQ(infer(), XHCI_TBT_INFER_NO_REPORT,
             "infer: a member without Link Active Reporting");
    inf_setup(1);
    mfind(B(0x14, 0, 0))->Cfg[0x19] = 0x1A;
    CHECK_EQ(infer(), XHCI_TBT_INFER_MOVED,
             "infer: a secondary bus not the baseline's");
    inf_setup(1);
    dev_on(0x19);
    link_at(B(0x14, 0, 0), 1);
    CHECK_EQ(infer(), XHCI_TBT_INFER_OCCUPANCY, "infer: two occupied members");
    inf_setup(0);
    link_at(B(0x14, 1, 0), 1);
    CHECK_EQ(infer(), XHCI_TBT_INFER_OCCUPANCY, "infer: none occupied");
    /* A wrong inference refused: the links disagree with the occupancy. */
    inf_setup(1);
    link_at(B(0x14, 1, 0), 0);
    CHECK_EQ(infer(), XHCI_TBT_INFER_LINKS,
             "infer: the occupied member's link down");
    inf_setup(1);
    link_at(B(0x14, 0, 0), 1);
    CHECK_EQ(infer(), XHCI_TBT_INFER_LINKS,
             "infer: an empty member's link up");
    /* What answers behind the one occupied member must be a tunnel's far
     * end: the controller's own xHCI behind a member (the emulator's
     * model) with port 1's function not answering yet, both links
     * agreeing, is never taken for port 1's; nor an endpoint directly
     * behind a tunnel port. */
    inf_setup_xhci(0);
    CHECK_EQ(topo.Tunnels, 3, "infer: the xHCI's port a third member");
    CHECK_EQ(infer(), XHCI_TBT_INFER_SHAPE,
             "infer: the xHCI's occupied member refused");
    inf_setup(0);
    dev_on(0x49);
    link_at(B(0x14, 1, 0), 1);
    CHECK_EQ(infer(), XHCI_TBT_INFER_SHAPE,
             "infer: an endpoint behind the member refused");
    inf_setup(1);
    dev_at(0x49, 1, 1);
    CHECK_EQ(infer(), XHCI_TBT_INFER_SHAPE,
             "infer: a second device behind the member refused");
    inf_setup(1);
    model.FailBdf = B(0x49, 0, 0);
    model.FailOffset = XHCI_TBT_CFG_HEADER;
    CHECK_EQ(infer(), XHCI_TBT_INFER_UNREAD,
             "infer: the far end's shape unreadable: unread, not a shape");

    /* The re-check at the eject: a change since drops it. */
    inf_setup(1);
    CHECK_EQ(infer(), XHCI_TBT_INFER_MADE, "infer: made before the eject");
    up(2);
    CHECK_EQ(infer(), XHCI_TBT_INFER_PENDING,
             "infer: the eject's re-check sees a second port pending");

    /* The read-back. */
    inf_setup(1);
    CHECK_EQ(infer(), XHCI_TBT_INFER_MADE, "readback: made");
    save = iout.Buses;
    CHECK_EQ(XhciTbtInferReadBack(&io, iout.Bdf, save), XHCI_TBT_RB_OPEN,
             "readback: the function still answers: open");
    put16(mfind(B(0x49, 0, 0)), 0x00, 0xFFFF);
    CHECK_EQ(XhciTbtInferReadBack(&io, iout.Bdf, save), XHCI_TBT_RB_OPEN,
             "readback: nothing answers but Link Active set: open");
    link_at(B(0x14, 1, 0), 0);
    CHECK_EQ(XhciTbtInferReadBack(&io, iout.Bdf, save), XHCI_TBT_RB_CLOSED,
             "readback: link down, nothing answering: closed");
    CHECK_EQ(XhciTbtInferReadBack(&io, iout.Bdf, save ^ 0x100UL),
             XHCI_TBT_RB_UNREAD, "readback: bus numbers moved: unread");
    model.FailBdf = B(0x14, 1, 0);
    CHECK_EQ(XhciTbtInferReadBack(&io, iout.Bdf, save), XHCI_TBT_RB_UNREAD,
             "readback: the member unreadable: unread");
}

static void test_narrowed(void)
{
    /* Port 1's enclosure on 14:01.0 at start; port 2 plugged on 14:00.0
     * while port 1 is pending: learned (formerly SEVERAL). */
    inf_setup(1);
    CHECK(binds.StartTaken && binds.StartOk && binds.StartCount == 2 &&
          binds.StartOcc == 2, "narrowed: the start reading trusted");
    up(2);
    usp_on(0x19);
    CHECK_EQ(learn(2, NULL), XHCI_TBT_MAP_LEARNED,
             "narrowed: port 2 learned while port 1 is pending");
    CHECK_EQ(member(2), 0, "narrowed: port 2 is 14:00.0");
    CHECK_EQ(binds.Pending, 1, "narrowed: port 1 still pending");
    CHECK_EQ(binds.AtStart, 1, "narrowed: port 1 still at start");

    /* A gain on the member occupied at start: never. */
    inf_setup(1);
    up(2);
    dev_at(0x49, 1, 1);
    CHECK_EQ(learn(2, NULL), XHCI_TBT_MAP_SEVERAL,
             "narrowed: a gain on the at-start member refused");

    /* The start reading not trusted: no function behind any member. */
    inf_setup(0);
    CHECK(binds.StartTaken && !binds.StartOk,
          "narrowed: fewer occupied members than at-start ports");
    up(2);
    usp_on(0x19);
    CHECK_EQ(learn(2, NULL), XHCI_TBT_MAP_SEVERAL,
             "narrowed: an untrusted start reading refuses");

    /* A bound member's occupancy does not make up the count: port 2 named
     * 14:00.0 with its device, port 1's at-start function late; port 3's
     * hot plug then sees 14:01.0 gain, which may be port 1's. */
    asmHost(0);
    family_asm(1);
    ioreset();
    find(B(0x7A, 0, 0), XHCI_TBT_NO_BDF);
    XhciTbtBindsInit(&binds);
    CHECK_EQ(named(2, B(0x14, 0, 0)), XHCI_TBT_MAP_KNOWN,
             "narrowed: port 2 named 14:00.0");
    usp_on(0x19);
    up(1);
    XhciTbtLearnAtStart(&binds, 1);
    arm();
    CHECK(binds.StartTaken && !binds.StartOk,
          "narrowed: a bound member's occupancy not counted");
    up(3);
    usp_on(0x49);
    CHECK_EQ(learn(3, NULL), XHCI_TBT_MAP_SEVERAL,
             "narrowed: port 1's late function not taken for port 3");

    /* A port not at start pending at the start reading. */
    asmHost(1);
    family_asm(1);
    ioreset();
    find(B(0x7A, 0, 0), XHCI_TBT_NO_BDF);
    XhciTbtBindsInit(&binds);
    up(1);
    up(2);
    XhciTbtLearnAtStart(&binds, 1);
    arm();
    CHECK(binds.StartTaken && !binds.StartOk,
          "narrowed: another port pending at the start reading");
    usp_on(0x19);
    CHECK_EQ(learn(2, NULL), XHCI_TBT_MAP_SEVERAL,
             "narrowed: so port 2 is not learned");

    /* Sources where a device can sit without a tunnel. */
    asmHost(1);
    family_asm(1);
    ioreset();
    find(B(0x7A, 0, 0), XHCI_TBT_NO_BDF);
    topo.Source[0] = XHCI_TBT_SRC_ROOTPORT;
    XhciTbtBindsInit(&binds);
    up(1);
    XhciTbtLearnAtStart(&binds, 1);
    arm();
    CHECK(binds.StartTaken && !binds.StartOk,
          "narrowed: a listed root port member not trusted");
    topo.Source[0] = XHCI_TBT_SRC_SWITCH;
    up(2);
    usp_on(0x19);
    CHECK_EQ(learn(2, NULL), XHCI_TBT_MAP_SEVERAL,
             "narrowed: so port 2 is not learned");

    /* A member not in the start reading. */
    inf_setup(1);
    binds.StartBdf[0] = B(0x14, 5, 0);
    up(2);
    usp_on(0x19);
    CHECK_EQ(learn(2, NULL), XHCI_TBT_MAP_SEVERAL,
             "narrowed: a member not in the start reading refused");

    /* The at-start mark ended by a TUNNEL_UP: the old rule. */
    inf_setup(1);
    XhciTbtLearnNotAtStart(&binds, 1);
    CHECK(!binds.AtStart && !binds.StartTaken,
          "narrowed: the start reading forgotten with the last mark");
    up(2);
    usp_on(0x19);
    CHECK_EQ(learn(2, NULL), XHCI_TBT_MAP_SEVERAL,
             "narrowed: port 1 brought up again blocks as before");

    /* Ended by its proof down: port 2 learned by the old rule. */
    inf_setup(1);
    XhciTbtLearnDown(&binds, 1);
    CHECK(!binds.AtStart && !binds.Pending,
          "narrowed: the mark ends with the pending mark");
    up(2);
    usp_on(0x19);
    CHECK_EQ(learn(2, NULL), XHCI_TBT_MAP_LEARNED,
             "narrowed: port 2 alone pending, learned");

    /* Marks only a pending, unbound port. */
    XhciTbtBindsInit(&binds);
    XhciTbtLearnAtStart(&binds, 1);
    CHECK_EQ(binds.AtStart, 0, "narrowed: no mark for a port not pending");
    inf_setup(1);
    CHECK_EQ(named(1, B(0x14, 1, 0)), XHCI_TBT_MAP_KNOWN,
             "narrowed: port 1 named");
    CHECK_EQ(learn(1, NULL), XHCI_TBT_MAP_KNOWN, "narrowed: port 1 known");
    CHECK(!binds.AtStart && !binds.Pending,
          "narrowed: a KNOWN outcome ends the mark");

    /* The controller's own function counted for a tunnel (the emulator's
     * model, its xHCI's port a member): port 1 preserved but its function
     * not answering yet, the xHCI's occupied member alone making up the
     * count. Not trusted; port 1's function answering during port 2's
     * attempt is never taken for port 2's. */
    inf_setup_xhci(0);
    CHECK_EQ(topo.Tunnels, 3, "narrowed: the xHCI's port a third member");
    CHECK(binds.StartTaken && !binds.StartOk && binds.StartOcc == 4,
          "narrowed: the xHCI's occupied member makes the start reading "
          "untrusted");
    up(2);
    usp_on(0x49);
    CHECK_EQ(learn(2, NULL), XHCI_TBT_MAP_SEVERAL,
             "narrowed: port 1's late function not taken for port 2");
    CHECK(binds.Count == 0 && member(2) == XHCI_TBT_NO_MEMBER &&
          member(1) == XHCI_TBT_NO_MEMBER,
          "narrowed: no binding entered in the shared table");
    CHECK_EQ(binds.Pending, 3, "narrowed: both ports still pending");

    /* The same with the xHCI not answering at the start reading: trusted
     * (port 1's far end on 14:01.0), but the xHCI answering during port
     * 2's attempt is no tunnel's far end; port 2's own then is. */
    inf_setup_xhci(1);
    put16(mfind(B(0x79, 0, 0)), 0x00, 0xFFFF);
    XhciTbtBindsInit(&binds);
    up(1);
    XhciTbtLearnAtStart(&binds, 1);
    arm();
    CHECK(binds.StartTaken && binds.StartOk && binds.StartOcc == 2,
          "narrowed: trusted with the xHCI silent");
    up(2);
    put16(mfind(B(0x79, 0, 0)), 0x00, 0x1B21);
    CHECK_EQ(learn(2, NULL), XHCI_TBT_MAP_SEVERAL,
             "narrowed: the xHCI's function not taken for port 2");
    CHECK_EQ(binds.Count, 0, "narrowed: nothing entered for it");
    usp_on(0x19);
    CHECK_EQ(learn(2, NULL), XHCI_TBT_MAP_LEARNED,
             "narrowed: port 2's far end then learned");
    CHECK_EQ(member(2), 0, "narrowed: port 2 is 14:00.0");

    /* A gain that is no switch's upstream port: refused when no link came
     * up for it (record 22 section 7.6.4); learned when its port read down
     * at the start reading and up now, as a hot plug's does. */
    inf_setup(1);
    up(2);
    dev_quiet(0x19);
    CHECK_EQ(learn(2, NULL), XHCI_TBT_MAP_SEVERAL,
             "narrowed: an endpoint gained with no link evidence refused");
    CHECK_EQ(binds.Count, 0, "narrowed: nothing entered for an endpoint");
    inf_setup(1);
    up(2);
    dev_on(0x19);
    CHECK_EQ(learn(2, NULL), XHCI_TBT_MAP_LEARNED,
             "narrowed: an endpoint with the link's evidence learned");
    CHECK_EQ(member(2), 0, "narrowed: port 2 is 14:00.0");

    /* A shape that cannot be read is a reading not made: the gain is not
     * consumed, and the next attempt learns it. */
    inf_setup(1);
    up(2);
    usp_on(0x19);
    model.FailBdf = B(0x19, 0, 0);
    model.FailOffset = XHCI_TBT_CFG_HEADER;
    model.FailNth = 1;
    model.FailSeen = 0;
    CHECK_EQ(learn(2, NULL), XHCI_TBT_MAP_UNREAD,
             "narrowed: the gain's shape unreadable: unread");
    CHECK_EQ(binds.BaseSeen[0], 0, "narrowed: the baseline kept");
    CHECK_EQ(learn(2, NULL), XHCI_TBT_MAP_LEARNED,
             "narrowed: learned once the shape reads");
    CHECK_EQ(member(2), 0, "narrowed: port 2 is 14:00.0 after the retry");

    /* Port 1's attempt between the two never consumes port 2's gain. */
    inf_setup(1);
    up(2);
    usp_on(0x19);
    model.FailBdf = B(0x19, 0, 0);
    model.FailOffset = XHCI_TBT_CFG_HEADER;
    model.FailNth = 1;
    model.FailSeen = 0;
    CHECK_EQ(learn(2, NULL), XHCI_TBT_MAP_UNREAD,
             "narrowed: interleaved: port 2's shape unreadable");
    CHECK_EQ(learn(1, NULL), XHCI_TBT_MAP_SEVERAL,
             "narrowed: interleaved: port 1's attempt refuses the gain");
    CHECK_EQ(binds.BaseSeen[0], 0,
             "narrowed: interleaved: and leaves it unconsumed");
    CHECK_EQ(learn(2, NULL), XHCI_TBT_MAP_LEARNED,
             "narrowed: interleaved: port 2 learned after the recovery");
    CHECK_EQ(member(2), 0, "narrowed: interleaved: port 2 is 14:00.0");
    /* Likewise with no read failure: port 1 tried first. */
    inf_setup(1);
    up(2);
    usp_on(0x19);
    CHECK_EQ(learn(1, NULL), XHCI_TBT_MAP_SEVERAL,
             "narrowed: port 1 tried first refuses port 2's gain");
    CHECK_EQ(learn(2, NULL), XHCI_TBT_MAP_LEARNED,
             "narrowed: port 2 learned after port 1's attempt");

    /* The same at the start reading: not taken, then taken at the next
     * reading. */
    asmHost(1);
    family_asm(1);
    ioreset();
    find(B(0x7A, 0, 0), XHCI_TBT_NO_BDF);
    XhciTbtBindsInit(&binds);
    up(1);
    XhciTbtLearnAtStart(&binds, 1);
    model.FailBdf = B(0x49, 0, 0);
    model.FailOffset = XHCI_TBT_CFG_HEADER;
    model.FailNth = 1;
    model.FailSeen = 0;
    arm();
    CHECK(binds.Armed && !binds.StartTaken,
          "narrowed: the start reading not taken on an unreadable shape");
    CHECK_EQ(learn(1, NULL), XHCI_TBT_MAP_NOT_GAINED,
             "narrowed: port 1's next attempt reads again");
    CHECK(binds.StartTaken && binds.StartOk && binds.StartOcc == 2,
          "narrowed: the start reading taken and trusted then");

    /* An unreadable shape of another member's function never holds up
     * the rule for a port pending alone: port 1 at start, its member
     * empty, the xHCI's member's header unreadable at every reading; port
     * 1's far end then answering is learned as port 1's. */
    inf_setup_xhci(0);
    model.FailBdf = B(0x79, 0, 0);
    model.FailOffset = XHCI_TBT_CFG_HEADER;
    model.FailNth = 0;
    XhciTbtBindsInit(&binds);
    up(1);
    XhciTbtLearnAtStart(&binds, 1);
    arm();
    CHECK(binds.Armed && !binds.StartTaken,
          "narrowed: no start reading while the xHCI's shape is unreadable");
    usp_on(0x49);
    CHECK_EQ(learn(1, NULL), XHCI_TBT_MAP_LEARNED,
             "narrowed: port 1 alone pending learned all the same");
    CHECK_EQ(member(1), 1, "narrowed: port 1 is 14:01.0");
    /* With port 2 pending too, the same unreadable shape holds the gain. */
    inf_setup_xhci(0);
    model.FailBdf = B(0x79, 0, 0);
    model.FailOffset = XHCI_TBT_CFG_HEADER;
    model.FailNth = 0;
    XhciTbtBindsInit(&binds);
    up(1);
    XhciTbtLearnAtStart(&binds, 1);
    arm();
    up(2);
    usp_on(0x19);
    CHECK_EQ(learn(2, NULL), XHCI_TBT_MAP_UNREAD,
             "narrowed: port 2's gain held while no start reading is taken");
    CHECK_EQ(binds.Count, 0, "narrowed: nothing entered while held");

    /* A member another domain claims: refused. */
    {
        static const ULONG oneOff[2] = { 1, 0 };

        inf_setup(1);
        up(2);
        usp_on(0x19);
        CHECK_EQ(learn(2, oneOff), XHCI_TBT_MAP_SEVERAL,
                 "narrowed: an inactive member refuses");
    }
}

/*
 * Record 22 section 7.6.4 (task 48.43): what a port pending alone may
 * learn. A tunnel's far end (an Upstream Switch Port at device 0 alone)
 * is learned; any other shape only on the link's evidence (its port read
 * down with nothing behind it, up now); a function of the controller's
 * own answering late is refused and nothing is bound; a shape or port
 * that cannot be read is not consumed.
 */
static void test_alone(void)
{
    MODEL_FN *f;
    ULONG over[1];

    /* The ASM4242 with its own xHCI's port a member (the emulator's model),
     * the xHCI not answering yet at the arm; port 2 plugged, pending
     * alone; the xHCI answering during port 2's attempt is refused, and
     * port 2's own far end then learned. */
    inf_setup(0);
    asm_xhci_member();
    put16(mfind(B(0x79, 0, 0)), 0x00, 0xFFFF);
    find(B(0x7A, 0, 0), XHCI_TBT_NO_BDF);
    XhciTbtBindsInit(&binds);
    CHECK_EQ(topo.Tunnels, 3, "alone: the xHCI's port a third member");
    CHECK_EQ(arm(), 0, "alone: armed");
    CHECK_EQ(binds.BaseDown, 3,
             "alone: the tunnel ports down, the xHCI's port's link up");
    up(2);
    CHECK_EQ(learn(2, NULL), XHCI_TBT_MAP_NOT_GAINED, "alone: nothing yet");
    put16(mfind(B(0x79, 0, 0)), 0x00, 0x1B21);
    CHECK_EQ(learn(2, NULL), XHCI_TBT_MAP_SHAPE,
             "alone: the xHCI answering late is refused");
    CHECK(binds.Count == 0 && member(2) == XHCI_TBT_NO_MEMBER &&
          binds.Pending == 2, "alone: nothing bound, port 2 still pending");
    CHECK_EQ(binds.BaseSeen[2], 1, "alone: the xHCI's gain consumed");
    usp_on(0x19);
    CHECK_EQ(learn(2, NULL), XHCI_TBT_MAP_LEARNED,
             "alone: port 2's far end then learned");
    CHECK_EQ(member(2), 0, "alone: port 2 is 14:00.0");

    /* The reported case: port 1 found up at start, pending alone, its
     * function not answering yet, the xHCI silent at the start reading;
     * the xHCI answering during port 1's own attempt is never port 1's. */
    inf_setup(0);
    asm_xhci_member();
    put16(mfind(B(0x79, 0, 0)), 0x00, 0xFFFF);
    link_at(B(0x14, 1, 0), 1);
    find(B(0x7A, 0, 0), XHCI_TBT_NO_BDF);
    XhciTbtBindsInit(&binds);
    up(1);
    XhciTbtLearnAtStart(&binds, 1);
    arm();
    put16(mfind(B(0x79, 0, 0)), 0x00, 0x1B21);
    CHECK_EQ(learn(1, NULL), XHCI_TBT_MAP_SHAPE,
             "alone: at start, the xHCI not taken for port 1");
    CHECK(binds.Count == 0 && binds.Pending == 1 && binds.AtStart == 1,
          "alone: nothing bound, port 1 pending at start");
    usp_on(0x49);
    CHECK_EQ(learn(1, NULL), XHCI_TBT_MAP_LEARNED,
             "alone: port 1's far end then learned");
    CHECK_EQ(member(1), 1, "alone: port 1 is 14:01.0");

    /* A real enclosure's far end needs no link evidence. */
    inf_setup(0);
    XhciTbtBindsInit(&binds);
    arm();
    up(2);
    link_at(B(0x14, 0, 0), 1);
    usp_on(0x19);
    CHECK_EQ(learn(2, NULL), XHCI_TBT_MAP_LEARNED, "alone: a far end learned");

    /* A shape that cannot be read: not consumed, learned at the next
     * attempt. */
    inf_setup(0);
    XhciTbtBindsInit(&binds);
    arm();
    up(2);
    usp_on(0x19);
    model.FailBdf = B(0x19, 0, 0);
    model.FailOffset = XHCI_TBT_CFG_HEADER;
    CHECK_EQ(learn(2, NULL), XHCI_TBT_MAP_UNREAD,
             "alone: the far end's shape unread");
    CHECK_EQ(binds.BaseSeen[0], 0, "alone: the baseline kept");
    model.FailBdf = XHCI_TBT_NO_BDF;
    model.FailOffset = XHCI_TBT_NO_BDF;
    CHECK_EQ(learn(2, NULL), XHCI_TBT_MAP_LEARNED, "alone: then learned");

    /* A direct endpoint (the emulator's plain enclosure: its NVMe behind
     * the tunnel port, no switch): learned on the link's evidence. */
    two_hp();
    XhciTbtBindsInit(&binds);
    over[0] = B(0, 5, 0);
    findin(B(0, 4, 0), over, 1);
    CHECK_EQ(arm(), 0, "direct: armed");
    CHECK_EQ(binds.BaseDown, 1, "direct: the empty port read down");
    up(1);
    dev_on(1);
    CHECK_EQ(learn(1, NULL), XHCI_TBT_MAP_LEARNED,
             "direct: down, then up with the endpoint: learned");
    CHECK_EQ(binds.BaseDown, 0, "direct: the evidence spent");

    /* The endpoint's port unreadable at the gain: not consumed. */
    two_hp();
    XhciTbtBindsInit(&binds);
    findin(B(0, 4, 0), over, 1);
    arm();
    up(1);
    dev_on(1);
    model.FailBdf = B(0, 5, 0);
    model.FailOffset = 0x40 + XHCI_TBT_PCIE_LINKSTA;
    CHECK_EQ(learn(1, NULL), XHCI_TBT_MAP_UNREAD,
             "direct: the port unread at the gain");
    CHECK(binds.BaseSeen[0] == 0 && binds.BaseDown == 1,
          "direct: the baseline and the evidence kept");
    model.FailBdf = XHCI_TBT_NO_BDF;
    model.FailOffset = XHCI_TBT_NO_BDF;
    CHECK_EQ(learn(1, NULL), XHCI_TBT_MAP_LEARNED, "direct: then learned");

    /* A port reading that fails part-way keeps what it read whole: Link
     * Active read inactive, then the Slot Status read failing, rejects
     * the gain as the whole reading (DOWN) would; the port up later, the
     * occupancy unchanged, learns nothing. */
    two_hp();
    XhciTbtBindsInit(&binds);
    findin(B(0, 4, 0), over, 1);
    arm();
    up(1);
    dev_quiet(1);
    model.FailBdf = B(0, 5, 0);
    model.FailOffset = 0x40 + XHCI_TBT_PCIE_SLOTSTA;
    CHECK_EQ(learn(1, NULL), XHCI_TBT_MAP_SHAPE,
             "direct: link inactive, then Slot Status failing: refused");
    CHECK(binds.Count == 0 && binds.Pending == 1,
          "direct: nothing bound, port 1 pending");
    model.FailBdf = XHCI_TBT_NO_BDF;
    model.FailOffset = XHCI_TBT_NO_BDF;
    port_up(mfind(B(0, 5, 0)), 1);
    CHECK(learn(1, NULL) != XHCI_TBT_MAP_LEARNED && binds.Count == 0 &&
          member(1) == XHCI_TBT_NO_MEMBER,
          "direct: the port up later, the same endpoint, not learned");

    /* An endpoint with no link change: refused, consumed, still pending. */
    two_hp();
    XhciTbtBindsInit(&binds);
    findin(B(0, 4, 0), over, 1);
    arm();
    up(1);
    dev_quiet(1);
    CHECK_EQ(learn(1, NULL), XHCI_TBT_MAP_SHAPE,
             "direct: an endpoint with its port still down refused");
    CHECK(binds.Count == 0 && binds.Pending == 1 && binds.BaseSeen[0] == 1,
          "direct: nothing bound, consumed, port 1 pending");

    /* The port's link up while nothing answered (no down reading): an
     * endpoint answering later has no evidence. */
    two_hp();
    XhciTbtBindsInit(&binds);
    findin(B(0, 4, 0), over, 1);
    port_up(mfind(B(0, 5, 0)), 1);
    arm();
    CHECK_EQ(binds.BaseDown, 0, "direct: up at the arm, no evidence");
    up(1);
    CHECK_EQ(learn(1, NULL), XHCI_TBT_MAP_NOT_GAINED, "direct: nothing yet");
    CHECK_EQ(binds.BaseDown, 0, "direct: still none");
    dev_quiet(1);
    CHECK_EQ(learn(1, NULL), XHCI_TBT_MAP_SHAPE,
             "direct: the late endpoint refused");

    /* The evidence read at an attempt, and kept by location across a
     * baseline taken again. */
    two_hp();
    XhciTbtBindsInit(&binds);
    findin(B(0, 4, 0), over, 1);
    port_up(mfind(B(0, 5, 0)), 1);
    arm();
    up(1);
    port_up(mfind(B(0, 5, 0)), 0);
    CHECK_EQ(learn(1, NULL), XHCI_TBT_MAP_NOT_GAINED, "direct: down now");
    CHECK_EQ(binds.BaseDown, 1, "direct: the evidence read at the attempt");
    port_up(mfind(B(0, 5, 0)), 1);
    CHECK_EQ(arm(), 0, "direct: armed again, the link up");
    CHECK_EQ(binds.BaseDown, 1, "direct: the evidence kept by location");
    dev_on(1);
    CHECK_EQ(learn(1, NULL), XHCI_TBT_MAP_LEARNED, "direct: learned");

    /* An arm that fails keeps the evidence: down, then the tunnel's link
     * up with the endpoint silent, a re-arm failing on a read, a retry,
     * then the endpoint learned. */
    two_hp();
    XhciTbtBindsInit(&binds);
    findin(B(0, 4, 0), over, 1);
    CHECK_EQ(arm(), 0, "direct: armed, down");
    CHECK_EQ(binds.BaseDown, 1, "direct: the evidence");
    up(1);
    port_up(mfind(B(0, 5, 0)), 1);
    model.FailBdf = B(0, 5, 0);
    model.FailOffset = XHCI_TBT_CFG_BUSES;
    CHECK_EQ(arm(), XHCI_TBT_MAP_UNREAD, "direct: a re-arm failing");
    CHECK(!binds.Armed && binds.BaseDown == 1,
          "direct: not armed, the evidence kept");
    model.FailBdf = XHCI_TBT_NO_BDF;
    model.FailOffset = XHCI_TBT_NO_BDF;
    CHECK_EQ(arm(), 0, "direct: the retry armed, the link up");
    CHECK_EQ(binds.BaseDown, 1, "direct: the evidence still kept");
    dev_on(1);
    CHECK_EQ(learn(1, NULL), XHCI_TBT_MAP_LEARNED,
             "direct: the endpoint then learned");

    /* A re-arm that reads A occupied and then fails on B spends A's
     * evidence all the same: A read empty again with its link still up
     * gives no new evidence, so A's endpoint answering again during
     * another port's attempt is refused and nothing is bound. */
    two_hp();
    XhciTbtBindsInit(&binds);
    {
        ULONG two[2];

        two[0] = B(0, 5, 0);
        two[1] = B(0, 6, 0);
        findin(B(0, 4, 0), two, 2);
    }
    CHECK_EQ(arm(), 0, "spent: armed, both down");
    CHECK_EQ(binds.BaseDown, 3, "spent: both carry the evidence");
    dev_on(1);
    model.FailBdf = B(0, 6, 0);
    model.FailOffset = XHCI_TBT_CFG_BUSES;
    CHECK_EQ(arm(), XHCI_TBT_MAP_UNREAD, "spent: A occupied, B unread");
    CHECK_EQ(binds.BaseDown, 2, "spent: A's evidence spent, B's kept");
    model.FailBdf = XHCI_TBT_NO_BDF;
    model.FailOffset = XHCI_TBT_NO_BDF;
    put16(mfind(B(1, 0, 0)), 0x00, 0xFFFF);
    port_up(mfind(B(0, 5, 0)), 1);
    CHECK_EQ(arm(), 0, "spent: the retry, A empty with its link up");
    CHECK_EQ(binds.BaseDown, 2, "spent: no evidence for A");
    up(2);
    dev_quiet(1);
    CHECK_EQ(learn(2, NULL), XHCI_TBT_MAP_SHAPE,
             "spent: A's endpoint answering again is refused");
    CHECK(binds.Count == 0 && member(2) == XHCI_TBT_NO_MEMBER,
          "spent: nothing bound");

    /* The same through an ordinary attempt: A answers (its link up), the
     * attempt fails on B and ends before the baseline moves, so A's
     * evidence is only held; A empty again at a complete reading spends
     * it, and A answering again with its link still up is refused. */
    two_hp();
    XhciTbtBindsInit(&binds);
    {
        ULONG two[2];

        two[0] = B(0, 5, 0);
        two[1] = B(0, 6, 0);
        findin(B(0, 4, 0), two, 2);
    }
    CHECK_EQ(arm(), 0, "held: armed, both down");
    up(1);
    port_up(mfind(B(0, 5, 0)), 1);
    dev_quiet(1);
    model.FailBdf = B(0, 6, 0);
    model.FailOffset = XHCI_TBT_CFG_BUSES;
    CHECK_EQ(learn(1, NULL), XHCI_TBT_MAP_UNREAD, "held: B unread");
    CHECK(binds.BaseHeld == 1 && binds.BaseDown == 3,
          "held: A's evidence held, not spent yet");
    model.FailBdf = XHCI_TBT_NO_BDF;
    model.FailOffset = XHCI_TBT_NO_BDF;
    put16(mfind(B(1, 0, 0)), 0x00, 0xFFFF);
    CHECK_EQ(learn(1, NULL), XHCI_TBT_MAP_NOT_GAINED,
             "held: A empty again, its link up");
    CHECK(binds.BaseHeld == 0 && binds.BaseDown == 2,
          "held: A's evidence spent");
    put16(mfind(B(1, 0, 0)), 0x00, 0x144D);
    CHECK_EQ(learn(1, NULL), XHCI_TBT_MAP_SHAPE,
             "held: A answering again refused");
    CHECK(binds.Count == 0 && member(1) == XHCI_TBT_NO_MEMBER,
          "held: nothing bound");

    /* Held evidence spent the moment its member reads empty, though
     * another member fails at the same attempt: A answers while B fails,
     * A empty while B still fails, then A answers again with B back. */
    two_hp();
    XhciTbtBindsInit(&binds);
    {
        ULONG two[2];

        two[0] = B(0, 5, 0);
        two[1] = B(0, 6, 0);
        findin(B(0, 4, 0), two, 2);
    }
    arm();
    up(1);
    port_up(mfind(B(0, 5, 0)), 1);
    dev_quiet(1);
    model.FailBdf = B(0, 6, 0);
    model.FailOffset = XHCI_TBT_CFG_BUSES;
    CHECK_EQ(learn(1, NULL), XHCI_TBT_MAP_UNREAD, "gap: A answers, B unread");
    put16(mfind(B(1, 0, 0)), 0x00, 0xFFFF);
    CHECK_EQ(learn(1, NULL), XHCI_TBT_MAP_UNREAD,
             "gap: A empty, B still unread");
    CHECK(binds.BaseDown == 2 && binds.BaseHeld == 0,
          "gap: A's held evidence spent at once");
    model.FailBdf = XHCI_TBT_NO_BDF;
    model.FailOffset = XHCI_TBT_NO_BDF;
    put16(mfind(B(1, 0, 0)), 0x00, 0x144D);
    CHECK_EQ(learn(1, NULL), XHCI_TBT_MAP_SHAPE,
             "gap: A answering again refused");
    CHECK_EQ(binds.Count, 0, "gap: nothing bound");

    /* A device seen in a member's scan that then fails counts: at an arm
     * it spends the evidence, at an attempt it holds it. */
    two_hp();
    XhciTbtBindsInit(&binds);
    {
        ULONG two[2];

        two[0] = B(0, 5, 0);
        two[1] = B(0, 6, 0);
        findin(B(0, 4, 0), two, 2);
    }
    arm();
    port_up(mfind(B(0, 5, 0)), 1);
    dev_quiet(1);
    model.FailBdf = B(1, 1, 0);
    model.FailOffset = XHCI_TBT_CFG_ID;
    CHECK_EQ(arm(), XHCI_TBT_MAP_UNREAD,
             "scan: A's device 0 answers, device 1 unread");
    CHECK_EQ(binds.BaseDown, 2, "scan: the arm spent A's evidence");
    model.FailBdf = XHCI_TBT_NO_BDF;
    model.FailOffset = XHCI_TBT_NO_BDF;
    put16(mfind(B(1, 0, 0)), 0x00, 0xFFFF);
    CHECK_EQ(arm(), 0, "scan: armed, A empty with its link up");
    CHECK_EQ(binds.BaseDown, 2, "scan: no evidence for A");
    up(1);
    put16(mfind(B(1, 0, 0)), 0x00, 0x144D);
    CHECK_EQ(learn(1, NULL), XHCI_TBT_MAP_SHAPE,
             "scan: A's endpoint refused");
    two_hp();
    XhciTbtBindsInit(&binds);
    {
        ULONG two[2];

        two[0] = B(0, 5, 0);
        two[1] = B(0, 6, 0);
        findin(B(0, 4, 0), two, 2);
    }
    arm();
    up(1);
    port_up(mfind(B(0, 5, 0)), 1);
    dev_quiet(1);
    model.FailBdf = B(1, 1, 0);
    model.FailOffset = XHCI_TBT_CFG_ID;
    CHECK_EQ(learn(1, NULL), XHCI_TBT_MAP_UNREAD,
             "scan: at an attempt, device 1 unread");
    CHECK_EQ(binds.BaseDown, 2,
             "scan: a gain on a scan that failed spends A's evidence");
    model.FailBdf = XHCI_TBT_NO_BDF;
    model.FailOffset = XHCI_TBT_NO_BDF;
    CHECK_EQ(learn(1, NULL), XHCI_TBT_MAP_SHAPE,
             "scan: the endpoint then refused (the recorded cost)");

    /* A gain seen at an attempt that fails on another member, rejected
     * outright (its port still down): spent, so the link coming up later
     * never admits it. A gain whose port reads up there is held and then
     * learned. */
    two_hp();
    XhciTbtBindsInit(&binds);
    {
        ULONG two[2];

        two[0] = B(0, 5, 0);
        two[1] = B(0, 6, 0);
        findin(B(0, 4, 0), two, 2);
    }
    arm();
    up(1);
    dev_quiet(1);
    model.FailBdf = B(0, 6, 0);
    model.FailOffset = XHCI_TBT_CFG_BUSES;
    CHECK_EQ(learn(1, NULL), XHCI_TBT_MAP_UNREAD,
             "reject: A gains, port down, B unread");
    CHECK_EQ(binds.BaseDown, 2, "reject: A's evidence spent");
    model.FailBdf = XHCI_TBT_NO_BDF;
    model.FailOffset = XHCI_TBT_NO_BDF;
    port_up(mfind(B(0, 5, 0)), 1);
    CHECK_EQ(learn(1, NULL), XHCI_TBT_MAP_SHAPE,
             "reject: the link up later admits nothing");
    CHECK_EQ(binds.Count, 0, "reject: nothing bound");
    two_hp();
    XhciTbtBindsInit(&binds);
    {
        ULONG two[2];

        two[0] = B(0, 5, 0);
        two[1] = B(0, 6, 0);
        findin(B(0, 4, 0), two, 2);
    }
    arm();
    up(1);
    dev_on(1);
    model.FailBdf = B(0, 6, 0);
    model.FailOffset = XHCI_TBT_CFG_BUSES;
    CHECK_EQ(learn(1, NULL), XHCI_TBT_MAP_UNREAD,
             "reject: A gains with its link up, B unread");
    CHECK(binds.BaseDown == 3 && binds.BaseHeld == 1,
          "reject: A's evidence held");
    model.FailBdf = XHCI_TBT_NO_BDF;
    model.FailOffset = XHCI_TBT_NO_BDF;
    CHECK_EQ(learn(1, NULL), XHCI_TBT_MAP_LEARNED, "reject: then learned");

    /* Held evidence behind a member scan that fails is spent: A gains
     * with its link up while B is unread (held); then A's device 0 reads
     * absent and device 1 fails; A answering again is refused. */
    two_hp();
    XhciTbtBindsInit(&binds);
    {
        ULONG two[2];

        two[0] = B(0, 5, 0);
        two[1] = B(0, 6, 0);
        findin(B(0, 4, 0), two, 2);
    }
    arm();
    up(1);
    dev_on(1);
    model.FailBdf = B(0, 6, 0);
    model.FailOffset = XHCI_TBT_CFG_BUSES;
    CHECK_EQ(learn(1, NULL), XHCI_TBT_MAP_UNREAD, "hidden: A held");
    CHECK_EQ(binds.BaseHeld, 1, "hidden: A's evidence held");
    put16(mfind(B(1, 0, 0)), 0x00, 0xFFFF);
    model.FailBdf = B(1, 1, 0);
    model.FailOffset = XHCI_TBT_CFG_ID;
    CHECK_EQ(learn(1, NULL), XHCI_TBT_MAP_UNREAD,
             "hidden: A's device 0 absent, device 1 unread");
    CHECK(binds.BaseHeld == 0 && (binds.BaseDown & 1) == 0,
          "hidden: A's evidence spent");
    model.FailBdf = XHCI_TBT_NO_BDF;
    model.FailOffset = XHCI_TBT_NO_BDF;
    put16(mfind(B(1, 0, 0)), 0x00, 0x144D);
    CHECK_EQ(learn(1, NULL), XHCI_TBT_MAP_SHAPE,
             "hidden: A answering again refused");
    CHECK_EQ(binds.Count, 0, "hidden: nothing bound");
    /* The same at an arm. */
    two_hp();
    XhciTbtBindsInit(&binds);
    {
        ULONG two[2];

        two[0] = B(0, 5, 0);
        two[1] = B(0, 6, 0);
        findin(B(0, 4, 0), two, 2);
    }
    arm();
    up(1);
    dev_on(1);
    model.FailBdf = B(0, 6, 0);
    model.FailOffset = XHCI_TBT_CFG_BUSES;
    CHECK_EQ(learn(1, NULL), XHCI_TBT_MAP_UNREAD, "hidden arm: A held");
    put16(mfind(B(1, 0, 0)), 0x00, 0xFFFF);
    model.FailBdf = B(1, 1, 0);
    model.FailOffset = XHCI_TBT_CFG_ID;
    CHECK_EQ(arm(), XHCI_TBT_MAP_UNREAD,
             "hidden arm: A's device 1 unread");
    CHECK(binds.BaseHeld == 0 && (binds.BaseDown & 1) == 0,
          "hidden arm: A's evidence spent");
    model.FailBdf = XHCI_TBT_NO_BDF;
    model.FailOffset = XHCI_TBT_NO_BDF;
    CHECK_EQ(arm(), 0, "hidden arm: armed, A empty with its link up");
    CHECK_EQ(binds.BaseDown & 1, 0, "hidden arm: no evidence for A");
    put16(mfind(B(1, 0, 0)), 0x00, 0x144D);
    CHECK_EQ(learn(1, NULL), XHCI_TBT_MAP_SHAPE,
             "hidden arm: A answering again refused");

    /* The emulator's i440FX SHPC tunnel bridge, its NVMe at 01:01.0: the
     * slot is read only through an accessor that can write DWORD Select;
     * the driver's learning reads with none, so there is no evidence and
     * the endpoint is refused; with one, learned. */
    mreset();
    ioreset();
    mdev(0, 4, 0, 0x1B36, 0x00F4, 0x0C0340UL);
    f = mbridge(0, 5, 0, 0x1B36, 0x0001, 1, 1);
    mshpc(f, 0x48, 31, 0);
    mslotid(f, 0x40);
    CHECK_EQ(find(B(0, 4, 0), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_SET,
             "shpc: the SHPC bridge a member");
    XhciTbtBindsInit(&binds);
    io.Write = NULL;
    arm();
    CHECK_EQ(binds.BaseDown, 0, "shpc: no write, no slot reading");
    up(1);
    dev_at(1, 1, 1);
    CHECK_EQ(learn(1, NULL), XHCI_TBT_MAP_SHAPE,
             "shpc: no evidence, the endpoint refused");
    dev_at(1, 1, 0);
    ioreset();
    XhciTbtBindsInit(&binds);
    arm();
    CHECK_EQ(binds.BaseDown, 1, "shpc: the slot read empty");
    up(1);
    dev_at(1, 1, 1);
    CHECK_EQ(learn(1, NULL), XHCI_TBT_MAP_LEARNED,
             "shpc: the card in the slot, learned");
    CHECK_EQ(model.StrayWrites, 0, "shpc: DWORD Select alone written");
}

int main(void)
{
    test_decoders();
    test_findcap();
    test_p14s();
    test_emu_i440fx();
    test_emu_generic();
    test_emu_q35_switch();
    test_asm4242_host();
    test_generic_edges();
    test_parent_search();
    test_parse();
    test_integrated();
    test_sets();
    test_failures();
    test_poll();
    test_named();
    test_acpi();
    test_learned();
    test_learn_window();
    test_chain_ports();
    test_chain_owed();
    test_chain_port_test();
    test_chain_bind();
    test_infer();
    test_narrowed();
    test_alone();
    printf("test_tbt_topo: %d checks, %d failures\n", checks, failures);
    return failures;
}
