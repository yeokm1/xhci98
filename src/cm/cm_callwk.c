/*
 * cm_callwk.c - the per-member call workers of Windows 98 SE and ME
 * (cm_call.h; roadmap-usb4.md task 42-A.12; design record 21 section 7.2,
 * "The 9x half", part 0).
 *
 * ONE WORKER PER MEMBER. Each member of the domain's set gets a system
 * thread of its own and a CONFIGMG route of its own (cm_cfgmg.c's thunks
 * and guards, opened by that worker as its first operation), and executes
 * one operation at a time, handed to it and taken back through the
 * member's handoff word (cm_call.c, interface 3's R6f) under the block's
 * spin lock. A call that blocks in the configuration manager blocks that
 * member's worker alone: the connection-manager thread never calls
 * CONFIGMG or PCI.VXD, and keeps running every transaction's deadline on
 * its own clock while a call runs.
 *
 * LIFETIME. The block is its own pool allocation, never the NHI's
 * extension, with one reference held by the domain (dropped by
 * CmCallwkStop) and one by each live worker (dropped as it leaves); the
 * last to let go closes the routes and frees it. The stop never waits for
 * a worker: one inside a call may be waiting on the very removal the stop
 * belongs to. Such a worker finishes its call alone and leaves; the image
 * stays resident on 98 SE and ME (no DriverUnload from DriverEntry, R4;
 * the CONFIGMG worker's rule, cm_cfgwk.c), so its code outlives the stop.
 * What a detached transaction still owes (the removal an accepted query
 * authorised, the member blocker of record 21 section 7.2 part 0) is its
 * client's state, kept where the client keeps it.
 *
 * WHAT RUNS HERE TODAY. Each worker's opening of its route, read back by
 * the thread once (CmCallwkService). The clients that hand operations to
 * the workers - record 28's state machine (task 42-A.15), the removal
 * transaction and the statement's slot reading (tasks 42-A.12 and 42-A.13)
 * - submit through CmCallwkSubmit and take through CmCallwkTake.
 *
 * IRQL: as cm_call.h. Windows 98 SE and ME only; on NT CmCallwkStart
 * makes nothing.
 */

#include "cm.h"
#include "cm_call.h"

/* The opening's result: opened, CM_CFGMG.Why, Get_Version's AX. */
#define CM_CW_OPEN_KIND     1UL
#define CM_CW_OPEN_BYTES    12UL

typedef struct _CM_CALLWK_M {
    CM_CALL Call;
    KEVENT Wake;            /* synchronization: an operation submitted    */
    struct _CM_CALLWK *Block;
    ULONG Index;
    ULONG Live;             /* its thread was made                        */
    ULONG OpenId;           /* the opening's operation id                 */
    ULONG OpenSeen;         /* ... read back                              */
    CM_CFGMG Cfgmg;         /* this worker's own route                    */
} CM_CALLWK_M;

typedef struct _CM_CALLWK {
    KSPIN_LOCK Lock;
    ULONG Refs;             /* the domain's and each live worker's        */
    ULONG Exit;             /* the stop asked every worker to leave       */
    ULONG Members;
    CM_CALLWK_M M[XHCI_TBT_MAX_TUNNELS];
} CM_CALLWK, *PCM_CALLWK;

static VOID cmCwRelease(PCM_CALLWK b)
{
    KIRQL irql;
    BOOLEAN last;
    ULONG i;

    KeAcquireSpinLock(&b->Lock, &irql);
    b->Refs--;
    last = (BOOLEAN)(b->Refs == 0);
    KeReleaseSpinLock(&b->Lock, irql);
    if (!last) {
        return;
    }
    for (i = 0; i < b->Members; i++) {
        CmCfgmgClose(&b->M[i].Cfgmg);
    }
    CmFree(b);
}

/* The opening of the worker's own route: Get_Version, the root's
 * Locate_DevNode and its Get_Device_ID (CmCfgmgOpen), read-only. */
static ULONG cmCwOpen(PVOID route, PVOID context, const VOID *op, VOID *res)
{
    PCM_CFGMG cfg;
    PULONG out;

    UNREFERENCED_PARAMETER(context);
    UNREFERENCED_PARAMETER(op);
    cfg = (PCM_CFGMG)route;
    out = (PULONG)res;
    out[0] = CmCfgmgOpen(cfg) ? 1UL : 0UL;
    out[1] = cfg->Why;
    out[2] = cfg->Version;
    return CM_CW_OPEN_BYTES;
}

static VOID NTAPI cmCwThread(PVOID context)
{
    CM_CALLWK_M *m;
    PCM_CALLWK b;
    KIRQL irql;
    BOOLEAN claimed;
    ULONG n;

    m = (CM_CALLWK_M *)context;
    b = m->Block;
    for (;;) {
        KeAcquireSpinLock(&b->Lock, &irql);
        if (b->Exit) {
            KeReleaseSpinLock(&b->Lock, irql);
            break;
        }
        /* SUBMITTED -> RUNNING under the lock: taken once (R6f). */
        claimed = CmCallClaim(&m->Call);
        KeReleaseSpinLock(&b->Lock, irql);
        if (!claimed) {
            (VOID)KeWaitForSingleObject(&m->Wake, Executive, KernelMode,
                                        FALSE, NULL);
            continue;
        }
        /* The call itself, outside the lock: the operation's arguments and
         * result room are the slot's, and nobody else writes them while
         * it reads RUNNING. */
        n = m->Call.Run(&m->Cfgmg, m->Call.Context, m->Call.Op,
                        m->Call.Res);
        KeAcquireSpinLock(&b->Lock, &irql);
        CmCallFinish(&m->Call, n);
        KeReleaseSpinLock(&b->Lock, irql);
    }
    cmCwRelease(b);
    (VOID)PsTerminateSystemThread(STATUS_SUCCESS);
}

PCM_CALLWK CmCallwkStart(struct _CM_NHI *nhi, ULONG members)
{
    PCM_CALLWK b;
    CM_CALLWK_M *m;
    NTSTATUS status;
    KIRQL irql;
    ULONG i;
    ULONG live;

    if (IoIsWdmVersionAvailable(1, 0x10)) {
        /* NT: the calls stay the bridge filter's. */
        return NULL;
    }
    b = (PCM_CALLWK)CmAlloc(sizeof(CM_CALLWK));
    if (b == NULL) {
        CmLogNote(nhi, "cm.callwk.no-block", members);
        return NULL;
    }
    KeInitializeSpinLock(&b->Lock);
    b->Members = members > XHCI_TBT_MAX_TUNNELS ? XHCI_TBT_MAX_TUNNELS
                                                : members;
    /* The domain's reference, plus one per worker made below, taken
     * before the thread can run (and so before it can let go). */
    b->Refs = 1;
    live = 0;
    for (i = 0; i < b->Members; i++) {
        m = &b->M[i];
        CmCallInit(&m->Call);
        KeInitializeEvent(&m->Wake, SynchronizationEvent, FALSE);
        m->Block = b;
        m->Index = i;
        KeAcquireSpinLock(&b->Lock, &irql);
        b->Refs++;
        KeReleaseSpinLock(&b->Lock, irql);
        status = CmThrSpawn(cmCwThread, m);
        if (!NT_SUCCESS(status)) {
            KeAcquireSpinLock(&b->Lock, &irql);
            b->Refs--;
            KeReleaseSpinLock(&b->Lock, irql);
            CmLogNote(nhi, "cm.callwk.no-thread", (i << 24) |
                                                      ((ULONG)status &
                                                       0xFFFFFFUL));
            continue;
        }
        m->Live = 1;
        live++;
        /* The worker's first operation: its own route's opening. */
        m->OpenId = CmCallwkSubmit(b, i, CM_CW_OPEN_KIND, cmCwOpen, NULL,
                                   NULL, 0,
                                   nhi->Host.NowMs(nhi->Host.Context));
    }
    CmLogNote(nhi, "cm.callwk.started", (b->Members << 8) | live);
    if (live == 0) {
        cmCwRelease(b);
        return NULL;
    }
    return b;
}

ULONG CmCallwkSubmit(PCM_CALLWK b, ULONG member, ULONG kind,
                     CM_CALL_RUN run, PVOID context, const VOID *op,
                     ULONG bytes, ULONG nowMs)
{
    CM_CALLWK_M *m;
    KIRQL irql;
    ULONG id;

    if (b == NULL || member >= b->Members || !b->M[member].Live) {
        return 0;
    }
    m = &b->M[member];
    id = 0;
    KeAcquireSpinLock(&b->Lock, &irql);
    if (!b->Exit) {
        id = CmCallSubmit(&m->Call, kind, run, context, op, bytes, nowMs);
        if (id != 0) {
            (VOID)KeSetEvent(&m->Wake, IO_NO_INCREMENT, FALSE);
        }
    }
    KeReleaseSpinLock(&b->Lock, irql);
    return id;
}

ULONG CmCallwkTake(PCM_CALLWK b, ULONG member, ULONG id, VOID *res,
                   ULONG bytes, PULONG got)
{
    KIRQL irql;
    ULONG r;

    if (got != NULL) {
        *got = 0;
    }
    if (b == NULL || member >= b->Members) {
        return CM_CALL_T_NONE;
    }
    KeAcquireSpinLock(&b->Lock, &irql);
    r = CmCallTake(&b->M[member].Call, id, res, bytes, got);
    KeReleaseSpinLock(&b->Lock, irql);
    return r;
}

BOOLEAN CmCallwkBusy(PCM_CALLWK b, ULONG member)
{
    KIRQL irql;
    ULONG state;

    if (b == NULL || member >= b->Members) {
        return FALSE;
    }
    KeAcquireSpinLock(&b->Lock, &irql);
    state = b->M[member].Call.State;
    KeReleaseSpinLock(&b->Lock, irql);
    return (BOOLEAN)(state == CM_CALL_SUBMITTED || state == CM_CALL_RUNNING);
}

VOID CmCallwkService(struct _CM_NHI *nhi, PCM_CALLWK b)
{
    CM_CALLWK_M *m;
    ULONG out[CM_CW_OPEN_BYTES / 4];
    ULONG got;
    ULONG r;
    ULONG i;

    if (b == NULL) {
        return;
    }
    for (i = 0; i < b->Members; i++) {
        m = &b->M[i];
        if (!m->Live || m->OpenSeen || m->OpenId == 0) {
            continue;
        }
        out[0] = 0;
        out[1] = 0;
        out[2] = 0;
        r = CmCallwkTake(b, i, m->OpenId, out, CM_CW_OPEN_BYTES, &got);
        if (r == CM_CALL_T_BUSY || r == CM_CALL_T_NONE) {
            continue;
        }
        m->OpenSeen = 1;
        CmLogNote(nhi, "cm.callwk.open",
                  (i << 24) | ((out[1] & 0xFFUL) << 16) |
                      (out[0] != 0 ? 0x00800000UL : 0) |
                      (r == CM_CALL_T_TAKEN ? (out[2] & 0xFFFFUL)
                                            : 0xFFFFUL));
    }
}

VOID CmCallwkStop(PCM_CALLWK b)
{
    KIRQL irql;
    ULONG i;

    if (b == NULL) {
        return;
    }
    KeAcquireSpinLock(&b->Lock, &irql);
    b->Exit = 1;
    for (i = 0; i < b->Members; i++) {
        (VOID)CmCallWithdraw(&b->M[i].Call, b->M[i].Call.Id);
        if (b->M[i].Live) {
            (VOID)KeSetEvent(&b->M[i].Wake, IO_NO_INCREMENT, FALSE);
        }
    }
    KeReleaseSpinLock(&b->Lock, irql);
    /* The domain's reference; a worker still inside a call keeps the
     * block until it returns. */
    cmCwRelease(b);
}
