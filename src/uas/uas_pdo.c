/*
 * uas_pdo.c - one PDO per logical unit: its PnP ids, the SRBs the storage
 * class layer sends it, and the two storage IOCTLs that layer asks first.
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

    stack = IoGetCurrentIrpStackLocation(irp);
    status = irp->IoStatus.Status;
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
         * it, and PnP may start it again. */
        pdo->Started = FALSE;
        if (pdo->Fdo != NULL) {
            UasEngineCloseLun(pdo->Fdo, pdo, SRB_STATUS_NO_DEVICE);
        }
        pdo->Removed = TRUE;
        pdo->QueueLocked = FALSE;
        pdo->Claimed = FALSE;
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

    UasZero(&a, sizeof(a));
    a.Version = sizeof(STORAGE_ADAPTER_DESCRIPTOR);
    a.Size = sizeof(STORAGE_ADAPTER_DESCRIPTOR);
    a.MaximumTransferLength = UAS_MAX_TRANSFER;
    a.MaximumPhysicalPages = UAS_MAX_TRANSFER / PAGE_SIZE + 1;
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

    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }
    status = UasCompleteIrp(irp, status, written);
    uasPdoLeave(pdo);
    return status;
}
