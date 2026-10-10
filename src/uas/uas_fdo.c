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

/*
 * Task 36.22: the command size, asked of the bus at every start before the
 * pipes are selected and before any LUN is exposed, separate from the
 * streams (a High-Speed device opens none). 64 KiB first, on this and every
 * path; then the bus's answer if it keeps the contract (UasPolicyValue).
 * An older bus, a reply out of contract and a request that failed each
 * fall back to 64 KiB, counted apart, and the start goes on: its own
 * failures (selection, streams) still fail it. The descriptor and the
 * pipes read the one value stored here. PASSIVE_LEVEL, no lock held.
 */
static VOID uasNoteDword(HANDLE key, PCWSTR name, ULONG value)
{
    UNICODE_STRING n;

    RtlInitUnicodeString(&n, name);
    (VOID)ZwSetValueKey(key, &n, 0, REG_DWORD, &value, sizeof(value));
}

/*
 * The question's outcome as a note of its own, in the UAS device's
 * hardware key (on NT its Device Parameters key): the size taken, where
 * from (XHCI98_TELE_SRC_*), and the request's NTSTATUS - written whatever
 * the outcome, so an older bus, a reply out of contract or a failed
 * request reads in the registry even when no telemetry can reach the bus
 * and the start fails later. A key that will not open writes nothing.
 * PASSIVE_LEVEL.
 */
static VOID uasNotePolicy(PUAS_FDO fdo, ULONG bytes, ULONG source,
                          NTSTATUS status, ULONG limit, ULONG why)
{
    HANDLE key;

    if (!NT_SUCCESS(IoOpenDeviceRegistryKey(fdo->Pdo, PLUGPLAY_REGKEY_DEVICE,
                                            KEY_WRITE, &key))) {
        return;
    }
    uasNoteDword(key, L"XhciUasCommandBytes", bytes);
    uasNoteDword(key, L"XhciUasCommandSource", source);
    uasNoteDword(key, L"XhciUasCommandStatus", (ULONG)status);
    /* The device's own usbstor value as read (0 when absent or not four
     * bytes) and what became of it (UAS_DEVLIM_*). */
    uasNoteDword(key, L"XhciUasDeviceLimit", limit);
    uasNoteDword(key, L"XhciUasDeviceLimitWhy", why);
    ZwClose(key);
}

/* What the query routine saw of the one value it was given: whether it
 * ran, the value's type and length and, when four bytes long, those bytes. */
typedef struct _UAS_DEVLIM_READ {
    ULONG Found;
    ULONG Type;
    ULONG Length;
    ULONG Value;
} UAS_DEVLIM_READ;

/* RtlQueryRegistryValues' routine: records the value whatever its type -
 * UasDeviceLimit judges it - with NOEXPAND set, so a string is seen as the
 * string it is. PASSIVE_LEVEL. */
static NTSTATUS NTAPI uasTakeLimit(PWSTR ValueName, ULONG ValueType,
                                   PVOID ValueData, ULONG ValueLength,
                                   PVOID Context, PVOID EntryContext)
{
    UAS_DEVLIM_READ *r;
    PUCHAR from;

    UNREFERENCED_PARAMETER(ValueName);
    UNREFERENCED_PARAMETER(Context);
    r = (UAS_DEVLIM_READ *)EntryContext;
    r->Found = 1;
    r->Type = ValueType;
    r->Length = ValueLength;
    r->Value = 0;
    if (ValueLength == 4 && ValueData != NULL) {
        from = (PUCHAR)ValueData;
        r->Value = (ULONG)from[0] | ((ULONG)from[1] << 8) |
                   ((ULONG)from[2] << 16) | ((ULONG)from[3] << 24);
    }
    return STATUS_SUCCESS;
}

/*
 * Task 36.22 (owner, 2026-10-09): Microsoft's per-device
 * MaximumTransferLength, from
 * HKLM\SYSTEM\CurrentControlSet\Control\usbstor\VVVVPPPP (KB 2581464; the
 * device's idVendor and idProduct, four uppercase hex digits each) - the
 * key Windows' own usbstor.sys reads and the tools that set it write. On
 * Windows 98 and ME nothing larger than 64 KiB is sent whatever it says
 * (USBNTMAP), and the read is harmless there. *r stays all zero when the
 * key or the value is absent. PASSIVE_LEVEL.
 */
static VOID uasReadLimit(USHORT vid, USHORT pid, UAS_DEVLIM_READ *r)
{
    static const char hex[] = "0123456789ABCDEF";
    static const char head[] = "usbstor\\";
    RTL_QUERY_REGISTRY_TABLE table[2];
    WCHAR path[20];
    ULONG ids;
    ULONG i;
    PUCHAR p;

    UasZero(r, sizeof(*r));
    for (i = 0; i < 8; i++) {
        path[i] = (WCHAR)head[i];
    }
    ids = ((ULONG)vid << 16) | (ULONG)pid;
    for (i = 0; i < 8; i++) {
        path[8 + i] = (WCHAR)hex[(ids >> (28 - 4 * i)) & 0xFUL];
    }
    path[16] = 0;
    p = (PUCHAR)table;
    for (i = 0; i < sizeof(table); i++) {
        p[i] = 0;
    }
    table[0].QueryRoutine = uasTakeLimit;
    table[0].Flags = RTL_QUERY_REGISTRY_REQUIRED | RTL_QUERY_REGISTRY_NOEXPAND;
    table[0].Name = L"MaximumTransferLength";
    table[0].EntryContext = r;
    if (!NT_SUCCESS(RtlQueryRegistryValues(RTL_REGISTRY_CONTROL, path, table,
                                           NULL, NULL)) &&
        !r->Found) {
        UasZero(r, sizeof(*r));
    }
}

static VOID uasAskPolicy(PUAS_FDO fdo, USHORT vid, USHORT pid)
{
    UAS_DEVLIM_READ limit;
    ULONG why;
    PXHCI98_TRANSFER_POLICY rq;
    NTSTATUS status;
    ULONG sent;
    ULONG source;
    ULONG bytes;
    KIRQL irql;

    KeAcquireSpinLock(&fdo->Lock, &irql);
    fdo->CommandBytes = UAS_MAX_TRANSFER;
    fdo->PolicySource = XHCI98_TELE_SRC_UNASKED;
    KeReleaseSpinLock(&fdo->Lock, irql);

    rq = (PXHCI98_TRANSFER_POLICY)UasAlloc(sizeof(XHCI98_TRANSFER_POLICY));
    status = STATUS_INSUFFICIENT_RESOURCES;
    if (rq == NULL) {
        sent = UAS_POLICY_SENT_FAILED;
    } else {
        UasZero(rq, sizeof(*rq));
        rq->Signature = XHCI98_POLICY_SIGNATURE;
        rq->Version = XHCI98_POLICY_VERSION;
        rq->Size = sizeof(XHCI98_TRANSFER_POLICY);
        status = uasInternalIoctl(fdo, IOCTL_XHCI98_TRANSFER_POLICY, rq);
        if (NT_SUCCESS(status)) {
            sent = UAS_POLICY_SENT_OK;
        } else if (status == STATUS_NOT_SUPPORTED ||
                   status == STATUS_INVALID_DEVICE_REQUEST) {
            sent = UAS_POLICY_SENT_UNSUPPORTED;
        } else if (status == STATUS_INVALID_PARAMETER) {
            sent = UAS_POLICY_SENT_REFUSED;
        } else {
            sent = UAS_POLICY_SENT_FAILED;
        }
    }
    bytes = UasPolicyValue(sent, rq, &source);
    UasFree(rq);
    /* The device's own limit over the bus's answer, for this device only
     * (UasDeviceLimit). */
    uasReadLimit(vid, pid, &limit);
    bytes = UasDeviceLimit(bytes, source, limit.Found, limit.Type,
                           limit.Length, limit.Value, &source, &why);

    KeAcquireSpinLock(&fdo->Lock, &irql);
    fdo->CommandBytes = bytes;
    fdo->PolicySource = source;
    if (source == XHCI98_TELE_SRC_UNSUPPORTED) {
        fdo->PolicyUnsupported++;
    } else if (source == XHCI98_TELE_SRC_MALFORMED) {
        fdo->PolicyMalformed++;
    } else if (source == XHCI98_TELE_SRC_TRANSPORT) {
        fdo->PolicyTransport++;
    }
    KeReleaseSpinLock(&fdo->Lock, irql);
    uasNotePolicy(fdo, bytes, source, status,
                  (limit.Found && limit.Length == 4) ? limit.Value : 0, why);
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
        ii->Pipes[i].MaximumTransferSize = fdo->CommandBytes;
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
    USHORT vid;
    USHORT pid;
    NTSTATUS status;

    desc = (PUCHAR)UasAlloc(18);
    if (desc == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    status = uasGetDescriptor(fdo, USB_DEVICE_DESCRIPTOR_TYPE, 0, 0, desc,
                              18, &got);
    serialIndex = (NT_SUCCESS(status) && got >= 17) ? desc[16] : 0;
    vid = (NT_SUCCESS(status) && got >= 12)
              ? (USHORT)(desc[8] | (desc[9] << 8)) : (USHORT)0;
    pid = (NT_SUCCESS(status) && got >= 12)
              ? (USHORT)(desc[10] | (desc[11] << 8)) : (USHORT)0;
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

    uasAskPolicy(fdo, vid, pid);
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

/*
 * After UasEngineStop: every request has completed and the engine has run
 * down. Each LUN PDO's dispatches are then run down too (Gone set, the
 * Busy bias dropped, the last dispatch waited for), so none still holds
 * this FDO, before the PDO is deleted and the FDO's memory goes. PnP has
 * sent each child its REMOVE before this parent's.
 */
static VOID uasFdoRemove(PUAS_FDO fdo)
{
    PUAS_PDO pdo;
    ULONG i;

    for (i = 0; i < fdo->LunCount; i++) {
        pdo = fdo->Luns[i];
        pdo->Gone = TRUE;
        if (InterlockedDecrement(&pdo->Busy) != 0) {
            (VOID)KeWaitForSingleObject(&pdo->BusyIdle, Executive,
                                        KernelMode, FALSE, NULL);
        }
    }
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
