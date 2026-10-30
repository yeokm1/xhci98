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
    CHECK_EQ(fabAdp(&g_fab, 0, 1, P19) & 0x80000000UL, 0, "host port: ELR cleared");
    CHECK_EQ(fabAdp(&g_fab, d, 1, P19) & 0x80000000UL, 0, "device upstream: cleared");
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
    fabPlug(&g_fab, 1, d);
    poll(3);
    CHECK(port(1)->Pcie.Up, "a v1 device after it: PCIe up");
    CHECK_EQ(fabAdp(&g_fab, 0, HOST_PCIE, PCIE1) & 1UL, 0, "the host's EE cleared first");
    {
        long clr = writeWith(0, 0, TBT_CS_ADAPTER, HOST_PCIE, PCIE1, 1UL, 0);
        long pe = writeWith(clr < 0 ? 0 : clr, 0, TBT_CS_ADAPTER, HOST_PCIE, PCIE0,
                            0x80000000UL, 1);
        CHECK(clr >= 0 && pe > clr, "before its Path Enable");
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

int main(void)
{
    RUN_VEC(test_cmuv);
    RUN_VEC(test_cmuv_preserved);
    RUN_VEC(test_set_capabilities);
    RUN_VEC(test_set_and_recovery);
    RUN_VEC(test_clean_resets_caps);
    RUN_VEC(test_two_dwords);
    RUN_VEC(test_tmu_enhanced);
    RUN_VEC(test_cv_found);
    RUN_VEC(test_elr_and_recovery);
    RUN_VEC(test_ee);
    RUN_VEC(test_gen4);
    RUN_VEC(test_gen4_training);
    RUN_VEC(test_resume_v2);
    RUN_VEC(test_link_bound);
    fabTraceMark("end");
    printf("test_u4v2: %d checks, %d failures\n", checks, failures);
    return failures;
}
