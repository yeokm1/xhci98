/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * tbt_u4cm.c - the USB4 software connection manager, its pure half (see
 * tbt_u4cm.h; roadmap-usb4.md tasks 40-A.3 and 40-A.8's pure part, design
 * record 22).
 *
 * Adapted, translated to C89 for Windows 98 SE and Windows 2000, from:
 *
 *   usb4-emu tools/usb4cm/core/u4cm.c (main 3c74189, read 2026-10-15):
 *     Copyright (c) 2026 usb4-emu contributors
 *     SPDX-License-Identifier: GPL-2.0-only
 *   - enumerate_router, discover_adapters, u4cm_host_init's port and
 *     protocol-adapter pairing, router_op, drom_read and read_drom,
 *     bond_lanes, u4cm_connect, config_valid, program_path,
 *     program_tunnel_paths, u4cm_pcie_tunnel, u4cm_usb3_tunnel.
 *
 *   Linux drivers/thunderbolt (mirror pinned at
 *   c6859eed755df351a3978b33cb92365f9b3e8f06, SPDX GPL-2.0), whose notices
 *   read:
 *     tb.c, switch.c, tunnel.c, path.c, cap.c, tb_regs.h:
 *       Copyright (c) 2014 Andreas Noever <andreas.noever@gmail.com>
 *       Copyright (C) 2018, Intel Corporation (tb.c, switch.c: 2019; the
 *       rest as each file states)
 *     usb4.c, tmu.c, lc.c, quirks.c: Copyright (C) 2019, Intel Corporation
 *     (quirks.c: Copyright (c) 2020 Intel Corporation)
 *   - the sequences, constants and register uses cited at each site below:
 *     tb_switch_configure, usb4_switch_setup, tb_plug_events_active,
 *     tb_switch_next_cap / tb_switch_find_vse_cap, usb4_switch_credits_init,
 *     tb_pci_init_credits, tb_available_credits, tb_path_activate,
 *     tb_tunnel_discover_pci / _usb3, tb_path_discover,
 *     usb4_usb3_port_allocate_bandwidth / _release_bandwidth,
 *     usb4_port_reset, tb_switch_lane_bonding_enable,
 *     tb_switch_tmu_post_time, tb_switch_tmu_enable (HiFi-Bi),
 *     tb_switch_tmu_disable, tb_lc_read_uuid, tb_lc_lane_bonding_possible,
 *     tb_lc_set_port_configured, tb_check_quirks.
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
 * Changed for xhci98 by Yeo Kheng Meng and Claude, 2026-10-15.
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

#define TBT_U4_LIVE(p, gen) ((p)->Generation == (gen) && !(p)->UnplugPending)

static const TBT_ROUTE tbtU4Root = { 0, 0 };

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

static LONG tbtU4Rd(PTBT_U4CM cm, const TBT_ROUTE *r, ULONG cs, ULONG adp,
                    ULONG addr, PULONG v)
{
    return TbtCtlRead(&cm->Ctl, r, cs, adp, addr, 1, v);
}

static LONG tbtU4RdN(PTBT_U4CM cm, const TBT_ROUTE *r, ULONG cs, ULONG adp,
                     ULONG addr, ULONG n, PULONG v)
{
    return TbtCtlRead(&cm->Ctl, r, cs, adp, addr, n, v);
}

static LONG tbtU4Wr(PTBT_U4CM cm, const TBT_ROUTE *r, ULONG cs, ULONG adp,
                    ULONG addr, ULONG v)
{
    return TbtCtlWrite(&cm->Ctl, r, cs, adp, addr, 1, &v);
}

static LONG tbtU4WrN(PTBT_U4CM cm, const TBT_ROUTE *r, ULONG cs, ULONG adp,
                     ULONG addr, ULONG n, const ULONG *v)
{
    return TbtCtlWrite(&cm->Ctl, r, cs, adp, addr, n, v);
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
        if (cm->Ports[i].Dev.Present &&
            TbtRouteEqual(&cm->Ports[i].Dev.Route, r)) {
            return &cm->Ports[i].Dev;
        }
    }
    return NULL;
}

/* A request that failed with ERR_CONN or ERR_LOCK on a device's route is
 * removal evidence (record 22 section 7.1 item 3). */
static BOOLEAN tbtU4Gone(PTBT_U4CM cm, LONG rc)
{
    return (BOOLEAN)(rc == TBT_E_NOTIFY &&
                     (cm->Ring->LastEvent == TBT_EV_ERR_CONN ||
                      cm->Ring->LastEvent == TBT_EV_ERR_LOCK));
}

static VOID tbtU4Queue(PTBT_U4CM cm, ULONG kind, PTBT_U4_PORT p)
{
    PTBT_U4_ACTION a;

    if (cm->ActionCount >= TBT_U4_ACTION_QUEUE) {
        cm->Counters.ActionOverflows++;
        cm->ResyncPending = TRUE;
        return;
    }
    a = &cm->Actions[(cm->ActionHead + cm->ActionCount) % TBT_U4_ACTION_QUEUE];
    a->Kind = kind;
    a->Port = p != NULL ? p->Index : 0;
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

/* ---------------------------------------------------------------------- */
/* The quirk table, record 22 section 6.10 (Linux quirks.c tb_quirks)      */
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

/* Dell WD19TB (Linux quirks.c, matched on the DROM's ids). */
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
static BOOLEAN tbtU4Evidence(PTBT_U4CM cm, PTBT_U4_PORT p, PULONG cls);

/* One step of a poll loop of the connection manager's own: the wait, then
 * the safe point. */
static VOID tbtU4Pause(PTBT_U4CM cm, PTBT_WAIT w)
{
    TbtWaitStep(w, cm->Host, TBT_U4_POLL_STEP_US);
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
    while (!TbtWaitDone(&w, cm->Host)) {
        tbtU4Pause(cm, &w);
    }
}

/* What a hot plug event or a note means for a port: a check, and for an
 * unplug or a disconnect on its route, the pending-unplug hint. */
static VOID tbtU4Hint(PTBT_U4CM cm, const TBT_ROUTE *route, ULONG adapter,
                      BOOLEAN unplug)
{
    PTBT_U4_PORT p;
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

    while (TbtCtlNextHotPlug(&cm->Ctl, &e)) {
        tbtU4Hint(cm, &e.Route, e.Adapter, e.Unplug);
    }
    while (TbtCtlNextNote(&cm->Ctl, &n)) {
        tbtU4Log(cm, "u4.note", (n.Code << 8) | n.Info);
        if (n.Code == TBT_EV_ERR_CONN || n.Code == TBT_EV_ERR_LOCK ||
            n.Code == TBT_EV_ERR_PLUG) {
            /* The adapter on the route that reported it, or the depth-1
             * router the route names. */
            if (TbtRouteDepth(&n.Route) == 0) {
                tbtU4Hint(cm, &n.Route, n.Info, (BOOLEAN)(n.Code != TBT_EV_ERR_PLUG));
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

    cm = (PTBT_U4CM)context;
    TbtCtlService(&cm->Ctl);
    tbtU4TakeQueues(cm);
    if (cm->InTeardown) {
        return;
    }
    for (i = 0; i < cm->PortCount; i++) {
        p = &cm->Ports[i];
        if (!p->UnplugPending || p->Index == cm->ActivePort ||
            cm->DeferredTail[i] || p->State == TBT_U4_EMPTY ||
            p->State == TBT_U4_TEARDOWN) {
            continue;
        }
        if (!tbtU4Evidence(cm, p, &cls)) {
            p->UnplugPending = FALSE;
            continue;
        }
        cm->Counters.UnplugsNested++;
        cm->InTeardown = TRUE;
        tbtU4TeardownHead(cm, p, TRUE);
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

static LONG tbtU4DromChunk(PTBT_U4CM cm, PTBT_U4_ROUTER rt, ULONG offset,
                           ULONG n, UCHAR *out)
{
    ULONG data[TBT_ROP_DATA_DWORDS];
    ULONG meta;
    ULONG i;
    LONG rc;

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
 * The USB4 DROM (the USB4 DROM Specification; Guide 2.5, 9.1): CRC8,
 * UID, CRC32C over bytes 13 on, Version, Data Length, entries from byte 16;
 * the ASCII vendor (generic entry type 1) and model (type 2) names kept.
 * A failure is logged and the device proceeds unnamed.
 */
static VOID tbtU4ReadDrom(PTBT_U4CM cm, PTBT_U4_ROUTER rt)
{
    UCHAR buf[TBT_U4_DROM_MAX];
    ULONG len;
    ULONG got;
    ULONG n;
    ULONG pos;
    ULONG crc;
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
    if (buf[13] != 3) {
        return;     /* a Thunderbolt 3 layout: names not parsed */
    }
    for (pos = 16; pos + 2 <= len && buf[pos] >= 2; pos += buf[pos]) {
        if (pos + buf[pos] > len) {
            break;
        }
        if ((buf[pos + 1] & 0x80) == 0 && (buf[pos + 1] & 0x3F) == 1) {
            tbtU4NameCopy(rt->VendorName, buf + pos + 2, (ULONG)buf[pos] - 2);
        } else if ((buf[pos + 1] & 0x80) == 0 && (buf[pos + 1] & 0x3F) == 2) {
            tbtU4NameCopy(rt->ModelName, buf + pos + 2, (ULONG)buf[pos] - 2);
        }
    }
}

/* ---------------------------------------------------------------------- */
/* Capabilities and adapters (Guide 3.2.2; Spec 8.2; u4cm.c               */
/* discover_adapters; Linux cap.c)                                        */
/* ---------------------------------------------------------------------- */

/* The router's capability list from ROUTER_CS_1's Next Capability Pointer:
 * the TMU, and the Thunderbolt 3 vendor capabilities (short and long VSEC
 * headers, Linux cap.c tb_switch_next_cap). At most TBT_CAP_WALK_LIMIT. */
static LONG tbtU4RouterCaps(PTBT_U4CM cm, PTBT_U4_ROUTER rt, ULONG first)
{
    ULONG ptr;
    ULONG h[2];
    ULONG n;
    LONG rc;

    rt->CapTmu = 0;
    rt->CapPlugEvents = 0;
    rt->CapLc = 0;
    ptr = first;
    for (n = 0; ptr != 0 && ptr <= TBT_CTL_ADDR_MASK - 1 && n < TBT_CAP_WALK_LIMIT; n++) {
        rc = tbtU4RdN(cm, &rt->Route, TBT_CS_ROUTER, 0, ptr, 2, h);
        if (rc != TBT_OK) {
            return rc;
        }
        if (TBT_CAP_ID(h[0]) == TBT_RCAP_TMU) {
            rt->CapTmu = ptr;
            ptr = TBT_CAP_NEXT(h[0]);
        } else if (TBT_CAP_ID(h[0]) == TBT_RCAP_VSC) {
            if (TBT_CAP_VSEC_ID(h[0]) == TBT_VSEC_PLUG_EVENTS) {
                rt->CapPlugEvents = ptr;
            } else if (TBT_CAP_VSEC_ID(h[0]) == TBT_VSEC_LINK_CONTROLLER) {
                rt->CapLc = ptr;
            }
            ptr = TBT_CAP_VSEC_LENGTH(h[0]) == 0 ? TBT_CAP_LONG_NEXT(h[1]) :
                  TBT_CAP_NEXT(h[0]);
        } else {
            /* Any order is accepted and undefined capabilities skipped
             * (Spec 8.2.1; Guide 3.2.2). */
            ptr = TBT_CAP_NEXT(h[0]);
        }
    }
    return TBT_OK;
}

/*
 * Every adapter 1 to Max Adapter: ADP_CS_0 to 5; ERR_ADDR (or ERR_ADP) or
 * the Unsupported type marks it unused (Guide 3.2.2); the capability chain
 * walked for the lane, TMU, adapter and USB4 port capabilities, at most 16
 * links, any order. A lane adapter with a USB4 Port Capability reports its
 * control path's credits in its HopID 0 entry; any other lane adapter's
 * are 2 (Linux switch.c tb_init_port).
 */
static LONG tbtU4DiscoverAdapters(PTBT_U4CM cm, PTBT_U4_ROUTER rt)
{
    ULONG a;
    ULONG v[TBT_ADP_BASIC_DWORDS];
    ULONG h;
    ULONG e[2];
    ULONG ptr;
    ULONG n;
    LONG rc;
    PTBT_U4_ADP ad;

    for (a = 1; a < TBT_U4_MAX_ADP; a++) {
        tbtU4Zero(&rt->Adp[a], sizeof(rt->Adp[a]));
    }
    for (a = 1; a <= rt->MaxAdp && a < TBT_U4_MAX_ADP; a++) {
        ad = &rt->Adp[a];
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
        for (n = 0; ptr != 0 && n < TBT_CAP_WALK_LIMIT; n++) {
            rc = tbtU4Rd(cm, &rt->Route, TBT_CS_ADAPTER, a, ptr, &h);
            if (rc != TBT_OK) {
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
 */
static VOID tbtU4GetCapabilities(PTBT_U4CM cm, PTBT_U4_ROUTER rt)
{
    ULONG meta;
    ULONG data[TBT_ROP_DATA_DWORDS];
    ULONG k;
    LONG rc;

    meta = 0;
    data[0] = 0;
    rc = tbtU4RouterOp(cm, rt, TBT_ROP_GET_CAPABILITIES, &meta, NULL, 0, data, 1);
    rt->CapsSupported = 0;
    rt->CapsEnabled = 0;
    if (rt->EnumeratedHere) {
        rt->CapsState = TBT_U4_CAPS_DEFAULT;
    } else if (rc == TBT_OK) {
        for (k = 1; k <= TBT_CAP_CLX_LATENCY; k++) {
            if ((data[0] >> (2 * k)) & 1UL) {
                rt->CapsSupported |= 1UL << k;
            }
            if ((data[0] >> (2 * k + 1)) & 1UL) {
                rt->CapsEnabled |= 1UL << k;
            }
        }
        rt->CapsState = TBT_U4_CAPS_KNOWN;
        if ((rt->CapsEnabled & ~((1UL << TBT_CAP_NOTIFY_SEQUENCE) |
                                 (1UL << TBT_CAP_BA_PER_PORT) |
                                 (1UL << TBT_CAP_HOT_PLUG_FAILURE))) != 0) {
            tbtU4Log(cm, "u4.caps.enabled-unhandled", rt->CapsEnabled);
        }
    } else if (rc == TBT_E_UNSUPPORTED) {
        rt->CapsState = TBT_U4_CAPS_DEFAULT;
    } else {
        rt->CapsState = TBT_U4_CAPS_UNKNOWN;
        tbtU4Log(cm, "u4.caps.unknown", (ULONG)rc);
    }
    TbtCtlSettle(&cm->Ctl, &rt->Route);
}

/* Buffer Allocation Request (opcode 33h; Spec Tables 8-56 to 8-58; Linux
 * usb4.c usb4_switch_credits_init), shaped by the capability answer: with
 * Buffer Allocation Per USB4 Port enabled it carries `lane0` in its
 * metadata. Not sent to a router whose capability state is unknown. */
static VOID tbtU4BufferAllocation(PTBT_U4CM cm, PTBT_U4_ROUTER rt, ULONG lane0)
{
    ULONG meta;
    ULONG data[TBT_ROP_DATA_DWORDS];
    ULONG n;
    ULONG k;
    LONG rc;

    rt->BaValid = FALSE;
    rt->BaMaxPcie = 0;
    rt->BaMaxUsb3 = 0;
    if (rt->CapsState == TBT_U4_CAPS_UNKNOWN || rt->CapsState == TBT_U4_CAPS_PENDING) {
        return;
    }
    meta = 0;
    if (rt->CapsState == TBT_U4_CAPS_KNOWN &&
        (rt->CapsEnabled & (1UL << TBT_CAP_BA_PER_PORT)) != 0) {
        meta = lane0 & 0x3FUL;
    }
    rc = tbtU4RouterOp(cm, rt, TBT_ROP_BUFFER_ALLOC, &meta, NULL, 0, data, 5);
    if (rc != TBT_OK) {
        tbtU4Log(cm, "u4.ba.none", (ULONG)rc);
        return;
    }
    n = TBT_ROP_BA_LENGTH(meta);
    if (n > 5) {
        n = 5;
    }
    for (k = 0; k < n; k++) {
        if (TBT_ROP_BA_INDEX(data[k]) == TBT_BA_MAX_PCIE) {
            rt->BaMaxPcie = TBT_ROP_BA_VALUE(data[k]);
        } else if (TBT_ROP_BA_INDEX(data[k]) == TBT_BA_MAX_USB3) {
            rt->BaMaxUsb3 = TBT_ROP_BA_VALUE(data[k]);
        }
    }
    rt->BaValid = TRUE;
}

/* The acknowledgement form of a router's notifications (TBT_CTL_ACK_FORM). */
static ULONG tbtU4AckForm(PVOID context, const TBT_ROUTE *route)
{
    PTBT_U4CM cm;
    PTBT_U4_ROUTER rt;

    cm = (PTBT_U4CM)context;
    rt = tbtU4RouterOf(cm, route);
    if (rt == NULL || rt->Tbt3) {
        return TBT_ACK_PLAIN;
    }
    switch (rt->CapsState) {
    case TBT_U4_CAPS_PENDING:
        return TBT_ACK_PENDING;
    case TBT_U4_CAPS_UNKNOWN:
        return TBT_ACK_NONE;
    case TBT_U4_CAPS_KNOWN:
        if ((rt->CapsEnabled & (1UL << TBT_CAP_NOTIFY_SEQUENCE)) != 0) {
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
            q->Port = port;
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
     * allocation scans the registers, where it stays Valid or pending. */
    tbtU4Log(cm, "u4.quarantine-full", (adp << 16) | hop);
}

/*
 * The live use of one lane adapter, read from the registers (Spec 8.2.3):
 * every entry of every adapter of the router from HopID 8 (1 on the host
 * interface adapter) to its Max Input HopID, two DWs each, 30 entries a
 * request; an entry Valid or with Pending Packets holds its input HopID,
 * a Valid entry its output HopID on its egress adapter, and a Valid entry
 * with ingress flow control on and shared buffering off its dedicated
 * credits; the quarantine adds what it holds.
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
                    if ((e[2 * k] & TBT_PATH0_VALID) != 0 &&
                        (e[2 * k + 1] & TBT_PATH1_IFC) != 0 &&
                        (e[2 * k + 1] & TBT_PATH1_ISE) == 0) {
                        u->Dedicated += TBT_PATH0_CREDITS(e[2 * k]);
                    }
                }
                if ((e[2 * k] & TBT_PATH0_VALID) != 0 &&
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
            TBT_U4_BIT_SET(u->InUse, q->Hop & TBT_HOP_MAX);
            u->Dedicated += q->Credits;
        }
        if (q->OutAdapter == lane) {
            TBT_U4_BIT_SET(u->OutUse, q->OutHop & TBT_HOP_MAX);
        }
    }
    return TBT_OK;
}

/* Credits a new path may take on a lane adapter (Guide 5.1, 5.1.4; Linux
 * tunnel.c tb_available_credits): Total Buffers, re-read, less the
 * control path's, the flow-control-disabled pool, the shared pool and the
 * dedicated credits already taken; each pool counted once. */
static LONG tbtU4LaneAvail(PTBT_U4CM cm, const TBT_U4_ROUTER *rt, ULONG lane,
                           const TBT_U4_USAGE *u, PULONG avail)
{
    ULONG v[2];
    ULONG used;
    LONG rc;

    rc = tbtU4RdN(cm, &rt->Route, TBT_CS_ADAPTER, lane, TBT_ADP_CS_4, 2, v);
    if (rc != TBT_OK) {
        return rc;
    }
    used = (ULONG)rt->Adp[lane].CtlCredits + TBT_ADP4_NFC_BUFFERS(v[0]) +
           TBT_ADP5_LCA(v[1]) + u->Dedicated;
    *avail = TBT_ADP4_TOTAL_BUFFERS(v[0]) > used ?
             TBT_ADP4_TOTAL_BUFFERS(v[0]) - used : 0;
    return TBT_OK;
}

/* The lowest HopID from 8 that the egress may send (its Max Output HopID)
 * and the next ingress may take (its Max Input HopID), free on both
 * (Guide 5.2.1; Linux switch.c tb_port_alloc_hopid). 0 for none. */
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
 * credits, IFC and ISE as read (vendor-defined on a USB4 protocol adapter,
 * Linux path.c tb_path_activate) and gets EFC 1, ESE 0 - except on a
 * Thunderbolt 3 router (`tbt3`), where they are written as for a lane
 * adapter (section 8 item 5).
 */
static LONG tbtU4WriteEntry(PTBT_U4CM cm, const TBT_ROUTE *r, ULONG adp,
                            ULONG hop, ULONG outAdp, ULONG outHop,
                            ULONG credits, ULONG weight, BOOLEAN lane,
                            BOOLEAN tbt3)
{
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
    return tbtU4WrN(cm, r, TBT_CS_PATH, adp, 2 * hop, 2, e);
}

/*
 * Invalidate one entry and prove it drained (Guide 5.2.2; Spec 5.4.2;
 * Linux path.c __tb_path_deactivate_hop): PATH_CS_0 Valid cleared, nothing
 * else changed, Pending Packets polled to 0 within 500 ms, then at least
 * tTeardown. TRUE when read back Valid 0 and Pending Packets 0; otherwise
 * the entry is quarantined.
 */
static BOOLEAN tbtU4Invalidate(PTBT_U4CM cm, ULONG port, const TBT_ROUTE *r,
                               ULONG adp, ULONG hop)
{
    ULONG e[2];
    LONG rc;
    TBT_WAIT w;

    e[0] = 0;
    rc = tbtU4RdN(cm, r, TBT_CS_PATH, adp, 2 * hop, 2, e);
    if (rc != TBT_OK) {
        goto fail;
    }
    if ((e[0] & TBT_PATH0_VALID) != 0) {
        rc = tbtU4Wr(cm, r, TBT_CS_PATH, adp, 2 * hop, e[0] & ~TBT_PATH0_VALID);
        if (rc != TBT_OK) {
            goto fail;
        }
    }
    TbtWaitStart(&w, cm->Host, TBT_U4_DRAIN_US, FALSE);
    for (;;) {
        rc = tbtU4RdN(cm, r, TBT_CS_PATH, adp, 2 * hop, 2, e);
        if (rc != TBT_OK) {
            goto fail;
        }
        if ((e[0] & TBT_PATH0_VALID) == 0 && (e[1] & TBT_PATH1_PP) == 0) {
            break;
        }
        if (TbtWaitDone(&w, cm->Host)) {
            cm->Counters.PendingTimeouts++;
            goto fail;
        }
        TbtWaitStep(&w, cm->Host, TBT_U4_POLL_STEP_US);
    }
    TbtWaitMin(cm->Host, TBT_U4_TEARDOWN_GAP_US);
    return TRUE;

fail:
    tbtU4Quarantine(cm, port, r, adp, hop, e[0], FALSE, 0, 0);
    return FALSE;
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

/* Each idle tick retries what is quarantined; only a proven invalidation
 * and drain, or a Path Enable read back clear, lifts it (section 7.3).
 * An entry on a device router that is gone is dropped with the router. */
static VOID tbtU4RetryQuarantine(PTBT_U4CM cm)
{
    ULONG i;
    ULONG e[2];
    ULONG v;
    PTBT_U4_QENTRY q;
    PTBT_U4_PORT p;
    LONG rc;

    for (i = 0; i < TBT_U4_QUARANTINE; i++) {
        q = &cm->Quarantine[i];
        if (!q->Used) {
            continue;
        }
        if (TbtRouteDepth(&q->Route) != 0) {
            p = tbtU4PortOf(cm, q->Port);
            if (p == NULL || !p->Dev.Present ||
                !TbtRouteEqual(&p->Dev.Route, &q->Route)) {
                q->Used = FALSE;
                continue;
            }
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
            }
            continue;
        }
        rc = tbtU4RdN(cm, &q->Route, TBT_CS_PATH, q->Adapter, 2 * q->Hop, 2, e);
        if (rc == TBT_OK && (e[0] & TBT_PATH0_VALID) != 0) {
            rc = tbtU4Wr(cm, &q->Route, TBT_CS_PATH, q->Adapter, 2 * q->Hop,
                         e[0] & ~TBT_PATH0_VALID);
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
            tbtU4Log(cm, "u4.quarantine-lifted", (q->Adapter << 16) | q->Hop);
        }
    }
}

/* Whether a protocol adapter may carry a new tunnel: not blocked, and its
 * HopID 8 entry not quarantined. */
static BOOLEAN tbtU4AdapterFree(PTBT_U4CM cm, const TBT_ROUTE *r, ULONG adp)
{
    return (BOOLEAN)(!tbtU4Quarantined(cm, r, adp, 0, TRUE) &&
                     !tbtU4Quarantined(cm, r, adp, TBT_HOP_PROTOCOL, FALSE));
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
 * Enumerate (Guide 3.2.1 step 4; u4cm.c enumerate_router; Linux switch.c
 * tb_switch_configure): one write of ROUTER_CS_1 to 4 - Depth, TopologyID
 * with Valid, Notification Timeout 255, and on a USB4 router CMUV 1.0 -
 * then, on a USB4 router, Router Ready within 500 ms (Linux usb4.c
 * usb4_switch_setup). `v` is ROUTER_CS_0 to 4 as read.
 */
static LONG tbtU4Enumerate(PTBT_U4CM cm, PTBT_U4_ROUTER rt, ULONG *v)
{
    ULONG w[4];
    ULONG cs6;
    LONG rc;
    TBT_WAIT t;

    w[0] = (v[1] & ~TBT_RTR1_DEPTH_MASK) |
           ((rt->Depth << TBT_RTR1_DEPTH_SHIFT) & TBT_RTR1_DEPTH_MASK);
    w[1] = rt->Route.Lo;
    w[2] = (rt->Route.Hi & TBT_RTR3_TOPOLOGY_MASK) | TBT_RTR3_V;
    w[3] = (v[4] & ~TBT_RTR4_NOTIFY_TIMEOUT_MASK) | TBT_RTR4_NOTIFY_TIMEOUT_MAX;
    if (!rt->Tbt3) {
        w[3] = (w[3] & ~TBT_RTR4_CMUV_MASK) | TBT_RTR4_CMUV_1_0;
    }
    rc = tbtU4WrN(cm, &rt->Route, TBT_CS_ROUTER, 0, TBT_ROUTER_CS_1, 4, w);
    if (rc != TBT_OK) {
        return rc;
    }
    rt->EnumeratedHere = TRUE;
    cm->Counters.RoutersEnumerated++;
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
 * Test (d)'s router half and section 6.1: ROUTER_CS_0 to 4 at route 0
 * answered, USB4 Version 20h or 40h; enumerated if TopologyID Valid is
 * clear, else verified and left unwritten (Guide 3.2.1 note); adapters;
 * USB4 ports and their PCIe and USB3 Down adapters paired in increasing
 * adapter number (Spec 5.2.1, 5.2.5; Linux usb4.c usb4_port_index,
 * usb4_switch_map_pcie_down); the mapping operation; Get Capabilities and
 * the Buffer Allocation Request; quirks; the host router's TMU.
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
    if (TbtHwRouterVersion(h->Usb4Version) != TBT_HW_ROUTER_USB4) {
        cm->InertReason = TBT_INERT_ROUTER_VERSION;
        tbtU4Log(cm, "u4.host.version", h->Usb4Version);
        return TBT_E_REFUSED;
    }
    h->Present = TRUE;
    if ((v[3] & TBT_RTR3_V) == 0) {
        rc = tbtU4Enumerate(cm, h, v);
        if (rc == TBT_E_TIMEOUT) {
            /* Linux never waits for the host router: logged, not fatal. */
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
        if (t == TBT_ADP_TYPE_LANE && h->Adp[a].CapPort != 0 &&
            h->Adp[a].CapLane != 0 && a != h->UpAdp &&
            cm->PortCount < TBT_U4_MAX_PORTS) {
            p = &cm->Ports[cm->PortCount];
            tbtU4Zero(p, sizeof(*p));
            p->Index = ++cm->PortCount;
            p->Lane0 = a;
        }
    }
    /* The Nth PCIe Down and the Nth USB3 Down adapter, in increasing
     * adapter number, belong to the Nth USB4 port. */
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
    tbtU4MapPcie(cm);
    tbtU4GetCapabilities(cm, h);
    tbtU4BufferAllocation(cm, h, 0);
    return TBT_OK;
}

/*
 * The host router's own TMU, once at start inside its own time disruption
 * (record 22 section 6.6 item 4; Linux tb.c tb_start, tmu.c
 * tb_switch_tmu_enable for the root: the rate alone), unless it is already
 * running, which is then left as found; a router reporting the protocol
 * not supported gets none.
 */
static VOID tbtU4HostTmu(PTBT_U4CM cm)
{
    PTBT_U4_ROUTER h;
    ULONG cs0;
    ULONG cs3;
    LONG rc;

    h = &cm->HostRouter;
    if (h->CapTmu == 0) {
        return;
    }
    rc = tbtU4Rd(cm, &h->Route, TBT_CS_ROUTER, 0, h->CapTmu + TBT_TMU_RTR_CS_0, &cs0);
    if (rc != TBT_OK || (cs0 & TBT_TMU_RTR0_TSP_NS) != 0) {
        return;
    }
    rc = tbtU4Rd(cm, &h->Route, TBT_CS_ROUTER, 0, h->CapTmu + TBT_TMU_RTR_CS_3, &cs3);
    if (rc != TBT_OK || (cs3 & TBT_TMU_RTR3_INTERVAL_MASK) != 0) {
        return;
    }
    rc = tbtU4Wr(cm, &h->Route, TBT_CS_ROUTER, 0, h->CapTmu + TBT_TMU_RTR_CS_0,
                 cs0 | TBT_TMU_RTR0_TD);
    if (rc == TBT_OK) {
        rc = tbtU4Wr(cm, &h->Route, TBT_CS_ROUTER, 0, h->CapTmu + TBT_TMU_RTR_CS_3,
                     (cs3 & ~TBT_TMU_RTR3_INTERVAL_MASK) |
                     (TBT_TMU_INTERVAL_HIFI << TBT_TMU_RTR3_INTERVAL_SHIFT));
    }
    (void)tbtU4Wr(cm, &h->Route, TBT_CS_ROUTER, 0, h->CapTmu + TBT_TMU_RTR_CS_0,
                  cs0 & ~TBT_TMU_RTR0_TD);
    tbtU4Log(cm, "u4.host.tmu", (ULONG)rc);
}

/* ---------------------------------------------------------------------- */
/* Thunderbolt 3 device routers (record 22 section 8)                     */
/* ---------------------------------------------------------------------- */

/* Alpine Ridge bridges keep the USB plug event (Linux switch.c
 * tb_plug_events_active; tb.h tb_switch_is_alpine_ridge). */
static BOOLEAN tbtU4AlpineRidge(const TBT_U4_ROUTER *rt)
{
    return (BOOLEAN)(rt->Vendor == 0x8086 &&
                     (rt->Device == 0x1576 || rt->Device == 0x1578 ||
                      rt->Device == 0x15C0 || rt->Device == 0x15D3 ||
                      rt->Device == 0x15DA));
}

/* The link controller's per-port block of an adapter (Linux lc.c
 * find_port_lc_cap: the descriptor's start, plus the physical port, (adapter
 * - 1) / 2, times the per-port size). 0 when there is none. */
static ULONG tbtU4LcPort(PTBT_U4CM cm, const TBT_U4_ROUTER *rt, ULONG adp)
{
    ULONG desc;

    if (rt->CapLc == 0 || adp == 0 ||
        tbtU4Rd(cm, &rt->Route, TBT_CS_ROUTER, 0, rt->CapLc + TBT_LC_DESC, &desc) != TBT_OK) {
        return 0;
    }
    return rt->CapLc + TBT_LC_DESC_SIZE(desc) +
           ((adp - 1) / 2) * TBT_LC_DESC_PORT_SIZE(desc);
}

/* Plug events active (Linux switch.c tb_plug_events_active): bits 6:2 of
 * the capability's second DW cleared, the USB disable bit set except on
 * Alpine Ridge; then the fused UUID (lc.c tb_lc_read_uuid). */
static LONG tbtU4Tbt3Setup(PTBT_U4CM cm, PTBT_U4_ROUTER rt)
{
    ULONG v;
    LONG rc;

    if (rt->CapPlugEvents == 0) {
        return TBT_E_STATE;
    }
    rc = tbtU4Rd(cm, &rt->Route, TBT_CS_ROUTER, 0,
                 rt->CapPlugEvents + TBT_PLUG_EVENTS_CS_1, &v);
    if (rc != TBT_OK) {
        return rc;
    }
    v &= ~TBT_PLUG_EVENTS_MASK;
    if (!tbtU4AlpineRidge(rt)) {
        v |= TBT_PLUG_EVENTS_USB_DISABLE;
    }
    rc = tbtU4Wr(cm, &rt->Route, TBT_CS_ROUTER, 0,
                 rt->CapPlugEvents + TBT_PLUG_EVENTS_CS_1, v);
    if (rc != TBT_OK) {
        return rc;
    }
    if (rt->CapLc != 0) {
        (void)tbtU4RdN(cm, &rt->Route, TBT_CS_ROUTER, 0, rt->CapLc + TBT_LC_FUSE,
                       4, rt->Uuid);
    }
    return TBT_OK;
}

/* ---------------------------------------------------------------------- */
/* Lanes and bonding (record 22 section 6.5)                              */
/* ---------------------------------------------------------------------- */

static LONG tbtU4LaneRd(PTBT_U4CM cm, const TBT_U4_ROUTER *rt, ULONG adp,
                        PULONG v)
{
    if (adp == 0 || adp >= TBT_U4_MAX_ADP || rt->Adp[adp].CapLane == 0) {
        return TBT_E_STATE;
    }
    return tbtU4Rd(cm, &rt->Route, TBT_CS_ADAPTER, adp,
                   rt->Adp[adp].CapLane + TBT_LANE_CS_1, v);
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
static BOOLEAN tbtU4TmuOff(PTBT_U4CM cm, PTBT_U4_PORT p)
{
    PTBT_U4_ROUTER h;
    PTBT_U4_ROUTER d;
    const TBT_U4_ROUTER *rt;
    ULONG adp;
    ULONG v;
    ULONG k;

    h = &cm->HostRouter;
    d = &p->Dev;
    if (d->CapTmu == 0 || h->Adp[p->Lane0].CapTmu == 0 ||
        d->Adp[d->UpAdp].CapTmu == 0) {
        return TRUE;
    }
    if (tbtU4Rd(cm, &d->Route, TBT_CS_ROUTER, 0, d->CapTmu + TBT_TMU_RTR_CS_3, &v) != TBT_OK ||
        (v & TBT_TMU_RTR3_INTERVAL_MASK) != 0) {
        return FALSE;
    }
    for (k = 0; k < 2; k++) {
        rt = k == 0 ? h : d;
        adp = k == 0 ? p->Lane0 : d->UpAdp;
        if (tbtU4Rd(cm, &rt->Route, TBT_CS_ADAPTER, adp,
                    rt->Adp[adp].CapTmu + TBT_TMU_ADP_CS_3, &v) != TBT_OK ||
            (v & TBT_TMU_ADP3_UDM) != 0) {
            return FALSE;
        }
        if (rt->Usb4Version == TBT_USB4_VERSION_2_0) {
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

/* USB4 Port is Configured on the host's lane 0 port and on the device's
 * upstream port (Linux usb4.c usb4_port_configure, switch.c
 * tb_switch_configure_link), or on a Thunderbolt 3 router its link
 * controller's SX_CTRL (lc.c tb_lc_set_port_configured): L1C on an odd
 * adapter, L2C on an even one, with UPSTREAM. */
static LONG tbtU4Configured(PTBT_U4CM cm, PTBT_U4_PORT p)
{
    PTBT_U4_ROUTER h;
    PTBT_U4_ROUTER d;
    ULONG lc;
    LONG rc;

    h = &cm->HostRouter;
    d = &p->Dev;
    rc = tbtU4Rmw(cm, &h->Route, TBT_CS_ADAPTER, p->Lane0,
                  h->Adp[p->Lane0].CapPort + TBT_PORT_CS_19, 0, TBT_PORT19_PC);
    if (rc != TBT_OK) {
        return rc;
    }
    p->PcSetHost = TRUE;
    if (!d->Tbt3 && d->Adp[d->UpAdp].CapPort != 0) {
        rc = tbtU4Rmw(cm, &d->Route, TBT_CS_ADAPTER, d->UpAdp,
                      d->Adp[d->UpAdp].CapPort + TBT_PORT_CS_19, 0, TBT_PORT19_PC);
    } else if (d->Tbt3) {
        lc = tbtU4LcPort(cm, d, d->UpAdp);
        if (lc != 0) {
            rc = tbtU4Rmw(cm, &d->Route, TBT_CS_ROUTER, 0, lc + TBT_LC_SX_CTRL, 0,
                          ((d->UpAdp % 2) != 0 ? TBT_LC_SX_CTRL_L1C : TBT_LC_SX_CTRL_L2C) |
                          TBT_LC_SX_CTRL_UPSTREAM);
        }
    }
    if (rc == TBT_OK) {
        p->PcSetDev = TRUE;
    }
    return rc;
}

/*
 * Section 6.5 (Guide 7.1, 7.5, 7.5.1; Linux switch.c
 * tb_switch_lane_bonding_enable): bonded, or single-lane only when that is
 * verified - lane 1 disconnected or disabled, or disabled here with the
 * Lane Adapter Configuration Capability at its defaults, no CLx enabled
 * and the link's TMU Off. TBT_OK when the port may carry paths,
 * TBT_E_STATE when it is FAILED, TBT_E_GONE when the port's generation
 * moved on.
 */
static LONG tbtU4Bond(PTBT_U4CM cm, PTBT_U4_PORT p, ULONG gen)
{
    PTBT_U4_ROUTER h;
    PTBT_U4_ROUTER d;
    ULONG ad[4];
    const TBT_U4_ROUTER *rt[4];
    ULONG v;
    ULONG v0;
    ULONG v1;
    ULONG k;
    ULONG lc;
    BOOLEAN possible;
    LONG rc;
    TBT_WAIT w;

    h = &cm->HostRouter;
    d = &p->Dev;
    ad[0] = p->Lane0;
    ad[1] = p->Lane0 + 1;
    ad[2] = d->UpAdp;
    ad[3] = d->UpAdp + 1;
    rt[0] = h;
    rt[1] = h;
    rt[2] = d;
    rt[3] = d;
    cm->Counters.BondAttempts++;

    rc = tbtU4LaneRd(cm, h, ad[0], &v0);
    if (rc != TBT_OK) {
        return TBT_E_STATE;
    }
    if (TBT_LANE1_NLW(v0) == TBT_LANE_WIDTH_X2) {
        p->Bonded = TRUE;
        cm->Counters.BondX2++;
        goto configured;
    }
    if (!cm->Config.LaneBonding) {
        goto fallback;
    }

    possible = FALSE;
    if (!d->Tbt3 && d->Adp[ad[2]].CapPort != 0) {
        possible = (BOOLEAN)(tbtU4Rd(cm, &d->Route, TBT_CS_ADAPTER, ad[2],
                                     d->Adp[ad[2]].CapPort + TBT_PORT_CS_18, &v) == TBT_OK &&
                             (v & TBT_PORT18_BE) != 0);
    } else if (d->Tbt3) {
        lc = tbtU4LcPort(cm, d, ad[2]);
        possible = (BOOLEAN)(lc != 0 &&
                             tbtU4Rd(cm, &d->Route, TBT_CS_ROUTER, 0,
                                     lc + TBT_LC_PORT_ATTR, &v) == TBT_OK &&
                             (v & TBT_LC_PORT_ATTR_BE) != 0);
    }
    for (k = 0; possible && k < 4; k += 2) {
        possible = (BOOLEAN)(rt[k]->Adp[ad[k]].CapLane != 0 &&
                             tbtU4Rd(cm, &rt[k]->Route, TBT_CS_ADAPTER, ad[k],
                                     rt[k]->Adp[ad[k]].CapLane + TBT_LANE_CS_0, &v) == TBT_OK &&
                             (v & TBT_LANE0_SLW_X2) != 0);
    }
    if (!possible) {
        tbtU4Log(cm, "u4.bond.impossible", p->Index);
        goto fallback;
    }
    if (!tbtU4WaitLane(cm, p, gen, h, ad[1], TBT_LANE_STATE_CL0, TBT_U4_LANE1_CL0_US) ||
        !tbtU4WaitLane(cm, p, gen, d, ad[3], TBT_LANE_STATE_CL0, TBT_U4_LANE1_CL0_US)) {
        if (!TBT_U4_LIVE(p, gen)) {
            return TBT_E_GONE;
        }
        tbtU4Log(cm, "u4.bond.lane1", p->Index);
        goto fallback;
    }
    for (k = 0; k < 4; k++) {
        if (tbtU4LaneRd(cm, rt[k], ad[k], &p->LaneSaved[k]) != TBT_OK) {
            goto fallback;
        }
    }
    p->LaneWritten = TRUE;
    for (k = 0; k < 4; k++) {
        if (tbtU4LaneRmw(cm, rt[k], ad[k], TBT_LANE1_TLW_MASK, TBT_LANE1_TLW_DUAL) != TBT_OK) {
            goto fallback;
        }
    }
    if (tbtU4LaneRmw(cm, h, ad[0], 0, TBT_LANE1_LB) != TBT_OK) {
        goto fallback;
    }
    TbtWaitStart(&w, cm->Host, TBT_U4_BONDING_US, FALSE);
    for (;;) {
        /* The lane 1 hot unplug event bonding produces is acknowledged by
         * the transport and only counted (section 7.1). */
        if (tbtU4LaneRd(cm, h, ad[0], &v0) == TBT_OK &&
            TBT_LANE1_NLW(v0) == TBT_LANE_WIDTH_X2) {
            p->Bonded = TRUE;
            cm->Counters.BondX2++;
            goto configured;
        }
        if (!TBT_U4_LIVE(p, gen)) {
            return TBT_E_GONE;
        }
        if (TbtWaitDone(&w, cm->Host)) {
            tbtU4Log(cm, "u4.bond.timeout", v0);
            break;
        }
        tbtU4Pause(cm, &w);
    }

fallback:
    if (!TBT_U4_LIVE(p, gen)) {
        return TBT_E_GONE;
    }
    /* 6.1: every field step 4 wrote, back to what it read. */
    if (p->LaneWritten) {
        for (k = 0; k < 4; k++) {
            if (tbtU4LaneRmw(cm, rt[k], ad[k], TBT_LANE1_TLW_MASK | TBT_LANE1_LB,
                             p->LaneSaved[k] & (TBT_LANE1_TLW_MASK | TBT_LANE1_LB)) != TBT_OK) {
                p->Why = TBT_U4_WHY_BONDING;
                return TBT_E_STATE;
            }
        }
        p->LaneWritten = FALSE;
    }
    /* 6.2: both lane 1 adapters disconnected or disabled: single. */
    if (tbtU4LaneRd(cm, h, ad[1], &v1) != TBT_OK ||
        tbtU4LaneRd(cm, d, ad[3], &v) != TBT_OK) {
        p->Why = TBT_U4_WHY_BONDING;
        return TBT_E_STATE;
    }
    if ((TBT_LANE1_STATE(v1) == TBT_LANE_STATE_CLD ||
         TBT_LANE1_STATE(v1) == TBT_LANE_STATE_DISABLED) &&
        (TBT_LANE1_STATE(v) == TBT_LANE_STATE_CLD ||
         TBT_LANE1_STATE(v) == TBT_LANE_STATE_DISABLED)) {
        cm->Counters.BondSingle++;
        p->SingleLane = TRUE;
        goto configured;
    }
    /* 6.3: the preconditions read back on the host's lane adapters. */
    if (tbtU4LaneRd(cm, h, ad[0], &v0) != TBT_OK) {
        p->Why = TBT_U4_WHY_BONDING;
        return TBT_E_STATE;
    }
    for (k = 0; k < 2; k++) {
        v = k == 0 ? v0 : v1;
        if ((v & TBT_LANE1_TLW_MASK) != TBT_LANE1_TLW_SINGLE ||
            (v & TBT_LANE1_LB) != 0 || (v & TBT_LANE1_CLX_EN) != 0) {
            tbtU4Log(cm, "u4.bond.not-default", (k << 31) | v);
            p->Why = TBT_U4_WHY_BONDING;
            return TBT_E_STATE;
        }
    }
    if (!tbtU4TmuOff(cm, p)) {
        tbtU4Log(cm, "u4.bond.tmu-not-off", p->Index);
        p->Why = TBT_U4_WHY_BONDING;
        return TBT_E_STATE;
    }
    /* 6.4: two single-lane links: lane 1 disabled, verified. */
    if (TBT_LANE1_STATE(v1) != TBT_LANE_STATE_CL0 ||
        TBT_LANE1_NLW(v0) != TBT_LANE_WIDTH_X1) {
        p->Why = TBT_U4_WHY_BONDING;
        return TBT_E_STATE;
    }
    if (tbtU4LaneRmw(cm, h, ad[1], 0, TBT_LANE1_LD) != TBT_OK) {
        p->Why = TBT_U4_WHY_BONDING;
        return TBT_E_STATE;
    }
    p->Lane1Disabled = TRUE;
    if (!tbtU4WaitLane(cm, p, gen, h, ad[1], TBT_LANE_STATE_DISABLED,
                       TBT_U4_LANE_DISABLE_US)) {
        if (!TBT_U4_LIVE(p, gen)) {
            return TBT_E_GONE;
        }
        p->Why = TBT_U4_WHY_BONDING;
        return TBT_E_STATE;
    }
    cm->Counters.Lane1Disabled++;
    cm->Counters.BondSingle++;
    p->SingleLane = TRUE;

configured:
    if (tbtU4Configured(cm, p) != TBT_OK) {
        tbtU4Log(cm, "u4.port-configured.failed", p->Index);
    }
    return TBT_U4_LIVE(p, gen) ? TBT_OK : TBT_E_GONE;
}

/* ---------------------------------------------------------------------- */
/* The TMU (record 22 section 6.6; Guide 7.4; Linux tmu.c)                */
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

/* Item 6: the link's TMU off - the downstream router's TSPacketInterval 0,
 * then Disable Time Sync on both lane 0 adapters, inside a time disruption
 * (Guide 7.4.3.4.2; Linux tmu.c tb_switch_tmu_disable). */
static LONG tbtU4TmuDisable(PTBT_U4CM cm, PTBT_U4_PORT p)
{
    PTBT_U4_ROUTER d;
    LONG rc;

    d = &p->Dev;
    rc = tbtU4TmuTd(cm, d, TRUE);
    if (rc == TBT_OK) {
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
    if (rc == TBT_OK) {
        p->TmuConfigured = FALSE;
        p->TmuOk = FALSE;
    } else {
        p->TmuQuarantine = TRUE;
    }
    return rc;
}

/*
 * Items 2 to 5: HiFi-Bi from Off on a link that will carry a USB3 tunnel,
 * inside a time disruption - the device router's local time posted from
 * the host router's (Linux tmu.c tb_switch_tmu_post_time: Post Local Time,
 * Post Time low 1 and high FFFFFFFFh, then high 0, Post Time polled to 0),
 * its parameters (Guide Table 7-3: window 800, averaging constants 8), UDM
 * 0 on both ports, TSPacketInterval 16, Disable Time Sync 0 upstream then
 * downstream (Linux tb_switch_tmu_enable_bidirectional), then 50 ms. A
 * failure clears the disruption, turns the link's TMU off again, and
 * leaves the USB3 tunnel unbuilt; a step that cannot be undone is the
 * port's TMU quarantine.
 */
static VOID tbtU4TmuEnable(PTBT_U4CM cm, PTBT_U4_PORT p, ULONG gen)
{
    PTBT_U4_ROUTER h;
    PTBT_U4_ROUTER d;
    ULONG lt[3];
    ULONG post[2];
    ULONG v;
    LONG rc;
    TBT_WAIT w;

    h = &cm->HostRouter;
    d = &p->Dev;
    p->TmuOk = FALSE;
    if (d->CapTmu == 0) {
        p->TmuOk = TRUE;    /* no TMU: no adapter time to correct */
        return;
    }
    if (tbtU4Rd(cm, &d->Route, TBT_CS_ROUTER, 0, d->CapTmu + TBT_TMU_RTR_CS_0, &v) != TBT_OK) {
        return;
    }
    if ((v & TBT_TMU_RTR0_TSP_NS) != 0) {
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
    rc = tbtU4TmuTd(cm, d, TRUE);
    if (rc == TBT_OK) {
        rc = tbtU4RdN(cm, &h->Route, TBT_CS_ROUTER, 0, h->CapTmu + TBT_TMU_RTR_CS_1, 3, lt);
    }
    if (rc == TBT_OK) {
        /* The grandmaster time in ns: CS_3 15:0, CS_2, CS_1 31:16, as two
         * DWs without a 64-bit type. */
        post[0] = (lt[1] << 16) | (lt[0] >> 16);
        post[1] = ((lt[2] & 0xFFFFUL) << 16) | (lt[1] >> 16);
        rc = tbtU4WrN(cm, &d->Route, TBT_CS_ROUTER, 0, d->CapTmu + TBT_TMU_RTR_CS_22, 2, post);
    }
    if (rc == TBT_OK) {
        post[0] = 1;
        post[1] = 0xFFFFFFFFUL;
        rc = tbtU4WrN(cm, &d->Route, TBT_CS_ROUTER, 0, d->CapTmu + TBT_TMU_RTR_CS_24, 2, post);
    }
    if (rc == TBT_OK) {
        rc = tbtU4Wr(cm, &d->Route, TBT_CS_ROUTER, 0, d->CapTmu + TBT_TMU_RTR_CS_25, 0);
    }
    if (rc == TBT_OK) {
        TbtWaitStart(&w, cm->Host, TBT_U4_TMU_POST_US, FALSE);
        for (;;) {
            rc = tbtU4RdN(cm, &d->Route, TBT_CS_ROUTER, 0, d->CapTmu + TBT_TMU_RTR_CS_24, 2, post);
            if (rc != TBT_OK || (post[0] == 0 && post[1] == 0)) {
                break;
            }
            if (TbtWaitDone(&w, cm->Host) || !TBT_U4_LIVE(p, gen)) {
                rc = TBT_E_TIMEOUT;
                break;
            }
            tbtU4Pause(cm, &w);
        }
    }
    if (rc == TBT_OK) {
        rc = tbtU4Rmw(cm, &d->Route, TBT_CS_ROUTER, 0, d->CapTmu + TBT_TMU_RTR_CS_0,
                      TBT_TMU_RTR0_FREQ_WIND_MASK,
                      TBT_TMU_FREQ_WINDOW_HIFI << TBT_TMU_RTR0_FREQ_WIND_SHIFT);
    }
    if (rc == TBT_OK) {
        v = TBT_TMU_AVG_CONST_HIFI | (TBT_TMU_AVG_CONST_HIFI << 6) |
            (TBT_TMU_AVG_CONST_HIFI << 12) | (TBT_TMU_AVG_CONST_HIFI << 18);
        rc = tbtU4Rmw(cm, &d->Route, TBT_CS_ROUTER, 0, d->CapTmu + TBT_TMU_RTR_CS_15,
                      TBT_TMU_RTR15_AVG_MASK, v);
    }
    if (rc == TBT_OK) {
        rc = tbtU4TmuAdp(cm, d, d->UpAdp, TBT_TMU_ADP_CS_3, TBT_TMU_ADP3_UDM, 0);
    }
    if (rc == TBT_OK) {
        rc = tbtU4TmuAdp(cm, h, p->Lane0, TBT_TMU_ADP_CS_3, TBT_TMU_ADP3_UDM, 0);
    }
    if (rc == TBT_OK) {
        rc = tbtU4Rmw(cm, &d->Route, TBT_CS_ROUTER, 0, d->CapTmu + TBT_TMU_RTR_CS_3,
                      TBT_TMU_RTR3_INTERVAL_MASK,
                      TBT_TMU_INTERVAL_HIFI << TBT_TMU_RTR3_INTERVAL_SHIFT);
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
 * Steps 2 to 9 on a port whose lane 0 reads connected: Lock cleared on both
 * lane adapters (Guide 3.2.1; Spec 8.2.2.1), ROUTER_CS_0 to 4 at route =
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
    for (a = p->Lane0; a <= p->Lane0 + 1; a++) {
        rc = tbtU4Rd(cm, &h->Route, TBT_CS_ADAPTER, a, TBT_ADP_CS_4, &v[0]);
        if (rc == TBT_OK && (v[0] & TBT_ADP4_LOCK) != 0) {
            rc = tbtU4Wr(cm, &h->Route, TBT_CS_ADAPTER, a, TBT_ADP_CS_4,
                         v[0] & ~TBT_ADP4_LOCK);
        }
        if (rc != TBT_OK && a == p->Lane0) {
            p->Why = TBT_U4_WHY_ENUMERATION;
            return rc;
        }
    }
    p->Unlocked = TRUE;

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
        rc = tbtU4RdN(cm, &d->Route, TBT_CS_ROUTER, 0, TBT_ROUTER_CS_0, 5, v);
        if (rc == TBT_E_NOTIFY && (cm->Ring->LastEvent == TBT_EV_ERR_ENUM ||
                                   cm->Ring->LastEvent == TBT_EV_ERR_NUA)) {
            /* Someone else's, or uninitialised behind its own downstream
             * port (Guide 3.2.1 step 1): left alone. */
            *next = TBT_U4_UNSUPPORTED;
            p->Why = TBT_U4_WHY_ENUM_ERROR;
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
            if (tbtU4Enumerated(d, v)) {
                d->Preserved = TRUE;
                cm->Counters.RoutersAdopted++;
                break;
            }
            rc = tbtU4Enumerate(cm, d, v);
            if (rc == TBT_OK) {
                break;
            }
        }
        if (k + 1 < TBT_U4_ENUM_TRIES) {
            tbtU4WaitMin(cm, TBT_U4_ENUM_GAP_US);
        }
    }
    if (rc != TBT_OK) {
        p->Why = TBT_U4_WHY_ENUMERATION;
        tbtU4Log(cm, "u4.router.enumeration", (ULONG)rc);
        return rc;
    }
    d->Present = TRUE;
    if (d->UpAdp != 0 && d->UpAdp + 1 < TBT_U4_MAX_ADP) {
        rc = tbtU4DiscoverAdapters(cm, d);
    } else {
        rc = TBT_E_STATE;
    }
    if (rc == TBT_OK) {
        rc = tbtU4RouterCaps(cm, d, TBT_RTR1_NEXT_CAP(v[1]));
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
        tbtU4ReadDrom(cm, d);
        (void)tbtU4RdN(cm, &d->Route, TBT_CS_ROUTER, 0, TBT_ROUTER_CS_7, 2, d->Uuid);
        tbtU4GetCapabilities(cm, d);
        tbtU4BufferAllocation(cm, d, d->UpAdp);
    }
    tbtU4Log(cm, "u4.router.up", (d->Vendor << 16) | d->Device);
    return TBT_U4_LIVE(p, gen) ? TBT_OK : TBT_E_GONE;
}

/* The link is a USB4 link, not Thunderbolt 3 compatible (PORT_CS_18 TCM
 * clear on the host's port; Linux usb4.c link_is_usb4). */
static BOOLEAN tbtU4LinkUsb4(PTBT_U4CM cm, PTBT_U4_PORT p)
{
    ULONG v;

    if (p->Dev.Tbt3) {
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
    return (BOOLEAN)(cm->Config.Usb3Tunnel && p->Usb3Down != 0 &&
                     tbtU4FirstOf(&p->Dev, TBT_ADP_TYPE_USB3_UP) != 0 &&
                     !p->DpPreserved && tbtU4LinkUsb4(cm, p));
}

/*
 * Section 6.6: ROUTER_CS_5 in one read-modify-write - PTO where both PCIe
 * adapters exist, UTO where both USB3 adapters exist on a USB4 link,
 * whatever XhciCmUsb3Tunnel says (Guide 3.2.3 "must"), HCO never, CNS left
 * as it is (0 on a fresh router, Linux usb4_switch_setup) - then the TMU
 * where a USB3 tunnel will be built, then CV in a second write and
 * Configuration Ready within 500 ms.
 */
static LONG tbtU4ConfigValid(PTBT_U4CM cm, PTBT_U4_PORT p, ULONG gen)
{
    PTBT_U4_ROUTER d;
    ULONG set;
    ULONG cs6;
    LONG rc;
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
    if (tbtU4WantUsb3(cm, p) && !p->TmuQuarantine) {
        tbtU4TmuEnable(cm, p, gen);
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
    t->Written &= TBT_U4_E_BANDWIDTH;
    t->Up = FALSE;
    t->Preserved = FALSE;
    return ok;
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
 * Table 5-2; Linux tunnel.c tb_pci_init_path / tb_usb3_init_path): HopIDs
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

    /* Section 6.8: the router's preference (Buffer Allocation, else 32
     * bonded and 16 single, Linux tunnel.c tb_pci_init_credits), bounded by
     * what the lane has left after reserving the USB3 preference when a
     * USB3 tunnel will follow; below 6, no tunnel. */
    def = p->Bonded ? TBT_CREDITS_BONDED : TBT_CREDITS_SINGLE;
    if (usb3) {
        prefH = h->BaMaxUsb3 != 0 ? h->BaMaxUsb3 : def;
        prefD = d->BaMaxUsb3 != 0 ? d->BaMaxUsb3 : def;
    } else {
        prefH = h->BaMaxPcie != 0 ? h->BaMaxPcie : def;
        prefD = d->BaMaxPcie != 0 ? d->BaMaxPcie : def;
    }
    resH = 0;
    resD = 0;
    if (reserveUsb3) {
        resH = h->BaMaxUsb3 != 0 ? h->BaMaxUsb3 : def;
        resD = d->BaMaxUsb3 != 0 ? d->BaMaxUsb3 : def;
    }
    availH = availH > resH ? availH - resH : 0;
    availD = availD > resD ? availD - resD : 0;
    t->CreditsHost = prefH < availH ? prefH : availH;
    t->CreditsDev = prefD < availD ? prefD : availD;
    if (t->CreditsHost < TBT_CREDITS_MIN_PCIE || t->CreditsDev < TBT_CREDITS_MIN_PCIE) {
        p->Why = TBT_U4_WHY_CREDITS;
        tbtU4Log(cm, "u4.credits.short", (t->CreditsHost << 16) | t->CreditsDev);
        return TBT_E_STATE;
    }

    /* Downstream, then upstream; each source adapter first (Guide 5.2.1). */
    rc = tbtU4WriteEntry(cm, &h->Route, hp, TBT_HOP_PROTOCOL, p->Lane0, t->Hd,
                         0, weight, FALSE, FALSE);
    if (rc == TBT_OK) {
        t->Written |= TBT_U4_E_HOST_PROTO;
        rc = tbtU4WriteEntry(cm, &d->Route, d->UpAdp, t->Hd, dp, TBT_HOP_PROTOCOL,
                             t->CreditsDev, weight, TRUE, FALSE);
    }
    if (rc == TBT_OK && TBT_U4_LIVE(p, gen)) {
        t->Written |= TBT_U4_E_DEV_LANE;
        rc = tbtU4WriteEntry(cm, &d->Route, dp, TBT_HOP_PROTOCOL, d->UpAdp, t->Hu,
                             d->Tbt3 ? TBT_TBT3_PROTO_CREDITS : 0, weight, FALSE,
                             d->Tbt3);
    }
    if (rc == TBT_OK && TBT_U4_LIVE(p, gen)) {
        t->Written |= TBT_U4_E_DEV_PROTO;
        rc = tbtU4WriteEntry(cm, &h->Route, p->Lane0, t->Hu, hp, TBT_HOP_PROTOCOL,
                             t->CreditsHost, weight, TRUE, FALSE);
        if (rc == TBT_OK) {
            t->Written |= TBT_U4_E_HOST_LANE;
        }
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

/*
 * Section 6.8: the PCIe tunnel. Both PCIe adapters in Detect within 500 ms
 * (only the USB4 end for a Thunderbolt 3 router, Linux tunnel.c
 * tb_pci_pre_activate) - section 10.1's gate; the four entries; Path
 * Enable on the device's PCIe Up, then the host's PCIe Down (Linux
 * tb_pci_activate); LTSSM L0 on the host's within 1 s. A failure after any
 * write tears down exactly what was written.
 */
static LONG tbtU4PcieTunnel(PTBT_U4CM cm, PTBT_U4_PORT p, ULONG gen)
{
    PTBT_U4_ROUTER h;
    PTBT_U4_ROUTER d;
    ULONG dp;
    ULONG v;
    LONG rc;
    TBT_WAIT w;

    h = &cm->HostRouter;
    d = &p->Dev;
    dp = tbtU4FirstOf(d, TBT_ADP_TYPE_PCIE_UP);
    if (p->PcieDown == 0 || dp == 0 || h->Adp[p->PcieDown].CapAdp == 0 ||
        d->Adp[dp].CapAdp == 0) {
        p->Why = TBT_U4_WHY_NO_ADAPTERS;
        return TBT_E_STATE;
    }
    if (!tbtU4AdapterFree(cm, &h->Route, p->PcieDown)) {
        p->Why = TBT_U4_WHY_BLOCKED;
        return TBT_E_STATE;
    }
    tbtU4Zero(&p->Pcie, sizeof(p->Pcie));
    if (!tbtU4WaitDetect(cm, p, gen, h, p->PcieDown, TBT_U4_DETECT_US) ||
        (!d->Tbt3 && !tbtU4WaitDetect(cm, p, gen, d, dp, TBT_U4_DETECT_US))) {
        if (!TBT_U4_LIVE(p, gen)) {
            return TBT_E_GONE;
        }
        cm->Counters.DetectTimeouts++;
        p->Why = TBT_U4_WHY_DETECT;
        return TBT_E_TIMEOUT;
    }
    rc = tbtU4Paths(cm, p, gen, &p->Pcie, FALSE, tbtU4WantUsb3(cm, p));
    if (rc == TBT_OK) {
        rc = tbtU4Rmw(cm, &d->Route, TBT_CS_ADAPTER, dp, d->Adp[dp].CapAdp + TBT_PCIE_CS_0,
                      0, TBT_PCIE0_PE);
        if (rc == TBT_OK) {
            p->Pcie.Written |= TBT_U4_E_DEV_ENABLE;
            rc = tbtU4Rmw(cm, &h->Route, TBT_CS_ADAPTER, p->PcieDown,
                          h->Adp[p->PcieDown].CapAdp + TBT_PCIE_CS_0, 0, TBT_PCIE0_PE);
        }
        if (rc == TBT_OK) {
            p->Pcie.Written |= TBT_U4_E_HOST_ENABLE;
        }
    }
    if (rc == TBT_OK) {
        TbtWaitStart(&w, cm->Host, TBT_U4_L0_US, FALSE);
        for (;;) {
            rc = tbtU4Rd(cm, &h->Route, TBT_CS_ADAPTER, p->PcieDown,
                         h->Adp[p->PcieDown].CapAdp + TBT_PCIE_CS_0, &v);
            if (rc != TBT_OK || TBT_PCIE0_LTSSM(v) == TBT_LTSSM_L0) {
                break;
            }
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

/* USB3 bandwidth in the adapters' units (Linux usb4.c mbps_to_usb3_bw,
 * usb3_bw_to_mbps: 512 << scale bytes per 125 us microframe), in 32 bits:
 * Mb/s times 125 stays below 2^23 at 40 Gb/s, and the scale is held to 16. */
static ULONG tbtU4BwUnits(ULONG mbps, ULONG scale)
{
    ULONG d;

    d = 512UL << scale;
    return (mbps * 125UL + d - 1) / d;
}

static ULONG tbtU4BwMbps(ULONG units, ULONG scale)
{
    ULONG q;

    q = units << scale;     /* q * 512 / 125 = q * 4 + q * 12 / 125 */
    return q * 4UL + (q * 12UL + 62UL) / 125UL;
}

/* Set or clear CMR and wait for HCA to follow (Linux usb4.c
 * usb4_usb3_port_cm_request), 1.5 s. */
static LONG tbtU4Cmr(PTBT_U4CM cm, PTBT_U4_PORT p, BOOLEAN request)
{
    PTBT_U4_ROUTER h;
    ULONG cap;
    ULONG v;
    LONG rc;
    TBT_WAIT w;

    h = &cm->HostRouter;
    cap = h->Adp[p->Usb3Down].CapAdp;
    rc = tbtU4Rmw(cm, &h->Route, TBT_CS_ADAPTER, p->Usb3Down, cap + TBT_USB3_CS_2,
                  request ? 0 : TBT_USB3_2_CMR, request ? TBT_USB3_2_CMR : 0);
    if (rc != TBT_OK) {
        return rc;
    }
    TbtWaitStart(&w, cm->Host, TBT_U4_HCA_US, FALSE);
    for (;;) {
        rc = tbtU4Rd(cm, &h->Route, TBT_CS_ADAPTER, p->Usb3Down, cap + TBT_USB3_CS_1, &v);
        if (rc != TBT_OK) {
            return rc;
        }
        if (((v & TBT_USB3_1_HCA) != 0) == (request != FALSE)) {
            return TBT_OK;
        }
        if (TbtWaitDone(&w, cm->Host)) {
            cm->Counters.HcaTimeouts++;
            return TBT_E_TIMEOUT;
        }
        tbtU4Pause(cm, &w);
    }
}

/* Write an allocation at the smallest scale that fits 12 bits (Linux
 * usb4.c usb4_usb3_port_write_allocated_bandwidth), CMR held. */
static LONG tbtU4WriteBw(PTBT_U4CM cm, PTBT_U4_PORT p, ULONG up, ULONG down)
{
    PTBT_U4_ROUTER h;
    ULONG cap;
    ULONG scale;
    ULONG most;
    LONG rc;

    h = &cm->HostRouter;
    cap = h->Adp[p->Usb3Down].CapAdp;
    most = up > down ? up : down;
    for (scale = 0; scale < 16 && tbtU4BwUnits(most, scale) >= 4096; scale++) {
    }
    rc = tbtU4Wr(cm, &h->Route, TBT_CS_ADAPTER, p->Usb3Down, cap + TBT_USB3_CS_3, scale);
    if (rc == TBT_OK) {
        rc = tbtU4Rmw(cm, &h->Route, TBT_CS_ADAPTER, p->Usb3Down, cap + TBT_USB3_CS_2,
                      TBT_USB3_2_BW_MASK,
                      (tbtU4BwUnits(up, scale) & 0xFFFUL) |
                      ((tbtU4BwUnits(down, scale) & 0xFFFUL) << TBT_USB3_2_ADBW_SHIFT));
    }
    return rc;
}

/* Consumed bandwidth in Mb/s, CMR held. */
static LONG tbtU4Consumed(PTBT_U4CM cm, PTBT_U4_PORT p, PULONG up, PULONG down)
{
    PTBT_U4_ROUTER h;
    ULONG cap;
    ULONG v[3];
    LONG rc;

    h = &cm->HostRouter;
    cap = h->Adp[p->Usb3Down].CapAdp;
    rc = tbtU4RdN(cm, &h->Route, TBT_CS_ADAPTER, p->Usb3Down, cap + TBT_USB3_CS_1, 3, v);
    if (rc == TBT_OK) {
        *up = tbtU4BwMbps(TBT_USB3_1_CUBW(v[0]), TBT_USB3_3_SCALE(v[2]) > 16 ? 16 :
                          TBT_USB3_3_SCALE(v[2]));
        *down = tbtU4BwMbps(TBT_USB3_1_CDBW(v[0]), TBT_USB3_3_SCALE(v[2]) > 16 ? 16 :
                            TBT_USB3_3_SCALE(v[2]));
    }
    return rc;
}

/* An adapter's maximum USB3 link rate (Linux usb4.c
 * usb4_usb3_port_max_link_rate): 20 Gb/s for MSLR 1, else 10. */
static ULONG tbtU4MaxRate(PTBT_U4CM cm, const TBT_U4_ROUTER *rt, ULONG adp)
{
    ULONG v;

    if (tbtU4Rd(cm, &rt->Route, TBT_CS_ADAPTER, adp, rt->Adp[adp].CapAdp + TBT_USB3_CS_4,
                &v) != TBT_OK) {
        return 0;
    }
    return TBT_USB3_4_MSLR(v) == TBT_USB3_MSLR_20G ? 20000UL : 10000UL;
}

/* Release to the consumed value, never below 900 Mb/s each way (Linux
 * usb4.c usb4_usb3_port_release_bandwidth); CMR cleared on every exit.
 * TRUE when the handshake completed. */
static BOOLEAN tbtU4ReleaseBw(PTBT_U4CM cm, PTBT_U4_PORT p)
{
    ULONG up;
    ULONG down;
    LONG rc;

    rc = tbtU4Cmr(cm, p, TRUE);
    if (rc == TBT_OK) {
        rc = tbtU4Consumed(cm, p, &up, &down);
        if (rc == TBT_OK) {
            rc = tbtU4WriteBw(cm, p, up < TBT_U4_USB3_KEEP_MBPS ? TBT_U4_USB3_KEEP_MBPS : up,
                              down < TBT_U4_USB3_KEEP_MBPS ? TBT_U4_USB3_KEEP_MBPS : down);
        }
    }
    if (tbtU4Cmr(cm, p, FALSE) != TBT_OK) {
        p->BwQuarantine = TRUE;
        return FALSE;
    }
    if (rc == TBT_OK) {
        p->Usb3.Written &= ~TBT_U4_E_BANDWIDTH;
    }
    return (BOOLEAN)(rc == TBT_OK);
}

/*
 * Section 6.9: the USB3 tunnel - at least 500 ms after the port's last
 * USB3 teardown; the TMU's accuracy (or no TMU on the device); the
 * bandwidth handshake on the host's USB3 Down adapter; the four entries
 * at weight 2; Path Enable with Valid on the host's USB3 Down, then the
 * device's USB3 Up. Every failure unwinds what it acquired and leaves the
 * PCIe tunnel untouched.
 */
static LONG tbtU4Usb3Tunnel(PTBT_U4CM cm, PTBT_U4_PORT p, ULONG gen)
{
    PTBT_U4_ROUTER h;
    PTBT_U4_ROUTER d;
    ULONG dp;
    ULONG up;
    ULONG down;
    ULONG rate;
    ULONG rateD;
    ULONG lane;
    ULONG raw;
    ULONG avail;
    ULONG alloc;
    ULONG since;
    LONG rc;

    h = &cm->HostRouter;
    d = &p->Dev;
    dp = tbtU4FirstOf(d, TBT_ADP_TYPE_USB3_UP);
    if (dp == 0 || h->Adp[p->Usb3Down].CapAdp == 0 || d->Adp[dp].CapAdp == 0) {
        return TBT_E_STATE;
    }
    if (p->BwQuarantine || !tbtU4AdapterFree(cm, &h->Route, p->Usb3Down) || !p->TmuOk) {
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

    /* The allocation (Linux usb4.c, tunnel.c tb_tunnel_alloc_usb3; Guide
     * 6.2.2.1, 6.1.2.3): each adapter's maximum link rate, the host's
     * clamped by the quirk, the lower; 90 per cent of it; no more than the
     * link's 90 per cent less 1.5 Gb/s for PCIe; never below consumed. */
    rate = tbtU4MaxRate(cm, h, p->Usb3Down);
    if ((h->Quirks & TBT_U4_QUIRK_USB3_MAX_BW) != 0 && rate > TBT_U4_USB3_QUIRK_MBPS) {
        rate = TBT_U4_USB3_QUIRK_MBPS;
    }
    rateD = tbtU4MaxRate(cm, d, dp);
    if (rateD < rate) {
        rate = rateD;
    }
    alloc = rate * 9UL / 10UL;
    if (tbtU4LaneRd(cm, h, p->Lane0, &lane) != TBT_OK) {
        return TBT_E_STATE;
    }
    switch (TBT_LANE1_SPEED(lane)) {
    case TBT_LANE_SPEED_GEN4:
        raw = 40000;
        break;
    case TBT_LANE_SPEED_GEN3:
        raw = 20000;
        break;
    default:
        raw = 10000;
        break;
    }
    if (TBT_LANE1_NLW(lane) == TBT_LANE_WIDTH_X2) {
        raw *= 2;
    }
    avail = raw * 9UL / 10UL;
    avail = avail > TBT_U4_PCIE_RESERVE_MBPS ? avail - TBT_U4_PCIE_RESERVE_MBPS : 0;
    if (alloc > avail) {
        alloc = avail;
    }

    rc = tbtU4Cmr(cm, p, TRUE);
    if (rc == TBT_OK) {
        rc = tbtU4Consumed(cm, p, &up, &down);
        if (rc == TBT_OK) {
            rc = tbtU4WriteBw(cm, p, alloc > up ? alloc : up, alloc > down ? alloc : down);
            if (rc == TBT_OK) {
                p->Usb3.Written |= TBT_U4_E_BANDWIDTH;
            }
        }
    }
    if (tbtU4Cmr(cm, p, FALSE) != TBT_OK) {
        p->BwQuarantine = TRUE;
        if (rc == TBT_OK) {
            rc = TBT_E_TIMEOUT;
        }
    }
    if (rc == TBT_OK) {
        rc = tbtU4Paths(cm, p, gen, &p->Usb3, TRUE, FALSE);
    }
    if (rc == TBT_OK) {
        rc = tbtU4Rmw(cm, &h->Route, TBT_CS_ADAPTER, p->Usb3Down,
                      h->Adp[p->Usb3Down].CapAdp + TBT_USB3_CS_0, 0,
                      TBT_USB3_0_PE | TBT_USB3_0_V);
        if (rc == TBT_OK) {
            p->Usb3.Written |= TBT_U4_E_HOST_ENABLE;
            rc = tbtU4Rmw(cm, &d->Route, TBT_CS_ADAPTER, dp, d->Adp[dp].CapAdp + TBT_USB3_CS_0,
                          0, TBT_USB3_0_PE | TBT_USB3_0_V);
        }
        if (rc == TBT_OK) {
            p->Usb3.Written |= TBT_U4_E_DEV_ENABLE;
        }
    }
    if (rc != TBT_OK || !TBT_U4_LIVE(p, gen)) {
        if (rc == TBT_OK) {
            return TBT_E_GONE;
        }
        if (rc == TBT_E_GONE) {
            return rc;
        }
        cm->Counters.Usb3Failed++;
        p->Usb3Failed = TRUE;
        tbtU4Log(cm, "u4.usb3.failed", (ULONG)rc);
        (void)tbtU4TunnelDown(cm, p, &p->Usb3, TRUE);
        if ((p->Usb3.Written & TBT_U4_E_BANDWIDTH) != 0) {
            (void)tbtU4ReleaseBw(cm, p);
        }
        return rc;
    }
    p->Usb3.Up = TRUE;
    cm->Counters.Usb3Built++;
    tbtU4Log(cm, "u4.usb3.up", (p->Index << 16) | (p->Usb3.Hd << 8) | p->Usb3.Hu);
    return TBT_OK;
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
    if ((p->Pcie.Written & TBT_U4_E_HOST_PROTO) != 0) {
        ok &= tbtU4Invalidate(cm, p->Index, &h->Route, p->PcieDown, TBT_HOP_PROTOCOL);
    }
    if ((p->Pcie.Written & TBT_U4_E_HOST_LANE) != 0) {
        ok &= tbtU4Invalidate(cm, p->Index, &h->Route, p->Lane0, p->Pcie.Hu);
    }
    if (ok) {
        ok = tbtU4WaitDetect(cm, NULL, 0, h, p->PcieDown, TBT_U4_DETECT_US);
    }
    p->Pcie.Written = 0;
    p->Pcie.Up = FALSE;
    p->Pcie.Preserved = FALSE;
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

    info.Port = p->Index;
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
    } else {
        cm->Counters.StatementsWithheld++;
    }
}

/* Steps 1 to 3. */
static VOID tbtU4TeardownHead(PTBT_U4CM cm, PTBT_U4_PORT p, BOOLEAN nested)
{
    BOOLEAN hadPcie;
    BOOLEAN closed;

    p->State = TBT_U4_TEARDOWN;
    cm->Counters.Unplugs++;
    hadPcie = (BOOLEAN)(p->Pcie.Up || p->Pcie.Written != 0);
    p->RemovalOwed = FALSE;
    if (hadPcie && p->PcieDown != 0) {
        closed = tbtU4PcieHostDown(cm, p);
        cm->Counters.PcieTornDown++;
        tbtU4LinkDownUpcall(cm, p, closed, nested);
        p->RemovalOwed = TRUE;
    }
    p->Pcie.Written = 0;
    p->Pcie.Up = FALSE;
}

/*
 * Steps 4 to 6: the host side of the USB3 tunnel (Guide 5.5.2, the removed
 * router's step skipped) - Path Enable 0 with Valid 1, its two host-side
 * entries invalidated and drained, the bandwidth released; the port's lane
 * adapters back to their defaults where this driver changed them (the TMU
 * of the host's port, USB4 Port is Configured, Target Link Width and Lane
 * Bonding, Lane Disable; Linux switch.c tb_switch_unconfigure_link,
 * tb_switch_lane_bonding_disable); then the removal queued for the OS.
 */
static VOID tbtU4TeardownTail(PTBT_U4CM cm, PTBT_U4_PORT p)
{
    PTBT_U4_ROUTER h;
    BOOLEAN removal;
    ULONG k;

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
        cm->Counters.Usb3TornDown++;
        p->LastUsb3TeardownUs = tbtU4Now(cm);
        p->Usb3GapValid = TRUE;
    }
    if ((p->Usb3.Written & TBT_U4_E_BANDWIDTH) != 0 && p->Usb3Down != 0) {
        (void)tbtU4ReleaseBw(cm, p);
    }
    tbtU4Zero(&p->Usb3, sizeof(p->Usb3));
    if (p->TmuConfigured) {
        if (tbtU4TmuAdp(cm, h, p->Lane0, TBT_TMU_ADP_CS_3, TBT_TMU_ADP3_UDM, 0) != TBT_OK ||
            tbtU4TmuAdp(cm, h, p->Lane0, TBT_TMU_ADP_CS_6, 0, TBT_TMU_ADP6_DTS) != TBT_OK) {
            p->TmuQuarantine = TRUE;
        }
        p->TmuConfigured = FALSE;
    }
    p->TmuOk = FALSE;
    if (p->PcSetHost) {
        (void)tbtU4Rmw(cm, &h->Route, TBT_CS_ADAPTER, p->Lane0,
                       h->Adp[p->Lane0].CapPort + TBT_PORT_CS_19, TBT_PORT19_PC, 0);
        p->PcSetHost = FALSE;
    }
    if (p->LaneWritten) {
        for (k = 0; k < 2; k++) {
            (void)tbtU4LaneRmw(cm, h, p->Lane0 + k, TBT_LANE1_TLW_MASK | TBT_LANE1_LB,
                               p->LaneSaved[k] & (TBT_LANE1_TLW_MASK | TBT_LANE1_LB));
        }
        p->LaneWritten = FALSE;
    }
    if (p->Lane1Disabled) {
        (void)tbtU4LaneRmw(cm, h, p->Lane0 + 1, TBT_LANE1_LD, 0);
        p->Lane1Disabled = FALSE;
    }
    TbtCtlForget(&cm->Ctl, &p->Dev.Route);
    tbtU4Zero(&p->Dev, sizeof(p->Dev));
    tbtU4Zero(&p->Pcie, sizeof(p->Pcie));
    p->Bonded = FALSE;
    p->SingleLane = FALSE;
    p->PcSetDev = FALSE;
    p->Unlocked = FALSE;
    p->PcieFailed = FALSE;
    p->Usb3Failed = FALSE;
    p->Ejected = FALSE;
    p->DpPreserved = FALSE;
    p->Transitional = FALSE;
    p->AnchorValid = FALSE;
    p->UnplugPending = FALSE;
    p->State = TBT_U4_EMPTY;
    p->Why = TBT_U4_WHY_NONE;
    p->CheckPending = TRUE;
    if (removal) {
        tbtU4Queue(cm, TBT_U4_ACT_TUNNEL_DOWN, p);
    }
}

static VOID tbtU4Teardown(PTBT_U4CM cm, PTBT_U4_PORT p)
{
    tbtU4TeardownHead(cm, p, FALSE);
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
    LONG rc;

    h = &cm->HostRouter;
    d = &p->Dev;
    *cls = TBT_U4_LANE_UNKNOWN;
    if (tbtU4LaneRd(cm, h, p->Lane0, &lane) != TBT_OK ||
        tbtU4Rd(cm, &h->Route, TBT_CS_ADAPTER, p->Lane0, TBT_ADP_CS_4, &cs4) != TBT_OK) {
        return FALSE;
    }
    *cls = TbtU4LaneClass(TBT_LANE1_STATE(lane));
    if (*cls == TBT_U4_LANE_DISCONNECTED) {
        return TRUE;
    }
    if (p->Unlocked && (cs4 & TBT_ADP4_LOCK) != 0) {
        return TRUE;
    }
    if (!d->Present) {
        return FALSE;
    }
    rc = tbtU4Rd(cm, &d->Route, TBT_CS_ROUTER, 0, TBT_ROUTER_CS_3, &v[0]);
    if (tbtU4Gone(cm, rc)) {
        return TRUE;
    }
    if (rc == TBT_OK && (v[0] & TBT_RTR3_V) == 0) {
        return TRUE;
    }
    if (!d->Tbt3 && rc == TBT_OK) {
        rc = tbtU4RdN(cm, &d->Route, TBT_CS_ROUTER, 0, TBT_ROUTER_CS_7, 2, v);
        if (tbtU4Gone(cm, rc)) {
            return TRUE;
        }
        if (rc == TBT_OK && (v[0] != d->Uuid[0] || v[1] != d->Uuid[1])) {
            return TRUE;
        }
    }
    return FALSE;
}

/* ---------------------------------------------------------------------- */
/* Connecting a port (record 22 sections 6.4 to 6.9, 7.2, 8)             */
/* ---------------------------------------------------------------------- */

/*
 * EMPTY to UP (or FAILED, UNSUPPORTED). `adopted` skips the enumeration
 * of a router discovery found already enumerated with no tunnel state;
 * `asFound` builds a missing PCIe tunnel on the link as found (section
 * 9.2's DisplayPort case: no bonding, no lane change, no USB3).
 */
static VOID tbtU4Connect(PTBT_U4CM cm, PTBT_U4_PORT p, BOOLEAN asFound)
{
    ULONG gen;
    ULONG next;
    ULONG lane;
    ULONG cls;
    BOOLEAN pcieWanted;
    LONG rc;
    TBT_WAIT w;

    pcieWanted = FALSE;
    next = TBT_U4_FAILED;
    cm->Counters.Connects++;
    p->Generation++;
    gen = p->Generation;
    p->UnplugPending = FALSE;
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
            p->State = TBT_U4_EMPTY;
            cm->ActivePort = 0;
            return;
        }
        tbtU4Pause(cm, &w);
    }

    rc = tbtU4Router(cm, p, gen, &next);
    if (rc != TBT_OK) {
        goto out;
    }
    tbtU4TmuAtConnect(cm, p);

    p->State = TBT_U4_BONDING;
    if (asFound) {
        p->Bonded = (BOOLEAN)(tbtU4LaneRd(cm, &cm->HostRouter, p->Lane0, &lane) == TBT_OK &&
                              TBT_LANE1_NLW(lane) == TBT_LANE_WIDTH_X2);
        p->SingleLane = (BOOLEAN)!p->Bonded;
    } else {
        rc = tbtU4Bond(cm, p, gen);
        if (rc != TBT_OK) {
            next = TBT_U4_FAILED;
            goto out;
        }
    }

    if (!p->Dev.Tbt3 && !asFound) {
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
    pcieWanted = (BOOLEAN)(cm->Config.Approve && p->PcieDown != 0 &&
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
    cm->ActivePort = 0;
    if (rc == TBT_E_GONE || !TBT_U4_LIVE(p, gen)) {
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

/* Follow one tunnel as Linux's discovery walks it (tunnel.c
 * tb_tunnel_discover_pci / _usb3, path.c tb_path_discover): the host
 * protocol adapter's Path Enable, its HopID 8 entry to the host lane 0 at
 * Hd, the device lane 0's Hd entry to the device protocol adapter at 8,
 * that adapter's HopID 8 entry to the device lane 0 at Hu, the host lane
 * 0's Hu entry back to the host protocol adapter at 8, and the device
 * adapter's Path Enable; `found` records each part seen. TRUE when all are
 * there and consistent. */
static BOOLEAN tbtU4Follow(PTBT_U4CM cm, PTBT_U4_PORT p, BOOLEAN usb3,
                           PTBT_U4_TUNNEL t)
{
    PTBT_U4_ROUTER h;
    PTBT_U4_ROUTER d;
    ULONG hp;
    ULONG dp;
    ULONG bit;
    ULONG v;
    ULONG e[2];

    h = &cm->HostRouter;
    d = &p->Dev;
    hp = usb3 ? p->Usb3Down : p->PcieDown;
    bit = usb3 ? TBT_USB3_0_PE : TBT_PCIE0_PE;
    tbtU4Zero(t, sizeof(*t));
    if (hp == 0 || h->Adp[hp].CapAdp == 0) {
        return FALSE;
    }
    if (tbtU4Rd(cm, &h->Route, TBT_CS_ADAPTER, hp, h->Adp[hp].CapAdp, &v) == TBT_OK &&
        (v & bit) != 0) {
        t->Written |= TBT_U4_E_HOST_ENABLE;
    }
    if (tbtU4Entry(cm, &h->Route, hp, TBT_HOP_PROTOCOL, e) == TBT_OK &&
        (e[0] & TBT_PATH0_VALID) != 0) {
        t->Written |= TBT_U4_E_HOST_PROTO;
        if (TBT_PATH0_OUT_ADP(e[0]) == p->Lane0 && TBT_PATH0_OUT_HOP(e[0]) >= TBT_HOP_FIRST) {
            t->Hd = TBT_PATH0_OUT_HOP(e[0]);
        }
    }
    /* The host lane's entry back to this adapter. */
    {
        ULONG hop;
        ULONG top;

        top = h->Adp[p->Lane0].MaxInHop;
        for (hop = TBT_HOP_FIRST; hop <= top; hop++) {
            if (tbtU4Entry(cm, &h->Route, p->Lane0, hop, e) == TBT_OK &&
                (e[0] & TBT_PATH0_VALID) != 0 && TBT_PATH0_OUT_ADP(e[0]) == hp &&
                TBT_PATH0_OUT_HOP(e[0]) == TBT_HOP_PROTOCOL) {
                t->Written |= TBT_U4_E_HOST_LANE;
                t->Hu = hop;
                break;
            }
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
        (e[0] & TBT_PATH0_VALID) != 0 && TBT_PATH0_OUT_ADP(e[0]) == dp &&
        TBT_PATH0_OUT_HOP(e[0]) == TBT_HOP_PROTOCOL) {
        t->Written |= TBT_U4_E_DEV_LANE;
    }
    if (tbtU4Entry(cm, &d->Route, dp, TBT_HOP_PROTOCOL, e) == TBT_OK &&
        (e[0] & TBT_PATH0_VALID) != 0) {
        t->Written |= TBT_U4_E_DEV_PROTO;
        if (TBT_PATH0_OUT_ADP(e[0]) != d->UpAdp || TBT_PATH0_OUT_HOP(e[0]) != t->Hu) {
            t->Written &= ~TBT_U4_E_DEV_PROTO;
        }
    }
    if (tbtU4Rd(cm, &d->Route, TBT_CS_ADAPTER, dp, d->Adp[dp].CapAdp, &v) == TBT_OK &&
        (v & bit) != 0) {
        t->Written |= TBT_U4_E_DEV_ENABLE;
    }
    return (BOOLEAN)((t->Written & 0x3FUL) == 0x3FUL && t->Hd != 0 && t->Hu != 0);
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
        s = TBT_PCIE0_LTSSM(v);
        if ((v & TBT_PCIE0_PE) == 0 || s == TBT_LTSSM_DETECT) {
            return FALSE;
        }
        if (s == TBT_LTSSM_L0 || s == TBT_LTSSM_L1 || s == TBT_LTSSM_RECOVERY ||
            s == TBT_LTSSM_RECOVERY_IDLE) {
            return TRUE;
        }
        if (TbtWaitDone(&w, cm->Host)) {
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

/*
 * Per port at start, reading only (section 9.2). The outcomes: all four
 * PCIe checks - UP and preserved; no tunnel state at all - connected
 * normally (an enumerated router adopted); only tunnels this driver does
 * not build - reserved, and a missing PCIe tunnel built on the link as
 * found; anything else - HELD. A disconnected port's residual host-side
 * paths are torn down as an unplug's are.
 */
static VOID tbtU4DiscoverPort(PTBT_U4CM cm, PTBT_U4_PORT p)
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
    TBT_U4_TUNNEL tp;
    TBT_U4_TUNNEL tu;
    LONG rc;

    h = &cm->HostRouter;
    d = &p->Dev;
    if (tbtU4LaneRd(cm, h, p->Lane0, &lane) != TBT_OK ||
        tbtU4Rd(cm, &h->Route, TBT_CS_ADAPTER, p->Lane0, TBT_ADP_CS_4, &cs4) != TBT_OK) {
        p->State = TBT_U4_HELD;
        p->Why = TBT_U4_WHY_LANE_STATE;
        cm->Counters.PortsHeld++;
        return;
    }
    cls = TbtU4LaneClass(TBT_LANE1_STATE(lane));
    tbtU4QuarantinePending(cm, p, h, p->Lane0);
    tbtU4QuarantinePending(cm, p, h, p->PcieDown);
    tbtU4QuarantinePending(cm, p, h, p->Usb3Down);
    hostState = tbtU4AnyState(cm, p, h, p->Lane0, p->PcieDown, p->Usb3Down);
    other = tbtU4OtherPaths(cm, p);

    if (cls == TBT_U4_LANE_DISABLED) {
        p->State = TBT_U4_HELD;
        p->Why = TBT_U4_WHY_LANE_STATE;
        cm->Counters.PortsHeld++;
        return;
    }
    if (cls == TBT_U4_LANE_DISCONNECTED || cls == TBT_U4_LANE_UNKNOWN) {
        if (hostState) {
            /* No tunnel can exist through a disconnected lane: unplug
             * handling, not a reset. Both tunnels' host sides found. */
            (void)tbtU4Follow(cm, p, FALSE, &p->Pcie);
            (void)tbtU4Follow(cm, p, TRUE, &p->Usb3);
            p->Usb3.Written &= ~(TBT_U4_E_DEV_LANE | TBT_U4_E_DEV_PROTO | TBT_U4_E_DEV_ENABLE);
            p->Pcie.Written &= ~(TBT_U4_E_DEV_LANE | TBT_U4_E_DEV_PROTO | TBT_U4_E_DEV_ENABLE);
            p->Pcie.Written |= TBT_U4_E_HOST_ENABLE;
            tbtU4Teardown(cm, p);
        }
        p->State = TBT_U4_EMPTY;
        return;
    }

    /* Connected (or training: re-read by the first check). */
    if ((cs4 & TBT_ADP4_LOCK) != 0) {
        if (hostState || other) {
            p->State = TBT_U4_HELD;
            p->Why = TBT_U4_WHY_DISCOVERY;
            cm->Counters.PortsHeld++;
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
            p->State = TBT_U4_HELD;
            p->Why = TBT_U4_WHY_DISCOVERY;
            cm->Counters.PortsHeld++;
        } else {
            p->State = TBT_U4_EMPTY;
            p->CheckPending = TRUE;
        }
        return;
    }
    d->Present = TRUE;
    d->Preserved = TRUE;
    cm->Counters.RoutersAdopted++;
    if (tbtU4DiscoverAdapters(cm, d) != TBT_OK ||
        tbtU4RouterCaps(cm, d, TBT_RTR1_NEXT_CAP(v[1])) != TBT_OK ||
        d->UpAdp == 0 || d->UpAdp + 1 >= TBT_U4_MAX_ADP) {
        tbtU4Zero(d, sizeof(*d));
        p->State = TBT_U4_HELD;
        p->Why = TBT_U4_WHY_DISCOVERY;
        cm->Counters.PortsHeld++;
        return;
    }
    tbtU4ApplyQuirks(cm, d);
    if (!d->Tbt3) {
        (void)tbtU4RdN(cm, &d->Route, TBT_CS_ROUTER, 0, TBT_ROUTER_CS_7, 2, d->Uuid);
    } else {
        d->CapsState = TBT_U4_CAPS_DEFAULT;
        if (d->CapLc != 0) {
            (void)tbtU4RdN(cm, &d->Route, TBT_CS_ROUTER, 0, d->CapLc + TBT_LC_FUSE, 4, d->Uuid);
        }
    }
    devState = tbtU4AnyState(cm, p, d, d->UpAdp, tbtU4FirstOf(d, TBT_ADP_TYPE_PCIE_UP),
                             tbtU4FirstOf(d, TBT_ADP_TYPE_USB3_UP));
    /* Step 2: the router's configuration (section 8's for Thunderbolt 3). */
    cfgOk = TRUE;
    if (!d->Tbt3) {
        cfgOk = (BOOLEAN)((v[6] & TBT_RTR6_RR) != 0 && (v[5] & TBT_RTR5_CV) != 0 &&
                          (v[6] & TBT_RTR6_CR) != 0);
    }
    /* Step 3: bonded, or lane 1 disconnected or disabled. */
    if (tbtU4LaneRd(cm, h, p->Lane0 + 1, &lane1) != TBT_OK) {
        cfgOk = FALSE;
    } else if (TBT_LANE1_NLW(lane) == TBT_LANE_WIDTH_X2) {
        p->Bonded = TRUE;
    } else if (TBT_LANE1_STATE(lane1) == TBT_LANE_STATE_CLD ||
               TBT_LANE1_STATE(lane1) == TBT_LANE_STATE_DISABLED) {
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

    if (!hostState && !devState && !other) {
        /* Nothing to preserve: connected normally, the router adopted. */
        p->State = TBT_U4_EMPTY;
        tbtU4Zero(d, sizeof(*d));
        p->Bonded = FALSE;
        p->SingleLane = FALSE;
        p->CheckPending = TRUE;
        return;
    }
    if (cfgOk && pcie) {
        p->Pcie = tp;
        p->Pcie.Up = TRUE;
        p->Pcie.Preserved = TRUE;
        if (usb3) {
            p->Usb3 = tu;
            p->Usb3.Up = TRUE;
            p->Usb3.Preserved = TRUE;
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
    if (cfgOk && other && tp.Written == 0 && tu.Written == 0) {
        /* Only tunnels this driver does not build: reserved; a missing
         * PCIe tunnel built on the link as found, no USB3 beside DP. */
        p->DpPreserved = TRUE;
        tbtU4Zero(d, sizeof(*d));
        p->Bonded = FALSE;
        p->SingleLane = FALSE;
        cm->Counters.PortsPreserved++;
        tbtU4Connect(cm, p, TRUE);
        return;
    }
    /* Anything else: left exactly as found. Its host-side parts are kept
     * for its unplug's teardown. */
    p->Pcie = tp;
    p->Pcie.Written &= TBT_U4_E_HOST_PROTO | TBT_U4_E_HOST_LANE | TBT_U4_E_HOST_ENABLE;
    p->Usb3 = tu;
    p->Usb3.Written &= TBT_U4_E_HOST_PROTO | TBT_U4_E_HOST_LANE | TBT_U4_E_HOST_ENABLE;
    p->DpPreserved = other;
    p->State = TBT_U4_HELD;
    p->Why = TBT_U4_WHY_DISCOVERY;
    p->Generation++;
    cm->Counters.PortsHeld++;
    tbtU4Log(cm, "u4.held", (p->Index << 16) | (tp.Written << 8) | tu.Written);
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
    ULONG k;

    s = &cm->Scratch;
    first = 0;
    parent = p->Dev.Route;
    for (;;) {
        tbtU4Zero(s, sizeof(*s));
        s->Route = parent;
        if (tbtU4RdN(cm, &s->Route, TBT_CS_ROUTER, 0, TBT_ROUTER_CS_0, 5, v) == TBT_OK) {
            tbtU4TakeHeader(s, v);
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
                    l->ChildUuid[0] = v[7];
                    l->ChildUuid[1] = v[8];
                    cm->Counters.LinksDiscovered++;
                    tbtU4Log(cm, "u4.link", (TbtRouteDepth(&child) << 8) | a);
                }
            }
        }
        /* The next link not yet walked, in table order. */
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
        first = i + 1;
    }
}

/* The removal of a router below depth 1 (section 7.1): on the parent, the
 * side of every path through the removed router - entries leading into its
 * lanes, entries arriving from them, Path Enable on the protocol adapters
 * that fed them - torn down with section 7.3's quarantine; the link and
 * every link below it forgotten. */
static VOID tbtU4RemoveLink(PTBT_U4CM cm, ULONG index)
{
    PTBT_U4_LINK l;
    PTBT_U4_ROUTER s;
    ULONG v[5];
    ULONG a;
    ULONG hop;
    ULONG e[2];
    ULONG k;
    BOOLEAN into;
    BOOLEAN from;
    TBT_ROUTE removed;
    PTBT_U4_PORT p;

    l = &cm->Links[index];
    removed = l->Child;
    s = &cm->Scratch;
    tbtU4Zero(s, sizeof(*s));
    s->Route = l->Parent;
    p = tbtU4PortOf(cm, l->Port);
    cm->Counters.LinksRemoved++;
    tbtU4Log(cm, "u4.link-removed", l->ParentLane0);
    if (tbtU4RdN(cm, &s->Route, TBT_CS_ROUTER, 0, TBT_ROUTER_CS_0, 5, v) == TBT_OK) {
        tbtU4TakeHeader(s, v);
        if (tbtU4DiscoverAdapters(cm, s) == TBT_OK) {
            for (a = 1; a <= s->MaxAdp && a < TBT_U4_MAX_ADP; a++) {
                if (!s->Adp[a].Present) {
                    continue;
                }
                from = (BOOLEAN)(a == l->ParentLane0 || a == l->ParentLane0 + 1);
                for (hop = TBT_HOP_FIRST; hop <= s->Adp[a].MaxInHop; hop++) {
                    if (tbtU4Entry(cm, &s->Route, a, hop, e) != TBT_OK ||
                        (e[0] & TBT_PATH0_VALID) == 0) {
                        continue;
                    }
                    into = (BOOLEAN)(TBT_PATH0_OUT_ADP(e[0]) == l->ParentLane0 ||
                                     TBT_PATH0_OUT_ADP(e[0]) == l->ParentLane0 + 1);
                    if (!into && !from) {
                        continue;
                    }
                    if (into && hop == TBT_HOP_PROTOCOL && s->Adp[a].CapAdp != 0 &&
                        (s->Adp[a].Type == TBT_ADP_TYPE_PCIE_DOWN ||
                         s->Adp[a].Type == TBT_ADP_TYPE_USB3_DOWN)) {
                        (void)tbtU4Disable(cm, l->Port, &s->Route, a, s->Adp[a].CapAdp,
                                           (BOOLEAN)(s->Adp[a].Type == TBT_ADP_TYPE_USB3_DOWN));
                    }
                    (void)tbtU4Invalidate(cm, l->Port, &s->Route, a, hop);
                }
            }
        }
    }
    for (k = 0; k < TBT_U4_MAX_LINKS; k++) {
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
    if (p != NULL) {
        tbtU4Queue(cm, TBT_U4_ACT_DEEP_REMOVED, p);
    }
}

/* Each tick: every link's parent lane, its Lock and the child's identity. */
static VOID tbtU4CheckLinks(PTBT_U4CM cm)
{
    ULONG i;
    ULONG lane;
    ULONG cs4;
    ULONG v[2];
    LONG rc;
    BOOLEAN gone;
    PTBT_U4_LINK l;

    for (i = 0; i < TBT_U4_MAX_LINKS; i++) {
        l = &cm->Links[i];
        if (!l->Used) {
            continue;
        }
        gone = FALSE;
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
            rc = tbtU4RdN(cm, &l->Child, TBT_CS_ROUTER, 0, TBT_ROUTER_CS_7, 2, v);
            if (tbtU4Gone(cm, rc) ||
                (rc == TBT_OK && (v[0] != l->ChildUuid[0] || v[1] != l->ChildUuid[1]))) {
                gone = TRUE;
            }
        }
        if (gone) {
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

/* Every Valid or pending entry of one host adapter, HopID 8 to its Max
 * Input HopID, deactivated (Linux switch.c tb_switch_reset_host). */
static VOID tbtU4CleanAdapter(PTBT_U4CM cm, PTBT_U4_PORT p, ULONG adp)
{
    PTBT_U4_ROUTER h;
    ULONG hop;
    ULONG e[2];

    h = &cm->HostRouter;
    if (adp == 0 || !h->Adp[adp].Present) {
        return;
    }
    for (hop = TBT_HOP_FIRST; hop <= h->Adp[adp].MaxInHop; hop++) {
        if (tbtU4Entry(cm, &h->Route, adp, hop, e) == TBT_OK &&
            ((e[0] & TBT_PATH0_VALID) != 0 || (e[1] & TBT_PATH1_PP) != 0)) {
            (void)tbtU4Invalidate(cm, p->Index, &h->Route, adp, hop);
        }
    }
}

/*
 * Cleaning one port (XhciCmPreOsCleanup 1 or 2), the Linux and Windows 11
 * sequence restricted to it: the orderly teardown of both tunnels with
 * both ends reachable; the statement and the tunnel ports' removal; a
 * Downstream Port Reset on the host's port (PORT_CS_19 DPR held 10 ms,
 * Guide 7.3; Spec 6.9; Linux usb4.c usb4_port_reset); the port's host-side
 * entries deactivated; the port EMPTY, to be connected afresh once its
 * link retrains.
 */
static VOID tbtU4Clean(PTBT_U4CM cm, PTBT_U4_PORT p, BOOLEAN portReset)
{
    PTBT_U4_ROUTER h;
    BOOLEAN closed;
    BOOLEAN hadPcie;
    TBT_WAIT w;
    ULONG lane;

    h = &cm->HostRouter;
    cm->Counters.PortsCleaned++;
    tbtU4Log(cm, "u4.clean", p->Index);
    hadPcie = (BOOLEAN)(p->Pcie.Written != 0);
    closed = TRUE;
    if (p->Pcie.Written != 0) {
        p->Pcie.Written |= TBT_U4_E_HOST_ENABLE | TBT_U4_E_DEV_ENABLE;
        closed = tbtU4TunnelDown(cm, p, &p->Pcie, FALSE);
    }
    if (p->Usb3.Written != 0) {
        p->Usb3.Written |= TBT_U4_E_HOST_ENABLE | TBT_U4_E_DEV_ENABLE;
        (void)tbtU4TunnelDown(cm, p, &p->Usb3, TRUE);
        p->LastUsb3TeardownUs = tbtU4Now(cm);
        p->Usb3GapValid = TRUE;
    }
    if (hadPcie) {
        tbtU4LinkDownUpcall(cm, p, closed, FALSE);
        tbtU4Queue(cm, TBT_U4_ACT_TUNNEL_DOWN, p);
    }
    if (!portReset) {
        /* Steps 1 and 2 only: the Host Router Reset that follows does the
         * rest for the whole domain. */
        return;
    }
    if (h->Adp[p->Lane0].CapPort != 0 &&
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
}

/* ---------------------------------------------------------------------- */
/* Port checks and the idle tick (record 22 sections 7.1, 7.2)            */
/* ---------------------------------------------------------------------- */

/* Section 7.1 item 4: the PCIe tunnel lost with the router still there -
 * Path Enable read clear or LTSSM Detect, or a transitional LTSSM for 2 s.
 * Only that tunnel is torn down, in the orderly form; the port is FAILED
 * for PCIe until a re-plug, its USB3 tunnel kept. */
static VOID tbtU4PcieLoss(PTBT_U4CM cm, PTBT_U4_PORT p)
{
    PTBT_U4_ROUTER h;
    ULONG v;
    ULONG s;
    BOOLEAN lost;
    BOOLEAN closed;

    h = &cm->HostRouter;
    if (!p->Pcie.Up || p->PcieDown == 0 ||
        tbtU4Rd(cm, &h->Route, TBT_CS_ADAPTER, p->PcieDown,
                h->Adp[p->PcieDown].CapAdp + TBT_PCIE_CS_0, &v) != TBT_OK) {
        return;
    }
    s = TBT_PCIE0_LTSSM(v);
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
    if (!lost) {
        return;
    }
    cm->Counters.PcieLost++;
    tbtU4Log(cm, "u4.pcie.lost", (p->Index << 8) | s);
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
        if (cls == TBT_U4_LANE_CONNECTED || cls == TBT_U4_LANE_TRANSITIONAL) {
            tbtU4Connect(cm, p, FALSE);
        }
        break;
    case TBT_U4_UNSUPPORTED:
        gone = tbtU4Evidence(cm, p, &cls);
        if (gone || cls == TBT_U4_LANE_DISCONNECTED) {
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
            if (cls == TBT_U4_LANE_CONNECTED || cls == TBT_U4_LANE_TRANSITIONAL) {
                tbtU4Connect(cm, p, FALSE);
            }
            break;
        }
        p->UnplugPending = FALSE;
        if (p->State == TBT_U4_UP) {
            tbtU4PcieLoss(cm, p);
        }
        break;
    default:
        break;
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
}

VOID TbtU4Init(PTBT_U4CM cm, PTBT_RING ring, const TBT_HOST *host,
               const TBT_U4_CONFIG *config)
{
    tbtU4Zero(cm, sizeof(*cm));
    cm->Ring = ring;
    cm->Host = host;
    if (config != NULL) {
        cm->Config = *config;
    } else {
        TbtU4DefaultConfig(&cm->Config);
    }
    if (cm->Config.PreOsCleanup > 2) {
        cm->Config.PreOsCleanup = 0;
    }
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
    TbtRingStop(cm->Ring);
    cm->Failed = TRUE;
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
 * (section 6.1; Guide 3.1.1; Linux usb4.c usb4_port_hotplug_enable). */
static VOID tbtU4HotPlugEnable(PTBT_U4CM cm)
{
    ULONG i;
    PTBT_U4_PORT p;

    for (i = 0; i < cm->PortCount; i++) {
        p = &cm->Ports[i];
        if (p->Pcie.Preserved || p->Usb3.Preserved || p->State == TBT_U4_HELD) {
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
 * left untouched 50 ms, cleared by the host within 500 ms (Linux nhi.c
 * nhi_reset), then the rings again and the host router enumerated afresh.
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
    TbtU4Poll(cm);
    return TBT_OK;
}

VOID TbtU4Poll(PTBT_U4CM cm)
{
    ULONG i;
    PTBT_U4_PORT p;

    if (cm->Failed || cm->Stage != TBT_U4_STAGE_RUNNING || cm->Suspended) {
        return;
    }
    TbtCtlService(&cm->Ctl);
    tbtU4TakeQueues(cm);
    /* Steps 4 to 6 owed by teardowns nested at a safe point. */
    for (i = 0; i < cm->PortCount; i++) {
        if (cm->DeferredTail[i]) {
            cm->DeferredTail[i] = 0;
            tbtU4DropLinks(cm, &cm->Ports[i]);
            tbtU4TeardownTail(cm, &cm->Ports[i]);
        }
    }
    tbtU4RetryQuarantine(cm);
    for (i = 0; i < cm->PortCount; i++) {
        p = &cm->Ports[i];
        if (p->TmuQuarantine && p->State != TBT_U4_EMPTY && p->Dev.Present &&
            p->TmuConfigured) {
            if (tbtU4TmuDisable(cm, p) == TBT_OK) {
                p->TmuQuarantine = FALSE;
            }
        } else if (p->TmuQuarantine && !p->Dev.Present) {
            if (tbtU4TmuAdp(cm, &cm->HostRouter, p->Lane0, TBT_TMU_ADP_CS_6, 0,
                            TBT_TMU_ADP6_DTS) == TBT_OK) {
                p->TmuQuarantine = FALSE;
            }
        }
        if (p->BwQuarantine && p->Usb3Down != 0 && tbtU4Cmr(cm, p, FALSE) == TBT_OK) {
            p->BwQuarantine = FALSE;
        }
        tbtU4Check(cm, p);
    }
    tbtU4CheckLinks(cm);
    if (cm->ResyncPending && cm->ActionCount == 0) {
        cm->ResyncPending = FALSE;
        tbtU4Queue(cm, TBT_U4_ACT_RESYNC, NULL);
    }
}

BOOLEAN TbtU4NextAction(PTBT_U4CM cm, PTBT_U4_ACTION out)
{
    if (cm->ActionCount == 0) {
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
        p->State == TBT_U4_EMPTY) {
        return;
    }
    if (!p->AnchorValid || p->AnchorGeneration != generation ||
        (LONG)(issuedUs - p->AnchorUs) > 0) {
        p->AnchorValid = TRUE;
        p->AnchorUs = issuedUs;
        p->AnchorGeneration = generation;
    }
}

LONG TbtU4Eject(PTBT_U4CM cm, ULONG port)
{
    PTBT_U4_PORT p;
    BOOLEAN closed;

    p = tbtU4PortOf(cm, port);
    if (p == NULL || cm->Failed || !p->Dev.Present) {
        return TBT_E_PARAM;
    }
    cm->Counters.Ejects++;
    closed = TRUE;
    if (p->Usb3.Up || p->Usb3.Written != 0) {
        p->Usb3.Written |= TBT_U4_E_HOST_ENABLE | TBT_U4_E_DEV_ENABLE;
        closed = tbtU4TunnelDown(cm, p, &p->Usb3, TRUE);
        p->LastUsb3TeardownUs = tbtU4Now(cm);
        p->Usb3GapValid = TRUE;
        if ((p->Usb3.Written & TBT_U4_E_BANDWIDTH) != 0) {
            (void)tbtU4ReleaseBw(cm, p);
        }
    }
    if (p->Pcie.Up || p->Pcie.Written != 0) {
        p->Pcie.Written |= TBT_U4_E_HOST_ENABLE | TBT_U4_E_DEV_ENABLE;
        closed &= tbtU4TunnelDown(cm, p, &p->Pcie, FALSE);
        cm->Counters.PcieTornDown++;
        tbtU4Queue(cm, TBT_U4_ACT_TUNNEL_DOWN, p);
    }
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
            o->BuiltHere = (BOOLEAN)!p->Pcie.Preserved;
            o->Hd = p->Pcie.Hd;
            o->Hu = p->Pcie.Hu;
        }
        if (p->Usb3.Up && k < TBT_U4_OBLIGATIONS) {
            o = &cm->Obligations[k++];
            o->Used = TRUE;
            o->Port = p->Index;
            o->Generation = p->Generation;
            o->Usb3 = TRUE;
            o->BuiltHere = (BOOLEAN)!p->Usb3.Preserved;
            o->Hd = p->Usb3.Hd;
            o->Hu = p->Usb3.Hu;
        }
    }
    TbtRingStop(cm->Ring);
    cm->Suspended = TRUE;
}

/*
 * Resume (sections 7.6, 11): the rings and the host router again; then
 * every obligation - verified by section 9.2's reads; rebuilt where the
 * domain lost its configuration and the port was built here; torn down on
 * removal evidence; else HELD and the failure path.
 */
LONG TbtU4Resume(PTBT_U4CM cm)
{
    ULONG i;
    ULONG k;
    ULONG cls;
    ULONG v;
    BOOLEAN met;
    BOOLEAN allMet;
    TBT_U4_TUNNEL t;
    PTBT_U4_OBLIGATION o;
    PTBT_U4_PORT p;
    TBT_U4_PORT saved[TBT_U4_MAX_PORTS];
    LONG rc;

    if (cm->Failed) {
        return TBT_E_STATE;
    }
    cm->Suspended = FALSE;
    for (i = 0; i < cm->PortCount; i++) {
        saved[i] = cm->Ports[i];
    }
    rc = tbtU4Rings(cm);
    if (rc != TBT_OK) {
        cm->Failed = TRUE;
        return rc;
    }
    rc = tbtU4HostRouter(cm);
    if (rc != TBT_OK) {
        return tbtU4Fail(cm, rc);
    }
    /* The port table is rebuilt by the host router's walk; what each port
     * held across the sleep comes back from the saved copy. */
    for (i = 0; i < cm->PortCount; i++) {
        p = &cm->Ports[i];
        for (k = 0; k < TBT_U4_MAX_PORTS; k++) {
            if (saved[k].Lane0 == p->Lane0 && saved[k].Index != 0) {
                saved[k].MapValid = p->MapValid;
                saved[k].MapRid = p->MapRid;
                *p = saved[k];
            }
        }
    }
    cm->Stage = TBT_U4_STAGE_RUNNING;
    tbtU4HostTmu(cm);
    allMet = TRUE;
    for (k = 0; k < TBT_U4_OBLIGATIONS; k++) {
        o = &cm->Obligations[k];
        if (!o->Used) {
            continue;
        }
        p = tbtU4PortOf(cm, o->Port);
        met = FALSE;
        if (p == NULL) {
            allMet = FALSE;
            continue;
        }
        if (p->Generation != o->Generation || p->State == TBT_U4_EMPTY) {
            met = TRUE;     /* already torn down: discharged */
        } else if (tbtU4Evidence(cm, p, &cls)) {
            tbtU4DropLinks(cm, p);
            tbtU4Teardown(cm, p);
            met = TRUE;
        } else if (p->Dev.Present &&
                   tbtU4Rd(cm, &p->Dev.Route, TBT_CS_ROUTER, 0, TBT_ROUTER_CS_3, &v) == TBT_OK &&
                   (v & TBT_RTR3_V) == 0) {
            /* The domain lost its configuration across the sleep. */
            if (o->BuiltHere) {
                tbtU4Zero(&p->Pcie, sizeof(p->Pcie));
                tbtU4Zero(&p->Usb3, sizeof(p->Usb3));
                p->Bonded = FALSE;
                p->SingleLane = FALSE;
                p->LaneWritten = FALSE;
                p->Lane1Disabled = FALSE;
                p->TmuConfigured = FALSE;
                p->State = TBT_U4_EMPTY;
                tbtU4Connect(cm, p, FALSE);
                met = (BOOLEAN)(o->Usb3 ? p->Usb3.Up : p->Pcie.Up);
            }
        } else if (p->Dev.Present) {
            met = tbtU4Follow(cm, p, o->Usb3, &t);
            if (met && !o->Usb3) {
                met = tbtU4PcieLive(cm, p);
            }
        }
        if (!met) {
            allMet = FALSE;
            if (p->State != TBT_U4_EMPTY) {
                p->State = TBT_U4_HELD;
                p->Why = TBT_U4_WHY_RESUME;
                cm->Counters.PortsHeld++;
            }
        }
        o->Used = FALSE;
    }
    tbtU4HotPlugEnable(cm);
    cm->ResumeHeld = (BOOLEAN)!allMet;
    for (i = 0; i < cm->PortCount; i++) {
        cm->Ports[i].CheckPending = TRUE;
    }
    return allMet ? TBT_OK : TBT_E_STATE;
}

/*
 * Section 7.6's bound: each step's implemented cost, a maximum counted at
 * its bound plus a tick, a minimum at its minimum plus two ticks (the tick
 * it waits more and the tick it may overshoot), a request attempt at
 * 100 ms plus a tick times its 4 attempts; platform preparation (1.05 s)
 * once, then per obligation a rebuild, the costlier of the two ways an
 * obligation is met. A rebuild's waits are the bounds of section 5.2 it
 * runs into, each once (enumeration's 20 tries with their gaps); its
 * requests are a project budget of TBT_U4_RESUME_REQUESTS, more than a
 * rebuild against either emulator host model issues. In microseconds:
 * about 66 s per obligation at a 55 ms tick, so eight stay far below the
 * 32-bit limit.
 */
#define TBT_U4_RESUME_REQUESTS      150UL

ULONG TbtU4ResumeBoundUs(ULONG obligations, ULONG tickUs)
{
    ULONG perMax;
    ULONG perMin;
    ULONG rebuild;
    ULONG total;

    perMax = tickUs;
    perMin = 2UL * tickUs;
    rebuild = TBT_U4_ENUM_TRIES * (TBT_U4_ENUM_GAP_US + perMin) +
              (TBT_U4_ROUTER_READY_US + perMax) +
              4UL * (TBT_U4_ROP_US + perMax) +
              2UL * (TBT_U4_LANE1_CL0_US + perMax) + (TBT_U4_BONDING_US + perMax) +
              (TBT_U4_TMU_POST_US + perMax) + (TBT_U4_TMU_SETTLE_US + perMin) +
              (TBT_U4_CONFIG_READY_US + perMax) +
              2UL * (TBT_U4_DETECT_US + perMax) + (TBT_U4_L0_US + perMax) +
              2UL * (TBT_U4_HCA_US + perMax) + (TBT_U4_USB3_GAP_US + perMin) +
              TBT_U4_RESUME_REQUESTS * TBT_CTL_ATTEMPTS * (TBT_CTL_TIMEOUT_US + perMax);
    total = TBT_ICL_FW_READY_POLLS * TBT_ICL_FW_READY_POLL_MS * 1000UL + perMax;
    if (obligations > TBT_U4_OBLIGATIONS) {
        obligations = TBT_U4_OBLIGATIONS;
    }
    return total + obligations * rebuild;
}

VOID TbtU4Stop(PTBT_U4CM cm)
{
    TbtRingStop(cm->Ring);
    cm->Stage = TBT_U4_STAGE_NONE;
}

BOOLEAN TbtU4AnyRouterConnected(const TBT_U4CM *cm)
{
    ULONG i;

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
