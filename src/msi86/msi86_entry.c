/*
 * msi86_entry.c - xhcimsi.sys, the NT 6 x86 MSI helper (design record 18;
 * roadmap-hcd.md tasks 36.15 and 36.16).
 *
 * The one x86 xhci98.sys also loads on Windows 98, ME, 2000 and XP, so it
 * may import neither IoConnectInterruptEx nor IoDisconnectInterruptEx, which
 * Vista introduced. This driver, installed by the Vista and 7 x86 install
 * path of xhci98.inf alone, imports both as any NT 6 driver does and hands
 * their addresses - the values the loader wrote into this image's import
 * address table, from the running kernel's own exports - to xhci98.sys
 * through one internal device control on a named device object
 * (xhci_msi86.h). Nothing the HCD later calls runs in this image.
 *
 * It creates one device object, \Device\Xhci98MsiHelper, secured to the
 * system alone (IoCreateDeviceSecure, SDDL_DEVOBJ_SYS_ALL), answers a
 * kernel-mode create with no trailing name, a close and a cleanup, and the
 * query; it never pends an IRP, allocates no pool, touches no hardware and
 * starts nothing. It has no Unload routine, so it cannot be unloaded.
 *
 * Built with WDK 7.1 for x86, Vista target (setenv x86 WLH), by
 * scripts\build-driver.cmd; the qemu flavour alone reads XhciMsiInject from
 * its service key's Parameters (record 18 section 2.1).
 *
 * IRQL: DriverEntry and every dispatch routine at PASSIVE_LEVEL.
 */

#include <ntddk.h>
#include <wdmsec.h>
#include "../xhci_msi86.h"
#include "../xhci_version.h"

#if defined(XHCI_FLAVOUR_QEMU)
#define XHCIMSI_FLAVOUR_NAME "XHCI98_FLAVOUR_QEMU"
#elif defined(XHCI_FLAVOUR_DEBUG)
#define XHCIMSI_FLAVOUR_NAME "XHCI98_FLAVOUR_DEBUG"
#elif defined(XHCI_FLAVOUR_RELEASE)
#define XHCIMSI_FLAVOUR_NAME "XHCI98_FLAVOUR_RELEASE"
#else
#error "no build flavour was defined. src/msi86/sources derives XHCI_FLAVOUR_RELEASE, _DEBUG or _QEMU from BUILD_ALT_DIR; build through scripts\build-driver.cmd."
#endif

static volatile const char XhcimsiFlavourMarker[] = XHCIMSI_FLAVOUR_NAME;
static volatile const char XhcimsiVersionString[] = "xhcimsi " XHCI_VER_STR;

/* The device's class for IoCreateDeviceSecure, this project's own:
 * {6B0C1E52-7A41-4E9C-9D3E-58C8A2F1B6A4}. */
static const GUID XhcimsiClassGuid = {
    0x6B0C1E52, 0x7A41, 0x4E9C,
    { 0x9D, 0x3E, 0x58, 0xC8, 0xA2, 0xF1, 0xB6, 0xA4 }
};

/* Record 18 section 2.1's injection, the qemu flavour's only; 0 in every
 * other flavour. */
static ULONG XhcimsiInject;

DRIVER_INITIALIZE DriverEntry;
__drv_dispatchType(IRP_MJ_CREATE) DRIVER_DISPATCH xhcimsiCreate;
__drv_dispatchType(IRP_MJ_CLOSE) DRIVER_DISPATCH xhcimsiClose;
__drv_dispatchType(IRP_MJ_INTERNAL_DEVICE_CONTROL) DRIVER_DISPATCH
    xhcimsiInternal;

static NTSTATUS xhcimsiComplete(PIRP Irp, NTSTATUS status,
                                ULONG_PTR information)
{
    Irp->IoStatus.Status = status;
    Irp->IoStatus.Information = information;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return status;
}

/* A kernel-mode open of the device itself, nothing else: the security
 * descriptor is the first line, this the second (record 18 section 2). */
NTSTATUS xhcimsiCreate(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PIO_STACK_LOCATION stack;

    UNREFERENCED_PARAMETER(DeviceObject);
    stack = IoGetCurrentIrpStackLocation(Irp);
    if (Irp->RequestorMode != KernelMode) {
        return xhcimsiComplete(Irp, STATUS_ACCESS_DENIED, 0);
    }
    if (stack->FileObject != NULL && stack->FileObject->FileName.Length != 0) {
        return xhcimsiComplete(Irp, STATUS_OBJECT_NAME_INVALID, 0);
    }
    return xhcimsiComplete(Irp, STATUS_SUCCESS, 0);
}

NTSTATUS xhcimsiClose(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    return xhcimsiComplete(Irp, STATUS_SUCCESS, 0);
}

/* The query (record 18 section 3.2), completed at once: the validation and
 * the fill are xhci_msi86.c's XhciMsi86Answer, which reads nothing of the
 * block until its length is known to cover it. */
NTSTATUS xhcimsiInternal(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PIO_STACK_LOCATION stack;
    ULONG answer;
    ULONG information;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(DeviceObject);
    stack = IoGetCurrentIrpStackLocation(Irp);
    if (stack->Parameters.DeviceIoControl.IoControlCode !=
            XHCI_MSI86_IOCTL_QUERY_V1 ||
        XhcimsiInject == 2) {
        return xhcimsiComplete(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    }
    information = 0;
    answer = XhciMsi86Answer(
        stack->Parameters.DeviceIoControl.Type3InputBuffer,
        stack->Parameters.DeviceIoControl.InputBufferLength,
        stack->Parameters.DeviceIoControl.OutputBufferLength,
        (PVOID)IoConnectInterruptEx, (PVOID)IoDisconnectInterruptEx,
        XhciMsi86PackVersion(XHCI_VER_STR),
        (XhcimsiInject >= XHCI_MSI86_INJ_MAJOR2) ? XhcimsiInject
                                                 : XHCI_MSI86_INJ_NONE,
        &information);
    switch (answer) {
    case XHCI_MSI86_ANS_OK:
        status = STATUS_SUCCESS;
        break;
    case XHCI_MSI86_ANS_TOO_SMALL:
        status = STATUS_BUFFER_TOO_SMALL;
        break;
    case XHCI_MSI86_ANS_REVISION:
        status = STATUS_REVISION_MISMATCH;
        break;
    default:
        status = STATUS_INVALID_PARAMETER;
        break;
    }
    return xhcimsiComplete(Irp, status, information);
}

#if defined(XHCI_FLAVOUR_QEMU)
/* A REG_DWORD of exactly four bytes, or a refusal: the value's type is
 * checked before anything is copied. */
static NTSTATUS NTAPI xhcimsiTakeDword(PWSTR ValueName, ULONG ValueType,
                                       PVOID ValueData, ULONG ValueLength,
                                       PVOID Context, PVOID EntryContext)
{
    UNREFERENCED_PARAMETER(ValueName);
    UNREFERENCED_PARAMETER(Context);
    if (ValueType != REG_DWORD || ValueLength != sizeof(ULONG) ||
        ValueData == NULL) {
        return STATUS_OBJECT_TYPE_MISMATCH;
    }
    *(PULONG)EntryContext = *(PULONG)ValueData;
    return STATUS_SUCCESS;
}

/* XhciMsiInject from the service key's Parameters subkey; 0 when absent, of
 * the wrong type, or outside 1 to 6. */
static VOID xhcimsiReadInject(PUNICODE_STRING RegistryPath)
{
    RTL_QUERY_REGISTRY_TABLE table[3];
    WCHAR path[256];
    ULONG value;
    ULONG i;
    ULONG n;

    XhcimsiInject = 0;
    n = RegistryPath->Length / sizeof(WCHAR);
    if (RegistryPath->Buffer == NULL || n >= 256) {
        return;
    }
    for (i = 0; i < n; i++) {
        path[i] = RegistryPath->Buffer[i];
    }
    path[n] = 0;
    for (i = 0; i < sizeof(table); i++) {
        ((PUCHAR)table)[i] = 0;
    }
    value = 0;
    table[0].Flags = RTL_QUERY_REGISTRY_SUBKEY;
    table[0].Name = L"Parameters";
    table[1].QueryRoutine = xhcimsiTakeDword;
    table[1].Flags = RTL_QUERY_REGISTRY_REQUIRED | RTL_QUERY_REGISTRY_NOEXPAND;
    table[1].Name = L"XhciMsiInject";
    table[1].EntryContext = &value;
    if (NT_SUCCESS(RtlQueryRegistryValues(RTL_REGISTRY_ABSOLUTE, path, table,
                                          NULL, NULL)) &&
        value >= 1 && value <= 6) {
        XhcimsiInject = value;
    }
}
#endif

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    UNICODE_STRING name;
    PDEVICE_OBJECT device;
    NTSTATUS status;

    /* Read once, so the linker keeps both strings in the image. */
    if (XhcimsiFlavourMarker[0] == 0 || XhcimsiVersionString[0] == 0) {
        return STATUS_UNSUCCESSFUL;
    }
    /* A minimum-version test: refuses a pre-NT 6 kernel, which this
     * image's imports already do (record 18 section 2). */
    if (!IoIsWdmVersionAvailable(6, 0)) {
        return STATUS_NOT_SUPPORTED;
    }
#if defined(XHCI_FLAVOUR_QEMU)
    xhcimsiReadInject(RegistryPath);
    if (XhcimsiInject == 1) {
        return STATUS_UNSUCCESSFUL;
    }
#else
    UNREFERENCED_PARAMETER(RegistryPath);
#endif

    RtlInitUnicodeString(&name, XHCI_MSI86_DEVICE_NAME);
    status = IoCreateDeviceSecure(DriverObject, 0, &name, FILE_DEVICE_UNKNOWN,
                                  FILE_DEVICE_SECURE_OPEN, FALSE,
                                  &SDDL_DEVOBJ_SYS_ALL, &XhcimsiClassGuid,
                                  &device);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    DriverObject->MajorFunction[IRP_MJ_CREATE] = xhcimsiCreate;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = xhcimsiClose;
    DriverObject->MajorFunction[IRP_MJ_CLEANUP] = xhcimsiClose;
    DriverObject->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] =
        xhcimsiInternal;
    /* No DriverUnload: the helper stays for the life of the boot (record
     * 18 section 2). */
    device->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}
