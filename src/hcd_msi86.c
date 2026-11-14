/*
 * hcd_msi86.c - message-signalled interrupts on Vista and later, the kernel
 * half that is not the connection itself (design record 15 section 4.3;
 * design record 18; roadmap-hcd.md tasks 36.15 and 36.16).
 *
 *   - Where IoConnectInterruptEx and IoDisconnectInterruptEx come from:
 *     amd64 asks MmGetSystemRoutineAddress; the x86 image, which also loads
 *     on Windows 98 and so may import neither, asks the x86 helper
 *     xhcihlpr.sys over its named device, channel version 2 (record 18
 *     sections 3 and 15.3; until task 38.3 xhcimsi.sys's version 1). The
 *     pair is handed to hcd_ctl.c's hcdMsgConnect, the one seam.
 *   - The helper's binding per controller FDO (record 18 sections 4.1 and
 *     15.4), which also carries the timer service's ExSetTimerResolution
 *     to hcd_tmr.c (HcdHlprTimerRoutine; record 15 section 7.2.3).
 *   - IRP_MN_FILTER_RESOURCE_REQUIREMENTS (record 18 section 4.2): the
 *     checked walk, the class, and the strip that keeps only the line
 *     configurations when x86 has no helper bound or the mode is 1 - or,
 *     under mode 3, the rewrite that keeps only the message (revision
 *     12).
 *   - The same IRP on Windows 2000, XP and XP x64 (task 47.33): the lower
 *     configurations kept, then a copy of each without its line, so a
 *     controller no line can be given (decodes off at boot and no _PRT
 *     entry: no boot configuration, no routing) starts on polled delivery
 *     instead of Code 12.
 *   - The mode cache the filter reads, filled at AddDevice and at each
 *     start, both system-thread contexts (section 4.2; on NT 5 too).
 *   - The qemu flavour's checksummed trace (record 18 section 9).
 *
 * Nothing here runs below NT 6 but the timer's look, the mode cache and the
 * NT 5 filter (task 47.33): every other entry tests
 * IoIsWdmVersionAvailable(6, 0) first, and Windows 98 and ME pass the
 * filter IRP down untouched, as before. HcdHlprTimerRoutine runs on
 * Windows 2000 and later only, from the timer thread (never on 98 or ME).
 *
 * IRQL: PASSIVE_LEVEL throughout, in the PnP manager's threads (AddDevice,
 * IRP_MN_START_DEVICE and IRP_MN_REMOVE_DEVICE are system-thread
 * contexts; IRP_MN_FILTER_RESOURCE_REQUIREMENTS reads no registry) and the
 * timer thread's (HcdHlprTimerRoutine), except hcdHlprDone, a completion
 * routine (<= DISPATCH_LEVEL).
 */

#include "hcd.h"
#include "xhci_version.h"
#include "xhci_dbg.h"
#include "xhci_hw.h"

/* Record 18 section 4.2: the strip is enabled, R1 and R2 having been read
 * on Vista SP2 and 7 SP1, x86 and x64 (section 11; the same list on all
 * four, so one default serves every NT 6 target), for the admitted shape
 * alone (XhciMsiAdmitted); the qemu flavour's XhciMsi86NoStrip still turns
 * it off. */
#define HCD_MSI_STRIP_DEFAULT       1UL

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

/* Windows 2000 and later: an NT kernel (98 and ME answer WDM 1.0x). */
static ULONG hcdMsiNt(VOID)
{
    return IoIsWdmVersionAvailable(1, 0x10) ? 1UL : 0UL;
}

/* XHCI_VER packed one byte a part, the channel's CallerVersion: the helper
 * keeps no log and does not read it, so it is there for a dump of the
 * block. */
static ULONG hcdMsiVersion(VOID)
{
    return XhciHlprPackVersion(XHCI_VER_STR);
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
    f[2] = hc->HlprBind.State;
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
    c->HlprMsiVerdict = hc->HlprMsiVerdict;
    c->HlprTimerVerdict = hc->HlprTimerVerdict;
    c->Msi86State = hc->HlprBind.State;
    c->Msi86Looks = hc->HlprBind.Looks;
    c->Msi86Status = (ULONG)hc->HlprStatus;
    c->Msi86Refusal = hc->HlprRefusal;
    c->Msi86HelperVersion = hc->HlprHelperVersion;
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

/* AddDevice, a system thread: the records' first state, and on NT (task
 * 47.33 for NT 5) the mode as the registry holds it now, for this FDO's
 * first filter. */
VOID HcdMsiAddDevice(PHCD_CONTROLLER hc)
{
    ULONG value;

    XhciMsiFilterInit(&hc->MsiFilter);
    XhciHlprBindInit(&hc->HlprBind);
    hc->ModeCacheRaw = 0;
    hc->ModeCacheFound = 0;
    hc->ModeCacheFrom = HCD_MSI_FROM_NONE;
    if (!hcdMsiNt()) {
        return;
    }
    value = 0;
    if (NT_SUCCESS(HcdReadValueDword(hc, L"XhciInterruptMode", &value))) {
        hc->ModeCacheRaw = value;
        hc->ModeCacheFound = 1;
    }
    value = 0;
    hc->Msg4StrategyCache =
        NT_SUCCESS(HcdReadValueDword(hc, L"XhciMsiStrategy", &value)) ? value
                                                                      : 0UL;
    hc->ModeCacheFrom = HCD_MSI_FROM_ADDDEVICE;
    hcdMsiReadSwitches(hc);
}

/* The start's own reading, a system thread too, kept for a later filter
 * on this FDO (a rebalance's). The filter's snapshot is not touched. */
VOID HcdMsiStartValues(PHCD_CONTROLLER hc, ULONG modeFound, ULONG mode)
{
    if (!hcdMsiNt()) {
        return;
    }
    hc->ModeCacheRaw = modeFound ? mode : 0UL;
    hc->ModeCacheFound = modeFound;
    hc->ModeCacheFrom = HCD_MSI_FROM_START;
    hcdMsiReadSwitches(hc);
}

/* ---- the x86 helper (record 18 sections 3, 4.1 and 15.3 to 15.4) ------- */

#if !defined(_WIN64)
static NTSTATUS NTAPI hcdHlprDone(PDEVICE_OBJECT DeviceObject, PIRP Irp,
                                  PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);

    (VOID)KeSetEvent((PKEVENT)Context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

/* One look: open the helper's device, send the V2 query, judge the channel
 * and each service, and step the binding with the result. A service's
 * routines are kept only on its own "accepted". */
static VOID hcdHlprLook(PHCD_CONTROLLER hc)
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
    ULONG msi;
    ULONG timer;
    ULONG event;
    ULONG act;
    PUCHAR p;
    ULONG i;

    file = NULL;
    msi = XHCI_HLPR_SVC_NONE;
    timer = XHCI_HLPR_SVC_NONE;
    hc->HlprRefusal = 0;
    RtlInitUnicodeString(&name, XHCI_HLPR_DEVICE_NAME);
    status = IoGetDeviceObjectPointer(&name, FILE_READ_DATA, &file, &device);
    if (!NT_SUCCESS(status)) {
        hc->HlprStatus = status;
        event = XHCI_HLPR_EV_LOOK_ABSENT;
        goto step;
    }
    irp = IoAllocateIrp(device->StackSize, FALSE);
    if (irp == NULL) {
        hc->HlprStatus = STATUS_INSUFFICIENT_RESOURCES;
        event = XHCI_HLPR_EV_LOOK_FAILED;
        goto step;
    }
    p = (PUCHAR)&hc->HlprQuery;
    for (i = 0; i < sizeof(hc->HlprQuery); i++) {
        p[i] = 0;
    }
    hc->HlprQuery.Size = sizeof(XHCI_HLPR_QUERY);
    hc->HlprQuery.CallerMajor = XHCI_HLPR_MAJOR;
    hc->HlprQuery.CallerMinor = XHCI_HLPR_MINOR_V20;
    hc->HlprQuery.CallerVersion = hcdMsiVersion();
    hc->HlprQuery.CallerWants =
        (hcdMsiNt6() ? XHCI_HLPR_SVC_MSI : 0UL) | XHCI_HLPR_SVC_TIMER;
    irp->RequestorMode = KernelMode;
    irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    irp->IoStatus.Information = 0;
    next = IoGetNextIrpStackLocation(irp);
    next->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
    next->Parameters.DeviceIoControl.IoControlCode =
        XHCI_HLPR_IOCTL_QUERY_V2;
    next->Parameters.DeviceIoControl.Type3InputBuffer = &hc->HlprQuery;
    next->Parameters.DeviceIoControl.InputBufferLength =
        sizeof(XHCI_HLPR_QUERY);
    next->Parameters.DeviceIoControl.OutputBufferLength =
        sizeof(XHCI_HLPR_QUERY);
    next->FileObject = file;
    KeInitializeEvent(&done, NotificationEvent, FALSE);
    HCD_SET_COMPLETION_ALWAYS(irp, hcdHlprDone, &done);
    if (IoCallDriver(device, irp) == STATUS_PENDING) {
        (VOID)HcdWaitEvent(&done, 0);
    }
    status = irp->IoStatus.Status;
    information = (ULONG)irp->IoStatus.Information;
    IoFreeIrp(irp);
    hc->HlprStatus = status;
    verdict = XhciHlprAccept(NT_SUCCESS(status) ? 1UL : 0UL, information,
                             sizeof(XHCI_HLPR_QUERY), &hc->HlprQuery,
                             hcdMsiNt6(), &msi, &timer);
    if (verdict == XHCI_HLPR_ACCEPTED) {
        event = XHCI_HLPR_EV_LOOK_ACCEPTED;
    } else {
        hc->HlprRefusal = verdict;
        event = XHCI_HLPR_EV_LOOK_REFUSED;
    }

step:
    act = XhciHlprBindStep(&hc->HlprBind, event);
    HcdMsiTraceLife(hc, HCD_MSI_LIFE_LOOK, event, (ULONG)hc->HlprStatus);
    /* The refusal's own event, with the channel test that failed, before
     * the look's reference goes (task 36.15). */
    if (event == XHCI_HLPR_EV_LOOK_REFUSED) {
        HcdMsiTraceLife(hc, HCD_MSI_LIFE_REFUSE, hc->HlprRefusal,
                        (ULONG)hc->HlprStatus);
    }
    if ((act & XHCI_HLPR_ACT_DROP_LOOK) != 0 && file != NULL) {
        ObDereferenceObject(file);
        HcdMsiTraceLife(hc, HCD_MSI_LIFE_DROP, 0, 0);
        return;
    }
    if (event == XHCI_HLPR_EV_LOOK_ACCEPTED) {
        hc->HlprFile = file;
        hc->HlprMsiVerdict = msi;
        hc->HlprTimerVerdict = timer;
        hc->HlprConnectEx = (msi == XHCI_HLPR_SVC_ACCEPTED)
                                ? hc->HlprQuery.ConnectEx
                                : NULL;
        hc->HlprDisconnectEx = (msi == XHCI_HLPR_SVC_ACCEPTED)
                                   ? hc->HlprQuery.DisconnectEx
                                   : NULL;
        hc->HlprSetTimer = (timer == XHCI_HLPR_SVC_ACCEPTED)
                               ? hc->HlprQuery.SetTimerResolution
                               : NULL;
        hc->HlprHelperVersion = hc->HlprQuery.HelperVersion;
        hc->HlprHelperMinor = hc->HlprQuery.HelperMinor;
        hc->HlprFlags = hc->HlprQuery.Flags;
        HcdMsiTraceLife(hc, HCD_MSI_LIFE_BIND, hc->HlprHelperVersion,
                        hc->HlprFlags);
    }
}

static VOID hcdHlprEvent(PHCD_CONTROLLER hc, ULONG event)
{
    if ((XhciHlprBindStep(&hc->HlprBind, event) & XHCI_HLPR_ACT_LOOK) != 0) {
        hcdHlprLook(hc);
    }
}

/* The MSI service is the helper's for this FDO: bound, and that half
 * accepted (record 18 section 15.3: "the helper bound" now reads so). */
static ULONG hcdHlprMsiServed(PHCD_CONTROLLER hc)
{
    return (hc->HlprBind.State == XHCI_HLPR_STATE_BOUND &&
            hc->HlprMsiVerdict == XHCI_HLPR_SVC_ACCEPTED)
               ? 1UL
               : 0UL;
}
#endif /* !_WIN64 */

/*
 * ExSetTimerResolution for the timer thread (design record 15 section
 * 7.2.3), NULL when not found: on amd64 MmGetSystemRoutineAddress; on x86
 * the helper's timer service. `look` asks for the binding machine's
 * XHCI_HLPR_EV_TIMER first, which looks only while not BOUND - the 32-bit
 * Windows 2000 and XP thread's own look (record 18 section 15.4); without
 * it the binding the filter and the start already made is read (Vista and
 * 7, from the start inside the PnP manager's serialisation). Never called on
 * Windows 98 or ME. IRQL: PASSIVE_LEVEL.
 */
PVOID HcdHlprTimerRoutine(PHCD_CONTROLLER hc, ULONG look)
{
#if defined(_WIN64)
    UNICODE_STRING name;

    UNREFERENCED_PARAMETER(hc);
    UNREFERENCED_PARAMETER(look);
    RtlInitUnicodeString(&name, L"ExSetTimerResolution");
    return MmGetSystemRoutineAddress(&name);
#else
    if (look) {
        hcdHlprEvent(hc, XHCI_HLPR_EV_TIMER);
    }
    if (hc->HlprBind.State != XHCI_HLPR_STATE_BOUND ||
        hc->HlprTimerVerdict != XHCI_HLPR_SVC_ACCEPTED) {
        return NULL;
    }
    return hc->HlprSetTimer;
#endif
}

/* The start's look on x86 while not bound (record 18 section 4.1). */
VOID HcdMsiStartLook(PHCD_CONTROLLER hc)
{
#if !defined(_WIN64)
    if (hcdMsiNt6()) {
        hcdHlprEvent(hc, XHCI_HLPR_EV_START);
    }
#else
    UNREFERENCED_PARAMETER(hc);
#endif
}

/* The remove, after the stop's release has disconnected and the stop has
 * joined the timer thread: the reference held for a BOUND answer dropped,
 * exactly once (record 18 sections 3.3 and 15.4). */
VOID HcdMsiRemove(PHCD_CONTROLLER hc)
{
#if !defined(_WIN64)
    ULONG act;

    act = XhciHlprBindStep(&hc->HlprBind, XHCI_HLPR_EV_REMOVE);
    if ((act & XHCI_HLPR_ACT_DROP) != 0 && hc->HlprFile != NULL) {
        ObDereferenceObject(hc->HlprFile);
        HcdMsiTraceLife(hc, HCD_MSI_LIFE_DROP, 1, 0);
    }
    hc->HlprFile = NULL;
    hc->HlprConnectEx = NULL;
    hc->HlprDisconnectEx = NULL;
    hc->HlprSetTimer = NULL;
#endif
    HcdMsiTraceLife(hc, HCD_MSI_LIFE_REMOVE, 0, 0);
}

/* ---- mode 4's name lookup (record 18 section 15.16, task 49.24) -------- */

#if !defined(_WIN64)
typedef PVOID (NTAPI *HCD_FIND_ROUTINE)(PUNICODE_STRING name);

/* The helper's MmGetSystemRoutineAddress, once fetched: the kernel's
 * routine, so it outlives the reference dropped after the query. */
static HCD_FIND_ROUTINE HcdMsiFindPtr;
#endif

/*
 * Whether mode 4's library can resolve: amd64 always; x86 once the
 * helper's minor-1 routine is fetched, by a one-shot query of its own
 * (the V2.1 block, judged by XhciHlprAcceptRoutine) that leaves this FDO's
 * binding (record 18 section 15.4) as it was. Called under hcd_msgnt.c's
 * image lock. IRQL: PASSIVE_LEVEL.
 */
ULONG HcdMsiFindRoutineLook(PHCD_CONTROLLER hc)
{
#if defined(_WIN64)
    UNREFERENCED_PARAMETER(hc);
    return 1;
#else
    XHCI_HLPR_QUERY1 query;
    UNICODE_STRING name;
    PFILE_OBJECT file;
    PDEVICE_OBJECT device;
    PIRP irp;
    PIO_STACK_LOCATION next;
    KEVENT done;
    NTSTATUS status;
    ULONG verdict;
    ULONG routine;
    PUCHAR p;
    ULONG i;

    if (HcdMsiFindPtr != NULL) {
        return 1;
    }
    RtlInitUnicodeString(&name, XHCI_HLPR_DEVICE_NAME);
    status = IoGetDeviceObjectPointer(&name, FILE_READ_DATA, &file, &device);
    if (!NT_SUCCESS(status)) {
        XhciLogNote(&hc->Hc, "msg4.helper", (ULONG)status);
        return 0;
    }
    irp = IoAllocateIrp(device->StackSize, FALSE);
    if (irp == NULL) {
        ObDereferenceObject(file);
        XhciLogNote(&hc->Hc, "msg4.helper", (ULONG)STATUS_NO_MEMORY);
        return 0;
    }
    p = (PUCHAR)&query;
    for (i = 0; i < sizeof(query); i++) {
        p[i] = 0;
    }
    query.V2.Size = sizeof(XHCI_HLPR_QUERY1);
    query.V2.CallerMajor = XHCI_HLPR_MAJOR;
    query.V2.CallerMinor = XHCI_HLPR_MINOR;
    query.V2.CallerVersion = hcdMsiVersion();
    query.V2.CallerWants = XHCI_HLPR_SVC_ROUTINE;
    irp->RequestorMode = KernelMode;
    irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    irp->IoStatus.Information = 0;
    next = IoGetNextIrpStackLocation(irp);
    next->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
    next->Parameters.DeviceIoControl.IoControlCode =
        XHCI_HLPR_IOCTL_QUERY_V2;
    next->Parameters.DeviceIoControl.Type3InputBuffer = &query;
    next->Parameters.DeviceIoControl.InputBufferLength =
        sizeof(XHCI_HLPR_QUERY1);
    next->Parameters.DeviceIoControl.OutputBufferLength =
        sizeof(XHCI_HLPR_QUERY1);
    next->FileObject = file;
    KeInitializeEvent(&done, NotificationEvent, FALSE);
    HCD_SET_COMPLETION_ALWAYS(irp, hcdHlprDone, &done);
    if (IoCallDriver(device, irp) == STATUS_PENDING) {
        (VOID)HcdWaitEvent(&done, 0);
    }
    status = irp->IoStatus.Status;
    verdict = XhciHlprAcceptRoutine(NT_SUCCESS(status) ? 1UL : 0UL,
                                    (ULONG)irp->IoStatus.Information,
                                    sizeof(XHCI_HLPR_QUERY1), &query,
                                    &routine);
    IoFreeIrp(irp);
    ObDereferenceObject(file);
    XhciLogNote(&hc->Hc, "msg4.helper", (verdict << 8) | routine);
    if (verdict != XHCI_HLPR_ACCEPTED || routine != XHCI_HLPR_SVC_ACCEPTED) {
        return 0;
    }
    HcdMsiFindPtr = (HCD_FIND_ROUTINE)query.FindRoutine;
    return 1;
#endif
}

/* msi4nt's Resolve and the seam's own names (hcd_msgnt.c): only after
 * HcdMsiFindRoutineLook answered 1. IRQL: PASSIVE_LEVEL. */
PVOID HcdMsiResolve(PCWSTR name)
{
    UNICODE_STRING s;

    RtlInitUnicodeString(&s, name);
#if defined(_WIN64)
    return MmGetSystemRoutineAddress(&s);
#else
    if (HcdMsiFindPtr == NULL) {
        return NULL;
    }
    return HcdMsiFindPtr(&s);
#endif
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
    if (!hcdHlprMsiServed(hc)) {
        return HCD_MSI_ROUTINES_NOT_FOUND;
    }
    *connect = hc->HlprConnectEx;
    *disconnect = hc->HlprDisconnectEx;
    return HCD_MSI_ROUTINES_HELPER;
#endif
}

/* Whether this system can grant a message-signalled interrupt at all: NT 6
 * (record 15 section 4, XhciPollDecide's messageSystem). */
ULONG HcdMsiSystem(VOID)
{
    return hcdMsiNt6();
}

/* x86 alone, no helper's MSI service accepted, and this FDO's latest filter
 * classified its list NO_LINE (record 18 sections 4.3 and 15.3). Revision 6's mode-1 input; since
 * revision 12 a recorded fact only, in the routines' L record. */
ULONG HcdMsiNoLineOffered(PHCD_CONTROLLER hc)
{
#if !defined(_WIN64)
    return (hcdMsiNt6() && !hcdHlprMsiServed(hc) &&
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

/* The rewrites hcdMsiReplace writes. */
#define HCD_MSI_REWRITE_INJ_NO_LINE 0UL     /* the qemu switch (H1a)      */
#define HCD_MSI_REWRITE_NO_INTR     3UL     /* NT 5: copies with no line  */
#define HCD_MSI_REWRITE_STRIP       1UL     /* the line kept              */
#define HCD_MSI_REWRITE_MESSAGE     2UL     /* mode 3: the message kept   */
#define HCD_MSI_REWRITE_STRIP_NT5   4UL     /* 2000, listed: no line at all */

/* Replaces the list at Information with `size` bytes the rewrite fills; the
 * old list freed. Returns the new list, NULL when no memory. */
static PUCHAR hcdMsiReplace(PIRP irp, PUCHAR list, ULONG size,
                            ULONG rewrite, const XHCI_MSI_WALK *walk,
                            const XHCI_MSI_CLASSIFY *classify, ULONG gsi,
                            ULONG memory)
{
    PUCHAR fresh;
    ULONG written;

    fresh = (PUCHAR)HcdPoolAllocRequirements(size);
    if (fresh == NULL) {
        return NULL;
    }
    if (rewrite == HCD_MSI_REWRITE_STRIP) {
        written = XhciMsiStrip(list, walk, classify, fresh, size);
    } else if (rewrite == HCD_MSI_REWRITE_MESSAGE) {
        written = XhciMsiKeepMessage(list, walk, classify, fresh, size);
    } else if (rewrite == HCD_MSI_REWRITE_NO_INTR) {
        written = XhciMsiAppendNt5(list, walk, gsi, memory, fresh, size);
    } else if (rewrite == HCD_MSI_REWRITE_STRIP_NT5) {
        written = XhciMsiStripNt5(list, walk, fresh, size);
    } else {
        written = XhciMsiInjNoLine(list, walk, fresh, size);
    }
    if (written != size) {
        HcdPoolFreeForeign(fresh);
        return NULL;
    }
    HcdPoolFreeForeign(list);
    irp->IoStatus.Information = (ULONG_PTR)fresh;
    return fresh;
}

/*
 * Record 18 section 16.4, where no interrupt-free list can be made: the
 * list that carries the line is freed and none is returned, so nothing is
 * arbitrated for the device and its start, given no memory, is refused
 * (Code 10). A failed IRP is no refusal: the PnP manager went on with the
 * PCI driver's list, and Windows 2000 stopped (the injected legs, runs/
 * run-47.md). Needs no memory. IRQL: PASSIVE_LEVEL.
 */
static VOID hcdMsiStripNone(PIRP irp, PUCHAR list, PULONG replaced)
{
    if (list != NULL) {
        HcdPoolFreeForeign(list);
    }
    irp->IoStatus.Information = 0;
    *replaced = 1;
}

/*
 * Task 47.33, design record 18 section 16.4: Windows 2000 on a listed
 * board (hc->NoRouteStrip, AddDevice's decision). There ACPI stops the
 * system with 0xA5 when it is asked to route this device's line, which its
 * firmware's _PRT does not name, so the list goes on with no interrupt in
 * any configuration, under every XhciInterruptMode (1 and 3 then refuse
 * the start, Code 10, instead of the stop). Where no such list can be
 * made, no list goes on at all (hcdMsiStripNone); a list already without
 * an interrupt passes unchanged. Returns the outcome; *replaced 1 when the
 * list was replaced or withdrawn. IRQL: PASSIVE_LEVEL.
 */
static ULONG hcdMsiStripNt5(PHCD_CONTROLLER hc, PIRP irp, PUCHAR list,
                            PULONG replaced)
{
    PUCHAR fresh;
    ULONG size;

    size = XhciMsiStripNt5(list, &hc->FilterWalk, NULL, 0);
#if defined(XHCI_FLAVOUR_QEMU)
    if (hc->QemuStripFail == 2) {
        size = 0;
    }
#endif
    if (size == 0) {
        if (hc->FilterClassify.Class == XHCI_MSI_CLASS_NO_INTERRUPT) {
            return XHCI_MSI_OUT_NO_INTERRUPT;
        }
        hcdMsiStripNone(irp, list, replaced);
        return XHCI_MSI_OUT_STRIP_FAILED;
    }
#if defined(XHCI_FLAVOUR_QEMU)
    if (hc->QemuStripFail == 1) {
        hcdMsiStripNone(irp, list, replaced);
        return XHCI_MSI_OUT_STRIP_FAILED;
    }
#endif
    fresh = hcdMsiReplace(irp, list, size, HCD_MSI_REWRITE_STRIP_NT5,
                          &hc->FilterWalk, &hc->FilterClassify,
                          XHCI_MSI_NO_GSI, 0);
    if (fresh == NULL) {
        /* hcdMsiReplace frees nothing when it fails. */
        hcdMsiStripNone(irp, list, replaced);
        return XHCI_MSI_OUT_STRIP_FAILED;
    }
    *replaced = 1;
    return XHCI_MSI_OUT_LINE_REMOVED;
}

/*
 * Task 47.33: the filter on Windows 2000, XP and XP x64. XP's PCI driver
 * gives a controller whose decodes the firmware left off no boot
 * configuration, so its interrupt can come only from the firmware's _PRT;
 * where _PRT does not name it (the HP EliteBook 2570p and 8470p) no line
 * can be given and the one configuration fails whole: Code 12. Behind the
 * lower configurations, kept first, a copy of each without its line lets
 * the PnP manager grant the memory alone there, and the start takes polled
 * delivery (record 15 section 4, mode 0). XhciInterruptMode 1 (the line)
 * and 3 (a message) ask for no copy. The x86 helper is not looked for here
 * (the timer thread's look stays NT 5's only one, record 18 section 15).
 * The counters' MsiFilterLists and the snapshot describe the list the
 * lower drivers gave. IRQL: PASSIVE_LEVEL, the PnP manager's thread.
 */
static NTSTATUS hcdMsiFilterNt5(PHCD_CONTROLLER hc, PIRP irp)
{
    NTSTATUS status;
    PUCHAR list;
    PUCHAR fresh;
    XHCI_POLL_VALUES values;
    ULONG outcome;
    ULONG size;
    ULONG replaced;
    ULONG gsi;
    ULONG memory;
    ULONG fm;

    XhciMsiFilterBegin(&hc->MsiFilter);
    hc->MsiAvoidFilterStrips = 0;
    hc->FilterStripped = 0;
    hc->FilterWalk.Verdict = XHCI_MSI_WALK_OK;
    hc->FilterWalk.Lists = 0;
    hc->FilterClassify.MessageMembers = 0;
    hc->FilterClassify.LineMembers = 0;
    hc->FilterSnapCount = 0;
    hc->FilterModeRaw = hc->ModeCacheRaw;
    hc->FilterModeFound = hc->ModeCacheFound;
    hc->FilterModeFrom = hc->ModeCacheFrom;
    XhciPollValues(&values, hc->ModeCacheFound, hc->ModeCacheRaw, 0, 0, 0,
                   0);
    hc->FilterMode = values.Mode;
    /* Offered only once a filter has written the GSI clone in. */
    hc->NoRouteOffered = 0;

    status = HcdForwardAndWait(hc, irp);
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
#if defined(XHCI_FLAVOUR_QEMU)
    if (hc->QemuStripFail == 3) {
        hc->FilterWalk.Verdict = XHCI_MSI_WALK_COUNT;
    }
#endif
    XhciMsiClassify(list, &hc->FilterWalk, &hc->FilterClassify);
    if (hc->FilterWalk.Verdict != XHCI_MSI_WALK_OK) {
        /* Record 18 section 16.4: a list the walk refuses is never passed
         * on where the strip is required - it may carry the line. */
        if (hc->NoRouteStrip) {
            hcdMsiStripNone(irp, list, &replaced);
            XhciMsiFilterPublish(&hc->MsiFilter, XHCI_MSI_CLASS_INCONSISTENT,
                                 XHCI_MSI_OUT_STRIP_FAILED);
            goto done;
        }
        XhciMsiFilterPublish(&hc->MsiFilter, XHCI_MSI_CLASS_INCONSISTENT,
                             XHCI_MSI_OUT_INCONSISTENT);
        goto done;
    }
    hcdMsiSnap(hc, list, &hc->FilterWalk);

    if (hc->NoRouteStrip) {
        outcome = hcdMsiStripNt5(hc, irp, list, &replaced);
        XhciMsiFilterPublish(&hc->MsiFilter, hc->FilterClassify.Class,
                             outcome);
        goto done;
    }

    /* What each mode adds (design record 18 section 16): 0 (auto) the
     * chipset-routed GSI where AddDevice's run offered one, then memory
     * alone; 1 (the line) the GSI alone; 2 (poll) memory alone; 3 (a
     * message, which NT 5 cannot give) nothing. The GSI clone only for
     * the PCI driver's shared, level-sensitive line (XhciMsiNt5LineShape). */
    /* Mode 4 (record 15 section 4.5.2, task 49.24): strategy 1 borrows the
     * line, so it asks what mode 1 asks; strategy 0 needs no line, so it
     * asks what mode 0 asks. From AddDevice's or the last start's cache. */
    fm = hc->FilterMode;
    if (fm == XHCI_POLL_MODE_MSG4) {
        fm = (hc->Msg4StrategyCache == XHCI_MSG4_STRATEGY_BORROW)
                 ? XHCI_POLL_MODE_LINE
                 : XHCI_POLL_MODE_AUTO;
    }
    gsi = XHCI_MSI_NO_GSI;
    if ((fm == XHCI_POLL_MODE_AUTO || fm == XHCI_POLL_MODE_LINE) &&
        (hc->NoRoute.Decision == XHCI_PIRQ_O_OFFERED ||
         hc->NoRoute.Decision == XHCI_PIRQ_O_QEMU) &&
        XhciMsiNt5LineShape(list, &hc->FilterWalk)) {
        gsi = hc->NoRoute.Gsi;
    }
    memory = (fm == XHCI_POLL_MODE_AUTO || fm == XHCI_POLL_MODE_POLL) ? 1UL
                                                                     : 0UL;
    if (gsi == XHCI_MSI_NO_GSI && !memory) {
        outcome = XHCI_MSI_OUT_NOT_ASKED;
    } else {
        size = XhciMsiAppendNt5(list, &hc->FilterWalk, gsi, memory, NULL, 0);
        if (size == 0) {
            outcome = (hc->FilterClassify.Class ==
                           XHCI_MSI_CLASS_NO_INTERRUPT)
                          ? XHCI_MSI_OUT_NO_INTERRUPT
                          : XHCI_MSI_OUT_UNSUPPORTED;
        } else {
            fresh = hcdMsiReplace(irp, list, size, HCD_MSI_REWRITE_NO_INTR,
                                  &hc->FilterWalk, &hc->FilterClassify, gsi,
                                  memory);
            if (fresh == NULL) {
                outcome = XHCI_MSI_OUT_NO_MEMORY;
            } else {
                outcome = (gsi == XHCI_MSI_NO_GSI)
                              ? XHCI_MSI_OUT_NO_INTR_ADDED
                              : (memory ? XHCI_MSI_OUT_GSI_ADDED
                                        : XHCI_MSI_OUT_GSI_ONLY);
                hc->NoRouteOffered = (gsi != XHCI_MSI_NO_GSI) ? 1UL : 0UL;
                replaced = 1;
            }
        }
    }
    XhciMsiFilterPublish(&hc->MsiFilter, hc->FilterClassify.Class, outcome);

done:
    HcdMsiCountersStart(hc);
    HcdMsiTraceLife(hc, HCD_MSI_LIFE_FILTER, hc->MsiFilter.Class,
                    hc->MsiFilter.Outcome);
    if (replaced) {
        status = STATUS_SUCCESS;
    }
    return HcdCompleteIrp(irp, status, irp->IoStatus.Information);
}

NTSTATUS HcdMsiFilter(PHCD_CONTROLLER hc, PIRP irp)
{
    NTSTATUS status;
    PUCHAR list;
    PUCHAR fresh;
    XHCI_POLL_VALUES values;
    ULONG asked;
    ULONG quirkAsked;
    ULONG keepMessage;
    ULONG enabled;
    ULONG outcome;
    ULONG size;
    ULONG replaced;

    if (!hcdMsiNt6()) {
        if (hcdMsiNt()) {
            return hcdMsiFilterNt5(hc, irp);
        }
        return HcdPassDown(hc, irp);
    }
    /* Step 0: the class is UNKNOWN before anything can return early. */
    XhciMsiFilterBegin(&hc->MsiFilter);
    hc->MsiAvoidFilterStrips = 0;
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
    hcdHlprEvent(hc, XHCI_HLPR_EV_FILTER);
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
            fresh = hcdMsiReplace(irp, list, size,
                                  HCD_MSI_REWRITE_INJ_NO_LINE,
                                  &hc->FilterWalk, &hc->FilterClassify,
                                        XHCI_MSI_NO_GSI, 0);
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

    /* Mode 3 asks for the message kept, on both images and whatever the
     * helper's state, and for nothing else (revision 12). */
    keepMessage = (hc->FilterMode == XHCI_POLL_MODE_MESSAGE) ? 1UL : 0UL;
#if defined(_WIN64)
    asked = (keepMessage || hc->FilterMode == XHCI_POLL_MODE_LINE) ? 1UL
                                                                   : 0UL;
#else
    asked = (keepMessage || hc->FilterMode == XHCI_POLL_MODE_LINE ||
             !hcdHlprMsiServed(hc))
                ? 1UL
                : 0UL;
#endif
    /* Task 38.10, XhciMsiAvoid (design record 20 section 4.1): under mode
     * 0, a controller the quirk applies to asks for the line alone, as
     * mode 1 does; from AddDevice's or the last start's caches. */
    quirkAsked = 0;
    if (!asked && hc->FilterMode == XHCI_POLL_MODE_AUTO &&
        HcdQuirkFilterAvoid(hc)) {
        asked = 1;
        quirkAsked = 1;
    }
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
        /* Only the shape R1 and R2 read is replaced (section 4.2). */
        if (outcome == XHCI_MSI_OUT_REPLACED &&
            !XhciMsiAdmitted(list, &hc->FilterWalk)) {
            outcome = XHCI_MSI_OUT_UNSUPPORTED;
        }
        if (outcome == XHCI_MSI_OUT_REPLACED && keepMessage) {
            size = XhciMsiKeepMessage(list, &hc->FilterWalk,
                                      &hc->FilterClassify, NULL, 0);
            fresh = (size != 0)
                        ? hcdMsiReplace(irp, list, size,
                                        HCD_MSI_REWRITE_MESSAGE,
                                        &hc->FilterWalk, &hc->FilterClassify,
                                        XHCI_MSI_NO_GSI, 0)
                        : NULL;
            if (fresh == NULL) {
                outcome = XHCI_MSI_OUT_NO_MEMORY;
            } else {
                outcome = XHCI_MSI_OUT_REPLACED_MESSAGE;
                hc->FilterStripped = hc->FilterClassify.LineMembers;
                list = fresh;
                replaced = 1;
            }
        } else if (outcome == XHCI_MSI_OUT_REPLACED) {
            size = XhciMsiStrip(list, &hc->FilterWalk, &hc->FilterClassify,
                                NULL, 0);
            fresh = (size != 0)
                        ? hcdMsiReplace(irp, list, size,
                                        HCD_MSI_REWRITE_STRIP,
                                        &hc->FilterWalk, &hc->FilterClassify,
                                        XHCI_MSI_NO_GSI, 0)
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
    /* 38.10: a strip the quirk asked for, kept for the next start's dump
     * (hcd_quirk.c, HcdQuirkMsiAvoid). */
    hc->MsiAvoidFilterStrips = 0;
    if (quirkAsked && outcome == XHCI_MSI_OUT_REPLACED) {
        hc->MsiAvoidFilterStrips = hc->FilterStripped;
    }
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
