/*
 * test_cm_marks.c - the marks answer's table (src/cm/cm_marks.c; task
 * 42-A.5; design record 21 section 7.4, revision 3): the current and
 * tombstone sets kept apart, a tombstone kept until a poll has seen it and
 * CM_QM_TOMB_MS has passed, the oldest evicted when full and counted when
 * unseen, the answer's layout, and the confirmation's checks.
 */

#include <stdio.h>
#include <string.h>
#include "../src/cm/cm_marks.h"
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
}

int main(void)
{
    test_sync();
    test_tombs();
    test_build();
    test_confirm();
    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
