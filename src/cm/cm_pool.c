/*
 * cm_pool.c - every pool call and every DMA call xhcicm.sys makes, in one
 * object, so the import allowlists can hold them to it (SITES=cm_pool.obj;
 * the HCD's rule of design record 13 section 7.5, as uas_mem.c applies it
 * to xhciuas.sys). Nonpaged pool only, at lifetime-scoped sites, never per
 * frame, but for one paged allocation, CmAllocPaged's (design record 31
 * section 4.2). The NHI's own state is its device extension; what is allocated
 * is the stor98 link-state table (cm_nvlinkos.c), on Windows 98 SE and ME
 * the CONFIGMG thunks and worker (cm_cfgmg.c, cm_cfgwk.c), and the query
 * device's names (freed in cm_qryos.c), so the allowlists carry the pool
 * pair as required rows.
 *
 * The ring block (record 21 section 4.2): one common buffer of
 * CM_NHI.DmaBytes - TBT_RING_DMA_BYTES for the ICM client's ten-entry
 * rings, TBT_RING_U4_DMA_BYTES for the USB4 profile's sixteen (record 22
 * section 4.4 step 3) - from the adapter IoGetDmaAdapter returns on the NHI's
 * PDO with the description hcd_dma.c passes (Windows 98 SE's own
 * openhci.sys and uhcd.sys pass it too: Master, ScatterGather,
 * Dma32BitAddresses, PCIBus), cache-enabled as AGENTS.md's DMA rule asks,
 * below 4 GB - checked, not assumed - and 16-byte aligned (tbt_ring.h).
 * Allocated once at a start whose selection said ICM or admitted the USB4
 * software connection manager, freed at the stop
 * after the client's rings are stopped, and kept (counted) if they cannot
 * be shown stopped.
 *
 * IRQL: CmAlloc and CmFree at <= DISPATCH_LEVEL (CmFree of a paged block
 * at PASSIVE_LEVEL); CmAllocPaged at PASSIVE_LEVEL; CmDmaOpen and CmDmaClose
 * at PASSIVE_LEVEL (IoGetDmaAdapter and AllocateCommonBuffer require it),
 * on the connection-manager thread.
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

/*
 * The one paged allocation (design record 31 section 4.2): the rewritten
 * BusQueryCompatibleIDs answer, which IRP_MN_QUERY_ID's contract puts in
 * paged pool and the PnP manager frees with ExFreePool. Not zeroed: the
 * caller writes every byte. PASSIVE_LEVEL.
 */
PVOID CmAllocPaged(ULONG bytes)
{
    return ExAllocatePoolWithTag(PagedPool, bytes, CM_POOL_TAG);
}

VOID CmFree(PVOID p)
{
    if (p != NULL) {
        ExFreePool(p);
    }
}

NTSTATUS CmDmaOpen(PCM_NHI nhi)
{
    DEVICE_DESCRIPTION desc;
    ULONG mapRegisters;
    PHYSICAL_ADDRESS pa;
    PVOID va;

    CmZero(&desc, sizeof(desc));
    desc.Version = DEVICE_DESCRIPTION_VERSION;
    desc.Master = TRUE;
    desc.ScatterGather = TRUE;
    desc.Dma32BitAddresses = TRUE;
    desc.InterfaceType = PCIBus;
    if (nhi->DmaBytes == 0) {
        nhi->DmaBytes = TBT_RING_DMA_BYTES;
    }
    desc.MaximumLength = nhi->DmaBytes;

    mapRegisters = 0;
    nhi->Dma = IoGetDmaAdapter(nhi->Common.Pdo, &desc, &mapRegisters);
    if (nhi->Dma == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    pa.LowPart = 0;
    pa.HighPart = 0;
    va = nhi->Dma->DmaOperations->AllocateCommonBuffer(
        nhi->Dma, nhi->DmaBytes, &pa, TRUE);
    if (va != NULL &&
        (pa.HighPart != 0 || (pa.LowPart & (TBT_RING_DMA_ALIGN - 1)) != 0 ||
         ((ULONG_PTR)va & (TBT_RING_DMA_ALIGN - 1)) != 0)) {
        nhi->Dma->DmaOperations->FreeCommonBuffer(
            nhi->Dma, nhi->DmaBytes, pa, va, TRUE);
        va = NULL;
    }
    if (va == NULL) {
        nhi->Dma->DmaOperations->PutDmaAdapter(nhi->Dma);
        nhi->Dma = NULL;
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    CmZero(va, nhi->DmaBytes);
    nhi->CommonVa = va;
    nhi->CommonPa = pa;
    return STATUS_SUCCESS;
}

/* The block given back, unless the host interface may still DMA into it:
 * a ring enabled and no verified stop since (TBT_RING.DmaMayRun - a
 * partial start counts, and an unanswered or refused read-back never
 * clears it). Then a bus master may hold its addresses, and it stays
 * allocated with its adapter until the next boot (hcd_dma.c's rule). */
VOID CmDmaClose(PCM_NHI nhi)
{
    if (nhi->Dma == NULL) {
        return;
    }
    if (nhi->CommonVa != NULL) {
        if (nhi->Ring.Running || nhi->Ring.DmaMayRun) {
            nhi->Counters.BuffersKept++;
            CmLogNote(nhi, "cm.buffer-kept", 1);
            nhi->CommonVa = NULL;
            nhi->Dma = NULL;
            return;
        }
        nhi->Dma->DmaOperations->FreeCommonBuffer(
            nhi->Dma, nhi->DmaBytes, nhi->CommonPa, nhi->CommonVa, TRUE);
        nhi->CommonVa = NULL;
    }
    nhi->Dma->DmaOperations->PutDmaAdapter(nhi->Dma);
    nhi->Dma = NULL;
}
