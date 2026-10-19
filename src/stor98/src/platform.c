// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright (c) 2025, Dominik Behr (nvme2k)
// Windows 9x/NT4 port and changes (nvme9x/nvment): Copyright (c) SweetLow
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
// Platform interfaces; see platform.h. The uncached-extension allocation and
// the Windows 9x cache-mode workaround come from the inherited
// HwFoundAdapter (nvme2k, nvme9x).
//

#include "nvme2k.h"
#include "utils.h"
#include "dpkeys.h"

ULONG PlatDmaPoolSize(VOID)
{
    return UNCACHED_EXTENSION_SIZE;
}

//
// The port's uncached extension (MD-3 (a) on NT). Called after the DMA
// negotiation (PLAN 0.8). The block is not assumed physically contiguous:
// each page is translated on its own (PlatDmaPagePa, PLAN 0.4); here only
// the base is translated, for the alignment. Fresh memory from the port on
// every find, so it is zeroed here.
//
static BOOLEAN PlatPortAcquire(IN PHW_DEVICE_EXTENSION DevExt, IN PPORT_CONFIGURATION_INFORMATION ConfigInfo,
                               IN ULONG Size)
{
    ULONG len;

    DevExt->UncachedExtensionSize = Size;
    DevExt->UncachedExtensionBase = ScsiPortGetUncachedExtension((PVOID)DevExt, ConfigInfo,
                                                                 DevExt->UncachedExtensionSize);
    if (DevExt->UncachedExtensionBase == NULL) {
        DBGSTAGE(STG_DMA_ACQUIRE, 0xFFFFFFFF);
        return FALSE;
    }

#ifdef NVME2K_W9X
    // SweetLow: Windows 9x Memory Manager always returns WB enabled area on
    // my system (Windows 98 SE with updates); the uncached extension lies in
    // MTRR WB (usual RAM), so make its pages uncached.
    if (_PageModifyPermissions(
            ((ULONG)DevExt->UncachedExtensionBase) >> PAGE_SHIFT,
            UNCACHED_EXTENSION_PAGES,
            0xFFFFFFFF,
            PC_CACHEWT | PC_CACHEDIS) == 0xFFFFFFFF) {
        if (ApplyCacheMode((PUCHAR)DevExt->UncachedExtensionBase, UNCACHED_EXTENSION_PAGES, PTE_CACHE_UC) !=
            UNCACHED_EXTENSION_PAGES) {
            DBGSTAGE(STG_DMA_ACQUIRE, 0xFFFFFFFE);
            return FALSE;
        }
    }
#endif

    len = DevExt->UncachedExtensionSize;
    DevExt->UncachedExtensionPhys = ScsiPortGetPhysicalAddress((PVOID)DevExt, NULL,
                                                               DevExt->UncachedExtensionBase, &len);
    DBGSTAGE(STG_DMA_ACQUIRE, (len >> 12) | ((DevExt->UncachedExtensionPhys.LowPart & 0xFFF) << 16));
    if (len == 0 || DevExt->UncachedExtensionPhys.QuadPart == 0) {
        // No translation: the bring-up rule allows failure here (nothing
        // written to CC).
        return FALSE;
    }
    RtlZeroMemory(DevExt->UncachedExtensionBase, DevExt->UncachedExtensionSize);
    return TRUE;
}

// One page of the uncached extension: the port's translation (the only
// route for memory the port allocated, PLAN 0.4).
static BOOLEAN PlatPortPagePa(IN PHW_DEVICE_EXTENSION DevExt, IN PVOID Va, OUT PHYSICAL_ADDRESS *Pa)
{
    ULONG len = PAGE_SIZE - ((ULONG)(ULONG_PTR)Va & (PAGE_SIZE - 1));

    *Pa = ScsiPortGetPhysicalAddress((PVOID)DevExt, NULL, Va, &len);
    return (BOOLEAN)(len != 0 && Pa->QuadPart != 0);
}

// The port owns the memory: no records outside the device extension.
static VOID PlatPortNote(IN PHW_DEVICE_EXTENSION DevExt, IN PPLAT_DMA_BLOCK Block, IN UCHAR Owner)
{
    UNREFERENCED_PARAMETER(DevExt);
    UNREFERENCED_PARAMETER(Block);
    UNREFERENCED_PARAMETER(Owner);
}

const PLAT_DMA_PROVIDER PlatDmaPortProvider = {
    PLAT_DMA_PORT,
    FALSE,                      // the port frees it at removal (G5, G9)
    PlatPortAcquire,
    PlatPortPagePa,
    PlatPortNote
};

static const PLAT_DMA_PROVIDER *PlatProvider(IN PHW_DEVICE_EXTENSION DevExt)
{
#ifdef STOR98_DMA_POOL
    if (DevExt->DmaProvider == PLAT_DMA_POOL) {
        return &PlatDmaPoolProvider;
    }
#else
    UNREFERENCED_PARAMETER(DevExt);
#endif
    return &PlatDmaPortProvider;
}

//
// The DMA region for this adapter instance (platform.h, provider hook).
// 9x with the MD-4 pool built in (STOR98_DMA_POOL, pool9x.c) takes the pool
// only; everything else takes the port's uncached extension, with the G5
// risk a 9x build without the pool has (the port frees it at removal;
// STG_DMA_GAP).
//
BOOLEAN PlatDmaAcquire(IN PHW_DEVICE_EXTENSION DevExt, IN PPORT_CONFIGURATION_INFORMATION ConfigInfo)
{
    ULONG i;
    BOOLEAN ok = FALSE;

    // New allocations, new records (a resume re-runs HwFindAdapter).
    for (i = 0; i < PLAT_DMA_BLOCKS; i++) {
        DevExt->DmaBlocks[i].Owner = PLAT_OWN_FREE;
    }
    DevExt->UncachedExtensionOffset = 0;
    DevExt->QueuesAllocated = FALSE;
#if defined(STOR98_DMA_POOL) && defined(NVME2K_W9X)
    DevExt->DmaProvider = PLAT_DMA_POOL;
    ok = PlatDmaPoolProvider.Acquire(DevExt, ConfigInfo, PlatDmaPoolSize());
    // A pool that cannot be had (cap reached, allocation failed) fails the
    // find before CC.EN; the uncached extension is not a silent fallback
    // (MD-3 (b) / MD-4 contract).
    return ok;
#else
    DevExt->DmaProvider = PLAT_DMA_PORT;
    ok = PlatDmaPortProvider.Acquire(DevExt, ConfigInfo, PlatDmaPoolSize());
    return ok;
#endif
}

BOOLEAN PlatDmaPagePa(IN PHW_DEVICE_EXTENSION DevExt, IN PVOID Va, OUT PHYSICAL_ADDRESS *Pa)
{
    return PlatProvider(DevExt)->PagePa(DevExt, Va, Pa);
}

//
// One block of device-visible memory carved from the region, aligned against
// its physical address (PLAN 0.5), with its allocation record. Called once
// per HwFindAdapter by the carve (lifecycle.c, LcCarve).
//
BOOLEAN PlatDmaAlloc(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG Size, IN UCHAR Use,
                     OUT PVOID *Va, OUT PHYSICAL_ADDRESS *Pa)
{
    ULONG i;
    PPLAT_DMA_BLOCK b = NULL;

    for (i = 0; i < PLAT_DMA_BLOCKS; i++) {
        if (DevExt->DmaBlocks[i].Owner == PLAT_OWN_FREE) {
            b = &DevExt->DmaBlocks[i];
            break;
        }
    }
    if (b == NULL || !AllocateUncachedMemory(DevExt, Size, PAGE_SIZE, Va, Pa)) {
        return FALSE;
    }
    b->Va = *Va;
    b->Pa = *Pa;
    b->Size = Size;
    b->Provider = DevExt->DmaProvider;
    b->Owner = PLAT_OWN_RESERVED;
    b->Use = Use;
    PlatProvider(DevExt)->Note(DevExt, b, PLAT_OWN_RESERVED);
    return TRUE;
}

static VOID PlatDmaMove(IN PHW_DEVICE_EXTENSION DevExt, IN PPLAT_DMA_BLOCK b, IN UCHAR To)
{
    if (b->Owner != To) {
        b->Owner = To;
        PlatProvider(DevExt)->Note(DevExt, b, To);
    }
}

// TRUE if quarantined memory stays owned past the removal of the adapter:
// only memory outside the port (MD-3 (b), MD-4). The port's uncached
// extension is freed by the port at removal on every measured port (G5).
BOOLEAN PlatDmaQuarantineHolds(IN PHW_DEVICE_EXTENSION DevExt)
{
    return PlatProvider(DevExt)->HoldsPastRemoval;
}

//
// The controller is about to be enabled on the blocks (LcEnable, before the
// queue bases and CC.EN are written): RESERVED or ISOLATED -> OWNED.
//
VOID PlatDmaReachable(IN PHW_DEVICE_EXTENSION DevExt)
{
    ULONG i;
    PPLAT_DMA_BLOCK b;

    for (i = 0; i < PLAT_DMA_BLOCKS; i++) {
        b = &DevExt->DmaBlocks[i];
        if (b->Owner == PLAT_OWN_RESERVED || b->Owner == PLAT_OWN_ISOLATED) {
            PlatDmaMove(DevExt, b, PLAT_OWN_OWNED);
        }
    }
}

//
// The recovery contract settled the engine's memory (EngChildrenSettle,
// EngQuarantineParentChildren). Isolation evidence (Evidence TRUE): OWNED ->
// ISOLATED; the blocks stay held by this load, which may re-enable the
// controller on them (PlatDmaReachable). No evidence: the records stay
// OWNED and the engine keeps the quarantined pages out of its own pools;
// nothing is relinquished, freed or handed back here (MD-1). With
// port-provided memory the port frees it at removal anyway: on NT that is
// the accepted MD-3 window, on 9x the known gap of a build without the MD-4
// pool (STOR98_DMA_POOL; platform.h), counted and logged here.
//
VOID PlatDmaSettle(IN PHW_DEVICE_EXTENSION DevExt, IN BOOLEAN Evidence)
{
    ULONG i, n = 0;
    PPLAT_DMA_BLOCK b;

    for (i = 0; i < PLAT_DMA_BLOCKS; i++) {
        b = &DevExt->DmaBlocks[i];
        if (b->Owner != PLAT_OWN_OWNED) {
            continue;
        }
        if (Evidence) {
            PlatDmaMove(DevExt, b, PLAT_OWN_ISOLATED);
        } else {
            n++;
        }
    }
    if (!Evidence && n != 0 && !PlatDmaQuarantineHolds(DevExt)) {
#ifdef NVME2K_W9X
        // Known gap of a 9x build without the MD-4 pool (the port's uncached
        // extension): owned until the .mpd unload at removal, not beyond it.
        if (DevExt->DmaGapEvents++ == 0) {
            DBGMARK(STG_DMA_GAP, n | ((ULONG)DevExt->DmaProvider << 8) | ((ULONG)DevExt->State << 16));
        }
#else
        DevExt->DmaGapEvents++;
#endif
    }
}

//
// The engine stops using its blocks for good (end of EngStop, or on 9x a
// removal transaction's acknowledged disable, pool9x.c; the next use starts
// with a new HwFindAdapter and PlatDmaAcquire, since ScsiRestartAdapter is
// not advertised). A second call (EngStop after the transaction) finds
// nothing held and changes nothing. The queue gate closes first
// (QueuesAllocated, AdminCqLive: no completion processing touches the blocks
// afterwards, not even an ISR call on a shared line), then each record is
// relinquished: held without controller access (RESERVED, ISOLATED) ->
// RELEASED, which lets the provider reuse or free it; OWNED (no isolation
// evidence) -> QUARANTINED.
//
VOID PlatDmaRelinquish(IN PHW_DEVICE_EXTENSION DevExt)
{
    ULONG i;
    PPLAT_DMA_BLOCK b;

    DevExt->QueuesAllocated = FALSE;
    DevExt->AdminCqLive = FALSE;
    for (i = 0; i < PLAT_DMA_BLOCKS; i++) {
        b = &DevExt->DmaBlocks[i];
        switch (b->Owner) {
        case PLAT_OWN_RESERVED:
        case PLAT_OWN_ISOLATED:
            PlatDmaMove(DevExt, b, PLAT_OWN_RELEASED);
            break;
        case PLAT_OWN_OWNED:
            PlatDmaMove(DevExt, b, PLAT_OWN_QUARANTINED);
            break;
        default:
            break;
        }
    }
    // The end of the relinquish (Block NULL, PLAT_OWN_FREE): a provider whose
    // adapter-level record no block transition reached (no block carved)
    // finishes it now (pool9x.c).
    PlatProvider(DevExt)->Note(DevExt, NULL, PLAT_OWN_FREE);
}

//
// Write-through setting (PLAN D3). The value names: auto | off | cacheoff | fua.
//
static UCHAR PlatWtParse(IN PCHAR s, IN UCHAR Default)
{
    static const char *names[4] = { "off", "cacheoff", "fua", "auto" };
    ULONG i, k;

    if (s == NULL) {
        return Default;
    }
    for (i = 0; i < 4; i++) {
        for (k = 0; names[i][k] != 0; k++) {
            UCHAR c = (UCHAR)s[k];

            if (c >= 'A' && c <= 'Z') {
                c = (UCHAR)(c + ('a' - 'A'));
            }
            if (c != (UCHAR)names[i][k]) {
                break;
            }
        }
        if (names[i][k] == 0) {
            UCHAR end = (UCHAR)s[k];

            if (end == 0 || end == ' ' || end == ';' || end == ',') {
                return (UCHAR)i;
            }
        }
    }
    return Default;
}

#ifdef NVME2K_W9X
//
// VMM services (Windows 98 DDK VMM.H), through the INT 20h dynalink the .mpd
// already uses (utils.c): Get_VMM_Version (0x0000: AH major, AL minor),
// _RegOpenKey (0x0148), _RegCloseKey (0x0149), _RegQueryValueEx (0x0151).
// No VxD and no undocumented interface.
//
#define PLAT_VMM_JMP(ord)     __asm int 0x20     __asm _emit ((ord) >> 0) & 0xFF     __asm _emit (((ord) >> 8) & 0xFF) | 0x80     __asm _emit (VMM_DEVICE_ID >> 0) & 0xFF     __asm _emit (VMM_DEVICE_ID >> 8) & 0xFF

static ULONG __declspec(naked) _cdecl PlatGetVmmVersion(VOID)
{
    PLAT_VMM_JMP(0x0000)
}

static ULONG __declspec(naked) _cdecl PlatRegOpenKey(ULONG hkey, PCHAR SubKey, PULONG phk)
{
    PLAT_VMM_JMP(0x0148)
}

static ULONG __declspec(naked) _cdecl PlatRegCloseKey(ULONG hkey)
{
    PLAT_VMM_JMP(0x0149)
}

static ULONG __declspec(naked) _cdecl PlatRegQueryValueEx(ULONG hkey, PCHAR Name, PULONG Reserved, PULONG Type,
                                             PVOID Data, PULONG cbData)
{
    PLAT_VMM_JMP(0x0151)
}

// _RegCreateKey (0x014A) and _RegSetValueEx (0x0152): the same documented
// VMM registry service table (Windows 98 DDK VMM.H), for the interrupt
// report (PLAN "Polling mode").
static ULONG __declspec(naked) _cdecl PlatRegCreateKey(ULONG hkey, PCHAR SubKey, PULONG phk)
{
    PLAT_VMM_JMP(0x014A)
}

static ULONG __declspec(naked) _cdecl PlatRegSetValueEx(ULONG hkey, PCHAR Name, ULONG Reserved, ULONG Type,
                                           PVOID Data, ULONG cbData)
{
    PLAT_VMM_JMP(0x0152)
}

#define PLAT_HKLM               0x80000002
#define PLAT_REG_SZ             1
#define PLAT_WT_KEY             "System\\CurrentControlSet\\Services\\stor98"
#define PLAT_ERROR_FILE_NOT_FOUND 2     // the VMM registry services return Win32 error codes
#endif

//
// 9x: Windows Me is VMM 4.90 (98 SE is 4.10). The value `StorWriteThrough`
// (REG_SZ) is read at HwFindAdapter from the key
// HKLM\System\CurrentControlSet\Services\stor98, the key the 9x CM locator already uses (D4), with the
// documented VMM registry services. No other name is read (owner decision
// 2026-10-16): 0.3.0.0 and 0.3.0.1 read `WriteThrough`, and a value of that
// name is ignored from 0.4.0.0 (README "Settings" tells users to rename it).
// NT: `writethrough=` in DriverParameter
// (the service's Parameters\Device key, the only per-adapter registry data a
// SCSIport miniport gets); kept in the device extension because a resume
// brings no ArgumentString. Unknown values fall back to `auto`.
//
VOID PlatWriteThroughSetting(IN PHW_DEVICE_EXTENSION DevExt, IN PCHAR ArgumentString)
{
    UCHAR cfg = PLAT_WT_AUTO;
    BOOLEAN me = FALSE;
#ifdef NVME2K_W9X
    ULONG hk = 0, type = 0, cb, rc;
    CHAR buf[16];
    ULONG ver = PlatGetVmmVersion() & 0xFFFF;

    UNREFERENCED_PARAMETER(ArgumentString);
    me = (BOOLEAN)(ver >= 0x045A);
    if (PlatRegOpenKey(PLAT_HKLM, PLAT_WT_KEY, &hk) == 0) {
        cb = sizeof(buf) - 1;
        rc = PlatRegQueryValueEx(hk, "StorWriteThrough", NULL, &type, buf, &cb);
        if (rc == 0 && type == PLAT_REG_SZ && cb < sizeof(buf)) {
            buf[cb] = 0;
            cfg = PlatWtParse(buf, PLAT_WT_AUTO);
        }
        PlatRegCloseKey(hk);
    }
    DevExt->WtConfig = cfg;
#else
    PCHAR p = NULL;
    ULONG i;

    if (ArgumentString != NULL) {
        for (i = 0; ArgumentString[i] != 0; i++) {
            static const char key[] = DPK_WRITETHROUGH;
            ULONG k;

            for (k = 0; key[k] != 0; k++) {
                UCHAR c = (UCHAR)ArgumentString[i + k];

                if (c >= 'A' && c <= 'Z') {
                    c = (UCHAR)(c + ('a' - 'A'));
                }
                if (c != (UCHAR)key[k]) {
                    break;
                }
            }
            if (key[k] == 0) {
                p = &ArgumentString[i + k];
                break;
            }
        }
        DevExt->WtConfig = PlatWtParse(p, PLAT_WT_AUTO);
    } else if (DevExt->WtConfig > PLAT_WT_AUTO) {
        DevExt->WtConfig = PLAT_WT_AUTO;
    }
    cfg = DevExt->WtConfig;
#endif
    if (cfg == PLAT_WT_AUTO) {
        cfg = me ? PLAT_WT_CACHEOFF : PLAT_WT_OFF;
    }
    DevExt->WtMode = cfg;
#ifdef NVME2K_W9X
    DBGSTAGE(STG_WRITE_THROUGH, 0x100 | DevExt->WtConfig | ((ULONG)cfg << 4) | ((ULONG)me << 12));
#else
    DBGSTAGE(STG_WRITE_THROUGH, 0x100 | DevExt->WtConfig | ((ULONG)cfg << 4) | ((ULONG)me << 12));
#endif
}

//
// Polling mode settings and the interrupt report (platform.h, PLAN
// "Polling mode").
//
#ifdef NVME2K_W9X
#define PLAT_REG_DWORD          4

static VOID PlatPollValue(IN ULONG hk, IN PCHAR Name, IN ULONG Bit, OUT ULONG *Value, IN OUT POLL_RAW *Raw)
{
    ULONG type = 0, cb;
    CHAR buf[16];

    cb = sizeof(buf) - 1;
    if (PlatRegQueryValueEx(hk, Name, NULL, &type, buf, &cb) != 0) {
        return;
    }
    if (type == PLAT_REG_DWORD && cb == sizeof(ULONG)) {
        *Value = *(ULONG UNALIGNED *)buf;
        Raw->Present |= Bit;
    } else if (type == PLAT_REG_SZ && cb < sizeof(buf)) {
        buf[cb] = 0;
        if (PollParseDec(buf, Value)) {
            Raw->Present |= Bit;
        }
    }
}

VOID PlatPollRaw(OUT POLL_RAW *Raw)
{
    ULONG hk = 0;

    if (PlatRegOpenKey(PLAT_HKLM, PLAT_WT_KEY, &hk) != 0) {
        return;
    }
    PlatPollValue(hk, "StorPollMode", POLL_CLAMP_MODE, &Raw->Mode, Raw);
    PlatPollValue(hk, "StorPollBusyUs", POLL_CLAMP_BUSY, &Raw->BusyUs, Raw);
    PlatPollValue(hk, "StorPollIdleMs", POLL_CLAMP_IDLE, &Raw->IdleMs, Raw);
    PlatPollValue(hk, "StorPollSpinUs", POLL_CLAMP_SPIN, &Raw->SpinUs, Raw);
    PlatRegCloseKey(hk);
}

VOID PlatXferRaw(OUT BOOLEAN *Present, OUT BOOLEAN *Valid, OUT ULONG *RawKb)
{
    ULONG hk = 0, type = 0, cb, rc;
    CHAR buf[16];

    *Present = FALSE;
    *Valid = FALSE;
    *RawKb = 0;
    if (PlatRegOpenKey(PLAT_HKLM, PLAT_WT_KEY, &hk) != 0) {
        return;
    }
    cb = sizeof(buf) - 1;
    rc = PlatRegQueryValueEx(hk, "StorMaxTransferKB", NULL, &type, buf, &cb);
    if (rc == 0) {
        *Present = TRUE;
        if (type == PLAT_REG_DWORD && cb == sizeof(ULONG)) {
            *RawKb = *(ULONG UNALIGNED *)buf;
            *Valid = TRUE;
        } else if (type == PLAT_REG_SZ && cb < sizeof(buf)) {
            buf[cb] = 0;
            *Valid = XferParseDec(buf, FALSE, RawKb);
        }
    } else if (rc != PLAT_ERROR_FILE_NOT_FOUND) {
        // Present but unreadable (a string too long for any valid value):
        // refused, not taken as absent.
        *Present = TRUE;
    }
    PlatRegCloseKey(hk);
}
#endif

// Small string helpers for the report (no C runtime in the driver).
static ULONG PlatCat(IN OUT PCHAR Buf, IN ULONG Pos, IN ULONG Size, IN const char *Str)
{
    while (*Str && Pos + 1 < Size) {
        Buf[Pos++] = *Str++;
    }
    Buf[Pos] = 0;
    return Pos;
}

static ULONG PlatCatDec(IN OUT PCHAR Buf, IN ULONG Pos, IN ULONG Size, IN ULONG Value)
{
    CHAR tmp[12];
    ULONG n = 0;

    do {
        tmp[n++] = (CHAR)('0' + Value % 10);
        Value /= 10;
    } while (Value != 0 && n < sizeof(tmp));
    while (n != 0 && Pos + 1 < Size) {
        Buf[Pos++] = tmp[--n];
    }
    Buf[Pos] = 0;
    return Pos;
}

static const char *PlatPollWhyText(IN ULONG Why)
{
    switch (Why) {
    case POLL_WHY_INTERRUPT:    return "interrupt";
#ifdef NVME2K_W9X
    case POLL_WHY_FORCED:       return "polling (forced by StorPollMode)";
#else
    case POLL_WHY_FORCED:       return "polling (forced by pollmode)";
#endif
    case POLL_WHY_NO_PIN:       return "polling (no interrupt pin)";
    case POLL_WHY_NO_PORT_IRQ:  return "polling (no interrupt assigned)";
    case POLL_WHY_NO_LINE:      return "polling (interrupt line not set)";
    default:                    return "polling (configuration unreadable)";
    }
}

// The value name ("StorInterrupt <bus>.<device>.<function>") and its text, e.g.
// "pin 1 line 11 msi no msix yes port 11/11 interrupt".
static VOID PlatIrqText(IN PHW_DEVICE_EXTENSION DevExt, OUT PCHAR Name, IN ULONG NameSize, OUT PCHAR Text,
                        IN ULONG TextSize)
{
    PCI_INT_INFO *ii = &DevExt->IntInfo;
    ULONG n;

    n = PlatCat(Name, 0, NameSize, "StorInterrupt ");
    n = PlatCatDec(Name, n, NameSize, DevExt->BusNumber);
    n = PlatCat(Name, n, NameSize, ".");
    n = PlatCatDec(Name, n, NameSize, DevExt->SlotNumber & 0x1F);
    n = PlatCat(Name, n, NameSize, ".");
    PlatCatDec(Name, n, NameSize, (DevExt->SlotNumber >> 5) & 0x7);

    n = PlatCat(Text, 0, TextSize, "pin ");
    n = PlatCatDec(Text, n, TextSize, ii->Pin);
    n = PlatCat(Text, n, TextSize, " line ");
    n = PlatCatDec(Text, n, TextSize, ii->Line);
    n = PlatCat(Text, n, TextSize, (ii->Flags & S98_PCI_MSI) ? " msi yes" : " msi no");
    n = PlatCat(Text, n, TextSize, (ii->Flags & S98_PCI_MSIX) ? " msix yes" : " msix no");
    if (ii->Flags & (S98_PCI_CAP_LOOP | S98_PCI_CAP_SHORT)) {
        n = PlatCat(Text, n, TextSize, " (capability list cut short)");
    }
    n = PlatCat(Text, n, TextSize, " port ");
    n = PlatCatDec(Text, n, TextSize, DevExt->PortIrqLevel);
    n = PlatCat(Text, n, TextSize, "/");
    n = PlatCatDec(Text, n, TextSize, DevExt->PortIrqVector);
    n = PlatCat(Text, n, TextSize, " ");
    PlatCat(Text, n, TextSize, PlatPollWhyText(DevExt->PollWhy));
}

#ifdef NVME2K_W9X
VOID PlatIrqReport(IN PHW_DEVICE_EXTENSION DevExt, IN PCHAR ArgumentString)
{
    CHAR name[32], text[128];
    ULONG hk = 0, rc, len = 0;

    UNREFERENCED_PARAMETER(ArgumentString);
    PlatIrqText(DevExt, name, sizeof(name), text, sizeof(text));
    rc = PlatRegCreateKey(PLAT_HKLM, PLAT_WT_KEY, &hk);
    if (rc == 0) {
        while (text[len] != 0) {
            len++;
        }
        rc = PlatRegSetValueEx(hk, name, 0, PLAT_REG_SZ, text, len + 1);
        PlatRegCloseKey(hk);
    }
    DBGSTAGE(STG_POLL_REPORT, rc & 0xFFFF);
}
#else
//
// NT: the ntoskrnl registry services, declared here because a miniport
// build has no ntddk.h (as cmlinknt.c does for MD-6's imports).
//
typedef struct _S98R_UNICODE_STRING {
    USHORT Length;
    USHORT MaximumLength;
    PWSTR Buffer;
} S98R_UNICODE_STRING;

typedef struct _S98R_OBJECT_ATTRIBUTES {
    ULONG Length;
    PVOID RootDirectory;
    S98R_UNICODE_STRING *ObjectName;
    ULONG Attributes;
    PVOID SecurityDescriptor;
    PVOID SecurityQualityOfService;
} S98R_OBJECT_ATTRIBUTES;

#define S98R_OBJ_CASE_INSENSITIVE   0x00000040
#define S98R_OBJ_KERNEL_HANDLE      0x00000200
#define S98R_KEY_SET_VALUE          0x0002
#define S98R_REG_SZ                 1

__declspec(dllimport) LONG __stdcall ZwOpenKey(PVOID *KeyHandle, ULONG DesiredAccess,
                                               S98R_OBJECT_ATTRIBUTES *ObjectAttributes);
__declspec(dllimport) LONG __stdcall ZwSetValueKey(PVOID KeyHandle, S98R_UNICODE_STRING *ValueName, ULONG TitleIndex,
                                                   ULONG Type, PVOID Data, ULONG DataSize);
__declspec(dllimport) LONG __stdcall ZwClose(PVOID Handle);

static const WCHAR PlatSvcKey[] = L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\stor98";

static VOID PlatWiden(IN const char *s, OUT PWSTR w, IN ULONG Max, OUT S98R_UNICODE_STRING *u)
{
    ULONG i;

    for (i = 0; s[i] != 0 && i + 1 < Max; i++) {
        w[i] = (WCHAR)(UCHAR)s[i];
    }
    w[i] = 0;
    u->Buffer = w;
    u->Length = (USHORT)(i * sizeof(WCHAR));
    u->MaximumLength = (USHORT)((i + 1) * sizeof(WCHAR));
}

static BOOLEAN PlatIsDump(IN PCHAR ArgumentString)
{
    static const char key[] = DPK_DUMP;
    ULONG i, k;

    if (ArgumentString == NULL) {
        return FALSE;
    }
    for (i = 0; ArgumentString[i] != 0; i++) {
        for (k = 0; key[k] != 0 && ArgumentString[i + k] == key[k]; k++) {
        }
        if (key[k] == 0) {
            return TRUE;
        }
    }
    return FALSE;
}

VOID PlatIrqReport(IN PHW_DEVICE_EXTENSION DevExt, IN PCHAR ArgumentString)
{
    CHAR name[32], text[128];
    WCHAR wname[32], wtext[128];
    S98R_UNICODE_STRING uKey, uName, uText;
    S98R_OBJECT_ATTRIBUTES oa;
    PVOID hk = NULL;
    LONG st;

    if (DevExt->Resumed) {
        DBGSTAGE(STG_POLL_REPORT, 1UL << 16);
        return;
    }
    if (PlatIsDump(ArgumentString)) {
        DBGSTAGE(STG_POLL_REPORT, 2UL << 16);
        return;
    }
    PlatIrqText(DevExt, name, sizeof(name), text, sizeof(text));
    PlatWiden(name, wname, sizeof(wname) / sizeof(wname[0]), &uName);
    PlatWiden(text, wtext, sizeof(wtext) / sizeof(wtext[0]), &uText);
    uKey.Buffer = (PWSTR)PlatSvcKey;
    uKey.Length = (USHORT)(sizeof(PlatSvcKey) - sizeof(WCHAR));
    uKey.MaximumLength = (USHORT)sizeof(PlatSvcKey);
    oa.Length = sizeof(oa);
    oa.RootDirectory = NULL;
    oa.ObjectName = &uKey;
    oa.Attributes = S98R_OBJ_CASE_INSENSITIVE | S98R_OBJ_KERNEL_HANDLE;
    oa.SecurityDescriptor = NULL;
    oa.SecurityQualityOfService = NULL;
    st = ZwOpenKey(&hk, S98R_KEY_SET_VALUE, &oa);
    if (st >= 0) {
        st = ZwSetValueKey(hk, &uName, 0, S98R_REG_SZ, wtext, uText.MaximumLength);
        ZwClose(hk);
    }
    DBGSTAGE(STG_POLL_REPORT, (ULONG)st & 0xFFFF);
}
#endif

//
// A crash dump or hibernation instance (NT: "dump=1" in the ArgumentString,
// present every time on 2000 and XP x64, G6; 9x has no miniport dump path).
//
BOOLEAN PlatDumpMode(IN PCHAR ArgumentString)
{
#ifdef NVME2K_W9X
    UNREFERENCED_PARAMETER(ArgumentString);
    return FALSE;
#else
    return PlatIsDump(ArgumentString);
#endif
}

//
// CM link-state table (G3, MD-6; include/stor98cm.h). NT: the interface 2
// receiver in cmlinknt.c (decisions in cmlink.h). 9x with the pool: the
// interface 3 receiver in pool9x.c, on the rendezvous the pool's E1 rule
// needs anyway. A 9x build without the pool has no channel: these stubs,
// so there is never evidence 2 and the MD-1 gone deadline applies.
//
#ifdef NVME2K_W9X
VOID PlatCmLinkDriverEntry(IN PVOID DriverObject, IN PVOID Argument2)
{
    UNREFERENCED_PARAMETER(DriverObject);
    UNREFERENCED_PARAMETER(Argument2);
}
#endif

#if defined(NVME2K_W9X) && !defined(STOR98_DMA_POOL)
VOID PlatCmLinkOpen(IN PHW_DEVICE_EXTENSION DevExt, IN PCHAR ArgumentString)
{
    UNREFERENCED_PARAMETER(DevExt);
    UNREFERENCED_PARAMETER(ArgumentString);
}

ULONG PlatCmLinkPoll(IN PHW_DEVICE_EXTENSION DevExt)
{
    UNREFERENCED_PARAMETER(DevExt);
    return PLAT_LINK_NO_INFO;
}

VOID PlatCmLinkAcknowledge(IN PHW_DEVICE_EXTENSION DevExt)
{
    UNREFERENCED_PARAMETER(DevExt);
}

// No channel: the engine's own timer periods apply.
ULONG PlatCmLinkPollUs(IN PHW_DEVICE_EXTENSION DevExt)
{
    UNREFERENCED_PARAMETER(DevExt);
    return 0;
}
#endif

#if !(defined(STOR98_DMA_POOL) && defined(NVME2K_W9X))
// No removal transaction without the interface 3 channel (platform.h).
ULONG PlatCmRemoveTimer(IN PHW_DEVICE_EXTENSION DevExt)
{
    UNREFERENCED_PARAMETER(DevExt);
    return 0;
}

BOOLEAN PlatCmRemoveStartIo(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb)
{
    UNREFERENCED_PARAMETER(DevExt);
    UNREFERENCED_PARAMETER(Srb);
    return FALSE;
}

BOOLEAN PlatCmRemoveActive(IN PHW_DEVICE_EXTENSION DevExt)
{
    UNREFERENCED_PARAMETER(DevExt);
    return FALSE;
}

BOOLEAN PlatCmRemoveLinkDown(IN PHW_DEVICE_EXTENSION DevExt)
{
    UNREFERENCED_PARAMETER(DevExt);
    return FALSE;
}

ULONG PlatCmRemoveGate(IN PHW_DEVICE_EXTENSION DevExt)
{
    UNREFERENCED_PARAMETER(DevExt);
    return 0;
}
#endif
