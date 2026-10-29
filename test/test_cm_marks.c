/*
 * test_cm_marks.c - the marks answer's table (src/cm/cm_marks.c; task
 * 42-A.5; design record 21 section 7.4, revision 3): the current and
 * tombstone kept until a poll has seen it and
 * CM_QM_TOMB_MS has passed, the oldest evicted when full and counted when
 * unseen, the answer's layout, and the confirmation's checks; and the
 * Removable answer's gate (src/cm/cm_ports.c CmPortsMayMark; design record
 * 31 section 4.1), which every attachment answers by since a child is
 * attached on every filtered bridge.
 */

#include <stdio.h>
#include <string.h>
#include "../src/cm/cm_marks.h"
#include "../src/cm/cm_ports.h"
#include "test_harness.h"

static CM_MARKS t;
static UCHAR buf[CM_QM_MAX_BYTES];
static int fake[200];

#define P(n) ((PVOID)&fake[(n)])

static ULONG get32(const UCHAR *b, ULONG o)
{
    return (ULONG)b[o] | ((ULONG)b[o + 1] << 8) | ((ULONG)b[o + 2] << 16) |
           ((ULONG)b[o + 3] << 24);
}

static void put32(UCHAR *b, ULONG o, ULONG v)
{
    b[o] = (UCHAR)v;
    b[o + 1] = (UCHAR)(v >> 8);
    b[o + 2] = (UCHAR)(v >> 16);
    b[o + 3] = (UCHAR)(v >> 24);
}

static ULONG countKind(int tombs)
{
    ULONG i;
    ULONG n = 0;

    for (i = 0; i < CM_QM_MAX_ENTRIES; i++) {
        if (t.E[i].Pdo != NULL && (t.E[i].Tomb != 0) == (tombs != 0)) {
            n++;
        }
    }
    return n;
}

static void test_sync(void)
{
    PVOID snap[40];
    PCM_MARK e;
    ULONG gen;
    ULONG serial;
    int i;

    CmMarksReset(&t);
    snap[0] = P(1);
    snap[1] = P(2);
    CmMarksBegin(&t);
    CHECK(CmMarksSync(&t, 0, 0x0300, snap, 2, FALSE, TRUE, 100), "two new PDOs change the table");
    CHECK_EQ(countKind(0), 2, "two current entries");
    CHECK(!t.Incomplete, "complete");
    e = CmMarksFind(&t, P(1));
    CHECK(e != NULL && e->Serial == 1 && e->MemberBdf == 0x0300, "first serial 1, the member's location");
    gen = t.Gen;
    CHECK(gen != 0, "a generation once changed");
    CmMarksBegin(&t);
    CHECK(!CmMarksSync(&t, 0, 0x0300, snap, 2, FALSE, TRUE, 200), "the same snapshot changes nothing");
    CHECK_EQ(t.Gen, gen, "and leaves the generation");

    /* The PDO 1 leaves: a tombstone, its departure cleared. */
    e = CmMarksFind(&t, P(1));
    e->PendingAt = 150;
    serial = e->Serial;
    snap[0] = P(2);
    CmMarksBegin(&t);
    CHECK(CmMarksSync(&t, 0, 0x0300, snap, 1, FALSE, TRUE, 300), "a departure changes the table");
    CHECK(CmMarksFind(&t, P(1)) == NULL, "no current entry for the departed PDO");
    CHECK_EQ(countKind(1), 1, "one tombstone");
    for (i = 0; i < (int)CM_QM_MAX_ENTRIES; i++) {
        if (t.E[i].Tomb) {
            CHECK_EQ(t.E[i].Serial, serial, "the tombstone keeps its serial");
            CHECK_EQ(t.E[i].PendingAt, 0, "and drops its departure");
            CHECK_EQ(t.E[i].TombGen, t.Gen, "made at this generation");
        }
    }

    /* A re-plug that reuses the PDO pointer is a new entry. */
    snap[1] = P(1);
    CmMarksBegin(&t);
    CHECK(CmMarksSync(&t, 0, 0x0300, snap, 2, FALSE, TRUE, 400), "a re-plug changes the table");
    e = CmMarksFind(&t, P(1));
    CHECK(e != NULL && e->Serial != serial, "a new serial for the re-plug");

    /* Not active: every current entry of the member buried. */
    CmMarksBegin(&t);
    CHECK(CmMarksSync(&t, 0, 0x0300, snap, 2, FALSE, FALSE, 500), "an inactive member buries");
    CHECK_EQ(countKind(0), 0, "no current entry");

    /* The current set full: incomplete, never a tombstone pushed out for it. */
    CmMarksReset(&t);
    for (i = 0; i < 40; i++) {
        snap[i] = P(10 + i);
    }
    CmMarksBegin(&t);
    (VOID)CmMarksSync(&t, 1, 0x0400, snap, 40, FALSE, TRUE, 10);
    CHECK_EQ(countKind(0), CM_QM_MAX_CURRENT, "the current set holds 32");
    CHECK(t.Incomplete, "and reads incomplete past them");
    CmMarksBegin(&t);
    (VOID)CmMarksSync(&t, 1, 0x0400, snap, 20, TRUE, TRUE, 20);
    CHECK(t.Incomplete, "a truncated snapshot reads incomplete");
    CHECK_EQ(countKind(1), 12, "twelve buried");

    /* A member dropped from the set. */
    CmMarksBegin(&t);
    CHECK(CmMarksDropMembers(&t, 1, 30), "the set shrank to member 0");
    CHECK_EQ(countKind(0), 0, "member 1's entries buried");
}

/* An entry buried while its member was inactive and made again from the
 * same snapshot takes back the location its bridge kept (the 7 x86 leg of
 * 2026-10-19: no attachment was left to read it again). */
static void test_relocate(void)
{
    PVOID snap[2];
    ULONG locd[2];
    ULONG bus[2];
    ULONG addr[2];
    PCM_MARK e;

    CmMarksReset(&t);
    snap[0] = P(60);
    snap[1] = P(61);
    CmMarksBegin(&t);
    (VOID)CmMarksSync(&t, 0, 0x0028, snap, 2, FALSE, TRUE, 10);
    CmMarksBegin(&t);
    (VOID)CmMarksSync(&t, 0, 0x0028, snap, 2, FALSE, FALSE, 20);
    CHECK_EQ(countKind(0), 0, "the inactive member's entries buried");
    CmMarksBegin(&t);
    (VOID)CmMarksSync(&t, 0, 0x0028, snap, 2, FALSE, TRUE, 30);
    e = CmMarksFind(&t, P(60));
    CHECK(e != NULL && (e->Flags & CM_QM_EF_LOCATED) == 0, "made again with no location");
    locd[0] = 1;
    bus[0] = 1;
    addr[0] = 0x00010000UL;
    locd[1] = 0;
    bus[1] = 0;
    addr[1] = 0;
    CHECK(CmMarksLocate(&t, 0, snap, locd, bus, addr, 2, 40), "the kept location is taken back");
    CHECK(e->Flags & CM_QM_EF_LOCATED, "located");
    CHECK_EQ(e->Bus, 1, "its bus");
    CHECK_EQ(e->Address, 0x00010000UL, "its address");
    e = CmMarksFind(&t, P(61));
    CHECK(e != NULL && (e->Flags & CM_QM_EF_LOCATED) == 0, "a PDO with nothing kept stays unlocated");
    CHECK(!CmMarksLocate(&t, 0, snap, locd, bus, addr, 2, 50), "nothing more to take");
    CHECK(!CmMarksLocate(&t, 1, snap, locd, bus, addr, 2, 50), "another member's entries are not touched");
}

static void test_tombs(void)
{
    PVOID snap[1];
    ULONG i;
    ULONG gen;

    CmMarksReset(&t);
    /* 40 departures, one at a time, none seen by a poll. */
    for (i = 0; i < 40; i++) {
        snap[0] = P(100 + i);
        CmMarksBegin(&t);
        (VOID)CmMarksSync(&t, 0, 0x0300, snap, 1, FALSE, TRUE, 1000 + i);
        CmMarksBegin(&t);
        (VOID)CmMarksSync(&t, 0, 0x0300, snap, 0, FALSE, TRUE, 1000 + i);
    }
    CHECK_EQ(countKind(1), CM_QM_MAX_TOMBS, "the tombstone set holds 32");
    CHECK_EQ(t.Dropped, 8, "eight evicted unseen, counted");
    /* Another member's departures do not push member 0's out. */
    for (i = 0; i < 40; i++) {
        snap[0] = P(150 + (i % 40));
        CmMarksBegin(&t);
        (VOID)CmMarksSync(&t, 1, 0x0400, snap, 1, FALSE, TRUE, 1000 + i);
        CmMarksBegin(&t);
        (VOID)CmMarksSync(&t, 1, 0x0400, snap, 0, FALSE, TRUE, 1000 + i);
    }
    CHECK_EQ(countKind(1), 2 * CM_QM_MAX_TOMBS, "each member keeps its own 32 tombstones");
    CHECK_EQ(t.Dropped, 16, "eight more dropped, each member's own oldest");
    /* Not seen: kept past the minute. */
    CmMarksEvict(&t, 1000 + 40 + CM_QM_TOMB_MS * 2);
    CHECK_EQ(countKind(1), 2 * CM_QM_MAX_TOMBS, "unseen tombstones outlive the minute");
    gen = t.Gen;
    CmMarksSeen(&t, gen);
    CHECK(CmMarksWasSeen(&t, gen), "a generation read is seen");
    CmMarksEvict(&t, 1000 + 40);
    CHECK_EQ(countKind(1), 2 * CM_QM_MAX_TOMBS, "seen but younger than a minute: kept");
    CmMarksEvict(&t, 1000 + 40 + CM_QM_TOMB_MS);
    CHECK_EQ(countKind(1), 0, "seen and a minute old: gone");
    CHECK(t.Gen != gen, "an eviction moves the generation on");
    /* The wrap. */
    CmMarksReset(&t);
    CmMarksSeen(&t, 0xFFFFFFF0UL);
    CHECK(CmMarksWasSeen(&t, 0xFFFFFFE0UL), "an older generation across the top is seen");
    CmMarksSeen(&t, 5);
    CHECK(CmMarksWasSeen(&t, 0xFFFFFFF8UL), "seen across the wrap");
    CHECK(!CmMarksWasSeen(&t, 6), "a newer one is not");
}

static void test_build(void)
{
    PVOID snap[3];
    CM_QM_IN in;
    PCM_MARK e;
    ULONG bytes;
    ULONG info;
    ULONG rc;
    UCHAR out[CM_QM_MAX_BYTES];

    CmMarksReset(&t);
    snap[0] = P(1);
    snap[1] = P(2);
    CmMarksBegin(&t);
    (VOID)CmMarksSync(&t, 2, 0x0518, snap, 2, FALSE, TRUE, 7);
    snap[0] = P(2);
    CmMarksBegin(&t);
    (VOID)CmMarksSync(&t, 2, 0x0518, snap, 1, FALSE, TRUE, 8);
    e = CmMarksFind(&t, P(2));
    e->Flags = CM_QM_EF_LOCATED | CM_QM_EF_ADD_ATT;
    e->Bus = 6;
    e->Address = 0x00000000UL;
    e->PendingAt = 99;
    e->PendingGen = 4;
    CmMarksCount(&t, e, CM_MK_QUERY_REMOVE, 9);
    CmMarksCount(&t, e, CM_MK_START_FAILED, 9);
    e->Count[CM_MK_CANCEL_REMOVE] = 0xFFFFUL;
    CmMarksCount(&t, e, CM_MK_CANCEL_REMOVE, 9);
    CHECK_EQ(e->Count[CM_MK_CANCEL_REMOVE], 0xFFFF, "a count saturates");
    in.EpochLo = 0x11111111UL;
    in.EpochHi = 0x22222222UL;
    in.NhiBdf = 0x0700;
    in.SafeEjectOff = 0;
    bytes = CmMarksBuild(buf, &t, &in);
    CHECK_EQ(bytes, CM_QM_HDR_BYTES + 2 * CM_QM_ENT_BYTES, "a header and two entries");
    CHECK_EQ(get32(buf, CM_QM_O_MAGIC), CM_QM_MAGIC, "XCMM");
    CHECK_EQ(get32(buf, CM_QM_O_VERSION), 1, "version 1");
    CHECK_EQ(get32(buf, CM_QM_O_BYTES), bytes, "its size");
    CHECK_EQ(get32(buf, CM_QM_O_EPOCH_HI), 0x22222222UL, "the epoch");
    CHECK_EQ(get32(buf, CM_QM_O_GEN), t.Gen, "the generation");
    CHECK_EQ(get32(buf, CM_QM_O_COUNT), 2, "two entries");
    CHECK_EQ(get32(buf, CM_QM_O_CURRENT), 1, "one current");
    CHECK_EQ(get32(buf, CM_QM_O_ENT_BYTES), 64, "64-byte entries");
    CHECK_EQ(get32(buf, CM_QM_O_FLAGS), 0, "complete, safe eject on");
    CHECK_EQ(get32(buf, CM_QM_O_NHI_BDF), 0x0700, "the NHI");
    /* The current entry comes first. */
    CHECK_EQ(get32(buf, 64 + CM_QM_E_MEMBER), 0x0518, "its member");
    CHECK_EQ(get32(buf, 64 + CM_QM_E_BUS), 6, "its bus");
    CHECK_EQ(get32(buf, 64 + CM_QM_E_SERIAL), e->Serial, "its serial");
    CHECK_EQ(get32(buf, 64 + CM_QM_E_FLAGS),
             CM_QM_EF_LOCATED | CM_QM_EF_ADD_ATT | CM_QM_EF_REPORTED, "reported, located, an AddDevice child");
    CHECK_EQ(get32(buf, 64 + CM_QM_E_COUNTS0), 0xFFFF0001UL, "query-remove 1, cancel saturated");
    CHECK_EQ(get32(buf, 64 + CM_QM_E_COUNTS2), 0x00010000UL, "a failed start");
    CHECK_EQ(get32(buf, 64 + CM_QM_E_PENDING), 99, "the departure's time");
    CHECK_EQ(get32(buf, 64 + CM_QM_E_PENDING_GEN), 4, "and its generation");
    CHECK_EQ(get32(buf, 128 + CM_QM_E_FLAGS), 0, "the tombstone: not reported, not located");
    CHECK_EQ(get32(buf, 128 + CM_QM_E_BUS), 0, "and no location");
    in.SafeEjectOff = 1;
    t.Incomplete = 1;
    bytes = CmMarksBuild(buf, &t, &in);
    CHECK_EQ(get32(buf, CM_QM_O_FLAGS), CM_QM_F_INCOMPLETE | CM_QM_F_SAFE_EJECT_OFF, "incomplete, safe eject off");
    CHECK(get32(buf, 64 + CM_QM_E_FLAGS) & CM_QM_EF_SAFE_EJECT_OFF, "each entry says safe eject off");

    rc = CmMarksAnswer(out, 63, buf, bytes, 5, FALSE, &info);
    CHECK_EQ(rc, 2, "below a header: too small");
    rc = CmMarksAnswer(out, 100, buf, bytes, 5, FALSE, &info);
    CHECK_EQ(rc, 1, "below the whole: the header");
    CHECK_EQ(info, 64, "64 bytes");
    CHECK_EQ(get32(out, CM_QM_O_BYTES), bytes, "the size to ask for");
    rc = CmMarksAnswer(out, sizeof(out), buf, bytes, 5, TRUE, &info);
    CHECK_EQ(rc, 0, "whole");
    CHECK_EQ(info, bytes, "every byte");
    CHECK_EQ(get32(out, CM_QM_O_PASS), 5, "the pass filled in");
    CHECK(get32(out, CM_QM_O_FLAGS) & CM_QM_F_STOPPING, "stopping filled in");
}

static void test_confirm(void)
{
    PVOID snap[1];
    PCM_MARK e;
    UCHAR v[CM_QC_BYTES];
    ULONG why;
    ULONG gens[4];

    CmMarksReset(&t);
    snap[0] = P(3);
    CmMarksBegin(&t);
    (VOID)CmMarksSync(&t, 1, 0x0520, snap, 1, FALSE, TRUE, 7);
    e = CmMarksFind(&t, P(3));
    e->Flags = CM_QM_EF_LOCATED;
    e->Bus = 7;
    e->Address = 0x00010000UL;
    gens[0] = 0;
    gens[1] = 9;
    memset(v, 0, sizeof(v));
    put32(v, CM_QC_O_EPOCH_LO, 1);
    put32(v, CM_QC_O_EPOCH_HI, 2);
    put32(v, CM_QC_O_MEMBER, 0x0520);
    put32(v, CM_QC_O_BUS, 7);
    put32(v, CM_QC_O_ADDRESS, 0x00010000UL);
    put32(v, CM_QC_O_SERIAL, e->Serial);
    put32(v, CM_QC_O_GEN, 9);
    CHECK(CmMarksConfirm(&t, v, 27, 1, 2, FALSE, gens, 2, &why) == NULL && why == CM_MK_CONF_SHORT, "short");
    CHECK(CmMarksConfirm(&t, v, 28, 1, 3, FALSE, gens, 2, &why) == NULL && why == CM_MK_CONF_EPOCH, "another start's");
    CHECK(CmMarksConfirm(&t, v, 28, 1, 2, FALSE, gens, 2, &why) == NULL && why == CM_MK_CONF_NOT_PENDING,
          "no departure pending: no eject (a unit stopped by the fallback, a disabled one)");
    e->PendingAt = 50;
    e->PendingGen = 9;
    CHECK(CmMarksConfirm(&t, v, 28, 1, 2, FALSE, gens, 2, &why) == e && why == CM_MK_CONF_OK, "a pending departure confirmed");
    put32(v, CM_QC_O_BUS, 8);
    CHECK(CmMarksConfirm(&t, v, 28, 1, 2, FALSE, gens, 2, &why) == NULL && why == CM_MK_CONF_IDENTITY, "another bus");
    put32(v, CM_QC_O_BUS, 7);
    put32(v, CM_QC_O_SERIAL, e->Serial + 1);
    CHECK(CmMarksConfirm(&t, v, 28, 1, 2, FALSE, gens, 2, &why) == NULL && why == CM_MK_CONF_SERIAL, "another serial");
    put32(v, CM_QC_O_SERIAL, e->Serial);
    gens[1] = 10;
    CHECK(CmMarksConfirm(&t, v, 28, 1, 2, TRUE, gens, 2, &why) == NULL && why == CM_MK_CONF_GEN,
          "USB4: the port's connection changed since");
    CHECK(CmMarksConfirm(&t, v, 28, 1, 2, FALSE, gens, 2, &why) == e, "the ICM client has no generation to check");
    gens[1] = 9;
    CHECK(CmMarksConfirm(&t, v, 28, 1, 2, TRUE, gens, 2, &why) == e, "USB4: the same connection");
    CHECK(CmMarksConfirm(&t, v, 28, 1, 2, TRUE, gens, 1, &why) == NULL, "a member past the ports given");
    e->Flags = 0;
    CHECK(CmMarksConfirm(&t, v, 28, 1, 2, FALSE, gens, 2, &why) == NULL && why == CM_MK_CONF_IDENTITY,
          "a location not known");

    /* Task 43.70: the eject's own departure reported before its
     * confirmation is read (native PCIe hot-plug turns the slot off first):
     * the tombstone that left pending is still confirmable, once. */
    e->Flags = CM_QM_EF_LOCATED;
    e->PendingAt = 50;
    e->PendingGen = 9;
    (VOID)CmMarksSync(&t, 1, 0x0520, snap, 0, FALSE, TRUE, 60);
    CHECK(e->Tomb && e->PendingAt == 0 && e->LeftPendingAt == 50 && e->LeftPendingGen == 9,
          "left with its departure pending: a tombstone that keeps it");
    CHECK(CmMarksConfirm(&t, v, 28, 1, 2, TRUE, gens, 2, &why) == e && why == CM_MK_CONF_OK,
          "its confirmation, read after the departure, names it");
    gens[1] = 10;
    CHECK(CmMarksConfirm(&t, v, 28, 1, 2, TRUE, gens, 2, &why) == NULL && why == CM_MK_CONF_GEN,
          "USB4: not across a new connection of the port");
    gens[1] = 9;
    put32(v, CM_QC_O_ADDRESS, 0x00020000UL);
    CHECK(CmMarksConfirm(&t, v, 28, 1, 2, TRUE, gens, 2, &why) == NULL && why == CM_MK_CONF_IDENTITY,
          "not for another address");
    put32(v, CM_QC_O_ADDRESS, 0x00010000UL);
    e->LeftPendingAt = 0;
    e->LeftPendingGen = 0;
    CHECK(CmMarksConfirm(&t, v, 28, 1, 2, TRUE, gens, 2, &why) == NULL && why == CM_MK_CONF_SERIAL,
          "confirmed once: the tombstone is no longer one");

    /* a function that left with nothing pending is never confirmable */
    CmMarksReset(&t);
    snap[0] = P(4);
    (VOID)CmMarksSync(&t, 1, 0x0520, snap, 1, FALSE, TRUE, 70);
    e = CmMarksFind(&t, P(4));
    e->Flags = CM_QM_EF_LOCATED;
    e->Bus = 7;
    e->Address = 0x00010000UL;
    put32(v, CM_QC_O_SERIAL, e->Serial);
    (VOID)CmMarksSync(&t, 1, 0x0520, snap, 0, FALSE, TRUE, 80);
    CHECK(e->Tomb && e->LeftPendingAt == 0, "left with nothing pending");
    CHECK(CmMarksConfirm(&t, v, 28, 1, 2, TRUE, gens, 2, &why) == NULL && why == CM_MK_CONF_SERIAL,
          "a surprise removal's tombstone is not ejected by a confirmation");

    /* a replacement answering before the late confirmation, with the
     * port's connection unchanged: the departure is forgotten */
    e->PendingAt = 0;
    e->LeftPendingAt = 90;
    e->LeftPendingGen = 9;
    CHECK(CmMarksConfirm(&t, v, 28, 1, 2, TRUE, gens, 2, &why) == e,
          "left pending: confirmable while nothing new answers");
    snap[0] = P(5);
    (VOID)CmMarksSync(&t, 1, 0x0520, snap, 1, FALSE, TRUE, 95);
    CHECK(e->Tomb && e->LeftPendingAt == 0 && e->LeftPendingGen == 0,
          "a new function behind the member forgets the kept departure");
    CHECK(CmMarksConfirm(&t, v, 28, 1, 2, TRUE, gens, 2, &why) == NULL && why == CM_MK_CONF_SERIAL,
          "the late confirmation no longer names anything");
}

/* The unexpected removals (design record 25 section 7.6): an entry found
 * by serial, current or a tombstone; counted once per PDO, only when the
 * attachment saw the device started; a loss counted apart; the block. */
static void test_removals(void)
{
    PVOID snap[4];
    PCM_MARK e1;
    PCM_MARK e2;
    PCM_MARK e3;
    ULONG s1;
    ULONG s2;
    ULONG i;

    CmMarksReset(&t);
    CHECK_EQ(t.RmMember, CM_Q_UNKNOWN, "reset: no member");
    CmMarksRemovalBlock(buf, &t, TRUE);
    CHECK_EQ(get32(buf, CM_Q_RM_FLAGS), CM_Q_RF_WATCHED, "block: watched");
    CHECK_EQ(get32(buf, CM_Q_RM_COUNT), 0, "block: none counted");
    CHECK_EQ(get32(buf, CM_Q_RM_MEMBER), CM_Q_UNKNOWN, "block: no member");
    CHECK_EQ(get32(buf, CM_Q_RM_LOST), 0, "block: none lost");
    CmMarksRemovalBlock(buf, &t, FALSE);
    CHECK_EQ(get32(buf, CM_Q_RM_FLAGS), 0, "block: not watched");
    memset(buf, 0xCC, CM_Q_RM_BYTES);
    CmMarksRemovalBlock(buf, NULL, FALSE);
    CHECK_EQ(get32(buf, CM_Q_RM_MEMBER), CM_Q_UNKNOWN, "no table: no member");
    for (i = 16; i < CM_Q_RM_BYTES; i++) {
        CHECK_EQ(buf[i], 0, "no table: reserved 0");
    }

    snap[0] = P(1);
    snap[1] = P(2);
    snap[2] = P(3);
    CmMarksBegin(&t);
    (void)CmMarksSync(&t, 0, 0x0300, snap, 3, FALSE, TRUE, 100);
    e1 = CmMarksFind(&t, P(1));
    e2 = CmMarksFind(&t, P(2));
    e3 = CmMarksFind(&t, P(3));
    s1 = e1->Serial;
    s2 = e2->Serial;
    CHECK(CmMarksFindSerial(&t, 0) == NULL, "serial 0: none");
    CHECK(CmMarksFindSerial(&t, 999) == NULL, "an unknown serial: none");
    CHECK(CmMarksFindSerial(&t, s2) == e2, "a current entry by serial");

    /* The bus answer drops P(1) first: its entry is a tombstone. */
    snap[0] = P(2);
    snap[1] = P(3);
    CmMarksBegin(&t);
    (void)CmMarksSync(&t, 0, 0x0300, snap, 2, FALSE, TRUE, 200);
    CHECK(CmMarksFind(&t, P(1)) == NULL, "P(1) no longer current");
    e1 = CmMarksFindSerial(&t, s1);
    CHECK(e1 != NULL && e1->Tomb, "the tombstone found by serial");
    CHECK(CmMarksSurprise(&t, e1, TRUE, FALSE, 210), "counted");
    CHECK_EQ(t.Removals, 1, "one removal");
    CHECK_EQ(t.RmMember, 0x0300, "its member");
    CHECK_EQ(e1->Count[CM_MK_SURPRISE], 1, "the entry's own count");
    CHECK(!CmMarksSurprise(&t, e1, TRUE, FALSE, 211),
          "a second attachment of the same PDO: not again");
    CHECK_EQ(t.Removals, 1, "still one");
    CHECK_EQ(e1->Count[CM_MK_SURPRISE], 2, "the entry's count still moves");

    /* A device the attachment never saw started; then one that did. */
    CHECK(!CmMarksSurprise(&t, e2, FALSE, FALSE, 220), "never started: not");
    CHECK_EQ(t.Removals, 1, "unchanged");
    CHECK(CmMarksSurprise(&t, e2, TRUE, FALSE, 221),
          "another attachment saw it started: counted");
    CHECK_EQ(t.Removals, 2, "two");

    /* In a loss: apart, once. */
    CHECK(!CmMarksSurprise(&t, e3, TRUE, TRUE, 230), "a loss: not unexpected");
    CHECK_EQ(t.Lost, 1, "counted apart");
    CHECK_EQ(t.Removals, 2, "the count unchanged");
    CHECK(!CmMarksSurprise(&t, e3, TRUE, FALSE, 231),
          "the same PDO after the loss ended: not again");
    CHECK_EQ(t.Removals, 2, "still two");

    CmMarksRemovalBlock(buf, &t, TRUE);
    CHECK_EQ(get32(buf, CM_Q_RM_COUNT), 2, "block: two");
    CHECK_EQ(get32(buf, CM_Q_RM_MEMBER), 0x0300, "block: the member");
    CHECK_EQ(get32(buf, CM_Q_RM_LOST), 1, "block: one lost");

    /* The counts wrap. */
    t.Removals = 0xFFFFFFFFUL;
    snap[0] = P(4);
    CmMarksBegin(&t);
    (void)CmMarksSync(&t, 1, 0x0400, snap, 1, FALSE, TRUE, 300);
    CHECK(CmMarksSurprise(&t, CmMarksFind(&t, P(4)), TRUE, FALSE, 301),
          "counted past the top");
    CHECK_EQ(t.Removals, 0, "wrapped");
    CHECK_EQ(t.RmMember, 0x0400, "the last one's member");

    CmMarksReset(&t);
    CHECK_EQ(t.Removals | t.Lost, 0, "reset: counts 0");
    CHECK(CmMarksFindSerial(&t, s1) == NULL, "reset: no entry");

    /* The binding's gates: the start, the serial, the PDO. */
    snap[0] = P(5);
    CmMarksBegin(&t);
    (void)CmMarksSync(&t, 0, 0x0300, snap, 1, FALSE, TRUE, 400);
    e1 = CmMarksFind(&t, P(5));
    s1 = e1->Serial;
    CHECK(CmMarksBound(&t, 2, 2, s1, P(5)) == e1, "bound: found");
    CHECK(CmMarksBound(&t, 3, 2, s1, P(5)) == NULL,
          "bound in an earlier start: refused (serials begin again)");
    CHECK(CmMarksBound(&t, 2, 2, s1, P(6)) == NULL,
          "another PDO under that serial: refused");
    CHECK(CmMarksBound(&t, 2, 2, 0, P(5)) == NULL, "never bound: refused");
    CHECK(CmMarksBound(&t, 2, 2, s1, NULL) == NULL, "no PDO: refused");
    /* The member stops and starts: buried, then a new entry and serial
     * for the same PDO; the old serial still finds the tombstone. */
    CmMarksBegin(&t);
    (void)CmMarksSync(&t, 0, 0x0300, NULL, 0, FALSE, FALSE, 410);
    CmMarksBegin(&t);
    (void)CmMarksSync(&t, 0, 0x0300, snap, 1, FALSE, TRUE, 420);
    e2 = CmMarksFind(&t, P(5));
    s2 = e2->Serial;
    CHECK(s2 != s1, "a new serial after the member's restart");
    CHECK(CmMarksBound(&t, 2, 2, s2, P(5)) == e2, "rebound: the new entry");
    e3 = CmMarksBound(&t, 2, 2, s1, P(5));
    CHECK(e3 != NULL && e3->Tomb, "the old binding: its tombstone");

    /* The loss predicate. */
    CHECK(!CmMarksLoss(TRUE, FALSE, 0, 0, 1), "ACTIVE, nothing owed: no loss");
    CHECK(CmMarksLoss(FALSE, FALSE, 0, 0, 1), "not ACTIVE: a loss");
    CHECK(CmMarksLoss(TRUE, TRUE, 0, 0, 1), "a loss drained: a loss");
    CHECK(CmMarksLoss(TRUE, FALSE, 0, 0x2, 1), "ACTIVE, the member owed: a loss");
    CHECK(CmMarksLoss(TRUE, FALSE, 0x2, 0, 1),
          "ACTIVE, the member barred: a loss");
    CHECK(!CmMarksLoss(TRUE, FALSE, 0x1, 0x4, 1),
          "another member owed or barred: no loss");
    CHECK(!CmMarksLoss(TRUE, FALSE, 0xFFFFFFFFUL, 0, 32),
          "a member past the mask: no loss by it");
}

/* Record 31 section 4.1: a child attached for the id on a bridge that is
 * not active marks nothing; on an active, exclusive member with safe
 * eject on, its unit does. */
static void test_may_mark(void)
{
    /* located, locBus, active, exclusive, secBusValid, secBus, inMarks, safeEject */
    CHECK_EQ(CmPortsMayMark(1, 5, 1, 1, 1, 5, 1, 1), CM_MM_YES,
             "a unit of an active, exclusive member with safe eject on: Removable");
    CHECK_EQ(CmPortsMayMark(1, 5, 0, 1, 1, 5, 1, 1), CM_MM_NO,
             "the bridge not active (the NHI's or the host xHCI's switch port): nothing");
    CHECK_EQ(CmPortsMayMark(1, 5, 1, 0, 1, 5, 1, 1), CM_MM_NO,
             "not exclusive (another domain claims it): nothing");
    CHECK_EQ(CmPortsMayMark(1, 5, 1, 1, 1, 5, 1, 0), CM_MM_SEEN,
             "safe eject off: seen, not Removable");
    CHECK_EQ(CmPortsMayMark(1, 5, 0, 0, 1, 5, 1, 0), CM_MM_NO,
             "none of the three: nothing");
    CHECK_EQ(CmPortsMayMark(0, 5, 1, 1, 1, 5, 1, 1), CM_MM_NO,
             "not located: nothing");
    CHECK_EQ(CmPortsMayMark(1, 6, 1, 1, 1, 5, 1, 1), CM_MM_NO,
             "a deeper bus (a function behind the enclosure's switch): nothing");
    CHECK_EQ(CmPortsMayMark(1, 5, 1, 1, 0, 5, 1, 1), CM_MM_NO,
             "the member's secondary bus unknown: nothing");
    CHECK_EQ(CmPortsMayMark(1, 5, 1, 1, 1, 5, 0, 1), CM_MM_NO,
             "no marks entry for the PDO: nothing");
}

int main(void)
{
    test_may_mark();
    test_removals();
    test_sync();
    test_relocate();
    test_tombs();
    test_build();
    test_confirm();
    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
