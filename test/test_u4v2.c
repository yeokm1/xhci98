/*
 * test_u4v2.c - host vectors for the USB4 software connection manager run
 * as a Ver. 2 Connection Manager (src\cm\tbt_u4cm.c; roadmap-usb4.md task
 * 45.20; design record 22 sections 6.2.4 and 14.1).
 *
 * Driven against the fake fabric of u4_fakefab.c with its USB4 version 2
 * host and device routers: CMUV by each router's version and as found on
 * a preserved one; Set Capabilities for the Sequence bit capability with
 * its read-back and every refusal and failure; every path entry written
 * as two DWs; the HiFi Enhanced Uni-Directional TMU on a link of two
 * version 2 routers and HiFi-Bi beside it; a preserved router's CV never
 * reconfigured; Enable Gen 4 Link Recovery cleared on preserved links and
 * a LINK_RECOVERY delivered as a port check; PCIe Extended Encapsulation;
 * Gen 4 links symmetric and asymmetric, and the unusable readings; the
 * USB3 allocation's link bound; and XhciCmUsb4V2 0, the version 1 run.
 * Task 45.56 (record 22 section 6.2.5): Enable Gen 4 Link Recovery set on
 * both ends of a depth-1 Gen 4 link and where it is not, the recovery
 * check after a LINK_RECOVERY, and the clears at a teardown, a stop, a
 * suspend and where it is no longer admitted.
 * A suite of its own beside test_u4cm, with the same fabric. Expected
 * values are worked from record 22 and the specification, not from the
 * code under test.
 */

#include <stdio.h>
#include <string.h>
#include "../src/cm/tbt_u4cm.h"
#include "../src/cm/tbt_hw.h"
#include "u4_fakefab.h"
#include "test_harness.h"

static FAB g_fab;
static TBT_RING g_ring;
static TBT_U4CM g_cm;

/* Lane, TMU and PCIe offsets of the fabric's adapters (u4_fakefab.h). */
#define P19         (FAB_CAP_PORT + 19)
#define TMU8        (FAB_CAP_TMU_ADP + 8)
#define TMU9        (FAB_CAP_TMU_ADP + 9)
#define TMU6        (FAB_CAP_TMU_ADP + 6)
#define PCIE0       (FAB_CAP_ADP)
#define PCIE1       (FAB_CAP_ADP + 1)
#define HOST_PCIE   5       /* port 1's PCIe Down on the minimal host */
#define DEV_PCIE    5       /* the device router's PCIe Up */

static void cfgDefault(TBT_U4_CONFIG *c)
{
    TbtU4DefaultConfig(c);
    c->RingFlags = TBT_RING_START_MASK_IMC;
}

static LONG start(const TBT_U4_CONFIG *c)
{
    memset(&g_ring, 0, sizeof(g_ring));
    TbtU4Init(&g_cm, &g_ring, &g_fab.Host, c);
    return TbtU4Start(&g_cm, fabMem(&g_fab), FAB_PHYS, FAB_MEM_BYTES);
}

static void poll(ULONG ticks)
{
    ULONG i;

    for (i = 0; i < ticks; i++) {
        TbtU4Poll(&g_cm);
        g_fab.Host.Sleep(g_fab.Host.Context, 200);
    }
}

static const TBT_U4_PORT *port(ULONG n)
{
    return TbtU4Port(&g_cm, n);
}

/* A domain: a version 2 host (or the version 1 minimal one) and one device
 * router of `kind` on port 1, plugged; nothing started. */
static int domain(int v2Host, int kind)
{
    int dev;

    fabInit(&g_fab);
    if (v2Host) {
        fabHostV2(&g_fab);
    } else {
        fabHostMinimal(&g_fab);
    }
    dev = fabDevice(&g_fab, kind);
    fabPlug(&g_fab, 1, dev);
    return dev;
}

/* The writes the fabric logged to one register. */
static unsigned long writesTo(int router, unsigned long cs, unsigned long adp,
                              unsigned long addr)
{
    unsigned long i;
    unsigned long n;

    n = 0;
    for (i = 0; i < g_fab.WrCount && i < FAB_WRLOG; i++) {
        if (g_fab.WrRouter[i] == router && g_fab.WrCs[i] == cs &&
            g_fab.WrAdp[i] == adp && g_fab.WrAddr[i] == addr) {
            n++;
        }
    }
    return n;
}

/* The first logged write to a register whose value has a bit of `mask`
 * set (`set`) or clear (!`set`), from `from`; -1 for none. */
static long writeWith(long from, int router, unsigned long cs, unsigned long adp,
                      unsigned long addr, unsigned long mask, int set)
{
    long i;

    for (i = from; i < (long)g_fab.WrCount && i < FAB_WRLOG; i++) {
        if (g_fab.WrRouter[i] == router && g_fab.WrCs[i] == cs &&
            g_fab.WrAdp[i] == adp && g_fab.WrAddr[i] == addr &&
            (((g_fab.WrVal[i] & mask) != 0) == (set != 0))) {
            return i;
        }
    }
    return -1;
}

/* Record 22 section 6.2.2 item 4: every PATH_CS write a two-DW request on
 * an even address (PATH_CS_0 first). The count of those that are not. */
static unsigned long pathWritesNotPaired(void)
{
    unsigned long i;
    unsigned long bad;

    bad = 0;
    for (i = 0; i < g_fab.WrCount && i < FAB_WRLOG; i++) {
        if (g_fab.WrCs[i] != TBT_CS_PATH) {
            continue;
        }
        if (g_fab.WrN[i] != 2) {
            bad++;
        } else if (i + 1 < g_fab.WrCount && (g_fab.WrAddr[i] & 1UL) == 0 &&
                   g_fab.WrCs[i + 1] == TBT_CS_PATH &&
                   g_fab.WrAddr[i + 1] == g_fab.WrAddr[i] + 1) {
            i++;
        } else {
            bad++;
        }
    }
    return bad;
}

static unsigned long pathWrites(void)
{
    unsigned long i;
    unsigned long n;

    n = 0;
    for (i = 0; i < g_fab.WrCount && i < FAB_WRLOG; i++) {
        if (g_fab.WrCs[i] == TBT_CS_PATH) {
            n++;
        }
    }
    return n;
}

/* A notification from the depth-1 router on port 1 (ERR_LINK: an event,
 * acknowledged in the router's form), with its Sequence bit set. */
static void notifyFromDevice(void)
{
    unsigned long dw[3];

    dw[0] = 0x80000000UL;
    dw[1] = (unsigned long)fabPortLane0(&g_fab, 1);
    dw[2] = TBT_EV_ERR_LINK | (1UL << 8) | TBT_NOTIFY_SQ;
    fabInject(&g_fab, TBT_PDF_NOTIFY, dw, 3);
}

/* ------------------------------------------------------------------ */
/* CMUV (section 6.2.1)                                                */
/* ------------------------------------------------------------------ */

static void test_cmuv(void)
{
    TBT_U4_CONFIG c;
    int d2;
    int d1;

    /* A version 2 host, a version 2 router on port 1 and a version 1 one
     * on port 2, all enumerated here. */
    d2 = domain(1, FAB_DEV_USB4V2);
    d1 = fabDevice(&g_fab, FAB_DEV_USB4);
    fabPlug(&g_fab, 2, d1);
    cfgDefault(&c);
    CHECK(c.Usb4V2, "XhciCmUsb4V2 1 by default (decision U30)");
    CHECK_EQ(start(&c), TBT_OK, "v2 domain: start");
    CHECK_EQ((fabRtr(&g_fab, 0, 4) >> 8) & 0xFF, 0x20, "the v2 host router: CMUV 20h");
    CHECK_EQ((fabRtr(&g_fab, d2, 4) >> 8) & 0xFF, 0x20, "the v2 device router: CMUV 20h");
    CHECK_EQ((fabRtr(&g_fab, d1, 4) >> 8) & 0xFF, 0x10, "the v1 device router: CMUV 10h");
    CHECK_EQ(port(1)->Dev.Cmuv, 0x20, "recorded 20h");
    CHECK_EQ(port(2)->Dev.Cmuv, 0x10, "recorded 10h");
    CHECK_EQ(port(1)->State, TBT_U4_UP, "the v2 port UP");
    CHECK_EQ(port(2)->State, TBT_U4_UP, "the v1 port UP");
    CHECK_EQ(writesTo(d2, TBT_CS_ROUTER, 0, 4), 1, "ROUTER_CS_4 written once, in the enumeration");

    /* XhciCmUsb4V2 0: 10h everywhere. */
    d2 = domain(1, FAB_DEV_USB4V2);
    cfgDefault(&c);
    c.Usb4V2 = FALSE;
    CHECK_EQ(start(&c), TBT_OK, "v1 run: start");
    CHECK_EQ((fabRtr(&g_fab, 0, 4) >> 8) & 0xFF, 0x10, "v1 run: the host CMUV 10h");
    CHECK_EQ((fabRtr(&g_fab, d2, 4) >> 8) & 0xFF, 0x10, "v1 run: the v2 device CMUV 10h");
    CHECK_EQ(port(1)->State, TBT_U4_UP, "v1 run: UP");

    /* A Thunderbolt 3 router keeps the field as read. */
    d1 = domain(1, FAB_DEV_TBT3_TR);
    cfgDefault(&c);
    CHECK_EQ(start(&c), TBT_OK, "TB3: start");
    CHECK_EQ((fabRtr(&g_fab, d1, 4) >> 8) & 0xFF, 0x00, "TB3: CMUV as read");
}

/* Section 6.2.1 item 3: a preserved router's CMUV read and kept. */
static void cmuvPreserved(unsigned long cmuv, const char *want, const char *what)
{
    TBT_U4_CONFIG c;
    int d;

    d = domain(1, FAB_DEV_USB4V2);
    fabPreos(&g_fab, 1, 0);
    g_fab.R[0].Rcs[4] = (g_fab.R[0].Rcs[4] & ~0xFF00UL) | 0x2000UL;
    g_fab.R[d].Rcs[4] = (g_fab.R[d].Rcs[4] & ~0xFF00UL) | (cmuv << 8);
    g_fab.RxQCount = 0;
    g_fab.WrCount = 0;
    g_fab.Watch = "u4.cmuv";
    g_fab.WatchCount = 0;
    cfgDefault(&c);
    (void)start(&c);
    CHECK_EQ(port(1)->State, TBT_U4_UP, what);
    CHECK_EQ(port(1)->Dev.Cmuv, cmuv, "the CMUV found recorded");
    CHECK_EQ(writesTo(d, TBT_CS_ROUTER, 0, 4), 0, "ROUTER_CS_4 never written");
    CHECK_EQ(writesTo(0, TBT_CS_ROUTER, 0, 4), 0, "nor the host's");
    CHECK_EQ((fabRtr(&g_fab, d, 4) >> 8) & 0xFF, cmuv, "CMUV as found");
    if (want == NULL) {
        CHECK_EQ(g_fab.WatchCount, 0, "nothing logged");
    } else {
        CHECK(g_fab.WatchCount == 1 && strcmp(g_fab.WatchLabel[0], want) == 0, want);
    }
    g_fab.Watch = NULL;
}

static void test_cmuv_preserved(void)
{
    cmuvPreserved(0x10, "u4.cmuv.v1", "10h on a v2 router: adopted");
    cmuvPreserved(0x20, NULL, "20h: adopted");
    cmuvPreserved(0x30, "u4.cmuv.odd", "30h: adopted");
}

/* ------------------------------------------------------------------ */
/* Set Capabilities (section 6.2.2 item 3)                            */
/* ------------------------------------------------------------------ */

static int g_setHookArmed;

/* A notification from the device router, the moment the Set is issued:
 * held until the read-back settles its form. */
static int setHook(FAB *f, unsigned long pdf, int router, unsigned long cs,
                   unsigned long adp, unsigned long addr, unsigned long n,
                   const unsigned long *data)
{
    (void)adp;
    if (g_setHookArmed && pdf == TBT_PDF_WRITE && router > 0 && cs == TBT_CS_ROUTER &&
        addr == 9 && n == 18 && data != NULL && (data[17] & 0xFFFFUL) == 0x32) {
        g_setHookArmed = 0;
        (void)f;
        notifyFromDevice();
    }
    return FAB_HOOK_GO;
}

/* A cold v2 or v1 device router with the capability supported, its Set
 * behaving as `mode`; one notification after the start. */
static void setWith(int kind, int mode, int ignore, int failAfter)
{
    TBT_U4_CONFIG c;
    int d;

    d = domain(1, kind);
    g_fab.R[d].CapsData = 1UL << 4;
    g_fab.R[d].RopMode[FAB_OP_SETCAPS] = mode;
    g_fab.R[d].SetCapsIgnore = ignore;
    g_fab.R[d].CapsFailAfterSet = failAfter;
    cfgDefault(&c);
    (void)start(&c);
    g_fab.AcksPlain = 0;
    g_fab.AcksEnh = 0;
    notifyFromDevice();
    poll(1);
}

static void test_set_capabilities(void)
{
    TBT_U4_CONFIG c;
    const TBT_U4_PORT *p;
    int d;

    /* Enabled and read back: enhanced acknowledgements. */
    setWith(FAB_DEV_USB4V2, FAB_ROP_OK, 0, 0);
    p = port(1);
    CHECK_EQ(g_fab.R[fabDevAt(&g_fab, 1)].SetCapsOps, 1, "v2: one Set Capabilities");
    CHECK_EQ(g_fab.R[fabDevAt(&g_fab, 1)].SetCapsLastMeta, 0x102, "index 2, Enable 1");
    CHECK_EQ(p->Dev.CapsState, TBT_U4_CAPS_KNOWN, "v2: the read-back adopted");
    CHECK(p->Dev.CapsEnabled & (1UL << TBT_CAP_NOTIFY_SEQUENCE), "v2: enabled");
    CHECK_EQ(g_fab.AcksEnh, 1, "v2: the enhanced acknowledgement");
    CHECK_EQ(g_fab.AcksPlain, 0, "v2: not the plain one");
    CHECK_EQ(p->State, TBT_U4_UP, "v2: UP");
    CHECK(g_cm.Counters.SeqEnabled >= 1, "counted");

    /* The host router too (a USB4 router reporting it). */
    CHECK_EQ(g_fab.R[0].SetCapsOps, 1, "the v2 host router: one Set");
    CHECK_EQ(g_cm.HostRouter.CapsState, TBT_U4_CAPS_KNOWN, "the host's read-back adopted");

    /* A version 1 router reporting the capability: the duty is the
     * connection manager's (section 6.2.2 item 3). */
    setWith(FAB_DEV_USB4, FAB_ROP_OK, 0, 0);
    CHECK_EQ(g_fab.R[fabDevAt(&g_fab, 1)].SetCapsOps, 1, "v1 router: Set too");
    CHECK_EQ(g_fab.AcksEnh, 1, "v1 router: enhanced acknowledgement");

    /* Refusals: plain, logged. */
    setWith(FAB_DEV_USB4V2, FAB_ROP_NS, 0, 0);
    CHECK_EQ(port(1)->Dev.CapsState, TBT_U4_CAPS_KNOWN, "Set not supported: read back");
    CHECK_EQ(port(1)->Dev.CapsEnabled & (1UL << TBT_CAP_NOTIFY_SEQUENCE), 0, "disabled");
    CHECK_EQ(g_fab.AcksPlain, 1, "Set not supported: plain");
    CHECK_EQ(g_fab.AcksEnh, 0, "and never enhanced");
    setWith(FAB_DEV_USB4V2, FAB_ROP_FAIL, 0, 0);
    CHECK_EQ(g_fab.AcksPlain, 1, "Set Status 1: plain");
    CHECK(g_cm.Counters.SeqRefused >= 1, "refused, counted");
    setWith(FAB_DEV_USB4V2, FAB_ROP_OK, 1, 0);
    CHECK_EQ(g_fab.AcksPlain, 1, "Set answered, nothing enabled: plain");
    CHECK_EQ(port(1)->State, TBT_U4_UP, "and the port UP");

    /* Failures: unknown, no acknowledgement of either form, no BA. */
    setWith(FAB_DEV_USB4V2, FAB_ROP_HANG, 0, 0);
    p = port(1);
    CHECK_EQ(p->Dev.CapsState, TBT_U4_CAPS_UNKNOWN, "Set never completing: unknown");
    CHECK(p->Dev.OpBlocked, "the router operation-blocked");
    CHECK_EQ(g_fab.AcksPlain + g_fab.AcksEnh, 0, "no acknowledgement");
    CHECK(!p->Dev.BaValid, "no Buffer Allocation Request");
    setWith(FAB_DEV_USB4V2, FAB_ROP_OK, 0, -1);
    p = port(1);
    CHECK_EQ(p->Dev.CapsState, TBT_U4_CAPS_UNKNOWN, "read-back failing: unknown");
    CHECK_EQ(g_fab.AcksPlain + g_fab.AcksEnh, 0, "no acknowledgement");
    CHECK(g_cm.Counters.SeqUnknown >= 1, "counted");

    /* PENDING, then enhanced: a notification arriving while the Set runs
     * is held, then acknowledged once, in the enhanced form. */
    d = domain(1, FAB_DEV_USB4V2);
    g_fab.Hook = setHook;
    g_setHookArmed = 1;
    cfgDefault(&c);
    (void)start(&c);
    CHECK_EQ(g_setHookArmed, 0, "the notification sent during the Set");
    CHECK_EQ(g_fab.AcksEnh, 1, "held, then acknowledged enhanced");
    CHECK_EQ(g_fab.AcksPlain, 0, "never plain");
    g_fab.Hook = NULL;
    (void)d;

    /* None with XhciCmUsb4V2 0, nor to a Thunderbolt 3 router. */
    d = domain(1, FAB_DEV_USB4V2);
    cfgDefault(&c);
    c.Usb4V2 = FALSE;
    (void)start(&c);
    CHECK_EQ(g_fab.R[d].SetCapsOps + g_fab.R[0].SetCapsOps, 0, "v1 run: no Set");
    CHECK_EQ(port(1)->Dev.CapsState, TBT_U4_CAPS_DEFAULT, "v1 run: at its default");
    d = domain(1, FAB_DEV_TBT3_TR);
    g_fab.R[d].CapsData = 1UL << 4;
    cfgDefault(&c);
    (void)start(&c);
    CHECK_EQ(g_fab.R[d].SetCapsOps, 0, "TB3: no Set");

    /* A preserved version 1 router a predecessor enumerated with CMUV 10h,
     * the capability supported and disabled: the same Set (item 3.4);
     * one found enabled is adopted with no Set (item 3.2). */
    d = domain(0, FAB_DEV_USB4);
    g_fab.R[d].CapsData = 1UL << 4;
    fabPreos(&g_fab, 1, 0);
    g_fab.RxQCount = 0;
    cfgDefault(&c);
    (void)start(&c);
    CHECK_EQ(port(1)->State, TBT_U4_UP, "preserved: UP");
    CHECK_EQ(g_fab.R[d].SetCapsOps, 1, "preserved, disabled: Set");
    CHECK(port(1)->Dev.CapsEnabled & (1UL << TBT_CAP_NOTIFY_SEQUENCE), "and enabled");
    d = domain(0, FAB_DEV_USB4);
    g_fab.R[d].CapsData = (1UL << 4) | (1UL << 5);
    fabPreos(&g_fab, 1, 0);
    g_fab.RxQCount = 0;
    cfgDefault(&c);
    (void)start(&c);
    CHECK_EQ(g_fab.R[d].SetCapsOps, 0, "preserved, enabled: adopted, no Set");
    CHECK_EQ(port(1)->Dev.CapsState, TBT_U4_CAPS_KNOWN, "adopted");
}

/* Section 7.7 and 6.2.2 item 3: a recovery reads alone. A router this
 * driver sent the Set has its answer adopted even after a read-back that
 * failed; one found with the capability disabled again gets its Set at the
 * first tick after the commit. */
static void recoverWith(void)
{
    BOOLEAN final;

    g_fab.Gone = 1;
    TbtU4Contain(&g_cm);
    g_fab.Gone = 0;
    g_fab.Cancel = 0;
    CHECK_EQ(TbtU4Recover(&g_cm, &final), TBT_OK, "recovered");
}

static int g_unplugOnGet;
static unsigned long g_setsAfter;

/* The owed Get Capabilities of the tick after a recovery: the device
 * unplugged the moment it is issued. */
static int unplugOnGetHook(FAB *f, unsigned long pdf, int router, unsigned long cs,
                           unsigned long adp, unsigned long addr, unsigned long n,
                           const unsigned long *data)
{
    (void)adp;
    (void)router;
    if (!g_unplugOnGet && pdf == TBT_PDF_WRITE && cs == TBT_CS_ROUTER && addr == 9 &&
        n == 18 && data != NULL && (data[17] & 0xFFFFUL) == 0x32) {
        g_setsAfter++;     /* any Set request after the unplug, routed or not */
    }
    if (g_unplugOnGet && pdf == TBT_PDF_WRITE && router > 0 && cs == TBT_CS_ROUTER &&
        addr == 9 && n == 18 && data != NULL && (data[17] & 0xFFFFUL) == 0x31) {
        g_unplugOnGet = 0;
        fabUnplug(f, 1);
        return FAB_HOOK_SWALLOW;
    }
    return FAB_HOOK_GO;
}

static void test_set_and_recovery(void)
{
    TBT_U4_CONFIG c;
    int d;

    d = domain(1, FAB_DEV_USB4V2);
    g_fab.R[d].CapsFailAfterSet = 1;
    cfgDefault(&c);
    (void)start(&c);
    CHECK_EQ(port(1)->Dev.CapsState, TBT_U4_CAPS_UNKNOWN, "the read-back failed: unknown");
    recoverWith();
    CHECK_EQ(g_fab.R[d].SetCapsOps, 1, "no Set in the recovery");
    CHECK_EQ(port(1)->Dev.CapsState, TBT_U4_CAPS_KNOWN, "the recovery's answer adopted");
    CHECK(port(1)->Dev.CapsEnabled & (1UL << TBT_CAP_NOTIFY_SEQUENCE),
          "the Sequence bit the Set enabled");
    TbtU4RecoverCommit(&g_cm);
    g_fab.AcksPlain = 0;
    g_fab.AcksEnh = 0;
    notifyFromDevice();
    poll(2);
    CHECK_EQ(g_fab.AcksEnh, 1, "enhanced after the recovery");
    CHECK_EQ(g_fab.AcksPlain, 0, "never plain");

    /* The capability found disabled again: no Set before the commit, one
     * at the first tick after it. */
    d = domain(1, FAB_DEV_USB4V2);
    cfgDefault(&c);
    (void)start(&c);
    CHECK_EQ(g_fab.R[d].SetCapsOps, 1, "the Set at the connection");
    g_fab.R[d].CapsData &= ~(1UL << 5);
    recoverWith();
    CHECK_EQ(g_fab.R[d].SetCapsOps, 1, "none in the recovery");
    CHECK(port(1)->Dev.SeqOwed, "owed");
    CHECK_EQ(port(1)->Dev.CapsEnabled & (1UL << TBT_CAP_NOTIFY_SEQUENCE), 0,
             "the state as read: disabled, plain");
    TbtU4RecoverCommit(&g_cm);
    poll(2);
    CHECK_EQ(g_fab.R[d].SetCapsOps, 2, "the owed Set after the commit");
    CHECK(!port(1)->Dev.SeqOwed, "no longer owed");
    CHECK(port(1)->Dev.CapsEnabled & (1UL << TBT_CAP_NOTIFY_SEQUENCE), "enabled again");

    /* The device unplugged under the owed Get: no Set, the port torn down
     * by its next check. */
    d = domain(1, FAB_DEV_USB4V2);
    cfgDefault(&c);
    (void)start(&c);
    g_fab.R[d].CapsData &= ~(1UL << 5);
    recoverWith();
    CHECK(port(1)->Dev.SeqOwed, "unplug under the Get: owed");
    TbtU4RecoverCommit(&g_cm);
    poll(1);    /* the commit's checks */
    g_fab.Hook = unplugOnGetHook;
    g_unplugOnGet = 1;
    g_setsAfter = 0;
    poll(3);
    g_fab.Hook = NULL;
    CHECK_EQ(g_unplugOnGet, 0, "the Get issued, the device pulled");
    CHECK_EQ(g_setsAfter, 0, "no Set request sent after it");
    CHECK_EQ(port(1)->State, TBT_U4_EMPTY, "torn down");
}

/* Section 7.7 part 3 step 4 (iii): a USB3 tunnel whose host side was lost
 * (its two host entries and the host's USB3 Down Path Enable gone, the
 * device side intact) is rebuilt after a recovery only when the link's
 * enhanced TMU reads exactly as set up; each adapter field changed alone
 * fails that, and the tunnel is drained as failed instead. */
static void tmuAsLeft(int r, unsigned long reg, unsigned long clr, unsigned long set,
                      const char *what)
{
    TBT_U4_CONFIG c;
    int d;
    int rr;

    d = domain(1, FAB_DEV_USB4V2);
    cfgDefault(&c);
    (void)start(&c);
    CHECK(port(1)->Usb3.Up && port(1)->TmuEnhanced, what);
    rr = r == 0 ? 0 : d;
    if (reg != 0) {
        g_fab.R[rr].Adp[1].Cs[reg] = (g_fab.R[rr].Adp[1].Cs[reg] & ~clr) | set;
    }
    g_fab.R[0].Adp[7].Path[2 * 8] &= ~0x80000000UL;
    g_fab.R[0].Adp[1].Path[2 * port(1)->Usb3.Hu] &= ~0x80000000UL;
    g_fab.R[0].Adp[7].Cs[PCIE0] &= ~0x80000000UL;
    recoverWith();
    if (reg == 0) {
        CHECK(port(1)->RebuildUsb3 && !port(1)->Usb3Failed, "as left: owed its rebuild");
    } else {
        CHECK(!port(1)->RebuildUsb3 && port(1)->Usb3Failed, "changed: failed, no rebuild");
    }
}

static void test_tmu_as_left(void)
{
    tmuAsLeft(0, 0, 0, 0, "TMU as left: the control");
    tmuAsLeft(1, TMU8, 0x03FF0000UL, 1UL << 16, "device Replenish Threshold changed");
    tmuAsLeft(0, TMU8, 0x03FF0000UL, 1UL << 16, "host Replenish Threshold changed");
    tmuAsLeft(1, TMU9, 0xFFUL, 31UL, "device ReplenishN changed");
    tmuAsLeft(0, TMU9, 0xFFUL, 31UL, "host ReplenishN changed");
    tmuAsLeft(1, TMU8, 0x7FFFUL, 3000UL, "device Replenish Timeout changed");
    tmuAsLeft(1, TMU9, 0xFF00UL, 128UL << 8, "device DirSwitchN changed");
}

/* Section 9.3: a cleaning's Downstream Port Reset returns the router to
 * its defaults; a preserved router's enabled capability is gone, and a
 * Set refused after it leaves plain acknowledgements. */
static void test_clean_resets_caps(void)
{
    TBT_U4_CONFIG c;
    int d;

    d = domain(1, FAB_DEV_USB4V2);
    g_fab.R[d].CapsData = (1UL << 4) | (1UL << 5);
    fabPreos(&g_fab, 1, 0);
    g_fab.R[0].Rcs[4] |= 0x2000UL;
    g_fab.R[d].SetCapsIgnore = 1;
    g_fab.RxQCount = 0;
    cfgDefault(&c);
    c.PreOsCleanup = 2;
    (void)start(&c);
    poll(3);
    CHECK(g_cm.Counters.PortsCleaned >= 1, "cleaned");
    CHECK_EQ(port(1)->State, TBT_U4_UP, "rebuilt: UP");
    CHECK_EQ(g_fab.R[d].CapsData & (1UL << 5), 0, "the capability disabled by the reset");
    CHECK_EQ(port(1)->Dev.CapsEnabled & (1UL << TBT_CAP_NOTIFY_SEQUENCE), 0,
             "recorded disabled after the refused Set");
    g_fab.AcksPlain = 0;
    g_fab.AcksEnh = 0;
    notifyFromDevice();
    poll(2);
    CHECK_EQ(g_fab.AcksPlain, 1, "plain acknowledgement");
    CHECK_EQ(g_fab.AcksEnh, 0, "never enhanced");
}

/* ------------------------------------------------------------------ */
/* Two-DW path writes (section 6.2.2 item 4)                           */
/* ------------------------------------------------------------------ */

static int g_swallow;
static unsigned long g_swallowed;

/* While armed, every invalidating PATH_CS write to the host lane 0's hu
 * entry swallowed: the entry stays Valid and is quarantined; once
 * disarmed the idle tick's retry writes it again. */
static int swallowHook(FAB *f, unsigned long pdf, int router, unsigned long cs,
                       unsigned long adp, unsigned long addr, unsigned long n,
                       const unsigned long *data)
{
    (void)f;
    (void)n;
    if (g_swallow && pdf == TBT_PDF_WRITE && router == 0 && cs == TBT_CS_PATH &&
        adp == 1 && addr == 2 * 9 && data != NULL && (data[0] & 0x80000000UL) == 0) {
        g_swallowed++;
        return FAB_HOOK_SWALLOW;
    }
    return FAB_HOOK_GO;
}

static int quarantined(void)
{
    ULONG i;

    for (i = 0; i < TBT_U4_QUARANTINE; i++) {
        if (g_cm.Quarantine[i].Used) {
            return 1;
        }
    }
    return 0;
}

static void twoDwords(int v2, const char *what)
{
    TBT_U4_CONFIG c;
    int d;

    d = domain(v2, v2 ? FAB_DEV_USB4V2 : FAB_DEV_USB4);
    cfgDefault(&c);
    (void)start(&c);
    CHECK_EQ(port(1)->State, TBT_U4_UP, what);
    g_fab.Hook = swallowHook;
    g_swallow = 1;
    g_swallowed = 0;
    fabUnplug(&g_fab, 1);
    poll(2);
    CHECK(g_swallowed >= 1, "the invalidation swallowed");
    CHECK(quarantined(), "the entry quarantined");
    g_swallow = 0;
    g_fab.Hook = NULL;
    poll(2);
    CHECK(g_cm.Counters.QuarantineLifted >= 1, "and its quarantine retried and lifted");
    CHECK_EQ(fabPath0(&g_fab, 0, 1, 9) & 0x80000000UL, 0, "the entry closed by the retry");
    CHECK(pathWrites() >= 8, "path writes made");
    CHECK_EQ(pathWritesNotPaired(), 0, "every PATH_CS write two DWs, PATH_CS_0 first");
    /* An invalidation writes PATH_CS_1 as read but RsvdZ bit 29. */
    {
        unsigned long i;
        unsigned long bad = 0;
        for (i = 0; i + 1 < g_fab.WrCount && i + 1 < FAB_WRLOG; i++) {
            if (g_fab.WrCs[i] == TBT_CS_PATH && (g_fab.WrAddr[i] & 1UL) == 0 &&
                (g_fab.WrVal[i] & 0x80000000UL) == 0 &&
                (g_fab.WrVal[i + 1] & 0x20000000UL) != 0) {
                bad++;
            }
        }
        CHECK_EQ(bad, 0, "bit 29 written 0");
    }
    (void)d;
}

static void test_two_dwords(void)
{
    twoDwords(1, "two DWs: a v2 domain UP");
    twoDwords(0, "two DWs: a v1 domain UP");
}

/* ------------------------------------------------------------------ */
/* The TMU (section 6.2.2 item 6)                                     */
/* ------------------------------------------------------------------ */

static void test_tmu_enhanced(void)
{
    TBT_U4_CONFIG c;
    const TBT_U4_PORT *p;
    unsigned long v;
    long dEudm;
    long hEudm;
    long dDts;
    long hDts;
    int d;
    int k;

    d = domain(1, FAB_DEV_USB4V2);
    cfgDefault(&c);
    (void)start(&c);
    p = port(1);
    CHECK_EQ(p->State, TBT_U4_UP, "enhanced: UP");
    CHECK(p->Usb3.Up, "with its USB3 tunnel");
    CHECK(p->TmuConfigured && p->TmuEnhanced && p->TmuOk, "HiFi Enhanced Uni-Directional");
    for (k = 0; k < 2; k++) {
        int r = k == 0 ? d : 0;
        v = fabAdp(&g_fab, r, 1, TMU8);
        CHECK(v & 0x8000UL, "EUDM 1");
        CHECK_EQ(v & 0x7FFFUL, 3125, "Replenish Timeout 3125");
        CHECK_EQ((v >> 16) & 0x3FFUL, 0, "Replenish Threshold 0");
        v = fabAdp(&g_fab, r, 1, TMU9);
        CHECK_EQ(v >> 16, 16, "AdapterTimeSyncInterval 16");
        CHECK_EQ((v >> 8) & 0xFFUL, 255, "DirSwitchN 255");
        CHECK_EQ(v & 0xFFUL, 30, "ReplenishN 30");
        CHECK_EQ(fabAdp(&g_fab, r, 1, TMU6) & 2UL, 0, "time sync enabled");
        CHECK_EQ(fabAdp(&g_fab, r, 1, FAB_CAP_TMU_ADP + 3) & 0x20000000UL, 0, "UDM 0");
    }
    CHECK_EQ(writesTo(d, TBT_CS_ROUTER, 0, FAB_RCAP_TMU + 3), 0,
             "TSPacketInterval not written in that mode");
    CHECK_EQ((fabRtr(&g_fab, d, FAB_RCAP_TMU) >> 16) & 0x7FFUL, 800, "the window 800");
    CHECK_EQ(fabRtr(&g_fab, d, FAB_RCAP_TMU) & 0x08000000UL, 0, "no time disruption left");
    dEudm = writeWith(0, d, TBT_CS_ADAPTER, 1, TMU8, 0x8000UL, 1);
    hEudm = writeWith(0, 0, TBT_CS_ADAPTER, 1, TMU8, 0x8000UL, 1);
    CHECK(dEudm >= 0 && hEudm > dEudm, "EUDM: the device's, then the host's");
    dDts = writeWith(dEudm, d, TBT_CS_ADAPTER, 1, TMU6, 2UL, 0);
    hDts = writeWith(dEudm, 0, TBT_CS_ADAPTER, 1, TMU6, 2UL, 0);
    CHECK(dDts > hEudm && hDts > dDts, "then Disable Time Sync 0, device first");

    /* The USB3 tunnel torn down with the router there: the enhanced TMU
     * stays for the connection's life. */
    CHECK_EQ(TbtU4Eject(&g_cm, 1), TBT_OK, "eject");
    CHECK(port(1)->TmuConfigured && port(1)->TmuEnhanced, "the TMU kept past the eject");
    CHECK(fabAdp(&g_fab, d, 1, TMU8) & 0x8000UL, "EUDM still 1");
    /* At unplug only the host's port remains. */
    fabUnplug(&g_fab, 1);
    poll(2);
    CHECK_EQ(port(1)->State, TBT_U4_EMPTY, "unplugged");
    CHECK_EQ(fabAdp(&g_fab, 0, 1, TMU8) & 0x8000UL, 0, "host EUDM 0");
    CHECK_EQ(fabAdp(&g_fab, 0, 1, TMU9) >> 16, 0, "host interval 0");
    CHECK(fabAdp(&g_fab, 0, 1, TMU6) & 2UL, "host Disable Time Sync 1");

    /* PCIe only (XhciCmUsb3Tunnel 0): still enhanced. */
    d = domain(1, FAB_DEV_USB4V2);
    cfgDefault(&c);
    c.Usb3Tunnel = FALSE;
    (void)start(&c);
    p = port(1);
    CHECK(!p->Usb3.Up && p->Pcie.Up, "PCIe only");
    CHECK(p->TmuConfigured && p->TmuEnhanced, "enhanced whatever the tunnels");
    CHECK(fabAdp(&g_fab, d, 1, TMU8) & 0x8000UL, "EUDM 1 with no USB3 tunnel");

    /* A v2 host and a v1 device: HiFi-Bi, for the USB3 tunnel alone. */
    d = domain(1, FAB_DEV_USB4);
    cfgDefault(&c);
    (void)start(&c);
    p = port(1);
    CHECK(p->TmuConfigured && !p->TmuEnhanced, "v2-v1: HiFi-Bi");
    CHECK_EQ((fabRtr(&g_fab, d, FAB_RCAP_TMU + 3) >> 16) & 0xFFFFUL, 16,
             "v2-v1: TSPacketInterval 16");
    CHECK_EQ(fabAdp(&g_fab, 0, 1, TMU8) & 0x8000UL, 0, "v2-v1: EUDM never set");
    CHECK_EQ(TbtU4Eject(&g_cm, 1), TBT_OK, "v2-v1: eject");
    CHECK(!port(1)->TmuConfigured, "v2-v1: HiFi-Bi disabled with its last USB3 tunnel");
    d = domain(1, FAB_DEV_USB4);
    cfgDefault(&c);
    c.Usb3Tunnel = FALSE;
    (void)start(&c);
    CHECK(!port(1)->TmuConfigured, "v2-v1, PCIe only: no TMU");

    /* XhciCmUsb4V2 0: HiFi-Bi on a v2-v2 link, for USB3 alone. */
    d = domain(1, FAB_DEV_USB4V2);
    cfgDefault(&c);
    c.Usb4V2 = FALSE;
    (void)start(&c);
    CHECK(port(1)->TmuConfigured && !port(1)->TmuEnhanced, "v1 run: HiFi-Bi");
    CHECK_EQ(writesTo(d, TBT_CS_ADAPTER, 1, TMU8), 0, "v1 run: no enhanced field written");
}

/* ------------------------------------------------------------------ */
/* CV found (section 6.2.2 item 5)                                     */
/* ------------------------------------------------------------------ */

static int g_cvSwallow;

/* The connection's own read of a preserved router's ROUTER_CS_5 never
 * answered. */
static int cvHook(FAB *f, unsigned long pdf, int router, unsigned long cs,
                  unsigned long adp, unsigned long addr, unsigned long n,
                  const unsigned long *data)
{
    (void)f;
    (void)adp;
    (void)data;
    if (g_cvSwallow && pdf == TBT_PDF_READ && router > 0 && cs == TBT_CS_ROUTER &&
        addr == 5 && n == 1) {
        return FAB_HOOK_SWALLOW;
    }
    return FAB_HOOK_GO;
}

/* A router a predecessor enumerated with CV set and the given PTO and UTO,
 * no tunnel; with g_cvSwallow the connection's read of ROUTER_CS_5 is
 * never answered. */
static int cvFound(int kind, int pto, int uto, int v2)
{
    TBT_U4_CONFIG c;
    int d;
    unsigned long hi;
    unsigned long lo;

    d = domain(1, kind);
    if (g_cvSwallow) {
        g_fab.Hook = cvHook;
    }
    g_fab.R[0].Adp[1].Cs[4] &= ~0x80000000UL;
    hi = 0;
    lo = 1;
    g_fab.R[d].Rcs[1] = (g_fab.R[d].Rcs[1] & ~(7UL << 20)) | (1UL << 20);
    g_fab.R[d].Rcs[2] = lo;
    g_fab.R[d].Rcs[3] = hi | 0x80000000UL;
    g_fab.R[d].Rcs[4] |= 0x10FFUL;
    g_fab.R[d].Rcs[5] = 0x80000000UL | (pto ? 0x01000000UL : 0) | (uto ? 0x02000000UL : 0);
    g_fab.R[d].Rcs[6] |= 0x03000000UL;
    g_fab.RxQCount = 0;
    g_fab.WrCount = 0;
    cfgDefault(&c);
    c.Usb4V2 = (BOOLEAN)(v2 != 0);
    (void)start(&c);
    poll(2);
    return d;
}

static void test_cv_found(void)
{
    int d;

    d = cvFound(FAB_DEV_USB4V2, 0, 1, 1);
    CHECK_EQ(port(1)->State, TBT_U4_HELD, "v2, CV without PTO: HELD");
    CHECK_EQ(port(1)->Why, TBT_U4_WHY_CV_FOUND, "for that reason");
    CHECK_EQ(writesTo(d, TBT_CS_ROUTER, 0, 5), 0, "ROUTER_CS_5 never written");
    CHECK(g_cm.Counters.CvFound >= 1, "counted");
    d = cvFound(FAB_DEV_USB4, 0, 1, 1);
    CHECK_EQ(port(1)->State, TBT_U4_HELD, "v1 router too: HELD");
    CHECK_EQ(writesTo(d, TBT_CS_ROUTER, 0, 5), 0, "v1: ROUTER_CS_5 never written");

    /* CV and PTO without UTO: the PCIe tunnel, no USB3 tunnel. */
    d = cvFound(FAB_DEV_USB4V2, 1, 0, 1);
    CHECK_EQ(port(1)->State, TBT_U4_UP, "CV, PTO, no UTO: UP");
    CHECK(port(1)->Pcie.Up, "the PCIe tunnel built");
    CHECK(!port(1)->Usb3.Up, "no USB3 tunnel");
    CHECK(port(1)->UtoOff, "UTO found clear");
    CHECK_EQ(writesTo(d, TBT_CS_ROUTER, 0, 5), 0, "ROUTER_CS_5 never written");

    /* The connection's read of ROUTER_CS_5 failing: HELD, never
     * configured. */
    g_cvSwallow = 1;
    d = cvFound(FAB_DEV_USB4V2, 1, 1, 1);
    g_fab.Hook = NULL;
    g_cvSwallow = 0;
    CHECK_EQ(port(1)->State, TBT_U4_HELD, "CS_5 unread: HELD");
    CHECK_EQ(writesTo(d, TBT_CS_ROUTER, 0, 5), 0, "CS_5 unread: never written");
    CHECK(!fabPcieUp(&g_fab, 1), "CS_5 unread: no tunnel");

    /* XhciCmUsb4V2 0: today's path, the handshake rerun. */
    d = cvFound(FAB_DEV_USB4V2, 0, 1, 0);
    CHECK_EQ(port(1)->State, TBT_U4_UP, "v1 run: configured again, UP");
    CHECK(writesTo(d, TBT_CS_ROUTER, 0, 5) >= 1, "v1 run: ROUTER_CS_5 written");
}

/* ------------------------------------------------------------------ */
/* Gen 4 Link Recovery (section 6.2.3 item 4)                          */
/* ------------------------------------------------------------------ */

static void elrPreserved(int v2)
{
    TBT_U4_CONFIG c;
    int d;
    int ch;

    d = domain(1, FAB_DEV_USB4V2);
    ch = fabDevice(&g_fab, FAB_DEV_USB4V2);
    fabPreos(&g_fab, 1, 0);
    fabPlugChild(&g_fab, d, ch, 1);
    g_fab.R[0].Adp[1].Cs[P19] |= 0x80000000UL;
    g_fab.R[d].Adp[1].Cs[P19] |= 0x80000000UL;
    g_fab.R[d].Adp[3].Cs[P19] |= 0x80000000UL;
    g_fab.R[ch].Adp[1].Cs[P19] |= 0x80000000UL;
    g_fab.RxQCount = 0;
    cfgDefault(&c);
    c.Usb4V2 = (BOOLEAN)(v2 != 0);
    (void)start(&c);
    CHECK_EQ(port(1)->State, TBT_U4_UP, "ELR: preserved, UP");
    CHECK_EQ(g_cm.Counters.LinksDiscovered, 1, "ELR: the depth-2 link discovered");
    /* Task 45.56: cleared at the adoption, then set again by the tick on
     * the depth-1 Gen 4 link where XhciCmUsb4V2 is 1 (test_elr_adopted). */
    CHECK_EQ(fabAdp(&g_fab, 0, 1, P19) & 0x80000000UL, v2 ? 0x80000000UL : 0,
             "host port: ELR cleared (and set again under v2)");
    CHECK_EQ(fabAdp(&g_fab, d, 1, P19) & 0x80000000UL, v2 ? 0x80000000UL : 0,
             "device upstream: cleared (and set again under v2)");
    CHECK_EQ(fabAdp(&g_fab, d, 3, P19) & 0x80000000UL, 0, "depth 1 downstream: cleared");
    CHECK_EQ(fabAdp(&g_fab, ch, 1, P19) & 0x80000000UL, 0, "depth 2 upstream: cleared");
    CHECK_EQ(g_cm.Counters.ElrCleared, 4, "four cleared");
    CHECK(fabPcieUp(&g_fab, 1), "the preserved tunnel untouched");
}

static void test_elr_and_recovery(void)
{
    TBT_U4_CONFIG c;
    unsigned long acks;
    unsigned long checks0;
    unsigned long dw[3];
    int d;

    elrPreserved(1);
    elrPreserved(0);

    /* A version 1 router's bit is not version 2's: left alone. */
    d = domain(0, FAB_DEV_USB4);
    fabPreos(&g_fab, 1, 0);
    g_fab.R[d].Adp[1].Cs[P19] |= 0x80000000UL;
    g_fab.RxQCount = 0;
    cfgDefault(&c);
    (void)start(&c);
    CHECK(fabAdp(&g_fab, d, 1, P19) & 0x80000000UL, "a v1 router's bit 31 untouched");
    CHECK_EQ(g_cm.Counters.ElrCleared, 0, "nothing cleared");

    /* LINK_RECOVERY: never acknowledged, a port check. */
    d = domain(1, FAB_DEV_USB4V2);
    cfgDefault(&c);
    (void)start(&c);
    CHECK_EQ(port(1)->State, TBT_U4_UP, "LINK_RECOVERY: UP");
    acks = g_fab.AcksPlain + g_fab.AcksEnh;
    checks0 = g_cm.Counters.PortChecks;
    dw[0] = 0x80000000UL;
    dw[1] = 1;
    dw[2] = TBT_EV_LINK_RECOVERY | (1UL << 8);
    fabInject(&g_fab, TBT_PDF_NOTIFY, dw, 3);
    poll(2);
    CHECK_EQ(g_cm.Counters.LinkRecoveries, 1, "delivered");
    CHECK(g_cm.Counters.PortChecks > checks0, "a port check queued and made");
    CHECK_EQ(g_fab.AcksPlain + g_fab.AcksEnh, acks, "never acknowledged");
    CHECK_EQ(port(1)->State, TBT_U4_UP, "the check finds it unchanged");
    /* One from the host router, its Event Info the lane adapter. */
    dw[0] = 0x80000000UL;
    dw[1] = 0;
    dw[2] = TBT_EV_LINK_RECOVERY | (1UL << 8);
    fabInject(&g_fab, TBT_PDF_NOTIFY, dw, 3);
    poll(2);
    CHECK_EQ(g_cm.Counters.LinkRecoveries, 2, "from the host router too");
    CHECK_EQ(g_fab.AcksPlain + g_fab.AcksEnh, acks, "never acknowledged");
    (void)d;
}

/* ------------------------------------------------------------------ */
/* Gen 4 Link Recovery enabled (section 6.2.3 item 4, task 45.56)      */
/* ------------------------------------------------------------------ */

#define ELR         0x80000000UL
#define ILR         0x40000000UL
#define SAF         0x01000000UL
#define TIP         0x01000000UL    /* PORT_CS_18 bit 24 */
#define P18         (FAB_CAP_PORT + 18)
#define USB3CS4     (FAB_CAP_ADP + 4)

/* The device router's USB3 Up adapter. */
static int devUsb3(int d)
{
    unsigned a;

    for (a = 1; a <= g_fab.R[d].MaxAdp; a++) {
        if ((g_fab.R[d].Adp[a].Cs[2] & 0xFFFFFFUL) == 0x200102UL) {
            return (int)a;
        }
    }
    return 0;
}

static unsigned long elrOf(int r)
{
    return fabAdp(&g_fab, r, 1, P19) & ELR;
}

static void drainActions(void)
{
    TBT_U4_ACTION a;

    while (TbtU4NextAction(&g_cm, &a)) {
    }
}

/* A LINK_RECOVERY from the host router for its lane adapter 1. */
static void linkRecovery(void)
{
    unsigned long dw[3];

    dw[0] = 0x80000000UL;
    dw[1] = 0;
    dw[2] = TBT_EV_LINK_RECOVERY | (1UL << 8);
    fabInject(&g_fab, TBT_PDF_NOTIFY, dw, 3);
}

/* gen4() with a configuration of its own: v2 host, v2 device on port 1. */
static int elrStart(int elr, int v2, unsigned long hostW, unsigned long devW)
{
    TBT_U4_CONFIG c;
    int d;

    fabInit(&g_fab);
    fabHostV2(&g_fab);
    d = fabDevice(&g_fab, FAB_DEV_USB4V2);
    g_fab.R[d].Gen4HostW = hostW;
    g_fab.R[d].Gen4DevW = devW;
    fabPlug(&g_fab, 1, d);
    cfgDefault(&c);
    c.Gen4LinkRecovery = (BOOLEAN)(elr != 0);
    c.Usb4V2 = (BOOLEAN)(v2 != 0);
    (void)start(&c);
    return d;
}

static void test_elr_set(void)
{
    TBT_U4_CONFIG c;
    long wd;
    long wh;
    int d;

    /* Part 2: both ends, device end first, each write bit 30 and 24 0. */
    d = elrStart(1, 1, 0, 0);
    CHECK_EQ(port(1)->State, TBT_U4_UP, "ELR: UP");
    CHECK(elrOf(0) && elrOf(d), "set on both ends");
    CHECK(port(1)->ElrSet && port(1)->ElrDev && port(1)->ElrHost, "recorded");
    CHECK_EQ(g_cm.Counters.ElrEnabled, 1, "counted once");
    wd = writeWith(0, d, TBT_CS_ADAPTER, 1, P19, ELR, 1);
    wh = writeWith(0, 0, TBT_CS_ADAPTER, 1, P19, ELR, 1);
    CHECK(wd >= 0 && wh > wd, "device end first");
    CHECK_EQ(writeWith(0, d, TBT_CS_ADAPTER, 1, P19, ILR | SAF, 1), -1, "device: no ILR, no SAF");
    CHECK_EQ(writeWith(0, 0, TBT_CS_ADAPTER, 1, P19, ILR | SAF, 1), -1, "host: no ILR, no SAF");
    CHECK(fabAdp(&g_fab, 0, 1, P19) & 8UL, "written as read: PC kept");
    poll(3);
    CHECK_EQ(g_cm.Counters.ElrEnabled, 1, "never written again while set");
    CHECK(fabPcieUp(&g_fab, 1) && fabUsb3Up(&g_fab, 1), "both tunnels up");

    d = elrStart(1, 1, 0x04, 0x08);
    CHECK(port(1)->Asym && elrOf(0) && elrOf(d), "asymmetric: set on both ends");

    /* Part 1: never with either value 0, on a v2-v1 link, a Gen 3 link. */
    d = elrStart(0, 1, 0, 0);
    poll(2);
    CHECK_EQ(writeWith(0, 0, TBT_CS_ADAPTER, 1, P19, ELR, 1), -1,
             "XhciCmGen4LinkRecovery 0: never on the host");
    CHECK_EQ(writeWith(0, d, TBT_CS_ADAPTER, 1, P19, ELR, 1), -1, "nor on the device");
    CHECK(port(1)->State == TBT_U4_UP && port(1)->Gen4, "the Gen 4 link used as before");
    d = elrStart(1, 0, 0, 0);
    poll(2);
    CHECK(!elrOf(0) && !elrOf(d) && g_cm.Counters.ElrEnabled == 0, "XhciCmUsb4V2 0: never");
    d = domain(1, FAB_DEV_USB4);
    cfgDefault(&c);
    (void)start(&c);
    poll(2);
    CHECK_EQ(port(1)->State, TBT_U4_UP, "v2-v1: UP");
    CHECK_EQ(writeWith(0, 0, TBT_CS_ADAPTER, 1, P19, ELR, 1), -1, "v2-v1: never");
    fabInit(&g_fab);
    fabHostV2(&g_fab);
    d = fabDevice(&g_fab, FAB_DEV_USB4V2);
    g_fab.R[d].Gen4 = 0;
    fabPlug(&g_fab, 1, d);
    cfgDefault(&c);
    (void)start(&c);
    poll(2);
    CHECK(port(1)->State == TBT_U4_UP && !port(1)->Gen4, "two v2 routers at Gen 3: UP");
    CHECK_EQ(writeWith(0, 0, TBT_CS_ADAPTER, 1, P19, ELR, 1), -1, "Gen 3: never");

    /* Part 2's gate: TIP, StartAsymmetricFlow, a lane short of CL0 -
     * nothing written; set at a later tick once they clear. */
    fabInit(&g_fab);
    fabHostV2(&g_fab);
    d = fabDevice(&g_fab, FAB_DEV_USB4V2);
    fabPlug(&g_fab, 1, d);
    g_fab.R[d].Adp[1].Cs[P18] |= TIP;
    cfgDefault(&c);
    (void)start(&c);
    poll(2);
    CHECK_EQ(port(1)->State, TBT_U4_UP, "TIP at the device: UP");
    CHECK(!elrOf(0) && !elrOf(d), "TIP: nothing written");
    g_fab.R[d].Adp[1].Cs[P18] &= ~TIP;
    g_fab.R[0].Adp[1].Cs[P19] |= SAF;
    poll(2);
    CHECK(!elrOf(0) && !elrOf(d), "StartAsymmetricFlow at the host: nothing written");
    CHECK_EQ(writeWith(0, 0, TBT_CS_ADAPTER, 1, P19, ELR, 1), -1,
             "no ELR write while StartAsymmetricFlow reads 1");
    g_fab.R[0].Adp[1].Cs[P19] &= ~SAF;
    g_fab.R[d].Adp[1].Cs[FAB_CAP_LANE + 1] =
        (g_fab.R[d].Adp[1].Cs[FAB_CAP_LANE + 1] & ~(0xFUL << 26)) | (5UL << 26);
    poll(2);
    CHECK(!elrOf(0) && !elrOf(d), "device lane 0 in CL1: nothing written");
    g_fab.R[d].Adp[1].Cs[FAB_CAP_LANE + 1] =
        (g_fab.R[d].Adp[1].Cs[FAB_CAP_LANE + 1] & ~(0xFUL << 26)) | (2UL << 26);
    poll(1);
    CHECK(elrOf(0) && elrOf(d), "all clear: set at the next tick");

    /* A router that keeps the bit 0: refused, both ends 0 again, no retry
     * on this connection; tried again after a re-plug. */
    fabInit(&g_fab);
    fabHostV2(&g_fab);
    d = fabDevice(&g_fab, FAB_DEV_USB4V2);
    g_fab.R[d].ElrStuck = 1;
    fabPlug(&g_fab, 1, d);
    cfgDefault(&c);
    (void)start(&c);
    CHECK_EQ(g_cm.Counters.ElrRefused, 1, "refused");
    CHECK(port(1)->ElrRefused && !port(1)->ElrSet, "recorded refused");
    CHECK(!elrOf(0) && !elrOf(d), "both ends 0");
    CHECK(!port(1)->ElrDev && !port(1)->ElrHost, "nothing owned");
    wd = (long)writesTo(d, TBT_CS_ADAPTER, 1, P19);
    poll(4);
    CHECK_EQ((long)writesTo(d, TBT_CS_ADAPTER, 1, P19), wd, "not tried again");
    CHECK_EQ(port(1)->State, TBT_U4_UP, "the port stays UP");
    fabUnplug(&g_fab, 1);
    poll(3);
    g_fab.R[d].ElrStuck = 0;
    fabPlug(&g_fab, 1, d);
    poll(3);
    CHECK_EQ(port(1)->State, TBT_U4_UP, "re-plugged: UP");
    CHECK(elrOf(0) && elrOf(d) && port(1)->ElrSet, "set on the new connection");

    /* The host refusing after the device took it: the device end written
     * 0 again and read back, nothing owned, no retry on the connection. */
    fabInit(&g_fab);
    fabHostV2(&g_fab);
    d = fabDevice(&g_fab, FAB_DEV_USB4V2);
    g_fab.R[0].ElrStuck = 1;
    fabPlug(&g_fab, 1, d);
    cfgDefault(&c);
    (void)start(&c);
    wd = writeWith(0, d, TBT_CS_ADAPTER, 1, P19, ELR, 1);
    wh = writeWith(0, 0, TBT_CS_ADAPTER, 1, P19, ELR, 1);
    CHECK(wd >= 0 && wh > wd, "host refusing: the device end enabled, then the host tried");
    CHECK(writeWith(wh, d, TBT_CS_ADAPTER, 1, P19, ELR, 0) > wh,
          "the device end then written 0");
    CHECK(!elrOf(0) && !elrOf(d), "both ends read 0");
    CHECK(port(1)->ElrRefused && !port(1)->ElrDev && !port(1)->ElrHost,
          "refused, nothing owned");
    CHECK_EQ(g_cm.Counters.ElrRefused, 1, "counted");
    wd = (long)writesTo(d, TBT_CS_ADAPTER, 1, P19);
    poll(4);
    CHECK_EQ((long)writesTo(d, TBT_CS_ADAPTER, 1, P19), wd, "no retry on the connection");

    /* A preserved DisplayPort tunnel on a v2-v2 Gen 4 link: never set. */
    fabInit(&g_fab);
    fabHostAdl(&g_fab);
    g_fab.R[0].Rcs[4] = (g_fab.R[0].Rcs[4] & 0x00FFFFFFUL) | (0x40UL << 24);
    g_fab.R[0].CapsData = 1UL << 4;
    g_fab.R[0].Gen4 = 1;
    g_fab.Caps = (g_fab.Caps & ~(0xFFUL << 16)) | (0x40UL << 16);
    d = fabDevice(&g_fab, FAB_DEV_USB4V2);
    fabPlug(&g_fab, 1, d);
    fabPreos(&g_fab, 1, 0);
    memset(g_fab.R[0].Adp[8].Path + 16, 0, 8);
    memset(g_fab.R[0].Adp[1].Path + 16, 0, 8 * 4);
    memset(g_fab.R[d].Adp[1].Path + 16, 0, 8 * 4);
    memset(g_fab.R[d].Adp[5].Path + 16, 0, 8);
    g_fab.R[0].Adp[8].Cs[FAB_CAP_ADP] &= ~0x80000000UL;
    g_fab.R[d].Adp[5].Cs[FAB_CAP_ADP] &= ~0x80000000UL;
    fabAddDpOut(&g_fab, d, 8);
    g_fab.R[0].Adp[5].Path[2 * 9] = 0x80000000UL | 12UL | (1UL << 11);
    g_fab.R[0].Adp[5].Path[2 * 8] = 0x80000000UL | 13UL | (1UL << 11);
    g_fab.R[d].Adp[1].Path[2 * 12] = 0x80000000UL | 9UL | (8UL << 11);
    g_fab.R[d].Adp[1].Path[2 * 13] = 0x80000000UL | 8UL | (8UL << 11);
    g_fab.R[d].Adp[8].Path[2 * 8] = 0x80000000UL | 14UL | (1UL << 11);
    g_fab.R[0].Adp[1].Path[2 * 14] = 0x80000000UL | 8UL | (5UL << 11);
    g_fab.R[0].Adp[1].Cs[5] |= TBT_ADP5_DHP;
    g_fab.RxQCount = 0;
    cfgDefault(&c);
    (void)start(&c);
    poll(4);
    CHECK(port(1)->DpPreserved, "DP: the DisplayPort tunnel preserved");
    CHECK(port(1)->State == TBT_U4_UP && port(1)->Gen4, "DP: UP on a Gen 4 link");
    CHECK(c.Usb4V2 && c.Gen4LinkRecovery, "DP: both values 1");
    CHECK_EQ(writeWith(0, 0, TBT_CS_ADAPTER, 1, P19, ELR, 1), -1, "DP: never on the host");
    CHECK_EQ(writeWith(0, d, TBT_CS_ADAPTER, 1, P19, ELR, 1), -1, "DP: nor on the device");
    CHECK(g_fab.R[0].Adp[5].Path[2 * 9] & 0x80000000UL, "DP: the main path kept");
    CHECK(g_fab.R[d].Adp[8].Path[2 * 8] & 0x80000000UL, "DP: the AUX return kept");

    /* A HELD port (a preserved router with CV set and PTO clear). */
    d = cvFound(FAB_DEV_USB4V2, 0, 1, 1);
    CHECK_EQ(port(1)->State, TBT_U4_HELD, "HELD");
    CHECK_EQ(writeWith(0, 0, TBT_CS_ADAPTER, 1, P19, ELR, 1), -1, "HELD: never set");
    (void)d;
}

/* Part 4: the recovery check. */
static void test_elr_check(void)
{
    unsigned long torn;
    unsigned long built;
    int d;
    int u;

    /* Whole: both tunnels kept, nothing written but no removal. */
    d = elrStart(1, 1, 0, 0);
    drainActions();
    torn = g_cm.Counters.PcieTornDown + g_cm.Counters.Usb3TornDown;
    g_fab.WrCount = 0;
    linkRecovery();
    poll(2);
    CHECK_EQ(g_cm.Counters.LinkRecoveries, 1, "delivered");
    CHECK_EQ(g_cm.Counters.ElrChecks, 1, "a recovery check");
    CHECK_EQ(g_cm.Counters.ElrKept, 1, "every tunnel whole: kept");
    CHECK(port(1)->Pcie.Up && port(1)->Usb3.Up && port(1)->State == TBT_U4_UP, "both kept");
    CHECK_EQ(g_cm.Counters.PcieTornDown + g_cm.Counters.Usb3TornDown, torn, "nothing torn down");
    CHECK_EQ(g_fab.WrCount, 0, "the check writes nothing");
    CHECK_EQ(g_cm.ActionCount, 0, "no removal queued");

    /* A PCIe entry gone (the LTSSM still L0): the loss sequence. */
    d = elrStart(1, 1, 0, 0);
    drainActions();
    g_fab.R[0].Adp[HOST_PCIE].LtssmForce = 7;
    g_fab.R[d].Adp[DEV_PCIE].Path[2 * 8] &= ~0x80000000UL;
    linkRecovery();
    poll(2);
    CHECK_EQ(g_cm.Counters.ElrPcieLost, 1, "PCIe lost to the check");
    CHECK_EQ(port(1)->State, TBT_U4_FAILED, "FAILED");
    CHECK(port(1)->PcieFailed && !port(1)->Pcie.Up, "for PCIe");
    CHECK(port(1)->Usb3.Up, "its USB3 tunnel kept");
    CHECK(g_cm.ActionCount >= 1, "the removal queued");

    /* A USB3 Path Enable clear: the USB3 tunnel alone, not rebuilt. */
    d = elrStart(1, 1, 0, 0);
    u = devUsb3(d);
    built = g_cm.Counters.Usb3Built;
    g_fab.R[d].Adp[u].Cs[FAB_CAP_ADP] &= ~0x80000000UL;
    linkRecovery();
    poll(2);
    CHECK_EQ(g_cm.Counters.ElrUsb3Lost, 1, "USB3 torn down by the check");
    CHECK(!port(1)->Usb3.Up && port(1)->Usb3Failed, "down, not built again");
    CHECK(!fabUsb3Up(&g_fab, 1), "host side down too");
    CHECK(port(1)->Pcie.Up && port(1)->State == TBT_U4_UP, "PCIe kept, UP");
    poll(4);
    CHECK_EQ(g_cm.Counters.Usb3Built, built, "no rebuild");

    /* USB3 Recovery then U0: watched, then kept. */
    d = elrStart(1, 1, 0, 0);
    u = fabPortUsb3(&g_fab, 1);
    g_fab.R[0].Adp[u].Usb3Ls = 0x880UL + 1;   /* Link Valid, Recovery */
    linkRecovery();
    poll(2);
    CHECK_EQ(g_cm.Counters.ElrChecks, 0, "USB3 in Recovery: the check owed");
    CHECK(port(1)->ElrCheckOwed, "owed");
    g_fab.R[0].Adp[u].Usb3Ls = 0;
    poll(1);
    CHECK_EQ(g_cm.Counters.ElrKept, 1, "U0: kept");
    CHECK(!port(1)->ElrCheckOwed, "done");

    /* Inactive for 2 s: recorded, the tunnel kept. */
    d = elrStart(1, 1, 0, 0);
    g_fab.R[0].Adp[u].Usb3Ls = 0x680UL + 1;   /* Link Valid, Inactive */
    linkRecovery();
    poll(16);
    CHECK_EQ(g_cm.Counters.ElrUsb3Down, 1, "Inactive 2 s on: recorded");
    CHECK(port(1)->Usb3.Up && fabUsb3Up(&g_fab, 1), "the tunnel kept");
    CHECK_EQ(g_cm.Counters.ElrUsb3Lost, 0, "not torn down");

    /* Part 5: a lost notification owes the check. */
    d = elrStart(1, 1, 0, 0);
    g_cm.Ctl.NotesLost = TRUE;
    poll(2);
    CHECK_EQ(g_cm.Counters.ElrChecks, 1, "NotesLost: checked");

    /* XhciCmGen4LinkRecovery 0: a LINK_RECOVERY still has its check. */
    d = elrStart(0, 1, 0, 0);
    linkRecovery();
    poll(2);
    CHECK_EQ(g_cm.Counters.ElrChecks, 1, "value 0: still checked");
    CHECK_EQ(g_cm.Counters.ElrKept, 1, "and kept");
    (void)d;
}

static int g_secondArmed;
static int g_secondDev;

/* At the PCIe loss sequence's first host write (Path Enable 0 on the
 * host's PCIe Down), a second LINK_RECOVERY arrives and the USB3 tunnel's
 * device Path Enable reads clear: drained at the sequence's safe point. */
static int secondHook(FAB *f, unsigned long pdf, int router, unsigned long cs,
                      unsigned long adp, unsigned long addr, unsigned long n,
                      const unsigned long *data)
{
    unsigned long dw[3];

    (void)n;
    if (g_secondArmed && pdf == TBT_PDF_WRITE && router == 0 && cs == TBT_CS_ADAPTER &&
        adp == HOST_PCIE && addr == PCIE0 && data != NULL &&
        (data[0] & 0x80000000UL) == 0) {
        g_secondArmed = 0;
        f->R[g_secondDev].Adp[devUsb3(g_secondDev)].Cs[FAB_CAP_ADP] &= ~0x80000000UL;
        dw[0] = 0x80000000UL;
        dw[1] = 0;
        dw[2] = TBT_EV_LINK_RECOVERY | (1UL << 8);
        fabInject(f, TBT_PDF_NOTIFY, dw, 3);
    }
    return FAB_HOOK_GO;
}

/* Part 4: a second LINK_RECOVERY drained during the first check keeps the
 * check owed; the next one finds the USB3 tunnel it broke. */
static void test_elr_second(void)
{
    int d;

    d = elrStart(1, 1, 0, 0);
    g_fab.R[0].Adp[HOST_PCIE].LtssmForce = 7;
    g_fab.R[d].Adp[DEV_PCIE].Path[2 * 8] &= ~0x80000000UL;
    g_secondDev = d;
    g_secondArmed = 1;
    g_fab.Hook = secondHook;
    linkRecovery();
    poll(4);
    g_fab.Hook = NULL;
    CHECK_EQ(g_secondArmed, 0, "the second LINK_RECOVERY sent during the loss sequence");
    CHECK_EQ(g_cm.Counters.LinkRecoveries, 2, "both delivered");
    CHECK_EQ(g_cm.Counters.ElrPcieLost, 1, "PCIe lost to the first check");
    CHECK_EQ(g_cm.Counters.ElrUsb3Lost, 1, "the USB3 tunnel to the second");
    CHECK(!port(1)->Usb3.Up && !port(1)->ElrCheckOwed, "torn down, nothing owed");
}

static int g_pathReadSwallow;

/* Device-side path reads swallowed: a register that does not read. */
static int pathReadHook(FAB *f, unsigned long pdf, int router, unsigned long cs,
                        unsigned long adp, unsigned long addr, unsigned long n,
                        const unsigned long *data)
{
    (void)f;
    (void)adp;
    (void)addr;
    (void)n;
    (void)data;
    if (g_pathReadSwallow != 0 && pdf == TBT_PDF_READ &&
        (router == g_pathReadSwallow || (g_pathReadSwallow < 0 && router == 0)) &&
        cs == TBT_CS_PATH) {
        return FAB_HOOK_SWALLOW;
    }
    return FAB_HOOK_GO;
}

static int g_hop20Dev;

/* The device's lane 1 entry at HopID 20 does not read. */
static int hop20Hook(FAB *f, unsigned long pdf, int router, unsigned long cs,
                     unsigned long adp, unsigned long addr, unsigned long n,
                     const unsigned long *data)
{
    (void)f;
    (void)n;
    (void)data;
    if (g_hop20Dev != 0 && pdf == TBT_PDF_READ && router == g_hop20Dev &&
        cs == TBT_CS_PATH && adp == 1 && addr == 2 * 20) {
        return FAB_HOOK_SWALLOW;
    }
    return FAB_HOOK_GO;
}

/* The host's PCIe entry now leading to lane HopID 20, whose device entry
 * does not read: still a mismatch with the recorded HopIDs, lost. */
static void test_elr_moved(void)
{
    int d;

    d = elrStart(1, 1, 0, 0);
    CHECK(port(1)->Pcie.Hd != 20, "the tunnel not at HopID 20");
    g_fab.R[0].Adp[HOST_PCIE].LtssmForce = 7;
    g_fab.R[0].Adp[HOST_PCIE].Path[2 * 8] =
        (g_fab.R[0].Adp[HOST_PCIE].Path[2 * 8] & ~0x7FFUL) | 20UL;
    g_hop20Dev = d;
    g_fab.Hook = hop20Hook;
    linkRecovery();
    poll(3);
    g_fab.Hook = NULL;
    g_hop20Dev = 0;
    CHECK_EQ(g_cm.Counters.ElrPcieLost, 1, "moved HopID: PCIe lost to the check");
    CHECK_EQ(g_cm.Counters.ElrUnread, 0, "not taken as unread");
    CHECK(port(1)->PcieFailed, "FAILED for PCIe");
}

static int g_p19Swallow;

/* A write clearing the host's lane 1 Enable Gen 4 Link Recovery
 * swallowed: a clear that fails (its other writes go through). */
static int p19Hook(FAB *f, unsigned long pdf, int router, unsigned long cs,
                   unsigned long adp, unsigned long addr, unsigned long n,
                   const unsigned long *data)
{
    (void)n;
    if (g_p19Swallow && pdf == TBT_PDF_WRITE && router == 0 && cs == TBT_CS_ADAPTER &&
        adp == 1 && addr == P19 && data != NULL && (data[0] & ELR) == 0 &&
        (f->R[0].Adp[1].Cs[P19] & ELR) != 0) {
        return FAB_HOOK_SWALLOW;
    }
    return FAB_HOOK_GO;
}

static void test_elr_unread(void)
{
    unsigned long torn;
    int d;

    d = elrStart(1, 1, 0, 0);
    torn = g_cm.Counters.PcieTornDown + g_cm.Counters.Usb3TornDown;
    g_fab.Hook = pathReadHook;
    g_pathReadSwallow = d;
    linkRecovery();
    poll(17);
    g_fab.Hook = NULL;
    g_pathReadSwallow = 0;
    CHECK_EQ(g_cm.Counters.ElrUnread, 1, "given up after 2 s");
    CHECK_EQ(g_cm.Counters.PcieTornDown + g_cm.Counters.Usb3TornDown, torn, "nothing torn down");
    CHECK(port(1)->Pcie.Up && port(1)->Usb3.Up, "both kept");

    /* The host's path reads unanswered: each tick's check bounded (one
     * unanswered request a tunnel, no HopID scan). */
    d = elrStart(1, 1, 0, 0);
    g_fab.Hook = pathReadHook;
    g_pathReadSwallow = -1;
    linkRecovery();
    {
        unsigned long t0;
        unsigned long worst;
        int i;

        worst = 0;
        for (i = 0; i < 12; i++) {
            t0 = g_fab.NowUs;
            TbtU4Poll(&g_cm);
            if (g_fab.NowUs - t0 > worst) {
                worst = g_fab.NowUs - t0;
            }
            g_fab.Host.Sleep(g_fab.Host.Context, 200);
        }
        CHECK(worst < 2500000UL, "host reads unanswered: a tick's check bounded");
    }
    g_fab.Hook = NULL;
    g_pathReadSwallow = 0;
    CHECK_EQ(g_cm.Counters.ElrUnread, 1, "given up, nothing torn down");
    CHECK(port(1)->Pcie.Up && port(1)->Usb3.Up, "both kept");
}

/* Parts 3 and 6: the clears. */
static void test_elr_clears(void)
{
    TBT_U4_CONFIG c;
    unsigned long deferred;
    LONG rc;
    int d;

    /* An unplug: the host's end cleared at step 5. */
    d = elrStart(1, 1, 0, 0);
    fabUnplug(&g_fab, 1);
    poll(3);
    CHECK_EQ(port(1)->State, TBT_U4_EMPTY, "unplugged: EMPTY");
    CHECK(!elrOf(0) && !port(1)->ElrHost && !port(1)->ElrDev, "host end cleared");

    /* A clear that fails: retried at the EMPTY tick, never deferring a
     * new connection; one withheld while StartAsymmetricFlow reads 1. */
    d = elrStart(1, 1, 0, 0);
    g_fab.Hook = p19Hook;
    g_p19Swallow = 1;
    fabUnplug(&g_fab, 1);
    poll(3);
    CHECK(elrOf(0) && port(1)->ElrHost, "clear failing: still owned");
    CHECK(g_cm.Counters.ElrClearFailed >= 1, "counted");
    g_p19Swallow = 0;
    g_fab.Hook = NULL;
    g_fab.R[0].Adp[1].Cs[P19] |= SAF;
    poll(2);
    CHECK(elrOf(0) && port(1)->ElrHost, "StartAsymmetricFlow: withheld");
    CHECK_EQ(writeWith(0, 0, TBT_CS_ADAPTER, 1, P19, SAF, 1), -1, "no write of SAF");
    g_fab.R[0].Adp[1].Cs[P19] &= ~SAF;
    poll(1);
    CHECK(!elrOf(0) && !port(1)->ElrHost, "then cleared at the EMPTY tick");
    d = elrStart(1, 1, 0, 0);
    g_fab.Hook = p19Hook;
    g_p19Swallow = 1;
    fabUnplug(&g_fab, 1);
    poll(3);
    deferred = g_cm.Counters.ConnectsDeferred;
    fabPlug(&g_fab, 1, d);
    poll(3);
    g_p19Swallow = 0;
    g_fab.Hook = NULL;
    CHECK_EQ(port(1)->State, TBT_U4_UP, "re-plug with the clear owed: UP");
    CHECK_EQ(g_cm.Counters.ConnectsDeferred, deferred, "not deferred by it");
    poll(2);
    CHECK(elrOf(0) && elrOf(d) && port(1)->ElrSet, "and set again on it");

    /* Stop and suspend: both ends 0 first; a resume sets it again. */
    d = elrStart(1, 1, 0, 0);
    TbtU4Stop(&g_cm);
    CHECK(!elrOf(0) && !elrOf(d), "stop: both ends cleared");
    d = elrStart(1, 1, 0, 0);
    TbtU4Suspend(&g_cm);
    CHECK(!elrOf(0) && !elrOf(d), "suspend: both ends cleared");
    rc = TbtU4Resume(&g_cm);
    CHECK_EQ(rc, TBT_OK, "resumed");
    poll(2);
    CHECK(elrOf(0) && elrOf(d) && port(1)->ElrSet, "set again after the resume");
    CHECK_EQ(g_cm.Counters.ElrEnabled, 2, "twice");

    /* A contained instance writes nothing. */
    d = elrStart(1, 1, 0, 0);
    TbtU4Contain(&g_cm);
    g_fab.WrCount = 0;
    TbtU4Stop(&g_cm);
    CHECK(elrOf(0) && elrOf(d), "contained stop: left");
    CHECK_EQ(g_fab.WrCount, 0, "nothing written");

    /* A recovery's commit: counted as not set, set again by the tick. */
    d = elrStart(1, 1, 0, 0);
    recoverWith();
    TbtU4RecoverCommit(&g_cm);
    CHECK(!port(1)->ElrSet && port(1)->ElrDev && port(1)->ElrHost, "commit: owned, not set");
    poll(2);
    CHECK(port(1)->ElrSet && elrOf(0) && elrOf(d), "set again");

    /* Owned on a port that is no longer admitted: cleared at the tick. */
    d = elrStart(1, 1, 0, 0);
    g_cm.Config.Gen4LinkRecovery = FALSE;
    g_cm.Ports[0].ElrSet = FALSE;
    poll(2);
    CHECK(!elrOf(0) && !elrOf(d), "not admitted: both ends cleared");
    CHECK(!port(1)->ElrDev && !port(1)->ElrHost, "nothing owned");

    /* Owned on a port left HELD (its router not identified): the host's
     * end alone cleared, the device end's ownership kept. */
    d = elrStart(1, 1, 0, 0);
    g_cm.Ports[0].State = TBT_U4_HELD;
    g_cm.Ports[0].ElrSet = FALSE;
    g_fab.WrCount = 0;
    poll(2);
    CHECK(!elrOf(0) && !port(1)->ElrHost, "HELD: the host end cleared");
    CHECK(elrOf(d) && port(1)->ElrDev, "the device end left, still owned");
    CHECK_EQ(writesTo(d, TBT_CS_ADAPTER, 1, P19), 0, "nothing written to the device");

    /* HELD with its classification unresolved: the host's end still
     * cleared, nothing written to the device. */
    d = elrStart(1, 1, 0, 0);
    g_cm.Ports[0].State = TBT_U4_HELD;
    g_cm.Ports[0].ClassifyPending = TRUE;
    g_cm.Ports[0].ElrSet = FALSE;
    g_fab.WrCount = 0;
    poll(1);
    CHECK(!elrOf(0) && !port(1)->ElrHost, "HELD, unclassified: the host end cleared");
    CHECK(elrOf(d) && port(1)->ElrDev, "the device end left, owned");
    CHECK_EQ(writesTo(d, TBT_CS_ADAPTER, 1, P19), 0, "nothing written to the device");

    /* A stop with a HELD port owning both ends: the host's end alone. */
    d = elrStart(1, 1, 0, 0);
    g_cm.Ports[0].State = TBT_U4_HELD;
    g_cm.Ports[0].ElrSet = FALSE;
    g_fab.WrCount = 0;
    TbtU4Stop(&g_cm);
    CHECK(!elrOf(0), "stop, HELD: the host end cleared");
    CHECK(elrOf(d) && port(1)->ElrDev, "the device end left, owned");
    CHECK_EQ(writesTo(d, TBT_CS_ADAPTER, 1, P19), 0, "nothing written to the device");
    (void)c;
}

/* Part 7: a predecessor's bit at the adoption, the depth-1 link set again. */
static void test_elr_adopted(void)
{
    TBT_U4_CONFIG c;
    int d;
    int ch;
    int elr;

    for (elr = 1; elr >= 0; elr--) {
        d = domain(1, FAB_DEV_USB4V2);
        ch = fabDevice(&g_fab, FAB_DEV_USB4V2);
        fabPreos(&g_fab, 1, 0);
        fabPlugChild(&g_fab, d, ch, 1);
        g_fab.R[0].Adp[1].Cs[P19] |= ELR;
        g_fab.R[d].Adp[1].Cs[P19] |= ELR;
        g_fab.R[d].Adp[3].Cs[P19] |= ELR;
        g_fab.R[ch].Adp[1].Cs[P19] |= ELR;
        g_fab.RxQCount = 0;
        cfgDefault(&c);
        c.Gen4LinkRecovery = (BOOLEAN)(elr != 0);
        (void)start(&c);
        poll(2);
        CHECK_EQ(port(1)->State, TBT_U4_UP, "adopted: UP");
        CHECK_EQ(g_cm.Counters.ElrCleared, 4, "the four cleared");
        CHECK_EQ(fabAdp(&g_fab, d, 3, P19) & ELR, 0, "depth 1 downstream: stays 0");
        CHECK_EQ(fabAdp(&g_fab, ch, 1, P19) & ELR, 0, "depth 2 upstream: stays 0");
        if (elr) {
            CHECK(elrOf(0) && elrOf(d), "value 1: the depth-1 link set again");
        } else {
            CHECK(!elrOf(0) && !elrOf(d), "value 0: left cleared");
        }
    }

    /* StartAsymmetricFlow 1 at the adoption: the clear withheld, held. */
    d = domain(1, FAB_DEV_USB4V2);
    fabPreos(&g_fab, 1, 0);
    g_fab.R[0].Adp[1].Cs[P19] |= ELR | SAF;
    g_fab.RxQCount = 0;
    cfgDefault(&c);
    (void)start(&c);
    CHECK_EQ(port(1)->State, TBT_U4_HELD, "a withheld clear holds the port");
    CHECK_EQ(writeWith(0, 0, TBT_CS_ADAPTER, 1, P19, ELR, 0), -1, "nothing written");
}

/* ------------------------------------------------------------------ */
/* Extended Encapsulation (section 6.2.2 item 7)                       */
/* ------------------------------------------------------------------ */

static void test_ee(void)
{
    TBT_U4_CONFIG c;
    long eeD;
    long eeH;
    long peD;
    long peH;
    long offD;
    long offH;
    long clrD;
    long clrH;
    int d;

    d = domain(1, FAB_DEV_USB4V2);
    cfgDefault(&c);
    (void)start(&c);
    CHECK(port(1)->Pcie.Up, "v2-v2: PCIe up");
    CHECK(fabAdp(&g_fab, d, DEV_PCIE, PCIE1) & 1UL, "device PCIe Up: EE 1");
    CHECK(fabAdp(&g_fab, 0, HOST_PCIE, PCIE1) & 1UL, "host PCIe Down: EE 1");
    eeD = writeWith(0, d, TBT_CS_ADAPTER, DEV_PCIE, PCIE1, 1UL, 1);
    eeH = writeWith(0, 0, TBT_CS_ADAPTER, HOST_PCIE, PCIE1, 1UL, 1);
    peD = writeWith(0, d, TBT_CS_ADAPTER, DEV_PCIE, PCIE0, 0x80000000UL, 1);
    peH = writeWith(0, 0, TBT_CS_ADAPTER, HOST_PCIE, PCIE0, 0x80000000UL, 1);
    CHECK(eeD >= 0 && eeH >= 0 && peD > eeD && peD > eeH && peH > peD,
          "EE on both before either Path Enable");
    CHECK_EQ(g_cm.Counters.EeSet, 1, "counted");

    /* Torn down: after the Path Enables, before the paths. */
    CHECK_EQ(TbtU4Eject(&g_cm, 1), TBT_OK, "eject");
    CHECK_EQ(fabAdp(&g_fab, d, DEV_PCIE, PCIE1) & 1UL, 0, "device EE 0");
    CHECK_EQ(fabAdp(&g_fab, 0, HOST_PCIE, PCIE1) & 1UL, 0, "host EE 0");
    offH = writeWith(peH, 0, TBT_CS_ADAPTER, HOST_PCIE, PCIE0, 0x80000000UL, 0);
    offD = writeWith(peD, d, TBT_CS_ADAPTER, DEV_PCIE, PCIE0, 0x80000000UL, 0);
    clrH = writeWith(eeH + 1, 0, TBT_CS_ADAPTER, HOST_PCIE, PCIE1, 1UL, 0);
    clrD = writeWith(eeD + 1, d, TBT_CS_ADAPTER, DEV_PCIE, PCIE1, 1UL, 0);
    CHECK(offH >= 0 && offD >= 0 && clrH > offH && clrH > offD && clrD > offD,
          "EE cleared after both Path Enables");
    CHECK(fabFindWrite(&g_fab, clrD, 0, TBT_CS_PATH, HOST_PCIE, 16) > clrD,
          "and before the paths are invalidated");
    CHECK_EQ(g_cm.Counters.EeCleared, 2, "two cleared");

    /* Unplug: the host's EE cleared at the host side's teardown. */
    d = domain(1, FAB_DEV_USB4V2);
    cfgDefault(&c);
    (void)start(&c);
    fabUnplug(&g_fab, 1);
    poll(2);
    CHECK_EQ(fabAdp(&g_fab, 0, HOST_PCIE, PCIE1) & 1UL, 0, "unplug: host EE 0");

    /* v2 host, v1 device: never written. */
    d = domain(1, FAB_DEV_USB4);
    cfgDefault(&c);
    (void)start(&c);
    CHECK(port(1)->Pcie.Up, "v2-v1: PCIe up");
    CHECK_EQ(writesTo(d, TBT_CS_ADAPTER, DEV_PCIE, PCIE1) +
             writesTo(0, TBT_CS_ADAPTER, HOST_PCIE, PCIE1), 0, "v2-v1: EE never written");
    (void)TbtU4Eject(&g_cm, 1);
    CHECK_EQ(writesTo(0, TBT_CS_ADAPTER, HOST_PCIE, PCIE1), 0, "nor at its teardown");

    /* XhciCmUsb4V2 0: never written. */
    d = domain(1, FAB_DEV_USB4V2);
    cfgDefault(&c);
    c.Usb4V2 = FALSE;
    (void)start(&c);
    CHECK(port(1)->Pcie.Up, "v1 run: PCIe up");
    CHECK_EQ(writesTo(d, TBT_CS_ADAPTER, DEV_PCIE, PCIE1) +
             writesTo(0, TBT_CS_ADAPTER, HOST_PCIE, PCIE1), 0, "v1 run: EE never written");

    /* EE that will not set: no Path Enable, FAILED, unwound. */
    d = domain(1, FAB_DEV_USB4V2);
    g_fab.R[d].Adp[DEV_PCIE].EeStuck = 1;
    cfgDefault(&c);
    (void)start(&c);
    CHECK(!port(1)->Pcie.Up, "EE stuck: no PCIe tunnel");
    CHECK_EQ(port(1)->State, TBT_U4_FAILED, "FAILED");
    CHECK_EQ(writeWith(0, d, TBT_CS_ADAPTER, DEV_PCIE, PCIE0, 0x80000000UL, 1), -1,
             "no Path Enable written");
    CHECK_EQ(fabPath0(&g_fab, 0, HOST_PCIE, 8) & 0x80000000UL, 0, "its entries invalidated");
    CHECK_EQ(fabAdp(&g_fab, 0, HOST_PCIE, PCIE1) & 1UL, 0, "the host's EE cleared again");

    /* The host's EE that will not set, the device's set first: both
     * Path Enables left clear, the device's EE cleared again and every
     * entry invalidated. */
    d = domain(1, FAB_DEV_USB4V2);
    g_fab.R[0].Adp[HOST_PCIE].EeStuck = 1;
    cfgDefault(&c);
    c.Usb3Tunnel = FALSE;       /* no USB3 tunnel reusing the freed HopIDs */
    (void)start(&c);
    eeD = writeWith(0, d, TBT_CS_ADAPTER, DEV_PCIE, PCIE1, 1UL, 1);
    CHECK(eeD >= 0, "host EE stuck: the device's EE set first");
    CHECK(writeWith(eeD + 1, d, TBT_CS_ADAPTER, DEV_PCIE, PCIE1, 1UL, 0) > eeD,
          "and written 0 again");
    CHECK_EQ(fabAdp(&g_fab, d, DEV_PCIE, PCIE1) & 1UL, 0, "the device's EE 0");
    CHECK_EQ(writeWith(0, d, TBT_CS_ADAPTER, DEV_PCIE, PCIE0, 0x80000000UL, 1) +
             writeWith(0, 0, TBT_CS_ADAPTER, HOST_PCIE, PCIE0, 0x80000000UL, 1), -2,
             "neither Path Enable written");
    CHECK_EQ(port(1)->State, TBT_U4_FAILED, "FAILED");
    CHECK_EQ((fabPath0(&g_fab, 0, HOST_PCIE, 8) | fabPath0(&g_fab, 0, 1, 9) |
              fabPath0(&g_fab, d, 1, 8) | fabPath0(&g_fab, d, DEV_PCIE, 8)) & 0x80000000UL,
             0, "all four entries invalidated");

    /* The host's EE left set by a teardown whose clear failed: the next
     * device, a version 1 one, finds it and clears it before its Path
     * Enable. */
    d = domain(1, FAB_DEV_USB4V2);
    cfgDefault(&c);
    (void)start(&c);
    g_fab.R[0].Adp[HOST_PCIE].EeStuck = 1;     /* holds the bit as set */
    fabUnplug(&g_fab, 1);
    poll(2);
    CHECK(fabAdp(&g_fab, 0, HOST_PCIE, PCIE1) & 1UL, "the host's EE left set");
    g_fab.R[0].Adp[HOST_PCIE].EeStuck = 0;
    d = fabDevice(&g_fab, FAB_DEV_USB4);
    g_fab.WrCount = 0;          /* this connection's writes alone */
    fabPlug(&g_fab, 1, d);
    poll(3);
    CHECK(port(1)->Pcie.Up, "a v1 device after it: PCIe up");
    CHECK_EQ(fabAdp(&g_fab, 0, HOST_PCIE, PCIE1) & 1UL, 0, "the host's EE cleared first");
    {
        long clr = writeWith(0, 0, TBT_CS_ADAPTER, HOST_PCIE, PCIE1, 1UL, 0);
        long peH = writeWith(0, 0, TBT_CS_ADAPTER, HOST_PCIE, PCIE0, 0x80000000UL, 1);
        long peD = writeWith(0, d, TBT_CS_ADAPTER, DEV_PCIE, PCIE0, 0x80000000UL, 1);
        CHECK(clr >= 0 && peH > clr && peD > clr, "before either Path Enable");
    }
    /* And with it stuck: no tunnel. */
    d = domain(1, FAB_DEV_USB4);
    g_fab.R[0].Adp[HOST_PCIE].Cs[PCIE1] |= 1UL;
    g_fab.R[0].Adp[HOST_PCIE].EeStuck = 1;
    cfgDefault(&c);
    (void)start(&c);
    CHECK(!port(1)->Pcie.Up, "EE that will not clear on a v2-v1 link: no PCIe tunnel");
    CHECK_EQ(writeWith(0, 0, TBT_CS_ADAPTER, HOST_PCIE, PCIE0, 0x80000000UL, 1), -1,
             "no Path Enable");

    /* A device Path Enable that will not clear: its EE left until the
     * quarantine's lift proves it clear; the host's cleared at once. */
    d = domain(1, FAB_DEV_USB4V2);
    cfgDefault(&c);
    (void)start(&c);
    g_fab.R[d].Adp[DEV_PCIE].PeStuck = 1;
    g_fab.WrCount = 0;
    (void)TbtU4Eject(&g_cm, 1);
    CHECK(fabAdp(&g_fab, d, DEV_PCIE, PCIE0) & 0x80000000UL, "PE stuck");
    CHECK(fabAdp(&g_fab, d, DEV_PCIE, PCIE1) & 1UL, "its EE untouched while PE stands");
    CHECK_EQ(writesTo(d, TBT_CS_ADAPTER, DEV_PCIE, PCIE1), 0, "not written");
    CHECK_EQ(fabAdp(&g_fab, 0, HOST_PCIE, PCIE1) & 1UL, 0, "the host's EE cleared");
    g_fab.R[d].Adp[DEV_PCIE].PeStuck = 0;
    poll(2);
    CHECK_EQ(fabAdp(&g_fab, d, DEV_PCIE, PCIE0) & 0x80000000UL, 0, "PE cleared by the retry");
    CHECK_EQ(fabAdp(&g_fab, d, DEV_PCIE, PCIE1) & 1UL, 0, "then its EE");

    /* A predecessor's EE on a preserved tunnel: kept while it stands,
     * cleared at its teardown. */
    d = domain(1, FAB_DEV_USB4V2);
    fabPreos(&g_fab, 1, 0);
    g_fab.R[0].Rcs[4] |= 0x2000UL;
    g_fab.R[0].Adp[HOST_PCIE].Cs[PCIE1] |= 1UL;
    g_fab.R[d].Adp[DEV_PCIE].Cs[PCIE1] |= 1UL;
    g_fab.RxQCount = 0;
    cfgDefault(&c);
    (void)start(&c);
    CHECK(port(1)->Pcie.Preserved, "preserved");
    CHECK(fabAdp(&g_fab, d, DEV_PCIE, PCIE1) & 1UL, "its EE kept");
    (void)TbtU4Eject(&g_cm, 1);
    CHECK_EQ(fabAdp(&g_fab, d, DEV_PCIE, PCIE1) & 1UL, 0, "cleared at its teardown");
    CHECK_EQ(fabAdp(&g_fab, 0, HOST_PCIE, PCIE1) & 1UL, 0, "both ends");
}

/* ------------------------------------------------------------------ */
/* Gen 4 links (section 6.2.3)                                         */
/* ------------------------------------------------------------------ */

static unsigned long laneWrites(int d)
{
    return writesTo(0, TBT_CS_ADAPTER, 1, FAB_CAP_LANE + 1) +
           writesTo(0, TBT_CS_ADAPTER, 2, FAB_CAP_LANE + 1) +
           writesTo(d, TBT_CS_ADAPTER, 1, FAB_CAP_LANE + 1) +
           writesTo(d, TBT_CS_ADAPTER, 2, FAB_CAP_LANE + 1);
}

static int gen4(unsigned long hostW, unsigned long devW, int bonding)
{
    TBT_U4_CONFIG c;
    int d;

    fabInit(&g_fab);
    fabHostV2(&g_fab);
    d = fabDevice(&g_fab, FAB_DEV_USB4V2);
    g_fab.R[d].Gen4HostW = hostW;
    g_fab.R[d].Gen4DevW = devW;
    fabPlug(&g_fab, 1, d);
    cfgDefault(&c);
    c.LaneBonding = (BOOLEAN)(bonding != 0);
    (void)start(&c);
    return d;
}

static void test_gen4(void)
{
    const TBT_U4_PORT *p;
    int d;

    d = gen4(0, 0, 1);
    p = port(1);
    CHECK_EQ(p->State, TBT_U4_UP, "Gen 4 x2: UP");
    CHECK(p->Bonded && p->Gen4 && !p->Asym, "used as bonded, symmetric");
    CHECK_EQ(laneWrites(d), 0, "no lane write: no TLW, LB or LD");
    CHECK(p->Pcie.Up && p->Usb3.Up, "both tunnels");
    CHECK(fabAdp(&g_fab, 0, 1, FAB_CAP_PORT + 19) & 8UL, "USB4 Port is Configured");
    CHECK_EQ(g_cm.Counters.BondAttempts, 1, "one bonding pass");
    CHECK_EQ(g_cm.Counters.Gen4Links, 1, "counted");

    d = gen4(0, 0, 0);
    p = port(1);
    CHECK_EQ(p->State, TBT_U4_UP, "Gen 4, XhciCmLaneBonding 0: used as bonded");
    CHECK(p->Bonded && !p->SingleLane, "no single-lane route");
    CHECK_EQ(laneWrites(d), 0, "and no lane write");

    d = gen4(0x04, 0x08, 1);
    p = port(1);
    CHECK_EQ(p->State, TBT_U4_UP, "asymmetric 3 Tx / 3 Rx: UP");
    CHECK(p->Bonded && p->Gen4 && p->Asym, "used as found");
    CHECK_EQ(laneWrites(d), 0, "no lane write");
    CHECK_EQ(writeWith(0, 0, TBT_CS_ADAPTER, 1, FAB_CAP_PORT + 19, 0x01000000UL, 1), -1,
             "StartAsymmetricFlow never written");
    CHECK_EQ(((fabAdp(&g_fab, 0, 1, FAB_CAP_LANE + 1)) >> 20) & 0x3FUL, 0x04,
             "never transitioned");
    CHECK(p->Pcie.Up && p->Usb3.Up, "both tunnels over it");
    CHECK_EQ(g_cm.Counters.Gen4Asym, 1, "counted");
    d = gen4(0x08, 0x04, 1);
    CHECK_EQ(port(1)->State, TBT_U4_UP, "asymmetric the other way: UP");

    d = gen4(0x01, 0x01, 1);
    p = port(1);
    CHECK_EQ(p->State, TBT_U4_FAILED, "Gen 4 x1: FAILED");
    CHECK_EQ(p->Why, TBT_U4_WHY_LINK, "the link");
    CHECK_EQ(laneWrites(d), 0, "no lane write");
    CHECK(!fabPcieUp(&g_fab, 1), "no tunnel");
    d = gen4(0x04, 0x04, 1);
    CHECK_EQ(port(1)->State, TBT_U4_FAILED, "unpaired asymmetric: FAILED");
    d = gen4(0x10, 0x10, 1);
    CHECK_EQ(port(1)->State, TBT_U4_FAILED, "a reserved width: FAILED");
    CHECK_EQ(g_cm.Counters.Gen4Unusable, 1, "counted");
    (void)d;

    /* A preserved asymmetric link: adopted as bonded, never written. */
    fabInit(&g_fab);
    fabHostV2(&g_fab);
    d = fabDevice(&g_fab, FAB_DEV_USB4V2);
    g_fab.R[d].Gen4HostW = 0x08;
    g_fab.R[d].Gen4DevW = 0x04;
    fabPlug(&g_fab, 1, d);
    fabPreos(&g_fab, 1, 0);
    g_fab.R[0].Adp[1].Cs[FAB_CAP_LANE + 1] =
        (g_fab.R[0].Adp[1].Cs[FAB_CAP_LANE + 1] & ~(0x3FUL << 20)) | (0x08UL << 20);
    g_fab.R[d].Adp[1].Cs[FAB_CAP_LANE + 1] =
        (g_fab.R[d].Adp[1].Cs[FAB_CAP_LANE + 1] & ~(0x3FUL << 20)) | (0x04UL << 20);
    g_fab.RxQCount = 0;
    {
        TBT_U4_CONFIG c;
        cfgDefault(&c);
        (void)start(&c);
    }
    p = port(1);
    CHECK_EQ(p->State, TBT_U4_UP, "preserved asymmetric: UP");
    CHECK(p->Pcie.Preserved && p->Bonded && p->Asym, "preserved, bonded, asymmetric");
    CHECK_EQ(laneWrites(d), 0, "no lane write");
}

/* Sections 7.6 and 6.2: a v2 Gen 4 asymmetric link across a sleep -
 * verified as it stands, and rebuilt where the domain lost its
 * configuration (the Set, CMUV and EE made again). */
static void test_resume_v2(void)
{
    LONG rc;
    int d;

    d = gen4(0x04, 0x08, 1);
    CHECK_EQ(port(1)->State, TBT_U4_UP, "v2 asymmetric: UP");
    TbtU4Suspend(&g_cm);
    rc = TbtU4Resume(&g_cm);
    CHECK_EQ(rc, TBT_OK, "resume: verified on the asymmetric link");
    CHECK_EQ(port(1)->State, TBT_U4_UP, "still UP");

    d = gen4(0x04, 0x08, 1);
    TbtU4Suspend(&g_cm);
    fabPowerCycle(&g_fab);
    CHECK_EQ(g_fab.R[d].CapsData & (1UL << 5), 0, "power loss: the device's capability disabled");
    CHECK_EQ(g_fab.R[0].CapsData & (1UL << 5), 0, "and the host's");
    g_fab.WrCount = 0;
    rc = TbtU4Resume(&g_cm);
    CHECK_EQ(rc, TBT_OK, "resume after a power loss: rebuilt");
    poll(2);
    CHECK_EQ(port(1)->State, TBT_U4_UP, "UP again");
    CHECK(port(1)->Gen4 && port(1)->Asym, "asymmetric as found again");
    CHECK_EQ((fabRtr(&g_fab, d, 4) >> 8) & 0xFF, 0x20, "CMUV 20h written again");
    CHECK_EQ(g_fab.R[d].SetCapsOps, 1, "the Sequence bit set again");
    CHECK(fabAdp(&g_fab, d, DEV_PCIE, PCIE1) & 1UL, "EE set again");
    CHECK(port(1)->TmuEnhanced, "the enhanced TMU again");
    CHECK_EQ(g_fab.R[0].SetCapsOps, 1, "the host router's Set again too");
    CHECK(g_cm.HostRouter.CapsEnabled & (1UL << TBT_CAP_NOTIFY_SEQUENCE),
          "the host's capability enabled again");
    CHECK_EQ(pathWritesNotPaired(), 0, "every PATH_CS write two DWs");
}

static int g_trainArmed;

/* The device's enumerating write: from then the host's lane 0 reads
 * Training (a link that will not settle). */
static int trainHook(FAB *f, unsigned long pdf, int router, unsigned long cs,
                     unsigned long adp, unsigned long addr, unsigned long n,
                     const unsigned long *data)
{
    (void)adp;
    (void)n;
    (void)data;
    if (g_trainArmed && pdf == TBT_PDF_WRITE && router > 0 && cs == TBT_CS_ROUTER &&
        addr == 1) {
        g_trainArmed = 0;
        f->R[0].Adp[1].Cs[FAB_CAP_LANE + 1] =
            (f->R[0].Adp[1].Cs[FAB_CAP_LANE + 1] & ~(0xFUL << 26)) | (1UL << 26);
    }
    return FAB_HOOK_GO;
}

/* Section 6.2.3 item 2: a v2-v2 lane 0 training past the wait - FAILED,
 * no lane write, nothing classified. */
static void test_gen4_training(void)
{
    TBT_U4_CONFIG c;
    int d;

    d = domain(1, FAB_DEV_USB4V2);
    g_fab.Hook = trainHook;
    g_trainArmed = 1;
    cfgDefault(&c);
    (void)start(&c);
    g_fab.Hook = NULL;
    CHECK_EQ(g_trainArmed, 0, "lane 0 put in Training after the enumeration");
    CHECK_EQ(port(1)->State, TBT_U4_FAILED, "training past the wait: FAILED");
    CHECK_EQ(port(1)->Why, TBT_U4_WHY_LANE_STATE, "for the lane state");
    CHECK_EQ(laneWrites(d), 0, "no lane write");
    CHECK(!port(1)->Bonded, "not used as bonded");
    CHECK(!fabPcieUp(&g_fab, 1), "no tunnel");
}

/* The USB3 allocation's link bound (section 6.2.3 item 5). */
static void test_link_bound(void)
{
    CHECK_EQ(TbtU4TestLinkBound((0x2UL << 16) | (0x02UL << 20)), 70500,
             "Gen 4 x2: 72 Gb/s less 1.5");
    CHECK_EQ(TbtU4TestLinkBound((0x2UL << 16) | (0x04UL << 20)), 34500,
             "Gen 4 asymmetric 3 Tx: 36 less 1.5");
    CHECK_EQ(TbtU4TestLinkBound((0x2UL << 16) | (0x08UL << 20)), 34500,
             "Gen 4 asymmetric 3 Rx: 36 less 1.5");
    CHECK_EQ(TbtU4TestLinkBound((0x4UL << 16) | (0x02UL << 20)), 34500, "Gen 3 x2");
    CHECK_EQ(TbtU4TestLinkBound((0x8UL << 16) | (0x01UL << 20)), 7500, "Gen 2 x1");
}

#define RUN_VEC(fn) do { fabTraceMark(#fn); fn(); } while (0)

/*
 * Record 34 Part 3 with version 2 routers (record 22 section 6.2 through
 * the link view): a kid's Gen 4 link classified as at depth 1 (bonded
 * with no lane written, symmetric or a paired asymmetric width; FAILED
 * otherwise); Extended Encapsulation set on a chained PCIe tunnel between
 * two version 2 routers and cleared at its teardown, a predecessor's
 * cleared before a tunnel to a version 1 child; a preserved child with CV
 * set and no PTO HELD, never configured again.
 */
#define PAR_LANE    3       /* the parent's downstream lane 0 */
#define PAR_PCIE    7       /* the parent's PCIe Down */

static const TBT_U4_KID *kid(void)
{
    ULONG i;

    for (i = 0; i < TBT_U4_MAX_KIDS; i++) {
        if (g_cm.Kids[i].Used && g_cm.Kids[i].Port == 1) {
            return &g_cm.Kids[i];
        }
    }
    return NULL;
}

static unsigned long kidLaneWrites(int d, int ch)
{
    return writesTo(d, TBT_CS_ADAPTER, PAR_LANE, FAB_CAP_LANE + 1) +
           writesTo(d, TBT_CS_ADAPTER, PAR_LANE + 1, FAB_CAP_LANE + 1) +
           writesTo(ch, TBT_CS_ADAPTER, 1, FAB_CAP_LANE + 1) +
           writesTo(ch, TBT_CS_ADAPTER, 2, FAB_CAP_LANE + 1);
}

static int g_kidParent;

static int kidV2(int childKind, unsigned long hostW, unsigned long devW, int eeLeft)
{
    TBT_U4_CONFIG c;
    int ch;

    fabInit(&g_fab);
    fabHostV2(&g_fab);
    g_kidParent = fabDevice(&g_fab, FAB_DEV_USB4V2);
    ch = fabDevice(&g_fab, childKind);
    g_fab.R[ch].Gen4HostW = hostW;
    g_fab.R[ch].Gen4DevW = devW;
    fabPlug(&g_fab, 1, g_kidParent);
    cfgDefault(&c);
    (void)start(&c);
    poll(2);
    if (eeLeft) {
        g_fab.R[g_kidParent].Adp[PAR_PCIE].Cs[PCIE1] |= 1UL;
    }
    g_fab.WrCount = 0;
    fabHotPlugChild(&g_fab, g_kidParent, ch);
    poll(3);
    return ch;
}

static void test_kid_v2(void)
{
    const TBT_U4_KID *k;
    long cv;
    long eu;
    int d;
    int ch;
    int i;

    ch = kidV2(FAB_DEV_USB4V2, 0, 0, 0);
    d = g_kidParent;
    k = kid();
    CHECK(k != NULL && k->State == TBT_U4_UP, "a v2 kid on a Gen 4 x2 link: UP");
    /* Record 22 section 6.2.2 item 6 through the link view: HiFi Enhanced
     * Uni-Directional on both lane 0 adapters of the link, before CV. */
    CHECK(k != NULL && k->TmuConfigured, "the link's TMU configured");
    for (i = 0; i < 2; i++) {
        int r = i == 0 ? ch : d;
        unsigned long a = i == 0 ? 1UL : PAR_LANE;
        CHECK(fabAdp(&g_fab, r, a, TMU8) & 0x8000UL, "EUDM 1");
        CHECK_EQ(fabAdp(&g_fab, r, a, TMU9) >> 16, 16, "AdapterTimeSyncInterval 16");
        CHECK_EQ(fabAdp(&g_fab, r, a, TMU6) & 2UL, 0, "time sync enabled");
    }
    cv = writeWith(0, ch, TBT_CS_ROUTER, 0, TBT_ROUTER_CS_5, TBT_RTR5_CV, 1);
    eu = writeWith(0, d, TBT_CS_ADAPTER, PAR_LANE, TMU8, 0x8000UL, 1);
    CHECK(cv >= 0 && eu >= 0 && eu < cv, "the mode set before CV");
    CHECK(k != NULL && k->Bonded && k->Gen4 && !k->Asym, "used as bonded, symmetric");
    CHECK_EQ(kidLaneWrites(d, ch), 0, "no lane write: no TLW, LB or LD");
    CHECK_EQ(g_cm.Counters.Gen4Links, 2, "the parent's link and the kid's counted");
    CHECK(fabChainPcieUp(&g_fab, d), "the chained tunnel up");
    CHECK(fabAdp(&g_fab, d, PAR_PCIE, PCIE1) & 1UL, "the parent's PCIe Down: EE 1");
    CHECK(fabAdp(&g_fab, ch, DEV_PCIE, PCIE1) & 1UL, "the child's PCIe Up: EE 1");
    CHECK_EQ(g_cm.Counters.EeSet, 2, "set on both tunnels");
    fabHotUnplugChild(&g_fab, d);
    poll(3);
    CHECK(kid() == NULL, "the child unplugged: the kid freed");
    CHECK_EQ(fabAdp(&g_fab, d, PAR_PCIE, PCIE1) & 1UL, 0,
             "the parent's PCIe Down: EE cleared at the teardown");
    CHECK_EQ(fabAdp(&g_fab, d, PAR_LANE, TMU8) & 0x8000UL, 0,
             "the parent's lane 0: EUDM 0 at the teardown");
    CHECK_EQ(fabAdp(&g_fab, d, PAR_LANE, TMU9) >> 16, 0, "AdapterTimeSyncInterval 0");
    CHECK(fabAdp(&g_fab, d, PAR_LANE, TMU6) & 2UL, "Disable Time Sync set");
    /* plugged again: the link's TMU read Off, configured again */
    fabHotPlugChild(&g_fab, d, ch);
    poll(3);
    k = kid();
    CHECK(k != NULL && k->State == TBT_U4_UP && k->TmuConfigured, "re-plugged: TMU again");
    CHECK_EQ(port(1)->State, TBT_U4_UP, "the parent kept");

    (void)kidV2(FAB_DEV_USB4V2, 0x04, 0x08, 0);
    k = kid();
    CHECK(k != NULL && k->State == TBT_U4_UP && k->Gen4 && k->Asym,
          "asymmetric 3 Tx / 3 Rx: UP, used as found");
    CHECK_EQ(g_cm.Counters.Gen4Asym, 1, "counted");

    ch = kidV2(FAB_DEV_USB4V2, 0x04, 0x04, 0);
    d = g_kidParent;
    k = kid();
    CHECK(k != NULL && k->State == TBT_U4_FAILED, "unpaired asymmetric: FAILED");
    CHECK(k != NULL && k->Why == TBT_U4_WHY_LINK, "the link");
    CHECK_EQ(kidLaneWrites(d, ch), 0, "no lane write");
    CHECK(!fabChainPcieUp(&g_fab, d), "no chained tunnel");
    CHECK_EQ(g_cm.Counters.Gen4Unusable, 1, "counted");

    /* A version 1 child behind a version 2 parent whose PCIe Down a
     * predecessor left with EE set: cleared before the Path Enables. */
    ch = kidV2(FAB_DEV_USB4, 0, 0, 1);
    d = g_kidParent;
    k = kid();
    CHECK(k != NULL && k->State == TBT_U4_UP && !k->Gen4, "a v1 kid: UP, not Gen 4");
    CHECK(k != NULL && !k->TmuConfigured, "no TMU on a v2-v1 link");
    CHECK_EQ(writesTo(d, TBT_CS_ADAPTER, PAR_LANE, TMU8), 0, "the parent's lane 0 TMU unwritten");
    CHECK_EQ(fabAdp(&g_fab, d, PAR_PCIE, PCIE1) & 1UL, 0, "the predecessor's EE cleared");
    CHECK(fabChainPcieUp(&g_fab, d), "the chained tunnel up");
    (void)ch;
}

/* The child's enumerating write: from then the parent's downstream lane
 * 0 reads Training (a link that will not settle). */
static int g_kidTrainChild;

static int kidTrainHook(FAB *f, unsigned long pdf, int router, unsigned long cs,
                        unsigned long adp, unsigned long addr, unsigned long n,
                        const unsigned long *data)
{
    (void)adp;
    (void)n;
    (void)data;
    if (g_trainArmed && pdf == TBT_PDF_WRITE && router == g_kidTrainChild &&
        cs == TBT_CS_ROUTER && addr == 1) {
        g_trainArmed = 0;
        f->R[g_kidParent].Adp[PAR_LANE].Cs[FAB_CAP_LANE + 1] =
            (f->R[g_kidParent].Adp[PAR_LANE].Cs[FAB_CAP_LANE + 1] & ~(0xFUL << 26)) |
            (1UL << 26);
    }
    return FAB_HOOK_GO;
}

/* A v2-v2 child link training past the wait: FAILED, no lane write. */
static void test_kid_training(void)
{
    TBT_U4_CONFIG c;
    const TBT_U4_KID *k;
    int ch;

    fabInit(&g_fab);
    fabHostV2(&g_fab);
    g_kidParent = fabDevice(&g_fab, FAB_DEV_USB4V2);
    ch = fabDevice(&g_fab, FAB_DEV_USB4V2);
    fabPlug(&g_fab, 1, g_kidParent);
    cfgDefault(&c);
    (void)start(&c);
    poll(2);
    g_fab.WrCount = 0;
    g_kidTrainChild = ch;
    g_trainArmed = 1;
    g_fab.Hook = kidTrainHook;
    fabHotPlugChild(&g_fab, g_kidParent, ch);
    poll(3);
    g_fab.Hook = NULL;
    k = kid();
    CHECK_EQ(g_trainArmed, 0, "the parent's lane 0 put in Training after the enumeration");
    CHECK(k != NULL && k->State == TBT_U4_FAILED, "training past the wait: FAILED");
    CHECK(k != NULL && k->Why == TBT_U4_WHY_LANE_STATE, "for the lane state");
    CHECK(k != NULL && !k->Bonded && !k->Gen4, "not classified");
    CHECK_EQ(kidLaneWrites(g_kidParent, ch), 0, "no lane write");
    CHECK(!fabChainPcieUp(&g_fab, g_kidParent), "no chained tunnel");
}

/* The parent's downstream lane 0 TMU_ADP_CS_9 writes swallowed while
 * armed (a write that does not complete). */
static int g_tmuSwallow;
static unsigned long g_tmuSwallowed;

static int tmuSwallowHook(FAB *f, unsigned long pdf, int router, unsigned long cs,
                          unsigned long adp, unsigned long addr, unsigned long n,
                          const unsigned long *data)
{
    (void)f;
    (void)n;
    (void)data;
    if (g_tmuSwallow > 0 && pdf == TBT_PDF_WRITE && router == g_kidParent &&
        cs == TBT_CS_ADAPTER && adp == PAR_LANE && addr == TMU9) {
        g_tmuSwallow--;
        g_tmuSwallowed++;
        return FAB_HOOK_SWALLOW;
    }
    return FAB_HOOK_GO;
}

/* The parent's side of a kid's TMU not reset at the unplug: the slot kept
 * TEARDOWN, nothing connected on the link, the reset retried; then a
 * version 1 child behind the same port finds the parent's lane 0 at its
 * defaults. */
static void test_kid_tmu_debt(void)
{
    const TBT_U4_KID *k;
    int d;
    int ch;
    int v1;

    ch = kidV2(FAB_DEV_USB4V2, 0, 0, 0);
    d = g_kidParent;
    k = kid();
    CHECK(k != NULL && k->State == TBT_U4_UP && k->TmuConfigured, "a v2 kid with its TMU");
    g_tmuSwallow = 1000;  /* every attempt in the unplug's tick */
    g_tmuSwallowed = 0;
    g_fab.Hook = tmuSwallowHook;
    fabHotUnplugChild(&g_fab, d);
    poll(1);
    k = kid();
    g_tmuSwallow = 0;
    CHECK(g_tmuSwallowed >= 2, "the reset's write did not complete, retried");
    CHECK(k != NULL && k->State == TBT_U4_TEARDOWN && k->TmuConfigured,
          "the slot kept TEARDOWN, the debt kept");
    v1 = fabDevice(&g_fab, FAB_DEV_USB4);
    fabHotPlugChild(&g_fab, d, v1);
    poll(3);
    g_fab.Hook = NULL;
    k = kid();
    CHECK(k != NULL && k->State == TBT_U4_UP && !k->TmuConfigured,
          "the reset retried, then the version 1 child connected");
    CHECK_EQ(fabAdp(&g_fab, d, PAR_LANE, TMU8) & 0x8000UL, 0, "the parent's lane 0: EUDM 0");
    CHECK_EQ(fabAdp(&g_fab, d, PAR_LANE, TMU9) >> 16, 0, "AdapterTimeSyncInterval 0");
    CHECK(fabAdp(&g_fab, d, PAR_LANE, TMU6) & 2UL, "Disable Time Sync set");
    (void)ch;
}

/* A pre-OS chain over an asymmetric Gen 4 link: adopted as bonded, used
 * as found, no lane written. */
static void test_kid_adopt_asym(void)
{
    TBT_U4_CONFIG c;
    const TBT_U4_KID *k;
    int d;
    int ch;
    int i;

    fabInit(&g_fab);
    fabHostV2(&g_fab);
    d = fabDevice(&g_fab, FAB_DEV_USB4V2);
    ch = fabDevice(&g_fab, FAB_DEV_USB4V2);
    g_fab.R[ch].Gen4HostW = 0x08;
    g_fab.R[ch].Gen4DevW = 0x04;
    fabPlug(&g_fab, 1, d);
    fabPreos(&g_fab, 1, 0);
    fabPreosChild(&g_fab, d, ch, 0);
    /* the pre-OS chain left the link's widths as initialised: 3 Rx / 3 Tx */
    for (i = 0; i < 2; i++) {
        g_fab.R[d].Adp[PAR_LANE + i].Cs[FAB_CAP_LANE + 1] =
            (g_fab.R[d].Adp[PAR_LANE + i].Cs[FAB_CAP_LANE + 1] & ~(0x3FUL << 20)) |
            (0x08UL << 20);
        g_fab.R[ch].Adp[1 + i].Cs[FAB_CAP_LANE + 1] =
            (g_fab.R[ch].Adp[1 + i].Cs[FAB_CAP_LANE + 1] & ~(0x3FUL << 20)) | (0x04UL << 20);
    }
    g_fab.WrCount = 0;
    cfgDefault(&c);
    (void)start(&c);
    poll(3);
    k = kid();
    CHECK_EQ(g_cm.Counters.KidsAdopted, 1, "the chain adopted");
    CHECK(k != NULL && k->State == TBT_U4_UP && k->Bonded && k->Gen4 && k->Asym,
          "asymmetric: bonded, used as found");
    CHECK_EQ(kidLaneWrites(d, ch), 0, "no lane write");
    CHECK(fabChainPcieUp(&g_fab, d), "the chained tunnel kept");
}

/* A child enumerated before this driver with CV set and no PTO, nothing
 * on its link: connected as found, HELD, ROUTER_CS_5 never written. */
static void test_kid_cv_found(void)
{
    TBT_U4_CONFIG c;
    const TBT_U4_KID *k;
    int d;
    int ch;

    fabInit(&g_fab);
    fabHostV2(&g_fab);
    d = fabDevice(&g_fab, FAB_DEV_USB4V2);
    ch = fabDevice(&g_fab, FAB_DEV_USB4V2);
    fabPlug(&g_fab, 1, d);
    fabPreos(&g_fab, 1, 0);
    fabPlugChild(&g_fab, d, ch, 1);
    g_fab.R[ch].Rcs[5] = TBT_RTR5_CV;
    g_fab.R[ch].Rcs[6] |= TBT_RTR6_CR;
    g_fab.WrCount = 0;
    cfgDefault(&c);
    (void)start(&c);
    poll(3);
    k = kid();
    CHECK_EQ(port(1)->State, TBT_U4_UP, "the parent preserved");
    CHECK(k != NULL && k->State == TBT_U4_HELD, "CV found without PTO: HELD");
    CHECK(k != NULL && k->Why == TBT_U4_WHY_CV_FOUND, "its reason");
    CHECK_EQ(writesTo(ch, TBT_CS_ROUTER, 0, TBT_ROUTER_CS_5), 0, "ROUTER_CS_5 never written");
    CHECK(!fabChainPcieUp(&g_fab, d), "no chained tunnel");
}

/* Section 6.2.3 item 4 part 1: never at depth 2 (record 34's kids). */
static void test_elr_kid(void)
{
    int d;

    d = kidV2(FAB_DEV_USB4V2, 0, 0, 0);
    CHECK(kid() != NULL && kid()->State == TBT_U4_UP, "a v2 kid UP");
    CHECK_EQ(writeWith(0, g_kidParent, TBT_CS_ADAPTER, 3, P19, ELR, 1), -1,
             "never on the parent's downstream port");
    CHECK_EQ(writeWith(0, d, TBT_CS_ADAPTER, 1, P19, ELR, 1), -1, "nor on the kid's");
    CHECK(elrOf(0) && elrOf(g_kidParent), "the depth-1 link still set");
}

int main(void)
{
    RUN_VEC(test_cmuv);
    RUN_VEC(test_cmuv_preserved);
    RUN_VEC(test_set_capabilities);
    RUN_VEC(test_set_and_recovery);
    RUN_VEC(test_clean_resets_caps);
    RUN_VEC(test_tmu_as_left);
    RUN_VEC(test_two_dwords);
    RUN_VEC(test_tmu_enhanced);
    RUN_VEC(test_cv_found);
    RUN_VEC(test_elr_and_recovery);
    RUN_VEC(test_elr_set);
    RUN_VEC(test_elr_check);
    RUN_VEC(test_elr_unread);
    RUN_VEC(test_elr_second);
    RUN_VEC(test_elr_moved);
    RUN_VEC(test_elr_clears);
    RUN_VEC(test_elr_adopted);
    RUN_VEC(test_elr_kid);
    RUN_VEC(test_ee);
    RUN_VEC(test_gen4);
    RUN_VEC(test_gen4_training);
    RUN_VEC(test_resume_v2);
    RUN_VEC(test_link_bound);
    RUN_VEC(test_kid_v2);
    RUN_VEC(test_kid_adopt_asym);
    RUN_VEC(test_kid_training);
    RUN_VEC(test_kid_tmu_debt);
    RUN_VEC(test_kid_cv_found);
    fabTraceMark("end");
    printf("test_u4v2: %d checks, %d failures\n", checks, failures);
    return failures;
}
