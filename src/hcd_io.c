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

/* A pipe with `records` transfer records (HCD_PIPE_XFERS, or a stream's
 * HCD_STREAM_XFERS: hcd.h), every one free. */
VOID HcdIoPipeInitCount(PHCD_PIPE pipe, PHCD_USB_DEVICE dev, ULONG records)
{
    ULONG i;

    pipe->Signature = HCD_PIPE_SIGNATURE;
    pipe->Device = dev;
    pipe->XferCount = records;
    XhciXstatClear(&pipe->Empty);
    InitializeListHead(&pipe->Waiting);
    InitializeListHead(&pipe->Held);
    for (i = 0; i < records; i++) {
        pipe->Xfers[i].Pipe = pipe;
        pipe->Xfers[i].State = HCD_XFER_FREE;
        pipe->Xfers[i].Wide = NULL;
        pipe->Xfers[i].Promoted = 0;
        pipe->Xfers[i].RefillQueued = 0;
        pipe->Xfers[i].RefillNext = NULL;
        pipe->Xfers[i].Probe = 0;
    }
    pipe->TruncHeld = 0;
}

VOID HcdIoPipeInit(PHCD_PIPE pipe, PHCD_USB_DEVICE dev)
{
    HcdIoPipeInitCount(pipe, dev, HCD_PIPE_XFERS);
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
/* The time-stamp counter (task 36.21; hcd.h)                               */
/* ----------------------------------------------------------------------- */

#if defined(_M_AMD64)
/* The compiler's CPUID intrinsic (intrin.h's declaration; wdm.h has none). */
void __cpuid(int info[4], int leaf);
#pragma intrinsic(__cpuid)
#endif

/* IRQL: any. */
VOID HcdTscRead(PULONG lo, PULONG hi)
{
#if defined(_M_AMD64)
    ULONG64 tsc;

    tsc = ReadTimeStampCounter();
    *lo = (ULONG)tsc;
    *hi = (ULONG)(tsc >> 32);
#else
    ULONG l;
    ULONG h;

    /* RDTSC, 0F 31: MSVC 6.0's assembler has no mnemonic for it. */
    __asm {
        _emit 0x0F
        _emit 0x31
        mov l, eax
        mov h, edx
    }
    *lo = l;
    *hi = h;
#endif
}

/* The initial APIC ID of the processor running this, CPUID leaf 1's EBX
 * bits 31:24 (0 where there is none). IRQL: any. */
static ULONG hcdCpuId(VOID)
{
#if defined(_M_AMD64)
    int info[4];

    __cpuid(info, 1);
    return ((ULONG)info[1] >> 24) & 0xFFUL;
#else
    ULONG b;

    /* CPUID, 0F A2, which MSVC 6.0's assembler does not name either. CPUID
     * writes EAX, EBX, ECX and EDX, and the emitted bytes tell the compiler
     * none of that. EAX, ECX and EDX are scratch across a call. EBX is the
     * caller's, and merely reading it here does not make MSVC 6.0 save it
     * (the built function had no push of EBX). So it is saved and restored
     * by hand: without that, HcdTscStamp's `raised`, which it keeps in EBX,
     * read CPUID's EBX, and KeLowerIrql ran with an unset IRQL inside the
     * event DPC - STOP 0xB8 on Windows 2000 at the first enumeration
     * (p36-int smoke leg, 2026-10-09). */
    __asm {
        push ebx
        mov eax, 1
        xor ecx, ecx
        _emit 0x0F
        _emit 0xA2
        mov b, ebx
        pop ebx
        xor edx, edx
    }
    return (b >> 24) & 0xFFUL;
#endif
}

/* IRQL: any. Below DISPATCH_LEVEL it is raised for the pair, so the count
 * and the processor's ID come from one processor (review round 2). */
VOID HcdTscStamp(PXHCI_XSTAT_STAMP s)
{
    KIRQL oldIrql;
    ULONG raised;

    raised = (KeGetCurrentIrql() < DISPATCH_LEVEL) ? 1UL : 0UL;
    if (raised) {
        KeRaiseIrql(DISPATCH_LEVEL, &oldIrql);
    }
    s->Cpu = hcdCpuId();
    HcdTscRead(&s->Lo, &s->Hi);
    if (raised) {
        KeLowerIrql(oldIrql);
    }
    if (s->Lo == 0 && s->Hi == 0) {
        /* All zero is "not stamped": one count later. */
        s->Lo = 1;
    }
}

/* A bulk data record: what 36.21 measures. */
static ULONG hcdIsData(PHCD_XFER x)
{
    return !x->Control && !x->Isoch &&
           x->Pipe->TransferType == XHCI_PIPE_XFER_BULK;
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

/* ----------------------------------------------------------------------- */
/* Stage 2: two TDs of one URB on the ring (36.24; record 13 section 11.6) */
/* ----------------------------------------------------------------------- */

static VOID hcdChunkPublished(PHCD_CONTROLLER hc, PHCD_PIPE pipe,
                              PHCD_XFER x, const XHCI_XSTAT_STAMP *entered);

/*
 * Point 1, at the fill: a record that may be pipelined claims a free wide
 * entry; whether it is pipelined is settled by its first mapping. One that
 * the span limits or an empty pool turn away is counted and stays
 * sequential. IRQL: <= DISPATCH_LEVEL, no lock held.
 */
static VOID hcdWideClaim(PHCD_CONTROLLER hc, PHCD_XFER x)
{
    PHCD_WIDE_ENTRY w;
    KIRQL oldIrql;
    ULONG span;
    ULONG verdict;
    ULONG i;

    if (!hc->WideEnabled || x->Mdl == NULL) {
        return;
    }
    span = ADDRESS_AND_SIZE_TO_SPAN_PAGES(MmGetMdlVirtualAddress(x->Mdl),
                                          x->Length);
    w = NULL;
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    for (i = 0; hc->WidePool != NULL && i < HCD_WIDE_ENTRIES; i++) {
        if (hc->WidePool[i].Owner == NULL) {
            w = &hc->WidePool[i];
            break;
        }
    }
    verdict = XhciXpipeEligible(hcdIsData(x), x->In, x->Pipe->StreamId != 0,
                                x->Chunk, x->Length, span, hc->MapRegisters,
                                w != NULL);
    if (verdict == XHCI_XPIPE_ELIG_OK) {
        w->Owner = x;
        w->Count = 0;
        x->Wide = w;
        hc->Xpipe.Claimed++;
        hc->Xpipe.InUse++;
    } else if (verdict == XHCI_XPIPE_ELIG_SPAN) {
        hc->Xpipe.AwaySpan++;
    } else if (verdict == XHCI_XPIPE_ELIG_NO_ENTRY) {
        hc->Xpipe.AwayPool++;
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);
}

/* The entry back: its first mapping did not promote, or the URB ended.
 * Only an entry of this start's pool counts against InUse: one of a pool
 * a fail-closed stop kept (HcdDmaWideClose) is not this start's (review
 * round 4). Controller lock held. */
static VOID hcdWideRelease(PHCD_CONTROLLER hc, PHCD_XFER x)
{
    if (x->Wide != NULL) {
        if (hc->WidePool != NULL && x->Wide >= hc->WidePool &&
            x->Wide < hc->WidePool + HCD_WIDE_ENTRIES &&
            hc->Xpipe.InUse != 0) {
            hc->Xpipe.InUse--;
        }
        x->Wide->Owner = NULL;
        x->Wide = NULL;
    }
}

/* A record whose next TD is published after the completion pass's walk
 * (HcdIoRefillFlush), once. Controller lock held. */
static VOID hcdRefillQueue(PHCD_CONTROLLER hc, PHCD_XFER x)
{
    if (!x->RefillQueued) {
        x->RefillQueued = 1;
        x->RefillNext = hc->RefillHead;
        hc->RefillHead = x;
    }
}

/* The endpoint's doorbell after a publication, read under the lock as
 * HcdIoMapped does (round 3, finding 2), and the save state a publication
 * spoils. Controller lock held. */
static VOID hcdWideDoorbell(PHCD_CONTROLLER hc, PHCD_PIPE pipe)
{
    PHCD_PIPE ep;

    ep = (pipe->Parent != NULL) ? pipe->Parent : pipe;
    ep->SeqUsed = 1;
    hc->Counters.TransfersSubmitted++;
    if (XhciTolSavePublish((hc->Hc.Flags & XHCI_EXT_FLAG_INITIALIZED) !=
                           0)) {
        hc->SavePublished = 1;
        hc->Hc.SavedStateValid = 0;
    }
    XhciWriteDoorbell(&hc->Hc, pipe->Device->SlotId,
                      XhciStreamDoorbell(pipe->Dci, pipe->StreamId));
}

/*
 * 36.25 (record 13 section 11.7, points 3 and 4): a promoted URB on a big
 * stream ring as one TD - the whole URB, whose list is the entry's wide
 * list (offsets from 0), built into the pool's scratch with the engine's
 * cap at 513. A ring with no room holds it while another record's TD is on
 * it, as a refill is held; any other refusal, or no current pool's
 * scratch, is a failure before the ring. Returns the machine's flags.
 * Controller lock held.
 */
XHCI_C_ASSERT(hcd_wide_cap, XHCI_XFER_MAX_WIDE_DATA_TRBS ==
                                 XHCI_XPIPE_WIDE_ELEMENTS);

static ULONG hcdWideBigOne(PHCD_CONTROLLER hc, PHCD_XFER x)
{
    PHCD_PIPE pipe;
    PHCD_WIDE_ENTRY w;
    PUSBPORT_SCATTER_GATHER_LIST sg;
    XHCI_NORMAL_REQUEST nreq;
    XHCI_XSTAT_STAMP entered;
    ULONG answer;
    ULONG i;

    pipe = x->Pipe;
    w = x->Wide;
    HcdTscStamp(&entered);
    if (hc->WideScratch == NULL || w->Count == 0 ||
        w->Count > XHCI_XPIPE_WIDE_ELEMENTS) {
        hc->Xpipe.PreRing++;
        return XhciXpipeRecStop(&x->Rec, XHCI_XPIPE_OUT_PREFAIL,
                                HCD_USBD_INTERNAL_HC_ERROR);
    }
    sg = &w->WideList.List;
    for (i = 0; i < sizeof(*sg) - sizeof(sg->SgElement); i++) {
        ((PUCHAR)sg)[i] = 0;
    }
    sg->SgElementCount = w->Count;
    for (i = 0; i < sizeof(nreq); i++) {
        ((PUCHAR)&nreq)[i] = 0;
    }
    nreq.TransferLength = x->Length;
    nreq.DirectionIn = (pipe->EndpointAddress & 0x80UL) != 0;
    nreq.MaxPacketSize = pipe->MaxPacketSize;
    nreq.SgList = sg;
    nreq.MaxDataTrbs = XHCI_XFER_MAX_WIDE_DATA_TRBS;
    answer = XhciXferSubmitNormal(pipe->Queue, pipe->Ring, &nreq, x->In,
                                  &x->Xfer, x, hc->WideScratch,
                                  HCD_WIDE_SCRATCH_TRBS);
    if (answer == XHCI_XFER_BUSY && pipe->Queue->Count != 0) {
        if (x->RefillHeld != HCD_REFILL_BUSY) {
            x->RefillHeld = HCD_REFILL_BUSY;
            hc->Xpipe.HeldBusy++;
        }
        return 0;
    }
    if (answer != XHCI_XFER_OK) {
        hc->Xpipe.PreRing++;
        return XhciXpipeRecStop(&x->Rec, XHCI_XPIPE_OUT_PREFAIL,
                                HCD_USBD_INTERNAL_HC_ERROR);
    }
    x->RefillHeld = HCD_REFILL_NONE;
    (VOID)XhciXpipeRecPublish(&x->Rec, x->Length);
    hc->Xpipe.Tds++;
    hc->Xpipe.BigTds++;
    if (x->Xfer.DataTrbCount > hc->Xpipe.TdMaxTrbs) {
        hc->Xpipe.TdMaxTrbs = x->Xfer.DataTrbCount;
    }
    switch (x->Xfer.DataTrbCount) {
    case 256:
        hc->Xpipe.Td256++;
        break;
    case 257:
        hc->Xpipe.Td257++;
        break;
    case 512:
        hc->Xpipe.Td512++;
        break;
    case 513:
        hc->Xpipe.Td513++;
        break;
    default:
        break;
    }
    hcdWideDoorbell(hc, pipe);
    hcdChunkPublished(hc, pipe, x, &entered);
    return 0;
}

/*
 * Publish a promoted record's next TDs, up to two on the ring (points 5
 * and 6): each cut from the wide list by the planner, copied into its
 * slot's list and handed to the engine as a chunk is. A pause, or a
 * successor the thread has still to take off, holds the refill; a ring
 * with no room holds it while another record's TD is on it; the device
 * gone or the pipe closed stops the record. Any other refusal is a failure
 * before the ring. Returns the machine's flags. Controller lock held.
 */
static ULONG hcdWidePump(PHCD_CONTROLLER hc, PHCD_XFER x)
{
    PHCD_PIPE pipe;
    PHCD_USB_DEVICE dev;
    PHCD_WIDE_ENTRY w;
    PUSBPORT_SCATTER_GATHER_LIST sg;
    PXHCI_TRANSFER t;
    XHCI_NORMAL_REQUEST nreq;
    XHCI_TRB trbs[XHCI_XFER_MAX_CONTROL_TRBS];
    XHCI_XPIPE_CURSOR saved;
    XHCI_XPIPE_TD td;
    XHCI_XSTAT_STAMP entered;
    ULONG flags;
    ULONG answer;
    ULONG slot;
    ULONG i;

    pipe = x->Pipe;
    dev = pipe->Device;
    w = x->Wide;
    flags = 0;
    while (!x->Rec.NoMore && XhciXpipeOnRing(&x->Rec) < 2 &&
           x->Rec.Planned < x->Length) {
        if (dev->Gone || pipe->Closed || x->CancelRequested) {
            flags |= XhciXpipeRecStop(&x->Rec, XHCI_XPIPE_OUT_STOP,
                                      dev->Gone ? HCD_USBD_DEVICE_GONE
                                                : HCD_USBD_CANCELED);
            break;
        }
        if (pipe->Paused || pipe->TruncHeld) {
            if (x->RefillHeld != HCD_REFILL_PAUSE) {
                x->RefillHeld = HCD_REFILL_PAUSE;
                hc->Xpipe.HeldPause++;
            }
            break;
        }
        HcdTscStamp(&entered);
        if (pipe->BigRing) {
            /* 36.25 (record 13 section 11.7, point 3): one TD, the whole
             * URB, its list the wide list itself. */
            flags |= hcdWideBigOne(hc, x);
            break;
        }
        saved = x->Cursor;
        slot = x->Rec.Published & 1UL;
        if (slot == 0) {
            sg = &x->Sg.List;
            t = &x->Xfer;
        } else {
            sg = &w->Sg.List;
            t = &w->Xfer;
        }
        for (i = 0; i < sizeof(*sg) - sizeof(sg->SgElement); i++) {
            ((PUCHAR)sg)[i] = 0;
        }
        if (XhciXpipePlan(w->WideList.List.SgElement, w->Count, x->Length,
                          x->Rec.Planned, pipe->MaxPacketSize,
                          XHCI_XPIPE_TD_ELEMENTS, &x->Cursor,
                          &td) != XHCI_XPIPE_OK ||
            XhciXpipeBuild(w->WideList.List.SgElement, w->Count, &td, sg,
                           XHCI_XPIPE_TD_ELEMENTS) != td.Count) {
            x->Cursor = saved;
            hc->Xpipe.PreRing++;
            flags |= XhciXpipeRecStop(&x->Rec, XHCI_XPIPE_OUT_PREFAIL,
                                      HCD_USBD_INTERNAL_HC_ERROR);
            break;
        }
        for (i = 0; i < sizeof(nreq); i++) {
            ((PUCHAR)&nreq)[i] = 0;
        }
        nreq.TransferLength = td.Bytes;
        nreq.DirectionIn = (pipe->EndpointAddress & 0x80UL) != 0;
        nreq.MaxPacketSize = pipe->MaxPacketSize;
        nreq.SgList = sg;
        answer = XhciXferSubmitNormal(pipe->Queue, pipe->Ring, &nreq, x->In,
                                      t, x, trbs,
                                      XHCI_XFER_MAX_CONTROL_TRBS);
        if (answer == XHCI_XFER_BUSY && pipe->Queue->Count != 0) {
            /* Only another TD can hold the room; its retirement retries
             * (HcdIoRetired). */
            x->Cursor = saved;
            if (x->RefillHeld != HCD_REFILL_BUSY) {
                x->RefillHeld = HCD_REFILL_BUSY;
                hc->Xpipe.HeldBusy++;
            }
            break;
        }
        if (answer != XHCI_XFER_OK) {
            x->Cursor = saved;
            hc->Xpipe.PreRing++;
            flags |= XhciXpipeRecStop(&x->Rec, XHCI_XPIPE_OUT_PREFAIL,
                                      HCD_USBD_INTERNAL_HC_ERROR);
            break;
        }
        x->RefillHeld = HCD_REFILL_NONE;
        (VOID)XhciXpipeRecPublish(&x->Rec, td.Bytes);
        hc->Xpipe.Tds++;
        if (XhciXpipeOnRing(&x->Rec) == 2 && !x->PipeCounted) {
            x->PipeCounted = 1;
            hc->Xpipe.Pipelined++;
        }
        hcdWideDoorbell(hc, pipe);
        hcdChunkPublished(hc, pipe, x, &entered);
    }
    return flags;
}

/* The publication half, the record on no list: stopped if its device,
 * pipe or IRP went, else its first TDs published; put on the done list if
 * that finalizes it. Returns 1 then. Controller lock held. */
static ULONG hcdWidePublishLocked(PHCD_CONTROLLER hc, PHCD_XFER x)
{
    PHCD_PIPE pipe;
    PHCD_USB_DEVICE dev;
    ULONG flags;

    pipe = x->Pipe;
    dev = pipe->Device;
    x->State = HCD_XFER_ON_RING;
    x->Engine = 1;
    if (dev->Gone || pipe->Closed || x->CancelRequested || hc->ShutLatched) {
        /* 37.6: past the shutdown latch nothing is published (record 13
         * section 5.8). */
        flags = XhciXpipeRecStop(
            &x->Rec, XHCI_XPIPE_OUT_STOP,
            dev->Gone           ? HCD_USBD_DEVICE_GONE
            : hc->ShutLatched   ? XHCI_USBD_STATUS_DEV_NOT_RESPONDING
                                : HCD_USBD_CANCELED);
    } else {
        flags = hcdWidePump(hc, x);
    }
    if (flags & XHCI_XPIPE_F_FINAL) {
        x->State = HCD_XFER_DONE;
        InsertTailList(&hc->DoneList, &x->Link);
        return 1;
    }
    return 0;
}

/*
 * A promoted record's mapping is in hand (its routine, or a held record's
 * release): its first TDs published, or the record held as any mapped
 * record is - paused, behind an earlier owner, or behind a truncation -
 * or ended when its device, pipe or IRP went meanwhile. The machine is the
 * only one to finalize it. IRQL: <= DISPATCH_LEVEL, no lock held.
 */
static VOID hcdWideMapped(PHCD_CONTROLLER hc, PHCD_XFER x)
{
    PHCD_PIPE pipe;
    PHCD_USB_DEVICE dev;
    KIRQL oldIrql;
    ULONG final;

    pipe = x->Pipe;
    dev = pipe->Device;
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    if (!dev->Gone && !pipe->Closed && !x->CancelRequested &&
        !hc->ShutLatched &&
        (pipe->Paused || pipe->TruncHeld ||
         (pipe->Exclusive != NULL && pipe->Exclusive != x &&
          x->Seq > pipe->Exclusive->Seq))) {
        x->State = HCD_XFER_HELD;
        x->Mapped = 1;
        InsertTailList(&pipe->Held, &x->Link);
        XhciControllerLockRelease(&hc->Hc, oldIrql);
        return;
    }
    final = hcdWidePublishLocked(hc, x);
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    if (final) {
        HcdIoDeferred(hc);
    }
}

/*
 * One TD of a promoted record retired, in ring order (point 6): accounted
 * by the machine; an early end with the successor still queued hands it
 * to the thread - the record Truncating, the pipe's CancelPending and the
 * controller's CancelWork set and the thread woken before the lock is
 * dropped - and no ring is edited here; a full TD with bytes left queues
 * the refill for after the walk. Controller lock held.
 */
static VOID hcdWideRetired(PHCD_CONTROLLER hc, PHCD_XFER x, PXHCI_TRANSFER t,
                           ULONG removed)
{
    PHCD_PIPE pipe;
    ULONG flags;

    pipe = x->Pipe;
    flags = XhciXpipeRecRetire(&x->Rec, (t == &x->Xfer) ? 0UL : 1UL,
                               t->BytesTransferred, t->UsbdStatus, removed);
    if (flags & XHCI_XPIPE_F_ORDER) {
        hc->Xpipe.Order++;
    }
    if (flags & XHCI_XPIPE_F_TRUNCATE) {
        if (t->UsbdStatus == XHCI_USBD_STATUS_SUCCESS) {
            hc->Xpipe.EarlyShort++;
        } else if (t->UsbdStatus == XHCI_USBD_STATUS_INTERNAL_HC_ERROR) {
            hc->Xpipe.EarlyVerdict++;
        } else {
            hc->Xpipe.EarlyError++;
        }
        pipe->TruncHeld++;
        if (!pipe->Device->Gone && !pipe->Closed) {
            pipe->CancelPending = 1;
            hc->CancelWork = 1;
            HcdThreadWake(hc);
        }
    }
    if ((flags & XHCI_XPIPE_F_TRUNC_END) && pipe->TruncHeld != 0) {
        pipe->TruncHeld--;
    }
    if (flags & XHCI_XPIPE_F_OVERLAP) {
        /* The controller had the next TD: no gap to measure. */
        hc->Xpipe.Overlap++;
    } else if (flags & XHCI_XPIPE_F_REFILL) {
        /* 36.21's gap opens here, as for a chunk. */
        HcdTscStamp(&x->Retire);
        if (pipe->Queue->Count == 0 && !XhciXstatStamped(&pipe->Empty)) {
            pipe->Empty = x->Retire;
        }
    }
    if (flags & XHCI_XPIPE_F_REFILL) {
        hcdRefillQueue(hc, x);
    }
    if (flags & XHCI_XPIPE_F_FINAL) {
        x->State = HCD_XFER_DONE;
        InsertTailList(&hc->DoneList, &x->Link);
    }
}

/*
 * The refills a pass collected, published now that the engine's completed
 * list - whose intrusive Next links a submission would rewrite - has been
 * walked (point 6). A record finalized in the same pass is skipped. Every
 * path that retires TDs calls it before dropping the lock. Controller lock
 * held.
 */
VOID HcdIoRefillFlush(PHCD_CONTROLLER hc)
{
    PHCD_XFER x;
    ULONG before;
    ULONG flags;

    while ((x = hc->RefillHead) != NULL) {
        hc->RefillHead = x->RefillNext;
        x->RefillNext = NULL;
        x->RefillQueued = 0;
        if (!x->Promoted || x->Rec.Finalized ||
            x->State != HCD_XFER_ON_RING) {
            continue;
        }
        before = x->Rec.Published;
        flags = hcdWidePump(hc, x);
        if (x->Rec.Published != before) {
            hc->Xpipe.Refills += x->Rec.Published - before;
        }
        if (flags & XHCI_XPIPE_F_FINAL) {
            x->State = HCD_XFER_DONE;
            InsertTailList(&hc->DoneList, &x->Link);
        }
    }
}

/*
 * A cancel, abort, reset or removal reaching a promoted record (point 6):
 * offered to its machine, which latches the status unless an earlier end
 * did and stops further TDs; the TDs on the ring are the thread's to take
 * off (they retire with their stopped progress). A record held mapped
 * leaves its pipe's held list here. Nothing for any other record.
 * Controller lock held.
 */
VOID HcdIoWideStop(PHCD_CONTROLLER hc, PHCD_XFER x, LONG usbd)
{
    ULONG flags;

    if (!x->Promoted || x->Rec.Finalized) {
        return;
    }
    if (x->State == HCD_XFER_HELD) {
        RemoveEntryList(&x->Link);
        x->Mapped = 0;
        x->State = HCD_XFER_ON_RING;
    }
    flags = XhciXpipeRecStop(&x->Rec, XHCI_XPIPE_OUT_STOP, usbd);
    if (flags & XHCI_XPIPE_F_FINAL) {
        x->State = HCD_XFER_DONE;
        InsertTailList(&hc->DoneList, &x->Link);
    }
}

/* A held list's walk that took `x` off it (a cancel, a drain): a promoted
 * record goes through its machine, not straight to the done list. Returns
 * 1 when it was one. Controller lock held. */
static ULONG hcdWideHeldEnd(PHCD_CONTROLLER hc, PHCD_XFER x, LONG usbd)
{
    if (!x->Promoted) {
        return 0;
    }
    x->Mapped = 0;
    x->State = HCD_XFER_ON_RING;
    HcdIoWideStop(hc, x, usbd);
    return 1;
}

/* Whether `t` is a TD of a record whose successor the thread is to take
 * off (hcd_cfg.c, hcdCfgCancelPipe): once its early end retired, every TD
 * of it still queued is past the boundary. Controller lock held. */
ULONG HcdIoTruncating(PXHCI_TRANSFER t)
{
    PHCD_XFER x;

    x = HcdIoXferOf(t);
    return x->Promoted && x->Rec.Truncating;
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
        HcdIoWideStop(hc, x, HCD_USBD_CANCELED);
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
        HcdIoWideStop(hc, x, HCD_USBD_CANCELED);
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

    for (i = 0; i < pipe->XferCount; i++) {
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
    PHCD_PIPE pipe;
    PIRP irp;
    PVOID urb;

    while (!IsListEmpty(list)) {
        entry = RemoveHeadList(list);
        irp = CONTAINING_RECORD(entry, IRP, Tail.Overlay.ListEntry);
        pdo = (PHCD_DEVICE_PDO)irp->Tail.Overlay.DriverContext[2];
        urb = IoGetCurrentIrpStackLocation(irp)->Parameters.Others.Argument1;
        pipe = (PHCD_PIPE)irp->Tail.Overlay.DriverContext[3];
        HcdIoIsoRefused(urb, usbd);
        /* A gone device's waiting IRP is held as its records' are
         * (HcdIoPark), still counted on its PDO; its pipe is the one it
         * waited on, not one read back out of the URB. */
        if (usbd != HCD_USBD_DEVICE_GONE ||
            !HcdIoPark(pdo, irp, urb,
                       (pipe == NULL || pipe == &dev->Ep0Pipe)
                           ? NULL
                           : (PVOID)pipe,
                       pipe != NULL ? (PVOID)pipe->Parent : NULL)) {
            (VOID)HcdIoRefuseLater(pdo, irp, urb, usbd);
            (VOID)InterlockedDecrement(&pdo->UrbsPending);
        }
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
    for (i = 0; i < pipe->XferCount; i++) {
        if (pipe->Xfers[i].State == HCD_XFER_FREE) {
            break;
        }
    }
    if (i == pipe->XferCount) {
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
 * dereferenced. NULL is the default pipe. A stream's handle resolves to its
 * pipe once its streams are live, and an endpoint with streams open takes
 * no transfer on its own handle: it has no stream 0 (xhci98_streams.h).
 * Controller lock held. */
static PHCD_PIPE hcdPipeFromHandle(PHCD_USB_DEVICE dev, PVOID handle)
{
    PHCD_PIPE pipe;
    ULONG dci;
    ULONG id;

    if (handle == NULL) {
        return &dev->Ep0Pipe;
    }
    for (dci = 2; dci < 32; dci++) {
        pipe = dev->Pipes[dci];
        if (pipe == NULL) {
            continue;
        }
        if ((PVOID)pipe == handle) {
            return pipe->Streams != NULL ? NULL : pipe;
        }
        if (pipe->Streams != NULL && pipe->Streams->Live) {
            for (id = 1; id <= pipe->Streams->Count; id++) {
                if ((PVOID)pipe->Streams->Pipe[id] == handle) {
                    return pipe->Streams->Pipe[id];
                }
            }
        }
    }
    return NULL;
}

/* ----------------------------------------------------------------------- */
/* Isochronous transfers                                                    */
/* ----------------------------------------------------------------------- */

/* How many SIA publications of an explicit StartFrame, and how many
 * first-TD Frame IDs on a controller without CFC, the driver notes as
 * `iso.sia` and `iso.fid1` (36.57): the first few say which pipes and why;
 * the rest are the same streams'. */
#define HCD_ISO_SIA_NOTES 4UL

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
    /* A zero-bandwidth endpoint (wMaxPacketSize 0, Max ESIT Payload 0:
     * xhci_pipe.c, XhciPipeZeroBandwidth) moves nothing, so a URB on it is
     * a client error, refused here before any TRB is built - and before
     * XhciPipeIsoCheck, whose bound of 0 means "unbounded". INVALID_PARAMETER
     * is this driver's choice: the handle is valid (ABORT_PIPE and
     * RESET_PIPE act on it), the request is what cannot be served. The
     * caller stamps every packet and completes the URB at the next tick, as
     * for every other isochronous refusal. */
    if (XhciPipeZeroBandwidth(&pipe->Ep)) {
        XHCI_DBG_VALUE("hcd: isoch URB on a zero-bandwidth pipe, DCI",
                       pipe->Dci);
        return HCD_USBD_INVALID_PARAMETER;
    }
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
 * the engine decides the rest, all or nothing. Without CFC, an explicit
 * StartFrame that starts a data flow (HCD_PIPE.IsoFlowFresh) gets a Frame
 * ID on its first TD alone. An explicit StartFrame that can
 * have no Frame ID goes out with SIA rather than being refused (task 36.57,
 * design record 13 section 6.10): Windows 7's usbaudio.sys gives every URB
 * of an asynchronous stream an explicit StartFrame, and paces its feedback
 * read by that read's completion. The extension's
 * IsoScratch and IsoLayout are shared by every isochronous pipe, which the
 * controller lock makes sound. Controller lock held.
 */
static ULONG hcdIsoPublish(PHCD_CONTROLLER hc, PHCD_PIPE pipe, PHCD_XFER x)
{
    PXHCI_EXTENSION ext;
    struct _URB_ISOCH_TRANSFER *it;
    PUSBPORT_ISO_TRANSFER blk;
    XHCI_ISO_REQUEST req;
    ULONG interval;
    ULONG answer;
    ULONG sampled;
    ULONG fresh;
    ULONG plan;
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
    sampled = XhciFrameIdNow(ext, &now);
    if (sampled) {
        req.Frames.CurrentFrame = now;
    } else {
        now = ext->FrameNumber;
    }
    fresh = pipe->IsoFlowFresh;
    XhciXferIsoFramePolicy(&req, x->Asap, ext->HcInfo.Cfc, sampled, fresh);
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
    /* The engine falls back to SIA for a group whose Frame IDs it cannot
     * use, which moves an explicit StartFrame to the controller's next
     * service (batch (c) review, round 16, finding 2, refused it instead).
     * Since 36.57 that is taken: a start the controller cannot be told - no
     * frame sample, a flow already under way on a controller without CFC,
     * a packet cadence the endpoint's Interval does not share, or a packet
     * outside the IST+1 to 895 frame window - is published with SIA, as
     * Linux publishes it, and noted. The plan asks the engine's own
     * decision, so the two cannot drift (round 17). A start beyond what a
     * Frame ID can name is still refused, by hcdIsoAdmit. */
    plan = XhciXferIsoStartPlan(&req, x->Asap, ext->HcInfo.Cfc, sampled,
                                fresh);
    answer = XhciXferSubmitIso(pipe->Queue, pipe->Ring, &req,
                               req.DirectionIn, &x->Xfer, x, ext->IsoScratch,
                               XHCI_XFER_MAX_ISO_TRBS, &ext->IsoLayout);
    if (answer == XHCI_XFER_OK) {
        pipe->IsoFlowFresh = XhciXferIsoFlowNext(pipe->IsoFlowFresh,
                                                 XHCI_ISO_FLOW_PUBLISHED, 0);
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
        if (plan == XHCI_ISO_START_FIRST_FRAME_ID) {
            hc->IsoStartsFirstFrameId++;
            if (hc->IsoStartsFirstFrameId <= HCD_ISO_SIA_NOTES) {
                XhciLogNoteLocked(ext, "iso.fid1",
                                  XhciXferIsoSiaNote(pipe->Device->SlotId,
                                                     pipe->Dci, plan, start,
                                                     now));
            }
        } else if (plan != XHCI_ISO_START_ASAP &&
                   plan != XHCI_ISO_START_FRAME_IDS) {
            hc->IsoStartsSia++;
            if (hc->IsoStartsSia <= HCD_ISO_SIA_NOTES) {
                XhciLogNoteLocked(ext, "iso.sia",
                                  XhciXferIsoSiaNote(pipe->Device->SlotId,
                                                     pipe->Dci, plan, start,
                                                     now));
            }
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
    x->Wide = NULL;
    x->Promoted = 0;
    x->RefillHeld = HCD_REFILL_NONE;
    x->RefillQueued = 0;
    x->RefillNext = NULL;
    x->Probe = 0;
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
    x->ChunksPublished = 0;
    XhciXstatClear(&x->Retire);
    XhciXstatClear(&x->MapQueued);
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
    hcdWideClaim(hc, x);
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
    if (x->CancelRequested || pipe->Closed || dev->Gone || hc->ShutLatched) {
        /* Cancelled, or its pipe or device going, while it was being
         * filled in: it completes now and is never held, so no drain the
         * thread has already passed can miss it (round 4, finding 2). Past
         * the shutdown latch the same, as not responding: no D0 comes to
         * serve it (37.6, record 13 section 5.8). */
        x->Engine = 0;
        x->Status = dev->Gone          ? HCD_USBD_DEVICE_GONE
                    : hc->ShutLatched  ? XHCI_USBD_STATUS_DEV_NOT_RESPONDING
                                       : HCD_USBD_CANCELED;
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
    PVOID handle;
    PVOID endpoint;
    KIRQL oldIrql;
    ULONG gone;
    ULONG parked;
    ULONG unreadable;
    ULONG latched;

    x = NULL;
    kick = NULL;
    parked = 0;
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    /* 35-T.6: read under the lock the containment sets it under, so a
     * request that passed hcd_urb.c's test as it was set is held too. */
    unreadable = hc->Hc.Tol.Unreadable;
    gone = dev->Gone;
    /* 37.6: and the shutdown latch under the lock it is set under, so a
     * request either sees it or is queued before the flush that follows
     * the halt (record 13 section 5.8). */
    latched = hc->ShutLatched;
    pipe = (gone || latched) ? NULL : hcdPipeFromHandle(dev, req->Handle);
    if (pipe != NULL && pipe->Closed) {
        pipe = NULL;
    }
    if (pipe != NULL && !HcdPdoOwnsPipe(pdo, dev, pipe)) {
        /* A sibling function's handle (design record 13 section 10.9). */
        pipe = NULL;
    }
    if (unreadable) {
        /* Only compared later, never dereferenced (hcdAbortedLocked). The
         * handle is resolved even on a Gone device, whose pipes stay
         * allocated while this reference is held, so a stream's request
         * keeps its endpoint and an abort of either covers it. */
        pipe = hcdPipeFromHandle(dev, req->Handle);
        handle = (pipe == NULL) ? req->Handle
                 : (pipe == &dev->Ep0Pipe) ? NULL : (PVOID)pipe;
        endpoint = (pipe != NULL) ? (PVOID)pipe->Parent : NULL;
        XhciControllerLockRelease(&hc->Hc, oldIrql);
        (VOID)InterlockedDecrement(&dev->Refs);
        return HcdIoHoldUnreadable(pdo, irp, urb, handle, endpoint);
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
    if (latched) {
        /* Never DEVICE_GONE, which HcdIoDeferred would park on the PDO
         * for a stop that a shutdown never sends. */
        if ((req->Flags & HCD_IO_ISOCH) != 0) {
            HcdIoIsoRefused(urb, XHCI_USBD_STATUS_DEV_NOT_RESPONDING);
        }
        (VOID)InterlockedDecrement(&dev->Refs);
        return HcdIoRefuseLater(pdo, irp, urb,
                                XHCI_USBD_STATUS_DEV_NOT_RESPONDING);
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
 * 36.21: a bulk data chunk is on the ring and its doorbell rung. Counted,
 * and the gap's two parts closed: from its URB's previous chunk's
 * retirement, and from the moment software found this ring empty with a
 * URB unfinished. `entered` is HcdIoMapped's own stamp, so its time to
 * here is the publication's processor cost. Controller lock held.
 */
static VOID hcdChunkPublished(PHCD_CONTROLLER hc, PHCD_PIPE pipe,
                              PHCD_XFER x, const XHCI_XSTAT_STAMP *entered)
{
    PXHCI_XSTAT s;
    XHCI_XSTAT_STAMP now;

    s = &hc->Xstat;
    HcdTscStamp(&now);
    x->ChunksPublished++;
    s->Chunks++;
    (VOID)XhciXstatInterval(&s->Gap, &s->Discarded, &s->Crossed, &x->Retire,
                            &now);
    XhciXstatClear(&x->Retire);
    (VOID)XhciXstatInterval(&s->Map, &s->Discarded, &s->Crossed,
                            &x->MapQueued, entered);
    XhciXstatClear(&x->MapQueued);
    (VOID)XhciXstatInterval(&s->Empty, &s->Discarded, &s->Crossed,
                            &pipe->Empty, &now);
    XhciXstatClear(&pipe->Empty);
    (VOID)XhciXstatInterval(&s->Publish, &s->Discarded, &s->Crossed, entered,
                            &now);
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
    PHCD_PIPE ep;
    PHCD_USB_DEVICE dev;
    XHCI_CONTROL_REQUEST creq;
    XHCI_NORMAL_REQUEST nreq;
    XHCI_TRB trbs[XHCI_XFER_MAX_CONTROL_TRBS];
    KIRQL oldIrql;
    ULONG answer;
    ULONG slot;
    ULONG dci;
    ULONG i;
    XHCI_XSTAT_STAMP entered;

    if (x->Promoted) {
        hcdWideMapped(hc, x);
        return;
    }
    HcdTscStamp(&entered);
    pipe = x->Pipe;
    dev = pipe->Device;
    ep = (pipe->Parent != NULL) ? pipe->Parent : pipe;
    answer = XHCI_XFER_BAD_PARAM;
    if (ok && x->Isoch && !hcdIsoFill(x)) {
        ok = 0;
        x->Status = HCD_USBD_INVALID_PARAMETER;
    }
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    /* 36.24: a record whose first mapping did not promote is sequential
     * for the rest of its life; its entry goes back. */
    hcdWideRelease(hc, x);
    if (ok && !dev->Gone && !pipe->Closed && !x->CancelRequested &&
        !hc->ShutLatched &&
        (pipe->Paused || pipe->TruncHeld ||
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
    /* 37.6: past the shutdown latch neither held nor published (record 13
     * section 5.8). */
    if (ok && !dev->Gone && !pipe->Closed && !x->CancelRequested &&
        !hc->ShutLatched) {
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
        /* The device's sequence moves from here (hcd_cfg.c reads it once
         * the endpoint is paused and settled; Codex review of c4ec1c3,
         * finding 2). */
        ep->SeqUsed = 1;
        hc->Counters.TransfersSubmitted++;
        /* On a controller not initialized - suspended, or resuming before
         * Run/Stop - the TD lands on a ring a saved image may describe and
         * its doorbell is dropped: no image is committed or restored over
         * it, and the resume's reinitialization completes it instead
         * (XhciSlotSaveCommit, XhciSlotSaveSpoiled). */
        if (XhciTolSavePublish(
                (hc->Hc.Flags & XHCI_EXT_FLAG_INITIALIZED) != 0)) {
            hc->SavePublished = 1;
            hc->Hc.SavedStateValid = 0;
        }
        /* The doorbell under the lock: once it is released the thread may
         * pause and stop the endpoint, and a doorbell rung after that
         * would restart it under the thread's edit (Codex review of batch
         * (c), round 3, finding 2). A doorbell is one register write. */
        slot = dev->SlotId;
        dci = XhciStreamDoorbell(pipe->Dci, pipe->StreamId);
        XhciWriteDoorbell(&hc->Hc, slot, dci);
        if (hcdIsData(x)) {
            hcdChunkPublished(hc, pipe, x, &entered);
        }
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
                    : hc->ShutLatched ? XHCI_USBD_STATUS_DEV_NOT_RESPONDING
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
    ULONG done;
    ULONG more;

    done = 0;
again:
    InitializeListHead(&mine);
    more = 0;
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    if (!pipe->Paused && !pipe->TruncHeld) {
        /* 36.24: the owner's refill a pause held (section 11.6, point 6). */
        owner = pipe->Exclusive;
        if (owner != NULL && owner->Promoted &&
            owner->RefillHeld == HCD_REFILL_PAUSE &&
            owner->State == HCD_XFER_ON_RING) {
            hcdRefillQueue(hc, owner);
            HcdIoRefillFlush(hc);
            done = 1;
        }
        for (entry = pipe->Held.Flink; entry != &pipe->Held; entry = next) {
            next = entry->Flink;
            x = CONTAINING_RECORD(entry, HCD_XFER, Link);
            owner = pipe->Exclusive;
            if (x->Promoted) {
                /* 36.24: a promoted record held mapped is published here,
                 * in this hold of the lock: once it leaves the held list
                 * it is the machine's, and a cancel may finalize it the
                 * moment the lock is dropped (review round 1, finding 1).
                 * Records listed before it go first, as the walk orders
                 * every record: it stays held until they are published,
                 * and the walk runs again (review round 2, finding 1). */
                if (!IsListEmpty(&mine)) {
                    more = 1;
                    break;
                }
                RemoveEntryList(entry);
                x->Mapped = 0;
                x->State = HCD_XFER_ON_RING;
                hcdWidePublishLocked(hc, x);
                done = 1;
                continue;
            }
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
    if (done) {
        HcdIoDeferred(hc);
    }
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
    if (more) {
        goto again;
    }
}

/* Stream `id` of an endpoint pipe with streams open, or NULL. The thread
 * alone sets and clears Streams, so on the thread it may be walked without
 * the lock; anywhere else, under it (31-A.1). */
static PHCD_PIPE hcdStreamAt(PHCD_PIPE pipe, ULONG id)
{
    if (pipe->Streams == NULL || id == 0 || id > pipe->Streams->Count) {
        return NULL;
    }
    return pipe->Streams->Pipe[id];
}

/*
 * The thread's gate around a stop or an edit of the endpoint (hcd_cfg.c):
 * Pause holds every record that reaches HcdIoMapped meanwhile; Resume lets
 * them go. IRQL: <= DISPATCH_LEVEL.
 *
 * On an endpoint with streams open, these two and the cancel, drain and
 * wait operations below reach every stream's pipe as well (31-A.1): what
 * the thread does to an endpoint it does to all of it, since a Stop
 * Endpoint stops every stream. On a stream's own pipe they reach that
 * stream alone.
 */
VOID HcdIoPipePause(PHCD_CONTROLLER hc, PHCD_PIPE pipe)
{
    PHCD_PIPE p;
    KIRQL oldIrql;
    ULONG id;

    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    pipe->Paused++;
    for (id = 1; (p = hcdStreamAt(pipe, id)) != NULL; id++) {
        p->Paused++;
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);
}

VOID HcdIoPipeResume(PHCD_CONTROLLER hc, PHCD_PIPE pipe)
{
    PHCD_PIPE p;
    KIRQL oldIrql;
    ULONG id;

    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    if (pipe->Paused != 0) {
        pipe->Paused--;
    }
    for (id = 1; (p = hcdStreamAt(pipe, id)) != NULL; id++) {
        if (p->Paused != 0) {
            p->Paused--;
        }
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    HcdIoPipeRelease(hc, pipe);
    for (id = 1; (p = hcdStreamAt(pipe, id)) != NULL; id++) {
        HcdIoPipeRelease(hc, p);
    }
}

/*
 * Mark every record a request holds on the pipe as cancelled, wherever it
 * is - in the map pump, held, or on the ring - and send the held ones to the
 * done list now (the ring's are the caller's to take off once the endpoint
 * is stopped). Returns how many were marked. Controller lock NOT held.
 */
static ULONG hcdCancelAllOne(PHCD_CONTROLLER hc, PHCD_PIPE pipe, LONG usbd)
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
    for (i = 0; i < pipe->XferCount; i++) {
        x = &pipe->Xfers[i];
        if (x->State != HCD_XFER_FREE && x->Irp != NULL) {
            x->CancelRequested = 1;
            /* The operation's own status, not CANCELED: an abort for a
             * fault drains with an error a promoted record must report
             * (review round 1, finding 2). */
            HcdIoWideStop(hc, x, usbd);
            marked++;
        }
    }
    while (!IsListEmpty(&pipe->Held)) {
        entry = RemoveHeadList(&pipe->Held);
        x = CONTAINING_RECORD(entry, HCD_XFER, Link);
        if (hcdWideHeldEnd(hc, x, usbd)) {
            continue;
        }
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
 * Mark every record a request holds on the pipe - and on each of its
 * streams - as cancelled, wherever it is, and move nothing: the caller
 * drains them (HcdIoDrainPipe) and then settles the marked ones
 * (HcdIoPipeWaitCancelled). The mark is what keeps a record still in the
 * map pump from being held mapped afterwards: HcdIoMapped completes a
 * marked record instead of putting it on Held, where the endpoint's resume
 * would publish it onto a ring the drain just emptied (Codex review of
 * 31-A.1, round 2, unit A). The waiting IRPs leave in the same hold of the
 * lock and complete with `usbd` once it is released: otherwise a marked
 * record could finish meanwhile and be handed one of them, unmarked
 * (hcdRecordRelease), past the cutoff the mark draws (Codex review of the
 * Phase 28-31 integration, finding 3). Returns how many were marked.
 * Controller lock NOT held.
 */
ULONG HcdIoPipeMarkAll(PHCD_CONTROLLER hc, PHCD_PIPE pipe, LONG usbd)
{
    LIST_ENTRY waiting;
    PHCD_PIPE p;
    PHCD_XFER x;
    KIRQL oldIrql;
    ULONG marked;
    ULONG id;
    ULONG i;

    marked = 0;
    InitializeListHead(&waiting);
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    for (id = 0, p = pipe; p != NULL; p = hcdStreamAt(pipe, ++id)) {
        hcdWaitingFlush(p, &waiting);
        for (i = 0; i < p->XferCount; i++) {
            x = &p->Xfers[i];
            if (x->State != HCD_XFER_FREE && x->Irp != NULL) {
                x->CancelRequested = 1;
                HcdIoWideStop(hc, x, usbd);
                marked++;
            }
        }
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    hcdWaitingRefuse(&waiting, pipe->Device, usbd);
    return marked;
}

ULONG HcdIoPipeCancelAll(PHCD_CONTROLLER hc, PHCD_PIPE pipe, LONG usbd)
{
    PHCD_PIPE p;
    ULONG marked;
    ULONG id;

    marked = hcdCancelAllOne(hc, pipe, usbd);
    for (id = 1; (p = hcdStreamAt(pipe, id)) != NULL; id++) {
        marked += hcdCancelAllOne(hc, p, usbd);
    }
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
/* HcdIoCancelPdo's work on one pipe. Controller lock held. */
static ULONG hcdCancelPdoPipe(PHCD_CONTROLLER hc, PHCD_PIPE pipe,
                              PHCD_DEVICE_PDO pdo, PLIST_ENTRY waiting)
{
    PLIST_ENTRY entry;
    PLIST_ENTRY next;
    PHCD_XFER x;
    PIRP irp;
    ULONG marked;
    ULONG i;

    marked = 0;
    for (entry = pipe->Waiting.Flink; entry != &pipe->Waiting;
         entry = next) {
        next = entry->Flink;
        irp = CONTAINING_RECORD(entry, IRP, Tail.Overlay.ListEntry);
        /* One whose routine already runs is left to it (hcdWaitCancel). */
        if (irp->Tail.Overlay.DriverContext[2] == pdo &&
            IoSetCancelRoutine(irp, NULL) != NULL) {
            RemoveEntryList(entry);
            InsertTailList(waiting, entry);
        }
    }
    for (i = 0; i < pipe->XferCount; i++) {
        x = &pipe->Xfers[i];
        if (x->State != HCD_XFER_FREE && x->Irp != NULL &&
            x->Irp->Tail.Overlay.DriverContext[2] == pdo) {
            x->CancelRequested = 1;
            pipe->CancelPending = 1;
            HcdIoWideStop(hc, x, HCD_USBD_CANCELED);
            marked = 1;
        }
    }
    return marked;
}

VOID HcdIoCancelPdo(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                    PHCD_DEVICE_PDO pdo)
{
    LIST_ENTRY waiting;
    PHCD_PIPE pipe;
    PHCD_PIPE p;
    KIRQL oldIrql;
    ULONG marked;
    ULONG dci;
    ULONG id;

    InitializeListHead(&waiting);
    marked = 0;
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    for (dci = 1; dci < 32; dci++) {
        pipe = (dci == 1) ? &dev->Ep0Pipe : dev->Pipes[dci];
        if (pipe == NULL) {
            continue;
        }
        marked |= hcdCancelPdoPipe(hc, pipe, pdo, &waiting);
        /* A stream's requests are its PDO's like any other (31-A.1). */
        for (id = 1; (p = hcdStreamAt(pipe, id)) != NULL; id++) {
            marked |= hcdCancelPdoPipe(hc, p, pdo, &waiting);
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
static VOID hcdWaitCancelledOne(PHCD_CONTROLLER hc, PHCD_PIPE pipe)
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
                if (hcdWideHeldEnd(hc, x, HCD_USBD_CANCELED)) {
                    continue;
                }
                x->Engine = 0;
                x->Status = HCD_USBD_CANCELED;
                x->State = HCD_XFER_DONE;
                InsertTailList(&hc->DoneList, &x->Link);
            }
        }
        XhciControllerLockRelease(&hc->Hc, oldIrql);
        busy = 0;
        for (i = 0; i < pipe->XferCount; i++) {
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

VOID HcdIoPipeWaitCancelled(PHCD_CONTROLLER hc, PHCD_PIPE pipe)
{
    PHCD_PIPE p;
    ULONG id;

    hcdWaitCancelledOne(hc, pipe);
    for (id = 1; (p = hcdStreamAt(pipe, id)) != NULL; id++) {
        hcdWaitCancelledOne(hc, p);
    }
}

/* ----------------------------------------------------------------------- */
/* Completion                                                               */
/* ----------------------------------------------------------------------- */

/* The engine retired one of the URB path's records (hcd_dev.c, from the
 * event DPC or a settle), or (`removed`) software took it off the queue
 * (the cancel walk, a drain). Controller lock held. */
static VOID hcdIoRetire(PHCD_CONTROLLER hc, PXHCI_TRANSFER t, ULONG removed)
{
    PHCD_XFER x;
    PHCD_XFER owner;

    x = HcdIoXferOf(t);
    if (x->Promoted) {
        hcdWideRetired(hc, x, t, removed);
        return;
    }
    /* 36.24: a refill the ring had no room for waits for another TD of the
     * pipe to leave it (section 11.6, point 6). */
    owner = x->Pipe->Exclusive;
    if (owner != NULL && owner != x && owner->Promoted &&
        owner->RefillHeld == HCD_REFILL_BUSY) {
        hcdRefillQueue(hc, owner);
    }
    x->State = HCD_XFER_DONE;
    InsertTailList(&hc->DoneList, &x->Link);
    /* 36.21: a bulk chunk that moved all it asked for with more of its URB
     * still to go opens the gap here, as software handles the completion;
     * if the ring now holds nothing, so does the software-empty interval.
     * The URB may yet end instead (cancelled, gone): HcdIoDeferred closes
     * both unrecorded then. */
    if (x->Engine && hcdIsData(x) &&
        x->Xfer.UsbdStatus == XHCI_USBD_STATUS_SUCCESS &&
        x->Xfer.BytesTransferred >= x->Chunk &&
        x->Offset + x->Chunk < x->Length) {
        HcdTscStamp(&x->Retire);
        if (x->Pipe->Queue->Count == 0 &&
            !XhciXstatStamped(&x->Pipe->Empty)) {
            x->Pipe->Empty = x->Retire;
        }
    }
}

VOID HcdIoRetired(PHCD_CONTROLLER hc, PXHCI_TRANSFER t)
{
    hcdIoRetire(hc, t, 0);
}

VOID HcdIoRemoved(PHCD_CONTROLLER hc, PXHCI_TRANSFER t)
{
    hcdIoRetire(hc, t, 1);
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
    ULONG parked;
    ULONG isData;
    ULONG pieceOk;
    ULONG piece;
    ULONG rest;
    XHCI_XSTAT_STAMP entered;
    XHCI_XSTAT_STAMP done;

    for (;;) {
        XhciControllerLockAcquire(&hc->Hc, &oldIrql);
        if (IsListEmpty(&hc->DoneList)) {
            XhciControllerLockRelease(&hc->Hc, oldIrql);
            break;
        }
        entry = RemoveHeadList(&hc->DoneList);
        XhciControllerLockRelease(&hc->Hc, oldIrql);

        x = CONTAINING_RECORD(entry, HCD_XFER, Link);
        HcdTscStamp(&entered);
        pieceOk = 0;
        piece = 0;
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
        if (x->Promoted) {
            /* 36.24: the machine's one finalization put it here; its bytes
             * are the TDs' before the boundary, its status the outcome
             * latched first (section 11.6, point 6). A path that sent a
             * held record here without asking the machine is settled as
             * a stop with the status it set. */
            XhciControllerLockAcquire(&hc->Hc, &oldIrql);
            if (!x->Rec.Finalized) {
                (VOID)XhciXpipeRecStop(&x->Rec, XHCI_XPIPE_OUT_STOP,
                                       x->Status);
                x->Rec.Finalized = 1;
            }
            XhciControllerLockRelease(&hc->Hc, oldIrql);
            engine = x->Rec.Published != 0;
            status = XhciXpipeRecStatus(&x->Rec, x->ShortOk,
                                        HCD_USBD_ERROR_SHORT_TRANSFER,
                                        HCD_USBD_CANCELED);
            x->Offset = x->Rec.Committed;
        } else if (x->Isoch) {
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
                if (hcdIsData(x)) {
                    /* 36.21: this pass's cost, before the map pump may
                     * run the next chunk's publication inside the call,
                     * and the mapping's start, which the publication
                     * closes (hcdChunkPublished). */
                    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
                    HcdTscStamp(&x->MapQueued);
                    (VOID)XhciXstatInterval(&hc->Xstat.Complete,
                                            &hc->Xstat.Discarded,
                                            &hc->Xstat.Crossed, &entered,
                                            &x->MapQueued);
                    XhciControllerLockRelease(&hc->Hc, oldIrql);
                }
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
        isData = hcdIsData(x);
        if (isData) {
            /* 36.21: the URB's end. A gap it left open closes unrecorded:
             * no next chunk of it will be published, and a ring left empty
             * by it is idle, not a gap. */
            if (XhciXstatStamped(&x->Retire) &&
                x->Pipe->Empty.Lo == x->Retire.Lo &&
                x->Pipe->Empty.Hi == x->Retire.Hi) {
                XhciXstatClear(&x->Pipe->Empty);
            }
            XhciXstatClear(&x->Retire);
            XhciXstatClear(&x->MapQueued);
            if (x->ChunksPublished != 0) {
                XhciXstatUrb(&hc->Xstat, x->Length, x->ChunksPublished);
            }
            /* The pass's first piece, to the pipe's release, which may
             * publish another record inside it (measured as its own). */
            HcdTscStamp(&done);
            pieceOk = XhciXstatDiff(&hc->Xstat.Discarded, &hc->Xstat.Crossed,
                                    &entered, &done, &piece);
        }
        release = (x->Pipe->Exclusive == x) || x->Pipe->RingWait;
        if (x->Pipe->Exclusive == x) {
            x->Pipe->Exclusive = NULL;
        }
        hcdWideRelease(hc, x);
        x->Promoted = 0;
        x->Pipe->RingWait = 0;
        XhciControllerLockRelease(&hc->Hc, oldIrql);
        if (release) {
            HcdIoPipeRelease(hc, x->Pipe);
        }
        if (isData) {
            HcdTscStamp(&entered);
        }
        XhciControllerLockAcquire(&hc->Hc, &oldIrql);
        if (isData) {
            /* 36.21: the second piece, from the pipe's release to here,
             * before the record is freed and its pipe may go. The pass's
             * cost is the two together: the pipe's release, which runs
             * another record's publication, is left out, and so is the
             * IRP's completion with the client's routine it runs. */
            HcdTscStamp(&done);
            if (XhciXstatDiff(&hc->Xstat.Discarded, &hc->Xstat.Crossed,
                              &entered, &done, &rest) &&
                pieceOk && rest <= XHCI_XSTAT_INTERVAL_MAX - piece) {
                XhciXstatAddTime(&hc->Xstat.Complete, piece + rest);
            }
        }
        nextIrp = hcdRecordRelease(x->Pipe, x);
        XhciControllerLockRelease(&hc->Hc, oldIrql);

        /* The pipe from the record, never from the URB, whose lengths the
         * completion has already rewritten (Codex review of 6dae92b,
         * finding 2). */
        parked = status == HCD_USBD_DEVICE_GONE &&
                 HcdIoPark(pdo, irp, urb,
                           (x->Pipe == &dev->Ep0Pipe) ? NULL
                                                      : (PVOID)x->Pipe,
                           (PVOID)x->Pipe->Parent);
        if (parked) {
            /* Its device left: held, as a request to a device that stopped
             * answering is, until the client aborts or cancels it or its
             * PDO is stopped or removed (HcdIoPark). Still counted on the
             * PDO, no longer on the device. */
        } else if (!engine && status != XHCI_USBD_STATUS_SUCCESS) {
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
        if (!parked) {
            (VOID)InterlockedIncrement((PLONG)&hc->UrbsCompleted);
            (VOID)InterlockedDecrement(&pdo->UrbsPending);
        }
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
static VOID hcdDrainOne(PHCD_CONTROLLER hc, PHCD_PIPE pipe, LONG usbd)
{
    PXHCI_TRANSFER t;
    PXHCI_TRANSFER next;
    PLIST_ENTRY entry;
    LIST_ENTRY waiting;
    PHCD_XFER x;
    KIRQL oldIrql;
    ULONG count;
    ULONG i;

    InitializeListHead(&waiting);
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    hcdWaitingFlush(pipe, &waiting);
    /* The held records too: they are on neither the engine's queue nor
     * the done list, and a closed or gone pipe never releases them (Codex
     * review of batch (c), round 3, finding 5). */
    while (!IsListEmpty(&pipe->Held)) {
        entry = RemoveHeadList(&pipe->Held);
        x = CONTAINING_RECORD(entry, HCD_XFER, Link);
        if (hcdWideHeldEnd(hc, x, usbd)) {
            continue;
        }
        x->Engine = 0;
        x->Status = usbd;
        x->State = HCD_XFER_DONE;
        InsertTailList(&hc->DoneList, &x->Link);
    }
    t = XhciXferQueueDrain(pipe->Queue, usbd, &count);
    while (t != NULL) {
        next = t->Next;
        if (t == &pipe->Device->HubXfer) {
            /* A hub's status-change record carries no IRP (hcd_hub.c). */
            t->UsbdStatus = usbd;
            HcdHubXferRetired(hc, pipe->Device);
        } else if (t != &pipe->Device->Ep0Xfer) {
            t->UsbdStatus = usbd;
            hc->Counters.TransfersCancelled++;
            HcdIoRemoved(hc, t);
        }
        t = next;
    }
    /* 36.24: a pipelined record with nothing left on the queue - its
     * refill held, or its last TD just drained - ends with the drain's
     * status. */
    for (i = 0; i < pipe->XferCount; i++) {
        x = &pipe->Xfers[i];
        if (x->State != HCD_XFER_FREE) {
            HcdIoWideStop(hc, x, usbd);
        }
    }
    HcdIoRefillFlush(hc);
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    hcdWaitingRefuse(&waiting, pipe->Device, usbd);
    HcdIoDeferred(hc);
}

VOID HcdIoDrainPipe(PHCD_CONTROLLER hc, PHCD_PIPE pipe, LONG usbd)
{
    PHCD_PIPE p;
    ULONG id;

    hcdDrainOne(hc, pipe, usbd);
    for (id = 1; (p = hcdStreamAt(pipe, id)) != NULL; id++) {
        hcdDrainOne(hc, p, usbd);
    }
}

/* Until every record of the pipe is free - a record still in the map pump
 * finishes through HcdIoMapped, which sees Gone or Closed. IRQL:
 * PASSIVE_LEVEL. */
static VOID hcdWaitPipeOne(PHCD_CONTROLLER hc, PHCD_PIPE pipe)
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
        for (i = 0; i < pipe->XferCount; i++) {
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

VOID HcdIoWaitPipe(PHCD_CONTROLLER hc, PHCD_PIPE pipe)
{
    PHCD_PIPE p;
    ULONG id;

    hcdWaitPipeOne(hc, pipe);
    for (id = 1; (p = hcdStreamAt(pipe, id)) != NULL; id++) {
        hcdWaitPipeOne(hc, p);
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
    HcdSuspDeviceGone(dev);
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    /* A queued door request holds a reference and no DMA: completed even
     * when the record must be kept, so neither the thread's wait for the
     * references nor a door FDO's REMOVE waits on it (task 36.45). */
    HcdDoorFlushDevice(hc, dev);
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

/* One device's pipes drained DEVICE_NOT_RESPONDING for HcdIoShutFlush,
 * its pipes left open and the device not marked gone. IRQL:
 * PASSIVE_LEVEL, the power gate held, no lock held. */
static VOID hcdShutDrainDevice(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    ULONG dci;

    HcdIoDrainPipe(hc, &dev->Ep0Pipe, XHCI_USBD_STATUS_DEV_NOT_RESPONDING);
    for (dci = 2; dci < 32; dci++) {
        if (dev->Pipes[dci] != NULL) {
            HcdIoDrainPipe(hc, dev->Pipes[dci],
                           XHCI_USBD_STATUS_DEV_NOT_RESPONDING);
        }
    }
}

/*
 * The shutdown latch's transfer flush (37.6, record 13 section 5.8): on the
 * power path after the shutdown's halt, the halt proven, every pipe of
 * every device - the set the thread's drain walks (hcd_enum.c,
 * hcdDrainAll), a device reached twice drained harmlessly twice - drained
 * DEVICE_NOT_RESPONDING: its waiting IRPs, held records, engine queue and
 * promoted records. The TDs stay on the halted rings, so the saved image
 * is dropped first and a resume, should one ever come, reinitializes
 * rather than restoring over them. The port and slot tables are the
 * thread's, stable here because the power gate keeps the thread out of
 * its pass. IRQL: PASSIVE_LEVEL (unlike this file's blanket), the power
 * gate held, no lock held.
 */
VOID HcdIoShutFlush(PHCD_CONTROLLER hc)
{
    KIRQL oldIrql;
    ULONG i;

    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    hc->Hc.SavedStateValid = 0;
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    for (i = 0; i < HCD_PORT_COUNT; i++) {
        if (hc->Ports[i].Device != NULL) {
            hcdShutDrainDevice(hc, hc->Ports[i].Device);
        }
    }
    for (i = 1; i <= XHCI_MAX_SLOTS; i++) {
        if (hc->SlotDevice[i] != NULL) {
            hcdShutDrainDevice(hc, hc->SlotDevice[i]);
        }
    }
}

ULONG HcdIoDeviceGone(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    LARGE_INTEGER due;
    KIRQL oldIrql;
    ULONG i;

    if (!HcdIoDeviceDrain(hc, dev)) {
        return 0;
    }
    while (dev->Refs != 0 || hc->CancelsRunning != 0) {
        HcdRelativeMs(&due, 1);
        (VOID)KeDelayExecutionThread(KernelMode, FALSE, &due);
        HcdIoDeferred(hc);
    }
    /* 36.21: a class driver record of this device is kept, said departed,
     * and owned by nothing: the next device takes it afresh. */
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    for (i = 0; i < HCD_UAS_TELE_RECORDS; i++) {
        if (hc->UasTele[i].Owner == (PVOID)dev) {
            hc->UasTele[i].Owner = NULL;
            hc->UasTele[i].State = XHCI_SNAPSHOT_HCD_TELE_S_DEPARTED;
        }
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    return 1;
}

/* ----------------------------------------------------------------------- */
/* Requests held for a device that left                                     */
/* ----------------------------------------------------------------------- */

/*
 * A URB IRP whose device left while it was queued, on the ring or waiting
 * for a record: held on its PDO, cancellable, instead of completed
 * DEVICE_GONE - as a request to a device that stopped answering stays
 * pending under the hub drivers this bus stands in for - until its client
 * aborts its pipe (ABORT_PIPE, hcd_urb.c), cancels the IRP, or its PDO is
 * stopped, surprise removed or removed (hcdPdoQuiesce); each completes it
 * CANCELED (HcdIoParkedRelease). A URB submitted after the device left is
 * still refused DEVICE_GONE at the next tick (HcdIoRefuseLater).
 *
 * Why (27-V.1, 2026-10-04): Windows 98 SE's hidclass.sys answers a read
 * failing STATUS_DEVICE_NOT_CONNECTED by failing every client read and
 * resubmitting at once while its device is still started (hcdRefusedDpc),
 * and those retries kept the guest busy for minutes - once eleven - before
 * its configuration manager sent the REMOVE: a USB keyboard unplugged from
 * a root port, its reads completed DEVICE_GONE as the teardown drained
 * them. A read that never fails puts hidclass in no such state.
 *
 * An abort is remembered as a horizon (HcdIoAbortMark): a request
 * submitted before an abort of its pipe - its dispatch stamp (HcdIoStamp)
 * at or below the pipe's horizon - is completed CANCELED here at once
 * instead of held; one the teardown reaches only after the abort released
 * the list would otherwise be held for good while its client waits for it
 * before passing its removal down (Codex review of f99f184, finding 2).
 * A later submission cannot revoke that: it only carries a later stamp
 * (Codex review of 6dae92b, finding 1). The pipe is the caller's, from the
 * request's record or wait list, never parsed back out of a URB the
 * completion has rewritten (finding 2). Its hardware resources are
 * already released by then.
 *
 * Not held once the PDO is closing (a stop or removal under way, read under
 * the cancel spin lock its release takes, so nothing is held after the
 * release has run) or the IRP is cancelled: the caller completes those as
 * before (returns 0). Returns 1 when held or completed here; either way
 * the IRP's count in pdo->UrbsPending is this file's to return, and the
 * caller returns its device reference, which neither needs. IRQL:
 * <= DISPATCH_LEVEL, no lock held.
 */
static VOID hcdParkedCancel(PDEVICE_OBJECT obj, PIRP irp);

static VOID hcdParkedComplete(PHCD_DEVICE_PDO pdo, PIRP irp);

/* Whether an IRP was submitted at or before an abort of its pipe - its own
 * horizon or every pipe's - its full stamp rebuilt from the PDO's 64-bit
 * count (xhci_pipe.h, XhciSeqCovers; Codex review of 09ed9d1). A stream's
 * request (31-A.1) has its stream's own handle and, in `endpoint`, its
 * endpoint's: an abort of either covers it, as ABORT_PIPE on the endpoint's
 * handle reaches every stream of a live device (xhci98_streams.h). NULL
 * for a request on no stream. Only compared, never dereferenced: the pipes
 * may be gone. Cancel spin lock held. */
static ULONG hcdAbortedLocked(PHCD_DEVICE_PDO pdo, PIRP irp, PVOID handle,
                              PVOID endpoint)
{
    ULONG stamp;
    ULONG i;

    stamp = (ULONG)(ULONG_PTR)irp->Tail.Overlay.DriverContext[0];
    if (XhciSeqCovers(&pdo->AbortAll, stamp, &pdo->SubmitSeq)) {
        return 1;
    }
    for (i = 0; i < pdo->AbortCount; i++) {
        /* HCD_IO_ENDPOINT_ANY: a request held with no device record left
         * to name its endpoint (35-T.6) - any pipe's abort covers it, as
         * its handle may be a stream of whichever endpoint was aborted. */
        if ((pdo->AbortPipe[i] == handle ||
             endpoint == HCD_IO_ENDPOINT_ANY ||
             (endpoint != NULL && pdo->AbortPipe[i] == endpoint)) &&
            XhciSeqCovers(&pdo->AbortHorizon[i], stamp, &pdo->SubmitSeq)) {
            return 1;
        }
    }
    return 0;
}

/*
 * Every URB IRP is stamped at its dispatch, before anything of this driver
 * reads or rewrites its URB: the PDO's next submission sequence, in
 * DriverContext[0], which nothing else uses on a transfer's IRP (hcd_cfg.c
 * moves it to DriverContext[1] while it pends a request, and back before
 * holding one). IRQL:
 * <= DISPATCH_LEVEL.
 */
VOID HcdIoStamp(PHCD_DEVICE_PDO pdo, PIRP irp)
{
    KIRQL cancelIrql;
    ULONG stamp;

    /* The 64-bit count moves under the lock its readers hold; its low word
     * is never 0 (XhciSeqNext), which marks an unstamped IRP. */
    IoAcquireCancelSpinLock(&cancelIrql);
    XhciSeqNext(&pdo->SubmitSeq);
    stamp = pdo->SubmitSeq.Lo;
    IoReleaseCancelSpinLock(cancelIrql);
    irp->Tail.Overlay.DriverContext[0] = (PVOID)(ULONG_PTR)stamp;
}

ULONG HcdIoPark(PHCD_DEVICE_PDO pdo, PIRP irp, PVOID urb, PVOID handle,
                PVOID endpoint)
{
    KIRQL cancelIrql;

    if (pdo == NULL) {
        return 0;
    }
    IoAcquireCancelSpinLock(&cancelIrql);
    if (pdo->Closing || irp->Cancel) {
        IoReleaseCancelSpinLock(cancelIrql);
        return 0;
    }
    if (hcdAbortedLocked(pdo, irp, handle, endpoint)) {
        IoReleaseCancelSpinLock(cancelIrql);
        hcdParkedComplete(pdo, irp);
        return 1;
    }
    ((PURB)urb)->UrbHeader.Status = HCD_USBD_DEVICE_GONE;
    /* The handle it was submitted on, and for a stream its endpoint's
     * (DriverContext[3], which only a waiting IRP used and this one has
     * left that list). */
    irp->Tail.Overlay.DriverContext[1] = handle;
    irp->Tail.Overlay.DriverContext[3] = endpoint;
    irp->Tail.Overlay.DriverContext[2] = pdo;
    IoMarkIrpPending(irp);
    (VOID)IoSetCancelRoutine(irp, hcdParkedCancel);
    InsertTailList(&pdo->ParkedIrps, &irp->Tail.Overlay.ListEntry);
    pdo->ParkedCount++;
    IoReleaseCancelSpinLock(cancelIrql);
    return 1;
}

/*
 * A new request on a contained controller (35-T.6, design record 17 section
 * 4.6: Unreadable set): held on its PDO as a departed device's is, so a
 * class driver's read simply pends, and released as those are. A refusal
 * would complete DEVICE_GONE, which reads STATUS_DEVICE_NOT_CONNECTED on a
 * PDO that is not closing - the status Windows 98 SE's hidclass.sys
 * resubmits on at once (hcdRefusedDpc). Where HcdIoPark declines - the PDO
 * closing or the IRP cancelled - the deferred refusal completes it
 * STATUS_DELETE_PENDING or STATUS_CANCELLED, never that status. The IRP is
 * pending before the hold, since a request an abort already covers is
 * completed inside it. Counted on the PDO as a held request is. IRQL:
 * <= DISPATCH_LEVEL, no lock held.
 */
NTSTATUS HcdIoHoldUnreadable(PHCD_DEVICE_PDO pdo, PIRP irp, PVOID urb,
                             PVOID handle, PVOID endpoint)
{
    HcdIoIsoRefused(urb, HCD_USBD_DEVICE_GONE);
    IoMarkIrpPending(irp);
    (VOID)InterlockedIncrement(&pdo->UrbsPending);
    if (HcdIoPark(pdo, irp, urb, handle, endpoint)) {
        return STATUS_PENDING;
    }
    (VOID)InterlockedDecrement(&pdo->UrbsPending);
    HcdIoIsoRefused(urb, HCD_USBD_CANCELED);
    return HcdIoRefuseLater(pdo, irp, urb, HCD_USBD_CANCELED);
}

/*
 * ABORT_PIPE on the PDO (hcd_urb.c): the sequence its own stamp took
 * becomes the pipe's horizon, so every request of the pipe submitted
 * before it - held now or reaching the hold later - completes CANCELED
 * (HcdIoPark, HcdIoParkedRelease). `known`: the caller placed the handle -
 * a pipe of the live device (validated there), or one a held request of a
 * departed device names. Horizons live in the 64-bit count's space
 * (xhci_pipe.h), so none is ever retired or reused.
 *
 * The every-pipe horizon (AbortAll) is a deliberately conservative
 * departure policy: when the handle cannot be placed - an unknown handle
 * on a departed PDO - or the PDO has already kept horizons for
 * HCD_PDO_ABORTS distinct live handles (repeated selects can accumulate
 * that many over its life), the abort covers every pipe of the PDO, so
 * aborting one pipe also releases the PDO's held requests on its other
 * pipes, EP0's included. It is consulted only for requests of a departed
 * device, never for live transfers, and never crosses into a sibling
 * function PDO (Codex review of ed025d2, its note). IRQL:
 * <= DISPATCH_LEVEL.
 */
VOID HcdIoAbortMark(PHCD_DEVICE_PDO pdo, PIRP abortIrp, PVOID handle,
                    ULONG known)
{
    PLIST_ENTRY entry;
    KIRQL cancelIrql;
    XHCI_SEQ64 horizon;
    ULONG i;

    IoAcquireCancelSpinLock(&cancelIrql);
    XhciSeqFromStamp(&pdo->SubmitSeq,
                     (ULONG)(ULONG_PTR)abortIrp->Tail.Overlay.DriverContext[0],
                     &horizon);
    if (!known) {
        for (i = 0; i < pdo->AbortCount && !known; i++) {
            known = pdo->AbortPipe[i] == handle;
        }
    }
    for (entry = pdo->ParkedIrps.Flink;
         !known && entry != &pdo->ParkedIrps; entry = entry->Flink) {
        /* A held request's own handle, or for a stream's its endpoint's
         * (hcdAbortedLocked). */
        known = CONTAINING_RECORD(entry, IRP, Tail.Overlay.ListEntry)
                        ->Tail.Overlay.DriverContext[1] == handle ||
                (handle != NULL &&
                 CONTAINING_RECORD(entry, IRP, Tail.Overlay.ListEntry)
                         ->Tail.Overlay.DriverContext[3] == handle);
    }
    if (!known) {
        XhciSeqLatest(&pdo->AbortAll, &horizon);
    } else {
        for (i = 0; i < pdo->AbortCount; i++) {
            if (pdo->AbortPipe[i] == handle) {
                break;
            }
        }
        if (i < pdo->AbortCount) {
            XhciSeqLatest(&pdo->AbortHorizon[i], &horizon);
        } else if (pdo->AbortCount < HCD_PDO_ABORTS) {
            pdo->AbortPipe[pdo->AbortCount] = handle;
            pdo->AbortHorizon[pdo->AbortCount] = horizon;
            pdo->AbortCount++;
        } else {
            XhciSeqLatest(&pdo->AbortAll, &horizon);
        }
    }
    IoReleaseCancelSpinLock(cancelIrql);
}

static VOID hcdParkedComplete(PHCD_DEVICE_PDO pdo, PIRP irp)
{
    ((PURB)IoGetCurrentIrpStackLocation(irp)->Parameters.Others.Argument1)
        ->UrbHeader.Status = HCD_USBD_CANCELED;
    irp->IoStatus.Status = STATUS_CANCELLED;
    irp->IoStatus.Information = 0;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    (VOID)InterlockedDecrement(&pdo->UrbsPending);
}

/* A held IRP its client cancelled. Called with the cancel spin lock held; a
 * release takes its IRPs off the list and clears their routines under that
 * lock, so one still listed is this routine's alone. */
static VOID hcdParkedCancel(PDEVICE_OBJECT obj, PIRP irp)
{
    PHCD_DEVICE_PDO pdo;

    UNREFERENCED_PARAMETER(obj);
    pdo = (PHCD_DEVICE_PDO)irp->Tail.Overlay.DriverContext[2];
    RemoveEntryList(&irp->Tail.Overlay.ListEntry);
    pdo->ParkedCount--;
    IoReleaseCancelSpinLock(irp->CancelIrql);
    hcdParkedComplete(pdo, irp);
}

/* The IRPs held on the PDO - every one, or with `aborted` those an abort
 * horizon now covers (HcdIoAbortMark) - completed CANCELED. Returns how
 * many. IRQL: <= DISPATCH_LEVEL, no lock held. */
ULONG HcdIoParkedRelease(PHCD_DEVICE_PDO pdo, ULONG aborted)
{
    LIST_ENTRY mine;
    PLIST_ENTRY entry;
    PLIST_ENTRY next;
    PIRP irp;
    KIRQL cancelIrql;
    ULONG n;

    InitializeListHead(&mine);
    IoAcquireCancelSpinLock(&cancelIrql);
    for (entry = pdo->ParkedIrps.Flink; entry != &pdo->ParkedIrps;
         entry = next) {
        next = entry->Flink;
        irp = CONTAINING_RECORD(entry, IRP, Tail.Overlay.ListEntry);
        if (aborted &&
            !hcdAbortedLocked(pdo, irp, irp->Tail.Overlay.DriverContext[1],
                              irp->Tail.Overlay.DriverContext[3])) {
            continue;
        }
        RemoveEntryList(entry);
        (VOID)IoSetCancelRoutine(irp, NULL);
        InsertTailList(&mine, entry);
        pdo->ParkedCount--;
    }
    IoReleaseCancelSpinLock(cancelIrql);
    n = 0;
    while (!IsListEmpty(&mine)) {
        entry = RemoveHeadList(&mine);
        irp = CONTAINING_RECORD(entry, IRP, Tail.Overlay.ListEntry);
        hcdParkedComplete(pdo, irp);
        n++;
    }
    if (n != 0) {
        XHCI_DBG_VALUE("hcd: held URBs released, count", n);
    }
    return n;
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
    ULONG i;

    InitializeListHead(&pdo->RefusedIrps);
    InitializeListHead(&pdo->ParkedIrps);
    pdo->ParkedCount = 0;
    pdo->SubmitSeq.Lo = 0;
    pdo->SubmitSeq.Hi = 0;
    pdo->AbortAll.Lo = 0;
    pdo->AbortAll.Hi = 0;
    pdo->AbortCount = 0;
    for (i = 0; i < HCD_PDO_ABORTS; i++) {
        pdo->AbortPipe[i] = NULL;
        pdo->AbortHorizon[i].Lo = 0;
        pdo->AbortHorizon[i].Hi = 0;
    }
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
