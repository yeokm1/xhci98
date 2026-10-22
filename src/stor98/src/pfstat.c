/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, Yeo Kheng Meng
 *
 * pfstat.c - the counter answer's pure half (pfstat.h; docs/PLAN.md, MD-8).
 */

#include "nvme2k.h"
#include "pfstat.h"

VOID PfAdd(IN OUT PSTOR98PF_TOTAL Total, IN ULONG N)
{
    ULONG lo = Total->Lo + N;

    if (lo < Total->Lo) {
        Total->Hi++;
    }
    Total->Lo = lo;
}

ULONG PfSizeBucket(IN ULONG Bytes)
{
    if (Bytes < 4096) {
        return 0;
    }
    if (Bytes == 4096) {
        return 1;
    }
    if (Bytes <= 8192) {
        return 2;
    }
    if (Bytes <= 16384) {
        return 3;
    }
    if (Bytes <= 32768) {
        return 4;
    }
    if (Bytes < STOR98PF_SIZE_63K) {
        return 5;
    }
    if (Bytes == STOR98PF_SIZE_63K) {
        return 6;
    }
    if (Bytes <= 65536) {
        return 7;
    }
    if (Bytes <= 131072) {
        return 8;
    }
    if (Bytes <= 262144) {
        return 9;
    }
    if (Bytes <= 524288) {
        return 10;
    }
    if (Bytes <= 1048576) {
        return 11;
    }
    return 12;
}

ULONG PfRunClass(IN ULONG Bytes)
{
    ULONG b = PfSizeBucket(Bytes);

    if (b < STOR98PF_RUN_CLASS_FIRST || b >= STOR98PF_RUN_CLASS_FIRST + STOR98PF_RUN_CLASSES) {
        return STOR98PF_RUN_CLASSES;
    }
    return b - STOR98PF_RUN_CLASS_FIRST;
}

ULONG PfRunBucket(IN ULONG Runs, IN BOOLEAN NotSeen)
{
    if (NotSeen || Runs == 0) {
        return STOR98PF_RUN_NOT_SEEN;
    }
    if (Runs == 1) {
        return 0;
    }
    if (Runs == 2) {
        return 1;
    }
    if (Runs <= 4) {
        return 2;
    }
    if (Runs <= 8) {
        return 3;
    }
    if (Runs <= 16) {
        return 4;
    }
    if (Runs == 17) {
        return 5;
    }
    if (Runs <= 32) {
        return 6;
    }
    if (Runs <= 64) {
        return 7;
    }
    return 8;
}

ULONG PfDepthBucket(IN ULONG N)
{
    if (N <= 1) {
        return 0;
    }
    if (N == 2) {
        return 1;
    }
    if (N <= 4) {
        return 2;
    }
    if (N <= 8) {
        return 3;
    }
    if (N <= 16) {
        return 4;
    }
    if (N <= 32) {
        return 5;
    }
    return 6;
}

ULONG PfReadBuffer(IN const UCHAR *Cdb, IN ULONG CdbLength, IN const STOR98PF_ANSWER *Answer,
                   OUT UCHAR *Desc, OUT const UCHAR **Src, OUT PULONG Length)
{
    ULONG mode, alloc, offset, n;

    *Src = NULL;
    *Length = 0;
    if (CdbLength < 10 || Cdb[0] != STOR98PF_OPCODE) {
        return PF_RB_REFUSE;
    }
    mode = Cdb[1] & 0x1F;
    offset = ((ULONG)Cdb[3] << 16) | ((ULONG)Cdb[4] << 8) | Cdb[5];
    alloc = ((ULONG)Cdb[6] << 16) | ((ULONG)Cdb[7] << 8) | Cdb[8];
    if (Cdb[2] != STOR98PF_BUFFER_ID || offset != 0) {
        return PF_RB_REFUSE;
    }
    if (mode == STOR98PF_MODE_DESC) {
        Desc[0] = 0;                                    // offset boundary: 2^0
        Desc[1] = (UCHAR)(sizeof(STOR98PF_ANSWER) >> 16);
        Desc[2] = (UCHAR)(sizeof(STOR98PF_ANSWER) >> 8);
        Desc[3] = (UCHAR)sizeof(STOR98PF_ANSWER);
        *Src = Desc;
        n = 4;
    } else if (mode == STOR98PF_MODE_DATA) {
        *Src = (const UCHAR *)Answer;
        n = sizeof(STOR98PF_ANSWER);
    } else {
        return PF_RB_REFUSE;
    }
    if (n > alloc) {
        n = alloc;
    }
    *Length = n;
    return PF_RB_OK;
}

static VOID PfPutLe(OUT UCHAR *Out, IN ULONG V)
{
    Out[0] = (UCHAR)V;
    Out[1] = (UCHAR)(V >> 8);
    Out[2] = (UCHAR)(V >> 16);
    Out[3] = (UCHAR)(V >> 24);
}

VOID PfIdentityPage(IN ULONG PciLocation, OUT UCHAR *Out)
{
    Out[0] = 0x00;
    Out[1] = STOR98PF_VPD_PAGE;
    Out[2] = 0;
    Out[3] = STOR98PF_VPD_LEN - 4;
    PfPutLe(&Out[4], STOR98PF_SIGNATURE);
    PfPutLe(&Out[8], STOR98PF_VERSION);
    PfPutLe(&Out[12], PciLocation);
}

ULONG PfPciLocation(IN ULONG Bus, IN ULONG Slot)
{
    // PCI_SLOT_NUMBER: device in bits 4:0, function in bits 7:5
    return ((Bus & 0xFF) << 16) | ((Slot & 0x1F) << 3) | ((Slot >> 5) & 0x7);
}
