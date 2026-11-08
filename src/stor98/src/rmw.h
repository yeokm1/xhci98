// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Yeo Kheng Meng
//
// rmw.h - 512-byte sectors on a namespace with larger blocks (PLAN "4 KiB-sector
// namespaces", 1.7): the exposed count, how a request of exposed sectors
// covers native blocks (the edges and the aligned middle), and the range test
// of the overlap rules. Pure arithmetic with no port or OS call, so the host
// unit test (tests/unit/rmw_ut.c) includes it unchanged.
//
// Units: the native block is 512 << Shift bytes (Shift 0 to 3, nsfmt.h's
// LBADS 9 to 12, minus 9); an exposed sector is 512 bytes; R = 1 << Shift
// exposed sectors make one native block.
//

#ifndef _STOR98_RMW_H_
#define _STOR98_RMW_H_

#define RMW_SECTOR          512
#define RMW_SHIFT_MAX       3

// One partly covered native block of a request.
typedef struct _RMW_EDGE {
    ULONGLONG Lba;          // native block
    ULONG Offset;           // byte offset of its caller bytes in the request's buffer
    ULONG Skip;             // byte offset of those bytes in the native block
    ULONG Bytes;            // caller bytes in the block (a multiple of 512, less than the block)
} RMW_EDGE;

typedef struct _RMW_PLAN {
    ULONGLONG First;        // first native block the request touches
    ULONGLONG End;          // the native block after the last one it touches
    ULONGLONG MidLba;       // first wholly covered native block
    ULONG MidBlocks;        // wholly covered native blocks (0: none)
    ULONG MidOffset;        // byte offset of the middle in the buffer
    ULONG Edges;            // 0, 1 or 2
    RMW_EDGE Edge[2];       // in buffer order: the head (or the shared block), then the tail
} RMW_PLAN;

// The view (xhci98 3.4.2.0, its roadmap 47.31; PLAN "4 KiB-sector
// namespaces"): TRUE when the OS sees the namespace's native 4096-byte
// sectors, as Windows' own stornvme shows them - the NT build (IsNt) on
// Windows 6.2 (8) or later, a namespace of LBADS 12. Everything else, 98 SE
// to 7 and 1 and 2 KiB namespaces everywhere, sees 512-byte sectors.
static BOOLEAN RmwNativeView(int IsNt, ULONG Major, ULONG Minor, ULONG Lbads)
{
    return (BOOLEAN)(IsNt && (Major > 6 || (Major == 6 && Minor >= 2)) && Lbads == 12);
}

// ATA IDENTIFY word 106 (physical / logical sector size) for the view: a
// 512-byte namespace 4000h; a translated larger one 6000h | Shift (2^Shift
// logical sectors a physical one); the native view 5000h (a logical sector
// longer than 256 words, words 117-118 giving it).
static ULONG RmwAtaWord106(ULONG Shift, BOOLEAN Native)
{
    if (Native) {
        return 0x5000;
    }
    return Shift == 0 ? 0x4000 : (0x6000 | Shift);
}

// ATA IDENTIFY words 117-118 (the logical sector in words) for a sector of
// SectorBytes the OS sees: 0 for 512 bytes (word 106 bit 12 clear), else
// SectorBytes / 2 (0800h for 4096).
static ULONG RmwAtaLogicalWords(ULONG SectorBytes)
{
    return SectorBytes > RMW_SECTOR ? SectorBytes / 2 : 0;
}

// The exposed sector count of a namespace of Nsze native blocks: FALSE when
// it does not fit 64 bits (the namespace is then refused).
static BOOLEAN RmwExposedCount(ULONGLONG Nsze, ULONG Shift, ULONGLONG *Exposed)
{
    *Exposed = 0;
    if (Shift > RMW_SHIFT_MAX || (Shift != 0 && (Nsze >> (64 - Shift)) != 0)) {
        return FALSE;
    }
    *Exposed = Nsze << Shift;
    return TRUE;
}

// The plan of a request of Count (>= 1) exposed sectors at exposed LBA Lba,
// already checked against the exposed count (so Lba + Count does not wrap).
static VOID RmwPlan(ULONGLONG Lba, ULONG Count, ULONG Shift, RMW_PLAN *P)
{
    ULONG r = 1UL << Shift, mask = r - 1;
    ULONG h = (ULONG)(Lba & mask);
    ULONGLONG end = Lba + Count;
    ULONG t = (ULONG)(end & mask);
    ULONGLONG midFirst, midEnd;
    ULONG headCaller;

    P->First = Lba >> Shift;
    P->End = (end >> Shift) + (t != 0 ? 1 : 0);    // not (end + mask) >> Shift: no wrap at the top
    P->Edges = 0;
    midFirst = P->First;
    midEnd = P->End;
    if (h != 0) {
        headCaller = r - h;
        if (headCaller > Count) {
            headCaller = Count;     // the shared block: head and tail in one native block
        }
        P->Edge[0].Lba = P->First;
        P->Edge[0].Offset = 0;
        P->Edge[0].Skip = h * RMW_SECTOR;
        P->Edge[0].Bytes = headCaller * RMW_SECTOR;
        P->Edges = 1;
        midFirst++;
    }
    if (t != 0 && P->End - 1 >= midFirst) {
        P->Edge[P->Edges].Lba = P->End - 1;
        P->Edge[P->Edges].Offset = (Count - t) * RMW_SECTOR;
        P->Edge[P->Edges].Skip = 0;
        P->Edge[P->Edges].Bytes = t * RMW_SECTOR;
        P->Edges++;
        midEnd--;
    }
    P->MidLba = midFirst;
    P->MidBlocks = (midEnd > midFirst) ? (ULONG)(midEnd - midFirst) : 0;
    P->MidOffset = (h != 0) ? (r - h) * RMW_SECTOR : 0;
    if (P->MidBlocks == 0) {
        P->MidLba = 0;
        P->MidOffset = 0;
    }
}

// What the overlap rules (PLAN "Concurrency and ordering") know of a
// READ/WRITE parent: its native range and, for an unaligned write, its edge
// blocks (Rmw TRUE).
typedef struct _RMW_RANGE {
    ULONGLONG First;
    ULONGLONG End;
    ULONGLONG EdgeLba[2];
    UCHAR Edges;
    UCHAR Rmw;
    UCHAR Reserved[6];
} RMW_RANGE;

static BOOLEAN RmwHasBlock(const RMW_RANGE *R, ULONGLONG Lba)
{
    return (BOOLEAN)(Lba >= R->First && Lba < R->End);
}

static BOOLEAN RmwEdgesIn(const RMW_RANGE *Edges, const RMW_RANGE *Range)
{
    ULONG i;

    if (!Edges->Rmw) {
        return FALSE;
    }
    for (i = 0; i < Edges->Edges && i < 2; i++) {
        if (RmwHasBlock(Range, Edges->EdgeLba[i])) {
            return TRUE;
        }
    }
    return FALSE;
}

// TRUE when Later may not start while Earlier is unfinished: an unaligned
// write starts only when no earlier parent's range holds one of its edge
// blocks, and any parent starts only when no earlier unaligned write has an
// edge block in its range.
static BOOLEAN RmwMustWait(const RMW_RANGE *Earlier, const RMW_RANGE *Later)
{
    return (BOOLEAN)(RmwEdgesIn(Later, Earlier) || RmwEdgesIn(Earlier, Later));
}

#endif /* _STOR98_RMW_H_ */
