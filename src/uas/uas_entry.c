/*
 * uas_entry.c - DriverEntry, AddDevice and the dispatch table of
 * xhciuas.sys (uas.h has the stack and the transports).
 *
 * The image carries the flavour marker XHCI98_FLAVOUR_* that
 * scripts\check-flavour-marker.ps1 and make-package.ps1 read out of the
 * bytes, from the same define src\uas\sources derives from BUILD_ALT_DIR as
 * src\sources does for xhci98.sys, and the package version string.
 *
 * IRQL: DriverEntry, AddDevice, Unload and the PnP and power dispatch at
 * PASSIVE_LEVEL (every object this driver creates is DO_POWER_PAGABLE). The
 * SCSI and device-control dispatch, UasCompleteIrp, UasZero and UasCopy at
 * <= DISPATCH_LEVEL.
 */

#include "uas.h"
#include "../xhci_version.h"

#if defined(XHCI_FLAVOUR_QEMU)
#define UAS_FLAVOUR_NAME "XHCI98_FLAVOUR_QEMU"
#elif defined(XHCI_FLAVOUR_RELEASE)
#define UAS_FLAVOUR_NAME "XHCI98_FLAVOUR_RELEASE"
#else
#error "no build flavour was defined. src/uas/sources derives XHCI_FLAVOUR_RELEASE or _QEMU from BUILD_ALT_DIR (the debug flavour was retired with task 45.45); build through scripts\build-driver.cmd."
#endif

static volatile const char UasFlavourMarker[] = UAS_FLAVOUR_NAME;
static volatile const char UasVersionString[] = "xhciuas " XHCI_VER_STR;

PDRIVER_OBJECT UasDriverObject;
/* TRUE on Windows 2000 and later (WDM 1.10), FALSE on 98 SE and ME. Set in
 * DriverEntry, read only afterwards. */
BOOLEAN UasIsNt;

/*
 * An event wait that returns only once the event is signalled. Windows 98
 * can return from KeWaitForSingleObject unsatisfied (0xFFFFFFFF,
 * docs/usb-xhci-info/win98-wdm.md), and every caller here goes on to
 * free, delete or reuse what the signaller still holds, so any other
 * return is waited again after a millisecond. No timeout: each caller's
 * event is one a completion or a rundown sets. IRQL: PASSIVE_LEVEL.
 */
VOID UasWaitEvent(PKEVENT event)
{
    LARGE_INTEGER due;

    while (KeWaitForSingleObject(event, Executive, KernelMode, FALSE,
                                 NULL) != STATUS_SUCCESS) {
        due.LowPart = (ULONG)-10000L;
        due.HighPart = -1;
        (VOID)KeDelayExecutionThread(KernelMode, FALSE, &due);
    }
}

VOID UasZero(PVOID p, ULONG n)
{
    PUCHAR b;
    ULONG i;

    b = (PUCHAR)p;
    for (i = 0; i < n; i++) {
        b[i] = 0;
    }
}

VOID UasCopy(PVOID dst, const VOID *src, ULONG n)
{
    PUCHAR d;
    const UCHAR *s;
    ULONG i;

    d = (PUCHAR)dst;
    s = (const UCHAR *)src;
    for (i = 0; i < n; i++) {
        d[i] = s[i];
    }
}

NTSTATUS UasCompleteIrp(PIRP irp, NTSTATUS status, ULONG_PTR information)
{
    irp->IoStatus.Status = status;
    irp->IoStatus.Information = information;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
}

static NTSTATUS uasAddDevice(PDRIVER_OBJECT driver, PDEVICE_OBJECT pdo)
{
    PDEVICE_OBJECT self;
    PUAS_FDO fdo;
    NTSTATUS status;

    status = IoCreateDevice(driver, sizeof(UAS_FDO), NULL,
                            FILE_DEVICE_MASS_STORAGE, 0, FALSE, &self);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    fdo = (PUAS_FDO)self->DeviceExtension;
    UasZero(fdo, sizeof(UAS_FDO));
    fdo->Kind = UAS_KIND_FDO;
    fdo->Self = self;
    fdo->Pdo = pdo;
    KeInitializeSpinLock(&fdo->Lock);
    InitializeListHead(&fdo->Queue);
    InitializeListHead(&fdo->RecoveryRequests);
    KeInitializeEvent(&fdo->TimerDone, NotificationEvent, TRUE);
    fdo->IoCount = 1;
    KeInitializeEvent(&fdo->IoIdle, NotificationEvent, FALSE);
    /* 64 KiB until the bus has answered otherwise (task 36.22). */
    fdo->CommandBytes = UAS_MAX_TRANSFER;
    fdo->PolicySource = XHCI98_TELE_SRC_UNASKED;
    fdo->Lower = IoAttachDeviceToDeviceStack(self, pdo);
    if (fdo->Lower == NULL) {
        IoDeleteDevice(self);
        return STATUS_NO_SUCH_DEVICE;
    }
    self->Flags |= DO_DIRECT_IO | DO_POWER_PAGABLE;
    self->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

static VOID uasUnload(PDRIVER_OBJECT driver)
{
    UNREFERENCED_PARAMETER(driver);
}

static NTSTATUS uasDispatchPnp(PDEVICE_OBJECT device, PIRP irp)
{
    ULONG kind;

    kind = *(PULONG)device->DeviceExtension;
    if (kind == UAS_KIND_FDO) {
        return UasFdoPnp((PUAS_FDO)device->DeviceExtension, irp);
    }
    return UasPdoPnp((PUAS_PDO)device->DeviceExtension, irp);
}

static NTSTATUS uasDispatchPower(PDEVICE_OBJECT device, PIRP irp)
{
    ULONG kind;

    kind = *(PULONG)device->DeviceExtension;
    if (kind == UAS_KIND_FDO) {
        return UasFdoPower((PUAS_FDO)device->DeviceExtension, irp);
    }
    return UasPdoPower((PUAS_PDO)device->DeviceExtension, irp);
}

/* IRP_MJ_SCSI is IRP_MJ_INTERNAL_DEVICE_CONTROL. On the FDO nothing above
 * sends one; it is passed down unchanged. */
static NTSTATUS uasDispatchInternal(PDEVICE_OBJECT device, PIRP irp)
{
    PUAS_FDO fdo;
    ULONG kind;

    kind = *(PULONG)device->DeviceExtension;
    if (kind == UAS_KIND_PDO) {
        return UasPdoScsi((PUAS_PDO)device->DeviceExtension, irp);
    }
    fdo = (PUAS_FDO)device->DeviceExtension;
    IoSkipCurrentIrpStackLocation(irp);
    return IoCallDriver(fdo->Lower, irp);
}

static NTSTATUS uasDispatchControl(PDEVICE_OBJECT device, PIRP irp)
{
    PUAS_FDO fdo;
    ULONG kind;

    kind = *(PULONG)device->DeviceExtension;
    if (kind == UAS_KIND_PDO) {
        return UasPdoDeviceControl((PUAS_PDO)device->DeviceExtension, irp);
    }
    fdo = (PUAS_FDO)device->DeviceExtension;
    IoSkipCurrentIrpStackLocation(irp);
    return IoCallDriver(fdo->Lower, irp);
}

/* Create and close succeed on both kinds; there is no per-handle state. */
static NTSTATUS uasDispatchCreateClose(PDEVICE_OBJECT device, PIRP irp)
{
    UNREFERENCED_PARAMETER(device);
    return UasCompleteIrp(irp, STATUS_SUCCESS, 0);
}

/* Anything else: the FDO passes it down, a PDO refuses it. */
static NTSTATUS uasDispatchOther(PDEVICE_OBJECT device, PIRP irp)
{
    PUAS_FDO fdo;
    ULONG kind;

    kind = *(PULONG)device->DeviceExtension;
    if (kind == UAS_KIND_FDO) {
        fdo = (PUAS_FDO)device->DeviceExtension;
        IoSkipCurrentIrpStackLocation(irp);
        return IoCallDriver(fdo->Lower, irp);
    }
    return UasCompleteIrp(irp, STATUS_INVALID_DEVICE_REQUEST, 0);
}

/* WMI: the FDO passes it down; a PDO has none and completes it as it came. */
static NTSTATUS uasDispatchSystemControl(PDEVICE_OBJECT device, PIRP irp)
{
    PUAS_FDO fdo;
    NTSTATUS status;
    ULONG kind;

    kind = *(PULONG)device->DeviceExtension;
    if (kind == UAS_KIND_FDO) {
        fdo = (PUAS_FDO)device->DeviceExtension;
        IoSkipCurrentIrpStackLocation(irp);
        return IoCallDriver(fdo->Lower, irp);
    }
    status = irp->IoStatus.Status;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
}

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    ULONG i;

    UNREFERENCED_PARAMETER(RegistryPath);
    /* Read once, so the linker keeps both strings in the image. */
    if (UasFlavourMarker[0] == 0 || UasVersionString[0] == 0) {
        return STATUS_UNSUCCESSFUL;
    }
    UasDriverObject = DriverObject;
    UasIsNt = IoIsWdmVersionAvailable(1, 0x10);
    UasPoolInit();
    for (i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; i++) {
        DriverObject->MajorFunction[i] = uasDispatchOther;
    }
    DriverObject->MajorFunction[IRP_MJ_CREATE] = uasDispatchCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = uasDispatchCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLEANUP] = uasDispatchCreateClose;
    DriverObject->MajorFunction[IRP_MJ_PNP] = uasDispatchPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = uasDispatchPower;
    DriverObject->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] =
        uasDispatchInternal;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = uasDispatchControl;
    DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] =
        uasDispatchSystemControl;
    DriverObject->DriverExtension->AddDevice = uasAddDevice;
    /* Windows 98 SE and ME: the image stays resident for the boot
     * (roadmap-usb4.md task 46.20). Their NTKERN calls DriverUnload from
     * the IoDeleteDevice of the driver's last device object
     * (win98-wdm.md), and the recovery and wake workers (ExQueueWorkItem;
     * 9x has no IoQueueWorkItem) release the rundown the removal waits for
     * (uasDeref) before they return through this image: the rundown proves
     * their last device access, not that they have left the image.
     * xhcicm.sys's rule (cm_entry.c). NT keeps uasUnload; the same worker
     * gap remains open there. */
    DriverObject->DriverUnload = UasIsNt ? uasUnload : NULL;
    return STATUS_SUCCESS;
}
