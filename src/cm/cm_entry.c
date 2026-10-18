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
#include "cm_nvlink.h"
#include "cm_ports.h"

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

/* TRUE when a bridge filter of this driver is already on the PDO's stack:
 * a bridge the filter INF names and whose LowerFilters value the mark
 * service wrote calls AddDevice twice, once as a lower and once as an
 * upper filter, and the first object serves both (decision M15). The
 * stack is being built by the PnP manager, which is this call's caller,
 * so it holds still. PASSIVE_LEVEL. */
static BOOLEAN cmBridgeOnStack(PDEVICE_OBJECT pdo)
{
    PDEVICE_OBJECT walk;
    ULONG n;

    n = 0;
    for (walk = pdo->AttachedDevice; walk != NULL && n < 16;
         walk = walk->AttachedDevice) {
        if (walk->DriverObject == CmDriverObject &&
            ((PCM_COMMON)walk->DeviceExtension)->Kind == CM_KIND_FILTER) {
            return TRUE;
        }
        n++;
    }
    return FALSE;
}

/*
 * Which role this PDO is (record 21 section 3.2; since task 42-A.5,
 * section 7.4 revision 3's CmRoleDecide). Windows 98 SE and ME
 * (IoIsWdmVersionAvailable(1, 0x10) FALSE) have the NHI role alone: no
 * INF of this driver that their engine reads names a bridge, no mark
 * service runs there, and a WDM driver on a bridge devnode there breaks
 * the bridge (section 6.1), so the ids are not read. On the NT targets the
 * ids decide, and every PDO that is neither an NHI nor a filter INF's
 * bridge is the LowerFilters child, which never fails AddDevice.
 */
static NTSTATUS cmAddDevice(PDRIVER_OBJECT driver, PDEVICE_OBJECT pdo)
{
    WCHAR ids[CM_ID_CHARS];
    ULONG bytes;
    CM_IDS idsSeen;
    NTSTATUS status;
    ULONG role;

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
    role = CmRoleDecide(&idsSeen,
                        (ULONG)CmPortsFilterId(CM_PORTS_ID(idsSeen.Vendor,
                                                           idsSeen.Device)));
    if (role == CM_ROLE_FILTER) {
        if (cmBridgeOnStack(pdo)) {
            return STATUS_SUCCESS;
        }
        return CmFilterAddDevice(driver, pdo);
    }
    if (role == CM_ROLE_NHI) {
        return CmNhiAddDevice(driver, pdo, &idsSeen);
    }
    return CmChildAddDevice(driver, pdo);
}

static VOID cmUnload(PDRIVER_OBJECT driver)
{
    UNREFERENCED_PARAMETER(driver);
    /* A 98 CONFIGMG worker a stop could not wait for leaves on its own
     * (cm_cfgwk.c): its code is this image's. */
    CmCfgwkDrain();
    CmNvlinkUnload();
}

/* Every dispatch routine tells the query device (CM_KIND_QUERY,
 * cm_qryos.c) from the PnP roles first: it has no lower object. */
static NTSTATUS cmDispatchPnp(PDEVICE_OBJECT device, PIRP irp)
{
    if (((PCM_COMMON)device->DeviceExtension)->Kind == CM_KIND_QUERY) {
        return CmQueryDispatch(device, irp);
    }
    if (((PCM_COMMON)device->DeviceExtension)->Kind == CM_KIND_NHI) {
        return CmNhiPnp((PCM_NHI)device->DeviceExtension, irp);
    }
    return CmFilterPnp((PCM_FILTER)device->DeviceExtension, irp);
}

static NTSTATUS cmDispatchPower(PDEVICE_OBJECT device, PIRP irp)
{
    if (((PCM_COMMON)device->DeviceExtension)->Kind == CM_KIND_QUERY) {
        return CmQueryDispatch(device, irp);
    }
    if (((PCM_COMMON)device->DeviceExtension)->Kind == CM_KIND_NHI) {
        return CmNhiPower((PCM_NHI)device->DeviceExtension, irp);
    }
    return CmFilterPower((PCM_FILTER)device->DeviceExtension, irp);
}

/* Every other request, on any PnP role, goes down unchanged: the NHI's
 * interface is the query device's, and the filters add nothing to their
 * device.
 * The PnP and power dispatch send both filter kinds (CM_KIND_FILTER and
 * CM_KIND_CHILD) to cm_filt.c. */
static NTSTATUS cmDispatchOther(PDEVICE_OBJECT device, PIRP irp)
{
    PCM_COMMON common;

    common = (PCM_COMMON)device->DeviceExtension;
    if (common->Kind == CM_KIND_QUERY) {
        return CmQueryDispatch(device, irp);
    }
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
    CmRegInit();
    CmNvlinkInit();
    CmQueryInit();
    for (i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; i++) {
        DriverObject->MajorFunction[i] = cmDispatchOther;
    }
    DriverObject->MajorFunction[IRP_MJ_PNP] = cmDispatchPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = cmDispatchPower;
    DriverObject->DriverExtension->AddDevice = cmAddDevice;
    DriverObject->DriverUnload = cmUnload;
    if (!IoIsWdmVersionAvailable(1, 0x10)) {
        /* Windows 98 SE and ME: resident for the boot, whatever the PnP
         * devices do (stor98cm.h R4: the link-state channel's proof key is
         * answered from this image, cm_nvlinkos.c), as a CONFIGMG worker's
         * start made it before. */
        DriverObject->DriverUnload = NULL;
    }
    return STATUS_SUCCESS;
}
