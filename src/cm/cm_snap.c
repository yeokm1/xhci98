/*
 * cm_snap.c - the connection manager's state snapshot: the image's builder
 * (design record 21 section 8.4; roadmap-usb4 task 49.37). Pure, host-tested
 * (test_cm_snap).
 *
 * The image is written in one pass in a fixed order - the header, the table
 * directory, the field directory, the names, the records - and only the
 * bytes inside the window asked for are stored, so a window costs no buffer
 * and every window of one image agrees with every other byte for byte but
 * for the records the running driver changed between them.
 *
 * IRQL: any. Touches only the memory it is handed.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "cm_snapb.h"

typedef struct _CM_QS_OUT {
    UCHAR *Out;             /* the window's first byte (after the header)  */
    ULONG From;             /* image offsets [From, To) are stored          */
    ULONG To;
    ULONG Pos;              /* the image offset of the next byte            */
} CM_QS_OUT;

static VOID cmQsByte(CM_QS_OUT *o, ULONG b)
{
    if (o->Pos >= o->From && o->Pos < o->To) {
        o->Out[o->Pos - o->From] = (UCHAR)(b & 0xFFUL);
    }
    o->Pos++;
}

static VOID cmQsWord(CM_QS_OUT *o, ULONG v)
{
    cmQsByte(o, v);
    cmQsByte(o, v >> 8);
    cmQsByte(o, v >> 16);
    cmQsByte(o, v >> 24);
}

static VOID cmQsPut32(UCHAR *p, ULONG v)
{
    p[0] = (UCHAR)(v & 0xFFUL);
    p[1] = (UCHAR)((v >> 8) & 0xFFUL);
    p[2] = (UCHAR)((v >> 16) & 0xFFUL);
    p[3] = (UCHAR)((v >> 24) & 0xFFUL);
}

static ULONG cmQsGet32(const UCHAR *p)
{
    return (ULONG)p[0] | ((ULONG)p[1] << 8) | ((ULONG)p[2] << 16) |
           ((ULONG)p[3] << 24);
}

static ULONG cmQsNameBytes(const char *s)
{
    ULONG n = 0;

    while (s[n] != '\0' && n < CM_QS_NAME_MAX) {
        n++;
    }
    return n + 1;
}

static VOID cmQsName(CM_QS_OUT *o, const char *s)
{
    ULONG n = cmQsNameBytes(s) - 1;
    ULONG i;

    for (i = 0; i < n; i++) {
        cmQsByte(o, (ULONG)(UCHAR)s[i]);
    }
    cmQsByte(o, 0);
}

ULONG CmQsFieldWords(const CM_QS_FIELD *f)
{
    if (f->Width == 0) {
        return 1;
    }
    return f->Size / f->Width;
}

ULONG CmQsRecordWords(const CM_QS_FIELD *fields, ULONG count)
{
    ULONG words = 0;
    ULONG i;

    for (i = 0; i < count; i++) {
        words += CmQsFieldWords(&fields[i]);
    }
    return words;
}

ULONG CmQsHash(ULONG salt, const UCHAR *p, ULONG bytes)
{
    ULONG h = 0x811C9DC5UL;
    ULONG any = 0;
    ULONG i;

    /* An identity not set (all zero) is 0, and a set one never is. */
    for (i = 0; i < bytes; i++) {
        any |= (ULONG)p[i];
    }
    if (any == 0) {
        return 0;
    }

    for (i = 0; i < 4; i++) {
        h ^= (salt >> (i * 8)) & 0xFFUL;
        h *= 0x01000193UL;
    }
    for (i = 0; i < bytes; i++) {
        h ^= (ULONG)p[i];
        h *= 0x01000193UL;
    }
    h = ((h >> 16) ^ h) & 0xFFFFUL;
    return h != 0 ? h : 1;
}

static ULONG cmQsValue(const UCHAR *p, ULONG width, ULONG flags)
{
    ULONG v;

    if (width == 1) {
        v = (ULONG)p[0];
        if ((flags & CM_QS_FD_SIGNED) && (v & 0x80UL)) {
            v |= 0xFFFFFF00UL;
        }
    } else if (width == 2) {
        v = (ULONG)p[0] | ((ULONG)p[1] << 8);
        if ((flags & CM_QS_FD_SIGNED) && (v & 0x8000UL)) {
            v |= 0xFFFF0000UL;
        }
    } else {
        v = cmQsGet32(p);
    }
    return v;
}

ULONG CmQsBuild(UCHAR *out, ULONG room, ULONG bodyAt, const CM_QS_IN *in,
                const CM_QS_TABLE *tables, ULONG count, PULONG total)
{
    CM_QS_OUT o;
    ULONG records[32];
    ULONG fieldsAt;
    ULONG namesAt;
    ULONG recordsAt;
    ULONG fields;
    ULONG names;
    ULONG left;
    ULONG flags;
    ULONG window;
    ULONG at;
    ULONG pos;
    ULONG t;
    ULONG i;
    ULONG r;
    ULONG e;

    if (count > 32) {
        count = 32;
    }
    flags = in->Flags;
    /* The sizes first: the directories, then the records that fit. */
    fields = 0;
    names = 0;
    for (t = 0; t < count; t++) {
        fields += tables[t].FieldCount;
        names += cmQsNameBytes(tables[t].Name);
        for (i = 0; i < tables[t].FieldCount; i++) {
            names += cmQsNameBytes(tables[t].Fields[i].Name);
        }
    }
    fieldsAt = CM_QS_HDR_BYTES + count * CM_QS_TAB_BYTES;
    namesAt = fieldsAt + fields * CM_QS_FLD_BYTES;
    recordsAt = (namesAt + names + 3UL) & ~3UL;
    if (recordsAt > CM_QS_MAX_BYTES) {
        flags |= CM_QS_F_CUT;
        count = 0;
        fields = 0;
        names = 0;
        fieldsAt = CM_QS_HDR_BYTES;
        namesAt = fieldsAt;
        recordsAt = fieldsAt;
    }
    /* Whole tables only: the first that does not fit, and every table
       after it, keeps its directory and carries no records. */
    left = CM_QS_MAX_BYTES - recordsAt;
    pos = recordsAt;
    for (t = 0; t < count; t++) {
        ULONG bytes = CmQsRecordWords(tables[t].Fields, tables[t].FieldCount) * 4;
        records[t] = tables[t].Base != NULL ? tables[t].Records : 0;
        if ((flags & CM_QS_F_CUT) ||
            (bytes != 0 && records[t] > left / bytes)) {
            records[t] = 0;
            flags |= CM_QS_F_CUT;
        }
        left -= records[t] * bytes;
        pos += records[t] * bytes;
    }
    *total = pos;
    if (room < CM_QS_HDR_BYTES) {
        return 0;
    }

    window = room - CM_QS_HDR_BYTES;
    if (window > CM_QS_WINDOW_MAX) {
        window = CM_QS_WINDOW_MAX;
    }
    if (bodyAt > pos - CM_QS_HDR_BYTES) {
        bodyAt = pos - CM_QS_HDR_BYTES;
    }
    if (window > pos - CM_QS_HDR_BYTES - bodyAt) {
        window = pos - CM_QS_HDR_BYTES - bodyAt;
    }
    for (i = 0; i < CM_QS_HDR_BYTES; i++) {
        out[i] = 0;
    }
    cmQsPut32(out + CM_QS_O_MAGIC, CM_QS_MAGIC);
    cmQsPut32(out + CM_QS_O_VERSION, CM_QS_VERSION);
    cmQsPut32(out + CM_QS_O_HDR_BYTES, CM_QS_HDR_BYTES);
    cmQsPut32(out + CM_QS_O_BYTES, pos);
    cmQsPut32(out + CM_QS_O_FLAGS, flags);
    cmQsPut32(out + CM_QS_O_TABLES_AT, CM_QS_HDR_BYTES);
    cmQsPut32(out + CM_QS_O_TABLES, count);
    cmQsPut32(out + CM_QS_O_DET_BEFORE, in->DetBefore);
    cmQsPut32(out + CM_QS_O_DET_AFTER, in->DetBefore);
    cmQsPut32(out + CM_QS_O_EPOCH_LO, in->EpochLo);
    cmQsPut32(out + CM_QS_O_EPOCH_HI, in->EpochHi);
    cmQsPut32(out + CM_QS_O_DRIVER, in->Driver);
    cmQsPut32(out + CM_QS_O_LEVEL, in->Level);
    cmQsPut32(out + CM_QS_O_CLOCK_MS, in->ClockMs);
    cmQsPut32(out + CM_QS_O_LAYOUT, in->Layout);
    cmQsPut32(out + CM_QS_O_WIN_AT, bodyAt);

    o.Out = out + CM_QS_HDR_BYTES;
    o.From = CM_QS_HDR_BYTES + bodyAt;
    o.To = o.From + window;
    o.Pos = CM_QS_HDR_BYTES;

    /* The table directory. */
    at = namesAt;
    e = fieldsAt;
    r = recordsAt;
    for (t = 0; t < count; t++) {
        ULONG words = CmQsRecordWords(tables[t].Fields, tables[t].FieldCount);
        cmQsWord(&o, at);
        cmQsWord(&o, e);
        cmQsWord(&o, tables[t].FieldCount);
        cmQsWord(&o, r);
        cmQsWord(&o, records[t]);
        cmQsWord(&o, words);
        at += cmQsNameBytes(tables[t].Name);
        for (i = 0; i < tables[t].FieldCount; i++) {
            at += cmQsNameBytes(tables[t].Fields[i].Name);
        }
        e += tables[t].FieldCount * CM_QS_FLD_BYTES;
        r += records[t] * words * 4;
    }
    /* The field directory: each table's names follow its own name. */
    at = namesAt;
    for (t = 0; t < count; t++) {
        at += cmQsNameBytes(tables[t].Name);
        for (i = 0; i < tables[t].FieldCount; i++) {
            const CM_QS_FIELD *f = &tables[t].Fields[i];
            cmQsWord(&o, at);
            cmQsWord(&o, (f->Width & 0xFUL) | ((f->Flags & 0xFFUL) << 8) |
                         ((CmQsFieldWords(f) & 0xFFFFUL) << 16));
            at += cmQsNameBytes(f->Name);
        }
    }
    for (t = 0; t < count; t++) {
        cmQsName(&o, tables[t].Name);
        for (i = 0; i < tables[t].FieldCount; i++) {
            cmQsName(&o, tables[t].Fields[i].Name);
        }
    }
    while (o.Pos < recordsAt) {
        cmQsByte(&o, 0);
    }
    /* The records, gathered field by field; a record wholly outside the
       window is skipped without reading it. */
    for (t = 0; t < count; t++) {
        ULONG words = CmQsRecordWords(tables[t].Fields, tables[t].FieldCount);
        for (r = 0; r < records[t]; r++) {
            const UCHAR *base = tables[t].Base + r * tables[t].Stride;
            if (o.Pos + words * 4 <= o.From || o.Pos >= o.To) {
                o.Pos += words * 4;
                continue;
            }
            for (i = 0; i < tables[t].FieldCount; i++) {
                const CM_QS_FIELD *f = &tables[t].Fields[i];
                if (f->Width == 0) {
                    cmQsWord(&o, CmQsHash(in->Salt, base + f->Offset, f->Size));
                    continue;
                }
                for (e = 0; e < f->Size / f->Width; e++) {
                    cmQsWord(&o, cmQsValue(base + f->Offset + e * f->Width,
                                           f->Width, f->Flags));
                }
            }
        }
    }
    return CM_QS_HDR_BYTES + window;
}

VOID CmQsSetAfter(UCHAR *out, ULONG detAfter)
{
    cmQsPut32(out + CM_QS_O_DET_AFTER, detAfter);
    if (detAfter != cmQsGet32(out + CM_QS_O_DET_BEFORE)) {
        cmQsPut32(out + CM_QS_O_FLAGS,
                  cmQsGet32(out + CM_QS_O_FLAGS) | CM_QS_F_TORN);
    }
}
