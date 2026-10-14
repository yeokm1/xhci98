/*
 * hlpr_entry.c - xhcihlpr.sys, the x86 helper (design record 18, section 15
 * for this revision; roadmap-hcd.md tasks 36.15, 36.16 and 38.3).
 *
 * The one x86 xhci98.sys also loads on Windows 98 and ME, so it may import
 * no routine those kernels lack: not IoConnectInterruptEx and
 * IoDisconnectInterruptEx, which Vista introduced, and not
 * ExSetTimerResolution or MmGetSystemRoutineAddress, which Windows 98's
 * kernel does not export. This driver, installed by the Windows 2000, XP,
 * Vista and 7 x86 install paths of xhci98.inf alone, finds them and hands
 * their addresses to xhci98.sys through one internal device control on a
 * named device object (xhci_hlpr.h, channel version 2):
 *
 *   - the timer service, Windows 2000 and later: ExSetTimerResolution,
 *     imported statically - the value the loader wrote into this image's
 *     import address table from the running kernel's exports;
 *   - the MSI service, Vista and 7 only: the IoConnectInterruptEx pair,
 *     asked of MmGetSystemRoutineAddress (the documented way to find a
 *     routine that may be absent), so the image still loads on 2000 and XP.
 *
 * Nothing the HCD later calls runs in this image, and it calls none of the
 * routines it hands out.
 *
 * It creates one device object, \Device\Xhci98Helper, secured to the system
 * alone (IoCreateDeviceSecure, SDDL_DEVOBJ_SYS_ALL), answers a kernel-mode
 * create with no trailing name, a close and a cleanup, and the query; it
 * never pends an IRP, allocates no pool, touches no hardware and starts
 * nothing. It has no Unload routine, so it cannot be unloaded.
 *
 * Built with WDK 7.1 for x86, Windows XP target (setenv x86 WXP) at
 * subsystem 5.00, by scripts\build-driver.cmd; the qemu flavour alone reads
 * XhciHlprInject from its service key's Parameters (record 18 sections 2.1
 * and 15.12).
 *
 * IRQL: DriverEntry and every dispatch routine at PASSIVE_LEVEL.
 */

#include <ntddk.h>
#include <wdmsec.h>
#include "../xhci_hlpr.h"
#include "../xhci_version.h"

#if defined(XHCI_FLAVOUR_QEMU)
#define XHCIHLPR_FLAVOUR_NAME "XHCI98_FLAVOUR_QEMU"
#elif defined(XHCI_FLAVOUR_DEBUG)
#define XHCIHLPR_FLAVOUR_NAME "XHCI98_FLAVOUR_DEBUG"
#elif defined(XHCI_FLAVOUR_RELEASE)
#define XHCIHLPR_FLAVOUR_NAME "XHCI98_FLAVOUR_RELEASE"
#else
#error "no build flavour was defined. src/hlpr/sources derives XHCI_FLAVOUR_RELEASE, _DEBUG or _QEMU from BUILD_ALT_DIR; build through scripts\build-driver.cmd."
#endif

static volatile const char XhcihlprFlavourMarker[] = XHCIHLPR_FLAVOUR_NAME;
static volatile const char XhcihlprVersionString[] = "xhcihlpr " XHCI_VER_STR;

/* The device's class for IoCreateDeviceSecure, this project's own, new with
 * the device's name (record 18 section 15.2):
 * {3C8E5F21-9B47-4D02-A61C-7E2B90D4C318}. */
static const GUID XhcihlprClassGuid = {
    0x3C8E5F21, 0x9B47, 0x4D02,
    { 0xA6, 0x1C, 0x7E, 0x2B, 0x90, 0xD4, 0xC3, 0x18 }
};

/* Record 18 sections 2.1 and 15.12's injection, the qemu flavour's only; 0
 * in every other flavour. */
static ULONG XhcihlprInject;

/* The services, decided once at DriverEntry and answered the same way to
 * every query (record 18 section 15.2). */
static PVOID XhcihlprConnectEx;
static PVOID XhcihlprDisconnectEx;
static PVOID XhcihlprSetTimer;

DRIVER_INITIALIZE DriverEntry;
__drv_dispatchType(IRP_MJ_CREATE) DRIVER_DISPATCH xhcihlprCreate;
__drv_dispatchType(IRP_MJ_CLOSE) DRIVER_DISPATCH xhcihlprClose;
__drv_dispatchType(IRP_MJ_INTERNAL_DEVICE_CONTROL) DRIVER_DISPATCH
    xhcihlprInternal;

static NTSTATUS xhcihlprComplete(PIRP Irp, NTSTATUS status,
                                ULONG_PTR information)
{
    Irp->IoStatus.Status = status;
    Irp->IoStatus.Information = information;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return status;
}

/* A kernel-mode open of the device itself, nothing else: the security
 * descriptor is the first line, this the second (record 18 section 2). */
NTSTATUS xhcihlprCreate(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PIO_STACK_LOCATION stack;

    UNREFERENCED_PARAMETER(DeviceObject);
    stack = IoGetCurrentIrpStackLocation(Irp);
    if (Irp->RequestorMode != KernelMode) {
        return xhcihlprComplete(Irp, STATUS_ACCESS_DENIED, 0);
    }
    if (stack->FileObject != NULL && stack->FileObject->FileName.Length != 0) {
        return xhcihlprComplete(Irp, STATUS_OBJECT_NAME_INVALID, 0);
    }
    return xhcihlprComplete(Irp, STATUS_SUCCESS, 0);
}

NTSTATUS xhcihlprClose(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    return xhcihlprComplete(Irp, STATUS_SUCCESS, 0);
}

/* The query (record 18 sections 3.2 and 15.3), completed at once: the
 * validation and the fill are xhci_hlpr.c's XhciHlprAnswer, which reads
 * nothing of the block until its length is known to cover it. */
NTSTATUS xhcihlprInternal(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PIO_STACK_LOCATION stack;
    ULONG answer;
    ULONG information;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(DeviceObject);
    stack = IoGetCurrentIrpStackLocation(Irp);
    if (stack->Parameters.DeviceIoControl.IoControlCode !=
            XHCI_HLPR_IOCTL_QUERY_V2 ||
        XhcihlprInject == 2) {
        return xhcihlprComplete(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    }
    information = 0;
    answer = XhciHlprAnswer(
        stack->Parameters.DeviceIoControl.Type3InputBuffer,
        stack->Parameters.DeviceIoControl.InputBufferLength,
        stack->Parameters.DeviceIoControl.OutputBufferLength,
        XhcihlprConnectEx, XhcihlprDisconnectEx, XhcihlprSetTimer,
        XhciHlprPackVersion(XHCI_VER_STR),
        (XhcihlprInject >= XHCI_HLPR_INJ_MAJOR) ? XhcihlprInject
                                                : XHCI_HLPR_INJ_NONE,
        &information);
    switch (answer) {
    case XHCI_HLPR_ANS_OK:
        status = STATUS_SUCCESS;
        break;
    case XHCI_HLPR_ANS_TOO_SMALL:
        status = STATUS_BUFFER_TOO_SMALL;
        break;
    case XHCI_HLPR_ANS_REVISION:
        status = STATUS_REVISION_MISMATCH;
        break;
    default:
        status = STATUS_INVALID_PARAMETER;
        break;
    }
    return xhcihlprComplete(Irp, status, information);
}

#if defined(XHCI_FLAVOUR_QEMU)
/* A REG_DWORD of exactly four bytes, or a refusal: the value's type is
 * checked before anything is copied. */
static NTSTATUS NTAPI xhcihlprTakeDword(PWSTR ValueName, ULONG ValueType,
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

/* XhciHlprInject from the service key's Parameters subkey; 0 when absent,
 * of the wrong type, or outside 1 to XHCI_HLPR_INJ_LAST. */
static VOID xhcihlprReadInject(PUNICODE_STRING RegistryPath)
{
    RTL_QUERY_REGISTRY_TABLE table[3];
    WCHAR path[256];
    ULONG value;
    ULONG i;
    ULONG n;

    XhcihlprInject = 0;
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
    table[1].QueryRoutine = xhcihlprTakeDword;
    table[1].Flags = RTL_QUERY_REGISTRY_REQUIRED | RTL_QUERY_REGISTRY_NOEXPAND;
    table[1].Name = L"XhciHlprInject";
    table[1].EntryContext = &value;
    if (NT_SUCCESS(RtlQueryRegistryValues(RTL_REGISTRY_ABSOLUTE, path, table,
                                          NULL, NULL)) &&
        value >= 1 && value <= XHCI_HLPR_INJ_LAST) {
        XhcihlprInject = value;
    }
}
#endif

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    UNICODE_STRING name;
    PDEVICE_OBJECT device;
    PVOID connect;
    PVOID disconnect;
    NTSTATUS status;

    /* Read once, so the linker keeps both strings in the image. */
    if (XhcihlprFlavourMarker[0] == 0 || XhcihlprVersionString[0] == 0) {
        return STATUS_UNSUCCESSFUL;
    }
    /* A minimum-version test (record 18 section 15.2, step 1): Windows
     * 2000 and later. The INF installs it on no older system anyway. */
    if (!IoIsWdmVersionAvailable(1, 0x10)) {
        return STATUS_NOT_SUPPORTED;
    }
#if defined(XHCI_FLAVOUR_QEMU)
    xhcihlprReadInject(RegistryPath);
    if (XhcihlprInject == 1) {
        return STATUS_UNSUCCESSFUL;
    }
#else
    UNREFERENCED_PARAMETER(RegistryPath);
#endif

    /* Step 2, the MSI service, Vista and 7 only: the pair kept only when
     * both are found and differ. Below NT 6 the call is not made. */
    if (IoIsWdmVersionAvailable(6, 0)) {
        RtlInitUnicodeString(&name, L"IoConnectInterruptEx");
        connect = MmGetSystemRoutineAddress(&name);
        RtlInitUnicodeString(&name, L"IoDisconnectInterruptEx");
        disconnect = MmGetSystemRoutineAddress(&name);
        if (connect != NULL && disconnect != NULL && connect != disconnect) {
            XhcihlprConnectEx = connect;
            XhcihlprDisconnectEx = disconnect;
        }
    }
    /* Step 3, the timer service, Windows 2000 and later: the address the
     * loader resolved from the running kernel; this image never calls it. */
    XhcihlprSetTimer = (PVOID)ExSetTimerResolution;

    RtlInitUnicodeString(&name, XHCI_HLPR_DEVICE_NAME);
    status = IoCreateDeviceSecure(DriverObject, 0, &name, FILE_DEVICE_UNKNOWN,
                                  FILE_DEVICE_SECURE_OPEN, FALSE,
                                  &SDDL_DEVOBJ_SYS_ALL, &XhcihlprClassGuid,
                                  &device);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    DriverObject->MajorFunction[IRP_MJ_CREATE] = xhcihlprCreate;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = xhcihlprClose;
    DriverObject->MajorFunction[IRP_MJ_CLEANUP] = xhcihlprClose;
    DriverObject->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] =
        xhcihlprInternal;
    /* No DriverUnload: the helper stays for the life of the boot (record
     * 18 section 2). */
    device->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}
