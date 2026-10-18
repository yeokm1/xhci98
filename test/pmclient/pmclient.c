/*
 * pmclient.c - XHCIPM.SYS, selective suspend's test client (roadmap-hcd.md
 * task 36.34; design record 14 section 10, SUSPa.6). TEST ONLY, NEVER
 * SHIPPED: scripts\package\make-release.ps1 refuses a release carrying it.
 *
 * A WDM function driver for one USB device on a VM leg (xhcipm.inf binds
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

/* 'XhPm' in a pool dump on Windows 2000: the bytes are stored low first.
 * Tagged allocation and the plain ExFreePool, the pair xhci98.sys uses
 * (src/hcd_pool.c). */
#define PM_POOL_TAG 0x6D506858UL

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
    /* Admission: Open from a successful START to the next STOP, SURPRISE
     * or REMOVE; Busy counts PMCTL's dispatches inside; Outstanding every
     * power request whose callback has not run, which REMOVE waits out. */
    volatile ULONG Open;
    LONG Busy;
    LONG Outstanding;
    ULONG PowerTarget;      /* the D-state the SET in flight asks, 0 none */
    PIRP DeferredSIrp;      /* a system SET held behind it (pmPowerRelease) */
    ULONG DeferredSState;
    ULONG DeferredD;        /* a wake's or idle completion's D0 behind it */
    /* WAIT_WAKE: WakeBusy reserves this client's one request until its
     * callback; WakeIrp/WakeRef track it as it passes down through this
     * FDO, WakeRef 1 for the IRP, +1 while a canceller holds it (the
     * completion routine keeps the IRP while a canceller does). */
    LONG WakeBusy;
    PIRP WakeIrp;
    LONG WakeRef;
    /* The idle IRP, this driver's own: IdleBusy reserves it until its last
     * reference is let go; IdleRef 1 for its completion, +1 while a
     * canceller holds it; the last frees it. IdlePowerDone: the callback's
     * D-IRP has completed. */
    LONG IdleBusy;
    PIRP IdleIrp;
    LONG IdleRef;
    ULONG IdleFlags;
    PM_IDLE_INFO IdleInfo;
    LONG IdlePowerDone;
    /* The read loop (pmReadPost): wanted, posted, the IRP and its
     * reference, the last completion's verdict. */
    USBD_PIPE_HANDLE InPipe;
    ULONG InMax;
    ULONG ReadWanted;
    LONG ReadBusy;
    PIRP ReadIrp;
    LONG ReadRef;
    ULONG ReadOk;
    PURB ReadUrb;
    UCHAR ReadBuf[64];
    UCHAR Config[512];
} PM_EXT, *PPM_EXT;

static ULONG pmNextIndex;

static NTSTATUS NTAPI pmAddDevice(PDRIVER_OBJECT driver, PDEVICE_OBJECT pdo);
static VOID NTAPI pmUnload(PDRIVER_OBJECT driver);
static NTSTATUS NTAPI pmCreateClose(PDEVICE_OBJECT dev, PIRP irp);
static NTSTATUS NTAPI pmIoctl(PDEVICE_OBJECT dev, PIRP irp);
static NTSTATUS NTAPI pmPnp(PDEVICE_OBJECT dev, PIRP irp);
static NTSTATUS NTAPI pmPower(PDEVICE_OBJECT dev, PIRP irp);
static NTSTATUS NTAPI pmPass(PDEVICE_OBJECT dev, PIRP irp);

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
/* Waits (PASSIVE_LEVEL)                                               */
/* ------------------------------------------------------------------ */

/* A relative 1..200000 ms interval built without 64-bit arithmetic, as
 * the bus's HcdRelativeMs (no _allmul import). */
static VOID pmSleepMs(ULONG ms)
{
    LARGE_INTEGER due;

    due.LowPart = (ULONG)(-(LONG)(ms * 10000UL));
    due.HighPart = -1;
    (VOID)KeDelayExecutionThread(KernelMode, FALSE, &due);
}

/* Until `*count` reads 0, polled at 1 ms: an event wait on Windows 98 may
 * return unsatisfied (docs\usb-xhci-info\win98-wdm.md). Returns 1, or 0
 * when `boundMs` (0: none) ran out first. */
static ULONG pmWaitZero(volatile LONG *count, ULONG boundMs)
{
    ULONG waited;

    for (waited = 0; *count != 0; waited++) {
        if (boundMs != 0 && waited >= boundMs) {
            return 0;
        }
        pmSleepMs(1);
    }
    return 1;
}

static VOID pmWaitNonZero(volatile LONG *flag)
{
    while (*flag == 0) {
        pmSleepMs(1);
    }
}

/* ------------------------------------------------------------------ */
/* Synchronous URBs at PASSIVE_LEVEL                                   */
/* ------------------------------------------------------------------ */

/* The completion proven by a flag the routine sets last, not by an event
 * wait whose unsatisfied return on Windows 98 would free or complete a
 * request still in flight (task 36.34). */
static NTSTATUS NTAPI pmSyncDone(PDEVICE_OBJECT dev, PIRP irp, PVOID ctx)
{
    UNREFERENCED_PARAMETER(dev);
    UNREFERENCED_PARAMETER(irp);
    InterlockedExchange((PLONG)ctx, 1);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static NTSTATUS pmUrbSync(PPM_EXT ext, PURB urb)
{
    PIRP irp;
    PIO_STACK_LOCATION next;
    LONG done;
    NTSTATUS status;

    irp = IoAllocateIrp(ext->Lower->StackSize, FALSE);
    if (irp == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    done = 0;
    next = IoGetNextIrpStackLocation(irp);
    next->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
    next->Parameters.DeviceIoControl.IoControlCode =
        IOCTL_INTERNAL_USB_SUBMIT_URB;
    next->Parameters.Others.Argument1 = urb;
    irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    IoSetCompletionRoutine(irp, pmSyncDone, &done, TRUE, TRUE, TRUE);
    (VOID)IoCallDriver(ext->Lower, irp);
    pmWaitNonZero(&done);
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

    /* Nothing of an earlier START's configuration survives a failed one
     * (task 36.34). */
    ext->InPipe = NULL;
    ext->InMax = 0;
    ext->St.Configured = 0;
    ext->St.InterruptIn = 0;
    ext->St.ConfigAttributes = 0;
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
    sel = (PURB)ExAllocatePoolWithTag(NonPagedPool, size, PM_POOL_TAG);
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

/*
 * The device transitions, serialized (task 36.34): one SET in flight at a
 * time, its slot PowerTarget owned by the
 * request that took it and let go only by that request's callback or
 * refusal (pmPowerRelease). A request for the confirmed current state with
 * no SET in flight is answered here, its result accounted with no slot
 * touched (9x may pend a same-state request and never call back). A
 * request that finds the slot taken is, for an explicit PMCTL command or
 * the idle callback, refused BUSY; for the transitions the policy owner
 * must make - a system SET's device state, the D0 after a wake or an idle
 * completion - deferred and started when the slot is let go: DeferredSIrp
 * (the system IRP, held, counted in Outstanding) first, then DeferredD.
 * A QUERY takes no slot.
 */
#define PM_POWER_GO         0
#define PM_POWER_LOCAL      1
#define PM_POWER_BUSY       2
#define PM_POWER_DEFERRED   3

#define PM_DEFER_NONE       0
#define PM_DEFER_D          1   /* DeferredD                         */
#define PM_DEFER_SIRP       2   /* DeferredSIrp with DeferredSState  */

static VOID NTAPI pmSystemDevDone(PDEVICE_OBJECT dev, UCHAR minor,
                                  POWER_STATE state, PVOID ctx,
                                  PIO_STATUS_BLOCK io);
static VOID NTAPI pmPowerDone(PDEVICE_OBJECT dev, UCHAR minor,
                              POWER_STATE state, PVOID ctx,
                              PIO_STATUS_BLOCK io);

static ULONG pmPowerBegin(PPM_EXT ext, UCHAR minor, DEVICE_POWER_STATE d,
                          ULONG defer, PIRP sirp)
{
    KIRQL irql;
    ULONG answer;

    KeAcquireSpinLock(&ext->Lock, &irql);
    if (defer == PM_DEFER_D && ext->St.SystemPower != PowerSystemWorking) {
        /* A wake's or idle completion's D0 has no place below S0: the
         * system's own transition decides the device state then (task
         * 36.34). */
        answer = PM_POWER_BUSY;
    } else if (ext->PowerTarget != 0) {
        /* A QUERY too: never answered against a state about to change;
         * only a SET may be deferred. */
        answer = PM_POWER_BUSY;
        if (minor == IRP_MN_SET_POWER && defer == PM_DEFER_SIRP &&
            ext->DeferredSIrp == NULL) {
            ext->DeferredSIrp = sirp;
            ext->DeferredSState = (ULONG)d;
            if ((ULONG)d != PowerDeviceD0) {
                /* A sleep supersedes a queued wake D0. */
                ext->DeferredD = 0;
            }
            answer = PM_POWER_DEFERRED;
        } else if (minor == IRP_MN_SET_POWER && defer == PM_DEFER_D) {
            ext->DeferredD = (ULONG)d;
            answer = PM_POWER_DEFERRED;
        }
    } else if ((ULONG)d == ext->St.DevicePower) {
        answer = PM_POWER_LOCAL;
    } else {
        answer = PM_POWER_GO;
        if (minor == IRP_MN_SET_POWER) {
            ext->PowerTarget = (ULONG)d;
        }
    }
    KeReleaseSpinLock(&ext->Lock, irql);
    return answer;
}

/* A SET's result counted; no slot is touched. */
static VOID pmPowerAccount(PPM_EXT ext, ULONG d, NTSTATUS status, ULONG idle)
{
    KIRQL irql;

    KeAcquireSpinLock(&ext->Lock, &irql);
    ext->St.PowerCallbacks++;
    ext->St.PowerLastStatus = status;
    ext->St.PowerLastState = d;
    if (idle) {
        ext->St.IdleCallbackPowerStatus = status;
    }
    KeReleaseSpinLock(&ext->Lock, irql);
}

static VOID pmPowerRelease(PPM_EXT ext);

/* A SET whose slot the caller owns, sent; a refusal lets the slot go. */
static NTSTATUS pmPowerSend(PPM_EXT ext, ULONG d,
                            PREQUEST_POWER_COMPLETE done, PVOID ctx)
{
    POWER_STATE state;
    NTSTATUS status;
    KIRQL irql;

    state.DeviceState = (DEVICE_POWER_STATE)d;
    InterlockedIncrement(&ext->Outstanding);
    status = PoRequestPowerIrp(ext->Pdo, IRP_MN_SET_POWER, state, done, ctx,
                               NULL);
    if (status != STATUS_PENDING && !NT_SUCCESS(status)) {
        KeAcquireSpinLock(&ext->Lock, &irql);
        ext->St.PowerRefused++;
        ext->St.PowerLastStatus = status;
        KeReleaseSpinLock(&ext->Lock, irql);
        pmPowerRelease(ext);
        InterlockedDecrement(&ext->Outstanding);
    }
    return status;
}

/* The slot let go by its owner, and the deferred work drained in order:
 * the held system IRP first (completed at once when its state is already
 * the device's, or when its request is refused), then the deferred D - a
 * wake's D0 only while the system is in S0 and the device open - until
 * one request is sent, which then owns the slot, or nothing is left
 * (task 36.34). IRQL: <= DISPATCH_LEVEL. */
static VOID pmPowerRelease(PPM_EXT ext)
{
    KIRQL irql;
    PIRP sirp;
    ULONG sd;
    ULONG wd;
    ULONG go;

    KeAcquireSpinLock(&ext->Lock, &irql);
    ext->PowerTarget = 0;
    for (;;) {
        if (ext->PowerTarget != 0) {
            /* Another request took the slot while this drain completed
             * satisfied work unlocked: the rest is its owner's (task
             * 36.34). */
            KeReleaseSpinLock(&ext->Lock, irql);
            return;
        }
        sirp = ext->DeferredSIrp;
        sd = ext->DeferredSState;
        wd = 0;
        go = 0;
        if (sirp != NULL) {
            ext->DeferredSIrp = NULL;
            if (sd != ext->St.DevicePower) {
                ext->PowerTarget = sd;
                go = 1;
            }
        } else if (ext->DeferredD != 0) {
            wd = ext->DeferredD;
            ext->DeferredD = 0;
            if (wd != ext->St.DevicePower && ext->Open &&
                ext->St.SystemPower == PowerSystemWorking) {
                ext->PowerTarget = wd;
                go = 1;
            }
        } else {
            KeReleaseSpinLock(&ext->Lock, irql);
            return;
        }
        KeReleaseSpinLock(&ext->Lock, irql);
        if (sirp != NULL) {
            if (!go ||
                !NT_SUCCESS(pmPowerSend(ext, sd, pmSystemDevDone, sirp))) {
                /* Satisfied already, or refused (pmPowerSend let the slot
                 * go and drained on): completed here. */
                PoStartNextPowerIrp(sirp);
                IoCompleteRequest(sirp, IO_NO_INCREMENT);
                /* The hold's own count (pmSystemDone). */
                InterlockedDecrement(&ext->Outstanding);
                if (go) {
                    return;
                }
                KeAcquireSpinLock(&ext->Lock, &irql);
                continue;
            }
            InterlockedDecrement(&ext->Outstanding);
            return;
        }
        if (go) {
            /* Sent, or refused with its own drain (pmPowerSend). */
            (VOID)pmPowerSend(ext, wd, pmPowerDone, ext);
            return;
        }
        KeAcquireSpinLock(&ext->Lock, &irql);
    }
}

/* A sent SET's callback: its owner, so it lets the slot go. */
static VOID NTAPI pmPowerDone(PDEVICE_OBJECT dev, UCHAR minor,
                              POWER_STATE state, PVOID ctx,
                              PIO_STATUS_BLOCK io)
{
    PPM_EXT ext;

    UNREFERENCED_PARAMETER(dev);
    UNREFERENCED_PARAMETER(minor);
    ext = (PPM_EXT)ctx;
    pmPowerAccount(ext, state.DeviceState, io->Status, 0);
    pmPowerRelease(ext);
    InterlockedDecrement(&ext->Outstanding);
}

static VOID NTAPI pmIdlePowerDone(PDEVICE_OBJECT dev, UCHAR minor,
                                  POWER_STATE state, PVOID ctx,
                                  PIO_STATUS_BLOCK io);

/* A D-IRP requested. `defer` (PM_DEFER_D) for a transition the policy
 * owner must make: deferred behind a SET in flight rather than refused.
 * `idle` the idle callback's own (its callback pmIdlePowerDone, which
 * releases its waiter). Every request holds Outstanding until its
 * callback has run, which REMOVE waits for. Returns SUCCESS (answered
 * here, or deferred), PENDING (sent), or a refusal. */
static NTSTATUS pmRequestPower(PPM_EXT ext, DEVICE_POWER_STATE d,
                               ULONG defer, ULONG idle)
{
    KIRQL irql;
    ULONG answer;

    KeAcquireSpinLock(&ext->Lock, &irql);
    ext->St.PowerRequests++;
    KeReleaseSpinLock(&ext->Lock, irql);
    answer = pmPowerBegin(ext, IRP_MN_SET_POWER, d, defer, NULL);
    switch (answer) {
    case PM_POWER_BUSY:
        KeAcquireSpinLock(&ext->Lock, &irql);
        ext->St.PowerRefused++;
        ext->St.PowerLastStatus = STATUS_DEVICE_BUSY;
        KeReleaseSpinLock(&ext->Lock, irql);
        return STATUS_DEVICE_BUSY;
    case PM_POWER_DEFERRED:
        return STATUS_SUCCESS;
    case PM_POWER_LOCAL:
        pmPowerAccount(ext, (ULONG)d, STATUS_SUCCESS, idle);
        if (idle) {
            InterlockedExchange(&ext->IdlePowerDone, 1);
        }
        return STATUS_SUCCESS;
    default:
        break;
    }
    return pmPowerSend(ext, (ULONG)d, idle ? pmIdlePowerDone : pmPowerDone,
                       ext);
}

/* The requester's WAIT_WAKE callback (5.5's 9x defect is this never being
 * called): counted, and a wake that completed SUCCESS brings the device
 * back to D0, as a class driver does. The reservation is the IRP's, let
 * go when its last reference in this FDO retires (pmWakeRetire). */
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
    if (NT_SUCCESS(status) && ext->Open) {
        (VOID)pmRequestPower(ext, PowerDeviceD0, PM_DEFER_D, 0);
    }
    InterlockedDecrement(&ext->Outstanding);
}

/* One WAIT_WAKE at a time: reserved here, released when the IRP's last
 * reference in this FDO retires (pmWakeRetire), or here when the request
 * is refused; its callback never touches the reservation, so an older
 * callback cannot release a newer request's (task 36.34). */
static NTSTATUS pmRequestWake(PPM_EXT ext)
{
    POWER_STATE state;
    NTSTATUS status;
    KIRQL irql;

    if (InterlockedExchange(&ext->WakeBusy, 1) != 0) {
        return STATUS_DEVICE_BUSY;
    }
    state.SystemState = PowerSystemWorking;
    KeAcquireSpinLock(&ext->Lock, &irql);
    ext->St.WakeRequests++;
    KeReleaseSpinLock(&ext->Lock, irql);
    InterlockedIncrement(&ext->Outstanding);
    status = PoRequestPowerIrp(ext->Pdo, IRP_MN_WAIT_WAKE, state, pmWakeDone,
                               ext, NULL);
    if (status != STATUS_PENDING && !NT_SUCCESS(status)) {
        InterlockedDecrement(&ext->Outstanding);
        InterlockedExchange(&ext->WakeBusy, 0);
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
    if (InterlockedDecrement(&ext->WakeRef) != 0) {
        return STATUS_MORE_PROCESSING_REQUIRED;
    }
    /* The request is over for the device whether or not its requester's
     * callback comes (the 9x defect): another may be requested. */
    InterlockedExchange(&ext->WakeBusy, 0);
    return STATUS_SUCCESS;
}

static VOID pmWakeCancel(PPM_EXT ext)
{
    PIRP irp;
    KIRQL irql;

    KeAcquireSpinLock(&ext->Lock, &irql);
    irp = ext->WakeIrp;
    if (irp != NULL) {
        InterlockedIncrement(&ext->WakeRef);
    }
    KeReleaseSpinLock(&ext->Lock, irql);
    if (irp == NULL) {
        return;
    }
    (VOID)IoCancelIrp(irp);
    if (InterlockedDecrement(&ext->WakeRef) == 0) {
        InterlockedExchange(&ext->WakeBusy, 0);
        IoCompleteRequest(irp, IO_NO_INCREMENT);
    }
}

/* ------------------------------------------------------------------ */
/* The idle notification                                               */
/* ------------------------------------------------------------------ */

/* The idle callback's sent SET: its owner, so it lets the slot go; its
 * waiter released. */
static VOID NTAPI pmIdlePowerDone(PDEVICE_OBJECT dev, UCHAR minor,
                                  POWER_STATE state, PVOID ctx,
                                  PIO_STATUS_BLOCK io)
{
    PPM_EXT ext;

    UNREFERENCED_PARAMETER(dev);
    UNREFERENCED_PARAMETER(minor);
    ext = (PPM_EXT)ctx;
    pmPowerAccount(ext, state.DeviceState, io->Status, 1);
    InterlockedExchange(&ext->IdlePowerDone, 1);
    pmPowerRelease(ext);
    InterlockedDecrement(&ext->Outstanding);
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
    if ((flags & PM_IDLE_NO_POWER) || !ext->Open) {
        return;
    }
    if (flags & PM_IDLE_ARM_WAKE) {
        (VOID)pmRequestWake(ext);
    }
    InterlockedExchange(&ext->IdlePowerDone, 0);
    if (pmRequestPower(ext, d, PM_DEFER_NONE, 1) == STATUS_PENDING) {
        /* LOCAL called the callback already; BUSY and a refusal never
         * will. */
        pmWaitNonZero(&ext->IdlePowerDone);
    }
}

/* The idle IRP's last reference let go: freed, the reservation released.
 * IRQL: <= DISPATCH_LEVEL. */
static VOID pmIdleRetire(PPM_EXT ext, PIRP irp)
{
    IoFreeIrp(irp);
    InterlockedExchange(&ext->IdleBusy, 0);
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
        ext->Open) {
        (VOID)pmRequestPower(ext, PowerDeviceD0, PM_DEFER_D, 0);
    }
    if (InterlockedDecrement(&ext->IdleRef) == 0) {
        pmIdleRetire(ext, irp);
    }
    return STATUS_MORE_PROCESSING_REQUIRED;
}

/* One idle IRP at a time: IdleBusy is reserved until its last reference
 * is let go (pmIdleRetire), so nothing of it is reused before then. */
static NTSTATUS pmIdleSubmit(PPM_EXT ext, ULONG flags)
{
    PIRP irp;
    PIO_STACK_LOCATION next;
    KIRQL irql;

    if (InterlockedExchange(&ext->IdleBusy, 1) != 0) {
        return STATUS_DEVICE_BUSY;
    }
    irp = IoAllocateIrp(ext->Lower->StackSize, FALSE);
    if (irp == NULL) {
        InterlockedExchange(&ext->IdleBusy, 0);
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
        InterlockedIncrement(&ext->IdleRef);
    }
    KeReleaseSpinLock(&ext->Lock, irql);
    if (irp == NULL) {
        return;
    }
    (VOID)IoCancelIrp(irp);
    if (InterlockedDecrement(&ext->IdleRef) == 0) {
        pmIdleRetire(ext, irp);
    }
}

/* ------------------------------------------------------------------ */
/* The read loop                                                       */
/* ------------------------------------------------------------------ */

/*
 * One interrupt IN read at a time. ReadWanted is the request (READS ON);
 * ReadBusy says a read is posted or being posted, and holds until its IRP
 * is retired; both under the lock. A post publishes its IRP only while the
 * read is still wanted and the device open, in the hold a stop clears
 * ReadWanted in, so a stop either cancels the IRP it finds or the post
 * finds the stop and retires itself.
 */
static NTSTATUS pmReadPost(PPM_EXT ext);

/* The IRP's last reference let go: freed, and either the next posted or
 * the loop ended. IRQL: <= DISPATCH_LEVEL. */
static VOID pmReadRetire(PPM_EXT ext, PIRP irp, ULONG ok)
{
    KIRQL irql;
    ULONG again;

    IoFreeIrp(irp);
    KeAcquireSpinLock(&ext->Lock, &irql);
    if (!ok) {
        ext->ReadWanted = 0;
    }
    again = ext->ReadWanted && ext->Open;
    if (!again) {
        ext->ReadBusy = 0;
        ext->St.ReadsOn = 0;
    }
    KeReleaseSpinLock(&ext->Lock, irql);
    if (again) {
        (VOID)pmReadPost(ext);
    }
}

static NTSTATUS NTAPI pmReadDone(PDEVICE_OBJECT dev, PIRP irp, PVOID ctx)
{
    PPM_EXT ext;
    KIRQL irql;
    ULONG ok;

    UNREFERENCED_PARAMETER(dev);
    ext = (PPM_EXT)ctx;
    ok = NT_SUCCESS(irp->IoStatus.Status) ? 1UL : 0UL;
    KeAcquireSpinLock(&ext->Lock, &irql);
    if (ok) {
        ext->St.ReadsCompleted++;
    } else {
        ext->St.ReadsFailed++;
    }
    ext->St.ReadLastStatus = irp->IoStatus.Status;
    ext->St.ReadLastUsbd = (ULONG)ext->ReadUrb->UrbHeader.Status;
    ext->ReadIrp = NULL;
    ext->ReadOk = ok;
    KeReleaseSpinLock(&ext->Lock, irql);
    if (InterlockedDecrement(&ext->ReadRef) == 0) {
        pmReadRetire(ext, irp, ok);
    }
    return STATUS_MORE_PROCESSING_REQUIRED;
}

/* IRQL: <= DISPATCH_LEVEL; ReadBusy set by the caller. Returns SUCCESS when
 * a read is posted, else why not. */
static NTSTATUS pmReadPost(PPM_EXT ext)
{
    PIRP irp;
    PIO_STACK_LOCATION next;
    struct _URB_BULK_OR_INTERRUPT_TRANSFER *u;
    KIRQL irql;
    ULONG go;

    irp = IoAllocateIrp(ext->Lower->StackSize, FALSE);
    u = &ext->ReadUrb->UrbBulkOrInterruptTransfer;
    if (irp != NULL) {
        pmZero(u, sizeof(*u));
        u->Hdr.Length = sizeof(*u);
        u->Hdr.Function = URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER;
        u->PipeHandle = ext->InPipe;
        u->TransferFlags =
            USBD_TRANSFER_DIRECTION_IN | USBD_SHORT_TRANSFER_OK;
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
    }
    KeAcquireSpinLock(&ext->Lock, &irql);
    go = (irp != NULL && ext->ReadWanted && ext->Open) ? 1UL : 0UL;
    if (go) {
        ext->ReadRef = 1;
        ext->ReadIrp = irp;
    } else {
        ext->ReadWanted = 0;
        ext->ReadBusy = 0;
        ext->St.ReadsOn = 0;
    }
    KeReleaseSpinLock(&ext->Lock, irql);
    if (!go) {
        if (irp != NULL) {
            IoFreeIrp(irp);
            return STATUS_CANCELLED;
        }
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    (VOID)IoCallDriver(ext->Lower, irp);
    return STATUS_SUCCESS;
}

/* READS ON: SUCCESS when a read is posted or already running, BUSY while
 * a stopped read is still retiring (task 36.34), NOT_READY with no
 * interrupt IN pipe. */
static NTSTATUS pmReadStart(PPM_EXT ext)
{
    KIRQL irql;
    NTSTATUS status;

    KeAcquireSpinLock(&ext->Lock, &irql);
    if (!ext->Open || ext->InPipe == NULL || ext->ReadUrb == NULL) {
        status = STATUS_DEVICE_NOT_READY;
    } else if (ext->ReadBusy) {
        status = ext->ReadWanted ? STATUS_SUCCESS : STATUS_DEVICE_BUSY;
    } else {
        ext->ReadBusy = 1;
        ext->ReadWanted = 1;
        ext->St.ReadsOn = 1;
        status = STATUS_PENDING;
    }
    KeReleaseSpinLock(&ext->Lock, irql);
    if (status == STATUS_PENDING) {
        /* The first post's failure is the request's (task 36.34). */
        status = pmReadPost(ext);
    }
    return status;
}

/* READS OFF, and every stop: the read cancelled; `wait` until the loop has
 * retired its IRP (PASSIVE_LEVEL). */
static VOID pmReadStop(PPM_EXT ext, ULONG wait)
{
    PIRP irp;
    KIRQL irql;

    KeAcquireSpinLock(&ext->Lock, &irql);
    ext->ReadWanted = 0;
    irp = ext->ReadIrp;
    if (irp != NULL) {
        InterlockedIncrement(&ext->ReadRef);
    }
    KeReleaseSpinLock(&ext->Lock, irql);
    if (irp != NULL) {
        (VOID)IoCancelIrp(irp);
        if (InterlockedDecrement(&ext->ReadRef) == 0) {
            pmReadRetire(ext, irp, ext->ReadOk);
        }
    }
    if (wait) {
        (VOID)pmWaitZero(&ext->ReadBusy, 0);
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
    ULONG code;
    ULONG_PTR info;
    NTSTATUS status;
    KIRQL irql;

    ext = (PPM_EXT)dev->DeviceExtension;
    stack = IoGetCurrentIrpStackLocation(irp);
    in = (PULONG)irp->AssociatedIrp.SystemBuffer;
    inLen = stack->Parameters.DeviceIoControl.InputBufferLength;
    outLen = stack->Parameters.DeviceIoControl.OutputBufferLength;
    code = stack->Parameters.DeviceIoControl.IoControlCode;
    info = 0;
    status = STATUS_SUCCESS;
    /* Admission (task 36.34): every request
     * but STATUS needs the device open, and runs inside Busy, which a stop
     * waits out after closing admission. */
    InterlockedIncrement(&ext->Busy);
    if (code != PM_IOCTL_STATUS && !ext->Open) {
        InterlockedDecrement(&ext->Busy);
        irp->IoStatus.Status = STATUS_DEVICE_NOT_READY;
        irp->IoStatus.Information = 0;
        IoCompleteRequest(irp, IO_NO_INCREMENT);
        return STATUS_DEVICE_NOT_READY;
    }
    switch (code) {
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
        status = pmRequestPower(ext, (DEVICE_POWER_STATE)*in,
                                PM_DEFER_NONE, 0);
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
            status = pmReadStart(ext);
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
    InterlockedDecrement(&ext->Busy);
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
    LONG done;

    done = 0;
    IoCopyCurrentIrpStackLocationToNext(irp);
    IoSetCompletionRoutine(irp, pmSyncDone, &done, TRUE, TRUE, TRUE);
    (VOID)IoCallDriver(ext->Lower, irp);
    pmWaitNonZero(&done);
    return irp->IoStatus.Status;
}

/* Admission closed, the dispatches inside waited out, then the read, the
 * idle IRP and the WAIT_WAKE let go of. PASSIVE_LEVEL. */
static VOID pmQuiesce(PPM_EXT ext)
{
    ext->Open = 0;
    (VOID)pmWaitZero(&ext->Busy, 0);
    pmReadStop(ext, 1);
    pmIdleCancel(ext);
    (VOID)pmWaitZero(&ext->IdleBusy, 0);
    pmWakeCancel(ext);
}

static NTSTATUS NTAPI pmPnp(PDEVICE_OBJECT dev, PIRP irp)
{
    PPM_EXT ext;
    PIO_STACK_LOCATION stack;
    NTSTATUS status;
    ULONG keep;

    ext = (PPM_EXT)dev->DeviceExtension;
    stack = IoGetCurrentIrpStackLocation(irp);
    switch (stack->MinorFunction) {
    case IRP_MN_START_DEVICE:
        status = pmForwardSync(ext, irp);
        if (NT_SUCCESS(status) && ext->ReadUrb == NULL) {
            ext->ReadUrb = (PURB)ExAllocatePoolWithTag(
                NonPagedPool, sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER),
                PM_POOL_TAG);
            if (ext->ReadUrb == NULL) {
                status = STATUS_INSUFFICIENT_RESOURCES;
            }
        }
        if (NT_SUCCESS(status)) {
            ext->St.Started = 1;
            pmConfigure(ext);
            ext->Open = 1;
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
        ext->St.Started = 0;
        pmQuiesce(ext);
        irp->IoStatus.Status = STATUS_SUCCESS;
        IoSkipCurrentIrpStackLocation(irp);
        status = IoCallDriver(ext->Lower, irp);
        /* The bus settles a held WAIT_WAKE and every D-IRP at its own
         * REMOVE: each callback has run before the extension goes - but
         * the 9x defect this client reads (record 14 section 5.5) is a
         * WAIT_WAKE callback that never comes. So the wait is bounded, and
         * one still owed keeps the device object, and with it this
         * extension and the image, for that late callback, which finds
         * the device closed (task 36.34). */
        keep = !pmWaitZero(&ext->Outstanding, 5000);
        if (ext->LinkMade) {
            (VOID)IoDeleteSymbolicLink(&ext->Link);
        }
        IoDetachDevice(ext->Lower);
        if (keep) {
            DbgPrint("XHCIPM: REMOVE with %ld power callback(s) owed; "
                     "device object kept\n", ext->Outstanding);
            return status;
        }
        if (ext->ReadUrb != NULL) {
            ExFreePool(ext->ReadUrb);
            ext->ReadUrb = NULL;
        }
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
        ext->St.DevicePower = stack->Parameters.Power.State.DeviceState;
        (VOID)PoSetPowerState(dev, DevicePowerState,
                              stack->Parameters.Power.State);
    }
    PoStartNextPowerIrp(irp);
    return STATUS_SUCCESS;
}

/* The device request a system IRP asked for has completed: the system IRP
 * completes now (the policy owner's order); a SET's callback owns its
 * slot and lets it go. */
static VOID NTAPI pmSystemDevDone(PDEVICE_OBJECT dev, UCHAR minor,
                                  POWER_STATE state, PVOID ctx,
                                  PIO_STATUS_BLOCK io)
{
    PIRP sirp;
    PPM_EXT ext;

    UNREFERENCED_PARAMETER(dev);
    sirp = (PIRP)ctx;
    ext = (PPM_EXT)IoGetCurrentIrpStackLocation(sirp)
              ->DeviceObject->DeviceExtension;
    if (minor == IRP_MN_QUERY_POWER) {
        /* The device query's verdict is the system query's. */
        sirp->IoStatus.Status = io->Status;
    } else {
        pmPowerAccount(ext, state.DeviceState, io->Status, 0);
    }
    PoStartNextPowerIrp(sirp);
    IoCompleteRequest(sirp, IO_NO_INCREMENT);
    if (minor == IRP_MN_SET_POWER) {
        pmPowerRelease(ext);
    }
    InterlockedDecrement(&ext->Outstanding);
}

/* A system SET_POWER or QUERY_POWER the stack below has taken: as the
 * device's power policy owner, the client asks the device state it maps
 * to - D0 for S0, D3 below it - with the same minor, and completes the
 * system IRP when that one completes (task 36.34). A system SET finding a
 * device transition in flight is held and its
 * device request started when that one ends; a QUERY then is refused
 * BUSY. */
static NTSTATUS NTAPI pmSystemDone(PDEVICE_OBJECT dev, PIRP irp, PVOID ctx)
{
    PPM_EXT ext;
    PIO_STACK_LOCATION stack;
    POWER_STATE d;
    NTSTATUS status;
    ULONG answer;

    UNREFERENCED_PARAMETER(ctx);
    ext = (PPM_EXT)dev->DeviceExtension;
    stack = IoGetCurrentIrpStackLocation(irp);
    if (!NT_SUCCESS(irp->IoStatus.Status)) {
        PoStartNextPowerIrp(irp);
        return STATUS_SUCCESS;
    }
    d.DeviceState = (stack->Parameters.Power.State.SystemState ==
                     PowerSystemWorking)
                        ? PowerDeviceD0
                        : PowerDeviceD3;
    if (stack->MinorFunction == IRP_MN_SET_POWER) {
        ext->St.SystemPower = stack->Parameters.Power.State.SystemState;
    }
    /* Counted before the hold is visible, so a release that takes it at
     * once finds its count (pmPowerRelease drops it). */
    InterlockedIncrement(&ext->Outstanding);
    answer = pmPowerBegin(ext, stack->MinorFunction,
                          (DEVICE_POWER_STATE)d.DeviceState,
                          (stack->MinorFunction == IRP_MN_SET_POWER)
                              ? PM_DEFER_SIRP
                              : PM_DEFER_NONE,
                          irp);
    if (answer == PM_POWER_DEFERRED) {
        return STATUS_MORE_PROCESSING_REQUIRED;
    }
    InterlockedDecrement(&ext->Outstanding);
    if (answer == PM_POWER_LOCAL) {
        /* Already there (the 9x same-state request is never sent). */
        PoStartNextPowerIrp(irp);
        return STATUS_SUCCESS;
    }
    if (answer == PM_POWER_BUSY) {
        /* A query while a transition is in flight. */
        irp->IoStatus.Status = STATUS_DEVICE_BUSY;
        PoStartNextPowerIrp(irp);
        return STATUS_SUCCESS;
    }
    if (stack->MinorFunction == IRP_MN_SET_POWER) {
        status = pmPowerSend(ext, d.DeviceState, pmSystemDevDone, irp);
    } else {
        InterlockedIncrement(&ext->Outstanding);
        status = PoRequestPowerIrp(ext->Pdo, IRP_MN_QUERY_POWER, d,
                                   pmSystemDevDone, irp, NULL);
        if (status != STATUS_PENDING && !NT_SUCCESS(status)) {
            InterlockedDecrement(&ext->Outstanding);
        }
    }
    if (status != STATUS_PENDING && !NT_SUCCESS(status)) {
        if (stack->MinorFunction == IRP_MN_QUERY_POWER) {
            /* No device query follows, so the system query fails with
             * the request (task 36.34). */
            irp->IoStatus.Status = status;
        }
        PoStartNextPowerIrp(irp);
        return STATUS_SUCCESS;
    }
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static NTSTATUS NTAPI pmPower(PDEVICE_OBJECT dev, PIRP irp)
{
    PPM_EXT ext;
    PIO_STACK_LOCATION stack;
    KIRQL irql;
    ULONG track;

    ext = (PPM_EXT)dev->DeviceExtension;
    stack = IoGetCurrentIrpStackLocation(irp);
    if ((stack->MinorFunction == IRP_MN_SET_POWER ||
         stack->MinorFunction == IRP_MN_QUERY_POWER) &&
        stack->Parameters.Power.Type == SystemPowerState) {
        /* SET and QUERY alike (task 36.34): the device request follows the
         * system IRP's success below, and its verdict completes the
         * system IRP. */
        IoMarkIrpPending(irp);
        IoCopyCurrentIrpStackLocationToNext(irp);
        IoSetCompletionRoutine(irp, pmSystemDone, NULL, TRUE, TRUE, TRUE);
        (VOID)PoCallDriver(ext->Lower, irp);
        return STATUS_PENDING;
    }
    if (stack->MinorFunction == IRP_MN_SET_POWER) {
        IoCopyCurrentIrpStackLocationToNext(irp);
        IoSetCompletionRoutine(irp, pmSetPowerDone, NULL, TRUE, TRUE, TRUE);
        return PoCallDriver(ext->Lower, irp);
    }
    if (stack->MinorFunction == IRP_MN_WAIT_WAKE) {
        /* Tracked only when none is: this client requests one at a time
         * (pmRequestWake), so a second is never its own. */
        KeAcquireSpinLock(&ext->Lock, &irql);
        track = (ext->WakeIrp == NULL && ext->WakeRef == 0) ? 1UL : 0UL;
        if (track) {
            ext->WakeIrp = irp;
            ext->WakeRef = 1;
            ext->St.WakeHeld = 1;
        }
        KeReleaseSpinLock(&ext->Lock, irql);
        PoStartNextPowerIrp(irp);
        if (!track) {
            IoSkipCurrentIrpStackLocation(irp);
            return PoCallDriver(ext->Lower, irp);
        }
        IoCopyCurrentIrpStackLocationToNext(irp);
        IoSetCompletionRoutine(irp, pmWakeIrpDone, NULL, TRUE, TRUE, TRUE);
        return PoCallDriver(ext->Lower, irp);
    }
    PoStartNextPowerIrp(irp);
    IoSkipCurrentIrpStackLocation(irp);
    return PoCallDriver(ext->Lower, irp);
}
