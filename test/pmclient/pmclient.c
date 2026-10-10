/*
 * pmclient.c - XHCIPM.SYS, selective suspend's test client (roadmap-hcd.md
 * task 36.34; design record 14 section 10, SUSPa.6). TEST ONLY, NEVER
 * SHIPPED: scripts\package\make-release.ps1 refuses a release carrying it.
 *
 * A WDM function driver for one USB device on a VM leg (pmclient.inf binds
 * it by hardware id), which is its stack's power policy owner and does, on
 * PMCTL.EXE's demand, what a class driver that idles in S0 would: request
 * D1/D2/D3 and D0 (PoRequestPowerIrp), request and cancel IRP_MN_WAIT_WAKE,
 * submit and cancel IOCTL_INTERNAL_USB_SUBMIT_IDLE_NOTIFICATION with a
 * callback that requests the wake state and waits for it, and keep an
 * interrupt IN read posted, so a leg reads each step separately (record 14
 * section 10: the arm, U3 held, the wake, WAIT_WAKE's completion, D0,
 * transfers resumed). No stock class driver on 98 SE or 2000 idles in S0,
 * so without it the primaries would read only "no regression".
 *
 * Built with the Windows 2000 DDK for x86 (build.cmd); it loads on 98 SE,
 * ME, 2000 and XP. It imports nothing usbd.sys exports: its URBs are built
 * by hand. Every count is in PM_STATUS (pmioctl.h), read by PMCTL STATUS.
 *
 * IRQL: as each routine says; the counts under the extension's spin lock.
 */

#include <wdm.h>
#include <usbdi.h>
#include <usbioctl.h>
#include "pmioctl.h"

/* XP's idle notification (WDK 7.1 usbioctl.h); the Windows 2000 DDK
 * predates it. */
#ifndef IOCTL_INTERNAL_USB_SUBMIT_IDLE_NOTIFICATION
#define IOCTL_INTERNAL_USB_SUBMIT_IDLE_NOTIFICATION 0x00220027UL
#endif

typedef VOID (NTAPI *PM_IDLE_CALLBACK)(PVOID Context);

typedef struct _PM_IDLE_INFO {
    PM_IDLE_CALLBACK IdleCallback;
    PVOID IdleContext;
} PM_IDLE_INFO;

/* The marker the packaging gate looks for, kept in the image. */
static const char pmMarker[] = PM_TEST_MARKER;

typedef struct _PM_EXT {
    PDEVICE_OBJECT Self;
    PDEVICE_OBJECT Lower;
    PDEVICE_OBJECT Pdo;
    ULONG Index;
    UNICODE_STRING Link;
    WCHAR LinkBuf[32];
    ULONG LinkMade;
    KSPIN_LOCK Lock;
    PM_STATUS St;
    /* WAIT_WAKE as it passes down through this FDO: held here so a cancel
     * can reach it; WakeRef 1 for the IRP, +1 while a canceller holds it
     * (the completion routine keeps the IRP while a canceller does). */
    PIRP WakeIrp;
    volatile LONG WakeRef;
    /* The idle IRP, this driver's own: IdleRef 1 for its completion, +1
     * while a canceller holds it; the last to let go frees it. */
    PIRP IdleIrp;
    volatile LONG IdleRef;
    ULONG IdleFlags;
    PM_IDLE_INFO IdleInfo;
    KEVENT IdleGone;
    /* The read loop: one IRP and URB at a time, the same reference rule. */
    USBD_PIPE_HANDLE InPipe;
    ULONG InMax;
    PIRP ReadIrp;
    volatile LONG ReadRef;
    PURB ReadUrb;
    UCHAR ReadBuf[64];
    KEVENT ReadGone;
    /* The idle callback's D-IRP, waited for in the callback. */
    KEVENT IdlePowerDone;
    UCHAR Config[512];
    ULONG Removed;
} PM_EXT, *PPM_EXT;

static ULONG pmNextIndex;

static NTSTATUS NTAPI pmAddDevice(PDRIVER_OBJECT driver, PDEVICE_OBJECT pdo);
static VOID NTAPI pmUnload(PDRIVER_OBJECT driver);
static NTSTATUS NTAPI pmCreateClose(PDEVICE_OBJECT dev, PIRP irp);
static NTSTATUS NTAPI pmIoctl(PDEVICE_OBJECT dev, PIRP irp);
static NTSTATUS NTAPI pmPnp(PDEVICE_OBJECT dev, PIRP irp);
static NTSTATUS NTAPI pmPower(PDEVICE_OBJECT dev, PIRP irp);
static NTSTATUS NTAPI pmPass(PDEVICE_OBJECT dev, PIRP irp);
static VOID pmReadStart(PPM_EXT ext);

NTSTATUS NTAPI DriverEntry(PDRIVER_OBJECT driver, PUNICODE_STRING path)
{
    UNREFERENCED_PARAMETER(path);
    driver->MajorFunction[IRP_MJ_CREATE] = pmCreateClose;
    driver->MajorFunction[IRP_MJ_CLOSE] = pmCreateClose;
    driver->MajorFunction[IRP_MJ_DEVICE_CONTROL] = pmIoctl;
    driver->MajorFunction[IRP_MJ_PNP] = pmPnp;
    driver->MajorFunction[IRP_MJ_POWER] = pmPower;
    driver->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = pmPass;
    driver->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = pmPass;
    driver->DriverExtension->AddDevice = pmAddDevice;
    driver->DriverUnload = pmUnload;
    DbgPrint("XHCIPM: %s loaded\n", pmMarker);
    return STATUS_SUCCESS;
}

static VOID NTAPI pmUnload(PDRIVER_OBJECT driver)
{
    UNREFERENCED_PARAMETER(driver);
}

static VOID pmName(PWCHAR out, const char *prefix, ULONG index)
{
    ULONG i;

    for (i = 0; prefix[i] != 0; i++) {
        out[i] = (WCHAR)prefix[i];
    }
    out[i++] = (WCHAR)(L'0' + index);
    out[i] = 0;
}

static NTSTATUS NTAPI pmAddDevice(PDRIVER_OBJECT driver, PDEVICE_OBJECT pdo)
{
    PDEVICE_OBJECT dev;
    PPM_EXT ext;
    UNICODE_STRING name;
    WCHAR nameBuf[32];
    NTSTATUS status;
    ULONG tries;
    ULONG index;

    dev = NULL;
    status = STATUS_UNSUCCESSFUL;
    index = 0;
    for (tries = 0; tries < PM_MAX_DEVICES; tries++) {
        index = (pmNextIndex + tries) % PM_MAX_DEVICES;
        pmName(nameBuf, "\\Device\\" PM_LINK_PREFIX, index);
        RtlInitUnicodeString(&name, nameBuf);
        status = IoCreateDevice(driver, sizeof(PM_EXT), &name,
                                FILE_DEVICE_UNKNOWN, 0, FALSE, &dev);
        if (NT_SUCCESS(status)) {
            break;
        }
    }
    if (!NT_SUCCESS(status)) {
        return status;
    }
    pmNextIndex = index + 1;
    ext = (PPM_EXT)dev->DeviceExtension;
    ext->Self = dev;
    ext->Pdo = pdo;
    ext->Index = index;
    KeInitializeSpinLock(&ext->Lock);
    KeInitializeEvent(&ext->IdleGone, NotificationEvent, TRUE);
    KeInitializeEvent(&ext->ReadGone, NotificationEvent, TRUE);
    KeInitializeEvent(&ext->IdlePowerDone, NotificationEvent, FALSE);
    ext->St.Size = sizeof(PM_STATUS);
    ext->St.Version = 1;
    ext->St.DevicePower = PowerDeviceD0;
    ext->St.SystemPower = PowerSystemWorking;
    ext->Lower = IoAttachDeviceToDeviceStack(dev, pdo);
    if (ext->Lower == NULL) {
        IoDeleteDevice(dev);
        return STATUS_NO_SUCH_DEVICE;
    }
    pmName(ext->LinkBuf, "\\DosDevices\\" PM_LINK_PREFIX, index);
    RtlInitUnicodeString(&ext->Link, ext->LinkBuf);
    if (NT_SUCCESS(IoCreateSymbolicLink(&ext->Link, &name))) {
        ext->LinkMade = 1;
    }
    dev->Flags |= DO_BUFFERED_IO | DO_POWER_PAGABLE;
    dev->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

static NTSTATUS NTAPI pmCreateClose(PDEVICE_OBJECT dev, PIRP irp)
{
    UNREFERENCED_PARAMETER(dev);
    irp->IoStatus.Status = STATUS_SUCCESS;
    irp->IoStatus.Information = 0;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static NTSTATUS NTAPI pmPass(PDEVICE_OBJECT dev, PIRP irp)
{
    PPM_EXT ext;

    ext = (PPM_EXT)dev->DeviceExtension;
    IoSkipCurrentIrpStackLocation(irp);
    return IoCallDriver(ext->Lower, irp);
}

/* ------------------------------------------------------------------ */
/* Synchronous URBs at PASSIVE_LEVEL                                   */
/* ------------------------------------------------------------------ */

static NTSTATUS NTAPI pmSyncDone(PDEVICE_OBJECT dev, PIRP irp, PVOID ctx)
{
    UNREFERENCED_PARAMETER(dev);
    UNREFERENCED_PARAMETER(irp);
    KeSetEvent((PKEVENT)ctx, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static NTSTATUS pmUrbSync(PPM_EXT ext, PURB urb)
{
    PIRP irp;
    PIO_STACK_LOCATION next;
    KEVENT done;
    NTSTATUS status;

    irp = IoAllocateIrp(ext->Lower->StackSize, FALSE);
    if (irp == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    KeInitializeEvent(&done, NotificationEvent, FALSE);
    next = IoGetNextIrpStackLocation(irp);
    next->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
    next->Parameters.DeviceIoControl.IoControlCode =
        IOCTL_INTERNAL_USB_SUBMIT_URB;
    next->Parameters.Others.Argument1 = urb;
    irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    IoSetCompletionRoutine(irp, pmSyncDone, &done, TRUE, TRUE, TRUE);
    (VOID)IoCallDriver(ext->Lower, irp);
    (VOID)KeWaitForSingleObject(&done, Executive, KernelMode, FALSE, NULL);
    status = irp->IoStatus.Status;
    IoFreeIrp(irp);
    return status;
}

static VOID pmZero(PVOID p, ULONG n)
{
    PUCHAR b;

    for (b = (PUCHAR)p; n != 0; n--) {
        *b++ = 0;
    }
}

/* Configuration 1, interface 0 alternate 0 selected, its interrupt IN
 * pipe kept for the read loop. IRQL: PASSIVE_LEVEL. */
static VOID pmConfigure(PPM_EXT ext)
{
    struct _URB_CONTROL_DESCRIPTOR_REQUEST gd;
    PURB sel;
    PUSB_CONFIGURATION_DESCRIPTOR cd;
    PUSB_INTERFACE_DESCRIPTOR id;
    PUSBD_INTERFACE_INFORMATION ii;
    PUCHAR p;
    ULONG total;
    ULONG pipes;
    ULONG size;
    ULONG i;
    NTSTATUS status;

    pmZero(&gd, sizeof(gd));
    gd.Hdr.Length = sizeof(gd);
    gd.Hdr.Function = URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE;
    gd.TransferBuffer = ext->Config;
    gd.TransferBufferLength = sizeof(ext->Config);
    gd.DescriptorType = USB_CONFIGURATION_DESCRIPTOR_TYPE;
    gd.Index = 0;
    status = pmUrbSync(ext, (PURB)&gd);
    ext->St.ConfigStatus = status;
    if (!NT_SUCCESS(status) || gd.TransferBufferLength < 9) {
        return;
    }
    cd = (PUSB_CONFIGURATION_DESCRIPTOR)ext->Config;
    ext->St.ConfigAttributes = cd->bmAttributes;
    total = cd->wTotalLength;
    if (total > gd.TransferBufferLength) {
        total = gd.TransferBufferLength;
    }
    id = NULL;
    for (p = ext->Config + cd->bLength; p + 2 <= ext->Config + total &&
                                        p[0] != 0;
         p += p[0]) {
        if (p[1] == USB_INTERFACE_DESCRIPTOR_TYPE) {
            id = (PUSB_INTERFACE_DESCRIPTOR)p;
            break;
        }
    }
    if (id == NULL) {
        return;
    }
    pipes = id->bNumEndpoints;
    size = sizeof(struct _URB_SELECT_CONFIGURATION) +
           (pipes != 0 ? pipes - 1 : 0) * sizeof(USBD_PIPE_INFORMATION);
    sel = (PURB)ExAllocatePool(NonPagedPool, size);
    if (sel == NULL) {
        return;
    }
    pmZero(sel, size);
    sel->UrbHeader.Length = (USHORT)size;
    sel->UrbHeader.Function = URB_FUNCTION_SELECT_CONFIGURATION;
    sel->UrbSelectConfiguration.ConfigurationDescriptor = cd;
    ii = &sel->UrbSelectConfiguration.Interface;
    ii->Length = (USHORT)(sizeof(USBD_INTERFACE_INFORMATION) +
                          (pipes != 0 ? pipes - 1 : 0) *
                              sizeof(USBD_PIPE_INFORMATION));
    ii->InterfaceNumber = id->bInterfaceNumber;
    ii->AlternateSetting = id->bAlternateSetting;
    ii->NumberOfPipes = pipes;
    for (i = 0; i < pipes; i++) {
        ii->Pipes[i].MaximumTransferSize = 4096;
    }
    status = pmUrbSync(ext, sel);
    ext->St.ConfigStatus = status;
    if (NT_SUCCESS(status)) {
        ext->St.Configured = 1;
        for (i = 0; i < ii->NumberOfPipes; i++) {
            if (ii->Pipes[i].PipeType == UsbdPipeTypeInterrupt &&
                (ii->Pipes[i].EndpointAddress & 0x80) != 0) {
                ext->InPipe = ii->Pipes[i].PipeHandle;
                ext->InMax = ii->Pipes[i].MaximumPacketSize;
                ext->St.InterruptIn = ii->Pipes[i].EndpointAddress;
                break;
            }
        }
    }
    ExFreePool(sel);
}

/* ------------------------------------------------------------------ */
/* Power requests                                                      */
/* ------------------------------------------------------------------ */

static VOID NTAPI pmPowerDone(PDEVICE_OBJECT dev, UCHAR minor,
                              POWER_STATE state, PVOID ctx,
                              PIO_STATUS_BLOCK io)
{
    PPM_EXT ext;
    KIRQL irql;

    UNREFERENCED_PARAMETER(dev);
    UNREFERENCED_PARAMETER(minor);
    ext = (PPM_EXT)ctx;
    KeAcquireSpinLock(&ext->Lock, &irql);
    ext->St.PowerCallbacks++;
    ext->St.PowerLastStatus = io->Status;
    ext->St.PowerLastState = state.DeviceState;
    KeReleaseSpinLock(&ext->Lock, irql);
}

static NTSTATUS pmRequestPower(PPM_EXT ext, DEVICE_POWER_STATE d,
                               PREQUEST_POWER_COMPLETE done, PVOID ctx)
{
    POWER_STATE state;
    NTSTATUS status;
    KIRQL irql;

    state.DeviceState = d;
    KeAcquireSpinLock(&ext->Lock, &irql);
    ext->St.PowerRequests++;
    KeReleaseSpinLock(&ext->Lock, irql);
    status = PoRequestPowerIrp(ext->Pdo, IRP_MN_SET_POWER, state,
                               done != NULL ? done : pmPowerDone,
                               done != NULL ? ctx : ext, NULL);
    if (status != STATUS_PENDING && !NT_SUCCESS(status)) {
        KeAcquireSpinLock(&ext->Lock, &irql);
        ext->St.PowerRefused++;
        ext->St.PowerLastStatus = status;
        KeReleaseSpinLock(&ext->Lock, irql);
    }
    return status;
}

/* The requester's WAIT_WAKE callback (5.5's 9x defect is this never being
 * called): counted, and a wake that completed SUCCESS brings the device
 * back to D0, as a class driver does. */
static VOID NTAPI pmWakeDone(PDEVICE_OBJECT dev, UCHAR minor,
                             POWER_STATE state, PVOID ctx,
                             PIO_STATUS_BLOCK io)
{
    PPM_EXT ext;
    KIRQL irql;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(dev);
    UNREFERENCED_PARAMETER(minor);
    UNREFERENCED_PARAMETER(state);
    ext = (PPM_EXT)ctx;
    status = io->Status;
    KeAcquireSpinLock(&ext->Lock, &irql);
    ext->St.WakeCallbacks++;
    ext->St.WakeLastStatus = status;
    KeReleaseSpinLock(&ext->Lock, irql);
    if (NT_SUCCESS(status) && !ext->Removed) {
        (VOID)pmRequestPower(ext, PowerDeviceD0, NULL, NULL);
    }
}

static NTSTATUS pmRequestWake(PPM_EXT ext)
{
    POWER_STATE state;
    NTSTATUS status;
    KIRQL irql;

    state.SystemState = PowerSystemWorking;
    KeAcquireSpinLock(&ext->Lock, &irql);
    ext->St.WakeRequests++;
    KeReleaseSpinLock(&ext->Lock, irql);
    status = PoRequestPowerIrp(ext->Pdo, IRP_MN_WAIT_WAKE, state, pmWakeDone,
                               ext, NULL);
    if (status != STATUS_PENDING && !NT_SUCCESS(status)) {
        KeAcquireSpinLock(&ext->Lock, &irql);
        ext->St.WakeLastStatus = status;
        KeReleaseSpinLock(&ext->Lock, irql);
    }
    return status;
}

/* The WAIT_WAKE passing back up through this FDO: counted; kept while a
 * canceller holds it (pmWakeCancel completes it again then). */
static NTSTATUS NTAPI pmWakeIrpDone(PDEVICE_OBJECT dev, PIRP irp, PVOID ctx)
{
    PPM_EXT ext;
    KIRQL irql;

    UNREFERENCED_PARAMETER(ctx);
    ext = (PPM_EXT)dev->DeviceExtension;
    if (irp->PendingReturned) {
        IoMarkIrpPending(irp);
    }
    KeAcquireSpinLock(&ext->Lock, &irql);
    ext->St.WakeIrpCompletions++;
    ext->St.WakeIrpLastStatus = irp->IoStatus.Status;
    ext->St.WakeHeld = 0;
    ext->WakeIrp = NULL;
    KeReleaseSpinLock(&ext->Lock, irql);
    if (InterlockedDecrement((PLONG)&ext->WakeRef) != 0) {
        return STATUS_MORE_PROCESSING_REQUIRED;
    }
    return STATUS_SUCCESS;
}

static VOID pmWakeCancel(PPM_EXT ext)
{
    PIRP irp;
    KIRQL irql;

    KeAcquireSpinLock(&ext->Lock, &irql);
    irp = ext->WakeIrp;
    if (irp != NULL) {
        InterlockedIncrement((PLONG)&ext->WakeRef);
    }
    KeReleaseSpinLock(&ext->Lock, irql);
    if (irp == NULL) {
        return;
    }
    (VOID)IoCancelIrp(irp);
    if (InterlockedDecrement((PLONG)&ext->WakeRef) == 0) {
        IoCompleteRequest(irp, IO_NO_INCREMENT);
    }
}

/* ------------------------------------------------------------------ */
/* The idle notification                                               */
/* ------------------------------------------------------------------ */

static VOID NTAPI pmIdlePowerDone(PDEVICE_OBJECT dev, UCHAR minor,
                                  POWER_STATE state, PVOID ctx,
                                  PIO_STATUS_BLOCK io)
{
    PPM_EXT ext;
    KIRQL irql;

    UNREFERENCED_PARAMETER(dev);
    UNREFERENCED_PARAMETER(minor);
    ext = (PPM_EXT)ctx;
    KeAcquireSpinLock(&ext->Lock, &irql);
    ext->St.PowerCallbacks++;
    ext->St.PowerLastStatus = io->Status;
    ext->St.PowerLastState = state.DeviceState;
    ext->St.IdleCallbackPowerStatus = io->Status;
    KeReleaseSpinLock(&ext->Lock, irql);
    KeSetEvent(&ext->IdlePowerDone, IO_NO_INCREMENT, FALSE);
}

/* The bus's idle callback, at PASSIVE_LEVEL on its thread: what XP's
 * class drivers do - optionally arm wake, then request the wake state and
 * wait for it (WDK selSusp's IdleNotificationCallback). */
static VOID NTAPI pmIdleCallback(PVOID context)
{
    PPM_EXT ext;
    DEVICE_POWER_STATE d;
    KIRQL irql;
    ULONG flags;

    ext = (PPM_EXT)context;
    flags = ext->IdleFlags;
    d = (flags & PM_IDLE_D3) ? PowerDeviceD3 : PowerDeviceD2;
    KeAcquireSpinLock(&ext->Lock, &irql);
    ext->St.IdleCallbacks++;
    ext->St.IdleCallbackPower = (flags & PM_IDLE_NO_POWER) ? 0 : d;
    KeReleaseSpinLock(&ext->Lock, irql);
    if (flags & PM_IDLE_NO_POWER) {
        return;
    }
    if ((flags & PM_IDLE_ARM_WAKE) && !ext->St.WakeHeld) {
        (VOID)pmRequestWake(ext);
    }
    KeClearEvent(&ext->IdlePowerDone);
    if (pmRequestPower(ext, d, pmIdlePowerDone, ext) == STATUS_PENDING) {
        (VOID)KeWaitForSingleObject(&ext->IdlePowerDone, Executive,
                                    KernelMode, FALSE, NULL);
    }
}

static NTSTATUS NTAPI pmIdleDone(PDEVICE_OBJECT dev, PIRP irp, PVOID ctx)
{
    PPM_EXT ext;
    KIRQL irql;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(dev);
    ext = (PPM_EXT)ctx;
    status = irp->IoStatus.Status;
    KeAcquireSpinLock(&ext->Lock, &irql);
    ext->St.IdleCompletions++;
    ext->St.IdleLastStatus = status;
    ext->St.IdleHeld = 0;
    ext->IdleIrp = NULL;
    KeReleaseSpinLock(&ext->Lock, irql);
    if (NT_SUCCESS(status) && (ext->IdleFlags & PM_IDLE_AUTO_D0) &&
        !ext->Removed) {
        (VOID)pmRequestPower(ext, PowerDeviceD0, NULL, NULL);
    }
    if (InterlockedDecrement((PLONG)&ext->IdleRef) == 0) {
        IoFreeIrp(irp);
        KeSetEvent(&ext->IdleGone, IO_NO_INCREMENT, FALSE);
    }
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static NTSTATUS pmIdleSubmit(PPM_EXT ext, ULONG flags)
{
    PIRP irp;
    PIO_STACK_LOCATION next;
    KIRQL irql;

    KeAcquireSpinLock(&ext->Lock, &irql);
    if (ext->IdleIrp != NULL) {
        KeReleaseSpinLock(&ext->Lock, irql);
        return STATUS_DEVICE_BUSY;
    }
    KeReleaseSpinLock(&ext->Lock, irql);
    irp = IoAllocateIrp(ext->Lower->StackSize, FALSE);
    if (irp == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    ext->IdleFlags = flags;
    ext->IdleInfo.IdleCallback = pmIdleCallback;
    ext->IdleInfo.IdleContext = ext;
    next = IoGetNextIrpStackLocation(irp);
    next->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
    next->Parameters.DeviceIoControl.IoControlCode =
        IOCTL_INTERNAL_USB_SUBMIT_IDLE_NOTIFICATION;
    next->Parameters.DeviceIoControl.InputBufferLength =
        sizeof(PM_IDLE_INFO);
    next->Parameters.DeviceIoControl.Type3InputBuffer = &ext->IdleInfo;
    next->Parameters.Others.Argument1 = &ext->IdleInfo;
    irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    IoSetCompletionRoutine(irp, pmIdleDone, ext, TRUE, TRUE, TRUE);
    KeClearEvent(&ext->IdleGone);
    KeAcquireSpinLock(&ext->Lock, &irql);
    ext->IdleRef = 1;
    ext->IdleIrp = irp;
    ext->St.IdleSubmits++;
    ext->St.IdleHeld = 1;
    KeReleaseSpinLock(&ext->Lock, irql);
    (VOID)IoCallDriver(ext->Lower, irp);
    return STATUS_SUCCESS;
}

static VOID pmIdleCancel(PPM_EXT ext)
{
    PIRP irp;
    KIRQL irql;

    KeAcquireSpinLock(&ext->Lock, &irql);
    irp = ext->IdleIrp;
    if (irp != NULL) {
        InterlockedIncrement((PLONG)&ext->IdleRef);
    }
    KeReleaseSpinLock(&ext->Lock, irql);
    if (irp == NULL) {
        return;
    }
    (VOID)IoCancelIrp(irp);
    if (InterlockedDecrement((PLONG)&ext->IdleRef) == 0) {
        IoFreeIrp(irp);
        KeSetEvent(&ext->IdleGone, IO_NO_INCREMENT, FALSE);
    }
}

/* ------------------------------------------------------------------ */
/* The read loop                                                       */
/* ------------------------------------------------------------------ */

static NTSTATUS NTAPI pmReadDone(PDEVICE_OBJECT dev, PIRP irp, PVOID ctx)
{
    PPM_EXT ext;
    KIRQL irql;
    ULONG again;

    UNREFERENCED_PARAMETER(dev);
    ext = (PPM_EXT)ctx;
    KeAcquireSpinLock(&ext->Lock, &irql);
    if (NT_SUCCESS(irp->IoStatus.Status)) {
        ext->St.ReadsCompleted++;
    } else {
        ext->St.ReadsFailed++;
    }
    ext->St.ReadLastStatus = irp->IoStatus.Status;
    ext->St.ReadLastUsbd = (ULONG)ext->ReadUrb->UrbHeader.Status;
    ext->ReadIrp = NULL;
    again = ext->St.ReadsOn && NT_SUCCESS(irp->IoStatus.Status) &&
            !ext->Removed;
    KeReleaseSpinLock(&ext->Lock, irql);
    if (InterlockedDecrement((PLONG)&ext->ReadRef) == 0) {
        IoFreeIrp(irp);
        if (again) {
            pmReadStart(ext);
        } else {
            KeSetEvent(&ext->ReadGone, IO_NO_INCREMENT, FALSE);
        }
    }
    return STATUS_MORE_PROCESSING_REQUIRED;
}

/* One interrupt IN read posted. IRQL: <= DISPATCH_LEVEL. */
static VOID pmReadStart(PPM_EXT ext)
{
    PIRP irp;
    PIO_STACK_LOCATION next;
    struct _URB_BULK_OR_INTERRUPT_TRANSFER *u;
    KIRQL irql;

    irp = (ext->InPipe != NULL)
              ? IoAllocateIrp(ext->Lower->StackSize, FALSE)
              : NULL;
    if (irp == NULL) {
        ext->St.ReadsOn = 0;
        KeSetEvent(&ext->ReadGone, IO_NO_INCREMENT, FALSE);
        return;
    }
    u = &ext->ReadUrb->UrbBulkOrInterruptTransfer;
    pmZero(u, sizeof(*u));
    u->Hdr.Length = sizeof(*u);
    u->Hdr.Function = URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER;
    u->PipeHandle = ext->InPipe;
    u->TransferFlags = USBD_TRANSFER_DIRECTION_IN | USBD_SHORT_TRANSFER_OK;
    u->TransferBuffer = ext->ReadBuf;
    u->TransferBufferLength = (ext->InMax != 0 && ext->InMax <= 64)
                                  ? ext->InMax
                                  : 64;
    next = IoGetNextIrpStackLocation(irp);
    next->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
    next->Parameters.DeviceIoControl.IoControlCode =
        IOCTL_INTERNAL_USB_SUBMIT_URB;
    next->Parameters.Others.Argument1 = ext->ReadUrb;
    IoSetCompletionRoutine(irp, pmReadDone, ext, TRUE, TRUE, TRUE);
    KeAcquireSpinLock(&ext->Lock, &irql);
    ext->ReadRef = 1;
    ext->ReadIrp = irp;
    KeReleaseSpinLock(&ext->Lock, irql);
    (VOID)IoCallDriver(ext->Lower, irp);
}

static VOID pmReadStop(PPM_EXT ext, ULONG wait)
{
    PIRP irp;
    KIRQL irql;

    KeAcquireSpinLock(&ext->Lock, &irql);
    ext->St.ReadsOn = 0;
    irp = ext->ReadIrp;
    if (irp != NULL) {
        InterlockedIncrement((PLONG)&ext->ReadRef);
    }
    KeReleaseSpinLock(&ext->Lock, irql);
    if (irp != NULL) {
        (VOID)IoCancelIrp(irp);
        if (InterlockedDecrement((PLONG)&ext->ReadRef) == 0) {
            IoFreeIrp(irp);
            KeSetEvent(&ext->ReadGone, IO_NO_INCREMENT, FALSE);
        }
    }
    if (wait) {
        (VOID)KeWaitForSingleObject(&ext->ReadGone, Executive, KernelMode,
                                    FALSE, NULL);
    }
}

/* ------------------------------------------------------------------ */
/* PMCTL's requests                                                    */
/* ------------------------------------------------------------------ */

static NTSTATUS NTAPI pmIoctl(PDEVICE_OBJECT dev, PIRP irp)
{
    PPM_EXT ext;
    PIO_STACK_LOCATION stack;
    PULONG in;
    ULONG inLen;
    ULONG outLen;
    ULONG_PTR info;
    NTSTATUS status;
    KIRQL irql;

    ext = (PPM_EXT)dev->DeviceExtension;
    stack = IoGetCurrentIrpStackLocation(irp);
    in = (PULONG)irp->AssociatedIrp.SystemBuffer;
    inLen = stack->Parameters.DeviceIoControl.InputBufferLength;
    outLen = stack->Parameters.DeviceIoControl.OutputBufferLength;
    info = 0;
    status = STATUS_SUCCESS;
    switch (stack->Parameters.DeviceIoControl.IoControlCode) {
    case PM_IOCTL_STATUS:
        if (outLen < sizeof(PM_STATUS)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        KeAcquireSpinLock(&ext->Lock, &irql);
        RtlCopyMemory(irp->AssociatedIrp.SystemBuffer, &ext->St,
                      sizeof(PM_STATUS));
        KeReleaseSpinLock(&ext->Lock, irql);
        info = sizeof(PM_STATUS);
        break;
    case PM_IOCTL_SET_POWER:
        if (inLen < sizeof(ULONG) || *in < PowerDeviceD0 ||
            *in > PowerDeviceD3) {
            status = STATUS_INVALID_PARAMETER;
            break;
        }
        status = pmRequestPower(ext, (DEVICE_POWER_STATE)*in, NULL, NULL);
        if (status == STATUS_PENDING) {
            status = STATUS_SUCCESS;
        }
        break;
    case PM_IOCTL_WAIT_WAKE:
        status = pmRequestWake(ext);
        if (status == STATUS_PENDING) {
            status = STATUS_SUCCESS;
        }
        break;
    case PM_IOCTL_WW_CANCEL:
        pmWakeCancel(ext);
        break;
    case PM_IOCTL_IDLE:
        status = pmIdleSubmit(ext, (inLen >= sizeof(ULONG)) ? *in : 0);
        break;
    case PM_IOCTL_IDLE_CANCEL:
        pmIdleCancel(ext);
        break;
    case PM_IOCTL_READS:
        if (inLen >= sizeof(ULONG) && *in != 0) {
            if (!ext->St.ReadsOn && ext->InPipe != NULL) {
                ext->St.ReadsOn = 1;
                KeClearEvent(&ext->ReadGone);
                pmReadStart(ext);
            }
        } else {
            pmReadStop(ext, 0);
        }
        break;
    case PM_IOCTL_RESET:
        KeAcquireSpinLock(&ext->Lock, &irql);
        ext->St.PowerRequests = ext->St.PowerCallbacks = 0;
        ext->St.PowerRefused = 0;
        ext->St.WakeRequests = ext->St.WakeCallbacks = 0;
        ext->St.WakeIrpCompletions = 0;
        ext->St.IdleSubmits = ext->St.IdleCallbacks = 0;
        ext->St.IdleCompletions = 0;
        ext->St.ReadsCompleted = ext->St.ReadsFailed = 0;
        KeReleaseSpinLock(&ext->Lock, irql);
        break;
    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }
    irp->IoStatus.Status = status;
    irp->IoStatus.Information = info;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
}

/* ------------------------------------------------------------------ */
/* PnP and power                                                       */
/* ------------------------------------------------------------------ */

static NTSTATUS pmForwardSync(PPM_EXT ext, PIRP irp)
{
    KEVENT done;

    KeInitializeEvent(&done, NotificationEvent, FALSE);
    IoCopyCurrentIrpStackLocationToNext(irp);
    IoSetCompletionRoutine(irp, pmSyncDone, &done, TRUE, TRUE, TRUE);
    (VOID)IoCallDriver(ext->Lower, irp);
    (VOID)KeWaitForSingleObject(&done, Executive, KernelMode, FALSE, NULL);
    return irp->IoStatus.Status;
}

static VOID pmQuiesce(PPM_EXT ext)
{
    pmReadStop(ext, 1);
    pmIdleCancel(ext);
    (VOID)KeWaitForSingleObject(&ext->IdleGone, Executive, KernelMode, FALSE,
                                NULL);
    pmWakeCancel(ext);
}

static NTSTATUS NTAPI pmPnp(PDEVICE_OBJECT dev, PIRP irp)
{
    PPM_EXT ext;
    PIO_STACK_LOCATION stack;
    NTSTATUS status;

    ext = (PPM_EXT)dev->DeviceExtension;
    stack = IoGetCurrentIrpStackLocation(irp);
    switch (stack->MinorFunction) {
    case IRP_MN_START_DEVICE:
        status = pmForwardSync(ext, irp);
        if (NT_SUCCESS(status)) {
            ext->St.Started = 1;
            if (ext->ReadUrb == NULL) {
                ext->ReadUrb = (PURB)ExAllocatePool(
                    NonPagedPool, sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER));
            }
            pmConfigure(ext);
        }
        irp->IoStatus.Status = status;
        IoCompleteRequest(irp, IO_NO_INCREMENT);
        return status;
    case IRP_MN_STOP_DEVICE:
    case IRP_MN_SURPRISE_REMOVAL:
        ext->St.Started = 0;
        pmQuiesce(ext);
        irp->IoStatus.Status = STATUS_SUCCESS;
        return pmPass(dev, irp);
    case IRP_MN_REMOVE_DEVICE:
        ext->Removed = 1;
        ext->St.Started = 0;
        pmQuiesce(ext);
        irp->IoStatus.Status = STATUS_SUCCESS;
        IoSkipCurrentIrpStackLocation(irp);
        status = IoCallDriver(ext->Lower, irp);
        if (ext->LinkMade) {
            (VOID)IoDeleteSymbolicLink(&ext->Link);
        }
        if (ext->ReadUrb != NULL) {
            ExFreePool(ext->ReadUrb);
        }
        IoDetachDevice(ext->Lower);
        IoDeleteDevice(dev);
        return status;
    case IRP_MN_QUERY_STOP_DEVICE:
    case IRP_MN_QUERY_REMOVE_DEVICE:
    case IRP_MN_CANCEL_STOP_DEVICE:
    case IRP_MN_CANCEL_REMOVE_DEVICE:
        irp->IoStatus.Status = STATUS_SUCCESS;
        return pmPass(dev, irp);
    default:
        return pmPass(dev, irp);
    }
}

static NTSTATUS NTAPI pmSetPowerDone(PDEVICE_OBJECT dev, PIRP irp,
                                     PVOID ctx)
{
    PPM_EXT ext;
    PIO_STACK_LOCATION stack;

    UNREFERENCED_PARAMETER(ctx);
    ext = (PPM_EXT)dev->DeviceExtension;
    stack = IoGetCurrentIrpStackLocation(irp);
    if (irp->PendingReturned) {
        IoMarkIrpPending(irp);
    }
    if (NT_SUCCESS(irp->IoStatus.Status)) {
        if (stack->Parameters.Power.Type == DevicePowerState) {
            ext->St.DevicePower = stack->Parameters.Power.State.DeviceState;
            (VOID)PoSetPowerState(dev, DevicePowerState,
                                  stack->Parameters.Power.State);
        } else {
            ext->St.SystemPower = stack->Parameters.Power.State.SystemState;
        }
    }
    PoStartNextPowerIrp(irp);
    return STATUS_SUCCESS;
}

static NTSTATUS NTAPI pmPower(PDEVICE_OBJECT dev, PIRP irp)
{
    PPM_EXT ext;
    PIO_STACK_LOCATION stack;
    KIRQL irql;

    ext = (PPM_EXT)dev->DeviceExtension;
    stack = IoGetCurrentIrpStackLocation(irp);
    if (stack->MinorFunction == IRP_MN_SET_POWER) {
        IoCopyCurrentIrpStackLocationToNext(irp);
        IoSetCompletionRoutine(irp, pmSetPowerDone, NULL, TRUE, TRUE, TRUE);
        return PoCallDriver(ext->Lower, irp);
    }
    if (stack->MinorFunction == IRP_MN_WAIT_WAKE) {
        KeAcquireSpinLock(&ext->Lock, &irql);
        ext->WakeIrp = irp;
        ext->WakeRef = 1;
        ext->St.WakeHeld = 1;
        KeReleaseSpinLock(&ext->Lock, irql);
        IoCopyCurrentIrpStackLocationToNext(irp);
        IoSetCompletionRoutine(irp, pmWakeIrpDone, NULL, TRUE, TRUE, TRUE);
        PoStartNextPowerIrp(irp);
        return PoCallDriver(ext->Lower, irp);
    }
    PoStartNextPowerIrp(irp);
    IoSkipCurrentIrpStackLocation(irp);
    return PoCallDriver(ext->Lower, irp);
}
