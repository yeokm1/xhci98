/*
 * uas_xport.c - the UAS transport engine of xhciuas.sys: the request
 * queue, tags, the information units on the four pipes in both transports,
 * timeouts, task management and reset recovery (uas.h has the overview).
 *
 * A COMMAND. Under the FDO lock a queued request is given a tag and a slot
 * (uasPump); its transfers are built and marked busy; the lock is dropped
 * and they are submitted. Streamed (SuperSpeed): the status read and the
 * data transfer are posted on stream <tag> before the COMMAND IU, so the
 * device can answer in any order it likes. Streamless (High Speed): one
 * shared status read is kept posted while any command is in flight; a READ
 * READY or WRITE READY IU naming the tag releases the data transfer on the
 * shared data pipe; one command is in flight at a time. Either way the
 * command ends on its SENSE IU (SCSI status and sense) or a RESPONSE IU
 * (the device refused the COMMAND IU), and the request completes once the
 * final IU is in and none of its transfers is still busy (uasFinish).
 *
 * CANCELLATION. A transfer's IRP is this driver's own memory, initialised
 * before each use and freed only by UasEngineFree, after every transfer has
 * completed. So IoCancelIrp may be called on it after the lock is dropped
 * with no risk of reaching freed memory; a completion that lands first
 * leaves the cancel a no-op on an idle IRP. A transfer this driver
 * cancelled is marked Cancelling, so its error is not read as a fault.
 *
 * RECOVERY (uasRecoveryWorker, PASSIVE_LEVEL on a system worker thread).
 * Triggered by a command timing out (the one-second timer), by a transfer
 * failing, by SRB_FUNCTION_ABORT_COMMAND or SRB_FUNCTION_RESET_DEVICE. In
 * escalating order: ABORT TASK for a timed-out command; LOGICAL UNIT RESET
 * if that fails or a reset was asked for; and a device reset if either
 * fails or a transfer failed - every transfer cancelled, every request in
 * flight completed with SRB_STATUS_BUS_RESET (which the class driver
 * retries), then IOCTL_INTERNAL_USB_RESET_PORT, after which the bus has the
 * streams open again on the same handles. A device that cannot be brought back is marked Dead and every
 * later request fails with SRB_STATUS_NO_DEVICE.
 *
 * IRQL: as uas.h states per entry; completion routines and the timer DPC
 * at DISPATCH_LEVEL; the lock is held by no caller of any function here
 * except those marked "lock held".
 */

#include "uas.h"

typedef struct _UAS_ACT {
    PUAS_XFER Submit[4];
    ULONG SubmitCount;
    PIRP Cancel[UAS_XFERS + 1];
    ULONG CancelCount;
    PUAS_SLOT Finish[2];
    ULONG FinishCount;
    BOOLEAN Recover;
} UAS_ACT, *PUAS_ACT;

static VOID uasPump(PUAS_FDO fdo);
static VOID uasFinish(PUAS_FDO fdo, PUAS_SLOT slot);

/* ------------------------------------------------------------------ */
/* Small helpers                                                      */
/* ------------------------------------------------------------------ */

static VOID uasActInit(PUAS_ACT a)
{
    UasZero(a, sizeof(*a));
}

/* Lock held. */
static VOID uasActSubmit(PUAS_ACT a, PUAS_XFER x)
{
    if (a->SubmitCount < 4) {
        a->Submit[a->SubmitCount++] = x;
    }
}

/* Lock held. A busy transfer this driver means to cancel. */
static VOID uasActCancel(PUAS_ACT a, PUAS_XFER x)
{
    if (x->Busy && !x->Cancelling && a->CancelCount < UAS_XFERS + 1) {
        x->Cancelling = TRUE;
        a->Cancel[a->CancelCount++] = x->Irp;
    }
}

/* Lock held. Whether a recovery pass needs queueing; marks it queued. */
static BOOLEAN uasWantRecovery(PUAS_FDO fdo)
{
    if (!fdo->Started || fdo->RecoveryQueued) {
        return FALSE;
    }
    fdo->RecoveryQueued = TRUE;
    KeClearEvent(&fdo->RecoveryIdle);
    return TRUE;
}

/* Lock held. No transfer of the slot's is busy (the shared status read is
 * not the slot's). */
static BOOLEAN uasSlotIdle(PUAS_SLOT slot)
{
    ULONG i;

    for (i = 0; i < UAS_XFERS; i++) {
        if (slot->Xfer[i].Busy) {
            return FALSE;
        }
    }
    return TRUE;
}

/* Lock held. Whether any slot still waits on a status IU - the condition
 * for keeping the streamless shared read posted. */
static BOOLEAN uasNeedShared(PUAS_FDO fdo)
{
    ULONG t;
    PUAS_SLOT slot;

    for (t = 1; t <= fdo->Tags.Count; t++) {
        slot = &fdo->Slots[t];
        if (slot->State == UAS_SLOT_ACTIVE && !slot->Aborted &&
            !slot->Failed) {
            return TRUE;
        }
    }
    return FALSE;
}

/* Lock held. A final slot whose transfers are idle is ready: a command is
 * finished by the caller after the lock is dropped, a TMF wakes its waiter. */
static VOID uasCheckDone(PUAS_SLOT slot, PUAS_ACT a)
{
    if (slot->State == UAS_SLOT_FREE || !uasSlotIdle(slot)) {
        return;
    }
    if (slot->Tmf) {
        /* A failed TMF wakes its waiter too, rather than leaving it to
         * time out. */
        if ((slot->State == UAS_SLOT_FINAL || slot->Failed) &&
            !slot->TmfSignalled && slot->TmfDone != NULL) {
            slot->TmfSignalled = TRUE;
            KeSetEvent(slot->TmfDone, IO_NO_INCREMENT, FALSE);
        }
        return;
    }
    if (slot->State != UAS_SLOT_FINAL || slot->Failed || slot->Aborted ||
        slot->Finishing) {
        return;
    }
    if (a->FinishCount < 2) {
        slot->Finishing = TRUE;
        a->Finish[a->FinishCount++] = slot;
    }
}

static NTSTATUS uasXferDone(PDEVICE_OBJECT device, PIRP irp, PVOID context);

/* Lock held. Builds one bulk transfer and marks it busy. */
static VOID uasXferBuild(PUAS_FDO fdo, PUAS_XFER x, USBD_PIPE_HANDLE pipe,
                         PVOID buffer, PMDL mdl, ULONG length, BOOLEAN in)
{
    PIO_STACK_LOCATION next;

    UasZero(&x->Urb, sizeof(x->Urb));
    x->Urb.Hdr.Length = sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER);
    x->Urb.Hdr.Function = URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER;
    x->Urb.PipeHandle = pipe;
    x->Urb.TransferFlags = in ? (USBD_TRANSFER_DIRECTION_IN |
                                 USBD_SHORT_TRANSFER_OK) : 0;
    x->Urb.TransferBufferLength = length;
    x->Urb.TransferBuffer = (mdl == NULL) ? buffer : NULL;
    x->Urb.TransferBufferMDL = mdl;
    UasIrpReset(x->Irp, x->IrpSize, fdo->Lower->StackSize);
    next = IoGetNextIrpStackLocation(x->Irp);
    next->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
    next->Parameters.DeviceIoControl.IoControlCode =
        IOCTL_INTERNAL_USB_SUBMIT_URB;
    next->Parameters.Others.Argument1 = &x->Urb;
    UAS_SET_COMPLETION_ALWAYS(x->Irp, uasXferDone, x);
    x->Busy = TRUE;
    x->Cancelling = FALSE;
}

/* Lock held. The status read for a slot: on its own stream when streamed,
 * the shared read otherwise (posted only if not already). */
static VOID uasPostStatus(PUAS_FDO fdo, PUAS_SLOT slot, PUAS_ACT a)
{
    PUAS_XFER x;

    if (fdo->Streamed) {
        x = &slot->Xfer[UAS_XFER_STATUS];
        if (x->Busy) {
            return;
        }
        uasXferBuild(fdo, x,
                     UasStreamsPipe(&fdo->Streams, UAS_STREAM_STATUS,
                                    slot->Tag),
                     slot->Stat, NULL, UAS_STATUS_BUFFER, TRUE);
    } else {
        x = &fdo->SharedStatus;
        if (x->Busy) {
            return;
        }
        uasXferBuild(fdo, x, fdo->Pipe[UAS_PIPE_STATUS - 1],
                     fdo->SharedBuffer, NULL, UAS_STATUS_BUFFER, TRUE);
    }
    uasActSubmit(a, x);
}

/* Lock held. */
static VOID uasPostData(PUAS_FDO fdo, PUAS_SLOT slot, PUAS_ACT a)
{
    PUAS_XFER x;
    ULONG which;

    x = &slot->Xfer[UAS_XFER_DATA];
    if (x->Busy || slot->DataPosted || !slot->NeedsData) {
        return;
    }
    which = slot->DataIn ? UAS_STREAM_DATA_IN : UAS_STREAM_DATA_OUT;
    uasXferBuild(fdo, x,
                 fdo->Streamed
                     ? UasStreamsPipe(&fdo->Streams, which, slot->Tag)
                     : fdo->Pipe[slot->DataIn ? UAS_PIPE_DATA_IN - 1
                                              : UAS_PIPE_DATA_OUT - 1],
                 NULL, slot->DataMdl, slot->DataLength, slot->DataIn);
    slot->DataPosted = TRUE;
    uasActSubmit(a, x);
}

/* Lock held. */
static VOID uasPostIu(PUAS_FDO fdo, PUAS_SLOT slot, ULONG length,
                      PUAS_ACT a)
{
    PUAS_XFER x;

    x = &slot->Xfer[UAS_XFER_COMMAND];
    uasXferBuild(fdo, x, fdo->Pipe[UAS_PIPE_COMMAND - 1], slot->Iu, NULL,
                 length, FALSE);
    uasActSubmit(a, x);
}

/* No lock held: runs what a locked section decided. */
static VOID uasActRun(PUAS_FDO fdo, PUAS_ACT a)
{
    ULONG i;
    BOOLEAN queue;
    KIRQL irql;

    for (i = 0; i < a->SubmitCount; i++) {
        (VOID)IoCallDriver(fdo->Lower, a->Submit[i]->Irp);
    }
    for (i = 0; i < a->CancelCount; i++) {
        (VOID)IoCancelIrp(a->Cancel[i]);
    }
    for (i = 0; i < a->FinishCount; i++) {
        uasFinish(fdo, a->Finish[i]);
    }
    if (a->Recover) {
        KeAcquireSpinLock(&fdo->Lock, &irql);
        queue = uasWantRecovery(fdo);
        KeReleaseSpinLock(&fdo->Lock, irql);
        if (queue) {
            ExQueueWorkItem(&fdo->Recovery, DelayedWorkQueue);
        }
    }
    if (a->FinishCount != 0) {
        uasPump(fdo);
    }
}

/* ------------------------------------------------------------------ */
/* Completing a request                                               */
/* ------------------------------------------------------------------ */

static VOID uasCompleteRequest(PIRP irp, PSCSI_REQUEST_BLOCK srb)
{
    PKEVENT event;
    NTSTATUS status;
    ULONG s;

    event = (PKEVENT)irp->Tail.Overlay.DriverContext[UAS_CTX_EVENT];
    if (event != NULL) {
        KeSetEvent(event, IO_NO_INCREMENT, FALSE);
        return;
    }
    s = SRB_STATUS(srb->SrbStatus);
    if (s == SRB_STATUS_SUCCESS || s == SRB_STATUS_DATA_OVERRUN) {
        status = STATUS_SUCCESS;
    } else if (s == SRB_STATUS_BUSY) {
        status = STATUS_DEVICE_BUSY;
    } else if (s == SRB_STATUS_NO_DEVICE) {
        status = STATUS_NO_SUCH_DEVICE;
    } else {
        status = STATUS_IO_DEVICE_ERROR;
    }
    irp->IoStatus.Status = status;
    irp->IoStatus.Information = srb->DataTransferLength;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
}

/* A request that never reached a slot. */
static VOID uasFailRequest(PIRP irp, UCHAR srbStatus)
{
    PSCSI_REQUEST_BLOCK srb;

    srb = (PSCSI_REQUEST_BLOCK)irp->Tail.Overlay.DriverContext[UAS_CTX_SRB];
    srb->SrbStatus = srbStatus;
    srb->ScsiStatus = 0;
    srb->DataTransferLength = 0;
    uasCompleteRequest(irp, srb);
}

/* The response codes a COMMAND IU can be refused with, as SRB status. */
static UCHAR uasResponseStatus(ULONG code)
{
    switch (code) {
    case UAS_RC_OVERLAPPED_TAG:
        return SRB_STATUS_BUSY;
    case UAS_RC_INCORRECT_LUN:
        return SRB_STATUS_INVALID_LUN;
    default:
        return SRB_STATUS_ERROR;
    }
}

/*
 * No lock held. A command slot whose final IU is in (or which recovery has
 * ended, FinalSrbStatus set) and none of whose transfers is busy: the SRB
 * gets its status, the tag goes back, the request completes.
 */
static VOID uasFinish(PUAS_FDO fdo, PUAS_SLOT slot)
{
    PSCSI_REQUEST_BLOCK srb;
    PIRP request;
    UCHAR st;
    KIRQL irql;

    srb = slot->Srb;
    request = slot->Request;
    if (slot->FinalSrbStatus != 0) {
        st = (UCHAR)slot->FinalSrbStatus;
        srb->ScsiStatus = 0;
    } else if (slot->ResponseOnly) {
        st = uasResponseStatus(slot->ResponseCode);
        srb->ScsiStatus = 0;
    } else {
        st = (UCHAR)UasSrbStatus(slot->ScsiStatus, slot->DataLength,
                                 slot->Transferred);
        srb->ScsiStatus = (UCHAR)slot->ScsiStatus;
        if (slot->ScsiStatus == UAS_SCSI_CHECK_CONDITION) {
            fdo->Counters[UAS_CTR_SENSE]++;
        }
        if (st == SRB_STATUS_ERROR && slot->SenseLength != 0) {
            st |= SRB_STATUS_AUTOSENSE_VALID;
            srb->SenseInfoBufferLength = (UCHAR)slot->SenseLength;
        }
        if (st == SRB_STATUS_BUSY) {
            fdo->Counters[UAS_CTR_BUSY]++;
        }
    }
    if (slot->NeedsData) {
        srb->DataTransferLength = slot->Transferred;
    }
    srb->SrbStatus = st;
    UasFreeMdl(slot->DataMdl);
    slot->DataMdl = NULL;

    KeAcquireSpinLock(&fdo->Lock, &irql);
    (VOID)UasTagFree(&fdo->Tags, slot->Tag);
    if (fdo->Active != 0) {
        fdo->Active--;
    }
    slot->State = UAS_SLOT_FREE;
    slot->Request = NULL;
    slot->Srb = NULL;
    KeReleaseSpinLock(&fdo->Lock, irql);

    uasCompleteRequest(request, srb);
}

/* ------------------------------------------------------------------ */
/* Starting commands                                                  */
/* ------------------------------------------------------------------ */

/*
 * Lock held. Fills a free slot from a request. Returns 0 when it is ready to
 * go, or the SRB status to fail the request with.
 */
static UCHAR uasSlotPrepare(PUAS_FDO fdo, PUAS_SLOT slot, PIRP irp,
                            PSCSI_REQUEST_BLOCK srb, ULONG lun)
{
    ULONG dir;
    ULONG attribute;

    slot->Tmf = FALSE;
    slot->DataIn = FALSE;
    slot->DataPosted = FALSE;
    slot->NeedsData = FALSE;
    slot->TimedOut = FALSE;
    slot->Failed = FALSE;
    slot->Aborted = FALSE;
    slot->ResponseOnly = FALSE;
    slot->TmfSignalled = FALSE;
    slot->Finishing = FALSE;
    slot->TmfDone = NULL;
    slot->Request = irp;
    slot->Srb = srb;
    slot->Lun = lun;
    slot->DataMdl = NULL;
    slot->DataLength = 0;
    slot->Transferred = 0;
    slot->ScsiStatus = 0;
    slot->SenseLength = 0;
    slot->ResponseCode = 0;
    slot->FinalSrbStatus = 0;
    slot->TimeLeft = (srb->TimeOutValue != 0) ? (LONG)srb->TimeOutValue
                                              : UAS_DEFAULT_TIMEOUT;

    if (srb->DataTransferLength != 0) {
        if ((srb->SrbFlags & SRB_FLAGS_UNSPECIFIED_DIRECTION) ==
            SRB_FLAGS_DATA_IN) {
            dir = UAS_DIR_IN;
        } else if ((srb->SrbFlags & SRB_FLAGS_UNSPECIFIED_DIRECTION) ==
                   SRB_FLAGS_DATA_OUT) {
            dir = UAS_DIR_OUT;
        } else {
            dir = UasCdbDirection(srb->Cdb[0]);
        }
        if (dir == UAS_DIR_NONE) {
            return SRB_STATUS_INVALID_REQUEST;
        }
        slot->NeedsData = TRUE;
        slot->DataIn = (BOOLEAN)(dir == UAS_DIR_IN);
        slot->DataLength = srb->DataTransferLength;
        slot->DataMdl = UasDataMdl(irp->MdlAddress != NULL ? irp : NULL, srb);
        if (slot->DataMdl == NULL) {
            return SRB_STATUS_BUSY;
        }
    }

    attribute = UAS_TASK_SIMPLE;
    if ((srb->SrbFlags & SRB_FLAGS_QUEUE_ACTION_ENABLE) != 0) {
        if (srb->QueueAction == SRB_HEAD_OF_QUEUE_TAG_REQUEST) {
            attribute = UAS_TASK_HEAD_OF_QUEUE;
        } else if (srb->QueueAction == SRB_ORDERED_QUEUE_TAG_REQUEST) {
            attribute = UAS_TASK_ORDERED;
        }
    }
    if (UasIuBuildCommand(slot->Iu, sizeof(slot->Iu), slot->Tag, lun,
                          attribute, srb->Cdb, srb->CdbLength) == 0) {
        UasFreeMdl(slot->DataMdl);
        slot->DataMdl = NULL;
        return SRB_STATUS_INVALID_REQUEST;
    }
    return 0;
}

/*
 * <= DISPATCH_LEVEL, no lock held. Starts queued requests while the queue
 * depth and the tags allow. A request whose LUN has its queue locked waits
 * unless it bypasses the lock (the class driver's power SRBs).
 */
static VOID uasPump(PUAS_FDO fdo)
{
    PLIST_ENTRY e;
    PIRP irp;
    PIRP failIrp;
    PSCSI_REQUEST_BLOCK srb;
    PUAS_PDO pdo;
    PUAS_SLOT slot;
    UAS_ACT a;
    ULONG tag;
    ULONG lun;
    UCHAR fail;
    KIRQL irql;

    for (;;) {
        uasActInit(&a);
        failIrp = NULL;
        fail = 0;
        KeAcquireSpinLock(&fdo->Lock, &irql);
        if (!fdo->Started || fdo->Recovering || fdo->Slots == NULL) {
            KeReleaseSpinLock(&fdo->Lock, irql);
            return;
        }
        if (fdo->Dead || fdo->Gone) {
            if (IsListEmpty(&fdo->Queue)) {
                KeReleaseSpinLock(&fdo->Lock, irql);
                return;
            }
            e = RemoveHeadList(&fdo->Queue);
            KeReleaseSpinLock(&fdo->Lock, irql);
            uasFailRequest(CONTAINING_RECORD(e, IRP, Tail.Overlay.ListEntry),
                           SRB_STATUS_NO_DEVICE);
            continue;
        }
        if (fdo->Active >= fdo->QueueDepth) {
            KeReleaseSpinLock(&fdo->Lock, irql);
            return;
        }
        irp = NULL;
        for (e = fdo->Queue.Flink; e != &fdo->Queue; e = e->Flink) {
            irp = CONTAINING_RECORD(e, IRP, Tail.Overlay.ListEntry);
            pdo = (PUAS_PDO)irp->Tail.Overlay.DriverContext[UAS_CTX_PDO];
            srb = (PSCSI_REQUEST_BLOCK)
                      irp->Tail.Overlay.DriverContext[UAS_CTX_SRB];
            if (pdo == NULL || !pdo->QueueLocked ||
                (srb->SrbFlags & SRB_FLAGS_BYPASS_LOCKED_QUEUE) != 0) {
                break;
            }
            irp = NULL;
        }
        if (irp == NULL) {
            KeReleaseSpinLock(&fdo->Lock, irql);
            return;
        }
        tag = UasTagAlloc(&fdo->Tags);
        if (tag == 0) {
            KeReleaseSpinLock(&fdo->Lock, irql);
            return;
        }
        RemoveEntryList(&irp->Tail.Overlay.ListEntry);
        pdo = (PUAS_PDO)irp->Tail.Overlay.DriverContext[UAS_CTX_PDO];
        srb = (PSCSI_REQUEST_BLOCK)irp->Tail.Overlay.DriverContext[UAS_CTX_SRB];
        lun = (pdo != NULL) ? pdo->Lun : srb->Lun;
        slot = &fdo->Slots[tag];
        fail = uasSlotPrepare(fdo, slot, irp, srb, lun);
        if (fail != 0) {
            (VOID)UasTagFree(&fdo->Tags, tag);
            failIrp = irp;
        } else {
            slot->State = UAS_SLOT_ACTIVE;
            fdo->Active++;
            fdo->Counters[UAS_CTR_COMMANDS]++;
            uasPostStatus(fdo, slot, &a);
            if (fdo->Streamed) {
                uasPostData(fdo, slot, &a);
            }
            uasPostIu(fdo, slot, UAS_IU_COMMAND_LENGTH, &a);
        }
        KeReleaseSpinLock(&fdo->Lock, irql);
        if (failIrp != NULL) {
            uasFailRequest(failIrp, fail);
            continue;
        }
        uasActRun(fdo, &a);
    }
}

/* ------------------------------------------------------------------ */
/* Completions                                                        */
/* ------------------------------------------------------------------ */

/* Lock held. Marks a slot failed and asks for recovery. */
static VOID uasSlotFault(PUAS_FDO fdo, PUAS_SLOT slot, PUAS_ACT a)
{
    if (slot->State == UAS_SLOT_FREE || slot->Aborted) {
        return;
    }
    slot->Failed = TRUE;
    a->Recover = TRUE;
    UNREFERENCED_PARAMETER(fdo);
}

/* Lock held. One status IU read: dispatch it by tag. */
static VOID uasStatusIu(PUAS_FDO fdo, PUAS_XFER x, PUAS_ACT a)
{
    UAS_IU_INFO iu;
    PUAS_SLOT target;
    PUCHAR buffer;
    PSCSI_REQUEST_BLOCK srb;
    ULONG parsed;
    ULONG copy;

    buffer = (x->Slot != NULL) ? x->Slot->Stat : fdo->SharedBuffer;
    parsed = UasIuParse(buffer, x->Urb.TransferBufferLength, &iu);
    target = NULL;
    if (parsed == UAS_PARSE_OK && iu.Tag >= 1 && iu.Tag <= fdo->Tags.Count &&
        fdo->Slots[iu.Tag].State == UAS_SLOT_ACTIVE &&
        !fdo->Slots[iu.Tag].Aborted) {
        target = &fdo->Slots[iu.Tag];
    }
    if (target == NULL || (x->Slot != NULL && target != x->Slot)) {
        fdo->Counters[UAS_CTR_PROTOCOL]++;
        if (x->Slot != NULL) {
            uasSlotFault(fdo, x->Slot, a);
        }
        return;
    }

    switch (iu.Id) {
    case UAS_IU_SENSE:
        if (target->Tmf) {
            fdo->Counters[UAS_CTR_PROTOCOL]++;
            uasSlotFault(fdo, target, a);
            return;
        }
        target->ScsiStatus = iu.Status;
        target->SenseLength = 0;
        srb = target->Srb;
        if (iu.SenseLength != 0 && srb->SenseInfoBuffer != NULL &&
            srb->SenseInfoBufferLength != 0 &&
            (srb->SrbFlags & SRB_FLAGS_DISABLE_AUTOSENSE) == 0) {
            copy = iu.SenseLength;
            if (copy > srb->SenseInfoBufferLength) {
                copy = srb->SenseInfoBufferLength;
            }
            UasCopy(srb->SenseInfoBuffer, buffer + UAS_IU_SENSE_HEADER, copy);
            target->SenseLength = copy;
        }
        target->State = UAS_SLOT_FINAL;
        if (iu.Status != UAS_SCSI_GOOD) {
            uasActCancel(a, &target->Xfer[UAS_XFER_DATA]);
        }
        break;

    case UAS_IU_RESPONSE:
        target->ResponseCode = iu.ResponseCode;
        target->State = UAS_SLOT_FINAL;
        if (!target->Tmf) {
            target->ResponseOnly = TRUE;
            fdo->Counters[UAS_CTR_PROTOCOL]++;
            uasActCancel(a, &target->Xfer[UAS_XFER_DATA]);
        }
        break;

    case UAS_IU_READ_READY:
    case UAS_IU_WRITE_READY:
        if (target->Tmf || !target->NeedsData ||
            target->DataIn != (BOOLEAN)(iu.Id == UAS_IU_READ_READY)) {
            fdo->Counters[UAS_CTR_PROTOCOL]++;
            uasSlotFault(fdo, target, a);
            return;
        }
        /* Streamed, the data transfer went out with the command and the
         * READY is only the device catching up; streamless, it releases
         * the data phase. */
        uasPostData(fdo, target, a);
        break;

    default:
        fdo->Counters[UAS_CTR_PROTOCOL]++;
        uasSlotFault(fdo, target, a);
        break;
    }
}

static NTSTATUS uasXferDone(PDEVICE_OBJECT device, PIRP irp, PVOID context)
{
    PUAS_XFER x;
    PUAS_FDO fdo;
    PUAS_SLOT slot;
    UAS_ACT a;
    BOOLEAN ok;
    BOOLEAN cancelled;
    ULONG t;
    KIRQL irql;

    UNREFERENCED_PARAMETER(device);
    x = (PUAS_XFER)context;
    fdo = x->Fdo;
    slot = x->Slot;
    uasActInit(&a);
    ok = (BOOLEAN)(NT_SUCCESS(irp->IoStatus.Status) &&
                   USBD_SUCCESS(x->Urb.Hdr.Status));

    KeAcquireSpinLock(&fdo->Lock, &irql);
    x->Busy = FALSE;
    cancelled = x->Cancelling;
    x->Cancelling = FALSE;

    if (!ok) {
        if (!cancelled) {
            if (slot != NULL) {
                uasSlotFault(fdo, slot, &a);
            } else {
                /* The shared status read: every slot waiting on it. */
                for (t = 1; t <= fdo->Tags.Count; t++) {
                    if (fdo->Slots[t].State != UAS_SLOT_FREE) {
                        uasSlotFault(fdo, &fdo->Slots[t], &a);
                    }
                }
            }
        }
    } else if (x->Kind == UAS_XFER_STATUS) {
        uasStatusIu(fdo, x, &a);
    } else if (x->Kind == UAS_XFER_DATA) {
        slot->Transferred = x->Urb.TransferBufferLength;
    }

    /* Keep reading status while something still waits for it. */
    if (x->Kind == UAS_XFER_STATUS && !fdo->Gone) {
        if (slot != NULL) {
            if (slot->State == UAS_SLOT_ACTIVE && !slot->Failed &&
                !slot->Aborted) {
                uasPostStatus(fdo, slot, &a);
            }
        } else if (uasNeedShared(fdo)) {
            uasPostStatus(fdo, NULL, &a);
        }
    }

    if (slot != NULL) {
        uasCheckDone(slot, &a);
    } else {
        for (t = 1; t <= fdo->Tags.Count; t++) {
            uasCheckDone(&fdo->Slots[t], &a);
        }
    }
    KeReleaseSpinLock(&fdo->Lock, irql);

    uasActRun(fdo, &a);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

/* ------------------------------------------------------------------ */
/* The timer                                                          */
/* ------------------------------------------------------------------ */

static VOID uasArmTimer(PUAS_FDO fdo)
{
    LARGE_INTEGER due;

    due.LowPart = (ULONG)-10000000L;    /* one second, relative */
    due.HighPart = -1;
    (VOID)KeSetTimer(&fdo->Timer, due, &fdo->TimerDpc);
}

static VOID uasTick(PKDPC dpc, PVOID context, PVOID arg1, PVOID arg2)
{
    PUAS_FDO fdo;
    PUAS_SLOT slot;
    BOOLEAN queue;
    BOOLEAN expired;
    ULONG t;
    KIRQL irql;

    UNREFERENCED_PARAMETER(dpc);
    UNREFERENCED_PARAMETER(arg1);
    UNREFERENCED_PARAMETER(arg2);
    fdo = (PUAS_FDO)context;
    queue = FALSE;
    expired = FALSE;
    KeAcquireSpinLock(&fdo->Lock, &irql);
    if (fdo->TimerStop) {
        KeSetEvent(&fdo->TimerDone, IO_NO_INCREMENT, FALSE);
        KeReleaseSpinLock(&fdo->Lock, irql);
        return;
    }
    for (t = 1; t <= fdo->Tags.Count; t++) {
        slot = &fdo->Slots[t];
        if (slot->State == UAS_SLOT_FREE || slot->Tmf || slot->TimedOut ||
            slot->Failed || slot->Aborted || slot->Finishing) {
            continue;
        }
        slot->TimeLeft--;
        if (slot->TimeLeft <= 0) {
            slot->TimedOut = TRUE;
            fdo->Counters[UAS_CTR_TIMEOUTS]++;
            expired = TRUE;
        }
    }
    if (expired) {
        queue = uasWantRecovery(fdo);
    }
    /* Re-armed under the lock, so UasEngineStop's KeCancelTimer either
     * finds it queued or this DPC finds TimerStop. */
    uasArmTimer(fdo);
    KeReleaseSpinLock(&fdo->Lock, irql);
    if (queue) {
        ExQueueWorkItem(&fdo->Recovery, DelayedWorkQueue);
    }
}

/* ------------------------------------------------------------------ */
/* Recovery (PASSIVE_LEVEL)                                           */
/* ------------------------------------------------------------------ */

static VOID uasSleepMs(ULONG ms)
{
    LARGE_INTEGER t;

    t.LowPart = (ULONG)-(LONG)(ms * 10000UL);
    t.HighPart = -1;
    (VOID)KeDelayExecutionThread(KernelMode, FALSE, &t);
}

/* Waits for every transfer of the given slot (NULL: of every slot, and the
 * shared read) to be idle. FALSE after five seconds. */
static BOOLEAN uasWaitIdle(PUAS_FDO fdo, PUAS_SLOT only)
{
    ULONG round;
    ULONG t;
    BOOLEAN busy;
    KIRQL irql;

    for (round = 0; round < 500; round++) {
        KeAcquireSpinLock(&fdo->Lock, &irql);
        if (only != NULL) {
            busy = (BOOLEAN)!uasSlotIdle(only);
        } else {
            busy = fdo->SharedStatus.Busy;
            for (t = 1; t <= UAS_MAX_TAGS && !busy; t++) {
                busy = (BOOLEAN)!uasSlotIdle(&fdo->Slots[t]);
            }
        }
        KeReleaseSpinLock(&fdo->Lock, irql);
        if (!busy) {
            return TRUE;
        }
        uasSleepMs(10);
    }
    return FALSE;
}

/* Cancels every busy transfer of a slot (and the shared read when asked),
 * then waits for them. */
static BOOLEAN uasCancelSlot(PUAS_FDO fdo, PUAS_SLOT slot, BOOLEAN shared)
{
    UAS_ACT a;
    ULONG i;
    KIRQL irql;

    uasActInit(&a);
    KeAcquireSpinLock(&fdo->Lock, &irql);
    for (i = 0; i < UAS_XFERS; i++) {
        uasActCancel(&a, &slot->Xfer[i]);
    }
    if (shared) {
        uasActCancel(&a, &fdo->SharedStatus);
    }
    KeReleaseSpinLock(&fdo->Lock, irql);
    for (i = 0; i < a.CancelCount; i++) {
        (VOID)IoCancelIrp(a.Cancel[i]);
    }
    return uasWaitIdle(fdo, slot);
}

/*
 * One task management function, waited for. Returns the response code, or
 * UAS_RC_TMF_FAILED when it could not be sent or no answer came.
 */
static ULONG uasTmf(PUAS_FDO fdo, ULONG function, ULONG lun, ULONG taskTag)
{
    PUAS_SLOT slot;
    UAS_ACT a;
    KEVENT done;
    LARGE_INTEGER timeout;
    NTSTATUS wait;
    ULONG tag;
    ULONG code;
    ULONG length;
    BOOLEAN lonely;
    KIRQL irql;

    KeInitializeEvent(&done, NotificationEvent, FALSE);
    uasActInit(&a);
    KeAcquireSpinLock(&fdo->Lock, &irql);
    tag = UasTagAlloc(&fdo->Tags);
    if (tag == 0) {
        KeReleaseSpinLock(&fdo->Lock, irql);
        return UAS_RC_TMF_FAILED;
    }
    slot = &fdo->Slots[tag];
    UasZero(slot->Iu, sizeof(slot->Iu));
    length = UasIuBuildTaskMgmt(slot->Iu, sizeof(slot->Iu), tag, function,
                                taskTag, lun);
    slot->Tmf = TRUE;
    slot->TmfDone = &done;
    slot->TmfSignalled = FALSE;
    slot->Finishing = FALSE;
    slot->Failed = FALSE;
    slot->Aborted = FALSE;
    slot->TimedOut = FALSE;
    slot->NeedsData = FALSE;
    slot->DataPosted = FALSE;
    slot->ResponseOnly = FALSE;
    slot->ResponseCode = UAS_RC_TMF_FAILED;
    slot->Request = NULL;
    slot->Srb = NULL;
    slot->Lun = lun;
    slot->State = UAS_SLOT_ACTIVE;
    uasPostStatus(fdo, slot, &a);
    uasPostIu(fdo, slot, length, &a);
    KeReleaseSpinLock(&fdo->Lock, irql);
    uasActRun(fdo, &a);

    timeout.LowPart = (ULONG)-(LONG)(UAS_TMF_TIMEOUT_MS * 10000UL);
    timeout.HighPart = -1;
    wait = KeWaitForSingleObject(&done, Executive, KernelMode, FALSE,
                                 &timeout);

    KeAcquireSpinLock(&fdo->Lock, &irql);
    code = (wait == STATUS_SUCCESS && slot->State == UAS_SLOT_FINAL &&
            !slot->Failed) ? slot->ResponseCode : UAS_RC_TMF_FAILED;
    slot->Aborted = TRUE;
    slot->State = UAS_SLOT_FINAL;
    lonely = (BOOLEAN)!uasNeedShared(fdo);
    KeReleaseSpinLock(&fdo->Lock, irql);

    (VOID)uasCancelSlot(fdo, slot, (BOOLEAN)(!fdo->Streamed && lonely));

    KeAcquireSpinLock(&fdo->Lock, &irql);
    slot->State = UAS_SLOT_FREE;
    slot->Tmf = FALSE;
    slot->TmfDone = NULL;
    (VOID)UasTagFree(&fdo->Tags, tag);
    KeReleaseSpinLock(&fdo->Lock, irql);
    return code;
}

static BOOLEAN uasTmfOk(ULONG code)
{
    return (BOOLEAN)(code == UAS_RC_TMF_COMPLETE ||
                     code == UAS_RC_TMF_SUCCEEDED);
}

/* Ends one command slot from recovery: its transfers cancelled and waited
 * for, its request completed with the given status. */
static VOID uasEndSlot(PUAS_FDO fdo, PUAS_SLOT slot, UCHAR srbStatus)
{
    BOOLEAN lonely;
    KIRQL irql;

    KeAcquireSpinLock(&fdo->Lock, &irql);
    if (slot->State == UAS_SLOT_FREE || slot->Tmf || slot->Finishing) {
        /* Free, a TMF, or already handed to uasFinish by a completion. */
        KeReleaseSpinLock(&fdo->Lock, irql);
        return;
    }
    slot->Aborted = TRUE;
    slot->Finishing = TRUE;
    slot->FinalSrbStatus = srbStatus;
    lonely = (BOOLEAN)!uasNeedShared(fdo);
    KeReleaseSpinLock(&fdo->Lock, irql);
    (VOID)uasCancelSlot(fdo, slot, (BOOLEAN)(!fdo->Streamed && lonely));
    uasFinish(fdo, slot);
}

/* Every command of one LUN (all LUNs when lun is ~0). */
static VOID uasEndLun(PUAS_FDO fdo, ULONG lun, UCHAR srbStatus)
{
    ULONG t;

    for (t = 1; t <= UAS_MAX_TAGS; t++) {
        if (lun == (ULONG)~0UL || fdo->Slots[t].Lun == lun) {
            uasEndSlot(fdo, &fdo->Slots[t], srbStatus);
        }
    }
}

/*
 * Every command in flight ended with srbStatus: each slot marked aborted
 * (so no completion re-posts a status read for it), every busy transfer
 * cancelled and waited for, every request completed. FALSE when a transfer
 * never completed, in which case nothing may be reused and the FDO is Dead.
 */
static BOOLEAN uasAbortAll(PUAS_FDO fdo, UCHAR srbStatus)
{
    UAS_ACT a;
    ULONG t;
    ULONG i;
    BOOLEAN idle;
    KIRQL irql;

    KeAcquireSpinLock(&fdo->Lock, &irql);
    for (t = 1; t <= UAS_MAX_TAGS; t++) {
        if (fdo->Slots[t].State != UAS_SLOT_FREE) {
            fdo->Slots[t].Aborted = TRUE;
        }
    }
    KeReleaseSpinLock(&fdo->Lock, irql);
    /* Cancel in rounds: UAS_ACT holds a slot's worth at a time. */
    for (t = 0; t <= UAS_MAX_TAGS; t++) {
        uasActInit(&a);
        KeAcquireSpinLock(&fdo->Lock, &irql);
        if (t == 0) {
            uasActCancel(&a, &fdo->SharedStatus);
        } else {
            for (i = 0; i < UAS_XFERS; i++) {
                uasActCancel(&a, &fdo->Slots[t].Xfer[i]);
            }
        }
        KeReleaseSpinLock(&fdo->Lock, irql);
        for (i = 0; i < a.CancelCount; i++) {
            (VOID)IoCancelIrp(a.Cancel[i]);
        }
    }
    idle = uasWaitIdle(fdo, NULL);
    uasEndLun(fdo, (ULONG)~0UL, srbStatus);
    if (!idle) {
        KeAcquireSpinLock(&fdo->Lock, &irql);
        fdo->Dead = TRUE;
        KeReleaseSpinLock(&fdo->Lock, irql);
    }
    return idle;
}

/*
 * The last resort: everything in flight ended with BUS_RESET (NO_DEVICE if
 * the device has gone), then the port reset. The bus restores the device
 * with its streams open and every stream handle valid
 * (src\xhci98_streams.h), so nothing is reopened here.
 */
static VOID uasResetDevice(PUAS_FDO fdo)
{
    NTSTATUS status;
    KIRQL irql;

    if (!uasAbortAll(fdo, (UCHAR)(fdo->Gone ? SRB_STATUS_NO_DEVICE
                                            : SRB_STATUS_BUS_RESET)) ||
        fdo->Gone) {
        return;
    }
    fdo->Counters[UAS_CTR_PORT_RESETS]++;
    status = UasSyncIoctl(fdo, IOCTL_INTERNAL_USB_RESET_PORT);
    if (!NT_SUCCESS(status)) {
        KeAcquireSpinLock(&fdo->Lock, &irql);
        fdo->Dead = TRUE;
        KeReleaseSpinLock(&fdo->Lock, irql);
    }
}

/* Lock held. What recovery should look at next. */
#define UAS_RECOVER_NONE    0
#define UAS_RECOVER_ABORT   1
#define UAS_RECOVER_LUN     2
#define UAS_RECOVER_RESET   3

static ULONG uasRecoveryNext(PUAS_FDO fdo, PULONG tag, PULONG lun)
{
    PUAS_SLOT slot;
    ULONG t;

    if (fdo->Gone) {
        for (t = 1; t <= UAS_MAX_TAGS; t++) {
            if (fdo->Slots[t].State != UAS_SLOT_FREE && !fdo->Slots[t].Tmf) {
                return UAS_RECOVER_RESET;
            }
        }
        return UAS_RECOVER_NONE;
    }
    for (t = 1; t <= UAS_MAX_TAGS; t++) {
        slot = &fdo->Slots[t];
        if (slot->State != UAS_SLOT_FREE && !slot->Tmf && slot->Failed &&
            slot->FinalSrbStatus == 0) {
            return UAS_RECOVER_RESET;
        }
    }
    if (fdo->ResetRequested) {
        fdo->ResetRequested = FALSE;
        *lun = fdo->ResetLun;
        return UAS_RECOVER_LUN;
    }
    for (t = 1; t <= UAS_MAX_TAGS; t++) {
        slot = &fdo->Slots[t];
        if (slot->State != UAS_SLOT_FREE && !slot->Tmf && slot->TimedOut &&
            !slot->Aborted && !slot->Finishing) {
            *tag = t;
            *lun = slot->Lun;
            return UAS_RECOVER_ABORT;
        }
    }
    return UAS_RECOVER_NONE;
}

static VOID uasRecoveryWorker(PVOID context)
{
    PUAS_FDO fdo;
    ULONG what;
    ULONG tag;
    ULONG lun;
    ULONG rounds;
    KIRQL irql;

    fdo = (PUAS_FDO)context;
    for (rounds = 0;; rounds++) {
        tag = 0;
        lun = 0;
        KeAcquireSpinLock(&fdo->Lock, &irql);
        fdo->Recovering = TRUE;
        what = (fdo->Dead && !fdo->Gone) ? UAS_RECOVER_NONE
                                          : uasRecoveryNext(fdo, &tag, &lun);
        if (what == UAS_RECOVER_NONE) {
            /* Decided under the same hold that ends the pass, so a fault
             * flagged after this sees RecoveryQueued FALSE and queues
             * another. */
            fdo->Recovering = FALSE;
            fdo->RecoveryQueued = FALSE;
            KeSetEvent(&fdo->RecoveryIdle, IO_NO_INCREMENT, FALSE);
            KeReleaseSpinLock(&fdo->Lock, irql);
            break;
        }
        KeReleaseSpinLock(&fdo->Lock, irql);

        if (rounds >= 16) {
            what = UAS_RECOVER_RESET;
        }
        if (what == UAS_RECOVER_ABORT) {
            fdo->Counters[UAS_CTR_ABORTS]++;
            if (uasTmfOk(uasTmf(fdo, UAS_TMF_ABORT_TASK, lun, tag))) {
                uasEndSlot(fdo, &fdo->Slots[tag], SRB_STATUS_TIMEOUT);
                continue;
            }
            what = UAS_RECOVER_LUN;
        }
        if (what == UAS_RECOVER_LUN) {
            fdo->Counters[UAS_CTR_LUN_RESETS]++;
            if (uasTmfOk(uasTmf(fdo, UAS_TMF_LOGICAL_UNIT_RESET, lun, 0))) {
                uasEndLun(fdo, lun, SRB_STATUS_BUS_RESET);
                continue;
            }
        }
        uasResetDevice(fdo);
    }
    uasPump(fdo);
}

/* ------------------------------------------------------------------ */
/* Entries                                                            */
/* ------------------------------------------------------------------ */

/* PASSIVE_LEVEL. The slots, their IRPs, the shared read, the timer. */
NTSTATUS UasEngineInit(PUAS_FDO fdo)
{
    PUAS_SLOT slot;
    PUAS_XFER x;
    CCHAR stack;
    ULONG t;
    ULONG i;

    stack = fdo->Lower->StackSize;
    fdo->Slots = (PUAS_SLOT)UasAlloc(sizeof(UAS_SLOT) * (UAS_MAX_TAGS + 1));
    fdo->SharedBuffer = (PUCHAR)UasAlloc(UAS_STATUS_BUFFER);
    if (fdo->Slots == NULL || fdo->SharedBuffer == NULL) {
        goto cleanup;
    }
    for (t = 1; t <= UAS_MAX_TAGS; t++) {
        slot = &fdo->Slots[t];
        slot->Tag = t;
        for (i = 0; i < UAS_XFERS; i++) {
            x = &slot->Xfer[i];
            x->Fdo = fdo;
            x->Slot = slot;
            x->Kind = i;
            x->Irp = UasIrpAlloc(stack, &x->IrpSize);
            if (x->Irp == NULL) {
                goto cleanup;
            }
        }
    }
    x = &fdo->SharedStatus;
    x->Fdo = fdo;
    x->Slot = NULL;
    x->Kind = UAS_XFER_STATUS;
    x->Irp = UasIrpAlloc(stack, &x->IrpSize);
    if (x->Irp == NULL) {
        goto cleanup;
    }
    KeInitializeTimer(&fdo->Timer);
    KeInitializeDpc(&fdo->TimerDpc, uasTick, fdo);
    ExInitializeWorkItem(&fdo->Recovery, uasRecoveryWorker, fdo);
    return STATUS_SUCCESS;

cleanup:
    UasEngineFree(fdo);
    return STATUS_INSUFFICIENT_RESOURCES;
}

/* PASSIVE_LEVEL, after every transfer has completed (UasEngineStop). */
VOID UasEngineFree(PUAS_FDO fdo)
{
    ULONG t;
    ULONG i;

    if (fdo->Slots != NULL) {
        for (t = 1; t <= UAS_MAX_TAGS; t++) {
            for (i = 0; i < UAS_XFERS; i++) {
                UasFree(fdo->Slots[t].Xfer[i].Irp);
            }
        }
        UasFree(fdo->Slots);
        fdo->Slots = NULL;
    }
    UasFree(fdo->SharedStatus.Irp);
    fdo->SharedStatus.Irp = NULL;
    UasFree(fdo->SharedBuffer);
    fdo->SharedBuffer = NULL;
}

/*
 * PASSIVE_LEVEL. Tags and depth for the transport the FDO chose: streamed,
 * one tag per granted stream with one kept back for task management;
 * streamless, one command and one task management tag.
 */
VOID UasEngineStart(PUAS_FDO fdo)
{
    KIRQL irql;

    KeAcquireSpinLock(&fdo->Lock, &irql);
    if (fdo->Streamed) {
        UasTagInit(&fdo->Tags, fdo->Streams.Granted);
        fdo->QueueDepth = fdo->Streams.Granted - 1;
    } else {
        UasTagInit(&fdo->Tags, 2);
        fdo->QueueDepth = 1;
    }
    fdo->Active = 0;
    fdo->Dead = FALSE;
    fdo->Recovering = FALSE;
    fdo->RecoveryQueued = FALSE;
    fdo->ResetRequested = FALSE;
    fdo->TimerStop = FALSE;
    KeClearEvent(&fdo->TimerDone);
    fdo->Started = TRUE;
    uasArmTimer(fdo);
    KeReleaseSpinLock(&fdo->Lock, irql);
}

/*
 * PASSIVE_LEVEL. Stops the timer and recovery, ends every request in flight
 * or queued with srbStatus, and waits for every transfer. The engine can be
 * started again afterwards.
 */
VOID UasEngineStop(PUAS_FDO fdo, UCHAR srbStatus)
{
    LIST_ENTRY drain;
    PLIST_ENTRY e;
    BOOLEAN timerQueued;
    BOOLEAN wasStarted;
    KIRQL irql;

    InitializeListHead(&drain);
    KeAcquireSpinLock(&fdo->Lock, &irql);
    wasStarted = fdo->Started;
    fdo->Started = FALSE;
    fdo->TimerStop = TRUE;
    KeReleaseSpinLock(&fdo->Lock, irql);
    if (wasStarted) {
        timerQueued = KeCancelTimer(&fdo->Timer);
        if (!timerQueued) {
            (VOID)KeWaitForSingleObject(&fdo->TimerDone, Executive,
                                        KernelMode, FALSE, NULL);
        }
    }
    (VOID)KeWaitForSingleObject(&fdo->RecoveryIdle, Executive, KernelMode,
                                FALSE, NULL);
    if (fdo->Slots != NULL) {
        (VOID)uasAbortAll(fdo, srbStatus);
    }
    KeAcquireSpinLock(&fdo->Lock, &irql);
    while (!IsListEmpty(&fdo->Queue)) {
        e = RemoveHeadList(&fdo->Queue);
        InsertTailList(&drain, e);
    }
    KeReleaseSpinLock(&fdo->Lock, irql);
    while (!IsListEmpty(&drain)) {
        e = RemoveHeadList(&drain);
        uasFailRequest(CONTAINING_RECORD(e, IRP, Tail.Overlay.ListEntry),
                       srbStatus);
    }
}

/* <= DISPATCH_LEVEL. Queues one SRB's IRP; it completes later. */
NTSTATUS UasEngineSubmit(PUAS_FDO fdo, PUAS_PDO pdo, PIRP irp,
                         PSCSI_REQUEST_BLOCK srb)
{
    KIRQL irql;
    UCHAR refuse;

    irp->Tail.Overlay.DriverContext[UAS_CTX_PDO] = pdo;
    irp->Tail.Overlay.DriverContext[UAS_CTX_SRB] = srb;
    irp->Tail.Overlay.DriverContext[UAS_CTX_EVENT] = NULL;
    srb->SrbStatus = SRB_STATUS_PENDING;
    refuse = 0;
    KeAcquireSpinLock(&fdo->Lock, &irql);
    if (!fdo->Started || fdo->Gone || fdo->Dead) {
        refuse = SRB_STATUS_NO_DEVICE;
    } else {
        IoMarkIrpPending(irp);
        InsertTailList(&fdo->Queue, &irp->Tail.Overlay.ListEntry);
    }
    KeReleaseSpinLock(&fdo->Lock, irql);
    if (refuse != 0) {
        uasFailRequest(irp, refuse);
        return STATUS_NO_SUCH_DEVICE;
    }
    uasPump(fdo);
    return STATUS_PENDING;
}

/*
 * PASSIVE_LEVEL. One command of the driver's own (REPORT LUNS, INQUIRY)
 * through the same engine, waited for. buffer is nonpaged pool, read into.
 */
NTSTATUS UasInternalCommand(PUAS_FDO fdo, ULONG lun, const UCHAR *cdb,
                            ULONG cdbLength, PVOID buffer, ULONG length,
                            PULONG transferred, PUCHAR scsiStatus)
{
    PSCSI_REQUEST_BLOCK srb;
    PIRP irp;
    KEVENT done;
    NTSTATUS status;
    BOOLEAN queued;
    KIRQL irql;

    *transferred = 0;
    *scsiStatus = 0;
    if (cdbLength > 16) {
        return STATUS_INVALID_PARAMETER;
    }
    srb = (PSCSI_REQUEST_BLOCK)UasAlloc(sizeof(SCSI_REQUEST_BLOCK));
    irp = IoAllocateIrp(1, FALSE);
    if (srb == NULL || irp == NULL) {
        UasFree(srb);
        if (irp != NULL) {
            IoFreeIrp(irp);
        }
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    KeInitializeEvent(&done, NotificationEvent, FALSE);
    srb->Length = sizeof(SCSI_REQUEST_BLOCK);
    srb->Function = SRB_FUNCTION_EXECUTE_SCSI;
    srb->Lun = (UCHAR)lun;
    srb->CdbLength = (UCHAR)cdbLength;
    UasCopy(srb->Cdb, cdb, cdbLength);
    srb->DataBuffer = buffer;
    srb->DataTransferLength = length;
    srb->SrbFlags = (length != 0) ? SRB_FLAGS_DATA_IN
                                  : SRB_FLAGS_NO_DATA_TRANSFER;
    srb->SrbFlags |= SRB_FLAGS_DISABLE_AUTOSENSE;
    srb->TimeOutValue = 10;
    srb->SrbStatus = SRB_STATUS_PENDING;
    irp->Tail.Overlay.DriverContext[UAS_CTX_PDO] = NULL;
    irp->Tail.Overlay.DriverContext[UAS_CTX_SRB] = srb;
    irp->Tail.Overlay.DriverContext[UAS_CTX_EVENT] = &done;

    queued = FALSE;
    KeAcquireSpinLock(&fdo->Lock, &irql);
    if (fdo->Started && !fdo->Gone && !fdo->Dead) {
        InsertTailList(&fdo->Queue, &irp->Tail.Overlay.ListEntry);
        queued = TRUE;
    }
    KeReleaseSpinLock(&fdo->Lock, irql);
    if (queued) {
        uasPump(fdo);
        (VOID)KeWaitForSingleObject(&done, Executive, KernelMode, FALSE,
                                    NULL);
        *scsiStatus = srb->ScsiStatus;
        *transferred = srb->DataTransferLength;
        status = (SRB_STATUS(srb->SrbStatus) == SRB_STATUS_SUCCESS ||
                  SRB_STATUS(srb->SrbStatus) == SRB_STATUS_DATA_OVERRUN)
                     ? STATUS_SUCCESS : STATUS_IO_DEVICE_ERROR;
    } else {
        status = STATUS_NO_SUCH_DEVICE;
    }
    IoFreeIrp(irp);
    UasFree(srb);
    return status;
}

/* <= DISPATCH_LEVEL. SRB_FUNCTION_ABORT_COMMAND: the victim, if it is in
 * flight, is treated as timed out, so recovery sends ABORT TASK for it. */
VOID UasEngineAbortSrb(PUAS_FDO fdo, PSCSI_REQUEST_BLOCK victim)
{
    PUAS_SLOT slot;
    BOOLEAN queue;
    ULONG t;
    KIRQL irql;

    queue = FALSE;
    KeAcquireSpinLock(&fdo->Lock, &irql);
    if (fdo->Slots != NULL) {
        for (t = 1; t <= fdo->Tags.Count; t++) {
            slot = &fdo->Slots[t];
            if (slot->State == UAS_SLOT_ACTIVE && !slot->Tmf &&
                slot->Srb == victim && !slot->Aborted) {
                slot->TimedOut = TRUE;
                queue = uasWantRecovery(fdo);
            }
        }
    }
    KeReleaseSpinLock(&fdo->Lock, irql);
    if (queue) {
        ExQueueWorkItem(&fdo->Recovery, DelayedWorkQueue);
    }
}

/* <= DISPATCH_LEVEL. SRB_FUNCTION_RESET_DEVICE: a LOGICAL UNIT RESET. */
VOID UasEngineResetLun(PUAS_FDO fdo, ULONG lun)
{
    BOOLEAN queue;
    KIRQL irql;

    KeAcquireSpinLock(&fdo->Lock, &irql);
    fdo->ResetRequested = TRUE;
    fdo->ResetLun = lun;
    queue = uasWantRecovery(fdo);
    KeReleaseSpinLock(&fdo->Lock, irql);
    if (queue) {
        ExQueueWorkItem(&fdo->Recovery, DelayedWorkQueue);
    }
}

/* <= DISPATCH_LEVEL. A LUN's queue was unlocked: start what waited. */
VOID UasEngineUnlockQueue(PUAS_FDO fdo)
{
    uasPump(fdo);
}
