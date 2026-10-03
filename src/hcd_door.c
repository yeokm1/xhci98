/*
 * hcd_door.c - the HCD's user-mode door (roadmap-hcd.md task 26-A.8; design
 * record 13 section 8): what usbport.sys and usbhub.sys answered for the
 * miniport, answered by the one binary that now owns both devnodes.
 *
 *   - On the controller FDO: IRP_MJ_CREATE and CLOSE with no work, and
 *     IRP_MJ_DEVICE_CONTROL for the controller's Advanced tab
 *     (IOCTL_GET_HCD_DRIVERKEY_NAME, IOCTL_USB_GET_ROOT_HUB_NAME and
 *     IOCTL_USB_USER_REQUEST request 1, section 8.1) and for XHCISNAP
 *     (IOCTL_USB_USER_REQUEST request 3, USBUSER_PASS_THRU, with usbport's
 *     dispatcher reproduced clause for clause so the tool's wire format and
 *     its -probe expectations carry over unchanged, section 8.8).
 *   - On the root-hub FDO: the hub IOCTLs of section 8.3 for the Power tab
 *     and Vista's and 7's hub Advanced tab, answered from the bus's own PDO
 *     list. 0x220408 means node information here and the root hub's name on
 *     the controller: under Microsoft's stack the two meanings reached two
 *     drivers, here they reach two device objects of one.
 *   - How each devnode is found (section 8.2): a \DosDevices name on each -
 *     HCD<n> on the controller, as usbport made it, and XHCI98RH<serial> on
 *     the root hub - written as the devnode's SymbolicName for the 9x and NT
 *     5.x pages, plus an enabled GUID_DEVINTERFACE_USB_HOST_CONTROLLER or
 *     GUID_DEVINTERFACE_USB_HUB interface for Vista's and 7's. A \DosDevices
 *     name rather than the interface link is what SymbolicName carries and
 *     what GET_ROOT_HUB_NAME returns, because every page rewrites that prefix
 *     to \\.\ and Windows 98's own USB drivers create such names, while
 *     whether NTKERN resolves an interface link through \\.\ is open
 *     (section 8.10).
 *
 * The hub answers are the bus's truth within what the structures can carry:
 * a SuperSpeed device's Speed reads High Speed in the _EX form, the ceiling
 * that interface has (section 8.4). External hubs are objects inside the bus
 * (Phase 27), so DeviceIsHub is FALSE everywhere and a composite the bus
 * splits is one connection whose driver key is its first function's (section
 * 8.10, the two rows bound to this task).
 *
 * Every structure is written byte by byte at the offsets of usbioctl.h's
 * #pragma pack(1) layouts and usbuser.h's, because the Windows 2000 DDK the
 * x86 build uses has neither the _EX forms nor usbuser.h, and the amd64
 * build's WDK 7.1 headers would otherwise give the two architectures two
 * different definitions of one wire format.
 *
 * IRQL: PASSIVE_LEVEL throughout (IRP_MJ_DEVICE_CONTROL from user mode, and
 * PnP), except where a function says otherwise.
 */

#include "hcd.h"
#include "xhci_hw.h"

/* CTL_CODE(FILE_DEVICE_USB = 0x22, fn, METHOD_BUFFERED, FILE_ANY_ACCESS);
 * design record 13 sections 8.1 and 8.3. */
#define HCD_IOCTL_ROOT_HUB_NAME    0x00220408UL /* the controller           */
#define HCD_IOCTL_NODE_INFO        0x00220408UL /* the root hub             */
#define HCD_IOCTL_CONN_INFO        0x0022040CUL
#define HCD_IOCTL_CONN_DESCRIPTOR  0x00220410UL
#define HCD_IOCTL_CONN_NAME        0x00220414UL
#define HCD_IOCTL_CONN_DRIVERKEY   0x00220420UL
#define HCD_IOCTL_HCD_DRIVERKEY    0x00220424UL
#define HCD_IOCTL_USER_REQUEST     0x00220438UL
#define HCD_IOCTL_HUB_CAPS         0x0022043CUL
#define HCD_IOCTL_CONN_ATTRIBUTES  0x00220440UL
#define HCD_IOCTL_CONN_INFO_EX     0x00220448UL
#define HCD_IOCTL_RESET_HUB        0x0022044CUL
#define HCD_IOCTL_HUB_CAPS_EX      0x00220450UL

/* USBUSER (WDK 7.1 usbuser.h): the header, the codes, the two requests. */
#define HCD_UU_HEADER_BYTES        0x10UL
#define HCD_UU_REQUEST             0x00UL
#define HCD_UU_STATUS              0x04UL
#define HCD_UU_REQUEST_BYTES       0x08UL
#define HCD_UU_ACTUAL_BYTES        0x0CUL
#define HCD_UU_GUID                0x10UL
#define HCD_UU_PARAM_BYTES         0x20UL
#define HCD_UU_PARAMETERS          0x24UL
#define HCD_UU_FLOOR               0x28UL   /* header + either body        */
#define HCD_UU_PARAM_MAX           0x10000UL
#define HCD_UU_GET_CONTROLLER_INFO_0 1UL
#define HCD_UU_PASS_THRU           3UL
#define HCD_UU_TABLE_LAST          8UL      /* usbport's table: 1 to 8     */
#define HCD_UU_GATED_MASK          0x30000000UL
#define HCD_UU_OK                  0UL      /* UsbUserSuccess              */
#define HCD_UU_NOT_SUPPORTED       1UL
#define HCD_UU_INVALID_REQUEST     2UL
#define HCD_UU_FEATURE_DISABLED    3UL
#define HCD_UU_BAD_HEADER          4UL
#define HCD_UU_BAD_PARAMETER       5UL
#define HCD_UU_MINIPORT_ERROR      6UL
#define HCD_UU_BUFFER_TOO_SMALL    7UL
/*
 * USB_CONTROLLER_INFO_0.ControllerFlavor. Windows 7's usbui.dll tests only
 * >= 1000 (UsbItem::IsController20), and WDK 7.1's USB_CONTROLLER_FLAVOR has
 * no xHCI value; EHCI_Generic (1000) is the one that makes the bandwidth view
 * use USB 2.0 arithmetic, which is what the bus schedules (section 8.10).
 */
#define HCD_FLAVOR_USB20           1000UL

/* usbioctl.h, #pragma pack(1). */
#define HCD_NODE_INFO_BYTES        0x4CUL
#define HCD_CONN_INFO_BYTES        0x23UL
#define HCD_PIPE_INFO_BYTES        0x0BUL
#define HCD_DESC_REQUEST_BYTES     0x0CUL
#define HCD_CONN_ATTR_BYTES        0x0CUL
#define HCD_CONN_NO_DEVICE         0UL      /* USB_CONNECTION_STATUS       */
#define HCD_CONN_CONNECTED         1UL
#define HCD_CONN_FAILED_ENUM       2UL
#define HCD_HUB_CAPS_EX_ROOT_HS    0x1FUL   /* HS capable, HS, multi-TT
                                             * capable, multi-TT, root    */

/* Open pipes a connection answer carries at most; usbview asks for 30. */
#define HCD_DOOR_PIPES             30UL

/* The highest \DosDevices\HCD<n> tried: other usbport controllers own the
 * low numbers on a machine that has them (section 8.8). */
#define HCD_DOOR_HCD_MAX           64UL

#if defined(XHCI_FLAVOUR_QEMU)
#define HCD_SNAPSHOT_FLAVOUR XHCI_SNAPSHOT_FLAVOUR_QEMU
#elif defined(XHCI_FLAVOUR_DEBUG)
#define HCD_SNAPSHOT_FLAVOUR XHCI_SNAPSHOT_FLAVOUR_DEBUG
#elif defined(XHCI_FLAVOUR_RELEASE)
#define HCD_SNAPSHOT_FLAVOUR XHCI_SNAPSHOT_FLAVOUR_RELEASE
#else
#define HCD_SNAPSHOT_FLAVOUR XHCI_SNAPSHOT_FLAVOUR_UNKNOWN
#endif

/* WDK 7.1 usbiodef.h; neither is in the Windows 2000 DDK. */
static const GUID hcdGuidHostController = {
    0x3ABF6F2DUL, 0x71C4, 0x462A,
    { 0x8A, 0x92, 0x1E, 0x68, 0x61, 0xE6, 0xAF, 0x27 }
};
static const GUID hcdGuidHub = {
    0xF18A0E88UL, 0xC30C, 0x11D0,
    { 0x88, 0x15, 0x00, 0xA0, 0xC9, 0x06, 0xBE, 0xD8 }
};

static const WCHAR hcdDosDevices[] = L"\\DosDevices\\";
static const WCHAR hcdDevice[] = L"\\Device\\";

typedef struct _HCD_DOOR_CONN {
    ULONG Status;                   /* HCD_CONN_*                          */
    UCHAR DeviceDesc[18];
    ULONG SpeedClass;               /* XHCI_SPEED_*                        */
    ULONG Address;
    ULONG ConfigValue;
    ULONG Pipes;
    UCHAR Pipe[HCD_DOOR_PIPES][7];  /* endpoint descriptors                */
    PDEVICE_OBJECT Pdo;             /* referenced, when asked for          */
    ULONG ConfigBytes;              /* copied, when asked for              */
    ULONG ConfigTotal;
} HCD_DOOR_CONN, *PHCD_DOOR_CONN;

/* ----------------------------------------------------------------------- */
/* Bytes and names                                                          */
/* ----------------------------------------------------------------------- */

/* IRQL: any. */
static VOID hcdPut16(PUCHAR b, ULONG off, ULONG v)
{
    b[off] = (UCHAR)(v & 0xFFUL);
    b[off + 1] = (UCHAR)((v >> 8) & 0xFFUL);
}

/* IRQL: any. */
static VOID hcdPut32(PUCHAR b, ULONG off, ULONG v)
{
    hcdPut16(b, off, v & 0xFFFFUL);
    hcdPut16(b, off + 2, (v >> 16) & 0xFFFFUL);
}

/* IRQL: any. */
static ULONG hcdGet32(const UCHAR *b, ULONG off)
{
    return (ULONG)b[off] | ((ULONG)b[off + 1] << 8) |
           ((ULONG)b[off + 2] << 16) | ((ULONG)b[off + 3] << 24);
}

/* IRQL: any. */
static VOID hcdZero(PUCHAR b, ULONG bytes)
{
    ULONG i;

    for (i = 0; i < bytes; i++) {
        b[i] = 0;
    }
}

/* IRQL: any. */
static VOID hcdCopy(PUCHAR dst, const UCHAR *src, ULONG bytes)
{
    ULONG i;

    for (i = 0; i < bytes; i++) {
        dst[i] = src[i];
    }
}

/* `prefix` then `stem` then decimal `n`, NUL-terminated, into `out` of
 * `cap` characters; returns the characters written before the NUL, 0 if it
 * does not fit. IRQL: any. */
static ULONG hcdFormatName(PWCHAR out, ULONG cap, const WCHAR *prefix,
                           const WCHAR *stem, ULONG n)
{
    WCHAR digits[12];
    ULONG len;
    ULONG d;
    ULONG i;

    len = 0;
    d = 0;
    do {
        digits[d++] = (WCHAR)(L'0' + (n % 10UL));
        n /= 10UL;
    } while (n != 0 && d < 11);
    for (i = 0; prefix[i] != 0; i++) {
        if (len + 1 >= cap) {
            return 0;
        }
        out[len++] = prefix[i];
    }
    for (i = 0; stem[i] != 0; i++) {
        if (len + 1 >= cap) {
            return 0;
        }
        out[len++] = stem[i];
    }
    while (d > 0) {
        if (len + 1 >= cap) {
            return 0;
        }
        out[len++] = digits[--d];
    }
    out[len] = 0;
    return len;
}

/*
 * usbport's and usbhub's two-call name protocol: a buffer that holds the
 * fixed part gets ActualLength (the whole answer's size, the name's NUL
 * included) and success, and the name when it fits. `header` is the bytes
 * before the name, ActualLength the ULONG that ends them. IRQL: any.
 */
static NTSTATUS hcdAnswerName(PUCHAR buf, ULONG outLen, ULONG header,
                              const WCHAR *name, ULONG chars,
                              PULONG_PTR info)
{
    ULONG actual;
    ULONG i;

    if (outLen < header + sizeof(WCHAR)) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    actual = header + (chars + 1) * (ULONG)sizeof(WCHAR);
    hcdPut32(buf, header - 4, actual);
    if (outLen >= actual) {
        for (i = 0; i < chars; i++) {
            hcdPut16(buf, header + i * 2, (ULONG)name[i]);
        }
        hcdPut16(buf, header + chars * 2, 0);
        *info = actual;
    } else {
        hcdPut16(buf, header, 0);
        *info = header + sizeof(WCHAR);
    }
    return STATUS_SUCCESS;
}

/*
 * A device's driver (software) key name, the string Device Manager's
 * CM_DRP_DRIVER gives and usbui.dll matches the devnode tree against
 * (section 8.1). `chars` excludes the NUL. IRQL: PASSIVE_LEVEL.
 */
static NTSTATUS hcdDriverKey(PDEVICE_OBJECT pdo, PWCHAR out, ULONG cap,
                             PULONG chars)
{
    ULONG bytes;
    NTSTATUS status;

    bytes = 0;
    status = IoGetDeviceProperty(pdo, DevicePropertyDriverKeyName,
                                 (cap - 1) * (ULONG)sizeof(WCHAR), out,
                                 &bytes);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    out[cap - 1] = 0;
    *chars = 0;
    while (*chars < cap - 1 && out[*chars] != 0) {
        (*chars)++;
    }
    return STATUS_SUCCESS;
}

/* REG_SZ SymbolicName on the devnode's hardware key, which the 98 to XP x64
 * pages read before they open the device (section 8.2). Best effort: a page
 * without it is a missing tab, not a failed device. IRQL: PASSIVE_LEVEL. */
static VOID hcdWriteSymbolicName(PDEVICE_OBJECT pdo, const WCHAR *value,
                                 ULONG chars)
{
    UNICODE_STRING name;
    HANDLE key;
    NTSTATUS status;

    status = IoOpenDeviceRegistryKey(pdo, PLUGPLAY_REGKEY_DEVICE, KEY_WRITE,
                                     &key);
    if (!NT_SUCCESS(status)) {
        return;
    }
    RtlInitUnicodeString(&name, L"SymbolicName");
    (VOID)ZwSetValueKey(key, &name, 0, REG_SZ, (PVOID)value,
                        (chars + 1) * (ULONG)sizeof(WCHAR));
    (VOID)ZwClose(key);
}

/* Register (once) and enable a device interface; best effort, as above.
 * IRQL: PASSIVE_LEVEL. */
static VOID hcdInterfaceOn(PDEVICE_OBJECT pdo, const GUID *guid,
                           PUNICODE_STRING link, PULONG on)
{
    if (link->Buffer == NULL) {
        if (!NT_SUCCESS(IoRegisterDeviceInterface(pdo, guid, NULL, link))) {
            link->Buffer = NULL;
            return;
        }
    }
    if (!*on && NT_SUCCESS(IoSetDeviceInterfaceState(link, TRUE))) {
        *on = 1;
    }
}

/* IRQL: PASSIVE_LEVEL. */
static VOID hcdInterfaceOff(PUNICODE_STRING link, PULONG on, ULONG release)
{
    if (*on) {
        (VOID)IoSetDeviceInterfaceState(link, FALSE);
        *on = 0;
    }
    if (release && link->Buffer != NULL) {
        /* The string IoRegisterDeviceInterface allocated, freed with the
         * pool file's ExFreePool (section 7.5 rule 4). */
        HcdPoolFreeForeign(link->Buffer);
        link->Buffer = NULL;
        link->Length = 0;
        link->MaximumLength = 0;
    }
}

/* IRQL: PASSIVE_LEVEL. */
static NTSTATUS hcdLink(const WCHAR *link, const WCHAR *target, ULONG create)
{
    UNICODE_STRING l;
    UNICODE_STRING t;

    RtlInitUnicodeString(&l, link);
    if (!create) {
        return IoDeleteSymbolicLink(&l);
    }
    RtlInitUnicodeString(&t, target);
    return IoCreateSymbolicLink(&l, &t);
}

/* ----------------------------------------------------------------------- */
/* The door's lifetime                                                      */
/* ----------------------------------------------------------------------- */

/* IRQL: PASSIVE_LEVEL. */
VOID HcdDoorGateEnter(PHCD_CONTROLLER hc)
{
    (VOID)KeWaitForSingleObject(&hc->DoorGate, Executive, KernelMode, FALSE,
                                NULL);
}

/* IRQL: <= DISPATCH_LEVEL. */
VOID HcdDoorGateLeave(PHCD_CONTROLLER hc)
{
    (VOID)KeSetEvent(&hc->DoorGate, IO_NO_INCREMENT, FALSE);
}

/*
 * The controller's names, at every successful start: \DosDevices\HCD<n> at
 * the first free n, made once and kept until the remove (a Windows 98
 * disable is a stop with no remove, and its re-enable a start), the host
 * controller interface enabled, and SymbolicName written. IRQL:
 * PASSIVE_LEVEL.
 */
VOID HcdDoorControllerStart(PHCD_CONTROLLER hc)
{
    WCHAR target[40];
    WCHAR link[40];
    ULONG n;

    if (!hc->HcdLinkMade &&
        hcdFormatName(target, 40, hcdDevice, L"XHCI98HC", hc->FdoSerial) !=
            0) {
        for (n = 0; n < HCD_DOOR_HCD_MAX; n++) {
            if (hcdFormatName(link, 40, hcdDosDevices, L"HCD", n) != 0 &&
                NT_SUCCESS(hcdLink(link, target, 1))) {
                hc->HcdIndex = n;
                hc->HcdLinkMade = 1;
                break;
            }
        }
    }
    hcdInterfaceOn(hc->Pdo, &hcdGuidHostController, &hc->HcInterface,
                   &hc->HcInterfaceOn);
    if (hc->HcdLinkMade) {
        n = hcdFormatName(link, 40, hcdDosDevices, L"HCD", hc->HcdIndex);
        if (n != 0) {
            hcdWriteSymbolicName(hc->Pdo, link, n);
        }
    }
    XhciLogNote(&hc->Hc, "door.hcd", hc->HcdLinkMade ? hc->HcdIndex
                                                     : 0xFFFFFFFFUL);
    XhciLogNote(&hc->Hc, "door.interface", hc->HcInterfaceOn);
}

/* At a stop or a surprise removal: Vista's and 7's pages stop finding the
 * controller. IRQL: PASSIVE_LEVEL. */
VOID HcdDoorControllerStop(PHCD_CONTROLLER hc)
{
    hcdInterfaceOff(&hc->HcInterface, &hc->HcInterfaceOn, 0);
}

/* At the remove, before the FDO is deleted. IRQL: PASSIVE_LEVEL. */
VOID HcdDoorControllerRemove(PHCD_CONTROLLER hc)
{
    WCHAR link[40];

    hcdInterfaceOff(&hc->HcInterface, &hc->HcInterfaceOn, 1);
    if (hc->HcdLinkMade &&
        hcdFormatName(link, 40, hcdDosDevices, L"HCD", hc->HcdIndex) != 0) {
        (VOID)hcdLink(link, NULL, 0);
    }
    hc->HcdLinkMade = 0;
}

/*
 * The root hub's names, at its FDO's start: \DosDevices\XHCI98RH<serial> on
 * the root-hub PDO's own name, the hub interface, SymbolicName, and the name
 * the controller's GET_ROOT_HUB_NAME returns. `hc` may be NULL (an orphaned
 * root hub). IRQL: PASSIVE_LEVEL.
 */
VOID HcdDoorRootHubStart(PHCD_ROOTHUB_FDO fdo, PHCD_CONTROLLER hc)
{
    PHCD_ROOTHUB_PDO pdo;
    WCHAR target[40];
    WCHAR link[40];
    WCHAR stem[24];
    KIRQL oldIrql;
    ULONG chars;
    ULONG i;

    pdo = (PHCD_ROOTHUB_PDO)fdo->Pdo->DeviceExtension;
    chars = hcdFormatName(link, 40, hcdDosDevices, L"XHCI98RH", pdo->Serial);
    if (chars == 0 ||
        hcdFormatName(target, 40, hcdDevice, L"XHCI98RH", pdo->Serial) ==
            0) {
        return;
    }
    if (!fdo->LinkMade && NT_SUCCESS(hcdLink(link, target, 1))) {
        fdo->LinkMade = 1;
    }
    hcdInterfaceOn(fdo->Pdo, &hcdGuidHub, &fdo->Interface,
                   &fdo->InterfaceOn);
    if (!fdo->LinkMade) {
        return;
    }
    hcdWriteSymbolicName(fdo->Pdo, link, chars);
    if (hc == NULL) {
        return;
    }
    chars = hcdFormatName(stem, 24, L"", L"XHCI98RH", pdo->Serial);
    KeAcquireSpinLock(&hc->PdoListLock, &oldIrql);
    for (i = 0; i <= chars; i++) {
        hc->RootHubName[i] = stem[i];
    }
    hc->RootHubNameChars = chars;
    KeReleaseSpinLock(&hc->PdoListLock, oldIrql);
}

/* IRQL: PASSIVE_LEVEL. */
VOID HcdDoorRootHubStop(PHCD_ROOTHUB_FDO fdo)
{
    hcdInterfaceOff(&fdo->Interface, &fdo->InterfaceOn, 0);
}

/* At the root-hub FDO's remove; `hc` may be NULL. IRQL: PASSIVE_LEVEL. */
VOID HcdDoorRootHubRemove(PHCD_ROOTHUB_FDO fdo, PHCD_CONTROLLER hc)
{
    PHCD_ROOTHUB_PDO pdo;
    WCHAR link[40];
    KIRQL oldIrql;

    hcdInterfaceOff(&fdo->Interface, &fdo->InterfaceOn, 1);
    pdo = (PHCD_ROOTHUB_PDO)fdo->Pdo->DeviceExtension;
    if (fdo->LinkMade &&
        hcdFormatName(link, 40, hcdDosDevices, L"XHCI98RH", pdo->Serial) !=
            0) {
        (VOID)hcdLink(link, NULL, 0);
    }
    fdo->LinkMade = 0;
    if (hc != NULL) {
        KeAcquireSpinLock(&hc->PdoListLock, &oldIrql);
        hc->RootHubNameChars = 0;
        hc->RootHubName[0] = 0;
        KeReleaseSpinLock(&hc->PdoListLock, oldIrql);
    }
}

/* IRP_MJ_CREATE, CLOSE and CLEANUP on either FDO: no work and no access
 * check, as usbport completes them (section 8.8). */
NTSTATUS HcdDoorCreateClose(PIRP irp)
{
    return HcdCompleteIrp(irp, STATUS_SUCCESS, 0);
}

/* ----------------------------------------------------------------------- */
/* USBUSER: request 1 and XHCISNAP's request 3                               */
/* ----------------------------------------------------------------------- */

/* IRQL: any. */
static ULONG hcdSnapshotBuildFlags(VOID)
{
    ULONG flags;

    flags = 0;
#if DBG
    flags |= XHCI_SNAPSHOT_B_DEBUG;
#endif
    return flags;
}

/* The root ports the bus reports: HCSPARAMS1.MaxPorts, every xHCI port
 * whatever its protocol. IRQL: any. */
static ULONG hcdDoorPorts(PHCD_CONTROLLER hc)
{
    ULONG ports;

    if (hc->Hc.HcInfoStatus != XHCI_HC_OK) {
        return 0;
    }
    ports = hc->Hc.HcInfo.MaxPorts;
    if (ports > XHCI_MAX_ROOT_PORTS) {
        ports = XHCI_MAX_ROOT_PORTS;
    }
    return ports;
}

/*
 * XHCISNAP's window, the miniport's xhciPassThru (branch 1.2.0.0) over the
 * HCD's embedded XHCI_EXTENSION: schema 5 unchanged, so the published tool
 * reads it. Returns the USBUSER status: 6 for a GUID not ours, for the shut
 * channel (XhciLogVerbosity 0) and for a block too small for a header - the
 * three answers usbport gave for the miniport's nonzero returns - and 0 with
 * the outcome in the header otherwise. The caller holds the door gate, so
 * no start or stop runs under the copy; the power gate is taken inside it,
 * the order the gates always nest in. IRQL: PASSIVE_LEVEL.
 */
static ULONG hcdDoorSnapshot(PHCD_CONTROLLER hc, const UCHAR *guid,
                             PUCHAR params, ULONG paramLength)
{
    PXHCI_EXTENSION ext;
    XHCI_SNAPSHOT_HEADER *header;
    PUCHAR payload;
    ULONG requestSignature;
    ULONG requestRegion;
    ULONG requestOffset;
    ULONG capacity;
    ULONG regionBytes;
    ULONG available;
    ULONG copied;
    ULONG ports;
    ULONG first;
    ULONG slots;
    ULONG valid;
    ULONG mmio;
    ULONG i;
    KIRQL oldIrql;

    if (hcdGet32(guid, 0) != XHCI_SNAPSHOT_GUID_0 ||
        hcdGet32(guid, 4) != XHCI_SNAPSHOT_GUID_1 ||
        hcdGet32(guid, 8) != XHCI_SNAPSHOT_GUID_2 ||
        hcdGet32(guid, 12) != XHCI_SNAPSHOT_GUID_3) {
        return HCD_UU_MINIPORT_ERROR;
    }
    ext = &hc->Hc;
    valid = (ext->Signature == XHCI_EXTENSION_SIGNATURE &&
             ext->TrailingSignature == XHCI_EXTENSION_TRAILING)
                ? 1UL
                : 0UL;
    if (valid && ext->Log.Verbosity == XHCI_LOG_VERBOSITY_OFF) {
        return HCD_UU_MINIPORT_ERROR;
    }
    if (paramLength < sizeof(XHCI_SNAPSHOT_HEADER)) {
        return HCD_UU_MINIPORT_ERROR;
    }

    /* The request and the header overlay each other. */
    requestSignature = hcdGet32(params, 0);
    requestRegion = hcdGet32(params, 4);
    requestOffset = hcdGet32(params, 8);

    hcdZero(params, sizeof(XHCI_SNAPSHOT_HEADER));
    header = (XHCI_SNAPSHOT_HEADER *)params;
    payload = params + sizeof(XHCI_SNAPSHOT_HEADER);
    capacity = paramLength - sizeof(XHCI_SNAPSHOT_HEADER);
    header->Signature = XHCI_SNAPSHOT_SIGNATURE;
    header->SchemaVersion = XHCI_SNAPSHOT_SCHEMA;
    header->HeaderBytes = sizeof(XHCI_SNAPSHOT_HEADER);
    header->Region = requestRegion;
    header->Offset = requestOffset;
    header->ExtensionBytes = sizeof(XHCI_EXTENSION);
    header->BuildFlags = hcdSnapshotBuildFlags();
    header->Flavour = HCD_SNAPSHOT_FLAVOUR;
    header->RingOffset = XHCI_FIELD_OFFSET(XHCI_EXTENSION, Log) +
                         XHCI_FIELD_OFFSET(XHCI_LOG, Ring);
    header->RingBytes = XHCI_LOG_RING_BYTES;

    if (requestSignature != XHCI_SNAPSHOT_REQUEST_SIGNATURE) {
        header->Status |= XHCI_SNAPSHOT_S_BAD_REQUEST;
        return HCD_UU_OK;
    }
    if (!valid) {
        header->Status |= XHCI_SNAPSHOT_S_BAD_EXTENSION;
        return HCD_UU_OK;
    }

    /* MMIO only on a started controller in D0: the door gate keeps the
     * mapping, and the power gate, held from this check to the last register
     * read, keeps a suspend from falling between the two (Codex review of
     * 26-A.8, round 22, finding 2) - every path that changes DevicePower or
     * SuspendedInD0 holds it. */
    HcdPowerGateEnter(hc);
    mmio = (hc->ControllerStarted && hc->BarVa != NULL &&
            hc->Common.DevicePower == PowerDeviceD0 && !hc->SuspendedInD0 &&
            ext->HcInfoStatus == XHCI_HC_OK)
               ? 1UL
               : 0UL;

    XhciControllerLockAcquire(ext, &oldIrql);
    header->VerbosityRead = ext->Log.VerbosityRead;
    header->VerbosityApplied = ext->Log.Verbosity;
    header->SwitchStatusVerbosity = ext->Log.SwitchStatusVerbosity;
    header->SwitchStatusDebugView = ext->Log.SwitchStatusDebugView;
    header->SwitchRead = ext->Log.SwitchRead;
    header->RingHead = ext->Log.Head;
    header->RingUsed = ext->Log.Used;
    header->ImodStatus = ext->ImodStatus;
    header->ImodRequested = ext->ImodRequested;
    header->ImodInterval = ext->ImodInterval;
    header->ImodReadback = ext->ImodReadback;
    /* The Vhub* fields stay 0: the HCD reads none of XhciVirtualHSHub's
     * values (section 5.5), and 0 is "switch not read" in schema 5. */
    header->TearDetector = ext->CheckCallbacks + ext->DpcCount +
                           ext->Log.Appends + ext->Log.Suppressed;
    if (ext->HcInfoStatus == XHCI_HC_OK) {
        header->PortCount = ext->HcInfo.MaxPorts;
    }

    if (requestRegion == XHCI_SNAPSHOT_REGION_EXTENSION) {
        regionBytes = sizeof(XHCI_EXTENSION);
        header->RegionBytes = regionBytes;
        if (requestOffset >= regionBytes) {
            header->Status |= XHCI_SNAPSHOT_S_PAST_END;
        } else {
            available = regionBytes - requestOffset;
            copied = (available > capacity) ? capacity : available;
            hcdCopy(payload, (const UCHAR *)ext + requestOffset, copied);
            header->PayloadBytes = copied;
            if (copied < available) {
                header->Status |= XHCI_SNAPSHOT_S_TRUNCATED;
            }
        }
    } else if (requestRegion == XHCI_SNAPSHOT_REGION_PORTSC) {
        /* A bare read: no shadow fold and no change-bit acknowledgement, the
         * observation-mode exception the miniport documented. */
        if (!mmio) {
            header->Status |= XHCI_SNAPSHOT_S_NO_MMIO;
        } else if ((requestOffset & 3UL) != 0) {
            header->Status |= XHCI_SNAPSHOT_S_BAD_REQUEST;
        } else {
            ports = ext->HcInfo.MaxPorts;
            regionBytes = ports * (ULONG)sizeof(ULONG);
            header->RegionBytes = regionBytes;
            if (requestOffset >= regionBytes) {
                header->Status |= XHCI_SNAPSHOT_S_PAST_END;
            } else {
                first = requestOffset / (ULONG)sizeof(ULONG);
                available = ports - first;
                slots = capacity / (ULONG)sizeof(ULONG);
                copied = (available > slots) ? slots : available;
                for (i = 0; i < copied; i++) {
                    hcdPut32(payload, i * 4,
                             XhciReadPortsc(ext, first + i + 1UL));
                }
                header->PayloadBytes = copied * (ULONG)sizeof(ULONG);
                if (copied < available) {
                    header->Status |= XHCI_SNAPSHOT_S_TRUNCATED;
                }
            }
        }
    } else {
        header->Status |= XHCI_SNAPSHOT_S_BAD_REGION;
    }
    XhciControllerLockRelease(ext, oldIrql);
    HcdPowerGateLeave(hc);
    return HCD_UU_OK;
}

/* USB_CONTROLLER_INFO_0 at `body`. The revision is read from PCI
 * configuration space while the controller runs. IRQL: PASSIVE_LEVEL, door
 * gate held. */
static VOID hcdDoorControllerInfo(PHCD_CONTROLLER hc, PUCHAR body)
{
    PXHCI_EXTENSION ext;
    ULONG revision;

    ext = &hc->Hc;
    revision = 0;
    if (hc->ControllerStarted &&
        XhciReadPciConfig(ext, 0x08UL, &revision, 1) != MP_STATUS_SUCCESS) {
        revision = 0;
    }
    hcdPut32(body, 0x00, ext->PciVendorDevice & 0xFFFFUL);
    hcdPut32(body, 0x04, (ext->PciVendorDevice >> 16) & 0xFFFFUL);
    hcdPut32(body, 0x08, revision & 0xFFUL);
    hcdPut32(body, 0x0C, hcdDoorPorts(hc));
    hcdPut32(body, 0x10, HCD_FLAVOR_USB20);
    hcdPut32(body, 0x14, 0);
}

/*
 * IOCTL_USB_USER_REQUEST, usbport's dispatcher clause for clause (usbport ABI
 * document, "Debug / single-packet"; design record 13 section 8.8): one
 * buffer in and out, every refusal in the header with STATUS_SUCCESS, and
 * Information = min(RequestBufferLength, ActualBufferLength). Requests 2 and
 * 4 to 8 were in usbport's table and no target's UI sends them; they answer
 * UsbUserNotSupported. IRQL: PASSIVE_LEVEL.
 */
static NTSTATUS hcdDoorUserRequest(PHCD_CONTROLLER hc, PUCHAR buf,
                                   ULONG inLen, ULONG outLen,
                                   PULONG_PTR info)
{
    ULONG request;
    ULONG declared;
    ULONG actual;
    ULONG code;
    ULONG paramLength;

    if (inLen != outLen) {
        return STATUS_INVALID_PARAMETER;
    }
    if (inLen < HCD_UU_HEADER_BYTES) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    request = hcdGet32(buf, HCD_UU_REQUEST);
    declared = hcdGet32(buf, HCD_UU_REQUEST_BYTES);
    hcdPut32(buf, HCD_UU_STATUS, 0);
    actual = HCD_UU_HEADER_BYTES;

    if (declared != inLen) {
        code = HCD_UU_BAD_HEADER;
    } else if ((request & HCD_UU_GATED_MASK) != 0) {
        code = HCD_UU_FEATURE_DISABLED;
    } else if (request == HCD_UU_GET_CONTROLLER_INFO_0) {
        actual = HCD_UU_FLOOR;
        if (declared < HCD_UU_FLOOR) {
            code = HCD_UU_BUFFER_TOO_SMALL;
        } else {
            HcdDoorGateEnter(hc);
            hcdDoorControllerInfo(hc, buf + HCD_UU_HEADER_BYTES);
            HcdDoorGateLeave(hc);
            code = HCD_UU_OK;
        }
    } else if (request == HCD_UU_PASS_THRU) {
        actual = HCD_UU_FLOOR;
        if (declared < HCD_UU_FLOOR) {
            code = HCD_UU_BUFFER_TOO_SMALL;
        } else {
            paramLength = hcdGet32(buf, HCD_UU_PARAM_BYTES);
            if (paramLength > HCD_UU_PARAM_MAX) {
                code = HCD_UU_BAD_PARAMETER;
            } else if (HCD_UU_PARAMETERS + paramLength > declared) {
                code = HCD_UU_BUFFER_TOO_SMALL;
            } else {
                actual = paramLength + HCD_UU_FLOOR;
                hc->DoorPassThru++;
                HcdDoorGateEnter(hc);
                code = hcdDoorSnapshot(hc, buf + HCD_UU_GUID,
                                       buf + HCD_UU_PARAMETERS, paramLength);
                HcdDoorGateLeave(hc);
            }
        }
    } else if (request >= 2 && request <= HCD_UU_TABLE_LAST) {
        code = HCD_UU_NOT_SUPPORTED;
    } else {
        code = HCD_UU_INVALID_REQUEST;
    }

    hcdPut32(buf, HCD_UU_STATUS, code);
    hcdPut32(buf, HCD_UU_ACTUAL_BYTES, actual);
    *info = (declared < actual) ? declared : actual;
    if (*info > inLen) {
        *info = inLen;
    }
    return STATUS_SUCCESS;
}

/*
 * IRP_MJ_DEVICE_CONTROL on the controller FDO. Anything else is
 * STATUS_INVALID_DEVICE_REQUEST, as usbport's switch answers it, and nothing
 * is forwarded to the PCI stack. IRQL: PASSIVE_LEVEL, the caller inside
 * HcdIoEnter.
 */
NTSTATUS HcdDoorControllerIoctl(PHCD_CONTROLLER hc, PIRP irp)
{
    PIO_STACK_LOCATION stack;
    PUCHAR buf;
    ULONG inLen;
    ULONG outLen;
    ULONG_PTR info;
    WCHAR name[160];
    ULONG chars;
    KIRQL oldIrql;
    NTSTATUS status;
    ULONG i;

    stack = IoGetCurrentIrpStackLocation(irp);
    buf = (PUCHAR)irp->AssociatedIrp.SystemBuffer;
    inLen = stack->Parameters.DeviceIoControl.InputBufferLength;
    outLen = stack->Parameters.DeviceIoControl.OutputBufferLength;
    info = 0;
    hc->DoorRequests++;
    if (buf == NULL && (inLen != 0 || outLen != 0)) {
        return HcdCompleteIrp(irp, STATUS_INVALID_PARAMETER, 0);
    }

    switch (stack->Parameters.DeviceIoControl.IoControlCode) {
    case HCD_IOCTL_USER_REQUEST:
        if (buf == NULL) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        status = hcdDoorUserRequest(hc, buf, inLen, outLen, &info);
        break;

    case HCD_IOCTL_HCD_DRIVERKEY:
        status = hcdDriverKey(hc->Pdo, name, 160, &chars);
        if (NT_SUCCESS(status)) {
            status = hcdAnswerName(buf, outLen, 4, name, chars, &info);
        }
        break;

    case HCD_IOCTL_ROOT_HUB_NAME:
        KeAcquireSpinLock(&hc->PdoListLock, &oldIrql);
        chars = hc->RootHubNameChars;
        for (i = 0; i < chars; i++) {
            name[i] = hc->RootHubName[i];
        }
        KeReleaseSpinLock(&hc->PdoListLock, oldIrql);
        if (chars == 0) {
            status = STATUS_UNSUCCESSFUL;
            break;
        }
        status = hcdAnswerName(buf, outLen, 4, name, chars, &info);
        break;

    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }
    return HcdCompleteIrp(irp, status, NT_SUCCESS(status) ? info : 0);
}

/* ----------------------------------------------------------------------- */
/* The root hub's IOCTLs                                                     */
/* ----------------------------------------------------------------------- */

/*
 * What the bus knows about root port `port`: the PDO standing for its device
 * (a lone device PDO, or a split device's first function, whose Group is its
 * own Serial), copied under PdoListLock and, for the open pipes, the
 * controller lock inside it - the order of design record 13 section 5.4.
 * `config`, when not NULL, receives up to `configCap` bytes of the whole
 * configuration descriptor. `wantPdo` returns the PDO referenced. IRQL:
 * <= DISPATCH_LEVEL.
 */
static VOID hcdDoorConnection(PHCD_CONTROLLER hc, ULONG port,
                              PHCD_DOOR_CONN c, PUCHAR config,
                              ULONG configCap, ULONG wantPdo)
{
    PHCD_DEVICE_PDO pdo;
    PHCD_USB_DEVICE dev;
    PHCD_PIPE pipe;
    const UCHAR *source;
    ULONG sourceBytes;
    ULONG wMax;
    ULONG dci;
    ULONG offset;
    ULONG slotDw3;
    KIRQL oldIrql;
    KIRQL lockIrql;

    hcdZero((PUCHAR)c, sizeof(*c));
    KeAcquireSpinLock(&hc->PdoListLock, &oldIrql);
    for (pdo = hc->DevicePdos; pdo != NULL; pdo = pdo->Next) {
        if (pdo->Port == port && pdo->Group == pdo->Serial) {
            break;
        }
    }
    if (pdo == NULL) {
        KeReleaseSpinLock(&hc->PdoListLock, oldIrql);
        if (port >= 1 && port <= XHCI_MAX_ROOT_PORTS &&
            hc->Ports[port - 1].Enum.State == XHCI_ENUM_FAILED) {
            c->Status = HCD_CONN_FAILED_ENUM;
        }
        return;
    }

    c->Status = HCD_CONN_CONNECTED;
    hcdCopy(c->DeviceDesc, pdo->DeviceDesc, sizeof(c->DeviceDesc));
    c->SpeedClass = pdo->SpeedClass;
    dev = pdo->Device;
    source = NULL;
    sourceBytes = 0;
    if (dev != NULL) {
        c->ConfigValue = dev->ConfigValue;
        if (dev->Config != NULL) {
            source = dev->Config;
            sourceBytes = dev->ConfigLength;
        }
        XhciControllerLockAcquire(&hc->Hc, &lockIrql);
        /* The address the xHC assigned, from the Output Slot Context: the
         * stop clears STARTED under this lock before the common buffer
         * goes, and below Addressed the field is not an address. */
        if ((hc->Hc.Flags & XHCI_EXT_FLAG_STARTED) != 0 &&
            dev->SlotId != 0 &&
            XhciSlotContextOffset(&hc->Hc.Layout, dev->SlotId, &offset) ==
                XHCI_LAYOUT_OK) {
            slotDw3 = XhciCommonAt(&hc->Hc, offset)[3];
            if (XHCI_SLOT_GET_STATE(slotDw3) >= XHCI_SLOT_STATE_ADDRESSED) {
                c->Address = XHCI_SLOT_GET_ADDRESS(slotDw3);
            }
        }
        for (dci = 2; dci < 32 && c->Pipes < HCD_DOOR_PIPES; dci++) {
            pipe = dev->Pipes[dci];
            if (pipe == NULL || pipe->Closed) {
                continue;
            }
            wMax = (pipe->MaxPacketSize & 0x7FFUL) |
                   ((pipe->Ep.MaxBurstSize & 3UL) << 11);
            c->Pipe[c->Pipes][0] = 7;
            c->Pipe[c->Pipes][1] = 5;
            c->Pipe[c->Pipes][2] = (UCHAR)pipe->EndpointAddress;
            c->Pipe[c->Pipes][3] = (UCHAR)(pipe->TransferType & 3UL);
            c->Pipe[c->Pipes][4] = (UCHAR)(wMax & 0xFFUL);
            c->Pipe[c->Pipes][5] = (UCHAR)((wMax >> 8) & 0xFFUL);
            c->Pipe[c->Pipes][6] = (UCHAR)pipe->Interval;
            c->Pipes++;
        }
        XhciControllerLockRelease(&hc->Hc, lockIrql);
    } else if (!pdo->Function && pdo->Config != NULL) {
        source = pdo->Config;
        sourceBytes = pdo->ConfigLength;
    }
    c->ConfigTotal = sourceBytes;
    if (config != NULL && source != NULL) {
        c->ConfigBytes = (sourceBytes < configCap) ? sourceBytes : configCap;
        hcdCopy(config, source, c->ConfigBytes);
    }
    if (wantPdo) {
        c->Pdo = pdo->Common.Self;
        ObReferenceObject(c->Pdo);
    }
    KeReleaseSpinLock(&hc->PdoListLock, oldIrql);
}

/* USB_NODE_INFORMATION: a self-powered hub of MaxPorts ports, which the
 * Power tab reads as 500 mA per port (section 8.3). IRQL: any. */
static NTSTATUS hcdDoorNodeInfo(PHCD_CONTROLLER hc, PUCHAR buf, ULONG outLen,
                                PULONG_PTR info)
{
    ULONG ports;
    ULONG maskBytes;

    if (outLen < HCD_NODE_INFO_BYTES) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    ports = hcdDoorPorts(hc);
    maskBytes = (ports + 1 + 7) / 8;
    hcdZero(buf, HCD_NODE_INFO_BYTES);
    hcdPut32(buf, 0, 0);                    /* UsbHub                       */
    buf[4] = (UCHAR)(7 + 2 * maskBytes);    /* bDescriptorLength            */
    buf[5] = 0x29;                          /* the hub descriptor           */
    buf[6] = (UCHAR)ports;
    /* Individual port power switching and over-current protection - what
     * an xHCI root port has (PORTSC PP and OCA). */
    hcdPut16(buf, 7, 0x0009UL);
    buf[9] = 10;                            /* bPwrOn2PwrGood, 20 ms        */
    buf[10] = 0;                            /* bHubContrCurrent             */
    buf[75] = 0;                            /* HubIsBusPowered: FALSE       */
    *info = HCD_NODE_INFO_BYTES;
    return STATUS_SUCCESS;
}

/* USB_NODE_CONNECTION_INFORMATION and its _EX form, which differ in byte 23:
 * BOOLEAN LowSpeed, or the UCHAR Speed of usb200.h (0 low, 1 full, 2 high -
 * the ceiling). IRQL: <= DISPATCH_LEVEL. */
static NTSTATUS hcdDoorConnInfo(PHCD_CONTROLLER hc, PUCHAR buf, ULONG inLen,
                                ULONG outLen, ULONG ex, PULONG_PTR info)
{
    HCD_DOOR_CONN c;
    ULONG port;
    ULONG room;
    ULONG n;
    ULONG i;
    ULONG speed;

    if (inLen < 4 || outLen < HCD_CONN_INFO_BYTES) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    port = hcdGet32(buf, 0);
    if (port == 0 || port > hcdDoorPorts(hc)) {
        return STATUS_INVALID_PARAMETER;
    }
    hcdDoorConnection(hc, port, &c, NULL, 0, 0);

    hcdZero(buf + 4, HCD_CONN_INFO_BYTES - 4);
    hcdCopy(buf + 4, c.DeviceDesc, 18);
    buf[22] = (UCHAR)c.ConfigValue;
    if (ex) {
        speed = (c.SpeedClass == XHCI_SPEED_LOW)    ? 0UL
                : (c.SpeedClass == XHCI_SPEED_FULL) ? 1UL
                                                    : 2UL;
        buf[23] = (c.Status == HCD_CONN_CONNECTED) ? (UCHAR)speed : 0;
    } else {
        buf[23] = (c.SpeedClass == XHCI_SPEED_LOW) ? 1 : 0;
    }
    buf[24] = 0;                            /* DeviceIsHub                  */
    hcdPut16(buf, 25, c.Address);
    /* NumberOfOpenPipes is the device's count however few records fit, so
     * a caller can size its next request from it. */
    room = (outLen - HCD_CONN_INFO_BYTES) / HCD_PIPE_INFO_BYTES;
    n = (c.Pipes < room) ? c.Pipes : room;
    hcdPut32(buf, 27, c.Pipes);
    hcdPut32(buf, 31, c.Status);
    for (i = 0; i < n; i++) {
        hcdCopy(buf + HCD_CONN_INFO_BYTES + i * HCD_PIPE_INFO_BYTES,
                c.Pipe[i], 7);
        hcdPut32(buf, HCD_CONN_INFO_BYTES + i * HCD_PIPE_INFO_BYTES + 7, 0);
    }
    *info = HCD_CONN_INFO_BYTES + n * HCD_PIPE_INFO_BYTES;
    return STATUS_SUCCESS;
}

/*
 * USB_DESCRIPTOR_REQUEST, answered from the descriptors the bus read at
 * enumeration: the device descriptor, and configuration 0 (the one the bus
 * reads, and the one usbui.dll takes bMaxPower from). Anything else - a
 * string above all - would be a control transfer on the device's EP0, which
 * this door does not issue. IRQL: <= DISPATCH_LEVEL.
 */
static NTSTATUS hcdDoorDescriptor(PHCD_CONTROLLER hc, PUCHAR buf,
                                  ULONG inLen, ULONG outLen, PULONG_PTR info)
{
    HCD_DOOR_CONN c;
    ULONG port;
    ULONG request;
    ULONG value;
    ULONG length;
    ULONG room;
    ULONG n;

    if (inLen < HCD_DESC_REQUEST_BYTES || outLen < HCD_DESC_REQUEST_BYTES) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    port = hcdGet32(buf, 0);
    request = buf[5];
    value = (ULONG)buf[6] | ((ULONG)buf[7] << 8);
    length = (ULONG)buf[10] | ((ULONG)buf[11] << 8);
    if (port == 0 || port > hcdDoorPorts(hc) || request != 6) {
        return STATUS_INVALID_PARAMETER;
    }
    room = outLen - HCD_DESC_REQUEST_BYTES;
    if (room > length) {
        room = length;
    }
    if ((value >> 8) == 2 && (value & 0xFFUL) == 0) {
        hcdDoorConnection(hc, port, &c, buf + HCD_DESC_REQUEST_BYTES, room,
                          0);
        if (c.Status != HCD_CONN_CONNECTED || c.ConfigTotal == 0) {
            return STATUS_INVALID_PARAMETER;
        }
        n = c.ConfigBytes;
    } else if (value == 0x0100UL) {
        hcdDoorConnection(hc, port, &c, NULL, 0, 0);
        if (c.Status != HCD_CONN_CONNECTED) {
            return STATUS_INVALID_PARAMETER;
        }
        n = (room < 18) ? room : 18;
        hcdCopy(buf + HCD_DESC_REQUEST_BYTES, c.DeviceDesc, n);
    } else {
        return STATUS_NOT_SUPPORTED;
    }
    *info = HCD_DESC_REQUEST_BYTES + n;
    return STATUS_SUCCESS;
}

/* USB_NODE_CONNECTION_DRIVERKEY_NAME: the connection's devnode's driver
 * key, exactly, for usbui.dll's CM_DRP_DRIVER match (section 8.7 item 5).
 * IRQL: PASSIVE_LEVEL. */
static NTSTATUS hcdDoorConnDriverKey(PHCD_CONTROLLER hc, PUCHAR buf,
                                     ULONG inLen, ULONG outLen,
                                     PULONG_PTR info)
{
    HCD_DOOR_CONN c;
    WCHAR name[160];
    ULONG port;
    ULONG chars;
    NTSTATUS status;

    if (inLen < 4 || outLen < 8 + sizeof(WCHAR)) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    port = hcdGet32(buf, 0);
    if (port == 0 || port > hcdDoorPorts(hc)) {
        return STATUS_INVALID_PARAMETER;
    }
    hcdDoorConnection(hc, port, &c, NULL, 0, 1);
    if (c.Pdo == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    status = hcdDriverKey(c.Pdo, name, 160, &chars);
    ObDereferenceObject(c.Pdo);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    return hcdAnswerName(buf, outLen, 8, name, chars, info);
}

/*
 * IRP_MJ_DEVICE_CONTROL on the root-hub FDO (section 8.3). `hc` is NULL on
 * an orphaned root hub, which answers nothing. RESET_HUB is refused, as
 * section 8.7 allows. IRQL: PASSIVE_LEVEL, the caller inside the root-hub
 * FDO's I/O count.
 */
NTSTATUS HcdDoorRootHubIoctl(PHCD_CONTROLLER hc, PIRP irp)
{
    PIO_STACK_LOCATION stack;
    PUCHAR buf;
    ULONG inLen;
    ULONG outLen;
    ULONG_PTR info;
    HCD_DOOR_CONN c;
    ULONG port;
    NTSTATUS status;

    stack = IoGetCurrentIrpStackLocation(irp);
    buf = (PUCHAR)irp->AssociatedIrp.SystemBuffer;
    inLen = stack->Parameters.DeviceIoControl.InputBufferLength;
    outLen = stack->Parameters.DeviceIoControl.OutputBufferLength;
    info = 0;
    if (hc == NULL) {
        return HcdCompleteIrp(irp, STATUS_DEVICE_NOT_CONNECTED, 0);
    }
    hc->DoorRequests++;
    if (buf == NULL) {
        return HcdCompleteIrp(irp, STATUS_BUFFER_TOO_SMALL, 0);
    }

    switch (stack->Parameters.DeviceIoControl.IoControlCode) {
    case HCD_IOCTL_NODE_INFO:
        status = hcdDoorNodeInfo(hc, buf, outLen, &info);
        break;

    case HCD_IOCTL_CONN_INFO:
        status = hcdDoorConnInfo(hc, buf, inLen, outLen, 0, &info);
        break;

    case HCD_IOCTL_CONN_INFO_EX:
        status = hcdDoorConnInfo(hc, buf, inLen, outLen, 1, &info);
        break;

    case HCD_IOCTL_CONN_DESCRIPTOR:
        status = hcdDoorDescriptor(hc, buf, inLen, outLen, &info);
        break;

    case HCD_IOCTL_CONN_NAME:
        /* No connection is a hub devnode (DeviceIsHub is FALSE): the empty
         * name usbhub gives a connection that is not a hub. */
        if (inLen < 4) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        status = hcdAnswerName(buf, outLen, 8, L"", 0, &info);
        break;

    case HCD_IOCTL_CONN_DRIVERKEY:
        status = hcdDoorConnDriverKey(hc, buf, inLen, outLen, &info);
        break;

    case HCD_IOCTL_CONN_ATTRIBUTES:
        if (inLen < 4 || outLen < HCD_CONN_ATTR_BYTES) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        port = hcdGet32(buf, 0);
        if (port == 0 || port > hcdDoorPorts(hc)) {
            status = STATUS_INVALID_PARAMETER;
            break;
        }
        hcdDoorConnection(hc, port, &c, NULL, 0, 0);
        hcdPut32(buf, 4, c.Status);
        hcdPut32(buf, 8, 0);                /* PortAttributes               */
        info = HCD_CONN_ATTR_BYTES;
        status = STATUS_SUCCESS;
        break;

    case HCD_IOCTL_HUB_CAPS:
        if (outLen < 4) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        hcdPut32(buf, 0, 1);                /* HubIs2xCapable               */
        info = 4;
        status = STATUS_SUCCESS;
        break;

    case HCD_IOCTL_HUB_CAPS_EX:
        if (outLen < 4) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        hcdPut32(buf, 0, HCD_HUB_CAPS_EX_ROOT_HS);
        info = 4;
        status = STATUS_SUCCESS;
        break;

    case HCD_IOCTL_RESET_HUB:
    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }
    return HcdCompleteIrp(irp, status, NT_SUCCESS(status) ? info : 0);
}
