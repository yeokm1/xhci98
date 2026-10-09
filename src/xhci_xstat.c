/*
 * xhci_xstat.c - the transfer-size instrumentation's arithmetic and the
 * command-size value's rule (roadmap-hcd.md tasks 36.21 and 36.22;
 * xhci_xstat.h). The host suite test_snap calls these as the driver does.
 *
 * DDK-free: part of the pure core. C89, pure: IRQL any.
 */

#include "xhci_xstat.h"

/* The snapshot copies the structure word for word (xhci.h,
 * XHCI_SNAPSHOT_HCD_XSTAT_WORDS). */
XHCI_C_ASSERT(xhci_xstat_words,
              sizeof(XHCI_XSTAT) == XHCI_XSTAT_WORDS * sizeof(ULONG));

ULONG XhciXpolValue(ULONG found, ULONG value, PULONG why)
{
    ULONG w;

    if (found == XHCI_XPOL_FOUND_NONE) {
        w = XHCI_XPOL_WHY_ABSENT;
    } else if (found != XHCI_XPOL_FOUND_DWORD) {
        w = XHCI_XPOL_WHY_TYPE;
    } else if (value < XHCI_XPOL_MIN_KB || value > XHCI_XPOL_MAX_KB) {
        w = XHCI_XPOL_WHY_RANGE;
    } else if ((value % XHCI_XPOL_STEP_KB) != 0) {
        w = XHCI_XPOL_WHY_STEP;
    } else {
        *why = XHCI_XPOL_WHY_USED;
        return value * 1024UL;
    }
    *why = w;
    return XHCI_XPOL_DEFAULT_KB * 1024UL;
}

ULONG XhciXstatBucket(ULONG bytes)
{
    ULONG limit;
    ULONG i;

    /* 4 KiB, then 16 and 64, then doubling to 2 MiB. */
    limit = 0x1000UL;
    for (i = 0; i < XHCI_XSTAT_BUCKETS - 1; i++) {
        if (bytes <= limit) {
            return i;
        }
        limit = (i < 2) ? limit * 4UL : limit * 2UL;
    }
    return XHCI_XSTAT_BUCKETS - 1;
}

ULONG XhciXstatChunkBucket(ULONG chunks)
{
    if (chunks <= 1) {
        return 0;
    }
    if (chunks == 2) {
        return 1;
    }
    if (chunks <= 8) {
        return 2;
    }
    if (chunks <= 16) {
        return 3;
    }
    return 4;
}

ULONG XhciXstatDepthBucket(ULONG active)
{
    ULONG limit;
    ULONG i;

    limit = 1;
    for (i = 0; i < XHCI_XSTAT_DEPTH_BUCKETS - 1; i++) {
        if (active <= limit) {
            return i;
        }
        limit *= 2UL;
    }
    return XHCI_XSTAT_DEPTH_BUCKETS - 1;
}

VOID XhciXstatAdd64(PULONG lo, PULONG hi, ULONG v)
{
    ULONG before;

    before = *lo;
    *lo = before + v;
    if (*lo < before) {
        *hi += 1UL;
    }
}

ULONG XhciXstatStamped(const XHCI_XSTAT_STAMP *s)
{
    return s->Lo != 0 || s->Hi != 0;
}

VOID XhciXstatClear(PXHCI_XSTAT_STAMP s)
{
    s->Lo = 0;
    s->Hi = 0;
    s->Cpu = 0;
}

ULONG XhciXstatDiff(PULONG discarded, PULONG crossed,
                    const XHCI_XSTAT_STAMP *from, const XHCI_XSTAT_STAMP *to,
                    PULONG counts)
{
    ULONG d;
    ULONG hi;

    if (!XhciXstatStamped(from) || !XhciXstatStamped(to)) {
        return 0;
    }
    if (from->Cpu != to->Cpu) {
        (*crossed)++;
        return 0;
    }
    /* The 64-bit difference by hand: a borrow from the low half, and any
     * high half left over is an interval far past the limit, or negative. */
    d = to->Lo - from->Lo;
    hi = to->Hi - from->Hi - ((to->Lo < from->Lo) ? 1UL : 0UL);
    if (hi != 0 || d > XHCI_XSTAT_INTERVAL_MAX) {
        (*discarded)++;
        return 0;
    }
    *counts = d;
    return 1;
}

VOID XhciXstatAddTime(PXHCI_XSTAT_TIME t, ULONG counts)
{
    t->Count++;
    XhciXstatAdd64(&t->SumLo, &t->SumHi, counts);
    if (counts > t->Max) {
        t->Max = counts;
    }
}

ULONG XhciXstatInterval(PXHCI_XSTAT_TIME t, PULONG discarded, PULONG crossed,
                        const XHCI_XSTAT_STAMP *from,
                        const XHCI_XSTAT_STAMP *to)
{
    ULONG d;

    if (!XhciXstatDiff(discarded, crossed, from, to, &d)) {
        return 0;
    }
    XhciXstatAddTime(t, d);
    return 1;
}

VOID XhciXstatUrb(PXHCI_XSTAT s, ULONG length, ULONG chunks)
{
    s->Urbs++;
    XhciXstatAdd64(&s->BytesLo, &s->BytesHi, length);
    if (length > s->LengthMax) {
        s->LengthMax = length;
    }
    s->Hist[XhciXstatBucket(length)]++;
    if (chunks > 1) {
        s->MultiChunk++;
    }
    if (chunks > s->ChunksMax) {
        s->ChunksMax = chunks;
    }
    s->ChunkHist[XhciXstatChunkBucket(chunks)]++;
}
