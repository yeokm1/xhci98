/*
 * cm_qpci.c - the query device's PCI answer (cm_qryb.h, cm_query.h
 * CM_QP_*; design record 25 section 4.7, revision 12). Written for this
 * project.
 *
 * WHAT IS READ. The NHI, each member of the domain's tunnel-port set, and,
 * where the caller allows it (Windows 2000 and later), the functions below
 * each member: the member's secondary bus, then the secondary bus of each
 * bridge found there whose bus numbers lie inside the member's own range,
 * each bus once, at most CM_QP_MAX_BUSES buses a member, CM_QP_MAX_DEPTH
 * bridges deep and CM_QP_MAX_BELOW functions in all. A bound reached sets
 * CM_QP_F_TRUNCATED; the walk never loops, since a bus is entered only
 * when its number is above its parent bus's and not yet visited.
 *
 * Nothing is written to configuration space: a reader the caller supplies
 * reads from offset 0 (the HalGetBusData form every configuration read of
 * this driver's takes, cm_reenum.c). Bus numbers come from the type 1
 * header as the PCI-to-PCI Bridge Architecture Specification 1.2 section
 * 3.2.5.2 to 3.2.5.4 places them (18h primary, 19h secondary, 1Ah
 * subordinate), the header type from the PCI Local Bus Specification 3.0
 * section 6.2.1 (0Eh, bit 7 multi-function, bits 6:0 the layout).
 *
 * IRQL: any; no lock, no DDK call. The reader's own IRQL rules apply.
 */

#include "cm_qryb.h"

#define CM_QP_CFG_HEADER    0x0EUL
#define CM_QP_CFG_SECONDARY 0x19UL
#define CM_QP_CFG_SUBORD    0x1AUL
#define CM_QP_HDR_MF        0x80UL
#define CM_QP_HDR_LAYOUT    0x7FUL
#define CM_QP_LAYOUT_BRIDGE 0x01UL

static VOID cmQpPut(UCHAR *buf, ULONG offset, ULONG value)
{
    buf[offset] = (UCHAR)(value & 0xFFUL);
    buf[offset + 1] = (UCHAR)((value >> 8) & 0xFFUL);
    buf[offset + 2] = (UCHAR)((value >> 16) & 0xFFUL);
    buf[offset + 3] = (UCHAR)((value >> 24) & 0xFFUL);
}

static ULONG cmQpGet(const UCHAR *buf, ULONG offset)
{
    return (ULONG)buf[offset] | ((ULONG)buf[offset + 1] << 8) |
           ((ULONG)buf[offset + 2] << 16) | ((ULONG)buf[offset + 3] << 24);
}

static VOID cmQpFill(UCHAR *buf, ULONG n, UCHAR v)
{
    while (n-- != 0) {
        *buf++ = v;
    }
}

static ULONG cmQpBdf(ULONG bus, ULONG dev, ULONG fn)
{
    return ((bus & 0xFFUL) << 8) | ((dev & 0x1FUL) << 3) | (fn & 7UL);
}

typedef struct _CM_QP_WALK {
    UCHAR *Buf;
    const CM_QPCI_IN *In;
    ULONG Count;            /* entries written */
    ULONG Below;            /* BELOW entries written */
    ULONG Flags;
    ULONG Reads;
    ULONG Fails;
} CM_QP_WALK;

/* A new entry's head, its configuration space zero; NULL when the answer
 * is full (never: the bounds keep Count below CM_QP_MAX_ENTRIES). */
static UCHAR *cmQpEntry(CM_QP_WALK *w, ULONG bdf, ULONG role, ULONG port,
                        ULONG parent, ULONG depth, ULONG info)
{
    UCHAR *e;

    if (w->Count >= CM_QP_MAX_ENTRIES) {
        w->Flags |= CM_QP_F_TRUNCATED;
        return NULL;
    }
    e = w->Buf + CM_QP_HDR_BYTES + w->Count * CM_QP_ENT_BYTES;
    cmQpFill(e, CM_QP_ENT_BYTES, 0);
    cmQpPut(e, CM_QP_E_BDF, bdf);
    cmQpPut(e, CM_QP_E_ROLE, role);
    cmQpPut(e, CM_QP_E_STATE, CM_QP_S_FAILED);
    cmQpPut(e, CM_QP_E_PORT, port);
    cmQpPut(e, CM_QP_E_PARENT, parent);
    cmQpPut(e, CM_QP_E_DEPTH, depth);
    cmQpPut(e, CM_QP_E_INFO, info);
    w->Count++;
    return e;
}

/* The function's 256 bytes into the entry; its state. */
static ULONG cmQpFill256(CM_QP_WALK *w, UCHAR *e, ULONG bdf)
{
    UCHAR *cfg;
    ULONG rc;
    ULONG state;

    cfg = e + CM_QP_EHDR_BYTES;
    w->Reads++;
    rc = w->In->Read(w->In->Context, bdf, cfg, CM_QP_CFG_BYTES);
    if (rc == CM_QP_RD_OK && cmQpGet(cfg, 0) != 0xFFFFFFFFUL) {
        state = CM_QP_S_READ;
    } else if (rc == CM_QP_RD_OK || rc == CM_QP_RD_ABSENT) {
        state = CM_QP_S_ABSENT;
    } else {
        w->Fails++;
        state = CM_QP_S_FAILED;
    }
    if (state != CM_QP_S_READ) {
        cmQpFill(cfg, CM_QP_CFG_BYTES, 0);
    }
    cmQpPut(e, CM_QP_E_STATE, state);
    return state;
}

/* The function at bdf answers: its vendor id is neither all ones nor 0.
 * *failed set when the read was refused. */
static ULONG cmQpPresent(CM_QP_WALK *w, ULONG bdf, PULONG failed)
{
    UCHAR id[4];
    ULONG rc;
    ULONG v;

    *failed = 0;
    w->Reads++;
    rc = w->In->Read(w->In->Context, bdf, id, 4);
    if (rc == CM_QP_RD_ABSENT) {
        return 0;
    }
    if (rc != CM_QP_RD_OK) {
        w->Fails++;
        *failed = 1;
        return 0;
    }
    v = cmQpGet(id, 0) & 0xFFFFUL;
    return (ULONG)(v != 0xFFFFUL && v != 0);
}

/* The functions below one member whose type 1 header is `pcfg`. */
static VOID cmQpBelow(CM_QP_WALK *w, ULONG port, const UCHAR *pcfg)
{
    ULONG visited[8];
    ULONG qBus[CM_QP_MAX_BUSES];
    ULONG qParent[CM_QP_MAX_BUSES];
    ULONG qDepth[CM_QP_MAX_BUSES];
    ULONG qHead;
    ULONG qTail;
    ULONG sec;
    ULONG sub;
    ULONG bus;
    ULONG dev;
    ULONG fn;
    ULONG fnMax;
    ULONG bdf;
    ULONG failed;   /* a refused read is counted, and skipped */
    ULONG csec;
    ULONG csub;
    ULONG i;
    UCHAR *e;
    const UCHAR *cfg;

    sec = pcfg[CM_QP_CFG_SECONDARY];
    sub = pcfg[CM_QP_CFG_SUBORD];
    /* A member with no bus numbers (0, or not above its own bus) has
     * nothing below it the walk could reach. */
    if (sec == 0 || sec <= ((port >> 8) & 0xFFUL) || sub < sec) {
        return;
    }
    for (i = 0; i < 8; i++) {
        visited[i] = 0;
    }
    qHead = 0;
    qTail = 0;
    qBus[qTail] = sec;
    qParent[qTail] = port;
    qDepth[qTail] = 0;
    qTail++;
    visited[sec >> 5] |= 1UL << (sec & 31UL);
    while (qHead < qTail) {
        bus = qBus[qHead];
        for (dev = 0; dev < 32; dev++) {
            fnMax = 1;
            for (fn = 0; fn < fnMax; fn++) {
                bdf = cmQpBdf(bus, dev, fn);
                if (!cmQpPresent(w, bdf, &failed)) {
                    if (fn == 0) {
                        break;
                    }
                    continue;
                }
                if (w->Below >= CM_QP_MAX_BELOW) {
                    w->Flags |= CM_QP_F_TRUNCATED;
                    return;
                }
                e = cmQpEntry(w, bdf, CM_QP_R_BELOW, port, qParent[qHead],
                              qDepth[qHead], 0);
                if (e == NULL) {
                    return;
                }
                w->Below++;
                if (cmQpFill256(w, e, bdf) != CM_QP_S_READ) {
                    continue;
                }
                cfg = e + CM_QP_EHDR_BYTES;
                if (fn == 0 && (cfg[CM_QP_CFG_HEADER] & CM_QP_HDR_MF) != 0) {
                    fnMax = 8;
                }
                if ((cfg[CM_QP_CFG_HEADER] & CM_QP_HDR_LAYOUT) !=
                    CM_QP_LAYOUT_BRIDGE) {
                    continue;
                }
                csec = cfg[CM_QP_CFG_SECONDARY];
                csub = cfg[CM_QP_CFG_SUBORD];
                /* Only a bus inside the member's range, above this one,
                 * not yet visited: the walk cannot loop or leave it. */
                if (csec <= bus || csec < sec || csec > sub || csub < csec ||
                    csub > sub ||
                    (visited[csec >> 5] & (1UL << (csec & 31UL))) != 0) {
                    continue;
                }
                if (qDepth[qHead] + 1 >= CM_QP_MAX_DEPTH ||
                    qTail >= CM_QP_MAX_BUSES) {
                    w->Flags |= CM_QP_F_TRUNCATED;
                    continue;
                }
                visited[csec >> 5] |= 1UL << (csec & 31UL);
                qBus[qTail] = csec;
                qParent[qTail] = bdf;
                qDepth[qTail] = qDepth[qHead] + 1;
                qTail++;
            }
        }
        qHead++;
    }
}

ULONG CmQpciBuild(UCHAR *buf, ULONG room, const CM_QPCI_IN *in)
{
    CM_QP_WALK w;
    ULONG ports;
    ULONG p;
    ULONG bytes;
    UCHAR *e;
    const UCHAR *cfg;

    if (buf == NULL || in == NULL || in->Read == NULL ||
        room < CM_QP_MAX_BYTES) {
        return 0;
    }
    cmQpFill(buf, CM_QP_HDR_BYTES, 0);
    w.Buf = buf;
    w.In = in;
    w.Count = 0;
    w.Below = 0;
    w.Flags = in->Flags & CM_QP_F_STOPPING;
    w.Reads = 0;
    w.Fails = 0;
    ports = in->PortCount;
    if (ports > CM_QP_MAX_PORTS) {
        ports = CM_QP_MAX_PORTS;
        w.Flags |= CM_QP_F_TRUNCATED;
    }
    if (ports == 0) {
        w.Flags |= CM_QP_F_NO_SET;
    }
    if (!in->ReadBelow) {
        w.Flags |= CM_QP_F_BELOW_OS;
    }

    e = cmQpEntry(&w, in->NhiBdf, CM_QP_R_NHI, CM_Q_UNKNOWN, CM_Q_UNKNOWN, 0,
                  0);
    if (e != NULL && in->NhiBdf <= 0xFFFFUL) {
        (VOID)cmQpFill256(&w, e, in->NhiBdf);
    }
    for (p = 0; p < ports; p++) {
        e = cmQpEntry(&w, in->Ports[p].Bdf, CM_QP_R_PORT, in->Ports[p].Bdf,
                      CM_Q_UNKNOWN, 0,
                      (in->Ports[p].Source & 0xFFUL) |
                          ((in->Ports[p].Flags & 0xFFUL) << 8));
        if (e == NULL || in->Ports[p].Bdf > 0xFFFFUL ||
            cmQpFill256(&w, e, in->Ports[p].Bdf) != CM_QP_S_READ) {
            continue;
        }
        if (!in->ReadBelow) {
            e = cmQpEntry(&w, CM_Q_UNKNOWN, CM_QP_R_BELOW, in->Ports[p].Bdf,
                          CM_Q_UNKNOWN, 0, 0);
            if (e != NULL) {
                cmQpPut(e, CM_QP_E_STATE, CM_QP_S_NOT_READ_OS);
            }
            continue;
        }
        cfg = e + CM_QP_EHDR_BYTES;
        if ((cfg[CM_QP_CFG_HEADER] & CM_QP_HDR_LAYOUT) == CM_QP_LAYOUT_BRIDGE) {
            cmQpBelow(&w, in->Ports[p].Bdf, cfg);
        }
    }

    bytes = CM_QP_HDR_BYTES + w.Count * CM_QP_ENT_BYTES;
    cmQpPut(buf, CM_QP_O_MAGIC, CM_QP_MAGIC);
    cmQpPut(buf, CM_QP_O_VERSION, CM_QP_VERSION);
    cmQpPut(buf, CM_QP_O_BYTES, bytes);
    cmQpPut(buf, CM_QP_O_FLAGS, w.Flags);
    cmQpPut(buf, CM_QP_O_EPOCH_LO, in->EpochLo);
    cmQpPut(buf, CM_QP_O_EPOCH_HI, in->EpochHi);
    cmQpPut(buf, CM_QP_O_ENT_OFFSET, CM_QP_HDR_BYTES);
    cmQpPut(buf, CM_QP_O_ENT_SHAPE, w.Count | (CM_QP_ENT_BYTES << 16));
    cmQpPut(buf, CM_QP_O_NHI_BDF, in->NhiBdf);
    cmQpPut(buf, CM_QP_O_PORTS, ports);
    cmQpPut(buf, CM_QP_O_READS, w.Reads);
    cmQpPut(buf, CM_QP_O_FAILS, w.Fails);
    return bytes;
}

ULONG CmQpciAnswer(UCHAR *out, ULONG room, const UCHAR *built, ULONG bytes,
                   PULONG info)
{
    ULONG n;
    ULONG rc;
    ULONG i;

    *info = 0;
    if (out == NULL || room < CM_QP_HDR_BYTES || bytes < CM_QP_HDR_BYTES) {
        return CM_Q_ANS_TOO_SMALL;
    }
    if (room < bytes) {
        n = CM_QP_HDR_BYTES;
        rc = CM_Q_ANS_HEADER;
    } else {
        n = bytes;
        rc = CM_Q_ANS_WHOLE;
    }
    for (i = 0; i < n; i++) {
        out[i] = built[i];
    }
    *info = n;
    return rc;
}
