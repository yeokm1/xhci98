/*
 * pfw_nt.c - XHCIPFW.SYS, which closes every PCI-to-PCI bridge's
 * prefetchable window lying wholly above 4 GB before the PCI driver of
 * Windows 2000, XP or the 32-bit Vista, 7, 8, 8.1 and 10 reads the
 * bridges (roadmap-usb4.md tasks 44.49, 45.9 and 48.27, design record 32
 * section 11),
 * with the step of the 98 SE and ME VxD (..\pfw_scan.c) and its log.
 *
 * Two roles, by the system's version (PfwNtRole): on 2000 to 7 a
 * boot-start driver of the group System Reserved whose DriverEntry runs
 * the step once, with no device, no dispatch routine and no unload
 * routine (record 32 section 11.2); on 8, 8.1 and 10 an upper filter of
 * every PCI root bus that runs the step at the first IRP_MN_START_DEVICE
 * of the boot before passing it down, and passes everything else down
 * (sections 11.5.2 and 11.5.6). Neither elsewhere: on any other version
 * DriverEntry records Skipped = PFW_SKIP_SYSTEM and returns, having
 * touched nothing. It decides (..\pfw_ntdec.c) from the system's
 * version, Safe Mode, whether it runs at the boot drivers' time, and the
 * processors (on 2000 and XP one processor only); runs the step with every other processor held inside
 * KeIpiGenericCall (resolved at run time: a static import would keep the
 * image from loading on 2000) or, with one processor, at HIGH_LEVEL; and
 * writes record 32 section 6's values under its service key's Parameters,
 * with Skipped, Mode, Role, HoldTicks and TickHz (the hold's length in the
 * performance counter's ticks and its frequency, low dwords: no 64-bit
 * arithmetic in the driver). It always returns STATUS_SUCCESS.
 *
 * Configuration accesses: mechanism #1 through CF8h and CFCh (the HAL's
 * HalSetBusDataByOffset does not write a bridge's base and limit
 * registers). The qemu build also writes its lines to QEMU's debug
 * console (port 0E9h), after the step, never inside the hold.
 */

#include <ntddk.h>
#include "../pfw_scan.h"
#include "../pfw_ntdec.h"
#include "../../xhci_version.h"

#if defined(XHCI_FLAVOUR_QEMU)
#define PFW_FLAVOUR_NAME "XHCI98_FLAVOUR_QEMU"
#elif defined(XHCI_FLAVOUR_RELEASE)
#define PFW_FLAVOUR_NAME "XHCI98_FLAVOUR_RELEASE"
#else
#error "no build flavour: src\pfw\nt\build.cmd sets XHCI_FLAVOUR_RELEASE or _QEMU."
#endif

static volatile const char PfwFlavourMarker[] = PFW_FLAVOUR_NAME;
static const char PfwStamp[] = "XHCIPFW.SYS " XHCI_VER_STR;

/* Exported by every kernel from 2000 on and not declared by the 2000
 * DDK's headers: the Safe Mode flag (record 32 section 11.2). A data
 * export is the import slot, so this is a pointer to the variable. */
extern PULONG InitSafeBootMode;

/* KeIpiGenericCall (Server 2003 SP1 and later), found at run time. */
typedef ULONG_PTR (NTAPI *PFW_IPI_WORKER)(ULONG_PTR Argument);
typedef ULONG_PTR (NTAPI *PFW_IPI_CALL)(PFW_IPI_WORKER Worker,
                                        ULONG_PTR Context);

static PFW_RESULT PfwRes;
static volatile LONG PfwOwner;
static volatile LONG PfwDone;
/* The buses the step scans: 0 to 255 in the entry role, one root bus's
 * range in the filter role (record 32 section 11.5.2). */
static PFW_U32 PfwFirst = 0;
static PFW_U32 PfwLast = 255;
static LARGE_INTEGER PfwT0;
static LARGE_INTEGER PfwT1;
static LARGE_INTEGER PfwFreq;

PFW_U32 PfwCfgRead(PFW_U32 bus, PFW_U32 devfn, PFW_U32 off)
{
    WRITE_PORT_ULONG((PULONG)0xCF8, 0x80000000UL | ((bus & 0xFFUL) << 16) |
                     ((devfn & 0xFFUL) << 8) | (off & 0xFCUL));
    return READ_PORT_ULONG((PULONG)0xCFC);
}

void PfwCfgWrite(PFW_U32 bus, PFW_U32 devfn, PFW_U32 off, PFW_U32 val)
{
    WRITE_PORT_ULONG((PULONG)0xCF8, 0x80000000UL | ((bus & 0xFFUL) << 16) |
                     ((devfn & 0xFFUL) << 8) | (off & 0xFCUL));
    WRITE_PORT_ULONG((PULONG)0xCFC, val);
}

/* The step, timed (record 32 section 11.2: the hold's length logged). */
static void PfwRunTimed(void)
{
    PfwT0 = KeQueryPerformanceCounter(&PfwFreq);
    PfwRunBuses(&PfwRes, PfwFirst, PfwLast);
    PfwT1 = KeQueryPerformanceCounter(NULL);
}

/* Every processor enters at IPI_LEVEL; the first runs the step, the others
 * wait for it (record 32 section 11.2). */
static ULONG_PTR NTAPI PfwIpiWorker(ULONG_PTR Argument)
{
    UNREFERENCED_PARAMETER(Argument);
    if (InterlockedExchange((PLONG)&PfwOwner, 1) == 0) {
        PfwRunTimed();
        InterlockedExchange((PLONG)&PfwDone, 1);
    } else {
        while (PfwDone == 0) {
            ;
        }
    }
    return 0;
}

/* 0 at the boot drivers' time: the SOFTWARE hive is not loaded yet (the
 * loader brings only SYSTEM; the rest is loaded after the boot drivers'
 * DriverEntry). 1 when it is there, or when the open fails for any reason
 * but the name's absence (then the step must not act). Not the PCI
 * driver's object: on 8 it exists before this DriverEntry although the
 * bridges are read after it (record 32 section 11.2). */
static int PfwLate(void)
{
    UNICODE_STRING name;
    OBJECT_ATTRIBUTES oa;
    HANDLE key;
    NTSTATUS st;

    RtlInitUnicodeString(&name, L"\\Registry\\Machine\\SOFTWARE");
    InitializeObjectAttributes(&oa, &name, OBJ_CASE_INSENSITIVE |
                               OBJ_KERNEL_HANDLE, NULL, NULL);
    st = ZwOpenKey(&key, KEY_QUERY_VALUE, &oa);
    if (NT_SUCCESS(st)) {
        ZwClose(key);
        return 1;
    }
    return st == STATUS_OBJECT_NAME_NOT_FOUND ? 0 : 1;
}

static void PfwPutValue(HANDLE key, PCWSTR name, ULONG type, PVOID data,
                        ULONG bytes)
{
    UNICODE_STRING n;

    RtlInitUnicodeString(&n, name);
    (void)ZwSetValueKey(key, &n, 0, type, data, bytes);
}

static void PfwPutDword(HANDLE key, PCWSTR name, ULONG v)
{
    PfwPutValue(key, name, REG_DWORD, &v, sizeof(v));
}

/* The filter role's account of every root bus recorded since the load;
 * changed under PfwStepLock only. */
static PFW_NT_SUM PfwSum;

/* Record 32 section 6's values, and Skipped, Mode, HoldTicks and TickHz,
 * under the service key's Parameters; in the filter role these are the
 * last root bus's, and the account of every bus is written beside them. */
static void PfwLog(PUNICODE_STRING RegistryPath, ULONG skip, ULONG mode,
                   ULONG holdTicks, ULONG role)
{
    WCHAR path[300];
    WCHAR stamp[64];
    UNICODE_STRING p;
    OBJECT_ATTRIBUTES oa;
    HANDLE key;
    ULONG n;
    ULONG i;
    static const WCHAR sub[] = L"\\Parameters";

    n = RegistryPath->Length / sizeof(WCHAR);
    if (n == 0 ||
        n + sizeof(sub) / sizeof(WCHAR) > sizeof(path) / sizeof(WCHAR)) {
        return;
    }
    RtlCopyMemory(path, RegistryPath->Buffer, n * sizeof(WCHAR));
    RtlCopyMemory(path + n, sub, sizeof(sub));
    RtlInitUnicodeString(&p, path);
    InitializeObjectAttributes(&oa, &p, OBJ_CASE_INSENSITIVE |
                               OBJ_KERNEL_HANDLE, NULL, NULL);
    if (!NT_SUCCESS(ZwCreateKey(&key, KEY_SET_VALUE, &oa, 0, NULL,
                                REG_OPTION_NON_VOLATILE, NULL))) {
        return;
    }
    PfwPutDword(key, L"XhciPfwSkipped", skip);
    PfwPutDword(key, L"XhciPfwMode", mode);
    PfwPutDword(key, L"XhciPfwRole", role);
    PfwPutDword(key, L"XhciPfwBuses", (ULONG)(PfwFirst | (PfwLast << 8)));
    if (role == PFW_ROLE_FILTER) {
        PfwPutDword(key, L"XhciPfwRootBuses", PfwSum.RootBuses);
        PfwPutDword(key, L"XhciPfwSkippedAny", PfwSum.SkippedAny);
        PfwPutDword(key, L"XhciPfwResultAny", PfwSum.ResultAny);
        PfwPutDword(key, L"XhciPfwClosedAll", PfwSum.ClosedAll);
    }
    PfwPutDword(key, L"XhciPfwHoldTicks", holdTicks);
    PfwPutDword(key, L"XhciPfwTickHz", PfwFreq.LowPart);
    PfwPutDword(key, L"XhciPfwResult", PfwRes.Result);
    PfwPutDword(key, L"XhciPfwClosed", PfwRes.Closed);
    PfwPutDword(key, L"XhciPfwRefused", PfwRes.Refused);
    PfwPutDword(key, L"XhciPfwParked", PfwRes.Parked);
    PfwPutDword(key, L"XhciPfwAcross", PfwRes.Across);
    PfwPutDword(key, L"XhciPfwBarsInside", PfwRes.BarsInside);
    PfwPutDword(key, L"XhciPfwFunctions", PfwRes.Functions);
    PfwPutDword(key, L"XhciPfwXhciHigh", PfwRes.XhciHigh);
    PfwPutValue(key, L"XhciPfwBridges", REG_BINARY, PfwRes.Bridges,
                PfwRes.NBridges * sizeof(PFW_BRIDGE_REC));
    PfwPutValue(key, L"XhciPfwBars", REG_BINARY, PfwRes.Bars,
                PfwRes.NBars * sizeof(PFW_BAR_REC));
    PfwPutValue(key, L"XhciPfwXhci", REG_BINARY, PfwRes.Xhci,
                PfwRes.NXhci * sizeof(PFW_XHCI_REC));
    for (i = 0; PfwStamp[i] != 0 && i < sizeof(stamp) / sizeof(WCHAR) - 1;
         i++) {
        stamp[i] = (WCHAR)PfwStamp[i];
    }
    stamp[i] = 0;
    PfwPutValue(key, L"XhciPfwVersion", REG_SZ, stamp,
                (i + 1) * sizeof(WCHAR));
    ZwClose(key);
}

#if defined(XHCI_FLAVOUR_QEMU)
static void PfwPut(const char *s)
{
    while (*s) {
        WRITE_PORT_UCHAR((PUCHAR)0xE9, (UCHAR)*s++);
    }
}

static void PfwHex(ULONG v)
{
    static const char d[] = "0123456789abcdef";
    int i;

    for (i = 28; i >= 0; i -= 4) {
        WRITE_PORT_UCHAR((PUCHAR)0xE9, (UCHAR)d[(v >> i) & 0xF]);
    }
    WRITE_PORT_UCHAR((PUCHAR)0xE9, ' ');
}

static void PfwTrace(ULONG skip, ULONG mode, ULONG holdTicks)
{
    PfwPut("xhcipfw.sys mode/skip/holdticks/tickhz ");
    PfwHex(mode);
    PfwHex(skip);
    PfwHex(holdTicks);
    PfwHex(PfwFreq.LowPart);
    PfwPut("\nxhcipfw.sys result/functions/closed/refused/parked ");
    PfwHex(PfwRes.Result);
    PfwHex(PfwRes.Functions);
    PfwHex(PfwRes.Closed);
    PfwHex(PfwRes.Refused);
    PfwHex(PfwRes.Parked);
    PfwPut("\n");
}
#define PFW_TRACE(s, m, h) PfwTrace((s), (m), (h))
#else
#define PFW_TRACE(s, m, h) ((void)0)
#endif

/* The step, decided and run as record 32 section 11.2 says, then traced
 * and logged; extra is a reason the caller already has (the filter role's
 * "not attached"), which runs nothing. PASSIVE_LEVEL. */
static void PfwStep(PUNICODE_STRING RegistryPath, ULONG role, ULONG extra)
{
    ULONG major = 0;
    ULONG minor = 0;
    ULONG skip;
    ULONG holdTicks = 0;
    int mode = PFW_NT_SKIP;
    PFW_IPI_CALL ipi = NULL;
    UNICODE_STRING name;
    KIRQL old;

    PsGetVersion(&major, &minor, NULL, NULL);
    {
        PUCHAR z = (PUCHAR)&PfwRes;
        ULONG k;

        for (k = 0; k < sizeof(PfwRes); k++) {
            z[k] = 0;
        }
    }
    PfwOwner = 0;
    PfwDone = 0;
    PfwT0.QuadPart = 0;
    PfwT1.QuadPart = 0;
    if (extra != 0) {
        skip = extra;
    } else {
        if (major >= 6) {
            RtlInitUnicodeString(&name, L"KeIpiGenericCall");
            ipi = (PFW_IPI_CALL)MmGetSystemRoutineAddress(&name);
        }
        mode = PfwNtDecide(major, minor, (PFW_U32)*KeNumberProcessors,
                           *InitSafeBootMode != 0, PfwLate(), ipi != NULL,
                           &skip);
        if (mode == PFW_NT_RUN_UP) {
            KeRaiseIrql(HIGH_LEVEL, &old);
            PfwRunTimed();
            KeLowerIrql(old);
        } else if (mode == PFW_NT_RUN_IPI) {
            ipi(PfwIpiWorker, 0);
        }
        if (mode != PFW_NT_SKIP) {
            holdTicks = PfwT1.LowPart - PfwT0.LowPart;  /* modulo 2^32 */
        }
    }
    if (role == PFW_ROLE_FILTER) {
        PfwNtSumAdd(&PfwSum, skip, PfwRes.Result, PfwRes.Closed);
    }
    PFW_TRACE(skip, (ULONG)mode, holdTicks);
    PfwLog(RegistryPath, skip, (ULONG)mode, holdTicks, role);
}

/* ---- The filter role on 8, 8.1 and 10 (record 32 section 11.5.2) ------- */

#define PFW_LOCK_TAG 'fPhX'

typedef struct PFW_EXT {
    PDEVICE_OBJECT Lower;
    IO_REMOVE_LOCK Lock;
    volatile LONG Started;      /* this root bus's first start seen */
} PFW_EXT;

static WCHAR PfwRegBuf[300];
static UNICODE_STRING PfwRegPath;
static KEVENT PfwStepLock;      /* one step at a time (the globals above) */

/* A root bus this driver is named on but could not attach to: logged
 * now, since no start of that bus will reach this driver; that bus's
 * bridges are not closed (record 32 section 11.5.2). A record of its own,
 * counted in the account, under the steps' lock. */
static void PfwMarkUnattached(void)
{
    (void)KeWaitForSingleObject(&PfwStepLock, Executive, KernelMode, FALSE,
                                NULL);
    PfwFirst = 0;
    PfwLast = 0;
    PfwStep(&PfwRegPath, PFW_ROLE_FILTER, PFW_SKIP_UNATTACHED);
    KeSetEvent(&PfwStepLock, IO_NO_INCREMENT, FALSE);
}

/* AddDevice never fails: a filter's failure would fail the root bus. */
static NTSTATUS PfwAddDevice(PDRIVER_OBJECT Drv, PDEVICE_OBJECT Pdo)
{
    PDEVICE_OBJECT dev;
    PFW_EXT *x;

    if (!NT_SUCCESS(IoCreateDevice(Drv, sizeof(PFW_EXT), NULL,
                                   FILE_DEVICE_UNKNOWN, 0, FALSE, &dev))) {
        PfwMarkUnattached();
        return STATUS_SUCCESS;
    }
    x = (PFW_EXT *)dev->DeviceExtension;
    x->Started = 0;
    IoInitializeRemoveLock(&x->Lock, PFW_LOCK_TAG, 0, 0);
    x->Lower = IoAttachDeviceToDeviceStack(dev, Pdo);
    if (x->Lower == NULL) {
        IoDeleteDevice(dev);
        PfwMarkUnattached();
        return STATUS_SUCCESS;
    }
    dev->DeviceType = x->Lower->DeviceType;
    dev->Characteristics = x->Lower->Characteristics;
    dev->Flags |= x->Lower->Flags & (DO_BUFFERED_IO | DO_DIRECT_IO |
                                     DO_POWER_PAGABLE | DO_POWER_INRUSH);
    dev->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

static NTSTATUS PfwRefuse(PIRP Irp)
{
    Irp->IoStatus.Status = STATUS_DELETE_PENDING;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_DELETE_PENDING;
}

static NTSTATUS PfwPass(PDEVICE_OBJECT Dev, PIRP Irp)
{
    PFW_EXT *x = (PFW_EXT *)Dev->DeviceExtension;
    NTSTATUS st;

    if (!NT_SUCCESS(IoAcquireRemoveLock(&x->Lock, Irp))) {
        return PfwRefuse(Irp);
    }
    IoSkipCurrentIrpStackLocation(Irp);
    st = IoCallDriver(x->Lower, Irp);
    IoReleaseRemoveLock(&x->Lock, Irp);
    return st;
}

static NTSTATUS PfwPower(PDEVICE_OBJECT Dev, PIRP Irp)
{
    PFW_EXT *x = (PFW_EXT *)Dev->DeviceExtension;
    NTSTATUS st;

    PoStartNextPowerIrp(Irp);
    if (!NT_SUCCESS(IoAcquireRemoveLock(&x->Lock, Irp))) {
        return PfwRefuse(Irp);
    }
    IoSkipCurrentIrpStackLocation(Irp);
    st = PoCallDriver(x->Lower, Irp);
    IoReleaseRemoveLock(&x->Lock, Irp);
    return st;
}

static NTSTATUS PfwSyncDone(PDEVICE_OBJECT Dev, PIRP Irp, PVOID Ctx)
{
    UNREFERENCED_PARAMETER(Dev);
    UNREFERENCED_PARAMETER(Irp);
    KeSetEvent((PKEVENT)Ctx, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

/* This root bus's bus numbers from its start's resources (the raw list,
 * CmResourceTypeBusNumber): 1 found. */
static int PfwBusRange(PIO_STACK_LOCATION s, PFW_U32 *first, PFW_U32 *last)
{
    PCM_RESOURCE_LIST l = s->Parameters.StartDevice.AllocatedResources;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR d;
    ULONG i;

    if (l == NULL || l->Count == 0) {
        return 0;
    }
    d = l->List[0].PartialResourceList.PartialDescriptors;
    for (i = 0; i < l->List[0].PartialResourceList.Count; i++, d++) {
        if (d->Type == CmResourceTypeBusNumber && d->u.BusNumber.Length != 0 &&
            d->u.BusNumber.Start < 256) {
            *first = d->u.BusNumber.Start;
            *last = d->u.BusNumber.Start + d->u.BusNumber.Length - 1;
            if (*last > 255 || *last < *first) {
                *last = 255;
            }
            return 1;
        }
    }
    return 0;
}

/* A root bus's first start: the step over that bus's own range before
 * the start is passed down, so before the PCI driver enumerates it;
 * later starts of the same bus run nothing (record 32 section 11.5.2).
 * Steps of several root buses run one after the other. */
static void PfwFilterStart(PIO_STACK_LOCATION s, PFW_EXT *x)
{
    PFW_U32 first;
    PFW_U32 last;

    if (InterlockedExchange((PLONG)&x->Started, 1) != 0) {
        return;
    }
    (void)KeWaitForSingleObject(&PfwStepLock, Executive, KernelMode, FALSE,
                                NULL);
    if (PfwBusRange(s, &first, &last)) {
        PfwFirst = first;
        PfwLast = last;
        PfwStep(&PfwRegPath, PFW_ROLE_FILTER, 0);
    } else {
        PfwFirst = 0;
        PfwLast = 0;
        PfwStep(&PfwRegPath, PFW_ROLE_FILTER, PFW_SKIP_NORANGE);
    }
    KeSetEvent(&PfwStepLock, IO_NO_INCREMENT, FALSE);
}

/* IRP_MN_DEVICE_USAGE_NOTIFICATION: DO_POWER_PAGABLE as a filter keeps
 * it (set before the request goes down where the object above is
 * pageable or there is none, cleared after it where the one below is not;
 * the old state back on a failure). */
static NTSTATUS PfwUsage(PDEVICE_OBJECT Dev, PIRP Irp, PFW_EXT *x)
{
    KEVENT ev;
    NTSTATUS st;
    ULONG had = Dev->Flags & DO_POWER_PAGABLE;

    if (Dev->AttachedDevice == NULL ||
        (Dev->AttachedDevice->Flags & DO_POWER_PAGABLE) != 0) {
        Dev->Flags |= DO_POWER_PAGABLE;
    }
    KeInitializeEvent(&ev, NotificationEvent, FALSE);
    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp, PfwSyncDone, &ev, TRUE, TRUE, TRUE);
    st = IoCallDriver(x->Lower, Irp);
    if (st == STATUS_PENDING) {
        (void)KeWaitForSingleObject(&ev, Executive, KernelMode, FALSE, NULL);
    }
    st = Irp->IoStatus.Status;
    if (!NT_SUCCESS(st)) {
        Dev->Flags = (Dev->Flags & ~DO_POWER_PAGABLE) | had;
    } else if ((x->Lower->Flags & DO_POWER_PAGABLE) == 0) {
        Dev->Flags &= ~DO_POWER_PAGABLE;
    }
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return st;
}

static NTSTATUS PfwPnp(PDEVICE_OBJECT Dev, PIRP Irp)
{
    PFW_EXT *x = (PFW_EXT *)Dev->DeviceExtension;
    PIO_STACK_LOCATION s = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS st;

    if (!NT_SUCCESS(IoAcquireRemoveLock(&x->Lock, Irp))) {
        return PfwRefuse(Irp);
    }
    switch (s->MinorFunction) {
    case IRP_MN_START_DEVICE:
        PfwFilterStart(s, x);
        break;
    case IRP_MN_REMOVE_DEVICE:
        IoSkipCurrentIrpStackLocation(Irp);
        st = IoCallDriver(x->Lower, Irp);
        IoReleaseRemoveLockAndWait(&x->Lock, Irp);
        IoDetachDevice(x->Lower);
        IoDeleteDevice(Dev);
        return st;
    case IRP_MN_DEVICE_USAGE_NOTIFICATION:
        st = PfwUsage(Dev, Irp, x);
        IoReleaseRemoveLock(&x->Lock, Irp);
        return st;
    default:
        break;
    }
    IoSkipCurrentIrpStackLocation(Irp);
    st = IoCallDriver(x->Lower, Irp);
    IoReleaseRemoveLock(&x->Lock, Irp);
    return st;
}

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    ULONG major = 0;
    ULONG minor = 0;
    ULONG i;
    int role;

    (void)PfwFlavourMarker[0];
    PsGetVersion(&major, &minor, NULL, NULL);
    role = PfwNtRole(major, minor);
    if (role == PFW_ROLE_ENTRY) {
        PfwStep(RegistryPath, PFW_ROLE_ENTRY, 0);
        return STATUS_SUCCESS;
    }
    if (role != PFW_ROLE_FILTER) {
        PFW_TRACE(PFW_SKIP_SYSTEM, PFW_NT_SKIP, 0);
        PfwLog(RegistryPath, PFW_SKIP_SYSTEM, 0, 0, PFW_ROLE_NONE);
        return STATUS_SUCCESS;
    }
    /* The filter role: nothing touched here; the step at the first start
     * of a root bus (record 32 section 11.5.2). */
    KeInitializeEvent(&PfwStepLock, SynchronizationEvent, TRUE);
    PfwRegPath.Buffer = PfwRegBuf;
    PfwRegPath.Length = 0;
    PfwRegPath.MaximumLength = sizeof(PfwRegBuf);
    if (RegistryPath->Length <= sizeof(PfwRegBuf) - sizeof(WCHAR)) {
        RtlCopyMemory(PfwRegBuf, RegistryPath->Buffer, RegistryPath->Length);
        PfwRegPath.Length = RegistryPath->Length;
    }
    for (i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; i++) {
        DriverObject->MajorFunction[i] = PfwPass;
    }
    DriverObject->MajorFunction[IRP_MJ_PNP] = PfwPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = PfwPower;
    DriverObject->DriverExtension->AddDevice = PfwAddDevice;
    return STATUS_SUCCESS;
}
