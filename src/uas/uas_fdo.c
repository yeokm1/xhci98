/*
 * uas_fdo.c - the FDO of xhciuas.sys on the bus's device or function PDO:
 * PnP, power pass-through, the configuration and the four pipes, the
 * transport choice, LUN discovery and the BusRelations of the LUN PDOs.
 *
 * START. The device descriptor (for the serial string's index) and the
 * configuration descriptor are read; uas_iu.c finds the 08/06/62 alternate
 * setting and the endpoint each Pipe Usage descriptor names; that setting
 * is selected directly in SELECT_CONFIGURATION, so a device whose setting 0
 * is Bulk-Only never runs it under this driver. SuperSpeed endpoint
 * companions in the descriptor mean the device enumerated at SuperSpeed,
 * where UAS is streamed: streams are opened on the status and both data
 * pipes through the adapter (uas_streams.h, over the bus's
 * IOCTL_XHCI98_OPEN_STREAMS) and a start without at least two fails, since a SuperSpeed UAS device has no streamless transport. Without
 * companions the device is at High Speed (or below) and runs streamless.
 * The engine is started, REPORT LUNS asked of LUN 0 (one LUN if the device
 * cannot answer), INQUIRY asked of each LUN, and a PDO made per LUN whose
 * peripheral qualifier says one is there.
 *
 * IRQL: PASSIVE_LEVEL throughout (PnP and power IRPs; this driver's objects
 * are DO_POWER_PAGABLE). The completion routine at <= DISPATCH_LEVEL.
 */

#include "uas.h"

static NTSTATUS uasSignal(PDEVICE_OBJECT device, PIRP irp, PVOID context)
{
    UNREFERENCED_PARAMETER(device);
    UNREFERENCED_PARAMETER(irp);
    KeSetEvent((PKEVENT)context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

/* The IRP passed down and waited for; the caller completes it. */
static NTSTATUS uasForwardSync(PUAS_FDO fdo, PIRP irp)
{
    KEVENT done;

    KeInitializeEvent(&done, NotificationEvent, FALSE);
    IoCopyCurrentIrpStackLocationToNext(irp);
    UAS_SET_COMPLETION_ALWAYS(irp, uasSignal, &done);
    (VOID)IoCallDriver(fdo->Lower, irp);
    (VOID)KeWaitForSingleObject(&done, Executive, KernelMode, FALSE, NULL);
    return irp->IoStatus.Status;
}

static NTSTATUS uasInternalIoctl(PUAS_FDO fdo, ULONG code, PVOID arg1)
{
    PIO_STACK_LOCATION next;
    KEVENT done;
    PIRP irp;
    NTSTATUS status;

    irp = IoAllocateIrp(fdo->Lower->StackSize, FALSE);
    if (irp == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    KeInitializeEvent(&done, NotificationEvent, FALSE);
    irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    next = IoGetNextIrpStackLocation(irp);
    next->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
    next->Parameters.DeviceIoControl.IoControlCode = code;
    next->Parameters.Others.Argument1 = arg1;
    UAS_SET_COMPLETION_ALWAYS(irp, uasSignal, &done);
    (VOID)IoCallDriver(fdo->Lower, irp);
    (VOID)KeWaitForSingleObject(&done, Executive, KernelMode, FALSE, NULL);
    status = irp->IoStatus.Status;
    IoFreeIrp(irp);
    return status;
}

NTSTATUS UasSyncUrb(PUAS_FDO fdo, PURB urb)
{
    NTSTATUS status;

    status = uasInternalIoctl(fdo, IOCTL_INTERNAL_USB_SUBMIT_URB, urb);
    if (NT_SUCCESS(status) && !USBD_SUCCESS(urb->UrbHeader.Status)) {
        status = STATUS_UNSUCCESSFUL;
    }
    return status;
}

NTSTATUS UasSyncIoctl(PUAS_FDO fdo, ULONG code)
{
    return uasInternalIoctl(fdo, code, NULL);
}

/* One standard descriptor from the device, into nonpaged pool. */
static NTSTATUS uasGetDescriptor(PUAS_FDO fdo, UCHAR type, UCHAR index,
                                 USHORT language, PVOID buffer,
                                 ULONG length, PULONG got)
{
    struct _URB_CONTROL_DESCRIPTOR_REQUEST *u;
    NTSTATUS status;

    *got = 0;
    u = (struct _URB_CONTROL_DESCRIPTOR_REQUEST *)
            UasAlloc(sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST));
    if (u == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    u->Hdr.Length = sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST);
    u->Hdr.Function = URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE;
    u->TransferBuffer = buffer;
    u->TransferBufferLength = length;
    u->Index = index;
    u->DescriptorType = type;
    u->LanguageId = language;
    status = UasSyncUrb(fdo, (PURB)u);
    if (NT_SUCCESS(status)) {
        *got = u->TransferBufferLength;
    }
    UasFree(u);
    return status;
}

/* The serial number string, as printable ASCII (the instance id's). */
static VOID uasReadSerial(PUAS_FDO fdo, UCHAR index)
{
    PUCHAR buf;
    ULONG got;
    ULONG i;
    ULONG n;
    USHORT ch;

    fdo->Serial[0] = 0;
    if (index == 0) {
        return;
    }
    buf = (PUCHAR)UasAlloc(256);
    if (buf == NULL) {
        return;
    }
    if (NT_SUCCESS(uasGetDescriptor(fdo, USB_STRING_DESCRIPTOR_TYPE, index,
                                    0x0409, buf, 255, &got)) &&
        got >= 2 && buf[1] == USB_STRING_DESCRIPTOR_TYPE) {
        if (got > buf[0]) {
            got = buf[0];
        }
        n = 0;
        for (i = 2; i + 1 < got && n < sizeof(fdo->Serial) - 1; i += 2) {
            ch = (USHORT)(buf[i] | (buf[i + 1] << 8));
            fdo->Serial[n++] = (ch > 0x20 && ch < 0x7F) ? (char)ch : '_';
        }
        fdo->Serial[n] = 0;
    }
    UasFree(buf);
}

/* SELECT_CONFIGURATION with the UAS alternate setting, and the four pipe
 * handles matched to the Pipe Usage descriptors by endpoint address. */
static NTSTATUS uasSelect(PUAS_FDO fdo)
{
    struct _URB_SELECT_CONFIGURATION *sc;
    PUSBD_INTERFACE_INFORMATION ii;
    ULONG pipes;
    ULONG ifSize;
    ULONG urbSize;
    ULONG i;
    ULONG id;
    ULONG found;
    NTSTATUS status;

    pipes = fdo->Info.EndpointCount;
    if (pipes < UAS_PIPES || pipes > 16) {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    ifSize = FIELD_OFFSET(USBD_INTERFACE_INFORMATION, Pipes) +
             pipes * sizeof(USBD_PIPE_INFORMATION);
    urbSize = FIELD_OFFSET(struct _URB_SELECT_CONFIGURATION, Interface) +
              ifSize;
    sc = (struct _URB_SELECT_CONFIGURATION *)UasAlloc(urbSize);
    if (sc == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    sc->Hdr.Length = (USHORT)urbSize;
    sc->Hdr.Function = URB_FUNCTION_SELECT_CONFIGURATION;
    sc->ConfigurationDescriptor =
        (PUSB_CONFIGURATION_DESCRIPTOR)fdo->Config;
    ii = &sc->Interface;
    ii->Length = (USHORT)ifSize;
    ii->InterfaceNumber = (UCHAR)fdo->Info.InterfaceNumber;
    ii->AlternateSetting = (UCHAR)fdo->Info.AlternateSetting;
    ii->NumberOfPipes = pipes;
    for (i = 0; i < pipes; i++) {
        ii->Pipes[i].MaximumTransferSize = UAS_MAX_TRANSFER;
        ii->Pipes[i].PipeFlags = 0;
    }
    status = UasSyncUrb(fdo, (PURB)sc);
    if (NT_SUCCESS(status)) {
        found = 0;
        fdo->ConfigHandle = sc->ConfigurationHandle;
        for (i = 0; i < ii->NumberOfPipes && i < pipes; i++) {
            for (id = 0; id < UAS_PIPES; id++) {
                if (ii->Pipes[i].EndpointAddress ==
                    (UCHAR)fdo->Info.EndpointAddress[id]) {
                    fdo->Pipe[id] = ii->Pipes[i].PipeHandle;
                    found |= 1UL << id;
                }
            }
        }
        if (found != 0x0F) {
            status = STATUS_DEVICE_CONFIGURATION_ERROR;
        }
    }
    UasFree(sc);
    return status;
}

/* The streams the status and both data pipes all support, capped. */
static ULONG uasStreamsWanted(PUAS_FDO fdo)
{
    ULONG wanted;
    ULONG id;

    wanted = UAS_MAX_TAGS;
    for (id = UAS_PIPE_STATUS; id <= UAS_PIPE_DATA_OUT; id++) {
        if (fdo->Info.MaxStreams[id - 1] < wanted) {
            wanted = fdo->Info.MaxStreams[id - 1];
        }
    }
    return wanted;
}

/* REPORT LUNS on LUN 0, retried past a unit attention; LUN 0 alone when
 * the device cannot answer. */
static ULONG uasReportLuns(PUAS_FDO fdo, PUCHAR luns)
{
    UCHAR cdb[12];
    PUCHAR buf;
    ULONG length;
    ULONG got;
    ULONG n;
    ULONG tries;
    UCHAR scsi;

    n = 0;
    length = 8 + 8 * UAS_MAX_LUNS;
    buf = (PUCHAR)UasAlloc(length);
    if (buf != NULL) {
        UasZero(cdb, sizeof(cdb));
        cdb[0] = 0xA0;
        cdb[9] = (UCHAR)length;
        for (tries = 0; tries < 3 && n == 0; tries++) {
            if (NT_SUCCESS(UasInternalCommand(fdo, 0, cdb, 12, buf, length,
                                              &got, &scsi))) {
                n = UasParseReportLuns(buf, got, luns, UAS_MAX_LUNS);
                break;
            }
        }
        UasFree(buf);
    }
    if (n == 0) {
        luns[0] = 0;
        n = 1;
    }
    return n;
}

static NTSTATUS uasInquiry(PUAS_FDO fdo, ULONG lun, PUCHAR inquiry)
{
    UCHAR cdb[6];
    PUCHAR buf;
    ULONG got;
    ULONG tries;
    UCHAR scsi;
    NTSTATUS status;

    buf = (PUCHAR)UasAlloc(UAS_INQUIRY_LENGTH);
    if (buf == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    UasZero(cdb, sizeof(cdb));
    cdb[0] = 0x12;
    cdb[4] = UAS_INQUIRY_LENGTH;
    status = STATUS_UNSUCCESSFUL;
    for (tries = 0; tries < 3; tries++) {
        UasZero(buf, UAS_INQUIRY_LENGTH);
        status = UasInternalCommand(fdo, lun, cdb, 6, buf,
                                    UAS_INQUIRY_LENGTH, &got, &scsi);
        if (NT_SUCCESS(status) && got >= 5) {
            UasCopy(inquiry, buf, UAS_INQUIRY_LENGTH);
            break;
        }
        status = STATUS_UNSUCCESSFUL;
    }
    UasFree(buf);
    return status;
}

static NTSTATUS uasFdoStart(PUAS_FDO fdo)
{
    UCHAR luns[UAS_MAX_LUNS];
    UCHAR inquiry[UAS_INQUIRY_LENGTH];
    PUCHAR desc;
    ULONG got;
    ULONG total;
    ULONG count;
    ULONG wanted;
    ULONG i;
    ULONG j;
    BOOLEAN known;
    PUAS_PDO pdo;
    UCHAR serialIndex;
    NTSTATUS status;

    desc = (PUCHAR)UasAlloc(18);
    if (desc == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    status = uasGetDescriptor(fdo, USB_DEVICE_DESCRIPTOR_TYPE, 0, 0, desc,
                              18, &got);
    serialIndex = (NT_SUCCESS(status) && got >= 17) ? desc[16] : 0;
    UasFree(desc);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    if (fdo->Config == NULL) {
        desc = (PUCHAR)UasAlloc(9);
        if (desc == NULL) {
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        status = uasGetDescriptor(fdo, USB_CONFIGURATION_DESCRIPTOR_TYPE, 0,
                                  0, desc, 9, &got);
        total = (NT_SUCCESS(status) && got >= 4)
                    ? (ULONG)(desc[2] | (desc[3] << 8)) : 0;
        UasFree(desc);
        if (total < 9 || total > 4096) {
            return NT_SUCCESS(status) ? STATUS_DEVICE_CONFIGURATION_ERROR
                                      : status;
        }
        fdo->Config = (PUCHAR)UasAlloc(total);
        if (fdo->Config == NULL) {
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        status = uasGetDescriptor(fdo, USB_CONFIGURATION_DESCRIPTOR_TYPE, 0,
                                  0, fdo->Config, total, &got);
        if (!NT_SUCCESS(status) || got < total) {
            UasFree(fdo->Config);
            fdo->Config = NULL;
            return NT_SUCCESS(status) ? STATUS_DEVICE_CONFIGURATION_ERROR
                                      : status;
        }
        fdo->ConfigLength = total;
        if (UasParseConfig(fdo->Config, total, &fdo->Info) != UAS_CFG_OK) {
            UasFree(fdo->Config);
            fdo->Config = NULL;
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }
    }

    status = uasSelect(fdo);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    uasReadSerial(fdo, serialIndex);

    fdo->Streamed = FALSE;
    if (fdo->Info.SuperSpeed) {
        wanted = uasStreamsWanted(fdo);
        if (wanted < 2) {
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }
        status = UasStreamsOpen(&fdo->Streams, fdo->Lower,
                                fdo->Pipe[UAS_PIPE_STATUS - 1],
                                fdo->Pipe[UAS_PIPE_DATA_IN - 1],
                                fdo->Pipe[UAS_PIPE_DATA_OUT - 1], wanted);
        if (!NT_SUCCESS(status)) {
            return status;
        }
        if (fdo->Streams.Granted < 2) {
            UasStreamsClose(&fdo->Streams);
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }
        fdo->Streamed = TRUE;
    }

    if (fdo->Slots == NULL) {
        status = UasEngineInit(fdo);
        if (!NT_SUCCESS(status)) {
            UasStreamsClose(&fdo->Streams);
            return status;
        }
    }
    UasEngineStart(fdo);

    count = uasReportLuns(fdo, luns);
    for (i = 0; i < count; i++) {
        if (!NT_SUCCESS(uasInquiry(fdo, luns[i], inquiry))) {
            continue;
        }
        /* Peripheral qualifier 0: a unit is attached at this LUN. */
        if ((inquiry[0] >> 5) != 0) {
            continue;
        }
        known = FALSE;
        for (j = 0; j < fdo->LunCount; j++) {
            if (fdo->Luns[j]->Lun == luns[i]) {
                known = TRUE;
                fdo->Luns[j]->Present = TRUE;
            }
        }
        if (known || fdo->LunCount >= UAS_MAX_LUNS) {
            continue;
        }
        if (NT_SUCCESS(UasPdoCreate(fdo, luns[i], inquiry, &pdo))) {
            fdo->Luns[fdo->LunCount++] = pdo;
        }
    }
    return STATUS_SUCCESS;
}

/* The engine stopped with every request ended, the streams closed. */
static VOID uasFdoQuiesce(PUAS_FDO fdo, UCHAR srbStatus)
{
    UasEngineStop(fdo, srbStatus);
    UasStreamsClose(&fdo->Streams);
}

static NTSTATUS uasBusRelations(PUAS_FDO fdo, PIRP irp)
{
    PDEVICE_RELATIONS old;
    PDEVICE_RELATIONS rel;
    ULONG count;
    ULONG oldCount;
    ULONG i;

    old = (PDEVICE_RELATIONS)irp->IoStatus.Information;
    oldCount = (old != NULL) ? old->Count : 0;
    count = 0;
    for (i = 0; i < fdo->LunCount; i++) {
        if (fdo->Luns[i]->Present) {
            count++;
        }
    }
    rel = (PDEVICE_RELATIONS)UasAlloc(sizeof(DEVICE_RELATIONS) +
                                      (oldCount + count) *
                                          sizeof(PDEVICE_OBJECT));
    if (rel == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    rel->Count = 0;
    for (i = 0; i < oldCount; i++) {
        rel->Objects[rel->Count++] = old->Objects[i];
    }
    for (i = 0; i < fdo->LunCount; i++) {
        if (fdo->Luns[i]->Present) {
            ObReferenceObject(fdo->Luns[i]->Self);
            rel->Objects[rel->Count++] = fdo->Luns[i]->Self;
        }
    }
    if (old != NULL) {
        UasFree(old);
    }
    irp->IoStatus.Information = (ULONG_PTR)rel;
    return STATUS_SUCCESS;
}

static VOID uasFdoRemove(PUAS_FDO fdo)
{
    ULONG i;

    UasStreamsClose(&fdo->Streams);
    UasEngineFree(fdo);
    for (i = 0; i < fdo->LunCount; i++) {
        fdo->Luns[i]->Present = FALSE;
        fdo->Luns[i]->Fdo = NULL;
        IoDeleteDevice(fdo->Luns[i]->Self);
        fdo->Luns[i] = NULL;
    }
    fdo->LunCount = 0;
    UasFree(fdo->Config);
    fdo->Config = NULL;
}

NTSTATUS UasFdoPnp(PUAS_FDO fdo, PIRP irp)
{
    PIO_STACK_LOCATION stack;
    PDEVICE_CAPABILITIES caps;
    NTSTATUS status;
    KIRQL irql;

    stack = IoGetCurrentIrpStackLocation(irp);
    switch (stack->MinorFunction) {
    case IRP_MN_START_DEVICE:
        status = uasForwardSync(fdo, irp);
        if (NT_SUCCESS(status)) {
            status = uasFdoStart(fdo);
            if (!NT_SUCCESS(status)) {
                uasFdoQuiesce(fdo, SRB_STATUS_NO_DEVICE);
            }
        }
        return UasCompleteIrp(irp, status, 0);

    case IRP_MN_QUERY_CAPABILITIES:
        status = uasForwardSync(fdo, irp);
        if (NT_SUCCESS(status)) {
            caps = stack->Parameters.DeviceCapabilities.Capabilities;
            caps->SurpriseRemovalOK = TRUE;
        }
        return UasCompleteIrp(irp, status, irp->IoStatus.Information);

    case IRP_MN_QUERY_DEVICE_RELATIONS:
        if (stack->Parameters.QueryDeviceRelations.Type == BusRelations) {
            status = uasBusRelations(fdo, irp);
            if (!NT_SUCCESS(status)) {
                return UasCompleteIrp(irp, status, 0);
            }
            irp->IoStatus.Status = STATUS_SUCCESS;
        }
        break;

    case IRP_MN_QUERY_STOP_DEVICE:
    case IRP_MN_QUERY_REMOVE_DEVICE:
    case IRP_MN_CANCEL_STOP_DEVICE:
    case IRP_MN_CANCEL_REMOVE_DEVICE:
        irp->IoStatus.Status = STATUS_SUCCESS;
        break;

    case IRP_MN_STOP_DEVICE:
        uasFdoQuiesce(fdo, SRB_STATUS_BUS_RESET);
        irp->IoStatus.Status = STATUS_SUCCESS;
        break;

    case IRP_MN_SURPRISE_REMOVAL:
        KeAcquireSpinLock(&fdo->Lock, &irql);
        fdo->Gone = TRUE;
        KeReleaseSpinLock(&fdo->Lock, irql);
        uasFdoQuiesce(fdo, SRB_STATUS_NO_DEVICE);
        irp->IoStatus.Status = STATUS_SUCCESS;
        break;

    case IRP_MN_REMOVE_DEVICE:
        KeAcquireSpinLock(&fdo->Lock, &irql);
        fdo->Gone = TRUE;
        KeReleaseSpinLock(&fdo->Lock, irql);
        uasFdoQuiesce(fdo, SRB_STATUS_NO_DEVICE);
        uasFdoRemove(fdo);
        irp->IoStatus.Status = STATUS_SUCCESS;
        IoSkipCurrentIrpStackLocation(irp);
        status = IoCallDriver(fdo->Lower, irp);
        IoDetachDevice(fdo->Lower);
        IoDeleteDevice(fdo->Self);
        return status;

    default:
        break;
    }
    IoSkipCurrentIrpStackLocation(irp);
    return IoCallDriver(fdo->Lower, irp);
}

NTSTATUS UasFdoPower(PUAS_FDO fdo, PIRP irp)
{
    PoStartNextPowerIrp(irp);
    IoSkipCurrentIrpStackLocation(irp);
    return PoCallDriver(fdo->Lower, irp);
}
