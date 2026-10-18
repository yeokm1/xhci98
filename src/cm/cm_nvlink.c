/*
 * cm_nvlink.c - the pure half of the stor98 link-state channel
 * (cm_nvlink.h; roadmap-usb4.md tasks 39-A.7 and 40-A.7): the entry
 * protocol of stor98cm.h over a STOR98LS_TABLE, the two profiles'
 * statement conditions, interface 3's locator encoding and proof query
 * handler, and its REMOVE_PENDING request. test\test_cm_nvlink.c runs it on the host.
 *
 * WRITE ORDER. stor98 reads an entry concurrently, from its own timer and
 * HwStartIo, as a seqlock: Sequence odd while the connection manager
 * writes, and a read whose Sequence was odd or changed is retried. Every
 * write here is therefore Sequence + 1 (odd), the fields, Sequence + 1
 * (even), each through a volatile access so the compiler keeps that
 * order; on x86 and x64 stores are not reordered with other stores, so
 * the processor keeps it too. BusNumber and SlotNumber are not volatile in
 * the header and are written through volatile pointers here for the same
 * reason. AckGeneration is stor98's and is only read.
 *
 * DDK-free: part of the pure core. C89, no 64-bit arithmetic. IRQL: any.
 */

#include "../xhci_compat.h"
#include "tbt_topo.h"
#include "cm_nvlink.h"

static VOID cmNvBegin(PSTOR98LS_ENTRY e)
{
    e->Sequence = e->Sequence + 1;
    if ((e->Sequence & 1UL) == 0) {
        /* An entry left odd (never, unless memory was overwritten) is
         * brought back to odd here rather than to even. */
        e->Sequence = e->Sequence + 1;
    }
}

static VOID cmNvEnd(PSTOR98LS_ENTRY e)
{
    e->Sequence = e->Sequence + 1;
}

static VOID cmNvPut(volatile ULONG *field, ULONG value)
{
    *field = value;
}

VOID CmNvlinkTableInit(PSTOR98LS_TABLE t)
{
    ULONG i;
    ULONG k;
    PUCHAR p;

    p = (PUCHAR)t;
    for (k = 0; k < sizeof(STOR98LS_TABLE); k++) {
        p[k] = 0;
    }
    t->Version = STOR98LS_VERSION;
    t->Count = STOR98LS_ENTRIES;
    for (i = 0; i < STOR98LS_ENTRIES; i++) {
        t->Entry[i].Sequence = 0;
    }
    /* The signature last: a reader that sees it sees the rest. */
    *(volatile ULONG *)&t->Signature = STOR98LS_SIGNATURE;
}

ULONG CmNvlinkFind(const STOR98LS_TABLE *t, ULONG bus, ULONG slot)
{
    ULONG i;

    if (t == NULL) {
        return CM_NVLINK_NONE;
    }
    for (i = 0; i < STOR98LS_ENTRIES; i++) {
        if ((t->Entry[i].Flags & STOR98LS_IN_USE) != 0 &&
            t->Entry[i].BusNumber == bus &&
            t->Entry[i].SlotNumber == slot) {
            return i;
        }
    }
    return CM_NVLINK_NONE;
}

/* An entry that may be given to a function: one never used. An entry once
 * given to (bus, slot) keeps it for the life of the table: an instance of
 * stor98 may hold its address and apply any newer generation's
 * DOWN_QUIESCED written there, whatever function it then named, and
 * nothing tells this side when that instance is gone. */
static BOOLEAN cmNvFree(const STOR98LS_ENTRY *e)
{
    return (BOOLEAN)((e->Flags & STOR98LS_IN_USE) == 0 && e->Sequence == 0);
}

BOOLEAN CmNvlinkRetireEntry(PSTOR98LS_TABLE t, ULONG index)
{
    PSTOR98LS_ENTRY e;

    if (t == NULL || index >= STOR98LS_ENTRIES) {
        return FALSE;
    }
    e = &t->Entry[index];
    if ((e->Flags & STOR98LS_IN_USE) == 0 || (e->Flags & STOR98LS_UP) != 0 ||
        e->Flags == STOR98LS_IN_USE) {
        /* Unused, UP, or already retired (nothing a reader could apply). */
        return FALSE;
    }
    cmNvBegin(e);
    e->LinkGeneration = e->LinkGeneration + 1;
    e->Flags = STOR98LS_IN_USE;
    cmNvEnd(e);
    return TRUE;
}

ULONG CmNvlinkRetire(PSTOR98LS_TABLE t, ULONG bus)
{
    ULONG i;
    ULONG n;

    n = 0;
    if (t == NULL) {
        return 0;
    }
    for (i = 0; i < STOR98LS_ENTRIES; i++) {
        if ((t->Entry[i].Flags & STOR98LS_IN_USE) != 0 &&
            t->Entry[i].BusNumber == bus && CmNvlinkRetireEntry(t, i)) {
            n++;
        }
    }
    return n;
}

ULONG CmNvlinkPublishUp(PSTOR98LS_TABLE t, ULONG bus, ULONG slot,
                        BOOLEAN fresh, PULONG outcome)
{
    ULONG i;
    ULONG pick;
    PSTOR98LS_ENTRY e;

    *outcome = CM_NVLINK_UP_BAD;
    if (t == NULL || bus > 0xFFUL || slot > 0xFFUL) {
        return CM_NVLINK_NONE;
    }
    i = CmNvlinkFind(t, bus, slot);
    if (i != CM_NVLINK_NONE) {
        e = &t->Entry[i];
        /* R9a: LinkGeneration is not changed, nor the request cleared,
         * while a removal request is outstanding; a fresh exposure then
         * waits for CmNvlinkWkWithdraw like any other. */
        if ((e->Flags & STOR98LS_UP) != 0 &&
            (!fresh || (e->Flags & CM_NVLINK_REMOVE_PENDING) != 0)) {
            *outcome = CM_NVLINK_UP_ALREADY;
            return i;
        }
        cmNvBegin(e);
        e->LinkGeneration = e->LinkGeneration + 1;
        e->Flags = STOR98LS_IN_USE | STOR98LS_UP;
        cmNvEnd(e);
        *outcome = CM_NVLINK_UP_AGAIN;
        return i;
    }
    pick = CM_NVLINK_NONE;
    for (i = 0; i < STOR98LS_ENTRIES; i++) {
        if (cmNvFree(&t->Entry[i])) {
            pick = i;
            break;
        }
    }
    if (pick == CM_NVLINK_NONE) {
        *outcome = CM_NVLINK_UP_FULL;
        return CM_NVLINK_NONE;
    }
    e = &t->Entry[pick];
    cmNvBegin(e);
    cmNvPut(&e->BusNumber, bus);
    cmNvPut(&e->SlotNumber, slot);
    cmNvPut(&e->RemoveSequence, 0);
    e->LinkGeneration = e->LinkGeneration + 1;
    e->Flags = STOR98LS_IN_USE | STOR98LS_UP;
    cmNvEnd(e);
    *outcome = CM_NVLINK_UP_NEW;
    return pick;
}

BOOLEAN CmNvlinkSetDown(PSTOR98LS_TABLE t, ULONG index, BOOLEAN quiesced)
{
    PSTOR98LS_ENTRY e;

    if (t == NULL || index >= STOR98LS_ENTRIES) {
        return FALSE;
    }
    e = &t->Entry[index];
    if ((e->Flags & STOR98LS_UP) == 0) {
        return FALSE;
    }
    cmNvBegin(e);
    e->Flags = quiesced ? (STOR98LS_IN_USE | STOR98LS_DOWN_QUIESCED)
                        : STOR98LS_IN_USE;
    cmNvEnd(e);
    return TRUE;
}

ULONG CmNvlinkNextUp(const STOR98LS_TABLE *t, ULONG bus, ULONG from)
{
    ULONG i;

    if (t == NULL) {
        return CM_NVLINK_NONE;
    }
    for (i = from; i < STOR98LS_ENTRIES; i++) {
        if ((t->Entry[i].Flags & (STOR98LS_IN_USE | STOR98LS_UP)) ==
                (STOR98LS_IN_USE | STOR98LS_UP) &&
            t->Entry[i].BusNumber == bus) {
            return i;
        }
    }
    return CM_NVLINK_NONE;
}

BOOLEAN CmNvlinkAcked(const STOR98LS_TABLE *t, ULONG index)
{
    const STOR98LS_ENTRY *e;
    ULONG ack;

    if (t == NULL || index >= STOR98LS_ENTRIES) {
        return FALSE;
    }
    e = &t->Entry[index];
    ack = e->AckGeneration;
    /* R6d: read once more, unchanged, before it is acted on. */
    return (BOOLEAN)((e->Flags & STOR98LS_DOWN_QUIESCED) != 0 &&
                     ack == e->LinkGeneration && e->AckGeneration == ack);
}

ULONG CmNvlinkIcmVerdict(ULONG members, ULONG source, BOOLEAN emulator,
                         BOOLEAN osQualified, ULONG pollState,
                         ULONG lastVerdict)
{
    if (members == 0) {
        return CM_NVLINK_NO_SET;
    }
    if (members > 1) {
        /* The departed device's port is not known under the ICM (record
         * 21 section 6.3): no member's reading speaks for it alone. */
        return CM_NVLINK_SEVERAL;
    }
    if (source != XHCI_TBT_SRC_SHPC || !emulator) {
        return CM_NVLINK_HARDWARE;
    }
    if (!osQualified) {
        return CM_NVLINK_UNQUALIFIED;
    }
    /* REACHED on DOWN only: GONE is the bridge itself reading all-ones, a
     * presence signal, not the stage-3 presence change. */
    if (pollState != XHCI_TBT_POLL_REACHED ||
        lastVerdict != XHCI_TBT_PORT_DOWN) {
        return CM_NVLINK_NOT_DOWN;
    }
    return CM_NVLINK_SEND;
}

ULONG CmNvlinkU4Verdict(BOOLEAN pathClosed, BOOLEAN memberKnown,
                        ULONG source, BOOLEAN emulator, BOOLEAN osQualified,
                        ULONG reading, ULONG remainingMs)
{
    if (!pathClosed) {
        return CM_NVLINK_PATH_OPEN;
    }
    if (!memberKnown) {
        return CM_NVLINK_SEVERAL;
    }
    if (source != XHCI_TBT_SRC_SHPC || !emulator) {
        return CM_NVLINK_HARDWARE;
    }
    if (!osQualified) {
        return CM_NVLINK_UNQUALIFIED;
    }
    if (reading != XHCI_TBT_PORT_DOWN) {
        return CM_NVLINK_NOT_DOWN;
    }
    if (remainingMs < CM_NVLINK_DELIVERY_MS) {
        return CM_NVLINK_LATE;
    }
    return CM_NVLINK_SEND;
}

ULONG CmNvlinkAckBoundMs(BOOLEAN nt)
{
    return nt ? CM_NVLINK_ACK_MS : CM_NVLINK_ACK_9X_MS;
}

ULONG CmNvlinkLeft(ULONG startMs, ULONG nowMs, ULONG budgetMs)
{
    ULONG spent;

    spent = nowMs - startMs;
    return spent >= budgetMs ? 0 : budgetMs - spent;
}

static VOID cmNvLe(PUCHAR out, ULONG v)
{
    out[0] = (UCHAR)(v & 0xFFUL);
    out[1] = (UCHAR)((v >> 8) & 0xFFUL);
    out[2] = (UCHAR)((v >> 16) & 0xFFUL);
    out[3] = (UCHAR)((v >> 24) & 0xFFUL);
}

VOID CmNvlinkRecordEncode(PUCHAR out, ULONG address, ULONG bytes,
                          ULONG cookie)
{
    cmNvLe(out, STOR98LS_SIGNATURE);
    cmNvLe(out + 4, CM_NVLINK_RECORD_VER);
    cmNvLe(out + 8, address);
    cmNvLe(out + 12, bytes);
    cmNvLe(out + 16, cookie);
}

ULONG CmNvlinkProofQuery(CM_NV_VALCTX *vc, ULONG n, PUCHAR data, PULONG cb,
                         const ULONG *cookie,
                         const STOR98LS9X_PROOF_TABLE *proof)
{
    ULONG i;
    ULONG k;
    ULONG need;
    const UCHAR *from;

    need = 0;
    for (i = 0; i < n; i++) {
        if (vc[i].value_context == (PVOID)cookie) {
            vc[i].valuelen = (int)sizeof(ULONG);
        } else if (vc[i].value_context == (PVOID)proof) {
            vc[i].valuelen = (int)CM_NVLINK_PROOF_BYTES;
        } else {
            vc[i].valuelen = 0;
        }
        need += (ULONG)vc[i].valuelen;
    }
    if (data == NULL) {
        *cb = need;
        return 0;
    }
    if (*cb < need) {
        *cb = need;
        return CM_NV_ERROR_MORE_DATA;
    }
    for (i = 0; i < n; i++) {
        from = (const UCHAR *)vc[i].value_context;
        for (k = 0; k < (ULONG)vc[i].valuelen; k++) {
            data[k] = from[k];
        }
        vc[i].val_buff_ptr = data;
        data += vc[i].valuelen;
    }
    *cb = need;
    return 0;
}

BOOLEAN CmNvlinkRemoveRequest(PSTOR98LS_TABLE t, ULONG index,
                              ULONG sequence)
{
    PSTOR98LS_ENTRY e;

    if (t == NULL || index >= STOR98LS_ENTRIES || sequence == 0 ||
        sequence > 0x00FFFFFFUL) {
        return FALSE;
    }
    e = &t->Entry[index];
    if ((e->Flags & STOR98LS_UP) == 0) {
        return FALSE;
    }
    cmNvBegin(e);
    cmNvPut(&e->RemoveSequence, sequence);
    e->Flags = STOR98LS_IN_USE | STOR98LS_UP | CM_NVLINK_REMOVE_PENDING;
    cmNvEnd(e);
    return TRUE;
}

ULONG CmNvlinkRemoveAnswer(const STOR98LS_TABLE *t, ULONG index,
                           ULONG sequence, ULONG gen)
{
    ULONG answer;
    ULONG outcome;

    if (t == NULL || index >= STOR98LS_ENTRIES) {
        return CM_NVLINK_RM_NONE;
    }
    /* R9c: the request's own sequence, on the request's generation; R6d:
     * read once more, unchanged, before it is acted on. */
    answer = t->Entry[index].RemoveAck;
    if (((answer >> 8) & (ULONG)STOR98LS9X_SEQ_MASK) != sequence ||
        t->Entry[index].LinkGeneration != gen ||
        t->Entry[index].RemoveAck != answer) {
        return CM_NVLINK_RM_NONE;
    }
    outcome = answer & 0xFFUL;
    if (outcome == CM_NVLINK_RM_CLEAN || outcome == CM_NVLINK_RM_ISOLATED ||
        outcome == CM_NVLINK_RM_FAILED) {
        return outcome;
    }
    /* An outcome this side does not know is not clean. */
    return CM_NVLINK_RM_FAILED;
}

VOID CmNvlinkRemoveCancel(PSTOR98LS_TABLE t, ULONG index)
{
    PSTOR98LS_ENTRY e;

    if (t == NULL || index >= STOR98LS_ENTRIES) {
        return;
    }
    e = &t->Entry[index];
    if ((e->Flags & CM_NVLINK_REMOVE_PENDING) == 0) {
        return;
    }
    cmNvBegin(e);
    e->Flags = e->Flags & ~CM_NVLINK_REMOVE_PENDING;
    cmNvEnd(e);
}

ULONG CmNvlinkRemoveStep(const STOR98LS_TABLE *t, ULONG index,
                         ULONG sequence, ULONG gen, ULONG elapsedMs)
{
    if (elapsedMs >= CM_NVLINK_REMOVE_MS) {
        /* Read only to log it: late, never taken. */
        return CmNvlinkRemoveAnswer(t, index, sequence, gen) !=
                       CM_NVLINK_RM_NONE
                   ? CM_NVLINK_RM_LATE
                   : CM_NVLINK_RM_TIMEOUT;
    }
    return CmNvlinkRemoveAnswer(t, index, sequence, gen);
}

ULONG CmNvlinkAckStep(const STOR98LS_TABLE *t, ULONG index,
                      BOOLEAN receiver, ULONG markedAt, ULONG nowMs,
                      ULONG boundMs)
{
    if (t == NULL || index >= STOR98LS_ENTRIES ||
        (t->Entry[index].Flags & STOR98LS_DOWN_QUIESCED) == 0) {
        return CM_NVLINK_AW_SUPERSEDED;
    }
    if (!receiver) {
        return CM_NVLINK_AW_NO_RECEIVER;
    }
    if (CmNvlinkLeft(markedAt, nowMs, boundMs) == 0) {
        return CmNvlinkAcked(t, index) ? CM_NVLINK_AW_LATE
                                       : CM_NVLINK_AW_TIMEOUT;
    }
    return CmNvlinkAcked(t, index) ? CM_NVLINK_AW_ACKED
                                   : CM_NVLINK_AW_PENDING;
}

ULONG CmNvlinkPublish9x(const CM_NV_PUB9X *ops, PULONG error)
{
    ULONG e;

    *error = 0;
    /* R4: resident before anything is published. */
    if (!ops->Resident(ops->Context)) {
        return CM_NVLINK_PUB_RESIDENCY;
    }
    /* R3a: open first; an existing key is another publisher's, and only
     * "not found" shows it absent. */
    e = ops->OpenProof(ops->Context);
    if (e == 0) {
        return CM_NVLINK_PUB_PUBLISHER;
    }
    if (e != CM_NV_ERROR_FILE_NOT_FOUND) {
        *error = e;
        return CM_NVLINK_PUB_OPEN;
    }
    e = ops->CreateProof(ops->Context);
    if (e != 0) {
        *error = e;
        return CM_NVLINK_PUB_PROOF;
    }
    /* R3b: the locator only after the proof. */
    e = ops->WriteLocator(ops->Context);
    if (e != 0) {
        *error = e;
        return CM_NVLINK_PUB_LOCATOR;
    }
    return CM_NVLINK_PUB_OK;
}

BOOLEAN CmNvlinkRetryAgain(ULONG elapsedMs)
{
    return (BOOLEAN)(elapsedMs < CM_NVLINK_RETRY_FOR_MS);
}
