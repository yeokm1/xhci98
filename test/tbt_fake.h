/*
 * tbt_fake.h - a modelled Thunderbolt/USB4 host interface for the host
 * suites test_tbt_ring, test_tbt_icm and test_tbt_hw (roadmap-usb4.md
 * tasks 39-A.2 and 39-A.3).
 *
 * The model is written from the USB4 Specification 2.0 (sections 12.3,
 * 12.4 and 12.6: the descriptor fields, the index registers and their
 * vacancy rule, Raw Mode) and ICM-PROTOCOL.md sections 2.1 to 2.3 (the
 * Alpine Ridge firmware status, mailbox and mode registers), not from the
 * code under test. It has its own CRC-32C, so the suites check the
 * driver's against Spec Appendix A.2 and against this one independently.
 *
 * One instance (g_fake), header-only and all-static, as test_harness.h is.
 * A virtual clock in microseconds advances only when the code under test
 * stalls or sleeps; frames queued for the receive ring carry the time they
 * arrive, so a firmware that answers 358 ms later is modelled exactly.
 * C89.
 */

#ifndef TBT_FAKE_H
#define TBT_FAKE_H

#include <string.h>
#include "../src/cm/tbt_ring.h"

#define FAKE_PHYS           0x00200000UL
#define FAKE_MEM_BYTES      16384
#define FAKE_RXQ            128
#define FAKE_TXLOG          512
#define FAKE_FRAME_BYTES    512
#define FAKE_MBOX_LOG       64
#define FAKE_CFG_DWORDS     64

/* FAKE_FRAME flags */
#define FAKE_FLAG_BO        0x01    /* report Buffer Overflow */
#define FAKE_FLAG_BAD_CRC   0x02    /* corrupt the trailer */
#define FAKE_FLAG_RAW_LEN   0x04    /* Len is reported as given, bytes truncated */

/* Mailbox behaviour */
#define FAKE_MBOX_COMPLETE  0
#define FAKE_MBOX_ERROR     1
#define FAKE_MBOX_HANG      2

typedef struct FAKE_FRAME {
    unsigned long Pdf;
    unsigned long Len;
    unsigned long AtUs;
    unsigned long Flags;
    unsigned char Bytes[FAKE_FRAME_BYTES];
} FAKE_FRAME;

struct FAKE;
typedef void (*FAKE_ON_TX)(struct FAKE *f, unsigned long pdf,
                           const unsigned long *dw, unsigned long words);

typedef struct FAKE {
    unsigned long NowUs;
    int ClockStopped;

    /* BAR0 */
    unsigned long Caps;
    unsigned long FwStatus;
    unsigned long Outmail;
    unsigned long HiControl;
    unsigned long HiResetWrites;
    unsigned long InmailData;
    unsigned long InmailCmd;
    int MboxMode;
    unsigned long MboxLog[FAKE_MBOX_LOG];
    unsigned long MboxCount;
    unsigned long Imr[4];
    unsigned long ImrWrites;
    unsigned long ImcWrites;
    unsigned long ImcLast;
    unsigned long Writes;           /* every BAR0 write */
    unsigned long FwStatusReads;
    unsigned long FwStatusNextAfter;    /* after this many reads, FwStatusNext; 0 never */
    unsigned long FwStatusNext;
    unsigned long FloodPdf;         /* nonzero: the receive queue never runs dry */
    unsigned long FloodWords;       /* with FloodDw: the frame it repeats */
    unsigned long FloodDw[64];

    unsigned long TxBaseLo, TxBaseHi, TxSize, TxCtrl, TxProd, TxCons;
    int TxConsume;                  /* the host interface fetches TX */
    unsigned long TxIndexOverride;  /* nonzero: the TX index register reads this */
    unsigned long RxBaseLo, RxBaseHi, RxSize, RxSizeReg, RxCtrl, RxPdf, RxProd, RxCons;
    unsigned long Ring1Ctrl;        /* TX Ring Control of ring 1, read only */

    unsigned long MemDw[FAKE_MEM_BYTES / 4];   /* DW-aligned host memory */

    FAKE_FRAME RxQ[FAKE_RXQ];
    unsigned long RxQCount;
    unsigned long RxDropped;
    unsigned long RxPosted;

    unsigned long TxLogPdf[FAKE_TXLOG];
    unsigned long TxLogWords[FAKE_TXLOG];
    unsigned long TxLogAtUs[FAKE_TXLOG];
    unsigned long TxLogDw[FAKE_TXLOG][64];
    unsigned long TxCount;
    unsigned long TxBadCrc;

    FAKE_ON_TX OnTx;

    /* PCI configuration space of the NHI, for test_tbt_hw */
    unsigned long Cfg[FAKE_CFG_DWORDS];
    unsigned long CfgWrites;
    unsigned long Vs9ReadyAfterReads;   /* FW_READY after this many reads, 0 never */
    unsigned long Vs9Reads;
    unsigned long Vs18DoneAfterReads;
    unsigned long Vs18Reads;
    int CfgAllOnes;

    /* the log hook */
    unsigned long LogCount;
    const char *LastLabel;
    unsigned long LastValue;
} FAKE;

static FAKE g_fake;

static unsigned char *fakeMem(void)
{
    return (unsigned char *)g_fake.MemDw;
}

/* Spec 6.4.2.2 Table 6-1, written separately from the driver's. */
static unsigned long fakeCrc(const unsigned char *p, unsigned long n)
{
    unsigned long crc = 0xFFFFFFFFUL;
    unsigned long i;
    int k;

    for (i = 0; i < n; i++) {
        crc ^= p[i];
        for (k = 0; k < 8; k++) {
            if (crc & 1UL) {
                crc = (crc >> 1) ^ 0x82F63B78UL;
            } else {
                crc >>= 1;
            }
        }
    }
    return (crc ^ 0xFFFFFFFFUL) & 0xFFFFFFFFUL;
}

static void fakePutBe(unsigned char *p, unsigned long v)
{
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
}

static unsigned long fakeGetBe(const unsigned char *p)
{
    return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) |
           ((unsigned long)p[2] << 8) | (unsigned long)p[3];
}

static void fakePutLe(unsigned char *p, unsigned long v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

static unsigned long fakeGetLe(const unsigned char *p)
{
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8) |
           ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

static unsigned char *fakeAt(unsigned long phys)
{
    if (phys < FAKE_PHYS || phys >= FAKE_PHYS + FAKE_MEM_BYTES) {
        return NULL;
    }
    return fakeMem() + (phys - FAKE_PHYS);
}

/* Queue a frame for the receive ring, `delayUs` from now. */
static void fakeQueue(unsigned long pdf, const unsigned long *dw,
                      unsigned long words, unsigned long delayUs,
                      unsigned long flags)
{
    FAKE_FRAME *fr;
    unsigned long i;
    unsigned long crc;

    unsigned long at;
    unsigned long pos;

    if (g_fake.RxQCount >= FAKE_RXQ) {
        return;
    }
    /* Kept in arrival order: by time, and in queueing order for equal
     * times. */
    /* With the clock stopped, everything arrives now. */
    at = g_fake.ClockStopped ? g_fake.NowUs : g_fake.NowUs + delayUs;
    pos = g_fake.RxQCount;
    while (pos > 0 && g_fake.RxQ[pos - 1].AtUs > at) {
        g_fake.RxQ[pos] = g_fake.RxQ[pos - 1];
        pos--;
    }
    g_fake.RxQCount++;
    fr = &g_fake.RxQ[pos];
    memset(fr, 0, sizeof(*fr));
    fr->Pdf = pdf;
    fr->AtUs = at;
    fr->Flags = flags;
    for (i = 0; i < words && 4 * i + 8 <= FAKE_FRAME_BYTES; i++) {
        fakePutBe(fr->Bytes + 4 * i, dw[i]);
    }
    crc = fakeCrc(fr->Bytes, 4 * words);
    if (flags & FAKE_FLAG_BAD_CRC) {
        crc ^= 0x00010000UL;
    }
    fakePutBe(fr->Bytes + 4 * words, crc);
    fr->Len = 4 * words + 4;
}

/* Queue raw bytes with an arbitrary reported length (malformed cases). */
static void fakeQueueRaw(unsigned long pdf, unsigned long len,
                         unsigned long flags)
{
    FAKE_FRAME *fr;

    if (g_fake.RxQCount >= FAKE_RXQ) {
        return;
    }
    fr = &g_fake.RxQ[g_fake.RxQCount++];
    memset(fr, 0, sizeof(*fr));
    fr->Pdf = pdf;
    fr->AtUs = g_fake.NowUs;
    fr->Flags = flags | FAKE_FLAG_RAW_LEN;
    fr->Len = len;
}

/* Spec 12.4.2.2: a packet takes the descriptor at the Producer Index if one
 * is available (PI != CI), else it is discarded. */
static void fakePump(void)
{
    unsigned long i;
    unsigned char *d;
    unsigned char *buf;
    unsigned long copy;
    unsigned long dw2;
    FAKE_FRAME *fr;

    if (g_fake.FloodPdf != 0 && g_fake.RxQCount == 0) {
        if (g_fake.FloodWords != 0) {
            fakeQueue(g_fake.FloodPdf, g_fake.FloodDw, g_fake.FloodWords, 0, 0);
        } else {
            unsigned long ev[2];
            ev[0] = 0x01000099UL;
            ev[1] = 0;
            fakeQueue(g_fake.FloodPdf, ev, 2, 0, 0);
        }
    }

    while (g_fake.RxQCount != 0 && g_fake.RxQ[0].AtUs <= g_fake.NowUs) {
        fr = &g_fake.RxQ[0];
        if ((g_fake.RxCtrl & 0x80000000UL) == 0 || g_fake.RxSize == 0 ||
            g_fake.RxProd == g_fake.RxCons) {
            g_fake.RxDropped++;
        } else {
            d = fakeAt(g_fake.RxBaseLo + 16 * g_fake.RxProd);
            buf = d ? fakeAt(fakeGetLe(d)) : NULL;
            if (d != NULL && buf != NULL) {
                copy = fr->Len;
                if (copy > 256) {
                    copy = 256;
                }
                if (copy > FAKE_FRAME_BYTES) {
                    copy = FAKE_FRAME_BYTES;
                }
                memcpy(buf, fr->Bytes, copy);
                dw2 = (fr->Len & 0xFFFUL) | ((fr->Pdf & 0xFUL) << 12) |
                      0x00200000UL;
                if ((fr->Flags & FAKE_FLAG_BO) || fr->Len > 256) {
                    dw2 |= 0x00400000UL;
                }
                fakePutLe(d + 8, dw2);
                g_fake.RxPosted++;
            }
            g_fake.RxProd = (g_fake.RxProd + 1) % g_fake.RxSize;
        }
        for (i = 1; i < g_fake.RxQCount; i++) {
            g_fake.RxQ[i - 1] = g_fake.RxQ[i];
        }
        g_fake.RxQCount--;
    }
}

static void fakeProcessTx(void)
{
    unsigned char *d;
    unsigned char *buf;
    unsigned long dw2;
    unsigned long len;
    unsigned long pdf;
    unsigned long words;
    unsigned long i;
    unsigned long n;
    unsigned long dw[64];

    while ((g_fake.TxCtrl & 0x80000000UL) && g_fake.TxConsume &&
           g_fake.TxSize != 0 && g_fake.TxCons != g_fake.TxProd) {
        d = fakeAt(g_fake.TxBaseLo + 16 * g_fake.TxCons);
        if (d == NULL) {
            break;
        }
        dw2 = fakeGetLe(d + 8);
        len = dw2 & 0xFFFUL;
        pdf = (dw2 >> 12) & 0xFUL;
        buf = fakeAt(fakeGetLe(d));
        if (dw2 & 0x00400000UL) {
            fakePutLe(d + 8, dw2 | 0x00200000UL);
        }
        g_fake.TxCons = (g_fake.TxCons + 1) % g_fake.TxSize;
        if (buf == NULL || len < 8 || (len & 3) != 0) {
            continue;
        }
        words = len / 4 - 1;
        for (i = 0; i < words && i < 64; i++) {
            dw[i] = fakeGetBe(buf + 4 * i);
        }
        if (fakeCrc(buf, len - 4) != fakeGetBe(buf + len - 4)) {
            g_fake.TxBadCrc++;
        }
        n = g_fake.TxCount;
        if (n < FAKE_TXLOG) {
            g_fake.TxLogPdf[n] = pdf;
            g_fake.TxLogWords[n] = words;
            g_fake.TxLogAtUs[n] = g_fake.NowUs;
            memcpy(g_fake.TxLogDw[n], dw, sizeof(unsigned long) * (words < 64 ? words : 64));
        }
        g_fake.TxCount++;
        if (g_fake.OnTx != NULL) {
            g_fake.OnTx(&g_fake, pdf, dw, words);
        }
    }
    fakePump();
}

static ULONG fakeRead32(PVOID ctx, ULONG off)
{
    (void)ctx;
    fakeProcessTx();
    fakePump();
    switch (off) {
    case 0x39640: return g_fake.Caps;
    case 0x39944:
        g_fake.FwStatusReads++;
        if (g_fake.FwStatusNextAfter != 0 &&
            g_fake.FwStatusReads > g_fake.FwStatusNextAfter) {
            g_fake.FwStatus = g_fake.FwStatusNext;
        }
        return g_fake.FwStatus;
    case 0x3990C: return g_fake.Outmail;
    case 0x39900: return g_fake.InmailData;
    case 0x39904: return g_fake.InmailCmd;
    case 0x39858: return 0;
    case 0x39864: return g_fake.HiControl;
    case 0x00000: return g_fake.TxBaseLo;
    case 0x00004: return g_fake.TxBaseHi;
    case 0x00008:
        if (g_fake.TxIndexOverride != 0) {
            return g_fake.TxIndexOverride;
        }
        return (g_fake.TxProd << 16) | g_fake.TxCons;
    case 0x0000C: return g_fake.TxSize;
    case 0x19800: return g_fake.TxCtrl;
    case 0x19820: return g_fake.Ring1Ctrl;
    case 0x08000: return g_fake.RxBaseLo;
    case 0x08004: return g_fake.RxBaseHi;
    case 0x08008: return (g_fake.RxProd << 16) | g_fake.RxCons;
    case 0x0800C: return g_fake.RxSizeReg;
    case 0x29800: return g_fake.RxCtrl;
    case 0x29804: return g_fake.RxPdf;
    }
    if (off >= 0x38200 && off < 0x38208) {
        return g_fake.Imr[(off - 0x38200) / 4];
    }
    return 0;
}

static void fakeWrite32(PVOID ctx, ULONG off, ULONG v)
{
    (void)ctx;
    g_fake.Writes++;
    switch (off) {
    case 0x39900: g_fake.InmailData = v; return;
    case 0x39904:
        if (v & 0x80000000UL) {
            if (g_fake.MboxCount < FAKE_MBOX_LOG) {
                g_fake.MboxLog[g_fake.MboxCount] = v & 0xFFUL;
            }
            g_fake.MboxCount++;
            if (g_fake.MboxMode == FAKE_MBOX_COMPLETE) {
                v &= ~0xC0000000UL;
            } else if (g_fake.MboxMode == FAKE_MBOX_ERROR) {
                v = (v & ~0x80000000UL) | 0x40000000UL;
            }
        }
        g_fake.InmailCmd = v;
        return;
    case 0x39858: g_fake.HiResetWrites++; return;
    case 0x39944: g_fake.FwStatus = v; return;
    case 0x00000: g_fake.TxBaseLo = v; g_fake.TxProd = g_fake.TxCons = 0; return;
    case 0x00004: g_fake.TxBaseHi = v; g_fake.TxProd = g_fake.TxCons = 0; return;
    case 0x00008: g_fake.TxProd = (v >> 16) & 0xFFFFUL; fakeProcessTx(); return;
    case 0x0000C: g_fake.TxSize = v & 0xFFFFUL; g_fake.TxProd = g_fake.TxCons = 0; return;
    case 0x19800: g_fake.TxCtrl = v; return;
    case 0x08000: g_fake.RxBaseLo = v; g_fake.RxProd = g_fake.RxCons = 0; return;
    case 0x08004: g_fake.RxBaseHi = v; g_fake.RxProd = g_fake.RxCons = 0; return;
    case 0x08008: g_fake.RxCons = v & 0xFFFFUL; fakePump(); return;
    case 0x0800C: g_fake.RxSizeReg = v; g_fake.RxSize = v & 0xFFFFUL; g_fake.RxProd = g_fake.RxCons = 0; return;
    case 0x29800: g_fake.RxCtrl = v; return;
    case 0x29804: g_fake.RxPdf = v; return;
    }
    if (off >= 0x38200 && off < 0x38208) {
        g_fake.Imr[(off - 0x38200) / 4] = v;
        g_fake.ImrWrites++;
        return;
    }
    if (off >= 0x38208 && off < 0x38210) {
        g_fake.ImcWrites++;
        g_fake.ImcLast = v;
    }
}

static void fakeStall(PVOID ctx, ULONG us)
{
    (void)ctx;
    if (!g_fake.ClockStopped) {
        g_fake.NowUs += us;
    }
    fakeProcessTx();
    fakePump();
}

static void fakeSleep(PVOID ctx, ULONG ms)
{
    (void)ctx;
    if (!g_fake.ClockStopped) {
        g_fake.NowUs += ms * 1000UL;
    }
    fakeProcessTx();
    fakePump();
}

static ULONG fakeNowMs(PVOID ctx)
{
    (void)ctx;
    return g_fake.NowUs / 1000UL;
}

static ULONG fakeClockUs(PVOID ctx)
{
    (void)ctx;
    return g_fake.NowUs;
}

static void fakeLog(PVOID ctx, const char *label, ULONG value)
{
    (void)ctx;
    g_fake.LogCount++;
    g_fake.LastLabel = label;
    g_fake.LastValue = value;
}

static const TBT_HOST g_fakeHost = {
    NULL, fakeRead32, fakeWrite32, fakeStall, fakeSleep, fakeNowMs, fakeLog,
    fakeClockUs, 0
};

/* An Alpine Ridge at rest (ICM-PROTOCOL.md 2.1 [obs]). */
static void fakeReset(void)
{
    memset(&g_fake, 0, sizeof(g_fake));
    g_fake.Caps = 0x0000000CUL;
    g_fake.FwStatus = 0x800001A1UL;
    g_fake.Outmail = 0x800003F5UL;
    g_fake.HiControl = 0x01000016UL;
    g_fake.InmailCmd = 0x00000007UL;
    g_fake.TxConsume = 1;
}

/* The NHI's configuration space (VS_CAP_* live at 0xC8..0xFC). */
static ULONG fakeCfgRead(PVOID ctx, ULONG off)
{
    (void)ctx;
    if (g_fake.CfgAllOnes) {
        return 0xFFFFFFFFUL;
    }
    if (off == 0xC8) {
        g_fake.Vs9Reads++;
        if (g_fake.Vs9ReadyAfterReads != 0 &&
            g_fake.Vs9Reads >= g_fake.Vs9ReadyAfterReads) {
            return g_fake.Cfg[off / 4] | 0x80000000UL;
        }
        return g_fake.Cfg[off / 4] & ~0x80000000UL;
    }
    if (off == 0xEC) {
        g_fake.Vs18Reads++;
        if (g_fake.Vs18DoneAfterReads != 0 &&
            g_fake.Vs18Reads >= g_fake.Vs18DoneAfterReads) {
            return 1;
        }
        return 0;
    }
    if (off / 4 < FAKE_CFG_DWORDS) {
        return g_fake.Cfg[off / 4];
    }
    return 0xFFFFFFFFUL;
}

static void fakeCfgWrite(PVOID ctx, ULONG off, ULONG v)
{
    (void)ctx;
    g_fake.CfgWrites++;
    if (off / 4 < FAKE_CFG_DWORDS) {
        g_fake.Cfg[off / 4] = v;
    }
}

#endif /* TBT_FAKE_H */
