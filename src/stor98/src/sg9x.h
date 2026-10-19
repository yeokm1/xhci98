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
// docs/INVESTIGATION-9X-REQUEST-SIZE.md). Sg9xResolve mirrors that walk.
// Pure arithmetic on a caller-supplied table, so the host unit test
// (tests/unit/sg9x_ut.c) includes it unchanged.
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

#endif // _STOR98_SG9X_H_
