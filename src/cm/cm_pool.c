/*
 * cm_pool.c - every pool call and every DMA call xhcicm.sys makes, in one
 * object, so the import allowlists can hold them to it (SITES=cm_pool.obj;
 * the HCD's rule of design record 13 section 7.5, as uas_mem.c applies it
 * to xhciuas.sys). Nonpaged pool, at lifetime-scoped sites, never per
 * frame, but for CmAllocPaged's: the replacement lists whose IRPs' contracts
 * put them in paged pool, all three in cm_filt.c (the rewritten
 * BusQueryCompatibleIDs answer, design record 31 section 4.2; a child's
 * interrupt requirements; a bridge's requirements). The NHI's own state is
 * its device extension; the nonpaged users are the stor98 link-state table
 * and its thunks (cm_nvlinkos.c), the member call workers (cm_callwk.c), on
 * Windows 98 SE and ME the CONFIGMG thunks, worker and PCI requests
 * (cm_cfgmg.c, cm_cfgwk.c), the filters' bridge snapshot and stripped
 * start lists (cm_filt.c), the _DSD answer (cm_dsdos.c), the network PDO's
 * ids and relations (cm_net.c), the handover's tables, scan areas and
 * walks (cm_oscos.c, cm_oscnt.c), and the query device's names (freed in
 * cm_qryos.c), so the allowlists carry the pool pair as required rows.
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

/*
 * The nonpaged pool type: NonPagedPoolNx (512), Windows 8's no-execute
 * nonpaged pool, which the Windows 2000 DDK and WDK 7.1 predate, from
 * NT 6.2 on, so Driver Verifier's code integrity checks, and the memory
 * integrity they test for, find no executable pool (roadmap-usb4 task
 * 45.63); NonPagedPool, the only nonpaged type they have, on Windows 98,
 * ME and 2000 to 7. NT is told by IoIsWdmVersionAvailable(1, 0x10)
 * before the kernel's shared data page is read, as cm_filt.c reads it:
 * Windows 98 and ME have no such page.
 */
#define CM_POOL_NX ((POOL_TYPE)512)

static POOL_TYPE CmPoolNonPaged = NonPagedPool;

/* DriverEntry, before the first allocation. PASSIVE_LEVEL. */
VOID CmPoolInit(VOID)
{
    ULONG major;
    ULONG minor;

    if (!IoIsWdmVersionAvailable(1, 0x10)) {
        return;
    }
    major = SharedUserData->NtMajorVersion;
    minor = SharedUserData->NtMinorVersion;
    if (major > 6 || (major == 6 && minor >= 2)) {
        CmPoolNonPaged = CM_POOL_NX;
    }
}

PVOID CmAlloc(ULONG bytes)
{
    PVOID p;

    p = ExAllocatePoolWithTag(CmPoolNonPaged, bytes, CM_POOL_TAG);
    if (p != NULL) {
        CmZero(p, bytes);
    }
    return p;
}

/*
 * The paged allocations, cm_filt.c's three replacement lists: the
 * rewritten BusQueryCompatibleIDs answer (design record 31 section 4.2),
 * which IRP_MN_QUERY_ID's contract puts in paged pool, and the replacement
 * IO_RESOURCE_REQUIREMENTS_LIST of a child's interrupt and of a bridge,
 * which IRP_MN_FILTER_RESOURCE_REQUIREMENTS' contract puts there; the PnP
 * manager frees each with ExFreePool. Not zeroed: the caller writes every
 * byte. PASSIVE_LEVEL.
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

/*
 * The Thunderbolt networking rings' common buffers (design record 35
 * section 5.3; cm_net.c): each descriptor block and each 4 KiB buffer its
 * own allocation on the ring block's adapter, cached, below 4 GB and 16-byte
 * aligned as the ring block is. Each is asked for in whole pages: Windows
 * 2000 served a common buffer of less than a page from pool, 8-byte
 * aligned (a 2 KiB descriptor block came back at an address ending 008h,
 * run-45 "45.59 (legs, continued)"; XP's refusal there is taken to be the
 * same), which the alignment check
 * refuses, and a start that met one had no service. The free takes the
 * same rounded length.
 * PASSIVE_LEVEL, on the thread.
 */
static ULONG cmDmaCommonBytes(ULONG bytes)
{
    return (bytes + (PAGE_SIZE - 1)) & ~(ULONG)(PAGE_SIZE - 1);
}

PVOID CmDmaCommonAlloc(PCM_NHI nhi, ULONG bytes, PULONG pa)
{
    PHYSICAL_ADDRESS phys;
    PVOID va;

    *pa = 0;
    if (nhi->Dma == NULL || bytes == 0) {
        return NULL;
    }
    bytes = cmDmaCommonBytes(bytes);
    phys.LowPart = 0;
    phys.HighPart = 0;
    va = nhi->Dma->DmaOperations->AllocateCommonBuffer(nhi->Dma, bytes, &phys, TRUE);
    if (va != NULL &&
        (phys.HighPart != 0 || (phys.LowPart & (TBT_RING_DMA_ALIGN - 1)) != 0 ||
         ((ULONG_PTR)va & (TBT_RING_DMA_ALIGN - 1)) != 0)) {
        nhi->Dma->DmaOperations->FreeCommonBuffer(nhi->Dma, bytes, phys, va, TRUE);
        va = NULL;
    }
    if (va == NULL) {
        return NULL;
    }
    CmZero(va, bytes);
    *pa = phys.LowPart;
    return va;
}

VOID CmDmaCommonFree(PCM_NHI nhi, ULONG bytes, ULONG pa, PVOID va)
{
    PHYSICAL_ADDRESS phys;

    if (nhi->Dma == NULL || va == NULL) {
        return;
    }
    bytes = cmDmaCommonBytes(bytes);
    phys.LowPart = pa;
    phys.HighPart = 0;
    nhi->Dma->DmaOperations->FreeCommonBuffer(nhi->Dma, bytes, phys, va, TRUE);
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
            nhi->Net.MemKept = 0;
            return;
        }
        nhi->Dma->DmaOperations->FreeCommonBuffer(
            nhi->Dma, nhi->DmaBytes, nhi->CommonPa, nhi->CommonVa, TRUE);
        nhi->CommonVa = NULL;
    }
    if (nhi->Net.MemKept) {
        /* The networking rings' memory kept (CmNetStopWork): its adapter
         * stays with it until the next boot, as the block's would. */
        nhi->Net.MemKept = 0;
        nhi->Dma = NULL;
        return;
    }
    nhi->Dma->DmaOperations->PutDmaAdapter(nhi->Dma);
    nhi->Dma = NULL;
}
