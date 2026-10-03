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
    /* The controller is programmed with the low dword alone (StartPA). The
     * 32-bit adapter keeps the block below 4 GB, which only an amd64 or PAE
     * machine could put to the test; checked rather than assumed (task
     * 28-A.2). */
    if (va != NULL && pa.HighPart != 0) {
        hc->Dma->DmaOperations->FreeCommonBuffer(
            hc->Dma, XHCI_HC_RESOURCES_SIZE, pa, va, TRUE);
        va = NULL;
    }
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
    if (hc->ScratchVa != NULL && hc->ScratchPa.HighPart != 0) {
        hc->Dma->DmaOperations->FreeCommonBuffer(
            hc->Dma, HCD_SCRATCH_BYTES, hc->ScratchPa, hc->ScratchVa, TRUE);
        hc->ScratchVa = NULL;
    }
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

/* ----------------------------------------------------------------------- */
/* The map pump (26-A.5; design record 13 section 11.3)                     */
/* ----------------------------------------------------------------------- */

/*
 * Every URB buffer reaches the controller through the adapter's map
 * registers: AllocateAdapterChannel for the pages a chunk spans, MapTransfer
 * per physically contiguous run in the execution routine, and at completion
 * FlushAdapterBuffers then FreeMapRegisters (HcdDmaUnmap). The pump runs one
 * channel request at a time - the request borrows the controller FDO's own
 * wait block, so a second one cannot be outstanding - and the next is
 * started from MapDpc. Whether Windows 98 runs the execution routine inside
 * the call or later is section 11.4's open reading: MapsSynchronous and
 * MapsDeferred count it. MapLock is never held across a DMA_OPERATIONS
 * call. IRQL: <= DISPATCH_LEVEL unless stated.
 */

static IO_ALLOCATION_ACTION NTAPI hcdMapExecute(PDEVICE_OBJECT DeviceObject,
                                                PIRP Irp,
                                                PVOID MapRegisterBase,
                                                PVOID Context);

static PUCHAR hcdXferVa(PHCD_XFER x)
{
    return (PUCHAR)MmGetMdlVirtualAddress(x->Mdl) + x->Offset;
}

/* Start the next queued record's channel request if none is outstanding.
 * IRQL: <= DISPATCH_LEVEL. */
static VOID hcdMapStart(PHCD_CONTROLLER hc)
{
    PLIST_ENTRY entry;
    PHCD_XFER x;
    KIRQL oldIrql;
    NTSTATUS status;

    KeAcquireSpinLock(&hc->MapLock, &oldIrql);
    if (hc->MapBusy || IsListEmpty(&hc->MapQueue)) {
        KeReleaseSpinLock(&hc->MapLock, oldIrql);
        return;
    }
    entry = RemoveHeadList(&hc->MapQueue);
    hc->MapBusy = 1;
    hc->MapInCall = 1;
    KeReleaseSpinLock(&hc->MapLock, oldIrql);

    x = CONTAINING_RECORD(entry, HCD_XFER, Link);
    KeRaiseIrql(DISPATCH_LEVEL, &oldIrql);
    status = hc->Dma->DmaOperations->AllocateAdapterChannel(
        hc->Dma, hc->Common.Self, x->MapCount, hcdMapExecute, x);
    hc->MapInCall = 0;
    KeLowerIrql(oldIrql);
    if (!NT_SUCCESS(status)) {
        hc->MapRefusals++;
        KeAcquireSpinLock(&hc->MapLock, &oldIrql);
        hc->MapBusy = 0;
        KeReleaseSpinLock(&hc->MapLock, oldIrql);
        x->MapBase = NULL;
        x->Status = HCD_USBD_NO_MEMORY;
        HcdIoMapped(hc, x, 0);
        HcdDmaMapKick(hc);
    }
}

/*
 * The channel's release, queued only by the execution routine: a DPC queued
 * on this processor from DISPATCH_LEVEL runs after the routine has
 * returned, so MapBusy is cleared only then, and no restart queued by
 * anything else can clear it early (Codex review of batch (c), round 3,
 * finding 4).
 */
static VOID NTAPI hcdMapDoneDpc(PKDPC Dpc, PVOID Context, PVOID Arg1,
                                PVOID Arg2)
{
    PHCD_CONTROLLER hc;
    KIRQL oldIrql;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    hc = (PHCD_CONTROLLER)Context;
    KeAcquireSpinLock(&hc->MapLock, &oldIrql);
    hc->MapBusy = 0;
    KeReleaseSpinLock(&hc->MapLock, oldIrql);
    hcdMapStart(hc);
    (VOID)InterlockedDecrement(&hc->MapDpcsInFlight);
}

static VOID NTAPI hcdMapDpc(PKDPC Dpc, PVOID Context, PVOID Arg1, PVOID Arg2)
{
    PHCD_CONTROLLER hc;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    hc = (PHCD_CONTROLLER)Context;
    hcdMapStart(hc);
    (VOID)InterlockedDecrement(&hc->MapDpcsInFlight);
}

/* Restart the pump from its DPC, counted at queue time as the event DPC is
 * (the stop waits the count out). IRQL: <= DISPATCH_LEVEL. */
VOID HcdDmaMapKick(PHCD_CONTROLLER hc)
{
    (VOID)InterlockedIncrement(&hc->MapDpcsInFlight);
    if (!KeInsertQueueDpc(&hc->MapDpc, NULL, NULL)) {
        (VOID)InterlockedDecrement(&hc->MapDpcsInFlight);
    }
}

/*
 * The execution routine: map the chunk run by run, each run cut at page
 * bounds so no element crosses a 64 KB line (spec 6.4.1), and hand the
 * record on to be published. The registers are kept until HcdDmaUnmap.
 * IRQL: DISPATCH_LEVEL.
 */
static IO_ALLOCATION_ACTION NTAPI hcdMapExecute(PDEVICE_OBJECT DeviceObject,
                                                PIRP Irp,
                                                PVOID MapRegisterBase,
                                                PVOID Context)
{
    PHCD_CONTROLLER hc;
    PHCD_XFER x;
    PHYSICAL_ADDRESS pa;
    PUCHAR va;
    ULONG done;
    ULONG run;
    ULONG piece;
    ULONG n;
    ULONG ok;

    UNREFERENCED_PARAMETER(Irp);
    hc = (PHCD_CONTROLLER)DeviceObject->DeviceExtension;
    x = (PHCD_XFER)Context;
    if (hc->MapInCall) {
        hc->MapsSynchronous++;
    } else {
        hc->MapsDeferred++;
    }
    x->MapBase = MapRegisterBase;
    va = hcdXferVa(x);
    n = 0;
    done = 0;
    ok = 1;
    while (done < x->Chunk && ok) {
        run = x->Chunk - done;
        pa = hc->Dma->DmaOperations->MapTransfer(hc->Dma, x->Mdl,
                                                 MapRegisterBase, va + done,
                                                 &run, (BOOLEAN)!x->In);
        if (run == 0 || pa.HighPart != 0) {
            ok = 0;
            break;
        }
        while (run > 0) {
            piece = PAGE_SIZE - (pa.LowPart & (PAGE_SIZE - 1));
            if (piece > run) {
                piece = run;
            }
            if (n >= HCD_SG_ELEMENTS) {
                ok = 0;
                break;
            }
            x->Sg.List.SgElement[n].SgPhysicalAddressLo = pa.LowPart;
            x->Sg.List.SgElement[n].SgPhysicalAddressHi = 0;
            x->Sg.List.SgElement[n].SgTransferLength = piece;
            x->Sg.List.SgElement[n].SgOffset = done;
            n++;
            pa.LowPart += piece;
            done += piece;
            run -= piece;
        }
    }
    x->Sg.List.SgElementCount = n;
    if (!ok) {
        x->Status = HCD_USBD_INTERNAL_HC_ERROR;
    }
    HcdIoMapped(hc, x, ok);

    /*
     * The channel is not free until this routine has returned (the
     * AllocateAdapterChannel contract): MapBusy is cleared by the pump's
     * DPC, queued on this processor and so unable to run before this
     * DISPATCH_LEVEL code has left, and counted before anything is cleared
     * so the stop's drain cannot see an idle pump meanwhile (Codex review
     * of batch (c), round 2, finding 4).
     */
    (VOID)InterlockedIncrement(&hc->MapDpcsInFlight);
    if (!KeInsertQueueDpc(&hc->MapDoneDpc, NULL, NULL)) {
        (VOID)InterlockedDecrement(&hc->MapDpcsInFlight);
    }
    return DeallocateObjectKeepRegisters;
}

/* Queue a record whose Mdl, Offset, Chunk and MapCount are set, and start
 * the pump. IRQL: <= DISPATCH_LEVEL, no lock held. */
VOID HcdDmaMapQueue(PHCD_CONTROLLER hc, PHCD_XFER x)
{
    KIRQL oldIrql;

    x->MapBase = NULL;
    KeAcquireSpinLock(&hc->MapLock, &oldIrql);
    InsertTailList(&hc->MapQueue, &x->Link);
    KeReleaseSpinLock(&hc->MapLock, oldIrql);
    hcdMapStart(hc);
}

/* A chunk's end: flush, then give the map registers back. IRQL:
 * <= DISPATCH_LEVEL (raised for the call), no lock held. */
VOID HcdDmaUnmap(PHCD_CONTROLLER hc, PHCD_XFER x)
{
    KIRQL oldIrql;

    if (x->MapBase == NULL) {
        return;
    }
    KeRaiseIrql(DISPATCH_LEVEL, &oldIrql);
    (VOID)hc->Dma->DmaOperations->FlushAdapterBuffers(
        hc->Dma, x->Mdl, x->MapBase, hcdXferVa(x), x->Chunk,
        (BOOLEAN)!x->In);
    hc->Dma->DmaOperations->FreeMapRegisters(hc->Dma, x->MapBase,
                                             x->MapCount);
    KeLowerIrql(oldIrql);
    x->MapBase = NULL;
}

/* The pump's objects, once, beside the controller's others. IRQL:
 * PASSIVE_LEVEL. */
VOID HcdDmaInitObjects(PHCD_CONTROLLER hc)
{
    KeInitializeSpinLock(&hc->MapLock);
    InitializeListHead(&hc->MapQueue);
    InitializeListHead(&hc->DoneList);
    InitializeListHead(&hc->SlowIrps);
    KeInitializeDpc(&hc->MapDpc, hcdMapDpc, hc);
    KeInitializeDpc(&hc->MapDoneDpc, hcdMapDoneDpc, hc);
    hc->MapBusy = 0;
    hc->MapDpcsInFlight = 0;
}

/* At a stop, before the adapter goes: no pump DPC or channel request may
 * still be live. IRQL: PASSIVE_LEVEL. */
VOID HcdDmaMapDrain(PHCD_CONTROLLER hc)
{
    LARGE_INTEGER due;

    while (hc->MapDpcsInFlight != 0 || hc->MapBusy) {
        HcdRelativeMs(&due, 1);
        (VOID)KeDelayExecutionThread(KernelMode, FALSE, &due);
    }
}
