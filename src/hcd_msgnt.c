/*
 * hcd_msgnt.c - XhciInterruptMode 4 on Windows 2000, XP, Server 2003 and
 * XP x64: the seam to the library msi4nt (design record 15 sections 4.5.3,
 * 4.5.4, 4.5.6 and 4.5.16; roadmap-usb4 task 49.24). The one file that
 * compiles msi4nt.h's implementation (src\msi4nt.h, vendored from msi4nt's
 * version 1, read 2026-11-12; legal-provenance.md section 2a), hosted,
 * without its Vista path and with the ISA claim: its pool is hcd_pool.c's, its
 * configuration access this controller's IRP_MN_READ_CONFIG route, its
 * names hcd_msi86.c's (the x86 helper's minor-1 routine, or amd64's
 * MmGetSystemRoutineAddress).
 *
 * The lifetime around these calls - when each is made and what follows a
 * failure - is hcd_msg4.c's; the library is never initialised on 98 SE or
 * ME (hcd_msg4.c decides by the system first).
 *
 * IRQL: PASSIVE_LEVEL, in the PnP manager's threads, the power work item
 * or the controller thread, none holding a processor affinity of this
 * driver's; HcdMsgNtSync any IRQL up to DISPATCH_LEVEL; HcdMsgNtIsrSaw at
 * the connection's DIRQL. Each tagged where it differs.
 */

#include "hcd.h"
#include "hcd_svc.h"
#include "xhci_hw.h"

#define MSI4NT_HOSTED
#define MSI4NT_NO_NATIVE
#define MSI4NT_ISA_LINE
#define MSI4NT_IMPLEMENTATION
#include "msi4nt.h"

/* The resource map's layout, the pure walk's (xhci_msg4.h), one level at a
 * time (MSVC 6.0 takes no nested member in a constant FIELD_OFFSET). */
XHCI_C_ASSERT(msgnt_rl_list,
              FIELD_OFFSET(CM_RESOURCE_LIST, List) == XHCI_MSG4_RL_HEADER);
XHCI_C_ASSERT(msgnt_full_count,
              FIELD_OFFSET(CM_FULL_RESOURCE_DESCRIPTOR, PartialResourceList) +
                      FIELD_OFFSET(CM_PARTIAL_RESOURCE_LIST, Count) ==
                  XHCI_MSG4_FULL_COUNT);
XHCI_C_ASSERT(msgnt_full_partials,
              FIELD_OFFSET(CM_FULL_RESOURCE_DESCRIPTOR, PartialResourceList) +
                      FIELD_OFFSET(CM_PARTIAL_RESOURCE_LIST,
                                   PartialDescriptors) ==
                  XHCI_MSG4_FULL_HEADER);
XHCI_C_ASSERT(msgnt_pd_u,
              FIELD_OFFSET(CM_PARTIAL_RESOURCE_DESCRIPTOR, u) ==
                  XHCI_MSG4_PD_DATASIZE);
XHCI_C_ASSERT(msgnt_pd_vector,
              XHCI_MSG4_PD_DATASIZE + sizeof(ULONG) == XHCI_MSG4_PD_VECTOR);
XHCI_C_ASSERT(msgnt_type_int,
              CmResourceTypeInterrupt == XHCI_MSG4_TYPE_INTERRUPT &&
                  CmResourceTypeDeviceSpecific == XHCI_MSG4_TYPE_DEVSPECIFIC);
/* The numbers hcd_msg4.c names (section 4.5.9). */
XHCI_C_ASSERT(msgnt_status, MSGIRQ_STATUS_CONFIG_ACCESS == 10 &&
                                MSGIRQ_STATUS_CLAIM_KEPT == 12 &&
                                MSGIRQ_STATUS_BRAKED == 13 &&
                                MSGIRQ_STATUS_NO_INPUTS == 14);

/* The library's numbers of this driver's (decision 10). */
#define HCD_MSGNT_QUIET_MS          500UL
#define HCD_MSGNT_BRAKE_ENTRIES     256UL
#define HCD_MSGNT_RETRY_MS          10UL

/* The veto's outcomes. */
#define HCD_MSGNT_VETO_CLEAR        0UL
#define HCD_MSGNT_VETO_SHARED       1UL
#define HCD_MSGNT_VETO_UNKNOWN      2UL

/* The image's: the library initialised (once, after the look), the
 * resolved IoInvalidateDeviceState, and DriverEntry's registry path the
 * upper pool's charges are kept under. Under HcdMsgNtLock. */
static KEVENT HcdMsgNtLock;
static ULONG HcdMsgNtReady;
typedef VOID (NTAPI *HCD_INVALIDATE_STATE)(PDEVICE_OBJECT pdo);
static HCD_INVALIDATE_STATE HcdMsgNtInvalidate;
static UNICODE_STRING HcdMsgNtRegistryPath;
static WCHAR HcdMsgNtRegistryBuffer[256];

/* IRQL: PASSIVE_LEVEL, DriverEntry. The path is copied, at most 255
 * characters (a longer one leaves the upper pool refused by the
 * library). */
VOID HcdMsgNtInit(PUNICODE_STRING registryPath)
{
    USHORT n;
    USHORT i;

    KeInitializeEvent(&HcdMsgNtLock, SynchronizationEvent, TRUE);
    HcdMsgNtReady = 0;
    HcdMsgNtInvalidate = NULL;
    n = 0;
    if (registryPath != NULL && registryPath->Buffer != NULL) {
        n = (USHORT)(registryPath->Length / sizeof(WCHAR));
        if (n > 255) {
            n = 0;
        }
        for (i = 0; i < n; i++) {
            HcdMsgNtRegistryBuffer[i] = registryPath->Buffer[i];
        }
    }
    HcdMsgNtRegistryBuffer[n] = 0;
    HcdMsgNtRegistryPath.Buffer = HcdMsgNtRegistryBuffer;
    HcdMsgNtRegistryPath.Length = (USHORT)(n * sizeof(WCHAR));
    HcdMsgNtRegistryPath.MaximumLength = (USHORT)sizeof(HcdMsgNtRegistryBuffer);
}

/* ---- the host table and the configuration routines -------------------- */

static PVOID __stdcall hcdMsgNtResolve(PVOID context, PCWSTR name)
{
    UNREFERENCED_PARAMETER(context);
    return HcdMsiResolve(name);
}

static PVOID __stdcall hcdMsgNtAlloc(PVOID context, ULONG bytes)
{
    UNREFERENCED_PARAMETER(context);
    return HcdPoolAlloc(bytes);
}

static VOID __stdcall hcdMsgNtFree(PVOID context, PVOID block)
{
    UNREFERENCED_PARAMETER(context);
    HcdPoolFree(block);
}

static ULONG __stdcall hcdMsgNtCfgRead(PVOID context, ULONG offset,
                                       PVOID buffer, ULONG length)
{
    return HcdSvcConfigBytes(&((PHCD_CONTROLLER)context)->Hc, TRUE, buffer,
                             offset, length);
}

static ULONG __stdcall hcdMsgNtCfgWrite(PVOID context, ULONG offset,
                                        PVOID buffer, ULONG length)
{
    PHCD_CONTROLLER hc;

    hc = (PHCD_CONTROLLER)context;
#if defined(XHCI_FLAVOUR_QEMU)
    /* XhciQemuMsg4Fail's bit for the call in progress: its writes fail,
     * as a configuration space that stops answering does. */
    if (hc->Msg4InjectNow) {
        return 0;
    }
#endif
    return HcdSvcConfigBytes(&hc->Hc, FALSE, buffer, offset, length);
}

static BOOLEAN __stdcall hcdMsgNtIsr(PKINTERRUPT Interrupt, PVOID Context,
                                     ULONG MessageID);

/* The parameters every call takes: the PDO, the location, the route, one
 * message; the source's fields are the caller's. */
static VOID hcdMsgNtParams(PHCD_CONTROLLER hc, MSGIRQ_CONNECT_PARAMS *params,
                           MSGIRQ_CONFIG_ACCESS *access)
{
    PUCHAR p;
    ULONG i;

    p = (PUCHAR)params;
    for (i = 0; i < sizeof(*params); i++) {
        p[i] = 0;
    }
    access->Read = hcdMsgNtCfgRead;
    access->Write = hcdMsgNtCfgWrite;
    access->ConfigContext = hc;
    params->Version = MSGIRQ_CONNECT_MESSAGE_BASED;
    params->MessageBased.PhysicalDeviceObject = hc->Pdo;
    params->MessageBased.Bus = (hc->Msg4Bdf >> 8) & 0xFFUL;
    params->MessageBased.Dev = (hc->Msg4Bdf >> 3) & 0x1FUL;
    params->MessageBased.Func = hc->Msg4Bdf & 0x7UL;
    params->MessageBased.ConfigAccess = access;
    params->MessageBased.MessageType = MSGIRQ_MESSAGE_ANY;
    params->MessageBased.MessagesWanted = 1;
    params->MessageBased.MessageServiceRoutine =
        (MSGIRQ_MESSAGE_SERVICE_ROUTINE)hcdMsgNtIsr;
    params->MessageBased.ServiceContext = hc;
    params->MessageBased.FallBackServiceRoutine = NULL;
    params->MessageBased.FloatingSave = FALSE;
    params->MessageBased.SpinLock = NULL;
    params->MessageBased.SynchronizeIrql = 0;
}

/* ---- the routine ------------------------------------------------------ */

/*
 * msi4nt's trampoline calls this for message 0 under the connection's
 * interrupt object lock: the gated routine of hcd_ctl.c, then, under
 * strategy 1 alone, the stray-entry brake (section 4.5.6): an entry not
 * claimed that finds no event at the ring's dequeue position is a stray
 * one; at the run's limit the drain is queued to latch the loss. Under
 * strategy 0 the brake is the library's. IRQL: the connection's DIRQL.
 */
static BOOLEAN __stdcall hcdMsgNtIsr(PKINTERRUPT Interrupt, PVOID Context,
                                     ULONG MessageID)
{
    PHCD_CONTROLLER hc;
    PXHCI_EXTENSION ext;
    BOOLEAN claimed;
    ULONG pending;

    UNREFERENCED_PARAMETER(Interrupt);

    hc = (PHCD_CONTROLLER)Context;
    claimed = HcdCtlMsg4Isr(hc, MessageID);
    if (hc->Msg4Claim.Strategy != XHCI_MSG4_STRATEGY_BORROW ||
        MessageID != 0 || !hc->Msg4Ready || hc->Msg4StrayTripped) {
        return claimed;
    }
    ext = &hc->Hc;
    pending = 0;
    if (!claimed && (ext->Flags & XHCI_EXT_FLAG_INITIALIZED) != 0 &&
        ext->HcInfoStatus == XHCI_HC_OK && ext->EventRing.Base != NULL) {
        pending = XhciEventRingPending(&ext->EventRing);
    }
    if (XhciMsg4StrayStep(&hc->Msg4StrayRun, claimed ? 1UL : 0UL,
                          pending)) {
        (VOID)InterlockedExchange(&hc->Msg4StrayTripped, 1);
        (VOID)InterlockedIncrement(&hc->DpcsInFlight);
        if (hc->DpcClosed || !KeInsertQueueDpc(&hc->IsrDpc, NULL, NULL)) {
            (VOID)InterlockedDecrement(&hc->DpcsInFlight);
        }
    }
    return claimed;
}

/* ---- the veto of strategy 1 (section 4.5.3) ---------------------------- */

typedef struct _HCD_MSGNT_VETO {
    const USHORT *Own;
    ULONG OwnChars;
    ULONG Vector;
    ULONG Lists;
    ULONG OwnSeen;
    ULONG Interrupts;
    ULONG Holder;           /* the value's index, +1; 0 none */
    ULONG Malformed;
    ULONG Index;
} HCD_MSGNT_VETO, *PHCD_MSGNT_VETO;

static NTSTATUS NTAPI hcdMsgNtMapValue(PWSTR ValueName, ULONG ValueType,
                                       PVOID ValueData, ULONG ValueLength,
                                       PVOID Context, PVOID EntryContext)
{
    PHCD_MSGNT_VETO v;
    ULONG chars;
    ULONG answer;

    UNREFERENCED_PARAMETER(EntryContext);

    v = (PHCD_MSGNT_VETO)Context;
    v->Index++;
    if (ValueName == NULL || ValueType != REG_RESOURCE_LIST) {
        return STATUS_SUCCESS;
    }
    for (chars = 0; ValueName[chars] != 0 && chars < 512; chars++) {
    }
    if (!XhciMsg4NameIsTranslated((const USHORT *)ValueName, chars)) {
        return STATUS_SUCCESS;
    }
    if (XhciMsg4NameIsOwn((const USHORT *)ValueName, chars, v->Own,
                          v->OwnChars)) {
        v->OwnSeen = 1;
        return STATUS_SUCCESS;
    }
    v->Lists++;
    answer = XhciMsg4ShareScan((const UCHAR *)ValueData, ValueLength,
                               sizeof(CM_PARTIAL_RESOURCE_DESCRIPTOR),
                               v->Vector, &v->Interrupts);
    if (answer == XHCI_MSG4_SHARE_FOUND && v->Holder == 0) {
        v->Holder = v->Index;
    } else if (answer == XHCI_MSG4_SHARE_MALFORMED) {
        v->Malformed++;
    }
    return STATUS_SUCCESS;
}

/* The PnP manager's resource map, every .Translated value but this
 * controller's own, then every present PCI function wired to the line;
 * a map or a walk that cannot be read is no clearance. */
static ULONG hcdMsgNtVeto(PHCD_CONTROLLER hc)
{
    RTL_QUERY_REGISTRY_TABLE table[2];
    HCD_MSGNT_VETO v;
    WCHAR own[128];
    NTSTATUS status;
    ULONG length;
    ULONG answer;
    ULONG bdf;
    ULONG id;
    ULONG functions;
    PUCHAR p;
    ULONG i;

    p = (PUCHAR)&v;
    for (i = 0; i < sizeof(v); i++) {
        p[i] = 0;
    }
    p = (PUCHAR)table;
    for (i = 0; i < sizeof(table); i++) {
        p[i] = 0;
    }
    length = 0;
    status = IoGetDeviceProperty(hc->Pdo,
                                 DevicePropertyPhysicalDeviceObjectName,
                                 sizeof(own) - sizeof(WCHAR), own, &length);
    if (!NT_SUCCESS(status) || length < 2 * sizeof(WCHAR)) {
        XhciLogNote(&hc->Hc, "msg4.shared.name", (ULONG)status);
        return HCD_MSGNT_VETO_UNKNOWN;
    }
    own[length / sizeof(WCHAR)] = 0;
    for (v.OwnChars = 0; own[v.OwnChars] != 0; v.OwnChars++) {
    }
    v.Own = (const USHORT *)own;
    v.Vector = hc->IntVector;
    table[0].QueryRoutine = hcdMsgNtMapValue;
    table[0].Flags = 0;
    table[0].Name = NULL;
    status = RtlQueryRegistryValues(
        RTL_REGISTRY_ABSOLUTE,
        L"\\Registry\\Machine\\HARDWARE\\RESOURCEMAP\\PnP Manager\\PnpManager",
        table, &v, NULL);
    XhciLogNote(&hc->Hc, "msg4.shared.lists",
                (v.Lists << 16) | (v.Interrupts & 0xFFFFUL));
    if (v.Holder != 0) {
        XhciLogNote(&hc->Hc, "msg4.shared", v.Holder);
        return HCD_MSGNT_VETO_SHARED;
    }
    if (!NT_SUCCESS(status) || !v.OwnSeen || v.Malformed != 0) {
        XhciLogNote(&hc->Hc, "msg4.shared.unknown",
                    NT_SUCCESS(status)
                        ? ((v.OwnSeen ? 0UL : 1UL) | (v.Malformed << 8))
                        : (ULONG)status);
        return HCD_MSGNT_VETO_UNKNOWN;
    }
    answer = HcdCtlLineShared(hc, &bdf, &id, &functions);
    XhciLogNote(&hc->Hc, "msg4.shared.pci", (answer << 24) | bdf);
    if (answer == XHCI_POLL_HELD_FOUND) {
        XhciLogNote(&hc->Hc, "msg4.shared.pci.id", id);
        return HCD_MSGNT_VETO_SHARED;
    }
    if (answer != XHCI_POLL_HELD_NONE) {
        return HCD_MSGNT_VETO_UNKNOWN;
    }
    XhciLogNote(&hc->Hc, "msg4.shared", 0);
    return HCD_MSGNT_VETO_CLEAR;
}

/* ---- the library, once per image (section 4.5.3, step 1) -------------- */

/* 1 when initialised: made only once the look has the name routine, so a
 * start before the helper's first boot initialises nothing and a later one
 * looks again; IoInvalidateDeviceState resolved first (no lifetime runs
 * without its way to report a failure). The image is kept for the boot
 * once the library has mapped the local APIC. */
static ULONG hcdMsgNtLibrary(PHCD_CONTROLLER hc)
{
    MSGIRQ_HOST host;
    ULONG status;
    PVOID invalidate;

    (VOID)HcdWaitEvent(&HcdMsgNtLock, 0);
    if (!HcdMsgNtReady) {
        if (!HcdMsiFindRoutineLook(hc)) {
            XhciLogNote(&hc->Hc, "msg4.init", 0xFFFFFFFFUL);
        } else {
            invalidate = HcdMsiResolve(L"IoInvalidateDeviceState");
            if (invalidate == NULL) {
                XhciLogNote(&hc->Hc, "msg4.init", 0xFFFFFFFEUL);
            } else {
                HcdMsgNtInvalidate = (HCD_INVALIDATE_STATE)invalidate;
                host.Size = sizeof(host);
                host.HostContext = NULL;
                host.Resolve = hcdMsgNtResolve;
                host.Allocate = hcdMsgNtAlloc;
                host.Free = hcdMsgNtFree;
                status = MsgIrqInitialize(&host);
                XhciLogNote(&hc->Hc, "msg4.init", status);
                HcdMsgNtReady = 1;
                HcdDmaKeepImage();
            }
        }
    }
    (VOID)KeSetEvent(&HcdMsgNtLock, IO_NO_INCREMENT, FALSE);
    return HcdMsgNtReady;
}

/* ---- the claim of strategy 0 (section 4.5.3, step 3) ------------------- */

/* The result, logged whole and into the counter block (section 4.5.16
 * item 3), the upper inputs' reasons as runs. */
static VOID hcdMsgNtResult(PHCD_CONTROLLER hc, ULONG status,
                           const MSGIRQ_ISA_RESULT *r)
{
    PXHCI_EXTENSION ext;
    ULONG i;
    ULONG first;
    ULONG budget;
    ULONG firstRefused;

    ext = &hc->Hc;
    hc->Counters.Msg4ClaimStatus = status;
    if (status == MSGIRQ_STATUS_INVALID_PARAMETER) {
        /* The library leaves the result untouched then. */
        XhciLogNote(ext, "msg4.claim.result", 0);
        return;
    }
    hc->Counters.Msg4Irq = r->Irq;
    hc->Counters.Msg4Members = r->Members;
    hc->Counters.Msg4Sci = r->Sci;
    hc->Counters.Msg4Sources = r->Sources;
    {
        ULONG w[4];

        XhciMsg4PackReasons(r->Reason, w);
        hc->Counters.Msg4IrqReason0 = w[0];
        hc->Counters.Msg4IrqReason1 = w[1];
        hc->Counters.Msg4IrqReason2 = w[2];
        hc->Counters.Msg4IrqReason3 = w[3];
    }
    XhciLogNote(ext, "msg4.claim.irq", (r->Members << 16) | r->Irq);
    for (i = 0; i < 16; i++) {
        if (r->Reason[i] != MSGIRQ_ISA_NOT_TRIED) {
            XhciLogNote(ext, "msg4.claim.reason",
                        (i << 24) | ((ULONG)r->Reason[i] << 16) |
                            (r->Detail[i] & 0xFFFFUL));
        }
    }
    hc->Counters.Msg4PoolsScanned = r->PoolsScanned;
    hc->Counters.Msg4HighFirst = r->HighFirst;
    hc->Counters.Msg4HighLast = r->HighLast;
    hc->Counters.Msg4HighIoApics = r->HighIoApics;
    hc->Counters.Msg4HighIoApicCount = r->HighIoApicCount;
    hc->Counters.Msg4HighEnd = r->HighEnd;
    hc->Counters.Msg4HighLastInput = r->HighLastInput;
    hc->Counters.Msg4HighDetail = r->HighDetail;
    hc->Counters.Msg4HighBudget = r->HighBudget;
    hc->Counters.Msg4HighNew = r->HighNew;
    hc->Counters.Msg4HighSpent = r->HighSpent;
    if ((r->PoolsScanned & MSGIRQ_POOL_HIGH) == 0) {
        return;
    }
    XhciLogNote(ext, "msg4.claim.high",
                (r->HighFirst << 24) | (r->HighLast << 16) |
                    (r->HighIoApicCount << 8) | (r->HighEnd & 0xFFUL));
    XhciLogNote(ext, "msg4.claim.high.budget",
                (r->HighBudget << 16) | (r->HighNew << 8) | r->HighSpent);
    XhciLogNote(ext, "msg4.claim.high.detail", r->HighDetail);
    budget = 0;
    firstRefused = 0;
    first = MSGIRQ_HIGH_MIN;
    for (i = MSGIRQ_HIGH_MIN; i <= MSGIRQ_HIGH_MAX + 1; i++) {
        if (i <= MSGIRQ_HIGH_MAX && r->HighReason[i] == MSGIRQ_ISA_BUDGET) {
            budget++;
        }
        if (i <= MSGIRQ_HIGH_MAX && r->HighReason[i] != MSGIRQ_ISA_NOT_TRIED &&
            r->HighReason[i] != MSGIRQ_ISA_CHOSEN && firstRefused == 0) {
            firstRefused = (i << 8) | r->HighReason[i];
        }
        if (i > MSGIRQ_HIGH_MAX || r->HighReason[i] != r->HighReason[first]) {
            if (r->HighReason[first] != MSGIRQ_ISA_NOT_TRIED) {
                XhciLogNote(ext, "msg4.claim.high.reason",
                            (first << 16) | ((i - 1) << 8) |
                                r->HighReason[first]);
            }
            first = i;
        }
    }
    hc->Counters.Msg4HighBudgetRefused = budget;
    hc->Counters.Msg4HighFirstRefused = firstRefused;
}

/* MSGIRQ_STATUS_OK with the claim held, else the refusal's status and
 * *reason. IRQL: PASSIVE_LEVEL, a PnP IRP's thread. */
static ULONG hcdMsgNtClaim(PHCD_CONTROLLER hc, MSGIRQ_CONNECT_PARAMS *params,
                           PULONG reason)
{
    MSGIRQ_ISA_PARAMS opt;
    MSGIRQ_ISA_RESULT *res;
    PUCHAR p;
    ULONG status;
    ULONG i;

    res = (MSGIRQ_ISA_RESULT *)HcdPoolAlloc(sizeof(MSGIRQ_ISA_RESULT));
    if (res == NULL) {
        *reason = XHCI_POLL_R_MSG4_CLAIM_REFUSED;
        return MSGIRQ_STATUS_NO_MEMORY;
    }
    p = (PUCHAR)&opt;
    for (i = 0; i < sizeof(opt); i++) {
        p[i] = 0;
    }
    p = (PUCHAR)res;
    for (i = 0; i < sizeof(*res); i++) {
        p[i] = 0;
    }
    opt.Size = sizeof(opt);
    res->Size = sizeof(*res);
    opt.DriverObject = hc->Common.Self->DriverObject;
    if (hc->Msg4Claim.Irq != 0) {
        opt.Flags |= MSGIRQ_ISA_FLAG_OVERRIDE;
        opt.CandidateCount = 1;
        opt.Candidates[0] = (UCHAR)hc->Msg4Claim.Irq;
    }
    if (hc->Msg4Share != 0) {
        /* XhciMsiShare fills a line before another is taken (the owner,
         * 2026-11-13); JOIN_FIRST is valid only with SHARE. */
        opt.Flags |= MSGIRQ_ISA_FLAG_SHARE | MSGIRQ_ISA_FLAG_JOIN_FIRST;
    }
    opt.QuietMilliseconds = HCD_MSGNT_QUIET_MS;
    opt.BrakeEntries = HCD_MSGNT_BRAKE_ENTRIES;
    opt.Exclude = 0;
    opt.MaxMembers = XhciMsg4MaxMembers(hc->Msg4Share);
    /* A cap above the library's own would refuse every claim, the first
     * controller's too: held to the library's. */
    if (opt.MaxMembers > MSGIRQP_ISA_MAX_MEMBERS) {
        opt.MaxMembers = MSGIRQP_ISA_MAX_MEMBERS;
    }
    opt.Pools = hc->Msg4Claim.LibPools;
    if ((opt.Pools & MSGIRQ_POOL_HIGH) != 0) {
        opt.HighFirst = hc->Msg4Claim.HighFirst;
        opt.HighLast = hc->Msg4Claim.HighLast;
        opt.HighBudget = hc->Msg4Claim.Budget;
        opt.RegistryPath = &HcdMsgNtRegistryPath;
    }
    status = MsgIrqIsaClaim(params, &opt, res);
    XhciLogNote(&hc->Hc, "msg4.claim", status);
    hcdMsgNtResult(hc, status, res);
    HcdPoolFree(res);
    if (status == MSGIRQ_STATUS_OK) {
        hc->Msg4ClaimHeld = 1;
        return status;
    }
    if (status == MSGIRQ_STATUS_NO_VECTORS ||
        status == MSGIRQ_STATUS_NO_INPUTS) {
        *reason = XHCI_POLL_R_MSG4_NO_IRQ;
    } else {
        *reason = XHCI_POLL_R_MSG4_CLAIM_REFUSED;
        if (status == MSGIRQ_STATUS_CLAIM_KEPT) {
            HcdMsg4EnterRefusal(hc);
        }
    }
    return status;
}

/* The claim's release, a kept claim entered in the refusal table. IRQL:
 * PASSIVE_LEVEL, a PnP IRP's thread. */
VOID HcdMsgNtReleaseClaim(PHCD_CONTROLLER hc)
{
    MSGIRQ_CONNECT_PARAMS params;
    MSGIRQ_CONFIG_ACCESS access;
    ULONG status;

    if (!hc->Msg4ClaimHeld) {
        return;
    }
    hcdMsgNtParams(hc, &params, &access);
    status = MsgIrqIsaRelease(&params);
    hc->Msg4ClaimHeld = 0;
    XhciLogNote(&hc->Hc, "msg4.release", status);
    if (status == MSGIRQ_STATUS_CLAIM_KEPT) {
        HcdMsg4EnterRefusal(hc);
    }
}

/* ---- the connect (section 4.5.3, steps 1 to 4) ------------------------- */

/* `reconnect` 1 at a cancelled stop: the claim it still holds, the veto
 * again under strategy 1. MSGIRQ_STATUS_OK, or the status with *reason. A
 * failed connect that returned a live connection keeps it in
 * hc->Msg4Info for the release. IRQL: PASSIVE_LEVEL. */
ULONG HcdMsgNtConnect(PHCD_CONTROLLER hc, ULONG reconnect, PULONG reason)
{
    MSGIRQ_CONNECT_PARAMS params;
    MSGIRQ_CONFIG_ACCESS access;
    PMSGIRQ_MESSAGE_INFO info;
    USHORT command;
    ULONG matches;
    ULONG status;

    *reason = XHCI_POLL_R_MSG4_REFUSED;
    if (!hcdMsgNtLibrary(hc)) {
        return MSGIRQ_STATUS_UNSUPPORTED;
    }
    XhciLogNote(&hc->Hc, "msg4.mode", MsgIrqGetMode());
    if (!reconnect) {
        matches = 0;
        hc->Msg4BdfKnown = HcdCtlMsg4Location(hc, &hc->Msg4Bdf, &matches);
        XhciLogNote(&hc->Hc, "msg4.bdf",
                    (hc->Msg4BdfKnown << 16) | hc->Msg4Bdf);
        if (!hc->Msg4BdfKnown) {
            return HCD_MSG4_ST_LOCATION;
        }
    }
    hcdMsgNtParams(hc, &params, &access);
    if (hc->Msg4Claim.Strategy == XHCI_MSG4_STRATEGY_CLAIM) {
        if (!reconnect) {
            status = hcdMsgNtClaim(hc, &params, reason);
            if (status != MSGIRQ_STATUS_OK) {
                return status;
            }
        }
        params.MessageBased.Flags = MSGIRQ_CONNECT_FLAG_ISA_LINE;
    } else {
        if (hcdMsgNtVeto(hc) != HCD_MSGNT_VETO_CLEAR) {
            *reason = XHCI_POLL_R_MSG4_SHARED;
            return MSGIRQ_STATUS_NO_VECTORS;
        }
        XhciLogNote(&hc->Hc, "msg4.line",
                    (hc->IntShare << 24) | (hc->IntVector & 0xFFFFFFUL));
        params.MessageBased.LineInterrupt.Vector = hc->IntVector;
        params.MessageBased.LineInterrupt.Irql = hc->IntIrql;
        params.MessageBased.LineInterrupt.Affinity = hc->IntAffinity;
        params.MessageBased.LineInterrupt.Mode = hc->IntMode;
        params.MessageBased.LineInterrupt.ShareDisposition = hc->IntShare;
        params.MessageBased.Flags = MSGIRQ_CONNECT_FLAG_PIN_PRIVATE;
    }
    info = NULL;
#if defined(XHCI_FLAVOUR_QEMU)
    hc->Msg4InjectNow = (reconnect && (hc->QemuMsg4Fail & 0x8UL) != 0) ? 1L
                                                                        : 0L;
#endif
    status = MsgIrqConnect(&params, &info);
#if defined(XHCI_FLAVOUR_QEMU)
    hc->Msg4InjectNow = 0;
#endif
    hc->Msg4Info = info;
    hc->Interrupt = (info != NULL) ? info->MessageInfo[0].InterruptObject
                                   : NULL;
    hc->Msg4SyncObject = hc->Interrupt;
    if (status == MSGIRQ_STATUS_OK && info != NULL) {
        XhciLogNote(&hc->Hc, "msg4.vector", info->MessageInfo[0].Vector);
        XhciLogNote(&hc->Hc, "msg4.data", info->MessageInfo[0].MessageData);
        hc->Counters.Msg4Vector = info->MessageInfo[0].Vector;
        /* Interrupt Disable as the arm left it: whether the function has
         * the bit, which the absence proof's use at a removal needs
         * (section 4.5.4, the release's step 1). */
        command = 0;
        hc->Msg4IdImplemented =
            (XhciReadPciConfig(&hc->Hc, XHCI_PCI_COMMAND, &command,
                               sizeof(USHORT)) == MP_STATUS_SUCCESS &&
             command != 0xFFFFU &&
             (command & XHCI_PCI_COMMAND_INTX_DISABLE) != 0)
                ? 1UL
                : 0UL;
        return status;
    }
    if (info == NULL && !reconnect) {
        HcdMsgNtReleaseClaim(hc);
    }
    return status;
}

/* ---- the flag, the re-arm, the disconnect, the look -------------------- */

typedef struct _HCD_MSGNT_SYNC {
    PHCD_CONTROLLER Hc;
    LONG Ready;
} HCD_MSGNT_SYNC;

static BOOLEAN NTAPI hcdMsgNtSetReady(PVOID SynchronizeContext)
{
    HCD_MSGNT_SYNC *s;

    s = (HCD_MSGNT_SYNC *)SynchronizeContext;
    s->Hc->Msg4Ready = (s->Ready && !s->Hc->Msg4Gone) ? 1L : 0L;
    return TRUE;
}

/* The flag's write under the interrupt object's lock. IRQL: <= DISPATCH
 * (the callers are PASSIVE_LEVEL, section 4.5.5). */
ULONG HcdMsgNtSync(PHCD_CONTROLLER hc, LONG ready)
{
    HCD_MSGNT_SYNC s;

    if (hc->Msg4Info == NULL || hc->Interrupt == NULL) {
        hc->Msg4Ready = 0;
        return ready ? 0UL : 1UL;
    }
    s.Hc = hc;
    s.Ready = ready;
    (VOID)KeSynchronizeExecution(hc->Interrupt, hcdMsgNtSetReady, &s);
    return 1;
}

/* One MsgIrqReArm; the caller retries. IRQL: PASSIVE_LEVEL. */
ULONG HcdMsgNtReArm(PHCD_CONTROLLER hc)
{
    ULONG status;

    if (hc->Msg4Info == NULL) {
        return MSGIRQ_STATUS_INVALID_PARAMETER;
    }
#if defined(XHCI_FLAVOUR_QEMU)
    hc->Msg4InjectNow = ((hc->QemuMsg4Fail & 0x2UL) != 0) ? 1L : 0L;
#endif
    status = MsgIrqReArm((PMSGIRQ_MESSAGE_INFO)hc->Msg4Info);
#if defined(XHCI_FLAVOUR_QEMU)
    hc->Msg4InjectNow = 0;
#endif
    return status;
}

/* MsgIrqDisconnect, once more after 10 ms on MSGIRQ_STATUS_CONFIG_ACCESS.
 * OK: the connection is gone. IRQL: PASSIVE_LEVEL. */
ULONG HcdMsgNtDisconnect(PHCD_CONTROLLER hc, ULONG flags, ULONG injectBit)
{
    LARGE_INTEGER due;
    ULONG status;
    ULONG attempt;

    status = MSGIRQ_STATUS_CONFIG_ACCESS;
    for (attempt = 0; attempt < 2 && hc->Msg4Info != NULL; attempt++) {
        if (attempt != 0) {
            HcdRelativeMs(&due, HCD_MSGNT_RETRY_MS);
            (VOID)KeDelayExecutionThread(KernelMode, FALSE, &due);
        }
#if defined(XHCI_FLAVOUR_QEMU)
        hc->Msg4InjectNow = ((hc->QemuMsg4Fail & injectBit) != 0) ? 1L : 0L;
#else
        UNREFERENCED_PARAMETER(injectBit);
#endif
        status = MsgIrqDisconnect((PMSGIRQ_MESSAGE_INFO)hc->Msg4Info, flags);
#if defined(XHCI_FLAVOUR_QEMU)
        hc->Msg4InjectNow = 0;
#endif
        if (status != MSGIRQ_STATUS_CONFIG_ACCESS) {
            break;
        }
    }
    XhciLogNote(&hc->Hc, "msg4.disconnect", (flags << 24) | status);
    if (status == MSGIRQ_STATUS_OK) {
        hc->Msg4Info = NULL;
        hc->Interrupt = NULL;
        hc->Msg4SyncObject = NULL;
    }
    return status;
}

/*
 * The final release (section 4.5.4, steps 1 to 4), the flag cleared and
 * the causes masked by the caller: the plain disconnect with the
 * controller proven silent; without that proof, at a removal Windows
 * reported and for a function with Interrupt Disable, the absence proof's
 * form; still live, Bus Master Enable cleared and the quarantine, the
 * function entered, the claim kept with its vector; else the claim
 * released. IRQL: PASSIVE_LEVEL, a PnP IRP's thread.
 */
VOID HcdMsgNtRelease(PHCD_CONTROLLER hc, ULONG silent)
{
    ULONG status;

    status = MSGIRQ_STATUS_CONFIG_ACCESS;
    if (hc->Msg4Info != NULL && silent) {
        status = HcdMsgNtDisconnect(hc, 0, 0x1UL);
    }
    if (hc->Msg4Info != NULL && hc->Msg4Removal && hc->Msg4IdImplemented) {
        status = HcdMsgNtDisconnect(hc, MSGIRQ_DISCONNECT_ABSENT_OK, 0x1UL);
    }
    if (hc->Msg4Info != NULL) {
        HcdMsg4ClearBusMaster(hc);
        (VOID)MsgIrqDisconnect((PMSGIRQ_MESSAGE_INFO)hc->Msg4Info,
                               MSGIRQ_DISCONNECT_QUARANTINE);
        hc->Msg4Info = NULL;
        hc->Interrupt = NULL;
        hc->Msg4SyncObject = NULL;
        hc->Msg4ClaimHeld = 0;
        XhciLogNote(&hc->Hc, "msg4.quarantine.nt", status);
        HcdMsg4EnterRefusal(hc);
        return;
    }
    HcdMsgNtReleaseClaim(hc);
}

/* The library's brake on a claim (section 4.5.6), the thread's look: 1
 * when it tripped. The counters into the block. IRQL: PASSIVE_LEVEL, the
 * power gate held. */
ULONG HcdMsgNtLook(PHCD_CONTROLLER hc)
{
    MSGIRQ_ISA_STATE state;
    ULONG status;

    if (hc->Msg4Info == NULL ||
        hc->Msg4Claim.Strategy != XHCI_MSG4_STRATEGY_CLAIM) {
        return 0;
    }
    state.Size = sizeof(state);
    state.Braked = 0;
    state.Entries = 0;
    state.Stray = 0;
    state.Run = 0;
    state.MemberEntries = 0;
    state.MemberStray = 0;
    status = MsgIrqIsaQuery((PMSGIRQ_MESSAGE_INFO)hc->Msg4Info, &state);
    if (status != MSGIRQ_STATUS_OK) {
        return 0;
    }
    hc->Counters.Msg4BrakeEntries = state.Entries;
    hc->Counters.Msg4BrakeStray = state.Stray;
    hc->Counters.Msg4BrakeRun = state.Run;
    hc->Counters.Msg4MemberEntries = state.MemberEntries;
    hc->Counters.Msg4MemberStray = state.MemberStray;
    if (state.Braked) {
        XhciLogNote(&hc->Hc, "msg4.brake",
                    (state.Stray << 8) | (state.Run & 0xFFUL));
    }
    return state.Braked ? 1UL : 0UL;
}

/* Windows told the controller failed (section 4.5.4, step 5). IRQL:
 * PASSIVE_LEVEL. */
VOID HcdMsgNtReportFailed(PHCD_CONTROLLER hc)
{
    if (HcdMsgNtInvalidate != NULL) {
        HcdMsgNtInvalidate(hc->Pdo);
    }
}
