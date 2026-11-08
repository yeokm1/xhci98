// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Yeo Kheng Meng
//
// capdata.h - the READ CAPACITY(16) parameter data and the length of a
// software answer. Pure byte building with no port or OS call, so the host
// unit test (tests/unit/capdata_ut.c) includes it unchanged.
//
// READ CAPACITY(16) (SERVICE ACTION IN(16), service action 10h) returns 32
// bytes: the last LBA (bytes 0-7) and the block length (bytes 8-11), both
// big-endian, then flags: byte 13 bits 3:0 LOGICAL BLOCKS PER PHYSICAL
// BLOCK EXPONENT (LBPPBE; PLAN "4 KiB-sector namespaces": 512-byte sectors on
// a namespace of 512 << LBPPBE-byte blocks), bytes 14-15 the lowest aligned
// LBA (0), and no protection or provisioning; the rest is reserved, zero. The
// allocation length is CDB bytes 10-13; a shorter one is not an error, the
// caller gets the first bytes (SBC, SPC). A caller that knows only the
// 12-byte form (last LBA and block length; the WDK's READ_CAPACITY_DATA_EX)
// may ask for 12.
//

#ifndef _STOR98_CAPDATA_H_
#define _STOR98_CAPDATA_H_

#define CAP16_LEN           32

// Builds the full answer for a disk of Blocks logical blocks (not 0) of
// BlockSize bytes, 2^Lbppbe of them per physical block, into Out[CAP16_LEN];
// returns CAP16_LEN.
static ULONG Cap16Build(ULONGLONG Blocks, ULONG BlockSize, ULONG Lbppbe, UCHAR *Out)
{
    ULONGLONG last = Blocks - 1;
    ULONG i;

    for (i = 0; i < CAP16_LEN; i++) {
        Out[i] = 0;
    }
    for (i = 0; i < 8; i++) {
        Out[7 - i] = (UCHAR)(last >> (8 * i));
    }
    for (i = 0; i < 4; i++) {
        Out[11 - i] = (UCHAR)(BlockSize >> (8 * i));
    }
    Out[13] = (UCHAR)(Lbppbe & 0x0F);
    return CAP16_LEN;
}

// The allocation length of a READ CAPACITY(16) CDB (bytes 10-13).
static ULONG Cap16Alloc(const UCHAR *Cdb)
{
    return ((ULONG)Cdb[10] << 24) | ((ULONG)Cdb[11] << 16) | ((ULONG)Cdb[12] << 8) | Cdb[13];
}

// Bytes of a Full-byte answer to return: cut at the allocation length and at
// the transport buffer.
static ULONG ScsiAnswerLen(ULONG Full, ULONG Alloc, ULONG Xfer)
{
    if (Full > Alloc) {
        Full = Alloc;
    }
    if (Full > Xfer) {
        Full = Xfer;
    }
    return Full;
}

#endif /* _STOR98_CAPDATA_H_ */
