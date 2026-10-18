// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Yeo Kheng Meng
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
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
// q9x.c - Windows 9x qualification layer (q9x.h, docs/gates/Q-9x.md).
// Prototype only: test builds with STOR98_Q9X=1 on the 9x target. The CM
// side of the contract is include/stor98cm.h, interface 3 version 2; rules
// are cited as R1-R14 from there.
//
// Superseded and not built: src\sources refuses STOR98_Q9X. The product 9x
// receiver and pool are src/pool9x.c, src/p9bind.h and src/p9lock.h. They
// follow the adopted rules where this file does not (which entries are
// bound, the stall budget per callback, RemoveAck only after the pool
// record is released). Kept for reference only: take no rule from it.
//
// Every VMM service used here is a documented Windows 98 DDK VMM service
// (VMM.H service table), reached through the INT 20h dynalink the .mpd
// already uses for _MapPhysToLinear and _PageModifyPermissions: _PageAllocate,
// _PageFree, _PageGetSizeAddr, _CopyPageTable, Get_System_Time, _RegOpenKey,
// _RegCreateKey, _RegCloseKey, _RegQueryValueEx, _RegSetValueEx. No VxD, no
// IOS- or SCSIPORT-private data.

#include "nvme2k.h"
#include "utils.h"
#include "g9dbg.h"
#include "q9x.h"
#include "..\include\stor98cm.h"

#if defined(STOR98_Q9X) && defined(NVME2K_W9X)

#define NAKED __declspec(naked)

// ---------------------------------------------------------------------------
// VMM services (Windows 98 DDK VMM.H; ordinal = position in the VMM service
// table, bit 15 = jump form of the dynalink).
// ---------------------------------------------------------------------------

#define VMMJMP(ord) \
    __asm int 0x20 \
    __asm _emit ((ord) >> 0) & 0xFF \
    __asm _emit (((ord) >> 8) & 0xFF) | 0x80 \
    __asm _emit (VMM_DEVICE_ID >> 0) & 0xFF \
    __asm _emit (VMM_DEVICE_ID >> 8) & 0xFF

// _PageAllocate returns the block's linear address (also its handle) in EAX
// and in EDX; ULONGLONG returns EDX:EAX.
static ULONGLONG NAKED _cdecl Q9PageAllocate(ULONG nPages, ULONG pType, ULONG VM, ULONG AlignMask,
                                            ULONG minPhys, ULONG maxPhys, PULONG PhysAddr, ULONG flags)
{
    VMMJMP(0x0053)
}

static ULONG NAKED _cdecl Q9PageFree(ULONG hMem, ULONG flags)
{
    VMMJMP(0x0055)
}

// _PageGetSizeAddr: EAX = pages of the allocation whose handle is hMem (0 if
// hMem is not an allocation handle), EDX = its linear address.
static ULONGLONG NAKED _cdecl Q9PageGetSizeAddr(ULONG hMem, ULONG flags)
{
    VMMJMP(0x0058)
}

// _CopyPageTable: copies nPages page-table entries starting at linear page
// LinPgNum into PageBuf; returns nonzero on success. Reads no page contents.
static ULONG NAKED _cdecl Q9CopyPageTable(ULONG LinPgNum, ULONG nPages, PULONG PageBuf, ULONG flags)
{
    VMMJMP(0x0061)
}

// _LinPageLock: locks nPages linear pages starting at HLinPgNum in memory
// (lock count incremented; present and not pageable while locked); nonzero
// on success.
static ULONG NAKED _cdecl Q9LinPageLock(ULONG HLinPgNum, ULONG nPages, ULONG flags)
{
    VMMJMP(0x0063)
}

// Get_System_Time: milliseconds since Windows started, in EAX.
static ULONG NAKED _cdecl Q9GetSystemTime(VOID)
{
    VMMJMP(0x003F)
}

static ULONG NAKED _cdecl Q9RegOpenKey(ULONG hkey, PCHAR SubKey, PULONG phk)
{
    VMMJMP(0x0148)
}

static ULONG NAKED _cdecl Q9RegCloseKey(ULONG hkey)
{
    VMMJMP(0x0149)
}

static ULONG NAKED _cdecl Q9RegCreateKey(ULONG hkey, PCHAR SubKey, PULONG phk)
{
    VMMJMP(0x014A)
}

static ULONG NAKED _cdecl Q9RegDeleteValue(ULONG hkey, PCHAR Name)
{
    VMMJMP(0x014F)
}

static ULONG NAKED _cdecl Q9RegQueryValueEx(ULONG hkey, PCHAR Name, PULONG Reserved, PULONG Type,
                                           PVOID Data, PULONG cbData)
{
    VMMJMP(0x0151)
}

static ULONG NAKED _cdecl Q9RegSetValueEx(ULONG hkey, PCHAR Name, ULONG Reserved, ULONG Type,
                                         PVOID Data, ULONG cbData)
{
    VMMJMP(0x0152)
}

#define Q9_HKLM             0x80000002
#define Q9_HKDD             0x80000006      // HKEY_DYN_DATA
#define Q9_REG_BINARY       3
#define Q9_REG_DWORD        4
#define Q9_PG_SYS           1
#define Q9_PAGEZEROINIT     0x00000001
#define Q9_PAGEUSEALIGN     0x00000002
#define Q9_PAGECONTIG       0x00000004
#define Q9_PAGEFIXED        0x00000008
#define Q9_PTE_PRESENT      0x00000001
#define Q9_PTE_WRITE        0x00000002

// ---------------------------------------------------------------------------
// Atomic access (R6). x86 keeps loads ordered with loads and stores with
// stores; the compiler is kept in order by doing every publication-point
// access in inline assembly, which MSVC never moves memory accesses across.
// Locked instructions are also full CPU barriers.
// ---------------------------------------------------------------------------

static ULONG Q9Load(volatile ULONG *p)
{
    ULONG v;

    __asm {
        mov ecx, p
        mov eax, [ecx]
        mov v, eax
    }
    return v;
}

static ULONG Q9Xchg(volatile ULONG *p, ULONG val)
{
    ULONG old;

    __asm {
        mov ecx, p
        mov eax, val
        xchg eax, [ecx]
        mov old, eax
    }
    return old;
}

static ULONG Q9CmpXchg(volatile ULONG *p, ULONG cmpv, ULONG val)
{
    ULONG old;

    __asm {
        mov ecx, p
        mov edx, val
        mov eax, cmpv
        lock cmpxchg [ecx], edx
        mov old, eax
    }
    return old;
}

static ULONGLONG Q9Tsc(VOID)
{
    ULONG lo, hi;

    __asm {
        _emit 0x0F
        _emit 0x31
        mov lo, eax
        mov hi, edx
    }
    return ((ULONGLONG)hi << 32) | lo;
}

// Pool layout (pages): 0-15 queues, PRP list pages, utility buffer (what the
// port's uncached extension held); 16-31 bounce buffer; 32 metadata.
#define Q9_POOL_DMA_PAGES       16
#define Q9_POOL_BOUNCE_PAGES    16
#define Q9_POOL_PAGES           33
#define Q9_POOL_META_OFFSET     (32 * PAGE_SIZE)
#define Q9_POOL_META_SIG        0x4C503951      // 'Q9PL'
#define Q9_BOUNCE_BYTES         (Q9_POOL_BOUNCE_PAGES * PAGE_SIZE)

typedef struct _Q9_POOL_META {
    ULONG Signature;
    ULONG Physical;
    ULONG Linear;
    ULONG Loads;
    ULONG LastInstance;
    ULONG LastBus;
    ULONG LastSlot;
    ULONG LastGen;
    ULONG LastToken;
} Q9_POOL_META, *PQ9_POOL_META;

// Deferred pool work (R7)
#define Q9P_RELEASE         0x00000001      // my ISOLATED record -> RELEASED (terminal)
#define Q9P_E2              0x00000002      // resolve Q9E2Mask after my own disable

// Bring-up steps
#define Q9B_DISABLE_WAIT    1
#define Q9B_PROGRAM         2
#define Q9B_ENABLE_WAIT     3
#define Q9B_ADMIN           4
#define Q9B_VWC_SET         5
#define Q9B_VWC_GET         6
#define Q9B_FAIL_DISABLE    7

// Removal / shutdown handshake steps
#define Q9R_DRAIN           1
#define Q9R_FLUSH           2
#define Q9R_SHN             3
#define Q9R_DISABLE         4

// Handshake bounds (R10)
#define Q9_DRAIN_MS         2000
#define Q9_DISABLE_MS       5000
// One overall budget, measured from this instance's first sight of the
// request (R10): the request is seen at most POLL + LATE after publication,
// and the check that acts on a threshold can itself come POLL + LATE after
// the threshold, so every decision is due by ACK_BOUND - 2 * (POLL + LATE)
// (8500 ms); the disable is requested DISABLE_RESERVE before that (5500 ms),
// whatever phase is still running.
#define Q9_DISABLE_RESERVE_MS   3000
#define Q9_ACK_BY_MS        (STOR98LS9X_ACK_BOUND_MS - 2 * (STOR98LS9X_POLL_MS + STOR98LS9X_LATE_MS))
#define Q9_DISABLE_BY_MS    (Q9_ACK_BY_MS - Q9_DISABLE_RESERVE_MS)
#define Q9_FLUSH_MS         2000

#define Q9_ADMIN_CID_SETF   (Q9_ADMIN_CID_BASE + 1)
#define Q9_ADMIN_CID_GETF   (Q9_ADMIN_CID_BASE + 2)
#define Q9_FEAT_VWC         0x06

static ULONG Q9Now(VOID)
{
    return Q9GetSystemTime();
}

static ULONG Q9Csts(PHW_DEVICE_EXTENSION DevExt)
{
    return NvmeReadReg32(DevExt, NVME_REG_CSTS);
}

static VOID Q9Mark(ULONG Code, ULONG Arg)
{
    G9Stage(Code, Arg);
    G9Marker();
}

static const char *Q9StateName(ULONG s)
{
    switch (s) {
    case Q9_STARTING: return "STARTING";
    case Q9_RUNNING: return "RUNNING";
    case Q9_GONE: return "GONE";
    case Q9_LINKDOWN: return "LINKDOWN";
    case Q9_REMOVING: return "REMOVING";
    case Q9_QUIESCED: return "QUIESCED";
    case Q9_FAILED: return "FAILED";
    case Q9_RESETTING: return "RESETTING";
    case Q9_SHUTDOWN: return "SHUTDOWN";
    case Q9_STOPPED: return "STOPPED";
    }
    return "?";
}

static VOID Q9SetState(PHW_DEVICE_EXTENSION DevExt, ULONG s, const char *Why)
{
    G9Log("q9 state %s -> %s (%s) depth=%u nontagged=%08X held=%08X", Q9StateName(DevExt->Q9State),
          Q9StateName(s), Why, DevExt->CurrentQueueDepth, (ULONG)DevExt->NonTaggedInFlight,
          (ULONG)DevExt->Q9HeldSrb);
    DevExt->Q9State = s;
}

static BOOLEAN Q9Outstanding(PHW_DEVICE_EXTENSION DevExt)
{
    return (BOOLEAN)(DevExt->CurrentQueueDepth || DevExt->NonTaggedInFlight || DevExt->Q9HeldSrb);
}

static VOID Q9Tick(IN PVOID DeviceExtension);

static VOID Q9Arm(PHW_DEVICE_EXTENSION DevExt)
{
    ScsiPortNotification(RequestTimerCall, DevExt, Q9Tick, DevExt->Q9Opt.TickMs * 1000);
    DevExt->Q9TimerArmed = TRUE;
}

// The entry this instance is bound to and may still act on (R8d).
static PSTOR98LS_ENTRY Q9MyEntry(PHW_DEVICE_EXTENSION DevExt)
{
    if (DevExt->Q9Entry == NULL || DevExt->Q9IdentityLost) {
        return NULL;
    }
    return (PSTOR98LS_ENTRY)DevExt->Q9Entry;
}

// ---------------------------------------------------------------------------
// Options
// ---------------------------------------------------------------------------

static ULONG Q9RegDword(ULONG hk, PCHAR Name, ULONG Default)
{
    ULONG v = 0, type = 0, cb = sizeof(v);

    if (hk && Q9RegQueryValueEx(hk, Name, NULL, &type, &v, &cb) == 0 && cb == sizeof(v)) {
        return v;
    }
    return Default;
}

static VOID Q9ReadOptions(PHW_DEVICE_EXTENSION DevExt)
{
    ULONG hk = 0;
    Q9X_OPTIONS *o = &DevExt->Q9Opt;

    if (Q9RegOpenKey(Q9_HKLM, "Software\\stor98\\Q9x", &hk) != 0) {
        hk = 0;
    }
    o->Pool = Q9RegDword(hk, "Pool", 1);
    o->Bounce = Q9RegDword(hk, "Bounce", 0);
    o->WriteThrough = Q9RegDword(hk, "WriteThrough", 0);
    o->Deferred = 1;
    o->HoldMs = Q9RegDword(hk, "HoldMs", 0);     // 0: pure hold (scan INQUIRY, Q-9x G1)
    o->GoneMs = Q9RegDword(hk, "GoneMs", 5000);
    o->TickMs = Q9RegDword(hk, "TickMs", 100);
    o->NoAck = Q9RegDword(hk, "NoAck", 0);
    o->ShstMs = Q9RegDword(hk, "ShstMs", 2000);
    if (o->ShstMs > 2000) {
        o->ShstMs = 2000;                           // R10 bound
    }
    o->PoolFree = Q9RegDword(hk, "PoolFree", 0);
    o->CapPages = Q9RegDword(hk, "CapPages", 8 * Q9_POOL_PAGES);
    o->ResetMode = Q9RegDword(hk, "ResetMode", 0);
    o->BringupMs = Q9RegDword(hk, "BringupMs", 0);
    o->IgnoreCm = Q9RegDword(hk, "IgnoreCm", 0);
    o->CorruptRec = Q9RegDword(hk, "CorruptRec", 0);
    o->CorruptAcct = Q9RegDword(hk, "CorruptAcct", 0);
    o->IsrProbe = Q9RegDword(hk, "IsrProbe", 0);
    o->BudgetSkewMs = Q9RegDword(hk, "BudgetSkewMs", 0);
    if (o->TickMs < 10) {
        o->TickMs = 10;
    }
    if (o->TickMs > STOR98LS9X_POLL_MS) {
        o->TickMs = STOR98LS9X_POLL_MS;             // R10
    }
    if (hk) {
        Q9RegCloseKey(hk);
    }
    G9Log("q9 options (key %s): CorruptRec=%u Pool=%u Bounce=%u WriteThrough=%u HoldMs=%u GoneMs=%u TickMs=%u NoAck=%u "
          "ShstMs=%u PoolFree=%u CapPages=%u ResetMode=%u BringupMs=%u IgnoreCm=%u",
          hk ? "found" : "absent", o->CorruptRec, o->Pool, o->Bounce, o->WriteThrough, o->HoldMs, o->GoneMs, o->TickMs,
          o->NoAck, o->ShstMs, o->PoolFree, o->CapPages, o->ResetMode, o->BringupMs, o->IgnoreCm);
    G9Stage(Q9S_OPTIONS, o->Pool | (o->Bounce << 1) | (o->WriteThrough << 2) | (o->NoAck << 4) |
            (o->PoolFree << 5) | (o->ResetMode << 6));
}

// ---------------------------------------------------------------------------
// Residency and provenance (R5b, R5e): page-table entries only, through the
// documented _CopyPageTable; no page content is touched here.
// ---------------------------------------------------------------------------

// TRUE when [Linear, Linear + Bytes) lies inside the system arena.
static BOOLEAN Q9InArena(ULONG Linear, ULONG Bytes)
{
    if (Bytes == 0 || Linear < STOR98LS9X_ARENA_LO || Linear >= STOR98LS9X_ARENA_HI) {
        return FALSE;
    }
    return (BOOLEAN)(Bytes <= STOR98LS9X_ARENA_HI - Linear);
}

// TRUE when every page of [Linear, Linear + Bytes) is present; with Phys
// nonzero, also when page i maps physical page Phys + i * PAGE_SIZE.
static BOOLEAN Q9PagesPresent(ULONG Linear, ULONG Bytes, ULONG Phys)
{
    ULONG first = Linear >> PAGE_SHIFT, last = (Linear + Bytes - 1) >> PAGE_SHIFT, i, pte;

    for (i = first; i <= last; i++) {
        pte = 0;
        if (!Q9CopyPageTable(i, 1, &pte, 0) || !(pte & Q9_PTE_PRESENT)) {
            G9Log("q9 page %08X not present (pte %08X)", i << PAGE_SHIFT, pte);
            return FALSE;
        }
        if (Phys && (pte & ~(PAGE_SIZE - 1)) != Phys + (i - first) * PAGE_SIZE) {
            G9Log("q9 page %08X maps %08X, record says %08X", i << PAGE_SHIFT, pte & ~(PAGE_SIZE - 1),
                  Phys + (i - first) * PAGE_SIZE);
            return FALSE;
        }
    }
    return TRUE;
}

// TRUE when every page of [Linear, Linear + Bytes) is present and writable.
static BOOLEAN Q9PagesWritable(ULONG Linear, ULONG Bytes)
{
    ULONG first = Linear >> PAGE_SHIFT, last = (Linear + Bytes - 1) >> PAGE_SHIFT, i, pte;

    for (i = first; i <= last; i++) {
        pte = 0;
        if (!Q9CopyPageTable(i, 1, &pte, 0) || (pte & (Q9_PTE_PRESENT | Q9_PTE_WRITE)) !=
            (Q9_PTE_PRESENT | Q9_PTE_WRITE)) {
            G9Log("q9 page %08X not present and writable (pte %08X)", i << PAGE_SHIFT, pte);
            return FALSE;
        }
    }
    return TRUE;
}

// ---------------------------------------------------------------------------
// stor98 boot block (R7 of interface 3; Q-9x "Interface 3 revision"): the
// MD-4 pool ownership records, instance tokens, the per-function binding and
// the boot-wide pool budget. One 2-page _PageAllocate block per boot, never
// freed, found again by later loads (the .mpd is unloaded at every removal)
// through HKLM\Software\stor98\Q9x "Acct", and accepted only when the
// referenced pages are present and map the recorded physical pages, carry
// the signature and the 64-bit cookie, and are the start of a 2-page VMM
// allocation. Anything else (a previous boot's value, or none) creates a new
// block for this boot. No clock is involved; every failure fails the load
// before CC.EN (closed).
// ---------------------------------------------------------------------------

#define Q9_ACCT_PAGES       2
#define Q9_ACCT_SIG         0x43413951      // 'Q9AC'
#define Q9_FUNCS            8
#define Q9_RECS             8

// Pool record states
#define Q9PL_FREE           0
#define Q9PL_RESERVED       1       // claimed by Owner; never touched by others
#define Q9PL_OWNED          2       // Owner's controller may reach the block
#define Q9PL_ISOLATED       3       // isolation evidence, still owned in software
#define Q9PL_RELEASED       4       // isolated and given up: any load may claim it
#define Q9PL_QUARANTINED    5       // given up without isolation evidence

#define Q9RF_NOFUNC         0x00000001  // owner had no function binding (liveness unknown)

typedef struct _Q9_FUNC {
    ULONG Bus;                      // 0xFFFFFFFF: unused
    ULONG Slot;
    volatile ULONG Token;           // current instance for the function
    volatile ULONG Live;            // 1 while that instance has not stopped
} Q9_FUNC;

typedef struct _Q9_REC {
    volatile ULONG State;
    volatile ULONG Owner;           // token
    ULONG Flags;                    // Q9RF_*
    ULONG Bus;                      // the owner's function
    ULONG Slot;
    ULONG Gen;                      // LinkGeneration the owner was bound to (0: none)
    ULONG TableCookie;              // cookie of that CM table (0: none)
    ULONG Handle;
    ULONG Linear;
    ULONG Physical;
    ULONG Pages;
    ULONG Loads;
    ULONG LastOwner;
} Q9_REC, *PQ9_REC;

typedef struct _Q9_ACCT {
    ULONG Signature;
    ULONG CookieLo;
    ULONG CookieHi;
    ULONG Linear;
    ULONG Physical;
    volatile ULONG ChargedPages;
    volatile ULONG Charges;
    volatile ULONG Uncharges;
    ULONG CreatedMs;
    volatile ULONG Lock;            // 0 free, else the holder's token (or 0xFFFFFFFF during setup)
    volatile ULONG NextToken;
    ULONG BootCookie;               // == the reference's (current-boot authority, P2-4)
    volatile ULONG LockedTable;     // cookie of the CM table whose pages this boot locked
    ULONG Reserved[3];
    Q9_FUNC Func[Q9_FUNCS];
    Q9_REC Rec[Q9_RECS];
} Q9_ACCT, *PQ9_ACCT;

typedef struct _Q9_ACCT_REF {
    ULONG Linear;
    ULONG Physical;
    ULONG CookieLo;
    ULONG CookieHi;
    ULONG BootCookie;       // the CM's current-boot proof cookie at creation, 0 if none
    ULONG Check;            // Q9AcctRefCheck of the fields above
} Q9_ACCT_REF;

// Registry values (HKLM\Software\stor98\Q9x). "AcctRef" (REG_BINARY,
// Q9_ACCT_REF) replaces the unchecked "Acct" of earlier builds, which is
// ignored. "AcctBoot" (REG_DWORD) is the proof cookie of the boot that last
// initialised accounting, written before "AcctRef".
#define Q9_ACCT_REF_VALUE   "AcctRef"
#define Q9_ACCT_BOOT_VALUE  "AcctBoot"
#define Q9_ERROR_FILE_NOT_FOUND 2

static ULONG Q9AcctRefCheck(const Q9_ACCT_REF *r)
{
    ULONG c = 0x4E393841, v[5], i;

    v[0] = r->Linear;
    v[1] = r->Physical;
    v[2] = r->CookieLo;
    v[3] = r->CookieHi;
    v[4] = r->BootCookie;
    for (i = 0; i < 5; i++) {
        c = ((c << 7) | (c >> 25)) ^ v[i];
        c *= 0x9E3779B1;
    }
    return c;
}

static PQ9_ACCT gQ9Acct;                // this image load's view of the block
static volatile ULONG gQ9AcctBusy;

static BOOLEAN Q9AcctCheck(Q9_ACCT_REF *ref)
{
    PQ9_ACCT a;
    ULONGLONG info;

    if ((ref->Linear & (PAGE_SIZE - 1)) || (ref->Physical & (PAGE_SIZE - 1)) || ref->Physical == 0 ||
        !Q9InArena(ref->Linear, Q9_ACCT_PAGES * PAGE_SIZE)) {
        G9Log("q9 boot block: reference %08X/%08X out of range", ref->Linear, ref->Physical);
        return FALSE;
    }
    // Present and mapping the recorded RAM pages: reading them is harmless.
    if (!Q9PagesPresent(ref->Linear, Q9_ACCT_PAGES * PAGE_SIZE, ref->Physical)) {
        return FALSE;
    }
    a = (PQ9_ACCT)ref->Linear;
    if (a->Signature != Q9_ACCT_SIG || a->CookieLo != ref->CookieLo || a->CookieHi != ref->CookieHi ||
        a->Linear != ref->Linear || a->Physical != ref->Physical) {
        G9Log("q9 boot block: %08X does not hold this boot's block (sig %08X)", ref->Linear, a->Signature);
        return FALSE;
    }
    // Only then ask VMM whether it is the start of a 2-page allocation.
    info = Q9PageGetSizeAddr(ref->Linear, 0);
    if ((ULONG)info != Q9_ACCT_PAGES || (ULONG)(info >> 32) != ref->Linear) {
        G9Log("q9 boot block: %08X is not a %u-page allocation (info %08X/%08X)", ref->Linear, Q9_ACCT_PAGES,
              (ULONG)info, (ULONG)(info >> 32));
        return FALSE;
    }
    return TRUE;
}

// Discovery is anchored in the CM's current-boot proof when one exists
// (Proof != 0). Then the load fails closed on anything but a clean answer:
// a lookup error other than "not found", a wrong type or size, a reference
// whose check word does not match (a corrupted cookie included), a reference
// of this boot whose block does not check out, or a missing reference while
// "AcctBoot" says this boot already initialised. Only a valid reference of
// another boot, or no reference and no marker of this boot, starts new
// accounting. Without a CM there is no current-boot authority and an
// unusable reference is treated as a new boot (documented residual; Q-9x
// "Interface 3 re-review").
static PQ9_ACCT Q9AcctGet(ULONG Proof)
{
    ULONG hk = 0, type = 0, cb, phys = 0, i, rc, boot = 0;
    ULONG buf[(sizeof(Q9_ACCT_REF) + 16) / sizeof(ULONG)];
    Q9_ACCT_REF ref;
    PQ9_ACCT a = NULL;
    ULONGLONG r, tsc;
    const char *why = NULL;

    if (gQ9Acct != NULL) {
        return gQ9Acct;
    }
    if (Q9Xchg(&gQ9AcctBusy, 1) != 0) {
        G9Log("q9 boot block: busy (another load is creating it): fail closed");
        return NULL;
    }
    if (Q9RegCreateKey(Q9_HKLM, "Software\\stor98\\Q9x", &hk) != 0) {
        G9Log("q9 boot block: cannot open its key: fail closed");
        Q9Xchg(&gQ9AcctBusy, 0);
        return NULL;
    }
    RtlZeroMemory(&ref, sizeof(ref));
    RtlZeroMemory(buf, sizeof(buf));
    cb = sizeof(buf);
    rc = Q9RegQueryValueEx(hk, Q9_ACCT_REF_VALUE, NULL, &type, buf, &cb);
    if (rc == Q9_ERROR_FILE_NOT_FOUND) {
        cb = 0;
        if (Proof != 0) {
            ULONG bt = 0, bcb = sizeof(boot), brc;
            // Only "not found" for both values means a first initialisation.
            brc = Q9RegQueryValueEx(hk, Q9_ACCT_BOOT_VALUE, NULL, &bt, &boot, &bcb);
            if (brc == Q9_ERROR_FILE_NOT_FOUND) {
                // neither value: first installation (residual with a CM, Q-9x)
            } else if (brc != 0) {
                why = "reference missing and the boot marker lookup failed";
            } else if (bt != Q9_REG_DWORD || bcb != sizeof(boot)) {
                why = "reference missing and the boot marker is malformed";
            } else if (boot == Proof) {
                why = "reference missing, but this boot already initialised accounting";
            }
        }
    } else if (rc != 0) {
        why = "reference lookup failed";
        cb = 0;
    } else if (type != Q9_REG_BINARY || cb != sizeof(ref)) {
        why = "reference has the wrong type or size";
        cb = 0;
    } else {
        RtlCopyMemory(&ref, buf, sizeof(ref));
        if (ref.Check != Q9AcctRefCheck(&ref)) {
            why = "reference check word does not match (corrupted)";
            cb = 0;
        }
    }
    if (why != NULL && Proof == 0) {
        G9Log("q9 boot block: %s (rc %u type %u size %u); no CM proof: treated as a new boot (residual)", why, rc,
              type, cb);
        why = NULL;
    }
    if (why != NULL) {
        G9Log("q9 boot block: %s (rc %u type %u); CM proof %08X present: accounting lost or corrupt, fail closed",
              why, rc, type, Proof);
        Q9Mark(Q9S_FAILED, 0x16);
    } else if (cb && Q9AcctCheck(&ref) && ((PQ9_ACCT)ref.Linear)->BootCookie == ref.BootCookie) {
        a = (PQ9_ACCT)ref.Linear;
        G9Log("q9 boot block: this boot's block at %08X: %u pages charged (%u charges, %u uncharges), next token %u, boot cookie %08X",
              ref.Linear, a->ChargedPages, a->Charges, a->Uncharges, a->NextToken, a->BootCookie);
    } else if (cb && Proof != 0 && ref.BootCookie == Proof) {
        G9Log("q9 boot block: the reference was recorded in this boot (proof %08X) but its block does not check out: "
              "accounting lost or corrupt, fail closed", Proof);
        Q9Mark(Q9S_FAILED, 0x15);
    } else {
        r = Q9PageAllocate(Q9_ACCT_PAGES, Q9_PG_SYS, 0, 0, 0, 0xFFFFF, &phys,
                           Q9_PAGEFIXED | Q9_PAGEUSEALIGN | Q9_PAGECONTIG | Q9_PAGEZEROINIT);
        if ((ULONG)r == 0 || phys == 0) {
            G9Log("q9 boot block: _PageAllocate failed: fail closed");
        } else {
            tsc = Q9Tsc();
            a = (PQ9_ACCT)(ULONG)r;
            a->CookieLo = (ULONG)tsc ^ Q9Now();
            a->CookieHi = (ULONG)(tsc >> 32) ^ (ULONG)r;
            a->Linear = (ULONG)r;
            a->Physical = phys;
            a->CreatedMs = Q9Now();
            a->BootCookie = Proof;
            for (i = 0; i < Q9_FUNCS; i++) {
                a->Func[i].Bus = 0xFFFFFFFF;
            }
            a->Signature = Q9_ACCT_SIG;
            i = ref.BootCookie;                     // the previous reference's proof, for the log
            ref.Linear = a->Linear;
            ref.Physical = phys;
            ref.CookieLo = a->CookieLo;
            ref.CookieHi = a->CookieHi;
            ref.BootCookie = Proof;
            ref.Check = Q9AcctRefCheck(&ref);
            boot = Proof;
            if (Q9RegSetValueEx(hk, Q9_ACCT_BOOT_VALUE, 0, Q9_REG_DWORD, &boot, sizeof(boot)) != 0 ||
                Q9RegSetValueEx(hk, Q9_ACCT_REF_VALUE, 0, Q9_REG_BINARY, &ref, sizeof(ref)) != 0) {
                G9Log("q9 boot block: cannot record it: fail closed");
                a->Signature = 0;
                Q9PageFree((ULONG)r, 0);
                a = NULL;
            } else {
                G9Log("q9 boot block: first initialisation in this boot: block at %08X phys %08X, proof %08X (previous reference %s, its proof %08X)",
                      (ULONG)r, phys, Proof, cb ? "not this boot's" : "absent or unusable", cb ? i : 0);
            }
        }
    }
    Q9RegCloseKey(hk);
    gQ9Acct = a;
    Q9Xchg(&gQ9AcctBusy, 0);
    return a;
}

// Charges Pages against the budget Cap; overflow-safe, never spins.
static BOOLEAN Q9AcctCharge(PQ9_ACCT a, ULONG Pages, ULONG Cap)
{
    ULONG i, old;

    for (i = 0; i < 8; i++) {
        old = Q9Load(&a->ChargedPages);
        if (Pages > Cap || old > Cap - Pages) {
            G9Log("q9 budget: %u charged + %u > cap %u: refused", old, Pages, Cap);
            return FALSE;
        }
        if (Q9CmpXchg(&a->ChargedPages, old, old + Pages) == old) {
            a->Charges++;
            G9Log("q9 budget: charged %u pages (%u -> %u of %u)", Pages, old, old + Pages, Cap);
            return TRUE;
        }
    }
    G9Log("q9 budget: contention: refused (closed)");
    return FALSE;
}

static VOID Q9AcctUncharge(PQ9_ACCT a, ULONG Pages)
{
    ULONG i, old;

    for (i = 0; i < 8; i++) {
        old = Q9Load(&a->ChargedPages);
        if (old < Pages) {
            G9Log("q9 budget: uncharge %u of %u: inconsistent, left as is", Pages, old);
            return;
        }
        if (Q9CmpXchg(&a->ChargedPages, old, old - Pages) == old) {
            a->Uncharges++;
            G9Log("q9 budget: uncharged %u pages (%u -> %u)", Pages, old, old - Pages);
            return;
        }
    }
    G9Log("q9 budget: uncharge contention: over-count kept");
}

// Boot-block lock: one attempt from the timer and HwStartIo (the work is
// deferred if busy), up to 10 attempts 100 us apart where a stall is allowed.
static BOOLEAN Q9Lock(PHW_DEVICE_EXTENSION DevExt, ULONG Tries)
{
    PQ9_ACCT a = gQ9Acct;
    ULONG i, me = DevExt->Q9Token ? DevExt->Q9Token : 0xFFFFFFFF;

    if (a == NULL) {
        return FALSE;
    }
    for (i = 0; i < Tries; i++) {
        if (Q9CmpXchg(&a->Lock, 0, me) == 0) {
            return TRUE;
        }
        if (i + 1 < Tries) {
            ScsiPortStallExecution(100);
        }
    }
    G9Log("q9 boot block lock busy (holder %u)", Q9Load(&a->Lock));
    return FALSE;
}

static VOID Q9Unlock(PHW_DEVICE_EXTENSION DevExt)
{
    PQ9_ACCT a = gQ9Acct;

    Q9Xchg(&a->Lock, 0);
    UNREFERENCED_PARAMETER(DevExt);
}

// ---------------------------------------------------------------------------
// Entry snapshots (R6c) and their validation (R5e)
// ---------------------------------------------------------------------------

typedef struct _Q9_SNAP {
    ULONG Bus;
    ULONG Slot;
    ULONG Flags;
    ULONG Gen;
    ULONG RemSeq;
} Q9_SNAP;

#define Q9SNAP_OK       0
#define Q9SNAP_BUSY     1       // being updated: try at the next poll

static ULONG Q9Snapshot(PSTOR98LS_ENTRY e, Q9_SNAP *s)
{
    ULONG attempt, s1, s2;

    for (attempt = 0; attempt < 2; attempt++) {
        s1 = Q9Load(&e->Sequence);
        if (s1 & 1) {
            continue;
        }
        s->Bus = Q9Load(&e->BusNumber);
        s->Slot = Q9Load(&e->SlotNumber);
        s->Flags = Q9Load(&e->Flags);
        s->Gen = Q9Load(&e->LinkGeneration);
        s->RemSeq = Q9Load(&e->RemoveSequence);
        s2 = Q9Load(&e->Sequence);
        if (s1 == s2) {
            return Q9SNAP_OK;
        }
    }
    return Q9SNAP_BUSY;
}

// R5e: 0 when the snapshot is legal, else a reason code.
static ULONG Q9SnapCheck(const Q9_SNAP *s)
{
    ULONG f = s->Flags;

    if (f & ~STOR98LS9X_FLAGS_KNOWN) return 0x01;
    if (!(f & STOR98LS_IN_USE)) return 0x02;
    if ((f & STOR98LS_UP) && (f & STOR98LS_DOWN_QUIESCED)) return 0x03;
    if ((f & STOR98LS_REMOVE_PENDING) && !(f & STOR98LS_UP)) return 0x04;
    if ((f & STOR98LS_REMOVE_PENDING) && (s->RemSeq == 0 || (s->RemSeq & ~STOR98LS9X_SEQ_MASK))) return 0x05;
    if ((f & (STOR98LS_UP | STOR98LS_DOWN_QUIESCED)) && s->Gen == 0) return 0x06;
    return 0;
}

// Stops acting on the entry for the rest of this instance's life (R5e, R8d).
static VOID Q9LoseIdentity(PHW_DEVICE_EXTENSION DevExt, ULONG Reason, const char *Why)
{
    if (DevExt->Q9IdentityLost) {
        return;
    }
    DevExt->Q9IdentityLost = TRUE;
    Q9Mark(Q9S_IDENTITY, Reason);
    G9Log("q9 entry: %s (reason %02X): no further evidence or acknowledgement from this instance", Why, Reason);
}

// ---------------------------------------------------------------------------
// Rendezvous: locator, current-boot proof, table (R3, R5a-d); binding (R8d)
// ---------------------------------------------------------------------------

static VOID Q9Bind(PHW_DEVICE_EXTENSION DevExt)
{
    PSTOR98LS_TABLE t = (PSTOR98LS_TABLE)DevExt->Q9Table;
    PSTOR98LS_ENTRY e = NULL;
    Q9_SNAP s, pick;
    ULONG i, n = 0;

    for (i = 0; i < STOR98LS_ENTRIES; i++) {
        if (Q9Snapshot(&t->Entry[i], &s) != Q9SNAP_OK || Q9SnapCheck(&s) != 0) {
            continue;                               // unused, being updated or illegal
        }
        if (s.Bus != DevExt->BusNumber || s.Slot != DevExt->SlotNumber) {
            continue;
        }
        n++;
        e = &t->Entry[i];
        pick = s;
    }
    if (n > 1) {
        G9Log("q9 bind: %u entries name bus %u slot %u: duplicate, table rejected (no channel)", n,
              DevExt->BusNumber, DevExt->SlotNumber);
        DevExt->Q9Table = NULL;
        DevExt->Q9RendezvousRc = 9;
        return;
    }
    if (n == 0 || pick.Gen == 0) {
        G9Log("q9 bind: no entry for bus %u slot %u: no link-state channel for this load", DevExt->BusNumber,
              DevExt->SlotNumber);
        return;
    }
    DevExt->Q9Entry = e;
    DevExt->Q9Gen = pick.Gen;
    DevExt->Q9BindSeq = pick.RemSeq & STOR98LS9X_SEQ_MASK;
    G9Log("q9 bind: entry %u generation %u flags %08X, removal sequence at binding %u, ack %08X",
          (ULONG)(e - t->Entry), pick.Gen, pick.Flags, DevExt->Q9BindSeq, Q9Load(&e->RemoveAck));
}

// R5b: reads the current-boot proof (a CM-hosted dynamic key) once per load,
// before the boot block is looked up (it is also that lookup's authority).
// 0: proof present (Q9ProofCookie set); 10: no proof key; 11: unusable value.
static ULONG Q9ReadProof(PHW_DEVICE_EXTENSION DevExt)
{
    ULONG hk = 0, type = 0, cb = sizeof(ULONG), rc, proof = 0;

    DevExt->Q9ProofCookie = 0;
    DevExt->Q9ProofTable[0] = DevExt->Q9ProofTable[1] = DevExt->Q9ProofTable[2] = 0;
    if (DevExt->Q9Opt.IgnoreCm) {
        return 7;
    }
    rc = Q9RegOpenKey(Q9_HKDD, STOR98LS9X_PROOF_KEY, &hk);
    if (rc != 0) {
        G9Log("q9 rendezvous: no current-boot proof HKEY_DYN_DATA\\%s (rc=%u)", STOR98LS9X_PROOF_KEY, rc);
        return 10;
    }
    rc = Q9RegQueryValueEx(hk, STOR98LS9X_PROOF_VALUE, NULL, &type, &proof, &cb);
    Q9RegCloseKey(hk);
    G9Log("q9 rendezvous: proof rc=%u type=%u cb=%u cookie=%08X", rc, type, cb, proof);
    if (rc != 0 || type != Q9_REG_DWORD || cb != sizeof(proof) || proof == 0) {
        return 11;
    }
    DevExt->Q9ProofCookie = proof;
    // The table address authority (option A, R3a): the provider's TableLinear.
    hk = 0;
    if (Q9RegOpenKey(Q9_HKDD, STOR98LS9X_PROOF_KEY, &hk) == 0) {
        type = 0;
        cb = sizeof(DevExt->Q9ProofTable);
        rc = Q9RegQueryValueEx(hk, STOR98LS9X_PROOF_TABLE_VALUE, NULL, &type, DevExt->Q9ProofTable, &cb);
        Q9RegCloseKey(hk);
        G9Log("q9 rendezvous: proof table rc=%u type=%u cb=%u table=%08X size=%u cookie=%08X", rc, type, cb,
              DevExt->Q9ProofTable[0], DevExt->Q9ProofTable[1], DevExt->Q9ProofTable[2]);
        if (rc != 0 || type != Q9_REG_BINARY || cb != sizeof(STOR98LS9X_PROOF_TABLE)) {
            DevExt->Q9ProofTable[0] = DevExt->Q9ProofTable[1] = DevExt->Q9ProofTable[2] = 0;
        }
    }
    return 0;
}

// Reads the locator once and checks it against the proof read by
// Q9ReadProof (ProofRc); returns 0 when the table is accepted.
static ULONG Q9Rendezvous(PHW_DEVICE_EXTENSION DevExt, ULONG ProofRc)
{
    ULONG hk = 0, type = 0, cb, rc;
    PQ9_ACCT a = gQ9Acct;
    STOR98LS9X_LOCATOR loc;
    PSTOR98LS_TABLE t;
    ULONG t0 = Q9Now();

    DevExt->Q9Table = NULL;
    DevExt->Q9Entry = NULL;
    DevExt->Q9Gen = 0;
    if (DevExt->Q9Opt.IgnoreCm) {
        G9Log("q9 rendezvous: IgnoreCm option: behaving as if no CM were present");
        return 7;
    }
    // R5a: the locator (persistent; only a locator).
    rc = Q9RegOpenKey(Q9_HKLM, STOR98LS9X_LOCATOR_KEY, &hk);
    if (rc != 0) {
        G9Log("q9 rendezvous: HKLM\\%s not found (rc=%u): CM channel unavailable", STOR98LS9X_LOCATOR_KEY, rc);
        return 1;
    }
    RtlZeroMemory(&loc, sizeof(loc));
    cb = sizeof(loc);
    rc = Q9RegQueryValueEx(hk, STOR98LS9X_LOCATOR_VALUE, NULL, &type, &loc, &cb);
    Q9RegCloseKey(hk);
    G9Log("q9 rendezvous: locator rc=%u type=%u cb=%u sig=%08X ver=%u table=%08X size=%u cookie=%08X",
          rc, type, cb, loc.Signature, loc.Version, loc.TableLinear, loc.TableSize, loc.Cookie);
    if (rc != 0 || type != Q9_REG_BINARY || cb != sizeof(loc)) {
        return 2;
    }
    if (loc.Signature != STOR98LS_SIGNATURE || loc.Version != STOR98LS9X_LOCATOR_VERSION ||
        loc.TableSize != sizeof(STOR98LS_TABLE) || loc.Cookie == 0) {
        return 3;
    }
    // R5b: the current-boot proof must exist and carry the locator's cookie.
    if (ProofRc != 0) {
        G9Log("q9 rendezvous: no usable current-boot proof (rc %u): locator is stale", ProofRc);
        return ProofRc;
    }
    if (DevExt->Q9ProofCookie != loc.Cookie) {
        G9Log("q9 rendezvous: proof cookie %08X != locator cookie %08X", DevExt->Q9ProofCookie, loc.Cookie);
        return 11;
    }
    // R3a/R5b: the resident provider's TableLinear is the authority for the
    // address; the locator must agree with it in address, size and cookie.
    if (DevExt->Q9ProofTable[0] != loc.TableLinear || DevExt->Q9ProofTable[1] != sizeof(STOR98LS_TABLE) ||
        DevExt->Q9ProofTable[2] != loc.Cookie) {
        G9Log("q9 rendezvous: proof table %08X/%u/%08X does not match the locator %08X/%u/%08X", DevExt->Q9ProofTable[0],
              DevExt->Q9ProofTable[1], DevExt->Q9ProofTable[2], loc.TableLinear, loc.TableSize, loc.Cookie);
        return 14;
    }
    // R5c: range and alignment, then residency, before the first dereference.
    if ((loc.TableLinear & 3) || !Q9InArena(loc.TableLinear, loc.TableSize)) {
        return 4;
    }
    if (!Q9PagesPresent(loc.TableLinear, loc.TableSize, 0)) {
        return 5;
    }
    // Write access (stor98 writes AckGeneration and RemoveAck) and residency
    // stor98 establishes itself: the pages are locked with the documented
    // _LinPageLock once per boot and table (never unlocked: the table is
    // never freed, R4), so they stay present whatever the CM does later.
    if (!Q9PagesWritable(loc.TableLinear, loc.TableSize)) {
        return 12;
    }
    if (a == NULL || Q9Load(&a->LockedTable) != loc.Cookie) {
        if (!Q9LinPageLock(loc.TableLinear >> PAGE_SHIFT,
                           ((loc.TableLinear + loc.TableSize - 1) >> PAGE_SHIFT) - (loc.TableLinear >> PAGE_SHIFT) + 1,
                           0)) {
            G9Log("q9 rendezvous: _LinPageLock of the table pages failed");
            return 13;
        }
        if (a != NULL) {
            Q9Xchg(&a->LockedTable, loc.Cookie);
        }
        G9Log("q9 rendezvous: table pages locked (_LinPageLock) for this boot");
    }
    // R5d
    t = (PSTOR98LS_TABLE)loc.TableLinear;
    if (t->Signature != STOR98LS_SIGNATURE || t->Version != STOR98LS_VERSION || t->Count != STOR98LS_ENTRIES ||
        t->Cookie != loc.Cookie) {
        G9Log("q9 rendezvous: table header mismatch (sig %08X ver %u count %u cookie %08X)", t->Signature,
              t->Version, t->Count, t->Cookie);
        return 6;
    }
    DevExt->Q9Table = t;
    DevExt->Q9TableCookie = t->Cookie;
    Q9Bind(DevExt);
    if (DevExt->Q9Table == NULL) {
        return DevExt->Q9RendezvousRc;
    }
    G9Log("q9 rendezvous: table %08X accepted, entry %08X (%u ms)", (ULONG)t, (ULONG)DevExt->Q9Entry,
          Q9Now() - t0);
    return 0;
}

// ---------------------------------------------------------------------------
// MD-4 pool records (boot block, under its lock)
// ---------------------------------------------------------------------------

static Q9_FUNC *Q9FindFunc(PQ9_ACCT a, ULONG Bus, ULONG Slot)
{
    ULONG i;

    for (i = 0; i < Q9_FUNCS; i++) {
        if (a->Func[i].Bus == Bus && a->Func[i].Slot == Slot) {
            return &a->Func[i];
        }
    }
    return NULL;
}

// Is the owner of r still alive? Unknown owners count as alive.
static BOOLEAN Q9OwnerAlive(PQ9_ACCT a, PQ9_REC r)
{
    Q9_FUNC *f;

    if (r->Flags & Q9RF_NOFUNC) {
        return TRUE;
    }
    f = Q9FindFunc(a, r->Bus, r->Slot);
    return (BOOLEAN)(f != NULL && Q9Load(&f->Live) && Q9Load(&f->Token) == Q9Load(&r->Owner));
}

// E1: this load's CM table states the record's generation is quiesced on the
// record's own entry (R8b).
static BOOLEAN Q9EvidenceE1(PHW_DEVICE_EXTENSION DevExt, PQ9_REC r)
{
    PSTOR98LS_TABLE t = (PSTOR98LS_TABLE)DevExt->Q9Table;
    Q9_SNAP s;
    ULONG i;

    if (t == NULL || r->Gen == 0 || r->TableCookie != DevExt->Q9TableCookie) {
        return FALSE;
    }
    for (i = 0; i < STOR98LS_ENTRIES; i++) {
        if (Q9Snapshot(&t->Entry[i], &s) != Q9SNAP_OK || Q9SnapCheck(&s) != 0 || s.Bus != r->Bus ||
            s.Slot != r->Slot) {
            continue;
        }
        return (BOOLEAN)(s.Gen > r->Gen || ((s.Flags & STOR98LS_DOWN_QUIESCED) && s.Gen >= r->Gen));
    }
    return FALSE;
}

static VOID Q9RecLog(ULONG i, PQ9_REC r, ULONG From, const char *Why)
{
    G9Stage(Q9S_POOL, i | (r->State << 8) | (From << 12) | (r->Pages << 16));
    G9Log("q9 pool[%u]: %u -> %u (%s) owner=%u last=%u bus=%u slot=%u gen=%u linear=%08X phys=%08X", i, From,
          r->State, Why, r->Owner, r->LastOwner, r->Bus, r->Slot, r->Gen, r->Linear, r->Physical);
}

// Under the lock: the record stops having an owner.
static VOID Q9RecDisown(ULONG i, PQ9_REC r, ULONG State, const char *Why)
{
    ULONG from = r->State;

    r->LastOwner = r->Owner;
    r->Owner = 0;
    Q9Xchg(&r->State, State);
    Q9RecLog(i, r, from, Why);
}

// This instance's record, only while it still owns it (every update
// verifies ownership).
static PQ9_REC Q9MyRecord(PHW_DEVICE_EXTENSION DevExt)
{
    PQ9_ACCT a = gQ9Acct;
    PQ9_REC r;

    if (a == NULL || DevExt->Q9PoolIndex < 0) {
        return NULL;
    }
    r = &a->Rec[DevExt->Q9PoolIndex];
    if (Q9Load(&r->Owner) != DevExt->Q9Token) {
        G9Log("q9 pool[%d]: no longer owned by token %u (owner %u, state %u)", DevExt->Q9PoolIndex,
              DevExt->Q9Token, r->Owner, r->State);
        DevExt->Q9PoolIndex = -1;
        return NULL;
    }
    return r;
}

// Owner-only transition without the lock (OWNED <-> ISOLATED).
static VOID Q9PoolMine(PHW_DEVICE_EXTENSION DevExt, ULONG From, ULONG To, const char *Why)
{
    PQ9_REC r = Q9MyRecord(DevExt);

    if (r == NULL || Q9Load(&r->State) != From) {
        return;
    }
    Q9Xchg(&r->State, To);
    Q9RecLog((ULONG)DevExt->Q9PoolIndex, r, From, Why);
}

// Closes the queue gate and disconnects every pointer into the DMA block
// before the block can be released or freed (another instance may claim it
// while this devnode still exists, and a shared IRQ still calls HwInterrupt).
// Runs only from the timer, HwStartIo or HwAdapterState, which the SCSIport
// contract never runs concurrently with HwInterrupt. On 9x (one CPU) an
// interrupt can only preempt this code, and the gate is closed first with a
// locked exchange, so an ISR that runs after it returns without touching the
// queues; the pointers are cleared only afterwards.
static VOID Q9DetachQueues(PHW_DEVICE_EXTENSION DevExt, const char *Why)
{
    ULONG was = Q9Xchg(&DevExt->Q9QueuesReady, 0);

    if (!was && DevExt->UncachedExtensionBase == NULL) {
        return;
    }
    if (Q9Csts(DevExt) != 0xFFFFFFFF) {
        NvmeWriteReg32(DevExt, NVME_REG_INTMS, 0xFFFFFFFF);
    }
    DevExt->InitComplete = FALSE;
    DevExt->AdminQueue.SubmissionQueue = NULL;
    DevExt->AdminQueue.CompletionQueue = NULL;
    DevExt->IoQueue.SubmissionQueue = NULL;
    DevExt->IoQueue.CompletionQueue = NULL;
    DevExt->UtilityBuffer = NULL;
    DevExt->PrpListPages = NULL;
    DevExt->UncachedExtensionBase = NULL;
    DevExt->Q9Bounce = NULL;
    DevExt->Q9BouncePhys = 0;
    DevExt->Q9BounceBusy = TRUE;                    // no bounce use either
    DevExt->Q9PoolLinear = 0;
    DevExt->Q9PoolPhys = 0;
    G9Log("q9 queues detached (%s): gate closed (was %u), queue and buffer pointers cleared", Why, was);
}

// Deferred work: Tries 1 from the timer and HwStartIo.
static VOID Q9PoolWork(PHW_DEVICE_EXTENSION DevExt, ULONG Tries)
{
    PQ9_ACCT a = gQ9Acct;
    PQ9_REC r;
    ULONG i;

    if (DevExt->Q9PoolPending == 0 || a == NULL) {
        DevExt->Q9PoolPending = 0;
        return;
    }
    if (!Q9Lock(DevExt, Tries)) {
        G9Log("q9 pool work %X deferred", DevExt->Q9PoolPending);
        return;
    }
    if (DevExt->Q9PoolPending & Q9P_RELEASE) {
        r = Q9MyRecord(DevExt);
        if (r != NULL && r->State == Q9PL_ISOLATED) {
            Q9RecDisown((ULONG)DevExt->Q9PoolIndex, r, Q9PL_RELEASED, "released: isolated and given up");
            if (DevExt->Q9Opt.CorruptRec) {
                r->Linear += 2;                     // test: the next claimer must refuse it
                G9Log("q9 pool[%d]: CorruptRec test: Linear now %08X", DevExt->Q9PoolIndex, r->Linear);
            }
            DevExt->Q9PoolIndex = -1;
        }
    }
    if (DevExt->Q9PoolPending & Q9P_E2) {
        for (i = 0; i < Q9_RECS; i++) {
            if (!(DevExt->Q9E2Mask & (1UL << i))) {
                continue;
            }
            r = &a->Rec[i];
            if (r->State == Q9PL_QUARANTINED && r->Bus == DevExt->BusNumber && r->Slot == DevExt->SlotNumber) {
                Q9RecDisown(i, r, Q9PL_RELEASED, "E2: this load's acknowledged disable of the same function");
            }
        }
        DevExt->Q9E2Mask = 0;
    }
    DevExt->Q9PoolPending = 0;
    Q9Unlock(DevExt);
}

// This load's controller acknowledged a disable (evidence for its own block
// and, E2, for quarantined blocks of the same function).
static VOID Q9OwnDisableAcked(PHW_DEVICE_EXTENSION DevExt, ULONG Tries)
{
    DevExt->Q9Isolated = TRUE;
    Q9PoolMine(DevExt, Q9PL_OWNED, Q9PL_ISOLATED, "acknowledged disable");
    if (DevExt->Q9E2Mask) {
        DevExt->Q9PoolPending |= Q9P_E2;
    }
    Q9PoolWork(DevExt, Tries);
}

// Terminal: the instance will never touch its block again.
static VOID Q9PoolGiveUp(PHW_DEVICE_EXTENSION DevExt, ULONG Tries)
{
    Q9DetachQueues(DevExt, "terminal");             // before the release can be published
    if (DevExt->Q9Opt.CorruptAcct == 1 && gQ9Acct != NULL) {
        gQ9Acct->Signature ^= 1;                    // test: this boot's accounting is now corrupt
        G9Log("q9 CorruptAcct test: boot block signature damaged");
    } else if (DevExt->Q9Opt.CorruptAcct == 4) {
        ULONG hk = 0, junk[2] = { 0, 0 };

        // test: the reference is gone and the boot marker is malformed (REG_BINARY, 8 bytes)
        if (Q9RegCreateKey(Q9_HKLM, "Software\\stor98\\Q9x", &hk) == 0) {
            Q9RegDeleteValue(hk, Q9_ACCT_REF_VALUE);
            Q9RegSetValueEx(hk, Q9_ACCT_BOOT_VALUE, 0, Q9_REG_BINARY, junk, sizeof(junk));
            G9Log("q9 CorruptAcct test 4: reference deleted, boot marker malformed");
            Q9RegCloseKey(hk);
        }
    } else if (DevExt->Q9Opt.CorruptAcct == 2 || DevExt->Q9Opt.CorruptAcct == 3) {
        ULONG hk = 0, type = 0, cb;
        Q9_ACCT_REF ref;

        // test: 2 = the reference becomes unreadable as one (wrong size);
        // 3 = its BootCookie is damaged (the check word no longer matches)
        if (Q9RegCreateKey(Q9_HKLM, "Software\\stor98\\Q9x", &hk) == 0) {
            cb = sizeof(ref);
            if (Q9RegQueryValueEx(hk, Q9_ACCT_REF_VALUE, NULL, &type, &ref, &cb) == 0 && cb == sizeof(ref)) {
                if (DevExt->Q9Opt.CorruptAcct == 3) {
                    ref.BootCookie ^= 0x00010000;
                }
                Q9RegSetValueEx(hk, Q9_ACCT_REF_VALUE, 0, Q9_REG_BINARY, &ref,
                                DevExt->Q9Opt.CorruptAcct == 2 ? sizeof(ref) - 4 : sizeof(ref));
                G9Log("q9 CorruptAcct test %u: reference value %s", DevExt->Q9Opt.CorruptAcct,
                      DevExt->Q9Opt.CorruptAcct == 2 ? "truncated" : "cookie damaged");
            }
            Q9RegCloseKey(hk);
        }
    }
    DevExt->Q9PoolPending |= Q9P_RELEASE;
    Q9PoolWork(DevExt, Tries);
}

// Record fields, residency, provenance, then the metadata page.
static BOOLEAN Q9PoolValidate(PQ9_REC r)
{
    PQ9_POOL_META m;
    ULONG bytes = Q9_POOL_PAGES * PAGE_SIZE;
    BOOLEAN ok;

    if (r->Pages != Q9_POOL_PAGES || (r->Linear & (PAGE_SIZE - 1)) || r->Handle != r->Linear ||
        !Q9InArena(r->Linear, bytes) || r->Physical == 0 || (r->Physical & (PAGE_SIZE - 1)) ||
        r->Physical > 0xFFFFFFFFUL - bytes + 1) {
        G9Log("q9 pool: record fields invalid (pages %u linear %08X handle %08X phys %08X)", r->Pages, r->Linear,
              r->Handle, r->Physical);
        return FALSE;
    }
    if (!Q9PagesPresent(r->Linear, bytes, r->Physical)) {
        return FALSE;
    }
    m = (PQ9_POOL_META)(r->Linear + Q9_POOL_META_OFFSET);
    ok = (BOOLEAN)(m->Signature == Q9_POOL_META_SIG && m->Physical == r->Physical && m->Linear == r->Linear);
    G9Stage(Q9S_POOL_CHECK, ok | (m->Loads << 8));
    G9Log("q9 pool: reuse check sig=%08X phys=%08X/%08X linear=%08X/%08X loads=%u last inst=%u token=%u bus=%u slot=%u gen=%u -> %s",
          m->Signature, m->Physical, r->Physical, m->Linear, r->Linear, m->Loads, m->LastInstance, m->LastToken,
          m->LastBus, m->LastSlot, m->LastGen, ok ? "intact" : "MISMATCH");
    return ok;
}

static BOOLEAN Q9PoolAllocate(PULONG Linear, PULONG Phys)
{
    ULONGLONG r;
    ULONG phys = 0;

    r = Q9PageAllocate(Q9_POOL_PAGES, Q9_PG_SYS, 0, 0, 0, 0xFFFFF, &phys,
                       Q9_PAGEFIXED | Q9_PAGEUSEALIGN | Q9_PAGECONTIG | Q9_PAGEZEROINIT);
    G9Log("q9 pool: _PageAllocate(%u pages, PG_SYS, fixed|align|contig|zero, max phys page FFFFF) -> eax=%08X edx=%08X phys=%08X",
          Q9_POOL_PAGES, (ULONG)r, (ULONG)(r >> 32), phys);
    if ((ULONG)r == 0 || phys == 0 || (phys & (PAGE_SIZE - 1))) {
        return FALSE;
    }
    if (!Q9PagesPresent((ULONG)r, Q9_POOL_PAGES * PAGE_SIZE, phys)) {
        Q9PageFree((ULONG)r, 0);
        return FALSE;
    }
    *Linear = (ULONG)r;
    *Phys = phys;
    return TRUE;
}

static VOID Q9PoolMetaWrite(PHW_DEVICE_EXTENSION DevExt)
{
    PQ9_POOL_META m = (PQ9_POOL_META)(DevExt->Q9PoolLinear + Q9_POOL_META_OFFSET);

    m->Signature = Q9_POOL_META_SIG;
    m->Physical = DevExt->Q9PoolPhys;
    m->Linear = DevExt->Q9PoolLinear;
    m->Loads++;
    m->LastInstance = DevExt->G9Instance;
    m->LastToken = DevExt->Q9Token;
    m->LastBus = DevExt->BusNumber;
    m->LastSlot = DevExt->SlotNumber;
    m->LastGen = DevExt->Q9Gen;
}

// Fills a record this load holds RESERVED and publishes it OWNED.
static VOID Q9PoolTake(PHW_DEVICE_EXTENSION DevExt, LONG Index, ULONG Linear, ULONG Phys, BOOLEAN Fresh,
                       BOOLEAN HasFunc)
{
    PQ9_REC r = &gQ9Acct->Rec[Index];

    if (Fresh) {
        r->Handle = Linear;
        r->Linear = Linear;
        r->Physical = Phys;
        r->Pages = Q9_POOL_PAGES;
        r->Loads = 0;
    }
    r->Flags = HasFunc ? 0 : Q9RF_NOFUNC;
    r->Bus = DevExt->BusNumber;
    r->Slot = DevExt->SlotNumber;
    r->Gen = DevExt->Q9Entry ? DevExt->Q9Gen : 0;
    r->TableCookie = DevExt->Q9Entry ? DevExt->Q9TableCookie : 0;
    r->Loads++;
    Q9Xchg(&r->State, Q9PL_OWNED);
    DevExt->Q9PoolIndex = Index;
    DevExt->Q9PoolLinear = r->Linear;
    DevExt->Q9PoolPhys = r->Physical;
    Q9RecLog((ULONG)Index, r, Q9PL_RESERVED, Fresh ? "new block" : "reused RELEASED block");
}

// Takes a token and binds the function (under the lock). FALSE: no token.
static BOOLEAN Q9TakeToken(PHW_DEVICE_EXTENSION DevExt, PQ9_ACCT a, PBOOLEAN HasFunc)
{
    Q9_FUNC *f;
    ULONG i, prev;

    if (a->NextToken >= 0xFFFFFFFE) {
        return FALSE;
    }
    DevExt->Q9Token = ++a->NextToken;
    f = Q9FindFunc(a, DevExt->BusNumber, DevExt->SlotNumber);
    for (i = 0; f == NULL && i < Q9_FUNCS; i++) {
        if (a->Func[i].Bus == 0xFFFFFFFF || !a->Func[i].Live) {
            f = &a->Func[i];
        }
    }
    *HasFunc = (BOOLEAN)(f != NULL);
    if (f == NULL) {
        G9Log("q9 token %u: no function slot free: owner liveness unknown for this load", DevExt->Q9Token);
        return TRUE;
    }
    prev = f->Token;
    f->Bus = DevExt->BusNumber;
    f->Slot = DevExt->SlotNumber;
    f->Token = DevExt->Q9Token;
    f->Live = 1;
    G9Log("q9 token %u for bus %u slot %u (replaces token %u: a new HwFindAdapter means it is gone)",
          DevExt->Q9Token, DevExt->BusNumber, DevExt->SlotNumber, prev);
    return TRUE;
}

// Chooses or allocates this load's block. Returns FALSE to fail
// HwFindAdapter (before CC.EN is ever written).
static BOOLEAN Q9PoolSetup(PHW_DEVICE_EXTENSION DevExt)
{
    PQ9_REC r;
    PQ9_ACCT a;
    ULONG i, st, linear, phys, how = 0, pass;
    LONG pick, freeSlot;
    BOOLEAN hasFunc = FALSE;

    DevExt->Q9PoolIndex = -1;
    DevExt->Q9E2Mask = 0;
    a = Q9AcctGet(DevExt->Q9ProofCookie);
    if (a == NULL) {
        Q9Mark(Q9S_FAILED, 0x12);
        return FALSE;
    }
    if (!Q9Lock(DevExt, 10)) {
        Q9Mark(Q9S_FAILED, 0x13);
        return FALSE;
    }
    if (!Q9TakeToken(DevExt, a, &hasFunc)) {
        Q9Unlock(DevExt);
        G9Log("q9 tokens exhausted: fail closed");
        return FALSE;
    }
    Q9Unlock(DevExt);
    if (!DevExt->Q9Opt.Pool) {
        return TRUE;
    }
    for (pass = 0; pass < 3; pass++) {
        pick = -1;
        freeSlot = -1;
        if (!Q9Lock(DevExt, 10)) {
            Q9Mark(Q9S_FAILED, 0x13);
            return FALSE;
        }
        for (i = 0; i < Q9_RECS; i++) {
            r = &a->Rec[i];
            st = Q9Load(&r->State);
            if (pass == 0) {
                G9Log("q9 pool[%u]: state=%u owner=%u flags=%X bus=%u slot=%u gen=%u cookie=%08X linear=%08X phys=%08X pages=%u loads=%u",
                      i, st, r->Owner, r->Flags, r->Bus, r->Slot, r->Gen, r->TableCookie, r->Linear, r->Physical,
                      r->Pages, r->Loads);
            }
            if (st == Q9PL_FREE) {
                if (freeSlot < 0) {
                    freeSlot = (LONG)i;
                }
                continue;
            }
            if (st == Q9PL_OWNED && !Q9OwnerAlive(a, r)) {
                Q9RecDisown(i, r, Q9PL_QUARANTINED, "owner gone, no isolation evidence");
            } else if (st == Q9PL_ISOLATED && !Q9OwnerAlive(a, r)) {
                Q9RecDisown(i, r, Q9PL_RELEASED, "owner gone after isolation");
            }
            if (r->State == Q9PL_QUARANTINED) {
                if (Q9EvidenceE1(DevExt, r)) {
                    Q9RecDisown(i, r, Q9PL_RELEASED, "E1: DOWN_QUIESCED or a later generation on its entry");
                } else if (r->Bus == DevExt->BusNumber && r->Slot == DevExt->SlotNumber) {
                    DevExt->Q9E2Mask |= 1UL << i;
                    G9Log("q9 pool[%u]: same function: QUARANTINED until this load's acknowledged disable", i);
                }
            }
            if (r->State == Q9PL_RELEASED && r->Pages == Q9_POOL_PAGES && pick < 0) {
                pick = (LONG)i;
            }
        }
        if (pick >= 0) {
            r = &a->Rec[pick];
            r->Owner = DevExt->Q9Token;
            Q9Xchg(&r->State, Q9PL_RESERVED);
            Q9Unlock(DevExt);
            if (Q9PoolValidate(r)) {
                Q9PoolTake(DevExt, pick, 0, 0, FALSE, hasFunc);
                how = 2;
                goto done;
            }
            // Corrupt: left RESERVED by this token: never claimed, freed or
            // dereferenced again. Scan again.
            G9Log("q9 pool[%d]: failed validation: taken out of circulation", pick);
            continue;
        }
        if (freeSlot < 0) {
            Q9Unlock(DevExt);
            G9Log("q9 pool: no free record and nothing to reuse: HwFindAdapter fails before CC.EN");
            Q9Mark(Q9S_FAILED, 0x14);
            return FALSE;
        }
        if (!Q9AcctCharge(a, Q9_POOL_PAGES, DevExt->Q9Opt.CapPages)) {
            Q9Unlock(DevExt);
            Q9Mark(Q9S_FAILED, 0x10);
            return FALSE;
        }
        r = &a->Rec[freeSlot];
        r->Owner = DevExt->Q9Token;
        r->LastOwner = 0;
        r->Handle = r->Linear = r->Physical = r->Pages = 0;
        Q9Xchg(&r->State, Q9PL_RESERVED);
        Q9Unlock(DevExt);
        if (!Q9PoolAllocate(&linear, &phys)) {
            if (Q9Lock(DevExt, 10)) {
                r->Owner = 0;
                Q9Xchg(&r->State, Q9PL_FREE);
                Q9Unlock(DevExt);
            }
            Q9AcctUncharge(a, Q9_POOL_PAGES);
            return FALSE;
        }
        Q9PoolTake(DevExt, freeSlot, linear, phys, TRUE, hasFunc);
        how = 1;
        goto done;
    }
    G9Log("q9 pool: no usable block after 3 passes: HwFindAdapter fails before CC.EN");
    return FALSE;
done:
    DevExt->Q9PoolPages = Q9_POOL_PAGES;
    DevExt->Q9PoolHow = how;
    Q9PoolMetaWrite(DevExt);
    // The DMA area is zeroed only after this load's own acknowledged disable
    // (Q9CarveQueues), never here.
    DevExt->Q9Bounce = (PUCHAR)DevExt->Q9PoolLinear + Q9_POOL_DMA_PAGES * PAGE_SIZE;
    DevExt->Q9BouncePhys = DevExt->Q9PoolPhys + Q9_POOL_DMA_PAGES * PAGE_SIZE;
    Q9Mark(Q9S_POOL, (ULONG)(DevExt->Q9PoolIndex & 0xFF) | (how << 8) | (Q9_POOL_PAGES << 16));
    G9Log("q9 pool: %s block, record %d, linear=%08X phys=%08X pages=%u bounce=%08X/%08X charged=%u",
          how == 1 ? "new" : "reused RELEASED", DevExt->Q9PoolIndex, DevExt->Q9PoolLinear, DevExt->Q9PoolPhys,
          Q9_POOL_PAGES, (ULONG)DevExt->Q9Bounce, DevExt->Q9BouncePhys, a->ChargedPages);
    return TRUE;
}

// HwFoundAdapter: use the pool instead of the port's uncached extension.
BOOLEAN Q9PoolInstall(PHW_DEVICE_EXTENSION DevExt)
{
    if (!DevExt->Q9Opt.Pool || DevExt->Q9PoolLinear == 0) {
        return FALSE;
    }
    DevExt->UncachedExtensionBase = (PVOID)DevExt->Q9PoolLinear;
    DevExt->UncachedExtensionPhys.QuadPart = DevExt->Q9PoolPhys;
    DevExt->UncachedExtensionSize = Q9_POOL_DMA_PAGES * PAGE_SIZE;
    DevExt->UncachedExtensionOffset = 0;
    // Same cache treatment as the base gives the uncached extension.
    if (_PageModifyPermissions(DevExt->Q9PoolLinear >> PAGE_SHIFT, Q9_POOL_DMA_PAGES, 0xFFFFFFFF,
                               PC_CACHEWT | PC_CACHEDIS) == 0xFFFFFFFF) {
        ApplyCacheMode((PUCHAR)DevExt->Q9PoolLinear, Q9_POOL_DMA_PAGES, PTE_CACHE_UC);
    }
    G9Log("q9 pool installed as DMA memory: no ScsiPortGetUncachedExtension");
    return TRUE;
}

// The function's instance has stopped (its records' owner is gone).
static VOID Q9EndLife(PHW_DEVICE_EXTENSION DevExt, const char *Why)
{
    PQ9_ACCT a = gQ9Acct;
    Q9_FUNC *f;

    DevExt->Q9IdentityLost = TRUE;
    if (a == NULL || DevExt->Q9Token == 0) {
        return;
    }
    f = Q9FindFunc(a, DevExt->BusNumber, DevExt->SlotNumber);
    if (f != NULL && Q9CmpXchg(&f->Token, DevExt->Q9Token, DevExt->Q9Token) == DevExt->Q9Token) {
        Q9Xchg(&f->Live, 0);
        G9Log("q9 token %u ends (%s)", DevExt->Q9Token, Why);
    }
}

ULONG Q9FindAdapter(PHW_DEVICE_EXTENSION DevExt, PPORT_CONFIGURATION_INFORMATION ConfigInfo)
{
    ULONG rc, prc;

    DevExt->Q9State = 0;
    DevExt->Q9HeldSrb = NULL;
    DevExt->Q9Token = 0;
    DevExt->Q9BindSeq = 0;
    DevExt->Q9TableCookie = 0;
    DevExt->Q9IdentityLost = FALSE;
    DevExt->Q9BadSnapshots = 0;
    DevExt->Q9RmReq = 0;
    DevExt->Q9RmAccepted = FALSE;
    DevExt->Q9RmAcked = FALSE;
    DevExt->Q9Terminal = FALSE;
    DevExt->Q9Isolated = FALSE;
    DevExt->Q9PoolPending = 0;
    DevExt->Q9Refused = 0;
    DevExt->Q9LinkDownGen = 0;
    DevExt->Q9PoolLinear = 0;
    DevExt->Q9QueuesReady = 0;
    DevExt->Q9IsrClosed = 0;
    DevExt->Q9ProofCookie = 0;
    DevExt->FallbackTimerNeeded = FALSE;
    DevExt->FallbackTimerActive = FALSE;
    Q9ReadOptions(DevExt);
    // The proof first: it is also the boot block's current-boot authority.
    prc = Q9ReadProof(DevExt);
    if (DevExt->Q9Opt.Pool && Q9AcctGet(DevExt->Q9ProofCookie) == NULL) {
        Q9Mark(Q9S_FAILED, 0x12);
        return SP_RETURN_ERROR;
    }
    rc = Q9Rendezvous(DevExt, prc);
    DevExt->Q9RendezvousRc = rc;
    Q9Mark(Q9S_RENDEZVOUS, rc | ((DevExt->Q9Entry != NULL) << 8) | ((DevExt->Q9Gen & 0xFFFF) << 16));
    if (!Q9PoolSetup(DevExt)) {
        Q9EndLife(DevExt, "HwFindAdapter failed");
        return SP_RETURN_ERROR;
    }
    UNREFERENCED_PARAMETER(ConfigInfo);
    return SP_RETURN_FOUND;
}

// ---------------------------------------------------------------------------
// Bring-up (deferred, G1 on 9x)
// ---------------------------------------------------------------------------

static VOID Q9DispatchHeld(PHW_DEVICE_EXTENSION DevExt);
static VOID Q9FailHeld(PHW_DEVICE_EXTENSION DevExt, UCHAR Status);

static VOID Q9BringStep(PHW_DEVICE_EXTENSION DevExt, ULONG Step)
{
    ULONG now = Q9Now();

    G9Stage(Q9S_BRINGUP, Step | ((now - DevExt->Q9BringStartMs) << 8));
    G9Log("q9 bring-up step %u at +%u ms CSTS=%08X CC=%08X", Step, now - DevExt->Q9BringStartMs,
          Q9Csts(DevExt), NvmeReadReg32(DevExt, NVME_REG_CC));
    DevExt->Q9BringStep = Step;
    DevExt->Q9BringStepMs = now;
}

static VOID Q9EnterFailed(PHW_DEVICE_EXTENSION DevExt, ULONG Reason)
{
    Q9Mark(Q9S_FAILED, Reason);
    Q9SetState(DevExt, Q9_FAILED, "failed");
    Q9FailHeld(DevExt, SRB_STATUS_NO_DEVICE);
}

// Starts (or restarts) bring-up; the timer and HwStartIo carry it. Never
// called once the instance is terminal (R9d).
static VOID Q9StartBringup(PHW_DEVICE_EXTENSION DevExt, const char *Why)
{
    ULONG cc, csts, to;

    DevExt->Q9BringStartMs = Q9Now();
    Q9SetState(DevExt, Q9_STARTING, Why);
    NvmeWriteReg32(DevExt, NVME_REG_INTMS, 0xFFFFFFFF);
    DevExt->ControllerCapabilities = NvmeReadReg64(DevExt, NVME_REG_CAP);
    DevExt->Version = NvmeReadReg32(DevExt, NVME_REG_VS);
    DevExt->MaxQueueEntries = (USHORT)((DevExt->ControllerCapabilities & NVME_CAP_MQES_MASK) + 1);
    DevExt->DoorbellStride = 4 << (((ULONG)(DevExt->ControllerCapabilities >> 32) & 0xF));
    DevExt->PageSize = PAGE_SIZE;
    to = (ULONG)((DevExt->ControllerCapabilities >> 24) & 0xFF) * 500;
    if (to == 0) {
        to = 500;
    }
    DevExt->Q9CapToMs = to;
    csts = Q9Csts(DevExt);
    cc = NvmeReadReg32(DevExt, NVME_REG_CC);
    G9Log("q9 bring-up (%s): CAP=%08X%08X TO=%u ms CC=%08X CSTS=%08X", Why,
          (ULONG)(DevExt->ControllerCapabilities >> 32), (ULONG)DevExt->ControllerCapabilities, to, cc, csts);
    if (csts == 0xFFFFFFFF) {
        Q9EnterFailed(DevExt, 0x20);
        return;
    }
    DevExt->InitComplete = FALSE;
    if ((cc & NVME_CC_ENABLE) || (csts & NVME_CSTS_RDY)) {
        cc &= ~(NVME_CC_ENABLE | NVME_CC_SHN_MASK);
        NvmeWriteReg32(DevExt, NVME_REG_CC, cc);
        Q9BringStep(DevExt, Q9B_DISABLE_WAIT);
    } else {
        // Found disabled with RDY=0 (not all-ones): already isolated.
        Q9OwnDisableAcked(DevExt, 1);
        Q9BringStep(DevExt, Q9B_PROGRAM);
    }
}

static BOOLEAN Q9CarveQueues(PHW_DEVICE_EXTENSION DevExt)
{
    USHORT queueSize = NVME_MAX_QUEUE_SIZE;

    if (queueSize > DevExt->MaxQueueEntries) {
        queueSize = DevExt->MaxQueueEntries;
    }
    // Only now (after this load's acknowledged disable) is the DMA area
    // zeroed and programmed (R10).
    if (DevExt->Q9PoolLinear != 0) {
        RtlZeroMemory((PVOID)DevExt->Q9PoolLinear, Q9_POOL_DMA_PAGES * PAGE_SIZE);
    }
    // Same layout every time (re-bring-up reuses the same memory).
    DevExt->UncachedExtensionOffset = 0;
    DevExt->AdminQueue.QueueSize = queueSize;
    DevExt->AdminQueue.QueueId = 0;
    DevExt->IoQueue.QueueSize = queueSize;
    DevExt->IoQueue.QueueId = 1;
    if (!AllocateUncachedMemory(DevExt, queueSize * NVME_SQ_ENTRY_SIZE, PAGE_SIZE,
                                &DevExt->AdminQueue.SubmissionQueue, &DevExt->AdminQueue.SubmissionQueuePhys) ||
        !AllocateUncachedMemory(DevExt, queueSize * NVME_SQ_ENTRY_SIZE, PAGE_SIZE,
                                &DevExt->IoQueue.SubmissionQueue, &DevExt->IoQueue.SubmissionQueuePhys) ||
        !AllocateUncachedMemory(DevExt, SG_LIST_PAGES * PAGE_SIZE, PAGE_SIZE,
                                &DevExt->UtilityBuffer, &DevExt->UtilityBufferPhys) ||
        !AllocateUncachedMemory(DevExt, queueSize * NVME_CQ_ENTRY_SIZE, PAGE_SIZE,
                                &DevExt->AdminQueue.CompletionQueue, &DevExt->AdminQueue.CompletionQueuePhys) ||
        !AllocateUncachedMemory(DevExt, queueSize * NVME_CQ_ENTRY_SIZE, PAGE_SIZE,
                                &DevExt->IoQueue.CompletionQueue, &DevExt->IoQueue.CompletionQueuePhys)) {
        return FALSE;
    }
    DevExt->PrpListPages = DevExt->UtilityBuffer;
    DevExt->PrpListPagesPhys = DevExt->UtilityBufferPhys;
    DevExt->PrpListPageBitmap = 0;
    DevExt->AdminQueue.QueueSizeBits = (UCHAR)log2(queueSize);
    DevExt->AdminQueue.QueueSizeMask = queueSize - 1;
    DevExt->IoQueue.QueueSizeBits = (UCHAR)log2(queueSize);
    DevExt->IoQueue.QueueSizeMask = queueSize - 1;
    DevExt->AdminQueue.SubmissionQueueHead = 0;
    DevExt->AdminQueue.SubmissionQueueTail = 0;
    DevExt->AdminQueue.CompletionQueueHead = queueSize;
    DevExt->AdminQueue.CompletionQueueTail = 0;
    DevExt->IoQueue.SubmissionQueueHead = 0;
    DevExt->IoQueue.SubmissionQueueTail = 0;
    DevExt->IoQueue.CompletionQueueHead = queueSize;
    DevExt->IoQueue.CompletionQueueTail = 0;
    RtlZeroMemory(DevExt->AdminQueue.SubmissionQueue, queueSize * NVME_SQ_ENTRY_SIZE);
    RtlZeroMemory(DevExt->AdminQueue.CompletionQueue, queueSize * NVME_CQ_ENTRY_SIZE);
    RtlZeroMemory(DevExt->IoQueue.SubmissionQueue, queueSize * NVME_SQ_ENTRY_SIZE);
    RtlZeroMemory(DevExt->IoQueue.CompletionQueue, queueSize * NVME_CQ_ENTRY_SIZE);
    RtlZeroMemory(DevExt->UtilityBuffer, PAGE_SIZE);
    DevExt->CurrentQueueDepth = 0;
    DevExt->CurrentAdminQueueDepth = 0;
    DevExt->NonTaggedInFlight = NULL;
    DevExt->NextNonTaggedId = 0;
    DevExt->Q9BounceBusy = FALSE;
    Q9Xchg(&DevExt->Q9QueuesReady, 1);
    return TRUE;
}

static VOID Q9SubmitFeature(PHW_DEVICE_EXTENSION DevExt, BOOLEAN Set)
{
    NVME_COMMAND cmd;

    RtlZeroMemory(&cmd, sizeof(cmd));
    cmd.CDW0.Fields.Opcode = Set ? NVME_ADMIN_SET_FEATURES : NVME_ADMIN_GET_FEATURES;
    cmd.CDW0.Fields.CommandId = Set ? Q9_ADMIN_CID_SETF : Q9_ADMIN_CID_GETF;
    cmd.CDW10 = Q9_FEAT_VWC;            // Get: SEL 0 (current)
    cmd.CDW11 = 0;                      // Set: WCE = 0
    DevExt->Q9FeatDone = FALSE;
    NvmeSubmitAdminCommand(DevExt, &cmd);
}

static VOID Q9BringReady(PHW_DEVICE_EXTENSION DevExt)
{
    ULONG ms = Q9Now() - DevExt->Q9BringStartMs;

    NvmeWriteReg32(DevExt, NVME_REG_INTMC, 0x00000001);
    Q9Mark(Q9S_READY, ms);
    G9Log("q9 bring-up complete in %u ms: blocks=%u bs=%u model=%.40s", ms, (ULONG)DevExt->NamespaceSizeInBlocks,
          DevExt->NamespaceBlockSize, DevExt->ControllerModelNumber);
    Q9SetState(DevExt, Q9_RUNNING, "ready");
    Q9DispatchHeld(DevExt);
}

// Advances bring-up; called from the timer and HwStartIo.
static VOID Q9BringAdvance(PHW_DEVICE_EXTENSION DevExt)
{
    ULONG now = Q9Now();
    ULONG csts = Q9Csts(DevExt);
    ULONG limit;

    if (csts == 0xFFFFFFFF && DevExt->Q9BringStep != Q9B_FAIL_DISABLE) {
        // Presence lost during bring-up: nothing was issued for an SRB.
        Q9EnterFailed(DevExt, 0x21);
        return;
    }
    switch (DevExt->Q9BringStep) {
    case Q9B_DISABLE_WAIT:
        if (!(csts & NVME_CSTS_RDY)) {
            G9Log("q9 bring-up: disable acknowledged after %u ms", now - DevExt->Q9BringStepMs);
            Q9OwnDisableAcked(DevExt, 1);
            Q9BringStep(DevExt, Q9B_PROGRAM);
        } else if (now - DevExt->Q9BringStepMs > DevExt->Q9CapToMs) {
            G9Log("q9 bring-up: RDY did not clear within CAP.TO; memory stays owned");
            Q9EnterFailed(DevExt, 0x22);
            return;
        } else {
            return;
        }
        // fall through
    case Q9B_PROGRAM:
        if (!Q9CarveQueues(DevExt)) {
            Q9EnterFailed(DevExt, 0x23);
            return;
        }
        NvmeWriteReg32(DevExt, NVME_REG_AQA,
                       ((DevExt->AdminQueue.QueueSize - 1) << 16) | (DevExt->AdminQueue.QueueSize - 1));
        NvmeWriteReg64(DevExt, NVME_REG_ASQ, DevExt->AdminQueue.SubmissionQueuePhys.QuadPart);
        NvmeWriteReg64(DevExt, NVME_REG_ACQ, DevExt->AdminQueue.CompletionQueuePhys.QuadPart);
        // The block becomes reachable again (R10: ISOLATED -> OWNED first).
        Q9PoolMine(DevExt, Q9PL_ISOLATED, Q9PL_OWNED, "re-enabling the controller");
        DevExt->Q9Isolated = FALSE;
        NvmeWriteReg32(DevExt, NVME_REG_CC, NVME_CC_ENABLE | NVME_CC_CSS_NVM | NVME_CC_AMS_RR |
                       NVME_CC_SHN_NONE | NVME_CC_IOSQES | NVME_CC_IOCQES);
        Q9BringStep(DevExt, Q9B_ENABLE_WAIT);
        return;
    case Q9B_ENABLE_WAIT:
        limit = DevExt->Q9Opt.BringupMs ? DevExt->Q9Opt.BringupMs : DevExt->Q9CapToMs;
        if (csts & NVME_CSTS_CFS) {
            G9Log("q9 bring-up: CSTS.CFS");
            NvmeWriteReg32(DevExt, NVME_REG_CC, 0);
            Q9BringStep(DevExt, Q9B_FAIL_DISABLE);
            return;
        }
        if (csts & NVME_CSTS_RDY) {
            DevExt->InitComplete = FALSE;
            DevExt->NamespaceSizeInBlocks = 0;
            Q9BringStep(DevExt, Q9B_ADMIN);
            NvmeCreateIoCQ(DevExt);
            return;
        }
        if (now - DevExt->Q9BringStepMs > limit) {
            G9Log("q9 bring-up: RDY not set within %u ms; disabling", limit);
            NvmeWriteReg32(DevExt, NVME_REG_CC, 0);
            Q9BringStep(DevExt, Q9B_FAIL_DISABLE);
        }
        return;
    case Q9B_ADMIN:
        NvmeProcessAdminCompletion(DevExt);
        if (DevExt->InitComplete) {
            if (DevExt->Q9Opt.WriteThrough & 2) {
                Q9BringStep(DevExt, Q9B_VWC_SET);
                Q9SubmitFeature(DevExt, TRUE);
                return;
            }
            Q9BringReady(DevExt);
            return;
        }
        if (now - DevExt->Q9BringStepMs > 5000) {
            G9Log("q9 bring-up: admin init (queues/identify) not complete in 5 s; disabling");
            NvmeWriteReg32(DevExt, NVME_REG_CC, 0);
            Q9BringStep(DevExt, Q9B_FAIL_DISABLE);
        }
        return;
    case Q9B_VWC_SET:
    case Q9B_VWC_GET:
        NvmeProcessAdminCompletion(DevExt);
        if (DevExt->Q9State == Q9_STARTING && (DevExt->Q9BringStep == Q9B_VWC_SET || DevExt->Q9BringStep == Q9B_VWC_GET) &&
            Q9Now() - DevExt->Q9BringStepMs > 2000) {
            G9Log("q9 bring-up: Set/Get Features (VWC) timed out; continuing without verification");
            Q9BringReady(DevExt);
        }
        return;
    case Q9B_FAIL_DISABLE:
        if (csts != 0xFFFFFFFF && !(csts & NVME_CSTS_RDY)) {
            G9Log("q9 bring-up failed; disable acknowledged (block isolated, still owned)");
            Q9OwnDisableAcked(DevExt, 1);
            Q9EnterFailed(DevExt, 0x24);
        } else if (now - DevExt->Q9BringStepMs > DevExt->Q9CapToMs) {
            G9Log("q9 bring-up failed; disable not acknowledged (block stays owned)");
            Q9EnterFailed(DevExt, 0x25);
        }
        return;
    }
}

BOOLEAN Q9AdminHook(PHW_DEVICE_EXTENSION DevExt, USHORT Cid, USHORT Status, ULONG Dw0)
{
    if (Cid == Q9_ADMIN_CID_SETF) {
        G9Log("q9 Set Features VWC=0: status=%04X", Status);
        DevExt->Q9VwcSetStatus = Status;
        Q9BringStep(DevExt, Q9B_VWC_GET);
        Q9SubmitFeature(DevExt, FALSE);
        return TRUE;
    }
    if (Cid == Q9_ADMIN_CID_GETF) {
        DevExt->Q9VwcValue = Dw0;
        DevExt->Q9VwcVerified = (BOOLEAN)(Status == 0 && DevExt->Q9VwcSetStatus == 0 && (Dw0 & 1) == 0);
        Q9Mark(Q9S_WT, DevExt->Q9Opt.WriteThrough | (DevExt->Q9VwcVerified << 8) | ((Dw0 & 0xFF) << 16));
        G9Log("q9 Get Features VWC: status=%04X dw0=%08X -> volatile write cache %s", Status, Dw0,
              DevExt->Q9VwcVerified ? "OFF (verified)" : "NOT verified off");
        if (DevExt->Q9State == Q9_STARTING) {
            Q9BringReady(DevExt);
        }
        return TRUE;
    }
    return FALSE;
}

BOOLEAN Q9HwInitialize(PHW_DEVICE_EXTENSION DevExt)
{
    USHORT pciCommand;

    G9Log("q9 HwInitialize: deferred bring-up");
    // INTx enabled at PCI level here (a permitted config callback); the
    // controller keeps every vector masked until bring-up completes.
    pciCommand = ReadPciConfigWord(DevExt, PCI_COMMAND_OFFSET);
    pciCommand &= ~PCI_INTERRUPT_DISABLE;
    WritePciConfigWord(DevExt, PCI_COMMAND_OFFSET, pciCommand);
    Q9StartBringup(DevExt, "HwInitialize");
    if (DevExt->Q9State == Q9_STARTING) {
        Q9BringAdvance(DevExt);
    }
    Q9Arm(DevExt);
    return TRUE;
}

// ---------------------------------------------------------------------------
// Held SRB (bring-up hold, hybrid hold-then-BUSY)
// ---------------------------------------------------------------------------

static VOID Q9DispatchHeld(PHW_DEVICE_EXTENSION DevExt)
{
    PSCSI_REQUEST_BLOCK srb = DevExt->Q9HeldSrb;

    if (srb == NULL) {
        return;
    }
    DevExt->Q9HeldSrb = NULL;
    G9Log("q9 dispatching held SRB %08X op=%02X after %u ms", (ULONG)srb, srb->Cdb[0],
          Q9Now() - DevExt->Q9HoldStartMs);
    HwStartIo(DevExt, srb);
}

static VOID Q9FailHeld(PHW_DEVICE_EXTENSION DevExt, UCHAR Status)
{
    PSCSI_REQUEST_BLOCK srb = DevExt->Q9HeldSrb;

    if (srb == NULL) {
        return;
    }
    DevExt->Q9HeldSrb = NULL;
    srb->SrbStatus = Status;
    srb->ScsiStatus = 0;
    srb->DataTransferLength = 0;
    G9Log("q9 completing held SRB %08X op=%02X with %02X", (ULONG)srb, srb->Cdb[0], Status);
    ScsiPortNotification(RequestComplete, DevExt, srb);
    ScsiPortNotification(NextRequest, DevExt, NULL);
}

static VOID Q9Hold(PHW_DEVICE_EXTENSION DevExt, PSCSI_REQUEST_BLOCK Srb)
{
    DevExt->Q9HeldSrb = Srb;
    DevExt->Q9HoldStartMs = Q9Now();
    DevExt->Q9HoldCount++;
    G9Stage(Q9S_HOLD, DevExt->Q9HoldCount);
    G9Log("q9 hold #%u SRB %08X func=%02X op=%02X (state %s)", DevExt->Q9HoldCount, (ULONG)Srb, Srb->Function,
          Srb->Cdb[0], Q9StateName(DevExt->Q9State));
}

static VOID Q9HoldTimeout(PHW_DEVICE_EXTENSION DevExt)
{
    PSCSI_REQUEST_BLOCK srb = DevExt->Q9HeldSrb;

    if (srb == NULL || DevExt->Q9State != Q9_STARTING || DevExt->Q9Opt.HoldMs == 0) {
        return;
    }
    if (Q9Now() - DevExt->Q9HoldStartMs < DevExt->Q9Opt.HoldMs) {
        return;
    }
    DevExt->Q9BusyCount++;
    G9Stage(Q9S_BUSY, DevExt->Q9BusyCount);
    G9Log("q9 hold bound reached: SRB %08X op=%02X completed BUSY (#%u)", (ULONG)srb, srb->Cdb[0],
          DevExt->Q9BusyCount);
    Q9FailHeld(DevExt, SRB_STATUS_BUSY);
}

// ---------------------------------------------------------------------------
// Presence loss, evidence 2 and the MD-1 deadline
// ---------------------------------------------------------------------------

static VOID Q9CompleteAllHeld(PHW_DEVICE_EXTENSION DevExt, UCHAR Status, const char *Why)
{
    ULONG n = DevExt->CurrentQueueDepth + (DevExt->Q9HeldSrb ? 1 : 0);

    G9Log("q9 completing everything outstanding with %02X (%s): depth=%u nontagged=%08X held=%08X", Status,
          Why, DevExt->CurrentQueueDepth, (ULONG)DevExt->NonTaggedInFlight, (ULONG)DevExt->Q9HeldSrb);
    DevExt->Q9HeldSrb = NULL;
    ScsiPortCompleteRequest(DevExt, 0, SP_UNTAGGED, SP_UNTAGGED, Status);
    DevExt->CurrentQueueDepth = 0;
    DevExt->NonTaggedInFlight = NULL;
    DevExt->Q9BounceBusy = FALSE;
    DevExt->PrpListPageBitmap = 0;
    ScsiPortNotification(NextRequest, DevExt, NULL);
    DevExt->Q9Completed += n;
}

static VOID Q9RmWriteAck(PHW_DEVICE_EXTENSION DevExt, ULONG Outcome, ULONG Detail);

static VOID Q9EnterGone(PHW_DEVICE_EXTENSION DevExt, const char *Where)
{
    if (DevExt->Q9State == Q9_GONE || DevExt->Q9State == Q9_LINKDOWN || DevExt->Q9State == Q9_QUIESCED ||
        DevExt->Q9State == Q9_STOPPED) {
        return;                                     // already isolated, or no longer serving
    }
    DevExt->Q9GoneMs = Q9Now();
    DevExt->Q9GoneOutstanding = Q9Outstanding(DevExt);
    DevExt->Q9DeadlineTaken = FALSE;
    Q9Mark(Q9S_GONE, (DevExt->CurrentQueueDepth & 0xFF) | ((DevExt->NonTaggedInFlight != NULL) << 8) |
           ((DevExt->Q9HeldSrb != NULL) << 9) | (DevExt->Q9State << 16));
    G9Log("q9 GONE in %s (was %s): CSTS all-ones", Where, Q9StateName(DevExt->Q9State));
    Q9SetState(DevExt, Q9_GONE, Where);
}

static VOID Q9GoneDeadline(PHW_DEVICE_EXTENSION DevExt)
{
    if (DevExt->Q9State != Q9_GONE || DevExt->Q9DeadlineTaken) {
        return;
    }
    if (Q9Now() - DevExt->Q9GoneMs < DevExt->Q9Opt.GoneMs) {
        return;
    }
    DevExt->Q9DeadlineTaken = TRUE;
    Q9Mark(Q9S_DEADLINE, DevExt->CurrentQueueDepth + (DevExt->Q9HeldSrb ? 1 : 0));
    G9Log("q9 gone deadline (MD-1) after %u ms without evidence: held SRBs completed, block stays OWNED",
          Q9Now() - DevExt->Q9GoneMs);
    if (Q9Outstanding(DevExt)) {
        Q9CompleteAllHeld(DevExt, SRB_STATUS_NO_DEVICE, "MD-1 deadline");
    }
}

// R12 S3: DOWN_QUIESCED for this instance's generation or a later one, or a
// later generation on its entry (R8b). Ack: AckGeneration = Gen, last; not
// written when the evidence is a later UP (nothing waits for it then).
static VOID Q9ApplyLinkDown(PHW_DEVICE_EXTENSION DevExt, ULONG Quiesced, BOOLEAN Ack)
{
    PSTOR98LS_ENTRY e = Q9MyEntry(DevExt);
    ULONG done = 0;

    Q9Mark(Q9S_LS_SEEN, (Quiesced & 0xFF) | ((DevExt->Q9Gen & 0xFF) << 8) | ((DevExt->Q9State & 0xFF) << 16) |
           ((DevExt->BusNumber & 0xFF) << 24));
    G9Log("q9 DOWN_QUIESCED seen: quiesced tunnel %u (mine %u) in state %s, %u ms after gone", Quiesced,
          DevExt->Q9Gen, Q9StateName(DevExt->Q9State), DevExt->Q9State == Q9_GONE ? Q9Now() - DevExt->Q9GoneMs : 0);
    if (Q9Outstanding(DevExt)) {
        done = DevExt->CurrentQueueDepth + (DevExt->Q9HeldSrb ? 1 : 0);
        Q9CompleteAllHeld(DevExt, SRB_STATUS_NO_DEVICE, "evidence 2");
    }
    if (DevExt->Q9RmAccepted && !DevExt->Q9RmAcked && DevExt->Q9State == Q9_REMOVING) {
        Q9RmWriteAck(DevExt, STOR98RM_ISOLATED_NOT_CLEAN, 0x800 | DevExt->Q9RmStep);
    }
    if (DevExt->Q9State != Q9_STOPPED) {
        Q9SetState(DevExt, Q9_LINKDOWN, "evidence 2");
    }
    // The endpoint was reset: isolated; the instance never re-enables it.
    DevExt->Q9Isolated = TRUE;
    Q9PoolMine(DevExt, Q9PL_OWNED, Q9PL_ISOLATED, "evidence 2 (DOWN_QUIESCED)");
    Q9PoolGiveUp(DevExt, 1);
    DevExt->Q9LinkDownGen = Quiesced;
    if (e != NULL && Ack) {
        Q9Xchg(&e->AckGeneration, Quiesced);
    }
    Q9Mark(Q9S_LS_ACK, (Quiesced & 0xFF) | ((done & 0xFF) << 8) | (Ack << 16) | ((DevExt->BusNumber & 0xFF) << 24));
    G9Log("q9 link-down applied for generation %u: AckGeneration %s (%u requests completed)", Quiesced,
          (e != NULL && Ack) ? "written" : "not written", done);
}

// ---------------------------------------------------------------------------
// MD-5 removal transaction (and the restartable SHUTDOWN, which shares it)
// ---------------------------------------------------------------------------

// R9c: written once; never changed afterwards.
static VOID Q9RmWriteAck(PHW_DEVICE_EXTENSION DevExt, ULONG Outcome, ULONG Detail)
{
    PSTOR98LS_ENTRY e = Q9MyEntry(DevExt);

    if (DevExt->Q9RmAcked) {
        G9Log("q9 handshake: request %u already acknowledged (outcome %u); %u not written", DevExt->Q9RmReq,
              DevExt->Q9RmOutcome, Outcome);
        return;
    }
    DevExt->Q9RmAcked = TRUE;
    DevExt->Q9RmOutcome = Outcome;
    if (e != NULL) {
        // R9c: one store, sequence and outcome together.
        Q9Xchg(&e->RemoveAck, ((DevExt->Q9RmReq & STOR98LS9X_SEQ_MASK) << 8) | (Outcome & 0xFF));
    }
    Q9Mark(Q9S_RM_ACK, Outcome | (DevExt->Q9RmReq << 8));
    G9Log("q9 handshake: acknowledged request %u (token %u generation %u) outcome %u detail %08X after %u ms%s",
          DevExt->Q9RmReq, DevExt->Q9Token, DevExt->Q9Gen, Outcome, Detail, Q9Now() - DevExt->Q9RmStartMs,
          e ? "" : " (identity lost: not published)");
}

static VOID Q9RmStep(PHW_DEVICE_EXTENSION DevExt, ULONG Step)
{
    ULONG now = Q9Now();

    G9Stage(Q9S_RM_STEP, Step | ((now - DevExt->Q9RmStartMs) << 8));
    G9Log("q9 %s step %u at +%u ms CSTS=%08X CC=%08X depth=%u", DevExt->Q9RmMode == 1 ? "handshake" : "shutdown",
          Step, now - DevExt->Q9RmStartMs, Q9Csts(DevExt), NvmeReadReg32(DevExt, NVME_REG_CC),
          DevExt->CurrentQueueDepth);
    DevExt->Q9RmStep = Step;
    DevExt->Q9RmStepMs = now;
}

static VOID Q9RmBegin(PHW_DEVICE_EXTENSION DevExt, ULONG Mode, ULONG Step)
{
    ULONG cc;

    DevExt->Q9RmMode = Mode;
    if (Step == Q9R_DRAIN) {
        DevExt->Q9RmFlushOk = FALSE;
        DevExt->Q9RmFlushDone = FALSE;
        DevExt->Q9RmShstOk = FALSE;
        DevExt->Q9RmDrainOk = FALSE;
        DevExt->Q9RmFlushStatus = 0;
    }
    DevExt->Q9RmDetail = 0;
    Q9SetState(DevExt, Q9_REMOVING, Mode == 1 ? "REMOVE_PENDING" : "SRB_FUNCTION_SHUTDOWN");
    if (Step == Q9R_DISABLE) {
        // No (further) data path work possible or needed: isolate now.
        NvmeWriteReg32(DevExt, NVME_REG_INTMS, 0xFFFFFFFF);
        cc = NvmeReadReg32(DevExt, NVME_REG_CC);
        NvmeWriteReg32(DevExt, NVME_REG_CC, cc & ~(NVME_CC_ENABLE | NVME_CC_SHN_MASK));
    }
    Q9RmStep(DevExt, Step);
}

static VOID Q9SubmitInternalFlush(PHW_DEVICE_EXTENSION DevExt)
{
    NVME_COMMAND cmd;

    RtlZeroMemory(&cmd, sizeof(cmd));
    cmd.CDW0.Fields.Opcode = NVME_CMD_FLUSH;
    cmd.CDW0.Fields.CommandId = (USHORT)(Q9_INTERNAL_CID_BASE | (DevExt->Q9FlushSeq++ & 0xFF));
    cmd.NSID = 1;
    if (!NvmeSubmitIoCommand(DevExt, &cmd)) {
        G9Log("q9 internal Flush: submission failed");
        DevExt->Q9RmFlushDone = TRUE;
        DevExt->Q9RmFlushStatus = 0xFFFF;
    }
}

BOOLEAN Q9IoCompletionHook(PHW_DEVICE_EXTENSION DevExt, USHORT Cid, USHORT Status)
{
    if ((Cid & 0xFF00) != Q9_INTERNAL_CID_BASE) {
        return FALSE;
    }
    if (DevExt->CurrentQueueDepth > 0) {
        DevExt->CurrentQueueDepth--;
    }
    DevExt->Q9RmFlushDone = TRUE;
    DevExt->Q9RmFlushStatus = Status;
    G9Log("q9 internal Flush CID %04X completed status %04X after %u ms", Cid, Status,
          Q9Now() - DevExt->Q9RmStepMs);
    return TRUE;
}

static VOID Q9RmFinish(PHW_DEVICE_EXTENSION DevExt, BOOLEAN RdyAcked)
{
    ULONG outcome, detail;
    PSCSI_REQUEST_BLOCK srb;

    detail = (DevExt->Q9RmFlushOk ? 1 : 0) | (DevExt->Q9RmShstOk ? 2 : 0) | (RdyAcked ? 4 : 0) |
             (DevExt->Q9RmDrainOk ? 8 : 0) | ((DevExt->Q9RmFlushStatus & 0xFFFF) << 16);
    if (DevExt->Q9RmMode == 2) {
        // Restartable SHUTDOWN: the controller stays enabled with CC.SHN set.
        srb = DevExt->Q9RmSrb;
        DevExt->Q9RmSrb = NULL;
        Q9SetState(DevExt, Q9_SHUTDOWN, "SHUTDOWN done");
        G9Stage(Q9S_SHUTDOWN, 0x80 | detail);
        G9Log("q9 SRB_FUNCTION_SHUTDOWN done: flush %s, SHST %s, %u ms", DevExt->Q9RmFlushOk ? "ok" : "FAILED",
              DevExt->Q9RmShstOk ? "complete" : "NOT complete", Q9Now() - DevExt->Q9RmStartMs);
        if (srb != NULL) {
            srb->SrbStatus = SRB_STATUS_SUCCESS;
            ScsiPortNotification(RequestComplete, DevExt, srb);
            ScsiPortNotification(NextRequest, DevExt, NULL);
        }
        return;
    }
    if (RdyAcked) {
        outcome = (DevExt->Q9RmDrainOk && DevExt->Q9RmFlushOk && DevExt->Q9RmShstOk) ? STOR98RM_CLEAN
                                                                                    : STOR98RM_ISOLATED_NOT_CLEAN;
        if (Q9Outstanding(DevExt)) {
            Q9CompleteAllHeld(DevExt, SRB_STATUS_NO_DEVICE, "isolated by the handshake");
        }
        DevExt->Q9Isolated = TRUE;
        Q9PoolMine(DevExt, Q9PL_OWNED, Q9PL_ISOLATED, "handshake: acknowledged disable");
        Q9SetState(DevExt, Q9_QUIESCED, "handshake done");
        Q9RmWriteAck(DevExt, outcome, detail);
        Q9PoolGiveUp(DevExt, 1);    // terminal: never touches the block again
    } else {
        // FAILED: the controller may still DMA; requests stay held until
        // evidence 2 (the CM's isolation, R12 O6) or the MD-1 deadline.
        Q9SetState(DevExt, Q9_FAILED, "handshake: no acknowledged disable");
        Q9RmWriteAck(DevExt, STOR98RM_FAILED, detail);
    }
}

static VOID Q9RmAdvance(PHW_DEVICE_EXTENSION DevExt)
{
    ULONG now = Q9Now();
    ULONG csts = Q9Csts(DevExt), cc;

    if (csts == 0xFFFFFFFF) {
        if (DevExt->Q9RmMode == 1) {
            Q9RmWriteAck(DevExt, STOR98RM_FAILED, 0xFFFFFFFF);
        }
        Q9EnterGone(DevExt, "handshake");
        return;
    }
    if (DevExt->Q9RmMode == 1) {
        ULONG el = now - DevExt->Q9RmStartMs + DevExt->Q9Opt.BudgetSkewMs;  // skew: test only

        if (DevExt->Q9RmStep != Q9R_DISABLE && el >= Q9_DISABLE_BY_MS) {
            G9Log("q9 handshake: overall budget: %u ms elapsed in step %u: disabling now (not clean)", el,
                  DevExt->Q9RmStep);
            NvmeWriteReg32(DevExt, NVME_REG_INTMS, 0xFFFFFFFF);
            cc = NvmeReadReg32(DevExt, NVME_REG_CC);
            NvmeWriteReg32(DevExt, NVME_REG_CC, cc & ~NVME_CC_ENABLE);
            Q9RmStep(DevExt, Q9R_DISABLE);
            now = Q9Now();                          // the step time is newer than the entry time
        } else if (DevExt->Q9RmStep == Q9R_DISABLE && el >= Q9_ACK_BY_MS &&
                   (Q9Csts(DevExt) & NVME_CSTS_RDY)) {
            G9Log("q9 handshake: overall budget: %u ms elapsed, RDY still set: FAILED", el);
            Q9RmFinish(DevExt, FALSE);
            return;
        }
    }
    switch (DevExt->Q9RmStep) {
    case Q9R_DRAIN:
        NvmeProcessIoCompletion(DevExt);
        if (DevExt->CurrentQueueDepth || DevExt->NonTaggedInFlight) {
            if (now - DevExt->Q9RmStepMs <= Q9_DRAIN_MS) {
                return;
            }
            G9Log("q9 handshake: drain not complete in %u ms (depth %u): no Flush, shutting down anyway",
                  Q9_DRAIN_MS, DevExt->CurrentQueueDepth);
            DevExt->Q9RmDrainOk = FALSE;
        } else {
            DevExt->Q9RmDrainOk = TRUE;
            Q9RmStep(DevExt, Q9R_FLUSH);
            Q9SubmitInternalFlush(DevExt);
            return;
        }
        goto shutdown;
    case Q9R_FLUSH:
        NvmeProcessIoCompletion(DevExt);
        if (!DevExt->Q9RmFlushDone) {
            if (now - DevExt->Q9RmStepMs <= Q9_FLUSH_MS) {
                return;
            }
            G9Log("q9 handshake: Flush not complete in %u ms", Q9_FLUSH_MS);
        } else {
            DevExt->Q9RmFlushOk = (BOOLEAN)(DevExt->Q9RmFlushStatus == 0);
        }
shutdown:
        NvmeWriteReg32(DevExt, NVME_REG_INTMS, 0xFFFFFFFF);
        cc = NvmeReadReg32(DevExt, NVME_REG_CC);
        NvmeWriteReg32(DevExt, NVME_REG_CC, (cc & ~NVME_CC_SHN_MASK) | NVME_CC_SHN_NORMAL);
        Q9RmStep(DevExt, Q9R_SHN);
        now = Q9Now();                              // (no unsigned underflow on fall-through)
        // fall through: SHST may already be complete
    case Q9R_SHN:
        if ((csts & NVME_CSTS_SHST_MASK) == NVME_CSTS_SHST_COMPLETE ||
            (Q9Csts(DevExt) & NVME_CSTS_SHST_MASK) == NVME_CSTS_SHST_COMPLETE) {
            DevExt->Q9RmShstOk = TRUE;
        } else if (now - DevExt->Q9RmStepMs <= DevExt->Q9Opt.ShstMs) {
            return;
        } else {
            G9Log("q9 handshake: SHST not complete in %u ms", DevExt->Q9Opt.ShstMs);
        }
        if (DevExt->Q9RmMode == 2) {
            Q9RmFinish(DevExt, FALSE);
            return;
        }
        cc = NvmeReadReg32(DevExt, NVME_REG_CC);
        NvmeWriteReg32(DevExt, NVME_REG_CC, cc & ~NVME_CC_ENABLE);
        Q9RmStep(DevExt, Q9R_DISABLE);
        now = Q9Now();                              // the step time is newer than the entry time
        // fall through
    case Q9R_DISABLE:
        csts = Q9Csts(DevExt);
        if (csts != 0xFFFFFFFF && !(csts & NVME_CSTS_RDY)) {
            DevExt->InitComplete = FALSE;
            Q9RmFinish(DevExt, TRUE);
        } else if (now - DevExt->Q9RmStepMs > (DevExt->Q9CapToMs < Q9_DISABLE_MS ? DevExt->Q9CapToMs : Q9_DISABLE_MS)) {
            G9Log("q9 handshake: RDY did not clear within min(CAP.TO %u, %u) ms", DevExt->Q9CapToMs, Q9_DISABLE_MS);
            Q9RmFinish(DevExt, FALSE);
        }
        return;
    }
}

// R9b: the first request after binding, for this instance's generation.
static VOID Q9RmAccept(PHW_DEVICE_EXTENSION DevExt, ULONG Seq)
{
    DevExt->Q9RmAccepted = TRUE;
    DevExt->Q9RmReq = Seq;
    DevExt->Q9RmStartMs = Q9Now();
    Q9Mark(Q9S_RM_SEEN, (Seq & 0xFFFF) | ((DevExt->Q9Gen & 0xFFFF) << 16));
    G9Log("q9 REMOVE_PENDING request %u for generation %u in state %s", Seq, DevExt->Q9Gen,
          Q9StateName(DevExt->Q9State));
    if (DevExt->Q9Opt.NoAck) {
        G9Log("q9 NoAck option: request %u ignored (instance keeps running)", Seq);
        return;
    }
    DevExt->Q9Terminal = TRUE;
    switch (DevExt->Q9State) {
    case Q9_RUNNING:
        Q9RmBegin(DevExt, 1, Q9R_DRAIN);
        break;
    case Q9_SHUTDOWN:
        // SHUTDOWN already drained, flushed and shut down: disable now; its
        // flags decide CLEAN.
        Q9RmBegin(DevExt, 1, Q9R_DISABLE);
        break;
    case Q9_STARTING:
    case Q9_RESETTING:
        Q9RmBegin(DevExt, 1, Q9R_DISABLE);
        break;
    case Q9_FAILED:
        if (DevExt->Q9Isolated) {
            Q9RmWriteAck(DevExt, STOR98RM_ISOLATED_NOT_CLEAN, 0x700);
            Q9PoolGiveUp(DevExt, 1);
        } else {
            Q9RmBegin(DevExt, 1, Q9R_DISABLE);
        }
        break;
    case Q9_LINKDOWN:
        Q9RmWriteAck(DevExt, STOR98RM_ISOLATED_NOT_CLEAN, 0x500);
        break;
    default:
        Q9RmWriteAck(DevExt, STOR98RM_FAILED, 0x400 | DevExt->Q9State);
        break;
    }
}

// ---------------------------------------------------------------------------
// Link-state polling (timer and HwStartIo)
// ---------------------------------------------------------------------------

static VOID Q9LsPoll(PHW_DEVICE_EXTENSION DevExt)
{
    PSTOR98LS_ENTRY e = Q9MyEntry(DevExt);
    Q9_SNAP s;
    ULONG bad, seq;

    if (e == NULL) {
        return;
    }
    if (Q9Snapshot(e, &s) != Q9SNAP_OK) {
        return;                                     // R6c: next poll
    }
    bad = Q9SnapCheck(&s);
    if (bad) {
        DevExt->Q9BadSnapshots++;
        Q9LoseIdentity(DevExt, bad, "illegal entry snapshot (R5e)");
        return;
    }
    if (s.Bus != DevExt->BusNumber || s.Slot != DevExt->SlotNumber) {
        Q9LoseIdentity(DevExt, 0x20, "entry identity changed");
        return;
    }
    if (s.Gen < DevExt->Q9Gen) {
        Q9LoseIdentity(DevExt, 0x21, "LinkGeneration went backwards");
        return;
    }
    // Evidence 2 (R12 S3, R8b), applied once.
    if (DevExt->Q9LinkDownGen == 0) {
        if ((s.Flags & STOR98LS_DOWN_QUIESCED) && s.Gen >= DevExt->Q9Gen) {
            Q9ApplyLinkDown(DevExt, s.Gen, TRUE);
            return;
        }
        if (s.Gen > DevExt->Q9Gen) {
            Q9ApplyLinkDown(DevExt, s.Gen, FALSE);
        }
    }
    if ((s.Flags & STOR98LS_UP) && s.Gen != DevExt->Q9Gen) {
        Q9LoseIdentity(DevExt, 0x23, "another generation is up on this entry (R8c)");
        return;
    }
    // Removal request (R9b).
    seq = s.RemSeq & STOR98LS9X_SEQ_MASK;
    if ((s.Flags & STOR98LS_REMOVE_PENDING) && seq != DevExt->Q9BindSeq && s.Gen == DevExt->Q9Gen) {
        if (!DevExt->Q9RmAccepted) {
            Q9RmAccept(DevExt, seq);
        } else if (seq != DevExt->Q9RmReq && DevExt->Q9RmDetail != seq) {
            DevExt->Q9RmDetail = seq;               // log each overlapping request once
            Q9Mark(Q9S_IDENTITY, 0x30);
            G9Log("q9 request %u overlaps accepted request %u: ignored (first acknowledgement %08X stays)", seq,
                  DevExt->Q9RmReq, Q9Load(&e->RemoveAck));
        }
    }
}

// ---------------------------------------------------------------------------
// Timer
// ---------------------------------------------------------------------------

static VOID Q9Poll(PHW_DEVICE_EXTENSION DevExt, const char *Where)
{
    ULONG csts;

    switch (DevExt->Q9State) {
    case Q9_FAILED:
        // Presence is still watched while failed: a real link loss with
        // requests held starts MD-1 (the block stays quarantined).
        if (Q9Csts(DevExt) == 0xFFFFFFFF) {
            Q9EnterGone(DevExt, Where);
        }
        break;
    case Q9_RUNNING:
    case Q9_SHUTDOWN:
        csts = Q9Csts(DevExt);
        if (csts == 0xFFFFFFFF) {
            Q9EnterGone(DevExt, Where);
        } else if (DevExt->InitComplete && DevExt->Q9State == Q9_RUNNING) {
            NvmeProcessIoCompletion(DevExt);
            NvmeProcessAdminCompletion(DevExt);
        }
        break;
    case Q9_STARTING:
        Q9BringAdvance(DevExt);
        break;
    case Q9_REMOVING:
        Q9RmAdvance(DevExt);
        break;
    }
    if (DevExt->Q9PoolPending) {
        Q9PoolWork(DevExt, 1);
    }
    Q9LsPoll(DevExt);
}

static VOID Q9ResetAdvance(PHW_DEVICE_EXTENSION DevExt);

static VOID Q9Tick(IN PVOID DeviceExtension)
{
    PHW_DEVICE_EXTENSION DevExt = (PHW_DEVICE_EXTENSION)DeviceExtension;
    ULONG now = Q9Now();

    DevExt->Q9TimerArmed = FALSE;
    DevExt->Q9Ticks++;
    if (DevExt->Q9State == Q9_STOPPED) {
        return;
    }
    if (DevExt->Q9State == Q9_RESETTING) {
        Q9ResetAdvance(DevExt);
    }
    Q9Poll(DevExt, "timer");
    Q9HoldTimeout(DevExt);
    Q9GoneDeadline(DevExt);
    if (DevExt->Q9Opt.IsrProbe && DevExt->Q9Terminal && !DevExt->Q9QueuesReady) {
        // Test (P1-1): what a shared IRQ does to a detached instance while
        // another instance may already use its former block.
        BOOLEAN h = HwInterrupt(DevExt);
        if (DevExt->Q9IsrClosed <= 3 || (DevExt->Q9IsrClosed % 50) == 0) {
            G9Log("q9 IsrProbe: HwInterrupt -> %u, gate-closed calls %u, queues %08X/%08X/%08X/%08X, block %08X",
                  h, DevExt->Q9IsrClosed, (ULONG)DevExt->AdminQueue.SubmissionQueue,
                  (ULONG)DevExt->AdminQueue.CompletionQueue, (ULONG)DevExt->IoQueue.SubmissionQueue,
                  (ULONG)DevExt->IoQueue.CompletionQueue, DevExt->Q9PoolLinear);
        }
    }
    if (now - DevExt->Q9LastSummaryMs >= 10000) {
        DevExt->Q9LastSummaryMs = now;
        G9Stage(Q9S_TICK, DevExt->Q9Ticks);
        G9Log("q9 tick #%u now=%u ms state=%s depth=%u held=%08X writes=%u fua=%u bounced=%u busy=%u holds=%u refused=%u isr-closed=%u",
              DevExt->Q9Ticks, now, Q9StateName(DevExt->Q9State), DevExt->CurrentQueueDepth, (ULONG)DevExt->Q9HeldSrb,
              DevExt->Q9Writes, DevExt->Q9FuaWrites, DevExt->Q9Bounced, DevExt->Q9BusyCount, DevExt->Q9HoldCount,
              DevExt->Q9Refused, DevExt->Q9IsrClosed);
    }
    if (DevExt->Q9State != Q9_STOPPED) {
        Q9Arm(DevExt);
    }
}

// ---------------------------------------------------------------------------
// HwStartIo gate
// ---------------------------------------------------------------------------

static BOOLEAN Q9FastFail(PHW_DEVICE_EXTENSION DevExt, PSCSI_REQUEST_BLOCK Srb, UCHAR Status)
{
    DevExt->Q9FastFail++;
    if (DevExt->Q9FastFail <= 20 || (DevExt->Q9FastFail & 63) == 0) {
        G9Log("q9 fast-fail #%u func=%02X op=%02X -> %02X (state %s)", DevExt->Q9FastFail, Srb->Function,
              Srb->Cdb[0], Status, Q9StateName(DevExt->Q9State));
    }
    Srb->SrbStatus = Status;
    Srb->ScsiStatus = 0;
    Srb->DataTransferLength = 0;
    ScsiPortNotification(RequestComplete, DevExt, Srb);
    ScsiPortNotification(NextRequest, DevExt, NULL);
    return TRUE;
}

static BOOLEAN Q9Busy(PHW_DEVICE_EXTENSION DevExt, PSCSI_REQUEST_BLOCK Srb, const char *Why)
{
    DevExt->Q9BusyCount++;
    G9Stage(Q9S_BUSY, DevExt->Q9BusyCount);
    if (DevExt->Q9BusyCount <= 20 || (DevExt->Q9BusyCount & 63) == 0) {
        G9Log("q9 BUSY #%u op=%02X (%s)", DevExt->Q9BusyCount, Srb->Cdb[0], Why);
    }
    return ScsiBusy(DevExt, Srb);
}

static BOOLEAN Q9CompleteSuccess(PHW_DEVICE_EXTENSION DevExt, PSCSI_REQUEST_BLOCK Srb)
{
    Srb->SrbStatus = SRB_STATUS_SUCCESS;
    ScsiPortNotification(RequestComplete, DevExt, Srb);
    ScsiPortNotification(NextRequest, DevExt, NULL);
    return TRUE;
}

// R9d: after the acknowledgement nothing is served and the controller is
// never re-enabled.
static BOOLEAN Q9Refuse(PHW_DEVICE_EXTENSION DevExt, PSCSI_REQUEST_BLOCK Srb)
{
    if (Srb->Function == SRB_FUNCTION_SHUTDOWN) {
        return Q9CompleteSuccess(DevExt, Srb);
    }
    DevExt->Q9Refused++;
    Q9Mark(Q9S_REFUSED, DevExt->Q9Refused);
    if (DevExt->Q9Refused <= 5 || (DevExt->Q9Refused & 63) == 0) {
        G9Log("q9 request func=%02X op=%02X after the acknowledgement of request %u (outcome %u): refused #%u, controller stays disabled",
              Srb->Function, Srb->Cdb[0], DevExt->Q9RmReq, DevExt->Q9RmOutcome, DevExt->Q9Refused);
    }
    return Q9FastFail(DevExt, Srb, SRB_STATUS_NO_DEVICE);
}

// TRUE when the SRB was taken here (held, completed or fast-failed).
BOOLEAN Q9StartIo(PHW_DEVICE_EXTENSION DevExt, PSCSI_REQUEST_BLOCK Srb)
{
    if (DevExt->Q9HeldSrb == Srb) {
        return FALSE;       // re-dispatch of the held SRB (from Q9DispatchHeld)
    }
    Q9Poll(DevExt, "HwStartIo");
    if (DevExt->Q9Terminal && DevExt->Q9RmAcked) {
        return Q9Refuse(DevExt, Srb);
    }
    switch (DevExt->Q9State) {
    case Q9_RUNNING:
        if (Srb->Function == SRB_FUNCTION_SHUTDOWN) {
            G9Stage(Q9S_SHUTDOWN, 1);
            DevExt->Q9RmSrb = Srb;
            Srb->SrbStatus = SRB_STATUS_PENDING;
            DevExt->Q9RmStartMs = Q9Now();
            Q9RmBegin(DevExt, 2, Q9R_DRAIN);
            Q9RmAdvance(DevExt);
            return TRUE;
        }
        return FALSE;
    case Q9_STARTING:
        if (DevExt->Q9HeldSrb != NULL) {
            return Q9Busy(DevExt, Srb, "starting, one SRB already held");
        }
        Q9Hold(DevExt, Srb);
        return TRUE;
    case Q9_SHUTDOWN:
        if (Srb->Function == SRB_FUNCTION_SHUTDOWN) {
            return Q9CompleteSuccess(DevExt, Srb);
        }
        // I/O after a SHUTDOWN: re-enable first (PLAN 0.11 class 2).
        Q9StartBringup(DevExt, "I/O after SHUTDOWN");
        Q9Hold(DevExt, Srb);
        return TRUE;
    case Q9_REMOVING:
        if (Srb->Function == SRB_FUNCTION_SHUTDOWN) {
            return Q9CompleteSuccess(DevExt, Srb);
        }
        return Q9Busy(DevExt, Srb, "handshake in progress");
    case Q9_RESETTING:
        return Q9Busy(DevExt, Srb, "reset in progress");
    case Q9_QUIESCED:
    case Q9_GONE:
    case Q9_LINKDOWN:
    case Q9_FAILED:
    case Q9_STOPPED:
    default:
        return Q9FastFail(DevExt, Srb, SRB_STATUS_NO_DEVICE);
    }
}

// ---------------------------------------------------------------------------
// Data path: write-through and bounce buffers
// ---------------------------------------------------------------------------

VOID Q9WriteFlags(PHW_DEVICE_EXTENSION DevExt, PNVME_COMMAND Cmd)
{
    DevExt->Q9Writes++;
    if (DevExt->Q9Opt.WriteThrough & 1) {
        Cmd->CDW12 |= (1UL << 30);      // FUA
        DevExt->Q9FuaWrites++;
    }
}

BOOLEAN Q9BuildBounce(PHW_DEVICE_EXTENSION DevExt, PSCSI_REQUEST_BLOCK Srb, PNVME_COMMAND Cmd, BOOLEAN IsWrite)
{
    PNVME_SRB_EXTENSION ext = (PNVME_SRB_EXTENSION)Srb->SrbExtension;
    ULONG len = Srb->DataTransferLength, pages, i;
    UCHAR page;
    PULONGLONG list;

    if (!DevExt->Q9Opt.Bounce || DevExt->Q9PoolLinear == 0 || DevExt->Q9BounceBusy || len == 0 ||
        len > Q9_BOUNCE_BYTES) {
        return FALSE;
    }
    pages = (len + PAGE_SIZE - 1) / PAGE_SIZE;
    if (pages > 2) {
        page = AllocatePrpListPage(DevExt);
        if (page == 0xFF) {
            return FALSE;
        }
        ext->PrpListPage = page;
        list = (PULONGLONG)GetPrpListPageVirtual(DevExt, page);
        for (i = 1; i < pages; i++) {
            list[i - 1] = (ULONGLONG)(DevExt->Q9BouncePhys + i * PAGE_SIZE);
        }
        Cmd->PRP2 = GetPrpListPagePhysical(DevExt, page).QuadPart;
    } else {
        Cmd->PRP2 = pages == 2 ? (ULONGLONG)(DevExt->Q9BouncePhys + PAGE_SIZE) : 0;
    }
    Cmd->PRP1 = DevExt->Q9BouncePhys;
    if (IsWrite) {
        RtlCopyMemory(DevExt->Q9Bounce, Srb->DataBuffer, len);
    }
    ext->Reserved[0] = 0xB0;
    DevExt->Q9BounceBusy = TRUE;
    DevExt->Q9Bounced++;
    return TRUE;
}

VOID Q9BeforeComplete(PHW_DEVICE_EXTENSION DevExt, PSCSI_REQUEST_BLOCK Srb, USHORT Status)
{
    PNVME_SRB_EXTENSION ext = (PNVME_SRB_EXTENSION)Srb->SrbExtension;

    if (ext == NULL || ext->Reserved[0] != 0xB0) {
        return;
    }
    ext->Reserved[0] = 0;
    if (Status == NVME_SC_SUCCESS && (Srb->SrbFlags & SRB_FLAGS_DATA_IN) && Srb->DataBuffer) {
        RtlCopyMemory(Srb->DataBuffer, DevExt->Q9Bounce, Srb->DataTransferLength);
    }
    DevExt->Q9BounceBusy = FALSE;
}

// ---------------------------------------------------------------------------
// HwResetBus (G4 on 9x)
// ---------------------------------------------------------------------------

static VOID Q9ResetAdvance(PHW_DEVICE_EXTENSION DevExt)
{
    ULONG csts = Q9Csts(DevExt);

    if (csts != 0xFFFFFFFF && !(csts & NVME_CSTS_RDY)) {
        Q9Mark(Q9S_RESET_LATE, Q9Now() - DevExt->Q9ResetMs);
        G9Log("q9 reset: late disable acknowledgement after %u ms: completing outstanding with BUS_RESET",
              Q9Now() - DevExt->Q9ResetMs);
        Q9CompleteAllHeld(DevExt, SRB_STATUS_BUS_RESET, "late reset acknowledgement");
        ScsiPortNotification(ResetDetected, DevExt, NULL);
        Q9OwnDisableAcked(DevExt, 1);
        Q9StartBringup(DevExt, "after late reset acknowledgement");
        return;
    }
    if (Q9Now() - DevExt->Q9ResetMs > DevExt->Q9CapToMs) {
        G9Log("q9 reset: RDY did not clear within CAP.TO; failed, SRBs stay held, block stays owned");
        Q9SetState(DevExt, Q9_FAILED, "reset not acknowledged");
        Q9Mark(Q9S_FAILED, 0x30);
    }
}

BOOLEAN Q9ResetBus(PHW_DEVICE_EXTENSION DevExt, ULONG PathId)
{
    ULONG csts, cc, i;

    DevExt->Q9ResetCount++;
    DevExt->Q9ResetMs = Q9Now();
    csts = Q9Csts(DevExt);
    G9Log("q9 HwResetBus #%u path=%u state=%s depth=%u nontagged=%08X held=%08X CSTS=%08X", DevExt->Q9ResetCount,
          PathId, Q9StateName(DevExt->Q9State), DevExt->CurrentQueueDepth, (ULONG)DevExt->NonTaggedInFlight,
          (ULONG)DevExt->Q9HeldSrb, csts);
    if (DevExt->Q9Terminal || DevExt->Q9Opt.ResetMode == 1 || DevExt->Q9State == Q9_GONE ||
        DevExt->Q9State == Q9_FAILED || DevExt->Q9State == Q9_RESETTING || DevExt->Q9State == Q9_LINKDOWN ||
        csts == 0xFFFFFFFF) {
        if (csts == 0xFFFFFFFF) {
            Q9EnterGone(DevExt, "HwResetBus");     // also from FAILED (MD-1 starts on presence loss)
        }
        Q9Mark(Q9S_RESET, 0 | (DevExt->Q9State << 8));
        G9Log("q9 HwResetBus returns FALSE, nothing completed%s", DevExt->Q9Terminal ? " (terminal)" : "");
        return FALSE;
    }
    if (DevExt->Q9State == Q9_STARTING) {
        // Bring-up in progress; nothing was issued for the held SRB. Measured
        // on Me (Q-9x G1): completing the scan INQUIRY here (BUS_RESET, TRUE)
        // makes the port fail ScsiPortInitialize and unload the adapter, and
        // BUSY is never retried; returning FALSE keeps the hold (the port calls
        // again about every 15 s) until bring-up completes or fails.
        Q9Mark(Q9S_RESET, 2 | (DevExt->Q9State << 8));
        G9Log("q9 HwResetBus during bring-up: FALSE, held SRB stays held");
        return FALSE;
    }
    NvmeWriteReg32(DevExt, NVME_REG_INTMS, 0xFFFFFFFF);
    cc = NvmeReadReg32(DevExt, NVME_REG_CC);
    NvmeWriteReg32(DevExt, NVME_REG_CC, cc & ~(NVME_CC_ENABLE | NVME_CC_SHN_MASK));
    for (i = 0; i < 10; i++) {
        csts = Q9Csts(DevExt);
        if (csts != 0xFFFFFFFF && !(csts & NVME_CSTS_RDY)) {
            break;
        }
        ScsiPortStallExecution(100);
    }
    if (csts != 0xFFFFFFFF && !(csts & NVME_CSTS_RDY)) {
        Q9Mark(Q9S_RESET, 1 | (DevExt->Q9State << 8));
        G9Log("q9 HwResetBus: disable acknowledged within budget: BUS_RESET for outstanding, re-bring-up, TRUE");
        Q9CompleteAllHeld(DevExt, SRB_STATUS_BUS_RESET, "reset");
        Q9OwnDisableAcked(DevExt, 1);
        Q9StartBringup(DevExt, "after reset");
        return TRUE;
    }
    Q9SetState(DevExt, Q9_RESETTING, "reset: RDY still set");
    Q9Mark(Q9S_RESET, 0x10 | (DevExt->Q9State << 8));
    G9Log("q9 HwResetBus: RDY still set after 1 ms: FALSE, SRBs held, timer waits up to CAP.TO");
    return FALSE;
}

// ---------------------------------------------------------------------------
// HwAdapterState(FALSE): 98 SE stop (no timer runs after it, G9)
// ---------------------------------------------------------------------------

static BOOLEAN Q9PollBudget(PHW_DEVICE_EXTENSION DevExt, ULONG Mask, ULONG Want, PULONG Csts)
{
    ULONG i;

    for (i = 0; i < 10; i++) {
        *Csts = Q9Csts(DevExt);
        if (*Csts == 0xFFFFFFFF) {
            return FALSE;
        }
        if ((*Csts & Mask) == Want) {
            return TRUE;
        }
        ScsiPortStallExecution(100);
    }
    return FALSE;
}

// _PageFree of this load's isolated block (PoolFree option).
static VOID Q9PoolFreeMine(PHW_DEVICE_EXTENSION DevExt)
{
    PQ9_REC r;
    PQ9_ACCT a = gQ9Acct;
    ULONG ok;

    if (!Q9Lock(DevExt, 10)) {
        return;
    }
    r = Q9MyRecord(DevExt);
    if (r == NULL || r->State != Q9PL_ISOLATED) {
        Q9Unlock(DevExt);
        return;
    }
    Q9Xchg(&r->State, Q9PL_RESERVED);
    Q9Unlock(DevExt);
    ok = Q9PageFree(r->Handle, 0);
    G9Log("q9 pool[%d]: _PageFree(%08X) -> %u", DevExt->Q9PoolIndex, r->Handle, ok);
    if (!Q9Lock(DevExt, 10)) {
        G9Log("q9 pool[%d]: lock busy after _PageFree: record stays RESERVED (slot lost)", DevExt->Q9PoolIndex);
        return;
    }
    if (ok) {
        r->Handle = r->Linear = r->Physical = r->Pages = 0;
        r->LastOwner = r->Owner;
        r->Owner = 0;
        Q9Xchg(&r->State, Q9PL_FREE);
        Q9RecLog((ULONG)DevExt->Q9PoolIndex, r, Q9PL_RESERVED, "freed");
        DevExt->Q9PoolIndex = -1;
        DevExt->Q9PoolLinear = 0;
    } else {
        Q9Xchg(&r->State, Q9PL_ISOLATED);
    }
    Q9Unlock(DevExt);
    if (ok && a != NULL) {
        Q9AcctUncharge(a, Q9_POOL_PAGES);
    }
}

VOID Q9AdapterStop(PHW_DEVICE_EXTENSION DevExt)
{
    ULONG csts, cc, i;
    BOOLEAN flushOk = FALSE, shstOk = FALSE, rdy0 = FALSE;
    PQ9_REC r;

    G9Log("q9 HwAdapterState(FALSE) in state %s", Q9StateName(DevExt->Q9State));
    if (DevExt->Q9TimerArmed) {
        ScsiPortNotification(RequestTimerCall, DevExt, Q9Tick, 0);
        DevExt->Q9TimerArmed = FALSE;
    }
    csts = Q9Csts(DevExt);
    cc = NvmeReadReg32(DevExt, NVME_REG_CC);
    if (csts == 0xFFFFFFFF) {
        G9Log("q9 stop: CSTS all-ones: nothing to do");
    } else if (!(cc & NVME_CC_ENABLE) && !(csts & NVME_CSTS_RDY)) {
        rdy0 = TRUE;
        G9Log("q9 stop: controller already disabled (CC=%08X CSTS=%08X)", cc, csts);
    } else if (DevExt->Q9State == Q9_RUNNING || DevExt->Q9State == Q9_SHUTDOWN) {
        // Bounded stop within the stall budget (PLAN 0.10, 2.4).
        NvmeWriteReg32(DevExt, NVME_REG_INTMS, 0xFFFFFFFF);
        if (DevExt->Q9State == Q9_RUNNING && DevExt->InitComplete && !Q9Outstanding(DevExt)) {
            DevExt->Q9RmFlushDone = FALSE;
            Q9SubmitInternalFlush(DevExt);
            for (i = 0; i < 10 && !DevExt->Q9RmFlushDone; i++) {
                NvmeProcessIoCompletion(DevExt);
                if (!DevExt->Q9RmFlushDone) {
                    ScsiPortStallExecution(100);
                }
            }
            flushOk = (BOOLEAN)(DevExt->Q9RmFlushDone && DevExt->Q9RmFlushStatus == 0);
        }
        NvmeWriteReg32(DevExt, NVME_REG_CC, (cc & ~NVME_CC_SHN_MASK) | NVME_CC_SHN_NORMAL);
        shstOk = Q9PollBudget(DevExt, NVME_CSTS_SHST_MASK, NVME_CSTS_SHST_COMPLETE, &csts);
        cc = NvmeReadReg32(DevExt, NVME_REG_CC);
        NvmeWriteReg32(DevExt, NVME_REG_CC, cc & ~NVME_CC_ENABLE);
        rdy0 = Q9PollBudget(DevExt, NVME_CSTS_RDY, 0, &csts);
        G9Log("q9 stop (bounded): flush %s, SHST %s, RDY=0 %s, CSTS=%08X", flushOk ? "ok" : "not within budget",
              shstOk ? "complete" : "not within budget", rdy0 ? "acknowledged" : "NOT acknowledged", csts);
    } else {
        // Any other state with the controller enabled or not yet showing RDY=0
        // (bring-up, reset, handshake in progress, failed): disable within
        // the budget; no shutdown notification (nothing of ours is cached).
        NvmeWriteReg32(DevExt, NVME_REG_INTMS, 0xFFFFFFFF);
        NvmeWriteReg32(DevExt, NVME_REG_CC, cc & ~(NVME_CC_ENABLE | NVME_CC_SHN_MASK));
        rdy0 = Q9PollBudget(DevExt, NVME_CSTS_RDY, 0, &csts);
        G9Log("q9 stop in state %s: CC.EN=0, RDY=0 %s, CSTS=%08X", Q9StateName(DevExt->Q9State),
              rdy0 ? "acknowledged" : "NOT acknowledged", csts);
    }
    Q9Mark(Q9S_STOP, flushOk | (shstOk << 1) | (rdy0 << 2) | (DevExt->Q9State << 8));
    if (rdy0) {
        DevExt->Q9Isolated = TRUE;
        Q9PoolMine(DevExt, Q9PL_OWNED, Q9PL_ISOLATED, "stop: acknowledged disable");
    }
    // No queue consumer may remain before the block is given up or freed.
    Q9DetachQueues(DevExt, "HwAdapterState(FALSE)");
    G9Log("q9 stop: HwInterrupt was called %u times with the queue gate closed", DevExt->Q9IsrClosed);
    if (DevExt->Q9Opt.PoolFree && DevExt->Q9Isolated) {
        Q9PoolFreeMine(DevExt);
    }
    // Give the block up: RELEASED if isolated, QUARANTINED otherwise (R10).
    if (gQ9Acct != NULL && Q9Lock(DevExt, 10)) {
        r = Q9MyRecord(DevExt);
        if (r != NULL && r->State == Q9PL_ISOLATED) {
            Q9RecDisown((ULONG)DevExt->Q9PoolIndex, r, Q9PL_RELEASED, "stop: isolated");
        } else if (r != NULL && r->State == Q9PL_OWNED) {
            Q9RecDisown((ULONG)DevExt->Q9PoolIndex, r, Q9PL_QUARANTINED, "stop without isolation evidence");
        }
        DevExt->Q9PoolIndex = -1;
        Q9Unlock(DevExt);
    }
    Q9EndLife(DevExt, "HwAdapterState(FALSE)");
    DevExt->InitComplete = FALSE;
    Q9SetState(DevExt, Q9_STOPPED, "HwAdapterState(FALSE)");
}

#else

// Keeps the translation unit non-empty in builds without STOR98_Q9X.
typedef int Q9X_UNUSED;

#endif
