/*
 * u4_fakefab.c - the fake USB4 fabric of test_u4cm (see u4_fakefab.h).
 *
 * Written for the host suite from the USB4 Specification 2.0 (sections
 * 4.4.5.2, 6.4, 8.2, 8.3, 12.3, 12.4, 12.6) and usb4-emu's documented
 * host models (design record 22 section 2.1), independently of
 * src\cm\tbt_u4cm.c. C89 with the C library; test-only.
 */

#include <stdio.h>
#include <string.h>
#include "u4_fakefab.h"

/* ---------------------------------------------------------------------- */
/* CRC and memory                                                         */
/* ---------------------------------------------------------------------- */

static unsigned long fabCrc(const unsigned char *p, unsigned long n)
{
    unsigned long crc = 0xFFFFFFFFUL;
    unsigned long i;
    int k;

    for (i = 0; i < n; i++) {
        crc ^= p[i];
        for (k = 0; k < 8; k++) {
            crc = (crc & 1UL) ? (crc >> 1) ^ 0x82F63B78UL : crc >> 1;
        }
    }
    return (crc ^ 0xFFFFFFFFUL) & 0xFFFFFFFFUL;
}

static unsigned char *fabAt(FAB *f, unsigned long phys)
{
    if (phys < FAB_PHYS || phys >= FAB_PHYS + FAB_MEM_BYTES) {
        return NULL;
    }
    return (unsigned char *)f->MemDw + (phys - FAB_PHYS);
}

static void putBe(unsigned char *p, unsigned long v)
{
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
}

static unsigned long getBe(const unsigned char *p)
{
    return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) |
           ((unsigned long)p[2] << 8) | (unsigned long)p[3];
}

static void putLe(unsigned char *p, unsigned long v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

static unsigned long getLe(const unsigned char *p)
{
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8) |
           ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

static void fabQueue(FAB *f, unsigned long pdf, const unsigned long *dw,
                     unsigned long words, unsigned long delayUs);

void fabInject(FAB *f, unsigned long pdf, const unsigned long *dw, unsigned long words)
{
    fabQueue(f, pdf, dw, words, 0);
}

void *fabMem(FAB *f)
{
    return f->MemDw;
}

/* ---------------------------------------------------------------------- */
/* The receive side                                                       */
/* ---------------------------------------------------------------------- */

static void fabQueue(FAB *f, unsigned long pdf, const unsigned long *dw,
                     unsigned long words, unsigned long delayUs)
{
    FAB_FRAME *fr;
    unsigned long at;
    unsigned long pos;

    if (f->RxQCount >= FAB_RXQ || words > 62) {
        f->RxDropped++;
        return;
    }
    at = f->NowUs + delayUs;
    pos = f->RxQCount;
    while (pos > 0 && f->RxQ[pos - 1].AtUs > at) {
        f->RxQ[pos] = f->RxQ[pos - 1];
        pos--;
    }
    f->RxQCount++;
    fr = &f->RxQ[pos];
    fr->Pdf = pdf;
    fr->Words = words;
    fr->AtUs = at;
    memcpy(fr->Dw, dw, words * sizeof(unsigned long));
}

static void fabPump(FAB *f)
{
    FAB_FRAME *fr;
    unsigned char *d;
    unsigned char *buf;
    unsigned long i;
    unsigned long len;

    while (f->RxQCount != 0 && f->RxQ[0].AtUs <= f->NowUs) {
        fr = &f->RxQ[0];
        if ((f->RxCtrl & 0x80000000UL) == 0 || f->RxSize == 0 ||
            f->RxProd == f->RxCons) {
            /* No buffer: a real router retries later (Spec 6.6 for the
             * events); keep it unless the ring is off. */
            if ((f->RxCtrl & 0x80000000UL) == 0) {
                f->RxDropped++;
            } else {
                return;
            }
        } else {
            d = fabAt(f, f->RxBaseLo + 16 * f->RxProd);
            buf = d != NULL ? fabAt(f, getLe(d)) : NULL;
            if (buf != NULL) {
                for (i = 0; i < fr->Words; i++) {
                    putBe(buf + 4 * i, fr->Dw[i]);
                }
                putBe(buf + 4 * fr->Words, fabCrc(buf, 4 * fr->Words));
                len = 4 * fr->Words + 4;
                putLe(d + 8, (len & 0xFFFUL) | ((fr->Pdf & 0xFUL) << 12) | 0x00200000UL);
            }
            f->RxProd = (f->RxProd + 1) % f->RxSize;
        }
        for (i = 1; i < f->RxQCount; i++) {
            f->RxQ[i - 1] = f->RxQ[i];
        }
        f->RxQCount--;
    }
}

/* ---------------------------------------------------------------------- */
/* Routes and routers                                                     */
/* ---------------------------------------------------------------------- */

static void fabRouteOf(const FAB *f, int r, unsigned long *hi, unsigned long *lo)
{
    unsigned long lv[8];
    int n;
    int i;

    n = 0;
    while (r > 0 && n < 7) {
        lv[n++] = (unsigned long)f->R[r].ParentAdp;
        r = f->R[r].Parent;
    }
    *hi = 0;
    *lo = 0;
    for (i = 0; i < n; i++) {
        if (i < 4) {
            *lo |= lv[n - 1 - i] << (8 * i);
        } else {
            *hi |= lv[n - 1 - i] << (8 * (i - 4));
        }
    }
}

static unsigned long laneState(const FAB_ADP *a)
{
    return (a->Cs[FAB_CAP_LANE + 1] >> 26) & 0xFUL;
}

static void setLaneState(FAB_ADP *a, unsigned long s)
{
    a->Cs[FAB_CAP_LANE + 1] = (a->Cs[FAB_CAP_LANE + 1] & ~(0xFUL << 26)) | (s << 26);
}

static void setLaneWidth(FAB_ADP *a, unsigned long w)
{
    a->Cs[FAB_CAP_LANE + 1] = (a->Cs[FAB_CAP_LANE + 1] & ~(0x3FUL << 20)) | (w << 20);
}

/* Walk a route; -1 with the reporting router, the code and its adapter
 * when it cannot be delivered (Spec 6.4.3). */
static int fabResolve(FAB *f, unsigned long hi, unsigned long lo, int *errRouter,
                      unsigned long *code, unsigned long *errAdp)
{
    int r;
    int level;
    unsigned long a;
    FAB_ADP *ad;

    r = 0;
    for (level = 0; level < 7; level++) {
        a = level < 4 ? (lo >> (8 * level)) & 0x3FUL : (hi >> (8 * (level - 4))) & 0x3FUL;
        if (a == 0) {
            break;
        }
        if (a >= FAB_MAX_ADP || !f->R[r].Adp[a].Present) {
            *errRouter = r;
            *code = TBT_EV_ERR_ADP;
            *errAdp = a;
            return -1;
        }
        ad = &f->R[r].Adp[a];
        if (ad->Peer < 0 || laneState(ad) == 7 || laneState(ad) == 0) {
            *errRouter = r;
            *code = TBT_EV_ERR_CONN;
            *errAdp = a;
            return -1;
        }
        if ((ad->Cs[4] & 0x80000000UL) != 0) {
            *errRouter = r;
            *code = TBT_EV_ERR_LOCK;
            *errAdp = a;
            return -1;
        }
        r = ad->Peer;
    }
    return r;
}

static void fabNotify(FAB *f, int r, unsigned long code, unsigned long adp)
{
    unsigned long dw[3];

    fabRouteOf(f, r, &dw[0], &dw[1]);
    dw[0] |= 0x80000000UL;
    dw[2] = code | ((adp & 0x3FUL) << 8);
    f->Notifications++;
    fabQueue(f, TBT_PDF_NOTIFY, dw, 3, 20);
}

static void fabHotPlug(FAB *f, int r, unsigned long adp, int unplug)
{
    unsigned long dw[3];

    fabRouteOf(f, r, &dw[0], &dw[1]);
    dw[0] |= 0x80000000UL;
    dw[2] = adp | (unplug ? 0x80000000UL : 0);
    fabQueue(f, TBT_PDF_HOTPLUG, dw, 3, 30);
}

/* ---------------------------------------------------------------------- */
/* Register semantics                                                     */
/* ---------------------------------------------------------------------- */

static int fabPcieAdp(const FAB_ROUTER *r, unsigned a)
{
    unsigned long t = r->Adp[a].Cs[2] & 0xFFFFFFUL;
    return t == 0x100101UL || t == 0x100102UL;
}

static int fabUsb3Adp(const FAB_ROUTER *r, unsigned a)
{
    unsigned long t = r->Adp[a].Cs[2] & 0xFFFFFFUL;
    return t == 0x200101UL || t == 0x200102UL;
}

static int fabPortOfLane(const FAB *f, unsigned lane)
{
    unsigned a;
    int k = 0;

    for (a = 1; a <= f->R[0].MaxAdp; a++) {
        if (f->R[0].Adp[a].Present && (f->R[0].Adp[a].Cs[2] & 0xFFFFFFUL) == 1 &&
            f->R[0].Adp[a].Cs[FAB_CAP_PORT] != 0) {
            k++;
            if (a == lane) {
                return k;
            }
        }
    }
    return 0;
}

static int fabNthOf(const FAB *f, int port, unsigned long type)
{
    unsigned a;
    int k = 0;

    for (a = 1; a <= f->R[0].MaxAdp; a++) {
        if (f->R[0].Adp[a].Present && (f->R[0].Adp[a].Cs[2] & 0xFFFFFFUL) == type) {
            if (++k == port) {
                return (int)a;
            }
        }
    }
    return 0;
}

/* The far end of a PCIe/USB3 protocol adapter across a host port: the
 * Nth down adapter of a type belongs to the Nth USB4 port (Spec 5.2.5),
 * and the device's first up adapter of the type is its peer. */
static int fabPeerProto(FAB *f, int r, unsigned a, int *pr, unsigned *pa)
{
    unsigned long t;
    unsigned long want;
    unsigned i;
    int port;
    int k;
    int lane;
    int other;

    t = f->R[r].Adp[a].Cs[2] & 0xFFFFFFUL;
    want = (t & ~0xFFUL) | ((t & 0xFFUL) == 1 ? 2UL : 1UL);
    if (r == 0) {
        port = 0;
        for (i = 1; i <= a; i++) {
            if ((f->R[0].Adp[i].Cs[2] & 0xFFFFFFUL) == t) {
                port++;
            }
        }
        lane = 0;
        k = 0;
        for (i = 1; i <= f->R[0].MaxAdp; i++) {
            if (f->R[0].Adp[i].Present && (f->R[0].Adp[i].Cs[2] & 0xFFFFFFUL) == 1 &&
                f->R[0].Adp[i].Cs[FAB_CAP_PORT] != 0 && ++k == port) {
                lane = (int)i;
                break;
            }
        }
        if (lane == 0) {
            return 0;
        }
        other = f->R[0].Adp[lane].Peer;
        if (other < 0) {
            return 0;
        }
        for (i = 1; i <= f->R[other].MaxAdp; i++) {
            if (f->R[other].Adp[i].Present &&
                (f->R[other].Adp[i].Cs[2] & 0xFFFFFFUL) == want) {
                *pr = other;
                *pa = i;
                return 1;
            }
        }
        return 0;
    }
    if (f->R[r].Parent != 0 || !f->R[r].Connected) {
        return 0;
    }
    port = fabPortOfLane(f, (unsigned)f->R[r].ParentAdp);
    k = fabNthOf(f, port, want);
    if (k == 0) {
        return 0;
    }
    /* Only the device's first adapter of the type pairs. */
    for (i = 1; i <= f->R[r].MaxAdp; i++) {
        if (f->R[r].Adp[i].Present && (f->R[r].Adp[i].Cs[2] & 0xFFFFFFUL) == t) {
            if (i != a) {
                return 0;
            }
            break;
        }
    }
    *pr = 0;
    *pa = (unsigned)k;
    return 1;
}

/* LTSSM follows Path Enable at both ends (a model: L0 when both are set,
 * Detect otherwise, unless stuck). */
static void fabPcieUpdate(FAB *f, int r, unsigned a)
{
    int pr;
    unsigned pa;
    unsigned long *c;
    unsigned long *o;
    int both;

    if (f->R[r].Adp[a].LtssmForce != 0) {
        c = &f->R[r].Adp[a].Cs[FAB_CAP_ADP];
        *c = (*c & ~(0xFUL << 25)) |
             ((unsigned long)(f->R[r].Adp[a].LtssmForce - 1) << 25);
        return;
    }
    if (!fabPeerProto(f, r, a, &pr, &pa)) {
        c = &f->R[r].Adp[a].Cs[FAB_CAP_ADP];
        *c = (*c & ~(0xFUL << 25)) | (f->R[r].Adp[a].DetectStuck ? (1UL << 25) : 0);
        return;
    }
    c = &f->R[r].Adp[a].Cs[FAB_CAP_ADP];
    o = &f->R[pr].Adp[pa].Cs[FAB_CAP_ADP];
    both = (*c & 0x80000000UL) && (*o & 0x80000000UL);
    *c = (*c & ~(0xFUL << 25)) |
         ((both ? 6UL : (f->R[r].Adp[a].DetectStuck ? 1UL : 0UL)) << 25);
    *o = (*o & ~(0xFUL << 25)) |
         ((both ? 6UL : (f->R[pr].Adp[pa].DetectStuck ? 1UL : 0UL)) << 25);
}

static void fabRouterOp(FAB *f, int ri)
{
    FAB_ROUTER *r = &f->R[ri];
    unsigned long op = r->Rcs[26] & 0xFFFFUL;
    int slot;
    int mode;
    unsigned long meta = r->Rcs[25];
    unsigned long addr;
    unsigned long n;
    unsigned long i;

    switch (op) {
    case 0x24: slot = FAB_OP_DROM; break;
    case 0x30: slot = FAB_OP_MAP; break;
    case 0x31: slot = FAB_OP_CAPS; break;
    case 0x33: slot = FAB_OP_BA; break;
    default: slot = -1; break;
    }
    mode = slot < 0 ? FAB_ROP_NS : r->RopMode[slot];
    if (mode == FAB_ROP_HANG) {
        return;     /* OV stays set */
    }
    if (mode == FAB_ROP_NS) {
        r->Rcs[26] = (r->Rcs[26] & 0xFFFFUL) | 0x40000000UL;
        return;
    }
    if (mode == FAB_ROP_FAIL) {
        r->Rcs[26] = (r->Rcs[26] & 0xFFFFUL) | (1UL << 24);
        return;
    }
    switch (slot) {
    case FAB_OP_DROM:
        addr = ((meta >> 2) & 0x1FFFUL) * 4;
        n = (meta >> 15) & 0x1FUL;
        for (i = 0; i < 16; i++) {
            r->Rcs[9 + i] = 0;
        }
        for (i = 0; i < 4 * n && addr + i < sizeof(r->Drom); i++) {
            r->Rcs[9 + i / 4] |= (unsigned long)r->Drom[addr + i] << (8 * (i % 4));
        }
        break;
    case FAB_OP_MAP:
        if (r->MapEntries == 0) {
            r->Rcs[26] = (r->Rcs[26] & 0xFFFFUL) | 0x40000000UL;
            return;
        }
        r->Rcs[25] = r->MapEntries | ((unsigned long)r->MapNext << 8);
        r->Rcs[9] = r->MapData[r->MapNext][0];
        r->Rcs[10] = r->MapData[r->MapNext][1];
        r->MapNext = (r->MapNext + 1) % r->MapEntries;
        break;
    case FAB_OP_CAPS:
        r->Rcs[25] = 5;
        r->Rcs[9] = r->CapsData;
        break;
    case FAB_OP_BA:
        r->BaLastMeta = meta;
        r->Rcs[25] = r->BaLen | ((meta & 0x3FUL) << 8);
        for (i = 0; i < r->BaLen; i++) {
            r->Rcs[9 + i] = r->BaData[i];
            if ((r->BaData[i] & 0xFFFFUL) == 4 && (meta & 0x3FUL) < FAB_MAX_ADP &&
                r->BaPerLane[meta & 0x3FUL] != 0) {
                r->Rcs[9 + i] = 4UL | (r->BaPerLane[meta & 0x3FUL] << 16);
            }
        }
        break;
    }
    r->Rcs[26] &= 0xFFFFUL;
}

static void fabEnumerated(FAB *f, int ri)
{
    FAB_ROUTER *r = &f->R[ri];
    unsigned a;

    if (!r->Tbt3 && !r->NoRouterReady) {
        r->Rcs[6] |= 0x01000000UL;
    }
    if (r->DpPlugEvents) {
        for (a = 1; a <= r->MaxAdp; a++) {
            if (r->Adp[a].Present && (r->Adp[a].Cs[2] & 0xFFFFFFUL) == 0x0E0101UL) {
                fabHotPlug(f, ri, a, 0);
            }
        }
    }
}

static void fabRouterWrite(FAB *f, int ri, unsigned long addr, unsigned long v)
{
    FAB_ROUTER *r = &f->R[ri];
    unsigned long old = r->Rcs[addr];

    if (addr == 0 || addr == 6 || addr == 7 || addr == 8) {
        return;     /* read only */
    }
    if (addr == 1) {
        r->Rcs[1] = (old & ~(7UL << 20)) | (v & (7UL << 20));
        return;
    }
    if (addr == 4) {
        r->Rcs[4] = (old & 0xFF000000UL) | (v & 0x0000FFFFUL);
        return;
    }
    r->Rcs[addr] = v;
    if (addr == 3 && (v & 0x80000000UL) && !(old & 0x80000000UL)) {
        fabEnumerated(f, ri);
    }
    if (addr == 5 && (v & 0x80000000UL) && !r->NoConfigReady) {
        r->Rcs[6] |= 0x02000000UL;
    }
    if (addr == 26 && (v & 0x80000000UL)) {
        fabRouterOp(f, ri);
    }
    if (!r->Tbt3 && addr == FAB_RCAP_TMU + 25 && !r->PostStuck) {
        r->Rcs[FAB_RCAP_TMU + 24] = 0;
        r->Rcs[FAB_RCAP_TMU + 25] = 0;
    }
}

static void fabAdapterWrite(FAB *f, int ri, unsigned a, unsigned long addr,
                            unsigned long v)
{
    FAB_ROUTER *r = &f->R[ri];
    FAB_ADP *ad = &r->Adp[a];
    unsigned long old = ad->Cs[addr];
    int pr;
    unsigned pa;
    int d;
    unsigned long t;

    t = ad->Cs[2] & 0xFFFFFFUL;
    if (addr <= 3 || (t == 1 && addr == FAB_CAP_LANE)) {
        return;
    }
    if (addr == 4) {
        if (t == 1 && (ad->Cs[FAB_CAP_LANE] & 0xFFUL) != FAB_CAP_PORT) {
            f->Lane1LockWrites++;
        }
        ad->Cs[4] = (old & ~0x800003FFUL) | (v & 0x800003FFUL);
        return;
    }
    if (addr == 5) {
        ad->Cs[5] = (old & 0x0003F87FUL) | (v & ~0x0003F87FUL);
        return;
    }
    if (t == 1 && addr == FAB_CAP_LANE + 1) {
        /* Lane Adapter Configuration Capability: R/W fields 15:0, 30. */
        ad->Cs[addr] = (old & ~0x4000FFFFUL) | (v & 0x4000FFFFUL);
        if ((v & 0x4000UL) && laneState(ad) == 2 && !r->Lane1NoDisable) {
            setLaneState(ad, 0);    /* Lane Disable: Disabled */
        }
        if (!(v & 0x4000UL) && (old & 0x4000UL) && ad->Peer >= 0) {
            setLaneState(ad, 2);
        }
        if ((v & 0x8000UL) && ad->Peer >= 0) {
            /* Lane Bonding on lane 0: x2 on both lanes at both ends, LB
             * cleared, lane 1 reports a hot unplug (the emulator). */
            ad->Cs[addr] &= ~0x8000UL;
            d = ad->Peer;
            if (r->BondPossible && f->R[d].BondPossible && !f->R[d].BondNoWidth &&
                (ad->Cs[addr] & 0x30UL) == 0x30UL &&
                (r->Adp[a + 1].Cs[FAB_CAP_LANE + 1] & 0x30UL) == 0x30UL &&
                (f->R[d].Adp[ad->PeerAdp].Cs[FAB_CAP_LANE + 1] & 0x30UL) == 0x30UL &&
                (f->R[d].Adp[ad->PeerAdp + 1].Cs[FAB_CAP_LANE + 1] & 0x30UL) == 0x30UL) {
                setLaneWidth(ad, 2);
                setLaneWidth(&r->Adp[a + 1], 2);
                setLaneWidth(&f->R[d].Adp[ad->PeerAdp], 2);
                setLaneWidth(&f->R[d].Adp[ad->PeerAdp + 1], 2);
                if ((ad->Cs[5] & 0x80000000UL) == 0) {
                    fabHotPlug(f, ri, a + 1, 1);
                }
                /* Total Buffers doubles on a bonded link (Spec 5.3.2.1). */
                ad->Cs[4] = (ad->Cs[4] & ~(0x3FFUL << 20)) | (120UL << 20);
                f->R[d].Adp[ad->PeerAdp].Cs[4] =
                    (f->R[d].Adp[ad->PeerAdp].Cs[4] & ~(0x3FFUL << 20)) | (120UL << 20);
            }
        }
        return;
    }
    if (t == 1 && addr == FAB_CAP_PORT + 19) {
        ad->Cs[addr] = v;
        if ((v & 1UL) && ad->Peer >= 0) {
            /* Downstream Port Reset: the router behind goes back to the
             * uninitialised state and its link retrains. */
            d = ad->Peer;
            f->R[d].Rcs[3] &= ~0x80000000UL;
            f->R[d].Rcs[5] = 0;
            f->R[d].Rcs[6] &= ~0x03000000UL;
            f->R[d].Rcs[1] &= ~(7UL << 20);
            for (pa = 1; pa <= f->R[d].MaxAdp; pa++) {
                memset(f->R[d].Adp[pa].Path + 16, 0, sizeof(f->R[d].Adp[pa].Path) - 64);
                f->R[d].Adp[pa].Cs[FAB_CAP_ADP] &= ~0xC0000000UL;
            }
            ad->Cs[4] |= 0x80000000UL;
            r->Adp[a + 1].Cs[4] |= 0x80000000UL;
        }
        return;
    }
    if ((fabPcieAdp(r, a) || fabUsb3Adp(r, a)) && addr == FAB_CAP_ADP) {
        if (ad->PeStuck && (old & 0x80000000UL) && !(v & 0x80000000UL)) {
            v |= 0x80000000UL;
        }
        ad->Cs[addr] = (old & 0x1FFFFFFFUL & ~(0xFUL << 25)) | (v & 0xC0000000UL) |
                       (old & (0xFUL << 25));
        if (fabPcieAdp(r, a)) {
            fabPcieUpdate(f, ri, a);
        }
        return;
    }
    if (fabUsb3Adp(r, a) && addr == FAB_CAP_ADP + 2) {
        ad->Cs[addr] = v;
        if (!ad->HcaStuck) {
            ad->Cs[FAB_CAP_ADP + 1] = (ad->Cs[FAB_CAP_ADP + 1] & ~0x80000000UL) |
                                      (v & 0x80000000UL) | ad->Consumed;
        }
        return;
    }
    if (fabUsb3Adp(r, a) && (addr == FAB_CAP_ADP + 1 || addr == FAB_CAP_ADP + 4)) {
        return;
    }
    (void)pr;
    ad->Cs[addr] = v;
}

static void fabPathWrite(FAB *f, int ri, unsigned a, unsigned long addr,
                         unsigned long v)
{
    FAB_ADP *ad = &f->R[ri].Adp[a];
    unsigned hop = (unsigned)(addr / 2);

    if ((addr & 1) == 0) {
        if ((ad->Path[addr] & 0x80000000UL) && !(v & 0x80000000UL)) {
            /* Valid cleared: drains at once unless stuck (Spec 5.4.2). */
            ad->Path[addr + 1] = (ad->Path[addr + 1] & ~0x10000000UL) |
                                 (hop < FAB_HOPS && ad->PpStuck[hop] ? 0x10000000UL : 0);
        }
        ad->Path[addr] = v;
    } else {
        ad->Path[addr] = (v & ~0x10000000UL) | (ad->Path[addr] & 0x10000000UL);
    }
    (void)f;
}

static unsigned long fabRead(FAB *f, int ri, unsigned long cs, unsigned a,
                             unsigned long addr)
{
    FAB_ROUTER *r = &f->R[ri];

    if (cs == TBT_CS_ROUTER) {
        if (!r->Tbt3 && addr == FAB_RCAP_TMU + 1) {
            r->Rcs[addr] += 0x10000UL;      /* time moves */
        }
        return r->Rcs[addr];
    }
    if (cs == TBT_CS_ADAPTER) {
        if (fabPcieAdp(r, a) && addr == FAB_CAP_ADP) {
            fabPcieUpdate(f, ri, a);
        }
        return r->Adp[a].Cs[addr];
    }
    return r->Adp[a].Path[addr];
}

/* ---------------------------------------------------------------------- */
/* Requests                                                               */
/* ---------------------------------------------------------------------- */

static void fabRequest(FAB *f, unsigned long pdf, const unsigned long *dw,
                       unsigned long words)
{
    unsigned long dw3 = dw[2];
    unsigned long addr = dw3 & 0x1FFFUL;
    unsigned long n = (dw3 >> 13) & 0x3FUL;
    unsigned long adp = (dw3 >> 19) & 0x3FUL;
    unsigned long cs = (dw3 >> 25) & 3UL;
    unsigned long resp[64];
    unsigned long code;
    unsigned long errAdp;
    unsigned long limit;
    unsigned long i;
    int errRouter;
    int r;
    int drop;

    f->Requests++;
    if (f->Stopped) {
        return;
    }
    r = fabResolve(f, dw[0] & 0xFFFFFFUL, dw[1], &errRouter, &code, &errAdp);
    drop = 0;
    if (f->Hook != NULL) {
        drop = f->Hook(f, pdf, r, cs, adp, addr, n, pdf == TBT_PDF_WRITE ? dw + 3 : NULL);
        if (drop == FAB_HOOK_SWALLOW) {
            return;
        }
    }
    r = fabResolve(f, dw[0] & 0xFFFFFFUL, dw[1], &errRouter, &code, &errAdp);
    if (r < 0) {
        fabNotify(f, errRouter, code, errAdp);
        return;
    }
    if (cs == TBT_CS_ROUTER) {
        limit = FAB_RTR_DW;
    } else if (adp == 0 || adp > f->R[r].MaxAdp || adp >= FAB_MAX_ADP ||
               !f->R[r].Adp[adp].Present) {
        fabNotify(f, r, TBT_EV_ERR_ADDR, adp);
        return;
    } else if (cs == TBT_CS_ADAPTER) {
        limit = FAB_ADP_DW;
    } else if (cs == TBT_CS_PATH) {
        limit = 2 * FAB_HOPS;
    } else {
        fabNotify(f, r, TBT_EV_ERR_ADDR, adp);
        return;
    }
    if (n == 0 || addr + n > limit || (pdf == TBT_PDF_WRITE && words < 3 + n)) {
        fabNotify(f, r, TBT_EV_ERR_ADDR, adp);
        return;
    }
    fabRouteOf(f, r, &resp[0], &resp[1]);
    resp[0] |= 0x80000000UL;
    resp[2] = dw3;
    if (cs == TBT_CS_ROUTER) {
        /* Spec Table 6-3, and usb4-emu for writes too: the arrival adapter. */
        resp[2] = (dw3 & ~(0x3FUL << 19)) | ((unsigned long)f->R[r].UpAdp << 19);
    }
    if (pdf == TBT_PDF_READ) {
        f->Reads++;
        for (i = 0; i < n; i++) {
            resp[3 + i] = fabRead(f, r, cs, (unsigned)adp, addr + i);
        }
        fabQueue(f, TBT_PDF_READ, resp, 3 + n, 40);
    } else {
        f->Writes++;
        for (i = 0; i < n; i++) {
            if (f->WrCount < FAB_WRLOG) {
                f->WrRouter[f->WrCount] = r;
                f->WrCs[f->WrCount] = cs;
                f->WrAdp[f->WrCount] = adp;
                f->WrAddr[f->WrCount] = addr + i;
                f->WrVal[f->WrCount] = dw[3 + i];
                f->WrAtUs[f->WrCount] = f->NowUs;
            }
            f->WrCount++;
            if (cs == TBT_CS_ROUTER) {
                fabRouterWrite(f, r, addr + i, dw[3 + i]);
            } else if (cs == TBT_CS_ADAPTER) {
                fabAdapterWrite(f, r, (unsigned)adp, addr + i, dw[3 + i]);
            } else {
                fabPathWrite(f, r, (unsigned)adp, addr + i, dw[3 + i]);
            }
        }
        if (drop != FAB_HOOK_DROP_ANSWER) {
            fabQueue(f, TBT_PDF_WRITE, resp, 3, 40);
        }
    }
}

static void fabTx(FAB *f, unsigned long pdf, const unsigned long *dw,
                  unsigned long words)
{
    if (words < 2) {
        return;
    }
    switch (pdf) {
    case TBT_PDF_READ:
    case TBT_PDF_WRITE:
        if (words >= 3) {
            fabRequest(f, pdf, dw, words);
        }
        break;
    case TBT_PDF_NOTIFY:
        if (words >= 3 && (dw[2] & 0xFFUL) == TBT_EV_HP_ACK) {
            f->HpAcks++;
        }
        break;
    case TBT_PDF_NOTIFY_ACK:
        f->AcksPlain++;
        break;
    case TBT_PDF_NOTIFY_ACK_ENH:
        f->AcksEnh++;
        break;
    }
}

static void fabProcessTx(FAB *f)
{
    unsigned char *d;
    unsigned char *buf;
    unsigned long dw2;
    unsigned long len;
    unsigned long words;
    unsigned long i;
    unsigned long dw[64];

    while ((f->TxCtrl & 0x80000000UL) && f->TxSize != 0 && f->TxCons != f->TxProd) {
        d = fabAt(f, f->TxBaseLo + 16 * f->TxCons);
        if (d == NULL) {
            break;
        }
        dw2 = getLe(d + 8);
        len = dw2 & 0xFFFUL;
        buf = fabAt(f, getLe(d));
        putLe(d + 8, dw2 | 0x00200000UL);
        f->TxCons = (f->TxCons + 1) % f->TxSize;
        if (buf == NULL || len < 8 || (len & 3) != 0) {
            continue;
        }
        words = len / 4 - 1;
        for (i = 0; i < words && i < 64; i++) {
            dw[i] = getBe(buf + 4 * i);
        }
        if (fabCrc(buf, len - 4) != getBe(buf + len - 4)) {
            f->TxBadCrc++;
            continue;
        }
        f->TxCount++;
        fabTx(f, (dw2 >> 12) & 0xFUL, dw, words);
    }
    fabPump(f);
}

/* ---------------------------------------------------------------------- */
/* The accessor                                                           */
/* ---------------------------------------------------------------------- */

static ULONG fabRead32(PVOID ctx, ULONG off)
{
    FAB *f = (FAB *)ctx;

    fabProcessTx(f);
    switch (off) {
    case 0x39640: return f->Caps;
    case 0x39898:
        if (f->HrReset && f->NowUs - f->HrResetAtUs >= 20000UL) {
            f->HrReset = 0;
        }
        return f->HrReset;
    case 0x00008: return (f->TxProd << 16) | f->TxCons;
    case 0x0000C: return f->TxSize;
    case 0x19800: return f->TxCtrl;
    case 0x08008: return (f->RxProd << 16) | f->RxCons;
    case 0x0800C: return f->RxSizeReg;
    case 0x29800: return f->RxCtrl;
    }
    return 0;
}

static void fabWrite32(PVOID ctx, ULONG off, ULONG v)
{
    FAB *f = (FAB *)ctx;

    switch (off) {
    case 0x00000: f->TxBaseLo = v; f->TxProd = f->TxCons = 0; return;
    case 0x00004: f->TxProd = f->TxCons = 0; return;
    case 0x00008: f->TxProd = (v >> 16) & 0xFFFFUL; fabProcessTx(f); return;
    case 0x0000C: f->TxSize = v & 0xFFFFUL; f->TxProd = f->TxCons = 0; return;
    case 0x19800: f->TxCtrl = v; return;
    case 0x08000: f->RxBaseLo = v; f->RxProd = f->RxCons = 0; return;
    case 0x08004: f->RxProd = f->RxCons = 0; return;
    case 0x08008: f->RxCons = v & 0xFFFFUL; fabPump(f); return;
    case 0x0800C: f->RxSizeReg = v; f->RxSize = v & 0xFFFFUL; f->RxProd = f->RxCons = 0; return;
    case 0x29800: f->RxCtrl = v; return;
    case 0x39898:
        if (v & 1UL) {
            f->HrReset = 1;
            f->HrResetAtUs = f->NowUs;
            f->HrResets++;
        }
        return;
    }
    if (off >= 0x38200 && off < 0x38210) {
        f->Imr[(off - 0x38200) / 4 % 4] = v;
    }
}

static void fabAdvance(FAB *f, unsigned long us)
{
    f->NowUs += us;
    fabProcessTx(f);
    fabPump(f);
}

static void fabStall(PVOID ctx, ULONG us)
{
    fabAdvance((FAB *)ctx, us);
}

static void fabSleep(PVOID ctx, ULONG ms)
{
    FAB *f = (FAB *)ctx;
    unsigned long us = ms * 1000UL;

    /* A tick-quantised sleep overshoots to the next tick (record 22
     * section 5.3). */
    if (f->TickUs != 0) {
        us = ((f->NowUs + us + f->TickUs - 1) / f->TickUs) * f->TickUs - f->NowUs;
    }
    fabAdvance(f, us);
}

static ULONG fabNowMs(PVOID ctx)
{
    return ((FAB *)ctx)->NowUs / 1000UL;
}

static ULONG fabClockUs(PVOID ctx)
{
    FAB *f = (FAB *)ctx;

    if (f->ClockFrozen) {
        return f->FrozenUs;
    }
    if (f->TickUs != 0) {
        return (f->NowUs / f->TickUs) * f->TickUs;
    }
    return f->NowUs;
}

static void fabLog(PVOID ctx, const char *label, ULONG value)
{
    FAB *f = (FAB *)ctx;

    f->LogCount++;
    f->LastLabel = label;
    f->LastValue = value;
    if (f->Verbose) {
        printf("    [log] %s %08lx\n", label, (unsigned long)value);
    }
}

/* ---------------------------------------------------------------------- */
/* Building                                                               */
/* ---------------------------------------------------------------------- */

void fabInit(FAB *f)
{
    int i;

    memset(f, 0, sizeof(*f));
    f->Host.Context = f;
    f->Host.Read32 = fabRead32;
    f->Host.Write32 = fabWrite32;
    f->Host.Stall = fabStall;
    f->Host.Sleep = fabSleep;
    f->Host.NowMs = fabNowMs;
    f->Host.Log = fabLog;
    f->Host.ClockUs = fabClockUs;
    f->Host.TickUs = 0;
    f->Caps = 0x0000000CUL;
    for (i = 0; i < FAB_MAX_ROUTERS; i++) {
        f->R[i].Parent = -1;
    }
}

static void fabAdpNew(FAB_ROUTER *r, unsigned a, unsigned long type, int maxIn)
{
    FAB_ADP *ad = &r->Adp[a];
    unsigned h;

    memset(ad, 0, sizeof(*ad));
    ad->Present = 1;
    ad->Peer = -1;
    ad->Cs[2] = type;
    ad->Cs[3] = (unsigned long)a << 20;
    ad->Cs[5] = (unsigned long)maxIn | ((unsigned long)maxIn << 11);
    if (a > r->MaxAdp) {
        r->MaxAdp = a;
    }
    for (h = 0; h < FAB_HOPS; h++) {
        ad->Path[2 * h + 1] = 0;
    }
}

/* A lane adapter: Lane capability, then (USB4) the USB4 Port capability,
 * then the TMU adapter capability. Lock set, as after power on. */
static void fabLane(FAB_ROUTER *r, unsigned a, int usb4Port, int tmu)
{
    FAB_ADP *ad;

    fabAdpNew(r, a, 0x000001UL, 23);
    ad = &r->Adp[a];
    ad->Cs[1] = FAB_CAP_LANE;
    ad->Cs[4] = 0x80000000UL | (60UL << 20) | 0;       /* Lock, Total Buffers 60 */
    ad->Cs[5] |= 0;                                     /* LCA 0, DHP 0 */
    ad->Cs[FAB_CAP_LANE] = (0x01UL << 8) | (usb4Port ? FAB_CAP_PORT : (tmu ? FAB_CAP_TMU_ADP : 0));
    ad->Cs[FAB_CAP_LANE + 0] |= (0xEUL << 16) | (3UL << 20);  /* speeds, x1 and x2 */
    ad->Cs[FAB_CAP_LANE + 1] = 0x10UL | (0x4UL << 16) | (1UL << 20) | (7UL << 26);
    if (usb4Port) {
        ad->Cs[FAB_CAP_PORT] = (0x06UL << 8) | (tmu ? FAB_CAP_TMU_ADP : 0);
        ad->Cs[FAB_CAP_PORT + 18] = 0;
        ad->Cs[FAB_CAP_PORT + 19] = 0;
    }
    if (tmu) {
        ad->Cs[FAB_CAP_TMU_ADP] = 0x03UL << 8;
        ad->Cs[FAB_CAP_TMU_ADP + 6] = 0;
    }
    /* The control path's credits in the HopID 0 entry. */
    ad->Path[0] = 2UL << 17;
}

static void fabProto(FAB_ROUTER *r, unsigned a, unsigned long type)
{
    FAB_ADP *ad;

    fabAdpNew(r, a, type, 8);
    ad = &r->Adp[a];
    ad->Cs[1] = FAB_CAP_ADP;
    ad->Cs[FAB_CAP_ADP] = 0x04UL << 8;
    if ((type >> 16) == 0x20) {
        ad->Cs[FAB_CAP_ADP + 3] = 0;
        ad->Cs[FAB_CAP_ADP + 4] = 1UL << 12;     /* MSLR 20G */
        ad->Consumed = 0x064064UL;              /* 100 each way, scale 0 */
    }
    /* Protocol adapters report their own ingress credits. */
    ad->Path[2 * 8] = 7UL << 17;
    ad->Path[2 * 8 + 1] = 0x01000000UL;        /* IFC as the router left it */
}

static void fabRouterBase(FAB_ROUTER *r, unsigned long vendor, unsigned long device,
                          unsigned long version)
{
    r->Used = 1;
    r->Rcs[0] = vendor | (device << 16);
    r->Rcs[4] = version << 24;
    r->BondPossible = 1;
    r->Lane1Present = 1;
}

static void fabTmuRouter(FAB_ROUTER *r)
{
    r->Rcs[1] |= FAB_RCAP_TMU;
    r->Rcs[FAB_RCAP_TMU] = 0x03UL << 8;
}

static void fabFinish(FAB_ROUTER *r)
{
    r->Rcs[1] = (r->Rcs[1] & ~((0x3FUL << 8) | (0x3FUL << 14))) |
                ((unsigned long)r->UpAdp << 8) | ((unsigned long)r->MaxAdp << 14);
}

int fabHostMinimal(FAB *f)
{
    FAB_ROUTER *h = &f->R[0];

    memset(h, 0, sizeof(*h));
    h->Parent = -1;
    fabRouterBase(h, 0x46F4, 0x0001, 0x20);
    fabTmuRouter(h);
    fabLane(h, 1, 1, 1);
    fabLane(h, 2, 0, 1);
    fabLane(h, 3, 1, 1);
    fabLane(h, 4, 0, 1);
    fabProto(h, 5, 0x100101UL);
    fabProto(h, 6, 0x100101UL);
    fabProto(h, 7, 0x200101UL);
    fabProto(h, 8, 0x200101UL);
    fabAdpNew(h, 9, 0x000002UL, 12);
    h->UpAdp = 0;
    fabFinish(h);
    h->RopMode[FAB_OP_DROM] = FAB_ROP_NS;
    h->RopMode[FAB_OP_MAP] = FAB_ROP_OK;
    h->RopMode[FAB_OP_CAPS] = FAB_ROP_OK;
    h->RopMode[FAB_OP_BA] = FAB_ROP_OK;
    h->MapEntries = 2;
    h->MapData[0][0] = 5UL << 1;
    h->MapData[0][1] = 0x0108UL;    /* bus 1, device 1 */
    h->MapData[1][0] = 6UL << 1;
    h->MapData[1][1] = 0x0110UL;    /* bus 1, device 2 */
    h->BaLen = 2;
    h->BaData[0] = 4UL | (32UL << 16);
    h->BaData[1] = 1UL | (16UL << 16);
    return 0;
}

int fabHostAdl(FAB *f)
{
    FAB_ROUTER *h = &f->R[0];

    memset(h, 0, sizeof(*h));
    h->Parent = -1;
    fabRouterBase(h, 0x8087, 0x463E, 0x20);
    fabTmuRouter(h);
    fabLane(h, 1, 1, 1);
    fabLane(h, 2, 0, 1);
    fabLane(h, 3, 1, 1);
    fabLane(h, 4, 0, 1);
    fabProto(h, 5, 0x0E0101UL);
    fabProto(h, 6, 0x0E0101UL);
    fabAdpNew(h, 7, 0x000002UL, 12);
    fabProto(h, 8, 0x100101UL);
    fabProto(h, 9, 0x100101UL);
    fabProto(h, 12, 0x200101UL);
    fabProto(h, 13, 0x200101UL);
    h->UpAdp = 0;
    fabFinish(h);
    h->RopMode[FAB_OP_DROM] = FAB_ROP_OK;
    h->RopMode[FAB_OP_MAP] = FAB_ROP_NS;
    h->RopMode[FAB_OP_CAPS] = FAB_ROP_NS;
    h->RopMode[FAB_OP_BA] = FAB_ROP_OK;
    h->BaLen = 2;
    h->BaData[0] = 4UL | (28UL << 16);
    h->BaData[1] = 1UL | (14UL << 16);
    h->DpPlugEvents = 1;
    return 0;
}

static void fabDrom(FAB_ROUTER *r, const char *vendor, const char *model)
{
    unsigned char *d = r->Drom;
    unsigned pos;
    unsigned n;
    unsigned long crc;
    unsigned len;

    memset(d, 0, sizeof(r->Drom));
    d[13] = 3;
    pos = 16;
    n = (unsigned)strlen(vendor) + 1;
    d[pos] = (unsigned char)(n + 2);
    d[pos + 1] = 1;
    memcpy(d + pos + 2, vendor, n);
    pos += n + 2;
    n = (unsigned)strlen(model) + 1;
    d[pos] = (unsigned char)(n + 2);
    d[pos + 1] = 2;
    memcpy(d + pos + 2, model, n);
    pos += n + 2;
    len = pos;
    d[14] = (unsigned char)((len - 13) & 0xFF);
    d[15] = (unsigned char)(((len - 13) >> 8) & 0x0F);
    crc = fabCrc(d + 13, len - 13);
    d[9] = (unsigned char)crc;
    d[10] = (unsigned char)(crc >> 8);
    d[11] = (unsigned char)(crc >> 16);
    d[12] = (unsigned char)(crc >> 24);
    r->DromLen = len;
}

int fabDevice(FAB *f, int kind)
{
    int i;
    FAB_ROUTER *d;

    for (i = 1; i < FAB_MAX_ROUTERS; i++) {
        if (!f->R[i].Used) {
            break;
        }
    }
    d = &f->R[i];
    memset(d, 0, sizeof(*d));
    d->Parent = -1;
    d->Kind = kind;
    if (kind == FAB_DEV_USB4) {
        fabRouterBase(d, 0x46F4, 0x4242, 0x20);
        fabTmuRouter(d);
        fabLane(d, 1, 1, 1);
        fabLane(d, 2, 0, 1);
        fabLane(d, 3, 1, 1);
        fabLane(d, 4, 0, 1);
        fabProto(d, 5, 0x100102UL);
        fabProto(d, 6, 0x200102UL);
        fabProto(d, 7, 0x100101UL);
        d->UpAdp = 1;
        d->Adp[1].Cs[FAB_CAP_PORT + 18] = 0x100UL;      /* BE */
        d->Rcs[7] = 0x11223344UL + (unsigned long)i;
        d->Rcs[8] = 0x55667788UL;
        d->RopMode[FAB_OP_DROM] = FAB_ROP_OK;
        d->RopMode[FAB_OP_MAP] = FAB_ROP_OK;
        d->RopMode[FAB_OP_CAPS] = FAB_ROP_OK;
        d->RopMode[FAB_OP_BA] = FAB_ROP_OK;
        d->BaLen = 2;
        d->BaData[0] = 4UL | (32UL << 16);
        d->BaData[1] = 1UL | (16UL << 16);
        fabDrom(d, "QEMU", "USB4 NVMe Enclosure");
    } else {
        d->Tbt3 = 1;
        fabRouterBase(d, 0x8086, kind == FAB_DEV_TBT3_TR ? 0x15EF : 0x15C0, 0x00);
        d->Rcs[1] |= FAB_RCAP_PLUG;
        d->Rcs[FAB_RCAP_PLUG] = FAB_RCAP_LC | (0x05UL << 8) | (0x01UL << 16) | (0x0CUL << 24);
        d->Rcs[FAB_RCAP_PLUG + 1] = 0x7CUL;
        d->Rcs[FAB_RCAP_LC] = (0x05UL << 8) | (0x06UL << 16);
        d->Rcs[FAB_RCAP_LC + 1] = 0;
        d->Rcs[FAB_RCAP_LC + 2] = 2UL | ((unsigned long)FAB_LC_START << 8) |
                                  ((unsigned long)FAB_LC_PORT_SIZE << 16);
        d->Rcs[FAB_RCAP_LC + 3] = 0xAABB0000UL + (unsigned long)i;
        d->Rcs[FAB_RCAP_LC + 4] = 0x01020304UL;
        d->Rcs[FAB_RCAP_LC + 5] = 0x05060708UL;
        d->Rcs[FAB_RCAP_LC + 6] = 0x090A0B0CUL;
        d->Rcs[FAB_RCAP_LC + FAB_LC_START + 0x8D] = 0x1000UL;     /* BE, phys port 0 */
        fabLane(d, 1, 0, 0);
        fabLane(d, 2, 0, 0);
        fabProto(d, 3, 0x100102UL);
        d->UpAdp = 1;
    }
    fabFinish(d);
    return i;
}

int fabChild(FAB *f, int dev)
{
    (void)dev;
    return fabDevice(f, FAB_DEV_USB4);
}

/* ---------------------------------------------------------------------- */
/* Plug, unplug, pre-OS                                                   */
/* ---------------------------------------------------------------------- */

int fabPortLane0(const FAB *f, int port)
{
    unsigned a;
    int k = 0;

    for (a = 1; a <= f->R[0].MaxAdp; a++) {
        if (f->R[0].Adp[a].Present && (f->R[0].Adp[a].Cs[2] & 0xFFFFFFUL) == 1 &&
            f->R[0].Adp[a].Cs[FAB_CAP_PORT] != 0) {
            if (++k == port) {
                return (int)a;
            }
        }
    }
    return 0;
}

static int fabNth(const FAB *f, int port, unsigned long type)
{
    unsigned a;
    int k = 0;

    for (a = 1; a <= f->R[0].MaxAdp; a++) {
        if (f->R[0].Adp[a].Present && (f->R[0].Adp[a].Cs[2] & 0xFFFFFFUL) == type) {
            if (++k == port) {
                return (int)a;
            }
        }
    }
    return 0;
}

int fabPortPcie(const FAB *f, int port)
{
    return fabNth(f, port, 0x100101UL);
}

int fabPortUsb3(const FAB *f, int port)
{
    return fabNth(f, port, 0x200101UL);
}

int fabDevAt(const FAB *f, int port)
{
    int a = fabPortLane0(f, port);

    return a != 0 ? f->R[0].Adp[a].Peer : -1;
}

static void fabConnect(FAB *f, int pr, unsigned pa, int dev)
{
    FAB_ROUTER *p = &f->R[pr];
    FAB_ROUTER *d = &f->R[dev];
    unsigned da = d->UpAdp;
    int tcm = d->Tbt3;

    d->Connected = 1;
    d->Parent = pr;
    d->ParentAdp = (int)pa;
    p->Adp[pa].Peer = dev;
    p->Adp[pa].PeerAdp = (int)da;
    d->Adp[da].Peer = pr;
    d->Adp[da].PeerAdp = (int)pa;
    setLaneState(&p->Adp[pa], 2);
    setLaneState(&d->Adp[da], 2);
    setLaneWidth(&p->Adp[pa], 1);
    setLaneWidth(&d->Adp[da], 1);
    if (d->Lane1Present && p->Adp[pa + 1].Present && d->Adp[da + 1].Present) {
        p->Adp[pa + 1].Peer = dev;
        p->Adp[pa + 1].PeerAdp = (int)da + 1;
        d->Adp[da + 1].Peer = pr;
        d->Adp[da + 1].PeerAdp = (int)pa + 1;
        setLaneState(&p->Adp[pa + 1], 2);
        setLaneState(&d->Adp[da + 1], 2);
        setLaneWidth(&p->Adp[pa + 1], 1);
        setLaneWidth(&d->Adp[da + 1], 1);
    }
    if (p->Adp[pa].Cs[FAB_CAP_PORT] != 0) {
        p->Adp[pa].Cs[FAB_CAP_PORT + 18] = (p->BondPossible && d->BondPossible ? 0x100UL : 0) |
                                          (tcm ? 0x200UL : 0);
    }
}

void fabPlug(FAB *f, int port, int dev)
{
    int a = fabPortLane0(f, port);

    fabConnect(f, 0, (unsigned)a, dev);
    if ((f->R[0].Adp[a].Cs[5] & 0x80000000UL) == 0) {
        fabHotPlug(f, 0, (unsigned long)a, 0);
    }
}

/* A router that loses power comes back uninitialised (Spec 3.3): every
 * configuration a connection manager wrote is gone. */
static void fabResetRouter(FAB *f, int d)
{
    FAB_ROUTER *r = &f->R[d];
    unsigned a;
    unsigned h;

    r->Rcs[1] &= ~(7UL << 20);
    r->Rcs[2] = 0;
    r->Rcs[3] = 0;
    r->Rcs[4] &= 0xFF000000UL;
    r->Rcs[5] = 0;
    r->Rcs[6] &= ~0x03000000UL;
    r->Rcs[26] = 0;
    if (!r->Tbt3) {
        r->Rcs[FAB_RCAP_TMU] &= 0x0000FFFFUL;
        r->Rcs[FAB_RCAP_TMU + 3] = 0;
        r->Rcs[FAB_RCAP_TMU + 15] = 0;
        for (h = 22; h <= 25; h++) {
            r->Rcs[FAB_RCAP_TMU + h] = 0;
        }
    } else {
        r->Rcs[FAB_RCAP_PLUG + 1] = 0x7CUL;
    }
    for (a = 1; a <= r->MaxAdp; a++) {
        FAB_ADP *ad = &r->Adp[a];
        unsigned long t;

        if (!ad->Present) {
            continue;
        }
        for (h = 8; h < FAB_HOPS; h++) {
            ad->Path[2 * h] = 0;
            ad->Path[2 * h + 1] = 0;
        }
        t = ad->Cs[2] & 0xFFFFFFUL;
        if (t == 1) {
            ad->Cs[FAB_CAP_LANE + 1] = (ad->Cs[FAB_CAP_LANE + 1] & (0xFUL << 26)) |
                                       0x10UL | (0x4UL << 16);
            ad->Cs[4] = (ad->Cs[4] & ~(0x3FFUL << 20)) | (60UL << 20);
            if (ad->Cs[FAB_CAP_PORT] != 0) {
                ad->Cs[FAB_CAP_PORT + 19] = 0;
            }
            ad->Cs[FAB_CAP_TMU_ADP + 3] = 0;
            ad->Cs[FAB_CAP_TMU_ADP + 6] = 0;
        } else if (t != 2) {
            ad->Path[2 * 8] = 7UL << 17;
            ad->Path[2 * 8 + 1] = 0x01000000UL;
            ad->Cs[FAB_CAP_ADP] &= ~0xC0000000UL;
        }
    }
}

static void fabDisconnect(FAB *f, int pr, unsigned pa)
{
    FAB_ROUTER *p = &f->R[pr];
    int dev = p->Adp[pa].Peer;
    unsigned k;
    unsigned i;

    for (k = 0; k < 2; k++) {
        if (!p->Adp[pa + k].Present) {
            continue;
        }
        p->Adp[pa + k].Peer = -1;
        setLaneState(&p->Adp[pa + k], 7);
        setLaneWidth(&p->Adp[pa + k], 0);
        p->Adp[pa + k].Cs[4] |= 0x80000000UL;
        p->Adp[pa + k].Cs[FAB_CAP_LANE + 1] &= ~0x4000UL;
    }
    if (dev >= 0) {
        f->R[dev].Connected = 0;
        for (i = 1; i <= f->R[dev].MaxAdp; i++) {
            f->R[dev].Adp[i].Peer = -1;
        }
        fabResetRouter(f, dev);
    }
    if (pr == 0) {
        /* Spec 4.4.5.2: the host's protocol adapters back to defaults. */
        for (i = 1; i <= p->MaxAdp; i++) {
            if (fabPcieAdp(p, i) || fabUsb3Adp(p, i)) {
                fabPcieUpdate(f, 0, i);
            }
        }
    }
}

void fabUnplug(FAB *f, int port)
{
    int a = fabPortLane0(f, port);
    int dev = f->R[0].Adp[a].Peer;
    unsigned i;
    int pr;
    unsigned pa;

    fabDisconnect(f, 0, (unsigned)a);
    /* The host's PCIe Down Path Enable goes to its default (record 22
     * section 7.3 step 1.1, Spec 4.4.5.2). */
    for (i = 1; i <= f->R[0].MaxAdp; i++) {
        if (fabPcieAdp(&f->R[0], i) && fabPortPcie(f, port) == (int)i) {
            f->R[0].Adp[i].Cs[FAB_CAP_ADP] &= ~0x80000000UL;
            fabPcieUpdate(f, 0, i);
        }
    }
    (void)dev;
    (void)pr;
    (void)pa;
    if ((f->R[0].Adp[a].Cs[5] & 0x80000000UL) == 0) {
        fabHotPlug(f, 0, (unsigned long)a, 1);
    }
}

void fabPowerCycle(FAB *f)
{
    int i;
    unsigned a;

    for (i = 1; i < FAB_MAX_ROUTERS; i++) {
        if (f->R[i].Used) {
            fabResetRouter(f, i);
        }
    }
    fabResetRouter(f, 0);
    for (a = 1; a <= f->R[0].MaxAdp; a++) {
        if (f->R[0].Adp[a].Present && (f->R[0].Adp[a].Cs[2] & 0xFFFFFFUL) == 1) {
            f->R[0].Adp[a].Cs[4] |= 0x80000000UL;
            if (f->R[0].Adp[a].Peer >= 0) {
                setLaneWidth(&f->R[0].Adp[a], 1);
            }
        }
    }
    for (i = 1; i < FAB_MAX_ROUTERS; i++) {
        if (f->R[i].Used && f->R[i].Connected) {
            setLaneWidth(&f->R[i].Adp[f->R[i].UpAdp], 1);
            setLaneWidth(&f->R[i].Adp[f->R[i].UpAdp + 1], 1);
        }
    }
    f->RxQCount = 0;
}

void fabSwapQuiet(FAB *f, int port, int dev)
{
    int a = fabPortLane0(f, port);
    int old = f->R[0].Adp[a].Peer;
    unsigned long hi;
    unsigned long lo;
    unsigned long lock0 = f->R[0].Adp[a].Cs[4] & 0x80000000UL;
    unsigned long lock1 = f->R[0].Adp[a + 1].Cs[4] & 0x80000000UL;
    unsigned long width = (f->R[0].Adp[a].Cs[FAB_CAP_LANE + 1] >> 20) & 0x3FUL;

    if (old >= 0) {
        f->R[old].Connected = 0;
        f->R[old].Adp[f->R[old].UpAdp].Peer = -1;
        f->R[old].Adp[f->R[old].UpAdp + 1].Peer = -1;
    }
    fabConnect(f, 0, (unsigned)a, dev);
    f->R[0].Adp[a].Cs[4] = (f->R[0].Adp[a].Cs[4] & ~0x80000000UL) | lock0;
    f->R[0].Adp[a + 1].Cs[4] = (f->R[0].Adp[a + 1].Cs[4] & ~0x80000000UL) | lock1;
    setLaneWidth(&f->R[0].Adp[a], width);
    setLaneWidth(&f->R[0].Adp[a + 1], width);
    setLaneWidth(&f->R[dev].Adp[f->R[dev].UpAdp], width);
    setLaneWidth(&f->R[dev].Adp[f->R[dev].UpAdp + 1], width);
    fabRouteOf(f, dev, &hi, &lo);
    f->R[dev].Rcs[1] = (f->R[dev].Rcs[1] & ~(7UL << 20)) | (1UL << 20);
    f->R[dev].Rcs[2] = lo;
    f->R[dev].Rcs[3] = hi | 0x80000000UL;
    f->R[dev].Rcs[6] |= 0x01000000UL;
}

void fabPlugChild(FAB *f, int dev, int child, int enumerate)
{
    unsigned long hi;
    unsigned long lo;

    fabConnect(f, dev, 3, child);
    f->R[dev].Adp[3].Cs[4] &= ~0x80000000UL;
    if (enumerate) {
        fabRouteOf(f, child, &hi, &lo);
        f->R[child].Rcs[1] = (f->R[child].Rcs[1] & ~(7UL << 20)) | (2UL << 20);
        f->R[child].Rcs[2] = lo;
        f->R[child].Rcs[3] = hi | 0x80000000UL;
        f->R[child].Rcs[6] |= 0x01000000UL;
    }
}

void fabUnplugChild(FAB *f, int dev)
{
    fabDisconnect(f, dev, 3);
}

static void fabSetPath(FAB *f, int r, unsigned a, unsigned hop, unsigned outAdp,
                       unsigned outHop, unsigned credits)
{
    FAB_ADP *ad = &f->R[r].Adp[a];

    ad->Path[2 * hop] = 0x80000000UL | outHop | ((unsigned long)outAdp << 11) |
                        ((unsigned long)credits << 17);
    ad->Path[2 * hop + 1] = 1UL | (3UL << 8) | 0x01000000UL;
}

void fabAddDpOut(FAB *f, int router, unsigned adp)
{
    fabProto(&f->R[router], adp, 0x0E0102UL);
    f->R[router].Adp[adp].Path[2 * 8] = 0;
    f->R[router].Adp[adp].Path[2 * 8 + 1] = 0;
    fabFinish(&f->R[router]);
}

void fabPreos(FAB *f, int port, int usb3)
{
    int a = fabPortLane0(f, port);
    int d = f->R[0].Adp[a].Peer;
    unsigned hp = (unsigned)fabPortPcie(f, port);
    unsigned hu = (unsigned)fabPortUsb3(f, port);
    unsigned long hi;
    unsigned long lo;
    unsigned dpu;
    unsigned du3;

    f->R[0].Rcs[3] |= 0x80000000UL;
    f->R[0].Rcs[6] |= 0x01000000UL;
    if (d < 0) {
        return;
    }
    f->R[0].Adp[a].Cs[4] &= ~0x80000000UL;
    f->R[0].Adp[a + 1].Cs[4] &= ~0x80000000UL;
    fabRouteOf(f, d, &hi, &lo);
    f->R[d].Rcs[1] = (f->R[d].Rcs[1] & ~(7UL << 20)) | (1UL << 20);
    f->R[d].Rcs[2] = lo;
    f->R[d].Rcs[3] = hi | 0x80000000UL;
    f->R[d].Rcs[4] |= 0x1000UL | 0xFFUL;
    f->R[d].Rcs[5] = 0x81000000UL | (usb3 ? 0x02000000UL : 0);
    f->R[d].Rcs[6] |= 0x03000000UL;
    setLaneWidth(&f->R[0].Adp[a], 2);
    setLaneWidth(&f->R[0].Adp[a + 1], 2);
    setLaneWidth(&f->R[d].Adp[f->R[d].UpAdp], 2);
    setLaneWidth(&f->R[d].Adp[f->R[d].UpAdp + 1], 2);
    dpu = 5;
    du3 = 6;
    fabSetPath(f, 0, hp, 8, (unsigned)a, 8, 7);
    fabSetPath(f, d, f->R[d].UpAdp, 8, dpu, 8, 32);
    fabSetPath(f, d, dpu, 8, f->R[d].UpAdp, 9, 7);
    fabSetPath(f, 0, (unsigned)a, 9, hp, 8, 32);
    f->R[0].Adp[hp].Cs[FAB_CAP_ADP] |= 0x80000000UL;
    f->R[d].Adp[dpu].Cs[FAB_CAP_ADP] |= 0x80000000UL;
    fabPcieUpdate(f, 0, hp);
    if (usb3) {
        fabSetPath(f, 0, hu, 8, (unsigned)a, 10, 7);
        fabSetPath(f, d, f->R[d].UpAdp, 10, du3, 8, 16);
        fabSetPath(f, d, du3, 8, f->R[d].UpAdp, 11, 7);
        fabSetPath(f, 0, (unsigned)a, 11, hu, 8, 16);
        f->R[0].Adp[hu].Cs[FAB_CAP_ADP] |= 0xC0000000UL;
        f->R[d].Adp[du3].Cs[FAB_CAP_ADP] |= 0xC0000000UL;
    }
}

/* ---------------------------------------------------------------------- */
/* Queries                                                                */
/* ---------------------------------------------------------------------- */

unsigned long fabPath0(const FAB *f, int router, int adp, int hop)
{
    return f->R[router].Adp[adp].Path[2 * hop];
}

unsigned long fabPath1(const FAB *f, int router, int adp, int hop)
{
    return f->R[router].Adp[adp].Path[2 * hop + 1];
}

unsigned long fabAdp(const FAB *f, int router, int adp, int dw)
{
    return f->R[router].Adp[adp].Cs[dw];
}

unsigned long fabRtr(const FAB *f, int router, int dw)
{
    return f->R[router].Rcs[dw];
}

int fabPcieUp(const FAB *f, int port)
{
    int hp = fabPortPcie(f, port);
    int d = fabDevAt(f, port);
    unsigned a;

    if (hp == 0 || d < 0) {
        return 0;
    }
    for (a = 1; a <= f->R[d].MaxAdp; a++) {
        if ((f->R[d].Adp[a].Cs[2] & 0xFFFFFFUL) == 0x100102UL) {
            return (f->R[0].Adp[hp].Cs[FAB_CAP_ADP] & 0x80000000UL) &&
                   (f->R[d].Adp[a].Cs[FAB_CAP_ADP] & 0x80000000UL) &&
                   ((f->R[0].Adp[hp].Cs[FAB_CAP_ADP] >> 25) & 0xF) == 6;
        }
    }
    return 0;
}

int fabUsb3Up(const FAB *f, int port)
{
    int hu = fabPortUsb3(f, port);
    int d = fabDevAt(f, port);
    unsigned a;

    if (hu == 0 || d < 0) {
        return 0;
    }
    for (a = 1; a <= f->R[d].MaxAdp; a++) {
        if ((f->R[d].Adp[a].Cs[2] & 0xFFFFFFUL) == 0x200102UL) {
            return (f->R[0].Adp[hu].Cs[FAB_CAP_ADP] & 0x80000000UL) &&
                   (f->R[d].Adp[a].Cs[FAB_CAP_ADP] & 0x80000000UL);
        }
    }
    return 0;
}

long fabFindWrite(const FAB *f, long from, int router, unsigned long cs,
                  unsigned long adp, unsigned long addr)
{
    long i;

    for (i = from; i < (long)f->WrCount && i < FAB_WRLOG; i++) {
        if (f->WrRouter[i] == router && f->WrCs[i] == cs && f->WrAdp[i] == adp &&
            f->WrAddr[i] == addr) {
            return i;
        }
    }
    return -1;
}
