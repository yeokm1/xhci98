/*
 * cm_query.c - the query device's answer (cm_qryb.h, cm_query.h; design
 * record 25 section 4.3). Written for this project.
 *
 * WHAT AN EXTENSION HOLDS. Under the USB4 software connection manager, a
 * host port's router entry gets host lane 0's rate, width, link state and
 * speed code as tbt_u4cm.c last read them on its own path
 * (TBT_U4_PORT.LinkLane1), within the port's current connection; a deeper
 * router's entry stays unknown, the core holding no ids or lane reading
 * for it. Under the ICM, an entry gets the rate and width the firmware's
 * device-connected event carried (its header flags, TBT_ICM_EVF_*), with
 * CM_Q_XF_RATE_READ clear: nothing of this driver read a lane adapter.
 *
 * IRQL: any; no lock, no DDK call.
 */

#include "cm_qryb.h"

static VOID cmQPut(UCHAR *buf, ULONG offset, ULONG value)
{
    buf[offset] = (UCHAR)(value & 0xFFUL);
    buf[offset + 1] = (UCHAR)((value >> 8) & 0xFFUL);
    buf[offset + 2] = (UCHAR)((value >> 16) & 0xFFUL);
    buf[offset + 3] = (UCHAR)((value >> 24) & 0xFFUL);
}

static ULONG cmQGet(const UCHAR *buf, ULONG offset)
{
    return (ULONG)buf[offset] | ((ULONG)buf[offset + 1] << 8) |
           ((ULONG)buf[offset + 2] << 16) | ((ULONG)buf[offset + 3] << 24);
}

static VOID cmQCopy(UCHAR *dst, const UCHAR *src, ULONG n)
{
    while (n-- != 0) {
        *dst++ = *src++;
    }
}

static VOID cmQZero(UCHAR *buf, ULONG n)
{
    while (n-- != 0) {
        *buf++ = 0;
    }
}

/* The record's router count, 0 where its size disagrees with it. */
static ULONG cmQRouters(const UCHAR *record, ULONG recordBytes)
{
    ULONG n;

    if (record == NULL || recordBytes < CM_ST_HDR_BYTES ||
        recordBytes > CM_ST_MAX_BYTES) {
        return 0;
    }
    n = cmQGet(record, CM_ST_O_ROUTER_COUNT);
    if (n > CM_ST_MAX_ROUTERS ||
        recordBytes != CM_ST_HDR_BYTES + n * CM_ST_ENTRY_BYTES ||
        cmQGet(record, CM_ST_O_BYTES) != recordBytes) {
        return 0;
    }
    return n;
}

static VOID cmQUnknown(UCHAR *e)
{
    cmQZero(e, CM_Q_EXT_BYTES);
    cmQPut(e, CM_Q_X_LINK_STATE, CM_Q_UNKNOWN);
    cmQPut(e, CM_Q_X_GEN_RAW, CM_Q_UNKNOWN);
}

static VOID cmQLane(UCHAR *e, ULONG lane1)
{
    ULONG rate;
    ULONG width;

    switch (TBT_LANE1_SPEED(lane1)) {
    case TBT_LANE_SPEED_GEN4:
        rate = 40;
        break;
    case TBT_LANE_SPEED_GEN3:
        rate = 20;
        break;
    case TBT_LANE_SPEED_GEN2:
        rate = 10;
        break;
    default:
        rate = 0;
        break;
    }
    switch (TBT_LANE1_NLW(lane1)) {
    case TBT_LANE_WIDTH_X1:
        width = 1;
        break;
    case TBT_LANE_WIDTH_X2:
        width = 2;
        break;
    default:
        width = 0;
        break;
    }
    cmQPut(e, CM_Q_X_RATE, rate);
    cmQPut(e, CM_Q_X_WIDTH, width);
    cmQPut(e, CM_Q_X_LINK_STATE, TBT_LANE1_STATE(lane1));
    cmQPut(e, CM_Q_X_GEN_RAW, TBT_LANE1_SPEED(lane1));
    cmQPut(e, CM_Q_X_FLAGS, CM_Q_XF_RATE_READ);
}

/* The k-th device of the ICM's table in use, the order cm_status.c walks. */
static const TBT_ICM_DEVICE *cmQIcmDevice(const TBT_ICM *icm, ULONG k)
{
    ULONG i;

    for (i = 0; i < TBT_ICM_MAX_DEVICES; i++) {
        if (icm->Devices[i].InUse) {
            if (k == 0) {
                return &icm->Devices[i];
            }
            k--;
        }
    }
    return NULL;
}

static const TBT_U4_PORT *cmQU4Port(const TBT_U4CM *u4, ULONG index)
{
    ULONG i;

    for (i = 0; i < u4->PortCount && i < TBT_U4_MAX_PORTS; i++) {
        if (u4->Ports[i].Index == index) {
            return &u4->Ports[i];
        }
    }
    return NULL;
}

ULONG CmQueryExt(UCHAR *ext, ULONG room, const UCHAR *record,
                 ULONG recordBytes, const TBT_ICM *icm, const TBT_U4CM *u4)
{
    ULONG n;
    ULONG i;
    ULONG cm;
    ULONG kind;
    ULONG icmSeen;
    UCHAR *e;
    const UCHAR *r;
    const TBT_ICM_DEVICE *d;
    const TBT_U4_PORT *p;

    n = cmQRouters(record, recordBytes);
    if (n > room) {
        n = room;
    }
    cm = n != 0 ? cmQGet(record, CM_ST_O_CM) : CM_ST_CM_NONE;
    icmSeen = 0;
    for (i = 0; i < n; i++) {
        e = ext + i * CM_Q_EXT_BYTES;
        r = record + CM_ST_HDR_BYTES + i * CM_ST_ENTRY_BYTES;
        cmQUnknown(e);
        kind = cmQGet(r, CM_ST_E_KIND);
        if (kind == CM_ST_KIND_ICM && cm == CM_ST_CM_ICM && icm != NULL) {
            d = cmQIcmDevice(icm, icmSeen++);
            if (d != NULL &&
                (!d->RouteValid ||
                 (cmQGet(r, CM_ST_E_ROUTE_LO) == d->Route.Lo &&
                  cmQGet(r, CM_ST_E_ROUTE_HI) == d->Route.Hi))) {
                cmQPut(e, CM_Q_X_RATE,
                       (d->EventFlags & TBT_ICM_EVF_SPEED_20G) != 0 ? 20 : 10);
                cmQPut(e, CM_Q_X_WIDTH,
                       (d->EventFlags & TBT_ICM_EVF_DUAL_LANE) != 0 ? 2 : 1);
            }
        } else if (kind == CM_ST_KIND_USB4_PORT && cm == CM_ST_CM_USB4 &&
                   u4 != NULL) {
            p = cmQU4Port(u4, cmQGet(r, CM_ST_E_PLACE) & 0xFFUL);
            if (p != NULL && p->LinkSeen &&
                p->LinkGeneration == p->Generation) {
                cmQLane(e, p->LinkLane1);
            }
        }
    }
    return n;
}

ULONG CmQueryBuild(UCHAR *buf, const CM_QUERY_IN *in)
{
    ULONG n;
    ULONG rec;
    ULONG ext;
    ULONG id;
    ULONG i;
    ULONG at;

    cmQZero(buf, CM_Q_HDR_BYTES);
    n = cmQRouters(in->Record, in->RecordBytes);
    rec = 0;
    if (in->Record != NULL && in->RecordBytes >= CM_ST_HDR_BYTES &&
        in->RecordBytes <= CM_ST_MAX_BYTES) {
        rec = CM_ST_HDR_BYTES + n * CM_ST_ENTRY_BYTES;
        cmQCopy(buf + CM_Q_HDR_BYTES, in->Record, CM_ST_HDR_BYTES);
        if (n == 0) {
            /* A record cut to its header says so in its own fields. */
            cmQPut(buf + CM_Q_HDR_BYTES, CM_ST_O_BYTES, CM_ST_HDR_BYTES);
            cmQPut(buf + CM_Q_HDR_BYTES, CM_ST_O_ROUTER_COUNT, 0);
        } else {
            cmQCopy(buf + CM_Q_HDR_BYTES + CM_ST_HDR_BYTES,
                    in->Record + CM_ST_HDR_BYTES, n * CM_ST_ENTRY_BYTES);
        }
    }
    at = CM_Q_HDR_BYTES + rec;
    ext = at;
    for (i = 0; i < n; i++) {
        if (in->Ext != NULL && i < in->ExtCount) {
            cmQCopy(buf + at, in->Ext + i * CM_Q_EXT_BYTES, CM_Q_EXT_BYTES);
        } else {
            cmQUnknown(buf + at);
        }
        at += CM_Q_EXT_BYTES;
    }
    id = in->Id != NULL ? in->IdBytes : 0;
    if (id > CM_Q_ID_MAX) {
        id = 0;
    }
    for (i = 0; i < id; i++) {
        if (in->Id[i] < 0x20 || in->Id[i] > 0x7E) {
            id = 0;
            break;
        }
    }
    cmQCopy(buf + at, (const UCHAR *)in->Id, id);
    cmQPut(buf, CM_Q_O_MAGIC, CM_Q_MAGIC);
    cmQPut(buf, CM_Q_O_VERSION, CM_Q_VERSION);
    cmQPut(buf, CM_Q_O_BYTES, at + id);
    cmQPut(buf, CM_Q_O_PASS, 0);
    cmQPut(buf, CM_Q_O_PASS_AGE_MS, CM_Q_UNKNOWN);
    cmQPut(buf, CM_Q_O_LIVE, in->Live);
    cmQPut(buf, CM_Q_O_FLAGS,
           (in->Inert ? CM_Q_F_INERT : 0) |
               (rec != 0 && (cmQGet(buf + CM_Q_HDR_BYTES, CM_ST_O_FLAGS) &
                             CM_ST_F_TRUNCATED) != 0
                    ? CM_Q_F_TRUNCATED
                    : 0));
    cmQPut(buf, CM_Q_O_REC_OFFSET, rec != 0 ? CM_Q_HDR_BYTES : 0);
    cmQPut(buf, CM_Q_O_EXT_OFFSET, ext);
    cmQPut(buf, CM_Q_O_EXT_SHAPE, (n & 0xFFFFUL) | (CM_Q_EXT_BYTES << 16));
    cmQPut(buf, CM_Q_O_EPOCH_LO, in->EpochLo);
    cmQPut(buf, CM_Q_O_EPOCH_HI, in->EpochHi);
    cmQPut(buf, CM_Q_O_NHI_BDF, in->NhiBdf);
    cmQPut(buf, CM_Q_O_ID_OFFSET, at);
    cmQPut(buf, CM_Q_O_ID_BYTES, id);
    return at + id;
}

/* A byte the content generation does not follow. */
static BOOLEAN cmQMasked(const UCHAR *a, ULONG i)
{
    ULONG rec;
    ULONG r;

    if ((i >= CM_Q_O_PASS && i < CM_Q_O_PASS_AGE_MS + 4) ||
        (i >= CM_Q_O_CONTENT_GEN && i < CM_Q_O_CONTENT_GEN + 4)) {
        return TRUE;
    }
    rec = cmQGet(a, CM_Q_O_REC_OFFSET);
    if (rec == 0 || i < rec || i >= rec + CM_ST_HDR_BYTES) {
        return FALSE;
    }
    r = i - rec;
    return (BOOLEAN)((r >= CM_ST_O_GENERATION && r < CM_ST_O_UPTIME_S + 4) ||
                     (r >= CM_ST_O_SYSTIME_LO && r < CM_ST_O_ECHO + 4));
}

ULONG CmQueryContentGen(UCHAR *next, ULONG nextBytes, const UCHAR *prev,
                        ULONG prevBytes)
{
    ULONG gen;
    ULONG i;
    BOOLEAN changed;

    if (prev == NULL || prevBytes < CM_Q_HDR_BYTES ||
        cmQGet(prev, CM_Q_O_CONTENT_GEN) == 0) {
        gen = 1;
    } else {
        gen = cmQGet(prev, CM_Q_O_CONTENT_GEN);
        changed = (BOOLEAN)(prevBytes != nextBytes ||
                            cmQGet(prev, CM_Q_O_REC_OFFSET) !=
                                cmQGet(next, CM_Q_O_REC_OFFSET));
        for (i = 0; i < nextBytes && !changed; i++) {
            if (prev[i] != next[i] && !cmQMasked(next, i)) {
                changed = TRUE;
            }
        }
        if (changed) {
            gen++;
            if (gen == 0) {
                gen = 1;
            }
        }
    }
    if (nextBytes >= CM_Q_HDR_BYTES) {
        cmQPut(next, CM_Q_O_CONTENT_GEN, gen);
    }
    return gen;
}

ULONG CmQueryAnswer(UCHAR *out, ULONG room, const UCHAR *built, ULONG bytes,
                    ULONG pass, ULONG ageMs, BOOLEAN stopping, PULONG info)
{
    ULONG n;
    ULONG rc;

    *info = 0;
    if (room < CM_Q_HDR_BYTES || bytes < CM_Q_HDR_BYTES) {
        return CM_Q_ANS_TOO_SMALL;
    }
    if (room < bytes) {
        n = CM_Q_HDR_BYTES;
        rc = CM_Q_ANS_HEADER;
    } else {
        n = bytes;
        rc = CM_Q_ANS_WHOLE;
    }
    cmQCopy(out, built, n);
    cmQPut(out, CM_Q_O_BYTES, bytes);
    cmQPut(out, CM_Q_O_PASS, pass);
    cmQPut(out, CM_Q_O_PASS_AGE_MS, pass != 0 ? ageMs : CM_Q_UNKNOWN);
    if (stopping) {
        cmQPut(out, CM_Q_O_FLAGS,
               cmQGet(out, CM_Q_O_FLAGS) | CM_Q_F_STOPPING);
    }
    *info = n;
    return rc;
}

ULONG CmQueryIdFromLink(const USHORT *link, ULONG chars, char *id,
                        ULONG room)
{
    ULONG first;
    ULONG last;
    ULONG start;
    ULONG i;
    ULONG n;

    for (first = 0; first < chars && link[first] != '#'; first++) {
    }
    for (last = chars; last > first && link[last - 1] != '#'; last--) {
    }
    if (first >= chars || last == 0 || last - 1 <= first) {
        return 0;
    }
    last--;
    for (start = first; start > 0 && link[start - 1] != '\\'; start--) {
    }
    n = 0;
    for (i = start; i < last; i++) {
        if (n >= room || link[i] < 0x20 || link[i] > 0x7E) {
            return 0;
        }
        id[n++] = link[i] == '#' ? '\\' : (char)link[i];
    }
    return n;
}

/* a + b held to CM_Q_UNKNOWN - 1. */
static ULONG cmQAddSat(ULONG a, ULONG b)
{
    if (a > CM_Q_UNKNOWN - 1UL || b > CM_Q_UNKNOWN - 1UL - a) {
        return CM_Q_UNKNOWN - 1UL;
    }
    return a + b;
}

ULONG CmQueryAgeMs(ULONG ticksNow, ULONG ticksThen, ULONG incr)
{
    ULONG delta;
    ULONG a;
    ULONG b;
    ULONG q;
    ULONG r;
    ULONG ms;

    delta = ticksNow - ticksThen;
    /* A clock read before the publication it is measured against (a
     * negative difference) is age 0, never a wrap to 49 days. */
    if (incr == 0 || (delta & 0x80000000UL) != 0) {
        return 0;
    }
    /* delta x incr / 10^4 in words, for any tick period (10 to 55 ms is
     * 100000 to 550000 units): incr = a x 10^4 + b, and delta x b / 10^4
     * as q x b + r x b / 10^4 with delta = q x 10^4 + r; every product
     * below 2^32 or held. */
    a = incr / 10000UL;
    b = incr % 10000UL;
    q = delta / 10000UL;
    r = delta % 10000UL;
    if (a != 0 && delta > (CM_Q_UNKNOWN - 1UL) / a) {
        return CM_Q_UNKNOWN - 1UL;
    }
    ms = delta * a;
    ms = cmQAddSat(ms, q * b);
    return cmQAddSat(ms, r * b / 10000UL);
}

VOID CmQuerySetAge(UCHAR *out, ULONG info, ULONG pass, ULONG ageMs)
{
    if (info >= CM_Q_HDR_BYTES) {
        cmQPut(out, CM_Q_O_PASS_AGE_MS, pass != 0 ? ageMs : CM_Q_UNKNOWN);
    }
}

ULONG CmQueryBumpGen(UCHAR *answer, ULONG bytes)
{
    ULONG gen;

    if (bytes < CM_Q_HDR_BYTES) {
        return 0;
    }
    gen = cmQGet(answer, CM_Q_O_CONTENT_GEN) + 1;
    if (gen == 0) {
        gen = 1;
    }
    cmQPut(answer, CM_Q_O_CONTENT_GEN, gen);
    return gen;
}
