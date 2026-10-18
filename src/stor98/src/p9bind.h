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
// p9bind.h - the 9x link-state receiver's entry binding (interface 3 of
// include/stor98cm.h: R5e, R6c, R8a, R8d), at HwFindAdapter and later, the
// late lookup (owner decision 2026-10-16, docs/PLAN.md 0.4.0.0 item 5);
// also the rendezvous field checks (R5a-R5c), what a newer generation means
// to the link-state poll (R8b, R8d), the pool records' release evidence (E1,
// E2), and the removal request's acceptance and acknowledgement (R9b, R9c).
// Header-only and free of VMM
// calls, so the host unit test (tests/unit/p9bind_ut.c) runs the same code
// against values and tables it builds itself. The includer may define
// P9BIND_LOAD(p) (an aligned 32-bit load that the compiler keeps in program
// order) and P9BIND_XCHG(p, v) (an interlocked 32-bit store, R6d) before
// including it; pool9x.c uses inline assembly for both.
//
// Why a late lookup: on 98 SE a function hot-plugged behind a tunnel after
// Windows started does not answer configuration reads until Windows
// enumerates it, so xhci98's CM can post the entry's UP only after that
// enumeration, which already started stor98: HwFindAdapter finds the table
// (proof accepted) but no entry for its function (xhci98 NV9X.1: UP posted
// about 206 ms after the find), or, when the drive returns in the same boot,
// the previous tunnel's DOWN_QUIESCED. Without the late lookup that load ran
// with no channel (the MD-1 gone deadline only).
//
// Rules:
//   - A scan reads every entry with one R6c snapshot (at most 2 attempts,
//     no spinning). Entries without IN_USE, or naming another function, are
//     skipped. Two IN_USE entries naming this function, legal or not, are a
//     duplicate (R8a, P9B_DUP); one that fails R5e is P9B_BAD. Both end the
//     lookup and the load's use of the table (no channel, and no E1 from
//     it either). An entry that could not be read
//     leaves the scan undecided (P9B_BUSY): it may be this function's, or a
//     duplicate of it, so nothing is bound until a scan reads them all.
//   - The one entry is bound only when it is UP with a LinkGeneration
//     above the floor. An entry that is not UP is never bound, and its
//     generation becomes the floor, so only a newer generation can be bound
//     later: DOWN_QUIESCED (P9B_DOWN) and IN_USE alone with a generation
//     (P9B_IDLE: the CM cleared UP after a link loss it could not state
//     quiesced, xhci98's "withheld" verdict). At the find either is the
//     previous tunnel's (a drive returning in the same boot: the CM posts
//     the new UP only after this find) or the link went down under the find
//     (the MD-1 deadline covers that). Binding the previous generation made
//     the new tunnel's UP look like R8b evidence against this load, which
//     then failed a working drive (xhci98 NV9X.1 on 98 SE: no disk after a
//     Safely Remove and re-plug). A generation at or below the floor, or
//     zero (an entry published before its first UP, R8a), is P9B_NONE.
//   - A later bind is safe by R8c: the CM publishes UP for a new tunnel only
//     after the previous endpoint devnode was removed, so while this load
//     lives a generation above the floor is this load's own. xhci98 3.0.0.0
//     and 3.1.0.0 do not yet enforce R8c on 9x when that removal is vetoed
//     or slow (its roadmap item 42-A.12): a re-plug can then publish the
//     next generation's UP while the previous devnode and its instance
//     still exist.
//   - The late lookup runs from the paths that already poll the entry (the
//     engine timer and HwStartIo, through PlatCmLinkPoll), only while the
//     load has a validated table and no entry, for at most P9_LATE_BIND_MS
//     of VMM elapsed time from the find. Each attempt first checks that the
//     table header is still the one validated at the find (signature,
//     version, count, cookie); a change ends the lookup (P9B_TABLE). A
//     bound entry is then used exactly as one bound at the find: its
//     generation, its RemoveSequence at binding (R8d: an older request is
//     never this load's), the R5e/R8d checks of every later poll and the
//     MD-5 removal transaction.
//   - The RemoveSequence at a late bind: once a complete scan (the find's
//     or a later one) has shown no entry to bind (P9B_NONE, P9B_DOWN,
//     P9B_IDLE), the generation bound later was published after the find,
//     so a request already on it was posted for this load: the sequence at
//     binding is taken as 0 and that request is accepted (R9b). Before
//     such a scan (the find was P9B_BUSY) the generation may predate the
//     load, and the sequence read at binding is kept, as at the find.
//

#ifndef _STOR98_P9BIND_H_
#define _STOR98_P9BIND_H_

#include "..\include\stor98cm.h"

#ifndef P9BIND_LOAD
#define P9BIND_LOAD(p)              (*(volatile ULONG *)(p))
#endif
#ifndef P9BIND_XCHG
#define P9BIND_XCHG(p, v)           (*(volatile ULONG *)(p) = (v))
#endif

// VMM registry value types (_RegQueryValueEx)
#define P9_REG_BINARY               3
#define P9_REG_DWORD                4

// ---------------------------------------------------------------------------
// Rendezvous field checks (R5a-R5c), on the values as read. The results are
// the rendezvous codes of stage 0x122 (pool9x.c P9Rendezvous).
// ---------------------------------------------------------------------------

// R5c: [Linear, Linear + Bytes) inside the VMM system arena, no overflow.
static BOOLEAN P9InArena(ULONG Linear, ULONG Bytes)
{
    if (Bytes == 0 || Linear < STOR98LS9X_ARENA_LO || Linear >= STOR98LS9X_ARENA_HI) {
        return FALSE;
    }
    return (BOOLEAN)(Bytes <= STOR98LS9X_ARENA_HI - Linear);
}

// R5a: the locator value (_RegQueryValueEx result, type, length) and its
// fields. 0, 2 (the value) or 3 (a field).
static ULONG P9LocatorCheck(ULONG Rc, ULONG Type, ULONG Cb, const STOR98LS9X_LOCATOR *Loc)
{
    if (Rc != 0 || Type != P9_REG_BINARY || Cb != sizeof(STOR98LS9X_LOCATOR)) {
        return 2;
    }
    if (Loc->Signature != STOR98LS_SIGNATURE || Loc->Version != STOR98LS9X_LOCATOR_VERSION ||
        Loc->TableSize != sizeof(STOR98LS_TABLE) || Loc->Cookie == 0) {
        return 3;
    }
    return 0;
}

// R5b: the proof's BootCookie value as read: the cookie, or 0 when it is
// unusable (no channel, rendezvous code 11).
static ULONG P9ProofCookie(ULONG Rc, ULONG Type, ULONG Cb, ULONG Value)
{
    if (Rc != 0 || Type != P9_REG_DWORD || Cb != sizeof(ULONG)) {
        return 0;
    }
    return Value;
}

// R5b: the proof's TableLinear value as read is usable (else it counts as
// all zero, which no locator matches).
static BOOLEAN P9ProofTableRead(ULONG Rc, ULONG Type, ULONG Cb)
{
    return (BOOLEAN)(Rc == 0 && Type == P9_REG_BINARY && Cb == sizeof(STOR98LS9X_PROOF_TABLE));
}

// R5b: a locator that passed R5a against the proof (BootCookie, then
// TableLinear {linear, size, cookie}). 0, 11 (cookie) or 14 (table).
static ULONG P9ProofCheck(const STOR98LS9X_LOCATOR *Loc, ULONG ProofCookie, const ULONG *ProofTable)
{
    if (ProofCookie != Loc->Cookie) {
        return 11;
    }
    if (ProofTable[0] != Loc->TableLinear || ProofTable[1] != sizeof(STOR98LS_TABLE) ||
        ProofTable[2] != Loc->Cookie) {
        return 14;
    }
    return 0;
}

// R5c, before the first dereference: the table address 4-byte aligned and
// the whole table in the arena. 0 or 4. (Residency, write access and the
// page lock follow with VMM services, pool9x.c.)
static ULONG P9TableAddressCheck(const STOR98LS9X_LOCATOR *Loc)
{
    if ((Loc->TableLinear & 3) || !P9InArena(Loc->TableLinear, Loc->TableSize)) {
        return 4;
    }
    return 0;
}

// The late lookup's window, from the find (VMM elapsed ms). The CM posts UP
// once the function answers configuration reads after Windows enumerated
// it: 206 ms after the find in xhci98 NV9X.1 on 98 SE. 10 s covers that
// with a wide margin for a slow CM writer, timer callbacks up to
// STOR98LS9X_LATE_MS late (Me) and the 250 ms poll period. It is bounded
// so that a function that is not tunnelled (no entry will ever come) stops
// costing a 250 ms idle timer; a load that finds no entry in it keeps the
// MD-1 gone deadline, as before.
#define P9_LATE_BIND_MS             10000

// P9BindScan and P9LateLookup results (also in STG_P9_LATE).
#define P9B_BOUND                   0   // the entry to bind (*Index, *Pick)
#define P9B_NONE                    1   // no entry for this function, generation 0 or not above the floor
#define P9B_DOWN                    2   // the entry is DOWN_QUIESCED: refused (its generation is the new floor)
#define P9B_BUSY                    3   // an entry was being updated: decide at a later poll
#define P9B_DUP                     4   // two entries name this function (R8a): no channel
#define P9B_BAD                     5   // the entry naming this function fails R5e: no channel
#define P9B_TABLE                   6   // the table header changed since the find: no channel
#define P9B_EXPIRED                 7   // the window ended without an entry: no channel (MD-1)
#define P9B_IDLE                    8   // IN_USE alone with a generation: that link is down, not stated quiesced (refused, floor)

typedef struct _P9_SNAP {
    ULONG Bus;
    ULONG Slot;
    ULONG Flags;
    ULONG Gen;
    ULONG RemSeq;
} P9_SNAP;

// R6c: s1 even, payload, s2 == s1; at most 2 attempts. FALSE: being
// updated, try at the next poll.
static BOOLEAN P9Snapshot(PSTOR98LS_ENTRY e, P9_SNAP *s)
{
    ULONG attempt, s1, s2;

    for (attempt = 0; attempt < 2; attempt++) {
        s1 = P9BIND_LOAD(&e->Sequence);
        if (s1 & 1) {
            continue;
        }
        s->Bus = P9BIND_LOAD(&e->BusNumber);
        s->Slot = P9BIND_LOAD(&e->SlotNumber);
        s->Flags = P9BIND_LOAD(&e->Flags);
        s->Gen = P9BIND_LOAD(&e->LinkGeneration);
        s->RemSeq = P9BIND_LOAD(&e->RemoveSequence);
        s2 = P9BIND_LOAD(&e->Sequence);
        if (s1 == s2) {
            return TRUE;
        }
    }
    return FALSE;                                   // being updated: next poll
}

// R5e: 0 when the snapshot is legal, else a reason code.
static ULONG P9SnapCheck(const P9_SNAP *s)
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

// One scan of the table for the entry of (Bus, Slot) (rules above).
static ULONG P9BindScan(PSTOR98LS_TABLE t, ULONG Bus, ULONG Slot, ULONG Floor, PULONG Index, P9_SNAP *Pick)
{
    P9_SNAP s;
    ULONG i, n = 0, bad = 0, busy = 0;

    *Index = 0;
    Pick->Bus = Pick->Slot = Pick->Flags = Pick->Gen = Pick->RemSeq = 0;
    for (i = 0; i < STOR98LS_ENTRIES; i++) {
        if (!P9Snapshot(&t->Entry[i], &s)) {
            busy++;
            continue;
        }
        if (!(s.Flags & STOR98LS_IN_USE) || s.Bus != Bus || s.Slot != Slot) {
            continue;                               // unused, or another function's
        }
        if (P9SnapCheck(&s) != 0) {
            bad++;
            continue;
        }
        n++;
        *Index = i;
        *Pick = s;
    }
    if (n + bad > 1) return P9B_DUP;                // a malformed one counts too (R8a)
    if (bad != 0) return P9B_BAD;
    if (busy != 0) return P9B_BUSY;
    if (n == 0) return P9B_NONE;
    if (Pick->Flags & STOR98LS_DOWN_QUIESCED) return P9B_DOWN;
    if (Pick->Gen == 0) return P9B_NONE;                // published before its first UP (R8a)
    if (!(Pick->Flags & STOR98LS_UP)) return P9B_IDLE;  // a lost link the CM did not state quiesced
    if (Pick->Gen <= Floor) return P9B_NONE;
    return P9B_BOUND;
}

// R5d: the header (at the find, against the locator's cookie; at each late
// lookup, the header validated at the find still there).
static BOOLEAN P9TableSame(PSTOR98LS_TABLE t, ULONG Cookie)
{
    return (BOOLEAN)(Cookie != 0 && P9BIND_LOAD(&t->Signature) == STOR98LS_SIGNATURE &&
                     P9BIND_LOAD(&t->Version) == STOR98LS_VERSION &&
                     P9BIND_LOAD(&t->Count) == STOR98LS_ENTRIES && P9BIND_LOAD(&t->Cookie) == Cookie);
}

// The late lookup's state (*On of P9LateLookup).
#define P9L_OFF                     0   // not running
#define P9L_UNSURE                  1   // running; no complete scan has shown the entry unbound yet
#define P9L_AFTER                   2   // running; one has: a generation bound now was published after the find

// The find's scan returned Rc, neither P9B_BOUND nor P9B_DUP nor P9B_BAD:
// the late lookup starts, with the DOWN or IDLE generation as the floor.
static VOID P9LateStart(ULONG Rc, const P9_SNAP *Pick, PULONG On, PULONG Floor)
{
    *On = (Rc == P9B_BUSY) ? P9L_UNSURE : P9L_AFTER;
    *Floor = (Rc == P9B_DOWN || Rc == P9B_IDLE) ? Pick->Gen : 0;
}

//
// One late-lookup attempt. *On is nonzero while the lookup runs and is
// cleared when it ends (bound, refused for good, or out of time); *Floor is
// raised by a DOWN_QUIESCED entry. ElapsedMs: VMM ms since the find.
// P9B_BOUND with *Index and *Pick: bind that entry as at the find, with
// Pick->RemSeq as the RemoveSequence at binding (R8d): the one read, or 0
// once a complete scan showed no entry to bind (rules above).
//
static ULONG P9LateLookup(PSTOR98LS_TABLE t, ULONG Cookie, ULONG Bus, ULONG Slot, ULONG ElapsedMs,
                          PULONG On, PULONG Floor, PULONG Index, P9_SNAP *Pick)
{
    ULONG rc;

    if (*On == P9L_OFF) {
        return P9B_EXPIRED;
    }
    if (t == NULL || !P9TableSame(t, Cookie)) {
        *On = P9L_OFF;
        return P9B_TABLE;
    }
    if (ElapsedMs >= P9_LATE_BIND_MS) {
        *On = P9L_OFF;
        return P9B_EXPIRED;
    }
    rc = P9BindScan(t, Bus, Slot, *Floor, Index, Pick);
    switch (rc) {
    case P9B_BOUND:
        if (*On == P9L_AFTER) {
            Pick->RemSeq = 0;                       // posted after the find: this load's (R9b)
        }
        *On = P9L_OFF;
        break;
    case P9B_DUP:
    case P9B_BAD:
        *On = P9L_OFF;
        break;
    case P9B_DOWN:
    case P9B_IDLE:
        if (Pick->Gen > *Floor) {
            *Floor = Pick->Gen;
        }
        *On = P9L_AFTER;
        break;
    case P9B_NONE:
        *On = P9L_AFTER;
        break;
    default:
        break;
    }
    return rc;
}

// ---------------------------------------------------------------------------
// The link-state statement (R8b, R8d; owner decision 2026-10-18, docs/PLAN.md
// "Decided"), on a validated snapshot of the bound entry outside a removal
// transaction (pool9x.c P9EntryRead passed: R5e, the place, and S->Gen not
// below the bound generation).
//   - DOWN_QUIESCED for the bound generation or a newer one is the CM's
//     explicit statement: evidence, at once, in every engine state.
//   - A newer generation in any other state (UP, IN_USE alone) is R8b
//     evidence only once this instance has lost presence (the engine is
//     gone, or the controller reads all-ones). While the controller still
//     answers it is identity loss: no evidence, nothing completed, no
//     acknowledgement, and the drive keeps running; a later presence loss
//     falls back to the MD-1 gone deadline. A CM that increments a
//     generation without a new tunnel (xhci98 3.0.0.0 and 3.1.0.0 on a
//     re-exposure, its roadmap item 42-A.6) then cannot fail a working
//     drive.
// ---------------------------------------------------------------------------

#define P9LS_NONE                   0   // nothing to report (IN_USE alone at the bound generation)
#define P9LS_UP                     1   // UP at the bound generation
#define P9LS_EVIDENCE               2   // isolation evidence: report it (PLAT_LINK_DOWN_QUIESCED)
#define P9LS_LOST                   3   // identity lost: stop acting on the entry

// The caller needs PresenceLost for this snapshot (a newer generation
// without DOWN_QUIESCED); otherwise it need not read the controller.
static BOOLEAN P9LinkNeedsPresence(const P9_SNAP *S, ULONG Gen)
{
    return (BOOLEAN)(S->Gen > Gen && !(S->Flags & STOR98LS_DOWN_QUIESCED));
}

// PresenceLost: the engine is gone or the controller reads all-ones.
static ULONG P9LinkSee(const P9_SNAP *S, ULONG Gen, BOOLEAN PresenceLost)
{
    if ((S->Flags & STOR98LS_DOWN_QUIESCED) && S->Gen >= Gen) {
        return P9LS_EVIDENCE;
    }
    if (S->Gen > Gen) {
        return PresenceLost ? P9LS_EVIDENCE : P9LS_LOST;
    }
    if (S->Gen != Gen) {
        return P9LS_LOST;                           // older (P9EntryRead refuses it already)
    }
    return (S->Flags & STOR98LS_UP) ? P9LS_UP : P9LS_NONE;
}

// ---------------------------------------------------------------------------
// Pool record evidence (MD-4; pool9x.c): what releases a record a previous
// instance left QUARANTINED (owner gone, controller-wide isolation never
// shown) to RELEASED. Owner decision 2026-10-18 (docs/PLAN.md "Decided").
//   E1, at the find: this load's CM table shows DOWN_QUIESCED on the
//     record's entry for the record's generation or a later one. A later
//     generation alone (UP, IN_USE alone) is not E1: a CM may increment it
//     without a new tunnel (xhci98 3.0.0.0 and 3.1.0.0, its roadmap item
//     42-A.6) while the old controller still reaches the block.
//   E2, after this load's own controller was shown disabled (CC.EN=0 and
//     CSTS.RDY=0 read back, its acknowledged disable or found so, before
//     its first enable): a record of the same function (Bus, Slot), with no
//     owner. One downstream port holds one function at a time: the same
//     drive has now stopped its DMA, and another drive means the old one
//     is no longer behind that port. Records of another function stay
//     quarantined.
// ---------------------------------------------------------------------------

// E1 on one validated snapshot of the record's entry (R5e passed, the
// record's Bus and Slot); RecGen: the generation the record's owner was
// bound to (nonzero).
static BOOLEAN P9EvidenceE1Snap(const P9_SNAP *S, ULONG RecGen)
{
    return (BOOLEAN)(RecGen != 0 && (S->Flags & STOR98LS_DOWN_QUIESCED) && S->Gen >= RecGen);
}

// E2 for one record: Quarantined and Owner are its state and owner token;
// DisableAcked: this load's controller was shown disabled.
static BOOLEAN P9EvidenceE2(BOOLEAN Quarantined, ULONG Owner, ULONG RecBus, ULONG RecSlot, ULONG Bus, ULONG Slot,
                            BOOLEAN DisableAcked)
{
    return (BOOLEAN)(DisableAcked && Quarantined && Owner == 0 && RecBus == Bus && RecSlot == Slot);
}

// ---------------------------------------------------------------------------
// The removal request (R9b, R9c), on validated snapshots of the bound entry
// (pool9x.c P9EntryRead: R5e and the R8d identity checks passed).
// ---------------------------------------------------------------------------

#define P9RM_NONE                   0   // no request for this instance: none, or the one at binding
#define P9RM_ACCEPT                 1   // the first other request: accepted (*RmSeq set)
#define P9RM_SAME                   2   // the accepted request again (a CM retry): nothing changes
#define P9RM_OVERLAP                3   // a later request while one is accepted: ignored, the first stays
#define P9RM_OTHER_GEN              4   // another LinkGeneration on the entry: no request of this instance

// R9b: what snapshot *S means for an instance bound to Gen with BindSeq the
// RemoveSequence at binding; *RmSeq is the accepted request's sequence (0
// while none) and is set only by P9RM_ACCEPT.
static ULONG P9RmSee(const P9_SNAP *S, ULONG Gen, ULONG BindSeq, PULONG RmSeq)
{
    ULONG seq = S->RemSeq & STOR98LS9X_SEQ_MASK;

    if (S->Gen != Gen) {
        return P9RM_OTHER_GEN;
    }
    if (!(S->Flags & STOR98LS_REMOVE_PENDING) || seq == 0 || seq == BindSeq) {
        return P9RM_NONE;
    }
    if (*RmSeq == 0) {
        *RmSeq = seq;
        return P9RM_ACCEPT;
    }
    return (seq == *RmSeq) ? P9RM_SAME : P9RM_OVERLAP;
}

// R9c: RemoveAck = Seq << 8 | Outcome, once (*Acked), by one interlocked
// store (R6d) after the work it reports. RemoveAck NULL: decided but not
// published (the instance lost its identity). TRUE when written now.
static BOOLEAN P9RmAckOnce(volatile ULONG *RemoveAck, PBOOLEAN Acked, ULONG Seq, ULONG Outcome)
{
    if (*Acked) {
        return FALSE;
    }
    *Acked = TRUE;
    if (RemoveAck == NULL) {
        return FALSE;
    }
    P9BIND_XCHG(RemoveAck, ((Seq & STOR98LS9X_SEQ_MASK) << 8) | (Outcome & 0xFF));
    return TRUE;
}

#endif // _STOR98_P9BIND_H_
