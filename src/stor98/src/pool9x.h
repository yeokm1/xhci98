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
// pool9x.h - the Windows 9x MD-4 DMA pool provider (PlatDmaPoolProvider,
// platform.h) on the interface 3 boot block, built with STOR98_DMA_POOL on
// the 9x target only. Design: docs/INTEGRATION-0.2.md, "Pool wiring".
//

#ifndef _STOR98_POOL9X_H_
#define _STOR98_POOL9X_H_

#if defined(STOR98_DMA_POOL) && defined(NVME2K_W9X)

//
// Debug-port stage codes 0x120-0x12F (docs/TESTING.md; test builds only).
//
#define STG_P9_FIND         0x120   // arg: result (0 ok, else P9_FAIL_*) | how << 8 (1 new, 2 reused, 3 kept) | record << 12 | pages << 16
#define STG_P9_REC          0x121   // arg: record | to << 8 | from << 12 | cause << 16 (P9C_*) | function bus << 24
#define STG_P9_RENDEZVOUS   0x122   // arg: result (0 table accepted) | bound << 8 | generation << 16
#define STG_P9_CHECK        0x123   // arg: reuse check passed (bit 0) | loads << 8
#define STG_P9_LS_SEEN      0x124   // marker; arg: generation | bound generation << 8 | engine state << 16 | bus << 24
#define STG_P9_LS_ACK       0x125   // marker; arg: generation | written << 16 | bus << 24 (AckGeneration)
#define STG_P9_IDENTITY     0x126   // arg: reason; this instance stops acting on its entry (R5e, R8d)
#define STG_P9_BUDGET       0x127   // arg: charged pages before | pages << 12 | refused << 31
#define STG_P9_TOKEN        0x128   // arg: token (15:0) | function slot << 16 (0xFF none) | replaced token low byte << 24
#define STG_P9_DEFER        0x129   // arg: pending work (P9P_*); the boot-block lock was busy

// STG_P9_FIND failures (all before any CC write)
#define P9_FAIL_ACCT        1       // boot block unavailable or not this boot's (fail closed)
#define P9_FAIL_LOCK        2       // boot-block lock busy
#define P9_FAIL_TOKEN       3       // instance tokens exhausted
#define P9_FAIL_RECORD      4       // no free record and nothing to reuse
#define P9_FAIL_BUDGET      5       // boot-wide budget reached
#define P9_FAIL_ALLOC       6       // _PageAllocate failed or the block does not check out
#define P9_FAIL_PASSES      7       // scan passes used up (not expected: P9Setup bounds them by P9_RECS)

// STG_P9_REC causes
#define P9C_CLAIM           1       // claimed by this load (FREE or RELEASED -> RESERVED)
#define P9C_NOTE            2       // the engine's records changed (OWNED / ISOLATED)
#define P9C_DEAD            3       // owner gone (a later HwFindAdapter of its function); to FREE if no block was installed
#define P9C_E1              4       // E1: the CM table states DOWN_QUIESCED for the record's generation or a later one
#define P9C_E2              5       // E2: this load's controller of the same function shown disabled
#define P9C_RELINQUISH      6       // every block relinquished (end of EngStop, or a removal's acknowledged disable)
#define P9C_BAD             7       // failed validation: out of circulation for the boot
#define P9C_UNCLAIM         8       // allocation or its install failed after the claim (back to FREE)

// Deferred boot-block work (P9 Pending)
#define P9P_E2              0x01
#define P9P_RELINQUISH      0x02

//
// Per adapter instance, in the device extension (HW_DEVICE_EXTENSION.Pool9x).
//
typedef struct _PLAT_POOL9X {
    ULONG Token;                // instance token in the boot block, 0 none
    LONG Rec;                   // boot-block record of this load's block, -1 none
    ULONG Linear;               // block: linear address (also the VMM handle)
    ULONG Phys;                 //        physical address of its first page (contiguous)
    ULONG Pages;                //        pages, the last one the metadata page
    ULONG E2Mask;               // QUARANTINED records of this function awaiting E2
    ULONG Pending;              // P9P_*
    ULONG FinalState;           // PLAT_OWN_RELEASED or _QUARANTINED, for P9P_RELINQUISH
    ULONG ProofCookie;          // current-boot proof cookie, 0 none (R3a, R5b)
    ULONG ProofTable[3];        // proof "TableLinear" {linear, size, cookie}
    PVOID Table;                // PSTOR98LS_TABLE, validated (R5), or NULL
    PVOID Entry;                // PSTOR98LS_ENTRY bound to (R8d), or NULL
    ULONG Gen;                  // LinkGeneration bound at HwFindAdapter or by the late lookup
    ULONG BindSeq;              // RemoveSequence present at binding (R8d; 0 at a late bind after the find, p9bind.h)
    ULONG LateOn;               // P9L_*: the late lookup runs (table kept, no entry yet; p9bind.h)
    ULONG LateStartMs;          // VMM ms at the find (its window, P9_LATE_BIND_MS)
    ULONG LateFloor;            // generation seen DOWN_QUIESCED: only a newer one is bound
    ULONG LateRc;               // last P9B_* result
    ULONG LateScans;
    ULONG TableCookie;
    ULONG RendezvousRc;
    ULONG BadSnapshots;
    ULONG LinkGen;              // statement reported by PlatCmLinkPoll, awaiting the acknowledgement
    ULONG LinkApplied;          // generation whose statement was applied (0 none)
    BOOLEAN LinkQuiesced;       // that statement was DOWN_QUIESCED (only then AckGeneration is written)
    BOOLEAN IdentityLost;
    BOOLEAN Ended;              // this instance's function token was ended (terminal)
    BOOLEAN DisableAcked;       // this load's controller shown disabled (CC.EN=0, RDY=0) before its first enable: E2
    // Removal transaction (R9-R11)
    ULONG RmSeq;                // accepted RemoveSequence (R9b), 0 none
    ULONG RmStep;               // P9R_*
    ULONG RmStartMs;            // VMM ms when the request was first seen (the overall budget, R10)
    ULONG RmStepMs;             // VMM ms at the step's start
    ULONG RmOutcome;            // STOR98RM_* once acknowledged
    ULONG RmDetail;             // what completed: P9RD_*
    ULONG RmOverlap;            // last overlapping RemoveSequence logged
    ULONG RmBusy;               // SRBs answered BUSY before the acknowledgement
    ULONG RmRefused;            // SRBs refused after it
    PVOID RmEntry;              // the entry the request came on (the acknowledgement goes there)
    BOOLEAN RmAccepted;         // terminal from here on (R9d)
    BOOLEAN RmAcked;            // RemoveAck written (immutable, R9c)
    BOOLEAN RmHandedOver;       // FAILED acknowledged and presence lost: the engine's gone state runs
    BOOLEAN RmFlushSubmitted;
    ULONG TestIsrProbe;         // test builds: the timer calls this adapter's ISR while a transaction runs
    ULONG TestShnPrime;         // test builds: a shutdown episode precedes the request (1 SHST timed out, 2 complete)
    ULONG TestBudgetSkewMs;     // test builds: added to the transaction's elapsed time (a late first sight, R10)
    ULONG TestCorruptAcct;      // test builds: damage this boot's accounting when the blocks are relinquished (1-4)
} PLAT_POOL9X;

// Removal transaction steps (PLAT_POOL9X.RmStep)
#define P9R_NONE            0
#define P9R_DECIDE          1       // accepted; the next timer tick chooses the first step
#define P9R_DRAIN           2       // admitted requests finish (2 s)
#define P9R_FLUSH           3       // ordered Flush (2 s)
#define P9R_SHN             4       // CC.SHN, waiting for SHST (2 s)
#define P9R_DISABLE         5       // CC.EN=0, waiting for RDY=0 (min(CAP.TO, 5 s), within the budget)
#define P9R_DONE            6       // CLEAN or ISOLATED_NOT_CLEAN acknowledged; blocks RELEASED
#define P9R_FAILED          7       // FAILED acknowledged; waits for presence loss (the CM isolates first)

// RmDetail bits (STG_P9_RM_ACK argument, bits 31:24)
#define P9RD_DRAIN          0x01
#define P9RD_FLUSH          0x02
#define P9RD_SHST           0x04
#define P9RD_RDY0           0x08
#define P9RD_BUDGET         0x10    // the overall budget forced the disable
#define P9RD_GONE           0x20    // presence lost during the transaction
#define P9RD_LINKDOWN       0x40    // DOWN_QUIESCED had been applied before the request
#define P9RD_STATE          0x80    // decided from the engine state at acceptance (no data-path step ran)

#define STG_P9_RM_SEEN      0x12A   // marker; arg: RemoveSequence (15:0) | engine state << 16 | generation << 24
#define STG_P9_RM_STEP      0x12B   // arg: step | ms since the request << 8
#define STG_P9_RM_ACK       0x12C   // marker; arg: outcome | RemoveSequence (15:0) << 8 | detail << 24
#define STG_P9_RM_REFUSED   0x12D   // arg: SRBs refused after the acknowledgement (first 5, then every 64th)
#define STG_P9_RM_OVERLAP   0x12E   // arg: overlapping RemoveSequence ignored (R9b)
#define STG_P9_ISR_PROBE    0x12F   // test builds (IsrProbe): arg: ISR result | step << 8 | issued children << 16
#define STG_P9_RM_BUSY      0x130   // arg: BUSY count (15:0) | SRB address bits 15:4 << 16 (first 255)
#define STG_P9_TEST         0x131   // test builds: arg: options read (ShnFlush | IsrProbe << 1 | Frag KiB << 8)
#define STG_P9_LATE         0x132   // marker; arg: P9B_* | ms since the find / 10 (12 bits) << 4 | generation << 16 | bus << 24 (late lookup: on a change of result and at its end)

#endif // STOR98_DMA_POOL && NVME2K_W9X

#endif // _STOR98_POOL9X_H_
