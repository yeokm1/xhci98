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
// Interfaces whose implementation waits on qualification items or open
// maintainer decisions (docs/PLAN.md, "Qualification before dependent
// product code"). The engine calls only these; each implementation here is
// the minimal one the decided contracts allow, and is marked STUB where the
// real one is still to come.
//
//   DMA memory provider    NT: MD-3 (a), decided: the port's uncached
//                          extension. 9x: MD-4, decided: a VMM _PageAllocate
//                          pool outside the port, on the interface 3 boot
//                          block (pool9x.c, PlatDmaPoolProvider, built with
//                          STOR98_DMA_POOL on the 9x target; integrated on
//                          int-0.2 as a trial ahead of D4 adoption,
//                          docs/INTEGRATION-0.2.md). A 9x build without
//                          STOR98_DMA_POOL uses the uncached extension, with
//                          the known gap below. Every block the engine carves (queues,
//                          identify page, PRP list pages, bounce pages) goes
//                          through PlatDmaAlloc, which keeps an allocation
//                          record (virtual and physical address, size,
//                          provider, owner state), and every page the engine
//                          hands the controller is translated by
//                          PlatDmaPagePa, so the provider is swapped behind
//                          the PLAT_DMA_PROVIDER hook below without touching
//                          the engine.
//   Memory release         Only on isolation evidence (PLAN 0.10). Without
//                          it the records are quarantined: kept owned and
//                          out of every pool. With MD-3 (a) the port owns
//                          the memory and frees it itself at removal (the
//                          accepted MD-3 window on NT).
//                          KNOWN GAP on 9x without STOR98_DMA_POOL
//                          (PLAN 0.5, MD-4): the 9x port frees the uncached
//                          extension when the .mpd is unloaded at removal,
//                          so memory quarantined after the MD-1 gone
//                          deadline (no isolation evidence) is owned only
//                          until that removal. The MD-1 path still keeps it
//                          owned and out of use until then; each quarantine
//                          of port-provided memory is counted and logged
//                          (STG_DMA_GAP), and PlatDmaQuarantineHolds says
//                          whether the provider can hold a quarantine past
//                          removal.
//   CM link state          MD-6 (include/stor98cm.h): poll the CM-owned
//                          link-state table. NT: interface 2, opened in
//                          HwFindAdapter through \Callback\Stor98LinkState
//                          (cmlinknt.c, cmlink.h). 9x: interface 3, found
//                          through the registry locator and the current-boot
//                          proof (pool9x.c). Without a channel the poll
//                          reports "no information", so evidence 2 never
//                          arrives and the MD-1 gone deadline applies.
//   CM removal handshake   MD-5, decided: the REMOVE_PENDING request in the
//                          9x table, as the removal transaction below
//                          (pool9x.c; PlatCmRemove*, stubs elsewhere), and
//                          write-through on Me (PlatWriteThroughSetting).
//

#ifndef _STOR98_PLATFORM_H_
#define _STOR98_PLATFORM_H_

struct _HW_DEVICE_EXTENSION;

// CM link-state poll results (evidence 2 of PLAN 0.10)
#define PLAT_LINK_NO_INFO       0   // no table, or nothing for our generation
#define PLAT_LINK_UP            1
#define PLAT_LINK_DOWN_QUIESCED 2   // CM: path closed and endpoint reset completed

// DMA allocation records
#define PLAT_DMA_PORT           1   // the port's uncached extension (NT MD-3 (a); 9x without STOR98_DMA_POOL)
#define PLAT_DMA_POOL           2   // driver-owned pool outside the port (9x MD-4, pool9x.c)

//
// Ownership states of an allocation record (review round 3, N2). Names and
// values follow the MD-4 pool records of interface 3 (the Q-9x boot block:
// FREE / RESERVED / OWNED / ISOLATED / RELEASED / QUARANTINED), so the pool
// provider can mirror every Note into its record unchanged at D4. Two
// groups, kept strictly apart:
//
//   Held by this load (the engine may still use and re-enable the block):
//     RESERVED     carved, never yet given to the controller by this load
//     OWNED        the controller may reach it (enabled; also while the
//                  engine quarantines pages without isolation evidence: the
//                  record stays OWNED and the pages stay out of the
//                  engine's own pools)
//     ISOLATED     isolation evidence (acknowledged disable, CM link-down):
//                  the controller cannot reach it, but this load still
//                  holds it and may re-enable it (ISOLATED -> OWNED in
//                  LcEnable, before any queue base or CC.EN is written)
//   Relinquished (terminal for this load; only PlatDmaRelinquish, once the
//   engine will never touch the block again: at the end of EngStop, or on
//   9x when a removal transaction's RDY=0 is acknowledged, pool9x.c):
//     RELEASED     given up after isolation (or never given to the
//                  controller): the provider may reuse or free it
//     QUARANTINED  given up without isolation evidence: the controller may
//                  still reach it; never reused or freed (MD-1)
//
#define PLAT_OWN_FREE           0   // record unused
#define PLAT_OWN_RESERVED       1   // carved by this load, not yet reachable by the controller
#define PLAT_OWN_OWNED          2   // the controller may reach it
#define PLAT_OWN_ISOLATED       3   // isolation evidence; still held by this load
#define PLAT_OWN_RELEASED       4   // relinquished after isolation: provider may reuse or free
#define PLAT_OWN_QUARANTINED    5   // relinquished without evidence: never reused or freed

#define PLAT_DMA_BLOCKS         8

typedef struct _PLAT_DMA_BLOCK {
    PVOID Va;
    PHYSICAL_ADDRESS Pa;        // physical address of the first byte (each page translated on use)
    ULONG Size;
    UCHAR Provider;             // PLAT_DMA_*
    UCHAR Owner;                // PLAT_OWN_*
    UCHAR Use;                  // PLAT_USE_*
    UCHAR Reserved;
} PLAT_DMA_BLOCK, *PPLAT_DMA_BLOCK;

#define PLAT_USE_ADMIN_SQ       1
#define PLAT_USE_ADMIN_CQ       2
#define PLAT_USE_IO_SQ          3
#define PLAT_USE_IO_CQ          4
#define PLAT_USE_UTILITY        5
#define PLAT_USE_PRP            6
#define PLAT_USE_BOUNCE         7

//
// DMA memory provider hook (PLAN 0.5, MD-3, MD-4). The engine never asks a
// provider directly: it calls PlatDmaAcquire once per HwFindAdapter,
// PlatDmaAlloc for each block it carves (RESERVED), PlatDmaPagePa for each
// page it gives the controller, PlatDmaReachable before it enables the
// controller on the blocks (OWNED), PlatDmaSettle when the recovery
// contract settles its memory (isolation evidence: ISOLATED; none: the
// records stay OWNED and the engine keeps the pages out of use) and
// PlatDmaRelinquish once, when it stops using the blocks for good: at the
// end of EngStop, or (9x) when a removal transaction's RDY=0 acknowledgement
// isolates the controller, after the queue gates close (pool9x.c; accepted
// in the int-0.2 review) (RELEASED or QUARANTINED). Those forward to the provider
// chosen at acquisition. A provider supplies:
//
//   Acquire    HwFindAdapter, after the DMA negotiation (0.8) and before any
//              CC write: obtain one region of at least Size bytes, uncached,
//              below the negotiated DMA limit, and set the region fields of
//              the device extension (base VA, the PA of its first byte, size,
//              carve offset 0). FALSE fails the find before CC.EN (bring-up
//              rule of 0.10). It must not write region memory the controller
//              may still reach from an earlier load (a pool reused across
//              .mpd loads): the engine zeroes each queue in LcEnable, after
//              the load's own acknowledged disable (Q-9x finding).
//   PagePa     the physical address of one page of the region (each page
//              translated on its own, PLAN 0.4); FALSE if it cannot.
//   Note       an allocation record changed owner state (PLAT_OWN_*), so a
//              provider that keeps records outside the device extension
//              (the MD-4 pool's records, interface 3) can follow. Only
//              RELEASED lets the provider reuse or free the block, and the
//              engine sends it only from PlatDmaRelinquish, after which it
//              never touches the block; ISOLATED keeps the block held by
//              this load (it may become OWNED again); QUARANTINED keeps it
//              owned past this adapter instance.
//   HoldsPastRemoval  TRUE only if a quarantined region stays owned after
//              the adapter's removal and the image's unload (the MD-4 pool);
//              FALSE for the port's uncached extension, which every
//              measured port frees at removal (G5, G9).
//
// Providers: PlatDmaPortProvider (platform.c, the port's uncached
// extension; NT under MD-3 (a), and a 9x build without STOR98_DMA_POOL)
// and, when built with STOR98_DMA_POOL (the 9x build since int-0.2),
// PlatDmaPoolProvider (pool9x.c), which PlatDmaAcquire then uses on 9x with
// no fallback: a pool that cannot be had fails the find before CC.EN.
//
typedef struct _PLAT_DMA_PROVIDER {
    UCHAR Id;                   // PLAT_DMA_*
    BOOLEAN HoldsPastRemoval;
    BOOLEAN (*Acquire)(IN struct _HW_DEVICE_EXTENSION *DevExt, IN PPORT_CONFIGURATION_INFORMATION ConfigInfo,
                       IN ULONG Size);
    BOOLEAN (*PagePa)(IN struct _HW_DEVICE_EXTENSION *DevExt, IN PVOID Va, OUT PHYSICAL_ADDRESS *Pa);
    VOID (*Note)(IN struct _HW_DEVICE_EXTENSION *DevExt, IN PPLAT_DMA_BLOCK Block, IN UCHAR Owner);
} PLAT_DMA_PROVIDER;

extern const PLAT_DMA_PROVIDER PlatDmaPortProvider;
#ifdef STOR98_DMA_POOL
extern const PLAT_DMA_PROVIDER PlatDmaPoolProvider;
#endif

ULONG PlatDmaPoolSize(VOID);
BOOLEAN PlatDmaAcquire(IN struct _HW_DEVICE_EXTENSION *DevExt,
                       IN PPORT_CONFIGURATION_INFORMATION ConfigInfo);
BOOLEAN PlatDmaAlloc(IN struct _HW_DEVICE_EXTENSION *DevExt, IN ULONG Size, IN UCHAR Use,
                     OUT PVOID *Va, OUT PHYSICAL_ADDRESS *Pa);
BOOLEAN PlatDmaPagePa(IN struct _HW_DEVICE_EXTENSION *DevExt, IN PVOID Va, OUT PHYSICAL_ADDRESS *Pa);
BOOLEAN PlatDmaQuarantineHolds(IN struct _HW_DEVICE_EXTENSION *DevExt);
VOID PlatDmaReachable(IN struct _HW_DEVICE_EXTENSION *DevExt);
VOID PlatDmaSettle(IN struct _HW_DEVICE_EXTENSION *DevExt, IN BOOLEAN Evidence);
VOID PlatDmaRelinquish(IN struct _HW_DEVICE_EXTENSION *DevExt);

//
// Write-through (PLAN D3). PlatWriteThroughSetting resolves the write-through
// setting (9x `StorWriteThrough`; NT `writethrough=`) once per HwFindAdapter
// into DevExt->WtMode (PLAT_WT_*): `auto` (the default) is on only on
// Windows Me (VMM 4.90), where removals that bypass the CM get no stop
// callback; elsewhere off.
//
#define PLAT_WT_OFF             0   // volatile write cache left as it is
#define PLAT_WT_CACHEOFF        1   // Set Features VWC off, verified by Get Features; FUA if refused
#define PLAT_WT_FUA             2   // FUA on every write
#define PLAT_WT_AUTO            3   // configured value only: resolved to CACHEOFF on Me, OFF elsewhere

VOID PlatWriteThroughSetting(IN struct _HW_DEVICE_EXTENSION *DevExt, IN PCHAR ArgumentString);

//
// Polling mode (PLAN "Polling mode", polltun.h).
//   PlatPollRaw     9x only: the StorPollMode, StorPollBusyUs, StorPollIdleMs
//                   and StorPollSpinUs values (REG_SZ decimal or REG_DWORD) of
//                   HKLM\System\CurrentControlSet\Services\stor98, read
//                   through the documented VMM registry services at
//                   HwFindAdapter (NT parses DriverParameter instead).
//   PlatIrqReport   the user-readable interrupt report: a REG_SZ value
//                   "StorInterrupt <bus>.<device>.<function>" in
//                   HKLM\System\CurrentControlSet\Services\stor98 on both
//                   families, written at every start from HwFindAdapter:
//                   Interrupt Pin and Line, MSI and MSI-X presence, the
//                   interrupt the port reported, and "interrupt" or
//                   "polling (<reason>)". 9x: VMM _RegCreateKey and
//                   _RegSetValueEx. NT: ZwOpenKey, ZwSetValueKey and ZwClose
//                   at PASSIVE_LEVEL, a step outside the strict SCSIport
//                   contract like MD-6's imports; never in a dump or
//                   hibernation instance and never at a resume (whose
//                   HwFindAdapter IRQL is not measured), and a failure only
//                   leaves the value unwritten.
//
#ifdef NVME2K_W9X
VOID PlatPollRaw(OUT POLL_RAW *Raw);
// 9x only: the StorMaxTransferKB value (REG_SZ decimal or REG_DWORD) of the
// same key (xfertun.h). Present: found; Valid: a number (*RawKb).
VOID PlatXferRaw(OUT BOOLEAN *Present, OUT BOOLEAN *Valid, OUT ULONG *RawKb);
#endif
VOID PlatIrqReport(IN struct _HW_DEVICE_EXTENSION *DevExt, IN PCHAR ArgumentString);
// TRUE in a crash dump or hibernation instance (NT "dump=1"; 9x never).
BOOLEAN PlatDumpMode(IN PCHAR ArgumentString);

VOID PlatCmLinkDriverEntry(IN PVOID DriverObject, IN PVOID Argument2);   // NT: notes a dump image
VOID PlatCmLinkOpen(IN struct _HW_DEVICE_EXTENSION *DevExt, IN PCHAR ArgumentString);
ULONG PlatCmLinkPoll(IN struct _HW_DEVICE_EXTENSION *DevExt);
VOID PlatCmLinkAcknowledge(IN struct _HW_DEVICE_EXTENSION *DevExt);
ULONG PlatCmLinkPollUs(IN struct _HW_DEVICE_EXTENSION *DevExt);   // idle poll period wanted, 0 none

//
// The 9x CM removal transaction (interface 3 R9-R11, MD-5; pool9x.c). Stubs
// elsewhere (never active).
//   PlatCmRemoveTimer    from EngTimer before the engine's step: 0 lets the
//                        engine run; otherwise the transaction owns the
//                        adapter this tick and the value is the timer period
//                        wanted (PLAT_RM_NO_REARM: none).
//   PlatCmRemoveStartIo  from EngStartIo first: TRUE when the SRB was
//                        completed here (an accepted request: BUSY until the
//                        acknowledgement, then NO_DEVICE; SHUTDOWN succeeds).
//   PlatCmRemoveActive   TRUE once a request is accepted: the instance is
//                        terminal (R9d), HwResetBus does nothing.
//
#define PLAT_RM_NO_REARM        0xFFFFFFFF
ULONG PlatCmRemoveTimer(IN struct _HW_DEVICE_EXTENSION *DevExt);
BOOLEAN PlatCmRemoveStartIo(IN struct _HW_DEVICE_EXTENSION *DevExt, IN PSCSI_REQUEST_BLOCK Srb);
BOOLEAN PlatCmRemoveActive(IN struct _HW_DEVICE_EXTENSION *DevExt);
//   PlatCmRemoveLinkDown a DOWN_QUIESCED statement while a transaction owns
//                        the adapter (K1): TRUE when the transaction applied
//                        it (cleanup and RemoveAck done); the caller then
//                        acknowledges the statement. FALSE: the engine's own
//                        rules apply (no transaction, or handed over).
BOOLEAN PlatCmRemoveLinkDown(IN struct _HW_DEVICE_EXTENSION *DevExt);
//   PlatCmRemoveGate     what the engine may still do on its own while a
//                        transaction runs: PLAT_RM_GATE_DISPATCH set = no
//                        dispatch (only the drain may finish admitted work);
//                        PLAT_RM_GATE_ISR set = the ISR consumes nothing.
#define PLAT_RM_GATE_DISPATCH   0x01
#define PLAT_RM_GATE_ISR        0x02
ULONG PlatCmRemoveGate(IN struct _HW_DEVICE_EXTENSION *DevExt);

#endif // _STOR98_PLATFORM_H_
