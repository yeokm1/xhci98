/*
 * u4cm-glue.c - the C89 side of the usb4-emu cross-check (roadmap-usb4.md
 * task 40-A.5): xhci98's TbtU4 core behind a plain-C interface of
 * `unsigned long` and `void *`, so the gnu11 harness that links usb4-emu's
 * fabric model (u4cm-crosscheck.c) never sees this project's DDK-style
 * types, which clash with the Windows headers QEMU's osdep pulls in on
 * MinGW. Compiled with the core, strictly C89.
 *
 * Written for xhci98, 2026-10-15. SPDX-License-Identifier: GPL-2.0-only
 */

#include "../../src/cm/tbt_u4cm.h"
#include "u4cm-glue.h"

static const XC_OPS *xcOps;
static TBT_RING xcRing;
static TBT_U4CM xcCm;
static unsigned long xcLdCalls;
static int xcLdClosed;

static ULONG xcRd(PVOID ctx, ULONG off)
{
    (void)ctx;
    return xcOps->rd(off);
}

static VOID xcWr(PVOID ctx, ULONG off, ULONG v)
{
    (void)ctx;
    xcOps->wr(off, v);
}

static VOID xcStall(PVOID ctx, ULONG us)
{
    (void)ctx;
    xcOps->advance_us(us);
}

static VOID xcSleep(PVOID ctx, ULONG ms)
{
    (void)ctx;
    xcOps->advance_us(ms * 1000UL);
}

static ULONG xcNowMs(PVOID ctx)
{
    (void)ctx;
    return xcOps->clock_us() / 1000UL;
}

static ULONG xcClockUs(PVOID ctx)
{
    (void)ctx;
    return xcOps->clock_us();
}

static VOID xcLog(PVOID ctx, const char *label, ULONG value)
{
    (void)ctx;
    xcOps->log(label, value);
}

static const TBT_HOST xcHost = {
    NULL, xcRd, xcWr, xcStall, xcSleep, xcNowMs, xcLog, xcClockUs, 0, NULL
};

static ULONG xcLinkDown(PVOID ctx, const TBT_U4_LINKDOWN *info)
{
    (void)ctx;
    xcLdCalls++;
    xcLdClosed = info->PathClosed ? 1 : 0;
    return TBT_U4_STMT_WITHHELD;
}

long xc_start(const XC_OPS *ops, void *arena, unsigned long phys,
              unsigned long bytes, int usb3)
{
    return xc_start_clean(ops, arena, phys, bytes, usb3, 0);
}

long xc_start_clean(const XC_OPS *ops, void *arena, unsigned long phys,
                    unsigned long bytes, int usb3, unsigned long preOsCleanup)
{
    TBT_U4_CONFIG c;
    UCHAR *p;
    ULONG i;

    xcOps = ops;
    TbtU4DefaultConfig(&c);
    c.Usb3Tunnel = (BOOLEAN)(usb3 != 0);
    c.PreOsCleanup = preOsCleanup;
    c.RingFlags = TBT_RING_START_MASK_IMC;
    p = (UCHAR *)&xcRing;
    for (i = 0; i < sizeof(xcRing); i++) {
        p[i] = 0;
    }
    TbtU4Init(&xcCm, &xcRing, &xcHost, &c);
    TbtU4SetLinkDown(&xcCm, xcLinkDown, NULL);
    xcLdCalls = 0;
    xcLdClosed = 0;
    return TbtU4Start(&xcCm, arena, phys, bytes);
}

void xc_poll(void)
{
    TbtU4Poll(&xcCm);
}

void xc_stop(void)
{
    TbtU4Stop(&xcCm);
}

unsigned long xc_get(int what, unsigned long port)
{
    const TBT_U4_PORT *p;

    p = TbtU4Port(&xcCm, port);
    switch (what) {
    case XC_STATE: return p != NULL ? p->State : 0xFFUL;
    case XC_WHY: return p != NULL ? p->Why : 0xFFUL;
    case XC_BONDED: return p != NULL ? p->Bonded : 0;
    case XC_PCIE_UP: return p != NULL ? p->Pcie.Up : 0;
    case XC_PCIE_PRESERVED: return p != NULL ? p->Pcie.Preserved : 0;
    case XC_USB3_UP: return p != NULL ? p->Usb3.Up : 0;
    case XC_TMU_OK: return p != NULL ? p->TmuOk : 0;
    case XC_PCIE_DOWN: return p != NULL ? p->PcieDown : 0;
    case XC_HOST_QUIRKS: return xcCm.HostRouter.Quirks;
    case XC_INERT: return xcCm.InertReason;
    case XC_REQUESTS: return xcCm.Ctl.Counters.Requests;
    case XC_RETRIES: return xcCm.Ctl.Counters.Retries;
    case XC_TIMEOUTS: return xcCm.Ctl.Counters.Timeouts;
    case XC_LD_CALLS: return xcLdCalls;
    case XC_LD_CLOSED: return (unsigned long)xcLdClosed;
    case XC_HOTPLUGS: return xcCm.Ctl.Counters.HotPlugs + xcCm.Ctl.Counters.HotUnplugs;
    case XC_ACKS: return xcCm.Ctl.Counters.AcksPlain + xcCm.Ctl.Counters.AcksEnhanced;
    case XC_HOST_ADP_PRESENT:
        return port < TBT_U4_MAX_ADP ? xcCm.HostRouter.Adp[port].Present : 0;
    case XC_DEV_ADP_PRESENT:
        p = TbtU4Port(&xcCm, port >> 8);
        return p != NULL && (port & 0xFFUL) < TBT_U4_MAX_ADP ?
               p->Dev.Adp[port & 0xFFUL].Present : 0;
    case XC_ROP_ISSUED: return xcCm.Counters.RopIssued;
    case XC_ROP_FAILED: return xcCm.Counters.RopFailed;
    case XC_ROP_TIMEOUTS: return xcCm.Counters.RopTimeouts;
    case XC_DPR: return xcCm.Counters.DownstreamPortResets;
    case XC_CR_TIMEOUTS: return xcCm.Counters.ConfigReadyTimeouts;
    case XC_USB3_BUILT: return xcCm.Counters.Usb3Built;
    case XC_USB3_FAILED: return xcCm.Counters.Usb3Failed;
    case XC_PCIE_BUILT: return xcCm.Counters.PcieBuilt;
    case XC_CONNECTS: return xcCm.Counters.Connects;
    case XC_DROM_CRC: return xcCm.Counters.DromCrcFailures;
    case XC_TMU_ENABLED: return xcCm.Counters.TmuEnabled;
    case XC_ENUMERATED: return xcCm.Counters.RoutersEnumerated;
    }
    return 0;
}

const char *xc_vendor_name(unsigned long port)
{
    const TBT_U4_PORT *p;

    p = TbtU4Port(&xcCm, port);
    return p != NULL ? p->Dev.VendorName : "";
}

const char *xc_model_name(unsigned long port)
{
    const TBT_U4_PORT *p;

    p = TbtU4Port(&xcCm, port);
    return p != NULL ? p->Dev.ModelName : "";
}

int xc_state_up(void)
{
    return (int)TBT_U4_UP;
}

int xc_state_empty(void)
{
    return (int)TBT_U4_EMPTY;
}

unsigned long xc_quirk_usb3(void)
{
    return TBT_U4_QUIRK_USB3_MAX_BW;
}
