/*
 * uas_pdo.c - one PDO per logical unit: its PnP ids, the SRBs the storage
 * class layer sends it, the two storage IOCTLs that layer asks first, and a
 * program's SCSI and ATA pass-through and SMART requests (task 45.52).
 *
 * THE IDS (uas_iu.c, UasBuildId, from the LUN's INQUIRY data). The device
 * id is this driver's own enumerator, XHCIUAS\<Type>&Ven_&Prod_&Rev_, so a
 * unit is never confused with one usbstor.sys presented for the same device
 * under Bulk-Only: the two get separate devnodes, which is what 31-A.3's
 * transport switch through uninstall and re-plug relies on. The hardware ids
 * end in the generic names the class INFs bind - GenDisk (Windows 2000 and
 * later, disk.inf; GenCdRom for cdrom.inf) and USBSTOR\GenDisk (NUSB's
 * usbntmap.inf on Windows 98 SE, which gives the unit DevLoader *IOS and
 * the USB mapping port driver USBMPHLP.PDR; roadmap-hcd.md decisions table,
 * "Mass storage on Windows 98 SE"). The instance id is the device's serial
 * string and the LUN; a device with no serial reports UniqueID FALSE.
 *
 * THE SRBs. SRB_FUNCTION_EXECUTE_SCSI goes to the engine (uas_xport.c) with
 * its CDB unchanged - READ and WRITE (10) and (16), INQUIRY, READ CAPACITY
 * (10) and (16), TEST UNIT READY, MODE SENSE, REQUEST SENSE, SYNCHRONIZE
 * CACHE, START STOP UNIT and the rest all travel in a COMMAND IU as they
 * came. The queue is never frozen: a failed SRB completes with autosense and
 * without SRB_STATUS_QUEUE_FROZEN, so RELEASE_QUEUE has nothing to do;
 * FLUSH_QUEUE completes the LUN's queued requests SRB_STATUS_REQUEST_FLUSHED.
 * LOCK_QUEUE and UNLOCK_QUEUE hold back every SRB of the LUN not flagged
 * SRB_FLAGS_BYPASS_LOCKED_QUEUE (the class driver's power path).
 * CLAIM_DEVICE and RELEASE_DEVICE keep one claimant. ABORT_COMMAND takes a
 * queued victim off the queue, or holds the abort SRB while recovery sends
 * ABORT TASK for one in flight; the reset SRBs are held while recovery
 * sends LOGICAL UNIT RESET (or resets the port). Each completes with its
 * outcome, not before it.
 *
 * PASS-THROUGH (task 45.52, design record 13 section 16). The disk class
 * driver passes a program's IOCTL_SCSI_PASS_THROUGH(_DIRECT) and
 * IOCTL_ATA_PASS_THROUGH(_DIRECT) down unchanged, and sends the SMART IOCTLs
 * as IOCTL_SCSI_MINIPORT with the SCSIDISK signature. Each is checked by the
 * pure core (uas_iu.c, UasPtPlan, UasSiocCheck, UasSmartPlan) and then runs
 * as a class SRB does: the caller's own IRP queued with a record's SRB
 * (UAS_PCMD), under the same admission, queue lock, sleep hold and
 * recovery, with a cancel routine while it waits; the ATA and SMART forms as
 * ATA PASS-THROUGH (16). UasPdoPassFinish writes the outcome back. No
 * access check of its own: the I/O manager has held the code's required
 * access to the handle (16.3, the owner's choice B).
 *
 * LIFETIME. STOP, SURPRISE_REMOVAL and REMOVE close the LUN's admission and
 * wait for its requests to finish (UasEngineCloseLun). The SCSI and device
 * control dispatches run inside pdo->Busy, which the parent's removal waits
 * out before the parent goes (uas_fdo.c); PnP IRPs reach a PDO only while
 * its parent is there, PnP sending the parent's REMOVE last.
 *
 * IRQL: UasPdoCreate and PnP and power at PASSIVE_LEVEL; SCSI and device
 * control at <= DISPATCH_LEVEL.
 */

#include "uas.h"

#ifndef SRB_FUNCTION_RESET_LOGICAL_UNIT
#define SRB_FUNCTION_RESET_LOGICAL_UNIT 0x20
#endif

/* Task 45.52: the ATA pass-through codes are not in the Windows 2000 DDK's
 * ntddscsi.h; WDK 7.1's values. */
#ifndef IOCTL_ATA_PASS_THROUGH
#define IOCTL_ATA_PASS_THROUGH          CTL_CODE(IOCTL_SCSI_BASE, 0x040b, \
                                                 METHOD_BUFFERED,         \
                                                 FILE_READ_ACCESS |       \
                                                 FILE_WRITE_ACCESS)
#endif
#ifndef IOCTL_ATA_PASS_THROUGH_DIRECT
#define IOCTL_ATA_PASS_THROUGH_DIRECT   CTL_CODE(IOCTL_SCSI_BASE, 0x040c, \
                                                 METHOD_BUFFERED,         \
                                                 FILE_READ_ACCESS |       \
                                                 FILE_WRITE_ACCESS)
#endif

/* The pure core reads the headers at byte offsets (uas_iu.h); these hold
 * those offsets to the DDK's own declarations wherever its headers carry
 * them: SCSI pass-through in both, ATA pass-through and the _32 forms in
 * WDK 7.1's amd64 headers. */
XHCI_C_ASSERT(uas_ioctl_ata, IOCTL_ATA_PASS_THROUGH == 0x4D02CUL &&
                             IOCTL_ATA_PASS_THROUGH_DIRECT == 0x4D030UL);
#if defined(_WIN64)
XHCI_C_ASSERT(uas_spt64, sizeof(SCSI_PASS_THROUGH) == 56 &&
              FIELD_OFFSET(SCSI_PASS_THROUGH, DataBufferOffset) == 24 &&
              FIELD_OFFSET(SCSI_PASS_THROUGH, SenseInfoOffset) == 32 &&
              FIELD_OFFSET(SCSI_PASS_THROUGH, Cdb) == 36);
XHCI_C_ASSERT(uas_sptd64, sizeof(SCSI_PASS_THROUGH_DIRECT) == 56 &&
              FIELD_OFFSET(SCSI_PASS_THROUGH_DIRECT, DataBuffer) == 24);
XHCI_C_ASSERT(uas_spt32, sizeof(SCSI_PASS_THROUGH32) == 44 &&
              FIELD_OFFSET(SCSI_PASS_THROUGH32, DataBufferOffset) == 20 &&
              FIELD_OFFSET(SCSI_PASS_THROUGH32, SenseInfoOffset) == 24 &&
              FIELD_OFFSET(SCSI_PASS_THROUGH32, Cdb) == 28);
XHCI_C_ASSERT(uas_sptd32, sizeof(SCSI_PASS_THROUGH_DIRECT32) == 44 &&
              FIELD_OFFSET(SCSI_PASS_THROUGH_DIRECT32, DataBuffer) == 20);
XHCI_C_ASSERT(uas_apt64, sizeof(ATA_PASS_THROUGH_EX) == 48 &&
              FIELD_OFFSET(ATA_PASS_THROUGH_EX, AtaFlags) == 2 &&
              FIELD_OFFSET(ATA_PASS_THROUGH_EX, DataTransferLength) == 8 &&
              FIELD_OFFSET(ATA_PASS_THROUGH_EX, TimeOutValue) == 12 &&
              FIELD_OFFSET(ATA_PASS_THROUGH_EX, DataBufferOffset) == 24 &&
              FIELD_OFFSET(ATA_PASS_THROUGH_EX, PreviousTaskFile) == 32 &&
              FIELD_OFFSET(ATA_PASS_THROUGH_EX, CurrentTaskFile) == 40);
XHCI_C_ASSERT(uas_aptd64, sizeof(ATA_PASS_THROUGH_DIRECT) == 48 &&
              FIELD_OFFSET(ATA_PASS_THROUGH_DIRECT, DataBuffer) == 24);
XHCI_C_ASSERT(uas_apt32, sizeof(ATA_PASS_THROUGH_EX32) == 40 &&
              FIELD_OFFSET(ATA_PASS_THROUGH_EX32, DataBufferOffset) == 20 &&
              FIELD_OFFSET(ATA_PASS_THROUGH_EX32, PreviousTaskFile) == 24 &&
              FIELD_OFFSET(ATA_PASS_THROUGH_EX32, CurrentTaskFile) == 32);
XHCI_C_ASSERT(uas_aptd32, sizeof(ATA_PASS_THROUGH_DIRECT32) == 40 &&
              FIELD_OFFSET(ATA_PASS_THROUGH_DIRECT32, DataBuffer) == 20);
#else
XHCI_C_ASSERT(uas_spt32x86, sizeof(SCSI_PASS_THROUGH) == 44 &&
              FIELD_OFFSET(SCSI_PASS_THROUGH, ScsiStatus) == 2 &&
              FIELD_OFFSET(SCSI_PASS_THROUGH, CdbLength) == 6 &&
              FIELD_OFFSET(SCSI_PASS_THROUGH, SenseInfoLength) == 7 &&
              FIELD_OFFSET(SCSI_PASS_THROUGH, DataIn) == 8 &&
              FIELD_OFFSET(SCSI_PASS_THROUGH, DataTransferLength) == 12 &&
              FIELD_OFFSET(SCSI_PASS_THROUGH, TimeOutValue) == 16 &&
              FIELD_OFFSET(SCSI_PASS_THROUGH, DataBufferOffset) == 20 &&
              FIELD_OFFSET(SCSI_PASS_THROUGH, SenseInfoOffset) == 24 &&
              FIELD_OFFSET(SCSI_PASS_THROUGH, Cdb) == 28);
XHCI_C_ASSERT(uas_sptd32x86, sizeof(SCSI_PASS_THROUGH_DIRECT) == 44 &&
              FIELD_OFFSET(SCSI_PASS_THROUGH_DIRECT, DataBuffer) == 20);
#endif

#define UAS_ID_BUFFER   512

static LONG uasPdoSerial;

NTSTATUS UasPdoCreate(PUAS_FDO fdo, ULONG lun, const UCHAR *inquiry,
                      PUAS_PDO *made)
{
    static const char prefix[] = "\\Device\\XHCIUAS";
    WCHAR nameBuffer[32];
    UNICODE_STRING name;
    PDEVICE_OBJECT self;
    PUAS_PDO pdo;
    NTSTATUS status;
    ULONG serial;
    ULONG n;
    ULONG i;
    char digits[10];
    ULONG d;

    *made = NULL;
    serial = (ULONG)InterlockedIncrement(&uasPdoSerial);
    n = 0;
    for (i = 0; prefix[i] != 0; i++) {
        nameBuffer[n++] = (WCHAR)prefix[i];
    }
    d = 0;
    do {
        digits[d++] = (char)('0' + serial % 10);
        serial /= 10;
    } while (serial != 0 && d < sizeof(digits));
    while (d > 0) {
        nameBuffer[n++] = (WCHAR)digits[--d];
    }
    nameBuffer[n] = 0;
    RtlInitUnicodeString(&name, nameBuffer);

    status = IoCreateDevice(UasDriverObject, sizeof(UAS_PDO), &name,
                            FILE_DEVICE_MASS_STORAGE, 0, FALSE, &self);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    pdo = (PUAS_PDO)self->DeviceExtension;
    UasZero(pdo, sizeof(UAS_PDO));
    pdo->Kind = UAS_KIND_PDO;
    pdo->Self = self;
    pdo->Fdo = fdo;
    pdo->Lun = lun;
    pdo->Present = TRUE;
    pdo->Busy = 1;
    KeInitializeEvent(&pdo->BusyIdle, NotificationEvent, FALSE);
    KeInitializeEvent(&pdo->RequestsIdle, NotificationEvent, TRUE);
    KeInitializeEvent(&pdo->CloseDone, NotificationEvent, TRUE);
    UasCopy(pdo->Inquiry, inquiry, UAS_INQUIRY_LENGTH);
    self->Flags |= DO_DIRECT_IO | DO_POWER_PAGABLE;
    self->Flags &= ~DO_DEVICE_INITIALIZING;
    *made = pdo;
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ */
/* PnP                                                                */
/* ------------------------------------------------------------------ */

/* An id from uas_iu.c, widened into a pool buffer PnP frees. */
static NTSTATUS uasQueryId(PUAS_PDO pdo, ULONG kind, PIRP irp)
{
    char *ascii;
    PWCHAR wide;
    ULONG length;
    ULONG i;
    const char *serial;

    ascii = (char *)UasAlloc(UAS_ID_BUFFER);
    if (ascii == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    serial = (pdo->Fdo != NULL) ? pdo->Fdo->Serial : NULL;
    length = UasBuildId(kind, pdo->Inquiry, UAS_INQUIRY_LENGTH, serial,
                        pdo->Lun, ascii, UAS_ID_BUFFER);
    if (length == 0) {
        UasFree(ascii);
        return STATUS_UNSUCCESSFUL;
    }
    wide = (PWCHAR)UasAlloc(length * sizeof(WCHAR));
    if (wide == NULL) {
        UasFree(ascii);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    for (i = 0; i < length; i++) {
        wide[i] = (WCHAR)(UCHAR)ascii[i];
    }
    UasFree(ascii);
    irp->IoStatus.Information = (ULONG_PTR)wide;
    return STATUS_SUCCESS;
}

/*
 * A REMOVE of a LUN its parent no longer reports (a start's REPORT LUNS no
 * longer listed it, uas_fdo.c): PnP will never see the object again, so it
 * goes now rather than at the parent's removal, which would otherwise
 * report it again if the LUN came back. Off the parent's list under its
 * lock (the worker reads the list there) and the dispatches that hold Busy
 * waited out, all before the IRP is completed: until then PnP cannot send
 * the parent its own REMOVE, so the parent is still whole here. The object
 * itself is deleted after the completion, by the caller, with nothing of
 * the parent touched. IRQL: PASSIVE_LEVEL.
 */
static VOID uasPdoUnlink(PUAS_PDO pdo)
{
    PUAS_FDO fdo;
    KIRQL irql;
    ULONG i;

    fdo = pdo->Fdo;
    KeAcquireSpinLock(&fdo->Lock, &irql);
    for (i = 0; i < fdo->LunCount; i++) {
        if (fdo->Luns[i] == pdo) {
            fdo->LunCount--;
            fdo->Luns[i] = fdo->Luns[fdo->LunCount];
            fdo->Luns[fdo->LunCount] = NULL;
            break;
        }
    }
    KeReleaseSpinLock(&fdo->Lock, irql);
    pdo->Gone = TRUE;
    if (InterlockedDecrement(&pdo->Busy) != 0) {
        UasWaitEvent(&pdo->BusyIdle);
    }
    pdo->Fdo = NULL;
}

static NTSTATUS uasCapabilities(PUAS_PDO pdo, PDEVICE_CAPABILITIES caps)
{
    ULONG i;

    if (caps->Version != 1 || caps->Size < sizeof(DEVICE_CAPABILITIES)) {
        return STATUS_UNSUCCESSFUL;
    }
    caps->LockSupported = FALSE;
    caps->EjectSupported = FALSE;
    caps->Removable = FALSE;
    caps->DockDevice = FALSE;
    caps->UniqueID = (BOOLEAN)(pdo->Fdo != NULL &&
                               pdo->Fdo->Serial[0] != 0);
    /* Always FALSE: a LUN is a disk, not a USB interface, so the silent
     * install (task 38.2; design record 13 section 6.11.2) never applies. */
    caps->SilentInstall = FALSE;
    caps->RawDeviceOK = FALSE;
    caps->SurpriseRemovalOK = TRUE;
    caps->DeviceD1 = FALSE;
    caps->DeviceD2 = FALSE;
    caps->WakeFromD0 = FALSE;
    caps->WakeFromD1 = FALSE;
    caps->WakeFromD2 = FALSE;
    caps->WakeFromD3 = FALSE;
    caps->Address = pdo->Lun;
    caps->UINumber = pdo->Lun;
    caps->DeviceState[PowerSystemWorking] = PowerDeviceD0;
    for (i = PowerSystemSleeping1; i < PowerSystemMaximum; i++) {
        caps->DeviceState[i] = PowerDeviceD3;
    }
    caps->SystemWake = PowerSystemUnspecified;
    caps->DeviceWake = PowerDeviceUnspecified;
    return STATUS_SUCCESS;
}

NTSTATUS UasPdoPnp(PUAS_PDO pdo, PIRP irp)
{
    PIO_STACK_LOCATION stack;
    PDEVICE_RELATIONS rel;
    NTSTATUS status;
    BOOLEAN deleteIt;
    PDEVICE_OBJECT self;

    stack = IoGetCurrentIrpStackLocation(irp);
    status = irp->IoStatus.Status;
    deleteIt = FALSE;
    self = pdo->Self;
    switch (stack->MinorFunction) {
    case IRP_MN_START_DEVICE:
        if (pdo->Fdo == NULL) {
            status = STATUS_NO_SUCH_DEVICE;
            break;
        }
        pdo->Started = TRUE;
        pdo->Removed = FALSE;
        UasEngineOpenLun(pdo->Fdo, pdo);
        status = STATUS_SUCCESS;
        break;

    case IRP_MN_QUERY_STOP_DEVICE:
    case IRP_MN_CANCEL_STOP_DEVICE:
    case IRP_MN_QUERY_REMOVE_DEVICE:
    case IRP_MN_CANCEL_REMOVE_DEVICE:
        status = STATUS_SUCCESS;
        break;

    case IRP_MN_STOP_DEVICE:
        pdo->Started = FALSE;
        if (pdo->Fdo != NULL) {
            UasEngineCloseLun(pdo->Fdo, pdo, SRB_STATUS_REQUEST_FLUSHED);
        }
        status = STATUS_SUCCESS;
        break;

    case IRP_MN_SURPRISE_REMOVAL:
        pdo->Started = FALSE;
        if (pdo->Fdo != NULL) {
            UasEngineCloseLun(pdo->Fdo, pdo, SRB_STATUS_NO_DEVICE);
        }
        status = STATUS_SUCCESS;
        break;

    case IRP_MN_REMOVE_DEVICE:
        /* Admission closed and the LUN's requests all finished before the
         * queue lock and the claim are cleared. The object stays until the
         * FDO is removed: the unit is still on the bus while the FDO reports
         * it, and PnP may start it again. One the FDO no longer reports
         * is unlinked here and deleted once the IRP is completed. */
        pdo->Started = FALSE;
        if (pdo->Fdo != NULL) {
            UasEngineCloseLun(pdo->Fdo, pdo, SRB_STATUS_NO_DEVICE);
        }
        pdo->Removed = TRUE;
        pdo->QueueLocked = FALSE;
        pdo->Claimed = FALSE;
        deleteIt = (BOOLEAN)(!pdo->Present && pdo->Fdo != NULL);
        if (deleteIt) {
            uasPdoUnlink(pdo);
        }
        status = STATUS_SUCCESS;
        break;

    case IRP_MN_QUERY_ID:
        switch (stack->Parameters.QueryId.IdType) {
        case BusQueryDeviceID:
            status = uasQueryId(pdo, UAS_ID_DEVICE, irp);
            break;
        case BusQueryHardwareIDs:
            status = uasQueryId(pdo, UAS_ID_HARDWARE, irp);
            break;
        case BusQueryCompatibleIDs:
            status = uasQueryId(pdo, UAS_ID_COMPATIBLE, irp);
            break;
        case BusQueryInstanceID:
            status = uasQueryId(pdo, UAS_ID_INSTANCE, irp);
            break;
        default:
            break;
        }
        break;

    case IRP_MN_QUERY_DEVICE_TEXT:
        if (stack->Parameters.QueryDeviceText.DeviceTextType ==
            DeviceTextDescription) {
            status = uasQueryId(pdo, UAS_ID_TEXT, irp);
        }
        break;

    case IRP_MN_QUERY_CAPABILITIES:
        status = uasCapabilities(
            pdo, stack->Parameters.DeviceCapabilities.Capabilities);
        break;

    case IRP_MN_QUERY_DEVICE_RELATIONS:
        if (stack->Parameters.QueryDeviceRelations.Type ==
            TargetDeviceRelation) {
            rel = (PDEVICE_RELATIONS)UasAlloc(sizeof(DEVICE_RELATIONS));
            if (rel == NULL) {
                status = STATUS_INSUFFICIENT_RESOURCES;
                break;
            }
            ObReferenceObject(pdo->Self);
            rel->Count = 1;
            rel->Objects[0] = pdo->Self;
            irp->IoStatus.Information = (ULONG_PTR)rel;
            status = STATUS_SUCCESS;
        }
        break;

    default:
        break;
    }
    irp->IoStatus.Status = status;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    if (deleteIt) {
        IoDeleteDevice(self);
    }
    return status;
}

/* The unit has no power of its own to manage: the device's is the bus's,
 * and the class driver above sends START STOP UNIT itself. */
NTSTATUS UasPdoPower(PUAS_PDO pdo, PIRP irp)
{
    PIO_STACK_LOCATION stack;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(pdo);
    stack = IoGetCurrentIrpStackLocation(irp);
    status = irp->IoStatus.Status;
    if (stack->MinorFunction == IRP_MN_SET_POWER ||
        stack->MinorFunction == IRP_MN_QUERY_POWER) {
        status = STATUS_SUCCESS;
    }
    PoStartNextPowerIrp(irp);
    irp->IoStatus.Status = status;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
}

/* ------------------------------------------------------------------ */
/* SRBs                                                               */
/* ------------------------------------------------------------------ */

static NTSTATUS uasSrbDone(PIRP irp, PSCSI_REQUEST_BLOCK srb, UCHAR srbStatus)
{
    NTSTATUS status;

    srb->SrbStatus = srbStatus;
    if (srbStatus == SRB_STATUS_SUCCESS) {
        status = STATUS_SUCCESS;
    } else if (srbStatus == SRB_STATUS_BUSY) {
        status = STATUS_DEVICE_BUSY;
    } else if (srbStatus == SRB_STATUS_NO_DEVICE) {
        status = STATUS_NO_SUCH_DEVICE;
    } else {
        status = STATUS_INVALID_DEVICE_REQUEST;
    }
    return UasCompleteIrp(irp, status, 0);
}

/* The parent rundown: a dispatch that will read pdo->Fdo enters, and the
 * parent's removal sets Gone and waits for the last to leave. */
static BOOLEAN uasPdoEnter(PUAS_PDO pdo)
{
    (VOID)InterlockedIncrement(&pdo->Busy);
    if (pdo->Gone || pdo->Fdo == NULL) {
        if (InterlockedDecrement(&pdo->Busy) == 0) {
            KeSetEvent(&pdo->BusyIdle, IO_NO_INCREMENT, FALSE);
        }
        return FALSE;
    }
    return TRUE;
}

static VOID uasPdoLeave(PUAS_PDO pdo)
{
    if (InterlockedDecrement(&pdo->Busy) == 0) {
        KeSetEvent(&pdo->BusyIdle, IO_NO_INCREMENT, FALSE);
    }
}

static NTSTATUS uasPdoScsiInside(PUAS_PDO pdo, PUAS_FDO fdo, PIRP irp,
                                 PSCSI_REQUEST_BLOCK srb);
static NTSTATUS uasPassDispatch(PUAS_PDO pdo, PIRP irp);

NTSTATUS UasPdoScsi(PUAS_PDO pdo, PIRP irp)
{
    PIO_STACK_LOCATION stack;
    PSCSI_REQUEST_BLOCK srb;
    NTSTATUS status;

    stack = IoGetCurrentIrpStackLocation(irp);
    srb = stack->Parameters.Scsi.Srb;
    if (srb == NULL) {
        return UasCompleteIrp(irp, STATUS_INVALID_PARAMETER, 0);
    }
    if (!uasPdoEnter(pdo)) {
        return uasSrbDone(irp, srb, SRB_STATUS_NO_DEVICE);
    }
    if (!pdo->Present) {
        status = uasSrbDone(irp, srb, SRB_STATUS_NO_DEVICE);
    } else {
        status = uasPdoScsiInside(pdo, pdo->Fdo, irp, srb);
    }
    uasPdoLeave(pdo);
    return status;
}

static NTSTATUS uasPdoScsiInside(PUAS_PDO pdo, PUAS_FDO fdo, PIRP irp,
                                 PSCSI_REQUEST_BLOCK srb)
{
    UCHAR result;
    KIRQL irql;

    switch (srb->Function) {
    case SRB_FUNCTION_EXECUTE_SCSI:
        return UasEngineSubmit(fdo, pdo, irp, srb);

    case SRB_FUNCTION_CLAIM_DEVICE:
        KeAcquireSpinLock(&fdo->Lock, &irql);
        if (pdo->Claimed) {
            result = SRB_STATUS_BUSY;
        } else {
            pdo->Claimed = TRUE;
            srb->DataBuffer = pdo->Self;
            result = SRB_STATUS_SUCCESS;
        }
        KeReleaseSpinLock(&fdo->Lock, irql);
        return uasSrbDone(irp, srb, result);

    case SRB_FUNCTION_RELEASE_DEVICE:
        KeAcquireSpinLock(&fdo->Lock, &irql);
        pdo->Claimed = FALSE;
        KeReleaseSpinLock(&fdo->Lock, irql);
        return uasSrbDone(irp, srb, SRB_STATUS_SUCCESS);

    case SRB_FUNCTION_LOCK_QUEUE:
        KeAcquireSpinLock(&fdo->Lock, &irql);
        pdo->QueueLocked = TRUE;
        KeReleaseSpinLock(&fdo->Lock, irql);
        return uasSrbDone(irp, srb, SRB_STATUS_SUCCESS);

    case SRB_FUNCTION_UNLOCK_QUEUE:
        KeAcquireSpinLock(&fdo->Lock, &irql);
        pdo->QueueLocked = FALSE;
        KeReleaseSpinLock(&fdo->Lock, irql);
        UasEngineUnlockQueue(fdo);
        return uasSrbDone(irp, srb, SRB_STATUS_SUCCESS);

    case SRB_FUNCTION_RELEASE_QUEUE:
    case SRB_FUNCTION_FLUSH:
    case SRB_FUNCTION_SHUTDOWN:
        return uasSrbDone(irp, srb, SRB_STATUS_SUCCESS);

    case SRB_FUNCTION_FLUSH_QUEUE:
        return UasEngineFlush(fdo, pdo, irp, srb);

    case SRB_FUNCTION_ABORT_COMMAND:
        return UasEngineAbort(fdo, pdo, irp, srb);

    case SRB_FUNCTION_RESET_DEVICE:
    case SRB_FUNCTION_RESET_LOGICAL_UNIT:
    case SRB_FUNCTION_RESET_BUS:
        return UasEngineReset(fdo, pdo, irp, srb);

    default:
        return uasSrbDone(irp, srb, SRB_STATUS_INVALID_REQUEST);
    }
}

/* ------------------------------------------------------------------ */
/* Device control                                                     */
/* ------------------------------------------------------------------ */

static ULONG uasPutString(PUCHAR base, ULONG at, const UCHAR *s, ULONG n)
{
    ULONG i;

    for (i = 0; i < n; i++) {
        base[at + i] = (s[i] >= 0x20 && s[i] < 0x7F) ? s[i] : ' ';
    }
    base[at + n] = 0;
    return at + n + 1;
}

/*
 * StorageDeviceProperty: the descriptor with the INQUIRY data as its raw
 * properties and the vendor, product, revision and serial strings after
 * them. Built whole in pool, then as much copied out as the caller's buffer
 * holds - the class driver asks first with room for the header alone.
 */
static NTSTATUS uasDeviceProperty(PUAS_PDO pdo, PVOID out, ULONG outLength,
                                  PULONG written)
{
    PSTORAGE_DEVICE_DESCRIPTOR d;
    PUCHAR b;
    ULONG serialLength;
    ULONG size;
    ULONG at;
    const char *serial;

    serial = (pdo->Fdo != NULL) ? pdo->Fdo->Serial : "";
    for (serialLength = 0; serial[serialLength] != 0; serialLength++) {
    }
    size = FIELD_OFFSET(STORAGE_DEVICE_DESCRIPTOR, RawDeviceProperties) +
           UAS_INQUIRY_LENGTH + 9 + 17 + 5 + serialLength + 1;
    d = (PSTORAGE_DEVICE_DESCRIPTOR)UasAlloc(size);
    if (d == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    b = (PUCHAR)d;
    d->Version = sizeof(STORAGE_DEVICE_DESCRIPTOR);
    d->Size = size;
    d->DeviceType = (UCHAR)(pdo->Inquiry[0] & 0x1F);
    d->DeviceTypeModifier = (UCHAR)(pdo->Inquiry[1] & 0x7F);
    d->RemovableMedia = (BOOLEAN)((pdo->Inquiry[1] & 0x80) != 0);
    d->CommandQueueing = (BOOLEAN)(pdo->Fdo != NULL && pdo->Fdo->Streamed);
    d->BusType = BusTypeUsb;
    d->RawPropertiesLength = UAS_INQUIRY_LENGTH;
    at = FIELD_OFFSET(STORAGE_DEVICE_DESCRIPTOR, RawDeviceProperties);
    UasCopy(b + at, pdo->Inquiry, UAS_INQUIRY_LENGTH);
    at += UAS_INQUIRY_LENGTH;
    d->VendorIdOffset = at;
    at = uasPutString(b, at, pdo->Inquiry + 8, 8);
    d->ProductIdOffset = at;
    at = uasPutString(b, at, pdo->Inquiry + 16, 16);
    d->ProductRevisionOffset = at;
    at = uasPutString(b, at, pdo->Inquiry + 32, 4);
    if (serialLength != 0) {
        d->SerialNumberOffset = at;
        at = uasPutString(b, at, (const UCHAR *)serial, serialLength);
    } else {
        d->SerialNumberOffset = 0;
    }
    if (outLength > size) {
        outLength = size;
    }
    UasCopy(out, d, outLength);
    *written = outLength;
    UasFree(d);
    return STATUS_SUCCESS;
}

static NTSTATUS uasAdapterProperty(PUAS_PDO pdo, PVOID out, ULONG outLength,
                                   PULONG written)
{
    STORAGE_ADAPTER_DESCRIPTOR a;
    ULONG bytes;

    /* Task 36.22: the one value the start stored (uas_fdo.c,
     * uasAskPolicy); 64 KiB when the parent is going. */
    bytes = (pdo->Fdo != NULL) ? pdo->Fdo->CommandBytes : UAS_MAX_TRANSFER;
    UasZero(&a, sizeof(a));
    a.Version = sizeof(STORAGE_ADAPTER_DESCRIPTOR);
    a.Size = sizeof(STORAGE_ADAPTER_DESCRIPTOR);
    a.MaximumTransferLength = bytes;
    a.MaximumPhysicalPages = bytes / PAGE_SIZE + 1;
    a.AlignmentMask = 0;
    a.AdapterUsesPio = FALSE;
    a.AdapterScansDown = FALSE;
    a.CommandQueueing = (BOOLEAN)(pdo->Fdo != NULL && pdo->Fdo->Streamed);
    a.AcceleratedTransfer = FALSE;
    a.BusType = (UCHAR)BusTypeUsb;
    a.BusMajorVersion = 2;
    a.BusMinorVersion = 0;
    if (outLength > sizeof(a)) {
        outLength = sizeof(a);
    }
    UasCopy(out, &a, outLength);
    *written = outLength;
    return STATUS_SUCCESS;
}

NTSTATUS UasPdoDeviceControl(PUAS_PDO pdo, PIRP irp)
{
    PIO_STACK_LOCATION stack;
    PSTORAGE_PROPERTY_QUERY query;
    PSCSI_ADDRESS address;
    STORAGE_PROPERTY_ID id;
    STORAGE_QUERY_TYPE type;
    ULONG inLength;
    ULONG outLength;
    ULONG written;
    NTSTATUS status;

    stack = IoGetCurrentIrpStackLocation(irp);
    inLength = stack->Parameters.DeviceIoControl.InputBufferLength;
    outLength = stack->Parameters.DeviceIoControl.OutputBufferLength;
    written = 0;
    if (!uasPdoEnter(pdo)) {
        return UasCompleteIrp(irp, STATUS_NO_SUCH_DEVICE, 0);
    }
    if (!pdo->Present) {
        uasPdoLeave(pdo);
        return UasCompleteIrp(irp, STATUS_NO_SUCH_DEVICE, 0);
    }

    switch (stack->Parameters.DeviceIoControl.IoControlCode) {
    case IOCTL_STORAGE_QUERY_PROPERTY:
        if (inLength < FIELD_OFFSET(STORAGE_PROPERTY_QUERY,
                                    AdditionalParameters)) {
            status = STATUS_INFO_LENGTH_MISMATCH;
            break;
        }
        /* METHOD_BUFFERED: in and out share the buffer, so the query is
         * read before anything is written. */
        query = (PSTORAGE_PROPERTY_QUERY)irp->AssociatedIrp.SystemBuffer;
        id = query->PropertyId;
        type = query->QueryType;
        if (id != StorageDeviceProperty && id != StorageAdapterProperty) {
            status = STATUS_NOT_SUPPORTED;
            break;
        }
        if (type == PropertyExistsQuery) {
            status = STATUS_SUCCESS;
            break;
        }
        if (type != PropertyStandardQuery) {
            status = STATUS_NOT_SUPPORTED;
            break;
        }
        if (outLength < sizeof(STORAGE_DESCRIPTOR_HEADER)) {
            status = STATUS_INFO_LENGTH_MISMATCH;
            break;
        }
        if (id == StorageDeviceProperty) {
            status = uasDeviceProperty(pdo, irp->AssociatedIrp.SystemBuffer,
                                       outLength, &written);
        } else {
            status = uasAdapterProperty(pdo, irp->AssociatedIrp.SystemBuffer,
                                        outLength, &written);
        }
        break;

    case IOCTL_SCSI_GET_ADDRESS:
        if (outLength < sizeof(SCSI_ADDRESS)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        address = (PSCSI_ADDRESS)irp->AssociatedIrp.SystemBuffer;
        address->Length = sizeof(SCSI_ADDRESS);
        address->PortNumber = 0;
        address->PathId = 0;
        address->TargetId = 0;
        address->Lun = (UCHAR)pdo->Lun;
        written = sizeof(SCSI_ADDRESS);
        status = STATUS_SUCCESS;
        break;

    case IOCTL_SCSI_PASS_THROUGH:
    case IOCTL_SCSI_PASS_THROUGH_DIRECT:
    case IOCTL_ATA_PASS_THROUGH:
    case IOCTL_ATA_PASS_THROUGH_DIRECT:
    case IOCTL_SCSI_MINIPORT:
        /* Task 45.52: completed by the helper or by the finish. */
        status = uasPassDispatch(pdo, irp);
        uasPdoLeave(pdo);
        return status;

    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }
    status = UasCompleteIrp(irp, status, written);
    uasPdoLeave(pdo);
    return status;
}

/* ------------------------------------------------------------------ */
/* Task 45.52: pass-through and SMART (design record 13 section 16)    */
/* ------------------------------------------------------------------ */

static NTSTATUS uasPassStatus(ULONG result)
{
    switch (result) {
    case UAS_PTR_SUCCESS:
        return STATUS_SUCCESS;
    case UAS_PTR_TIMEOUT:
        return STATUS_IO_TIMEOUT;
    case UAS_PTR_NO_DEVICE:
        return STATUS_NO_SUCH_DEVICE;
    case UAS_PTR_BUSY:
        return STATUS_DEVICE_BUSY;
    case UAS_PTR_INVALID:
        return STATUS_INVALID_PARAMETER;
    case UAS_PTR_NOT_SUPPORTED:
        return STATUS_NOT_SUPPORTED;
    default:
        return STATUS_IO_DEVICE_ERROR;
    }
}

static NTSTATUS uasPlanStatus(ULONG check)
{
    switch (check) {
    case UAS_PT_OK:
        return STATUS_SUCCESS;
    case UAS_PT_TOO_SMALL:
        return STATUS_BUFFER_TOO_SMALL;
    case UAS_PT_UNKNOWN:
        return STATUS_INVALID_DEVICE_REQUEST;
    default:
        return STATUS_INVALID_PARAMETER;
    }
}

/* The record's SRB, as a class driver would build it for this command. */
static VOID uasPassSrb(PUAS_PCMD pc, ULONG lun, const UCHAR *cdb,
                       ULONG cdbLength, PVOID data, ULONG length, ULONG dir,
                       ULONG timeout)
{
    PSCSI_REQUEST_BLOCK srb;

    srb = &pc->Srb;
    srb->Length = sizeof(SCSI_REQUEST_BLOCK);
    srb->Function = SRB_FUNCTION_EXECUTE_SCSI;
    srb->Lun = (UCHAR)lun;
    srb->CdbLength = (UCHAR)cdbLength;
    UasCopy(srb->Cdb, cdb, cdbLength);
    srb->DataBuffer = data;
    srb->DataTransferLength = length;
    if (dir == UAS_DIR_IN) {
        srb->SrbFlags = SRB_FLAGS_DATA_IN;
    } else if (dir == UAS_DIR_OUT) {
        srb->SrbFlags = SRB_FLAGS_DATA_OUT;
    } else {
        srb->SrbFlags = SRB_FLAGS_NO_DATA_TRANSFER;
    }
    /* Autosense always: an ATA answer's registers come back as sense. */
    srb->SenseInfoBuffer = pc->Sense;
    srb->SenseInfoBufferLength = (UCHAR)UAS_PT_SENSE_MAX;
    srb->TimeOutValue = timeout;
    srb->OriginalRequest = pc->Irp;
}

/* A record's end before or without the engine: no count was taken. */
static NTSTATUS uasPassDrop(PUAS_PCMD pc, NTSTATUS status)
{
    PIRP irp;

    irp = pc->Irp;
    if (pc->UserMdl != NULL) {
        irp->MdlAddress = NULL;
        UasUnlockBuffer(pc->UserMdl);
    }
    UasFree(pc->Data);
    UasFree(pc);
    return UasCompleteIrp(irp, status, 0);
}

/*
 * SCSI and ATA pass-through, buffered and direct (16.4, 16.5). Inside the
 * PDO's dispatch rundown. The direct forms at PASSIVE_LEVEL only.
 */
static NTSTATUS uasPassThrough(PUAS_PDO pdo, PIRP irp, ULONG kind,
                               ULONG inLength, ULONG outLength)
{
    PUAS_FDO fdo;
    PUAS_PCMD pc;
    PUCHAR buf;
    PVOID data;
    ULONG layout;
    ULONG direct;
    ULONG check;
    ULONG_PTR address;
    NTSTATUS status;

    fdo = pdo->Fdo;
    buf = (PUCHAR)irp->AssociatedIrp.SystemBuffer;
    direct = kind == UAS_PT_SCSI_DIRECT || kind == UAS_PT_ATA_DIRECT;
    if (buf == NULL) {
        return UasCompleteIrp(irp, STATUS_INVALID_PARAMETER, 0);
    }
    if (direct && KeGetCurrentIrql() != PASSIVE_LEVEL) {
        return UasCompleteIrp(irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    }
    layout = UAS_PT_LAYOUT_32;
#if defined(_WIN64)
    /* Only an IRP the I/O manager built for a process can be asked; a
     * driver's own request is native. */
    if (irp->RequestorMode != UserMode || !IoIs32bitProcess(irp)) {
        layout = UAS_PT_LAYOUT_64;
    }
#endif
    pc = (PUAS_PCMD)UasAlloc(sizeof(UAS_PCMD));
    if (pc == NULL) {
        return UasCompleteIrp(irp, STATUS_INSUFFICIENT_RESOURCES, 0);
    }
    pc->Fdo = fdo;
    pc->Pdo = pdo;
    pc->Irp = irp;
    pc->Form = UAS_PCMD_PT;
    pc->OutLength = outLength;
    check = UasPtPlan(kind, layout, buf, inLength, outLength,
                      fdo->CommandBytes, &pc->Plan);
    if (check != UAS_PT_OK) {
        return uasPassDrop(pc, uasPlanStatus(check));
    }
    /* No pool byte the caller never wrote goes back (16.4). */
    if (outLength > inLength) {
        UasZero(buf + inLength, outLength - inLength);
    }
    data = NULL;
    if (pc->Plan.DataLength != 0 && direct) {
        if (irp->MdlAddress != NULL ||
            (irp->RequestorMode == UserMode &&
             (PVOID)irp->Tail.Overlay.Thread != (PVOID)KeGetCurrentThread())) {
            /* Not in the issuing thread: the address cannot be read in the
             * caller's space here. */
            return uasPassDrop(pc, STATUS_INVALID_DEVICE_REQUEST);
        }
#if defined(_WIN64)
        address = ((ULONG_PTR)pc->Plan.DataPtrHi << 32) |
                  (ULONG_PTR)pc->Plan.DataPtrLo;
#else
        address = (ULONG_PTR)pc->Plan.DataPtrLo;
#endif
        status = UasLockBuffer((PVOID)address, pc->Plan.DataLength,
                               irp->RequestorMode,
                               (BOOLEAN)(pc->Plan.Dir == UAS_DIR_IN),
                               &pc->UserMdl);
        if (!NT_SUCCESS(status)) {
            return uasPassDrop(pc, status);
        }
        irp->MdlAddress = pc->UserMdl;
        data = (PVOID)address;
    } else if (pc->Plan.DataLength != 0) {
        data = buf + pc->Plan.DataOffset;
    }
    uasPassSrb(pc, pdo->Lun, pc->Plan.Cdb, pc->Plan.CdbLength, data,
               pc->Plan.DataLength, pc->Plan.Dir, pc->Plan.Timeout);
    if (!UasEngineSubmitPass(fdo, pdo, pc)) {
        return uasPassDrop(pc, STATUS_NO_SUCH_DEVICE);
    }
    return STATUS_PENDING;
}

/* Writes the envelope's reply fields (16.4): the rest of it as sent. */
static VOID uasSiocReply(PUCHAR buf, ULONG returnCode, ULONG length)
{
    PULONG w;

    w = (PULONG)buf;
    w[3] = 0;               /* Timeout */
    w[5] = returnCode;      /* ReturnCode */
    w[6] = length;          /* Length */
}

/*
 * The SMART requests as disk.sys sends them (16.9): IOCTL_SCSI_MINIPORT,
 * signature SCSIDISK. Anything else keeps STATUS_INVALID_DEVICE_REQUEST.
 */
static NTSTATUS uasMiniport(PUAS_PDO pdo, PIRP irp, ULONG inLength,
                            ULONG outLength)
{
    PUAS_FDO fdo;
    PUAS_PCMD pc;
    PUCHAR buf;
    ULONG check;
    ULONG control;
    ULONG timeout;
    ULONG capacity;
    ULONG room;
    ULONG err;

    fdo = pdo->Fdo;
    buf = (PUCHAR)irp->AssociatedIrp.SystemBuffer;
    if (buf == NULL) {
        return UasCompleteIrp(irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    }
    check = UasSiocCheck(buf, inLength, outLength, &control, &timeout,
                         &capacity);
    if (check != UAS_PT_OK) {
        return UasCompleteIrp(irp, uasPlanStatus(check), 0);
    }
    if (outLength > inLength) {
        UasZero(buf + inLength, outLength - inLength);
    }
    room = outLength - UAS_SIOC_LENGTH;
    if (capacity < room) {
        room = capacity;
    }
    if (control == UAS_SIOC_SMART_VERSION) {
        if (inLength - UAS_SIOC_LENGTH < UAS_SMART_VERSION_LENGTH ||
            room < UAS_SMART_VERSION_LENGTH) {
            return UasCompleteIrp(irp, STATUS_INVALID_PARAMETER, 0);
        }
        UasSmartVersion(buf + UAS_SIOC_LENGTH);
        uasSiocReply(buf, 0, UAS_SMART_VERSION_LENGTH);
        return UasCompleteIrp(irp, STATUS_SUCCESS,
                              UAS_SIOC_LENGTH + UAS_SMART_VERSION_LENGTH);
    }
    pc = (PUAS_PCMD)UasAlloc(sizeof(UAS_PCMD));
    if (pc == NULL) {
        return UasCompleteIrp(irp, STATUS_INSUFFICIENT_RESOURCES, 0);
    }
    pc->Fdo = fdo;
    pc->Pdo = pdo;
    pc->Irp = irp;
    pc->Form = UAS_PCMD_SMART;
    pc->OutLength = outLength;
    pc->Room = room;
    err = UasSmartPlan(control, buf + UAS_SIOC_LENGTH,
                       inLength - UAS_SIOC_LENGTH, room, &pc->Smart);
    if (err != UAS_SMART_NO_ERROR) {
        /* Refused: the IRP fails, so nothing of this reaches the caller;
         * written for a sender that reads its buffer anyway. */
        if (room >= UAS_SMART_OUT_HEADER) {
            UasZero(buf + UAS_SIOC_LENGTH, UAS_SMART_OUT_HEADER);
            buf[UAS_SIOC_LENGTH + 4] = (UCHAR)err;
            uasSiocReply(buf, err, UAS_SMART_OUT_HEADER);
        }
        return uasPassDrop(pc, STATUS_INVALID_PARAMETER);
    }
    if (pc->Smart.DataLength != 0) {
        pc->Data = (PUCHAR)UasAlloc(pc->Smart.DataLength);
        if (pc->Data == NULL) {
            return uasPassDrop(pc, STATUS_INSUFFICIENT_RESOURCES);
        }
    }
    uasPassSrb(pc, pdo->Lun, pc->Smart.Cdb, 16, pc->Data,
               pc->Smart.DataLength,
               (pc->Smart.DataLength != 0) ? UAS_DIR_IN : UAS_DIR_NONE,
               UasPtTimeout(timeout));
    if (!UasEngineSubmitPass(fdo, pdo, pc)) {
        return uasPassDrop(pc, STATUS_NO_SUCH_DEVICE);
    }
    return STATUS_PENDING;
}

static NTSTATUS uasPassDispatch(PUAS_PDO pdo, PIRP irp)
{
    PIO_STACK_LOCATION stack;
    ULONG inLength;
    ULONG outLength;

    stack = IoGetCurrentIrpStackLocation(irp);
    inLength = stack->Parameters.DeviceIoControl.InputBufferLength;
    outLength = stack->Parameters.DeviceIoControl.OutputBufferLength;
    switch (stack->Parameters.DeviceIoControl.IoControlCode) {
    case IOCTL_SCSI_PASS_THROUGH:
        return uasPassThrough(pdo, irp, UAS_PT_SCSI, inLength, outLength);
    case IOCTL_SCSI_PASS_THROUGH_DIRECT:
        return uasPassThrough(pdo, irp, UAS_PT_SCSI_DIRECT, inLength,
                              outLength);
    case IOCTL_ATA_PASS_THROUGH:
        return uasPassThrough(pdo, irp, UAS_PT_ATA, inLength, outLength);
    case IOCTL_ATA_PASS_THROUGH_DIRECT:
        return uasPassThrough(pdo, irp, UAS_PT_ATA_DIRECT, inLength,
                              outLength);
    default:
        return uasMiniport(pdo, irp, inLength, outLength);
    }
}

/*
 * <= DISPATCH_LEVEL, no lock held, from the engine's completion of the
 * request (uas_xport.c, uasCompleteRequest), which drops the LUN's count
 * after this returns. The outcome is written into the system buffer, the
 * direct form's pages are released (MdlAddress cleared first, so the I/O
 * manager never sees this driver's MDL), and the IRP completes.
 */
VOID UasPdoPassFinish(PUAS_PCMD pc, PSCSI_REQUEST_BLOCK srb)
{
    PIRP irp;
    PUCHAR buf;
    ULONG senseLength;
    ULONG result;
    ULONG information;
    ULONG bytes;
    ULONG err;
    NTSTATUS status;

    irp = pc->Irp;
    buf = (PUCHAR)irp->AssociatedIrp.SystemBuffer;
    senseLength = ((srb->SrbStatus & SRB_STATUS_AUTOSENSE_VALID) != 0)
                      ? srb->SenseInfoBufferLength : 0;
    information = 0;
    if (pc->Cancelled) {
        status = STATUS_CANCELLED;
    } else if (pc->Form == UAS_PCMD_PT) {
        result = UasPtResult(&pc->Plan, buf, pc->OutLength,
                             SRB_STATUS(srb->SrbStatus), srb->ScsiStatus,
                             srb->DataTransferLength, pc->Sense, senseLength,
                             &information);
        status = uasPassStatus(result);
    } else {
        result = UasSmartResult(&pc->Smart, buf + UAS_SIOC_LENGTH, pc->Room,
                                SRB_STATUS(srb->SrbStatus), srb->ScsiStatus,
                                srb->DataTransferLength, pc->Data, pc->Sense,
                                senseLength, &bytes, &err);
        uasSiocReply(buf, err, bytes);
        if (result == UAS_PTR_SUCCESS) {
            information = UAS_SIOC_LENGTH + bytes;
        }
        status = uasPassStatus(result);
    }
    if (pc->UserMdl != NULL) {
        irp->MdlAddress = NULL;
        UasUnlockBuffer(pc->UserMdl);
    }
    UasFree(pc->Data);
    UasFree(pc);
    (VOID)UasCompleteIrp(irp, status, NT_SUCCESS(status) ? information : 0);
}
