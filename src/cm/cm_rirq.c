/*
 * cm_rirq.c - the pure half of the NHI's ring interrupt (design record 21
 * section 4.5; task 45.22). See cm_rirq.h; the driver half is cm_rirqos.c.
 *
 * C89, DDK-free, no allocation, no lock. IRQL: any.
 */

#include "cm_rirq.h"

BOOLEAN CmRirqGeometry(ULONG paths, CM_RIRQ_GEOM *g)
{
    g->Paths = 0;
    g->Dwords = 0;
    g->CauseDw = 0;
    g->CauseBit = 0;
    g->DataRing = 0;
    g->DataDw = 0;
    g->DataBit = 0;
    if (paths == 0 || paths > TBT_HI_CAPS_PATHS_MAX) {
        return FALSE;
    }
    g->Paths = paths;
    g->Dwords = TBT_HI_IMR_DWORDS(paths);
    g->CauseDw = paths / 32UL;
    g->CauseBit = 1UL << (paths % 32UL);
    return TRUE;
}

BOOLEAN CmRirqGeomData(CM_RIRQ_GEOM *g, ULONG ring)
{
    ULONG bit;

    g->DataRing = 0;
    g->DataDw = 0;
    g->DataBit = 0;
    if (g->Paths == 0 || ring == 0 || ring >= g->Paths) {
        return FALSE;
    }
    bit = g->Paths + ring;
    g->DataRing = ring;
    g->DataDw = bit / 32UL;
    g->DataBit = 1UL << (bit % 32UL);
    return TRUE;
}

ULONG CmRirqCauses(const CM_RIRQ_GEOM *g, const ULONG *v)
{
    ULONG c;

    c = 0;
    if (g->CauseBit != 0 && g->CauseDw < g->Dwords &&
        (v[g->CauseDw] & g->CauseBit) != 0) {
        c |= CM_RIRQ_CAUSE_RING0;
    }
    if (g->DataBit != 0 && g->DataDw < g->Dwords &&
        (v[g->DataDw] & g->DataBit) != 0) {
        c |= CM_RIRQ_CAUSE_DATA;
    }
    return c;
}

ULONG CmRirqClassify(const CM_RIRQ_GEOM *g, const ULONG *v)
{
    if (v[0] == 0xFFFFFFFFUL) {
        return CM_RIRQ_ISR_ONES;
    }
    if (CmRirqCauses(g, v) == 0) {
        return CM_RIRQ_ISR_OTHER;
    }
    return CM_RIRQ_ISR_MINE;
}

VOID CmRirqBurstReset(CM_RIRQ_BURST *b, LONG seq)
{
    b->Burst = 0;
    b->Seq = seq;
}

BOOLEAN CmRirqBurstClaim(CM_RIRQ_BURST *b, LONG seq)
{
    if (b->Seq != seq) {
        b->Seq = seq;
        b->Burst = 0;
    }
    if (b->Burst < CM_RIRQ_STORM_CLAIMS) {
        b->Burst++;
    }
    return (BOOLEAN)(b->Burst >= CM_RIRQ_STORM_CLAIMS);
}

/* One DW's bits by the host's contract. */
static VOID cmRirqBitsWrite(ULONG dw, ULONG bits, BOOLEAN intel, BOOLEAN open,
                            ULONG imr, CM_RIRQ_WRITE *w)
{
    w->Write = FALSE;
    w->Offset = 0;
    w->Value = 0;
    if (bits == 0) {
        return;
    }
    if (intel) {
        if (imr == 0xFFFFFFFFUL) {
            return;
        }
        w->Write = TRUE;
        w->Offset = TBT_HI_IMR + 4UL * dw;
        w->Value = open ? (imr | bits) : (imr & ~bits);
        return;
    }
    w->Write = TRUE;
    w->Offset = (open ? TBT_HI_IMS : TBT_HI_IMC) + 4UL * dw;
    w->Value = bits;
}

static BOOLEAN cmRirqBitsProven(ULONG bits, BOOLEAN open, ULONG imr)
{
    if (imr == 0xFFFFFFFFUL || bits == 0) {
        return FALSE;
    }
    if (open) {
        return (BOOLEAN)((imr & bits) == bits);
    }
    return (BOOLEAN)((imr & bits) == 0);
}

VOID CmRirqMaskWrite(const CM_RIRQ_GEOM *g, BOOLEAN intel, BOOLEAN open,
                     ULONG imr, CM_RIRQ_WRITE *w)
{
    cmRirqBitsWrite(g->CauseDw, g->CauseBit, intel, open, imr, w);
}

BOOLEAN CmRirqMaskProven(const CM_RIRQ_GEOM *g, BOOLEAN open, ULONG imr)
{
    return cmRirqBitsProven(g->CauseBit, open, imr);
}

VOID CmRirqDataMaskWrite(const CM_RIRQ_GEOM *g, BOOLEAN intel, BOOLEAN open,
                         ULONG imr, CM_RIRQ_WRITE *w)
{
    cmRirqBitsWrite(g->DataDw, g->DataBit, intel, open, imr, w);
}

BOOLEAN CmRirqDataMaskProven(const CM_RIRQ_GEOM *g, BOOLEAN open, ULONG imr)
{
    return cmRirqBitsProven(g->DataBit, open, imr);
}

ULONG CmRirqMaskBoth(const CM_RIRQ_GEOM *g, BOOLEAN intel, ULONG imr0,
                     ULONG imrData, CM_RIRQ_WRITE *w)
{
    ULONG n;

    n = 0;
    if (g->DataBit != 0 && g->DataDw == g->CauseDw) {
        cmRirqBitsWrite(g->CauseDw, g->CauseBit | g->DataBit, intel, FALSE,
                        imr0, &w[0]);
        return w[0].Write ? 1UL : 0UL;
    }
    cmRirqBitsWrite(g->CauseDw, g->CauseBit, intel, FALSE, imr0, &w[n]);
    if (w[n].Write) {
        n++;
    }
    if (g->DataBit != 0) {
        cmRirqBitsWrite(g->DataDw, g->DataBit, intel, FALSE, imrData, &w[n]);
        if (w[n].Write) {
            n++;
        }
    }
    return n;
}

BOOLEAN CmRirqBothMasked(const CM_RIRQ_GEOM *g, ULONG imr0, ULONG imrData)
{
    if (!cmRirqBitsProven(g->CauseBit, FALSE, imr0)) {
        return FALSE;
    }
    if (g->DataBit == 0) {
        return TRUE;
    }
    return cmRirqBitsProven(g->DataBit, FALSE,
                            g->DataDw == g->CauseDw ? imr0 : imrData);
}

ULONG CmRirqAckWrites(const CM_RIRQ_GEOM *g, BOOLEAN autoClearOff,
                      const ULONG *v, CM_RIRQ_WRITE *w, ULONG max)
{
    ULONG i;
    ULONG n;

    n = 0;
    if (!autoClearOff) {
        return 0;
    }
    for (i = 0; i < g->Dwords && n < max; i++) {
        if (v[i] != 0 && v[i] != 0xFFFFFFFFUL) {
            w[n].Write = TRUE;
            w[n].Offset = TBT_HI_ISC + 4UL * i;
            w[n].Value = v[i];
            n++;
        }
    }
    return n;
}

VOID CmRirqWatchStart(CM_RIRQ_WATCH *w, ULONG claims, ULONG rx)
{
    w->ClaimsAtDrain = claims;
    w->Suspects = 0;
    w->StormClaims = claims;
    w->StormRx = rx;
}

VOID CmRirqWatchDataStart(CM_RIRQ_WATCH *w, BOOLEAN on, ULONG claims,
                          ULONG rx)
{
    w->DataOn = on ? 1UL : 0UL;
    w->DataClaims = claims;
    w->DataRx = rx;
}

BOOLEAN CmRirqWatchData(CM_RIRQ_WATCH *w, ULONG claims, ULONG rx)
{
    BOOLEAN storm;

    if (!w->DataOn) {
        return FALSE;
    }
    storm = (BOOLEAN)(claims - w->DataClaims > CM_RIRQ_SLOW_STORM &&
                      rx == w->DataRx);
    w->DataClaims = claims;
    w->DataRx = rx;
    return storm;
}

VOID CmRirqWatchDrained(CM_RIRQ_WATCH *w, ULONG claims)
{
    w->ClaimsAtDrain = claims;
}

ULONG CmRirqWatchPass(CM_RIRQ_WATCH *w, ULONG claims, ULONG rx,
                      BOOLEAN pending)
{
    BOOLEAN storm;

    storm = (BOOLEAN)(claims - w->StormClaims > CM_RIRQ_SLOW_STORM &&
                      rx == w->StormRx);
    w->StormClaims = claims;
    w->StormRx = rx;
    if (storm) {
        w->Suspects = 0;
        return CM_RIRQ_WATCH_STORM;
    }
    if (!pending || claims != w->ClaimsAtDrain) {
        w->Suspects = 0;
        return CM_RIRQ_WATCH_OK;
    }
    w->Suspects++;
    if (w->Suspects >= CM_RIRQ_SUSPECT_RUN) {
        w->Suspects = 0;
        return CM_RIRQ_WATCH_SELFTEST;
    }
    return CM_RIRQ_WATCH_OK;
}

ULONG CmRirqDownEnd(ULONG why, BOOLEAN retired)
{
    if (retired) {
        return CM_RIRQ_END_DISCONNECT;
    }
    if (why == CM_RIRQ_DOWN_FALLBACK || why == CM_RIRQ_DOWN_CONTAIN) {
        return CM_RIRQ_END_RETAIN;
    }
    return CM_RIRQ_END_UNPROVEN;
}

BOOLEAN CmRirqDownFails(ULONG why, ULONG end)
{
    return (BOOLEAN)(why == CM_RIRQ_DOWN_FALLBACK ||
                     end == CM_RIRQ_END_UNPROVEN);
}

BOOLEAN CmRirqWanted(ULONG value, BOOLEAN line, BOOLEAN failed)
{
    return (BOOLEAN)(value == CM_RIRQ_VALUE_LINE && line && !failed);
}

ULONG CmRirqLeftMs(ULONG now, ULONG due, ULONG limit)
{
    ULONG left;

    if ((LONG)(due - now) <= 0) {
        return 0;
    }
    left = due - now;
    return left < limit ? left : limit;
}

BOOLEAN CmRirqDue(ULONG now, ULONG due)
{
    return (BOOLEAN)((LONG)(due - now) <= 0);
}
