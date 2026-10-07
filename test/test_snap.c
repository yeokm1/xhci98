/*
 * test_snap.c - host vectors for the snapshot's HCD region layout
 * (src\xhci_snap.c; src\xhci.h, XHCI_SNAPSHOT_REGION_HCD; roadmap-hcd.md
 * task 35.3).
 *
 * The door fills the image word by word from where XhciSnapHcdLocate says
 * each word lies, and XHCISNAP walks it by the sizes the header gives, so
 * the two must agree: every word of the image is located exactly once, in
 * order, the header's offsets name the words Locate puts there, and the
 * counter block is the whole of XHCIHC_COUNTERS.
 */

#include <stdio.h>
#include "../src/xhci.h"
#include "../src/xhci_counters.h"
#include "../src/xhci_enum.h"
#include "test_harness.h"

#define H XHCI_SNAPSHOT_HCD_HEAD_WORDS

static void test_header(void)
{
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_VERSION_AT), 1,
             "version 1");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_HEAD_BYTES), 92,
             "a twenty-three-word header (35-T.8 appended twelve, 36.2 three)");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_PORTS), 18,
             "the E460's eighteen root ports");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_PORT_BYTES), 64,
             "a sixteen-word record");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_PORTS_AT), 92,
             "records right after the header");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_COUNTERS), 69,
             "the whole counter block (36.1 and 36.2 appended six)");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_COUNTERS_AT),
             92 + 18 * 64, "counters after the last record");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_CLIENTS), 4,
             "four client reports kept");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_CLIENT_BYTES), 128,
             "a thirty-two-word report record");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_CLIENTS_AT),
             92 + 18 * 64 + 69 * 4, "reports after the counters");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_NOTE_BUDGET),
             XHCI_ENUM_NOTE_BUDGET, "the notes' budget");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_HEAD_WORDS), 0,
             "past the header reads 0");
    CHECK_EQ(XHCI_SNAPSHOT_HCD_COUNTER_WORDS * sizeof(ULONG),
             sizeof(XHCIHC_COUNTERS), "the counters word for word");
    CHECK_EQ(XhciSnapHcdWords(18), 23 + 18 * 16 + 69 + 4 * 32,
             "the E460's image");
    CHECK_EQ(XhciSnapHcdWords(0), 23 + 69 + 4 * 32,
             "no ports: a header, the counters and the reports");
    CHECK_EQ(XhciSnapHcdWords(1000), XhciSnapHcdWords(XHCI_MAX_ROOT_PORTS),
             "ports held to the root port maximum");
    CHECK_EQ(XhciSnapHcdHead(1000, XHCI_SNAPSHOT_HCD_PORTS),
             XHCI_MAX_ROOT_PORTS, "and so is the header's count");
    CHECK(XhciSnapHcdWords(XHCI_MAX_ROOT_PORTS) * 4UL <= 0xF000UL - 256UL,
          "the largest image fits one of XHCISNAP's windows");
}

/* Every word located once, in order: header, each port's record, the
 * counters, then past the end; and the header's offsets agree. */
static void test_walk(ULONG ports)
{
    ULONG total;
    ULONG index;
    ULONG part;
    ULONG record;
    ULONG word;
    ULONG expectPart;
    ULONG expectRecord;
    ULONG expectWord;
    ULONG bad;

    total = XhciSnapHcdWords(ports);
    bad = 0;
    for (index = 0; index < total + 3; index++) {
        part = XhciSnapHcdLocate(ports, index, &record, &word);
        if (index < H) {
            expectPart = XHCI_SNAPSHOT_HCD_IN_HEAD;
            expectRecord = 0;
            expectWord = index;
        } else if (index < H + ports * 16) {
            expectPart = XHCI_SNAPSHOT_HCD_IN_PORT;
            expectRecord = (index - H) / 16;
            expectWord = (index - H) % 16;
        } else if (index < H + ports * 16 + 69) {
            expectPart = XHCI_SNAPSHOT_HCD_IN_COUNTERS;
            expectRecord = 0;
            expectWord = index - H - ports * 16;
        } else if (index < total) {
            expectPart = XHCI_SNAPSHOT_HCD_IN_CLIENTS;
            expectRecord = (index - H - ports * 16 - 69) / 32;
            expectWord = (index - H - ports * 16 - 69) % 32;
        } else {
            expectPart = XHCI_SNAPSHOT_HCD_PAST_END;
            expectRecord = 0;
            expectWord = 0;
        }
        if (part != expectPart || record != expectRecord ||
            word != expectWord) {
            bad++;
        }
    }
    CHECK_EQ(bad, 0, "every word located where the layout puts it");
    CHECK_EQ(XhciSnapHcdLocate(ports,
                               XhciSnapHcdHead(ports,
                                               XHCI_SNAPSHOT_HCD_COUNTERS_AT) /
                                   4,
                               &record, &word),
             XHCI_SNAPSHOT_HCD_IN_COUNTERS,
             "the header's counter offset is the first counter");
    CHECK_EQ(word, 0, "word 0 of the counters");
    CHECK_EQ(XhciSnapHcdLocate(ports,
                               XhciSnapHcdHead(ports,
                                               XHCI_SNAPSHOT_HCD_CLIENTS_AT) /
                                   4,
                               &record, &word),
             XHCI_SNAPSHOT_HCD_IN_CLIENTS,
             "the header's report offset is the first report");
    CHECK_EQ(record + word, 0, "report 0's first word");
    CHECK_EQ(XhciSnapHcdLocate(ports, total - 1, &record, &word),
             XHCI_SNAPSHOT_HCD_IN_CLIENTS, "the image ends with a report");
    CHECK_EQ(record, XHCI_SNAPSHOT_HCD_CLIENTS_KEPT - 1, "the last one");
    CHECK_EQ(word, XHCI_SNAPSHOT_HCD_CLIENT_WORDS - 1, "its last word");
    if (ports != 0) {
        CHECK_EQ(XhciSnapHcdLocate(ports,
                                   XhciSnapHcdHead(ports,
                                                   XHCI_SNAPSHOT_HCD_PORTS_AT) /
                                       4,
                                   &record, &word),
                 XHCI_SNAPSHOT_HCD_IN_PORT,
                 "the header's record offset is the first record");
        CHECK_EQ(record + word, 0, "port 1's first word");
        CHECK_EQ(XhciSnapHcdLocate(ports, H + ports * 16 - 1, &record,
                                   &word),
                 XHCI_SNAPSHOT_HCD_IN_PORT, "the last record's last word");
        CHECK_EQ(record, ports - 1, "is the last port's");
        CHECK_EQ(word, XHCI_SNAPSHOT_HCD_PORT_FLAGS, "its flags");
    }
}

static void test_record_words(void)
{
    CHECK_EQ(XHCI_SNAPSHOT_HCD_PORT_ID, 0, "port number first");
    CHECK_EQ(XHCI_SNAPSHOT_HCD_PORT_STATE, 1, "then the machine's state");
    CHECK_EQ(XHCI_SNAPSHOT_HCD_PORT_CAUSE, 2, "its failure's cause");
    CHECK_EQ(XHCI_SNAPSHOT_HCD_PORT_PSIV, 5, "the raw speed ID at 5");
    CHECK_EQ(XHCI_SNAPSHOT_HCD_PORT_SOURCE, 7, "its meaning's source at 7");
    CHECK_EQ(XHCI_SNAPSHOT_HCD_PORT_REFUSED, 13, "refused notes at 13");
    CHECK_EQ(XHCI_SNAPSHOT_HCD_PORT_FLAGS, XHCI_SNAPSHOT_HCD_PORT_WORDS - 1,
             "flags last");
    CHECK_EQ(XhciSnapHcdFlags(0, 0, 0), 0, "no flags");
    CHECK_EQ(XhciSnapHcdFlags(1, 0, 0), XHCI_SNAPSHOT_HCD_F_DEFERRED,
             "deferred by the settle");
    CHECK_EQ(XhciSnapHcdFlags(0, 7, 0), XHCI_SNAPSHOT_HCD_F_RECOVERING,
             "a warm reset in flight, whatever its count");
    CHECK_EQ(XhciSnapHcdFlags(0, 0, 3), XHCI_SNAPSHOT_HCD_F_UNREADABLE,
             "PORTSC unreadable");
    CHECK_EQ(XhciSnapHcdFlags(1, 1, 1), 7, "all three, bits 0 to 2");
}

/* 35-T.8: the words that place the tolerance state in the extension image.
 * XHCISNAP reads them at these fixed header positions and believes each
 * only inside the image, so every part must lie inside the extension, the
 * counters first, and the locations whole. */
static void test_tol_words(void)
{
    ULONG at;
    ULONG bytes;
    ULONG loc;

    CHECK_EQ(XHCI_SNAPSHOT_HCD_TOL_AT, 8, "appended after the notes' budget");
    CHECK_EQ(XHCI_SNAPSHOT_HCD_TERMINAL, 19,
             "the terminal reason last of 35-T.8's words");
    CHECK_EQ(XHCI_SNAPSHOT_HCD_CLIENTS_AT, XHCI_SNAPSHOT_HCD_HEAD_WORDS - 1,
             "36.2's report offset last");
    at = XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_TOL_AT);
    bytes = XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_TOL_BYTES);
    loc = XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_TOL_LOC_BYTES);
    CHECK_EQ(at, XHCI_FIELD_OFFSET(XHCI_EXTENSION, Tol), "where Tol lies");
    CHECK_EQ(bytes, sizeof(XHCI_TOL_STATE), "its size");
    CHECK(at + bytes <= sizeof(XHCI_EXTENSION), "inside the extension");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_TOL_STATS) * 4,
             sizeof(XHCI_TOL_STATS), "the counters, word for word");
    CHECK_EQ(XHCI_FIELD_OFFSET(XHCI_TOL_STATE, Stats), 0, "counters first");
    CHECK_EQ(XHCI_FIELD_OFFSET(XHCI_TOL_STATS, Codes) + 256 * 4,
             sizeof(XHCI_TOL_STATS), "the histogram ends the counters");
    /* XHCISNAP's names (xhcisnap.c, tol_counter_names) are these 34 words
     * in field order; a counter added before the histogram fails here
     * until the tool names it. */
    CHECK_EQ(XHCI_FIELD_OFFSET(XHCI_TOL_STATS, Codes), 34 * 4,
             "the thirty-four counters XHCISNAP names");
    CHECK_EQ(XHCI_FIELD_OFFSET(XHCI_TOL_STATS, Cycles), 17 * 4,
             "Cycles at word 17");
    CHECK_EQ(XHCI_FIELD_OFFSET(XHCI_TOL_STATS, Contained), 33 * 4,
             "Contained at word 33");
    /* XHCISNAP judges the window's stamps itself (SNAP_TOL_WINDOW_TICKS),
     * since the driver prunes them only at the next admission. */
    CHECK_EQ(XHCI_TOL_RECOVERY_WINDOW_TICKS, 13335,
             "the recovery window XHCISNAP restates");
    CHECK_EQ(loc, 32, "a location is eight words");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_TOL_ROOT_LOCS), 255,
             "every root port");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_TOL_HUB_LOCS), 16 * 14,
             "every hub port object");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_TOL_HUB_PORTS), 14,
             "fourteen to a hub object");
    CHECK(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_TOL_ROOT_AT) + 255 * loc <=
              bytes,
          "the root locations inside Tol");
    CHECK(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_TOL_HUB_AT) + 224 * loc <=
              bytes,
          "the hub locations inside Tol");
    CHECK(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_TOL_WINDOW_AT) +
                  sizeof(XHCI_TOL_WINDOW) <= bytes,
          "the window inside Tol");
    CHECK_EQ(sizeof(XHCI_TOL_WINDOW), 5 * 4,
             "the window: count, three stamps, refused");
    CHECK(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_TOL_CLOCK_AT) + 4 <= bytes,
          "the clock inside Tol");
    CHECK_EQ(XHCI_FIELD_OFFSET(XHCI_TOL_LOC, Reenums), 24,
             "the byte fields after six words");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_TERMINAL), 0,
             "the terminal reason is the door's, not layout");
    CHECK_EQ(XhciSnapHcdHead(0, XHCI_SNAPSHOT_HCD_TOL_AT), at,
             "independent of the port count");
}

/* 36.2: the report records - its own device's record again, an empty one,
 * then the oldest; the write's stamp, fields and zeroed tail. */
static void test_clients(void)
{
    ULONG rec[XHCI_SNAPSHOT_HCD_CLIENTS_KEPT * XHCI_SNAPSHOT_HCD_CLIENT_WORDS];
    ULONG words[40];
    ULONG i;
    ULONG pick;
    ULONG stamp;
    ULONG bad;

    for (i = 0; i < sizeof(rec) / sizeof(rec[0]); i++) {
        rec[i] = 0xEEEEEEEEUL;
    }
    for (i = 0; i < XHCI_SNAPSHOT_HCD_CLIENTS_KEPT; i++) {
        rec[i * 32] = 0;
    }
    for (i = 0; i < 40; i++) {
        words[i] = 0x100UL + i;
    }
    CHECK_EQ(XhciSnapClientPick(rec, 4, 1, 0x0D000000UL), 0,
             "all empty: the first");
    XhciSnapClientStore(rec, 1, 1, 0x0D000000UL, 0x31534155UL, words, 28);
    CHECK_EQ(rec[0], 1, "stamped");
    CHECK_EQ(rec[1], 1, "the slot");
    CHECK_EQ(rec[2], 0x0D000000UL, "the place");
    CHECK_EQ(rec[3], 0x31534155UL, "the kind");
    CHECK_EQ(rec[4], 0x100, "the client's first word");
    CHECK_EQ(rec[31], 0x100 + 27, "and its last");
    CHECK_EQ(rec[32], 0, "the next record untouched");
    CHECK_EQ(XhciSnapClientPick(rec, 4, 1, 0x0D000000UL), 0,
             "the same device: its own record");
    CHECK_EQ(XhciSnapClientPick(rec, 4, 1, 0x0E000000UL), 1,
             "the same slot on another port: a new record");
    CHECK_EQ(XhciSnapClientPick(rec, 4, 2, 0x0D000000UL), 1,
             "another slot: a new record");

    XhciSnapClientStore(rec, 0, 9, 0, 0, words, 3);
    CHECK_EQ(rec[0], 1, "a zero stamp written as 1");
    CHECK_EQ(rec[6], 0x102, "three client words");
    bad = 0;
    for (i = 7; i < 32; i++) {
        bad += (rec[i] != 0) ? 1UL : 0UL;
    }
    CHECK_EQ(bad, 0, "and the rest zeroed");
    XhciSnapClientStore(rec, 5, 9, 0, 0, words, 40);
    CHECK_EQ(rec[31], 0x100 + 27, "too many words: held to the record");
    CHECK_EQ(rec[32], 0, "nothing written past it");

    /* Fill all four, stamps 10, 11, 12, 13; a fifth device takes the
     * oldest, and a refreshed one is no longer the oldest. */
    for (i = 0; i < 4; i++) {
        XhciSnapClientStore(&rec[i * 32], 10 + i, i + 1, 0, 0, words, 28);
    }
    CHECK_EQ(XhciSnapClientPick(rec, 4, 7, 0), 0, "full: the oldest");
    XhciSnapClientStore(&rec[0], 14, 1, 0, 0, words, 28);
    CHECK_EQ(XhciSnapClientPick(rec, 4, 7, 0), 1,
             "refreshed: the next oldest");
    CHECK_EQ(XhciSnapClientPick(rec, 4, 3, 0), 2,
             "a kept device still finds its own");
    rec[3 * 32] = 0;
    CHECK_EQ(XhciSnapClientPick(rec, 4, 7, 0), 3,
             "an empty record before the oldest");

    /* Twenty devices in turn: each takes a record, and the last four are
     * the ones kept. */
    for (i = 0; i < sizeof(rec) / sizeof(rec[0]); i++) {
        rec[i] = 0;
    }
    for (stamp = 1; stamp <= 20; stamp++) {
        pick = XhciSnapClientPick(rec, 4, stamp, 0);
        XhciSnapClientStore(&rec[pick * 32], stamp, stamp, 0, 0, words, 28);
    }
    bad = 0;
    for (i = 0; i < 4; i++) {
        bad += (rec[i * 32 + 1] < 17) ? 1UL : 0UL;
    }
    CHECK_EQ(bad, 0, "the last four devices kept");
}

int main(void)
{
    test_header();
    test_clients();
    test_tol_words();
    test_walk(0);
    test_walk(1);
    test_walk(18);
    test_walk(XHCI_MAX_ROOT_PORTS);
    test_record_words();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
