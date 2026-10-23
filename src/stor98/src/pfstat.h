/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, Yeo Kheng Meng
 *
 * pfstat.h - the counter answer's pure half (docs/PLAN.md, MD-8;
 * include/stor98pf.h): the buckets, the 64-bit totals, the READ BUFFER
 * CDB's checks and copy, and the identity page. No port call, no device
 * extension: tests/unit/pfstat_ut.c links pfstat.c on the host.
 */

#ifndef _STOR98_PFSTAT_H_
#define _STOR98_PFSTAT_H_

#include "..\include\stor98pf.h"

// Adds N to a total, carrying into the high word by hand.
VOID PfAdd(IN OUT PSTOR98PF_TOTAL Total, IN ULONG N);

// The bucket of a transfer of Bytes (stor98pf.h, size buckets).
ULONG PfSizeBucket(IN ULONG Bytes);

// The run class of a request of Bytes (0 to STOR98PF_RUN_CLASSES - 1), or
// STOR98PF_RUN_CLASSES when it is in none.
ULONG PfRunClass(IN ULONG Bytes);

// The bucket of Runs physical runs; NotSeen gives STOR98PF_RUN_NOT_SEEN.
ULONG PfRunBucket(IN ULONG Runs, IN BOOLEAN NotSeen);

// The bucket of N commands outstanding (N >= 1).
ULONG PfDepthBucket(IN ULONG N);

// READ BUFFER (Cdb, CdbLength bytes) against the answer Answer: on success
// returns PF_RB_OK with *Src the bytes to return (the answer, or Desc, which
// it fills, for mode 03h) and *Length their count, already cut at the
// allocation length (the caller cuts it at the data buffer too);
// PF_RB_REFUSE for a CDB this interface does not answer (INVALID FIELD IN
// CDB), *Length 0.
#define PF_RB_OK        0
#define PF_RB_REFUSE    1
ULONG PfReadBuffer(IN const UCHAR *Cdb, IN ULONG CdbLength, IN const STOR98PF_ANSWER *Answer,
                   OUT UCHAR *Desc, OUT const UCHAR **Src, OUT PULONG Length);

// The identity page (STOR98PF_VPD_LEN bytes) for the adapter at
// PciLocation (stor98pf.h, "The identity page").
VOID PfIdentityPage(IN ULONG PciLocation, OUT UCHAR *Out);

// bus << 16 | device << 3 | function, from a bus and a PCI_SLOT_NUMBER.u.AsULONG
ULONG PfPciLocation(IN ULONG Bus, IN ULONG Slot);

#endif /* _STOR98_PFSTAT_H_ */
