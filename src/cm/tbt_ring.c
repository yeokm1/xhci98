/*
 * tbt_ring.c - the host interface's ring 0 and the control transport (see
 * tbt_ring.h).
 *
 * Lifted from usb4-emu's tools/usb4cm/core/u4hi.c, whose notice follows.
 * What changed: the accessor is TBT_HOST and the common buffer is the
 * caller's; every wait is a clock deadline with a backstop (TbtDeadline*,
 * TbtWait*), not a count of polls; the TX ring refuses to fill its last
 * vacant descriptor; a receive buffer's Buffer Overflow bit and a length
 * past the buffer are reported rather than read past; the trailer of an ICM
 * frame is flagged, not dropped; the interrupt mask follows each host's
 * contract; the Host Interface Reset is optional, because the Alpine Ridge
 * client does not reset (ICM-PROTOCOL.md 6.1) and the USB4 profile never
 * does (design record 22 section 4.4), which takes ring 0 over instead.
 * For the USB4 software connection manager (roadmap-usb4.md task 40-A.1,
 * 2026-10-15): the geometry is a start parameter; write requests share the
 * read's transfer; a notification fails a request only when its code is a
 * request error, the others being events (record 22 section 5.1, which
 * departs from u4hi.c's "any code below 32"); and TBT_CTL is u4hi.c's
 * hot-plug acknowledgement and queue (u4_hp_ack, u4cm_unsolicited) grown
 * into record 22's transport - plain and enhanced acknowledgements by the
 * connection manager's per-router answer, acknowledgements held until that
 * answer exists, the microsecond clock, the safe point. Spec citations are
 * to the USB4 Specification 2.0 (November 2025), sections 6.4, 12.3, 12.4
 * and 12.6.
 *
 * ------------------------------------------------------------------------
 * Original notice of u4hi.c:
 *
 *   u4hi.c - usb4cm core: host interface ring 0 and the control packet
 *   transport, by polling.
 *
 *   Copyright (c) 2026 usb4-emu contributors
 *   SPDX-License-Identifier: MIT
 *
 *   Written from scratch for the usb4-emu project from the USB4
 *   Specification 2.0 sections 6.4 (control packets), 12.3 (transmit),
 *   12.4 (receive) and 12.6 (host interface registers). C89.
 *
 * The MIT permission notice that covers it is reproduced in full in
 * tbt_regs.h.
 * ------------------------------------------------------------------------
 *
 * IRQL: see tbt_ring.h - PASSIVE_LEVEL on the connection-manager thread for
 * everything that takes a ring, a transport or a host; TbtCrc32c,
 * TbtCtlClassify and the TbtRoute helpers at any IRQL. No locks.
 */

#include "tbt_ring.h"

/* ---------------------------------------------------------------------- */
/* Byte order                                                             */
/* ---------------------------------------------------------------------- */

/* Descriptors are little-endian DWs (Spec 12.1.1). */
static VOID tbtStoreLe32(volatile UCHAR *p, ULONG v)
{
    p[0] = (UCHAR)v;
    p[1] = (UCHAR)(v >> 8);
    p[2] = (UCHAR)(v >> 16);
    p[3] = (UCHAR)(v >> 24);
}

static ULONG tbtLoadLe32(const volatile UCHAR *p)
{
    return (ULONG)p[0] | ((ULONG)p[1] << 8) | ((ULONG)p[2] << 16) |
           ((ULONG)p[3] << 24);
}

/* A data buffer holds its lowest-addressed byte in bits 31:24 of each DW
 * (Spec 12.1.1), so packet DWs are stored most significant byte first. */
static VOID tbtStoreBe32(volatile UCHAR *p, ULONG v)
{
    p[0] = (UCHAR)(v >> 24);
    p[1] = (UCHAR)(v >> 16);
    p[2] = (UCHAR)(v >> 8);
    p[3] = (UCHAR)v;
}

static ULONG tbtLoadBe32(const volatile UCHAR *p)
{
    return ((ULONG)p[0] << 24) | ((ULONG)p[1] << 16) | ((ULONG)p[2] << 8) |
           (ULONG)p[3];
}

#define TBT_RD(ring, off) \
    ((ring)->Host->Read32((ring)->Host->Context, (off)))
#define TBT_WR(ring, off, v) \
    ((ring)->Host->Write32((ring)->Host->Context, (off), (v)))

static VOID tbtLog(const TBT_HOST *host, const char *label, ULONG value)
{
    if (host != NULL && host->Log != NULL) {
        host->Log(host->Context, label, value);
    }
}

/* ---------------------------------------------------------------------- */
/* CRC-32C, Spec 6.4.2.2 Table 6-1                                        */
/* ---------------------------------------------------------------------- */

/*
 * Reflected, so bit by bit from the least significant end of each byte,
 * with the bytes in buffer order - which is DW order, bit 31 first, since
 * the buffer holds each DW most significant byte first. Bitwise rather than
 * a 1 KB table in non-paged data; the longest frame is 252 bytes.
 */
ULONG TbtCrc32c(const UCHAR *bytes, ULONG length)
{
    ULONG crc;
    ULONG i;
    ULONG k;

    crc = 0xFFFFFFFFUL;
    for (i = 0; i < length; i++) {
        crc ^= bytes[i];
        for (k = 0; k < 8; k++) {
            crc = (crc >> 1) ^ (TBT_CRC32C_POLY_REFLECTED & (0UL - (crc & 1UL)));
        }
    }
    return crc ^ 0xFFFFFFFFUL;
}

/* ---------------------------------------------------------------------- */
/* Deadlines and waits                                                    */
/* ---------------------------------------------------------------------- */

VOID TbtDeadlineStart(PTBT_DEADLINE d, const TBT_HOST *host, ULONG limitMs)
{
    d->StartMs = host->NowMs(host->Context);
    d->LimitMs = limitMs;
    d->WaitedUs = 0;
}

/*
 * The clock ends a wait on time; the sum of the waits asked for ends it
 * even if the clock never moves, and never early, because every wait takes
 * at least what it asked for. LimitMs is held to 60 s by every caller, so
 * the microsecond sum cannot wrap.
 */
BOOLEAN TbtDeadlinePassed(const TBT_DEADLINE *d, const TBT_HOST *host)
{
    ULONG elapsed;

    if (host->Cancelled != NULL && host->Cancelled(host->Context)) {
        return TRUE;
    }
    elapsed = host->NowMs(host->Context) - d->StartMs;
    if (elapsed >= d->LimitMs) {
        return TRUE;
    }
    return (BOOLEAN)(d->WaitedUs / 1000UL >= d->LimitMs);
}

VOID TbtDeadlineWait(PTBT_DEADLINE d, const TBT_HOST *host)
{
    if (d->WaitedUs < TBT_WAIT_BUSY_US) {
        host->Stall(host->Context, TBT_WAIT_STALL_US);
        d->WaitedUs += TBT_WAIT_STALL_US;
    } else {
        host->Sleep(host->Context, TBT_WAIT_SLEEP_MS);
        d->WaitedUs += TBT_WAIT_SLEEP_MS * 1000UL;
    }
}

ULONG TbtClockUs(const TBT_HOST *host)
{
    if (host->ClockUs != NULL) {
        return host->ClockUs(host->Context);
    }
    return host->NowMs(host->Context) * 1000UL;
}

VOID TbtWaitStart(PTBT_WAIT w, const TBT_HOST *host, ULONG limitUs,
                  BOOLEAN minimum)
{
    w->StartUs = TbtClockUs(host);
    w->LimitUs = limitUs;
    w->WaitedUs = 0;
    w->Minimum = minimum;
}

ULONG TbtWaitElapsedUs(const TBT_WAIT *w, const TBT_HOST *host)
{
    return TbtClockUs(host) - w->StartUs;
}

/*
 * Every bound of record 22 section 5.2 is at most a few seconds, so the
 * limit plus a tick cannot wrap, and the waited sum, which stops growing
 * once it passes the limit, cannot either.
 */
BOOLEAN TbtWaitDone(const TBT_WAIT *w, const TBT_HOST *host)
{
    ULONG elapsed;
    ULONG need;

    if (w->WaitedUs >= w->LimitUs) {
        return TRUE;
    }
    if (host->Cancelled != NULL && host->Cancelled(host->Context)) {
        return TRUE;
    }
    elapsed = TbtClockUs(host) - w->StartUs;
    need = w->LimitUs;
    if (w->Minimum) {
        need += host->TickUs;
    }
    return (BOOLEAN)(elapsed >= need);
}

VOID TbtWaitStep(PTBT_WAIT w, const TBT_HOST *host, ULONG stepUs)
{
    ULONG ms;

    if (stepUs == 0) {
        stepUs = w->WaitedUs < TBT_WAIT_BUSY_US ? TBT_WAIT_STALL_US :
                 TBT_WAIT_SLEEP_MS * 1000UL;
    }
    if (stepUs < 1000UL) {
        host->Stall(host->Context, stepUs);
    } else {
        ms = stepUs / 1000UL;
        host->Sleep(host->Context, ms);
        stepUs = ms * 1000UL;
        if (!w->Minimum) {
            /* A sleep may last to the next tick (record 22 section 5.3):
             * a maximum's waited sum counts that overshoot, so that on a
             * clock that does not move it still ends within its bound. A
             * minimum's does not: it must never end early. */
            stepUs += host->TickUs;
        }
    }
    if (w->WaitedUs < w->LimitUs) {
        w->WaitedUs += stepUs;
    }
}

VOID TbtWaitMin(const TBT_HOST *host, ULONG minimumUs)
{
    TBT_WAIT w;

    TbtWaitStart(&w, host, minimumUs, TRUE);
    while (!TbtWaitDone(&w, host)) {
        TbtWaitStep(&w, host, minimumUs < 1000UL ? minimumUs : 1000UL);
    }
}

/* ---------------------------------------------------------------------- */
/* Route strings, Spec 6.4.2.2 Figure 6-5                                 */
/* ---------------------------------------------------------------------- */

ULONG TbtRouteLevel(const TBT_ROUTE *route, ULONG level)
{
    ULONG b;

    if (level < 4) {
        b = route->Lo >> (8 * level);
    } else if (level < TBT_ROUTE_LEVELS) {
        b = route->Hi >> (8 * (level - 4));
    } else {
        return 0;
    }
    return b & TBT_ROUTE_LEVEL_ADAPTER;
}

ULONG TbtRouteDepth(const TBT_ROUTE *route)
{
    ULONG depth;

    for (depth = 0; depth < TBT_ROUTE_LEVELS; depth++) {
        if (TbtRouteLevel(route, depth) == 0) {
            break;
        }
    }
    return depth;
}

BOOLEAN TbtRouteEqual(const TBT_ROUTE *a, const TBT_ROUTE *b)
{
    return (BOOLEAN)(((a->Hi ^ b->Hi) & TBT_ROUTE_HI_TOPOLOGY) == 0 &&
                     a->Lo == b->Lo);
}

BOOLEAN TbtRouteChild(const TBT_ROUTE *parent, ULONG adapter, PTBT_ROUTE child)
{
    ULONG level;

    level = TbtRouteDepth(parent);
    if (level >= TBT_ROUTE_LEVELS || adapter == 0 ||
        adapter > TBT_ROUTE_LEVEL_ADAPTER) {
        return FALSE;
    }
    child->Hi = parent->Hi & TBT_ROUTE_HI_TOPOLOGY;
    child->Lo = parent->Lo;
    if (level < 4) {
        child->Lo |= adapter << (8 * level);
    } else {
        child->Hi |= adapter << (8 * (level - 4));
    }
    return TRUE;
}

/* `route` is `top` or one of the routes below it. */
static BOOLEAN tbtRouteUnder(const TBT_ROUTE *route, const TBT_ROUTE *top)
{
    ULONG depth;
    ULONG i;

    depth = TbtRouteDepth(top);
    if (TbtRouteDepth(route) < depth) {
        return FALSE;
    }
    for (i = 0; i < depth; i++) {
        if (TbtRouteLevel(route, i) != TbtRouteLevel(top, i)) {
            return FALSE;
        }
    }
    return TRUE;
}

/* ---------------------------------------------------------------------- */
/* Start and stop, Spec 12.6                                              */
/* ---------------------------------------------------------------------- */

static ULONG tbtRxDescOff(const TBT_RING *ring)
{
    return TBT_DESC_BYTES * ring->Entries;
}

static ULONG tbtTxBufOff(const TBT_RING *ring)
{
    return 2UL * TBT_DESC_BYTES * ring->Entries;
}

static ULONG tbtRxBufOff(const TBT_RING *ring)
{
    return 2UL * TBT_DESC_BYTES * ring->Entries + TBT_RING_BUF_BYTES * ring->Entries;
}

static volatile UCHAR *tbtTxDesc(PTBT_RING ring, ULONG i)
{
    return ring->Va + TBT_DESC_BYTES * i;
}

static volatile UCHAR *tbtRxDesc(PTBT_RING ring, ULONG i)
{
    return ring->Va + tbtRxDescOff(ring) + TBT_DESC_BYTES * i;
}

/* Spec 12.4.1 Table 12-7: the buffer's address, RS set so the status is
 * written back, DD clear. */
static VOID tbtRxPost(PTBT_RING ring, ULONG i)
{
    volatile UCHAR *d;

    d = tbtRxDesc(ring, i);
    tbtStoreLe32(d + TBT_DESC_ADDR_LO,
                 ring->Phys + tbtRxBufOff(ring) + TBT_RING_BUF_BYTES * i);
    tbtStoreLe32(d + TBT_DESC_ADDR_HI, 0);
    tbtStoreLe32(d + TBT_DESC_DW2, TBT_DESC_RS);
    tbtStoreLe32(d + TBT_DESC_DW3, 0);
}

VOID TbtRingSetUnsolicited(PTBT_RING ring, TBT_UNSOLICITED fn, PVOID context)
{
    ring->Unsolicited = fn;
    ring->UnsolicitedContext = context;
}

/*
 * Record 22 section 4.4 step 2 (Guide 5.7.3; Linux nhi.c tb_ring_stop,
 * which also clears the index register): ring 0 disabled whatever state a
 * predecessor left it in, before any descriptor address is written, then
 * the settle. Rings 1 and up are only looked at.
 */
/* DmaMayRun cleared only on evidence: after the settle, both ring 0
 * controls read back, readable (not all ones) and with Valid clear. A
 * refused or unanswered read leaves it set (cm_live.h, cm_pool.c). */
static VOID tbtRingVerifyStopped(PTBT_RING ring)
{
    ULONG tx;
    ULONG rx;

    TbtWaitMin(ring->Host, TBT_RING_SETTLE_US);
    tx = TBT_RD(ring, TBT_TX_CTRL(0));
    rx = TBT_RD(ring, TBT_RX_CTRL(0));
    if (tx != 0xFFFFFFFFUL && rx != 0xFFFFFFFFUL &&
        (tx & TBT_RING_CTRL_VALID) == 0 && (rx & TBT_RING_CTRL_VALID) == 0) {
        ring->DmaMayRun = FALSE;
    } else {
        ring->Counters.StopUnverified++;
        tbtLog(ring->Host, "tbt.ring.stop-unverified", rx);
    }
}

static VOID tbtRingTakeOver(PTBT_RING ring, ULONG paths)
{
    ULONG n;
    ULONG others;

    if ((TBT_RD(ring, TBT_TX_CTRL(0)) & TBT_RING_CTRL_VALID) != 0 ||
        (TBT_RD(ring, TBT_RX_CTRL(0)) & TBT_RING_CTRL_VALID) != 0) {
        ring->Counters.Ring0WasEnabled++;
        tbtLog(ring->Host, "tbt.ring.ring0-was-enabled", 1);
    }
    others = 0;
    for (n = 1; n < paths; n++) {
        if ((TBT_RD(ring, TBT_TX_CTRL(n)) & TBT_RING_CTRL_VALID) != 0 ||
            (TBT_RD(ring, TBT_RX_CTRL(n)) & TBT_RING_CTRL_VALID) != 0) {
            others++;
        }
    }
    if (others != 0) {
        ring->Counters.OtherRingsEnabled += others;
        tbtLog(ring->Host, "tbt.ring.other-rings-enabled", others);
    }
    TBT_WR(ring, TBT_TX_CTRL(0), 0);
    TBT_WR(ring, TBT_RX_CTRL(0), 0);
    TBT_WR(ring, TBT_TX_RING(0) + TBT_RING_BASE_LO, 0);
    TBT_WR(ring, TBT_TX_RING(0) + TBT_RING_BASE_HI, 0);
    TBT_WR(ring, TBT_TX_RING(0) + TBT_RING_SIZE_REG, 0);
    TBT_WR(ring, TBT_TX_RING(0) + TBT_RING_INDEX, 0);
    TBT_WR(ring, TBT_RX_RING(0) + TBT_RING_BASE_LO, 0);
    TBT_WR(ring, TBT_RX_RING(0) + TBT_RING_BASE_HI, 0);
    TBT_WR(ring, TBT_RX_RING(0) + TBT_RING_SIZE_REG, 0);
    TBT_WR(ring, TBT_RX_RING(0) + TBT_RING_INDEX, 0);
    TbtWaitMin(ring->Host, TBT_RING_SETTLE_US);
}

LONG TbtRingStart(PTBT_RING ring,
                  const TBT_HOST *host,
                  PVOID va,
                  ULONG phys,
                  ULONG bytes,
                  ULONG rxPdfMask,
                  ULONG flags)
{
    return TbtRingStartEx(ring, host, va, phys, bytes, rxPdfMask, flags,
                          TBT_RING_ENTRIES);
}

LONG TbtRingStartEx(PTBT_RING ring,
                    const TBT_HOST *host,
                    PVOID va,
                    ULONG phys,
                    ULONG bytes,
                    ULONG rxPdfMask,
                    ULONG flags,
                    ULONG entries)
{
    ULONG caps;
    ULONG paths;
    ULONG i;
    ULONG n;
    ULONG idx;
    ULONG need;
    TBT_DEADLINE d;

    ring->Host = host;
    ring->Running = FALSE;
    ring->Programmed = FALSE;
    if (entries < 2 || entries > TBT_RING_MAX_ENTRIES) {
        return TBT_E_PARAM;
    }
    need = TBT_RING_DMA_BYTES_N(entries);
    if (va == NULL || bytes < need ||
        (phys & (TBT_RING_DMA_ALIGN - 1)) != 0 ||
        phys > 0xFFFFFFFFUL - need) {
        tbtLog(host, "tbt.ring.memory", bytes);
        return TBT_E_MEMORY;
    }
    if (ring->DmaMayRun && ring->Va == (volatile UCHAR *)va) {
        /* The host interface may still own this buffer (no verified stop
         * since its rings ran): nothing is cleared or reprogrammed in it. */
        tbtLog(host, "tbt.ring.reuse-unverified", 1);
        return TBT_E_STATE;
    }
    ring->Va = (volatile UCHAR *)va;
    ring->Phys = phys;
    ring->Entries = entries;

    caps = TBT_RD(ring, TBT_HI_CAPS);
    paths = TBT_HI_CAPS_PATHS(caps);
    tbtLog(host, "tbt.ring.caps", caps);
    if (caps == 0xFFFFFFFFUL || paths == 0 || paths > TBT_HI_CAPS_PATHS_MAX) {
        return TBT_E_HW;
    }
    ring->Paths = paths;

    ring->Programmed = TRUE;

    /* Every cause masked first: this driver polls (record 21 section 4.3)
     * and has no handler for a cause a predecessor left enabled (record
     * 22 section 4.4 step 1). */
    n = TBT_HI_IMR_DWORDS(paths);
    for (i = 0; i < n; i++) {
        if ((flags & TBT_RING_START_MASK_IMC) != 0) {
            TBT_WR(ring, TBT_HI_IMC + 4UL * i, 0xFFFFFFFFUL);
        } else {
            TBT_WR(ring, TBT_HI_IMR + 4UL * i, 0);
        }
    }

    if ((flags & TBT_RING_START_TAKEOVER) != 0) {
        tbtRingTakeOver(ring, paths);
    }
    if ((flags & TBT_RING_START_RESET_HI) != 0) {
        /* Spec 12.6.3.1.3: self-clearing within tHIReset. */
        TBT_WR(ring, TBT_HI_RESET, TBT_HI_RESET_RST);
        TbtDeadlineStart(&d, host, TBT_HI_RESET_WAIT_MS * 2);
        while ((TBT_RD(ring, TBT_HI_RESET) & TBT_HI_RESET_RST) != 0) {
            if (TbtDeadlinePassed(&d, host)) {
                tbtLog(host, "tbt.ring.reset-stuck", 0);
                return TBT_E_HW;
            }
            TbtDeadlineWait(&d, host);
        }
    }

    for (i = 0; i < need; i++) {
        ring->Va[i] = 0;
    }

    /* TX ring 0, Spec 12.6.3.2: base and size (each resets the indexes),
     * then Valid and Raw Mode in one DW write (Table 12-21's note). */
    TBT_WR(ring, TBT_TX_RING(0) + TBT_RING_BASE_LO, phys);
    TBT_WR(ring, TBT_TX_RING(0) + TBT_RING_BASE_HI, 0);
    TBT_WR(ring, TBT_TX_RING(0) + TBT_RING_SIZE_REG, entries);
    ring->DmaMayRun = TRUE;
    TBT_WR(ring, TBT_TX_CTRL(0), TBT_RING_CTRL_VALID | TBT_RING_CTRL_RAW);

    /* RX ring 0, Spec 12.6.3.3: every descriptor ready before the ring is
     * valid, then the Consumer Index one behind the Producer, so all but one
     * are the host interface's (Table 12-24's vacancy rule). */
    for (i = 0; i < entries; i++) {
        tbtRxPost(ring, i);
    }
    TBT_WR(ring, TBT_RX_RING(0) + TBT_RING_BASE_LO, phys + tbtRxDescOff(ring));
    TBT_WR(ring, TBT_RX_RING(0) + TBT_RING_BASE_HI, 0);
    TBT_WR(ring, TBT_RX_RING(0) + TBT_RING_SIZE_REG,
           entries | (TBT_RING_BUF_BYTES << TBT_RX_BUFSIZE_SHIFT));
    TBT_WR(ring, TBT_RX_PDF(0), rxPdfMask);
    ring->RxPdfMask = rxPdfMask;
    TBT_WR(ring, TBT_RX_CTRL(0), TBT_RING_CTRL_VALID | TBT_RING_CTRL_RAW);
    TBT_WR(ring, TBT_RX_RING(0) + TBT_RING_INDEX, entries - 1);

    ring->TxNext = 0;
    ring->RxNext = 0;

    /* Writing the base reset both TX indexes (Table 12-17). A ring whose
     * indexes did not move to an agreeing pair is not one to send on. */
    idx = TBT_RD(ring, TBT_TX_RING(0) + TBT_RING_INDEX);
    if (idx != 0) {
        tbtLog(host, "tbt.ring.tx-index", idx);
        if (idx == 0xFFFFFFFFUL ||
            TBT_RING_PROD(idx) != TBT_RING_CONS(idx) ||
            TBT_RING_PROD(idx) >= entries) {
            TBT_WR(ring, TBT_TX_CTRL(0), 0);
            TBT_WR(ring, TBT_RX_CTRL(0), 0);
            TBT_WR(ring, TBT_RX_RING(0) + TBT_RING_INDEX, 0);
            tbtRingVerifyStopped(ring);
            return TBT_E_HW;
        }
        ring->TxNext = TBT_RING_PROD(idx);
    }
    ring->Running = TRUE;
    return TBT_OK;
}

BOOLEAN TbtRingIntact(PTBT_RING ring)
{
    ULONG ctrl;

    /* Every value as the start wrote it, the controls whole (only Valid
     * and Raw Mode were set: an unreadable all-ones control, or a bit the
     * start never set, is a change). */
    ctrl = TBT_RING_CTRL_VALID | TBT_RING_CTRL_RAW;
    return (BOOLEAN)(
        TBT_RD(ring, TBT_TX_RING(0) + TBT_RING_BASE_LO) == ring->Phys &&
        TBT_RD(ring, TBT_TX_RING(0) + TBT_RING_BASE_HI) == 0 &&
        TBT_RD(ring, TBT_TX_RING(0) + TBT_RING_SIZE_REG) == ring->Entries &&
        TBT_RD(ring, TBT_TX_CTRL(0)) == ctrl &&
        TBT_RD(ring, TBT_RX_RING(0) + TBT_RING_BASE_LO) ==
            ring->Phys + tbtRxDescOff(ring) &&
        TBT_RD(ring, TBT_RX_RING(0) + TBT_RING_BASE_HI) == 0 &&
        TBT_RD(ring, TBT_RX_RING(0) + TBT_RING_SIZE_REG) ==
            (ring->Entries | (TBT_RING_BUF_BYTES << TBT_RX_BUFSIZE_SHIFT)) &&
        TBT_RD(ring, TBT_RX_PDF(0)) == ring->RxPdfMask &&
        TBT_RD(ring, TBT_RX_CTRL(0)) == ctrl);
}

VOID TbtRingStop(PTBT_RING ring)
{
    if (!ring->Running && !ring->DmaMayRun) {
        return;
    }
    TBT_WR(ring, TBT_TX_CTRL(0), 0);
    TBT_WR(ring, TBT_RX_CTRL(0), 0);
    TBT_WR(ring, TBT_RX_RING(0) + TBT_RING_INDEX, 0);
    ring->Running = FALSE;
    tbtRingVerifyStopped(ring);
}

/* ---------------------------------------------------------------------- */
/* Transmit, Spec 12.3                                                    */
/* ---------------------------------------------------------------------- */

LONG TbtRingSend(PTBT_RING ring, ULONG pdf, const ULONG *dw, ULONG words)
{
    return TbtRingSendUs(ring, pdf, dw, words, TBT_TX_CONSUME_MS * 1000UL);
}

/* One step of a ring wait, added to the ring's waited sum. */
static VOID tbtRingStep(PTBT_RING ring, PTBT_WAIT w)
{
    ULONG before;

    before = w->WaitedUs;
    TbtWaitStep(w, ring->Host, 0);
    ring->WaitedUs += w->WaitedUs - before;
}

LONG TbtRingSendUs(PTBT_RING ring, ULONG pdf, const ULONG *dw, ULONG words,
                   ULONG consumeUs)
{
    ULONG i;
    ULONG n;
    ULONG len;
    ULONG idx;
    ULONG next;
    ULONG crc;
    volatile UCHAR *buf;
    volatile UCHAR *d;
    UCHAR tmp[TBT_RING_BUF_BYTES];
    TBT_WAIT w;

    if (!ring->Running) {
        return TBT_E_STOPPED;
    }
    len = 4UL * words + 4UL;
    if (words == 0 || len > TBT_RING_BUF_BYTES || pdf > 0xEUL) {
        return TBT_E_PARAM;
    }
    i = ring->TxNext;
    next = (i + 1) % ring->Entries;

    /* Table 12-19: the Producer may not be moved onto the Consumer. */
    idx = TBT_RD(ring, TBT_TX_RING(0) + TBT_RING_INDEX);
    if (idx == 0xFFFFFFFFUL) {
        return TBT_E_HW;
    }
    if (TBT_RING_CONS(idx) == next) {
        ring->Counters.TxFull++;
        tbtLog(ring->Host, "tbt.ring.tx-full", idx);
        return TBT_E_RING_FULL;
    }

    for (n = 0; n < words; n++) {
        tbtStoreBe32(tmp + 4 * n, dw[n]);
    }
    crc = TbtCrc32c(tmp, 4UL * words);
    tbtStoreBe32(tmp + 4 * words, crc);
    buf = ring->Va + tbtTxBufOff(ring) + TBT_RING_BUF_BYTES * i;
    for (n = 0; n < len; n++) {
        buf[n] = tmp[n];
    }

    /* Spec 12.3.1 Table 12-6. In Raw Mode the EOF PDF is the packet's PDF
     * and the SOF PDF is ignored; u4hi.c writes both and so does this. */
    d = tbtTxDesc(ring, i);
    tbtStoreLe32(d + TBT_DESC_ADDR_LO,
                 ring->Phys + tbtTxBufOff(ring) + TBT_RING_BUF_BYTES * i);
    tbtStoreLe32(d + TBT_DESC_ADDR_HI, 0);
    tbtStoreLe32(d + TBT_DESC_DW3, 0);
    tbtStoreLe32(d + TBT_DESC_DW2,
                 len | ((pdf & TBT_DESC_PDF_MASK) << TBT_DESC_EOF_SHIFT) |
                 ((pdf & TBT_DESC_PDF_MASK) << TBT_DESC_SOF_SHIFT) |
                 TBT_DESC_RS);

    ring->TxNext = next;
    TBT_WR(ring, TBT_TX_RING(0) + TBT_RING_INDEX, next << TBT_RING_PROD_SHIFT);
    ring->Counters.Tx++;

    TbtWaitStart(&w, ring->Host, consumeUs, FALSE);
    for (;;) {
        idx = TBT_RD(ring, TBT_TX_RING(0) + TBT_RING_INDEX);
        if ((idx != 0xFFFFFFFFUL && TBT_RING_CONS(idx) == next) ||
            (tbtLoadLe32(d + TBT_DESC_DW2) & TBT_DESC_DD) != 0) {
            return TBT_OK;
        }
        if (TbtWaitDone(&w, ring->Host)) {
            break;
        }
        tbtRingStep(ring, &w);
    }
    ring->Counters.TxStuck++;
    tbtLog(ring->Host, "tbt.ring.tx-stuck", idx);
    return TBT_E_HW;
}

/* ---------------------------------------------------------------------- */
/* Receive, Spec 12.4                                                     */
/* ---------------------------------------------------------------------- */

BOOLEAN TbtRingReceive(PTBT_RING ring, PTBT_FRAME frame)
{
    ULONG i;
    ULONG n;
    ULONG len;
    ULONG dw2;
    volatile UCHAR *d;
    volatile UCHAR *buf;
    UCHAR tmp[TBT_RING_BUF_BYTES];

    if (!ring->Running) {
        return FALSE;
    }
    /* The transport's own entry gate: no descriptor is taken or reposted
     * once the host interface may not be accessed (TBT_HOST.Cancelled). */
    if (ring->Host->Cancelled != NULL &&
        ring->Host->Cancelled(ring->Host->Context)) {
        return FALSE;
    }
    i = ring->RxNext;
    d = tbtRxDesc(ring, i);
    dw2 = tbtLoadLe32(d + TBT_DESC_DW2);
    if ((dw2 & TBT_DESC_DD) == 0) {
        return FALSE;
    }

    frame->Pdf = (dw2 >> TBT_DESC_EOF_SHIFT) & TBT_DESC_PDF_MASK;
    frame->Words = 0;
    frame->Flags = 0;
    len = dw2 & TBT_DESC_LEN_MASK;
    if (len == 0) {
        len = 4096;     /* Table 12-8: 0 is 4096 bytes */
    }
    frame->Length = len;
    ring->Counters.Rx++;

    if ((dw2 & TBT_DESC_RX_BO) != 0 || len > TBT_RING_BUF_BYTES) {
        frame->Flags |= TBT_FRAME_OVERFLOW;
        ring->Counters.Overflows++;
        tbtLog(ring->Host, "tbt.ring.rx-overflow", dw2);
    } else if (len < 8 || (len & 3) != 0) {
        frame->Flags |= TBT_FRAME_MALFORMED;
        ring->Counters.Malformed++;
        tbtLog(ring->Host, "tbt.ring.rx-malformed", dw2);
    } else {
        buf = ring->Va + tbtRxBufOff(ring) + TBT_RING_BUF_BYTES * i;
        for (n = 0; n < len; n++) {
            tmp[n] = buf[n];
        }
        if (TbtCrc32c(tmp, len - 4) != tbtLoadBe32(tmp + len - 4)) {
            frame->Flags |= TBT_FRAME_CRC_BAD;
            ring->Counters.CrcErrors++;
            tbtLog(ring->Host, "tbt.ring.rx-crc", frame->Pdf);
        }
        /* Control packets are dropped on a bad trailer (Spec 6.4.4 has the
         * request retried); ICM frames are not, record 21 section 5.2. */
        if ((frame->Flags & TBT_FRAME_CRC_BAD) == 0 ||
            frame->Pdf >= TBT_PDF_ICM_EVENT) {
            frame->Words = len / 4 - 1;
            for (n = 0; n < frame->Words; n++) {
                frame->Dw[n] = tbtLoadBe32(tmp + 4 * n);
            }
            ring->Counters.RxUsable++;
        }
    }

    /* Give the descriptor back: the Consumer Index names the one just
     * taken, which leaves exactly one vacant (Spec 12.4.2.2, Table 12-24). */
    tbtRxPost(ring, i);
    TBT_WR(ring, TBT_RX_RING(0) + TBT_RING_INDEX, i);
    ring->RxNext = (i + 1) % ring->Entries;
    return TRUE;
}

/* ---------------------------------------------------------------------- */
/* Read and Write Requests, Spec 6.4.2.3 to 6.4.2.6                       */
/* ---------------------------------------------------------------------- */

static ULONG tbtReqDw3(ULONG address, ULONG count, ULONG adapter, ULONG cs,
                       ULONG seq)
{
    return (address & TBT_CTL_ADDR_MASK) |
           ((count & TBT_CTL_SIZE_MASK) << TBT_CTL_SIZE_SHIFT) |
           ((adapter & TBT_CTL_ADP_MASK) << TBT_CTL_ADP_SHIFT) |
           ((cs & TBT_CTL_CS_MASK) << TBT_CTL_CS_SHIFT) |
           ((seq & TBT_CTL_SN_MASK) << TBT_CTL_SN_SHIFT);
}

static VOID tbtHandOff(PTBT_RING ring, const TBT_FRAME *f)
{
    ring->Counters.Unsolicited++;
    if (ring->Unsolicited != NULL) {
        ring->Unsolicited(ring->UnsolicitedContext, f);
    }
}

ULONG TbtCtlClassify(ULONG code)
{
    switch (code) {
    case TBT_EV_ERR_CONN:
    case TBT_EV_ERR_ADDR:
    case TBT_EV_ERR_ADP:
    case TBT_EV_ERR_ENUM:
    case TBT_EV_ERR_NUA:
    case TBT_EV_ERR_LEN:
    case TBT_EV_ERR_LOCK:
        return TBT_NOTE_REQUEST;
    case TBT_EV_ERR_LINK:
    case TBT_EV_ERR_HEC:
    case TBT_EV_ERR_FC:
    case TBT_EV_ERR_PLUG:
        return TBT_NOTE_EVENT;
    case TBT_EV_DP_BW:
    case TBT_EV_PCIE_WAKE:
    case TBT_EV_DP_CON_CHANGE:
    case TBT_EV_DPTX_DISCOVERY:
        return TBT_NOTE_ACK_IGNORE;
    case TBT_EV_ROP_CMPLT:
    case TBT_EV_POP_CMPLT:
    case TBT_EV_LINK_RECOVERY:
    case TBT_EV_ASYM_LINK:
        return TBT_NOTE_SILENT;
    case TBT_EV_HP_ACK:
        return TBT_NOTE_HP_ACK;
    }
    return TBT_NOTE_UNKNOWN;
}

LONG TbtRingCtlTransfer(PTBT_RING ring,
                        ULONG pdf,
                        const TBT_ROUTE *route,
                        ULONG cs,
                        ULONG adapter,
                        ULONG address,
                        ULONG count,
                        PULONG buf,
                        ULONG timeoutUs,
                        ULONG attempts)
{
    ULONG req[3 + TBT_CTL_SIZE_MAX];
    ULONG want;
    ULONG attempt;
    ULONG k;
    ULONG words;
    ULONG frames;
    ULONG ev;
    ULONG echo;
    ULONG acked;
    LONG rc;
    TBT_FRAME f;
    TBT_WAIT w;

    if ((pdf != TBT_PDF_READ && pdf != TBT_PDF_WRITE) ||
        count == 0 || count > TBT_CTL_SIZE_MAX || cs > TBT_CS_COUNTERS ||
        address > TBT_CTL_ADDR_MASK || adapter > TBT_CTL_ADP_MASK) {
        return TBT_E_PARAM;
    }
    if (attempts == 0) {
        attempts = 1;
    }
    echo = TBT_CTL_ECHO_MASK;
    if (cs != TBT_CS_ROUTER) {
        echo |= TBT_CTL_ADP_MASK << TBT_CTL_ADP_SHIFT;
    }
    /* Spec 6.4.2.3's note: Adapter Num 0 for the router space. */
    if (cs == TBT_CS_ROUTER) {
        adapter = 0;
    }
    if (pdf == TBT_PDF_READ) {
        ring->Counters.CtlReads++;
    } else {
        ring->Counters.CtlWrites++;
    }
    for (attempt = 0; attempt < attempts; attempt++) {
        req[0] = route->Hi & TBT_ROUTE_HI_TOPOLOGY;
        req[1] = route->Lo;
        req[2] = tbtReqDw3(address, count, adapter, cs, ring->Seq);
        want = req[2];
        words = 3;
        if (pdf == TBT_PDF_WRITE) {
            for (k = 0; k < count; k++) {
                req[3 + k] = buf[k];
            }
            words += count;
        }
        ring->Seq = (ring->Seq + 1) & TBT_CTL_SN_MASK;
        if (attempt != 0) {
            ring->Counters.CtlRetries++;
        }
        rc = TbtRingSendUs(ring, pdf, req, words,
                           ring->TxConsumeUs != 0 ? ring->TxConsumeUs :
                           TBT_TX_CONSUME_MS * 1000UL);
        if (rc != TBT_OK) {
            return rc;
        }
        TbtWaitStart(&w, ring->Host, timeoutUs, FALSE);
        frames = 0;
        for (;;) {
            /* The budget is checked before a frame is taken, so the frame
             * that would pass it stays on the ring for the next drain. */
            if (frames >= TBT_WAIT_FRAME_BUDGET) {
                break;
            }
            if (!TbtRingReceive(ring, &f)) {
                if (TbtWaitDone(&w, ring->Host)) {
                    break;
                }
                tbtRingStep(ring, &w);
                continue;
            }
            frames++;
            if (f.Words == 0) {
                if (TbtWaitDone(&w, ring->Host)) {
                    break;
                }
                continue;
            }
            if (f.Pdf == TBT_PDF_NOTIFY && f.Words >= 3) {
                /*
                 * An error notification comes from the router that could not
                 * deliver or execute the request, which may be one upstream
                 * of the target, so its route is not compared: one request is
                 * outstanding at a time (Spec 6.4.2.3's note). Only the
                 * request-error codes end the request (record 22 section
                 * 5.1); the others are events and go to the receiver.
                 */
                ev = TBT_NOTIFY_EVENT(f.Dw[2]);
                if (TbtCtlClassify(ev) == TBT_NOTE_REQUEST) {
                    ring->LastEvent = ev;
                    ring->LastEventInfo = TBT_NOTIFY_INFO(f.Dw[2]);
                    ring->LastEventRouteHi = f.Dw[0] & TBT_ROUTE_HI_TOPOLOGY;
                    ring->LastEventRouteLo = f.Dw[1];
                    ring->Counters.CtlNotify++;
                    tbtLog(ring->Host, "tbt.ring.ctl-notify", ev);
                    return TBT_E_NOTIFY;
                }
            }
            if (f.Pdf != pdf || f.Words < 3 ||
                ((f.Dw[0] ^ route->Hi) & TBT_ROUTE_HI_TOPOLOGY) != 0 ||
                f.Dw[1] != route->Lo) {
                /* An acknowledgement the hand-off sends waits too: it
                 * counts against this attempt, so a frozen clock still
                 * ends it. */
                acked = ring->WaitedUs;
                tbtHandOff(ring, &f);
                w.WaitedUs += ring->WaitedUs - acked;
                if (TbtWaitDone(&w, ring->Host)) {
                    break;
                }
                continue;
            }
            if ((f.Dw[2] & echo) != (want & echo)) {
                ring->Counters.CtlStale++;
                if (TbtWaitDone(&w, ring->Host)) {
                    break;
                }
                continue;
            }
            if (pdf == TBT_PDF_READ) {
                if (f.Words < 3 + count) {
                    ring->Counters.Malformed++;
                    return TBT_E_HW;
                }
                for (k = 0; k < count; k++) {
                    buf[k] = f.Dw[3 + k];
                }
                if (cs == TBT_CS_ROUTER) {
                    ring->LastReadAdapter = (f.Dw[2] >> TBT_CTL_ADP_SHIFT) &
                                            TBT_CTL_ADP_MASK;
                }
            }
            return TBT_OK;
        }
        tbtLog(ring->Host, "tbt.ring.ctl-no-answer", want);
    }
    ring->Counters.CtlTimeouts++;
    return TBT_E_TIMEOUT;
}

LONG TbtRingCtlRead(PTBT_RING ring,
                    const TBT_ROUTE *route,
                    ULONG cs,
                    ULONG adapter,
                    ULONG address,
                    ULONG count,
                    PULONG out,
                    ULONG timeoutMs,
                    ULONG retries)
{
    return TbtRingCtlTransfer(ring, TBT_PDF_READ, route, cs, adapter, address,
                              count, out, timeoutMs * 1000UL, retries + 1);
}

/* ---------------------------------------------------------------------- */
/* The USB4 control transport, record 22 sections 5.1 and 7.4             */
/* ---------------------------------------------------------------------- */

static VOID tbtCtlRouteOf(const TBT_FRAME *f, PTBT_ROUTE r)
{
    r->Hi = f->Dw[0] & TBT_ROUTE_HI_TOPOLOGY;
    r->Lo = f->Dw[1];
}

/* Spec 6.4.2.8 (two DWs: the route) and 6.4.2.9 (the route and the
 * Sequence bit); Linux ctl.c tb_cfg_ack_notification sends the first. */
static VOID tbtCtlSendAck(PTBT_CTL ctl, const TBT_NOTE *n, ULONG form)
{
    ULONG dw[3];
    LONG rc;

    dw[0] = n->Route.Hi & TBT_ROUTE_HI_TOPOLOGY;
    dw[1] = n->Route.Lo;
    if (form == TBT_ACK_ENHANCED) {
        dw[2] = n->Sq ? TBT_NOTIFY_ACK_ENH_SQ : 0;
        rc = TbtRingSendUs(ctl->Ring, TBT_PDF_NOTIFY_ACK_ENH, dw, 3,
                           TBT_TX_CONSUME_U4_US);
        if (rc == TBT_OK) {
            ctl->Counters.AcksEnhanced++;
        }
    } else {
        rc = TbtRingSendUs(ctl->Ring, TBT_PDF_NOTIFY_ACK, dw, 2,
                           TBT_TX_CONSUME_U4_US);
        if (rc == TBT_OK) {
            ctl->Counters.AcksPlain++;
        }
    }
    if (rc != TBT_OK) {
        ctl->Counters.AckFailures++;
        tbtLog(ctl->Ring->Host, "tbt.ctl.ack-failed", n->Code);
    }
}

static ULONG tbtCtlForm(PTBT_CTL ctl, const TBT_ROUTE *route)
{
    if (ctl->AckForm == NULL) {
        return TBT_ACK_PLAIN;
    }
    return ctl->AckForm(ctl->AckContext, route);
}

static VOID tbtCtlAcknowledge(PTBT_CTL ctl, const TBT_NOTE *n)
{
    ULONG form;

    form = tbtCtlForm(ctl, &n->Route);
    if (form == TBT_ACK_PLAIN || form == TBT_ACK_ENHANCED) {
        tbtCtlSendAck(ctl, n, form);
    } else if (form == TBT_ACK_PENDING &&
               ctl->DeferCount < TBT_CTL_DEFER_QUEUE) {
        ctl->Deferred[ctl->DeferCount++] = *n;
        ctl->Counters.AcksDeferred++;
    } else {
        ctl->Counters.AcksWithheld++;
        tbtLog(ctl->Ring->Host, "tbt.ctl.ack-withheld", n->Code);
    }
}

static VOID tbtCtlQueueNote(PTBT_CTL ctl, const TBT_NOTE *n)
{
    if (ctl->NoteCount >= TBT_CTL_NOTE_QUEUE) {
        ctl->NotesLost = TRUE;
        ctl->Counters.NoteOverflows++;
        return;
    }
    ctl->Notes[(ctl->NoteHead + ctl->NoteCount) % TBT_CTL_NOTE_QUEUE] = *n;
    ctl->NoteCount++;
}

/* Spec 6.4.2.10, Table 6-12 HP_ACK; Linux ctl.c tb_cfg_ack_plug (a
 * Notification Packet with the adapter and PG); acknowledged before it is
 * processed, in the order received (Guide 4.3). */
static VOID tbtCtlHotPlug(PTBT_CTL ctl, const TBT_FRAME *f)
{
    TBT_HP_EVENT e;
    ULONG dw[3];
    ULONG k;
    PTBT_HP_EVENT q;

    tbtCtlRouteOf(f, &e.Route);
    e.Adapter = TBT_HOTPLUG_ADAPTER(f->Dw[2]);
    e.Unplug = (BOOLEAN)((f->Dw[2] & TBT_HOTPLUG_UPG) != 0);

    dw[0] = e.Route.Hi;
    dw[1] = e.Route.Lo;
    dw[2] = TBT_EV_HP_ACK | (e.Adapter << TBT_NOTIFY_INFO_SHIFT) |
            ((e.Unplug ? TBT_NOTIFY_PG_UNPLUG : TBT_NOTIFY_PG_PLUG) <<
             TBT_NOTIFY_PG_SHIFT);
    if (TbtRingSendUs(ctl->Ring, TBT_PDF_NOTIFY, dw, 3,
                      TBT_TX_CONSUME_U4_US) != TBT_OK) {
        ctl->Counters.HotPlugAckFailures++;
        tbtLog(ctl->Ring->Host, "tbt.ctl.hp-ack-failed", e.Adapter);
    }

    /* A retransmitted copy of a queued event is dropped (u4hi.c). */
    for (k = 0; k < ctl->HpCount; k++) {
        q = &ctl->HotPlug[(ctl->HpHead + k) % TBT_CTL_HP_QUEUE];
        if (TbtRouteEqual(&q->Route, &e.Route) && q->Adapter == e.Adapter &&
            q->Unplug == e.Unplug) {
            ctl->Counters.HotPlugDuplicates++;
            return;
        }
    }
    if (e.Unplug) {
        ctl->Counters.HotUnplugs++;
    } else {
        ctl->Counters.HotPlugs++;
    }
    if (ctl->HpCount >= TBT_CTL_HP_QUEUE) {
        ctl->HpLost = TRUE;
        ctl->Counters.HotPlugOverflows++;
        tbtLog(ctl->Ring->Host, "tbt.ctl.hp-overflow", e.Adapter);
        return;
    }
    ctl->HotPlug[(ctl->HpHead + ctl->HpCount) % TBT_CTL_HP_QUEUE] = e;
    ctl->HpCount++;
}

static VOID tbtCtlNotification(PTBT_CTL ctl, const TBT_FRAME *f)
{
    TBT_NOTE n;
    ULONG cls;

    tbtCtlRouteOf(f, &n.Route);
    n.Code = TBT_NOTIFY_EVENT(f->Dw[2]);
    n.Info = TBT_NOTIFY_INFO(f->Dw[2]);
    n.Sq = (BOOLEAN)((f->Dw[2] & TBT_NOTIFY_SQ) != 0);
    if (n.Code < TBT_CTL_EVENT_CODES) {
        ctl->Counters.Events[n.Code]++;
    } else {
        ctl->Counters.EventsOther++;
    }
    cls = TbtCtlClassify(n.Code);
    switch (cls) {
    case TBT_NOTE_EVENT:
        tbtLog(ctl->Ring->Host, "tbt.ctl.event", (n.Code << 8) | n.Info);
        tbtCtlAcknowledge(ctl, &n);
        if (n.Code == TBT_EV_ERR_PLUG) {
            tbtCtlQueueNote(ctl, &n);
        }
        break;
    case TBT_NOTE_ACK_IGNORE:
        tbtCtlAcknowledge(ctl, &n);
        break;
    case TBT_NOTE_REQUEST:
        /* No request waits for it: a disconnect or a re-lock the connection
         * manager checks the port for; the others are only counted. */
        if (n.Code == TBT_EV_ERR_CONN || n.Code == TBT_EV_ERR_LOCK ||
            n.Code == TBT_EV_ERR_ENUM || n.Code == TBT_EV_ERR_NUA) {
            tbtCtlQueueNote(ctl, &n);
        } else {
            ctl->Counters.Strays++;
        }
        break;
    case TBT_NOTE_SILENT:
        break;
    default:
        ctl->Counters.Strays++;
        break;
    }
}

static VOID tbtCtlReceive(PVOID context, const TBT_FRAME *f)
{
    PTBT_CTL ctl;

    ctl = (PTBT_CTL)context;
    if (f->Words < 3) {
        ctl->Counters.Strays++;
        return;
    }
    if (f->Pdf == TBT_PDF_HOTPLUG) {
        tbtCtlHotPlug(ctl, f);
    } else if (f->Pdf == TBT_PDF_NOTIFY) {
        tbtCtlNotification(ctl, f);
    } else {
        ctl->Counters.Strays++;
    }
}

VOID TbtCtlInit(PTBT_CTL ctl, PTBT_RING ring)
{
    ULONG i;
    UCHAR *p;

    p = (UCHAR *)ctl;
    for (i = 0; i < sizeof(*ctl); i++) {
        p[i] = 0;
    }
    ctl->Ring = ring;
    ring->TxConsumeUs = TBT_TX_CONSUME_U4_US;
    ctl->TimeoutUs = TBT_CTL_TIMEOUT_US;
    ctl->Attempts = TBT_CTL_ATTEMPTS;
    TbtRingSetUnsolicited(ring, tbtCtlReceive, ctl);
}

VOID TbtCtlSetAckForm(PTBT_CTL ctl, TBT_CTL_ACK_FORM fn, PVOID context)
{
    ctl->AckForm = fn;
    ctl->AckContext = context;
}

VOID TbtCtlSetSafePoint(PTBT_CTL ctl, TBT_CTL_SAFE_POINT fn, PVOID context)
{
    ctl->SafePoint = fn;
    ctl->SafeContext = context;
}

static LONG tbtCtlCount(PTBT_CTL ctl, LONG rc)
{
    if (rc == TBT_E_TIMEOUT) {
        ctl->Counters.Timeouts++;
    } else if (rc == TBT_E_NOTIFY) {
        ctl->Counters.Notified++;
    }
    return rc;
}

LONG TbtCtlRead(PTBT_CTL ctl, const TBT_ROUTE *route, ULONG cs, ULONG adapter,
                ULONG address, ULONG count, PULONG out)
{
    ULONG before;
    LONG rc;

    ctl->Counters.Requests++;
    before = ctl->Ring->Counters.CtlRetries;
    rc = TbtRingCtlTransfer(ctl->Ring, TBT_PDF_READ, route, cs, adapter,
                            address, count, out, ctl->TimeoutUs,
                            ctl->Attempts);
    ctl->Counters.Retries += ctl->Ring->Counters.CtlRetries - before;
    return tbtCtlCount(ctl, rc);
}

LONG TbtCtlWrite(PTBT_CTL ctl, const TBT_ROUTE *route, ULONG cs,
                 ULONG adapter, ULONG address, ULONG count, const ULONG *in)
{
    ULONG tmp[TBT_CTL_SIZE_MAX];
    ULONG before;
    ULONG k;
    LONG rc;

    if (count == 0 || count > TBT_CTL_SIZE_MAX) {
        return TBT_E_PARAM;
    }
    for (k = 0; k < count; k++) {
        tmp[k] = in[k];
    }
    ctl->Counters.Requests++;
    before = ctl->Ring->Counters.CtlRetries;
    rc = TbtRingCtlTransfer(ctl->Ring, TBT_PDF_WRITE, route, cs, adapter,
                            address, count, tmp, ctl->TimeoutUs,
                            ctl->Attempts);
    ctl->Counters.Retries += ctl->Ring->Counters.CtlRetries - before;
    return tbtCtlCount(ctl, rc);
}

LONG TbtCtlRead1(PTBT_CTL ctl, const TBT_ROUTE *route, ULONG cs,
                 ULONG adapter, ULONG address, PULONG out)
{
    return TbtCtlRead(ctl, route, cs, adapter, address, 1, out);
}

LONG TbtCtlWrite1(PTBT_CTL ctl, const TBT_ROUTE *route, ULONG cs,
                  ULONG adapter, ULONG address, ULONG value)
{
    return TbtCtlWrite(ctl, route, cs, adapter, address, 1, &value);
}

VOID TbtCtlService(PTBT_CTL ctl)
{
    TbtCtlServiceN(ctl, TBT_WAIT_FRAME_BUDGET);
}

VOID TbtCtlServiceN(PTBT_CTL ctl, ULONG maxFrames)
{
    TBT_FRAME f;
    ULONG frames;

    for (frames = 0; frames < maxFrames; frames++) {
        if (!TbtRingReceive(ctl->Ring, &f)) {
            return;
        }
        if (f.Words != 0) {
            tbtHandOff(ctl->Ring, &f);
        }
    }
}

BOOLEAN TbtCtlNextHotPlug(PTBT_CTL ctl, PTBT_HP_EVENT out)
{
    if (ctl->HpCount == 0) {
        return FALSE;
    }
    *out = ctl->HotPlug[ctl->HpHead];
    ctl->HpHead = (ctl->HpHead + 1) % TBT_CTL_HP_QUEUE;
    ctl->HpCount--;
    return TRUE;
}

BOOLEAN TbtCtlNextNote(PTBT_CTL ctl, PTBT_NOTE out)
{
    if (ctl->NoteCount == 0) {
        return FALSE;
    }
    *out = ctl->Notes[ctl->NoteHead];
    ctl->NoteHead = (ctl->NoteHead + 1) % TBT_CTL_NOTE_QUEUE;
    ctl->NoteCount--;
    return TRUE;
}

VOID TbtCtlSettle(PTBT_CTL ctl, const TBT_ROUTE *route)
{
    ULONG i;
    ULONG j;
    ULONG form;
    TBT_NOTE n;

    i = 0;
    while (i < ctl->DeferCount) {
        if (!TbtRouteEqual(&ctl->Deferred[i].Route, route)) {
            i++;
            continue;
        }
        form = tbtCtlForm(ctl, route);
        if (form == TBT_ACK_PENDING) {
            return;
        }
        n = ctl->Deferred[i];
        for (j = i + 1; j < ctl->DeferCount; j++) {
            ctl->Deferred[j - 1] = ctl->Deferred[j];
        }
        ctl->DeferCount--;
        if (form == TBT_ACK_PLAIN || form == TBT_ACK_ENHANCED) {
            tbtCtlSendAck(ctl, &n, form);
        } else {
            ctl->Counters.AcksWithheld++;
        }
    }
}

VOID TbtCtlForget(PTBT_CTL ctl, const TBT_ROUTE *route)
{
    ULONG i;
    ULONG j;

    i = 0;
    while (i < ctl->DeferCount) {
        if (tbtRouteUnder(&ctl->Deferred[i].Route, route)) {
            for (j = i + 1; j < ctl->DeferCount; j++) {
                ctl->Deferred[j - 1] = ctl->Deferred[j];
            }
            ctl->DeferCount--;
        } else {
            i++;
        }
    }
}

VOID TbtCtlSafePoint(PTBT_CTL ctl)
{
    if (ctl->SafePoint == NULL || ctl->InSafePoint) {
        return;
    }
    ctl->Counters.SafePoints++;
    ctl->InSafePoint = TRUE;
    ctl->SafePoint(ctl->SafeContext);
    ctl->InSafePoint = FALSE;
}
