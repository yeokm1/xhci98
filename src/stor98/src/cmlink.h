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
// cmlink.h - the NT receiver of the CM link-state table (include/stor98cm.h,
// interface 2 on Windows 2000 to 7; MD-6, PLAN 0.10 evidence 2): its
// decisions, free of kernel calls, so the host unit test
// (tests/unit/cmlink_ut.c) runs the same code against tables it builds
// itself. The kernel half (the callback object, ExNotifyCallback, the
// acknowledgement store) is src/cmlinknt.c.
//
// Rules, from stor98cm.h ("Interface 2", "CM lifetime (NT)", "Entry
// protocol") and the interface 3 rules it shares (R6c snapshot, R5e-like
// entry checks, R8d identity):
//   - The table is accepted only with STOR98LS_SIGNATURE, STOR98LS_VERSION
//     (4) and STOR98LS_ENTRIES: a CM built against another interface
//     version is "no channel" (MD-1 applies), never read further.
//   - One table per boot: the first table this image accepted is the only
//     one; a later answer naming another table is refused (a CM that
//     published a second table broke its contract).
//   - Binding, once per HwFindAdapter: the one IN_USE entry naming this
//     function's (bus, slot), UP or IN_USE alone; its LinkGeneration is
//     recorded. Two entries naming the function: no channel. An entry
//     reading DOWN_QUIESCED at binding is refused (the function answered
//     the find, so the statement is stale or the link went down under the
//     find; the MD-1 deadline covers the latter).
//   - Each poll takes one R6c snapshot (at most 2 attempts, no spinning).
//     DOWN_QUIESCED for the bound generation or a newer one is evidence 2;
//     it is reported until acknowledged, and AckGeneration is written only
//     for the bound generation (a newer one belongs to a newer instance:
//     applied, never acknowledged from here). DOWN without QUIESCED, UP or
//     IN_USE alone change nothing. On NT a newer generation without
//     DOWN_QUIESCED is not evidence (interface 3's R8b is a 9x rule; an NT
//     CM withholds the statement where it cannot prove the reset, and a
//     later UP does not prove it either).
//   - A snapshot with unknown flag bits (REMOVE_PENDING is 9x only), IN_USE
//     clear, UP with DOWN_QUIESCED, a zero generation while UP or
//     DOWN_QUIESCED, another bus or slot, or an older generation ends this
//     instance's use of the entry: no evidence, no acknowledgement.
//   - One statement per binding: once applied the entry is not read again
//     until the next HwFindAdapter (a resume re-binds).
//
// The includer may define CMLINK_LOAD(p) (an aligned 32-bit load that the
// compiler keeps in program order) before including this file.
//

#ifndef _STOR98_CMLINK_H_
#define _STOR98_CMLINK_H_

#include "..\include\stor98cm.h"

#ifndef CMLINK_LOAD
#define CMLINK_LOAD(p)              (*(volatile ULONG *)(p))
#endif

// Flags an NT entry may carry (no REMOVE_PENDING: interface 3 only).
#define CML_FLAGS_KNOWN             (STOR98LS_IN_USE | STOR98LS_UP | STOR98LS_DOWN_QUIESCED)

// Open and binding results (CML_STATE.Rc; debug-port stage STG_CML_OPEN).
#define CML_RC_BOUND                0   // table accepted, entry bound
#define CML_RC_DUMP                 1   // dump or hibernation instance: never opened
#define CML_RC_NO_OBJECT            2   // no \Callback\Stor98LinkState: no CM
#define CML_RC_NO_ANSWER            3   // the object answered nothing (CM unloaded, or a reload)
#define CML_RC_BAD_POINTER          4   // misaligned, not in system space, or not resident
#define CML_RC_SIGNATURE            5
#define CML_RC_VERSION              6   // a CM of another interface version: refused
#define CML_RC_COUNT                7
#define CML_RC_SECOND_TABLE         8   // not the table this image accepted first
#define CML_RC_NO_ENTRY             9   // no entry for this function (not tunnelled, or not published)
#define CML_RC_DUPLICATE            10  // two entries name this function
#define CML_RC_BUSY                 11  // no entry found, and some kept changing during the find
#define CML_RC_DOWN_AT_BIND         12  // the entry read DOWN_QUIESCED at binding
#define CML_RC_BAD_ENTRY            13  // the entry naming this function is malformed
                                    // 14: not used (no code produces it)
#define CML_RC_NONE                 0xFF // never opened (initial value)

// Why an instance stopped acting on its entry (CML_STATE.Lost).
#define CML_LOST_FLAGS              0x01    // unknown flag bits
#define CML_LOST_IN_USE             0x02    // IN_USE cleared
#define CML_LOST_BOTH               0x03    // UP and DOWN_QUIESCED together
#define CML_LOST_GEN_ZERO           0x04    // UP or DOWN_QUIESCED with generation 0
#define CML_LOST_PLACE              0x05    // another bus or slot
#define CML_LOST_OLDER              0x06    // a generation below the bound one
#define CML_LOST_DUPLICATE          0x07    // an entry unread at binding names this function too

typedef struct _CML_SNAP {
    ULONG Bus;
    ULONG Slot;
    ULONG Gen;
    ULONG Flags;
} CML_SNAP;

typedef struct _CML_STATE {
    PSTOR98LS_TABLE Table;          // the accepted table, or NULL (no channel)
    PSTOR98LS_ENTRY Entry;          // the bound entry, or NULL
    ULONG Bus;                      // the bound function (ConfigInfo)
    ULONG Slot;
    ULONG Gen;                      // LinkGeneration at binding
    ULONG LinkGen;                  // statement reported, not yet acknowledged (0 none)
    BOOLEAN LinkOwn;                // that statement is for the bound generation
    BOOLEAN Opened;                 // the callback was called by an earlier find (a resume reuses Table)
    UCHAR Rc;                       // CML_RC_* of the last find
    UCHAR Lost;                     // CML_LOST_* once the entry is no longer used (0 in use)
    UCHAR DupPending;               // entries not read at binding, still to check for a duplicate
    ULONG Applied;                  // generation applied (0 none): the entry is not read again
    ULONG BadSnapshots;             // snapshots that ended the binding
    ULONG Busy;                     // polls skipped because the entry was being updated
} CML_STATE;

// R6c: s1 even, payload, s2 == s1; at most 2 attempts. FALSE: being
// updated, try at the next poll.
static BOOLEAN CmlSnapshot(PSTOR98LS_ENTRY e, CML_SNAP *s)
{
    ULONG attempt, s1, s2;

    for (attempt = 0; attempt < 2; attempt++) {
        s1 = CMLINK_LOAD(&e->Sequence);
        if (s1 & 1) {
            continue;
        }
        s->Bus = CMLINK_LOAD(&e->BusNumber);
        s->Slot = CMLINK_LOAD(&e->SlotNumber);
        s->Flags = CMLINK_LOAD(&e->Flags);
        s->Gen = CMLINK_LOAD(&e->LinkGeneration);
        s2 = CMLINK_LOAD(&e->Sequence);
        if (s1 == s2) {
            return TRUE;
        }
    }
    return FALSE;
}

// 0 when the snapshot is a legal IN_USE entry, else CML_LOST_*.
static ULONG CmlSnapCheck(const CML_SNAP *s)
{
    ULONG f = s->Flags;

    if (f & ~(ULONG)CML_FLAGS_KNOWN) return CML_LOST_FLAGS;
    if (!(f & STOR98LS_IN_USE)) return CML_LOST_IN_USE;
    if ((f & STOR98LS_UP) && (f & STOR98LS_DOWN_QUIESCED)) return CML_LOST_BOTH;
    if ((f & (STOR98LS_UP | STOR98LS_DOWN_QUIESCED)) && s->Gen == 0) return CML_LOST_GEN_ZERO;
    return 0;
}

// The table's header: CML_RC_BOUND when it is interface version 4.
static ULONG CmlCheckTable(const STOR98LS_TABLE *t)
{
    if (CMLINK_LOAD(&t->Signature) != STOR98LS_SIGNATURE) return CML_RC_SIGNATURE;
    if (CMLINK_LOAD(&t->Version) != STOR98LS_VERSION) return CML_RC_VERSION;
    if (CMLINK_LOAD(&t->Count) != STOR98LS_ENTRIES) return CML_RC_COUNT;
    return CML_RC_BOUND;
}

// One table per boot: *Boot is the first table this image accepted.
static ULONG CmlBootTable(PSTOR98LS_TABLE *Boot, PSTOR98LS_TABLE t)
{
    if (*Boot == NULL) {
        *Boot = t;
        return CML_RC_BOUND;
    }
    return *Boot == t ? CML_RC_BOUND : CML_RC_SECOND_TABLE;
}

//
// Binding (R8d on NT): St->Table set; fills Entry and Gen on success. Each
// entry is read until one snapshot of it succeeds, in up to three passes.
// An entry still being updated after them (another port's update, the CM
// thread preempted mid-update) does not block the binding once this
// function's own entry was read: the binding is made with the generation
// read now, and the unread entries are checked for a duplicate by the
// polls (DupPending) before any statement is taken. If this function's own
// entry may be among the unread ones (none found), there is no channel:
// its generation cannot be known, and adopting one later could take a
// newer instance's.
//
static ULONG CmlBind(CML_STATE *St, ULONG Bus, ULONG Slot)
{
    PSTOR98LS_TABLE t = St->Table;
    PSTOR98LS_ENTRY e = NULL;
    CML_SNAP s, pick;
    ULONG pass, i, n = 0, bad = 0, unread = (1UL << STOR98LS_ENTRIES) - 1;

    St->Entry = NULL;
    St->Bus = Bus;
    St->Slot = Slot;
    St->Gen = 0;
    St->LinkGen = 0;
    St->LinkOwn = FALSE;
    St->Applied = 0;
    St->Lost = 0;
    St->DupPending = 0;
    pick.Bus = pick.Slot = pick.Gen = pick.Flags = 0;
    for (pass = 0; pass < 3 && unread != 0; pass++) {
        for (i = 0; i < STOR98LS_ENTRIES; i++) {
            if (!(unread & (1UL << i)) || !CmlSnapshot(&t->Entry[i], &s)) {
                continue;
            }
            unread &= ~(1UL << i);
            if (!(s.Flags & STOR98LS_IN_USE) || s.Bus != Bus || s.Slot != Slot) {
                continue;                           // unused, or another function's
            }
            if (CmlSnapCheck(&s) != 0) {
                bad++;
                continue;
            }
            n++;
            e = &t->Entry[i];
            pick = s;
        }
    }
    if (bad != 0) return CML_RC_BAD_ENTRY;
    if (n > 1) return CML_RC_DUPLICATE;
    if (n == 0) return unread != 0 ? CML_RC_BUSY : CML_RC_NO_ENTRY;
    if (pick.Flags & STOR98LS_DOWN_QUIESCED) return CML_RC_DOWN_AT_BIND;
    St->Entry = e;
    St->Gen = pick.Gen;
    St->DupPending = (UCHAR)unread;
    return CML_RC_BOUND;
}

// The entries binding could not read: each is read once per poll until it
// is; one that names this function is a duplicate and ends the binding.
// TRUE when none is left.
static BOOLEAN CmlDupCheck(CML_STATE *St)
{
    CML_SNAP s;
    ULONG i;

    for (i = 0; i < STOR98LS_ENTRIES && St->DupPending != 0; i++) {
        if (!(St->DupPending & (1UL << i)) || !CmlSnapshot(&St->Table->Entry[i], &s)) {
            continue;
        }
        St->DupPending &= (UCHAR)~(1UL << i);
        if ((s.Flags & STOR98LS_IN_USE) && s.Bus == St->Bus && s.Slot == St->Slot) {
            St->Lost = CML_LOST_DUPLICATE;
            St->BadSnapshots++;
            return FALSE;
        }
    }
    return (BOOLEAN)(St->DupPending == 0);
}

// Whether the timer must keep polling the entry (a statement can still come).
static BOOLEAN CmlWantsPoll(const CML_STATE *St)
{
    return (BOOLEAN)(St->Entry != NULL && St->Lost == 0 && St->Applied == 0);
}

// One poll: PLAT_LINK_DOWN_QUIESCED (reported until CmlAcknowledged),
// PLAT_LINK_UP for the bound generation, else PLAT_LINK_NO_INFO.
static ULONG CmlPoll(CML_STATE *St)
{
    CML_SNAP s;
    ULONG bad;

    if (!CmlWantsPoll(St)) {
        return PLAT_LINK_NO_INFO;
    }
    if (St->LinkGen != 0) {
        return PLAT_LINK_DOWN_QUIESCED;
    }
    if (St->DupPending != 0 && !CmlDupCheck(St)) {
        return PLAT_LINK_NO_INFO;                   // not yet known to be the only entry
    }
    if (!CmlSnapshot(St->Entry, &s)) {
        St->Busy++;
        return PLAT_LINK_NO_INFO;
    }
    bad = CmlSnapCheck(&s);
    if (bad == 0 && (s.Bus != St->Bus || s.Slot != St->Slot)) {
        bad = CML_LOST_PLACE;
    }
    if (bad == 0 && s.Gen < St->Gen) {
        bad = CML_LOST_OLDER;
    }
    if (bad != 0) {
        St->Lost = (UCHAR)bad;
        St->BadSnapshots++;
        return PLAT_LINK_NO_INFO;
    }
    if (s.Flags & STOR98LS_DOWN_QUIESCED) {
        St->LinkGen = s.Gen;
        St->LinkOwn = (BOOLEAN)(s.Gen == St->Gen);
        return PLAT_LINK_DOWN_QUIESCED;
    }
    return ((s.Flags & STOR98LS_UP) && s.Gen == St->Gen) ? PLAT_LINK_UP : PLAT_LINK_NO_INFO;
}

// The engine applied the statement: the value to store in AckGeneration
// (0: nothing to store). The entry is not read again for this binding.
static ULONG CmlAcknowledged(CML_STATE *St)
{
    ULONG gen = St->LinkGen;

    if (gen == 0) {
        return 0;
    }
    St->Applied = gen;
    St->LinkGen = 0;
    return St->LinkOwn ? gen : 0;
}

// Poll period while a statement can still come (PlatCmLinkPollUs): the
// watchdog tick.
#define CML_POLL_US                 ENG_TICK_WATCH_US

// Debug-port stage codes 0x140-0x143 (docs/TESTING.md; test builds only).
#define STG_CML_OPEN        0x140   // arg: CML_RC_* | resumed << 7 | entry << 8 (0xFF none) | generation << 16 | bus << 24
#define STG_CML_SEEN        0x141   // marker; arg: statement generation | bound generation << 8 | engine state << 16 | bus << 24
#define STG_CML_ACK         0x142   // marker; arg: generation applied | AckGeneration written << 16 | bus << 24
#define STG_CML_LOST        0x143   // arg: CML_LOST_* | engine state << 8; the binding ends (no evidence, no acknowledgement)

#endif // _STOR98_CMLINK_H_
