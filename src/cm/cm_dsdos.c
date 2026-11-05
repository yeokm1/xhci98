/*
 * cm_dsdos.c - the ACPI-named tunnel-port source's kernel half (design
 * record 22 section 7.6.1, task 46.23), both builds:
 *
 *   - CmDsdFilterRead: the bridge filter on an AMD family's root port
 *     sends IOCTL_ACPI_EVAL_METHOD for _DSD down its own stack once its
 *     start has completed (ACPI is a filter in that stack), reads the
 *     answer with CmDsdRead (cm_dsd.c) and keeps the result and any claim
 *     in its registration entry (CmRegSetClaim);
 *   - CmDsdNhiName: the NHI's own ACPI name, DEVPKEY_Device_BiosDeviceName,
 *     read with IoGetDevicePropertyData.
 *
 * IoGetDevicePropertyData is found with CmRoutine (cm_oscnt.c, record 32
 * section 12.8.15.15): the amd64 image's MmGetSystemRoutineAddress; on
 * x86, whose one image loads on 98 SE and ME too and cannot import that,
 * the helper's channel. Where it is not found (98 SE and ME, 2000, XP,
 * XP x64, no helper) no NHI can read its name and the source is absent;
 * the override and the router-named source serve as before. The filter
 * evaluates _DSD on Vista and later alone (WDM 6.0, the routine's
 * systems), so a claim it keeps where the NHI has no name is matched by
 * none.
 *
 * IRQL: PASSIVE_LEVEL, no lock held.
 */

#include "cm.h"

#if defined(_WIN64)
#include <acpiioct.h>
#else
/* The Windows 2000 DDK keeps it beside its WDM headers, outside the
 * build's include path. */
#include <ddk\wdm\acpiioct.h>
#endif

/* cm_dsd.h's layout is the DDK's. */
XHCI_C_ASSERT(cm_dsd_sig, ACPI_EVAL_OUTPUT_BUFFER_SIGNATURE ==
                              CM_DSD_OUT_SIGNATURE);
XHCI_C_ASSERT(cm_dsd_hdr, FIELD_OFFSET(ACPI_EVAL_OUTPUT_BUFFER, Argument) ==
                              12);
XHCI_C_ASSERT(cm_dsd_arg, FIELD_OFFSET(ACPI_METHOD_ARGUMENT, Data) == 4);
XHCI_C_ASSERT(cm_dsd_int, ACPI_METHOD_ARGUMENT_INTEGER == CM_DSD_ARG_INTEGER);
XHCI_C_ASSERT(cm_dsd_str, ACPI_METHOD_ARGUMENT_STRING == CM_DSD_ARG_STRING);
XHCI_C_ASSERT(cm_dsd_buf, ACPI_METHOD_ARGUMENT_BUFFER == CM_DSD_ARG_BUFFER);
#if defined(_WIN64)
/* The Windows 2000 DDK's header has no package type. */
XHCI_C_ASSERT(cm_dsd_pkg, ACPI_METHOD_ARGUMENT_PACKAGE == CM_DSD_ARG_PACKAGE);
#endif
XHCI_C_ASSERT(cm_dsd_in, sizeof(ACPI_EVAL_INPUT_BUFFER) == 8);

/* IoGetDevicePropertyData's shape and DEVPROPKEY's layout, declared here:
 * neither build's headers reach the version that declares them. */
typedef struct _CM_DEVPROPKEY {
    GUID Fmtid;
    ULONG Pid;
} CM_DEVPROPKEY;

typedef NTSTATUS (NTAPI *CM_GETPROPDATA)(PDEVICE_OBJECT, const CM_DEVPROPKEY *,
                                         ULONG, ULONG, ULONG, PVOID, PULONG,
                                         PULONG);

#define CM_DEVPROP_TYPE_STRING  0x12UL

/* DEVPKEY_Device_BiosDeviceName, {540b947e-8b40-45bc-a8a2-6a0b894cbda2} 10
 * (read on a Strix Halo machine with Get-PnpDeviceProperty, 2026-11-04). */
static const CM_DEVPROPKEY cmDsdBiosName = {
    { 0x540B947EUL, 0x8B40, 0x45BC,
      { 0xA8, 0xA2, 0x6A, 0x0B, 0x89, 0x4C, 0xBD, 0xA2 } },
    10
};

static CM_GETPROPDATA cmDsdGetProp(VOID)
{
    return (CM_GETPROPDATA)CmRoutine(L"IoGetDevicePropertyData");
}

static NTSTATUS NTAPI cmDsdDone(PDEVICE_OBJECT device, PIRP irp,
                                PVOID context)
{
    UNREFERENCED_PARAMETER(device);
    UNREFERENCED_PARAMETER(irp);
    KeSetEvent((PKEVENT)context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

VOID CmDsdFilterRead(PCM_FILTER filter)
{
    PUCHAR buf;
    PIRP irp;
    PIO_STACK_LOCATION next;
    PDEVICE_OBJECT lower;
    ACPI_EVAL_INPUT_BUFFER in;
    KEVENT done;
    NTSTATUS status;
    ULONG got;
    ULONG rc;
    CM_DSD dsd;

    CmZero(&dsd, sizeof(dsd));
    lower = filter->Common.Lower;
    /* Vista and later (WDM 6.0), where the routine is exported: not
     * CmRoutine's answer, which a lookup failed for want of memory would
     * make NULL for this start alone. */
    if (lower == NULL || !IoIsWdmVersionAvailable(6, 0)) {
        return;
    }
    buf = (PUCHAR)CmAlloc(CM_DSD_ANSWER_MAX);
    if (buf == NULL) {
        CmRegSetClaim(filter, CM_DSD_BAD_HEADER, NULL, 0);
        return;
    }
    CmZero(buf, CM_DSD_ANSWER_MAX);
    CmZero(&in, sizeof(in));
    in.Signature = ACPI_EVAL_INPUT_BUFFER_SIGNATURE;
    in.MethodName[0] = '_';
    in.MethodName[1] = 'D';
    in.MethodName[2] = 'S';
    in.MethodName[3] = 'D';
    RtlCopyMemory(buf, &in, sizeof(in));
    status = STATUS_INSUFFICIENT_RESOURCES;
    got = 0;
    irp = IoAllocateIrp(lower->StackSize, FALSE);
    if (irp != NULL) {
        irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
        irp->IoStatus.Information = 0;
        /* METHOD_BUFFERED: one system buffer, the input in, the answer
         * out. */
        irp->AssociatedIrp.SystemBuffer = buf;
        next = IoGetNextIrpStackLocation(irp);
        next->MajorFunction = IRP_MJ_DEVICE_CONTROL;
        next->Parameters.DeviceIoControl.IoControlCode =
            IOCTL_ACPI_EVAL_METHOD;
        next->Parameters.DeviceIoControl.InputBufferLength = sizeof(in);
        next->Parameters.DeviceIoControl.OutputBufferLength =
            CM_DSD_ANSWER_MAX;
        KeInitializeEvent(&done, NotificationEvent, FALSE);
        CM_SET_COMPLETION_ALWAYS(irp, cmDsdDone, &done);
        status = IoCallDriver(lower, irp);
        if (status == STATUS_PENDING) {
            (VOID)KeWaitForSingleObject(&done, Executive, KernelMode, FALSE,
                                        NULL);
        }
        status = irp->IoStatus.Status;
        got = (ULONG)irp->IoStatus.Information;
        IoFreeIrp(irp);
    }
    if (!NT_SUCCESS(status) || got > CM_DSD_ANSWER_MAX) {
        /* No _DSD, a refused request, or an answer over the bound. */
        rc = CM_DSD_BAD_HEADER;
    } else {
        rc = CmDsdRead(buf, got, &dsd);
    }
    CmRegSetClaim(filter, rc, rc == CM_DSD_OK ? dsd.Ref : NULL,
                  dsd.HavePort ? dsd.Port : 0);
    CmFree(buf);
}

ULONG CmDsdNhiName(PCM_NHI nhi, char *out, ULONG cap)
{
    CM_GETPROPDATA get;
    USHORT text[CM_DSD_NAME_MAX + 1];
    ULONG need;
    ULONG type;
    NTSTATUS status;

    out[0] = 0;
    if (nhi->Vendor != 0x1022UL || nhi->Common.Pdo == NULL) {
        return 0;
    }
    get = cmDsdGetProp();
    if (get == NULL) {
        return 0;
    }
    need = 0;
    type = 0;
    CmZero(text, sizeof(text));
    /* LCID 0 (neutral), no flags. */
    status = get(nhi->Common.Pdo, &cmDsdBiosName, 0, 0,
                 sizeof(text) - sizeof(USHORT), text, &need, &type);
    if (!NT_SUCCESS(status) || type != CM_DEVPROP_TYPE_STRING) {
        return 0;
    }
    return CmDsdNormNameW(text, CM_DSD_NAME_MAX, out, cap);
}
