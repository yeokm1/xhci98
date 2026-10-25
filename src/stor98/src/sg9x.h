// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Yeo Kheng Meng
//
// sg9x.h - where byte <offset> of an SRB's data lies on Windows 98 SE / ME.
//
// SCSIPORT.PDR (4.10.2223) gives the miniport Srb->DataBuffer = the IOR's
// buffer pointer, or, when the IOR carries a logical scatter/gather list
// (IORF_SCATTER_GATHER), the linear address of its first fragment. Later
// fragments need not follow it in linear memory, so DataBuffer + offset is
// wrong past the first fragment. SCSIPORT itself resolves an offset by
// walking that list: entries of (count in sectors, linear address), the
// count shifted by the physical DCB's apparent block shift, terminated by a
// zero count (SCSIPORT obj1 0x00C9-0x0116; investigation note
// docs/INVESTIGATION-9X-REQUEST-SIZE.md). Sg9xResolve mirrors that walk,
// Sg9xClassify finds the list from the SRB, Sg9xCopy copies through it. No
// port or OS call, so the host unit test (tests/unit/sg9x_ut.c) includes it
// unchanged, with SG9X_ARENA lowered for host addresses.
//

#ifndef _STOR98_SG9X_H_
#define _STOR98_SG9X_H_

typedef struct _STOR98_SGD9X {
    ULONG Count;            // BD_SG_Count, sectors (shifted by the block shift)
    ULONG Linear;           // BD_SG_Buffer_Ptr, linear address
} STOR98_SGD9X, *PSTOR98_SGD9X;

// At most this many fragments are walked; IOS offers DCB_max_sg_elements
// (17 under SCSIPORT, 255 at most), so a longer walk means a broken list.
#define SG9X_MAX_WALK   256

// Linear address of byte Offset, and in *Run the bytes contiguous from it
// to the end of its fragment. Returns FALSE when Offset lies past the list
// (or the list is broken); *Linear and *Run are then 0.
static BOOLEAN Sg9xResolve(const STOR98_SGD9X *Sgd, UCHAR Shift, ULONG Offset,
                           ULONG *Linear, ULONG *Run)
{
    ULONG i, bytes;

    *Linear = 0;
    *Run = 0;
    if (Sgd == NULL || Shift > 16) {
        return FALSE;
    }
    for (i = 0; i < SG9X_MAX_WALK; i++) {
        if (Sgd[i].Count == 0) {
            return FALSE;
        }
        if (Sgd[i].Count > (0xFFFFFFFFUL >> Shift)) {
            return FALSE;
        }
        bytes = Sgd[i].Count << Shift;
        if (Offset < bytes) {
            *Linear = Sgd[i].Linear + Offset;
            *Run = bytes - Offset;
            return TRUE;
        }
        Offset -= bytes;
    }
    return FALSE;
}

//
// Which rule gives the linear address of the SRB's data (9x). Every SRB
// SCSIPORT.PDR hands the miniport is the BaseSrb of a PORT_SRB, whose next
// field, SrbIopPointer (offset 0x40), is the IOP (Win98 DDK,
// block\inc\SCSIPORT.H). IOP layout (IOP.H): IOP_physical_dcb +0x04,
// IOP_srb +0x20, IOP_ior +0x64; IOR (IOR.H): IOR_flags +0x08,
// IOR_buffer_ptr +0x1C; DCB (DCB.H): DCB_apparent_blk_shift +0x3C. The same
// code, at the same offsets, is in SCSIPORT.PDR of 98 SE 4.10.2222, the
// 4.10.2223 update and ME (docs/INVESTIGATION-9X-REQUEST-SIZE.md, section 7).
// Every pointer is checked to lie in the system arena before it is read, and
// the IOP must point back at the SRB (IOP_srb), so a different layout is
// reported as unknown rather than followed. No alignment is required: IOS's
// big-request splitter hands down a one-entry list at an odd address inside
// its own block (IOS obj1 0x0F3E-0x0F5C, + 0x39; emulator 2026-10-19).
//
#define SG9X_SRB_IOP        0x40        // PORT_SRB.SrbIopPointer
#define SG9X_IOP_PDCB       0x04        // IOP_physical_dcb
#define SG9X_IOP_SRB        0x20        // IOP_srb
#define SG9X_IOR            0x64        // IOP_ior
#define SG9X_IOR_FLAGS      (SG9X_IOR + 0x08)
#define SG9X_IOR_BUFFER     (SG9X_IOR + 0x1C)
#define SG9X_DCB_SHIFT      0x3C        // DCB_apparent_blk_shift
#define SG9X_IORF_SG        0x00000002  // IORF_SCATTER_GATHER
#define SG9X_IORF_CHAR      0x00000200  // IORF_CHAR_COMMAND: counts are bytes
#ifndef SG9X_ARENA
#define SG9X_ARENA          0xC0000000UL    // the 9x system arena; the host test lowers it
#endif

#define SG9X_LINEAR         0           // DataBuffer + offset is right
#define SG9X_SG             1           // resolve through *Sgd
#define SG9X_UNKNOWN        2           // cannot tell: never copy through the buffer

static ULONG Sg9xClassify(const void *Srb, const void *DataBuffer, const STOR98_SGD9X **Sgd, UCHAR *Shift)
{
    ULONG iop, flags, dcb, sgd;

    *Sgd = NULL;
    *Shift = 0;
    iop = *(const ULONG *)((const UCHAR *)Srb + SG9X_SRB_IOP);
    if (iop < SG9X_ARENA || *(const ULONG *)(iop + SG9X_IOP_SRB) != (ULONG)Srb) {
        return SG9X_UNKNOWN;
    }
    flags = *(const ULONG *)(iop + SG9X_IOR_FLAGS);
    if ((flags & SG9X_IORF_SG) == 0) {
        return SG9X_LINEAR;
    }
    dcb = *(const ULONG *)(iop + SG9X_IOP_PDCB);
    sgd = *(const ULONG *)(iop + SG9X_IOR_BUFFER);
    if (dcb < SG9X_ARENA || sgd < SG9X_ARENA ||
        ((const STOR98_SGD9X *)sgd)->Linear != (ULONG)DataBuffer) {
        return SG9X_UNKNOWN;
    }
    *Shift = (flags & SG9X_IORF_CHAR) ? 0 : *(const UCHAR *)(dcb + SG9X_DCB_SHIFT);
    *Sgd = (const STOR98_SGD9X *)sgd;
    return SG9X_SG;
}

// IOR_func of the SRB's IOR (IOR.H, IOR + 0x04), or 0xFFFF when the layout is
// not the one Sg9xClassify checks. Diagnostics only.
#define SG9X_IOR_FUNC       (SG9X_IOR + 0x04)
static ULONG Sg9xIorFunc(const void *Srb)
{
    ULONG iop = *(const ULONG *)((const UCHAR *)Srb + SG9X_SRB_IOP);

    if (iop < SG9X_ARENA || *(const ULONG *)(iop + SG9X_IOP_SRB) != (ULONG)Srb) {
        return 0xFFFF;
    }
    return *(const USHORT *)(iop + SG9X_IOR_FUNC);
}

// Copy Bytes between Bounce and the list's data at Offset. The whole span
// is resolved before anything is copied; FALSE (nothing copied) if any of
// it lies past the list. ToCaller: Bounce -> list, else list -> Bounce.
static BOOLEAN Sg9xCopy(const STOR98_SGD9X *Sgd, UCHAR Shift, ULONG Offset, UCHAR *Bounce,
                        ULONG Bytes, BOOLEAN ToCaller)
{
    ULONG lin, run, n, off, left;

    for (off = Offset, left = Bytes; left != 0; off += n, left -= n) {
        if (!Sg9xResolve(Sgd, Shift, off, &lin, &run)) {
            return FALSE;
        }
        n = (run < left) ? run : left;
    }
    for (off = Offset, left = Bytes; left != 0; off += n, left -= n, Bounce += n) {
        (void)Sg9xResolve(Sgd, Shift, off, &lin, &run);
        n = (run < left) ? run : left;
        if (ToCaller) {
            RtlCopyMemory((void *)lin, Bounce, n);
        } else {
            RtlCopyMemory(Bounce, (const void *)lin, n);
        }
    }
    return TRUE;
}

// What a request's DCB says (docs/PLAN.md MD-8, the counter answer), read
// through the same checks as Sg9xClassify but for linear and scatter/gather
// requests alike: the IOP in the arena and pointing back at the SRB, the
// physical DCB pointer in the arena. DCB (Win98 DDK block\inc\DCB.H and
// DCB.INC, packed; DCB_COMMON is 0x50 bytes): DCB_dmd_flags +0x1C,
// DCB_max_xfer_len +0x50 (the first field after DCB_COMMON),
// DCB_max_sg_elements +0x77 (a byte); IOR_flags' IORF_DOUBLE_BUFFER 0x40
// (IOR.H). FALSE, and nothing written, when a check fails.
#define SG9X_DCB_DMD        0x1C        // DCB_dmd_flags
#define SG9X_DCB_MAXXFER    0x50        // DCB_max_xfer_len
#define SG9X_DCB_MAXSG      0x77        // DCB_max_sg_elements
#define SG9X_IORF_DBLBUF    0x00000040  // IORF_DOUBLE_BUFFER

static BOOLEAN Sg9xDcbFacts(const void *Srb, ULONG *MaxXfer, ULONG *MaxSg, ULONG *DmdFlags, ULONG *IorFlags)
{
    ULONG iop, dcb;

    iop = *(const ULONG *)((const UCHAR *)Srb + SG9X_SRB_IOP);
    if (iop < SG9X_ARENA || *(const ULONG *)(iop + SG9X_IOP_SRB) != (ULONG)Srb) {
        return FALSE;
    }
    dcb = *(const ULONG *)(iop + SG9X_IOP_PDCB);
    if (dcb < SG9X_ARENA) {
        return FALSE;
    }
    *IorFlags = *(const ULONG *)(iop + SG9X_IOR_FLAGS);
    *DmdFlags = *(const ULONG *)(dcb + SG9X_DCB_DMD);
    *MaxXfer = *(const ULONG *)(dcb + SG9X_DCB_MAXXFER);
    *MaxSg = *(const UCHAR *)(dcb + SG9X_DCB_MAXSG);
    return TRUE;
}

#endif // _STOR98_SG9X_H_
