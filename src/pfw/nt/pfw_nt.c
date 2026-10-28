/*
 * pfw_nt.c - XHCIPFW.SYS, the boot-start driver of the group System
 * Reserved that closes every PCI-to-PCI bridge's prefetchable window lying
 * wholly above 4 GB before Windows 2000 or the 32-bit Vista, 7, 8, 8.1 and
 * 10 PCI driver reads the bridges (roadmap-usb4.md task 44.49, design
 * record 32 section 11), with the step of the 98 SE and ME VxD
 * (..\pfw_scan.c) and its log.
 *
 * DriverEntry only, once, at a start: no device, no dispatch routine and
 * no unload routine, so the driver is never stopped and started again
 * (record 32 section 11.2). It decides (..\pfw_ntdec.c) from the system's
 * version, Safe Mode, whether it runs at the boot drivers' time, and the
 * processors; runs the step with every other processor held inside
 * KeIpiGenericCall (resolved at run time: a static import would keep the
 * image from loading on 2000) or, with one processor, at HIGH_LEVEL; and
 * writes record 32 section 6's values under its service key's Parameters,
 * with Skipped, Mode, HoldTicks and TickHz (the hold's length in the
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
    PfwRun(&PfwRes);
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

/* Record 32 section 6's values, and Skipped, Mode, HoldTicks and TickHz,
 * under the service key's Parameters. */
static void PfwLog(PUNICODE_STRING RegistryPath, ULONG skip, ULONG mode,
                   ULONG holdTicks)
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
    if (n + sizeof(sub) / sizeof(WCHAR) > sizeof(path) / sizeof(WCHAR)) {
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

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    ULONG major = 0;
    ULONG minor = 0;
    ULONG skip;
    ULONG holdTicks = 0;
    int mode;
    PFW_IPI_CALL ipi = NULL;
    UNICODE_STRING name;
    KIRQL old;

    UNREFERENCED_PARAMETER(DriverObject);
    (void)PfwFlavourMarker[0];
    PsGetVersion(&major, &minor, NULL, NULL);
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
    PFW_TRACE(skip, (ULONG)mode, holdTicks);
    PfwLog(RegistryPath, skip, (ULONG)mode, holdTicks);
    return STATUS_SUCCESS;
}
