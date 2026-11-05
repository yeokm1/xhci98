/*
 * cm_dsd.c - the pure half of the ACPI-named tunnel-port source (cm_dsd.h
 * says what it holds; design record 22 section 7.6.1). C89, DDK-free.
 * IRQL: any.
 */

#include "cm_dsd.h"

/* daffd814-6eba-4d8c-8a91-bc9bbf4aa301 as ToUUID lays it out. */
static const UCHAR cmDsdUuid[16] = {
    0x14, 0xD8, 0xFF, 0xDA, 0xBA, 0x6E, 0x8C, 0x4D,
    0x8A, 0x91, 0xBC, 0x9B, 0xBF, 0x4A, 0xA3, 0x01
};

static const char cmDsdKeyHost[] = "usb4-host-interface";
static const char cmDsdKeyPort[] = "usb4-port-number";

/* One argument inside [*pos, end): its type, data and length; the
 * position advanced past it. 0 when it does not fit. */
typedef struct _CM_DSD_ARG {
    ULONG Type;
    ULONG DataLength;
    const UCHAR *Data;
} CM_DSD_ARG;

static ULONG cmDsdU16(const UCHAR *p)
{
    return (ULONG)p[0] | ((ULONG)p[1] << 8);
}

static ULONG cmDsdU32(const UCHAR *p)
{
    return (ULONG)p[0] | ((ULONG)p[1] << 8) | ((ULONG)p[2] << 16) |
           ((ULONG)p[3] << 24);
}

static ULONG cmDsdNext(const UCHAR *buf, ULONG end, PULONG pos, CM_DSD_ARG *a)
{
    ULONG step;

    if (*pos > end || end - *pos < 8) {
        return 0;
    }
    a->Type = cmDsdU16(buf + *pos);
    a->DataLength = cmDsdU16(buf + *pos + 2);
    a->Data = buf + *pos + 4;
    step = 4 + (a->DataLength > 4 ? a->DataLength : 4);
    if (end - *pos < step) {
        return 0;
    }
    *pos += step;
    return 1;
}

/* A string argument equal to `key` (its data the characters and a NUL). */
static ULONG cmDsdIsKey(const CM_DSD_ARG *a, const char *key)
{
    ULONG i;

    if (a->Type != CM_DSD_ARG_STRING) {
        return 0;
    }
    for (i = 0; key[i] != 0; i++) {
        if (i >= a->DataLength || (char)a->Data[i] != key[i]) {
            return 0;
        }
    }
    /* The rest of the data, if any, is NULs. */
    for (; i < a->DataLength; i++) {
        if (a->Data[i] != 0) {
            return 0;
        }
    }
    return 1;
}

/* The device-properties package's key-value pairs, [pos, end). */
static ULONG cmDsdProps(const UCHAR *buf, ULONG pos, ULONG end, PCM_DSD out)
{
    CM_DSD_ARG pair;
    CM_DSD_ARG key;
    CM_DSD_ARG val;
    ULONG p;
    ULONG pend;
    ULONG haveHost;
    ULONG i;

    haveHost = 0;
    while (pos < end) {
        if (!cmDsdNext(buf, end, &pos, &pair)) {
            return CM_DSD_BAD_SHAPE;
        }
        if (pair.Type != CM_DSD_ARG_PACKAGE) {
            return CM_DSD_BAD_SHAPE;
        }
        p = (ULONG)(pair.Data - buf);
        pend = p + pair.DataLength;
        if (!cmDsdNext(buf, pend, &p, &key) || !cmDsdNext(buf, pend, &p, &val) ||
            p != pend) {
            return CM_DSD_BAD_SHAPE;
        }
        if (cmDsdIsKey(&key, cmDsdKeyHost)) {
            if (haveHost) {
                return CM_DSD_DUPLICATE;
            }
            haveHost = 1;
            if (val.Type != CM_DSD_ARG_STRING) {
                return CM_DSD_BAD_REF;
            }
            out->RefLen = CmDsdNormName((const char *)val.Data, val.DataLength,
                                        out->Ref, sizeof(out->Ref));
            if (out->RefLen == 0) {
                return CM_DSD_BAD_REF;
            }
        } else if (cmDsdIsKey(&key, cmDsdKeyPort)) {
            if (out->HavePort) {
                return CM_DSD_DUPLICATE;
            }
            if (val.Type != CM_DSD_ARG_INTEGER || val.DataLength < 4) {
                return CM_DSD_BAD_PORT;
            }
            /* An integer wider than 32 bits keeps its low half; the port
             * number is for the log alone. */
            out->HavePort = 1;
            out->Port = cmDsdU32(val.Data);
            for (i = 4; i < val.DataLength; i++) {
                if (val.Data[i] != 0) {
                    out->Port = 0xFFFFFFFFUL;
                }
            }
        }
    }
    return haveHost ? CM_DSD_OK : CM_DSD_NO_KEY;
}

/* The _DSD package's elements, [pos, end): UUID and package, in pairs. */
static ULONG cmDsdPairs(const UCHAR *buf, ULONG pos, ULONG end, PCM_DSD out)
{
    CM_DSD_ARG uuid;
    CM_DSD_ARG pkg;
    ULONG found;
    ULONG rc;
    ULONG i;
    ULONG same;

    found = 0;
    rc = CM_DSD_NO_UUID;
    while (pos < end) {
        if (!cmDsdNext(buf, end, &pos, &uuid) ||
            !cmDsdNext(buf, end, &pos, &pkg)) {
            return CM_DSD_BAD_SHAPE;
        }
        if (uuid.Type != CM_DSD_ARG_BUFFER || pkg.Type != CM_DSD_ARG_PACKAGE) {
            return CM_DSD_BAD_SHAPE;
        }
        same = uuid.DataLength == 16;
        for (i = 0; same && i < 16; i++) {
            if (uuid.Data[i] != cmDsdUuid[i]) {
                same = 0;
            }
        }
        if (!same) {
            continue;
        }
        if (found) {
            return CM_DSD_DUPLICATE;
        }
        found = 1;
        rc = cmDsdProps(buf, (ULONG)(pkg.Data - buf),
                        (ULONG)(pkg.Data - buf) + pkg.DataLength, out);
        if (rc != CM_DSD_OK) {
            return rc;
        }
    }
    return rc;
}

ULONG CmDsdRead(const UCHAR *buf, ULONG len, PCM_DSD out)
{
    ULONG length;
    ULONG count;
    ULONG pos;
    ULONG end;
    ULONG n;
    CM_DSD_ARG a;

    out->Ref[0] = 0;
    out->RefLen = 0;
    out->HavePort = 0;
    out->Port = 0;
    if (buf == NULL || len < 12 || cmDsdU32(buf) != CM_DSD_OUT_SIGNATURE) {
        return CM_DSD_BAD_HEADER;
    }
    length = cmDsdU32(buf + 4);
    count = cmDsdU32(buf + 8);
    if (length < 12 || length > len || count == 0) {
        return CM_DSD_BAD_HEADER;
    }
    end = length;
    /* Every top argument inside the answer, and no more than Count. */
    pos = 12;
    for (n = 0; n < count; n++) {
        if (!cmDsdNext(buf, end, &pos, &a)) {
            return CM_DSD_BAD_SHAPE;
        }
    }
    end = pos;
    if (count == 1) {
        /* The package as one argument: its elements are the pairs. */
        pos = 12;
        (VOID)cmDsdNext(buf, end, &pos, &a);
        if (a.Type != CM_DSD_ARG_PACKAGE) {
            return CM_DSD_BAD_SHAPE;
        }
        return cmDsdPairs(buf, (ULONG)(a.Data - buf),
                          (ULONG)(a.Data - buf) + a.DataLength, out);
    }
    /* The package's elements as the answer's arguments. */
    return cmDsdPairs(buf, 12, end, out);
}

static ULONG cmDsdNameChar(char c, char *o)
{
    if (c >= 'a' && c <= 'z') {
        *o = (char)(c - 'a' + 'A');
        return 1;
    }
    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') {
        *o = c;
        return 1;
    }
    return 0;
}

ULONG CmDsdNormName(const char *in, ULONG inLen, char *out, ULONG cap)
{
    ULONG i;
    ULONG o;
    ULONG seg;
    ULONG segs;
    char c;

    if (in == NULL || out == NULL || cap < 2) {
        return 0;
    }
    i = 0;
    o = 0;
    if (inLen > 0 && in[0] == '\\') {
        i = 1;
    }
    out[o++] = '\\';
    segs = 0;
    for (;;) {
        seg = 0;
        while (i < inLen && in[i] != 0 && in[i] != '.') {
            if (seg == 4 || o + 1 >= cap || !cmDsdNameChar(in[i], &c)) {
                return 0;
            }
            out[o++] = c;
            seg++;
            i++;
        }
        if (seg == 0) {
            return 0;
        }
        while (seg < 4) {
            if (o + 1 >= cap) {
                return 0;
            }
            out[o++] = '_';
            seg++;
        }
        segs++;
        if (segs > CM_DSD_SEGMENTS_MAX) {
            return 0;
        }
        if (i < inLen && in[i] == '.') {
            if (o + 1 >= cap) {
                return 0;
            }
            out[o++] = '.';
            i++;
            continue;
        }
        break;
    }
    /* Only NULs may follow the name. */
    for (; i < inLen; i++) {
        if (in[i] != 0) {
            return 0;
        }
    }
    out[o] = 0;
    return o;
}

ULONG CmDsdNormNameW(const USHORT *in, ULONG chars, char *out, ULONG cap)
{
    char tmp[CM_DSD_NAME_MAX + 1];
    ULONG i;

    if (in == NULL) {
        return 0;
    }
    for (i = 0; i < chars && in[i] != 0; i++) {
        if (i >= CM_DSD_NAME_MAX || in[i] > 0x7FU) {
            return 0;
        }
        tmp[i] = (char)in[i];
    }
    return CmDsdNormName(tmp, i, out, cap);
}

ULONG CmDsdClaimsTaken(ULONG claims)
{
    return claims == 1 ? 1UL : 0UL;
}

BOOLEAN CmDsdRenewed(ULONG memberSerial, ULONG claimSerial, BOOLEAN sole)
{
    return (BOOLEAN)(sole && claimSerial != 0 && claimSerial != memberSerial);
}

BOOLEAN CmDsdSameName(const char *a, const char *b)
{
    ULONG i;

    if (a == NULL || b == NULL || a[0] == 0 || b[0] == 0) {
        return FALSE;
    }
    for (i = 0; a[i] != 0 || b[i] != 0; i++) {
        if (a[i] != b[i]) {
            return FALSE;
        }
    }
    return TRUE;
}
