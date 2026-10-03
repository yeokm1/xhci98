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
 * cannot be split. Returns 0 when the remainder cannot be mapped at all. */
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
        if (x->Control) {
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
    if (hc != NULL) {
        (VOID)InterlockedIncrement(&hc->CancelsRunning);
    }
    IoReleaseCancelSpinLock(Irp->CancelIrql);
    if (hc == NULL) {
        return;
    }

    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    if (x->Irp == Irp && x->State != HCD_XFER_FREE) {
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

/* A claimed record given back before anything was published. */
static VOID hcdFreeRecord(PHCD_CONTROLLER hc, PHCD_XFER x)
{
    KIRQL oldIrql;

    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    x->Irp = NULL;
    x->State = HCD_XFER_FREE;
    XhciControllerLockRelease(&hc->Hc, oldIrql);
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

static NTSTATUS hcdRefuse(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev, PIRP irp,
                          PURB urb, LONG usbd)
{
    urb->UrbHeader.Status = usbd;
    irp->IoStatus.Status = (NTSTATUS)XhciPipeNtStatus((ULONG)usbd);
    irp->IoStatus.Information = 0;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    (VOID)InterlockedDecrement(&dev->Refs);
    UNREFERENCED_PARAMETER(hc);
    return (NTSTATUS)XhciPipeNtStatus((ULONG)usbd);
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

/*
 * Submit one transfer for `irp` on the pipe `handle` names (NULL: the
 * default pipe) of `dev`, on which the caller holds a reference (hcd_urb.c).
 * The handle is resolved and a record taken in one hold of the controller
 * lock, so a pipe the thread is closing cannot be freed between the two.
 * `setup` is the 8 SETUP bytes of a control transfer, NULL otherwise; `in`
 * the data direction; `buffer` or `mdl` the data (the MDL wins);
 * `lengthOut` the URB's TransferBufferLength, written at completion. The
 * reference passes to the IRP: it is returned at completion, here on a
 * refusal or later in HcdIoDeferred. Returns STATUS_PENDING or the
 * refusal's status, the IRP completed either way.
 */
NTSTATUS HcdIoSubmit(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev, PVOID handle,
                     PHCD_DEVICE_PDO pdo, PIRP irp, PVOID urb,
                     const UCHAR *setup, ULONG flags, PVOID buffer, PMDL mdl,
                     ULONG length, PULONG lengthOut)
{
    PHCD_PIPE pipe;
    PHCD_XFER x;
    KIRQL oldIrql;
    ULONG gone;
    ULONG i;

    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    gone = dev->Gone;
    pipe = gone ? NULL : hcdPipeFromHandle(dev, handle);
    if (pipe != NULL && pipe->Closed) {
        pipe = NULL;
    }
    x = (pipe == NULL) ? NULL : hcdTakeRecord(pipe, irp);
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    if (gone) {
        (VOID)InterlockedIncrement((PLONG)&hc->UrbsGone);
        return hcdRefuse(hc, dev, irp, (PURB)urb, HCD_USBD_DEVICE_GONE);
    }
    if (pipe == NULL) {
        return hcdRefuse(hc, dev, irp, (PURB)urb, HCD_USBD_INVALID_PIPE);
    }
    if (x == NULL) {
        /* Every record is out. The waiting list that queues instead is
         * the interrupt and bulk pipes' (26-A.5's next step); a control
         * client has one request out at a time. Counted. */
        (VOID)InterlockedIncrement((PLONG)&hc->UrbsBusy);
        return hcdRefuse(hc, dev, irp, (PURB)urb, HCD_USBD_ERROR_BUSY);
    }

    x->Urb = urb;
    x->Control = (setup != NULL);
    x->In = (flags & HCD_IO_IN) != 0;
    x->ShortOk = (flags & HCD_IO_SHORT_OK) != 0;
    if (setup != NULL) {
        x->Setup.bmRequestType = setup[0];
        x->Setup.bRequest = setup[1];
        x->Setup.wValue = (USHORT)(setup[2] | (setup[3] << 8));
        x->Setup.wIndex = (USHORT)(setup[4] | (setup[5] << 8));
        x->Setup.wLength = (USHORT)(setup[6] | (setup[7] << 8));
    }
    x->Length = length;
    x->Offset = 0;
    x->Chunk = 0;
    x->MapBase = NULL;
    x->MapCount = 0;
    x->Status = XHCI_USBD_STATUS_SUCCESS;
    x->Engine = 0;
    x->LengthOut = lengthOut;
    x->Mdl = NULL;
    x->OwnMdl = 0;
    x->Sg.List.SgElementCount = 0;
    for (i = 0; i < sizeof(x->Sg.List) - sizeof(x->Sg.List.SgElement);
         i++) {
        ((PUCHAR)&x->Sg.List)[i] = 0;
    }
    if (length != 0) {
        if (mdl != NULL) {
            x->Mdl = mdl;
        } else if (buffer != NULL) {
            x->Mdl = HcdPoolMdlBuild(buffer, length);
            x->OwnMdl = 1;
        }
        if (x->Mdl == NULL) {
            hcdFreeRecord(hc, x);
            return hcdRefuse(hc, dev, irp, (PURB)urb,
                             buffer == NULL ? (LONG)USBD_STATUS_INVALID_PARAMETER
                                            : HCD_USBD_NO_MEMORY);
        }
        if (!hcdPlanChunk(hc, x)) {
            if (x->OwnMdl) {
                HcdPoolMdlFree(x->Mdl);
            }
            x->Mdl = NULL;
            hcdFreeRecord(hc, x);
            return hcdRefuse(hc, dev, irp, (PURB)urb,
                             (LONG)USBD_STATUS_INVALID_PARAMETER);
        }
    }

    /* Counted on the PDO until completed: its REMOVE waits the count out,
     * so no completion can reach a client driver that has unloaded
     * (c4-2k: 0xCE in hidusb on an unplug, 2026-10-03). */
    x->Pdo = pdo;
    (VOID)InterlockedIncrement(&pdo->UrbsPending);
    IoMarkIrpPending(irp);
    hcdCancelOn(irp, x);

    /*
     * A request split into chunks owns the pipe from its submission, and a
     * request submitted while another owns it waits here unmapped, on the
     * pipe's Held list: it holds no map registers the owner's next chunk
     * may need (round 3, finding 8), and it cannot land between two of the
     * owner's chunks (round 2, finding 7). HcdIoPipeRelease starts it.
     */
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
        return STATUS_PENDING;
    }
    if (pipe->Exclusive != NULL || pipe->Paused) {
        /* Held unmapped: behind the pipe's owner, or while the thread has
         * it paused - holding no map registers either way, so an abort's
         * cancelled work never waits for registers a held request keeps
         * (round 4, finding 4). */
        x->State = HCD_XFER_HELD;
        InsertTailList(&pipe->Held, &x->Link);
        XhciControllerLockRelease(&hc->Hc, oldIrql);
        return STATUS_PENDING;
    }
    if (length != 0 && x->Chunk < length) {
        pipe->Exclusive = x;
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    hcdStart(hc, x);
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
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    if (ok && !dev->Gone && !pipe->Closed && !x->CancelRequested &&
        (pipe->Paused ||
         (pipe->Exclusive != NULL && pipe->Exclusive != x &&
          x->Seq > pipe->Exclusive->Seq))) {
        /* Held mapped: the thread has the pipe paused, or a request
         * submitted before this one owns it. A request submitted before
         * the owner is not held - it goes first, as it was asked first. */
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
    if (x->Status == XHCI_USBD_STATUS_SUCCESS) {
        x->Status = dev->Gone      ? HCD_USBD_DEVICE_GONE
                    : (pipe->Closed || x->CancelRequested)
                        ? HCD_USBD_CANCELED
                    : answer == XHCI_XFER_BUSY ? HCD_USBD_ERROR_BUSY
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
    KIRQL oldIrql;
    ULONG marked;
    ULONG i;

    marked = 0;
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
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
    HcdIoDeferred(hc);
    return marked;
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
    ULONG bytes;
    ULONG more;
    ULONG release;

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
        if (x->Engine) {
            status = x->Xfer.UsbdStatus;
            bytes = x->Xfer.BytesTransferred;
        } else {
            status = x->Status;
            bytes = 0;
        }
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
            /* A short transfer the client did not allow (Windows 2000 DDK
             * usbdi.h:282-284; round 2, finding 11). */
            status = HCD_USBD_ERROR_SHORT_TRANSFER;
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
         * finding 6). */
        XhciControllerLockAcquire(&hc->Hc, &oldIrql);
        release = (x->Pipe->Exclusive == x);
        if (release) {
            x->Pipe->Exclusive = NULL;
        }
        XhciControllerLockRelease(&hc->Hc, oldIrql);
        if (release) {
            HcdIoPipeRelease(hc, x->Pipe);
        }
        XhciControllerLockAcquire(&hc->Hc, &oldIrql);
        x->Irp = NULL;
        x->CancelRequested = 0;
        x->State = HCD_XFER_FREE;
        XhciControllerLockRelease(&hc->Hc, oldIrql);

        irp->IoStatus.Status = (NTSTATUS)XhciPipeNtStatus((ULONG)status);
        irp->IoStatus.Information = 0;
        IoCompleteRequest(irp, IO_NO_INCREMENT);
        (VOID)InterlockedIncrement((PLONG)&hc->UrbsCompleted);
        (VOID)InterlockedDecrement(&pdo->UrbsPending);
        (VOID)InterlockedDecrement(&dev->Refs);
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
    PHCD_XFER x;
    KIRQL oldIrql;
    ULONG count;

    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
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
            HcdIoRetired(hc, t);
        }
        t = next;
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    HcdIoDeferred(hc);
}

/* Until every record of the pipe is free - a record still in the map pump
 * finishes through HcdIoMapped, which sees Gone or Closed. IRQL:
 * PASSIVE_LEVEL. */
VOID HcdIoWaitPipe(PHCD_CONTROLLER hc, PHCD_PIPE pipe)
{
    LARGE_INTEGER due;
    ULONG busy;
    ULONG i;

    for (;;) {
        busy = hc->CancelsRunning != 0;
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
ULONG HcdIoDeviceGone(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    LARGE_INTEGER due;
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
    while (dev->Refs != 0 || hc->CancelsRunning != 0) {
        HcdRelativeMs(&due, 1);
        (VOID)KeDelayExecutionThread(KernelMode, FALSE, &due);
        HcdIoDeferred(hc);
    }
    return 1;
}
