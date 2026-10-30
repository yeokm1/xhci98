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
    if (paths == 0 || paths > TBT_HI_CAPS_PATHS_MAX) {
        return FALSE;
    }
    g->Paths = paths;
    g->Dwords = TBT_HI_IMR_DWORDS(paths);
    g->CauseDw = paths / 32UL;
    g->CauseBit = 1UL << (paths % 32UL);
    return TRUE;
}

ULONG CmRirqClassify(const CM_RIRQ_GEOM *g, ULONG first, ULONG causeDw)
{
    if (first == 0xFFFFFFFFUL) {
        return CM_RIRQ_ISR_ONES;
    }
    if ((causeDw & g->CauseBit) == 0) {
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

VOID CmRirqMaskWrite(const CM_RIRQ_GEOM *g, BOOLEAN intel, BOOLEAN open,
                     ULONG imr, CM_RIRQ_WRITE *w)
{
    w->Write = FALSE;
    w->Offset = 0;
    w->Value = 0;
    if (g->CauseBit == 0) {
        return;
    }
    if (intel) {
        if (imr == 0xFFFFFFFFUL) {
            return;
        }
        w->Write = TRUE;
        w->Offset = TBT_HI_IMR + 4UL * g->CauseDw;
        w->Value = open ? (imr | g->CauseBit) : (imr & ~g->CauseBit);
        return;
    }
    w->Write = TRUE;
    w->Offset = (open ? TBT_HI_IMS : TBT_HI_IMC) + 4UL * g->CauseDw;
    w->Value = g->CauseBit;
}

BOOLEAN CmRirqMaskProven(const CM_RIRQ_GEOM *g, BOOLEAN open, ULONG imr)
{
    if (imr == 0xFFFFFFFFUL || g->CauseBit == 0) {
        return FALSE;
    }
    if (open) {
        return (BOOLEAN)((imr & g->CauseBit) != 0);
    }
    return (BOOLEAN)((imr & g->CauseBit) == 0);
}

VOID CmRirqWatchStart(CM_RIRQ_WATCH *w, ULONG claims, ULONG rx)
{
    w->ClaimsAtDrain = claims;
    w->Suspects = 0;
    w->StormClaims = claims;
    w->StormRx = rx;
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

BOOLEAN CmRirqWanted(ULONG value, BOOLEAN nt, BOOLEAN line, BOOLEAN failed)
{
    return (BOOLEAN)(value == CM_RIRQ_VALUE_LINE && nt && line && !failed);
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
