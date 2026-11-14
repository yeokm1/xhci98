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
 * carved by xhci_mem.c (section 11.2). Since task 48.33 (section 11.11) it
 * and every other allocation the controller can name - the event ring's
 * block, the enumeration scratch, stream blocks and isochronous rings, and
 * all of the staging (hcd_stage.c) - is common buffer from this file, and
 * no map register is asked for a transfer: the map pump is gone. Each
 * allocation is charged to the driver's boot ledger before it is made and
 * refused past its cap (Q-STG.3, the owner's answer (a): 64 MiB). Each is
 * freed at stop and remove before the adapter is put back - unless the
 * controller could not be proven to have stopped mastering
 * (HcdSvcDmaNotStopped), in which case it is kept for the rest of the boot
 * with the adapter, its charge turned kept, and the driver image kept
 * loaded so the ledger lasts the boot.
 *
 * IRQL: PASSIVE_LEVEL (AllocateCommonBuffer and IoGetDmaAdapter require it).
 */

#include "hcd.h"
#include "xhci_hw.h"
#include "xhci_stage.h"
#include "xhci_dbg.h"

/* ----------------------------------------------------------------------- */
/* The boot ledger (task 48.33; design record 13 section 11.11.6)           */
/* ----------------------------------------------------------------------- */

/*
 * One for the driver image, not per controller, since a controller removed
 * and found again is a new FDO: the bytes of every allocation a controller
 * could name, at its allocation size in whole pages, charged before it is
 * made. A charge is live while the allocation is, kept once an unproven
 * stop keeps it, and returned only when the allocation is freed. At the
 * first kept allocation the driver's DriverUnload is cleared, under the
 * ledger's lock, so the image and the ledger in it stay for the boot.
 */
static KSPIN_LOCK HcdLedgerLock;
static ULONG HcdLedgerLive;
static ULONG HcdLedgerKept;
static PDRIVER_OBJECT HcdLedgerDriver;

/* DriverEntry. IRQL: PASSIVE_LEVEL. */
VOID HcdDmaLedgerInit(PDRIVER_OBJECT driver)
{
    KeInitializeSpinLock(&HcdLedgerLock);
    HcdLedgerLive = 0;
    HcdLedgerKept = 0;
    HcdLedgerDriver = driver;
}

#if defined(XHCI_FLAVOUR_QEMU)
/* Task 49.29 (record 13 section 11.11.12), qemu only: the ledger's live and
 * kept totals on the 0xE9 channel, which outlives every controller. */
static VOID hcdLedgerPrint(const char *what)
{
    KIRQL oldIrql;
    ULONG w[2];

    KeAcquireSpinLock(&HcdLedgerLock, &oldIrql);
    w[0] = HcdLedgerLive;
    w[1] = HcdLedgerKept;
    KeReleaseSpinLock(&HcdLedgerLock, oldIrql);
    XHCI_DBG_WORDS(what, w, 2);
}
#endif

/* The image kept for the rest of the boot, whatever kept it: an unproven
 * stop's memory below, or mode 4's refusal table, whose entries must
 * outlive every device object (design record 15 section 4.5.4). Under the
 * ledger's lock, as the kept memory's own clearing is. IRQL: <=
 * DISPATCH_LEVEL. */
VOID HcdDmaKeepImage(VOID)
{
    KIRQL oldIrql;

    KeAcquireSpinLock(&HcdLedgerLock, &oldIrql);
    if (HcdLedgerDriver != NULL) {
        HcdLedgerDriver->DriverUnload = NULL;
    }
    KeReleaseSpinLock(&HcdLedgerLock, oldIrql);
}

/* Returns 0, nothing charged, past the cap. */
static ULONG hcdLedgerCharge(PHCD_CONTROLLER hc, ULONG bytes)
{
    KIRQL oldIrql;
    ULONG fits;
    ULONG total;

    bytes = XhciStagePages(bytes);
    KeAcquireSpinLock(&HcdLedgerLock, &oldIrql);
    fits = (bytes != 0 &&
            XhciStageLedgerFits(HcdLedgerLive, HcdLedgerKept, bytes,
                                XHCI_STAGE_LEDGER_CAP))
               ? 1UL
               : 0UL;
    if (fits) {
        HcdLedgerLive += bytes;
    }
    total = HcdLedgerLive + HcdLedgerKept;
    KeReleaseSpinLock(&HcdLedgerLock, oldIrql);
    if (!fits) {
        XhciLogNote(&hc->Hc, "dma.quarantine.cap", total);
#if defined(XHCI_FLAVOUR_QEMU)
        XHCI_DBG_VALUE("dma.ledger: REFUSED, bytes asked", bytes);
        hcdLedgerPrint("dma.ledger: at the refusal");
#endif
    }
    return fits;
}

static VOID hcdLedgerReturn(ULONG bytes)
{
    KIRQL oldIrql;

    bytes = XhciStagePages(bytes);
    KeAcquireSpinLock(&HcdLedgerLock, &oldIrql);
    HcdLedgerLive = (HcdLedgerLive >= bytes) ? HcdLedgerLive - bytes : 0UL;
    KeReleaseSpinLock(&HcdLedgerLock, oldIrql);
}

static VOID hcdLedgerKeep(PHCD_CONTROLLER hc, ULONG bytes)
{
    KIRQL oldIrql;
    ULONG first;

    bytes = XhciStagePages(bytes);
    KeAcquireSpinLock(&HcdLedgerLock, &oldIrql);
    HcdLedgerLive = (HcdLedgerLive >= bytes) ? HcdLedgerLive - bytes : 0UL;
    first = (HcdLedgerKept == 0) ? 1UL : 0UL;
    HcdLedgerKept += bytes;
    if (HcdLedgerDriver != NULL) {
        HcdLedgerDriver->DriverUnload = NULL;
    }
    KeReleaseSpinLock(&HcdLedgerLock, oldIrql);
    hc->CommonBuffersKept++;
#if defined(XHCI_FLAVOUR_QEMU)
    hc->QemuDmaKept++;
    hc->QemuDmaKeptPages += bytes / PAGE_SIZE;
#endif
    if (first) {
        XhciLogNote(&hc->Hc, "dma.quarantine.first", bytes);
    }
}

/* A common buffer of `bytes`, charged first and checked whole below 4 GB
 * (its high part 0 and its last byte below 4 GB); NULL otherwise, nothing
 * left charged. IRQL: PASSIVE_LEVEL. */
static PVOID hcdDmaAlloc(PHCD_CONTROLLER hc, ULONG bytes,
                         PPHYSICAL_ADDRESS pa)
{
    PVOID va;

    if (hc->Dma == NULL || bytes == 0 || !hcdLedgerCharge(hc, bytes)) {
        return NULL;
    }
    va = hc->Dma->DmaOperations->AllocateCommonBuffer(hc->Dma, bytes, pa,
                                                      TRUE);
    if (va != NULL &&
        (pa->HighPart != 0 || bytes - 1UL > 0xFFFFFFFFUL - pa->LowPart)) {
        hc->Dma->DmaOperations->FreeCommonBuffer(hc->Dma, bytes, *pa, va,
                                                 TRUE);
        va = NULL;
    }
    if (va == NULL) {
        hcdLedgerReturn(bytes);
    }
#if defined(XHCI_FLAVOUR_QEMU)
    if (va != NULL) {
        hc->QemuDmaMade++;
        hc->QemuDmaMadePages += XhciStagePages(bytes) / PAGE_SIZE;
    }
#endif
    return va;
}

/* Its release, or - a bus master that may still run holding it - kept
 * for the boot, its charge turned kept. IRQL: PASSIVE_LEVEL. */
static VOID hcdDmaFree(PHCD_CONTROLLER hc, ULONG bytes, PHYSICAL_ADDRESS pa,
                       PVOID va)
{
    if (va == NULL) {
        return;
    }
    if (hc->Dma == NULL || hc->CommonBufferPinned) {
        hcdLedgerKeep(hc, bytes);
        return;
    }
    hc->Dma->DmaOperations->FreeCommonBuffer(hc->Dma, bytes, pa, va, TRUE);
    hcdLedgerReturn(bytes);
#if defined(XHCI_FLAVOUR_QEMU)
    hc->QemuDmaFreed++;
    hc->QemuDmaFreedPages += XhciStagePages(bytes) / PAGE_SIZE;
#endif
}

/*
 * Task 47.30 (design record 13 section 11.10, point 2): the larger event
 * ring's block. 16 KiB first; when it has no 64 KiB-safe window
 * (XhciEventRingWindow) it is given back and 32 KiB asked for once, which
 * always has one. A refusal, a block above 4 GB or no window leaves the
 * start on the in-block ring, which every start carves anyway, so nothing
 * here can fail the start. IRQL: PASSIVE_LEVEL.
 */
static VOID hcdDmaEventRingOpen(PHCD_CONTROLLER hc)
{
    PHYSICAL_ADDRESS pa;
    PVOID va;
    ULONG bytes;
    ULONG offset;
    ULONG pass;
    ULONG first;
    ULONG refuse;

    hc->EvtBlockVa = NULL;
    hc->EvtBlockPa.LowPart = 0;
    hc->EvtBlockPa.HighPart = 0;
    hc->EvtBlockBytes = 0;
    hc->EvtRingVa = NULL;
    hc->EvtRingPa = 0;
    if (!hc->EvtWanted) {
        /* Task 49.35: which rule chose the smaller ring. */
        hc->EvtRoute = (hc->EvtPick == XHCI_EVT_PICK_HELD) ? HCD_EVT_ROUTE_HELD
                       : (hc->EvtPick == XHCI_EVT_PICK_DEFAULT)
                           ? HCD_EVT_ROUTE_DEFAULT
                           : HCD_EVT_ROUTE_VALUE;
        return;
    }
    first = 0;
    refuse = 0;
#if defined(XHCI_FLAVOUR_QEMU)
    if (hc->QemuEventRing == HCD_EVT_QEMU_SMALL) {
        hc->EvtRoute = HCD_EVT_ROUTE_TEST;
        return;
    }
    first = (hc->QemuEventRing == HCD_EVT_QEMU_WIDE) ? 1UL : 0UL;
    refuse = (hc->QemuEventRing == HCD_EVT_QEMU_REFUSE) ? 1UL : 0UL;
#endif
    hc->EvtRoute = HCD_EVT_ROUTE_REFUSED;
    for (pass = first; pass < 2; pass++) {
        bytes = (pass == 0) ? XHCI_EVENT_RING_BIG_BLOCK
                            : XHCI_EVENT_RING_BIG_WIDE;
        va = hcdDmaAlloc(hc, bytes, &pa);
        if (va == NULL) {
            return;
        }
        if (!refuse &&
            XhciEventRingWindow(pa.LowPart, bytes, XHCI_EVENT_RING_BIG_BYTES,
                                &offset)) {
            hc->EvtBlockVa = va;
            hc->EvtBlockPa = pa;
            hc->EvtBlockBytes = bytes;
            hc->EvtRingVa = (PVOID)((PUCHAR)va + offset);
            hc->EvtRingPa = pa.LowPart + offset;
            hc->EvtRoute = (pass == 0) ? HCD_EVT_ROUTE_BLOCK
                                       : HCD_EVT_ROUTE_WIDE;
            return;
        }
        hcdDmaFree(hc, bytes, pa, va);
    }
}

/* Its release with the controller block, under the same pin rule: a bus
 * master that may still run keeps it, counted. IRQL: PASSIVE_LEVEL. */
static VOID hcdDmaEventRingClose(PHCD_CONTROLLER hc)
{
    if (hc->EvtBlockVa != NULL) {
        hcdDmaFree(hc, hc->EvtBlockBytes, hc->EvtBlockPa, hc->EvtBlockVa);
    }
    hc->EvtBlockVa = NULL;
    hc->EvtBlockPa.LowPart = 0;
    hc->EvtBlockPa.HighPart = 0;
    hc->EvtBlockBytes = 0;
    hc->EvtRingVa = NULL;
    hc->EvtRingPa = 0;
}

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

#if defined(XHCI_FLAVOUR_QEMU)
    /* Task 49.29 (record 13 section 11.11.12): this start's allocations
     * counted from 0, and what the ledger carries over printed. */
    hc->QemuDmaMade = 0;
    hc->QemuDmaMadePages = 0;
    hc->QemuDmaFreed = 0;
    hc->QemuDmaFreedPages = 0;
    hc->QemuDmaKept = 0;
    hc->QemuDmaKeptPages = 0;
    hcdLedgerPrint("dma.ledger: at the start, live/kept");
#endif
    mapRegisters = 0;
    hc->Dma = IoGetDmaAdapter(hc->Pdo, &desc, &mapRegisters);
    if (hc->Dma == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    hc->MapRegisters = mapRegisters;
    hc->MapGrantRaw = mapRegisters;

    /* The controller is programmed with the low dword alone (StartPA):
     * checked whole below 4 GB rather than assumed (task 28-A.2, and the
     * whole range since task 48.33). A start past the boot ledger's cap
     * fails here (Code 10). */
    va = hcdDmaAlloc(hc, XHCI_HC_RESOURCES_SIZE, &pa);
    if (va == NULL) {
        hc->Dma->DmaOperations->PutDmaAdapter(hc->Dma);
        hc->Dma = NULL;
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    hc->CommonVa = va;
    hc->CommonPa = pa;
    hc->CommonBytes = XHCI_HC_RESOURCES_SIZE;
    /* The pin names the block a bus master may still hold. A block kept by
     * an earlier stop stays kept; this new one starts unpinned (task
     * 26-A.2). */
    hc->CommonBufferPinned = 0;

    /* The enumeration's scratch, where descriptors land (hcd_enum.c). Its
     * absence leaves the controller started with nothing enumerated, which
     * HcdEnumService checks for. */
    hc->ScratchVa = hcdDmaAlloc(hc, HCD_SCRATCH_BYTES, &hc->ScratchPa);
    hcdDmaEventRingOpen(hc);
    return STATUS_SUCCESS;
}

#if defined(XHCI_FLAVOUR_QEMU)
/* Task 49.29 (record 13 section 11.11.12): this start's allocations handed
 * to a caller (hcdDmaAlloc's own refusal of a range above 4 GB is not one),
 * freed and kept, in number and in pages, and those neither freed nor kept
 * (0 at an unproven stop with staging on; at staging 0 a kept device's own
 * blocks stay live), then the ledger after it. IRQL: PASSIVE_LEVEL. */
static VOID hcdDmaQemuTotals(PHCD_CONTROLLER hc)
{
    ULONG w[8];

    w[0] = hc->QemuDmaMade;
    w[1] = hc->QemuDmaMadePages;
    w[2] = hc->QemuDmaFreed;
    w[3] = hc->QemuDmaFreedPages;
    w[4] = hc->QemuDmaKept;
    w[5] = hc->QemuDmaKeptPages;
    w[6] = hc->QemuDmaMade - hc->QemuDmaFreed - hc->QemuDmaKept;
    w[7] = hc->QemuDmaMadePages - hc->QemuDmaFreedPages -
           hc->QemuDmaKeptPages;
    XHCI_DBG_WORDS("dma.alloc: made, freed, kept, left (n/pages each)", w,
                   8);
    XhciLogNote(&hc->Hc, "dma.qemu.kept.pages", hc->QemuDmaKeptPages);
    XhciLogNote(&hc->Hc, "dma.qemu.left", w[6]);
    hcdLedgerPrint("dma.ledger: after the release, live/kept");
}
#endif

VOID HcdDmaClose(PHCD_CONTROLLER hc)
{
    if (hc->Dma == NULL) {
        return;
    }
    hcdDmaEventRingClose(hc);
    hcdDmaFree(hc, HCD_SCRATCH_BYTES, hc->ScratchPa, hc->ScratchVa);
    hc->ScratchVa = NULL;
    hcdDmaFree(hc, hc->CommonBytes, hc->CommonPa, hc->CommonVa);
    hc->CommonVa = NULL;
    if (hc->CommonBufferPinned) {
        /* A bus master that may still be running holds these blocks'
         * addresses; giving them back would let the next owner's data be
         * overwritten. They stay allocated, and so does the adapter. */
        hc->Dma = NULL;
#if defined(XHCI_FLAVOUR_QEMU)
        hcdDmaQemuTotals(hc);
#endif
        return;
    }
    hc->Dma->DmaOperations->PutDmaAdapter(hc->Dma);
    hc->Dma = NULL;
#if defined(XHCI_FLAVOUR_QEMU)
    hcdDmaQemuTotals(hc);
#endif
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
    return hcdDmaAlloc(hc, bytes, pa);
}

/* Its release, once the controller holds no pointer into it - or, while a
 * bus master may still run (CommonBufferPinned), kept for good and counted,
 * as the main block is kept (HcdDmaClose). IRQL: PASSIVE_LEVEL. */
VOID HcdDmaStreamFree(PHCD_CONTROLLER hc, ULONG bytes, PHYSICAL_ADDRESS pa,
                      PVOID va)
{
    hcdDmaFree(hc, bytes, pa, va);
}

/* One its caller cannot give back (hcd_cfg.c, a ring page it could not
 * retire): kept, its charge turned kept. IRQL: PASSIVE_LEVEL. */
VOID HcdDmaStreamKeep(PHCD_CONTROLLER hc, ULONG bytes)
{
    hcdLedgerKeep(hc, bytes);
}

/* ----------------------------------------------------------------------- */
/* The map pump (26-A.5; design record 13 section 11.3)                     */
/* ----------------------------------------------------------------------- */

/*
 * With XhciDmaStaging 0 alone (owner, 2026-11-09; staging, task 48.33,
 * is the default and never queues here), every URB buffer reaches the
 * controller through the adapter's map registers, as in 3.4.2.0: AllocateAdapterChannel for the pages a chunk spans, MapTransfer
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
    KIRQL raised;
    NTSTATUS status;

    KeAcquireSpinLock(&hc->MapLock, &oldIrql);
    if (hc->MapBusy || hc->MapQuarantined || IsListEmpty(&hc->MapQueue)) {
        KeReleaseSpinLock(&hc->MapLock, oldIrql);
        return;
    }
    entry = RemoveHeadList(&hc->MapQueue);
    hc->MapBusy = 1;
    hc->MapInCall = 1;
    hc->MapPhase = HCD_MAP_PHASE_ASKED;
    KeReleaseSpinLock(&hc->MapLock, oldIrql);

    x = CONTAINING_RECORD(entry, HCD_XFER, Link);
    KeRaiseIrql(DISPATCH_LEVEL, &raised);
    status = hc->Dma->DmaOperations->AllocateAdapterChannel(
        hc->Dma, hc->Common.Self, x->MapCount, hcdMapExecute, x);
    /* Under the lock, so the drain's quarantine never sees a request still
     * inside this call as one only the HAL holds. */
    KeAcquireSpinLock(&hc->MapLock, &oldIrql);
    hc->MapInCall = 0;
    KeReleaseSpinLock(&hc->MapLock, oldIrql);
    KeLowerIrql(raised);
    if (!NT_SUCCESS(status)) {
        hc->MapRefusals++;
        KeAcquireSpinLock(&hc->MapLock, &oldIrql);
        hc->MapBusy = 0;
        hc->MapPhase = HCD_MAP_PHASE_IDLE;
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
 * anything else can clear it early (batch (c)).
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
    hc->MapPhase = HCD_MAP_PHASE_IDLE;
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
            !XhciXpipeAppendRun(w->WideList.List.SgElement, &n,
                                XHCI_XPIPE_WIDE_ELEMENTS,
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
    ULONG quarantined;

    UNREFERENCED_PARAMETER(Irp);
    hc = (PHCD_CONTROLLER)DeviceObject->DeviceExtension;
    x = (PHCD_XFER)Context;
    /* Entered, or quarantined by a fail-closed stop that could not wait
     * for it (HcdDmaMapDrain): decided under the lock the drain decides
     * under. A quarantined request touches nothing but this FDO's
     * extension, which its REMOVE keeps - not the record, the adapter
     * pointer the stop dropped, or the pump - and hands the registers it
     * was just given straight back. */
    KeAcquireSpinLock(&hc->MapLock, &oldIrql);
    quarantined = hc->MapQuarantined;
    if (quarantined) {
        hc->MapsQuarantinedLate++;
    } else {
        hc->MapPhase = HCD_MAP_PHASE_ROUTINE;
    }
    KeReleaseSpinLock(&hc->MapLock, oldIrql);
    if (quarantined) {
        return DeallocateObject;
    }
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
     * so the stop's drain cannot see an idle pump meanwhile (batch (c)).
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


/* The pump's objects, the completion list and the slow requests, once,
 * beside the controller's other objects, and staging's (hcd_stage.c).
 * IRQL: PASSIVE_LEVEL. */
VOID HcdDmaInitObjects(PHCD_CONTROLLER hc)
{
    KeInitializeSpinLock(&hc->MapLock);
    InitializeListHead(&hc->MapQueue);
    InitializeListHead(&hc->DoneList);
    InitializeListHead(&hc->SlowIrps);
    KeInitializeDpc(&hc->MapDpc, hcdMapDpc, hc);
    KeInitializeDpc(&hc->MapDoneDpc, hcdMapDoneDpc, hc);
    hc->MapBusy = 0;
    hc->MapPhase = HCD_MAP_PHASE_IDLE;
    hc->MapQuarantined = 0;
    hc->MapDpcsInFlight = 0;
    HcdStageInitObjects(hc);
}

/* How many 1 ms waits - each a clock tick at most, so at least 0.1 s and
 * under 2 s - a fail-closed stop gives a channel request the HAL still
 * holds before it quarantines it (HcdDmaMapDrain). */
#define HCD_MAP_QUARANTINE_ROUNDS 100UL

/*
 * At a stop, before the adapter goes: no pump DPC or channel request may
 * still be live. IRQL: PASSIVE_LEVEL.
 *
 * One case cannot be waited out. A stop that could not prove the
 * controller stopped mastering (CommonBufferPinned) keeps every record and
 * the map registers its mapped ones hold (HcdIoDeviceDrain), so a channel
 * request the HAL queued for want of registers may never be granted, and
 * there is no way to withdraw one (AllocateAdapterChannel has no cancel
 * before DMA version 3). Once it has stayed asked, with nothing of the
 * pump running, through HCD_MAP_QUARANTINE_ROUNDS waits, it is quarantined
 * instead, under MapLock, so its routine either had entered already - and
 * is waited for - or will find the mark: the pump stays closed, the HAL's
 * late call hands its registers back and touches nothing else
 * (hcdMapExecute), no start runs on this FDO again (hcd_pnp.c), and its
 * REMOVE keeps the device object, whose wait block the HAL still holds
 * and whose extension the late call reads, for the life of the system,
 * as the kept common buffer and adapter are. Not run: it needs real map
 * registers (2000 under PAE, 32-bit Vista to 10 with memory above 4 GB)
 * and a controller that would not stop.
 */
VOID HcdDmaMapDrain(PHCD_CONTROLLER hc)
{
    LARGE_INTEGER due;
    KIRQL oldIrql;
    ULONG asked;
    ULONG busy;

    asked = 0;
    for (;;) {
        busy = 1;
        if (hc->MapDpcsInFlight == 0) {
            KeAcquireSpinLock(&hc->MapLock, &oldIrql);
            busy = hc->MapBusy;
            if (busy && hc->CommonBufferPinned && !hc->MapInCall &&
                hc->MapPhase == HCD_MAP_PHASE_ASKED) {
                asked++;
                if (asked >= HCD_MAP_QUARANTINE_ROUNDS) {
                    hc->MapQuarantined = 1;
                    busy = 0;
                }
            } else {
                asked = 0;
            }
            KeReleaseSpinLock(&hc->MapLock, oldIrql);
        }
        if (!busy) {
            break;
        }
        HcdRelativeMs(&due, 1);
        (VOID)KeDelayExecutionThread(KernelMode, FALSE, &due);
    }
    if (hc->MapQuarantined) {
        XhciLogNote(&hc->Hc, "dma.map.quarantined", asked);
    }
}


/* ----------------------------------------------------------------------- */
/* Stage 2's wide pool (36.24; design record 13 section 11.6)              */
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
        HcdRelativeMs(&due, 1000);
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
     * nothing it was given goes back before then. */
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
 * At a start, after the grant is known (hcd_ctl.c). With staging (task
 * 48.33, design record 13 section 11.11.2, as built) promotion is
 * withdrawn: a sequential chunk is up to 1 MiB, XhciWideMap is still read
 * and noted (hcd_ctl.c), no pool is made and no record is promoted, so
 * hcd_io.c's pipelined machine is never entered. With XhciDmaStaging 0, as
 * in 3.4.2.0: promotion is enabled only with XhciWideMap on, on Windows
 * 2000 or later (98 and ME never send a URB that would be promoted,
 * 36.21), with the adapter's register-free grant of 0x80001 (and, under
 * the qemu flavour's constraint, at least the check's 16 pages), the check
 * passed and the pool allocated. Anything else leaves every URB
 * sequential - stage 1 - counted and noted (xfer.wide.*). IRQL:
 * PASSIVE_LEVEL.
 */
static VOID hcdDmaWideOpenDirect(PHCD_CONTROLLER hc)
{
    PHCD_WIDE_ENTRY pool;
    KIRQL oldIrql;
    ULONG check;
    ULONG i;

    hc->WideEnabled = 0;
    hc->WidePool = NULL;
    hc->WideScratch = NULL;
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
        /* The entries, then 36.25's TRB scratch, one allocation: a pool a
         * fail-closed stop keeps keeps its scratch with it. */
        pool = (PHCD_WIDE_ENTRY)HcdPoolAlloc(
            HCD_WIDE_ENTRIES * (ULONG)sizeof(HCD_WIDE_ENTRY) +
            HCD_WIDE_SCRATCH_TRBS * (ULONG)sizeof(XHCI_TRB));
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
        hc->WideScratch = (PXHCI_TRB)(pool + HCD_WIDE_ENTRIES);
        hc->WideEnabled = 1;
        hc->Xpipe.Pool = HCD_WIDE_ENTRIES;
    }
    hc->Xpipe.Enabled = hc->WideEnabled;
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    XhciLogNote(&hc->Hc, "xfer.wide.check", check);
    XhciLogNote(&hc->Hc, "xfer.wide.enabled", hc->WideEnabled);
}

VOID HcdDmaWideOpen(PHCD_CONTROLLER hc)
{
    KIRQL oldIrql;

    if (!hc->Staging) {
        hcdDmaWideOpenDirect(hc);
        return;
    }
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    hc->WideEnabled = 0;
    hc->WidePool = NULL;
    hc->WideScratch = NULL;
    hc->RefillHead = NULL;
    hc->Xpipe.Check = XHCI_XPIPE_CHECK_NOT_RUN;
    hc->Xpipe.Enabled = 0;
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    XhciLogNote(&hc->Hc, "xfer.wide.check", XHCI_XPIPE_CHECK_NOT_RUN);
    XhciLogNote(&hc->Hc, "xfer.wide.enabled", 0);
}

/*
 * At a stop, after HcdDmaMapDrain. With staging no pool was made and this
 * clears the fields. With XhciDmaStaging 0 every record has normally
 * completed and no entry is owned; a stop that could not prove the
 * controller stopped mastering (CommonBufferPinned) keeps its devices'
 * records instead (HcdIoDeviceDrain), and one of them may still own an
 * entry and be finalized by a late cancel: then the pool is detached from
 * admission and kept for the life of the system, as the records it serves
 * are, and counted as a kept buffer is. IRQL: PASSIVE_LEVEL.
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
    hc->WideScratch = NULL;
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
