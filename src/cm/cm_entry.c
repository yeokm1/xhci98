/*
 * cm_entry.c - DriverEntry, AddDevice and the dispatch table of
 * xhcicm.sys (cm.h has the two roles).
 *
 * The image carries the flavour marker XHCI98_FLAVOUR_* that
 * scripts\check-flavour-marker.ps1 and make-package.ps1 read out of the
 * bytes, from the define src\cm\sources derives from BUILD_ALT_DIR as
 * src\sources does for xhci98.sys, and the package version string.
 *
 * IRQL: DriverEntry, AddDevice, Unload, the PnP and power dispatch and
 * CmForwardSync at PASSIVE_LEVEL; CmCompleteIrp, CmZero and the other
 * dispatch routines at <= DISPATCH_LEVEL.
 */

#include "cm.h"
#include "../xhci_version.h"

#if defined(XHCI_FLAVOUR_QEMU)
#define CM_FLAVOUR_NAME "XHCI98_FLAVOUR_QEMU"
#elif defined(XHCI_FLAVOUR_DEBUG)
#define CM_FLAVOUR_NAME "XHCI98_FLAVOUR_DEBUG"
#elif defined(XHCI_FLAVOUR_RELEASE)
#define CM_FLAVOUR_NAME "XHCI98_FLAVOUR_RELEASE"
#else
#error "no build flavour was defined. src/cm/sources derives XHCI_FLAVOUR_RELEASE, _DEBUG or _QEMU from BUILD_ALT_DIR; build through scripts\build-driver.cmd."
#endif

static volatile const char CmFlavourMarker[] = CM_FLAVOUR_NAME;
static volatile const char CmVersionString[] = "xhcicm " XHCI_VER_STR;

PDRIVER_OBJECT CmDriverObject;

/* The hardware id list IoGetDeviceProperty returns for a PCI function:
 * four or five ids of about 50 characters each. A longer list is read as
 * no list (CM_ROLE_NONE). */
#define CM_ID_CHARS 512

VOID CmZero(PVOID p, ULONG n)
{
    PUCHAR b;
    ULONG i;

    b = (PUCHAR)p;
    for (i = 0; i < n; i++) {
        b[i] = 0;
    }
}

NTSTATUS CmCompleteIrp(PIRP irp, NTSTATUS status, ULONG_PTR information)
{
    irp->IoStatus.Status = status;
    irp->IoStatus.Information = information;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
}

static NTSTATUS cmSignal(PDEVICE_OBJECT device, PIRP irp, PVOID context)
{
    UNREFERENCED_PARAMETER(device);
    UNREFERENCED_PARAMETER(irp);
    KeSetEvent((PKEVENT)context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

/* The IRP passed down and waited for; the caller completes it. */
NTSTATUS CmForwardSync(PCM_COMMON common, PIRP irp)
{
    KEVENT done;

    KeInitializeEvent(&done, NotificationEvent, FALSE);
    IoCopyCurrentIrpStackLocationToNext(irp);
    CM_SET_COMPLETION_ALWAYS(irp, cmSignal, &done);
    (VOID)IoCallDriver(common->Lower, irp);
    (VOID)KeWaitForSingleObject(&done, Executive, KernelMode, FALSE, NULL);
    return irp->IoStatus.Status;
}

/*
 * Which role this PDO is (record 21 section 3.2). Windows 98 SE and ME
 * (IoIsWdmVersionAvailable(1, 0x10) FALSE) have the NHI role alone: no
 * INF of this driver that their engine reads names a bridge, and a WDM
 * driver on a bridge devnode there breaks the bridge (section 6.1), so
 * the ids are not read. On the NT targets the ids decide; a list that
 * cannot be read takes no role, which declines a filter's attach rather
 * than fail the bridge it would sit on.
 */
static NTSTATUS cmAddDevice(PDRIVER_OBJECT driver, PDEVICE_OBJECT pdo)
{
    WCHAR ids[CM_ID_CHARS];
    ULONG bytes;
    CM_IDS idsSeen;
    NTSTATUS status;

    CmZero(&idsSeen, sizeof(idsSeen));
    if (!IoIsWdmVersionAvailable(1, 0x10)) {
        idsSeen.Role = CM_ROLE_NHI;
        return CmNhiAddDevice(driver, pdo, &idsSeen);
    }
    bytes = 0;
    status = IoGetDeviceProperty(pdo, DevicePropertyHardwareID,
                                 sizeof(ids) - 2 * sizeof(WCHAR), ids,
                                 &bytes);
    if (NT_SUCCESS(status) && bytes <= sizeof(ids) - 2 * sizeof(WCHAR)) {
        /* Two terminators past what was returned, so a list the bus did
         * not end with an empty string still ends here. */
        ids[bytes / sizeof(WCHAR)] = 0;
        ids[bytes / sizeof(WCHAR) + 1] = 0;
        (VOID)CmClassifyIds(ids, bytes / sizeof(WCHAR) + 2, &idsSeen);
    }
    if (idsSeen.Role == CM_ROLE_FILTER) {
        return CmFilterAddDevice(driver, pdo);
    }
    if (idsSeen.Role == CM_ROLE_NHI) {
        return CmNhiAddDevice(driver, pdo, &idsSeen);
    }
    return STATUS_SUCCESS;
}

static VOID cmUnload(PDRIVER_OBJECT driver)
{
    UNREFERENCED_PARAMETER(driver);
}

static NTSTATUS cmDispatchPnp(PDEVICE_OBJECT device, PIRP irp)
{
    if (((PCM_COMMON)device->DeviceExtension)->Kind == CM_KIND_NHI) {
        return CmNhiPnp((PCM_NHI)device->DeviceExtension, irp);
    }
    return CmFilterPnp((PCM_FILTER)device->DeviceExtension, irp);
}

static NTSTATUS cmDispatchPower(PDEVICE_OBJECT device, PIRP irp)
{
    if (((PCM_COMMON)device->DeviceExtension)->Kind == CM_KIND_NHI) {
        return CmNhiPower((PCM_NHI)device->DeviceExtension, irp);
    }
    return CmFilterPower((PCM_FILTER)device->DeviceExtension, irp);
}

/* Every other request, on either role, goes down unchanged: the NHI has no
 * interface of its own yet, and the filter adds nothing to its bridge. */
static NTSTATUS cmDispatchOther(PDEVICE_OBJECT device, PIRP irp)
{
    PCM_COMMON common;

    common = (PCM_COMMON)device->DeviceExtension;
    IoSkipCurrentIrpStackLocation(irp);
    return IoCallDriver(common->Lower, irp);
}

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    ULONG i;

    UNREFERENCED_PARAMETER(RegistryPath);
    /* Read once, so the linker keeps both strings in the image. */
    if (CmFlavourMarker[0] == 0 || CmVersionString[0] == 0) {
        return STATUS_UNSUCCESSFUL;
    }
    CmDriverObject = DriverObject;
    for (i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; i++) {
        DriverObject->MajorFunction[i] = cmDispatchOther;
    }
    DriverObject->MajorFunction[IRP_MJ_PNP] = cmDispatchPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = cmDispatchPower;
    DriverObject->DriverExtension->AddDevice = cmAddDevice;
    DriverObject->DriverUnload = cmUnload;
    return STATUS_SUCCESS;
}
