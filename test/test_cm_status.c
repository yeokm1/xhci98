/*
 * test_cm_status.c - the status record xhcicm.sys publishes for the install
 * tool's device viewer (src\cm\cm_status.c; design record 24 section 3):
 * the record from an ICM table and from a USB4 port set, what is never
 * copied into it, the change compare, the entry limit, and the Windows 98
 * instance path over a fake configuration space.
 */

#include <stdio.h>
#include <string.h>
#include "../src/cm/cm_statb.h"
#include "test_harness.h"

static TBT_ICM icm;
static TBT_U4CM u4;
static UCHAR rec[CM_ST_MAX_BYTES];
static UCHAR rec2[CM_ST_MAX_BYTES];

static ULONG get32(const UCHAR *b, ULONG o)
{
    return (ULONG)b[o] | ((ULONG)b[o + 1] << 8) | ((ULONG)b[o + 2] << 16) |
           ((ULONG)b[o + 3] << 24);
}

static const UCHAR *entry(const UCHAR *b, ULONG n)
{
    return b + CM_ST_HDR_BYTES + n * CM_ST_ENTRY_BYTES;
}

static int contains(const UCHAR *b, ULONG n, ULONG word)
{
    ULONG i;

    for (i = 0; i + 4 <= n; i++) {
        if (get32(b, i) == word) {
            return 1;
        }
    }
    return 0;
}

static void base_in(CM_STATUS_IN *in, ULONG cm)
{
    memset(in, 0, sizeof(*in));
    in->NhiId = 0x15BF8086UL;
    in->Profile = 2;
    in->Stage = CM_ST_STAGE_ACTIVE;
    in->Cm = cm;
    in->PortCount = 2;
    in->PortBdf[0] = 0x0208UL;  /* 02:01.0 */
    in->PortBdf[1] = 0x0210UL;  /* 02:02.0 */
    in->PortActive = 0x1UL;
}

static void test_icm(void)
{
    CM_STATUS_IN in;
    ULONG n;
    const UCHAR *e;
    TBT_ICM_DEVICE *d;

    memset(&icm, 0, sizeof(icm));
    icm.Ready.SecurityLevel = 1;
    icm.Ready.RomVersion = 0x20;
    d = &icm.Devices[3];
    d->InUse = TRUE;
    d->Approved = TRUE;
    d->Link = 1;
    d->Depth = 1;
    d->RouteValid = TRUE;
    d->Route.Lo = 0x1;
    d->Uuid[0] = 0x11111111UL;
    d->Uuid[1] = 0x22222222UL;
    d->Uuid[2] = 0x33333333UL;
    d->Uuid[3] = 0x44443A7CUL;
    d->Key = 0xC0FFEE11UL;
    d->Names.Present = TBT_ICM_NAMES_VENDOR | TBT_ICM_NAMES_MODEL |
                       TBT_ICM_NAMES_INTEL;
    strcpy(d->Names.Vendor, "Lenovo");
    strcpy(d->Names.Model,
           "ThinkPad Thunderbolt 3 Dock with a very long model name");
    d->Names.Model[5] = (char)0xE9;
    d->Names.TbVendorId = 0x0109;
    d->Names.TbModelId = 0x2031;
    icm.Devices[5].InUse = TRUE;
    icm.Devices[5].ApproveFailed = TRUE;
    icm.Devices[6].InUse = TRUE;
    icm.Devices[6].ApproveUnknown = TRUE;
    icm.Devices[7].InUse = TRUE;
    icm.Devices[7].Approved = TRUE;
    icm.Devices[7].Boot = TRUE;

    base_in(&in, CM_ST_CM_ICM);
    n = CmStatusBuild(rec, &in, &icm, NULL, NULL);
    CHECK_EQ(n, CM_ST_HDR_BYTES + 4 * CM_ST_ENTRY_BYTES, "four ICM entries");
    CHECK_EQ(get32(rec, CM_ST_O_MAGIC), CM_ST_MAGIC, "magic");
    CHECK_EQ(get32(rec, CM_ST_O_VERSION), CM_ST_VERSION, "version");
    CHECK_EQ(get32(rec, CM_ST_O_BYTES), n, "size field");
    CHECK_EQ(get32(rec, CM_ST_O_ROUTER_COUNT), 4, "count");
    CHECK_EQ(get32(rec, CM_ST_O_ENTRY_BYTES), CM_ST_ENTRY_BYTES, "entry size");
    CHECK_EQ(get32(rec, CM_ST_O_GENERATION), 0, "the stamp is left 0");
    CHECK_EQ(get32(rec, CM_ST_O_NHI_ID), 0x15BF8086UL, "NHI id");
    CHECK_EQ(get32(rec, CM_ST_O_CM), CM_ST_CM_ICM, "the ICM");
    CHECK_EQ(get32(rec, CM_ST_O_SECURITY), 1, "security level");
    CHECK_EQ(get32(rec, CM_ST_O_FW_VERSION), 0x20, "ROM version where no NVM");
    CHECK_EQ(get32(rec, CM_ST_O_PORT_COUNT), 2, "ports");
    CHECK_EQ(get32(rec, CM_ST_O_PORT_ACTIVE), 1, "active mask");
    CHECK_EQ(get32(rec, CM_ST_O_PORTS), 0x0208UL, "port 0");
    CHECK_EQ(get32(rec, CM_ST_O_PORTS + 8), CM_ST_UNKNOWN, "port 2 unused");
    CHECK_EQ(rec[CM_ST_O_PATHS], 0, "no path given");
    CHECK_EQ(get32(rec, CM_ST_O_ECHO), 0, "no ask read: echo 0");
    in.Echo = 0x5EED1234UL;
    (void)CmStatusBuild(rec2, &in, &icm, NULL, NULL);
    CHECK_EQ(get32(rec2, CM_ST_O_ECHO), 0x5EED1234UL, "the echo");
    CHECK(CmStatusChanged(rec, n, rec2, n), "a new echo is a change");
    in.Echo = 0;

    e = entry(rec, 0);
    CHECK_EQ(get32(e, CM_ST_E_KIND), CM_ST_KIND_ICM, "kind");
    CHECK_EQ(get32(e, CM_ST_E_PLACE), 0x0101UL, "link 1 depth 1");
    CHECK_EQ(get32(e, CM_ST_E_ROUTE_LO), 1, "route");
    CHECK_EQ(get32(e, CM_ST_E_APPROVAL), CM_ST_APPR_APPROVED, "approved");
    CHECK_EQ(get32(e, CM_ST_E_IDS), 0x20310109UL, "Thunderbolt ids");
    CHECK_EQ(get32(e, CM_ST_E_GEN), CM_ST_GEN_TBT3, "Thunderbolt 3");
    CHECK_EQ(get32(e, CM_ST_E_TUNNELS), 0x551UL, "PCIe up, the rest unknown");
    CHECK_EQ(get32(e, CM_ST_E_BRIDGE), 0x0208UL, "the single active member");
    CHECK_EQ(get32(e, CM_ST_E_SHORT_ID), 0x3A7CUL, "16-bit UUID suffix");
    CHECK(strcmp((const char *)e + CM_ST_E_VENDOR, "Lenovo") == 0, "vendor");
    CHECK(strlen((const char *)e + CM_ST_E_MODEL) == CM_ST_NAME_BYTES - 1,
          "model cut to 31");
    CHECK(e[CM_ST_E_MODEL + 5] == '?', "non-ASCII written as ?");
    CHECK(!contains(rec, n, 0xC0FFEE11UL), "the Key is never copied");
    CHECK(!contains(rec, n, 0x44443A7CUL), "the whole UUID word is not copied");
    CHECK(!contains(rec, n, 0x11111111UL), "nor any other UUID word");
    CHECK_EQ(get32(entry(rec, 1), CM_ST_E_APPROVAL), CM_ST_APPR_FAILED,
             "approval refused");
    CHECK_EQ(get32(entry(rec, 1), CM_ST_E_TUNNELS) & 0xF, CM_ST_TUN_NONE,
             "no tunnel when not approved");
    CHECK_EQ(get32(entry(rec, 1), CM_ST_E_BRIDGE), CM_ST_UNKNOWN,
             "no bridge when not approved");
    CHECK_EQ(get32(entry(rec, 1), CM_ST_E_IDS), CM_ST_UNKNOWN,
             "no ids without the Intel entry");
    CHECK_EQ(get32(entry(rec, 2), CM_ST_E_APPROVAL), CM_ST_APPR_UNANSWERED,
             "approval unanswered");
    CHECK_EQ(get32(entry(rec, 3), CM_ST_E_APPROVAL), CM_ST_APPR_BOOT,
             "approved before boot");
    CHECK_EQ(get32(entry(rec, 1), CM_ST_E_ROUTE_LO), CM_ST_UNKNOWN,
             "no route when the event carried none");

    /* Two active members: no bridge is named. */
    in.PortActive = 0x3UL;
    (void)CmStatusBuild(rec2, &in, &icm, NULL, NULL);
    CHECK_EQ(get32(entry(rec2, 0), CM_ST_E_BRIDGE), CM_ST_UNKNOWN,
             "two active members name no bridge");

    /* The change compare ignores the stamp alone. */
    in.PortActive = 0x1UL;
    n = CmStatusBuild(rec, &in, &icm, NULL, NULL);
    (void)CmStatusBuild(rec2, &in, &icm, NULL, NULL);
    CmStatusStamp(rec2, 7, 1234, 0xAAAAAAAAUL, 0x01D9BBBBUL);
    CHECK(!CmStatusChanged(rec, n, rec2, n), "a stamp alone is no change");
    CHECK_EQ(get32(rec2, CM_ST_O_GENERATION), 7, "stamp generation");
    CHECK_EQ(get32(rec2, CM_ST_O_UPTIME_S), 1234, "stamp uptime");
    CHECK_EQ(get32(rec2, CM_ST_O_SYSTIME_HI), 0x01D9BBBBUL, "stamp time");
    icm.Devices[5].InUse = FALSE;
    n = CmStatusBuild(rec2, &in, &icm, NULL, NULL);
    CHECK(CmStatusChanged(rec, CM_ST_HDR_BYTES + 4 * CM_ST_ENTRY_BYTES, rec2,
                          n),
          "a device leaving is a change");
    memcpy(rec, rec2, n);
    rec2[CM_ST_HDR_BYTES + CM_ST_E_APPROVAL] ^= 1;
    CHECK(CmStatusChanged(rec, n, rec2, n), "one field is a change");
}

static void test_limit(void)
{
    CM_STATUS_IN in;
    ULONG n;
    ULONG i;

    memset(&u4, 0, sizeof(u4));
    u4.PortCount = TBT_U4_MAX_PORTS;
    for (i = 0; i < TBT_U4_MAX_PORTS; i++) {
        u4.Ports[i].Index = i + 1;
        u4.Ports[i].State = TBT_U4_UP;
        u4.Ports[i].Dev.Present = TRUE;
    }
    for (i = 0; i < TBT_U4_MAX_LINKS; i++) {
        u4.Links[i].Used = TRUE;
    }
    base_in(&in, CM_ST_CM_USB4);
    n = CmStatusBuild(rec, &in, NULL, &u4, NULL);
    CHECK_EQ(get32(rec, CM_ST_O_ROUTER_COUNT),
             TBT_U4_MAX_PORTS + TBT_U4_MAX_LINKS, "every router fits");
    CHECK(TBT_U4_MAX_PORTS + TBT_U4_MAX_LINKS <= CM_ST_MAX_ROUTERS,
          "the USB4 maximum fits the record");
    CHECK(TBT_ICM_MAX_DEVICES <= CM_ST_MAX_ROUTERS,
          "the ICM maximum fits the record");
    CHECK(n <= CM_ST_MAX_BYTES, "within the maximum");
    CHECK_EQ(get32(rec, CM_ST_O_FLAGS), 0, "not truncated");
    CHECK_EQ(CM_ST_MAX_BYTES, 128 + 24 * 128, "the maximum size");
    CHECK_EQ(CM_ST_O_ECHO + 4, CM_ST_HDR_BYTES, "the echo ends the header");
    CHECK_EQ(CM_ST_O_PATHS + CM_ST_MAX_PORTS * CM_ST_PATH_BYTES,
             CM_ST_O_SYSTIME_LO, "the paths end where the time begins");
    CHECK(CM_ST_E_MODEL + CM_ST_NAME_BYTES == CM_ST_ENTRY_BYTES,
          "the model name ends the entry");
}

static void test_u4(void)
{
    CM_STATUS_IN in;
    XHCI_TBT_BINDS binds;
    ULONG n;
    const UCHAR *e;
    TBT_U4_PORT *p;

    memset(&u4, 0, sizeof(u4));
    memset(&binds, 0, sizeof(binds));
    u4.PortCount = 2;
    p = &u4.Ports[0];
    p->Index = 1;
    p->State = TBT_U4_UP;
    p->Bonded = TRUE;
    p->Pcie.Up = TRUE;
    p->Usb3.Up = TRUE;
    p->Usb3.Preserved = TRUE;
    p->Dev.Present = TRUE;
    p->Dev.Vendor = 0x8087;
    p->Dev.Device = 0x0B26;
    p->Dev.Usb4Version = 0x10;
    p->Dev.Route.Lo = 0x1;
    p->Dev.UuidValid = TRUE;
    p->Dev.Uuid[3] = 0xABCD1234UL;
    strcpy(p->Dev.VendorName, "Example");
    strcpy(p->Dev.ModelName, "USB4 Dock");
    p = &u4.Ports[1];
    p->Index = 2;
    p->State = TBT_U4_EMPTY;
    p->PcieFailed = TRUE;
    u4.Links[2].Used = TRUE;
    u4.Links[2].Port = 1;
    u4.Links[2].ChildTbt3 = TRUE;
    u4.Links[2].Child.Lo = 0x301;
    binds.Count = 2;
    binds.Port[0] = 2;
    binds.Bdf[0] = 0x0510UL;
    binds.How[0] = XHCI_TBT_CORR_NAMED;
    binds.Port[1] = 1;
    binds.Bdf[1] = 0x0408UL;
    binds.How[1] = XHCI_TBT_CORR_LEARNED;

    base_in(&in, CM_ST_CM_USB4);
    n = CmStatusBuild(rec, &in, NULL, &u4, &binds);
    CHECK_EQ(n, CM_ST_HDR_BYTES + 2 * CM_ST_ENTRY_BYTES,
             "the empty port is not listed; the link is");
    CHECK_EQ(get32(rec, CM_ST_O_SECURITY), CM_ST_UNKNOWN, "no ICM security");
    e = entry(rec, 0);
    CHECK_EQ(get32(e, CM_ST_E_KIND), CM_ST_KIND_USB4_PORT, "kind");
    CHECK_EQ(get32(e, CM_ST_E_PLACE), 0x0101UL, "port 1 depth 1");
    CHECK_EQ(get32(e, CM_ST_E_APPROVAL), CM_ST_APPR_APPROVED, "UP is approved");
    CHECK_EQ(get32(e, CM_ST_E_PORT_STATE), TBT_U4_UP, "port state");
    CHECK_EQ(get32(e, CM_ST_E_IDS), 0x0B268087UL, "router ids");
    CHECK_EQ(get32(e, CM_ST_E_GEN), CM_ST_GEN_USB4 | 0x1000UL, "USB4 1.0");
    CHECK_EQ(get32(e, CM_ST_E_LANES), CM_ST_LANES_BONDED, "bonded");
    CHECK_EQ(get32(e, CM_ST_E_TUNNELS),
             CM_ST_TUN_UP | (CM_ST_TUN_PRESERVED << 4), "PCIe up, USB3 kept");
    CHECK_EQ(get32(e, CM_ST_E_BRIDGE), 0x0408UL, "the learned bind");
    CHECK_EQ(get32(e, CM_ST_E_SHORT_ID), 0x1234UL, "UUID suffix");
    CHECK(!contains(rec, n, 0xABCD1234UL), "the UUID word is not copied");
    CHECK(strcmp((const char *)e + CM_ST_E_MODEL, "USB4 Dock") == 0, "model");
    e = entry(rec, 1);
    CHECK_EQ(get32(e, CM_ST_E_KIND), CM_ST_KIND_USB4_LINK, "a deeper router");
    CHECK_EQ(get32(e, CM_ST_E_GEN), CM_ST_GEN_TBT3, "a Thunderbolt 3 router");
    CHECK_EQ(get32(e, CM_ST_E_ROUTE_LO), 0x301UL, "its route");
    CHECK_EQ(get32(e, CM_ST_E_TUNNELS), 0x555UL, "unknown tunnels");
    CHECK_EQ(get32(e, CM_ST_E_SHORT_ID), CM_ST_UNKNOWN, "UUID not read");

    /* A failed PCIe tunnel, an ejected one, a held port. */
    u4.Ports[1].State = TBT_U4_FAILED;
    u4.Ports[1].Why = TBT_U4_WHY_PATH;
    n = CmStatusBuild(rec, &in, NULL, &u4, &binds);
    e = entry(rec, 1);
    CHECK_EQ(get32(e, CM_ST_E_APPROVAL), CM_ST_APPR_HELD, "failed is held");
    CHECK_EQ(get32(e, CM_ST_E_PORT_STATE),
             TBT_U4_FAILED | (TBT_U4_WHY_PATH << 8), "state and reason");
    CHECK_EQ(get32(e, CM_ST_E_TUNNELS) & 0xF, CM_ST_TUN_FAILED, "PCIe failed");
    CHECK_EQ(get32(e, CM_ST_E_BRIDGE), 0x0510UL, "the named bind");
    CHECK_EQ(get32(e, CM_ST_E_GEN), CM_ST_GEN_UNKNOWN, "no router present");
    u4.Ports[0].Pcie.Up = FALSE;
    u4.Ports[0].Ejected = TRUE;
    (void)CmStatusBuild(rec, &in, NULL, &u4, &binds);
    CHECK_EQ(get32(entry(rec, 0), CM_ST_E_TUNNELS) & 0xF, CM_ST_TUN_EJECTED,
             "torn down after an eject");

    /* A conflicted port names no bridge. */
    binds.PortConflict = 0x1UL;
    (void)CmStatusBuild(rec, &in, NULL, &u4, &binds);
    CHECK_EQ(get32(entry(rec, 0), CM_ST_E_BRIDGE), CM_ST_UNKNOWN,
             "a conflicted port");
    binds.PortConflict = 0;
    binds.Port[0] = 1;
    (void)CmStatusBuild(rec, &in, NULL, &u4, &binds);
    CHECK_EQ(get32(entry(rec, 0), CM_ST_E_BRIDGE), CM_ST_UNKNOWN,
             "two bridges for one port name none");
    (void)CmStatusBuild(rec, &in, NULL, &u4, NULL);
    CHECK_EQ(get32(entry(rec, 0), CM_ST_E_BRIDGE), CM_ST_UNKNOWN,
             "no binds");

    /* Stopped: no routers whatever the tables hold. */
    in.Stage = CM_ST_STAGE_STOPPED;
    in.Cm = CM_ST_CM_NONE;
    n = CmStatusBuild(rec, &in, NULL, NULL, NULL);
    CHECK_EQ(n, CM_ST_HDR_BYTES, "a stopped record has no routers");
    CHECK_EQ(get32(rec, CM_ST_O_STAGE), CM_ST_STAGE_STOPPED, "stopped");

    /* The paths. */
    base_in(&in, CM_ST_CM_NONE);
    in.PathCount[0] = 4;
    in.Path[0][0] = 0x08;
    in.Path[0][1] = 0x08;
    in.Path[0][2] = 0x30;
    in.Path[0][3] = 0x00;
    in.PathCount[1] = 9;
    (void)CmStatusBuild(rec, &in, NULL, NULL, NULL);
    CHECK_EQ(rec[CM_ST_O_PATHS], 4, "path count");
    CHECK_EQ(rec[CM_ST_O_PATHS + 3], 0x30, "path element");
    CHECK_EQ(rec[CM_ST_O_PATHS + CM_ST_PATH_BYTES], 0, "too long is unknown");
}

/* ---- the 98 path over a fake bus ---------------------------------------- */

/* The emulator's Alpine Ridge tree (runs/run-39.md): the root bridge at
 * 00:06.0 (secondary 1), the upstream port 01:01.0 (secondary 2), the
 * downstream ports 02:00.0, 02:01.0 (secondary 4), 02:02.0. */
static ULONG fakeUnreadableBus;

static ULONG fake_read(PVOID context, ULONG bdf, ULONG offset, PULONG value)
{
    ULONG bus;
    ULONG dev;
    ULONG fn;

    (void)context;
    bus = XHCI_TBT_BDF_BUS(bdf);
    dev = (bdf >> 3) & 0x1FUL;
    fn = bdf & 7UL;
    if (fakeUnreadableBus != 0xFFUL && bus == fakeUnreadableBus) {
        return 0;
    }
    *value = 0xFFFFFFFFUL;
    if (fn != 0) {
        return 1;
    }
    if (bus == 0 && dev == 0) {
        *value = offset == 0 ? 0x12378086UL : 0;
    } else if (bus == 0 && dev == 6) {
        *value = offset == 0 ? 0x00011B36UL
                 : offset == XHCI_TBT_CFG_HEADER ? 0x00010000UL
                 : offset == XHCI_TBT_CFG_BUSES ? 0x00020100UL : 0;
    } else if (bus == 1 && dev == 1) {
        *value = offset == 0 ? 0x15C08086UL
                 : offset == XHCI_TBT_CFG_HEADER ? 0x00010000UL
                 : offset == XHCI_TBT_CFG_BUSES ? 0x00050201UL : 0;
    } else if (bus == 2 && dev <= 2) {
        *value = offset == 0 ? 0x15C08086UL
                 : offset == XHCI_TBT_CFG_HEADER ? 0x00010000UL
                 : offset == XHCI_TBT_CFG_BUSES
                     ? (0x00000002UL | ((3UL + dev) << 8) | ((3UL + dev) << 16))
                     : 0;
    }
    return 1;
}

static void test_path(void)
{
    XHCI_TBT_CFG cfg;
    ULONG path[CM_ST_PATH_MAX];
    ULONG n;

    memset(&cfg, 0, sizeof(cfg));
    cfg.Read = fake_read;
    fakeUnreadableBus = 0xFFUL;
    CHECK(CmStatusPciPath(&cfg, XHCI_TBT_BDF(2, 1, 0), path, &n,
                          CM_ST_PATH_MAX), "02:01.0 has a path");
    CHECK_EQ(n, 4, "four elements");
    CHECK(path[0] == 0x08 && path[1] == 0x08 && path[2] == 0x30 &&
              path[3] == 0x00, "08 08 30 00, as run-39 read it");
    CHECK(CmStatusPciPath(&cfg, XHCI_TBT_BDF(0, 6, 0), path, &n,
                          CM_ST_PATH_MAX), "a bus 0 function");
    CHECK(n == 2 && path[0] == 0x30 && path[1] == 0, "30 00");
    CHECK(CmStatusPciPath(&cfg, XHCI_TBT_BDF(2, 2, 0), path, &n,
                          CM_ST_PATH_MAX) && n == 4 && path[0] == 0x10,
          "02:02.0 is 10 08 30 00");
    CHECK(!CmStatusPciPath(&cfg, XHCI_TBT_BDF(2, 1, 0), path, &n, 3),
          "a path longer than the room");
    fakeUnreadableBus = 0;
    CHECK(!CmStatusPciPath(&cfg, XHCI_TBT_BDF(2, 1, 0), path, &n,
                           CM_ST_PATH_MAX), "bus 0 unreadable: unknown");
    fakeUnreadableBus = 0xFFUL;
    CHECK(CmStatusPciPath(&cfg, XHCI_TBT_BDF(9, 0, 0), path, &n,
                          CM_ST_PATH_MAX) && n == 2 && path[1] == 9,
          "a bus with no parent is a root bus");
}

int main(void)
{
    test_icm();
    test_limit();
    test_u4();
    test_path();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
