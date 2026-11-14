/*
 * cm_rirqnt.c - the NHI's ring interrupt by a message on Windows 2000, XP,
 * Server 2003 and XP x64: the seam to the library msi4nt (design record 21
 * section 4.5.14; roadmap-usb4.md task 49.27). The one file of xhcicm.sys
 * that compiles msi4nt.h's implementation (..\msi4nt.h, the copy
 * xhci98.sys's hcd_msgnt.c compiles too; legal-provenance.md section 2a),
 * hosted, without its Vista path and with the ISA claim: its pool is
 * cm_pool.c's, its configuration access this NHI's own PDO route
 * (cm_thr.c), its names CmRoutine's (amd64's MmGetSystemRoutineAddress, or
 * the x86 helper's minor-1 routine). Each image holds its own copy of the
 * library: xhci98.sys's claims and this driver's never join, and the PnP
 * manager's arbiter decides between them (section 4.5.14, "Two drivers").
 *
 * The lifetime around these calls - when each is made and what follows a
 * failure - is cm_rirqos.c's; nothing here is called on 98 SE or ME, nor on
 * Vista and later (CmRirqHow). Each returns an MSGIRQ_STATUS_* value
 * (CM_MB_* in cm_rirqos.h). This file also keeps the image-wide gate and
 * the image's refusal table (section 4.5.14).
 *
 * IRQL: PASSIVE_LEVEL on the connection-manager thread, a system thread in
 * the system process with no affinity of this driver's (the claim, the
 * connect and the release ask for exactly that).
 */

#include "cm.h"

#define MSI4NT_HOSTED
#define MSI4NT_NO_NATIVE
#define MSI4NT_ISA_LINE
#define MSI4NT_IMPLEMENTATION
#include "../msi4nt.h"

XHCI_C_ASSERT(cm_mn_status, MSGIRQ_STATUS_OK == CM_MB_OK &&
                                MSGIRQ_STATUS_IN_USE == CM_MB_IN_USE &&
                                MSGIRQ_STATUS_INVALID_PARAMETER ==
                                    CM_MB_INVALID &&
                                MSGIRQ_STATUS_CONFIG_ACCESS ==
                                    CM_MB_CONFIG_ACCESS &&
                                MSGIRQ_STATUS_CLAIM_KEPT == CM_MN_CLAIM_KEPT &&
                                MSGIRQ_STATUS_BRAKED == CM_MB_BRAKED);

/*
 * Both images (section 4.5.14): the image-wide try-gate on the two
 * interlocked imports, which serialises the library's one-time
 * initialisation and this table, and the image's refusal table (sections
 * 4.5.12 step 1 and 4.5.14): functions whose last disconnect had to be
 * forced, kept until Windows restarts (the image stays resident once an
 * interrupt was connected or a claim made). On the threads.
 */
static LONG cmRirqGate;

#define CM_RIRQ_REFUSED_MAX 8UL
static ULONG cmRirqRefusedList[CM_RIRQ_REFUSED_MAX];
static ULONG cmRirqRefusedCount;
static ULONG cmRirqRefusedLost;

VOID CmRirqGateEnter(PCM_NHI nhi)
{
    while (InterlockedIncrement(&cmRirqGate) != 1) {
        (VOID)InterlockedDecrement(&cmRirqGate);
        nhi->Host.Sleep(nhi->Host.Context, 1);
    }
}

VOID CmRirqGateLeave(VOID)
{
    (VOID)InterlockedDecrement(&cmRirqGate);
}

BOOLEAN CmRirqRefused(PCM_NHI nhi, ULONG bdf)
{
    ULONG i;
    BOOLEAN found;

    found = FALSE;
    CmRirqGateEnter(nhi);
    for (i = 0; i < cmRirqRefusedCount; i++) {
        if (cmRirqRefusedList[i] == bdf) {
            found = TRUE;
            break;
        }
    }
    CmRirqGateLeave();
    return found;
}

/* TRUE when entered (or already there); FALSE when the table was full, in
 * which case the caller's failure latch refuses that function for the
 * rest of the activation. */
BOOLEAN CmRirqRefuse(PCM_NHI nhi, ULONG bdf)
{
    ULONG i;
    BOOLEAN done;

    done = FALSE;
    CmRirqGateEnter(nhi);
    for (i = 0; i < cmRirqRefusedCount; i++) {
        if (cmRirqRefusedList[i] == bdf) {
            done = TRUE;
            break;
        }
    }
    if (!done && cmRirqRefusedCount < CM_RIRQ_REFUSED_MAX) {
        cmRirqRefusedList[cmRirqRefusedCount++] = bdf;
        done = TRUE;
    } else if (!done) {
        cmRirqRefusedLost++;
    }
    CmRirqGateLeave();
    return done;
}

/* The library initialised once per image (section 4.5.14): under the
 * image-wide gate (CmRirqGateEnter). */
static ULONG cmRirqNtReady;
static ULONG cmRirqNtInitStatus;

static PVOID __stdcall cmRirqNtResolve(PVOID context, PCWSTR name)
{
    UNREFERENCED_PARAMETER(context);
    return CmRoutine(name);
}

static PVOID __stdcall cmRirqNtAlloc(PVOID context, ULONG bytes)
{
    UNREFERENCED_PARAMETER(context);
    return CmAlloc(bytes);
}

static VOID __stdcall cmRirqNtFree(PVOID context, PVOID block)
{
    UNREFERENCED_PARAMETER(context);
    CmFree(block);
}

static ULONG __stdcall cmRirqNtCfgRead(PVOID context, ULONG offset,
                                       PVOID buffer, ULONG length)
{
    return CmCfgBytes((PCM_NHI)context, TRUE, buffer, offset, length);
}

static ULONG __stdcall cmRirqNtCfgWrite(PVOID context, ULONG offset,
                                        PVOID buffer, ULONG length)
{
    return CmCfgBytes((PCM_NHI)context, FALSE, buffer, offset, length);
}

/* The library's trampoline calls this for message 0 under the
 * connection's interrupt object lock, at its DIRQL: the gated routine. */
static BOOLEAN __stdcall cmRirqNtIsr(PKINTERRUPT Interrupt, PVOID Context,
                                     ULONG MessageID)
{
    UNREFERENCED_PARAMETER(Interrupt);
    UNREFERENCED_PARAMETER(MessageID);

    return CmRirqIsrBody((PCM_NHI)Context);
}

/* The parameters every call takes: the PDO, the location, this NHI's own
 * configuration route, MSI, one message, the routine. */
static VOID cmRirqNtParams(PCM_NHI nhi, MSGIRQ_CONNECT_PARAMS *params,
                           MSGIRQ_CONFIG_ACCESS *access)
{
    ULONG bdf;

    CmZero(params, sizeof(*params));
    bdf = nhi->Rirq.MsgBdf;
    access->Read = cmRirqNtCfgRead;
    access->Write = cmRirqNtCfgWrite;
    access->ConfigContext = nhi;
    params->Version = MSGIRQ_CONNECT_MESSAGE_BASED;
    params->MessageBased.PhysicalDeviceObject = nhi->Common.Pdo;
    params->MessageBased.Bus = XHCI_TBT_BDF_BUS(bdf);
    params->MessageBased.Dev = XHCI_TBT_BDF_DEV(bdf);
    params->MessageBased.Func = XHCI_TBT_BDF_FN(bdf);
    params->MessageBased.ConfigAccess = access;
    params->MessageBased.MessageType = MSGIRQ_MESSAGE_MSI;
    params->MessageBased.MessagesWanted = 1;
    params->MessageBased.MessageServiceRoutine =
        (MSGIRQ_MESSAGE_SERVICE_ROUTINE)cmRirqNtIsr;
    params->MessageBased.ServiceContext = nhi;
    params->MessageBased.FallBackServiceRoutine = NULL;
    params->MessageBased.FloatingSave = FALSE;
    params->MessageBased.SpinLock = NULL;
    params->MessageBased.SynchronizeIrql = 0;
}

ULONG CmRirqNtInit(PCM_NHI nhi)
{
    MSGIRQ_HOST host;
    ULONG status;

    CmRirqGateEnter(nhi);
    if (!cmRirqNtReady) {
        host.Size = sizeof(host);
        host.HostContext = NULL;
        host.Resolve = cmRirqNtResolve;
        host.Allocate = cmRirqNtAlloc;
        host.Free = cmRirqNtFree;
        cmRirqNtInitStatus = MsgIrqInitialize(&host);
        /* Once: the library marks itself done before it resolves, so a
         * second call would not run it again (record 15 section 4.5.11
         * item 5); its mode says whether it can serve. */
        cmRirqNtReady = 1;
    }
    status = cmRirqNtInitStatus;
    if (status == MSGIRQ_STATUS_OK &&
        MsgIrqGetMode() != MSGIRQ_MODE_FAKE_IRQ) {
        status = MSGIRQ_STATUS_HAL_UNSUPPORTED;
    }
    CmRirqGateLeave();
    return status;
}

/* The claim of an ISA interrupt with the library's defaults (section
 * 4.5.14): exclusive, the ISA pool only, no candidates, no exclusions.
 * *irq the IRQ claimed. */
ULONG CmRirqNtClaim(PCM_NHI nhi, PULONG irq)
{
    MSGIRQ_CONNECT_PARAMS params;
    MSGIRQ_CONFIG_ACCESS access;
    MSGIRQ_ISA_PARAMS opt;
    MSGIRQ_ISA_RESULT *res;
    ULONG status;

    *irq = 0;
    res = (MSGIRQ_ISA_RESULT *)CmAlloc(sizeof(MSGIRQ_ISA_RESULT));
    if (res == NULL) {
        return MSGIRQ_STATUS_NO_MEMORY;
    }
    cmRirqNtParams(nhi, &params, &access);
    CmZero(&opt, sizeof(opt));
    CmZero(res, sizeof(*res));
    opt.Size = sizeof(opt);
    res->Size = sizeof(*res);
    opt.DriverObject = CmDriverObject;
    opt.Pools = MSGIRQ_POOL_ISA;
    status = MsgIrqIsaClaim(&params, &opt, res);
    if (status == MSGIRQ_STATUS_OK) {
        nhi->Rirq.MsgClaimHeld = 1;
        *irq = res->Irq;
    }
    CmFree(res);
    return status;
}

ULONG CmRirqNtRelease(PCM_NHI nhi)
{
    MSGIRQ_CONNECT_PARAMS params;
    MSGIRQ_CONFIG_ACCESS access;
    ULONG status;

    if (!nhi->Rirq.MsgClaimHeld) {
        return MSGIRQ_STATUS_OK;
    }
    cmRirqNtParams(nhi, &params, &access);
    status = MsgIrqIsaRelease(&params);
    nhi->Rirq.MsgClaimHeld = 0;
    return status;
}

ULONG CmRirqNtConnect(PCM_NHI nhi)
{
    MSGIRQ_CONNECT_PARAMS params;
    MSGIRQ_CONFIG_ACCESS access;
    PMSGIRQ_MESSAGE_INFO info;
    ULONG status;

    cmRirqNtParams(nhi, &params, &access);
    params.MessageBased.Flags = MSGIRQ_CONNECT_FLAG_ISA_LINE;
    info = NULL;
    status = MsgIrqConnect(&params, &info);
    /* A failed connect that hands back a live connection is kept for its
     * end, Reach never set. */
    nhi->Rirq.MsgInfo = info;
    nhi->Rirq.Object = (info != NULL) ? info->MessageInfo[0].InterruptObject
                                      : NULL;
    return status;
}

ULONG CmRirqNtReArm(PCM_NHI nhi)
{
    if (nhi->Rirq.MsgInfo == NULL) {
        return MSGIRQ_STATUS_INVALID_PARAMETER;
    }
    return MsgIrqReArm((PMSGIRQ_MESSAGE_INFO)nhi->Rirq.MsgInfo);
}

/* `how`: 0 the plain disconnect; 1 with the absence proof (a removal
 * Windows reported); 2 the quarantine, which always ends it. */
ULONG CmRirqNtDisconnect(PCM_NHI nhi, ULONG how)
{
    ULONG flags;
    ULONG status;

    if (nhi->Rirq.MsgInfo == NULL) {
        return MSGIRQ_STATUS_OK;
    }
    flags = how == 2 ? MSGIRQ_DISCONNECT_QUARANTINE
                     : (how == 1 ? MSGIRQ_DISCONNECT_ABSENT_OK : 0UL);
    status = MsgIrqDisconnect((PMSGIRQ_MESSAGE_INFO)nhi->Rirq.MsgInfo, flags);
    if (status == MSGIRQ_STATUS_OK) {
        nhi->Rirq.MsgInfo = NULL;
        nhi->Rirq.Object = NULL;
    }
    return status;
}

ULONG CmRirqNtVector(PCM_NHI nhi, PULONG data)
{
    PMSGIRQ_MESSAGE_INFO info;

    *data = 0;
    info = (PMSGIRQ_MESSAGE_INFO)nhi->Rirq.MsgInfo;
    if (info == NULL || info->MessageCount == 0) {
        return 0;
    }
    *data = info->MessageInfo[0].MessageData;
    return info->MessageInfo[0].Vector;
}
