/*
 * hcd_dma.c - the only IoGetDmaAdapter call and the only DMA_OPERATIONS calls
 * in xhci98.sys (design record 13 sections 7.5 and 11).
 *
 * One adapter per controller, from IoGetDmaAdapter on the PCI PDO with the
 * description Windows 98 SE's own openhci.sys and uhcd.sys pass: Master,
 * ScatterGather, Dma32BitAddresses, PCIBus. Every service goes through the
 * adapter's table; the legacy Hal* / Io*MapTransfer names have no Windows 98
 * precedent and are denied by the allowlist.
 *
 * The common buffer is design record 04's block, unchanged: one
 * AllocateCommonBuffer of XHCI_HC_RESOURCES_SIZE bytes, cache-enabled,
 * carved by xhci_mem.c (section 11.2). It is freed at stop and remove before
 * the adapter is put back - unless the controller could not be proven to have
 * stopped mastering (HcdSvcDmaNotStopped), in which case the block is kept
 * for the life of the system and counted, the HCD's replacement for the
 * miniport's UsbPortBugCheck.
 *
 * IRQL: PASSIVE_LEVEL (AllocateCommonBuffer and IoGetDmaAdapter require it).
 */

#include "hcd.h"

NTSTATUS HcdDmaOpen(PHCD_CONTROLLER hc)
{
    DEVICE_DESCRIPTION desc;
    ULONG mapRegisters;
    PHYSICAL_ADDRESS pa;
    PVOID va;
    PUCHAR p;
    ULONG i;

    p = (PUCHAR)&desc;
    for (i = 0; i < sizeof(desc); i++) {
        p[i] = 0;
    }
    desc.Version = DEVICE_DESCRIPTION_VERSION;
    desc.Master = TRUE;
    desc.ScatterGather = TRUE;
    desc.Dma32BitAddresses = TRUE;
    desc.InterfaceType = PCIBus;
    desc.MaximumLength = 0xFFFFFFFFUL;

    mapRegisters = 0;
    hc->Dma = IoGetDmaAdapter(hc->Pdo, &desc, &mapRegisters);
    if (hc->Dma == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    hc->MapRegisters = mapRegisters;

    va = hc->Dma->DmaOperations->AllocateCommonBuffer(
        hc->Dma, XHCI_HC_RESOURCES_SIZE, &pa, TRUE);
    if (va == NULL) {
        hc->Dma->DmaOperations->PutDmaAdapter(hc->Dma);
        hc->Dma = NULL;
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    hc->CommonVa = va;
    hc->CommonPa = pa;
    hc->CommonBytes = XHCI_HC_RESOURCES_SIZE;
    /* The pin names the block a bus master may still hold. A block kept by
     * an earlier stop stays kept; this new one starts unpinned (Codex review
     * of 26-A.2, round 1, finding 8). */
    hc->CommonBufferPinned = 0;

    /* The enumeration's scratch, where descriptors land (hcd_enum.c). Its
     * absence leaves the controller started with nothing enumerated, which
     * HcdEnumService checks for. */
    hc->ScratchVa = hc->Dma->DmaOperations->AllocateCommonBuffer(
        hc->Dma, HCD_SCRATCH_BYTES, &hc->ScratchPa, TRUE);
    return STATUS_SUCCESS;
}

VOID HcdDmaClose(PHCD_CONTROLLER hc)
{
    if (hc->Dma == NULL) {
        return;
    }
    if (hc->CommonVa != NULL) {
        if (hc->CommonBufferPinned) {
            /* A bus master that may still be running holds this block's
             * addresses; giving it back would let the next owner's data be
             * overwritten. It stays allocated, and so does the adapter. */
            hc->CommonBuffersKept++;
            hc->CommonVa = NULL;
            hc->ScratchVa = NULL;
            hc->Dma = NULL;
            return;
        }
        if (hc->ScratchVa != NULL) {
            hc->Dma->DmaOperations->FreeCommonBuffer(
                hc->Dma, HCD_SCRATCH_BYTES, hc->ScratchPa, hc->ScratchVa,
                TRUE);
            hc->ScratchVa = NULL;
        }
        hc->Dma->DmaOperations->FreeCommonBuffer(
            hc->Dma, hc->CommonBytes, hc->CommonPa, hc->CommonVa, TRUE);
        hc->CommonVa = NULL;
    }
    hc->Dma->DmaOperations->PutDmaAdapter(hc->Dma);
    hc->Dma = NULL;
}
