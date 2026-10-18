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
// p9bind_ut.c - host unit test of the 9x entry binding and its late lookup
// (src/p9bind.h; owner decision 2026-10-16, docs/PLAN.md 0.4.0.0 item 5),
// of the rendezvous field checks (R5a-R5c) and of the removal request's
// acceptance and acknowledgement (R9b, R9c), against values and tables
// built here. Built by tests/unit/build.cmd.
//

#include <stdio.h>
#include <string.h>
#include "nvme2k.h"
#include "p9bind.h"

static int failures;

static void check(int ok, const char *what)
{
    printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) {
        failures++;
    }
}

#define BUS     6
#define SLOT    0x00
#define COOKIE  0x5A17C0DE

static STOR98LS_TABLE table;

static void reset(void)
{
    memset(&table, 0, sizeof(table));
    table.Signature = STOR98LS_SIGNATURE;
    table.Version = STOR98LS_VERSION;
    table.Count = STOR98LS_ENTRIES;
    table.Cookie = COOKIE;
}

static void put(ULONG i, ULONG bus, ULONG slot, ULONG gen, ULONG flags, ULONG remseq)
{
    PSTOR98LS_ENTRY e = &table.Entry[i];

    e->Sequence++;
    e->BusNumber = bus;
    e->SlotNumber = slot;
    e->LinkGeneration = gen;
    e->Flags = flags;
    e->RemoveSequence = remseq;
    e->Sequence++;
}

#define UP      (STOR98LS_IN_USE | STOR98LS_UP)
#define DQ      (STOR98LS_IN_USE | STOR98LS_DOWN_QUIESCED)
#define ALONE   (STOR98LS_IN_USE)
#define RP      (STOR98LS_IN_USE | STOR98LS_UP | STOR98LS_REMOVE_PENDING)

// The find, as P9Bind: P9B_BOUND binds; DUP and BAD end; the others start
// the late lookup with the DOWN generation as the floor.
static ULONG lateOn, lateFloor;

static ULONG find(ULONG *index, P9_SNAP *pick)
{
    ULONG rc = P9BindScan(&table, BUS, SLOT, 0, index, pick);

    lateOn = 0;
    lateFloor = 0;
    if (rc == P9B_NONE || rc == P9B_DOWN || rc == P9B_IDLE || rc == P9B_BUSY) {
        P9LateStart(rc, pick, &lateOn, &lateFloor);
    }
    return rc;
}

// A locator as the CM writes it (R3b) for a table at Linear.
static STOR98LS9X_LOCATOR loc;
static ULONG proofTable[3];

static void locator(ULONG linear)
{
    loc.Signature = STOR98LS_SIGNATURE;
    loc.Version = STOR98LS9X_LOCATOR_VERSION;
    loc.TableLinear = linear;
    loc.TableSize = sizeof(STOR98LS_TABLE);
    loc.Cookie = COOKIE;
    proofTable[0] = linear;
    proofTable[1] = sizeof(STOR98LS_TABLE);
    proofTable[2] = COOKIE;
}

// R5a-R5c in P9Rendezvous order, on the values as read: 0 or its code.
static ULONG rendezvous(ULONG rc, ULONG type, ULONG cb, ULONG proofCookie)
{
    ULONG r = P9LocatorCheck(rc, type, cb, &loc);

    if (r == 0) {
        r = P9ProofCheck(&loc, proofCookie, proofTable);
    }
    if (r == 0) {
        r = P9TableAddressCheck(&loc);
    }
    return r;
}

#define LOC_OK(r)   rendezvous(0, P9_REG_BINARY, sizeof(STOR98LS9X_LOCATOR), COOKIE) == (r)

static void testRendezvous(void)
{
    ULONG top = STOR98LS9X_ARENA_HI - sizeof(STOR98LS_TABLE);

    // P9InArena: the arena is [C0000000, FFC00000), no overflow.
    check(P9InArena(0xC0000000, 1) && P9InArena(0xFFBFFFFF, 1), "arena: first and last byte inside");
    check(!P9InArena(0xBFFFFFFF, 1) && !P9InArena(0xFFC00000, 1), "arena: the bytes just outside");
    check(P9InArena(0xC0000000, 0x3FC00000) && !P9InArena(0xC0000000, 0x3FC00001), "arena: all of it, not one byte more");
    check(!P9InArena(0xFFBFFFFF, 2) && !P9InArena(0xBFFFFF00, 0x200), "arena: ranges crossing either end");
    check(!P9InArena(0xC0001000, 0) && !P9InArena(0xFFFFFFFF, 0xFFFFFFFF) && !P9InArena(0xC0000000, 0xFFFFFFFF),
          "arena: empty and wrapping ranges");

    // R5a: the value and its fields.
    locator(0xC1234560);
    check(LOC_OK(0), "a well-formed locator, proof and table address: accepted");
    check(rendezvous(2, P9_REG_BINARY, sizeof(loc), COOKIE) == 2, "R5a: query failed: refused (2)");
    check(rendezvous(0, P9_REG_DWORD, sizeof(loc), COOKIE) == 2, "R5a: not REG_BINARY: refused (2)");
    check(rendezvous(0, P9_REG_BINARY, 19, COOKIE) == 2 && rendezvous(0, P9_REG_BINARY, 21, COOKIE) == 2,
          "R5a: length 19 or 21, not 20: refused (2)");
    loc.Signature = 0x4C38394E;                     // 'N98L', the old name
    check(LOC_OK(3), "R5a: another signature: refused (3)");
    locator(0xC1234560);
    loc.Version = 1;
    check(LOC_OK(3), "R5a: version 1: refused (3)");
    locator(0xC1234560);
    loc.TableSize = sizeof(STOR98LS_TABLE) - 4;
    check(LOC_OK(3), "R5a: table size not 272: refused (3)");
    locator(0xC1234560);
    loc.Cookie = 0;
    check(rendezvous(0, P9_REG_BINARY, sizeof(loc), 0) == 3, "R5a: cookie 0: refused (3), before the proof");

    // R5b: the proof's values as read, then against the locator.
    check(P9ProofCookie(0, P9_REG_DWORD, 4, COOKIE) == COOKIE, "R5b: BootCookie read: its value");
    check(P9ProofCookie(2, P9_REG_DWORD, 4, COOKIE) == 0 && P9ProofCookie(0, P9_REG_BINARY, 4, COOKIE) == 0 &&
          P9ProofCookie(0, P9_REG_DWORD, 2, COOKIE) == 0 && P9ProofCookie(0, P9_REG_DWORD, 4, 0) == 0,
          "R5b: BootCookie failed, wrong type, wrong length or zero: unusable");
    check(P9ProofTableRead(0, P9_REG_BINARY, 12) && !P9ProofTableRead(2, P9_REG_BINARY, 12) &&
          !P9ProofTableRead(0, P9_REG_DWORD, 12) && !P9ProofTableRead(0, P9_REG_BINARY, 8),
          "R5b: TableLinear usable only as REG_BINARY of 12 bytes");
    locator(0xC1234560);
    check(rendezvous(0, P9_REG_BINARY, sizeof(loc), COOKIE + 1) == 11, "R5b: BootCookie not the locator's: refused (11)");
    proofTable[0] = 0xC1234570;
    check(LOC_OK(14), "R5b: TableLinear names another address: refused (14)");
    locator(0xC1234560);
    proofTable[1] = 256;
    check(LOC_OK(14), "R5b: TableLinear's size not 272: refused (14)");
    locator(0xC1234560);
    proofTable[2] = COOKIE + 1;
    check(LOC_OK(14), "R5b: TableLinear's cookie not the locator's: refused (14)");
    locator(0xC1234560);
    proofTable[0] = proofTable[1] = proofTable[2] = 0;  // the value was unusable (P9ProofTableRead)
    check(LOC_OK(14), "R5b: no usable TableLinear: refused (14)");

    // R5c: alignment and the arena, at the boundary values.
    locator(0xC1234562);
    check(LOC_OK(4), "R5c: table address not 4-byte aligned: refused (4)");
    locator(0xBFFFFF00);
    check(LOC_OK(4), "R5c: table starting below the arena: refused (4)");
    locator(0x00401000);
    check(LOC_OK(4), "R5c: a user-space address: refused (4)");
    locator(STOR98LS9X_ARENA_LO);
    check(LOC_OK(0), "R5c: table at the arena's start: accepted");
    locator(top);
    check(top == 0xFFBFFEF0 && LOC_OK(0), "R5c: table ending exactly at FFC00000 (FFC00000 - 272): accepted");
    locator(top + 4);
    check(LOC_OK(4), "R5c: 4 bytes later, crossing FFC00000: refused (4)");
    locator(0xFFBFFF00);
    check(LOC_OK(4), "R5c: TableLinear FFBFFF00 (only 256 bytes left): refused (4)");
    locator(STOR98LS9X_ARENA_HI);
    check(LOC_OK(4), "R5c: table at FFC00000: refused (4)");
    locator(0xFFFFFF00);
    check(LOC_OK(4), "R5c: table wrapping past 4 GiB: refused (4)");
}

// The link-state statement of an instance bound to generation 3 (R8b, R8d;
// owner decision 2026-10-18).
static void testLinkSee(void)
{
    P9_SNAP s;

    s.Bus = BUS;
    s.Slot = SLOT;
    s.RemSeq = 0;
    s.Gen = 3;
    s.Flags = UP;
    check(P9LinkSee(&s, 3, FALSE) == P9LS_UP && !P9LinkNeedsPresence(&s, 3), "UP at the bound generation: UP");
    s.Flags = ALONE;
    check(P9LinkSee(&s, 3, FALSE) == P9LS_NONE && P9LinkSee(&s, 3, TRUE) == P9LS_NONE,
          "IN_USE alone at the bound generation: nothing (withheld statement)");
    s.Flags = DQ;
    check(P9LinkSee(&s, 3, FALSE) == P9LS_EVIDENCE && !P9LinkNeedsPresence(&s, 3),
          "DOWN_QUIESCED at the bound generation: evidence, controller answering or not");
    s.Gen = 4;
    check(P9LinkSee(&s, 3, FALSE) == P9LS_EVIDENCE && !P9LinkNeedsPresence(&s, 3),
          "DOWN_QUIESCED at a newer generation: evidence while the controller answers (explicit statement)");
    s.Flags = UP;
    check(P9LinkNeedsPresence(&s, 3), "UP at a newer generation: presence decides");
    check(P9LinkSee(&s, 3, FALSE) == P9LS_LOST,
          "UP at a newer generation, controller answering (a generation bumped without a new tunnel): identity lost, no evidence");
    check(P9LinkSee(&s, 3, TRUE) == P9LS_EVIDENCE, "UP at a newer generation after presence loss: R8b evidence");
    s.Flags = ALONE;
    check(P9LinkSee(&s, 3, FALSE) == P9LS_LOST && P9LinkSee(&s, 3, TRUE) == P9LS_EVIDENCE,
          "IN_USE alone at a newer generation: identity lost while answering, evidence once gone");
    s.Gen = 2;
    s.Flags = DQ;
    check(P9LinkSee(&s, 3, TRUE) == P9LS_LOST && !P9LinkNeedsPresence(&s, 3), "an older generation: never evidence");
}

// Pool record evidence (E1 at the find, E2 after this load's disable) for
// a record QUARANTINED by an owner bound to generation 3 on (BUS, SLOT).
static void testPoolEvidence(void)
{
    P9_SNAP s;

    s.Bus = BUS;
    s.Slot = SLOT;
    s.RemSeq = 0;
    s.Gen = 4;
    s.Flags = UP;
    check(!P9EvidenceE1Snap(&s, 3), "E1: UP at a later generation (a bump without a new tunnel): no release at the find");
    s.Flags = ALONE;
    check(!P9EvidenceE1Snap(&s, 3), "E1: IN_USE alone at a later generation (statement withheld): no release");
    s.Gen = 3;
    s.Flags = UP;
    check(!P9EvidenceE1Snap(&s, 3), "E1: UP at the record's generation: no release");
    s.Flags = DQ;
    check(P9EvidenceE1Snap(&s, 3), "E1: DOWN_QUIESCED for the record's generation: released");
    s.Gen = 5;
    check(P9EvidenceE1Snap(&s, 3), "E1: DOWN_QUIESCED for a later generation: released");
    s.Gen = 2;
    check(!P9EvidenceE1Snap(&s, 3), "E1: DOWN_QUIESCED for an earlier generation: no release");
    s.Gen = 3;
    check(!P9EvidenceE1Snap(&s, 0), "E1: a record bound to no generation: never");

    check(!P9EvidenceE2(TRUE, 0, BUS, SLOT, BUS, SLOT, FALSE), "E2: same function, before this load's disable is acknowledged: no release");
    check(P9EvidenceE2(TRUE, 0, BUS, SLOT, BUS, SLOT, TRUE), "E2: same function, after the acknowledged disable: released");
    check(!P9EvidenceE2(TRUE, 0, BUS + 1, SLOT, BUS, SLOT, TRUE) && !P9EvidenceE2(TRUE, 0, BUS, SLOT + 8, BUS, SLOT, TRUE),
          "E2: another bus or another device: stays quarantined");
    check(!P9EvidenceE2(TRUE, 7, BUS, SLOT, BUS, SLOT, TRUE), "E2: a record with an owner: never");
    check(!P9EvidenceE2(FALSE, 0, BUS, SLOT, BUS, SLOT, TRUE), "E2: a record no longer QUARANTINED: untouched");
}

// R9b and R9c on one bound entry: generation 3, RemoveSequence X at binding.
static void testRemoval(void)
{
    P9_SNAP s;
    ULONG rmSeq = 0, x = 0x000101, y = 0x000102, z = 0x000103;
    BOOLEAN acked = FALSE;

    s.Bus = BUS;
    s.Slot = SLOT;
    s.Gen = 3;
    s.Flags = UP;
    s.RemSeq = 0;
    check(P9RmSee(&s, 3, x, &rmSeq) == P9RM_NONE && rmSeq == 0, "R9b: no REMOVE_PENDING: no request");
    s.Flags = RP;
    s.RemSeq = x;
    check(P9RmSee(&s, 3, x, &rmSeq) == P9RM_NONE && rmSeq == 0, "R9b: the request present at binding (X): ignored");
    s.Gen = 4;
    s.RemSeq = y;
    check(P9RmSee(&s, 3, x, &rmSeq) == P9RM_OTHER_GEN && rmSeq == 0, "R9b: a request on another generation: not this instance's");
    s.Gen = 3;
    check(P9RmSee(&s, 3, x, &rmSeq) == P9RM_ACCEPT && rmSeq == y, "R9b: the first other request (Y): accepted");
    check(P9RmSee(&s, 3, x, &rmSeq) == P9RM_SAME && rmSeq == y, "R9b: Y again (a CM retry): nothing changes");
    s.RemSeq = z;
    check(P9RmSee(&s, 3, x, &rmSeq) == P9RM_OVERLAP && rmSeq == y, "R9b: a later request (Z): ignored, Y stays");
    s.RemSeq = x;
    check(P9RmSee(&s, 3, x, &rmSeq) == P9RM_NONE && rmSeq == y, "R9b: X again: still ignored");
    s.Flags = UP;
    check(P9RmSee(&s, 3, x, &rmSeq) == P9RM_NONE && rmSeq == y, "R9b: REMOVE_PENDING cleared: Y stays accepted");

    // R9c: written once, never changed.
    reset();
    table.Entry[2].RemoveAck = 0;
    check(P9RmAckOnce(&table.Entry[2].RemoveAck, &acked, rmSeq, STOR98RM_CLEAN) && acked &&
          table.Entry[2].RemoveAck == ((y << 8) | STOR98RM_CLEAN), "R9c: RemoveAck = Y << 8 | CLEAN");
    check(!P9RmAckOnce(&table.Entry[2].RemoveAck, &acked, rmSeq, STOR98RM_FAILED) &&
          table.Entry[2].RemoveAck == ((y << 8) | STOR98RM_CLEAN), "R9c: a second outcome: not written, unchanged");
    check(!P9RmAckOnce(&table.Entry[2].RemoveAck, &acked, z, STOR98RM_ISOLATED_NOT_CLEAN) &&
          table.Entry[2].RemoveAck == ((y << 8) | STOR98RM_CLEAN), "R9c: another sequence: not written, unchanged");
    acked = FALSE;
    table.Entry[3].RemoveAck = 0;
    check(!P9RmAckOnce(NULL, &acked, y, STOR98RM_FAILED) && acked && table.Entry[3].RemoveAck == 0,
          "R9c: identity lost: decided once, nothing published");
    acked = FALSE;
    check(P9RmAckOnce(&table.Entry[3].RemoveAck, &acked, 0xFFFFFF, STOR98RM_FAILED) &&
          table.Entry[3].RemoveAck == 0xFFFFFF03UL, "R9c: the 24-bit sequence fills bits 31:8");
}

static ULONG late(ULONG ms, ULONG *index, P9_SNAP *pick)
{
    return P9LateLookup(&table, COOKIE, BUS, SLOT, ms, &lateOn, &lateFloor, index, pick);
}

int main(void)
{
    ULONG idx, rc, ms;
    P9_SNAP pick;

    printf("p9bind: the 9x entry binding, its late lookup, the rendezvous checks and the removal request\n");

    // A boot-present drive: the entry is UP at the find.
    reset();
    put(0, 3, 0, 4, UP, 0);
    put(2, BUS, SLOT, 1, UP, 0);
    rc = find(&idx, &pick);
    check(rc == P9B_BOUND && idx == 2 && pick.Gen == 1 && lateOn == 0, "UP at the find: bound there, no late lookup");

    // NV9X.1 on 98 SE: the find sees the table but no entry; UP 206 ms later.
    reset();
    put(0, 3, 0, 4, UP, 0);
    rc = find(&idx, &pick);
    check(rc == P9B_NONE && lateOn != 0 && lateFloor == 0, "no entry at the find: the late lookup starts");
    rc = late(0, &idx, &pick);
    check(rc == P9B_NONE && lateOn != 0, "first tick, still no entry: keeps looking");
    put(5, BUS, SLOT, 1, UP, 0);
    rc = late(250, &idx, &pick);
    check(rc == P9B_BOUND && idx == 5 && pick.Gen == 1 && pick.RemSeq == 0 && lateOn == 0,
          "UP appears: bound late to its entry and generation, the lookup ends");
    rc = late(500, &idx, &pick);
    check(rc == P9B_EXPIRED, "after binding the lookup does nothing");

    // An entry published before its first UP (R8a: IN_USE, generation 0).
    reset();
    put(1, BUS, SLOT, 0, ALONE, 0);
    rc = find(&idx, &pick);
    check(rc == P9B_NONE && lateOn != 0, "IN_USE with generation 0 at the find: not bound, lookup starts");
    put(1, BUS, SLOT, 1, UP, 0);
    rc = late(300, &idx, &pick);
    check(rc == P9B_BOUND && idx == 1 && pick.Gen == 1, "the same entry turns UP: bound");

    // Never appearing: the window ends, MD-1 as before.
    reset();
    rc = find(&idx, &pick);
    for (ms = 0; ms < P9_LATE_BIND_MS; ms += 250) {
        rc = late(ms, &idx, &pick);
        if (rc != P9B_NONE) {
            break;
        }
    }
    check(rc == P9B_NONE && lateOn != 0, "no entry for the whole window: still looking just before its end");
    rc = late(P9_LATE_BIND_MS, &idx, &pick);
    check(rc == P9B_EXPIRED && lateOn == 0, "the window ends: no channel (MD-1)");
    put(0, BUS, SLOT, 1, UP, 0);
    rc = late(P9_LATE_BIND_MS + 250, &idx, &pick);
    check(rc == P9B_EXPIRED && lateOn == 0, "an entry after the window is never bound");
    reset();
    rc = find(&idx, &pick);
    put(0, BUS, SLOT, 1, UP, 0);
    rc = late(P9_LATE_BIND_MS + 1500, &idx, &pick);
    check(rc == P9B_EXPIRED && lateOn == 0, "a tick arriving late past the window does not bind");

    // A duplicate appearing: refused for good.
    reset();
    rc = find(&idx, &pick);
    put(2, BUS, SLOT, 1, UP, 0);
    put(6, BUS, SLOT, 2, UP, 0);
    rc = late(250, &idx, &pick);
    check(rc == P9B_DUP && lateOn == 0, "two entries name the function: duplicate, no channel");
    put(6, 9, SLOT, 2, UP, 0);
    rc = late(500, &idx, &pick);
    check(rc == P9B_EXPIRED, "the duplicate going away later changes nothing");

    // Duplicate at the find: P9Bind ends it there (no late lookup).
    reset();
    put(2, BUS, SLOT, 1, UP, 0);
    put(3, BUS, SLOT, 1, UP, 0);
    rc = find(&idx, &pick);
    check(rc == P9B_DUP && lateOn == 0, "duplicate at the find: no channel, no late lookup");

    // A malformed entry naming the function beside legal ones: still a
    // duplicate (no channel), never bound to either legal one.
    reset();
    put(1, BUS, SLOT, 1, UP, 0);
    put(2, BUS, SLOT, 2, UP, 0);
    put(3, BUS, SLOT, 0, UP, 0);
    rc = find(&idx, &pick);
    check(rc == P9B_DUP && lateOn == 0, "two legal and one malformed entry: duplicate at the find");
    reset();
    put(1, BUS, SLOT, 1, UP, 0);
    put(3, BUS, SLOT, 0, UP, 0);
    rc = find(&idx, &pick);
    check(rc == P9B_DUP && lateOn == 0, "one legal and one malformed entry: duplicate, not bound");

    // A DOWN_QUIESCED entry appearing: refused, never bound.
    reset();
    rc = find(&idx, &pick);
    put(4, BUS, SLOT, 1, DQ, 0);
    rc = late(250, &idx, &pick);
    check(rc == P9B_DOWN && lateOn != 0 && lateFloor == 1, "DOWN_QUIESCED appears: refused, its generation is the floor");
    rc = late(500, &idx, &pick);
    check(rc == P9B_DOWN && lateOn != 0, "still DOWN: still refused");

    // A drive returning in the same boot: the previous tunnel's DOWN at the
    // find, then UP of the next generation.
    reset();
    put(4, BUS, SLOT, 3, DQ, 0);
    rc = find(&idx, &pick);
    check(rc == P9B_DOWN && lateOn != 0 && lateFloor == 3, "previous tunnel's DOWN at the find: refused, floor 3");
    put(4, BUS, SLOT, 3, ALONE, 0);
    rc = late(250, &idx, &pick);
    check(rc == P9B_IDLE && lateOn != 0 && lateFloor == 3, "the old generation without DOWN: not UP, not bound");
    put(4, BUS, SLOT, 4, UP, 0);
    rc = late(500, &idx, &pick);
    check(rc == P9B_BOUND && idx == 4 && pick.Gen == 4 && lateOn == 0, "UP of generation 4: bound late");

    // xhci98 NV9X.1 on 98 SE: Safely Remove, unplug (the CM withheld the
    // statement and cleared UP: IN_USE alone, generation 1), re-plug: the
    // find comes before UP of generation 2. The old generation must not be
    // bound (its successor's UP would then read as R8b evidence against the
    // new load); the new one is bound late.
    reset();
    put(0, BUS, SLOT, 1, ALONE, 0);
    rc = find(&idx, &pick);
    check(rc == P9B_IDLE && lateOn != 0 && lateFloor == 1, "withheld entry (IN_USE alone, gen 1) at the find: refused, floor 1");
    rc = late(100, &idx, &pick);
    check(rc == P9B_IDLE && lateOn != 0, "still withheld: still refused");
    put(0, BUS, SLOT, 2, UP, 0);
    rc = late(350, &idx, &pick);
    check(rc == P9B_BOUND && idx == 0 && pick.Gen == 2 && lateOn == 0, "UP of generation 2 (192 ms later in NV9X.1): bound late to 2");
    reset();
    put(0, BUS, SLOT, 1, ALONE, 0);
    rc = find(&idx, &pick);
    put(0, BUS, SLOT, 1, UP, 0);
    rc = late(250, &idx, &pick);
    check(rc == P9B_NONE && lateOn != 0, "UP again at the refused generation (not a new tunnel): not bound");

    // The table vanished (the CM unloaded and its header changed, or another
    // cookie): refused for good.
    reset();
    rc = find(&idx, &pick);
    table.Signature = 0;
    put(0, BUS, SLOT, 1, UP, 0);
    rc = late(250, &idx, &pick);
    check(rc == P9B_TABLE && lateOn == 0, "table signature gone: refused");
    table.Signature = STOR98LS_SIGNATURE;
    rc = late(500, &idx, &pick);
    check(rc == P9B_EXPIRED && lateOn == 0, "the header coming back changes nothing");
    reset();
    rc = find(&idx, &pick);
    table.Cookie = COOKIE + 1;
    put(0, BUS, SLOT, 1, UP, 0);
    rc = late(250, &idx, &pick);
    check(rc == P9B_TABLE && lateOn == 0, "another cookie: refused");
    reset();
    rc = find(&idx, &pick);
    table.Version = STOR98LS_VERSION + 1;
    rc = late(250, &idx, &pick);
    check(rc == P9B_TABLE, "another version: refused");
    reset();
    rc = find(&idx, &pick);
    table.Count = STOR98LS_ENTRIES - 1;
    rc = late(250, &idx, &pick);
    check(rc == P9B_TABLE, "Count not 8: refused");
    // R5d at the find (P9Rendezvous, code 6): the same check.
    reset();
    check(P9TableSame(&table, COOKIE), "R5d: the header as published: accepted");
    check(!P9TableSame(&table, 0), "R5d: cookie 0: refused");
    table.Count = 9;
    check(!P9TableSame(&table, COOKIE), "R5d: Count 9: refused");
    reset();
    table.Signature = 0x4C38394E;
    check(!P9TableSame(&table, COOKIE), "R5d: the old signature: refused");
    reset();
    check(!P9TableSame(&table, COOKIE ^ 1), "R5d: another cookie than the locator's: refused");
    reset();
    lateOn = 1;
    lateFloor = 0;
    rc = P9LateLookup(NULL, COOKIE, BUS, SLOT, 0, &lateOn, &lateFloor, &idx, &pick);
    check(rc == P9B_TABLE && lateOn == 0, "no table: refused");

    // An entry being updated: undecided until every entry reads.
    reset();
    rc = find(&idx, &pick);
    put(3, BUS, SLOT, 1, UP, 0);
    table.Entry[5].Sequence = 7;                    // another port's update, half done
    rc = late(250, &idx, &pick);
    check(rc == P9B_BUSY && lateOn != 0, "another entry mid-update: not bound yet (it may be a duplicate)");
    table.Entry[5].Sequence = 8;
    rc = late(500, &idx, &pick);
    check(rc == P9B_BOUND && idx == 3, "update finished: bound");
    reset();
    put(3, BUS, SLOT, 1, UP, 0);
    table.Entry[3].Sequence = 9;
    rc = find(&idx, &pick);
    check(rc == P9B_BUSY && lateOn != 0, "own entry mid-update at the find: the late lookup takes it");
    table.Entry[3].Sequence = 10;
    rc = late(250, &idx, &pick);
    check(rc == P9B_BOUND && idx == 3 && pick.Gen == 1, "then bound late");

    // A malformed entry naming the function (R5e): no channel.
    reset();
    rc = find(&idx, &pick);
    put(1, BUS, SLOT, 1, UP | STOR98LS_DOWN_QUIESCED, 0);
    rc = late(250, &idx, &pick);
    check(rc == P9B_BAD && lateOn == 0, "UP with DOWN_QUIESCED: malformed, refused");
    reset();
    put(1, BUS, SLOT, 0, UP, 0);
    rc = find(&idx, &pick);
    check(rc == P9B_BAD && lateOn == 0, "UP with generation 0 at the find: malformed, no channel");
    reset();
    rc = find(&idx, &pick);
    put(1, BUS, SLOT, 1, UP | 0x100, 0);
    rc = late(250, &idx, &pick);
    check(rc == P9B_BAD, "unknown flag bit: refused");

    // R8d at a late bind. The find saw no entry (a complete scan), so the
    // generation bound later was published after the find and a request
    // already on it was posted for this load: the sequence at binding is 0
    // and the request is accepted (R9b).
    reset();
    rc = find(&idx, &pick);
    check(rc == P9B_NONE && lateOn == P9L_AFTER, "no entry at the find: a generation bound later is newer than the load");
    put(0, BUS, SLOT, 2, RP, 0x123);
    rc = late(250, &idx, &pick);
    check(rc == P9B_BOUND && pick.RemSeq == 0 && pick.Gen == 2,
          "REMOVE_PENDING posted between the UP and a late bind after the find: sequence at binding 0");
    reset();
    put(0, BUS, SLOT, 1, ALONE, 0);
    rc = find(&idx, &pick);
    put(0, BUS, SLOT, 2, RP, 0x044);
    rc = late(250, &idx, &pick);
    check(rc == P9B_BOUND && pick.RemSeq == 0 && pick.Gen == 2,
          "withheld at the find, then UP 2 with a request: sequence at binding 0");
    // The find could not read every entry (P9B_BUSY): the generation may
    // predate the load, so a request already on it keeps its sequence as
    // the one at binding (never this load's own).
    reset();
    put(0, BUS, SLOT, 2, RP, 0x123);
    table.Entry[0].Sequence = 11;
    rc = find(&idx, &pick);
    check(rc == P9B_BUSY && lateOn == P9L_UNSURE, "own entry mid-update at the find: unsure");
    table.Entry[0].Sequence = 12;
    rc = late(250, &idx, &pick);
    check(rc == P9B_BOUND && pick.RemSeq == 0x123 && pick.Gen == 2,
          "REMOVE_PENDING present at a late bind after a busy find: its sequence recorded as at binding");
    // Busy at the find, then a complete scan without the entry: from then on
    // a generation bound is newer than the load.
    reset();
    table.Entry[5].Sequence = 7;
    rc = find(&idx, &pick);
    check(rc == P9B_BUSY && lateOn == P9L_UNSURE, "another entry mid-update at the find: unsure");
    table.Entry[5].Sequence = 8;
    rc = late(250, &idx, &pick);
    check(rc == P9B_NONE && lateOn == P9L_AFTER, "then a complete scan without the entry: after");
    put(1, BUS, SLOT, 1, RP, 0x200);
    rc = late(500, &idx, &pick);
    check(rc == P9B_BOUND && pick.RemSeq == 0 && pick.Gen == 1 && lateOn == P9L_OFF,
          "then UP with a request: sequence at binding 0, the lookup ends");

    // Other functions and free entries do not count.
    reset();
    rc = find(&idx, &pick);
    put(0, BUS, 0x08, 1, UP, 0);                    // device 8 on the same bus
    put(1, BUS + 1, SLOT, 1, UP, 0);
    put(2, BUS, SLOT, 5, 0, 0);                     // IN_USE clear: a retired entry
    rc = late(250, &idx, &pick);
    check(rc == P9B_NONE && lateOn != 0, "other functions' and retired entries: not this function's");

    testRendezvous();
    testLinkSee();
    testPoolEvidence();
    testRemoval();

    if (failures) {
        printf("p9bind: %d FAILED\n", failures);
        return 1;
    }
    printf("p9bind: all passed\n");
    return 0;
}
