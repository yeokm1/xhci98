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

/*
 * Task 45.23 (design record 20 section 4.3.3): an internal command's own
 * storage - its IRP (only a carrier on the queue, never sent), SRB, data
 * buffer and sense - and how its waiter and its finish meet. State is the
 * FDO lock's: the finish (uasIcmdDone) sets DONE and signals the waiter; a
 * waiter whose deadline passed first sets ABANDONED and returns, and the
 * finish then frees everything itself, whenever it comes.
 */
#define UAS_ICMD_WAITING        0UL
#define UAS_ICMD_ABANDONED      1UL
#define UAS_ICMD_DONE           2UL
#define UAS_ICMD_SENSE          32UL

typedef struct _UAS_ICMD {
    KEVENT Done;
    ULONG State;
    PIRP Irp;
    PSCSI_REQUEST_BLOCK Srb;
    PUCHAR Buffer;
    UCHAR Sense[UAS_ICMD_SENSE];
} UAS_ICMD, *PUAS_ICMD;

static VOID uasPump(PUAS_FDO fdo);
static VOID uasFinish(PUAS_FDO fdo, PUAS_SLOT slot);
static VOID uasRecoveryWorker(PVOID context);
static VOID uasIcmdDone(PUAS_FDO fdo, PUAS_ICMD ic);
static VOID uasWakeWorker(PVOID context);

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
    /* Task 45.23: none while the sleep's seal stands (uasUnseal queues
     * what waited). */
    if (!fdo->Started || fdo->RecoveryQueued || fdo->Sealed) {
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
    PUAS_ICMD ic;
    PUAS_PDO pdo;
    NTSTATUS status;
    ULONG s;
    KIRQL irql;

    ic = (PUAS_ICMD)irp->Tail.Overlay.DriverContext[UAS_CTX_EVENT];
    if (ic != NULL) {
        uasIcmdDone(fdo, ic);
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
    /* Clamped, so a timeout from 0x80000000 up is not a negative count
     * that ends the request at the first tick. */
    if (srb->TimeOutValue == 0) {
        slot->TimeLeft = UAS_DEFAULT_TIMEOUT;
    } else if (srb->TimeOutValue > 0x7FFFFFFFUL) {
        slot->TimeLeft = 0x7FFFFFFFL;
    } else {
        slot->TimeLeft = (LONG)srb->TimeOutValue;
    }

    if (srb->DataTransferLength != 0) {
        if ((srb->SrbFlags & SRB_FLAGS_UNSPECIFIED_DIRECTION) ==
            SRB_FLAGS_DATA_IN) {
            dir = UAS_DIR_IN;
        } else if ((srb->SrbFlags & SRB_FLAGS_UNSPECIFIED_DIRECTION) ==
                   SRB_FLAGS_DATA_OUT) {
            dir = UAS_DIR_OUT;
        } else {
            dir = UasCdbDataDirection(srb->Cdb, srb->CdbLength);
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

/* Lock held. Task 36.21: a command started, counted by the commands in
 * flight with it and by its data length. */
static VOID uasTeleCount(PUAS_FDO fdo, PUAS_SLOT slot)
{
    if (fdo->Active > fdo->ActiveMax) {
        fdo->ActiveMax = fdo->Active;
    }
    fdo->DepthHist[UasTeleDepthBucket(fdo->Active)]++;
    if (!slot->NeedsData) {
        return;
    }
    fdo->DataCommands++;
    UasTeleAdd64(&fdo->DataLo, &fdo->DataHi, slot->DataLength);
    if (slot->DataLength > fdo->LengthMax) {
        fdo->LengthMax = slot->DataLength;
    }
    fdo->Hist[UasTeleBucket(slot->DataLength)]++;
}

/*
 * <= DISPATCH_LEVEL, no lock held, from a context that keeps the FDO (a
 * referenced engine context or a PDO dispatch inside its Busy count).
 * Starts queued requests while the queue depth and the tags allow. A
 * request whose LUN has its queue locked waits unless it bypasses the lock
 * (the class driver's power SRBs). While the sleep hold stands (Holding,
 * task 45.23) only the driver's own internal requests start.
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
            if ((!fdo->Holding ||
                 irp->Tail.Overlay.DriverContext[UAS_CTX_EVENT] != NULL) &&
                (pdo == NULL || !pdo->QueueLocked ||
                 (srb->SrbFlags & SRB_FLAGS_BYPASS_LOCKED_QUEUE) != 0)) {
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
            uasTeleCount(fdo, slot);
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

/*
 * Task 36.21: the telemetry record handed to the bus (xhci98_xfer.h,
 * IOCTL_XHCI98_TELEMETRY). Built under the lock from the readings, sent
 * after its release with the FDO's own IRP, which this bus completes before
 * its dispatch returns; counted in the rundown from the send to the end of
 * the completion, so UasEngineStop waits for it - and cancels it, should a
 * bus or a filter between leave it pending. Sent when any word differs
 * from the last record the bus took, so a counter that moves alone (a
 * timeout on a command still in flight) is reported; a send that failed
 * leaves the last taken record as it was, so the next tick tries again. A
 * bus that does not know the request is not asked again. Never through
 * UasSyncIoctl (PASSIVE_LEVEL only, and waiting) and never under the lock.
 */
static NTSTATUS uasTeleDone(PDEVICE_OBJECT device, PIRP irp, PVOID context)
{
    PUAS_FDO fdo;
    KIRQL irql;

    UNREFERENCED_PARAMETER(device);
    fdo = (PUAS_FDO)context;
    KeAcquireSpinLock(&fdo->Lock, &irql);
    if (irp->IoStatus.Status == STATUS_NOT_SUPPORTED ||
        irp->IoStatus.Status == STATUS_INVALID_DEVICE_REQUEST) {
        fdo->TeleOff = TRUE;
    } else if (NT_SUCCESS(irp->IoStatus.Status)) {
        UasCopy(&fdo->TeleLast, &fdo->TeleRecord, sizeof(fdo->TeleLast));
        fdo->TeleLast.Word[XHCI98_TELE_SEQUENCE] = 0;
    }
    fdo->TeleBusy = FALSE;
    KeReleaseSpinLock(&fdo->Lock, irql);
    uasDeref(fdo);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

/* Lock held. TRUE when a record was built and is to be sent; the caller
 * then sends it with uasTeleSend after the lock's release. */
static BOOLEAN uasTeleBuild(PUAS_FDO fdo)
{
    PULONG w;
    ULONG i;

    if (fdo->TeleIrp == NULL || fdo->TeleBusy || fdo->TeleOff ||
        fdo->Gone || !fdo->Started) {
        return FALSE;
    }
    w = fdo->TeleRecord.Word;
    UasZero(w, sizeof(fdo->TeleRecord));
    w[XHCI98_TELE_SIG] = XHCI98_TELE_SIGNATURE;
    w[XHCI98_TELE_VER] = XHCI98_TELE_VERSION;
    w[XHCI98_TELE_SIZE] = sizeof(XHCI98_TELEMETRY);
    w[XHCI98_TELE_COMMAND_BYTES] = fdo->CommandBytes;
    w[XHCI98_TELE_POLICY_SOURCE] = fdo->PolicySource;
    w[XHCI98_TELE_STREAMED] = fdo->Streamed ? 1UL : 0UL;
    w[XHCI98_TELE_QUEUE_DEPTH] = fdo->QueueDepth;
    w[XHCI98_TELE_ACTIVE] = fdo->Active;
    w[XHCI98_TELE_ACTIVE_MAX] = fdo->ActiveMax;
    w[XHCI98_TELE_COMMANDS] = fdo->Counters[UAS_CTR_COMMANDS];
    w[XHCI98_TELE_DATA_COMMANDS] = fdo->DataCommands;
    w[XHCI98_TELE_DATA_LO] = fdo->DataLo;
    w[XHCI98_TELE_DATA_HI] = fdo->DataHi;
    w[XHCI98_TELE_LENGTH_MAX] = fdo->LengthMax;
    for (i = 0; i < XHCI98_TELE_BUCKETS; i++) {
        w[XHCI98_TELE_HIST + i] = fdo->Hist[i];
    }
    for (i = 0; i < XHCI98_TELE_DEPTHS; i++) {
        w[XHCI98_TELE_DEPTH_HIST + i] = fdo->DepthHist[i];
    }
    for (i = 0; i < 8; i++) {
        w[XHCI98_TELE_COUNTERS + i] = fdo->Counters[i];
    }
    w[XHCI98_TELE_UNSUPPORTED] = fdo->PolicyUnsupported;
    w[XHCI98_TELE_MALFORMED] = fdo->PolicyMalformed;
    w[XHCI98_TELE_TRANSPORT] = fdo->PolicyTransport;
    /* The sequence is the one word that moves on every send: compared
     * as 0 on both sides. */
    for (i = 0; i < XHCI98_TELE_WORDS; i++) {
        if (w[i] != fdo->TeleLast.Word[i]) {
            break;
        }
    }
    if (i == XHCI98_TELE_WORDS) {
        return FALSE;
    }
    fdo->TeleSequence++;
    w[XHCI98_TELE_SEQUENCE] = fdo->TeleSequence;
    fdo->TeleBusy = TRUE;
    fdo->TeleSubmitting = TRUE;
    uasRef(fdo);
    return TRUE;
}

/* No lock held, <= DISPATCH_LEVEL, after uasTeleBuild said TRUE. */
static VOID uasTeleSend(PUAS_FDO fdo)
{
    PIO_STACK_LOCATION next;
    PIRP irp;
    KIRQL irql;

    irp = fdo->TeleIrp;
    UasIrpReset(irp, fdo->TeleIrpSize, fdo->Lower->StackSize);
    irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    next = IoGetNextIrpStackLocation(irp);
    next->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
    next->Parameters.DeviceIoControl.IoControlCode = IOCTL_XHCI98_TELEMETRY;
    next->Parameters.Others.Argument1 = &fdo->TeleRecord;
    UAS_SET_COMPLETION_ALWAYS(irp, uasTeleDone, fdo);
    (VOID)IoCallDriver(fdo->Lower, irp);
    KeAcquireSpinLock(&fdo->Lock, &irql);
    fdo->TeleSubmitting = FALSE;
    KeReleaseSpinLock(&fdo->Lock, irql);
}

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
    BOOLEAN tele;
    ULONG t;
    KIRQL irql;

    UNREFERENCED_PARAMETER(dpc);
    UNREFERENCED_PARAMETER(arg1);
    UNREFERENCED_PARAMETER(arg2);
    fdo = (PUAS_FDO)context;
    queue = FALSE;
    expired = FALSE;
    tele = FALSE;
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
    tele = uasTeleBuild(fdo);
    /* Re-armed under the lock, so UasEngineStop's KeCancelTimer either
     * finds it queued or this DPC finds TimerStop. */
    uasArmTimer(fdo);
    KeReleaseSpinLock(&fdo->Lock, irql);
    if (queue) {
        ExQueueWorkItem(&fdo->Recovery, DelayedWorkQueue);
    }
    if (tele) {
        uasTeleSend(fdo);
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
    BOOLEAN answered;
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
    (VOID)KeWaitForSingleObject(&done, Executive, KernelMode, FALSE,
                                &timeout);

    uasActInit(&a);
    KeAcquireSpinLock(&fdo->Lock, &irql);
    /* Decided from the slot, not the wait: a RESPONSE IU parsed between
     * the timeout and this lock is still the device's answer, and its tag
     * is then no longer live in the device. */
    answered = (BOOLEAN)(slot->State == UAS_SLOT_FINAL && !slot->Failed);
    code = answered ? slot->ResponseCode : UAS_RC_TMF_FAILED;
    /* The event is on this stack: nothing may signal it after this. An
     * unanswered TMF may still be live in the device under its tag. */
    slot->TmfDone = NULL;
    slot->Quarantine = (BOOLEAN)!answered;
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
    /* Task 36.21's telemetry send; its absence only leaves the bus without
     * the record. */
    fdo->TeleIrp = UasIrpAlloc(stack, &fdo->TeleIrpSize);
    KeInitializeTimer(&fdo->Timer);
    KeInitializeDpc(&fdo->TimerDpc, uasTick, fdo);
    ExInitializeWorkItem(&fdo->Recovery, uasRecoveryWorker, fdo);
    /* Task 45.23: the sleep phase and the wake check, which act only on
     * an engine set up here (Slots). */
    KeInitializeEvent(&fdo->PowerLock, SynchronizationEvent, TRUE);
    ExInitializeWorkItem(&fdo->WakeItem, uasWakeWorker, fdo);
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
    UasFree(fdo->TeleIrp);
    fdo->TeleIrp = NULL;
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
    /* Task 45.23: a start ends any sleep hold; a new one is the next
     * sleep's. */
    fdo->Holding = FALSE;
    fdo->HoldCheck = FALSE;
    fdo->Sealed = FALSE;
    /* And the epoch the last wake check verified: the bus's epoch is its
     * device record's, which a restart may have replaced (a dormant PDO
     * revived on a fresh record counts from 0 again), so the first keep
     * after a start is always checked. No wake check runs here: the stop
     * waited out its rundown reference. */
    fdo->VerifiedEpoch = 0;
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
    BOOLEAN teleBusy;
    BOOLEAN teleSubmitting;
    KIRQL irql;

    InitializeListHead(&drain);
    KeAcquireSpinLock(&fdo->Lock, &irql);
    wasStarted = fdo->Started;
    fdo->Started = FALSE;
    fdo->TimerStop = TRUE;
    KeReleaseSpinLock(&fdo->Lock, irql);
    if (wasStarted && !KeCancelTimer(&fdo->Timer)) {
        UasWaitEvent(&fdo->TimerDone);
    }
    /* Task 36.21: no telemetry send is built now (Started is FALSE). One
     * a timer run has built is waited for until its IoCallDriver has
     * returned - a timer run on another processor may still be resetting
     * and sending the IRP, and a cancel before that would be lost - then,
     * still pending, cancelled; the rundown below waits
     * for its completion. The IRP is this driver's, freed only by
     * UasEngineFree after that rundown. */
    for (;;) {
        KeAcquireSpinLock(&fdo->Lock, &irql);
        teleBusy = fdo->TeleBusy;
        teleSubmitting = fdo->TeleSubmitting;
        KeReleaseSpinLock(&fdo->Lock, irql);
        if (!teleBusy || !teleSubmitting) {
            break;
        }
        uasSleepMs(1);
    }
    if (teleBusy) {
        (VOID)IoCancelIrp(fdo->TeleIrp);
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
        UasWaitEvent(&fdo->IoIdle);
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
        UasWaitEvent(&pdo->CloseDone);
    } else if (live) {
        uasEndPdoSlots(fdo, pdo, srbStatus, TRUE);
    }
    UasWaitEvent(&pdo->RequestsIdle);
    uasPump(fdo);
}

/* The finish's half of an internal command (uasCompleteRequest). */
static VOID uasIcmdFree(PUAS_ICMD ic)
{
    if (ic->Irp != NULL) {
        IoFreeIrp(ic->Irp);
    }
    UasFree(ic->Srb);
    UasFree(ic->Buffer);
    UasFree(ic);
}

static VOID uasIcmdDone(PUAS_FDO fdo, PUAS_ICMD ic)
{
    ULONG was;
    KIRQL irql;

    KeAcquireSpinLock(&fdo->Lock, &irql);
    was = ic->State;
    ic->State = UAS_ICMD_DONE;
    KeReleaseSpinLock(&fdo->Lock, irql);
    if (was == UAS_ICMD_ABANDONED) {
        uasIcmdFree(ic);
    } else {
        KeSetEvent(&ic->Done, IO_NO_INCREMENT, FALSE);
    }
}

/* Task 45.23: a deadline, as elapsed time summed from the system time's
 * low word between successive reads (so a phase of minutes needs no
 * 64-bit arithmetic). A step that goes backwards - the clock set back -
 * counts nothing, so a correction can extend a phase by one polling step
 * at most; one set forward can only end it early. Spent is in 100 ns
 * units: thirty seconds are far below its range. */
static VOID uasDeadlineStart(PUAS_DEADLINE d, ULONG ms)
{
    LARGE_INTEGER now;

    KeQuerySystemTime(&now);
    d->Last = now.LowPart;
    d->Spent = 0;
    d->Budget = ms;
}

static ULONG uasDeadlineLeft(PUAS_DEADLINE d)
{
    LARGE_INTEGER now;
    ULONG step;
    ULONG spent;

    KeQuerySystemTime(&now);
    step = now.LowPart - d->Last;
    d->Last = now.LowPart;
    if (step < 0x80000000UL) {
        d->Spent = (d->Spent + step < d->Spent) ? 0xFFFFFFFFUL
                                                : d->Spent + step;
    }
    spent = d->Spent / 10000UL;
    return (spent < d->Budget) ? d->Budget - spent : 0;
}

/*
 * PASSIVE_LEVEL. One SCSI command of the driver's own, data in or none,
 * queued as a class request is and waited for. With sense, autosense is
 * on and what came is copied out (*senseLength bytes; 0 when none). With
 * a deadline, the SRB's own timeout is no longer than what is left of it
 * (one second at least) and the wait ends with it: a command still owned
 * then is abandoned to its own finish (UAS_ICMD) and STATUS_IO_TIMEOUT
 * returned. The bounded wait is polled at 10 ms, as an event wait's
 * unsatisfied return on Windows 98 would need anyway (UasWaitEvent).
 */
NTSTATUS UasInternalCommandEx(PUAS_FDO fdo, ULONG lun, const UCHAR *cdb,
                              ULONG cdbLength, PVOID buffer, ULONG length,
                              PULONG transferred, PUCHAR scsiStatus,
                              PUCHAR sense, PULONG senseLength,
                              PUAS_DEADLINE deadline)
{
    PSCSI_REQUEST_BLOCK srb;
    PUAS_ICMD ic;
    NTSTATUS status;
    BOOLEAN queued;
    BOOLEAN done;
    ULONG left;
    ULONG got;
    KIRQL irql;

    *transferred = 0;
    *scsiStatus = 0;
    if (senseLength != NULL) {
        *senseLength = 0;
    }
    if (cdbLength > 16) {
        return STATUS_INVALID_PARAMETER;
    }
    left = (deadline != NULL) ? uasDeadlineLeft(deadline) : 0;
    if (deadline != NULL && left == 0) {
        return STATUS_IO_TIMEOUT;
    }
    ic = (PUAS_ICMD)UasAlloc(sizeof(UAS_ICMD));
    if (ic == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    UasZero(ic, sizeof(UAS_ICMD));
    ic->Srb = (PSCSI_REQUEST_BLOCK)UasAlloc(sizeof(SCSI_REQUEST_BLOCK));
    ic->Irp = IoAllocateIrp(1, FALSE);
    ic->Buffer = (length != 0) ? (PUCHAR)UasAlloc(length) : NULL;
    if (ic->Srb == NULL || ic->Irp == NULL ||
        (length != 0 && ic->Buffer == NULL)) {
        uasIcmdFree(ic);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    KeInitializeEvent(&ic->Done, NotificationEvent, FALSE);
    ic->State = UAS_ICMD_WAITING;
    srb = ic->Srb;
    UasZero(srb, sizeof(SCSI_REQUEST_BLOCK));
    if (length != 0) {
        UasZero(ic->Buffer, length);
    }
    srb->Length = sizeof(SCSI_REQUEST_BLOCK);
    srb->Function = SRB_FUNCTION_EXECUTE_SCSI;
    srb->Lun = (UCHAR)lun;
    srb->CdbLength = (UCHAR)cdbLength;
    UasCopy(srb->Cdb, cdb, cdbLength);
    srb->DataBuffer = ic->Buffer;
    srb->DataTransferLength = length;
    srb->SrbFlags = (length != 0) ? SRB_FLAGS_DATA_IN
                                  : SRB_FLAGS_NO_DATA_TRANSFER;
    if (sense != NULL) {
        srb->SenseInfoBuffer = ic->Sense;
        srb->SenseInfoBufferLength = (UCHAR)UAS_ICMD_SENSE;
    } else {
        srb->SrbFlags |= SRB_FLAGS_DISABLE_AUTOSENSE;
    }
    srb->TimeOutValue = 10;
    if (deadline != NULL && left / 1000UL < srb->TimeOutValue) {
        srb->TimeOutValue = (left < 1000UL) ? 1UL : left / 1000UL;
    }
    srb->SrbStatus = SRB_STATUS_PENDING;

    KeAcquireSpinLock(&fdo->Lock, &irql);
    queued = uasAdmit(fdo, NULL, ic->Irp, srb);
    if (queued) {
        ic->Irp->Tail.Overlay.DriverContext[UAS_CTX_EVENT] = ic;
        InsertTailList(&fdo->Queue, &ic->Irp->Tail.Overlay.ListEntry);
    }
    KeReleaseSpinLock(&fdo->Lock, irql);
    if (!queued) {
        uasIcmdFree(ic);
        return STATUS_NO_SUCH_DEVICE;
    }
    uasPump(fdo);
    if (deadline == NULL) {
        UasWaitEvent(&ic->Done);
    } else {
        for (;;) {
            left = uasDeadlineLeft(deadline);
            KeAcquireSpinLock(&fdo->Lock, &irql);
            done = (BOOLEAN)(ic->State == UAS_ICMD_DONE);
            if (!done && left == 0) {
                ic->State = UAS_ICMD_ABANDONED;
            }
            KeReleaseSpinLock(&fdo->Lock, irql);
            if (done) {
                break;
            }
            if (left == 0) {
                /* Its finish frees it. */
                return STATUS_IO_TIMEOUT;
            }
            uasSleepMs(10);
        }
        /* DONE is set before the signal: wait that out, so nothing of the
         * finish still touches the event when it is freed. */
        UasWaitEvent(&ic->Done);
    }
    *scsiStatus = srb->ScsiStatus;
    got = srb->DataTransferLength;
    if (got > length) {
        got = length;
    }
    if (got != 0) {
        UasCopy(buffer, ic->Buffer, got);
    }
    *transferred = got;
    if (sense != NULL && senseLength != NULL &&
        (srb->SrbStatus & SRB_STATUS_AUTOSENSE_VALID) != 0) {
        *senseLength = srb->SenseInfoBufferLength;
        if (*senseLength > UAS_ICMD_SENSE) {
            *senseLength = UAS_ICMD_SENSE;
        }
        UasCopy(sense, ic->Sense, *senseLength);
    }
    status = (SRB_STATUS(srb->SrbStatus) == SRB_STATUS_SUCCESS ||
              SRB_STATUS(srb->SrbStatus) == SRB_STATUS_DATA_OVERRUN)
                 ? STATUS_SUCCESS : STATUS_IO_DEVICE_ERROR;
    uasIcmdFree(ic);
    return status;
}

/*
 * PASSIVE_LEVEL. One command of the driver's own (REPORT LUNS, INQUIRY)
 * through the same engine, waited for, unbounded and with no sense: the
 * FDO's start's, inside its PnP IRP. buffer is nonpaged pool, read into.
 */
NTSTATUS UasInternalCommand(PUAS_FDO fdo, ULONG lun, const UCHAR *cdb,
                            ULONG cdbLength, PVOID buffer, ULONG length,
                            PULONG transferred, PUCHAR scsiStatus)
{
    return UasInternalCommandEx(fdo, lun, cdb, cdbLength, buffer, length,
                                transferred, scsiStatus, NULL, NULL, NULL);
}

/* <= DISPATCH_LEVEL. A LUN's queue was unlocked: start what waited. */
VOID UasEngineUnlockQueue(PUAS_FDO fdo)
{
    uasPump(fdo);
}

/* ------------------------------------------------------------------ */
/* Task 45.23: the sleep hold and the wake check (design record 20    */
/* section 4.3.3). PASSIVE_LEVEL unless noted.                        */
/* ------------------------------------------------------------------ */

/* Each phase's deadline (B3, B6) and the steps it is polled at. */
#define UAS_POWER_PHASE_MS      30000UL
#define UAS_POWER_QUERY_MS      50UL
#define UAS_POWER_READY_MS      500UL
#define UAS_ID_TRIES            4UL
#define UAS_ID_PAGE_BYTES       255UL

#define UAS_SCSI_STATUS_CHECK   0x02

static VOID uasPowerLock(PUAS_FDO fdo)
{
    UasWaitEvent(&fdo->PowerLock);
}

static VOID uasPowerUnlock(PUAS_FDO fdo)
{
    KeSetEvent(&fdo->PowerLock, IO_NO_INCREMENT, FALSE);
}

/* One IOCTL_XHCI98_RESUME_STATE request; STATUS_SUCCESS only with the bus's
 * success status in the reply. */
static NTSTATUS uasResumeRequest(PUAS_FDO fdo, ULONG op, ULONG value,
                                 PXHCI98_RESUME_STATE rs)
{
    NTSTATUS status;

    UasZero(rs, sizeof(*rs));
    rs->Signature = XHCI98_RESUME_SIGNATURE;
    rs->Version = XHCI98_RESUME_VERSION;
    rs->Size = sizeof(*rs);
    rs->Op = op;
    rs->Value = value;
    rs->Status = XHCI98_RESUME_INVALID;
    status = UasBusIoctl(fdo, IOCTL_XHCI98_RESUME_STATE, rs);
    if (NT_SUCCESS(status) && rs->Status != XHCI98_RESUME_SUCCESS) {
        status = STATUS_UNSUCCESSFUL;
    }
    return status;
}

/* Lock held. Nothing of the engine's own in flight or about to be: every
 * slot free (a slot is taken before its transfers are built and freed
 * only once idle, so a submission between its decision and its
 * IoCallDriver keeps one), the shared read idle, no recovery queued or
 * running. */
static BOOLEAN uasQuiescentLocked(PUAS_FDO fdo)
{
    ULONG t;

    if (fdo->Recovering || fdo->RecoveryQueued ||
        fdo->SharedStatus.Busy || fdo->SharedStatus.CancelRefs != 0) {
        return FALSE;
    }
    for (t = 1; t <= UAS_MAX_TAGS; t++) {
        if (fdo->Slots[t].State != UAS_SLOT_FREE) {
            return FALSE;
        }
    }
    return TRUE;
}

/* The seal lifted (the wake check's, B6, or a later sleep's): recovery
 * refused while it stood is queued now. */
static VOID uasUnseal(PUAS_FDO fdo)
{
    BOOLEAN queue;
    KIRQL irql;

    queue = FALSE;
    KeAcquireSpinLock(&fdo->Lock, &irql);
    if (fdo->Sealed) {
        fdo->Sealed = FALSE;
        /* Whatever recovery was refused meanwhile - a request held, a
         * slot timed out or failed - the worker finds for itself; one with
         * nothing to do ends at once. */
        queue = uasWantRecovery(fdo);
    }
    KeReleaseSpinLock(&fdo->Lock, irql);
    if (queue) {
        ExQueueWorkItem(&fdo->Recovery, DelayedWorkQueue);
    }
}

/* The present LUNs, each held against its removal by its PDO's Busy count
 * (uasPdoUnlink unlinks it under the FDO lock, then waits that out), so a
 * LUN that leaves meanwhile neither frees what is read nor moves another
 * out of the walk. Returns how many; uasLunsRelease lets them go. */
static ULONG uasLunsHold(PUAS_FDO fdo, PUAS_PDO *luns)
{
    PUAS_PDO pdo;
    ULONG n;
    ULONG i;
    KIRQL irql;

    n = 0;
    KeAcquireSpinLock(&fdo->Lock, &irql);
    for (i = 0; i < fdo->LunCount && i < UAS_MAX_LUNS; i++) {
        pdo = fdo->Luns[i];
        if (pdo != NULL && pdo->Present && !pdo->Gone) {
            (VOID)InterlockedIncrement(&pdo->Busy);
            luns[n++] = pdo;
        }
    }
    KeReleaseSpinLock(&fdo->Lock, irql);
    return n;
}

static VOID uasLunsRelease(PUAS_PDO *luns, ULONG n)
{
    ULONG i;

    for (i = 0; i < n; i++) {
        if (InterlockedDecrement(&luns[i]->Busy) == 0) {
            KeSetEvent(&luns[i]->BusyIdle, IO_NO_INCREMENT, FALSE);
        }
    }
}

/*
 * One identity read, retried past a UNIT ATTENTION (UAS_ID_TRIES in all);
 * one with ASC 28h marks the record. Returns the command's status, with
 * *cls its sense's class on a CHECK CONDITION.
 */
static NTSTATUS uasIdCommand(PUAS_FDO fdo, ULONG lun, const UCHAR *cdb,
                             ULONG cdbLength, PUCHAR buf, ULONG length,
                             PULONG got, PUAS_LUN_ID id, PULONG cls,
                             PUAS_DEADLINE deadline)
{
    UCHAR sense[UAS_ICMD_SENSE];
    ULONG senseLength;
    ULONG tries;
    UCHAR scsi;
    NTSTATUS status;

    status = STATUS_IO_DEVICE_ERROR;
    for (tries = 0; tries < UAS_ID_TRIES; tries++) {
        *cls = UAS_SENSE_OTHER;
        status = UasInternalCommandEx(fdo, lun, cdb, cdbLength, buf, length,
                                      got, &scsi, sense, &senseLength,
                                      deadline);
        if (NT_SUCCESS(status) || scsi != UAS_SCSI_STATUS_CHECK) {
            return status;
        }
        *cls = UasSenseClass(sense, senseLength);
        if (*cls == UAS_SENSE_UA_MEDIUM) {
            id->MediumChanged = 1;
        } else if (*cls != UAS_SENSE_UA) {
            return status;
        }
    }
    return status;
}

/* A vital product data page read whole: its advertised length must have
 * come in full and fit `max`, or the read fails - a prefix is never taken
 * for the identity. Returns the length, *ok FALSE on failure. */
static ULONG uasIdPage(PUAS_FDO fdo, ULONG lun, UCHAR page, PUCHAR buf,
                       PUCHAR out, ULONG max, PUAS_LUN_ID id,
                       PUAS_DEADLINE deadline, PBOOLEAN ok)
{
    UCHAR cdb[6];
    ULONG got;
    ULONG cls;
    ULONG n;

    UasZero(cdb, sizeof(cdb));
    cdb[0] = 0x12;                          /* INQUIRY, EVPD */
    cdb[1] = 0x01;
    cdb[2] = page;
    cdb[4] = (UCHAR)UAS_ID_PAGE_BYTES;
    *ok = FALSE;
    if (!NT_SUCCESS(uasIdCommand(fdo, lun, cdb, 6, buf, UAS_ID_PAGE_BYTES,
                                 &got, id, &cls, deadline)) ||
        got < 4 || buf[1] != page) {
        return 0;
    }
    n = (page == 0x80) ? (ULONG)buf[3] : (((ULONG)buf[2] << 8) | buf[3]);
    if (n > got - 4 || n > max) {
        return 0;
    }
    UasCopy(out, buf + 4, n);
    *ok = TRUE;
    return n;
}

/*
 * One LUN's identity (uas_iu.h, UAS_LUN_ID): INQUIRY's removable bit,
 * pages 0x80 and 0x83 when page 0x00 lists them, read whole, READ
 * CAPACITY (10), (16) above (10)'s range, or no medium. NOT READY
 * becoming ready, or wanting a start, is given START STOP UNIT (start, no
 * LoEj - nothing written) and the read retried every UAS_POWER_READY_MS
 * within the deadline.
 */
static VOID uasReadId(PUAS_FDO fdo, ULONG lun, PUAS_LUN_ID id,
                      PUAS_DEADLINE deadline)
{
    UCHAR cdb[16];
    PUCHAR buf;
    ULONG got;
    ULONG cls;
    ULONG has80;
    ULONG has83;
    BOOLEAN started;
    BOOLEAN ok;

    UasZero(id, sizeof(*id));
    buf = (PUCHAR)UasAlloc(UAS_ID_PAGE_BYTES);
    if (buf == NULL) {
        return;
    }
    UasZero(cdb, sizeof(cdb));
    cdb[0] = 0x12;                          /* INQUIRY */
    cdb[4] = 36;
    if (!NT_SUCCESS(uasIdCommand(fdo, lun, cdb, 6, buf, 36, &got, id, &cls,
                                 deadline)) || got < 2) {
        goto done;
    }
    id->Removable = ((buf[1] & 0x80) != 0) ? 1UL : 0UL;

    has80 = 0;
    has83 = 0;
    cdb[1] = 0x01;                          /* EVPD, page 0x00 */
    cdb[2] = 0x00;
    cdb[4] = (UCHAR)UAS_ID_PAGE_BYTES;
    if (NT_SUCCESS(uasIdCommand(fdo, lun, cdb, 6, buf, UAS_ID_PAGE_BYTES,
                                &got, id, &cls, deadline))) {
        /* The whole list, or the read failed: a page cut off is never
         * taken as one the LUN lacks. */
        if (!UasVpdListComplete(buf, got)) {
            goto done;
        }
        has80 = UasVpdListHas(buf, got, 0x80);
        has83 = UasVpdListHas(buf, got, 0x83);
    } else if (cls != UAS_SENSE_ILLEGAL) {
        /* Not a LUN without the pages: a read that failed. */
        goto done;
    }
    if (has80) {
        id->SerialLength = uasIdPage(fdo, lun, 0x80, buf, id->Serial,
                                     UAS_ID_SERIAL_MAX, id, deadline, &ok);
        if (!ok) {
            goto done;
        }
    }
    if (has83) {
        id->DevIdLength = uasIdPage(fdo, lun, 0x83, buf, id->DevId,
                                    UAS_ID_DEVID_MAX, id, deadline, &ok);
        if (!ok) {
            goto done;
        }
    }

    started = FALSE;
    for (;;) {
        UasZero(cdb, sizeof(cdb));
        cdb[0] = 0x25;                      /* READ CAPACITY (10) */
        if (NT_SUCCESS(uasIdCommand(fdo, lun, cdb, 10, buf, 8, &got, id,
                                    &cls, deadline))) {
            if (got < 8) {
                break;
            }
            if (buf[0] == 0xFF && buf[1] == 0xFF && buf[2] == 0xFF &&
                buf[3] == 0xFF) {
                UasZero(cdb, sizeof(cdb));
                cdb[0] = 0x9E;              /* READ CAPACITY (16) */
                cdb[1] = 0x10;
                cdb[13] = 32;
                if (!NT_SUCCESS(uasIdCommand(fdo, lun, cdb, 16, buf, 32,
                                             &got, id, &cls, deadline)) ||
                    got < 12) {
                    break;
                }
                UasCopy(id->Cap, buf, 12);
            } else {
                UasZero(id->Cap, sizeof(id->Cap));
                UasCopy(id->Cap + 4, buf, 8);
            }
            id->CapState = UAS_ID_CAP_VALID;
            break;
        }
        if (cls == UAS_SENSE_NO_MEDIUM) {
            id->CapState = UAS_ID_CAP_NO_MEDIUM;
            break;
        }
        if (cls != UAS_SENSE_BECOMING ||
            uasDeadlineLeft(deadline) < UAS_POWER_READY_MS) {
            break;
        }
        if (!started) {
            UasZero(cdb, sizeof(cdb));
            cdb[0] = 0x1B;                  /* START STOP UNIT: start */
            cdb[4] = 0x01;
            (VOID)uasIdCommand(fdo, lun, cdb, 6, NULL, 0, &got, id, &cls,
                               deadline);
            started = TRUE;
        }
        uasSleepMs(UAS_POWER_READY_MS);
    }
    id->Read = (id->CapState != UAS_ID_CAP_FAILED) ? 1UL : 0UL;

done:
    UasFree(buf);
}

/*
 * The sleep phase (B3), at the FDO's system set-power to a sleeping state,
 * before it is passed down, inside the engine's rundown (a reference,
 * taken before any wait and kept until the caller's UasEngineRelease, so
 * UasEngineStop waits for it): the hold, any earlier declaration
 * withdrawn, the drain, each present LUN's identity read and its cache
 * flushed, then - quiescent, every identity one the wake check could pass,
 * the bus knowing the request - the seal (no recovery is queued from now
 * on, so nothing more is built for the bus until the wake check) and the
 * quiet declaration. All within one deadline; whatever ends it, the IRP
 * goes down after, the hold standing. TRUE when it took the reference.
 */
BOOLEAN UasEngineSleep(PUAS_FDO fdo)
{
    XHCI98_RESUME_STATE rs;
    UAS_DEADLINE deadline;
    PUAS_PDO luns[UAS_MAX_LUNS];
    UCHAR cdb[10];
    NTSTATUS status;
    ULONG count;
    ULONG flushed;
    ULONG refused;
    ULONG got;
    ULONG i;
    UCHAR scsi;
    BOOLEAN ok;
    BOOLEAN declare;
    BOOLEAN stacked;
    KIRQL irql;

    if (fdo->Slots == NULL) {
        return FALSE;
    }
    /* The reference first, before any wait, so UasEngineStop's rundown
     * covers the whole phase and the caller's dispatch after it. */
    KeAcquireSpinLock(&fdo->Lock, &irql);
    ok = (BOOLEAN)(fdo->Started && !fdo->Gone && !fdo->Dead);
    if (ok) {
        uasRef(fdo);
    }
    KeReleaseSpinLock(&fdo->Lock, irql);
    if (!ok) {
        return FALSE;
    }
    uasPowerLock(fdo);
    /* A hold still standing - its wake check not yet run - is carried on:
     * its records stay the baseline that check compares with, and this
     * sleep declares nothing, so the check that releases the hold is the
     * one owed (the query path, DeclareTried kept). Its seal, if any, is
     * lifted: the resume it guarded is over, and this phase's commands
     * need recovery. */
    uasUnseal(fdo);
    KeAcquireSpinLock(&fdo->Lock, &irql);
    stacked = (BOOLEAN)(fdo->Holding && !fdo->Dead);
    fdo->Episode++;
    fdo->Holding = TRUE;
    fdo->HoldCheck = FALSE;
    KeReleaseSpinLock(&fdo->Lock, irql);
    uasDeadlineStart(&deadline, UAS_POWER_PHASE_MS);
    status = uasResumeRequest(fdo, XHCI98_RESUME_OP_WITHDRAW,
                              fdo->Episode & 0x00FFFFFFUL, &rs);
    if (!stacked) {
        fdo->DeclareTried = FALSE;
        fdo->BusKnown = (BOOLEAN)(status != STATUS_NOT_SUPPORTED &&
                                  status != STATUS_INVALID_DEVICE_REQUEST);
        fdo->WithdrawOk = (BOOLEAN)NT_SUCCESS(status);
    }

    for (;;) {
        KeAcquireSpinLock(&fdo->Lock, &irql);
        ok = uasQuiescentLocked(fdo);
        KeReleaseSpinLock(&fdo->Lock, irql);
        if (ok || uasDeadlineLeft(&deadline) == 0) {
            break;
        }
        uasSleepMs(10);
    }
    /* Drained, or nothing more is sent: a drain that timed out flushes
     * nothing and declares nothing. */
    declare = (BOOLEAN)(ok && !stacked);
    flushed = 0;
    refused = 0;
    count = ok ? uasLunsHold(fdo, luns) : 0;
    for (i = 0; ok && i < count; i++) {
        if (!stacked) {
            uasReadId(fdo, luns[i]->Lun, &luns[i]->Id, &deadline);
        }
        if (!UasIdMatch(&luns[i]->Id, &luns[i]->Id)) {
            /* One the wake check could not pass: kept, it would only be
             * cycled then. Its cache is flushed all the same. */
            declare = FALSE;
        }
        UasZero(cdb, sizeof(cdb));
        cdb[0] = 0x35;                      /* SYNCHRONIZE CACHE (10) */
        status = UasInternalCommandEx(fdo, luns[i]->Lun, cdb, 10, NULL, 0,
                                      &got, &scsi, NULL, NULL, &deadline);
        if (NT_SUCCESS(status)) {
            flushed++;
        } else if (scsi == UAS_SCSI_STATUS_CHECK) {
            refused++;
        } else {
            /* Timed out (abandoned) or failed in transport: the phase
             * ends. */
            ok = FALSE;
            declare = FALSE;
        }
    }
    uasLunsRelease(luns, count);
    /* A phase whose deadline has passed declares nothing, whatever came
     * back meanwhile. */
    if (declare && fdo->BusKnown && fdo->WithdrawOk &&
        uasDeadlineLeft(&deadline) != 0) {
        /* The proof and the seal in one hold of the lock: with the pump
         * held and no recovery to be queued, nothing is built for the bus
         * from here to the wake check. */
        KeAcquireSpinLock(&fdo->Lock, &irql);
        declare = uasQuiescentLocked(fdo);
        if (declare) {
            fdo->Sealed = TRUE;
        }
        KeReleaseSpinLock(&fdo->Lock, irql);
        if (declare) {
            fdo->DeclareTried = TRUE;
            (VOID)uasResumeRequest(fdo, XHCI98_RESUME_OP_DECLARE,
                                   ((flushed & 0xFFUL) << 8) |
                                       (refused & 0xFFUL),
                                   &rs);
        }
    }
    uasPowerUnlock(fdo);
    /* The reference stays for the caller's dispatch: UasEngineRelease. */
    return TRUE;
}

/* A rundown reference for a power dispatch: the sleep phase's, dropped
 * once its IRP has gone down, and the S0's (UasEngineHold, FALSE and none
 * taken once the engine is stopped), from before it is passed down to the
 * last line of its completion. Any IRQL <= DISPATCH_LEVEL. */
BOOLEAN UasEngineHold(PUAS_FDO fdo)
{
    BOOLEAN held;
    KIRQL irql;

    /* Under the lock UasEngineStop closes admission under, so a
     * reference is either seen by its rundown or never taken. */
    KeAcquireSpinLock(&fdo->Lock, &irql);
    held = fdo->Started;
    if (held) {
        uasRef(fdo);
    }
    KeReleaseSpinLock(&fdo->Lock, irql);
    return held;
}

VOID UasEngineRelease(PUAS_FDO fdo)
{
    uasDeref(fdo);
}

/* <= DISPATCH_LEVEL. The S0, completed below: the hold standing asks for
 * its wake check, queued once with a rundown reference. */
VOID UasEngineWake(PUAS_FDO fdo)
{
    BOOLEAN queue;
    KIRQL irql;

    queue = FALSE;
    KeAcquireSpinLock(&fdo->Lock, &irql);
    if (fdo->Started && fdo->Holding) {
        fdo->HoldCheck = TRUE;
        if (!fdo->WakeQueued) {
            fdo->WakeQueued = TRUE;
            uasRef(fdo);
            queue = TRUE;
        }
    }
    KeReleaseSpinLock(&fdo->Lock, irql);
    if (queue) {
        ExQueueWorkItem(&fdo->WakeItem, DelayedWorkQueue);
    }
}

/* B7: every present LUN read again and compared with its record; all
 * passing, the records refreshed. */
static BOOLEAN uasMediumCheck(PUAS_FDO fdo, PUAS_DEADLINE deadline)
{
    PUAS_PDO luns[UAS_MAX_LUNS];
    PUAS_LUN_ID now;
    BOOLEAN ok;
    ULONG count;
    ULONG i;

    now = (PUAS_LUN_ID)UasAlloc(sizeof(UAS_LUN_ID) * UAS_MAX_LUNS);
    if (now == NULL) {
        return FALSE;
    }
    count = uasLunsHold(fdo, luns);
    ok = TRUE;
    for (i = 0; ok && i < count; i++) {
        uasReadId(fdo, luns[i]->Lun, &now[i], deadline);
        ok = (BOOLEAN)UasIdMatch(&luns[i]->Id, &now[i]);
    }
    for (i = 0; ok && i < count; i++) {
        UasCopy(&luns[i]->Id, &now[i], sizeof(UAS_LUN_ID));
    }
    uasLunsRelease(luns, count);
    UasFree(now);
    return ok;
}

/*
 * The wake check (B6, B7): on its own work item, under the power lock, and
 * only for a hold still waiting for it. The bus is asked (UasWakeDecide)
 * until its pass has ended or the deadline passed; then the seal is lifted
 * and a drive kept across a re-initialization has its medium checked.
 * Released, the held requests start; failed, the FDO is Dead, they
 * complete SRB_STATUS_NO_DEVICE, and the port is cycled so the drive
 * comes back as a new device. The outcome goes to the bus's log.
 */
static VOID uasWakeWorker(PVOID context)
{
    XHCI98_RESUME_STATE rs;
    UAS_DEADLINE deadline;
    PUAS_FDO fdo;
    NTSTATUS status;
    ULONG decision;
    ULONG report;
    ULONG spent;
    BOOLEAN act;
    BOOLEAN cycle;
    KIRQL irql;

    fdo = (PUAS_FDO)context;
    uasPowerLock(fdo);
    KeAcquireSpinLock(&fdo->Lock, &irql);
    fdo->WakeQueued = FALSE;
    act = (BOOLEAN)(fdo->Started && fdo->Holding && fdo->HoldCheck);
    KeReleaseSpinLock(&fdo->Lock, irql);
    if (!act) {
        uasPowerUnlock(fdo);
        uasDeref(fdo);
        return;
    }

    uasDeadlineStart(&deadline, UAS_POWER_PHASE_MS);
    UasZero(&rs, sizeof(rs));
    if (!fdo->BusKnown || (fdo->WithdrawOk && !fdo->DeclareTried)) {
        decision = UasWakeDecide(fdo->BusKnown, fdo->WithdrawOk,
                                 fdo->DeclareTried, 0, 0, 0, 0, 0);
    } else {
        for (;;) {
            status = uasResumeRequest(fdo, XHCI98_RESUME_OP_QUERY, 0, &rs);
            decision = UasWakeDecide(1, fdo->WithdrawOk, fdo->DeclareTried,
                                     NT_SUCCESS(status) ? 1UL : 0UL,
                                     rs.State, rs.Epoch, fdo->VerifiedEpoch,
                                     uasDeadlineLeft(&deadline) == 0 ? 1UL
                                                                     : 0UL);
            if (decision != UAS_WAKE_WAIT) {
                break;
            }
            uasSleepMs(UAS_POWER_QUERY_MS);
        }
    }
    /* The bus has decided: what the check sends, and what the class
     * driver asked of recovery meanwhile, may go now. */
    uasUnseal(fdo);
    switch (decision) {
    case UAS_WAKE_RELEASE:
        report = XHCI98_RESUME_RPT_RELEASED;
        break;
    case UAS_WAKE_SAME:
        report = XHCI98_RESUME_RPT_SAME;
        break;
    case UAS_WAKE_CHECK:
        if (uasMediumCheck(fdo, &deadline)) {
            fdo->VerifiedEpoch = rs.Epoch;
            report = XHCI98_RESUME_RPT_VERIFIED;
        } else {
            report = XHCI98_RESUME_RPT_FAILED;
        }
        break;
    default:
        report = XHCI98_RESUME_RPT_FAILED;
        break;
    }
    /* The declaration ends with the hold it was made under: one the bus
     * never used - a sleep that ended with no controller suspend, so no
     * resume voided it - would otherwise let a later re-initialization
     * with no sleep phase of this driver (a controller's power-down of
     * its own) keep the drive with no hold and no wake check. Withdrawn
     * before the release; refused, the drive is failed rather than
     * released with the declaration standing. */
    if (fdo->BusKnown && fdo->DeclareTried &&
        report != XHCI98_RESUME_RPT_FAILED &&
        !NT_SUCCESS(uasResumeRequest(fdo, XHCI98_RESUME_OP_WITHDRAW,
                                     fdo->Episode & 0x00FFFFFFUL, &rs))) {
        report = XHCI98_RESUME_RPT_FAILED;
    }

    cycle = FALSE;
    KeAcquireSpinLock(&fdo->Lock, &irql);
    if (report == XHCI98_RESUME_RPT_FAILED) {
        fdo->Dead = TRUE;
        cycle = (BOOLEAN)(fdo->Started && !fdo->Gone);
    }
    fdo->Holding = FALSE;
    fdo->HoldCheck = FALSE;
    KeReleaseSpinLock(&fdo->Lock, irql);
    uasPump(fdo);
    if (fdo->BusKnown) {
        spent = UAS_POWER_PHASE_MS - uasDeadlineLeft(&deadline);
        (VOID)uasResumeRequest(fdo, XHCI98_RESUME_OP_REPORT,
                               (report << 16) | ((spent / 100UL) & 0xFFFFUL),
                               &rs);
    }
    if (cycle) {
        (VOID)UasSyncIoctl(fdo, IOCTL_INTERNAL_USB_CYCLE_PORT);
    }
    uasPowerUnlock(fdo);
    uasDeref(fdo);
}
