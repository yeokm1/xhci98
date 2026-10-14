/*
 * tbt_ring.c - the host interface's ring 0 (see tbt_ring.h).
 *
 * Lifted from usb4-emu's tools/usb4cm/core/u4hi.c, whose notice follows.
 * What changed: the accessor is TBT_HOST and the common buffer is the
 * caller's; every wait is a clock deadline with a backstop (TbtDeadline*),
 * not a count of polls; the TX ring refuses to fill its last vacant
 * descriptor; a receive buffer's Buffer Overflow bit and a length past the
 * buffer are reported rather than read past; the trailer of an ICM frame is
 * flagged, not dropped; the interrupt mask follows each host's contract; the Host
 * Interface Reset is optional, because the Alpine Ridge client does not
 * reset (ICM-PROTOCOL.md 6.1); the hot-plug and router code, which the ICM
 * client never uses, stayed behind for the software connection manager of
 * Phase 40. Spec citations are to the USB4 Specification 2.0 (November
 * 2025), sections 6.4, 12.3, 12.4 and 12.6.
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
 * everything that takes a ring or a host; TbtCrc32c and the TbtRoute
 * helpers at any IRQL. No locks.
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
/* Deadlines                                                              */
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

/* ---------------------------------------------------------------------- */
/* Start and stop, Spec 12.6                                              */
/* ---------------------------------------------------------------------- */

static volatile UCHAR *tbtTxDesc(PTBT_RING ring, ULONG i)
{
    return ring->Va + TBT_RING_TX_DESC_OFF + TBT_DESC_BYTES * i;
}

static volatile UCHAR *tbtRxDesc(PTBT_RING ring, ULONG i)
{
    return ring->Va + TBT_RING_RX_DESC_OFF + TBT_DESC_BYTES * i;
}

/* Spec 12.4.1 Table 12-7: the buffer's address, RS set so the status is
 * written back, DD clear. */
static VOID tbtRxPost(PTBT_RING ring, ULONG i)
{
    volatile UCHAR *d;

    d = tbtRxDesc(ring, i);
    tbtStoreLe32(d + TBT_DESC_ADDR_LO,
                 ring->Phys + TBT_RING_RX_BUF_OFF + TBT_RING_BUF_BYTES * i);
    tbtStoreLe32(d + TBT_DESC_ADDR_HI, 0);
    tbtStoreLe32(d + TBT_DESC_DW2, TBT_DESC_RS);
    tbtStoreLe32(d + TBT_DESC_DW3, 0);
}

VOID TbtRingSetUnsolicited(PTBT_RING ring, TBT_UNSOLICITED fn, PVOID context)
{
    ring->Unsolicited = fn;
    ring->UnsolicitedContext = context;
}

LONG TbtRingStart(PTBT_RING ring,
                  const TBT_HOST *host,
                  PVOID va,
                  ULONG phys,
                  ULONG bytes,
                  ULONG rxPdfMask,
                  ULONG flags)
{
    ULONG caps;
    ULONG paths;
    ULONG i;
    ULONG n;
    ULONG idx;
    TBT_DEADLINE d;

    ring->Host = host;
    ring->Running = FALSE;
    if (va == NULL || bytes < TBT_RING_DMA_BYTES ||
        (phys & (TBT_RING_DMA_ALIGN - 1)) != 0 ||
        phys > 0xFFFFFFFFUL - TBT_RING_DMA_BYTES) {
        tbtLog(host, "tbt.ring.memory", bytes);
        return TBT_E_MEMORY;
    }
    ring->Va = (volatile UCHAR *)va;
    ring->Phys = phys;

    caps = TBT_RD(ring, TBT_HI_CAPS);
    paths = TBT_HI_CAPS_PATHS(caps);
    tbtLog(host, "tbt.ring.caps", caps);
    if (caps == 0xFFFFFFFFUL || paths == 0 || paths > TBT_HI_CAPS_PATHS_MAX) {
        return TBT_E_HW;
    }
    ring->Paths = paths;

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

    for (i = 0; i < TBT_RING_DMA_BYTES; i++) {
        ring->Va[i] = 0;
    }

    /* Every cause masked: this driver polls (record 21 section 4.3). */
    n = TBT_HI_IMR_DWORDS(paths);
    for (i = 0; i < n; i++) {
        if ((flags & TBT_RING_START_MASK_IMC) != 0) {
            TBT_WR(ring, TBT_HI_IMC + 4UL * i, 0xFFFFFFFFUL);
        } else {
            TBT_WR(ring, TBT_HI_IMR + 4UL * i, 0);
        }
    }

    /* TX ring 0, Spec 12.6.3.2: base and size (each resets the indexes),
     * then Valid and Raw Mode in one DW write (Table 12-21's note). */
    TBT_WR(ring, TBT_TX_RING(0) + TBT_RING_BASE_LO, phys + TBT_RING_TX_DESC_OFF);
    TBT_WR(ring, TBT_TX_RING(0) + TBT_RING_BASE_HI, 0);
    TBT_WR(ring, TBT_TX_RING(0) + TBT_RING_SIZE_REG, TBT_RING_ENTRIES);
    TBT_WR(ring, TBT_TX_CTRL(0), TBT_RING_CTRL_VALID | TBT_RING_CTRL_RAW);

    /* RX ring 0, Spec 12.6.3.3: every descriptor ready before the ring is
     * valid, then the Consumer Index one behind the Producer, so all but one
     * are the host interface's (Table 12-24's vacancy rule). */
    for (i = 0; i < TBT_RING_ENTRIES; i++) {
        tbtRxPost(ring, i);
    }
    TBT_WR(ring, TBT_RX_RING(0) + TBT_RING_BASE_LO, phys + TBT_RING_RX_DESC_OFF);
    TBT_WR(ring, TBT_RX_RING(0) + TBT_RING_BASE_HI, 0);
    TBT_WR(ring, TBT_RX_RING(0) + TBT_RING_SIZE_REG,
           TBT_RING_ENTRIES | (TBT_RING_BUF_BYTES << TBT_RX_BUFSIZE_SHIFT));
    TBT_WR(ring, TBT_RX_PDF(0), rxPdfMask);
    TBT_WR(ring, TBT_RX_CTRL(0), TBT_RING_CTRL_VALID | TBT_RING_CTRL_RAW);
    TBT_WR(ring, TBT_RX_RING(0) + TBT_RING_INDEX, TBT_RING_ENTRIES - 1);

    ring->TxNext = 0;
    ring->RxNext = 0;

    /* Writing the base reset both TX indexes (Table 12-17). A ring whose
     * indexes did not move to an agreeing pair is not one to send on. */
    idx = TBT_RD(ring, TBT_TX_RING(0) + TBT_RING_INDEX);
    if (idx != 0) {
        tbtLog(host, "tbt.ring.tx-index", idx);
        if (idx == 0xFFFFFFFFUL ||
            TBT_RING_PROD(idx) != TBT_RING_CONS(idx) ||
            TBT_RING_PROD(idx) >= TBT_RING_ENTRIES) {
            TBT_WR(ring, TBT_TX_CTRL(0), 0);
            TBT_WR(ring, TBT_RX_CTRL(0), 0);
            TBT_WR(ring, TBT_RX_RING(0) + TBT_RING_INDEX, 0);
            return TBT_E_HW;
        }
        ring->TxNext = TBT_RING_PROD(idx);
    }
    ring->Running = TRUE;
    return TBT_OK;
}

VOID TbtRingStop(PTBT_RING ring)
{
    if (!ring->Running) {
        return;
    }
    TBT_WR(ring, TBT_TX_CTRL(0), 0);
    TBT_WR(ring, TBT_RX_CTRL(0), 0);
    TBT_WR(ring, TBT_RX_RING(0) + TBT_RING_INDEX, 0);
    ring->Running = FALSE;
}

/* ---------------------------------------------------------------------- */
/* Transmit, Spec 12.3                                                    */
/* ---------------------------------------------------------------------- */

LONG TbtRingSend(PTBT_RING ring, ULONG pdf, const ULONG *dw, ULONG words)
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
    TBT_DEADLINE dl;

    if (!ring->Running) {
        return TBT_E_STOPPED;
    }
    len = 4UL * words + 4UL;
    if (words == 0 || len > TBT_RING_BUF_BYTES || pdf > 0xEUL) {
        return TBT_E_PARAM;
    }
    i = ring->TxNext;
    next = (i + 1) % TBT_RING_ENTRIES;

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
    buf = ring->Va + TBT_RING_TX_BUF_OFF + TBT_RING_BUF_BYTES * i;
    for (n = 0; n < len; n++) {
        buf[n] = tmp[n];
    }

    /* Spec 12.3.1 Table 12-6. In Raw Mode the EOF PDF is the packet's PDF
     * and the SOF PDF is ignored; u4hi.c writes both and so does this. */
    d = tbtTxDesc(ring, i);
    tbtStoreLe32(d + TBT_DESC_ADDR_LO,
                 ring->Phys + TBT_RING_TX_BUF_OFF + TBT_RING_BUF_BYTES * i);
    tbtStoreLe32(d + TBT_DESC_ADDR_HI, 0);
    tbtStoreLe32(d + TBT_DESC_DW3, 0);
    tbtStoreLe32(d + TBT_DESC_DW2,
                 len | ((pdf & TBT_DESC_PDF_MASK) << TBT_DESC_EOF_SHIFT) |
                 ((pdf & TBT_DESC_PDF_MASK) << TBT_DESC_SOF_SHIFT) |
                 TBT_DESC_RS);

    ring->TxNext = next;
    TBT_WR(ring, TBT_TX_RING(0) + TBT_RING_INDEX, next << TBT_RING_PROD_SHIFT);
    ring->Counters.Tx++;

    TbtDeadlineStart(&dl, ring->Host, TBT_TX_CONSUME_MS);
    for (;;) {
        idx = TBT_RD(ring, TBT_TX_RING(0) + TBT_RING_INDEX);
        if ((idx != 0xFFFFFFFFUL && TBT_RING_CONS(idx) == next) ||
            (tbtLoadLe32(d + TBT_DESC_DW2) & TBT_DESC_DD) != 0) {
            return TBT_OK;
        }
        if (TbtDeadlinePassed(&dl, ring->Host)) {
            break;
        }
        TbtDeadlineWait(&dl, ring->Host);
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
        buf = ring->Va + TBT_RING_RX_BUF_OFF + TBT_RING_BUF_BYTES * i;
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
    ring->RxNext = (i + 1) % TBT_RING_ENTRIES;
    return TRUE;
}

/* ---------------------------------------------------------------------- */
/* Read Request, Spec 6.4.2.3 and 6.4.2.4                                 */
/* ---------------------------------------------------------------------- */

static ULONG tbtReadDw3(ULONG address, ULONG count, ULONG adapter, ULONG cs,
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
    ULONG req[3];
    ULONG want;
    ULONG attempt;
    ULONG k;
    ULONG frames;
    ULONG ev;
    LONG rc;
    TBT_FRAME f;
    TBT_DEADLINE d;

    if (count == 0 || count > TBT_CTL_SIZE_MAX || cs > TBT_CS_COUNTERS ||
        address > TBT_CTL_ADDR_MASK || adapter > TBT_CTL_ADP_MASK) {
        return TBT_E_PARAM;
    }
    /* Spec 6.4.2.3's note: Adapter Num 0 for the router space. */
    if (cs == TBT_CS_ROUTER) {
        adapter = 0;
    }
    ring->Counters.CtlReads++;
    for (attempt = 0; attempt <= retries; attempt++) {
        req[0] = route->Hi & TBT_ROUTE_HI_TOPOLOGY;
        req[1] = route->Lo;
        req[2] = tbtReadDw3(address, count, adapter, cs, ring->Seq);
        want = req[2];
        ring->Seq = (ring->Seq + 1) & TBT_CTL_SN_MASK;
        if (attempt != 0) {
            ring->Counters.CtlRetries++;
        }
        rc = TbtRingSend(ring, TBT_PDF_READ, req, 3);
        if (rc != TBT_OK) {
            return rc;
        }
        TbtDeadlineStart(&d, ring->Host, timeoutMs);
        frames = 0;
        for (;;) {
            if (!TbtRingReceive(ring, &f)) {
                if (TbtDeadlinePassed(&d, ring->Host)) {
                    break;
                }
                TbtDeadlineWait(&d, ring->Host);
                continue;
            }
            if (++frames > TBT_WAIT_FRAME_BUDGET) {
                break;
            }
            if (f.Words == 0) {
                if (TbtDeadlinePassed(&d, ring->Host)) {
                    break;
                }
                continue;
            }
            if (f.Pdf == TBT_PDF_NOTIFY && f.Words >= 3) {
                /*
                 * An error notification comes from the router that could not
                 * deliver or execute the request, which may be one upstream
                 * of the target, so its route is not compared: one request is
                 * outstanding at a time (Spec 6.4.2.3's note). The hot-plug
                 * acknowledgment code (7) and the codes from 32 up are not
                 * errors (Spec Table 6-12).
                 */
                ev = TBT_NOTIFY_EVENT(f.Dw[2]);
                if (ev != 7 && ev < 32) {
                    ring->Counters.CtlNotify++;
                    tbtLog(ring->Host, "tbt.ring.ctl-notify", ev);
                    return TBT_E_NOTIFY;
                }
            }
            if (f.Pdf != TBT_PDF_READ || f.Words < 3 ||
                ((f.Dw[0] ^ route->Hi) & TBT_ROUTE_HI_TOPOLOGY) != 0 ||
                f.Dw[1] != route->Lo) {
                tbtHandOff(ring, &f);
                if (TbtDeadlinePassed(&d, ring->Host)) {
                    break;
                }
                continue;
            }
            if ((f.Dw[2] & TBT_CTL_ECHO_MASK) != (want & TBT_CTL_ECHO_MASK)) {
                ring->Counters.CtlStale++;
                if (TbtDeadlinePassed(&d, ring->Host)) {
                    break;
                }
                continue;
            }
            if (f.Words < 3 + count) {
                ring->Counters.Malformed++;
                return TBT_E_HW;
            }
            for (k = 0; k < count; k++) {
                out[k] = f.Dw[3 + k];
            }
            return TBT_OK;
        }
        tbtLog(ring->Host, "tbt.ring.ctl-no-answer", want);
    }
    ring->Counters.CtlTimeouts++;
    return TBT_E_TIMEOUT;
}
