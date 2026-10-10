/*
 * hcd_msi86.c - message-signalled interrupts on Vista and 7, the kernel
 * half that is not the connection itself (design record 15 section 4.3;
 * design record 18; roadmap-hcd.md tasks 36.15 and 36.16).
 *
 *   - Where IoConnectInterruptEx and IoDisconnectInterruptEx come from:
 *     amd64 asks MmGetSystemRoutineAddress; the x86 image, which also loads
 *     on Windows 98 and so may import neither, asks the NT 6-only helper
 *     xhcimsi.sys over its named device (record 18 section 3). The pair is
 *     handed to hcd_ctl.c's hcdMsgConnect, the one seam.
 *   - The helper's binding per controller FDO (record 18 section 4.1).
 *   - IRP_MN_FILTER_RESOURCE_REQUIREMENTS (record 18 section 4.2): the
 *     checked walk, the class, and the strip that keeps only the line
 *     configurations when x86 has no helper bound or the mode is 1.
 *   - The mode cache the filter reads, filled at AddDevice and at each
 *     start, both system-thread contexts (section 4.2).
 *   - The qemu flavour's checksummed trace (record 18 section 9).
 *
 * Nothing here runs below NT 6: every entry tests IoIsWdmVersionAvailable
 * (6, 0) first, so Windows 98, ME, 2000 and XP pass the filter IRP down
 * untouched, as before.
 *
 * IRQL: PASSIVE_LEVEL throughout, in the PnP manager's threads (AddDevice,
 * IRP_MN_START_DEVICE and IRP_MN_REMOVE_DEVICE are system-thread
 * contexts; IRP_MN_FILTER_RESOURCE_REQUIREMENTS reads no registry), except
 * hcdMsi86Done, a completion routine (<= DISPATCH_LEVEL).
 */

#include "hcd.h"
#include "xhci_version.h"
#include "xhci_dbg.h"

/* Record 18 section 4.2: the strip is enabled once R1 and R2 have been
 * read on Vista and 7, x86 and x64 (section 11); the qemu flavour's
 * XhciMsi86NoStrip turns it off for those readings. */
#define HCD_MSI_STRIP_DEFAULT       0UL

/* The DDK's layouts are the pure walk's (xhci_msi.h). */
XHCI_C_ASSERT(msi_rrl_list,
              FIELD_OFFSET(IO_RESOURCE_REQUIREMENTS_LIST, List) ==
                  XHCI_MSI_RRL_HEADER);
XHCI_C_ASSERT(msi_rrl_alternatives,
              FIELD_OFFSET(IO_RESOURCE_REQUIREMENTS_LIST, AlternativeLists) ==
                  XHCI_MSI_RRL_ALTERNATIVES);
XHCI_C_ASSERT(msi_rl_descriptors,
              FIELD_OFFSET(IO_RESOURCE_LIST, Descriptors) ==
                  XHCI_MSI_RL_HEADER);
XHCI_C_ASSERT(msi_rl_count,
              FIELD_OFFSET(IO_RESOURCE_LIST, Count) == XHCI_MSI_RL_COUNT);
XHCI_C_ASSERT(msi_rd_size, sizeof(IO_RESOURCE_DESCRIPTOR) == XHCI_MSI_RD_SIZE);
XHCI_C_ASSERT(msi_rd_flags,
              FIELD_OFFSET(IO_RESOURCE_DESCRIPTOR, Flags) == XHCI_MSI_RD_FLAGS);
XHCI_C_ASSERT(msi_rd_u,
              FIELD_OFFSET(IO_RESOURCE_DESCRIPTOR, u) == XHCI_MSI_RD_U);

/* The restated WDK 7.1 structures (hcd.h), at WDK 7.1's sizes and offsets
 * for each architecture: x86 the FullySpecified member 0x2C, the
 * MessageBased member at 4, a message entry 0x28 with its interrupt object
 * at 0x0C; amd64 0x48, 8, 0x30 and 0x10. */
#if defined(_WIN64)
XHCI_C_ASSERT(msi_connect_size, sizeof(HCD_IO_CONNECT_PARAMETERS) == 0x50);
XHCI_C_ASSERT(msi_connect_member,
              FIELD_OFFSET(HCD_IO_CONNECT_PARAMETERS, u) == 8);
XHCI_C_ASSERT(msi_connect_fallback,
              FIELD_OFFSET(HCD_IO_CONNECT_MESSAGE_BASED,
                           FallBackServiceRoutine) == 0x30);
XHCI_C_ASSERT(msi_entry_size, sizeof(HCD_IO_MESSAGE_INFO_ENTRY) == 0x30);
XHCI_C_ASSERT(msi_entry_object,
              FIELD_OFFSET(HCD_IO_MESSAGE_INFO_ENTRY, InterruptObject) ==
                  0x10);
XHCI_C_ASSERT(msi_disconnect_size,
              sizeof(HCD_IO_DISCONNECT_PARAMETERS) == 0x10);
#else
XHCI_C_ASSERT(msi_connect_size, sizeof(HCD_IO_CONNECT_PARAMETERS) == 0x30);
XHCI_C_ASSERT(msi_connect_member,
              FIELD_OFFSET(HCD_IO_CONNECT_PARAMETERS, u) == 4);
XHCI_C_ASSERT(msi_connect_fallback,
              FIELD_OFFSET(HCD_IO_CONNECT_MESSAGE_BASED,
                           FallBackServiceRoutine) == 0x18);
XHCI_C_ASSERT(msi_entry_size, sizeof(HCD_IO_MESSAGE_INFO_ENTRY) == 0x28);
XHCI_C_ASSERT(msi_entry_object,
              FIELD_OFFSET(HCD_IO_MESSAGE_INFO_ENTRY, InterruptObject) ==
                  0x0C);
XHCI_C_ASSERT(msi_disconnect_size,
              sizeof(HCD_IO_DISCONNECT_PARAMETERS) == 0x08);
#endif
XHCI_C_ASSERT(msi_info_count,
              FIELD_OFFSET(HCD_IO_MESSAGE_INFO, MessageCount) == 4);
XHCI_C_ASSERT(msi_info_entries,
              FIELD_OFFSET(HCD_IO_MESSAGE_INFO, MessageInfo) == 8);

static ULONG hcdMsiNt6(VOID)
{
    return IoIsWdmVersionAvailable(6, 0) ? 1UL : 0UL;
}

/* XHCI_VER packed one byte a part, for the helper's log and ours. */
static ULONG hcdMsiVersion(VOID)
{
    return XhciMsi86PackVersion(XHCI_VER_STR);
}

/* ---- the trace (record 18 section 9) ---------------------------------- */

#if defined(XHCI_FLAVOUR_QEMU)
/* The records' running number, across every FDO of this image. */
static LONG hcdMsiTraceNumber;

static VOID hcdMsiTrace(PHCD_CONTROLLER hc, char kind, const ULONG *fields,
                        ULONG count)
{
    char line[XHCI_MSI_TRACE_MAX];
    ULONG number;

    /* Numbered and emitted in one step per record; records from two FDOs
     * at once may still reach the channel out of number order, which the
     * validator reports as a failed capture to retake, never as a pass. */
    number = (ULONG)InterlockedIncrement(&hcdMsiTraceNumber);
    if ((number & 0xFFFFUL) == 0) {
        number = (ULONG)InterlockedIncrement(&hcdMsiTraceNumber);
    }
    (VOID)XhciMsiTraceFormat(line, kind, hc->FdoSerial,
                             hc->MsiFilter.Sequence, number, fields, count);
    XHCI_DBG_TEXT(line);
}

/* A walked list, B, C per configuration, D per descriptor, then E with the
 * D records written: `which` is 'R' (received) or 'T' (returned). */
static VOID hcdMsiTraceList(PHCD_CONTROLLER hc, ULONG which,
                            const UCHAR *list, const XHCI_MSI_WALK *walk,
                            ULONG listClass, ULONG outcome)
{
    ULONG f[XHCI_MSI_TRACE_FIELDS];
    const UCHAR *cfg;
    const UCHAR *d;
    ULONG count;
    ULONG c;
    ULONG n;
    ULONG k;
    ULONG written;

    f[0] = which;
    f[1] = walk->ListSize;
    f[2] = walk->Lists;
    f[3] = walk->Verdict;
    f[4] = walk->FailList;
    f[5] = listClass;
    f[6] = outcome;
    hcdMsiTrace(hc, XHCI_MSI_TRACE_BEGIN, f, 7);
    written = 0;
    if (walk->Verdict == XHCI_MSI_WALK_OK) {
        cfg = list + XHCI_MSI_RRL_HEADER;
        for (c = 0; c < walk->Lists; c++) {
            count = *(const ULONG *)(cfg + XHCI_MSI_RL_COUNT);
            f[0] = which;
            f[1] = (c << 16) | walk->Lists;
            f[2] = count;
            f[3] = *(const ULONG *)cfg;
            hcdMsiTrace(hc, XHCI_MSI_TRACE_CONFIG, f, 4);
            for (n = 0; n < count; n++) {
                d = cfg + XHCI_MSI_RL_HEADER + n * XHCI_MSI_RD_SIZE;
                f[0] = which;
                f[1] = (c << 16) | n;
                f[2] = (ULONG)d[XHCI_MSI_RD_OPTION] |
                       ((ULONG)d[XHCI_MSI_RD_TYPE] << 8) |
                       ((ULONG)d[XHCI_MSI_RD_SHARE] << 16);
                f[3] = (ULONG)*(const USHORT *)(d + XHCI_MSI_RD_FLAGS);
                for (k = 0; k < 6; k++) {
                    f[4 + k] = *(const ULONG *)(d + XHCI_MSI_RD_U + 4 * k);
                }
                hcdMsiTrace(hc, XHCI_MSI_TRACE_DESCRIPTOR, f, 10);
                written++;
            }
            cfg += XHCI_MSI_RL_HEADER + count * XHCI_MSI_RD_SIZE;
        }
    }
    f[0] = which;
    f[1] = written;
    f[2] = walk->Descriptors;
    hcdMsiTrace(hc, XHCI_MSI_TRACE_END, f, 3);
}
#endif

VOID HcdMsiTraceLife(PHCD_CONTROLLER hc, ULONG event, ULONG a, ULONG b)
{
#if defined(XHCI_FLAVOUR_QEMU)
    ULONG f[3];

    f[0] = event;
    f[1] = a;
    f[2] = b;
    hcdMsiTrace(hc, XHCI_MSI_TRACE_LIFE, f, 3);
#else
    UNREFERENCED_PARAMETER(hc);
    UNREFERENCED_PARAMETER(event);
    UNREFERENCED_PARAMETER(a);
    UNREFERENCED_PARAMETER(b);
#endif
}

/* The start-decision event, before a failing start returns (record 18
 * section 4.3): grant 0 none, 1 line, 2 message. */
VOID HcdMsiTraceStart(PHCD_CONTROLLER hc, ULONG grant, NTSTATUS status)
{
#if defined(XHCI_FLAVOUR_QEMU)
    ULONG f[8];

    f[0] = hc->PollValues.Mode;
    f[1] = grant;
    f[2] = hc->Msi86Bind.State;
    f[3] = hc->MsiFilter.Class;
    f[4] = hc->MsiFilter.Outcome;
    f[5] = hc->PollDelivery;
    f[6] = hc->PollReason;
    f[7] = (ULONG)status;
    hcdMsiTrace(hc, XHCI_MSI_TRACE_START, f, 8);
#else
    UNREFERENCED_PARAMETER(hc);
    UNREFERENCED_PARAMETER(grant);
    UNREFERENCED_PARAMETER(status);
#endif
}

/* ---- the counters ------------------------------------------------------ */

/* The record into the counter block, which the start zeroes (record 18
 * section 9). IRQL: PASSIVE_LEVEL. */
VOID HcdMsiCountersStart(PHCD_CONTROLLER hc)
{
    PXHCIHC_COUNTERS c;

    c = &hc->Counters;
    c->MsiRoutines = hc->MsgRoutines;
    c->MsiMessages = hc->MsgConnected;
    c->MsiCapability = hc->MsgCapability;
    c->MsiConnectStatus = (ULONG)hc->MsgConnectStatus;
    c->MsiDescriptors = hc->MsgDescriptors;
    c->Msi86State = hc->Msi86Bind.State;
    c->Msi86Looks = hc->Msi86Bind.Looks;
    c->Msi86Status = (ULONG)hc->Msi86Status;
    c->Msi86Refusal = hc->Msi86Refusal;
    c->Msi86HelperVersion = hc->Msi86HelperVersion;
    c->MsiFilterSeq = hc->MsiFilter.Sequence;
    c->MsiFilterClass = hc->MsiFilter.Class;
    c->MsiFilterOutcome = hc->MsiFilter.Outcome;
    c->MsiFilterMode = hc->FilterModeFound ? hc->FilterModeRaw
                                           : XHCI_POLL_NOT_FOUND;
    c->MsiFilterLists = hc->FilterWalk.Lists;
    c->MsiFilterMessages = hc->FilterClassify.MessageMembers;
    c->MsiFilterLines = hc->FilterClassify.LineMembers;
    c->MsiFilterStripped = hc->FilterStripped;
}

/* ---- the mode cache (record 18 section 4.2) ---------------------------- */

static VOID hcdMsiReadSwitches(PHCD_CONTROLLER hc)
{
#if defined(XHCI_FLAVOUR_QEMU)
    ULONG value;

    value = 0;
    hc->QemuNoStrip =
        (NT_SUCCESS(HcdReadValueDword(hc, HCD_VALUE_QEMU_NO_STRIP, &value)) &&
         value == 1) ? 1UL : 0UL;
    value = 0;
    hc->QemuInjNoLine =
        (NT_SUCCESS(HcdReadValueDword(hc, HCD_VALUE_QEMU_NO_LINE, &value)) &&
         value == 1) ? 1UL : 0UL;
#else
    hc->QemuNoStrip = 0;
    hc->QemuInjNoLine = 0;
#endif
}

/* AddDevice, a system thread: the records' first state, and on NT 6 the
 * mode as the registry holds it now, for this FDO's first filter. */
VOID HcdMsiAddDevice(PHCD_CONTROLLER hc)
{
    ULONG value;

    XhciMsiFilterInit(&hc->MsiFilter);
    XhciMsi86BindInit(&hc->Msi86Bind);
    hc->ModeCacheRaw = 0;
    hc->ModeCacheFound = 0;
    hc->ModeCacheFrom = HCD_MSI_FROM_NONE;
    if (!hcdMsiNt6()) {
        return;
    }
    value = 0;
    if (NT_SUCCESS(HcdReadValueDword(hc, L"XhciInterruptMode", &value))) {
        hc->ModeCacheRaw = value;
        hc->ModeCacheFound = 1;
    }
    hc->ModeCacheFrom = HCD_MSI_FROM_ADDDEVICE;
    hcdMsiReadSwitches(hc);
}

/* The start's own reading, a system thread too, kept for a later filter
 * on this FDO (a rebalance's). The filter's snapshot is not touched. */
VOID HcdMsiStartValues(PHCD_CONTROLLER hc, ULONG modeFound, ULONG mode)
{
    if (!hcdMsiNt6()) {
        return;
    }
    hc->ModeCacheRaw = modeFound ? mode : 0UL;
    hc->ModeCacheFound = modeFound;
    hc->ModeCacheFrom = HCD_MSI_FROM_START;
    hcdMsiReadSwitches(hc);
}

/* ---- the x86 helper (record 18 sections 3 and 4.1) --------------------- */

#if !defined(_WIN64)
static NTSTATUS NTAPI hcdMsi86Done(PDEVICE_OBJECT DeviceObject, PIRP Irp,
                                   PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);

    (VOID)KeSetEvent((PKEVENT)Context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

/* One look: open the helper's device, send the query, judge the answer,
 * and step the binding with the result. */
static VOID hcdMsi86Look(PHCD_CONTROLLER hc)
{
    UNICODE_STRING name;
    PFILE_OBJECT file;
    PDEVICE_OBJECT device;
    PIRP irp;
    PIO_STACK_LOCATION next;
    KEVENT done;
    NTSTATUS status;
    ULONG information;
    ULONG verdict;
    ULONG event;
    ULONG act;
    PUCHAR p;
    ULONG i;

    file = NULL;
    hc->Msi86Refusal = 0;
    RtlInitUnicodeString(&name, XHCI_MSI86_DEVICE_NAME);
    status = IoGetDeviceObjectPointer(&name, FILE_READ_DATA, &file, &device);
    if (!NT_SUCCESS(status)) {
        hc->Msi86Status = status;
        event = XHCI_MSI86_EV_LOOK_ABSENT;
        goto step;
    }
    irp = IoAllocateIrp(device->StackSize, FALSE);
    if (irp == NULL) {
        hc->Msi86Status = STATUS_INSUFFICIENT_RESOURCES;
        event = XHCI_MSI86_EV_LOOK_FAILED;
        goto step;
    }
    p = (PUCHAR)&hc->Msi86Query;
    for (i = 0; i < sizeof(hc->Msi86Query); i++) {
        p[i] = 0;
    }
    hc->Msi86Query.Size = sizeof(XHCI_MSI86_QUERY);
    hc->Msi86Query.CallerMajor = XHCI_MSI86_MAJOR;
    hc->Msi86Query.CallerMinor = XHCI_MSI86_MINOR;
    hc->Msi86Query.CallerVersion = hcdMsiVersion();
    irp->RequestorMode = KernelMode;
    irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    irp->IoStatus.Information = 0;
    next = IoGetNextIrpStackLocation(irp);
    next->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
    next->Parameters.DeviceIoControl.IoControlCode =
        XHCI_MSI86_IOCTL_QUERY_V1;
    next->Parameters.DeviceIoControl.Type3InputBuffer = &hc->Msi86Query;
    next->Parameters.DeviceIoControl.InputBufferLength =
        sizeof(XHCI_MSI86_QUERY);
    next->Parameters.DeviceIoControl.OutputBufferLength =
        sizeof(XHCI_MSI86_QUERY);
    next->FileObject = file;
    KeInitializeEvent(&done, NotificationEvent, FALSE);
    HCD_SET_COMPLETION_ALWAYS(irp, hcdMsi86Done, &done);
    if (IoCallDriver(device, irp) == STATUS_PENDING) {
        (VOID)KeWaitForSingleObject(&done, Executive, KernelMode, FALSE,
                                    NULL);
    }
    status = irp->IoStatus.Status;
    information = (ULONG)irp->IoStatus.Information;
    IoFreeIrp(irp);
    hc->Msi86Status = status;
    verdict = XhciMsi86Accept(NT_SUCCESS(status) ? 1UL : 0UL, information,
                              sizeof(XHCI_MSI86_QUERY), &hc->Msi86Query);
    if (verdict == XHCI_MSI86_ACCEPTED) {
        event = XHCI_MSI86_EV_LOOK_ACCEPTED;
    } else {
        hc->Msi86Refusal = verdict;
        event = XHCI_MSI86_EV_LOOK_REFUSED;
    }

step:
    act = XhciMsi86BindStep(&hc->Msi86Bind, event);
    HcdMsiTraceLife(hc, HCD_MSI_LIFE_LOOK, event, (ULONG)hc->Msi86Status);
    /* The refusal's own event, with the acceptance test that failed, before
     * the look's reference goes (Codex review of the 36.15 code, round 1). */
    if (event == XHCI_MSI86_EV_LOOK_REFUSED) {
        HcdMsiTraceLife(hc, HCD_MSI_LIFE_REFUSE, hc->Msi86Refusal,
                        (ULONG)hc->Msi86Status);
    }
    if ((act & XHCI_MSI86_ACT_DROP_LOOK) != 0 && file != NULL) {
        ObDereferenceObject(file);
        HcdMsiTraceLife(hc, HCD_MSI_LIFE_DROP, 0, 0);
        return;
    }
    if (event == XHCI_MSI86_EV_LOOK_ACCEPTED) {
        hc->Msi86File = file;
        hc->Msi86ConnectEx = hc->Msi86Query.ConnectEx;
        hc->Msi86DisconnectEx = hc->Msi86Query.DisconnectEx;
        hc->Msi86HelperVersion = hc->Msi86Query.HelperVersion;
        hc->Msi86HelperMinor = hc->Msi86Query.HelperMinor;
        hc->Msi86Flags = hc->Msi86Query.Flags;
        HcdMsiTraceLife(hc, HCD_MSI_LIFE_BIND, hc->Msi86HelperVersion,
                        hc->Msi86Flags);
    }
}

static VOID hcdMsi86Event(PHCD_CONTROLLER hc, ULONG event)
{
    if ((XhciMsi86BindStep(&hc->Msi86Bind, event) & XHCI_MSI86_ACT_LOOK) !=
        0) {
        hcdMsi86Look(hc);
    }
}
#endif /* !_WIN64 */

/* The start's look on x86 while not bound (record 18 section 4.1). */
VOID HcdMsiStartLook(PHCD_CONTROLLER hc)
{
#if !defined(_WIN64)
    if (hcdMsiNt6()) {
        hcdMsi86Event(hc, XHCI_MSI86_EV_START);
    }
#else
    UNREFERENCED_PARAMETER(hc);
#endif
}

/* The remove, after the stop's release has disconnected: the reference
 * held for a BOUND answer dropped, exactly once (record 18 section 3.3). */
VOID HcdMsiRemove(PHCD_CONTROLLER hc)
{
#if !defined(_WIN64)
    ULONG act;

    act = XhciMsi86BindStep(&hc->Msi86Bind, XHCI_MSI86_EV_REMOVE);
    if ((act & XHCI_MSI86_ACT_DROP) != 0 && hc->Msi86File != NULL) {
        ObDereferenceObject(hc->Msi86File);
        HcdMsiTraceLife(hc, HCD_MSI_LIFE_DROP, 1, 0);
    }
    hc->Msi86File = NULL;
    hc->Msi86ConnectEx = NULL;
    hc->Msi86DisconnectEx = NULL;
#endif
    HcdMsiTraceLife(hc, HCD_MSI_LIFE_REMOVE, 0, 0);
}

/*
 * The IoConnectInterruptEx pair for the start, by architecture (record 15
 * section 4.3): HCD_MSI_ROUTINES_* and the pair, NULL when not found.
 */
ULONG HcdMsiRoutines(PHCD_CONTROLLER hc, PVOID *connect, PVOID *disconnect)
{
    *connect = NULL;
    *disconnect = NULL;
    if (!hcdMsiNt6()) {
        return HCD_MSI_ROUTINES_NONE;
    }
#if defined(_WIN64)
    {
        UNICODE_STRING name;

        UNREFERENCED_PARAMETER(hc);
        RtlInitUnicodeString(&name, L"IoConnectInterruptEx");
        *connect = MmGetSystemRoutineAddress(&name);
        RtlInitUnicodeString(&name, L"IoDisconnectInterruptEx");
        *disconnect = MmGetSystemRoutineAddress(&name);
        if (*connect == NULL || *disconnect == NULL) {
            *connect = NULL;
            *disconnect = NULL;
            return HCD_MSI_ROUTINES_NOT_FOUND;
        }
        return HCD_MSI_ROUTINES_SYSTEM;
    }
#else
    if (hc->Msi86Bind.State != XHCI_MSI86_STATE_BOUND) {
        return HCD_MSI_ROUTINES_NOT_FOUND;
    }
    *connect = hc->Msi86ConnectEx;
    *disconnect = hc->Msi86DisconnectEx;
    return HCD_MSI_ROUTINES_HELPER;
#endif
}

/* XhciPollDecide's noLineOffered (record 18 section 4.3): x86 alone, no
 * helper bound, and this FDO's latest filter classified its list NO_LINE. */
ULONG HcdMsiNoLineOffered(PHCD_CONTROLLER hc)
{
#if !defined(_WIN64)
    return (hcdMsiNt6() &&
            hc->Msi86Bind.State != XHCI_MSI86_STATE_BOUND &&
            hc->MsiFilter.Class == XHCI_MSI_CLASS_NO_LINE)
               ? 1UL
               : 0UL;
#else
    UNREFERENCED_PARAMETER(hc);
    return 0;
#endif
}

/* ---- the requirements filter (record 18 section 4.2) ------------------- */

/* The bounded snapshot of a walked list's first descriptors. */
static VOID hcdMsiSnap(PHCD_CONTROLLER hc, const UCHAR *list,
                       const XHCI_MSI_WALK *walk)
{
    const UCHAR *cfg;
    const UCHAR *d;
    ULONG count;
    ULONG c;
    ULONG n;
    ULONG k;
    ULONG s;

    s = 0;
    hc->FilterSnapCount = walk->Descriptors;
    cfg = list + XHCI_MSI_RRL_HEADER;
    for (c = 0; c < walk->Lists; c++) {
        count = *(const ULONG *)(cfg + XHCI_MSI_RL_COUNT);
        for (n = 0; n < count && s < 12; n++) {
            d = cfg + XHCI_MSI_RL_HEADER + n * XHCI_MSI_RD_SIZE;
            hc->FilterSnap[s][0] = (c << 16) | n;
            hc->FilterSnap[s][1] = (ULONG)d[XHCI_MSI_RD_OPTION] |
                                   ((ULONG)d[XHCI_MSI_RD_TYPE] << 8) |
                                   ((ULONG)d[XHCI_MSI_RD_SHARE] << 16);
            hc->FilterSnap[s][2] =
                (ULONG)*(const USHORT *)(d + XHCI_MSI_RD_FLAGS);
            for (k = 0; k < 6; k++) {
                hc->FilterSnap[s][3 + k] =
                    *(const ULONG *)(d + XHCI_MSI_RD_U + 4 * k);
            }
            s++;
        }
        cfg += XHCI_MSI_RL_HEADER + count * XHCI_MSI_RD_SIZE;
    }
}

/* Replaces the list at Information with `size` bytes `write` fills; the
 * old list freed. Returns the new list, NULL when no memory. */
static PUCHAR hcdMsiReplace(PIRP irp, PUCHAR list, ULONG size, ULONG strip,
                            const XHCI_MSI_WALK *walk,
                            const XHCI_MSI_CLASSIFY *classify)
{
    PUCHAR fresh;
    ULONG written;

    fresh = (PUCHAR)HcdPoolAllocRequirements(size);
    if (fresh == NULL) {
        return NULL;
    }
    written = strip ? XhciMsiStrip(list, walk, classify, fresh, size)
                    : XhciMsiInjNoLine(list, walk, fresh, size);
    if (written != size) {
        HcdPoolFreeForeign(fresh);
        return NULL;
    }
    HcdPoolFreeForeign(list);
    irp->IoStatus.Information = (ULONG_PTR)fresh;
    return fresh;
}

NTSTATUS HcdMsiFilter(PHCD_CONTROLLER hc, PIRP irp)
{
    NTSTATUS status;
    PUCHAR list;
    PUCHAR fresh;
    XHCI_POLL_VALUES values;
    ULONG asked;
    ULONG enabled;
    ULONG outcome;
    ULONG size;
    ULONG replaced;

    if (!hcdMsiNt6()) {
        return HcdPassDown(hc, irp);
    }
    /* Step 0: the class is UNKNOWN before anything can return early. */
    XhciMsiFilterBegin(&hc->MsiFilter);
    hc->FilterStripped = 0;
    hc->FilterWalk.Verdict = XHCI_MSI_WALK_OK;
    hc->FilterWalk.Lists = 0;
    hc->FilterClassify.MessageMembers = 0;
    hc->FilterClassify.LineMembers = 0;
    hc->FilterSnapCount = 0;
    /* The filter's snapshot of the mode cache (AddDevice's or the last
     * start's reading); this filter reads no registry. */
    hc->FilterModeRaw = hc->ModeCacheRaw;
    hc->FilterModeFound = hc->ModeCacheFound;
    hc->FilterModeFrom = hc->ModeCacheFrom;
    XhciPollValues(&values, hc->ModeCacheFound, hc->ModeCacheRaw, 0, 0, 0,
                   0);
    hc->FilterMode = values.Mode;

    status = HcdForwardAndWait(hc, irp);
#if !defined(_WIN64)
    hcdMsi86Event(hc, XHCI_MSI86_EV_FILTER);
#endif
    replaced = 0;
    if (!NT_SUCCESS(status) && status != STATUS_NOT_SUPPORTED) {
        XhciMsiFilterPublish(&hc->MsiFilter, XHCI_MSI_CLASS_UNKNOWN,
                             XHCI_MSI_OUT_LOWER_FAILED);
        goto done;
    }
    list = (PUCHAR)irp->IoStatus.Information;
    if (list == NULL) {
        XhciMsiFilterPublish(&hc->MsiFilter, XHCI_MSI_CLASS_UNKNOWN,
                             XHCI_MSI_OUT_NO_LIST);
        goto done;
    }
    XhciMsiWalk(list, &hc->FilterWalk);
    XhciMsiClassify(list, &hc->FilterWalk, &hc->FilterClassify);
#if defined(XHCI_FLAVOUR_QEMU)
    hcdMsiTraceList(hc, 'R', list, &hc->FilterWalk,
                    hc->FilterClassify.Class, 0);
#endif
    if (hc->FilterWalk.Verdict != XHCI_MSI_WALK_OK) {
        XhciMsiFilterPublish(&hc->MsiFilter, XHCI_MSI_CLASS_INCONSISTENT,
                             XHCI_MSI_OUT_INCONSISTENT);
        goto done;
    }
    hcdMsiSnap(hc, list, &hc->FilterWalk);

#if defined(XHCI_FLAVOUR_QEMU)
    /* H1a's stimulus (record 18 section 10): the list made NO_LINE before
     * it is classified for the outcome. */
    if (hc->QemuInjNoLine) {
        size = XhciMsiInjNoLine(list, &hc->FilterWalk, NULL, 0);
        if (size != 0) {
            fresh = hcdMsiReplace(irp, list, size, 0, &hc->FilterWalk,
                                  &hc->FilterClassify);
            if (fresh != NULL) {
                list = fresh;
                replaced = 1;
                XhciMsiWalk(list, &hc->FilterWalk);
                XhciMsiClassify(list, &hc->FilterWalk, &hc->FilterClassify);
                hcdMsiTraceList(hc, 'I', list, &hc->FilterWalk,
                                hc->FilterClassify.Class, 0);
            }
        }
    }
#endif

#if defined(_WIN64)
    asked = (hc->FilterMode == XHCI_POLL_MODE_LINE) ? 1UL : 0UL;
#else
    asked = (hc->FilterMode == XHCI_POLL_MODE_LINE ||
             hc->Msi86Bind.State != XHCI_MSI86_STATE_BOUND)
                ? 1UL
                : 0UL;
#endif
    enabled = HCD_MSI_STRIP_DEFAULT;
    if (hc->QemuNoStrip) {
        enabled = 0;
    }
    if (!asked) {
        outcome = XHCI_MSI_OUT_NOT_ASKED;
    } else if (!enabled) {
        outcome = XHCI_MSI_OUT_DISABLED;
    } else {
        outcome = XhciMsiOutcome(hc->FilterClassify.Class);
        if (outcome == XHCI_MSI_OUT_REPLACED) {
            size = XhciMsiStrip(list, &hc->FilterWalk, &hc->FilterClassify,
                                NULL, 0);
            fresh = (size != 0)
                        ? hcdMsiReplace(irp, list, size, 1, &hc->FilterWalk,
                                        &hc->FilterClassify)
                        : NULL;
            if (fresh == NULL) {
                outcome = XHCI_MSI_OUT_NO_MEMORY;
            } else {
                hc->FilterStripped = hc->FilterClassify.MessageMembers;
                list = fresh;
                replaced = 1;
            }
        }
    }
    XhciMsiFilterPublish(&hc->MsiFilter, hc->FilterClassify.Class, outcome);
#if defined(XHCI_FLAVOUR_QEMU)
    if (replaced) {
        XHCI_MSI_WALK after;
        XHCI_MSI_CLASSIFY afterClass;

        XhciMsiWalk(list, &after);
        XhciMsiClassify(list, &after, &afterClass);
        hcdMsiTraceList(hc, 'T', list, &after, afterClass.Class, outcome);
    }
#endif

done:
    HcdMsiCountersStart(hc);
    HcdMsiTraceLife(hc, HCD_MSI_LIFE_FILTER, hc->MsiFilter.Class,
                    hc->MsiFilter.Outcome);
    if (replaced) {
        status = STATUS_SUCCESS;
    }
    return HcdCompleteIrp(irp, status, irp->IoStatus.Information);
}
