/*
 * u4_fakefab.c - the fake USB4 fabric of test_u4cm (see u4_fakefab.h).
 *
 * Written for the host suite from the USB4 Specification 2.0 (sections
 * 4.4.5.2, 6.4, 8.2, 8.3, 12.3, 12.4, 12.6) and usb4-emu's documented
 * host models (design record 22 section 2.1), independently of
 * src\cm\tbt_u4cm.c. C89 with the C library; test-only.
 *
 * One exception: the Thunderbolt 3 router's EEPROM (fabEeByte, fabEeCtl)
 * is adapted from usb4-emu src/qemu/hw/usb4/usb4-router-tb3.c
 * (eeprom_byte, eeprom_ctl; QEMU fork 2a893ea, read 2026-10-16),
 * Copyright (c) 2026 usb4-emu contributors. usb4-emu's copyright holder
 * offers what this project takes from it under GPL-2.0-or-later (owner,
 * 2026-10-25; legal-provenance.md section 2a).
 * This file is under this project's LICENSE, GPL-2.0-or-later.
 */

#include <stdio.h>
#include <stdlib.h>
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

unsigned long fabCrcOf(const unsigned char *p, unsigned long n)
{
    return fabCrc(p, n);
}

void *fabMem(FAB *f)
{
    return f->MemDw;
}

/* ---------------------------------------------------------------------- */
/* The request trace (u4_fakefab.h)                                       */
/* ---------------------------------------------------------------------- */

static FILE *fabTraceFp;
static int fabTraceOpened;
static unsigned long fabTraceFabs;

static FILE *fabTraceOut(void)
{
    const char *p;

    if (!fabTraceOpened) {
        fabTraceOpened = 1;
        p = getenv("U4_TRACE");
        if (p != NULL && p[0] != '\0') {
            fabTraceFp = fopen(p, "w");
            if (fabTraceFp == NULL) {
                fprintf(stderr, "U4_TRACE: cannot open %s\n", p);
                exit(2);
            }
        }
    }
    return fabTraceFp;
}

/* A write that failed ends the run: a trace with a hole in it must not
 * compare as identical. "end" closes the trace. */
void fabTraceMark(const char *name)
{
    FILE *t = fabTraceOut();

    if (t != NULL) {
        fprintf(t, "== %s\n", name);
        if (fflush(t) != 0 || ferror(t)) {
            fprintf(stderr, "U4_TRACE: write failed\n");
            exit(2);
        }
        if (strcmp(name, "end") == 0) {
            fabTraceFp = NULL;
            if (fclose(t) != 0) {
                fprintf(stderr, "U4_TRACE: close failed\n");
                exit(2);
            }
        }
    }
}

static void fabTraceFrame(const FAB *f, const char *what, unsigned long pdf,
                          const unsigned long *dw, unsigned long words)
{
    FILE *t = fabTraceOut();
    unsigned long i;

    if (t == NULL) {
        return;
    }
    fprintf(t, "%lu %lu %s %lx", f->TraceId, f->NowUs, what, pdf);
    for (i = 0; i < words && i < 64; i++) {
        fprintf(t, " %08lx", dw[i]);
    }
    fputc('\n', t);
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

    /* 63 words and the CRC fill a 256-byte ring 0 buffer (TBT_RING_BUF_BYTES):
     * a Properties Read Response of 45 DWs is that long (task 45.59). */
    if (f->RxQCount >= FAB_RXQ || words > 63) {
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
            /* No buffer. The host interface drops such a frame (as
             * tbt_fake.h does); this fabric keeps it instead, unless the
             * ring is off, standing for a router's retransmission of an
             * unacknowledged event (Spec 6.6). A response lost to a full
             * ring is therefore not modelled here. */
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

/* Current Link Speed (Spec 8.2.2.3): 8h Gen 2, 4h Gen 3, 2h Gen 4. */
static void setLaneSpeed(FAB_ADP *a, unsigned long s)
{
    a->Cs[FAB_CAP_LANE + 1] = (a->Cs[FAB_CAP_LANE + 1] & ~(0xFUL << 16)) | (s << 16);
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
            (f->R[0].Tbt3 ? (a & 1U) != 0 : f->R[0].Adp[a].Cs[FAB_CAP_PORT] != 0)) {
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

/* A device router's Nth downstream lane 0 adapter (1-based), its
 * upstream port not counted; 0 when there is none. */
static int fabDownLaneN(const FAB_ROUTER *d, int n)
{
    unsigned i;
    int k = 0;

    for (i = 1; i <= d->MaxAdp; i++) {
        if (d->Adp[i].Present && (d->Adp[i].Cs[2] & 0xFFFFFFUL) == 1 && !d->Adp[i].Lane1 &&
            i != d->UpAdp && ++k == n) {
            return (int)i;
        }
    }
    return 0;
}

/* The far end of a protocol adapter below depth 1 (record 34 sections 6.4
 * and 6.6): a device router's Nth down adapter of a type belongs to its
 * Nth downstream port, and the child there pairs by its first up adapter
 * of the type; a child's up adapter the other way round. */
static int fabPeerDeep(FAB *f, int r, unsigned a, unsigned long t, unsigned long want,
                       int *pr, unsigned *pa)
{
    const FAB_ROUTER *d = &f->R[r];
    unsigned i;
    int n;
    int k;
    int lane;
    int other;

    if ((t & 0xFFUL) == 1) {
        n = 0;
        for (i = 1; i <= a; i++) {
            if (d->Adp[i].Present && (d->Adp[i].Cs[2] & 0xFFFFFFUL) == t) {
                n++;
            }
        }
        lane = fabDownLaneN(d, n);
        if (lane == 0) {
            return 0;
        }
        other = d->Adp[lane].Peer;
        if (other < 0 || !f->R[other].Connected || f->R[other].Parent != r) {
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
    if (!d->Connected || d->Parent <= 0) {
        return 0;
    }
    for (i = 1; i <= d->MaxAdp; i++) {
        if (d->Adp[i].Present && (d->Adp[i].Cs[2] & 0xFFFFFFUL) == t) {
            if (i != a) {
                return 0;
            }
            break;
        }
    }
    other = d->Parent;
    n = 0;
    for (k = 1; ; k++) {
        lane = fabDownLaneN(&f->R[other], k);
        if (lane == 0) {
            return 0;
        }
        if (lane == d->ParentAdp) {
            n = k;
            break;
        }
    }
    k = 0;
    for (i = 1; i <= f->R[other].MaxAdp; i++) {
        if (f->R[other].Adp[i].Present && (f->R[other].Adp[i].Cs[2] & 0xFFFFFFUL) == want &&
            ++k == n) {
            *pr = other;
            *pa = i;
            return 1;
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
    if (r != 0 && ((t & 0xFFUL) == 1 || f->R[r].Parent > 0)) {
        return fabPeerDeep(f, r, a, t, want, pr, pa);
    }
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
                (f->R[0].Tbt3 ? (i & 1U) != 0 : f->R[0].Adp[i].Cs[FAB_CAP_PORT] != 0) &&
                ++k == port) {
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
        /* The far end of a pair whose near end reads a fixed state (the
         * Thunderbolt 3 dock's PCIe Down, FAB_DEV_TBT3_DOCK) still trains
         * by both Path Enables; any other forced end stops here, as it
         * always did. */
        if (!f->R[r].Adp[a].PeerFollows || !fabPeerProto(f, r, a, &pr, &pa) ||
            f->R[pr].Adp[pa].LtssmForce != 0) {
            return;
        }
        o = &f->R[pr].Adp[pa].Cs[FAB_CAP_ADP];
        both = (*c & 0x80000000UL) && (*o & 0x80000000UL);
        *o = (*o & ~(0xFUL << 25)) |
             ((both ? 6UL : (f->R[pr].Adp[pa].DetectStuck ? 1UL : 0UL)) << 25);
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
    if (f->R[pr].Adp[pa].LtssmForce != 0 && f->R[pr].Adp[pa].PeerFollows) {
        /* The far end reads a fixed state; only this end trains. */
        *c = (*c & ~(0xFUL << 25)) |
             ((both ? 6UL : (f->R[r].Adp[a].DetectStuck ? 1UL : 0UL)) << 25);
        return;
    }
    /* A Thunderbolt router's adapter reads 3 there with its tunnel up, not
     * USB4's L0 (usb4-emu's captured Thunderbolt 3 adapter, 0x46210400). */
    *c = (*c & ~(0xFUL << 25)) |
         ((both ? (f->R[r].Tbt3 ? 3UL : 6UL) :
                  (f->R[r].Adp[a].DetectStuck ? 1UL : 0UL)) << 25);
    *o = (*o & ~(0xFUL << 25)) |
         ((both ? (f->R[pr].Tbt3 ? 3UL : 6UL) :
                  (f->R[pr].Adp[pa].DetectStuck ? 1UL : 0UL)) << 25);
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
    case 0x32: slot = FAB_OP_SETCAPS; break;
    case 0x25:
        r->NvmOps++;
        slot = r->Kind == FAB_DEV_ASM2464 ? FAB_OP_NVM : -1;
        break;
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
        r->Rcs[26] = (r->Rcs[26] & 0xFFFFUL) |
                     ((r->FailStatus != 0 ? r->FailStatus : 1UL) << 24);
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
        if (r->CapsFailAfterSet != 0 && r->SetCapsOps != 0) {
            if (r->CapsFailAfterSet > 0) {
                r->CapsFailAfterSet--;
            }
            r->Rcs[26] = (r->Rcs[26] & 0xFFFFUL) | (1UL << 24);
            return;
        }
        r->Rcs[25] = 5;
        r->Rcs[9] = r->CapsData;
        break;
    case FAB_OP_SETCAPS:
        /* Spec 8.3.1.3.3: Capability Index 7:0, Enable Capability 8; a
         * capability is enabled only where it is supported. */
        r->SetCapsOps++;
        r->SetCapsLastMeta = meta;
        i = meta & 0xFFUL;
        if (!r->SetCapsIgnore && i >= 1 && i <= 15 &&
            (r->CapsData & (1UL << (2 * i))) != 0) {
            if (meta & 0x100UL) {
                r->CapsData |= 1UL << (2 * i + 1);
            } else {
                r->CapsData &= ~(1UL << (2 * i + 1));
            }
        }
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
        if (r->RrDelayUs != 0) {
            /* Router Ready later than Enumerated (the ASM2464, usb4-emu
             * docs/captures/ORICO-ASM2464.md section 3): set by the read
             * that finds the delay over (fabRead). */
            r->RrPending = 1;
            r->RrAtUs = f->NowUs + r->RrDelayUs;
        } else {
            r->Rcs[6] |= 0x01000000UL;
        }
    }
    if (ri == 0 && r->Tbt3) {
        for (a = 1; a <= r->MaxAdp; a++) {
            if (r->Adp[a].Present && (r->Adp[a].Cs[2] & 0xFFFFFFUL) == 1 &&
                r->Adp[a].Peer >= 0) {
                fabHotPlug(f, 0, a, 0);
            }
        }
    }
    if (r->DpPlugEvents) {
        for (a = 1; a <= r->MaxAdp; a++) {
            if (r->Adp[a].Present && (r->Adp[a].Cs[2] & 0xFFFFFFUL) == 0x0E0101UL) {
                fabHotPlug(f, ri, a, 0);
            }
        }
    }
}

static unsigned fabEeByte(const FAB_ROUTER *r, unsigned long addr)
{
    if (addr >= r->EeBase && addr - r->EeBase < r->DromLen) {
        return r->Drom[addr - r->EeBase];
    }
    return 0xFF;
}

/* SK 0, CS 1 (0 selects), DI 2 (to the part), DO 3 (from it), enable 4. */
static void fabEeCtl(FAB_ROUTER *r, unsigned long old, unsigned long v)
{
    int was = (old & 0x10UL) && !(old & 0x02UL);
    int on = (v & 0x10UL) && !(v & 0x02UL);

    r->EeCtlWrites++;
    if (!on) {
        r->EeOn = 0;
        r->EeData = 1;
        return;
    }
    if (!was || !r->EeOn) {
        r->EeOn = 1;
        r->EeBits = 0;
        r->EeShift = 0;
        r->EeData = 0;
    }
    if ((old & 0x01UL) || !(v & 0x01UL)) {
        return;
    }
    if (r->EeBits < 24) {
        r->EeShift = (r->EeShift << 1) | ((v & 0x04UL) ? 1UL : 0UL);
        if (++r->EeBits == 24) {
            r->EeCmd = r->EeShift >> 16;
            r->EeAddr = r->EeShift & 0xFFFFUL;
            r->EeOutByte = r->EeCmd == 3 ? fabEeByte(r, r->EeAddr) : 0xFF;
            r->EeOutBit = 0;
        }
        return;
    }
    r->EeData = (r->EeOutByte >> (7 - r->EeOutBit)) & 1;
    if (++r->EeOutBit == 8) {
        r->EeAddr++;
        r->EeOutByte = r->EeCmd == 3 ? fabEeByte(r, r->EeAddr) : 0xFF;
        r->EeOutBit = 0;
    }
}

static void fabAdapterWrite(FAB *f, int ri, unsigned a, unsigned long addr,
                            unsigned long v);

/* A Thunderbolt router's link controller port block of `addr`, as
 * physical port 0 or 1, or -1 (FAB_RCAP_LC + FAB_LC_START + n *
 * FAB_LC_PORT_SIZE). */
static int fabLcPortOf(const FAB_ROUTER *r, unsigned long addr, unsigned long reg)
{
    unsigned long n;

    if (!r->Tbt3 || r->Rcs[FAB_RCAP_LC] == 0 || addr < FAB_RCAP_LC + FAB_LC_START) {
        return -1;
    }
    for (n = 0; n < 2; n++) {
        if (addr == FAB_RCAP_LC + FAB_LC_START + n * FAB_LC_PORT_SIZE + reg) {
            return (int)n;
        }
    }
    return -1;
}

static void fabRouterWrite(FAB *f, int ri, unsigned long addr, unsigned long v)
{
    FAB_ROUTER *r = &f->R[ri];
    unsigned long old = r->Rcs[addr];
    int phys;

    phys = fabLcPortOf(r, addr, 0x26);
    if (phys >= 0 && ri == 0) {
        /* The link controller's Downstream Port Reset (Linux lc.c
         * tb_lc_reset_port): the PORT_CS_19 semantics on the port's lane
         * 0 adapter, which keeps no Lock on a Thunderbolt host. */
        r->Rcs[addr] = (old & ~1UL) | (v & 1UL);
        fabAdapterWrite(f, ri, (unsigned)(2 * phys + 1), FAB_CAP_PORT + 19, v & 1UL);
        r->Adp[2 * phys + 1].Cs[4] &= ~0x80000000UL;
        r->Adp[2 * phys + 2].Cs[4] &= ~0x80000000UL;
        f->LcResets += (v & 1UL);
        return;
    }
    if (r->Tbt3 && addr == 0x26 && r->Rcs[0] == (0x8086UL | (0x1513UL << 16))) {
        /* Light Ridge's TMU access enable (cap.c), counted while set. */
        r->Rcs[addr] = v;
        if ((v & 0x00100000UL) != 0 && (old & 0x00100000UL) == 0) {
            f->TmuAccessOn++;
        }
        return;
    }

    if (r->Tbt3 && addr == FAB_RCAP_PLUG + 4) {
        r->Rcs[addr] = (old & ~0x17UL) | (v & 0x17UL);
        fabEeCtl(r, old, r->Rcs[addr]);
        return;
    }

    if (addr == 0 || addr == 6 || addr == 7 || addr == 8) {
        return;     /* read only */
    }
    if (addr == 1) {
        r->Rcs[1] = (old & ~(7UL << 20)) | (v & (7UL << 20));
        if (r->UpWritable) {
            r->Rcs[1] = (r->Rcs[1] & ~(0x3FUL << 8)) | (v & (0x3FUL << 8));
        }
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
    if (!r->Tbt3 && r->TmuAt != 0 && addr == r->TmuAt + 25 && !r->PostStuck) {
        r->Rcs[r->TmuAt + 24] = 0;
        r->Rcs[r->TmuAt + 25] = 0;
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
        if (t == 1 && ad->Lane1) {
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
        if ((v & 0x8000UL) && ad->Peer >= 0 &&
            ((ad->Cs[addr] >> 16) & 0xFUL) == 0x2UL) {
            /* Spec 8.2.2.3: on a Gen 4 link the bit clears itself. */
            ad->Cs[addr] &= ~0x8000UL;
            return;
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
                /* Total Buffers doubles on a bonded link (Spec 5.3.2.1):
                 * the generic figure 60 to 120; a preset or test that set
                 * its own figure (asm4242's 174) keeps it. */
                if (((ad->Cs[4] >> 20) & 0x3FFUL) == 60UL) {
                    ad->Cs[4] = (ad->Cs[4] & ~(0x3FFUL << 20)) | (120UL << 20);
                }
                if (((f->R[d].Adp[ad->PeerAdp].Cs[4] >> 20) & 0x3FFUL) == 60UL) {
                    f->R[d].Adp[ad->PeerAdp].Cs[4] =
                        (f->R[d].Adp[ad->PeerAdp].Cs[4] & ~(0x3FFUL << 20)) | (120UL << 20);
                }
            }
        }
        return;
    }
    if (t == 1 && addr == FAB_CAP_PORT + 19) {
        if (r->ElrStuck) {
            v &= ~0x80000000UL;
        }
        ad->Cs[addr] = v;
        if ((v & 1UL) == 0 && ad->Peer >= 0 && f->R[ad->Peer].ReconnectPending &&
            !f->R[ad->Peer].ReconnectArmed) {
            /* The capture's 594 ms run from the reset's release. */
            f->R[ad->Peer].ReconnectArmed = 1;
            f->R[ad->Peer].DprReleasedAtUs = f->NowUs;
            f->R[ad->Peer].ReconnectAtUs = f->NowUs + f->R[ad->Peer].LinkDelayUs;
        }
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
                /* Version 2 state back to its defaults (task 45.20). */
                if (fabPcieAdp(&f->R[d], pa)) {
                    f->R[d].Adp[pa].Cs[FAB_CAP_ADP + 1] = 0;
                } else if ((f->R[d].Adp[pa].Cs[2] & 0xFFFFFFUL) == 1) {
                    f->R[d].Adp[pa].Cs[FAB_CAP_TMU_ADP + 8] = 0;
                    f->R[d].Adp[pa].Cs[FAB_CAP_TMU_ADP + 9] = 0;
                }
            }
            if (!f->R[d].Tbt3) {
                f->R[d].CapsData &= 0x55555555UL;
                f->R[d].SetCapsOps = 0;
            }
            ad->Cs[4] |= 0x80000000UL;
            r->Adp[a + 1].Cs[4] |= 0x80000000UL;
            if (f->R[d].LinkDelayUs != 0 && !f->R[d].ReconnectPending) {
                /* A router that takes its time to come back (the ASM2464,
                 * 594 ms after the reset's release, usb4-emu
                 * docs/captures/ORICO-ASM2464.md section 10): the link is
                 * down from the reset until then, with an unplug event now
                 * and a plug event then. */
                f->R[d].RrPending = 0;
                for (pa = 0; pa < 2; pa++) {
                    if (r->Adp[a + pa].Present) {
                        setLaneState(&r->Adp[a + pa], 7);
                        setLaneWidth(&r->Adp[a + pa], 0);
                    }
                }
                f->R[d].ReconnectPending = 1;
                f->R[d].ReconnectArmed = 0;
                if ((ad->Cs[5] & 0x80000000UL) == 0) {
                    fabHotPlug(f, ri, a, 1);
                }
            }
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
    if (fabPcieAdp(r, a) && addr == FAB_CAP_ADP + 1 && ad->EeStuck) {
        ad->Cs[addr] = (v & ~1UL) | (old & 1UL);
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
        if (!r->Tbt3 && r->TmuAt != 0 && addr == r->TmuAt + 1) {
            r->Rcs[addr] += 0x10000UL;      /* time moves */
        }
        if (addr == 6 && r->RrPending && f->NowUs - r->RrAtUs < 0x80000000UL) {
            r->RrPending = 0;
            r->Rcs[6] |= 0x01000000UL;
        }
        if (r->Tbt3 && addr == FAB_RCAP_PLUG + 4) {
            return (r->Rcs[addr] & ~0x08UL) | (r->EeData ? 0x08UL : 0UL);
        }
        return r->Rcs[addr];
    }
    if (cs == TBT_CS_ADAPTER) {
        if (fabPcieAdp(r, a) && addr == FAB_CAP_ADP) {
            fabPcieUpdate(f, ri, a);
        }
        if (fabUsb3Adp(r, a) && addr == FAB_CAP_ADP + 4) {
            /* Task 45.56: USB3 Link Valid and the Port Link State. */
            if (r->Adp[a].Usb3Ls != 0) {
                return (r->Adp[a].Cs[addr] & ~0xFFFUL) | ((r->Adp[a].Usb3Ls - 1) & 0xFFFUL);
            }
            return (r->Adp[a].Cs[addr] & ~0xFFFUL) |
                   ((r->Adp[a].Cs[FAB_CAP_ADP] & 0x80000000UL) ? 0x80UL : 0UL);
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
    if (pdf == TBT_PDF_WRITE) {
        if (f->TriedCount < FAB_WRLOG) {
            f->TriedHi[f->TriedCount] = dw[0] & 0xFFFFFFUL;
            f->TriedLo[f->TriedCount] = dw[1];
        }
        f->TriedCount++;
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
    if (cs != TBT_CS_ROUTER && adp < FAB_MAX_ADP) {
        f->R[r].AdpTouches[adp]++;
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
                f->WrN[f->WrCount] = n;
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
    case TBT_PDF_XDOMAIN_REQ:
    case TBT_PDF_XDOMAIN_RESP:
        f->XdSent++;
        if (f->XdTx != NULL) {
            f->XdTx(f, pdf, dw, words);
        }
        break;
    }
}

void fabAdapterPoke(FAB *f, int router, unsigned adp, unsigned long addr, unsigned long v)
{
    fabAdapterWrite(f, router, adp, addr, v);
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
            fabTraceFrame(f, "TXBADCRC", (dw2 >> 12) & 0xFUL, dw, words);
            continue;
        }
        f->TxCount++;
        fabTraceFrame(f, "TX", (dw2 >> 12) & 0xFUL, dw, words);
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

    f->MmioReads++;
    if (f->Gone) {
        f->GoneReads++;
        return 0xFFFFFFFFUL;
    }
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

    f->MmioWrites++;
    if (fabTraceOut() != NULL) {
        fprintf(fabTraceOut(), "%lu %lu MW %05lx %08lx%s\n", f->TraceId, f->NowUs,
                (unsigned long)off, (unsigned long)v, f->Gone ? " gone" : "");
    }
    if (f->Gone) {
        f->GoneWrites++;
        return;
    }
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

static void fabConnect(FAB *f, int pr, unsigned pa, int dev);

static void fabAdvance(FAB *f, unsigned long us)
{
    int i;
    FAB_ROUTER *r;

    f->NowUs += us;
    if (f->Gone) {
        return;
    }
    for (i = 1; i < FAB_MAX_ROUTERS; i++) {
        r = &f->R[i];
        if (r->Used && r->ReconnectPending && r->ReconnectArmed && r->Connected &&
            f->NowUs - r->ReconnectAtUs < 0x80000000UL) {
            /* The link of a Downstream Port Reset back (LinkDelayUs). */
            r->ReconnectPending = 0;
            r->ReconnectArmed = 0;
            r->ReconnectedAtUs = f->NowUs;
            fabConnect(f, r->Parent, (unsigned)r->ParentAdp, i);
            if ((f->R[r->Parent].Adp[r->ParentAdp].Cs[5] & 0x80000000UL) == 0) {
                fabHotPlug(f, r->Parent, (unsigned long)r->ParentAdp, 0);
            }
        }
    }
    fabProcessTx(f);
    fabPump(f);
}

static BOOLEAN fabCancelled(PVOID ctx)
{
    return (BOOLEAN)(((FAB *)ctx)->Cancel != 0);
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
    if (fabTraceOut() != NULL) {
        fprintf(fabTraceOut(), "%lu %lu L %s %08lx\n", f->TraceId, f->NowUs, label,
                (unsigned long)value);
    }
    if (f->Watch != NULL && strncmp(label, f->Watch, strlen(f->Watch)) == 0) {
        if (f->WatchCount < 16) {
            f->WatchLabel[f->WatchCount] = label;
            f->WatchValue[f->WatchCount] = value;
        }
        f->WatchCount++;
    }
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
    f->TraceId = ++fabTraceFabs;
    if (fabTraceOut() != NULL) {
        fprintf(fabTraceOut(), "%lu new\n", f->TraceId);
    }
    f->Host.Context = f;
    f->Host.Read32 = fabRead32;
    f->Host.Write32 = fabWrite32;
    f->Host.Stall = fabStall;
    f->Host.Sleep = fabSleep;
    f->Host.NowMs = fabNowMs;
    f->Host.Log = fabLog;
    f->Host.ClockUs = fabClockUs;
    f->Host.TickUs = 0;
    f->Host.Cancelled = fabCancelled;
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
    ad->Lane1 = (a % 2) == 0;
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
    r->TmuAt = FAB_RCAP_TMU;
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

/* usb4-emu's `minimal` host as a USB4 version 2 host router (task 45.20):
 * USB4 Version 40h, host interface version 40h (Spec Table 12-11), Get
 * Capabilities reporting the Sequence bit capability supported, Gen 4
 * links with another version 2 router. Test ids, no product's. */
int fabHostV2(FAB *f)
{
    FAB_ROUTER *h = &f->R[0];

    fabHostMinimal(f);
    h->Rcs[4] = (h->Rcs[4] & 0x00FFFFFFUL) | (0x40UL << 24);
    h->Rcs[0] = 0x46F4UL | (0x0002UL << 16);
    h->CapsData = 1UL << 4;
    h->Gen4 = 1;
    f->Caps = (f->Caps & ~(0xFFUL << 16)) | (0x40UL << 16);
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

/*
 * usb4-emu's `asm4242` host model (its docs/USB4-HOSTS.md section 5 and
 * hw/usb4/usb4-router-asm.c, from public sources only, nothing captured):
 * router 174c:2428, USB4 1.0; lanes 1-4, DP IN 5-6, the Host Interface 7
 * (the upstream adapter), PCIe Down 8-9, USB3 Down 10-11; lanes reporting
 * 174 Total Buffers and baMaxHI 32 (quirk c); a Host Interface path whose
 * Valid is cleared keeps Pending Packets set (quirk a); Buffer Allocation
 * USB3 32, DP AUX 1, DP main 18, PCIe 32; the mapping operation as the
 * minimal router answers it, with the switch topology's tunnel ports
 * 02:00.0 and 02:01.0.
 */
int fabHostAsm4242(FAB *f)
{
    FAB_ROUTER *h = &f->R[0];
    unsigned a;
    unsigned k;

    memset(h, 0, sizeof(*h));
    h->Parent = -1;
    fabRouterBase(h, 0x174C, 0x2428, 0x20);   /* USB4 1.0 */
    fabTmuRouter(h);
    fabLane(h, 1, 1, 1);
    fabLane(h, 2, 0, 1);
    fabLane(h, 3, 1, 1);
    fabLane(h, 4, 0, 1);
    for (a = 1; a <= 4; a++) {
        h->Adp[a].Cs[4] = (h->Adp[a].Cs[4] & ~(0x3FFUL << 20)) | (174UL << 20);
    }
    fabProto(h, 5, 0x0E0101UL);
    fabProto(h, 6, 0x0E0101UL);
    fabAdpNew(h, 7, 0x000002UL, 12);
    for (k = 0; k < FAB_HOPS; k++) {
        h->Adp[7].PpStuck[k] = 1;
    }
    fabProto(h, 8, 0x100101UL);
    fabProto(h, 9, 0x100101UL);
    fabProto(h, 10, 0x200101UL);
    fabProto(h, 11, 0x200101UL);
    h->UpAdp = 7;       /* the Host Interface, as usb4-emu's model sets it */
    fabFinish(h);
    h->RopMode[FAB_OP_DROM] = FAB_ROP_NS;
    h->RopMode[FAB_OP_MAP] = FAB_ROP_OK;
    h->RopMode[FAB_OP_CAPS] = FAB_ROP_OK;
    h->RopMode[FAB_OP_BA] = FAB_ROP_OK;
    h->MapEntries = 2;
    h->MapData[0][0] = 8UL << 1;
    h->MapData[0][1] = 0x0200UL;    /* bus 2, device 0 */
    h->MapData[1][0] = 9UL << 1;
    h->MapData[1][1] = 0x0208UL;    /* bus 2, device 1 */
    h->BaLen = 5;
    h->BaData[0] = 1UL | (32UL << 16);
    h->BaData[1] = 2UL | (1UL << 16);
    h->BaData[2] = 3UL | (18UL << 16);
    h->BaData[3] = 4UL | (32UL << 16);
    h->BaData[4] = 5UL | (32UL << 16);
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

void fabDromMarkUnused(FAB *f, int router, unsigned adp)
{
    FAB_ROUTER *r = &f->R[router];
    unsigned char *d = r->Drom;
    unsigned len = r->DromLen;
    unsigned long crc;

    d[len] = 2;
    d[len + 1] = (unsigned char)(0xC0U | (adp & 0x3FU));
    len += 2;
    d[14] = (unsigned char)((len - 13) & 0xFF);
    d[15] = (unsigned char)(((len - 13) >> 8) & 0x0F);
    crc = fabCrc(d + 13, len - 13);
    d[9] = (unsigned char)crc;
    d[10] = (unsigned char)(crc >> 8);
    d[11] = (unsigned char)(crc >> 16);
    d[12] = (unsigned char)(crc >> 24);
    r->DromLen = len;
}

/*
 * The ASMedia ASM2464PD of an ORICO AAGM2-U4 enclosure, as usb4-emu's
 * `asm2464` device model reproduces its capture (usb4-emu
 * docs/captures/ORICO-ASM2464.md, src/qemu/hw/usb4/usb4-asm2464-data.h,
 * usb4-router-asm2464.c; QEMU fork 20655c3): router 174C:2463, USB4
 * version 1.0; lanes 1 and 2 (Max HopID 27, 66 Total Buffers that
 * bonding does not change, no CL0s, CL1 or CL2 support); PCIe Up 3, its
 * HopID 8 entry reset to output HopID 8 with no credits; adapter 4 a USB3
 * Up adapter that the DROM marks unused; the TMU capability at 0xC0, then
 * vendor-specific capabilities 1, 3 and 6 (long); Router Ready 5 ms after
 * Enumerated; Buffer Allocation PCIe 64 alone; NVM Sector Size failing
 * with Status 2; the link back 594 ms after a Downstream Port Reset; the
 * captured revision 1 (Thunderbolt 3 compatible) DROM, 86 bytes, and the
 * two bytes the DROM Read operation returns past its end.
 */
static const unsigned char fabAsmDrom[88] = {
    0x5e, 0x00, 0x27, 0x91, 0x70, 0x8e, 0x60, 0x4c, 0x17, 0x38, 0x4a, 0xab,
    0xda, 0x01, 0x49, 0x00, 0xb8, 0x00, 0x63, 0x24, 0xe3, 0x02, 0x02, 0xc4,
    0x08, 0x81, 0x80, 0x02, 0x00, 0x00, 0x00, 0x00, 0x08, 0x82, 0x90, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x0b, 0x83, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x0a, 0x01, 0x41, 0x53, 0x4d, 0x65, 0x64, 0x69, 0x61,
    0x00, 0x07, 0x02, 0x32, 0x34, 0x36, 0x78, 0x00, 0x03, 0x08, 0x00, 0x0f,
    0x09, 0x10, 0x04, 0x4c, 0x17, 0x63, 0x24, 0x17, 0x54, 0x66, 0x06, 0x00,
    0xc0, 0x5a, 0x23, 0x02
};

static void fabAsm2464(FAB_ROUTER *d, int i)
{
    unsigned a;

    fabRouterBase(d, 0x174C, 0x2463, 0x20);
    d->Rcs[4] |= 0x0AUL;                        /* Notification Timeout 10 ms */
    fabLane(d, 1, 1, 1);
    fabLane(d, 2, 0, 1);
    fabProto(d, 3, 0x100102UL);
    fabProto(d, 4, 0x200102UL);
    for (a = 1; a <= 2; a++) {
        d->Adp[a].Cs[4] = 0x80000000UL | (66UL << 20);
        d->Adp[a].Cs[5] = 27UL | (27UL << 11);
    }
    d->Adp[3].Path[2 * 8] = 0x00000008UL;
    d->Adp[3].Path[2 * 8 + 1] = 0;
    d->UpAdp = 1;
    d->Adp[1].Cs[FAB_CAP_PORT + 18] = 0x3110UL;     /* BE, no Cable Gen 3, no CLx */
    d->Rcs[1] |= 0xC0UL;
    d->TmuAt = 0xC0;
    d->Rcs[0xC0] = 0x43200328UL;                    /* TMU, next 28h */
    d->Rcs[0x28] = 0x1E010580UL;                    /* VSEC 1, next 80h */
    d->Rcs[0x80] = 0x320305ECUL;                    /* VSEC 3, next ECh */
    d->Rcs[0xEC] = 0x00060500UL;                    /* VSEC 6, long */
    d->Rcs[0xED] = 0x01140000UL;                    /* next 0, 114h dwords */
    d->Rcs[7] = 0x174C608EUL;
    d->Rcs[8] = 0x70912700UL + (unsigned long)i;
    d->RopMode[FAB_OP_NVM] = FAB_ROP_FAIL;
    d->FailStatus = 2;
    d->BaLen = 1;
    d->BaData[0] = 0x00400004UL;
    memcpy(d->Drom, fabAsmDrom, sizeof(fabAsmDrom));
    d->DromLen = 86;
    d->RrDelayUs = 5000;
    d->LinkDelayUs = 594000;
}

static void fabDromUid(FAB_ROUTER *r, unsigned long lo, unsigned long hi);
static void fabTbtCaps(FAB_ROUTER *r, int lc, unsigned ports, unsigned long tag);

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
    if (kind == FAB_DEV_USB4 || kind == FAB_DEV_USB4V2) {
        fabRouterBase(d, 0x46F4, kind == FAB_DEV_USB4 ? 0x4242 : 0x4280,
                      kind == FAB_DEV_USB4 ? 0x20 : 0x40);
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
        if (kind == FAB_DEV_USB4V2) {
            d->CapsData = 1UL << 4;     /* the Sequence bit capability */
            d->Gen4 = 1;
        }
    } else if (kind == FAB_DEV_ASM2464) {
        fabAsm2464(d, i);
    } else if (kind == FAB_DEV_TBT1_PR) {
        /* Port Ridge: generation 1, no link controller; its identity the
         * DROM's UID (record 22 section 19.4 item 3a). */
        d->Tbt3 = 1;
        fabRouterBase(d, 0x8086, 0x1549, 0x00);
        d->BondPossible = 0;
        fabTbtCaps(d, 0, 1, 0);
        fabDrom(d, "QEMU", "Port Ridge Enclosure");
        fabDromUid(d, 0x51DE0000UL + (unsigned long)i, 0x80861549UL);
        fabLane(d, 1, 0, 0);
        fabLane(d, 2, 0, 0);
        fabProto(d, 3, 0x100102UL);
        d->UpAdp = 1;
    } else {
        d->Tbt3 = 1;
        fabRouterBase(d, 0x8086, kind == FAB_DEV_TBT3_AR ? 0x15C0 : 0x15EF, 0x00);
        d->Rcs[1] |= FAB_RCAP_PLUG;
        d->Rcs[FAB_RCAP_PLUG] = FAB_RCAP_LC | (0x05UL << 8) | (0x01UL << 16) | (0x0CUL << 24);
        d->Rcs[FAB_RCAP_PLUG + 1] = 0x7CUL;
        /* EEPROM present, deselected, its DROM at 0x100 (as the TS3 Plus
         * capture's idle 0x0002218A reads present and deselected). */
        d->Rcs[FAB_RCAP_PLUG + 4] = 0x0002218AUL;
        d->Rcs[FAB_RCAP_PLUG + 12] = 0x100UL;
        d->EeBase = 0x100UL;
        d->EeData = 1;
        fabDrom(d, "CalDigit, Inc.", "TS3 Plus");
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
        if (kind == FAB_DEV_TBT3_DOCK) {
            /* usb4-emu usb4-router-tb3.c's device router: its downstream
             * port's lanes 3 and 4, PCIe Up 6 and Down 7. */
            d->Rcs[FAB_RCAP_LC + FAB_LC_START + FAB_LC_PORT_SIZE + 0x8D] = 0x1000UL;
            fabLane(d, 3, 0, 0);
            fabLane(d, 4, 0, 0);
            d->Adp[3].Cs[4] &= ~0x80000000UL;
            d->Adp[4].Cs[4] &= ~0x80000000UL;
            fabProto(d, 6, 0x100102UL);
            fabProto(d, 7, 0x100101UL);
            /* Its PCIe Down's LTSSM field reads 3 whatever the link does
             * (the emulator's 0xc6210400 with the chained tunnel up,
             * record 34 sections 6.5 and 6.6); the child's end trains. */
            d->Adp[7].LtssmForce = 1 + 3;
            d->Adp[7].PeerFollows = 1;
        } else {
            fabProto(d, 3, 0x100102UL);
        }
        d->UpAdp = 1;
    }
    fabFinish(d);
    return i;
}

/* The DROM's UID and its CRC-8 (Linux eeprom.c tb_drom_read_uid_only):
 * byte 0 the CRC-8 (polynomial 07h, from FFh) of bytes 1 to 8. */
static void fabDromUid(FAB_ROUTER *r, unsigned long lo, unsigned long hi)
{
    unsigned char c = 0xFF;
    unsigned i;
    unsigned b;

    for (i = 0; i < 4; i++) {
        r->Drom[1 + i] = (unsigned char)(lo >> (8 * i));
        r->Drom[5 + i] = (unsigned char)(hi >> (8 * i));
    }
    for (i = 1; i <= 8; i++) {
        c = (unsigned char)(c ^ r->Drom[i]);
        for (b = 0; b < 8; b++) {
            c = (unsigned char)((c & 0x80) != 0 ? ((c << 1) ^ 0x07) : (c << 1));
        }
    }
    r->Drom[0] = c;
}

static void fabTbtCaps(FAB_ROUTER *r, int lc, unsigned ports, unsigned long tag)
{
    r->Rcs[1] |= FAB_RCAP_PLUG;
    r->Rcs[FAB_RCAP_PLUG] = (lc ? FAB_RCAP_LC : 0) | (0x05UL << 8) | (0x01UL << 16) |
                            (0x0CUL << 24);
    r->Rcs[FAB_RCAP_PLUG + 1] = 0x7CUL;
    r->Rcs[FAB_RCAP_PLUG + 4] = 0x0002218AUL;
    r->Rcs[FAB_RCAP_PLUG + 12] = 0x100UL;
    r->EeBase = 0x100UL;
    r->EeData = 1;
    if (!lc) {
        return;
    }
    r->Rcs[FAB_RCAP_LC] = (0x05UL << 8) | (0x06UL << 16);
    r->Rcs[FAB_RCAP_LC + 1] = 0;
    r->Rcs[FAB_RCAP_LC + 2] = (unsigned long)ports | ((unsigned long)FAB_LC_START << 8) |
                              ((unsigned long)FAB_LC_PORT_SIZE << 16);
    r->Rcs[FAB_RCAP_LC + 3] = tag;
    r->Rcs[FAB_RCAP_LC + 4] = 0x01020304UL;
    r->Rcs[FAB_RCAP_LC + 5] = 0x05060708UL;
    r->Rcs[FAB_RCAP_LC + 6] = 0x090A0B0CUL;
}

int fabHostTbt(FAB *f, unsigned long device)
{
    FAB_ROUTER *h = &f->R[0];
    unsigned a;
    unsigned pcieA = 6;
    unsigned pcieB = 7;
    int gen = 3;

    switch (device) {
    case 0x1513:
    case 0x1547:
        gen = 1;
        break;
    case 0x156D:
        gen = 2;
        pcieB = 8;
        break;
    case 0x15EA:
        pcieA = 8;
        pcieB = 9;
        break;
    }
    memset(h, 0, sizeof(*h));
    h->Parent = -1;
    h->Tbt3 = 1;
    fabRouterBase(h, 0x8086, device, 0x00);
    h->BondPossible = gen == 3;
    fabTbtCaps(h, gen >= 2, 2, 0xCAFE0000UL);
    if (gen >= 2) {
        h->Rcs[FAB_RCAP_LC + FAB_LC_START + 0x8D] = gen == 3 ? 0x1000UL : 0;
        h->Rcs[FAB_RCAP_LC + FAB_LC_START + FAB_LC_PORT_SIZE + 0x8D] =
            gen == 3 ? 0x1000UL : 0;
    }
    fabDrom(h, "QEMU", "Thunderbolt Host Router");
    fabDromUid(h, 0x00C0FFEEUL, 0x8086D00DUL);
    for (a = 1; a <= 4; a++) {
        fabLane(h, a, 0, 0);
        h->Adp[a].Cs[4] &= ~0x80000000UL;
    }
    fabAdpNew(h, 5, 0x000002UL, 11);
    fabProto(h, pcieA, 0x100101UL);
    fabProto(h, pcieB, 0x100101UL);
    h->UpAdp = 5;
    fabFinish(h);
    h->Rcs[3] = 0;
    h->RopMode[FAB_OP_DROM] = FAB_ROP_NS;
    h->RopMode[FAB_OP_MAP] = FAB_ROP_NS;
    h->RopMode[FAB_OP_CAPS] = FAB_ROP_NS;
    h->RopMode[FAB_OP_BA] = FAB_ROP_NS;
    return 0;
}

/* A Thunderbolt router whose TMU runs, as usb4-emu's routers come up on
 * its tb*-apple-* hosts (task 45.58's Cactus Ridge leg): a router TMU
 * capability, ahead of the others at 0x20, with a TSPacketInterval, and a
 * TMU adapter capability on each lane adapter, uni-directional mode
 * clear. A USB4 link's "TMU Off" reads false on it. */
int fabTbtTmuOn(FAB *f, int rt, unsigned long interval)
{
    FAB_ROUTER *r = &f->R[rt];
    FAB_ADP *ad;
    unsigned a;

    r->Rcs[0x20] = (r->Rcs[1] & 0xFFUL) | (0x03UL << 8);
    r->Rcs[0x23] = interval << 16;
    r->Rcs[1] = (r->Rcs[1] & ~0xFFUL) | 0x20UL;
    for (a = 1; a <= r->MaxAdp; a++) {
        ad = &r->Adp[a];
        if (ad->Present && (ad->Cs[2] & 0xFFFFFFUL) == 1 &&
            (ad->Cs[FAB_CAP_LANE] & 0xFFUL) == 0) {
            ad->Cs[FAB_CAP_LANE] |= FAB_CAP_TMU_ADP;
            ad->Cs[FAB_CAP_TMU_ADP] = 0x03UL << 8;
            ad->Cs[FAB_CAP_TMU_ADP + 3] = 0;
        }
    }
    return 0;
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
            (f->R[0].Tbt3 ? (a & 1U) != 0 : f->R[0].Adp[a].Cs[FAB_CAP_PORT] != 0)) {
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

/* A Gen 4 link's lanes at both ends: Current Link Speed Gen 4, CL0, the
 * widths the device router's preset names (task 45.20). */
static void fabGen4Lanes(FAB *f, int pr, unsigned pa, int dev)
{
    FAB_ROUTER *p = &f->R[pr];
    FAB_ROUTER *d = &f->R[dev];
    unsigned da = d->UpAdp;
    unsigned k;

    for (k = 0; k < 2; k++) {
        if (p->Adp[pa + k].Present) {
            setLaneSpeed(&p->Adp[pa + k], 0x2UL);
            setLaneState(&p->Adp[pa + k], 2);
            setLaneWidth(&p->Adp[pa + k], d->Gen4HostW != 0 ? d->Gen4HostW : 2UL);
        }
        if (d->Adp[da + k].Present) {
            setLaneSpeed(&d->Adp[da + k], 0x2UL);
            setLaneState(&d->Adp[da + k], 2);
            setLaneWidth(&d->Adp[da + k], d->Gen4DevW != 0 ? d->Gen4DevW : 2UL);
        }
    }
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
    /* Two version 2 routers: Gen 4, its two lanes aggregated by link
     * initialisation (Spec 4.2.2), the widths on both lanes of each end. */
    d->Gen4Up = p->Gen4 && d->Gen4;
    if (d->Gen4Up) {
        fabGen4Lanes(f, pr, pa, dev);
    } else if (((p->Adp[pa].Cs[FAB_CAP_LANE + 1] >> 16) & 0xFUL) == 0x2UL) {
        setLaneSpeed(&p->Adp[pa], 0x4UL);
        if (p->Adp[pa + 1].Present) {
            setLaneSpeed(&p->Adp[pa + 1], 0x4UL);
        }
    }
}

void fabPlug(FAB *f, int port, int dev)
{
    int a = fabPortLane0(f, port);

    fabConnect(f, 0, (unsigned)a, dev);
    if (f->R[0].Tbt3 && (f->R[0].Rcs[3] & 0x80000000UL) == 0) {
        return;     /* a Thunderbolt host router reports once configured */
    }
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
    r->RrPending = 0;
    r->ReconnectPending = 0;
    r->ReconnectArmed = 0;
    if (!r->Tbt3) {
        if (r->TmuAt != 0) {
            r->Rcs[r->TmuAt] &= 0x0000FFFFUL;
            r->Rcs[r->TmuAt + 3] = 0;
            r->Rcs[r->TmuAt + 15] = 0;
            for (h = 22; h <= 25; h++) {
                r->Rcs[r->TmuAt + h] = 0;
            }
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
            ad->Cs[4] = (ad->Cs[4] & ~(0x3FFUL << 20)) |
                        ((r->Kind == FAB_DEV_ASM2464 ? 66UL : 60UL) << 20);
            if (ad->Cs[FAB_CAP_PORT] != 0) {
                ad->Cs[FAB_CAP_PORT + 19] = 0;
            }
            ad->Cs[FAB_CAP_TMU_ADP + 3] = 0;
            ad->Cs[FAB_CAP_TMU_ADP + 6] = 0;
            ad->Cs[FAB_CAP_TMU_ADP + 8] = 0;
            ad->Cs[FAB_CAP_TMU_ADP + 9] = 0;
        } else if (t != 2 && r->Kind == FAB_DEV_ASM2464) {
            ad->Path[2 * 8] = t == 0x100102UL ? 0x00000008UL : 0;
            ad->Path[2 * 8 + 1] = 0;
            ad->Cs[FAB_CAP_ADP] &= ~0xC0000000UL;
        } else if (t != 2) {
            ad->Path[2 * 8] = 7UL << 17;
            ad->Path[2 * 8 + 1] = 0x01000000UL;
            ad->Cs[FAB_CAP_ADP] &= ~0xC0000000UL;
            if (fabPcieAdp(r, a)) {
                ad->Cs[FAB_CAP_ADP + 1] = 0;    /* Extended Encapsulation 0 */
            }
        }
    }
    if (!r->Tbt3) {
        /* Uninitialised again: every capability back to disabled (Spec
         * 8.3.1.3.2), on every USB4 router, the host's included. */
        r->CapsData &= 0x55555555UL;
        r->SetCapsOps = 0;
    }
}

/* A device router losing power (fabDisconnect) or the domain's (fabPowerCycle):
 * its downstream lanes' Lock set again, as after power on; with `deep`
 * the router behind each downstream port loses its upstream link and
 * resets too (Spec 3.3), still attached there. The host-only reset
 * (fabResetHost) never comes here. */
static void fabLockDownstream(FAB *f, int d, int deep)
{
    FAB_ROUTER *r = &f->R[d];
    unsigned a;
    int c;

    for (a = 1; a <= r->MaxAdp; a++) {
        if (!r->Adp[a].Present || (r->Adp[a].Cs[2] & 0xFFFFFFUL) != 1 ||
            a == r->UpAdp || a == r->UpAdp + 1) {
            continue;
        }
        if (!r->Tbt3) {
            /* No Lock on a Thunderbolt 3 router's lanes (fabHotPlugChild). */
            r->Adp[a].Cs[4] |= 0x80000000UL;
        }
        c = r->Adp[a].Peer;
        if (deep && !r->Adp[a].Lane1 && c > 0 && c != d && f->R[c].Parent == d) {
            fabResetRouter(f, c);
            fabLockDownstream(f, c, 1);
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
        /* Lane Disable is left as the connection manager wrote it: an
         * unplug does not reset the host router, so only the driver's own
         * write clears it. */
    }
    if (dev >= 0) {
        /* Its configuration and that of every router behind it go; the
         * routers behind it stay attached to its downstream ports. */
        fabResetRouter(f, dev);
        fabLockDownstream(f, dev, 1);
        f->R[dev].Connected = 0;
        for (i = 1; i <= f->R[dev].MaxAdp; i++) {
            if (i == f->R[dev].UpAdp || i == f->R[dev].UpAdp + 1) {
                f->R[dev].Adp[i].Peer = -1;
            }
        }
        f->R[dev].Gen4Up = 0;
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
            fabLockDownstream(f, i, 0);
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
            if (f->R[i].Gen4Up && f->R[i].Parent >= 0) {
                /* A Gen 4 link retrains as Gen 4 (task 45.20). */
                fabGen4Lanes(f, f->R[i].Parent, (unsigned)f->R[i].ParentAdp, i);
            }
        }
    }
    f->RxQCount = 0;
}

void fabResetHost(FAB *f)
{
    unsigned a;
    unsigned long lane[FAB_MAX_ADP];
    unsigned long cs4[FAB_MAX_ADP];

    /* The router's configuration and paths reset; its lane adapters keep
     * the link as trained (the link itself stayed up), Lock set again. */
    for (a = 1; a <= f->R[0].MaxAdp && a < FAB_MAX_ADP; a++) {
        lane[a] = f->R[0].Adp[a].Cs[FAB_CAP_LANE + 1];
        cs4[a] = f->R[0].Adp[a].Cs[4];
    }
    fabResetRouter(f, 0);
    for (a = 1; a <= f->R[0].MaxAdp && a < FAB_MAX_ADP; a++) {
        if (f->R[0].Adp[a].Present && (f->R[0].Adp[a].Cs[2] & 0xFFFFFFUL) == 1) {
            f->R[0].Adp[a].Cs[FAB_CAP_LANE + 1] = lane[a];
            f->R[0].Adp[a].Cs[4] = cs4[a] | 0x80000000UL;
        }
    }
    f->TxCtrl = 0;
    f->RxCtrl = 0;
    f->TxSize = 0;
    f->RxSizeReg = 0;
    f->RxSize = 0;
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

unsigned long fabTriedTo(const FAB *f, unsigned long from, int r)
{
    unsigned long hi;
    unsigned long lo;
    unsigned long i;
    unsigned long n = 0;

    fabRouteOf(f, r, &hi, &lo);
    for (i = from; i < f->TriedCount && i < FAB_WRLOG; i++) {
        if (f->TriedHi[i] == hi && f->TriedLo[i] == lo) {
            n++;
        }
    }
    return n;
}

int fabDownLane0(const FAB *f, int dev)
{
    return fabDownLaneN(&f->R[dev], 1);
}

void fabHotPlugChild(FAB *f, int dev, int child)
{
    unsigned a = (unsigned)fabDownLaneN(&f->R[dev], 1);

    fabConnect(f, dev, a, child);
    if (f->R[dev].Tbt3) {
        /* No Lock on a Thunderbolt 3 router's lanes ([linux] switch.c
         * tb_port_unlock writes it only on a USB4 router). */
        f->R[dev].Adp[a].Cs[4] &= ~0x80000000UL;
        f->R[dev].Adp[a + 1].Cs[4] &= ~0x80000000UL;
    }
    if (f->R[dev].Tbt3 || (f->R[dev].Adp[a].Cs[5] & 0x80000000UL) == 0) {
        fabHotPlug(f, dev, (unsigned long)a, 0);
    }
}

static void fabSetPath(FAB *f, int r, unsigned a, unsigned hop, unsigned outAdp,
                       unsigned outHop, unsigned credits);

void fabPreosChild(FAB *f, int dev, int child, int stray)
{
    unsigned a = (unsigned)fabDownLaneN(&f->R[dev], 1);
    unsigned dp = 0;
    unsigned cu = 0;
    unsigned cl = f->R[child].UpAdp;
    unsigned i;

    fabPlugChild(f, dev, child, 1);
    f->R[dev].Adp[a + 1].Cs[4] &= ~0x80000000UL;
    f->R[child].Rcs[4] |= 0x1000UL | 0xFFUL;
    f->R[child].Rcs[5] = 0x81000000UL;
    f->R[child].Rcs[6] |= 0x03000000UL;
    setLaneWidth(&f->R[dev].Adp[a], 2);
    setLaneWidth(&f->R[dev].Adp[a + 1], 2);
    setLaneWidth(&f->R[child].Adp[cl], 2);
    setLaneWidth(&f->R[child].Adp[cl + 1], 2);
    for (i = 1; i <= f->R[dev].MaxAdp; i++) {
        if (dp == 0 && (f->R[dev].Adp[i].Cs[2] & 0xFFFFFFUL) == 0x100101UL) {
            dp = i;
        }
    }
    for (i = 1; i <= f->R[child].MaxAdp; i++) {
        if (cu == 0 && (f->R[child].Adp[i].Cs[2] & 0xFFFFFFUL) == 0x100102UL) {
            cu = i;
        }
    }
    fabSetPath(f, dev, dp, 8, a, 8, 7);
    fabSetPath(f, child, cl, 8, cu, 8, 32);
    fabSetPath(f, child, cu, 8, cl, 9, 7);
    fabSetPath(f, dev, a, 9, dp, 8, 32);
    f->R[dev].Adp[dp].Cs[FAB_CAP_ADP] |= 0x80000000UL;
    f->R[child].Adp[cu].Cs[FAB_CAP_ADP] |= 0x80000000UL;
    fabPcieUpdate(f, dev, dp);
    if (stray) {
        /* A predecessor's second path through the link: lane 0's HopID
         * 10 into the parent's lane 1 back toward the host, as a
         * DisplayPort or USB3 path through the parent would cross it. */
        fabSetPath(f, dev, a, 10, f->R[dev].UpAdp, 12, 16);
    }
}

void fabHotUnplugChild(FAB *f, int dev)
{
    unsigned a = (unsigned)fabDownLaneN(&f->R[dev], 1);
    unsigned i;

    fabDisconnect(f, dev, a);
    /* The parent's protocol adapters toward the departed child follow
     * (Spec 4.4.5.2, as fabUnplug models the host's). */
    for (i = 1; i <= f->R[dev].MaxAdp; i++) {
        if (fabPcieAdp(&f->R[dev], i)) {
            fabPcieUpdate(f, dev, i);
        }
    }
    if (f->R[dev].Tbt3 || (f->R[dev].Adp[a].Cs[5] & 0x80000000UL) == 0) {
        fabHotPlug(f, dev, (unsigned long)a, 1);
    }
}

int fabChainPcieUp(const FAB *f, int dev)
{
    unsigned a = (unsigned)fabDownLaneN(&f->R[dev], 1);
    int child;
    unsigned i;
    unsigned dp = 0;
    unsigned cu = 0;

    if (a == 0) {
        return 0;
    }
    child = f->R[dev].Adp[a].Peer;
    if (child < 0) {
        return 0;
    }
    for (i = 1; i <= f->R[dev].MaxAdp; i++) {
        if (dp == 0 && (f->R[dev].Adp[i].Cs[2] & 0xFFFFFFUL) == 0x100101UL) {
            dp = i;
        }
    }
    for (i = 1; i <= f->R[child].MaxAdp; i++) {
        if (cu == 0 && (f->R[child].Adp[i].Cs[2] & 0xFFFFFFUL) == 0x100102UL) {
            cu = i;
        }
    }
    if (dp == 0 || cu == 0) {
        return 0;
    }
    {
        /* The four entries of record 34 section 6.6, chained: the
         * parent's PCIe Down HopID 8 into its lane at hd, the child's lane
         * hd to its PCIe Up 8, its PCIe Up 8 to its lane at hu, the
         * parent's lane hu back to the PCIe Down 8. */
        unsigned cl = f->R[child].UpAdp;
        unsigned long e;
        unsigned hd;
        unsigned hu;

        e = f->R[dev].Adp[dp].Path[2 * 8];
        if (!(e & 0x80000000UL) || ((e >> 11) & 0x3FUL) != a) {
            return 0;
        }
        hd = (unsigned)(e & 0x7FUL);
        e = f->R[child].Adp[cu].Path[2 * 8];
        if (!(e & 0x80000000UL) || ((e >> 11) & 0x3FUL) != cl) {
            return 0;
        }
        hu = (unsigned)(e & 0x7FUL);
        if (hd >= FAB_HOPS || hu >= FAB_HOPS) {
            return 0;
        }
        e = f->R[child].Adp[cl].Path[2 * hd];
        if (!(e & 0x80000000UL) || ((e >> 11) & 0x3FUL) != cu || (e & 0x7FUL) != 8) {
            return 0;
        }
        e = f->R[dev].Adp[a].Path[2 * hu];
        if (!(e & 0x80000000UL) || ((e >> 11) & 0x3FUL) != dp || (e & 0x7FUL) != 8) {
            return 0;
        }
    }
    return (f->R[dev].Adp[dp].Cs[FAB_CAP_ADP] & 0x80000000UL) &&
           (f->R[child].Adp[cu].Cs[FAB_CAP_ADP] & 0x80000000UL) &&
           ((f->R[child].Adp[cu].Cs[FAB_CAP_ADP] >> 25) & 0xF) == 6;
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
    unsigned k;

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
    /* The device's first PCIe Up and USB3 Up adapters (5 and 6 on the
     * generic device router, 3 and 4 on the ASM2464). */
    dpu = 0;
    du3 = 0;
    for (k = f->R[d].MaxAdp; k >= 1; k--) {
        if ((f->R[d].Adp[k].Cs[2] & 0xFFFFFFUL) == 0x100102UL) {
            dpu = k;
        } else if ((f->R[d].Adp[k].Cs[2] & 0xFFFFFFUL) == 0x200102UL) {
            du3 = k;
        }
    }
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
                   ((f->R[0].Adp[hp].Cs[FAB_CAP_ADP] >> 25) & 0xF) ==
                   (f->R[0].Tbt3 ? 3UL : 6UL);
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
