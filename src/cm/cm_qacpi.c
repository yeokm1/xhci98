/*
 * cm_qacpi.c - the query device's firmware tables answer (cm_qryb.h,
 * cm_query.h CM_QA_*; design record 25 section 21, record 32 section
 * 12.8.14; task 46.21). Written for this project.
 *
 * The body is built once, at the start whose discovery read the tables,
 * from the copies cm_oscos.c kept (CmQaBody); each request answers the
 * header and one part of it (CmQaAnswer). Nothing here reads a table or
 * interprets one: the entries say what is held, and the reader checks it.
 *
 * IRQL: any; no lock, no DDK call.
 */

#include "cm_qryb.h"

static VOID cmQaPut(UCHAR *buf, ULONG offset, ULONG value)
{
    buf[offset] = (UCHAR)(value & 0xFFUL);
    buf[offset + 1] = (UCHAR)((value >> 8) & 0xFFUL);
    buf[offset + 2] = (UCHAR)((value >> 16) & 0xFFUL);
    buf[offset + 3] = (UCHAR)((value >> 24) & 0xFFUL);
}

static ULONG cmQaRound(ULONG n)
{
    return (n + 3UL) & ~3UL;
}

ULONG CmQaBodyBytes(const CM_QA_ITEM *items, ULONG count)
{
    ULONG bytes;
    ULONG data;
    ULONG i;

    if (count > CM_QA_MAX_ENTRIES) {
        return 0;
    }
    bytes = count * CM_QA_ENT_BYTES;
    data = 0;
    for (i = 0; i < count; i++) {
        if (items[i].Held > CM_QA_DATA_MAX ||
            (items[i].Held != 0 && items[i].Data == NULL)) {
            return 0;
        }
        data += items[i].Held;
        if (data > CM_QA_DATA_MAX) {
            return 0;
        }
        bytes += cmQaRound(items[i].Held);
    }
    return bytes;
}

ULONG CmQaBody(UCHAR *out, ULONG room, const CM_QA_ITEM *items, ULONG count)
{
    ULONG bytes;
    ULONG at;
    ULONG i;
    ULONG k;
    UCHAR *e;

    bytes = CmQaBodyBytes(items, count);
    if (bytes == 0 || room < bytes) {
        return 0;
    }
    for (i = 0; i < bytes; i++) {
        out[i] = 0;
    }
    at = count * CM_QA_ENT_BYTES;
    for (i = 0; i < count; i++) {
        e = out + i * CM_QA_ENT_BYTES;
        cmQaPut(e, CM_QA_E_SIG, items[i].Sig);
        cmQaPut(e, CM_QA_E_KIND, items[i].Kind);
        cmQaPut(e, CM_QA_E_LENGTH, items[i].Length);
        cmQaPut(e, CM_QA_E_OFFSET, items[i].Held != 0 ? at : 0);
        cmQaPut(e, CM_QA_E_HELD, items[i].Held);
        for (k = 0; k < items[i].Held; k++) {
            out[at + k] = items[i].Data[k];
        }
        at += cmQaRound(items[i].Held);
    }
    return bytes;
}

ULONG CmQaAnswer(UCHAR *out, ULONG room, const CM_QA_IN *in, ULONG from,
                 PULONG info)
{
    ULONG part;
    ULONG flags;
    ULONG i;

    *info = 0;
    if (out == NULL || room < CM_QA_HDR_BYTES) {
        return CM_Q_ANS_TOO_SMALL;
    }
    for (i = 0; i < CM_QA_HDR_BYTES; i++) {
        out[i] = 0;
    }
    flags = in->Flags;
    if (in->Stopping) {
        flags |= CM_QA_F_STOPPING;
    }
    part = 0;
    if (from > in->BodyBytes) {
        flags |= CM_QA_F_PAST;
    } else {
        part = in->BodyBytes - from;
        if (part > room - CM_QA_HDR_BYTES) {
            part = room - CM_QA_HDR_BYTES;
        }
        if (part > CM_QA_PART_MAX) {
            part = CM_QA_PART_MAX;
        }
        for (i = 0; i < part; i++) {
            out[CM_QA_HDR_BYTES + i] = in->Body[from + i];
        }
    }
    cmQaPut(out, CM_QA_O_MAGIC, CM_QA_MAGIC);
    cmQaPut(out, CM_QA_O_VERSION, CM_QA_VERSION);
    cmQaPut(out, CM_QA_O_BYTES, CM_QA_HDR_BYTES + part);
    cmQaPut(out, CM_QA_O_FLAGS, flags);
    cmQaPut(out, CM_QA_O_EPOCH_LO, in->EpochLo);
    cmQaPut(out, CM_QA_O_EPOCH_HI, in->EpochHi);
    cmQaPut(out, CM_QA_O_STATE, in->State);
    cmQaPut(out, CM_QA_O_OUTCOME, in->Outcome);
    cmQaPut(out, CM_QA_O_TIER, in->Tier);
    cmQaPut(out, CM_QA_O_BODY_BYTES, in->BodyBytes);
    cmQaPut(out, CM_QA_O_ENT_SHAPE,
            (in->Count & 0xFFFFUL) | (CM_QA_ENT_BYTES << 16));
    cmQaPut(out, CM_QA_O_PART_AT, (flags & CM_QA_F_PAST) ? 0 : from);
    cmQaPut(out, CM_QA_O_PART_BYTES, part);
    *info = CM_QA_HDR_BYTES + part;
    return CM_Q_ANS_WHOLE;
}
