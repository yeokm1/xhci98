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
// Request engine and adapter lifecycle (docs/PLAN.md phase 0: 0.2 parent/child
// engine, 0.3 admission control, 0.4 per-child PRP construction, 0.7 states,
// 0.10 recovery contract, 0.11 callback matrix).
//
// Serialisation: SCSIport runs HwStartIo, HwInterrupt, the timer routine,
// HwResetBus and HwAdapterControl serialised against each other (the
// interrupt spin lock on NT, the single-threaded port on 9x), so the engine
// needs no locks of its own. "Under the parent's lock" in PLAN 0.10 is this
// serialisation.
//

#ifndef _STOR98_ENGINE_H_
#define _STOR98_ENGINE_H_

#include "polltun.h"
#include "xfertun.h"
#include "pcicap.h"

//
// Adapter states (PLAN 0.7). One variable, changed only through
// EngSetState, which logs every transition (STG_STATE).
//
//   starting     HwInitialize wrote CC.EN (or is re-enabling); bring-up runs
//                from the timer. Requests are held (hold-then-BUSY), no child
//                is ever issued.
//   running      queues live, children issued.
//   resetting    HwResetBus or watchdog recovery wrote CC.EN=0 and the
//                disable is not yet acknowledged; issued SRBs stay owned.
//                Also entered from `gone` on a reopen violation (contact
//                regained with CC.EN=1 or RDY=1, PLAN 0.10 reopen
//                invariant); its acknowledgement then ends in `failed`.
//   shutdown     restartable shutdown notification (PLAN 0.11 class 2):
//                CC.SHN written; SHST waited by the timer, then quiet with
//                every resource retained. The next request re-enables.
//   stopping     inside ScsiStopAdapter / HwAdapterState(FALSE) (class 3).
//   stopped      after the stop; nothing runs, the timer never rearms.
//   gone         presence lost (all-ones CSTS): issued SRBs held until
//                isolation evidence or their gone deadline (MD-1), new SRBs
//                fast-fail with the terminal status (PLAN 2.2). The adapter
//                stays here after the MD-1 deadline (the deadline's children
//                are quarantined, EngQuarantineParentChildren), so new SRBs
//                keep the terminal status; contact regained leaves it for
//                `failed` (fresh controller) or `resetting` (reopen
//                violation).
//   failed       present but fatal, bring-up failed, or the controller was
//                disabled after a failure. Requests fail per the parent error
//                mapping. Memory owned; released when the disable was
//                acknowledged.
//   quarantined  failed without an acknowledged disable (bring-up, a reset
//                or the reopen wait past CAP.TO): issued SRBs stay owned,
//                driver-owned DMA memory stays out of every pool and is
//                never reused. The timer keeps checking CSTS at a low rate
//                and writes CC.EN=0 again while it reads 1 (PLAN 0.10
//                unresponsive row).
//
// HwResetBus (EngResetBus) returns TRUE on an acknowledged disable, or with
// no child issued (in `quarantined`, none quarantined either): the SRBs it
// then completes BUS_RESET have nothing the device could reach, and no
// hardware action is taken. FALSE in `gone`, and on 9x in `starting`
// (PLAN 0.10 entry points).
//
#define ENG_ST_NONE         0   // before HwInitialize
#define ENG_ST_STARTING     1
#define ENG_ST_RUNNING      2
#define ENG_ST_RESETTING    3
#define ENG_ST_SHUTDOWN     4
#define ENG_ST_STOPPING     5
#define ENG_ST_STOPPED      6
#define ENG_ST_GONE         7
#define ENG_ST_FAILED       8
#define ENG_ST_QUARANTINED  9

// Bring-up sub-steps while ENG_ST_STARTING (or ENG_ST_SHUTDOWN, for the
// SHST wait).
#define ENG_BU_NONE         0
#define ENG_BU_DISABLING    1   // CC.EN=0 written (previous owner or re-enable); waiting for RDY=0
#define ENG_BU_WAIT_RDY     2   // CC.EN=1 written; waiting for RDY=1
#define ENG_BU_ADMIN        3   // RDY seen: create I/O queues, identify (admin CQ polled by the timer)
#define ENG_BU_FAIL_DISABLE 4   // bring-up failed after CC.EN: CC.EN=0 written, waiting for RDY=0
#define ENG_BU_SHN_WAIT     5   // CC.SHN written, waiting for SHST complete (shutdown state)
#define ENG_BU_SHN_DONE     6   // shutdown notification complete (shutdown state)

// Bring-up failure reasons (STG_BRINGUP_FAIL)
#define ENG_FAIL_RDY_TIMEOUT    1
#define ENG_FAIL_CFS            2
#define ENG_FAIL_ADMIN_STATUS   3
#define ENG_FAIL_ADMIN_TIMEOUT  4
#define ENG_FAIL_PRESENCE       5
#define ENG_FAIL_DISABLE_TIMEOUT 6
#define ENG_FAIL_SUBMIT         7
#define ENG_FAIL_CAPS           8   // queue capability validation (0.9)
#define ENG_FAIL_RESET_TIMEOUT  9   // reset disable not acknowledged within CAP.TO
// 10 was ENG_FAIL_STORM: retired in review round 3 (N1), a storm never disables the controller
#define ENG_FAIL_IDLE_FLUSH     11  // the internal idle Flush missed its deadline (no port timeout covers it)
#define ENG_FAIL_REOPEN         12  // contact regained in gone with CC.EN=1 or RDY=1 (reopen invariant, PLAN 0.10)

//
// Capacity (PLAN 0.3; TESTING "Test build settings": 32 parents). Children
// are further limited at run time by the I/O queue size (one slot is always
// kept free, so a submission never finds the queue full).
//
#define ENG_MAX_PARENTS     32
#define ENG_MAX_CHILDREN    64
#define ENG_PRP_PAGES       8   // PRP list pages (also the Get Log Page data pages)
#define ENG_BOUNCE_PAGES    8   // bounce buffer pages (PLAN 0.4, MD-3 (a): uncached extension)
#define ENG_ADMIN_CHILDREN  4   // admin children at once (Get Log Page)
#define ENG_BUSY_TRACK      ENG_MAX_PARENTS // SRBs whose BUSY completions are counted (0.3, Q-NT)
#define ENG_BUSY_LIMIT      20  // never a 21st BUSY for one SRB (Q-NT: the port fails it after 21)
#define ENG_SENSE_KEEP      4   // undelivered sense entries kept for REQUEST SENSE (sense.c)

#define ENG_NONE            0xFF

// Child states
#define ENG_CH_FREE         0
#define ENG_CH_ISSUED       1
#define ENG_CH_QUARANTINED  2   // never reused: no isolation evidence for it (MD-1, unresponsive row)

// Child "internal" kinds (no parent)
#define ENG_INT_NONE        0
#define ENG_INT_IDLE_FLUSH  1   // MD-7 (a)
#define ENG_INT_STOP_FLUSH  2   // stop path (0.10 stop)

// Parent states
#define ENG_PA_FREE         0
#define ENG_PA_WAITING      1   // admitted; children still to dispatch (none yet, or more)
#define ENG_PA_ISSUED       2   // every required child dispatched; waiting for completions
#define ENG_PA_PARKED       3   // ABORT/RESET SRB waiting on an event, outside dispatch order
#define ENG_PA_SHN          4   // SHUTDOWN SRB: flush done, waiting for SHST

// Parent kinds
#define ENG_K_RW            1   // READ/WRITE(6/10/16)
#define ENG_K_FLUSH         2   // SYNCHRONIZE CACHE, SRB_FUNCTION_FLUSH
#define ENG_K_SHUTDOWN      3   // SRB_FUNCTION_SHUTDOWN (class 2): Flush, CC.SHN, SHST
#define ENG_K_LOG           4   // NVMe Get Log Page for LOG SENSE / SAT / SMART IOCTL
#define ENG_K_HELD          5   // any SRB held while not running; replayed through ScsiDispatch when running
#define ENG_K_ABORT         6   // SRB_FUNCTION_ABORT_COMMAND waiting for its target
#define ENG_K_RESET         7   // SRB_FUNCTION_RESET_BUS/DEVICE waiting for the disable acknowledgement

// Parent flags
#define ENG_PF_WRITE        0x01
#define ENG_PF_BARRIER      0x02    // ORDERED tag, or SHUTDOWN: waits for every earlier parent, blocks later ones
#define ENG_PF_TAGGED       0x04
#define ENG_PF_STARTED      0x08    // at least one child issued: never BUSY from now on (0.3)
#define ENG_PF_PRESET       0x10    // SrbStatus already set by the completion handler (ENG_K_LOG)
#define ENG_PF_REPLAYED     0x20    // admitted by the replay of a held SRB
#define ENG_PF_FUA          0x40    // READ/WRITE(10)/(16) with FUA (CDB byte 1 bit 3): every child carries NVMe FUA

// Cancellation (PLAN 0.10 "Parent cancellation", H1)
#define ENG_CANCEL_NONE     0
#define ENG_CANCEL_ABORT    1       // ABORTED
#define ENG_CANCEL_RESET    2       // BUS_RESET
#define ENG_CANCEL_GONE     3       // terminal status of 2.2

// Gone-state sources (STG_GONE_ENTER)
#define ENG_GONE_TIMER      1
#define ENG_GONE_STARTIO    2
#define ENG_GONE_RESET      3
#define ENG_GONE_BRINGUP    4
#define ENG_GONE_STOP       5

typedef struct _ENG_CHILD {
    UCHAR State;            // ENG_CH_*
    UCHAR Parent;           // parent index, or ENG_NONE for an internal command
    UCHAR Opcode;
    UCHAR Admin;            // TRUE: admin queue
    UCHAR PrpPage;          // PRP list (or log data) page, ENG_NONE if none
    UCHAR BouncePage;       // first bounce page, ENG_NONE if not bounced
    UCHAR BounceCount;
    UCHAR Internal;         // ENG_INT_*
    USHORT Gen;             // CID generation of this slot
    USHORT Cid;
    ULONG Bytes;
    ULONG BufOffset;        // offset into the parent SRB's data buffer
    ULONG CtrlGen;          // controller generation at issue
    ULONG FlushGen;         // flush children: write generation covered
    ULONG IssueTime;        // clock units
} ENG_CHILD, *PENG_CHILD;

typedef struct _ENG_PARENT {
    PSCSI_REQUEST_BLOCK Srb;
    UCHAR State;            // ENG_PA_*
    UCHAR Kind;             // ENG_K_*
    UCHAR Flags;            // ENG_PF_*
    UCHAR Cancel;           // ENG_CANCEL_*
    UCHAR ErrSrbStatus;     // first error (0: none), parent error mapping
    UCHAR ErrScsiStatus;
    UCHAR SenseKey;
    UCHAR Asc;
    UCHAR Ascq;
    UCHAR LogId;            // ENG_K_LOG: log page
    UCHAR Target;           // ENG_K_ABORT: target parent index
    UCHAR Reserved;
    USHORT Outstanding;     // children issued and not yet quiesced
    USHORT Issued;          // children ever issued
    USHORT NvmeStatus;      // first failing NVMe status (SCT << 8 | SC)
    USHORT Reserved2;
    ULONG Seq;              // admission order
    ULONG AdmitTime;        // clock units at admission (the port times the SRB from about here)
    ULONG AdmitSafe;        // safe clock (ms) at admission
    ULONGLONG NextLba;      // next block to dispatch (ENG_K_RW)
    ULONG BlocksLeft;       // blocks not yet dispatched
    ULONG NextOffset;       // byte offset of the next fragment
    ULONG Bytes;            // bytes the request transfers
    ULONGLONG PfNextPa;     // counter answer: the physical address the previous span ended at
    UCHAR PfRuns;           // physical runs seen so far, at most STOR98PF_RUN_WALK_MAX
    UCHAR PfNotSeen;        // a span was not translated (bounced): the layout is not known
    USHORT PfReserved;
} ENG_PARENT, *PENG_PARENT;

typedef struct _ENG_BUSY {
    PSCSI_REQUEST_BLOCK Srb;
    PVOID DataBuffer;       // with Function and Cdb0: what the counted SRB carried
    UCHAR Function;
    UCHAR Cdb0;
    USHORT Reserved;
    ULONG Count;
    ULONG Last;             // clock units
} ENG_BUSY;

//
// Tunables (PLAN 0.5): parsed once from DriverParameter in HwFindAdapter and
// kept in the device extension, which survives resume (where ArgumentString
// is NULL). Defaults are the test build settings of TESTING.md.
//
typedef struct _ENG_TUNABLES {
    ULONG Magic;            // ENG_TUNABLES_MAGIC once parsed
    ULONG GoneMs;           // gone deadline (MD-1), 5 s
    ULONG IdleFlushMs;      // MD-7 (a) write-idle time before the timer's Flush
    ULONG GoneMarginMs;     // per-SRB bound margin (R6-6)
    ULONG HoldMarginMs;     // hold-then-BUSY: TimeOutValue minus this (0.3)
    ULONG Hold9xMs;         // 9x fixed hold bound (TimeOutValue unusable, G9)
    ULONG FragBytes;        // test: maximum child size (0 = MDTS / port limit)
    ULONG ForceBounce;      // test: bounce every data child (exercises 0.4 bounce)
    ULONG Sg9xFailEvery;    // test (9x): every Nth caller-buffer copy reports unresolved (failure paths)
    ULONG Sg9xFailCount;    // test: copies counted for Sg9xFailEvery
    ULONG CalPct;           // test: TSC calibration scaled to this percent (0 = as measured)
    ULONG ShnFlush;         // test: SRB_FUNCTION_FLUSH runs the restartable shutdown (late-Flush tests)
    ULONG CapToMs;          // test: bring-up and disable wait longer than CAP.TO (0 = CAP.TO)
    ULONG StormTest;        // test: provoke the QEMU INTx defect after bring-up (storm recovery tests)
    ULONG StormInts;        // test: empty interrupts per window taken as a storm (0 = ENG_STORM_INTS; shared-line tests)
    ULONG IdleFlushToMs;    // test: idle Flush deadline (0 = ENG_IDLE_FLUSH_TO_MS; held-idle-Flush test)
    POLL_SETTINGS Poll;     // polling mode (polltun.h; PLAN "Polling mode"), product settings
    ULONG MaxXferKb;        // maximum transfer, KiB (xfertun.h; StorMaxTransferKB / maxtransferkb=), product setting
    ULONG MaxXferWhy;       // XFER_DEFAULT, XFER_SET or XFER_REFUSED, from the last parse
} ENG_TUNABLES;

#define ENG_TUNABLES_MAGIC  0x4E393854  // "T89N"

// Timer periods
#define ENG_TICK_FAST_US    10000   // bring-up, SHST wait, reset wait
#define ENG_TICK_WATCH_US   100000  // children outstanding, held requests, gone, dirty
#define ENG_TICK_SLOW_US    1000000 // quarantined: low-rate CSTS check

// Interrupt storm (lifecycle.c, EngInterrupt): this many interrupts with
// nothing to consume within one window mask the vector (INTMS) and the
// timer polls the queues; as many again with the vector masked disable the
// controller (at most once per controller generation). The vector is
// unmasked on trial after the polling hold, which doubles up to its maximum.
#define ENG_STORM_INTS      1000
#define ENG_STORM_WINDOW_MS 1000
#define ENG_IDLE_FLUSH_TO_MS 30000 // idle Flush deadline (review round 4): no SRB, so no port timeout
#define ENG_POLL_HOLD_MS    2000
#define ENG_POLL_HOLD_MAX_MS 60000

// Stall budget (PLAN 0.10): 100 us per stall, under 1 ms per callback.
#define ENG_STALL_US        100
#define ENG_STALL_BUDGET    9       // stalls per callback (900 us)

//
// Entry points used by the miniport callbacks (engine.c, lifecycle.c)
//
struct _HW_DEVICE_EXTENSION;

VOID EngParseTunables(IN struct _HW_DEVICE_EXTENSION *DevExt, IN PCHAR ArgumentString);
VOID EngResetSoftState(IN struct _HW_DEVICE_EXTENSION *DevExt);
VOID EngSetState(IN struct _HW_DEVICE_EXTENSION *DevExt, IN UCHAR State, IN ULONG Reason);
BOOLEAN EngInitialize(IN struct _HW_DEVICE_EXTENSION *DevExt);
VOID EngTimer(IN PVOID DeviceExtension);
VOID EngTimerEnsure(IN struct _HW_DEVICE_EXTENSION *DevExt, IN ULONG PeriodUs);
BOOLEAN EngStartIo(IN struct _HW_DEVICE_EXTENSION *DevExt, IN PSCSI_REQUEST_BLOCK Srb);
BOOLEAN EngInterrupt(IN struct _HW_DEVICE_EXTENSION *DevExt);
BOOLEAN EngResetBus(IN struct _HW_DEVICE_EXTENSION *DevExt, IN ULONG PathId);
VOID EngStop(IN struct _HW_DEVICE_EXTENSION *DevExt);

// Request submission from the SCSI handlers
BOOLEAN EngSubmitReadWrite(IN struct _HW_DEVICE_EXTENSION *DevExt, IN PSCSI_REQUEST_BLOCK Srb);
// Counter answer (docs/PLAN.md MD-8): a data SRB at HwStartIo's entry, and a
// software answer copied to the SRB's data (fragment by fragment on 9x;
// FALSE, nothing copied, when its buffer cannot be resolved).
VOID EngPfSrbIn(IN struct _HW_DEVICE_EXTENSION *DevExt, IN PSCSI_REQUEST_BLOCK Srb);
BOOLEAN EngCopyToCaller(IN struct _HW_DEVICE_EXTENSION *DevExt, IN PSCSI_REQUEST_BLOCK Srb, IN const UCHAR *Src,
                        IN ULONG Bytes);
BOOLEAN EngSubmitFlush(IN struct _HW_DEVICE_EXTENSION *DevExt, IN PSCSI_REQUEST_BLOCK Srb);
#if defined(NVME2K_W9X) && defined(STOR98_DBGPORT)
VOID EngProbeSg9x(IN PSCSI_REQUEST_BLOCK Srb);
#endif
BOOLEAN EngSubmitLogPage(IN struct _HW_DEVICE_EXTENSION *DevExt, IN PSCSI_REQUEST_BLOCK Srb, IN UCHAR LogId);
VOID EngCompleteSrb(IN struct _HW_DEVICE_EXTENSION *DevExt, IN PSCSI_REQUEST_BLOCK Srb);

// Completion paths (nvme2k_cpl.c)
VOID EngChildCompleted(IN struct _HW_DEVICE_EXTENSION *DevExt, IN BOOLEAN Admin, IN USHORT Cid,
                       IN USHORT Status, IN ULONG Dw0);
VOID EngKick(IN struct _HW_DEVICE_EXTENSION *DevExt);
VOID EngKickSoftware(IN struct _HW_DEVICE_EXTENSION *DevExt);
BOOLEAN EngProcessCompletions(IN struct _HW_DEVICE_EXTENSION *DevExt);


// Engine internals shared by engine.c and lifecycle.c
BOOLEAN EngHold(IN struct _HW_DEVICE_EXTENSION *DevExt, IN PSCSI_REQUEST_BLOCK Srb);
UCHAR EngBusyOrFail(IN struct _HW_DEVICE_EXTENSION *DevExt, IN PSCSI_REQUEST_BLOCK Srb);
VOID EngStartIoTail(IN struct _HW_DEVICE_EXTENSION *DevExt, IN BOOLEAN Tagged);
VOID EngDispatch(IN struct _HW_DEVICE_EXTENSION *DevExt);
BOOLEAN EngParentsWaiting(IN struct _HW_DEVICE_EXTENSION *DevExt);
ULONG EngFreeParents(IN struct _HW_DEVICE_EXTENSION *DevExt);
PENG_PARENT EngParentAlloc(IN struct _HW_DEVICE_EXTENSION *DevExt, IN PSCSI_REQUEST_BLOCK Srb, IN UCHAR Kind);
VOID EngFinishParent(IN struct _HW_DEVICE_EXTENSION *DevExt, IN ULONG Index);
VOID EngCancelParent(IN struct _HW_DEVICE_EXTENSION *DevExt, IN ULONG Index, IN UCHAR Cause);
VOID EngChildrenSettle(IN struct _HW_DEVICE_EXTENSION *DevExt, IN BOOLEAN Evidence);
VOID EngQuarantineParentChildren(IN struct _HW_DEVICE_EXTENSION *DevExt, IN ULONG Index);
BOOLEAN EngSubmitInternalFlush(IN struct _HW_DEVICE_EXTENSION *DevExt, IN UCHAR Internal);
VOID EngShutdownFlushed(IN struct _HW_DEVICE_EXTENSION *DevExt, IN ULONG Index, IN USHORT Status);
ULONG EngIssuedChildren(IN struct _HW_DEVICE_EXTENSION *DevExt);
VOID EngEnterGone(IN struct _HW_DEVICE_EXTENSION *DevExt, IN ULONG Source);

// Engine CIDs carry bit 15; bring-up admin CIDs (1-5) do not.
#define ENG_CID_FLAG        0x8000
#define ENG_CID_INDEX(c)    ((c) & 0x3F)
#define ENG_CID_GEN(c)      (((c) >> 6) & 0x1FF)
#define ENG_MAKE_CID(i, g)  ((USHORT)(ENG_CID_FLAG | (((g) & 0x1FF) << 6) | ((i) & 0x3F)))

#endif // _STOR98_ENGINE_H_
