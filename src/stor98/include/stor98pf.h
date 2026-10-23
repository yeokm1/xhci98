// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright (c) 2026, Yeo Kheng Meng
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
// 3. Neither the name of the copyright holder nor the names of its
//    contributors may be used to endorse or promote products derived from
//    this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
//
// stor98pf.h: the read-only counter answer of stor98 (docs/PLAN.md, decision
// MD-8, 2026-10-22), read by xhci98's disk benchmark XHCIPERF (xhci98 design
// record 29, section 7).
//
// LICENSING OF THIS FILE. This header is an interface definition, licensed
// on its own under the BSD-3-Clause licence above, independently of the code
// that implements either side of it. stor98 (BSD-3-Clause) answers; xhci98's
// XHCIPERF (GPL-2.0) reads, and may include this file as is. Including it in
// a GPL-2.0 work does not change the licence of this file, and keep it that
// way: do not copy GPL-licensed code, structure definitions or text into it.
//
// The carrier is one SCSI command on every system, READ BUFFER (3Ch):
//   byte 1 bits 4:0  mode: STOR98PF_MODE_DATA (02h) or STOR98PF_MODE_DESC (03h)
//   byte 2           buffer ID: STOR98PF_BUFFER_ID (53h)
//   bytes 3-5        buffer offset: 0
//   bytes 6-8        allocation length
// Any other mode, buffer ID or offset, or a CDB shorter than 10 bytes, is
// CHECK CONDITION, ILLEGAL REQUEST, INVALID FIELD IN CDB. Mode 03h returns the
// four-byte descriptor (offset boundary 0, then the answer's size, big-endian
// in bytes 1-3). Mode 02h returns the answer, cut at the allocation length and
// the data buffer. It is answered from memory: no NVMe command, no access to
// the medium. Outside the running state the request is held or refused as any
// other (no answer while the adapter is gone).
//
// The identity page: INQUIRY with EVPD, page STOR98PF_VPD_PAGE (C9h), listed
// in page 00h. A reader on Windows 98 SE and ME (ASPI) asks it before any READ
// BUFFER, so READ BUFFER is sent to no device that is not stor98's. Layout:
//   bytes 0-3   peripheral 00h, page C9h, page length (2 bytes, big-endian) 12
//   bytes 4-7   STOR98PF_SIGNATURE, little-endian
//   bytes 8-11  STOR98PF_VERSION, little-endian
//   bytes 12-15 the adapter's PCI location: bus << 16 | device << 3 | function
//
// The answer's fields have one of three kinds:
//   total  rises only, never reset while the adapter runs: a 64-bit count as
//          a low and high ULONG pair (STOR98PF_TOTAL); subtract two answers
//          exactly, when their Epoch is the same;
//   event  a 32-bit count of rare events (the driver's own counters, which
//          predate this interface): subtract modulo 2^32;
//   fact   a value or a limit, shown as read, never subtracted.
// Epoch changes at every adapter start that is not a resume; two answers of
// different epochs are not subtracted.
//
// Versions: STOR98PF_VERSION is a protocol version, never the package
// version. A reader accepts an answer of its version whose Length is at least
// sizeof(STOR98PF_ANSWER) and reads only what it knows.
//

#ifndef _STOR98PF_H_
#define _STOR98PF_H_

#define STOR98PF_OPCODE             0x3C            // READ BUFFER
#define STOR98PF_MODE_DATA          0x02
#define STOR98PF_MODE_DESC          0x03
#define STOR98PF_BUFFER_ID          0x53
#define STOR98PF_VPD_PAGE           0xC9
#define STOR98PF_VPD_LEN            16
#define STOR98PF_SIGNATURE          0x50383953      // 'S98P'
#define STOR98PF_VERSION            1

// Directions
#define STOR98PF_READ               0
#define STOR98PF_WRITE              1

// Size buckets (bytes), STOR98PF_SIZE_BUCKETS of them:
//  0 below 4 KiB      1 4 KiB            2 to 8 KiB        3 to 16 KiB
//  4 to 32 KiB        5 below 63 KiB     6 exactly 64,512  7 to 64 KiB
//  8 to 128 KiB       9 to 256 KiB      10 to 512 KiB     11 to 1 MiB
// 12 above 1 MiB
// ("to N" is above the previous bucket and at most N; 63 KiB is IOS's piece
// on Windows 98 SE and ME, 126 sectors.)
#define STOR98PF_SIZE_BUCKETS       13
#define STOR98PF_SIZE_63K           64512

// Physical runs of a data request's caller buffer, counted over the whole
// request before stor98 cuts it into commands, for the request-size classes
// STOR98PF_RUN_CLASSES (size buckets 7 to 11: 64 KiB, 128, 256, 512 KiB and
// 1 MiB), in STOR98PF_RUN_BUCKETS buckets:
//  0 one run   1 two   2 3-4   3 5-8   4 9-16   5 17   6 18-32   7 33-64
//  8 above 64  9 not seen (a part of the buffer could not be translated)
#define STOR98PF_RUN_CLASSES        5
#define STOR98PF_RUN_CLASS_FIRST    7
#define STOR98PF_RUN_BUCKETS        10
#define STOR98PF_RUN_NOT_SEEN       9
#define STOR98PF_RUN_WALK_MAX       65

// Commands outstanding on the I/O queue at each data command's issue, itself
// included, in STOR98PF_DEPTH_BUCKETS buckets:
//  0 one   1 two   2 3-4   3 5-8   4 9-16   5 17-32   6 above 32
#define STOR98PF_DEPTH_BUCKETS      7

// How IOS handed each data request to stor98 (Windows 98 SE and ME only):
#define STOR98PF_SG9X_LINEAR        0
#define STOR98PF_SG9X_LIST          1
#define STOR98PF_SG9X_UNKNOWN       2
#define STOR98PF_SG9X_KINDS         3

// Build flags
#define STOR98PF_BUILD_9X           0x00000001
#define STOR98PF_BUILD_AMD64        0x00000002

// Dcb9xSeen
#define STOR98PF_DCB_SEEN           0x00000001      // the three DCB fields were read in this epoch

// Events[] (event kind)
#define STOR98PF_EV_FAST_FAILS      0
#define STOR98PF_EV_BUSY            1
#define STOR98PF_EV_RESETS          2
#define STOR98PF_EV_GONE_DEADLINES  3
#define STOR98PF_EV_STALE_CQES      4
#define STOR98PF_EV_LATE_CQES       5
#define STOR98PF_EV_QUARANTINED     6
#define STOR98PF_EV_STORM_MASKS     7
#define STOR98PF_EVENTS             8

#pragma pack(push, 4)

typedef struct _STOR98PF_TOTAL {                // 8 bytes
    ULONG Lo;
    ULONG Hi;
} STOR98PF_TOTAL, *PSTOR98PF_TOTAL;

// The totals, kept by the driver in this layout and copied as they are.
typedef struct _STOR98PF_TOTALS {
    STOR98PF_TOTAL Srbs[2];                     // data SRBs received, per direction, at HwStartIo
    STOR98PF_TOTAL Sectors[2];                  // their 512-byte units
    STOR98PF_TOTAL SrbSize[2][STOR98PF_SIZE_BUCKETS];
    STOR98PF_TOTAL CmdSize[2][STOR98PF_SIZE_BUCKETS];   // NVMe data commands issued
    STOR98PF_TOTAL Splits;                      // data requests that took more than one command
    STOR98PF_TOTAL Bounced;                     // data commands through stor98's bounce buffer
    STOR98PF_TOTAL Flushes;                     // NVMe Flush commands issued
    STOR98PF_TOTAL SrbErrors;                   // data requests completed with an error status
    STOR98PF_TOTAL Depth[STOR98PF_DEPTH_BUCKETS];
    STOR98PF_TOTAL Runs[STOR98PF_RUN_CLASSES][STOR98PF_RUN_BUCKETS];
    STOR98PF_TOTAL Sg9x[STOR98PF_SG9X_KINDS];   // 9x only
    STOR98PF_TOTAL DoubleBuffered;              // 9x only: IORF_DOUBLE_BUFFER set
} STOR98PF_TOTALS, *PSTOR98PF_TOTALS;

typedef struct _STOR98PF_ANSWER {
    ULONG Signature;            // 0x00 STOR98PF_SIGNATURE
    ULONG Version;              // 0x04 STOR98PF_VERSION
    ULONG Length;               // 0x08 bytes of this answer
    ULONG Epoch;                // 0x0C nonzero; changes at every adapter start but a resume
    // Facts
    ULONG Build;                // 0x10 STOR98PF_BUILD_*
    ULONG PciLocation;          // 0x14 bus << 16 | device << 3 | function
    ULONG VendorDevice;         // 0x18 device ID << 16 | vendor ID
    ULONG State;                // 0x1C the engine's state (implementation-defined)
    UCHAR DriverVersion[16];    // 0x20 the file version as text, NUL-padded
    ULONG BlockSize;            // 0x30 the namespace's logical block size
    ULONG BlocksLo;             // 0x34 its size in blocks
    ULONG BlocksHi;             // 0x38
    ULONG MaxTransfer;          // 0x3C MaximumTransferLength as granted
    ULONG PhysicalBreaks;       // 0x40 NumberOfPhysicalBreaks as granted
    ULONG MdtsBytes;            // 0x44 the controller's MDTS in bytes (0: no limit, or not known)
    ULONG MaxChildBytes;        // 0x48 the largest command stor98 permits itself (a limit)
    ULONG ChildLimitIo;         // 0x4C commands allowed at once on the I/O queue
    ULONG ChildrenIo;           // 0x50 commands outstanding when answered (a gauge)
    ULONG WriteThrough;         // 0x54 resolved mode: 0 off, 1 cache off, 2 FUA (implementation-defined)
    ULONG Vwc;                  // 0x58 1: a volatile write cache is present
    ULONG Polling;              // 0x5C bit 0 polling mode, bit 1 storm polling, bits 15:8 why
    ULONG Dcb9xSeen;            // 0x60 STOR98PF_DCB_SEEN
    ULONG DcbMaxXferLen;        // 0x64 9x: DCB_max_xfer_len
    ULONG DcbMaxSgElements;     // 0x68 9x: DCB_max_sg_elements
    ULONG DcbDmdFlags;          // 0x6C 9x: DCB_dmd_flags (DCB_dmd_phys_sgd 0x800)
    ULONG Events[STOR98PF_EVENTS];  // 0x70 event kind
    STOR98PF_TOTALS Totals;     // 0x90
} STOR98PF_ANSWER, *PSTOR98PF_ANSWER;

#pragma pack(pop)

#define STOR98PF_CASSERT(n, c)      typedef char stor98pf_cassert_##n[(c) ? 1 : -1]
#define STOR98PF_OFS(t, f)          ((ULONG)(ULONG_PTR)&(((t *)0)->f))

STOR98PF_CASSERT(total, sizeof(STOR98PF_TOTAL) == 8);
STOR98PF_CASSERT(totals, sizeof(STOR98PF_TOTALS) == 8 * (2 + 2 + 26 + 26 + 4 + 7 + 50 + 3 + 1));
STOR98PF_CASSERT(drvver, STOR98PF_OFS(STOR98PF_ANSWER, DriverVersion) == 0x20);
STOR98PF_CASSERT(dcbdmd, STOR98PF_OFS(STOR98PF_ANSWER, DcbDmdFlags) == 0x6C);
STOR98PF_CASSERT(events, STOR98PF_OFS(STOR98PF_ANSWER, Events) == 0x70);
STOR98PF_CASSERT(totofs, STOR98PF_OFS(STOR98PF_ANSWER, Totals) == 0x90);
STOR98PF_CASSERT(answer, sizeof(STOR98PF_ANSWER) == 0x90 + 8 * 121);

#endif // _STOR98PF_H_
