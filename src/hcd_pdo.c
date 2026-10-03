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
 * bDeviceClass is 0 and there is one interface, from it. The instance id is
 * the device's place - the root port, with the Route String above it for a
 * device behind hubs (XhciHubInstanceKey) - until the serial string is read
 * (26-A.4 later, or 29-A.5). A hub is the bus's own and never reaches here
 * (hcd_hub.c, 27-A.1).
 *
 * A storage interface offering UAS gets one transport, chosen at creation
 * (31-A.3; xhci_xport.h, hcdXportDecide), and its hardware and compatible
 * ids follow it: only that transport's class ids. A device with no
 * transport it can run shows the project-owned XHCI98_NOXPORT hardware id
 * and no compatible id, and on a companion-paired root port is offered to
 * 29-A.5's hold (hcdXportRefusal, HcdHoldRequestUsb2).
 *
 * A composite device the bus splits (26-A.7; sections 10.8 and 10.9) gets no
 * PDO of its own: one function PDO per function, each an HCD_DEVICE_PDO with
 * Function set, its ids carrying &MI_nn (xhci_func.c), all sharing the one
 * device record and created, listed and unlisted together (the Group).
 *
 * The URB contract is 26-A.5's, in hcd_urb.c: IRP_MJ_INTERNAL_DEVICE_CONTROL
 * reaches it, and until the transfer path lands every URB is refused, so a
 * class driver binds and its start fails.
 *
 * IRQL: PASSIVE_LEVEL, except HcdDevicePdoList (<= DISPATCH_LEVEL inside the
 * lock it takes).
 */

#include "hcd.h"
#include "xhci_hw.h"
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

/* The class triple a device PDO's compatible ids carry: the device
 * descriptor's, or, when bDeviceClass is 0 and the configuration has
 * exactly one interface, that interface's (section 10.7). */
static VOID hcdClassTriple(PHCD_DEVICE_PDO pdo, PULONG cls, PULONG sub,
                           PULONG prot)
{
    ULONG found;
    ULONG count;
    ULONG at;
    ULONG len;

    *cls = pdo->DeviceDesc[4];
    *sub = pdo->DeviceDesc[5];
    *prot = pdo->DeviceDesc[6];
    if (*cls != 0) {
        return;
    }
    found = 0;
    count = 0;
    at = 0;
    while (at + 2 <= pdo->ConfigLength) {
        len = pdo->Config[at];
        if (len < 2 || at + len > pdo->ConfigLength) {
            break;
        }
        if (pdo->Config[at + 1] == 4 && len >= 9 && pdo->Config[at + 3] == 0) {
            if (count++ == 0) {
                found = at;
            }
        }
        at += len;
    }
    /* A device the bus did not split keeps its own (zero) class when it
     * has several interfaces: the first one's id would bind a class driver
     * to the whole device, which Windows 2000's usbaudio.sys survives only
     * as a function (usbaudio-crash.md, STOP 0x1E). */
    if (count == 1) {
        *cls = pdo->Config[found + 5];
        *sub = pdo->Config[found + 6];
        *prot = pdo->Config[found + 7];
    }
}

/* A function PDO's ids (design record 13 section 10.7), xhci_func.c's
 * ASCII widened. */
static NTSTATUS hcdFunctionQueryId(PHCD_DEVICE_PDO pdo, PIRP irp,
                                   BUS_QUERY_ID_TYPE type)
{
    char text[128];
    WCHAR buf[128];
    PWCHAR out;
    ULONG which;
    ULONG used;
    ULONG i;

    switch (type) {
    case BusQueryDeviceID:
        which = XHCI_FUNC_ID_DEVICE;
        break;
    case BusQueryHardwareIDs:
        which = XHCI_FUNC_ID_HARDWARE;
        break;
    case BusQueryCompatibleIDs:
        which = XHCI_FUNC_ID_COMPATIBLE;
        break;
    case BusQueryInstanceID:
        which = XHCI_FUNC_ID_INSTANCE;
        break;
    default:
        return HcdCompleteIrp(irp, irp->IoStatus.Status,
                              irp->IoStatus.Information);
    }
    used = 0;
    if (XhciFuncId(pdo->DeviceDesc, &pdo->Func, pdo->InstanceKey, which, text,
                   sizeof(text), &used) != XHCI_FUNC_OK) {
        return HcdCompleteIrp(irp, STATUS_UNSUCCESSFUL, 0);
    }
    for (i = 0; i < used; i++) {
        buf[i] = (WCHAR)(UCHAR)text[i];
    }
    out = hcdHandOff(buf, used);
    if (out == NULL) {
        return HcdCompleteIrp(irp, STATUS_INSUFFICIENT_RESOURCES, 0);
    }
    return HcdCompleteIrp(irp, STATUS_SUCCESS, (ULONG_PTR)out);
}

/* The hardware or compatible ids of a PDO whose storage transport the bus
 * chose (31-A.3; xhci_xport.c): only that transport's class ids, and no
 * VID/PID-only hardware id under UAS. A refused interface answers its
 * compatible-id query with none, as an id type the PDO does not handle. */
static NTSTATUS hcdXportQueryId(PHCD_DEVICE_PDO pdo, PIRP irp, ULONG which)
{
    char text[160];
    WCHAR buf[160];
    PWCHAR out;
    ULONG used;
    ULONG answer;
    ULONG i;

    used = 0;
    answer = XhciXportId(pdo->DeviceDesc, &pdo->Xport,
                         pdo->Function ? pdo->Func.FirstInterface
                                       : XHCI_XPORT_NO_MI,
                         which, text, sizeof(text), &used);
    if (answer == XHCI_XPORT_NO_IDS) {
        return HcdCompleteIrp(irp, irp->IoStatus.Status,
                              irp->IoStatus.Information);
    }
    if (answer != XHCI_XPORT_OK) {
        return HcdCompleteIrp(irp, STATUS_UNSUCCESSFUL, 0);
    }
    for (i = 0; i < used; i++) {
        buf[i] = (WCHAR)(UCHAR)text[i];
    }
    out = hcdHandOff(buf, used);
    if (out == NULL) {
        return HcdCompleteIrp(irp, STATUS_INSUFFICIENT_RESOURCES, 0);
    }
    return HcdCompleteIrp(irp, STATUS_SUCCESS, (ULONG_PTR)out);
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
    const WCHAR *prefix;

    if (pdo->Xport.Transport != XHCI_XPORT_NONE) {
        if (type == BusQueryHardwareIDs) {
            return hcdXportQueryId(pdo, irp, XHCI_XPORT_ID_HARDWARE);
        }
        if (type == BusQueryCompatibleIDs) {
            return hcdXportQueryId(pdo, irp, XHCI_XPORT_ID_COMPATIBLE);
        }
    }
    if (pdo->Function) {
        return hcdFunctionQueryId(pdo, irp, type);
    }
    prefix = L"USB\\VID_";
    vid = (ULONG)pdo->DeviceDesc[8] | ((ULONG)pdo->DeviceDesc[9] << 8);
    pid = (ULONG)pdo->DeviceDesc[10] | ((ULONG)pdo->DeviceDesc[11] << 8);
    rev = (ULONG)pdo->DeviceDesc[12] | ((ULONG)pdo->DeviceDesc[13] << 8);
    n = 0;

    switch (type) {
    case BusQueryDeviceID:
        hcdPut(buf, &n, prefix);
        hcdPutHex(buf, &n, vid, 4);
        hcdPut(buf, &n, L"&PID_");
        hcdPutHex(buf, &n, pid, 4);
        buf[n++] = 0;
        break;

    case BusQueryHardwareIDs:
        hcdPut(buf, &n, prefix);
        hcdPutHex(buf, &n, vid, 4);
        hcdPut(buf, &n, L"&PID_");
        hcdPutHex(buf, &n, pid, 4);
        hcdPut(buf, &n, L"&REV_");
        hcdPutHex(buf, &n, rev, 4);
        buf[n++] = 0;
        hcdPut(buf, &n, prefix);
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
        hcdPutDecimal(buf, &n, pdo->InstanceKey);
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

/*
 * THE LIFECYCLE (Codex review of batch (b), round 1, findings 1-4 and 12).
 * Only the controller thread lists and unlists a PDO; BusRelations answers
 * mark it, under PdoListLock, in the same hold that copies the list. A PDO is
 *
 *   listed    - on DevicePdos while its device is present; every relations
 *               answer carries it and sets Reported;
 *   gone      - on GonePdos once its device left; the next relations answer
 *               omits it and sets MissingReported;
 *   deleted   - by its own IRP_MN_REMOVE_DEVICE once it is gone and either
 *               reported missing or never reported at all; by the thread at
 *               once when it leaves before PnP ever saw it; or by its
 *               parent's removal when PnP has already removed it;
 *   orphaned  - when its parent goes first while it still awaits a REMOVE
 *               (a surprise removal with handles open): it leaves both lists,
 *               forgets the controller, and deletes itself at that REMOVE.
 *
 * A REMOVE while still listed (a disable of the device) keeps the PDO, the
 * WDM bus rule. A deletion is fed back to the port's machine as PDO_REMOVED
 * (the port bit PortPdoRemoved), and a start as PDO_STARTED, so the machine
 * waits in Gone for the PDO it reported and reaches Bound (design record 13
 * section 5.3).
 */

/* A port's handshake bit, under the controller lock. IRQL: <= DISPATCH. */
static VOID hcdPortNotify(PHCD_CONTROLLER hc, PULONG bits, ULONG port)
{
    KIRQL oldIrql;

    if (port == 0 || port > HCD_PORT_COUNT) {
        return;
    }
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    bits[(port - 1) / 32UL] |= 1UL << ((port - 1) % 32UL);
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    HcdThreadWake(hc);
}

/* The one IoDeleteDevice of a PDO. Deleted stays readable while a reference
 * keeps the object, so a later REMOVE completes and deletes nothing again,
 * and touches no controller (Codex review of batch (b), round 2, finding 1). */
static VOID hcdDeletePdo(PHCD_DEVICE_PDO pdo)
{
    /* Its refusal timer and DPC live in the extension (hcd_io.c). */
    HcdIoRefusedDrain(pdo);
    pdo->Deleted = 1;
    pdo->Controller = NULL;
    HcdPoolFree(pdo->Config);
    pdo->Config = NULL;
    IoDeleteDevice(pdo->Common.Self);
}

/*
 * The storage transport for a new PDO (roadmap-hcd.md 31-A.3; xhci_xport.h),
 * decided once from its own configuration copy: a device PDO's one interface
 * when the device's class is its interfaces' (section 10.7), or a function's
 * one interface when no IAD groups it. Anything else keeps section 10.7's ids
 * (XHCI_XPORT_NONE). Each branch but NOT_UAS is counted and traced. Thread
 * only, PASSIVE_LEVEL.
 */
static VOID hcdXportDecide(PHCD_CONTROLLER hc, PHCD_DEVICE_PDO pdo,
                           ULONG flags)
{
    ULONG iface;

    pdo->Xport.Transport = XHCI_XPORT_NONE;
    pdo->Xport.Why = XHCI_XPORT_WHY_NOT_UAS;
    if (pdo->Function) {
        if (pdo->Func.InterfaceCount != 1 || pdo->Func.IadOffset != 0) {
            return;
        }
        iface = pdo->Func.FirstInterface;
    } else if (pdo->DeviceDesc[4] != 0 ||
               !XhciXportSingleInterface(pdo->Config, pdo->ConfigLength,
                                         &iface)) {
        return;
    }
    if (pdo->SpeedClass >= XHCI_SPEED_SUPER) {
        flags |= XHCI_XPORT_F_SUPERSPEED;
    }
    if (XhciXportChoose(pdo->Config, pdo->ConfigLength, iface, flags,
                        &pdo->Xport) != XHCI_XPORT_OK) {
        pdo->Xport.Transport = XHCI_XPORT_NONE;
        pdo->Xport.Why = XHCI_XPORT_WHY_NOT_UAS;
        return;
    }
    if (pdo->Xport.Why == XHCI_XPORT_WHY_NOT_UAS) {
        return;
    }
    hc->XportDecisions[pdo->Xport.Why]++;
    XHCI_DBG_VALUE("hcd: storage transport, port/transport/why/alternate",
                   (pdo->Port << 16) | (pdo->Xport.Transport << 12) |
                       (pdo->Xport.Why << 8) | (pdo->Xport.Alternate & 0xFFUL));
}

/*
 * A device one of whose PDOs has no transport (31-A.3: UAS-only at
 * SuperSpeed on a controller that does not stream), once per device, before
 * any PDO is listed. Where it sits decides (XhciXportRefusedAt): on a
 * companion-paired root port 29-A.5's hold is asked to send it back to USB
 * 2.0 (HcdHoldRequestUsb2); on a root port with no companion, behind a
 * SuperSpeed hub, or when the hold is not taken, it is refused in place -
 * its PDOs list with the XHCI98_NOXPORT hardware id and no compatible ids,
 * its hub port and siblings untouched. Each place is counted. Returns
 * nonzero when the hold was taken. Thread only, PASSIVE_LEVEL.
 */
static ULONG hcdXportRefusal(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                             PHCD_DEVICE_PDO first)
{
    PHCD_DEVICE_PDO pdo;
    ULONG companion;
    ULONG at;

    for (pdo = first; pdo != NULL; pdo = pdo->Sibling) {
        if (pdo->Xport.Transport == XHCI_XPORT_REFUSED) {
            break;
        }
    }
    if (pdo == NULL) {
        return 0;
    }
    companion = 0;
    if (dev->Route == 0 && dev->Port >= 1 &&
        dev->Port <= XHCI_MAX_ROOT_PORTS) {
        companion = (ULONG)hc->Hc.PortMap.Companion[dev->Port - 1];
    }
    at = XhciXportRefusedAt(dev->Route, companion);
    hc->XportRefusedAt[at]++;
    XHCI_DBG_VALUE("hcd: UAS-only at SuperSpeed without streams, port/where",
                   (dev->Port << 16) | at);
    if (at != XHCI_XPORT_AT_ROOT_COMPANION) {
        return 0;
    }
    if (HcdHoldRequestUsb2(hc, dev, HCD_HOLD_WHY_UAS_NO_STREAMS)) {
        return 1;
    }
    hc->XportHoldsNotTaken++;
    return 0;
}

/*
 * STUB of the 31-A.3 / 29-A.5 call boundary (hcd.h): takes no hold, so the
 * device is refused in place. Phase 29's executor (branch p29) replaces this
 * definition at integration - delete it then. Thread only, PASSIVE_LEVEL.
 */
ULONG HcdHoldRequestUsb2(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                         ULONG reason)
{
    UNREFERENCED_PARAMETER(hc);
    UNREFERENCED_PARAMETER(dev);
    UNREFERENCED_PARAMETER(reason);
    return 0;
}

/* One PDO, not yet listed: the device's (func NULL) with the whole
 * configuration, or a function's with its filtered one. The descriptors are
 * copied into the PDO, which outlives the device record. IoCreateDevice
 * zeroes the extension. */
static NTSTATUS hcdPdoNew(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                          const XHCI_FUNC *func, ULONG xportFlags,
                          PHCD_DEVICE_PDO *made)
{
    WCHAR nameBuffer[40];
    UNICODE_STRING name;
    PDEVICE_OBJECT obj;
    PHCD_DEVICE_PDO pdo;
    NTSTATUS status;
    ULONG serial;
    ULONG length;
    ULONG n;
    ULONG i;

    *made = NULL;
    length = dev->ConfigLength;
    if (func != NULL &&
        XhciFuncConfig(dev->Config, dev->ConfigLength, func, NULL, 0,
                       &length) != XHCI_FUNC_OK) {
        return STATUS_UNSUCCESSFUL;
    }

    serial = (ULONG)InterlockedIncrement(&hcdDevicePdoSerial);
    if (serial == 0) {
        serial = (ULONG)InterlockedIncrement(&hcdDevicePdoSerial);
    }
    n = 0;
    hcdPut(nameBuffer, &n, L"\\Device\\XHCI98DEV");
    hcdPutDecimal(nameBuffer, &n, serial);
    nameBuffer[n] = 0;
    RtlInitUnicodeString(&name, nameBuffer);
    status = IoCreateDevice(HcdDriverObject, sizeof(HCD_DEVICE_PDO), &name,
                            FILE_DEVICE_UNKNOWN, 0, FALSE, &obj);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    pdo = (PHCD_DEVICE_PDO)obj->DeviceExtension;
    pdo->Config = (PUCHAR)HcdPoolAlloc(length);
    if (pdo->Config == NULL) {
        /* Without the configuration the compatible ids would be wrong for
         * a device whose class is its interfaces' (round 1, finding 13). */
        IoDeleteDevice(obj);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    if (func == NULL) {
        for (i = 0; i < length; i++) {
            pdo->Config[i] = dev->Config[i];
        }
    } else {
        (VOID)XhciFuncConfig(dev->Config, dev->ConfigLength, func,
                             pdo->Config, length, &length);
        pdo->Function = 1;
        pdo->Func = *func;
        pdo->InterfaceMask = func->InterfaceMask;
    }
    pdo->ConfigLength = length;
    pdo->Common.Kind = HCD_KIND_DEVICE_PDO;
    pdo->Common.Self = obj;
    HcdIoRefusedInit(pdo);
    pdo->Common.PnpState = HCD_PNP_ADDED;
    pdo->Common.DevicePower = PowerDeviceD0;
    pdo->Common.SystemPower = PowerSystemWorking;
    pdo->Controller = hc;
    pdo->Device = dev;
    pdo->Serial = serial;
    pdo->Port = dev->Location;
    pdo->InstanceKey = XhciHubInstanceKey(dev->Port, dev->Route);
    pdo->Speed = dev->Speed;
    pdo->SpeedClass = XHCI_SPEED_UNKNOWN;
    (VOID)XhciPortSpeedClass(&hc->Hc.PortMap, dev->Port, dev->Speed,
                             &pdo->SpeedClass);
    for (i = 0; i < sizeof(pdo->DeviceDesc); i++) {
        pdo->DeviceDesc[i] = dev->DeviceDesc[i];
    }
    pdo->Group = serial;
    hcdXportDecide(hc, pdo, xportFlags);

    obj->Flags |= DO_POWER_PAGABLE;
    obj->Flags &= ~DO_DEVICE_INITIALIZING;
    *made = pdo;
    return STATUS_SUCCESS;
}

/*
 * The PDO(s) for an enumerated device: one device PDO, or - for a device the
 * bus splits (xhci_func.c, design record 13 section 10.8) - the device
 * configured by the bus and one function PDO per function, all created
 * before any is listed, listed in one hold of PdoListLock, and announced by
 * one relations invalidation, so PnP meets them together. On a failure
 * nothing is listed and the port's machine fails; a device the bus already
 * configured stays so until the slot is disabled. Thread only (the bus's
 * configure is a thread control transfer).
 */
NTSTATUS HcdDevicePdoCreate(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    XHCI_FUNC_SET set;
    PHCD_DEVICE_PDO first;
    PHCD_DEVICE_PDO last;
    PHCD_DEVICE_PDO pdo;
    KIRQL oldIrql;
    NTSTATUS status;
    ULONG xportFlags;
    ULONG count;
    ULONG i;

    if (hc->RootHubPdo == NULL || !hc->RootHubStarted ||
        dev->ConfigLength == 0 || dev->Config == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }
    if (XhciFuncSplit(dev->DeviceDesc, dev->Config, dev->ConfigLength,
                      &set) != XHCI_FUNC_OK) {
        set.Count = 0;
    }
    if (set.Count != 0 && !HcdCfgParentConfigure(hc, dev)) {
        XHCI_DBG_VALUE("hcd: split device not configured, port", dev->Port);
        return STATUS_UNSUCCESSFUL;
    }
    /* The transport inputs that are the controller's (31-A.3): whether it
     * streams, and the force value, read here so a change takes effect at
     * the next enumeration. */
    xportFlags = 0;
    if (XhciStreamHcEntries(XhciBarReader(&hc->Hc, XHCI_CAP_HCCPARAMS1)) !=
        0) {
        xportFlags |= XHCI_XPORT_F_HC_STREAMS;
    }
    if (HcdCtlForceBulkOnly(hc)) {
        xportFlags |= XHCI_XPORT_F_FORCE_BOT;
    }
    count = (set.Count != 0) ? set.Count : 1;
    first = NULL;
    last = NULL;
    status = STATUS_SUCCESS;
    for (i = 0; i < count; i++) {
        status = hcdPdoNew(hc, dev, (set.Count != 0) ? &set.Func[i] : NULL,
                           xportFlags, &pdo);
        if (!NT_SUCCESS(status)) {
            goto cleanup;
        }
        if (first == NULL) {
            first = pdo;
        } else {
            pdo->Group = first->Group;
            last->Sibling = pdo;
        }
        last = pdo;
    }
    if (hcdXportRefusal(hc, dev, first)) {
        /* The device is leaving for the USB 2.0 companion (29-A.5). */
        status = STATUS_DEVICE_REMOVED;
        goto cleanup;
    }
    dev->Pdo = first->Common.Self;
    dev->PdoGroup = first->Group;

    KeAcquireSpinLock(&hc->PdoListLock, &oldIrql);
    for (pdo = first; pdo != NULL; pdo = pdo->Sibling) {
        pdo->Next = hc->DevicePdos;
        hc->DevicePdos = pdo;
        pdo->Listed = 1;
    }
    KeReleaseSpinLock(&hc->PdoListLock, oldIrql);

    XHCI_DBG_VALUE("hcd: device PDOs created, port/count",
                   (dev->Port << 16) | count);
    /* The first guest run reads the grouping off these (design record 13
     * section 10.10: the real units' interface numbering is unread). */
    for (i = 0; i < set.Count; i++) {
        XHCI_DBG_VALUE("hcd: function PDO, port/MI",
                       (dev->Port << 16) | set.Func[i].FirstInterface);
        XHCI_DBG_VALUE("hcd: function PDO, interface mask",
                       set.Func[i].InterfaceMask);
        XHCI_DBG_VALUE("hcd: function PDO, class/subclass/protocol",
                       (set.Func[i].Class << 16) |
                           (set.Func[i].SubClass << 8) |
                           set.Func[i].Protocol);
    }
    IoInvalidateDeviceRelations(hc->RootHubPdo, BusRelations);
    return STATUS_SUCCESS;

cleanup:
    while (first != NULL) {
        pdo = first;
        first = first->Sibling;
        hcdDeletePdo(pdo);
    }
    return status;
}

static VOID hcdUnlinkLocked(PHCD_DEVICE_PDO *head, PHCD_DEVICE_PDO pdo)
{
    PHCD_DEVICE_PDO *at;

    for (at = head; *at != NULL; at = &(*at)->Next) {
        if (*at == pdo) {
            *at = pdo->Next;
            pdo->Next = NULL;
            return;
        }
    }
}

/*
 * The device left. Every PDO of it - one device PDO, or every function PDO
 * of a split device - leaves the relations in one hold of PdoListLock, so the
 * next relations answer omits them all together (design record 13 sections
 * 10.5 step 3 and 10.9, CYCLE_PORT); a PDO PnP never saw is deleted here and
 * now. Returns 0 when nothing remains for the port to wait for (no PDO, or
 * every one deleted at once), otherwise the group serial the port must wait
 * on until the last of them is deleted (HcdDevicePdoExists; the
 * PortPdoRemoved bit only says when to look - round 2, finding 2). The
 * device record goes with the slot; the PDOs keep nothing of it. Thread, or
 * the stop.
 */
ULONG HcdDevicePdoGone(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    PHCD_DEVICE_PDO pdo;
    PHCD_DEVICE_PDO next;
    PHCD_DEVICE_PDO doomed;
    KIRQL oldIrql;
    ULONG waits;
    ULONG group;

    if (dev == NULL || dev->Pdo == NULL) {
        return 0;
    }
    pdo = (PHCD_DEVICE_PDO)dev->Pdo->DeviceExtension;
    dev->Pdo = NULL;
    group = pdo->Group;
    doomed = NULL;
    waits = 0;

    /* The chain is whole here: every PDO on it is still listed, and a
     * listed PDO is never deleted by its own REMOVE (hcdPdoRemoved); the
     * parent's release, the one other deleter, clears dev->Pdo first. */
    KeAcquireSpinLock(&hc->PdoListLock, &oldIrql);
    for (; pdo != NULL; pdo = next) {
        next = pdo->Sibling;
        pdo->Sibling = NULL;
        hcdUnlinkLocked(&hc->DevicePdos, pdo);
        pdo->Listed = 0;
        pdo->Device = NULL;
        if (!pdo->Reported) {
            pdo->Next = doomed;
            doomed = pdo;
        } else {
            pdo->Next = hc->GonePdos;
            hc->GonePdos = pdo;
            waits = 1;
        }
    }
    KeReleaseSpinLock(&hc->PdoListLock, oldIrql);

    while (doomed != NULL) {
        pdo = doomed;
        doomed = pdo->Next;
        pdo->Next = NULL;
        hcdDeletePdo(pdo);
    }
    if (!waits) {
        return 0;
    }
    if (hc->RootHubPdo != NULL) {
        IoInvalidateDeviceRelations(hc->RootHubPdo, BusRelations);
    }
    return group;
}

/* Whether any PDO of that group is still on either list - not yet deleted
 * nor released. A lone device PDO's group is its own serial. 0 for serial
 * 0. IRQL: <= DISPATCH_LEVEL. */
ULONG HcdDevicePdoExists(PHCD_CONTROLLER hc, ULONG serial)
{
    PHCD_DEVICE_PDO pdo;
    KIRQL oldIrql;
    ULONG found;

    if (serial == 0) {
        return 0;
    }
    found = 0;
    KeAcquireSpinLock(&hc->PdoListLock, &oldIrql);
    for (pdo = hc->GonePdos; pdo != NULL && !found; pdo = pdo->Next) {
        found = pdo->Group == serial;
    }
    for (pdo = hc->DevicePdos; pdo != NULL && !found; pdo = pdo->Next) {
        found = pdo->Group == serial;
    }
    KeReleaseSpinLock(&hc->PdoListLock, oldIrql);
    return found;
}

/*
 * The root hub's BusRelations: whatever the list already held, then every
 * listed PDO, referenced and marked Reported; every gone PDO is marked
 * MissingReported, since this answer omits it. One hold of PdoListLock for
 * the count, the allocation and the copy (round 1, finding 3). NULL when the
 * pool has nothing. IRQL: PASSIVE_LEVEL (the allocation is NonPagedPool, so
 * it is legal under the spin lock).
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

    count = (old != NULL) ? old->Count : 0;
    KeAcquireSpinLock(&hc->PdoListLock, &oldIrql);
    listed = 0;
    for (pdo = hc->DevicePdos; pdo != NULL; pdo = pdo->Next) {
        listed++;
    }
    rel = (PDEVICE_RELATIONS)HcdPoolAllocHandedOff(
        sizeof(DEVICE_RELATIONS) + (count + listed) * sizeof(PDEVICE_OBJECT));
    if (rel != NULL) {
        for (i = 0; i < count; i++) {
            rel->Objects[i] = old->Objects[i];
        }
        for (pdo = hc->DevicePdos; pdo != NULL; pdo = pdo->Next) {
            rel->Objects[i++] = pdo->Common.Self;
            ObReferenceObject(pdo->Common.Self);
            pdo->Reported = 1;
        }
        rel->Count = i;
        for (pdo = hc->GonePdos; pdo != NULL; pdo = pdo->Next) {
            pdo->MissingReported = 1;
        }
    }
    KeReleaseSpinLock(&hc->PdoListLock, oldIrql);
    return rel;
}

/* Until no dispatch is inside the PDO's internal-IOCTL entry. IRQL:
 * PASSIVE_LEVEL. */
static VOID hcdWaitBusy(PHCD_DEVICE_PDO pdo)
{
    LARGE_INTEGER due;

    while (pdo->Busy != 0) {
        HcdRelativeMs(&due, 1);
        (VOID)KeDelayExecutionThread(KernelMode, FALSE, &due);
    }
}

/*
 * The parent is going (the root hub's remove, or the controller's): every
 * PDO left on either list is settled - deleted when PnP has removed it or
 * never knew it, orphaned when it still awaits its REMOVE (round 1, finding
 * 2). The thread has detached the bus first, so nothing lists more PDOs
 * meanwhile. IRQL: PASSIVE_LEVEL.
 */
VOID HcdDevicePdoReleaseAll(PHCD_CONTROLLER hc)
{
    PHCD_DEVICE_PDO pdo;
    KIRQL oldIrql;
    ULONG deleteIt;

    for (;;) {
        KeAcquireSpinLock(&hc->PdoListLock, &oldIrql);
        pdo = hc->DevicePdos;
        if (pdo != NULL) {
            hc->DevicePdos = pdo->Next;
        } else {
            pdo = hc->GonePdos;
            if (pdo != NULL) {
                hc->GonePdos = pdo->Next;
            }
        }
        deleteIt = 0;
        if (pdo != NULL) {
            pdo->Next = NULL;
            pdo->Listed = 0;
            if (pdo->Device != NULL) {
                pdo->Device->Pdo = NULL;
            }
            pdo->Device = NULL;
            deleteIt = pdo->RemoveReceived || !pdo->Reported;
            pdo->Controller = NULL;
        }
        KeReleaseSpinLock(&hc->PdoListLock, oldIrql);
        if (pdo == NULL) {
            break;
        }
        /* A dispatch that read the controller before the line above is
         * still inside hcd_urb.c; the controller's storage outlives it
         * (Codex review of batch (c), round 1, finding 1). */
        hcdWaitBusy(pdo);
        if (deleteIt) {
            hcdDeletePdo(pdo);
        }
    }
}

/* ----------------------------------------------------------------------- */
/* The device PDO's PnP and power                                           */
/* ----------------------------------------------------------------------- */

/* The refusals pended here (hcd_io.c, HcdIoRefuseLater), at a REMOVE only:
 * each completes at a clock tick, but a client may resubmit from its
 * completion routine until its own stack hears of the stop, so a STOP or
 * SURPRISE_REMOVAL that waited for them might never return (Codex review
 * of batch (c), round 8, finding 3). A REMOVE comes after every driver
 * above has handled it and stopped submitting. IRQL: PASSIVE_LEVEL. */
static VOID hcdPdoRefusalsWait(PHCD_DEVICE_PDO pdo, ULONG removing)
{
    if (removing) {
        HcdIoRefusedDrain(pdo);
    }
}

/*
 * Before a stop or a removal completes, every URB this PDO's stack has
 * pended here completes: a client driver unloads once its REMOVE is done,
 * and a completion arriving after that runs code that is gone (c4-2k,
 * 2026-10-03: bugcheck 0xCE in hidusb+0xb0a on an unplug, its interrupt
 * read completed by the device-gone drain after the REMOVE). A device
 * still present has its pipes aborted by the thread (AbortAll); a gone one
 * is drained by the thread's free. IRQL: PASSIVE_LEVEL.
 */
static VOID hcdPdoQuiesce(PHCD_DEVICE_PDO pdo, ULONG removing)
{
    PHCD_CONTROLLER hc;
    PHCD_USB_DEVICE dev;
    LARGE_INTEGER due;
    KIRQL oldIrql;

    /* Admission closes first and the dispatches already inside are
     * waited out, so every URB this stack will ever pend here is counted
     * before the count is read (Codex review of batch (c), round 3,
     * finding 1). */
    pdo->Closing = 1;
    while (pdo->Busy != 0) {
        HcdRelativeMs(&due, 1);
        (VOID)KeDelayExecutionThread(KernelMode, FALSE, &due);
    }
    if (pdo->UrbsPending == 0) {
        hcdPdoRefusalsWait(pdo, removing);
        return;
    }
    hc = pdo->Controller;
    if (hc != NULL) {
        /* Referenced while the PDO still names it, as a URB's dispatch
         * does, so the record cannot be freed under the flag (round 3,
         * finding 2). */
        KeAcquireSpinLock(&hc->PdoListLock, &oldIrql);
        dev = pdo->Device;
        if (dev != NULL) {
            (VOID)InterlockedIncrement(&dev->Refs);
        }
        KeReleaseSpinLock(&hc->PdoListLock, oldIrql);
        if (dev != NULL) {
            if (pdo->Function) {
                /* Its own requests only, EP0's included: its siblings
                 * share the device and keep running (design record 13
                 * section 10.9). Its endpoints stay enabled with nothing
                 * on them until its next SELECT_CONFIGURATION, or the
                 * release a REMOVE queues (hcdPdoFunctionRelease). */
                HcdIoCancelPdo(hc, dev, pdo);
            } else {
                XhciControllerLockAcquire(&hc->Hc, &oldIrql);
                dev->AbortAll = 1;
                hc->CancelWork = 1;
                XhciControllerLockRelease(&hc->Hc, oldIrql);
                HcdThreadWake(hc);
            }
            (VOID)InterlockedDecrement(&dev->Refs);
        }
    }
    while (pdo->UrbsPending != 0 &&
           !(hc != NULL && hc->CommonBufferPinned)) {
        HcdRelativeMs(&due, 1);
        (VOID)KeDelayExecutionThread(KernelMode, FALSE, &due);
    }
    hcdPdoRefusalsWait(pdo, removing);
}

/* A removed function's endpoints and alternates handed back while its
 * siblings run on (hcd_cfg.c, HcdCfgReleaseFunction): a REMOVE without a
 * prior unconfigure would otherwise leave an audio function's periodic
 * bandwidth reserved for as long as the device stays (Codex review of
 * batch (c), round 19, finding 4). After hcdPdoQuiesce, so its URBs are
 * complete. IRQL: PASSIVE_LEVEL. */
static VOID hcdPdoFunctionRelease(PHCD_DEVICE_PDO pdo)
{
    PHCD_CONTROLLER hc;
    PHCD_USB_DEVICE dev;
    KIRQL oldIrql;

    hc = pdo->Controller;
    if (!pdo->Function || hc == NULL) {
        return;
    }
    KeAcquireSpinLock(&hc->PdoListLock, &oldIrql);
    dev = pdo->Device;
    if (dev != NULL) {
        (VOID)InterlockedIncrement((PLONG)&dev->Refs);
    }
    KeReleaseSpinLock(&hc->PdoListLock, oldIrql);
    if (dev != NULL) {
        HcdCfgReleaseFunction(hc, dev, pdo->InterfaceMask);
        (VOID)InterlockedDecrement((PLONG)&dev->Refs);
    }
}

/* A start after a remove (an enable after a disable) makes the PDO PnP's
 * again, so the remove it received no longer lets a parent's release delete
 * it. Then PDO_STARTED for the port's machine; an orphan or a gone PDO has
 * no port left to tell. IRQL: PASSIVE_LEVEL. */
static VOID hcdPdoStarted(PHCD_DEVICE_PDO pdo)
{
    PHCD_CONTROLLER hc;
    KIRQL oldIrql;
    ULONG listed;

    pdo->Closing = 0;
    hc = pdo->Controller;
    if (hc == NULL) {
        pdo->RemoveReceived = 0;
        return;
    }
    KeAcquireSpinLock(&hc->PdoListLock, &oldIrql);
    pdo->RemoveReceived = 0;
    listed = pdo->Listed;
    KeReleaseSpinLock(&hc->PdoListLock, oldIrql);
    if (listed) {
        hcdPortNotify(hc, hc->PortPdoStarted, pdo->Port);
    }
}

/*
 * IRP_MN_REMOVE_DEVICE, already completed. The lifecycle above decides: an
 * orphan deletes itself; a gone PDO that PnP has seen reported missing (or
 * never saw) is unlinked and deleted, once, and its port told PDO_REMOVED; a
 * listed PDO stays (a disable), as does a gone one whose absence PnP has not
 * yet been told - its REMOVE after the next relations answer, or the
 * parent's release, deletes it. IRQL: PASSIVE_LEVEL.
 */
static VOID hcdPdoRemoved(PHCD_DEVICE_PDO pdo)
{
    PHCD_CONTROLLER hc;
    KIRQL oldIrql;
    ULONG deleteIt;
    ULONG port;

    if (pdo->Deleted) {
        /* A REMOVE again while a reference keeps the object: already
         * deleted, and the controller may be gone. */
        return;
    }
    hc = pdo->Controller;
    if (hc == NULL) {
        hcdDeletePdo(pdo);
        return;
    }
    deleteIt = 0;
    KeAcquireSpinLock(&hc->PdoListLock, &oldIrql);
    if (pdo->Controller == NULL) {
        deleteIt = 2;
    } else {
        pdo->RemoveReceived = 1;
        if (!pdo->Listed && (pdo->MissingReported || !pdo->Reported)) {
            hcdUnlinkLocked(&hc->GonePdos, pdo);
            deleteIt = 1;
        }
    }
    KeReleaseSpinLock(&hc->PdoListLock, oldIrql);
    if (deleteIt == 0) {
        return;
    }
    port = pdo->Port;
    hcdDeletePdo(pdo);
    if (deleteIt == 1) {
        hcdPortNotify(hc, hc->PortPdoRemoved, port);
    }
}

static NTSTATUS hcdDeviceCapabilities(PHCD_DEVICE_PDO pdo, PIRP irp)
{
    PIO_STACK_LOCATION stack;
    PDEVICE_CAPABILITIES caps;
    ULONG i;

    stack = IoGetCurrentIrpStackLocation(irp);
    caps = stack->Parameters.DeviceCapabilities.Capabilities;
    /*
     * Every field written here precedes D1Latency, so a structure at least
     * that long is enough. Windows 98 SE's hidclass.sys sends a shorter
     * DEVICE_CAPABILITIES than the Windows 2000 DDK's (inferred: its start
     * failed without ever reaching this PDO, 2026-10-03, c6-98), and a
     * refusal here fails the client's start - Code 10.
     */
    XHCI_DBG_VALUE("hcd: capabilities query, version/size",
                   ((ULONG)caps->Version << 16) | caps->Size);
    /* Windows 98 SE's hidclass.sys sends Version 0 and Size 0 - the fields
     * left unset - in the query its start depends on (measured, c7-98,
     * 2026-10-03: every query from the PnP manager read version 1 size
     * 0x40, hidclass's read 0/0, and refusing it was the Code 10). A
     * caller that sets nothing still passes a whole structure, and every
     * field written here is in its fixed prefix; only a caller that states
     * a size too short for them is refused. */
    if (!(caps->Version == 0 && caps->Size == 0) &&
        (caps->Version < 1 ||
         caps->Size < FIELD_OFFSET(DEVICE_CAPABILITIES, D1Latency))) {
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
    caps->Address = pdo->InstanceKey;
    caps->UINumber = pdo->InstanceKey;
    caps->DeviceState[PowerSystemWorking] = PowerDeviceD0;
    for (i = PowerSystemSleeping1; i < PowerSystemMaximum; i++) {
        caps->DeviceState[i] = PowerDeviceD3;
    }
    caps->SystemWake = PowerSystemUnspecified;
    caps->DeviceWake = PowerDeviceUnspecified;
    return HcdCompleteIrp(irp, STATUS_SUCCESS, 0);
}

/*
 * A QUERY_INTERFACE no PDO answers yet (USBDI's is roadmap 28-A.1's, asked
 * for by XP onward only), traced once per interface GUID by its first ULONG,
 * so a guest run shows what a class driver looked for. Past the table's room
 * every query is traced. PnP IRPs come one at a time per device; two devices
 * at once can at worst trace one GUID twice.
 */
static VOID hcdQueryInterfaceTrace(PIO_STACK_LOCATION stack)
{
#ifdef XHCI_DBG_TRACE
    static ULONG seen[8];
    static LONG count;
    ULONG data1;
    ULONG limit;
    LONG slot;
    ULONG i;

    if (stack->Parameters.QueryInterface.InterfaceType == NULL) {
        return;
    }
    data1 = stack->Parameters.QueryInterface.InterfaceType->Data1;
    /* The slot is claimed interlocked: two PDOs' queries on two
     * processors share the table (Codex review of batch (c), round 16,
     * finding 6). */
    limit = (ULONG)count;
    if (limit > 8) {
        limit = 8;
    }
    for (i = 0; i < limit; i++) {
        if (seen[i] == data1) {
            return;
        }
    }
    if (count < 8) {
        /* Reserved only while the table has room, so the count stops a
         * little past 8 rather than wrapping (round 17, finding 3). */
        slot = InterlockedIncrement(&count) - 1;
        if (slot >= 0 && slot < 8) {
            seen[slot] = data1;
        }
    }
    XHCI_DBG_VALUE("hcd: QUERY_INTERFACE not answered, GUID Data1", data1);
    XHCI_DBG_VALUE("hcd: QUERY_INTERFACE not answered, version/size",
                   ((ULONG)stack->Parameters.QueryInterface.Version << 16) |
                       stack->Parameters.QueryInterface.Size);
#else
    UNREFERENCED_PARAMETER(stack);
#endif
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
        hcdPdoStarted(pdo);
        return HcdCompleteIrp(irp, STATUS_SUCCESS, 0);

    case IRP_MN_QUERY_STOP_DEVICE:
    case IRP_MN_QUERY_REMOVE_DEVICE:
    case IRP_MN_CANCEL_STOP_DEVICE:
    case IRP_MN_CANCEL_REMOVE_DEVICE:
        return HcdCompleteIrp(irp, STATUS_SUCCESS, 0);

    case IRP_MN_STOP_DEVICE:
    case IRP_MN_SURPRISE_REMOVAL:
        pdo->Common.PnpState = HCD_PNP_STOPPED;
        hcdPdoQuiesce(pdo, 0);
        return HcdCompleteIrp(irp, STATUS_SUCCESS, 0);

    case IRP_MN_REMOVE_DEVICE:
        pdo->Common.PnpState = HCD_PNP_REMOVED;
        hcdPdoQuiesce(pdo, 1);
        hcdPdoFunctionRelease(pdo);
        HcdCompleteIrp(irp, STATUS_SUCCESS, 0);
        hcdPdoRemoved(pdo);
        return STATUS_SUCCESS;

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

    case IRP_MN_QUERY_INTERFACE:
        hcdQueryInterfaceTrace(stack);
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
