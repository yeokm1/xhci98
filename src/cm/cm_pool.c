/*
 * cm_pool.c - every pool call and every DMA call xhcicm.sys makes, in one
 * object, so the import allowlists can hold them to it (SITES=cm_pool.obj;
 * the HCD's rule of design record 13 section 7.5, as uas_mem.c applies it
 * to xhciuas.sys). Nonpaged pool only, at lifetime-scoped sites, never per
 * frame; nothing allocates pool yet (the NHI's state is its device
 * extension), so the linker drops CmAlloc and CmFree and the allowlists
 * carry the pool pair as optional rows.
 *
 * The ring block (record 21 section 4.2): one common buffer of
 * TBT_RING_DMA_BYTES, from the adapter IoGetDmaAdapter returns on the NHI's
 * PDO with the description hcd_dma.c passes (Windows 98 SE's own
 * openhci.sys and uhcd.sys pass it too: Master, ScatterGather,
 * Dma32BitAddresses, PCIBus), cache-enabled as AGENTS.md's DMA rule asks,
 * below 4 GB - checked, not assumed - and 16-byte aligned (tbt_ring.h).
 * Allocated once at a start whose selection said ICM, freed at the stop
 * after the client's rings are stopped, and kept (counted) if they cannot
 * be shown stopped.
 *
 * IRQL: CmAlloc and CmFree at <= DISPATCH_LEVEL; CmDmaOpen and CmDmaClose
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
    desc.MaximumLength = TBT_RING_DMA_BYTES;

    mapRegisters = 0;
    nhi->Dma = IoGetDmaAdapter(nhi->Common.Pdo, &desc, &mapRegisters);
    if (nhi->Dma == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    pa.LowPart = 0;
    pa.HighPart = 0;
    va = nhi->Dma->DmaOperations->AllocateCommonBuffer(
        nhi->Dma, TBT_RING_DMA_BYTES, &pa, TRUE);
    if (va != NULL &&
        (pa.HighPart != 0 || (pa.LowPart & (TBT_RING_DMA_ALIGN - 1)) != 0 ||
         ((ULONG_PTR)va & (TBT_RING_DMA_ALIGN - 1)) != 0)) {
        nhi->Dma->DmaOperations->FreeCommonBuffer(
            nhi->Dma, TBT_RING_DMA_BYTES, pa, va, TRUE);
        va = NULL;
    }
    if (va == NULL) {
        nhi->Dma->DmaOperations->PutDmaAdapter(nhi->Dma);
        nhi->Dma = NULL;
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    CmZero(va, TBT_RING_DMA_BYTES);
    nhi->CommonVa = va;
    nhi->CommonPa = pa;
    return STATUS_SUCCESS;
}

/* The block given back, unless the client's ring may still be running:
 * then a bus master may hold its addresses, and it stays allocated with
 * its adapter for the life of the system (hcd_dma.c's rule). */
VOID CmDmaClose(PCM_NHI nhi)
{
    if (nhi->Dma == NULL) {
        return;
    }
    if (nhi->CommonVa != NULL) {
        if (nhi->Ring.Running) {
            nhi->Counters.BuffersKept++;
            CmLogNote(nhi, "cm.buffer-kept", 1);
            nhi->CommonVa = NULL;
            nhi->Dma = NULL;
            return;
        }
        nhi->Dma->DmaOperations->FreeCommonBuffer(
            nhi->Dma, TBT_RING_DMA_BYTES, nhi->CommonPa, nhi->CommonVa, TRUE);
        nhi->CommonVa = NULL;
    }
    nhi->Dma->DmaOperations->PutDmaAdapter(nhi->Dma);
    nhi->Dma = NULL;
}
