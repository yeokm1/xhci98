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

ULONG CmRirqHow(ULONG value, ULONG sys, BOOLEAN yield, BOOLEAN line,
                BOOLEAN failed)
{
    if (failed) {
        return CM_RIRQ_HOW_NONE;
    }
    if (value == CM_RIRQ_VALUE_MESSAGE && sys == CM_RIRQ_SYS_9X) {
        return CM_RIRQ_HOW_NONE;
    }
    if (value == CM_RIRQ_VALUE_MESSAGE && sys == CM_RIRQ_SYS_NT5 && !yield) {
        return CM_RIRQ_HOW_MESSAGE;
    }
    if ((value == CM_RIRQ_VALUE_LINE || value == CM_RIRQ_VALUE_MESSAGE) &&
        line) {
        return CM_RIRQ_HOW_LINE;
    }
    return CM_RIRQ_HOW_NONE;
}

ULONG CmRirqMsgEnd(ULONG why, BOOLEAN shutdown, BOOLEAN gone)
{
    if (why == CM_RIRQ_DOWN_D3 && !shutdown) {
        return CM_RIRQ_MSG_KEEP;
    }
    if (gone) {
        return CM_RIRQ_MSG_GONE;
    }
    if (why == CM_RIRQ_DOWN_STOP || why == CM_RIRQ_DOWN_D3) {
        return CM_RIRQ_MSG_FORCE;
    }
    return CM_RIRQ_MSG_HOLD;
}

BOOLEAN CmRirqMsgRefuse(ULONG end, BOOLEAN shutdown)
{
    return (BOOLEAN)(end == CM_RIRQ_MSG_FORCE && !shutdown);
}

BOOLEAN CmRirqMsgFails(ULONG why, ULONG end)
{
    return (BOOLEAN)(why == CM_RIRQ_DOWN_FALLBACK || end == CM_RIRQ_MSG_HOLD ||
                     end == CM_RIRQ_MSG_FORCE);
}

ULONG CmRirqMsgWhere(ULONG found, ULONG at, BOOLEAN busKnown, ULONG bus)
{
    if (found != 1 || at > 0xFFFFUL) {
        return CM_RIRQ_NO_BDF;
    }
    if (busKnown && ((at >> 8) & 0xFFUL) != bus) {
        return CM_RIRQ_NO_BDF;
    }
    return at;
}

ULONG CmRirqWhyOfStatus(ULONG status)
{
    switch (status) {
    case 1UL:   /* VERSION */
        return CM_Q_IRQ_WHY_OLD;
    case 2UL:   /* NO_MSI_CAP */
        return CM_Q_IRQ_WHY_NO_MSI;
    case 4UL:   /* NO_VECTORS */
    case 14UL:  /* msi4nt's NO_INPUTS: no free ISA IRQ */
        return CM_Q_IRQ_WHY_NO_VECTOR;
    case 6UL:   /* IN_USE */
        return CM_Q_IRQ_WHY_IN_USE;
    case 12UL:  /* msi4nt's CLAIM_KEPT */
        return CM_Q_IRQ_WHY_REFUSED;
    case 13UL:  /* BRAKED */
        return CM_Q_IRQ_WHY_BRAKED;
    default:
        /* CONFIG_ACCESS (10) among them: msi4nt version 1 also returns
         * it for a malformed capability list, so not "in use" (the
         * owner, via the coordinator, 2026-11-12). */
        return CM_Q_IRQ_WHY_CONNECT;
    }
}

BOOLEAN CmRirqIsrAdmits(BOOLEAN connected, BOOLEAN message, BOOLEAN reach,
                        ULONG gate, BOOLEAN bar, ULONG dwords)
{
    return (BOOLEAN)(connected && gate == 0 && bar && dwords != 0 &&
                     (!message || reach));
}

ULONG CmRirqGateClose(ULONG gate, ULONG why)
{
    return gate | (why & (CM_RIRQ_GATE_POWER | CM_RIRQ_GATE_GONE));
}

ULONG CmRirqGateOpenPower(ULONG gate)
{
    return gate & ~CM_RIRQ_GATE_POWER;
}

BOOLEAN CmRirqGateMayOpen(BOOLEAN d0, BOOLEAN bar, BOOLEAN suspended,
                          BOOLEAN working, BOOLEAN revoked)
{
    return (BOOLEAN)(d0 && bar && !suspended && working && !revoked);
}

ULONG CmRirqGateAtStart(BOOLEAN d0, BOOLEAN bar)
{
    return (d0 && bar) ? 0UL : CM_RIRQ_GATE_POWER;
}

ULONG CmRirqShowWord(ULONG value, BOOLEAN refused, ULONG sys, BOOLEAN yield,
                     BOOLEAN line, ULONG mode, ULONG why, ULONG number,
                     BOOLEAN isIrq)
{
    ULONG w;

    w = CM_Q_IRQ_PRESENT;
    if (mode == CM_RIRQ_MODE_LINE) {
        /* cm_qnet.h's CM_QN_KIND_LINE, or _LINE_YIELD (3). */
        return w | (yield ? 3UL : 1UL);
    }
    if (mode == CM_RIRQ_MODE_MESSAGE) {
        return w | 2UL | ((number & 0xFFUL) << 16) |
               (isIrq ? CM_Q_IRQ_IS_IRQ : 0UL);
    }
    if (why == CM_Q_IRQ_WHY_NONE) {
        if (refused) {
            why = CM_Q_IRQ_WHY_VALUE;
        } else if (value == CM_RIRQ_VALUE_OFF) {
            why = CM_Q_IRQ_WHY_OFF;
        } else if (value == CM_RIRQ_VALUE_MESSAGE && sys == CM_RIRQ_SYS_9X) {
            why = CM_Q_IRQ_WHY_NOT_9X;
        } else if (CmRirqHow(value, sys, yield, line, FALSE) ==
                   CM_RIRQ_HOW_NONE) {
            why = sys == CM_RIRQ_SYS_9X ? CM_Q_IRQ_WHY_NO_LINE_9X
                                        : CM_Q_IRQ_WHY_NO_LINE;
        } else {
            why = CM_Q_IRQ_WHY_WAIT;
        }
    }
    return w | ((why & 0xFFUL) << 8);
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
