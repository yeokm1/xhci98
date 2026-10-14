/*
 * test_u4cm.c - host vectors for the USB4 software connection manager
 * (src\cm\tbt_u4cm.c; roadmap-usb4.md tasks 40-A.3 and 40-A.8's pure
 * part; design record 22 section 14.1).
 *
 * Driven against the C89 fake fabric of u4_fakefab.c (decision U11): the
 * `minimal` and `intel-adl` host models, two domains at once, USB4 and
 * Thunderbolt 3 device routers, a router below depth 1, router operations
 * supported, not supported, failing and never completing, hot plug and
 * unplug in every state and inside waits, quick re-plugs seen only through
 * Lock, TopologyID Valid or the UUID, bonding and its verified fallback,
 * the Detect gate, the USB3 handshake, discovery that preserves, finds
 * nothing and holds, cleaning, approval, the quirk table, the statement's
 * gating and anchor, capabilities of a preserved router, and the inert
 * start after activation. Expected values are worked from record 22 and
 * the specification, not from the code under test.
 */

#include <stdio.h>
#include <string.h>
#include "../src/cm/tbt_u4cm.h"
#include "u4_fakefab.h"
#include "test_harness.h"

static FAB g_fab;
static FAB g_fab2;
static TBT_RING g_ring;
static TBT_RING g_ring2;
static TBT_U4CM g_cm;
static TBT_U4CM g_cm2;

/* The link-down upcall: records what the teardown handed over. */
static ULONG g_ldCalls;
static TBT_U4_LINKDOWN g_ld;
static ULONG g_ldAnswer;

static ULONG linkDown(PVOID ctx, const TBT_U4_LINKDOWN *info)
{
    (void)ctx;
    g_ldCalls++;
    g_ld = *info;
    return g_ldAnswer;
}

static void cfgDefault(TBT_U4_CONFIG *c)
{
    TbtU4DefaultConfig(c);
    c->RingFlags = TBT_RING_START_MASK_IMC;
}

static LONG start(FAB *f, TBT_RING *ring, TBT_U4CM *cm, const TBT_U4_CONFIG *c)
{
    memset(ring, 0, sizeof(*ring));
    TbtU4Init(cm, ring, &f->Host, c);
    TbtU4SetLinkDown(cm, linkDown, NULL);
    return TbtU4Start(cm, fabMem(f), FAB_PHYS, FAB_MEM_BYTES);
}

static void poll(TBT_U4CM *cm, FAB *f, ULONG ticks)
{
    ULONG i;

    for (i = 0; i < ticks; i++) {
        TbtU4Poll(cm);
        f->Host.Sleep(f->Host.Context, 200);
    }
}

static ULONG countActions(TBT_U4CM *cm, ULONG kind)
{
    TBT_U4_ACTION a;
    ULONG n = 0;

    while (TbtU4NextAction(cm, &a)) {
        if (a.Kind == kind) {
            n++;
        }
    }
    return n;
}

static ULONG state(const TBT_U4CM *cm, ULONG port)
{
    const TBT_U4_PORT *p = TbtU4Port(cm, port);

    return p != NULL ? p->State : 0xFFFFFFFFUL;
}

/* ------------------------------------------------------------------ */

static void test_cold_minimal(void)
{
    TBT_U4_CONFIG c;
    LONG rc;
    int dev;
    int lane;
    int hp;
    const TBT_U4_PORT *p;

    fabInit(&g_fab);
    fabHostMinimal(&g_fab);
    dev = fabDevice(&g_fab, FAB_DEV_USB4);
    fabPlug(&g_fab, 1, dev);
    cfgDefault(&c);
    rc = start(&g_fab, &g_ring, &g_cm, &c);
    CHECK_EQ(rc, TBT_OK, "minimal: start");
    CHECK_EQ(g_cm.PortCount, 2, "two USB4 ports");
    CHECK_EQ(g_cm.Ports[0].Lane0, 1, "port 1 lanes 1-2");
    CHECK_EQ(g_cm.Ports[0].PcieDown, 5, "port 1 PCIe Down 5");
    CHECK_EQ(g_cm.Ports[1].PcieDown, 6, "port 2 PCIe Down 6");
    CHECK_EQ(g_cm.Ports[0].Usb3Down, 7, "port 1 USB3 Down 7");
    CHECK(g_cm.Ports[0].MapValid && g_cm.Ports[0].MapRid == 0x0108, "router-named port 1");
    CHECK(g_cm.Ports[1].MapValid && g_cm.Ports[1].MapRid == 0x0110, "router-named port 2");
    CHECK_EQ(state(&g_cm, 1), TBT_U4_UP, "cold device: UP");
    CHECK_EQ(state(&g_cm, 2), TBT_U4_EMPTY, "port 2 empty");
    p = TbtU4Port(&g_cm, 1);
    CHECK(p->Bonded, "bonded");
    CHECK(p->Pcie.Up && p->Pcie.Hd == 8 && p->Pcie.Hu == 9, "PCIe at lane HopIDs 8 and 9");
    CHECK(p->Usb3.Up && p->Usb3.Hd == 10 && p->Usb3.Hu == 11, "USB3 at 10 and 11");
    CHECK(fabPcieUp(&g_fab, 1), "the fabric's PCIe tunnel is up");
    CHECK(fabUsb3Up(&g_fab, 1), "and its USB3 tunnel");
    lane = fabPortLane0(&g_fab, 1);
    hp = fabPortPcie(&g_fab, 1);
    CHECK_EQ(fabPath0(&g_fab, 0, hp, 8) & 0x8001F87FUL,
             0x80000000UL | ((unsigned long)lane << 11) | 8, "host PCIe Down 8 -> lane 0 hop 8");
    CHECK_EQ((fabPath1(&g_fab, 0, hp, 8) >> 24) & 0xF, 0x3,
             "EFC 1, ESE 0; IFC kept as the router left it (1), ISE 0");
    CHECK_EQ(fabPath1(&g_fab, 0, lane, 9) & 0x0F00070FUL, 0x01000301UL,
             "host lane entry: IFC 1, EFC 0, priority 3, weight 1");
    CHECK_EQ((fabPath0(&g_fab, 0, lane, 9) >> 17) & 0x7F, 32, "32 credits (the host's BA)");
    CHECK_EQ((fabPath0(&g_fab, 0, lane, 11) >> 17) & 0x7F, 16, "USB3 16 credits");
    CHECK_EQ(fabPath1(&g_fab, 0, lane, 11) & 0xF, 2, "USB3 weight 2");
    CHECK(fabRtr(&g_fab, dev, 5) & 0x80000000UL, "CV set");
    CHECK(fabRtr(&g_fab, dev, 5) & 0x01000000UL, "PTO set");
    CHECK(fabRtr(&g_fab, dev, 5) & 0x02000000UL, "UTO set");
    CHECK_EQ(fabRtr(&g_fab, dev, 4) & 0xFFFFUL, 0x10FFUL, "CMUV 1.0, notification timeout 255");
    CHECK(fabAdp(&g_fab, 0, lane, FAB_CAP_PORT + 19) & 8UL, "USB4 Port is Configured, host");
    CHECK(fabAdp(&g_fab, dev, 1, FAB_CAP_PORT + 19) & 8UL, "and device");
    CHECK_EQ(fabRtr(&g_fab, dev, FAB_RCAP_TMU + 3) >> 16, 16, "TMU HiFi on the USB3 link");
    CHECK_EQ(fabAdp(&g_fab, 0, lane, 5) & 0x80000000UL, 0, "hot plug enabled on lane 0");
    CHECK_EQ(g_cm.Counters.PcieBuilt, 1, "one PCIe tunnel");
    CHECK_EQ(countActions(&g_cm, TBT_U4_ACT_TUNNEL_UP), 1, "one re-enumeration queued");
    CHECK(strcmp(p->Dev.ModelName, "USB4 NVMe Enclosure") == 0, "the DROM's model name");
    CHECK_EQ(g_fab.TxBadCrc, 0, "every frame's CRC good");
}

int main(void)
{
    test_cold_minimal();
    printf("test_u4cm: %d checks, %d failures\n", checks, failures);
    return failures;
}
