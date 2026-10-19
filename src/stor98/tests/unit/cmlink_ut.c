/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, Yeo Kheng Meng
 *
 * cmlink_ut - host unit test of src/cmlink.h, the decisions of the NT
 * receiver of the CM link-state table (include/stor98cm.h interface 2,
 * MD-6): table acceptance (interface version 4 only), one table per boot,
 * binding, the odd/even entry protocol, LinkGeneration and AckGeneration
 * handling, and identity loss. The tables are built here; every load the
 * receiver makes goes through a mock that can tear a snapshot (the CM
 * updating the entry between the two reads of Sequence). Built by
 * tests\unit\build.cmd; the exit code is the number of failed checks.
 */

#include <stdio.h>
#include <string.h>

static volatile unsigned long *tearSeq;    /* Sequence bumped by the mock */
static int tearLoads;                       /* loads until the next bump (0 off) */
static int tearTimes;                       /* bumps left */

static unsigned long MockLoad(volatile unsigned long *p)
{
    if (tearSeq != 0 && tearTimes > 0 && --tearLoads == 0) {
        *tearSeq += 2;                      /* a whole CM update in between */
        tearTimes--;
        tearLoads = 6;                      /* one snapshot later */
    }
    return *p;
}

#define CMLINK_LOAD(p)  MockLoad((volatile unsigned long *)(p))
#include "nvme2k.h"

static int failures;

static void check(int ok, const char *what)
{
    printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) {
        failures++;
    }
}

#define BUS     5
#define SLOT    0x00        /* device 0, function 0 */

static STOR98LS_TABLE table;
static CML_STATE st;

static void reset(void)
{
    memset(&table, 0, sizeof(table));
    table.Signature = STOR98LS_SIGNATURE;
    table.Version = STOR98LS_VERSION;
    table.Count = STOR98LS_ENTRIES;
    memset(&st, 0, sizeof(st));
    st.Table = &table;
    tearSeq = 0;
    tearTimes = 0;
}

static void put(ULONG i, ULONG bus, ULONG slot, ULONG gen, ULONG flags)
{
    PSTOR98LS_ENTRY e = &table.Entry[i];

    e->Sequence++;
    e->BusNumber = bus;
    e->SlotNumber = slot;
    e->LinkGeneration = gen;
    e->Flags = flags;
    e->Sequence++;
}

#define UP      (STOR98LS_IN_USE | STOR98LS_UP)
#define DQ      (STOR98LS_IN_USE | STOR98LS_DOWN_QUIESCED)
#define ALONE   (STOR98LS_IN_USE)

int main(void)
{
    PSTOR98LS_TABLE boot = NULL;
    STOR98LS_TABLE other;
    ULONG r;

    printf("cmlink_ut: table acceptance\n");
    reset();
    check(CmlCheckTable(&table) == CML_RC_BOUND, "version 4 table accepted");
    table.Version = 1;
    check(CmlCheckTable(&table) == CML_RC_VERSION, "version 1 (the version-1 table header) refused");
    table.Version = 5;
    check(CmlCheckTable(&table) == CML_RC_VERSION, "version 5 refused");
    table.Version = STOR98LS_VERSION;
    table.Signature = 0x4C38394E;           /* 'N98L' */
    check(CmlCheckTable(&table) == CML_RC_SIGNATURE, "old signature refused");
    table.Signature = STOR98LS_SIGNATURE;
    table.Count = 7;
    check(CmlCheckTable(&table) == CML_RC_COUNT, "count 7 refused");

    printf("cmlink_ut: one table per boot\n");
    check(CmlBootTable(&boot, &table) == CML_RC_BOUND && boot == &table, "first table recorded");
    check(CmlBootTable(&boot, &table) == CML_RC_BOUND, "same table again accepted");
    check(CmlBootTable(&boot, &other) == CML_RC_SECOND_TABLE && boot == &table, "second table refused");

    printf("cmlink_ut: binding\n");
    reset();
    check(CmlBind(&st, BUS, SLOT) == CML_RC_NO_ENTRY && st.Entry == NULL, "empty table: no entry");
    put(0, BUS + 1, SLOT, 4, UP);
    check(CmlBind(&st, BUS, SLOT) == CML_RC_NO_ENTRY, "another function's entry ignored");
    put(3, BUS, SLOT, 7, UP);
    check(CmlBind(&st, BUS, SLOT) == CML_RC_BOUND && st.Entry == &table.Entry[3] && st.Gen == 7,
          "UP entry bound, generation 7 recorded");
    check(CmlWantsPoll(&st), "bound: the timer polls");
    put(3, BUS, SLOT, 8, ALONE);
    check(CmlBind(&st, BUS, SLOT) == CML_RC_BOUND && st.Gen == 8, "IN_USE alone (retired) bound");
    put(3, BUS, SLOT, 8, DQ);
    check(CmlBind(&st, BUS, SLOT) == CML_RC_DOWN_AT_BIND && st.Entry == NULL, "DOWN_QUIESCED at binding refused");
    put(3, BUS, SLOT, 8, UP | STOR98LS_DOWN_QUIESCED);
    check(CmlBind(&st, BUS, SLOT) == CML_RC_BAD_ENTRY, "UP with DOWN_QUIESCED refused");
    put(3, BUS, SLOT, 8, UP | STOR98LS_REMOVE_PENDING);
    check(CmlBind(&st, BUS, SLOT) == CML_RC_BAD_ENTRY, "REMOVE_PENDING (9x only) refused on NT");
    put(3, BUS, SLOT, 0, UP);
    check(CmlBind(&st, BUS, SLOT) == CML_RC_BAD_ENTRY, "UP with generation 0 refused");
    put(3, BUS, SLOT, 2, UP);
    put(5, BUS, SLOT, 2, UP);
    check(CmlBind(&st, BUS, SLOT) == CML_RC_DUPLICATE && st.Entry == NULL, "two entries for the function refused");
    put(5, 0, 0, 0, 0);
    put(3, 0, 0, 0, 0);
    put(1, BUS, SLOT, 2, UP);
    table.Entry[6].Sequence = 9;            /* our own entry stuck mid-update */
    table.Entry[6].BusNumber = BUS;
    table.Entry[1].Sequence = 9;
    check(CmlBind(&st, BUS, SLOT) == CML_RC_BUSY && st.Entry == NULL,
          "own entry odd through every pass: generation unknown, no channel");

    printf("cmlink_ut: another entry mid-update at binding (Codex round 1)\n");
    reset();
    put(1, BUS, SLOT, 2, UP);
    table.Entry[6].Sequence = 9;            /* another port's entry stuck mid-update */
    check(CmlBind(&st, BUS, SLOT) == CML_RC_BOUND && st.Gen == 2 && st.DupPending == (1 << 6),
          "bound on the own entry, the unread entry left to the polls");
    check(CmlWantsPoll(&st), "the timer polls while the check is pending");
    put(1, BUS, SLOT, 2, DQ);
    check(CmlPoll(&st) == PLAT_LINK_NO_INFO && st.LinkGen == 0, "no statement taken while the duplicate check is pending");
    table.Entry[6].Sequence = 10;           /* the update finished: another function */
    table.Entry[6].BusNumber = BUS + 3;
    table.Entry[6].Flags = UP;
    table.Entry[6].LinkGeneration = 1;
    check(CmlPoll(&st) == PLAT_LINK_DOWN_QUIESCED && st.DupPending == 0 && st.LinkOwn,
          "check done: the own statement applied");
    check(CmlAcknowledged(&st) == 2, "and acknowledged with the generation read at binding");
    reset();
    put(1, BUS, SLOT, 2, UP);
    table.Entry[6].Sequence = 9;
    CmlBind(&st, BUS, SLOT);
    table.Entry[6].Sequence = 10;           /* it named this function too */
    table.Entry[6].BusNumber = BUS;
    table.Entry[6].SlotNumber = SLOT;
    table.Entry[6].Flags = UP;
    table.Entry[6].LinkGeneration = 2;
    put(1, BUS, SLOT, 2, DQ);
    check(CmlPoll(&st) == PLAT_LINK_NO_INFO && st.Lost == CML_LOST_DUPLICATE && !CmlWantsPoll(&st),
          "a duplicate found later ends the binding: no evidence, no acknowledgement");

    printf("cmlink_ut: entry protocol and acknowledgement\n");
    reset();
    put(1, BUS, SLOT, 3, UP);
    CmlBind(&st, BUS, SLOT);
    check(CmlPoll(&st) == PLAT_LINK_UP, "UP for the bound generation");
    put(1, BUS, SLOT, 3, ALONE);
    check(CmlPoll(&st) == PLAT_LINK_NO_INFO && CmlWantsPoll(&st), "DOWN without QUIESCED ignored, still polled");
    table.Entry[1].Sequence++;              /* the CM is writing (odd) */
    table.Entry[1].Flags = DQ;
    check(CmlPoll(&st) == PLAT_LINK_NO_INFO && st.Busy == 1 && st.LinkGen == 0, "odd Sequence: read again later");
    table.Entry[1].Sequence++;
    check(CmlPoll(&st) == PLAT_LINK_DOWN_QUIESCED && st.LinkGen == 3 && st.LinkOwn, "DOWN_QUIESCED for the bound generation");
    put(1, BUS, SLOT, 4, UP);               /* even a change after the report */
    check(CmlPoll(&st) == PLAT_LINK_DOWN_QUIESCED, "reported until acknowledged");
    r = CmlAcknowledged(&st);
    check(r == 3, "AckGeneration = bound generation");
    check(!CmlWantsPoll(&st) && CmlPoll(&st) == PLAT_LINK_NO_INFO, "one statement per binding: not read again");
    check(CmlAcknowledged(&st) == 0, "no second acknowledgement");

    printf("cmlink_ut: snapshot torn by a CM update\n");
    reset();
    put(2, BUS, SLOT, 6, UP);
    CmlBind(&st, BUS, SLOT);
    put(2, BUS, SLOT, 6, DQ);
    tearSeq = &table.Entry[2].Sequence;
    tearLoads = 3;                          /* after s1 and two payload loads */
    tearTimes = 1;
    check(CmlPoll(&st) == PLAT_LINK_DOWN_QUIESCED, "torn once: the second attempt reads it");
    reset();
    put(2, BUS, SLOT, 6, UP);
    CmlBind(&st, BUS, SLOT);
    put(2, BUS, SLOT, 6, DQ);
    tearSeq = &table.Entry[2].Sequence;
    tearLoads = 3;
    tearTimes = 2;
    check(CmlPoll(&st) == PLAT_LINK_NO_INFO && st.Busy == 1, "torn twice: poll skipped, no spinning");
    tearSeq = 0;
    check(CmlPoll(&st) == PLAT_LINK_DOWN_QUIESCED, "next poll reads it");

    printf("cmlink_ut: generations\n");
    reset();
    put(0, BUS, SLOT, 5, UP);
    CmlBind(&st, BUS, SLOT);
    put(0, BUS, SLOT, 6, UP);
    check(CmlPoll(&st) == PLAT_LINK_NO_INFO && CmlWantsPoll(&st), "newer UP alone is not evidence on NT");
    put(0, BUS, SLOT, 6, DQ);
    check(CmlPoll(&st) == PLAT_LINK_DOWN_QUIESCED && !st.LinkOwn, "DOWN_QUIESCED for a newer generation is evidence");
    check(CmlAcknowledged(&st) == 0 && st.Applied == 6, "applied, but not acknowledged (a newer instance's)");
    reset();
    put(0, BUS, SLOT, 5, UP);
    CmlBind(&st, BUS, SLOT);
    put(0, BUS, SLOT, 4, DQ);
    check(CmlPoll(&st) == PLAT_LINK_NO_INFO && st.Lost == CML_LOST_OLDER, "older generation: identity lost");
    put(0, BUS, SLOT, 5, DQ);
    check(CmlPoll(&st) == PLAT_LINK_NO_INFO && !CmlWantsPoll(&st), "after identity loss nothing is taken");
    reset();
    put(0, BUS, SLOT, 5, UP);
    CmlBind(&st, BUS, SLOT);
    put(0, BUS + 2, SLOT, 5, DQ);
    check(CmlPoll(&st) == PLAT_LINK_NO_INFO && st.Lost == CML_LOST_PLACE, "another bus: identity lost");
    reset();
    put(0, BUS, SLOT, 5, UP);
    CmlBind(&st, BUS, SLOT);
    put(0, BUS, SLOT, 5, 0);
    check(CmlPoll(&st) == PLAT_LINK_NO_INFO && st.Lost == CML_LOST_IN_USE, "IN_USE cleared: identity lost");
    reset();
    put(0, BUS, SLOT, 5, UP);
    CmlBind(&st, BUS, SLOT);
    put(0, BUS, SLOT, 5, DQ | STOR98LS_REMOVE_PENDING);
    check(CmlPoll(&st) == PLAT_LINK_NO_INFO && st.Lost == CML_LOST_FLAGS, "unknown flag bit: identity lost, no evidence");

    printf("cmlink_ut: CM unloaded (table frozen)\n");
    reset();
    put(4, BUS, SLOT, 2, UP);
    CmlBind(&st, BUS, SLOT);
    for (r = 0; r < 100; r++) {
        CmlPoll(&st);
    }
    check(CmlPoll(&st) == PLAT_LINK_UP && st.LinkGen == 0 && CmlWantsPoll(&st),
          "a table that stops changing gives no statement (MD-1 applies)");

    printf("cmlink_ut: no table\n");
    memset(&st, 0, sizeof(st));
    check(!CmlWantsPoll(&st) && CmlPoll(&st) == PLAT_LINK_NO_INFO && CmlAcknowledged(&st) == 0,
          "no binding: no poll, no statement, no acknowledgement");

    printf("cmlink_ut: %d failure(s)\n", failures);
    return failures;
}
