/*
 * cm_netr.c - the Thunderbolt networking service's data rings, their pure
 * half (design record 35 sections 5.3 and 6.3; task 45.59 step S5). See
 * cm_netr.h; the driver half is cm_net.c.
 *
 * C89, DDK-free, no allocation, no lock. IRQL: any.
 */

#include "cm_netr.h"

static VOID cmNetrStore(volatile UCHAR *p, ULONG v)
{
    p[0] = (UCHAR)(v & 0xFFUL);
    p[1] = (UCHAR)((v >> 8) & 0xFFUL);
    p[2] = (UCHAR)((v >> 16) & 0xFFUL);
    p[3] = (UCHAR)((v >> 24) & 0xFFUL);
}

static ULONG cmNetrLoad(const volatile UCHAR *p)
{
    return (ULONG)p[0] | ((ULONG)p[1] << 8) | ((ULONG)p[2] << 16) |
           ((ULONG)p[3] << 24);
}

static volatile UCHAR *cmNetrDesc(const CM_NETR *r, ULONG i)
{
    return r->Desc + TBT_DESC_BYTES * (i % r->Size);
}

VOID CmNetrInit(CM_NETR *r, volatile UCHAR *desc, ULONG size, const ULONG *bufPa)
{
    ULONG i;
    volatile UCHAR *d;

    r->Size = size;
    r->Desc = desc;
    r->Head = 0;
    r->Tail = 0;
    for (i = 0; i < size; i++) {
        d = cmNetrDesc(r, i);
        cmNetrStore(d + TBT_DESC_ADDR_LO, bufPa[i]);
        cmNetrStore(d + TBT_DESC_ADDR_HI, 0);
        cmNetrStore(d + TBT_DESC_DW2, 0);
        cmNetrStore(d + TBT_DESC_DW3, 0);
    }
}

VOID CmNetrRxRepost(CM_NETR *r, ULONG i, BOOLEAN ie)
{
    cmNetrStore(cmNetrDesc(r, i) + TBT_DESC_DW2,
                TBT_DESC_RS | (ie ? TBT_DESC_IE : 0UL));
}

VOID CmNetrRxPostAll(CM_NETR *r, BOOLEAN ie)
{
    ULONG i;

    for (i = 0; i < r->Size; i++) {
        CmNetrRxRepost(r, i, ie);
    }
}

ULONG CmNetrRxConsumer(const CM_NETR *r)
{
    return (r->Head + r->Size - 1UL) % r->Size;
}

ULONG CmNetrRxTake(const CM_NETR *r, ULONG i, PULONG length, PULONG flags)
{
    ULONG dw2;
    ULONG len;

    dw2 = cmNetrLoad(cmNetrDesc(r, i) + TBT_DESC_DW2);
    *length = 0;
    *flags = 0;
    if ((dw2 & TBT_DESC_DD) == 0) {
        return CM_NETR_RX_NONE;
    }
    len = dw2 & TBT_DESC_LEN_MASK;
    *length = len == 0 ? CM_NETR_BUFFER_BYTES : len;
    if ((dw2 & TBT_DESC_RX_CRC_ERR) != 0) {
        *flags |= CM_NETR_FLAG_CRC;
    }
    if ((dw2 & TBT_DESC_RX_BO) != 0) {
        *flags |= CM_NETR_FLAG_OVERFLOW;
    }
    if (((dw2 >> TBT_DESC_EOF_SHIFT) & TBT_DESC_PDF_MASK) != TBT_NET_PDF_EOF) {
        return CM_NETR_RX_BAD_PDF;
    }
    return CM_NETR_RX_FRAME;
}

ULONG CmNetrTxUsed(const CM_NETR *r)
{
    return (r->Head + r->Size - r->Tail) % r->Size;
}

ULONG CmNetrTxFree(const CM_NETR *r)
{
    return r->Size - 1UL - CmNetrTxUsed(r);
}

VOID CmNetrTxPost(CM_NETR *r, ULONG i, ULONG length)
{
    cmNetrStore(cmNetrDesc(r, i) + TBT_DESC_DW2,
                (length & TBT_DESC_LEN_MASK) |
                    (TBT_NET_PDF_EOF << TBT_DESC_EOF_SHIFT) |
                    (TBT_NET_PDF_SOF << TBT_DESC_SOF_SHIFT) | TBT_DESC_RS);
}

ULONG CmNetrTxReap(CM_NETR *r)
{
    ULONG n;
    ULONG dw2;

    n = 0;
    while (r->Tail != r->Head) {
        dw2 = cmNetrLoad(cmNetrDesc(r, r->Tail) + TBT_DESC_DW2);
        if ((dw2 & TBT_DESC_DD) == 0) {
            break;
        }
        r->Tail = (r->Tail + 1UL) % r->Size;
        n++;
    }
    return n;
}

ULONG CmNetrReserve(const CM_NETR *tx, CM_NETR_RES *res, ULONG gen, ULONG count,
                    PULONG first)
{
    *first = 0;
    if (res->Held || count == 0 || count > CM_NETR_MAX_RESERVE || tx->Size == 0 ||
        CmNetrTxFree(tx) < count) {
        return 0;
    }
    res->Held = TRUE;
    res->Generation = gen;
    res->First = tx->Head;
    res->Count = count;
    *first = tx->Head;
    return count;
}

ULONG CmNetrRelease(CM_NETR_RES *res, ULONG gen, ULONG tokGen, ULONG tokFirst,
                    ULONG tokCount)
{
    if (!res->Held || res->Generation != tokGen || res->First != tokFirst ||
        res->Count != tokCount) {
        return CM_NETR_REL_NONE;
    }
    res->Held = FALSE;
    return res->Generation == gen ? CM_NETR_REL_POST : CM_NETR_REL_STALE;
}
