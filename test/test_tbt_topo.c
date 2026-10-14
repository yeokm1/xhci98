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
    ULONG FailNth;          /* only the Nth access at FailBdf/FailOffset
                               fails, 0 for every one                       */
    ULONG FailSeen;         /* accesses counted for FailNth                 */
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
    model.FailWriteAt = 0;
    CHECK_EQ(XhciTbtPortRead(&io, B(2, 1, 0), &port), XHCI_TBT_PORT_UP,
             "next time it reads");
}

/* ---- the USB4 profile's correspondence (record 22 section 7.6) --------- */

/* A device behind a tunnel port: function 0 on its secondary bus. */
static VOID dev_on(ULONG bus)
{
    MODEL_FN *f = mfind(B(bus, 0, 0));

    if (f != NULL) {
        put16(f, 0x00, 0x144D);
        return;
    }
    mdev(bus, 0, 0, 0x144D, 0xA808, 0x010802UL);
}

static VOID dev_off(ULONG bus)
{
    MODEL_FN *f = mfind(B(bus, 0, 0));

    if (f != NULL) {
        put16(f, 0x00, 0xFFFF);
    }
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
    CHECK_EQ(find(B(0, 4, 0), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_UNLISTED,
             "named: the generic host's discovery takes nothing");
    CHECK_EQ(topo.FindOutcome, XHCI_TBT_TOPO_UNLISTED,
             "named: FindOutcome is the discovery's");
    CHECK_EQ(XhciTbtPortMember(&topo, 1), XHCI_TBT_NO_MEMBER,
             "named: no port known before any naming");
    CHECK_EQ(XhciTbtTopoAddNamed(&io, &topo, 1, B(0, 5, 0)),
             XHCI_TBT_MAP_ADDED, "named: port 1's bridge joins the set");
    CHECK_EQ(topo.Outcome, XHCI_TBT_TOPO_SET, "named: the outcome is SET");
    CHECK_EQ(topo.FindOutcome, XHCI_TBT_TOPO_UNLISTED,
             "named: the discovery's outcome kept");
    CHECK(topo.Tunnels == 1 && topo.Set[0] == B(0, 5, 0) &&
          topo.Source[0] == XHCI_TBT_SRC_ROUTER &&
          topo.Tunnel == B(0, 5, 0), "named: 00:05.0, source ROUTER");
    CHECK(topo.Port[0] == 1 && topo.Corr[0] == XHCI_TBT_CORR_NAMED &&
          topo.Named == 1, "named: port 1's, by naming");
    CHECK(topo.Pick.Bdf == B(0, 5, 0) &&
          (topo.Pick.Flags & XHCI_TBT_BR_HOTPLUG) &&
          (topo.Pick.Flags & XHCI_TBT_BR_MEMBER), "named: the pick read");
    CHECK(sibling(B(0, 5, 0)) != NULL &&
          (sibling(B(0, 5, 0))->Flags & XHCI_TBT_BR_MEMBER) &&
          !(sibling(B(0, 6, 0))->Flags & XHCI_TBT_BR_MEMBER),
          "named: the log's MEMBER flag follows");
    CHECK_EQ(XhciTbtPortMember(&topo, 1), 0, "named: port 1 -> member 0");
    CHECK_EQ(XhciTbtPortMember(&topo, 2), XHCI_TBT_NO_MEMBER,
             "named: port 2 not known");
    CHECK_EQ(XhciTbtTopoAddNamed(&io, &topo, 1, B(0, 5, 0)),
             XHCI_TBT_MAP_KNOWN, "named: the same naming again is KNOWN");
    CHECK_EQ(topo.Tunnels, 1, "named: not added twice");
    CHECK_EQ(XhciTbtTopoAddNamed(&io, &topo, 2, B(0, 6, 0)),
             XHCI_TBT_MAP_ADDED, "named: port 2's bridge");
    CHECK(topo.Tunnels == 2 && topo.Named == 2 &&
          XhciTbtPortMember(&topo, 2) == 1, "named: port 2 -> member 1");
    CHECK_EQ(XhciTbtSetRead(&io, &topo, XHCI_TBT_WANT_DOWN),
             XHCI_TBT_PORT_DOWN, "named: the set reads as any set");

    /* A bridge named for two ports: neither correspondence is kept. */
    CHECK_EQ(XhciTbtTopoAddNamed(&io, &topo, 3, B(0, 5, 0)),
             XHCI_TBT_MAP_CONFLICT, "named: 00:05.0 for port 3 too");
    CHECK(XhciTbtPortMember(&topo, 1) == XHCI_TBT_NO_MEMBER &&
          XhciTbtPortMember(&topo, 3) == XHCI_TBT_NO_MEMBER &&
          topo.Corr[0] == XHCI_TBT_CORR_CONFLICT && topo.Port[0] == 0,
          "named: ports 1 and 3 unknown, member 0 conflicted");
    CHECK_EQ(XhciTbtPortMember(&topo, 2), 1, "named: port 2 untouched");
    CHECK(topo.Tunnels == 2 && topo.Source[0] == XHCI_TBT_SRC_ROUTER,
          "named: the member stays in the set");
    CHECK_EQ(XhciTbtTopoAddNamed(&io, &topo, 1, B(0, 5, 0)),
             XHCI_TBT_MAP_CONFLICT, "named: never known again (port 1)");
    CHECK_EQ(XhciTbtTopoAddNamed(&io, &topo, 4, B(0, 5, 0)),
             XHCI_TBT_MAP_CONFLICT, "named: never known again (member)");
    CHECK_EQ(XhciTbtPortMember(&topo, 4), XHCI_TBT_NO_MEMBER,
             "named: port 4 unknown");

    /* A port named for two bridges: the first withdrawn, the second not
     * added. */
    two_hp();
    f = mbridge(0, 8, 0, 0x1B36, 0x000C, 3, 3);
    mpcie(f, 0x40, XHCI_TBT_PCIE_TYPE_ROOT, 1, XHCI_TBT_LINKCAP_DLLLARC, 0,
          XHCI_TBT_SLOTCAP_HPC, 0);
    find(B(0, 4, 0), XHCI_TBT_NO_BDF);
    CHECK_EQ(XhciTbtTopoAddNamed(&io, &topo, 1, B(0, 5, 0)),
             XHCI_TBT_MAP_ADDED, "named: port 1 -> 00:05.0");
    CHECK_EQ(XhciTbtTopoAddNamed(&io, &topo, 1, B(0, 6, 0)),
             XHCI_TBT_MAP_CONFLICT, "named: port 1 -> 00:06.0 as well");
    CHECK(topo.Tunnels == 1 && XhciTbtPortMember(&topo, 1) ==
          XHCI_TBT_NO_MEMBER && topo.Corr[0] == XHCI_TBT_CORR_CONFLICT,
          "named: 00:06.0 not added, 00:05.0 unknown");
    CHECK_EQ(XhciTbtTopoAddNamed(&io, &topo, 2, B(0, 8, 0)),
             XHCI_TBT_MAP_ADDED, "named: another port still named");

    /* What is not a bridge, or cannot be read. */
    two_hp();
    mdev(0, 0, 0, 0x8086, 0x29C0, 0x060000UL);
    find(B(0, 4, 0), XHCI_TBT_NO_BDF);
    CHECK_EQ(XhciTbtTopoAddNamed(&io, &topo, 1, B(0, 9, 0)),
             XHCI_TBT_MAP_NO_BRIDGE, "named: nothing answers there");
    CHECK_EQ(XhciTbtTopoAddNamed(&io, &topo, 1, B(0, 0, 0)),
             XHCI_TBT_MAP_NO_BRIDGE, "named: a type 0 function");
    CHECK_EQ(XhciTbtTopoAddNamed(&io, &topo, 1, B(0, 4, 0)),
             XHCI_TBT_MAP_REFUSED, "named: the NHI itself");
    model.FailBdf = B(0, 5, 0);
    CHECK_EQ(XhciTbtTopoAddNamed(&io, &topo, 1, B(0, 5, 0)),
             XHCI_TBT_MAP_UNREAD, "named: a failed identity read");
    model.FailOffset = XHCI_TBT_CFG_HEADER;
    CHECK_EQ(XhciTbtTopoAddNamed(&io, &topo, 1, B(0, 5, 0)),
             XHCI_TBT_MAP_UNREAD, "named: a failed header read");
    model.FailBdf = XHCI_TBT_NO_BDF;
    model.FailOffset = XHCI_TBT_NO_BDF;
    CHECK(topo.Tunnels == 0 && topo.Outcome == XHCI_TBT_TOPO_UNLISTED &&
          XhciTbtPortMember(&topo, 1) == XHCI_TBT_NO_MEMBER,
          "named: nothing taken by a refusal");
    CHECK_EQ(XhciTbtTopoAddNamed(&io, &topo, 1, B(0, 5, 0)),
             XHCI_TBT_MAP_ADDED, "named: taken once it reads");
    CHECK_EQ(XhciTbtTopoAddNamed(&io, &topo, 0, B(0, 6, 0)),
             XHCI_TBT_MAP_BAD_PARAM, "named: port 0");
    CHECK_EQ(XhciTbtTopoAddNamed(&io, &topo, XHCI_TBT_MAX_PORTS + 1,
                                 B(0, 6, 0)),
             XHCI_TBT_MAP_BAD_PARAM, "named: port 33");
    CHECK_EQ(XhciTbtTopoAddNamed(&io, &topo, 2, 0x10000UL),
             XHCI_TBT_MAP_BAD_PARAM, "named: a location past 16 bits");
    CHECK_EQ(XhciTbtTopoAddNamed(&io, NULL, 2, B(0, 6, 0)),
             XHCI_TBT_MAP_BAD_PARAM, "named: no topo");
    CHECK_EQ(XhciTbtTopoAddNamed(NULL, &topo, 2, B(0, 6, 0)),
             XHCI_TBT_MAP_BAD_PARAM, "named: no accessors");
    CHECK_EQ(XhciTbtTopoAddNamed(&io, &topo, XHCI_TBT_MAX_PORTS, B(0, 6, 0)),
             XHCI_TBT_MAP_ADDED, "named: port 32 is a port");
    CHECK_EQ(XhciTbtPortMember(&topo, XHCI_TBT_MAX_PORTS), 1,
             "named: port 32 -> member 1");

    /* A rediscovery starts the correspondence again. */
    find(B(0, 4, 0), XHCI_TBT_NO_BDF);
    CHECK(topo.Tunnels == 0 && topo.Named == 0 && topo.PortConflict == 0 &&
          XhciTbtPortMember(&topo, 1) == XHCI_TBT_NO_MEMBER,
          "named: XhciTbtTopoFind clears it");

    /* The override is the whole set: a named member is bound, anything
     * else refused. */
    over[0] = B(0, 5, 0);
    over[1] = B(0, 6, 0);
    CHECK_EQ(findin(B(0, 4, 0), over, 1), XHCI_TBT_TOPO_OVERRIDE,
             "named: an override of 00:05.0");
    CHECK_EQ(XhciTbtTopoAddNamed(&io, &topo, 1, B(0, 5, 0)),
             XHCI_TBT_MAP_KNOWN, "named: the override's member is port 1's");
    CHECK(topo.Source[0] == XHCI_TBT_SRC_OVERRIDE &&
          XhciTbtPortMember(&topo, 1) == 0 && topo.Named == 0,
          "named: its source stays OVERRIDE");
    CHECK_EQ(XhciTbtTopoAddNamed(&io, &topo, 2, B(0, 6, 0)),
             XHCI_TBT_MAP_REFUSED, "named: no member past the override");
    CHECK_EQ(topo.Tunnels, 1, "named: the override's set unchanged");
    over[0] = B(0, 9, 0);
    CHECK_EQ(findin(B(0, 4, 0), over, 1), XHCI_TBT_TOPO_OVERRIDE_BAD,
             "named: an override naming nothing");
    CHECK_EQ(XhciTbtTopoAddNamed(&io, &topo, 1, B(0, 5, 0)),
             XHCI_TBT_MAP_REFUSED, "named: refused under OVERRIDE_BAD");

    /* An incomplete discovery takes nothing. */
    model.FailBdf = B(0, 6, 0);
    model.FailOffset = XHCI_TBT_CFG_ID;
    CHECK_EQ(find(B(0, 4, 0), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_INCOMPLETE,
             "named: an unread function on the bus");
    model.FailBdf = XHCI_TBT_NO_BDF;
    model.FailOffset = XHCI_TBT_NO_BDF;
    CHECK_EQ(XhciTbtTopoAddNamed(&io, &topo, 1, B(0, 5, 0)),
             XHCI_TBT_MAP_REFUSED, "named: refused under INCOMPLETE");
    CHECK_EQ(topo.Tunnels, 0, "named: still empty");
    find(B(0, 4, 0), XHCI_TBT_NO_BDF);
    topo.Outcome = XHCI_TBT_TOPO_MANY;
    CHECK_EQ(XhciTbtTopoAddNamed(&io, &topo, 1, B(0, 5, 0)),
             XHCI_TBT_MAP_REFUSED, "named: refused under MANY");
    topo.Outcome = XHCI_TBT_TOPO_NONE;
    CHECK_EQ(XhciTbtTopoAddNamed(&io, &topo, 1, B(0, 5, 0)),
             XHCI_TBT_MAP_REFUSED, "named: refused with no discovery");

    /* A member by discovery keeps its source; the naming binds it. The
     * i440FX generic layout: the SHPC bridge 00:05.0. */
    mreset();
    mdev(0, 4, 0, 0x1B36, 0x00F4, 0x0C0340UL);
    f = mbridge(0, 5, 0, 0x1B36, 0x0001, 1, 1);
    mshpc(f, 0x48, 31, 0);
    CHECK_EQ(find(B(0, 4, 0), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_SET,
             "named: the SHPC bridge by discovery");
    CHECK_EQ(XhciTbtTopoAddNamed(&io, &topo, 1, B(0, 5, 0)),
             XHCI_TBT_MAP_KNOWN, "named: and named");
    CHECK(topo.Source[0] == XHCI_TBT_SRC_SHPC &&
          topo.Corr[0] == XHCI_TBT_CORR_NAMED && topo.Named == 0,
          "named: still SHPC, now port 1's");

    /* No room: four SHPC members, a fifth bridge named. */
    for (i = 0; i < 3; i++) {
        f = mbridge(0, 6 + i, 0, 0x1B36, 0x0001, 2 + i, 2 + i);
        mshpc(f, 0x48, 1, 0);
    }
    f = mbridge(0, 0x0A, 0, 0x1B36, 0x000C, 6, 6);
    mpcie(f, 0x40, XHCI_TBT_PCIE_TYPE_ROOT, 1, 0, 0, XHCI_TBT_SLOTCAP_HPC, 0);
    CHECK_EQ(find(B(0, 4, 0), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_SET,
             "named: four SHPC members");
    CHECK_EQ(XhciTbtTopoAddNamed(&io, &topo, 1, B(0, 0x0A, 0)),
             XHCI_TBT_MAP_FULL, "named: no room for a fifth");
    CHECK_EQ(topo.Tunnels, XHCI_TBT_MAX_TUNNELS, "named: four kept");

    /* The NHI's parent is never a tunnel port: the P14s, Assist on. */
    mreset();
    mload(tbt_p14s_off_on,
          sizeof(tbt_p14s_off_on) / sizeof(tbt_p14s_off_on[0]));
    CHECK_EQ(find(B(5, 0, 0), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_SET,
             "named: the P14s's switch port");
    CHECK_EQ(XhciTbtTopoAddNamed(&io, &topo, 1, B(4, 0, 0)),
             XHCI_TBT_MAP_REFUSED, "named: the NHI's parent refused");
    CHECK_EQ(XhciTbtTopoAddNamed(&io, &topo, 1, B(4, 1, 0)),
             XHCI_TBT_MAP_KNOWN, "named: the switch member bound");
    CHECK(topo.Source[0] == XHCI_TBT_SRC_SWITCH &&
          XhciTbtPortMember(&topo, 1) == 0, "named: still SWITCH");

    /* Two NHIs of an integrated family: the listed root ports are
     * ambiguous; the one the router names joins this domain's set, the
     * other stays out, and exclusive ownership decides as for any claim. */
    integrated(2);
    family_ic(1);
    mdev(0, 0x0D, 3, 0x8086, TEST_NHI_ID2 >> 16, 0x0C0340UL);
    CHECK_EQ(find(B(0, 0x0D, 2), XHCI_TBT_NO_BDF), XHCI_TBT_TOPO_AMBIGUOUS,
             "named: two NHIs, ambiguous ports");
    CHECK_EQ(XhciTbtTopoAddNamed(&io, &topo, 1, B(0, 7, 1)),
             XHCI_TBT_MAP_ADDED, "named: the router names 00:07.1");
    CHECK(topo.Outcome == XHCI_TBT_TOPO_SET &&
          topo.FindOutcome == XHCI_TBT_TOPO_AMBIGUOUS &&
          topo.Tunnels == 1 && topo.Set[0] == B(0, 7, 1) &&
          topo.Source[0] == XHCI_TBT_SRC_ROUTER,
          "named: one member, ROUTER, 00:07.0 still out");
    over[0] = B(0, 7, 1);
    CHECK_EQ(XhciTbtExclusive(topo.Set, topo.Tunnels, over, 1, list), 1,
             "named: the other NHI's router names it too: inactive");
    CHECK_EQ(list[0], 0, "named: inactive for this domain");

    /* XhciTbtLearn does not read for a named port. */
    two_hp();
    find(B(0, 4, 0), XHCI_TBT_NO_BDF);
    XhciTbtTopoAddNamed(&io, &topo, 1, B(0, 5, 0));
    reads = io.Reads;
    CHECK_EQ(XhciTbtLearn(&io, &topo, 1, NULL), XHCI_TBT_MAP_KNOWN,
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
    over[0] = B(0, 5, 0);
    over[1] = B(0, 6, 0);
    CHECK_EQ(findin(B(0, 4, 0), over, 2), XHCI_TBT_TOPO_OVERRIDE,
             "learned: two members by the override");
    CHECK_EQ(XhciTbtLearn(&io, &topo, 1, NULL), XHCI_TBT_MAP_NOT_ARMED,
             "learned: nothing armed");
    CHECK_EQ(XhciTbtLearnArm(&io, &topo, 1), 0, "learned: armed for port 1");
    CHECK(topo.LearnBus[0] == 1 && topo.LearnBus[1] == 2 &&
          topo.LearnSeen[0] == XHCI_TBT_GAIN_ABSENT &&
          topo.LearnSeen[1] == XHCI_TBT_GAIN_ABSENT,
          "learned: both secondary buses empty");
    CHECK_EQ(XhciTbtLearn(&io, &topo, 2, NULL), XHCI_TBT_MAP_NOT_ARMED,
             "learned: armed for another port");
    CHECK_EQ(XhciTbtLearn(&io, &topo, 1, NULL), XHCI_TBT_MAP_NOT_GAINED,
             "learned: nothing yet");
    dev_on(1);
    CHECK_EQ(XhciTbtLearn(&io, &topo, 1, NULL), XHCI_TBT_MAP_LEARNED,
             "learned: bus 1 gained a function: 00:05.0 is port 1's");
    CHECK(topo.Port[0] == 1 && topo.Corr[0] == XHCI_TBT_CORR_LEARNED &&
          XhciTbtPortMember(&topo, 1) == 0 && topo.LearnPort == 0,
          "learned: bound, the baseline ended");
    CHECK_EQ(topo.Source[0], XHCI_TBT_SRC_OVERRIDE,
             "learned: the source unchanged");
    reads = io.Reads;
    CHECK_EQ(XhciTbtLearn(&io, &topo, 1, NULL), XHCI_TBT_MAP_KNOWN,
             "learned: for the life of the discovery");
    CHECK_EQ(io.Reads, reads, "learned: with no read");

    /* Port 2, with port 1's device still there. */
    CHECK_EQ(XhciTbtLearnArm(&io, &topo, 2), 0, "learned: armed for port 2");
    CHECK_EQ(topo.LearnSeen[0], XHCI_TBT_GAIN_PRESENT,
             "learned: port 1's device seen at the arm");
    dev_on(2);
    CHECK_EQ(XhciTbtLearn(&io, &topo, 2, NULL), XHCI_TBT_MAP_LEARNED,
             "learned: 00:06.0 is port 2's");
    CHECK_EQ(XhciTbtPortMember(&topo, 2), 1, "learned: port 2 -> member 1");
    CHECK_EQ(XhciTbtLearn(&io, &topo, 3, NULL), XHCI_TBT_MAP_NOT_ARMED,
             "learned: the baseline ended at LEARNED");

    /* A bus that gains a function at another port's tunnel: the claims
     * disagree and neither is kept. */
    dev_off(1);
    CHECK_EQ(XhciTbtLearnArm(&io, &topo, 3), 0, "learned: armed for port 3");
    dev_on(1);
    CHECK_EQ(XhciTbtLearn(&io, &topo, 3, NULL), XHCI_TBT_MAP_CONFLICT,
             "learned: port 3 on port 1's member");
    CHECK(XhciTbtPortMember(&topo, 1) == XHCI_TBT_NO_MEMBER &&
          XhciTbtPortMember(&topo, 3) == XHCI_TBT_NO_MEMBER &&
          topo.Corr[0] == XHCI_TBT_CORR_CONFLICT,
          "learned: ports 1 and 3 unknown");
    CHECK_EQ(XhciTbtPortMember(&topo, 2), 1, "learned: port 2 kept");
    CHECK_EQ(XhciTbtLearn(&io, &topo, 1, NULL), XHCI_TBT_MAP_CONFLICT,
             "learned: port 1 never learned again");
    CHECK_EQ(XhciTbtLearnArm(&io, &topo, 4), 0, "learned: armed for port 4");
    dev_off(1);
    CHECK_EQ(XhciTbtLearn(&io, &topo, 4, NULL), XHCI_TBT_MAP_NOT_GAINED,
             "learned: a function that left is no gain");
    CHECK_EQ(XhciTbtLearnArm(&io, &topo, 4), 0, "learned: armed again");
    dev_on(1);
    CHECK_EQ(XhciTbtLearn(&io, &topo, 4, NULL), XHCI_TBT_MAP_CONFLICT,
             "learned: a conflicted member is never learned");

    /* Two buses gain at once: nothing learned. */
    two_hp();
    findin(B(0, 4, 0), over, 2);
    XhciTbtLearnArm(&io, &topo, 1);
    dev_on(1);
    dev_on(2);
    CHECK_EQ(XhciTbtLearn(&io, &topo, 1, NULL), XHCI_TBT_MAP_SEVERAL,
             "learned: two members gained");
    CHECK(XhciTbtPortMember(&topo, 1) == XHCI_TBT_NO_MEMBER &&
          topo.Corr[0] == XHCI_TBT_CORR_NONE &&
          topo.Corr[1] == XHCI_TBT_CORR_NONE, "learned: neither bound");
    CHECK_EQ(XhciTbtLearn(&io, &topo, 1, NULL), XHCI_TBT_MAP_NOT_ARMED,
             "learned: the baseline ended at SEVERAL");

    /* The member that gained is another domain's. */
    two_hp();
    findin(B(0, 4, 0), over, 2);
    active[0] = 0;
    active[1] = 1;
    XhciTbtLearnArm(&io, &topo, 1);
    dev_on(1);
    CHECK_EQ(XhciTbtLearn(&io, &topo, 1, active), XHCI_TBT_MAP_INACTIVE,
             "learned: an inactive member is never learned");
    CHECK_EQ(XhciTbtPortMember(&topo, 1), XHCI_TBT_NO_MEMBER,
             "learned: port 1 unknown");

    /* Unreadable: a failed access, a bus number that moved, a bridge with
     * no secondary bus. */
    two_hp();
    findin(B(0, 4, 0), over, 2);
    XhciTbtLearnArm(&io, &topo, 1);
    dev_on(1);
    model.FailBdf = B(0, 6, 0);
    model.FailOffset = XHCI_TBT_CFG_BUSES;
    CHECK_EQ(XhciTbtLearn(&io, &topo, 1, NULL), XHCI_TBT_MAP_UNREAD,
             "learned: the other member's bus numbers unread");
    model.FailBdf = B(1, 0, 0);
    model.FailOffset = XHCI_TBT_CFG_ID;
    CHECK_EQ(XhciTbtLearn(&io, &topo, 1, NULL), XHCI_TBT_MAP_UNREAD,
             "learned: the gained function's identity unread");
    model.FailBdf = XHCI_TBT_NO_BDF;
    model.FailOffset = XHCI_TBT_NO_BDF;
    CHECK_EQ(XhciTbtLearn(&io, &topo, 1, NULL), XHCI_TBT_MAP_LEARNED,
             "learned: the baseline survives an unread reading");

    two_hp();
    findin(B(0, 4, 0), over, 2);
    XhciTbtLearnArm(&io, &topo, 1);
    mfind(B(0, 5, 0))->Cfg[0x19] = 7;
    mfind(B(0, 5, 0))->Cfg[0x1A] = 7;
    dev_on(7);
    CHECK_EQ(XhciTbtLearn(&io, &topo, 1, NULL), XHCI_TBT_MAP_UNREAD,
             "learned: a secondary bus that changed since the arm");

    two_hp();
    mfind(B(0, 6, 0))->Cfg[0x19] = 0;
    findin(B(0, 4, 0), over, 2);
    XhciTbtLearnArm(&io, &topo, 1);
    CHECK_EQ(topo.LearnSeen[1], XHCI_TBT_GAIN_UNREAD,
             "learned: no secondary bus is unread");
    dev_on(1);
    CHECK_EQ(XhciTbtLearn(&io, &topo, 1, NULL), XHCI_TBT_MAP_UNREAD,
             "learned: and nothing is learned beside it");

    /* A naming after the arm ends the baseline; a learned port then named
     * for another bridge conflicts. */
    two_hp();
    find(B(0, 4, 0), XHCI_TBT_NO_BDF);
    XhciTbtTopoAddNamed(&io, &topo, 2, B(0, 6, 0));
    XhciTbtLearnArm(&io, &topo, 1);
    XhciTbtTopoAddNamed(&io, &topo, 1, B(0, 5, 0));
    CHECK_EQ(XhciTbtLearn(&io, &topo, 1, NULL), XHCI_TBT_MAP_KNOWN,
             "learned: port 1 named meanwhile");
    two_hp();
    find(B(0, 4, 0), XHCI_TBT_NO_BDF);
    XhciTbtTopoAddNamed(&io, &topo, 2, B(0, 6, 0));
    XhciTbtLearnArm(&io, &topo, 1);
    CHECK_EQ(topo.LearnPort, 1, "learned: armed over a named set");
    XhciTbtTopoAddNamed(&io, &topo, 3, B(0, 5, 0));
    CHECK_EQ(topo.LearnPort, 0, "learned: a new member ends the baseline");
    XhciTbtLearnArm(&io, &topo, 1);
    dev_on(2);
    CHECK_EQ(XhciTbtLearn(&io, &topo, 1, NULL), XHCI_TBT_MAP_CONFLICT,
             "learned: port 1 gained on port 2's named member");
    CHECK(XhciTbtPortMember(&topo, 2) == XHCI_TBT_NO_MEMBER &&
          XhciTbtPortMember(&topo, 1) == XHCI_TBT_NO_MEMBER,
          "learned: the router's naming withdrawn with it");
    CHECK_EQ(XhciTbtPortMember(&topo, 3), 1, "learned: port 3 kept");

    /* A single member: learned like any. Bad parameters. */
    two_hp();
    findin(B(0, 4, 0), over, 1);
    CHECK_EQ(XhciTbtLearnArm(&io, &topo, 0), XHCI_TBT_MAP_BAD_PARAM,
             "learned: arm port 0");
    CHECK_EQ(XhciTbtLearnArm(&io, &topo, XHCI_TBT_MAX_PORTS + 1),
             XHCI_TBT_MAP_BAD_PARAM, "learned: arm port 33");
    CHECK_EQ(XhciTbtLearnArm(NULL, &topo, 1), XHCI_TBT_MAP_BAD_PARAM,
             "learned: arm with no accessors");
    CHECK_EQ(XhciTbtLearnArm(&io, NULL, 1), XHCI_TBT_MAP_BAD_PARAM,
             "learned: arm with no topo");
    CHECK_EQ(XhciTbtLearn(&io, &topo, 0, NULL), XHCI_TBT_MAP_BAD_PARAM,
             "learned: port 0");
    CHECK_EQ(XhciTbtLearn(&io, NULL, 1, NULL), XHCI_TBT_MAP_BAD_PARAM,
             "learned: no topo");
    CHECK_EQ(XhciTbtLearnArm(&io, &topo, 1), 0, "learned: one member");
    dev_on(1);
    CHECK_EQ(XhciTbtLearn(&io, &topo, 1, NULL), XHCI_TBT_MAP_LEARNED,
             "learned: one member learned");
    find(B(0, 4, 0), XHCI_TBT_NO_BDF);
    CHECK_EQ(XhciTbtLearnArm(&io, &topo, 1), XHCI_TBT_MAP_BAD_PARAM,
             "learned: an empty set has nothing to learn");
    CHECK_EQ(XhciTbtLearn(&io, &topo, 1, NULL), XHCI_TBT_MAP_NOT_ARMED,
             "learned: and stays unarmed");
    CHECK_EQ(XhciTbtPortMember(NULL, 1), XHCI_TBT_NO_MEMBER,
             "learned: no topo, no member");
    CHECK_EQ(XhciTbtPortMember(&topo, 0), XHCI_TBT_NO_MEMBER,
             "learned: port 0, no member");
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
    test_named();
    test_learned();
    printf("test_tbt_topo: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
