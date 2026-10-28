/*
 * cm_irq.c - the pure half of the interrupt a child filter gives a
 * function hot-added behind a tunnel on Windows 2000's Standard PC HAL
 * (design record 21 section 6.2.1; task 44.53). See cm_irq.h; the driver
 * half is in cm_filt.c and cm_reenum.c.
 *
 * C89, DDK-free, no allocation, no lock. IRQL: any.
 */

#include "cm_irq.h"

static ULONG cmIrqGet32(const UCHAR *p)
{
    return (ULONG)p[0] | ((ULONG)p[1] << 8) | ((ULONG)p[2] << 16) |
           ((ULONG)p[3] << 24);
}

static VOID cmIrqPut32(UCHAR *p, ULONG v)
{
    p[0] = (UCHAR)(v & 0xFFUL);
    p[1] = (UCHAR)((v >> 8) & 0xFFUL);
    p[2] = (UCHAR)((v >> 16) & 0xFFUL);
    p[3] = (UCHAR)((v >> 24) & 0xFFUL);
}

static VOID cmIrqCopy(UCHAR *to, const UCHAR *from, ULONG bytes)
{
    ULONG i;

    for (i = 0; i < bytes; i++) {
        to[i] = from[i];
    }
}

BOOLEAN CmIrqHalStdPc(const USHORT *ids, ULONG chars)
{
    static const char want[] = "e_isa_up";
    ULONG i;
    USHORT c;

    if (ids == NULL) {
        return FALSE;
    }
    for (i = 0; i < sizeof(want); i++) {
        if (i >= chars) {
            return FALSE;
        }
        c = ids[i];
        if (c >= 'A' && c <= 'Z') {
            c = (USHORT)(c - 'A' + 'a');
        }
        if (c != (USHORT)(UCHAR)want[i]) {
            return FALSE;
        }
    }
    return TRUE;
}

ULONG CmIrqWire(ULONG pin, const ULONG *devs, ULONG n, ULONG portPin,
                ULONG portLine, PULONG why)
{
    ULONG sum;
    ULONG i;

    if (pin < 1UL || pin > 4UL) {
        *why = CM_IRQ_WHY_NO_PIN;
        return 0;
    }
    if (devs == NULL || n == 0 || n > CM_IRQ_PATH_MAX) {
        *why = CM_IRQ_WHY_PATH;
        return 0;
    }
    sum = 0;
    for (i = 0; i < n; i++) {
        if (devs[i] > 31UL) {
            *why = CM_IRQ_WHY_PATH;
            return 0;
        }
        sum += devs[i];
    }
    if (portPin < 1UL || portPin > 4UL) {
        *why = CM_IRQ_WHY_PORT_PIN;
        return 0;
    }
    if (portLine < 3UL || portLine > 15UL || portLine == 8UL ||
        portLine == 13UL) {
        *why = CM_IRQ_WHY_PORT_LINE;
        return 0;
    }
    if (((pin - 1UL + sum) & 3UL) != portPin - 1UL) {
        *why = CM_IRQ_WHY_OTHER_PIN;
        return 0;
    }
    *why = CM_IRQ_WHY_GIVEN;
    return portLine;
}

ULONG CmIrqReqScan(const UCHAR *list, PULONG newBytes)
{
    ULONG size;
    ULONG alts;
    ULONG at;
    ULONG a;
    ULONG count;
    ULONG d;
    ULONG has;

    *newBytes = 0;
    if (list == NULL) {
        return CM_IRQ_REQ_BAD;
    }
    size = cmIrqGet32(list);
    if (size < CM_IRQ_REQ_HDR || size > CM_IRQ_REQ_MAX_BYTES) {
        return CM_IRQ_REQ_BAD;
    }
    alts = cmIrqGet32(list + CM_IRQ_REQ_ALTS_AT);
    if (alts == 0 ||
        alts > (CM_IRQ_REQ_MAX_BYTES - CM_IRQ_REQ_HDR) / CM_IRQ_IOL_HDR) {
        return CM_IRQ_REQ_BAD;
    }
    at = CM_IRQ_REQ_HDR;
    has = 0;
    for (a = 0; a < alts; a++) {
        if (size - at < CM_IRQ_IOL_HDR) {
            return CM_IRQ_REQ_BAD;
        }
        count = cmIrqGet32(list + at + CM_IRQ_IOL_COUNT_AT);
        at += CM_IRQ_IOL_HDR;
        if (count > (size - at) / CM_IRQ_IOD_SIZE) {
            return CM_IRQ_REQ_BAD;
        }
        for (d = 0; d < count; d++) {
            if ((ULONG)list[at + CM_IRQ_IOD_TYPE_AT] == CM_IRQ_TYPE_INTERRUPT) {
                has = 1;
            }
            at += CM_IRQ_IOD_SIZE;
        }
    }
    if (at != size) {
        return CM_IRQ_REQ_BAD;
    }
    if (has) {
        return CM_IRQ_REQ_HAS;
    }
    *newBytes = size + alts * CM_IRQ_IOD_SIZE;
    return CM_IRQ_REQ_NONE;
}

ULONG CmIrqReqAdd(const UCHAR *list, UCHAR *out, ULONG outBytes, ULONG line)
{
    ULONG want;
    ULONG alts;
    ULONG from;
    ULONG to;
    ULONG a;
    ULONG count;
    ULONG body;
    ULONG i;

    if (out == NULL || CmIrqReqScan(list, &want) != CM_IRQ_REQ_NONE ||
        outBytes != want) {
        return 0;
    }
    alts = cmIrqGet32(list + CM_IRQ_REQ_ALTS_AT);
    cmIrqCopy(out, list, CM_IRQ_REQ_HDR);
    cmIrqPut32(out, want);
    from = CM_IRQ_REQ_HDR;
    to = CM_IRQ_REQ_HDR;
    for (a = 0; a < alts; a++) {
        count = cmIrqGet32(list + from + CM_IRQ_IOL_COUNT_AT);
        body = CM_IRQ_IOL_HDR + count * CM_IRQ_IOD_SIZE;
        cmIrqCopy(out + to, list + from, body);
        cmIrqPut32(out + to + CM_IRQ_IOL_COUNT_AT, count + 1UL);
        from += body;
        to += body;
        for (i = 0; i < CM_IRQ_IOD_SIZE; i++) {
            out[to + i] = 0;
        }
        out[to + CM_IRQ_IOD_TYPE_AT] = (UCHAR)CM_IRQ_TYPE_INTERRUPT;
        out[to + CM_IRQ_IOD_SHARE_AT] = (UCHAR)CM_IRQ_SHARE_SHARED;
        out[to + CM_IRQ_IOD_FLAGS_AT] = (UCHAR)(CM_IRQ_FLAGS_LEVEL & 0xFFUL);
        out[to + CM_IRQ_IOD_FLAGS_AT + 1] =
            (UCHAR)((CM_IRQ_FLAGS_LEVEL >> 8) & 0xFFUL);
        cmIrqPut32(out + to + CM_IRQ_IOD_MIN_AT, line);
        cmIrqPut32(out + to + CM_IRQ_IOD_MAX_AT, line);
        to += CM_IRQ_IOD_SIZE;
    }
    return to;
}

/*
 * One CM_RESOURCE_LIST walked: its size, its shape (`fulls`, and each full
 * descriptor's partial count in `parts`), and the interrupt descriptors'
 * offsets and levels (at most two kept: a third changes nothing the caller
 * decides). FALSE for a list over the bounds or with a device-specific
 * descriptor.
 */
typedef struct _CM_IRQ_WALK {
    ULONG Bytes;
    ULONG Fulls;
    ULONG Parts[CM_IRQ_CML_MAX_FULL];
    ULONG Irqs;
    ULONG IrqAt[2];
    ULONG IrqCountAt[2];
    ULONG IrqLevel[2];
} CM_IRQ_WALK;

static BOOLEAN cmIrqWalk(const UCHAR *list, CM_IRQ_WALK *w)
{
    ULONG f;
    ULONG p;
    ULONG at;
    ULONG countAt;
    ULONG parts;

    w->Fulls = cmIrqGet32(list);
    w->Irqs = 0;
    if (w->Fulls == 0 || w->Fulls > CM_IRQ_CML_MAX_FULL) {
        return FALSE;
    }
    at = CM_IRQ_CML_HDR;
    for (f = 0; f < w->Fulls; f++) {
        countAt = at + CM_IRQ_CMF_COUNT_AT;
        parts = cmIrqGet32(list + countAt);
        if (parts > CM_IRQ_CMF_MAX_PARTIAL) {
            return FALSE;
        }
        w->Parts[f] = parts;
        at += CM_IRQ_CMF_HDR;
        for (p = 0; p < parts; p++) {
            if ((ULONG)list[at] == CM_IRQ_TYPE_DEVSPECIFIC) {
                return FALSE;
            }
            if ((ULONG)list[at] == CM_IRQ_TYPE_INTERRUPT) {
                if (w->Irqs < 2UL) {
                    w->IrqAt[w->Irqs] = at;
                    w->IrqCountAt[w->Irqs] = countAt;
                    w->IrqLevel[w->Irqs] =
                        cmIrqGet32(list + at + CM_IRQ_CMD_LEVEL_AT);
                }
                w->Irqs++;
            }
            at += CM_IRQ_CMD_SIZE;
        }
    }
    w->Bytes = at;
    return TRUE;
}

ULONG CmIrqStartScan(const UCHAR *raw, const UCHAR *tr, ULONG level,
                     PCM_IRQ_STRIP s)
{
    CM_IRQ_WALK r;
    CM_IRQ_WALK t;
    ULONG f;

    s->Bytes = 0;
    s->At = 0;
    s->CountAt = 0;
    if (raw == NULL) {
        return CM_IRQ_START_NONE;
    }
    if (!cmIrqWalk(raw, &r)) {
        return CM_IRQ_START_REFUSE;
    }
    if (r.Irqs == 0) {
        return CM_IRQ_START_NONE;
    }
    if (r.Irqs != 1UL || level == 0 || r.IrqLevel[0] != level ||
        tr == NULL || !cmIrqWalk(tr, &t) || t.Fulls != r.Fulls) {
        return CM_IRQ_START_REFUSE;
    }
    for (f = 0; f < r.Fulls; f++) {
        if (t.Parts[f] != r.Parts[f]) {
            return CM_IRQ_START_REFUSE;
        }
    }
    if (t.Bytes != r.Bytes || (ULONG)tr[r.IrqAt[0]] != CM_IRQ_TYPE_INTERRUPT) {
        return CM_IRQ_START_REFUSE;
    }
    s->Bytes = r.Bytes;
    s->At = r.IrqAt[0];
    s->CountAt = r.IrqCountAt[0];
    return CM_IRQ_START_STRIP;
}

ULONG CmIrqStartStrip(const UCHAR *in, UCHAR *out, ULONG outBytes,
                      const CM_IRQ_STRIP *s)
{
    ULONG tail;

    if (in == NULL || out == NULL || s->Bytes < CM_IRQ_CML_HDR +
        CM_IRQ_CMF_HDR + CM_IRQ_CMD_SIZE ||
        outBytes != s->Bytes - CM_IRQ_CMD_SIZE ||
        s->At < CM_IRQ_CML_HDR + CM_IRQ_CMF_HDR ||
        s->At > s->Bytes - CM_IRQ_CMD_SIZE ||
        s->CountAt + 4UL > s->At) {
        return 0;
    }
    if (cmIrqGet32(in + s->CountAt) == 0) {
        return 0;
    }
    tail = s->Bytes - s->At - CM_IRQ_CMD_SIZE;
    cmIrqCopy(out, in, s->At);
    cmIrqCopy(out + s->At, in + s->At + CM_IRQ_CMD_SIZE, tail);
    cmIrqPut32(out + s->CountAt, cmIrqGet32(in + s->CountAt) - 1UL);
    return outBytes;
}

ULONG CmIrqQueryStep(PCM_IRQ_STATE st, ULONG ok, ULONG scan, ULONG wire,
                     PULONG line)
{
    *line = 0;
    if (!ok || scan == CM_IRQ_REQ_BAD) {
        return CM_IRQ_ACT_LEAVE;
    }
    if (scan == CM_IRQ_REQ_HAS) {
        st->PciNoIrq = 0;
        st->Added = 0;
        return CM_IRQ_ACT_LEAVE;
    }
    st->PciNoIrq = 1;
    if (wire == 0) {
        return CM_IRQ_ACT_LEAVE;
    }
    *line = wire;
    return CM_IRQ_ACT_ADD;
}

VOID CmIrqQueryAdded(PCM_IRQ_STATE st, ULONG line)
{
    st->Added = line;
}

ULONG CmIrqFilterStep(const CM_IRQ_STATE *st, ULONG ok, ULONG scan,
                      PULONG line)
{
    *line = 0;
    if (!ok || !st->PciNoIrq || st->Added == 0 || scan != CM_IRQ_REQ_NONE) {
        return CM_IRQ_ACT_LEAVE;
    }
    *line = st->Added;
    return CM_IRQ_ACT_ADD;
}

BOOLEAN CmIrqStartNeedsScan(const CM_IRQ_STATE *st)
{
    return (BOOLEAN)(st->PciNoIrq && st->Added != 0);
}

ULONG CmIrqStartStep(const CM_IRQ_STATE *st, ULONG scan)
{
    if (!CmIrqStartNeedsScan(st)) {
        return CM_IRQ_ACT_PASS;
    }
    if (scan == CM_IRQ_START_STRIP) {
        return CM_IRQ_ACT_STRIP;
    }
    if (scan == CM_IRQ_START_NONE) {
        return CM_IRQ_ACT_PASS;
    }
    return CM_IRQ_ACT_FAIL;
}
