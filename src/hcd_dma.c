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
#include "xhci_hw.h"

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
    hc->MapGrantRaw = mapRegisters;

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

/*
 * A stream endpoint's block (31-A.1; xhci_stream.h, XhciStreamLayout): the
 * Primary Stream Context Array and every stream's ring, one allocation per
 * endpoint with streams open - the second-allocation route design record 13
 * section 11.2 left for streams, since the fixed block's ring pool is sized
 * for endpoints, not for 31 rings at a time. Whole pages, cache-enabled as
 * the main block is, below 4 GB by the adapter's Dma32BitAddresses; NULL
 * when the allocation fails or lands above 4 GB, which no pointer field
 * here can name. IRQL: PASSIVE_LEVEL.
 */
PVOID HcdDmaStreamAlloc(PHCD_CONTROLLER hc, ULONG bytes, PPHYSICAL_ADDRESS pa)
{
    PVOID va;

    if (hc->Dma == NULL || bytes == 0) {
        return NULL;
    }
    va = hc->Dma->DmaOperations->AllocateCommonBuffer(hc->Dma, bytes, pa,
                                                      TRUE);
    if (va != NULL && pa->HighPart != 0) {
        hc->Dma->DmaOperations->FreeCommonBuffer(hc->Dma, bytes, *pa, va,
                                                 TRUE);
        va = NULL;
    }
    return va;
}

/* Its release, once the controller holds no pointer into it - or, while a
 * bus master may still run (CommonBufferPinned), kept for good and counted,
 * as the main block is kept (HcdDmaClose). IRQL: PASSIVE_LEVEL. */
VOID HcdDmaStreamFree(PHCD_CONTROLLER hc, ULONG bytes, PHYSICAL_ADDRESS pa,
                      PVOID va)
{
    if (va == NULL) {
        return;
    }
    if (hc->Dma == NULL || hc->CommonBufferPinned) {
        hc->CommonBuffersKept++;
        return;
    }
    hc->Dma->DmaOperations->FreeCommonBuffer(hc->Dma, bytes, pa, va, TRUE);
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
        if (x->Probe) {
            /* The register-free check's own record: nothing acquired. */
            x->Mapped = 0;
            x->Status = HCD_USBD_NO_MEMORY;
            if (InterlockedDecrement(&hc->WideProbeLeft) == 0) {
                (VOID)KeSetEvent(&hc->WideProbeEvent, IO_NO_INCREMENT,
                                 FALSE);
            }
        } else {
            x->Status = HCD_USBD_NO_MEMORY;
            HcdIoMapped(hc, x, 0);
        }
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
 * 36.24 (design record 13 section 11.6, point 2): the whole URB mapped run
 * by run into its wide entry, under a request the HAL handed no registers -
 * a NULL base, so MapTransfer returns the pages' own addresses and holds
 * nothing. Returns 0 when a run does not map or the list would overflow;
 * with no registers there is nothing to undo. IRQL: DISPATCH_LEVEL.
 */
static ULONG hcdMapWide(PHCD_CONTROLLER hc, PHCD_XFER x)
{
    PHCD_WIDE_ENTRY w;
    PHYSICAL_ADDRESS pa;
    PUCHAR va;
    ULONG done;
    ULONG run;
    ULONG n;

    w = x->Wide;
    va = (PUCHAR)MmGetMdlVirtualAddress(x->Mdl);
    n = 0;
    done = 0;
    while (done < x->Length) {
        run = x->Length - done;
        pa = hc->Dma->DmaOperations->MapTransfer(hc->Dma, x->Mdl, NULL,
                                                 va + done, &run,
                                                 (BOOLEAN)!x->In);
        if (run == 0 || run > x->Length - done || pa.HighPart != 0 ||
            !XhciXpipeAppendRun(w->Wide, &n, XHCI_XPIPE_WIDE_ELEMENTS,
                                pa.LowPart, run, done)) {
            return 0;
        }
        done += run;
    }
    w->Count = n;
    return 1;
}

/*
 * The register-free check's records (HcdDmaWideOpen): a 1-register request
 * maps all 16 pages only when the HAL handed it a NULL base - with real
 * registers that would map past its grant, so it maps nothing - and the
 * 16-register one maps them as any chunk is. The registers are given back
 * by the check, after both have returned. IRQL: DISPATCH_LEVEL.
 */
static VOID hcdMapProbe(PHCD_CONTROLLER hc, PHCD_XFER x, PVOID base)
{
    PHYSICAL_ADDRESS pa;
    PUCHAR va;
    ULONG done;
    ULONG run;
    ULONG n;

    x->Mapped = 1;
    n = 0;
    if (x->MapCount == 1 && base != NULL) {
        x->Mapped = 0;
    }
    va = hcdXferVa(x);
    done = 0;
    while (x->Mapped && done < x->Chunk) {
        run = x->Chunk - done;
        pa = hc->Dma->DmaOperations->MapTransfer(hc->Dma, x->Mdl, base,
                                                 va + done, &run, TRUE);
        if (run == 0 || run > x->Chunk - done || pa.HighPart != 0 ||
            !XhciXpipeAppendRun(x->Sg.List.SgElement, &n, HCD_SG_ELEMENTS,
                                pa.LowPart, run, done)) {
            x->Mapped = 0;
            break;
        }
        done += run;
    }
    x->Sg.List.SgElementCount = n;
    if (InterlockedDecrement(&hc->WideProbeLeft) == 0) {
        (VOID)KeSetEvent(&hc->WideProbeEvent, IO_NO_INCREMENT, FALSE);
    }
}

/*
 * The execution routine: map the chunk run by run, each run cut at page
 * bounds so no element crosses a 64 KB line (spec 6.4.1), and hand the
 * record on to be published. The registers are kept until HcdDmaUnmap.
 *
 * 36.24: a record that claimed a wide entry is promoted here to a mapping
 * of its whole URB when the HAL handed this request no registers and
 * promotion is on (section 11.6, point 2); otherwise, or when a run of the
 * whole does not map, it maps its first chunk as any record does and is
 * sequential (HcdIoMapped gives the entry back). A promoted record is
 * finalized only once this routine has given up its mapping state
 * (XHCI_XPIPE_MAP_ALLOCATED), so it is still this routine's after
 * HcdIoMapped; any other record may complete inside HcdIoMapped and is not
 * touched after it. IRQL: DISPATCH_LEVEL.
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
    KIRQL oldIrql;
    ULONG done;
    ULONG run;
    ULONG piece;
    ULONG n;
    ULONG ok;
    ULONG decision;
    ULONG promoted;
    ULONG final;

    UNREFERENCED_PARAMETER(Irp);
    hc = (PHCD_CONTROLLER)DeviceObject->DeviceExtension;
    x = (PHCD_XFER)Context;
    if (hc->MapInCall) {
        hc->MapsSynchronous++;
    } else {
        hc->MapsDeferred++;
    }
    x->MapBase = MapRegisterBase;
    if (x->Probe) {
        hcdMapProbe(hc, x, MapRegisterBase);
        goto release;
    }
    promoted = 0;
    if (x->Wide != NULL) {
        decision = XhciXpipePromote(MapRegisterBase == NULL,
                                    hc->MapGrantRaw, hc->WideEnabled);
        if (decision == XHCI_XPIPE_PROMOTE) {
            promoted = hcdMapWide(hc, x);
        }
        XhciControllerLockAcquire(&hc->Hc, &oldIrql);
        if (promoted) {
            /* Its life is the machine's from here (hcd_io.c). The mapped
             * extent is the whole URB; the base is NULL. */
            x->Promoted = 1;
            x->Chunk = x->Length;
            x->Cursor.Element = 0;
            x->Cursor.Skip = 0;
            x->PipeCounted = 0;
            x->RefillHeld = HCD_REFILL_NONE;
            XhciXpipeRecInit(&x->Rec, x->Length, XHCI_XPIPE_MAP_IN_ROUTINE);
            hc->Xpipe.Promoted++;
        } else if (decision == XHCI_XPIPE_PROMOTE_BASE) {
            hc->Xpipe.NoPromoteBase++;
        } else if (decision == XHCI_XPIPE_PROMOTE_OFF ||
                   decision == XHCI_XPIPE_PROMOTE_GRANT) {
            hc->Xpipe.NoPromoteOff++;
        } else {
            hc->Xpipe.NoPromoteRun++;
        }
        XhciControllerLockRelease(&hc->Hc, oldIrql);
    }
    if (promoted) {
        HcdIoMapped(hc, x, 1);
        XhciControllerLockAcquire(&hc->Hc, &oldIrql);
        final = XhciXpipeRecMapState(&x->Rec, XHCI_XPIPE_MAP_ALLOCATED);
        if (final & XHCI_XPIPE_F_FINAL) {
            x->State = HCD_XFER_DONE;
            InsertTailList(&hc->DoneList, &x->Link);
        }
        XhciControllerLockRelease(&hc->Hc, oldIrql);
        if (final & XHCI_XPIPE_F_FINAL) {
            HcdIoDeferred(hc);
        }
        goto release;
    }
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

release:
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

/* ----------------------------------------------------------------------- */
/* Stage 2's wide mappings (36.24; design record 13 section 11.6)          */
/* ----------------------------------------------------------------------- */

#define HCD_PROBE_PAGES     16UL

/*
 * The register-free check (section 11.6, point 2): one 16-page non-paged
 * buffer of the HCD's own, mapped twice through the pump - by a 1-register
 * request and by an ordinary 16-register one - and the two lists compared.
 * A regression check, never the capability detector: the HAL readings are
 * that. The buffer and its MDL go back only once both routines have
 * returned, and each request's registers, if the HAL handed any, after its
 * extent is flushed. Returns XHCI_XPIPE_CHECK_*. IRQL: PASSIVE_LEVEL, at a
 * start, before anything is enumerated.
 */
static ULONG hcdWideCheck(PHCD_CONTROLLER hc)
{
    LARGE_INTEGER due;
    PVOID buffer;
    PMDL mdl;
    PHCD_XFER x;
    KIRQL oldIrql;
    ULONG check;
    ULONG waited;
    ULONG i;

    buffer = HcdPoolAlloc(HCD_PROBE_PAGES * PAGE_SIZE);
    if (buffer == NULL) {
        return XHCI_XPIPE_CHECK_NO_MEMORY;
    }
    mdl = HcdPoolMdlBuild(buffer, HCD_PROBE_PAGES * PAGE_SIZE);
    if (mdl == NULL) {
        HcdPoolFree(buffer);
        return XHCI_XPIPE_CHECK_NO_MEMORY;
    }
    KeInitializeEvent(&hc->WideProbeEvent, NotificationEvent, FALSE);
    hc->WideProbeLeft = 2;
    for (i = 0; i < 2; i++) {
        x = &hc->WideProbe[i];
        x->Probe = 1;
        x->Mdl = mdl;
        x->Offset = 0;
        x->Chunk = HCD_PROBE_PAGES * PAGE_SIZE;
        x->MapCount = (i == 0) ? 1UL : HCD_PROBE_PAGES;
        x->In = 0;
        x->Mapped = 0;
        x->Status = 0;
        x->Sg.List.SgElementCount = 0;
    }
    HcdDmaMapQueue(hc, &hc->WideProbe[0]);
    HcdDmaMapQueue(hc, &hc->WideProbe[1]);
    /* No timeout: until both routines have run the HAL may still write
     * through the buffer's mapping, so it cannot go back. At a start with
     * nothing else mapped, a grant of 16 is never short. */
    waited = 0;
    for (;;) {
        due.QuadPart = -10000000;       /* 1 s */
        if (KeWaitForSingleObject(&hc->WideProbeEvent, Executive,
                                  KernelMode, FALSE, &due) ==
            STATUS_SUCCESS) {
            break;
        }
        waited++;
        XhciLogNote(&hc->Hc, "xfer.wide.check.waiting", waited);
    }
    /* The event is set inside the second routine; that routine and its
     * caller have returned only once the pump's release DPC has run, so
     * nothing it was given goes back before then (review round 1,
     * finding 4). */
    HcdDmaMapDrain(hc);
    if (hc->WideProbe[0].Status != 0 ||
        hc->WideProbe[1].Status != 0) {
        check = XHCI_XPIPE_CHECK_CHANNEL;
    } else {
        check = XhciXpipeCheckCompare(
        hc->WideProbe[0].Sg.List.SgElement,
        hc->WideProbe[0].Mapped ? hc->WideProbe[0].Sg.List.SgElementCount
                                : 0,
        hc->WideProbe[0].MapBase == NULL,
        hc->WideProbe[1].Sg.List.SgElement,
        hc->WideProbe[1].Mapped ? hc->WideProbe[1].Sg.List.SgElementCount
                                : 0,
        hc->WideProbe[1].MapBase == NULL, HCD_PROBE_PAGES * PAGE_SIZE);
    }
    for (i = 0; i < 2; i++) {
        x = &hc->WideProbe[i];
        if (x->MapBase != NULL) {
            KeRaiseIrql(DISPATCH_LEVEL, &oldIrql);
            if (x->Mapped) {
                (VOID)hc->Dma->DmaOperations->FlushAdapterBuffers(
                    hc->Dma, mdl, x->MapBase, MmGetMdlVirtualAddress(mdl),
                    x->Chunk, TRUE);
            }
            hc->Dma->DmaOperations->FreeMapRegisters(hc->Dma, x->MapBase,
                                                     x->MapCount);
            KeLowerIrql(oldIrql);
            x->MapBase = NULL;
        }
        x->Mdl = NULL;
    }
    HcdPoolMdlFree(mdl);
    HcdPoolFree(buffer);
    return check;
}

/*
 * At a start, after the grant is known (hcd_ctl.c): promotion is enabled
 * only with XhciWideMap on, on Windows 2000 or later (98 and ME never send
 * a URB that would be promoted, 36.21), with the adapter's register-free
 * grant of 0x80001 (and, under the qemu flavour's constraint, at least the
 * check's 16 pages), the check passed and the pool allocated. Anything else
 * leaves every URB sequential - stage 1 - counted and noted
 * (xfer.wide.*). IRQL: PASSIVE_LEVEL.
 */
VOID HcdDmaWideOpen(PHCD_CONTROLLER hc)
{
    PHCD_WIDE_ENTRY pool;
    KIRQL oldIrql;
    ULONG check;
    ULONG i;

    hc->WideEnabled = 0;
    hc->WidePool = NULL;
    hc->RefillHead = NULL;
    pool = NULL;
    if (!hc->WideSwitch) {
        check = XHCI_XPIPE_CHECK_NOT_RUN;
    } else if (!IoIsWdmVersionAvailable(1, 0x10)) {
        check = XHCI_XPIPE_CHECK_NOT_NT;
    } else if (hc->Dma == NULL || hc->MapGrantRaw != XHCI_XPIPE_FREE_GRANT) {
        /* Record 13 section 11.6.1: 0x80001 is the register-free adapter
         * on every HAL read; anything else - real registers (2000 under
         * PAE, Vista and 7 x86 with memory that may lie above 4 GB) - maps
         * nothing here. */
        check = XHCI_XPIPE_CHECK_GRANT;
    } else if (hc->MapRegisters < HCD_PROBE_PAGES) {
        /* The qemu flavour's constrained grant (XhciQemuMapGrant). */
        check = XHCI_XPIPE_CHECK_CHANNEL;
    } else {
        check = hcdWideCheck(hc);
    }
    if (check == XHCI_XPIPE_CHECK_PASS) {
        pool = (PHCD_WIDE_ENTRY)HcdPoolAlloc(
            HCD_WIDE_ENTRIES * (ULONG)sizeof(HCD_WIDE_ENTRY));
        if (pool == NULL) {
            check = XHCI_XPIPE_CHECK_NO_MEMORY;
        } else {
            for (i = 0; i < HCD_WIDE_ENTRIES; i++) {
                pool[i].Owner = NULL;
                pool[i].Count = 0;
            }
        }
    }
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    hc->Xpipe.Check = check;
    if (pool != NULL) {
        hc->WidePool = pool;
        hc->WideEnabled = 1;
        hc->Xpipe.Pool = HCD_WIDE_ENTRIES;
    }
    hc->Xpipe.Enabled = hc->WideEnabled;
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    XhciLogNote(&hc->Hc, "xfer.wide.check", check);
    XhciLogNote(&hc->Hc, "xfer.wide.enabled", hc->WideEnabled);
}

/*
 * At a stop, after HcdDmaMapDrain. Every record has normally completed and
 * no entry is owned. A stop that could not prove the controller stopped
 * mastering (CommonBufferPinned) keeps its devices' records instead
 * (HcdIoDeviceDrain), and one of them may still own an entry and be
 * finalized by a late cancel: then the pool is detached from admission and
 * kept for the life of the system, as the records it serves are (review
 * round 3), and counted as a kept buffer is. IRQL: PASSIVE_LEVEL.
 */
VOID HcdDmaWideClose(PHCD_CONTROLLER hc)
{
    KIRQL oldIrql;
    PHCD_WIDE_ENTRY pool;
    ULONG owned;
    ULONG i;

    owned = 0;
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    pool = hc->WidePool;
    hc->WidePool = NULL;
    hc->WideEnabled = 0;
    hc->Xpipe.Pool = 0;
    for (i = 0; pool != NULL && i < HCD_WIDE_ENTRIES; i++) {
        if (pool[i].Owner != NULL) {
            owned = 1;
        }
    }
    if (!owned) {
        hc->RefillHead = NULL;
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    if (pool == NULL) {
        return;
    }
    if (owned) {
        hc->CommonBuffersKept++;
        XhciLogNote(&hc->Hc, "xfer.wide.kept", 1);
        return;
    }
    HcdPoolFree(pool);
}
