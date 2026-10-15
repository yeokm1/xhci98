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
#include "../src/cm/tbt_hw.h"
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

/* ------------------------------------------------------------------ */
/* Hooks: unplug at a chosen request                                  */
/* ------------------------------------------------------------------ */

static int g_hkRouter;
static unsigned long g_hkCs;
static unsigned long g_hkAdp;
static unsigned long g_hkAddr;
static int g_hkWrite;
static int g_hkPort;
static int g_hkFired;
static unsigned long g_hkSkip;

static int unplugHook(FAB *f, unsigned long pdf, int router, unsigned long cs,
                      unsigned long adp, unsigned long addr, unsigned long n,
                      const unsigned long *data)
{
    (void)data;
    if (g_hkFired || router != g_hkRouter || cs != g_hkCs ||
        (g_hkWrite && pdf != TBT_PDF_WRITE) || (!g_hkWrite && pdf != TBT_PDF_READ) ||
        (cs != TBT_CS_ROUTER && adp != g_hkAdp) ||
        g_hkAddr < addr || g_hkAddr >= addr + n) {
        return 0;
    }
    if (g_hkSkip != 0) {
        g_hkSkip--;
        return 0;
    }
    g_hkFired = 1;
    fabUnplug(f, g_hkPort);
    return 0;
}

static void armUnplug(FAB *f, int router, unsigned long cs, unsigned long adp,
                      unsigned long addr, int write, int port)
{
    g_hkRouter = router;
    g_hkCs = cs;
    g_hkAdp = adp;
    g_hkAddr = addr;
    g_hkWrite = write;
    g_hkPort = port;
    g_hkFired = 0;
    g_hkSkip = 0;
    f->Hook = unplugHook;
}

/* No host-side entry of the port left Valid or pending, and the host's
 * PCIe Down disabled. */
static int hostClean(const FAB *f, int port)
{
    int lane = fabPortLane0(f, port);
    int hp = fabPortPcie(f, port);
    int hu = fabPortUsb3(f, port);
    int h;

    for (h = 8; h < 24; h++) {
        if ((fabPath0(f, 0, lane, h) & 0x80000000UL) ||
            (fabPath1(f, 0, lane, h) & 0x10000000UL)) {
            return 0;
        }
    }
    if (hp && ((fabPath0(f, 0, hp, 8) & 0x80000000UL) ||
               (fabAdp(f, 0, hp, FAB_CAP_ADP) & 0x80000000UL))) {
        return 0;
    }
    if (hu && ((fabPath0(f, 0, hu, 8) & 0x80000000UL) ||
               (fabAdp(f, 0, hu, FAB_CAP_ADP) & 0x80000000UL))) {
        return 0;
    }
    return 1;
}

static int quarantineCount(const TBT_U4CM *cm)
{
    int i;
    int n = 0;

    for (i = 0; i < (int)TBT_U4_QUARANTINE; i++) {
        if (cm->Quarantine[i].Used) {
            n++;
        }
    }
    return n;
}

static int setupMinimal(int plugged, const TBT_U4_CONFIG *c)
{
    int dev;
    TBT_U4_CONFIG d;

    fabInit(&g_fab);
    fabHostMinimal(&g_fab);
    dev = fabDevice(&g_fab, FAB_DEV_USB4);
    if (plugged) {
        fabPlug(&g_fab, 1, dev);
    }
    if (c == NULL) {
        cfgDefault(&d);
        c = &d;
    }
    g_ldCalls = 0;
    g_ldAnswer = TBT_U4_STMT_WITHHELD;
    memset(&g_ld, 0, sizeof(g_ld));
    (void)start(&g_fab, &g_ring, &g_cm, c);
    return dev;
}

/* ------------------------------------------------------------------ */

static void test_hot_plug_unplug(void)
{
    int dev;
    int lane;
    ULONG gen;
    const TBT_U4_PORT *p;

    dev = setupMinimal(0, NULL);
    CHECK_EQ(state(&g_cm, 1), TBT_U4_EMPTY, "nothing attached");
    fabPlug(&g_fab, 1, dev);
    poll(&g_cm, &g_fab, 2);
    CHECK(g_fab.HpAcks >= 1, "the plug event acknowledged");
    CHECK_EQ(state(&g_cm, 1), TBT_U4_UP, "hot plug: UP");
    p = TbtU4Port(&g_cm, 1);
    gen = p->Generation;
    (void)countActions(&g_cm, 0);

    /* A presence sample anchors the statement's deadline. */
    TbtU4PresenceSample(&g_cm, 1, gen, g_fab.NowUs, TRUE);
    CHECK(p->AnchorValid, "anchor taken");
    TbtU4PresenceSample(&g_cm, 1, gen + 7, g_fab.NowUs, TRUE);
    CHECK_EQ(p->AnchorGeneration, gen, "a sample of another generation ignored");

    lane = fabPortLane0(&g_fab, 1);
    fabUnplug(&g_fab, 1);
    poll(&g_cm, &g_fab, 1);
    CHECK_EQ(state(&g_cm, 1), TBT_U4_EMPTY, "unplug: EMPTY");
    CHECK(hostClean(&g_fab, 1), "the host side torn down");
    CHECK_EQ(g_ldCalls, 1, "the link-down upcall ran once");
    CHECK(g_ld.PathClosed, "with the path proven closed");
    CHECK(g_ld.AnchorValid, "the anchor handed over");
    CHECK(g_ld.RemainingUs > 0 && g_ld.RemainingUs <= TBT_U4_STATEMENT_US,
          "and what is left of 3 s");
    CHECK_EQ(g_ld.Port, 1, "for port 1");
    CHECK(g_ld.MapValid && g_ld.MapRid == 0x0108, "with its router-named bridge");
    CHECK_EQ(countActions(&g_cm, TBT_U4_ACT_TUNNEL_DOWN), 1, "a removal queued");
    CHECK_EQ(fabAdp(&g_fab, 0, lane, FAB_CAP_PORT + 19) & 8UL, 0, "Port Configured cleared");
    CHECK_EQ(fabAdp(&g_fab, 0, lane, FAB_CAP_LANE + 1) & 0x30UL, 0x10UL,
             "Target Link Width restored");
    CHECK_EQ(fabAdp(&g_fab, 0, lane, FAB_CAP_TMU_ADP + 6) & 2UL, 2UL,
             "host port TMU: Disable Time Sync again");
    CHECK_EQ(quarantineCount(&g_cm), 0, "nothing quarantined");
    CHECK_EQ(g_cm.Counters.Usb3TornDown, 1, "the USB3 host side too");

    /* Re-plug: a new router, a new generation, the Detect gate again. */
    fabPlug(&g_fab, 1, dev);
    poll(&g_cm, &g_fab, 2);
    CHECK_EQ(state(&g_cm, 1), TBT_U4_UP, "re-plug: UP");
    CHECK(p->Generation > gen, "a new generation");
    CHECK(p->Usb3.Up, "USB3 again, 500 ms after the last teardown");
    CHECK_EQ(g_cm.Counters.PcieBuilt, 2, "two PCIe tunnels built in all");
}

/* An unplug at each point of the sequence: never a hang, the port
 * EMPTY, the host side clean. */
static void test_unplug_everywhere(void)
{
    static const struct {
        int devRouter;
        unsigned long cs;
        unsigned long adp;
        unsigned long addr;
        int write;
        const char *what;
    } pts[] = {
        { 1, TBT_CS_ROUTER, 0, 3, 1, "enumeration write" },
        { 1, TBT_CS_ROUTER, 0, 6, 0, "Router Ready wait" },
        { 1, TBT_CS_ROUTER, 0, 26, 1, "a router operation" },
        { 0, TBT_CS_ADAPTER, 1, FAB_CAP_LANE + 1, 1, "bonding" },
        { 1, TBT_CS_ROUTER, 0, FAB_RCAP_TMU + 25, 1, "TMU post time" },
        { 1, TBT_CS_ROUTER, 0, 5, 1, "Configuration Valid" },
        { 1, TBT_CS_ROUTER, 0, 6, 0, "Configuration Ready wait" },
        { 0, TBT_CS_PATH, 5, 16, 1, "PCIe host entry" },
        { 1, TBT_CS_PATH, 1, 16, 1, "PCIe device entry" },
        { 1, TBT_CS_ADAPTER, 5, FAB_CAP_ADP, 1, "PCIe Path Enable" },
        { 0, TBT_CS_ADAPTER, 7, FAB_CAP_ADP + 2, 1, "USB3 bandwidth request" },
        { 0, TBT_CS_PATH, 7, 16, 1, "USB3 host entry" },
        { 1, TBT_CS_ADAPTER, 6, FAB_CAP_ADP, 1, "USB3 Path Enable" }
    };
    unsigned i;
    int dev;
    ULONG t0;
    char msg[96];

    for (i = 0; i < sizeof(pts) / sizeof(pts[0]); i++) {
        dev = setupMinimal(0, NULL);
        armUnplug(&g_fab, pts[i].devRouter ? dev : 0, pts[i].cs, pts[i].adp, pts[i].addr,
                  pts[i].write, 1);
        if (pts[i].devRouter && pts[i].addr == 6 && pts[i].cs == TBT_CS_ROUTER &&
            strstr(pts[i].what, "Configuration") != NULL) {
            g_hkSkip = 1;       /* the second read of CS_6 */
        }
        fabPlug(&g_fab, 1, dev);
        t0 = g_fab.NowUs;
        poll(&g_cm, &g_fab, 3);
        sprintf(msg, "unplug during %s: fired", pts[i].what);
        CHECK(g_hkFired, msg);
        sprintf(msg, "unplug during %s: EMPTY", pts[i].what);
        CHECK_EQ(state(&g_cm, 1), TBT_U4_EMPTY, msg);
        sprintf(msg, "unplug during %s: host side clean", pts[i].what);
        CHECK(hostClean(&g_fab, 1), msg);
        sprintf(msg, "unplug during %s: bounded", pts[i].what);
        CHECK(g_fab.NowUs - t0 < 30000000UL, msg);
        sprintf(msg, "unplug during %s: nothing quarantined", pts[i].what);
        CHECK_EQ(quarantineCount(&g_cm), 0, msg);
        g_fab.Hook = NULL;
        /* And the port works again. */
        fabPlug(&g_fab, 1, dev);
        poll(&g_cm, &g_fab, 2);
        sprintf(msg, "after an unplug during %s: re-plug UP", pts[i].what);
        CHECK_EQ(state(&g_cm, 1), TBT_U4_UP, msg);
    }
}

/* Unplug during tunnel-up on port 2 while port 1's sequence runs: the
 * safe point nests port 2's teardown. */
static void test_unplug_nested(void)
{
    int d1;
    int d2;
    TBT_U4_CONFIG c;

    fabInit(&g_fab);
    fabHostMinimal(&g_fab);
    d1 = fabDevice(&g_fab, FAB_DEV_USB4);
    d2 = fabDevice(&g_fab, FAB_DEV_USB4);
    fabPlug(&g_fab, 2, d2);
    cfgDefault(&c);
    g_ldCalls = 0;
    (void)start(&g_fab, &g_ring, &g_cm, &c);
    CHECK_EQ(state(&g_cm, 2), TBT_U4_UP, "port 2 up");
    /* While port 1 connects, pull port 2 at port 1's CV write. */
    g_hkRouter = d1;
    g_hkCs = TBT_CS_ROUTER;
    g_hkAddr = 5;
    g_hkWrite = 1;
    g_hkPort = 2;
    g_hkFired = 0;
    g_hkSkip = 0;
    g_fab.Hook = unplugHook;
    fabPlug(&g_fab, 1, d1);
    poll(&g_cm, &g_fab, 3);
    CHECK(g_hkFired, "port 2 pulled during port 1's sequence");
    CHECK_EQ(state(&g_cm, 1), TBT_U4_UP, "port 1 still comes up");
    CHECK_EQ(state(&g_cm, 2), TBT_U4_EMPTY, "port 2 torn down");
    CHECK(hostClean(&g_fab, 2), "port 2's host side clean");
    CHECK(g_cm.Counters.UnplugsNested >= 1 || g_cm.Counters.Unplugs >= 1, "counted");
    g_fab.Hook = NULL;
}

static void test_quick_replug(void)
{
    int dev;
    int d2;
    ULONG gen;
    const TBT_U4_PORT *p;

    /* Lock: unplugged and re-plugged between two checks, no event (hot
     * plug disabled on the lane). */
    dev = setupMinimal(1, NULL);
    p = TbtU4Port(&g_cm, 1);
    gen = p->Generation;
    g_fab.R[0].Adp[fabPortLane0(&g_fab, 1)].Cs[5] |= 0x80000000UL;
    fabUnplug(&g_fab, 1);
    fabPlug(&g_fab, 1, dev);
    poll(&g_cm, &g_fab, 2);
    CHECK_EQ(state(&g_cm, 1), TBT_U4_UP, "quick re-plug seen through Lock: UP again");
    CHECK(p->Generation >= gen + 1, "as a new connection");
    CHECK(g_cm.Counters.QuickReplugs >= 1, "counted as a quick re-plug");

    /* TopologyID Valid clear: the router was reset under a live link. */
    gen = p->Generation;
    g_fab.R[dev].Rcs[3] &= ~0x80000000UL;
    poll(&g_cm, &g_fab, 2);
    CHECK_EQ(state(&g_cm, 1), TBT_U4_UP, "a fresh router behind a live link: UP again");
    CHECK(p->Generation > gen, "torn down and reconnected");

    /* A different router, enumerated by someone else, swapped in between
     * two checks with Lock as it was: only its UUID tells. */
    gen = p->Generation;
    d2 = fabDevice(&g_fab, FAB_DEV_USB4);
    g_fab.R[d2].Rcs[7] = 0xCAFEF00DUL;
    fabSwapQuiet(&g_fab, 1, d2);
    poll(&g_cm, &g_fab, 2);
    CHECK_EQ(state(&g_cm, 1), TBT_U4_UP, "another UUID: UP again");
    CHECK(p->Generation > gen, "as another router");
}

static void test_bonding(void)
{
    TBT_U4_CONFIG c;
    int dev;
    int lane;
    const TBT_U4_PORT *p;

    /* Impossible (the device's BE clear): lane 1 disabled, verified. */
    fabInit(&g_fab);
    fabHostMinimal(&g_fab);
    dev = fabDevice(&g_fab, FAB_DEV_USB4);
    g_fab.R[dev].BondPossible = 0;
    g_fab.R[dev].Adp[1].Cs[FAB_CAP_PORT + 18] = 0;
    fabPlug(&g_fab, 1, dev);
    cfgDefault(&c);
    (void)start(&g_fab, &g_ring, &g_cm, &c);
    p = TbtU4Port(&g_cm, 1);
    lane = fabPortLane0(&g_fab, 1);
    CHECK_EQ(state(&g_cm, 1), TBT_U4_UP, "bonding impossible: UP single");
    CHECK(!p->Bonded && p->SingleLane && p->Lane1Disabled, "lane 1 disabled");
    CHECK(fabAdp(&g_fab, 0, lane + 1, FAB_CAP_LANE + 1) & 0x4000UL, "Lane Disable set");
    CHECK_EQ((fabPath0(&g_fab, 0, lane, 9) >> 17) & 0x7F, 32,
             "host BA's 32 credits even single (within what the lane has)");
    fabUnplug(&g_fab, 1);
    poll(&g_cm, &g_fab, 1);
    CHECK_EQ(fabAdp(&g_fab, 0, lane + 1, FAB_CAP_LANE + 1) & 0x4000UL, 0,
             "Lane Disable cleared at unplug");

    /* Bonding that never reaches x2: the fields written back, then lane 1
     * disabled. */
    fabInit(&g_fab);
    fabHostMinimal(&g_fab);
    dev = fabDevice(&g_fab, FAB_DEV_USB4);
    g_fab.R[dev].BondNoWidth = 1;
    fabPlug(&g_fab, 1, dev);
    (void)start(&g_fab, &g_ring, &g_cm, &c);
    p = TbtU4Port(&g_cm, 1);
    lane = fabPortLane0(&g_fab, 1);
    CHECK_EQ(state(&g_cm, 1), TBT_U4_UP, "bonding timeout: UP single");
    CHECK(p->Lane1Disabled, "via lane 1 disabled");
    CHECK_EQ(fabAdp(&g_fab, 0, lane, FAB_CAP_LANE + 1) & 0x30UL, 0x10UL,
             "Target Link Width written back to single");
    CHECK_EQ(fabAdp(&g_fab, dev, 1, FAB_CAP_LANE + 1) & 0x30UL, 0x10UL,
             "on the device too");

    /* The preconditions unmet: a CLx enable left on the host's lane 1. */
    fabInit(&g_fab);
    fabHostMinimal(&g_fab);
    dev = fabDevice(&g_fab, FAB_DEV_USB4);
    g_fab.R[dev].BondNoWidth = 1;
    lane = fabPortLane0(&g_fab, 1);
    g_fab.R[0].Adp[lane + 1].Cs[FAB_CAP_LANE + 1] |= 0x0800UL;
    fabPlug(&g_fab, 1, dev);
    (void)start(&g_fab, &g_ring, &g_cm, &c);
    CHECK_EQ(state(&g_cm, 1), TBT_U4_FAILED, "CL1 enabled on lane 1: FAILED");
    CHECK_EQ(TbtU4Port(&g_cm, 1)->Why, TBT_U4_WHY_BONDING, "for bonding");
    CHECK_EQ(fabPath0(&g_fab, 0, fabPortPcie(&g_fab, 1), 8) & 0x80000000UL, 0, "no path");
    CHECK_EQ(fabAdp(&g_fab, 0, lane + 1, FAB_CAP_LANE + 1) & 0x4000UL, 0,
             "Lane Disable not touched");

    /* A predecessor's non-default Target Link Width, bonding impossible:
     * not rewritten, FAILED. */
    fabInit(&g_fab);
    fabHostMinimal(&g_fab);
    dev = fabDevice(&g_fab, FAB_DEV_USB4);
    g_fab.R[dev].BondPossible = 0;
    g_fab.R[dev].Adp[1].Cs[FAB_CAP_PORT + 18] = 0;
    lane = fabPortLane0(&g_fab, 1);
    g_fab.R[0].Adp[lane].Cs[FAB_CAP_LANE + 1] |= 0x30UL;
    fabPlug(&g_fab, 1, dev);
    (void)start(&g_fab, &g_ring, &g_cm, &c);
    CHECK_EQ(state(&g_cm, 1), TBT_U4_FAILED, "a predecessor's dual width: FAILED");
    CHECK_EQ(fabAdp(&g_fab, 0, lane, FAB_CAP_LANE + 1) & 0x30UL, 0x30UL, "left as found");

    /* XhciCmLaneBonding 0. */
    fabInit(&g_fab);
    fabHostMinimal(&g_fab);
    dev = fabDevice(&g_fab, FAB_DEV_USB4);
    fabPlug(&g_fab, 1, dev);
    c.LaneBonding = FALSE;
    (void)start(&g_fab, &g_ring, &g_cm, &c);
    p = TbtU4Port(&g_cm, 1);
    CHECK_EQ(state(&g_cm, 1), TBT_U4_UP, "bonding off: UP");
    CHECK(!p->Bonded && p->Lane1Disabled, "single, lane 1 disabled");
    CHECK_EQ(fabFindWrite(&g_fab, 0, 0, TBT_CS_ADAPTER, (unsigned long)fabPortLane0(&g_fab, 1),
                          FAB_CAP_LANE + 1), -1, "lane 0's width never written");
    CHECK(fabFindWrite(&g_fab, 0, 0, TBT_CS_ADAPTER,
                       (unsigned long)fabPortLane0(&g_fab, 1) + 1, FAB_CAP_LANE + 1) >= 0,
          "lane 1's Lane Disable written");
    CHECK(!p->Usb3.Up || p->TmuOk, "USB3 only with the TMU ready");
}

static void test_rops(void)
{
    int dev;
    int k;
    TBT_U4_CONFIG c;
    const TBT_U4_PORT *p;

    /* Every router operation Not Supported, host and device. */
    fabInit(&g_fab);
    fabHostMinimal(&g_fab);
    dev = fabDevice(&g_fab, FAB_DEV_USB4);
    for (k = 0; k < FAB_OPS; k++) {
        g_fab.R[0].RopMode[k] = FAB_ROP_NS;
        g_fab.R[dev].RopMode[k] = FAB_ROP_NS;
    }
    fabPlug(&g_fab, 1, dev);
    cfgDefault(&c);
    (void)start(&g_fab, &g_ring, &g_cm, &c);
    p = TbtU4Port(&g_cm, 1);
    CHECK_EQ(state(&g_cm, 1), TBT_U4_UP, "no router operation: still UP");
    CHECK(!g_cm.Ports[0].MapValid, "no router-named bridge");
    CHECK(p->Dev.ModelName[0] == 0, "unnamed");
    CHECK_EQ((fabPath0(&g_fab, 0, fabPortLane0(&g_fab, 1), 9) >> 17) & 0x7F, 32,
             "32 credits on a bonded lane by default");
    CHECK(g_cm.Counters.RopUnsupported >= 4, "each not-supported counted");

    /* Failing operations. */
    fabInit(&g_fab);
    fabHostMinimal(&g_fab);
    dev = fabDevice(&g_fab, FAB_DEV_USB4);
    for (k = 0; k < FAB_OPS; k++) {
        g_fab.R[dev].RopMode[k] = FAB_ROP_FAIL;
    }
    fabPlug(&g_fab, 1, dev);
    (void)start(&g_fab, &g_ring, &g_cm, &c);
    CHECK_EQ(state(&g_cm, 1), TBT_U4_UP, "failing operations: still UP");
    CHECK(g_cm.Counters.RopFailed >= 3, "each failure counted");

    /* Never completing: the router operation-blocked, then everything
     * takes its unsupported branch. */
    fabInit(&g_fab);
    fabHostMinimal(&g_fab);
    dev = fabDevice(&g_fab, FAB_DEV_USB4);
    g_fab.R[dev].RopMode[FAB_OP_DROM] = FAB_ROP_HANG;
    fabPlug(&g_fab, 1, dev);
    (void)start(&g_fab, &g_ring, &g_cm, &c);
    p = TbtU4Port(&g_cm, 1);
    CHECK_EQ(state(&g_cm, 1), TBT_U4_UP, "a hanging operation: still UP");
    CHECK(p->Dev.OpBlocked, "the router operation-blocked");
    CHECK(g_cm.Counters.RopTimeouts == 1 && g_cm.Counters.RopBlocked >= 1,
          "one timeout, then blocked without another write");
    CHECK_EQ(p->Dev.CapsState, TBT_U4_CAPS_DEFAULT,
             "enumerated here: capabilities at their defaults anyway");
}

static void test_detect_and_hca(void)
{
    int dev;
    TBT_U4_CONFIG c;
    const TBT_U4_PORT *p;

    /* The device's PCIe Up never in Detect: no path entry written. */
    fabInit(&g_fab);
    fabHostMinimal(&g_fab);
    dev = fabDevice(&g_fab, FAB_DEV_USB4);
    g_fab.R[dev].Adp[5].DetectStuck = 1;
    fabPlug(&g_fab, 1, dev);
    cfgDefault(&c);
    (void)start(&g_fab, &g_ring, &g_cm, &c);
    p = TbtU4Port(&g_cm, 1);
    CHECK_EQ(state(&g_cm, 1), TBT_U4_FAILED, "Detect gate timed out: FAILED");
    CHECK_EQ(p->Why, TBT_U4_WHY_DETECT, "for the gate");
    CHECK_EQ(g_cm.Counters.DetectTimeouts, 1, "counted");
    CHECK_EQ(fabFindWrite(&g_fab, 0, 0, TBT_CS_PATH, (unsigned long)fabPortPcie(&g_fab, 1), 16),
             -1, "no PCIe path entry written");
    CHECK(p->Usb3.Up, "the USB3 tunnel still built");

    /* HCA never answers: no USB3 tunnel, CMR cleared, PCIe untouched. */
    fabInit(&g_fab);
    fabHostMinimal(&g_fab);
    dev = fabDevice(&g_fab, FAB_DEV_USB4);
    g_fab.R[0].Adp[7].HcaStuck = 1;
    fabPlug(&g_fab, 1, dev);
    (void)start(&g_fab, &g_ring, &g_cm, &c);
    p = TbtU4Port(&g_cm, 1);
    CHECK_EQ(state(&g_cm, 1), TBT_U4_UP, "HCA silent: still UP");
    CHECK(p->Pcie.Up && !p->Usb3.Up && p->Usb3Failed, "PCIe up, USB3 failed");
    CHECK_EQ(fabAdp(&g_fab, 0, 7, FAB_CAP_ADP + 2) & 0x80000000UL, 0, "CMR cleared");
    CHECK_EQ(fabPath0(&g_fab, 0, 7, 8) & 0x80000000UL, 0, "no USB3 path");
    CHECK_EQ(g_cm.Counters.HcaTimeouts, 1, "counted");
}

static void test_approve_and_usb3_off(void)
{
    int dev;
    TBT_U4_CONFIG c;
    const TBT_U4_PORT *p;

    fabInit(&g_fab);
    fabHostMinimal(&g_fab);
    dev = fabDevice(&g_fab, FAB_DEV_USB4);
    fabPlug(&g_fab, 1, dev);
    cfgDefault(&c);
    c.Approve = FALSE;
    (void)start(&g_fab, &g_ring, &g_cm, &c);
    p = TbtU4Port(&g_cm, 1);
    CHECK_EQ(state(&g_cm, 1), TBT_U4_UP, "approval 0: UP");
    CHECK(!p->Pcie.Up && p->Usb3.Up, "no PCIe, USB3 built");
    CHECK_EQ(fabPath0(&g_fab, 0, 5, 8) & 0x80000000UL, 0, "no PCIe entry");
    CHECK_EQ(countActions(&g_cm, TBT_U4_ACT_TUNNEL_UP), 0, "nothing to re-enumerate");

    fabInit(&g_fab);
    fabHostMinimal(&g_fab);
    dev = fabDevice(&g_fab, FAB_DEV_USB4);
    fabPlug(&g_fab, 1, dev);
    cfgDefault(&c);
    c.Usb3Tunnel = FALSE;
    (void)start(&g_fab, &g_ring, &g_cm, &c);
    p = TbtU4Port(&g_cm, 1);
    CHECK_EQ(state(&g_cm, 1), TBT_U4_UP, "USB3 off: UP");
    CHECK(p->Pcie.Up && !p->Usb3.Up, "PCIe only");
    CHECK_EQ(fabAdp(&g_fab, 0, 7, FAB_CAP_ADP) & 0xC0000000UL, 0x40000000UL,
             "host USB3 Down: Path Enable 0, Valid 1");
    CHECK_EQ(fabAdp(&g_fab, dev, 6, FAB_CAP_ADP) & 0xC0000000UL, 0x40000000UL,
             "device USB3 Up the same");
    CHECK(fabRtr(&g_fab, dev, 5) & 0x02000000UL, "UTO set all the same (Guide 3.2.3)");
    CHECK_EQ(fabRtr(&g_fab, dev, FAB_RCAP_TMU + 3) >> 16, 0, "no TMU without USB3");
}

static void test_quirks(void)
{
    CHECK_EQ(TbtU4QuirkMatch(0x8087, 0x463E), TBT_U4_QUIRK_USB3_MAX_BW, "ADL NHI0");
    CHECK_EQ(TbtU4QuirkMatch(0x8087, 0x466D), TBT_U4_QUIRK_USB3_MAX_BW, "ADL NHI1");
    CHECK_EQ(TbtU4QuirkMatch(0x8087, 0xA73E), TBT_U4_QUIRK_USB3_MAX_BW, "RPL NHI0");
    CHECK_EQ(TbtU4QuirkMatch(0x8087, 0xA76D), TBT_U4_QUIRK_USB3_MAX_BW, "RPL NHI1");
    CHECK_EQ(TbtU4QuirkMatch(0x8087, 0x7EB2), TBT_U4_QUIRK_USB3_MAX_BW, "MTL-M");
    CHECK_EQ(TbtU4QuirkMatch(0x8087, 0x7EC2), TBT_U4_QUIRK_USB3_MAX_BW, "MTL-P 0");
    CHECK_EQ(TbtU4QuirkMatch(0x8087, 0x7EC3), TBT_U4_QUIRK_USB3_MAX_BW, "MTL-P 1");
    CHECK_EQ(TbtU4QuirkMatch(0x8087, 0x5781),
             TBT_U4_QUIRK_USB3_MAX_BW | TBT_U4_QUIRK_BLOCK_RPM, "Barlow Ridge host 80G");
    CHECK_EQ(TbtU4QuirkMatch(0x8087, 0x5784),
             TBT_U4_QUIRK_USB3_MAX_BW | TBT_U4_QUIRK_BLOCK_RPM, "Barlow Ridge host 40G");
    CHECK_EQ(TbtU4QuirkMatch(0x8087, 0x5786), TBT_U4_QUIRK_USB3_MAX_BW, "Barlow Ridge hub 80G");
    CHECK_EQ(TbtU4QuirkMatch(0x8087, 0x57A4), TBT_U4_QUIRK_USB3_MAX_BW, "Barlow Ridge hub 40G");
    CHECK_EQ(TbtU4QuirkMatch(0x8086, 0x15EF), TBT_U4_QUIRK_NO_CLX, "Titan Ridge DD");
    CHECK_EQ(TbtU4QuirkMatch(0x0438, 0x0208), TBT_U4_QUIRK_NO_CLX, "AMD 0208");
    CHECK_EQ(TbtU4QuirkMatch(0x0438, 0x020B), TBT_U4_QUIRK_NO_CLX, "AMD 020B");
    CHECK_EQ(TbtU4QuirkMatch(0x0438, 0x020C), 0, "AMD 020C is not listed");
    CHECK_EQ(TbtU4QuirkMatch(0x8087, 0x0B26), TBT_U4_QUIRK_DP_CREDITS, "Goshen Ridge");
    CHECK_EQ(TbtU4QuirkMatch(0x8086, 0x463E), 0, "8086 is not 8087");
    CHECK_EQ(TbtU4QuirkMatchDrom(0x00D4, 0xB070), TBT_U4_QUIRK_FORCE_POWER, "WD19TB");
    CHECK_EQ(TbtU4QuirkMatchDrom(0x00D4, 0xB071), TBT_U4_QUIRK_FORCE_POWER, "WD19TB 2");
    CHECK_EQ(TbtU4QuirkMatchDrom(0x00D4, 0xB072), 0, "not B072");
    CHECK_EQ(TbtU4LaneClass(2), TBT_U4_LANE_CONNECTED, "CL0 connected");
    CHECK_EQ(TbtU4LaneClass(6), TBT_U4_LANE_CONNECTED, "CL2 a live link");
    CHECK_EQ(TbtU4LaneClass(1), TBT_U4_LANE_TRANSITIONAL, "training");
    CHECK_EQ(TbtU4LaneClass(7), TBT_U4_LANE_DISCONNECTED, "CLd");
    CHECK_EQ(TbtU4LaneClass(0), TBT_U4_LANE_DISABLED, "disabled");
    CHECK_EQ(TbtU4LaneClass(9), TBT_U4_LANE_UNKNOWN, "reserved");
}

/* ------------------------------------------------------------------ */
/* Tunnels found at start (record 22 section 9)                       */
/* ------------------------------------------------------------------ */

static unsigned long pathWrites(const FAB *f)
{
    unsigned long i;
    unsigned long n = 0;

    for (i = 0; i < f->WrCount && i < FAB_WRLOG; i++) {
        if (f->WrCs[i] == TBT_CS_PATH ||
            (f->WrCs[i] == TBT_CS_ADAPTER && f->WrAddr[i] == FAB_CAP_ADP &&
             f->WrAdp[i] != 0 && f->WrRouter[i] >= 0 &&
             ((f->R[f->WrRouter[i]].Adp[f->WrAdp[i]].Cs[2] >> 16) == 0x10 ||
              (f->R[f->WrRouter[i]].Adp[f->WrAdp[i]].Cs[2] >> 16) == 0x20))) {
            n++;
        }
    }
    return n;
}

static void test_preos(void)
{
    static const int adl[2] = { 0, 1 };
    TBT_U4_CONFIG c;
    int dev;
    int k;
    const TBT_U4_PORT *p;

    for (k = 0; k < 2; k++) {
        fabInit(&g_fab);
        if (adl[k]) {
            fabHostAdl(&g_fab);
        } else {
            fabHostMinimal(&g_fab);
        }
        dev = fabDevice(&g_fab, FAB_DEV_USB4);
        fabPlug(&g_fab, 1, dev);
        fabPreos(&g_fab, 1, 1);
        g_fab.RxQCount = 0;     /* the pre-OS CM consumed its own events */
        g_fab.R[0].Adp[fabPortLane0(&g_fab, 1)].Cs[5] |= 0x80000000UL;
        cfgDefault(&c);
        g_ldCalls = 0;
        (void)start(&g_fab, &g_ring, &g_cm, &c);
        p = TbtU4Port(&g_cm, 1);
        CHECK_EQ(state(&g_cm, 1), TBT_U4_UP, adl[k] ? "intel-adl pre-OS: UP" : "minimal pre-OS: UP");
        CHECK(p->Pcie.Preserved && p->Pcie.Hd == 8 && p->Pcie.Hu == 9, "PCIe preserved at 8 and 9");
        CHECK(p->Usb3.Preserved && p->Usb3.Hd == 10 && p->Usb3.Hu == 11, "USB3 preserved");
        CHECK_EQ(pathWrites(&g_fab), 0, "no path entry or Path Enable written");
        CHECK_EQ(g_cm.Counters.PortsPreserved, 1, "counted");
        CHECK_EQ(countActions(&g_cm, TBT_U4_ACT_TUNNEL_UP), 0, "nothing re-enumerated");
        CHECK(fabPcieUp(&g_fab, 1), "the fabric's tunnel still up");
        CHECK(!p->HotPlugEnabled, "hot plug left as found on a preserved port");
        CHECK_EQ(fabFindWrite(&g_fab, 0, dev, TBT_CS_ROUTER, 1, 0) == -1 &&
                 fabFindWrite(&g_fab, 0, dev, TBT_CS_ROUTER, 3, 0) == -1, 1,
                 "the device's DW 1 to 3 never written (Guide 3.2.1 note)");
        poll(&g_cm, &g_fab, 2);
        CHECK_EQ(state(&g_cm, 1), TBT_U4_UP, "and stays UP across idle ticks");
        /* Its unplug: the discovered tunnel's host side torn down. */
        fabUnplug(&g_fab, 1);
        poll(&g_cm, &g_fab, 1);
        CHECK_EQ(state(&g_cm, 1), TBT_U4_EMPTY, "preserved, then unplugged: EMPTY");
        CHECK(hostClean(&g_fab, 1), "host side clean");
        CHECK_EQ(g_ldCalls, 1, "the statement's upcall for the preserved tunnel");
    }

    /* A router a predecessor enumerated with no tunnel: adopted, then
     * connected normally. */
    fabInit(&g_fab);
    fabHostMinimal(&g_fab);
    dev = fabDevice(&g_fab, FAB_DEV_USB4);
    fabPlug(&g_fab, 1, dev);
    fabPreos(&g_fab, 1, 0);
    memset(g_fab.R[0].Adp[5].Path + 16, 0, 8);
    memset(g_fab.R[0].Adp[1].Path + 16, 0, 8 * 4);
    memset(g_fab.R[dev].Adp[1].Path + 16, 0, 8 * 4);
    memset(g_fab.R[dev].Adp[5].Path + 16, 0, 8);
    g_fab.R[0].Adp[5].Cs[FAB_CAP_ADP] &= ~0x80000000UL;
    g_fab.R[dev].Adp[5].Cs[FAB_CAP_ADP] &= ~0x80000000UL;
    g_fab.RxQCount = 0;
    cfgDefault(&c);
    (void)start(&g_fab, &g_ring, &g_cm, &c);
    p = TbtU4Port(&g_cm, 1);
    CHECK_EQ(state(&g_cm, 1), TBT_U4_UP, "no tunnel state: connected normally");
    CHECK(p->Dev.Preserved && !p->Dev.EnumeratedHere, "the router adopted, not re-enumerated");
    CHECK(p->Pcie.Up && !p->Pcie.Preserved, "the tunnel built here");

    /* Half-built: the host's PCIe Down entry valid, the device's missing. */
    fabInit(&g_fab);
    fabHostMinimal(&g_fab);
    dev = fabDevice(&g_fab, FAB_DEV_USB4);
    fabPlug(&g_fab, 1, dev);
    fabPreos(&g_fab, 1, 0);
    memset(g_fab.R[dev].Adp[5].Path + 16, 0, 8);
    g_fab.RxQCount = 0;
    (void)start(&g_fab, &g_ring, &g_cm, &c);
    CHECK_EQ(state(&g_cm, 1), TBT_U4_HELD, "half-built: HELD");
    CHECK_EQ(pathWrites(&g_fab), 0, "left exactly as found");
    CHECK_EQ(g_cm.Counters.PortsHeld, 1, "counted");
    poll(&g_cm, &g_fab, 2);
    CHECK_EQ(state(&g_cm, 1), TBT_U4_HELD, "and stays HELD");
    fabUnplug(&g_fab, 1);
    poll(&g_cm, &g_fab, 1);
    CHECK_EQ(state(&g_cm, 1), TBT_U4_EMPTY, "its unplug: EMPTY");
    CHECK(hostClean(&g_fab, 1), "its host side torn down");

    /* XhciCmPreOsCleanup 1: the HELD port cleaned, then connected. */
    fabInit(&g_fab);
    fabHostMinimal(&g_fab);
    dev = fabDevice(&g_fab, FAB_DEV_USB4);
    fabPlug(&g_fab, 1, dev);
    fabPreos(&g_fab, 1, 0);
    memset(g_fab.R[dev].Adp[5].Path + 16, 0, 8);
    g_fab.RxQCount = 0;
    cfgDefault(&c);
    c.PreOsCleanup = 1;
    (void)start(&g_fab, &g_ring, &g_cm, &c);
    poll(&g_cm, &g_fab, 2);
    CHECK_EQ(g_cm.Counters.PortsCleaned, 1, "cleanup 1: the HELD port cleaned");
    CHECK_EQ(g_cm.Counters.DownstreamPortResets, 1, "with a Downstream Port Reset");
    CHECK_EQ(state(&g_cm, 1), TBT_U4_UP, "and connected afresh");
    CHECK(TbtU4Port(&g_cm, 1)->Dev.EnumeratedHere, "the router enumerated here");

    /* XhciCmPreOsCleanup 2: a verified tunnel cleaned too. */
    fabInit(&g_fab);
    fabHostMinimal(&g_fab);
    dev = fabDevice(&g_fab, FAB_DEV_USB4);
    fabPlug(&g_fab, 1, dev);
    fabPreos(&g_fab, 1, 1);
    g_fab.RxQCount = 0;
    cfgDefault(&c);
    c.PreOsCleanup = 2;
    g_ldCalls = 0;
    (void)start(&g_fab, &g_ring, &g_cm, &c);
    poll(&g_cm, &g_fab, 2);
    CHECK_EQ(g_cm.Counters.PortsCleaned, 1, "cleanup 2: the preserved port cleaned");
    CHECK_EQ(g_ldCalls, 1, "its endpoint's statement path ran");
    CHECK_EQ(state(&g_cm, 1), TBT_U4_UP, "and rebuilt");
    CHECK(TbtU4Port(&g_cm, 1)->Pcie.Up && !TbtU4Port(&g_cm, 1)->Pcie.Preserved, "built here now");
    CHECK(countActions(&g_cm, TBT_U4_ACT_TUNNEL_DOWN) == 1, "the removal queued");

    /* XhciCmPreOsCleanup 2 on a version 2 host interface: one Host Router
     * Reset for the domain in place of the port's reset. */
    fabInit(&g_fab);
    fabHostMinimal(&g_fab);
    g_fab.Caps = 0x0040000CUL;
    dev = fabDevice(&g_fab, FAB_DEV_USB4);
    fabPlug(&g_fab, 1, dev);
    fabPreos(&g_fab, 1, 1);
    g_fab.RxQCount = 0;
    cfgDefault(&c);
    c.PreOsCleanup = 2;
    c.HostInterfaceCaps = g_fab.Caps;
    (void)start(&g_fab, &g_ring, &g_cm, &c);
    CHECK_EQ(g_fab.HrResets, 1, "version 2: one Host Router Reset");
    CHECK_EQ(g_cm.Counters.DownstreamPortResets, 0, "no Downstream Port Reset");
    CHECK_EQ(g_cm.Counters.HostRouterResets, 1, "counted");
    /* The model's HRR leaves the routers as they were; the host router is
     * enumerated afresh and the port connected again. */
    fabUnplug(&g_fab, 1);
    poll(&g_cm, &g_fab, 1);
    fabPlug(&g_fab, 1, dev);
    poll(&g_cm, &g_fab, 2);
    CHECK_EQ(state(&g_cm, 1), TBT_U4_UP, "after the reset: UP");

    /* A disconnected port with a predecessor's residual host paths: torn
     * down as an unplug's. */
    fabInit(&g_fab);
    fabHostMinimal(&g_fab);
    dev = fabDevice(&g_fab, FAB_DEV_USB4);
    fabPlug(&g_fab, 1, dev);
    fabPreos(&g_fab, 1, 0);
    fabUnplug(&g_fab, 1);
    g_fab.R[0].Adp[1].Path[2 * 9] |= 0x80000000UL;
    g_fab.R[0].Adp[5].Path[2 * 8] |= 0x80000000UL;
    g_fab.RxQCount = 0;
    cfgDefault(&c);
    (void)start(&g_fab, &g_ring, &g_cm, &c);
    CHECK_EQ(state(&g_cm, 1), TBT_U4_EMPTY, "residual paths on a dark port: EMPTY");
    CHECK(hostClean(&g_fab, 1), "and torn down");
}

/* Only a DisplayPort tunnel through the port: reserved, a PCIe tunnel
 * built on the link as found, no USB3. */
static void test_dp_preserved(void)
{
    TBT_U4_CONFIG c;
    int dev;
    const TBT_U4_PORT *p;

    fabInit(&g_fab);
    fabHostAdl(&g_fab);
    dev = fabDevice(&g_fab, FAB_DEV_USB4);
    fabPlug(&g_fab, 1, dev);
    fabPreos(&g_fab, 1, 0);
    memset(g_fab.R[0].Adp[8].Path + 16, 0, 8);
    memset(g_fab.R[0].Adp[1].Path + 16, 0, 8 * 4);
    memset(g_fab.R[dev].Adp[1].Path + 16, 0, 8 * 4);
    memset(g_fab.R[dev].Adp[5].Path + 16, 0, 8);
    g_fab.R[0].Adp[8].Cs[FAB_CAP_ADP] &= ~0x80000000UL;
    g_fab.R[dev].Adp[5].Cs[FAB_CAP_ADP] &= ~0x80000000UL;
    /* A whole DP tunnel (Guide 5.4.3.3 Table 5-3; Linux tunnel.c
     * tb_tunnel_alloc_dp): the main link from DP IN 5's HopID 9 out of
     * lane 1 at 12 to DP OUT 8's HopID 9; AUX from DP IN's HopID 8 at 13 to
     * DP OUT's HopID 8; AUX back from DP OUT's HopID 8 at 14 to DP IN's
     * HopID 8. */
    fabAddDpOut(&g_fab, dev, 8);
    g_fab.R[0].Adp[5].Path[2 * 9] = 0x80000000UL | 12UL | (1UL << 11);
    g_fab.R[0].Adp[5].Path[2 * 8] = 0x80000000UL | 13UL | (1UL << 11);
    g_fab.R[dev].Adp[1].Path[2 * 12] = 0x80000000UL | 9UL | (8UL << 11);
    g_fab.R[dev].Adp[1].Path[2 * 13] = 0x80000000UL | 8UL | (8UL << 11);
    g_fab.R[dev].Adp[8].Path[2 * 8] = 0x80000000UL | 14UL | (1UL << 11);
    g_fab.R[0].Adp[1].Path[2 * 14] = 0x80000000UL | 8UL | (5UL << 11);
    /* hot plug disabled on the port as the pre-OS manager left it */
    g_fab.R[0].Adp[1].Cs[5] |= TBT_ADP5_DHP;
    g_fab.RxQCount = 0;
    cfgDefault(&c);
    (void)start(&g_fab, &g_ring, &g_cm, &c);
    p = TbtU4Port(&g_cm, 1);
    CHECK(p->DpPreserved, "the DisplayPort tunnel noted");
    CHECK_EQ(state(&g_cm, 1), TBT_U4_UP, "PCIe built beside it: UP");
    CHECK(!p->Usb3.Up, "no USB3 beside DisplayPort");
    CHECK(g_fab.R[0].Adp[5].Path[2 * 9] & 0x80000000UL, "the main path untouched");
    CHECK(g_fab.R[0].Adp[5].Path[2 * 8] & 0x80000000UL, "the AUX path untouched");
    CHECK(g_fab.R[dev].Adp[1].Path[2 * 12] & 0x80000000UL, "the device ends untouched");
    CHECK(g_fab.R[dev].Adp[1].Path[2 * 13] & 0x80000000UL, "both of them");
    CHECK(g_fab.R[dev].Adp[8].Path[2 * 8] & 0x80000000UL, "the AUX return untouched");
    CHECK(g_fab.R[0].Adp[1].Path[2 * 14] & 0x80000000UL, "and its host end");
    CHECK(p->Pcie.Hd != 12 && p->Pcie.Hd != 13 && p->Pcie.Hu != 14,
          "PCIe around every DP HopID on the lane");
    CHECK(g_fab.R[0].Adp[1].Cs[5] & TBT_ADP5_DHP, "hot plug left as found (section 6.1)");

    /* Unplugged and re-plugged with the DP paths still there: no bonding
     * over them; they are left alone. */
    fabUnplug(&g_fab, 1);
    poll(&g_cm, &g_fab, 1);
    fabPlug(&g_fab, 1, dev);
    poll(&g_cm, &g_fab, 2);
    CHECK(state(&g_cm, 1) != TBT_U4_UP, "re-plug deferred around the kept paths");
    CHECK(g_fab.R[0].Adp[5].Path[2 * 9] & 0x80000000UL, "the kept DP paths untouched");

    /* The same with PCIe Tunneling On clear on the device: no PCIe tunnel
     * built beside it, HELD (no handshake runs on a preserved router). */
    fabInit(&g_fab);
    fabHostAdl(&g_fab);
    dev = fabDevice(&g_fab, FAB_DEV_USB4);
    fabPlug(&g_fab, 1, dev);
    fabPreos(&g_fab, 1, 0);
    memset(g_fab.R[0].Adp[8].Path + 16, 0, 8);
    memset(g_fab.R[0].Adp[1].Path + 16, 0, 8 * 4);
    memset(g_fab.R[dev].Adp[1].Path + 16, 0, 8 * 4);
    memset(g_fab.R[dev].Adp[5].Path + 16, 0, 8);
    g_fab.R[0].Adp[8].Cs[FAB_CAP_ADP] &= ~0x80000000UL;
    g_fab.R[dev].Adp[5].Cs[FAB_CAP_ADP] &= ~0x80000000UL;
    g_fab.R[0].Adp[5].Path[2 * 8] = 0x80000000UL | 12UL | (1UL << 11);
    g_fab.R[dev].Rcs[5] &= ~TBT_RTR5_PTO;
    g_fab.RxQCount = 0;
    (void)start(&g_fab, &g_ring, &g_cm, &c);
    p = TbtU4Port(&g_cm, 1);
    CHECK_EQ(state(&g_cm, 1), TBT_U4_HELD, "PTO clear beside DP: HELD");
    CHECK(!p->Pcie.Up && !fabPcieUp(&g_fab, 1), "no PCIe tunnel built");

    /* DeferFirstPoll: the PCIe tunnel beside a preserved DP tunnel is not
     * built in the start, and the first poll builds it as found. */
    fabInit(&g_fab);
    fabHostAdl(&g_fab);
    dev = fabDevice(&g_fab, FAB_DEV_USB4);
    fabPlug(&g_fab, 1, dev);
    fabPreos(&g_fab, 1, 0);
    memset(g_fab.R[0].Adp[8].Path + 16, 0, 8);
    memset(g_fab.R[0].Adp[1].Path + 16, 0, 8 * 4);
    memset(g_fab.R[dev].Adp[1].Path + 16, 0, 8 * 4);
    memset(g_fab.R[dev].Adp[5].Path + 16, 0, 8);
    g_fab.R[0].Adp[8].Cs[FAB_CAP_ADP] &= ~0x80000000UL;
    g_fab.R[dev].Adp[5].Cs[FAB_CAP_ADP] &= ~0x80000000UL;
    fabAddDpOut(&g_fab, dev, 8);
    g_fab.R[0].Adp[5].Path[2 * 9] = 0x80000000UL | 12UL | (1UL << 11);
    g_fab.R[0].Adp[5].Path[2 * 8] = 0x80000000UL | 13UL | (1UL << 11);
    g_fab.R[dev].Adp[1].Path[2 * 12] = 0x80000000UL | 9UL | (8UL << 11);
    g_fab.R[dev].Adp[1].Path[2 * 13] = 0x80000000UL | 8UL | (8UL << 11);
    g_fab.R[dev].Adp[8].Path[2 * 8] = 0x80000000UL | 14UL | (1UL << 11);
    g_fab.R[0].Adp[1].Path[2 * 14] = 0x80000000UL | 8UL | (5UL << 11);
    g_fab.R[0].Adp[1].Cs[5] |= TBT_ADP5_DHP;
    g_fab.RxQCount = 0;
    cfgDefault(&c);
    c.DeferFirstPoll = TRUE;
    (void)start(&g_fab, &g_ring, &g_cm, &c);
    p = TbtU4Port(&g_cm, 1);
    CHECK(p->DpPreserved && p->AsFoundOwed, "deferred: the DP tunnel noted, its connect owed");
    CHECK(!p->Pcie.Up && !fabPcieUp(&g_fab, 1), "deferred: no PCIe built in the start");
    poll(&g_cm, &g_fab, 1);
    CHECK_EQ(state(&g_cm, 1), TBT_U4_UP, "deferred: the first poll builds it: UP");
    CHECK(!p->AsFoundOwed, "deferred: the owed connect made");
    CHECK(g_fab.R[0].Adp[5].Path[2 * 9] & 0x80000000UL, "deferred: the DP main path untouched");

    /* Deferred and XhciCmPreOsCleanup 2 on a version 1 host: the cleaning
     * removes the preserved configuration, so no as-found connect is owed
     * and the port connects normally. */
    fabInit(&g_fab);
    fabHostAdl(&g_fab);
    dev = fabDevice(&g_fab, FAB_DEV_USB4);
    fabPlug(&g_fab, 1, dev);
    fabPreos(&g_fab, 1, 0);
    memset(g_fab.R[0].Adp[8].Path + 16, 0, 8);
    memset(g_fab.R[0].Adp[1].Path + 16, 0, 8 * 4);
    memset(g_fab.R[dev].Adp[1].Path + 16, 0, 8 * 4);
    memset(g_fab.R[dev].Adp[5].Path + 16, 0, 8);
    g_fab.R[0].Adp[8].Cs[FAB_CAP_ADP] &= ~0x80000000UL;
    g_fab.R[dev].Adp[5].Cs[FAB_CAP_ADP] &= ~0x80000000UL;
    fabAddDpOut(&g_fab, dev, 8);
    g_fab.R[0].Adp[5].Path[2 * 9] = 0x80000000UL | 12UL | (1UL << 11);
    g_fab.R[0].Adp[5].Path[2 * 8] = 0x80000000UL | 13UL | (1UL << 11);
    g_fab.R[dev].Adp[1].Path[2 * 12] = 0x80000000UL | 9UL | (8UL << 11);
    g_fab.R[dev].Adp[1].Path[2 * 13] = 0x80000000UL | 8UL | (8UL << 11);
    g_fab.R[dev].Adp[8].Path[2 * 8] = 0x80000000UL | 14UL | (1UL << 11);
    g_fab.R[0].Adp[1].Path[2 * 14] = 0x80000000UL | 8UL | (5UL << 11);
    g_fab.RxQCount = 0;
    cfgDefault(&c);
    c.DeferFirstPoll = TRUE;
    c.PreOsCleanup = 2;
    (void)start(&g_fab, &g_ring, &g_cm, &c);
    p = TbtU4Port(&g_cm, 1);
    CHECK_EQ(g_cm.Counters.PortsCleaned, 1, "deferred, cleanup 2: the DP port cleaned");
    CHECK(!p->AsFoundOwed, "deferred, cleanup 2: no as-found connect owed");
    CHECK(!p->DpPreserved, "deferred, cleanup 2: nothing preserved any more");
    poll(&g_cm, &g_fab, 3);
    CHECK(!p->AsFoundOwed && p->State != TBT_U4_UP,
          "deferred, cleanup 2: no as-found connect made (the normal path, which "
          "waits while the kept DP paths hold the lane, as without the deferral)");
}

/* ------------------------------------------------------------------ */
/* The statement (record 22 sections 7.3, 10)                          */
/* ------------------------------------------------------------------ */

static void test_statement(void)
{
    int dev;
    int lane;
    ULONG gen;
    const TBT_U4_PORT *p;

    /* A prerequisite failing (Pending Packets stuck on the host lane's
     * upstream entry): the path is not proven closed, the entry
     * quarantined, and retried each tick until it drains. */
    dev = setupMinimal(1, NULL);
    p = TbtU4Port(&g_cm, 1);
    lane = fabPortLane0(&g_fab, 1);
    g_fab.R[0].Adp[lane].PpStuck[p->Pcie.Hu] = 1;
    g_ldAnswer = TBT_U4_STMT_SENT;
    fabUnplug(&g_fab, 1);
    poll(&g_cm, &g_fab, 1);
    CHECK_EQ(g_ldCalls, 1, "the upcall ran");
    CHECK(!g_ld.PathClosed, "path not proven closed: the statement's prerequisite failed");
    CHECK_EQ(quarantineCount(&g_cm), 1, "the entry quarantined");
    CHECK_EQ(g_cm.Counters.PendingTimeouts, 1, "Pending Packets timeout counted");
    g_fab.R[0].Adp[lane].PpStuck[9] = 0;
    g_fab.R[0].Adp[lane].Path[2 * 9 + 1] &= ~0x10000000UL;
    poll(&g_cm, &g_fab, 1);
    CHECK_EQ(quarantineCount(&g_cm), 0, "drained: the quarantine lifted");
    CHECK_EQ(g_cm.Counters.QuarantineLifted, 1, "counted");
    /* A quarantined HopID is never reused meanwhile. */
    g_fab.R[0].Adp[lane].PpStuck[9] = 1;
    fabPlug(&g_fab, 1, dev);
    poll(&g_cm, &g_fab, 2);
    fabUnplug(&g_fab, 1);
    poll(&g_cm, &g_fab, 1);
    fabPlug(&g_fab, 1, dev);
    poll(&g_cm, &g_fab, 2);
    CHECK(!p->Pcie.Up && g_cm.Counters.ConnectsDeferred > 0,
          "no bonding while an entry through the port is still pending (Guide 7.1)");
    g_fab.R[0].Adp[lane].PpStuck[9] = 0;
    g_fab.R[0].Adp[lane].Path[2 * 9 + 1] &= ~0x10000000UL;
    poll(&g_cm, &g_fab, 3);
    CHECK(p->Pcie.Up, "connected once it drained");

    /* A later step failing (the USB3 bandwidth release): the statement's
     * prerequisites still hold. */
    dev = setupMinimal(1, NULL);
    g_fab.R[0].Adp[7].HcaStuck = 1;
    fabUnplug(&g_fab, 1);
    poll(&g_cm, &g_fab, 1);
    CHECK(g_ld.PathClosed, "a later step's failure does not reach back");

    /* The anchor: the last matching sample's issue time; a sample that
     * arrives late shrinks the allowance. */
    dev = setupMinimal(1, NULL);
    p = TbtU4Port(&g_cm, 1);
    gen = p->Generation;
    TbtU4PresenceSample(&g_cm, 1, gen, g_fab.NowUs, TRUE);
    g_fab.Host.Sleep(g_fab.Host.Context, 2500);
    fabUnplug(&g_fab, 1);
    poll(&g_cm, &g_fab, 1);
    CHECK(g_ld.AnchorValid, "an anchor");
    CHECK(g_ld.RemainingUs < 500000UL, "2.5 s old: less than 0.5 s left");
    dev = setupMinimal(1, NULL);
    p = TbtU4Port(&g_cm, 1);
    TbtU4PresenceSample(&g_cm, 1, p->Generation, g_fab.NowUs, FALSE);
    fabUnplug(&g_fab, 1);
    poll(&g_cm, &g_fab, 1);
    CHECK(!g_ld.AnchorValid && g_ld.RemainingUs == 0, "no matching sample: no anchor");
    CHECK(g_cm.Counters.StatementsWithheld >= 1, "withheld counted");
}

/* ------------------------------------------------------------------ */
/* Capabilities of a preserved router (record 22 section 6.4)          */
/* ------------------------------------------------------------------ */

static void preserveWith(int mode, unsigned long capsData)
{
    TBT_U4_CONFIG c;
    int dev;

    fabInit(&g_fab);
    fabHostMinimal(&g_fab);
    dev = fabDevice(&g_fab, FAB_DEV_USB4);
    g_fab.R[dev].RopMode[FAB_OP_CAPS] = mode;
    g_fab.R[dev].CapsData = capsData;
    fabPlug(&g_fab, 1, dev);
    fabPreos(&g_fab, 1, 0);
    g_fab.RxQCount = 0;
    cfgDefault(&c);
    (void)start(&g_fab, &g_ring, &g_cm, &c);
    /* the device reports a link error */
    {
        unsigned long dw[3];
        dw[0] = 0x80000000UL;
        dw[1] = (unsigned long)fabPortLane0(&g_fab, 1);
        dw[2] = TBT_EV_ERR_LINK | (1UL << 8) | TBT_NOTIFY_SQ;
        g_fab.AcksPlain = 0;
        g_fab.AcksEnh = 0;
        fabInject(&g_fab, TBT_PDF_NOTIFY, dw, 3);
    }
    poll(&g_cm, &g_fab, 1);
}

static void test_caps_preserved(void)
{
    const TBT_U4_PORT *p;

    /* Sequence bit enabled (capability 2: bits 4 and 5). */
    preserveWith(FAB_ROP_OK, (1UL << 4) | (1UL << 5));
    p = TbtU4Port(&g_cm, 1);
    CHECK_EQ(p->Dev.CapsState, TBT_U4_CAPS_KNOWN, "answered: adopted");
    CHECK_EQ(g_fab.AcksEnh, 1, "enhanced acknowledgement for that router");
    CHECK_EQ(g_fab.AcksPlain, 0, "not the plain one");

    preserveWith(FAB_ROP_NS, 0);
    p = TbtU4Port(&g_cm, 1);
    CHECK_EQ(p->Dev.CapsState, TBT_U4_CAPS_DEFAULT, "not supported: none enabled");
    CHECK_EQ(g_fab.AcksPlain, 1, "the plain acknowledgement");

    preserveWith(FAB_ROP_FAIL, 0);
    p = TbtU4Port(&g_cm, 1);
    CHECK_EQ(p->Dev.CapsState, TBT_U4_CAPS_UNKNOWN, "failing: unknown");
    CHECK_EQ(g_fab.AcksPlain + g_fab.AcksEnh, 0, "no acknowledgement of either form");
    CHECK(!p->Dev.BaValid, "no Buffer Allocation Request");

    preserveWith(FAB_ROP_HANG, 0);
    p = TbtU4Port(&g_cm, 1);
    CHECK_EQ(p->Dev.CapsState, TBT_U4_CAPS_UNKNOWN, "never completing: unknown");
    CHECK(p->Dev.OpBlocked, "and the router operation-blocked");
    CHECK_EQ(g_fab.AcksPlain + g_fab.AcksEnh, 0, "no acknowledgement");
}

/* ------------------------------------------------------------------ */
/* Inert after activation, two domains                                 */
/* ------------------------------------------------------------------ */

static void test_inert_and_domains(void)
{
    TBT_U4_CONFIG c;
    LONG rc;
    int d1;
    int d2;

    /* The host router never answers: rings stopped, the fabric as it was. */
    fabInit(&g_fab);
    fabHostMinimal(&g_fab);
    g_fab.Stopped = 1;
    cfgDefault(&c);
    rc = start(&g_fab, &g_ring, &g_cm, &c);
    CHECK(rc != TBT_OK, "host router unanswered: the start fails");
    CHECK(g_cm.Failed, "inert after activation");
    CHECK_EQ(g_cm.InertReason, TBT_INERT_HOST_ROUTER, "for that reason");
    CHECK_EQ(g_fab.TxCtrl | g_fab.RxCtrl, 0, "both rings stopped before returning");
    CHECK_EQ(g_fab.Writes, 0, "nothing written to the fabric");
    TbtU4Poll(&g_cm);
    CHECK_EQ(g_fab.TxCtrl, 0, "and the poll does nothing");

    /* A Thunderbolt 3 host router (USB4 Version 0): refused. */
    fabInit(&g_fab);
    fabHostMinimal(&g_fab);
    g_fab.R[0].Rcs[4] &= 0x00FFFFFFUL;
    rc = start(&g_fab, &g_ring, &g_cm, &c);
    CHECK(rc != TBT_OK && g_cm.InertReason == TBT_INERT_ROUTER_VERSION,
          "host router version 0: inert");
    fabInit(&g_fab);
    fabHostMinimal(&g_fab);
    g_fab.R[0].Rcs[4] = (g_fab.R[0].Rcs[4] & 0x00FFFFFFUL) | 0x30000000UL;
    rc = start(&g_fab, &g_ring, &g_cm, &c);
    CHECK(rc != TBT_OK && g_cm.InertReason == TBT_INERT_ROUTER_VERSION,
          "a reserved host router version: inert");

    /* Two domains at once, each its own fabric, ring and instance. */
    fabInit(&g_fab);
    fabHostMinimal(&g_fab);
    d1 = fabDevice(&g_fab, FAB_DEV_USB4);
    fabPlug(&g_fab, 1, d1);
    fabInit(&g_fab2);
    fabHostAdl(&g_fab2);
    d2 = fabDevice(&g_fab2, FAB_DEV_USB4);
    fabPlug(&g_fab2, 2, d2);
    CHECK_EQ(start(&g_fab, &g_ring, &g_cm, &c), TBT_OK, "domain 1 starts");
    CHECK_EQ(start(&g_fab2, &g_ring2, &g_cm2, &c), TBT_OK, "domain 2 starts");
    CHECK_EQ(state(&g_cm, 1), TBT_U4_UP, "domain 1 port 1 UP");
    CHECK_EQ(state(&g_cm2, 2), TBT_U4_UP, "domain 2 port 2 UP");
    fabUnplug(&g_fab2, 2);
    poll(&g_cm, &g_fab, 1);
    CHECK_EQ(state(&g_cm2, 2), TBT_U4_UP, "domain 2 not polled yet");
    poll(&g_cm2, &g_fab2, 1);
    CHECK_EQ(state(&g_cm2, 2), TBT_U4_EMPTY, "domain 2's unplug");
    CHECK_EQ(state(&g_cm, 1), TBT_U4_UP, "domain 1 untouched");
}

/* ------------------------------------------------------------------ */
/* intel-adl, Thunderbolt 3, links below depth 1                       */
/* ------------------------------------------------------------------ */

static void test_adl(void)
{
    TBT_U4_CONFIG c;
    int dev;
    const TBT_U4_PORT *p;
    unsigned long bw;

    fabInit(&g_fab);
    fabHostAdl(&g_fab);
    dev = fabDevice(&g_fab, FAB_DEV_USB4);
    fabPlug(&g_fab, 1, dev);
    cfgDefault(&c);
    (void)start(&g_fab, &g_ring, &g_cm, &c);
    poll(&g_cm, &g_fab, 2);
    CHECK_EQ(g_cm.Ports[0].PcieDown, 8, "intel-adl: PCIe Down 8 paired with port 1");
    CHECK_EQ(g_cm.Ports[1].PcieDown, 9, "and 9 with port 2");
    CHECK_EQ(g_cm.Ports[0].Usb3Down, 12, "USB3 Down 12");
    CHECK(!g_cm.HostRouter.Adp[10].Present && !g_cm.HostRouter.Adp[11].Present,
          "10 and 11 not present (ERR_ADDR)");
    CHECK(!g_cm.Ports[0].MapValid, "the mapping operation not supported: the pairing rule");
    CHECK(g_cm.HostRouter.BaValid && g_cm.HostRouter.BaMaxPcie == 28, "BA answered");
    CHECK(g_cm.HostRouter.Quirks & TBT_U4_QUIRK_USB3_MAX_BW, "the USB3 quirk on 8087:463E");
    CHECK(g_cm.Counters.HotPlugOther >= 2, "DP IN plug events counted");
    CHECK(g_fab.HpAcks >= 2, "and acknowledged");
    p = TbtU4Port(&g_cm, 1);
    CHECK_EQ(state(&g_cm, 1), TBT_U4_UP, "UP");
    CHECK_EQ((fabPath0(&g_fab, 0, 1, p->Pcie.Hu) >> 17) & 0x7F, 28, "the host's baMaxPCIe");
    /* The quirk: 16376 * 0.9 = 14738 Mb/s, in 512 B units of 125 us at
     * the smallest scale that fits 12 bits. */
    bw = fabAdp(&g_fab, 0, 12, FAB_CAP_ADP + 2) & 0xFFFUL;
    CHECK(bw > 0, "a bandwidth allocated");
    CHECK_EQ(fabAdp(&g_fab, 0, 12, FAB_CAP_ADP + 3), 0, "scale 0 (Linux's choice for 14.7 Gb/s)");
    CHECK_EQ(bw, (14738UL * 125UL + 511UL) / 512UL, "14738 Mb/s, the clamped 90 per cent");
}

static void test_tbt3(void)
{
    TBT_U4_CONFIG c;
    int dev;
    const TBT_U4_PORT *p;
    unsigned long sx;

    fabInit(&g_fab);
    fabHostMinimal(&g_fab);
    dev = fabDevice(&g_fab, FAB_DEV_TBT3_TR);
    fabPlug(&g_fab, 1, dev);
    cfgDefault(&c);
    (void)start(&g_fab, &g_ring, &g_cm, &c);
    p = TbtU4Port(&g_cm, 1);
    CHECK_EQ(state(&g_cm, 1), TBT_U4_UP, "Thunderbolt 3 Titan Ridge DD: UP");
    CHECK(p->Dev.Tbt3, "run as a Thunderbolt 3 router");
    CHECK(p->Pcie.Up && !p->Usb3.Up, "PCIe, no USB3");
    CHECK_EQ(fabRtr(&g_fab, dev, FAB_RCAP_PLUG + 1) & 0x7CUL, 0x04UL,
             "plug events active, USB disable set (not Alpine Ridge)");
    CHECK_EQ(fabRtr(&g_fab, dev, 5), 0, "no ROUTER_CS_5 handshake");
    CHECK_EQ(fabRtr(&g_fab, dev, 4) & 0xFF00UL, 0, "no CMUV written");
    sx = fabRtr(&g_fab, dev, FAB_RCAP_LC + FAB_LC_START + 0x96);
    CHECK_EQ(sx & 0x40010000UL, 0x40010000UL, "LC: L1C and UPSTREAM (odd adapter 1)");
    CHECK_EQ((fabPath0(&g_fab, dev, 3, 8) >> 17) & 0x7F, 7, "TB3 protocol ingress: 7 credits");
    CHECK(fabPath1(&g_fab, dev, 3, 8) & 0x01000000UL, "with IFC");
    CHECK(p->Bonded, "bonded through the link controller's BE");
    CHECK(p->Dev.Uuid[0] == 0xAABB0000UL + (unsigned long)dev, "UUID from the LC fuse");
    CHECK_EQ(p->Dev.Quirks, TBT_U4_QUIRK_NO_CLX, "the CLx quirk noted");
    CHECK(g_cm.Counters.Tbt3Routers == 1, "counted");

    /* Alpine Ridge keeps its USB plug event. */
    fabInit(&g_fab);
    fabHostMinimal(&g_fab);
    dev = fabDevice(&g_fab, FAB_DEV_TBT3_AR);
    fabPlug(&g_fab, 1, dev);
    (void)start(&g_fab, &g_ring, &g_cm, &c);
    CHECK_EQ(state(&g_cm, 1), TBT_U4_UP, "Alpine Ridge: UP");
    CHECK_EQ(fabRtr(&g_fab, dev, FAB_RCAP_PLUG + 1) & 0x7CUL, 0, "USB disable left clear");

    /* No Plug Events capability: refused. */
    fabInit(&g_fab);
    fabHostMinimal(&g_fab);
    dev = fabDevice(&g_fab, FAB_DEV_TBT3_TR);
    g_fab.R[dev].Rcs[1] &= ~0xFFUL;
    fabPlug(&g_fab, 1, dev);
    (void)start(&g_fab, &g_ring, &g_cm, &c);
    CHECK_EQ(state(&g_cm, 1), TBT_U4_UNSUPPORTED, "no Plug Events: UNSUPPORTED");
    fabUnplug(&g_fab, 1);
    poll(&g_cm, &g_fab, 1);
    CHECK_EQ(state(&g_cm, 1), TBT_U4_EMPTY, "its unplug: EMPTY");
}

static void test_links(void)
{
    TBT_U4_CONFIG c;
    int dev;
    int child;

    fabInit(&g_fab);
    fabHostMinimal(&g_fab);
    dev = fabDevice(&g_fab, FAB_DEV_USB4);
    child = fabChild(&g_fab, dev);
    fabPlug(&g_fab, 1, dev);
    fabPreos(&g_fab, 1, 0);
    fabPlugChild(&g_fab, dev, child, 1);
    /* the depth-1 router's own PCIe Down feeds the child */
    g_fab.R[dev].Adp[7].Path[2 * 8] = 0x80000000UL | 8UL | (3UL << 11) | (7UL << 17);
    g_fab.R[dev].Adp[7].Cs[FAB_CAP_ADP] |= 0x80000000UL;
    g_fab.R[dev].Adp[3].Path[2 * 9] = 0x80000000UL | 8UL | (7UL << 11) | (16UL << 17);
    g_fab.RxQCount = 0;
    cfgDefault(&c);
    (void)start(&g_fab, &g_ring, &g_cm, &c);
    CHECK_EQ(state(&g_cm, 1), TBT_U4_UP, "preserved with a router below");
    CHECK_EQ(g_cm.Counters.LinksDiscovered, 1, "the depth-2 link discovered");
    (void)countActions(&g_cm, 0);
    fabUnplugChild(&g_fab, dev);
    poll(&g_cm, &g_fab, 1);
    CHECK_EQ(g_cm.Counters.LinksRemoved, 1, "its removal noticed");
    CHECK_EQ(g_fab.R[dev].Adp[7].Path[2 * 8] & 0x80000000UL, 0, "the parent's side invalidated");
    CHECK_EQ(g_fab.R[dev].Adp[3].Path[2 * 9] & 0x80000000UL, 0, "both directions");
    CHECK_EQ(g_fab.R[dev].Adp[7].Cs[FAB_CAP_ADP] & 0x80000000UL, 0, "its PCIe Down disabled");
    CHECK_EQ(countActions(&g_cm, TBT_U4_ACT_DEEP_REMOVED), 1, "the OS told");
    CHECK_EQ(state(&g_cm, 1), TBT_U4_UP, "the depth-1 tunnel kept");

    /* A new router at depth 2 is left uninitialised: its event counted. */
    fabPlugChild(&g_fab, dev, child, 0);
    g_fab.R[dev].Adp[3].Cs[4] |= 0x80000000UL;
    {
        unsigned long dw[3];
        dw[0] = 0x80000000UL;
        dw[1] = 1;
        dw[2] = 3;
        fabInject(&g_fab, TBT_PDF_HOTPLUG, dw, 3);
    }
    poll(&g_cm, &g_fab, 2);
    CHECK(g_cm.Counters.HotPlugDownstream >= 1, "a depth-2 plug event counted");
    CHECK_EQ(fabRtr(&g_fab, child, 3) & 0x80000000UL, 0, "and the router left uninitialised");
}

/* ------------------------------------------------------------------ */
/* Loss, eject, suspend and resume, a coarse clock                     */
/* ------------------------------------------------------------------ */

static void test_loss_and_eject(void)
{
    int dev;
    const TBT_U4_PORT *p;

    /* The PCIe tunnel lost with the router still there (an admin-down). */
    dev = setupMinimal(1, NULL);
    p = TbtU4Port(&g_cm, 1);
    (void)countActions(&g_cm, 0);
    g_fab.R[0].Adp[5].Cs[FAB_CAP_ADP] &= ~0x80000000UL;
    poll(&g_cm, &g_fab, 1);
    CHECK_EQ(state(&g_cm, 1), TBT_U4_FAILED, "PCIe lost: FAILED");
    CHECK_EQ(p->Why, TBT_U4_WHY_LOST, "lost");
    CHECK(p->Usb3.Up, "the USB3 tunnel kept");
    CHECK_EQ(countActions(&g_cm, TBT_U4_ACT_TUNNEL_DOWN), 1, "a removal queued");
    CHECK_EQ(g_cm.Counters.PcieLost, 1, "counted");
    CHECK_EQ(fabAdp(&g_fab, dev, 5, FAB_CAP_ADP) & 0x80000000UL, 0, "the device end disabled too");
    poll(&g_cm, &g_fab, 2);
    CHECK_EQ(state(&g_cm, 1), TBT_U4_FAILED, "no rebuild until a re-plug");

    /* L1 and Recovery are a live link; a transitional state is a loss
     * only after 2 s (record 22 section 7.1 item 4). */
    dev = setupMinimal(1, NULL);
    g_fab.R[0].Adp[5].LtssmForce = 1 + 7;      /* L1 */
    poll(&g_cm, &g_fab, 12);
    CHECK_EQ(state(&g_cm, 1), TBT_U4_UP, "L1: still UP");
    g_fab.R[0].Adp[5].LtssmForce = 1 + 4;      /* Recovery */
    poll(&g_cm, &g_fab, 12);
    CHECK_EQ(state(&g_cm, 1), TBT_U4_UP, "Recovery: still UP");
    g_fab.R[0].Adp[5].LtssmForce = 1 + 2;      /* Configuration */
    poll(&g_cm, &g_fab, 5);
    CHECK_EQ(state(&g_cm, 1), TBT_U4_UP, "Configuration under 2 s: still UP");
    poll(&g_cm, &g_fab, 10);
    CHECK_EQ(state(&g_cm, 1), TBT_U4_FAILED, "Configuration for 2 s: lost");
    g_fab.R[0].Adp[5].LtssmForce = 0;
    (void)dev;

    /* Eject: both tunnels down orderly, the port down until a re-plug. */
    dev = setupMinimal(1, NULL);
    CHECK_EQ(TbtU4Eject(&g_cm, 1), TBT_OK, "eject");
    CHECK(!fabPcieUp(&g_fab, 1) && !fabUsb3Up(&g_fab, 1), "both tunnels down");
    CHECK(hostClean(&g_fab, 1), "host side clean");
    CHECK_EQ(state(&g_cm, 1), TBT_U4_FAILED, "FAILED, ejected");
    poll(&g_cm, &g_fab, 2);
    CHECK_EQ(state(&g_cm, 1), TBT_U4_FAILED, "stays down");
    fabUnplug(&g_fab, 1);
    poll(&g_cm, &g_fab, 1);
    fabPlug(&g_fab, 1, dev);
    poll(&g_cm, &g_fab, 2);
    CHECK_EQ(state(&g_cm, 1), TBT_U4_UP, "re-plugged: UP");
    CHECK_EQ(TbtU4Eject(&g_cm, 2), TBT_E_PARAM, "nothing on port 2 to eject");
}

static void test_suspend_resume(void)
{
    int dev;
    LONG rc;
    const TBT_U4_PORT *p;
    ULONG b;

    /* Everything survived: verified. */
    dev = setupMinimal(1, NULL);
    TbtU4Suspend(&g_cm);
    CHECK_EQ(g_fab.TxCtrl, 0, "rings stopped at suspend");
    rc = TbtU4Resume(&g_cm);
    CHECK_EQ(rc, TBT_OK, "resume: every obligation verified");
    CHECK_EQ(state(&g_cm, 1), TBT_U4_UP, "still UP");

    /* The domain lost its configuration: rebuilt before the hold is
     * released. */
    dev = setupMinimal(1, NULL);
    TbtU4Suspend(&g_cm);
    fabUnplug(&g_fab, 1);
    fabPlug(&g_fab, 1, dev);
    g_fab.RxQCount = 0;
    rc = TbtU4Resume(&g_cm);
    p = TbtU4Port(&g_cm, 1);
    CHECK_EQ(rc, TBT_OK, "resume: rebuilt (or torn down on removal evidence)");
    poll(&g_cm, &g_fab, 2);
    CHECK_EQ(state(&g_cm, 1), TBT_U4_UP, "UP after resume");

    /* The router gone: torn down, the obligation discharged. */
    dev = setupMinimal(1, NULL);
    TbtU4Suspend(&g_cm);
    fabUnplug(&g_fab, 1);
    rc = TbtU4Resume(&g_cm);
    CHECK_EQ(rc, TBT_OK, "resume: removal evidence discharges it");
    CHECK_EQ(state(&g_cm, 1), TBT_U4_EMPTY, "EMPTY");

    /* Verification failing with no removal evidence: HELD, failure path. */
    dev = setupMinimal(1, NULL);
    TbtU4Suspend(&g_cm);
    g_fab.R[dev].Adp[1].Path[2 * 8] &= ~0x80000000UL;
    rc = TbtU4Resume(&g_cm);
    CHECK_EQ(rc, TBT_E_STATE, "resume: an obligation unmet");
    CHECK_EQ(state(&g_cm, 1), TBT_U4_HELD, "HELD, untouched");

    b = TbtU4ResumeBoundUs(0, 10000);
    CHECK(b >= 1050000UL + TBT_U4_RESUME_HOST_US && b < 1050000UL + TBT_U4_RESUME_HOST_US + 3000000UL,
          "no obligation: the platform, the host router's budget and the margin");
    CHECK(TbtU4ResumeBoundUs(2, 10000) > TbtU4ResumeBoundUs(1, 10000), "grows per obligation");
    CHECK(TbtU4ResumeBoundUs(1, 55000) > TbtU4ResumeBoundUs(1, 10000), "and with the tick");
    CHECK(TbtU4ResumeBoundUs(1000, 55000) == TbtU4ResumeBoundUs(TBT_U4_OBLIGATIONS, 55000),
          "held to the table");
}

static void test_coarse_clock(void)
{
    TBT_U4_CONFIG c;
    int dev;
    const TBT_U4_PORT *p;

    /* Windows 98's 10 ms tick: every bound measured on the clock; the
     * same outcome. */
    fabInit(&g_fab);
    g_fab.TickUs = 10000;
    g_fab.Host.TickUs = 10000;
    fabHostMinimal(&g_fab);
    dev = fabDevice(&g_fab, FAB_DEV_USB4);
    fabPlug(&g_fab, 1, dev);
    cfgDefault(&c);
    (void)start(&g_fab, &g_ring, &g_cm, &c);
    p = TbtU4Port(&g_cm, 1);
    CHECK_EQ(state(&g_cm, 1), TBT_U4_UP, "a 10 ms clock: UP");
    CHECK(p->Pcie.Up && p->Usb3.Up, "both tunnels");
    fabUnplug(&g_fab, 1);
    poll(&g_cm, &g_fab, 1);
    fabPlug(&g_fab, 1, dev);
    poll(&g_cm, &g_fab, 2);
    CHECK(p->Usb3.Up, "USB3 again after its 500 ms gap");
}

/* ------------------------------------------------------------------ */
/* Review round 1's cases                                              */
/* ------------------------------------------------------------------ */

static int g_dropRouter;
static unsigned long g_dropAdp;
static unsigned long g_dropAddr;
static unsigned long g_dropBit;
static unsigned long g_dropCount;

/* Apply every write of `bit` to (router, adapter, address) and lose its
 * answer, as a response lost on the wire after the router acted. */
static int dropHook(FAB *f, unsigned long pdf, int router, unsigned long cs,
                    unsigned long adp, unsigned long addr, unsigned long n,
                    const unsigned long *data)
{
    (void)f;
    if (pdf == TBT_PDF_WRITE && router == g_dropRouter && cs == TBT_CS_ADAPTER &&
        adp == g_dropAdp && addr == g_dropAddr && n == 1 && data != NULL &&
        (data[0] & g_dropBit) != 0) {
        g_dropCount++;
        return FAB_HOOK_DROP_ANSWER;
    }
    return FAB_HOOK_GO;
}

static void test_review_enumeration(void)
{
    TBT_U4_CONFIG c;
    int dev;
    const TBT_U4_PORT *p;
    unsigned long hi;

    /* A router that never reports Router Ready: never used. */
    fabInit(&g_fab);
    fabHostMinimal(&g_fab);
    dev = fabDevice(&g_fab, FAB_DEV_USB4);
    g_fab.R[dev].NoRouterReady = 1;
    fabPlug(&g_fab, 1, dev);
    cfgDefault(&c);
    (void)start(&g_fab, &g_ring, &g_cm, &c);
    p = TbtU4Port(&g_cm, 1);
    CHECK_EQ(state(&g_cm, 1), TBT_U4_FAILED, "no Router Ready: FAILED");
    CHECK_EQ(p->Why, TBT_U4_WHY_ROUTER_READY, "for Router Ready");
    CHECK(!fabPcieUp(&g_fab, 1), "no tunnel through it");
    CHECK_EQ(fabFindWrite(&g_fab, 0, dev, TBT_CS_ROUTER, 0, 5), -1, "never configured");

    /* Valid set with another TopologyID: refused, DW 1 to 3 never written. */
    fabInit(&g_fab);
    fabHostMinimal(&g_fab);
    dev = fabDevice(&g_fab, FAB_DEV_USB4);
    fabPlug(&g_fab, 1, dev);
    g_fab.R[dev].Rcs[1] |= 1UL << 20;
    g_fab.R[dev].Rcs[2] = 0x0303UL;
    g_fab.R[dev].Rcs[3] = 0x80000000UL;
    (void)start(&g_fab, &g_ring, &g_cm, &c);
    CHECK_EQ(state(&g_cm, 1), TBT_U4_UNSUPPORTED, "someone else's router: UNSUPPORTED");
    hi = (unsigned long)fabFindWrite(&g_fab, 0, dev, TBT_CS_ROUTER, 0, 3);
    CHECK_EQ((long)hi, -1, "its TopologyID never rewritten");
    CHECK_EQ(g_fab.R[dev].Rcs[2], 0x0303UL, "left as found");
}

static void test_review_lost_answer(void)
{
    TBT_U4_CONFIG c;
    int dev;

    /* The host's PCIe Path Enable applied, every answer lost: the request
     * fails, and the rollback still clears what may have been written. */
    fabInit(&g_fab);
    fabHostMinimal(&g_fab);
    dev = fabDevice(&g_fab, FAB_DEV_USB4);
    fabPlug(&g_fab, 1, dev);
    g_dropRouter = 0;
    g_dropAdp = 5;
    g_dropAddr = FAB_CAP_ADP;
    g_dropBit = 0x80000000UL;
    g_dropCount = 0;
    g_fab.Hook = dropHook;
    cfgDefault(&c);
    (void)start(&g_fab, &g_ring, &g_cm, &c);
    CHECK(g_dropCount >= 1, "the enable was applied, its answers lost");
    CHECK_EQ(state(&g_cm, 1), TBT_U4_FAILED, "the PCIe tunnel failed: FAILED");
    CHECK(!TbtU4Port(&g_cm, 1)->Pcie.Up, "not up");
    CHECK_EQ(fabAdp(&g_fab, 0, 5, FAB_CAP_ADP) & 0x80000000UL, 0,
             "the host's Path Enable cleared by the rollback");
    CHECK_EQ(fabPath0(&g_fab, 0, 5, 8) & 0x80000000UL, 0, "its entry invalidated");
    g_fab.Hook = NULL;
    (void)dev;
}

static void test_review_held_inventory(void)
{
    TBT_U4_CONFIG c;
    int dev;

    /* A locked port carrying a predecessor's host-side paths: HELD, and
     * at its unplug those paths torn down. */
    fabInit(&g_fab);
    fabHostMinimal(&g_fab);
    dev = fabDevice(&g_fab, FAB_DEV_USB4);
    fabPlug(&g_fab, 1, dev);
    fabPreos(&g_fab, 1, 1);
    g_fab.R[0].Adp[1].Cs[4] |= 0x80000000UL;    /* Lock set again */
    g_fab.RxQCount = 0;
    cfgDefault(&c);
    (void)start(&g_fab, &g_ring, &g_cm, &c);
    CHECK_EQ(state(&g_cm, 1), TBT_U4_HELD, "locked with host paths: HELD");
    CHECK(TbtU4Port(&g_cm, 1)->Pcie.Written != 0, "its host side inventoried");
    CHECK_EQ(pathWrites(&g_fab), 0, "nothing written");
    fabUnplug(&g_fab, 1);
    poll(&g_cm, &g_fab, 1);
    CHECK_EQ(state(&g_cm, 1), TBT_U4_EMPTY, "unplugged: EMPTY");
    CHECK(hostClean(&g_fab, 1), "its host paths torn down, USB3 included");

    /* A reserved Adapter State: no removal evidence, nothing torn down. */
    fabInit(&g_fab);
    fabHostMinimal(&g_fab);
    dev = fabDevice(&g_fab, FAB_DEV_USB4);
    fabPlug(&g_fab, 1, dev);
    fabPreos(&g_fab, 1, 0);
    g_fab.R[0].Adp[1].Cs[FAB_CAP_LANE + 1] |= 0xFUL << 26;     /* state 15 */
    g_fab.RxQCount = 0;
    (void)start(&g_fab, &g_ring, &g_cm, &c);
    CHECK_EQ(state(&g_cm, 1), TBT_U4_HELD, "a reserved lane state: HELD");
    CHECK(fabPath0(&g_fab, 0, 5, 8) & 0x80000000UL, "the host paths left as found");
    CHECK_EQ(pathWrites(&g_fab), 0, "nothing written");

    /* A verified PCIe tunnel beside a USB3 tunnel that does not verify:
     * the USB3 host side is still torn down at the unplug. */
    fabInit(&g_fab);
    fabHostMinimal(&g_fab);
    dev = fabDevice(&g_fab, FAB_DEV_USB4);
    fabPlug(&g_fab, 1, dev);
    fabPreos(&g_fab, 1, 1);
    g_fab.R[dev].Adp[1].Path[2 * 10] &= ~0x80000000UL;   /* device side broken */
    g_fab.RxQCount = 0;
    (void)start(&g_fab, &g_ring, &g_cm, &c);
    CHECK_EQ(state(&g_cm, 1), TBT_U4_UP, "PCIe preserved");
    CHECK(!TbtU4Port(&g_cm, 1)->Usb3.Up, "the broken USB3 not counted up");
    fabUnplug(&g_fab, 1);
    poll(&g_cm, &g_fab, 1);
    CHECK(hostClean(&g_fab, 1), "its USB3 host side torn down too");
}

static void test_review_quarantine(void)
{
    int dev;
    int d2;
    int lane;
    const TBT_U4_PORT *p;
    TBT_U4_CONFIG c;

    /* A device-side entry quarantined is dropped with its router: a
     * replacement at the same route is never touched by it. */
    dev = setupMinimal(1, NULL);
    p = TbtU4Port(&g_cm, 1);
    g_fab.R[dev].Adp[1].PpStuck[p->Pcie.Hd] = 1;
    g_fab.R[dev].Adp[5].DetectStuck = 0;
    CHECK_EQ(TbtU4Eject(&g_cm, 1), TBT_E_STATE, "an eject with a stuck entry: not proven");
    CHECK(quarantineCount(&g_cm) >= 1, "quarantined on the device");
    fabUnplug(&g_fab, 1);
    poll(&g_cm, &g_fab, 1);
    CHECK_EQ(quarantineCount(&g_cm), 0, "dropped with its router");
    d2 = fabDevice(&g_fab, FAB_DEV_USB4);
    fabPlug(&g_fab, 1, d2);
    poll(&g_cm, &g_fab, 3);
    CHECK_EQ(state(&g_cm, 1), TBT_U4_UP, "the replacement UP");
    CHECK(g_fab.R[d2].Adp[1].Path[2 * p->Pcie.Hd] & 0x80000000UL,
          "its paths untouched by the old quarantine");

    /* A quarantine on a surviving router below depth 1 is kept and
     * retried. */
    fabInit(&g_fab);
    fabHostMinimal(&g_fab);
    dev = fabDevice(&g_fab, FAB_DEV_USB4);
    d2 = fabChild(&g_fab, dev);
    fabPlug(&g_fab, 1, dev);
    fabPreos(&g_fab, 1, 0);
    fabPlugChild(&g_fab, dev, d2, 1);
    g_fab.R[dev].Adp[7].Path[2 * 8] = 0x80000000UL | 8UL | (3UL << 11) | (7UL << 17);
    g_fab.R[dev].Adp[7].PpStuck[8] = 1;
    g_fab.RxQCount = 0;
    cfgDefault(&c);
    (void)start(&g_fab, &g_ring, &g_cm, &c);
    fabUnplugChild(&g_fab, dev);
    poll(&g_cm, &g_fab, 1);
    CHECK_EQ(quarantineCount(&g_cm), 1, "the parent's stuck entry quarantined");
    poll(&g_cm, &g_fab, 1);
    CHECK_EQ(quarantineCount(&g_cm), 1, "and kept while its router is there");
    g_fab.R[dev].Adp[7].PpStuck[8] = 0;
    g_fab.R[dev].Adp[7].Path[2 * 8 + 1] &= ~0x10000000UL;
    poll(&g_cm, &g_fab, 1);
    CHECK_EQ(quarantineCount(&g_cm), 0, "lifted once drained");
    lane = 0;
    (void)lane;
}

static void test_review_resume(void)
{
    int dev;
    LONG rc;
    const TBT_U4_PORT *p;

    /* The whole domain lost power across the sleep: the same router comes
     * back uninitialised, and is rebuilt before TbtU4Resume returns. */
    dev = setupMinimal(1, NULL);
    TbtU4Suspend(&g_cm);
    fabPowerCycle(&g_fab);
    rc = TbtU4Resume(&g_cm);
    p = TbtU4Port(&g_cm, 1);
    CHECK_EQ(rc, TBT_OK, "lost configuration: rebuilt, success");
    CHECK_EQ(state(&g_cm, 1), TBT_U4_UP, "UP before the hold is released");
    CHECK(p->Pcie.Up && p->Usb3.Up, "both tunnels rebuilt");
    CHECK(fabPcieUp(&g_fab, 1), "the fabric's tunnel up");

    /* A path re-walked at resume is adopted from the registers. */
    dev = setupMinimal(1, NULL);
    p = TbtU4Port(&g_cm, 1);
    TbtU4Suspend(&g_cm);
    rc = TbtU4Resume(&g_cm);
    CHECK_EQ(rc, TBT_OK, "intact: verified");
    CHECK(p->Pcie.Up && p->Pcie.Hu == 9, "the walked HopIDs kept");

    /* Configuration Valid lost but the paths there: not verified. */
    dev = setupMinimal(1, NULL);
    TbtU4Suspend(&g_cm);
    g_fab.R[dev].Rcs[5] &= ~0x80000000UL;
    rc = TbtU4Resume(&g_cm);
    CHECK_EQ(rc, TBT_E_STATE, "CV clear: an obligation unmet");
    CHECK_EQ(state(&g_cm, 1), TBT_U4_HELD, "HELD");

    /* The budget: a router that never comes ready makes the rebuild run
     * out; what is left is HELD, nothing torn down. */
    dev = setupMinimal(1, NULL);
    TbtU4Suspend(&g_cm);
    fabPowerCycle(&g_fab);
    g_fab.R[dev].NoRouterReady = 1;
    rc = TbtU4Resume(&g_cm);
    CHECK_EQ(rc, TBT_E_STATE, "a rebuild that cannot finish: failure path");
    CHECK(state(&g_cm, 1) == TBT_U4_HELD || state(&g_cm, 1) == TBT_U4_FAILED,
          "not torn down without removal evidence");
    CHECK(g_fab.NowUs < TbtU4ResumeBoundUs(2, 0) * 2UL, "bounded");
}

static void test_review_bandwidth_and_tmu(void)
{
    int dev;
    TBT_U4_CONFIG c;
    const TBT_U4_PORT *p;
    ULONG t0;

    /* An unrepresentable consumed bandwidth: no allocation written. */
    fabInit(&g_fab);
    fabHostMinimal(&g_fab);
    dev = fabDevice(&g_fab, FAB_DEV_USB4);
    g_fab.R[0].Adp[7].Cs[FAB_CAP_ADP + 3] = 20;     /* scale 20 */
    fabPlug(&g_fab, 1, dev);
    cfgDefault(&c);
    (void)start(&g_fab, &g_ring, &g_cm, &c);
    p = TbtU4Port(&g_cm, 1);
    CHECK(!p->Usb3.Up, "scale 20: no USB3 tunnel");
    CHECK_EQ(fabAdp(&g_fab, 0, 7, FAB_CAP_ADP + 2) & 0xFFFFFFUL, 0, "nothing allocated");
    CHECK_EQ(fabAdp(&g_fab, 0, 7, FAB_CAP_ADP + 2) & 0x80000000UL, 0, "CMR cleared");
    CHECK(p->Pcie.Up, "PCIe unaffected");

    /* A preserved USB3 tunnel's allocation released at its unplug. */
    fabInit(&g_fab);
    fabHostMinimal(&g_fab);
    dev = fabDevice(&g_fab, FAB_DEV_USB4);
    fabPlug(&g_fab, 1, dev);
    fabPreos(&g_fab, 1, 1);
    g_fab.R[0].Adp[7].Cs[FAB_CAP_ADP + 2] = 0x123456UL;
    g_fab.RxQCount = 0;
    (void)start(&g_fab, &g_ring, &g_cm, &c);
    CHECK(TbtU4Port(&g_cm, 1)->Usb3.Preserved, "USB3 preserved");
    fabUnplug(&g_fab, 1);
    poll(&g_cm, &g_fab, 1);
    CHECK((fabAdp(&g_fab, 0, 7, FAB_CAP_ADP + 2) & 0xFFFFFFUL) != 0x123456UL,
          "its allocation released to the consumed value");

    /* A failed USB3 construction starts the 500 ms gap. */
    fabInit(&g_fab);
    fabHostMinimal(&g_fab);
    dev = fabDevice(&g_fab, FAB_DEV_USB4);
    g_dropRouter = dev;
    g_dropAdp = 6;
    g_dropAddr = FAB_CAP_ADP;
    g_dropBit = 0x80000000UL;
    g_dropCount = 0;
    g_fab.Hook = dropHook;
    fabPlug(&g_fab, 1, dev);
    (void)start(&g_fab, &g_ring, &g_cm, &c);
    p = TbtU4Port(&g_cm, 1);
    CHECK(p->Usb3Failed && p->Usb3GapValid, "the failed USB3 tunnel started the gap");
    t0 = p->LastUsb3TeardownUs;
    CHECK(t0 != 0, "with its time");
    CHECK_EQ(fabAdp(&g_fab, dev, 6, FAB_CAP_ADP) & 0x80000000UL, 0,
             "the device's enable rolled back");
    CHECK_EQ(fabRtr(&g_fab, dev, FAB_RCAP_TMU + 3) >> 16, 0,
             "the link's TMU, enabled for USB3, disabled again");
    g_fab.Hook = NULL;

    /* An eject turns the TMU it enabled off. */
    dev = setupMinimal(1, NULL);
    CHECK_EQ(fabRtr(&g_fab, dev, FAB_RCAP_TMU + 3) >> 16, 16, "HiFi before");
    CHECK_EQ(TbtU4Eject(&g_cm, 1), TBT_OK, "eject");
    CHECK_EQ(fabRtr(&g_fab, dev, FAB_RCAP_TMU + 3) >> 16, 0, "TMU off after the eject");
    CHECK(fabAdp(&g_fab, dev, 1, FAB_CAP_TMU_ADP + 6) & 2UL, "Disable Time Sync on the device");
}

static void test_review_ba_and_closure(void)
{
    int dev;
    TBT_U4_CONFIG c;
    int d2;

    /* Buffer Allocation Per USB4 Port enabled on a preserved host router:
     * one request per lane 0 adapter, each port its own answer. */
    fabInit(&g_fab);
    fabHostMinimal(&g_fab);
    dev = fabDevice(&g_fab, FAB_DEV_USB4);
    d2 = fabDevice(&g_fab, FAB_DEV_USB4);
    g_fab.R[0].CapsData = (1UL << 6) | (1UL << 7);    /* capability 3 */
    g_fab.R[0].BaPerLane[1] = 24;
    g_fab.R[0].BaPerLane[3] = 20;
    g_fab.R[0].Rcs[3] |= 0x80000000UL;                /* preserved */
    g_fab.R[0].Rcs[6] |= 0x01000000UL;
    fabPlug(&g_fab, 1, dev);
    fabPlug(&g_fab, 2, d2);
    g_fab.RxQCount = 0;
    cfgDefault(&c);
    (void)start(&g_fab, &g_ring, &g_cm, &c);
    CHECK_EQ(g_cm.HostRouter.CapsState, TBT_U4_CAPS_KNOWN, "the host's capabilities adopted");
    CHECK_EQ(g_cm.Ports[0].HostBaPcie, 24, "port 1's own baMaxPCIe");
    CHECK_EQ(g_cm.Ports[1].HostBaPcie, 20, "port 2's own");
    CHECK_EQ((fabPath0(&g_fab, 0, 1, TbtU4Port(&g_cm, 1)->Pcie.Hu) >> 17) & 0x7F, 24,
             "port 1's path credits");
    CHECK_EQ((fabPath0(&g_fab, 0, 3, TbtU4Port(&g_cm, 2)->Pcie.Hu) >> 17) & 0x7F, 20,
             "port 2's");

    /* Cleaning proves closure with Detect: an LTSSM held transitional
     * withholds the statement's prerequisite. */
    fabInit(&g_fab);
    fabHostMinimal(&g_fab);
    dev = fabDevice(&g_fab, FAB_DEV_USB4);
    fabPlug(&g_fab, 1, dev);
    fabPreos(&g_fab, 1, 0);
    g_fab.RxQCount = 0;
    g_fab.R[0].Adp[5].LtssmForce = 1 + 2;
    cfgDefault(&c);
    c.PreOsCleanup = 2;
    g_ldCalls = 0;
    (void)start(&g_fab, &g_ring, &g_cm, &c);
    CHECK_EQ(g_ldCalls, 1, "the cleaning's upcall");
    CHECK(!g_ld.PathClosed, "not closed while the LTSSM is not in Detect");
    g_fab.R[0].Adp[5].LtssmForce = 0;
}

static void test_review_identity(void)
{
    TBT_U4_CONFIG c;
    int dev;
    int child;
    ULONG gen;
    const TBT_U4_PORT *p;

    /* A router below depth 1 reset under a live link: TopologyID Valid. */
    fabInit(&g_fab);
    fabHostMinimal(&g_fab);
    dev = fabDevice(&g_fab, FAB_DEV_USB4);
    child = fabChild(&g_fab, dev);
    fabPlug(&g_fab, 1, dev);
    fabPreos(&g_fab, 1, 0);
    fabPlugChild(&g_fab, dev, child, 1);
    g_fab.RxQCount = 0;
    cfgDefault(&c);
    (void)start(&g_fab, &g_ring, &g_cm, &c);
    CHECK_EQ(g_cm.Counters.LinksDiscovered, 1, "the link");
    CHECK(g_cm.Links[0].CapsState != TBT_U4_CAPS_PENDING, "its capabilities asked");
    g_fab.R[child].Rcs[3] &= ~0x80000000UL;
    poll(&g_cm, &g_fab, 1);
    CHECK_EQ(g_cm.Counters.LinksRemoved, 1, "TopologyID Valid clear at depth 2: removed");

    /* A Thunderbolt 3 router swapped quietly: only its LC UUID tells. */
    fabInit(&g_fab);
    fabHostMinimal(&g_fab);
    dev = fabDevice(&g_fab, FAB_DEV_TBT3_TR);
    fabPlug(&g_fab, 1, dev);
    (void)start(&g_fab, &g_ring, &g_cm, &c);
    p = TbtU4Port(&g_cm, 1);
    gen = p->Generation;
    g_fab.R[dev].Rcs[FAB_RCAP_LC + 6] ^= 0xFFUL;
    poll(&g_cm, &g_fab, 2);
    CHECK(p->Generation > gen, "another Thunderbolt 3 UUID: torn down and reconnected");

    /* A notification from a router this instance does not know: neither
     * acknowledgement. */
    {
        unsigned long dw[3];
        g_fab.AcksPlain = 0;
        g_fab.AcksEnh = 0;
        dw[0] = 0x80000000UL;
        dw[1] = 0x0503UL;
        dw[2] = TBT_EV_ERR_LINK;
        fabInject(&g_fab, TBT_PDF_NOTIFY, dw, 3);
        poll(&g_cm, &g_fab, 2);
        CHECK_EQ(g_fab.AcksPlain + g_fab.AcksEnh, 0, "an unknown router's event: withheld");
    }
}

/* ------------------------------------------------------------------ */
/* Review round 2's cases                                              */
/* ------------------------------------------------------------------ */

static int g_swRouter;
static unsigned long g_swCs;
static unsigned long g_swAdp;
static unsigned long g_swAddr;
static int g_swWrite;
static unsigned long g_swMask;      /* a write: swallowed when (data & mask) == want */
static unsigned long g_swWant;
static unsigned long g_swLeft;      /* how many to swallow */

static int swallowHook(FAB *f, unsigned long pdf, int router, unsigned long cs,
                       unsigned long adp, unsigned long addr, unsigned long n,
                       const unsigned long *data)
{
    (void)f;
    if (g_swLeft == 0 || router != g_swRouter || cs != g_swCs ||
        (cs != TBT_CS_ROUTER && adp != g_swAdp) ||
        g_swAddr < addr || g_swAddr >= addr + n) {
        return FAB_HOOK_GO;
    }
    if (g_swWrite) {
        if (pdf != TBT_PDF_WRITE || data == NULL ||
            (data[g_swAddr - addr] & g_swMask) != g_swWant) {
            return FAB_HOOK_GO;
        }
    } else if (pdf != TBT_PDF_READ) {
        return FAB_HOOK_GO;
    }
    g_swLeft--;
    return FAB_HOOK_SWALLOW;
}

static void armSwallow(FAB *f, int router, unsigned long cs, unsigned long adp,
                       unsigned long addr, int write, unsigned long mask,
                       unsigned long want, unsigned long count)
{
    g_swRouter = router;
    g_swCs = cs;
    g_swAdp = adp;
    g_swAddr = addr;
    g_swWrite = write;
    g_swMask = mask;
    g_swWant = want;
    g_swLeft = count;
    f->Hook = swallowHook;
}

/* The longest a resume may take with budgets `host` and `each` for n
 * obligations: the bound's own margin and platform term, with these
 * budgets in place of the defaults. */
static unsigned long boundWith(ULONG n, unsigned long host, unsigned long each)
{
    return TbtU4ResumeBoundUs(n, 0) - TBT_U4_RESUME_HOST_US - n * TBT_U4_RESUME_EACH_US +
           host + n * each;
}

static void test_review2_budget(void)
{
    int dev;
    LONG rc;
    unsigned long t0;

    /* Spent before the host router answers: the host router's failure
     * (inert after activation, section 4.4), and nothing torn down. */
    dev = setupMinimal(1, NULL);
    TbtU4Suspend(&g_cm);
    g_cm.ResumeHostUs = 1;
    g_cm.ResumeEachUs = 0;
    rc = TbtU4Resume(&g_cm);
    CHECK(rc != TBT_OK, "no budget: the failure path");
    CHECK(g_cm.Failed, "the host router not reached in its budget: inert");
    CHECK(fabPcieUp(&g_fab, 1), "nothing torn down");
    CHECK(g_fab.R[dev].Adp[1].Path[2 * 8] & 0x80000000UL, "the device's paths untouched");

    /* Spent in the host router's discovery: the same. */
    dev = setupMinimal(1, NULL);
    TbtU4Suspend(&g_cm);
    g_cm.ResumeHostUs = 150000UL;
    g_cm.ResumeEachUs = 0;
    armSwallow(&g_fab, 0, TBT_CS_ROUTER, 0, 0, 0, 0, 0, 100);
    t0 = g_fab.NowUs;
    rc = TbtU4Resume(&g_cm);
    g_fab.Hook = NULL;
    CHECK(rc != TBT_OK && g_cm.Failed, "spent at the host router: the failure path");
    CHECK(g_fab.NowUs - t0 <= boundWith(2, 150000UL, 0), "within the bound");
    CHECK(fabPcieUp(&g_fab, 1), "nothing torn down");

    /* Spent in a rebuild (a router that never comes ready), on a frozen
     * clock: the waited sum ends it, within the bound. */
    dev = setupMinimal(1, NULL);
    TbtU4Suspend(&g_cm);
    fabPowerCycle(&g_fab);
    g_fab.R[dev].NoRouterReady = 1;
    g_cm.ResumeHostUs = 500000UL;
    g_cm.ResumeEachUs = 300000UL;
    g_fab.ClockFrozen = 1;
    g_fab.FrozenUs = g_fab.NowUs;
    t0 = g_fab.NowUs;
    rc = TbtU4Resume(&g_cm);
    g_fab.ClockFrozen = 0;
    CHECK_EQ(rc, TBT_E_STATE, "a frozen clock: still ends, the failure path");
    CHECK(g_fab.NowUs - t0 <= boundWith(2, 500000UL, 300000UL), "within the bound");
    CHECK(state(&g_cm, 1) == TBT_U4_HELD, "HELD, not torn down");

    /* Spent in verification: no teardown, HELD. */
    dev = setupMinimal(1, NULL);
    TbtU4Suspend(&g_cm);
    g_cm.ResumeEachUs = 1;
    armSwallow(&g_fab, dev, TBT_CS_ROUTER, 0, 0, 0, 0, 0, 100);
    t0 = g_fab.NowUs;
    rc = TbtU4Resume(&g_cm);
    g_fab.Hook = NULL;
    CHECK_EQ(rc, TBT_E_STATE, "spent verifying: the failure path");
    CHECK(g_fab.NowUs - t0 <= boundWith(2, TBT_U4_RESUME_HOST_US, 1), "within the bound");
    CHECK_EQ(state(&g_cm, 1), TBT_U4_HELD, "HELD");
    CHECK(fabPcieUp(&g_fab, 1), "nothing torn down");
}

static void test_review2_identity_and_links(void)
{
    TBT_U4_CONFIG c;
    int dev;
    int child;
    ULONG gen;
    const TBT_U4_PORT *p;

    /* The UUID's read failing at the connect: never compared, so no false
     * removal; the next good sample becomes the identity. */
    fabInit(&g_fab);
    fabHostMinimal(&g_fab);
    dev = fabDevice(&g_fab, FAB_DEV_USB4);
    fabPlug(&g_fab, 1, dev);
    armSwallow(&g_fab, dev, TBT_CS_ROUTER, 0, TBT_ROUTER_CS_7, 0, 0, 0, TBT_CTL_ATTEMPTS);
    cfgDefault(&c);
    (void)start(&g_fab, &g_ring, &g_cm, &c);
    g_fab.Hook = NULL;
    p = TbtU4Port(&g_cm, 1);
    CHECK_EQ(state(&g_cm, 1), TBT_U4_UP, "UUID unread: UP all the same");
    CHECK(!p->Dev.UuidValid, "its UUID not valid");
    gen = p->Generation;
    poll(&g_cm, &g_fab, 3);
    CHECK_EQ(p->Generation, gen, "no false removal");
    CHECK(p->Dev.UuidValid && p->Dev.Uuid[0] == g_fab.R[dev].Rcs[7], "the next sample kept");

    /* A Thunderbolt 3 router below depth 1: no router operation, its LC
     * fuse as the identity. */
    fabInit(&g_fab);
    fabHostMinimal(&g_fab);
    dev = fabDevice(&g_fab, FAB_DEV_USB4);
    child = fabDevice(&g_fab, FAB_DEV_TBT3_TR);
    fabPlug(&g_fab, 1, dev);
    fabPreos(&g_fab, 1, 0);
    fabPlugChild(&g_fab, dev, child, 1);
    g_fab.RxQCount = 0;
    (void)start(&g_fab, &g_ring, &g_cm, &c);
    CHECK_EQ(g_cm.Counters.LinksDiscovered, 1, "the Thunderbolt 3 link");
    CHECK(g_cm.Links[0].ChildTbt3, "known as Thunderbolt 3");
    CHECK_EQ(g_cm.Links[0].CapsState, TBT_U4_CAPS_DEFAULT, "no Get Capabilities");
    CHECK_EQ(fabFindWrite(&g_fab, 0, child, TBT_CS_ROUTER, 0, TBT_ROUTER_CS_26), -1,
             "no router operation sent to it");
    CHECK_EQ(g_cm.Links[0].ChildUuid[0], 0xAABB0000UL + (unsigned long)child,
             "the LC's fused UUID");
    poll(&g_cm, &g_fab, 2);
    CHECK_EQ(g_cm.Counters.LinksRemoved, 0, "stable: kept");
    g_fab.R[child].Rcs[FAB_RCAP_LC + 6] ^= 0xFFUL;
    poll(&g_cm, &g_fab, 2);
    CHECK_EQ(g_cm.Counters.LinksRemoved, 1, "another fused UUID: removed");
}

static void test_review2_restore(void)
{
    TBT_U4_CONFIG c;
    int dev;
    int lane;
    const TBT_U4_PORT *p;

    /* Lane Disable's clearing lost at the unplug: still owed, retried at
     * the idle tick until read back clear. */
    fabInit(&g_fab);
    fabHostMinimal(&g_fab);
    dev = fabDevice(&g_fab, FAB_DEV_USB4);
    g_fab.R[dev].BondPossible = 0;
    g_fab.R[dev].Adp[1].Cs[FAB_CAP_PORT + 18] = 0;
    fabPlug(&g_fab, 1, dev);
    cfgDefault(&c);
    (void)start(&g_fab, &g_ring, &g_cm, &c);
    p = TbtU4Port(&g_cm, 1);
    lane = fabPortLane0(&g_fab, 1);
    CHECK(p->Lane1Disabled, "lane 1 disabled");
    armSwallow(&g_fab, 0, TBT_CS_ADAPTER, (unsigned long)lane + 1, FAB_CAP_LANE + 1, 1,
               0x4000UL, 0, 2 * TBT_CTL_ATTEMPTS);
    fabUnplug(&g_fab, 1);
    /* the fabric clears it at a disconnect; this router keeps it */
    g_fab.R[0].Adp[lane + 1].Cs[FAB_CAP_LANE + 1] |= 0x4000UL;
    poll(&g_cm, &g_fab, 1);
    g_fab.Hook = NULL;
    CHECK_EQ(state(&g_cm, 1), TBT_U4_EMPTY, "unplugged: EMPTY");
    CHECK(p->Lane1Disabled, "the restore failed: still owed");
    CHECK(fabAdp(&g_fab, 0, lane + 1, FAB_CAP_LANE + 1) & 0x4000UL, "Lane Disable still set");
    poll(&g_cm, &g_fab, 2);
    CHECK(!p->Lane1Disabled, "retried at the idle tick");
    CHECK_EQ(fabAdp(&g_fab, 0, lane + 1, FAB_CAP_LANE + 1) & 0x4000UL, 0,
             "Lane Disable cleared");

    /* A host lane entry unreadable when the tunnel is followed: the
     * tunnel incomplete, so its teardown never claims the path closed. */
    fabInit(&g_fab);
    fabHostMinimal(&g_fab);
    dev = fabDevice(&g_fab, FAB_DEV_USB4);
    fabPlug(&g_fab, 1, dev);
    fabPreos(&g_fab, 1, 0);
    g_fab.RxQCount = 0;
    lane = fabPortLane0(&g_fab, 1);
    armSwallow(&g_fab, 0, TBT_CS_PATH, (unsigned long)lane, 2 * 9, 0, 0, 0, 1000);
    (void)start(&g_fab, &g_ring, &g_cm, &c);
    g_fab.Hook = NULL;
    p = TbtU4Port(&g_cm, 1);
    CHECK(p->Pcie.Incomplete, "the host lane's entry unread: incomplete");
    CHECK_EQ(state(&g_cm, 1), TBT_U4_HELD, "HELD");
    CHECK(p->Pcie.Written & TBT_U4_E_HOST_ENABLE, "its host side owned all the same");
    /* Its unplug with the entry readable again: the sweep finds it and
     * closes it, and only then is the path proven closed. */
    fabUnplug(&g_fab, 1);
    poll(&g_cm, &g_fab, 1);
    CHECK_EQ(state(&g_cm, 1), TBT_U4_EMPTY, "its unplug: EMPTY");
    CHECK_EQ(fabPath0(&g_fab, 0, lane, 9) & 0x80000000UL, 0, "the unread entry found and closed");
    CHECK(hostClean(&g_fab, 1), "the host side clean");
    CHECK(!p->HostResidual, "nothing owed");

    /* Still unreadable at the unplug: never proven closed, owed; the
     * re-plug waits for the sweep, then connects. */
    fabInit(&g_fab);
    fabHostMinimal(&g_fab);
    dev = fabDevice(&g_fab, FAB_DEV_USB4);
    fabPlug(&g_fab, 1, dev);
    fabPreos(&g_fab, 1, 0);
    g_fab.RxQCount = 0;
    lane = fabPortLane0(&g_fab, 1);
    armSwallow(&g_fab, 0, TBT_CS_PATH, (unsigned long)lane, 2 * 9, 0, 0, 0, 100000UL);
    (void)start(&g_fab, &g_ring, &g_cm, &c);
    p = TbtU4Port(&g_cm, 1);
    CHECK(p->Pcie.Incomplete, "incomplete");
    g_ldCalls = 0;
    memset(&g_ld, 0, sizeof(g_ld));
    fabUnplug(&g_fab, 1);
    poll(&g_cm, &g_fab, 1);
    CHECK(g_ldCalls == 0 || !g_ld.PathClosed, "an incomplete tunnel: never proven closed");
    CHECK(p->HostResidual, "the sweep owed");
    CHECK(fabPath0(&g_fab, 0, lane, 9) & 0x80000000UL, "the entry still there");
    fabPlug(&g_fab, 1, dev);
    poll(&g_cm, &g_fab, 2);
    CHECK(state(&g_cm, 1) != TBT_U4_UP, "no connection over an unswept host side");
    g_fab.Hook = NULL;
    poll(&g_cm, &g_fab, 3);
    CHECK(!p->HostResidual, "swept once readable");
    CHECK_EQ(state(&g_cm, 1), TBT_U4_UP, "then connected");
}

static int g_jumpRouter;
static int g_jumpDone;
static int g_jumpWrite;             /* 0: the first path read; 1: a write at */
static unsigned long g_jumpAdp;     /* (g_jumpAdp, g_jumpAddr) */
static unsigned long g_jumpAddr;

/* The chosen request moves the clock past any budget. */
static int jumpHook(FAB *f, unsigned long pdf, int router, unsigned long cs,
                    unsigned long adp, unsigned long addr, unsigned long n,
                    const unsigned long *data)
{
    (void)n;
    (void)data;
    if (g_jumpDone || router != g_jumpRouter) {
        return FAB_HOOK_GO;
    }
    if ((!g_jumpWrite && pdf == TBT_PDF_READ && cs == TBT_CS_PATH) ||
        (g_jumpWrite && pdf == TBT_PDF_WRITE && cs == TBT_CS_ADAPTER &&
         adp == g_jumpAdp && addr == g_jumpAddr)) {
        g_jumpDone = 1;
        f->NowUs += 1000000000UL;
    }
    return FAB_HOOK_GO;
}

static int g_retryRouter;
static unsigned long g_retryCount;

/* Every request to one router: three attempts lost, the fourth answered. */
static int retryHook(FAB *f, unsigned long pdf, int router, unsigned long cs,
                     unsigned long adp, unsigned long addr, unsigned long n,
                     const unsigned long *data)
{
    (void)f;
    (void)pdf;
    (void)cs;
    (void)adp;
    (void)addr;
    (void)n;
    (void)data;
    if (router != g_retryRouter) {
        return FAB_HOOK_GO;
    }
    g_retryCount++;
    return (g_retryCount % 4UL) != 0 ? FAB_HOOK_SWALLOW : FAB_HOOK_GO;
}

static int g_tmuFail;

/* Every write of the host router's TMU Time Disruption clear swallowed. */
static int tdHook(FAB *f, unsigned long pdf, int router, unsigned long cs,
                  unsigned long adp, unsigned long addr, unsigned long n,
                  const unsigned long *data)
{
    (void)f;
    (void)adp;
    if (g_tmuFail && pdf == TBT_PDF_WRITE && router == 0 && cs == TBT_CS_ROUTER &&
        data != NULL && n == 1 && addr == (unsigned long)g_cm.HostRouter.CapTmu &&
        (data[0] & TBT_TMU_RTR0_TD) == 0) {
        return FAB_HOOK_SWALLOW;
    }
    return FAB_HOOK_GO;
}

static void test_review3(void)
{
    TBT_U4_CONFIG c;
    int dev;
    int child;
    int lane;
    int i;
    LONG rc;
    ULONG before;
    ULONG before0;
    const TBT_U4_PORT *p;

    /* The budget spent inside verification itself: HELD, nothing torn
     * down, the spent path counted. */
    dev = setupMinimal(1, NULL);
    TbtU4Suspend(&g_cm);
    before = g_cm.Counters.ResumeBudgetsSpent;
    g_jumpRouter = dev;
    g_jumpDone = 0;
    g_jumpWrite = 0;
    g_fab.Hook = jumpHook;
    rc = TbtU4Resume(&g_cm);
    g_fab.Hook = NULL;
    CHECK(g_jumpDone, "the verification's path read reached");
    CHECK_EQ(rc, TBT_E_STATE, "spent in verification: the failure path");
    CHECK_EQ(g_cm.Counters.ResumeBudgetsSpent, before + 1, "the budget-spent path ran");
    CHECK_EQ(state(&g_cm, 1), TBT_U4_HELD, "HELD");
    CHECK(fabPcieUp(&g_fab, 1), "nothing torn down");

    /* Retries that succeed on the last attempt, on a frozen clock: the
     * waited sum still ends the resume within the bound. */
    dev = setupMinimal(1, NULL);
    TbtU4Suspend(&g_cm);
    g_cm.ResumeHostUs = 2000000UL;
    g_cm.ResumeEachUs = 0;
    before0 = g_cm.Counters.ResumeBudgetsSpent;
    g_fab.ClockFrozen = 1;
    g_fab.FrozenUs = g_fab.NowUs;
    g_retryRouter = dev;
    g_retryCount = 0;
    g_fab.Hook = retryHook;
    before = g_fab.NowUs;
    rc = TbtU4Resume(&g_cm);
    g_fab.Hook = NULL;
    g_fab.ClockFrozen = 0;
    CHECK(g_retryCount > 0, "retries happened");
    CHECK(g_cm.Counters.ResumeBudgetsSpent > before0, "the waited sum spent the budget");
    CHECK(g_fab.NowUs - before <= boundWith(2, 2000000UL, 0), "a frozen clock: within the bound");

    /* A full quarantine table: a pending host protocol HopID 8 is still
     * never reused. */
    fabInit(&g_fab);
    fabHostMinimal(&g_fab);
    dev = fabDevice(&g_fab, FAB_DEV_USB4);
    cfgDefault(&c);
    g_ldCalls = 0;
    (void)start(&g_fab, &g_ring, &g_cm, &c);
    /* Host entries (their router never goes) that stay pending: port 2's
     * lanes, HopIDs 8 to 31 of lane 0 and 8 to 15 of lane 1. */
    lane = fabPortLane0(&g_fab, 2);
    for (i = 0; i < (int)TBT_U4_QUARANTINE; i++) {
        int a = i < 24 ? lane : lane + 1;
        int hp = i < 24 ? 8 + i : 8 + (i - 24);
        g_fab.R[0].Adp[a].Path[2 * hp + 1] |= 0x10000000UL;
        g_fab.R[0].Adp[a].PpStuck[hp] = 1;
        g_cm.Quarantine[i].Used = TRUE;
        g_cm.Quarantine[i].Route.Hi = 0;
        g_cm.Quarantine[i].Route.Lo = 0;
        g_cm.Quarantine[i].Adapter = (ULONG)a;
        g_cm.Quarantine[i].Hop = (ULONG)hp;
    }
    g_fab.R[0].Adp[fabPortPcie(&g_fab, 1)].Path[2 * 8 + 1] |= 0x10000000UL;  /* pending */
    g_fab.R[0].Adp[fabPortPcie(&g_fab, 1)].PpStuck[8] = 1;                  /* never drains */
    fabPlug(&g_fab, 1, dev);
    poll(&g_cm, &g_fab, 2);
    p = TbtU4Port(&g_cm, 1);
    CHECK_EQ(quarantineCount(&g_cm), (int)TBT_U4_QUARANTINE, "the table still full");
    CHECK(!p->Pcie.Up, "a pending protocol entry with the table full: no PCIe tunnel");
    CHECK(!fabPcieUp(&g_fab, 1), "nothing built over it");
    for (i = 0; i < (int)TBT_U4_QUARANTINE; i++) {
        g_cm.Quarantine[i].Used = FALSE;
    }

    /* The host's Time Disruption that will not clear: owned, no USB3
     * built, cleared and USB3 built once it clears. */
    fabInit(&g_fab);
    fabHostMinimal(&g_fab);
    dev = fabDevice(&g_fab, FAB_DEV_USB4);
    fabPlug(&g_fab, 1, dev);
    g_tmuFail = 1;
    g_fab.Hook = tdHook;
    (void)start(&g_fab, &g_ring, &g_cm, &c);
    p = TbtU4Port(&g_cm, 1);
    if (g_cm.HostRouter.CapTmu != 0) {
        CHECK(g_cm.HostTmuTdOwed, "the disruption owned");
        CHECK(!g_cm.HostTmuOk, "the host TMU not usable");
        CHECK(!p->Usb3.Up, "no USB3 over it");
        CHECK(p->Pcie.Up, "PCIe built");
        g_tmuFail = 0;
        fabUnplug(&g_fab, 1);
        poll(&g_cm, &g_fab, 2);
        CHECK(!g_cm.HostTmuTdOwed && g_cm.HostTmuOk, "cleared at the idle tick");
        fabPlug(&g_fab, 1, dev);
        poll(&g_cm, &g_fab, 4);
        CHECK(p->Usb3.Up, "USB3 once the host TMU is usable");
    }
    g_fab.Hook = NULL;
    g_tmuFail = 0;

    /* A Thunderbolt 3 child whose fuse read fails at discovery: adopted
     * from the next good sample, never a false removal. */
    fabInit(&g_fab);
    fabHostMinimal(&g_fab);
    dev = fabDevice(&g_fab, FAB_DEV_USB4);
    child = fabDevice(&g_fab, FAB_DEV_TBT3_TR);
    fabPlug(&g_fab, 1, dev);
    fabPreos(&g_fab, 1, 0);
    fabPlugChild(&g_fab, dev, child, 1);
    g_fab.RxQCount = 0;
    armSwallow(&g_fab, child, TBT_CS_ROUTER, 0, FAB_RCAP_LC + 3, 0, 0, 0, TBT_CTL_ATTEMPTS);
    (void)start(&g_fab, &g_ring, &g_cm, &c);
    g_fab.Hook = NULL;
    CHECK_EQ(g_cm.Counters.LinksDiscovered, 1, "the link");
    CHECK_EQ(g_swLeft, 0, "its fuse read failed at discovery");
    poll(&g_cm, &g_fab, 3);
    CHECK_EQ(g_cm.Counters.LinksRemoved, 0, "no false removal");
    CHECK(g_cm.Links[0].ChildUuidValid &&
          g_cm.Links[0].ChildUuid[0] == 0xAABB0000UL + (unsigned long)child, "adopted");

    /* A link's removal whose parent entry cannot be read: kept pending,
     * retried, the OS told once. */
    fabInit(&g_fab);
    fabHostMinimal(&g_fab);
    dev = fabDevice(&g_fab, FAB_DEV_USB4);
    child = fabChild(&g_fab, dev);
    fabPlug(&g_fab, 1, dev);
    fabPreos(&g_fab, 1, 0);
    fabPlugChild(&g_fab, dev, child, 1);
    g_fab.R[dev].Adp[7].Path[2 * 8] = 0x80000000UL | 8UL | (3UL << 11) | (7UL << 17);
    g_fab.R[dev].Adp[7].Cs[FAB_CAP_ADP] |= 0x80000000UL;
    g_fab.R[dev].Adp[3].Path[2 * 9] = 0x80000000UL | 8UL | (7UL << 11) | (16UL << 17);
    g_fab.RxQCount = 0;
    (void)start(&g_fab, &g_ring, &g_cm, &c);
    (void)countActions(&g_cm, 0);
    armSwallow(&g_fab, dev, TBT_CS_PATH, 7, 2 * 8, 0, 0, 0, 100000UL);
    fabUnplugChild(&g_fab, dev);
    poll(&g_cm, &g_fab, 1);
    CHECK(g_cm.Links[0].Used && g_cm.Links[0].RemovalPending, "its removal pending");
    CHECK(g_fab.R[dev].Adp[7].Path[2 * 8] & 0x80000000UL, "the unread entry still there");
    g_fab.Hook = NULL;
    poll(&g_cm, &g_fab, 2);
    CHECK(!g_cm.Links[0].Used, "finished once readable");
    CHECK_EQ(g_fab.R[dev].Adp[7].Path[2 * 8] & 0x80000000UL, 0, "the entry closed");
    CHECK_EQ(countActions(&g_cm, TBT_U4_ACT_DEEP_REMOVED), 1, "the OS told once");
    CHECK_EQ(g_cm.Counters.LinksRemoved, 1, "counted once");

    /* The resume of a lost configuration closes a pending host protocol
     * entry before rebuilding over it. */
    dev = setupMinimal(1, NULL);
    lane = fabPortLane0(&g_fab, 1);
    TbtU4Suspend(&g_cm);
    fabPowerCycle(&g_fab);
    {
        int hp = fabPortPcie(&g_fab, 1);
        g_fab.R[0].Adp[hp].Path[2 * 8] = 8UL | ((unsigned long)lane << 11);
        g_fab.R[0].Adp[hp].Path[2 * 8 + 1] |= 0x10000000UL;     /* pending */
        g_fab.R[0].Adp[hp].PpStuck[8] = 1;                      /* never drains */
    }
    rc = TbtU4Resume(&g_cm);
    CHECK_EQ(rc, TBT_E_STATE, "a pending host entry that never drains: not rebuilt");
    CHECK(state(&g_cm, 1) != TBT_U4_UP, "not UP over it");
    CHECK(quarantineCount(&g_cm) >= 1, "quarantined");
}

static void frozenResume(unsigned long tick, unsigned long startWaited, const char *what)
{
    int dev;
    unsigned long t0;
    ULONG spent;

    dev = setupMinimal(1, NULL);
    TbtU4Suspend(&g_cm);
    g_fab.TickUs = tick;
    g_fab.Host.TickUs = tick;
    g_ring.WaitedUs = startWaited;
    g_cm.ResumeHostUs = 2000000UL;
    g_cm.ResumeEachUs = 0;
    spent = g_cm.Counters.ResumeBudgetsSpent;
    g_fab.ClockFrozen = 1;
    g_fab.FrozenUs = g_fab.NowUs;
    g_retryRouter = dev;
    g_retryCount = 0;
    g_fab.Hook = retryHook;
    t0 = g_fab.NowUs;
    (void)TbtU4Resume(&g_cm);
    g_fab.Hook = NULL;
    g_fab.ClockFrozen = 0;
    g_fab.TickUs = 0;
    g_fab.Host.TickUs = 0;
    CHECK(g_cm.Counters.ResumeBudgetsSpent > spent, what);
    CHECK(g_fab.NowUs - t0 <= TbtU4ResumeBoundUs(2, tick) - TBT_U4_RESUME_HOST_US -
                              2UL * TBT_U4_RESUME_EACH_US + 2000000UL, what);
}

static int g_cfgFail;

/* The host's USB4 Port is Configured write swallowed. */
static int cfgHook(FAB *f, unsigned long pdf, int router, unsigned long cs,
                   unsigned long adp, unsigned long addr, unsigned long n,
                   const unsigned long *data)
{
    (void)f;
    (void)adp;
    (void)n;
    if (g_cfgFail && pdf == TBT_PDF_WRITE && router == 0 && cs == TBT_CS_ADAPTER &&
        addr == FAB_CAP_PORT + TBT_PORT_CS_19 && data != NULL && (data[0] & TBT_PORT19_PC) != 0) {
        return FAB_HOOK_SWALLOW;
    }
    return FAB_HOOK_GO;
}

static void test_review4(void)
{
    TBT_U4_CONFIG c;
    int dev;
    int lane;
    int hu;
    LONG rc;
    unsigned long t0 = 0;
    const TBT_U4_PORT *p;

    cfgDefault(&c);

    /* The waited sum across the ring counter's wrap, and with coarse
     * sleeps (10 ms and 55 ms ticks), on a frozen clock. */
    frozenResume(0, 0xFFFFFF00UL, "the ring counter wrapping: still spent, within the bound");
    frozenResume(10000UL, 0, "a 10 ms tick, frozen clock: spent within the bound");
    frozenResume(55000UL, 0, "a 55 ms tick, frozen clock: spent within the bound");

    /* A pending (invalid, never draining) host lane entry met by the
     * sweep of an incomplete tunnel: quarantined, never proven closed. */
    fabInit(&g_fab);
    fabHostMinimal(&g_fab);
    dev = fabDevice(&g_fab, FAB_DEV_USB4);
    fabPlug(&g_fab, 1, dev);
    fabPreos(&g_fab, 1, 0);
    g_fab.RxQCount = 0;
    lane = fabPortLane0(&g_fab, 1);
    armSwallow(&g_fab, 0, TBT_CS_PATH, (unsigned long)lane, 2 * 9, 0, 0, 0, 1000);
    (void)start(&g_fab, &g_ring, &g_cm, &c);
    g_fab.Hook = NULL;
    p = TbtU4Port(&g_cm, 1);
    CHECK(p->Pcie.Incomplete, "incomplete");
    g_fab.R[0].Adp[lane].Path[2 * 9] &= ~0x80000000UL;
    g_fab.R[0].Adp[lane].Path[2 * 9 + 1] |= 0x10000000UL;
    g_fab.R[0].Adp[lane].PpStuck[9] = 1;
    g_ldCalls = 0;
    memset(&g_ld, 0, sizeof(g_ld));
    fabUnplug(&g_fab, 1);
    poll(&g_cm, &g_fab, 1);
    CHECK(g_ldCalls == 0 || !g_ld.PathClosed, "a pending entry: never proven closed");
    CHECK(quarantineCount(&g_cm) >= 1, "quarantined");

    /* An incomplete USB3 record at the unplug: swept, owed until done. */
    fabInit(&g_fab);
    fabHostMinimal(&g_fab);
    dev = fabDevice(&g_fab, FAB_DEV_USB4);
    fabPlug(&g_fab, 1, dev);
    fabPreos(&g_fab, 1, 1);
    g_fab.RxQCount = 0;
    lane = fabPortLane0(&g_fab, 1);
    armSwallow(&g_fab, 0, TBT_CS_PATH, (unsigned long)lane, 2 * 11, 0, 0, 0, 100000UL);
    (void)start(&g_fab, &g_ring, &g_cm, &c);
    p = TbtU4Port(&g_cm, 1);
    CHECK(p->Usb3.Incomplete, "the USB3 walk incomplete");
    fabUnplug(&g_fab, 1);
    poll(&g_cm, &g_fab, 1);
    CHECK(p->HostResidual, "its sweep owed");
    CHECK(fabPath0(&g_fab, 0, lane, 11) & 0x80000000UL, "its lane entry still there");
    g_fab.Hook = NULL;
    t0 = g_fab.NowUs;
    poll(&g_cm, &g_fab, 2);
    CHECK(!p->HostResidual, "swept once readable");
    CHECK(hostClean(&g_fab, 1), "the host side clean, USB3 included");
    CHECK(p->Usb3GapValid && p->LastUsb3TeardownUs >= t0, "the USB3 gap from the sweep");

    /* Unplugged during the resume's own verification, both tunnels owed:
     * one teardown, one removal, nothing left deferred. */
    dev = setupMinimal(1, NULL);
    (void)countActions(&g_cm, 0);
    TbtU4Suspend(&g_cm);
    armUnplug(&g_fab, dev, TBT_CS_PATH, 1, 2 * 8, 0, 1);
    rc = TbtU4Resume(&g_cm);
    g_fab.Hook = NULL;
    poll(&g_cm, &g_fab, 2);
    CHECK(g_hkFired, "unplugged inside verification");
    CHECK_EQ(state(&g_cm, 1), TBT_U4_EMPTY, "EMPTY");
    CHECK_EQ(countActions(&g_cm, TBT_U4_ACT_TUNNEL_DOWN), 1, "the removal told once");
    CHECK_EQ(g_cm.DeferredTail[0], 0, "nothing left deferred");
    CHECK(hostClean(&g_fab, 1), "the host side clean");
    (void)rc;

    /* The rebuild's budget spent just before it reads its lane: HELD,
     * never EMPTY without removal evidence. */
    dev = setupMinimal(1, NULL);
    TbtU4Suspend(&g_cm);
    fabPowerCycle(&g_fab);
    g_jumpRouter = 0;
    g_jumpDone = 0;
    g_jumpWrite = 1;
    g_jumpAdp = (unsigned long)fabPortPcie(&g_fab, 1);
    g_jumpAddr = FAB_CAP_ADP;
    g_fab.Hook = jumpHook;
    rc = TbtU4Resume(&g_cm);
    g_fab.Hook = NULL;
    CHECK(g_jumpDone, "the budget spent during the rebuild's host work");
    CHECK_EQ(rc, TBT_E_STATE, "the failure path");
    CHECK_EQ(state(&g_cm, 1), TBT_U4_HELD, "HELD, not EMPTY");

    /* A USB3 allocation whose release fails at the unplug: owed, the
     * whole handshake retried until it succeeds. */
    dev = setupMinimal(1, NULL);
    p = TbtU4Port(&g_cm, 1);
    hu = fabPortUsb3(&g_fab, 1);
    CHECK(p->Usb3.Up, "USB3 up");
    armSwallow(&g_fab, 0, TBT_CS_ADAPTER, (unsigned long)hu, FAB_CAP_ADP + TBT_USB3_CS_3, 1,
               0, 0, 100000UL);
    fabUnplug(&g_fab, 1);
    poll(&g_cm, &g_fab, 1);
    CHECK(p->BwReleaseOwed, "the release owed");
    g_fab.Hook = NULL;
    poll(&g_cm, &g_fab, 2);
    CHECK(!p->BwReleaseOwed, "released once it could be");

    /* A permanently transitional PCIe LTSSM across a resume: decided once,
     * after one 2 s window, not two. */
    dev = setupMinimal(1, NULL);
    TbtU4Suspend(&g_cm);
    g_fab.R[0].Adp[fabPortPcie(&g_fab, 1)].LtssmForce = 1 + 2;
    t0 = g_fab.NowUs;
    (void)TbtU4Resume(&g_cm);
    g_fab.R[0].Adp[fabPortPcie(&g_fab, 1)].LtssmForce = 0;
    CHECK(!TbtU4Port(&g_cm, 1)->Pcie.Up, "transitional for 2 s: lost");
    CHECK(g_fab.NowUs - t0 < 3500000UL, "one 2 s window");

    /* The host's USB4 Port is Configured write failing: no tunnel. */
    fabInit(&g_fab);
    fabHostMinimal(&g_fab);
    dev = fabDevice(&g_fab, FAB_DEV_USB4);
    fabPlug(&g_fab, 1, dev);
    g_cfgFail = 1;
    g_fab.Hook = cfgHook;
    (void)start(&g_fab, &g_ring, &g_cm, &c);
    g_fab.Hook = NULL;
    g_cfgFail = 0;
    p = TbtU4Port(&g_cm, 1);
    CHECK_EQ(state(&g_cm, 1), TBT_U4_FAILED, "PC not written: FAILED");
    CHECK(!fabPcieUp(&g_fab, 1), "no tunnel over an unconfigured link");
    CHECK(p->PcSetHost, "its PC still owned for the teardown");
}

static int g_stage;
static int g_stageLane;
static int g_stageJump;

/* After the host's lane 1 is written (the resume's host restore), the
 * next reads of lane 0's state (the rebuild's first) are lost, or move
 * the clock past the budget. */
static int stageHook(FAB *f, unsigned long pdf, int router, unsigned long cs,
                     unsigned long adp, unsigned long addr, unsigned long n,
                     const unsigned long *data)
{
    (void)n;
    (void)data;
    if (router != 0 || cs != TBT_CS_ADAPTER || addr != FAB_CAP_LANE + 1) {
        return FAB_HOOK_GO;
    }
    if (g_stage == 0 && pdf == TBT_PDF_WRITE && adp == (unsigned long)g_stageLane + 1) {
        g_stage = 1;
        return FAB_HOOK_GO;
    }
    if (g_stage >= 1 && pdf == TBT_PDF_READ && adp == (unsigned long)g_stageLane) {
        g_stage = 2;
        if (g_stageJump) {
            f->NowUs += 1000000000UL;
            return FAB_HOOK_GO;
        }
        return FAB_HOOK_SWALLOW;
    }
    return FAB_HOOK_GO;
}

static unsigned long g_recoverAt;
static int g_recoverAdp;

/* The host's PCIe adapter back in L0 once the clock passes g_recoverAt. */
static int recoverHook(FAB *f, unsigned long pdf, int router, unsigned long cs,
                       unsigned long adp, unsigned long addr, unsigned long n,
                       const unsigned long *data)
{
    (void)pdf;
    (void)cs;
    (void)addr;
    (void)n;
    (void)data;
    if (router == 0 && adp == (unsigned long)g_recoverAdp && f->NowUs >= g_recoverAt) {
        f->R[0].Adp[g_recoverAdp].LtssmForce = 0;
    }
    return FAB_HOOK_GO;
}

static void test_review5(void)
{
    TBT_U4_CONFIG c;
    int dev;
    int k;
    LONG rc;
    ULONG gen;
    const TBT_U4_PORT *p;

    cfgDefault(&c);

    /* A coarse but running clock: a transitional LTSSM recovering at
     * 1.95 s is not lost. */
    dev = setupMinimal(1, NULL);
    TbtU4Suspend(&g_cm);
    g_fab.TickUs = 10000UL;
    g_fab.Host.TickUs = 10000UL;
    g_recoverAdp = fabPortPcie(&g_fab, 1);
    g_fab.R[0].Adp[g_recoverAdp].LtssmForce = 1 + 2;
    g_recoverAt = g_fab.NowUs + 1950000UL;
    g_fab.Hook = recoverHook;
    rc = TbtU4Resume(&g_cm);
    g_fab.Hook = NULL;
    g_fab.R[0].Adp[g_recoverAdp].LtssmForce = 0;
    g_fab.TickUs = 0;
    g_fab.Host.TickUs = 0;
    p = TbtU4Port(&g_cm, 1);
    CHECK(p->Pcie.Up && fabPcieUp(&g_fab, 1), "recovered at 1.95 s: kept");
    CHECK_EQ(rc, TBT_OK, "and verified");

    /* Lock set again across the sleep with the router still configured:
     * removal evidence, discharged as a removal. */
    dev = setupMinimal(1, NULL);
    (void)countActions(&g_cm, 0);
    TbtU4Suspend(&g_cm);
    g_fab.R[0].Adp[fabPortLane0(&g_fab, 1)].Cs[4] |= 0x80000000UL;
    rc = TbtU4Resume(&g_cm);
    CHECK_EQ(rc, TBT_OK, "re-locked: the obligation discharged by removal");
    CHECK_EQ(state(&g_cm, 1), TBT_U4_EMPTY, "torn down");
    CHECK_EQ(countActions(&g_cm, TBT_U4_ACT_TUNNEL_DOWN), 1, "the removal told once");

    /* A preserved tunnel whose router lost its configuration: HELD, and
     * that known loss alone never tears it down at the next ticks. */
    fabInit(&g_fab);
    fabHostMinimal(&g_fab);
    dev = fabDevice(&g_fab, FAB_DEV_USB4);
    fabPlug(&g_fab, 1, dev);
    fabPreos(&g_fab, 1, 0);
    g_fab.RxQCount = 0;
    (void)start(&g_fab, &g_ring, &g_cm, &c);
    p = TbtU4Port(&g_cm, 1);
    CHECK(p->Pcie.Preserved, "preserved");
    TbtU4Suspend(&g_cm);
    g_fab.R[dev].Rcs[3] &= ~0x80000000UL;
    rc = TbtU4Resume(&g_cm);
    CHECK_EQ(rc, TBT_E_STATE, "lost under a preserved tunnel: the failure path");
    CHECK_EQ(state(&g_cm, 1), TBT_U4_HELD, "HELD");
    gen = p->Generation;
    poll(&g_cm, &g_fab, 3);
    CHECK_EQ(state(&g_cm, 1), TBT_U4_HELD, "still HELD at the next ticks");
    CHECK_EQ(p->Generation, gen, "not torn down and reconnected");

    /* The rebuild's first lane read lost, then the same with the budget
     * spent there: HELD both times, the rebuild reached. */
    for (k = 0; k < 2; k++) {
        dev = setupMinimal(1, NULL);
        TbtU4Suspend(&g_cm);
        fabPowerCycle(&g_fab);
        g_stage = 0;
        g_stageLane = fabPortLane0(&g_fab, 1);
        g_stageJump = k;
        g_fab.Hook = stageHook;
        rc = TbtU4Resume(&g_cm);
        g_fab.Hook = NULL;
        CHECK_EQ(g_stage, 2, "the rebuild's lane read reached");
        CHECK_EQ(rc, TBT_E_STATE, "the failure path");
        CHECK_EQ(state(&g_cm, 1), TBT_U4_HELD, "HELD, never EMPTY without evidence");
        gen = TbtU4Port(&g_cm, 1)->Generation;
        poll(&g_cm, &g_fab, 3);
        CHECK_EQ(state(&g_cm, 1), TBT_U4_HELD, "still HELD at the next ticks");
        CHECK_EQ(TbtU4Port(&g_cm, 1)->Generation, gen, "no new connection");
    }

    /* Transitional past 2 s: recovery at 2.05 s or never is a loss. */
    for (k = 0; k < 2; k++) {
        unsigned long t0;

        dev = setupMinimal(1, NULL);
        TbtU4Suspend(&g_cm);
        g_fab.TickUs = 10000UL;
        g_fab.Host.TickUs = 10000UL;
        g_recoverAdp = fabPortPcie(&g_fab, 1);
        g_fab.R[0].Adp[g_recoverAdp].LtssmForce = 1 + 2;
        t0 = g_fab.NowUs;
        g_recoverAt = k == 0 ? t0 + 2050000UL : 0xFFFFFFFFUL;
        g_fab.Hook = recoverHook;
        (void)TbtU4Resume(&g_cm);
        g_fab.Hook = NULL;
        g_fab.R[0].Adp[g_recoverAdp].LtssmForce = 0;
        g_fab.TickUs = 0;
        g_fab.Host.TickUs = 0;
        CHECK(!TbtU4Port(&g_cm, 1)->Pcie.Up, k == 0 ? "recovered only at 2.05 s: lost" :
                                                      "never recovered: lost");
        CHECK(g_fab.NowUs - t0 < 3000000UL, "decided on one 2 s deadline");
    }

    /* A configuration loss whose host closure fails only at the bandwidth
     * release: the release retried at the idle ticks, no reconnection. */
    dev = setupMinimal(1, NULL);
    p = TbtU4Port(&g_cm, 1);
    TbtU4Suspend(&g_cm);
    fabPowerCycle(&g_fab);
    armSwallow(&g_fab, 0, TBT_CS_ADAPTER, (unsigned long)fabPortUsb3(&g_fab, 1),
               FAB_CAP_ADP + TBT_USB3_CS_3, 1, 0, 0, 100000UL);
    rc = TbtU4Resume(&g_cm);
    CHECK_EQ(rc, TBT_E_STATE, "the failure path");
    CHECK(p->BwReleaseOwed, "the release owed");
    g_fab.Hook = NULL;
    gen = p->Generation;
    poll(&g_cm, &g_fab, 3);
    CHECK(!p->BwReleaseOwed, "released at the idle ticks");
    CHECK_EQ(p->Generation, gen, "no reconnection");

    /* A link's removal with the quarantine table full: a Path Enable that
     * will not clear is retried at every pass even after its entry is
     * invalid; done once it clears. */
    {
        int child;
        int lane2;
        int i;

        fabInit(&g_fab);
        fabHostMinimal(&g_fab);
        dev = fabDevice(&g_fab, FAB_DEV_USB4);
        child = fabChild(&g_fab, dev);
        fabPlug(&g_fab, 1, dev);
        fabPreos(&g_fab, 1, 0);
        fabPlugChild(&g_fab, dev, child, 1);
        g_fab.R[dev].Adp[7].Path[2 * 8] = 0x80000000UL | 8UL | (3UL << 11) | (7UL << 17);
        g_fab.R[dev].Adp[7].Cs[FAB_CAP_ADP] |= 0x80000000UL;
        g_fab.R[dev].Adp[3].Path[2 * 9] = 0x80000000UL | 8UL | (7UL << 11) | (16UL << 17);
        g_fab.RxQCount = 0;
        (void)start(&g_fab, &g_ring, &g_cm, &c);
        lane2 = fabPortLane0(&g_fab, 2);
        for (i = 0; i < (int)TBT_U4_QUARANTINE; i++) {
            int a = i < 24 ? lane2 : lane2 + 1;
            int hp = i < 24 ? 8 + i : 8 + (i - 24);
            g_fab.R[0].Adp[a].Path[2 * hp + 1] |= 0x10000000UL;
            g_fab.R[0].Adp[a].PpStuck[hp] = 1;
            g_cm.Quarantine[i].Used = TRUE;
            g_cm.Quarantine[i].Enable = FALSE;
            g_cm.Quarantine[i].Route.Hi = 0;
            g_cm.Quarantine[i].Route.Lo = 0;
            g_cm.Quarantine[i].Adapter = (ULONG)a;
            g_cm.Quarantine[i].Hop = (ULONG)hp;
        }
        armSwallow(&g_fab, dev, TBT_CS_ADAPTER, 7, FAB_CAP_ADP, 1, 0x80000000UL, 0, 100000UL);
        fabUnplugChild(&g_fab, dev);
        poll(&g_cm, &g_fab, 3);
        CHECK(g_cm.Links[0].Used && g_cm.Links[0].RemovalPending, "pending while PE stays set");
        CHECK_EQ(g_fab.R[dev].Adp[7].Path[2 * 8] & 0x80000000UL, 0, "its entry invalid");
        g_fab.Hook = NULL;
        poll(&g_cm, &g_fab, 2);
        CHECK(!g_cm.Links[0].Used, "done once Path Enable clears");
        CHECK_EQ(g_fab.R[dev].Adp[7].Cs[FAB_CAP_ADP] & 0x80000000UL, 0, "Path Enable clear");
        for (i = 0; i < (int)TBT_U4_QUARANTINE; i++) {
            g_cm.Quarantine[i].Used = FALSE;
        }
    }
    (void)dev;
}

static int g_closeDev;
static int g_closeLane;
static int g_closeStage;

/* After the device's header is read (the resume's classification), the
 * first read of the host lane's entries (the host sweep) moves the clock
 * past the budget. */
static int closeHook(FAB *f, unsigned long pdf, int router, unsigned long cs,
                     unsigned long adp, unsigned long addr, unsigned long n,
                     const unsigned long *data)
{
    (void)n;
    (void)data;
    if (g_closeStage == 0 && pdf == TBT_PDF_READ && router == g_closeDev &&
        cs == TBT_CS_ROUTER && addr == 0) {
        g_closeStage = 1;
    } else if (g_closeStage == 1 && pdf == TBT_PDF_READ && router == 0 &&
               cs == TBT_CS_PATH && adp == (unsigned long)g_closeLane) {
        g_closeStage = 2;
        f->NowUs += 1000000000UL;
    }
    return FAB_HOOK_GO;
}

static void test_review7(void)
{
    int dev;
    int k;
    LONG rc;
    ULONG gen;
    const TBT_U4_PORT *p;

    /* Lock is never written in a lane 1 adapter (Spec 8.2.2.1). */
    dev = setupMinimal(1, NULL);
    CHECK_EQ(g_fab.Lane1LockWrites, 0, "connect: lane 1's ADP_CS_4 never written");

    /* Only lane 1's Lock set across the sleep: no evidence, verified. */
    (void)countActions(&g_cm, 0);
    TbtU4Suspend(&g_cm);
    g_fab.R[0].Adp[fabPortLane0(&g_fab, 1) + 1].Cs[4] |= 0x80000000UL;
    rc = TbtU4Resume(&g_cm);
    CHECK_EQ(rc, TBT_OK, "lane 1 Lock alone: verified");
    CHECK_EQ(state(&g_cm, 1), TBT_U4_UP, "still UP");
    CHECK_EQ(countActions(&g_cm, TBT_U4_ACT_TUNNEL_DOWN), 0, "no removal");
    CHECK_EQ(g_fab.Lane1LockWrites, 0, "resume: lane 1's ADP_CS_4 never written");

    /* Lane 0's Lock seen and cleared, then the classification cut short:
     * the evidence kept, acted on once reads work again. */
    dev = setupMinimal(1, NULL);
    p = TbtU4Port(&g_cm, 1);
    (void)countActions(&g_cm, 0);
    TbtU4Suspend(&g_cm);
    g_fab.R[0].Adp[fabPortLane0(&g_fab, 1)].Cs[4] |= 0x80000000UL;
    armSwallow(&g_fab, dev, TBT_CS_ROUTER, 0, 0, 0, 0, 0, 100000UL);
    rc = TbtU4Resume(&g_cm);
    g_fab.Hook = NULL;
    CHECK_EQ(rc, TBT_E_STATE, "cut short: the failure path");
    CHECK(p->RelockSeen, "the re-lock kept");
    gen = p->Generation;
    poll(&g_cm, &g_fab, 2);
    CHECK(p->Generation != gen, "acted on: torn down and connected afresh");
    CHECK_EQ(countActions(&g_cm, TBT_U4_ACT_TUNNEL_DOWN), 1, "one removal");

    /* A resume's host closure cut short by the budget: finished at the
     * idle ticks while the port stays HELD, the release with it. */
    dev = setupMinimal(1, NULL);
    p = TbtU4Port(&g_cm, 1);
    TbtU4Suspend(&g_cm);
    fabPowerCycle(&g_fab);
    {
        /* the host's side kept across the sleep (only the device lost its
         * configuration): a lane entry and the PCIe Down's HopID 8 */
        int ln = fabPortLane0(&g_fab, 1);
        int hp = fabPortPcie(&g_fab, 1);

        g_fab.R[0].Adp[ln].Path[2 * 9] = 0x80000000UL | 8UL | ((unsigned long)hp << 11);
        g_fab.R[0].Adp[ln].Path[2 * 9 + 1] = 0x01000000UL;
        g_fab.R[0].Adp[hp].Path[2 * 8] = 0x80000000UL | 8UL | ((unsigned long)ln << 11);
    }
    g_closeDev = dev;
    g_closeLane = fabPortLane0(&g_fab, 1);
    g_closeStage = 0;
    g_fab.Hook = closeHook;
    rc = TbtU4Resume(&g_cm);
    g_fab.Hook = NULL;
    CHECK_EQ(g_closeStage, 2, "the budget spent inside the host sweep");
    CHECK_EQ(rc, TBT_E_STATE, "cut short: the failure path");
    CHECK(p->HostCloseOwed, "the closure owed");
    CHECK(fabPath0(&g_fab, 0, fabPortLane0(&g_fab, 1), 9) & 0x80000000UL,
          "the host lane entry still outstanding");
    gen = p->Generation;
    poll(&g_cm, &g_fab, 3);
    CHECK(!p->HostCloseOwed && !p->HostResidual, "finished at the idle ticks");
    CHECK(!p->BwReleaseOwed, "the release with it");
    CHECK(!p->Pcie.Up && !p->Usb3.Up, "its tunnels down");
    CHECK(hostClean(&g_fab, 1), "the host side clean");
    CHECK_EQ(fabPath0(&g_fab, 0, fabPortLane0(&g_fab, 1), 9) & 0x80000000UL, 0,
             "the outstanding entry invalidated by the idle sweep");
    CHECK_EQ(state(&g_cm, 1), TBT_U4_HELD, "still HELD");
    CHECK_EQ(p->Generation, gen, "no reconnection");

    /* Transitional since a tick before the sleep: the deadline runs from
     * then; a recovery 2.5 s after the first sample is a loss. */
    dev = setupMinimal(1, NULL);
    p = TbtU4Port(&g_cm, 1);
    g_recoverAdp = fabPortPcie(&g_fab, 1);
    g_fab.R[0].Adp[g_recoverAdp].LtssmForce = 1 + 2;
    poll(&g_cm, &g_fab, 1);
    CHECK(p->Transitional, "seen transitional at a tick");
    g_fab.Host.Sleep(g_fab.Host.Context, 1500);
    TbtU4Suspend(&g_cm);
    g_recoverAt = g_fab.NowUs + 1000000UL;
    g_fab.Hook = recoverHook;
    (void)TbtU4Resume(&g_cm);
    g_fab.Hook = NULL;
    g_fab.R[0].Adp[g_recoverAdp].LtssmForce = 0;
    CHECK(!p->Pcie.Up, "transitional across the sleep past 2 s: lost");

    /* A preserved tunnel whose router lost its configuration, the resume's
     * classification cut short after Lock was cleared: once reads work,
     * classified as the known loss - no teardown, no reconnection. */
    {
        TBT_U4_CONFIG c;

        cfgDefault(&c);
        fabInit(&g_fab);
        fabHostMinimal(&g_fab);
        dev = fabDevice(&g_fab, FAB_DEV_USB4);
        fabPlug(&g_fab, 1, dev);
        fabPreos(&g_fab, 1, 0);
        g_fab.RxQCount = 0;
        (void)start(&g_fab, &g_ring, &g_cm, &c);
        p = TbtU4Port(&g_cm, 1);
        CHECK(p->Pcie.Preserved, "preserved");
        (void)countActions(&g_cm, 0);
        TbtU4Suspend(&g_cm);
        g_fab.R[dev].Rcs[3] &= ~0x80000000UL;
        g_fab.R[0].Adp[fabPortLane0(&g_fab, 1)].Cs[4] |= 0x80000000UL;
        armSwallow(&g_fab, dev, TBT_CS_ROUTER, 0, 0, 0, 0, 0, 100000UL);
        rc = TbtU4Resume(&g_cm);
        g_fab.Hook = NULL;
        CHECK_EQ(rc, TBT_E_STATE, "cut short: the failure path");
        CHECK(p->ClassifyPending, "left unclassified");
        gen = p->Generation;
        poll(&g_cm, &g_fab, 3);
        CHECK_EQ(state(&g_cm, 1), TBT_U4_HELD, "classified as the known loss: still HELD");
        CHECK(p->LostKnown && !p->ClassifyPending, "the loss known");
        CHECK_EQ(p->Generation, gen, "no teardown, no reconnection");
        CHECK_EQ(countActions(&g_cm, TBT_U4_ACT_TUNNEL_DOWN), 0, "no removal");
    }

    /* The same with the resume unable to clear Lock at all: classified at
     * the next readable tick, unlocking first - a lost configuration stays
     * HELD; a router still configured is one removal. */
    for (k = 0; k < 2; k++) {
        TBT_U4_CONFIG c;

        cfgDefault(&c);
        fabInit(&g_fab);
        fabHostMinimal(&g_fab);
        dev = fabDevice(&g_fab, FAB_DEV_USB4);
        fabPlug(&g_fab, 1, dev);
        fabPreos(&g_fab, 1, 0);
        g_fab.RxQCount = 0;
        (void)start(&g_fab, &g_ring, &g_cm, &c);
        p = TbtU4Port(&g_cm, 1);
        (void)countActions(&g_cm, 0);
        TbtU4Suspend(&g_cm);
        if (k == 0) {
            g_fab.R[dev].Rcs[3] &= ~0x80000000UL;
        }
        g_fab.R[0].Adp[fabPortLane0(&g_fab, 1)].Cs[4] |= 0x80000000UL;
        armSwallow(&g_fab, 0, TBT_CS_ADAPTER, (unsigned long)fabPortLane0(&g_fab, 1), 4, 1,
                   0, 0, 100000UL);
        rc = TbtU4Resume(&g_cm);
        g_fab.Hook = NULL;
        CHECK_EQ(rc, TBT_E_STATE, "Lock not cleared: the failure path");
        CHECK(p->ClassifyPending, "left unclassified");
        gen = p->Generation;
        poll(&g_cm, &g_fab, 3);
        if (k == 0) {
            CHECK_EQ(state(&g_cm, 1), TBT_U4_HELD, "lost configuration: still HELD");
            CHECK(p->LostKnown, "the loss known");
            CHECK_EQ(p->Generation, gen, "no teardown, no reconnection");
            CHECK_EQ(countActions(&g_cm, TBT_U4_ACT_TUNNEL_DOWN), 0, "no removal");
        } else {
            CHECK(p->Generation != gen, "still configured, re-locked: torn down");
            CHECK_EQ(countActions(&g_cm, TBT_U4_ACT_TUNNEL_DOWN), 1, "one removal");
        }
    }

    /* Thunderbolt 3: lane 0's Lock written, lane 1's never. */
    {
        TBT_U4_CONFIG c;

        cfgDefault(&c);
        fabInit(&g_fab);
        fabHostMinimal(&g_fab);
        dev = fabDevice(&g_fab, FAB_DEV_TBT3_TR);
        fabPlug(&g_fab, 1, dev);
        (void)start(&g_fab, &g_ring, &g_cm, &c);
        CHECK_EQ(state(&g_cm, 1), TBT_U4_UP, "Thunderbolt 3: UP");
        CHECK_EQ(g_fab.Lane1LockWrites, 0, "lane 1 never written");
        CHECK(fabFindWrite(&g_fab, 0, 0, TBT_CS_ADAPTER, (unsigned long)fabPortLane0(&g_fab, 1),
                           4) >= 0, "lane 0's Lock written");
    }
    (void)dev;
}

/* usb4-emu's `asm4242` host model (ASMedia ASM4242, AM5 X870): the CSM
 * desktop case where this software CM is the only CM. */
static void test_asm4242(void)
{
    TBT_U4_CONFIG c;
    int dev;
    int dev2;
    int lane;
    unsigned long cr;
    const TBT_U4_PORT *p;

    cfgDefault(&c);
    fabInit(&g_fab);
    fabHostAsm4242(&g_fab);
    dev = fabDevice(&g_fab, FAB_DEV_USB4);
    dev2 = fabDevice(&g_fab, FAB_DEV_USB4);
    fabPlug(&g_fab, 1, dev);
    g_ldCalls = 0;
    (void)start(&g_fab, &g_ring, &g_cm, &c);
    p = TbtU4Port(&g_cm, 1);
    lane = fabPortLane0(&g_fab, 1);
    CHECK_EQ(g_cm.PortCount, 2, "asm4242: two USB4 ports");
    CHECK_EQ((fabRtr(&g_fab, 0, 1) >> 8) & 0x3FUL, 7, "the Host Interface 7 as the upstream adapter");
    CHECK_EQ(state(&g_cm, 1), TBT_U4_UP, "asm4242: a cold device UP");
    CHECK(p->Pcie.Up && p->Usb3.Up && p->Bonded, "PCIe and USB3, bonded");
    CHECK_EQ(p->PcieDown, 8, "PCIe Down 8 on port 1 (found, not assumed)");
    CHECK_EQ(p->Usb3Down, 10, "USB3 Down 10 on port 1");
    CHECK(p->MapValid && p->MapRid == 0x0200, "the tunnel port from the mapping: 02:00.0");
    cr = (fabPath0(&g_fab, 0, lane, p->Pcie.Hu) >> 17) & 0x7F;
    CHECK(cr != 0 && cr <= 32, "host lane credits from Buffer Allocation");
    CHECK_EQ(g_fab.Lane1LockWrites, 0, "lane 1 Lock never written");

    fabPlug(&g_fab, 2, dev2);
    poll(&g_cm, &g_fab, 3);
    CHECK_EQ(state(&g_cm, 2), TBT_U4_UP, "asm4242: hot plug on port 2 UP");
    CHECK_EQ(TbtU4Port(&g_cm, 2)->PcieDown, 9, "PCIe Down 9 on port 2");
    CHECK_EQ(TbtU4Port(&g_cm, 2)->MapRid, 0x0208, "its tunnel port 02:01.0");
    fabUnplug(&g_fab, 2);
    poll(&g_cm, &g_fab, 2);
    CHECK_EQ(state(&g_cm, 2), TBT_U4_EMPTY, "its unplug EMPTY");
    CHECK(hostClean(&g_fab, 2), "its host side clean");
    CHECK(g_ld.PathClosed, "the path proven closed");
    CHECK_EQ(state(&g_cm, 1), TBT_U4_UP, "port 1 untouched");

    /* A device router whose lanes and Buffer Allocation exceed the 7-bit
     * credit field: clamped to 127, never truncated. */
    fabInit(&g_fab);
    fabHostAsm4242(&g_fab);
    dev = fabDevice(&g_fab, FAB_DEV_USB4);
    g_fab.R[dev].BaData[0] = 4UL | (600UL << 16);
    fabPlug(&g_fab, 1, dev);
    /* after the plug, which resets the router's lanes */
    g_fab.R[dev].Adp[1].Cs[4] = (g_fab.R[dev].Adp[1].Cs[4] & ~(0x3FFUL << 20)) | (1000UL << 20);
    g_fab.R[dev].Adp[2].Cs[4] = (g_fab.R[dev].Adp[2].Cs[4] & ~(0x3FFUL << 20)) | (1000UL << 20);
    (void)start(&g_fab, &g_ring, &g_cm, &c);
    p = TbtU4Port(&g_cm, 1);
    CHECK_EQ(state(&g_cm, 1), TBT_U4_UP, "UP");
    cr = (fabPath0(&g_fab, dev, 1, p->Pcie.Hd) >> 17) & 0x7F;
    CHECK_EQ(cr, 127, "device lane credits clamped to 127 (not 600 & 0x7F)");

    /* The pre-OS case on asm4242 (the model's preos-cm=on): preserved. */
    fabInit(&g_fab);
    fabHostAsm4242(&g_fab);
    dev = fabDevice(&g_fab, FAB_DEV_USB4);
    fabPlug(&g_fab, 1, dev);
    fabPreos(&g_fab, 1, 1);
    g_fab.RxQCount = 0;
    (void)start(&g_fab, &g_ring, &g_cm, &c);
    p = TbtU4Port(&g_cm, 1);
    CHECK(state(&g_cm, 1) == TBT_U4_UP && p->Pcie.Preserved, "asm4242 pre-OS: preserved");
    fabUnplug(&g_fab, 1);
    poll(&g_cm, &g_fab, 2);
    CHECK_EQ(state(&g_cm, 1), TBT_U4_EMPTY, "its unplug EMPTY");
    CHECK(hostClean(&g_fab, 1), "its host side clean, USB3 included");
}

/* TBT_U4_CONFIG.DeferFirstPoll (task 40-A.6): the start connects nothing
 * found empty; the caller's first poll does, and a preserved tunnel is
 * preserved either way. */
static void test_defer_first_poll(void)
{
    TBT_U4_CONFIG c;
    int dev;
    TBT_U4_ACTION a;

    cfgDefault(&c);
    c.DeferFirstPoll = TRUE;
    fabInit(&g_fab);
    fabHostMinimal(&g_fab);
    dev = fabDevice(&g_fab, FAB_DEV_USB4);
    fabPlug(&g_fab, 1, dev);
    CHECK_EQ(start(&g_fab, &g_ring, &g_cm, &c), TBT_OK, "deferred: start");
    CHECK_EQ(g_cm.Stage, TBT_U4_STAGE_RUNNING, "deferred: running");
    CHECK(g_cm.Ports[0].MapValid, "deferred: the mapping known at start");
    CHECK_EQ(state(&g_cm, 1), TBT_U4_EMPTY, "deferred: the cold device not yet connected");
    CHECK(!TbtU4Port(&g_cm, 1)->Pcie.Up, "deferred: no tunnel built in the start");
    CHECK(!TbtU4NextAction(&g_cm, &a), "deferred: no action queued by the start");
    poll(&g_cm, &g_fab, 1);
    CHECK_EQ(state(&g_cm, 1), TBT_U4_UP, "deferred: the first poll connects it");
    CHECK_EQ(countActions(&g_cm, TBT_U4_ACT_TUNNEL_UP), 1, "deferred: one tunnel up");

    fabInit(&g_fab);
    fabHostMinimal(&g_fab);
    dev = fabDevice(&g_fab, FAB_DEV_USB4);
    fabPlug(&g_fab, 1, dev);
    fabPreos(&g_fab, 1, 1);
    g_fab.RxQCount = 0;
    (void)start(&g_fab, &g_ring, &g_cm, &c);
    CHECK(state(&g_cm, 1) == TBT_U4_UP && TbtU4Port(&g_cm, 1)->Pcie.Preserved,
          "deferred: a pre-OS tunnel preserved by the start itself");
    CHECK(!TbtU4AllPortsDisconnected(&g_cm), "a router connected: not all disconnected");

    /* TbtU4AllPortsDisconnected: a fresh reading of every lane 0. */
    fabInit(&g_fab);
    fabHostMinimal(&g_fab);
    cfgDefault(&c);
    (void)start(&g_fab, &g_ring, &g_cm, &c);
    CHECK(TbtU4AllPortsDisconnected(&g_cm), "nothing plugged: every lane 0 in CLd");
    g_fab.Stopped = 1;
    CHECK(!TbtU4AllPortsDisconnected(&g_cm), "a lane unreadable: no evidence");
    g_fab.Stopped = 0;
    dev = fabDevice(&g_fab, FAB_DEV_USB4);
    fabPlug(&g_fab, 2, dev);
    CHECK(!TbtU4AllPortsDisconnected(&g_cm),
          "a router plugged since the last check: read connected");
    TbtU4Stop(&g_cm);
    CHECK(!TbtU4AllPortsDisconnected(&g_cm), "stopped: no reading");
}

int main(void)
{
    test_cold_minimal();
    test_defer_first_poll();
    test_hot_plug_unplug();
    test_unplug_everywhere();
    test_unplug_nested();
    test_quick_replug();
    test_bonding();
    test_rops();
    test_detect_and_hca();
    test_approve_and_usb3_off();
    test_quirks();
    test_preos();
    test_dp_preserved();
    test_statement();
    test_caps_preserved();
    test_inert_and_domains();
    test_adl();
    test_tbt3();
    test_links();
    test_loss_and_eject();
    test_suspend_resume();
    test_coarse_clock();
    test_review_enumeration();
    test_review_lost_answer();
    test_review_held_inventory();
    test_review_quarantine();
    test_review_resume();
    test_review_bandwidth_and_tmu();
    test_review_ba_and_closure();
    test_review_identity();
    test_review2_budget();
    test_review2_identity_and_links();
    test_review2_restore();
    test_review3();
    test_review4();
    test_review5();
    test_review7();
    test_asm4242();
    printf("test_u4cm: %d checks, %d failures\n", checks, failures);
    return failures;
}
