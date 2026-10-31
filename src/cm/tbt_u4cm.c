/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * tbt_u4cm.c - the USB4 software connection manager, its pure half (see
 * tbt_u4cm.h; roadmap-usb4.md tasks 40-A.3 and 40-A.8's pure part, design
 * record 22).
 *
 * Adapted, translated to C89 for Windows 98 SE and Windows 2000, from
 * usb4-emu tools/usb4cm/core/u4cm.c (main 3c74189, read 2026-10-15) -
 * enumerate_router, discover_adapters, u4cm_host_init's port and
 * protocol-adapter pairing, router_op, drom_read and read_drom,
 * bond_lanes, u4cm_connect, config_valid, program_path,
 * program_tunnel_paths, u4cm_pcie_tunnel, u4cm_usb3_tunnel - Copyright
 * (c) 2026 usb4-emu contributors. usb4-emu's copyright holder offers what
 * this project takes from it under GPL-2.0-or-later (owner, 2026-10-25;
 * legal-provenance.md section 2a).
 *
 * The rest is written for this project from the USB4 Specification 2.0,
 * the USB4 Connection Manager Guide 2.0, the USB4 DROM Specification 1.2,
 * usb4-emu's ICM-PROTOCOL.md and captures, and design record 22. The
 * portions tasks 40-A.3 and 40-A.8 had translated from Linux
 * drivers/thunderbolt were rewritten from those sources in task 44.38
 * (2026-10-26; legal-provenance.md section 2a). A [linux] tag marks a
 * value or an order whose only source is Linux's behaviour, cited as a
 * fact.
 *
 * What changed from u4cm.c is design record 22 section 2.2's table: no
 * Host Interface Reset (ring 0 taken over, tbt_ring.c), Router Ready 500 ms,
 * Notification Timeout 255, hot plug enabled on lane 0 adapters, PTO and
 * UTO before CV with CNS 0, the Detect gate, protocol-adapter ingress
 * fields kept as read, credit accounting, USB3 weight 2 with the bandwidth
 * handshake, lane 1 disabled when bonding fails, USB4 Port is Configured,
 * request errors only failing a request, 100 ms by 4 attempts, a clock in
 * place of summed delays; and everything usb4cm lacks: hot plug and unplug,
 * the TMU, discovery and preservation, quarantine, router operations each
 * possibly unsupported, Thunderbolt 3 device routers, resume and eject.
 * Changed for xhci98 by Yeo Kheng Meng and Claude, 2026-10-15; in part
 * rewritten 2026-10-26 (task 44.38).
 *
 * IRQL: see tbt_u4cm.h - PASSIVE_LEVEL on the connection-manager thread,
 * no lock held; TbtU4QuirkMatch, TbtU4QuirkMatchDrom and TbtU4LaneClass at
 * any IRQL.
 */

#include "tbt_u4cm.h"
#include "tbt_hw.h"

/* ---------------------------------------------------------------------- */
/* Small helpers                                                          */
/* ---------------------------------------------------------------------- */

static BOOLEAN tbtU4Live(struct _TBT_U4CM *cm, struct _TBT_U4_PORT *p, ULONG gen);
#define TBT_U4_LIVE(p, gen) tbtU4Live(cm, (p), (gen))

static const TBT_ROUTE tbtU4Root = { 0, 0 };

/* Record 35's inter-domain links (task 45.59), at the end of this file. */
static VOID tbtU4XdSink(PVOID context, const TBT_FRAME *frame);
static VOID tbtU4XdTake(struct _TBT_U4CM *cm);
static VOID tbtU4XdService(struct _TBT_U4CM *cm, struct _TBT_U4_PORT *p);
static VOID tbtU4XdEnd(struct _TBT_U4CM *cm, struct _TBT_U4_PORT *p, BOOLEAN gone);
static VOID tbtU4XdEndAll(struct _TBT_U4CM *cm, BOOLEAN gone);
static VOID tbtU4XdPre(struct _TBT_U4CM *cm, struct _TBT_U4_PORT *p);

static VOID tbtU4XdEarlyEnd(PTBT_U4CM cm, PTBT_U4_PORT p);
static VOID tbtU4XdEarlyDrop(PTBT_U4CM cm, PTBT_U4_PORT p);
static VOID tbtU4XdEarlyRetry(PTBT_U4CM cm);

static VOID tbtU4Log(PTBT_U4CM cm, const char *label, ULONG value)
{
    if (cm->Host != NULL && cm->Host->Log != NULL) {
        cm->Host->Log(cm->Host->Context, label, value);
    }
}

static VOID tbtU4Zero(PVOID p, ULONG n)
{
    UCHAR *b;
    ULONG i;

    b = (UCHAR *)p;
    for (i = 0; i < n; i++) {
        b[i] = 0;
    }
}

static ULONG tbtU4Now(PTBT_U4CM cm)
{
    return TbtClockUs(cm->Host);
}

/*
 * Section 7.6: a resume runs under a budget, measured on the clock with the
 * waits known to have passed as the backstop (a stopped clock cannot hold
 * it open). Once spent, every request is refused before it is sent, so
 * whatever sequence is running ends at its next request.
 */
/* Section 7.7: contained, or the driver's gate closed under the current
 * access class (TBT_HOST.Cancelled: the loss or a surprise removal
 * arrived inside a sequence) - no request reaches the transport. A
 * recovery's own work (TbtU4Recover, decision U26) is the one exception
 * to the first. */
static BOOLEAN tbtU4Off(PTBT_U4CM cm)
{
    /* A recovery that could not hold an entry of unestablished ownership
     * unwritten (RecReserveFull) is ended at once: no further request,
     * and no nested teardown at a safe point, may write it. The failure
     * path's ring stop does not pass through here. */
    return (BOOLEAN)((cm->Contained && !cm->Recovering) ||
                     (cm->Recovering && cm->RecReserveFull) ||
                     (cm->Host->Cancelled != NULL &&
                      cm->Host->Cancelled(cm->Host->Context)));
}

static BOOLEAN tbtU4Spent(PTBT_U4CM cm)
{
    ULONG waited;

    /* A contained instance is a budget spent for good - every request
     * refused before the transport, every wait of the core's own ended -
     * without the resume's accounting. */
    if (tbtU4Off(cm)) {
        return TRUE;
    }
    if (!cm->Budgeted) {
        return FALSE;
    }
    /* The core's own pauses, plus every wait of the ring's loops - a
     * request's attempts, whether they ended in an answer or not, and
     * each acknowledgement's consumption. */
    /* Unsigned differences: the ring's sum may wrap during the resume,
     * the interval itself never comes near 2^32 us. */
    waited = cm->BudgetWaitedUs + (cm->Ring->WaitedUs - cm->BudgetRingBase);
    if (waited < cm->BudgetWaitedUs) {
        waited = 0xFFFFFFFFUL;
    }
    if (!cm->BudgetSpent &&
        (TbtClockUs(cm->Host) - cm->BudgetStartUs >= cm->BudgetUs ||
         waited >= cm->BudgetUs)) {
        cm->BudgetSpent = TRUE;
        cm->Counters.ResumeBudgetsSpent++;
        tbtU4Log(cm, "u4.resume.budget-spent", waited);
    }
    return cm->BudgetSpent;
}

static VOID tbtU4NotifyOf(PTBT_U4CM cm, const TBT_ROUTE *r, LONG rc);

static LONG tbtU4RdN(PTBT_U4CM cm, const TBT_ROUTE *r, ULONG cs, ULONG adp,
                     ULONG addr, ULONG n, PULONG v)
{
    LONG rc;

    if (tbtU4Spent(cm)) {
        if (tbtU4Off(cm)) {
            cm->Counters.ContainedRefused++;
        }
        return TBT_E_STOPPED;
    }
    rc = TbtCtlRead(&cm->Ctl, r, cs, adp, addr, n, v);
    tbtU4NotifyOf(cm, r, rc);
    if (cm->ForeignNotify && !tbtU4Spent(cm)) {
        /* Ended by another router's notification (tbtU4NotifyOf): asked
         * once more, so a request is not lost to someone else's event. */
        rc = TbtCtlRead(&cm->Ctl, r, cs, adp, addr, n, v);
        tbtU4NotifyOf(cm, r, rc);
    }
    if (rc == TBT_E_HW || rc == TBT_E_STOPPED || rc == TBT_E_RING_FULL ||
        rc == TBT_E_TIMEOUT) {
        cm->HwErrors++;
    }
    return rc;
}

static LONG tbtU4Rd(PTBT_U4CM cm, const TBT_ROUTE *r, ULONG cs, ULONG adp,
                    ULONG addr, PULONG v)
{
    return tbtU4RdN(cm, r, cs, adp, addr, 1, v);
}

static LONG tbtU4WrN(PTBT_U4CM cm, const TBT_ROUTE *r, ULONG cs, ULONG adp,
                     ULONG addr, ULONG n, const ULONG *v)
{
    LONG rc;

    if (tbtU4Spent(cm)) {
        if (tbtU4Off(cm)) {
            cm->Counters.ContainedRefused++;
        }
        return TBT_E_STOPPED;
    }
    rc = TbtCtlWrite(&cm->Ctl, r, cs, adp, addr, n, v);
    tbtU4NotifyOf(cm, r, rc);
    if (cm->ForeignNotify && !tbtU4Spent(cm) &&
        !(cs == TBT_CS_ROUTER && addr <= TBT_ROUTER_CS_26 && addr + n > TBT_ROUTER_CS_9)) {
        /* As tbtU4RdN: the same DWs written once more (a write applied
         * whose answer the notification cut short is written again with
         * the same values) - but never a router operation's launch, which
         * a second write could run twice (the mapping operation's index
         * moves on): that request fails, as any other that is cut short. */
        rc = TbtCtlWrite(&cm->Ctl, r, cs, adp, addr, n, v);
        tbtU4NotifyOf(cm, r, rc);
    }
    if (rc == TBT_E_HW || rc == TBT_E_STOPPED || rc == TBT_E_RING_FULL ||
        rc == TBT_E_TIMEOUT) {
        cm->HwErrors++;
    }
    return rc;
}

static LONG tbtU4Wr(PTBT_U4CM cm, const TBT_ROUTE *r, ULONG cs, ULONG adp,
                    ULONG addr, ULONG v)
{
    return tbtU4WrN(cm, r, cs, adp, addr, 1, &v);
}

/* One read, then one write of the changed DW (u4cm.c rmw). */
static LONG tbtU4Rmw(PTBT_U4CM cm, const TBT_ROUTE *r, ULONG cs, ULONG adp,
                     ULONG addr, ULONG clr, ULONG set)
{
    ULONG v;
    LONG rc;

    rc = tbtU4Rd(cm, r, cs, adp, addr, &v);
    if (rc != TBT_OK) {
        return rc;
    }
    return tbtU4Wr(cm, r, cs, adp, addr, (v & ~clr) | set);
}

static PTBT_U4_PORT tbtU4PortOf(PTBT_U4CM cm, ULONG index)
{
    if (index == 0 || index > cm->PortCount) {
        return NULL;
    }
    return &cm->Ports[index - 1];
}

/* The host port whose lane 0 or lane 1 adapter is `adapter`. */
static PTBT_U4_PORT tbtU4PortByLane(PTBT_U4CM cm, ULONG adapter, BOOLEAN *lane1)
{
    ULONG i;

    for (i = 0; i < cm->PortCount; i++) {
        if (cm->Ports[i].Lane0 == adapter) {
            *lane1 = FALSE;
            return &cm->Ports[i];
        }
        if (cm->Ports[i].Lane0 + 1 == adapter) {
            *lane1 = TRUE;
            return &cm->Ports[i];
        }
    }
    return NULL;
}

/* The router a route names, if this instance knows it. */
static PTBT_U4_ROUTER tbtU4RouterOf(PTBT_U4CM cm, const TBT_ROUTE *r)
{
    ULONG i;

    if (TbtRouteDepth(r) == 0) {
        return &cm->HostRouter;
    }
    for (i = 0; i < cm->PortCount; i++) {
        if ((cm->Ports[i].Dev.Present || cm->Ports[i].State == TBT_U4_CONNECTING) &&
            cm->Ports[i].Dev.Depth == 1 &&
            TbtRouteEqual(&cm->Ports[i].Dev.Route, r)) {
            return &cm->Ports[i].Dev;
        }
    }
    /* A kid's router (record 34 section 6.2), once its sequence reads it. */
    for (i = 0; i < TBT_U4_MAX_KIDS; i++) {
        if (cm->Kids[i].Used && cm->Kids[i].State != TBT_U4_EMPTY &&
            cm->Kids[i].Dev.Depth == 2 &&
            TbtRouteEqual(&cm->Kids[i].Dev.Route, r)) {
            return &cm->Kids[i].Dev;
        }
    }
    return NULL;
}

/* Record 22 section 6.2.1 item 1: a router whose USB4 Version has major 2
 * (the router's own version, whatever CMUV a predecessor wrote). */
static BOOLEAN tbtU4IsV2(const TBT_U4_ROUTER *rt)
{
    return (BOOLEAN)(!rt->Tbt3 &&
                     TBT_USB4_VERSION_MAJOR(rt->Usb4Version) == TBT_USB4_MAJOR_2);
}

/* Section 6.2.2 items 6 and 7: both ends of a port's link version 2
 * routers, and this driver a Ver. 2 Connection Manager (XhciCmUsb4V2). */
static BOOLEAN tbtU4V2Link(PTBT_U4CM cm, const TBT_U4_PORT *p)
{
    return (BOOLEAN)(cm->Config.Usb4V2 && tbtU4IsV2(&cm->HostRouter) &&
                     tbtU4IsV2(&p->Dev));
}

/*
 * Section 6.2.3 item 4 (task 45.56): one end's PORT_CS_19 written as read
 * with Enable Gen 4 Link Recovery `on`, Initiate Gen 4 Link Recovery and
 * StartAsymmetricFlow 0, and read back. TBT_OK when the read-back has the
 * bit as asked; TBT_E_STATE when the router keeps it otherwise.
 */
static LONG tbtU4ElrEnd(PTBT_U4CM cm, const TBT_U4_ROUTER *rt, ULONG adp, BOOLEAN on)
{
    ULONG v;
    ULONG want;
    LONG rc;

    if (adp == 0 || adp >= TBT_U4_MAX_ADP || rt->Adp[adp].CapPort == 0) {
        return TBT_E_STATE;
    }
    want = on ? TBT_PORT19_ELR : 0;
    rc = tbtU4Rd(cm, &rt->Route, TBT_CS_ADAPTER, adp, rt->Adp[adp].CapPort + TBT_PORT_CS_19,
                 &v);
    if (rc != TBT_OK) {
        return rc;
    }
    if ((v & TBT_PORT19_SAF) != 0) {
        /* Table 8-13: no write of PORT_CS_19 while StartAsymmetricFlow
         * reads 1 (part 6); whatever is owed stays owed. */
        return TBT_E_STATE;
    }
    rc = tbtU4Wr(cm, &rt->Route, TBT_CS_ADAPTER, adp, rt->Adp[adp].CapPort + TBT_PORT_CS_19,
                 (v & ~(TBT_PORT19_ELR | TBT_PORT19_ILR | TBT_PORT19_SAF)) | want);
    if (rc != TBT_OK) {
        return rc;
    }
    rc = tbtU4Rd(cm, &rt->Route, TBT_CS_ADAPTER, adp, rt->Adp[adp].CapPort + TBT_PORT_CS_19,
                 &v);
    if (rc != TBT_OK) {
        return rc;
    }
    return (v & TBT_PORT19_ELR) == want ? TBT_OK : TBT_E_STATE;
}

/*
 * Section 6.2.3 item 1: a link classified from the host's lane 0
 * LANE_ADP_CS_1 (Current Link Speed, Negotiated Link Width) and, for the
 * asymmetric pairing, the far end's. Both fields are valid only with the
 * link active (Spec 8.2.2.3, Table 8-12).
 */
#define TBT_U4_LINK_LEGACY      0UL     /* Gen 2 or Gen 3: section 6.5 */
#define TBT_U4_LINK_GEN4        1UL     /* Gen 4 symmetric: bonded */
#define TBT_U4_LINK_GEN4_ASYM   2UL     /* Gen 4 asymmetric, paired: bonded */
#define TBT_U4_LINK_UNUSABLE    3UL     /* Gen 4 x1, unpaired or reserved width */
#define TBT_U4_LINK_UNKNOWN     4UL     /* a reserved speed */

static ULONG tbtU4LinkKind(ULONG host, ULONG dev)
{
    ULONG w;
    ULONG dw;

    switch (TBT_LANE1_SPEED(host)) {
    case TBT_LANE_SPEED_GEN2:
    case TBT_LANE_SPEED_GEN3:
        return TBT_U4_LINK_LEGACY;
    case TBT_LANE_SPEED_GEN4:
        break;
    default:
        return TBT_U4_LINK_UNKNOWN;
    }
    w = TBT_LANE1_NLW(host);
    dw = TBT_LANE1_NLW(dev);
    if (w == TBT_LANE_WIDTH_X2) {
        return TBT_U4_LINK_GEN4;
    }
    if ((w == TBT_LANE_WIDTH_ASYM_TX && dw == TBT_LANE_WIDTH_ASYM_RX) ||
        (w == TBT_LANE_WIDTH_ASYM_RX && dw == TBT_LANE_WIDTH_ASYM_TX)) {
        return TBT_U4_LINK_GEN4_ASYM;
    }
    return TBT_U4_LINK_UNUSABLE;
}

/* The host's lane 0 alone reads a link that carries paths as a bonded one:
 * x2, or a Gen 4 asymmetric width (its pairing was read when the port came
 * up). For the read-only checks of an existing link. */
static BOOLEAN tbtU4WidthBonded(ULONG host)
{
    ULONG w;

    w = TBT_LANE1_NLW(host);
    if (w == TBT_LANE_WIDTH_X2) {
        return TRUE;
    }
    return (BOOLEAN)(TBT_LANE1_SPEED(host) == TBT_LANE_SPEED_GEN4 &&
                     (w == TBT_LANE_WIDTH_ASYM_TX || w == TBT_LANE_WIDTH_ASYM_RX));
}

/* Section 6.2.1 item 3: a preserved USB4 router's CMUV read and kept,
 * never written; logged where it is not the one this driver would have
 * written. */
static VOID tbtU4CmuvFound(PTBT_U4CM cm, PTBT_U4_ROUTER rt, ULONG cs4)
{
    if (rt->Tbt3) {
        rt->Cmuv = 0;
        return;
    }
    rt->Cmuv = TBT_RTR4_CMUV(cs4);
    if (rt->Cmuv == TBT_RTR4_CMUV(TBT_RTR4_CMUV_1_0) && tbtU4IsV2(rt)) {
        tbtU4Log(cm, "u4.cmuv.v1", (rt->Route.Lo << 8) | rt->Cmuv);
    } else if (rt->Cmuv != TBT_RTR4_CMUV(TBT_RTR4_CMUV_1_0) &&
               rt->Cmuv != TBT_RTR4_CMUV(TBT_RTR4_CMUV_2_0)) {
        tbtU4Log(cm, "u4.cmuv.odd", (rt->Route.Lo << 8) | rt->Cmuv);
    }
}

static BOOLEAN tbtU4Evidence(PTBT_U4CM cm, PTBT_U4_PORT p, PULONG cls);
static BOOLEAN tbtU4EeClear(PTBT_U4CM cm, const TBT_U4_ROUTER *rt, ULONG adp);

/*
 * Whether a port's sequence may go on (record 22 sections 7.2, 7.4): its
 * generation unchanged; no resume budget spent (section 7.6); and no
 * pending unplug - which, for the port whose sequence is running, is only
 * a hint until its registers show removal evidence (section 7.1: the
 * check decides from registers, never from an event's polarity).
 */
static BOOLEAN tbtU4Live(PTBT_U4CM cm, PTBT_U4_PORT p, ULONG gen)
{
    ULONG cls;

    if (p->Generation != gen) {
        return FALSE;
    }
    if (tbtU4Spent(cm)) {
        return FALSE;
    }
    if (p->UnplugPending) {
        if (p->Index == cm->ActivePort && !cm->InTeardown &&
            !tbtU4Evidence(cm, p, &cls)) {
            p->UnplugPending = FALSE;
            return (BOOLEAN)(p->Generation == gen);
        }
        return FALSE;
    }
    return TRUE;
}

/* A request that failed with ERR_CONN or ERR_LOCK on a device's route is
 * removal evidence (record 22 section 7.1 item 3). */
static BOOLEAN tbtU4Gone(PTBT_U4CM cm, LONG rc)
{
    /* Not one a kid's lane named for a request outside that kid
     * (tbtU4NotifyOf): that request failed, nothing more. */
    return (BOOLEAN)(rc == TBT_E_NOTIFY && !cm->ForeignNotify &&
                     (cm->Ring->LastEvent == TBT_EV_ERR_CONN ||
                      cm->Ring->LastEvent == TBT_EV_ERR_LOCK));
}

static VOID tbtU4Queue(PTBT_U4CM cm, ULONG kind, PTBT_U4_PORT p)
{
    PTBT_U4_ACTION a;

    if (cm->Recovering) {
        /* Section 7.7 part 3: a recovery publishes nothing before its
         * commit; what it drained is the driver's removal debt. */
        cm->Counters.ContainedRetired++;
        return;
    }
    if (cm->ActionCount >= TBT_U4_ACTION_QUEUE) {
        cm->Counters.ActionOverflows++;
        cm->ResyncPending = TRUE;
        return;
    }
    a = &cm->Actions[(cm->ActionHead + cm->ActionCount) % TBT_U4_ACTION_QUEUE];
    a->Kind = kind;
    a->Port = p != NULL ? p->Index : 0;
    a->Kid = 0;
    a->Generation = p != NULL ? p->Generation : 0;
    a->PcieDown = p != NULL ? p->PcieDown : 0;
    a->MapValid = p != NULL ? p->MapValid : FALSE;
    a->MapRid = p != NULL ? p->MapRid : 0;
    cm->ActionCount++;
}

ULONG TbtU4LaneClass(ULONG adapterState)
{
    switch (adapterState) {
    case TBT_LANE_STATE_CL0:
    case TBT_LANE_STATE_CL0S_TX:
    case TBT_LANE_STATE_CL0S_RX:
    case TBT_LANE_STATE_CL1:
    case TBT_LANE_STATE_CL2:
        return TBT_U4_LANE_CONNECTED;
    case TBT_LANE_STATE_TRAINING:
        return TBT_U4_LANE_TRANSITIONAL;
    case TBT_LANE_STATE_CLD:
        return TBT_U4_LANE_DISCONNECTED;
    case TBT_LANE_STATE_DISABLED:
        return TBT_U4_LANE_DISABLED;
    }
    return TBT_U4_LANE_UNKNOWN;
}

/*
 * A link not bonded is a single-lane one this driver accepts (section 6.5
 * step 6) when its lane 1 adapter `lane1` (LANE_ADP_CS_1 as read) is in
 * CLd or Disabled - or, on a Thunderbolt host, also connected: such a link
 * stays two single lanes, never disabled (record 22 section 19.9). One
 * predicate for the connection, the discovery, the resume and the
 * recovery.
 */
static BOOLEAN tbtU4SingleOk(PTBT_U4CM cm, ULONG lane1)
{
    ULONG cls;

    cls = TbtU4LaneClass(TBT_LANE1_STATE(lane1));
    if (cls == TBT_U4_LANE_DISCONNECTED || cls == TBT_U4_LANE_DISABLED) {
        return TRUE;
    }
    return (BOOLEAN)(cm->HostRouter.Tbt3 && cls == TBT_U4_LANE_CONNECTED);
}

/* The same through the link view, for a kid's link: lane 1 disconnected
 * or disabled, or, behind a Thunderbolt 3 parent (whose lane 1 is never
 * disabled, tbtU4KidBond), connected as a second single lane. */
static BOOLEAN tbtU4KidSingleOk(const TBT_U4_ROUTER *pr, ULONG lane1)
{
    ULONG cls;

    cls = TbtU4LaneClass(TBT_LANE1_STATE(lane1));
    if (cls == TBT_U4_LANE_DISCONNECTED || cls == TBT_U4_LANE_DISABLED) {
        return TRUE;
    }
    return (BOOLEAN)(pr->Tbt3 && cls == TBT_U4_LANE_CONNECTED);
}

/* ---------------------------------------------------------------------- */
/* The quirk table, record 22 section 6.10 ([linux]: ids and flags)        */
/* ---------------------------------------------------------------------- */

typedef struct _TBT_U4_QUIRK_ROW {
    ULONG Vendor;
    ULONG Device;
    ULONG Flags;
} TBT_U4_QUIRK_ROW;

static const TBT_U4_QUIRK_ROW tbtU4Quirks[] = {
    { 0x8086, 0x15EF, TBT_U4_QUIRK_NO_CLX },    /* Titan Ridge DD (NVM < 65h) */
    { 0x8087, 0x0B26, TBT_U4_QUIRK_DP_CREDITS },/* Goshen Ridge */
    { 0x8087, 0x463E, TBT_U4_QUIRK_USB3_MAX_BW },
    { 0x8087, 0x466D, TBT_U4_QUIRK_USB3_MAX_BW },
    { 0x8087, 0xA73E, TBT_U4_QUIRK_USB3_MAX_BW },
    { 0x8087, 0xA76D, TBT_U4_QUIRK_USB3_MAX_BW },
    { 0x8087, 0x7EB2, TBT_U4_QUIRK_USB3_MAX_BW },
    { 0x8087, 0x7EC2, TBT_U4_QUIRK_USB3_MAX_BW },
    { 0x8087, 0x7EC3, TBT_U4_QUIRK_USB3_MAX_BW },
    { 0x8087, 0x5781, TBT_U4_QUIRK_USB3_MAX_BW | TBT_U4_QUIRK_BLOCK_RPM },
    { 0x8087, 0x5784, TBT_U4_QUIRK_USB3_MAX_BW | TBT_U4_QUIRK_BLOCK_RPM },
    { 0x8087, 0x5786, TBT_U4_QUIRK_USB3_MAX_BW },
    { 0x8087, 0x57A4, TBT_U4_QUIRK_USB3_MAX_BW },
    { 0x0438, 0x0208, TBT_U4_QUIRK_NO_CLX },    /* AMD Yellow Carp, Pink Sardine */
    { 0x0438, 0x0209, TBT_U4_QUIRK_NO_CLX },
    { 0x0438, 0x020A, TBT_U4_QUIRK_NO_CLX },
    { 0x0438, 0x020B, TBT_U4_QUIRK_NO_CLX }
};

ULONG TbtU4QuirkMatch(ULONG vendor, ULONG device)
{
    ULONG i;
    ULONG f;

    f = 0;
    for (i = 0; i < sizeof(tbtU4Quirks) / sizeof(tbtU4Quirks[0]); i++) {
        if (tbtU4Quirks[i].Vendor == vendor && tbtU4Quirks[i].Device == device) {
            f |= tbtU4Quirks[i].Flags;
        }
    }
    return f;
}

/* Dell WD19TB (record 22 section 6.10, [linux]: matched on the DROM's
 * ids). */
ULONG TbtU4QuirkMatchDrom(ULONG dromVendor, ULONG dromDevice)
{
    if (dromVendor == 0x00D4 && (dromDevice == 0xB070 || dromDevice == 0xB071)) {
        return TBT_U4_QUIRK_FORCE_POWER;
    }
    return 0;
}

static VOID tbtU4ApplyQuirks(PTBT_U4CM cm, PTBT_U4_ROUTER rt)
{
    rt->Quirks = TbtU4QuirkMatch(rt->Vendor, rt->Device);
    if (rt->Quirks != 0) {
        cm->Counters.QuirksFired++;
        tbtU4Log(cm, "u4.quirk", (rt->Device << 16) | rt->Quirks);
    }
}

/* ---------------------------------------------------------------------- */
/* Waits and the safe point (record 22 sections 5.3 and 7.4)              */
/* ---------------------------------------------------------------------- */

static VOID tbtU4TeardownHead(PTBT_U4CM cm, PTBT_U4_PORT p, BOOLEAN nested);
static VOID tbtU4TeardownTail(PTBT_U4CM cm, PTBT_U4_PORT p);
static BOOLEAN tbtU4HostRestore(PTBT_U4CM cm, PTBT_U4_PORT p);
static BOOLEAN tbtU4Evidence(PTBT_U4CM cm, PTBT_U4_PORT p, PULONG cls);
static LONG tbtU4Tbt3Setup(PTBT_U4CM cm, PTBT_U4_ROUTER rt);
static ULONG tbtU4Ltssm(PTBT_U4CM cm, ULONG v);
static BOOLEAN tbtU4Departed(PTBT_U4CM cm, PTBT_U4_PORT p, const BOOLEAN *ob);
static BOOLEAN tbtU4RecCleanupGate(PTBT_U4CM cm, PTBT_U4_PORT p);
static VOID tbtU4KidsDrop(PTBT_U4CM cm, PTBT_U4_PORT p);
static BOOLEAN tbtU4KidLive(PTBT_U4CM cm, PTBT_U4_PORT p, PTBT_U4_KID k, ULONG gen);
static BOOLEAN tbtU4KidsDown(PTBT_U4CM cm, PTBT_U4_PORT p, ULONG why);

/* One step of a poll loop of the connection manager's own: the wait, then
 * the safe point. */
static VOID tbtU4Pause(PTBT_U4CM cm, PTBT_WAIT w)
{
    TbtWaitStep(w, cm->Host, TBT_U4_POLL_STEP_US);
    if (cm->Budgeted) {
        cm->BudgetWaitedUs += TBT_U4_POLL_STEP_US + cm->Host->TickUs;
    }
    TbtCtlSafePoint(&cm->Ctl);
}

/* A whole minimum (Guide's settles and holds), with safe points. */
static VOID tbtU4WaitMin(PTBT_U4CM cm, ULONG minimumUs)
{
    TBT_WAIT w;

    if (minimumUs < TBT_U4_POLL_STEP_US) {
        TbtWaitMin(cm->Host, minimumUs);
        return;
    }
    TbtWaitStart(&w, cm->Host, minimumUs, TRUE);
    while (!TbtWaitDone(&w, cm->Host) && !tbtU4Spent(cm)) {
        tbtU4Pause(cm, &w);
    }
}

/* The kid on `route`'s downstream lane `adapter` (its lane 0, or with
 * *lane1 its lane 1): `route` its parent's, a host port's device router. */
static PTBT_U4_KID tbtU4KidByLane(PTBT_U4CM cm, const TBT_ROUTE *route, ULONG adapter,
                                  BOOLEAN *lane1)
{
    ULONG i;
    PTBT_U4_KID k;
    PTBT_U4_PORT p;

    *lane1 = FALSE;
    for (i = 0; i < TBT_U4_MAX_KIDS; i++) {
        k = &cm->Kids[i];
        p = tbtU4PortOf(cm, k->Port);
        if (!k->Used || p == NULL || !TbtRouteEqual(&p->Dev.Route, route) ||
            TbtRouteDepth(route) != 1) {
            continue;
        }
        if (k->ParentLane0 == adapter) {
            return k;
        }
        if (k->ParentLane0 + 1 == adapter) {
            *lane1 = TRUE;
            return k;
        }
    }
    return NULL;
}

/* Whether `route` is `top` or below it. */
static BOOLEAN tbtU4Under(const TBT_ROUTE *route, const TBT_ROUTE *top);
static BOOLEAN tbtU4KidNote(PTBT_U4CM cm, const TBT_NOTE *n);

static VOID tbtU4Hint(PTBT_U4CM cm, const TBT_ROUTE *route, ULONG adapter,
                      BOOLEAN unplug);

/*
 * Record 34 section 6.5 at a request: the transport ends the one request
 * outstanding with whatever ERR_CONN or ERR_LOCK arrives meanwhile, its
 * route not compared (tbt_ring.c), so a child's departure can end a
 * request to its parent or to another port. A router reports either
 * error for the adapter it could not forward through (Spec 6.4.2.3), so
 * one that ends a request to route `r` is that request's own only when it
 * comes from a router above `r` (the host router included) and names the
 * next hop towards `r`; a request to the host router itself forwards
 * nothing, so none is its own. Any other is foreign: for the request
 * never removal evidence (tbtU4Gone; the request is asked once more,
 * tbtU4RdN), and the notification handled as its queued form would be
 * (tbtU4KidNote, else tbtU4Hint) - so a child's departure is its kid's
 * check and unplug hint and only a plain check of the host port, and
 * another host port's its own check and hint.
 */
static VOID tbtU4NotifyOf(PTBT_U4CM cm, const TBT_ROUTE *r, LONG rc)
{
    TBT_NOTE n;
    TBT_ROUTE top;
    ULONG de;
    ULONG dr;

    cm->ForeignNotify = FALSE;
    if (rc != TBT_E_NOTIFY || (cm->Ring->LastEvent != TBT_EV_ERR_CONN &&
                               cm->Ring->LastEvent != TBT_EV_ERR_LOCK)) {
        return;
    }
    n.Route.Hi = cm->Ring->LastEventRouteHi;
    n.Route.Lo = cm->Ring->LastEventRouteLo;
    n.Code = cm->Ring->LastEvent;
    n.Info = cm->Ring->LastEventInfo;
    n.Sq = FALSE;
    de = TbtRouteDepth(&n.Route);
    dr = TbtRouteDepth(r);
    if (de < dr && (de == 0 || tbtU4Under(r, &n.Route)) &&
        TbtRouteLevel(r, de) == n.Info) {
        return;     /* the request's own */
    }
    cm->ForeignNotify = TRUE;
    tbtU4Log(cm, "u4.notify.foreign", (n.Code << 8) | n.Info);
    if (de == 0) {
        tbtU4Hint(cm, &n.Route, n.Info, TRUE);
    } else if (!tbtU4KidNote(cm, &n)) {
        top.Hi = 0;
        top.Lo = 0;
        tbtU4Hint(cm, &top, TbtRouteLevel(&n.Route, 0), TRUE);
    }
}

static BOOLEAN tbtU4Under(const TBT_ROUTE *route, const TBT_ROUTE *top)
{
    ULONG lv;
    ULONG d;

    d = TbtRouteDepth(top);
    if (d == 0 || TbtRouteDepth(route) < d) {
        return FALSE;
    }
    for (lv = 0; lv < d; lv++) {
        if (TbtRouteLevel(route, lv) != TbtRouteLevel(top, lv)) {
            return FALSE;
        }
    }
    return TRUE;
}

/*
 * Record 34 section 6.5: an Error Notification that is a kid's. One from
 * a kid's parent naming the kid's lane 0 or lane 1 is the kid's check
 * (an ERR_CONN or ERR_LOCK its unplug hint); one from a kid's own route
 * or below it is the kid's check alone (the links there are watched as
 * today). Either queues a plain check of the host port, never its unplug.
 * FALSE: not a kid's, handled as before.
 */
static BOOLEAN tbtU4KidNote(PTBT_U4CM cm, const TBT_NOTE *n)
{
    PTBT_U4_KID k;
    PTBT_U4_PORT p;
    BOOLEAN lane1;
    ULONG i;

    k = tbtU4KidByLane(cm, &n->Route, n->Info, &lane1);
    if (k != NULL) {
        k->CheckPending = TRUE;
        if (n->Code != TBT_EV_ERR_PLUG && k->State != TBT_U4_EMPTY) {
            k->UnplugPending = TRUE;
        }
    } else {
        for (i = 0; i < TBT_U4_MAX_KIDS; i++) {
            if (cm->Kids[i].Used && cm->Kids[i].Dev.Depth == 2 &&
                tbtU4Under(&n->Route, &cm->Kids[i].Dev.Route)) {
                k = &cm->Kids[i];
                k->CheckPending = TRUE;
                break;
            }
        }
    }
    if (k == NULL) {
        return FALSE;
    }
    p = tbtU4PortOf(cm, k->Port);
    if (p != NULL) {
        p->CheckPending = TRUE;
    }
    return TRUE;
}

/* What a hot plug event or a note means for a port: a check, and for an
 * unplug or a disconnect on its route, the pending-unplug hint. */
static VOID tbtU4Hint(PTBT_U4CM cm, const TBT_ROUTE *route, ULONG adapter,
                      BOOLEAN unplug)
{
    PTBT_U4_PORT p;
    PTBT_U4_KID k;
    BOOLEAN lane1;
    ULONG i;

    if (TbtRouteDepth(route) == 0) {
        p = tbtU4PortByLane(cm, adapter, &lane1);
        if (p == NULL || lane1) {
            /* Lane 1 (bonding produces one), DP and other adapters: counted
             * only (record 22 section 7.1). */
            cm->Counters.HotPlugOther++;
            return;
        }
        cm->Counters.HotPlugLane0++;
        p->CheckPending = TRUE;
        if (unplug && p->State != TBT_U4_EMPTY) {
            p->UnplugPending = TRUE;
        }
        return;
    }
    /* From a device router's downstream port: a check of a discovered link
     * there, else counted (the port stays locked, Guide 3.1.1). */
    cm->Counters.HotPlugDownstream++;
    /* Record 34 section 6.5: a lane 0 event of a parent's downstream port
     * is a kid check there (its unplug hint when it reads unplug and the
     * kid is not EMPTY), and only a plain check of the host port; lane 1
     * is counted. An empty port is scanned by the idle tick anyway. */
    k = tbtU4KidByLane(cm, route, adapter, &lane1);
    if (k != NULL && !lane1) {
        k->CheckPending = TRUE;
        if (unplug && k->State != TBT_U4_EMPTY) {
            k->UnplugPending = TRUE;
        }
        p = tbtU4PortOf(cm, k->Port);
        if (p != NULL) {
            p->CheckPending = TRUE;
        }
    }
    for (i = 0; i < TBT_U4_MAX_LINKS; i++) {
        if (cm->Links[i].Used && TbtRouteEqual(&cm->Links[i].Parent, route) &&
            (cm->Links[i].ParentLane0 == adapter ||
             cm->Links[i].ParentLane0 + 1 == adapter)) {
            p = tbtU4PortOf(cm, cm->Links[i].Port);
            if (p != NULL) {
                p->CheckPending = TRUE;
            }
        }
    }
}

/* Drain what the transport queued. */
static VOID tbtU4TakeQueues(PTBT_U4CM cm)
{
    TBT_HP_EVENT e;
    TBT_NOTE n;
    TBT_ROUTE top;
    ULONG i;
    PTBT_U4_PORT p;
    BOOLEAN lane1;

    while (TbtCtlNextHotPlug(&cm->Ctl, &e)) {
        tbtU4Hint(cm, &e.Route, e.Adapter, e.Unplug);
    }
    while (TbtCtlNextNote(&cm->Ctl, &n)) {
        tbtU4Log(cm, "u4.note", (n.Code << 8) | n.Info);
        if (n.Code == TBT_EV_LINK_RECOVERY) {
            /* Record 22 section 6.2.3 item 4: never acknowledged; a check
             * of the port whose path holds the router that sent it (its
             * lane adapter's port at the host, else the depth-1 one). */
            cm->Counters.LinkRecoveries++;
            p = tbtU4PortByLane(cm, TbtRouteDepth(&n.Route) == 0 ? n.Info :
                                TbtRouteLevel(&n.Route, 0), &lane1);
            if (p != NULL) {
                /* Task 45.56: and that port's recovery check, after the
                 * check (section 6.2.3 item 4 part 4), its 2 s anew. */
                p->CheckPending = TRUE;
                p->ElrCheckOwed = TRUE;
                p->ElrCheckTried = FALSE;
                p->ElrNoteSeq++;
            }
            continue;
        }
        if (n.Code == TBT_EV_ERR_CONN || n.Code == TBT_EV_ERR_LOCK ||
            n.Code == TBT_EV_ERR_PLUG) {
            /* The adapter on the route that reported it, or the depth-1
             * router the route names. */
            if (TbtRouteDepth(&n.Route) == 0) {
                tbtU4Hint(cm, &n.Route, n.Info, (BOOLEAN)(n.Code != TBT_EV_ERR_PLUG));
            } else if (tbtU4KidNote(cm, &n)) {
                /* Record 34 section 6.5: a kid's, never the host port's
                 * unplug. */
            } else {
                top.Hi = 0;
                top.Lo = 0;
                tbtU4Hint(cm, &top, TbtRouteLevel(&n.Route, 0),
                          (BOOLEAN)(n.Code != TBT_EV_ERR_PLUG));
            }
        }
    }
    if (cm->Ctl.HpLost || cm->Ctl.NotesLost) {
        cm->Ctl.HpLost = FALSE;
        cm->Ctl.NotesLost = FALSE;
        for (i = 0; i < cm->PortCount; i++) {
            cm->Ports[i].CheckPending = TRUE;
            /* Part 5: a LINK_RECOVERY may have been among them. */
            if (cm->Ports[i].ElrSet) {
                cm->Ports[i].ElrCheckOwed = TRUE;
                cm->Ports[i].ElrCheckTried = FALSE;
                cm->Ports[i].ElrNoteSeq++;
            }
        }
    }
}

/*
 * The safe point (record 22 section 7.4): drain the transport's queues;
 * a pending unplug of a port other than the one whose sequence is running
 * has its teardown's steps 1 to 3 run at once, nested, once registers show
 * removal evidence, its steps 4 to 6 owed for after the outer step. A
 * teardown in progress does not nest a second.
 */
static VOID tbtU4SafePoint(PVOID context)
{
    PTBT_U4CM cm;
    PTBT_U4_PORT p;
    ULONG i;
    ULONG cls;
    ULONG kid;

    cm = (PTBT_U4CM)context;
    if (tbtU4Off(cm)) {
        return;     /* section 7.7: the ring is not serviced */
    }
    if (!cm->Budgeted) {
        TbtCtlService(&cm->Ctl);
    } else if (!tbtU4Spent(cm)) {
        /* Under a resume budget: a bounded batch, its acknowledgements'
         * waits counted; the rest stays on the ring for the next. */
        TbtCtlServiceN(&cm->Ctl, TBT_U4_SERVICE_BATCH);
    }
    tbtU4TakeQueues(cm);
    if (cm->InTeardown) {
        return;
    }
    for (i = 0; i < cm->PortCount; i++) {
        p = &cm->Ports[i];
        if (!p->UnplugPending || p->Index == cm->ActivePort ||
            cm->DeferredTail[i] || p->State == TBT_U4_EMPTY ||
            p->State == TBT_U4_TEARDOWN || p->RecDeparted) {
            /* A departed port (section 7.7) gets its recorded host-side
             * drain alone (tbtU4Departed), never an ordinary teardown. */
            continue;
        }
        if (!tbtU4Evidence(cm, p, &cls)) {
            p->UnplugPending = FALSE;
            continue;
        }
        cm->Counters.UnplugsNested++;
        cm->InTeardown = TRUE;
        /* Nested inside a kid's work, maybe: this port's records are its
         * own, never the kid's (record 34 section 6.7). */
        kid = cm->KidCtx;
        cm->KidCtx = 0;
        tbtU4TeardownHead(cm, p, TRUE);
        cm->KidCtx = kid;
        cm->InTeardown = FALSE;
        cm->DeferredTail[i] = 1;
    }
}

/* ---------------------------------------------------------------------- */
/* Router operations, record 22 section 6.7 (u4cm.c router_op)            */
/* ---------------------------------------------------------------------- */

/*
 * ROUTER_CS_26 read first: OV set (a predecessor's operation, or one of
 * this driver's that timed out) is waited out up to 500 ms, and still set
 * the router is operation-blocked - nothing is written (Guide 9.1). Then
 * data, metadata and opcode with OV in one write, OV polled clear within
 * 500 ms, ONS and Status read before any result. `meta` in and out;
 * `dataIn` DWs written (the rest 0, the Spec 8.3.1 note), `dataOut` DWs
 * read back on success.
 */
static LONG tbtU4RouterOp(PTBT_U4CM cm, PTBT_U4_ROUTER rt, ULONG opcode,
                          PULONG meta, const ULONG *dataIn, ULONG nIn,
                          PULONG dataOut, ULONG nOut)
{
    ULONG regs[TBT_ROP_DATA_DWORDS + 2];
    ULONG cs26;
    ULONG i;
    LONG rc;
    TBT_WAIT w;

    rc = tbtU4Rd(cm, &rt->Route, TBT_CS_ROUTER, 0, TBT_ROUTER_CS_26, &cs26);
    if (rc != TBT_OK) {
        return rc;
    }
    if ((cs26 & TBT_RTR26_OV) != 0) {
        TbtWaitStart(&w, cm->Host, TBT_U4_ROP_US, FALSE);
        while ((cs26 & TBT_RTR26_OV) != 0 && !TbtWaitDone(&w, cm->Host)) {
            tbtU4Pause(cm, &w);
            rc = tbtU4Rd(cm, &rt->Route, TBT_CS_ROUTER, 0, TBT_ROUTER_CS_26, &cs26);
            if (rc != TBT_OK) {
                return rc;
            }
        }
        if ((cs26 & TBT_RTR26_OV) != 0) {
            rt->OpBlocked = TRUE;
            cm->Counters.RopBlocked++;
            tbtU4Log(cm, "u4.rop.blocked", opcode);
            return TBT_E_BLOCKED;
        }
    }
    rt->OpBlocked = FALSE;
    for (i = 0; i < TBT_ROP_DATA_DWORDS; i++) {
        regs[i] = (dataIn != NULL && i < nIn) ? dataIn[i] : 0;
    }
    regs[TBT_ROP_DATA_DWORDS] = *meta;
    regs[TBT_ROP_DATA_DWORDS + 1] = (opcode & TBT_RTR26_OPCODE_MASK) | TBT_RTR26_OV;
    cm->Counters.RopIssued++;
    rc = tbtU4WrN(cm, &rt->Route, TBT_CS_ROUTER, 0, TBT_ROUTER_CS_9,
                  TBT_ROP_DATA_DWORDS + 2, regs);
    if (rc != TBT_OK) {
        return rc;
    }
    TbtWaitStart(&w, cm->Host, TBT_U4_ROP_US, FALSE);
    for (;;) {
        rc = tbtU4Rd(cm, &rt->Route, TBT_CS_ROUTER, 0, TBT_ROUTER_CS_26, &cs26);
        if (rc != TBT_OK) {
            return rc;
        }
        if ((cs26 & TBT_RTR26_OV) == 0) {
            break;
        }
        if (TbtWaitDone(&w, cm->Host)) {
            rt->OpBlocked = TRUE;
            cm->Counters.RopTimeouts++;
            tbtU4Log(cm, "u4.rop.timeout", opcode);
            return TBT_E_BLOCKED;
        }
        tbtU4Pause(cm, &w);
    }
    if ((cs26 & TBT_RTR26_ONS) != 0) {
        cm->Counters.RopUnsupported++;
        return TBT_E_UNSUPPORTED;
    }
    if (TBT_RTR26_STATUS(cs26) != 0) {
        cm->Counters.RopFailed++;
        tbtU4Log(cm, "u4.rop.failed", (opcode << 8) | TBT_RTR26_STATUS(cs26));
        return TBT_E_FAILED;
    }
    rc = tbtU4Rd(cm, &rt->Route, TBT_CS_ROUTER, 0, TBT_ROUTER_CS_25, meta);
    if (rc == TBT_OK && dataOut != NULL && nOut != 0) {
        rc = tbtU4RdN(cm, &rt->Route, TBT_CS_ROUTER, 0, TBT_ROUTER_CS_9,
                      nOut > TBT_ROP_DATA_DWORDS ? TBT_ROP_DATA_DWORDS : nOut,
                      dataOut);
    }
    return rc;
}

/* ---------------------------------------------------------------------- */
/* DROM (record 22 section 6.4 step 6; u4cm.c drom_read and read_drom)    */
/* ---------------------------------------------------------------------- */

static VOID tbtU4NameCopy(char *dst, const UCHAR *src, ULONG n)
{
    ULONG i;

    if (n >= TBT_U4_NAME_LEN) {
        n = TBT_U4_NAME_LEN - 1;
    }
    for (i = 0; i < n && src[i] != 0; i++) {
        dst[i] = (src[i] >= 0x20 && src[i] < 0x7F) ? (char)src[i] : '?';
    }
    dst[i] = 0;
}

/*
 * A Thunderbolt 3 router's DROM, read from its flash by the bit banging
 * interface of its Vendor Specific 1 Capability (record 22 section 8 item
 * 4; Spec 13.4.5 and Table 13-15, the TBT3-Compatible registers; the
 * write sequence as usb4-emu ICM-PROTOCOL.md section 5.4 records it from
 * the P14s capture [obs], and as usb4-router-tb3.c models it). VSC_CS_4:
 * bit 0 FL_SK the clock, bit 1 FL_CS the chip select (low selects), bit 2
 * FL_DI data to the part, bit 3 FL_DO data from it, bit 4 Bit Banging
 * Enable, bit 5 Invalid Flash Memory; bit 7 reads 1 when a part is
 * present (ICM-PROTOCOL.md 5.4). Every write goes to that one DW.
 */
/* The EEPROM's release (deselect, bit-banging off): a spent resume budget
 * does not refuse it - a part left selected would hold the router's
 * EEPROM - only a contained or cancelled instance does. */
static LONG tbtU4EeRelease(PTBT_U4CM cm, PTBT_U4_ROUTER rt, ULONG ctl)
{
    LONG rc;

    if (tbtU4Off(cm)) {
        cm->Counters.ContainedRefused++;
        return TBT_E_STOPPED;
    }
    rc = TbtCtlWrite(&cm->Ctl, &rt->Route, TBT_CS_ROUTER, 0,
                     rt->CapPlugEvents + TBT_PLUG_EVENTS_EE_CTL, 1, &ctl);
    if (rc == TBT_E_HW || rc == TBT_E_STOPPED || rc == TBT_E_RING_FULL ||
        rc == TBT_E_TIMEOUT) {
        cm->HwErrors++;
    }
    return rc;
}

static LONG tbtU4EeWr(PTBT_U4CM cm, PTBT_U4_ROUTER rt, ULONG ctl)
{
    return tbtU4Wr(cm, &rt->Route, TBT_CS_ROUTER, 0,
                   rt->CapPlugEvents + TBT_PLUG_EVENTS_EE_CTL, ctl);
}

/* ICM-PROTOCOL.md 5.4 step 2: one byte out, most significant bit first;
 * each bit three writes - DI set to the bit with the clock low, the clock
 * high, the clock low again. `ctl` keeps the last value written. */
static LONG tbtU4EeSend(PTBT_U4CM cm, PTBT_U4_ROUTER rt, PULONG ctl, ULONG byte)
{
    ULONG mask;
    LONG rc;

    rc = TBT_OK;
    for (mask = 0x80UL; mask != 0 && rc == TBT_OK; mask >>= 1) {
        *ctl = (*ctl & ~(TBT_EE_DI | TBT_EE_SK)) | ((byte & mask) != 0 ? TBT_EE_DI : 0);
        rc = tbtU4EeWr(cm, rt, *ctl);
        if (rc == TBT_OK) {
            rc = tbtU4EeWr(cm, rt, *ctl | TBT_EE_SK);
        }
        if (rc == TBT_OK) {
            rc = tbtU4EeWr(cm, rt, *ctl);
        }
    }
    return rc;
}

/* ICM-PROTOCOL.md 5.4 step 4: one byte in, most significant bit first;
 * each bit the clock high, the control DW read for DO, the clock low. */
static LONG tbtU4EeRecv(PTBT_U4CM cm, PTBT_U4_ROUTER rt, ULONG ctl, UCHAR *byte)
{
    ULONG mask;
    ULONG got;
    ULONG seen;
    LONG rc;

    seen = 0;
    for (mask = 0x80UL; mask != 0; mask >>= 1) {
        rc = tbtU4EeWr(cm, rt, ctl | TBT_EE_SK);
        if (rc == TBT_OK) {
            rc = tbtU4Rd(cm, &rt->Route, TBT_CS_ROUTER, 0,
                         rt->CapPlugEvents + TBT_PLUG_EVENTS_EE_CTL, &got);
        }
        if (rc == TBT_OK) {
            rc = tbtU4EeWr(cm, rt, ctl);
        }
        if (rc != TBT_OK) {
            return rc;
        }
        if ((got & TBT_EE_DO) != 0) {
            seen |= mask;
        }
    }
    *byte = (UCHAR)seen;
    return TBT_OK;
}

/*
 * `count` bytes from `offset` in the DROM. Where the DROM sits in the part
 * is VSC_CS_12, DROM Base Address (Table 13-15), at most FFFFh; the part
 * must read present and not invalid. The transfer is ICM-PROTOCOL.md 5.4's
 * steps 1 to 5: enable, then select; the read command 03h and the 16-bit
 * address, high byte first (Spec 13.4.5), the DROM's base plus `offset`;
 * the bytes; then deselect and disable. Once
 * the enable was sent - a write whose answer was lost may still have been
 * applied - the last two writes are made whatever happened between, each
 * whatever the other did, so the part is never left selected or enabled.
 */
static LONG tbtU4EepromRead(PTBT_U4CM cm, PTBT_U4_ROUTER rt, ULONG offset,
                            ULONG count, UCHAR *out)
{
    ULONG ctl;
    ULONG base;
    ULONG addr;
    ULONG cmd[3];
    ULONG i;
    LONG rc;
    LONG rcEnd;

    if (rt->CapPlugEvents == 0) {
        return TBT_E_UNSUPPORTED;
    }
    rc = tbtU4Rd(cm, &rt->Route, TBT_CS_ROUTER, 0,
                 rt->CapPlugEvents + TBT_PLUG_EVENTS_DROM_OFFSET, &base);
    if (rc == TBT_OK) {
        rc = tbtU4Rd(cm, &rt->Route, TBT_CS_ROUTER, 0,
                     rt->CapPlugEvents + TBT_PLUG_EVENTS_EE_CTL, &ctl);
    }
    if (rc != TBT_OK) {
        return rc;
    }
    if ((ctl & TBT_EE_NOT_PRESENT) != 0 || (ctl & TBT_EE_PRESENT) == 0) {
        return TBT_E_UNSUPPORTED;
    }
    if (base > 0xFFFFUL || base + offset > 0xFFFFUL) {
        return TBT_E_STATE;
    }
    addr = base + offset;
    cmd[0] = TBT_EE_READ;
    cmd[1] = addr >> 8;
    cmd[2] = addr & 0xFFUL;

    ctl = (ctl | TBT_EE_ENABLE) & ~(TBT_EE_SK | TBT_EE_DI | TBT_EE_DO);
    rc = tbtU4EeWr(cm, rt, ctl);
    if (rc == TBT_OK) {
        ctl &= ~TBT_EE_CS;
        rc = tbtU4EeWr(cm, rt, ctl);
    }
    for (i = 0; i < 3 && rc == TBT_OK; i++) {
        rc = tbtU4EeSend(cm, rt, &ctl, cmd[i]);
    }
    ctl &= ~TBT_EE_DI;
    for (i = 0; i < count && rc == TBT_OK; i++) {
        rc = tbtU4EeRecv(cm, rt, ctl, &out[i]);
    }
    ctl = (ctl | TBT_EE_CS) & ~TBT_EE_SK;
    rcEnd = tbtU4EeRelease(cm, rt, ctl);
    if (tbtU4EeRelease(cm, rt, ctl & ~TBT_EE_ENABLE) != TBT_OK) {
        rcEnd = TBT_E_STATE;
    }
    if (rcEnd != TBT_OK) {
        tbtU4Log(cm, "u4.drom.eeprom-release", (ULONG)rcEnd);
    }
    return rc != TBT_OK ? rc : rcEnd;
}

static LONG tbtU4DromChunk(PTBT_U4CM cm, PTBT_U4_ROUTER rt, ULONG offset,
                           ULONG n, UCHAR *out)
{
    ULONG data[TBT_ROP_DATA_DWORDS];
    ULONG meta;
    ULONG i;
    LONG rc;

    if (rt->Tbt3) {
        return tbtU4EepromRead(cm, rt, offset, 4UL * n, out);
    }
    meta = (((offset / 4UL) << TBT_ROP_DROM_ADDR_SHIFT) & TBT_ROP_DROM_ADDR_MASK) |
           ((n << TBT_ROP_DROM_SIZE_SHIFT) & TBT_ROP_DROM_SIZE_MASK);
    rc = tbtU4RouterOp(cm, rt, TBT_ROP_DROM_READ, &meta, NULL, 0, data, n);
    if (rc != TBT_OK) {
        return rc;
    }
    for (i = 0; i < 4UL * n; i++) {
        out[i] = (UCHAR)(data[i / 4] >> (8 * (i % 4)));
    }
    return TBT_OK;
}

/*
 * The DROM (USB4 DROM Specification 1.2; Guide 2.5, 9.1): CRC32 over the
 * bytes from Version (byte 13) on, Version, Length (byte 14, 12 bits),
 * then the entries. Their start depends on the Version: byte 16 for a
 * USB4 DROM, Version 3 (DROM sections 4 and 5); byte 22 for a TBT3
 * product's (Version 2) or a TBT3-Compatible USB4 product's (Version 1),
 * whose TBT3-Identification and TBT3-Header sections replace the header
 * (DROM section 8.2 to 8.4) - which a USB4 device router may carry: the
 * ASMedia ASM2464PD of usb4-emu docs/captures/ORICO-ASM2464.md section 2.
 * Any other Version is read as the TBT3 layout ([linux], record 22
 * section 6.11), and logged. Kept: the ASCII vendor (generic entry 1)
 * and model (generic entry 2) names, and every adapter entry (Adapter
 * Entry, bit 7) with Adapter Disabled (bit 6) set, which marks that
 * adapter unused (DROM section 6.1): the router's adapters are then
 * discovered without it (record 22 section 6.11). A failure is logged
 * and the device proceeds unnamed, every adapter used.
 */
static VOID tbtU4ReadDrom(PTBT_U4CM cm, PTBT_U4_ROUTER rt)
{
    UCHAR buf[TBT_U4_DROM_MAX];
    ULONG len;
    ULONG got;
    ULONG n;
    ULONG pos;
    ULONG crc;
    ULONG idx;
    ULONG unused[2];
    LONG rc;

    cm->Counters.DromReads++;
    rc = tbtU4DromChunk(cm, rt, 0, 4, buf);
    if (rc != TBT_OK) {
        tbtU4Log(cm, "u4.drom.unavailable", (ULONG)rc);
        return;
    }
    len = 13UL + ((ULONG)buf[14] | (((ULONG)buf[15] & 0x0FUL) << 8));
    if (len > TBT_U4_DROM_MAX || len < 16) {
        tbtU4Log(cm, "u4.drom.length", len);
        return;
    }
    for (got = 16; got < len; got += 4UL * n) {
        n = (len - got + 3UL) / 4UL;
        if (n > TBT_ROP_DATA_DWORDS) {
            n = TBT_ROP_DATA_DWORDS;
        }
        if (got + 4UL * n > TBT_U4_DROM_MAX) {
            n = (TBT_U4_DROM_MAX - got) / 4UL;
        }
        rc = tbtU4DromChunk(cm, rt, got, n, buf + got);
        if (rc != TBT_OK) {
            tbtU4Log(cm, "u4.drom.unavailable", (ULONG)rc);
            return;
        }
    }
    crc = (ULONG)buf[9] | ((ULONG)buf[10] << 8) | ((ULONG)buf[11] << 16) |
          ((ULONG)buf[12] << 24);
    if (TbtCrc32c(buf + 13, len - 13) != crc) {
        cm->Counters.DromCrcFailures++;
        tbtU4Log(cm, "u4.drom.crc", crc);
        return;
    }
    rt->DromRevision = buf[13];
    pos = TBT_DROM_ENTRIES_TBT3;
    if (buf[13] == TBT_DROM_REV_USB4) {
        pos = TBT_DROM_ENTRIES_USB4;
    } else if (buf[13] != TBT_DROM_REV_TBT3) {
        tbtU4Log(cm, "u4.drom.revision", buf[13]);
    }
    /* Checked whole before anything is kept: a malformed list (an entry
     * shorter than its header, or running past the data) changes nothing. */
    unused[0] = 0;
    unused[1] = 0;
    for (got = pos; got < len; got += buf[got]) {
        if (got + 2 > len || buf[got] < 2 || got + buf[got] > len) {
            tbtU4Log(cm, "u4.drom.entries", got);
            return;
        }
        idx = (ULONG)buf[got + 1] & TBT_DROM_ENTRY_INDEX;
        if ((buf[got + 1] & TBT_DROM_ENTRY_ADAPTER) != 0 &&
            (buf[got + 1] & TBT_DROM_ENTRY_DISABLED) != 0 && idx != 0) {
            unused[idx / 32] |= 1UL << (idx % 32);
        }
    }
    for (; pos < len; pos += buf[pos]) {
        if ((buf[pos + 1] & TBT_DROM_ENTRY_ADAPTER) != 0) {
            continue;
        }
        if ((buf[pos + 1] & TBT_DROM_ENTRY_INDEX) == TBT_DROM_GENERIC_VENDOR) {
            tbtU4NameCopy(rt->VendorName, buf + pos + 2, (ULONG)buf[pos] - 2);
        } else if ((buf[pos + 1] & TBT_DROM_ENTRY_INDEX) == TBT_DROM_GENERIC_MODEL) {
            tbtU4NameCopy(rt->ModelName, buf + pos + 2, (ULONG)buf[pos] - 2);
        }
    }
    rt->DromUnused[0] = unused[0];
    rt->DromUnused[1] = unused[1];
    if (unused[0] != 0 || unused[1] != 0) {
        tbtU4Log(cm, "u4.drom.unused", unused[0]);
    }
}

/* The DROM marks adapter `a` unused (tbtU4ReadDrom). Never the router's
 * own upstream lanes, which carry the link the DROM was read over. */
static BOOLEAN tbtU4DromUnused(const TBT_U4_ROUTER *rt, ULONG a)
{
    return (BOOLEAN)(a < 64 && a != rt->UpAdp && a != rt->UpAdp + 1 &&
                     (rt->DromUnused[a / 32] & (1UL << (a % 32))) != 0);
}

/* ---------------------------------------------------------------------- */
/* Capabilities and adapters (Guide 3.2.2; Spec 8.2; u4cm.c               */
/* discover_adapters)                                                     */
/* ---------------------------------------------------------------------- */

/*
 * The router's capability list from ROUTER_CS_1's Next Capability
 * Pointer, each header's two DWs in one read, in any order, an undefined
 * capability skipped (Spec 8.2.1; Guide 3.2.2): the TMU's position kept,
 * and on a Thunderbolt 3 router those of its Vendor Specific 1 and
 * Vendor Specific Extended 6 Capabilities (Spec 13.6.1.2, 13.6.1.5). A
 * Vendor Specific Capability with VSC Length 0 is the Extended form,
 * whose next pointer is DW 1 bits 15:0 (8.2.1.3, 8.2.1.4). A USB4
 * router's vendor-specific capabilities are its vendor's and are not
 * kept (the ASM2464 carries IDs 1, 3 and 6 after its TMU at C0h, usb4-emu
 * docs/captures/ORICO-ASM2464.md section 3). At most TBT_CAP_WALK_LIMIT.
 */
static LONG tbtU4RouterCaps(PTBT_U4CM cm, PTBT_U4_ROUTER rt, ULONG first)
{
    ULONG at;
    ULONG hdr[2];
    ULONG steps;
    LONG rc;

    rt->CapTmu = 0;
    rt->CapPlugEvents = 0;
    rt->CapLc = 0;
    at = first;
    for (steps = 0; steps < TBT_CAP_WALK_LIMIT; steps++) {
        if (at == 0 || at > TBT_CTL_ADDR_MASK - 1) {
            break;
        }
        rc = tbtU4RdN(cm, &rt->Route, TBT_CS_ROUTER, 0, at, 2, hdr);
        if (rc != TBT_OK) {
            return rc;
        }
        if (TBT_CAP_ID(hdr[0]) != TBT_RCAP_VSC) {
            if (TBT_CAP_ID(hdr[0]) == TBT_RCAP_TMU) {
                rt->CapTmu = at;
            }
            at = TBT_CAP_NEXT(hdr[0]);
            continue;
        }
        if (rt->Tbt3) {
            switch (TBT_CAP_VSEC_ID(hdr[0])) {
            case TBT_VSEC_PLUG_EVENTS:
                rt->CapPlugEvents = at;
                break;
            case TBT_VSEC_LINK_CONTROLLER:
                rt->CapLc = at;
                break;
            }
        }
        at = TBT_CAP_VSEC_LENGTH(hdr[0]) != 0 ? TBT_CAP_NEXT(hdr[0]) :
             TBT_CAP_LONG_NEXT(hdr[1]);
    }
    return TBT_OK;
}

/*
 * Every adapter 1 to Max Adapter: ADP_CS_0 to 5; ERR_ADDR (or ERR_ADP) or
 * the Unsupported type marks it unused (Guide 3.2.2); the capability chain
 * walked for the lane, TMU, adapter and USB4 port capabilities, at most 16
 * links, any order. A lane adapter with a USB4 Port Capability reports its
 * control path's credits in its HopID 0 entry (Guide 5.1.4 step 2); any
 * other lane adapter's are taken as 2 ([linux], record 22 section 8 item
 * 5).
 */
/*
 * Light Ridge and Eagle Ridge (record 22 section 19.4 item 2, [linux] cap.c
 * tb_port_enable_tmu and tb_port_dummy_read): TMU access enabled around a
 * walk of an adapter's capabilities, then one read of the adapter's DW 0,
 * since the next-capability read returns stale data there. 0 for every
 * other router.
 */
static ULONG tbtU4TmuAccessDw(const TBT_U4_ROUTER *rt)
{
    if (!rt->Tbt3 || rt->Vendor != 0x8086) {
        return 0;
    }
    if (rt->Device == 0x1513) {
        return TBT_LR_TMU_ACCESS_DW;
    }
    if (rt->Device == 0x151A) {
        return TBT_ER_TMU_ACCESS_DW;
    }
    return 0;
}

static LONG tbtU4DiscoverAdapters(PTBT_U4CM cm, PTBT_U4_ROUTER rt)
{
    ULONG a;
    ULONG v[TBT_ADP_BASIC_DWORDS];
    ULONG h;
    ULONG e[2];
    ULONG ptr;
    ULONG n;
    ULONG tmu;
    LONG rc;
    LONG rcTmu;
    PTBT_U4_ADP ad;

    for (a = 1; a < TBT_U4_MAX_ADP; a++) {
        tbtU4Zero(&rt->Adp[a], sizeof(rt->Adp[a]));
    }
    for (a = 1; a <= rt->MaxAdp && a < TBT_U4_MAX_ADP; a++) {
        ad = &rt->Adp[a];
        if (tbtU4DromUnused(rt, a)) {
            /* Unused by the DROM (DROM section 6.1: connected to nothing
             * in the product): never read nor used (record 22 section
             * 6.11). */
            continue;
        }
        rc = tbtU4RdN(cm, &rt->Route, TBT_CS_ADAPTER, a, 0, TBT_ADP_BASIC_DWORDS, v);
        if (rc == TBT_E_NOTIFY && (cm->Ring->LastEvent == TBT_EV_ERR_ADDR ||
                                   cm->Ring->LastEvent == TBT_EV_ERR_ADP)) {
            continue;
        }
        if (rc != TBT_OK) {
            return rc;
        }
        ad->Type = TBT_ADP2_TYPE(v[TBT_ADP_CS_2]);
        if (ad->Type == TBT_ADP_TYPE_UNUSED) {
            continue;
        }
        ad->Present = 1;
        ad->MaxInHop = (UCHAR)TBT_ADP5_MAX_IN_HOP(v[TBT_ADP_CS_5]);
        ad->MaxOutHop = (UCHAR)TBT_ADP5_MAX_OUT_HOP(v[TBT_ADP_CS_5]);
        ptr = TBT_ADP1_NEXT_CAP(v[TBT_ADP_CS_1]);
        tmu = tbtU4TmuAccessDw(rt);
        if (tmu != 0) {
            rc = tbtU4Rmw(cm, &rt->Route, TBT_CS_ROUTER, 0, tmu, 0, TBT_TMU_ACCESS_EN);
            if (rc != TBT_OK) {
                return rc;
            }
        }
        for (n = 0; ptr != 0 && n < TBT_CAP_WALK_LIMIT; n++) {
            rc = tbtU4Rd(cm, &rt->Route, TBT_CS_ADAPTER, a, ptr, &h);
            if (rc != TBT_OK) {
                if (tmu != 0) {
                    (void)tbtU4Rmw(cm, &rt->Route, TBT_CS_ROUTER, 0, tmu,
                                   TBT_TMU_ACCESS_EN, 0);
                }
                return rc;
            }
            switch (TBT_CAP_ID(h)) {
            case TBT_ACAP_LANE:
                ad->CapLane = (UCHAR)ptr;
                break;
            case TBT_ACAP_USB4_PORT:
                ad->CapPort = (UCHAR)ptr;
                break;
            case TBT_ACAP_ADAPTER:
                ad->CapAdp = (UCHAR)ptr;
                break;
            case TBT_ACAP_TMU:
                ad->CapTmu = (UCHAR)ptr;
                break;
            }
            ptr = TBT_CAP_NEXT(h);
        }
        if (tmu != 0) {
            rcTmu = tbtU4Rmw(cm, &rt->Route, TBT_CS_ROUTER, 0, tmu, TBT_TMU_ACCESS_EN, 0);
            if (rcTmu == TBT_OK) {
                rcTmu = tbtU4Rd(cm, &rt->Route, TBT_CS_ADAPTER, a, TBT_ADP_CS_0, &h);
            }
            if (rcTmu != TBT_OK) {
                return rcTmu;
            }
        }
        if (ad->Type == TBT_ADP_TYPE_LANE) {
            ad->CtlCredits = (UCHAR)TBT_TBT3_CTL_CREDITS;
            if (ad->CapPort != 0 &&
                tbtU4RdN(cm, &rt->Route, TBT_CS_PATH, a, 0, 2, e) == TBT_OK &&
                TBT_PATH0_CREDITS(e[0]) != 0) {
                ad->CtlCredits = (UCHAR)TBT_PATH0_CREDITS(e[0]);
            }
        }
    }
    return TBT_OK;
}

/* The first adapter of `type`, 0 for none. */
static ULONG tbtU4FirstOf(const TBT_U4_ROUTER *rt, ULONG type)
{
    ULONG a;

    for (a = 1; a <= rt->MaxAdp && a < TBT_U4_MAX_ADP; a++) {
        if (rt->Adp[a].Present && rt->Adp[a].Type == type) {
            return a;
        }
    }
    return 0;
}

/* ---------------------------------------------------------------------- */
/* Get Capabilities and the Buffer Allocation Request (section 6.4)       */
/* ---------------------------------------------------------------------- */

/*
 * Capability Index 0 (Spec Tables 8-49 to 8-51): every capability's
 * supported and enabled bits in one answer. A router enumerated here has
 * every capability at its default, disabled, whatever the answer; a
 * preserved router's answer is adopted on success, none enabled on Not
 * Supported, and unknown on anything else (no Buffer Allocation Request,
 * no acknowledgement).
 *
 * As a Ver. 2 Connection Manager (XhciCmUsb4V2, record 22 section 6.2.2
 * item 3), on any USB4 router: an answer reporting the Sequence bit in
 * Notification Packet capability supported and not enabled is followed by
 * Set Capabilities (opcode 32h, Capability Index 2, Enable Capability 1,
 * Spec Tables 8-53 to 8-55) and Get Capabilities again as the read-back,
 * whose answer is then the router's capability state - for a router
 * enumerated here too, since the Set took it from its default. Until the
 * read-back settles it the state stays PENDING, so the router's
 * notifications are held (section 6.4). A Set answering Status non-zero
 * or Not Supported is judged by the read-back; a Set or read-back that
 * fails otherwise, times out or finds the router operation-blocked leaves
 * the state unknown. A recovery (section 7.7) classifies by reading alone:
 * it sends no Set (one it finds owed, SeqOwed, is made at the next tick),
 * and for a router this driver ever sent the Set (SeqSet, whatever its
 * read-back recorded) it adopts a successful answer and leaves a failed
 * one unknown.
 */
static ULONG tbtU4CapsParse(PTBT_U4_ROUTER rt, ULONG dw0)
{
    ULONG k;

    rt->CapsSupported = 0;
    rt->CapsEnabled = 0;
    for (k = 1; k <= TBT_CAP_CLX_LATENCY; k++) {
        if ((dw0 >> (2 * k)) & 1UL) {
            rt->CapsSupported |= 1UL << k;
        }
        if ((dw0 >> (2 * k + 1)) & 1UL) {
            rt->CapsEnabled |= 1UL << k;
        }
    }
    return TBT_U4_CAPS_KNOWN;
}

static ULONG tbtU4SetSequenceBit(PTBT_U4CM cm, PTBT_U4_ROUTER rt)
{
    ULONG meta;
    ULONG data[TBT_ROP_DATA_DWORDS];
    LONG rc;

    meta = TBT_CAP_NOTIFY_SEQUENCE | TBT_ROP_SETCAP_ENABLE;
    /* Owned before the request: a Set whose answer is lost may still have
     * applied. */
    rt->SeqSet = TRUE;
    rc = tbtU4RouterOp(cm, rt, TBT_ROP_SET_CAPABILITIES, &meta, NULL, 0, NULL, 0);
    if (rc != TBT_OK && rc != TBT_E_FAILED && rc != TBT_E_UNSUPPORTED) {
        cm->Counters.SeqUnknown++;
        tbtU4Log(cm, "u4.caps.seq-unknown", (ULONG)rc);
        return TBT_U4_CAPS_UNKNOWN;
    }
    meta = 0;
    data[0] = 0;
    if (tbtU4RouterOp(cm, rt, TBT_ROP_GET_CAPABILITIES, &meta, NULL, 0, data, 1) != TBT_OK) {
        cm->Counters.SeqUnknown++;
        tbtU4Log(cm, "u4.caps.seq-unknown", 0xFFFFFFFFUL);
        return TBT_U4_CAPS_UNKNOWN;
    }
    (void)tbtU4CapsParse(rt, data[0]);
    if ((rt->CapsEnabled & (1UL << TBT_CAP_NOTIFY_SEQUENCE)) != 0) {
        cm->Counters.SeqEnabled++;
        tbtU4Log(cm, "u4.caps.seq", rt->Route.Lo);
    } else {
        cm->Counters.SeqRefused++;
        tbtU4Log(cm, "u4.caps.seq-refused", (ULONG)rc);
    }
    return TBT_U4_CAPS_KNOWN;
}

static VOID tbtU4GetCapabilities(PTBT_U4CM cm, PTBT_U4_ROUTER rt)
{
    ULONG meta;
    ULONG data[TBT_ROP_DATA_DWORDS];
    ULONG state;
    BOOLEAN hadSeq;
    BOOLEAN v2;
    LONG rc;

    hadSeq = (BOOLEAN)(rt->SeqSet ||
                       (rt->CapsState == TBT_U4_CAPS_KNOWN &&
                        (rt->CapsEnabled & (1UL << TBT_CAP_NOTIFY_SEQUENCE)) != 0));
    rt->SeqOwed = FALSE;
    v2 = (BOOLEAN)(cm->Config.Usb4V2 && !rt->Tbt3);
    /* Held until this settles the form (section 6.4). */
    rt->CapsState = TBT_U4_CAPS_PENDING;
    meta = 0;
    data[0] = 0;
    rc = tbtU4RouterOp(cm, rt, TBT_ROP_GET_CAPABILITIES, &meta, NULL, 0, data, 1);
    rt->CapsSupported = 0;
    rt->CapsEnabled = 0;
    if (rt->EnumeratedHere && !v2) {
        state = TBT_U4_CAPS_DEFAULT;
    } else if (rc == TBT_OK) {
        state = tbtU4CapsParse(rt, data[0]);
        if (rt->EnumeratedHere && !(cm->Recovering && hadSeq)) {
            /* At its default whatever the answer, until a Set. */
            state = TBT_U4_CAPS_DEFAULT;
            rt->CapsEnabled = 0;
        }
        if (v2 && (rt->CapsSupported & (1UL << TBT_CAP_NOTIFY_SEQUENCE)) != 0 &&
            (rt->CapsEnabled & (1UL << TBT_CAP_NOTIFY_SEQUENCE)) == 0) {
            if (cm->Recovering) {
                rt->SeqOwed = TRUE;
            } else if (cm->SeqGuardPort != 0 &&
                       !tbtU4Live(cm, &cm->Ports[cm->SeqGuardPort - 1], cm->SeqGuardGen)) {
                /* The owed Set's port gone or changed under the Get: no
                 * request to whatever holds the route now. */
                state = TBT_U4_CAPS_UNKNOWN;
            } else {
                state = tbtU4SetSequenceBit(cm, rt);
            }
        }
        if (state == TBT_U4_CAPS_KNOWN &&
            (rt->CapsEnabled & ~((1UL << TBT_CAP_NOTIFY_SEQUENCE) |
                                 (1UL << TBT_CAP_BA_PER_PORT) |
                                 (1UL << TBT_CAP_HOT_PLUG_FAILURE))) != 0) {
            tbtU4Log(cm, "u4.caps.enabled-unhandled", rt->CapsEnabled);
        }
    } else if (rc == TBT_E_UNSUPPORTED) {
        state = TBT_U4_CAPS_DEFAULT;
    } else if (rt->EnumeratedHere && !(cm->Recovering && hadSeq)) {
        /* Never Set here: still at its default. */
        state = TBT_U4_CAPS_DEFAULT;
    } else {
        state = TBT_U4_CAPS_UNKNOWN;
        tbtU4Log(cm, "u4.caps.unknown", (ULONG)rc);
    }
    if (state != TBT_U4_CAPS_KNOWN) {
        rt->CapsSupported = 0;
        rt->CapsEnabled = 0;
    }
    rt->CapsState = state;
    if (!tbtU4Off(cm)) {
        TbtCtlSettle(&cm->Ctl, &rt->Route);
    }
}

/*
 * The router's buffer preferences, Guide 5.1.4 step 3: the Buffer
 * Allocation Request router operation (opcode 33h, Spec Tables 8-56 to
 * 8-58). Its Metadata names the lane 0 adapter (bits 5:0) only when the
 * router has Buffer Allocation Per USB4 Port enabled, else it is 0. The
 * completion's Metadata 7:0 is the number of parameter DWs, each a
 * Parameter Index (15:0) and its buffer count (31:16); of the first
 * TBT_U4_BA_READ read, baMaxPCIe and baMaxUSB3 GenX are kept. A router
 * whose capability state is unknown or pending is not asked.
 */
#define TBT_U4_BA_READ  5UL

static VOID tbtU4BufferAllocation(PTBT_U4CM cm, PTBT_U4_ROUTER rt, ULONG lane0)
{
    ULONG meta;
    ULONG param[TBT_ROP_DATA_DWORDS];
    ULONG count;
    ULONG i;
    LONG rc;

    rt->BaValid = FALSE;
    rt->BaMaxPcie = 0;
    rt->BaMaxUsb3 = 0;
    rt->BaMaxHi = 0;
    switch (rt->CapsState) {
    case TBT_U4_CAPS_UNKNOWN:
    case TBT_U4_CAPS_PENDING:
        return;
    }
    meta = (rt->CapsState == TBT_U4_CAPS_KNOWN &&
            (rt->CapsEnabled & (1UL << TBT_CAP_BA_PER_PORT)) != 0) ? (lane0 & 0x3FUL) : 0;
    rc = tbtU4RouterOp(cm, rt, TBT_ROP_BUFFER_ALLOC, &meta, NULL, 0, param, TBT_U4_BA_READ);
    if (rc != TBT_OK) {
        tbtU4Log(cm, "u4.ba.none", (ULONG)rc);
        return;
    }
    count = TBT_ROP_BA_LENGTH(meta);
    for (i = 0; i < count && i < TBT_U4_BA_READ; i++) {
        switch (TBT_ROP_BA_INDEX(param[i])) {
        case TBT_BA_MAX_PCIE:
            rt->BaMaxPcie = TBT_ROP_BA_VALUE(param[i]);
            break;
        case TBT_BA_MAX_USB3:
            rt->BaMaxUsb3 = TBT_ROP_BA_VALUE(param[i]);
            break;
        case TBT_BA_MAX_HI:
            rt->BaMaxHi = TBT_ROP_BA_VALUE(param[i]);
            break;
        }
    }
    rt->BaValid = TRUE;
}

/* The acknowledgement form of a router's notifications (TBT_CTL_ACK_FORM). */
static ULONG tbtU4AckForm(PVOID context, const TBT_ROUTE *route)
{
    PTBT_U4CM cm;
    PTBT_U4_ROUTER rt;
    ULONG caps;
    ULONG enabled;
    ULONG i;

    cm = (PTBT_U4CM)context;
    rt = tbtU4RouterOf(cm, route);
    if (rt == NULL) {
        /* A preserved router below depth 1: its own answer (section 6.4).
         * Any other router this instance does not know: its capability
         * state is unknown, so neither form is sent. */
        for (i = 0; i < TBT_U4_MAX_LINKS; i++) {
            if (cm->Links[i].Used && TbtRouteEqual(&cm->Links[i].Child, route)) {
                caps = cm->Links[i].CapsState;
                enabled = cm->Links[i].CapsEnabled;
                goto form;
            }
        }
        return TBT_ACK_NONE;
    }
    if (rt->Tbt3) {
        return TBT_ACK_PLAIN;
    }
    caps = rt->CapsState;
    enabled = rt->CapsEnabled;
form:
    switch (caps) {
    case TBT_U4_CAPS_PENDING:
        return TBT_ACK_PENDING;
    case TBT_U4_CAPS_UNKNOWN:
        return TBT_ACK_NONE;
    case TBT_U4_CAPS_KNOWN:
        if ((enabled & (1UL << TBT_CAP_NOTIFY_SEQUENCE)) != 0) {
            return TBT_ACK_ENHANCED;
        }
        break;
    }
    return TBT_ACK_PLAIN;
}

/* ---------------------------------------------------------------------- */
/* Path entries, HopIDs, credits and the quarantine (record 22 sections   */
/* 6.8, 7.3, 9.2)                                                         */
/* ---------------------------------------------------------------------- */

typedef struct _TBT_U4_USAGE {
    ULONG InUse[4];         /* input HopIDs of the lane adapter, live or held */
    ULONG OutUse[4];        /* output HopIDs other entries send out of it */
    ULONG Dedicated;        /* credits of its entries with IFC on, ISE off */
} TBT_U4_USAGE, *PTBT_U4_USAGE;

#define TBT_U4_BIT_SET(m, h)    ((m)[(h) >> 5] |= 1UL << ((h) & 31UL))
#define TBT_U4_BIT_GET(m, h)    (((m)[(h) >> 5] >> ((h) & 31UL)) & 1UL)

static BOOLEAN tbtU4Quarantined(PTBT_U4CM cm, const TBT_ROUTE *r, ULONG adp,
                                ULONG hop, BOOLEAN enable)
{
    ULONG i;
    PTBT_U4_QENTRY q;

    for (i = 0; i < TBT_U4_QUARANTINE; i++) {
        q = &cm->Quarantine[i];
        if (q->Used && q->Enable == enable && q->Adapter == adp &&
            TbtRouteEqual(&q->Route, r) && (enable || q->Hop == hop)) {
            return TRUE;
        }
    }
    return FALSE;
}

/* Section 7.7: a quarantine record held without writes (Reserve) for this
 * entry, or with `enable` this adapter's Path Enable. */
static BOOLEAN tbtU4Reserved(PTBT_U4CM cm, const TBT_ROUTE *r, ULONG adp,
                             ULONG hop, BOOLEAN enable)
{
    ULONG i;
    PTBT_U4_QENTRY q;

    for (i = 0; i < TBT_U4_QUARANTINE; i++) {
        q = &cm->Quarantine[i];
        if (q->Used && q->Reserve && q->Enable == enable && q->Adapter == adp &&
            TbtRouteEqual(&q->Route, r) && (enable || q->Hop == hop)) {
            return TRUE;
        }
    }
    return FALSE;
}

static VOID tbtU4Quarantine(PTBT_U4CM cm, ULONG port, const TBT_ROUTE *r,
                            ULONG adp, ULONG hop, ULONG e0, BOOLEAN enable,
                            ULONG cap, ULONG bit)
{
    ULONG i;
    PTBT_U4_QENTRY q;

    if (tbtU4Quarantined(cm, r, adp, hop, enable)) {
        return;
    }
    for (i = 0; i < TBT_U4_QUARANTINE; i++) {
        q = &cm->Quarantine[i];
        if (!q->Used) {
            q->Used = TRUE;
            q->Enable = enable;
            q->Reserve = FALSE;
            q->Port = port;
            q->Generation = (port != 0 && port <= cm->PortCount) ?
                            cm->Ports[port - 1].Generation : 0;
            /* Record 34 section 6.7: inside a kid's work, keyed by the kid
             * too; an entry on the kid's own router lives with the kid's
             * connection, one on its parent with the parent's. */
            q->Kid = 0;
            if (cm->KidCtx != 0 && cm->KidCtx <= TBT_U4_MAX_KIDS &&
                cm->Kids[cm->KidCtx - 1].Used && cm->Kids[cm->KidCtx - 1].Port == port) {
                q->Kid = cm->KidCtx;
                if (TbtRouteDepth(r) == 2) {
                    q->Generation = cm->Kids[cm->KidCtx - 1].Generation;
                }
            }
            q->Route = *r;
            q->Adapter = adp;
            q->Hop = hop;
            q->OutAdapter = TBT_PATH0_OUT_ADP(e0);
            q->OutHop = TBT_PATH0_OUT_HOP(e0);
            q->Credits = TBT_PATH0_CREDITS(e0);
            q->EnableCap = cap;
            q->EnableBit = bit;
            cm->Counters.Quarantined++;
            tbtU4Log(cm, "u4.quarantine", (adp << 16) | (enable ? 0x8000UL : hop));
            return;
        }
    }
    /* A full table: the entry is still never reused, because every
     * allocation scans the registers, where it stays Valid or pending -
     * the lanes' HopIDs (tbtU4ScanLane) and the protocol adapters' HopID 8
     * (tbtU4AdapterFree) alike. */
    tbtU4Log(cm, "u4.quarantine-full", (adp << 16) | hop);
}

/*
 * Section 7.7 part 3 step 4: an entry (or an adapter's Path Enable) whose
 * ownership a recovery could not establish - read as another path's, or
 * not read - quarantined without writes: held out of use as section 7.3's
 * quarantine holds it, and refused by every write of this core
 * (tbtU4Invalidate, tbtU4Disable, and so the teardowns and the sweeps)
 * until tbtU4RetryQuarantine reads it closed (lifted), or as this driver
 * wrote it (`outAdp`, `outHop`, made writable again; an enable once its
 * adapter's HopID 8 entry reads closed). A reserved protocol entry also
 * holds its adapter's Path Enable. TRUE when a record now holds it.
 */
static BOOLEAN tbtU4Reserve(PTBT_U4CM cm, ULONG port, const TBT_ROUTE *r,
                            ULONG adp, ULONG hop, ULONG outAdp, ULONG outHop,
                            BOOLEAN enable, ULONG cap, ULONG bit)
{
    ULONG i;
    BOOLEAN held;
    PTBT_U4_QENTRY q;

    tbtU4Quarantine(cm, port, r, adp, hop,
                    ((outAdp << TBT_PATH0_OUT_ADP_SHIFT) & TBT_PATH0_OUT_ADP_MASK) |
                        (outHop & TBT_PATH0_OUT_HOP_MASK),
                    enable, cap, bit);
    held = FALSE;
    for (i = 0; i < TBT_U4_QUARANTINE; i++) {
        q = &cm->Quarantine[i];
        if (q->Used && q->Enable == enable && q->Adapter == adp &&
            TbtRouteEqual(&q->Route, r) && (enable || q->Hop == hop)) {
            held = TRUE;
            q->Reserve = TRUE;
            q->OutAdapter = outAdp;
            q->OutHop = outHop;
            if (enable) {
                q->EnableCap = cap;
                q->EnableBit = bit;
            }
            tbtU4Log(cm, "u4.quarantine.reserve",
                     (adp << 16) | (enable ? 0x8000UL : hop));
        }
    }
    if (!held) {
        /* A full table: nothing would keep the writes off it, so the
         * recovery attempt is refused (TbtU4Recover), the instance kept
         * contained for the episode. */
        cm->RecReserveFull = TRUE;
        tbtU4Log(cm, "u4.quarantine.reserve-full", (adp << 16) | hop);
    }
    return held;
}

/*
 * The live use of one lane adapter, read from the registers (Spec 8.2.3):
 * every entry of every adapter of the router from HopID 8 (1 on the host
 * interface adapter) to its Max Input HopID, two DWs each, 30 entries a
 * request; an entry Valid or with Pending Packets holds its input HopID,
 * its output HopID on its egress adapter, and, with ingress flow control
 * on and shared buffering off, its dedicated credits - each counted once,
 * from the registers. The quarantine additionally reserves the HopIDs it
 * holds; their credits, if still held, are in the registers.
 */
static LONG tbtU4ScanLane(PTBT_U4CM cm, const TBT_U4_ROUTER *rt, ULONG lane,
                          PTBT_U4_USAGE u)
{
    ULONG a;
    ULONG h;
    ULONG first;
    ULONG last;
    ULONG k;
    ULONG n;
    ULONG e[2 * 30];
    ULONG i;
    LONG rc;
    const TBT_U4_QENTRY *q;

    tbtU4Zero(u, sizeof(*u));
    for (a = 1; a <= rt->MaxAdp && a < TBT_U4_MAX_ADP; a++) {
        if (!rt->Adp[a].Present) {
            continue;
        }
        first = rt->Adp[a].Type == TBT_ADP_TYPE_HI ? 1UL : TBT_HOP_FIRST;
        last = rt->Adp[a].MaxInHop;
        for (h = first; h <= last; h += n) {
            n = last - h + 1;
            if (n > 30) {
                n = 30;
            }
            rc = tbtU4RdN(cm, &rt->Route, TBT_CS_PATH, a, 2 * h, 2 * n, e);
            if (rc != TBT_OK) {
                return rc;
            }
            for (k = 0; k < n; k++) {
                if (a == lane && ((e[2 * k] & TBT_PATH0_VALID) != 0 ||
                                  (e[2 * k + 1] & TBT_PATH1_PP) != 0)) {
                    TBT_U4_BIT_SET(u->InUse, h + k);
                    /* Valid or still pending, its buffers are held: each
                     * ingress HopID counted once, here. */
                    if ((e[2 * k + 1] & TBT_PATH1_IFC) != 0 &&
                        (e[2 * k + 1] & TBT_PATH1_ISE) == 0) {
                        u->Dedicated += TBT_PATH0_CREDITS(e[2 * k]);
                    }
                }
                if (((e[2 * k] & TBT_PATH0_VALID) != 0 ||
                     (e[2 * k + 1] & TBT_PATH1_PP) != 0) &&
                    TBT_PATH0_OUT_ADP(e[2 * k]) == lane) {
                    TBT_U4_BIT_SET(u->OutUse, TBT_PATH0_OUT_HOP(e[2 * k]));
                }
            }
        }
    }
    for (i = 0; i < TBT_U4_QUARANTINE; i++) {
        q = &cm->Quarantine[i];
        if (!q->Used || q->Enable || !TbtRouteEqual(&q->Route, &rt->Route)) {
            continue;
        }
        if (q->Adapter == lane) {
            /* Its HopID held; its credits, if any are still held, were
             * counted from its registers above (Valid or pending). */
            TBT_U4_BIT_SET(u->InUse, q->Hop & TBT_HOP_MAX);
        }
        if (q->OutAdapter == lane) {
            TBT_U4_BIT_SET(u->OutUse, q->OutHop & TBT_HOP_MAX);
        }
    }
    return TBT_OK;
}

/*
 * The buffers left for a new path at an ingress lane adapter (Guide
 * 5.1.4 steps 1 and 2, with 5.1.1 to 5.1.3): ADP_CS_4 Total Buffers, read
 * again with ADP_CS_5 in one request, less what is spoken for - the
 * credits of Path 0, the control path, as read at discovery; the
 * Non-Flow Controlled Buffers (ADP_CS_4); the Link Credits Allocated
 * shared pool (ADP_CS_5); and the dedicated credits of the entries this
 * driver counts in `u`. Each pool once; 0 when nothing is left.
 */
static LONG tbtU4LaneAvail(PTBT_U4CM cm, const TBT_U4_ROUTER *rt, ULONG lane,
                           const TBT_U4_USAGE *u, PULONG avail)
{
    ULONG cs[2];
    ULONG taken;
    LONG rc;

    rc = tbtU4RdN(cm, &rt->Route, TBT_CS_ADAPTER, lane, TBT_ADP_CS_4, 2, cs);
    if (rc != TBT_OK) {
        return rc;
    }
    taken = u->Dedicated;
    taken += TBT_ADP5_LCA(cs[1]);
    taken += TBT_ADP4_NFC_BUFFERS(cs[0]);
    taken += (ULONG)rt->Adp[lane].CtlCredits;
    *avail = 0;
    if (TBT_ADP4_TOTAL_BUFFERS(cs[0]) > taken) {
        *avail = TBT_ADP4_TOTAL_BUFFERS(cs[0]) - taken;
    }
    return TBT_OK;
}

/* The lowest HopID from 8 that the egress may send (its Max Output HopID)
 * and the next ingress may take (its Max Input HopID), free on both
 * (Guide 5.2.1; Spec 8.2.3.2: a lane adapter's paths start at HopID 8).
 * 0 for none. */
static ULONG tbtU4AllocHop(ULONG maxOut, ULONG maxIn, const ULONG *outUse,
                           const ULONG *inUse)
{
    ULONG h;
    ULONG top;

    top = maxOut < maxIn ? maxOut : maxIn;
    for (h = TBT_HOP_FIRST; h <= top && h <= TBT_HOP_MAX; h++) {
        if (!TBT_U4_BIT_GET(outUse, h) && !TBT_U4_BIT_GET(inUse, h)) {
            return h;
        }
    }
    return 0;
}

/*
 * One path entry, one read of its two DWs and one write (Guide 5.2.1;
 * u4cm.c program_path; Guide Table 5-2): output adapter and HopID, Valid,
 * priority 3 and `weight`. A lane-adapter ingress (`lane`) gets `credits`,
 * IFC 1, EFC 0, ISE 0, ESE 0. A protocol-adapter ingress keeps its
 * credits, IFC and ISE as read (Guide 5.2.1: a connection manager does
 * not change them there) and gets EFC 1, ESE 0 - except on a Thunderbolt
 * 3 router (`tbt3`), where they are written as for a lane adapter
 * (section 8 item 5, [linux]), and where `asm7`
 * (XhciCmAsmediaPcie7Credits on the one router pair it names, record 22
 * section 18.8) writes 7 credits, IFC 1 and ISE 0, an experiment
 * read on the hardware bench (44-E.6); `asm7` logs the entry as
 * read, as written and as read back under `label`.
 */
static LONG tbtU4WriteEntry(PTBT_U4CM cm, const TBT_ROUTE *r, ULONG adp,
                            ULONG hop, ULONG outAdp, ULONG outHop,
                            ULONG credits, ULONG weight, BOOLEAN lane,
                            BOOLEAN tbt3, BOOLEAN asm7, const char *label)
{
    ULONG e[2];
    ULONG back[2];
    LONG rc;

    rc = tbtU4RdN(cm, r, TBT_CS_PATH, adp, 2 * hop, 2, e);
    if (rc != TBT_OK) {
        return rc;
    }
    if (asm7) {
        tbtU4Log(cm, label, e[0]);
        tbtU4Log(cm, label, e[1]);
    }
    e[0] &= ~(TBT_PATH0_OUT_HOP_MASK | TBT_PATH0_OUT_ADP_MASK | 0x01000000UL |
              TBT_PATH0_VALID);
    e[0] |= (outHop & TBT_PATH0_OUT_HOP_MASK) |
            ((outAdp << TBT_PATH0_OUT_ADP_SHIFT) & TBT_PATH0_OUT_ADP_MASK) |
            TBT_PATH0_VALID;
    e[1] &= ~(TBT_PATH1_WEIGHT_MASK | TBT_PATH1_PRIO_MASK | TBT_PATH1_EFC |
              TBT_PATH1_ESE);
    e[1] |= (weight & TBT_PATH1_WEIGHT_MASK) |
            (TBT_PATH_PRIORITY_TUNNEL << TBT_PATH1_PRIO_SHIFT);
    if (lane || tbt3) {
        e[0] = (e[0] & ~TBT_PATH0_CREDITS_MASK) |
               ((credits << TBT_PATH0_CREDITS_SHIFT) & TBT_PATH0_CREDITS_MASK);
        e[1] = (e[1] & ~TBT_PATH1_ISE) | TBT_PATH1_IFC;
    }
    if (!lane) {
        e[1] |= TBT_PATH1_EFC;
    }
    if (asm7 && !lane && !tbt3) {
        e[0] = (e[0] & ~TBT_PATH0_CREDITS_MASK) |
               ((TBT_U4_ASM7_CREDITS << TBT_PATH0_CREDITS_SHIFT) &
                TBT_PATH0_CREDITS_MASK);
        e[1] = (e[1] & ~TBT_PATH1_ISE) | TBT_PATH1_IFC;
    }
    rc = tbtU4WrN(cm, r, TBT_CS_PATH, adp, 2 * hop, 2, e);
    if (asm7) {
        tbtU4Log(cm, label, e[0]);
        tbtU4Log(cm, label, e[1]);
        if (rc == TBT_OK &&
            tbtU4RdN(cm, r, TBT_CS_PATH, adp, 2 * hop, 2, back) == TBT_OK) {
            tbtU4Log(cm, label, back[0]);
            tbtU4Log(cm, label, back[1]);
        }
    }
    return rc;
}

/*
 * XhciCmAsmediaPcie7Credits's gate (record 22 section 18.8): set, a PCIe
 * tunnel, the host router 174C:2428 and a USB4 device router 174C:2463.
 * Logged once per tunnel built while the value is set: bit 0 applied.
 */
static BOOLEAN tbtU4Asm7(PTBT_U4CM cm, const TBT_U4_ROUTER *d, BOOLEAN usb3)
{
    BOOLEAN on;

    if (!cm->Config.AsmPcie7Credits) {
        return FALSE;
    }
    on = (BOOLEAN)(!usb3 && !d->Tbt3 &&
                   cm->HostRouter.Vendor == TBT_U4_ASM_VENDOR &&
                   cm->HostRouter.Device == TBT_U4_ASM_HOST &&
                   d->Vendor == TBT_U4_ASM_VENDOR &&
                   d->Device == TBT_U4_ASM_DEVICE);
    tbtU4Log(cm, "u4.asm7.gate", on ? 1UL : 0UL);
    return on;
}

/*
 * Record 22 section 6.2.2 item 4 (Spec 8.2.3.4): a path entry is written
 * in one Write Request of both its DWs, on every router - here PATH_CS_0
 * with Valid cleared and PATH_CS_1 as just read, its RsvdZ bit 29 written
 * 0 (Pending Packets, bit 28, is read only).
 */
static LONG tbtU4WrPath(PTBT_U4CM cm, const TBT_ROUTE *r, ULONG adp, ULONG hop,
                        const ULONG *e)
{
    ULONG w[2];

    w[0] = e[0] & ~TBT_PATH0_VALID;
    w[1] = e[1] & ~TBT_PATH1_RSVDZ29;
    return tbtU4WrN(cm, r, TBT_CS_PATH, adp, 2 * hop, 2, w);
}

/*
 * Guide 5.2.2 step 2 for one hop, at its ingress adapter: PATH_CS_0 Valid
 * cleared with no other field of the entry changed (written only when it
 * reads 1, both DWs in one write); PATH_CS_1 Pending Packets polled until the entry reads Valid 0
 * and Pending Packets 0, within TBT_U4_DRAIN_US (record 22 section 5.2);
 * then tTeardown (Spec Table 5-16, at least 2 us) before the entry may be
 * made valid again. TRUE when both read 0; any other ending quarantines
 * the entry (section 7.3). An entry whose ownership is not established
 * (section 7.7) is never written.
 */
static BOOLEAN tbtU4Invalidate(PTBT_U4CM cm, ULONG port, const TBT_ROUTE *r,
                               ULONG adp, ULONG hop)
{
    ULONG e[2];
    BOOLEAN drained;
    LONG rc;
    TBT_WAIT w;

    if (tbtU4Reserved(cm, r, adp, hop, FALSE)) {
        return FALSE;
    }
    drained = FALSE;
    e[0] = 0;
    rc = tbtU4RdN(cm, r, TBT_CS_PATH, adp, 2 * hop, 2, e);
    if (rc == TBT_OK && (e[0] & TBT_PATH0_VALID) != 0) {
        rc = tbtU4WrPath(cm, r, adp, hop, e);
    }
    if (rc == TBT_OK) {
        TbtWaitStart(&w, cm->Host, TBT_U4_DRAIN_US, FALSE);
        while (tbtU4RdN(cm, r, TBT_CS_PATH, adp, 2 * hop, 2, e) == TBT_OK) {
            if ((e[0] & TBT_PATH0_VALID) == 0 && (e[1] & TBT_PATH1_PP) == 0) {
                drained = TRUE;
                break;
            }
            if (TbtWaitDone(&w, cm->Host) || tbtU4Spent(cm)) {
                cm->Counters.PendingTimeouts++;
                break;
            }
            TbtWaitStep(&w, cm->Host, TBT_U4_POLL_STEP_US);
            if (cm->Budgeted) {
                cm->BudgetWaitedUs += TBT_U4_POLL_STEP_US + cm->Host->TickUs;
            }
        }
    }
    if (!drained) {
        tbtU4Quarantine(cm, port, r, adp, hop, e[0], FALSE, 0, 0);
        return FALSE;
    }
    TbtWaitMin(cm->Host, TBT_U4_TEARDOWN_GAP_US);
    return TRUE;
}

/* Path Enable cleared on a protocol adapter (with Valid kept 1 on a USB3
 * adapter, Guide 5.5.2 and 5.5.3), read back; a bit that will not clear
 * blocks the adapter (section 7.3). */
static BOOLEAN tbtU4Disable(PTBT_U4CM cm, ULONG port, const TBT_ROUTE *r,
                            ULONG adp, ULONG cap, BOOLEAN usb3)
{
    ULONG v;
    ULONG bit;
    LONG rc;

    bit = usb3 ? TBT_USB3_0_PE : TBT_PCIE0_PE;
    if (adp == 0 || cap == 0) {
        /* No protocol adapter, or none with its capability found (the
         * forced teardowns pass whatever the port records): offset 0 is
         * the adapter's basic configuration space, never written here,
         * and nothing is proven closed. */
        return FALSE;
    }
    if (tbtU4Reserved(cm, r, adp, 0, TRUE) ||
        tbtU4Reserved(cm, r, adp, TBT_HOP_PROTOCOL, FALSE)) {
        /* Its ownership, or that of the adapter's own protocol path, not
         * established (section 7.7): never written. */
        return FALSE;
    }
    rc = tbtU4Rd(cm, r, TBT_CS_ADAPTER, adp, cap, &v);
    if (rc == TBT_OK && (v & bit) != 0) {
        rc = tbtU4Wr(cm, r, TBT_CS_ADAPTER, adp, cap,
                     (v & ~bit) | (usb3 ? TBT_USB3_0_V : 0));
        if (rc == TBT_OK) {
            rc = tbtU4Rd(cm, r, TBT_CS_ADAPTER, adp, cap, &v);
        }
    }
    if (rc == TBT_OK && (v & bit) == 0) {
        return TRUE;
    }
    tbtU4Quarantine(cm, port, r, adp, 0, 0, TRUE, cap, bit);
    return FALSE;
}

/* The router a device-router quarantine entry belongs to is still the
 * one it was recorded on: the depth-1 router of the same connection, or a
 * discovered router below it that is still linked. */
static BOOLEAN tbtU4QOwnerPresent(PTBT_U4CM cm, const TBT_U4_QENTRY *q)
{
    PTBT_U4_PORT p;
    ULONG i;

    p = tbtU4PortOf(cm, q->Port);
    if (p == NULL) {
        return FALSE;
    }
    if (p->Dev.Present && TbtRouteEqual(&p->Dev.Route, &q->Route)) {
        return (BOOLEAN)(q->Generation == p->Generation);
    }
    /* A kid's own router: the kid's connection, still there. */
    for (i = 0; i < TBT_U4_MAX_KIDS; i++) {
        if (cm->Kids[i].Used && cm->Kids[i].Port == q->Port &&
            cm->Kids[i].Dev.Present &&
            TbtRouteEqual(&cm->Kids[i].Dev.Route, &q->Route)) {
            return (BOOLEAN)(q->Kid == 0 || q->Generation == cm->Kids[i].Generation);
        }
    }
    for (i = 0; i < TBT_U4_MAX_LINKS; i++) {
        if (cm->Links[i].Used && cm->Links[i].Port == q->Port &&
            TbtRouteEqual(&cm->Links[i].Child, &q->Route)) {
            return TRUE;
        }
    }
    return FALSE;
}

/* Drop every device-router entry of a port (its router left). */
static VOID tbtU4QDropPort(PTBT_U4CM cm, PTBT_U4_PORT p)
{
    ULONG i;

    for (i = 0; i < TBT_U4_QUARANTINE; i++) {
        if (cm->Quarantine[i].Used && cm->Quarantine[i].Port == p->Index &&
            TbtRouteDepth(&cm->Quarantine[i].Route) != 0) {
            cm->Quarantine[i].Used = FALSE;
        }
    }
}

/* Each idle tick retries what is quarantined; only a proven invalidation
 * and drain, or a Path Enable read back clear, lifts it (section 7.3).
 * An entry on a device router that is gone is dropped with the router. */
/* A held entry finally closed: whatever USB3 cleanup it was part of is
 * only now done, so the port's 500 ms teardown gap starts again (Guide
 * 5.5.1 step 1; conservative for an entry of another protocol). */
static VOID tbtU4QLifted(PTBT_U4CM cm, const TBT_U4_QENTRY *q)
{
    PTBT_U4_PORT p;

    if (q->Port == 0 || q->Port > cm->PortCount) {
        return;
    }
    p = &cm->Ports[q->Port - 1];
    p->LastUsb3TeardownUs = tbtU4Now(cm);
    p->Usb3GapValid = TRUE;
}

/* Record 34 section 6.11: a kid's record (keyed by a kid, or on a router
 * below depth 1 under a kid) waits, neither written nor dropped, while
 * the kid's parent is not classified (HELD by a resume before its check,
 * by a recovery, or barred) or holds another connection than the kid's:
 * the router there may not be the one the record was made on, and the
 * kid service settles which it is. A record on the child itself waits
 * too until the child reads as the same router now (its UUID, ROUTER_CS_7
 * and 8), and while an unplug hint of the kid is unresolved: behind a
 * Thunderbolt 3 parent no Lock tells a replacement, and the retry runs
 * before the kid's own check. The same holds for a record on a USB4
 * router a watched link names (a kid made a link again by a recovery
 * among them): retried only while that router reads its link's UUID. */
static BOOLEAN tbtU4KidQWait(PTBT_U4CM cm, const TBT_U4_QENTRY *q)
{
    PTBT_U4_PORT p;
    PTBT_U4_KID k;
    ULONG i;
    ULONG u[2];
    BOOLEAN kid;

    p = tbtU4PortOf(cm, q->Port);
    if (p == NULL) {
        return FALSE;
    }
    kid = (BOOLEAN)(q->Kid != 0);
    for (i = 0; i < TBT_U4_MAX_KIDS; i++) {
        if (cm->Kids[i].Used && cm->Kids[i].Port == q->Port &&
            (cm->Kids[i].Index == q->Kid ||
             (cm->Kids[i].Dev.Depth == 2 && tbtU4Under(&q->Route, &cm->Kids[i].Dev.Route)))) {
            kid = TRUE;
            k = &cm->Kids[i];
            if (k->ParentGeneration != p->Generation) {
                return TRUE;
            }
            if (k->Dev.Depth == 2 && tbtU4Under(&q->Route, &k->Dev.Route) &&
                (!k->Dev.Present || !k->Dev.UuidValid || k->UnplugPending ||
                 tbtU4RdN(cm, &k->Dev.Route, TBT_CS_ROUTER, 0, TBT_ROUTER_CS_7, 2, u) !=
                     TBT_OK ||
                 u[0] != k->Dev.Uuid[0] || u[1] != k->Dev.Uuid[1])) {
                return TRUE;
            }
        }
    }
    for (i = 0; i < TBT_U4_MAX_LINKS; i++) {
        if (cm->Links[i].Used && cm->Links[i].Port == q->Port && !cm->Links[i].ChildTbt3 &&
            TbtRouteEqual(&cm->Links[i].Child, &q->Route) &&
            (!cm->Links[i].ChildUuidValid ||
             tbtU4RdN(cm, &q->Route, TBT_CS_ROUTER, 0, TBT_ROUTER_CS_7, 2, u) != TBT_OK ||
             u[0] != cm->Links[i].ChildUuid[0] || u[1] != cm->Links[i].ChildUuid[1])) {
            return TRUE;
        }
    }
    return (BOOLEAN)(kid && p->State == TBT_U4_HELD &&
                     (p->ClassifyPending || p->Why == TBT_U4_WHY_RECOVER || p->Barred));
}

static VOID tbtU4RetryQuarantine(PTBT_U4CM cm)
{
    ULONG i;
    ULONG e[2];
    ULONG v;
    PTBT_U4_QENTRY q;
    PTBT_U4_ROUTER rt;
    LONG rc;

    for (i = 0; i < TBT_U4_QUARANTINE; i++) {
        q = &cm->Quarantine[i];
        if (!q->Used) {
            continue;
        }
        if (tbtU4KidQWait(cm, q)) {
            continue;
        }
        if (TbtRouteDepth(&q->Route) != 0 && !tbtU4QOwnerPresent(cm, q)) {
            /* Its router is gone, and its entries with it; a router that
             * replaced it at the same route is another connection. */
            q->Used = FALSE;
            continue;
        }
        if (q->Reserve) {
            /* Section 7.7: read only. An entry read closed is lifted, one
             * read as this driver wrote it is retried normally from the
             * next tick; an enable is retried once its adapter's protocol
             * entry reads closed. Anything else stays held, unwritten. */
            rc = tbtU4RdN(cm, &q->Route, TBT_CS_PATH, q->Adapter,
                          2 * (q->Enable ? TBT_HOP_PROTOCOL : q->Hop), 2, e);
            if (rc != TBT_OK) {
                continue;
            }
            if ((e[0] & TBT_PATH0_VALID) == 0 && (e[1] & TBT_PATH1_PP) == 0) {
                if (q->Enable) {
                    q->Reserve = FALSE;
                } else {
                    TbtWaitMin(cm->Host, TBT_U4_TEARDOWN_GAP_US);
                    q->Used = FALSE;
                    cm->Counters.QuarantineLifted++;
                    tbtU4QLifted(cm, q);
                }
            } else if (!q->Enable && (e[0] & TBT_PATH0_VALID) != 0 &&
                       (q->OutAdapter != 0 || q->OutHop != 0) &&
                       TBT_PATH0_OUT_ADP(e[0]) == q->OutAdapter &&
                       TBT_PATH0_OUT_HOP(e[0]) == q->OutHop) {
                q->Reserve = FALSE;
            }
            continue;
        }
        if (q->Enable &&
            tbtU4Reserved(cm, &q->Route, q->Adapter, TBT_HOP_PROTOCOL, FALSE)) {
            continue;   /* its adapter's protocol path held (section 7.7) */
        }
        if (q->Enable) {
            rc = tbtU4Rd(cm, &q->Route, TBT_CS_ADAPTER, q->Adapter, q->EnableCap, &v);
            if (rc == TBT_OK && (v & q->EnableBit) != 0) {
                rc = tbtU4Wr(cm, &q->Route, TBT_CS_ADAPTER, q->Adapter,
                             q->EnableCap, v & ~q->EnableBit);
                if (rc == TBT_OK) {
                    rc = tbtU4Rd(cm, &q->Route, TBT_CS_ADAPTER, q->Adapter,
                                 q->EnableCap, &v);
                }
            }
            if (rc == TBT_OK && (v & q->EnableBit) == 0) {
                q->Used = FALSE;
                cm->Counters.QuarantineLifted++;
                tbtU4QLifted(cm, q);
                if (q->EnableBit == TBT_PCIE0_PE) {
                    /* Its teardown's Extended Encapsulation clear, owed
                     * while the enable stood (section 6.2.2 item 7). */
                    rt = tbtU4RouterOf(cm, &q->Route);
                    if (rt != NULL && rt->Adp[q->Adapter].CapAdp == q->EnableCap) {
                        (void)tbtU4EeClear(cm, rt, q->Adapter);
                    }
                }
            }
            continue;
        }
        rc = tbtU4RdN(cm, &q->Route, TBT_CS_PATH, q->Adapter, 2 * q->Hop, 2, e);
        if (rc == TBT_OK && (e[0] & TBT_PATH0_VALID) != 0) {
            rc = tbtU4WrPath(cm, &q->Route, q->Adapter, q->Hop, e);
            if (rc == TBT_OK) {
                rc = tbtU4RdN(cm, &q->Route, TBT_CS_PATH, q->Adapter,
                              2 * q->Hop, 2, e);
            }
        }
        if (rc == TBT_OK && (e[0] & TBT_PATH0_VALID) == 0 &&
            (e[1] & TBT_PATH1_PP) == 0) {
            TbtWaitMin(cm->Host, TBT_U4_TEARDOWN_GAP_US);
            q->Used = FALSE;
            cm->Counters.QuarantineLifted++;
            tbtU4QLifted(cm, q);
            tbtU4Log(cm, "u4.quarantine-lifted", (q->Adapter << 16) | q->Hop);
        }
    }
}

static BOOLEAN tbtU4ProtoEmpty(PTBT_U4CM cm, const TBT_U4_ROUTER *rt, ULONG adp);

/* Whether a protocol adapter may carry a new tunnel: not blocked, its
 * HopID 8 entry not quarantined, and - independent of the quarantine
 * table, which can fill - its registers showing Path Enable clear and
 * HopID 8 neither Valid nor pending. */
static BOOLEAN tbtU4AdapterFree(PTBT_U4CM cm, const TBT_U4_ROUTER *rt, ULONG adp)
{
    return (BOOLEAN)(!tbtU4Quarantined(cm, &rt->Route, adp, 0, TRUE) &&
                     !tbtU4Quarantined(cm, &rt->Route, adp, TBT_HOP_PROTOCOL, FALSE) &&
                     tbtU4ProtoEmpty(cm, rt, adp));
}

/* ---------------------------------------------------------------------- */
/* Routers: read, enumerate, verify (record 22 sections 6.1, 6.4)         */
/* ---------------------------------------------------------------------- */

/* ROUTER_CS_0 to 4 into `rt`. */
static VOID tbtU4TakeHeader(PTBT_U4_ROUTER rt, const ULONG *v)
{
    rt->Vendor = TBT_RTR0_VENDOR(v[0]);
    rt->Device = TBT_RTR0_DEVICE(v[0]);
    rt->UpAdp = TBT_RTR1_UP_ADP(v[1]);
    rt->MaxAdp = TBT_RTR1_MAX_ADP(v[1]);
    rt->Depth = TBT_RTR1_DEPTH(v[1]);
    rt->Usb4Version = TBT_RTR4_VERSION(v[4]);
    rt->Tbt3 = (BOOLEAN)(TbtHwRouterVersion(rt->Usb4Version) == TBT_HW_ROUTER_TBT3);
    rt->TbtGen = rt->Tbt3 ? TbtHwTbtGeneration(rt->Vendor, rt->Device) : 0;
}

/* TopologyID Valid set, at `depth`, with the TopologyID its route gives. */
static BOOLEAN tbtU4Enumerated(const TBT_U4_ROUTER *rt, const ULONG *v)
{
    return (BOOLEAN)((v[3] & TBT_RTR3_V) != 0 &&
                     TBT_RTR1_DEPTH(v[1]) == rt->Depth &&
                     v[2] == rt->Route.Lo &&
                     (v[3] & TBT_RTR3_TOPOLOGY_MASK) ==
                     (rt->Route.Hi & TBT_RTR3_TOPOLOGY_MASK));
}

/*
 * Enumerate (Guide 3.2.1 step 4; u4cm.c enumerate_router): one write of
 * ROUTER_CS_1 to 4 - Depth, TopologyID with Valid, Notification Timeout
 * 255, and on a USB4 router CMUV by its version (record 22 section 6.2.1
 * item 2: 2.0 on a major 2 router while XhciCmUsb4V2 is 1, else 1.0) -
 * then, on a USB4 router, Router Ready within 500 ms (Guide 3.2.1 note).
 * `v` is ROUTER_CS_0 to 4 as read.
 */
static LONG tbtU4WaitReady(PTBT_U4CM cm, PTBT_U4_ROUTER rt);

static LONG tbtU4Enumerate(PTBT_U4CM cm, PTBT_U4_ROUTER rt, ULONG *v)
{
    ULONG w[4];
    LONG rc;

    w[0] = (v[1] & ~TBT_RTR1_DEPTH_MASK) |
           ((rt->Depth << TBT_RTR1_DEPTH_SHIFT) & TBT_RTR1_DEPTH_MASK);
    w[1] = rt->Route.Lo;
    w[2] = (rt->Route.Hi & TBT_RTR3_TOPOLOGY_MASK) | TBT_RTR3_V;
    w[3] = (v[4] & ~TBT_RTR4_NOTIFY_TIMEOUT_MASK) | TBT_RTR4_NOTIFY_TIMEOUT_MAX;
    if (!rt->Tbt3) {
        w[3] = (w[3] & ~TBT_RTR4_CMUV_MASK) |
               ((cm->Config.Usb4V2 && tbtU4IsV2(rt)) ? TBT_RTR4_CMUV_2_0 :
                                                       TBT_RTR4_CMUV_1_0);
        rt->Cmuv = TBT_RTR4_CMUV(w[3]);
    }
    /* Marked before the write: a write applied whose answer was lost is
     * still this driver's enumeration (Router Ready required). */
    rt->EnumeratedHere = TRUE;
    rc = tbtU4WrN(cm, &rt->Route, TBT_CS_ROUTER, 0, TBT_ROUTER_CS_1, 4, w);
    if (rc != TBT_OK) {
        return rc;
    }
    cm->Counters.RoutersEnumerated++;
    return tbtU4WaitReady(cm, rt);
}

/* Router Ready within 500 ms on a USB4 router (Guide 3.2.1 note). */
static LONG tbtU4WaitReady(PTBT_U4CM cm, PTBT_U4_ROUTER rt)
{
    ULONG cs6;
    LONG rc;
    TBT_WAIT t;

    if (rt->Tbt3) {
        return TBT_OK;
    }
    TbtWaitStart(&t, cm->Host, TBT_U4_ROUTER_READY_US, FALSE);
    for (;;) {
        rc = tbtU4Rd(cm, &rt->Route, TBT_CS_ROUTER, 0, TBT_ROUTER_CS_6, &cs6);
        if (rc != TBT_OK) {
            return rc;
        }
        if ((cs6 & TBT_RTR6_RR) != 0) {
            return TBT_OK;
        }
        if (TbtWaitDone(&t, cm->Host)) {
            tbtU4Log(cm, "u4.router-not-ready", cs6);
            return TBT_E_TIMEOUT;
        }
        tbtU4Pause(cm, &t);
    }
}

/*
 * A router found enumerated by someone else, ROUTER_CS_5 and 6 in cs56[0]
 * and [1]: a clear Router Ready may only be an enumeration moments old
 * (the ASM2464 sets it after Enumerated, [obs] usb4-emu
 * docs/captures/ORICO-ASM2464.md), so it is waited for as after an
 * enumeration of this driver's (Guide 3.2.1 note, 500 ms) and both
 * re-read. Reads only; on a timeout or a refused read cs56 stays as
 * found, and the caller's judgement with it.
 */
static VOID tbtU4FoundReady(PTBT_U4CM cm, PTBT_U4_ROUTER rt, PULONG cs56)
{
    ULONG v[2];

    if (rt->Tbt3 || (cs56[1] & TBT_RTR6_RR) != 0) {
        return;
    }
    if (tbtU4WaitReady(cm, rt) == TBT_OK &&
        tbtU4RdN(cm, &rt->Route, TBT_CS_ROUTER, 0, TBT_ROUTER_CS_5, 2, v) == TBT_OK) {
        cs56[0] = v[0];
        cs56[1] = v[1];
    }
}

/* ---------------------------------------------------------------------- */
/* The host router (record 22 sections 3.5 test (d), 6.1 to 6.3)          */
/* ---------------------------------------------------------------------- */

/* Get PCIe Downstream Entry Mapping (opcode 30h; Spec Tables 8-47, 8-48),
 * once per entry while the host answers: each entry's adapter is checked
 * against the pairing rule and its non-FPB Routing ID kept for the port
 * whose PCIe Down adapter it names (section 6.3). */
static VOID tbtU4MapPcie(PTBT_U4CM cm)
{
    ULONG meta;
    ULONG data[2];
    ULONG total;
    ULONG k;
    ULONG adp;
    ULONG i;
    LONG rc;
    PTBT_U4_PORT p;

    total = 1;
    for (k = 0; k < total && k < TBT_U4_MAX_ADP; k++) {
        meta = 0;
        rc = tbtU4RouterOp(cm, &cm->HostRouter, TBT_ROP_PCIE_DOWN_MAPPING, &meta,
                           NULL, 0, data, 2);
        if (rc != TBT_OK) {
            tbtU4Log(cm, "u4.map.none", (ULONG)rc);
            return;
        }
        total = TBT_ROP_MAP_TOTAL(meta);
        if ((data[0] & TBT_ROP_MAP_NATIVE) != 0) {
            continue;
        }
        adp = TBT_ROP_MAP_ADAPTER(data[0]);
        p = NULL;
        for (i = 0; i < cm->PortCount; i++) {
            if (cm->Ports[i].PcieDown == adp) {
                p = &cm->Ports[i];
            }
        }
        if (p == NULL) {
            tbtU4Log(cm, "u4.map.unpaired", adp);
            continue;
        }
        if (p->Index != TBT_ROP_MAP_INDEX(meta) + 1) {
            /* The entry order disagrees with the pairing; the pairing rule
             * wins and the routing id stays with its adapter. */
            tbtU4Log(cm, "u4.map.order", (p->Index << 8) | TBT_ROP_MAP_INDEX(meta));
        }
        p->MapValid = TRUE;
        p->MapRid = TBT_ROP_MAP_RID(data[1]);
    }
}

/*
 * A Thunderbolt host router's PCIe Down adapter for the port of lane 0
 * adapter `lane0` (record 22 section 19.4 item 4, [linux] tb.c
 * tb_find_pcie_down): by the port's physical index (lane0 - 1) / 2, 6 or
 * 7 on Cactus Ridge and Alpine Ridge, 6 or 8 on Falcon Ridge, 8 or 9 on
 * Titan Ridge, if that adapter is a PCIe Down adapter; 0 - the pairing in
 * increasing adapter number, as on a USB4 host - for Light Ridge, which
 * Linux maps to no fixed adapter (its first free one), and for an
 * adapter the map names that is not one (Linux's WARN_ON path).
 */
static ULONG tbtU4TbtPcieDown(const TBT_U4_ROUTER *h, ULONG lane0)
{
    ULONG phys;
    ULONG a;

    phys = lane0 >= 1 ? (lane0 - 1) / 2 : 0;
    if (phys > 1 || h->Vendor != 0x8086) {
        return 0;
    }
    switch (h->Device) {
    case 0x1547:    /* Cactus Ridge 4C */
    case 0x1548:
    case 0x1576:    /* Alpine Ridge */
    case 0x1578:
    case 0x15C0:
    case 0x15D3:
    case 0x15DA:
        a = phys == 0 ? 6 : 7;
        break;
    case 0x156B:    /* Falcon Ridge */
    case 0x156D:
        a = phys == 0 ? 6 : 8;
        break;
    case 0x15E7:    /* Titan Ridge */
    case 0x15EA:
    case 0x15EF:
        a = phys == 0 ? 8 : 9;
        break;
    default:
        return 0;
    }
    if (a > h->MaxAdp || a >= TBT_U4_MAX_ADP || !h->Adp[a].Present ||
        h->Adp[a].Type != TBT_ADP_TYPE_PCIE_DOWN || h->Adp[a].CapAdp == 0) {
        return 0;
    }
    return a;
}

/*
 * Test (d)'s router half and section 6.1: ROUTER_CS_0 to 4 at route 0
 * answered, USB4 Version 20h or 40h; enumerated if TopologyID Valid is
 * clear, else verified and left unwritten (Guide 3.2.1 note); adapters;
 * USB4 ports and their PCIe and USB3 Down adapters paired in increasing
 * adapter number (Spec 5.2.1, 5.2.5; usb4-emu u4cm.c discover_adapters);
 * the mapping operation, which checks the pairing where the router
 * answers it; Get Capabilities and the Buffer Allocation Request; quirks;
 * the host router's TMU.
 */
static LONG tbtU4HostRouter(PTBT_U4CM cm)
{
    PTBT_U4_ROUTER h;
    ULONG v[5];
    ULONG a;
    ULONG npcie;
    ULONG nusb3;
    ULONG t;
    ULONG i;
    LONG rc;
    PTBT_U4_PORT p;

    h = &cm->HostRouter;
    tbtU4Zero(h, sizeof(*h));
    h->Route = tbtU4Root;
    h->Depth = 0;
    h->CapsState = TBT_U4_CAPS_PENDING;
    rc = tbtU4RdN(cm, &h->Route, TBT_CS_ROUTER, 0, TBT_ROUTER_CS_0, 5, v);
    if (rc != TBT_OK) {
        cm->InertReason = TBT_INERT_HOST_ROUTER;
        return rc;
    }
    tbtU4TakeHeader(h, v);
    tbtU4Log(cm, "u4.host.id", (h->Vendor << 16) | h->Device);
    if (cm->Config.TbtHostRouter != 0) {
        /* Record 22 section 19.3 test (d): a Thunderbolt router (USB4
         * Version major 0), and the host router the bound NHI pairs with. */
        if (!h->Tbt3) {
            cm->InertReason = TBT_INERT_ROUTER_VERSION;
            tbtU4Log(cm, "u4.host.version", h->Usb4Version);
            return TBT_E_REFUSED;
        }
        if (h->Vendor != TBT_HW_VENDOR_INTEL || h->Device != cm->Config.TbtHostRouter) {
            cm->InertReason = TBT_INERT_HOST_ID;
            return TBT_E_REFUSED;
        }
        tbtU4Log(cm, "u4.tbt.host-gen", h->TbtGen);
    } else if (TbtHwRouterVersion(h->Usb4Version) != TBT_HW_ROUTER_USB4) {
        cm->InertReason = TBT_INERT_ROUTER_VERSION;
        tbtU4Log(cm, "u4.host.version", h->Usb4Version);
        return TBT_E_REFUSED;
    }
    h->Present = TRUE;
    if ((v[3] & TBT_RTR3_V) == 0) {
        rc = tbtU4Enumerate(cm, h, v);
        if (rc == TBT_E_TIMEOUT) {
            /* Linux does not wait for the host router's Router Ready
             * ([linux], record 22 section 6.1); this driver waits, and a
             * timeout is logged, not fatal. */
            rc = TBT_OK;
        }
        if (rc != TBT_OK) {
            cm->InertReason = TBT_INERT_HOST_ROUTER;
            return rc;
        }
    } else if (!tbtU4Enumerated(h, v)) {
        cm->InertReason = TBT_INERT_PROTOCOL;
        tbtU4Log(cm, "u4.host.topology", v[3]);
        return TBT_E_STATE;
    } else {
        h->Preserved = TRUE;
        cm->Counters.RoutersAdopted++;
        tbtU4CmuvFound(cm, h, v[4]);
    }
    rc = tbtU4DiscoverAdapters(cm, h);
    if (rc == TBT_OK) {
        rc = tbtU4RouterCaps(cm, h, TBT_RTR1_NEXT_CAP(v[1]));
    }
    if (rc != TBT_OK) {
        cm->InertReason = TBT_INERT_PROTOCOL;
        return rc;
    }

    cm->PortCount = 0;
    npcie = 0;
    nusb3 = 0;
    for (a = 1; a <= h->MaxAdp && a < TBT_U4_MAX_ADP; a++) {
        t = h->Adp[a].Type;
        if (!h->Adp[a].Present) {
            continue;
        }
        /* A Thunderbolt router's lane adapters have no USB4 Port
         * Capability: its ports are the lane pairs (a, a + 1) from adapter
         * 1, each lane adapter a null-type adapter with the lane (PHY)
         * capability (record 22 section 19.4 item 0; [linux] switch.c
         * tb_init_port, tb_switch_default_link_ports). */
        if (t == TBT_ADP_TYPE_LANE && h->Adp[a].CapLane != 0 && a != h->UpAdp &&
            (h->Tbt3 ? (a & 1UL) != 0 : h->Adp[a].CapPort != 0) &&
            cm->PortCount < TBT_U4_MAX_PORTS) {
            p = &cm->Ports[cm->PortCount];
            tbtU4Zero(p, sizeof(*p));
            p->Index = ++cm->PortCount;
            p->Lane0 = a;
        }
    }
    /* The Nth PCIe Down and the Nth USB3 Down adapter, in increasing
     * adapter number, belong to the Nth USB4 port; on a Thunderbolt host,
     * whose PCIe Down adapters are fixed per controller, the one of
     * tbtU4TbtPcieDown. */
    npcie = 0;
    nusb3 = 0;
    for (a = 1; a <= h->MaxAdp && a < TBT_U4_MAX_ADP; a++) {
        if (!h->Adp[a].Present || h->Adp[a].CapAdp == 0) {
            continue;
        }
        if (h->Adp[a].Type == TBT_ADP_TYPE_PCIE_DOWN) {
            if (npcie < cm->PortCount) {
                cm->Ports[npcie].PcieDown = a;
            }
            npcie++;
        } else if (h->Adp[a].Type == TBT_ADP_TYPE_USB3_DOWN) {
            if (nusb3 < cm->PortCount) {
                cm->Ports[nusb3].Usb3Down = a;
            }
            nusb3++;
        }
    }
    if (h->Tbt3) {
        /* The controller's fixed map first, every adapter it names
         * reserved; then a port it names none for (Light Ridge, or a map
         * entry that is not a PCIe Down adapter) takes the first PCIe Down
         * adapter no other port holds, or none. Never one adapter for two
         * ports. */
        for (i = 0; i < cm->PortCount; i++) {
            cm->Ports[i].PcieDown = tbtU4TbtPcieDown(h, cm->Ports[i].Lane0);
            cm->Ports[i].Usb3Down = 0;
        }
        for (i = 0; i < cm->PortCount; i++) {
            if (cm->Ports[i].PcieDown != 0) {
                continue;
            }
            for (a = 1; a <= h->MaxAdp && a < TBT_U4_MAX_ADP; a++) {
                if (!h->Adp[a].Present || h->Adp[a].CapAdp == 0 ||
                    h->Adp[a].Type != TBT_ADP_TYPE_PCIE_DOWN) {
                    continue;
                }
                for (t = 0; t < cm->PortCount; t++) {
                    if (cm->Ports[t].PcieDown == a) {
                        break;
                    }
                }
                if (t == cm->PortCount) {
                    cm->Ports[i].PcieDown = a;
                    break;
                }
            }
        }
    }
    for (i = 0; i < cm->PortCount; i++) {
        tbtU4Log(cm, "u4.host.port", (cm->Ports[i].Lane0 << 16) |
                 (cm->Ports[i].PcieDown << 8) | cm->Ports[i].Usb3Down);
    }
    if (cm->PortCount == 0) {
        cm->InertReason = TBT_INERT_PROTOCOL;
        tbtU4Log(cm, "u4.host.no-ports", h->MaxAdp);
        return TBT_E_STATE;
    }
    tbtU4ApplyQuirks(cm, h);
    if (h->Tbt3) {
        /* Section 19.4 item 1: no router operations, no Buffer Allocation;
         * the Plug Events active and the identity as on a Thunderbolt
         * device router (section 8 items 2 and 3, 19.4 item 3a). */
        h->CapsState = TBT_U4_CAPS_DEFAULT;
        if (tbtU4Tbt3Setup(cm, h) != TBT_OK) {
            cm->InertReason = TBT_INERT_PROTOCOL;
            tbtU4Log(cm, "u4.tbt.host-plug-events", h->CapPlugEvents);
            return TBT_E_STATE;
        }
        for (i = 0; i < cm->PortCount; i++) {
            cm->Ports[i].HostBaPcie = 0;
            cm->Ports[i].HostBaUsb3 = 0;
            cm->Ports[i].HostBaHi = 0;
        }
        return TBT_OK;
    }
    tbtU4MapPcie(cm);
    tbtU4GetCapabilities(cm, h);
    /* With Buffer Allocation Per USB4 Port enabled, one request per lane 0
     * adapter (Spec Table 8-56; Guide 5.1.4 step 3); else one for all. */
    if (h->CapsState == TBT_U4_CAPS_KNOWN &&
        (h->CapsEnabled & (1UL << TBT_CAP_BA_PER_PORT)) != 0) {
        for (i = 0; i < cm->PortCount; i++) {
            tbtU4BufferAllocation(cm, h, cm->Ports[i].Lane0);
            cm->Ports[i].HostBaPcie = h->BaMaxPcie;
            cm->Ports[i].HostBaUsb3 = h->BaMaxUsb3;
            cm->Ports[i].HostBaHi = h->BaMaxHi;
        }
    } else {
        tbtU4BufferAllocation(cm, h, 0);
        for (i = 0; i < cm->PortCount; i++) {
            cm->Ports[i].HostBaPcie = h->BaMaxPcie;
            cm->Ports[i].HostBaUsb3 = h->BaMaxUsb3;
            cm->Ports[i].HostBaHi = h->BaMaxHi;
        }
    }
    return TBT_OK;
}

/* The host router's Time Disruption (TMU_RTR_CS_0) cleared and read back
 * clear; only then is the disruption this driver set no longer owed. */
static BOOLEAN tbtU4HostTdClear(PTBT_U4CM cm)
{
    PTBT_U4_ROUTER h;
    ULONG at;
    ULONG cs0;

    h = &cm->HostRouter;
    at = h->CapTmu + TBT_TMU_RTR_CS_0;
    if (tbtU4Rmw(cm, &h->Route, TBT_CS_ROUTER, 0, at, TBT_TMU_RTR0_TD, 0) != TBT_OK ||
        tbtU4Rd(cm, &h->Route, TBT_CS_ROUTER, 0, at, &cs0) != TBT_OK) {
        return (BOOLEAN)!cm->HostTmuTdOwed;
    }
    if ((cs0 & TBT_TMU_RTR0_TD) == 0) {
        cm->HostTmuTdOwed = FALSE;
    }
    return (BOOLEAN)!cm->HostTmuTdOwed;
}

/*
 * The host router's own TMU, once at start (record 22 section 6.6 item 4,
 * [linux]): TMU_RTR_CS_3 TSPacketInterval set to Table 7-2's HiFi value,
 * the rate alone, inside a time disruption of the host router's own
 * (TMU_RTR_CS_0 Time Disruption set, then cleared, as Guide 7.4.3.4 steps
 * 2 and 7 bracket a mode change). An interval already set is left as
 * found; Time Synchronization Protocol Not Supported (Guide 7.4) means no
 * TMU to set up, as does no TMU capability. A disruption this driver set
 * and could not clear is cleared first: until it is, the TMU is not
 * usable and no USB3 tunnel is built (section 6.9); the idle tick retries.
 */
static VOID tbtU4HostTmu(PTBT_U4CM cm)
{
    PTBT_U4_ROUTER h;
    ULONG cs0;
    ULONG cs3;
    LONG rc;

    h = &cm->HostRouter;
    if (h->CapTmu == 0 || h->Tbt3) {
        /* A Thunderbolt host router's TMU is not configured (record 22
         * section 19.4 item 6, as section 8 item 6). */
        cm->HostTmuOk = TRUE;
        return;
    }
    cm->HostTmuOk = FALSE;
    if (cm->HostTmuTdOwed && !tbtU4HostTdClear(cm)) {
        return;
    }
    if (tbtU4Rd(cm, &h->Route, TBT_CS_ROUTER, 0, h->CapTmu + TBT_TMU_RTR_CS_0, &cs0) != TBT_OK) {
        return;
    }
    if ((cs0 & TBT_TMU_RTR0_TSP_NS) == 0) {
        if (tbtU4Rd(cm, &h->Route, TBT_CS_ROUTER, 0, h->CapTmu + TBT_TMU_RTR_CS_3,
                    &cs3) != TBT_OK) {
            return;
        }
        if ((cs3 & TBT_TMU_RTR3_INTERVAL_MASK) == 0) {
            /* Owed from before the write, whose answer may be lost. */
            cm->HostTmuTdOwed = TRUE;
            rc = tbtU4Wr(cm, &h->Route, TBT_CS_ROUTER, 0, h->CapTmu + TBT_TMU_RTR_CS_0,
                         cs0 | TBT_TMU_RTR0_TD);
            if (rc == TBT_OK) {
                cs3 &= ~TBT_TMU_RTR3_INTERVAL_MASK;
                cs3 |= TBT_TMU_INTERVAL_HIFI << TBT_TMU_RTR3_INTERVAL_SHIFT;
                rc = tbtU4Wr(cm, &h->Route, TBT_CS_ROUTER, 0, h->CapTmu + TBT_TMU_RTR_CS_3,
                             cs3);
            }
            (void)tbtU4HostTdClear(cm);
            cm->HostTmuOk = (BOOLEAN)(rc == TBT_OK && !cm->HostTmuTdOwed);
            tbtU4Log(cm, "u4.host.tmu", (ULONG)rc);
            return;
        }
    }
    cm->HostTmuOk = TRUE;
}

/* ---------------------------------------------------------------------- */
/* Thunderbolt 3 device routers (record 22 section 8)                     */
/* ---------------------------------------------------------------------- */

/* The Alpine Ridge bridges, by their router ids: the routers that keep
 * the USB plug event (record 22 section 8 item 2, [linux]). */
static BOOLEAN tbtU4AlpineRidge(const TBT_U4_ROUTER *rt)
{
    if (rt->Vendor != 0x8086) {
        return FALSE;
    }
    switch (rt->Device) {
    case 0x1576:
    case 0x1578:
    case 0x15C0:
    case 0x15D3:
    case 0x15DA:
        return TRUE;
    }
    return FALSE;
}

/* Light Ridge, Eagle Ridge and Port Ridge keep the USB plug event too
 * ([linux] switch.c tb_plug_events_active, record 22 section 19.4 item 1). */
static BOOLEAN tbtU4UsbEventKept(const TBT_U4_ROUTER *rt)
{
    return (BOOLEAN)(rt->Vendor == 0x8086 &&
                     (rt->Device == 0x1513 || rt->Device == 0x151A ||
                      rt->Device == 0x1549));
}

/* An adapter's USB4 Port Region in the Vendor Specific Extended 6
 * Capability (record 22 section 8 item 3; Spec 13.6.1.5, Table 13-18):
 * the regions follow the Common Region, whose CAP_STRUCT gives the Common
 * Region Length (bits 15:8, counted from the capability's DW 0) and the
 * USB4 Port Region Length (bits 27:16); one region per USB4 port, the two
 * lane adapters of a port (1 and 2, 3 and 4, ...) sharing it. 0 when
 * there is none. */
static ULONG tbtU4LcPort(PTBT_U4CM cm, const TBT_U4_ROUTER *rt, ULONG adp)
{
    ULONG desc;
    ULONG port;

    if (rt->CapLc == 0 || adp == 0) {
        return 0;
    }
    if (tbtU4Rd(cm, &rt->Route, TBT_CS_ROUTER, 0, rt->CapLc + TBT_LC_DESC, &desc) != TBT_OK) {
        return 0;
    }
    port = (adp - 1) / 2;
    return rt->CapLc + TBT_LC_DESC_SIZE(desc) + port * TBT_LC_DESC_PORT_SIZE(desc);
}

/* tbtU4LcPort for a router's own marks (the host router's, a kid's
 * Thunderbolt 3 parent's): TBT_OK with *region 0 when it has no link
 * controller (nothing to mark), an error when it has one whose descriptor
 * cannot be read - a mark then never counts as made nor as undone. */
static LONG tbtU4RouterLcPort(PTBT_U4CM cm, const TBT_U4_ROUTER *rt, ULONG adp,
                              PULONG region)
{
    *region = 0;
    if (rt->CapLc == 0) {
        return TBT_OK;
    }
    *region = tbtU4LcPort(cm, rt, adp);
    return *region != 0 ? TBT_OK : TBT_E_STATE;
}

static LONG tbtU4HostLcPort(PTBT_U4CM cm, ULONG adp, PULONG region)
{
    return tbtU4RouterLcPort(cm, &cm->HostRouter, adp, region);
}

/*
 * A Thunderbolt router's identity, chosen by what it has (record 22
 * section 19.4 item 3a): its link controller's fused UUID, four DWs from
 * DW 3 of the capability; with no link controller capability (generation
 * 1), the DROM's UID alone through the EEPROM - CRC-8 over bytes 1 to 8 in
 * byte 0 - as two DWs then two of all ones ([linux] eeprom.c
 * tb_drom_read_uid_only, switch.c tb_switch_set_uuid). A read that fails
 * where the capability exists is a failed identity, never a reason to fall
 * back to the DROM. The source is the router's UidFromDrom.
 */
static UCHAR tbtU4Crc8(const UCHAR *p, ULONG n)
{
    UCHAR c;
    ULONG b;

    c = 0xFF;
    while (n-- != 0) {
        c = (UCHAR)(c ^ *p++);
        for (b = 0; b < 8; b++) {
            c = (UCHAR)((c & 0x80) != 0 ? (UCHAR)((c << 1) ^ 0x07) : (UCHAR)(c << 1));
        }
    }
    return c;
}

static LONG tbtU4TbtUuid(PTBT_U4CM cm, PTBT_U4_ROUTER rt, PULONG u)
{
    UCHAR b[9];
    ULONG i;
    LONG rc;

    if (!rt->UidFromDrom) {
        if (rt->CapLc == 0) {
            return TBT_E_UNSUPPORTED;
        }
        return tbtU4RdN(cm, &rt->Route, TBT_CS_ROUTER, 0, rt->CapLc + TBT_LC_FUSE, 4, u);
    }
    /* Under a resume budget too: the read stops when it is spent, and its
     * release is not refused (tbtU4EeRelease). */
    rc = tbtU4EepromRead(cm, rt, 0, 9, b);
    if (rc != TBT_OK) {
        return rc;
    }
    if (tbtU4Crc8(b + 1, 8) != b[0]) {
        tbtU4Log(cm, "u4.tbt.uid-crc", b[0]);
        return TBT_E_STATE;
    }
    u[0] = 0;
    u[1] = 0;
    for (i = 0; i < 4; i++) {
        u[0] |= (ULONG)b[1 + i] << (8 * i);
        u[1] |= (ULONG)b[5 + i] << (8 * i);
    }
    u[2] = 0xFFFFFFFFUL;
    u[3] = 0xFFFFFFFFUL;
    return TBT_OK;
}

/*
 * A Thunderbolt 3 router's setup behind a USB4 host (record 22 section 8
 * items 2 and 4): VSC_CS_1 of its Vendor Specific 1 Capability (at router
 * DW 28h, Spec 13.6.1.2) read and written back with Plug Event Disable
 * (bits 6:3, Table 13-15) cleared, so every adapter type reports its hot
 * plugs, and bit 2, a TBT3-Compatible bit, set - the USB plug event off -
 * except on an Alpine Ridge bridge ([linux]); then the router's fused
 * UUID, four DWs of its Vendor Specific Extended 6 Capability from DW 3
 * ([linux]; read the same way from the host router, ICM-PROTOCOL.md 5.1
 * [obs]). No Vendor Specific 1 Capability: refused.
 */
static LONG tbtU4Tbt3Setup(PTBT_U4CM cm, PTBT_U4_ROUTER rt)
{
    ULONG at;
    ULONG pe;
    LONG rc;

    if (rt->CapPlugEvents == 0) {
        return TBT_E_STATE;
    }
    at = rt->CapPlugEvents + TBT_PLUG_EVENTS_CS_1;
    rc = tbtU4Rd(cm, &rt->Route, TBT_CS_ROUTER, 0, at, &pe);
    if (rc == TBT_OK) {
        pe &= ~TBT_PLUG_EVENTS_MASK;
        if (!tbtU4AlpineRidge(rt) && !tbtU4UsbEventKept(rt)) {
            pe |= TBT_PLUG_EVENTS_USB_DISABLE;
        }
        rc = tbtU4Wr(cm, &rt->Route, TBT_CS_ROUTER, 0, at, pe);
    }
    if (rc != TBT_OK) {
        return rc;
    }
    rt->UidFromDrom = (BOOLEAN)(rt->CapLc == 0 && rt->TbtGen == 1);
    rt->UuidValid = (BOOLEAN)(tbtU4TbtUuid(cm, rt, rt->Uuid) == TBT_OK);
    if (!rt->UuidValid) {
        tbtU4Log(cm, "u4.tbt.noid", (rt->Route.Lo << 8) | rt->TbtGen);
    }
    return TBT_OK;
}

/* ---------------------------------------------------------------------- */
/* Lanes and bonding (record 22 section 6.5)                              */
/* ---------------------------------------------------------------------- */

/* A host lane 0 read is kept for the query device (TBT_U4_PORT.LinkSeen):
 * the reads the sequences make anyway, no read of its own. */
static LONG tbtU4LaneRd(PTBT_U4CM cm, const TBT_U4_ROUTER *rt, ULONG adp,
                        PULONG v)
{
    LONG rc;
    ULONG i;
    PTBT_U4_PORT p;

    if (adp == 0 || adp >= TBT_U4_MAX_ADP || rt->Adp[adp].CapLane == 0) {
        return TBT_E_STATE;
    }
    rc = tbtU4Rd(cm, &rt->Route, TBT_CS_ADAPTER, adp,
                 rt->Adp[adp].CapLane + TBT_LANE_CS_1, v);
    if (rc == TBT_OK && rt == &cm->HostRouter) {
        for (i = 0; i < cm->PortCount && i < TBT_U4_MAX_PORTS; i++) {
            p = &cm->Ports[i];
            if (p->Lane0 == adp) {
                p->LinkSeen = TRUE;
                p->LinkLane1 = *v;
                p->LinkGeneration = p->Generation;
                break;
            }
        }
    }
    return rc;
}

static LONG tbtU4LaneRmw(PTBT_U4CM cm, const TBT_U4_ROUTER *rt, ULONG adp,
                         ULONG clr, ULONG set)
{
    if (adp == 0 || adp >= TBT_U4_MAX_ADP || rt->Adp[adp].CapLane == 0) {
        return TBT_E_STATE;
    }
    return tbtU4Rmw(cm, &rt->Route, TBT_CS_ADAPTER, adp,
                    rt->Adp[adp].CapLane + TBT_LANE_CS_1, clr, set);
}

/* Wait for an adapter's Lane state to satisfy `want` (a class) within
 * `us`; FALSE on a timeout, a read failing, or the port's generation
 * moving on. */
static BOOLEAN tbtU4WaitLane(PTBT_U4CM cm, PTBT_U4_PORT p, ULONG gen,
                             const TBT_U4_ROUTER *rt, ULONG adp, ULONG state,
                             ULONG us)
{
    ULONG v;
    TBT_WAIT w;

    TbtWaitStart(&w, cm->Host, us, FALSE);
    for (;;) {
        if (tbtU4LaneRd(cm, rt, adp, &v) != TBT_OK) {
            return FALSE;
        }
        if (TBT_LANE1_STATE(v) == state) {
            return TRUE;
        }
        if (TbtWaitDone(&w, cm->Host) || !TBT_U4_LIVE(p, gen)) {
            return FALSE;
        }
        tbtU4Pause(cm, &w);
    }
}

/* Section 6.6's TMU-Off predicate of the port's link (also section 6.5
 * step 6.3): the downstream router's TSPacketInterval 0, UDM 0 on both
 * lane 0 adapters, and on each USB4 version 2 router its lane 0 adapter's
 * EUDM and AdapterTimeSyncInterval 0. A link without TMU capabilities on
 * either end has no TMU running. A reading that cannot be taken is not
 * Off. */
/* The same for any link (the link view, design record 34 section 6.2):
 * the upstream router `h` at its lane 0 adapter `lane0`, the downstream
 * router `d` at its upstream adapter. */
static BOOLEAN tbtU4LinkTmuOff(PTBT_U4CM cm, const TBT_U4_ROUTER *h, ULONG lane0,
                               const TBT_U4_ROUTER *d)
{
    const TBT_U4_ROUTER *rt;
    ULONG adp;
    ULONG v;
    ULONG k;

    if (d->CapTmu == 0 || h->Adp[lane0].CapTmu == 0 ||
        d->Adp[d->UpAdp].CapTmu == 0) {
        return TRUE;
    }
    if (tbtU4Rd(cm, &d->Route, TBT_CS_ROUTER, 0, d->CapTmu + TBT_TMU_RTR_CS_3, &v) != TBT_OK ||
        (v & TBT_TMU_RTR3_INTERVAL_MASK) != 0) {
        return FALSE;
    }
    for (k = 0; k < 2; k++) {
        rt = k == 0 ? h : d;
        adp = k == 0 ? lane0 : d->UpAdp;
        if (tbtU4Rd(cm, &rt->Route, TBT_CS_ADAPTER, adp,
                    rt->Adp[adp].CapTmu + TBT_TMU_ADP_CS_3, &v) != TBT_OK ||
            (v & TBT_TMU_ADP3_UDM) != 0) {
            return FALSE;
        }
        if (TBT_USB4_VERSION_MAJOR(rt->Usb4Version) == TBT_USB4_MAJOR_2) {
            if (tbtU4Rd(cm, &rt->Route, TBT_CS_ADAPTER, adp,
                        rt->Adp[adp].CapTmu + TBT_TMU_ADP_CS_8, &v) != TBT_OK ||
                (v & TBT_TMU_ADP8_EUDM) != 0) {
                return FALSE;
            }
            if (tbtU4Rd(cm, &rt->Route, TBT_CS_ADAPTER, adp,
                        rt->Adp[adp].CapTmu + TBT_TMU_ADP_CS_9, &v) != TBT_OK ||
                (v & TBT_TMU_ADP9_INTERVAL_MASK) != 0) {
                return FALSE;
            }
        }
    }
    return TRUE;
}

static BOOLEAN tbtU4TmuOff(PTBT_U4CM cm, PTBT_U4_PORT p)
{
    return tbtU4LinkTmuOff(cm, &cm->HostRouter, p->Lane0, &p->Dev);
}

/*
 * The link marked configured once it may carry paths (record 22 section
 * 6.5): USB4 Port is Configured (PORT_CS_19 bit 3, Spec 8.2.2.4) on
 * the host's lane 0 adapter, then on the device router's upstream one.
 * A Thunderbolt 3 router has no PC there; its USB4 Port Region's
 * LC_SX_CTRL (Spec 13.6.1.5, Table 13-19) takes Lane 0 Configured (bit
 * 16) for an odd upstream adapter, Lane 1 Configured (bit 20) for an
 * even one, with bit 30, a TBT3-Compatible bit, set as well ([linux],
 * record 22 section 8 item 3). The host's mark is owned before its
 * write, the device's once its write succeeded.
 */
static LONG tbtU4Configured(PTBT_U4CM cm, PTBT_U4_PORT p)
{
    PTBT_U4_ROUTER h;
    PTBT_U4_ROUTER d;
    ULONG region;
    ULONG mark;
    LONG rc;

    h = &cm->HostRouter;
    d = &p->Dev;
    p->PcSetHost = TRUE;
    if (h->Tbt3) {
        /* A Thunderbolt host router marks its downstream port in its link
         * controller from generation 2 ([linux] lc.c tb_lc_configure_port,
         * record 22 section 19.4 item 0); with none, nothing to mark. */
        rc = tbtU4HostLcPort(cm, p->Lane0, &region);
        if (rc == TBT_OK && region != 0) {
            mark = (p->Lane0 & 1UL) != 0 ? TBT_LC_SX_CTRL_L1C : TBT_LC_SX_CTRL_L2C;
            rc = tbtU4Rmw(cm, &h->Route, TBT_CS_ROUTER, 0, region + TBT_LC_SX_CTRL, 0, mark);
        } else if (rc == TBT_OK) {
            p->PcSetHost = FALSE;
        }
    } else {
        rc = tbtU4Rmw(cm, &h->Route, TBT_CS_ADAPTER, p->Lane0,
                      h->Adp[p->Lane0].CapPort + TBT_PORT_CS_19, 0, TBT_PORT19_PC);
    }
    if (rc != TBT_OK) {
        return rc;
    }
    if (d->Tbt3) {
        /* A link controller whose descriptor does not read fails the
         * step: the link is never taken as configured unmarked. */
        rc = tbtU4RouterLcPort(cm, d, d->UpAdp, &region);
        if (rc == TBT_OK && region != 0) {
            mark = (d->UpAdp & 1UL) != 0 ? TBT_LC_SX_CTRL_L1C : TBT_LC_SX_CTRL_L2C;
            rc = tbtU4Rmw(cm, &d->Route, TBT_CS_ROUTER, 0, region + TBT_LC_SX_CTRL, 0,
                          mark | TBT_LC_SX_CTRL_UPSTREAM);
        }
    } else if (d->Adp[d->UpAdp].CapPort != 0) {
        rc = tbtU4Rmw(cm, &d->Route, TBT_CS_ADAPTER, d->UpAdp,
                      d->Adp[d->UpAdp].CapPort + TBT_PORT_CS_19, 0, TBT_PORT19_PC);
    }
    if (rc == TBT_OK) {
        p->PcSetDev = TRUE;
    }
    return rc;
}

/* The four lane adapters of a port's link: the host's lane 0 and lane 1,
 * then the device router's (its upstream adapter and the one above). */
typedef struct _TBT_U4_LANES {
    const TBT_U4_ROUTER *Rt[4];
    ULONG Adp[4];
} TBT_U4_LANES;

/*
 * Whether the link can bond at all: the device's upstream port reports
 * Bonding Enabled (PORT_CS_18 bit 8, Spec 8.2.2.4, 4.1.2.3; on a
 * Thunderbolt 3 router its USB4 Port Region's PORT_ATTR bit 12, Spec
 * 13.6.1.5), and both lane 0 adapters, the host's first, list x2 among
 * their Supported Link Widths (LANE_ADP_CS_0 bit 21, Spec 8.2.2.3).
 */
static BOOLEAN tbtU4BondPossible(PTBT_U4CM cm, PTBT_U4_PORT p, const TBT_U4_LANES *ln)
{
    PTBT_U4_ROUTER d;
    ULONG region;
    ULONG v;
    ULONG k;

    d = &p->Dev;
    if (d->Tbt3) {
        region = tbtU4LcPort(cm, d, ln->Adp[2]);
        if (region == 0 ||
            tbtU4Rd(cm, &d->Route, TBT_CS_ROUTER, 0, region + TBT_LC_PORT_ATTR, &v) != TBT_OK ||
            (v & TBT_LC_PORT_ATTR_BE) == 0) {
            return FALSE;
        }
    } else if (d->Adp[ln->Adp[2]].CapPort == 0 ||
               tbtU4Rd(cm, &d->Route, TBT_CS_ADAPTER, ln->Adp[2],
                       d->Adp[ln->Adp[2]].CapPort + TBT_PORT_CS_18, &v) != TBT_OK ||
               (v & TBT_PORT18_BE) == 0) {
        return FALSE;
    }
    for (k = 0; k < 4; k += 2) {
        if (ln->Rt[k]->Adp[ln->Adp[k]].CapLane == 0 ||
            tbtU4Rd(cm, &ln->Rt[k]->Route, TBT_CS_ADAPTER, ln->Adp[k],
                    ln->Rt[k]->Adp[ln->Adp[k]].CapLane + TBT_LANE_CS_0, &v) != TBT_OK ||
            (v & TBT_LANE0_SLW_X2) == 0) {
            return FALSE;
        }
    }
    return TRUE;
}

/*
 * Guide 7.1, the bonding of a Gen 2 or Gen 3 link: (1) both lane 1
 * adapters in CL0 (lane 0 carries the link already), within
 * TBT_U4_LANE1_CL0_US (record 22 section 5.2, [linux]); (3) Target Link
 * Width dual on all four lane adapters, each one's value read and kept
 * first so section 6.5 step 6.1 can put it back; (4) Lane Bonding set on
 * the host's lane 0; (5) Negotiated Link Width polled on the host's lane
 * 0 until it reads x2, within TBT_U4_BONDING_US (record 22 section 5.2:
 * a software bound well above the Guide's tBonding). The lane 1
 * hot unplug the bonding produces is acknowledged by the transport and
 * only counted (section 7.1). `nlw` holds the last reading of the host's
 * lane 0 (logged at a timeout when none was taken since). TBT_OK bonded, TBT_E_GONE when the port's
 * generation moved on, TBT_E_STATE for the fallback.
 */
static LONG tbtU4BondTry(PTBT_U4CM cm, PTBT_U4_PORT p, ULONG gen, const TBT_U4_LANES *ln,
                         ULONG nlw)
{
    ULONG k;
    TBT_WAIT w;

    if (!tbtU4BondPossible(cm, p, ln)) {
        tbtU4Log(cm, "u4.bond.impossible", p->Index);
        return TBT_E_STATE;
    }
    if (!tbtU4WaitLane(cm, p, gen, ln->Rt[1], ln->Adp[1], TBT_LANE_STATE_CL0,
                       TBT_U4_LANE1_CL0_US) ||
        !tbtU4WaitLane(cm, p, gen, ln->Rt[3], ln->Adp[3], TBT_LANE_STATE_CL0,
                       TBT_U4_LANE1_CL0_US)) {
        if (!TBT_U4_LIVE(p, gen)) {
            return TBT_E_GONE;
        }
        tbtU4Log(cm, "u4.bond.lane1", p->Index);
        return TBT_E_STATE;
    }
    for (k = 0; k < 4; k++) {
        if (tbtU4LaneRd(cm, ln->Rt[k], ln->Adp[k], &p->LaneSaved[k]) != TBT_OK) {
            return TBT_E_STATE;
        }
    }
    p->LaneWritten = TRUE;      /* owned before the writes */
    for (k = 0; k < 4; k++) {
        if (tbtU4LaneRmw(cm, ln->Rt[k], ln->Adp[k], TBT_LANE1_TLW_MASK,
                         TBT_LANE1_TLW_DUAL) != TBT_OK) {
            return TBT_E_STATE;
        }
    }
    if (tbtU4LaneRmw(cm, ln->Rt[0], ln->Adp[0], 0, TBT_LANE1_LB) != TBT_OK) {
        return TBT_E_STATE;
    }
    TbtWaitStart(&w, cm->Host, TBT_U4_BONDING_US, FALSE);
    for (;;) {
        if (tbtU4LaneRd(cm, ln->Rt[0], ln->Adp[0], &nlw) == TBT_OK &&
            TBT_LANE1_NLW(nlw) == TBT_LANE_WIDTH_X2) {
            return TBT_OK;
        }
        if (!TBT_U4_LIVE(p, gen)) {
            return TBT_E_GONE;
        }
        if (TbtWaitDone(&w, cm->Host)) {
            tbtU4Log(cm, "u4.bond.timeout", nlw);
            return TBT_E_STATE;
        }
        tbtU4Pause(cm, &w);
    }
}

/*
 * Section 6.5 step 6: no bond, so a single-lane link, accepted only when
 * verified. 6.1: every field the bonding wrote back to what it read. 6.2:
 * both lane 1 adapters in CLd or Disabled: single already. 6.3: else the
 * host's lane adapters at their defaults (Target Link Width single, no
 * Lane Bonding, no CLx enabled) and the link's TMU Off, as Guide 7.5 asks
 * before a lane is disabled (not on a Thunderbolt host, which disables
 * none). 6.4: two single-lane links - lane 1 in CL0
 * and lane 0 x1 - so the host's lane 1 disabled (Guide 7.5: Lane Disable)
 * and read back Disabled within TBT_U4_LANE_DISABLE_US. TBT_OK single,
 * TBT_E_GONE, or TBT_E_STATE with the port's reason set.
 */
static LONG tbtU4BondSingle(PTBT_U4CM cm, PTBT_U4_PORT p, ULONG gen, const TBT_U4_LANES *ln)
{
    ULONG lane[3];      /* host lane 0, host lane 1, device lane 1 */
    ULONG k;
    ULONG st;

    if (!TBT_U4_LIVE(p, gen)) {
        return TBT_E_GONE;
    }
    if (p->LaneWritten) {
        for (k = 0; k < 4; k++) {
            if (tbtU4LaneRmw(cm, ln->Rt[k], ln->Adp[k], TBT_LANE1_TLW_MASK | TBT_LANE1_LB,
                             p->LaneSaved[k] & (TBT_LANE1_TLW_MASK | TBT_LANE1_LB)) != TBT_OK) {
                goto failed;
            }
        }
        p->LaneWritten = FALSE;
    }
    if (tbtU4LaneRd(cm, ln->Rt[1], ln->Adp[1], &lane[1]) != TBT_OK ||
        tbtU4LaneRd(cm, ln->Rt[3], ln->Adp[3], &lane[2]) != TBT_OK) {
        goto failed;
    }
    for (k = 1; k <= 2; k++) {
        st = TBT_LANE1_STATE(lane[k]);
        if (st != TBT_LANE_STATE_CLD && st != TBT_LANE_STATE_DISABLED) {
            break;
        }
    }
    (void)st;
    if (k > 2) {
        cm->Counters.BondSingle++;
        p->SingleLane = TRUE;
        return TBT_OK;
    }
    if (tbtU4LaneRd(cm, ln->Rt[0], ln->Adp[0], &lane[0]) != TBT_OK) {
        goto failed;
    }
    for (k = 0; k < 2; k++) {
        if ((lane[k] & TBT_LANE1_TLW_MASK) != TBT_LANE1_TLW_SINGLE ||
            (lane[k] & (TBT_LANE1_LB | TBT_LANE1_CLX_EN)) != 0) {
            tbtU4Log(cm, "u4.bond.not-default", (k << 31) | lane[k]);
            goto failed;
        }
    }
    /* The TMU Off that Guide 7.5 asks for comes before a Lane Disable,
     * which a Thunderbolt host never writes (below): there the TMU is left
     * as it is (record 22 section 19.4 item 6), whatever mode the routers
     * came up in. */
    if (!cm->HostRouter.Tbt3 && !tbtU4TmuOff(cm, p)) {
        tbtU4Log(cm, "u4.bond.tmu-not-off", p->Index);
        goto failed;
    }
    if (TBT_LANE1_STATE(lane[1]) != TBT_LANE_STATE_CL0 ||
        TBT_LANE1_NLW(lane[0]) != TBT_LANE_WIDTH_X1) {
        goto failed;
    }
    if (cm->HostRouter.Tbt3) {
        /* A Thunderbolt host's link that does not bond stays two single
         * lanes, as Linux leaves it: no Lane Disable there, which is a
         * USB4 lane adapter's (record 22 section 19.4 item 0). */
        cm->Counters.BondSingle++;
        p->SingleLane = TRUE;
        return TBT_OK;
    }
    p->Lane1Disabled = TRUE;    /* owned before the write */
    if (tbtU4LaneRmw(cm, ln->Rt[1], ln->Adp[1], 0, TBT_LANE1_LD) != TBT_OK) {
        goto failed;
    }
    if (!tbtU4WaitLane(cm, p, gen, ln->Rt[1], ln->Adp[1], TBT_LANE_STATE_DISABLED,
                       TBT_U4_LANE_DISABLE_US)) {
        if (!TBT_U4_LIVE(p, gen)) {
            return TBT_E_GONE;
        }
        goto failed;
    }
    cm->Counters.Lane1Disabled++;
    cm->Counters.BondSingle++;
    p->SingleLane = TRUE;
    return TBT_OK;

failed:
    p->Why = TBT_U4_WHY_BONDING;
    return TBT_E_STATE;
}

/*
 * Section 6.5 (Guide 7.1, 7.5, 7.5.1): bonded, or single-lane only when
 * that is verified (tbtU4BondSingle), then the link marked configured.
 * A link already x2 (Negotiated Link Width read first, Guide 7.1) is
 * bonded without a bonding; XhciCmLaneBonding 0 goes straight to the
 * single-lane checks. A Gen 4 link (record 22 section 6.2.3 items 1 and
 * 2) is classified first and never bonded: symmetric, or asymmetric with
 * the two ends a transmitter and receiver pair, it is used as bonded with
 * no lane write (XhciCmLaneBonding 0 has no single-lane route there);
 * any other Gen 4 reading leaves the port FAILED. On a link of two
 * version 2 routers a lane 0 still training is waited for first, with no
 * write. TBT_OK when the port may carry paths, TBT_E_STATE
 * when it is FAILED, TBT_E_GONE when the port's generation moved on.
 */
static LONG tbtU4Bond(PTBT_U4CM cm, PTBT_U4_PORT p, ULONG gen)
{
    TBT_U4_LANES ln;
    ULONG lane0;
    ULONG dev0;
    ULONG kind;
    LONG rc;
    TBT_WAIT w;

    ln.Rt[0] = &cm->HostRouter;
    ln.Rt[1] = &cm->HostRouter;
    ln.Rt[2] = &p->Dev;
    ln.Rt[3] = &p->Dev;
    ln.Adp[0] = p->Lane0;
    ln.Adp[1] = p->Lane0 + 1;
    ln.Adp[2] = p->Dev.UpAdp;
    ln.Adp[3] = p->Dev.UpAdp + 1;
    cm->Counters.BondAttempts++;

    if (tbtU4LaneRd(cm, ln.Rt[0], ln.Adp[0], &lane0) != TBT_OK) {
        return TBT_E_STATE;
    }
    if (tbtU4IsV2(&cm->HostRouter) && tbtU4IsV2(&p->Dev)) {
        /* Speed and width are valid only on an active link (Spec 8.2.2.3):
         * a lane 0 still training is waited for with no write, and one
         * that never reads connected leaves the port FAILED with no lane
         * written, rather than classified on a stale reading. */
        TbtWaitStart(&w, cm->Host, TBT_U4_CONNECT_US, FALSE);
        while (TbtU4LaneClass(TBT_LANE1_STATE(lane0)) != TBT_U4_LANE_CONNECTED) {
            if (!TBT_U4_LIVE(p, gen)) {
                return TBT_E_GONE;
            }
            if (TbtU4LaneClass(TBT_LANE1_STATE(lane0)) != TBT_U4_LANE_TRANSITIONAL ||
                TbtWaitDone(&w, cm->Host)) {
                tbtU4Log(cm, "u4.link.not-active", (p->Index << 8) | TBT_LANE1_STATE(lane0));
                p->Why = TBT_U4_WHY_LANE_STATE;
                return TBT_E_STATE;
            }
            tbtU4Pause(cm, &w);
            if (tbtU4LaneRd(cm, ln.Rt[0], ln.Adp[0], &lane0) != TBT_OK) {
                return TBT_E_STATE;
            }
        }
    }
    kind = TBT_U4_LINK_LEGACY;
    if (TBT_LANE1_SPEED(lane0) == TBT_LANE_SPEED_GEN4) {
        dev0 = 0;
        if (TBT_LANE1_NLW(lane0) != TBT_LANE_WIDTH_X2 &&
            tbtU4LaneRd(cm, ln.Rt[2], ln.Adp[2], &dev0) != TBT_OK) {
            return TBT_U4_LIVE(p, gen) ? TBT_E_STATE : TBT_E_GONE;
        }
        kind = tbtU4LinkKind(lane0, dev0);
    }
    if (kind == TBT_U4_LINK_UNUSABLE) {
        cm->Counters.Gen4Unusable++;
        tbtU4Log(cm, "u4.link.gen4-unusable", (p->Index << 8) | TBT_LANE1_NLW(lane0));
        p->Why = TBT_U4_WHY_LINK;
        return TBT_E_STATE;
    }
    if (kind == TBT_U4_LINK_GEN4 || kind == TBT_U4_LINK_GEN4_ASYM) {
        /* Aggregated by link initialisation: no Target Link Width, Lane
         * Bonding or Lane Disable written (Spec 4.2.2, Table 8-12). An
         * asymmetric link is never transitioned (decision U31). */
        p->Bonded = TRUE;
        p->Gen4 = TRUE;
        p->Asym = (BOOLEAN)(kind == TBT_U4_LINK_GEN4_ASYM);
        cm->Counters.Gen4Links++;
        if (p->Asym) {
            cm->Counters.Gen4Asym++;
        }
        if (!cm->Config.LaneBonding) {
            tbtU4Log(cm, "u4.bond.gen4", p->Index);
        }
        tbtU4Log(cm, "u4.link.gen4", (p->Index << 8) | TBT_LANE1_NLW(lane0));
        rc = TBT_OK;
        goto configured;
    }
    if (TBT_LANE1_NLW(lane0) == TBT_LANE_WIDTH_X2) {
        rc = TBT_OK;
    } else if (cm->Config.LaneBonding) {
        rc = tbtU4BondTry(cm, p, gen, &ln, lane0);
    } else {
        rc = TBT_E_STATE;
    }
    if (rc == TBT_E_GONE) {
        return rc;
    }
    if (rc == TBT_OK) {
        p->Bonded = TRUE;
        cm->Counters.BondX2++;
    } else {
        rc = tbtU4BondSingle(cm, p, gen, &ln);
        if (rc != TBT_OK) {
            return rc;
        }
    }
configured:
    if (tbtU4Configured(cm, p) != TBT_OK) {
        /* What it set stays owned (PcSetHost, PcSetDev) for the
         * teardown; no tunnel is built over an unconfigured link. */
        tbtU4Log(cm, "u4.port-configured.failed", p->Index);
        if (!TBT_U4_LIVE(p, gen)) {
            return TBT_E_GONE;
        }
        p->Why = TBT_U4_WHY_BONDING;
        return TBT_E_STATE;
    }
    return TBT_U4_LIVE(p, gen) ? TBT_OK : TBT_E_GONE;
}

/* ---------------------------------------------------------------------- */
/* The TMU (record 22 section 6.6; Guide 7.4)                              */
/* ---------------------------------------------------------------------- */

static LONG tbtU4TmuAdp(PTBT_U4CM cm, const TBT_U4_ROUTER *rt, ULONG adp,
                        ULONG reg, ULONG clr, ULONG set)
{
    if (rt->Adp[adp].CapTmu == 0) {
        return TBT_OK;
    }
    return tbtU4Rmw(cm, &rt->Route, TBT_CS_ADAPTER, adp,
                    rt->Adp[adp].CapTmu + reg, clr, set);
}

static LONG tbtU4TmuTd(PTBT_U4CM cm, const TBT_U4_ROUTER *rt, BOOLEAN on)
{
    return tbtU4Rmw(cm, &rt->Route, TBT_CS_ROUTER, 0, rt->CapTmu + TBT_TMU_RTR_CS_0,
                    on ? 0 : TBT_TMU_RTR0_TD, on ? TBT_TMU_RTR0_TD : 0);
}

/*
 * Item 6: the link's TMU off from HiFi-Bi (Guide 7.4.3.4.2 step 2: the
 * downstream router's TSPacketInterval 0) or, from HiFi Enhanced
 * Uni-Directional (step 1, record 22 section 6.2.2 item 6),
 * AdapterTimeSyncInterval 0 and Enable Enhanced Uni-Directional Mode 0
 * on both lane 0 adapters, the device's first; then Disable Time Sync set
 * on both lane 0 adapters (as at a new connection, 7.4.3.1, which names no
 * order; the device's first is [linux], record 22 section 6.6), inside a
 * time disruption of the device router. Done: the
 * link's TMU is no longer this driver's; failed: the port's TMU
 * quarantine.
 */
static LONG tbtU4TmuEnhancedOff(PTBT_U4CM cm, const TBT_U4_ROUTER *rt, ULONG adp)
{
    LONG rc;

    rc = tbtU4TmuAdp(cm, rt, adp, TBT_TMU_ADP_CS_9, TBT_TMU_ADP9_INTERVAL_MASK, 0);
    if (rc == TBT_OK) {
        rc = tbtU4TmuAdp(cm, rt, adp, TBT_TMU_ADP_CS_8, TBT_TMU_ADP8_EUDM, 0);
    }
    return rc;
}

static LONG tbtU4TmuDisable(PTBT_U4CM cm, PTBT_U4_PORT p)
{
    PTBT_U4_ROUTER d;
    LONG rc;

    d = &p->Dev;
    rc = tbtU4TmuTd(cm, d, TRUE);
    if (rc == TBT_OK && p->TmuEnhanced) {
        rc = tbtU4TmuEnhancedOff(cm, d, d->UpAdp);
        if (rc == TBT_OK) {
            rc = tbtU4TmuEnhancedOff(cm, &cm->HostRouter, p->Lane0);
        }
    } else if (rc == TBT_OK) {
        rc = tbtU4Rmw(cm, &d->Route, TBT_CS_ROUTER, 0, d->CapTmu + TBT_TMU_RTR_CS_3,
                      TBT_TMU_RTR3_INTERVAL_MASK, 0);
    }
    if (rc == TBT_OK) {
        rc = tbtU4TmuAdp(cm, d, d->UpAdp, TBT_TMU_ADP_CS_6, 0, TBT_TMU_ADP6_DTS);
    }
    if (rc == TBT_OK) {
        rc = tbtU4TmuAdp(cm, &cm->HostRouter, p->Lane0, TBT_TMU_ADP_CS_6, 0,
                         TBT_TMU_ADP6_DTS);
    }
    if (tbtU4TmuTd(cm, d, FALSE) != TBT_OK && rc == TBT_OK) {
        rc = TBT_E_STATE;
    }
    if (rc != TBT_OK) {
        p->TmuQuarantine = TRUE;
        return rc;
    }
    p->TmuConfigured = FALSE;
    p->TmuEnhanced = FALSE;
    p->TmuOk = FALSE;
    return TBT_OK;
}

/*
 * Guide 7.4.3.1 step 1: the device router's local time set from the host
 * router's. (a) The host router time, TMU_RTR_CS_1 bits 31:16 to
 * TMU_RTR_CS_3 bits 15:0 in ns, one read of the three DWs; (b) written to
 * the device's Post Local Time (TMU_RTR_CS_22 and 23) as two DWs, with no
 * 64-bit type; (c) Post Time low 1 and high FFFFFFFFh (TMU_RTR_CS_24, 25)
 * in one write; (d) Post Time high 0, which makes the router take the
 * posted time; (e) Post Time read until it is 0, within TBT_U4_TMU_POST_US
 * (record 22 section 6.6).
 */
static LONG tbtU4TmuPostTimeTo(PTBT_U4CM cm, PTBT_U4_PORT p, PTBT_U4_KID k, ULONG gen)
{
    PTBT_U4_ROUTER h;
    PTBT_U4_ROUTER d;
    ULONG now[3];
    ULONG pair[2];
    LONG rc;
    TBT_WAIT w;

    h = &cm->HostRouter;
    d = k != NULL ? &k->Dev : &p->Dev;
    rc = tbtU4RdN(cm, &h->Route, TBT_CS_ROUTER, 0, h->CapTmu + TBT_TMU_RTR_CS_1, 3, now);
    if (rc != TBT_OK) {
        return rc;
    }
    pair[0] = (now[0] >> 16) | (now[1] << 16);
    pair[1] = (now[1] >> 16) | ((now[2] & 0xFFFFUL) << 16);
    rc = tbtU4WrN(cm, &d->Route, TBT_CS_ROUTER, 0, d->CapTmu + TBT_TMU_RTR_CS_22, 2, pair);
    if (rc == TBT_OK) {
        pair[0] = 1;
        pair[1] = 0xFFFFFFFFUL;
        rc = tbtU4WrN(cm, &d->Route, TBT_CS_ROUTER, 0, d->CapTmu + TBT_TMU_RTR_CS_24, 2, pair);
    }
    if (rc == TBT_OK) {
        rc = tbtU4Wr(cm, &d->Route, TBT_CS_ROUTER, 0, d->CapTmu + TBT_TMU_RTR_CS_25, 0);
    }
    if (rc != TBT_OK) {
        return rc;
    }
    TbtWaitStart(&w, cm->Host, TBT_U4_TMU_POST_US, FALSE);
    while ((rc = tbtU4RdN(cm, &d->Route, TBT_CS_ROUTER, 0, d->CapTmu + TBT_TMU_RTR_CS_24, 2,
                          pair)) == TBT_OK && (pair[0] | pair[1]) != 0) {
        if (TbtWaitDone(&w, cm->Host) ||
            (k != NULL ? !tbtU4KidLive(cm, p, k, gen) : !TBT_U4_LIVE(p, gen))) {
            return TBT_E_TIMEOUT;
        }
        tbtU4Pause(cm, &w);
    }
    return rc;
}

/* The device router on a port: its local time from the host's. A kid's
 * (record 34 Part 3): tbtU4TmuPostTimeTo with the kid. */
static LONG tbtU4TmuPostTime(PTBT_U4CM cm, PTBT_U4_PORT p, ULONG gen)
{
    return tbtU4TmuPostTimeTo(cm, p, NULL, gen);
}

/*
 * Guide 7.4.3.4.1 from Off to HiFi-Bi, up to the Disable Time Sync bits:
 * step 1, Table 7-3's parameters in the downstream router (Freq
 * Measurement Window 800 in TMU_RTR_CS_0; ErrorAvgConst, OffsetAvgConst,
 * DelayAvgConst and FreqAvgConst 8, the four 6-bit fields of
 * TMU_RTR_CS_15); step 3a, EnableUniDirectionalMode 0 in both ports (the
 * Guide names no order; the device's first is [linux], record 22 section
 * 6.6); step 3b, TSPacketInterval 16 (Table 7-2) in the downstream
 * router.
 */
static LONG tbtU4TmuHiFiBi(PTBT_U4CM cm, PTBT_U4_PORT p)
{
    PTBT_U4_ROUTER d;
    ULONG avg;
    ULONG k;
    LONG rc;

    d = &p->Dev;
    avg = 0;
    for (k = 0; k < 4; k++) {
        avg |= TBT_TMU_AVG_CONST_HIFI << (6 * k);
    }
    rc = tbtU4Rmw(cm, &d->Route, TBT_CS_ROUTER, 0, d->CapTmu + TBT_TMU_RTR_CS_0,
                  TBT_TMU_RTR0_FREQ_WIND_MASK,
                  TBT_TMU_FREQ_WINDOW_HIFI << TBT_TMU_RTR0_FREQ_WIND_SHIFT);
    if (rc == TBT_OK) {
        rc = tbtU4Rmw(cm, &d->Route, TBT_CS_ROUTER, 0, d->CapTmu + TBT_TMU_RTR_CS_15,
                      TBT_TMU_RTR15_AVG_MASK, avg);
    }
    if (rc == TBT_OK) {
        rc = tbtU4TmuAdp(cm, d, d->UpAdp, TBT_TMU_ADP_CS_3, TBT_TMU_ADP3_UDM, 0);
    }
    if (rc == TBT_OK) {
        rc = tbtU4TmuAdp(cm, &cm->HostRouter, p->Lane0, TBT_TMU_ADP_CS_3, TBT_TMU_ADP3_UDM, 0);
    }
    if (rc == TBT_OK) {
        rc = tbtU4Rmw(cm, &d->Route, TBT_CS_ROUTER, 0, d->CapTmu + TBT_TMU_RTR_CS_3,
                      TBT_TMU_RTR3_INTERVAL_MASK,
                      TBT_TMU_INTERVAL_HIFI << TBT_TMU_RTR3_INTERVAL_SHIFT);
    }
    return rc;
}

/*
 * Guide 7.4.3.4.1 from Off to HiFi Enhanced Uni-Directional (record 22
 * section 6.2.2 item 6), up to the Disable Time Sync bits: step 1, Table
 * 7-3's parameters in the downstream router as for HiFi-Bi, and on both
 * lane 0 adapters, the device's first (Table 7-3 names the downstream
 * facing port; Spec 8.2.2.2 Table 8-11 requires DirSwitchN above 1 on a
 * port whose enhanced mode is enabled), Spec 8.2.2.2's note's values:
 * Replenish Timeout 3125 and Replenish Threshold 0 (TMU_ADP_CS_8),
 * ReplenishN 30 and DirSwitchN 255 (TMU_ADP_CS_9); step 2b and 2c,
 * AdapterTimeSyncInterval 16 and Enable Enhanced Uni-Directional Mode 1
 * on the device's upstream lane 0 adapter, then on the host's. CLx is
 * never enabled here (step 2a), and TSPacketInterval is not written (Spec
 * 7.3.1.3: ignored in that mode).
 */
static LONG tbtU4TmuEnhancedOnAt(PTBT_U4CM cm, const TBT_U4_ROUTER *up, ULONG upAdp,
                                 const TBT_U4_ROUTER *d)
{
    const TBT_U4_ROUTER *rt;
    ULONG adp;
    ULONG avg;
    ULONG k;
    LONG rc;

    avg = 0;
    for (k = 0; k < 4; k++) {
        avg |= TBT_TMU_AVG_CONST_HIFI << (6 * k);
    }
    rc = tbtU4Rmw(cm, &d->Route, TBT_CS_ROUTER, 0, d->CapTmu + TBT_TMU_RTR_CS_0,
                  TBT_TMU_RTR0_FREQ_WIND_MASK,
                  TBT_TMU_FREQ_WINDOW_HIFI << TBT_TMU_RTR0_FREQ_WIND_SHIFT);
    if (rc == TBT_OK) {
        rc = tbtU4Rmw(cm, &d->Route, TBT_CS_ROUTER, 0, d->CapTmu + TBT_TMU_RTR_CS_15,
                      TBT_TMU_RTR15_AVG_MASK, avg);
    }
    for (k = 0; k < 2 && rc == TBT_OK; k++) {
        rt = k == 0 ? d : up;
        adp = k == 0 ? d->UpAdp : upAdp;
        rc = tbtU4TmuAdp(cm, rt, adp, TBT_TMU_ADP_CS_8,
                         TBT_TMU_ADP8_REPL_TIMEOUT_MASK | TBT_TMU_ADP8_REPL_THRESH_MASK,
                         TBT_TMU_REPL_TIMEOUT_HIFI |
                         (TBT_TMU_REPL_THRESH_HIFI << TBT_TMU_ADP8_REPL_THRESH_SHIFT));
        if (rc == TBT_OK) {
            rc = tbtU4TmuAdp(cm, rt, adp, TBT_TMU_ADP_CS_9,
                             TBT_TMU_ADP9_REPLN_MASK | TBT_TMU_ADP9_DIRSWN_MASK,
                             TBT_TMU_REPLN_HIFI |
                             (TBT_TMU_DIRSWN_HIFI << TBT_TMU_ADP9_DIRSWN_SHIFT));
        }
    }
    for (k = 0; k < 2 && rc == TBT_OK; k++) {
        rt = k == 0 ? d : up;
        adp = k == 0 ? d->UpAdp : upAdp;
        rc = tbtU4TmuAdp(cm, rt, adp, TBT_TMU_ADP_CS_9, TBT_TMU_ADP9_INTERVAL_MASK,
                         TBT_TMU_ADP_INTERVAL_HIFI << TBT_TMU_ADP9_INTERVAL_SHIFT);
        if (rc == TBT_OK) {
            rc = tbtU4TmuAdp(cm, rt, adp, TBT_TMU_ADP_CS_8, 0, TBT_TMU_ADP8_EUDM);
        }
    }
    return rc;
}

static LONG tbtU4TmuEnhancedOn(PTBT_U4CM cm, PTBT_U4_PORT p)
{
    return tbtU4TmuEnhancedOnAt(cm, &cm->HostRouter, p->Lane0, &p->Dev);
}

/*
 * Items 2 to 5: HiFi-Bi from Off on a link that will carry a USB3 tunnel,
 * inside a time disruption of the device router: the local time posted
 * (tbtU4TmuPostTime), the mode set (tbtU4TmuHiFiBi), then Disable Time
 * Sync cleared on the device's upstream lane adapter and then on the
 * host's - Guide 7.4.3.4.1 steps 3c and 3d name the other order; this one
 * is [linux] (record 22 section 6.6) - and, the disruption over, 50 ms
 * for the TMU to converge (Guide 7.4.3.1 step 4). `enhanced`: HiFi
 * Enhanced Uni-Directional instead (tbtU4TmuEnhancedOn), on a link of two
 * version 2 routers (section 6.2.2 item 6), Disable Time Sync then
 * cleared in the Guide's own order (steps 2d and 2e, the device's first).
 * A failure clears the
 * disruption, turns the link's TMU off again and leaves the USB3 tunnel
 * unbuilt; a step that cannot be undone is the port's TMU quarantine.
 */
static VOID tbtU4TmuEnable(PTBT_U4CM cm, PTBT_U4_PORT p, ULONG gen, BOOLEAN enhanced)
{
    PTBT_U4_ROUTER h;
    PTBT_U4_ROUTER d;
    ULONG cs0;
    LONG rc;

    h = &cm->HostRouter;
    d = &p->Dev;
    p->TmuOk = FALSE;
    if (d->CapTmu == 0) {
        p->TmuOk = TRUE;    /* no TMU: no adapter time to correct */
        return;
    }
    if (tbtU4Rd(cm, &d->Route, TBT_CS_ROUTER, 0, d->CapTmu + TBT_TMU_RTR_CS_0, &cs0) != TBT_OK) {
        return;
    }
    if ((cs0 & TBT_TMU_RTR0_TSP_NS) != 0) {
        p->TmuOk = TRUE;    /* the ECN's TMU disabled: built without */
        return;
    }
    if (h->CapTmu == 0 || h->Adp[p->Lane0].CapTmu == 0 ||
        d->Adp[d->UpAdp].CapTmu == 0 || (!p->Bonded && !p->SingleLane)) {
        return;
    }
    if (!tbtU4TmuOff(cm, p)) {
        tbtU4Log(cm, "u4.tmu.inherited", p->Index);
        return;
    }
    p->TmuEnhanced = enhanced;
    rc = tbtU4TmuTd(cm, d, TRUE);
    if (rc == TBT_OK) {
        rc = tbtU4TmuPostTime(cm, p, gen);
    }
    if (rc == TBT_OK) {
        if (enhanced) {
            /* Owned before its writes: a failure undoes the enhanced fields. */
            p->TmuConfigured = TRUE;
            rc = tbtU4TmuEnhancedOn(cm, p);
        } else {
            rc = tbtU4TmuHiFiBi(cm, p);
        }
    }
    if (rc == TBT_OK) {
        p->TmuConfigured = TRUE;
        rc = tbtU4TmuAdp(cm, d, d->UpAdp, TBT_TMU_ADP_CS_6, TBT_TMU_ADP6_DTS, 0);
    }
    if (rc == TBT_OK) {
        rc = tbtU4TmuAdp(cm, h, p->Lane0, TBT_TMU_ADP_CS_6, TBT_TMU_ADP6_DTS, 0);
    }
    if (tbtU4TmuTd(cm, d, FALSE) != TBT_OK && rc == TBT_OK) {
        rc = TBT_E_STATE;
    }
    if (rc != TBT_OK) {
        cm->Counters.TmuFailures++;
        tbtU4Log(cm, "u4.tmu.failed", (ULONG)rc);
        p->TmuConfigured = TRUE;    /* whatever was written is undone */
        (void)tbtU4TmuDisable(cm, p);
        return;
    }
    cm->Counters.TmuEnabled++;
    if (enhanced) {
        tbtU4Log(cm, "u4.tmu.enhanced", p->Index);
    }
    tbtU4WaitMin(cm, TBT_U4_TMU_SETTLE_US);
    p->TmuOk = TRUE;
}

/* Item 1: no TMU packet crosses a new link until it is configured
 * (Guide 7.4.3.1). */
static VOID tbtU4TmuAtConnect(PTBT_U4CM cm, PTBT_U4_PORT p)
{
    if (!p->Dev.EnumeratedHere || p->Dev.CapTmu == 0) {
        return;
    }
    (void)tbtU4TmuAdp(cm, &cm->HostRouter, p->Lane0, TBT_TMU_ADP_CS_6, 0,
                      TBT_TMU_ADP6_DTS);
    (void)tbtU4TmuAdp(cm, &p->Dev, p->Dev.UpAdp, TBT_TMU_ADP_CS_6, 0,
                      TBT_TMU_ADP6_DTS);
}

/* ---------------------------------------------------------------------- */
/* The device router on a port (record 22 section 6.4)                    */
/* ---------------------------------------------------------------------- */

/*
 * Steps 2 to 9 on a port whose lane 0 reads connected: Lock cleared on lane
 * 0 only (Guide 3.2.1; Spec 8.2.2.1, which leaves a write to a lane 1
 * adapter's Lock undefined), ROUTER_CS_0 to 4 at route =
 * lane 0, the USB4 Version classified, enumerated (or adopted when a
 * predecessor enumerated it, its TopologyID kept), retried up to 20 times
 * 50 ms apart while it comes up (u4cm.c u4cm_connect), its adapters, the
 * DROM and UUID, Get Capabilities, the Buffer Allocation Request; a
 * Thunderbolt 3 router through section 8 items 2 and 4. TBT_OK, or the
 * port's next state in *next (UNSUPPORTED or FAILED), or TBT_E_GONE.
 */
static LONG tbtU4Router(PTBT_U4CM cm, PTBT_U4_PORT p, ULONG gen, PULONG next)
{
    PTBT_U4_ROUTER h;
    PTBT_U4_ROUTER d;
    ULONG v[9];
    ULONG a;
    ULONG k;
    ULONG ver;
    LONG rc;

    h = &cm->HostRouter;
    d = &p->Dev;
    *next = TBT_U4_FAILED;
    /* Lane 0 only: writing Lock in a lane 1 adapter is undefined
     * behaviour (Spec 8.2.2.1, ADP_CS_4). */
    a = p->Lane0;
    rc = tbtU4Rd(cm, &h->Route, TBT_CS_ADAPTER, a, TBT_ADP_CS_4, &v[0]);
    if (rc == TBT_OK && (v[0] & TBT_ADP4_LOCK) != 0) {
        rc = tbtU4Wr(cm, &h->Route, TBT_CS_ADAPTER, a, TBT_ADP_CS_4,
                     v[0] & ~TBT_ADP4_LOCK);
    }
    if (rc != TBT_OK) {
        p->Why = TBT_U4_WHY_ENUMERATION;
        return rc;
    }
    p->Unlocked = TRUE;
    p->RelockSeen = FALSE;

    tbtU4Zero(d, sizeof(*d));
    d->Route.Hi = 0;
    d->Route.Lo = p->Lane0;
    d->Depth = 1;
    d->CapsState = TBT_U4_CAPS_PENDING;
    rc = TBT_E_TIMEOUT;
    for (k = 0; k < TBT_U4_ENUM_TRIES; k++) {
        if (!TBT_U4_LIVE(p, gen)) {
            return TBT_E_GONE;
        }
        rc = tbtU4RdN(cm, &d->Route, TBT_CS_ROUTER, 0, TBT_ROUTER_CS_0, 7, v);
        if (rc == TBT_E_NOTIFY && (cm->Ring->LastEvent == TBT_EV_ERR_ENUM ||
                                   cm->Ring->LastEvent == TBT_EV_ERR_NUA)) {
            /* Someone else's, or uninitialised behind its own downstream
             * port (Guide 3.2.1 step 1): left alone - or, with XhciCmNet 1,
             * perhaps another host, which the inter-domain discovery
             * decides (record 35 section 3.3; tbtU4XdService). */
            *next = cm->Config.Net ? TBT_U4_XDOMAIN : TBT_U4_UNSUPPORTED;
            p->Why = cm->Config.Net ? TBT_U4_WHY_XDOMAIN : TBT_U4_WHY_ENUM_ERROR;
            return rc;
        }
        if (tbtU4Gone(cm, rc)) {
            return TBT_E_GONE;
        }
        if (rc == TBT_OK) {
            tbtU4TakeHeader(d, v);
            ver = TbtHwRouterVersion(d->Usb4Version);
            if (ver == TBT_HW_ROUTER_RESERVED) {
                *next = TBT_U4_UNSUPPORTED;
                p->Why = TBT_U4_WHY_VERSION;
                cm->Counters.RoutersRefused++;
                tbtU4Log(cm, "u4.router.version", d->Usb4Version);
                return TBT_E_STATE;
            }
            d->Depth = 1;
            if (d->Tbt3 && (v[3] & TBT_RTR3_V) == 0 &&
                cm->Ring->LastReadAdapter != 0) {
                /* A Thunderbolt 3 router before enumeration may read
                 * Upstream Adapter 0 (the TS3 Plus does [obs], record 22
                 * section 8 item 2); its upstream adapter is the one the
                 * read arrived at, the Adapter Num of the read's response
                 * (Spec Table 6-3; ICM-PROTOCOL.md 5), which the
                 * enumeration write then sets ([linux]). */
                d->UpAdp = cm->Ring->LastReadAdapter;
                v[1] = (v[1] & ~TBT_RTR1_UP_ADP_MASK) |
                       ((d->UpAdp << TBT_RTR1_UP_ADP_SHIFT) & TBT_RTR1_UP_ADP_MASK);
            }
            if ((v[3] & TBT_RTR3_V) != 0) {
                if (!tbtU4Enumerated(d, v)) {
                    /* Valid with another depth or TopologyID: someone
                     * else's; DW 1 to 3 are never rewritten once Valid is
                     * set (Guide 3.2.1 note). */
                    *next = TBT_U4_UNSUPPORTED;
                    p->Why = TBT_U4_WHY_ENUMERATION;
                    cm->Counters.RoutersRefused++;
                    tbtU4Log(cm, "u4.router.topology", v[3]);
                    return TBT_E_STATE;
                }
                if (!d->EnumeratedHere) {
                    d->Preserved = TRUE;
                    tbtU4CmuvFound(cm, d, v[4]);
                }
                /* Enumerated here or by a predecessor: Router Ready either
                 * way before the router is used. */
                rc = (d->Tbt3 || (v[6] & TBT_RTR6_RR) != 0) ? TBT_OK :
                     tbtU4WaitReady(cm, d);
                if (rc == TBT_OK) {
                    if (d->Preserved) {
                        cm->Counters.RoutersAdopted++;
                    }
                    break;
                }
            } else {
                rc = tbtU4Enumerate(cm, d, v);
                if (rc == TBT_OK) {
                    break;
                }
            }
            if (tbtU4Gone(cm, rc)) {
                return TBT_E_GONE;
            }
        }
        if (k + 1 < TBT_U4_ENUM_TRIES) {
            tbtU4WaitMin(cm, TBT_U4_ENUM_GAP_US);
        }
    }
    if (rc != TBT_OK) {
        p->Why = rc == TBT_E_TIMEOUT && d->EnumeratedHere ? TBT_U4_WHY_ROUTER_READY :
                 TBT_U4_WHY_ENUMERATION;
        tbtU4Log(cm, "u4.router.enumeration", (ULONG)rc);
        return rc;
    }
    d->Present = TRUE;
    if (!d->Tbt3) {
        /* Before the adapters: an adapter the DROM marks unused is never
         * read (section 6.11). */
        tbtU4ReadDrom(cm, d);
        if (!TBT_U4_LIVE(p, gen)) {
            return TBT_E_GONE;
        }
    }
    if (d->UpAdp == 0 || d->UpAdp + 1 >= TBT_U4_MAX_ADP) {
        rc = TBT_E_STATE;
    } else if (d->Tbt3) {
        /* Its DROM sits behind the Plug Events capability's EEPROM
         * bit-bang (section 8 item 4), so the capabilities come first;
         * the DROM still before the adapters, as on a USB4 router. Not
         * under a resume budget: a budget running out refuses every
         * request, the release of the part included, and the name is
         * optional; under one, the names are read once the resume is done
         * (TbtU4Poll, tbtU4DromLate). */
        rc = tbtU4RouterCaps(cm, d, TBT_RTR1_NEXT_CAP(v[1]));
        if (rc == TBT_OK && d->CapPlugEvents != 0 && !cm->Budgeted) {
            tbtU4ReadDrom(cm, d);
            if (!TBT_U4_LIVE(p, gen)) {
                return TBT_E_GONE;
            }
        } else if (rc == TBT_OK && d->CapPlugEvents != 0) {
            d->DromOwed = TRUE;
        }
        if (rc == TBT_OK) {
            rc = tbtU4DiscoverAdapters(cm, d);
        }
    } else {
        rc = tbtU4DiscoverAdapters(cm, d);
        if (rc == TBT_OK) {
            rc = tbtU4RouterCaps(cm, d, TBT_RTR1_NEXT_CAP(v[1]));
        }
    }
    if (rc != TBT_OK) {
        p->Why = TBT_U4_WHY_ENUMERATION;
        return tbtU4Gone(cm, rc) ? TBT_E_GONE : rc;
    }
    tbtU4ApplyQuirks(cm, d);
    if (d->Tbt3) {
        cm->Counters.Tbt3Routers++;
        d->CapsState = TBT_U4_CAPS_DEFAULT;
        if (tbtU4Tbt3Setup(cm, d) != TBT_OK) {
            *next = TBT_U4_UNSUPPORTED;
            p->Why = TBT_U4_WHY_PLUG_EVENTS;
            cm->Counters.RoutersRefused++;
            return TBT_E_STATE;
        }
    } else {
        d->UuidValid = (BOOLEAN)(tbtU4RdN(cm, &d->Route, TBT_CS_ROUTER, 0, TBT_ROUTER_CS_7, 2,
                                          d->Uuid) == TBT_OK);
        tbtU4GetCapabilities(cm, d);
        tbtU4BufferAllocation(cm, d, d->UpAdp);
    }
    tbtU4Log(cm, "u4.router.up", (d->Vendor << 16) | d->Device);
    return TBT_U4_LIVE(p, gen) ? TBT_OK : TBT_E_GONE;
}

/* The link is a USB4 link, not in TBT3-Compatible Mode (PORT_CS_18 bit 9
 * TCM clear on the host's port, Spec 8.2.2.4). */
static BOOLEAN tbtU4LinkUsb4(PTBT_U4CM cm, PTBT_U4_PORT p)
{
    ULONG v;

    if (p->Dev.Tbt3 || cm->HostRouter.Tbt3) {
        /* Every link of a Thunderbolt host is a Thunderbolt link: no USB3
         * tunnel, ever (record 22 section 19.4 item 5). */
        return FALSE;
    }
    if (tbtU4Rd(cm, &cm->HostRouter.Route, TBT_CS_ADAPTER, p->Lane0,
                cm->HostRouter.Adp[p->Lane0].CapPort + TBT_PORT_CS_18, &v) != TBT_OK) {
        return FALSE;
    }
    return (BOOLEAN)((v & TBT_PORT18_TCM) == 0);
}

/* Whether this connection will carry a USB3 tunnel built here. */
static BOOLEAN tbtU4WantUsb3(PTBT_U4CM cm, PTBT_U4_PORT p)
{
    return (BOOLEAN)(cm->Config.Usb3Tunnel && p->Usb3Down != 0 && !p->UtoOff &&
                     tbtU4FirstOf(&p->Dev, TBT_ADP_TYPE_USB3_UP) != 0 &&
                     !p->DpPreserved && tbtU4LinkUsb4(cm, p));
}

/*
 * Section 6.6: ROUTER_CS_5 in one read-modify-write - PTO where both PCIe
 * adapters exist, UTO where both USB3 adapters exist on a USB4 link,
 * whatever XhciCmUsb3Tunnel says (Guide 3.2.3 "must"), HCO never, CNS left
 * as it is (0 on a fresh router; record 22 section 6.6) - then the TMU
 * where a USB3 tunnel will be built, and on a link of two version 2
 * routers whose device router this driver enumerated whatever the tunnels
 * (section 6.2.2 item 6: HiFi Enhanced Uni-Directional, its mode on any
 * link of two version 2 routers), then CV in a second write and
 * Configuration Ready within 500 ms.
 */
static LONG tbtU4ConfigValid(PTBT_U4CM cm, PTBT_U4_PORT p, ULONG gen)
{
    PTBT_U4_ROUTER d;
    ULONG set;
    ULONG cs6;
    LONG rc;
    BOOLEAN v2;
    TBT_WAIT w;

    d = &p->Dev;
    set = 0;
    if (p->PcieDown != 0 && tbtU4FirstOf(d, TBT_ADP_TYPE_PCIE_UP) != 0) {
        set |= TBT_RTR5_PTO;
    }
    if (p->Usb3Down != 0 && tbtU4FirstOf(d, TBT_ADP_TYPE_USB3_UP) != 0 &&
        tbtU4LinkUsb4(cm, p)) {
        set |= TBT_RTR5_UTO;
    }
    rc = tbtU4Rmw(cm, &d->Route, TBT_CS_ROUTER, 0, TBT_ROUTER_CS_5, TBT_RTR5_HCO, set);
    if (rc != TBT_OK) {
        return tbtU4Gone(cm, rc) ? TBT_E_GONE : rc;
    }
    v2 = tbtU4V2Link(cm, p);
    if (!p->TmuQuarantine && ((v2 && d->EnumeratedHere) || tbtU4WantUsb3(cm, p))) {
        tbtU4TmuEnable(cm, p, gen, v2);
    }
    if (!TBT_U4_LIVE(p, gen)) {
        return TBT_E_GONE;
    }
    rc = tbtU4Rmw(cm, &d->Route, TBT_CS_ROUTER, 0, TBT_ROUTER_CS_5, 0, TBT_RTR5_CV);
    if (rc != TBT_OK) {
        return tbtU4Gone(cm, rc) ? TBT_E_GONE : rc;
    }
    TbtWaitStart(&w, cm->Host, TBT_U4_CONFIG_READY_US, FALSE);
    for (;;) {
        rc = tbtU4Rd(cm, &d->Route, TBT_CS_ROUTER, 0, TBT_ROUTER_CS_6, &cs6);
        if (rc != TBT_OK) {
            return tbtU4Gone(cm, rc) ? TBT_E_GONE : rc;
        }
        if ((cs6 & TBT_RTR6_CR) != 0) {
            return TBT_OK;
        }
        if (!TBT_U4_LIVE(p, gen)) {
            return TBT_E_GONE;
        }
        if (TbtWaitDone(&w, cm->Host)) {
            cm->Counters.ConfigReadyTimeouts++;
            p->Why = TBT_U4_WHY_CONFIG_READY;
            return TBT_E_TIMEOUT;
        }
        tbtU4Pause(cm, &w);
    }
}

/* ---------------------------------------------------------------------- */
/* Tunnels (record 22 sections 6.8, 6.9, 9.3 step 1)                      */
/* ---------------------------------------------------------------------- */

/* The orderly teardown of a tunnel with both ends reachable (Guide 5.3.2,
 * 5.5.2; section 9.3 step 1, also section 6.8 step 5's and the eject's):
 * Path Enable cleared host side first, then device side; then every entry
 * written or found invalidated and drained. TRUE when every step was
 * proven. */
static BOOLEAN tbtU4ReleaseBw(PTBT_U4CM cm, PTBT_U4_PORT p);
static LONG tbtU4Entry(PTBT_U4CM cm, const TBT_ROUTE *r, ULONG adp, ULONG hop,
                       PULONG e);
static BOOLEAN tbtU4EeClear(PTBT_U4CM cm, const TBT_U4_ROUTER *rt, ULONG adp);

/*
 * The host side of a port's PCIe and/or USB3 tunnels found again from the
 * registers and closed, whatever the records say: each selected protocol
 * adapter's Path Enable cleared and its HopID 8 entry invalidated and
 * drained if Valid or pending; every Valid or pending entry of the host
 * lane 0 that leads to a selected adapter invalidated and drained. A failing step is
 * quarantined by tbtU4Invalidate. TRUE only when every entry was read and
 * every step proven.
 */
static BOOLEAN tbtU4HostSweep(PTBT_U4CM cm, PTBT_U4_PORT p, BOOLEAN pcie, BOOLEAN usb3)
{
    PTBT_U4_ROUTER h;
    ULONG k;
    ULONG adp;
    ULONG hop;
    ULONG out;
    ULONG e[2];
    BOOLEAN ok;

    h = &cm->HostRouter;
    ok = TRUE;
    for (k = 0; k < 2; k++) {
        adp = k == 0 ? (pcie ? p->PcieDown : 0) : (usb3 ? p->Usb3Down : 0);
        if (adp == 0 || h->Adp[adp].CapAdp == 0) {
            continue;
        }
        ok &= tbtU4Disable(cm, p->Index, &h->Route, adp, h->Adp[adp].CapAdp,
                           (BOOLEAN)(k == 1));
        if (k == 0) {
            (void)tbtU4EeClear(cm, h, adp);
        }
        if (tbtU4Entry(cm, &h->Route, adp, TBT_HOP_PROTOCOL, e) != TBT_OK) {
            ok = FALSE;
        } else if ((e[0] & TBT_PATH0_VALID) != 0 || (e[1] & TBT_PATH1_PP) != 0) {
            ok &= tbtU4Invalidate(cm, p->Index, &h->Route, adp, TBT_HOP_PROTOCOL);
        }
    }
    if (p->Lane0 == 0) {
        return ok;
    }
    for (hop = TBT_HOP_FIRST; hop <= h->Adp[p->Lane0].MaxInHop; hop++) {
        if (tbtU4Entry(cm, &h->Route, p->Lane0, hop, e) != TBT_OK) {
            ok = FALSE;
            continue;
        }
        if ((e[0] & TBT_PATH0_VALID) == 0 && (e[1] & TBT_PATH1_PP) == 0) {
            continue;
        }
        /* Valid, or invalid with packets still pending: either way the
         * path is closed only once drained (sections 7.3, 10.2). */
        out = TBT_PATH0_OUT_ADP(e[0]);
        if ((pcie && p->PcieDown != 0 && out == p->PcieDown) ||
            (usb3 && p->Usb3Down != 0 && out == p->Usb3Down)) {
            ok &= tbtU4Invalidate(cm, p->Index, &h->Route, p->Lane0, hop);
        }
    }
    return ok;
}

/* What a teardown could not read, swept again; the port connects only
 * once it is proven gone. */
static BOOLEAN tbtU4ResidualClear(PTBT_U4CM cm, PTBT_U4_PORT p)
{
    if (p->HostResidual && tbtU4HostSweep(cm, p, TRUE, TRUE)) {
        p->HostResidual = FALSE;
        p->LastUsb3TeardownUs = tbtU4Now(cm);   /* the 500 ms gap from now */
        p->Usb3GapValid = TRUE;
        tbtU4Log(cm, "u4.residual.cleared", p->Index);
    }
    return (BOOLEAN)!p->HostResidual;
}

static BOOLEAN tbtU4TunnelDown(PTBT_U4CM cm, PTBT_U4_PORT p, PTBT_U4_TUNNEL t,
                               BOOLEAN usb3)
{
    PTBT_U4_ROUTER h;
    PTBT_U4_ROUTER d;
    ULONG hp;
    ULONG dp;
    BOOLEAN ok;

    h = &cm->HostRouter;
    d = &p->Dev;
    hp = usb3 ? p->Usb3Down : p->PcieDown;
    dp = tbtU4FirstOf(d, usb3 ? TBT_ADP_TYPE_USB3_UP : TBT_ADP_TYPE_PCIE_UP);
    ok = TRUE;
    if ((t->Written & TBT_U4_E_HOST_ENABLE) != 0) {
        ok &= tbtU4Disable(cm, p->Index, &h->Route, hp, h->Adp[hp].CapAdp, usb3);
    }
    if ((t->Written & TBT_U4_E_DEV_ENABLE) != 0 && d->Present && dp != 0) {
        ok &= tbtU4Disable(cm, p->Index, &d->Route, dp, d->Adp[dp].CapAdp, usb3);
    }
    if (!usb3) {
        /* Its Extended Encapsulation, whoever set it (section 6.2.2 item
         * 7); a failure is logged and the teardown goes on. */
        (void)tbtU4EeClear(cm, h, hp);
        if (d->Present) {
            (void)tbtU4EeClear(cm, d, dp);
        }
    }
    if ((t->Written & TBT_U4_E_HOST_PROTO) != 0) {
        ok &= tbtU4Invalidate(cm, p->Index, &h->Route, hp, TBT_HOP_PROTOCOL);
    }
    if ((t->Written & TBT_U4_E_HOST_LANE) != 0) {
        ok &= tbtU4Invalidate(cm, p->Index, &h->Route, p->Lane0, t->Hu);
    }
    if (d->Present && (t->Written & TBT_U4_E_DEV_LANE) != 0) {
        ok &= tbtU4Invalidate(cm, p->Index, &d->Route, d->UpAdp, t->Hd);
    }
    if (d->Present && dp != 0 && (t->Written & TBT_U4_E_DEV_PROTO) != 0) {
        ok &= tbtU4Invalidate(cm, p->Index, &d->Route, dp, TBT_HOP_PROTOCOL);
    }
    if (t->Incomplete) {
        /* An entry never read: found again from the registers. Until
         * that succeeds nothing is proven closed, and the port owes the
         * sweep before it connects again. */
        if (tbtU4HostSweep(cm, p, (BOOLEAN)!usb3, usb3)) {
            t->Incomplete = FALSE;
        } else {
            ok = FALSE;
            p->HostResidual = TRUE;
        }
    }
    if (usb3 && (t->Written & ~TBT_U4_E_BANDWIDTH) != 0) {
        /* Guide 5.5.1 step 1's gap runs from any USB3 path teardown. */
        p->LastUsb3TeardownUs = tbtU4Now(cm);
        p->Usb3GapValid = TRUE;
    }
    t->Written &= TBT_U4_E_BANDWIDTH;
    t->Up = FALSE;
    t->Preserved = FALSE;
    t->Own = FALSE;
    return ok;
}

/*
 * Record 22 section 6.2.2 item 7 (Spec 11.1.1.1's note, 8.2.2.7; Guide
 * 5.3.1 step 4, 5.3.2 step 3): ADP_PCIE_CS_1 Extended Encapsulation on a
 * PCIe adapter. tbtU4EeSet: set and read back, before either Path Enable,
 * on a tunnel between two version 2 routers. tbtU4EeClear: at a PCIe
 * teardown, after the Path Enables and before the paths are invalidated,
 * read on an adapter of a version 2 router and written 0 if it reads 1 - a
 * predecessor's tunnel included - only once that adapter's Path Enable
 * reads 0; an adapter whose ownership is not established (section 7.7) is
 * never written. Neither while XhciCmUsb4V2
 * is 0. TRUE when EE reads as wanted.
 */
static BOOLEAN tbtU4EeSet(PTBT_U4CM cm, const TBT_U4_ROUTER *rt, ULONG adp)
{
    ULONG v;
    ULONG at;

    at = rt->Adp[adp].CapAdp + TBT_PCIE_CS_1;
    if (tbtU4Rmw(cm, &rt->Route, TBT_CS_ADAPTER, adp, at, 0, TBT_PCIE1_EE) != TBT_OK ||
        tbtU4Rd(cm, &rt->Route, TBT_CS_ADAPTER, adp, at, &v) != TBT_OK ||
        (v & TBT_PCIE1_EE) == 0) {
        tbtU4Log(cm, "u4.ee.failed", (rt->Route.Lo << 8) | adp);
        return FALSE;
    }
    return TRUE;
}

static BOOLEAN tbtU4EeClear(PTBT_U4CM cm, const TBT_U4_ROUTER *rt, ULONG adp)
{
    ULONG v;
    ULONG at;

    if (!cm->Config.Usb4V2 || !tbtU4IsV2(rt) || adp == 0 || adp >= TBT_U4_MAX_ADP ||
        rt->Adp[adp].CapAdp == 0 || rt->Adp[adp].Type == TBT_ADP_TYPE_UNUSED) {
        return TRUE;
    }
    if (tbtU4Reserved(cm, &rt->Route, adp, 0, TRUE) ||
        tbtU4Reserved(cm, &rt->Route, adp, TBT_HOP_PROTOCOL, FALSE)) {
        return FALSE;
    }
    /* Never with the adapter's Path Enable set (Spec 8.2.2.7): a disable
     * that failed is the adapter's quarantine, whose lift clears EE then
     * (tbtU4RetryQuarantine). */
    if (tbtU4Rd(cm, &rt->Route, TBT_CS_ADAPTER, adp,
                rt->Adp[adp].CapAdp + TBT_PCIE_CS_0, &v) != TBT_OK ||
        (v & TBT_PCIE0_PE) != 0) {
        return FALSE;
    }
    at = rt->Adp[adp].CapAdp + TBT_PCIE_CS_1;
    if (tbtU4Rd(cm, &rt->Route, TBT_CS_ADAPTER, adp, at, &v) != TBT_OK) {
        return FALSE;
    }
    if ((v & TBT_PCIE1_EE) == 0) {
        return TRUE;
    }
    if (tbtU4Wr(cm, &rt->Route, TBT_CS_ADAPTER, adp, at, v & ~TBT_PCIE1_EE) != TBT_OK ||
        tbtU4Rd(cm, &rt->Route, TBT_CS_ADAPTER, adp, at, &v) != TBT_OK ||
        (v & TBT_PCIE1_EE) != 0) {
        tbtU4Log(cm, "u4.ee.stuck", (rt->Route.Lo << 8) | adp);
        return FALSE;
    }
    cm->Counters.EeCleared++;
    return TRUE;
}

/* A protocol adapter's LTSSM in Detect within `us` (Guide 5.3.1 step 1). */
static BOOLEAN tbtU4WaitDetect(PTBT_U4CM cm, PTBT_U4_PORT p, ULONG gen,
                               const TBT_U4_ROUTER *rt, ULONG adp, ULONG us)
{
    ULONG v;
    TBT_WAIT w;

    TbtWaitStart(&w, cm->Host, us, FALSE);
    for (;;) {
        if (tbtU4Rd(cm, &rt->Route, TBT_CS_ADAPTER, adp,
                    rt->Adp[adp].CapAdp + TBT_PCIE_CS_0, &v) != TBT_OK) {
            return FALSE;
        }
        if (TBT_PCIE0_LTSSM(v) == TBT_LTSSM_DETECT) {
            return TRUE;
        }
        if (TbtWaitDone(&w, cm->Host) || (p != NULL && !TBT_U4_LIVE(p, gen))) {
            return FALSE;
        }
        tbtU4Pause(cm, &w);
    }
}

/* The four entries of a tunnel (u4cm.c program_tunnel_paths; Guide 5.3.1
 * and 5.5.1, Table 5-2): HopIDs
 * allocated around everything live and held, credits from section 6.8's
 * accounting. `usb3` picks the adapters and weight; `reserveUsb3` keeps the
 * USB3 preference out of the PCIe path's credits. Each entry is recorded
 * as it is written. */
static LONG tbtU4Paths(PTBT_U4CM cm, PTBT_U4_PORT p, ULONG gen, PTBT_U4_TUNNEL t,
                       BOOLEAN usb3, BOOLEAN reserveUsb3)
{
    PTBT_U4_ROUTER h;
    PTBT_U4_ROUTER d;
    TBT_U4_USAGE uh;
    TBT_U4_USAGE ud;
    ULONG hp;
    ULONG dp;
    ULONG availH;
    ULONG availD;
    ULONG prefH;
    ULONG prefD;
    ULONG resH;
    ULONG resD;
    ULONG def;
    ULONG weight;
    ULONG busy[4];
    ULONG k;
    LONG rc;
    BOOLEAN asm7;

    h = &cm->HostRouter;
    d = &p->Dev;
    hp = usb3 ? p->Usb3Down : p->PcieDown;
    dp = tbtU4FirstOf(d, usb3 ? TBT_ADP_TYPE_USB3_UP : TBT_ADP_TYPE_PCIE_UP);
    weight = usb3 ? TBT_PATH_WEIGHT_USB3 : TBT_PATH_WEIGHT_PCIE;
    availH = 0;
    availD = 0;

    rc = tbtU4ScanLane(cm, h, p->Lane0, &uh);
    if (rc == TBT_OK) {
        rc = tbtU4ScanLane(cm, d, d->UpAdp, &ud);
    }
    if (rc == TBT_OK) {
        rc = tbtU4LaneAvail(cm, h, p->Lane0, &uh, &availH);
    }
    if (rc == TBT_OK) {
        rc = tbtU4LaneAvail(cm, d, d->UpAdp, &ud, &availD);
    }
    if (rc != TBT_OK) {
        p->Why = TBT_U4_WHY_PATH;
        return tbtU4Gone(cm, rc) ? TBT_E_GONE : rc;
    }
    /* hd: out of the host's lane 0, into the device's; hu the reverse.
     * A HopID live or held on either lane adapter, in either direction, is
     * skipped, and hu never equals hd: on an empty port that is 8 and 9,
     * as usb4cm and the pre-OS model use (record 22 section 6.8) - stricter
     * than the Guide's per-direction rule, never looser. */
    for (k = 0; k < 4; k++) {
        busy[k] = uh.InUse[k] | uh.OutUse[k] | ud.InUse[k] | ud.OutUse[k];
    }
    t->Hd = tbtU4AllocHop(h->Adp[p->Lane0].MaxOutHop, d->Adp[d->UpAdp].MaxInHop,
                          busy, busy);
    if (t->Hd != 0) {
        TBT_U4_BIT_SET(busy, t->Hd);
    }
    t->Hu = tbtU4AllocHop(d->Adp[d->UpAdp].MaxOutHop, h->Adp[p->Lane0].MaxInHop,
                          busy, busy);
    if (t->Hd == 0 || t->Hu == 0) {
        p->Why = TBT_U4_WHY_HOPID;
        tbtU4Log(cm, "u4.hopid.none", (t->Hd << 8) | t->Hu);
        return TBT_E_STATE;
    }

    /* Section 6.8: the router's preference (Buffer Allocation, Guide 5.1.4
     * step 4; else 32 bonded and 16 single, [linux]), bounded by
     * what the lane has left after reserving the USB3 preference when a
     * USB3 tunnel will follow; below 6, no tunnel. */
    def = p->Bonded ? TBT_CREDITS_BONDED : TBT_CREDITS_SINGLE;
    if (usb3) {
        prefH = p->HostBaUsb3 != 0 ? p->HostBaUsb3 : def;
        prefD = d->BaMaxUsb3 != 0 ? d->BaMaxUsb3 : def;
    } else {
        prefH = p->HostBaPcie != 0 ? p->HostBaPcie : def;
        prefD = d->BaMaxPcie != 0 ? d->BaMaxPcie : def;
    }
    resH = 0;
    resD = 0;
    if (reserveUsb3) {
        resH = p->HostBaUsb3 != 0 ? p->HostBaUsb3 : def;
        resD = d->BaMaxUsb3 != 0 ? d->BaMaxUsb3 : def;
    }
    availH = availH > resH ? availH - resH : 0;
    availD = availD > resD ? availD - resD : 0;
    t->CreditsHost = prefH < availH ? prefH : availH;
    t->CreditsDev = prefD < availD ? prefD : availD;
    /* PATH_CS_0's Initial Credits is 7 bits (bits 23:17, TBT_PATH0_CREDITS): a
     * router whose lanes report more buffers (the ASMedia ASM4242's 174,
     * as usb4-emu docs/USB4-HOSTS.md section 5 quirk c records) must never
     * have the figure truncated into the field. */
    if (t->CreditsHost > TBT_PATH_CREDITS_MAX) {
        t->CreditsHost = TBT_PATH_CREDITS_MAX;
    }
    if (t->CreditsDev > TBT_PATH_CREDITS_MAX) {
        t->CreditsDev = TBT_PATH_CREDITS_MAX;
    }
    if (t->CreditsHost < TBT_CREDITS_MIN_PCIE || t->CreditsDev < TBT_CREDITS_MIN_PCIE) {
        p->Why = TBT_U4_WHY_CREDITS;
        tbtU4Log(cm, "u4.credits.short", (t->CreditsHost << 16) | t->CreditsDev);
        return TBT_E_STATE;
    }

    asm7 = tbtU4Asm7(cm, d, usb3);

    /* Downstream, then upstream; each source adapter first (Guide 5.2.1). */
    /* Each entry is recorded before its write: one whose answer was lost
     * may still have been applied, and its teardown must cover it. */
    t->Written |= TBT_U4_E_HOST_PROTO;
    rc = tbtU4WriteEntry(cm, &h->Route, hp, TBT_HOP_PROTOCOL, p->Lane0, t->Hd,
                         h->Tbt3 ? TBT_TBT3_PROTO_CREDITS : 0, weight, FALSE,
                         h->Tbt3, asm7, "u4.asm7.host");
    if (rc == TBT_OK) {
        t->Written |= TBT_U4_E_DEV_LANE;
        rc = tbtU4WriteEntry(cm, &d->Route, d->UpAdp, t->Hd, dp, TBT_HOP_PROTOCOL,
                             t->CreditsDev, weight, TRUE, FALSE, FALSE, NULL);
    }
    if (rc == TBT_OK && TBT_U4_LIVE(p, gen)) {
        t->Written |= TBT_U4_E_DEV_PROTO;
        rc = tbtU4WriteEntry(cm, &d->Route, dp, TBT_HOP_PROTOCOL, d->UpAdp, t->Hu,
                             d->Tbt3 ? TBT_TBT3_PROTO_CREDITS : 0, weight, FALSE,
                             d->Tbt3, asm7, "u4.asm7.dev");
    }
    if (rc == TBT_OK && TBT_U4_LIVE(p, gen)) {
        t->Written |= TBT_U4_E_HOST_LANE;
        rc = tbtU4WriteEntry(cm, &h->Route, p->Lane0, t->Hu, hp, TBT_HOP_PROTOCOL,
                             t->CreditsHost, weight, TRUE, FALSE, FALSE, NULL);
    }
    if (!TBT_U4_LIVE(p, gen)) {
        return TBT_E_GONE;
    }
    if (rc != TBT_OK) {
        p->Why = TBT_U4_WHY_PATH;
        return tbtU4Gone(cm, rc) ? TBT_E_GONE : rc;
    }
    return TBT_OK;
}

/* Guide 5.3.1 step 1: the LTSSM of both PCIe adapters in Detect, the
 * host's PCIe Down first; on a Thunderbolt 3 router only the host's, the
 * USB4 end ([linux], record 22 section 8 item 5). */
static BOOLEAN tbtU4PcieDetect(PTBT_U4CM cm, PTBT_U4_PORT p, ULONG gen, ULONG dp)
{
    if (!tbtU4WaitDetect(cm, p, gen, &cm->HostRouter, p->PcieDown, TBT_U4_DETECT_US)) {
        return FALSE;
    }
    if (p->Dev.Tbt3) {
        return TRUE;
    }
    return tbtU4WaitDetect(cm, p, gen, &p->Dev, dp, TBT_U4_DETECT_US);
}

/*
 * Section 6.8: the PCIe tunnel, as Guide 5.3.1 sets one up. Step 1, the
 * Detect gate within TBT_U4_DETECT_US (tbtU4PcieDetect; section 10.1's
 * gate too); steps 2 and 3, the four entries (tbtU4Paths); step 4, on a
 * link of two version 2 routers, Extended Encapsulation (tbtU4EeSet,
 * record 22 section 6.2.2 item 7); steps 5 and 6,
 * Path Enable (ADP_PCIE_CS_0 bit 31) on the upstream PCIe adapter, the
 * device's, then on the host's downstream one. Then the host's LTSSM in
 * L0 within TBT_U4_L0_US (usb4-emu u4cm.c u4cm_pcie_tunnel). A failure
 * after any write tears down exactly what was written.
 */
static LONG tbtU4PcieTunnel(PTBT_U4CM cm, PTBT_U4_PORT p, ULONG gen)
{
    PTBT_U4_ROUTER h;
    PTBT_U4_ROUTER d;
    ULONG dp;
    ULONG cs0;
    LONG rc;
    TBT_WAIT w;

    /* Record 22 section 10.5: the port's isolation statement withdrawn
     * before anything below can enable its path. */
    if (p->Index >= 1 && p->Index <= TBT_U4_MAX_PORTS) {
        cm->IsoWord[p->Index - 1] = 0;
    }
    h = &cm->HostRouter;
    d = &p->Dev;
    dp = tbtU4FirstOf(d, TBT_ADP_TYPE_PCIE_UP);
    if (p->PcieDown == 0 || dp == 0 || h->Adp[p->PcieDown].CapAdp == 0 ||
        d->Adp[dp].CapAdp == 0) {
        p->Why = TBT_U4_WHY_NO_ADAPTERS;
        return TBT_E_STATE;
    }
    if (!tbtU4AdapterFree(cm, h, p->PcieDown)) {
        p->Why = TBT_U4_WHY_BLOCKED;
        return TBT_E_STATE;
    }
    tbtU4Zero(&p->Pcie, sizeof(p->Pcie));
    if (!tbtU4PcieDetect(cm, p, gen, dp)) {
        if (!TBT_U4_LIVE(p, gen)) {
            return TBT_E_GONE;
        }
        cm->Counters.DetectTimeouts++;
        p->Why = TBT_U4_WHY_DETECT;
        return TBT_E_TIMEOUT;
    }
    rc = tbtU4Paths(cm, p, gen, &p->Pcie, FALSE, tbtU4WantUsb3(cm, p));
    if (rc == TBT_OK && tbtU4V2Link(cm, p)) {
        /* Section 6.2.2 item 7: Extended Encapsulation on both adapters
         * before either Path Enable (Spec 8.2.2.7). */
        if (!tbtU4EeSet(cm, d, dp) || !tbtU4EeSet(cm, h, p->PcieDown)) {
            p->Why = TBT_U4_WHY_PATH;
            rc = TBT_E_STATE;
        } else {
            cm->Counters.EeSet++;
        }
    } else if (rc == TBT_OK) {
        /* Otherwise EE 0 (Spec 11.1.1.1's note): a version 2 end left
         * with it set - an earlier tunnel's clear that failed, or a
         * predecessor's - is cleared first, else no Path Enable. */
        if (!tbtU4EeClear(cm, h, p->PcieDown) || (!d->Tbt3 && !tbtU4EeClear(cm, d, dp))) {
            tbtU4Log(cm, "u4.ee.left", (p->Index << 8) | p->PcieDown);
            p->Why = TBT_U4_WHY_PATH;
            rc = TBT_E_STATE;
        }
    }
    if (rc == TBT_OK) {
        p->Pcie.Written |= TBT_U4_E_DEV_ENABLE;
        rc = tbtU4Rmw(cm, &d->Route, TBT_CS_ADAPTER, dp, d->Adp[dp].CapAdp + TBT_PCIE_CS_0,
                      0, TBT_PCIE0_PE);
    }
    if (rc == TBT_OK) {
        p->Pcie.Written |= TBT_U4_E_HOST_ENABLE;
        rc = tbtU4Rmw(cm, &h->Route, TBT_CS_ADAPTER, p->PcieDown,
                      h->Adp[p->PcieDown].CapAdp + TBT_PCIE_CS_0, 0, TBT_PCIE0_PE);
    }
    if (rc == TBT_OK) {
        TbtWaitStart(&w, cm->Host, TBT_U4_L0_US, FALSE);
        while ((rc = tbtU4Rd(cm, &h->Route, TBT_CS_ADAPTER, p->PcieDown,
                             h->Adp[p->PcieDown].CapAdp + TBT_PCIE_CS_0, &cs0)) == TBT_OK &&
               tbtU4Ltssm(cm, cs0) != TBT_LTSSM_L0) {
            if (!TBT_U4_LIVE(p, gen)) {
                rc = TBT_E_GONE;
                break;
            }
            if (TbtWaitDone(&w, cm->Host)) {
                p->Why = TBT_U4_WHY_L0;
                rc = TBT_E_TIMEOUT;
                break;
            }
            tbtU4Pause(cm, &w);
        }
    }
    if (rc == TBT_E_GONE || !TBT_U4_LIVE(p, gen)) {
        return TBT_E_GONE;
    }
    if (rc != TBT_OK) {
        cm->Counters.PcieFailed++;
        tbtU4Log(cm, "u4.pcie.failed", (p->Index << 8) | p->Why);
        (void)tbtU4TunnelDown(cm, p, &p->Pcie, FALSE);
        return rc;
    }
    p->Pcie.Up = TRUE;
    cm->Counters.PcieBuilt++;
    tbtU4Log(cm, "u4.pcie.up", (p->Index << 16) | (p->Pcie.Hd << 8) | p->Pcie.Hu);
    tbtU4Queue(cm, TBT_U4_ACT_TUNNEL_UP, p);
    return TBT_OK;
}

/*
 * USB3 bandwidth (Spec 8.2.2.5, Table 8-14). The Consumed and Allocated
 * fields of a USB3 Gen X adapter count the bytes of one 1 ms USB3 frame in
 * units of 512 * 2^Scale bytes, and one Mb/s is 125 bytes in 1 ms. A rate
 * becomes whole units rounded up, so an allocation is never short; with
 * the rate at most 80 Gb/s and the scale at most 16 every product stays in
 * 32 bits.
 */
static ULONG tbtU4BwUnits(ULONG mbps, ULONG scale)
{
    ULONG bytes;
    ULONG unit;
    ULONG n;

    bytes = mbps * 125UL;
    unit = 512UL << scale;
    n = bytes / unit;
    if (n * unit != bytes) {
        n++;
    }
    return n;
}

/* Units back to Mb/s, rounded to the nearest: `units` << scale blocks of
 * 512 bytes per ms are blocks * 512 / 125 Mb/s. Divided by 125 first and
 * the remainder scaled alone, so 4095 units at scale 16 stay in 32 bits;
 * the remainder's half never falls on a whole number, so no tie to break. */
static ULONG tbtU4BwMbps(ULONG units, ULONG scale)
{
    ULONG blocks;

    blocks = units << scale;
    return (blocks / 125UL) * 512UL + ((blocks % 125UL) * 512UL + 62UL) / 125UL;
}

#ifdef XHCI_HOST_TEST
ULONG TbtU4TestBwUnits(ULONG mbps, ULONG scale)
{
    return tbtU4BwUnits(mbps, scale);
}

ULONG TbtU4TestBwMbps(ULONG units, ULONG scale)
{
    return tbtU4BwMbps(units, scale);
}
#endif

/*
 * One half of Guide 6.2.2.2's negotiation (Figure 6-3) on the host's USB3
 * Down adapter: Connection Manager Request (ADP_USB3_GX_CS_2 bit 31) set,
 * or cleared, and Host Controller Ack (ADP_USB3_GX_CS_1 bit 31) awaited
 * until it reads the same. Scale, Allocated and Consumed are touched only
 * while the Ack reads 1. The Ack is given within TBT_U4_HCA_US (record 22
 * section 5.2) or the handshake times out.
 */
static LONG tbtU4BwRequest(PTBT_U4CM cm, PTBT_U4_PORT p, BOOLEAN on)
{
    PTBT_U4_ROUTER h;
    ULONG at;
    ULONG want;
    ULONG cs1;
    LONG rc;
    TBT_WAIT w;

    h = &cm->HostRouter;
    at = h->Adp[p->Usb3Down].CapAdp;
    want = on ? TBT_USB3_1_HCA : 0;
    rc = tbtU4Rmw(cm, &h->Route, TBT_CS_ADAPTER, p->Usb3Down, at + TBT_USB3_CS_2,
                  on ? 0 : TBT_USB3_2_CMR, on ? TBT_USB3_2_CMR : 0);
    if (rc != TBT_OK) {
        return rc;
    }
    TbtWaitStart(&w, cm->Host, TBT_U4_HCA_US, FALSE);
    while ((rc = tbtU4Rd(cm, &h->Route, TBT_CS_ADAPTER, p->Usb3Down,
                         at + TBT_USB3_CS_1, &cs1)) == TBT_OK) {
        if ((cs1 & TBT_USB3_1_HCA) == want) {
            break;
        }
        if (TbtWaitDone(&w, cm->Host)) {
            cm->Counters.HcaTimeouts++;
            rc = TBT_E_TIMEOUT;
            break;
        }
        tbtU4Pause(cm, &w);
    }
    return rc;
}

static ULONG tbtU4Larger(ULONG a, ULONG b)
{
    return a > b ? a : b;
}

/*
 * The allocation written while the Ack is held (Guide 6.2.2.1): the
 * Allocated fields (ADP_USB3_GX_CS_2 bits 11:0 up, 23:12 down) are 12
 * bits, so Scale (ADP_USB3_GX_CS_3) is the granularity the rates need:
 * the finest that still holds the larger of the two, found by refining
 * from 16 down while a finer one would hold it too (16 when even that is
 * short). Scale is written first, then the two fields at it.
 */
static LONG tbtU4WriteBw(PTBT_U4CM cm, PTBT_U4_PORT p, ULONG up, ULONG down)
{
    PTBT_U4_ROUTER h;
    ULONG at;
    ULONG scale;
    ULONG peak;
    LONG rc;

    if (up > TBT_U4_USB3_MAX_MBPS || down > TBT_U4_USB3_MAX_MBPS) {
        return TBT_E_PARAM;
    }
    h = &cm->HostRouter;
    at = h->Adp[p->Usb3Down].CapAdp;
    peak = tbtU4Larger(up, down);
    for (scale = 16; scale != 0 && tbtU4BwUnits(peak, scale - 1) <= 0xFFFUL; scale--) {
    }
    rc = tbtU4Wr(cm, &h->Route, TBT_CS_ADAPTER, p->Usb3Down, at + TBT_USB3_CS_3, scale);
    if (rc == TBT_OK) {
        rc = tbtU4Rmw(cm, &h->Route, TBT_CS_ADAPTER, p->Usb3Down, at + TBT_USB3_CS_2,
                      TBT_USB3_2_BW_MASK,
                      (tbtU4BwUnits(up, scale) & 0xFFFUL) |
                      ((tbtU4BwUnits(down, scale) & 0xFFFUL) << TBT_USB3_2_ADBW_SHIFT));
    }
    return rc;
}

/*
 * What the host controller consumes, in Mb/s each way, read while the Ack
 * is held: ADP_USB3_GX_CS_1's two fields at ADP_USB3_GX_CS_3's Scale, the
 * three DWs in one request. A Scale above 16, or a value no USB4 link
 * could carry, is not reinterpreted: the handshake is refused and nothing
 * is written.
 */
static LONG tbtU4Consumed(PTBT_U4CM cm, PTBT_U4_PORT p, PULONG up, PULONG down)
{
    PTBT_U4_ROUTER h;
    ULONG cs[3];
    ULONG scale;
    LONG rc;

    h = &cm->HostRouter;
    rc = tbtU4RdN(cm, &h->Route, TBT_CS_ADAPTER, p->Usb3Down,
                  h->Adp[p->Usb3Down].CapAdp + TBT_USB3_CS_1, 3, cs);
    if (rc != TBT_OK) {
        return rc;
    }
    scale = TBT_USB3_3_SCALE(cs[2]);
    if (scale > 16) {
        tbtU4Log(cm, "u4.usb3.scale", cs[2]);
        return TBT_E_STATE;
    }
    *up = tbtU4BwMbps(TBT_USB3_1_CUBW(cs[0]), scale);
    *down = tbtU4BwMbps(TBT_USB3_1_CDBW(cs[0]), scale);
    if (*up <= TBT_U4_USB3_MAX_MBPS && *down <= TBT_U4_USB3_MAX_MBPS) {
        return TBT_OK;
    }
    tbtU4Log(cm, "u4.usb3.consumed", cs[0]);
    return TBT_E_STATE;
}

/* Maximum Supported Link Rate (ADP_USB3_GX_CS_4 bits 18:12, Table 8-14):
 * 1h is 20 Gb/s, 0h 10 Gb/s. Table 8-14 reserves 2h to 7Fh; this driver
 * takes a reserved value as the lower rate, 10 Gb/s, as it did before
 * (record 22 section 6.9 step 2, [linux]). 0 when the register cannot be
 * read. */
static ULONG tbtU4MaxRate(PTBT_U4CM cm, const TBT_U4_ROUTER *rt, ULONG adp)
{
    ULONG cs4;

    if (tbtU4Rd(cm, &rt->Route, TBT_CS_ADAPTER, adp, rt->Adp[adp].CapAdp + TBT_USB3_CS_4,
                &cs4) != TBT_OK) {
        return 0;
    }
    if (TBT_USB3_4_MSLR(cs4) == TBT_USB3_MSLR_20G) {
        return 20000UL;
    }
    return 10000UL;
}

/*
 * The allocation handed back when the tunnel goes (Guide 6.2.2.2: never
 * below what is consumed), with TBT_U4_USB3_KEEP_MBPS each way as a floor
 * (record 22 section 7.3 step 4). The Request is cleared whatever happened
 * in between; an Ack that will not drop quarantines the adapter. TRUE when
 * the whole handshake completed.
 */
static BOOLEAN tbtU4ReleaseBw(PTBT_U4CM cm, PTBT_U4_PORT p)
{
    ULONG up;
    ULONG down;
    LONG rc;

    rc = tbtU4BwRequest(cm, p, TRUE);
    if (rc == TBT_OK) {
        rc = tbtU4Consumed(cm, p, &up, &down);
    }
    if (rc == TBT_OK) {
        rc = tbtU4WriteBw(cm, p, tbtU4Larger(TBT_U4_USB3_KEEP_MBPS, up),
                          tbtU4Larger(TBT_U4_USB3_KEEP_MBPS, down));
    }
    if (tbtU4BwRequest(cm, p, FALSE) != TBT_OK) {
        p->BwQuarantine = TRUE;
        p->BwReleaseOwed = TRUE;
        return FALSE;
    }
    /* A release that failed in between is owed past this record's end:
     * the idle tick tries the whole handshake again. */
    p->BwReleaseOwed = (BOOLEAN)(rc != TBT_OK);
    if (rc != TBT_OK) {
        return FALSE;
    }
    p->Usb3.Written &= ~TBT_U4_E_BANDWIDTH;
    return TRUE;
}

/*
 * The bandwidth a new USB3 Gen X path asks for, in Mb/s each way (Guide
 * 6.2.2.1 and 6.1.2.3; record 22 section 6.9 step 2): the lower of the two
 * adapters' Maximum Supported Link Rates, the host's first held to
 * section 6.10's quirk; 90 per cent of that; but no more than the link
 * can spare, 90 per cent of its raw bandwidth (Guide Table 6-2, from the
 * host lane's Current Link Speed and Negotiated Link Width) less the
 * 1.5 Gb/s the PCIe path keeps (Guide 6.1.2.3). The two rates are read
 * before the lane. TBT_E_STATE when the lane cannot be read.
 */
/* The USB3 allocation's bound from the host's lane 0 (record 22 section
 * 6.2.3 item 5, Guide Table 6-2): 10, 20 or 40 Gb/s a lane for Gen 2, 3 or
 * 4, doubled for x2; a Gen 4 asymmetric link counted as one lane's 40,
 * the lower direction, since the allocation is both ways; then 90% of it
 * less the 1.5 Gb/s the PCIe path keeps. */
static ULONG tbtU4LinkBound(ULONG lane)
{
    ULONG link;

    if (TBT_LANE1_SPEED(lane) == TBT_LANE_SPEED_GEN4) {
        link = 40000UL;
    } else if (TBT_LANE1_SPEED(lane) == TBT_LANE_SPEED_GEN3) {
        link = 20000UL;
    } else {
        link = 10000UL;
    }
    if (TBT_LANE1_NLW(lane) == TBT_LANE_WIDTH_X2) {
        link += link;
    }
    link = link * 9UL / 10UL;
    return link > TBT_U4_PCIE_RESERVE_MBPS ? link - TBT_U4_PCIE_RESERVE_MBPS : 0;
}

#ifdef XHCI_HOST_TEST
ULONG TbtU4TestLinkBound(ULONG lane)
{
    return tbtU4LinkBound(lane);
}
#endif

static LONG tbtU4Usb3Budget(PTBT_U4CM cm, PTBT_U4_PORT p, ULONG dp, PULONG mbps)
{
    ULONG hostRate;
    ULONG devRate;
    ULONG want;
    ULONG lane;
    ULONG link;

    hostRate = tbtU4MaxRate(cm, &cm->HostRouter, p->Usb3Down);
    if ((cm->HostRouter.Quirks & TBT_U4_QUIRK_USB3_MAX_BW) != 0 &&
        hostRate > TBT_U4_USB3_QUIRK_MBPS) {
        hostRate = TBT_U4_USB3_QUIRK_MBPS;
    }
    devRate = tbtU4MaxRate(cm, &p->Dev, dp);
    want = (devRate < hostRate ? devRate : hostRate) * 9UL / 10UL;
    if (tbtU4LaneRd(cm, &cm->HostRouter, p->Lane0, &lane) != TBT_OK) {
        return TBT_E_STATE;
    }
    link = tbtU4LinkBound(lane);
    *mbps = want < link ? want : link;
    return TBT_OK;
}

static BOOLEAN tbtU4Usb3Down(PTBT_U4CM cm, PTBT_U4_PORT p);

/*
 * Section 6.9: the USB3 tunnel - at least 500 ms after the port's last
 * USB3 teardown; the TMU's accuracy (or no TMU on the device); the
 * bandwidth handshake on the host's USB3 Down adapter; the four entries
 * at weight 2; Path Enable with Valid on the host's USB3 Down, then the
 * device's USB3 Up (Guide 5.5.1 steps 4 and 5). Every failure unwinds
 * what it acquired and leaves the PCIe tunnel untouched.
 */
static LONG tbtU4Usb3Tunnel(PTBT_U4CM cm, PTBT_U4_PORT p, ULONG gen)
{
    PTBT_U4_ROUTER h;
    PTBT_U4_ROUTER d;
    ULONG dp;
    ULONG budget;
    ULONG up;
    ULONG down;
    ULONG since;
    LONG rc;

    h = &cm->HostRouter;
    d = &p->Dev;
    dp = tbtU4FirstOf(d, TBT_ADP_TYPE_USB3_UP);
    if (dp == 0 || h->Adp[p->Usb3Down].CapAdp == 0 || d->Adp[dp].CapAdp == 0) {
        return TBT_E_STATE;
    }
    if (p->BwQuarantine || p->BwReleaseOwed || !tbtU4AdapterFree(cm, h, p->Usb3Down) ||
        !p->TmuOk || !cm->HostTmuOk) {
        tbtU4Log(cm, "u4.usb3.not-now", p->Index);
        return TBT_E_STATE;
    }
    if (p->Usb3GapValid) {
        since = tbtU4Now(cm) - p->LastUsb3TeardownUs;
        if (since < TBT_U4_USB3_GAP_US + cm->Host->TickUs) {
            tbtU4WaitMin(cm, TBT_U4_USB3_GAP_US + cm->Host->TickUs - since);
        }
    }
    if (!TBT_U4_LIVE(p, gen)) {
        return TBT_E_GONE;
    }
    tbtU4Zero(&p->Usb3, sizeof(p->Usb3));

    if (tbtU4Usb3Budget(cm, p, dp, &budget) != TBT_OK) {
        return TBT_E_STATE;
    }
    /* The handshake: never below what the host controller consumes. */
    rc = tbtU4BwRequest(cm, p, TRUE);
    if (rc == TBT_OK) {
        rc = tbtU4Consumed(cm, p, &up, &down);
    }
    if (rc == TBT_OK) {
        p->Usb3.Written |= TBT_U4_E_BANDWIDTH;
        rc = tbtU4WriteBw(cm, p, tbtU4Larger(budget, up), tbtU4Larger(budget, down));
    }
    if (tbtU4BwRequest(cm, p, FALSE) != TBT_OK) {
        p->BwQuarantine = TRUE;
        if (rc == TBT_OK) {
            rc = TBT_E_TIMEOUT;
        }
    }
    if (rc == TBT_OK) {
        rc = tbtU4Paths(cm, p, gen, &p->Usb3, TRUE, FALSE);
    }
    if (rc == TBT_OK) {
        p->Usb3.Written |= TBT_U4_E_HOST_ENABLE;
        rc = tbtU4Rmw(cm, &h->Route, TBT_CS_ADAPTER, p->Usb3Down,
                      h->Adp[p->Usb3Down].CapAdp + TBT_USB3_CS_0, 0,
                      TBT_USB3_0_PE | TBT_USB3_0_V);
    }
    if (rc == TBT_OK) {
        p->Usb3.Written |= TBT_U4_E_DEV_ENABLE;
        rc = tbtU4Rmw(cm, &d->Route, TBT_CS_ADAPTER, dp, d->Adp[dp].CapAdp + TBT_USB3_CS_0,
                      0, TBT_USB3_0_PE | TBT_USB3_0_V);
    }
    if (rc == TBT_OK && !TBT_U4_LIVE(p, gen)) {
        return TBT_E_GONE;
    }
    if (rc == TBT_E_GONE) {
        return rc;
    }
    if (rc != TBT_OK) {
        cm->Counters.Usb3Failed++;
        p->Usb3Failed = TRUE;
        tbtU4Log(cm, "u4.usb3.failed", (ULONG)rc);
        (void)tbtU4Usb3Down(cm, p);
        return rc;
    }
    p->Usb3.Up = TRUE;
    cm->Counters.Usb3Built++;
    tbtU4Log(cm, "u4.usb3.up", (p->Index << 16) | (p->Usb3.Hd << 8) | p->Usb3.Hu);
    return TBT_OK;
}

/* A USB3 tunnel down with both ends reachable (Guide 5.5.2; the eject,
 * a cleaning, a failed construction): the tunnel, the 500 ms gap started,
 * the bandwidth released, and the link's TMU, which this driver enabled
 * for USB3 alone, disabled while both routers are there (section 6.6 item
 * 6). TRUE when every step was proven. */
static BOOLEAN tbtU4Usb3Down(PTBT_U4CM cm, PTBT_U4_PORT p)
{
    BOOLEAN ok;

    ok = tbtU4TunnelDown(cm, p, &p->Usb3, TRUE);
    if ((p->Usb3.Written & TBT_U4_E_BANDWIDTH) != 0 && !tbtU4ReleaseBw(cm, p)) {
        ok = FALSE;
    }
    if (p->TmuConfigured && !p->TmuEnhanced && p->Dev.Present &&
        tbtU4TmuDisable(cm, p) != TBT_OK) {
        /* An enhanced link's TMU stays for the connection's life
         * (section 6.2.2 item 6). */
        ok = FALSE;
    }
    return ok;
}

/* XhciCmUsb3Tunnel 0: both USB3 adapters disconnected, Path Enable 0 with
 * Valid 1 (Guide 5.5.3), on a router this driver configures. */
static VOID tbtU4Usb3Disconnect(PTBT_U4CM cm, PTBT_U4_PORT p)
{
    ULONG dp;

    dp = tbtU4FirstOf(&p->Dev, TBT_ADP_TYPE_USB3_UP);
    if (p->Usb3Down != 0 && cm->HostRouter.Adp[p->Usb3Down].CapAdp != 0) {
        (void)tbtU4Rmw(cm, &cm->HostRouter.Route, TBT_CS_ADAPTER, p->Usb3Down,
                       cm->HostRouter.Adp[p->Usb3Down].CapAdp + TBT_USB3_CS_0,
                       TBT_USB3_0_PE, TBT_USB3_0_V);
    }
    if (dp != 0 && p->Dev.Adp[dp].CapAdp != 0) {
        (void)tbtU4Rmw(cm, &p->Dev.Route, TBT_CS_ADAPTER, dp,
                       p->Dev.Adp[dp].CapAdp + TBT_USB3_CS_0, TBT_USB3_0_PE, TBT_USB3_0_V);
    }
    cm->Counters.Usb3Disconnected++;
}

static VOID tbtU4DropLinks(PTBT_U4CM cm, PTBT_U4_PORT p);

/* ---------------------------------------------------------------------- */
/* Unplug: the teardown (record 22 section 7.3)                           */
/* ---------------------------------------------------------------------- */

/*
 * Step 1, the host side of the PCIe tunnel (Guide 3.4.1, 5.3.2, 5.2.2):
 * the host's PCIe Down Path Enable read and written 0 if set; Valid
 * cleared on its HopID 8 entry and on the host lane 0's Hu entry, each
 * drained; then read back - Path Enable 0, both entries invalid and
 * drained, the host adapter's LTSSM in Detect within 500 ms. TRUE when
 * every request succeeded and the read-back matched.
 */
static BOOLEAN tbtU4PcieHostDown(PTBT_U4CM cm, PTBT_U4_PORT p)
{
    PTBT_U4_ROUTER h;
    ULONG cap;
    BOOLEAN ok;

    h = &cm->HostRouter;
    cap = h->Adp[p->PcieDown].CapAdp;
    ok = tbtU4Disable(cm, p->Index, &h->Route, p->PcieDown, cap, FALSE);
    /* Section 6.2.2 item 7: on a version 2 host router its Extended
     * Encapsulation too, a failure logged. */
    (void)tbtU4EeClear(cm, h, p->PcieDown);
    if ((p->Pcie.Written & TBT_U4_E_HOST_PROTO) != 0) {
        ok &= tbtU4Invalidate(cm, p->Index, &h->Route, p->PcieDown, TBT_HOP_PROTOCOL);
    }
    if ((p->Pcie.Written & TBT_U4_E_HOST_LANE) != 0) {
        ok &= tbtU4Invalidate(cm, p->Index, &h->Route, p->Lane0, p->Pcie.Hu);
    }
    if (p->Pcie.Incomplete) {
        /* An entry never read is never proven closed until the sweep
         * finds and closes it. */
        if (tbtU4HostSweep(cm, p, TRUE, FALSE)) {
            p->Pcie.Incomplete = FALSE;
        } else {
            ok = FALSE;
            p->HostResidual = TRUE;
        }
    }
    if (ok) {
        ok = tbtU4WaitDetect(cm, NULL, 0, h, p->PcieDown, TBT_U4_DETECT_US);
    }
    p->Pcie.Written = 0;
    p->Pcie.Up = FALSE;
    p->Pcie.Preserved = FALSE;
    p->Pcie.Own = FALSE;
    return ok;
}

/* Steps 2 and 3: the driver's PCI-side reading and the statement, with
 * the anchor and what is left of the 3 s (section 10.3). */
static VOID tbtU4LinkDownUpcall(PTBT_U4CM cm, PTBT_U4_PORT p, BOOLEAN closed,
                                BOOLEAN nested)
{
    TBT_U4_LINKDOWN info;
    ULONG age;
    ULONG r;

    if (tbtU4Off(cm) || cm->Recovering) {
        /* Section 7.7: nothing read after the loss proves a path closed;
         * the driver reports presence lost itself, and a recovery's drain
         * makes no statement (section 10.2). */
        cm->Counters.StatementsWithheld++;
        return;
    }
    /* Record 22 section 10.5: a path read back closed is that port's
     * isolation statement, before the upcall (whatever it decides). */
    if (closed && p->Index >= 1 && p->Index <= TBT_U4_MAX_PORTS) {
        cm->IsoWord[p->Index - 1] = TBT_U4_ISO_WORD(p->Generation);
    }
    info.Port = p->Index;
    info.Kid = 0;
    info.Generation = p->Generation;
    info.PcieDown = p->PcieDown;
    info.MapValid = p->MapValid;
    info.MapRid = p->MapRid;
    info.PathClosed = closed;
    info.Nested = nested;
    info.AnchorValid = (BOOLEAN)(p->AnchorValid && p->AnchorGeneration == p->Generation);
    info.AnchorUs = p->AnchorUs;
    info.RemainingUs = 0;
    if (info.AnchorValid) {
        age = tbtU4Now(cm) - p->AnchorUs;
        info.RemainingUs = age < TBT_U4_STATEMENT_US ? TBT_U4_STATEMENT_US - age : 0;
    }
    r = TBT_U4_STMT_WITHHELD;
    if (cm->LinkDown != NULL) {
        r = cm->LinkDown(cm->LinkDownContext, &info);
    }
    if (r == TBT_U4_STMT_SENT) {
        cm->Counters.StatementsSent++;
    } else if (r == TBT_U4_STMT_PENDING) {
        cm->Counters.StatementsPending++;
    } else {
        cm->Counters.StatementsWithheld++;
    }
}

/* Steps 1 to 3. */
static VOID tbtU4TeardownHead(PTBT_U4CM cm, PTBT_U4_PORT p, BOOLEAN nested)
{
    BOOLEAN hadPcie;
    BOOLEAN closed;

    /* Record 35 section 3.8: an inter-domain peer ends before anything of
     * the port is restored (its paths before its lanes) - unless its own
     * engine is running, whose record is then ended by the generation
     * check (tbtU4XdStale) before the tick touches the port again. */
    if (cm->XdIn != p->Index) {
        tbtU4XdEnd(cm, p, TRUE);
    }
    /* Record 34 section 6.8: the kids below go first, with no register
     * work - their paths lived on the routers that left. */
    tbtU4KidsDrop(cm, p);
    p->State = TBT_U4_TEARDOWN;
    cm->Counters.Unplugs++;
    /* Section 6.2.3 item 4 part 3: the device router's end goes with its
     * reset; the host's (ElrHost) is section 7.3 step 5's. */
    p->ElrSet = FALSE;
    p->ElrDev = FALSE;
    p->ElrCheckOwed = FALSE;
    hadPcie = (BOOLEAN)(p->Pcie.Up || p->Pcie.Written != 0);
    /* A removal already owed (an earlier head whose tail has not run)
     * stays owed: only the tail consumes it. */
    if (hadPcie && p->PcieDown != 0) {
        closed = tbtU4PcieHostDown(cm, p);
        cm->Counters.PcieTornDown++;
        tbtU4LinkDownUpcall(cm, p, closed, nested);
        p->RemovalOwed = TRUE;
    } else if (p->Pcie.Incomplete) {
        /* No PCIe path found, but a host entry was never read
         * (tbtU4Follow): what it may hold is swept from the registers,
         * with no removal and no path-closed statement - no PCIe tunnel
         * was found to state closed (sections 7.3, 9.2). Until a sweep
         * proves it, the port owes one before it connects again. */
        if (!tbtU4HostSweep(cm, p, TRUE, FALSE)) {
            p->HostResidual = TRUE;
        }
    }
    p->Pcie.Written = 0;
    p->Pcie.Up = FALSE;
}

/*
 * Steps 4 to 6 of section 7.3: the host side of the USB3 tunnel (Guide
 * 5.5.2, the removed router's step skipped) - Path Enable 0 with Valid 1,
 * its two host-side entries invalidated and drained, the bandwidth
 * released; the port's lane adapters back to their defaults where this
 * driver changed them (Guide 3.4.1: the host port's TMU
 * EnableUniDirectionalMode 0, with Disable Time Sync 1 as at a new
 * connection, Guide 7.4.3.1; USB4 Port is Configured; and section 7.3
 * step 5's Target Link Width, Lane Bonding and Lane Disable,
 * tbtU4HostRestore); then the removal queued for the OS.
 *
 * tbtU4HostTmuDefaults is its TMU part: on a link whose TMU this driver
 * configured, the host's lane 0 adapter back to Guide 3.4.1's default
 * mode and Disable Time Sync set ([linux], record 22 section 6.6 item 6);
 * a write that fails is the port's TMU quarantine.
 */
static VOID tbtU4HostTmuDefaults(PTBT_U4CM cm, PTBT_U4_PORT p)
{
    PTBT_U4_ROUTER h;

    h = &cm->HostRouter;
    if (p->TmuConfigured) {
        /* A version 2 host router's lane 0 also leaves enhanced
         * uni-directional mode (section 6.6 item 6). */
        if (tbtU4TmuAdp(cm, h, p->Lane0, TBT_TMU_ADP_CS_3, TBT_TMU_ADP3_UDM, 0) != TBT_OK ||
            (tbtU4IsV2(h) && tbtU4TmuEnhancedOff(cm, h, p->Lane0) != TBT_OK) ||
            tbtU4TmuAdp(cm, h, p->Lane0, TBT_TMU_ADP_CS_6, 0, TBT_TMU_ADP6_DTS) != TBT_OK) {
            p->TmuQuarantine = TRUE;
        }
        p->TmuConfigured = FALSE;
    }
    p->TmuOk = FALSE;
}

static VOID tbtU4TeardownTail(PTBT_U4CM cm, PTBT_U4_PORT p)
{
    PTBT_U4_ROUTER h;
    BOOLEAN removal;

    h = &cm->HostRouter;
    removal = p->RemovalOwed;
    p->RemovalOwed = FALSE;
    tbtU4DropLinks(cm, p);
    if ((p->Usb3.Up || (p->Usb3.Written & ~TBT_U4_E_BANDWIDTH) != 0) && p->Usb3Down != 0) {
        (void)tbtU4Disable(cm, p->Index, &h->Route, p->Usb3Down, h->Adp[p->Usb3Down].CapAdp,
                           TRUE);
        if ((p->Usb3.Written & TBT_U4_E_HOST_PROTO) != 0) {
            (void)tbtU4Invalidate(cm, p->Index, &h->Route, p->Usb3Down, TBT_HOP_PROTOCOL);
        }
        if ((p->Usb3.Written & TBT_U4_E_HOST_LANE) != 0) {
            (void)tbtU4Invalidate(cm, p->Index, &h->Route, p->Lane0, p->Usb3.Hu);
        }
        if (p->Usb3.Incomplete && !tbtU4HostSweep(cm, p, FALSE, TRUE)) {
            p->HostResidual = TRUE;     /* swept before the next connection */
        }
        cm->Counters.Usb3TornDown++;
        p->LastUsb3TeardownUs = tbtU4Now(cm);
        p->Usb3GapValid = TRUE;
    } else if (p->Usb3.Incomplete && p->Usb3Down != 0) {
        /* As the head's PCIe case: no USB3 path found, an entry unread;
         * swept, or owed. Whatever the sweep closed starts the gap. */
        if (!tbtU4HostSweep(cm, p, FALSE, TRUE)) {
            p->HostResidual = TRUE;
        }
        p->LastUsb3TeardownUs = tbtU4Now(cm);
        p->Usb3GapValid = TRUE;
    }
    if ((p->Usb3.Written & TBT_U4_E_BANDWIDTH) != 0 && p->Usb3Down != 0) {
        (void)tbtU4ReleaseBw(cm, p);    /* owed (BwReleaseOwed) if it fails */
    }
    tbtU4Zero(&p->Usb3, sizeof(p->Usb3));
    tbtU4HostTmuDefaults(cm, p);
    (void)tbtU4HostRestore(cm, p);
    TbtCtlForget(&cm->Ctl, &p->Dev.Route);
    tbtU4QDropPort(cm, p);
    tbtU4Zero(&p->Dev, sizeof(p->Dev));
    tbtU4Zero(&p->Pcie, sizeof(p->Pcie));
    p->Bonded = FALSE;
    p->SingleLane = FALSE;
    p->PcSetDev = FALSE;
    p->Unlocked = FALSE;
    tbtU4XdEarlyEnd(cm, p);
    p->PcieFailed = FALSE;
    p->Usb3Failed = FALSE;
    p->Ejected = FALSE;
    p->DpPreserved = FALSE;
    p->Transitional = FALSE;
    p->AnchorValid = FALSE;
    p->UnplugPending = FALSE;
    p->LostKnown = FALSE;
    p->RelockSeen = FALSE;
    p->HostCloseOwed = FALSE;
    p->ClassifyPending = FALSE;
    p->State = TBT_U4_EMPTY;
    p->Why = TBT_U4_WHY_NONE;
    p->CheckPending = TRUE;
    if (removal) {
        tbtU4Queue(cm, TBT_U4_ACT_TUNNEL_DOWN, p);
    }
}

/* The host's lane adapters and USB4 port back to what this driver found
 * (section 7.3 step 5): each change it made stays owned until a read-back
 * proves it undone, and is retried by the next teardown, the next
 * connection and the idle tick. TRUE when nothing is left owed. */
/* A Thunderbolt host's link controller port reset released (PORT_MODE bit
 * 0 cleared) and read back clear; TRUE when nothing is owed. */
static BOOLEAN tbtU4LcDprRelease(PTBT_U4CM cm, PTBT_U4_PORT p)
{
    ULONG region;
    ULONG v;

    if (!p->LcDprOwed) {
        return TRUE;
    }
    if (tbtU4HostLcPort(cm, p->Lane0, &region) != TBT_OK) {
        return FALSE;
    }
    if (region == 0 ||
        (tbtU4Rmw(cm, &cm->HostRouter.Route, TBT_CS_ROUTER, 0, region + TBT_LC_PORT_MODE,
                  TBT_LC_PORT_MODE_DPR, 0) == TBT_OK &&
         tbtU4Rd(cm, &cm->HostRouter.Route, TBT_CS_ROUTER, 0, region + TBT_LC_PORT_MODE,
                 &v) == TBT_OK && (v & TBT_LC_PORT_MODE_DPR) == 0)) {
        p->LcDprOwed = FALSE;
    }
    return (BOOLEAN)!p->LcDprOwed;
}

static BOOLEAN tbtU4HostRestore(PTBT_U4CM cm, PTBT_U4_PORT p)
{
    PTBT_U4_ROUTER h;
    ULONG v;
    ULONG k;
    ULONG region;
    ULONG mark;
    BOOLEAN ok;

    h = &cm->HostRouter;
    if (p->PcSetHost && h->Tbt3) {
        mark = (p->Lane0 & 1UL) != 0 ? TBT_LC_SX_CTRL_L1C : TBT_LC_SX_CTRL_L2C;
        if (tbtU4HostLcPort(cm, p->Lane0, &region) != TBT_OK) {
            /* still owed: retried by the next teardown or tick */
        } else if (region == 0 ||
            (tbtU4Rmw(cm, &h->Route, TBT_CS_ROUTER, 0, region + TBT_LC_SX_CTRL, mark, 0) ==
                 TBT_OK &&
             tbtU4Rd(cm, &h->Route, TBT_CS_ROUTER, 0, region + TBT_LC_SX_CTRL, &v) == TBT_OK &&
             (v & mark) == 0)) {
            p->PcSetHost = FALSE;
        }
    } else if (p->PcSetHost && h->Adp[p->Lane0].CapPort != 0 &&
        tbtU4Rmw(cm, &h->Route, TBT_CS_ADAPTER, p->Lane0,
                 h->Adp[p->Lane0].CapPort + TBT_PORT_CS_19, TBT_PORT19_PC, 0) == TBT_OK &&
        tbtU4Rd(cm, &h->Route, TBT_CS_ADAPTER, p->Lane0,
                h->Adp[p->Lane0].CapPort + TBT_PORT_CS_19, &v) == TBT_OK &&
        (v & TBT_PORT19_PC) == 0) {
        p->PcSetHost = FALSE;
    }
    if (p->LaneWritten) {
        ok = TRUE;
        for (k = 0; k < 2; k++) {
            if (tbtU4LaneRmw(cm, h, p->Lane0 + k, TBT_LANE1_TLW_MASK | TBT_LANE1_LB,
                             p->LaneSaved[k] & (TBT_LANE1_TLW_MASK | TBT_LANE1_LB)) != TBT_OK ||
                tbtU4LaneRd(cm, h, p->Lane0 + k, &v) != TBT_OK ||
                (v & TBT_LANE1_TLW_MASK) != (p->LaneSaved[k] & TBT_LANE1_TLW_MASK)) {
                ok = FALSE;
            }
        }
        if (ok) {
            p->LaneWritten = FALSE;
        }
    }
    if (p->Lane1Disabled &&
        tbtU4LaneRmw(cm, h, p->Lane0 + 1, TBT_LANE1_LD, 0) == TBT_OK &&
        tbtU4LaneRd(cm, h, p->Lane0 + 1, &v) == TBT_OK && (v & TBT_LANE1_LD) == 0) {
        p->Lane1Disabled = FALSE;
    }
    (void)tbtU4LcDprRelease(cm, p);
    /* Section 6.2.3 item 4 part 6: the host's Enable Gen 4 Link Recovery,
     * which a disconnect leaves (Spec 4.4.5.2); retried at the EMPTY tick
     * while it fails, never part of the verdict that gates a connection. */
    if (p->ElrHost && !p->ElrSet) {
        if (tbtU4ElrEnd(cm, h, p->Lane0, FALSE) == TBT_OK) {
            p->ElrHost = FALSE;
            cm->Counters.ElrOwnCleared++;
        } else {
            cm->Counters.ElrClearFailed++;
        }
    }
    return (BOOLEAN)(!p->PcSetHost && !p->LaneWritten && !p->Lane1Disabled &&
                     !p->LcDprOwed);
}

static VOID tbtU4Teardown(PTBT_U4CM cm, PTBT_U4_PORT p)
{
    if (p->Index != 0 && p->Index <= TBT_U4_MAX_PORTS && cm->DeferredTail[p->Index - 1]) {
        /* Its head already ran, nested at a safe point: the tail once. */
        cm->DeferredTail[p->Index - 1] = 0;
    } else {
        tbtU4TeardownHead(cm, p, FALSE);
    }
    tbtU4TeardownTail(cm, p);
}

/* ---------------------------------------------------------------------- */
/* The one truth (record 22 sections 7.1, 7.4)                            */
/* ---------------------------------------------------------------------- */

/* Removal evidence from the registers: lane 0 in CLd, Lock set again on a
 * port this driver unlocked (or found unlocked), the device router's
 * TopologyID Valid clear or its UUID changed, or ERR_CONN / ERR_LOCK on its
 * route. `cls` gets lane 0's class. */
static BOOLEAN tbtU4Evidence(PTBT_U4CM cm, PTBT_U4_PORT p, PULONG cls)
{
    PTBT_U4_ROUTER h;
    PTBT_U4_ROUTER d;
    ULONG lane;
    ULONG cs4;
    ULONG v[2];
    ULONG u[4];
    LONG rc;

    h = &cm->HostRouter;
    d = &p->Dev;
    *cls = TBT_U4_LANE_UNKNOWN;
    if (tbtU4LaneRd(cm, h, p->Lane0, &lane) != TBT_OK) {
        return FALSE;
    }
    if (TbtU4LaneClass(TBT_LANE1_STATE(lane)) == TBT_U4_LANE_DISCONNECTED) {
        /* Lane 0 read disconnected, whatever the port's state: an early
         * UUID answer held for the link it came with is owed to nobody
         * (a later connection brings its own request). */
        tbtU4XdEarlyDrop(cm, p);
    }
    if (tbtU4Rd(cm, &h->Route, TBT_CS_ADAPTER, p->Lane0, TBT_ADP_CS_4, &cs4) != TBT_OK) {
        return FALSE;
    }
    *cls = TbtU4LaneClass(TBT_LANE1_STATE(lane));
    if (*cls == TBT_U4_LANE_DISCONNECTED) {
        return TRUE;
    }
    if (p->ClassifyPending && d->Present && (cs4 & TBT_ADP4_LOCK) != 0) {
        /* A port a resume left unclassified, Lock still set (its clear
         * failed then): the re-lock kept, lane 0 unlocked and read back
         * before anything counts - the router cannot be read through a
         * Lock (ERR_LOCK). Not cleared: still unresolved, no evidence. */
        p->RelockSeen = TRUE;
        if (tbtU4Wr(cm, &h->Route, TBT_CS_ADAPTER, p->Lane0, TBT_ADP_CS_4,
                    cs4 & ~TBT_ADP4_LOCK) != TBT_OK ||
            tbtU4Rd(cm, &h->Route, TBT_CS_ADAPTER, p->Lane0, TBT_ADP_CS_4, &cs4) != TBT_OK ||
            (cs4 & TBT_ADP4_LOCK) != 0) {
            return FALSE;
        }
    }
    if (p->Unlocked && (cs4 & TBT_ADP4_LOCK) != 0) {
        return TRUE;
    }
    if (p->ClassifyPending && d->Present) {
        /* A resume left this port HELD without classifying it: resolved
         * here first. The same router with TopologyID Valid clear is the
         * known configuration loss (no evidence); another identity is
         * removal; still configured, a kept re-lock decides below. */
        rc = tbtU4Rd(cm, &d->Route, TBT_CS_ROUTER, 0, TBT_ROUTER_CS_3, &v[0]);
        if (tbtU4Gone(cm, rc)) {
            return TRUE;
        }
        if (rc != TBT_OK) {
            return FALSE;       /* still unresolved: HELD, as it is */
        }
        if ((v[0] & TBT_RTR3_V) == 0) {
            if (!d->UuidValid) {
                return FALSE;
            }
            if (!d->Tbt3) {
                rc = tbtU4RdN(cm, &d->Route, TBT_CS_ROUTER, 0, TBT_ROUTER_CS_7, 2, u);
                u[2] = d->Uuid[2];
                u[3] = d->Uuid[3];
            } else if (d->CapLc != 0 || d->UidFromDrom) {
                rc = tbtU4TbtUuid(cm, d, u);
            } else {
                return FALSE;
            }
            if (tbtU4Gone(cm, rc)) {
                return TRUE;
            }
            if (rc != TBT_OK) {
                return FALSE;
            }
            if (u[0] != d->Uuid[0] || u[1] != d->Uuid[1] || u[2] != d->Uuid[2] ||
                u[3] != d->Uuid[3]) {
                return TRUE;
            }
            p->ClassifyPending = FALSE;
            p->LostKnown = TRUE;
            p->RelockSeen = FALSE;
            return FALSE;
        }
        p->ClassifyPending = FALSE;
    }
    if (p->RelockSeen && !p->LostKnown) {
        return TRUE;    /* seen by a resume that could not finish */
    }
    if (!d->Present) {
        return FALSE;
    }
    rc = tbtU4Rd(cm, &d->Route, TBT_CS_ROUTER, 0, TBT_ROUTER_CS_3, &v[0]);
    if (tbtU4Gone(cm, rc)) {
        return TRUE;
    }
    if (rc == TBT_OK && (v[0] & TBT_RTR3_V) == 0 && !p->LostKnown) {
        return TRUE;
    }
    if (!d->Tbt3 && rc == TBT_OK) {
        rc = tbtU4RdN(cm, &d->Route, TBT_CS_ROUTER, 0, TBT_ROUTER_CS_7, 2, u);
        u[2] = 0;
        u[3] = 0;
    } else if (d->Tbt3 && rc == TBT_OK && d->CapLc != 0) {
        rc = tbtU4RdN(cm, &d->Route, TBT_CS_ROUTER, 0, d->CapLc + TBT_LC_FUSE, 4, u);
    } else if (d->Tbt3 && rc == TBT_OK && d->UidFromDrom) {
        /* Generation 1: its DROM UID, the recorded source (record 22
         * section 19.4 item 3a). */
        rc = tbtU4TbtUuid(cm, d, u);
    } else {
        return FALSE;
    }
    if (tbtU4Gone(cm, rc)) {
        return TRUE;
    }
    if (rc != TBT_OK) {
        return FALSE;
    }
    if (!d->UuidValid) {
        /* Never compared against a sample that failed: this one becomes
         * the identity. */
        d->Uuid[0] = u[0];
        d->Uuid[1] = u[1];
        d->Uuid[2] = u[2];
        d->Uuid[3] = u[3];
        d->UuidValid = TRUE;
        return FALSE;
    }
    return (BOOLEAN)(u[0] != d->Uuid[0] || u[1] != d->Uuid[1] ||
                     u[2] != d->Uuid[2] || u[3] != d->Uuid[3]);
}

/* ---------------------------------------------------------------------- */
/* Connecting a port (record 22 sections 6.4 to 6.9, 7.2, 8)             */
/* ---------------------------------------------------------------------- */

/*
 * EMPTY to UP (or FAILED, UNSUPPORTED). A router already enumerated is
 * taken as found by tbtU4Router's own enumeration-state check; `asFound`
 * builds a missing PCIe tunnel on the link as found (section 9.2's
 * DisplayPort case: no bonding, no lane change, no USB3). A port whose
 * host side an earlier teardown could not restore or read is not
 * connected until that is proven done (the next check retries).
 */
/* No entry of the host's lane adapters of the port in use, as input or as
 * output, from HopID 8 up (the registers and the quarantine). FALSE when
 * anything cannot be read. */
static BOOLEAN tbtU4LanesIdle(PTBT_U4CM cm, PTBT_U4_PORT p)
{
    TBT_U4_USAGE u;
    ULONG k;
    ULONG i;

    for (k = 0; k < 2; k++) {
        if (p->Lane0 + k >= TBT_U4_MAX_ADP || !cm->HostRouter.Adp[p->Lane0 + k].Present) {
            continue;
        }
        if (tbtU4ScanLane(cm, &cm->HostRouter, p->Lane0 + k, &u) != TBT_OK) {
            return FALSE;
        }
        for (i = 0; i < 4; i++) {
            if ((u.InUse[i] | u.OutUse[i]) != 0) {
                return FALSE;
            }
        }
    }
    return TRUE;
}

static VOID tbtU4Connect(PTBT_U4CM cm, PTBT_U4_PORT p, BOOLEAN asFound)
{
    ULONG gen;
    ULONG next;
    ULONG lane;
    ULONG cls;
    BOOLEAN pcieWanted;
    BOOLEAN gone;
    LONG rc;
    TBT_WAIT w;

    pcieWanted = FALSE;
    next = TBT_U4_FAILED;
    /* What an earlier teardown could not restore or read on the host is
     * tried again; a new connection never writes over it. */
    if (!asFound && (!tbtU4HostRestore(cm, p) || !tbtU4ResidualClear(cm, p) ||
                     !tbtU4LanesIdle(cm, p))) {
        /* Guide 7.1: bonding (and every lane change) only with no path
         * enabled through the port. Paths someone else keeps there are
         * left alone; the port waits for them (the next check retries). */
        tbtU4Log(cm, "u4.connect.deferred", p->Index);
        cm->Counters.ConnectsDeferred++;
        if (cm->Budgeted) {
            p->State = TBT_U4_HELD;
            p->Why = TBT_U4_WHY_RESUME;
        } else {
            p->State = TBT_U4_EMPTY;
        }
        p->CheckPending = TRUE;
        return;
    }
    cm->Counters.Connects++;
    p->Generation++;
    gen = p->Generation;
    p->UnplugPending = FALSE;
    p->Gen4 = FALSE;
    p->Asym = FALSE;
    p->UtoOff = FALSE;
    /* Section 6.2.3 item 4 part 3: per connection; ElrHost outlives it. */
    p->ElrSet = FALSE;
    p->ElrRefused = FALSE;
    p->ElrCheckOwed = FALSE;
    p->ElrCheckTried = FALSE;
    p->State = TBT_U4_CONNECTING;
    p->Why = TBT_U4_WHY_NONE;
    p->AnchorValid = FALSE;
    cm->ActivePort = p->Index;

    /* Lane 0 connected; a training lane waited up to 2 s. */
    TbtWaitStart(&w, cm->Host, TBT_U4_CONNECT_US, FALSE);
    for (;;) {
        if (tbtU4LaneRd(cm, &cm->HostRouter, p->Lane0, &lane) != TBT_OK) {
            cls = TBT_U4_LANE_UNKNOWN;
        } else {
            cls = TbtU4LaneClass(TBT_LANE1_STATE(lane));
        }
        if (cls == TBT_U4_LANE_CONNECTED) {
            break;
        }
        if (cls != TBT_U4_LANE_TRANSITIONAL || TbtWaitDone(&w, cm->Host)) {
            cm->ActivePort = 0;
            if (cm->Budgeted && cls != TBT_U4_LANE_DISCONNECTED) {
                /* A resume's rebuild that cannot read its lane, or whose
                 * lane never finishes training, has no removal evidence:
                 * the failure path, nothing assumed. */
                p->State = TBT_U4_HELD;
                p->Why = TBT_U4_WHY_RESUME;
                return;
            }
            p->State = TBT_U4_EMPTY;
            if (cls == TBT_U4_LANE_UNKNOWN) {
                p->CheckPending = TRUE;
            }
            return;
        }
        tbtU4Pause(cm, &w);
    }

    rc = tbtU4Router(cm, p, gen, &next);
    if (rc != TBT_OK) {
        goto out;
    }
    /* The router enumerated again: a known configuration loss is over
     * (until then it stays known, through a failed rebuild). */
    p->LostKnown = FALSE;
    p->ClassifyPending = FALSE;
    tbtU4TmuAtConnect(cm, p);

    p->State = TBT_U4_BONDING;
    if (asFound) {
        p->Bonded = (BOOLEAN)(tbtU4LaneRd(cm, &cm->HostRouter, p->Lane0, &lane) == TBT_OK &&
                              tbtU4WidthBonded(lane));
        p->Gen4 = (BOOLEAN)(p->Bonded && TBT_LANE1_SPEED(lane) == TBT_LANE_SPEED_GEN4);
        p->Asym = (BOOLEAN)(p->Gen4 && TBT_LANE1_NLW(lane) != TBT_LANE_WIDTH_X2);
        p->SingleLane = (BOOLEAN)!p->Bonded;
    } else {
        rc = tbtU4Bond(cm, p, gen);
        if (rc != TBT_OK) {
            next = TBT_U4_FAILED;
            goto out;
        }
    }

    if (!p->Dev.Tbt3 && !asFound && cm->Config.Usb4V2 && p->Dev.Preserved) {
        /* Record 22 section 6.2.2 item 5: CV found set is never
         * configured again (CNS, PTO, UTO, HCO unchanged under it, Spec
         * 8.2.1.1). Without PTO no PCIe tunnel can be built: HELD;
         * without UTO no USB3 tunnel. Section 6.6 runs only once CV is
         * read clear; a read that fails holds the port. */
        rc = tbtU4Rd(cm, &p->Dev.Route, TBT_CS_ROUTER, 0, TBT_ROUTER_CS_5, &lane);
        if (rc != TBT_OK) {
            if (tbtU4Gone(cm, rc)) {
                rc = TBT_E_GONE;
                goto out;
            }
            tbtU4Log(cm, "u4.cv.unread", (ULONG)rc);
            next = TBT_U4_HELD;
            p->Why = TBT_U4_WHY_CV_FOUND;
            goto out;
        }
        if ((lane & TBT_RTR5_CV) != 0) {
            cm->Counters.CvFound++;
            if ((lane & TBT_RTR5_UTO) == 0) {
                p->UtoOff = TRUE;
            }
            if ((lane & TBT_RTR5_PTO) == 0) {
                tbtU4Log(cm, "u4.cv.found", (p->Index << 24) | (lane >> 8));
                next = TBT_U4_HELD;
                p->Why = TBT_U4_WHY_CV_FOUND;
                goto out;
            }
        } else {
            p->State = TBT_U4_CONFIGURING;
            rc = tbtU4ConfigValid(cm, p, gen);
            if (rc != TBT_OK) {
                next = TBT_U4_FAILED;
                goto out;
            }
        }
    } else if (!p->Dev.Tbt3 && !asFound) {
        /* A router a predecessor enumerated may already hold CV with PTO;
         * otherwise the handshake runs (Guide 3.2.3). */
        if (!p->Dev.Preserved ||
            tbtU4Rd(cm, &p->Dev.Route, TBT_CS_ROUTER, 0, TBT_ROUTER_CS_5, &lane) != TBT_OK ||
            (lane & (TBT_RTR5_CV | TBT_RTR5_PTO)) != (TBT_RTR5_CV | TBT_RTR5_PTO)) {
            p->State = TBT_U4_CONFIGURING;
            rc = tbtU4ConfigValid(cm, p, gen);
            if (rc != TBT_OK) {
                next = TBT_U4_FAILED;
                goto out;
            }
        }
    }

    p->State = TBT_U4_TUNNELLING;
    /* Never on a port whose PCIe tunnel was ejected (TbtU4EjectPcie): only
     * an unplug and re-plug clears that (record 22 section 10.4), not a
     * rebuild of its USB3 tunnel at resume. */
    pcieWanted = (BOOLEAN)(cm->Config.Approve && p->PcieDown != 0 &&
                           !p->Ejected &&
                           tbtU4FirstOf(&p->Dev, TBT_ADP_TYPE_PCIE_UP) != 0);
    if (pcieWanted) {
        rc = tbtU4PcieTunnel(cm, p, gen);
        if (rc == TBT_E_GONE) {
            goto out;
        }
        if (rc != TBT_OK) {
            p->PcieFailed = TRUE;
        }
    }
    if (!asFound && !p->Dev.Tbt3) {
        if (tbtU4WantUsb3(cm, p)) {
            rc = tbtU4Usb3Tunnel(cm, p, gen);
            if (rc == TBT_E_GONE) {
                goto out;
            }
        } else if (!cm->Config.Usb3Tunnel) {
            tbtU4Usb3Disconnect(cm, p);
        }
    }
    if (!TBT_U4_LIVE(p, gen)) {
        rc = TBT_E_GONE;
        goto out;
    }
    if (p->Pcie.Up || !pcieWanted) {
        p->State = TBT_U4_UP;
    } else {
        p->State = TBT_U4_FAILED;
    }
    cm->ActivePort = 0;
    tbtU4Log(cm, "u4.port.state", (p->Index << 8) | p->State);
    return;

out:
    /* The stop's reason is decided while the port is still the active
     * one, so that a pending-unplug hint is checked against the
     * registers (tbtU4Live), and a spent resume budget is told apart. */
    if (tbtU4Spent(cm) && p->Generation == gen) {
        /* Section 7.6: nothing is torn down without removal evidence;
         * the port is HELD, the failure path. Section 7.7: the same for a
         * sequence the containment stopped - left as it was, never
         * touched again. */
        cm->ActivePort = 0;
        p->State = TBT_U4_HELD;
        p->Why = tbtU4Off(cm) ? TBT_U4_WHY_CONTAINED : TBT_U4_WHY_RESUME;
        return;
    }
    gone = (BOOLEAN)(rc == TBT_E_GONE || !TBT_U4_LIVE(p, gen));
    cm->ActivePort = 0;
    if (gone) {
        /* Unplugged in the middle (section 7.4): the teardown undoes
         * exactly what the sequence wrote. */
        tbtU4Teardown(cm, p);
        return;
    }
    p->State = next;
    tbtU4Log(cm, "u4.port.state", (p->Index << 8) | p->State | (p->Why << 16));
}

/* ---------------------------------------------------------------------- */
/* Tunnels already present (record 22 section 9.2)                        */
/* ---------------------------------------------------------------------- */

/* Read one entry. */
static LONG tbtU4Entry(PTBT_U4CM cm, const TBT_ROUTE *r, ULONG adp, ULONG hop,
                       PULONG e)
{
    return tbtU4RdN(cm, r, TBT_CS_PATH, adp, 2 * hop, 2, e);
}

/* Whether a path entry, as read, is valid and sends to `adp` at `hop`
 * (PATH_CS_0 Valid, Output Adapter and Output HopID, Spec 8.2.3.2). */
static BOOLEAN tbtU4EntryGoes(const ULONG *e, ULONG adp, ULONG hop)
{
    return (BOOLEAN)((e[0] & TBT_PATH0_VALID) != 0 && TBT_PATH0_OUT_ADP(e[0]) == adp &&
                     TBT_PATH0_OUT_HOP(e[0]) == hop);
}

/*
 * One tunnel as a predecessor left it (record 22 section 9.2): the two
 * paths Guide 5.3.1 (5.5.1 for USB3) sets up, each followed hop by hop
 * through the entries of Table 5-2 - downstream, the host protocol
 * adapter's HopID 8 entry out of the host's lane 0 at some HopID Hd, and
 * the device lane 0's Hd entry into the device protocol adapter at 8;
 * upstream, that adapter's HopID 8 entry out of the device's lane 0 at
 * some Hu, and the host lane 0's Hu entry into the host protocol adapter
 * at 8 - with the Path Enable of both protocol adapters. The host's side
 * is read first: its adapter's enable, its HopID 8 entry, then its lane
 * 0's entries from HopID 8 up until the one back; then the device's.
 * `t` records each part seen; a part that cannot be read is owned all the
 * same and marks the tunnel incomplete, so its teardown can never claim
 * the path closed (sections 7.3, 10.2). TRUE when all six parts are there,
 * consistent and read.
 */
static BOOLEAN tbtU4Follow(PTBT_U4CM cm, PTBT_U4_PORT p, BOOLEAN usb3,
                           PTBT_U4_TUNNEL t)
{
    PTBT_U4_ROUTER h;
    PTBT_U4_ROUTER d;
    ULONG hp;
    ULONG dp;
    ULONG pe;
    ULONG cs0;
    ULONG hop;
    ULONG last;
    ULONG e[2];

    h = &cm->HostRouter;
    d = &p->Dev;
    hp = usb3 ? p->Usb3Down : p->PcieDown;
    pe = usb3 ? TBT_USB3_0_PE : TBT_PCIE0_PE;
    tbtU4Zero(t, sizeof(*t));
    if (hp == 0 || h->Adp[hp].CapAdp == 0) {
        return FALSE;
    }
    if (tbtU4Rd(cm, &h->Route, TBT_CS_ADAPTER, hp, h->Adp[hp].CapAdp, &cs0) != TBT_OK) {
        t->Incomplete = TRUE;
        t->Written |= TBT_U4_E_HOST_ENABLE;
    } else if ((cs0 & pe) != 0) {
        t->Written |= TBT_U4_E_HOST_ENABLE;
    }
    if (tbtU4Entry(cm, &h->Route, hp, TBT_HOP_PROTOCOL, e) != TBT_OK) {
        t->Incomplete = TRUE;
        t->Written |= TBT_U4_E_HOST_PROTO;
    } else if ((e[0] & TBT_PATH0_VALID) != 0) {
        t->Written |= TBT_U4_E_HOST_PROTO;
        if (TBT_PATH0_OUT_ADP(e[0]) == p->Lane0 && TBT_PATH0_OUT_HOP(e[0]) >= TBT_HOP_FIRST) {
            t->Hd = TBT_PATH0_OUT_HOP(e[0]);
        }
    }
    last = h->Adp[p->Lane0].MaxInHop;
    for (hop = TBT_HOP_FIRST; hop <= last; hop++) {
        if (tbtU4Entry(cm, &h->Route, p->Lane0, hop, e) != TBT_OK) {
            t->Incomplete = TRUE;
        } else if (tbtU4EntryGoes(e, hp, TBT_HOP_PROTOCOL)) {
            t->Written |= TBT_U4_E_HOST_LANE;
            t->Hu = hop;
            break;
        }
    }
    if (!d->Present) {
        return FALSE;
    }
    dp = tbtU4FirstOf(d, usb3 ? TBT_ADP_TYPE_USB3_UP : TBT_ADP_TYPE_PCIE_UP);
    if (dp == 0 || d->Adp[dp].CapAdp == 0) {
        return FALSE;
    }
    if (t->Hd != 0 && tbtU4Entry(cm, &d->Route, d->UpAdp, t->Hd, e) == TBT_OK &&
        tbtU4EntryGoes(e, dp, TBT_HOP_PROTOCOL)) {
        t->Written |= TBT_U4_E_DEV_LANE;
    }
    if (tbtU4Entry(cm, &d->Route, dp, TBT_HOP_PROTOCOL, e) == TBT_OK &&
        tbtU4EntryGoes(e, d->UpAdp, t->Hu)) {
        t->Written |= TBT_U4_E_DEV_PROTO;
    }
    if (tbtU4Rd(cm, &d->Route, TBT_CS_ADAPTER, dp, d->Adp[dp].CapAdp, &cs0) == TBT_OK &&
        (cs0 & pe) != 0) {
        t->Written |= TBT_U4_E_DEV_ENABLE;
    }
    /* Written may hold bits set only for ownership (an unread entry or
     * enable): an incomplete walk is never a whole tunnel. */
    return (BOOLEAN)((t->Written & 0x3FUL) == 0x3FUL && t->Hd != 0 && t->Hu != 0 &&
                     !t->Incomplete);
}

/*
 * The host's PCIe Down adapter's LTSSM from ADP_PCIE_CS_0 `v`. On a
 * Thunderbolt host (record 22 section 19.4) the field is not the USB4
 * encoding - usb4-emu's captured Thunderbolt 3 adapter reads 3 with its
 * tunnel up, 0 idle - and Linux reads it on USB4 routers only (usb4.c
 * usb4_pci_port_check_ltssm_state; tunnel.c activates a Thunderbolt
 * router's PCIe path by Path Enable alone): there, Path Enable set and
 * not Detect is taken as L0.
 */
static ULONG tbtU4Ltssm(PTBT_U4CM cm, ULONG v)
{
    ULONG s;

    s = TBT_PCIE0_LTSSM(v);
    if (cm->HostRouter.Tbt3 && (v & TBT_PCIE0_PE) != 0 && s != TBT_LTSSM_DETECT) {
        return TBT_LTSSM_L0;
    }
    return s;
}

/* A PCIe Down adapter's LTSSM live (L0, L1, Recovery, Recovery.Idle), a
 * transitional state re-read up to 2 s, Detect failing (section 7.1 item
 * 4, 9.2 step 4). */
static BOOLEAN tbtU4PcieLive(PTBT_U4CM cm, PTBT_U4_PORT p)
{
    PTBT_U4_ROUTER h;
    ULONG v;
    ULONG s;
    TBT_WAIT w;

    h = &cm->HostRouter;
    TbtWaitStart(&w, cm->Host, TBT_U4_TRANSITIONAL_US, FALSE);
    for (;;) {
        if (tbtU4Rd(cm, &h->Route, TBT_CS_ADAPTER, p->PcieDown,
                    h->Adp[p->PcieDown].CapAdp + TBT_PCIE_CS_0, &v) != TBT_OK) {
            return FALSE;
        }
        s = tbtU4Ltssm(cm, v);
        if ((v & TBT_PCIE0_PE) == 0 || s == TBT_LTSSM_DETECT) {
            return FALSE;
        }
        if (s == TBT_LTSSM_L0 || s == TBT_LTSSM_L1 || s == TBT_LTSSM_RECOVERY ||
            s == TBT_LTSSM_RECOVERY_IDLE) {
            p->Transitional = FALSE;
            return TRUE;
        }
        /* Transitional: one deadline on the clock from the first such
         * sample, an earlier check's included, recorded at once and
         * checked at every sample. */
        if (!p->Transitional) {
            p->Transitional = TRUE;
            p->TransitionalSinceUs = tbtU4Now(cm);
        }
        if (tbtU4Now(cm) - p->TransitionalSinceUs >= TBT_U4_TRANSITIONAL_US) {
            return FALSE;
        }
        if (TbtWaitDone(&w, cm->Host)) {
            /* The waited sum ended the wait (a coarse or stopped clock):
             * no evidence of 2 s; the deadline from the first transitional
             * sample, recorded above, stands. */
            return FALSE;
        }
        tbtU4Pause(cm, &w);
    }
}

/* Any Valid or pending entry on an adapter, or a Valid entry anywhere on
 * the router leading out of it ("tunnel state"). Reads only. */
static BOOLEAN tbtU4AnyState(PTBT_U4CM cm, PTBT_U4_PORT p, const TBT_U4_ROUTER *rt,
                             ULONG lane, ULONG proto1, ULONG proto2)
{
    TBT_U4_USAGE u;
    ULONG k;
    ULONG v;
    ULONG e[2];
    ULONG adp;

    if (tbtU4ScanLane(cm, rt, lane, &u) != TBT_OK) {
        return TRUE;    /* unreadable: not "nothing" */
    }
    for (k = 0; k < 4; k++) {
        if (u.InUse[k] != 0 || u.OutUse[k] != 0) {
            return TRUE;
        }
    }
    for (k = 0; k < 2; k++) {
        adp = k == 0 ? proto1 : proto2;
        if (adp == 0 || rt->Adp[adp].CapAdp == 0) {
            continue;
        }
        if (tbtU4Rd(cm, &rt->Route, TBT_CS_ADAPTER, adp, rt->Adp[adp].CapAdp, &v) != TBT_OK ||
            (v & TBT_PCIE0_PE) != 0) {
            return TRUE;
        }
        if (tbtU4Entry(cm, &rt->Route, adp, TBT_HOP_PROTOCOL, e) != TBT_OK ||
            (e[0] & TBT_PATH0_VALID) != 0 || (e[1] & TBT_PATH1_PP) != 0) {
            return TRUE;
        }
    }
    (void)p;
    return FALSE;
}

/* Quarantine every pending (Valid 0, Pending Packets 1) entry of an
 * adapter's input HopIDs: residual state no path owns (section 9.2). */
static VOID tbtU4QuarantinePending(PTBT_U4CM cm, PTBT_U4_PORT p, const TBT_U4_ROUTER *rt,
                                   ULONG adp)
{
    ULONG hop;
    ULONG e[2];

    if (adp == 0) {
        return;
    }
    for (hop = TBT_HOP_FIRST; hop <= rt->Adp[adp].MaxInHop; hop++) {
        if (tbtU4Entry(cm, &rt->Route, adp, hop, e) == TBT_OK &&
            (e[0] & TBT_PATH0_VALID) == 0 && (e[1] & TBT_PATH1_PP) != 0) {
            tbtU4Quarantine(cm, p->Index, &rt->Route, adp, hop, e[0], FALSE, 0, 0);
        }
    }
}

/* A DisplayPort (or unclassified) path through the port: a Valid entry on
 * one of the host's DP adapters leading to its lanes. */
static BOOLEAN tbtU4OtherPaths(PTBT_U4CM cm, PTBT_U4_PORT p)
{
    PTBT_U4_ROUTER h;
    ULONG a;
    ULONG hop;
    ULONG e[2];

    h = &cm->HostRouter;
    for (a = 1; a <= h->MaxAdp && a < TBT_U4_MAX_ADP; a++) {
        if (!h->Adp[a].Present || (h->Adp[a].Type != TBT_ADP_TYPE_DP_IN &&
                                   h->Adp[a].Type != TBT_ADP_TYPE_DP_OUT)) {
            continue;
        }
        for (hop = TBT_HOP_FIRST; hop <= h->Adp[a].MaxInHop; hop++) {
            if (tbtU4Entry(cm, &h->Route, a, hop, e) == TBT_OK &&
                (e[0] & TBT_PATH0_VALID) != 0 &&
                (TBT_PATH0_OUT_ADP(e[0]) == p->Lane0 ||
                 TBT_PATH0_OUT_ADP(e[0]) == p->Lane0 + 1)) {
                return TRUE;
            }
        }
    }
    return FALSE;
}

/* Lock found clear on a port this driver adopts: a later Lock is the
 * router's disconnect mark (Guide 3.2 note 2). */
static VOID tbtU4MarkUnlocked(PTBT_U4CM cm, PTBT_U4_PORT p)
{
    ULONG cs4;

    if (tbtU4Rd(cm, &cm->HostRouter.Route, TBT_CS_ADAPTER, p->Lane0, TBT_ADP_CS_4,
                &cs4) == TBT_OK && (cs4 & TBT_ADP4_LOCK) == 0) {
        p->Unlocked = TRUE;
    }
}

static VOID tbtU4DiscoverLinks(PTBT_U4CM cm, PTBT_U4_PORT p);

/* The host-side parts of both tunnels as the registers show them, kept so
 * that a port left as found can still have its host side torn down at its
 * unplug (sections 7.3, 9.2); a USB3 Down adapter found enabled may hold an
 * allocation, released then too. Reads only. */
static VOID tbtU4HostInventory(PTBT_U4CM cm, PTBT_U4_PORT p, PTBT_U4_TUNNEL tp,
                               PTBT_U4_TUNNEL tu)
{
    BOOLEAN was;

    was = p->Dev.Present;
    p->Dev.Present = FALSE;
    (void)tbtU4Follow(cm, p, FALSE, tp);
    (void)tbtU4Follow(cm, p, TRUE, tu);
    p->Dev.Present = was;
    tp->Written &= TBT_U4_E_HOST_PROTO | TBT_U4_E_HOST_LANE | TBT_U4_E_HOST_ENABLE;
    tu->Written &= TBT_U4_E_HOST_PROTO | TBT_U4_E_HOST_LANE | TBT_U4_E_HOST_ENABLE;
    if ((tu->Written & TBT_U4_E_HOST_ENABLE) != 0) {
        tu->Written |= TBT_U4_E_BANDWIDTH;
    }
}

/* A protocol adapter with nothing on it: Path Enable clear, its HopID 8
 * entry neither Valid nor pending. Reads only. */
static BOOLEAN tbtU4ProtoEmpty(PTBT_U4CM cm, const TBT_U4_ROUTER *rt, ULONG adp)
{
    ULONG v;
    ULONG e[2];

    if (adp == 0 || rt->Adp[adp].CapAdp == 0) {
        return TRUE;
    }
    return (BOOLEAN)(tbtU4Rd(cm, &rt->Route, TBT_CS_ADAPTER, adp, rt->Adp[adp].CapAdp, &v) ==
                     TBT_OK && (v & TBT_PCIE0_PE) == 0 &&
                     tbtU4Entry(cm, &rt->Route, adp, TBT_HOP_PROTOCOL, e) == TBT_OK &&
                     (e[0] & TBT_PATH0_VALID) == 0 && (e[1] & TBT_PATH1_PP) == 0);
}

/* The device router's PCIe and USB3 Up adapters carry nothing, and no
 * Valid entry of its upstream lanes leads to them: the DisplayPort (or
 * other) paths through its lanes are not PCIe or USB3 state. Reads only;
 * anything unreadable is not "nothing". */
static BOOLEAN tbtU4DevProtoFree(PTBT_U4CM cm, const TBT_U4_ROUTER *d)
{
    ULONG pu;
    ULONG uu;
    ULONG k;
    ULONG hop;
    ULONG lane;
    ULONG out;
    ULONG e[2];

    pu = tbtU4FirstOf(d, TBT_ADP_TYPE_PCIE_UP);
    uu = tbtU4FirstOf(d, TBT_ADP_TYPE_USB3_UP);
    if (!tbtU4ProtoEmpty(cm, d, pu) || !tbtU4ProtoEmpty(cm, d, uu)) {
        return FALSE;
    }
    for (k = 0; k < 2; k++) {
        lane = d->UpAdp + k;
        if (lane >= TBT_U4_MAX_ADP || !d->Adp[lane].Present) {
            continue;
        }
        for (hop = TBT_HOP_FIRST; hop <= d->Adp[lane].MaxInHop; hop++) {
            if (tbtU4Entry(cm, &d->Route, lane, hop, e) != TBT_OK) {
                return FALSE;
            }
            out = TBT_PATH0_OUT_ADP(e[0]);
            if ((e[0] & TBT_PATH0_VALID) != 0 &&
                ((pu != 0 && out == pu) || (uu != 0 && out == uu))) {
                return FALSE;
            }
        }
    }
    return TRUE;
}

/* HELD: left exactly as found, its host side inventoried for its unplug. */
static VOID tbtU4Hold(PTBT_U4CM cm, PTBT_U4_PORT p, ULONG why)
{
    TBT_U4_TUNNEL tp;
    TBT_U4_TUNNEL tu;

    tbtU4HostInventory(cm, p, &tp, &tu);
    if (!p->Pcie.Up) {
        p->Pcie = tp;
    }
    if (!p->Usb3.Up) {
        p->Usb3 = tu;
    }
    p->State = TBT_U4_HELD;
    p->Why = why;
    p->Generation++;
    cm->Counters.PortsHeld++;
    tbtU4Log(cm, "u4.held", (p->Index << 16) | (tp.Written << 8) | tu.Written);
}

/*
 * Per port at start, reading only (section 9.2). The outcomes: all four
 * PCIe checks - UP and preserved; no tunnel state at all - connected
 * normally (an enumerated router adopted); only tunnels this driver does
 * not build - reserved, and a missing PCIe tunnel built on the link as
 * found; anything else - HELD. A disconnected port's residual host-side
 * paths are torn down as an unplug's are. tbtU4DiscoverPort records the
 * discovery complete (Discovered) only when nothing cut it short - a loss
 * or a spent budget inside it leaves the port to a recovery's discovery
 * (section 7.7, decision U27).
 */
static VOID tbtU4DiscoverRead(PTBT_U4CM cm, PTBT_U4_PORT p);

/* No DisplayPort entry on the host leads to the port's lanes, every
 * entry read: tbtU4OtherPaths with an unreadable entry counted against.
 * Reads only. */
static BOOLEAN tbtU4DpClear(PTBT_U4CM cm, const TBT_U4_PORT *p)
{
    PTBT_U4_ROUTER h;
    ULONG a;
    ULONG hop;
    ULONG e[2];

    h = &cm->HostRouter;
    for (a = 1; a <= h->MaxAdp && a < TBT_U4_MAX_ADP; a++) {
        if (!h->Adp[a].Present || (h->Adp[a].Type != TBT_ADP_TYPE_DP_IN &&
                                   h->Adp[a].Type != TBT_ADP_TYPE_DP_OUT)) {
            continue;
        }
        for (hop = TBT_HOP_FIRST; hop <= h->Adp[a].MaxInHop; hop++) {
            if (tbtU4Entry(cm, &h->Route, a, hop, e) != TBT_OK) {
                return FALSE;
            }
            if ((e[0] & TBT_PATH0_VALID) != 0 &&
                (TBT_PATH0_OUT_ADP(e[0]) == p->Lane0 ||
                 TBT_PATH0_OUT_ADP(e[0]) == p->Lane0 + 1)) {
                return FALSE;
            }
        }
    }
    return TRUE;
}

/* No Valid entry of a router's two lane adapters from `lane` on leads to
 * the protocol adapter `target`, and none that is not Valid is still
 * pending (a drained path's residue, whatever it led to); every entry
 * read. Reads only. */
static BOOLEAN tbtU4LanesFree(PTBT_U4CM cm, const TBT_U4_ROUTER *rt, ULONG lane,
                              ULONG target)
{
    ULONG k;
    ULONG a;
    ULONG hop;
    ULONG e[2];

    if (target == 0) {
        return TRUE;
    }
    for (k = 0; k < 2; k++) {
        a = lane + k;
        if (a == 0 || a >= TBT_U4_MAX_ADP || !rt->Adp[a].Present) {
            continue;
        }
        for (hop = TBT_HOP_FIRST; hop <= rt->Adp[a].MaxInHop; hop++) {
            if (tbtU4Entry(cm, &rt->Route, a, hop, e) != TBT_OK) {
                return FALSE;
            }
            if ((e[0] & TBT_PATH0_VALID) != 0 ? TBT_PATH0_OUT_ADP(e[0]) == target
                                              : (e[1] & TBT_PATH1_PP) != 0) {
                return FALSE;
            }
        }
    }
    return TRUE;
}

/* One recorded tunnel against one found: up in both at the same HopIDs;
 * or up in neither, the found side proven empty - nothing of it found or
 * left unread, both its protocol adapters (host `hp`, device `dp`)
 * reading Path Enable clear and HopID 8 neither Valid nor pending, and no
 * lane entry of either router leading to them or pending. Reads only for
 * the empty case. */
static BOOLEAN tbtU4CarrySame(PTBT_U4CM cm, const TBT_U4_PORT *p,
                              const TBT_U4_CARRY_TUN *r, const TBT_U4_TUNNEL *t,
                              ULONG hp, ULONG dp)
{
    if (t->Up) {
        return (BOOLEAN)(t->Preserved && r->Up && r->Hd == t->Hd && r->Hu == t->Hu);
    }
    return (BOOLEAN)(!r->Up && t->Written == 0 && !t->Incomplete &&
                     tbtU4ProtoEmpty(cm, &cm->HostRouter, hp) &&
                     tbtU4ProtoEmpty(cm, &p->Dev, dp) &&
                     tbtU4LanesFree(cm, &cm->HostRouter, p->Lane0, hp) &&
                     tbtU4LanesFree(cm, &p->Dev, p->Dev.UpAdp, dp));
}

/*
 * Section 9.4: a port just found UP and preserved is this driver's own,
 * as a whole, when the record its earlier instance on the same NHI handed
 * on names it - the host router found enumerated with the recorded ids,
 * the same lane 0 adapter, the same device router (ids, kind and UUID,
 * read by this discovery), the PCIe tunnel and any USB3 tunnel up recorded
 * up at the same HopIDs (a USB3 tunnel neither up nor recorded matches),
 * and no preserved DisplayPort tunnel. Section 7.6 rebuilds a port, not a
 * tunnel, so one tunnel that is someone else's keeps the whole port so.
 * The empty USB3 side and the DisplayPort side are proven by reads (a
 * read that fails is no match); nothing is written.
 */
static BOOLEAN tbtU4CarryMatch(PTBT_U4CM cm, const TBT_U4_PORT *p)
{
    const TBT_U4_CARRY *c;
    const TBT_U4_CARRY_PORT *e;
    const TBT_U4_ROUTER *d;
    ULONG i;
    ULONG k;

    c = &cm->Carried;
    d = &p->Dev;
    if (!c->Valid || !cm->HostRouter.Preserved ||
        c->HostVendor != cm->HostRouter.Vendor ||
        c->HostDevice != cm->HostRouter.Device ||
        !d->Present || !d->UuidValid || !p->Pcie.Up || p->DpPreserved) {
        return FALSE;
    }
    for (i = 0; i < TBT_U4_MAX_PORTS; i++) {
        e = &c->Port[i];
        if (!e->Used || e->Lane0 != p->Lane0) {
            continue;
        }
        if (e->Vendor != d->Vendor || e->Device != d->Device ||
            e->Tbt3 != d->Tbt3) {
            return FALSE;
        }
        /* A USB4 router's UUID is ROUTER_CS_7 and 8; a Thunderbolt 3
         * router's the four words of its link controller's fuses. */
        for (k = 0; k < (d->Tbt3 ? 4UL : 2UL); k++) {
            if (e->Uuid[k] != d->Uuid[k]) {
                return FALSE;
            }
        }
        return (BOOLEAN)(tbtU4CarrySame(cm, p, &e->Pcie, &p->Pcie, p->PcieDown,
                                        tbtU4FirstOf(d, TBT_ADP_TYPE_PCIE_UP)) &&
                         tbtU4CarrySame(cm, p, &e->Usb3, &p->Usb3, p->Usb3Down,
                                        tbtU4FirstOf(d, TBT_ADP_TYPE_USB3_UP)) &&
                         tbtU4DpClear(cm, p));
    }
    return FALSE;
}

/*
 * Record 22 section 6.2.3 item 4 part 1: on a USB4 port of a version 2
 * router (`cap` its USB4 Port Capability) a predecessor's PORT_CS_19
 * Enable Gen 4 Link Recovery (bit 31) is read and, set, cleared by a
 * read-modify-write of that bit alone, whatever XhciCmUsb4V2 says; this
 * driver never sets it, nor Initiate Gen 4 Link Recovery. TRUE unless the
 * read or the write failed. The one write a discovery makes.
 */
static BOOLEAN tbtU4ElrClearAt(PTBT_U4CM cm, const TBT_ROUTE *route, ULONG adp, ULONG cap)
{
    ULONG v;

    if (cap == 0) {
        return TRUE;
    }
    if (tbtU4Rd(cm, route, TBT_CS_ADAPTER, adp, cap + TBT_PORT_CS_19, &v) != TBT_OK) {
        return FALSE;
    }
    if ((v & TBT_PORT19_ELR) == 0) {
        return TRUE;
    }
    if ((v & TBT_PORT19_SAF) != 0) {
        /* Table 8-13: no write while StartAsymmetricFlow reads 1 - a clear
         * withheld, which holds the port as a failing one does (section
         * 6.2.3 item 4 part 7). */
        return FALSE;
    }
    if (tbtU4Wr(cm, route, TBT_CS_ADAPTER, adp, cap + TBT_PORT_CS_19,
                v & ~(TBT_PORT19_ELR | TBT_PORT19_ILR | TBT_PORT19_SAF)) != TBT_OK) {
        return FALSE;
    }
    cm->Counters.ElrCleared++;
    tbtU4Log(cm, "u4.elr.cleared", (route->Lo << 8) | adp);
    return TRUE;
}

static BOOLEAN tbtU4ElrClear(PTBT_U4CM cm, const TBT_U4_ROUTER *rt, ULONG adp, ULONG cap)
{
    if (!tbtU4IsV2(rt) || adp == 0 || adp >= TBT_U4_MAX_ADP) {
        return TRUE;
    }
    return tbtU4ElrClearAt(cm, &rt->Route, adp, cap);
}

/* An adapter's capability of id `id` found by its own chain (section 6.1's
 * walk for one adapter), 0 for none. Reads only. */
static ULONG tbtU4AdpCapOf(PTBT_U4CM cm, const TBT_ROUTE *route, ULONG adp, ULONG id)
{
    ULONG h;
    ULONG ptr;
    ULONG n;

    if (tbtU4Rd(cm, route, TBT_CS_ADAPTER, adp, TBT_ADP_CS_1, &h) != TBT_OK) {
        return 0;
    }
    ptr = TBT_ADP1_NEXT_CAP(h);
    for (n = 0; ptr != 0 && n < TBT_CAP_WALK_LIMIT; n++) {
        if (tbtU4Rd(cm, route, TBT_CS_ADAPTER, adp, ptr, &h) != TBT_OK) {
            return 0;
        }
        if (TBT_CAP_ID(h) == id) {
            return ptr;
        }
        ptr = TBT_CAP_NEXT(h);
    }
    return 0;
}

static VOID tbtU4DiscoverPort(PTBT_U4CM cm, PTBT_U4_PORT p)
{
    ULONG hw;

    p->Discovered = FALSE;
    hw = cm->HwErrors;
    tbtU4DiscoverRead(cm, p);
    /* A transport failure inside it, cancellation or not, is no completed
     * discovery either. */
    p->Discovered = (BOOLEAN)(!tbtU4Spent(cm) && cm->HwErrors == hw);
    /* Section 9.4: a port found UP and preserved by a completed discovery,
     * matched as a whole to the earlier instance's record. */
    if (p->Discovered && p->State == TBT_U4_UP && p->Pcie.Up &&
        p->Pcie.Preserved && tbtU4CarryMatch(cm, p)) {
        p->Pcie.Own = TRUE;
        p->Usb3.Own = (BOOLEAN)(p->Usb3.Up && p->Usb3.Preserved);
        tbtU4Log(cm, "u4.preserved.own", (p->Index << 16) | 1UL |
                                         (p->Usb3.Own ? 2UL : 0UL));
    }
}

static VOID tbtU4DiscoverRead(PTBT_U4CM cm, PTBT_U4_PORT p)
{
    PTBT_U4_ROUTER h;
    PTBT_U4_ROUTER d;
    ULONG lane;
    ULONG lane1;
    ULONG cs4;
    ULONG cls;
    ULONG v[7];
    BOOLEAN hostState;
    BOOLEAN devState;
    BOOLEAN pcie;
    BOOLEAN usb3;
    BOOLEAN other;
    BOOLEAN cfgOk;
    BOOLEAN elrOk;
    TBT_U4_TUNNEL tp;
    TBT_U4_TUNNEL tu;
    LONG rc;

    h = &cm->HostRouter;
    d = &p->Dev;
    if (tbtU4LaneRd(cm, h, p->Lane0, &lane) != TBT_OK ||
        tbtU4Rd(cm, &h->Route, TBT_CS_ADAPTER, p->Lane0, TBT_ADP_CS_4, &cs4) != TBT_OK) {
        tbtU4Hold(cm, p, TBT_U4_WHY_LANE_STATE);
        return;
    }
    cls = TbtU4LaneClass(TBT_LANE1_STATE(lane));
    tbtU4QuarantinePending(cm, p, h, p->Lane0);
    tbtU4QuarantinePending(cm, p, h, p->PcieDown);
    tbtU4QuarantinePending(cm, p, h, p->Usb3Down);
    hostState = tbtU4AnyState(cm, p, h, p->Lane0, p->PcieDown, p->Usb3Down);
    other = tbtU4OtherPaths(cm, p);

    if (cls == TBT_U4_LANE_DISABLED) {
        tbtU4Hold(cm, p, TBT_U4_WHY_LANE_STATE);
        return;
    }
    if (cls == TBT_U4_LANE_UNKNOWN) {
        /* A reserved Adapter State is no removal evidence (section 6.4
         * step 1): whatever is there stays; the checks re-read it. */
        if (hostState || other) {
            tbtU4Hold(cm, p, TBT_U4_WHY_LANE_STATE);
        } else {
            p->State = TBT_U4_EMPTY;
            p->CheckPending = TRUE;
        }
        return;
    }
    if (cls == TBT_U4_LANE_DISCONNECTED) {
        if (hostState) {
            /* No tunnel can exist through a disconnected lane: unplug
             * handling, not a reset. Both tunnels' host sides found. */
            (void)tbtU4Follow(cm, p, FALSE, &p->Pcie);
            (void)tbtU4Follow(cm, p, TRUE, &p->Usb3);
            p->Usb3.Written &= ~(TBT_U4_E_DEV_LANE | TBT_U4_E_DEV_PROTO | TBT_U4_E_DEV_ENABLE);
            p->Pcie.Written &= ~(TBT_U4_E_DEV_LANE | TBT_U4_E_DEV_PROTO | TBT_U4_E_DEV_ENABLE);
            if (p->Pcie.Written != 0) {
                /* Path Enable may have cleared itself at the disconnect.
                 * Only a PCIe path found: USB3 or DP residue alone is no
                 * PCIe tunnel to state closed (sections 7.3, 9.2). */
                p->Pcie.Written |= TBT_U4_E_HOST_ENABLE;
            }
            if (p->Usb3.Written != 0) {
                /* Path Enable may have cleared itself at the disconnect;
                 * an allocation may still be held: released. */
                p->Usb3.Written |= TBT_U4_E_BANDWIDTH;
            }
            tbtU4Teardown(cm, p);
        }
        p->State = TBT_U4_EMPTY;
        return;
    }

    /* Connected (or training: re-read by the first check). */
    if ((cs4 & TBT_ADP4_LOCK) != 0) {
        if (hostState || other) {
            tbtU4Hold(cm, p, TBT_U4_WHY_DISCOVERY);
        } else {
            p->State = TBT_U4_EMPTY;
            p->CheckPending = TRUE;
        }
        return;
    }
    tbtU4Zero(d, sizeof(*d));
    d->Route.Hi = 0;
    d->Route.Lo = p->Lane0;
    d->Depth = 1;
    d->CapsState = TBT_U4_CAPS_PENDING;
    rc = tbtU4RdN(cm, &d->Route, TBT_CS_ROUTER, 0, TBT_ROUTER_CS_0, 7, v);
    if (rc == TBT_OK) {
        tbtU4TakeHeader(d, v);
        d->Depth = 1;
    }
    if (rc != TBT_OK || !tbtU4Enumerated(d, v) ||
        TbtHwRouterVersion(d->Usb4Version) == TBT_HW_ROUTER_RESERVED) {
        tbtU4Zero(d, sizeof(*d));
        if (hostState || other) {
            tbtU4Hold(cm, p, TBT_U4_WHY_DISCOVERY);
        } else {
            p->State = TBT_U4_EMPTY;
            p->CheckPending = TRUE;
        }
        return;
    }
    /* Step 2 is judged after Router Ready's own wait (section 9.2). */
    tbtU4FoundReady(cm, d, &v[5]);
    d->Present = TRUE;
    d->Preserved = TRUE;
    cm->Counters.RoutersAdopted++;
    tbtU4CmuvFound(cm, d, v[4]);
    if (!d->Tbt3) {
        /* As tbtU4Router: an adapter the DROM marks unused is never read. */
        tbtU4ReadDrom(cm, d);
    }
    if (tbtU4DiscoverAdapters(cm, d) != TBT_OK ||
        tbtU4RouterCaps(cm, d, TBT_RTR1_NEXT_CAP(v[1])) != TBT_OK ||
        d->UpAdp == 0 || d->UpAdp + 1 >= TBT_U4_MAX_ADP) {
        tbtU4Zero(d, sizeof(*d));
        tbtU4Hold(cm, p, TBT_U4_WHY_DISCOVERY);
        return;
    }
    tbtU4ApplyQuirks(cm, d);
    if (!d->Tbt3) {
        d->UuidValid = (BOOLEAN)(tbtU4RdN(cm, &d->Route, TBT_CS_ROUTER, 0, TBT_ROUTER_CS_7, 2,
                                          d->Uuid) == TBT_OK);
    } else {
        d->CapsState = TBT_U4_CAPS_DEFAULT;
        d->UidFromDrom = (BOOLEAN)(d->CapLc == 0 && d->TbtGen == 1);
        d->UuidValid = (BOOLEAN)(tbtU4TbtUuid(cm, d, d->Uuid) == TBT_OK);
        /* Adopted with no connection sequence of this driver's: its
         * names are read on a later pass (TbtU4Poll, tbtU4DromLate), its
         * adapters as found here. */
        d->DromOwed = (BOOLEAN)(d->CapPlugEvents != 0);
    }
    devState = tbtU4AnyState(cm, p, d, d->UpAdp, tbtU4FirstOf(d, TBT_ADP_TYPE_PCIE_UP),
                             tbtU4FirstOf(d, TBT_ADP_TYPE_USB3_UP));
    /* Step 2: the router's configuration (section 8's for Thunderbolt 3). */
    cfgOk = TRUE;
    if (!d->Tbt3) {
        cfgOk = (BOOLEAN)((v[6] & TBT_RTR6_RR) != 0 && (v[5] & TBT_RTR5_CV) != 0 &&
                          (v[6] & TBT_RTR6_CR) != 0);
    }
    /* Section 6.2.3 item 4 part 1: a predecessor's Enable Gen 4 Link
     * Recovery cleared on this link before its tunnels are verified; a
     * write that fails holds the port. */
    elrOk = (BOOLEAN)(tbtU4ElrClear(cm, h, p->Lane0, h->Adp[p->Lane0].CapPort) &&
                      (d->Tbt3 || tbtU4ElrClear(cm, d, d->UpAdp, d->Adp[d->UpAdp].CapPort)));
    if (!elrOk) {
        cfgOk = FALSE;
    }
    /* Step 3: bonded, or lane 1 disconnected or disabled; a Gen 4 link
     * bonded when symmetric or a paired asymmetric one (section 6.2.3). */
    if (TBT_LANE1_SPEED(lane) == TBT_LANE_SPEED_GEN4 &&
        TBT_LANE1_NLW(lane) != TBT_LANE_WIDTH_X2) {
        if (tbtU4LaneRd(cm, d, d->UpAdp, &lane1) != TBT_OK ||
            tbtU4LinkKind(lane, lane1) != TBT_U4_LINK_GEN4_ASYM) {
            cfgOk = FALSE;
        } else {
            p->Bonded = TRUE;
            p->Gen4 = TRUE;
            p->Asym = TRUE;
        }
    } else if (tbtU4LaneRd(cm, h, p->Lane0 + 1, &lane1) != TBT_OK) {
        cfgOk = FALSE;
    } else if (TBT_LANE1_NLW(lane) == TBT_LANE_WIDTH_X2) {
        p->Bonded = TRUE;
        p->Gen4 = (BOOLEAN)(TBT_LANE1_SPEED(lane) == TBT_LANE_SPEED_GEN4);
    } else if (tbtU4SingleOk(cm, lane1)) {
        p->SingleLane = TRUE;
    } else {
        cfgOk = FALSE;
    }
    tbtU4MarkUnlocked(cm, p);

    pcie = tbtU4Follow(cm, p, FALSE, &tp);
    if (pcie && !tbtU4PcieLive(cm, p)) {
        pcie = FALSE;
    }
    if (pcie && !d->Tbt3 && (v[5] & TBT_RTR5_PTO) == 0) {
        pcie = FALSE;
    }
    usb3 = tbtU4Follow(cm, p, TRUE, &tu);

    if (!hostState && !devState && !other && (!elrOk ||
        (!d->Tbt3 && cm->Config.Usb4V2 && (v[5] & TBT_RTR5_CV) != 0 &&
         (v[5] & TBT_RTR5_PTO) == 0))) {
        /* Section 6.2.2 item 5: CV found without PTO is never configured
         * again, so no PCIe tunnel can be built: HELD, which
         * XhciCmPreOsCleanup 1 cleans. An Enable Gen 4 Link Recovery that
         * would not clear holds the port too (section 6.2.3 item 4). */
        if (elrOk) {
            cm->Counters.CvFound++;
            tbtU4Log(cm, "u4.cv.found", (p->Index << 24) | (v[5] >> 8));
        }
        if (!d->Tbt3) {
            tbtU4GetCapabilities(cm, d);
        }
        tbtU4Hold(cm, p, elrOk ? TBT_U4_WHY_CV_FOUND : TBT_U4_WHY_DISCOVERY);
        tbtU4DiscoverLinks(cm, p);
        return;
    }
    if (!hostState && !devState && !other) {
        /* Nothing to preserve: connected normally, the router adopted. */
        p->State = TBT_U4_EMPTY;
        tbtU4Zero(d, sizeof(*d));
        p->Bonded = FALSE;
        p->SingleLane = FALSE;
        p->Gen4 = FALSE;
        p->Asym = FALSE;
        p->CheckPending = TRUE;
        return;
    }
    if (cfgOk && pcie) {
        p->Pcie = tp;
        p->Pcie.Up = TRUE;
        p->Pcie.Preserved = TRUE;
        if (usb3 && (d->Tbt3 || (v[5] & TBT_RTR5_UTO) != 0)) {
            p->Usb3 = tu;
            p->Usb3.Up = TRUE;
            p->Usb3.Preserved = TRUE;
            p->Usb3.Written |= TBT_U4_E_BANDWIDTH;  /* its allocation, released at unplug */
        } else {
            /* Not verified: left as found, its host side kept for the
             * unplug (section 9.2's removal rule). */
            p->Usb3 = tu;
            p->Usb3.Written &= TBT_U4_E_HOST_PROTO | TBT_U4_E_HOST_LANE | TBT_U4_E_HOST_ENABLE;
            if ((p->Usb3.Written & TBT_U4_E_HOST_ENABLE) != 0) {
                p->Usb3.Written |= TBT_U4_E_BANDWIDTH;
            }
        }
        p->DpPreserved = other;
        p->State = TBT_U4_UP;
        p->Generation++;
        cm->Counters.PortsPreserved++;
        tbtU4Log(cm, "u4.preserved", (p->Index << 16) | (tp.Hd << 8) | tp.Hu);
        if (!d->Tbt3) {
            tbtU4GetCapabilities(cm, d);
        }
        tbtU4DiscoverLinks(cm, p);
        return;
    }
    if (cfgOk && other && tp.Written == 0 && tu.Written == 0 &&
        (d->Tbt3 || (v[5] & TBT_RTR5_PTO) != 0) &&
        tbtU4ProtoEmpty(cm, h, p->PcieDown) && tbtU4ProtoEmpty(cm, h, p->Usb3Down) &&
        tbtU4DevProtoFree(cm, d)) {
        /* Only tunnels this driver does not build: reserved; a missing
         * PCIe tunnel built on the link as found, no USB3 beside DP. */
        p->DpPreserved = TRUE;
        tbtU4Zero(d, sizeof(*d));
        p->Bonded = FALSE;
        p->SingleLane = FALSE;
        p->Gen4 = FALSE;
        p->Asym = FALSE;
        cm->Counters.PortsPreserved++;
        if (cm->Config.DeferFirstPoll && cm->Stage != TBT_U4_STAGE_RUNNING) {
            /* Built by the caller's first poll, as found (TbtU4Poll). */
            p->AsFoundOwed = TRUE;
            return;
        }
        tbtU4Connect(cm, p, TRUE);
        if (p->Dev.Present) {
            tbtU4DiscoverLinks(cm, p);
        }
        return;
    }
    /* Anything else: left exactly as found. Its host-side parts are kept
     * for its unplug's teardown; a USB3 tunnel that verifies on its own
     * stays an obligation; the router's capabilities are asked so its
     * notifications settle, and what hangs below it is discovered. */
    p->DpPreserved = other;
    if (cfgOk && usb3 && (d->Tbt3 || (v[5] & TBT_RTR5_UTO) != 0)) {
        p->Usb3 = tu;
        p->Usb3.Up = TRUE;
        p->Usb3.Preserved = TRUE;
        p->Usb3.Written |= TBT_U4_E_BANDWIDTH;
    }
    if (!d->Tbt3) {
        tbtU4GetCapabilities(cm, d);
    }
    tbtU4Hold(cm, p, TBT_U4_WHY_DISCOVERY);
    tbtU4DiscoverLinks(cm, p);
}

/* ---------------------------------------------------------------------- */
/* Links below depth 1 (record 22 sections 7.1, 9.2)                      */
/* ---------------------------------------------------------------------- */

/*
 * A preserved router's downstream ports, followed to depth 5 reading only
 * (Guide 3.2.1 step 2): each connected, unlocked lane 0 with an enumerated
 * router behind it is a link, checked each tick so that its removal is
 * noticed. The scratch router holds each parent in turn; links are
 * appended and walked breadth first.
 */
/* A router's Vendor Specific Extended 6 Capability (Spec 13.6.1.5; record
 * 22 section 8 item 3), found by walking its capability list from `first`
 * without a router record: each header's two DWs in one read; a Vendor
 * Specific Capability with VSC Length 0 is the Extended form, whose next
 * pointer is DW 1 bits 15:0 (Spec 8.2.1.3, 8.2.1.4). 0 for none. */
static ULONG tbtU4FindLc(PTBT_U4CM cm, const TBT_ROUTE *route, ULONG first)
{
    ULONG at;
    ULONG hdr[2];
    ULONG steps;

    at = first;
    for (steps = 0; steps < TBT_CAP_WALK_LIMIT; steps++) {
        if (at == 0 || at > TBT_CTL_ADDR_MASK - 1 ||
            tbtU4RdN(cm, route, TBT_CS_ROUTER, 0, at, 2, hdr) != TBT_OK) {
            break;
        }
        if (TBT_CAP_ID(hdr[0]) != TBT_RCAP_VSC) {
            at = TBT_CAP_NEXT(hdr[0]);
        } else if (TBT_CAP_VSEC_ID(hdr[0]) == TBT_VSEC_LINK_CONTROLLER) {
            return at;
        } else if (TBT_CAP_VSEC_LENGTH(hdr[0]) != 0) {
            at = TBT_CAP_NEXT(hdr[0]);
        } else {
            at = TBT_CAP_LONG_NEXT(hdr[1]);
        }
    }
    return 0;
}

/* Get Capabilities on every discovered router below depth 1 whose state
 * is not known yet (section 6.4: issued once on every USB4 router this
 * driver uses, preserved ones included), so its notifications take the
 * right acknowledgement. */
static VOID tbtU4LinkCaps(PTBT_U4CM cm, PTBT_U4_PORT p)
{
    ULONG i;
    PTBT_U4_ROUTER s;
    TBT_ROUTE keep;

    s = &cm->Scratch;
    keep = s->Route;
    for (i = 0; i < TBT_U4_MAX_LINKS; i++) {
        if (!cm->Links[i].Used || cm->Links[i].Port != p->Index ||
            cm->Links[i].CapsState != TBT_U4_CAPS_PENDING) {
            continue;
        }
        tbtU4Zero(s, sizeof(*s));
        s->Route = cm->Links[i].Child;
        s->CapsState = TBT_U4_CAPS_PENDING;
        tbtU4GetCapabilities(cm, s);
        cm->Links[i].CapsState = s->CapsState;
        cm->Links[i].CapsEnabled = s->CapsEnabled;
        if (!tbtU4Off(cm)) {
            TbtCtlSettle(&cm->Ctl, &cm->Links[i].Child);
        }
    }
    tbtU4Zero(s, sizeof(*s));
    s->Route = keep;
}

/* Section 6.2.3 item 4 part 1 on a discovered link below depth 1: the
 * parent's downstream lane 0 `a` and the child's upstream one (`v` its
 * ROUTER_CS_0 to 4), on each end that is a version 2 router. A write that
 * fails is logged; the link stays a predecessor's, served by its own
 * removal (section 7.1). */
static VOID tbtU4LinkElr(PTBT_U4CM cm, const TBT_U4_ROUTER *s, ULONG a,
                         const TBT_ROUTE *child, const ULONG *v)
{
    ULONG up;
    BOOLEAN ok;

    ok = tbtU4ElrClear(cm, s, a, s->Adp[a].CapPort);
    if (TbtHwRouterVersion(TBT_RTR4_VERSION(v[4])) == TBT_HW_ROUTER_USB4 &&
        TBT_USB4_VERSION_MAJOR(TBT_RTR4_VERSION(v[4])) == TBT_USB4_MAJOR_2) {
        up = TBT_RTR1_UP_ADP(v[1]);
        if (up != 0 &&
            !tbtU4ElrClearAt(cm, child, up, tbtU4AdpCapOf(cm, child, up, TBT_ACAP_USB4_PORT))) {
            ok = FALSE;
        }
    }
    if (!ok) {
        tbtU4Log(cm, "u4.elr.failed", (TbtRouteDepth(child) << 8) | a);
    }
}

static VOID tbtU4DiscoverLinks(PTBT_U4CM cm, PTBT_U4_PORT p)
{
    ULONG i;
    ULONG first;
    ULONG a;
    ULONG lane;
    ULONG cs4;
    ULONG v[9];
    TBT_ROUTE parent;
    TBT_ROUTE child;
    PTBT_U4_ROUTER s;
    PTBT_U4_LINK l;
    PTBT_U4_LINK cur;
    ULONG k;

    s = &cm->Scratch;
    first = 0;
    parent = p->Dev.Route;
    cur = NULL;
    for (;;) {
        tbtU4Zero(s, sizeof(*s));
        s->Route = parent;
        if (cur == NULL) {
            /* The port's own router: its DROM's unused adapters stay
             * unread here too. */
            s->DromUnused[0] = p->Dev.DromUnused[0];
            s->DromUnused[1] = p->Dev.DromUnused[1];
        }
        if (tbtU4RdN(cm, &s->Route, TBT_CS_ROUTER, 0, TBT_ROUTER_CS_0, 5, v) == TBT_OK) {
            tbtU4TakeHeader(s, v);
            if (cur != NULL && !s->Tbt3) {
                /* A router below depth 1: its DROM asked once, as the
                 * port's own router's (section 6.11), and the answer kept
                 * on its link for the next walk and for its removal. */
                if (!cur->ChildDromRead) {
                    tbtU4ReadDrom(cm, s);
                    cur->ChildDromUnused[0] = s->DromUnused[0];
                    cur->ChildDromUnused[1] = s->DromUnused[1];
                    cur->ChildDromRead = TRUE;
                }
                s->DromUnused[0] = cur->ChildDromUnused[0];
                s->DromUnused[1] = cur->ChildDromUnused[1];
            }
            if (tbtU4DiscoverAdapters(cm, s) == TBT_OK) {
                for (a = 1; a <= s->MaxAdp && a < TBT_U4_MAX_ADP; a++) {
                    if (!s->Adp[a].Present || s->Adp[a].Type != TBT_ADP_TYPE_LANE ||
                        s->Adp[a].CapLane == 0 || a == s->UpAdp || a == s->UpAdp + 1 ||
                        (s->Adp[a].CapPort == 0 && !s->Tbt3) ||
                        (s->Tbt3 && (a % 2) == 0)) {
                        continue;
                    }
                    if (TbtRouteDepth(&parent) >= TBT_U4_DEPTH_MAX ||
                        !TbtRouteChild(&parent, a, &child)) {
                        continue;
                    }
                    if (tbtU4Rd(cm, &s->Route, TBT_CS_ADAPTER, a,
                                s->Adp[a].CapLane + TBT_LANE_CS_1, &lane) != TBT_OK ||
                        TbtU4LaneClass(TBT_LANE1_STATE(lane)) != TBT_U4_LANE_CONNECTED ||
                        tbtU4Rd(cm, &s->Route, TBT_CS_ADAPTER, a, TBT_ADP_CS_4, &cs4) != TBT_OK ||
                        (cs4 & TBT_ADP4_LOCK) != 0) {
                        continue;
                    }
                    if (tbtU4RdN(cm, &child, TBT_CS_ROUTER, 0, TBT_ROUTER_CS_0, 9, v) != TBT_OK ||
                        (v[3] & TBT_RTR3_V) == 0) {
                        continue;
                    }
                    l = NULL;
                    for (k = 0; k < TBT_U4_MAX_LINKS; k++) {
                        if (!cm->Links[k].Used) {
                            l = &cm->Links[k];
                            break;
                        }
                    }
                    if (l == NULL) {
                        tbtU4Log(cm, "u4.links-full", a);
                        return;
                    }
                    l->Used = TRUE;
                    l->Port = p->Index;
                    l->Parent = parent;
                    l->ParentLane0 = a;
                    l->ParentLaneCap = s->Adp[a].CapLane;
                    l->Child = child;
                    l->ChildTbt3 = (BOOLEAN)(TbtHwRouterVersion(TBT_RTR4_VERSION(v[4])) ==
                                             TBT_HW_ROUTER_TBT3);
                    l->ChildLc = 0;
                    l->ChildUuid[0] = v[7];
                    l->ChildUuid[1] = v[8];
                    l->ChildUuid[2] = 0;
                    l->ChildUuid[3] = 0;
                    l->ChildUuidValid = TRUE;
                    l->RemovalPending = FALSE;
                    l->CapsState = TBT_U4_CAPS_PENDING;
                    l->CapsEnabled = 0;
                    l->ChildDromRead = FALSE;
                    l->ChildDromUnused[0] = 0;
                    l->ChildDromUnused[1] = 0;
                    if (l->ChildTbt3) {
                        /* Section 8: no router operation, plain
                         * acknowledgements, the LC's fused UUID - adopted
                         * from the first sample that reads. */
                        l->CapsState = TBT_U4_CAPS_DEFAULT;
                        l->ChildLc = tbtU4FindLc(cm, &child, TBT_RTR1_NEXT_CAP(v[1]));
                        l->ChildUuidValid = (BOOLEAN)(l->ChildLc != 0 &&
                            tbtU4RdN(cm, &child, TBT_CS_ROUTER, 0, l->ChildLc + TBT_LC_FUSE,
                                     4, l->ChildUuid) == TBT_OK);
                    }
                    tbtU4LinkElr(cm, s, a, &child, v);
                    cm->Counters.LinksDiscovered++;
                    tbtU4Log(cm, "u4.link", (TbtRouteDepth(&child) << 8) | a);
                }
            }
        }
        /* The next link not yet walked, in table order. */
        tbtU4LinkCaps(cm, p);
        for (i = first; i < TBT_U4_MAX_LINKS; i++) {
            if (cm->Links[i].Used && cm->Links[i].Port == p->Index &&
                TbtRouteDepth(&cm->Links[i].Child) < TBT_U4_DEPTH_MAX) {
                break;
            }
        }
        if (i >= TBT_U4_MAX_LINKS) {
            return;
        }
        parent = cm->Links[i].Child;
        cur = &cm->Links[i];
        first = i + 1;
    }
}

/* The removal of a router below depth 1 (section 7.1): on the parent, the
 * side of every path through the removed router - entries leading into its
 * lanes, entries arriving from them, Path Enable on the protocol adapters
 * that fed them - torn down with section 7.3's quarantine; the link and
 * every link below it forgotten. */
/*
 * The sweep of a removed router's link on its parent `s` (adapters
 * discovered), section 7.1: every entry that enters the link's lanes
 * (`lane0` and the lane above) or leaves them invalidated and drained, and
 * every protocol adapter whose HopID 8 entry leads into them has its Path
 * Enable proven clear, with section 7.3's quarantine under `port`. TRUE
 * when every entry was read and each one found open was closed or is
 * held by a record. Also record 34 section 6.7's residual pass.
 */
static BOOLEAN tbtU4SweepLane(PTBT_U4CM cm, ULONG port, const TBT_U4_ROUTER *s,
                              ULONG lane0)
{
    ULONG a;
    ULONG hop;
    ULONG e[2];
    BOOLEAN into;
    BOOLEAN from;
    BOOLEAN complete;

    complete = TRUE;
    for (a = 1; a <= s->MaxAdp && a < TBT_U4_MAX_ADP; a++) {
        if (!s->Adp[a].Present) {
            continue;
        }
        from = (BOOLEAN)(a == lane0 || a == lane0 + 1);
        for (hop = TBT_HOP_FIRST; hop <= s->Adp[a].MaxInHop; hop++) {
            if (tbtU4Entry(cm, &s->Route, a, hop, e) != TBT_OK) {
                complete = FALSE;   /* read again at the next tick */
                continue;
            }
            /* The output fields stay in an invalidated entry: a protocol
             * adapter whose HopID 8 led into the link has its Path Enable
             * proven clear at every pass, even once the entry itself is
             * invalid and drained. */
            into = (BOOLEAN)(TBT_PATH0_OUT_ADP(e[0]) == lane0 ||
                             TBT_PATH0_OUT_ADP(e[0]) == lane0 + 1);
            if (into && hop == TBT_HOP_PROTOCOL && s->Adp[a].CapAdp != 0 &&
                (s->Adp[a].Type == TBT_ADP_TYPE_PCIE_DOWN ||
                 s->Adp[a].Type == TBT_ADP_TYPE_USB3_DOWN)) {
                if (!tbtU4Disable(cm, port, &s->Route, a, s->Adp[a].CapAdp,
                                  (BOOLEAN)(s->Adp[a].Type == TBT_ADP_TYPE_USB3_DOWN)) &&
                    !tbtU4Quarantined(cm, &s->Route, a, 0, TRUE)) {
                    complete = FALSE;   /* no record holds it: again */
                }
            }
            if ((e[0] & TBT_PATH0_VALID) == 0 && (e[1] & TBT_PATH1_PP) == 0) {
                continue;
            }
            if (!into && !from) {
                continue;
            }
            if (!tbtU4Invalidate(cm, port, &s->Route, a, hop) &&
                !tbtU4Quarantined(cm, &s->Route, a, hop, FALSE)) {
                complete = FALSE;       /* no record holds it: again */
            }
        }
    }
    return complete;
}

static VOID tbtU4RemoveLink(PTBT_U4CM cm, ULONG index)
{
    PTBT_U4_LINK l;
    PTBT_U4_ROUTER s;
    ULONG v[5];
    ULONG k;
    BOOLEAN first;
    BOOLEAN complete;
    TBT_ROUTE removed;
    PTBT_U4_PORT p;

    l = &cm->Links[index];
    removed = l->Child;
    s = &cm->Scratch;
    tbtU4Zero(s, sizeof(*s));
    s->Route = l->Parent;
    p = tbtU4PortOf(cm, l->Port);
    if (p != NULL && TbtRouteEqual(&s->Route, &p->Dev.Route)) {
        s->DromUnused[0] = p->Dev.DromUnused[0];
        s->DromUnused[1] = p->Dev.DromUnused[1];
    }
    for (k = 0; k < TBT_U4_MAX_LINKS; k++) {
        if (cm->Links[k].Used && TbtRouteEqual(&cm->Links[k].Child, &s->Route)) {
            /* A parent below depth 1: the mask its DROM gave (DiscoverLinks). */
            s->DromUnused[0] = cm->Links[k].ChildDromUnused[0];
            s->DromUnused[1] = cm->Links[k].ChildDromUnused[1];
        }
    }
    first = (BOOLEAN)!l->RemovalPending;
    complete = FALSE;
    if (first) {
        cm->Counters.LinksRemoved++;
        tbtU4Log(cm, "u4.link-removed", l->ParentLane0);
    }
    if (tbtU4RdN(cm, &s->Route, TBT_CS_ROUTER, 0, TBT_ROUTER_CS_0, 5, v) == TBT_OK) {
        tbtU4TakeHeader(s, v);
        if (tbtU4DiscoverAdapters(cm, s) == TBT_OK) {
            complete = tbtU4SweepLane(cm, l->Port, s, l->ParentLane0);
        }
    }
    /* The parent unreadable: its own check decides (its removal drops this
     * link with it); an entry unread: the link stays, its removal pending,
     * until every entry of the parent has been read and closed. Links
     * below it are gone either way. */
    for (k = 0; k < TBT_U4_MAX_LINKS; k++) {
        if (k == index && !complete) {
            continue;
        }
        if (cm->Links[k].Used && TbtRouteDepth(&cm->Links[k].Child) >=
            TbtRouteDepth(&removed)) {
            ULONG lv;
            BOOLEAN under;

            under = TRUE;
            for (lv = 0; lv < TbtRouteDepth(&removed); lv++) {
                if (TbtRouteLevel(&cm->Links[k].Child, lv) != TbtRouteLevel(&removed, lv)) {
                    under = FALSE;
                }
            }
            if (under) {
                TbtCtlForget(&cm->Ctl, &cm->Links[k].Child);
                cm->Links[k].Used = FALSE;
            }
        }
    }
    if (!complete) {
        l->RemovalPending = TRUE;
    }
    if (first && p != NULL) {
        tbtU4Queue(cm, TBT_U4_ACT_DEEP_REMOVED, p);     /* once */
    }
}

/* Each tick: every link's parent lane, its Lock and the child's identity. */
static VOID tbtU4CheckLinks(PTBT_U4CM cm)
{
    ULONG i;
    ULONG lane;
    ULONG cs4;
    ULONG v[2];
    ULONG u[4];
    LONG rc;
    BOOLEAN gone;
    PTBT_U4_LINK l;

    for (i = 0; i < TBT_U4_MAX_LINKS; i++) {
        l = &cm->Links[i];
        if (!l->Used) {
            continue;
        }
        gone = FALSE;
        if (l->RemovalPending) {
            tbtU4RemoveLink(cm, i);     /* its parent's paths, again */
            continue;
        }
        rc = tbtU4Rd(cm, &l->Parent, TBT_CS_ADAPTER, l->ParentLane0,
                     l->ParentLaneCap + TBT_LANE_CS_1, &lane);
        if (rc != TBT_OK) {
            continue;   /* the parent itself: its own check decides */
        }
        if (TbtU4LaneClass(TBT_LANE1_STATE(lane)) == TBT_U4_LANE_DISCONNECTED) {
            gone = TRUE;
        } else if (tbtU4Rd(cm, &l->Parent, TBT_CS_ADAPTER, l->ParentLane0, TBT_ADP_CS_4,
                           &cs4) == TBT_OK && (cs4 & TBT_ADP4_LOCK) != 0) {
            gone = TRUE;
        } else {
            rc = tbtU4Rd(cm, &l->Child, TBT_CS_ROUTER, 0, TBT_ROUTER_CS_3, &v[0]);
            if (tbtU4Gone(cm, rc) || (rc == TBT_OK && (v[0] & TBT_RTR3_V) == 0)) {
                gone = TRUE;
            } else if (!l->ChildTbt3) {
                rc = tbtU4RdN(cm, &l->Child, TBT_CS_ROUTER, 0, TBT_ROUTER_CS_7, 2, v);
                if (tbtU4Gone(cm, rc) ||
                    (rc == TBT_OK && (v[0] != l->ChildUuid[0] || v[1] != l->ChildUuid[1]))) {
                    gone = TRUE;
                }
            } else {
                if (l->ChildLc == 0 &&
                    tbtU4Rd(cm, &l->Child, TBT_CS_ROUTER, 0, TBT_ROUTER_CS_1, &v[0]) == TBT_OK) {
                    l->ChildLc = tbtU4FindLc(cm, &l->Child, TBT_RTR1_NEXT_CAP(v[0]));
                }
                rc = l->ChildLc == 0 ? TBT_E_STATE :
                     tbtU4RdN(cm, &l->Child, TBT_CS_ROUTER, 0, l->ChildLc + TBT_LC_FUSE, 4, u);
                if (tbtU4Gone(cm, rc)) {
                    gone = TRUE;
                } else if (rc == TBT_OK && !l->ChildUuidValid) {
                    l->ChildUuid[0] = u[0];
                    l->ChildUuid[1] = u[1];
                    l->ChildUuid[2] = u[2];
                    l->ChildUuid[3] = u[3];
                    l->ChildUuidValid = TRUE;
                } else if (rc == TBT_OK &&
                           (u[0] != l->ChildUuid[0] || u[1] != l->ChildUuid[1] ||
                            u[2] != l->ChildUuid[2] || u[3] != l->ChildUuid[3])) {
                    gone = TRUE;
                }
            }
        }
        if (gone || l->RemovalPending) {
            tbtU4RemoveLink(cm, i);
        }
    }
}

/* Forget a port's links (its depth-1 router left). */
static VOID tbtU4DropLinks(PTBT_U4CM cm, PTBT_U4_PORT p)
{
    ULONG i;

    for (i = 0; i < TBT_U4_MAX_LINKS; i++) {
        if (cm->Links[i].Used && cm->Links[i].Port == p->Index) {
            TbtCtlForget(&cm->Ctl, &cm->Links[i].Child);
            cm->Links[i].Used = FALSE;
        }
    }
}

/* ---------------------------------------------------------------------- */
/* Explicit cleaning (record 22 section 9.3)                              */
/* ---------------------------------------------------------------------- */

/* The cleaning's sweep of one host adapter (record 22 section 9.3): each
 * entry from HopID 8 to the adapter's Max Input HopID that reads Valid or
 * Pending Packets is torn down as Guide 5.2.2 step 2 says. */
static VOID tbtU4CleanAdapter(PTBT_U4CM cm, PTBT_U4_PORT p, ULONG adp)
{
    PTBT_U4_ROUTER h;
    ULONG hop;
    ULONG e[2];

    h = &cm->HostRouter;
    /* Lane 1 is Lane0 + 1, past the table for a lane 0 at adapter 63. */
    if (adp == 0 || adp >= TBT_U4_MAX_ADP || !h->Adp[adp].Present) {
        return;
    }
    for (hop = TBT_HOP_FIRST; hop <= h->Adp[adp].MaxInHop; hop++) {
        if (tbtU4Entry(cm, &h->Route, adp, hop, e) != TBT_OK) {
            continue;
        }
        if ((e[0] & TBT_PATH0_VALID) == 0 && (e[1] & TBT_PATH1_PP) == 0) {
            continue;
        }
        (void)tbtU4Invalidate(cm, p->Index, &h->Route, adp, hop);
    }
}

/*
 * Cleaning one port (XhciCmPreOsCleanup 1 or 2; record 22 section 9.3):
 * the orderly teardown of both tunnels with both ends reachable; the
 * statement and the tunnel ports' removal; a Downstream Port Reset on the
 * host's port (Guide 7.3 steps 2 to 4: PORT_CS_19 DPR set, at least 10 ms,
 * cleared; Spec 6.9 tClearDPR); the port's host-side entries deactivated;
 * the port EMPTY, to be connected afresh once its link retrains. The
 * port's own sequence, as an eject is (TbtU4Eject).
 */
static VOID tbtU4Clean(PTBT_U4CM cm, PTBT_U4_PORT p, BOOLEAN portReset)
{
    PTBT_U4_ROUTER h;
    ULONG region;
    BOOLEAN closed;
    BOOLEAN hadPcie;
    TBT_WAIT w;
    ULONG lane;
    ULONG prev;

    h = &cm->HostRouter;
    cm->Counters.PortsCleaned++;
    tbtU4Log(cm, "u4.clean", p->Index);
    prev = cm->ActivePort;
    cm->ActivePort = p->Index;
    /* A deferred as-found connect (DeferFirstPoll) was for the preserved
     * configuration this cleaning removes: the port connects normally. */
    p->AsFoundOwed = FALSE;
    hadPcie = (BOOLEAN)(p->Pcie.Written != 0);
    closed = TRUE;
    if (p->Pcie.Written != 0) {
        p->Pcie.Written |= TBT_U4_E_HOST_ENABLE | TBT_U4_E_DEV_ENABLE;
        closed = tbtU4TunnelDown(cm, p, &p->Pcie, FALSE);
        if (closed && p->PcieDown != 0) {
            /* Section 10.2: closed means read back, the host adapter's
             * LTSSM in Detect included. */
            closed = tbtU4WaitDetect(cm, NULL, 0, h, p->PcieDown, TBT_U4_DETECT_US);
        }
    }
    if (p->Usb3.Written != 0) {
        p->Usb3.Written |= TBT_U4_E_HOST_ENABLE | TBT_U4_E_DEV_ENABLE;
        (void)tbtU4Usb3Down(cm, p);
    }
    if (hadPcie) {
        tbtU4LinkDownUpcall(cm, p, closed, FALSE);
        tbtU4Queue(cm, TBT_U4_ACT_TUNNEL_DOWN, p);
    }
    if (!portReset) {
        /* Steps 1 and 2 only: the Host Router Reset that follows does the
         * rest for the whole domain. */
        cm->ActivePort = prev;
        return;
    }
    if (h->Tbt3) {
        /* Step 3 on a Thunderbolt host: the link controller's port reset,
         * generations 2 and 3 ([linux] lc.c tb_lc_reset_port: set, 10 ms,
         * clear); generation 1 has none, so the port's paths are torn
         * down alone (record 22 section 19.4 item 7). */
        region = h->TbtGen >= 2 ? tbtU4LcPort(cm, h, p->Lane0) : 0;
        if (region != 0) {
            /* Owned before the write, whose answer may be lost; released
             * and read back clear here, else by tbtU4HostRestore before
             * the port connects again. */
            p->LcDprOwed = TRUE;
            if (tbtU4Rmw(cm, &h->Route, TBT_CS_ROUTER, 0, region + TBT_LC_PORT_MODE, 0,
                         TBT_LC_PORT_MODE_DPR) == TBT_OK) {
                cm->Counters.DownstreamPortResets++;
                tbtU4WaitMin(cm, TBT_U4_DPR_HOLD_US);
            }
            (void)tbtU4LcDprRelease(cm, p);
        }
    } else if (h->Adp[p->Lane0].CapPort != 0 &&
        tbtU4Rmw(cm, &h->Route, TBT_CS_ADAPTER, p->Lane0,
                 h->Adp[p->Lane0].CapPort + TBT_PORT_CS_19, 0, TBT_PORT19_DPR) == TBT_OK) {
        cm->Counters.DownstreamPortResets++;
        tbtU4WaitMin(cm, TBT_U4_DPR_HOLD_US);
        (void)tbtU4Rmw(cm, &h->Route, TBT_CS_ADAPTER, p->Lane0,
                       h->Adp[p->Lane0].CapPort + TBT_PORT_CS_19, TBT_PORT19_DPR, 0);
    }
    tbtU4CleanAdapter(cm, p, p->Lane0);
    tbtU4CleanAdapter(cm, p, p->Lane0 + 1);
    tbtU4CleanAdapter(cm, p, p->PcieDown);
    tbtU4CleanAdapter(cm, p, p->Usb3Down);
    tbtU4DropLinks(cm, p);
    TbtCtlForget(&cm->Ctl, &p->Dev.Route);
    tbtU4Zero(&p->Dev, sizeof(p->Dev));
    tbtU4Zero(&p->Pcie, sizeof(p->Pcie));
    tbtU4Zero(&p->Usb3, sizeof(p->Usb3));
    p->Bonded = FALSE;
    p->SingleLane = FALSE;
    p->Unlocked = FALSE;
    tbtU4XdEarlyEnd(cm, p);
    p->DpPreserved = FALSE;
    p->State = TBT_U4_EMPTY;
    p->Why = TBT_U4_WHY_NONE;
    p->CheckPending = TRUE;
    /* The link retrains: lane 0 back in CL0 within 2 s before the fresh
     * connect (section 5.2). */
    TbtWaitStart(&w, cm->Host, TBT_U4_DPR_CL0_US, FALSE);
    while (!TbtWaitDone(&w, cm->Host)) {
        if (tbtU4LaneRd(cm, h, p->Lane0, &lane) == TBT_OK &&
            TBT_LANE1_STATE(lane) == TBT_LANE_STATE_CL0) {
            break;
        }
        tbtU4Pause(cm, &w);
    }
    cm->ActivePort = prev;
}

/* ---------------------------------------------------------------------- */
/* Port checks and the idle tick (record 22 sections 7.1, 7.2)            */
/* ---------------------------------------------------------------------- */

/* Section 7.1 item 4: the PCIe tunnel lost with the router still there -
 * Path Enable read clear or LTSSM Detect, or a transitional LTSSM for 2 s.
 * Only that tunnel is torn down, in the orderly form; the port is FAILED
 * for PCIe until a re-plug, its USB3 tunnel kept. The port's own sequence,
 * as an eject is (TbtU4Eject). */
/* The loss sequence itself, run as the port's own sequence by its caller
 * (ActivePort): the kids' tunnels, then the port's, in the orderly form;
 * the statement and the removal; FAILED for PCIe. `s` the LTSSM logged.
 * Also a recovery check's lost tunnel (section 6.2.3 item 4 part 4). */
static VOID tbtU4PcieLost(PTBT_U4CM cm, PTBT_U4_PORT p, ULONG s)
{
    PTBT_U4_ROUTER h;
    BOOLEAN closed;

    h = &cm->HostRouter;
    cm->Counters.PcieLost++;
    tbtU4Log(cm, "u4.pcie.lost", (p->Index << 8) | s);
    /* Record 34 section 6.3: both routers there, so each kid's tunnel is
     * torn down in the orderly form first, then the parent's. */
    tbtU4KidsDown(cm, p, TBT_U4_WHY_LOST);
    p->Pcie.Written |= TBT_U4_E_HOST_ENABLE | TBT_U4_E_DEV_ENABLE;
    closed = tbtU4TunnelDown(cm, p, &p->Pcie, FALSE);
    if (closed) {
        closed = tbtU4WaitDetect(cm, NULL, 0, h, p->PcieDown, TBT_U4_DETECT_US);
    }
    cm->Counters.PcieTornDown++;
    tbtU4LinkDownUpcall(cm, p, closed, FALSE);
    tbtU4Queue(cm, TBT_U4_ACT_TUNNEL_DOWN, p);
    p->Transitional = FALSE;
    p->PcieFailed = TRUE;
    p->State = TBT_U4_FAILED;
    p->Why = TBT_U4_WHY_LOST;
}

static VOID tbtU4PcieLoss(PTBT_U4CM cm, PTBT_U4_PORT p, BOOLEAN watch)
{
    PTBT_U4_ROUTER h;
    ULONG v;
    ULONG s;
    ULONG prev;
    BOOLEAN lost;
    TBT_WAIT w;

    h = &cm->HostRouter;
    if (!p->Pcie.Up || p->PcieDown == 0 ||
        tbtU4Rd(cm, &h->Route, TBT_CS_ADAPTER, p->PcieDown,
                h->Adp[p->PcieDown].CapAdp + TBT_PCIE_CS_0, &v) != TBT_OK) {
        return;
    }
    prev = cm->ActivePort;
    cm->ActivePort = p->Index;
    s = tbtU4Ltssm(cm, v);
    if (watch && !(p->Transitional &&
                   tbtU4Now(cm) - p->TransitionalSinceUs >= TBT_U4_TRANSITIONAL_US)) {
        /* No later tick will look again (a resume): a transitional state
         * not already watched to its 2 s is watched here, the answer
         * decided now. */
        TbtWaitStart(&w, cm->Host, TBT_U4_TRANSITIONAL_US, FALSE);
        while ((v & TBT_PCIE0_PE) != 0 && s != TBT_LTSSM_DETECT &&
               s != TBT_LTSSM_L0 && s != TBT_LTSSM_L1 && s != TBT_LTSSM_RECOVERY &&
               s != TBT_LTSSM_RECOVERY_IDLE) {
            /* One deadline on the clock from the first transitional
             * sample (an earlier wait's included), checked at every
             * sample; the waited sum ends the wait but proves nothing. */
            if (!p->Transitional) {
                p->Transitional = TRUE;
                p->TransitionalSinceUs = w.StartUs;
            }
            if (tbtU4Now(cm) - p->TransitionalSinceUs >= TBT_U4_TRANSITIONAL_US ||
                TbtWaitDone(&w, cm->Host)) {
                break;
            }
            tbtU4Pause(cm, &w);
            if (tbtU4Rd(cm, &h->Route, TBT_CS_ADAPTER, p->PcieDown,
                        h->Adp[p->PcieDown].CapAdp + TBT_PCIE_CS_0, &v) != TBT_OK) {
                goto done;
            }
            s = tbtU4Ltssm(cm, v);
        }
    }
    lost = FALSE;
    if ((v & TBT_PCIE0_PE) == 0 || s == TBT_LTSSM_DETECT) {
        lost = TRUE;
    } else if (s == TBT_LTSSM_L0 || s == TBT_LTSSM_L1 || s == TBT_LTSSM_RECOVERY ||
               s == TBT_LTSSM_RECOVERY_IDLE) {
        p->Transitional = FALSE;
    } else if (!p->Transitional) {
        p->Transitional = TRUE;
        p->TransitionalSinceUs = tbtU4Now(cm);
    } else if (tbtU4Now(cm) - p->TransitionalSinceUs >= TBT_U4_TRANSITIONAL_US) {
        lost = TRUE;
    }
    if (lost) {
        tbtU4PcieLost(cm, p, s);
    }
done:
    cm->ActivePort = prev;
}

/* One port check: lane 0's class and Lock, and on a port with a router its
 * identity; the decision is the registers', never an event's polarity. */
static VOID tbtU4Check(PTBT_U4CM cm, PTBT_U4_PORT p)
{
    ULONG cls;
    BOOLEAN gone;

    cm->Counters.PortChecks++;
    p->CheckPending = FALSE;
    switch (p->State) {
    case TBT_U4_EMPTY:
        (void)tbtU4Evidence(cm, p, &cls);
        p->UnplugPending = FALSE;
        if (p->Barred) {
            /* Section 7.7 part 4: the retirement barrier - nothing is
             * built on the port until the old endpoint's devnode is
             * retired; checked again at every tick. */
            p->CheckPending = TRUE;
            break;
        }
        if ((cls == TBT_U4_LANE_CONNECTED || cls == TBT_U4_LANE_TRANSITIONAL) &&
            tbtU4RecCleanupGate(cm, p) && !p->DiscoverOwed) {
            /* A departed router's cleanup still owed is never the new
             * connection's restore: the gate has the router discovered as
             * found instead, at the next tick (DiscoverOwed). */
            tbtU4Connect(cm, p, FALSE);
        }
        break;
    case TBT_U4_XDOMAIN:
        /* Record 35 section 3.8: the peer ends on the port's removal
         * evidence; a lane 0 hot plug event alone (a bonding's end, IDS
         * 3.4.1) is only the hint that asked for this check. */
        gone = tbtU4Evidence(cm, p, &cls);
        if (gone || cls == TBT_U4_LANE_DISCONNECTED) {
            tbtU4XdEnd(cm, p, TRUE);
            tbtU4DropLinks(cm, p);
            tbtU4Teardown(cm, p);
            break;
        }
        p->UnplugPending = FALSE;
        break;
    case TBT_U4_UNSUPPORTED:
        gone = tbtU4Evidence(cm, p, &cls);
        if (gone || cls == TBT_U4_LANE_DISCONNECTED) {
            tbtU4XdEnd(cm, p, TRUE);
            tbtU4DropLinks(cm, p);
            tbtU4Teardown(cm, p);
        }
        break;
    case TBT_U4_UP:
    case TBT_U4_FAILED:
    case TBT_U4_HELD:
        gone = tbtU4Evidence(cm, p, &cls);
        if (gone) {
            if (cls == TBT_U4_LANE_CONNECTED) {
                cm->Counters.QuickReplugs++;
            }
            tbtU4DropLinks(cm, p);
            tbtU4Teardown(cm, p);
            p->RebuildPcie = FALSE;
            p->RebuildUsb3 = FALSE;
            if (!p->Barred &&
                (cls == TBT_U4_LANE_CONNECTED || cls == TBT_U4_LANE_TRANSITIONAL)) {
                tbtU4Connect(cm, p, FALSE);
            }
            break;
        }
        p->UnplugPending = FALSE;
        if (p->State == TBT_U4_UP) {
            tbtU4PcieLoss(cm, p, FALSE);
        }
        break;
    default:
        break;
    }
}

/* ---------------------------------------------------------------------- */
/* The minimal chain (design record 34 Part 3, task 45.15)                */
/* ---------------------------------------------------------------------- */

/*
 * A USB4 router behind a host port's device router, at depth 2, PCIe
 * only. The link view of record 34 section 6.2 is the parent's router
 * and downstream lane 0 in the host's place: the shared primitives take a
 * router and an adapter, and the kid's own sequences below use them so
 * that every depth-1 sequence stays as it was. A kid's work runs as its
 * host port's sequence (ActivePort) and keys its quarantine records by
 * the kid (KidCtx).
 */

static BOOLEAN tbtU4KidLanesIdle(PTBT_U4CM cm, const TBT_U4_ROUTER *pr, ULONG a);

static PTBT_U4_KID tbtU4KidOf(PTBT_U4CM cm, ULONG index)
{
    if (index == 0 || index > TBT_U4_MAX_KIDS || !cm->Kids[index - 1].Used) {
        return NULL;
    }
    return &cm->Kids[index - 1];
}

/* A downstream lane 0 adapter of a device router, as section 9.2's walk
 * (tbtU4DiscoverLinks) takes them. */
static BOOLEAN tbtU4DownLane0(const TBT_U4_ROUTER *rt, ULONG a)
{
    return (BOOLEAN)(a != 0 && a < TBT_U4_MAX_ADP && a <= rt->MaxAdp && rt->Adp[a].Present &&
                     rt->Adp[a].Type == TBT_ADP_TYPE_LANE && rt->Adp[a].CapLane != 0 &&
                     a != rt->UpAdp && a != rt->UpAdp + 1 &&
                     (rt->Adp[a].CapPort != 0 || rt->Tbt3) &&
                     (!rt->Tbt3 || (a % 2) != 0));
}

/*
 * Section 6.4: the protocol adapter of `type` paired with the parent's
 * downstream port at `lane0`. On a USB4 parent, the Nth adapter of the
 * type in increasing number belongs to its Nth downstream USB4 port (Spec
 * 5.2.5, the upstream port not counted [linux: usb4.c usb4_port_index]);
 * on a Thunderbolt 3 parent, the first PCIe Down adapter not taken by
 * another kid of the same parent and, with `requireFree`, neither in use
 * nor held (tbtU4AdapterFree; its DROM-unused adapters are never
 * discovered) [linux: tb.c tb_find_pcie_down]. 0 for none.
 */
static ULONG tbtU4KidPair(PTBT_U4CM cm, const TBT_U4_PORT *p, ULONG lane0, ULONG type,
                          BOOLEAN requireFree)
{
    const TBT_U4_ROUTER *rt;
    ULONG a;
    ULONG n;
    ULONG k;
    ULONG i;
    BOOLEAN taken;

    rt = &p->Dev;
    if (rt->Tbt3) {
        if (type != TBT_ADP_TYPE_PCIE_DOWN) {
            return 0;
        }
        for (a = 1; a <= rt->MaxAdp && a < TBT_U4_MAX_ADP; a++) {
            if (!rt->Adp[a].Present || rt->Adp[a].Type != type || rt->Adp[a].CapAdp == 0) {
                continue;
            }
            taken = FALSE;
            for (i = 0; i < TBT_U4_MAX_KIDS; i++) {
                if (cm->Kids[i].Used && cm->Kids[i].Port == p->Index &&
                    cm->Kids[i].ParentLane0 != lane0 && cm->Kids[i].ParentPcieDown == a) {
                    taken = TRUE;
                }
            }
            if (!taken && (!requireFree || tbtU4AdapterFree(cm, rt, a))) {
                return a;
            }
        }
        return 0;
    }
    n = 0;
    for (a = 1; a <= rt->MaxAdp && a < TBT_U4_MAX_ADP; a++) {
        if (tbtU4DownLane0(rt, a)) {
            if (a == lane0) {
                break;
            }
            n++;
        }
    }
    if (a > rt->MaxAdp || a >= TBT_U4_MAX_ADP) {
        return 0;
    }
    k = 0;
    for (a = 1; a <= rt->MaxAdp && a < TBT_U4_MAX_ADP; a++) {
        if (rt->Adp[a].Present && rt->Adp[a].CapAdp != 0 && rt->Adp[a].Type == type) {
            if (k == n) {
                return a;
            }
            k++;
        }
    }
    return 0;
}

/* Section 6.5: a kid's removal evidence from the registers - the
 * parent's downstream lane 0 in CLd, its Lock set again on a USB4 parent
 * whose lane this driver unlocked (or found unlocked), the child's
 * TopologyID Valid clear or its UUID changed, ERR_CONN or ERR_LOCK on its
 * route. `cls` gets the lane's class. */
static BOOLEAN tbtU4KidEvidence(PTBT_U4CM cm, PTBT_U4_PORT p, PTBT_U4_KID k, PULONG cls)
{
    PTBT_U4_ROUTER pr;
    PTBT_U4_ROUTER d;
    ULONG lane;
    ULONG cs4;
    ULONG v;
    ULONG u[2];
    LONG rc;

    pr = &p->Dev;
    d = &k->Dev;
    *cls = TBT_U4_LANE_UNKNOWN;
    if (tbtU4LaneRd(cm, pr, k->ParentLane0, &lane) != TBT_OK) {
        return FALSE;
    }
    *cls = TbtU4LaneClass(TBT_LANE1_STATE(lane));
    if (*cls == TBT_U4_LANE_DISCONNECTED) {
        return TRUE;
    }
    if (!pr->Tbt3) {
        if (tbtU4Rd(cm, &pr->Route, TBT_CS_ADAPTER, k->ParentLane0, TBT_ADP_CS_4,
                    &cs4) != TBT_OK) {
            return FALSE;
        }
        if (k->Unlocked && (cs4 & TBT_ADP4_LOCK) != 0) {
            return TRUE;
        }
    }
    if (!d->Present) {
        return FALSE;
    }
    rc = tbtU4Rd(cm, &d->Route, TBT_CS_ROUTER, 0, TBT_ROUTER_CS_3, &v);
    if (tbtU4Gone(cm, rc)) {
        return TRUE;
    }
    if (rc != TBT_OK) {
        return FALSE;
    }
    if ((v & TBT_RTR3_V) == 0) {
        return TRUE;
    }
    rc = tbtU4RdN(cm, &d->Route, TBT_CS_ROUTER, 0, TBT_ROUTER_CS_7, 2, u);
    if (tbtU4Gone(cm, rc)) {
        return TRUE;
    }
    if (rc != TBT_OK) {
        return FALSE;
    }
    if (!d->UuidValid) {
        d->Uuid[0] = u[0];
        d->Uuid[1] = u[1];
        d->UuidValid = TRUE;
        return FALSE;
    }
    return (BOOLEAN)(u[0] != d->Uuid[0] || u[1] != d->Uuid[1]);
}

/*
 * Section 6.2's liveness rule: the kid's own generation unchanged; its
 * host port's still the one it connected under, UP with its PCIe tunnel
 * (the host port's pending unplug, the active port's, counting only with
 * removal evidence, tbtU4Live); and the kid's own pending unplug counting
 * only once its removal evidence is read - with none the hint is cleared
 * and the sequence goes on.
 */
static BOOLEAN tbtU4KidLive(PTBT_U4CM cm, PTBT_U4_PORT p, PTBT_U4_KID k, ULONG gen)
{
    ULONG cls;

    if (!k->Used || k->Generation != gen) {
        return FALSE;
    }
    if (!tbtU4Live(cm, p, k->ParentGeneration) || p->State != TBT_U4_UP ||
        !p->Pcie.Up || !p->Dev.Present) {
        return FALSE;
    }
    if (k->UnplugPending) {
        if (cm->InTeardown || tbtU4KidEvidence(cm, p, k, &cls)) {
            return FALSE;
        }
        k->UnplugPending = FALSE;
        return (BOOLEAN)(k->Used && k->Generation == gen);
    }
    return TRUE;
}

/* A kid's action: its host port, the kid, the kid's generation; the
 * host's adapter and mapping for the log. */
static VOID tbtU4QueueKid(PTBT_U4CM cm, ULONG kind, PTBT_U4_PORT p, PTBT_U4_KID k)
{
    PTBT_U4_ACTION a;

    if (cm->Recovering) {
        cm->Counters.ContainedRetired++;
        return;
    }
    if (cm->ActionCount >= TBT_U4_ACTION_QUEUE) {
        cm->Counters.ActionOverflows++;
        cm->ResyncPending = TRUE;
        return;
    }
    a = &cm->Actions[(cm->ActionHead + cm->ActionCount) % TBT_U4_ACTION_QUEUE];
    a->Kind = kind;
    a->Port = p->Index;
    a->Kid = k->Index;
    a->Generation = k->Generation;
    a->PcieDown = p->PcieDown;
    a->MapValid = p->MapValid;
    a->MapRid = p->MapRid;
    cm->ActionCount++;
}

/* Steps 2 and 3 of a kid's teardown (section 6.7): the driver's reading
 * and statement decision, with the kid named. No anchor (no statement at
 * depth 2 before task 45.18, record 34 section 12 Q4); the host port's
 * isolation word is the host port's alone and is never written here. */
static VOID tbtU4KidUpcall(PTBT_U4CM cm, PTBT_U4_PORT p, PTBT_U4_KID k, BOOLEAN closed)
{
    TBT_U4_LINKDOWN info;
    ULONG r;

    if (tbtU4Off(cm) || cm->Recovering) {
        cm->Counters.StatementsWithheld++;
        return;
    }
    tbtU4Zero(&info, sizeof(info));
    info.Port = p->Index;
    info.Kid = k->Index;
    info.Generation = k->Generation;
    info.PcieDown = p->PcieDown;
    info.MapValid = p->MapValid;
    info.MapRid = p->MapRid;
    info.PathClosed = closed;
    info.Nested = FALSE;
    r = TBT_U4_STMT_WITHHELD;
    if (cm->LinkDown != NULL) {
        r = cm->LinkDown(cm->LinkDownContext, &info);
    }
    if (r == TBT_U4_STMT_SENT) {
        cm->Counters.StatementsSent++;
    } else if (r == TBT_U4_STMT_PENDING) {
        cm->Counters.StatementsPending++;
    } else {
        cm->Counters.StatementsWithheld++;
    }
}

/* A wait on one of the kid's lane adapters' state (the bonding's). */
static BOOLEAN tbtU4KidWaitLane(PTBT_U4CM cm, PTBT_U4_PORT p, PTBT_U4_KID k, ULONG gen,
                                const TBT_U4_ROUTER *rt, ULONG adp, ULONG state, ULONG us)
{
    ULONG v;
    TBT_WAIT w;

    TbtWaitStart(&w, cm->Host, us, FALSE);
    for (;;) {
        if (tbtU4LaneRd(cm, rt, adp, &v) != TBT_OK) {
            return FALSE;
        }
        if (TBT_LANE1_STATE(v) == state) {
            return TRUE;
        }
        if (TbtWaitDone(&w, cm->Host) || !tbtU4KidLive(cm, p, k, gen)) {
            return FALSE;
        }
        tbtU4Pause(cm, &w);
    }
}

/* A wait on a PCIe adapter's LTSSM reading `state` (Detect or L0). */
static BOOLEAN tbtU4KidWaitPcie(PTBT_U4CM cm, PTBT_U4_PORT p, PTBT_U4_KID k, ULONG gen,
                                const TBT_U4_ROUTER *rt, ULONG adp, ULONG state, ULONG us)
{
    ULONG v;
    TBT_WAIT w;

    TbtWaitStart(&w, cm->Host, us, FALSE);
    for (;;) {
        if (tbtU4Rd(cm, &rt->Route, TBT_CS_ADAPTER, adp,
                    rt->Adp[adp].CapAdp + TBT_PCIE_CS_0, &v) != TBT_OK) {
            return FALSE;
        }
        if (TBT_PCIE0_LTSSM(v) == state) {
            return TRUE;
        }
        if (TbtWaitDone(&w, cm->Host) ||
            (p != NULL && !tbtU4KidLive(cm, p, k, gen))) {
            return FALSE;
        }
        tbtU4Pause(cm, &w);
    }
}

/*
 * Section 6.4 steps 1 to 4: the parent's downstream lane 0 unlocked on a
 * USB4 parent (Guide 3.2.1; a Thunderbolt 3 parent has no Lock there
 * [linux: switch.c tb_port_unlock]); ROUTER_CS_0 to 6 at the child's
 * route (ERR_ENUM or ERR_NUA: someone else's, UNSUPPORTED; a reserved
 * USB4 Version or a Thunderbolt 3 child: UNSUPPORTED in the minimal cut);
 * enumerated at depth 2 with Router Ready, or a router found enumerated
 * at its route taken as found; the DROM, adapters, capabilities, UUID,
 * Get Capabilities and the Buffer Allocation Request, as at depth 1.
 */
static LONG tbtU4KidRouter(PTBT_U4CM cm, PTBT_U4_PORT p, PTBT_U4_KID k, ULONG gen,
                           PULONG next)
{
    PTBT_U4_ROUTER pr;
    PTBT_U4_ROUTER d;
    ULONG v[7];
    ULONG a;
    ULONG t;
    ULONG ver;
    LONG rc;

    pr = &p->Dev;
    d = &k->Dev;
    a = k->ParentLane0;
    *next = TBT_U4_FAILED;
    if (!pr->Tbt3) {
        rc = tbtU4Rd(cm, &pr->Route, TBT_CS_ADAPTER, a, TBT_ADP_CS_4, &v[0]);
        if (rc == TBT_OK && (v[0] & TBT_ADP4_LOCK) != 0) {
            rc = tbtU4Wr(cm, &pr->Route, TBT_CS_ADAPTER, a, TBT_ADP_CS_4,
                         v[0] & ~TBT_ADP4_LOCK);
        }
        if (rc != TBT_OK) {
            k->Why = TBT_U4_WHY_ENUMERATION;
            return tbtU4Gone(cm, rc) ? TBT_E_GONE : rc;
        }
        k->Unlocked = TRUE;
    }
    tbtU4Zero(d, sizeof(*d));
    if (!TbtRouteChild(&pr->Route, a, &d->Route)) {
        k->Why = TBT_U4_WHY_ENUMERATION;
        return TBT_E_STATE;
    }
    d->Depth = 2;
    d->CapsState = TBT_U4_CAPS_PENDING;
    rc = TBT_E_TIMEOUT;
    for (t = 0; t < TBT_U4_ENUM_TRIES; t++) {
        if (!tbtU4KidLive(cm, p, k, gen)) {
            return TBT_E_GONE;
        }
        rc = tbtU4RdN(cm, &d->Route, TBT_CS_ROUTER, 0, TBT_ROUTER_CS_0, 7, v);
        if (rc == TBT_E_NOTIFY && (cm->Ring->LastEvent == TBT_EV_ERR_ENUM ||
                                   cm->Ring->LastEvent == TBT_EV_ERR_NUA)) {
            *next = TBT_U4_UNSUPPORTED;
            k->Why = TBT_U4_WHY_ENUM_ERROR;
            return rc;
        }
        if (tbtU4Gone(cm, rc)) {
            return TBT_E_GONE;
        }
        if (rc == TBT_OK) {
            tbtU4TakeHeader(d, v);
            ver = TbtHwRouterVersion(d->Usb4Version);
            if (ver == TBT_HW_ROUTER_RESERVED || d->Tbt3) {
                *next = TBT_U4_UNSUPPORTED;
                k->Why = TBT_U4_WHY_VERSION;
                cm->Counters.RoutersRefused++;
                tbtU4Log(cm, "u4.kid.version", (k->Index << 24) | d->Usb4Version);
                return TBT_E_STATE;
            }
            d->Depth = 2;
            if ((v[3] & TBT_RTR3_V) != 0) {
                if (!tbtU4Enumerated(d, v)) {
                    *next = TBT_U4_UNSUPPORTED;
                    k->Why = TBT_U4_WHY_ENUMERATION;
                    cm->Counters.RoutersRefused++;
                    tbtU4Log(cm, "u4.kid.topology", v[3]);
                    return TBT_E_STATE;
                }
                if (!d->EnumeratedHere) {
                    d->Preserved = TRUE;
                }
                rc = (v[6] & TBT_RTR6_RR) != 0 ? TBT_OK : tbtU4WaitReady(cm, d);
                if (rc == TBT_OK) {
                    if (d->Preserved) {
                        cm->Counters.RoutersAdopted++;
                    }
                    break;
                }
            } else {
                rc = tbtU4Enumerate(cm, d, v);
                if (rc == TBT_OK) {
                    break;
                }
            }
            if (tbtU4Gone(cm, rc)) {
                return TBT_E_GONE;
            }
        }
        if (t + 1 < TBT_U4_ENUM_TRIES) {
            tbtU4WaitMin(cm, TBT_U4_ENUM_GAP_US);
        }
    }
    if (rc != TBT_OK) {
        k->Why = rc == TBT_E_TIMEOUT && d->EnumeratedHere ? TBT_U4_WHY_ROUTER_READY :
                 TBT_U4_WHY_ENUMERATION;
        tbtU4Log(cm, "u4.kid.enumeration", (ULONG)rc);
        return rc;
    }
    d->Present = TRUE;
    tbtU4ReadDrom(cm, d);
    if (!tbtU4KidLive(cm, p, k, gen)) {
        return TBT_E_GONE;
    }
    rc = (d->UpAdp == 0 || d->UpAdp + 1 >= TBT_U4_MAX_ADP) ? TBT_E_STATE :
         tbtU4DiscoverAdapters(cm, d);
    if (rc == TBT_OK) {
        rc = tbtU4RouterCaps(cm, d, TBT_RTR1_NEXT_CAP(v[1]));
    }
    if (rc != TBT_OK) {
        k->Why = TBT_U4_WHY_ENUMERATION;
        return tbtU4Gone(cm, rc) ? TBT_E_GONE : rc;
    }
    tbtU4ApplyQuirks(cm, d);
    d->UuidValid = (BOOLEAN)(tbtU4RdN(cm, &d->Route, TBT_CS_ROUTER, 0, TBT_ROUTER_CS_7, 2,
                                      d->Uuid) == TBT_OK);
    tbtU4GetCapabilities(cm, d);
    tbtU4BufferAllocation(cm, d, d->UpAdp);
    tbtU4Log(cm, "u4.kid.router", (d->Vendor << 16) | d->Device);
    return tbtU4KidLive(cm, p, k, gen) ? TBT_OK : TBT_E_GONE;
}

/* Section 6.4 step 5's mark: USB4 Port is Configured on the parent's
 * downstream port - PORT_CS_19 PC on a USB4 parent, on a Thunderbolt 3
 * parent its USB4 Port Region's LC_SX_CTRL Lane 0 or Lane 1 Configured
 * for the adapter, without UPSTREAM [linux: lc.c tb_lc_configure_port] -
 * then PC on the child's upstream port. Each owned before its write. */
static LONG tbtU4KidConfigured(PTBT_U4CM cm, PTBT_U4_PORT p, PTBT_U4_KID k)
{
    PTBT_U4_ROUTER pr;
    PTBT_U4_ROUTER d;
    ULONG a;
    ULONG region;
    LONG rc;

    pr = &p->Dev;
    d = &k->Dev;
    a = k->ParentLane0;
    k->PcSetParent = TRUE;
    rc = TBT_OK;
    if (pr->Tbt3) {
        rc = tbtU4RouterLcPort(cm, pr, a, &region);
        if (rc == TBT_OK && region != 0) {
            rc = tbtU4Rmw(cm, &pr->Route, TBT_CS_ROUTER, 0, region + TBT_LC_SX_CTRL, 0,
                          (a & 1UL) != 0 ? TBT_LC_SX_CTRL_L1C : TBT_LC_SX_CTRL_L2C);
        }
    } else if (pr->Adp[a].CapPort != 0) {
        rc = tbtU4Rmw(cm, &pr->Route, TBT_CS_ADAPTER, a,
                      pr->Adp[a].CapPort + TBT_PORT_CS_19, 0, TBT_PORT19_PC);
    }
    if (rc != TBT_OK) {
        return rc;
    }
    k->PcSetDev = TRUE;
    if (d->Adp[d->UpAdp].CapPort != 0) {
        rc = tbtU4Rmw(cm, &d->Route, TBT_CS_ADAPTER, d->UpAdp,
                      d->Adp[d->UpAdp].CapPort + TBT_PORT_CS_19, 0, TBT_PORT19_PC);
    }
    return rc;
}

/*
 * Section 6.4 step 5, record 22 section 6.5 through the link view: the
 * parent's downstream lane adapters in the host's role. Bonded (the link
 * already x2, or Guide 7.1's sequence: both lane 1 adapters in CL0,
 * Target Link Width dual on all four, Lane Bonding on the parent's lane
 * 0, x2 within TBT_U4_BONDING_US), else a single-lane link verified (both
 * lane 1 adapters CLd or Disabled; or the parent's lane 1 disabled with
 * Guide 7.5's preconditions); then the link marked configured.
 */
static LONG tbtU4KidBond(PTBT_U4CM cm, PTBT_U4_PORT p, PTBT_U4_KID k, ULONG gen)
{
    TBT_U4_LANES ln;
    PTBT_U4_ROUTER pr;
    PTBT_U4_ROUTER d;
    ULONG lane[3];
    ULONG v;
    ULONG i;
    ULONG st;
    ULONG kind;
    BOOLEAN bonded;
    TBT_WAIT w;

    pr = &p->Dev;
    d = &k->Dev;
    ln.Rt[0] = pr;
    ln.Rt[1] = pr;
    ln.Rt[2] = d;
    ln.Rt[3] = d;
    ln.Adp[0] = k->ParentLane0;
    ln.Adp[1] = k->ParentLane0 + 1;
    ln.Adp[2] = d->UpAdp;
    ln.Adp[3] = d->UpAdp + 1;
    cm->Counters.BondAttempts++;
    k->Why = TBT_U4_WHY_BONDING;
    if (tbtU4LaneRd(cm, pr, ln.Adp[0], &lane[0]) != TBT_OK) {
        return tbtU4KidLive(cm, p, k, gen) ? TBT_E_STATE : TBT_E_GONE;
    }
    if (tbtU4IsV2(pr) && tbtU4IsV2(d)) {
        /* As tbtU4Bond: speed and width are valid only on an active link
         * (Spec 8.2.2.3), so a lane 0 still training is waited for with
         * no write, and one that never reads connected leaves the kid
         * FAILED with no lane written. */
        TbtWaitStart(&w, cm->Host, TBT_U4_CONNECT_US, FALSE);
        while (TbtU4LaneClass(TBT_LANE1_STATE(lane[0])) != TBT_U4_LANE_CONNECTED) {
            if (!tbtU4KidLive(cm, p, k, gen)) {
                return TBT_E_GONE;
            }
            if (TbtU4LaneClass(TBT_LANE1_STATE(lane[0])) != TBT_U4_LANE_TRANSITIONAL ||
                TbtWaitDone(&w, cm->Host)) {
                tbtU4Log(cm, "u4.kid.link.not-active",
                         (k->Index << 8) | TBT_LANE1_STATE(lane[0]));
                k->Why = TBT_U4_WHY_LANE_STATE;
                return TBT_E_STATE;
            }
            tbtU4Pause(cm, &w);
            if (tbtU4LaneRd(cm, pr, ln.Adp[0], &lane[0]) != TBT_OK) {
                return tbtU4KidLive(cm, p, k, gen) ? TBT_E_STATE : TBT_E_GONE;
            }
        }
    }
    bonded = (BOOLEAN)(TBT_LANE1_NLW(lane[0]) == TBT_LANE_WIDTH_X2);
    if (TBT_LANE1_SPEED(lane[0]) == TBT_LANE_SPEED_GEN4) {
        /* Record 22 section 6.2.3 through the link view: a Gen 4 link is
         * aggregated by its initialisation, so no Target Link Width, Lane
         * Bonding or Lane Disable is written. Symmetric, or a paired
         * asymmetric width used as found (decision U31): bonded. Any
         * other Gen 4 reading: FAILED, nothing written. */
        lane[1] = 0;
        if (!bonded && tbtU4LaneRd(cm, d, ln.Adp[2], &lane[1]) != TBT_OK) {
            return tbtU4KidLive(cm, p, k, gen) ? TBT_E_STATE : TBT_E_GONE;
        }
        kind = tbtU4LinkKind(lane[0], lane[1]);
        if (kind != TBT_U4_LINK_GEN4 && kind != TBT_U4_LINK_GEN4_ASYM) {
            cm->Counters.Gen4Unusable++;
            tbtU4Log(cm, "u4.kid.link.gen4-unusable",
                     (k->Index << 8) | TBT_LANE1_NLW(lane[0]));
            k->Why = TBT_U4_WHY_LINK;
            return TBT_E_STATE;
        }
        k->Gen4 = TRUE;
        k->Asym = (BOOLEAN)(kind == TBT_U4_LINK_GEN4_ASYM);
        cm->Counters.Gen4Links++;
        if (k->Asym) {
            cm->Counters.Gen4Asym++;
        }
        tbtU4Log(cm, "u4.kid.link.gen4", (k->Index << 8) | TBT_LANE1_NLW(lane[0]));
        bonded = TRUE;
    }
    if (!bonded && cm->Config.LaneBonding &&
        d->Adp[ln.Adp[2]].CapPort != 0 &&
        tbtU4Rd(cm, &d->Route, TBT_CS_ADAPTER, ln.Adp[2],
                d->Adp[ln.Adp[2]].CapPort + TBT_PORT_CS_18, &v) == TBT_OK &&
        (v & TBT_PORT18_BE) != 0 &&
        tbtU4Rd(cm, &pr->Route, TBT_CS_ADAPTER, ln.Adp[0],
                pr->Adp[ln.Adp[0]].CapLane + TBT_LANE_CS_0, &v) == TBT_OK &&
        (v & TBT_LANE0_SLW_X2) != 0 &&
        d->Adp[ln.Adp[2]].CapLane != 0 &&
        tbtU4Rd(cm, &d->Route, TBT_CS_ADAPTER, ln.Adp[2],
                d->Adp[ln.Adp[2]].CapLane + TBT_LANE_CS_0, &v) == TBT_OK &&
        (v & TBT_LANE0_SLW_X2) != 0 &&
        tbtU4KidWaitLane(cm, p, k, gen, ln.Rt[1], ln.Adp[1], TBT_LANE_STATE_CL0,
                         TBT_U4_LANE1_CL0_US) &&
        tbtU4KidWaitLane(cm, p, k, gen, ln.Rt[3], ln.Adp[3], TBT_LANE_STATE_CL0,
                         TBT_U4_LANE1_CL0_US)) {
        for (i = 0; i < 4; i++) {
            if (tbtU4LaneRd(cm, ln.Rt[i], ln.Adp[i], &k->LaneSaved[i]) != TBT_OK) {
                break;
            }
        }
        if (i == 4) {
            k->LaneWritten = TRUE;      /* owned before the writes */
            for (i = 0; i < 4; i++) {
                if (tbtU4LaneRmw(cm, ln.Rt[i], ln.Adp[i], TBT_LANE1_TLW_MASK,
                                 TBT_LANE1_TLW_DUAL) != TBT_OK) {
                    break;
                }
            }
            if (i == 4 &&
                tbtU4LaneRmw(cm, ln.Rt[0], ln.Adp[0], 0, TBT_LANE1_LB) == TBT_OK) {
                TbtWaitStart(&w, cm->Host, TBT_U4_BONDING_US, FALSE);
                for (;;) {
                    if (tbtU4LaneRd(cm, ln.Rt[0], ln.Adp[0], &v) == TBT_OK &&
                        TBT_LANE1_NLW(v) == TBT_LANE_WIDTH_X2) {
                        bonded = TRUE;
                        break;
                    }
                    if (!tbtU4KidLive(cm, p, k, gen) || TbtWaitDone(&w, cm->Host)) {
                        break;
                    }
                    tbtU4Pause(cm, &w);
                }
            }
        }
    }
    if (!tbtU4KidLive(cm, p, k, gen)) {
        return TBT_E_GONE;
    }
    if (bonded) {
        k->Bonded = TRUE;
        if (!k->Gen4) {
            cm->Counters.BondX2++;
        }
    } else {
        /* The single-lane fallback, verified (record 22 section 6.5 step
         * 6): what the bonding wrote put back, then either both lane 1
         * adapters down already, or the parent's lane 1 disabled. */
        if (k->LaneWritten) {
            for (i = 0; i < 4; i++) {
                if (tbtU4LaneRmw(cm, ln.Rt[i], ln.Adp[i], TBT_LANE1_TLW_MASK | TBT_LANE1_LB,
                                 k->LaneSaved[i] & (TBT_LANE1_TLW_MASK | TBT_LANE1_LB)) !=
                    TBT_OK) {
                    return TBT_E_STATE;
                }
            }
            k->LaneWritten = FALSE;
        }
        if (tbtU4LaneRd(cm, ln.Rt[1], ln.Adp[1], &lane[1]) != TBT_OK ||
            tbtU4LaneRd(cm, ln.Rt[3], ln.Adp[3], &lane[2]) != TBT_OK ||
            tbtU4LaneRd(cm, ln.Rt[0], ln.Adp[0], &lane[0]) != TBT_OK) {
            return TBT_E_STATE;
        }
        for (i = 1; i <= 2; i++) {
            st = TBT_LANE1_STATE(lane[i]);
            if (st != TBT_LANE_STATE_CLD && st != TBT_LANE_STATE_DISABLED) {
                break;
            }
        }
        if (i <= 2) {
            /* Guide 7.5: the lane at its defaults and the link's TMU Off
             * (none is enabled at depth 2; a predecessor's may be) before
             * a lane is disabled. */
            for (i = 0; i < 2; i++) {
                if ((lane[i] & TBT_LANE1_TLW_MASK) != TBT_LANE1_TLW_SINGLE ||
                    (lane[i] & (TBT_LANE1_LB | TBT_LANE1_CLX_EN)) != 0) {
                    return TBT_E_STATE;
                }
            }
            if (!tbtU4LinkTmuOff(cm, pr, ln.Adp[0], d)) {
                tbtU4Log(cm, "u4.kid.bond.tmu-not-off", k->Index);
                return TBT_E_STATE;
            }
            if (TBT_LANE1_STATE(lane[1]) != TBT_LANE_STATE_CL0 ||
                TBT_LANE1_NLW(lane[0]) != TBT_LANE_WIDTH_X1) {
                return TBT_E_STATE;
            }
            if (!pr->Tbt3) {
                /* Behind a Thunderbolt 3 parent the link stays two single
                 * lanes, as a Thunderbolt host's does (tbtU4Bond): Lane
                 * Disable is a USB4 lane adapter's (record 22 section
                 * 19.4 item 0). */
                k->Lane1Disabled = TRUE;    /* owned before the write */
                if (tbtU4LaneRmw(cm, ln.Rt[1], ln.Adp[1], 0, TBT_LANE1_LD) != TBT_OK ||
                    !tbtU4KidWaitLane(cm, p, k, gen, ln.Rt[1], ln.Adp[1],
                                      TBT_LANE_STATE_DISABLED, TBT_U4_LANE_DISABLE_US)) {
                    return tbtU4KidLive(cm, p, k, gen) ? TBT_E_STATE : TBT_E_GONE;
                }
                cm->Counters.Lane1Disabled++;
            }
        }
        k->SingleLane = TRUE;
        cm->Counters.BondSingle++;
    }
    if (tbtU4KidConfigured(cm, p, k) != TBT_OK) {
        tbtU4Log(cm, "u4.kid.port-configured.failed", k->Index);
        return tbtU4KidLive(cm, p, k, gen) ? TBT_E_STATE : TBT_E_GONE;
    }
    k->Why = TBT_U4_WHY_NONE;
    return tbtU4KidLive(cm, p, k, gen) ? TBT_OK : TBT_E_GONE;
}

/*
 * The link of a kid's TMU off again, on the parent's side always and on
 * the child's when `dev` (both there): AdapterTimeSyncInterval 0 and
 * Enable Enhanced Uni-Directional Mode 0, then Disable Time Sync set, the
 * child's inside a time disruption of the child (record 22 section 6.6
 * TMU item 6 through the link view); Disable Time Sync is attempted
 * whatever the mode's writes did. The parent's side is the kid's debt:
 * TmuConfigured stays set until it is proven, and the kid's teardown
 * keeps the slot TEARDOWN, so nothing connects on the link, until then.
 * The child's side goes with its next connection. TRUE when the parent's
 * side is back to its defaults (or was never configured).
 */
static BOOLEAN tbtU4KidTmuOff(PTBT_U4CM cm, PTBT_U4_PORT p, PTBT_U4_KID k, BOOLEAN dev)
{
    PTBT_U4_ROUTER pr;
    PTBT_U4_ROUTER d;
    LONG rc;
    BOOLEAN parent;

    if (!k->TmuConfigured) {
        return TRUE;
    }
    pr = &p->Dev;
    d = &k->Dev;
    rc = TBT_OK;
    if (dev && d->Present) {
        rc = tbtU4TmuTd(cm, d, TRUE);
        if (rc == TBT_OK) {
            rc = tbtU4TmuEnhancedOff(cm, d, d->UpAdp);
        }
        if (rc == TBT_OK) {
            rc = tbtU4TmuAdp(cm, d, d->UpAdp, TBT_TMU_ADP_CS_6, 0, TBT_TMU_ADP6_DTS);
        }
        if (tbtU4TmuTd(cm, d, FALSE) != TBT_OK && rc == TBT_OK) {
            rc = TBT_E_STATE;
        }
    }
    parent = (BOOLEAN)(tbtU4TmuEnhancedOff(cm, pr, k->ParentLane0) == TBT_OK);
    if (tbtU4TmuAdp(cm, pr, k->ParentLane0, TBT_TMU_ADP_CS_6, 0, TBT_TMU_ADP6_DTS) !=
        TBT_OK) {
        parent = FALSE;
    }
    if (rc != TBT_OK || !parent) {
        tbtU4Log(cm, "u4.kid.tmu.off-failed",
                 (p->Index << 16) | (k->Index << 8) | (parent ? 0UL : 1UL));
    }
    if (parent) {
        k->TmuConfigured = FALSE;
    }
    return parent;
}

/*
 * Record 22 section 6.2.2 item 6 through the link view: on the link of a
 * kid this driver enumerated, both ends version 2 routers with a TMU
 * (neither reporting Time Synchronization Protocol Not Supported) under
 * XhciCmUsb4V2 1, HiFi Enhanced Uni-Directional before CV, as at depth 1
 * (tbtU4TmuEnable's enhanced form): Disable Time Sync set on both lane 0
 * adapters (section 6.6 TMU item 1), the link's TMU read Off (else left
 * as found, logged), then inside a time disruption of the child its
 * local time posted from the host's, the mode set on the child's
 * upstream and the parent's downstream lane 0 adapters, Disable Time
 * Sync cleared on the child's then the parent's. A failure is logged and
 * counted, what was written undone, and the PCIe tunnel proceeds (TMU
 * item 5). Every other link of a kid: no TMU.
 */
static VOID tbtU4KidTmu(PTBT_U4CM cm, PTBT_U4_PORT p, PTBT_U4_KID k, ULONG gen)
{
    PTBT_U4_ROUTER pr;
    PTBT_U4_ROUTER d;
    ULONG a;
    ULONG v;
    LONG rc;

    pr = &p->Dev;
    d = &k->Dev;
    a = k->ParentLane0;
    if (!cm->Config.Usb4V2 || !tbtU4IsV2(pr) || !tbtU4IsV2(d) || !d->EnumeratedHere ||
        pr->CapTmu == 0 || d->CapTmu == 0 || pr->Adp[a].CapTmu == 0 ||
        d->Adp[d->UpAdp].CapTmu == 0) {
        return;
    }
    if (tbtU4Rd(cm, &d->Route, TBT_CS_ROUTER, 0, d->CapTmu + TBT_TMU_RTR_CS_0, &v) != TBT_OK ||
        (v & TBT_TMU_RTR0_TSP_NS) != 0 ||
        tbtU4Rd(cm, &pr->Route, TBT_CS_ROUTER, 0, pr->CapTmu + TBT_TMU_RTR_CS_0, &v) != TBT_OK ||
        (v & TBT_TMU_RTR0_TSP_NS) != 0) {
        return;
    }
    (void)tbtU4TmuAdp(cm, pr, a, TBT_TMU_ADP_CS_6, 0, TBT_TMU_ADP6_DTS);
    (void)tbtU4TmuAdp(cm, d, d->UpAdp, TBT_TMU_ADP_CS_6, 0, TBT_TMU_ADP6_DTS);
    if (!tbtU4LinkTmuOff(cm, pr, a, d)) {
        tbtU4Log(cm, "u4.kid.tmu.inherited", (p->Index << 8) | k->Index);
        return;
    }
    rc = tbtU4TmuTd(cm, d, TRUE);
    if (rc == TBT_OK) {
        rc = tbtU4TmuPostTimeTo(cm, p, k, gen);
    }
    if (rc == TBT_OK) {
        k->TmuConfigured = TRUE;    /* owned before its writes */
        rc = tbtU4TmuEnhancedOnAt(cm, pr, a, d);
    }
    if (rc == TBT_OK) {
        rc = tbtU4TmuAdp(cm, d, d->UpAdp, TBT_TMU_ADP_CS_6, TBT_TMU_ADP6_DTS, 0);
    }
    if (rc == TBT_OK) {
        rc = tbtU4TmuAdp(cm, pr, a, TBT_TMU_ADP_CS_6, TBT_TMU_ADP6_DTS, 0);
    }
    if (tbtU4TmuTd(cm, d, FALSE) != TBT_OK && rc == TBT_OK) {
        rc = TBT_E_STATE;
    }
    if (rc != TBT_OK) {
        cm->Counters.TmuFailures++;
        tbtU4Log(cm, "u4.kid.tmu.failed", (k->Index << 8) | ((ULONG)rc & 0xFFUL));
        if (tbtU4KidLive(cm, p, k, gen)) {
            k->TmuConfigured = TRUE;    /* whatever was written is undone */
            (void)tbtU4KidTmuOff(cm, p, k, TRUE);
        }
        return;
    }
    cm->Counters.TmuEnabled++;
    tbtU4Log(cm, "u4.kid.tmu.enhanced", (p->Index << 8) | k->Index);
    tbtU4WaitMin(cm, TBT_U4_TMU_SETTLE_US);
}

/*
 * Section 6.4 step 6: ROUTER_CS_5 by record 22 section 6.6 step 1's
 * predicates through the link view - PTO where the parent's port has a
 * PCIe Down adapter and the child a PCIe Up; UTO where both USB3 adapters
 * exist on a USB4 link (Guide 3.2.3; never behind a Thunderbolt 3
 * parent); with no USB3 tunnel at depth 2, both USB3 adapters
 * disconnected (Path Enable 0, Valid 1, Guide 5.5.3); the TMU on a link
 * of two version 2 routers (tbtU4KidTmu), none on any other; then CV and
 * Configuration Ready within 500 ms.
 */
static LONG tbtU4KidConfigValid(PTBT_U4CM cm, PTBT_U4_PORT p, PTBT_U4_KID k, ULONG gen)
{
    PTBT_U4_ROUTER pr;
    PTBT_U4_ROUTER d;
    ULONG set;
    ULONG v;
    ULONG up3;
    LONG rc;
    TBT_WAIT w;

    pr = &p->Dev;
    d = &k->Dev;
    set = 0;
    up3 = tbtU4FirstOf(d, TBT_ADP_TYPE_USB3_UP);
    if (k->ParentPcieDown != 0 && tbtU4FirstOf(d, TBT_ADP_TYPE_PCIE_UP) != 0) {
        set |= TBT_RTR5_PTO;
    }
    if (!pr->Tbt3 && k->ParentUsb3Down != 0 && up3 != 0 &&
        pr->Adp[k->ParentLane0].CapPort != 0 &&
        tbtU4Rd(cm, &pr->Route, TBT_CS_ADAPTER, k->ParentLane0,
                pr->Adp[k->ParentLane0].CapPort + TBT_PORT_CS_18, &v) == TBT_OK &&
        (v & TBT_PORT18_TCM) == 0) {
        set |= TBT_RTR5_UTO;
    }
    rc = tbtU4Rmw(cm, &d->Route, TBT_CS_ROUTER, 0, TBT_ROUTER_CS_5, TBT_RTR5_HCO, set);
    if (rc != TBT_OK) {
        return tbtU4Gone(cm, rc) ? TBT_E_GONE : rc;
    }
    if (k->ParentUsb3Down != 0 && pr->Adp[k->ParentUsb3Down].CapAdp != 0) {
        (void)tbtU4Rmw(cm, &pr->Route, TBT_CS_ADAPTER, k->ParentUsb3Down,
                       pr->Adp[k->ParentUsb3Down].CapAdp + TBT_USB3_CS_0,
                       TBT_USB3_0_PE, TBT_USB3_0_V);
    }
    if (up3 != 0 && d->Adp[up3].CapAdp != 0) {
        (void)tbtU4Rmw(cm, &d->Route, TBT_CS_ADAPTER, up3, d->Adp[up3].CapAdp + TBT_USB3_CS_0,
                       TBT_USB3_0_PE, TBT_USB3_0_V);
    }
    tbtU4KidTmu(cm, p, k, gen);
    if (!tbtU4KidLive(cm, p, k, gen)) {
        return TBT_E_GONE;
    }
    rc = tbtU4Rmw(cm, &d->Route, TBT_CS_ROUTER, 0, TBT_ROUTER_CS_5, 0, TBT_RTR5_CV);
    if (rc != TBT_OK) {
        return tbtU4Gone(cm, rc) ? TBT_E_GONE : rc;
    }
    TbtWaitStart(&w, cm->Host, TBT_U4_CONFIG_READY_US, FALSE);
    for (;;) {
        rc = tbtU4Rd(cm, &d->Route, TBT_CS_ROUTER, 0, TBT_ROUTER_CS_6, &v);
        if (rc != TBT_OK) {
            return tbtU4Gone(cm, rc) ? TBT_E_GONE : rc;
        }
        if ((v & TBT_RTR6_CR) != 0) {
            return TBT_OK;
        }
        if (!tbtU4KidLive(cm, p, k, gen)) {
            return TBT_E_GONE;
        }
        if (TbtWaitDone(&w, cm->Host)) {
            cm->Counters.ConfigReadyTimeouts++;
            k->Why = TBT_U4_WHY_CONFIG_READY;
            return TBT_E_TIMEOUT;
        }
        tbtU4Pause(cm, &w);
    }
}

/*
 * The kid's chained tunnel down with both ends reachable (record 22
 * section 9.3 step 1 through the link view): Path Enable cleared on the
 * parent's PCIe Down, then on the child's PCIe Up; then every entry
 * written or found invalidated and drained, what does not complete
 * quarantined under the kid. TRUE when every step was proven.
 */
static BOOLEAN tbtU4KidTunnelDown(PTBT_U4CM cm, PTBT_U4_PORT p, PTBT_U4_KID k)
{
    PTBT_U4_ROUTER pr;
    PTBT_U4_ROUTER d;
    PTBT_U4_TUNNEL t;
    ULONG hp;
    ULONG dp;
    BOOLEAN ok;

    pr = &p->Dev;
    d = &k->Dev;
    t = &k->Pcie;
    hp = k->ParentPcieDown;
    dp = tbtU4FirstOf(d, TBT_ADP_TYPE_PCIE_UP);
    ok = TRUE;
    if ((t->Written & TBT_U4_E_HOST_ENABLE) != 0) {
        ok &= tbtU4Disable(cm, p->Index, &pr->Route, hp, pr->Adp[hp].CapAdp, FALSE);
    }
    if ((t->Written & TBT_U4_E_DEV_ENABLE) != 0 && d->Present && dp != 0) {
        ok &= tbtU4Disable(cm, p->Index, &d->Route, dp, d->Adp[dp].CapAdp, FALSE);
    }
    /* Extended Encapsulation, whoever set it (record 22 section 6.2.2
     * item 7, as tbtU4TunnelDown): a failure is logged and the teardown
     * goes on. */
    (void)tbtU4EeClear(cm, pr, hp);
    if (d->Present && dp != 0) {
        (void)tbtU4EeClear(cm, d, dp);
    }
    if ((t->Written & TBT_U4_E_HOST_PROTO) != 0) {
        ok &= tbtU4Invalidate(cm, p->Index, &pr->Route, hp, TBT_HOP_PROTOCOL);
    }
    if ((t->Written & TBT_U4_E_HOST_LANE) != 0) {
        ok &= tbtU4Invalidate(cm, p->Index, &pr->Route, k->ParentLane0, t->Hu);
    }
    if (d->Present && (t->Written & TBT_U4_E_DEV_LANE) != 0) {
        ok &= tbtU4Invalidate(cm, p->Index, &d->Route, d->UpAdp, t->Hd);
    }
    if (d->Present && dp != 0 && (t->Written & TBT_U4_E_DEV_PROTO) != 0) {
        ok &= tbtU4Invalidate(cm, p->Index, &d->Route, dp, TBT_HOP_PROTOCOL);
    }
    t->Written = 0;
    t->Up = FALSE;
    t->Preserved = FALSE;
    t->Own = FALSE;
    return ok;
}

/*
 * Section 6.6, Guide 5.3.1 through the link view: the Detect gate (both
 * PCIe adapters in Detect within 500 ms, the USB4 end alone behind a
 * Thunderbolt 3 parent); the four entries, HopIDs `hd` and `hu` the
 * lowest free from 8 on the parent's downstream lane and the child's lane
 * 0, credits by record 22 section 6.8's accounting on the parent's lane
 * (a Thunderbolt 3 parent's preferences the defaults); Path Enable on the
 * child's PCIe Up first, then the parent's PCIe Down; L0 on the parent's
 * PCIe Down within 1 s on a USB4 parent, none waited for behind a
 * Thunderbolt 3 parent [inf]. A failure after any write tears down what
 * was written, the kid FAILED.
 */
static LONG tbtU4KidPcieTunnel(PTBT_U4CM cm, PTBT_U4_PORT p, PTBT_U4_KID k, ULONG gen)
{
    PTBT_U4_ROUTER pr;
    PTBT_U4_ROUTER d;
    PTBT_U4_TUNNEL t;
    TBT_U4_USAGE up;
    TBT_U4_USAGE ud;
    ULONG a;
    ULONG hp;
    ULONG dp;
    ULONG availP;
    ULONG availD;
    ULONG def;
    ULONG prefP;
    ULONG prefD;
    ULONG busy[4];
    ULONG i;
    LONG rc;

    pr = &p->Dev;
    d = &k->Dev;
    t = &k->Pcie;
    a = k->ParentLane0;
    hp = k->ParentPcieDown;
    dp = tbtU4FirstOf(d, TBT_ADP_TYPE_PCIE_UP);
    if (hp == 0 || dp == 0 || pr->Adp[hp].CapAdp == 0 || d->Adp[dp].CapAdp == 0) {
        k->Why = TBT_U4_WHY_NO_ADAPTERS;
        return TBT_E_STATE;
    }
    if (!tbtU4AdapterFree(cm, pr, hp)) {
        k->Why = TBT_U4_WHY_BLOCKED;
        return TBT_E_STATE;
    }
    tbtU4Zero(t, sizeof(*t));
    if ((!pr->Tbt3 &&
         !tbtU4KidWaitPcie(cm, p, k, gen, pr, hp, TBT_LTSSM_DETECT, TBT_U4_DETECT_US)) ||
        !tbtU4KidWaitPcie(cm, p, k, gen, d, dp, TBT_LTSSM_DETECT, TBT_U4_DETECT_US)) {
        if (!tbtU4KidLive(cm, p, k, gen)) {
            return TBT_E_GONE;
        }
        cm->Counters.DetectTimeouts++;
        k->Why = TBT_U4_WHY_DETECT;
        return TBT_E_TIMEOUT;
    }
    availP = 0;
    availD = 0;
    rc = tbtU4ScanLane(cm, pr, a, &up);
    if (rc == TBT_OK) {
        rc = tbtU4ScanLane(cm, d, d->UpAdp, &ud);
    }
    if (rc == TBT_OK) {
        rc = tbtU4LaneAvail(cm, pr, a, &up, &availP);
    }
    if (rc == TBT_OK) {
        rc = tbtU4LaneAvail(cm, d, d->UpAdp, &ud, &availD);
    }
    if (rc != TBT_OK) {
        k->Why = TBT_U4_WHY_PATH;
        return tbtU4Gone(cm, rc) ? TBT_E_GONE : rc;
    }
    for (i = 0; i < 4; i++) {
        busy[i] = up.InUse[i] | up.OutUse[i] | ud.InUse[i] | ud.OutUse[i];
    }
    t->Hd = tbtU4AllocHop(pr->Adp[a].MaxOutHop, d->Adp[d->UpAdp].MaxInHop, busy, busy);
    if (t->Hd != 0) {
        TBT_U4_BIT_SET(busy, t->Hd);
    }
    t->Hu = tbtU4AllocHop(d->Adp[d->UpAdp].MaxOutHop, pr->Adp[a].MaxInHop, busy, busy);
    if (t->Hd == 0 || t->Hu == 0) {
        k->Why = TBT_U4_WHY_HOPID;
        tbtU4Log(cm, "u4.kid.hopid.none", (t->Hd << 8) | t->Hu);
        return TBT_E_STATE;
    }
    def = k->Bonded ? TBT_CREDITS_BONDED : TBT_CREDITS_SINGLE;
    prefP = (!pr->Tbt3 && pr->BaMaxPcie != 0) ? pr->BaMaxPcie : def;
    prefD = d->BaMaxPcie != 0 ? d->BaMaxPcie : def;
    t->CreditsHost = prefP < availP ? prefP : availP;
    t->CreditsDev = prefD < availD ? prefD : availD;
    if (t->CreditsHost > TBT_PATH_CREDITS_MAX) {
        t->CreditsHost = TBT_PATH_CREDITS_MAX;
    }
    if (t->CreditsDev > TBT_PATH_CREDITS_MAX) {
        t->CreditsDev = TBT_PATH_CREDITS_MAX;
    }
    if (t->CreditsHost < TBT_CREDITS_MIN_PCIE || t->CreditsDev < TBT_CREDITS_MIN_PCIE) {
        k->Why = TBT_U4_WHY_CREDITS;
        tbtU4Log(cm, "u4.kid.credits.short", (t->CreditsHost << 16) | t->CreditsDev);
        return TBT_E_STATE;
    }
    /* Record 22 section 10.5 is the host port's own path: nothing of this
     * one touches the port's isolation word. Each entry recorded before
     * its write, downstream then upstream, each source first. */
    t->Written |= TBT_U4_E_HOST_PROTO;
    rc = tbtU4WriteEntry(cm, &pr->Route, hp, TBT_HOP_PROTOCOL, a, t->Hd,
                         pr->Tbt3 ? TBT_TBT3_PROTO_CREDITS : 0, TBT_PATH_WEIGHT_PCIE,
                         FALSE, pr->Tbt3, FALSE, NULL);
    if (rc == TBT_OK) {
        t->Written |= TBT_U4_E_DEV_LANE;
        rc = tbtU4WriteEntry(cm, &d->Route, d->UpAdp, t->Hd, dp, TBT_HOP_PROTOCOL,
                             t->CreditsDev, TBT_PATH_WEIGHT_PCIE, TRUE, FALSE, FALSE, NULL);
    }
    if (rc == TBT_OK && tbtU4KidLive(cm, p, k, gen)) {
        t->Written |= TBT_U4_E_DEV_PROTO;
        rc = tbtU4WriteEntry(cm, &d->Route, dp, TBT_HOP_PROTOCOL, d->UpAdp, t->Hu, 0,
                             TBT_PATH_WEIGHT_PCIE, FALSE, FALSE, FALSE, NULL);
    }
    if (rc == TBT_OK && tbtU4KidLive(cm, p, k, gen)) {
        t->Written |= TBT_U4_E_HOST_LANE;
        rc = tbtU4WriteEntry(cm, &pr->Route, a, t->Hu, hp, TBT_HOP_PROTOCOL,
                             t->CreditsHost, TBT_PATH_WEIGHT_PCIE, TRUE, FALSE, FALSE, NULL);
    }
    if (rc == TBT_OK && tbtU4KidLive(cm, p, k, gen)) {
        /* Record 22 section 6.2.2 item 7 through the link view: between
         * two version 2 routers Extended Encapsulation set and read back
         * on both adapters before either Path Enable; otherwise EE 0, a
         * version 2 end left with it set (a predecessor's, or a clear
         * that failed) cleared first, else no Path Enable. */
        if (cm->Config.Usb4V2 && tbtU4IsV2(pr) && tbtU4IsV2(d)) {
            if (!tbtU4EeSet(cm, d, dp) || !tbtU4EeSet(cm, pr, hp)) {
                k->Why = TBT_U4_WHY_PATH;
                rc = TBT_E_STATE;
            } else {
                cm->Counters.EeSet++;
            }
        } else if (!tbtU4EeClear(cm, pr, hp) || !tbtU4EeClear(cm, d, dp)) {
            tbtU4Log(cm, "u4.kid.ee.left", (k->Index << 8) | hp);
            k->Why = TBT_U4_WHY_PATH;
            rc = TBT_E_STATE;
        }
    }
    if (rc == TBT_OK && tbtU4KidLive(cm, p, k, gen)) {
        t->Written |= TBT_U4_E_DEV_ENABLE;
        rc = tbtU4Rmw(cm, &d->Route, TBT_CS_ADAPTER, dp, d->Adp[dp].CapAdp + TBT_PCIE_CS_0,
                      0, TBT_PCIE0_PE);
    }
    if (rc == TBT_OK && tbtU4KidLive(cm, p, k, gen)) {
        t->Written |= TBT_U4_E_HOST_ENABLE;
        rc = tbtU4Rmw(cm, &pr->Route, TBT_CS_ADAPTER, hp, pr->Adp[hp].CapAdp + TBT_PCIE_CS_0,
                      0, TBT_PCIE0_PE);
    }
    if (rc == TBT_OK && !pr->Tbt3 &&
        !tbtU4KidWaitPcie(cm, p, k, gen, pr, hp, TBT_LTSSM_L0, TBT_U4_L0_US)) {
        k->Why = TBT_U4_WHY_L0;
        rc = TBT_E_TIMEOUT;
    }
    if (!tbtU4KidLive(cm, p, k, gen) || tbtU4Gone(cm, rc)) {
        return TBT_E_GONE;
    }
    if (rc != TBT_OK) {
        if (k->Why == TBT_U4_WHY_NONE) {
            k->Why = TBT_U4_WHY_PATH;
        }
        cm->Counters.PcieFailed++;
        tbtU4Log(cm, "u4.kid.pcie.failed", (k->Index << 8) | k->Why);
        (void)tbtU4KidTunnelDown(cm, p, k);
        return rc;
    }
    t->Up = TRUE;
    cm->Counters.KidsUp++;
    tbtU4Log(cm, "u4.kid.pcie.up", (k->Index << 16) | (t->Hd << 8) | t->Hu);
    tbtU4QueueKid(cm, TBT_U4_ACT_TUNNEL_UP, p, k);
    return TBT_OK;
}

/* A kid slot back to free: the links discovered below the child dropped
 * unwritten and every route of it and below forgotten by the transport
 * (section 6.7, as tbtU4DropLinks at depth 1), the quarantine records on
 * the child's routers dropped with them (those on the parent stay with
 * the parent), its resume obligation gone. */
static VOID tbtU4KidFree(PTBT_U4CM cm, PTBT_U4_KID k)
{
    ULONG i;
    ULONG port;
    PTBT_U4_PORT p;

    if (k->Dev.Depth == 2) {
        for (i = 0; i < TBT_U4_MAX_LINKS; i++) {
            if (cm->Links[i].Used && tbtU4Under(&cm->Links[i].Child, &k->Dev.Route)) {
                TbtCtlForget(&cm->Ctl, &cm->Links[i].Child);
                cm->Links[i].Used = FALSE;
            }
        }
        for (i = 0; i < TBT_U4_QUARANTINE; i++) {
            if (cm->Quarantine[i].Used &&
                tbtU4Under(&cm->Quarantine[i].Route, &k->Dev.Route)) {
                cm->Quarantine[i].Used = FALSE;
            }
        }
        TbtCtlForget(&cm->Ctl, &k->Dev.Route);
    }
    for (i = 0; i < TBT_U4_OBLIGATIONS; i++) {
        if (cm->Obligations[i].Used && cm->Obligations[i].Kid == k->Index) {
            cm->Obligations[i].Used = FALSE;
        }
    }
    port = k->Port;
    i = k->Index;
    tbtU4Zero(k, sizeof(*k));
    k->Index = i;
    p = tbtU4PortOf(cm, port);
    if (p != NULL) {
        p->CheckPending = TRUE;     /* the parent's port scanned again */
    }
}

/* Section 6.8 step 1: every kid of a host port whose router left, leaf
 * first, with no register work; a sequence of one in flight stops at its
 * next check (its slot is free, so the liveness rule fails). */
static VOID tbtU4KidsDrop(PTBT_U4CM cm, PTBT_U4_PORT p)
{
    ULONG i;
    PTBT_U4_KID k;

    for (i = 0; i < TBT_U4_MAX_KIDS; i++) {
        k = &cm->Kids[i];
        if (!k->Used || k->Port != p->Index) {
            continue;
        }
        cm->Counters.KidsDropped++;
        tbtU4Log(cm, "u4.kid.dropped", (p->Index << 8) | k->Index);
        tbtU4KidFree(cm, k);
    }
}

/* The parent's downstream lane adapters back to what this driver found
 * (section 6.7 step 5): USB4 Port is Configured (LC_SX_CTRL on a
 * Thunderbolt 3 parent), Target Link Width and Lane Bonding, lane 1's
 * Lane Disable - each change kept owned until a read-back proves it
 * undone, as tbtU4HostRestore keeps the host's, and retried; the child's
 * lanes put back too while it is there, at best. TRUE when nothing is
 * left owed on the parent. */
static BOOLEAN tbtU4KidLanesBack(PTBT_U4CM cm, PTBT_U4_PORT p, PTBT_U4_KID k)
{
    PTBT_U4_ROUTER pr;
    PTBT_U4_ROUTER d;
    ULONG a;
    ULONG i;
    ULONG v;
    ULONG mark;
    ULONG region;
    BOOLEAN ok;

    pr = &p->Dev;
    d = &k->Dev;
    a = k->ParentLane0;
    if (k->PcSetParent) {
        if (pr->Tbt3) {
            mark = (a & 1UL) != 0 ? TBT_LC_SX_CTRL_L1C : TBT_LC_SX_CTRL_L2C;
            if (tbtU4RouterLcPort(cm, pr, a, &region) != TBT_OK) {
                /* A link controller whose descriptor did not read: the
                 * mark is neither proven undone nor forgotten. */
            } else if (region == 0 ||
                (tbtU4Rmw(cm, &pr->Route, TBT_CS_ROUTER, 0, region + TBT_LC_SX_CTRL, mark, 0) ==
                     TBT_OK &&
                 tbtU4Rd(cm, &pr->Route, TBT_CS_ROUTER, 0, region + TBT_LC_SX_CTRL, &v) ==
                     TBT_OK && (v & mark) == 0)) {
                k->PcSetParent = FALSE;
            }
        } else if (pr->Adp[a].CapPort == 0 ||
                   (tbtU4Rmw(cm, &pr->Route, TBT_CS_ADAPTER, a,
                             pr->Adp[a].CapPort + TBT_PORT_CS_19, TBT_PORT19_PC, 0) == TBT_OK &&
                    tbtU4Rd(cm, &pr->Route, TBT_CS_ADAPTER, a,
                            pr->Adp[a].CapPort + TBT_PORT_CS_19, &v) == TBT_OK &&
                    (v & TBT_PORT19_PC) == 0)) {
            k->PcSetParent = FALSE;
        }
    }
    if (k->LaneWritten) {
        ok = TRUE;
        for (i = 0; i < 4; i++) {
            if (i >= 2) {
                if (d->Present) {
                    (void)tbtU4LaneRmw(cm, d, d->UpAdp + (i - 2),
                                       TBT_LANE1_TLW_MASK | TBT_LANE1_LB,
                                       k->LaneSaved[i] & (TBT_LANE1_TLW_MASK | TBT_LANE1_LB));
                }
                continue;
            }
            if (tbtU4LaneRmw(cm, pr, a + i, TBT_LANE1_TLW_MASK | TBT_LANE1_LB,
                             k->LaneSaved[i] & (TBT_LANE1_TLW_MASK | TBT_LANE1_LB)) != TBT_OK ||
                tbtU4LaneRd(cm, pr, a + i, &v) != TBT_OK ||
                (v & TBT_LANE1_TLW_MASK) != (k->LaneSaved[i] & TBT_LANE1_TLW_MASK)) {
                ok = FALSE;
            }
        }
        if (ok) {
            k->LaneWritten = FALSE;
        }
    }
    if (k->Lane1Disabled &&
        tbtU4LaneRmw(cm, pr, a + 1, TBT_LANE1_LD, 0) == TBT_OK &&
        tbtU4LaneRd(cm, pr, a + 1, &v) == TBT_OK && (v & TBT_LANE1_LD) == 0) {
        k->Lane1Disabled = FALSE;
    }
    return (BOOLEAN)(!k->PcSetParent && !k->LaneWritten && !k->Lane1Disabled);
}

/*
 * Section 6.7: the child gone with its parent still there. Step 1, the
 * parent's side of the chained tunnel (Path Enable read and cleared;
 * Valid cleared on the parent's PCIe Down HopID 8 entry and its lane's
 * Hu entry, each drained; the PCIe Down's LTSSM in Detect on a USB4
 * parent); steps 2 and 3, the upcall with the kid; then the residual
 * sweep of every other entry entering or leaving the link's lanes
 * (tbtU4SweepLane), and once the link is empty step 5, the parent's lanes
 * back (tbtU4KidLanesBack); step 6, the chain port's removal
 * (TUNNEL_DOWN), at the first pass. An entry unread, or a lane change not
 * proven undone, keeps the kid in TEARDOWN (SweepOwed), retried at the
 * next tick; the slot is freed once nothing is owed. The host port's
 * sequence; no teardown nests in it.
 */
static VOID tbtU4KidTeardown(PTBT_U4CM cm, PTBT_U4_PORT p, PTBT_U4_KID k)
{
    PTBT_U4_ROUTER pr;
    PTBT_U4_TUNNEL t;
    ULONG prevPort;
    ULONG prevKid;
    BOOLEAN prevTd;
    BOOLEAN closed;
    BOOLEAN complete;
    BOOLEAN first;
    ULONG hp;

    pr = &p->Dev;
    t = &k->Pcie;
    hp = k->ParentPcieDown;
    prevPort = cm->ActivePort;
    prevKid = cm->KidCtx;
    prevTd = cm->InTeardown;
    cm->ActivePort = p->Index;
    cm->KidCtx = k->Index;
    cm->InTeardown = TRUE;
    if (!k->SweepOwed) {
        k->State = TBT_U4_TEARDOWN;
        cm->Counters.KidsTornDown++;
        tbtU4Log(cm, "u4.kid.teardown", (p->Index << 8) | k->Index);
        if ((t->Up || t->Written != 0) && hp != 0) {
            closed = tbtU4Disable(cm, p->Index, &pr->Route, hp, pr->Adp[hp].CapAdp, FALSE);
            (void)tbtU4EeClear(cm, pr, hp);
            if ((t->Written & TBT_U4_E_HOST_PROTO) != 0) {
                closed &= tbtU4Invalidate(cm, p->Index, &pr->Route, hp, TBT_HOP_PROTOCOL);
            }
            if ((t->Written & TBT_U4_E_HOST_LANE) != 0) {
                closed &= tbtU4Invalidate(cm, p->Index, &pr->Route, k->ParentLane0, t->Hu);
            }
            if (closed && !pr->Tbt3) {
                closed = tbtU4WaitDetect(cm, NULL, 0, pr, hp, TBT_U4_DETECT_US);
            }
            tbtU4Zero(t, sizeof(*t));
            tbtU4KidUpcall(cm, p, k, closed);
            k->RemovalOwed = TRUE;
        }
        tbtU4Zero(t, sizeof(*t));
        /* The link's TMU, the parent's side (the child is gone or being
         * let go: its side goes with its next connection); a failure is
         * retried with the sweep below. */
        (void)tbtU4KidTmuOff(cm, p, k, FALSE);
        k->Dev.Present = FALSE;
        /* What lived below the child went with it. */
        tbtU4Zero(&k->Dev.Adp, sizeof(k->Dev.Adp));
    }
    first = (BOOLEAN)!k->SweepOwed;
    complete = tbtU4SweepLane(cm, p->Index, pr, k->ParentLane0);
    /* The lanes only once nothing is left on the link (Guide 7.1): a held
     * entry is not closed, so the lanes, read again with the quarantine,
     * and the PCIe Down (its Path Enable and HopID 8) must read idle. */
    if (complete) {
        complete = (BOOLEAN)(tbtU4KidLanesIdle(cm, pr, k->ParentLane0) &&
                             (hp == 0 || tbtU4AdapterFree(cm, pr, hp)));
    }
    if (complete) {
        complete = tbtU4KidTmuOff(cm, p, k, FALSE);
    }
    if (complete) {
        complete = tbtU4KidLanesBack(cm, p, k);
    }
    if (first && k->RemovalOwed) {
        /* Step 6 once, whatever is still owed: the operating system's
         * removal does not wait on the parent's cleanup. */
        k->RemovalOwed = FALSE;
        tbtU4QueueKid(cm, TBT_U4_ACT_TUNNEL_DOWN, p, k);
    }
    if (!complete) {
        /* The slot stays TEARDOWN, so nothing connects on the link until
         * every entry is closed and every lane change undone. */
        k->SweepOwed = TRUE;
        tbtU4Log(cm, "u4.kid.sweep-owed", (p->Index << 8) | k->Index);
    } else {
        k->SweepOwed = FALSE;
        tbtU4KidFree(cm, k);
    }
    cm->InTeardown = prevTd;
    cm->KidCtx = prevKid;
    cm->ActivePort = prevPort;
}

/*
 * Both routers there (an eject, or a PCIe tunnel lost at either level):
 * the kid's chained tunnel torn down in the orderly form, both ends
 * (section 6.3's first case, section 6.7's eject), the chain port's
 * removal queued; for a loss, the upcall with the PCIe Down read back in
 * Detect on a USB4 parent. The kid FAILED for PCIe until a re-plug of
 * the child, `why` its reason. TRUE when every step was proven.
 */
static BOOLEAN tbtU4KidDown(PTBT_U4CM cm, PTBT_U4_PORT p, PTBT_U4_KID k, ULONG why)
{
    ULONG prevPort;
    ULONG prevKid;
    ULONG u[2];
    BOOLEAN closed;
    BOOLEAN had;
    BOOLEAN present;

    prevPort = cm->ActivePort;
    prevKid = cm->KidCtx;
    cm->ActivePort = p->Index;
    cm->KidCtx = k->Index;
    closed = TRUE;
    had = (BOOLEAN)(k->Pcie.Up || k->Pcie.Written != 0);
    if (had) {
        k->Pcie.Written |= TBT_U4_E_HOST_ENABLE | TBT_U4_E_DEV_ENABLE;
        /* The child's side only on the child this kid is: read as the
         * same router now (its UUID), since this can run before the
         * kid's own check (a parent's PCIe loss or eject) and behind a
         * Thunderbolt 3 parent no Lock tells a replacement. Unconfirmed,
         * the parent's side alone; the kid's check settles the rest. */
        present = k->Dev.Present;
        if (present && (!k->Dev.UuidValid ||
                        tbtU4RdN(cm, &k->Dev.Route, TBT_CS_ROUTER, 0, TBT_ROUTER_CS_7, 2, u) !=
                            TBT_OK ||
                        u[0] != k->Dev.Uuid[0] || u[1] != k->Dev.Uuid[1])) {
            tbtU4Log(cm, "u4.kid.down.unverified", k->Index);
            k->Dev.Present = FALSE;
        }
        closed = tbtU4KidTunnelDown(cm, p, k);
        k->Dev.Present = present;
        cm->Counters.KidsTornDown++;
        if (why == TBT_U4_WHY_LOST) {
            if (closed && !p->Dev.Tbt3 && k->ParentPcieDown != 0) {
                closed = tbtU4WaitDetect(cm, NULL, 0, &p->Dev, k->ParentPcieDown,
                                         TBT_U4_DETECT_US);
            }
            tbtU4KidUpcall(cm, p, k, closed);
        }
        tbtU4QueueKid(cm, TBT_U4_ACT_TUNNEL_DOWN, p, k);
    }
    if (why == TBT_U4_WHY_EJECTED) {
        k->Ejected = TRUE;
    }
    k->PcieFailed = TRUE;
    k->Transitional = FALSE;
    k->State = TBT_U4_FAILED;
    k->Why = why;
    tbtU4Log(cm, "u4.kid.down", (k->Index << 16) | (why << 8) | (closed ? 1UL : 0UL));
    cm->KidCtx = prevKid;
    cm->ActivePort = prevPort;
    return closed;
}

/* Every kid of `p` down in the orderly form, before the parent's own
 * tunnel goes (its PCIe loss, its eject). TRUE when every one closed. */
static BOOLEAN tbtU4KidsDown(PTBT_U4CM cm, PTBT_U4_PORT p, ULONG why)
{
    ULONG i;
    PTBT_U4_KID k;
    BOOLEAN closed;

    closed = TRUE;
    for (i = 0; i < TBT_U4_MAX_KIDS; i++) {
        k = &cm->Kids[i];
        if (!k->Used || k->Port != p->Index || !k->Dev.Present || k->SweepOwed ||
            k->State == TBT_U4_TEARDOWN) {
            continue;
        }
        closed &= tbtU4KidDown(cm, p, k, why);
    }
    return closed;
}

/*
 * Section 6.5's idle reading of an UP kid's PCIe tunnel, record 22
 * section 7.1 item 4 (Path Enable clear, the LTSSM in Detect, or
 * transitional for 2 s): behind a USB4 parent the parent's PCIe Down;
 * behind a Thunderbolt 3 parent, whose LTSSM field is never interpreted
 * [emu: usb4-router-tb3.c reads 0xc6210400 with the tunnel up], the
 * child's PCIe Up, plus the parent's PCIe Down Path Enable every fourth
 * tick. A loss tears the kid's tunnel down with both ends.
 */
static VOID tbtU4KidPcieLoss(PTBT_U4CM cm, PTBT_U4_PORT p, PTBT_U4_KID k)
{
    const TBT_U4_ROUTER *rt;
    ULONG adp;
    ULONG v;
    ULONG s;
    BOOLEAN lost;

    if (!k->Pcie.Up || k->ParentPcieDown == 0) {
        return;
    }
    k->Ticks++;
    if (p->Dev.Tbt3) {
        rt = &k->Dev;
        adp = tbtU4FirstOf(&k->Dev, TBT_ADP_TYPE_PCIE_UP);
    } else {
        rt = &p->Dev;
        adp = k->ParentPcieDown;
    }
    if (adp == 0 ||
        tbtU4Rd(cm, &rt->Route, TBT_CS_ADAPTER, adp, rt->Adp[adp].CapAdp + TBT_PCIE_CS_0,
                &v) != TBT_OK) {
        return;
    }
    s = TBT_PCIE0_LTSSM(v);
    lost = FALSE;
    if ((v & TBT_PCIE0_PE) == 0 || s == TBT_LTSSM_DETECT) {
        lost = TRUE;
    } else if (s == TBT_LTSSM_L0 || s == TBT_LTSSM_L1 || s == TBT_LTSSM_RECOVERY ||
               s == TBT_LTSSM_RECOVERY_IDLE) {
        k->Transitional = FALSE;
    } else if (!k->Transitional) {
        k->Transitional = TRUE;
        k->TransitionalSinceUs = tbtU4Now(cm);
    } else if (tbtU4Now(cm) - k->TransitionalSinceUs >= TBT_U4_TRANSITIONAL_US) {
        lost = TRUE;
    }
    if (!lost && p->Dev.Tbt3 && (k->Ticks % 4UL) == 0 &&
        tbtU4Rd(cm, &p->Dev.Route, TBT_CS_ADAPTER, k->ParentPcieDown,
                p->Dev.Adp[k->ParentPcieDown].CapAdp + TBT_PCIE_CS_0, &v) == TBT_OK &&
        (v & TBT_PCIE0_PE) == 0) {
        lost = TRUE;
    }
    if (!lost) {
        return;
    }
    cm->Counters.KidsLost++;
    cm->Counters.PcieLost++;
    tbtU4Log(cm, "u4.kid.lost", (k->Index << 8) | s);
    (void)tbtU4KidDown(cm, p, k, TBT_U4_WHY_LOST);
}

/*
 * The chained tunnel read whole, for adoption and the resume's
 * verification (section 6.9): the parent's PCIe Down HopID 8 entry Valid
 * to its downstream lane at `hd`; the child's lane 0 entry `hd` Valid to
 * its PCIe Up HopID 8; the child's PCIe Up HopID 8 Valid to its lane at
 * `hu`; the parent's lane entry `hu` Valid to the PCIe Down HopID 8;
 * Path Enable at both ends; a live LTSSM at the USB4 ends (behind a
 * Thunderbolt 3 parent the child's alone). Reads only; anything
 * unreadable is not verified. `hd` and `hu` out.
 */
static BOOLEAN tbtU4KidChained(PTBT_U4CM cm, const TBT_U4_ROUTER *pr, ULONG a, ULONG hp,
                               const TBT_U4_ROUTER *d, PULONG hd, PULONG hu)
{
    ULONG dp;
    ULONG e[2];
    ULONG v;
    ULONG s;
    ULONG k;
    const TBT_U4_ROUTER *rt;
    ULONG adp;

    dp = tbtU4FirstOf(d, TBT_ADP_TYPE_PCIE_UP);
    if (hp == 0 || dp == 0 || pr->Adp[hp].CapAdp == 0 || d->Adp[dp].CapAdp == 0) {
        return FALSE;
    }
    if (tbtU4Entry(cm, &pr->Route, hp, TBT_HOP_PROTOCOL, e) != TBT_OK ||
        (e[0] & TBT_PATH0_VALID) == 0 || TBT_PATH0_OUT_ADP(e[0]) != a) {
        return FALSE;
    }
    *hd = TBT_PATH0_OUT_HOP(e[0]);
    if (*hd < TBT_HOP_FIRST ||
        tbtU4Entry(cm, &d->Route, d->UpAdp, *hd, e) != TBT_OK ||
        !tbtU4EntryGoes(e, dp, TBT_HOP_PROTOCOL) ||
        tbtU4Entry(cm, &d->Route, dp, TBT_HOP_PROTOCOL, e) != TBT_OK ||
        (e[0] & TBT_PATH0_VALID) == 0 || TBT_PATH0_OUT_ADP(e[0]) != d->UpAdp) {
        return FALSE;
    }
    *hu = TBT_PATH0_OUT_HOP(e[0]);
    if (*hu < TBT_HOP_FIRST ||
        tbtU4Entry(cm, &pr->Route, a, *hu, e) != TBT_OK ||
        !tbtU4EntryGoes(e, hp, TBT_HOP_PROTOCOL)) {
        return FALSE;
    }
    for (k = 0; k < 2; k++) {
        rt = k == 0 ? pr : d;
        adp = k == 0 ? hp : dp;
        if (tbtU4Rd(cm, &rt->Route, TBT_CS_ADAPTER, adp, rt->Adp[adp].CapAdp + TBT_PCIE_CS_0,
                    &v) != TBT_OK || (v & TBT_PCIE0_PE) == 0) {
            return FALSE;
        }
        if (k == 0 && pr->Tbt3) {
            continue;
        }
        s = TBT_PCIE0_LTSSM(v);
        if (s != TBT_LTSSM_L0 && s != TBT_LTSSM_L1 && s != TBT_LTSSM_RECOVERY &&
            s != TBT_LTSSM_RECOVERY_IDLE) {
            return FALSE;
        }
    }
    return TRUE;
}

/*
 * Whether a router's link lanes (`lane0` and the one above) carry
 * nothing but the entries named: no entry Valid or with Pending Packets
 * that enters them or leaves them (the set tbtU4SweepLane closes) other
 * than ingress `in1`:`in1Hop` and `in2`:`in2Hop` (0 for none). Reads only.
 */
static BOOLEAN tbtU4LinkOnly(PTBT_U4CM cm, const TBT_U4_ROUTER *rt, ULONG lane0,
                             ULONG in1, ULONG in1Hop, ULONG in2, ULONG in2Hop)
{
    ULONG a;
    ULONG hop;
    ULONG e[2];
    BOOLEAN into;
    BOOLEAN from;

    for (a = 1; a <= rt->MaxAdp && a < TBT_U4_MAX_ADP; a++) {
        if (!rt->Adp[a].Present) {
            continue;
        }
        from = (BOOLEAN)(a == lane0 || a == lane0 + 1);
        for (hop = TBT_HOP_FIRST; hop <= rt->Adp[a].MaxInHop; hop++) {
            if (tbtU4Entry(cm, &rt->Route, a, hop, e) != TBT_OK) {
                return FALSE;
            }
            if ((e[0] & TBT_PATH0_VALID) == 0 && (e[1] & TBT_PATH1_PP) == 0) {
                continue;
            }
            into = (BOOLEAN)(TBT_PATH0_OUT_ADP(e[0]) == lane0 ||
                             TBT_PATH0_OUT_ADP(e[0]) == lane0 + 1);
            if (!into && !from) {
                continue;
            }
            if ((a == in1 && hop == in1Hop) || (a == in2 && hop == in2Hop)) {
                continue;
            }
            return FALSE;
        }
    }
    return TRUE;
}

/* Every protocol adapter of `rt` other than `skip` carrying nothing
 * (tbtU4ProtoEmpty's test). */
static BOOLEAN tbtU4ProtosEmpty(PTBT_U4CM cm, const TBT_U4_ROUTER *rt, ULONG skip)
{
    ULONG a;

    for (a = 1; a <= rt->MaxAdp && a < TBT_U4_MAX_ADP; a++) {
        if (a == skip || !rt->Adp[a].Present || rt->Adp[a].CapAdp == 0) {
            continue;
        }
        if (rt->Adp[a].Type != TBT_ADP_TYPE_PCIE_UP && rt->Adp[a].Type != TBT_ADP_TYPE_PCIE_DOWN &&
            rt->Adp[a].Type != TBT_ADP_TYPE_USB3_UP && rt->Adp[a].Type != TBT_ADP_TYPE_USB3_DOWN) {
            continue;
        }
        if (!tbtU4ProtoEmpty(cm, rt, a)) {
            return FALSE;
        }
    }
    return TRUE;
}

/* Section 6.9 with section 9.4: an adopted kid's tunnel matched to the
 * earlier instance's record on the same NHI. */
static BOOLEAN tbtU4KidCarryMatch(PTBT_U4CM cm, const TBT_U4_PORT *p, const TBT_U4_KID *k)
{
    const TBT_U4_CARRY_KID *c;
    ULONG i;

    if (!cm->Carried.Valid || cm->Carried.HostVendor != cm->HostRouter.Vendor ||
        cm->Carried.HostDevice != cm->HostRouter.Device) {
        return FALSE;
    }
    for (i = 0; i < TBT_U4_MAX_KIDS; i++) {
        c = &cm->Carried.Kid[i];
        if (c->Used && c->HostLane0 == p->Lane0 && c->ParentLane0 == k->ParentLane0 &&
            c->Vendor == k->Dev.Vendor && c->Device == k->Dev.Device &&
            c->Uuid[0] == k->Dev.Uuid[0] && c->Uuid[1] == k->Dev.Uuid[1] &&
            c->Hd == k->Pcie.Hd && c->Hu == k->Pcie.Hu) {
            return TRUE;
        }
    }
    return FALSE;
}

/* Adoption's PCIe Down (section 6.9): on a USB4 parent the paired one;
 * on a Thunderbolt 3 parent, whose pairing is by use, the first PCIe
 * Down whose HopID 8 entry reads Valid into the link, else the first not
 * taken by another kid. Reads only. */
static ULONG tbtU4KidAdoptPcie(PTBT_U4CM cm, const TBT_U4_PORT *p, ULONG a)
{
    const TBT_U4_ROUTER *rt;
    ULONG e[2];
    ULONG i;

    rt = &p->Dev;
    if (rt->Tbt3) {
        for (i = 1; i <= rt->MaxAdp && i < TBT_U4_MAX_ADP; i++) {
            if (rt->Adp[i].Present && rt->Adp[i].Type == TBT_ADP_TYPE_PCIE_DOWN &&
                rt->Adp[i].CapAdp != 0 &&
                tbtU4Entry(cm, &rt->Route, i, TBT_HOP_PROTOCOL, e) == TBT_OK &&
                (e[0] & TBT_PATH0_VALID) != 0 && TBT_PATH0_OUT_ADP(e[0]) == a) {
                return i;
            }
        }
    }
    return tbtU4KidPair(cm, p, a, TBT_ADP_TYPE_PCIE_DOWN, FALSE);
}

/* A free kid slot, or NULL; the full table logged once a port. */
static PTBT_U4_KID tbtU4KidSlot(PTBT_U4CM cm, PTBT_U4_PORT p, ULONG a)
{
    ULONG i;

    for (i = 0; i < TBT_U4_MAX_KIDS; i++) {
        if (!cm->Kids[i].Used) {
            tbtU4Zero(&cm->Kids[i], sizeof(cm->Kids[i]));
            cm->Kids[i].Index = i + 1;
            return &cm->Kids[i];
        }
    }
    cm->Counters.KidsFull++;
    tbtU4Log(cm, "u4.kid.full", (p->Index << 8) | a);
    return NULL;
}

/*
 * Section 6.9, from link to kid: a depth-2 link section 9.2's walk
 * recorded under an UP parent with its PCIe tunnel, read once. Adopted -
 * a kid UP, Preserved, nothing written - when the child verifies
 * (TopologyID Valid at depth 2, Router Ready, PTO, CV, Configuration
 * Ready, the same UUID), the link is bonded or its lane 1 disconnected or
 * disabled, the chained tunnel reads whole (tbtU4KidChained), and the link
 * carries nothing else (tbtU4LinkOnly on both routers, the child's other
 * protocol adapters empty). With no tunnel at all and nothing else on it,
 * the link is freed for a fresh connect (its router taken as enumerated).
 * Anything else stays a watched link. A USB4 child only.
 */
static VOID tbtU4KidAdopt(PTBT_U4CM cm, PTBT_U4_PORT p, ULONG li)
{
    PTBT_U4_LINK l;
    PTBT_U4_ROUTER pr;
    PTBT_U4_KID k;
    PTBT_U4_ROUTER d;
    ULONG v[9];
    ULONG a;
    ULONG hp;
    ULONG dp;
    ULONG hd;
    ULONG hu;
    ULONG lane0;
    ULONG lane1;
    BOOLEAN bonded;
    BOOLEAN gen4;
    BOOLEAN asym;

    l = &cm->Links[li];
    pr = &p->Dev;
    a = l->ParentLane0;
    l->AdoptTried = TRUE;
    if (l->ChildTbt3 || TbtRouteDepth(&l->Child) != 2 || l->RemovalPending || l->KidDown) {
        return;
    }
    k = tbtU4KidSlot(cm, p, a);
    if (k == NULL) {
        return;
    }
    d = &k->Dev;
    d->Route = l->Child;
    d->Depth = 2;
    d->DromUnused[0] = l->ChildDromUnused[0];
    d->DromUnused[1] = l->ChildDromUnused[1];
    if (tbtU4RdN(cm, &d->Route, TBT_CS_ROUTER, 0, TBT_ROUTER_CS_0, 9, v) != TBT_OK) {
        goto watched;
    }
    tbtU4TakeHeader(d, v);
    d->Depth = 2;
    if (d->Tbt3 || !tbtU4Enumerated(d, v) ||
        (l->ChildUuidValid && (v[7] != l->ChildUuid[0] || v[8] != l->ChildUuid[1])) ||
        tbtU4DiscoverAdapters(cm, d) != TBT_OK ||
        tbtU4RouterCaps(cm, d, TBT_RTR1_NEXT_CAP(v[1])) != TBT_OK ||
        d->UpAdp == 0 || d->UpAdp + 1 >= TBT_U4_MAX_ADP) {
        goto watched;
    }
    hp = tbtU4KidAdoptPcie(cm, p, a);
    dp = tbtU4FirstOf(d, TBT_ADP_TYPE_PCIE_UP);
    if (tbtU4LinkOnly(cm, pr, a, 0, 0, 0, 0) &&
        tbtU4LinkOnly(cm, d, d->UpAdp, 0, 0, 0, 0) && tbtU4ProtosEmpty(cm, d, 0) &&
        (hp == 0 || tbtU4ProtoEmpty(cm, pr, hp))) {
        /* No tunnel and nothing else: connected as a fresh plug. */
        tbtU4Log(cm, "u4.kid.fresh", (p->Index << 8) | a);
        l->Used = FALSE;
        tbtU4Zero(k, sizeof(*k));
        return;
    }
    if ((v[6] & TBT_RTR6_RR) == 0 || (v[6] & TBT_RTR6_CR) == 0 ||
        (v[5] & (TBT_RTR5_CV | TBT_RTR5_PTO)) != (TBT_RTR5_CV | TBT_RTR5_PTO) ||
        tbtU4LaneRd(cm, pr, a, &lane0) != TBT_OK ||
        tbtU4LaneRd(cm, pr, a + 1, &lane1) != TBT_OK) {
        goto watched;
    }
    bonded = (BOOLEAN)(TBT_LANE1_NLW(lane0) == TBT_LANE_WIDTH_X2);
    gen4 = (BOOLEAN)(TBT_LANE1_SPEED(lane0) == TBT_LANE_SPEED_GEN4);
    asym = FALSE;
    if (gen4 && !bonded) {
        /* Record 22 section 6.2.3: a Gen 4 link is bonded when symmetric
         * or a paired asymmetric one; anything else is not adopted. */
        if (tbtU4LaneRd(cm, d, d->UpAdp, &lane1) != TBT_OK ||
            tbtU4LinkKind(lane0, lane1) != TBT_U4_LINK_GEN4_ASYM) {
            goto watched;
        }
        bonded = TRUE;
        asym = TRUE;
    }
    if (!bonded && !tbtU4KidSingleOk(pr, lane1)) {
        goto watched;
    }
    if (!tbtU4KidChained(cm, pr, a, hp, d, &hd, &hu) ||
        !tbtU4LinkOnly(cm, pr, a, hp, TBT_HOP_PROTOCOL, a, hu) ||
        !tbtU4LinkOnly(cm, d, d->UpAdp, d->UpAdp, hd, dp, TBT_HOP_PROTOCOL) ||
        !tbtU4ProtosEmpty(cm, d, dp)) {
        goto watched;
    }
    k->Used = TRUE;
    k->Port = p->Index;
    k->ParentLane0 = a;
    k->ParentPcieDown = hp;
    k->ParentUsb3Down = pr->Tbt3 ? 0 : tbtU4KidPair(cm, p, a, TBT_ADP_TYPE_USB3_DOWN, FALSE);
    cm->KidGeneration++;
    if (cm->KidGeneration == 0) {
        cm->KidGeneration = 1;
    }
    k->Generation = cm->KidGeneration;
    k->ParentGeneration = p->Generation;
    k->Unlocked = (BOOLEAN)!pr->Tbt3;   /* found unlocked: a Lock again is removal */
    k->Bonded = bonded;
    k->SingleLane = (BOOLEAN)!bonded;
    k->Gen4 = (BOOLEAN)(gen4 && bonded);
    k->Asym = asym;
    d->Present = TRUE;
    d->Preserved = TRUE;
    d->Uuid[0] = v[7];
    d->Uuid[1] = v[8];
    d->UuidValid = TRUE;
    d->CapsState = l->CapsState;
    d->CapsEnabled = l->CapsEnabled;
    tbtU4ApplyQuirks(cm, d);
    k->Pcie.Up = TRUE;
    k->Pcie.Preserved = TRUE;
    k->Pcie.Hd = hd;
    k->Pcie.Hu = hu;
    k->Pcie.Written = TBT_U4_E_HOST_PROTO | TBT_U4_E_DEV_LANE | TBT_U4_E_DEV_PROTO |
                      TBT_U4_E_HOST_LANE | TBT_U4_E_HOST_ENABLE | TBT_U4_E_DEV_ENABLE;
    k->Pcie.Own = tbtU4KidCarryMatch(cm, p, k);
    k->State = TBT_U4_UP;
    l->Used = FALSE;    /* never both a kid and a link */
    cm->Counters.KidsAdopted++;
    tbtU4Log(cm, "u4.kid.adopted", (p->Index << 16) | (hd << 8) | hu);
    return;

watched:
    tbtU4Log(cm, "u4.kid.watched", (p->Index << 8) | a);
    tbtU4Zero(k, sizeof(*k));
}

/*
 * Sections 6.4 to 6.6: a kid's connect, as its host port's sequence.
 * EMPTY to UP, or FAILED or UNSUPPORTED; ended by the liveness rule - the
 * child gone, its parent's side torn down (section 6.7); the parent gone,
 * the kid dropped unwritten, the parent's own check tearing it down.
 */
static VOID tbtU4KidConnect(PTBT_U4CM cm, PTBT_U4_PORT p, PTBT_U4_KID k)
{
    ULONG prevPort;
    ULONG prevKid;
    ULONG gen;
    ULONG next;
    ULONG cls;
    ULONG v;
    BOOLEAN pcieWanted;
    BOOLEAN gone;
    LONG rc;

    prevPort = cm->ActivePort;
    prevKid = cm->KidCtx;
    cm->ActivePort = p->Index;
    cm->KidCtx = k->Index;
    cm->Counters.KidConnects++;
    cm->KidGeneration++;
    if (cm->KidGeneration == 0) {
        cm->KidGeneration = 1;
    }
    k->Used = TRUE;
    k->Port = p->Index;
    k->Generation = cm->KidGeneration;
    gen = k->Generation;
    k->ParentGeneration = p->Generation;
    k->State = TBT_U4_CONNECTING;
    k->Why = TBT_U4_WHY_NONE;
    k->UnplugPending = FALSE;
    k->CheckPending = FALSE;
    k->ParentPcieDown = tbtU4KidPair(cm, p, k->ParentLane0, TBT_ADP_TYPE_PCIE_DOWN, TRUE);
    k->ParentUsb3Down = p->Dev.Tbt3 ? 0 :
                        tbtU4KidPair(cm, p, k->ParentLane0, TBT_ADP_TYPE_USB3_DOWN, FALSE);
    tbtU4Log(cm, "u4.kid.connect", (p->Index << 16) | (k->Index << 8) | k->ParentLane0);
    pcieWanted = FALSE;
    rc = tbtU4KidRouter(cm, p, k, gen, &next);
    if (rc != TBT_OK) {
        goto out;
    }
    next = TBT_U4_FAILED;
    k->State = TBT_U4_BONDING;
    rc = tbtU4KidBond(cm, p, k, gen);
    if (rc != TBT_OK) {
        goto out;
    }
    if (k->Dev.Preserved && !k->Dev.Tbt3 && cm->Config.Usb4V2) {
        /* Record 22 section 6.2.2 item 5 through the link view: CV found
         * set is never configured again; without PTO no PCIe tunnel can
         * be built, so HELD; a read that fails holds the kid. Configured
         * only once CV is read clear. */
        rc = tbtU4Rd(cm, &k->Dev.Route, TBT_CS_ROUTER, 0, TBT_ROUTER_CS_5, &v);
        if (rc != TBT_OK) {
            if (tbtU4Gone(cm, rc)) {
                rc = TBT_E_GONE;
                goto out;
            }
            tbtU4Log(cm, "u4.kid.cv.unread", (ULONG)rc);
            next = TBT_U4_HELD;
            k->Why = TBT_U4_WHY_CV_FOUND;
            goto out;
        }
        if ((v & TBT_RTR5_CV) != 0) {
            cm->Counters.CvFound++;
            if ((v & TBT_RTR5_PTO) == 0) {
                tbtU4Log(cm, "u4.kid.cv.found", (k->Index << 24) | (v >> 8));
                next = TBT_U4_HELD;
                k->Why = TBT_U4_WHY_CV_FOUND;
                rc = TBT_E_STATE;
                goto out;
            }
        } else {
            k->State = TBT_U4_CONFIGURING;
            rc = tbtU4KidConfigValid(cm, p, k, gen);
            if (rc != TBT_OK) {
                goto out;
            }
        }
    } else if (!k->Dev.Preserved ||
               tbtU4Rd(cm, &k->Dev.Route, TBT_CS_ROUTER, 0, TBT_ROUTER_CS_5, &v) != TBT_OK ||
               (v & (TBT_RTR5_CV | TBT_RTR5_PTO)) != (TBT_RTR5_CV | TBT_RTR5_PTO)) {
        k->State = TBT_U4_CONFIGURING;
        rc = tbtU4KidConfigValid(cm, p, k, gen);
        if (rc != TBT_OK) {
            goto out;
        }
    }
    k->State = TBT_U4_TUNNELLING;
    if (k->ParentPcieDown == 0) {
        tbtU4Log(cm, "u4.kid.no-pcie-down", (p->Index << 8) | k->ParentLane0);
    }
    pcieWanted = (BOOLEAN)(cm->Config.Approve && k->ParentPcieDown != 0 && !k->Ejected &&
                           tbtU4FirstOf(&k->Dev, TBT_ADP_TYPE_PCIE_UP) != 0);
    if (pcieWanted) {
        rc = tbtU4KidPcieTunnel(cm, p, k, gen);
        if (rc == TBT_E_GONE) {
            goto out;
        }
        if (rc != TBT_OK) {
            k->PcieFailed = TRUE;
        }
    }
    if (!tbtU4KidLive(cm, p, k, gen)) {
        rc = TBT_E_GONE;
        goto out;
    }
    k->State = (k->Pcie.Up || !pcieWanted) ? TBT_U4_UP : TBT_U4_FAILED;
    tbtU4Log(cm, "u4.kid.state", (k->Index << 8) | k->State | (k->Why << 16));
    cm->KidCtx = prevKid;
    cm->ActivePort = prevPort;
    return;

out:
    if (tbtU4Spent(cm) && k->Used && k->Generation == gen) {
        /* A resume's budget, or the containment: nothing torn down
         * without evidence (record 22 sections 7.6, 7.7). */
        k->State = TBT_U4_HELD;
        k->Why = tbtU4Off(cm) ? TBT_U4_WHY_CONTAINED : TBT_U4_WHY_RESUME;
    } else if (k->Used && k->Generation == gen) {
        gone = (BOOLEAN)(rc == TBT_E_GONE || !tbtU4KidLive(cm, p, k, gen));
        if (!gone) {
            k->State = next;
            tbtU4Log(cm, "u4.kid.state", (k->Index << 8) | k->State | (k->Why << 16));
        } else if (!tbtU4Live(cm, p, k->ParentGeneration) || p->State != TBT_U4_UP ||
                   tbtU4Evidence(cm, p, &cls)) {
            /* The parent left (section 6.8): its own check, at the next
             * tick, tears it down; the kid goes with no register work. */
            cm->Counters.KidsDropped++;
            tbtU4Log(cm, "u4.kid.dropped", (p->Index << 8) | k->Index);
            tbtU4KidFree(cm, k);
        } else {
            tbtU4KidTeardown(cm, p, k);
        }
    }
    cm->KidCtx = prevKid;
    cm->ActivePort = prevPort;
}

/* No entry of the parent's lane adapters `a` and `a` + 1 in use, as input
 * or as output, from HopID 8 up (the registers and the quarantine).
 * FALSE when anything cannot be read. */
static BOOLEAN tbtU4KidLanesIdle(PTBT_U4CM cm, const TBT_U4_ROUTER *pr, ULONG a)
{
    TBT_U4_USAGE u;
    ULONG k;
    ULONG i;

    for (k = 0; k < 2; k++) {
        if (a + k >= TBT_U4_MAX_ADP || !pr->Adp[a + k].Present) {
            continue;
        }
        if (tbtU4ScanLane(cm, pr, a + k, &u) != TBT_OK) {
            return FALSE;
        }
        for (i = 0; i < 4; i++) {
            if ((u.InUse[i] | u.OutUse[i]) != 0) {
                return FALSE;
            }
        }
    }
    return TRUE;
}

/* The parent's downstream Disable Hot Plug cleared (section 6.5): on a
 * USB4 parent UP with its PCIe tunnel, once per connection of it, at the
 * first idle tick that finds it so. Read first; written only when set. */
static VOID tbtU4KidDhp(PTBT_U4CM cm, PTBT_U4_PORT p)
{
    PTBT_U4_ROUTER pr;
    ULONG a;
    ULONG v;
    ULONG bit;
    BOOLEAN ok;

    pr = &p->Dev;
    if (pr->Tbt3 || p->Index == 0 || p->Index > TBT_U4_MAX_PORTS) {
        return;
    }
    bit = 1UL << (p->Index - 1);
    if ((cm->KidDhpValid & bit) != 0 && cm->KidDhpGen[p->Index - 1] == p->Generation) {
        return;
    }
    ok = TRUE;
    for (a = 1; a <= pr->MaxAdp && a < TBT_U4_MAX_ADP; a++) {
        if (!tbtU4DownLane0(pr, a)) {
            continue;
        }
        if (tbtU4Rd(cm, &pr->Route, TBT_CS_ADAPTER, a, TBT_ADP_CS_5, &v) != TBT_OK ||
            ((v & TBT_ADP5_DHP) != 0 &&
             tbtU4Wr(cm, &pr->Route, TBT_CS_ADAPTER, a, TBT_ADP_CS_5, v & ~TBT_ADP5_DHP) !=
                 TBT_OK)) {
            ok = FALSE;
        }
    }
    if (ok) {
        cm->KidDhpValid |= bit;
        cm->KidDhpGen[p->Index - 1] = p->Generation;
    }
}

/*
 * Each idle tick, after the host port's own check: the kids of `p`
 * checked from the registers (section 6.5: the parent's downstream lane 0
 * and its Lock, the child's TopologyID and UUID; for an UP kid its PCIe
 * tunnel), a removal torn down; then, the parent UP with its PCIe tunnel
 * and XhciCmApprove 1, every downstream port of the parent without a kid
 * read once - a watched depth-2 link there read for adoption once, and a
 * connected lane connected as a kid.
 */
static VOID tbtU4KidService(PTBT_U4CM cm, PTBT_U4_PORT p)
{
    PTBT_U4_ROUTER pr;
    PTBT_U4_KID k;
    ULONG i;
    ULONG a;
    ULONG cls;
    ULONG lane;
    BOOLEAN up;
    BOOLEAN busy;

    pr = &p->Dev;
    up = (BOOLEAN)(p->State == TBT_U4_UP && p->Pcie.Up && pr->Present && pr->Depth == 1);
    for (i = 0; i < TBT_U4_MAX_KIDS; i++) {
        k = &cm->Kids[i];
        if (!k->Used || k->Port != p->Index) {
            continue;
        }
        if (p->State == TBT_U4_EMPTY || !pr->Present || pr->Depth != 1 ||
            k->ParentGeneration != p->Generation) {
            /* Its parent left without the teardown dropping it, or the
             * port holds another connection now: dropped, unwritten -
             * nothing of it is ever written to a router that replaced its
             * parent. */
            cm->Counters.KidsDropped++;
            tbtU4Log(cm, "u4.kid.dropped", (p->Index << 8) | k->Index);
            tbtU4KidFree(cm, k);
            continue;
        }
        if (!pr->Present || pr->Depth != 1 ||
            (p->State != TBT_U4_UP && p->State != TBT_U4_FAILED &&
             p->State != TBT_U4_HELD) ||
            (p->State == TBT_U4_HELD &&
             (p->ClassifyPending || p->Why == TBT_U4_WHY_RECOVER || p->Barred))) {
            /* A parent not yet classified (a resume's, read again at its
             * next check; a recovery's) is not written for its kids: they
             * wait, their cleanup and checks with them. */
            continue;
        }
        if (k->SweepOwed) {
            tbtU4KidTeardown(cm, p, k);
            continue;
        }
        /* Under a parent FAILED or HELD (record 34 section 6.3) a kid is
         * still served by its own unplug: its removal read, and on
         * evidence its teardown; nothing else, until the parent is UP. */
        k->CheckPending = FALSE;
        cm->ActivePort = p->Index;
        if (tbtU4KidEvidence(cm, p, k, &cls)) {
            cm->ActivePort = 0;
            tbtU4KidTeardown(cm, p, k);
            continue;
        }
        k->UnplugPending = FALSE;
        if (up && k->State == TBT_U4_UP) {
            tbtU4KidPcieLoss(cm, p, k);
        }
        cm->ActivePort = 0;
    }
    if (!up || !cm->Config.Approve || p->Barred || p->Ejected || p->UnplugPending ||
        cm->Contained) {
        return;
    }
    tbtU4KidDhp(cm, p);
    for (a = 1; a <= pr->MaxAdp && a < TBT_U4_MAX_ADP; a++) {
        if (!tbtU4DownLane0(pr, a) || p->State != TBT_U4_UP || !p->Pcie.Up) {
            continue;
        }
        busy = FALSE;
        for (i = 0; i < TBT_U4_MAX_KIDS; i++) {
            if (cm->Kids[i].Used && cm->Kids[i].Port == p->Index &&
                cm->Kids[i].ParentLane0 == a) {
                busy = TRUE;
            }
        }
        for (i = 0; i < TBT_U4_MAX_LINKS && !busy; i++) {
            if (cm->Links[i].Used && cm->Links[i].Port == p->Index &&
                TbtRouteEqual(&cm->Links[i].Parent, &pr->Route) &&
                cm->Links[i].ParentLane0 == a) {
                if (!cm->Links[i].AdoptTried) {
                    tbtU4KidAdopt(cm, p, i);
                }
                busy = cm->Links[i].Used;
            }
        }
        if (busy) {
            continue;
        }
        for (i = 0; i < TBT_U4_MAX_KIDS; i++) {
            if (cm->Kids[i].Used && cm->Kids[i].Port == p->Index &&
                cm->Kids[i].ParentLane0 == a) {
                busy = TRUE;    /* adopted just now */
            }
        }
        if (busy || tbtU4LaneRd(cm, pr, a, &lane) != TBT_OK ||
            TbtU4LaneClass(TBT_LANE1_STATE(lane)) != TBT_U4_LANE_CONNECTED) {
            continue;
        }
        if (!tbtU4KidLanesIdle(cm, pr, a)) {
            /* Guide 7.1, as tbtU4Connect's tbtU4LanesIdle: no lane change
             * while anything live, pending or held is on the link; the
             * next tick reads it again. */
            tbtU4Log(cm, "u4.kid.connect.deferred", (p->Index << 8) | a);
            continue;
        }
        k = tbtU4KidSlot(cm, p, a);
        if (k == NULL) {
            continue;
        }
        k->ParentLane0 = a;
        tbtU4KidConnect(cm, p, k);
    }
}

LONG TbtU4EjectKid(PTBT_U4CM cm, ULONG port, ULONG kid)
{
    PTBT_U4_PORT p;
    PTBT_U4_KID k;
    BOOLEAN closed;

    if (cm->Contained) {
        cm->Counters.ContainedRefused++;
        return TBT_E_STOPPED;
    }
    p = tbtU4PortOf(cm, port);
    k = tbtU4KidOf(cm, kid);
    if (p == NULL || k == NULL || k->Port != port || cm->Failed || !p->Dev.Present ||
        !k->Dev.Present || k->SweepOwed || k->State == TBT_U4_TEARDOWN) {
        return TBT_E_PARAM;
    }
    cm->Counters.Ejects++;
    closed = tbtU4KidDown(cm, p, k, TBT_U4_WHY_EJECTED);
    return closed ? TBT_OK : TBT_E_STATE;
}

const TBT_U4_KID *TbtU4Kid(const TBT_U4CM *cm, ULONG kid)
{
    if (kid == 0 || kid > TBT_U4_MAX_KIDS) {
        return NULL;
    }
    return &cm->Kids[kid - 1];
}

/*
 * Section 6.9 across a sleep: a kid whose parent verified, verified by
 * the same reading as adoption (the child the same router, still
 * enumerated, Router Ready, CV, PTO, Configuration Ready; a USB4 parent's
 * lane still unlocked; the link bonded or lane 1 down; the chained tunnel
 * whole at its recorded HopIDs). Reads only.
 */
static BOOLEAN tbtU4KidVerify(PTBT_U4CM cm, PTBT_U4_PORT p, PTBT_U4_KID k)
{
    PTBT_U4_ROUTER pr;
    PTBT_U4_ROUTER d;
    ULONG v[9];
    ULONG cs4;
    ULONG lane0;
    ULONG lane1;
    ULONG hd;
    ULONG hu;

    pr = &p->Dev;
    d = &k->Dev;
    if (!d->Present || !d->UuidValid || !k->Pcie.Up ||
        tbtU4RdN(cm, &d->Route, TBT_CS_ROUTER, 0, TBT_ROUTER_CS_0, 9, v) != TBT_OK ||
        TBT_RTR0_VENDOR(v[0]) != d->Vendor || TBT_RTR0_DEVICE(v[0]) != d->Device ||
        v[7] != d->Uuid[0] || v[8] != d->Uuid[1] || !tbtU4Enumerated(d, v) ||
        (v[6] & TBT_RTR6_RR) == 0 || (v[6] & TBT_RTR6_CR) == 0 ||
        (v[5] & (TBT_RTR5_CV | TBT_RTR5_PTO)) != (TBT_RTR5_CV | TBT_RTR5_PTO)) {
        return FALSE;
    }
    if (!pr->Tbt3 &&
        (tbtU4Rd(cm, &pr->Route, TBT_CS_ADAPTER, k->ParentLane0, TBT_ADP_CS_4,
                 &cs4) != TBT_OK || (cs4 & TBT_ADP4_LOCK) != 0)) {
        return FALSE;
    }
    if (tbtU4LaneRd(cm, pr, k->ParentLane0, &lane0) != TBT_OK ||
        tbtU4LaneRd(cm, pr, k->ParentLane0 + 1, &lane1) != TBT_OK ||
        (!tbtU4WidthBonded(lane0) && !tbtU4KidSingleOk(pr, lane1))) {
        return FALSE;
    }
    return (BOOLEAN)(tbtU4KidChained(cm, pr, k->ParentLane0, k->ParentPcieDown, d, &hd, &hu) &&
                     hd == k->Pcie.Hd && hu == k->Pcie.Hu);
}

/*
 * One kid obligation at resume (section 6.9), after every port's: the
 * parent HELD (or the resume unable to judge) leaves the kid HELD,
 * nothing written, its obligation kept; a parent verified as the same
 * has its kid verified, a kid with removal evidence torn down (the next
 * tick connects what is there afresh), anything else HELD; a parent the
 * resume rebuilt (the domain lost its configuration) has a kid this
 * driver built rebuilt after it by section 6.4's sequence, else HELD; a
 * parent discharged otherwise took its kid with it. TRUE when met.
 */
static BOOLEAN tbtU4KidResume(PTBT_U4CM cm, PTBT_U4_OBLIGATION o, BOOLEAN moved)
{
    PTBT_U4_PORT p;
    PTBT_U4_KID k;
    ULONG prevPort;
    ULONG prevKid;
    ULONG cls;
    ULONG keep[3];
    BOOLEAN met;

    p = tbtU4PortOf(cm, o->Port);
    k = tbtU4KidOf(cm, o->Kid);
    if (k == NULL || k->Port != o->Port || k->Generation != o->Generation) {
        o->Used = FALSE;
        return (BOOLEAN)(k == NULL || k->Port != o->Port || k->Pcie.Up);
    }
    if (p == NULL) {
        return FALSE;
    }
    (void)tbtU4Spent(cm);
    if (moved || cm->BudgetSpent || p->State == TBT_U4_HELD) {
        k->State = TBT_U4_HELD;
        k->Why = TBT_U4_WHY_RESUME;
        return FALSE;
    }
    if (p->State != TBT_U4_UP || !p->Pcie.Up || !p->Dev.Present) {
        o->Used = FALSE;
        return (BOOLEAN)!k->Pcie.Up;
    }
    o->Used = FALSE;
    prevPort = cm->ActivePort;
    prevKid = cm->KidCtx;
    cm->ActivePort = p->Index;
    cm->KidCtx = k->Index;
    met = FALSE;
    if (p->Generation == k->ParentGeneration) {
        if (tbtU4KidVerify(cm, p, k)) {
            met = TRUE;
        } else if (tbtU4KidEvidence(cm, p, k, &cls)) {
            tbtU4KidTeardown(cm, p, k);
            met = TRUE;
        }
    } else if (o->BuiltHere) {
        /* Rebuilt after its parent's (section 6.9), the old records gone
         * with the configuration the domain lost. */
        keep[0] = k->Index;
        keep[1] = k->Port;
        keep[2] = k->ParentLane0;
        tbtU4Zero(k, sizeof(*k));
        k->Index = keep[0];
        k->Port = keep[1];
        k->ParentLane0 = keep[2];
        tbtU4KidConnect(cm, p, k);
        met = (BOOLEAN)(k->Used && k->State == TBT_U4_UP && k->Pcie.Up);
    }
    if (!met && k->Used && k->State != TBT_U4_EMPTY) {
        k->State = TBT_U4_HELD;
        k->Why = TBT_U4_WHY_RESUME;
        if (k->Pcie.Up) {
            o->Used = TRUE;     /* still owed */
        }
    }
    cm->KidCtx = prevKid;
    cm->ActivePort = prevPort;
    return met;
}

/*
 * Section 6.9, the host interface lost (record 22 section 7.7): every kid
 * becomes a discovered link again before the recovery classifies its
 * parent - kept, drained or held by the parent's classification, never
 * rebuilt by the recovery - and one that still verifies is adopted again
 * by the start rule at the first tick after the commit. Nothing written.
 */
static VOID tbtU4KidsToLinks(PTBT_U4CM cm)
{
    ULONG i;
    ULONG j;
    ULONG need;
    ULONG room;
    PTBT_U4_KID k;
    PTBT_U4_PORT p;
    PTBT_U4_LINK l;

    need = 0;
    room = 0;
    for (i = 0; i < TBT_U4_MAX_KIDS; i++) {
        if (cm->Kids[i].Used && !cm->Kids[i].SweepOwed &&
            cm->Kids[i].State != TBT_U4_TEARDOWN && cm->Kids[i].Dev.Present &&
            cm->Kids[i].Dev.Depth == 2 && tbtU4PortOf(cm, cm->Kids[i].Port) != NULL) {
            need++;
        }
    }
    for (j = 0; j < TBT_U4_MAX_LINKS; j++) {
        if (!cm->Links[j].Used) {
            room++;
        }
    }
    if (need > room) {
        /* No record would keep a kid's state (an eject's above all): the
         * attempt is refused, as one that cannot hold an entry unwritten
         * (RecReserveFull), and the kids stay as they are. */
        cm->RecReserveFull = TRUE;
        tbtU4Log(cm, "u4.recover.kids-full", (need << 8) | room);
        return;
    }
    for (i = 0; i < TBT_U4_MAX_KIDS; i++) {
        k = &cm->Kids[i];
        if (!k->Used || k->SweepOwed || k->State == TBT_U4_TEARDOWN) {
            /* A child gone whose teardown still owes its sweep or a lane
             * change stays a kid: what it owes is not yet a record a link
             * could keep, and it is finished or dropped with its parent
             * after the commit (tbtU4KidService). */
            continue;
        }
        p = tbtU4PortOf(cm, k->Port);
        if (p != NULL && k->Dev.Present && k->Dev.Depth == 2) {
            l = NULL;
            for (j = 0; j < TBT_U4_MAX_LINKS; j++) {
                if (!cm->Links[j].Used) {
                    l = &cm->Links[j];
                    break;
                }
            }
            if (l == NULL) {
                tbtU4Log(cm, "u4.links-full", k->ParentLane0);
            } else {
                tbtU4Zero(l, sizeof(*l));
                l->Used = TRUE;
                l->Port = k->Port;
                l->Parent = p->Dev.Route;
                l->ParentLane0 = k->ParentLane0;
                l->ParentLaneCap = p->Dev.Adp[k->ParentLane0].CapLane;
                l->Child = k->Dev.Route;
                l->ChildUuid[0] = k->Dev.Uuid[0];
                l->ChildUuid[1] = k->Dev.Uuid[1];
                l->ChildUuidValid = k->Dev.UuidValid;
                l->CapsState = k->Dev.CapsState;
                l->CapsEnabled = k->Dev.CapsEnabled;
                l->ChildDromRead = TRUE;
                l->ChildDromUnused[0] = k->Dev.DromUnused[0];
                l->ChildDromUnused[1] = k->Dev.DromUnused[1];
                /* Ejected, or FAILED for PCIe: down until a re-plug, so
                 * never adopted or connected from the link. */
                l->KidDown = (BOOLEAN)(k->Ejected || k->PcieFailed);
                l->AdoptTried = l->KidDown;
            }
        }
        for (j = 0; j < TBT_U4_OBLIGATIONS; j++) {
            if (cm->Obligations[j].Used && cm->Obligations[j].Kid == k->Index) {
                cm->Obligations[j].Used = FALSE;
            }
        }
        j = k->Index;
        tbtU4Zero(k, sizeof(*k));
        k->Index = j;
    }
}

/* ---------------------------------------------------------------------- */
/* The public half                                                        */
/* ---------------------------------------------------------------------- */

VOID TbtU4DefaultConfig(PTBT_U4_CONFIG cfg)
{
    tbtU4Zero(cfg, sizeof(*cfg));
    cfg->Approve = TRUE;
    cfg->Usb3Tunnel = TRUE;
    cfg->LaneBonding = TRUE;
    cfg->PreOsCleanup = 0;
    cfg->Usb4V2 = TRUE;
    cfg->Gen4LinkRecovery = TRUE;
}

VOID TbtU4Init(PTBT_U4CM cm, PTBT_RING ring, const TBT_HOST *host,
               const TBT_U4_CONFIG *config)
{
    tbtU4Zero(cm, sizeof(*cm));
    cm->Ring = ring;
    cm->Host = host;
    cm->ResumeHostUs = TBT_U4_RESUME_HOST_US;
    cm->ResumeEachUs = TBT_U4_RESUME_EACH_US;
    if (config != NULL) {
        cm->Config = *config;
    } else {
        TbtU4DefaultConfig(&cm->Config);
    }
    if (cm->Config.PreOsCleanup > 2) {
        cm->Config.PreOsCleanup = 0;
    }
    if (ring != NULL) {
        TbtRingSetXdomainSink(ring, tbtU4XdSink, cm);
    }
}

/* Record 22 section 10.5: every port's isolation statement withdrawn, one
 * aligned store each. */
static VOID tbtU4IsoRevokeAll(PTBT_U4CM cm)
{
    ULONG i;

    for (i = 0; i < TBT_U4_MAX_PORTS; i++) {
        cm->IsoWord[i] = 0;
    }
}

BOOLEAN TbtU4PortIsolated(const TBT_U4CM *cm, ULONG port, ULONG gen)
{
    ULONG w;

    if (cm == NULL || port < 1 || port > TBT_U4_MAX_PORTS) {
        return FALSE;
    }
    w = cm->IsoWord[port - 1];
    return (BOOLEAN)(w == TBT_U4_ISO_WORD(gen));
}

BOOLEAN TbtU4PortPcieGen(const TBT_U4CM *cm, ULONG port, PULONG gen)
{
    const TBT_U4_PORT *p;
    ULONG g1;
    ULONG g2;
    BOOLEAN up;

    *gen = 0;
    if (cm == NULL || port < 1 || port > TBT_U4_MAX_PORTS ||
        port > cm->PortCount) {
        return FALSE;
    }
    p = &cm->Ports[port - 1];
    /* Another thread's sample of the connection manager's fields: the
     * generation read on both sides of Up, so an Up and a generation of
     * two different connections are never paired (a teardown clears Up,
     * a reconnect moves the generation). */
    g1 = *(volatile const ULONG *)&p->Generation;
    up = *(volatile const BOOLEAN *)&p->Pcie.Up;
    g2 = *(volatile const ULONG *)&p->Generation;
    if (!up || g1 != g2) {
        return FALSE;
    }
    *gen = g1;
    return TRUE;
}

VOID TbtU4SetLinkDown(PTBT_U4CM cm, TBT_U4_LINKDOWN_FN fn, PVOID context)
{
    cm->LinkDown = fn;
    cm->LinkDownContext = context;
}

/* Section 3.5's failure after activation: the rings stopped - nothing of
 * theirs is pending - and the fabric left as it is. */
static LONG tbtU4Fail(PTBT_U4CM cm, LONG rc)
{
    if (!cm->Contained) {
        TbtRingStop(cm->Ring);
    }
    cm->Failed = TRUE;
    cm->Disturbed = TRUE;
    if (cm->InertReason == TBT_INERT_NONE) {
        cm->InertReason = TBT_INERT_PROTOCOL;
    }
    tbtU4Log(cm, "u4.inert-after-activation", cm->InertReason);
    return rc != TBT_OK ? rc : TBT_E_STATE;
}

/* The rings and the transport, ring 0 taken over (section 4.4). */
static LONG tbtU4Rings(PTBT_U4CM cm)
{
    LONG rc;

    rc = TbtRingStartEx(cm->Ring, cm->Host, cm->Va, cm->Phys, cm->Bytes,
                        TBT_RX_PDF_ALL_ONES,
                        (cm->Config.RingFlags & ~TBT_RING_START_RESET_HI) |
                        TBT_RING_START_TAKEOVER,
                        TBT_RING_U4_ENTRIES);
    if (rc != TBT_OK) {
        return rc;
    }
    TbtCtlInit(&cm->Ctl, cm->Ring);
    TbtCtlSetAckForm(&cm->Ctl, tbtU4AckForm, cm);
    TbtCtlSetSafePoint(&cm->Ctl, tbtU4SafePoint, cm);
    cm->Stage = TBT_U4_STAGE_RINGS;
    return TBT_OK;
}

/* Hot plug events enabled on every port that carries no preserved tunnel
 * (section 6.1; Guide 3.1.1: ADP_CS_5 Disable Hot Plug cleared on the
 * lane 0 adapter). */
static VOID tbtU4HotPlugEnable(PTBT_U4CM cm)
{
    ULONG i;
    PTBT_U4_PORT p;

    for (i = 0; i < cm->PortCount; i++) {
        p = &cm->Ports[i];
        if (p->Pcie.Preserved || p->Usb3.Preserved || p->DpPreserved ||
            p->State == TBT_U4_HELD) {
            continue;
        }
        if (tbtU4Rmw(cm, &cm->HostRouter.Route, TBT_CS_ADAPTER, p->Lane0, TBT_ADP_CS_5,
                     TBT_ADP5_DHP, 0) == TBT_OK) {
            p->HotPlugEnabled = TRUE;
        }
    }
}

/*
 * Section 9.3 on a version 2 host interface with XhciCmPreOsCleanup 2: the
 * Host Router Reset of Guide 3.5 for the whole domain in place of the
 * per-port resets - the rings disabled at least 1 ms before, HRR set and
 * left untouched 50 ms, cleared by the host within 500 ms (Guide 3.5
 * notes 1 to 3), then the rings again and the host router enumerated
 * afresh.
 * Every port is EMPTY after it, its link retraining.
 */
static LONG tbtU4HostRouterReset(PTBT_U4CM cm)
{
    ULONG v;
    ULONG i;
    LONG rc;
    TBT_WAIT w;

    cm->Counters.HostRouterResets++;
    tbtU4Log(cm, "u4.host-router-reset", 1);
    TbtRingStop(cm->Ring);
    TbtWaitMin(cm->Host, TBT_RING_SETTLE_US);
    cm->Host->Write32(cm->Host->Context, TBT_HI_HR_RESET, TBT_HI_HR_RESET_HRR);
    TbtWaitMin(cm->Host, TBT_U4_HRR_HOLD_US);
    TbtWaitStart(&w, cm->Host, TBT_U4_HRR_DONE_US, FALSE);
    for (;;) {
        v = cm->Host->Read32(cm->Host->Context, TBT_HI_HR_RESET);
        if (v != 0xFFFFFFFFUL && (v & TBT_HI_HR_RESET_HRR) == 0) {
            break;
        }
        if (TbtWaitDone(&w, cm->Host)) {
            cm->InertReason = TBT_INERT_PROTOCOL;
            return TBT_E_TIMEOUT;
        }
        TbtWaitStep(&w, cm->Host, TBT_U4_POLL_STEP_US);
    }
    rc = tbtU4Rings(cm);
    if (rc != TBT_OK) {
        return rc;
    }
    for (i = 0; i < TBT_U4_MAX_LINKS; i++) {
        cm->Links[i].Used = FALSE;
    }
    for (i = 0; i < TBT_U4_MAX_KIDS; i++) {
        tbtU4Zero(&cm->Kids[i], sizeof(cm->Kids[i]));
    }
    rc = tbtU4HostRouter(cm);
    if (rc != TBT_OK) {
        return rc;
    }
    tbtU4HostTmu(cm);
    for (i = 0; i < cm->PortCount; i++) {
        cm->Ports[i].State = TBT_U4_EMPTY;
        cm->Ports[i].CheckPending = TRUE;
    }
    return TBT_OK;
}

LONG TbtU4Start(PTBT_U4CM cm, PVOID va, ULONG phys, ULONG bytes)
{
    ULONG i;
    BOOLEAN hrr;
    BOOLEAN any;
    LONG rc;
    PTBT_U4_PORT p;

    if (tbtU4Off(cm)) {
        /* Section 7.7: a contained instance never starts again; a new one
         * (TbtU4Init) does. Nothing changed, nothing touched. */
        cm->Counters.ContainedRefused++;
        return TBT_E_STOPPED;
    }
    tbtU4IsoRevokeAll(cm);
    cm->Va = va;
    cm->Phys = phys;
    cm->Bytes = bytes;
    cm->Failed = FALSE;
    cm->InertReason = TBT_INERT_NONE;
    rc = tbtU4Rings(cm);
    if (rc != TBT_OK) {
        return rc;      /* before activation: nothing built */
    }
    rc = tbtU4HostRouter(cm);
    if (rc != TBT_OK) {
        return tbtU4Fail(cm, rc);
    }
    cm->Stage = TBT_U4_STAGE_HOST;
    cm->HostReached = TRUE;
    tbtU4HostTmu(cm);

    for (i = 0; i < cm->PortCount; i++) {
        tbtU4DiscoverPort(cm, &cm->Ports[i]);
    }
    if (cm->Config.PreOsCleanup != 0) {
        hrr = (BOOLEAN)(cm->Config.PreOsCleanup == 2 &&
                        TBT_HI_CAPS_VERSION(cm->Config.HostInterfaceCaps) ==
                        TBT_HI_VERSION_2);
        any = FALSE;
        for (i = 0; i < cm->PortCount; i++) {
            p = &cm->Ports[i];
            if (p->State == TBT_U4_HELD ||
                (cm->Config.PreOsCleanup == 2 &&
                 (p->Pcie.Preserved || p->Usb3.Preserved || p->DpPreserved))) {
                tbtU4Clean(cm, p, (BOOLEAN)!hrr);
                any = TRUE;
            }
        }
        if (hrr && any) {
            rc = tbtU4HostRouterReset(cm);
            if (rc != TBT_OK) {
                return tbtU4Fail(cm, rc);
            }
        }
    }
    tbtU4HotPlugEnable(cm);
    cm->Stage = TBT_U4_STAGE_RUNNING;
    cm->RanHealthy = TRUE;
    if (!cm->Config.DeferFirstPoll) {
        TbtU4Poll(cm);
    }
    return TBT_OK;
}

/*
 * A Thunderbolt 3 router's names owed (DromOwed: adopted, or rebuilt under
 * a resume budget; record 22 section 8 item 4), read on a pass with no
 * budget running, once, while its port is UP: the EEPROM bit-bang of
 * tbtU4ReadDrom as at a connect, as the port's own sequence (ActivePort,
 * so a removal seen inside it waits for tbtU4Check as a connect's does).
 * Only the names are taken: the adapters were discovered without the
 * DROM and stay as they are, so the unused marks it carries are left as
 * they were. A read that fails leaves the router unnamed, as at a
 * connect.
 */
static VOID tbtU4DromLate(PTBT_U4CM cm, PTBT_U4_PORT p)
{
    ULONG unused[2];
    ULONG gen;

    if (!p->Dev.DromOwed || !p->Dev.Present || !p->Dev.Tbt3 ||
        p->Dev.CapPlugEvents == 0 || p->State != TBT_U4_UP || cm->Budgeted ||
        p->UnplugPending) {
        return;
    }
    p->Dev.DromOwed = FALSE;
    gen = p->Generation;
    unused[0] = p->Dev.DromUnused[0];
    unused[1] = p->Dev.DromUnused[1];
    cm->ActivePort = p->Index;
    tbtU4ReadDrom(cm, &p->Dev);
    cm->ActivePort = 0;
    if (p->Generation == gen && p->Dev.Present) {
        p->Dev.DromUnused[0] = unused[0];
        p->Dev.DromUnused[1] = unused[1];
        tbtU4Log(cm, "u4.drom.late", (p->Index << 24) |
                                     (p->Dev.ModelName[0] != 0 ? 1UL : 0UL));
    }
}

/*
 * Record 22 section 7.7 part 4, decision U28: the per-tunnel rebuild
 * transition, after a recovery's commit, for a tunnel it drained at both
 * ends on the router it adopted - not the whole-port connect of section
 * 6.4, which restores the lanes, wants them idle and decides both tunnels.
 * From UP (a sibling preserved), HELD or FAILED; the lanes and the TMU as
 * found, the sibling untouched, Ejected and PcieFailed respected. PCIe:
 * section 6.8 behind the Detect gate at both ends (the USB4 end alone for
 * a Thunderbolt 3 router), its TUNNEL_UP queued as any tunnel up's, whose
 * exposure is the one owner of the generation change. USB3: section 6.9,
 * only where the TMU still reads usable, with its reuse gap; no action.
 * TRUE when a build was attempted.
 */
static BOOLEAN tbtU4Rebuild(PTBT_U4CM cm, PTBT_U4_PORT p)
{
    ULONG gen;
    LONG rc;
    BOOLEAN tried;

    if (p->State == TBT_U4_EMPTY || !p->Dev.Present ||
        (p->State != TBT_U4_UP && p->State != TBT_U4_HELD &&
         p->State != TBT_U4_FAILED)) {
        p->RebuildPcie = FALSE;
        p->RebuildUsb3 = FALSE;
        return FALSE;
    }
    gen = p->Generation;
    tried = FALSE;
    cm->ActivePort = p->Index;
    if (p->RebuildPcie) {
        p->RebuildPcie = FALSE;
        if (!p->Ejected && !p->PcieFailed && !p->Pcie.Up) {
            tried = TRUE;
            rc = tbtU4PcieTunnel(cm, p, gen);
            tbtU4Log(cm, "u4.recover.rebuild.pcie", (p->Index << 16) | (ULONG)(-rc));
            if (rc == TBT_OK) {
                cm->Counters.RecoverRebuilt++;
            } else if (rc != TBT_E_GONE) {
                p->PcieFailed = TRUE;
            }
        }
    }
    if (p->RebuildUsb3 && TBT_U4_LIVE(p, gen)) {
        p->RebuildUsb3 = FALSE;
        if (!p->Usb3.Up && !p->Usb3Failed) {
            tried = TRUE;
            rc = TBT_E_STATE;
            if (p->TmuOk && p->Usb3Down != 0) {
                rc = tbtU4Usb3Tunnel(cm, p, gen);
            }
            tbtU4Log(cm, "u4.recover.rebuild.usb3", (p->Index << 16) | (ULONG)(-rc));
            if (rc == TBT_OK) {
                cm->Counters.RecoverRebuilt++;
            } else if (rc != TBT_E_GONE) {
                p->Usb3Failed = TRUE;
            }
        }
    }
    cm->ActivePort = 0;
    if (TBT_U4_LIVE(p, gen) && p->State != TBT_U4_EMPTY) {
        if (p->Pcie.Up || (p->Usb3.Up && !p->Ejected && !p->PcieFailed &&
                           p->PcieDown == 0)) {
            p->State = TBT_U4_UP;
            p->Why = TBT_U4_WHY_NONE;
        } else if (p->Usb3.Up) {
            /* TbtU4EjectPcie's shape: FAILED for PCIe, USB3 up. */
            p->State = TBT_U4_FAILED;
        } else if (!p->RebuildPcie && !p->RebuildUsb3) {
            p->State = TBT_U4_FAILED;
        }
    }
    return tried;
}

/* ---------------------------------------------------------------------- */
/* Gen 4 Link Recovery (record 22 section 6.2.3 item 4, task 45.56)       */
/* ---------------------------------------------------------------------- */

static BOOLEAN tbtU4TmuAsLeft(PTBT_U4CM cm, PTBT_U4_PORT p);

/* The port is UP or FAILED with its router, and nothing else owed on it
 * that a tick serves first (parts 1, 2 and 4). */
static BOOLEAN tbtU4ElrIdle(PTBT_U4CM cm, const TBT_U4_PORT *p)
{
    return (BOOLEAN)((p->State == TBT_U4_UP || p->State == TBT_U4_FAILED) &&
                     p->Dev.Present && !p->Barred && !p->RecDeparted &&
                     !p->UnplugPending && !p->CheckPending && !p->ClassifyPending &&
                     !p->DiscoverOwed && !p->AsFoundOwed &&
                     !cm->DeferredTail[p->Index - 1]);
}

static VOID tbtU4ElrOff(PTBT_U4CM cm, PTBT_U4_PORT p, BOOLEAN dev);

/* Part 1: a depth-1 Gen 4 link of two version 2 routers, both values 1. */
static BOOLEAN tbtU4ElrWanted(PTBT_U4CM cm, const TBT_U4_PORT *p)
{
    return (BOOLEAN)(cm->Config.Gen4LinkRecovery && tbtU4V2Link(cm, p) && p->Gen4 &&
                     !p->DpPreserved && cm->HostRouter.Adp[p->Lane0].CapPort != 0 &&
                     p->Dev.UpAdp != 0 && p->Dev.Adp[p->Dev.UpAdp].CapPort != 0);
}

/* Part 2: set on both ends, device end first, each read back; a reading
 * short of CL0 at both lane 0s or with StartAsymmetricFlow set writes
 * nothing (the next tick tries again); a refusal or a failure writes the
 * bit 0 again where it was written and is not tried again on this
 * connection. The port's own sequence. */
static VOID tbtU4ElrSet(PTBT_U4CM cm, PTBT_U4_PORT p)
{
    PTBT_U4_ROUTER h;
    PTBT_U4_ROUTER d;
    ULONG gen;
    ULONG hl;
    ULONG dl;
    ULONG h18;
    ULONG d18;
    ULONG hv;
    ULONG dv;
    LONG rc;

    h = &cm->HostRouter;
    d = &p->Dev;
    gen = p->Generation;
    if (tbtU4LaneRd(cm, h, p->Lane0, &hl) != TBT_OK ||
        tbtU4LaneRd(cm, d, d->UpAdp, &dl) != TBT_OK ||
        TBT_LANE1_STATE(hl) != TBT_LANE_STATE_CL0 ||
        TBT_LANE1_STATE(dl) != TBT_LANE_STATE_CL0 ||
        TBT_LANE1_SPEED(hl) != TBT_LANE_SPEED_GEN4 ||
        tbtU4Rd(cm, &h->Route, TBT_CS_ADAPTER, p->Lane0,
                h->Adp[p->Lane0].CapPort + TBT_PORT_CS_18, &h18) != TBT_OK ||
        tbtU4Rd(cm, &d->Route, TBT_CS_ADAPTER, d->UpAdp,
                d->Adp[d->UpAdp].CapPort + TBT_PORT_CS_18, &d18) != TBT_OK ||
        ((h18 | d18) & TBT_PORT18_TIP) != 0 ||
        tbtU4Rd(cm, &h->Route, TBT_CS_ADAPTER, p->Lane0,
                h->Adp[p->Lane0].CapPort + TBT_PORT_CS_19, &hv) != TBT_OK ||
        tbtU4Rd(cm, &d->Route, TBT_CS_ADAPTER, d->UpAdp,
                d->Adp[d->UpAdp].CapPort + TBT_PORT_CS_19, &dv) != TBT_OK ||
        ((hv | dv) & TBT_PORT19_SAF) != 0) {
        return;
    }
    if (!TBT_U4_LIVE(p, gen)) {
        p->CheckPending = TRUE;
        return;
    }
    /* Part 3: each end owned from its write on. */
    p->ElrDev = TRUE;
    rc = tbtU4ElrEnd(cm, d, d->UpAdp, TRUE);
    if (rc == TBT_OK && TBT_U4_LIVE(p, gen)) {
        /* Owed to section 7.3 step 5 from the write on, whatever the
         * read-back says. */
        p->ElrHost = TRUE;
        rc = tbtU4ElrEnd(cm, h, p->Lane0, TRUE);
        if (rc == TBT_OK && TBT_U4_LIVE(p, gen)) {
            p->ElrSet = TRUE;
            cm->Counters.ElrEnabled++;
            tbtU4Log(cm, "u4.elr.set", p->Index);
            return;
        }
    }
    if (p->Generation != gen) {
        return;
    }
    p->ElrRefused = TRUE;
    cm->Counters.ElrRefused++;
    tbtU4Log(cm, "u4.elr.refused", (p->Index << 16) | ((ULONG)(-rc) & 0xFFFFUL));
    /* Both ends written 0 again; what does not clear stays owned and
     * the tick's reconciliation retries it (part 3). */
    tbtU4ElrOff(cm, p, TRUE);
    if (!TBT_U4_LIVE(p, gen)) {
        p->CheckPending = TRUE;
    }
}

/* Part 6: the ends of a link where this driver may have set the bit
 * written 0, device end first (`dev`: only where its router is
 * identified, part 3), before this instance stops or suspends and where
 * part 3 finds it no longer admitted; the bit then counts as not set. */
static VOID tbtU4ElrOff(PTBT_U4CM cm, PTBT_U4_PORT p, BOOLEAN dev)
{
    PTBT_U4_ROUTER d;

    d = &p->Dev;
    if (dev && p->ElrDev && d->Present) {
        if (tbtU4ElrEnd(cm, d, d->UpAdp, FALSE) == TBT_OK) {
            p->ElrDev = FALSE;
            cm->Counters.ElrOwnCleared++;
        } else {
            cm->Counters.ElrClearFailed++;
            tbtU4Log(cm, "u4.elr.clear.failed", (p->Index << 8) | 1UL);
        }
    }
    p->ElrSet = FALSE;
    p->ElrCheckOwed = FALSE;
    if (p->ElrHost) {
        if (tbtU4ElrEnd(cm, &cm->HostRouter, p->Lane0, FALSE) == TBT_OK) {
            p->ElrHost = FALSE;
            cm->Counters.ElrOwnCleared++;
        } else {
            cm->Counters.ElrClearFailed++;
            tbtU4Log(cm, "u4.elr.clear.failed", p->Index << 8);
        }
    }
}

static VOID tbtU4ElrAllOff(PTBT_U4CM cm)
{
    ULONG i;

    for (i = 0; i < cm->PortCount; i++) {
        if (cm->Ports[i].ElrSet || cm->Ports[i].ElrDev || cm->Ports[i].ElrHost) {
            cm->ActivePort = cm->Ports[i].Index;
            tbtU4ElrOff(cm, &cm->Ports[i], (BOOLEAN)(cm->Ports[i].State != TBT_U4_HELD));
            cm->ActivePort = 0;
        }
    }
}

/* One tunnel's part of the recovery check: 1 whole and the same, 0 not,
 * -1 a register unread. */
static int tbtU4ElrTunnel(PTBT_U4CM cm, PTBT_U4_PORT p, BOOLEAN usb3)
{
    PTBT_U4_ROUTER h;
    PTBT_U4_ROUTER d;
    const TBT_U4_TUNNEL *t;
    ULONG hp;
    ULONG dp;
    ULONG pe;
    ULONG v;
    ULONG e[2];

    /* Section 9.2's six parts read at the tunnel's recorded lane HopIDs
     * alone, the host's first (no HopID scan: at most six requests), each
     * answered at once: a part read and wrong is a mismatch, a request
     * unanswered leaves the rest unread. */
    h = &cm->HostRouter;
    d = &p->Dev;
    t = usb3 ? &p->Usb3 : &p->Pcie;
    hp = usb3 ? p->Usb3Down : p->PcieDown;
    pe = usb3 ? TBT_USB3_0_PE : TBT_PCIE0_PE;
    dp = tbtU4FirstOf(d, usb3 ? TBT_ADP_TYPE_USB3_UP : TBT_ADP_TYPE_PCIE_UP);
    if (hp == 0 || h->Adp[hp].CapAdp == 0 || dp == 0 || d->Adp[dp].CapAdp == 0 ||
        t->Hd == 0 || t->Hu == 0) {
        return 0;
    }
    if (tbtU4Rd(cm, &h->Route, TBT_CS_ADAPTER, hp, h->Adp[hp].CapAdp, &v) != TBT_OK) {
        return -1;
    }
    if ((v & pe) == 0) {
        return 0;
    }
    if (tbtU4Entry(cm, &h->Route, hp, TBT_HOP_PROTOCOL, e) != TBT_OK) {
        return -1;
    }
    if (!tbtU4EntryGoes(e, p->Lane0, t->Hd)) {
        return 0;
    }
    if (tbtU4Entry(cm, &h->Route, p->Lane0, t->Hu, e) != TBT_OK) {
        return -1;
    }
    if (!tbtU4EntryGoes(e, hp, TBT_HOP_PROTOCOL)) {
        return 0;
    }
    if (tbtU4Entry(cm, &d->Route, d->UpAdp, t->Hd, e) != TBT_OK) {
        return -1;
    }
    if (!tbtU4EntryGoes(e, dp, TBT_HOP_PROTOCOL)) {
        return 0;
    }
    if (tbtU4Entry(cm, &d->Route, dp, TBT_HOP_PROTOCOL, e) != TBT_OK) {
        return -1;
    }
    if (!tbtU4EntryGoes(e, d->UpAdp, t->Hu)) {
        return 0;
    }
    if (tbtU4Rd(cm, &d->Route, TBT_CS_ADAPTER, dp, d->Adp[dp].CapAdp, &v) != TBT_OK) {
        return -1;
    }
    return (v & pe) != 0 ? 1 : 0;
}

/*
 * Part 4: the recovery check after a LINK_RECOVERY, reads only: the link
 * logged; each tunnel up on the port read whole at its lane HopIDs; a
 * USB3 tunnel's Port Link State and the TMU as left logged. A PCIe tunnel
 * not whole is section 7.1 item 4's lost one; a USB3 one is torn down
 * alone. A register unread leaves the check owed for 2 s from its first
 * try, then it is given up with nothing torn down. The port's own
 * sequence (ActivePort set by the caller).
 */
static VOID tbtU4ElrCheck(PTBT_U4CM cm, PTBT_U4_PORT p)
{
    PTBT_U4_ROUTER h;
    ULONG gen;
    ULONG seq;
    ULONG v;
    ULONG pls;
    int pcie;
    int usb3;
    int op;

    h = &cm->HostRouter;
    gen = p->Generation;
    seq = p->ElrNoteSeq;
    if (!p->ElrCheckTried) {
        p->ElrCheckTried = TRUE;
        p->ElrCheckSinceUs = tbtU4Now(cm);
    }
    if (tbtU4LaneRd(cm, h, p->Lane0, &v) == TBT_OK) {
        tbtU4Log(cm, "u4.elr.link",
                 (p->Index << 16) | (TBT_LANE1_SPEED(v) << 8) | TBT_LANE1_NLW(v));
    }
    pcie = 1;
    if (p->Pcie.Up) {
        pcie = tbtU4ElrTunnel(cm, p, FALSE);
    }
    usb3 = 1;
    op = 1;
    pls = 0;
    if (p->Usb3.Up && TBT_U4_LIVE(p, gen)) {
        usb3 = tbtU4ElrTunnel(cm, p, TRUE);
        /* Part 4.3: operational with USB3 Link Valid and U0, U2 or U3;
         * anything else watched for 2 s, the USB3 link's own to recover. */
        op = -1;
        if (p->Usb3Down != 0 && h->Adp[p->Usb3Down].CapAdp != 0 &&
            tbtU4Rd(cm, &h->Route, TBT_CS_ADAPTER, p->Usb3Down,
                    h->Adp[p->Usb3Down].CapAdp + TBT_USB3_CS_4, &v) == TBT_OK) {
            pls = ((v & TBT_USB3_4_ULV) != 0 ? 0x100UL : 0) | TBT_USB3_4_PLS(v);
            tbtU4Log(cm, "u4.elr.usb3", (p->Index << 16) | pls);
            op = (pls == 0x100UL || pls == 0x102UL || pls == 0x103UL) ? 1 : 0;
        }
    }
    if (!TBT_U4_LIVE(p, gen)) {
        /* Removal evidence, or a new connection: the check's own. */
        p->CheckPending = TRUE;
        return;
    }
    if (p->TmuConfigured) {
        tbtU4Log(cm, "u4.elr.tmu", (p->Index << 8) | (tbtU4TmuAsLeft(cm, p) ? 1UL : 0));
    }
    if (pcie == 0 && p->Pcie.Up) {
        cm->Counters.ElrPcieLost++;
        tbtU4PcieLost(cm, p, 0xFFUL);
    }
    if (usb3 == 0 && p->Usb3.Up && TBT_U4_LIVE(p, gen)) {
        cm->Counters.ElrUsb3Lost++;
        tbtU4Log(cm, "u4.elr.usb3.lost", p->Index);
        (void)tbtU4Usb3Down(cm, p);
        p->Usb3.Up = FALSE;
        p->Usb3Failed = TRUE;
        cm->Counters.Usb3TornDown++;
    }
    if (!p->Usb3.Up) {
        op = 1;
    }
    if (pcie < 0 || usb3 < 0 || op != 1) {
        if (tbtU4Now(cm) - p->ElrCheckSinceUs < TBT_U4_TRANSITIONAL_US) {
            return;
        }
        if (pcie < 0 || usb3 < 0 || op < 0) {
            cm->Counters.ElrUnread++;
            tbtU4Log(cm, "u4.elr.unread", p->Index);
        } else {
            cm->Counters.ElrUsb3Down++;
            tbtU4Log(cm, "u4.elr.usb3.down", (p->Index << 16) | pls);
        }
    } else if (pcie == 1 && usb3 == 1) {
        cm->Counters.ElrKept++;
        tbtU4Log(cm, "u4.elr.ok", p->Index);
    }
    cm->Counters.ElrChecks++;
    p->ElrCheckTried = FALSE;
    if (p->ElrNoteSeq == seq) {
        p->ElrCheckOwed = FALSE;
    }
    /* Else a LINK_RECOVERY drained during this check (a loss sequence's
     * safe point): the check stays owed, its 2 s anew, for that one. */
    if (!TBT_U4_LIVE(p, gen)) {
        p->CheckPending = TRUE;
    }
}

/* The tick's part, after the port's check and its kids' service. */
static VOID tbtU4ElrTick(PTBT_U4CM cm, PTBT_U4_PORT p)
{
    if (p->ElrCheckOwed) {
        if (!(p->State == TBT_U4_UP || p->State == TBT_U4_FAILED) || !p->Dev.Present) {
            p->ElrCheckOwed = FALSE;
            p->ElrCheckTried = FALSE;
        } else if (tbtU4ElrIdle(cm, p)) {
            cm->ActivePort = p->Index;
            tbtU4ElrCheck(cm, p);
            cm->ActivePort = 0;
        }
    }
    if (p->ElrSet || p->ElrCheckOwed) {
        return;
    }
    if (!p->ElrRefused && tbtU4ElrIdle(cm, p) && tbtU4ElrWanted(cm, p)) {
        cm->ActivePort = p->Index;
        tbtU4ElrSet(cm, p);
        cm->ActivePort = 0;
    } else if (p->State == TBT_U4_HELD) {
        /* Part 3: a HELD port's host end alone, whatever its router's
         * classification (a write to the host router only), retried at
         * each tick while it fails; the device end's ownership kept. */
        if (p->ElrHost && !p->RecDeparted && !cm->DeferredTail[p->Index - 1]) {
            cm->ActivePort = p->Index;
            tbtU4ElrOff(cm, p, FALSE);
            cm->ActivePort = 0;
        }
    } else if ((p->ElrDev || p->ElrHost) && p->Dev.Present &&
               (p->State == TBT_U4_UP || p->State == TBT_U4_FAILED) &&
               !p->Barred && !p->RecDeparted && !p->UnplugPending && !p->CheckPending &&
               !p->ClassifyPending && !cm->DeferredTail[p->Index - 1]) {
        /* Part 3: owned but not set, and not admitted (or refused): both
         * ends written 0, its router identified, retried at each tick
         * while one fails. */
        cm->ActivePort = p->Index;
        tbtU4ElrOff(cm, p, TRUE);
        cm->ActivePort = 0;
    }
}

VOID TbtU4Poll(PTBT_U4CM cm)
{
    ULONG i;
    PTBT_U4_PORT p;
    BOOLEAN rebuilt;
    BOOLEAN ob[2];
    BOOLEAN debt;

    if (cm->Failed || cm->Stage != TBT_U4_STAGE_RUNNING || cm->Suspended ||
        cm->Contained) {
        return;
    }
    rebuilt = FALSE;
    TbtCtlService(&cm->Ctl);
    tbtU4TakeQueues(cm);
    tbtU4XdTake(cm);
    tbtU4XdEarlyRetry(cm);
    if (cm->HostTmuTdOwed || !cm->HostTmuOk) {
        tbtU4HostTmu(cm);
    }
    /* Record 22 section 6.2.2 item 3: a Set a recovery found owed and,
     * reading only, did not send. */
    if (cm->HostRouter.SeqOwed) {
        tbtU4GetCapabilities(cm, &cm->HostRouter);
    }
    for (i = 0; i < cm->PortCount; i++) {
        p = &cm->Ports[i];
        if (p->Dev.Present && p->Dev.SeqOwed && !p->Dev.Tbt3 &&
            (p->State == TBT_U4_UP || p->State == TBT_U4_HELD ||
             p->State == TBT_U4_FAILED) &&
            !p->UnplugPending && !p->CheckPending && !p->ClassifyPending &&
            !cm->DeferredTail[i]) {
            /* The port's own sequence (section 7.4): no nested teardown of
             * it at a safe point, and the Set withheld once a check shows
             * removal evidence; the next check acts on it. */
            cm->ActivePort = p->Index;
            cm->SeqGuardPort = p->Index;
            cm->SeqGuardGen = p->Generation;
            tbtU4GetCapabilities(cm, &p->Dev);
            cm->SeqGuardPort = 0;
            cm->ActivePort = 0;
            if (!tbtU4Live(cm, p, cm->SeqGuardGen)) {
                p->CheckPending = TRUE;
            }
        }
    }
    /* Steps 4 to 6 owed by teardowns nested at a safe point. */
    for (i = 0; i < cm->PortCount; i++) {
        if (cm->DeferredTail[i] && !cm->Ports[i].RecDeparted &&
            tbtU4RecCleanupGate(cm, &cm->Ports[i])) {
            /* A departed port's tail waits for its recorded drain, and is
             * made only on a link read disconnected right now
             * (tbtU4RecCleanupGate). */
            cm->DeferredTail[i] = 0;
            tbtU4DropLinks(cm, &cm->Ports[i]);
            tbtU4TeardownTail(cm, &cm->Ports[i]);
        }
    }
    tbtU4RetryQuarantine(cm);
    for (i = 0; i < cm->PortCount; i++) {
        p = &cm->Ports[i];
        tbtU4XdPre(cm, p);
        if (p->RecDeparted) {
            /* Section 7.7 step 5: a departed router's recorded host-side
             * drain, owed - retried alone at each tick (its removal debt
             * was taken at the commit), nothing else made on the port. */
            ob[0] = p->RecDrainPcie;
            ob[1] = p->RecDrainUsb3;
            debt = p->RemovalDebt;
            (VOID)tbtU4Departed(cm, p, ob);
            p->RemovalDebt = debt;
            continue;
        }
        if (p->DiscoverOwed && !p->Barred) {
            /* Section 7.7 part 3 step 4: a router found on the link of a
             * departed one during a recovery, discovered after the commit
             * as section 9.2 discovers a router found at start. */
            p->DiscoverOwed = FALSE;
            if (p->State == TBT_U4_EMPTY) {
                tbtU4DiscoverPort(cm, p);
            }
        }
        if (p->AsFoundOwed && !p->Barred) {
            /* TBT_U4_CONFIG.DeferFirstPoll: the discovery's connect on the
             * link as found, made here instead of in the start (or after
             * a recovery's commit, once the port's barrier is clear). */
            p->AsFoundOwed = FALSE;
            tbtU4Connect(cm, p, TRUE);
            if (p->Dev.Present) {
                tbtU4DiscoverLinks(cm, p);
            }
        }
        if (p->TmuQuarantine && p->State != TBT_U4_EMPTY && p->Dev.Present &&
            p->TmuConfigured) {
            if (tbtU4TmuDisable(cm, p) == TBT_OK) {
                p->TmuQuarantine = FALSE;
            }
        } else if (p->TmuQuarantine && !p->Dev.Present &&
                   tbtU4RecCleanupGate(cm, p)) {
            if ((!tbtU4IsV2(&cm->HostRouter) ||
                 tbtU4TmuEnhancedOff(cm, &cm->HostRouter, p->Lane0) == TBT_OK) &&
                tbtU4TmuAdp(cm, &cm->HostRouter, p->Lane0, TBT_TMU_ADP_CS_6, 0,
                            TBT_TMU_ADP6_DTS) == TBT_OK) {
                p->TmuQuarantine = FALSE;
            }
        }
        if (p->BwQuarantine && p->Usb3Down != 0 && tbtU4BwRequest(cm, p, FALSE) == TBT_OK) {
            p->BwQuarantine = FALSE;
        }
        if (p->BwReleaseOwed && p->Usb3Down != 0 && !p->Usb3.Up && !p->BwQuarantine) {
            (void)tbtU4ReleaseBw(cm, p);
        }
        if (p->State == TBT_U4_EMPTY &&
            (p->PcSetHost || p->LaneWritten || p->Lane1Disabled || p->LcDprOwed ||
             p->ElrHost) &&
            tbtU4RecCleanupGate(cm, p)) {
            (void)tbtU4HostRestore(cm, p);
        }
        if (p->State == TBT_U4_EMPTY && p->HostResidual) {
            (void)tbtU4ResidualClear(cm, p);
        }
        if (p->HostCloseOwed && p->State == TBT_U4_HELD && tbtU4ResidualClear(cm, p)) {
            /* A resume's host closure, interrupted, finished here: the
             * tunnels are down; the hold itself stays. */
            p->HostCloseOwed = FALSE;
            p->Usb3.Up = FALSE;
            p->Pcie.Up = FALSE;
            if (p->BwReleaseOwed && p->Usb3Down != 0 && !p->BwQuarantine) {
                (void)tbtU4ReleaseBw(cm, p);
            }
        }
        tbtU4DromLate(cm, p);
        tbtU4Check(cm, p);
        tbtU4XdService(cm, p);
        /* Record 34 Part 3: the kids behind it, after its own check. */
        tbtU4KidService(cm, p);
        if ((p->RebuildPcie || p->RebuildUsb3) && !p->Barred && !rebuilt) {
            /* Section 7.7 part 4: one rebuild a tick, so that each
             * unattributed one is learned before the next (U29). */
            rebuilt = tbtU4Rebuild(cm, p);
        }
        /* Section 6.2.3 item 4: the recovery check, then the set. */
        tbtU4ElrTick(cm, p);
    }
    tbtU4CheckLinks(cm);
    if (cm->ResyncPending && cm->ActionCount == 0) {
        cm->ResyncPending = FALSE;
        tbtU4Queue(cm, TBT_U4_ACT_RESYNC, NULL);
    }
}

/*
 * The ring interrupt's wake (record 22 section 5.4): the RX ring drained
 * and its queues taken as the tick takes them, then the check of each
 * port this drain's own events asked for - and of no other: a port whose
 * check was already owed, or one the tick serves with something else first
 * (its recorded drain, a deferred tail, a discovery owed, the barrier), is
 * left for the tick, so the responses to a check can never ask for
 * another one and the wakes end. No port scan, quarantine retry, rebuild
 * or link scan: those stay the tick's.
 */
VOID TbtU4RingWake(PTBT_U4CM cm)
{
    ULONG i;
    ULONG owed;
    PTBT_U4_PORT p;

    if (cm->Failed || cm->Stage != TBT_U4_STAGE_RUNNING || cm->Suspended ||
        cm->Contained) {
        return;
    }
    owed = 0;
    for (i = 0; i < cm->PortCount && i < 32; i++) {
        if (cm->Ports[i].CheckPending) {
            owed |= 1UL << i;
        }
    }
    TbtCtlService(&cm->Ctl);
    tbtU4TakeQueues(cm);
    tbtU4XdTake(cm);
    for (i = 0; i < cm->PortCount && i < 32; i++) {
        p = &cm->Ports[i];
        if (!p->CheckPending || (owed & (1UL << i)) != 0 || p->RecDeparted ||
            p->DiscoverOwed || p->AsFoundOwed || p->Barred ||
            cm->DeferredTail[i]) {
            continue;
        }
        tbtU4Check(cm, p);
    }
}

BOOLEAN TbtU4NextAction(PTBT_U4CM cm, PTBT_U4_ACTION out)
{
    if (cm->ActionCount == 0 || cm->Contained) {
        return FALSE;
    }
    *out = cm->Actions[cm->ActionHead];
    cm->ActionHead = (cm->ActionHead + 1) % TBT_U4_ACTION_QUEUE;
    cm->ActionCount--;
    return TRUE;
}

VOID TbtU4PresenceSample(PTBT_U4CM cm, ULONG port, ULONG generation,
                         ULONG issuedUs, BOOLEAN matched)
{
    PTBT_U4_PORT p;

    p = tbtU4PortOf(cm, port);
    if (p == NULL || !matched || generation != p->Generation ||
        p->State == TBT_U4_EMPTY || cm->Contained) {
        return;
    }
    if (!p->AnchorValid || p->AnchorGeneration != generation ||
        (LONG)(issuedUs - p->AnchorUs) > 0) {
        p->AnchorValid = TRUE;
        p->AnchorUs = issuedUs;
        p->AnchorGeneration = generation;
    }
}

/*
 * The ejects, the cleaning and the PCIe loss are the port's own sequence
 * (ActivePort), as a connect is: a safe point reached in one of their waits
 * never nests this port's teardown under it (section 7.4), which would
 * leave the outer sequence writing FAILED over a head whose tail is owed.
 * An unplug seen meanwhile stays pending for tbtU4Check.
 */
LONG TbtU4Eject(PTBT_U4CM cm, ULONG port)
{
    PTBT_U4_PORT p;
    BOOLEAN closed;
    ULONG prev;

    if (cm->Contained) {
        cm->Counters.ContainedRefused++;
        return TBT_E_STOPPED;
    }
    p = tbtU4PortOf(cm, port);
    if (p == NULL || cm->Failed || !p->Dev.Present) {
        return TBT_E_PARAM;
    }
    cm->Counters.Ejects++;
    prev = cm->ActivePort;
    cm->ActivePort = p->Index;
    /* Record 34 section 6.7: a kid's tunnel first, then the parent's. */
    closed = tbtU4KidsDown(cm, p, TBT_U4_WHY_EJECTED);
    if (p->Usb3.Up || p->Usb3.Written != 0) {
        p->Usb3.Written |= TBT_U4_E_HOST_ENABLE | TBT_U4_E_DEV_ENABLE;
        closed &= tbtU4Usb3Down(cm, p);
    }
    if (p->Pcie.Up || p->Pcie.Written != 0) {
        p->Pcie.Written |= TBT_U4_E_HOST_ENABLE | TBT_U4_E_DEV_ENABLE;
        closed &= tbtU4TunnelDown(cm, p, &p->Pcie, FALSE);
        cm->Counters.PcieTornDown++;
        tbtU4Queue(cm, TBT_U4_ACT_TUNNEL_DOWN, p);
    }
    cm->ActivePort = prev;
    p->Ejected = TRUE;
    p->PcieFailed = TRUE;
    p->State = TBT_U4_FAILED;
    p->Why = TBT_U4_WHY_EJECTED;
    return closed ? TBT_OK : TBT_E_STATE;
}

LONG TbtU4EjectPcie(PTBT_U4CM cm, ULONG port)
{
    PTBT_U4_PORT p;
    BOOLEAN closed;
    ULONG prev;

    if (cm->Contained) {
        cm->Counters.ContainedRefused++;
        return TBT_E_STOPPED;
    }
    p = tbtU4PortOf(cm, port);
    if (p == NULL || cm->Failed || !p->Dev.Present) {
        return TBT_E_PARAM;
    }
    cm->Counters.Ejects++;
    prev = cm->ActivePort;
    cm->ActivePort = p->Index;
    /* A kid's tunnel rides on this one: down first (record 34 section 6.7). */
    closed = tbtU4KidsDown(cm, p, TBT_U4_WHY_EJECTED);
    if (p->Pcie.Up || p->Pcie.Written != 0) {
        p->Pcie.Written |= TBT_U4_E_HOST_ENABLE | TBT_U4_E_DEV_ENABLE;
        closed &= tbtU4TunnelDown(cm, p, &p->Pcie, FALSE);
        cm->Counters.PcieTornDown++;
        tbtU4Queue(cm, TBT_U4_ACT_TUNNEL_DOWN, p);
    }
    cm->ActivePort = prev;
    /* The USB3 tunnel stays, as on a port FAILED for PCIe alone (record 22
     * section 7.2): its devices are the host xHCI's, never asked to stop. */
    p->Ejected = TRUE;
    p->PcieFailed = TRUE;
    p->State = TBT_U4_FAILED;
    p->Why = TBT_U4_WHY_EJECTED;
    return closed ? TBT_OK : TBT_E_STATE;
}

VOID TbtU4Suspend(PTBT_U4CM cm)
{
    ULONG i;
    ULONG k;
    PTBT_U4_PORT p;
    PTBT_U4_OBLIGATION o;
    PTBT_U4_KID kd;

    if (cm->Contained) {
        /* Section 7.7: the obligations stay as recorded; nothing written. */
        cm->Counters.ContainedRefused++;
        cm->Suspended = TRUE;
        return;
    }
    /* Record 35 section 3.8: every peer ends, a Logout first, before the
     * domain sleeps (IDS 5.1.2). */
    tbtU4XdEndAll(cm, FALSE);
    tbtU4Zero(cm->Obligations, sizeof(cm->Obligations));
    k = 0;
    for (i = 0; i < cm->PortCount; i++) {
        p = &cm->Ports[i];
        if (p->Pcie.Up && k < TBT_U4_OBLIGATIONS) {
            o = &cm->Obligations[k++];
            o->Used = TRUE;
            o->Port = p->Index;
            o->Generation = p->Generation;
            o->Usb3 = FALSE;
            o->BuiltHere = (BOOLEAN)(!p->Pcie.Preserved || p->Pcie.Own);
            o->Hd = p->Pcie.Hd;
            o->Hu = p->Pcie.Hu;
        }
        if (p->Usb3.Up && k < TBT_U4_OBLIGATIONS) {
            o = &cm->Obligations[k++];
            o->Used = TRUE;
            o->Port = p->Index;
            o->Generation = p->Generation;
            o->Usb3 = TRUE;
            o->BuiltHere = (BOOLEAN)(!p->Usb3.Preserved || p->Usb3.Own);
            o->Hd = p->Usb3.Hd;
            o->Hu = p->Usb3.Hu;
        }
    }
    /* Record 34 section 6.9: a kid's PCIe tunnel up is an obligation of
     * its own, judged after every port's. */
    for (i = 0; i < TBT_U4_MAX_KIDS; i++) {
        kd = &cm->Kids[i];
        if (kd->Used && kd->Pcie.Up && k < TBT_U4_OBLIGATIONS) {
            o = &cm->Obligations[k++];
            o->Used = TRUE;
            o->Port = kd->Port;
            o->Kid = kd->Index;
            o->Generation = kd->Generation;
            o->Usb3 = FALSE;
            o->BuiltHere = (BOOLEAN)(!kd->Pcie.Preserved || kd->Pcie.Own);
            o->Hd = kd->Pcie.Hd;
            o->Hu = kd->Pcie.Hu;
        }
    }
    /* Section 6.2.3 item 4 part 6 (Guide 8.2.1): the bit cleared before
     * the sleep, set again by the tick after the resume. */
    if (cm->Stage == TBT_U4_STAGE_RUNNING && !cm->Failed) {
        tbtU4ElrAllOff(cm);
    }
    TbtRingStop(cm->Ring);
    cm->Suspended = TRUE;
}

static VOID tbtU4PcieLoss(PTBT_U4CM cm, PTBT_U4_PORT p, BOOLEAN watch);

/* The host's side of a port's tunnels closed and proven closed from the
 * registers (tbtU4HostSweep: Path Enable read back clear, every entry of
 * the port's protocol adapters and the lane's entries leading to them
 * invalid and drained, pending ones included; the PCIe adapter in
 * Detect), with section 7.3's quarantine where a step fails; the USB3
 * teardown's 500 ms gap started and its bandwidth released, or kept owned
 * (BwQuarantine) when that fails. TRUE when everything was proven. */
static BOOLEAN tbtU4HostClose(PTBT_U4CM cm, PTBT_U4_PORT p)
{
    PTBT_U4_ROUTER h;
    BOOLEAN ok;

    h = &cm->HostRouter;
    p->HostCloseOwed = TRUE;
    ok = tbtU4HostSweep(cm, p, TRUE, TRUE);
    if (!ok) {
        p->HostResidual = TRUE;     /* retried at the idle tick, HELD or not */
    } else {
        /* Proven closed on the host: no tunnel is up whatever else fails
         * below, so an owed release may be retried. */
        p->HostCloseOwed = FALSE;
        p->Usb3.Up = FALSE;
        p->Pcie.Up = FALSE;
    }
    if (ok && p->PcieDown != 0 && h->Adp[p->PcieDown].CapAdp != 0) {
        ok = tbtU4WaitDetect(cm, NULL, 0, h, p->PcieDown, TBT_U4_DETECT_US);
    }
    if (p->Usb3Down != 0 && p->Usb3.Written != 0) {
        p->LastUsb3TeardownUs = tbtU4Now(cm);
        p->Usb3GapValid = TRUE;
    }
    if ((p->Usb3.Written & TBT_U4_E_BANDWIDTH) != 0 && !tbtU4ReleaseBw(cm, p)) {
        ok = FALSE;     /* owed (BwReleaseOwed), retried at the idle tick */
    }
    return ok;
}

/* What a port's router is after the sleep (section 7.6). */
#define TBT_U4_RES_REMOVED      1UL     /* CLd, ERR_CONN, another UUID */
#define TBT_U4_RES_LOST         2UL     /* the same router, uninitialised */
#define TBT_U4_RES_SAME         3UL     /* the same router, still enumerated */
#define TBT_U4_RES_UNKNOWN      4UL

/*
 * The router behind a port after the sleep. Removal evidence is section
 * 7.1's (CLd, ERR_CONN, another identity, lane 0's Lock set again). A
 * router that lost power with the domain reads TopologyID Valid clear
 * under its own UUID: that is a lost configuration, not removal, and it
 * explains a Lock set again too. Lane 0's Lock is cleared so that the
 * router can be read at all (section 6.4 step 2, lane 0 only), after the
 * observation is kept on the port (RelockSeen): with the router still
 * configured it is a removal, and a classification interrupted after the
 * clear still acts on it (tbtU4Evidence).
 */
static ULONG tbtU4ResumeClass(PTBT_U4CM cm, PTBT_U4_PORT p)
{
    PTBT_U4_ROUTER h;
    PTBT_U4_ROUTER d;
    ULONG lane;
    ULONG cs4;
    ULONG v[9];
    ULONG u[4];
    ULONG a;
    ULONG cls;
    LONG rc;
    TBT_WAIT w;

    h = &cm->HostRouter;
    d = &p->Dev;
    TbtWaitStart(&w, cm->Host, TBT_U4_CONNECT_US, FALSE);
    for (;;) {
        if (tbtU4LaneRd(cm, h, p->Lane0, &lane) != TBT_OK) {
            return TBT_U4_RES_UNKNOWN;
        }
        cls = TbtU4LaneClass(TBT_LANE1_STATE(lane));
        if (cls == TBT_U4_LANE_DISCONNECTED) {
            return TBT_U4_RES_REMOVED;
        }
        if (cls == TBT_U4_LANE_CONNECTED) {
            break;
        }
        if (cls != TBT_U4_LANE_TRANSITIONAL || TbtWaitDone(&w, cm->Host)) {
            return TBT_U4_RES_UNKNOWN;
        }
        tbtU4Pause(cm, &w);
    }
    /* Lane 0 only (Spec 8.2.2.1: Lock in a lane 1 adapter is not to be
     * written). Seen set, it is kept on the port before it is cleared, so
     * that a classification interrupted after this point never loses it. */
    a = p->Lane0;
    if (tbtU4Rd(cm, &h->Route, TBT_CS_ADAPTER, a, TBT_ADP_CS_4, &cs4) != TBT_OK) {
        return TBT_U4_RES_UNKNOWN;
    }
    if ((cs4 & TBT_ADP4_LOCK) != 0) {
        p->RelockSeen = TRUE;
        if (tbtU4Wr(cm, &h->Route, TBT_CS_ADAPTER, a, TBT_ADP_CS_4,
                    cs4 & ~TBT_ADP4_LOCK) != TBT_OK ||
            tbtU4Rd(cm, &h->Route, TBT_CS_ADAPTER, a, TBT_ADP_CS_4, &cs4) != TBT_OK ||
            (cs4 & TBT_ADP4_LOCK) != 0) {
            return TBT_U4_RES_UNKNOWN;
        }
    }
    p->Unlocked = TRUE;
    rc = tbtU4RdN(cm, &d->Route, TBT_CS_ROUTER, 0, TBT_ROUTER_CS_0, 9, v);
    if (tbtU4Gone(cm, rc)) {
        return TBT_U4_RES_REMOVED;
    }
    if (rc != TBT_OK) {
        return TBT_U4_RES_UNKNOWN;
    }
    if (TBT_RTR0_VENDOR(v[0]) != d->Vendor || TBT_RTR0_DEVICE(v[0]) != d->Device) {
        return TBT_U4_RES_REMOVED;
    }
    if (!d->UuidValid) {
        return TBT_U4_RES_UNKNOWN;
    }
    if (!d->Tbt3) {
        if (v[7] != d->Uuid[0] || v[8] != d->Uuid[1]) {
            return TBT_U4_RES_REMOVED;
        }
    } else {
        /* The identity's recorded source (record 22 section 19.4 item
         * 3a); one that cannot be confirmed leaves the port held. */
        rc = tbtU4TbtUuid(cm, d, u);
        if (rc != TBT_OK) {
            return tbtU4Gone(cm, rc) ? TBT_U4_RES_REMOVED : TBT_U4_RES_UNKNOWN;
        }
        if (u[0] != d->Uuid[0] || u[1] != d->Uuid[1] || u[2] != d->Uuid[2] ||
            u[3] != d->Uuid[3]) {
            return TBT_U4_RES_REMOVED;
        }
    }
    if ((v[3] & TBT_RTR3_V) == 0) {
        p->RelockSeen = FALSE;  /* the lost configuration explains it */
        return TBT_U4_RES_LOST;
    }
    if (p->RelockSeen && cm->RelockExplained) {
        /* Section 7.7: the recovery found the host router reset; its
         * lane adapters lock again on their own. */
        p->RelockSeen = FALSE;
    }
    if (p->RelockSeen) {
        /* Lock set again across the sleep is section 7.1's removal
         * evidence: with the router still configured it is not the lost
         * configuration above, so the connection is discharged as removed
         * (torn down, the OS told), never kept as unchanged. */
        return TBT_U4_RES_REMOVED;
    }
    return tbtU4Enumerated(d, v) ? TBT_U4_RES_SAME : TBT_U4_RES_UNKNOWN;
}

/*
 * Inside a resume verification, on a USB4 router: the hot plug events a
 * safe point consumes (every wait of the resume has them, this one's
 * Router Ready wait and the PCIe loss's 2 s alike) are not acted on before
 * the obligation is judged (ActivePort), so each verification first
 * proves the router the one tbtU4ResumeClass classified - an unplug hint
 * only with no removal evidence behind it (tbtU4Live), lane 0 still
 * unlocked, the same IDs and UUID, still enumerated at its route - and
 * returns its ROUTER_CS_5 and 6 in cs56. Reads only, but for the hint's
 * own evidence check; anything else is not the same router, and the
 * obligation is not met.
 */
static BOOLEAN tbtU4StillSame(PTBT_U4CM cm, PTBT_U4_PORT p, PULONG cs56)
{
    PTBT_U4_ROUTER d;
    ULONG cs4;
    ULONG v[9];

    d = &p->Dev;
    if (!tbtU4Live(cm, p, p->Generation) || !d->UuidValid ||
        tbtU4Rd(cm, &cm->HostRouter.Route, TBT_CS_ADAPTER, p->Lane0, TBT_ADP_CS_4,
                &cs4) != TBT_OK || (cs4 & TBT_ADP4_LOCK) != 0 ||
        tbtU4RdN(cm, &d->Route, TBT_CS_ROUTER, 0, TBT_ROUTER_CS_0, 9, v) != TBT_OK ||
        TBT_RTR0_VENDOR(v[0]) != d->Vendor || TBT_RTR0_DEVICE(v[0]) != d->Device ||
        v[7] != d->Uuid[0] || v[8] != d->Uuid[1] || !tbtU4Enumerated(d, v)) {
        return FALSE;
    }
    cs56[0] = v[5];
    cs56[1] = v[6];
    return TRUE;
}

/*
 * Section 9.2's whole verification of one tunnel after the sleep: the
 * router's configuration (Router Ready, CV, Configuration Ready, PTO or
 * UTO), the link (bonded, or lane 1 disconnected or disabled), the path
 * walked end to end, and for PCIe a live LTSSM. On success the walked
 * HopIDs are adopted, so that the tunnel's teardown is the registers'.
 */
static BOOLEAN tbtU4Verify(PTBT_U4CM cm, PTBT_U4_PORT p, BOOLEAN usb3)
{
    PTBT_U4_ROUTER h;
    PTBT_U4_ROUTER d;
    ULONG v[2];
    ULONG lane;
    ULONG lane1;
    TBT_U4_TUNNEL t;
    PTBT_U4_TUNNEL dst;

    h = &cm->HostRouter;
    d = &p->Dev;
    if (!d->Tbt3) {
        if (!tbtU4StillSame(cm, p, v)) {
            return FALSE;
        }
        /* Router Ready waited for inside the resume's budget (a spent one
         * refuses the reads), then the router proven the same again. */
        if ((v[1] & TBT_RTR6_RR) == 0 &&
            (tbtU4WaitReady(cm, d) != TBT_OK || !tbtU4StillSame(cm, p, v))) {
            return FALSE;
        }
        if ((v[1] & TBT_RTR6_RR) == 0 || (v[0] & TBT_RTR5_CV) == 0 ||
            (v[1] & TBT_RTR6_CR) == 0 ||
            (v[0] & (usb3 ? TBT_RTR5_UTO : TBT_RTR5_PTO)) == 0) {
            return FALSE;
        }
    }
    if (tbtU4LaneRd(cm, h, p->Lane0, &lane) != TBT_OK ||
        tbtU4LaneRd(cm, h, p->Lane0 + 1, &lane1) != TBT_OK) {
        return FALSE;
    }
    if (!tbtU4WidthBonded(lane) && !tbtU4SingleOk(cm, lane1)) {
        return FALSE;
    }
    if (!tbtU4Follow(cm, p, usb3, &t)) {
        return FALSE;
    }
    if (!usb3 && !tbtU4PcieLive(cm, p)) {
        return FALSE;
    }
    dst = usb3 ? &p->Usb3 : &p->Pcie;
    t.Up = TRUE;
    t.Preserved = dst->Preserved;
    t.Own = dst->Own;
    t.Written |= dst->Written & TBT_U4_E_BANDWIDTH;
    t.CreditsHost = dst->CreditsHost;
    t.CreditsDev = dst->CreditsDev;
    *dst = t;
    return TRUE;
}

/*
 * Resume (sections 7.6, 11): the rings and the host router again, then
 * every obligation - verified by section 9.2's whole reading; rebuilt
 * where the domain lost its configuration and the port was built here;
 * torn down on removal evidence; else HELD and the failure path. The work
 * runs under its own budget on the clock (the host router, then each
 * obligation); what is left when it runs out is HELD, never torn down.
 */
LONG TbtU4Resume(PTBT_U4CM cm)
{
    ULONG i;
    ULONG k;
    ULONG n;
    ULONG oldCount;
    ULONG what;
    BOOLEAN met;
    BOOLEAN allMet;
    BOOLEAN moved;
    PTBT_U4_OBLIGATION o;
    PTBT_U4_PORT p;
    PTBT_U4_PORT saved;
    LONG rc;

    if (cm->Contained) {
        cm->Counters.ContainedRefused++;
        return TBT_E_STOPPED;
    }
    if (cm->Failed) {
        return TBT_E_STATE;
    }
    tbtU4IsoRevokeAll(cm);
    cm->Suspended = FALSE;
    /* Several kilobytes: in the instance, never on the stack (a kernel
     * stack has no probe, __chkstk). */
    saved = cm->ResumeSaved;
    tbtU4Zero(saved, sizeof(cm->ResumeSaved));
    oldCount = cm->PortCount;
    for (i = 0; i < oldCount && i < TBT_U4_MAX_PORTS; i++) {
        saved[i] = cm->Ports[i];
    }
    n = 0;
    for (k = 0; k < TBT_U4_OBLIGATIONS; k++) {
        if (cm->Obligations[k].Used) {
            n++;
        }
    }
    cm->Budgeted = TRUE;
    cm->BudgetSpent = FALSE;
    cm->BudgetStartUs = TbtClockUs(cm->Host);
    cm->BudgetWaitedUs = 0;
    cm->BudgetUs = cm->ResumeHostUs + n * cm->ResumeEachUs;
    rc = tbtU4Rings(cm);
    if (rc != TBT_OK) {
        cm->Budgeted = FALSE;
        cm->Failed = TRUE;
        cm->Disturbed = TRUE;
        return rc;
    }
    cm->BudgetRingBase = cm->Ring->WaitedUs;
    rc = tbtU4HostRouter(cm);
    if (rc != TBT_OK) {
        cm->Budgeted = FALSE;
        return tbtU4Fail(cm, rc);
    }
    /* The port table is rebuilt by the host router's walk; what each port
     * held across the sleep comes back from the saved copy, matched by
     * its lane 0 adapter. The obligations, DeferredTail[], the links and
     * the queued actions name a port by its Index, which the walk
     * renumbers when an adapter is not found again, so the table is
     * checked whole first, as TbtU4Recover checks its snapshot: a saved
     * port missing from the walk, or found under another Index, keeps the
     * table as it was before the sleep, and every obligation is held
     * unjudged rather than judged against another port's record. */
    moved = FALSE;
    for (k = 0; k < oldCount && k < TBT_U4_MAX_PORTS; k++) {
        if (saved[k].Index == 0) {
            continue;
        }
        for (i = 0; i < cm->PortCount; i++) {
            if (cm->Ports[i].Lane0 == saved[k].Lane0) {
                break;
            }
        }
        if (i == cm->PortCount || cm->Ports[i].Index != saved[k].Index) {
            tbtU4Log(cm, "u4.resume.port", saved[k].Index);
            moved = TRUE;
        }
    }
    if (moved) {
        cm->PortCount = oldCount;
        for (k = 0; k < oldCount && k < TBT_U4_MAX_PORTS; k++) {
            cm->Ports[k] = saved[k];
        }
    }
    for (i = 0; i < cm->PortCount && !moved; i++) {
        p = &cm->Ports[i];
        for (k = 0; k < oldCount && k < TBT_U4_MAX_PORTS; k++) {
            if (saved[k].Index != 0 && saved[k].Lane0 == p->Lane0) {
                saved[k].MapValid = p->MapValid;
                saved[k].MapRid = p->MapRid;
                saved[k].HostBaPcie = p->HostBaPcie;
                saved[k].HostBaHi = p->HostBaHi;
                saved[k].HostBaUsb3 = p->HostBaUsb3;
                saved[k].Index = p->Index;
                *p = saved[k];
            }
        }
    }
    cm->Stage = TBT_U4_STAGE_RUNNING;
    tbtU4HostTmu(cm);
    allMet = TRUE;
    for (k = 0; k < TBT_U4_OBLIGATIONS; k++) {
        o = &cm->Obligations[k];
        if (!o->Used || o->Kid != 0) {
            continue;   /* a kid's: after every port's */
        }
        o->Used = FALSE;
        p = tbtU4PortOf(cm, o->Port);
        if (p == NULL) {
            allMet = FALSE;
            continue;
        }
        (void)tbtU4Spent(cm);
        met = FALSE;
        /* The port's own sequence: a safe point inside it never nests
         * this port's teardown (section 7.4). */
        cm->ActivePort = p->Index;
        if (p->State == TBT_U4_EMPTY) {
            met = TRUE;     /* already torn down on removal evidence */
        } else if (p->Generation != o->Generation) {
            /* Rebuilt for an earlier obligation of the same port. */
            met = (BOOLEAN)(o->Usb3 ? p->Usb3.Up : p->Pcie.Up);
        } else if (!cm->BudgetSpent && !moved) {
            what = tbtU4ResumeClass(cm, p);
            if (what == TBT_U4_RES_LOST) {
                p->LostKnown = TRUE;    /* cleared by a connection or a teardown */
            }
            if (what == TBT_U4_RES_REMOVED) {
                tbtU4DropLinks(cm, p);
                tbtU4Teardown(cm, p);
                met = TRUE;
            } else if (what == TBT_U4_RES_LOST && o->BuiltHere &&
                       tbtU4HostClose(cm, p) && tbtU4HostRestore(cm, p)) {
                /* The domain lost its configuration across the sleep; the
                 * host's side of the old paths proven closed first (its
                 * Valid bits need not have gone with the device's), then
                 * the same router rebuilt through sections 6.4 to 6.9 and
                 * the Detect gate before the hold is released. */
                tbtU4Zero(&p->Pcie, sizeof(p->Pcie));
                tbtU4Zero(&p->Usb3, sizeof(p->Usb3));
                tbtU4QDropPort(cm, p);
                p->Bonded = FALSE;
                p->SingleLane = FALSE;
                p->PcSetDev = FALSE;
                p->TmuConfigured = FALSE;
                p->TmuOk = FALSE;
                p->State = TBT_U4_EMPTY;
                tbtU4Connect(cm, p, FALSE);
                met = (BOOLEAN)(p->State == TBT_U4_UP &&
                                (o->Usb3 ? p->Usb3.Up : p->Pcie.Up));
            } else if (what == TBT_U4_RES_SAME) {
                met = tbtU4Verify(cm, p, o->Usb3);
                if (!met && !o->Usb3 && p->Pcie.Up && p->State != TBT_U4_EMPTY) {
                    /* Section 7.1 item 4 across the sleep, whatever the
                     * port's state: the PCIe tunnel lost with the router
                     * there is discharged through its own teardown and
                     * removal, a transitional LTSSM watched to its 2 s
                     * here; a USB3 obligation is verified on its own. */
                    tbtU4PcieLoss(cm, p, TRUE);
                    met = (BOOLEAN)!p->Pcie.Up;
                    if (!met && p->Pcie.Up && p->State != TBT_U4_EMPTY) {
                        /* Not lost after all (it recovered inside its
                         * 2 s): verified again. */
                        met = tbtU4Verify(cm, p, FALSE);
                    }
                }
            }
        }
        cm->ActivePort = 0;
        if (!met) {
            allMet = FALSE;
            if (p->State != TBT_U4_EMPTY && p->State != TBT_U4_HELD) {
                p->State = TBT_U4_HELD;
                p->Why = TBT_U4_WHY_RESUME;
                cm->Counters.PortsHeld++;
            }
            if (p->State == TBT_U4_HELD && !p->LostKnown) {
                /* Unclassified (or not verified): the next readable check
                 * classifies it before any reset mark counts. */
                p->ClassifyPending = TRUE;
            }
        }
    }
    for (k = 0; k < TBT_U4_OBLIGATIONS; k++) {
        o = &cm->Obligations[k];
        if (o->Used && o->Kid != 0 && !tbtU4KidResume(cm, o, moved)) {
            allMet = FALSE;
        }
    }
    for (i = 0; i < TBT_U4_MAX_KIDS; i++) {
        cm->Kids[i].CheckPending = TRUE;
    }
    tbtU4HotPlugEnable(cm);
    if (cm->BudgetSpent || moved) {
        allMet = FALSE;
    }
    cm->Budgeted = FALSE;
    cm->BudgetSpent = FALSE;
    cm->ResumeHeld = (BOOLEAN)!allMet;
    if (!allMet) {
        /* Section 9.4: a resume that left an obligation unmet failed;
         * this instance hands no record at its stop. */
        cm->Disturbed = TRUE;
    }
    for (i = 0; i < cm->PortCount; i++) {
        cm->Ports[i].CheckPending = TRUE;
    }
    return allMet ? TBT_OK : TBT_E_STATE;
}

/*
 * Section 7.6's bound: the platform preparation (1.05 s, a maximum counted
 * at its bound plus a tick), then the budgets TbtU4Resume enforces on the
 * clock - TBT_U4_RESUME_HOST_US for the rings and the host router,
 * TBT_U4_RESUME_EACH_US per obligation - and the margin for the one step
 * running when the budget runs out: the longest single wait of a sequence
 * (2 s, the connect and transitional waits), one request's four attempts
 * (100 ms each, with the request's and one acknowledgement's 10 ms
 * consumption), and one safe point's bounded batch of acknowledgements,
 * each counted with its tick. Every wait checks the
 * budget at its next step and every request ends within its attempts, so
 * no resume returns later than this. In microseconds: a few minutes for
 * eight obligations at most, below the 32-bit limit.
 */
ULONG TbtU4ResumeBoundUs(ULONG obligations, ULONG tickUs)
{
    ULONG margin;

    if (obligations > TBT_U4_OBLIGATIONS) {
        obligations = TBT_U4_OBLIGATIONS;
    }
    margin = (TBT_U4_CONNECT_US + tickUs) +
             TBT_CTL_ATTEMPTS * (TBT_CTL_TIMEOUT_US + tickUs +
                                 2UL * (TBT_TX_CONSUME_U4_US + tickUs)) +
             TBT_U4_SERVICE_BATCH * (TBT_TX_CONSUME_U4_US + tickUs);
    return TBT_ICL_FW_READY_POLLS * TBT_ICL_FW_READY_POLL_MS * 1000UL + tickUs +
           TBT_U4_RESUME_HOST_US + obligations * TBT_U4_RESUME_EACH_US + margin;
}

VOID TbtU4SetCarry(PTBT_U4CM cm, const TBT_U4_CARRY *in)
{
    if (in != NULL && in->Valid) {
        cm->Carried = *in;
    } else {
        tbtU4Zero(&cm->Carried, sizeof(cm->Carried));
    }
}

VOID TbtU4GetCarry(const TBT_U4CM *cm, PTBT_U4_CARRY out)
{
    const TBT_U4_PORT *p;
    const TBT_U4_KID *kd;
    TBT_U4_CARRY_PORT *e;
    TBT_U4_CARRY_KID *c;
    BOOLEAN pcie;
    BOOLEAN usb3;
    ULONG i;
    ULONG k;

    tbtU4Zero(out, sizeof(*out));
    if (cm->Contained || cm->Recovering) {
        return;
    }
    if (!cm->HostReached) {
        /* The start never read the host router: nothing was read or
         * written on the fabric, so the earlier record still stands. */
        if (!cm->Disturbed && cm->Carried.Valid) {
            *out = cm->Carried;
            out->Forwarded = TRUE;
        }
        return;
    }
    if (!cm->RanHealthy || cm->Disturbed || cm->Failed || cm->Suspended ||
        cm->Budgeted) {
        return;
    }
    out->Valid = TRUE;
    out->HostVendor = cm->HostRouter.Vendor;
    out->HostDevice = cm->HostRouter.Device;
    for (i = 0; i < cm->PortCount && i < TBT_U4_MAX_PORTS; i++) {
        p = &cm->Ports[i];
        if (!p->Dev.Present || !p->Dev.UuidValid) {
            continue;
        }
        pcie = (BOOLEAN)(p->Pcie.Up && (!p->Pcie.Preserved || p->Pcie.Own));
        usb3 = (BOOLEAN)(p->Usb3.Up && (!p->Usb3.Preserved || p->Usb3.Own));
        if (!pcie && !usb3) {
            continue;
        }
        e = &out->Port[i];
        e->Used = TRUE;
        e->Lane0 = p->Lane0;
        e->Vendor = p->Dev.Vendor;
        e->Device = p->Dev.Device;
        e->Tbt3 = p->Dev.Tbt3;
        for (k = 0; k < (p->Dev.Tbt3 ? 4UL : 2UL); k++) {
            e->Uuid[k] = p->Dev.Uuid[k];
        }
        if (pcie) {
            e->Pcie.Up = TRUE;
            e->Pcie.Hd = p->Pcie.Hd;
            e->Pcie.Hu = p->Pcie.Hu;
        }
        if (usb3) {
            e->Usb3.Up = TRUE;
            e->Usb3.Hd = p->Usb3.Hd;
            e->Usb3.Hu = p->Usb3.Hu;
        }
    }
    /* Record 34 section 6.9: the kids' tunnels this instance owns. */
    for (i = 0; i < TBT_U4_MAX_KIDS; i++) {
        kd = &cm->Kids[i];
        p = (kd->Port >= 1 && kd->Port <= cm->PortCount) ? &cm->Ports[kd->Port - 1] : NULL;
        if (!kd->Used || p == NULL || !kd->Dev.Present || !kd->Dev.UuidValid ||
            !kd->Pcie.Up || (kd->Pcie.Preserved && !kd->Pcie.Own)) {
            continue;
        }
        c = &out->Kid[i];
        c->Used = TRUE;
        c->HostLane0 = p->Lane0;
        c->ParentLane0 = kd->ParentLane0;
        c->Vendor = kd->Dev.Vendor;
        c->Device = kd->Dev.Device;
        c->Uuid[0] = kd->Dev.Uuid[0];
        c->Uuid[1] = kd->Dev.Uuid[1];
        c->Hd = kd->Pcie.Hd;
        c->Hu = kd->Pcie.Hu;
    }
}

VOID TbtU4Stop(PTBT_U4CM cm)
{
    /* Record 35 section 3.8: every peer ends before the rings stop. */
    if (cm->Stage == TBT_U4_STAGE_RUNNING) {
        tbtU4XdEndAll(cm, FALSE);
    }
    tbtU4IsoRevokeAll(cm);
    /* Section 6.2.3 item 4 part 6: no connection manager will verify a
     * recovery once this one stops; a contained one writes nothing. */
    if (!cm->Contained && !cm->Suspended && !cm->Failed &&
        cm->Stage == TBT_U4_STAGE_RUNNING) {
        tbtU4ElrAllOff(cm);
    }
    if (!cm->Contained) {
        TbtRingStop(cm->Ring);
    }
    cm->Stage = TBT_U4_STAGE_NONE;
}

VOID TbtU4Contain(PTBT_U4CM cm)
{
    ULONG i;

    if (cm->Contained) {
        return;
    }
    tbtU4IsoRevokeAll(cm);
    cm->Contained = TRUE;
    cm->Disturbed = TRUE;
    /* Record 35: every peer gone, nothing written (tbtU4Off). */
    tbtU4XdEndAll(cm, TRUE);
    cm->Counters.ContainedRetired += cm->ActionCount;
    cm->ActionCount = 0;
    cm->ActionHead = 0;
    cm->ResyncPending = FALSE;
    for (i = 0; i < TBT_U4_MAX_PORTS; i++) {
        cm->Ports[i].AnchorValid = FALSE;
    }
    /* The snapshot before the tails are abandoned: a recovery owes them
     * (section 7.7 part 3 step 4). */
    TbtU4Snapshot(cm);
    for (i = 0; i < TBT_U4_MAX_PORTS; i++) {
        if (cm->DeferredTail[i]) {
            cm->DeferredTail[i] = 0;
            cm->Counters.ContainedRetired++;
        }
    }
    tbtU4Log(cm, "u4.contained", cm->Stage);
}

VOID TbtU4Snapshot(PTBT_U4CM cm)
{
    ULONG i;

    cm->SnapStage = cm->Stage;
    cm->SnapPortCount = cm->PortCount;
    for (i = 0; i < TBT_U4_MAX_PORTS; i++) {
        cm->SnapPorts[i] = cm->Ports[i];
        /* A later episode's snapshot keeps the tails the first one
         * recorded until a commit owes them. */
        cm->SnapDeferred[i] = cm->SnapDeferred[i] || cm->DeferredTail[i];
    }
    cm->SnapHost = cm->HostRouter;
    for (i = 0; i < TBT_U4_MAX_LINKS; i++) {
        cm->SnapLinks[i] = cm->Links[i];
    }
    cm->SnapValid = TRUE;
}

/* A router answering at the device's recorded route is positively the
 * recorded one only through tbtU4ResumeClass's SAME (identity, UUID,
 * still enumerated). For a tunnel of such a router: its own side intact
 * - the Up adapter's Path Enable set and the recorded device entries
 * Valid - read only. */
static BOOLEAN tbtU4DevSide(PTBT_U4CM cm, PTBT_U4_PORT p, BOOLEAN usb3)
{
    PTBT_U4_ROUTER d;
    PTBT_U4_TUNNEL t;
    ULONG dp;
    ULONG v;
    ULONG e[2];

    d = &p->Dev;
    t = usb3 ? &p->Usb3 : &p->Pcie;
    dp = tbtU4FirstOf(d, usb3 ? TBT_ADP_TYPE_USB3_UP : TBT_ADP_TYPE_PCIE_UP);
    if (dp == 0 || d->Adp[dp].CapAdp == 0 || t->Hd == 0 ||
        tbtU4Rd(cm, &d->Route, TBT_CS_ADAPTER, dp,
                d->Adp[dp].CapAdp + (usb3 ? TBT_USB3_CS_0 : TBT_PCIE_CS_0),
                &v) != TBT_OK ||
        (v & (usb3 ? TBT_USB3_0_PE : TBT_PCIE0_PE)) == 0) {
        return FALSE;
    }
    if (tbtU4Entry(cm, &d->Route, d->UpAdp, t->Hd, e) != TBT_OK ||
        (e[0] & TBT_PATH0_VALID) == 0 ||
        tbtU4Entry(cm, &d->Route, dp, TBT_HOP_PROTOCOL, e) != TBT_OK ||
        (e[0] & TBT_PATH0_VALID) == 0) {
        return FALSE;
    }
    return TRUE;
}

/* The host side of a tunnel gone: the host router enumerated afresh by
 * this recovery, or its protocol adapter's Path Enable clear or its
 * HopID 8 entry not Valid. Read only; an unreadable register is not
 * "gone". */
static BOOLEAN tbtU4HostGone(PTBT_U4CM cm, PTBT_U4_PORT p, BOOLEAN usb3)
{
    PTBT_U4_ROUTER h;
    ULONG hp;
    ULONG v;
    ULONG e[2];

    h = &cm->HostRouter;
    if (!h->Preserved) {
        return TRUE;
    }
    hp = usb3 ? p->Usb3Down : p->PcieDown;
    if (hp == 0 || h->Adp[hp].CapAdp == 0) {
        return FALSE;
    }
    if (tbtU4Rd(cm, &h->Route, TBT_CS_ADAPTER, hp,
                h->Adp[hp].CapAdp + (usb3 ? TBT_USB3_CS_0 : TBT_PCIE_CS_0),
                &v) != TBT_OK) {
        return FALSE;
    }
    if ((v & (usb3 ? TBT_USB3_0_PE : TBT_PCIE0_PE)) == 0) {
        return TRUE;
    }
    if (tbtU4Entry(cm, &h->Route, hp, TBT_HOP_PROTOCOL, e) != TBT_OK) {
        return FALSE;
    }
    return (BOOLEAN)((e[0] & TBT_PATH0_VALID) == 0);
}

/*
 * Part 3 step 5's drain of one tunnel, the recorded teardown alone (decision
 * U21): on the positively identified recorded router (`dev`) both ends -
 * Path Enable cleared at both protocol adapters, each recorded entry
 * invalidated and drained (tbtU4Invalidate reads Pending Packets whatever
 * Valid reads); on a departed or replaced router (`dev` FALSE) the host
 * end alone. No other entry, no lane adapter and no TMU register is
 * written. For PCIe the host adapter's LTSSM read in Detect; for USB3 the
 * reuse gap started and the bandwidth released (owed where HCA does not
 * answer). The host entries are taken as recorded: a host router
 * enumerated afresh holds whatever it held. TRUE only when every step was
 * proven; what did not drain is quarantined, and the obligation is kept on
 * the port (RecDrain*) for the next attempt.
 */
static BOOLEAN tbtU4RecoverDrain(PTBT_U4CM cm, PTBT_U4_PORT p, BOOLEAN usb3,
                                 BOOLEAN dev)
{
    PTBT_U4_TUNNEL t;
    PTBT_U4_ROUTER h;
    BOOLEAN present;
    BOOLEAN ok;

    h = &cm->HostRouter;
    t = usb3 ? &p->Usb3 : &p->Pcie;
    t->Written |= TBT_U4_E_HOST_PROTO | TBT_U4_E_HOST_ENABLE |
                  (t->Hu != 0 ? TBT_U4_E_HOST_LANE : 0);
    if (dev) {
        t->Written |= TBT_U4_E_DEV_PROTO | TBT_U4_E_DEV_ENABLE |
                      (t->Hd != 0 ? TBT_U4_E_DEV_LANE : 0);
    } else {
        t->Written &= ~(TBT_U4_E_DEV_PROTO | TBT_U4_E_DEV_ENABLE |
                        TBT_U4_E_DEV_LANE);
    }
    present = p->Dev.Present;
    if (!dev) {
        p->Dev.Present = FALSE;     /* no device-side write of any kind */
    }
    t->Incomplete = FALSE;
    ok = tbtU4TunnelDown(cm, p, t, usb3);
    p->Dev.Present = present;
    if (!usb3 && ok && p->PcieDown != 0 && h->Adp[p->PcieDown].CapAdp != 0) {
        ok = tbtU4WaitDetect(cm, NULL, 0, h, p->PcieDown, TBT_U4_DETECT_US);
    }
    if (usb3) {
        p->LastUsb3TeardownUs = tbtU4Now(cm);
        p->Usb3GapValid = TRUE;
        if ((t->Written & TBT_U4_E_BANDWIDTH) != 0 && !tbtU4ReleaseBw(cm, p)) {
            ok = FALSE;
        }
        cm->Counters.Usb3TornDown++;
        p->RecDrainUsb3 = (BOOLEAN)!ok;
    } else {
        cm->Counters.PcieTornDown++;
        p->RecDrainPcie = (BOOLEAN)!ok;
        /* Drained or not, the host side of the endpoint's path was
         * touched: its devnode is the operating system's to retire. */
        p->RemovalDebt = TRUE;
    }
    t->Up = FALSE;
    t->Preserved = FALSE;
    t->Own = FALSE;
    return ok;
}

/* One recorded path entry of an interrupted sequence, read now: TRUE when
 * it still reads as this driver's write - Valid with the output the
 * sequence wrote there (the pairs tbtU4Follow reads back: host protocol
 * HopID 8 to lane 0 at Hd, host lane 0 at Hu to the protocol adapter's
 * HopID 8, device lane at Hd to its protocol adapter's HopID 8, device
 * protocol HopID 8 to its lane at Hu), or not Valid (drained whatever it
 * holds). An entry that cannot be read, or whose HopID was never
 * recorded, is not proven either way (*unread). */
static BOOLEAN tbtU4StillWritten(PTBT_U4CM cm, const TBT_ROUTE *r, ULONG adp,
                                 ULONG hop, ULONG outAdp, ULONG outHop,
                                 BOOLEAN *unread)
{
    ULONG e[2];

    if (hop == 0 || outHop == 0 || tbtU4Entry(cm, r, adp, hop, e) != TBT_OK) {
        *unread = TRUE;
        return FALSE;
    }
    if ((e[0] & TBT_PATH0_VALID) == 0) {
        return TRUE;
    }
    return (BOOLEAN)(TBT_PATH0_OUT_ADP(e[0]) == outAdp &&
                     TBT_PATH0_OUT_HOP(e[0]) == outHop);
}

/* The Path Enable an interrupted sequence left owed on a protocol adapter
 * whose own HopID 8 entry it did not record (or no longer does): cleared
 * only where that entry reads closed - a Path Enable beside an open path
 * this driver cannot show its own is held without writes (tbtU4Reserve).
 * TRUE when cleared. */
static BOOLEAN tbtU4UndoEnable(PTBT_U4CM cm, ULONG port, const TBT_ROUTE *r,
                               ULONG adp, ULONG cap, BOOLEAN usb3)
{
    ULONG e[2];

    if (tbtU4Entry(cm, r, adp, TBT_HOP_PROTOCOL, e) == TBT_OK &&
        (e[0] & TBT_PATH0_VALID) == 0 && (e[1] & TBT_PATH1_PP) == 0) {
        return tbtU4Disable(cm, port, r, adp, cap, usb3);
    }
    (VOID)tbtU4Reserve(cm, port, r, adp, 0, 0, 0, TRUE, cap,
                       usb3 ? TBT_USB3_0_PE : TBT_PCIE0_PE);
    return FALSE;
}

/*
 * Part 3 step 4's undo of a sequence the loss (or an earlier attempt)
 * interrupted, on a port with no tunnel up at the loss: each write its
 * record holds undone only where the entry still reads as written
 * (tbtU4StillWritten) - the entry invalidated and drained - and the
 * protocol adapter's Path Enable cleared only where its own path is still
 * this driver's or reads closed (tbtU4UndoEnable); the device side only on
 * the positively identified router (dev), no sweep and no other entry.
 * Where not - an entry read as another path's, or not read - it is
 * quarantined without writes (tbtU4Reserve) and its bit leaves the record,
 * so no teardown, sweep or retry writes it; the port stays HELD, served by
 * its unplug. A record bit is dropped otherwise only once its undo is
 * proven (a later attempt reads it again). TRUE when nothing recorded is
 * left; *residual when the record was incomplete or an undo did not
 * complete (the quarantine's sweep before the port connects again, which
 * the held entries refuse).
 */
static BOOLEAN tbtU4RecoverUndo(PTBT_U4CM cm, PTBT_U4_PORT p, BOOLEAN usb3,
                                BOOLEAN dev, BOOLEAN *residual)
{
    PTBT_U4_ROUTER h;
    PTBT_U4_ROUTER d;
    PTBT_U4_TUNNEL t;
    ULONG hp;
    ULONG dp;
    ULONG w;
    ULONG pt;
    BOOLEAN u;
    BOOLEAN mHp;
    BOOLEAN mHl;
    BOOLEAN mDl;
    BOOLEAN mDp;
    BOOLEAN ok;

    h = &cm->HostRouter;
    d = &p->Dev;
    t = usb3 ? &p->Usb3 : &p->Pcie;
    w = t->Written;
    if ((w & ~TBT_U4_E_BANDWIDTH) == 0 && !t->Incomplete) {
        return TRUE;
    }
    pt = usb3 ? TBT_ADP_TYPE_USB3_UP : TBT_ADP_TYPE_PCIE_UP;
    hp = usb3 ? p->Usb3Down : p->PcieDown;
    dp = (dev && d->Present) ? tbtU4FirstOf(d, pt) : 0;
    ok = TRUE;
    mHp = FALSE;
    mHl = FALSE;
    mDl = FALSE;
    mDp = FALSE;
    /* Each recorded entry read; one not as written is held (reserved),
     * its bit out of the record. */
    if ((w & TBT_U4_E_HOST_PROTO) != 0 && hp != 0) {
        u = FALSE;
        mHp = tbtU4StillWritten(cm, &h->Route, hp, TBT_HOP_PROTOCOL, p->Lane0,
                                t->Hd, &u);
        if (!mHp) {
            if (tbtU4Reserve(cm, p->Index, &h->Route, hp, TBT_HOP_PROTOCOL,
                             p->Lane0, t->Hd, FALSE, 0, 0)) {
                t->Written &= ~TBT_U4_E_HOST_PROTO;
            }
            ok = FALSE;
        }
    }
    if ((w & TBT_U4_E_HOST_LANE) != 0 && hp != 0) {
        u = FALSE;
        mHl = tbtU4StillWritten(cm, &h->Route, p->Lane0, t->Hu, hp,
                                TBT_HOP_PROTOCOL, &u);
        if (!mHl) {
            /* No HopID recorded: nothing of it can be named, or written. */
            if (t->Hu == 0 ||
                tbtU4Reserve(cm, p->Index, &h->Route, p->Lane0, t->Hu, hp,
                             TBT_HOP_PROTOCOL, FALSE, 0, 0)) {
                t->Written &= ~TBT_U4_E_HOST_LANE;
            }
            ok = FALSE;
        }
    }
    if ((w & TBT_U4_E_DEV_LANE) != 0 && dp != 0) {
        u = FALSE;
        mDl = tbtU4StillWritten(cm, &d->Route, d->UpAdp, t->Hd, dp,
                                TBT_HOP_PROTOCOL, &u);
        if (!mDl) {
            if (t->Hd == 0 ||
                tbtU4Reserve(cm, p->Index, &d->Route, d->UpAdp, t->Hd, dp,
                             TBT_HOP_PROTOCOL, FALSE, 0, 0)) {
                t->Written &= ~TBT_U4_E_DEV_LANE;
            }
            ok = FALSE;
        }
    }
    if ((w & TBT_U4_E_DEV_PROTO) != 0 && dp != 0) {
        u = FALSE;
        mDp = tbtU4StillWritten(cm, &d->Route, dp, TBT_HOP_PROTOCOL, d->UpAdp,
                                t->Hu, &u);
        if (!mDp) {
            if (tbtU4Reserve(cm, p->Index, &d->Route, dp, TBT_HOP_PROTOCOL,
                             d->UpAdp, t->Hu, FALSE, 0, 0)) {
                t->Written &= ~TBT_U4_E_DEV_PROTO;
            }
            ok = FALSE;
        }
    }
    /* Path Enable: cleared beside a protocol entry still this driver's;
     * else only where that entry reads closed, held otherwise. */
    if ((w & TBT_U4_E_HOST_ENABLE) != 0 && hp != 0 && h->Adp[hp].CapAdp != 0) {
        if (mHp ? tbtU4Disable(cm, p->Index, &h->Route, hp, h->Adp[hp].CapAdp,
                               usb3)
                : tbtU4UndoEnable(cm, p->Index, &h->Route, hp,
                                  h->Adp[hp].CapAdp, usb3)) {
            t->Written &= ~TBT_U4_E_HOST_ENABLE;
        } else {
            if (tbtU4Reserved(cm, &h->Route, hp, 0, TRUE)) {
                t->Written &= ~TBT_U4_E_HOST_ENABLE;
            }
            ok = FALSE;
        }
    }
    if ((w & TBT_U4_E_DEV_ENABLE) != 0 && dp != 0 && d->Adp[dp].CapAdp != 0) {
        if (mDp ? tbtU4Disable(cm, p->Index, &d->Route, dp, d->Adp[dp].CapAdp,
                               usb3)
                : tbtU4UndoEnable(cm, p->Index, &d->Route, dp,
                                  d->Adp[dp].CapAdp, usb3)) {
            t->Written &= ~TBT_U4_E_DEV_ENABLE;
        } else {
            if (tbtU4Reserved(cm, &d->Route, dp, 0, TRUE)) {
                t->Written &= ~TBT_U4_E_DEV_ENABLE;
            }
            ok = FALSE;
        }
    }
    if (mHp) {
        if (tbtU4Invalidate(cm, p->Index, &h->Route, hp, TBT_HOP_PROTOCOL)) {
            t->Written &= ~TBT_U4_E_HOST_PROTO;
        } else {
            ok = FALSE;
        }
    }
    if (mHl) {
        if (tbtU4Invalidate(cm, p->Index, &h->Route, p->Lane0, t->Hu)) {
            t->Written &= ~TBT_U4_E_HOST_LANE;
        } else {
            ok = FALSE;
        }
    }
    if (mDl) {
        if (tbtU4Invalidate(cm, p->Index, &d->Route, d->UpAdp, t->Hd)) {
            t->Written &= ~TBT_U4_E_DEV_LANE;
        } else {
            ok = FALSE;
        }
    }
    if (mDp) {
        if (tbtU4Invalidate(cm, p->Index, &d->Route, dp, TBT_HOP_PROTOCOL)) {
            t->Written &= ~TBT_U4_E_DEV_PROTO;
        } else {
            ok = FALSE;
        }
    }
    if (usb3 && (w & ~TBT_U4_E_BANDWIDTH) != 0) {
        /* Guide 5.5.1 step 1's gap runs from any USB3 path teardown. */
        p->LastUsb3TeardownUs = tbtU4Now(cm);
        p->Usb3GapValid = TRUE;
    }
    if (usb3 && (t->Written & TBT_U4_E_BANDWIDTH) != 0 && !tbtU4ReleaseBw(cm, p)) {
        ok = FALSE;
    }
    if (t->Incomplete || !ok) {
        *residual = TRUE;
    }
    return (BOOLEAN)(ok && !t->Incomplete &&
                     (t->Written & ~TBT_U4_E_BANDWIDTH) == 0);
}

/* The same for HiFi Enhanced Uni-Directional (record 22 section 6.2.2 item
 * 6): no time disruption, the downstream parameters as written, and on
 * both lane 0 adapters uni-directional mode clear, the enhanced mode and
 * its HiFi interval set, the replenish values as written and time sync
 * enabled. `cs0` is the device's TMU_RTR_CS_0 as just read. Read only. */
static BOOLEAN tbtU4TmuEnhancedAsLeft(PTBT_U4CM cm, PTBT_U4_PORT p, ULONG cs0)
{
    PTBT_U4_ROUTER h;
    PTBT_U4_ROUTER d;
    const TBT_U4_ROUTER *rt;
    ULONG adp;
    ULONG k;
    ULONG v;
    ULONG avg;

    h = &cm->HostRouter;
    d = &p->Dev;
    avg = TBT_TMU_AVG_CONST_HIFI | (TBT_TMU_AVG_CONST_HIFI << 6) |
          (TBT_TMU_AVG_CONST_HIFI << 12) | (TBT_TMU_AVG_CONST_HIFI << 18);
    if (!p->TmuConfigured || d->Adp[d->UpAdp].CapTmu == 0 ||
        h->Adp[p->Lane0].CapTmu == 0 || (cs0 & TBT_TMU_RTR0_TD) != 0 ||
        (cs0 & TBT_TMU_RTR0_FREQ_WIND_MASK) !=
            (TBT_TMU_FREQ_WINDOW_HIFI << TBT_TMU_RTR0_FREQ_WIND_SHIFT) ||
        tbtU4Rd(cm, &d->Route, TBT_CS_ROUTER, 0, d->CapTmu + TBT_TMU_RTR_CS_15,
                &v) != TBT_OK ||
        (v & TBT_TMU_RTR15_AVG_MASK) != avg) {
        return FALSE;
    }
    for (k = 0; k < 2; k++) {
        rt = k == 0 ? h : d;
        adp = k == 0 ? p->Lane0 : d->UpAdp;
        if (tbtU4Rd(cm, &rt->Route, TBT_CS_ADAPTER, adp,
                    rt->Adp[adp].CapTmu + TBT_TMU_ADP_CS_3, &v) != TBT_OK ||
            (v & TBT_TMU_ADP3_UDM) != 0 ||
            tbtU4Rd(cm, &rt->Route, TBT_CS_ADAPTER, adp,
                    rt->Adp[adp].CapTmu + TBT_TMU_ADP_CS_6, &v) != TBT_OK ||
            (v & TBT_TMU_ADP6_DTS) != 0 ||
            tbtU4Rd(cm, &rt->Route, TBT_CS_ADAPTER, adp,
                    rt->Adp[adp].CapTmu + TBT_TMU_ADP_CS_8, &v) != TBT_OK ||
            (v & TBT_TMU_ADP8_EUDM) == 0 ||
            (v & TBT_TMU_ADP8_REPL_TIMEOUT_MASK) != TBT_TMU_REPL_TIMEOUT_HIFI ||
            ((v & TBT_TMU_ADP8_REPL_THRESH_MASK) >> TBT_TMU_ADP8_REPL_THRESH_SHIFT) !=
                TBT_TMU_REPL_THRESH_HIFI ||
            tbtU4Rd(cm, &rt->Route, TBT_CS_ADAPTER, adp,
                    rt->Adp[adp].CapTmu + TBT_TMU_ADP_CS_9, &v) != TBT_OK ||
            (v >> TBT_TMU_ADP9_INTERVAL_SHIFT) != TBT_TMU_ADP_INTERVAL_HIFI ||
            ((v & TBT_TMU_ADP9_DIRSWN_MASK) >> TBT_TMU_ADP9_DIRSWN_SHIFT) !=
                TBT_TMU_DIRSWN_HIFI ||
            (v & TBT_TMU_ADP9_REPLN_MASK) != TBT_TMU_REPLN_HIFI) {
            return FALSE;
        }
    }
    return TRUE;
}

/* Part 3 step 4 (iii): the link's TMU read now, as tbtU4TmuEnable left it
 * configured (the device router's TSPacketInterval HiFi, Disable Time
 * Sync clear on both lane 0 adapters), or a router whose TMU needs none;
 * read only, nothing changed. The snapshot's TmuOk alone proves nothing
 * across the outage. */
static BOOLEAN tbtU4TmuAsLeft(PTBT_U4CM cm, PTBT_U4_PORT p)
{
    PTBT_U4_ROUTER h;
    PTBT_U4_ROUTER d;
    const TBT_U4_ROUTER *rt;
    ULONG adp;
    ULONG k;
    ULONG v;
    ULONG avg;

    h = &cm->HostRouter;
    d = &p->Dev;
    if (!p->TmuOk) {
        return FALSE;
    }
    if (d->CapTmu == 0) {
        return TRUE;
    }
    if (tbtU4Rd(cm, &d->Route, TBT_CS_ROUTER, 0, d->CapTmu + TBT_TMU_RTR_CS_0,
                &v) != TBT_OK) {
        return FALSE;
    }
    if ((v & TBT_TMU_RTR0_TSP_NS) != 0) {
        return TRUE;
    }
    if (p->TmuEnhanced) {
        return tbtU4TmuEnhancedAsLeft(cm, p, v);
    }
    /* HiFi-Bi completed: no time disruption left set, the measurement
     * window and averaging constants as written, TSPacketInterval HiFi,
     * uni-directional mode clear and time sync enabled on both lane 0
     * adapters. */
    avg = TBT_TMU_AVG_CONST_HIFI | (TBT_TMU_AVG_CONST_HIFI << 6) |
          (TBT_TMU_AVG_CONST_HIFI << 12) | (TBT_TMU_AVG_CONST_HIFI << 18);
    if (!p->TmuConfigured || d->Adp[d->UpAdp].CapTmu == 0 ||
        h->Adp[p->Lane0].CapTmu == 0 ||
        (v & TBT_TMU_RTR0_TD) != 0 ||
        (v & TBT_TMU_RTR0_FREQ_WIND_MASK) !=
            (TBT_TMU_FREQ_WINDOW_HIFI << TBT_TMU_RTR0_FREQ_WIND_SHIFT) ||
        tbtU4Rd(cm, &d->Route, TBT_CS_ROUTER, 0, d->CapTmu + TBT_TMU_RTR_CS_15,
                &v) != TBT_OK ||
        (v & TBT_TMU_RTR15_AVG_MASK) != avg ||
        tbtU4Rd(cm, &d->Route, TBT_CS_ROUTER, 0, d->CapTmu + TBT_TMU_RTR_CS_3,
                &v) != TBT_OK ||
        ((v & TBT_TMU_RTR3_INTERVAL_MASK) >> TBT_TMU_RTR3_INTERVAL_SHIFT) !=
            TBT_TMU_INTERVAL_HIFI ||
        tbtU4Rd(cm, &d->Route, TBT_CS_ADAPTER, d->UpAdp,
                d->Adp[d->UpAdp].CapTmu + TBT_TMU_ADP_CS_3, &v) != TBT_OK ||
        (v & TBT_TMU_ADP3_UDM) != 0 ||
        tbtU4Rd(cm, &d->Route, TBT_CS_ADAPTER, d->UpAdp,
                d->Adp[d->UpAdp].CapTmu + TBT_TMU_ADP_CS_6, &v) != TBT_OK ||
        (v & TBT_TMU_ADP6_DTS) != 0 ||
        tbtU4Rd(cm, &h->Route, TBT_CS_ADAPTER, p->Lane0,
                h->Adp[p->Lane0].CapTmu + TBT_TMU_ADP_CS_3, &v) != TBT_OK ||
        (v & TBT_TMU_ADP3_UDM) != 0 ||
        tbtU4Rd(cm, &h->Route, TBT_CS_ADAPTER, p->Lane0,
                h->Adp[p->Lane0].CapTmu + TBT_TMU_ADP_CS_6, &v) != TBT_OK ||
        (v & TBT_TMU_ADP6_DTS) != 0) {
        return FALSE;
    }
    /* A USB4 version 2 end as tbtU4TmuEnable leaves it from Off
     * (tbtU4TmuOff's fields): enhanced uni-directional mode clear and no
     * adapter time sync interval. */
    for (k = 0; k < 2; k++) {
        rt = k == 0 ? h : d;
        adp = k == 0 ? p->Lane0 : d->UpAdp;
        if (TBT_USB4_VERSION_MAJOR(rt->Usb4Version) != TBT_USB4_MAJOR_2) {
            continue;
        }
        if (tbtU4Rd(cm, &rt->Route, TBT_CS_ADAPTER, adp,
                    rt->Adp[adp].CapTmu + TBT_TMU_ADP_CS_8, &v) != TBT_OK ||
            (v & TBT_TMU_ADP8_EUDM) != 0 ||
            tbtU4Rd(cm, &rt->Route, TBT_CS_ADAPTER, adp,
                    rt->Adp[adp].CapTmu + TBT_TMU_ADP_CS_9, &v) != TBT_OK ||
            (v & TBT_TMU_ADP9_INTERVAL_MASK) != 0) {
            return FALSE;
        }
    }
    return TRUE;
}

/* The lanes of a link this driver bonded or left single, still as it left
 * them (section 7.7 part 3 step 4 (ii)): read only. */
static BOOLEAN tbtU4LanesAsLeft(PTBT_U4CM cm, PTBT_U4_PORT p)
{
    ULONG lane;
    ULONG lane1;

    if (tbtU4LaneRd(cm, &cm->HostRouter, p->Lane0, &lane) != TBT_OK ||
        tbtU4LaneRd(cm, &cm->HostRouter, p->Lane0 + 1, &lane1) != TBT_OK) {
        return FALSE;
    }
    if (p->Bonded) {
        return tbtU4WidthBonded(lane);
    }
    return (BOOLEAN)(!tbtU4WidthBonded(lane) &&
                     tbtU4SingleOk(cm, lane1));
}

/* The port reset to EMPTY by a recovery after a departed router (part 3
 * step 4, removal evidence): the device's records dropped with nothing
 * written. On a disconnected link the lane fields this driver changed stay
 * owed to the idle tick's restore after the commit (tbtU4HostRestore), the
 * host TMU of the port to its quarantine retry, an abandoned teardown tail
 * to the tick. On a link another router holds (`replaced`) none of that
 * is owed: the router is discovered with its state as found (section 9.2,
 * DiscoverOwed, after the commit once its barrier allows) and the departed
 * router's cleanup is never applied to its link. */
static VOID tbtU4RecoverEmpty(PTBT_U4CM cm, PTBT_U4_PORT p, BOOLEAN replaced)
{
    /* Record 34 section 6.8: its kids go with the departed router,
     * unwritten, whatever cleanup they still owed. */
    tbtU4KidsDrop(cm, p);
    tbtU4DropLinks(cm, p);
    TbtCtlForget(&cm->Ctl, &p->Dev.Route);
    tbtU4QDropPort(cm, p);
    if (replaced) {
        p->LaneWritten = FALSE;
        p->Lane1Disabled = FALSE;
        p->PcSetHost = FALSE;
        p->TmuConfigured = FALSE;
        p->TmuQuarantine = FALSE;
        cm->DeferredTail[p - cm->Ports] = 0;
        p->DiscoverOwed = TRUE;
        p->RecCleanup = FALSE;
    } else {
        if (p->TmuConfigured) {
            p->TmuQuarantine = TRUE;
            p->TmuConfigured = FALSE;
        }
        p->RecCleanup = TRUE;
    }
    tbtU4Zero(&p->Dev, sizeof(p->Dev));
    tbtU4Zero(&p->Pcie, sizeof(p->Pcie));
    tbtU4Zero(&p->Usb3, sizeof(p->Usb3));
    p->TmuOk = FALSE;
    p->Bonded = FALSE;
    p->SingleLane = FALSE;
    p->PcSetDev = FALSE;
    p->Unlocked = FALSE;
    tbtU4XdEarlyEnd(cm, p);
    p->PcieFailed = FALSE;
    p->Usb3Failed = FALSE;
    p->Ejected = FALSE;
    p->DpPreserved = FALSE;
    p->RemovalOwed = FALSE;
    p->LostKnown = FALSE;
    p->RelockSeen = FALSE;
    p->HostCloseOwed = FALSE;
    p->ClassifyPending = FALSE;
    p->RebuildPcie = FALSE;
    p->RebuildUsb3 = FALSE;
    p->RecVerified = 0;
    p->State = TBT_U4_EMPTY;
    p->Why = TBT_U4_WHY_NONE;
    p->CheckPending = TRUE;
}

/*
 * A departed router's lane, TMU and tail cleanup (RecCleanup), owed on a
 * link that read disconnected, admitted at each point that would make
 * some of it (the deferred tail, the host TMU retry, the EMPTY port's lane
 * restore, the connect's own restore): the link is read there and then;
 * connected (or unreadable), another router holds it - the cleanup is
 * dropped and that router discovered as found (DiscoverOwed, section 9.2).
 * TRUE when the caller may go on with it. Once nothing of it is owed, or
 * the port is no longer EMPTY (a connection began, its own records from
 * then on), the gate ends and touches none of them.
 */
static BOOLEAN tbtU4RecCleanupGate(PTBT_U4CM cm, PTBT_U4_PORT p)
{
    ULONG lane;

    if (!p->RecCleanup) {
        return TRUE;
    }
    if (p->RecDeparted) {
        return FALSE;   /* its recorded drain first (tbtU4Departed) */
    }
    if (p->State != TBT_U4_EMPTY ||
        (!p->LaneWritten && !p->Lane1Disabled && !p->PcSetHost &&
         !p->TmuQuarantine && !cm->DeferredTail[p - cm->Ports])) {
        p->RecCleanup = FALSE;
        return TRUE;
    }
    if (tbtU4LaneRd(cm, &cm->HostRouter, p->Lane0, &lane) == TBT_OK &&
        TbtU4LaneClass(TBT_LANE1_STATE(lane)) == TBT_U4_LANE_DISCONNECTED) {
        return TRUE;
    }
    p->LaneWritten = FALSE;
    p->Lane1Disabled = FALSE;
    p->PcSetHost = FALSE;
    p->TmuConfigured = FALSE;
    p->TmuQuarantine = FALSE;
    cm->DeferredTail[p - cm->Ports] = 0;
    p->RecCleanup = FALSE;
    p->DiscoverOwed = TRUE;
    tbtU4Log(cm, "u4.recover.cleanup-dropped", p->Index);
    return FALSE;
}

/*
 * A departed port (part 3 steps 4 and 5): each tunnel owed (`ob`) drained
 * by its recorded teardown on the host side alone, then the port EMPTY
 * (tbtU4RecoverEmpty, as found on a link another router holds). A drain
 * that did not complete keeps the port HELD as departed (RecDeparted): its
 * records and quarantine kept, the device side forgotten, nothing of the
 * departed router's lane, TMU or tail cleanup left owed on a held link,
 * and no ordinary check or teardown made for it - the recorded host-side
 * drain alone is retried, by the next attempt or at each tick after the
 * commit (TbtU4Poll), until it completes. TRUE when EMPTY.
 */
static BOOLEAN tbtU4Departed(PTBT_U4CM cm, PTBT_U4_PORT p, const BOOLEAN *ob)
{
    ULONG k;
    ULONG lane;
    BOOLEAN held;
    BOOLEAN replaced;

    held = FALSE;
    for (k = 0; k < 2; k++) {
        if (ob[k] && !tbtU4RecoverDrain(cm, p, (BOOLEAN)(k == 1), FALSE)) {
            held = TRUE;
        }
    }
    /* Read after the drains: a link still connected is another router's
     * (a replacement, or the same one with its enumeration given up); an
     * unreadable lane is taken as connected. A disconnected one keeps the
     * departed router's cleanup owed, gated again on the link when it is
     * made (RecCleanup, tbtU4RecCleanupGate). */
    replaced = (BOOLEAN)(tbtU4LaneRd(cm, &cm->HostRouter, p->Lane0, &lane) != TBT_OK ||
                         TbtU4LaneClass(TBT_LANE1_STATE(lane)) !=
                             TBT_U4_LANE_DISCONNECTED);
    if (held) {
        tbtU4DropLinks(cm, p);
        if (replaced) {
            p->LaneWritten = FALSE;
            p->Lane1Disabled = FALSE;
            p->PcSetHost = FALSE;
            p->TmuConfigured = FALSE;
            p->TmuQuarantine = FALSE;
            cm->DeferredTail[p - cm->Ports] = 0;
        } else {
            p->RecCleanup = TRUE;
        }
        p->Dev.Present = FALSE;
        if (!p->RecDeparted || p->State != TBT_U4_HELD) {
            cm->Counters.RecoverHeld++;
            cm->Counters.PortsHeld++;
        }
        p->RecDeparted = TRUE;
        p->State = TBT_U4_HELD;
        p->Why = TBT_U4_WHY_RECOVER;
        p->ClassifyPending = TRUE;
        return FALSE;
    }
    p->RecDeparted = FALSE;
    p->RecDrainPcie = FALSE;
    p->RecDrainUsb3 = FALSE;
    tbtU4RecoverEmpty(cm, p, replaced);
    tbtU4Log(cm, "u4.recover.departed", (p->Index << 8) | (replaced ? 1UL : 0UL));
    return TRUE;
}

/* Part 3 step 4 for one port, against its record from the snapshot (and
 * whatever an earlier attempt left in it): every tunnel up at the loss, or
 * owed a rebuild or a drain by an earlier attempt, is an obligation of its
 * own. Reading only, but for the drains of step 5. Nothing is queued
 * (tbtU4Queue refuses while Recovering) and no statement made. */
static VOID tbtU4RecoverPort(PTBT_U4CM cm, PTBT_U4_PORT p)
{
    BOOLEAN ob[2];
    BOOLEAN hadPcie;
    BOOLEAN any;
    BOOLEAN held;
    BOOLEAN owed;
    BOOLEAN lanes;
    BOOLEAN dev;
    ULONG what;
    ULONG k;
    BOOLEAN usb3;

    ob[0] = (BOOLEAN)(p->Pcie.Up || p->RebuildPcie || p->RecDrainPcie);
    ob[1] = (BOOLEAN)(p->Usb3.Up || p->RebuildUsb3 || p->RecDrainUsb3);
    p->RecVerified = 0;
    p->CheckPending = TRUE;
    p->UnplugPending = FALSE;
    p->Transitional = FALSE;
    p->AnchorValid = FALSE;
    p->LinkSeen = FALSE;
    if (!ob[0] && !ob[1] && p->State == TBT_U4_XDOMAIN) {
        /* Record 35: an inter-domain link, its peer ended by the
         * containment - connected afresh after the commit (the ordinary
         * check of an EMPTY port), its quarantine kept, its lanes and its
         * Inter-Domain bit put back by the tick's host restore. */
        p->State = TBT_U4_EMPTY;
        p->Why = TBT_U4_WHY_NONE;
        return;
    }
    if (!ob[0] && !ob[1]) {
        /* No tunnel at the loss: found again by the ordinary check after
         * the commit (an EMPTY port's connect, a HELD or FAILED port's
         * evidence), its quarantine kept. A sequence the loss stopped
         * left the port HELD with what it wrote recorded; its entries
         * are swept before it connects again. */
        if (p->State != TBT_U4_EMPTY && p->State != TBT_U4_UNSUPPORTED &&
            p->State != TBT_U4_FAILED) {
            if ((p->Pcie.Written & ~TBT_U4_E_BANDWIDTH) != 0 ||
                (p->Usb3.Written & ~TBT_U4_E_BANDWIDTH) != 0 ||
                p->Pcie.Incomplete || p->Usb3.Incomplete) {
                /* Part 3 step 4: the sequence's writes undone from the
                 * record before the port is classified, where they still
                 * read as written, the device side only on the positively
                 * identified router; an entry never read, or that did not
                 * undo, is quarantined and swept before the port connects
                 * again. */
                dev = FALSE;
                cm->ActivePort = p->Index;
                if (p->Dev.Present) {
                    cm->RelockExplained = (BOOLEAN)!cm->HostRouter.Preserved;
                    dev = (BOOLEAN)(tbtU4ResumeClass(cm, p) ==
                                    TBT_U4_RES_SAME);
                    cm->RelockExplained = FALSE;
                }
                held = FALSE;
                for (k = 0; k < 2; k++) {
                    (VOID)tbtU4RecoverUndo(cm, p, (BOOLEAN)(k == 1), dev,
                                           &held);
                }
                if (held) {
                    p->HostResidual = TRUE;
                }
                cm->ActivePort = 0;
            }
            p->State = TBT_U4_HELD;
            p->Why = TBT_U4_WHY_RECOVER;
            p->ClassifyPending = TRUE;
        }
        return;
    }
    hadPcie = ob[0];
    cm->ActivePort = p->Index;
    if (p->RecDeparted) {
        /* An earlier attempt's departed drain, still owed: the router is
         * gone already, nothing of it is read again. */
        what = TBT_U4_RES_REMOVED;
    } else {
        cm->RelockExplained = (BOOLEAN)!cm->HostRouter.Preserved;
        what = tbtU4ResumeClass(cm, p);
        cm->RelockExplained = FALSE;
    }
    tbtU4Log(cm, "u4.recover.class", (p->Index << 8) | what);
    if (what == TBT_U4_RES_REMOVED || what == TBT_U4_RES_LOST) {
        /* Departed (a router that gave up its enumeration reads as
         * removal evidence, section 7.7's open item): the host side
         * alone drained - a replacement's adapters are never written -
         * the port EMPTY, its device gone; connected afresh after the
         * commit when its barrier allows. No TUNNEL_DOWN and no
         * statement. */
        cm->Counters.RecoverDeparted += (ob[0] ? 1UL : 0) + (ob[1] ? 1UL : 0);
        (VOID)tbtU4Departed(cm, p, ob);
        if (hadPcie) {
            p->RemovalDebt = TRUE;
        }
        cm->ActivePort = 0;
        return;
    }
    any = FALSE;
    held = FALSE;
    lanes = (BOOLEAN)(what == TBT_U4_RES_SAME && tbtU4LanesAsLeft(cm, p));
    if (what == TBT_U4_RES_SAME && !p->Dev.Tbt3) {
        /* Prerequisite (i): Get Capabilities read again (section 6.4). */
        tbtU4GetCapabilities(cm, &p->Dev);
    }
    for (k = 0; k < 2; k++) {
        if (!ob[k]) {
            continue;
        }
        usb3 = (BOOLEAN)(k == 1);
        owed = (BOOLEAN)(usb3 ? (p->RebuildUsb3 || p->RecDrainUsb3)
                              : (p->RebuildPcie || p->RecDrainPcie));
        if (what == TBT_U4_RES_SAME && !owed && tbtU4Verify(cm, p, usb3)) {
            p->RecVerified |= usb3 ? 2 : 1;
            cm->Counters.RecoverPreserved++;
            any = TRUE;
            continue;
        }
        if (what != TBT_U4_RES_SAME || !lanes ||
            !(owed || (tbtU4DevSide(cm, p, usb3) && tbtU4HostGone(cm, p, usb3)))) {
            held = TRUE;
            cm->Counters.RecoverHeld++;
            continue;
        }
        if (usb3 && (!cm->HostRouter.Preserved || !tbtU4TmuAsLeft(cm, p))) {
            /* Prerequisite (iii): the TMU as found must still give the
             * link its accuracy; a host router enumerated afresh lost its
             * own TMU configuration. Drained, failed, its quarantine
             * kept; the PCIe obligation goes on. */
            if (p->RebuildUsb3 || tbtU4RecoverDrain(cm, p, TRUE, TRUE)) {
                p->RebuildUsb3 = FALSE;
                p->Usb3Failed = TRUE;
            } else {
                held = TRUE;
                cm->Counters.RecoverHeld++;
            }
            continue;
        }
        /* A partial loss (or an earlier attempt's drain): to be rebuilt
         * after the commit, its lanes and TMU as found. */
        if ((usb3 ? p->RebuildUsb3 : p->RebuildPcie) ||
            tbtU4RecoverDrain(cm, p, usb3, TRUE)) {
            if (usb3) {
                p->RebuildUsb3 = TRUE;
            } else {
                p->RebuildPcie = TRUE;
            }
            continue;
        }
        held = TRUE;
        cm->Counters.RecoverHeld++;
    }
    cm->ActivePort = 0;
    if (held || (!any && !p->RebuildPcie && !p->RebuildUsb3 && !p->Usb3Failed)) {
        p->State = TBT_U4_HELD;
        p->Why = TBT_U4_WHY_RECOVER;
        p->ClassifyPending = TRUE;
        cm->Counters.PortsHeld++;
    } else if (any) {
        p->State = TBT_U4_UP;
    } else {
        p->State = TBT_U4_HELD;
        p->Why = TBT_U4_WHY_RECOVER;
    }
}

/* Part 3 step 4, before the commit: every quarantine record on a router
 * still present read again, so that nothing written for one after the
 * commit is another path's. An entry record that reads closed, or Valid
 * with the output recorded for it, stays writable; one that reads Valid
 * otherwise, whose output was never recorded, or that cannot be read is
 * held without writes (Reserve) until tbtU4RetryQuarantine establishes it.
 * An enable record stays writable only while its adapter's protocol entry
 * reads closed. Read only. */
static VOID tbtU4QOwnership(PTBT_U4CM cm)
{
    ULONG i;
    ULONG e[2];
    BOOLEAN own;
    PTBT_U4_QENTRY q;

    for (i = 0; i < TBT_U4_QUARANTINE; i++) {
        q = &cm->Quarantine[i];
        if (!q->Used || q->Reserve ||
            (TbtRouteDepth(&q->Route) != 0 && !tbtU4QOwnerPresent(cm, q))) {
            continue;
        }
        own = FALSE;
        if (tbtU4Entry(cm, &q->Route, q->Adapter,
                       q->Enable ? TBT_HOP_PROTOCOL : q->Hop, e) == TBT_OK) {
            if ((e[0] & TBT_PATH0_VALID) == 0 && (e[1] & TBT_PATH1_PP) == 0) {
                own = TRUE;
            } else if (!q->Enable && (e[0] & TBT_PATH0_VALID) != 0 &&
                       (q->OutAdapter != 0 || q->OutHop != 0) &&
                       TBT_PATH0_OUT_ADP(e[0]) == q->OutAdapter &&
                       TBT_PATH0_OUT_HOP(e[0]) == q->OutHop) {
                own = TRUE;
            } else if (!q->Enable && (e[0] & TBT_PATH0_VALID) == 0) {
                own = TRUE;     /* closed, still draining: ours to drain */
            }
        }
        if (!own) {
            q->Reserve = TRUE;
            tbtU4Log(cm, "u4.quarantine.reserve",
                     (q->Adapter << 16) | (q->Enable ? 0x8000UL : q->Hop));
        }
    }
}

LONG TbtU4Recover(PTBT_U4CM cm, BOOLEAN *final)
{
    ULONG i;
    ULONG k;
    ULONG v;
    ULONG found;
    LONG rc;
    PTBT_U4_PORT p;
    PTBT_U4_PORT saved;

    *final = FALSE;
    if (!cm->Contained || !cm->SnapValid || cm->Va == NULL) {
        return TBT_E_STATE;
    }
    tbtU4IsoRevokeAll(cm);
    cm->Counters.RecoverAttempts++;
    cm->Recovering = TRUE;
    cm->RecoverRings = FALSE;
    cm->RecRestored = FALSE;
    cm->RecReserveFull = FALSE;
    cm->Budgeted = FALSE;
    cm->BudgetSpent = FALSE;
    /* Step 1 (decision U20): nothing is placed in the kept buffer before
     * the controller is proven to write nothing into it. */
    if (!TbtRingStopVerified(cm->Ring)) {
        cm->Counters.RecoverFinal++;
        tbtU4Log(cm, "u4.recover.stop-unverified", 1);
        *final = TRUE;
        return TBT_E_HW;
    }
    cm->RecoverRings = TRUE;
    rc = tbtU4Rings(cm);
    if (rc != TBT_OK) {
        return rc;
    }
    /* Bounded as the resume is (section 7.6's budgets on the clock): the
     * host router, then each tunnel up at the loss. */
    k = 0;
    for (i = 0; i < cm->SnapPortCount && i < TBT_U4_MAX_PORTS; i++) {
        p = &cm->SnapPorts[i];
        k += (p->Pcie.Up || p->RebuildPcie || p->RecDrainPcie) ? 1UL : 0UL;
        k += (p->Usb3.Up || p->RebuildUsb3 || p->RecDrainUsb3) ? 1UL : 0UL;
    }
    cm->Budgeted = TRUE;
    cm->BudgetStartUs = TbtClockUs(cm->Host);
    cm->BudgetWaitedUs = 0;
    cm->BudgetUs = cm->ResumeHostUs + k * cm->ResumeEachUs;
    cm->BudgetRingBase = cm->Ring->WaitedUs;
    v = cm->Host->Read32(cm->Host->Context, TBT_RX_RING(0) + TBT_RING_SIZE_REG);
    if (v != (TBT_RING_U4_ENTRIES | (TBT_RING_BUF_BYTES << TBT_RX_BUFSIZE_SHIFT))) {
        tbtU4Log(cm, "u4.recover.size", v);
        return TBT_E_HW;
    }
    /* Steps 2 and 3: test (d)'s router half and the host router, by
     * section 6.1 (enumerated afresh, adopted, or refused). */
    rc = tbtU4HostRouter(cm);
    if (rc != TBT_OK) {
        return rc;
    }
    cm->Stage = TBT_U4_STAGE_HOST;
    /* The port table was rebuilt by the walk; each port's record comes
     * back from the snapshot, matched by its lane 0 adapter. A port the
     * router's mapping operation named at the loss must be named the same
     * now (the router-named source of step 6). */
    saved = cm->ResumeSaved;
    for (k = 0; k < TBT_U4_MAX_PORTS; k++) {
        saved[k] = cm->SnapPorts[k];
    }
    /* Checked whole before any record is replaced: a refusal leaves the
     * snapshot authoritative, and TbtU4RecoverFail puts it back over the
     * walk (RecRestored still FALSE). A snapshot port the walk did not
     * find again refuses the attempt too: its obligations are not lost. */
    for (k = 0; k < cm->SnapPortCount && k < TBT_U4_MAX_PORTS; k++) {
        if (saved[k].Index == 0) {
            continue;
        }
        for (i = 0; i < cm->PortCount; i++) {
            if (cm->Ports[i].Lane0 == saved[k].Lane0) {
                break;
            }
        }
        if (i == cm->PortCount) {
            tbtU4Log(cm, "u4.recover.port", saved[k].Index);
            return TBT_E_STATE;
        }
        p = &cm->Ports[i];
        if (saved[k].MapValid &&
            (!p->MapValid || p->MapRid != saved[k].MapRid)) {
            tbtU4Log(cm, "u4.recover.map", (p->Index << 16) | p->MapRid);
            return TBT_E_STATE;
        }
    }
    found = 0;
    for (i = 0; i < cm->PortCount; i++) {
        p = &cm->Ports[i];
        for (k = 0; k < cm->SnapPortCount && k < TBT_U4_MAX_PORTS; k++) {
            if (saved[k].Index != 0 && saved[k].Lane0 == p->Lane0) {
                saved[k].MapValid = p->MapValid;
                saved[k].MapRid = p->MapRid;
                saved[k].HostBaPcie = p->HostBaPcie;
                saved[k].HostBaHi = p->HostBaHi;
                saved[k].HostBaUsb3 = p->HostBaUsb3;
                saved[k].Index = p->Index;
                *p = saved[k];
                if (p->Discovered) {
                    found |= 1UL << i;
                }
                if (cm->SnapDeferred[k] && i < TBT_U4_MAX_PORTS) {
                    /* Section 7.4's tail abandoned at the containment:
                     * owed to the idle tick once ACTIVE, as section 7.3's
                     * quarantine rule owes it, never resumed here. */
                    cm->DeferredTail[i] = 1;
                }
            }
        }
    }
    for (k = 0; k < TBT_U4_MAX_LINKS; k++) {
        cm->Links[k] = cm->SnapLinks[k];
    }
    cm->RecRestored = TRUE;
    /* Record 34 section 6.9: every kid a discovered link again, under its
     * parent's classification. */
    tbtU4KidsToLinks(cm);
    tbtU4HostTmu(cm);
    /* Steps 4 and 5, port by port; a port the snapshot has no discovered
     * record of (a loss before the bind's discovery had completed,
     * decision U27) is discovered as the start discovers it (section
     * 9.2), reading only. */
    for (i = 0; i < cm->PortCount; i++) {
        if (tbtU4Spent(cm)) {
            break;
        }
        if ((found & (1UL << i)) == 0) {
            tbtU4DiscoverPort(cm, &cm->Ports[i]);
            continue;
        }
        tbtU4RecoverPort(cm, &cm->Ports[i]);
    }
    if (!tbtU4Spent(cm)) {
        tbtU4QOwnership(cm);
    }
    rc = TBT_OK;
    if (tbtU4Off(cm)) {
        rc = TBT_E_STOPPED;
    } else if (cm->BudgetSpent) {
        rc = TBT_E_TIMEOUT;
    } else if (cm->RecReserveFull) {
        /* An entry of unestablished ownership found no quarantine record
         * to hold it unwritten (tbtU4Reserve). */
        rc = TBT_E_STATE;
    }
    cm->Budgeted = FALSE;
    cm->BudgetSpent = FALSE;
    return rc;
}

BOOLEAN TbtU4RecoverDemote(PTBT_U4CM cm, ULONG port)
{
    PTBT_U4_PORT p;
    ULONG v[2];
    BOOLEAN ok;

    p = tbtU4PortOf(cm, port);
    if (p == NULL || !cm->Recovering || !p->Pcie.Up ||
        (p->RecVerified & 1) == 0) {
        /* Only a tunnel this recovery verified and kept: a router that
         * was not positively identified is never written. */
        return FALSE;
    }
    cm->ActivePort = p->Index;
    if (!p->Dev.Tbt3 && !tbtU4StillSame(cm, p, v)) {
        cm->ActivePort = 0;
        p->State = TBT_U4_HELD;
        p->Why = TBT_U4_WHY_RECOVER;
        p->ClassifyPending = TRUE;
        p->RecVerified = 0;
        cm->Counters.RecoverHeld++;
        return FALSE;
    }
    ok = tbtU4RecoverDrain(cm, p, FALSE, TRUE);
    cm->ActivePort = 0;
    p->RecVerified &= ~1;
    cm->Counters.RecoverPreserved--;
    tbtU4Log(cm, "u4.recover.demote", (p->Index << 8) | (ok ? 1UL : 0UL));
    if (!ok) {
        p->State = TBT_U4_HELD;
        p->Why = TBT_U4_WHY_RECOVER;
        p->ClassifyPending = TRUE;
        cm->Counters.RecoverHeld++;
        return FALSE;
    }
    p->RebuildPcie = TRUE;
    if (!p->Usb3.Up) {
        p->State = TBT_U4_HELD;
        p->Why = TBT_U4_WHY_RECOVER;
    }
    return TRUE;
}

VOID TbtU4RecoverCommit(PTBT_U4CM cm)
{
    ULONG i;

    cm->Contained = FALSE;
    cm->Recovering = FALSE;
    cm->RecoverRings = FALSE;
    cm->Suspended = FALSE;
    cm->Failed = FALSE;
    cm->Stage = TBT_U4_STAGE_RUNNING;
    cm->SnapValid = FALSE;
    for (i = 0; i < TBT_U4_MAX_PORTS; i++) {
        cm->SnapDeferred[i] = 0;
    }
    cm->Counters.RecoverOk++;
    for (i = 0; i < cm->PortCount; i++) {
        cm->Ports[i].CheckPending = TRUE;
        /* Section 6.2.3 item 4 part 6: counted as not set; the tick sets
         * it again where the port qualifies. */
        cm->Ports[i].ElrSet = FALSE;
    }
}

VOID TbtU4RecoverFail(PTBT_U4CM cm)
{
    ULONG i;

    if (cm->Recovering && !cm->RecRestored && cm->SnapValid) {
        /* Failed before the snapshot's records were put back over the host
         * router's walk: the records stay the snapshot's, never the walk's
         * (the next snapshot copies them). */
        cm->PortCount = cm->SnapPortCount;
        for (i = 0; i < TBT_U4_MAX_PORTS; i++) {
            cm->Ports[i] = cm->SnapPorts[i];
        }
        cm->HostRouter = cm->SnapHost;
        for (i = 0; i < TBT_U4_MAX_LINKS; i++) {
            cm->Links[i] = cm->SnapLinks[i];
        }
        cm->RecRestored = TRUE;
    }
    if (cm->RecoverRings) {
        /* Verified where the gate still admits it; else DmaMayRun stays
         * set and the buffer is kept (cm_pool.c). */
        TbtRingStop(cm->Ring);
    }
    cm->RecoverRings = FALSE;
    cm->Recovering = FALSE;
    cm->Budgeted = FALSE;
    cm->BudgetSpent = FALSE;
    cm->Stage = cm->SnapStage;
    cm->ActivePort = 0;
    tbtU4Log(cm, "u4.recover.failed", cm->Ring->DmaMayRun ? 1UL : 0UL);
}

BOOLEAN TbtU4PortVacant(PTBT_U4CM cm, ULONG port)
{
    PTBT_U4_PORT p;
    ULONG lane;

    p = tbtU4PortOf(cm, port);
    if (p == NULL || p->Dev.Present || p->Pcie.Up || p->Usb3.Up ||
        p->RebuildPcie || p->RecDrainPcie || p->RebuildUsb3 ||
        p->RecDrainUsb3 || p->HostResidual || p->RecDeparted) {
        return FALSE;
    }
    if (tbtU4LaneRd(cm, &cm->HostRouter, p->Lane0, &lane) != TBT_OK) {
        return FALSE;
    }
    return (BOOLEAN)(TbtU4LaneClass(TBT_LANE1_STATE(lane)) ==
                     TBT_U4_LANE_DISCONNECTED);
}

BOOLEAN TbtU4TakeDebt(PTBT_U4CM cm, ULONG port)
{
    PTBT_U4_PORT p;
    BOOLEAN debt;

    p = tbtU4PortOf(cm, port);
    if (p == NULL) {
        return FALSE;
    }
    debt = p->RemovalDebt;
    p->RemovalDebt = FALSE;
    return debt;
}

VOID TbtU4SetBarred(PTBT_U4CM cm, ULONG port, BOOLEAN barred)
{
    PTBT_U4_PORT p;

    p = tbtU4PortOf(cm, port);
    if (p != NULL) {
        p->Barred = barred;
    }
}

BOOLEAN TbtU4AllPortsDisconnected(PTBT_U4CM cm)
{
    ULONG i;
    ULONG lane;

    if (cm->Failed || cm->Suspended || cm->Stage != TBT_U4_STAGE_RUNNING ||
        cm->PortCount == 0 || cm->Contained) {
        return FALSE;
    }
    for (i = 0; i < cm->PortCount; i++) {
        if (tbtU4LaneRd(cm, &cm->HostRouter, cm->Ports[i].Lane0, &lane) != TBT_OK ||
            TbtU4LaneClass(TBT_LANE1_STATE(lane)) != TBT_U4_LANE_DISCONNECTED) {
            return FALSE;
        }
    }
    return TRUE;
}

BOOLEAN TbtU4AnyRouterConnected(const TBT_U4CM *cm)
{
    ULONG i;

    if (cm->Contained) {
        return TRUE;    /* section 7.7: unknown is not "nothing connected" */
    }
    for (i = 0; i < cm->PortCount; i++) {
        if (cm->Ports[i].Dev.Present || cm->Ports[i].State == TBT_U4_HELD) {
            return TRUE;
        }
    }
    return FALSE;
}

const TBT_U4_PORT *TbtU4Port(const TBT_U4CM *cm, ULONG port)
{
    if (port == 0 || port > cm->PortCount) {
        return NULL;
    }
    return &cm->Ports[port - 1];
}

/* ---------------------------------------------------------------------- */
/* Inter-domain links (design record 35 sections 3.3, 3.5, 3.8, 4, 5.1;   */
/* task 45.59 step S3)                                                    */
/* ---------------------------------------------------------------------- */

/*
 * A depth-1 port whose router read answered ERR_ENUM or ERR_NUA leads, with
 * XhciCmNet 1 (TBT_U4_CONFIG.Net), perhaps to another host: the port is
 * TBT_U4_XDOMAIN and one TBT_XDP_PEER (tbt_xdp.c, the pure engine) runs on
 * it from the idle tick, which also feeds it the inter-domain packets the
 * ring's XDomain sink queued (tbt_ring.h TbtRingSetXdomainSink, step S2).
 * Everything here acts on the host router alone: its lane adapters of the
 * port, its PORT_CS_19 and the two path entries of section 5.1. The
 * peer's own router is the peer's connection manager's.
 */

static PTBT_U4_XD tbtU4XdOf(PTBT_U4CM cm, const TBT_U4_PORT *p)
{
    if (p == NULL || p->Index == 0 || p->Index > TBT_U4_MAX_PORTS) {
        return NULL;
    }
    return &cm->Xd[p->Index - 1];
}

/* The engine's clock: milliseconds that never wrap with the microsecond
 * clock (TbtClockUs wraps every 71 minutes; the engine compares deadlines
 * by unsigned difference). */
static ULONG tbtU4XdMs(PTBT_U4CM cm)
{
    ULONG now;
    ULONG d;

    now = TbtClockUs(cm->Host);
    if (!cm->XdClockValid) {
        cm->XdClockValid = TRUE;
        cm->XdLastUs = now;
        cm->XdMs = 0;
        cm->XdRemUs = 0;
    }
    d = (now - cm->XdLastUs) + cm->XdRemUs;
    cm->XdLastUs = now;
    cm->XdMs += d / 1000UL;
    cm->XdRemUs = d % 1000UL;
    return cm->XdMs;
}

/*
 * Section 3.1: this domain's UUID, read once from the host router's
 * ROUTER_CS_7 and 8 and laid out as Linux and the firmware lay it (DW 0
 * ROUTER_CS_7, DW 1 ROUTER_CS_8, DW 2 and 3 all ones, [linux] switch.c
 * tb_switch_set_uuid); one that reads all zero or all ones is replaced by
 * a value of this start, from the clock and the ring's bus address [inf],
 * logged. The adapter's MAC (section 4.4) is derived from the same two
 * DWs, port number 0. FALSE when the read fails (tried again later).
 */
static BOOLEAN tbtU4XdIdentity(PTBT_U4CM cm)
{
    ULONG u[2];
    ULONG t;

    if (cm->XdLocalValid) {
        return TRUE;
    }
    if (tbtU4RdN(cm, &cm->HostRouter.Route, TBT_CS_ROUTER, 0, TBT_ROUTER_CS_7, 2, u) !=
        TBT_OK) {
        return FALSE;
    }
    if ((u[0] == 0 && u[1] == 0) || (u[0] == 0xFFFFFFFFUL && u[1] == 0xFFFFFFFFUL)) {
        t = TbtClockUs(cm->Host);
        u[0] = (t * 2654435761UL) ^ cm->Phys ^ 0x58484349UL;
        u[1] = ~(t ^ (cm->Phys << 7)) ^ 0x39384E54UL;
        tbtU4Log(cm, "cm.xdp.uuid.random", u[0]);
    }
    cm->XdLocal.Dw[0] = u[0];
    cm->XdLocal.Dw[1] = u[1];
    cm->XdLocal.Dw[2] = 0xFFFFFFFFUL;
    cm->XdLocal.Dw[3] = 0xFFFFFFFFUL;
    TbtNetMacFromUuid(u[0], u[1], 0, 0, cm->XdMac);
    cm->XdLocalValid = TRUE;
    return TRUE;
}

/* The host router's host interface adapter, 0 for none. */
static ULONG tbtU4XdHi(const TBT_U4CM *cm)
{
    ULONG a;

    for (a = 1; a <= cm->HostRouter.MaxAdp && a < TBT_U4_MAX_ADP; a++) {
        if (cm->HostRouter.Adp[a].Present &&
            cm->HostRouter.Adp[a].Type == TBT_ADP_TYPE_HI) {
            return a;
        }
    }
    return 0;
}

/* PORT_CS_19's USB4 Port is Inter-Domain bit (Guide 3.3; Spec PORT_CS_19
 * bit 4) written as `on` says, the other fields as read, StartAsymmetricFlow
 * and Initiate Gen 4 Link Recovery as 0 - and no write at all while
 * StartAsymmetricFlow reads 1 (Spec Table 8-13, as tbtU4ElrEnd has it):
 * then the write is withheld and TBT_E_STATE returned, to be tried again. */
static LONG tbtU4XdPidWrite(PTBT_U4CM cm, PTBT_U4_PORT p, ULONG cap, BOOLEAN on)
{
    PTBT_U4_ROUTER h = &cm->HostRouter;
    ULONG v;
    LONG rc;

    rc = tbtU4Rd(cm, &h->Route, TBT_CS_ADAPTER, p->Lane0, cap + TBT_PORT_CS_19, &v);
    if (rc != TBT_OK) {
        return rc;
    }
    if ((v & TBT_PORT19_SAF) != 0) {
        return TBT_E_STATE;
    }
    v &= ~(TBT_PORT19_PID | TBT_PORT19_ILR | TBT_PORT19_SAF);
    return tbtU4Wr(cm, &h->Route, TBT_CS_ADAPTER, p->Lane0, cap + TBT_PORT_CS_19,
                   on ? (v | TBT_PORT19_PID) : v);
}

/* The bit cleared and read back clear; TRUE when it is. */
static BOOLEAN tbtU4XdPidClear(PTBT_U4CM cm, PTBT_U4_PORT p)
{
    PTBT_U4_ROUTER h;
    ULONG cap;
    ULONG v;

    h = &cm->HostRouter;
    cap = h->Adp[p->Lane0].CapPort;
    if (cap == 0) {
        return TRUE;
    }
    return (BOOLEAN)(tbtU4XdPidWrite(cm, p, cap, FALSE) == TBT_OK &&
                     tbtU4Rd(cm, &h->Route, TBT_CS_ADAPTER, p->Lane0, cap + TBT_PORT_CS_19,
                             &v) == TBT_OK &&
                     (v & TBT_PORT19_PID) == 0);
}

/* The bit set once the peer is known, owned before the write; cleared at
 * the peer's end. A clear not read back stays owed to the port
 * (TBT_U4CM.XdPidOwed), retried by the idle tick while no peer runs there
 * (tbtU4XdService). */
static VOID tbtU4XdPid(PTBT_U4CM cm, PTBT_U4_PORT p, PTBT_U4_XD x, BOOLEAN on)
{
    PTBT_U4_ROUTER h;
    ULONG cap;

    h = &cm->HostRouter;
    cap = h->Adp[p->Lane0].CapPort;
    if (cap == 0) {
        x->PidOk = on;      /* no PORT_CS_19 to write */
        return;
    }
    if (on) {
        x->PidSet = TRUE;
        if (tbtU4XdPidWrite(cm, p, cap, TRUE) == TBT_OK) {
            x->PidOk = TRUE;
        } else {
            tbtU4Log(cm, "cm.xdp.pid.fail", p->Index);
        }
        return;
    }
    x->PidOk = FALSE;
    if (x->PidSet && tbtU4XdPidClear(cm, p)) {
        x->PidSet = FALSE;
    }
    if (x->PidSet) {
        tbtU4Log(cm, "cm.xdp.pid.owed", p->Index);
    }
}

/* Whether a port other than `x`'s holds the service (section 13: one per
 * NHI); its link up counts from the moment its paths are built. */
static BOOLEAN tbtU4XdOtherUp(const TBT_U4CM *cm, const TBT_U4_XD *x)
{
    ULONG i;

    for (i = 0; i < TBT_U4_MAX_PORTS; i++) {
        if (&cm->Xd[i] != x && cm->Xd[i].Used &&
            (cm->Xd[i].LinkUp || cm->Xd[i].Peer.PathsUp)) {
            return TRUE;
        }
    }
    return FALSE;
}

/* Record 35 section 4.2: the network driver's link goes down at the first
 * logout of either side. A Logout from the peer takes the paths down
 * inside the engine (ServiceDown, which lowers the link first); this
 * host's own Logout takes them down only when it is answered or times
 * out, so after each engine call the link is lowered as soon as the
 * engine's service is no longer up, the paths, the rings and the NHI's one
 * service held until ServiceDown. PathsUp guards the service's own build:
 * inside ServiceUp the link is raised before the engine marks the service
 * up. */
static VOID tbtU4XdLinkSync(PTBT_U4CM cm, PTBT_U4_XD x)
{
    if (x->Used && x->LinkUp && x->Peer.PathsUp && x->Peer.NetState != TBT_NET_S_UP) {
        x->LinkUp = FALSE;
        tbtU4Log(cm, "cm.net.link.down", x->Port);
        if (cm->Net.Link != NULL) {
            cm->Net.Link(cm->Net.Context, x->Port, FALSE, NULL);
        }
    }
}

/* Each peer offered the service (its block's network directory, its
 * logins accepted) while the network driver holds it open and no other
 * port's is up (section 13); a change rebuilds that peer's block and tells
 * it (TbtXdpPeerSetNetWanted). */
static VOID tbtU4XdOffer(PTBT_U4CM cm)
{
    ULONG i;
    ULONG in;
    BOOLEAN want;
    PTBT_U4_XD x;

    for (i = 0; i < TBT_U4_MAX_PORTS; i++) {
        x = &cm->Xd[i];
        if (!x->Used) {
            continue;
        }
        want = (BOOLEAN)(cm->NetWanted && !tbtU4XdOtherUp(cm, x));
        if (want != x->Peer.NetWanted ||
            (want && x->Peer.LocalProps.Prtcstns != cm->NetPrtcstns)) {
            in = cm->XdIn;
            cm->XdIn = x->Port;
            TbtXdpPeerSetNetWanted(&x->Peer, want, cm->NetPrtcstns);
            cm->XdIn = in;
            tbtU4XdLinkSync(cm, x);
        }
    }
}

/* ---- the engine's operations (TBT_XDP_OPS; Context is the TBT_U4_XD) -- */

static PTBT_U4_PORT tbtU4XdPort(PTBT_U4_XD x)
{
    return &x->Cm->Ports[x->Port - 1];
}

static LONG tbtU4XdSend(PVOID context, ULONG pdf, const ULONG *dw, ULONG words)
{
    PTBT_U4_XD x = (PTBT_U4_XD)context;

    if (tbtU4Off(x->Cm) || words == 0) {
        return TBT_E_STOPPED;
    }
    return TbtRingSend(x->Cm->Ring, pdf, dw, words);
}

static ULONG tbtU4XdNow(PVOID context)
{
    return tbtU4XdMs(((PTBT_U4_XD)context)->Cm);
}

static VOID tbtU4XdLog(PVOID context, const char *label, ULONG value)
{
    PTBT_U4_XD x = (PTBT_U4_XD)context;

    tbtU4Log(x->Cm, label, (x->Port << 24) | (value & 0x00FFFFFFUL));
}

/* IDS Table 2-7's widths from the host's lane 0: Supported Link Widths
 * (LANE_ADP_CS_0 21:20), Target Link Width (LANE_ADP_CS_1 5:4) and the
 * Negotiated Link Width as a lane count. */
static LONG tbtU4XdLinkRead(PVOID context, PULONG slw, PULONG tlw, PULONG lanes)
{
    PTBT_U4_XD x = (PTBT_U4_XD)context;
    PTBT_U4CM cm = x->Cm;
    PTBT_U4_PORT p = tbtU4XdPort(x);
    PTBT_U4_ROUTER h = &cm->HostRouter;
    ULONG cs0;
    ULONG cs1;
    LONG rc;

    rc = tbtU4Rd(cm, &h->Route, TBT_CS_ADAPTER, p->Lane0,
                 h->Adp[p->Lane0].CapLane + TBT_LANE_CS_0, &cs0);
    if (rc == TBT_OK) {
        rc = tbtU4LaneRd(cm, h, p->Lane0, &cs1);
    }
    if (rc != TBT_OK) {
        return rc;
    }
    *slw = (cs0 >> 20) & 0x3UL;
    *tlw = (cs1 & TBT_LANE1_TLW_MASK) >> 4;
    *lanes = TBT_LANE1_NLW(cs1) == TBT_LANE_WIDTH_X2 ? 2UL : 1UL;
    return TBT_OK;
}

/* Target Link Width on both host lane adapters of the port, each value
 * read and kept first so the teardown's host restore puts it back
 * (TBT_U4_PORT.LaneSaved, LaneWritten; record 22 section 6.5 step 6.1). */
static LONG tbtU4XdLinkSetTarget(PVOID context, ULONG tlw)
{
    PTBT_U4_XD x = (PTBT_U4_XD)context;
    PTBT_U4CM cm = x->Cm;
    PTBT_U4_PORT p = tbtU4XdPort(x);
    PTBT_U4_ROUTER h = &cm->HostRouter;
    ULONG k;

    if (!p->LaneWritten) {
        for (k = 0; k < 2; k++) {
            if (tbtU4LaneRd(cm, h, p->Lane0 + k, &p->LaneSaved[k]) != TBT_OK) {
                return TBT_E_STATE;
            }
        }
        p->LaneWritten = TRUE;      /* owned before the writes */
    }
    for (k = 0; k < 2; k++) {
        if (tbtU4LaneRmw(cm, h, p->Lane0 + k, TBT_LANE1_TLW_MASK,
                         (tlw << 4) & TBT_LANE1_TLW_MASK) != TBT_OK) {
            return TBT_E_STATE;
        }
    }
    return TBT_OK;
}

/* Lane Bonding on the host's lane 0 (Guide 7.1 step 4). */
static LONG tbtU4XdLinkBond(PVOID context)
{
    PTBT_U4_XD x = (PTBT_U4_XD)context;
    PTBT_U4_PORT p = tbtU4XdPort(x);

    x->Cm->Counters.BondAttempts++;
    return tbtU4LaneRmw(x->Cm, &x->Cm->HostRouter, p->Lane0, 0, TBT_LANE1_LB);
}

/*
 * A link that stays single (Guide 7.5.2), as record 22 section 6.5 step 6
 * verifies one on the host's side: 6.1, the Target Link Width and Lane
 * Bonding a bonding wrote put back and read back; 6.2, lane 1 in CLd or
 * Disabled is single already, and a lane 0 that reads x2 bonded after
 * all; 6.4, otherwise lane 1 in CL0 beside a lane 0 at x1, its Lane
 * Disable set (owned before the write, put back by the teardown's host
 * restore) and read back Disabled. Anything short of that leaves the link
 * unverified (TBT_U4_XD.LinkBad): the peer is served, but no path is
 * built on it until a re-plug.
 */
static VOID tbtU4XdLane1Off(PVOID context)
{
    PTBT_U4_XD x = (PTBT_U4_XD)context;
    PTBT_U4CM cm = x->Cm;
    PTBT_U4_PORT p = tbtU4XdPort(x);
    PTBT_U4_ROUTER h = &cm->HostRouter;
    ULONG v[2];
    ULONG k;
    ULONG st;

    if (h->Tbt3 || p->Lane0 + 1 >= TBT_U4_MAX_ADP || h->Adp[p->Lane0 + 1].CapLane == 0) {
        return;
    }
    if (p->LaneWritten) {
        for (k = 0; k < 2; k++) {
            if (tbtU4LaneRmw(cm, h, p->Lane0 + k, TBT_LANE1_TLW_MASK | TBT_LANE1_LB,
                             p->LaneSaved[k] & (TBT_LANE1_TLW_MASK | TBT_LANE1_LB)) != TBT_OK ||
                tbtU4LaneRd(cm, h, p->Lane0 + k, &v[k]) != TBT_OK ||
                (v[k] & TBT_LANE1_TLW_MASK) != (p->LaneSaved[k] & TBT_LANE1_TLW_MASK)) {
                goto bad;
            }
        }
        p->LaneWritten = FALSE;
    }
    if (tbtU4LaneRd(cm, h, p->Lane0, &v[0]) != TBT_OK ||
        tbtU4LaneRd(cm, h, p->Lane0 + 1, &v[1]) != TBT_OK) {
        goto bad;
    }
    st = TBT_LANE1_STATE(v[1]);
    if (st == TBT_LANE_STATE_CLD || st == TBT_LANE_STATE_DISABLED ||
        TBT_LANE1_NLW(v[0]) == TBT_LANE_WIDTH_X2) {
        return;
    }
    if (st != TBT_LANE_STATE_CL0 || TBT_LANE1_NLW(v[0]) != TBT_LANE_WIDTH_X1) {
        goto bad;
    }
    for (k = 0; k < 2; k++) {
        /* 6.3: the host's lane adapters at their defaults - Target Link
         * Width single, no Lane Bonding, no CL state enabled. */
        if ((v[k] & TBT_LANE1_TLW_MASK) != TBT_LANE1_TLW_SINGLE ||
            (v[k] & (TBT_LANE1_LB | TBT_LANE1_CLX_EN)) != 0) {
            tbtU4Log(cm, "u4.bond.not-default", (k << 31) | v[k]);
            goto bad;
        }
    }
    /* 6.3: the link's TMU off (Guide 7.5) - this driver never enables it
     * on an inter-domain link (no timesync entry, section 13), and Disable
     * Time Sync on the host's lane 0 makes it so whatever was found. */
    if (tbtU4TmuAdp(cm, h, p->Lane0, TBT_TMU_ADP_CS_6, 0, TBT_TMU_ADP6_DTS) != TBT_OK) {
        goto bad;
    }
    p->Lane1Disabled = TRUE;
    if (tbtU4LaneRmw(cm, h, p->Lane0 + 1, 0, TBT_LANE1_LD) != TBT_OK ||
        !tbtU4WaitLane(cm, p, p->Generation, h, p->Lane0 + 1, TBT_LANE_STATE_DISABLED,
                       TBT_U4_LANE_DISABLE_US)) {
        goto bad;
    }
    cm->Counters.Lane1Disabled++;
    return;

bad:
    x->LinkBad = TRUE;
    tbtU4Log(cm, "cm.xdp.lane1", p->Index);
}

/*
 * One path entry of section 5.1, one read of its two DWs and one write of
 * both (record 22 sections 6.2.2 item 4 and 6.8): output adapter and
 * HopID, Valid, priority 4 (Guide Table 6-1), weight 1, ESE 0. A lane
 * adapter's ingress (`lane`) gets `credits`, IFC 1, EFC 0, ISE 0; the host
 * interface adapter's keeps its credits, IFC and ISE as read (Spec Table
 * 8-23 and its note) and gets EFC 1.
 */
static LONG tbtU4XdEntry(PTBT_U4CM cm, ULONG adp, ULONG hop, ULONG outAdp, ULONG outHop,
                         ULONG credits, BOOLEAN lane)
{
    const TBT_ROUTE *r = &cm->HostRouter.Route;
    ULONG e[2];
    LONG rc;

    rc = tbtU4RdN(cm, r, TBT_CS_PATH, adp, 2 * hop, 2, e);
    if (rc != TBT_OK) {
        return rc;
    }
    e[0] &= ~(TBT_PATH0_OUT_HOP_MASK | TBT_PATH0_OUT_ADP_MASK | 0x01000000UL |
              TBT_PATH0_VALID);
    e[0] |= (outHop & TBT_PATH0_OUT_HOP_MASK) |
            ((outAdp << TBT_PATH0_OUT_ADP_SHIFT) & TBT_PATH0_OUT_ADP_MASK) |
            TBT_PATH0_VALID;
    e[1] &= ~(TBT_PATH1_WEIGHT_MASK | TBT_PATH1_PRIO_MASK | TBT_PATH1_EFC |
              TBT_PATH1_ESE | TBT_PATH1_RSVDZ29);
    e[1] |= 1UL | (TBT_U4_XD_PRIORITY << TBT_PATH1_PRIO_SHIFT);
    if (lane) {
        e[0] = (e[0] & ~TBT_PATH0_CREDITS_MASK) |
               ((credits << TBT_PATH0_CREDITS_SHIFT) & TBT_PATH0_CREDITS_MASK);
        e[1] = (e[1] & ~TBT_PATH1_ISE) | TBT_PATH1_IFC;
    } else {
        e[1] |= TBT_PATH1_EFC;
    }
    return tbtU4WrN(cm, r, TBT_CS_PATH, adp, 2 * hop, 2, e);
}

/*
 * One owed entry at `adp`, HopID `hop`, of the service that sent it to
 * `outAdp` at `outHop`: read first, and invalidated (tbtU4Invalidate, its
 * Pending Packets waited for, quarantined if they never drain) only while
 * it still reads Valid with that output; one that reads invalid, or valid
 * with another output (no longer this service's: left untouched, record
 * 22 section 7.7's rule for what is not established as one's own), is owed
 * no more. TRUE when it is owed no more; FALSE when it could not be read,
 * or the access closed during the invalidation: still owed.
 */
static BOOLEAN tbtU4XdOwedOne(PTBT_U4CM cm, PTBT_U4_PORT p, ULONG adp, ULONG hop,
                              ULONG outAdp, ULONG outHop)
{
    ULONG e[2];
    ULONG i;
    PTBT_U4_QENTRY q;

    if (tbtU4RdN(cm, &cm->HostRouter.Route, TBT_CS_PATH, adp, 2 * hop, 2, e) != TBT_OK) {
        return FALSE;
    }
    if ((e[0] & TBT_PATH0_VALID) == 0) {
        return TRUE;
    }
    if (!tbtU4EntryGoes(e, outAdp, outHop)) {
        tbtU4Log(cm, "cm.net.owed.other", (adp << 8) | hop);
        return TRUE;
    }
    if (tbtU4Invalidate(cm, p->Index, &cm->HostRouter.Route, adp, hop)) {
        return TRUE;
    }
    /* Not invalidated: a quarantine record holds the entry now (its drain
     * failed, or the access closed before the entry could be read, or one
     * was already held). The read above established it as this service's,
     * so a record without an output identity is given this one: its retry
     * then knows the entry as this driver wrote it (record 22 section 7.7,
     * TBT_U4_QENTRY.Reserve), and the quarantine owns it from here. No
     * record: still owed. */
    for (i = 0; i < TBT_U4_QUARANTINE; i++) {
        q = &cm->Quarantine[i];
        if (q->Used && !q->Enable && q->Adapter == adp && q->Hop == hop &&
            TbtRouteEqual(&q->Route, &cm->HostRouter.Route)) {
            if (q->OutAdapter == 0 && q->OutHop == 0) {
                q->OutAdapter = outAdp;
                q->OutHop = outHop;
            }
            return TRUE;
        }
    }
    return FALSE;
}

/* Section 5.1's order down for the owed entries: the transmit entry, then
 * the receive entry. Nothing while there is no access. */
static VOID tbtU4XdOwedDown(PTBT_U4CM cm, PTBT_U4_PORT p)
{
    TBT_U4_XD_OWED *o = &cm->XdOwed[p->Index - 1];

    if (tbtU4Off(cm)) {
        return;
    }
    if (o->Tx && tbtU4XdOwedOne(cm, p, o->Hi, o->Ring, p->Lane0, o->TxHop)) {
        o->Tx = FALSE;
    }
    if (o->Rx && !o->Tx && tbtU4XdOwedOne(cm, p, p->Lane0, o->RxHop, o->Hi, o->Ring)) {
        o->Rx = FALSE;
    }
}

/* The two entries handed to the owed record with their identities, then
 * taken down from it (at once, or by the idle tick where that cannot be
 * done now: no access, an unread entry). */
static VOID tbtU4XdPathsDown(PTBT_U4CM cm, PTBT_U4_PORT p, PTBT_U4_XD x)
{
    TBT_U4_XD_OWED *o = &cm->XdOwed[p->Index - 1];

    if (x->TxWritten) {
        o->Tx = TRUE;
        o->Hi = x->Hi;
        o->Ring = x->Ring;
        o->TxHop = x->TxHop;
        x->TxWritten = FALSE;
    }
    if (x->RxWritten) {
        o->Rx = TRUE;
        o->Hi = x->Hi;
        o->Ring = x->Ring;
        o->RxHop = x->RxHop;
        x->RxWritten = FALSE;
    }
    tbtU4XdOwedDown(cm, p);
}

/*
 * Section 5.1's order up: the rings (the driver's, TBT_U4_NET.RingsUp),
 * the receive path, the transmit path, then the link up. One service per
 * NHI (section 13): refused while another port's is up. Credits for the
 * receive entry at the lane: the lesser of what the lane has left and the
 * host router's baMaxHI (14 without one), at least 1 (record 22 section
 * 6.8's accounting; [linux] tb_dma_init_rx_path).
 */
static LONG tbtU4XdUp(PVOID context, ULONG txHop, ULONG rxHop)
{
    PTBT_U4_XD x = (PTBT_U4_XD)context;
    PTBT_U4CM cm = x->Cm;
    PTBT_U4_PORT p = tbtU4XdPort(x);
    TBT_U4_USAGE u;
    TBT_U4_USAGE uh;
    ULONG avail;
    ULONG want;
    ULONG credits;
    BOOLEAN e2e;
    LONG rc;

    if (tbtU4XdOtherUp(cm, x)) {
        tbtU4Log(cm, "cm.net.busy", p->Index);
        return TBT_E_STATE;
    }
    if (cm->XdOwed[p->Index - 1].Tx || cm->XdOwed[p->Index - 1].Rx) {
        tbtU4Log(cm, "cm.net.owed", p->Index);   /* an earlier service's entries */
        return TBT_E_STATE;
    }
    if (x->LinkBad || !x->PidOk) {
        tbtU4Log(cm, "cm.net.link", (p->Index << 8) | (x->PidOk ? 0UL : 1UL));
        return TBT_E_STATE;
    }
    /* Every HopID and the ring free, live or quarantined (tbtU4ScanLane
     * counts both): the receive entry's input HopID on lane 0 and its
     * output at the host interface, the transmit entry's input there and
     * its output HopID on lane 0. */
    if (rxHop < TBT_HOP_FIRST || rxHop > TBT_HOP_MAX || txHop < TBT_HOP_FIRST ||
        txHop > TBT_HOP_MAX ||
        tbtU4ScanLane(cm, &cm->HostRouter, p->Lane0, &u) != TBT_OK ||
        tbtU4ScanLane(cm, &cm->HostRouter, x->Hi, &uh) != TBT_OK ||
        tbtU4LaneAvail(cm, &cm->HostRouter, p->Lane0, &u, &avail) != TBT_OK) {
        return TBT_E_STATE;
    }
    if (TBT_U4_BIT_GET(u.InUse, rxHop) || TBT_U4_BIT_GET(u.OutUse, txHop) ||
        TBT_U4_BIT_GET(uh.InUse, TBT_U4_XD_RING) || TBT_U4_BIT_GET(uh.OutUse, TBT_U4_XD_RING)) {
        tbtU4Log(cm, "cm.net.hop.busy", (p->Index << 16) | (txHop << 8) | rxHop);
        return TBT_E_STATE;
    }
    want = p->HostBaHi != 0 ? p->HostBaHi : TBT_U4_XD_CREDITS;
    credits = avail < want ? avail : want;
    if (credits < 1) {
        tbtU4Log(cm, "cm.net.credits", (p->Index << 16) | avail);
        return TBT_E_STATE;
    }
    e2e = (BOOLEAN)((cm->NetPrtcstns & x->Peer.RemoteInfo.Prtcstns &
                     TBT_NET_STNS_E2E) != 0);
    x->Ring = TBT_U4_XD_RING;
    x->TxHop = txHop;
    x->RxHop = rxHop;
    if (cm->Net.RingsUp != NULL &&
        cm->Net.RingsUp(cm->Net.Context, p->Index, x->Ring, e2e) != TBT_OK) {
        tbtU4Log(cm, "cm.net.rings", p->Index);
        return TBT_E_STATE;
    }
    x->RingsUp = TRUE;
    x->RxWritten = TRUE;        /* owned before the write */
    rc = tbtU4XdEntry(cm, p->Lane0, rxHop, x->Hi, x->Ring, credits, TRUE);
    if (rc == TBT_OK) {
        x->TxWritten = TRUE;
        rc = tbtU4XdEntry(cm, x->Hi, x->Ring, p->Lane0, txHop, 0, FALSE);
    }
    if (rc != TBT_OK) {
        tbtU4Log(cm, "cm.net.path", (ULONG)rc);
        tbtU4XdPathsDown(cm, p, x);
        x->RingsUp = FALSE;
        if (cm->Net.RingsDown != NULL) {
            cm->Net.RingsDown(cm->Net.Context, p->Index, (BOOLEAN)tbtU4Off(cm));
        }
        return TBT_E_STATE;
    }
    tbtU4Log(cm, "cm.net.paths", (p->Index << 24) | (txHop << 16) | (rxHop << 8) | credits);
    x->LinkUp = TRUE;
    if (cm->Net.Link != NULL) {
        cm->Net.Link(cm->Net.Context, p->Index, TRUE, x->Peer.PeerMac);
    }
    tbtU4XdOffer(cm);           /* withdrawn from every other peer */
    return TBT_OK;
}

/* Section 5.1's order down: the link down (the driver stops its sends and
 * drains its transmit ring, at most 500 ms), the entries, the rings. */
static VOID tbtU4XdDown(PVOID context, BOOLEAN peerGone)
{
    PTBT_U4_XD x = (PTBT_U4_XD)context;
    PTBT_U4CM cm = x->Cm;
    PTBT_U4_PORT p = tbtU4XdPort(x);

    tbtU4Log(cm, "cm.net.down", (p->Index << 8) | (peerGone ? 1UL : 0UL));
    if (x->LinkUp) {
        x->LinkUp = FALSE;
        if (cm->Net.Link != NULL) {
            cm->Net.Link(cm->Net.Context, p->Index, FALSE, NULL);
        }
    }
    tbtU4XdPathsDown(cm, p, x);
    if (x->RingsUp) {
        x->RingsUp = FALSE;
        if (cm->Net.RingsDown != NULL) {
            cm->Net.RingsDown(cm->Net.Context, p->Index, (BOOLEAN)tbtU4Off(cm));
        }
    }
    tbtU4XdOffer(cm);           /* offered again to the others */
}

static VOID tbtU4XdReady(PVOID context, const TBT_XDP_REMOTE *remote)
{
    PTBT_U4_XD x = (PTBT_U4_XD)context;

    tbtU4Log(x->Cm, "cm.xdp.peer", (x->Port << 24) | ((remote->VendorId & 0xFFFUL) << 12) |
                                   (remote->DeviceId & 0xFFFUL));
}

/* ---- the port's peer --------------------------------------------------- */

/*
 * Section 3.3 step 4 and 3.5: the peer record of a port, its engine given
 * this domain's UUID, the published block (vendorid 0 "xHCI98", deviceid 1
 * "xhcicm", the package version, maxhopid the lane's Max Input HopID, the
 * network directory while the network driver holds it open), the lane's
 * Max Output HopID and the HopIDs in use on it, and whether bonding is
 * possible: XhciCmLaneBonding 1, a Gen 2 or 3 link at x1 and both host
 * lane adapters listing x2 (the peer's side the Link State exchange
 * reads). A link that cannot bond keeps lane 1 disabled once the peer is
 * known (tbtU4XdSync). FALSE when it cannot start: not on a Thunderbolt
 * host router (its link controller's inter-domain setting is not
 * designed here), or a read failing.
 */
static BOOLEAN tbtU4XdBegin(PTBT_U4CM cm, PTBT_U4_PORT p, PTBT_U4_XD x)
{
    PTBT_U4_ROUTER h;
    TBT_XDP_OPS ops;
    TBT_XDP_LOCAL props;
    TBT_ROUTE route;
    TBT_U4_USAGE u;
    ULONG inUse[4];
    ULONG v[2];
    ULONG lane;
    ULONG speed;
    ULONG i;
    BOOLEAN gen23;
    BOOLEAN bond;

    h = &cm->HostRouter;
    if (h->Tbt3 || p->Lane0 == 0 || p->Lane0 + 1 >= TBT_U4_MAX_ADP ||
        h->Adp[p->Lane0].CapLane == 0) {
        tbtU4Log(cm, "cm.xdp.host", p->Index);
        return FALSE;
    }
    tbtU4Zero(x, sizeof(*x));
    x->Cm = cm;
    x->Port = p->Index;
    x->Gen = p->Generation;
    x->Hi = tbtU4XdHi(cm);
    if (x->Hi == 0 || !tbtU4XdIdentity(cm) ||
        tbtU4ScanLane(cm, h, p->Lane0, &u) != TBT_OK ||
        tbtU4LaneRd(cm, h, p->Lane0, &lane) != TBT_OK) {
        tbtU4Log(cm, "cm.xdp.begin.fail", p->Index);
        return FALSE;
    }
    for (i = 0; i < 4; i++) {
        inUse[i] = u.InUse[i] | u.OutUse[i];
    }
    speed = TBT_LANE1_SPEED(lane);
    gen23 = (BOOLEAN)((speed == TBT_LANE_SPEED_GEN2 || speed == TBT_LANE_SPEED_GEN3) &&
                      TBT_LANE1_NLW(lane) == TBT_LANE_WIDTH_X1 &&
                      h->Adp[p->Lane0 + 1].CapLane != 0);
    bond = FALSE;
    if (gen23 && cm->Config.LaneBonding &&
        tbtU4Rd(cm, &h->Route, TBT_CS_ADAPTER, p->Lane0,
                h->Adp[p->Lane0].CapLane + TBT_LANE_CS_0, &v[0]) == TBT_OK &&
        tbtU4Rd(cm, &h->Route, TBT_CS_ADAPTER, p->Lane0 + 1,
                h->Adp[p->Lane0 + 1].CapLane + TBT_LANE_CS_0, &v[1]) == TBT_OK &&
        (v[0] & TBT_LANE0_SLW_X2) != 0 && (v[1] & TBT_LANE0_SLW_X2) != 0) {
        bond = TRUE;
    }
    x->Lane1Owed = (BOOLEAN)(gen23 && !bond);

    tbtU4Zero(&ops, sizeof(ops));
    ops.Context = x;
    ops.Send = tbtU4XdSend;
    ops.NowMs = tbtU4XdNow;
    ops.Log = tbtU4XdLog;
    ops.LinkRead = tbtU4XdLinkRead;
    ops.LinkSetTarget = tbtU4XdLinkSetTarget;
    ops.LinkBond = tbtU4XdLinkBond;
    ops.LinkLane1Off = tbtU4XdLane1Off;
    ops.ServiceUp = tbtU4XdUp;
    ops.ServiceDown = tbtU4XdDown;
    ops.PeerReady = tbtU4XdReady;
    tbtU4Zero(&props, sizeof(props));
    props.VendorId = 0;
    props.VendorText = "xHCI98";
    props.DeviceId = 1;
    props.DeviceText = "xhcicm";
    props.DeviceRevision = cm->Config.NetRevision;
    props.MaxHopId = h->Adp[p->Lane0].MaxInHop;
    props.Network = (BOOLEAN)(cm->NetWanted && !tbtU4XdOtherUp(cm, x));
    props.Prtcstns = cm->NetPrtcstns;
    route.Hi = 0;
    route.Lo = p->Lane0;
    TbtXdpPeerInit(&x->Peer, &ops, &route, &cm->XdLocal, NULL, FALSE, bond, &props,
                   cm->XdMac, h->Adp[p->Lane0].MaxOutHop, inUse);
    x->Used = TRUE;
    if (p->Index <= 32 && (cm->XdPidOwed & (1UL << (p->Index - 1))) != 0) {
        /* An earlier record's clear still owed: this record owns it. */
        cm->XdPidOwed &= ~(1UL << (p->Index - 1));
        x->PidSet = TRUE;
    }
    TbtXdpPeerStart(&x->Peer);
    tbtU4Log(cm, "cm.xdp.begin", (p->Index << 8) | (bond ? 1UL : 0UL));
    return TRUE;
}

/*
 * Section 3.8 for the port's peer: the engine ended (with a Logout first
 * where `gone` is FALSE and the service is up: sent once, not waited for,
 * since IDS 5.1.2 takes no answer as one), its paths and rings down, the
 * Inter-Domain bit cleared, the record freed. The lanes the bonding or the
 * lane 1 disable changed are the teardown's host restore's.
 */
static VOID tbtU4XdEnd(PTBT_U4CM cm, PTBT_U4_PORT p, BOOLEAN gone)
{
    PTBT_U4_XD x;
    ULONG prev;
    ULONG in;

    x = tbtU4XdOf(cm, p);
    if (x == NULL || !x->Used) {
        return;
    }
    prev = cm->ActivePort;
    cm->ActivePort = p->Index;
    if (tbtU4Off(cm)) {
        gone = TRUE;
    }
    in = cm->XdIn;
    cm->XdIn = p->Index;
    if (!gone) {
        TbtXdpPeerEnd(&x->Peer, FALSE);
        TbtXdpPeerTick(&x->Peer);
    }
    TbtXdpPeerEnd(&x->Peer, TRUE);
    cm->XdIn = in;
    if (!tbtU4Off(cm)) {
        tbtU4XdPid(cm, p, x, FALSE);
    }
    if (x->PidSet && p->Index <= 32) {
        cm->XdPidOwed |= 1UL << (p->Index - 1);
    }
    tbtU4Log(cm, "cm.xdp.end", (p->Index << 8) | (gone ? 1UL : 0UL));
    tbtU4Zero(x, sizeof(*x));
    cm->ActivePort = prev;
}

static VOID tbtU4XdEndAll(PTBT_U4CM cm, BOOLEAN gone)
{
    ULONG i;

    for (i = 0; i < cm->PortCount && i < TBT_U4_MAX_PORTS; i++) {
        tbtU4XdEnd(cm, &cm->Ports[i], gone);
    }
}

/* The engine's state as the port's (record 22 section 7.2's table with
 * record 35's row): a loop UNSUPPORTED with WHY_LOOP and Disable Time Sync
 * on the port's lane 0 (Guide 3.3); no answer to the UUID Request
 * UNSUPPORTED with WHY_ENUM_ERROR as before the feature (section 3.3 step
 * 2), the record kept so the peer's own request restarts it; anything else
 * TBT_U4_XDOMAIN, the Inter-Domain bit set once the peer is known, and
 * lane 1 disabled where the link stays single. */
static VOID tbtU4XdSync(PTBT_U4CM cm, PTBT_U4_PORT p, PTBT_U4_XD x)
{
    ULONG st;

    tbtU4XdLinkSync(cm, x);
    st = x->Peer.State;
    if (st == TBT_XDP_S_LOOP) {
        if (p->State != TBT_U4_UNSUPPORTED || p->Why != TBT_U4_WHY_LOOP) {
            p->State = TBT_U4_UNSUPPORTED;
            p->Why = TBT_U4_WHY_LOOP;
            cm->XdLoops++;
            (void)tbtU4TmuAdp(cm, &cm->HostRouter, p->Lane0, TBT_TMU_ADP_CS_6, 0,
                              TBT_TMU_ADP6_DTS);
            tbtU4XdPid(cm, p, x, FALSE);
            tbtU4Log(cm, "u4.port.state", (p->Index << 8) | p->State | (p->Why << 16));
        }
        return;
    }
    if (st == TBT_XDP_S_ERROR && !x->Peer.HaveRemote) {
        if (p->State != TBT_U4_UNSUPPORTED) {
            p->State = TBT_U4_UNSUPPORTED;
            p->Why = TBT_U4_WHY_ENUM_ERROR;
            tbtU4Log(cm, "cm.xdp.uuid.none", p->Index);
        }
        return;
    }
    if (p->State != TBT_U4_XDOMAIN) {
        p->State = TBT_U4_XDOMAIN;
        p->Why = TBT_U4_WHY_XDOMAIN;
        tbtU4Log(cm, "u4.port.state", (p->Index << 8) | p->State | (p->Why << 16));
    }
    if (!x->Peer.HaveRemote) {
        return;
    }
    if (!x->PidOk) {
        tbtU4XdPid(cm, p, x, TRUE);     /* again at each tick until answered */
    }
    if (x->Lane1Owed && st != TBT_XDP_S_UUID && st != TBT_XDP_S_LINK &&
        st != TBT_XDP_S_BOND_LOW && st != TBT_XDP_S_BOND_HIGH) {
        x->Lane1Owed = FALSE;
        tbtU4XdLane1Off(x);
    }
}

/* Whether a port may carry a peer: XDOMAIN, or UNSUPPORTED by the UUID
 * step or the loop rule (its record kept), on a running instance. */
static BOOLEAN tbtU4XdPortOk(const TBT_U4CM *cm, const TBT_U4_PORT *p)
{
    return (BOOLEAN)(cm->Config.Net &&
                     (p->State == TBT_U4_XDOMAIN ||
                      (p->State == TBT_U4_UNSUPPORTED &&
                       (p->Why == TBT_U4_WHY_ENUM_ERROR || p->Why == TBT_U4_WHY_LOOP))));
}

/* A record of an earlier connection of its port: a teardown nested at a
 * safe point (record 22 section 7.4) ended that connection without this
 * section, and the port may already be connected again. Ended as gone. */
static VOID tbtU4XdStale(PTBT_U4CM cm, PTBT_U4_PORT p, PTBT_U4_XD x)
{
    if (x->Used && x->Gen != p->Generation) {
        tbtU4XdEnd(cm, p, TRUE);
    }
}

/* At the top of each port's share of the idle tick, before its host
 * restore and its check: a stale record ended, and the entries owed by a
 * service with no record left taken down, so no lane is restored and no
 * connection made while this service's paths stand. */
static VOID tbtU4XdPre(PTBT_U4CM cm, PTBT_U4_PORT p)
{
    PTBT_U4_XD x;

    x = tbtU4XdOf(cm, p);
    if (x == NULL || tbtU4Off(cm)) {
        return;
    }
    tbtU4XdStale(cm, p, x);
    tbtU4XdOwedDown(cm, p);     /* a downed service's, whether or not its
                                 * peer's record lives on */
}

/* The idle tick's share, after the port's check: an owed Inter-Domain
 * clear retried while no peer runs on the port; the record started for a
 * port that has just become XDOMAIN, the engine's resends and timeouts,
 * the port's state from the engine's. A record whose port has left those
 * states (a teardown elsewhere) is ended as gone. */
static VOID tbtU4XdService(PTBT_U4CM cm, PTBT_U4_PORT p)
{
    PTBT_U4_XD x;
    ULONG prev;
    ULONG bit;

    x = tbtU4XdOf(cm, p);
    if (x == NULL || tbtU4Off(cm)) {
        return;
    }
    tbtU4XdPre(cm, p);
    bit = p->Index <= 32 ? 1UL << (p->Index - 1) : 0;
    if ((cm->XdPidOwed & bit) != 0 && !x->Used && tbtU4XdPidClear(cm, p)) {
        cm->XdPidOwed &= ~bit;
    }
    if (!tbtU4XdPortOk(cm, p)) {
        tbtU4XdEnd(cm, p, TRUE);
        return;
    }
    if (!x->Used && p->State != TBT_U4_XDOMAIN) {
        return;     /* left UNSUPPORTED: only the peer's own packet restarts it */
    }
    prev = cm->ActivePort;
    cm->ActivePort = p->Index;
    if (!x->Used && !tbtU4XdBegin(cm, p, x)) {
        tbtU4Zero(x, sizeof(*x));
        p->State = TBT_U4_UNSUPPORTED;
        p->Why = TBT_U4_WHY_ENUM_ERROR;
        tbtU4Log(cm, "u4.port.state", (p->Index << 8) | p->State | (p->Why << 16));
    } else {
        cm->XdIn = p->Index;
        TbtXdpPeerTick(&x->Peer);
        cm->XdIn = 0;
        tbtU4XdSync(cm, p, x);
    }
    cm->ActivePort = prev;
}

/*
 * The XDomain sink (tbt_ring.h TBT_XDOMAIN_SINK): called inside the ring's
 * receive, whoever reads it, so it only records - a small queue the thread
 * empties after the read in progress (tbtU4XdTake). With XhciCmNet 0 the
 * packet is counted and dropped unanswered (section 3.6).
 */
static VOID tbtU4XdSink(PVOID context, const TBT_FRAME *frame)
{
    PTBT_U4CM cm = (PTBT_U4CM)context;

    if (!cm->Config.Net) {
        if (cm->XdIgnored++ == 0) {
            tbtU4Log(cm, "cm.xdp.ignored", frame->Pdf);    /* the first only */
        }
        return;
    }
    if (cm->XdCount >= TBT_U4_XD_QUEUE) {
        cm->XdOverflow++;
        return;
    }
    cm->XdQ[(cm->XdHead + cm->XdCount) % TBT_U4_XD_QUEUE] = *frame;
    cm->XdCount++;
    cm->XdQueued++;
}

/* The queue emptied, oldest first: each packet to the peer of the port
 * whose lane 0 its route names (depth 1), a record started for a port
 * left UNSUPPORTED by the UUID step (the peer's request restarts it,
 * section 3.3 step 2); any other route counted. Packets queued while one
 * is answered are taken in the same pass. */
/*
 * Run-45 45-E.15 [obs]: an Alpine Ridge firmware CM at the far end sends
 * one UUID Request about 10 ms after the link is up, with no retry seen,
 * and tells its host of this one only once that is answered - before this
 * side's port machine has read ERR_ENUM and classified the port. With
 * XhciCmNet 1, a UUID Request on a port's lane 0 that no record serves is
 * answered here with this domain's UUID, no record started, whatever the
 * port's state - it may arrive with the hot plug event, before the port
 * machine has seen the connection; the classification then starts the
 * discovery as before. TRUE when it is taken: answered, or owed to the
 * next takes when this domain's UUID cannot be read or the ring refuses
 * the answer (the far firmware does not resend it).
 */
static VOID tbtU4XdEarlySend(PTBT_U4CM cm, PTBT_U4_PORT p)
{
    PTBT_U4_XD_EARLY e = &cm->XdEarly[p->Index - 1];
    ULONG buf[TBT_XDP_MAX_WORDS];
    TBT_ROUTE r;
    ULONG words;
    ULONG cs4;

    /* The request comes with the link, while lane 0 is still Locked: the
     * host router refuses a packet out through a Locked adapter with
     * ERR_LOCK (Spec 8.2.2.1, ADP_CS_4), and the answer would be lost
     * though the ring took it (read on the emulator, run-45 "45.59 (legs,
     * continued)"). One taken before this driver has cleared that Lock,
     * or behind an unplug not yet checked (a quick re-plug leaves the last
     * connection's Unlocked standing while the unplug set the Lock again),
     * is for the port's next connection: it waits, owed and no try
     * counted, until that connection is up with lane 0 unlocked
     * (tbtU4Router) and no unplug pending, and is dropped if another
     * connection has come since. */
    if (p->Generation - e->Gen > 1UL) {
        e->Owed = FALSE;
        tbtU4Log(cm, "cm.xdp.uuid.early.stale", p->Index);
        return;
    }
    /* Sent only on a port this driver has unlocked whose lane 0 reads
     * unlocked now: the flags alone can describe a connection a
     * quick re-plug has replaced, so the Lock is read before each try.
     * Until then it waits; no try counted. At worst an answer held across
     * a re-plug reaches the next connection, where it is a UUID Response
     * to no request of that peer's, which a receiver drops ([linux]
     * ctl.c takes a response only for a request outstanding), or one with this
     * domain's same UUID for that peer's own pending request. */
    if (!p->Unlocked ||
        tbtU4Rd(cm, &cm->HostRouter.Route, TBT_CS_ADAPTER, p->Lane0, TBT_ADP_CS_4,
                &cs4) != TBT_OK) {
        return;
    }
    if ((cs4 & TBT_ADP4_LOCK) != 0) {
        /* Locked again under a flag that still says unlocked: the
         * connection the flag describes is gone, and the answer is the
         * next one's (its teardown keeps it). */
        e->NextConn = TRUE;
        return;
    }
    r.Hi = 0;
    r.Lo = p->Lane0;
    words = 0;
    if (tbtU4XdIdentity(cm)) {
        words = TbtXdpBuildUuidResponse(buf, &r, e->Sn, &cm->XdLocal,
                                        (BOOLEAN)(e->Type == TBT_XDP_UUID_REQ));
    }
    if (words != 0 && TbtRingSend(cm->Ring, TBT_PDF_XDOMAIN_RESP, buf, words) == TBT_OK) {
        e->Owed = FALSE;
        tbtU4Log(cm, "cm.xdp.uuid.early", (p->Index << 8) | p->State);
        return;
    }
    e->Tries++;
    if (e->Tries > TBT_XDP_RESENDS) {
        e->Owed = FALSE;
        tbtU4Log(cm, "cm.xdp.uuid.early.fail", p->Index);
    }
}

static BOOLEAN tbtU4XdEarlyUuid(PTBT_U4CM cm, PTBT_U4_PORT p, const TBT_FRAME *f)
{
    PTBT_U4_XD_EARLY e;
    TBT_XDP_PKT k;

    if (!cm->Config.Net || f->Pdf != TBT_PDF_XDOMAIN_REQ || p->Index < 1 ||
        p->Index > TBT_U4_MAX_PORTS ||
        TbtXdpDecode(f->Pdf, f->Dw, f->Words, &k) != TBT_XDP_K_DISCOVERY || k.Response ||
        (k.Type != TBT_XDP_UUID_REQ && k.Type != TBT_XDP_UUID_REQ_TBT3)) {
        return FALSE;
    }
    e = &cm->XdEarly[p->Index - 1];
    e->Owed = TRUE;
    e->Sn = k.Sn;
    e->Type = k.Type;
    e->Tries = 0;
    e->Gen = p->Generation;
    e->NextConn = (BOOLEAN)(!p->Unlocked || p->UnplugPending);
    tbtU4XdEarlySend(cm, p);
    return TRUE;
}

static VOID tbtU4XdEarlyDrop(PTBT_U4CM cm, PTBT_U4_PORT p)
{
    if (p->Index >= 1 && p->Index <= TBT_U4_MAX_PORTS) {
        cm->XdEarly[p->Index - 1].Owed = FALSE;
    }
}

/* At a teardown of the port's connection: an answer owed to that
 * connection goes with it; one held for the next connection (taken after
 * this one's unplug, before the check) stays. */
static VOID tbtU4XdEarlyEnd(PTBT_U4CM cm, PTBT_U4_PORT p)
{
    PTBT_U4_XD_EARLY e;

    if (p->Index < 1 || p->Index > TBT_U4_MAX_PORTS) {
        return;
    }
    e = &cm->XdEarly[p->Index - 1];
    if (e->Owed && (!e->NextConn || p->Generation != e->Gen)) {
        e->Owed = FALSE;
    }
}

/* The early answers still owed, at each idle poll (TbtU4Poll), until sent
 * or out of tries: a record started on the port since never saw that
 * request, so its engine does not answer it. One held for the Lock waits
 * for this driver's unlock of that port. Not at a ring wake: the Lock
 * read of a held answer completes on the ring and would wake it again. */
static VOID tbtU4XdEarlyRetry(PTBT_U4CM cm)
{
    ULONG i;

    if (tbtU4Off(cm) || cm->Suspended) {
        return;
    }
    for (i = 0; i < cm->PortCount && i < TBT_U4_MAX_PORTS; i++) {
        if (cm->XdEarly[i].Owed) {
            tbtU4XdEarlySend(cm, &cm->Ports[i]);
        }
    }
}

static VOID tbtU4XdTake(PTBT_U4CM cm)
{
    TBT_FRAME f;
    TBT_ROUTE r;
    PTBT_U4_PORT p;
    PTBT_U4_XD x;
    BOOLEAN lane1;
    ULONG prev;

    if (cm->XdTaking) {
        return;
    }
    cm->XdTaking = TRUE;
    while (cm->XdCount != 0) {
        f = cm->XdQ[cm->XdHead];
        cm->XdHead = (cm->XdHead + 1) % TBT_U4_XD_QUEUE;
        cm->XdCount--;
        if (tbtU4Off(cm) || cm->Suspended || f.Words < 2) {
            continue;
        }
        r.Hi = f.Dw[0] & 0x7FFFFFFFUL;
        r.Lo = f.Dw[1];
        p = NULL;
        if (r.Hi == 0 && r.Lo != 0 && r.Lo < TBT_U4_MAX_ADP) {
            p = tbtU4PortByLane(cm, r.Lo, &lane1);
            if (p != NULL && lane1) {
                p = NULL;
            }
        }
        x = tbtU4XdOf(cm, p);
        if (p != NULL && x != NULL && !x->Used && !tbtU4XdPortOk(cm, p) &&
            tbtU4XdEarlyUuid(cm, p, &f)) {
            continue;
        }
        if (p == NULL || x == NULL || !tbtU4XdPortOk(cm, p)) {
            cm->XdNoPeer++;
            continue;
        }
        tbtU4XdStale(cm, p, x);
        prev = cm->ActivePort;
        cm->ActivePort = p->Index;
        if (x->Used || tbtU4XdBegin(cm, p, x)) {
            cm->XdIn = p->Index;
            (void)TbtXdpPeerOnPacket(&x->Peer, f.Pdf, f.Dw, f.Words);
            cm->XdIn = 0;
            tbtU4XdSync(cm, p, x);
        } else {
            tbtU4Zero(x, sizeof(*x));
            cm->XdNoPeer++;
        }
        cm->ActivePort = prev;
    }
    cm->XdTaking = FALSE;
}

VOID TbtU4SetNet(PTBT_U4CM cm, const TBT_U4_NET *net)
{
    if (net != NULL) {
        cm->Net = *net;
    } else {
        tbtU4Zero(&cm->Net, sizeof(cm->Net));
    }
}

VOID TbtU4NetWanted(PTBT_U4CM cm, BOOLEAN wanted, ULONG prtcstns)
{
    cm->NetWanted = wanted;
    cm->NetPrtcstns = prtcstns;
    tbtU4XdOffer(cm);
}

BOOLEAN TbtU4NetIdentity(PTBT_U4CM cm, PUCHAR mac6)
{
    ULONG i;

    if (!cm->Config.Net || cm->Stage != TBT_U4_STAGE_RUNNING || tbtU4Off(cm) ||
        !tbtU4XdIdentity(cm)) {
        return FALSE;
    }
    for (i = 0; i < 6; i++) {
        mac6[i] = cm->XdMac[i];
    }
    return TRUE;
}

const TBT_XDP_PEER *TbtU4Peer(const TBT_U4CM *cm, ULONG port)
{
    if (port == 0 || port > TBT_U4_MAX_PORTS || !cm->Xd[port - 1].Used) {
        return NULL;
    }
    return &cm->Xd[port - 1].Peer;
}
