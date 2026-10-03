/*
 * hcd_io.c - the URB transfer path: a device PDO's request onto a pipe,
 * through the map pump, onto the ring, and back (roadmap-hcd.md task
 * 26-A.5; design record 13 sections 6 and 11.3).
 *
 * A submission takes one of the pipe's preallocated records (HCD_XFER,
 * hcd.h), builds or borrows the MDL, and queues the record on the map pump
 * (hcd_dma.c), whose execution routine maps the chunk and hands it back to
 * HcdIoMapped, which publishes it through the transfer engine (xhci_xfer.c)
 * under the controller lock and rings the doorbell. The event DPC retires it
 * (hcd_dev.c -> HcdIoRetired) onto the controller's done list, and the
 * deferred work after the lock's release (HcdIoDeferred) unmaps it, starts
 * the next chunk or completes the IRP. A record never leaves its pipe and a
 * transfer allocates nothing but, for a URB without one, its MDL (section
 * 7.5 rule 3).
 *
 * The device record lives while any URB holds it: hcd_urb.c takes a
 * reference (Refs) under PdoListLock while the PDO still names the record,
 * and every IRP that reaches HcdIoSubmit returns it at completion. When the
 * thread frees the record, HcdIoDeviceGone refuses new submissions, drains
 * the engine's queues - the slot is already disabled or HCRST has taken it,
 * so nothing can still DMA - completes everything with DEVICE_GONE and waits
 * the references out.
 *
 * IRQL: <= DISPATCH_LEVEL unless a function says otherwise.
 */

#include <ntddk.h>
#include <usbdi.h>
#include "hcd.h"
#include "xhci_hw.h"
#include "xhci_xfer.h"
#include "xhci_pipe.h"
#include "xhci_dbg.h"

/* ----------------------------------------------------------------------- */
/* Pipes                                                                    */
/* ----------------------------------------------------------------------- */

VOID HcdIoPipeInit(PHCD_PIPE pipe, PHCD_USB_DEVICE dev)
{
    ULONG i;

    pipe->Signature = HCD_PIPE_SIGNATURE;
    pipe->Device = dev;
    InitializeListHead(&pipe->Waiting);
    InitializeListHead(&pipe->Held);
    for (i = 0; i < HCD_PIPE_XFERS; i++) {
        pipe->Xfers[i].Pipe = pipe;
        pipe->Xfers[i].State = HCD_XFER_FREE;
    }
}

/* The default pipe of a new device record: EP0's ring and queue are the
 * record's own, which the thread's enumeration shares. IRQL: PASSIVE_LEVEL,
 * before the record is published. */
VOID HcdIoPipeInitEp0(PHCD_USB_DEVICE dev)
{
    PHCD_PIPE pipe;

    pipe = &dev->Ep0Pipe;
    HcdIoPipeInit(pipe, dev);
    pipe->Dci = 1;
    pipe->EndpointAddress = 0;
    pipe->TransferType = XHCI_PIPE_XFER_CONTROL;
    pipe->Ring = &dev->Ep0;
    pipe->Queue = &dev->Ep0Queue;
}

/* ----------------------------------------------------------------------- */
/* Submission                                                               */
/* ----------------------------------------------------------------------- */

/* The next chunk: the pages it may span are bounded by the adapter's grant
 * and by the SG elements a record carries; a control transfer is one TD and
 * cannot be split, and neither is an isochronous one - its second chunk
 * would reach the ring only after the first retired, a gap in the stream
 * every time. Returns 0 when the remainder cannot be mapped at all. */
static ULONG hcdPlanChunk(PHCD_CONTROLLER hc, PHCD_XFER x)
{
    PUCHAR va;
    ULONG remaining;
    ULONG pageOffset;
    ULONG pages;
    ULONG room;
    ULONG mps;

    remaining = x->Length - x->Offset;
    va = (PUCHAR)MmGetMdlVirtualAddress(x->Mdl) + x->Offset;
    pageOffset = BYTE_OFFSET(va);
    pages = hc->MapRegisters;
    if (pages > HCD_SG_ELEMENTS) {
        pages = HCD_SG_ELEMENTS;
    }
    if (pages == 0) {
        return 0;
    }
    room = pages * PAGE_SIZE - pageOffset;
    if (remaining <= room) {
        x->Chunk = remaining;
    } else {
        if (x->Control || x->Isoch) {
            return 0;
        }
        /* A chunk that is not the last ends on a packet boundary, or the
         * device would see a short packet inside one URB (USB 2.0 5.8.3). */
        mps = x->Pipe->MaxPacketSize;
        if (mps == 0 || room < mps) {
            return 0;
        }
        x->Chunk = room - (room % mps);
    }
    x->MapCount = ADDRESS_AND_SIZE_TO_SPAN_PAGES(va, x->Chunk);
    return 1;
}

/*
 * Cancellation (design record 13 section 5.4: the cancel routine drops the
 * cancel spin lock before it takes the controller lock). The routine reads
 * the record from the IRP while the cancel lock still holds the IRP, then
 * only marks the record - under the controller lock, and only if the record
 * still carries this IRP - and wakes the thread, which stops the endpoint
 * and takes the TD off the ring (hcd_cfg.c). It touches the IRP no further,
 * so whichever completion path runs never races it: a completion that finds
 * the routine already called syncs on the cancel spin lock first
 * (hcdCancelOff).
 */
static VOID NTAPI hcdCancel(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PHCD_XFER x;
    ULONG seq;
    PHCD_USB_DEVICE dev;
    PHCD_CONTROLLER hc;
    KIRQL oldIrql;

    /* Counted while the cancel lock still holds the IRP: a completion that
     * syncs on that lock (hcdCancelOff) then finds the count raised, and a
     * pipe or device is freed only once it is back to 0 (HcdIoWaitPipe,
     * HcdIoDeviceGone), so the record read below stays valid. The
     * controller comes from the PDO, which outlives the record. */
    hc = ((PHCD_DEVICE_PDO)DeviceObject->DeviceExtension)->Controller;
    x = (PHCD_XFER)Irp->Tail.Overlay.DriverContext[1];
    /* The submission's identity, read while the cancel lock still holds
     * the IRP: a record freed and reused by the same IRP meanwhile has a
     * new Seq (Codex review of batch (c), round 5, finding 4). */
    seq = x->Seq;
    if (hc != NULL) {
        (VOID)InterlockedIncrement(&hc->CancelsRunning);
    }
    IoReleaseCancelSpinLock(Irp->CancelIrql);
    if (hc == NULL) {
        return;
    }

    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    if (x->Irp == Irp && x->Seq == seq && x->State != HCD_XFER_FREE) {
        x->CancelRequested = 1;
        x->Pipe->CancelPending = 1;
        hc->CancelWork = 1;
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    HcdThreadWake(hc);
    (VOID)InterlockedDecrement(&hc->CancelsRunning);
}

/* Arm the routine for a pending IRP; one already cancelled is marked for
 * the thread at once. IRQL: <= DISPATCH_LEVEL. */
static VOID hcdCancelOn(PIRP irp, PHCD_XFER x)
{
    PHCD_CONTROLLER hc;
    KIRQL oldIrql;

    irp->Tail.Overlay.DriverContext[1] = x;
    (VOID)IoSetCancelRoutine(irp, hcdCancel);
    if (irp->Cancel && IoSetCancelRoutine(irp, NULL) != NULL) {
        hc = x->Pipe->Device->Controller;
        XhciControllerLockAcquire(&hc->Hc, &oldIrql);
        x->CancelRequested = 1;
        x->Pipe->CancelPending = 1;
        hc->CancelWork = 1;
        XhciControllerLockRelease(&hc->Hc, oldIrql);
        HcdThreadWake(hc);
    }
}

/* Disarm before completing. NULL back means the routine was called: wait
 * until it has released the cancel lock, after which it reads only the
 * record. IRQL: <= DISPATCH_LEVEL. */
static VOID hcdCancelOff(PIRP irp)
{
    KIRQL cancelIrql;

    if (IoSetCancelRoutine(irp, NULL) == NULL) {
        IoAcquireCancelSpinLock(&cancelIrql);
        IoReleaseCancelSpinLock(cancelIrql);
    }
}

/* Take a free record of the pipe for an IRP, or refuse: the device is gone,
 * or every record is busy. Controller lock held. */
static PHCD_XFER hcdTakeRecord(PHCD_PIPE pipe, PIRP irp)
{
    PHCD_XFER x;
    ULONG i;

    for (i = 0; i < HCD_PIPE_XFERS; i++) {
        x = &pipe->Xfers[i];
        if (x->State == HCD_XFER_FREE) {
            /* Claimed with its IRP, so an abort that runs before the rest
             * is filled in still finds and marks it (Codex review of batch
             * (c), round 3, finding 2). */
            x->State = HCD_XFER_MAPPING;
            x->Irp = irp;
            x->CancelRequested = 0;
            x->Mapped = 0;
            x->Seq = ++pipe->Seq;
            return x;
        }
    }
    return NULL;
}

/*
 * The Waiting list. An IRP that finds every record of its pipe out - or an
 * earlier IRP already waiting, so the pipe keeps submission order - is
 * parked on the pipe, linked through its Tail.Overlay.ListEntry, with its
 * PDO in DriverContext[2] and its pipe in DriverContext[3]. It holds its
 * device reference and is counted in its PDO's UrbsPending as a record's IRP
 * is, so a REMOVE or the device's free waits for it. A record that finishes
 * is handed straight to the first waiting IRP rather than freed
 * (hcdRecordRelease), so the pipe a record belongs to stays alive across the
 * hand-over. The list is under the controller lock; an IRP is taken off it
 * only once its cancel routine is cleared, and one whose routine is already
 * running is left for the routine to take (the cancel-safe queue pattern).
 * Every IRP leaving it unserved completes at the next tick
 * (HcdIoRefuseLater), never inline.
 */
static VOID hcdLaunch(PHCD_CONTROLLER hc, PHCD_XFER x, PVOID urb,
                      const HCD_IO_REQUEST *req);
static PHCD_XFER hcdWaitingStart(PHCD_CONTROLLER hc, PHCD_PIPE pipe);
static VOID hcdWaitingLaunch(PHCD_CONTROLLER hc, PHCD_XFER x);

static VOID NTAPI hcdWaitCancel(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PHCD_DEVICE_PDO pdo;
    PHCD_CONTROLLER hc;
    PHCD_PIPE pipe;
    PHCD_USB_DEVICE dev;
    PHCD_XFER x;
    PVOID urb;
    KIRQL oldIrql;

    pdo = (PHCD_DEVICE_PDO)DeviceObject->DeviceExtension;
    hc = pdo->Controller;
    /* Counted while the cancel lock still holds the IRP, so the pipe and
     * device stay until this routine is done with them (HcdIoWaitPipe,
     * HcdIoDeviceGone), as hcdCancel does. */
    if (hc != NULL) {
        (VOID)InterlockedIncrement(&hc->CancelsRunning);
    }
    IoReleaseCancelSpinLock(Irp->CancelIrql);
    if (hc == NULL) {
        return;
    }
    pipe = (PHCD_PIPE)Irp->Tail.Overlay.DriverContext[3];
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    RemoveEntryList(&Irp->Tail.Overlay.ListEntry);
    dev = pipe->Device;
    x = hcdWaitingStart(hc, pipe);
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    if (x != NULL) {
        hcdWaitingLaunch(hc, x);
    }
    urb = IoGetCurrentIrpStackLocation(Irp)->Parameters.Others.Argument1;
    HcdIoIsoRefused(urb, HCD_USBD_CANCELED);
    (VOID)HcdIoRefuseLater(pdo, Irp, urb, HCD_USBD_CANCELED);
    (VOID)InterlockedDecrement(&pdo->UrbsPending);
    (VOID)InterlockedDecrement(&dev->Refs);
    (VOID)InterlockedDecrement(&hc->CancelsRunning);
}

/* Park an IRP on its pipe. Returns 0 when it was cancelled before it could
 * wait: the caller refuses it. Controller lock held. */
static ULONG hcdPark(PHCD_CONTROLLER hc, PHCD_PIPE pipe, PHCD_DEVICE_PDO pdo,
                     PIRP irp)
{
    irp->Tail.Overlay.DriverContext[2] = pdo;
    irp->Tail.Overlay.DriverContext[3] = pipe;
    IoMarkIrpPending(irp);
    (VOID)IoSetCancelRoutine(irp, hcdWaitCancel);
    if (irp->Cancel && IoSetCancelRoutine(irp, NULL) != NULL) {
        return 0;
    }
    /* Listed even when the routine is already running: it waits for this
     * lock and then takes the IRP off the list. */
    InsertTailList(&pipe->Waiting, &irp->Tail.Overlay.ListEntry);
    (VOID)InterlockedIncrement(&pdo->UrbsPending);
    hc->UrbsWaited++;
    return 1;
}

/* The first waiting IRP whose cancel routine this call cleared, off the
 * list; NULL when none can be taken. Controller lock held. */
static PIRP hcdWaitingTake(PHCD_PIPE pipe)
{
    PLIST_ENTRY entry;
    PIRP irp;

    for (entry = pipe->Waiting.Flink; entry != &pipe->Waiting;
         entry = entry->Flink) {
        irp = CONTAINING_RECORD(entry, IRP, Tail.Overlay.ListEntry);
        if (IoSetCancelRoutine(irp, NULL) != NULL) {
            RemoveEntryList(entry);
            return irp;
        }
    }
    return NULL;
}

/* Every waiting IRP that can be taken, onto `out`. Controller lock held. */
static VOID hcdWaitingFlush(PHCD_PIPE pipe, PLIST_ENTRY out)
{
    PIRP irp;

    for (;;) {
        irp = hcdWaitingTake(pipe);
        if (irp == NULL) {
            break;
        }
        InsertTailList(out, &irp->Tail.Overlay.ListEntry);
    }
}

/* Complete what hcdWaitingFlush took, with `usbd`, at the next tick. IRQL:
 * <= DISPATCH_LEVEL, no lock held. */
static VOID hcdWaitingRefuse(PLIST_ENTRY list, PHCD_USB_DEVICE dev, LONG usbd)
{
    PLIST_ENTRY entry;
    PHCD_DEVICE_PDO pdo;
    PIRP irp;

    while (!IsListEmpty(list)) {
        entry = RemoveHeadList(list);
        irp = CONTAINING_RECORD(entry, IRP, Tail.Overlay.ListEntry);
        pdo = (PHCD_DEVICE_PDO)irp->Tail.Overlay.DriverContext[2];
        HcdIoIsoRefused(
            IoGetCurrentIrpStackLocation(irp)->Parameters.Others.Argument1,
            usbd);
        (VOID)HcdIoRefuseLater(
            pdo, irp,
            IoGetCurrentIrpStackLocation(irp)->Parameters.Others.Argument1,
            usbd);
        (VOID)InterlockedDecrement(&pdo->UrbsPending);
        (VOID)InterlockedDecrement(&dev->Refs);
    }
}

/*
 * A record done with its IRP: handed to the first waiting IRP, which it
 * then belongs to (returned, to be launched once the lock is released), or
 * freed. Nothing is handed over on a pipe closing or a device going - their
 * drains take the waiting IRPs. Controller lock held.
 */
static PIRP hcdRecordRelease(PHCD_PIPE pipe, PHCD_XFER x)
{
    PIRP next;

    next = NULL;
    if (!pipe->Closed && !pipe->Device->Gone) {
        next = hcdWaitingTake(pipe);
    }
    x->CancelRequested = 0;
    x->Mapped = 0;
    x->Irp = next;
    if (next == NULL) {
        x->State = HCD_XFER_FREE;
    } else {
        x->State = HCD_XFER_MAPPING;
        x->Seq = ++pipe->Seq;
    }
    return next;
}

/*
 * A free record for the first waiting IRP that can be taken, claimed for
 * it; NULL when there is no free record or no such IRP. Admission parks
 * behind waiting IRPs without looking for a free record, and a record's
 * release skips an IRP whose cancel routine is running, so without this a
 * waiter could stay listed with every record free once that routine has
 * taken its IRP away (Codex review of batch (c), round 16, finding 1): the
 * cancel routine and every parking call it. Controller lock held.
 */
static PHCD_XFER hcdWaitingStart(PHCD_CONTROLLER hc, PHCD_PIPE pipe)
{
    PIRP irp;
    ULONG i;

    if (pipe->Closed || pipe->Device->Gone) {
        return NULL;
    }
    for (i = 0; i < HCD_PIPE_XFERS; i++) {
        if (pipe->Xfers[i].State == HCD_XFER_FREE) {
            break;
        }
    }
    if (i == HCD_PIPE_XFERS) {
        return NULL;
    }
    irp = hcdWaitingTake(pipe);
    if (irp == NULL) {
        return NULL;
    }
    hc->WaitingKicks++;
    return hcdTakeRecord(pipe, irp);
}

/* Launch a record hcdWaitingStart or hcdRecordRelease claimed for a waiting
 * IRP, its request re-read from the URB. IRQL: <= DISPATCH_LEVEL, no lock
 * held. */
static VOID hcdWaitingLaunch(PHCD_CONTROLLER hc, PHCD_XFER x)
{
    HCD_IO_REQUEST req;
    PVOID urb;

    x->Pdo = (PHCD_DEVICE_PDO)x->Irp->Tail.Overlay.DriverContext[2];
    urb = IoGetCurrentIrpStackLocation(x->Irp)->Parameters.Others.Argument1;
    hcdLaunch(hc, x, urb,
              HcdUrbIoRequest(urb, &req) == XHCI_USBD_STATUS_SUCCESS ? &req
                                                                     : NULL);
}

/* A record on its way: mapped by the pump, or published at once when it
 * moves no data. IRQL: <= DISPATCH_LEVEL, no lock held. */
static VOID hcdStart(PHCD_CONTROLLER hc, PHCD_XFER x)
{
    if (x->Length == 0) {
        HcdIoMapped(hc, x, 1);
    } else {
        x->State = HCD_XFER_MAPPING;
        HcdDmaMapQueue(hc, x);
    }
}

/* A refusal at submission: the device reference goes back now, and the
 * IRP completes at the next tick, never inside this dispatch (Codex review
 * of batch (c), round 8, finding 4; HcdIoRefuseLater). */
static NTSTATUS hcdRefuse(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                          PHCD_DEVICE_PDO pdo, PIRP irp, PURB urb, LONG usbd)
{
    (VOID)InterlockedDecrement(&dev->Refs);
    return HcdIoRefuseLater(pdo, irp, urb, usbd);
}

/* A client's pipe handle, checked against the device's open pipes - a
 * handle is a pipe's address, and only one the device still holds is
 * dereferenced. NULL is the default pipe. Controller lock held. */
static PHCD_PIPE hcdPipeFromHandle(PHCD_USB_DEVICE dev, PVOID handle)
{
    ULONG dci;

    if (handle == NULL) {
        return &dev->Ep0Pipe;
    }
    for (dci = 2; dci < 32; dci++) {
        if (dev->Pipes[dci] != NULL && (PVOID)dev->Pipes[dci] == handle) {
            return dev->Pipes[dci];
        }
    }
    return NULL;
}

/* ----------------------------------------------------------------------- */
/* Isochronous transfers                                                    */
/* ----------------------------------------------------------------------- */

static PUSBPORT_ISO_TRANSFER hcdIsoBlock(PHCD_XFER x)
{
    return &x->Pipe->Iso[x - x->Pipe->Xfers].Block;
}

/*
 * The pipe-dependent half of an isochronous URB's checks (hcd_urb.c has the
 * rest): no packet above the endpoint's Max ESIT Payload, which the xHC
 * must not be given (4.14.2.1 p.238), and an explicit StartFrame no further
 * ahead than an xHCI Frame ID can name (4.11.2.5 p.199) - 895 frames,
 * narrower than usbdi.h's 1024. The frame axis is read here, with no lock
 * held, so it is synced before the publish asks XhciFrameIdNow under the
 * lock. IRQL: <= DISPATCH_LEVEL, no lock held.
 */
static LONG hcdIsoAdmit(PHCD_CONTROLLER hc, PHCD_XFER x)
{
    struct _URB_ISOCH_TRANSFER *it;
    PHCD_PIPE pipe;
    ULONG now;

    it = &((PURB)x->Urb)->UrbIsochronousTransfer;
    pipe = x->Pipe;
    if (XhciPipeIsoCheck((const XHCI_PIPE_ISO_PACKET *)it->IsoPacket,
                         it->NumberOfPackets, it->TransferBufferLength,
                         XHCI_XFER_MAX_ISO_PACKETS,
                         pipe->Ep.MaxEsitPayload) != XHCI_PIPE_OK) {
        (VOID)InterlockedIncrement((PLONG)&hc->Hc.IsoRefusalsMalformed);
        return HCD_USBD_INVALID_PARAMETER;
    }
    now = XhciFrameNumber(&hc->Hc);
    /* The shape usbaudio.sys gives its URBs on each target is still to be
     * measured (packets, ASAP, how far ahead an explicit start is). */
    XHCI_DBG_VALUE_CHANGED(
        "hcd: isoch URB, packets << 16 | ASAP << 15 | start - now",
        (it->NumberOfPackets << 16) | (x->Asap ? 0x8000UL : 0UL) |
            (x->Asap ? 0UL : ((it->StartFrame - now) & 0x7FFFUL)));
    if (!x->Asap &&
        XhciPipeIsoStartOk(it->StartFrame, now, XHCI_FRAME_ID_WINDOW_END) !=
            XHCI_PIPE_OK) {
        return HCD_USBD_BAD_START_FRAME;
    }
    return XHCI_USBD_STATUS_SUCCESS;
}

/* The physical address of byte `offset` of the mapped chunk, from the SG
 * element holding it; 0 when none does. */
static ULONG hcdSgPa(const USBPORT_SCATTER_GATHER_LIST *sg, ULONG offset,
                     PULONG pa)
{
    const USBPORT_SCATTER_GATHER_ELEMENT *e;
    ULONG i;

    for (i = 0; i < sg->SgElementCount; i++) {
        e = &sg->SgElement[i];
        if (offset >= e->SgOffset &&
            offset - e->SgOffset < e->SgTransferLength) {
            *pa = e->SgPhysicalAddressLo + (offset - e->SgOffset);
            return 1;
        }
    }
    return 0;
}

/*
 * The record's block from its URB and its mapped chunk, each packet one or
 * two page-bounded fragments: the pump cuts its elements at page bounds
 * (hcd_dma.c), so a fragment never spans two. Rewritten whole each time, so
 * a record held mapped is filled again when released. IRQL:
 * <= DISPATCH_LEVEL.
 */
static ULONG hcdIsoFill(PHCD_XFER x)
{
    struct _URB_ISOCH_TRANSFER *it;
    PUSBPORT_ISO_TRANSFER blk;
    PUSBPORT_ISO_PACKET p;
    ULONG lengths[2];
    ULONG page;
    ULONG off;
    ULONG pa;
    ULONG n;
    ULONG i;
    ULONG b;

    it = &((PURB)x->Urb)->UrbIsochronousTransfer;
    blk = hcdIsoBlock(x);
    n = it->NumberOfPackets;
    page = BYTE_OFFSET(MmGetMdlVirtualAddress(x->Mdl));
    blk->Signature = USBPORT_ISO_SIGNATURE;
    blk->NumberOfPackets = n;
    blk->SgElementCount = x->Sg.List.SgElementCount;
    blk->Reserved = 0;
    for (i = 0; i < n; i++) {
        p = &blk->Packet[i];
        for (b = 0; b < sizeof(*p); b++) {
            ((PUCHAR)p)[b] = 0;
        }
        off = it->IsoPacket[i].Offset;
        p->Length = XhciPipeIsoLength(
            (const XHCI_PIPE_ISO_PACKET *)it->IsoPacket, n, x->Length, i);
        p->FragmentCount = 1;
        if (p->Length == 0) {
            /* One empty TRB (xhci_xfer.c), at an address the chunk owns. */
            if (!hcdSgPa(&x->Sg.List, 0, &pa)) {
                return 0;
            }
            p->Fragment0AddressLo = pa;
            continue;
        }
        p->FragmentCount = XhciPipeIsoFragments(page, off, p->Length,
                                                lengths);
        if (p->FragmentCount == 0 || !hcdSgPa(&x->Sg.List, off, &pa)) {
            return 0;
        }
        p->Fragment0Length = lengths[0];
        p->Fragment0AddressLo = pa;
        if (p->FragmentCount == 2) {
            if (!hcdSgPa(&x->Sg.List, off + lengths[0], &pa)) {
                return 0;
            }
            p->Fragment1Length = lengths[1];
            p->Fragment1AddressLo = pa;
        }
    }
    return 1;
}

/*
 * Publish an isochronous record. ASAP is always SIA: SIA is "after the last
 * queued TD" by definition, where an explicit Frame ID from an estimate of
 * the stream's position would open a gap or miss a service whenever the
 * estimate is off by one. An explicit StartFrame gets Frame IDs only when
 * the controller has CFC (4.11.2.5 p.200) and the frame axis is congruent;
 * the engine decides the rest, all or nothing. The extension's IsoScratch
 * and IsoLayout are shared by every isochronous pipe, which the controller
 * lock makes sound. Controller lock held.
 */
static ULONG hcdIsoPublish(PHCD_CONTROLLER hc, PHCD_PIPE pipe, PHCD_XFER x)
{
    PXHCI_EXTENSION ext;
    struct _URB_ISOCH_TRANSFER *it;
    PUSBPORT_ISO_TRANSFER blk;
    XHCI_ISO_REQUEST req;
    ULONG interval;
    ULONG answer;
    ULONG start;
    ULONG now;
    ULONG i;

    ext = &hc->Hc;
    it = &((PURB)x->Urb)->UrbIsochronousTransfer;
    blk = hcdIsoBlock(x);
    interval = pipe->Ep.Interval;
    for (i = 0; i < sizeof(req); i++) {
        ((PUCHAR)&req)[i] = 0;
    }
    req.Iso = blk;
    req.DirectionIn = pipe->Ep.DirectionIn;
    req.MaxPacketSize = pipe->Ep.MaxPacketSize;
    req.MaxBurstSize = pipe->Ep.MaxBurstSize;
    req.MaxEsitPayload = pipe->Ep.MaxEsitPayload;
    req.PacketsPerFrame = (interval <= 3UL) ? (8UL >> interval) : 0UL;
    req.Frames.IstFrames = ext->HcInfo.IstFrames;
    if (XhciFrameIdNow(ext, &now)) {
        req.Frames.CurrentFrame = now;
        req.Frames.Allowed = (ext->HcInfo.Cfc && !x->Asap) ? 1UL : 0UL;
    } else {
        now = ext->FrameNumber;
    }
    if (!x->Asap) {
        start = it->StartFrame;
    } else if (pipe->Queue->Count != 0) {
        start = pipe->IsoNext;
    } else {
        start = now + ext->HcInfo.IstFrames + 1UL;
    }
    for (i = 0; i < blk->NumberOfPackets; i++) {
        blk->Packet[i].FrameNumber = start + XhciPipeIsoFrameOf(i, interval);
    }
    if (!x->Asap) {
        /* The engine falls back to SIA for a group whose Frame IDs it cannot
         * use, which would move an explicit StartFrame to "now" (Codex review
         * of batch (c), round 16, finding 2). A start the controller cannot
         * be told - no CFC, no frame sample, a packet cadence the endpoint's
         * Interval does not share, or a packet outside the IST+1 to 895
         * frame window - is refused as BAD_START_FRAME instead; the engine's
         * own decision is asked, so the two cannot drift (round 17). */
        if (!XhciXferIsoUsesFrameIds(&req)) {
            hc->IsoBadStartFrames++;
            x->Status = HCD_USBD_BAD_START_FRAME;
            return XHCI_XFER_BAD_PARAM;
        }
    }
    answer = XhciXferSubmitIso(pipe->Queue, pipe->Ring, &req,
                               req.DirectionIn, &x->Xfer, x, ext->IsoScratch,
                               XHCI_XFER_MAX_ISO_TRBS, &ext->IsoLayout);
    if (answer == XHCI_XFER_OK) {
        if (x->Asap) {
            it->StartFrame = start;
        }
        pipe->IsoNext = start + XhciPipeIsoFrames(blk->NumberOfPackets,
                                                  interval);
        ext->IsoSubmits++;
        ext->IsoPacketsSubmitted += ext->IsoLayout.TdCount;
        if (ext->IsoLayout.FrameIdsUsed) {
            ext->IsoSubmitsWithFrameId++;
        }
        if (ext->IsoLayout.CadenceMismatch && req.PacketsPerFrame != 0) {
            ext->IsoCadenceMismatches++;
        }
        XHCI_DBG_VALUE_CHANGED(
            "hcd: isoch published, TRBs << 16 | Frame IDs << 8 | packets",
            (ext->IsoLayout.TrbCount << 16) |
                (ext->IsoLayout.FrameIdsUsed << 8) |
                (ext->IsoLayout.TdCount & 0xFFUL));
    } else if (answer == XHCI_XFER_ISO_TOO_LARGE) {
        ext->IsoRefusalsTooLarge++;
    } else if (answer != XHCI_XFER_BUSY) {
        ext->IsoRefusalsMalformed++;
    }
    return answer;
}

/*
 * An isochronous record's end: the engine's per-packet answers - every
 * packet no event answered stamped first (XhciXferIsoFinalise) - copied into
 * the URB as usbport does (usbport-miniport-abi.md, "Isochronous
 * transfers"): DATA_UNDERRUN is stored as success and not counted in
 * ErrorCount, and Length is the bytes for IN and 0 for OUT (usbdi.h:745-746).
 * The URB fails only as a whole (CANCELED, DEVICE_GONE) or with every packet
 * failed (usbdi.h:292-294). A record the ring never had gives every packet
 * its refusal. Returns the URB status, the bytes in *bytesOut. IRQL:
 * <= DISPATCH_LEVEL, no lock held.
 */
static LONG hcdIsoComplete(PHCD_XFER x, ULONG engine, LONG status,
                           PULONG bytesOut)
{
    struct _URB_ISOCH_TRANSFER *it;
    PUSBPORT_ISO_TRANSFER blk;
    LONG s;
    ULONG bytes;
    ULONG errors;
    ULONG len;
    ULONG n;
    ULONG i;

    it = &((PURB)x->Urb)->UrbIsochronousTransfer;
    blk = hcdIsoBlock(x);
    n = it->NumberOfPackets;
    if (engine) {
        XhciXferIsoFinalise(&x->Xfer);
    }
    bytes = 0;
    errors = 0;
    for (i = 0; i < n; i++) {
        s = engine ? blk->Packet[i].Status : status;
        len = engine ? blk->Packet[i].LengthTransferred : 0;
        if (s == XHCI_USBD_STATUS_DATA_UNDERRUN) {
            s = XHCI_USBD_STATUS_SUCCESS;
        }
        if (s != XHCI_USBD_STATUS_SUCCESS) {
            errors++;
        }
        it->IsoPacket[i].Status = s;
        it->IsoPacket[i].Length = x->In ? len : 0;
        bytes += len;
    }
    it->ErrorCount = errors;
    *bytesOut = bytes;
    if (!engine || status == HCD_USBD_CANCELED ||
        status == HCD_USBD_DEVICE_GONE) {
        return status;
    }
    return (errors == n) ? HCD_USBD_ISOCH_REQUEST_FAILED
                         : XHCI_USBD_STATUS_SUCCESS;
}

/* An isochronous URB refused before a record was filled: every packet
 * says so, as hcdIsoComplete would have written it. Only for a URB whose
 * packet count hcd_urb.c accepted. */
VOID HcdIoIsoRefused(PVOID urb, LONG usbd)
{
    struct _URB_ISOCH_TRANSFER *it;
    ULONG fixed;
    ULONG room;
    ULONG i;

    it = &((PURB)urb)->UrbIsochronousTransfer;
    if (it->Hdr.Function != URB_FUNCTION_ISOCH_TRANSFER) {
        return;
    }
    /* Only what the URB's own length holds: a refusal may be of a URB too
     * short for its fixed fields, or whose NumberOfPackets failed
     * validation (round 17, finding 2). */
    fixed = FIELD_OFFSET(struct _URB_ISOCH_TRANSFER, IsoPacket);
    if (it->Hdr.Length < fixed) {
        return;
    }
    room = (it->Hdr.Length - fixed) / sizeof(USBD_ISO_PACKET_DESCRIPTOR);
    for (i = 0; i < it->NumberOfPackets && i < room; i++) {
        it->IsoPacket[i].Status = usbd;
        it->IsoPacket[i].Length = 0;
    }
    it->ErrorCount = it->NumberOfPackets;
}

/*
 * Fill a claimed record from its request: the SETUP bytes, the direction,
 * the MDL - the client's, or one built here over its buffer - and the first
 * chunk. Returns the USBD status that refuses the request, or success. The
 * MDL is the only one the transfer maps, from its start, as usbport maps it
 * (ReactOS usbport.c USBPORT_MapTransfer, static): URB clients on these
 * targets pass no chained MDLs, and a length past the MDL's own byte count
 * is refused rather than mapped past its page list. IRQL: <= DISPATCH_LEVEL,
 * no lock held.
 */
static LONG hcdFill(PHCD_CONTROLLER hc, PHCD_XFER x, PVOID urb,
                    const HCD_IO_REQUEST *req)
{
    LONG usbd;
    ULONG isochPipe;
    ULONG i;

    x->Urb = urb;
    x->Mdl = NULL;
    x->OwnMdl = 0;
    x->MapBase = NULL;
    x->MapCount = 0;
    if (req == NULL) {
        (VOID)InterlockedIncrement((PLONG)&hc->Counters.UrbsMalformed);
        return HCD_USBD_INVALID_PARAMETER;
    }
    x->Control = req->Control;
    x->In = (req->Flags & HCD_IO_IN) != 0;
    x->ShortOk = (req->Flags & HCD_IO_SHORT_OK) != 0;
    x->Isoch = (req->Flags & HCD_IO_ISOCH) != 0;
    x->Asap = (req->Flags & HCD_IO_ASAP) != 0;
    if (!req->Control) {
        /* A URB's function must match its pipe's type: an isochronous URB
         * only on an isochronous pipe and a bulk or interrupt one on any
         * other, the default pipe never - a Normal or Isoch TD on a
         * control ring is a TRB Error at best. */
        isochPipe = (x->Pipe->TransferType == XHCI_PIPE_XFER_ISOCH) ? 1UL
                                                                    : 0UL;
        if (x->Pipe->TransferType == XHCI_PIPE_XFER_CONTROL ||
            x->Isoch != isochPipe) {
            if (x->Isoch || isochPipe) {
                (VOID)InterlockedIncrement(
                    (PLONG)&hc->Hc.IsoSubmitsWrongType);
            }
            return HCD_USBD_INVALID_PIPE;
        }
        /* The endpoint's direction, never the client's flag: usbport sets
         * the flag from the endpoint for every non-control pipe (ReactOS
         * usbport urb.c:393-401), and the map's WriteToDevice and the
         * engine's direction check both follow x->In. */
        x->In = x->Pipe->Ep.DirectionIn ? 1UL : 0UL;
    }
    if (req->Control) {
        x->Setup.bmRequestType = req->Setup[0];
        x->Setup.bRequest = req->Setup[1];
        x->Setup.wValue = (USHORT)(req->Setup[2] | (req->Setup[3] << 8));
        x->Setup.wIndex = (USHORT)(req->Setup[4] | (req->Setup[5] << 8));
        x->Setup.wLength = (USHORT)(req->Setup[6] | (req->Setup[7] << 8));
    }
    x->Length = req->Length;
    x->Offset = 0;
    x->Chunk = 0;
    x->Status = XHCI_USBD_STATUS_SUCCESS;
    x->Engine = 0;
    x->LengthOut = req->LengthOut;
    x->Sg.List.SgElementCount = 0;
    for (i = 0; i < sizeof(x->Sg.List) - sizeof(x->Sg.List.SgElement);
         i++) {
        ((PUCHAR)&x->Sg.List)[i] = 0;
    }
    if (x->Isoch) {
        usbd = hcdIsoAdmit(hc, x);
        if (usbd != XHCI_USBD_STATUS_SUCCESS) {
            return usbd;
        }
    }
    if (req->Length == 0) {
        return XHCI_USBD_STATUS_SUCCESS;
    }
    if (req->Mdl != NULL) {
        if (MmGetMdlByteCount(req->Mdl) < req->Length) {
            (VOID)InterlockedIncrement((PLONG)&hc->UrbsMdlShort);
            (VOID)InterlockedIncrement((PLONG)&hc->Counters.UrbsMalformed);
            return HCD_USBD_INVALID_PARAMETER;
        }
        x->Mdl = req->Mdl;
    } else if (req->Buffer != NULL) {
        x->Mdl = HcdPoolMdlBuild(req->Buffer, req->Length);
        if (x->Mdl == NULL) {
            return HCD_USBD_NO_MEMORY;
        }
        x->OwnMdl = 1;
    } else {
        (VOID)InterlockedIncrement((PLONG)&hc->Counters.UrbsMalformed);
        return HCD_USBD_INVALID_PARAMETER;
    }
    if (!hcdPlanChunk(hc, x)) {
        if (x->OwnMdl) {
            HcdPoolMdlFree(x->Mdl);
        }
        x->Mdl = NULL;
        x->OwnMdl = 0;
        if (x->Isoch) {
            (VOID)InterlockedIncrement((PLONG)&hc->Hc.IsoRefusalsTooLarge);
        }
        return HCD_USBD_INVALID_PARAMETER;
    }
    return XHCI_USBD_STATUS_SUCCESS;
}

/*
 * A filled record's IRP made cancellable and the record started. A request
 * split into chunks owns the pipe from its start, and a request started
 * while another owns it waits here unmapped, on the pipe's Held list: it
 * holds no map registers the owner's next chunk may need (round 3, finding
 * 8), and it cannot land between two of the owner's chunks (round 2,
 * finding 7). HcdIoPipeRelease starts it. IRQL: <= DISPATCH_LEVEL, no lock
 * held.
 */
static VOID hcdGo(PHCD_CONTROLLER hc, PHCD_XFER x)
{
    PHCD_PIPE pipe;
    PHCD_USB_DEVICE dev;
    KIRQL oldIrql;

    pipe = x->Pipe;
    dev = pipe->Device;
    hcdCancelOn(x->Irp, x);
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    if (x->CancelRequested || pipe->Closed || dev->Gone) {
        /* Cancelled, or its pipe or device going, while it was being
         * filled in: it completes now and is never held, so no drain the
         * thread has already passed can miss it (round 4, finding 2). */
        x->Engine = 0;
        x->Status = dev->Gone ? HCD_USBD_DEVICE_GONE : HCD_USBD_CANCELED;
        x->State = HCD_XFER_DONE;
        InsertTailList(&hc->DoneList, &x->Link);
        XhciControllerLockRelease(&hc->Hc, oldIrql);
        HcdIoDeferred(hc);
        return;
    }
    if (pipe->Exclusive != NULL || pipe->Paused) {
        /* Held unmapped: behind the pipe's owner, or while the thread has
         * it paused - holding no map registers either way, so an abort's
         * cancelled work never waits for registers a held request keeps
         * (round 4, finding 4). */
        x->State = HCD_XFER_HELD;
        InsertTailList(&pipe->Held, &x->Link);
        XhciControllerLockRelease(&hc->Hc, oldIrql);
        return;
    }
    if (x->Length != 0 && x->Chunk < x->Length) {
        pipe->Exclusive = x;
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    hcdStart(hc, x);
}

/*
 * Launch a claimed record for its IRP, which holds a device reference and
 * is counted in its PDO's UrbsPending, its PDO in x->Pdo. A request that
 * cannot be filled is refused at the next tick and the record goes to the
 * next waiting IRP, which is launched in turn - a loop, so a run of
 * refusals never recurses. `req` NULL refuses. IRQL: <= DISPATCH_LEVEL, no
 * lock held.
 */
static VOID hcdLaunch(PHCD_CONTROLLER hc, PHCD_XFER x, PVOID urb,
                      const HCD_IO_REQUEST *req)
{
    HCD_IO_REQUEST next;
    PHCD_PIPE pipe;
    PHCD_USB_DEVICE dev;
    PHCD_DEVICE_PDO pdo;
    PIRP irp;
    PIRP nextIrp;
    KIRQL oldIrql;
    LONG usbd;

    pipe = x->Pipe;
    dev = pipe->Device;
    for (;;) {
        usbd = hcdFill(hc, x, urb, req);
        if (usbd == XHCI_USBD_STATUS_SUCCESS) {
            hcdGo(hc, x);
            return;
        }
        irp = x->Irp;
        pdo = x->Pdo;
        x->Pdo = NULL;
        x->Urb = NULL;
        if (req != NULL && (req->Flags & HCD_IO_ISOCH) != 0) {
            HcdIoIsoRefused(urb, usbd);
        }
        XhciControllerLockAcquire(&hc->Hc, &oldIrql);
        nextIrp = hcdRecordRelease(pipe, x);
        XhciControllerLockRelease(&hc->Hc, oldIrql);
        (VOID)HcdIoRefuseLater(pdo, irp, urb, usbd);
        (VOID)InterlockedDecrement(&pdo->UrbsPending);
        /* The record and its pipe stay with nextIrp's reference; with no
         * next IRP neither is touched after this. */
        (VOID)InterlockedDecrement(&dev->Refs);
        if (nextIrp == NULL) {
            return;
        }
        x->Pdo = (PHCD_DEVICE_PDO)nextIrp->Tail.Overlay.DriverContext[2];
        urb = IoGetCurrentIrpStackLocation(nextIrp)
                  ->Parameters.Others.Argument1;
        req = (HcdUrbIoRequest(urb, &next) == XHCI_USBD_STATUS_SUCCESS)
                  ? &next
                  : NULL;
    }
}

/*
 * Submit one transfer for `irp`, as `req` describes it, on `dev`, on which
 * the caller holds a reference (hcd_urb.c). The handle is resolved and a
 * record taken - or the IRP parked behind the pipe's waiting IRPs - in one
 * hold of the controller lock, so a pipe the thread is closing cannot be
 * freed between the two. The reference passes to the IRP: it is returned at
 * completion, here on a refusal or later in HcdIoDeferred. Returns
 * STATUS_PENDING, the IRP completed either way, never inside this call.
 */
NTSTATUS HcdIoSubmit(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                     PHCD_DEVICE_PDO pdo, PIRP irp, PVOID urb,
                     const HCD_IO_REQUEST *req)
{
    PHCD_PIPE pipe;
    PHCD_XFER x;
    PHCD_XFER kick;
    KIRQL oldIrql;
    ULONG gone;
    ULONG parked;

    x = NULL;
    kick = NULL;
    parked = 0;
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    gone = dev->Gone;
    pipe = gone ? NULL : hcdPipeFromHandle(dev, req->Handle);
    if (pipe != NULL && pipe->Closed) {
        pipe = NULL;
    }
    if (pipe != NULL && !HcdPdoOwnsPipe(pdo, dev, pipe)) {
        /* A sibling function's handle (design record 13 section 10.9). */
        pipe = NULL;
    }
    if (pipe != NULL) {
        /* The PDO rides on the IRP from here, under this lock, so a
         * function's cancel (HcdIoCancelPdo) finds a record by its IRP
         * before x->Pdo is written, which happens after the lock. */
        irp->Tail.Overlay.DriverContext[2] = pdo;
        if (IsListEmpty(&pipe->Waiting)) {
            x = hcdTakeRecord(pipe, irp);
        }
        if (x == NULL) {
            parked = hcdPark(hc, pipe, pdo, irp);
            if (parked) {
                kick = hcdWaitingStart(hc, pipe);
            }
        }
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    if (kick != NULL) {
        hcdWaitingLaunch(hc, kick);
    }
    if (gone) {
        (VOID)InterlockedIncrement((PLONG)&hc->UrbsGone);
        (VOID)InterlockedDecrement(&dev->Refs);
        return HcdIoRefuseLater(pdo, irp, urb, HCD_USBD_DEVICE_GONE);
    }
    if (pipe == NULL) {
        return hcdRefuse(hc, dev, pdo, irp, (PURB)urb, HCD_USBD_INVALID_PIPE);
    }
    if (x == NULL) {
        if (parked) {
            return STATUS_PENDING;
        }
        return hcdRefuse(hc, dev, pdo, irp, (PURB)urb, HCD_USBD_CANCELED);
    }

    /* Counted on the PDO until completed: its REMOVE waits the count out,
     * so no completion can reach a client driver that has unloaded
     * (c4-2k: 0xCE in hidusb on an unplug, 2026-10-03). */
    x->Pdo = pdo;
    (VOID)InterlockedIncrement(&pdo->UrbsPending);
    IoMarkIrpPending(irp);
    hcdLaunch(hc, x, urb, req);
    return STATUS_PENDING;
}

/*
 * A record whose chunk is mapped (or needs no mapping): publish it through
 * the engine and ring the doorbell, or, when the mapping failed, the device
 * has gone, the pipe closed, the IRP was cancelled or the ring refused it,
 * put it straight on the done list. Called from the map pump's execution
 * routine, from HcdIoSubmit, or for a held record from HcdIoPipeRelease.
 *
 * Two gates hold a record mapped but unpublished, on the pipe's Held list
 * (Codex review of batch (c), round 2, findings 2 and 7): the thread has the
 * pipe Paused while it stops or edits the endpoint, and a request split into
 * chunks owns the pipe (Exclusive) until its last chunk ends, so no other
 * request's TD lands between two of its chunks. The doorbell's slot and DCI
 * are read under the lock: once it is released the record may complete and
 * its pipe be freed (finding 3).
 *
 * IRQL: <= DISPATCH_LEVEL, no lock held.
 */
VOID HcdIoMapped(PHCD_CONTROLLER hc, PHCD_XFER x, ULONG ok)
{
    PHCD_PIPE pipe;
    PHCD_USB_DEVICE dev;
    XHCI_CONTROL_REQUEST creq;
    XHCI_NORMAL_REQUEST nreq;
    XHCI_TRB trbs[XHCI_XFER_MAX_CONTROL_TRBS];
    KIRQL oldIrql;
    ULONG answer;
    ULONG slot;
    ULONG dci;
    ULONG i;

    pipe = x->Pipe;
    dev = pipe->Device;
    answer = XHCI_XFER_BAD_PARAM;
    if (ok && x->Isoch && !hcdIsoFill(x)) {
        ok = 0;
        x->Status = HCD_USBD_INVALID_PARAMETER;
    }
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    if (ok && !dev->Gone && !pipe->Closed && !x->CancelRequested &&
        (pipe->Paused ||
         (pipe->Exclusive != NULL && pipe->Exclusive != x &&
          x->Seq > pipe->Exclusive->Seq) ||
         (x->Isoch && pipe->RingWait && pipe->Queue->Count != 0))) {
        /* Held mapped: the thread has the pipe paused, or a request
         * submitted before this one owns it, or an earlier isochronous
         * request waits for ring room. A request submitted before the
         * owner is not held - it goes first, as it was asked first. */
        x->State = HCD_XFER_HELD;
        x->Mapped = 1;
        InsertTailList(&pipe->Held, &x->Link);
        XhciControllerLockRelease(&hc->Hc, oldIrql);
        return;
    }
    if (ok && !dev->Gone && !pipe->Closed && !x->CancelRequested) {
        if (x->Control) {
            for (i = 0; i < sizeof(creq); i++) {
                ((PUCHAR)&creq)[i] = 0;
            }
            creq.Setup = x->Setup;
            creq.TransferLength = x->Chunk;
            creq.TransferFlagsIn = x->In;
            creq.MaxPacketSize = dev->Mps0;
            creq.SgList = &x->Sg.List;
            answer = XhciXferSubmitControl(pipe->Queue, pipe->Ring, &creq,
                                           &x->Xfer, x, trbs,
                                           XHCI_XFER_MAX_CONTROL_TRBS);
        } else if (x->Isoch) {
            answer = hcdIsoPublish(hc, pipe, x);
        } else {
            for (i = 0; i < sizeof(nreq); i++) {
                ((PUCHAR)&nreq)[i] = 0;
            }
            nreq.TransferLength = x->Chunk;
            nreq.DirectionIn = (pipe->EndpointAddress & 0x80UL) != 0;
            nreq.MaxPacketSize = pipe->MaxPacketSize;
            nreq.SgList = &x->Sg.List;
            answer = XhciXferSubmitNormal(pipe->Queue, pipe->Ring, &nreq,
                                          x->In, &x->Xfer, x, trbs,
                                          XHCI_XFER_MAX_CONTROL_TRBS);
        }
    }
    if (answer == XHCI_XFER_OK) {
        x->State = HCD_XFER_ON_RING;
        x->Engine = 1;
        hc->Counters.TransfersSubmitted++;
        /* The doorbell under the lock: once it is released the thread may
         * pause and stop the endpoint, and a doorbell rung after that
         * would restart it under the thread's edit (Codex review of batch
         * (c), round 3, finding 2). A doorbell is one register write. */
        slot = dev->SlotId;
        dci = pipe->Dci;
        XhciWriteDoorbell(&hc->Hc, slot, dci);
        XhciControllerLockRelease(&hc->Hc, oldIrql);
        return;
    }
    if (answer == XHCI_XFER_BUSY && x->Isoch && pipe->Queue->Count != 0) {
        /* The ring still holds the pipe's earlier groups: this one waits
         * for room, mapped, and the pipe's next completion releases it
         * (HcdIoDeferred). Failing it would be a gap in the stream. */
        pipe->RingWait = 1;
        x->State = HCD_XFER_HELD;
        x->Mapped = 1;
        InsertTailList(&pipe->Held, &x->Link);
        XhciControllerLockRelease(&hc->Hc, oldIrql);
        return;
    }
    if (x->Status == XHCI_USBD_STATUS_SUCCESS) {
        x->Status = dev->Gone      ? HCD_USBD_DEVICE_GONE
                    : (pipe->Closed || x->CancelRequested)
                        ? HCD_USBD_CANCELED
                    : answer == XHCI_XFER_BUSY ? HCD_USBD_ERROR_BUSY
                    : (answer == XHCI_XFER_ISO_MALFORMED ||
                       answer == XHCI_XFER_ISO_TOO_LARGE)
                        ? HCD_USBD_INVALID_PARAMETER
                        : HCD_USBD_INTERNAL_HC_ERROR;
    }
    x->Engine = 0;
    x->State = HCD_XFER_DONE;
    InsertTailList(&hc->DoneList, &x->Link);
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    HcdIoDeferred(hc);
}

/*
 * Give the held records of a pipe their turn, in submission order, once
 * the thread is not pausing it. A record held mapped goes back through
 * HcdIoMapped's gate; the owner of the pipe always passes (it can be held
 * mapped while paused, and nothing else would release it - round 3, finding
 * 4), and so does any record submitted before it. A record held unmapped
 * waits for no owner: the first one starts, taking the pipe if it is split
 * into chunks, and the walk stops there. IRQL: <= DISPATCH_LEVEL, no lock
 * held.
 */
VOID HcdIoPipeRelease(PHCD_CONTROLLER hc, PHCD_PIPE pipe)
{
    LIST_ENTRY mine;
    PLIST_ENTRY entry;
    PLIST_ENTRY next;
    PHCD_XFER owner;
    PHCD_XFER x;
    KIRQL oldIrql;

    InitializeListHead(&mine);
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    if (!pipe->Paused) {
        for (entry = pipe->Held.Flink; entry != &pipe->Held; entry = next) {
            next = entry->Flink;
            x = CONTAINING_RECORD(entry, HCD_XFER, Link);
            owner = pipe->Exclusive;
            if (x->Mapped) {
                if (owner != NULL && x != owner && x->Seq > owner->Seq) {
                    continue;
                }
            } else {
                if (owner != NULL) {
                    continue;
                }
                if (x->Length != 0 && x->Chunk < x->Length) {
                    pipe->Exclusive = x;
                }
            }
            RemoveEntryList(entry);
            InsertTailList(&mine, entry);
        }
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    while (!IsListEmpty(&mine)) {
        entry = RemoveHeadList(&mine);
        x = CONTAINING_RECORD(entry, HCD_XFER, Link);
        if (x->Mapped) {
            x->Mapped = 0;
            HcdIoMapped(hc, x, 1);
        } else {
            hcdStart(hc, x);
        }
    }
}

/*
 * The thread's gate around a stop or an edit of the endpoint (hcd_cfg.c):
 * Pause holds every record that reaches HcdIoMapped meanwhile; Resume lets
 * them go. IRQL: <= DISPATCH_LEVEL.
 */
VOID HcdIoPipePause(PHCD_CONTROLLER hc, PHCD_PIPE pipe)
{
    KIRQL oldIrql;

    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    pipe->Paused++;
    XhciControllerLockRelease(&hc->Hc, oldIrql);
}

VOID HcdIoPipeResume(PHCD_CONTROLLER hc, PHCD_PIPE pipe)
{
    KIRQL oldIrql;

    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    if (pipe->Paused != 0) {
        pipe->Paused--;
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    HcdIoPipeRelease(hc, pipe);
}

/*
 * Mark every record a request holds on the pipe as cancelled, wherever it
 * is - in the map pump, held, or on the ring - and send the held ones to the
 * done list now (the ring's are the caller's to take off once the endpoint
 * is stopped). Returns how many were marked. Controller lock NOT held.
 */
ULONG HcdIoPipeCancelAll(PHCD_CONTROLLER hc, PHCD_PIPE pipe)
{
    PHCD_XFER x;
    PLIST_ENTRY entry;
    LIST_ENTRY waiting;
    KIRQL oldIrql;
    ULONG marked;
    ULONG i;

    marked = 0;
    InitializeListHead(&waiting);
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    /* The waiting IRPs were submitted before the abort, so they go with
     * it; an IRP submitted after it may take a record or wait. */
    hcdWaitingFlush(pipe, &waiting);
    for (i = 0; i < HCD_PIPE_XFERS; i++) {
        x = &pipe->Xfers[i];
        if (x->State != HCD_XFER_FREE && x->Irp != NULL) {
            x->CancelRequested = 1;
            marked++;
        }
    }
    while (!IsListEmpty(&pipe->Held)) {
        entry = RemoveHeadList(&pipe->Held);
        x = CONTAINING_RECORD(entry, HCD_XFER, Link);
        x->Engine = 0;
        x->Status = HCD_USBD_CANCELED;
        x->State = HCD_XFER_DONE;
        InsertTailList(&hc->DoneList, &x->Link);
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    hcdWaitingRefuse(&waiting, pipe->Device, HCD_USBD_CANCELED);
    HcdIoDeferred(hc);
    return marked;
}

/*
 * A function PDO stopping or being removed (hcd_pdo.c): every request of
 * that PDO on any pipe of the device - EP0's included, which its siblings
 * share - is marked as its own cancel routine would mark it (hcdCancel),
 * and its waiting IRPs leave their lists to complete as cancelled; the
 * siblings' requests are not touched. The thread then takes the marked
 * ones off each pipe with the survivors kept (hcd_cfg.c, hcdCfgCancelPipe).
 * A record is matched by its IRP's DriverContext[2], which HcdIoSubmit and
 * the parking write under the controller lock, not by x->Pdo, which a
 * record handed to a waiting IRP gets only after the lock's release; under
 * the lock a record that is not free holds an IRP not yet completed
 * (hcdRecordRelease). The quiesce closed admission and waited out the
 * dispatches first. IRQL: PASSIVE_LEVEL, a device reference held.
 */
VOID HcdIoCancelPdo(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                    PHCD_DEVICE_PDO pdo)
{
    LIST_ENTRY waiting;
    PLIST_ENTRY entry;
    PLIST_ENTRY next;
    PHCD_PIPE pipe;
    PHCD_XFER x;
    PIRP irp;
    KIRQL oldIrql;
    ULONG marked;
    ULONG dci;
    ULONG i;

    InitializeListHead(&waiting);
    marked = 0;
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    for (dci = 1; dci < 32; dci++) {
        pipe = (dci == 1) ? &dev->Ep0Pipe : dev->Pipes[dci];
        if (pipe == NULL) {
            continue;
        }
        for (entry = pipe->Waiting.Flink; entry != &pipe->Waiting;
             entry = next) {
            next = entry->Flink;
            irp = CONTAINING_RECORD(entry, IRP, Tail.Overlay.ListEntry);
            /* One whose routine already runs is left to it
             * (hcdWaitCancel). */
            if (irp->Tail.Overlay.DriverContext[2] == pdo &&
                IoSetCancelRoutine(irp, NULL) != NULL) {
                RemoveEntryList(entry);
                InsertTailList(&waiting, entry);
            }
        }
        for (i = 0; i < HCD_PIPE_XFERS; i++) {
            x = &pipe->Xfers[i];
            if (x->State != HCD_XFER_FREE && x->Irp != NULL &&
                x->Irp->Tail.Overlay.DriverContext[2] == pdo) {
                x->CancelRequested = 1;
                pipe->CancelPending = 1;
                marked = 1;
            }
        }
    }
    if (marked) {
        hc->CancelWork = 1;
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    hcdWaitingRefuse(&waiting, dev, HCD_USBD_CANCELED);
    XHCI_DBG_VALUE("hcd: function PDO requests cancelled, MI",
                   pdo->Func.FirstInterface);
    HcdThreadWake(hc);
}

/* Until no record of the pipe is still marked cancelled - each clears its
 * mark when it completes. A record in the map pump completes as CANCELED
 * through HcdIoMapped. IRQL: PASSIVE_LEVEL. */
VOID HcdIoPipeWaitCancelled(PHCD_CONTROLLER hc, PHCD_PIPE pipe)
{
    LARGE_INTEGER due;
    PLIST_ENTRY entry;
    PLIST_ENTRY next;
    PHCD_XFER x;
    KIRQL oldIrql;
    ULONG busy;
    ULONG i;

    for (;;) {
        /* A cancelled record that reached the held list after the drain
         * completes from here - the pipe is paused, so nothing else would
         * release it (round 4, finding 2). */
        XhciControllerLockAcquire(&hc->Hc, &oldIrql);
        for (entry = pipe->Held.Flink; entry != &pipe->Held; entry = next) {
            next = entry->Flink;
            x = CONTAINING_RECORD(entry, HCD_XFER, Link);
            if (x->CancelRequested) {
                RemoveEntryList(entry);
                x->Engine = 0;
                x->Status = HCD_USBD_CANCELED;
                x->State = HCD_XFER_DONE;
                InsertTailList(&hc->DoneList, &x->Link);
            }
        }
        XhciControllerLockRelease(&hc->Hc, oldIrql);
        busy = 0;
        for (i = 0; i < HCD_PIPE_XFERS; i++) {
            if (pipe->Xfers[i].State != HCD_XFER_FREE &&
                pipe->Xfers[i].CancelRequested) {
                busy = 1;
            }
        }
        if (!busy) {
            break;
        }
        HcdRelativeMs(&due, 1);
        (VOID)KeDelayExecutionThread(KernelMode, FALSE, &due);
        HcdIoDeferred(hc);
    }
}

/* ----------------------------------------------------------------------- */
/* Completion                                                               */
/* ----------------------------------------------------------------------- */

/* The engine retired one of the URB path's records (hcd_dev.c, from the
 * event DPC or a settle). Controller lock held. */
VOID HcdIoRetired(PHCD_CONTROLLER hc, PXHCI_TRANSFER t)
{
    PHCD_XFER x;

    x = (PHCD_XFER)t;
    x->State = HCD_XFER_DONE;
    InsertTailList(&hc->DoneList, &x->Link);
}

/*
 * Everything on the done list: give the chunk's map registers back, start
 * the next chunk of a bulk or interrupt transfer that moved all it asked
 * for, or complete the IRP - the URB's length and status written, its MDL
 * freed if this driver built it, the record freed under the lock, and the
 * device reference returned last. IRQL: <= DISPATCH_LEVEL, no lock held.
 */
VOID HcdIoDeferred(PHCD_CONTROLLER hc)
{
    PLIST_ENTRY entry;
    PHCD_XFER x;
    PHCD_USB_DEVICE dev;
    PURB urb;
    PIRP irp;
    KIRQL oldIrql;
    LONG status;
    PHCD_DEVICE_PDO pdo;
    PIRP nextIrp;
    HCD_IO_REQUEST req;
    ULONG bytes;
    ULONG more;
    ULONG release;
    ULONG engine;

    for (;;) {
        XhciControllerLockAcquire(&hc->Hc, &oldIrql);
        if (IsListEmpty(&hc->DoneList)) {
            XhciControllerLockRelease(&hc->Hc, oldIrql);
            break;
        }
        entry = RemoveHeadList(&hc->DoneList);
        XhciControllerLockRelease(&hc->Hc, oldIrql);

        x = CONTAINING_RECORD(entry, HCD_XFER, Link);
        dev = x->Pipe->Device;
        HcdDmaUnmap(hc, x);
        engine = x->Engine;
        if (engine) {
            status = x->Xfer.UsbdStatus;
            bytes = x->Xfer.BytesTransferred;
        } else {
            status = x->Status;
            bytes = 0;
        }
        if (x->Isoch) {
            status = hcdIsoComplete(x, engine, status, &bytes);
            x->Offset = bytes;
        } else {
            if (bytes > x->Chunk) {
                bytes = x->Chunk;
            }
            x->Offset += bytes;

            more = status == XHCI_USBD_STATUS_SUCCESS && !x->Control &&
                   x->Engine && bytes == x->Chunk && x->Offset < x->Length &&
                   !dev->Gone && !x->Pipe->Closed && !x->CancelRequested;
            if (more && hcdPlanChunk(hc, x)) {
                x->State = HCD_XFER_MAPPING;
                x->Engine = 0;
                HcdDmaMapQueue(hc, x);
                continue;
            }
            if (x->CancelRequested && status == XHCI_USBD_STATUS_SUCCESS &&
                x->Offset < x->Length) {
                status = HCD_USBD_CANCELED;
            } else if (status == XHCI_USBD_STATUS_SUCCESS && !x->Control &&
                       !x->ShortOk && x->Offset < x->Length) {
                /* A short transfer the client did not allow (Windows 2000
                 * DDK usbdi.h:282-284; round 2, finding 11). */
                status = HCD_USBD_ERROR_SHORT_TRANSFER;
            }
        }

        urb = (PURB)x->Urb;
        irp = x->Irp;
        pdo = x->Pdo;
        x->Pdo = NULL;
        if (x->LengthOut != NULL) {
            *x->LengthOut = x->Offset;
        }
        urb->UrbHeader.Status = status;
        if (x->OwnMdl) {
            HcdPoolMdlFree(x->Mdl);
        }
        x->Mdl = NULL;
        x->OwnMdl = 0;
        x->Urb = NULL;
        hcdCancelOff(irp);
        /* The owner's release runs while this record is still not free:
         * a record not free keeps its pipe from being closed and freed
         * (HcdIoWaitPipe), so the pipe is alive for the call (round 3,
         * finding 6). Any end of a record also releases one that waited
         * for ring room on the pipe. */
        XhciControllerLockAcquire(&hc->Hc, &oldIrql);
        release = (x->Pipe->Exclusive == x) || x->Pipe->RingWait;
        if (x->Pipe->Exclusive == x) {
            x->Pipe->Exclusive = NULL;
        }
        x->Pipe->RingWait = 0;
        XhciControllerLockRelease(&hc->Hc, oldIrql);
        if (release) {
            HcdIoPipeRelease(hc, x->Pipe);
        }
        XhciControllerLockAcquire(&hc->Hc, &oldIrql);
        nextIrp = hcdRecordRelease(x->Pipe, x);
        XhciControllerLockRelease(&hc->Hc, oldIrql);

        if (!engine && status != XHCI_USBD_STATUS_SUCCESS) {
            /* Failed before the ring had it - a refused submit, a mapping
             * that failed, a cancel while it was filled in. This call may
             * be inside the client's submission, so the IRP completes at
             * the next tick (HcdIoRefuseLater), never here: a client that
             * resubmits from its completion routine would recurse (static
             * sweep of batch (c), after Codex round 8). */
            (VOID)HcdIoRefuseLater(pdo, irp, urb, status);
        } else {
            irp->IoStatus.Status =
                (NTSTATUS)XhciPipeNtStatus((ULONG)status);
            irp->IoStatus.Information = 0;
            IoCompleteRequest(irp, IO_NO_INCREMENT);
        }
        (VOID)InterlockedIncrement((PLONG)&hc->UrbsCompleted);
        (VOID)InterlockedDecrement(&pdo->UrbsPending);
        (VOID)InterlockedDecrement(&dev->Refs);
        if (nextIrp != NULL) {
            /* The record went to a waiting IRP, whose own reference keeps
             * the device and pipe. */
            x->Pdo = (PHCD_DEVICE_PDO)nextIrp->Tail.Overlay.DriverContext[2];
            urb = (PURB)IoGetCurrentIrpStackLocation(nextIrp)
                      ->Parameters.Others.Argument1;
            hcdLaunch(hc, x, urb,
                      HcdUrbIoRequest(urb, &req) == XHCI_USBD_STATUS_SUCCESS
                          ? &req
                          : NULL);
        }
    }
}

/* ----------------------------------------------------------------------- */
/* A device leaving                                                         */
/* ----------------------------------------------------------------------- */


/*
 * Take every transfer off one pipe's engine queue and complete it with
 * `usbd`. Only once the controller owns none of the pipe's TRBs: its
 * endpoint stopped, deconfigured or disabled, or the slot gone. The
 * thread's own EP0 record is dropped with the rest; it waits on nothing by
 * then. IRQL: PASSIVE_LEVEL.
 */
VOID HcdIoDrainPipe(PHCD_CONTROLLER hc, PHCD_PIPE pipe, LONG usbd)
{
    PXHCI_TRANSFER t;
    PXHCI_TRANSFER next;
    PLIST_ENTRY entry;
    LIST_ENTRY waiting;
    PHCD_XFER x;
    KIRQL oldIrql;
    ULONG count;

    InitializeListHead(&waiting);
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    hcdWaitingFlush(pipe, &waiting);
    /* The held records too: they are on neither the engine's queue nor
     * the done list, and a closed or gone pipe never releases them (Codex
     * review of batch (c), round 3, finding 5). */
    while (!IsListEmpty(&pipe->Held)) {
        entry = RemoveHeadList(&pipe->Held);
        x = CONTAINING_RECORD(entry, HCD_XFER, Link);
        x->Engine = 0;
        x->Status = usbd;
        x->State = HCD_XFER_DONE;
        InsertTailList(&hc->DoneList, &x->Link);
    }
    t = XhciXferQueueDrain(pipe->Queue, usbd, &count);
    while (t != NULL) {
        next = t->Next;
        if (t != &pipe->Device->Ep0Xfer) {
            t->UsbdStatus = usbd;
            hc->Counters.TransfersCancelled++;
            HcdIoRetired(hc, t);
        }
        t = next;
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    hcdWaitingRefuse(&waiting, pipe->Device, usbd);
    HcdIoDeferred(hc);
}

/* Until every record of the pipe is free - a record still in the map pump
 * finishes through HcdIoMapped, which sees Gone or Closed. IRQL:
 * PASSIVE_LEVEL. */
VOID HcdIoWaitPipe(PHCD_CONTROLLER hc, PHCD_PIPE pipe)
{
    LARGE_INTEGER due;
    KIRQL oldIrql;
    ULONG busy;
    ULONG i;

    for (;;) {
        /* A waiting IRP whose cancel routine was running at the drain is
         * still listed until the routine takes it. */
        XhciControllerLockAcquire(&hc->Hc, &oldIrql);
        busy = !IsListEmpty(&pipe->Waiting);
        XhciControllerLockRelease(&hc->Hc, oldIrql);
        if (hc->CancelsRunning != 0) {
            busy = 1;
        }
        for (i = 0; i < HCD_PIPE_XFERS; i++) {
            if (pipe->Xfers[i].State != HCD_XFER_FREE) {
                busy = 1;
            }
        }
        if (!busy) {
            break;
        }
        HcdRelativeMs(&due, 1);
        (VOID)KeDelayExecutionThread(KernelMode, FALSE, &due);
        HcdIoDeferred(hc);
    }
}

/*
 * The thread is freeing the record, with the slot disabled or taken by
 * HCRST, so the controller owns none of its TRBs: refuse new submissions,
 * drain every pipe, complete everything with DEVICE_GONE, and wait for the
 * references. The pipes themselves are freed by HcdCfgDeviceGone after.
 * IRQL: PASSIVE_LEVEL.
 */
/*
 * The drain half alone: refuse new submissions and complete everything the
 * device has queued, without waiting. A stop or a reset invalidation drains
 * every device first and waits after (HcdIoDeviceGone), so no device's wait
 * holds up another's drain - a request of one waiting for map registers
 * another's outstanding transfers keep would otherwise never end (Codex
 * review of batch (c), round 5, finding 3). Idempotent. Returns 0 when the
 * device must be kept (DMA not proven stopped). IRQL: PASSIVE_LEVEL.
 */
ULONG HcdIoDeviceDrain(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    KIRQL oldIrql;
    ULONG dci;

    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    dev->Gone = 1;
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    if (hc->CommonBufferPinned) {
        /* The controller could not be proven to have stopped mastering:
         * its rings still name the clients' buffers, so none goes back.
         * The URBs stay pending and the record stays allocated (Codex
         * review of batch (c), round 2, finding 8). */
        return 0;
    }
    HcdCfgFlushDevice(hc, dev);
    HcdIoDrainPipe(hc, &dev->Ep0Pipe, HCD_USBD_DEVICE_GONE);
    for (dci = 2; dci < 32; dci++) {
        if (dev->Pipes[dci] != NULL) {
            HcdIoDrainPipe(hc, dev->Pipes[dci], HCD_USBD_DEVICE_GONE);
        }
    }
    return 1;
}

ULONG HcdIoDeviceGone(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    LARGE_INTEGER due;

    if (!HcdIoDeviceDrain(hc, dev)) {
        return 0;
    }
    while (dev->Refs != 0 || hc->CancelsRunning != 0) {
        HcdRelativeMs(&due, 1);
        (VOID)KeDelayExecutionThread(KernelMode, FALSE, &due);
        HcdIoDeferred(hc);
    }
    return 1;
}

/* ----------------------------------------------------------------------- */
/* Refusals completed later                                                 */
/* ----------------------------------------------------------------------- */

/*
 * A URB this dispatch refuses is not completed inside the dispatch that
 * received it: Windows 98 SE's hidclass.sys resubmits its interrupt read
 * from that read's completion routine, so a refusal completed inline
 * recursed until the kernel stack overflowed and the guest reset (measured,
 * c8-98, 2026-10-03: a mouse unplugged, the trace one "refused" line after
 * another, then the guest gone). It is pended instead and completed by a
 * timer DPC at the next clock tick, which also paces a client's retries to
 * the tick. The timer, not the controller thread: the thread stops before
 * the devices' transfers are drained at a controller stop, and a drained
 * read's completion resubmits (Codex review of batch (c), round 8, finding
 * 1). Every refusal the URB path can reach comes here - device gone, PDO
 * closing, a closed pipe, no record free, no MDL (finding 4).
 *
 * The list, timer and DPC are the PDO's own, not the controller's: a PDO
 * can outlive its controller (orphaned while it awaits its REMOVE), and its
 * refusals must stay deferred then too, while the controller's storage
 * goes (Codex review of batch (c), round 9, findings 1 and 2). The list
 * lives under the cancel spin lock, so a refusal is cancellable like any
 * pended IRP (round 8, finding 3). Counted on the PDO apart from its URBs
 * (RefusedPending, which also holds one count while the timer is armed or
 * its DPC runs): a STOP or SURPRISE_REMOVAL waits for the URBs only, since
 * a client may keep resubmitting until its own stack hears of the stop; a
 * REMOVE, which reaches this PDO after every driver above has stopped
 * submitting, waits for both, and the PDO is never deleted before the
 * count is 0 (hcd_pdo.c). IRQL: <= DISPATCH_LEVEL.
 */
static VOID hcdRefusedCancel(PDEVICE_OBJECT obj, PIRP irp);

NTSTATUS HcdIoRefuseLater(PHCD_DEVICE_PDO pdo, PIRP irp, PVOID urb,
                          LONG usbd)
{
    LARGE_INTEGER due;
    KIRQL cancelIrql;

    ((PURB)urb)->UrbHeader.Status = usbd;
    irp->Tail.Overlay.DriverContext[2] = pdo;
    (VOID)InterlockedIncrement(&pdo->RefusedPending);
    IoMarkIrpPending(irp);
    IoAcquireCancelSpinLock(&cancelIrql);
    (VOID)IoSetCancelRoutine(irp, hcdRefusedCancel);
    InsertTailList(&pdo->RefusedIrps, &irp->Tail.Overlay.ListEntry);
    if (!pdo->RefuseArmed) {
        pdo->RefuseArmed = 1;
        (VOID)InterlockedIncrement(&pdo->RefusedPending);
        due.QuadPart = -10000;          /* 1 ms: the next tick */
        (VOID)KeSetTimer(&pdo->RefuseTimer, due, &pdo->RefuseDpc);
    }
    IoReleaseCancelSpinLock(cancelIrql);
    return STATUS_PENDING;
}

/* A refusal cancelled before its tick: completed here, as cancelled. The
 * DPC takes its IRPs off the list under the cancel spin lock and clears
 * their routines there, so one still listed is this routine's alone.
 * Called with the cancel spin lock held. */
static VOID hcdRefusedCancel(PDEVICE_OBJECT obj, PIRP irp)
{
    PHCD_DEVICE_PDO pdo;

    UNREFERENCED_PARAMETER(obj);
    RemoveEntryList(&irp->Tail.Overlay.ListEntry);
    IoReleaseCancelSpinLock(irp->CancelIrql);
    pdo = (PHCD_DEVICE_PDO)irp->Tail.Overlay.DriverContext[2];
    ((PURB)IoGetCurrentIrpStackLocation(irp)->Parameters.Others.Argument1)
        ->UrbHeader.Status = HCD_USBD_CANCELED;
    irp->IoStatus.Status = STATUS_CANCELLED;
    irp->IoStatus.Information = 0;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    (VOID)InterlockedDecrement(&pdo->RefusedPending);
}

/* The refusals listed at the tick - only those, so a client resubmitting
 * from its completion routine waits for the next one. IRQL:
 * DISPATCH_LEVEL. */
static VOID hcdRefusedDpc(PKDPC dpc, PVOID context, PVOID arg1, PVOID arg2)
{
    LIST_ENTRY mine;
    PLIST_ENTRY entry;
    PHCD_DEVICE_PDO pdo;
    PIRP irp;
    PURB urb;
    NTSTATUS status;
    KIRQL cancelIrql;

    UNREFERENCED_PARAMETER(dpc);
    UNREFERENCED_PARAMETER(arg1);
    UNREFERENCED_PARAMETER(arg2);
    pdo = (PHCD_DEVICE_PDO)context;
    InitializeListHead(&mine);
    IoAcquireCancelSpinLock(&cancelIrql);
    while (!IsListEmpty(&pdo->RefusedIrps)) {
        entry = RemoveHeadList(&pdo->RefusedIrps);
        irp = CONTAINING_RECORD(entry, IRP, Tail.Overlay.ListEntry);
        (VOID)IoSetCancelRoutine(irp, NULL);
        InsertTailList(&mine, entry);
    }
    pdo->RefuseArmed = 0;
    IoReleaseCancelSpinLock(cancelIrql);
    while (!IsListEmpty(&mine)) {
        entry = RemoveHeadList(&mine);
        irp = CONTAINING_RECORD(entry, IRP, Tail.Overlay.ListEntry);
        urb = (PURB)IoGetCurrentIrpStackLocation(irp)
                  ->Parameters.Others.Argument1;
        if (irp->Cancel) {
            /* The URB says what the IRP says (round 9, finding 3). */
            urb->UrbHeader.Status = HCD_USBD_CANCELED;
            status = STATUS_CANCELLED;
        } else if (pdo->Closing) {
            /*
             * A STOP or REMOVE is under way: STATUS_DELETE_PENDING, never
             * STATUS_DEVICE_NOT_CONNECTED. Windows 98 SE's hidclass.sys
             * answers a read failing with DEVICE_NOT_CONNECTED by failing
             * every client read and resubmitting at once while its device
             * is still started (0x110A7, then 0x10C20's state test at
             * 0x10C60, static), and it leaves that state only after the
             * REMOVE this PDO is waiting in returns - so the resubmissions
             * never stopped and the REMOVE never ended (c10/c11, a USB
             * keyboard and tablet unplugged, 2026-10-03). Any other
             * failure takes its one-second back-off instead.
             */
            status = STATUS_DELETE_PENDING;
        } else {
            status = (NTSTATUS)XhciPipeNtStatus(
                (ULONG)urb->UrbHeader.Status);
        }
        irp->IoStatus.Status = status;
        irp->IoStatus.Information = 0;
        IoCompleteRequest(irp, IO_NO_INCREMENT);
        (VOID)InterlockedDecrement(&pdo->RefusedPending);
    }
    /* The arm's count, last: once it is 0 the PDO may be deleted. */
    (VOID)InterlockedDecrement(&pdo->RefusedPending);
}

/* The PDO's list, timer and DPC, at its creation (hcd_pdo.c). IRQL:
 * PASSIVE_LEVEL. */
VOID HcdIoRefusedInit(PHCD_DEVICE_PDO pdo)
{
    InitializeListHead(&pdo->RefusedIrps);
    KeInitializeTimer(&pdo->RefuseTimer);
    KeInitializeDpc(&pdo->RefuseDpc, hcdRefusedDpc, pdo);
    pdo->RefuseArmed = 0;
    pdo->RefusedPending = 0;
}

/* Before the PDO is deleted, and at its REMOVE: every refusal completed,
 * the timer disarmed and its DPC returned. IRQL: PASSIVE_LEVEL. */
VOID HcdIoRefusedDrain(PHCD_DEVICE_PDO pdo)
{
    LARGE_INTEGER due;

    while (pdo->RefusedPending != 0) {
        HcdRelativeMs(&due, 1);
        (VOID)KeDelayExecutionThread(KernelMode, FALSE, &due);
    }
}
