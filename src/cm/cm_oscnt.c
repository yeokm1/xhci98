/*
 * cm_oscnt.c - the platform's USB4 handover on the NT targets, the OS half
 * (roadmap-usb4.md task 46.12; design record 32 section 12.8.15): on an
 * ASM4242 host whose firmware sends the handover from _OSC only for
 * Windows 11, the command is sent from the NHI's start, before ring 0,
 * once per boot and again after a hibernation (Q16 (a)), to a known
 * board's profile (E, section 12.8.15.11: cm_oscp.c's match, no firmware
 * memory read), through the upstream port's own configuration interface
 * alone, with the mailbox excluded from the firmware's own use of it by
 * this driver's filters on the switch's six other functions (section
 * 12.8.15.7), and the boot guard of section 12.8.15.13 around every send.
 * An unknown board is sent nothing: with XhciCmAsm4242Reads 0 it is
 * reported so (status bit 22); with 1 the read path (B, C and D, sections
 * 12.8.15.4 to 12.8.15.6 and 12.8.15.17) reads the firmware's tables and
 * data in the match's place, through cm_oscos.c's discovery, every read
 * refused whose pages the system's own snapshot of the memory it manages
 * holds; a refusal of it is 110h, cm.osc.read its reason.
 *
 * ECAM (record 32 section 12.8.15.18, task 46.25; the owner's exception
 * to section 12.8.13.1 for these systems alone): on Windows 2000, 32-bit
 * XP and XP x64, whose bus interfaces do not reach the mailbox, its two
 * dwords go through the upstream port's ECAM page instead, only with
 * XhciCmAsm4242Reads 1 (else 113h): the base from MCFG (class 76 on XP
 * x64, the scan and the MCFG pass on 2000 and XP), the page refused where
 * the system's snapshot holds it, mapped (MmMapIoSpace, MmNonCached) only
 * inside a hold of the members and unmapped before its release, and
 * checked against the bus interface before the mailbox is read. The bus
 * interface is never asked an offset of 100h or more there.
 *
 * THE MEMBERS. The upstream port (1B21:2421, an upper filter above the PCI
 * driver, xhcicmf.inf) and the xHCI (1B21:2426, an upper filter above its
 * function driver, the installer's UpperFilters entry) are CM_KIND_MEMBER
 * objects of this file; each downstream port (1B21:2423) is the bridge
 * filter of cm_filt.c, whose member half is its Osc. Each tracks its
 * device SET_POWER requests and its start, stop, query-stop,
 * query-remove, cancel and removal requests from their arrival until they
 * return; while an exchange holds the list, a new one waits (PASSIVE_LEVEL)
 * or is queued (a power request at DISPATCH_LEVEL) and goes on when the
 * exchange ends. A surprise removal is never held: it stops the exchange
 * before its next mailbox access.
 *
 * Reads: configuration space through HalGetBusData (the host by bus, the
 * root port, coverage) and through the upstream port's
 * BUS_INTERFACE_STANDARD (the route's checks and the mailbox); SMBIOS
 * through WMI's MSSmBios_RawSMBiosTables block. Writes: the mailbox's two
 * dwords alone, through that interface; the boot guard's marker
 * (cm_statos.c). The NT-only routines are found by
 * MmGetSystemRoutineAddress: imported on amd64; on x86, whose image loads
 * on 98 SE and ME too and cannot import it, handed out by the helper's
 * channel, minor 1 (xhcihlpr.sys, \Device\Xhci98Helper, record 32
 * section 12.8.15.15), asked once a load. Where the helper is absent or
 * answers without it, the x86 image refuses (108h).
 *
 * IRQL: PASSIVE_LEVEL, the NHI's thread (the start's work, the resume's
 * D0 work, the shutdown's quiesce) and the members' PnP dispatch; the
 * power dispatch and its completion, and CmOscntSystem, at <=
 * DISPATCH_LEVEL. The list's lock is a spin lock; the once-per-boot
 * sequence is under a mutex (a synchronization event) at PASSIVE_LEVEL.
 */

#include "cm.h"
#include "cm_osc.h"
#include "cm_oscp.h"
#if !defined(_AMD64_)
#include "../xhci_hlpr.h"
#include "../xhci_version.h"
#endif

#if defined(_WIN64) && defined(NO_LEGACY_DRIVERS)
NTHALAPI ULONG HalGetBusData(BUS_DATA_TYPE BusDataType, ULONG BusNumber,
                             ULONG SlotNumber, PVOID Buffer, ULONG Length);
#endif

#define CM_OSN_HOSTS        4UL
#define CM_OSN_MEMBERS      28UL    /* seven functions of four hosts      */
#define CM_OSN_WANT         16UL
#define CM_OSN_WAIT_MS      1000UL
#define CM_OSN_SETTLE_MS    200UL
#define CM_OSN_REPLY_OK     0x00000100UL
#define CM_OSN_WIN11        22000UL
#define CM_OSN_WMI_MAX      0x10000UL
#define CM_OSN_TIER_PROFILE 4UL     /* the status record's tier 4 (E)     */

/* The refusals (cm.osc.refused) of the NT routes, beside 98's 100h-107h. */
#define CM_OSN_R_SYSTEM     0x108UL /* Windows 11, or no routine route    */
#define CM_OSN_R_MAILBOX    0x109UL /* the first mailbox read failed      */
#define CM_OSN_R_NO_ENTRY   0x10AUL /* no upstream port member            */
#define CM_OSN_R_ROUTE      0x10BUL /* the route reaches another function */
#define CM_OSN_R_OBJECT     0x10CUL /* the once-per-boot object failed    */
#define CM_OSN_R_COVERAGE   0x10DUL /* a member filter absent or withdrawn */
#define CM_OSN_R_BUSY       0x10EUL /* a member's request held past 1 s   */
#define CM_OSN_R_GUARD      0x10FUL /* the guard's marker not persisted   */
#define CM_OSN_R_READS      0x110UL /* the read path declined (cm.osc.read) */
#define CM_OSN_R_CHANGED    0x111UL /* the host changed since the start's
                                       decision (12.8.15.16)              */
#define CM_OSN_R_ALIAS      0x112UL /* the route lands an extended access
                                       in the first 256 bytes             */
#define CM_OSN_R_ECAMOFF    0x113UL /* ECAM alone reaches the mailbox, and
                                       XhciCmAsm4242Reads is 0 (12.8.15.18) */
#define CM_OSN_R_ECAM       0x114UL /* no ECAM page for the port          */
#define CM_OSN_R_ECAMPORT   0x115UL /* the ECAM page is not the port      */

/* ---- the routines looked up -------------------------------------------- */

typedef struct _CM_OSN_VERSION {
    ULONG Size;
    ULONG Major;
    ULONG Minor;
    ULONG Build;
    ULONG Platform;
    WCHAR Csd[128];
} CM_OSN_VERSION;

typedef NTSTATUS (NTAPI *CM_OSN_GETVER)(CM_OSN_VERSION *info);
typedef NTSTATUS (NTAPI *CM_OSN_WMIOPEN)(GUID *guid, ULONG access,
                                         PVOID *object);
typedef NTSTATUS (NTAPI *CM_OSN_WMIQUERY)(PVOID object, PULONG size,
                                          PVOID buffer);

typedef PVOID (NTAPI *CM_OSN_FIND)(PUNICODE_STRING name);
typedef PVOID (NTAPI *CM_OSN_EX2)(PVOID partition, ULONG flags);
typedef PVOID (NTAPI *CM_OSN_RANGES)(VOID);
typedef NTSTATUS (NTAPI *CM_OSN_FWENV)(PUNICODE_STRING name, GUID *vendor,
                                       PVOID value, PULONG length,
                                       PULONG attributes);
typedef NTSTATUS (NTAPI *CM_OSN_QUERY)(ULONG infoClass, PVOID info,
                                       ULONG length, PULONG returned);

static ULONG cmOsnRtDone;
static BOOLEAN cmOsnRtOk;               /* cmOsnRoutines' answer          */
static CM_OSN_GETVER cmOsnGetVersion;
static CM_OSN_WMIOPEN cmOsnWmiOpen;
static CM_OSN_WMIQUERY cmOsnWmiQuery;
static CM_OSN_EX2 cmOsnEx2;             /* the read path (12.8.15.17)    */
static CM_OSN_RANGES cmOsnRanges;
static CM_OSN_FWENV cmOsnFwEnv;
static CM_OSN_QUERY cmOsnQuery;
static ULONG cmOsnRtWhy;                /* cm.osc.nt.route: 0 found      */
static ULONG cmOsnRtStatus;             /* ... and its status            */
static KEVENT cmOsnMutex;               /* the once-per-boot sequence     */

#define CM_OSN_RT_FOUND     0UL
#define CM_OSN_RT_ABSENT    1UL     /* x86: the helper's device not opened */
#define CM_OSN_RT_IRP       2UL     /* x86: no IRP for the query          */
#define CM_OSN_RT_REFUSED   3UL     /* x86: the channel refused (+ test)  */
#define CM_OSN_RT_NO_FIND   4UL     /* x86: no routine service (+ verdict) */
#define CM_OSN_RT_MISSING   5UL     /* x86: the marker's routines absent  */

static PVOID cmOsnFind(CM_OSN_FIND find, const WCHAR *name)
{
    UNICODE_STRING s;
    ULONG n;

    for (n = 0; name[n] != 0; n++) {
    }
    s.Buffer = (PWSTR)name;
    s.Length = (USHORT)(n * sizeof(WCHAR));
    s.MaximumLength = (USHORT)((n + 1UL) * sizeof(WCHAR));
    return find(&s);
}

#if !defined(_AMD64_)
static NTSTATUS NTAPI cmOsnHlprDone(PDEVICE_OBJECT device, PIRP irp,
                                    PVOID context)
{
    UNREFERENCED_PARAMETER(device);
    UNREFERENCED_PARAMETER(irp);
    KeSetEvent((PKEVENT)context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static XHCI_HLPR_QUERY1 cmOsnHlprQuery;   /* under cmOsnRtMutex          */

/* The helper's MmGetSystemRoutineAddress (12.8.15.15): one minor 1 query
 * of \Device\Xhci98Helper, judged by XhciHlprAcceptRoutine, the file
 * object dereferenced at once (the helper never unloads). NULL, with
 * cmOsnRtWhy and cmOsnRtStatus, when the helper is absent, refuses or
 * answers without the service; *transient TRUE when the query failed for
 * want of memory alone (no IRP, or the device not opened for it), so it
 * may be asked again. PASSIVE_LEVEL, under cmOsnRtMutex. */
static CM_OSN_FIND cmOsnHelperFind(BOOLEAN *transient)
{
    XHCI_HLPR_QUERY1 *q;
    UNICODE_STRING name;
    PFILE_OBJECT file;
    PDEVICE_OBJECT device;
    PIRP irp;
    PIO_STACK_LOCATION next;
    KEVENT done;
    NTSTATUS status;
    ULONG verdict;
    ULONG routine;
    CM_OSN_FIND find;

    find = NULL;
    *transient = FALSE;
    cmOsnRtWhy = CM_OSN_RT_FOUND;
    q = &cmOsnHlprQuery;
    /* Spelled out: RtlInitUnicodeString is held to cm_statos.obj. */
    name.Buffer = (PWSTR)XHCI_HLPR_DEVICE_NAME;
    name.Length = (USHORT)(sizeof(XHCI_HLPR_DEVICE_NAME) - sizeof(WCHAR));
    name.MaximumLength = (USHORT)sizeof(XHCI_HLPR_DEVICE_NAME);
    status = IoGetDeviceObjectPointer(&name, FILE_READ_DATA, &file, &device);
    cmOsnRtStatus = (ULONG)status;
    if (!NT_SUCCESS(status)) {
        cmOsnRtWhy = CM_OSN_RT_ABSENT;
        *transient = (BOOLEAN)(status == STATUS_INSUFFICIENT_RESOURCES);
        return NULL;
    }
    irp = IoAllocateIrp(device->StackSize, FALSE);
    if (irp == NULL) {
        cmOsnRtWhy = CM_OSN_RT_IRP;
        *transient = TRUE;
        ObDereferenceObject(file);
        return NULL;
    }
    CmZero(q, sizeof(*q));
    q->V2.Size = sizeof(XHCI_HLPR_QUERY1);
    q->V2.CallerMajor = XHCI_HLPR_MAJOR;
    q->V2.CallerMinor = XHCI_HLPR_MINOR;
    q->V2.CallerVersion = XhciHlprPackVersion(XHCI_VER_STR);
    q->V2.CallerWants = XHCI_HLPR_SVC_ROUTINE;
    irp->RequestorMode = KernelMode;
    irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    irp->IoStatus.Information = 0;
    next = IoGetNextIrpStackLocation(irp);
    next->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
    next->Parameters.DeviceIoControl.IoControlCode =
        XHCI_HLPR_IOCTL_QUERY_V2;
    next->Parameters.DeviceIoControl.Type3InputBuffer = q;
    next->Parameters.DeviceIoControl.InputBufferLength = sizeof(*q);
    next->Parameters.DeviceIoControl.OutputBufferLength = sizeof(*q);
    next->FileObject = file;
    KeInitializeEvent(&done, NotificationEvent, FALSE);
    CM_SET_COMPLETION_ALWAYS(irp, cmOsnHlprDone, &done);
    if (IoCallDriver(device, irp) == STATUS_PENDING) {
        (VOID)KeWaitForSingleObject(&done, Executive, KernelMode, FALSE,
                                    NULL);
    }
    status = irp->IoStatus.Status;
    verdict = XhciHlprAcceptRoutine(NT_SUCCESS(status) ? 1UL : 0UL,
                                    (ULONG)irp->IoStatus.Information,
                                    sizeof(*q), q, &routine);
    IoFreeIrp(irp);
    ObDereferenceObject(file);
    cmOsnRtStatus = (ULONG)status;
    if (verdict != XHCI_HLPR_ACCEPTED) {
        cmOsnRtWhy = CM_OSN_RT_REFUSED | (verdict << 8);
    } else if (routine != XHCI_HLPR_SVC_ACCEPTED) {
        cmOsnRtWhy = CM_OSN_RT_NO_FIND | (routine << 8);
    } else {
        find = (CM_OSN_FIND)q->FindRoutine;
    }
    return find;
}
#endif

/* The lookup routine itself, found once a load under its own mutex
 * (cmOsnRtMutex): amd64 its import; x86 the helper's (cmOsnRtWhy and
 * cmOsnRtStatus say why not). A query that failed for want of memory is
 * not kept: the next caller asks again, so an early caller (the ACPI-named
 * source's filter, record 22 section 7.6.1) cannot leave a later one
 * without the routine for the load. */
static KEVENT cmOsnRtMutex;
static ULONG cmOsnReady;              /* CmOscntInit ran               */
static ULONG cmOsnFindDone;
static CM_OSN_FIND cmOsnFindRoutine;

/* The lookup, and under its mutex a copy of why it failed (`why` and
 * `status`, cm.osc.nt.route's pair; each may be NULL) and whether the
 * failure was for want of memory alone (`transient`, may be NULL), in
 * which case nothing is kept and the next caller asks again. */
static CM_OSN_FIND cmOsnLookup(BOOLEAN *transient, PULONG why,
                               PULONG status)
{
    CM_OSN_FIND find;
    BOOLEAN again;

    again = FALSE;
    (VOID)KeWaitForSingleObject(&cmOsnRtMutex, Executive, KernelMode, FALSE,
                                NULL);
    if (!cmOsnFindDone) {
#if defined(_AMD64_)
        cmOsnFindRoutine = MmGetSystemRoutineAddress;
#else
        cmOsnFindRoutine = cmOsnHelperFind(&again);
#endif
        cmOsnFindDone = (ULONG)!again;
    }
    find = cmOsnFindRoutine;
    if (why != NULL) {
        *why = cmOsnRtWhy;
    }
    if (status != NULL) {
        *status = cmOsnRtStatus;
    }
    (VOID)KeSetEvent(&cmOsnRtMutex, IO_NO_INCREMENT, FALSE);
    if (transient != NULL) {
        *transient = again;
    }
    return find;
}

PVOID CmRoutine(const WCHAR *name)
{
    CM_OSN_FIND find;

    if (!cmOsnReady || !IoIsWdmVersionAvailable(1, 0x10)) {
        return NULL;
    }
    find = cmOsnLookup(NULL, NULL, NULL);
    return (find != NULL) ? cmOsnFind(find, name) : NULL;
}

/* TRUE when the lookup and the routines every route needs were found
 * (on x86 the marker's two, handed to cm_statos.c; 12.8.15.17: the
 * version and WMI routines, and the read path's, are each looked for and
 * needed only where they are used), found once a load under the
 * once-per-boot mutex: a lookup that failed for want of memory is not
 * kept, so a later handover looks again. `why` and `status` get this
 * judgement's cm.osc.nt.route pair. PASSIVE_LEVEL, that mutex not held. */
static ULONG cmOsnHoWhy;                /* under cmOsnMutex               */
static ULONG cmOsnHoStatus;

static BOOLEAN cmOsnRoutines(PULONG why, PULONG status)
{
    CM_OSN_FIND find;
    BOOLEAN transient;
    BOOLEAN ok;

    (VOID)KeWaitForSingleObject(&cmOsnMutex, Executive, KernelMode, FALSE,
                                NULL);
    if (!cmOsnRtDone) {
        find = cmOsnLookup(&transient, &cmOsnHoWhy, &cmOsnHoStatus);
        if (find != NULL) {
            cmOsnGetVersion = (CM_OSN_GETVER)cmOsnFind(find,
                                                       L"RtlGetVersion");
            cmOsnWmiOpen = (CM_OSN_WMIOPEN)cmOsnFind(find,
                                                     L"IoWMIOpenBlock");
            cmOsnWmiQuery = (CM_OSN_WMIQUERY)cmOsnFind(find,
                                                       L"IoWMIQueryAllData");
            cmOsnEx2 = (CM_OSN_EX2)cmOsnFind(find,
                                             L"MmGetPhysicalMemoryRangesEx2");
            cmOsnRanges = (CM_OSN_RANGES)cmOsnFind(find,
                                                   L"MmGetPhysicalMemoryRanges");
            cmOsnFwEnv = (CM_OSN_FWENV)cmOsnFind(find,
                                       L"ExGetFirmwareEnvironmentVariable");
            cmOsnQuery = (CM_OSN_QUERY)cmOsnFind(find,
                                                 L"ZwQuerySystemInformation");
#if !defined(_AMD64_)
            {
                PVOID flush;
                PVOID del;

                flush = cmOsnFind(find, L"ZwFlushKey");
                del = cmOsnFind(find, L"ZwDeleteValueKey");
                if (flush != NULL && del != NULL) {
                    CmStatusMarkerRoutines(flush, del);
                } else {
                    cmOsnHoWhy = CM_OSN_RT_MISSING;
                }
            }
#endif
            if (cmOsnWmiOpen == NULL || cmOsnWmiQuery == NULL) {
                cmOsnWmiOpen = NULL;
                cmOsnWmiQuery = NULL;
            }
        }
        cmOsnRtOk = (BOOLEAN)(find != NULL &&
                              cmOsnHoWhy == CM_OSN_RT_FOUND);
        cmOsnRtDone = (ULONG)(find != NULL || !transient);
    }
    ok = cmOsnRtOk;
    *why = cmOsnHoWhy;
    *status = cmOsnHoStatus;
    (VOID)KeSetEvent(&cmOsnMutex, IO_NO_INCREMENT, FALSE);
    return ok;
}

/* ---- the state of the boot ------------------------------------------------ */

typedef struct _CM_OSN_HOST {
    ULONG Usp;          /* the upstream port's bdf + 1; 0 free           */
    ULONG Sent;         /* the handover written this boot                */
    ULONG Ok;           /* the first send's reply was 100h               */
    ULONG Owed;         /* an S4 resend owed (12.8.13.6)                 */
    ULONG TimedOut;     /* an exchange timed out this boot (Q21)         */
} CM_OSN_HOST;

static KSPIN_LOCK cmOsnLock;
static CM_OSN_HOST cmOsnHost[CM_OSN_HOSTS];
static ULONG cmOsnLatch;                /* a clear whose persistence is
                                           unknown: no send for the boot  */
static ULONG cmOsnGuarded[CM_OSN_HOSTS];/* hosts whose marker this boot's
                                           first start read (bdf + 1)     */
static ULONG cmOsnGuardSkip[CM_OSN_HOSTS];/* ... and skipped this boot     */
static PCM_OSC_MEMBER cmOsnList[CM_OSN_MEMBERS];
static ULONG cmOsnFull;                 /* registrations refused: full    */
static ULONG cmOsnHolding;              /* an exchange holds the members  */
static ULONG cmOsnAbort;                /* a surprise removal meanwhile   */
static ULONG cmOsnNoExt;                /* no offset of 100h or more asked
                                           of a route: Windows 2000
                                           (12.8.15.16), 32-bit XP and XP
                                           x64 (12.8.15.18)               */
static ULONG cmOsnEcamSys;              /* the mailbox through ECAM alone:
                                           2000, 32-bit XP, XP x64
                                           (12.8.15.18)                   */
static LONG cmOsnInFlight;              /* the members' tracked requests  */
static KEVENT cmOsnQuiet;               /* set while cmOsnInFlight is 0   */
static KEVENT cmOsnOpen;                /* set while not holding          */
static LIST_ENTRY cmOsnQueue;           /* power requests queued at
                                           DISPATCH_LEVEL while holding   */

VOID CmOscntInit(VOID)
{
    if (cmOsnReady) {
        return;
    }
    KeInitializeSpinLock(&cmOsnLock);
    KeInitializeEvent(&cmOsnMutex, SynchronizationEvent, TRUE);
    KeInitializeEvent(&cmOsnRtMutex, SynchronizationEvent, TRUE);
    KeInitializeEvent(&cmOsnQuiet, NotificationEvent, TRUE);
    KeInitializeEvent(&cmOsnOpen, NotificationEvent, TRUE);
    InitializeListHead(&cmOsnQueue);
    cmOsnReady = 1;
}

static BOOLEAN cmOsnNt(VOID)
{
    return IoIsWdmVersionAvailable(1, 0x10);
}

/* The host entry of `usp` under the lock, or NULL. */
static CM_OSN_HOST *cmOsnHostOf(ULONG usp)
{
    ULONG i;

    for (i = 0; i < CM_OSN_HOSTS; i++) {
        if (cmOsnHost[i].Usp == usp + 1UL) {
            return &cmOsnHost[i];
        }
    }
    return NULL;
}

/* ---- configuration space through the HAL --------------------------------- */

#define CM_OSN_CFG_OK       0UL
#define CM_OSN_CFG_ABSENT   1UL
#define CM_OSN_CFG_NO_BUS   2UL
#define CM_OSN_CFG_FAIL     3UL

static ULONG cmOsnCfgEx(ULONG bdf, ULONG offset, PULONG value)
{
    UCHAR buffer[256];
    ULONG got;

    if ((offset & 3UL) != 0 || offset > 252UL) {
        return CM_OSN_CFG_FAIL;
    }
    got = HalGetBusData(PCIConfiguration, XHCI_TBT_BDF_BUS(bdf),
                        ((bdf >> 3) & 0x1FUL) | ((bdf & 7UL) << 5), buffer,
                        offset + 4UL);
    if (got == 0) {
        return CM_OSN_CFG_NO_BUS;
    }
    if (got == 2) {
        return CM_OSN_CFG_ABSENT;
    }
    if (got < offset + 4UL) {
        return CM_OSN_CFG_FAIL;
    }
    *value = (ULONG)buffer[offset] | ((ULONG)buffer[offset + 1] << 8) |
             ((ULONG)buffer[offset + 2] << 16) |
             ((ULONG)buffer[offset + 3] << 24);
    return CM_OSN_CFG_OK;
}

static BOOLEAN cmOsnCfg(ULONG bdf, ULONG offset, PULONG value)
{
    return (BOOLEAN)(cmOsnCfgEx(bdf, offset, value) == CM_OSN_CFG_OK);
}

/* A present function's id at `bdf`, else 0. */
static ULONG cmOsnId(ULONG bdf)
{
    ULONG v;

    if (!cmOsnCfg(bdf, 0, &v) || v == 0xFFFFFFFFUL ||
        (v & 0xFFFFUL) == 0xFFFFUL) {
        return 0;
    }
    return v;
}

/* A capability of `bdf` by id through the HAL: its offset or 0. */
static ULONG cmOsnCap(ULONG bdf, ULONG want)
{
    ULONG v;
    ULONG at;
    ULONG i;

    if (!cmOsnCfg(bdf, 0x34, &v)) {
        return 0;
    }
    at = v & 0xFFUL;
    for (i = 0; i < 48UL && at >= 0x40UL && (at & 3UL) == 0; i++) {
        if (!cmOsnCfg(bdf, at, &v)) {
            return 0;
        }
        if ((v & 0xFFUL) == want) {
            return at;
        }
        at = (v >> 8) & 0xFFUL;
    }
    return 0;
}

/* ---- the members ----------------------------------------------------------- */

VOID CmOscmInit(PCM_OSC_MEMBER m, ULONG role, PDEVICE_OBJECT pdo,
                PDEVICE_OBJECT lower)
{
    CmZero(m, sizeof(*m));
    m->Role = role;
    m->Pdo = pdo;
    m->Lower = lower;
    KeInitializeEvent(&m->Unused, NotificationEvent, TRUE);
}

BOOLEAN CmOscmTracked(PIRP irp)
{
    PIO_STACK_LOCATION stack;

    stack = IoGetCurrentIrpStackLocation(irp);
    if (stack->MajorFunction == IRP_MJ_POWER) {
        return (BOOLEAN)(stack->MinorFunction == IRP_MN_SET_POWER &&
                         stack->Parameters.Power.Type == DevicePowerState);
    }
    switch (stack->MinorFunction) {
    case IRP_MN_START_DEVICE:
    case IRP_MN_STOP_DEVICE:
    case IRP_MN_QUERY_STOP_DEVICE:
    case IRP_MN_CANCEL_STOP_DEVICE:
    case IRP_MN_QUERY_REMOVE_DEVICE:
    case IRP_MN_CANCEL_REMOVE_DEVICE:
    case IRP_MN_REMOVE_DEVICE:
        return TRUE;
    default:
        return FALSE;
    }
}

/* A tracked request admitted to the members' count, under the lock: the
 * exchange's hold waits for this count. The member's own count (InFlight,
 * its lifetime) is taken at the request's arrival, before any wait. */
static VOID cmOsnAdmit(VOID)
{
    if (cmOsnInFlight++ == 0) {
        KeClearEvent(&cmOsnQuiet);
    }
}

/* PASSIVE_LEVEL: the member held from here (its removal waits for it),
 * then a wait while an exchange holds the members. */
VOID CmOscmEnter(PCM_OSC_MEMBER m)
{
    KIRQL irql;

    KeAcquireSpinLock(&cmOsnLock, &irql);
    m->InFlight++;
    for (;;) {
        if (!cmOsnHolding) {
            cmOsnAdmit();
            KeReleaseSpinLock(&cmOsnLock, irql);
            return;
        }
        KeReleaseSpinLock(&cmOsnLock, irql);
        (VOID)KeWaitForSingleObject(&cmOsnOpen, Executive, KernelMode, FALSE,
                                    NULL);
        KeAcquireSpinLock(&cmOsnLock, &irql);
    }
}

/* <= DISPATCH_LEVEL. */
VOID CmOscmLeave(PCM_OSC_MEMBER m)
{
    KIRQL irql;

    KeAcquireSpinLock(&cmOsnLock, &irql);
    m->InFlight--;
    if (--cmOsnInFlight == 0) {
        KeSetEvent(&cmOsnQuiet, IO_NO_INCREMENT, FALSE);
    }
    KeReleaseSpinLock(&cmOsnLock, irql);
}

/* The member's location and, for a bridge, its secondary bus, read at its
 * start: its PDO's, checked against its ids through the HAL. */
static VOID cmOsnLocate(PCM_OSC_MEMBER m, ULONG *bdf, ULONG *sec,
                        BOOLEAN *ok)
{
    ULONG bus;
    ULONG address;
    ULONG bytes;
    ULONG id;
    ULONG v;
    ULONG want;

    *ok = FALSE;
    bus = 0;
    address = 0;
    if (!NT_SUCCESS(IoGetDeviceProperty(m->Pdo, DevicePropertyBusNumber,
                                        sizeof(bus), &bus, &bytes)) ||
        !NT_SUCCESS(IoGetDeviceProperty(m->Pdo, DevicePropertyAddress,
                                        sizeof(address), &address,
                                        &bytes)) ||
        bus > 0xFFUL || (address >> 16) > 0x1FUL ||
        (address & 0xFFFFUL) > 7UL) {
        return;
    }
    *bdf = XHCI_TBT_BDF(bus, address >> 16, address & 0xFFFFUL);
    want = m->Role == CM_OSCP_R_USP   ? CM_OSCP_ID_USP
           : m->Role == CM_OSCP_R_DSP ? CM_OSCP_ID_DSP
                                      : CM_OSCP_ID_XHCI;
    id = cmOsnId(*bdf);
    if (id != want) {
        return;
    }
    *sec = 0;
    if (m->Role != CM_OSCP_R_XHCI) {
        if (!cmOsnCfg(*bdf, 0x18, &v)) {
            return;
        }
        *sec = (v >> 8) & 0xFFUL;
    }
    *ok = TRUE;
}

static NTSTATUS NTAPI cmOsnSignal(PDEVICE_OBJECT device, PIRP irp,
                                  PVOID context)
{
    UNREFERENCED_PARAMETER(device);
    UNREFERENCED_PARAMETER(irp);
    KeSetEvent((PKEVENT)context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

/* {496B8280-6F25-11D0-BEAF-08002BE2092F}, GUID_BUS_INTERFACE_STANDARD as
 * the DDK's wdmguid.h defines it (not ...8281, the PCI bus interface). */
static const GUID cmOsnBusGuid = {
    0x496B8280UL, 0x6F25, 0x11D0,
    { 0xBE, 0xAF, 0x08, 0x00, 0x2B, 0xE2, 0x09, 0x2F }
};

/* The upstream port's configuration interface, asked of the stack below
 * it at its start (12.8.15.7): TRUE when taken. PASSIVE_LEVEL. */
static BOOLEAN cmOsnTakeBusAt(PCM_OSC_MEMBER m, PDEVICE_OBJECT to)
{
    PIRP irp;
    PIO_STACK_LOCATION next;
    KEVENT done;
    NTSTATUS status;

    irp = IoAllocateIrp(to->StackSize, FALSE);
    if (irp == NULL) {
        return FALSE;
    }
    CmZero(&m->Bus, sizeof(m->Bus));
    KeInitializeEvent(&done, NotificationEvent, FALSE);
    irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    irp->IoStatus.Information = 0;
    next = IoGetNextIrpStackLocation(irp);
    next->MajorFunction = IRP_MJ_PNP;
    next->MinorFunction = IRP_MN_QUERY_INTERFACE;
    next->Parameters.QueryInterface.InterfaceType = &cmOsnBusGuid;
    next->Parameters.QueryInterface.Size =
        (USHORT)sizeof(BUS_INTERFACE_STANDARD);
    next->Parameters.QueryInterface.Version = 1;
    next->Parameters.QueryInterface.Interface = (PINTERFACE)&m->Bus;
    next->Parameters.QueryInterface.InterfaceSpecificData = NULL;
    CM_SET_COMPLETION_ALWAYS(irp, cmOsnSignal, &done);
    (VOID)IoCallDriver(to, irp);
    (VOID)KeWaitForSingleObject(&done, Executive, KernelMode, FALSE, NULL);
    status = irp->IoStatus.Status;
    IoFreeIrp(irp);
    m->BusStatus = (ULONG)status;
    if (NT_SUCCESS(status) && (m->Bus.GetBusData == NULL ||
                               m->Bus.SetBusData == NULL ||
                               m->Bus.InterfaceDereference == NULL)) {
        m->BusStatus = 0xC00000BBUL;    /* STATUS_NOT_SUPPORTED */
    }
    if (!NT_SUCCESS(status) || m->Bus.GetBusData == NULL ||
        m->Bus.SetBusData == NULL || m->Bus.InterfaceDereference == NULL) {
        if (NT_SUCCESS(status) && m->Bus.InterfaceDereference != NULL) {
            m->Bus.InterfaceDereference(m->Bus.Context);
        }
        CmZero(&m->Bus, sizeof(m->Bus));
        return FALSE;
    }
    return TRUE;
}

/* Asked of the stack below the member, and when that refuses, of the
 * port's PDO itself, the PCI driver's object that answers it. */
static BOOLEAN cmOsnTakeBus(PCM_OSC_MEMBER m)
{
    ULONG first;

    if (cmOsnTakeBusAt(m, m->Lower)) {
        return TRUE;
    }
    first = m->BusStatus;
    if (m->Lower == m->Pdo || !cmOsnTakeBusAt(m, m->Pdo)) {
        m->BusStatus = (first & 0xFFFFUL) | (m->BusStatus << 16);
        return FALSE;
    }
    return TRUE;
}

/* The list entry withdrawn and the interface given back once no exchange
 * holds it. PASSIVE_LEVEL. */
static VOID cmOsnWithdraw(PCM_OSC_MEMBER m)
{
    KIRQL irql;

    (VOID)KeWaitForSingleObject(&m->Unused, Executive, KernelMode, FALSE,
                                NULL);
    KeAcquireSpinLock(&cmOsnLock, &irql);
    if (m->Slot != 0) {
        cmOsnList[m->Slot - 1UL] = NULL;
        m->Slot = 0;
    }
    KeReleaseSpinLock(&cmOsnLock, irql);
    if (m->BusHeld) {
        m->Bus.InterfaceDereference(m->Bus.Context);
        m->BusHeld = 0;
    }
}

VOID CmOscmAfter(PCM_OSC_MEMBER m, UCHAR minor, NTSTATUS status)
{
    KIRQL irql;
    ULONG bdf;
    ULONG sec;
    ULONG i;
    BOOLEAN ok;
    BOOLEAN bus;

    if (m->Role == CM_OSCP_R_NONE) {
        return;
    }
    if (minor == IRP_MN_START_DEVICE) {
        if (!NT_SUCCESS(status)) {
            return;
        }
        bdf = 0;
        sec = 0;
        cmOsnLocate(m, &bdf, &sec, &ok);
        bus = FALSE;
        if (m->Role == CM_OSCP_R_USP && !m->BusHeld) {
            bus = cmOsnTakeBus(m);
        }
        KeAcquireSpinLock(&cmOsnLock, &irql);
        if (bus) {
            m->BusHeld = 1;
        }
        m->Located = ok ? 1UL : 0UL;
        m->Bdf = bdf;
        m->Secondary = sec;
        m->Started = 1;
        m->Closed = 0;
        if (m->Slot == 0 && !m->Gone) {
            for (i = 0; i < CM_OSN_MEMBERS && cmOsnList[i] != NULL; i++) {
            }
            if (i < CM_OSN_MEMBERS) {
                cmOsnList[i] = m;
                m->Slot = i + 1UL;
            } else {
                cmOsnFull++;
            }
        }
        KeReleaseSpinLock(&cmOsnLock, irql);
        return;
    }
    KeAcquireSpinLock(&cmOsnLock, &irql);
    switch (minor) {
    case IRP_MN_QUERY_STOP_DEVICE:
    case IRP_MN_QUERY_REMOVE_DEVICE:
        /* A pending answer is taken as a success: closed, a refusal. */
        if (NT_SUCCESS(status) || status == STATUS_PENDING) {
            m->Closed = 1;
        }
        break;
    case IRP_MN_CANCEL_STOP_DEVICE:
    case IRP_MN_CANCEL_REMOVE_DEVICE:
        if (m->Started && NT_SUCCESS(status) && status != STATUS_PENDING) {
            m->Closed = 0;
        }
        break;
    case IRP_MN_STOP_DEVICE:
        m->Started = 0;
        m->Closed = 0;
        break;
    default:
        break;
    }
    KeReleaseSpinLock(&cmOsnLock, irql);
}

VOID CmOscmSurprise(PCM_OSC_MEMBER m)
{
    KIRQL irql;

    if (m->Role == CM_OSCP_R_NONE) {
        return;
    }
    KeAcquireSpinLock(&cmOsnLock, &irql);
    m->Started = 0;
    m->Gone = 1;
    if (cmOsnHolding) {
        cmOsnAbort = 1;
    }
    KeReleaseSpinLock(&cmOsnLock, irql);
    cmOsnWithdraw(m);
}

/* The removal (its own request counted by CmOscmEnter): closed to new
 * tracked requests, then every request that still holds the member (a
 * power request queued by the hold and sent at its release, or one in
 * flight below) waited for, then withdrawn; after it nothing of this file
 * refers to the member. PASSIVE_LEVEL. */
VOID CmOscmRemove(PCM_OSC_MEMBER m)
{
    LARGE_INTEGER ms;
    KIRQL irql;
    LONG n;

    if (m->Role == CM_OSCP_R_NONE) {
        return;
    }
    KeAcquireSpinLock(&cmOsnLock, &irql);
    m->Started = 0;
    m->Gone = 1;
    KeReleaseSpinLock(&cmOsnLock, irql);
    ms.QuadPart = -(LONGLONG)(1UL * 10000UL);
    for (;;) {
        KeAcquireSpinLock(&cmOsnLock, &irql);
        n = m->InFlight + m->Usage;
        KeReleaseSpinLock(&cmOsnLock, irql);
        if (n <= 1) {
            break;
        }
        (VOID)KeDelayExecutionThread(KernelMode, FALSE, &ms);
    }
    cmOsnWithdraw(m);
}

/* <= DISPATCH_LEVEL (a completion routine). */
static NTSTATUS NTAPI cmOsnPowerDone(PDEVICE_OBJECT device, PIRP irp,
                                     PVOID context)
{
    UNREFERENCED_PARAMETER(device);
    if (irp->PendingReturned) {
        IoMarkIrpPending(irp);
    }
    CmOscmLeave((PCM_OSC_MEMBER)context);
    return STATUS_SUCCESS;
}

static NTSTATUS cmOsnPowerSend(PCM_OSC_MEMBER m, PDEVICE_OBJECT lower,
                               PIRP irp)
{
    PoStartNextPowerIrp(irp);
    IoCopyCurrentIrpStackLocationToNext(irp);
    CM_SET_COMPLETION_ALWAYS(irp, cmOsnPowerDone, m);
    return PoCallDriver(lower, irp);
}

/*
 * A member's power request: untracked ones pass at once; a device
 * SET_POWER waits for the exchange (PASSIVE_LEVEL) or, at DISPATCH_LEVEL,
 * is queued until it ends; then it is counted from here until it returns.
 */
NTSTATUS CmOscmPowerPass(PCM_OSC_MEMBER m, PDEVICE_OBJECT lower, PIRP irp)
{
    KIRQL irql;
    BOOLEAN passive;

    if (m->Role == CM_OSCP_R_NONE || !CmOscmTracked(irp)) {
        PoStartNextPowerIrp(irp);
        IoSkipCurrentIrpStackLocation(irp);
        return PoCallDriver(lower, irp);
    }
    passive = (BOOLEAN)(KeGetCurrentIrql() == PASSIVE_LEVEL);
    /* Gone and the member's count taken together: once the removal has
     * begun no request takes the member again, so its drain is final. */
    KeAcquireSpinLock(&cmOsnLock, &irql);
    if (m->Gone) {
        KeReleaseSpinLock(&cmOsnLock, irql);
        PoStartNextPowerIrp(irp);
        IoSkipCurrentIrpStackLocation(irp);
        return PoCallDriver(lower, irp);
    }
    m->InFlight++;
    if (passive) {
        while (cmOsnHolding) {
            KeReleaseSpinLock(&cmOsnLock, irql);
            (VOID)KeWaitForSingleObject(&cmOsnOpen, Executive, KernelMode,
                                        FALSE, NULL);
            KeAcquireSpinLock(&cmOsnLock, &irql);
        }
        cmOsnAdmit();
        KeReleaseSpinLock(&cmOsnLock, irql);
        return cmOsnPowerSend(m, lower, irp);
    }
    if (cmOsnHolding) {
        irp->Tail.Overlay.DriverContext[0] = m;
        irp->Tail.Overlay.DriverContext[1] = lower;
        IoMarkIrpPending(irp);
        InsertTailList(&cmOsnQueue, &irp->Tail.Overlay.ListEntry);
        KeReleaseSpinLock(&cmOsnLock, irql);
        return STATUS_PENDING;
    }
    cmOsnAdmit();
    KeReleaseSpinLock(&cmOsnLock, irql);
    return cmOsnPowerSend(m, lower, irp);
}

/* ---- the members' own objects (the upstream port, the xHCI) ------------ */

NTSTATUS CmOscmAddDevice(PDRIVER_OBJECT driver, PDEVICE_OBJECT pdo,
                         ULONG role)
{
    PDEVICE_OBJECT self;
    PDEVICE_OBJECT lower;
    PCM_MEMBER mb;
    NTSTATUS status;

    status = IoCreateDevice(driver, sizeof(CM_MEMBER), NULL,
                            FILE_DEVICE_UNKNOWN, 0, FALSE, &self);
    if (!NT_SUCCESS(status)) {
        /* A failed filter AddDevice fails the device's start (decision
         * M7): no object, the stack built without it, coverage refused. */
        return STATUS_SUCCESS;
    }
    mb = (PCM_MEMBER)self->DeviceExtension;
    CmZero(mb, sizeof(CM_MEMBER));
    lower = IoAttachDeviceToDeviceStack(self, pdo);
    if (lower == NULL) {
        IoDeleteDevice(self);
        return STATUS_SUCCESS;
    }
    mb->Common.Kind = CM_KIND_MEMBER;
    mb->Common.Self = self;
    mb->Common.Pdo = pdo;
    mb->Common.Lower = lower;
    CmOscmInit(&mb->Osc, role, pdo, lower);
    self->DeviceType = lower->DeviceType;
    self->Characteristics = lower->Characteristics;
    self->Flags |= lower->Flags &
                   (DO_BUFFERED_IO | DO_DIRECT_IO | DO_POWER_PAGABLE |
                    DO_POWER_INRUSH);
    self->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

/* <= DISPATCH_LEVEL (a completion routine). */
static NTSTATUS NTAPI cmOsnUsageDone(PDEVICE_OBJECT device, PIRP irp,
                                     PVOID context)
{
    PCM_MEMBER mb;
    KIRQL irql;

    UNREFERENCED_PARAMETER(context);
    mb = (PCM_MEMBER)device->DeviceExtension;
    if (irp->PendingReturned) {
        IoMarkIrpPending(irp);
    }
    if ((mb->Common.Lower->Flags & DO_POWER_PAGABLE) == 0) {
        device->Flags &= ~DO_POWER_PAGABLE;
    }
    /* The last access: the removal may delete the member after it. */
    KeAcquireSpinLock(&cmOsnLock, &irql);
    mb->Osc.Usage--;
    KeReleaseSpinLock(&cmOsnLock, irql);
    return STATUS_SUCCESS;
}

NTSTATUS CmOscmPnp(PCM_MEMBER mb, PIRP irp)
{
    PIO_STACK_LOCATION stack;
    PDEVICE_OBJECT self;
    NTSTATUS status;
    KIRQL irql;
    UCHAR minor;

    stack = IoGetCurrentIrpStackLocation(irp);
    minor = stack->MinorFunction;
    switch (minor) {
    case IRP_MN_DEVICE_USAGE_NOTIFICATION:
        /* Counted until its completion's last access (it can reach the
         * stack from a child's outside PnP's order); refused once the
         * member's removal has begun. */
        KeAcquireSpinLock(&cmOsnLock, &irql);
        if (mb->Osc.Gone) {
            KeReleaseSpinLock(&cmOsnLock, irql);
            return CmCompleteIrp(irp, STATUS_DELETE_PENDING, 0);
        }
        mb->Osc.Usage++;
        KeReleaseSpinLock(&cmOsnLock, irql);
        self = mb->Common.Self;
        if (self->AttachedDevice == NULL ||
            (self->AttachedDevice->Flags & DO_POWER_PAGABLE) != 0) {
            self->Flags |= DO_POWER_PAGABLE;
        }
        IoCopyCurrentIrpStackLocationToNext(irp);
        CM_SET_COMPLETION_ALWAYS(irp, cmOsnUsageDone, NULL);
        return IoCallDriver(mb->Common.Lower, irp);

    case IRP_MN_START_DEVICE:
    case IRP_MN_STOP_DEVICE:
    case IRP_MN_QUERY_STOP_DEVICE:
    case IRP_MN_CANCEL_STOP_DEVICE:
    case IRP_MN_QUERY_REMOVE_DEVICE:
    case IRP_MN_CANCEL_REMOVE_DEVICE:
        CmOscmEnter(&mb->Osc);
        status = CmForwardSync(&mb->Common, irp);
        CmOscmAfter(&mb->Osc, minor, status);
        CmOscmLeave(&mb->Osc);
        return CmCompleteIrp(irp, status, irp->IoStatus.Information);

    case IRP_MN_SURPRISE_REMOVAL:
        CmOscmSurprise(&mb->Osc);
        break;

    case IRP_MN_REMOVE_DEVICE:
        CmOscmEnter(&mb->Osc);
        CmOscmRemove(&mb->Osc);
        CmOscmLeave(&mb->Osc);
        IoSkipCurrentIrpStackLocation(irp);
        status = IoCallDriver(mb->Common.Lower, irp);
        IoDetachDevice(mb->Common.Lower);
        IoDeleteDevice(mb->Common.Self);
        return status;

    default:
        break;
    }
    IoSkipCurrentIrpStackLocation(irp);
    return IoCallDriver(mb->Common.Lower, irp);
}

NTSTATUS CmOscmPower(PCM_MEMBER mb, PIRP irp)
{
    return CmOscmPowerPass(&mb->Osc, mb->Common.Lower, irp);
}

/* ---- the hold (12.8.15.7) ---------------------------------------------- */

/* Holds the members: TRUE once none has a tracked request in flight,
 * waited for at most CM_OSN_WAIT_MS; FALSE (released) otherwise. */
static BOOLEAN cmOsnHold(PCM_NHI nhi)
{
    LARGE_INTEGER timeout;
    KIRQL irql;
    NTSTATUS status;

    KeAcquireSpinLock(&cmOsnLock, &irql);
    cmOsnHolding = 1;
    cmOsnAbort = 0;
    KeClearEvent(&cmOsnOpen);
    KeReleaseSpinLock(&cmOsnLock, irql);
    timeout.QuadPart = -(LONGLONG)(CM_OSN_WAIT_MS * 10000UL);
    status = KeWaitForSingleObject(&cmOsnQuiet, Executive, KernelMode, FALSE,
                                   &timeout);
    if (status == STATUS_SUCCESS) {
        return TRUE;
    }
    CmLogNote(nhi, "cm.osc.busy", (ULONG)cmOsnInFlight);
    return FALSE;
}

/* The hold released: the waiters go on, the queued requests are sent. */
static VOID cmOsnRelease(VOID)
{
    LIST_ENTRY queued;
    PLIST_ENTRY e;
    PIRP irp;
    PCM_OSC_MEMBER m;
    KIRQL irql;

    InitializeListHead(&queued);
    KeAcquireSpinLock(&cmOsnLock, &irql);
    cmOsnHolding = 0;
    while (!IsListEmpty(&cmOsnQueue)) {
        e = RemoveHeadList(&cmOsnQueue);
        cmOsnAdmit();
        InsertTailList(&queued, e);
    }
    KeSetEvent(&cmOsnOpen, IO_NO_INCREMENT, FALSE);
    KeReleaseSpinLock(&cmOsnLock, irql);
    while (!IsListEmpty(&queued)) {
        e = RemoveHeadList(&queued);
        irp = CONTAINING_RECORD(e, IRP, Tail.Overlay.ListEntry);
        m = (PCM_OSC_MEMBER)irp->Tail.Overlay.DriverContext[0];
        (VOID)cmOsnPowerSend(m,
                             (PDEVICE_OBJECT)irp->Tail.Overlay.DriverContext[1],
                             irp);
    }
}

/* Before the hold: the upstream port member at `usp`, started, whose
 * interface its start could not take, asked for it again from the thread
 * (the query's status logged either way). PASSIVE_LEVEL. */
static VOID cmOsnRetakeBus(PCM_NHI nhi, ULONG usp)
{
    PCM_OSC_MEMBER m;
    KIRQL irql;
    ULONG i;
    BOOLEAN ok;

    m = NULL;
    KeAcquireSpinLock(&cmOsnLock, &irql);
    for (i = 0; i < CM_OSN_MEMBERS && m == NULL; i++) {
        if (cmOsnList[i] != NULL && cmOsnList[i]->Role == CM_OSCP_R_USP &&
            cmOsnList[i]->Located && cmOsnList[i]->Bdf == usp &&
            cmOsnList[i]->Started && !cmOsnList[i]->Gone) {
            m = cmOsnList[i];
            if (m->BusHeld || m->Uses != 0) {
                m = NULL;
                break;
            }
            /* Held against its removal while the query runs. */
            m->Uses++;
            KeClearEvent(&m->Unused);
        }
    }
    KeReleaseSpinLock(&cmOsnLock, irql);
    if (m == NULL) {
        return;
    }
    CmLogNote(nhi, "cm.osc.bus.start", m->BusStatus);
    ok = cmOsnTakeBus(m);
    CmLogNote(nhi, "cm.osc.bus.again", m->BusStatus);
    KeAcquireSpinLock(&cmOsnLock, &irql);
    if (ok) {
        m->BusHeld = 1;
    }
    if (--m->Uses == 0) {
        KeSetEvent(&m->Unused, IO_NO_INCREMENT, FALSE);
    }
    KeReleaseSpinLock(&cmOsnLock, irql);
}

/* A use of the upstream port member at `usp` (its interface held across
 * the exchange), or NULL. */
static PCM_OSC_MEMBER cmOsnUse(ULONG usp)
{
    PCM_OSC_MEMBER m;
    KIRQL irql;
    ULONG i;

    m = NULL;
    KeAcquireSpinLock(&cmOsnLock, &irql);
    for (i = 0; i < CM_OSN_MEMBERS && m == NULL; i++) {
        if (cmOsnList[i] != NULL && cmOsnList[i]->Role == CM_OSCP_R_USP &&
            cmOsnList[i]->Located && cmOsnList[i]->Bdf == usp &&
            cmOsnList[i]->Started && cmOsnList[i]->BusHeld &&
            !cmOsnList[i]->Gone) {
            m = cmOsnList[i];
            if (m->Uses++ == 0) {
                KeClearEvent(&m->Unused);
            }
        }
    }
    KeReleaseSpinLock(&cmOsnLock, irql);
    return m;
}

static VOID cmOsnUnuse(PCM_OSC_MEMBER m)
{
    KIRQL irql;

    KeAcquireSpinLock(&cmOsnLock, &irql);
    if (--m->Uses == 0) {
        KeSetEvent(&m->Unused, IO_NO_INCREMENT, FALSE);
    }
    KeReleaseSpinLock(&cmOsnLock, irql);
}

/* A surprise removal of a member, or of the NHI itself (its revocation,
 * CmNhiRevoke): the exchange stops before its next mailbox access. */
static BOOLEAN cmOsnAborted(PCM_NHI nhi)
{
    return (BOOLEAN)(cmOsnAbort != 0 || !CmNhiHwAccess(nhi));
}

/* ---- the route ----------------------------------------------------------- */

/* A dword through the interface: TRUE with exactly 4 bytes read. */
static BOOLEAN cmOsnRd(PCM_NHI nhi, PCM_OSC_MEMBER m, ULONG off, PULONG v)
{
    ULONG got;

    *v = 0xFFFFFFFFUL;
    if (cmOsnAborted(nhi) || (cmOsnNoExt && off >= 0x100UL)) {
        return FALSE;
    }
    got = m->Bus.GetBusData(m->Bus.Context, PCI_WHICHSPACE_CONFIG, v, off,
                            4);
    return (BOOLEAN)(got == 4UL);
}

static BOOLEAN cmOsnAlias(PCM_NHI nhi, PCM_OSC_MEMBER m, BOOLEAN *aliased);

static BOOLEAN cmOsnWr(PCM_NHI nhi, PCM_OSC_MEMBER m, ULONG off, ULONG v)
{
    if (cmOsnAborted(nhi) || (cmOsnNoExt && off >= 0x100UL)) {
        return FALSE;
    }
    return (BOOLEAN)(m->Bus.SetBusData(m->Bus.Context, PCI_WHICHSPACE_CONFIG,
                                       &v, off, 4) == 4UL);
}

/* The mailbox's access (12.8.15.18): through the upstream port's bus
 * interface, or, where Va is not NULL, its two dwords (and the reads of
 * CmOscpEcamOffset) through the ECAM page, which a hold keeps mapped. */
typedef struct _CM_OSN_MB {
    PCM_OSC_MEMBER M;
    PUCHAR Va;
} CM_OSN_MB;

/* A dword of the ECAM page `va`, at an offset CmOscpEcamOffset admits. */
static BOOLEAN cmOsnEcRd(PCM_NHI nhi, PUCHAR va, ULONG off, PULONG v)
{
    *v = 0xFFFFFFFFUL;
    if (va == NULL || cmOsnAborted(nhi) || !CmOscpEcamOffset(off, FALSE)) {
        return FALSE;
    }
    *v = READ_REGISTER_ULONG((PULONG)(va + off));
    return TRUE;
}

static BOOLEAN cmOsnMbRd(PCM_NHI nhi, const CM_OSN_MB *mb, ULONG off,
                         PULONG v)
{
    if (mb->Va != NULL) {
        return cmOsnEcRd(nhi, mb->Va, off, v);
    }
    return cmOsnRd(nhi, mb->M, off, v);
}

static BOOLEAN cmOsnMbWr(PCM_NHI nhi, const CM_OSN_MB *mb, ULONG off,
                         ULONG v)
{
    if (mb->Va != NULL) {
        if (cmOsnAborted(nhi) || !CmOscpEcamOffset(off, TRUE)) {
            return FALSE;
        }
        WRITE_REGISTER_ULONG((PULONG)(mb->Va + off), v);
        return TRUE;
    }
    return cmOsnWr(nhi, mb->M, off, v);
}

/* The power state through the route (12.8.4): TRUE in D0. */
static BOOLEAN cmOsnD0(PCM_NHI nhi, PCM_OSC_MEMBER m)
{
    ULONG v;
    ULONG at;
    ULONG i;

    if (!cmOsnRd(nhi, m, 0x34, &v)) {
        return FALSE;
    }
    at = v & 0xFFUL;
    for (i = 0; i < 48UL && at >= 0x40UL && (at & 3UL) == 0; i++) {
        if (!cmOsnRd(nhi, m, at, &v)) {
            return FALSE;
        }
        if ((v & 0xFFUL) == 0x01UL) {
            return (BOOLEAN)(cmOsnRd(nhi, m, at + 4UL, &v) &&
                             (v & 3UL) == 0);
        }
        at = (v >> 8) & 0xFFUL;
    }
    return FALSE;
}

#define CM_OSN_X_GONE       0xFFFFFFFFUL
#define CM_OSN_X_REFUSED    0xFFFFFFFEUL
#define CM_OSN_X_NO_REPLY   0xFFFFFFFDUL
#define CM_OSN_X_ABORTED    0xFFFFFFFCUL

/* One exchange (12.8.5) through the route: the command written, its
 * acceptance and the reply waited for, each within CM_OSN_WAIT_MS; on no
 * reply the reply dword polled CM_OSN_WAIT_MS more and cleared if one
 * comes (12.8.15.7), never retried. Returns the reply or CM_OSN_X_*. */
static ULONG cmOsnExchange(PCM_NHI nhi, const CM_OSN_MB *m, ULONG cmd,
                           CM_OSN_HOST *h)
{
    ULONG start;
    ULONG v;
    ULONG i;

    if (!cmOsnMbWr(nhi, m, CM_OSC_MB_CMD, cmd)) {
        return cmOsnAborted(nhi) ? CM_OSN_X_ABORTED : CM_OSN_X_GONE;
    }
    start = nhi->Host.NowMs(nhi->Host.Context);
    for (i = 0;; i++) {
        if (!cmOsnMbRd(nhi, m, CM_OSC_MB_CMD, &v)) {
            return cmOsnAborted(nhi) ? CM_OSN_X_ABORTED : CM_OSN_X_GONE;
        }
        if (v == 0xFFFFFFFFUL) {
            return CM_OSN_X_GONE;
        }
        if (!CM_OSC_MB_BUSY(v)) {
            break;
        }
        if (i >= CM_OSN_WAIT_MS ||
            nhi->Host.NowMs(nhi->Host.Context) - start >= CM_OSN_WAIT_MS) {
            CmLogNote(nhi, "cm.osc.not-accepted", v);
            break;
        }
        KeStallExecutionProcessor(1000);
    }
    if (CM_OSC_MB_BUSY(v)) {
        /* Not accepted in time: a late acceptance's reply is cleared if
         * it comes within the extra wait, and not taken. */
        h->TimedOut = 1;
        CmLogNote(nhi, "cm.osc.timeout", 1);
        start = nhi->Host.NowMs(nhi->Host.Context);
        for (i = 0; i < CM_OSN_WAIT_MS; i++) {
            if (!cmOsnMbRd(nhi, m, CM_OSC_MB_REPLY, &v) ||
                v == 0xFFFFFFFFUL) {
                break;
            }
            if (CM_OSC_MB_READY(v)) {
                (VOID)cmOsnMbWr(nhi, m, CM_OSC_MB_REPLY, 0);
                CmLogNote(nhi, "cm.osc.timeout.reply", v);
                break;
            }
            if (nhi->Host.NowMs(nhi->Host.Context) - start >=
                CM_OSN_WAIT_MS) {
                break;
            }
            KeStallExecutionProcessor(1000);
        }
        return CM_OSN_X_REFUSED;
    }
    CmLogNote(nhi, "cm.osc.accepted-ms", i);
    start = nhi->Host.NowMs(nhi->Host.Context);
    for (i = 0;; i++) {
        if (!cmOsnMbRd(nhi, m, CM_OSC_MB_REPLY, &v)) {
            return cmOsnAborted(nhi) ? CM_OSN_X_ABORTED : CM_OSN_X_GONE;
        }
        if (v == 0xFFFFFFFFUL) {
            return CM_OSN_X_GONE;
        }
        if (CM_OSC_MB_READY(v)) {
            break;
        }
        if (i >= 2UL * CM_OSN_WAIT_MS ||
            nhi->Host.NowMs(nhi->Host.Context) - start >=
                2UL * CM_OSN_WAIT_MS) {
            h->TimedOut = 1;
            CmLogNote(nhi, "cm.osc.timeout", 2);
            CmLogNote(nhi, "cm.osc.timeout.reply", v);
            return CM_OSN_X_NO_REPLY;
        }
        KeStallExecutionProcessor(1000);
    }
    (VOID)cmOsnMbWr(nhi, m, CM_OSC_MB_REPLY, 0);
    if (i >= CM_OSN_WAIT_MS) {
        /* The reply came in the extra wait: the timeout stands, the reply
         * cleared and not taken. */
        h->TimedOut = 1;
        CmLogNote(nhi, "cm.osc.timeout", 3);
        CmLogNote(nhi, "cm.osc.timeout.reply", v);
        return CM_OSN_X_NO_REPLY;
    }
    CmLogNote(nhi, "cm.osc.reply-ms", i);
    return v;
}

/* ---- the host and the board ------------------------------------------------ */

typedef struct _CM_OSN_FOUND {
    ULONG NhiBdf;
    ULONG DspBdf;
    ULONG UspBdf;
    ULONG UspSec;
    ULONG RootBdf;
    CM_OSCP_PCI Pci;
} CM_OSN_FOUND;

/* The members' view, a snapshot under the lock: up to CM_OSN_MEMBERS. */
static ULONG cmOsnSnapshot(CM_OSCP_MEMBER *out, ULONG *sec)
{
    KIRQL irql;
    ULONG i;
    ULONG n;

    n = 0;
    KeAcquireSpinLock(&cmOsnLock, &irql);
    for (i = 0; i < CM_OSN_MEMBERS; i++) {
        if (cmOsnList[i] != NULL) {
            out[n].Role = cmOsnList[i]->Role;
            out[n].Bdf = cmOsnList[i]->Bdf;
            out[n].Located = cmOsnList[i]->Located;
            out[n].Started = cmOsnList[i]->Started;
            out[n].Closed = cmOsnList[i]->Closed;
            out[n].Gone = cmOsnList[i]->Gone;
            sec[n] = cmOsnList[i]->Secondary;
            n++;
        }
    }
    KeReleaseSpinLock(&cmOsnLock, irql);
    return n;
}

/* The one located member of `role` whose secondary bus is `bus`: its
 * index in the snapshot, or n when none or two. */
static ULONG cmOsnAbove(const CM_OSCP_MEMBER *v, const ULONG *sec, ULONG n,
                        ULONG role, ULONG bus)
{
    ULONG i;
    ULONG at;
    ULONG found;

    at = n;
    found = 0;
    for (i = 0; i < n; i++) {
        if (v[i].Role == role && v[i].Located && !v[i].Gone &&
            sec[i] == bus) {
            at = i;
            found++;
        }
    }
    return found == 1 ? at : n;
}

/* The NHI's location: its PDO's, checked against its ids. */
static ULONG cmOsnNhiBdf(PCM_NHI nhi)
{
    ULONG bus;
    ULONG address;
    ULONG bytes;
    ULONG bdf;

    bus = 0;
    address = 0;
    if (!NT_SUCCESS(IoGetDeviceProperty(nhi->Common.Pdo,
                                        DevicePropertyBusNumber, sizeof(bus),
                                        &bus, &bytes)) ||
        !NT_SUCCESS(IoGetDeviceProperty(nhi->Common.Pdo,
                                        DevicePropertyAddress,
                                        sizeof(address), &address, &bytes)) ||
        bus > 0xFFUL || (address >> 16) > 0x1FUL ||
        (address & 0xFFFFUL) > 7UL) {
        return XHCI_TBT_NO_BDF;
    }
    bdf = XHCI_TBT_BDF(bus, address >> 16, address & 0xFFFFUL);
    return cmOsnId(bdf) == CM_OSCP_ID_NHI ? bdf : XHCI_TBT_NO_BDF;
}

/* The root port: the one type 1 function on a bus below the upstream
 * port's whose secondary bus is the upstream port's (a bounded scan of
 * those buses), a root port by its PCI Express capability. */
static ULONG cmOsnRoot(ULONG uspBus)
{
    ULONG b;
    ULONG dev;
    ULONG fn;
    ULONG v;
    ULONG hdr;
    ULONG r;
    ULONG at;
    ULONG found;
    ULONG cap;

    at = XHCI_TBT_NO_BDF;
    found = 0;
    for (b = 0; b < uspBus; b++) {
        for (dev = 0; dev < 32UL; dev++) {
            for (fn = 0; fn < 8UL; fn++) {
                r = cmOsnCfgEx(XHCI_TBT_BDF(b, dev, fn), 0, &v);
                if (r == CM_OSN_CFG_NO_BUS) {
                    dev = 32UL;
                    break;
                }
                if (r != CM_OSN_CFG_OK || v == 0xFFFFFFFFUL ||
                    (v & 0xFFFFUL) == 0xFFFFUL) {
                    if (fn == 0) {
                        break;
                    }
                    continue;
                }
                if (!cmOsnCfg(XHCI_TBT_BDF(b, dev, fn), 0x0C, &hdr)) {
                    return XHCI_TBT_NO_BDF;
                }
                if (((hdr >> 16) & 0x7FUL) == 1UL &&
                    cmOsnCfg(XHCI_TBT_BDF(b, dev, fn), 0x18, &v) &&
                    ((v >> 8) & 0xFFUL) == uspBus) {
                    at = XHCI_TBT_BDF(b, dev, fn);
                    found++;
                }
                if (fn == 0 && ((hdr >> 16) & 0x80UL) == 0) {
                    break;
                }
            }
        }
    }
    if (found != 1) {
        return XHCI_TBT_NO_BDF;
    }
    cap = cmOsnCap(at, 0x10);
    if (cap == 0 || !cmOsnCfg(at, cap, &v) || ((v >> 20) & 0xFUL) != 4UL) {
        return XHCI_TBT_NO_BDF;
    }
    return at;
}

/* 12.8.3 step 1 on NT: the host's functions by bus, the downstream and
 * upstream ports from the members' own locations. 0 or the refusal. */
static ULONG cmOsnHostByBus(PCM_NHI nhi, CM_OSN_FOUND *f)
{
    CM_OSCP_MEMBER v[CM_OSN_MEMBERS];
    ULONG sec[CM_OSN_MEMBERS];
    ULONG n;
    ULONG d;
    ULONG u;
    ULONG s;

    CmZero(f, sizeof(*f));
    f->NhiBdf = cmOsnNhiBdf(nhi);
    if (f->NhiBdf == XHCI_TBT_NO_BDF) {
        return 0x100UL;
    }
    n = cmOsnSnapshot(v, sec);
    d = cmOsnAbove(v, sec, n, CM_OSCP_R_DSP, XHCI_TBT_BDF_BUS(f->NhiBdf));
    if (d == n) {
        return 0x101UL;
    }
    f->DspBdf = v[d].Bdf;
    u = cmOsnAbove(v, sec, n, CM_OSCP_R_USP, XHCI_TBT_BDF_BUS(f->DspBdf));
    if (u == n || (v[u].Bdf & 0xFFUL) != 0) {
        return 0x102UL;
    }
    f->UspBdf = v[u].Bdf;
    f->UspSec = sec[u];
    f->RootBdf = cmOsnRoot(XHCI_TBT_BDF_BUS(f->UspBdf));
    if (f->RootBdf == XHCI_TBT_NO_BDF) {
        return 0x103UL;
    }
    f->Pci.RootId = cmOsnId(f->RootBdf);
    f->Pci.UspId = cmOsnId(f->UspBdf);
    f->Pci.DspId = cmOsnId(f->DspBdf);
    f->Pci.NhiId = cmOsnId(f->NhiBdf);
    if (!cmOsnCfg(f->NhiBdf, 0x2C, &s)) {
        return 0x100UL;
    }
    f->Pci.NhiSubsystem = s;
    return 0;
}

/* {8F680850-A584-11D1-BF38-00A0C9062910}: MSSmBios_RawSMBiosTables. */
static GUID cmOsnSmbiosGuid = {
    0x8F680850UL, 0xA584, 0x11D1,
    { 0xBF, 0x38, 0x00, 0xA0, 0xC9, 0x06, 0x29, 0x10 }
};

/* The SMBIOS identity through WMI (12.8.15.11): CM_OSCP_OK, or a reason
 * (a CM_OSCP_* of the decode, or 0x100 + a step that failed). */
static ULONG cmOsnSmbios(PCM_NHI nhi, CM_OSCP_SMBIOS *sm)
{
    PVOID obj;
    UCHAR *buf;
    const UCHAR *t;
    ULONG size;
    ULONG got;
    ULONG tlen;
    ULONG r;
    NTSTATUS status;

    obj = NULL;
    if (cmOsnWmiOpen == NULL) {
        /* 12.8.15.17: no WMI routines (2000): an unknown board. */
        CmLogNote(nhi, "cm.osc.wmi.none", 0);
        return 0x100UL;
    }
    status = cmOsnWmiOpen(&cmOsnSmbiosGuid, 0x0001UL, &obj);
    if (!NT_SUCCESS(status) || obj == NULL) {
        CmLogNote(nhi, "cm.osc.wmi.open", (ULONG)status);
        return 0x101UL;
    }
    size = 0;
    status = cmOsnWmiQuery(obj, &size, NULL);
    if (status != STATUS_BUFFER_TOO_SMALL || size == 0 ||
        size > CM_OSN_WMI_MAX) {
        CmLogNote(nhi, "cm.osc.wmi.size", (ULONG)status);
        CmLogNote(nhi, "cm.osc.wmi.bytes", size);
        ObDereferenceObject(obj);
        return 0x102UL;
    }
    buf = (UCHAR *)CmAlloc(size);
    if (buf == NULL) {
        ObDereferenceObject(obj);
        return 0x103UL;
    }
    got = size;
    status = cmOsnWmiQuery(obj, &got, buf);
    ObDereferenceObject(obj);
    if (!NT_SUCCESS(status) || got > size) {
        CmLogNote(nhi, "cm.osc.wmi.query", (ULONG)status);
        CmFree(buf);
        return 0x104UL;
    }
    r = CmOscpWmiRaw(buf, got, &t, &tlen);
    if (r == CM_OSCP_OK) {
        r = CmOscpSmbios(t, tlen, sm);
    }
    CmFree(buf);
    return r;
}

/* The board's identity and its profile (12.8.15.11): the index + 1 of the
 * profile matched with its data byte, or 0 (unknown, the reason logged). */
static ULONG cmOsnMatch(PCM_NHI nhi, const CM_OSN_FOUND *f, PULONG data)
{
    CM_OSCP_SMBIOS sm;
    ULONG r;
    ULONG p;

    CmLogNote(nhi, "cm.osc.id.root", f->Pci.RootId);
    CmLogNote(nhi, "cm.osc.id.usp", f->Pci.UspId);
    CmLogNote(nhi, "cm.osc.id.dsp", f->Pci.DspId);
    CmLogNote(nhi, "cm.osc.id.nhi", f->Pci.NhiId);
    CmLogNote(nhi, "cm.osc.id.subsystem", f->Pci.NhiSubsystem);
    CmZero(&sm, sizeof(sm));
    r = cmOsnSmbios(nhi, &sm);
    if (r != CM_OSCP_OK) {
        CmLogNote(nhi, "cm.osc.smbios.refused", r);
        return 0;
    }
    CmLogNote(nhi, "cm.osc.sm.vendor", sm.BiosVendor);
    CmLogNote(nhi, "cm.osc.sm.version", sm.BiosVersion);
    CmLogNote(nhi, "cm.osc.sm.date", sm.BiosDate);
    CmLogNote(nhi, "cm.osc.sm.maker", sm.BoardMaker);
    CmLogNote(nhi, "cm.osc.sm.product", sm.BoardProduct);
    p = CmOscpMatch(&f->Pci, &sm, data);
    CmLogNote(nhi, "cm.osc.profile", p);
    return p;
}

/* Every function of the host but the NHI, wanted covered (12.8.15.7):
 * the upstream port, each function on its secondary bus (a downstream
 * port), and the xHCI behind one of them. FALSE when the set cannot be
 * read or is not the switch's. */
/* A function's id through the HAL: TRUE with *id (0 absent), FALSE when
 * the read failed (neither present nor absent). */
static BOOLEAN cmOsnIdEx(ULONG bdf, PULONG id)
{
    ULONG v;
    ULONG r;

    *id = 0;
    r = cmOsnCfgEx(bdf, 0, &v);
    if (r == CM_OSN_CFG_ABSENT || r == CM_OSN_CFG_NO_BUS) {
        return TRUE;
    }
    if (r != CM_OSN_CFG_OK) {
        return FALSE;
    }
    if (v != 0xFFFFFFFFUL && (v & 0xFFFFUL) != 0xFFFFUL) {
        *id = v;
    }
    return TRUE;
}

/* Every function of the host but the NHI, wanted covered (12.8.15.7):
 * the upstream port, its four downstream ports (every function on its
 * secondary bus, each one of them), and the one xHCI behind one of them,
 * the NHI behind another. FALSE (logged by the caller) when any read
 * fails or the set is not exactly the switch's. */
static BOOLEAN cmOsnWanted(const CM_OSN_FOUND *f, ULONG *want, ULONG *role,
                           PULONG count)
{
    ULONG dev;
    ULONG fn;
    ULONG bdf;
    ULONG id;
    ULONG hdr;
    ULONG v;
    ULONG n;
    ULONG dsp;
    ULONG xhci;
    ULONG nhi;

    n = 0;
    dsp = 0;
    xhci = 0;
    nhi = 0;
    want[n] = f->UspBdf;
    role[n++] = CM_OSCP_R_USP;
    for (dev = 0; dev < 32UL; dev++) {
        for (fn = 0; fn < 8UL; fn++) {
            bdf = XHCI_TBT_BDF(f->UspSec, dev, fn);
            if (!cmOsnIdEx(bdf, &id)) {
                return FALSE;
            }
            if (id == 0) {
                if (fn == 0) {
                    break;
                }
                continue;
            }
            if (id != CM_OSCP_ID_DSP || n + 2UL > CM_OSN_WANT ||
                !cmOsnCfg(bdf, 0x18, &v)) {
                return FALSE;
            }
            want[n] = bdf;
            role[n++] = CM_OSCP_R_DSP;
            dsp++;
            if (!cmOsnIdEx(XHCI_TBT_BDF((v >> 8) & 0xFFUL, 0, 0), &id)) {
                return FALSE;
            }
            if (id == CM_OSCP_ID_XHCI) {
                want[n] = XHCI_TBT_BDF((v >> 8) & 0xFFUL, 0, 0);
                role[n++] = CM_OSCP_R_XHCI;
                xhci++;
            } else if (id == CM_OSCP_ID_NHI) {
                nhi++;
            }
            if (fn == 0) {
                if (!cmOsnCfg(bdf, 0x0C, &hdr)) {
                    return FALSE;
                }
                if (((hdr >> 16) & 0x80UL) == 0) {
                    break;
                }
            }
        }
    }
    if (dsp != 4UL || xhci != 1UL || nhi != 1UL) {
        return FALSE;
    }
    *count = n;
    return TRUE;
}

/* ---- once per boot (12.8.13.5) ------------------------------------------- */

/* Served this boot, by this load (the static table) or an earlier one (the
 * object): TRUE, the table marked. Under cmOsnMutex. */
static BOOLEAN cmOsnServed(ULONG usp)
{
    KIRQL irql;
    ULONG i;
    BOOLEAN served;
    BOOLEAN ok;

    KeAcquireSpinLock(&cmOsnLock, &irql);
    served = (BOOLEAN)(cmOsnHostOf(usp) != NULL);
    KeReleaseSpinLock(&cmOsnLock, irql);
    if (served) {
        return TRUE;
    }
    if (CmNvOscObject(usp, FALSE, FALSE) != CM_NVO_FOUND) {
        return FALSE;
    }
    ok = (BOOLEAN)(CmNvOscObject(usp, TRUE, FALSE) == CM_NVO_FOUND);
    KeAcquireSpinLock(&cmOsnLock, &irql);
    for (i = 0; i < CM_OSN_HOSTS; i++) {
        if (cmOsnHost[i].Usp == 0) {
            cmOsnHost[i].Usp = usp + 1UL;
            cmOsnHost[i].Sent = 1;
            cmOsnHost[i].Ok = ok ? 1UL : 0UL;
            break;
        }
    }
    KeReleaseSpinLock(&cmOsnLock, irql);
    return TRUE;
}

/* Reserved for this boot: the table's entry and the object made. NULL
 * (logged) when no entry is free or the object failed. Under cmOsnMutex. */
static CM_OSN_HOST *cmOsnReserve(PCM_NHI nhi, ULONG usp)
{
    CM_OSN_HOST *h;
    KIRQL irql;
    ULONG i;

    h = NULL;
    KeAcquireSpinLock(&cmOsnLock, &irql);
    for (i = 0; i < CM_OSN_HOSTS; i++) {
        if (cmOsnHost[i].Usp == 0) {
            h = &cmOsnHost[i];
            CmZero(h, sizeof(*h));
            h->Usp = usp + 1UL;
            break;
        }
    }
    KeReleaseSpinLock(&cmOsnLock, irql);
    if (h == NULL) {
        CmLogNote(nhi, "cm.osc.refused", CM_OSN_R_OBJECT);
        return NULL;
    }
    if (CmNvOscObject(usp, FALSE, TRUE) != CM_NVO_MADE) {
        KeAcquireSpinLock(&cmOsnLock, &irql);
        h->Usp = 0;
        KeReleaseSpinLock(&cmOsnLock, irql);
        CmLogNote(nhi, "cm.osc.refused", CM_OSN_R_OBJECT);
        return NULL;
    }
    return h;
}

static VOID cmOsnLock0(VOID)
{
    (VOID)KeWaitForSingleObject(&cmOsnMutex, Executive, KernelMode, FALSE,
                                NULL);
}

static VOID cmOsnUnlock0(VOID)
{
    KeSetEvent(&cmOsnMutex, IO_NO_INCREMENT, FALSE);
}

/* ---- the boot guard (12.8.15.13) ------------------------------------------ */

/* The marker cleared after `event` (CM_OSCG_E_*), on the thread. */
static VOID cmOsnClear(PCM_NHI nhi, ULONG event)
{
    BOOLEAN deleted;
    BOOLEAN flushed;
    BOOLEAN written;
    ULONG r;

    if (nhi->OscArmed == 0 || !CmOscgClears(event)) {
        return;
    }
    CmStatusMarkerDelete(nhi, &deleted, &flushed);
    r = CmOscgCleared(deleted, flushed);
    CmLogNote(nhi, "cm.osc.guard.clear", (event << 8) | r);
    if (r == CM_OSCG_C_DONE) {
        nhi->OscArmed = 0;
        return;
    }
    if (r == CM_OSCG_C_LATCH) {
        cmOsnLatch = 1;
        CmStatusMarkerWrite(nhi, nhi->OscArmed, &written, &flushed);
        CmLogNote(nhi, "cm.osc.guard.rewrite",
                  (written ? 1UL : 0UL) | (flushed ? 2UL : 0UL));
        nhi->OscArmed = 0;
    }
}

/* At the boot's first start for a host: TRUE when this start may go on to
 * send, FALSE when the guard skips this boot (the entry reserved so no
 * start of this boot sends, the marker deleted). Under cmOsnMutex. */
static BOOLEAN cmOsnGuard(PCM_NHI nhi, ULONG usp)
{
    BOOLEAN deleted;
    BOOLEAN flushed;
    ULONG kind;
    ULONG at;
    ULONG i;
    ULONG slot;

    /* Read at the host's first start of the boot alone: a disable and
     * enable within it neither reads nor clears the marker. */
    slot = CM_OSN_HOSTS;
    for (i = 0; i < CM_OSN_HOSTS; i++) {
        if (cmOsnGuarded[i] == usp + 1UL) {
            if (cmOsnGuardSkip[i]) {
                /* Skipped this boot: every later start of it too. */
                CmLogNote(nhi, "cm.osc.guard", 0x400UL);
                nhi->OscFlags |= CM_OSCN_F_GUARD;
                return FALSE;
            }
            return TRUE;
        }
        if (cmOsnGuarded[i] == 0 && slot == CM_OSN_HOSTS) {
            slot = i;
        }
    }
    if (slot == CM_OSN_HOSTS) {
        /* No room to remember the reading: fail closed. */
        CmLogNote(nhi, "cm.osc.guard", 0x500UL);
        nhi->OscFlags |= CM_OSCN_F_GUARD;
        return FALSE;
    }
    cmOsnGuarded[slot] = usp + 1UL;
    kind = 0;
    at = CmOscgAtBoot(CmStatusMarkerRead(nhi, &kind));
    if (at == CM_OSCG_GO) {
        return TRUE;
    }
    cmOsnGuardSkip[slot] = 1;
    CmLogNote(nhi, "cm.osc.guard", (at << 8) | (kind & 0xFFUL));
    nhi->OscFlags |= CM_OSCN_F_GUARD;
    if (cmOsnReserve(nhi, usp) == NULL) {
        /* The skip would not survive a reload of this image: the marker
         * is kept, so a reload skips too (and the next boot once more). */
        CmLogNote(nhi, "cm.osc.guard.kept", 1);
        return FALSE;
    }
    if (at == CM_OSCG_SKIP_CLEAR) {
        CmStatusMarkerDelete(nhi, &deleted, &flushed);
        CmLogNote(nhi, "cm.osc.guard.deleted",
                  (deleted ? 1UL : 0UL) | (flushed ? 2UL : 0UL));
    }
    return FALSE;
}

/* ---- the send ----------------------------------------------------------- */

/*
 * The send of 12.8.15.11's sequence once the board is matched: the marker
 * armed, the members held, coverage, the route's checks and the mailbox's
 * state under the hold, the reservation (a start's) and the exchange.
 * `resume` an S4 resend (the entry already the host's, `h`). On the
 * ECAM systems (12.8.15.18) `ecamBase` and `ecamEnd` the base found this
 * start or this resume.
 */
static PUCHAR cmOsnEcamMap(PCM_NHI nhi, ULONG base, ULONG busEnd, ULONG bdf);
static VOID cmOsnEcamUnmap(PCM_NHI nhi, PUCHAR va);
static ULONG cmOsnEcamCheck(PCM_NHI nhi, PCM_OSC_MEMBER m, PUCHAR va,
                            const CM_OSN_FOUND *f);

static ULONG cmOsnSend(PCM_NHI nhi, const CM_OSN_FOUND *f, ULONG data,
                       BOOLEAN resume, CM_OSN_HOST *h, ULONG ecamBase,
                       ULONG ecamEnd)
{
    CM_OSN_MB mb;
    CM_OSCP_MEMBER v[CM_OSN_MEMBERS];
    ULONG sec[CM_OSN_MEMBERS];
    ULONG want[CM_OSN_WANT];
    ULONG role[CM_OSN_WANT];
    PCM_OSC_MEMBER usp;
    ULONG n;
    ULONG count;
    ULONG missing;
    ULONG reason;
    ULONG cmd;
    ULONG reply;
    ULONG x;
    KIRQL irql;
    LARGE_INTEGER settle;
    BOOLEAN written;
    BOOLEAN flushed;

    mb.M = NULL;
    mb.Va = NULL;
    if (cmOsnLatch) {
        CmLogNote(nhi, "cm.osc.refused", CM_OSN_R_GUARD);
        return CM_OSCN_DONE;
    }
    /* 12.8.15.13: armed and flushed before the hold, never inside it (a
     * start's key found flushable by the sequence first, 12.8.15.16). */
    CmStatusMarkerWrite(nhi, resume ? 2UL : 1UL, &written, &flushed);
    if (!CmOscgMaySend((BOOLEAN)(cmOsnLatch != 0), written, flushed)) {
        CmLogNote(nhi, "cm.osc.guard.write",
                  (written ? 1UL : 0UL) | (flushed ? 2UL : 0UL));
        CmLogNote(nhi, "cm.osc.refused", CM_OSN_R_GUARD);
        return CM_OSCN_DONE;
    }
    nhi->OscArmed = resume ? 2UL : 1UL;
    cmOsnRetakeBus(nhi, f->UspBdf);
    if (!cmOsnHold(nhi)) {
        cmOsnRelease();
        CmLogNote(nhi, "cm.osc.refused", CM_OSN_R_BUSY);
        return CM_OSCN_DONE;
    }
    reason = 0;
    usp = NULL;
    n = cmOsnSnapshot(v, sec);
    if (!cmOsnWanted(f, want, role, &count)) {
        reason = CM_OSN_R_COVERAGE;
        CmLogNote(nhi, "cm.osc.members.unread", f->UspSec);
    } else if (!CmOscpCovered(want, role, count, v, n, &missing)) {
        reason = CM_OSN_R_COVERAGE;
        CmLogNote(nhi, "cm.osc.member.missing",
                  (role[missing] << 16) | (want[missing] & 0xFFFFUL));
        if (cmOsnFull != 0) {
            CmLogNote(nhi, "cm.osc.members.full", cmOsnFull);
        }
    }
    if (reason == 0) {
        CmLogNote(nhi, "cm.osc.members", count);
        usp = cmOsnUse(f->UspBdf);
        if (usp == NULL) {
            reason = CM_OSN_R_NO_ENTRY;
        }
    }
    if (reason == 0) {
        CmLogNote(nhi, "cm.osc.route", usp->Bdf);
        /* The route reaches the port found by bus. */
        if (!cmOsnRd(nhi, usp, 0, &x) || x != f->Pci.UspId ||
            !cmOsnRd(nhi, usp, 0x18, &x) ||
            ((x >> 8) & 0xFFUL) != f->UspSec) {
            reason = CM_OSN_R_ROUTE;
        } else if (!cmOsnD0(nhi, usp)) {
            reason = 0x104UL;
        }
    }
    if (reason == 0 && cmOsnNoExt && !cmOsnEcamSys) {
        reason = CM_OSN_R_ALIAS;
    }
    if (reason == 0) {
        mb.M = usp;
        if (cmOsnEcamSys) {
            /* 12.8.15.18: the page mapped inside this hold alone, and
             * shown to be the port the route reads before the mailbox. */
            mb.Va = cmOsnEcamMap(nhi, ecamBase, ecamEnd, f->UspBdf);
            if (mb.Va == NULL) {
                reason = CM_OSN_R_ECAM;
            } else {
                reason = cmOsnEcamCheck(nhi, usp, mb.Va, f);
            }
        }
    }
    if (reason == 0) {
        BOOLEAN aliased;

        aliased = FALSE;
        if (!cmOsnMbRd(nhi, &mb, CM_OSC_MB_CMD, &x) || x == 0xFFFFFFFFUL ||
            !cmOsnMbRd(nhi, &mb, CM_OSC_MB_REPLY, &reply)) {
            reason = CM_OSN_R_MAILBOX;
        } else if (mb.Va == NULL &&
                   (!cmOsnAlias(nhi, usp, &aliased) || aliased)) {
            /* 12.8.15.16: no write where the offset may be truncated. */
            reason = CM_OSN_R_ALIAS;
        } else {
            CmLogNote(nhi, "cm.osc.mb.before", x);
            if (CM_OSC_MB_BUSY(x) || reply != 0) {
                reason = 0x107UL;
            }
        }
    }
    if (reason == 0 && !resume) {
        h = cmOsnReserve(nhi, f->UspBdf);
        if (h == NULL) {
            reason = CM_OSN_R_OBJECT;
        }
    }
    if (reason == 0) {
        KeAcquireSpinLock(&cmOsnLock, &irql);
        h->Sent = 1;
        KeReleaseSpinLock(&cmOsnLock, irql);
        cmd = CM_OSC_CMD_HANDOVER | ((data & 0xFFUL) << 16);
        CmLogNote(nhi, "cm.osc.resume", resume ? 1UL : 0UL);
        reply = cmOsnExchange(nhi, &mb, cmd, h);
        CmLogNote(nhi, "cm.osc.handover", reply);
        if (reply == CM_OSN_REPLY_OK) {
            if (!resume) {
                KeAcquireSpinLock(&cmOsnLock, &irql);
                h->Ok = 1;
                KeReleaseSpinLock(&cmOsnLock, irql);
                if (CmNvOscObject(f->UspBdf, TRUE, TRUE) != CM_NVO_MADE) {
                    CmLogNote(nhi, "cm.osc.object.ok", 0);
                }
            }
            reply = cmOsnExchange(nhi, &mb, CM_OSC_CMD_QUERY, h);
            CmLogNote(nhi, "cm.osc.query", reply);
        }
        if (reply == CM_OSN_X_ABORTED) {
            CmLogNote(nhi, "cm.osc.aborted", 1);
        }
        if (h->TimedOut) {
            nhi->OscFlags |= CM_OSCN_F_TIMEOUT;
        }
    } else {
        CmLogNote(nhi, "cm.osc.refused", reason);
    }
    if (mb.Va != NULL) {
        cmOsnEcamUnmap(nhi, mb.Va);
        mb.Va = NULL;
    }
    if (usp != NULL) {
        cmOsnUnuse(usp);
    }
    cmOsnRelease();
    if (reason == 0) {
        settle.QuadPart = -(LONGLONG)(CM_OSN_SETTLE_MS * 10000UL);
        (VOID)KeDelayExecutionThread(KernelMode, FALSE, &settle);
    }
    if (reason == CM_OSN_R_COVERAGE && !resume) {
        /* 12.8.15.16: a member not there yet defers once the marker armed
         * for this try is taken back and flushed, outside the hold; else
         * the refusal stands, the guard conservative (12.8.15.13). */
        CmStatusMarkerDelete(nhi, &written, &flushed);
        CmLogNote(nhi, "cm.osc.guard.deleted",
                  (written ? 1UL : 0UL) | (flushed ? 2UL : 0UL));
        if (written && flushed) {
            nhi->OscArmed = 0;
            CmLogNote(nhi, "cm.osc.defer", reason);
            return CM_OSCN_DEFER;
        }
        if (written) {
            cmOsnLatch = 1;
            CmStatusMarkerWrite(nhi, nhi->OscArmed, &written, &flushed);
            CmLogNote(nhi, "cm.osc.guard.rewrite",
                      (written ? 1UL : 0UL) | (flushed ? 2UL : 0UL));
        }
    }
    return CM_OSCN_DONE;
}

/* The common gates (12.8.15.11, E's sequence): FALSE when nothing may be
 * sent by any route (logged). */
static BOOLEAN cmOsnGates(PCM_NHI nhi)
{
    CM_OSN_VERSION ver;
    ULONG rtWhy;
    ULONG rtStatus;

    if (nhi->Values.PlatformHandover == 0) {
        CmLogNote(nhi, "cm.osc.off", 0);
        return FALSE;
    }
    if (!cmOsnRoutines(&rtWhy, &rtStatus)) {
        /* 12.8.15.15: the x86 image's routines come through the helper,
         * which only the controller's xhci98.inf installs. */
        nhi->OscFlags |= CM_OSCN_F_NOROUTE;
        CmLogNote(nhi, "cm.osc.nt.route", rtWhy);
        CmLogNote(nhi, "cm.osc.nt.route.status", rtStatus);
        CmLogNote(nhi, "cm.osc.refused", CM_OSN_R_SYSTEM);
        return FALSE;
    }
    CmZero(&ver, sizeof(ver));
    ver.Size = sizeof(ver);
    /* Windows 11: the firmware's own _OSC sends it. No RtlGetVersion:
     * Windows 2000 alone goes on (12.8.15.17). */
    if ((cmOsnGetVersion != NULL && !NT_SUCCESS(cmOsnGetVersion(&ver))) ||
        !CmOscpSystem((BOOLEAN)(cmOsnGetVersion != NULL), ver.Build,
                      IoIsWdmVersionAvailable(1, 0x20))) {
        CmLogNote(nhi, "cm.osc.nt.build", ver.Build);
        CmLogNote(nhi, "cm.osc.refused", CM_OSN_R_SYSTEM);
        return FALSE;
    }
    CmLogNote(nhi, "cm.osc.nt.build", ver.Build);
#if defined(_AMD64_)
    cmOsnEcamSys = CmOscpEcamSystem((BOOLEAN)(cmOsnGetVersion != NULL),
                                    ver.Major, ver.Minor, TRUE) ? 1UL : 0UL;
#else
    cmOsnEcamSys = CmOscpEcamSystem((BOOLEAN)(cmOsnGetVersion != NULL),
                                    ver.Major, ver.Minor, FALSE) ? 1UL : 0UL;
#endif
    cmOsnNoExt = (CmOscpExtAllowed((BOOLEAN)(cmOsnGetVersion != NULL)) &&
                  !cmOsnEcamSys) ? 0UL : 1UL;
    if (cmOsnEcamSys) {
        CmLogNote(nhi, "cm.osc.ecam.system",
                  (ver.Major << 8) | (ver.Minor & 0xFFUL));
    }
    return TRUE;
}

/* ---- the read path, B, C and D (12.8.15.17) ----------------------------- */

/* PHYSICAL_MEMORY_RANGE as the range routines give it. */
typedef struct _CM_OSN_PMR {
    ULONG BaseLo;
    ULONG BaseHi;
    ULONG BytesLo;
    ULONG BytesHi;
} CM_OSN_PMR;

#define CM_OSN_RD_ROUTINE   0x21UL  /* a routine of the route absent      */
#define CM_OSN_RD_SNAPSHOT  0x22UL  /* the snapshot not had               */
#define CM_OSN_RD_BOOT      0x23UL  /* the boot type not read             */
#define CM_OSN_RD_TABLE     0x24UL  /* class 76 refused                   */
#define CM_OSN_RD_SCAN      0x25UL  /* the scan refused (a page)          */
#define CM_OSN_RD_NONE      0x26UL  /* no route for this system           */
#define CM_OSN_RD_PAGE      0x27UL  /* a read refused by the page check   */
#define CM_OSN_RD_ECAM      0x28UL  /* MCFG unlike the ECAM base found
                                       (12.8.15.18)                      */

#define CM_OSN_FW_ACPI      0x41435049UL    /* 'ACPI', the provider       */
#define CM_OSN_FW_CLASS     76UL    /* SystemFirmwareTableInformation     */
#define CM_OSN_FW_HEAD      16UL    /* the information's bytes before the
                                       table                              */
#define CM_OSN_ROOT_CAP     0x224UL /* 24h + 8 x 64                        */
#define CM_OSN_MCFG_CAP     0x1000UL

/* {4A1D3C77-0E9B-4F25-8B6E-5C2D7A19F3E0}: this project's own vendor GUID
 * for the boot test's variable, which no firmware holds. */
static GUID cmOsnProbeGuid = {
    0x4A1D3C77UL, 0x0E9B, 0x4F25,
    { 0x8B, 0x6E, 0x5C, 0x2D, 0x7A, 0x19, 0xF3, 0xE0 }
};

/* The system's snapshot of the memory it manages as the pure check takes
 * it, into a copy of the driver's own (*out, freed by the caller with
 * CmFree): CM_OSC_OK, or CM_OSN_RD_SNAPSHOT. The system's buffer is freed
 * here (CmFree, ExFreePool). */
static ULONG cmOsnMemSnap(PCM_NHI nhi, CM_OSCP_RANGE **out, PULONG count)
{
    const CM_OSN_PMR *p;
    CM_OSCP_RANGE *r;
    PVOID snap;
    ULONG n;
    ULONG i;

    *out = NULL;
    *count = 0;
    snap = (cmOsnEx2 != NULL) ? cmOsnEx2(NULL, 0) : cmOsnRanges();
    if (snap == NULL) {
        CmLogNote(nhi, "cm.osc.read.snapshot", 0);
        return CM_OSN_RD_SNAPSHOT;
    }
    p = (const CM_OSN_PMR *)snap;
    for (n = 0; n <= CM_OSCP_RANGES_MAX; n++) {
        if (p[n].BaseLo == 0 && p[n].BaseHi == 0 && p[n].BytesLo == 0 &&
            p[n].BytesHi == 0) {
            break;
        }
    }
    if (n > CM_OSCP_RANGES_MAX) {
        CmFree(snap);
        CmLogNote(nhi, "cm.osc.read.snapshot", n);
        return CM_OSN_RD_SNAPSHOT;
    }
    r = (CM_OSCP_RANGE *)CmAlloc((n + 1UL) * sizeof(CM_OSCP_RANGE));
    if (r == NULL) {
        CmFree(snap);
        return CM_OSN_RD_SNAPSHOT;
    }
    for (i = 0; i < n; i++) {
        r[i].BaseLo = p[i].BaseLo;
        r[i].BaseHi = p[i].BaseHi;
        r[i].BytesLo = p[i].BytesLo;
        r[i].BytesHi = p[i].BytesHi;
    }
    CmFree(snap);
    CmLogNote(nhi, "cm.osc.read.ranges", n);
    *out = r;
    *count = n;
    return CM_OSC_OK;
}

/* B's boot test: CM_OSCP_S_OBTAINED a legacy BIOS boot (the variable
 * service not implemented), CM_OSCP_S_ABSENT a UEFI boot (any other
 * status). */
static ULONG cmOsnBoot(PCM_NHI nhi)
{
    static const WCHAR probe[] = L"Xhci98BootProbe";
    UNICODE_STRING name;
    ULONG value;
    ULONG length;
    ULONG attributes;
    NTSTATUS status;

    name.Buffer = (PWSTR)probe;
    name.Length = (USHORT)(sizeof(probe) - sizeof(WCHAR));
    name.MaximumLength = (USHORT)sizeof(probe);
    value = 0;
    length = sizeof(value);
    attributes = 0;
    status = cmOsnFwEnv(&name, &cmOsnProbeGuid, &value, &length,
                        &attributes);
    CmLogNote(nhi, "cm.osc.read.uefi", (ULONG)status);
    return (status == STATUS_NOT_IMPLEMENTED) ? CM_OSCP_S_OBTAINED
                                              : CM_OSCP_S_ABSENT;
}

/* One table by class 76 (C): the information block of CM_OSN_FW_HEAD +
 * `cap` bytes in *block (freed by the caller with CmFree when not NULL),
 * the table at *block + CM_OSN_FW_HEAD; a CM_OSCP_S_*. */
static ULONG cmOsnFwTable(PCM_NHI nhi, ULONG sig, ULONG cap, UCHAR **block)
{
    UCHAR *b;
    ULONG returned;
    ULONG got;
    ULONG r;
    NTSTATUS status;

    *block = NULL;
    b = (UCHAR *)CmAlloc(CM_OSN_FW_HEAD + cap);
    if (b == NULL) {
        return CM_OSCP_S_REFUSED;
    }
    CmZero(b, CM_OSN_FW_HEAD + cap);
    ((PULONG)b)[0] = CM_OSN_FW_ACPI;
    ((PULONG)b)[1] = 1UL;               /* SystemFirmwareTable_Get */
    ((PULONG)b)[2] = sig;
    ((PULONG)b)[3] = cap;
    returned = 0;
    status = cmOsnQuery(CM_OSN_FW_CLASS, b, CM_OSN_FW_HEAD + cap, &returned);
    got = ((PULONG)b)[3];
    r = CmOscpFwTable((BOOLEAN)NT_SUCCESS(status),
                      (BOOLEAN)(status == STATUS_NOT_FOUND ||
                                status == STATUS_OBJECT_NAME_NOT_FOUND),
                      got, cap, b + CM_OSN_FW_HEAD);
    CmLogNote(nhi, "cm.osc.read.table", sig);
    CmLogNote(nhi, "cm.osc.read.table.status", (ULONG)status);
    CmLogNote(nhi, "cm.osc.read.table.length", got);
    if (r != CM_OSCP_S_OBTAINED) {
        CmFree(b);
        return r;
    }
    *block = b;
    return r;
}

/* The read path in the match's place (12.8.15.17), under the
 * once-per-boot mutex: CM_OSC_OK with the command's data byte in *data
 * and the tier in nhi->OscTier, or a reason (CM_OSC_* or CM_OSN_RD_*).
 * On the ECAM systems (12.8.15.18) the discovery's MCFG must give
 * `ecamBase` and `ecamEnd`, the base the page is mapped from. */
static ULONG cmOsnRead(PCM_NHI nhi, const CM_OSN_FOUND *f, PULONG data,
                       ULONG ecamBase, ULONG ecamEnd)
{
    ULONG eb;
    ULONG ee;
    CM_OSC_NTSRC s;
    CM_OSCP_RANGE *ranges;
    UCHAR *root;
    UCHAR *mcfg;
    ULONG have;
    ULONG need;
    ULONG route;
    ULONG boot;
    ULONG scan;
    ULONG refused;
    ULONG r;

    *data = 0;
    have = (cmOsnEx2 != NULL ? CM_OSCP_HAVE_EX2 : 0UL) |
           (cmOsnRanges != NULL ? CM_OSCP_HAVE_RANGES : 0UL) |
           (cmOsnFwEnv != NULL ? CM_OSCP_HAVE_FWENV : 0UL) |
           (cmOsnQuery != NULL ? CM_OSCP_HAVE_QUERY : 0UL) |
           (IoIsWdmVersionAvailable(1, 0x30) ? CM_OSCP_SYS_130 : 0UL);
#if !defined(_AMD64_)
    have |= CM_OSCP_SYS_X86;
#endif
    route = CmOscpReadRoute(have, &need);
    CmLogNote(nhi, "cm.osc.read.route", route);
    if (route == CM_OSCP_RT_NONE) {
        return CM_OSN_RD_NONE;
    }
    if ((have & need) != need) {
        CmLogNote(nhi, "cm.osc.read.have", have);
        return CM_OSN_RD_ROUTINE;
    }
    CmZero(&s, sizeof(s));
    s.RootBdf = f->RootBdf;
    s.UspBdf = f->UspBdf;
    s.DspBdf = f->DspBdf;
    ranges = NULL;
    root = NULL;
    mcfg = NULL;
    refused = 0;
    r = cmOsnMemSnap(nhi, &ranges, &s.Count);
    if (r != CM_OSC_OK) {
        return r;
    }
    s.Ranges = ranges;
    if (route == CM_OSCP_RT_B) {
        boot = cmOsnBoot(nhi);
        scan = CM_OSCP_S_REFUSED;
        if (boot == CM_OSCP_S_OBTAINED) {
            scan = CmOscNtScan(&s, s.Rsdp, &refused);
            CmLogNote(nhi, "cm.osc.read.scan", scan);
        }
        route = CmOscpAfterB(boot, scan);
        CmLogNote(nhi, "cm.osc.read.route", route);
        if (route == CM_OSCP_RT_NONE) {
            r = CM_OSN_RD_SCAN;
            goto done;
        }
        if (route == CM_OSCP_RT_C && cmOsnQuery == NULL) {
            r = CM_OSN_RD_ROUTINE;
            goto done;
        }
    } else if (route == CM_OSCP_RT_D) {
        scan = CmOscNtScan(&s, s.Rsdp, &refused);
        CmLogNote(nhi, "cm.osc.read.scan", scan);
        if (scan != CM_OSCP_S_OBTAINED) {
            r = (scan == CM_OSCP_S_ABSENT) ? CM_OSC_NO_RSDP : CM_OSN_RD_SCAN;
            goto done;
        }
    }
    if (route == CM_OSCP_RT_C) {
        r = cmOsnFwTable(nhi, CM_OSC_SIG('X', 'S', 'D', 'T'), CM_OSN_ROOT_CAP,
                         &root);
        if (r == CM_OSCP_S_ABSENT) {
            r = cmOsnFwTable(nhi, CM_OSC_SIG('R', 'S', 'D', 'T'),
                             CM_OSN_ROOT_CAP, &root);
        }
        if (r == CM_OSCP_S_ABSENT && cmOsnEcamSys) {
            /* 12.8.15.18: XP x64's provider gives MCFG by name and
             * neither root table (read on the emulator): the root pointer
             * by the scan of E0000h to FFFFFh, as D finds it, MCFG still
             * by name. Only there; every other system refuses as before. */
            scan = CmOscNtScan(&s, s.Rsdp, &refused);
            CmLogNote(nhi, "cm.osc.read.rootscan", scan);
            if (scan != CM_OSCP_S_OBTAINED) {
                r = (scan == CM_OSCP_S_ABSENT) ? CM_OSC_NO_RSDP
                                               : CM_OSN_RD_SCAN;
                goto done;
            }
            r = CM_OSCP_S_OBTAINED;
        }
        if (r == CM_OSCP_S_OBTAINED) {
            r = cmOsnFwTable(nhi, CM_OSC_SIG('M', 'C', 'F', 'G'),
                             CM_OSN_MCFG_CAP, &mcfg);
        }
        if (r != CM_OSCP_S_OBTAINED) {
            r = CM_OSN_RD_TABLE;
            goto done;
        }
        s.Root = (root != NULL) ? root + CM_OSN_FW_HEAD : NULL;
        s.Mcfg = mcfg + CM_OSN_FW_HEAD;
    }
    r = CmOscNtDiscover(nhi, &s, data, &eb, &ee, &refused);
    if (r == CM_OSC_OK && cmOsnEcamSys && (eb != ecamBase || ee != ecamEnd)) {
        CmLogNote(nhi, "cm.osc.read.ecam", eb);
        *data = 0;
        r = CM_OSN_RD_ECAM;
    }

done:
    CmLogNote(nhi, "cm.osc.read.refused.pages", refused);
    if (root != NULL) {
        CmFree(root);
    }
    if (mcfg != NULL) {
        CmFree(mcfg);
    }
    CmFree(ranges);
    return r;
}

/* The board known, or not: FALSE when nothing is sent (logged). The S4
 * resend's (12.8.15.17); `ecamBase` and `ecamEnd` as cmOsnRead's. */
static BOOLEAN cmOsnBoard(PCM_NHI nhi, const CM_OSN_FOUND *f, PULONG data,
                          ULONG ecamBase, ULONG ecamEnd)
{
    ULONG r;

    nhi->OscTier = CM_OSC_TIER_UNKNOWN;
    if (cmOsnMatch(nhi, f, data) != 0) {
        nhi->OscTier = CM_OSN_TIER_PROFILE;
        return TRUE;
    }
    if (nhi->Values.HandoverReads == 0) {
        /* 12.8.15.12: the report's line. */
        nhi->OscFlags |= CM_OSCN_F_UNKNOWN;
        CmLogNote(nhi, "cm.osc.unknown", 0);
        return FALSE;
    }
    r = cmOsnRead(nhi, f, data, ecamBase, ecamEnd);
    if (r == CM_OSC_OK) {
        return TRUE;
    }
    CmLogNote(nhi, "cm.osc.read", r);
    CmLogNote(nhi, "cm.osc.refused", CM_OSN_R_READS);
    return FALSE;
}

/* ---- ECAM on 2000, 32-bit XP and XP x64 (12.8.15.18) ------------------- */

#define CM_OSN_EC_ROUTINE   1UL     /* a routine of the source absent     */
#define CM_OSN_EC_SNAPSHOT  2UL     /* the snapshot not had               */
#define CM_OSN_EC_RSDP      3UL     /* no root pointer                    */
#define CM_OSN_EC_ROOT      4UL     /* the root table                     */
#define CM_OSN_EC_MCFG      5UL     /* no MCFG, or one refused            */
#define CM_OSN_EC_BUSES     6UL     /* the buses outside its allocation   */
#define CM_OSN_EC_PAGE      7UL     /* the page in the snapshot           */
#define CM_OSN_EC_MAP       8UL     /* the mapping failed                 */
#define CM_OSN_EC_TABLE     9UL     /* class 76 refused                   */
#define CM_OSN_EC_REFUSED   0x0AUL  /* a read refused by the page check   */

/* The ECAM base for the upstream port (once a start, and at an S4
 * resend), under the once-per-boot mutex, PASSIVE_LEVEL: 0 with *base
 * and *busEnd, else CM_OSN_R_ECAM (cm.osc.ecam the reason). XP x64: MCFG
 * through class 76; 2000 and 32-bit XP: the scan, the root table and the
 * MCFG pass (CmOscNtMcfg). The page is refused where the system's
 * snapshot holds it. No SSDT, no firmware data and no ECAM is read. */
static ULONG cmOsnEcamFind(PCM_NHI nhi, const CM_OSN_FOUND *f, PULONG base,
                           PULONG busEnd)
{
    CM_OSC_NTSRC s;
    CM_OSCP_RANGE *ranges;
    UCHAR *mcfg;
    ULONG why;
    ULONG phys;
    ULONG refused;
    ULONG r;

    *base = 0;
    *busEnd = 0;
    ranges = NULL;
    mcfg = NULL;
    refused = 0;
    why = 0;
    CmZero(&s, sizeof(s));
    s.RootBdf = f->RootBdf;
    s.UspBdf = f->UspBdf;
    s.DspBdf = f->DspBdf;
    if (cmOsnEx2 == NULL && cmOsnRanges == NULL) {
        why = CM_OSN_EC_ROUTINE;
        goto done;
    }
#if defined(_AMD64_)
    if (cmOsnQuery == NULL) {
        why = CM_OSN_EC_ROUTINE;
        goto done;
    }
#endif
    if (cmOsnMemSnap(nhi, &ranges, &s.Count) != CM_OSC_OK) {
        why = CM_OSN_EC_SNAPSHOT;
        goto done;
    }
    s.Ranges = ranges;
#if defined(_AMD64_)
    r = cmOsnFwTable(nhi, CM_OSC_SIG('M', 'C', 'F', 'G'), CM_OSN_MCFG_CAP,
                     &mcfg);
    if (r != CM_OSCP_S_OBTAINED) {
        why = (r == CM_OSCP_S_ABSENT) ? CM_OSN_EC_MCFG : CM_OSN_EC_TABLE;
        goto done;
    }
    {
        CM_OSC_HDR th;

        CmOscHeader(mcfg + CM_OSN_FW_HEAD, &th);
        if (!CmOscMcfg(mcfg + CM_OSN_FW_HEAD, th.Length,
                       XHCI_TBT_BDF_BUS(f->RootBdf),
                       XHCI_TBT_BDF_BUS(f->DspBdf), base, busEnd)) {
            why = CM_OSN_EC_BUSES;
            goto done;
        }
    }
#else
    r = CmOscNtScan(&s, s.Rsdp, &refused);
    if (r != CM_OSCP_S_OBTAINED) {
        why = (r == CM_OSCP_S_ABSENT) ? CM_OSN_EC_RSDP : CM_OSN_EC_REFUSED;
        goto done;
    }
    r = CmOscNtMcfg(nhi, &s, base, busEnd, &refused);
    if (r != CM_OSC_OK) {
        CmLogNote(nhi, "cm.osc.ecam.mcfg", r);
        why = (r == 0x27UL) ? CM_OSN_EC_REFUSED :
              (r == CM_OSC_NO_MCFG) ? CM_OSN_EC_MCFG : CM_OSN_EC_ROOT;
        goto done;
    }
#endif
#if defined(XHCI_FLAVOUR_QEMU)
    if (nhi->QemuEcamFault == 1) {
        CmLogNote(nhi, "cm.osc.qemu.ecam", 1);
        *base += 0x100000UL;
    } else if (nhi->QemuEcamFault == 2) {
        CmLogNote(nhi, "cm.osc.qemu.ecam", 2);
        why = CM_OSN_EC_MCFG;
        goto done;
    }
#endif
    if (!CmOscEcam(*base, *busEnd, f->UspBdf, 0, &phys)) {
        why = CM_OSN_EC_BUSES;
        goto done;
    }
    if (CmOscpRangeHit(ranges, s.Count, phys, 0x1000UL)) {
        why = CM_OSN_EC_PAGE;
        goto done;
    }
    CmLogNote(nhi, "cm.osc.ecam.base", *base);
    CmLogNote(nhi, "cm.osc.ecam.end", *busEnd);

done:
    if (refused != 0) {
        CmLogNote(nhi, "cm.osc.read.refused.pages", refused);
    }
    if (mcfg != NULL) {
        CmFree(mcfg);
    }
    if (ranges != NULL) {
        CmFree(ranges);
    }
    if (why != 0) {
        *base = 0;
        *busEnd = 0;
        CmLogNote(nhi, "cm.osc.ecam", why);
        return CM_OSN_R_ECAM;
    }
    return 0;
}

static LONG cmOsnEcamMaps;              /* mappings outstanding         */

/* The upstream port's ECAM page, one 4 KB page non-cached, or NULL
 * (logged). Only inside a hold of the members; PASSIVE_LEVEL. */
static PUCHAR cmOsnEcamMap(PCM_NHI nhi, ULONG base, ULONG busEnd, ULONG bdf)
{
    PHYSICAL_ADDRESS pa;
    PUCHAR va;
    ULONG phys;

    if (base == 0 || !CmOscEcam(base, busEnd, bdf, 0, &phys)) {
        CmLogNote(nhi, "cm.osc.ecam", CM_OSN_EC_BUSES);
        return NULL;
    }
    pa.QuadPart = (LONGLONG)phys;
    va = (PUCHAR)MmMapIoSpace(pa, 0x1000, MmNonCached);
    if (va == NULL) {
        CmLogNote(nhi, "cm.osc.ecam", CM_OSN_EC_MAP);
    } else {
        (VOID)InterlockedIncrement(&cmOsnEcamMaps);
    }
    return va;
}

static VOID cmOsnEcamUnmap(PCM_NHI nhi, PUCHAR va)
{
    MmUnmapIoSpace(va, 0x1000);
    CmLogNote(nhi, "cm.osc.ecam.maps",
              (ULONG)InterlockedDecrement(&cmOsnEcamMaps));
}

/* The ECAM page is the port the route reads (12.8.15.18): dwords 0,
 * 08h, 18h and 20h through both, then the alias test on the page;
 * reads only. 0, or CM_OSN_R_ECAMPORT (cm.osc.ecam.check which test). */
static ULONG cmOsnEcamCheck(PCM_NHI nhi, PCM_OSC_MEMBER m, PUCHAR va,
                            const CM_OSN_FOUND *f)
{
    static const ULONG at[4] = { 0x00UL, 0x08UL, 0x18UL, 0x20UL };
    ULONG route[4];
    ULONG ecam[4];
    ULONG d100;
    ULONG d420;
    ULONG i;
    ULONG r;

    for (i = 0; i < 4UL; i++) {
        if (!cmOsnRd(nhi, m, at[i], &route[i]) ||
            !cmOsnEcRd(nhi, va, at[i], &ecam[i])) {
            CmLogNote(nhi, "cm.osc.ecam.check", 0x80UL | i);
            return CM_OSN_R_ECAMPORT;
        }
    }
    if (!cmOsnEcRd(nhi, va, 0x100UL, &d100) ||
        !cmOsnEcRd(nhi, va, CM_OSC_MB_CMD, &d420)) {
        CmLogNote(nhi, "cm.osc.ecam.check", 0x84UL);
        return CM_OSN_R_ECAMPORT;
    }
    CmLogNote(nhi, "cm.osc.ecam.id", ecam[0]);
    CmLogNote(nhi, "cm.osc.alias.100", d100);
    r = CmOscpEcamCheck(route, ecam, f->UspSec, d100, d420);
    if (r != CM_OSCP_E_OK) {
        CmLogNote(nhi, "cm.osc.ecam.check", r);
        return CM_OSN_R_ECAMPORT;
    }
    return 0;
}

/* The alias test (12.8.15.16): dwords 0, 100h, 20h and 420h through the
 * route, reads only. FALSE when one could not be read; else *aliased. */
static BOOLEAN cmOsnAlias(PCM_NHI nhi, PCM_OSC_MEMBER m, BOOLEAN *aliased)
{
    ULONG d000;
    ULONG d100;
    ULONG d020;
    ULONG d420;

    *aliased = TRUE;
    if (!cmOsnRd(nhi, m, 0, &d000) || !cmOsnRd(nhi, m, 0x100, &d100) ||
        !cmOsnRd(nhi, m, 0x20, &d020) ||
        !cmOsnRd(nhi, m, CM_OSC_MB_CMD, &d420)) {
        return FALSE;
    }
    CmLogNote(nhi, "cm.osc.alias.100", d100);
    CmLogNote(nhi, "cm.osc.alias.20", d020);
    *aliased = CmOscpAliased(d000, d100, d020, d420);
    return TRUE;
}

/* The mailbox probe (12.8.15.16): under a hold of its own, through the
 * upstream port's own interface, before any firmware memory is read and
 * before the marker is armed; reads only. 0 passed, CM_OSCP_P_DEFER (*why
 * the reason), else the refusal. */
static ULONG cmOsnProbe(PCM_NHI nhi, const CM_OSN_FOUND *f,
                        const CM_OSCP_KEPT *k, PULONG why)
{
    PCM_OSC_MEMBER usp;
    PUCHAR va;
    ULONG ecamWhy;
    BOOLEAN held;
    BOOLEAN aborted;
    BOOLEAN route;
    BOOLEAN d0;
    BOOLEAN aliased;
    BOOLEAN mbRead;
    ULONG mb;
    ULONG x;
    ULONG r;

    if (cmOsnNoExt && !cmOsnEcamSys) {
        /* No access to the extended space at all, and no ECAM. */
        *why = CM_OSN_R_ALIAS;
        CmLogNote(nhi, "cm.osc.probe.system", 0);
        CmLogNote(nhi, "cm.osc.probe", *why);
        return *why;
    }
    usp = NULL;
    aborted = FALSE;
    route = FALSE;
    d0 = FALSE;
    aliased = FALSE;
    mbRead = FALSE;
    mb = 0xFFFFFFFFUL;
    ecamWhy = 0;
    va = NULL;
    cmOsnRetakeBus(nhi, f->UspBdf);
    held = cmOsnHold(nhi);
    if (held) {
        usp = cmOsnUse(f->UspBdf);
    }
    if (usp != NULL) {
        route = (BOOLEAN)(cmOsnRd(nhi, usp, 0, &x) && x == f->Pci.UspId &&
                          cmOsnRd(nhi, usp, 0x18, &x) &&
                          ((x >> 8) & 0xFFUL) == f->UspSec);
        d0 = (BOOLEAN)(route && cmOsnD0(nhi, usp));
        if (d0 && cmOsnEcamSys) {
            /* 12.8.15.18: the mailbox through the page alone, mapped in
             * this hold and checked against the route first. */
            va = cmOsnEcamMap(nhi, k->EcamBase, k->EcamEnd, f->UspBdf);
            ecamWhy = (va == NULL) ? CM_OSN_R_ECAM
                                   : cmOsnEcamCheck(nhi, usp, va, f);
            if (ecamWhy == 0) {
                mbRead = cmOsnEcRd(nhi, va, CM_OSC_MB_CMD, &mb);
                CmLogNote(nhi, "cm.osc.probe.mb", mb);
            }
            if (va != NULL) {
                cmOsnEcamUnmap(nhi, va);
            }
        } else if (d0) {
            /* A route that truncates the offset is refused by the alias
             * test before any write. */
            mbRead = (BOOLEAN)(cmOsnRd(nhi, usp, CM_OSC_MB_CMD, &mb) &&
                               cmOsnAlias(nhi, usp, &aliased));
            CmLogNote(nhi, "cm.osc.probe.mb", mb);
        }
        aborted = cmOsnAborted(nhi);
        cmOsnUnuse(usp);
    }
    cmOsnRelease();
    r = CmOscpProbeEx(held, (BOOLEAN)(usp != NULL), aborted, route, d0,
                      ecamWhy, aliased, mbRead, mb, why);
    CmLogNote(nhi, "cm.osc.probe",
              *why | (r == CM_OSCP_P_DEFER ? 0x10000UL : 0UL));
    return r;
}

/* E's sequence at a start or a deferred start's retry: CM_OSCN_DEFER
 * when a member is not there yet or the key cannot be flushed yet
 * (12.8.15.16), else CM_OSCN_DONE. The board is decided once a start
 * (12.8.15.16): what is decided is kept in nhi->OscKept for the start's
 * later tries, with the host it was made for. */
static ULONG cmOsnRun(PCM_NHI nhi)
{
    CM_OSN_FOUND f;
    CM_OSCP_KEPT now;
    CM_OSCP_KEPT *k;
    ULONG reason;
    ULONG flush;
    ULONG step;
    ULONG data;
    ULONG r;

    k = &nhi->OscKept;
    nhi->OscFlags &= CM_OSCN_F_EXPIRED;
    nhi->OscArmed = 0;
    nhi->OscTier = CM_OSC_TIER_NONE;
    if (!cmOsnGates(nhi)) {
        return CM_OSCN_DONE;
    }
    reason = cmOsnHostByBus(nhi, &f);
    if (reason == 0x101UL || reason == 0x102UL) {
        CmLogNote(nhi, "cm.osc.defer", reason);
        return CM_OSCN_DEFER;
    }
    if (reason != 0) {
        CmLogNote(nhi, "cm.osc.refused", reason);
        return CM_OSCN_DONE;
    }
    CmLogNote(nhi, "cm.osc.usp", f.UspBdf);
    nhi->OscUspBdf = f.UspBdf;
    cmOsnLock0();
    if (cmOsnServed(f.UspBdf)) {
        cmOsnUnlock0();
        CmLogNote(nhi, "cm.osc.already", f.UspBdf);
        return CM_OSCN_DONE;
    }
    if (!cmOsnGuard(nhi, f.UspBdf)) {
        cmOsnUnlock0();
        return CM_OSCN_DONE;
    }
    CmZero(&now, sizeof(now));
    now.NhiBdf = f.NhiBdf;
    now.DspBdf = f.DspBdf;
    now.UspBdf = f.UspBdf;
    now.RootBdf = f.RootBdf;
    now.Pci = f.Pci;
    if (!CmOscpKept(k, &now)) {
        cmOsnUnlock0();
        CmLogNote(nhi, "cm.osc.refused", CM_OSN_R_CHANGED);
        return CM_OSCN_DONE;
    }
    if (k->State == CM_OSCP_K_SEND) {
        nhi->OscTier = k->Tier;
        CmLogNote(nhi, "cm.osc.kept", k->Tier);
    } else if (k->State == CM_OSCP_K_UNKNOWN) {
        nhi->OscTier = CM_OSC_TIER_UNKNOWN;
    }
    flush = CM_OSCP_F_UNTESTED;
    r = CM_OSCN_DONE;
    for (;;) {
        step = CmOscpBoardNextEx(k->State,
                                 (BOOLEAN)(nhi->Values.HandoverReads != 0),
                                 (BOOLEAN)(cmOsnLatch != 0),
                                 (BOOLEAN)(cmOsnEcamSys != 0),
                                 (BOOLEAN)(k->Ecam != 0),
                                 (BOOLEAN)(k->Probed != 0), flush);
        if (step == CM_OSCP_B_MATCH) {
            *k = now;
            k->State = CM_OSCP_K_UNKNOWN;
            nhi->OscTier = CM_OSC_TIER_UNKNOWN;
            data = 0;
            if (cmOsnMatch(nhi, &f, &data) != 0) {
                k->State = CM_OSCP_K_SEND;
                k->Tier = CM_OSN_TIER_PROFILE;
                k->Data = data;
                nhi->OscTier = CM_OSN_TIER_PROFILE;
            }
        } else if (step == CM_OSCP_B_UNKNOWN) {
            /* 12.8.15.12: the report's line. */
            nhi->OscFlags |= CM_OSCN_F_UNKNOWN;
            CmLogNote(nhi, "cm.osc.unknown", 0);
            break;
        } else if (step == CM_OSCP_B_LATCHED) {
            CmLogNote(nhi, "cm.osc.refused", CM_OSN_R_GUARD);
            break;
        } else if (step == CM_OSCP_B_NOECAM) {
            /* 12.8.15.18 (Q25 (b)): status bit 25, XHCITR's line. */
            nhi->OscFlags |= CM_OSCN_F_ECAMOFF;
            CmLogNote(nhi, "cm.osc.refused", CM_OSN_R_ECAMOFF);
            break;
        } else if (step == CM_OSCP_B_ECAM) {
            reason = cmOsnEcamFind(nhi, &f, &k->EcamBase, &k->EcamEnd);
            if (reason != 0) {
                CmLogNote(nhi, "cm.osc.refused", reason);
                break;
            }
            k->Ecam = 1;
        } else if (step == CM_OSCP_B_PROBE) {
            reason = cmOsnProbe(nhi, &f, k, &data);
            if (reason == CM_OSCP_P_DEFER) {
                CmLogNote(nhi, "cm.osc.defer", data);
                r = CM_OSCN_DEFER;
                break;
            }
            if (reason != 0) {
                CmLogNote(nhi, "cm.osc.refused", reason);
                break;
            }
            k->Probed = 1;
        } else if (step == CM_OSCP_B_FLUSH) {
            /* A key that cannot be flushed yet (the system hive not backed
             * by its file in the boot's PnP phase): nothing read, nothing
             * written. */
            flush = CmStatusMarkerFlushable(nhi) ? CM_OSCP_F_OK
                                                 : CM_OSCP_F_FAILED;
        } else if (step == CM_OSCP_B_DEFER) {
            CmLogNote(nhi, "cm.osc.defer", CM_OSN_R_GUARD);
            r = CM_OSCN_DEFER;
            break;
        } else if (step == CM_OSCP_B_READ) {
            data = 0;
            reason = cmOsnRead(nhi, &f, &data, k->EcamBase, k->EcamEnd);
            if (reason != CM_OSC_OK) {
                CmLogNote(nhi, "cm.osc.read", reason);
                CmLogNote(nhi, "cm.osc.refused", CM_OSN_R_READS);
                break;
            }
            k->State = CM_OSCP_K_SEND;
            k->Tier = nhi->OscTier;
            k->Data = data;
        } else {
            r = cmOsnSend(nhi, &f, k->Data, FALSE, NULL, k->EcamBase,
                          k->EcamEnd);
            break;
        }
    }
    cmOsnUnlock0();
    return r;
}

ULONG CmOscntHandover(PCM_NHI nhi)
{
    nhi->OscFlags = 0;
    nhi->OscArmed = 0;
    /* A new start decides afresh (12.8.15.16). */
    CmZero(&nhi->OscKept, sizeof(nhi->OscKept));
    if (!cmOsnNt() || ((nhi->Vendor & 0xFFFFUL) | (nhi->Device << 16)) !=
                          CM_OSCP_ID_NHI) {
        return CM_OSCN_DONE;
    }
    return cmOsnRun(nhi);
}

ULONG CmOscntRetry(PCM_NHI nhi)
{
    CmLogNote(nhi, "cm.osc.defer.try", 0);
    return cmOsnRun(nhi);
}

VOID CmOscntSystem(PCM_NHI nhi, SYSTEM_POWER_STATE state)
{
    CM_OSN_HOST *h;
    KIRQL irql;

    if (!cmOsnReady || nhi->OscUspBdf == 0 ||
        nhi->OscUspBdf == XHCI_TBT_NO_BDF) {
        return;
    }
    KeAcquireSpinLock(&cmOsnLock, &irql);
    h = cmOsnHostOf(nhi->OscUspBdf);
    if (h != NULL) {
        h->Owed = CmOscpOwed(h->Owed, (ULONG)state);
    }
    KeReleaseSpinLock(&cmOsnLock, irql);
}

VOID CmOscntResume(PCM_NHI nhi)
{
    CM_OSN_FOUND f;
    CM_OSN_HOST *h;
    KIRQL irql;
    ULONG kind;
    ULONG data;
    ULONG reason;
    ULONG eb;
    ULONG ee;
    BOOLEAN owed;
    BOOLEAN ok;

    if (!cmOsnNt() || !cmOsnReady || nhi->OscUspBdf == 0 ||
        nhi->OscUspBdf == XHCI_TBT_NO_BDF) {
        return;
    }
    owed = FALSE;
    ok = FALSE;
    KeAcquireSpinLock(&cmOsnLock, &irql);
    h = cmOsnHostOf(nhi->OscUspBdf);
    if (h != NULL && h->Owed) {
        /* Consumed at the start of the attempt (12.8.13.6). */
        h->Owed = 0;
        owed = TRUE;
        ok = (BOOLEAN)(h->Ok != 0);
    }
    KeReleaseSpinLock(&cmOsnLock, irql);
    if (!owed) {
        return;
    }
    if (!ok) {
        CmLogNote(nhi, "cm.osc.resume.unserved", nhi->OscUspBdf);
        return;
    }
    if (!cmOsnGates(nhi)) {
        return;
    }
    kind = 0;
    if (CmStatusMarkerRead(nhi, &kind) != CM_OSCG_M_ABSENT) {
        /* A marker of this boot not cleared: that resend skipped. */
        CmLogNote(nhi, "cm.osc.guard", 0x300UL | (kind & 0xFFUL));
        return;
    }
    reason = cmOsnHostByBus(nhi, &f);
    if (reason != 0 || f.UspBdf != nhi->OscUspBdf) {
        CmLogNote(nhi, "cm.osc.refused", reason != 0 ? reason : 0x102UL);
        return;
    }
    /* 12.8.15.17: the board decided under the mutex too, so the read
     * path's tables are read again for this send under it. */
    data = 0;
    cmOsnLock0();
    eb = 0;
    ee = 0;
    if (cmOsnEcamSys) {
        /* 12.8.15.18: the base afresh, only under the reads opt-in. */
        if (nhi->Values.HandoverReads == 0) {
            nhi->OscFlags |= CM_OSCN_F_ECAMOFF;
            CmLogNote(nhi, "cm.osc.refused", CM_OSN_R_ECAMOFF);
            cmOsnUnlock0();
            return;
        }
        reason = cmOsnEcamFind(nhi, &f, &eb, &ee);
        if (reason != 0) {
            CmLogNote(nhi, "cm.osc.refused", reason);
            cmOsnUnlock0();
            return;
        }
    }
    if (cmOsnBoard(nhi, &f, &data, eb, ee)) {
        (VOID)cmOsnSend(nhi, &f, data, TRUE, h, eb, ee);
    }
    cmOsnUnlock0();
}

VOID CmOscntRouter(PCM_NHI nhi)
{
#if defined(XHCI_FLAVOUR_QEMU)
    if (nhi->QemuOscNoClear != 0 && nhi->OscArmed != 0) {
        CmLogNote(nhi, "cm.osc.qemu.noclear", nhi->OscArmed);
        return;
    }
#endif
    if (cmOsnNt()) {
        cmOsnClear(nhi, CM_OSCG_E_ROUTER);
    }
}

VOID CmOscntShutdown(PCM_NHI nhi)
{
    if (cmOsnNt()) {
        cmOsnClear(nhi, CM_OSCG_E_SHUTDOWN);
    }
}

ULONG CmOscntReport(PCM_NHI nhi)
{
    CM_OSN_HOST *h;
    KIRQL irql;
    ULONG v;

    v = (nhi->OscTier & 7UL) |
        (nhi->OscFlags & (CM_OSCN_F_GUARD | CM_OSCN_F_UNKNOWN |
                          CM_OSCN_F_NOROUTE | CM_OSCN_F_EXPIRED |
                          CM_OSCN_F_ECAMOFF));
    if (!cmOsnReady || nhi->OscUspBdf == 0 ||
        nhi->OscUspBdf == XHCI_TBT_NO_BDF) {
        return v;
    }
    KeAcquireSpinLock(&cmOsnLock, &irql);
    h = cmOsnHostOf(nhi->OscUspBdf);
    if (h != NULL) {
        if (h->Sent) {
            v |= 8UL;
        }
        if (h->TimedOut) {
            v |= CM_OSCN_F_TIMEOUT;
        }
    }
    KeReleaseSpinLock(&cmOsnLock, irql);
    return v;
}
