/*
 * cm_call.c - the pure half of the per-member call workers (cm_call.h;
 * roadmap-usb4.md task 42-A.12): interface 3's R6f handoff word over one
 * CM_CALL slot. The driver (cm_callwk.c) calls each function under the
 * slot's block lock; test\test_cm_call.c runs them on the host.
 *
 * DDK-free: part of the pure core. C89, no 64-bit arithmetic. IRQL: any.
 */

#include "../xhci_compat.h"
#include "cm_call.h"

static VOID cmCallCopy(PUCHAR to, const UCHAR *from, ULONG bytes)
{
    ULONG k;

    for (k = 0; k < bytes; k++) {
        to[k] = from[k];
    }
}

static VOID cmCallZero(PUCHAR to, ULONG bytes)
{
    ULONG k;

    for (k = 0; k < bytes; k++) {
        to[k] = 0;
    }
}

VOID CmCallInit(PCM_CALL c)
{
    cmCallZero((PUCHAR)c, (ULONG)sizeof(*c));
    c->State = CM_CALL_IDLE;
}

ULONG CmCallSubmit(PCM_CALL c, ULONG kind, CM_CALL_RUN run, PVOID context,
                   const VOID *op, ULONG bytes, ULONG nowMs)
{
    if (c->State != CM_CALL_IDLE || run == NULL ||
        bytes > CM_CALL_OP_BYTES || (bytes != 0 && op == NULL)) {
        return 0;
    }
    c->NextId = c->NextId + 1;
    if (c->NextId == 0) {
        c->NextId = 1;
    }
    cmCallZero((PUCHAR)c->Op, CM_CALL_OP_BYTES);
    if (bytes != 0) {
        cmCallCopy((PUCHAR)c->Op, (const UCHAR *)op, bytes);
    }
    c->OpBytes = bytes;
    c->Run = run;
    c->Late = NULL;
    c->Withdrawn = 0;
    c->Context = context;
    c->Kind = kind;
    c->SubmittedAt = nowMs;
    c->Id = c->NextId;
    c->Ran = 0;
    c->ResBytes = 0;
    /* The word last: a worker that sees SUBMITTED sees the rest. */
    *(volatile ULONG *)&c->State = CM_CALL_SUBMITTED;
    return c->Id;
}

VOID CmCallSetLate(PCM_CALL c, CM_CALL_LATE late)
{
    c->Late = late;
}

BOOLEAN CmCallStopSlot(PCM_CALL c)
{
    if (c->State != CM_CALL_SUBMITTED || (c->Kind & CM_CALL_K_DETACH) != 0) {
        return FALSE;
    }
    cmCallZero((PUCHAR)c->Res, CM_CALL_RES_BYTES);
    c->ResBytes = 0;
    c->Withdrawn = 1;
    c->Ran = c->Id;
    *(volatile ULONG *)&c->State = CM_CALL_DONE;
    return TRUE;
}

BOOLEAN CmCallLateDue(const CM_CALL *c, PULONG outcome)
{
    if (c->State != CM_CALL_DONE) {
        return FALSE;
    }
    *outcome = c->Withdrawn ? CM_CALL_L_WITHDRAWN : CM_CALL_L_RESULT;
    return TRUE;
}

BOOLEAN CmCallRunsAfterStop(const CM_CALL *c)
{
    return (BOOLEAN)(c->State == CM_CALL_SUBMITTED &&
                     (c->Kind & CM_CALL_K_DETACH) != 0);
}

BOOLEAN CmCallClaim(PCM_CALL c)
{
    if (c->State != CM_CALL_SUBMITTED) {
        return FALSE;
    }
    cmCallZero((PUCHAR)c->Res, CM_CALL_RES_BYTES);
    c->ResBytes = 0;
    *(volatile ULONG *)&c->State = CM_CALL_RUNNING;
    return TRUE;
}

VOID CmCallFinish(PCM_CALL c, ULONG resBytes)
{
    if (c->State != CM_CALL_RUNNING) {
        return;
    }
    c->ResBytes = resBytes > CM_CALL_RES_BYTES ? CM_CALL_RES_BYTES
                                               : resBytes;
    c->Ran = c->Id;
    *(volatile ULONG *)&c->State = CM_CALL_DONE;
}

ULONG CmCallTake(PCM_CALL c, ULONG id, VOID *res, ULONG bytes, PULONG got)
{
    ULONG n;

    if (got != NULL) {
        *got = 0;
    }
    if (c->State == CM_CALL_IDLE) {
        return CM_CALL_T_NONE;
    }
    if (c->State != CM_CALL_DONE) {
        return CM_CALL_T_BUSY;
    }
    if (id == 0 || c->Ran != id) {
        /* A result the thread did not ask for (a stale id): dropped. */
        *(volatile ULONG *)&c->State = CM_CALL_IDLE;
        return CM_CALL_T_STALE;
    }
    n = c->ResBytes < bytes ? c->ResBytes : bytes;
    if (res != NULL && n != 0) {
        cmCallCopy((PUCHAR)res, (const UCHAR *)c->Res, n);
    }
    if (got != NULL) {
        *got = n;
    }
    *(volatile ULONG *)&c->State = CM_CALL_IDLE;
    return CM_CALL_T_TAKEN;
}

BOOLEAN CmCallWithdraw(PCM_CALL c, ULONG id)
{
    if (c->State != CM_CALL_SUBMITTED || c->Id != id || id == 0) {
        return FALSE;
    }
    *(volatile ULONG *)&c->State = CM_CALL_IDLE;
    return TRUE;
}

ULONG CmCallAge(const CM_CALL *c, ULONG nowMs)
{
    if (c->State == CM_CALL_IDLE) {
        return 0;
    }
    return nowMs - c->SubmittedAt;
}
