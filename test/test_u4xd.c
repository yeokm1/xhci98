/*
 * test_u4xd.c - host vectors for the software connection manager's
 * inter-domain links (src\cm\tbt_u4cm.c, its "Inter-domain links"
 * section; roadmap-usb4.md task 45.59 step S3; design record 35 sections
 * 3.3, 3.5, 3.8, 4 and 5.1).
 *
 * The fake fabric of u4_fakefab.c is this domain; a device router plugged
 * on a port stands for the far host's router: every request this domain
 * sends to it is answered with ERR_ENUM, as another domain's enumerated
 * router answers (Guide 3.3), and its two lane adapters are the far
 * host's lanes. The far host's connection manager is a second instance of
 * the pure engine (tbt_xdp.c) with operations of the test's own: the
 * inter-domain packets this domain sends reach it through the fabric's
 * FAB_XDTX hook, and its packets are injected into this domain's ring 0.
 * The engine itself has its own vectors (test_tbt_xdp); these check what
 * the connection manager adds: the port's state, the Inter-Domain bit,
 * the host lanes' bonding or lane 1, the two path entries of section 5.1
 * and their credits, the service's operations, the ends of section 3.8
 * and XhciCmNet 0. Expected values are worked from record 35 and the
 * specifications, not from the code under test.
 */

#include <stdio.h>
#include <string.h>
#include "../src/cm/tbt_u4cm.h"
#include "../src/cm/tbt_hw.h"
#include "../src/cm/tbt_xdp.h"
#include "u4_fakefab.h"
#include "test_harness.h"

static FAB g_fab;
static TBT_RING g_ring;
static TBT_U4CM g_cm;

/* The far hosts: one per port of the minimal host model. */
typedef struct LP {
    int On;             /* its router answers ERR_ENUM */
    int Live;           /* its engine runs and receives */
    int Dev;
    int Port;
    TBT_XDP_PEER E;
    unsigned long Ups;
    unsigned long Downs;
    unsigned long TxHop;
    unsigned long RxHop;
} LP;
static LP g_lp[2];

/* The service operations this domain's driver would give. */
static unsigned long g_ringsUp;
static unsigned long g_ringsDown;
static unsigned long g_linkUp;
static unsigned long g_linkDown;
static unsigned long g_lastRing;
static long g_ringsUpAt;
static long g_ringsDownAt;
static long g_linkUpAt;
static long g_linkDownAt;
static int g_lastE2e;
static LONG g_ringsAnswer;
static unsigned long g_uuidResp;    /* UUID Responses this domain sent */
static int g_failCs7;               /* ROUTER_CS_7 reads of this domain left unanswered */
static long g_flood;                /* Link Status Requests the far host has left to send */
static unsigned long g_floodSn;
static unsigned long g_linkResp;    /* Link Status Responses this domain sent */

/* One Link Status Request from the far host on port 1: answering it reads
 * the host lane (tbtU4XdLinkRead), a control request of this domain. */
static void floodOne(struct FAB *f)
{
    ULONG buf[TBT_XDP_MAX_WORDS];
    unsigned long w[TBT_XDP_MAX_WORDS];
    TBT_ROUTE route;
    ULONG n;
    ULONG i;

    route.Hi = 0;
    route.Lo = (ULONG)fabPortLane0(f, 1);
    n = TbtXdpBuildLinkStatusRequest(buf, &route, g_floodSn++ & 3UL);
    for (i = 0; i < n; i++) {
        w[i] = buf[i];
    }
    fabInject(f, TBT_PDF_XDOMAIN_REQ, w, n);
}

static LONG netRingsUp(PVOID c, ULONG port, ULONG ring, BOOLEAN e2e)
{
    (void)c; (void)port;
    g_ringsUp++;
    g_ringsUpAt = (long)g_fab.WrCount;
    g_lastRing = ring;
    g_lastE2e = e2e ? 1 : 0;
    return g_ringsAnswer;
}

static VOID netRingsDown(PVOID c, ULONG port, BOOLEAN lost)
{
    (void)c; (void)port; (void)lost;
    g_ringsDown++;
    g_ringsDownAt = (long)g_fab.WrCount;
}

static VOID netLink(PVOID c, ULONG port, BOOLEAN up, const UCHAR *mac)
{
    (void)c; (void)port; (void)mac;
    if (up) {
        g_linkUp++;
        g_linkUpAt = (long)g_fab.WrCount;
    } else {
        g_linkDown++;
        g_linkDownAt = (long)g_fab.WrCount;
    }
}

static LP *lpOfLane(unsigned long lane)
{
    int i;

    for (i = 0; i < 2; i++) {
        if (g_lp[i].On && (unsigned long)fabPortLane0(&g_fab, g_lp[i].Port) == lane) {
            return &g_lp[i];
        }
    }
    return NULL;
}

/* The far router: every request to it answered by ERR_ENUM. */
static int lpHook(struct FAB *f, unsigned long pdf, int router, unsigned long cs,
                  unsigned long adp, unsigned long addr, unsigned long n,
                  const unsigned long *data)
{
    unsigned long dw[3];
    int i;

    (void)pdf; (void)cs; (void)adp; (void)addr; (void)n; (void)data;
    if (g_flood > 0) {
        /* A peer that answers each control request of this domain with
         * one more request. */
        g_flood--;
        floodOne(f);
    }
    if (g_failCs7 > 0 && router == 0 && cs == TBT_CS_ROUTER && addr == TBT_ROUTER_CS_7) {
        g_failCs7--;
        return FAB_HOOK_SWALLOW;
    }
    for (i = 0; i < 2; i++) {
        if (g_lp[i].On && router == g_lp[i].Dev) {
            dw[0] = 0x80000000UL;
            dw[1] = (unsigned long)fabPortLane0(f, g_lp[i].Port);
            dw[2] = TBT_EV_ERR_ENUM | ((unsigned long)f->R[router].UpAdp << 8);
            fabInject(f, TBT_PDF_NOTIFY, dw, 3);
            return FAB_HOOK_SWALLOW;
        }
    }
    return FAB_HOOK_GO;
}

static void lpTx(struct FAB *f, unsigned long pdf, const unsigned long *dw,
                 unsigned long words)
{
    LP *lp;
    ULONG w[64];
    unsigned long i;
    TBT_XDP_PKT k;

    (void)f;
    if (words < 2) {
        return;
    }
    for (i = 0; i < words && i < 64; i++) {
        w[i] = dw[i];
    }
    if (TbtXdpDecode(pdf, w, words, &k) == TBT_XDP_K_DISCOVERY && k.Response &&
        k.Type == TBT_XDP_UUID_RESP) {
        g_uuidResp++;
    }
    if (TbtXdpDecode(pdf, w, words, &k) == TBT_XDP_K_DISCOVERY && k.Response &&
        k.Type == TBT_XDP_LINK_STATUS_RESP) {
        g_linkResp++;
    }
    lp = lpOfLane(dw[1]);
    if (lp == NULL || !lp->Live) {
        return;
    }
    for (i = 0; i < words && i < 64; i++) {
        w[i] = dw[i];
    }
    (void)TbtXdpPeerOnPacket(&lp->E, pdf, w, words);
}

/* ---- the far engine's operations --------------------------------------- */

static LONG lpSend(PVOID c, ULONG pdf, const ULONG *dw, ULONG words)
{
    unsigned long w[64];
    ULONG i;

    (void)c;
    for (i = 0; i < words && i < 64; i++) {
        w[i] = dw[i];
    }
    fabInject(&g_fab, pdf, w, words);
    return TBT_OK;
}

static ULONG lpNow(PVOID c)
{
    (void)c;
    return g_fab.NowUs / 1000UL;
}

static VOID lpLog(PVOID c, const char *label, ULONG value)
{
    (void)c; (void)label; (void)value;
}

static LONG lpLinkRead(PVOID c, PULONG slw, PULONG tlw, PULONG lanes)
{
    LP *lp = (LP *)c;
    unsigned up = g_fab.R[lp->Dev].UpAdp;
    unsigned long cs0 = fabAdp(&g_fab, lp->Dev, (int)up, FAB_CAP_LANE);
    unsigned long cs1 = fabAdp(&g_fab, lp->Dev, (int)up, FAB_CAP_LANE + 1);

    *slw = (cs0 >> 20) & 3UL;
    *tlw = (cs1 >> 4) & 3UL;
    *lanes = ((cs1 >> 20) & 0x3FUL) == 2UL ? 2UL : 1UL;
    return TBT_OK;
}

static LONG lpLinkSetTarget(PVOID c, ULONG tlw)
{
    LP *lp = (LP *)c;
    unsigned up = g_fab.R[lp->Dev].UpAdp;
    unsigned k;
    unsigned long v;

    for (k = 0; k < 2; k++) {
        v = fabAdp(&g_fab, lp->Dev, (int)(up + k), FAB_CAP_LANE + 1);
        fabAdapterPoke(&g_fab, lp->Dev, up + k, FAB_CAP_LANE + 1,
                       (v & ~0x30UL) | ((tlw << 4) & 0x30UL));
    }
    return TBT_OK;
}

static LONG lpLinkBond(PVOID c)
{
    LP *lp = (LP *)c;
    unsigned up = g_fab.R[lp->Dev].UpAdp;

    fabAdapterPoke(&g_fab, lp->Dev, up, FAB_CAP_LANE + 1,
                   fabAdp(&g_fab, lp->Dev, (int)up, FAB_CAP_LANE + 1) | 0x8000UL);
    return TBT_OK;
}

static VOID lpLane1Off(PVOID c)
{
    (void)c;
}

static LONG lpUp(PVOID c, ULONG txHop, ULONG rxHop)
{
    LP *lp = (LP *)c;

    lp->Ups++;
    lp->TxHop = txHop;
    lp->RxHop = rxHop;
    return TBT_OK;
}

static VOID lpDown(PVOID c, BOOLEAN gone)
{
    (void)gone;
    ((LP *)c)->Downs++;
}

/* The far host on `port`: its UUID's first DW `uuid0` (the comparison of
 * IDS 3.4.1 starts at DW 0's low byte), the network directory offered. */
static void lpStart(LP *lp, int port, unsigned long uuid0, int live)
{
    TBT_XDP_OPS ops;
    TBT_XDP_LOCAL props;
    TBT_ROUTE route;
    TBT_UUID u;
    UCHAR mac[6];
    int i;

    memset(&ops, 0, sizeof(ops));
    ops.Context = lp;
    ops.Send = lpSend;
    ops.NowMs = lpNow;
    ops.Log = lpLog;
    ops.LinkRead = lpLinkRead;
    ops.LinkSetTarget = lpLinkSetTarget;
    ops.LinkBond = lpLinkBond;
    ops.LinkLane1Off = lpLane1Off;
    ops.ServiceUp = lpUp;
    ops.ServiceDown = lpDown;
    memset(&props, 0, sizeof(props));
    props.VendorId = 0x1D6BUL;
    props.VendorText = "Linux";
    props.DeviceId = 3;
    props.DeviceText = "peer";
    props.MaxHopId = 15;
    props.Network = TRUE;
    props.Prtcstns = 7;
    u.Dw[0] = uuid0;
    u.Dw[1] = 0x01020304UL;
    u.Dw[2] = 0xFFFFFFFFUL;
    u.Dw[3] = 0xFFFFFFFFUL;
    for (i = 0; i < 6; i++) {
        mac[i] = (UCHAR)(0x02 + i);
    }
    route.Hi = 0;
    route.Lo = (ULONG)fabPortLane0(&g_fab, port);
    lp->Port = port;
    lp->Ups = 0;
    lp->Downs = 0;
    TbtXdpPeerInit(&lp->E, &ops, &route, &u, NULL, FALSE, TRUE, &props, mac, 15, NULL);
    lp->Live = live;
    if (live) {
        TbtXdpPeerStart(&lp->E);
    }
}

/* ---- the domain -------------------------------------------------------- */

#define OUR_UUID0   0x00004411UL    /* DW 0's low byte 11h */
#define HI_ADP      9

/* ROUTER_CS_0 of the host router for the next setup(), 0 for the fake
 * fabric's own (test_asm_no_e2e). */
static ULONG g_hostCs0;

/* The host router's baMaxHI (Buffer Allocation index 5) for the next
 * setup(), 0 for none (test_credit_clamp). */
static ULONG g_hostBaHi;

static void setup(int net, int bonding)
{
    TBT_U4_CONFIG c;
    TBT_U4_NET n;
    int i;

    fabInit(&g_fab);
    fabHostMinimal(&g_fab);
    if (g_hostCs0 != 0) {
        g_fab.R[0].Rcs[0] = g_hostCs0;
    }
    if (g_hostBaHi != 0) {
        g_fab.R[0].BaData[g_fab.R[0].BaLen++] = TBT_BA_MAX_HI | (g_hostBaHi << 16);
    }
    g_fab.R[0].Rcs[7] = OUR_UUID0;
    g_fab.R[0].Rcs[8] = 0x55667788UL;
    memset(g_lp, 0, sizeof(g_lp));
    for (i = 0; i < 2; i++) {
        g_lp[i].Dev = fabDevice(&g_fab, FAB_DEV_USB4);
        g_lp[i].Port = i + 1;
    }
    g_fab.Hook = lpHook;
    g_fab.XdTx = lpTx;
    TbtU4DefaultConfig(&c);
    c.RingFlags = TBT_RING_START_MASK_IMC;
    c.Net = net ? TRUE : FALSE;
    c.NetRevision = 0x03040000UL;
    c.LaneBonding = bonding ? TRUE : FALSE;
    memset(&g_ring, 0, sizeof(g_ring));
    TbtU4Init(&g_cm, &g_ring, &g_fab.Host, &c);
    memset(&n, 0, sizeof(n));
    n.RingsUp = netRingsUp;
    n.RingsDown = netRingsDown;
    n.Link = netLink;
    TbtU4SetNet(&g_cm, &n);
    g_ringsUp = g_ringsDown = g_linkUp = g_linkDown = 0;
    g_ringsAnswer = TBT_OK;
    (void)TbtU4Start(&g_cm, fabMem(&g_fab), FAB_PHYS, FAB_MEM_BYTES);
}

/* One tick of each side, 100 ms apart. */
static void pump(int ticks)
{
    int i;
    int k;

    for (i = 0; i < ticks; i++) {
        TbtU4Poll(&g_cm);
        for (k = 0; k < 2; k++) {
            if (g_lp[k].On && g_lp[k].Live) {
                TbtXdpPeerTick(&g_lp[k].E);
            }
        }
        g_fab.Host.Sleep(g_fab.Host.Context, 100UL);
    }
}

static int pumpUntilUp(LP *lp, int bound)
{
    int i;

    for (i = 0; i < bound; i++) {
        if (lp->E.NetState == TBT_NET_S_UP && g_linkUp != 0) {
            return 1;
        }
        pump(1);
    }
    return 0;
}

static ULONG portState(ULONG port)
{
    const TBT_U4_PORT *p = TbtU4Port(&g_cm, port);

    return p != NULL ? p->State : 0xFFFFFFFFUL;
}

static ULONG portWhy(ULONG port)
{
    const TBT_U4_PORT *p = TbtU4Port(&g_cm, port);

    return p != NULL ? p->Why : 0xFFFFFFFFUL;
}

static unsigned long hostLane(int port, int k)
{
    return fabAdp(&g_fab, 0, fabPortLane0(&g_fab, port) + k, FAB_CAP_LANE + 1);
}

static unsigned long pid(int port)
{
    return fabAdp(&g_fab, 0, fabPortLane0(&g_fab, port), FAB_CAP_PORT + 19) & 0x10UL;
}

static unsigned long entry(int adp, int hop, int dw)
{
    return g_fab.R[0].Adp[adp].Path[2 * hop + dw];
}

static void plug(LP *lp, unsigned long uuid0, int live)
{
    lp->On = 1;
    lpStart(lp, lp->Port, uuid0, live);
    fabPlug(&g_fab, lp->Port, lp->Dev);
}

static void unplug(LP *lp)
{
    fabUnplug(&g_fab, lp->Port);
    lp->On = 0;
    if (lp->Live) {
        TbtXdpPeerEnd(&lp->E, TRUE);
    }
    lp->Live = 0;
}

/* ------------------------------------------------------------------------ */

/* XhciCmNet 0 (record 35 section 8.1): the port stays UNSUPPORTED as before
 * the feature, nothing is sent, and the peer's requests go unanswered. */
static void test_off(void)
{
    LP *lp = &g_lp[0];
    unsigned long sent;

    setup(0, 1);
    TbtU4NetWanted(&g_cm, TRUE, 7);
    plug(lp, 0x00002222UL, 1);
    pump(150);
    CHECK_EQ(portState(1), TBT_U4_UNSUPPORTED, "XhciCmNet 0: UNSUPPORTED");
    CHECK_EQ(portWhy(1), TBT_U4_WHY_ENUM_ERROR, "with the enumeration error");
    CHECK_EQ(g_fab.XdSent, 0, "no inter-domain packet sent");
    CHECK(g_cm.XdIgnored >= 1, "the peer's requests counted and dropped");
    CHECK(TbtU4Peer(&g_cm, 1) == NULL, "no peer record");
    CHECK_EQ(pid(1), 0, "the Inter-Domain bit left clear");
    CHECK_EQ(lp->E.State, TBT_XDP_S_ERROR, "the peer gave up its UUID step");
    sent = g_fab.XdSent;
    unplug(lp);
    pump(2);
    CHECK_EQ(portState(1), TBT_U4_EMPTY, "the unplug: EMPTY");
    CHECK_EQ(g_fab.XdSent, sent, "still nothing sent");
}

/* Record 35 section 5.3 (revision 15): on the ASM4242's host router
 * (ROUTER_CS_0 174C:2428) this host withdraws prtcstns bit 0, so the block
 * it serves carries 6 and RingsUp is told no E2E, while the peer offers 7;
 * the service still comes up. */
static void test_asm_no_e2e(void)
{
    LP *lp = &g_lp[0];
    const TBT_XDP_PEER *ours;

    g_hostCs0 = TBT_U4_ASM_VENDOR | (TBT_U4_ASM_HOST << 16);
    setup(1, 1);
    g_hostCs0 = 0;
    TbtU4NetWanted(&g_cm, TRUE, 7);
    plug(lp, 0x00002222UL, 1);
    CHECK(pumpUntilUp(lp, 300), "ASM4242: the service came up");
    ours = TbtU4Peer(&g_cm, 1);
    CHECK(ours != NULL, "ASM4242: the peer record");
    if (ours == NULL) {
        return;
    }
    CHECK_EQ(g_cm.HostRouter.Vendor, TBT_U4_ASM_VENDOR, "the ASM4242 host router read");
    CHECK_EQ(ours->LocalProps.Prtcstns, 6, "ASM4242: bit 0 withdrawn from the block");
    CHECK_EQ(g_ringsUp, 1, "ASM4242: RingsUp once");
    CHECK_EQ(g_lastE2e, 0, "ASM4242: no E2E on the rings");
    CHECK_EQ(g_cm.NetPrtcstns, 7, "the driver's own value kept");
}

/* A service up, the cable out and in, the service up again: `asm` the
 * ASM4242's host router identity; on either router the Host Interface's
 * Pending Packets stick after a Valid clear on every HopID (record 22
 * section 18.9). Returns whether the second service came up. */
static int ppReplug(int asm, unsigned long *quarantined)
{
    LP *lp = &g_lp[0];
    int k;
    int up;

    g_hostCs0 = asm ? (TBT_U4_ASM_VENDOR | (TBT_U4_ASM_HOST << 16)) : 0;
    setup(1, 1);
    g_hostCs0 = 0;
    for (k = 0; k < FAB_HOPS; k++) {
        g_fab.R[0].Adp[HI_ADP].PpStuck[k] = 1;
    }
    TbtU4NetWanted(&g_cm, TRUE, 7);
    plug(lp, 0x00002222UL, 1);
    CHECK(pumpUntilUp(lp, 300), "stuck Pending Packets: the first service up");
    unplug(lp);
    pump(10);
    CHECK_EQ(portState(1), TBT_U4_EMPTY, "the unplug: EMPTY");
    CHECK_EQ(g_ringsDown, 1, "the rings down once");
    CHECK(!(entry(HI_ADP, (int)TBT_U4_XD_RING, 0) & 0x80000000UL),
          "the transmit entry invalid");
    CHECK(g_fab.R[0].Adp[HI_ADP].Path[2 * TBT_U4_XD_RING + 1] & 0x10000000UL,
          "the fabric's Pending Packets stuck at the transmit entry");
    g_linkUp = 0;
    plug(lp, 0x00002222UL, 1);
    up = pumpUntilUp(lp, 300);
    *quarantined = g_cm.Counters.Quarantined;
    return up;
}

/* Record 22 section 18.9 on the ASM4242's host router: the Host
 * Interface's stuck Pending Packets read as 0, so the transmit entry is
 * drained at its Valid clear, nothing quarantined, and the service comes
 * up again after a replug on the same ring and entry. */
static void test_asm_replug_pp(void)
{
    unsigned long q;
    int up;

    up = ppReplug(1, &q);
    CHECK(up, "ASM4242: the service up again after the replug");
    CHECK_EQ(q, 0, "ASM4242: nothing quarantined");
    CHECK_EQ(g_ringsUp, 2, "ASM4242: RingsUp twice");
    CHECK(entry(HI_ADP, (int)TBT_U4_XD_RING, 0) & 0x80000000UL,
          "ASM4242: the transmit entry valid again");
    CHECK(g_cm.Counters.PendingIgnored != 0, "ASM4242: the stuck bit seen and ignored");
    CHECK_EQ(g_cm.Counters.PendingTimeouts, 0, "ASM4242: no drain waited out");
}

/* The same stuck bit on any other host router is read as it is (record 22
 * section 7.3): the entry quarantined, its ring held, no second service -
 * what the ASM4242's case above departs from, by its identity alone. */
static void test_pp_stuck_other(void)
{
    unsigned long q;
    int up;

    up = ppReplug(0, &q);
    CHECK(!up, "another router: no second service while the entry is held");
    CHECK(q != 0, "another router: the entry quarantined");
    CHECK_EQ(g_cm.Counters.PendingIgnored, 0, "another router: nothing ignored");
    CHECK_EQ(g_ringsUp, 1, "another router: RingsUp once");
}

/* Section 3.5 as the lower UUID (BOND_LOW), with both sides' network
 * directory: the link bonded, the service up, the two entries of section
 * 5.1; then the unplug of section 3.8. */
static void test_low_bonded_service(void)
{
    LP *lp = &g_lp[0];
    const TBT_XDP_PEER *ours;
    int lane;
    unsigned long e0;
    unsigned long e1;
    unsigned long rxHop;
    long rxAt;
    long txAt;

    setup(1, 1);
    TbtU4NetWanted(&g_cm, TRUE, 7);
    lane = fabPortLane0(&g_fab, 1);
    plug(lp, 0x00002222UL, 1);
    pump(2);
    CHECK_EQ(portState(1), TBT_U4_XDOMAIN, "ERR_ENUM with XhciCmNet 1: XDOMAIN");
    CHECK_EQ(portWhy(1), TBT_U4_WHY_XDOMAIN, "its reason");
    CHECK(pumpUntilUp(lp, 300), "the service came up");
    pump(50);                   /* each end's re-read, record 35 section 3.18 */
    ours = TbtU4Peer(&g_cm, 1);
    CHECK(ours != NULL, "the peer record");
    if (ours == NULL) {
        return;
    }
    CHECK_EQ(ours->State, TBT_XDP_S_READY, "discovery READY");
    CHECK_EQ(ours->NetState, TBT_NET_S_UP, "our side UP");
    CHECK(ours->Counters.Bonded == 1, "bonded as the lower UUID");
    CHECK_EQ((hostLane(1, 0) >> 20) & 0x3FUL, 2, "the host lane 0 x2");
    CHECK_EQ(hostLane(1, 0) & 0x30UL, 0x30UL, "its Target Link Width dual");
    CHECK_EQ(pid(1), 0x10UL, "PORT_CS_19 Inter-Domain set");
    CHECK(TbtUuidEqual(&ours->Remote, &lp->E.Local), "the peer's UUID learned");
    CHECK_EQ(g_ringsUp, 1, "RingsUp once");
    CHECK_EQ(g_lastRing, TBT_U4_XD_RING, "on ring 1");
    CHECK_EQ(g_lastE2e, 1, "E2E: both blocks advertise bit 0");
    CHECK_EQ(g_linkUp, 1, "the link up once");
    CHECK_EQ(lp->Ups, 1, "the far side's service up");
    CHECK_EQ(ours->TxHop, 8, "our transmit HopID 8, the lowest free");
    CHECK_EQ(ours->RxHop, lp->TxHop, "our receive HopID the peer's Service HopID");
    /* Receive: the lane's entry at the peer's HopID into the host
     * interface at ring 1, credits from the lane, IFC 1, EFC 0, prio 4. */
    e0 = entry(lane, (int)ours->RxHop, 0);
    e1 = entry(lane, (int)ours->RxHop, 1);
    CHECK(e0 & 0x80000000UL, "the receive entry valid");
    CHECK_EQ((e0 >> 11) & 0x3FUL, HI_ADP, "into the host interface adapter");
    CHECK_EQ(e0 & 0x7FUL, 1, "at the ring's HopID");
    CHECK_EQ((e0 >> 17) & 0x7FUL, TBT_U4_XD_CREDITS, "14 credits (no baMaxHI)");
    CHECK((e1 & 0x01000000UL) && !(e1 & 0x02000000UL) && !(e1 & 0x04000000UL),
          "IFC 1, EFC 0, ISE 0");
    CHECK_EQ((e1 >> 8) & 7UL, 4, "priority 4");
    CHECK_EQ(e1 & 0xFUL, 1, "weight 1");
    /* Transmit: the host interface's ring 1 entry out of lane 0 at our
     * Service HopID, EFC 1. */
    e0 = entry(HI_ADP, 1, 0);
    e1 = entry(HI_ADP, 1, 1);
    CHECK(e0 & 0x80000000UL, "the transmit entry valid");
    CHECK_EQ((e0 >> 11) & 0x3FUL, (unsigned long)lane, "out of lane 0");
    CHECK_EQ(e0 & 0x7FUL, ours->TxHop, "at our Service HopID");
    CHECK((e1 & 0x02000000UL) && !(e1 & 0x08000000UL), "EFC 1, ESE 0");
    CHECK_EQ((e1 >> 8) & 7UL, 4, "priority 4");
    /* Section 5.1's order up: the rings, the receive entry, the transmit
     * entry, the link (the write log's positions). */
    rxAt = fabFindWrite(&g_fab, g_ringsUpAt, 0, TBT_CS_PATH, (unsigned long)lane,
                        2 * ours->RxHop);
    txAt = fabFindWrite(&g_fab, g_ringsUpAt, 0, TBT_CS_PATH, HI_ADP, 2);
    CHECK(rxAt >= 0 && txAt > rxAt, "the receive entry written before the transmit entry");
    CHECK(txAt >= 0 && g_linkUpAt > txAt, "the link up after both");

    /* Section 3.8: the unplug - no Logout, the entries invalid, the bit
     * and the lanes back. */
    rxHop = ours->RxHop;
    unplug(lp);
    pump(2);
    CHECK_EQ(portState(1), TBT_U4_EMPTY, "the unplug: EMPTY");
    CHECK(TbtU4Peer(&g_cm, 1) == NULL, "the record freed");
    /* The order down: the link, the transmit entry, the receive entry, the
     * rings. */
    txAt = fabFindWrite(&g_fab, g_linkDownAt, 0, TBT_CS_PATH, HI_ADP, 2);
    rxAt = fabFindWrite(&g_fab, g_linkDownAt, 0, TBT_CS_PATH, (unsigned long)lane, 2 * rxHop);
    CHECK(txAt >= 0 && rxAt > txAt, "the transmit entry invalid before the receive entry");
    CHECK(rxAt >= 0 && g_ringsDownAt > rxAt, "the rings down after both");
    CHECK_EQ(g_linkDown, 1, "the link down once");
    CHECK_EQ(g_ringsDown, 1, "the rings down once");
    CHECK(!(entry(lane, (int)lp->TxHop, 0) & 0x80000000UL), "the receive entry invalid");
    CHECK(!(entry(HI_ADP, 1, 0) & 0x80000000UL), "the transmit entry invalid");
    CHECK_EQ(pid(1), 0, "the Inter-Domain bit cleared");
    CHECK_EQ(hostLane(1, 0) & 0x30UL, 0x10UL, "Target Link Width back to single");
    CHECK_EQ(hostLane(1, 0) & 0x8000UL, 0, "no Lane Bonding left");
}

/* Record 35 section 5.1: the receive entry's Initial Credits is 7 bits
 * (Spec Table 8-23), so a host router whose baMaxHI and lane exceed it gets
 * 127, never the figure truncated into the field (128 would write 0, 600
 * would write 88); the clamp is logged once, with the figure clamped. The
 * host lane 0 is given 1000 Total Buffers, so the lesser is baMaxHI up to
 * there; at 0xFFFF the lesser is the lane's spare, between 128 and 1000. */
static void creditClamp(unsigned long baHi, unsigned long want, int clamped)
{
    LP *lp = &g_lp[0];
    const TBT_XDP_PEER *ours;
    FAB_ADP *ad;
    int lane;

    g_hostBaHi = baHi;
    setup(1, 0);
    g_hostBaHi = 0;
    lane = fabPortLane0(&g_fab, 1);
    ad = &g_fab.R[0].Adp[lane];
    ad->Cs[4] = (ad->Cs[4] & ~(0x3FFUL << 20)) | (1000UL << 20);
    g_fab.Watch = "cm.net.credits.clamp";
    g_fab.WatchCount = 0;
    TbtU4NetWanted(&g_cm, TRUE, 7);
    plug(lp, 0x00002222UL, 1);
    CHECK(pumpUntilUp(lp, 300), "credits: the service came up");
    CHECK_EQ(g_cm.Ports[0].HostBaHi, baHi, "credits: baMaxHI read");
    ours = TbtU4Peer(&g_cm, 1);
    CHECK(ours != NULL, "credits: the peer record");
    if (ours == NULL) {
        return;
    }
    CHECK_EQ((entry(lane, (int)ours->RxHop, 0) >> 17) & 0x7FUL, want,
             "credits: the receive entry's Initial Credits");
    CHECK_EQ(g_fab.WatchCount, clamped ? 1UL : 0UL, "credits: the clamp logged once or not");
    if (clamped && g_fab.WatchCount != 0) {
        CHECK_EQ(g_fab.WatchValue[0] >> 16, 1, "credits: the clamp's port");
        if (baHi < 1000) {
            CHECK_EQ(g_fab.WatchValue[0] & 0xFFFFUL, baHi, "credits: baMaxHI the figure");
        } else {
            CHECK((g_fab.WatchValue[0] & 0xFFFFUL) > 127 &&
                  (g_fab.WatchValue[0] & 0xFFFFUL) < 1000, "credits: the lane's spare the figure");
        }
    }
    unplug(lp);
    pump(2);
    CHECK_EQ(portState(1), TBT_U4_EMPTY, "credits: EMPTY");
}

static void test_credit_clamp(void)
{
    creditClamp(127, 127, 0);
    creditClamp(128, 127, 1);
    creditClamp(600, 127, 1);
    creditClamp(0xFFFFUL, 127, 1);
}

/* The higher UUID (BOND_HIGH): the peer asks, this side sets its Target
 * Link Width and the peer bonds. */
static void test_high_bonded(void)
{
    LP *lp = &g_lp[0];
    const TBT_XDP_PEER *ours;

    setup(1, 1);
    TbtU4NetWanted(&g_cm, TRUE, 7);
    plug(lp, 0x00000005UL, 1);
    CHECK(pumpUntilUp(lp, 300), "the service came up");
    ours = TbtU4Peer(&g_cm, 1);
    CHECK(ours != NULL && ours->Counters.Bonded == 1, "bonded as the higher UUID");
    CHECK_EQ((hostLane(1, 0) >> 20) & 0x3FUL, 2, "the host lane 0 x2");
    CHECK(lp->E.Counters.Bonded == 1, "the peer bonded too");
    unplug(lp);
    pump(2);
    CHECK_EQ(portState(1), TBT_U4_EMPTY, "EMPTY");
}

/* XhciCmLaneBonding 0: no bonding; lane 1 disabled once the peer is known
 * (Guide 7.5.2), put back at the unplug. */
static void test_no_bonding(void)
{
    LP *lp = &g_lp[0];
    const TBT_XDP_PEER *ours;

    setup(1, 0);
    TbtU4NetWanted(&g_cm, TRUE, 7);
    plug(lp, 0x00002222UL, 1);
    CHECK(pumpUntilUp(lp, 300), "the service came up");
    ours = TbtU4Peer(&g_cm, 1);
    CHECK(ours != NULL && ours->Counters.Bonded == 0, "not bonded");
    CHECK_EQ(hostLane(1, 1) & 0x4000UL, 0x4000UL, "lane 1 Lane Disable set");
    CHECK_EQ((hostLane(1, 1) >> 26) & 0xFUL, 0, "lane 1 Disabled");
    unplug(lp);
    pump(2);
    CHECK_EQ(hostLane(1, 1) & 0x4000UL, 0, "Lane Disable put back");
}

/* Section 3.3 step 2: no answer to the UUID Request - UNSUPPORTED as
 * before the feature, the record kept; the peer's own request later
 * restarts the protocol and the service comes up. */
static void test_silent_then_restart(void)
{
    LP *lp = &g_lp[0];

    setup(1, 1);
    TbtU4NetWanted(&g_cm, TRUE, 7);
    plug(lp, 0x00002222UL, 0);
    pump(150);
    CHECK_EQ(portState(1), TBT_U4_UNSUPPORTED, "no answer: UNSUPPORTED");
    CHECK_EQ(portWhy(1), TBT_U4_WHY_ENUM_ERROR, "with the enumeration error");
    CHECK(g_fab.XdSent >= 11, "the UUID Request and its ten resends");
    CHECK(TbtU4Peer(&g_cm, 1) != NULL, "the record kept for the peer's request");
    lp->Live = 1;
    TbtXdpPeerStart(&lp->E);
    CHECK(pumpUntilUp(lp, 300), "the peer's request restarted it: service up");
    CHECK_EQ(portState(1), TBT_U4_XDOMAIN, "XDOMAIN again");
    unplug(lp);
    pump(2);
}

/* Guide 3.3: the far end answers with this domain's own UUID - a loop:
 * UNSUPPORTED with WHY_LOOP, no Inter-Domain bit, nothing built. */
static void test_loop(void)
{
    LP *lp = &g_lp[0];

    setup(1, 1);
    TbtU4NetWanted(&g_cm, TRUE, 7);
    lp->On = 1;
    lpStart(lp, 1, OUR_UUID0, 1);
    lp->E.Local.Dw[1] = 0x55667788UL;
    fabPlug(&g_fab, 1, lp->Dev);
    pump(40);
    CHECK_EQ(portState(1), TBT_U4_UNSUPPORTED, "a loop: UNSUPPORTED");
    CHECK_EQ(portWhy(1), TBT_U4_WHY_LOOP, "WHY_LOOP");
    CHECK_EQ(pid(1), 0, "no Inter-Domain bit");
    CHECK_EQ(g_ringsUp, 0, "nothing built");
    CHECK_EQ(g_cm.XdLoops, 1, "counted once");
}

/* Section 3.8: the stop - a Logout sent before the rings stop, the
 * entries invalid. */
static void test_stop_logout(void)
{
    LP *lp = &g_lp[0];
    int lane;
    unsigned long before;

    setup(1, 1);
    TbtU4NetWanted(&g_cm, TRUE, 7);
    lane = fabPortLane0(&g_fab, 1);
    plug(lp, 0x00002222UL, 1);
    CHECK(pumpUntilUp(lp, 300), "the service came up");
    before = lp->E.Counters.LogoutsReceived;
    TbtU4Stop(&g_cm);
    CHECK(lp->E.Counters.LogoutsReceived > before, "a Logout reached the peer");
    CHECK_EQ(g_linkDown, 1, "the link down");
    CHECK_EQ(g_ringsDown, 1, "the rings down");
    CHECK(!(entry(lane, (int)lp->TxHop, 0) & 0x80000000UL), "the receive entry invalid");
    CHECK(!(entry(HI_ADP, 1, 0) & 0x80000000UL), "the transmit entry invalid");
    CHECK_EQ(pid(1), 0, "the Inter-Domain bit cleared");
}

/* The containment (record 22 section 7.7): nothing written, the link and
 * the rings down, both entries kept owed with their identities. */
static void test_contain(void)
{
    LP *lp = &g_lp[0];
    int lane;
    unsigned long writes;
    const TBT_U4_XD_OWED *o;

    setup(1, 1);
    TbtU4NetWanted(&g_cm, TRUE, 7);
    lane = fabPortLane0(&g_fab, 1);
    plug(lp, 0x00002222UL, 1);
    CHECK(pumpUntilUp(lp, 300), "the service came up");
    writes = g_fab.WrCount;
    TbtU4Contain(&g_cm);
    o = &g_cm.XdOwed[0];
    CHECK_EQ(g_fab.WrCount, writes, "nothing written");
    CHECK_EQ(g_linkDown, 1, "the link down");
    CHECK_EQ(g_ringsDown, 1, "the rings down");
    CHECK(TbtU4Peer(&g_cm, 1) == NULL, "the record ended");
    CHECK(o->Tx && o->Hi == HI_ADP && o->Ring == TBT_U4_XD_RING, "the transmit entry owed");
    CHECK(o->Rx && o->RxHop == lp->TxHop, "the receive entry owed");
    CHECK(entry(lane, (int)lp->TxHop, 0) & 0x80000000UL, "left valid, unread");

    /* Access back (a recovery's commit, modelled by its flag alone), the
     * receive entry meanwhile someone else's (another output): the tick
     * invalidates the transmit entry, which still reads as this service's,
     * and leaves the other untouched; neither stays owed. */
    g_fab.R[0].Adp[lane].Path[2 * lp->TxHop] =
        (g_fab.R[0].Adp[lane].Path[2 * lp->TxHop] & ~0x0001F87FUL) | (5UL << 11) | 8UL;
    g_cm.Contained = FALSE;
    TbtU4Poll(&g_cm);
    CHECK(!(entry(HI_ADP, 1, 0) & 0x80000000UL), "the transmit entry invalidated");
    CHECK(entry(lane, (int)lp->TxHop, 0) & 0x80000000UL, "another's entry left valid");
    CHECK_EQ((entry(lane, (int)lp->TxHop, 0) >> 11) & 0x3FUL, 5, "and as it was");
    CHECK(!o->Tx && !o->Rx, "nothing owed");
}

/* The network driver's Close and Open (TbtU4NetWanted): the service ends
 * with a Logout and the peer stays READY; opened again, it comes back. */
static void test_close_open(void)
{
    LP *lp = &g_lp[0];
    const TBT_XDP_PEER *ours;

    setup(1, 1);
    plug(lp, 0x00002222UL, 1);
    pump(60);
    ours = TbtU4Peer(&g_cm, 1);
    CHECK(ours != NULL && ours->State == TBT_XDP_S_READY, "READY with the service closed");
    CHECK_EQ(g_ringsUp, 0, "nothing built while closed");
    TbtU4NetWanted(&g_cm, TRUE, 7);
    CHECK(pumpUntilUp(lp, 300), "opened: the service came up");
    TbtU4NetWanted(&g_cm, FALSE, 0);
    pump(30);
    ours = TbtU4Peer(&g_cm, 1);
    CHECK(ours != NULL && ours->NetState == TBT_NET_S_OFF, "closed: our side OFF");
    CHECK(ours != NULL && ours->State == TBT_XDP_S_READY, "the peer still READY");
    CHECK_EQ(g_linkDown, 1, "the link down");
    CHECK_EQ(g_ringsDown, 1, "the rings down");
    CHECK_EQ(portState(1), TBT_U4_XDOMAIN, "the port XDOMAIN");
    CHECK_EQ(pid(1), 0x10UL, "the Inter-Domain bit kept");
    unplug(lp);
    pump(2);
}

/* Section 4.2: the link goes down at this host's own Logout, not when the
 * Logout is answered. The far host goes silent once the service is up, so
 * the Logout of the network driver's Close is never answered: the link is
 * down at once, the paths and the rings held until the Logout times out,
 * and only then the entries invalid and the rings down. */
static void test_close_unanswered(void)
{
    LP *lp = &g_lp[0];
    const TBT_XDP_PEER *ours;
    int lane;
    int i;

    setup(1, 1);
    TbtU4NetWanted(&g_cm, TRUE, 7);
    lane = fabPortLane0(&g_fab, 1);
    plug(lp, 0x00002222UL, 1);
    CHECK(pumpUntilUp(lp, 300), "the service came up");
    lp->Live = 0;
    TbtU4NetWanted(&g_cm, FALSE, 0);
    ours = TbtU4Peer(&g_cm, 1);
    CHECK(ours != NULL && ours->NetState == TBT_NET_S_LOGOUT, "our Logout due");
    CHECK_EQ(g_linkDown, 1, "the link down at the Logout");
    CHECK_EQ(g_ringsDown, 0, "the rings held");
    pump(1);
    CHECK_EQ(g_linkDown, 1, "the link down once");
    CHECK_EQ(g_ringsDown, 0, "the rings held while the Logout is out");
    CHECK(entry(lane, (int)lp->TxHop, 0) & 0x80000000UL, "the receive entry kept");
    CHECK(entry(HI_ADP, 1, 0) & 0x80000000UL, "the transmit entry kept");
    for (i = 0; i < 300 && g_ringsDown == 0; i++) {
        pump(1);
    }
    CHECK_EQ(g_ringsDown, 1, "the rings down once the Logout timed out");
    CHECK_EQ(g_linkDown, 1, "no second link down");
    CHECK(!(entry(lane, (int)lp->TxHop, 0) & 0x80000000UL), "the receive entry invalid");
    CHECK(!(entry(HI_ADP, 1, 0) & 0x80000000UL), "the transmit entry invalid");
    unplug(lp);
    pump(2);
}

/* Run-45 45-E.15: a far firmware CM's single early UUID Request, on a
 * port this domain has not classified as XDOMAIN, is answered with this
 * domain's UUID (no record started) once this driver has cleared lane 0's
 * Lock - never into the Lock, which the host router refuses (ERR_LOCK);
 * with XhciCmNet 0 it is dropped as before. The far router here is left a
 * plain device router, so the port is connected and not XDOMAIN when the
 * request lands. */
static void test_early_uuid(void)
{
    LP *lp = &g_lp[0];
    ULONG w[TBT_XDP_MAX_WORDS];
    ULONG n;
    TBT_ROUTE r;
    int net;
    unsigned long refused;

    /* With the plug, before the first poll: the port still EMPTY. */
    setup(1, 1);
    r.Hi = 0;
    r.Lo = (ULONG)fabPortLane0(&g_fab, 1);
    n = TbtXdpBuildUuidRequest(w, &r, 1, TBT_XDP_UUID_REQ);
    g_uuidResp = 0;
    fabPlug(&g_fab, 1, lp->Dev);
    fabInject(&g_fab, TBT_PDF_XDOMAIN_REQ, w, n);
    TbtU4Poll(&g_cm);
    TbtU4Poll(&g_cm);
    CHECK_EQ(g_uuidResp, 1, "answered with the plug");
    CHECK_EQ(g_fab.XdRefused, 0, "nothing sent into the Lock");
    fabUnplug(&g_fab, 1);
    pump(2);
    /* The emulator's order (run-45 "45.59 (legs, continued)"): the
     * request read by the ring wake that brings the hot plug event, taken
     * before the port's check has cleared lane 0's Lock. The answer is
     * held, not sent into the Lock, and goes once the check has run. */
    setup(1, 1);
    g_uuidResp = 0;
    fabPlug(&g_fab, 1, lp->Dev);
    fabInject(&g_fab, TBT_PDF_XDOMAIN_REQ, w, n);
    g_fab.Host.Sleep(g_fab.Host.Context, 100UL);
    TbtU4RingWake(&g_cm);
    CHECK(g_cm.XdEarly[0].Owed, "the answer held at the wake");
    CHECK_EQ(g_fab.XdRefused, 0, "nothing sent into the Lock at the wake");
    TbtU4Poll(&g_cm);
    TbtU4Poll(&g_cm);
    CHECK_EQ(g_uuidResp, 1, "answered after the unlock");
    CHECK_EQ(g_fab.XdRefused, 0, "nothing refused");
    CHECK(!g_cm.XdEarly[0].Owed, "nothing owed");
    fabUnplug(&g_fab, 1);
    pump(2);
    /* A quick re-plug of a port this driver had connected and unlocked:
     * the unplug and the plug read with the request, before the check,
     * while the port still shows the last connection's Unlocked. The
     * answer waits for the new connection's unlock, not sent into the
     * Lock the unplug set again. */
    setup(1, 1);
    fabPlug(&g_fab, 1, lp->Dev);
    pump(10);
    CHECK(g_cm.Ports[0].Unlocked, "the first connection unlocked");
    g_uuidResp = 0;
    refused = g_fab.XdRefused;
    fabUnplug(&g_fab, 1);
    fabPlug(&g_fab, 1, lp->Dev);
    fabInject(&g_fab, TBT_PDF_XDOMAIN_REQ, w, n);
    g_fab.Host.Sleep(g_fab.Host.Context, 100UL);
    TbtU4RingWake(&g_cm);
    CHECK(g_cm.XdEarly[0].Owed, "held at the re-plug");
    CHECK_EQ(g_fab.XdRefused, refused, "nothing sent into the re-locked lane");
    pump(10);
    CHECK_EQ(g_uuidResp, 1, "answered on the new connection");
    CHECK_EQ(g_fab.XdRefused, refused, "nothing refused");
    CHECK(!g_cm.XdEarly[0].Owed, "nothing owed");
    fabUnplug(&g_fab, 1);
    pump(2);
    /* The cable out again before this driver connected the port: the
     * held answer is owed to nobody, and a later connection that sends no
     * request of its own gets none. */
    setup(1, 1);
    g_uuidResp = 0;
    fabPlug(&g_fab, 1, lp->Dev);
    fabInject(&g_fab, TBT_PDF_XDOMAIN_REQ, w, n);
    g_fab.Host.Sleep(g_fab.Host.Context, 100UL);
    TbtU4RingWake(&g_cm);
    CHECK(g_cm.XdEarly[0].Owed, "held at the plug");
    fabUnplug(&g_fab, 1);
    pump(5);
    CHECK(!g_cm.XdEarly[0].Owed, "dropped with the connection");
    fabPlug(&g_fab, 1, lp->Dev);
    pump(10);
    CHECK_EQ(g_uuidResp, 0, "no stale answer on the later connection");
    fabUnplug(&g_fab, 1);
    pump(2);
    /* This domain's UUID unreadable at first: the answer owed, sent at a
     * later take with no second request. */
    setup(1, 1);
    g_cm.XdLocalValid = FALSE;
    g_failCs7 = 1;
    g_uuidResp = 0;
    fabPlug(&g_fab, 1, lp->Dev);
    fabInject(&g_fab, TBT_PDF_XDOMAIN_REQ, w, n);
    TbtU4Poll(&g_cm);
    TbtU4Poll(&g_cm);
    TbtU4Poll(&g_cm);
    CHECK_EQ(g_failCs7, 0, "the first read went unanswered");
    CHECK_EQ(g_uuidResp, 1, "answered at a later take");
    CHECK(!g_cm.XdEarly[0].Owed, "nothing owed");
    fabUnplug(&g_fab, 1);
    pump(2);
    /* The same, the far end another domain's router: the port becomes
     * XDOMAIN and its record starts before the retry, which still sends
     * the owed answer (the record's engine never saw that request). */
    setup(1, 1);
    g_cm.XdLocalValid = FALSE;
    g_failCs7 = 1;
    g_uuidResp = 0;
    plug(lp, 0x00002222UL, 0);
    fabInject(&g_fab, TBT_PDF_XDOMAIN_REQ, w, n);
    pump(10);
    CHECK_EQ(portState(1), TBT_U4_XDOMAIN, "XDOMAIN");
    CHECK(TbtU4Peer(&g_cm, 1) != NULL, "its record started");
    CHECK_EQ(g_uuidResp, 1, "the owed answer sent all the same");
    CHECK(!g_cm.XdEarly[0].Owed, "nothing owed");
    unplug(lp);
    pump(2);
    for (net = 1; net >= 0; net--) {
        setup(net, 1);
        fabPlug(&g_fab, 1, lp->Dev);
        pump(20);
        CHECK(portState(1) != TBT_U4_EMPTY && portState(1) != TBT_U4_XDOMAIN,
              "connected, not XDOMAIN");
        r.Hi = 0;
        r.Lo = (ULONG)fabPortLane0(&g_fab, 1);
        n = TbtXdpBuildUuidRequest(w, &r, 0, TBT_XDP_UUID_REQ);
        g_uuidResp = 0;
        fabInject(&g_fab, TBT_PDF_XDOMAIN_REQ, w, n);
        TbtU4Poll(&g_cm);
        TbtU4Poll(&g_cm);
        CHECK_EQ(g_uuidResp, (unsigned long)net, net ? "answered at once" : "XhciCmNet 0: dropped");
        CHECK(TbtU4Peer(&g_cm, 1) == NULL, "no record started");
        fabUnplug(&g_fab, 1);
        pump(2);
    }
}

/* Section 13: one service per NHI - a second peer on port 2 is discovered
 * and READY, its service refused while port 1's is up. */
static void test_one_service(void)
{
    LP *a = &g_lp[0];
    LP *b = &g_lp[1];
    const TBT_XDP_PEER *pb;
    int i;

    setup(1, 1);
    TbtU4NetWanted(&g_cm, TRUE, 7);
    plug(a, 0x00002222UL, 1);
    CHECK(pumpUntilUp(a, 300), "port 1's service up");
    plug(b, 0x00003333UL, 1);
    for (i = 0; i < 100; i++) {
        pump(1);
    }
    pb = TbtU4Peer(&g_cm, 2);
    CHECK_EQ(portState(2), TBT_U4_XDOMAIN, "port 2 XDOMAIN");
    CHECK(pb != NULL && pb->State == TBT_XDP_S_READY, "port 2's peer READY");
    CHECK(pb != NULL && !pb->PathsUp, "no paths for port 2");
    CHECK(pb != NULL && !pb->LocalProps.Network, "no network directory offered there");
    CHECK(!b->E.RemoteInfo.Network, "port 2's peer read no directory");
    CHECK_EQ(b->E.NetState, TBT_NET_S_OFF, "so it never logged in");
    CHECK_EQ(g_ringsUp, 1, "one ring set up");
    /* Port 1's peer leaves: port 2 is offered the service and gets it. */
    unplug(a);
    g_linkUp = 0;
    CHECK(pumpUntilUp(b, 300), "port 2's service up once port 1's ended");
    CHECK_EQ(g_ringsUp, 2, "its rings set up");
    unplug(b);
    pump(2);
}

/* The rings refused (no memory, section 5.3): no entry written, the
 * engine logs out, the peer stays READY. */
static void test_rings_refused(void)
{
    LP *lp = &g_lp[0];
    int lane;
    const TBT_XDP_PEER *ours;

    setup(1, 1);
    TbtU4NetWanted(&g_cm, TRUE, 7);
    g_ringsAnswer = TBT_E_PARAM;
    lane = fabPortLane0(&g_fab, 1);
    plug(lp, 0x00002222UL, 1);
    pump(80);
    ours = TbtU4Peer(&g_cm, 1);
    CHECK(g_ringsUp >= 1, "the rings asked for");
    CHECK_EQ(g_linkUp, 0, "no link up");
    CHECK(ours != NULL && !ours->PathsUp, "no paths");
    CHECK(!(entry(HI_ADP, 1, 0) & 0x80000000UL), "no transmit entry");
    CHECK(!(entry(lane, 8, 0) & 0x80000000UL), "no receive entry");
    CHECK(ours != NULL && ours->State == TBT_XDP_S_READY, "the peer READY");
    unplug(lp);
    pump(2);
}

/* Whether our peer on port 1 is at stage `k`: 0 the UUID step, 1 the link
 * and bonding, 2 the properties, 3 the login, 4 the service up. */
static int atStage(int k)
{
    const TBT_XDP_PEER *q = TbtU4Peer(&g_cm, 1);

    if (q == NULL) {
        return 0;
    }
    switch (k) {
    case 0: return q->State == TBT_XDP_S_UUID;
    case 1: return q->State == TBT_XDP_S_LINK || q->State == TBT_XDP_S_BOND_LOW ||
                   q->State == TBT_XDP_S_BOND_HIGH;
    case 2: return q->State == TBT_XDP_S_PROPS;
    case 3: return q->State == TBT_XDP_S_READY && q->NetState == TBT_NET_S_LOGIN;
    default: return q->PathsUp;
    }
}

/* Section 3.8: an unplug at each stage (the UUID step, the bonding, the
 * properties, the login, the service), the stage asserted first: EMPTY,
 * nothing left on the host - no entry valid, the Inter-Domain bit clear,
 * the lanes as found - every RingsUp and link up matched; then a re-plug
 * served afresh. The far end's login is held back for the login stage,
 * so this side's Login is the one outstanding. */
static void test_unplug_midway(void)
{
    LP *lp = &g_lp[0];
    int lane;
    int k;
    int i;
    int h;
    int clean;
    int reached;

    for (k = 0; k <= 4; k++) {
        setup(1, 1);
        TbtU4NetWanted(&g_cm, TRUE, 7);
        lane = fabPortLane0(&g_fab, 1);
        plug(lp, 0x00002222UL, 1);
        if (k == 3) {
            /* The far end not wanting the service, its published block
             * left as it was (the directory still in it, so this side
             * logs in): it never logs in itself and refuses this side's
             * Login, which stays outstanding. */
            lp->E.LocalProps.Network = FALSE;
            TbtXdpPeerSetNetWanted(&lp->E, FALSE, 0);
        }
        reached = 0;
        for (i = 0; i < 400 && !reached; i++) {
            reached = atStage(k);
            if (!reached) {
                pump(1);
            }
        }
        CHECK(reached, "the stage reached before the unplug");
        unplug(lp);
        pump(2);
        CHECK_EQ(portState(1), TBT_U4_EMPTY, "an unplug midway: EMPTY");
        CHECK(TbtU4Peer(&g_cm, 1) == NULL, "the record freed");
        clean = 1;
        for (h = 1; h < FAB_HOPS; h++) {
            if (entry(lane, h, 0) & 0x80000000UL) {
                clean = 0;
            }
        }
        CHECK(clean && !(entry(HI_ADP, 1, 0) & 0x80000000UL), "no entry left valid");
        CHECK_EQ(pid(1), 0, "the Inter-Domain bit clear");
        CHECK_EQ(hostLane(1, 0) & 0x8030UL, 0x10UL, "lane 0 as found");
        CHECK_EQ(hostLane(1, 1) & 0x4030UL, 0x10UL, "lane 1 as found");
        CHECK_EQ(g_linkUp, g_linkDown, "every link up matched by a down");
        CHECK_EQ(g_ringsUp, g_ringsDown, "every RingsUp matched by a RingsDown");
        CHECK_EQ(g_cm.XdPidOwed, 0, "no clear owed");
    }
    plug(lp, 0x00002222UL, 1);
    CHECK(pumpUntilUp(lp, 300), "the re-plug's service up");
    unplug(lp);
    pump(2);
}

/* The identity (section 3.1, 4.4): the UUID from ROUTER_CS_7 and 8, the
 * MAC unicast and locally administered. */
static void test_identity(void)
{
    UCHAR mac[6];

    setup(1, 1);
    CHECK(TbtU4NetIdentity(&g_cm, mac), "the identity read");
    CHECK_EQ(g_cm.XdLocal.Dw[0], OUR_UUID0, "DW 0 ROUTER_CS_7");
    CHECK_EQ(g_cm.XdLocal.Dw[1], 0x55667788UL, "DW 1 ROUTER_CS_8");
    CHECK_EQ(g_cm.XdLocal.Dw[2], 0xFFFFFFFFUL, "DW 2 all ones");
    CHECK_EQ(g_cm.XdLocal.Dw[3], 0xFFFFFFFFUL, "DW 3 all ones");
    CHECK_EQ(mac[0] & 3, 2, "locally administered, unicast");
    setup(0, 1);
    CHECK(!TbtU4NetIdentity(&g_cm, mac), "none with XhciCmNet 0");
}

/* A peer that sends a Link Status Request for every control request this
 * domain makes: each answer reads the host lane, whose receive queues the
 * next. One pass takes what was queued when it began, so the poll returns
 * with the peer still sending, and what is left is answered later. */
static void test_flood(void)
{
    LP *lp = &g_lp[0];
    unsigned long first;

    setup(1, 0);
    TbtU4NetWanted(&g_cm, TRUE, 7);
    plug(lp, 0x00002222UL, 1);
    CHECK(pumpUntilUp(lp, 300), "flood: the service came up");
    g_flood = 100000L;
    floodOne(&g_fab);
    /* This tick's receives queue the requests; the next takes them. */
    TbtU4Poll(&g_cm);
    CHECK(g_cm.XdCount != 0, "flood: requests queued");
    g_linkResp = 0;
    TbtU4Poll(&g_cm);
    CHECK(g_flood > 0, "flood: the poll returned while the peer still sent");
    CHECK(g_linkResp >= 1, "flood: the first request answered");
    CHECK(g_linkResp <= TBT_U4_XD_QUEUE, "flood: at most a queue a pass");
    first = g_linkResp;
    TbtU4Poll(&g_cm);
    CHECK(g_linkResp > first, "flood: the next pass answers what was left");
    CHECK(g_linkResp - first <= TBT_U4_XD_QUEUE, "flood: again at most a queue");
    g_flood = 0;
    pump(3);
    CHECK_EQ(portState(1), TBT_U4_XDOMAIN, "flood: the port still XDOMAIN");
}

#define RUN_VEC(fn) do { fabTraceMark(#fn); fn(); } while (0)

int main(void)
{
    RUN_VEC(test_off);
    RUN_VEC(test_low_bonded_service);
    RUN_VEC(test_credit_clamp);
    RUN_VEC(test_asm_no_e2e);
    RUN_VEC(test_asm_replug_pp);
    RUN_VEC(test_pp_stuck_other);
    RUN_VEC(test_high_bonded);
    RUN_VEC(test_no_bonding);
    RUN_VEC(test_silent_then_restart);
    RUN_VEC(test_loop);
    RUN_VEC(test_stop_logout);
    RUN_VEC(test_contain);
    RUN_VEC(test_close_open);
    RUN_VEC(test_close_unanswered);
    RUN_VEC(test_one_service);
    RUN_VEC(test_rings_refused);
    RUN_VEC(test_unplug_midway);
    RUN_VEC(test_identity);
    RUN_VEC(test_early_uuid);
    RUN_VEC(test_flood);
    fabTraceMark("end");
    printf("test_u4xd: %d checks, %d failures\n", checks, failures);
    return failures;
}
