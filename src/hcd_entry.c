/*
 * hcd_entry.c - DriverEntry, AddDevice and the dispatch table of xhci98.sys,
 * the successor host controller driver (design record 13 section 5.2;
 * roadmap-hcd.md tasks 26-A.1 and 26-A.2).
 *
 * The image carries two strings read once in DriverEntry so the linker keeps
 * them: the flavour marker, XHCI98_FLAVOUR_*, which
 * scripts\check-flavour-marker.ps1 and make-package.ps1 read out of the bytes,
 * and the version string. Task 25.8's scaffold marker left with task 26-A.1,
 * which made the image stageable.
 *
 * IRQL: DriverEntry, AddDevice, Unload and the PnP dispatch run at
 * PASSIVE_LEVEL. The power dispatch runs at PASSIVE_LEVEL because every object
 * this driver creates is DO_POWER_PAGABLE. HcdIoEnter, HcdIoLeave and
 * HcdCompleteIrp are callable at <= DISPATCH_LEVEL.
 */

#include "hcd.h"
#include "xhci_version.h"

#if defined(XHCI_FLAVOUR_QEMU)
#define HCD_FLAVOUR_NAME "XHCI98_FLAVOUR_QEMU"
#elif defined(XHCI_FLAVOUR_DEBUG)
#define HCD_FLAVOUR_NAME "XHCI98_FLAVOUR_DEBUG"
#elif defined(XHCI_FLAVOUR_RELEASE)
#define HCD_FLAVOUR_NAME "XHCI98_FLAVOUR_RELEASE"
#else
#error "no build flavour was defined. src/sources derives XHCI_FLAVOUR_RELEASE, _DEBUG or _QEMU from BUILD_ALT_DIR; build through scripts\build-driver.cmd."
#endif

static volatile const char HcdFlavourMarker[] = HCD_FLAVOUR_NAME;

static volatile const char HcdVersionString[] = "xhci98 " XHCI_VER_STR;

PDRIVER_OBJECT HcdDriverObject;

static LONG hcdFdoSerial;

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath);
static NTSTATUS NTAPI hcdAddDevice(PDRIVER_OBJECT DriverObject,
                                   PDEVICE_OBJECT Pdo);
static VOID NTAPI hcdUnload(PDRIVER_OBJECT DriverObject);
static NTSTATUS NTAPI hcdDispatchPnp(PDEVICE_OBJECT DeviceObject, PIRP Irp);
static NTSTATUS NTAPI hcdDispatchPower(PDEVICE_OBJECT DeviceObject, PIRP Irp);
static NTSTATUS NTAPI hcdDispatchOther(PDEVICE_OBJECT DeviceObject, PIRP Irp);

ULONG HcdIoEnter(PHCD_CONTROLLER hc)
{
    (VOID)InterlockedIncrement(&hc->OutstandingIo);
    if (hc->Common.PnpState == HCD_PNP_REMOVED) {
        HcdIoLeave(hc);
        return 0;
    }
    return 1;
}

VOID HcdIoLeave(PHCD_CONTROLLER hc)
{
    if (InterlockedDecrement(&hc->OutstandingIo) == 0) {
        (VOID)KeSetEvent(&hc->RemoveEvent, IO_NO_INCREMENT, FALSE);
    }
}

NTSTATUS HcdCompleteIrp(PIRP irp, NTSTATUS status, ULONG_PTR information)
{
    irp->IoStatus.Status = status;
    irp->IoStatus.Information = information;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
}

NTSTATUS HcdPassDown(PHCD_CONTROLLER hc, PIRP irp)
{
    IoSkipCurrentIrpStackLocation(irp);
    return IoCallDriver(hc->LowerDevice, irp);
}

NTSTATUS
DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    ULONG i;

    UNREFERENCED_PARAMETER(RegistryPath);

    if (HcdFlavourMarker[0] != 'X' || HcdVersionString[0] != 'x') {
        return STATUS_UNSUCCESSFUL;
    }

    HcdDriverObject = DriverObject;
    HcdUrbInit();
    HcdPdoRetireInit();
    for (i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; i++) {
        DriverObject->MajorFunction[i] = hcdDispatchOther;
    }
    DriverObject->MajorFunction[IRP_MJ_PNP] = hcdDispatchPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = hcdDispatchPower;
    DriverObject->DriverExtension->AddDevice = hcdAddDevice;
    DriverObject->DriverUnload = hcdUnload;
    return STATUS_SUCCESS;
}

static VOID NTAPI hcdUnload(PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
}

static NTSTATUS NTAPI hcdAddDevice(PDRIVER_OBJECT DriverObject,
                                   PDEVICE_OBJECT Pdo)
{
    PDEVICE_OBJECT fdo;
    PHCD_CONTROLLER hc;
    NTSTATUS status;
    UNICODE_STRING name;
    WCHAR nameBuffer[32];
    ULONG serial;
    ULONG n;
    ULONG d;
    WCHAR digits[12];
    static const WCHAR prefix[] = L"\\Device\\XHCI98HC";

    /* A PDO of this driver's own is the root hub's: the second role
     * (design record 13 section 5.2). */
    if (Pdo->DriverObject == DriverObject) {
        return HcdRootHubAddDevice(DriverObject, Pdo);
    }

    /* Named, because \DosDevices\HCD<n> must point at a name (hcd_door.c);
     * usbport names its FDO \Device\USBFDO-<n> for the same link. */
    serial = (ULONG)InterlockedIncrement(&hcdFdoSerial);
    n = 0;
    for (d = 0; prefix[d] != 0; d++) {
        nameBuffer[n++] = prefix[d];
    }
    d = 0;
    do {
        digits[d++] = (WCHAR)(L'0' + (serial % 10UL));
        serial /= 10UL;
    } while (serial != 0 && d < 11);
    while (d > 0) {
        nameBuffer[n++] = digits[--d];
    }
    nameBuffer[n] = 0;
    RtlInitUnicodeString(&name, nameBuffer);
    status = IoCreateDevice(DriverObject, sizeof(HCD_CONTROLLER), &name,
                            FILE_DEVICE_CONTROLLER, 0, FALSE, &fdo);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    /* IoCreateDevice zeroes the extension; no RtlZeroMemory, which the DDK
     * spells as memset and which would arrive as an unlisted import. */
    hc = (PHCD_CONTROLLER)fdo->DeviceExtension;
    hc->Common.Kind = HCD_KIND_CONTROLLER_FDO;
    hc->Common.Self = fdo;
    hc->Common.PnpState = HCD_PNP_ADDED;
    hc->Common.DevicePower = PowerDeviceD3;
    hc->Common.SystemPower = PowerSystemWorking;
    hc->Pdo = Pdo;
    hc->FdoSerial = (ULONG)hcdFdoSerial;
    hc->OutstandingIo = 1;
    KeInitializeEvent(&hc->RemoveEvent, NotificationEvent, FALSE);
    HcdControllerInitObjects(hc);

    hc->LowerDevice = IoAttachDeviceToDeviceStack(fdo, Pdo);
    if (hc->LowerDevice == NULL) {
        IoDeleteDevice(fdo);
        return STATUS_NO_SUCH_DEVICE;
    }

    fdo->Flags |= DO_POWER_PAGABLE;
    fdo->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

static NTSTATUS NTAPI hcdDispatchPnp(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PHCD_COMMON common;

    common = (PHCD_COMMON)DeviceObject->DeviceExtension;
    if (common->Kind == HCD_KIND_CONTROLLER_FDO) {
        return HcdControllerPnp((PHCD_CONTROLLER)common, Irp);
    }
    if (common->Kind == HCD_KIND_ROOTHUB_PDO) {
        return HcdRootHubPdoPnp((PHCD_ROOTHUB_PDO)common, Irp);
    }
    if (common->Kind == HCD_KIND_ROOTHUB_FDO) {
        return HcdRootHubFdoPnp((PHCD_ROOTHUB_FDO)common, Irp);
    }
    if (common->Kind == HCD_KIND_DEVICE_PDO) {
        return HcdDevicePdoPnp((PHCD_DEVICE_PDO)common, Irp);
    }
    return HcdCompleteIrp(Irp, Irp->IoStatus.Status, Irp->IoStatus.Information);
}

static NTSTATUS NTAPI hcdDispatchPower(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PHCD_COMMON common;

    common = (PHCD_COMMON)DeviceObject->DeviceExtension;
    if (common->Kind == HCD_KIND_CONTROLLER_FDO) {
        return HcdControllerPower((PHCD_CONTROLLER)common, Irp);
    }
    if (common->Kind == HCD_KIND_ROOTHUB_PDO) {
        return HcdRootHubPdoPower((PHCD_ROOTHUB_PDO)common, Irp);
    }
    if (common->Kind == HCD_KIND_ROOTHUB_FDO) {
        return HcdRootHubFdoPower((PHCD_ROOTHUB_FDO)common, Irp);
    }
    if (common->Kind == HCD_KIND_DEVICE_PDO) {
        return HcdDevicePdoPower((PHCD_DEVICE_PDO)common, Irp);
    }
    PoStartNextPowerIrp(Irp);
    return HcdCompleteIrp(Irp, Irp->IoStatus.Status, Irp->IoStatus.Information);
}

/* Everything that is neither PnP nor power. The door (hcd_door.c, 26-A.8)
 * takes CREATE, CLOSE, CLEANUP and DEVICE_CONTROL on both FDOs; the
 * controller FDO forwards the rest to the PCI stack (IRP_MJ_SYSTEM_CONTROL is
 * the one that arrives there). */
static NTSTATUS NTAPI hcdDispatchOther(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PHCD_COMMON common;
    PHCD_CONTROLLER hc;
    NTSTATUS status;
    UCHAR major;

    common = (PHCD_COMMON)DeviceObject->DeviceExtension;
    major = IoGetCurrentIrpStackLocation(Irp)->MajorFunction;
    if (common->Kind == HCD_KIND_DEVICE_PDO &&
        major == IRP_MJ_INTERNAL_DEVICE_CONTROL) {
        /* The function-driver contract (hcd_urb.c, 26-A.5). */
        return HcdDevicePdoInternalIoctl((PHCD_DEVICE_PDO)common, Irp);
    }
    if ((common->Kind == HCD_KIND_CONTROLLER_FDO ||
         common->Kind == HCD_KIND_ROOTHUB_FDO) &&
        (major == IRP_MJ_CREATE || major == IRP_MJ_CLOSE ||
         major == IRP_MJ_CLEANUP)) {
        return HcdDoorCreateClose(Irp);
    }
    if (common->Kind == HCD_KIND_ROOTHUB_FDO &&
        major == IRP_MJ_DEVICE_CONTROL) {
        return HcdRootHubFdoDeviceControl((PHCD_ROOTHUB_FDO)common, Irp);
    }
    if (common->Kind != HCD_KIND_CONTROLLER_FDO) {
        return HcdCompleteIrp(Irp, STATUS_NOT_SUPPORTED, 0);
    }
    hc = (PHCD_CONTROLLER)common;
    if (!HcdIoEnter(hc)) {
        return HcdCompleteIrp(Irp, STATUS_DELETE_PENDING, 0);
    }
    if (major == IRP_MJ_DEVICE_CONTROL) {
        status = HcdDoorControllerIoctl(hc, Irp);
    } else {
        status = HcdPassDown(hc, Irp);
    }
    HcdIoLeave(hc);
    return status;
}
