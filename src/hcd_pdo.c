/*
 * hcd_pdo.c - the device PDOs of xhci98.sys: one per device the bus has
 * enumerated, every one a child of the root hub (roadmap-hcd.md task
 * 26-A.4; design record 13 sections 5.2, 6.5 and 10.7).
 *
 * Created by the controller thread when a port's machine reaches Present
 * (hcd_enum.c), listed on the controller under PdoListLock - the one lock of
 * design record 13 section 5.4's layer 2, held only to link, unlink or copy
 * the list - and reported through the root hub's BusRelations. When the
 * device leaves, its PDO is unlinked and the root hub's relations
 * invalidated; the PnP manager then removes it and the remove deletes it. A
 * PDO the PnP manager removes while it is still listed (a disable of the
 * device) stays, as the WDM bus rule requires.
 *
 * The ids are section 10.7's: USB\VID_vvvv&PID_pppp with &REV_rrrr ahead of
 * it, and the USB\Class_ compatible ids from the device descriptor or, when
 * bDeviceClass is 0, from the first interface. The instance id is the root
 * port, until the serial string is read (26-A.4 later, or 29-A.5).
 *
 * The URB contract is 26-A.5's: until then IRP_MJ_INTERNAL_DEVICE_CONTROL is
 * refused, so a class driver binds and its start fails.
 *
 * IRQL: PASSIVE_LEVEL, except HcdDevicePdoList (<= DISPATCH_LEVEL inside the
 * lock it takes).
 */

#include "hcd.h"
#include "xhci_dbg.h"

static LONG hcdDevicePdoSerial;

/* ----------------------------------------------------------------------- */
/* Strings                                                                  */
/* ----------------------------------------------------------------------- */

static const WCHAR hcdHex[] = L"0123456789ABCDEF";

/* Append `s` to `out` at *n. */
static VOID hcdPut(PWCHAR out, PULONG n, const WCHAR *s)
{
    while (*s != 0) {
        out[(*n)++] = *s++;
    }
}

/* Append `digits` hex digits of `value`. */
static VOID hcdPutHex(PWCHAR out, PULONG n, ULONG value, ULONG digits)
{
    while (digits > 0) {
        digits--;
        out[(*n)++] = hcdHex[(value >> (digits * 4UL)) & 0xFUL];
    }
}

static VOID hcdPutDecimal(PWCHAR out, PULONG n, ULONG value)
{
    WCHAR digits[12];
    ULONG d;

    d = 0;
    do {
        digits[d++] = (WCHAR)(L'0' + (value % 10UL));
        value /= 10UL;
    } while (value != 0 && d < 11);
    while (d > 0) {
        out[(*n)++] = digits[--d];
    }
}

/* A handed-off copy of `count` WCHARs. */
static PWCHAR hcdHandOff(const WCHAR *s, ULONG count)
{
    PWCHAR out;
    ULONG i;

    out = (PWCHAR)HcdPoolAllocHandedOff(count * sizeof(WCHAR));
    if (out == NULL) {
        return NULL;
    }
    for (i = 0; i < count; i++) {
        out[i] = s[i];
    }
    return out;
}

/* The class triple the compatible ids carry: the device descriptor's, or the
 * first interface's when bDeviceClass is 0 (section 10.7). */
static VOID hcdClassTriple(PHCD_DEVICE_PDO pdo, PULONG cls, PULONG sub,
                           PULONG prot)
{
    ULONG at;
    ULONG len;

    *cls = pdo->DeviceDesc[4];
    *sub = pdo->DeviceDesc[5];
    *prot = pdo->DeviceDesc[6];
    if (*cls != 0) {
        return;
    }
    at = 0;
    while (at + 2 <= pdo->ConfigLength) {
        len = pdo->Config[at];
        if (len < 2 || at + len > pdo->ConfigLength) {
            break;
        }
        if (pdo->Config[at + 1] == 4 && len >= 9) {
            *cls = pdo->Config[at + 5];
            *sub = pdo->Config[at + 6];
            *prot = pdo->Config[at + 7];
            return;
        }
        at += len;
    }
}

static NTSTATUS hcdDeviceQueryId(PHCD_DEVICE_PDO pdo, PIRP irp,
                                 BUS_QUERY_ID_TYPE type)
{
    WCHAR buf[200];
    ULONG n;
    ULONG vid;
    ULONG pid;
    ULONG rev;
    ULONG cls;
    ULONG sub;
    ULONG prot;
    PWCHAR out;

    vid = (ULONG)pdo->DeviceDesc[8] | ((ULONG)pdo->DeviceDesc[9] << 8);
    pid = (ULONG)pdo->DeviceDesc[10] | ((ULONG)pdo->DeviceDesc[11] << 8);
    rev = (ULONG)pdo->DeviceDesc[12] | ((ULONG)pdo->DeviceDesc[13] << 8);
    n = 0;

    switch (type) {
    case BusQueryDeviceID:
        hcdPut(buf, &n, L"USB\\VID_");
        hcdPutHex(buf, &n, vid, 4);
        hcdPut(buf, &n, L"&PID_");
        hcdPutHex(buf, &n, pid, 4);
        buf[n++] = 0;
        break;

    case BusQueryHardwareIDs:
        hcdPut(buf, &n, L"USB\\VID_");
        hcdPutHex(buf, &n, vid, 4);
        hcdPut(buf, &n, L"&PID_");
        hcdPutHex(buf, &n, pid, 4);
        hcdPut(buf, &n, L"&REV_");
        hcdPutHex(buf, &n, rev, 4);
        buf[n++] = 0;
        hcdPut(buf, &n, L"USB\\VID_");
        hcdPutHex(buf, &n, vid, 4);
        hcdPut(buf, &n, L"&PID_");
        hcdPutHex(buf, &n, pid, 4);
        buf[n++] = 0;
        buf[n++] = 0;
        break;

    case BusQueryCompatibleIDs:
        hcdClassTriple(pdo, &cls, &sub, &prot);
        hcdPut(buf, &n, L"USB\\Class_");
        hcdPutHex(buf, &n, cls, 2);
        hcdPut(buf, &n, L"&SubClass_");
        hcdPutHex(buf, &n, sub, 2);
        hcdPut(buf, &n, L"&Prot_");
        hcdPutHex(buf, &n, prot, 2);
        buf[n++] = 0;
        hcdPut(buf, &n, L"USB\\Class_");
        hcdPutHex(buf, &n, cls, 2);
        hcdPut(buf, &n, L"&SubClass_");
        hcdPutHex(buf, &n, sub, 2);
        buf[n++] = 0;
        hcdPut(buf, &n, L"USB\\Class_");
        hcdPutHex(buf, &n, cls, 2);
        buf[n++] = 0;
        buf[n++] = 0;
        break;

    case BusQueryInstanceID:
        hcdPutDecimal(buf, &n, pdo->Port);
        buf[n++] = 0;
        break;

    default:
        return HcdCompleteIrp(irp, irp->IoStatus.Status,
                              irp->IoStatus.Information);
    }

    out = hcdHandOff(buf, n);
    if (out == NULL) {
        return HcdCompleteIrp(irp, STATUS_INSUFFICIENT_RESOURCES, 0);
    }
    return HcdCompleteIrp(irp, STATUS_SUCCESS, (ULONG_PTR)out);
}

/* ----------------------------------------------------------------------- */
/* Creation, listing and loss - the controller thread's side               */
/* ----------------------------------------------------------------------- */

/* Create the PDO for an enumerated device, list it, and have PnP ask the
 * root hub for its relations. The descriptors are copied into the PDO, which
 * may outlive the device record. */
NTSTATUS HcdDevicePdoCreate(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    WCHAR nameBuffer[40];
    UNICODE_STRING name;
    PDEVICE_OBJECT obj;
    PHCD_DEVICE_PDO pdo;
    KIRQL oldIrql;
    NTSTATUS status;
    ULONG n;
    ULONG i;

    if (hc->RootHubPdo == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }

    n = 0;
    hcdPut(nameBuffer, &n, L"\\Device\\XHCI98DEV");
    hcdPutDecimal(nameBuffer, &n,
                  (ULONG)InterlockedIncrement(&hcdDevicePdoSerial));
    nameBuffer[n] = 0;
    RtlInitUnicodeString(&name, nameBuffer);
    status = IoCreateDevice(HcdDriverObject, sizeof(HCD_DEVICE_PDO), &name,
                            FILE_DEVICE_UNKNOWN, 0, FALSE, &obj);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    pdo = (PHCD_DEVICE_PDO)obj->DeviceExtension;
    pdo->Common.Kind = HCD_KIND_DEVICE_PDO;
    pdo->Common.Self = obj;
    pdo->Common.PnpState = HCD_PNP_ADDED;
    pdo->Common.DevicePower = PowerDeviceD0;
    pdo->Common.SystemPower = PowerSystemWorking;
    pdo->Controller = hc;
    pdo->Device = dev;
    pdo->Port = dev->Port;
    pdo->Speed = dev->Speed;
    for (i = 0; i < sizeof(pdo->DeviceDesc); i++) {
        pdo->DeviceDesc[i] = dev->DeviceDesc[i];
    }
    pdo->ConfigLength = 0;
    pdo->Config = NULL;
    if (dev->ConfigLength != 0) {
        pdo->Config = (PUCHAR)HcdPoolAlloc(dev->ConfigLength);
        if (pdo->Config != NULL) {
            for (i = 0; i < dev->ConfigLength; i++) {
                pdo->Config[i] = dev->Config[i];
            }
            pdo->ConfigLength = dev->ConfigLength;
        }
    }

    obj->Flags |= DO_POWER_PAGABLE;
    obj->Flags &= ~DO_DEVICE_INITIALIZING;
    dev->Pdo = obj;

    KeAcquireSpinLock(&hc->PdoListLock, &oldIrql);
    pdo->Next = hc->DevicePdos;
    hc->DevicePdos = pdo;
    pdo->Listed = 1;
    KeReleaseSpinLock(&hc->PdoListLock, oldIrql);

    XHCI_DBG_VALUE("hcd: device PDO created on port", dev->Port);
    IoInvalidateDeviceRelations(hc->RootHubPdo, BusRelations);
    return STATUS_SUCCESS;
}

static VOID hcdUnlink(PHCD_CONTROLLER hc, PHCD_DEVICE_PDO pdo)
{
    PHCD_DEVICE_PDO *at;
    KIRQL oldIrql;

    KeAcquireSpinLock(&hc->PdoListLock, &oldIrql);
    for (at = &hc->DevicePdos; *at != NULL; at = &(*at)->Next) {
        if (*at == pdo) {
            *at = pdo->Next;
            break;
        }
    }
    pdo->Listed = 0;
    pdo->Device = NULL;
    KeReleaseSpinLock(&hc->PdoListLock, oldIrql);
}

/* The device left: its PDO leaves the relations, PnP is told, and the
 * PDO's own remove deletes it. The device record goes with the slot; the
 * PDO keeps nothing of it. */
VOID HcdDevicePdoGone(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    PHCD_DEVICE_PDO pdo;

    if (dev == NULL || dev->Pdo == NULL) {
        return;
    }
    pdo = (PHCD_DEVICE_PDO)dev->Pdo->DeviceExtension;
    dev->Pdo = NULL;
    hcdUnlink(hc, pdo);
    if (hc->RootHubPdo != NULL) {
        IoInvalidateDeviceRelations(hc->RootHubPdo, BusRelations);
    }
}

/*
 * The root hub's BusRelations: every listed PDO, referenced, after whatever
 * the list already held. Returns the new list in handed-off pool, or NULL
 * when there is nothing to add or no pool. IRQL: PASSIVE_LEVEL.
 */
PDEVICE_RELATIONS HcdDevicePdoRelations(PHCD_CONTROLLER hc,
                                        PDEVICE_RELATIONS old)
{
    PDEVICE_RELATIONS rel;
    PHCD_DEVICE_PDO pdo;
    KIRQL oldIrql;
    ULONG listed;
    ULONG count;
    ULONG i;

    KeAcquireSpinLock(&hc->PdoListLock, &oldIrql);
    listed = 0;
    for (pdo = hc->DevicePdos; pdo != NULL; pdo = pdo->Next) {
        listed++;
    }
    count = (old != NULL) ? old->Count : 0;
    KeReleaseSpinLock(&hc->PdoListLock, oldIrql);

    rel = (PDEVICE_RELATIONS)HcdPoolAllocHandedOff(
        sizeof(DEVICE_RELATIONS) + (count + listed) * sizeof(PDEVICE_OBJECT));
    if (rel == NULL) {
        return NULL;
    }
    for (i = 0; i < count; i++) {
        rel->Objects[i] = old->Objects[i];
    }

    /* The list may have changed between the count and here; only the thread
     * links, and it links at most the count it can see, so what is copied is
     * bounded by what was allocated. */
    KeAcquireSpinLock(&hc->PdoListLock, &oldIrql);
    for (pdo = hc->DevicePdos; pdo != NULL && i < count + listed;
         pdo = pdo->Next) {
        rel->Objects[i++] = pdo->Common.Self;
        ObReferenceObject(pdo->Common.Self);
    }
    KeReleaseSpinLock(&hc->PdoListLock, oldIrql);
    rel->Count = i;
    return rel;
}

/* The root hub is going: every PDO still listed goes with it. IRQL:
 * PASSIVE_LEVEL (the root-hub FDO's remove). */
VOID HcdDevicePdoDeleteAll(PHCD_CONTROLLER hc)
{
    PHCD_DEVICE_PDO pdo;
    KIRQL oldIrql;

    for (;;) {
        KeAcquireSpinLock(&hc->PdoListLock, &oldIrql);
        pdo = hc->DevicePdos;
        if (pdo != NULL) {
            hc->DevicePdos = pdo->Next;
            pdo->Listed = 0;
            if (pdo->Device != NULL) {
                pdo->Device->Pdo = NULL;
            }
            pdo->Device = NULL;
        }
        KeReleaseSpinLock(&hc->PdoListLock, oldIrql);
        if (pdo == NULL) {
            break;
        }
        HcdPoolFree(pdo->Config);
        IoDeleteDevice(pdo->Common.Self);
    }
}

/* ----------------------------------------------------------------------- */
/* The device PDO's PnP and power                                           */
/* ----------------------------------------------------------------------- */

static NTSTATUS hcdDeviceCapabilities(PHCD_DEVICE_PDO pdo, PIRP irp)
{
    PIO_STACK_LOCATION stack;
    PDEVICE_CAPABILITIES caps;
    ULONG i;

    stack = IoGetCurrentIrpStackLocation(irp);
    caps = stack->Parameters.DeviceCapabilities.Capabilities;
    if (caps->Version != 1 || caps->Size < sizeof(DEVICE_CAPABILITIES)) {
        return HcdCompleteIrp(irp, STATUS_UNSUCCESSFUL, 0);
    }
    caps->DeviceD1 = FALSE;
    caps->DeviceD2 = FALSE;
    caps->LockSupported = FALSE;
    caps->EjectSupported = FALSE;
    caps->Removable = TRUE;
    caps->DockDevice = FALSE;
    caps->UniqueID = FALSE;
    caps->SilentInstall = FALSE;
    caps->RawDeviceOK = FALSE;
    caps->SurpriseRemovalOK = TRUE;
    caps->Address = pdo->Port;
    caps->UINumber = pdo->Port;
    caps->DeviceState[PowerSystemWorking] = PowerDeviceD0;
    for (i = PowerSystemSleeping1; i < PowerSystemMaximum; i++) {
        caps->DeviceState[i] = PowerDeviceD3;
    }
    caps->SystemWake = PowerSystemUnspecified;
    caps->DeviceWake = PowerDeviceUnspecified;
    return HcdCompleteIrp(irp, STATUS_SUCCESS, 0);
}

NTSTATUS HcdDevicePdoPnp(PHCD_DEVICE_PDO pdo, PIRP irp)
{
    PIO_STACK_LOCATION stack;
    PDEVICE_RELATIONS rel;
    PWCHAR text;

    stack = IoGetCurrentIrpStackLocation(irp);
    XHCI_DBG_VALUE("hcd: device PDO PnP minor", stack->MinorFunction);

    switch (stack->MinorFunction) {
    case IRP_MN_START_DEVICE:
        pdo->Common.PnpState = HCD_PNP_STARTED;
        return HcdCompleteIrp(irp, STATUS_SUCCESS, 0);

    case IRP_MN_QUERY_STOP_DEVICE:
    case IRP_MN_QUERY_REMOVE_DEVICE:
    case IRP_MN_CANCEL_STOP_DEVICE:
    case IRP_MN_CANCEL_REMOVE_DEVICE:
        return HcdCompleteIrp(irp, STATUS_SUCCESS, 0);

    case IRP_MN_STOP_DEVICE:
    case IRP_MN_SURPRISE_REMOVAL:
        pdo->Common.PnpState = HCD_PNP_STOPPED;
        return HcdCompleteIrp(irp, STATUS_SUCCESS, 0);

    case IRP_MN_REMOVE_DEVICE:
        pdo->Common.PnpState = HCD_PNP_REMOVED;
        if (!pdo->Listed) {
            HcdCompleteIrp(irp, STATUS_SUCCESS, 0);
            HcdPoolFree(pdo->Config);
            pdo->Config = NULL;
            IoDeleteDevice(pdo->Common.Self);
            return STATUS_SUCCESS;
        }
        return HcdCompleteIrp(irp, STATUS_SUCCESS, 0);

    case IRP_MN_QUERY_ID:
        return hcdDeviceQueryId(pdo, irp, stack->Parameters.QueryId.IdType);

    case IRP_MN_QUERY_DEVICE_TEXT:
        if (stack->Parameters.QueryDeviceText.DeviceTextType ==
            DeviceTextDescription) {
            text = hcdHandOff(L"USB Device", 11);
            if (text == NULL) {
                return HcdCompleteIrp(irp, STATUS_INSUFFICIENT_RESOURCES, 0);
            }
            return HcdCompleteIrp(irp, STATUS_SUCCESS, (ULONG_PTR)text);
        }
        return HcdCompleteIrp(irp, irp->IoStatus.Status,
                              irp->IoStatus.Information);

    case IRP_MN_QUERY_CAPABILITIES:
        return hcdDeviceCapabilities(pdo, irp);

    case IRP_MN_QUERY_PNP_DEVICE_STATE:
        return HcdCompleteIrp(irp, STATUS_SUCCESS,
                              irp->IoStatus.Information);

    case IRP_MN_QUERY_DEVICE_RELATIONS:
        if (stack->Parameters.QueryDeviceRelations.Type ==
            TargetDeviceRelation) {
            rel = (PDEVICE_RELATIONS)HcdPoolAllocHandedOff(
                sizeof(DEVICE_RELATIONS));
            if (rel == NULL) {
                return HcdCompleteIrp(irp, STATUS_INSUFFICIENT_RESOURCES, 0);
            }
            rel->Count = 1;
            rel->Objects[0] = pdo->Common.Self;
            ObReferenceObject(pdo->Common.Self);
            return HcdCompleteIrp(irp, STATUS_SUCCESS, (ULONG_PTR)rel);
        }
        return HcdCompleteIrp(irp, irp->IoStatus.Status,
                              irp->IoStatus.Information);

    default:
        return HcdCompleteIrp(irp, irp->IoStatus.Status,
                              irp->IoStatus.Information);
    }
}

NTSTATUS HcdDevicePdoPower(PHCD_DEVICE_PDO pdo, PIRP irp)
{
    PIO_STACK_LOCATION stack;
    NTSTATUS status;

    stack = IoGetCurrentIrpStackLocation(irp);
    status = irp->IoStatus.Status;
    if (stack->MinorFunction == IRP_MN_SET_POWER) {
        if (stack->Parameters.Power.Type == DevicePowerState) {
            pdo->Common.DevicePower =
                stack->Parameters.Power.State.DeviceState;
            (VOID)PoSetPowerState(pdo->Common.Self, DevicePowerState,
                                  stack->Parameters.Power.State);
        } else {
            pdo->Common.SystemPower =
                stack->Parameters.Power.State.SystemState;
        }
        status = STATUS_SUCCESS;
    } else if (stack->MinorFunction == IRP_MN_QUERY_POWER) {
        status = STATUS_SUCCESS;
    } else if (stack->MinorFunction == IRP_MN_WAIT_WAKE) {
        status = STATUS_NOT_SUPPORTED;
    }
    PoStartNextPowerIrp(irp);
    return HcdCompleteIrp(irp, status, irp->IoStatus.Information);
}
