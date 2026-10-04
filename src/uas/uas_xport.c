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
 * (the device refused the COMMAND IU).
 *
 * OWNERSHIP. A slot - its tag, its transfers and their IRPs, its data MDL
 * and the request and SRB it carries - is finished (uasFinish: the MDL
 * freed, the tag returned, the request completed) only when it is idle: no
 * transfer busy and no IoCancelIrp on one still running. Whatever ended it
 * - its final IU, or recovery or teardown setting Aborted and
 * FinalSrbStatus - the finish happens in whichever context first sees it
 * idle (uasCheckDone), which for a transfer the bus has not yet given back
 * is that transfer's own completion. Nothing waits out a cancellation in
 * order to free: a cancellation the bus does not honour leaves the slot
 * owned, its request pending, and the device Dead, until the bus completes
 * the transfer or tears the device down. A task management slot whose
 * waiter gave up is released the same way.
 *
 * TAGS AND THE DEVICE. Cancelling the host's transfers does not end the
 * device's task (T10 UAS task management), so a tag goes back to the
 * allocator only when the device has finished the command itself (its final
 * IU), confirmed its end (ABORT TASK or LOGICAL UNIT RESET answered), or
 * lost it to a port reset or a new configuration (an I_T nexus loss). A
 * slot ended any other way - a task management function that got no answer,
 * a LUN closed on a device that cannot be reached - is quarantined: its tag
 * stays allocated, so no later command can collide with the device's
 * stale task, and recovery treats a quarantine as a reason to reset the
 * port, which releases it.
 *
 * CANCELLATION. IoCancelIrp is called with the lock dropped, on a transfer
 * marked under the lock (Cancelling, CancelRefs). Until that call has
 * returned and CancelRefs is back to 0 the transfer is not built again and
 * its slot counts as busy, so a completion that lands first cannot let the
 * IRP be reused under the cancel (uasActRun).
 *
 * RUNDOWN. fdo->IoCount holds a reference for each transfer from its build
 * to the last line of its completion routine, for each queued recovery
 * worker to its last line, and for each timer DPC run that does work.
 * UasEngineStop drops the bias and waits for 0, with no timeout: the bus
 * completes every transfer by the time it tears the device down.
 *
 * RECOVERY (uasRecoveryWorker, PASSIVE_LEVEL on a system worker thread).
 * Triggered by a command timing out (the one-second timer), by a transfer
 * failing, by SRB_FUNCTION_ABORT_COMMAND or a reset SRB, which wait on the
 * recovery list for their outcome. In escalating order: ABORT TASK for a
 * timed-out or aborted command (completed SRB_STATUS_TIMEOUT or
 * SRB_STATUS_ABORTED); LOGICAL UNIT RESET if that fails or a reset was
 * asked for; and a device reset if either fails or a transfer failed -
 * every transfer cancelled, every request in flight ended with
 * SRB_STATUS_BUS_RESET (which the class driver retries), and, once every
 * transfer has come back, IOCTL_INTERNAL_USB_RESET_PORT, after which the
 * bus has the streams open again on the same handles. A device whose
 * transfers do not come back, or whose port reset fails, is marked Dead and
 * every later request fails with SRB_STATUS_NO_DEVICE.
 *
 * IRQL: as uas.h states per entry; completion routines and the timer DPC
 * at DISPATCH_LEVEL; the lock is held by no caller of any function here
 * except those marked "lock held".
 */

#include "uas.h"

typedef struct _UAS_ACT {
    PUAS_XFER Submit[4];
    ULONG SubmitCount;
    PUAS_XFER Cancel[UAS_XFERS + 1];
    ULONG CancelCount;
    PUAS_SLOT Finish[2];
    ULONG FinishCount;
    BOOLEAN Recover;
} UAS_ACT, *PUAS_ACT;

static VOID uasPump(PUAS_FDO fdo);
static VOID uasFinish(PUAS_FDO fdo, PUAS_SLOT slot);
static VOID uasRecoveryWorker(PVOID context);

/* ------------------------------------------------------------------ */
/* Rundown and small helpers                                          */
/* ------------------------------------------------------------------ */

static VOID uasRef(PUAS_FDO fdo)
{
    (VOID)InterlockedIncrement(&fdo->IoCount);
}

/* The last access to the FDO a referenced context makes. */
static VOID uasDeref(PUAS_FDO fdo)
{
    if (InterlockedDecrement(&fdo->IoCount) == 0) {
        KeSetEvent(&fdo->IoIdle, IO_NO_INCREMENT, FALSE);
    }
}

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

/* Lock held. A busy transfer this driver means to cancel: marked, and held
 * against reuse until the IoCancelIrp call has returned. */
static VOID uasActCancel(PUAS_ACT a, PUAS_XFER x)
{
    if (x->Busy && !x->Cancelling && a->CancelCount < UAS_XFERS + 1) {
        x->Cancelling = TRUE;
        x->CancelRefs++;
        a->Cancel[a->CancelCount++] = x;
    }
}

/* Lock held. Whether a recovery pass needs queueing; marks it queued and
 * takes the worker's reference. */
static BOOLEAN uasWantRecovery(PUAS_FDO fdo)
{
    if (!fdo->Started || fdo->RecoveryQueued) {
        return FALSE;
    }
    fdo->RecoveryQueued = TRUE;
    uasRef(fdo);
    return TRUE;
}

/* Lock held. No transfer of the slot's busy or under a cancel call (the
 * shared status read is not the slot's). */
static BOOLEAN uasSlotIdle(PUAS_SLOT slot)
{
    ULONG i;

    for (i = 0; i < UAS_XFERS; i++) {
        if (slot->Xfer[i].Busy || slot->Xfer[i].CancelRefs != 0) {
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

/*
 * Lock held. The one place a slot is let go, and only when it is idle. A
 * command whose final IU is in, or which recovery or teardown ended
 * (Aborted, FinalSrbStatus), is handed to uasFinish once; a TMF wakes its
 * waiter, or - when the waiter has given up (Aborted) - releases its tag.
 * A Failed command waits for recovery to end it.
 */
static VOID uasCheckDone(PUAS_FDO fdo, PUAS_SLOT slot, PUAS_ACT a)
{
    if (slot->State == UAS_SLOT_FREE || !uasSlotIdle(slot)) {
        return;
    }
    if (slot->Tmf) {
        if (slot->Aborted) {
            if (slot->Quarantine) {
                fdo->Quarantined |= 1UL << (slot->Tag - 1);
            } else {
                (VOID)UasTagFree(&fdo->Tags, slot->Tag);
            }
            slot->Tmf = FALSE;
            slot->State = UAS_SLOT_FREE;
        } else if ((slot->State == UAS_SLOT_FINAL || slot->Failed) &&
                   !slot->TmfSignalled && slot->TmfDone != NULL) {
            slot->TmfSignalled = TRUE;
            KeSetEvent(slot->TmfDone, IO_NO_INCREMENT, FALSE);
        }
        return;
    }
    if (slot->Finishing) {
        return;
    }
    if ((slot->Aborted && slot->FinalSrbStatus != 0) ||
        (slot->State == UAS_SLOT_FINAL && !slot->Failed && !slot->Aborted)) {
        if (a->FinishCount < 2) {
            slot->Finishing = TRUE;
            a->Finish[a->FinishCount++] = slot;
        }
    }
}

static NTSTATUS uasXferDone(PDEVICE_OBJECT device, PIRP irp, PVOID context);

/* Lock held. Builds one bulk transfer, marks it busy and takes its
 * reference. The caller has checked it is neither busy nor under a cancel. */
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
    uasRef(fdo);
}

/* Lock held. The status read for a slot: on its own stream when streamed,
 * the shared read otherwise (posted only if not already). */
static VOID uasPostStatus(PUAS_FDO fdo, PUAS_SLOT slot, PUAS_ACT a)
{
    PUAS_XFER x;

    if (fdo->Streamed) {
        x = &slot->Xfer[UAS_XFER_STATUS];
        if (x->Busy || x->CancelRefs != 0) {
            return;
        }
        uasXferBuild(fdo, x,
                     UasStreamsPipe(&fdo->Streams, UAS_STREAM_STATUS,
                                    slot->Tag),
                     slot->Stat, NULL, UAS_STATUS_BUFFER, TRUE);
    } else {
        x = &fdo->SharedStatus;
        if (x->Busy || x->CancelRefs != 0) {
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
    if (x->Busy || x->CancelRefs != 0 || slot->DataPosted ||
        !slot->NeedsData) {
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

/*
 * No lock held: runs what a locked section decided. Each IoCancelIrp is
 * followed, under the lock, by dropping that transfer's CancelRefs and
 * looking again at what the cancel was holding back: the slot, which may
 * now be idle, or the shared read, which may be wanted again.
 */
static VOID uasActRun(PUAS_FDO fdo, PUAS_ACT a)
{
    UAS_ACT after;
    PUAS_XFER x;
    ULONG i;
    BOOLEAN queue;
    KIRQL irql;

    for (i = 0; i < a->SubmitCount; i++) {
        (VOID)IoCallDriver(fdo->Lower, a->Submit[i]->Irp);
    }
    for (i = 0; i < a->CancelCount; i++) {
        x = a->Cancel[i];
        (VOID)IoCancelIrp(x->Irp);
        uasActInit(&after);
        KeAcquireSpinLock(&fdo->Lock, &irql);
        x->CancelRefs--;
        if (x->Slot != NULL) {
            uasCheckDone(fdo, x->Slot, &after);
        } else if (!fdo->Gone && uasNeedShared(fdo)) {
            uasPostStatus(fdo, NULL, &after);
        }
        KeReleaseSpinLock(&fdo->Lock, irql);
        uasActRun(fdo, &after);
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

/*
 * The request's IRP status follows the SRB status, except a short transfer
 * (SRB_STATUS_DATA_OVERRUN with the bytes moved), which stays
 * STATUS_SUCCESS: the class driver reads the SRB itself, and how Windows
 * 98's NUSB mapping layer treats STATUS_DATA_OVERRUN on the IRP is unread,
 * so a short INQUIRY must not become a failure there.
 * The LUN's request count is dropped after the completion, under the lock.
 */
static VOID uasCompleteRequest(PUAS_FDO fdo, PIRP irp,
                               PSCSI_REQUEST_BLOCK srb)
{
    PKEVENT event;
    PUAS_PDO pdo;
    NTSTATUS status;
    ULONG s;
    KIRQL irql;

    event = (PKEVENT)irp->Tail.Overlay.DriverContext[UAS_CTX_EVENT];
    if (event != NULL) {
        KeSetEvent(event, IO_NO_INCREMENT, FALSE);
        return;
    }
    pdo = (PUAS_PDO)irp->Tail.Overlay.DriverContext[UAS_CTX_PDO];
    s = SRB_STATUS(srb->SrbStatus);
    switch (s) {
    case SRB_STATUS_SUCCESS:
        status = STATUS_SUCCESS;
        break;
    case SRB_STATUS_DATA_OVERRUN:
        status = STATUS_SUCCESS;
        break;
    case SRB_STATUS_BUSY:
        status = STATUS_DEVICE_BUSY;
        break;
    case SRB_STATUS_NO_DEVICE:
        status = STATUS_NO_SUCH_DEVICE;
        break;
    case SRB_STATUS_ABORTED:
    case SRB_STATUS_REQUEST_FLUSHED:
        status = STATUS_CANCELLED;
        break;
    case SRB_STATUS_TIMEOUT:
        status = STATUS_IO_TIMEOUT;
        break;
    case SRB_STATUS_INVALID_REQUEST:
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    default:
        status = STATUS_IO_DEVICE_ERROR;
        break;
    }
    irp->IoStatus.Status = status;
    irp->IoStatus.Information = srb->DataTransferLength;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    if (pdo != NULL) {
        KeAcquireSpinLock(&fdo->Lock, &irql);
        if (pdo->Requests != 0) {
            pdo->Requests--;
        }
        if (pdo->Requests == 0) {
            KeSetEvent(&pdo->RequestsIdle, IO_NO_INCREMENT, FALSE);
        }
        KeReleaseSpinLock(&fdo->Lock, irql);
    }
}

/* A request that never reached a slot, or a recovery SRB. */
static VOID uasFailRequest(PUAS_FDO fdo, PIRP irp, UCHAR srbStatus)
{
    PSCSI_REQUEST_BLOCK srb;

    srb = (PSCSI_REQUEST_BLOCK)irp->Tail.Overlay.DriverContext[UAS_CTX_SRB];
    srb->SrbStatus = srbStatus;
    srb->ScsiStatus = 0;
    if (srb->Function == SRB_FUNCTION_EXECUTE_SCSI) {
        srb->DataTransferLength = 0;
    }
    uasCompleteRequest(fdo, irp, srb);
}

/* Lock held. Admits one request of a LUN: counted, its context set. */
static BOOLEAN uasAdmit(PUAS_FDO fdo, PUAS_PDO pdo, PIRP irp,
                        PSCSI_REQUEST_BLOCK srb)
{
    if (!fdo->Started || fdo->Gone || fdo->Dead ||
        (pdo != NULL && !pdo->Admit)) {
        return FALSE;
    }
    irp->Tail.Overlay.DriverContext[UAS_CTX_PDO] = pdo;
    irp->Tail.Overlay.DriverContext[UAS_CTX_SRB] = srb;
    irp->Tail.Overlay.DriverContext[UAS_CTX_EVENT] = NULL;
    if (pdo != NULL) {
        if (pdo->Requests == 0) {
            KeClearEvent(&pdo->RequestsIdle);
        }
        pdo->Requests++;
    }
    return TRUE;
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
 * No lock held. A command slot uasCheckDone handed over: idle, ended by its
 * final IU or by recovery or teardown. The SRB gets its status, the MDL is
 * freed, the tag goes back, the request completes.
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
    if (slot->Quarantine) {
        fdo->Quarantined |= 1UL << (slot->Tag - 1);
    } else {
        (VOID)UasTagFree(&fdo->Tags, slot->Tag);
    }
    if (fdo->Active != 0) {
        fdo->Active--;
    }
    slot->State = UAS_SLOT_FREE;
    slot->Request = NULL;
    slot->Srb = NULL;
    KeReleaseSpinLock(&fdo->Lock, irql);

    uasCompleteRequest(fdo, request, srb);
}

/* ------------------------------------------------------------------ */
/* Starting commands                                                  */
/* ------------------------------------------------------------------ */

/* Lock held. A slot's flags for a new command or TMF. */
static VOID uasSlotReset(PUAS_SLOT slot)
{
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
    slot->Quarantine = FALSE;
    slot->TmfDone = NULL;
    slot->Request = NULL;
    slot->Srb = NULL;
    slot->DataMdl = NULL;
    slot->DataLength = 0;
    slot->Transferred = 0;
    slot->ScsiStatus = 0;
    slot->SenseLength = 0;
    slot->ResponseCode = 0;
    slot->FinalSrbStatus = 0;
}

/*
 * Lock held. Fills a free slot from a request. Returns 0 when it is ready to
 * go, or the SRB status to fail the request with.
 */
static UCHAR uasSlotPrepare(PUAS_SLOT slot, PIRP irp,
                            PSCSI_REQUEST_BLOCK srb, ULONG lun)
{
    ULONG dir;
    ULONG attribute;

    uasSlotReset(slot);
    slot->Request = irp;
    slot->Srb = srb;
    slot->Lun = lun;
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
 * <= DISPATCH_LEVEL, no lock held, from a context that keeps the FDO (a
 * referenced engine context or a PDO dispatch inside its Busy count).
 * Starts queued requests while the queue depth and the tags allow. A
 * request whose LUN has its queue locked waits unless it bypasses the lock
 * (the class driver's power SRBs).
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
            uasFailRequest(fdo,
                           CONTAINING_RECORD(e, IRP, Tail.Overlay.ListEntry),
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
        fail = uasSlotPrepare(slot, irp, srb, lun);
        if (fail != 0) {
            (VOID)UasTagFree(&fdo->Tags, tag);
            uasSlotReset(slot);
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
            uasFailRequest(fdo, failIrp, fail);
            continue;
        }
        uasActRun(fdo, &a);
    }
}

/* ------------------------------------------------------------------ */
/* Completions                                                        */
/* ------------------------------------------------------------------ */

/* Lock held. Marks a slot failed and asks for recovery. */
static VOID uasSlotFault(PUAS_SLOT slot, PUAS_ACT a)
{
    if (slot->State == UAS_SLOT_FREE || slot->Aborted) {
        return;
    }
    slot->Failed = TRUE;
    if (!slot->Tmf) {
        a->Recover = TRUE;
    }
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
            uasSlotFault(x->Slot, a);
        }
        return;
    }

    switch (iu.Id) {
    case UAS_IU_SENSE:
        if (target->Tmf) {
            fdo->Counters[UAS_CTR_PROTOCOL]++;
            uasSlotFault(target, a);
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
            uasSlotFault(target, a);
            return;
        }
        /* Streamed, the data transfer went out with the command and the
         * READY is only the device catching up; streamless, it releases
         * the data phase. */
        uasPostData(fdo, target, a);
        break;

    default:
        fdo->Counters[UAS_CTR_PROTOCOL]++;
        uasSlotFault(target, a);
        break;
    }
}

/*
 * The completion of every transfer. It holds the reference uasXferBuild
 * took until its last line, so nothing it starts - the finish of a slot,
 * a re-post, a recovery request, a pump - can outlive the FDO.
 */
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
                uasSlotFault(slot, &a);
            } else {
                /* The shared status read: every slot waiting on it. */
                for (t = 1; t <= fdo->Tags.Count; t++) {
                    if (fdo->Slots[t].State != UAS_SLOT_FREE) {
                        uasSlotFault(&fdo->Slots[t], &a);
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
        uasCheckDone(fdo, slot, &a);
    } else {
        for (t = 1; t <= fdo->Tags.Count; t++) {
            uasCheckDone(fdo, &fdo->Slots[t], &a);
        }
    }
    KeReleaseSpinLock(&fdo->Lock, irql);

    uasActRun(fdo, &a);
    uasDeref(fdo);
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
        KeReleaseSpinLock(&fdo->Lock, irql);
        KeSetEvent(&fdo->TimerDone, IO_NO_INCREMENT, FALSE);
        return;
    }
    uasRef(fdo);
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
    uasDeref(fdo);
}

/* ------------------------------------------------------------------ */
/* Ending slots (any IRQL <= DISPATCH unless noted)                   */
/* ------------------------------------------------------------------ */

/* Lock held. Ends one command slot with srbStatus: marked aborted, its busy
 * transfers (and the shared read, when nothing else needs it) put up for
 * cancelling, and finished here if it is already idle - otherwise by the
 * completion that makes it idle. */
static VOID uasEndSlotLocked(PUAS_FDO fdo, PUAS_SLOT slot, UCHAR srbStatus,
                             BOOLEAN quarantine, PUAS_ACT a)
{
    ULONG i;

    if (slot->State == UAS_SLOT_FREE || slot->Tmf || slot->Finishing ||
        slot->Aborted) {
        return;
    }
    slot->Aborted = TRUE;
    slot->Quarantine = quarantine;
    slot->FinalSrbStatus = srbStatus;
    for (i = 0; i < UAS_XFERS; i++) {
        uasActCancel(a, &slot->Xfer[i]);
    }
    if (!fdo->Streamed && !uasNeedShared(fdo)) {
        uasActCancel(a, &fdo->SharedStatus);
    }
    uasCheckDone(fdo, slot, a);
}

/* Every command of one LUN (all LUNs when lun is ~0), the LUN test and
 * the end in one hold of the lock per slot. */
static VOID uasEndLun(PUAS_FDO fdo, ULONG lun, UCHAR srbStatus)
{
    UAS_ACT a;
    ULONG t;
    KIRQL irql;

    for (t = 1; t <= UAS_MAX_TAGS; t++) {
        uasActInit(&a);
        KeAcquireSpinLock(&fdo->Lock, &irql);
        if (lun == (ULONG)~0UL || fdo->Slots[t].Lun == lun) {
            uasEndSlotLocked(fdo, &fdo->Slots[t], srbStatus, FALSE, &a);
        }
        KeReleaseSpinLock(&fdo->Lock, irql);
        uasActRun(fdo, &a);
    }
}

/* Every command ended with srbStatus, and every task management function's
 * transfers cancelled so its waiter returns. */
static VOID uasAbortAll(PUAS_FDO fdo, UCHAR srbStatus)
{
    UAS_ACT a;
    ULONG t;
    ULONG i;
    KIRQL irql;

    uasEndLun(fdo, (ULONG)~0UL, srbStatus);
    for (t = 0; t <= UAS_MAX_TAGS; t++) {
        uasActInit(&a);
        KeAcquireSpinLock(&fdo->Lock, &irql);
        if (t == 0) {
            uasActCancel(&a, &fdo->SharedStatus);
        } else if (fdo->Slots[t].State != UAS_SLOT_FREE &&
                   fdo->Slots[t].Tmf) {
            fdo->Slots[t].Failed = TRUE;
            for (i = 0; i < UAS_XFERS; i++) {
                uasActCancel(&a, &fdo->Slots[t].Xfer[i]);
            }
            uasCheckDone(fdo, &fdo->Slots[t], &a);
        }
        KeReleaseSpinLock(&fdo->Lock, irql);
        uasActRun(fdo, &a);
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

/* Whether every slot is free and the shared read idle, within five
 * seconds. FALSE leaves everything as it is: owned, to be finished by the
 * completions still due. */
static BOOLEAN uasWaitAllFree(PUAS_FDO fdo)
{
    ULONG round;
    ULONG t;
    BOOLEAN busy;
    KIRQL irql;

    for (round = 0; round < 500; round++) {
        KeAcquireSpinLock(&fdo->Lock, &irql);
        busy = (BOOLEAN)(fdo->SharedStatus.Busy ||
                         fdo->SharedStatus.CancelRefs != 0);
        for (t = 1; t <= UAS_MAX_TAGS && !busy; t++) {
            busy = (BOOLEAN)(fdo->Slots[t].State != UAS_SLOT_FREE);
        }
        KeReleaseSpinLock(&fdo->Lock, irql);
        if (!busy) {
            return TRUE;
        }
        uasSleepMs(10);
    }
    return FALSE;
}

/*
 * One task management function, waited for. Returns the response code, or
 * UAS_RC_TMF_FAILED when it could not be sent or no answer came. The slot
 * is not waited for: when the waiter gives up it is marked aborted, its
 * transfers cancelled, and it releases its own tag once idle (uasCheckDone).
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
    ULONG i;
    KIRQL irql;

    KeInitializeEvent(&done, NotificationEvent, FALSE);
    uasActInit(&a);
    KeAcquireSpinLock(&fdo->Lock, &irql);
    tag = UasTagAlloc(&fdo->Tags);
    if (tag == 0 || fdo->Gone) {
        if (tag != 0) {
            (VOID)UasTagFree(&fdo->Tags, tag);
        }
        KeReleaseSpinLock(&fdo->Lock, irql);
        return UAS_RC_TMF_FAILED;
    }
    slot = &fdo->Slots[tag];
    uasSlotReset(slot);
    UasZero(slot->Iu, sizeof(slot->Iu));
    length = UasIuBuildTaskMgmt(slot->Iu, sizeof(slot->Iu), tag, function,
                                taskTag, lun);
    slot->Tmf = TRUE;
    slot->TmfDone = &done;
    slot->ResponseCode = UAS_RC_TMF_FAILED;
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

    uasActInit(&a);
    KeAcquireSpinLock(&fdo->Lock, &irql);
    code = (wait == STATUS_SUCCESS && slot->State == UAS_SLOT_FINAL &&
            !slot->Failed) ? slot->ResponseCode : UAS_RC_TMF_FAILED;
    /* The event is on this stack: nothing may signal it after this. An
     * unanswered TMF may still be live in the device under its tag. */
    slot->TmfDone = NULL;
    slot->Quarantine = (BOOLEAN)(code == UAS_RC_TMF_FAILED);
    slot->Aborted = TRUE;
    slot->State = UAS_SLOT_FINAL;
    for (i = 0; i < UAS_XFERS; i++) {
        uasActCancel(&a, &slot->Xfer[i]);
    }
    if (!fdo->Streamed && !uasNeedShared(fdo)) {
        uasActCancel(&a, &fdo->SharedStatus);
    }
    uasCheckDone(fdo, slot, &a);
    KeReleaseSpinLock(&fdo->Lock, irql);
    uasActRun(fdo, &a);
    return code;
}

static BOOLEAN uasTmfOk(ULONG code)
{
    return (BOOLEAN)(code == UAS_RC_TMF_COMPLETE ||
                     code == UAS_RC_TMF_SUCCEEDED);
}

/* Lock held. Every quarantined tag back to the allocator. */
static VOID uasReleaseQuarantine(PUAS_FDO fdo)
{
    ULONG t;

    for (t = 1; t <= fdo->Tags.Count; t++) {
        if ((fdo->Quarantined & (1UL << (t - 1))) != 0) {
            (VOID)UasTagFree(&fdo->Tags, t);
        }
    }
    fdo->Quarantined = 0;
}

/*
 * The last resort: everything in flight ended with BUS_RESET (NO_DEVICE if
 * the device has gone), then - only once every transfer has come back - the
 * port reset. The bus restores the device with its streams open and every
 * stream handle valid (src\xhci98_streams.h), so nothing is reopened.
 * FALSE, with the FDO Dead, when transfers did not come back or the reset
 * failed.
 */
static BOOLEAN uasResetDevice(PUAS_FDO fdo)
{
    NTSTATUS status;
    KIRQL irql;

    uasAbortAll(fdo, (UCHAR)(fdo->Gone ? SRB_STATUS_NO_DEVICE
                                       : SRB_STATUS_BUS_RESET));
    if (fdo->Gone) {
        return FALSE;
    }
    status = STATUS_IO_TIMEOUT;
    if (uasWaitAllFree(fdo)) {
        fdo->Counters[UAS_CTR_PORT_RESETS]++;
        status = UasSyncIoctl(fdo, IOCTL_INTERNAL_USB_RESET_PORT);
    }
    KeAcquireSpinLock(&fdo->Lock, &irql);
    if (!NT_SUCCESS(status)) {
        fdo->Dead = TRUE;
        KeReleaseSpinLock(&fdo->Lock, irql);
        return FALSE;
    }
    /* The port reset lost every task the device held: quarantined tags
     * are safe again. */
    uasReleaseQuarantine(fdo);
    KeReleaseSpinLock(&fdo->Lock, irql);
    return TRUE;
}

/* Recovery after a LOGICAL UNIT RESET was wanted: the TMF, then the port
 * reset if it fails. TRUE when the LUN's commands are gone and it is
 * usable. */
static BOOLEAN uasResetLun(PUAS_FDO fdo, ULONG lun)
{
    fdo->Counters[UAS_CTR_LUN_RESETS]++;
    if (uasTmfOk(uasTmf(fdo, UAS_TMF_LOGICAL_UNIT_RESET, lun, 0))) {
        uasEndLun(fdo, lun, SRB_STATUS_BUS_RESET);
        return TRUE;
    }
    return uasResetDevice(fdo);
}

/* ABORT TASK for one command, escalating. TRUE when the command is gone
 * (aborted with abortStatus, or swept up by a reset). */
static BOOLEAN uasAbortTask(PUAS_FDO fdo, PUAS_SLOT slot, UCHAR abortStatus)
{
    UAS_ACT a;
    PIRP request;
    ULONG lun;
    ULONG tag;
    KIRQL irql;

    KeAcquireSpinLock(&fdo->Lock, &irql);
    request = slot->Request;
    lun = slot->Lun;
    tag = slot->Tag;
    KeReleaseSpinLock(&fdo->Lock, irql);
    fdo->Counters[UAS_CTR_ABORTS]++;
    if (uasTmfOk(uasTmf(fdo, UAS_TMF_ABORT_TASK, lun, tag))) {
        /* Ended only if it is still the same command (one hold). */
        uasActInit(&a);
        KeAcquireSpinLock(&fdo->Lock, &irql);
        if (slot->Request == request) {
            uasEndSlotLocked(fdo, slot, abortStatus, FALSE, &a);
        }
        KeReleaseSpinLock(&fdo->Lock, irql);
        uasActRun(fdo, &a);
        return TRUE;
    }
    return uasResetLun(fdo, lun);
}

/* Lock held. Whether a slot is a live command of the LUN's PDO. */
static BOOLEAN uasSlotOfPdo(PUAS_SLOT slot, PUAS_PDO pdo)
{
    return (BOOLEAN)(slot->State != UAS_SLOT_FREE && !slot->Tmf &&
                     !slot->Aborted && !slot->Finishing &&
                     slot->Request != NULL &&
                     slot->Request->Tail.Overlay.DriverContext[UAS_CTX_PDO]
                         == pdo);
}

/*
 * Ends every live command of a closing LUN with its close status, each test
 * and transition in one hold of the lock, so a slot recycled in between
 * cannot be taken for the LUN's. With quarantine the tags are held (no
 * device-side confirmation was had); without, the caller has had one.
 */
static VOID uasEndPdoSlots(PUAS_FDO fdo, PUAS_PDO pdo, UCHAR srbStatus,
                           BOOLEAN quarantine)
{
    UAS_ACT a;
    ULONG t;
    KIRQL irql;

    if (fdo->Slots == NULL) {
        return;
    }
    for (t = 1; t <= UAS_MAX_TAGS; t++) {
        uasActInit(&a);
        KeAcquireSpinLock(&fdo->Lock, &irql);
        if (uasSlotOfPdo(&fdo->Slots[t], pdo)) {
            uasEndSlotLocked(fdo, &fdo->Slots[t], srbStatus, quarantine, &a);
        }
        KeReleaseSpinLock(&fdo->Lock, irql);
        uasActRun(fdo, &a);
    }
}

/* Signals a closing LUN's waiter. */
static VOID uasCloseDone(PUAS_FDO fdo, PUAS_PDO pdo)
{
    KIRQL irql;

    KeAcquireSpinLock(&fdo->Lock, &irql);
    pdo->CloseRequested = FALSE;
    KeSetEvent(&pdo->CloseDone, IO_NO_INCREMENT, FALSE);
    KeReleaseSpinLock(&fdo->Lock, irql);
}

/*
 * The worker's half of UasEngineCloseLun. ABORT TASK for each of the LUN's
 * commands in flight, each ended with the close status once the device has
 * answered; the first that is not answered escalates to LOGICAL UNIT RESET
 * for the LUN, then to the port reset (uasResetLun), either of which ends
 * the rest. No new command starts meanwhile (Recovering), so the slots
 * looked at stay the LUN's: each is still re-tested by its request.
 */
static VOID uasCloseLun(PUAS_FDO fdo, PUAS_PDO pdo)
{
    UAS_ACT a;
    PUAS_SLOT slot;
    PIRP request;
    ULONG t;
    ULONG tag;
    BOOLEAN escalate;
    KIRQL irql;

    escalate = FALSE;
    for (t = 1; t <= UAS_MAX_TAGS && !escalate; t++) {
        KeAcquireSpinLock(&fdo->Lock, &irql);
        slot = &fdo->Slots[t];
        request = uasSlotOfPdo(slot, pdo) ? slot->Request : NULL;
        tag = slot->Tag;
        KeReleaseSpinLock(&fdo->Lock, irql);
        if (request == NULL) {
            continue;
        }
        fdo->Counters[UAS_CTR_ABORTS]++;
        if (!uasTmfOk(uasTmf(fdo, UAS_TMF_ABORT_TASK, pdo->Lun, tag))) {
            escalate = TRUE;
            break;
        }
        uasActInit(&a);
        KeAcquireSpinLock(&fdo->Lock, &irql);
        if (uasSlotOfPdo(slot, pdo) && slot->Request == request) {
            uasEndSlotLocked(fdo, slot, pdo->CloseStatus, FALSE, &a);
        }
        KeReleaseSpinLock(&fdo->Lock, irql);
        uasActRun(fdo, &a);
    }
    if (escalate) {
        if (uasResetLun(fdo, pdo->Lun)) {
            /* uasResetLun ended the LUN's commands BUS_RESET; any left
             * (none, unless the reset ended them all already) close too. */
            uasEndPdoSlots(fdo, pdo, pdo->CloseStatus, FALSE);
        } else {
            uasEndPdoSlots(fdo, pdo, pdo->CloseStatus, TRUE);
        }
    }
    uasCloseDone(fdo, pdo);
}

/* Lock held. A LUN waiting for the worker to close it. */
static PUAS_PDO uasCloseNext(PUAS_FDO fdo)
{
    ULONG i;

    for (i = 0; i < fdo->LunCount; i++) {
        if (fdo->Luns[i]->CloseRequested) {
            return fdo->Luns[i];
        }
    }
    return NULL;
}

/* Lock held. The in-flight command carrying an SRB, if any. */
static PUAS_SLOT uasFindSrb(PUAS_FDO fdo, PSCSI_REQUEST_BLOCK victim)
{
    ULONG t;
    PUAS_SLOT slot;

    for (t = 1; t <= UAS_MAX_TAGS; t++) {
        slot = &fdo->Slots[t];
        if (slot->State != UAS_SLOT_FREE && !slot->Tmf && !slot->Aborted &&
            !slot->Finishing && slot->Srb == victim) {
            return slot;
        }
    }
    return NULL;
}

/* One held ABORT_COMMAND or reset SRB, completed with its outcome. */
static VOID uasRecoveryRequest(PUAS_FDO fdo, PIRP irp)
{
    PSCSI_REQUEST_BLOCK srb;
    PUAS_PDO pdo;
    PUAS_SLOT slot;
    UCHAR st;
    KIRQL irql;

    srb = (PSCSI_REQUEST_BLOCK)irp->Tail.Overlay.DriverContext[UAS_CTX_SRB];
    pdo = (PUAS_PDO)irp->Tail.Overlay.DriverContext[UAS_CTX_PDO];
    if (srb->Function == SRB_FUNCTION_ABORT_COMMAND) {
        KeAcquireSpinLock(&fdo->Lock, &irql);
        slot = uasFindSrb(fdo, srb->NextSrb);
        KeReleaseSpinLock(&fdo->Lock, irql);
        if (slot == NULL) {
            st = SRB_STATUS_ABORT_FAILED;   /* it had already completed */
        } else {
            st = uasAbortTask(fdo, slot, SRB_STATUS_ABORTED)
                     ? SRB_STATUS_SUCCESS : SRB_STATUS_ABORT_FAILED;
        }
    } else if (srb->Function == SRB_FUNCTION_RESET_BUS) {
        st = uasResetDevice(fdo) ? SRB_STATUS_SUCCESS : SRB_STATUS_ERROR;
    } else {
        st = uasResetLun(fdo, pdo->Lun) ? SRB_STATUS_SUCCESS
                                        : SRB_STATUS_ERROR;
    }
    uasFailRequest(fdo, irp, st);
}

/* Lock held. What recovery should look at next. */
#define UAS_RECOVER_NONE    0
#define UAS_RECOVER_TIMEOUT 1
#define UAS_RECOVER_REQUEST 2
#define UAS_RECOVER_RESET   3
#define UAS_RECOVER_CLOSE   4

static ULONG uasRecoveryNext(PUAS_FDO fdo, PUAS_SLOT *slotOut,
                             PIRP *irpOut, PUAS_PDO *pdoOut)
{
    PUAS_SLOT slot;
    PLIST_ENTRY e;
    ULONG t;

    for (t = 1; t <= UAS_MAX_TAGS; t++) {
        slot = &fdo->Slots[t];
        if (slot->State != UAS_SLOT_FREE && !slot->Tmf && slot->Failed &&
            !slot->Aborted) {
            return UAS_RECOVER_RESET;
        }
    }
    *pdoOut = uasCloseNext(fdo);
    if (*pdoOut != NULL) {
        return UAS_RECOVER_CLOSE;
    }
    if (fdo->Quarantined != 0) {
        return UAS_RECOVER_RESET;
    }
    if (!IsListEmpty(&fdo->RecoveryRequests)) {
        e = RemoveHeadList(&fdo->RecoveryRequests);
        *irpOut = CONTAINING_RECORD(e, IRP, Tail.Overlay.ListEntry);
        return UAS_RECOVER_REQUEST;
    }
    for (t = 1; t <= UAS_MAX_TAGS; t++) {
        slot = &fdo->Slots[t];
        if (slot->State != UAS_SLOT_FREE && !slot->Tmf && slot->TimedOut &&
            !slot->Aborted && !slot->Finishing) {
            *slotOut = slot;
            return UAS_RECOVER_TIMEOUT;
        }
    }
    return UAS_RECOVER_NONE;
}

/*
 * The worker. Holds the reference uasWantRecovery took until its last
 * line. A stopped engine, a gone device or a Dead one ends the pass: the
 * held recovery SRBs are then failed here (UasEngineStop fails them too).
 */
static VOID uasRecoveryWorker(PVOID context)
{
    PUAS_FDO fdo;
    PUAS_SLOT slot;
    PUAS_PDO pdo;
    PIRP irp;
    ULONG what;
    ULONG rounds;
    LIST_ENTRY drain;
    PLIST_ENTRY e;
    KIRQL irql;

    fdo = (PUAS_FDO)context;
    InitializeListHead(&drain);
    for (rounds = 0;; rounds++) {
        slot = NULL;
        irp = NULL;
        pdo = NULL;
        KeAcquireSpinLock(&fdo->Lock, &irql);
        fdo->Recovering = TRUE;
        if (!fdo->Started || fdo->Gone || fdo->Dead) {
            while (!IsListEmpty(&fdo->RecoveryRequests)) {
                e = RemoveHeadList(&fdo->RecoveryRequests);
                InsertTailList(&drain, e);
            }
            /* A close can no longer reach the device: its commands end
             * with their tags held (uasEndPdoSlots, below the loop). */
            pdo = uasCloseNext(fdo);
            what = (pdo != NULL) ? UAS_RECOVER_CLOSE : UAS_RECOVER_NONE;
        } else {
            what = uasRecoveryNext(fdo, &slot, &irp, &pdo);
        }
        if (what == UAS_RECOVER_NONE) {
            /* Decided under the same hold that ends the pass, so a fault
             * flagged after this sees RecoveryQueued FALSE and queues
             * another worker with its own reference. */
            fdo->Recovering = FALSE;
            fdo->RecoveryQueued = FALSE;
            KeReleaseSpinLock(&fdo->Lock, irql);
            break;
        }
        KeReleaseSpinLock(&fdo->Lock, irql);

        if (rounds >= 16 && what == UAS_RECOVER_TIMEOUT) {
            what = UAS_RECOVER_RESET;
        }
        switch (what) {
        case UAS_RECOVER_CLOSE:
            if (!fdo->Started || fdo->Gone || fdo->Dead) {
                uasEndPdoSlots(fdo, pdo, pdo->CloseStatus, TRUE);
                uasCloseDone(fdo, pdo);
            } else {
                uasCloseLun(fdo, pdo);
            }
            break;
        case UAS_RECOVER_TIMEOUT:
            (VOID)uasAbortTask(fdo, slot, SRB_STATUS_TIMEOUT);
            break;
        case UAS_RECOVER_REQUEST:
            uasRecoveryRequest(fdo, irp);
            break;
        default:
            (VOID)uasResetDevice(fdo);
            break;
        }
    }
    while (!IsListEmpty(&drain)) {
        e = RemoveHeadList(&drain);
        uasFailRequest(fdo, CONTAINING_RECORD(e, IRP, Tail.Overlay.ListEntry),
                       SRB_STATUS_NO_DEVICE);
    }
    uasPump(fdo);
    uasDeref(fdo);
}

/* ------------------------------------------------------------------ */
/* Entries                                                            */
/* ------------------------------------------------------------------ */

/* PASSIVE_LEVEL. The slots, their IRPs, the shared read, the timer, the
 * rundown. */
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

/* PASSIVE_LEVEL, after UasEngineStop has run the engine down. */
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
    /* uasFdoStart has just selected the configuration again, which the
     * device takes as an I_T nexus loss: no task survives it. */
    fdo->Quarantined = 0;
    fdo->Active = 0;
    fdo->Dead = FALSE;
    fdo->Recovering = FALSE;
    fdo->RecoveryQueued = FALSE;
    fdo->TimerStop = FALSE;
    KeClearEvent(&fdo->TimerDone);
    fdo->Started = TRUE;
    uasArmTimer(fdo);
    KeReleaseSpinLock(&fdo->Lock, irql);
}

/*
 * PASSIVE_LEVEL. Closes admission, stops the timer, ends every command in
 * flight and fails every queued and held request with srbStatus, then runs
 * the engine down: waits, with no timeout, until every transfer's
 * completion, every recovery worker and every timer run has finished with
 * the FDO. The engine can be started again afterwards.
 */
VOID UasEngineStop(PUAS_FDO fdo, UCHAR srbStatus)
{
    LIST_ENTRY drain;
    PLIST_ENTRY e;
    BOOLEAN wasStarted;
    KIRQL irql;

    InitializeListHead(&drain);
    KeAcquireSpinLock(&fdo->Lock, &irql);
    wasStarted = fdo->Started;
    fdo->Started = FALSE;
    fdo->TimerStop = TRUE;
    KeReleaseSpinLock(&fdo->Lock, irql);
    if (wasStarted && !KeCancelTimer(&fdo->Timer)) {
        (VOID)KeWaitForSingleObject(&fdo->TimerDone, Executive, KernelMode,
                                    FALSE, NULL);
    }
    if (fdo->Slots != NULL) {
        uasAbortAll(fdo, srbStatus);
    }
    KeAcquireSpinLock(&fdo->Lock, &irql);
    while (!IsListEmpty(&fdo->Queue)) {
        e = RemoveHeadList(&fdo->Queue);
        InsertTailList(&drain, e);
    }
    while (!IsListEmpty(&fdo->RecoveryRequests)) {
        e = RemoveHeadList(&fdo->RecoveryRequests);
        InsertTailList(&drain, e);
    }
    KeReleaseSpinLock(&fdo->Lock, irql);
    while (!IsListEmpty(&drain)) {
        e = RemoveHeadList(&drain);
        uasFailRequest(fdo, CONTAINING_RECORD(e, IRP, Tail.Overlay.ListEntry),
                       srbStatus);
    }

    /* The rundown: drop the bias, wait for 0, take the bias back. */
    KeClearEvent(&fdo->IoIdle);
    if (InterlockedDecrement(&fdo->IoCount) != 0) {
        (VOID)KeWaitForSingleObject(&fdo->IoIdle, Executive, KernelMode,
                                    FALSE, NULL);
    }
    (VOID)InterlockedIncrement(&fdo->IoCount);
}

/* <= DISPATCH_LEVEL. Queues one SRB's IRP; it completes later. */
NTSTATUS UasEngineSubmit(PUAS_FDO fdo, PUAS_PDO pdo, PIRP irp,
                         PSCSI_REQUEST_BLOCK srb)
{
    BOOLEAN admitted;
    KIRQL irql;

    srb->SrbStatus = SRB_STATUS_PENDING;
    KeAcquireSpinLock(&fdo->Lock, &irql);
    admitted = uasAdmit(fdo, pdo, irp, srb);
    if (admitted) {
        IoMarkIrpPending(irp);
        InsertTailList(&fdo->Queue, &irp->Tail.Overlay.ListEntry);
    }
    KeReleaseSpinLock(&fdo->Lock, irql);
    if (!admitted) {
        srb->SrbStatus = SRB_STATUS_NO_DEVICE;
        return UasCompleteIrp(irp, STATUS_NO_SUCH_DEVICE, 0);
    }
    uasPump(fdo);
    return STATUS_PENDING;
}

/*
 * <= DISPATCH_LEVEL. SRB_FUNCTION_ABORT_COMMAND. A victim still queued is
 * taken off the queue and completed SRB_STATUS_ABORTED, and the abort SRB
 * succeeds at once. A victim in flight is the recovery worker's: the abort
 * SRB is held until ABORT TASK (or the reset it escalates to) has ended it.
 * A victim that is neither has already completed: SRB_STATUS_ABORT_FAILED.
 */
NTSTATUS UasEngineAbort(PUAS_FDO fdo, PUAS_PDO pdo, PIRP irp,
                        PSCSI_REQUEST_BLOCK srb)
{
    PLIST_ENTRY e;
    PIRP victimIrp;
    PSCSI_REQUEST_BLOCK victim;
    BOOLEAN admitted;
    BOOLEAN queue;
    UCHAR st;
    KIRQL irql;

    victim = srb->NextSrb;
    victimIrp = NULL;
    queue = FALSE;
    st = SRB_STATUS_ABORT_FAILED;
    KeAcquireSpinLock(&fdo->Lock, &irql);
    admitted = uasAdmit(fdo, pdo, irp, srb);
    if (admitted && victim != NULL) {
        for (e = fdo->Queue.Flink; e != &fdo->Queue; e = e->Flink) {
            if (CONTAINING_RECORD(e, IRP, Tail.Overlay.ListEntry)
                    ->Tail.Overlay.DriverContext[UAS_CTX_SRB] == victim) {
                victimIrp = CONTAINING_RECORD(e, IRP, Tail.Overlay.ListEntry);
                RemoveEntryList(e);
                st = SRB_STATUS_SUCCESS;
                break;
            }
        }
        if (victimIrp == NULL && uasFindSrb(fdo, victim) != NULL) {
            IoMarkIrpPending(irp);
            InsertTailList(&fdo->RecoveryRequests,
                           &irp->Tail.Overlay.ListEntry);
            queue = uasWantRecovery(fdo);
            st = 0;
        }
    }
    KeReleaseSpinLock(&fdo->Lock, irql);
    if (!admitted) {
        srb->SrbStatus = SRB_STATUS_NO_DEVICE;
        return UasCompleteIrp(irp, STATUS_NO_SUCH_DEVICE, 0);
    }
    if (victimIrp != NULL) {
        uasFailRequest(fdo, victimIrp, SRB_STATUS_ABORTED);
    }
    if (st == 0) {
        if (queue) {
            ExQueueWorkItem(&fdo->Recovery, DelayedWorkQueue);
        }
        return STATUS_PENDING;
    }
    uasFailRequest(fdo, irp, st);
    uasPump(fdo);
    /* The status uasCompleteRequest gave the IRP. */
    return (st == SRB_STATUS_SUCCESS) ? STATUS_SUCCESS
                                      : STATUS_IO_DEVICE_ERROR;
}

/* <= DISPATCH_LEVEL. SRB_FUNCTION_RESET_DEVICE, _RESET_LOGICAL_UNIT and
 * _RESET_BUS: held on the recovery list, one entry per SRB, and completed
 * with the outcome of the LOGICAL UNIT RESET (or the port reset). */
NTSTATUS UasEngineReset(PUAS_FDO fdo, PUAS_PDO pdo, PIRP irp,
                        PSCSI_REQUEST_BLOCK srb)
{
    BOOLEAN admitted;
    BOOLEAN queue;
    KIRQL irql;

    queue = FALSE;
    KeAcquireSpinLock(&fdo->Lock, &irql);
    admitted = uasAdmit(fdo, pdo, irp, srb);
    if (admitted) {
        IoMarkIrpPending(irp);
        InsertTailList(&fdo->RecoveryRequests, &irp->Tail.Overlay.ListEntry);
        queue = uasWantRecovery(fdo);
    }
    KeReleaseSpinLock(&fdo->Lock, irql);
    if (!admitted) {
        srb->SrbStatus = SRB_STATUS_NO_DEVICE;
        return UasCompleteIrp(irp, STATUS_NO_SUCH_DEVICE, 0);
    }
    if (queue) {
        ExQueueWorkItem(&fdo->Recovery, DelayedWorkQueue);
    }
    return STATUS_PENDING;
}

/* <= DISPATCH_LEVEL. SRB_FUNCTION_FLUSH_QUEUE: every request of the LUN
 * still queued is completed SRB_STATUS_REQUEST_FLUSHED. */
NTSTATUS UasEngineFlush(PUAS_FDO fdo, PUAS_PDO pdo, PIRP irp,
                        PSCSI_REQUEST_BLOCK srb)
{
    LIST_ENTRY drain;
    PLIST_ENTRY e;
    PLIST_ENTRY next;
    BOOLEAN admitted;
    KIRQL irql;

    InitializeListHead(&drain);
    KeAcquireSpinLock(&fdo->Lock, &irql);
    admitted = uasAdmit(fdo, pdo, irp, srb);
    if (admitted) {
        for (e = fdo->Queue.Flink; e != &fdo->Queue; e = next) {
            next = e->Flink;
            if (CONTAINING_RECORD(e, IRP, Tail.Overlay.ListEntry)
                    ->Tail.Overlay.DriverContext[UAS_CTX_PDO] == pdo) {
                RemoveEntryList(e);
                InsertTailList(&drain, e);
            }
        }
    }
    KeReleaseSpinLock(&fdo->Lock, irql);
    if (!admitted) {
        srb->SrbStatus = SRB_STATUS_NO_DEVICE;
        return UasCompleteIrp(irp, STATUS_NO_SUCH_DEVICE, 0);
    }
    while (!IsListEmpty(&drain)) {
        e = RemoveHeadList(&drain);
        uasFailRequest(fdo, CONTAINING_RECORD(e, IRP, Tail.Overlay.ListEntry),
                       SRB_STATUS_REQUEST_FLUSHED);
    }
    uasFailRequest(fdo, irp, SRB_STATUS_SUCCESS);
    return STATUS_SUCCESS;
}

/* <= DISPATCH_LEVEL. The LUN's PDO started: admission opens. */
VOID UasEngineOpenLun(PUAS_FDO fdo, PUAS_PDO pdo)
{
    KIRQL irql;

    KeAcquireSpinLock(&fdo->Lock, &irql);
    pdo->Admit = TRUE;
    KeReleaseSpinLock(&fdo->Lock, irql);
}

/*
 * PASSIVE_LEVEL. The LUN's PDO is stopping or leaving. Admission closes
 * first, then its queued and held requests are failed with srbStatus. Its
 * commands in flight are ended with srbStatus by the recovery worker, which
 * has the device terminate each first (ABORT TASK, escalating: uasCloseLun)
 * so their tags are safe to reuse; when the device cannot be reached they
 * are ended here, their tags quarantined. The call then waits - with no
 * timeout, since a command is finished only when the bus has given its
 * transfers back - for the last of the LUN's requests to complete.
 */
VOID UasEngineCloseLun(PUAS_FDO fdo, PUAS_PDO pdo, UCHAR srbStatus)
{
    LIST_ENTRY drain;
    PLIST_ENTRY e;
    PLIST_ENTRY next;
    BOOLEAN live;
    BOOLEAN viaWorker;
    BOOLEAN queue;
    ULONG t;
    KIRQL irql;

    InitializeListHead(&drain);
    live = FALSE;
    viaWorker = FALSE;
    queue = FALSE;
    KeAcquireSpinLock(&fdo->Lock, &irql);
    pdo->Admit = FALSE;
    for (e = fdo->Queue.Flink; e != &fdo->Queue; e = next) {
        next = e->Flink;
        if (CONTAINING_RECORD(e, IRP, Tail.Overlay.ListEntry)
                ->Tail.Overlay.DriverContext[UAS_CTX_PDO] == pdo) {
            RemoveEntryList(e);
            InsertTailList(&drain, e);
        }
    }
    for (e = fdo->RecoveryRequests.Flink; e != &fdo->RecoveryRequests;
         e = next) {
        next = e->Flink;
        if (CONTAINING_RECORD(e, IRP, Tail.Overlay.ListEntry)
                ->Tail.Overlay.DriverContext[UAS_CTX_PDO] == pdo) {
            RemoveEntryList(e);
            InsertTailList(&drain, e);
        }
    }
    if (fdo->Slots != NULL) {
        for (t = 1; t <= UAS_MAX_TAGS && !live; t++) {
            live = uasSlotOfPdo(&fdo->Slots[t], pdo);
        }
    }
    if (live && fdo->Started && !fdo->Gone && !fdo->Dead) {
        pdo->CloseStatus = srbStatus;
        pdo->CloseRequested = TRUE;
        KeClearEvent(&pdo->CloseDone);
        queue = uasWantRecovery(fdo);
        viaWorker = TRUE;
    }
    KeReleaseSpinLock(&fdo->Lock, irql);
    while (!IsListEmpty(&drain)) {
        e = RemoveHeadList(&drain);
        uasFailRequest(fdo, CONTAINING_RECORD(e, IRP, Tail.Overlay.ListEntry),
                       srbStatus);
    }
    if (viaWorker) {
        if (queue) {
            ExQueueWorkItem(&fdo->Recovery, DelayedWorkQueue);
        }
        (VOID)KeWaitForSingleObject(&pdo->CloseDone, Executive, KernelMode,
                                    FALSE, NULL);
    } else if (live) {
        uasEndPdoSlots(fdo, pdo, srbStatus, TRUE);
    }
    (VOID)KeWaitForSingleObject(&pdo->RequestsIdle, Executive, KernelMode,
                                FALSE, NULL);
    uasPump(fdo);
}

/*
 * PASSIVE_LEVEL. One command of the driver's own (REPORT LUNS, INQUIRY)
 * through the same engine, waited for. buffer is nonpaged pool, read into.
 * Only from the FDO's start, inside its PnP IRP.
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

    KeAcquireSpinLock(&fdo->Lock, &irql);
    queued = uasAdmit(fdo, NULL, irp, srb);
    if (queued) {
        irp->Tail.Overlay.DriverContext[UAS_CTX_EVENT] = &done;
        InsertTailList(&fdo->Queue, &irp->Tail.Overlay.ListEntry);
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

/* <= DISPATCH_LEVEL. A LUN's queue was unlocked: start what waited. */
VOID UasEngineUnlockQueue(PUAS_FDO fdo)
{
    uasPump(fdo);
}
