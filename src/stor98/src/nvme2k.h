// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright (c) 2025, Dominik Behr (nvme2k)
// Windows 9x/NT4 port and changes (nvme9x/nvment): Copyright (c) SweetLow
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
// NVMe Driver for Windows 2000 - Header File
// Contains all defines, constants, and type definitions
//

#ifndef _NVME2K_H_
#define _NVME2K_H_

#include <miniport.h>
#include <scsi.h>
#include <devioctl.h>
#include <ntddscsi.h>
#include "atomic.h"
#include "nvme.h"
#include "scsiext.h"
#include "scsi64.h"
#include "clock.h"
#include "engine.h"
#include "platform.h"
#include "pool9x.h"
#ifndef NVME2K_W9X
#include "cmlink.h"
#endif
#include "dbgport.h"
#include "pfstat.h"

//#define NVME2K_W9X

// !!! define NVME2K_NT4 when you build sources by 2000 DDK for NT4 target
// no need to comment if you defined NVME2K_W9X too
//#define NVME2K_NT4

// uncomment to enable debugging and logging
/* for debug messages use
 * ScsiDebugPrint(0, "nvme2k: ...\n");
 * it seems only level 0 messages are displayed, possibly registry changes are needed
 * 
 */

//#define NVME2K_DBG
// extra spammy logging for NVMe commands
//#define NVME2K_DBG_CMD
//#define NVME2K_DBG_EXTRA
//#define NVME2K_DBG_STATS
//#define NVME2K_DBG_TOOMUCH

//
// Synchronization control - comment out to disable specific locks
//
//#define NVME2K_USE_INTERRUPT_LOCK       // Lock to serialize HwInterrupt on SMP
//#define NVME2K_USE_SUBMISSION_LOCK      // Lock to serialize command submission
//#define NVME2K_USE_COMPLETION_LOCK      // Lock to serialize completion processing

#if !defined(NVME2K_NT4) && (defined(NVME2K_W9X) || (_WIN32_WINNT < 0x500))
// auto defining if you defined NVME2K_W9X
// auto defining if you use NT4 DDK
#define NVME2K_NT4
#endif

#ifdef NVME2K_NT4
//Size check
//You have to compile with SCSI.H and SRB.H from NT4 or 98/SE DDK

typedef UCHAR HW_INITIALIZATION_DATA_SIZE_CHECK[sizeof(HW_INITIALIZATION_DATA) == 76 ? 1 : -1];
#endif

// Change PCI Command Register instead of overriding it
#define CHANGE_PCI_COMMAND_REGISTER

//
// Memory constants
//
#define PAGE_SIZE                           0x1000  // 4KB page size
#define PAGE_SHIFT                          12      // 4KB page size

//
// NVMe PCI Class Codes
//
#define PCI_CLASS_MASS_STORAGE_CONTROLLER   0x01
#define PCI_SUBCLASS_NON_VOLATILE_MEMORY    0x08
#define PCI_PROGIF_NVME                     0x02

//
// PCI Configuration Space Offsets
//
#define PCI_VENDOR_ID_OFFSET                0x00
#define PCI_DEVICE_ID_OFFSET                0x02
#define PCI_COMMAND_OFFSET                  0x04
#define PCI_STATUS_OFFSET                   0x06
#define PCI_REVISION_ID_OFFSET              0x08
#define PCI_CLASS_CODE_OFFSET               0x09
#define PCI_HEADER_TYPE_OFFSET              0x0E
#define PCI_BASE_ADDRESS_0                  0x10
#define PCI_BASE_ADDRESS_1                  0x14    // high dword of a 64-bit BAR0
#define PCI_SUBSYSTEM_VENDOR_ID_OFFSET      0x2C
#define PCI_SUBSYSTEM_ID_OFFSET             0x2E
#define PCI_INTERRUPT_LINE_OFFSET           0x3C
#define PCI_INTERRUPT_PIN_OFFSET            0x3D

//
// PCI Command Register bits
//
#define PCI_ENABLE_IO_SPACE                 0x0001
#define PCI_ENABLE_MEMORY_SPACE             0x0002
#define PCI_ENABLE_BUS_MASTER               0x0004
#define PCI_INTERRUPT_DISABLE               0x0400  // Bit 10: Disable INTx interrupt assertion

//
// PCI Ststus Register bits
//
#define PCI_INTERRUPT_STATUS                0x0008  // Bit 3: Interrupt Status (IS): Indicates the interrupt status of the device ('1' = asserted).

//
// Doorbell registers (stride determined by CAP.DSTRD)
// (not defined in nvme.h as it's driver-specific)
//
#define NVME_REG_DBS        0x1000  // Doorbell base


//
// Queue sizes (PLAN 0.9): the I/O and admin queues each take one page, so
// at most 64 entries; the size actually used is the largest power of two
// that is at most min(64, CAP.MQES + 1), validated before CC.EN is written.
//
#define NVME_MAX_QUEUE_SIZE     (PAGE_SIZE/NVME_SQ_ENTRY_SIZE)

//
// Uncached extension layout (MD-3 (a): driver DMA memory lives in the port's
// uncached extension on NT; on 9x the MD-4 pool of pool9x.c, see platform.h).
// Every block is page aligned against its physical address (PLAN 0.5), so
// one page of slack is requested.
//
//   admin SQ, admin CQ, I/O SQ, I/O CQ     4 pages
//   utility page (identify data)           1 page
//   PRP list / log data pages              ENG_PRP_PAGES
//   bounce pages                           ENG_BOUNCE_PAGES
//   physical alignment slack               1 page
//
#define UNCACHED_EXTENSION_PAGES    (4 + 1 + ENG_PRP_PAGES + ENG_BOUNCE_PAGES + 1)
#define UNCACHED_EXTENSION_SIZE     (PAGE_SIZE * UNCACHED_EXTENSION_PAGES)

// Largest transfer offered to the port before its own limits (PLAN 0.8),
// the build's maximum: the setting StorMaxTransferKB (9x) or stormaxtransferkb=
// (NT) can lower it at load time, never raise it (xfertun.h).
// Windows 9x: 2 MiB (owner, 2026-10-22; PLAN "Decided 2026-10-22"; 512 KiB
// from PLAN "0.4.0.0 plan", item 1). IOS sends a request whole only up to
// MaximumTransferLength and cuts a larger one into 63 KiB SRBs at queue
// depth 1; on SCSIPORT's physical-list path IOS's own ceiling is 512 KiB
// and 17 runs (measured, 98 SE), so more than 512 KiB reaches this driver
// only through xhci98's S98VSD.VXD, which drops that path (xhci98 design
// record 30). Without it the larger value changes nothing. NT: 256 KiB,
// unchanged.
// What it bounds: the largest child (with MDTS, LcBringupDone) and so the
// PRP list of one child. A buffer of N pages at a non-zero offset touches
// N + 1 pages, N entries in the list; one PRP list page holds 512 entries,
// so one list page covers any child up to this. The PRP list pages
// (ENG_PRP_PAGES), the bounce pages (ENG_BOUNCE_PAGES; a bounced child is
// cut to at most that many pages) and so the DMA block (UNCACHED_EXTENSION_PAGES,
// the 9x pool's per-adapter block and its boot-wide cap) are counted per
// child, not per transfer, and do not depend on it.
#ifdef NVME2K_W9X
#define STOR98_MAX_TRANSFER         (512 * PAGE_SIZE)
#else
#define STOR98_MAX_TRANSFER         (64 * PAGE_SIZE)
#endif
#define STOR98_PRP_PER_PAGE         (PAGE_SIZE / 8)

// One PRP list page per child (EngMapCaller), and a child's block count in
// CDW12's 16-bit NLB field at the smallest block size (512 bytes).
typedef UCHAR STOR98_MAX_TRANSFER_PRP_CHECK[(STOR98_MAX_TRANSFER / PAGE_SIZE <= STOR98_PRP_PER_PAGE) ? 1 : -1];
typedef UCHAR STOR98_MAX_TRANSFER_NLB_CHECK[(STOR98_MAX_TRANSFER / 512 <= 0x10000) ? 1 : -1];

//
// NVMe Queue Pair
//
typedef struct _NVME_QUEUE {
    PVOID SubmissionQueue;
    PVOID CompletionQueue;
    PHYSICAL_ADDRESS SubmissionQueuePhys;
    PHYSICAL_ADDRESS CompletionQueuePhys;
    ULONG SubmissionQueueHead;   // from the CQE SQHead field
    ULONG SubmissionQueueTail;
    ULONG CompletionQueueHead;   // Monotonic counter. Phase = (head >> QueueSizeBits) & 1
    USHORT QueueSizeMask;        // QueueSize - 1, for index masking
    USHORT QueueId;
    USHORT QueueSize;
    UCHAR QueueSizeBits;         // log2(QueueSize), for phase calculation
    UCHAR Reserved;
} NVME_QUEUE, *PNVME_QUEUE;

//
// SRB Extension - per-request data stored by ScsiPort. The engine keeps its
// state in the device extension (bounded parent table); the SRB extension
// only records the parent index for debugging.
//
typedef struct _NVME_SRB_EXTENSION {
    UCHAR Parent;
    UCHAR Reserved[3];
} NVME_SRB_EXTENSION, *PNVME_SRB_EXTENSION;

//
// Admin Command IDs of the bring-up sequence (no ENG_CID_FLAG)
//
#define ADMIN_CID_CREATE_IO_CQ          1
#define ADMIN_CID_CREATE_IO_SQ          2
#define ADMIN_CID_IDENTIFY_CONTROLLER   3
#define ADMIN_CID_IDENTIFY_NAMESPACE    4
#define ADMIN_CID_SET_VWC               5   // D3: Set Features FID 06h, WCE=0
#define ADMIN_CID_GET_VWC               6   // D3: Get Features FID 06h (current), verifies WCE=0

#define STOR98_DEVEXT_MAGIC             0x3839454E  // "NE98": device extension initialised once

//
// Device extension structure - stores per-adapter data
//
typedef struct _HW_DEVICE_EXTENSION {
    ULONG Magic;                                    // STOR98_DEVEXT_MAGIC after the first HwFindAdapter
    ULONG BusNumber;
    ULONG SlotNumber;
    USHORT VendorId;
    USHORT DeviceId;
    USHORT SubsystemVendorId;
    USHORT SubsystemId;
    UCHAR RevisionId;
    BOOLEAN Resumed;                                // HwFindAdapter on an initialised extension (resume)
    BOOLEAN IdentifyDone;                           // identify succeeded in this start (cleared by EngResetSoftState)
    BOOLEAN AdminChainDone;                         // bring-up admin chain finished
    PVOID ControllerRegisters;
    ULONG ControllerRegistersLength;

    // Controller capabilities (validated before CC.EN, PLAN 0.9)
    ULONGLONG ControllerCapabilities;
    ULONG Version;
    ULONG PageSize;
    ULONG DoorbellStride;
    USHORT MaxQueueEntries;
    USHORT Reserved1;
    ULONG CapTimeoutMs;                             // CAP.TO in ms (bring-up and disable deadlines)

    NVME_QUEUE AdminQueue;
    NVME_QUEUE IoQueue;

    // Controller and namespace information
    ULONG NumberOfNamespaces;
    UCHAR ControllerSerialNumber[21];
    UCHAR ControllerModelNumber[41];
    UCHAR ControllerFirmwareRevision[9];
    BOOLEAN SMARTEnabled;
    UCHAR Mdts;                                     // Identify Controller MDTS (units of CAP.MPSMIN)
    UCHAR Reserved2[3];
    ULONGLONG NamespaceSizeInBlocks;
    ULONG NamespaceBlockSize;

    // Uncached extension and its sub-allocations (PLAN 0.5)
    PHYSICAL_ADDRESS UncachedExtensionPhys;
    PVOID UncachedExtensionBase;
    ULONG UncachedExtensionSize;
    ULONG UncachedExtensionOffset;
    PVOID UtilityBuffer;
    PHYSICAL_ADDRESS UtilityBufferPhys;
    PVOID PrpListPages;
    PHYSICAL_ADDRESS PrpListPagesPhys;
    ULONG PrpListPageBitmap;                        // allocated
    ULONG PrpQuarantineBitmap;                      // quarantined: never freed (MD-1)
    ULONG CurrentPrpListPagesUsed;
    ULONG MaxPrpListPagesUsed;
    PVOID BouncePages;
    PHYSICAL_ADDRESS BouncePagesPhys;
    ULONG BounceBitmap;
    ULONG BounceQuarantineBitmap;
    PHYSICAL_ADDRESS PrpPagePa[ENG_PRP_PAGES];      // each page translated (PLAN 0.4: validated mappings)
    PHYSICAL_ADDRESS BouncePagePa[ENG_BOUNCE_PAGES];
    BOOLEAN QueuesAllocated;
    BOOLEAN IntxEnabled;
    UCHAR Reserved3[2];

    // Lifecycle (PLAN 0.7)
    UCHAR State;                                    // ENG_ST_*
    UCHAR Bringup;                                  // ENG_BU_*
    BOOLEAN TimerActive;
    BOOLEAN TimerFirstSeen;
    BOOLEAN NextOwed;                               // NextRequest withheld (0.3)
    BOOLEAN NextLuOwed;                             // NextLuRequest owed after a tagged SRB (engine.c, EngNotifyReady)
    BOOLEAN BusChangeSent;
    BOOLEAN ContactLogged;
    BOOLEAN ResetIsReopen;                          // disable started for a reopen violation in gone: fail after the acknowledgement, do not re-create
    ULONG TimerPeriodUs;
    ULONG StateSafe;                                // safe clock (ms, clock.h) at the last state or step change
    ULONG ResetStartSafe;
    ULONG ShnSafe;
    ULONG StateTime;                                // clock units at the last state or step change
    ULONG InitTime;                                 // clock units at HwInitialize
    ULONG GoneTime;                                 // clock units at entry into gone
    ULONG GoneSafe;
    ULONG GoneSummarySec;
    ULONG FailReason;
    ULONG AdminError;                               // CID | status << 16 of a failed bring-up command
    ULONG CtrlGen;                                  // controller generation: bumped at every enable
    ULONG ResetCalls;
    ULONG ResetStartTime;
    ULONG ShutdownCount;
    ULONG ShnTime;

    // Durability (PLAN 0.10 "Durability at stop", MD-7 (a))
    ULONG WriteGen;                                 // write children submitted
    ULONG CleanGen;                                 // writes covered by a completed ordered Flush
    ULONG LastWriteTime;
    ULONG OutstandingWrites;                        // write children issued, not yet completed
    ULONG IdleFlushTime;                            // clock ms and safe ms when the idle Flush was submitted (its deadline)
    ULONG IdleFlushSafe;
    BOOLEAN IdleFlushOutstanding;
    BOOLEAN StopFlushOutstanding;
    USHORT StopFlushStatus;
    BOOLEAN ShnFlushOk;                             // this shutdown episode: its Flush succeeded and covered every write
    BOOLEAN ShnShstOk;                              // this shutdown episode: SHST complete seen (not timed out)

    // Request engine (PLAN 0.2, 0.3, 0.4)
    BOOLEAN InDispatch;                             // EngDispatch running (no re-entry)
    BOOLEAN ResetIsFailure;                         // disable started for CFS: fail, do not re-create
    BOOLEAN GoneDeadlineTaken;                      // MD-1 outcome taken in this gone episode
    BOOLEAN GoneDeadlineLogged;
    BOOLEAN GoneEvidence;                           // isolation evidence applied in this gone: the MD-1 deadline is cancelled
    UCHAR WtConfig;                                 // D3 `WriteThrough` as configured (PLAT_WT_*, AUTO unresolved)
    UCHAR WtMode;                                   // D3 resolved mode: OFF, CACHEOFF or FUA
    BOOLEAN WtFua;                                  // this bring-up: FUA on every write (FUA mode, or VWC off refused)
    BOOLEAN Vwc;                                    // Identify Controller VWC bit 0: a volatile write cache is present
    USHORT VwcSetStatus;                            // Set Features (VWC off) status of this bring-up
    ULONG WtFuaWrites;                              // writes issued with FUA
    BOOLEAN CapWaitLogged;
    ULONG ReplayAdmitTime;                          // admission time carried over when a held SRB is replayed
    ULONG ReplayAdmitSafe;
    ULONG ParentSeq;
    ULONG ChildLimitIo;                             // children at once on the I/O queue
    ULONG ChildrenIo;                               // issued on the I/O queue
    ULONG ChildrenAdmin;                            // issued on the admin queue
    ULONG MaxChildBytes;
    ULONG ParentsAdmitted;
    ULONG SrbsCompleted;                            // every RequestComplete (EngCompleteSrb)
    ULONG ParentsCompleted;
    ULONG ChildrenIssued;
    ULONG StaleCqes;
    ULONG DoubleBlocked;
    ULONG FastFails;
    ULONG BusyReturned;
    ULONG GoneDeadlineCount;
    ULONG Quarantined;                              // children quarantined
    ULONG QuarantinedAdmin;                         // of them, admin children (their CQ stays drained)
    BOOLEAN AdminCqLive;                            // admin queue initialised by an enable (LcEnable)
    ULONG LateCqes;                                 // completions of quarantined children consumed
    UCHAR DmaProvider;                              // PLAT_DMA_* (platform.h)
    ULONG DmaGapEvents;                             // quarantines of memory the port frees at removal (MD-3 window / 9x MD-4 gap)
    PLAT_DMA_BLOCK DmaBlocks[PLAT_DMA_BLOCKS];      // allocation records (platform.h)
#ifndef NVME2K_W9X
    CML_STATE CmLink;                               // NT CM link-state receiver (cmlink.h, cmlinknt.c)
#endif
#if defined(STOR98_DMA_POOL) && defined(NVME2K_W9X)
    PLAT_POOL9X Pool9x;                             // 9x MD-4 pool provider state (pool9x.h)
#endif
    UCHAR SenseCount;                               // undelivered sense entries kept for LU 0 (sense.c, R3-9)
    UCHAR SenseHead;                                // oldest entry
    UCHAR SenseLost;                                // placeholders queued after the kept entries: sense lost on overflow (N4)
    UCHAR SenseQ[ENG_SENSE_KEEP][18];               // fixed-format sense, one entry per request that failed without autosense
    ULONG SenseDropped;                             // entries not kept: all ENG_SENSE_KEEP in use
    ULONG DisableRewrites;                          // quarantine: CC.EN=0 written again
    ULONG EmptyInts;                                // interrupts with nothing new in the current storm window
    ULONG StormStart;                               // clock ms of the storm window's first empty interrupt
    ULONG StormMasks;                               // interrupt storms answered by masking the vector (polling)
    ULONG IdleFlushTimeouts;                        // idle Flushes that missed their deadline (review round 4)
    ULONG StormMaskedBatches;                       // storm batches with the vector already masked (not evidence; hold extended)
    ULONG PollSince;                                // clock ms when the vector was masked for a storm
    ULONG PollHoldMs;                               // polling time before the next unmask trial
    BOOLEAN PollMode;                               // vector masked after a storm: the timer polls the queues
    BOOLEAN PollAlways;                             // polling mode (PLAN "Polling mode"): no interrupt is used, set at HwFindAdapter
    UCHAR PollWhy;                                  // POLL_WHY_* of the last HwFindAdapter (polltun.h)
    BOOLEAN PollIdleLogged;                         // STG_POLL_IDLE given for the current idle period
    PCI_INT_INFO IntInfo;                           // Interrupt Pin/Line, MSI/MSI-X presence (pcicap.h), report only
    ULONG PortIrqLevel;                             // BusInterruptLevel and Vector as the port supplied them
    ULONG PortIrqVector;
    ULONG PollTicks;                                // timer callbacks while polling
    ULONG PollStartIoHits;                          // HwStartIo calls (spin included) that consumed a completion
    ULONG PollIsrCalls;                             // HwInterrupt calls while polling (the port, a shared line or the dump port)
    // Counter answer (docs/PLAN.md MD-8, include/stor98pf.h): totals since
    // PfEpoch began (HwFindAdapter that is not a resume), and the facts
    // only stor98 sees.
    ULONG PfEpoch;
    ULONG PfMaxTransfer;                            // as NegotiateDma left them
    ULONG PfBreaks;
    ULONG PfDcbSeen;                                // STOR98PF_DCB_SEEN (9x)
    ULONG PfDcbMaxXfer;
    ULONG PfDcbMaxSg;
    ULONG PfDcbDmd;
    STOR98PF_TOTALS Pf;
    STOR98PF_ANSWER PfAnswer;                       // built in HwStartIo for READ BUFFER, then copied
#ifdef STOR98_TEST_NOAUTOSENSE
    ULONG TestNoAsReads;                            // READ(10)s seen by the test build
#endif
    ENG_CLOCK Clock;
    ENG_TUNABLES Tun;
    ENG_BUSY Busy[ENG_BUSY_TRACK];
    ENG_PARENT Parents[ENG_MAX_PARENTS];
    ENG_CHILD Children[ENG_MAX_CHILDREN];
} HW_DEVICE_EXTENSION, *PHW_DEVICE_EXTENSION;

//
// Forward declarations of miniport entry points
//
ULONG DriverEntry(IN PVOID DriverObject, IN PVOID Argument2);
ULONG HwFindAdapter(IN PVOID DeviceExtension, IN PVOID HwContext,
                    IN PVOID BusInformation, IN PCHAR ArgumentString,
                    IN OUT PPORT_CONFIGURATION_INFORMATION ConfigInfo,
                    OUT PBOOLEAN Again);
BOOLEAN HwInitialize(IN PVOID DeviceExtension);
BOOLEAN HwStartIo(IN PVOID DeviceExtension, IN PSCSI_REQUEST_BLOCK Srb);
BOOLEAN HwInterrupt(IN PVOID DeviceExtension);
BOOLEAN HwResetBus(IN PVOID DeviceExtension, IN ULONG PathId);
#ifndef NVME2K_NT4
SCSI_ADAPTER_CONTROL_STATUS HwAdapterControl(IN PVOID DeviceExtension,
                                              IN SCSI_ADAPTER_CONTROL_TYPE ControlType,
                                              IN PVOID Parameters);
#else
#ifdef NVME2K_W9X
BOOLEAN HwAdapterState(
    IN PVOID DeviceExtension,
    IN PVOID Context,
    IN BOOLEAN SaveState);
#endif
#endif
//
// Helper functions
//
BOOLEAN IsNvmeDevice(UCHAR BaseClass, UCHAR SubClass, UCHAR ProgIf);
// PCI config space of the own function (HwFindAdapter only, PLAN 3.4):
// FALSE when the port returned fewer bytes than the field needs, or wrote
// fewer than requested.
BOOLEAN ReadPciConfigWord(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG Offset, OUT PUSHORT Value);
BOOLEAN ReadPciConfigDword(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG Offset, OUT PULONG Value);
BOOLEAN WritePciConfigWord(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG Offset, IN USHORT Value);
BOOLEAN WritePciConfigDword(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG Offset, IN ULONG Value);

//
// NVMe helper functions
//
ULONG NvmeReadReg32(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG Offset);
VOID NvmeWriteReg32(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG Offset, IN ULONG Value);
ULONGLONG NvmeReadReg64(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG Offset);
VOID NvmeWriteReg64(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG Offset, IN ULONGLONG Value);
BOOLEAN NvmeSubmitIoCommand(IN PHW_DEVICE_EXTENSION DevExt, IN PNVME_COMMAND Cmd);
BOOLEAN NvmeSubmitAdminCommand(IN PHW_DEVICE_EXTENSION DevExt, IN PNVME_COMMAND Cmd);
BOOLEAN NvmeProcessAdminCompletion(IN PHW_DEVICE_EXTENSION DevExt);
BOOLEAN NvmeProcessIoCompletion(IN PHW_DEVICE_EXTENSION DevExt);
VOID NvmeRingDoorbell(IN PHW_DEVICE_EXTENSION DevExt, IN USHORT QueueId, IN BOOLEAN IsSubmission, IN USHORT Value);
BOOLEAN NvmeCreateIoCQ(IN PHW_DEVICE_EXTENSION DevExt);
BOOLEAN NvmeCreateIoSQ(IN PHW_DEVICE_EXTENSION DevExt);
BOOLEAN NvmeIdentifyController(IN PHW_DEVICE_EXTENSION DevExt);
BOOLEAN NvmeIdentifyNamespace(IN PHW_DEVICE_EXTENSION DevExt);
VOID NvmeToAtaIdentify(IN PHW_DEVICE_EXTENSION DevExt, OUT PATA_IDENTIFY_DEVICE_STRUCT AtaIdentify);
BOOLEAN NvmeLogPageToScsiLogPage(IN PNVME_SMART_INFO NvmeSmart, IN UCHAR ScsiPageCode, OUT PVOID ScsiLogBuffer, IN ULONG BufferSize, OUT PULONG BytesWritten);
VOID NvmeCompleteLogPage(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb, IN USHORT Status, IN PVOID Data);

// SCSI helper functions
BOOLEAN ScsiParseSatCommand(IN PSCSI_REQUEST_BLOCK Srb, OUT PUCHAR AtaCommand, OUT PUCHAR AtaFeatures, OUT PUCHAR AtaCylLow, OUT PUCHAR AtaCylHigh);
UCHAR ScsiGetLogPageCodeFromSrb(IN PSCSI_REQUEST_BLOCK Srb);

// helpers for completing SRBs answered by the driver (no NVMe command):
// they complete the SRB; NextRequest is the engine's (EngStartIo).
BOOLEAN ScsiSuccess(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb);
BOOLEAN ScsiError(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb, IN UCHAR SrbStatus);
BOOLEAN ScsiIllegalRequest(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb, IN UCHAR Asc);
VOID ScsiSetSense(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb, IN UCHAR Key, IN UCHAR Asc,
                  IN UCHAR Ascq);
BOOLEAN ScsiHandleRequestSense(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb);
VOID ScsiSenseNewCommand(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb);
VOID ScsiSenseReset(IN PHW_DEVICE_EXTENSION DevExt);
VOID ScsiCompletionStatus(IN PSCSI_REQUEST_BLOCK Srb);

// Dispatch of an SRB to its handler (running state; also replays held SRBs)
BOOLEAN ScsiDispatch(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb);
BOOLEAN ScsiCompletesInSoftware(IN PSCSI_REQUEST_BLOCK Srb);
BOOLEAN NvmeVwcFeature(IN PHW_DEVICE_EXTENSION DevExt, IN BOOLEAN Set);

//
// SMART/IOCTL functions
//
BOOLEAN HandleIO_NVME2KDB(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb);
BOOLEAN HandleIO_SCSIDISK(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb);
BOOLEAN ScsiHandleInquiry(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb);
BOOLEAN ScsiHandleReadBuffer(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb);
BOOLEAN ScsiHandleReadCapacity(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb);
BOOLEAN ScsiHandleReadCapacity64(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb);
BOOLEAN ScsiHandleLogSense(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb);
BOOLEAN ScsiHandleSatPassthrough(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb);
BOOLEAN ScsiHandleModeSense(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb);
BOOLEAN ScsiHandleReadDefectData10(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb);

//
// Uncached memory allocator (physically aligned, PLAN 0.5)
//
BOOLEAN AllocateUncachedMemory(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG Size, IN ULONG Alignment,
                               OUT PVOID* VirtualAddress, OUT PHYSICAL_ADDRESS* PhysicalAddress);

#endif // _NVME2K_H_
