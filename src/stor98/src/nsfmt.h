// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Yeo Kheng Meng
//
// nsfmt.h - the namespace's format from the Identify Namespace data (PLAN
// 1.1; AGENTS.md "Decode the namespace correctly"). Pure byte decoding with
// no port or OS call, so the host unit test (tests/unit/nsfmt_ut.c)
// includes it unchanged.
//
// The format index is FLBAS bits 3:0, with bits 6:5 as its two high bits
// when NLBAF (0's based) says more than 16 formats exist; it must not pass
// NLBAF. The block size is 2^LBADS of that format. A namespace the driver
// cannot represent is refused, never served under another size:
//
//   NSZE 0                  no active namespace 1 (it would be a phantom
//                           disk whose every READ and WRITE fails)
//   index past NLBAF        no such format
//   LBADS outside 9..12     block sizes 512 to 4096 bytes are served; the
//                           engine splits a request into children of at
//                           least one 4 KiB page and bounce buffers of
//                           pages, and a larger block need not fit one
//                           child (PLAN, "4 KiB-sector namespaces":
//                           larger sizes only within the bounce budget,
//                           otherwise refused). 4 KiB is never refused.
//   metadata (MS != 0)      interleaved with the data (FLBAS bit 4) it
//                           changes the transfer layout; in a separate
//                           buffer it needs a metadata pointer, which the
//                           driver never supplies
//   protection (DPS 2:0)    end-to-end protection information
//
// The OS sees the namespace's own block size until 512-byte presentation
// (PLAN 1.7) is implemented.
//

#ifndef _STOR98_NSFMT_H_
#define _STOR98_NSFMT_H_

#define NSF_OK              0
#define NSF_ZERO_SIZE       1
#define NSF_BAD_INDEX       2
#define NSF_BAD_LBADS       3
#define NSF_METADATA        4
#define NSF_PROTECTION      5

#define NSF_LBADS_MIN       9       // 512 bytes
#define NSF_LBADS_MAX       12      // 4096 bytes

// Decodes Id (the 4096-byte Identify Namespace data). Returns NSF_OK with
// *Nsze (blocks) and *BlockSize (bytes) set, or the NSF_* reason the
// namespace is refused. *Lbads is the selected format's LBADS (0 when the
// index is invalid), for the log.
static ULONG NsDecodeFormat(const UCHAR *Id, ULONGLONG *Nsze, ULONG *BlockSize, UCHAR *Lbads)
{
    ULONGLONG nsze = 0;
    ULONG i, index, ms;
    const UCHAR *lbaf;

    *Nsze = 0;
    *BlockSize = 0;
    *Lbads = 0;
    for (i = 8; i > 0; i--) {
        nsze = (nsze << 8) | Id[i - 1];     // NSZE, bytes 0-7, little-endian
    }
    if (nsze == 0) {
        return NSF_ZERO_SIZE;
    }
    index = Id[26] & 0x0F;                  // FLBAS bits 3:0
    if (Id[25] > 15) {
        index |= ((ULONG)(Id[26] >> 5) & 0x03) << 4;
    }
    if (index > Id[25]) {
        return NSF_BAD_INDEX;
    }
    lbaf = Id + 128 + 4 * index;            // LBAF: MS (15:0), LBADS (23:16), RP
    ms = (ULONG)lbaf[0] | ((ULONG)lbaf[1] << 8);
    *Lbads = lbaf[2];
    if (lbaf[2] < NSF_LBADS_MIN || lbaf[2] > NSF_LBADS_MAX) {
        return NSF_BAD_LBADS;
    }
    if (ms != 0) {
        return NSF_METADATA;
    }
    if (Id[29] & 0x07) {                    // DPS: protection type
        return NSF_PROTECTION;
    }
    *Nsze = nsze;
    *BlockSize = 1UL << lbaf[2];
    return NSF_OK;
}

#endif /* _STOR98_NSFMT_H_ */
