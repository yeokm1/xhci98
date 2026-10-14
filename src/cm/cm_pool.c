/*
 * cm_pool.c - every pool call xhcicm.sys makes, in one object, so the
 * import allowlists can hold the pair to it (SITES=cm_pool.obj; the HCD's
 * rule of design record 13 section 7.5, as uas_mem.c applies it to
 * xhciuas.sys). Nonpaged pool only, at lifetime-scoped sites, never per
 * frame. The scaffold allocates nothing, so the linker drops both
 * functions and the allowlists carry the pair as optional rows; task
 * 39-A.4 adds the ring block's common buffer here too (record 21 section
 * 4.2), through the adapter IoGetDmaAdapter returns.
 *
 * IRQL: <= DISPATCH_LEVEL.
 */

#include "cm.h"

PVOID CmAlloc(ULONG bytes)
{
    PVOID p;

    p = ExAllocatePoolWithTag(NonPagedPool, bytes, CM_POOL_TAG);
    if (p != NULL) {
        CmZero(p, bytes);
    }
    return p;
}

VOID CmFree(PVOID p)
{
    if (p != NULL) {
        ExFreePool(p);
    }
}
