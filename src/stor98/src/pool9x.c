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
// pool9x.c - the Windows 9x MD-4 DMA pool provider (platform.h, provider
// hook) on the stor98 boot block of interface 3 (include/stor98cm.h R7;
// docs/gates/Q-9x.md, "Interface 3 revision"). Built with STOR98_DMA_POOL
// on the 9x target only. The boot block, its records, the budget, the
// rendezvous and the evidence rules E1 and E2 follow the Q-9x prototype
// (src/q9x.c, branch qual-9x); the record states are the engine's (N2):
// the engine's per-block notes are folded into one record per adapter
// block. Design and the mapping: docs/INTEGRATION-0.2.md, "Pool wiring".
//
// Every VMM service used here is a documented Windows 98 DDK VMM service
// (VMM.H service table), reached through the INT 20h dynalink the .mpd
// already uses for _MapPhysToLinear and _PageModifyPermissions (decision D1;
// no VxD): _PageAllocate, _PageFree, _PageGetSizeAddr, _CopyPageTable,
// _LinPageLock, Get_System_Time, _RegOpenKey, _RegCreateKey, _RegCloseKey,
// _RegDeleteValue (test builds only), _RegQueryValueEx, _RegSetValueEx. No
// IOS- or SCSIPORT-private data.
//

#include "nvme2k.h"
#include "utils.h"
#include "..\include\stor98cm.h"

#if defined(STOR98_DMA_POOL) && defined(NVME2K_W9X)

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
static ULONGLONG NAKED _cdecl P9PageAllocate(ULONG nPages, ULONG pType, ULONG VM, ULONG AlignMask,
                                            ULONG minPhys, ULONG maxPhys, PULONG PhysAddr, ULONG flags)
{
    VMMJMP(0x0053)
}

static ULONG NAKED _cdecl P9PageFree(ULONG hMem, ULONG flags)
{
    VMMJMP(0x0055)
}

// _PageGetSizeAddr: EAX = pages of the allocation whose handle is hMem (0 if
// hMem is not an allocation handle), EDX = its linear address.
static ULONGLONG NAKED _cdecl P9PageGetSizeAddr(ULONG hMem, ULONG flags)
{
    VMMJMP(0x0058)
}

// _CopyPageTable: copies nPages page-table entries starting at linear page
// LinPgNum into PageBuf; nonzero on success. Reads no page contents.
static ULONG NAKED _cdecl P9CopyPageTable(ULONG LinPgNum, ULONG nPages, PULONG PageBuf, ULONG flags)
{
    VMMJMP(0x0061)
}

// _LinPageLock: locks nPages linear pages starting at HLinPgNum; nonzero on
// success.
static ULONG NAKED _cdecl P9LinPageLock(ULONG HLinPgNum, ULONG nPages, ULONG flags)
{
    VMMJMP(0x0063)
}

// Get_System_Time: milliseconds since Windows started, in EAX (the VMM
// elapsed time interface 3 R10 measures its bounds in).
static ULONG NAKED _cdecl P9GetSystemTime(VOID)
{
    VMMJMP(0x003F)
}

static ULONG NAKED _cdecl P9RegOpenKey(ULONG hkey, PCHAR SubKey, PULONG phk)
{
    VMMJMP(0x0148)
}

static ULONG NAKED _cdecl P9RegCloseKey(ULONG hkey)
{
    VMMJMP(0x0149)
}

static ULONG NAKED _cdecl P9RegCreateKey(ULONG hkey, PCHAR SubKey, PULONG phk)
{
    VMMJMP(0x014A)
}

static ULONG NAKED _cdecl P9RegDeleteValue(ULONG hkey, PCHAR Name)
{
    VMMJMP(0x014F)
}

static ULONG NAKED _cdecl P9RegQueryValueEx(ULONG hkey, PCHAR Name, PULONG Reserved, PULONG Type,
                                           PVOID Data, PULONG cbData)
{
    VMMJMP(0x0151)
}

static ULONG NAKED _cdecl P9RegSetValueEx(ULONG hkey, PCHAR Name, ULONG Reserved, ULONG Type,
                                         PVOID Data, ULONG cbData)
{
    VMMJMP(0x0152)
}

#define P9_HKLM             0x80000002
#define P9_HKDD             0x80000006      // HKEY_DYN_DATA
#define P9_PG_SYS           1
#define P9_PAGEZEROINIT     0x00000001
#define P9_PAGEUSEALIGN     0x00000002
#define P9_PAGECONTIG       0x00000004
#define P9_PAGEFIXED        0x00000008
#define P9_PTE_PRESENT      0x00000001
#define P9_PTE_WRITE        0x00000002

// ---------------------------------------------------------------------------
// Atomic access (interface 3 R6). x86 keeps loads ordered with loads and
// stores with stores; every publication-point access is inline assembly,
// which MSVC never moves memory accesses across. Locked instructions are
// full CPU barriers.
// ---------------------------------------------------------------------------

static ULONG P9Load(volatile ULONG *p)
{
    ULONG v;

    __asm {
        mov ecx, p
        mov eax, [ecx]
        mov v, eax
    }
    return v;
}

static ULONG P9Xchg(volatile ULONG *p, ULONG val)
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

static ULONG P9CmpXchg(volatile ULONG *p, ULONG cmpv, ULONG val)
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

#define P9LOCK_CAS(p, cmp, val)     P9CmpXchg((p), (cmp), (val))
#define P9LOCK_STALL(us)            ScsiPortStallExecution(us)
#include "p9lock.h"

static ULONGLONG P9Tsc(VOID)
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

// The rendezvous field checks (R5a-R5c, P9InArena), the entry snapshot, its
// R5e check and the binding scan, and the removal request's acceptance and
// acknowledgement (R9b, R9c), shared with the late lookup and the host unit
// test (tests/unit/p9bind_ut.c).
#define P9BIND_LOAD(p)              P9Load(p)
#define P9BIND_XCHG(p, v)           P9Xchg((p), (v))
#include "p9bind.h"

// ---------------------------------------------------------------------------
// Residency and provenance: page-table entries only, through the documented
// _CopyPageTable; no page content is touched here (the arena range check,
// P9InArena, is in p9bind.h).
// ---------------------------------------------------------------------------

// Every page of [Linear, Linear + Bytes) present; with Phys nonzero, page i
// also maps physical page Phys + i * PAGE_SIZE.
static BOOLEAN P9PagesPresent(ULONG Linear, ULONG Bytes, ULONG Phys)
{
    ULONG first = Linear >> PAGE_SHIFT, last = (Linear + Bytes - 1) >> PAGE_SHIFT, i, pte;

    for (i = first; i <= last; i++) {
        pte = 0;
        if (!P9CopyPageTable(i, 1, &pte, 0) || !(pte & P9_PTE_PRESENT)) {
            return FALSE;
        }
        if (Phys && (pte & ~(PAGE_SIZE - 1)) != Phys + (i - first) * PAGE_SIZE) {
            return FALSE;
        }
    }
    return TRUE;
}

static BOOLEAN P9PagesWritable(ULONG Linear, ULONG Bytes)
{
    ULONG first = Linear >> PAGE_SHIFT, last = (Linear + Bytes - 1) >> PAGE_SHIFT, i, pte;

    for (i = first; i <= last; i++) {
        pte = 0;
        if (!P9CopyPageTable(i, 1, &pte, 0) ||
            (pte & (P9_PTE_PRESENT | P9_PTE_WRITE)) != (P9_PTE_PRESENT | P9_PTE_WRITE)) {
            return FALSE;
        }
    }
    return TRUE;
}

// ---------------------------------------------------------------------------
// The stor98 boot block (interface 3 R7): the MD-4 pool ownership records,
// instance tokens, the per-function binding and the boot-wide budget. One
// 2-page _PageAllocate block per boot, never freed, found again by later
// loads (the .mpd is unloaded at every removal) through HKLM\Software\stor98
// \Pool "Acct", and accepted only when the referenced pages are present and
// map the recorded physical pages, carry the signature and the 64-bit
// cookie, and start a 2-page VMM allocation. Anything else (a previous
// boot's value, or none) creates a new block for this boot, unless the
// reference names this boot's CM proof (then this boot's accounting was lost:
// fail closed). Separate from the Q-9x prototype's block (its own key and
// signature), so prototype and product records never mix.
// ---------------------------------------------------------------------------

#define P9_ACCT_PAGES       2
#define P9_ACCT_SIG         0x42503938      // '89PB'
#define P9_ACCT_KEY         "Software\\stor98\\Pool"
#define P9_FUNCS            8
#define P9_RECS             8
#define P9_META_SIG         0x4D503938      // '89PM'

#ifndef STOR98_POOL_CAP_BLOCKS
#define STOR98_POOL_CAP_BLOCKS  6           // boot-wide budget, in adapter blocks (test builds may override)
#endif

#define P9RF_NOFUNC         0x00000001      // owner had no function slot (liveness unknown: alive)
#define P9RF_BAD            0x00000002      // failed validation: never claimed, freed or touched again

typedef struct _P9_FUNC {
    ULONG Bus;                      // 0xFFFFFFFF: unused
    ULONG Slot;
    volatile ULONG Token;           // current instance for the function
    volatile ULONG Live;            // 1 while that instance has not ended
} P9_FUNC;

typedef struct _P9_REC {
    volatile ULONG State;           // PLAT_OWN_*
    volatile ULONG Owner;           // token, 0 none
    volatile ULONG Flags;           // P9RF_*
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
} P9_REC, *PP9_REC;

typedef struct _P9_ACCT {
    ULONG Signature;
    ULONG CookieLo;
    ULONG CookieHi;
    ULONG Linear;
    ULONG Physical;
    volatile ULONG ChargedPages;
    volatile ULONG Charges;
    volatile ULONG Uncharges;
    volatile ULONG Lock;            // 0 free, else the holder's token (0xFFFFFFFF before it has one)
    volatile ULONG NextToken;
    ULONG BootCookie;               // the CM proof cookie at creation, 0 if none
    volatile ULONG LockedTable;     // cookie of the CM table whose pages this boot locked
    ULONG Reserved[4];
    P9_FUNC Func[P9_FUNCS];
    P9_REC Rec[P9_RECS];
} P9_ACCT, *PP9_ACCT;

typedef struct _P9_ACCT_REF {
    ULONG Linear;
    ULONG Physical;
    ULONG CookieLo;
    ULONG CookieHi;
    ULONG BootCookie;       // the CM's current-boot proof cookie at creation, 0 if none
    ULONG Check;            // P9AcctRefCheck of the fields above
} P9_ACCT_REF;

// Registry values under HKLM\Software\stor98\Pool, as interface 3's final
// accounting rules (Q-9x, qual-9x cf56ccd) for the prototype's own block:
// "AcctRef" (REG_BINARY, P9_ACCT_REF with its check word) and "AcctBoot"
// (REG_DWORD, the proof cookie of the boot that last initialised
// accounting, written before "AcctRef"). The unchecked "Acct" of earlier
// int-0.2 builds is ignored.
#define P9_ACCT_REF_VALUE       "AcctRef"
#define P9_ACCT_BOOT_VALUE      "AcctBoot"
#define P9_ERROR_FILE_NOT_FOUND 2

static ULONG P9AcctRefCheck(const P9_ACCT_REF *r)
{
    ULONG c = 0x4E393850, v[5], i;

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

// Metadata page: the last page of each block, never given to the controller.
typedef struct _P9_META {
    ULONG Signature;
    ULONG Physical;
    ULONG Linear;
    ULONG Pages;
    ULONG Loads;
    ULONG LastToken;
    ULONG LastBus;
    ULONG LastSlot;
    ULONG LastGen;
} P9_META, *PP9_META;

static PP9_ACCT gP9Acct;                // this image load's view of the block
static volatile ULONG gP9AcctBusy;

static BOOLEAN P9AcctCheck(P9_ACCT_REF *Ref)
{
    PP9_ACCT a;
    ULONGLONG info;

    if ((Ref->Linear & (PAGE_SIZE - 1)) || (Ref->Physical & (PAGE_SIZE - 1)) || Ref->Physical == 0 ||
        !P9InArena(Ref->Linear, P9_ACCT_PAGES * PAGE_SIZE)) {
        return FALSE;
    }
    // Present and mapping the recorded RAM pages: reading them is harmless.
    if (!P9PagesPresent(Ref->Linear, P9_ACCT_PAGES * PAGE_SIZE, Ref->Physical)) {
        return FALSE;
    }
    a = (PP9_ACCT)Ref->Linear;
    if (a->Signature != P9_ACCT_SIG || a->CookieLo != Ref->CookieLo || a->CookieHi != Ref->CookieHi ||
        a->Linear != Ref->Linear || a->Physical != Ref->Physical) {
        return FALSE;
    }
    // Only then ask VMM whether it is the start of a 2-page allocation.
    info = P9PageGetSizeAddr(Ref->Linear, 0);
    return (BOOLEAN)((ULONG)info == P9_ACCT_PAGES && (ULONG)(info >> 32) == Ref->Linear);
}

// Discovery is anchored in the CM's current-boot proof when one exists
// (Proof != 0). Then the load fails closed on anything but a clean answer: a
// lookup error other than "not found", a wrong type or size, a reference
// whose check word does not match, a reference of this boot whose block does
// not check out, or a missing reference while "AcctBoot" says this boot
// already initialised (or cannot be read cleanly). Only a valid reference of
// another boot, or no reference and no marker of this boot, starts new
// accounting. Without a CM there is no current-boot authority and an
// unusable reference is treated as a new boot (the documented residual of
// Q-9x). Every failure fails the find before CC.EN.
static PP9_ACCT P9AcctGet(ULONG Proof)
{
    ULONG hk = 0, type = 0, cb, phys = 0, i, rc, boot = 0, why = 0;
    ULONG buf[(sizeof(P9_ACCT_REF) + 16) / sizeof(ULONG)];
    P9_ACCT_REF ref;
    PP9_ACCT a = NULL;
    ULONGLONG r, tsc;

    if (gP9Acct != NULL) {
        return gP9Acct;
    }
    if (P9Xchg(&gP9AcctBusy, 1) != 0) {
        return NULL;                                // another adapter of this image is creating it
    }
    if (P9RegCreateKey(P9_HKLM, P9_ACCT_KEY, &hk) != 0) {
        P9Xchg(&gP9AcctBusy, 0);
        return NULL;
    }
    RtlZeroMemory(&ref, sizeof(ref));
    RtlZeroMemory(buf, sizeof(buf));
    cb = sizeof(buf);
    rc = P9RegQueryValueEx(hk, P9_ACCT_REF_VALUE, NULL, &type, buf, &cb);
    if (rc == P9_ERROR_FILE_NOT_FOUND) {
        cb = 0;
        if (Proof != 0) {
            ULONG bt = 0, bcb = sizeof(boot), brc;

            // Only "not found" for both values means a first initialisation.
            brc = P9RegQueryValueEx(hk, P9_ACCT_BOOT_VALUE, NULL, &bt, &boot, &bcb);
            if (brc == P9_ERROR_FILE_NOT_FOUND) {
                why = 0;                            // neither value: first installation
            } else if (brc != 0) {
                why = 0x31;                         // reference missing, marker lookup failed
            } else if (bt != P9_REG_DWORD || bcb != sizeof(boot)) {
                why = 0x32;                         // reference missing, marker malformed
            } else if (boot == Proof) {
                why = 0x33;                         // reference missing, this boot already initialised
            }
        }
    } else if (rc != 0) {
        why = 0x34;                                 // reference lookup failed
        cb = 0;
    } else if (type != P9_REG_BINARY || cb != sizeof(ref)) {
        why = 0x35;                                 // wrong type or size
        cb = 0;
    } else {
        RtlCopyMemory(&ref, buf, sizeof(ref));
        if (ref.Check != P9AcctRefCheck(&ref)) {
            why = 0x36;                             // check word does not match
            cb = 0;
        }
    }
    if (why != 0 && Proof == 0) {
        why = 0;                                    // no CM: treated as a new boot (residual)
    }
    if (why != 0) {
        DBGSTAGE(STG_P9_FIND, P9_FAIL_ACCT | (why << 8));
        a = NULL;                                   // this boot's accounting lost or corrupt: fail closed
    } else if (cb && P9AcctCheck(&ref) && ((PP9_ACCT)ref.Linear)->BootCookie == ref.BootCookie) {
        a = (PP9_ACCT)ref.Linear;
    } else if (cb && Proof != 0 && ref.BootCookie == Proof) {
        // Recorded in this boot (the CM's proof) but no longer checks out.
        DBGSTAGE(STG_P9_FIND, P9_FAIL_ACCT | (0x37 << 8));
        a = NULL;
    } else {
        r = P9PageAllocate(P9_ACCT_PAGES, P9_PG_SYS, 0, 0, 0, 0xFFFFF, &phys,
                           P9_PAGEFIXED | P9_PAGEUSEALIGN | P9_PAGECONTIG | P9_PAGEZEROINIT);
        if ((ULONG)r != 0 && phys != 0) {
            tsc = P9Tsc();
            a = (PP9_ACCT)(ULONG)r;
            a->CookieLo = (ULONG)tsc;
            a->CookieHi = (ULONG)(tsc >> 32) ^ (ULONG)r;
            a->Linear = (ULONG)r;
            a->Physical = phys;
            a->BootCookie = Proof;
            for (i = 0; i < P9_FUNCS; i++) {
                a->Func[i].Bus = 0xFFFFFFFF;
            }
            a->Signature = P9_ACCT_SIG;
            ref.Linear = a->Linear;
            ref.Physical = phys;
            ref.CookieLo = a->CookieLo;
            ref.CookieHi = a->CookieHi;
            ref.BootCookie = Proof;
            ref.Check = P9AcctRefCheck(&ref);
            boot = Proof;
            // The boot marker first, then the reference.
            if (P9RegSetValueEx(hk, P9_ACCT_BOOT_VALUE, 0, P9_REG_DWORD, &boot, sizeof(boot)) != 0 ||
                P9RegSetValueEx(hk, P9_ACCT_REF_VALUE, 0, P9_REG_BINARY, &ref, sizeof(ref)) != 0) {
                a->Signature = 0;
                P9PageFree((ULONG)r, 0);
                a = NULL;
            }
        }
    }
    P9RegCloseKey(hk);
    gP9Acct = a;
    P9Xchg(&gP9AcctBusy, 0);
    return a;
}

// Charges Pages against the boot-wide budget; overflow-safe, never spins.
static BOOLEAN P9AcctCharge(PP9_ACCT a, ULONG Pages, ULONG Cap)
{
    ULONG i, old;

    for (i = 0; i < 8; i++) {
        old = P9Load(&a->ChargedPages);
        if (Pages > Cap || old > Cap - Pages) {
            DBGSTAGE(STG_P9_BUDGET, (old & 0xFFF) | (Pages << 12) | 0x80000000UL);
            return FALSE;
        }
        if (P9CmpXchg(&a->ChargedPages, old, old + Pages) == old) {
            a->Charges++;
            DBGSTAGE(STG_P9_BUDGET, (old & 0xFFF) | (Pages << 12));
            return TRUE;
        }
    }
    return FALSE;
}

static VOID P9AcctUncharge(PP9_ACCT a, ULONG Pages)
{
    ULONG i, old;

    for (i = 0; i < 8; i++) {
        old = P9Load(&a->ChargedPages);
        if (old < Pages) {
            return;                                 // inconsistent: left as is (over-count is safe)
        }
        if (P9CmpXchg(&a->ChargedPages, old, old - Pages) == old) {
            a->Uncharges++;
            return;
        }
    }
}

// Boot-block lock (p9lock.h, K2): one attempt, and further ones only while
// the callback's shared stall budget lasts. Runtime callbacks pass NULL (one
// non-blocking attempt) and defer the work.
static BOOLEAN P9Lock(IN PHW_DEVICE_EXTENSION DevExt, IN OUT PP9_STALLS Budget)
{
    PP9_ACCT a = gP9Acct;
    ULONG me = DevExt->Pool9x.Token ? DevExt->Pool9x.Token : 0xFFFFFFFF;

    if (a == NULL) {
        return FALSE;
    }
    return P9LockAttempt(&a->Lock, me, Budget);
}

static VOID P9Unlock(VOID)
{
    P9Xchg(&gP9Acct->Lock, 0);
}

// ---------------------------------------------------------------------------
// Rendezvous: locator, current-boot proof, table (R3, R5a-d); binding (R8d)
// ---------------------------------------------------------------------------

// Binds the entry found (R8d), at the find or by the late lookup.
static VOID P9BindTo(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG Index, IN const P9_SNAP *Pick)
{
    PLAT_POOL9X *p = &DevExt->Pool9x;

    p->Gen = Pick->Gen;
    p->BindSeq = Pick->RemSeq & STOR98LS9X_SEQ_MASK;
    p->Entry = &((PSTOR98LS_TABLE)p->Table)->Entry[Index];
}

//
// At HwFindAdapter (p9bind.h). No entry yet, the previous tunnel's
// DOWN_QUIESCED, or an entry being updated: the table is kept and the late
// lookup (PlatCmLinkPoll) looks again for P9_LATE_BIND_MS (owner decision
// 2026-10-16: on 98 SE the CM posts a hot-plugged function's UP only after
// Windows enumerated it, which is after this find). A duplicate or a
// malformed entry for this function: no channel for this load.
//
static VOID P9Bind(IN PHW_DEVICE_EXTENSION DevExt)
{
    PLAT_POOL9X *p = &DevExt->Pool9x;
    P9_SNAP pick;
    ULONG rc, index;

    rc = P9BindScan((PSTOR98LS_TABLE)p->Table, DevExt->BusNumber, DevExt->SlotNumber, 0, &index, &pick);
    switch (rc) {
    case P9B_BOUND:
        P9BindTo(DevExt, index, &pick);
        break;
    case P9B_DUP:
        p->Table = NULL;                            // duplicate (R8a): no channel
        p->RendezvousRc = 9;
        break;
    case P9B_BAD:
        p->Table = NULL;                            // malformed entry for this function: no channel,
        p->RendezvousRc = 17;                       // and no E1 from this table
        break;
    default:
        // P9B_NONE (8), P9B_DOWN (15) and P9B_IDLE (18): their generation is
        // the floor; P9B_BUSY (16)
        P9LateStart(rc, &pick, &p->LateOn, &p->LateFloor);
        p->LateStartMs = P9GetSystemTime();
        p->LateRc = rc;
        p->RendezvousRc = (rc == P9B_DOWN) ? 15 : (rc == P9B_IDLE) ? 18 : (rc == P9B_BUSY) ? 16 : 8;
        break;
    }
}

// R5b: the current-boot proof (a CM-hosted dynamic key), read once per load
// before the boot block is looked up (it is also that lookup's authority).
// 0: proof present; 10: no proof key; 11: unusable value.
static ULONG P9ReadProof(IN PHW_DEVICE_EXTENSION DevExt)
{
    PLAT_POOL9X *p = &DevExt->Pool9x;
    ULONG hk = 0, type = 0, cb = sizeof(ULONG), rc, proof = 0;

    p->ProofCookie = 0;
    p->ProofTable[0] = p->ProofTable[1] = p->ProofTable[2] = 0;
    if (P9RegOpenKey(P9_HKDD, STOR98LS9X_PROOF_KEY, &hk) != 0) {
        return 10;
    }
    rc = P9RegQueryValueEx(hk, STOR98LS9X_PROOF_VALUE, NULL, &type, &proof, &cb);
    P9RegCloseKey(hk);
    proof = P9ProofCookie(rc, type, cb, proof);
    if (proof == 0) {
        return 11;
    }
    p->ProofCookie = proof;
    // The table address authority (option A, R3a): the provider's TableLinear.
    hk = 0;
    if (P9RegOpenKey(P9_HKDD, STOR98LS9X_PROOF_KEY, &hk) == 0) {
        type = 0;
        cb = sizeof(p->ProofTable);
        rc = P9RegQueryValueEx(hk, STOR98LS9X_PROOF_TABLE_VALUE, NULL, &type, p->ProofTable, &cb);
        P9RegCloseKey(hk);
        if (!P9ProofTableRead(rc, type, cb)) {
            p->ProofTable[0] = p->ProofTable[1] = p->ProofTable[2] = 0;
        }
    }
    return 0;
}

// Reads the locator once and checks it against the proof; 0 when the table
// is accepted and this function bound to its entry.
static ULONG P9Rendezvous(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG ProofRc)
{
    PLAT_POOL9X *p = &DevExt->Pool9x;
    ULONG hk = 0, type = 0, cb, rc;
    PP9_ACCT a = gP9Acct;
    STOR98LS9X_LOCATOR loc;
    PSTOR98LS_TABLE t;

    p->Table = NULL;
    p->Entry = NULL;
    p->Gen = 0;
    p->TableCookie = 0;
    // R5a: the locator (persistent; only a locator).
    if (P9RegOpenKey(P9_HKLM, STOR98LS9X_LOCATOR_KEY, &hk) != 0) {
        return 1;
    }
    RtlZeroMemory(&loc, sizeof(loc));
    cb = sizeof(loc);
    rc = P9RegQueryValueEx(hk, STOR98LS9X_LOCATOR_VALUE, NULL, &type, &loc, &cb);
    P9RegCloseKey(hk);
    rc = P9LocatorCheck(rc, type, cb, &loc);        // 2 the value, 3 a field
    if (rc != 0) {
        return rc;
    }
    // R5b: the current-boot proof must exist and carry the locator's cookie,
    // and its TableLinear (the address authority) must agree with it.
    if (ProofRc != 0) {
        return ProofRc;
    }
    rc = P9ProofCheck(&loc, p->ProofCookie, p->ProofTable);     // 11 cookie, 14 table
    if (rc != 0) {
        return rc;
    }
    // R5c: range and alignment, then residency, before the first dereference;
    // then write access and this boot's lock of the pages (never unlocked).
    rc = P9TableAddressCheck(&loc);                 // 4
    if (rc != 0) {
        return rc;
    }
    if (!P9PagesPresent(loc.TableLinear, loc.TableSize, 0)) {
        return 5;
    }
    if (!P9PagesWritable(loc.TableLinear, loc.TableSize)) {
        return 12;
    }
    if (a == NULL || P9Load(&a->LockedTable) != loc.Cookie) {
        if (!P9LinPageLock(loc.TableLinear >> PAGE_SHIFT,
                           ((loc.TableLinear + loc.TableSize - 1) >> PAGE_SHIFT) - (loc.TableLinear >> PAGE_SHIFT) + 1,
                           0)) {
            return 13;
        }
        if (a != NULL) {
            P9Xchg(&a->LockedTable, loc.Cookie);
        }
    }
    // R5d: signature, version, count and the locator's (nonzero) cookie
    t = (PSTOR98LS_TABLE)loc.TableLinear;
    if (!P9TableSame(t, loc.Cookie)) {
        return 6;
    }
    p->Table = t;
    p->TableCookie = t->Cookie;
    p->RendezvousRc = 0;
    P9Bind(DevExt);
    if (p->Table == NULL || p->Entry == NULL) {
        return p->RendezvousRc;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Pool records (boot block, under its lock unless noted)
// ---------------------------------------------------------------------------

static P9_FUNC *P9FindFunc(PP9_ACCT a, ULONG Bus, ULONG Slot)
{
    ULONG i;

    for (i = 0; i < P9_FUNCS; i++) {
        if (a->Func[i].Bus == Bus && a->Func[i].Slot == Slot) {
            return &a->Func[i];
        }
    }
    return NULL;
}

// Is the owner of r still alive? Unknown owners count as alive.
static BOOLEAN P9OwnerAlive(PP9_ACCT a, PP9_REC r)
{
    P9_FUNC *f;

    if (r->Flags & P9RF_NOFUNC) {
        return TRUE;
    }
    f = P9FindFunc(a, r->Bus, r->Slot);
    return (BOOLEAN)(f != NULL && P9Load(&f->Live) && P9Load(&f->Token) == P9Load(&r->Owner));
}

// E1 (p9bind.h): this load's CM table states DOWN_QUIESCED on the record's
// own entry for the record's generation or a later one. A later generation
// alone is not E1 (owner decision 2026-10-18); such a record of this load's
// own function waits for E2.
static BOOLEAN P9EvidenceE1(IN PHW_DEVICE_EXTENSION DevExt, PP9_REC r)
{
    PLAT_POOL9X *p = &DevExt->Pool9x;
    PSTOR98LS_TABLE t = (PSTOR98LS_TABLE)p->Table;
    P9_SNAP s;
    ULONG i;

    if (t == NULL || r->Gen == 0 || r->TableCookie != p->TableCookie) {
        return FALSE;
    }
    for (i = 0; i < STOR98LS_ENTRIES; i++) {
        if (!P9Snapshot(&t->Entry[i], &s) || P9SnapCheck(&s) != 0 || s.Bus != r->Bus || s.Slot != r->Slot) {
            continue;
        }
        return P9EvidenceE1Snap(&s, r->Gen);
    }
    return FALSE;
}

static VOID P9RecMark(ULONG i, PP9_REC r, ULONG From, ULONG Cause)
{
    DBGSTAGE(STG_P9_REC, i | ((r->State & 0xF) << 8) | ((From & 0xF) << 12) | ((Cause & 0xFF) << 16) |
             ((r->Bus & 0xFF) << 24));
    UNREFERENCED_PARAMETER(i);
    UNREFERENCED_PARAMETER(r);
    UNREFERENCED_PARAMETER(From);
    UNREFERENCED_PARAMETER(Cause);
}

// Under the lock: the record stops having an owner (RELEASED or QUARANTINED).
static VOID P9RecDisown(ULONG i, PP9_REC r, ULONG State, ULONG Cause)
{
    ULONG from = r->State;

    r->LastOwner = r->Owner;
    r->Owner = 0;
    P9Xchg(&r->State, State);
    P9RecMark(i, r, from, Cause);
}

// This instance's record, only while it still owns it.
static PP9_REC P9MyRecord(IN PHW_DEVICE_EXTENSION DevExt)
{
    PLAT_POOL9X *p = &DevExt->Pool9x;
    PP9_REC r;

    if (gP9Acct == NULL || p->Rec < 0 || p->Token == 0) {
        return NULL;
    }
    r = &gP9Acct->Rec[p->Rec];
    if (P9Load(&r->Owner) != p->Token) {
        p->Rec = -1;
        return NULL;
    }
    return r;
}

// Owner-only transition among the held states (RESERVED, OWNED, ISOLATED),
// without the lock: other loads act only on records whose owner is gone.
static VOID P9Mine(IN PHW_DEVICE_EXTENSION DevExt, ULONG To)
{
    PP9_REC r = P9MyRecord(DevExt);
    ULONG from;

    if (r == NULL) {
        return;
    }
    from = P9Load(&r->State);
    if (from == To) {
        return;
    }
    P9Xchg(&r->State, To);
    P9RecMark((ULONG)DevExt->Pool9x.Rec, r, from, P9C_NOTE);
}

// This instance has ended: its function's token is no longer live, so a
// record it still holds is resolved by the next load (P9C_DEAD).
static VOID P9EndLife(IN PHW_DEVICE_EXTENSION DevExt)
{
    PLAT_POOL9X *p = &DevExt->Pool9x;
    PP9_ACCT a = gP9Acct;
    P9_FUNC *f;

    if (p->Ended || a == NULL || p->Token == 0) {
        return;
    }
    p->Ended = TRUE;
    f = P9FindFunc(a, DevExt->BusNumber, DevExt->SlotNumber);
    if (f != NULL && P9CmpXchg(&f->Token, p->Token, p->Token) == p->Token) {
        P9Xchg(&f->Live, 0);
    }
}

// Boot-block work (P9P_*), done now if the lock is free; otherwise it stays
// pending and PlatCmLinkPoll retries it on a later timer tick or HwStartIo,
// one non-blocking attempt each (K2). Budget NULL in runtime callbacks.
// Pending work also keeps the timer at the link poll period (PlatCmLinkPollUs).
static VOID P9Work(IN PHW_DEVICE_EXTENSION DevExt, IN OUT PP9_STALLS Budget)
{
    PLAT_POOL9X *p = &DevExt->Pool9x;
    PP9_ACCT a = gP9Acct;
    PP9_REC r;
    ULONG i;

    if (p->Pending == 0 || a == NULL) {
        return;
    }
    if (!P9Lock(DevExt, Budget)) {
        DBGSTAGE(STG_P9_DEFER, p->Pending);
        return;
    }
    if (p->Pending & P9P_E2) {
        // E2 (p9bind.h), pended only once DisableAcked is set: state changes
        // under the lock, no VMM service.
        for (i = 0; i < P9_RECS; i++) {
            if (!(p->E2Mask & (1UL << i))) {
                continue;
            }
            r = &a->Rec[i];
            if (P9EvidenceE2((BOOLEAN)(P9Load(&r->State) == PLAT_OWN_QUARANTINED), P9Load(&r->Owner), r->Bus, r->Slot,
                             DevExt->BusNumber, DevExt->SlotNumber, p->DisableAcked)) {
                P9RecDisown(i, r, PLAT_OWN_RELEASED, P9C_E2);
            }
        }
        p->E2Mask = 0;
    }
    if (p->Pending & P9P_RELINQUISH) {
        r = P9MyRecord(DevExt);
        if (r != NULL) {
            P9RecDisown((ULONG)p->Rec, r, p->FinalState, P9C_RELINQUISH);
        }
        p->Rec = -1;
    }
    p->Pending = 0;
    P9Unlock();
}

// Takes a token and binds the function (under the lock). FALSE: no token.
static BOOLEAN P9TakeToken(IN PHW_DEVICE_EXTENSION DevExt, PP9_ACCT a, PBOOLEAN HasFunc)
{
    PLAT_POOL9X *p = &DevExt->Pool9x;
    P9_FUNC *f;
    ULONG i, prev = 0, slot = 0xFF;

    if (a->NextToken >= 0xFFFFFFFE) {
        return FALSE;
    }
    p->Token = ++a->NextToken;
    f = P9FindFunc(a, DevExt->BusNumber, DevExt->SlotNumber);
    for (i = 0; f == NULL && i < P9_FUNCS; i++) {
        if (a->Func[i].Bus == 0xFFFFFFFF || !a->Func[i].Live) {
            f = &a->Func[i];
        }
    }
    *HasFunc = (BOOLEAN)(f != NULL);
    if (f != NULL) {
        // A new HwFindAdapter for a function means its previous instance is
        // gone (R8c: 9x binds one adapter per PCI function).
        prev = f->Token;
        f->Bus = DevExt->BusNumber;
        f->Slot = DevExt->SlotNumber;
        f->Token = p->Token;
        f->Live = 1;
        slot = (ULONG)(f - a->Func);
    }
    DBGSTAGE(STG_P9_TOKEN, (p->Token & 0xFFFF) | (slot << 16) | ((prev & 0xFF) << 24));
    UNREFERENCED_PARAMETER(prev);
    UNREFERENCED_PARAMETER(slot);
    return TRUE;
}

// A RELEASED block about to be reused: record fields, residency and
// provenance, then its metadata page. Reads only; nothing is written to a
// block a previous load may still have reachable (it is RELEASED: isolated).
static BOOLEAN P9Validate(PP9_REC r, ULONG Pages)
{
    PP9_META m;
    ULONG bytes = Pages * PAGE_SIZE;
    BOOLEAN ok;

    if (r->Pages != Pages || (r->Linear & (PAGE_SIZE - 1)) || r->Handle != r->Linear ||
        !P9InArena(r->Linear, bytes) || r->Physical == 0 || (r->Physical & (PAGE_SIZE - 1)) ||
        r->Physical > 0xFFFFFFFFUL - bytes + 1) {
        DBGSTAGE(STG_P9_CHECK, 0);
        return FALSE;
    }
    if (!P9PagesPresent(r->Linear, bytes, r->Physical)) {
        DBGSTAGE(STG_P9_CHECK, 0);
        return FALSE;
    }
    m = (PP9_META)(r->Linear + (Pages - 1) * PAGE_SIZE);
    ok = (BOOLEAN)(m->Signature == P9_META_SIG && m->Physical == r->Physical && m->Linear == r->Linear &&
                   m->Pages == Pages);
    DBGSTAGE(STG_P9_CHECK, ok | ((m->Loads & 0xFFFFFF) << 8));
    return ok;
}

// A fresh, fixed, physically contiguous block below 4 GiB.
static BOOLEAN P9Allocate(ULONG Pages, PULONG Linear, PULONG Phys)
{
    ULONGLONG r;
    ULONG phys = 0;

    r = P9PageAllocate(Pages, P9_PG_SYS, 0, 0, 0, 0xFFFFF, &phys,
                       P9_PAGEFIXED | P9_PAGEUSEALIGN | P9_PAGECONTIG | P9_PAGEZEROINIT);
    if ((ULONG)r == 0 || phys == 0 || (phys & (PAGE_SIZE - 1))) {
        return FALSE;
    }
    if (!P9PagesPresent((ULONG)r, Pages * PAGE_SIZE, phys)) {
        P9PageFree((ULONG)r, 0);
        return FALSE;
    }
    *Linear = (ULONG)r;
    *Phys = phys;
    return TRUE;
}

// Fills the record this load holds RESERVED. It stays RESERVED: the engine
// moves it to OWNED in LcEnable (PlatDmaReachable), after this load's own
// acknowledged disable (or a controller found disabled).
static VOID P9Take(IN PHW_DEVICE_EXTENSION DevExt, LONG Index, ULONG Linear, ULONG Phys, ULONG Pages,
                   BOOLEAN Fresh, BOOLEAN HasFunc)
{
    PLAT_POOL9X *p = &DevExt->Pool9x;
    PP9_REC r = &gP9Acct->Rec[Index];
    PP9_META m;

    if (Fresh) {
        r->Handle = Linear;
        r->Linear = Linear;
        r->Physical = Phys;
        r->Pages = Pages;
        r->Loads = 0;
    }
    r->Flags = HasFunc ? 0 : P9RF_NOFUNC;
    r->Bus = DevExt->BusNumber;
    r->Slot = DevExt->SlotNumber;
    r->Gen = p->Entry ? p->Gen : 0;
    r->TableCookie = p->Entry ? p->TableCookie : 0;
    r->Loads++;
    p->Rec = Index;
    p->Linear = r->Linear;
    p->Phys = r->Physical;
    p->Pages = r->Pages;
    // The metadata page is never given to the controller.
    m = (PP9_META)(p->Linear + (Pages - 1) * PAGE_SIZE);
    m->Signature = P9_META_SIG;
    m->Physical = p->Phys;
    m->Linear = p->Linear;
    m->Pages = Pages;
    m->Loads++;
    m->LastToken = p->Token;
    m->LastBus = DevExt->BusNumber;
    m->LastSlot = DevExt->SlotNumber;
    m->LastGen = r->Gen;
}

// Under the lock: the claim carries this load's complete owner identity
// (token and function) from its first publication, so no other load can
// take the claimant for gone (P9OwnerAlive) while it validates or
// allocates (review: an overlapping HwFindAdapter of another function).
static VOID P9Claim(IN PHW_DEVICE_EXTENSION DevExt, PP9_REC r, BOOLEAN HasFunc)
{
    r->Flags = HasFunc ? 0 : P9RF_NOFUNC;
    r->Bus = DevExt->BusNumber;
    r->Slot = DevExt->SlotNumber;
    r->Owner = DevExt->Pool9x.Token;
}

// Installs the claimed record under the lock, after checking that this load
// still holds the claim. FALSE: lock busy or the claim lost.
static BOOLEAN P9TakeLocked(IN PHW_DEVICE_EXTENSION DevExt, LONG Index, ULONG Linear, ULONG Phys, ULONG Pages,
                            BOOLEAN Fresh, BOOLEAN HasFunc, IN OUT PP9_STALLS Budget)
{
    PP9_REC r = &gP9Acct->Rec[Index];

    if (!P9Lock(DevExt, Budget)) {
        return FALSE;
    }
    if (P9Load(&r->Owner) != DevExt->Pool9x.Token || P9Load(&r->State) != PLAT_OWN_RESERVED) {
        P9Unlock();
        return FALSE;
    }
    P9Take(DevExt, Index, Linear, Phys, Pages, Fresh, HasFunc);
    P9Unlock();
    return TRUE;
}

// Gives a record this load claimed from FREE, and never installed (Linear
// 0: no block was ever recorded in it), back as FREE, if this load still
// holds the claim. Lock busy: it stays RESERVED by this token, and the
// next load's scan frees it once the token has ended (P9C_DEAD).
static VOID P9Unclaim(IN PHW_DEVICE_EXTENSION DevExt, LONG Index, IN OUT PP9_STALLS Budget)
{
    PP9_REC r = &gP9Acct->Rec[Index];

    if (!P9Lock(DevExt, Budget)) {
        return;
    }
    if (P9Load(&r->Owner) == DevExt->Pool9x.Token && P9Load(&r->State) == PLAT_OWN_RESERVED && r->Linear == 0) {
        r->Owner = 0;
        P9Xchg(&r->State, PLAT_OWN_FREE);
        P9RecMark((ULONG)Index, r, PLAT_OWN_RESERVED, P9C_UNCLAIM);
    }
    P9Unlock();
}

// Chooses or allocates this load's block; 0 or a P9_FAIL_* reason (the find
// then fails before any CC write).
static ULONG P9Setup(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG Pages, OUT PULONG How, IN OUT PP9_STALLS Budget)
{
    PLAT_POOL9X *p = &DevExt->Pool9x;
    PP9_ACCT a = gP9Acct;
    PP9_REC r;
    ULONG i, st, linear, phys, pass, from;
    LONG pick, freeSlot;
    BOOLEAN hasFunc = FALSE;

    if (!P9Lock(DevExt, Budget)) {
        return P9_FAIL_LOCK;
    }
    if (!P9TakeToken(DevExt, a, &hasFunc)) {
        P9Unlock();
        return P9_FAIL_TOKEN;
    }
    P9Unlock();
    // A pass ends in a claim, a failure, or a RELEASED pick that failed
    // validation; that pick is then BAD and never picked again, so after at
    // most P9_RECS such passes the next one finds no RELEASED candidate and
    // takes a FREE record (or fails on the record or the budget).
    for (pass = 0; pass <= P9_RECS; pass++) {
        pick = -1;
        freeSlot = -1;
        if (!P9Lock(DevExt, Budget)) {
            return P9_FAIL_LOCK;
        }
        for (i = 0; i < P9_RECS; i++) {
            r = &a->Rec[i];
            st = P9Load(&r->State);
            if (st == PLAT_OWN_FREE) {
                if (freeSlot < 0) {
                    freeSlot = (LONG)i;
                }
                continue;
            }
            // Owner gone: settle what it left (P9C_DEAD). Without isolation
            // evidence a reachable block is quarantined; a block its owner
            // never gave to a controller, or gave up after isolation, is
            // released. A record claimed from FREE whose block was never
            // installed (RESERVED with Linear 0: the allocation failed or
            // the install lost the lock) holds no block: FREE again, so
            // the slot is not lost for the boot. Blocks that failed
            // validation stay out (BAD).
            if (!(r->Flags & P9RF_BAD) && !P9OwnerAlive(a, r)) {
                from = st;
                if (st == PLAT_OWN_OWNED) {
                    P9RecDisown(i, r, PLAT_OWN_QUARANTINED, P9C_DEAD);
                } else if (st == PLAT_OWN_RESERVED && r->Linear == 0) {
                    P9RecDisown(i, r, PLAT_OWN_FREE, P9C_DEAD);
                    if (freeSlot < 0) {
                        freeSlot = (LONG)i;
                    }
                    continue;
                } else if (st == PLAT_OWN_ISOLATED || st == PLAT_OWN_RESERVED) {
                    P9RecDisown(i, r, PLAT_OWN_RELEASED, P9C_DEAD);
                }
                UNREFERENCED_PARAMETER(from);
            }
            if (r->State == PLAT_OWN_QUARANTINED) {
                if (P9EvidenceE1(DevExt, r)) {
                    P9RecDisown(i, r, PLAT_OWN_RELEASED, P9C_E1);
                } else if (r->Bus == DevExt->BusNumber && r->Slot == DevExt->SlotNumber) {
                    p->E2Mask |= 1UL << i;          // until this load's controller is shown disabled
                }
            }
            if (r->State == PLAT_OWN_RELEASED && r->Pages == Pages && !(r->Flags & P9RF_BAD) && pick < 0) {
                pick = (LONG)i;
            }
        }
        if (pick >= 0) {
            r = &a->Rec[pick];
            P9Claim(DevExt, r, hasFunc);
            P9Xchg(&r->State, PLAT_OWN_RESERVED);
            P9RecMark((ULONG)pick, r, PLAT_OWN_RELEASED, P9C_CLAIM);
            P9Unlock();
            if (P9Validate(r, Pages)) {
                if (!P9TakeLocked(DevExt, pick, 0, 0, Pages, FALSE, hasFunc, Budget)) {
                    return P9_FAIL_LOCK;            // lock busy or the claim lost: nothing installed
                }
                *How = 2;
                return 0;
            }
            // Corrupt: kept RESERVED and BAD: never claimed, freed or
            // dereferenced again in this boot. Scan again. Written without
            // the lock: the record is RESERVED by this live token, and no
            // other load writes the Flags of such a record (P9Claim writes
            // only FREE or RELEASED ones, under the lock).
            r->Flags |= P9RF_BAD;
            P9RecMark((ULONG)pick, r, PLAT_OWN_RESERVED, P9C_BAD);
            continue;
        }
        if (freeSlot < 0) {
            P9Unlock();
            return P9_FAIL_RECORD;
        }
        if (!P9AcctCharge(a, Pages, STOR98_POOL_CAP_BLOCKS * Pages)) {
            P9Unlock();
            return P9_FAIL_BUDGET;
        }
        r = &a->Rec[freeSlot];
        r->LastOwner = 0;
        r->Handle = r->Linear = r->Physical = r->Pages = 0;
        P9Claim(DevExt, r, hasFunc);
        P9Xchg(&r->State, PLAT_OWN_RESERVED);
        P9RecMark((ULONG)freeSlot, r, PLAT_OWN_FREE, P9C_CLAIM);
        P9Unlock();
        if (!P9Allocate(Pages, &linear, &phys)) {
            P9Unclaim(DevExt, freeSlot, Budget);
            P9AcctUncharge(a, Pages);
            return P9_FAIL_ALLOC;
        }
        if (!P9TakeLocked(DevExt, freeSlot, linear, phys, Pages, TRUE, hasFunc, Budget)) {
            P9PageFree(linear, 0);                  // never installed, never given to a controller
            P9Unclaim(DevExt, freeSlot, Budget);
            P9AcctUncharge(a, Pages);
            return P9_FAIL_LOCK;
        }
        *How = 1;
        return 0;
    }
    return P9_FAIL_PASSES;
}

// The region fields of the device extension (platform.h, Acquire), and the
// cache treatment the port's uncached extension gets (platform.c).
static BOOLEAN P9Install(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG Size)
{
    PLAT_POOL9X *p = &DevExt->Pool9x;
    ULONG dmaPages = p->Pages - 1;

    if (Size > dmaPages * PAGE_SIZE) {
        return FALSE;
    }
    if (_PageModifyPermissions(p->Linear >> PAGE_SHIFT, dmaPages, 0xFFFFFFFF, PC_CACHEWT | PC_CACHEDIS) ==
        0xFFFFFFFF) {
        if (ApplyCacheMode((PUCHAR)p->Linear, dmaPages, PTE_CACHE_UC) != dmaPages) {
            return FALSE;
        }
    }
    DevExt->UncachedExtensionBase = (PVOID)p->Linear;
    DevExt->UncachedExtensionPhys.QuadPart = p->Phys;
    DevExt->UncachedExtensionSize = Size;
    DevExt->UncachedExtensionOffset = 0;
    return TRUE;
}

#ifdef STOR98_DBGPORT
// Test builds only: 9x has no ArgumentString, so the removal tests take a few
// engine test shapes from HKLM\Software\stor98\Q9x (cm9xctl "set q9 Name N"),
// read once per load: ShnFlush (SRB_FUNCTION_FLUSH runs the restartable
// shutdown), Frag (KiB, largest child), IsrProbe (the timer calls this
// adapter's ISR while a removal transaction runs, as a shared line would).
static ULONG P9TestDword(ULONG Hk, PCHAR Name)
{
    ULONG v = 0, type = 0, cb = sizeof(v);

    if (Hk != 0 && P9RegQueryValueEx(Hk, Name, NULL, &type, &v, &cb) == 0 && type == P9_REG_DWORD && cb == sizeof(v)) {
        return v;
    }
    return 0;
}

static VOID P9TestOptions(IN PHW_DEVICE_EXTENSION DevExt)
{
    ULONG hk = 0, frag;

    if (P9RegOpenKey(P9_HKLM, "Software\\stor98\\Q9x", &hk) != 0) {
        return;
    }
    DevExt->Tun.ShnFlush = P9TestDword(hk, "ShnFlush");
    DevExt->Pool9x.TestIsrProbe = P9TestDword(hk, "IsrProbe");
    DevExt->Pool9x.TestShnPrime = P9TestDword(hk, "ShnPrime");
    DevExt->Pool9x.TestBudgetSkewMs = P9TestDword(hk, "BudgetSkewMs");
    DevExt->Pool9x.TestCorruptAcct = P9TestDword(hk, "CorruptAcct");
    frag = P9TestDword(hk, "Frag");
    DevExt->Tun.FragBytes = (frag <= 64) ? frag * 1024 : 0;
    P9RegCloseKey(hk);
    DBGSTAGE(STG_P9_TEST, (DevExt->Tun.ShnFlush ? 1 : 0) | (DevExt->Pool9x.TestIsrProbe ? 2 : 0) | ((frag & 0xFF) << 8));
}

// Test (CorruptAcct, as q9x.c): this boot's accounting is damaged when the
// instance relinquishes its blocks, so the next load must fail closed
// (with the CM's proof present): 1 the boot block's signature; 2 the
// reference truncated; 3 its BootCookie changed (the check word no longer
// matches); 4 the reference deleted and the boot marker malformed.
static VOID P9TestCorruptAcct(IN PHW_DEVICE_EXTENSION DevExt)
{
    ULONG mode = DevExt->Pool9x.TestCorruptAcct, hk = 0, type = 0, cb, junk[2] = { 0, 0 };
    P9_ACCT_REF ref;

    if (mode == 0) {
        return;
    }
    if (mode == 1 && gP9Acct != NULL) {
        gP9Acct->Signature ^= 1;
    } else if (P9RegCreateKey(P9_HKLM, P9_ACCT_KEY, &hk) == 0) {
        if (mode == 4) {
            P9RegDeleteValue(hk, P9_ACCT_REF_VALUE);
            P9RegSetValueEx(hk, P9_ACCT_BOOT_VALUE, 0, P9_REG_BINARY, junk, sizeof(junk));
        } else {
            cb = sizeof(ref);
            if (P9RegQueryValueEx(hk, P9_ACCT_REF_VALUE, NULL, &type, &ref, &cb) == 0 && cb == sizeof(ref)) {
                if (mode == 3) {
                    ref.BootCookie ^= 0x00010000;
                }
                P9RegSetValueEx(hk, P9_ACCT_REF_VALUE, 0, P9_REG_BINARY, &ref,
                                mode == 2 ? sizeof(ref) - 4 : sizeof(ref));
            }
        }
        P9RegCloseKey(hk);
    }
    DBGSTAGE(STG_P9_TEST, 0x20000 | mode);
}
#endif

// ---------------------------------------------------------------------------
// The provider (platform.h)
// ---------------------------------------------------------------------------

//
// Acquire, from HwFindAdapter after the DMA negotiation and before any CC
// write. Never writes a block a previous load may still reach: only a FREE
// record (a fresh, zeroed _PageAllocate block) or a RELEASED one (isolated
// and given up) is claimed, and the engine zeroes its queues only in
// LcEnable, after this load's own acknowledged disable. No silent fallback
// to the port's uncached extension: FALSE fails the find (MD-4).
//
static BOOLEAN P9Acquire(IN PHW_DEVICE_EXTENSION DevExt, IN PPORT_CONFIGURATION_INFORMATION ConfigInfo,
                         IN ULONG Size)
{
    PLAT_POOL9X *p = &DevExt->Pool9x;
    ULONG pages = ((Size + PAGE_SIZE - 1) >> PAGE_SHIFT) + 1;     // + the metadata page
    ULONG prc, rc, how = 0;
    P9_STALLS budget;

    UNREFERENCED_PARAMETER(ConfigInfo);
    // One stall budget for every lock attempt of this HwFindAdapter (K2).
    P9StallsInit(&budget, P9_FIND_STALLS);

    // HwFindAdapter again on this extension (a resume): keep this load's own
    // block; the engine re-runs its disable before reusing anything.
    if (DevExt->Resumed && P9MyRecord(DevExt) != NULL && p->Pages == pages && !p->Ended) {
        if (!P9Install(DevExt, Size)) {
            DBGSTAGE(STG_P9_FIND, P9_FAIL_ALLOC | (3 << 8));
            return FALSE;
        }
        DBGSTAGE(STG_P9_FIND, 0 | (3 << 8) | (((ULONG)p->Rec & 0xF) << 12) | (pages << 16));
        return TRUE;
    }

    RtlZeroMemory(p, sizeof(*p));
    p->Rec = -1;
#ifdef STOR98_DBGPORT
    P9TestOptions(DevExt);
#endif
    // The proof first: it is also the boot block's current-boot authority.
    prc = P9ReadProof(DevExt);
    if (P9AcctGet(p->ProofCookie) == NULL) {
        DBGSTAGE(STG_P9_FIND, P9_FAIL_ACCT);
        return FALSE;
    }
    rc = P9Rendezvous(DevExt, prc);
    p->RendezvousRc = rc;
    DBGSTAGE(STG_P9_RENDEZVOUS, (rc & 0xFF) | ((ULONG)(p->Entry != NULL) << 8) | ((p->Gen & 0xFFFF) << 16));
    rc = P9Setup(DevExt, pages, &how, &budget);
    if (rc == 0 && !P9Install(DevExt, Size)) {
        // Never given to a controller: release it at once (lock permitting;
        // otherwise the ended token releases it at the next load).
        p->FinalState = PLAT_OWN_RELEASED;
        p->Pending |= P9P_RELINQUISH;
        P9Work(DevExt, &budget);
        rc = P9_FAIL_ALLOC;
    }
    DBGSTAGE(STG_P9_FIND, rc | (how << 8) | (((ULONG)p->Rec & 0xF) << 12) | (pages << 16));
    if (rc != 0) {
        P9EndLife(DevExt);
        return FALSE;
    }
    return TRUE;
}

// One page of the block: its own page-table entry (PLAN 0.4), which must be
// the page the contiguous allocation recorded.
static BOOLEAN P9PagePa(IN PHW_DEVICE_EXTENSION DevExt, IN PVOID Va, OUT PHYSICAL_ADDRESS *Pa)
{
    PLAT_POOL9X *p = &DevExt->Pool9x;
    ULONG va = (ULONG)Va, off, pte = 0;

    Pa->QuadPart = 0;
    if (p->Linear == 0 || va < p->Linear) {
        return FALSE;
    }
    off = va - p->Linear;
    if (off >= (p->Pages - 1) * PAGE_SIZE) {
        return FALSE;                               // outside the DMA pages (or the metadata page)
    }
    if (!P9CopyPageTable(va >> PAGE_SHIFT, 1, &pte, 0) || !(pte & P9_PTE_PRESENT) ||
        (pte & ~(PAGE_SIZE - 1)) != p->Phys + (off & ~(PAGE_SIZE - 1))) {
        return FALSE;
    }
    Pa->LowPart = (pte & ~(PAGE_SIZE - 1)) | (off & (PAGE_SIZE - 1));
    return TRUE;
}

//
// The engine's allocation records changed (platform.c). The engine keeps one
// record per carved block; the boot block keeps one per adapter block, so
// the record follows the most exposed of the blocks (docs/INTEGRATION-0.2.md):
//   a block OWNED                      -> record OWNED (from RESERVED or ISOLATED)
//   no block OWNED, one ISOLATED       -> record ISOLATED
//   every block relinquished (RELEASED or QUARANTINED; PlatDmaRelinquish at
//   the end of EngStop, or at a removal transaction's acknowledged disable
//   below, after the queue gates close: N2 as revised in the int-0.2 review)
//                                      -> record QUARANTINED if any block is
//                                         QUARANTINED, else RELEASED; then the
//                                         instance ends.
// RELEASED is never published while a block is held. The first OWNED is
// also E2 for this function's QUARANTINED records: LcEnable runs only on a
// controller this load has shown disabled (CC.EN=0, RDY=0). Since the owner
// decision of 2026-10-18 a record whose entry only shows a later generation
// (no DOWN_QUIESCED) is released by E2 only, never by E1 (p9bind.h).
//
// Every block relinquished (none held): the record follows, owner-side state
// first, so a fallback (lock busy) resolves the same way through P9C_DEAD at
// the next load; then the instance ends.
static VOID P9Finish(IN PHW_DEVICE_EXTENSION DevExt, IN BOOLEAN Quarantine)
{
    PLAT_POOL9X *p = &DevExt->Pool9x;

    P9Mine(DevExt, Quarantine ? PLAT_OWN_OWNED : PLAT_OWN_ISOLATED);
    p->FinalState = Quarantine ? PLAT_OWN_QUARANTINED : PLAT_OWN_RELEASED;
    p->Pending |= P9P_RELINQUISH;
    P9Work(DevExt, NULL);                           // K2: non-blocking, deferred if busy
    P9EndLife(DevExt);
#ifdef STOR98_DBGPORT
    P9TestCorruptAcct(DevExt);
#endif
}

// TRUE if this instance still holds its record but none of its blocks is
// held (none was carved: a resume that failed before LcCarve, Codex round 2
// on fix-0.3.0.1), so no block transition will ever finish the record.
static BOOLEAN P9RecordOnly(IN PHW_DEVICE_EXTENSION DevExt)
{
    ULONG i;
    PPLAT_DMA_BLOCK b;

    if (DevExt->Pool9x.Ended || P9MyRecord(DevExt) == NULL) {
        return FALSE;
    }
    for (i = 0; i < PLAT_DMA_BLOCKS; i++) {
        b = &DevExt->DmaBlocks[i];
        if (b->Provider == PLAT_DMA_POOL && (b->Owner == PLAT_OWN_RESERVED || b->Owner == PLAT_OWN_OWNED ||
                                             b->Owner == PLAT_OWN_ISOLATED || b->Owner == PLAT_OWN_QUARANTINED)) {
            return FALSE;
        }
    }
    return TRUE;
}

// Before a relinquish that carries controller-wide isolation evidence (a
// DOWN_QUIESCED statement, a removal's acknowledged disable): a record-only
// instance is released here, since the relinquish's own finish (no
// evidence) would quarantine an OWNED record (Codex round 3).
static VOID P9FinishIsolated(IN PHW_DEVICE_EXTENSION DevExt)
{
    if (P9RecordOnly(DevExt)) {
        P9Finish(DevExt, FALSE);
    }
}

static VOID P9Note(IN PHW_DEVICE_EXTENSION DevExt, IN PPLAT_DMA_BLOCK Block, IN UCHAR Owner)
{
    PLAT_POOL9X *p = &DevExt->Pool9x;
    ULONG i, held = 0, owned = 0, isolated = 0, quarantined = 0;
    PPLAT_DMA_BLOCK b;

    if (Block == NULL) {
        // PlatDmaRelinquish has finished (platform.c). A record no block
        // transition reached is finished here without isolation evidence: a
        // record this load (or the session before a resume) gave to a
        // controller (OWNED) is quarantined, anything else released.
        if (Owner == PLAT_OWN_FREE && P9RecordOnly(DevExt)) {
            P9Finish(DevExt, (BOOLEAN)(P9Load(&P9MyRecord(DevExt)->State) == PLAT_OWN_OWNED));
        }
        return;
    }
    if (P9MyRecord(DevExt) == NULL) {
        return;
    }
    for (i = 0; i < PLAT_DMA_BLOCKS; i++) {
        b = &DevExt->DmaBlocks[i];
        if (b->Provider != PLAT_DMA_POOL) {
            continue;
        }
        switch (b->Owner) {
        case PLAT_OWN_RESERVED:
            held++;
            break;
        case PLAT_OWN_OWNED:
            held++;
            owned++;
            break;
        case PLAT_OWN_ISOLATED:
            held++;
            isolated++;
            break;
        case PLAT_OWN_QUARANTINED:
            quarantined++;
            break;
        default:
            break;
        }
    }
    switch (Owner) {
    case PLAT_OWN_OWNED:
        P9Mine(DevExt, PLAT_OWN_OWNED);
        // From PlatDmaReachable in LcEnable, which runs only after this
        // load's acknowledged disable or on a controller found disabled
        // (CC.EN=0, RDY=0): E2 holds from here on.
        p->DisableAcked = TRUE;
        if (p->E2Mask != 0) {
            p->Pending |= P9P_E2;
        }
        P9Work(DevExt, NULL);                       // K2: non-blocking, deferred if busy
        break;
    case PLAT_OWN_ISOLATED:
        if (owned == 0) {
            P9Mine(DevExt, PLAT_OWN_ISOLATED);
        }
        break;
    case PLAT_OWN_RELEASED:
    case PLAT_OWN_QUARANTINED:
        if (held == 0) {
            P9Finish(DevExt, (BOOLEAN)(quarantined != 0));
        }
        break;
    default:
        break;
    }
    UNREFERENCED_PARAMETER(isolated);
}

const PLAT_DMA_PROVIDER PlatDmaPoolProvider = {
    PLAT_DMA_POOL,
    TRUE,                       // a quarantined block stays owned past removal and unload (MD-4)
    P9Acquire,
    P9PagePa,
    P9Note
};

// ---------------------------------------------------------------------------
// Interface 3 link-state receiver (evidence 2, PLAN 0.10; R5e, R8, R12):
// the 9x side of the platform.h CM link-state hooks, on the entry the
// rendezvous in P9Acquire bound. The MD-5 removal transaction (REMOVE_PENDING,
// RemoveAck) follows at the end of this file.
// ---------------------------------------------------------------------------

// The entry this instance is bound to and may still act on (R8d).
static PSTOR98LS_ENTRY P9MyEntry(IN PHW_DEVICE_EXTENSION DevExt)
{
    PLAT_POOL9X *p = &DevExt->Pool9x;

    if (p->Entry == NULL || p->IdentityLost || p->Ended) {
        return NULL;
    }
    return (PSTOR98LS_ENTRY)p->Entry;
}

// The entry the link-state receiver reads (K1): the bound one while this
// instance may act on it, and also after a removal transaction ended the
// instance with its acknowledgement written (P9R_DONE: its devnode stays
// until the CM's Remove_SubTree, which may fail, R11 O6), so a DOWN_QUIESCED
// for its own generation is still acknowledged; PlatCmLinkPoll then accepts
// no other generation (a later one belongs to a newer instance, R8c).
static PSTOR98LS_ENTRY P9LinkEntry(IN PHW_DEVICE_EXTENSION DevExt)
{
    PLAT_POOL9X *p = &DevExt->Pool9x;

    if (p->Entry == NULL || p->IdentityLost) {
        return NULL;
    }
    if (p->Ended && !(p->RmAccepted && p->RmStep == P9R_DONE)) {
        return NULL;
    }
    return (PSTOR98LS_ENTRY)p->Entry;
}

// Stops acting on the entry for the rest of this instance's life (R5e, R8d).
static VOID P9LoseIdentity(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG Reason)
{
    if (!DevExt->Pool9x.IdentityLost) {
        DevExt->Pool9x.IdentityLost = TRUE;
        DBGSTAGE(STG_P9_IDENTITY, Reason);
    }
    UNREFERENCED_PARAMETER(Reason);
}

// R10: a timer callback every STOR98LS9X_POLL_MS, idle or not, in every
// engine state, while this instance can still take a statement from its
// entry and has not applied one yet (K1: also terminal states, and after a
// removal's acknowledgement), while boot-block work waits for the lock
// (K2: also after the instance has ended), and while the late lookup runs.
ULONG PlatCmLinkPollUs(IN PHW_DEVICE_EXTENSION DevExt)
{
    PLAT_POOL9X *p = &DevExt->Pool9x;

    if (p->RmAccepted && !p->RmAcked) {
        return ENG_TICK_FAST_US;                    // removal transaction steps
    }
    if (p->Pending != 0 || p->LateOn || (P9LinkEntry(DevExt) != NULL && p->LinkApplied == 0)) {
        return STOR98LS9X_POLL_MS * 1000;
    }
    return 0;
}

// One validated look at the bound entry, shared by every reader of it (the
// link-state poll and the removal transaction): R6c snapshot, then R5e and
// the R8d identity rules; a failure ends this instance's use of the entry
// for good (no evidence, no acknowledgement of any kind). P9E_OK with *S
// filled; P9E_BUSY: being updated, try at the next poll; P9E_LOST.
#define P9E_OK      0
#define P9E_BUSY    1
#define P9E_LOST    2

static ULONG P9EntryRead(IN PHW_DEVICE_EXTENSION DevExt, OUT P9_SNAP *S)
{
    PLAT_POOL9X *p = &DevExt->Pool9x;
    PSTOR98LS_ENTRY e = P9LinkEntry(DevExt);
    ULONG bad;

    if (e == NULL) {
        return P9E_LOST;
    }
    if (!P9Snapshot(e, S)) {
        return P9E_BUSY;                            // R6c: next poll
    }
    bad = P9SnapCheck(S);
    if (bad != 0) {
        p->BadSnapshots++;
        P9LoseIdentity(DevExt, bad);
        return P9E_LOST;
    }
    if (S->Bus != DevExt->BusNumber || S->Slot != DevExt->SlotNumber) {
        P9LoseIdentity(DevExt, 0x20);
        return P9E_LOST;
    }
    if (S->Gen < p->Gen) {
        P9LoseIdentity(DevExt, 0x21);
        return P9E_LOST;
    }
    return P9E_OK;
}

//
// The late lookup (p9bind.h; owner decision 2026-10-16), from PlatCmLinkPoll
// (the engine timer and HwStartIo): one scan per call, no spinning, only
// while the find kept a validated table without an entry. Once found, the
// entry is bound as at the find (generation, RemoveSequence at binding as
// p9bind.h rules it for a late bind, R8d) and this load's pool record takes
// the generation, so a later load
// can apply E1 to it as to a record bound at the find.
//
static VOID P9LateBind(IN PHW_DEVICE_EXTENSION DevExt)
{
    PLAT_POOL9X *p = &DevExt->Pool9x;
    ULONG rc, index = 0, el;
    P9_SNAP pick;
    PP9_REC r;

    if (p->Ended || p->Entry != NULL || p->IdentityLost) {
        p->LateOn = 0;
        return;
    }
    el = P9GetSystemTime() - p->LateStartMs;
    pick.Gen = 0;
    rc = P9LateLookup((PSTOR98LS_TABLE)p->Table, p->TableCookie, DevExt->BusNumber, DevExt->SlotNumber, el,
                      &p->LateOn, &p->LateFloor, &index, &pick);
    p->LateScans++;
    if (rc == P9B_BOUND) {
        P9BindTo(DevExt, index, &pick);
        r = P9MyRecord(DevExt);
        if (r != NULL && r->Gen == 0) {
            // Owner-only, before the cookie: a reader that sees this table's
            // cookie also sees the generation (E1 checks both).
            P9Xchg(&r->Gen, p->Gen);
            P9Xchg(&r->TableCookie, p->TableCookie);
        }
        p->RendezvousRc = 0;
    } else if (rc == P9B_TABLE || rc == P9B_DUP || rc == P9B_BAD) {
        p->Table = NULL;                            // no channel: never read again by this load
    }
    if (rc != p->LateRc || p->LateOn == 0) {
        DBGMARK(STG_P9_LATE, rc | (((el / 10) & 0xFFF) << 4) | ((pick.Gen & 0xFF) << 16) |
                ((DevExt->BusNumber & 0xFF) << 24));
    }
    p->LateRc = rc;
}

// The rendezvous ran in P9Acquire (HwFindAdapter), once per load.
VOID PlatCmLinkOpen(IN PHW_DEVICE_EXTENSION DevExt, IN PCHAR ArgumentString)
{
    UNREFERENCED_PARAMETER(DevExt);
    UNREFERENCED_PARAMETER(ArgumentString);
}

//
// From the engine's timer, HwStartIo and gone state. Reports a statement
// once: DOWN_QUIESCED for the bound generation or a later one, or, once
// presence is lost, a later generation on the same entry (R8b: the bound
// one is then quiesced too), until PlatCmLinkAcknowledge. The engine polls it from every timer tick
// before its state's own step (K1), so a statement is taken in any state.
// Also retries boot-block work deferred because the lock was busy (K2: one
// non-blocking attempt per call, also after the instance has ended).
//
ULONG PlatCmLinkPoll(IN PHW_DEVICE_EXTENSION DevExt)
{
    PLAT_POOL9X *p = &DevExt->Pool9x;
    P9_SNAP s;
    BOOLEAN lost = FALSE;

    if (p->Pending != 0) {
        P9Work(DevExt, NULL);
    }
    if (p->LateOn) {
        P9LateBind(DevExt);
    }
    if (P9LinkEntry(DevExt) == NULL || p->LinkApplied != 0) {
        return PLAT_LINK_NO_INFO;
    }
    if (p->LinkGen != 0) {
        return PLAT_LINK_DOWN_QUIESCED;             // reported, not yet acknowledged
    }
    if (P9EntryRead(DevExt, &s) != P9E_OK) {
        return PLAT_LINK_NO_INFO;
    }
    if (p->Ended || (p->RmAccepted && !p->RmAcked)) {
        // A removal's terminal instance (P9LinkEntry), or an open removal
        // transaction (R9a: LinkGeneration does not change while
        // REMOVE_PENDING is set): any other generation on the entry means
        // this instance has lost its identity (as P9RmCheck rules, R8d), so
        // it takes no evidence and writes no acknowledgement of any kind.
        if (s.Gen != p->Gen) {
            P9LoseIdentity(DevExt, p->Ended ? 0x25 : 0x24);
            return PLAT_LINK_NO_INFO;
        }
        if (p->Ended && !(s.Flags & STOR98LS_DOWN_QUIESCED)) {
            return PLAT_LINK_NO_INFO;
        }
    }
    // p9bind.h, P9LinkSee (owner decision 2026-10-18): a newer generation
    // without DOWN_QUIESCED is evidence only once presence is lost (the
    // engine is gone, or CSTS reads all-ones now); while the controller
    // still answers it is identity loss and the drive keeps running.
    if (P9LinkNeedsPresence(&s, p->Gen)) {
        lost = (BOOLEAN)(DevExt->State == ENG_ST_GONE || NvmeReadReg32(DevExt, NVME_REG_CSTS) == 0xFFFFFFFF);
    }
    switch (P9LinkSee(&s, p->Gen, lost)) {
    case P9LS_EVIDENCE:
        p->LinkGen = s.Gen;
        // AckGeneration is written only for this instance's own generation
        // (R12 S3-S4, R8c). A later generation is evidence that the bound
        // one is quiesced (R8b) but belongs to a newer instance: applied,
        // never acknowledged from here.
        p->LinkQuiesced = (BOOLEAN)((s.Flags & STOR98LS_DOWN_QUIESCED) != 0 && s.Gen == p->Gen);
        DBGMARK(STG_P9_LS_SEEN, (s.Gen & 0xFF) | ((p->Gen & 0xFF) << 8) | ((ULONG)DevExt->State << 16) |
                ((DevExt->BusNumber & 0xFF) << 24));
        return PLAT_LINK_DOWN_QUIESCED;
    case P9LS_LOST:
        // A newer generation while the controller answers (a CM that
        // incremented it without a new tunnel), or, defensively, an older
        // one: no evidence, nothing completed, no acknowledgement (R8d).
        P9LoseIdentity(DevExt, 0x23);
        return PLAT_LINK_NO_INFO;
    case P9LS_UP:
        return PLAT_LINK_UP;
    default:
        return PLAT_LINK_NO_INFO;
    }
}

//
// The engine applied the statement (held requests completed, its blocks
// ISOLATED): AckGeneration, one interlocked store after the work (R6d,
// R12), only for a DOWN_QUIESCED statement.
//
VOID PlatCmLinkAcknowledge(IN PHW_DEVICE_EXTENSION DevExt)
{
    PLAT_POOL9X *p = &DevExt->Pool9x;
    // Taken before the relinquish below ends this instance (P9LinkEntry is
    // then NULL, unless a removal transaction already acknowledged): the
    // acknowledgement still goes to the bound entry.
    PSTOR98LS_ENTRY e = P9LinkEntry(DevExt);
    ULONG written = 0;

    if (p->LinkGen == 0) {
        return;
    }
    // R12 S3: cleanup first, the acknowledgement last. The statement is
    // controller-wide isolation for this instance, which never uses the
    // controller again (the adapter stays failed or gone; a returning link
    // is a new devnode and load, R8c), so its blocks are relinquished here
    // (ISOLATED -> RELEASED; the queue gates close first) and its owner token
    // retired (P9EndLife, from P9Note) before AckGeneration. Without this a
    // surprise removal left the records ISOLATED with a live owner, which no
    // later load at another PCI location could reclaim: on Me (no stop
    // callback) repeated removals with changing tunnel bus numbers used up
    // the boot-wide budget (Codex round 1 on stor98-0.3, P2-2). A second
    // relinquish (EngStop, or an open removal transaction) changes nothing.
    // The boot-block record update is one non-blocking attempt (K2); if the
    // lock is busy it stays pending and PlatCmLinkPoll retries it, and with
    // the owner-side state final and the token ended any later load resolves
    // the record the same way (P9C_DEAD), so the release is complete for the
    // acknowledgement either way.
    // No block carved (a resume that failed before LcCarve): the statement
    // is controller-wide isolation, so the record is released first.
    P9FinishIsolated(DevExt);
    PlatDmaRelinquish(DevExt);
    if (e != NULL && p->LinkQuiesced) {
        P9Xchg(&e->AckGeneration, p->LinkGen);
        written = 1;
    }
    DBGMARK(STG_P9_LS_ACK, (p->LinkGen & 0xFF) | (written << 16) | ((DevExt->BusNumber & 0xFF) << 24));
    p->LinkApplied = p->LinkGen;
    p->LinkGen = 0;
    UNREFERENCED_PARAMETER(written);
}


// ---------------------------------------------------------------------------
// The removal transaction (interface 3 R9-R11, MD-5; D2). Ported from the
// Q-9x prototype (src/q9x.c, Q9Rm*) onto the engine: the engine's internal
// Flush, its child and parent bookkeeping, and the DMA pool's relinquish.
// The request is seen by P9RmCheck from the timer hook and HwStartIo (R9b);
// the timer hook then owns the adapter and drives the steps at the fast
// tick: drain (2 s), ordered Flush (2 s), CC.SHN and SHST (2 s), CC.EN=0
// and RDY=0 (min(CAP.TO, 5 s)), all inside one overall budget from the
// first sight of the request (R10): the disable starts by DISABLE_BY
// whatever step is running, and a disable not acknowledged by ACK_BY is
// FAILED. With RDY=0 acknowledged the instance's children are settled with
// isolation evidence, its outstanding requests completed NO_DEVICE, its
// blocks relinquished (RELEASED), and only then is RemoveAck written: CLEAN
// when drain, Flush (every write covered) and SHST all completed, otherwise
// ISOLATED_NOT_CLEAN. Without an acknowledged disable it is FAILED: requests
// stay held, the blocks stay OWNED, and the instance waits for presence loss
// (the CM isolates first, R11 O5), then hands over to the engine's gone
// state (evidence 2 or the MD-1 deadline). Accepting makes the instance
// terminal (R9d); the acknowledgement is written once and never changed
// (R9c); a CM retry is the same request left in place and changes nothing;
// a later RemoveSequence is an overlapping request and is ignored (R9b).
// ---------------------------------------------------------------------------

#define P9_RM_STEP_MS       2000    // drain, Flush, SHST: each (R10)
#define P9_RM_DISABLE_MS    5000    // RDY=0 wait cap
// R10 (final): the request is seen at most POLL + LATE after publication,
// and the check that acts on a threshold can itself come POLL + LATE after
// it, so measured from first sight every decision is due by ACK_BOUND -
// 2 * (POLL + LATE) = 8500 ms, and the disable is requested from the first
// check at or after 8500 - 3000 = 5500 ms whatever phase is running.
#define P9_RM_ACK_BY_MS     (STOR98LS9X_ACK_BOUND_MS - 2 * (STOR98LS9X_POLL_MS + STOR98LS9X_LATE_MS))
#define P9_RM_DISABLE_BY_MS (P9_RM_ACK_BY_MS - 3000)
#define P9_RM_FAILED_US     100000  // FAILED: presence check while waiting for the CM's isolation

static ULONG P9Now(VOID)
{
    return P9GetSystemTime();
}

static ULONG P9Csts(IN PHW_DEVICE_EXTENSION DevExt)
{
    return NvmeReadReg32(DevExt, NVME_REG_CSTS);
}

// The timer period after P9R_DONE (PlatCmRemoveTimer's return value).
static ULONG P9RmDoneTimer(IN PHW_DEVICE_EXTENSION DevExt)
{
    ULONG us = PlatCmLinkPollUs(DevExt);

    return us != 0 ? us : PLAT_RM_NO_REARM;
}

static VOID P9RmStepTo(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG Step)
{
    PLAT_POOL9X *p = &DevExt->Pool9x;

    p->RmStep = Step;
    p->RmStepMs = P9Now();
    DBGSTAGE(STG_P9_RM_STEP, Step | ((p->RmStepMs - p->RmStartMs) << 8));
}

// R9b: the first request after binding with a RemoveSequence other than the
// one at binding, for this instance's own generation. Reads only.
static VOID P9RmCheck(IN PHW_DEVICE_EXTENSION DevExt)
{
    PLAT_POOL9X *p = &DevExt->Pool9x;
    PSTOR98LS_ENTRY e = P9MyEntry(DevExt);
    P9_SNAP s;
    ULONG seq;

    if (e == NULL || p->LinkApplied != 0 || p->RmAcked) {
        return;
    }
    // The same validation as the link-state poll (P9EntryRead): an invalid
    // or foreign observation ends this instance's use of the entry, so an
    // active transaction then never publishes its acknowledgement (R5e, R8d).
    if (P9EntryRead(DevExt, &s) != P9E_OK) {
        return;
    }
    // p9bind.h; p->RmSeq is 0 until a request is accepted (RmAccepted).
    switch (P9RmSee(&s, p->Gen, p->BindSeq, &p->RmSeq)) {
    case P9RM_OTHER_GEN:
        // Another generation on the entry: outside a transaction the
        // link-state poll rules it (P9LinkSee: evidence or identity loss);
        // inside one this instance stops acting on the entry (no
        // acknowledgement).
        if (p->RmAccepted) {
            P9LoseIdentity(DevExt, 0x24);
        }
        return;
    case P9RM_OVERLAP:
        seq = s.RemSeq & STOR98LS9X_SEQ_MASK;
        if (seq != p->RmOverlap) {
            p->RmOverlap = seq;                     // logged once; the first request stays
            DBGSTAGE(STG_P9_RM_OVERLAP, seq);
        }
        return;
    case P9RM_ACCEPT:
        break;
    default:
        return;                                     // none, or the accepted one again
    }
    seq = p->RmSeq;
    p->RmAccepted = TRUE;
    p->RmEntry = e;
    p->RmStartMs = P9Now();
    p->RmStepMs = p->RmStartMs;
    p->RmStep = P9R_DECIDE;
    p->RmDetail = 0;
    DBGMARK(STG_P9_RM_SEEN, (seq & 0xFFFF) | ((ULONG)DevExt->State << 16) | ((p->Gen & 0xFF) << 24));
}

// R9c: once, one interlocked store, after the work it reports (R6d).
static VOID P9RmAck(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG Outcome)
{
    PLAT_POOL9X *p = &DevExt->Pool9x;
    PSTOR98LS_ENTRY e = (PSTOR98LS_ENTRY)p->RmEntry;
    BOOLEAN published = FALSE;

    if (p->RmAcked) {
        return;
    }
    p->RmOutcome = Outcome;
    published = P9RmAckOnce((e != NULL && !p->IdentityLost) ? &e->RemoveAck : NULL, &p->RmAcked, p->RmSeq,
                            Outcome);
    // Bit 7 of the outcome byte: decided but not published (identity lost).
    DBGMARK(STG_P9_RM_ACK, (Outcome & 0x7F) | (published ? 0 : 0x80) | ((p->RmSeq & 0xFFFF) << 8) |
            ((p->RmDetail & 0xFF) << 24));
    UNREFERENCED_PARAMETER(published);
}

// Completes every request the engine still holds (terminal status, R9d).
static VOID P9RmFinishParents(IN PHW_DEVICE_EXTENSION DevExt)
{
    ULONG i;
    PENG_PARENT q;

    for (i = 0; i < ENG_MAX_PARENTS; i++) {
        q = &DevExt->Parents[i];
        if (q->State == ENG_PA_FREE || q->State == ENG_PA_PARKED) {
            continue;
        }
        q->Cancel = ENG_CANCEL_GONE;
        EngFinishParent(DevExt, i);
    }
    for (i = 0; i < ENG_MAX_PARENTS; i++) {
        q = &DevExt->Parents[i];
        if (q->State == ENG_PA_PARKED) {            // an ABORT or RESET left waiting
            q->Cancel = ENG_CANCEL_GONE;
            q->ErrSrbStatus = SRB_STATUS_ERROR;
            EngFinishParent(DevExt, i);
        }
    }
}

// RDY=0 acknowledged with CC.EN=0: isolated.
static VOID P9RmIsolated(IN PHW_DEVICE_EXTENSION DevExt)
{
    PLAT_POOL9X *p = &DevExt->Pool9x;
    ULONG want = P9RD_DRAIN | P9RD_FLUSH | P9RD_SHST | P9RD_RDY0;
    ULONG outcome = ((p->RmDetail & want) == want && !(p->RmDetail & P9RD_BUDGET)) ? STOR98RM_CLEAN
                                                                                 : STOR98RM_ISOLATED_NOT_CLEAN;

    // Isolation evidence: children released, blocks ISOLATED; every request
    // still held completes NO_DEVICE; the engine stays out of the way.
    EngChildrenSettle(DevExt, TRUE);
    P9RmFinishParents(DevExt);
    EngSetState(DevExt, ENG_ST_FAILED, 0x5200 | outcome);
    // Never touched again: RELEASED (and the instance ends), then the
    // acknowledgement.
    P9FinishIsolated(DevExt);
    PlatDmaRelinquish(DevExt);
    P9RmStepTo(DevExt, P9R_DONE);
    P9RmAck(DevExt, outcome);
}

// No acknowledged disable: the controller may still DMA (R9c FAILED).
static VOID P9RmFailed(IN PHW_DEVICE_EXTENSION DevExt, IN BOOLEAN Gone)
{
    PLAT_POOL9X *p = &DevExt->Pool9x;

    if (Gone) {
        p->RmDetail |= P9RD_GONE;
    }
    P9RmStepTo(DevExt, P9R_FAILED);
    P9RmAck(DevExt, STOR98RM_FAILED);
    if (Gone) {
        EngEnterGone(DevExt, ENG_GONE_TIMER);
        p->RmHandedOver = TRUE;
    }
}

static VOID P9RmDisable(IN PHW_DEVICE_EXTENSION DevExt)
{
    ULONG cc;

    NvmeWriteReg32(DevExt, NVME_REG_INTMS, 0xFFFFFFFF);
    cc = NvmeReadReg32(DevExt, NVME_REG_CC);
    NvmeWriteReg32(DevExt, NVME_REG_CC, cc & ~NVME_CC_ENABLE);
    P9RmStepTo(DevExt, P9R_DISABLE);
}

static VOID P9RmShutdown(IN PHW_DEVICE_EXTENSION DevExt)
{
    ULONG cc;

    NvmeWriteReg32(DevExt, NVME_REG_INTMS, 0xFFFFFFFF);
    cc = NvmeReadReg32(DevExt, NVME_REG_CC);
    NvmeWriteReg32(DevExt, NVME_REG_CC, (cc & ~NVME_CC_SHN_MASK) | NVME_CC_SHN_NORMAL);
    P9RmStepTo(DevExt, P9R_SHN);
}

static BOOLEAN P9RmDrained(IN PHW_DEVICE_EXTENSION DevExt)
{
    ULONG i;

    if (EngIssuedChildren(DevExt) != 0) {
        return FALSE;
    }
    for (i = 0; i < ENG_MAX_PARENTS; i++) {
        if (DevExt->Parents[i].State != ENG_PA_FREE) {
            return FALSE;
        }
    }
    return TRUE;
}

// The first step, from the engine's state when the request was accepted.
static VOID P9RmDecide(IN PHW_DEVICE_EXTENSION DevExt, IN ULONG Csts)
{
    PLAT_POOL9X *p = &DevExt->Pool9x;

    switch (DevExt->State) {
    case ENG_ST_RUNNING:
        P9RmStepTo(DevExt, P9R_DRAIN);
        break;
    case ENG_ST_SHUTDOWN:
        // A restartable SHUTDOWN already flushed and shut down: its result
        // counts; disable now.
        // Only this episode's explicit successes count (ShnFlushOk,
        // ShnShstOk, kept by the engine): a timed-out SHST or a failed or
        // superseded Flush earns nothing, and the outcome is then
        // ISOLATED_NOT_CLEAN after the disable.
        p->RmDetail |= P9RD_STATE;
        if (DevExt->Bringup == ENG_BU_SHN_DONE && P9RmDrained(DevExt) && DevExt->ShnShstOk &&
            (Csts & NVME_CSTS_SHST_MASK) == NVME_CSTS_SHST_COMPLETE) {
            p->RmDetail |= P9RD_DRAIN | P9RD_SHST;
            if (DevExt->ShnFlushOk && DevExt->WriteGen == DevExt->CleanGen) {
                p->RmDetail |= P9RD_FLUSH;
            }
        }
        P9RmDisable(DevExt);
        break;
    case ENG_ST_FAILED:
        // The engine enters `failed` with the controller disabled and RDY=0
        // acknowledged, or before CC.EN was ever written.
        p->RmDetail |= P9RD_STATE;
        if (!(Csts & NVME_CSTS_RDY) && !(NvmeReadReg32(DevExt, NVME_REG_CC) & NVME_CC_ENABLE)) {
            p->RmDetail |= P9RD_RDY0;
            P9RmIsolated(DevExt);
        } else {
            P9RmDisable(DevExt);
        }
        break;
    case ENG_ST_GONE:
        p->RmDetail |= P9RD_STATE;
        P9RmFailed(DevExt, TRUE);
        break;
    default:
        // Starting, resetting, quarantined: no data path to finish; disable.
        p->RmDetail |= P9RD_STATE;
        P9RmDisable(DevExt);
        break;
    }
}

//
// From EngTimer before the engine's own step (platform.h).
//
ULONG PlatCmRemoveTimer(IN PHW_DEVICE_EXTENSION DevExt)
{
    PLAT_POOL9X *p = &DevExt->Pool9x;
    ULONG csts, now, el;

    P9RmCheck(DevExt);
    if (!p->RmAccepted || p->RmHandedOver) {
        return 0;
    }
#ifdef STOR98_DBGPORT
    if (p->TestIsrProbe && p->RmStep != P9R_DONE) {
        // A shared line's call of this adapter's ISR (review: the ISR must
        // not consume or dispatch past the drain).
        BOOLEAN r = EngInterrupt(DevExt);

        DBGSTAGE(STG_P9_ISR_PROBE, (ULONG)r | (p->RmStep << 8) | ((EngIssuedChildren(DevExt) & 0xFFFF) << 16));
    }
#endif
    if (p->RmStep == P9R_DONE) {
        // Terminal, controller disabled, blocks released. The engine's step
        // does not run; the timer stays at the link poll period while a
        // DOWN_QUIESCED for this generation can still come (K1) or
        // boot-block work is pending (K2).
        return P9RmDoneTimer(DevExt);
    }
    csts = P9Csts(DevExt);
    if (csts == 0xFFFFFFFF) {
        // Presence lost: no acknowledged disable is possible any more.
        if (p->RmStep == P9R_FAILED) {
            EngEnterGone(DevExt, ENG_GONE_TIMER);
            p->RmHandedOver = TRUE;
        } else {
            P9RmFailed(DevExt, TRUE);
        }
        return 0;
    }
    if (p->RmStep == P9R_FAILED) {
        return P9_RM_FAILED_US;
    }
    if (p->RmStep == P9R_DECIDE) {
#ifdef STOR98_DBGPORT
        if (p->TestShnPrime && DevExt->State == ENG_ST_RUNNING && P9RmDrained(DevExt)) {
            // Test (ShnPrime): 9x gives no SRB_FUNCTION_SHUTDOWN or FLUSH on
            // demand (98 SE never sends SHUTDOWN, Me only at system
            // shutdown), so the shutdown episode before the request is made
            // here: CC.SHN written as the engine writes it, and the engine's
            // episode results set as LcShnStep leaves them after an SHST
            // timeout (1) or an SHST completion (2), after the episode's own
            // ordered Flush. The decision then runs on the real controller
            // state.
            ULONG cc, i;

            // The episode's own ordered Flush first, as the engine's SHUTDOWN
            // runs it (polled within the stall budget).
            DevExt->StopFlushStatus = 0xFFFF;
            if (EngSubmitInternalFlush(DevExt, ENG_INT_STOP_FLUSH)) {
                DevExt->StopFlushOutstanding = TRUE;
                for (i = 0; i < ENG_STALL_BUDGET && DevExt->StopFlushOutstanding; i++) {
                    EngProcessCompletions(DevExt);
                    if (DevExt->StopFlushOutstanding) {
                        ScsiPortStallExecution(ENG_STALL_US);
                    }
                }
            }
            NvmeWriteReg32(DevExt, NVME_REG_INTMS, 0xFFFFFFFF);
            cc = NvmeReadReg32(DevExt, NVME_REG_CC);
            NvmeWriteReg32(DevExt, NVME_REG_CC, (cc & ~NVME_CC_SHN_MASK) | NVME_CC_SHN_NORMAL);
            ScsiPortStallExecution(500);
            DevExt->ShnFlushOk = (BOOLEAN)(!DevExt->StopFlushOutstanding && DevExt->StopFlushStatus == 0 &&
                                           DevExt->WriteGen == DevExt->CleanGen);
            DevExt->ShnShstOk = (BOOLEAN)(p->TestShnPrime == 2);
            DevExt->Bringup = ENG_BU_SHN_DONE;
            EngSetState(DevExt, ENG_ST_SHUTDOWN, 0);
            csts = P9Csts(DevExt);
            DBGSTAGE(STG_P9_TEST, 0x10000 | (p->TestShnPrime << 8) | (csts & 0xFF));
        }
#endif
        P9RmDecide(DevExt, csts);
        if (p->RmStep == P9R_DONE) {
            return P9RmDoneTimer(DevExt);
        }
        if (p->RmHandedOver) {
            return 0;
        }
    }
    now = P9Now();
    el = now - p->RmStartMs;
#ifdef STOR98_DBGPORT
    el += p->TestBudgetSkewMs;                      // test: a late first sight of the request
#endif
    // The overall budget (R10).
    if (p->RmStep != P9R_DISABLE && el >= P9_RM_DISABLE_BY_MS) {
        p->RmDetail |= P9RD_BUDGET;
        P9RmDisable(DevExt);
        now = p->RmStepMs;                          // the step time is newer than the entry time (as q9x.c)
    } else if (p->RmStep == P9R_DISABLE && el >= P9_RM_ACK_BY_MS && (P9Csts(DevExt) & NVME_CSTS_RDY)) {
        P9RmFailed(DevExt, FALSE);
        return P9_RM_FAILED_US;
    }
    switch (p->RmStep) {
    case P9R_DRAIN:
        EngProcessCompletions(DevExt);
        EngKick(DevExt);
        if (P9RmDrained(DevExt)) {
            p->RmDetail |= P9RD_DRAIN;
            p->RmFlushSubmitted = FALSE;
            P9RmStepTo(DevExt, P9R_FLUSH);
        } else if (now - p->RmStepMs > P9_RM_STEP_MS) {
            P9RmShutdown(DevExt);                   // not drained: no Flush, shut down anyway (not clean)
        }
        break;
    case P9R_FLUSH:
        if (!p->RmFlushSubmitted) {
            DevExt->StopFlushStatus = 0xFFFF;
            if (!EngSubmitInternalFlush(DevExt, ENG_INT_STOP_FLUSH)) {
                P9RmShutdown(DevExt);               // could not submit: not clean
                break;
            }
            DevExt->StopFlushOutstanding = TRUE;
            p->RmFlushSubmitted = TRUE;
        }
        EngProcessCompletions(DevExt);
        if (!DevExt->StopFlushOutstanding) {
            // An ordered Flush after the drain: every write is covered.
            if (DevExt->StopFlushStatus == 0 && DevExt->WriteGen == DevExt->CleanGen) {
                p->RmDetail |= P9RD_FLUSH;
            }
            P9RmShutdown(DevExt);
        } else if (now - p->RmStepMs > P9_RM_STEP_MS) {
            P9RmShutdown(DevExt);                   // Flush still outstanding: settled at the disable
        }
        break;
    case P9R_SHN:
        if ((P9Csts(DevExt) & NVME_CSTS_SHST_MASK) == NVME_CSTS_SHST_COMPLETE) {
            p->RmDetail |= P9RD_SHST;
            P9RmDisable(DevExt);
        } else if (now - p->RmStepMs > P9_RM_STEP_MS) {
            P9RmDisable(DevExt);
        }
        break;
    case P9R_DISABLE:
        csts = P9Csts(DevExt);
        if (csts != 0xFFFFFFFF && !(csts & NVME_CSTS_RDY)) {
            p->RmDetail |= P9RD_RDY0;
            P9RmIsolated(DevExt);
            return P9RmDoneTimer(DevExt);
        }
        if (now - p->RmStepMs > (DevExt->CapTimeoutMs < P9_RM_DISABLE_MS ? DevExt->CapTimeoutMs : P9_RM_DISABLE_MS)) {
            P9RmFailed(DevExt, FALSE);
            return P9_RM_FAILED_US;
        }
        break;
    default:
        break;
    }
    if (p->RmStep == P9R_DONE) {
        return P9RmDoneTimer(DevExt);
    }
    return (p->RmStep == P9R_FAILED) ? P9_RM_FAILED_US : ENG_TICK_FAST_US;
}

//
// A DOWN_QUIESCED statement while a removal transaction owns the adapter
// (K1; R12 S3, R9c, R9d), from the engine's timer before any state step and
// from HwStartIo. FALSE: no transaction, or it has handed over to the
// engine's gone state, whose own rules apply. TRUE: handled here; the caller
// then writes AckGeneration (PlatCmLinkAcknowledge), after this.
//   open (DECIDE to DISABLE): the statement is controller-wide isolation:
//     children released, every held request completed NO_DEVICE, the blocks
//     relinquished, then RemoveAck ISOLATED_NOT_CLEAN (S3: "an open request
//     acknowledged ISOLATED_NOT_CLEAN"), all before AckGeneration;
//   FAILED acknowledged, presence still seen: the same cleanup releases
//     what FAILED kept held (R9d); the outcome stays FAILED (immutable);
//   DONE: nothing is held any more; only the acknowledgement remains.
//
BOOLEAN PlatCmRemoveLinkDown(IN PHW_DEVICE_EXTENSION DevExt)
{
    PLAT_POOL9X *p = &DevExt->Pool9x;

    if (!p->RmAccepted || p->RmHandedOver) {
        return FALSE;
    }
    if (p->RmStep == P9R_DONE) {
        return TRUE;
    }
    p->RmDetail |= P9RD_LINKDOWN;
    NvmeWriteReg32(DevExt, NVME_REG_INTMS, 0xFFFFFFFF);
    EngChildrenSettle(DevExt, TRUE);
    P9RmFinishParents(DevExt);
    EngSetState(DevExt, ENG_ST_FAILED, 0x5300 | (p->RmAcked ? p->RmOutcome : STOR98RM_ISOLATED_NOT_CLEAN));
    P9FinishIsolated(DevExt);
    PlatDmaRelinquish(DevExt);
    P9RmStepTo(DevExt, P9R_DONE);
    P9RmAck(DevExt, STOR98RM_ISOLATED_NOT_CLEAN);   // no-op when FAILED was already written
    return TRUE;
}

//
// From EngStartIo first (R9d, R11 O3): BUSY while the transaction runs,
// NO_DEVICE after the acknowledgement; SRB_FUNCTION_SHUTDOWN succeeds.
//
BOOLEAN PlatCmRemoveStartIo(IN PHW_DEVICE_EXTENSION DevExt, IN PSCSI_REQUEST_BLOCK Srb)
{
    PLAT_POOL9X *p = &DevExt->Pool9x;

    P9RmCheck(DevExt);
    if (!p->RmAccepted) {
        return FALSE;
    }
    if (Srb->Function == SRB_FUNCTION_SHUTDOWN) {
        Srb->SrbStatus = SRB_STATUS_SUCCESS;
        Srb->DataTransferLength = 0;
        EngCompleteSrb(DevExt, Srb);
    } else if (p->RmAcked || p->RmHandedOver) {
        p->RmRefused++;
        if (p->RmRefused <= 5 || (p->RmRefused & 63) == 0) {
            DBGSTAGE(STG_P9_RM_REFUSED, p->RmRefused);
        }
        Srb->SrbStatus = SRB_STATUS_NO_DEVICE;
        Srb->ScsiStatus = SCSISTAT_GOOD;
        Srb->DataTransferLength = 0;
        EngCompleteSrb(DevExt, Srb);
    } else {
        // R9d: BUSY, without the engine's per-SRB BUSY limit (which would
        // turn the 21st into an error).
        p->RmBusy++;
        if (p->RmBusy <= 255) {
            DBGSTAGE(STG_P9_RM_BUSY, (p->RmBusy & 0xFFFF) | ((((ULONG)(ULONG_PTR)Srb >> 4) & 0xFFF) << 16));
        }
        Srb->SrbStatus = SRB_STATUS_BUSY;
        Srb->ScsiStatus = SCSISTAT_GOOD;
        Srb->DataTransferLength = 0;
        EngCompleteSrb(DevExt, Srb);
        EngTimerEnsure(DevExt, ENG_TICK_FAST_US);
    }
    return TRUE;
}

BOOLEAN PlatCmRemoveActive(IN PHW_DEVICE_EXTENSION DevExt)
{
    return DevExt->Pool9x.RmAccepted;
}

//
// What the engine may do on its own while the transaction runs (platform.h).
// Accepted and still draining: admitted work may finish (dispatch, ISR).
// Flush: the ISR may consume the Flush's completion; nothing is dispatched.
// From the shutdown notification on, and after FAILED until presence loss:
// nothing; late completions stay unconsumed and requests stay held until
// isolation (R9c, R9d). After the hand-over to `gone` the engine's own gone
// rules apply to completions; nothing is dispatched any more.
//
ULONG PlatCmRemoveGate(IN PHW_DEVICE_EXTENSION DevExt)
{
    PLAT_POOL9X *p = &DevExt->Pool9x;

    if (!p->RmAccepted) {
        return 0;
    }
    if (p->RmHandedOver) {
        return PLAT_RM_GATE_DISPATCH;
    }
    switch (p->RmStep) {
    case P9R_DECIDE:
    case P9R_DRAIN:
        return 0;
    case P9R_FLUSH:
        return PLAT_RM_GATE_DISPATCH;
    default:
        return PLAT_RM_GATE_DISPATCH | PLAT_RM_GATE_ISR;
    }
}

#endif // STOR98_DMA_POOL && NVME2K_W9X
