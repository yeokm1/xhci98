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
#include "../src/xhci_pirq.h"
#include "../src/xhci_xstat.h"
#include "../src/xhci_poll.h"
#include "../src/xhci98_xfer.h"
#include "../src/xhci_xpipe.h"
#include "test_harness.h"

#define H XHCI_SNAPSHOT_HCD_HEAD_WORDS

static void test_header(void)
{
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_VERSION_AT), 1,
             "version 1");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_HEAD_BYTES), 948,
             "a 237-word header (35-T.8 appended twelve, 36.3 28, 36.21 "
             "154, 36.9 two, 36.24 and 36.25 33)");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_PORTS), 18,
             "the E460's eighteen root ports");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_PORT_BYTES), 64,
             "a sixteen-word record");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_PORTS_AT), 948,
             "records right after the header");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_COUNTERS), 114,
             "the whole counter block");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_COUNTERS_AT),
             948 + 18 * 64, "counters after the last record");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_NOTE_BUDGET),
             XHCI_ENUM_NOTE_BUDGET, "the notes' budget");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_HEAD_WORDS), 0,
             "past the header reads 0");
    CHECK_EQ(XHCI_SNAPSHOT_HCD_COUNTER_WORDS * sizeof(ULONG),
             sizeof(XHCIHC_COUNTERS), "the counters word for word");
    CHECK_EQ(XhciSnapHcdWords(18), 237 + 18 * 16 + 114, "the E460's image");
    CHECK_EQ(XhciSnapHcdWords(0), 237 + 114,
             "no ports: a header and the counters");
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
        } else if (index < total) {
            expectPart = XHCI_SNAPSHOT_HCD_IN_COUNTERS;
            expectRecord = 0;
            expectWord = index - H - ports * 16;
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

/* 36.3: the routing quirk's record, copied whole after its count.
 * XHCISNAP names these words at these places (xhcisnap.c,
 * SNAP_PIRQ_*), so a field moved fails here until the tool follows. */
static void test_pirq_words(void)
{
    CHECK_EQ(XHCI_SNAPSHOT_HCD_PIRQ_WORDS, 20, "after the terminal reason");
    CHECK_EQ(XHCI_SNAPSHOT_HCD_PIRQ_AT, 21, "the record right after");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_PIRQ_WORDS),
             sizeof(XHCI_PIRQ_STATE) / 4, "its count, the whole state");
    CHECK_EQ(XHCI_SNAPSHOT_HCD_PIRQ_AT + sizeof(XHCI_PIRQ_STATE) / 4, 48,
             "the record ends 36.3's header, word 48");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_PIRQ_AT), 0,
             "the record is the door's, not layout");
    CHECK_EQ(XHCI_FIELD_OFFSET(XHCI_PIRQ_STATE, Runs), 0 * 4, "Runs");
    CHECK_EQ(XHCI_FIELD_OFFSET(XHCI_PIRQ_STATE, Mode), 3 * 4, "Mode");
    CHECK_EQ(XHCI_FIELD_OFFSET(XHCI_PIRQ_STATE, Decision), 8 * 4,
             "Decision");
    CHECK_EQ(XHCI_FIELD_OFFSET(XHCI_PIRQ_STATE, XhciId), 9 * 4, "XhciId");
    CHECK_EQ(XHCI_FIELD_OFFSET(XHCI_PIRQ_STATE, LpcId), 13 * 4, "LpcId");
    CHECK_EQ(XHCI_FIELD_OFFSET(XHCI_PIRQ_STATE, IrqCount), 17 * 4,
             "IrqCount");
    CHECK_EQ(XHCI_FIELD_OFFSET(XHCI_PIRQ_STATE, IrqMode), 22 * 4, "IrqMode");
    CHECK_EQ(XHCI_FIELD_OFFSET(XHCI_PIRQ_STATE, D20irAfter), 26 * 4,
             "D20irAfter last");
    CHECK_EQ(XHCI_PIRQ_D_COUNT, 24, "the decisions XHCISNAP names");
}

/* 36.21: the transfer-size readings and the class driver records, after
 * the routing record. XHCISNAP names the words at these places (xhcisnap.c,
 * SNAP_XS_* and SNAP_TL_*), so a field moved fails here until the tool
 * follows. */
static void test_xstat_words(void)
{
    CHECK_EQ(XHCI_SNAPSHOT_HCD_XSTAT_WORDS, 48, "after the routing record");
    CHECK_EQ(XHCI_SNAPSHOT_HCD_TELE_RECORDS, 49, "then the record count");
    CHECK_EQ(XHCI_SNAPSHOT_HCD_TELE_WORDS, 50, "and a record's words");
    CHECK_EQ(XHCI_SNAPSHOT_HCD_XSTAT_AT, 51, "the readings at 51");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_XSTAT_WORDS), 59,
             "fifty-nine readings");
    CHECK_EQ(sizeof(XHCI_XSTAT), 59 * 4, "the whole of XHCI_XSTAT");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_TELE_RECORDS), 2,
             "two class driver records");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_TELE_WORDS), 46,
             "five words of the bus's, then the record");
    CHECK_EQ(XHCI_SNAPSHOT_HCD_TELE_AT, 51 + 59, "the records after");
    CHECK_EQ(XHCI_SNAPSHOT_HCD_TELE_AT + 2 * 46, XHCI_SNAPSHOT_HCD_DELIVERY_AT,
             "and 36.9's pair follows them");
    /* 36.24: 36.9's two words, then stage 2's counters,
     * appended with every offset before them unchanged. */
    CHECK_EQ(XHCI_SNAPSHOT_HCD_TELE_END, 202, "36.21's end unmoved");
    CHECK_EQ(XHCI_SNAPSHOT_HCD_DELIVERY_AT, 202, "36.9's pair at 202");
    CHECK_EQ(XHCI_SNAPSHOT_HCD_DELIVERY_WORDS, 203, "and 203");
    CHECK_EQ(XHCI_SNAPSHOT_HCD_XPIPE_WORDS, 204, "stage 2's count at 204");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_XPIPE_WORDS), 32,
             "thirty-two counters: 36.24's 24, 36.25's eight");
    CHECK_EQ(XHCI_SNAPSHOT_HCD_XPIPE_AT, 205, "from 205");
    CHECK_EQ(sizeof(XHCI_XPIPE_STATS), 32 * 4, "the whole of the stats");
    CHECK_EQ(XHCI_SNAPSHOT_HCD_XPIPE_AT + 32, XHCI_SNAPSHOT_HCD_HEAD_WORDS,
             "to the header's end");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_XPIPE_AT), 0,
             "the counters are the door's");
    CHECK_EQ(XHCI_FIELD_OFFSET(XHCI_XPIPE_STATS, Check), 2 * 4,
             "the check's verdict third");
    CHECK_EQ(XHCI_FIELD_OFFSET(XHCI_XPIPE_STATS, Pipelined), 12 * 4,
             "pipelined URBs thirteenth");
    CHECK_EQ(XHCI_FIELD_OFFSET(XHCI_XPIPE_STATS, Order), 23 * 4,
             "the order anomaly last of 36.24's");
    CHECK_EQ(XHCI_FIELD_OFFSET(XHCI_XPIPE_STATS, BigBuilt), 24 * 4,
             "36.25's first at word 229");
    CHECK_EQ(XHCI_FIELD_OFFSET(XHCI_XPIPE_STATS, Td513), 31 * 4,
             "and its last at 236");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_XSTAT_AT), 0,
             "the readings are the door's, not layout");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_TELE_AT), 0,
             "and so are the records");
    CHECK_EQ(sizeof(XHCI98_TELEMETRY), 41 * 4, "a record of 41 words");
    CHECK_EQ(XHCI98_TELE_WORDS, XHCI_SNAPSHOT_HCD_TELE_BODY_WORDS,
             "the snapshot carries it whole");
    CHECK_EQ(XHCI98_TELE_BUCKETS, XHCI_XSTAT_BUCKETS,
             "the class driver's buckets are the bus's");
    CHECK_EQ(XHCI98_TELE_DEPTHS, XHCI_XSTAT_DEPTH_BUCKETS,
             "and its depths");
    CHECK_EQ(XHCI98_TELE_DEPTH_HIST, XHCI98_TELE_HIST + 9,
             "depths after lengths");
    CHECK_EQ(XHCI98_TELE_COUNTERS + 8, XHCI98_TELE_UNSUPPORTED,
             "the policy counts after the counters");
    CHECK_EQ(XHCI98_TELE_TRANSPORT + 1, XHCI98_TELE_WORDS,
             "and last");
    CHECK_EQ(XHCI_FIELD_OFFSET(XHCI_XSTAT, MapGrant), 4 * 4, "MapGrant");
    CHECK_EQ(XHCI_FIELD_OFFSET(XHCI_XSTAT, Hist), 10 * 4, "Hist");
    CHECK_EQ(XHCI_FIELD_OFFSET(XHCI_XSTAT, Chunks), 19 * 4, "Chunks");
    CHECK_EQ(XHCI_FIELD_OFFSET(XHCI_XSTAT, ChunkHist), 22 * 4, "ChunkHist");
    CHECK_EQ(XHCI_FIELD_OFFSET(XHCI_XSTAT, Gap), 27 * 4, "Gap");
    CHECK_EQ(XHCI_FIELD_OFFSET(XHCI_XSTAT, Empty), 31 * 4, "Empty");
    CHECK_EQ(XHCI_FIELD_OFFSET(XHCI_XSTAT, Publish), 35 * 4, "Publish");
    CHECK_EQ(XHCI_FIELD_OFFSET(XHCI_XSTAT, Complete), 39 * 4, "Complete");
    CHECK_EQ(XHCI_FIELD_OFFSET(XHCI_XSTAT, Discarded), 43 * 4, "Discarded");
    CHECK_EQ(XHCI_FIELD_OFFSET(XHCI_XSTAT, TscStartLo), 44 * 4,
             "TscStartLo");
    CHECK_EQ(XHCI_FIELD_OFFSET(XHCI_XSTAT, TimeStartLo), 46 * 4,
             "TimeStartLo");
    CHECK_EQ(XHCI_FIELD_OFFSET(XHCI_XSTAT, TscNowLo), 48 * 4, "TscNowLo");
    CHECK_EQ(XHCI_FIELD_OFFSET(XHCI_XSTAT, TimeNowLo), 50 * 4, "TimeNowLo");
    CHECK_EQ(XHCI_FIELD_OFFSET(XHCI_XSTAT, TeleRefused), 53 * 4,
             "TeleRefused");
    CHECK_EQ(XHCI_FIELD_OFFSET(XHCI_XSTAT, Map), 54 * 4, "Map");
    CHECK_EQ(XHCI_FIELD_OFFSET(XHCI_XSTAT, Crossed), 58 * 4, "Crossed last");
    CHECK_EQ(XHCI_SNAPSHOT_HCD_TELE_STATE, 4, "a record's state at 4");
    CHECK_EQ(XHCI_SNAPSHOT_HCD_TELE_BODY, 5, "its body at 5");
}

/* 36.9: the event delivery. XHCISNAP names the counter words from 63 by
 * these places and decodes them by these codes (xhcisnap.c,
 * hcd_counter_names and SNAP_POLL_*), and finds the six extension words by
 * the header's pair, so a field moved or a code renumbered fails here until
 * the tool follows. */
static void test_delivery_words(void)
{
    CHECK_EQ(XHCI_SNAPSHOT_HCD_DELIVERY_AT, 202, "after the records");
    CHECK_EQ(XHCI_SNAPSHOT_HCD_DELIVERY_WORDS, 203, "then the count");
    CHECK_EQ(XHCI_SNAPSHOT_HCD_XPIPE_WORDS, 204,
             "and 36.24's counters follow");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_DELIVERY_AT),
             XHCI_FIELD_OFFSET(XHCI_EXTENSION, EventDelivery),
             "EventDelivery's place in the extension");
    CHECK_EQ(XhciSnapHcdHead(0, XHCI_SNAPSHOT_HCD_DELIVERY_AT),
             XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_DELIVERY_AT),
             "whatever the ports");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_DELIVERY_WORDS), 6,
             "six words");
    CHECK_EQ(XHCI_FIELD_OFFSET(XHCI_EXTENSION, PollService),
             XHCI_FIELD_OFFSET(XHCI_EXTENSION, EventDelivery) + 4,
             "PollService second");
    CHECK_EQ(XHCI_FIELD_OFFSET(XHCI_EXTENSION, PollServiceGen),
             XHCI_FIELD_OFFSET(XHCI_EXTENSION, EventDelivery) + 8,
             "PollServiceGen third");
    CHECK_EQ(XHCI_FIELD_OFFSET(XHCI_EXTENSION, LineMoved),
             XHCI_FIELD_OFFSET(XHCI_EXTENSION, EventDelivery) + 12,
             "LineMoved fourth");
    CHECK_EQ(XHCI_FIELD_OFFSET(XHCI_EXTENSION, LineRetired),
             XHCI_FIELD_OFFSET(XHCI_EXTENSION, EventDelivery) + 16,
             "LineRetired fifth");
    CHECK_EQ(XHCI_FIELD_OFFSET(XHCI_EXTENSION, LineRetireState),
             XHCI_FIELD_OFFSET(XHCI_EXTENSION, EventDelivery) + 20,
             "LineRetireState last");
    CHECK(XHCI_FIELD_OFFSET(XHCI_EXTENSION, LineRetireState) + 4 <=
              sizeof(XHCI_EXTENSION),
          "inside the extension image");

    CHECK_EQ(XHCI_FIELD_OFFSET(XHCIHC_COUNTERS, IntrModeRead), 63 * 4,
             "IntrModeRead at 63");
    CHECK_EQ(XHCI_FIELD_OFFSET(XHCIHC_COUNTERS, IntrDelivery), 65 * 4,
             "IntrDelivery");
    CHECK_EQ(XHCI_FIELD_OFFSET(XHCIHC_COUNTERS, IntrPin), 68 * 4, "IntrPin");
    CHECK_EQ(XHCI_FIELD_OFFSET(XHCIHC_COUNTERS, PollActiveMs), 72 * 4,
             "PollActiveMs");
    CHECK_EQ(XHCI_FIELD_OFFSET(XHCIHC_COUNTERS, PollStateLast), 75 * 4,
             "PollStateLast");
    CHECK_EQ(XHCI_FIELD_OFFSET(XHCIHC_COUNTERS, PollIntervalMinMs), 84 * 4,
             "PollIntervalMinMs");
    CHECK_EQ(XHCI_FIELD_OFFSET(XHCIHC_COUNTERS, WatchLooks), 87 * 4,
             "WatchLooks");
    CHECK_EQ(XHCI_FIELD_OFFSET(XHCIHC_COUNTERS, LineMoves), 90 * 4,
             "LineMoves");
    CHECK_EQ(XHCI_FIELD_OFFSET(XHCIHC_COUNTERS, LineRetireState), 92 * 4,
             "LineRetireState");
    CHECK_EQ(XHCI_FIELD_OFFSET(XHCIHC_COUNTERS, IntxDisableCleared), 93 * 4,
             "IntxDisableCleared");
    CHECK_EQ(XHCI_FIELD_OFFSET(XHCIHC_COUNTERS, LineMoveMasked), 94 * 4,
             "LineMoveMasked");
    CHECK_EQ(XHCI_FIELD_OFFSET(XHCIHC_COUNTERS, IntrValuesFound), 95 * 4,
             "IntrValuesFound last");

    CHECK_EQ(XHCI_POLL_NOT_FOUND, 0xFFFFFFFFUL, "not found");
    CHECK_EQ(XHCI_POLL_IDLE_DEFAULT_MS, 50, "idle default");
    CHECK_EQ(XHCI_POLL_IDLE_MAX_MS, 5000, "idle bound");
    CHECK_EQ(XHCI_POLL_ACTIVE_DEFAULT_MS, 1, "active default");
    CHECK_EQ(XHCI_POLL_FOUND_MODE, 1, "found bits");
    CHECK_EQ(XHCI_POLL_FOUND_IDLE, 2, "found bits");
    CHECK_EQ(XHCI_POLL_FOUND_ACTIVE, 4, "found bits");
    CHECK_EQ(XHCI_POLL_DELIVERY_POLL, 2, "delivery codes");
    CHECK_EQ(XHCI_POLL_DELIVERY_MESSAGE, 5, "delivery codes, last");
    CHECK_EQ(XHCI_POLL_R_AUTO_PIN0, 3, "reason codes");
    CHECK_EQ(XHCI_POLL_R_FORCED_POLL, 8, "reason codes");
    CHECK_EQ(XHCI_POLL_R_AUTO_LINE_DEAD, 9, "reason codes");
    CHECK_EQ(XHCI_POLL_R_MESSAGE_NOT_CONNECTED, 12, "reason codes");
    CHECK_EQ(XHCI_POLL_R_FORCED_NO_LINE_OFFERED, 13, "reason codes, last");
    CHECK_EQ(XHCI_POLL_PIN_UNREADABLE, 2, "pin status, last");
    CHECK_EQ(XHCI_POLL_ROLE_SERVE, 2, "roles, last");
    CHECK_EQ(XHCI_POLL_BUS_WATCH, 4, "bus states, last");
    CHECK_EQ(XHCI_POLL_RETIRED_OWED, 5, "retirement states, last");
}

/* 36.22: XhciMaxTransferKB as each start takes it. */
static void test_xpol(void)
{
    ULONG why;

    CHECK_EQ(XhciXpolValue(XHCI_XPOL_FOUND_NONE, 0, &why), 1048576,
             "absent: 1 MiB");
    CHECK_EQ(why, XHCI_XPOL_WHY_ABSENT, "said as absent");
    CHECK_EQ(XhciXpolValue(XHCI_XPOL_FOUND_OTHER, 64, &why), 1048576,
             "another type: 1 MiB whatever it holds");
    CHECK_EQ(why, XHCI_XPOL_WHY_TYPE, "said as the type");
    CHECK_EQ(XhciXpolValue(XHCI_XPOL_FOUND_DWORD, 64, &why), 65536,
             "64: 64 KiB, today's size");
    CHECK_EQ(why, XHCI_XPOL_WHY_USED, "used");
    CHECK_EQ(XhciXpolValue(XHCI_XPOL_FOUND_DWORD, 1024, &why), 1048576,
             "1024: 1 MiB");
    CHECK_EQ(XhciXpolValue(XHCI_XPOL_FOUND_DWORD, 2048, &why), 2097152,
             "2048: 2 MiB");
    CHECK_EQ(why, XHCI_XPOL_WHY_USED, "the top used");
    CHECK_EQ(XhciXpolValue(XHCI_XPOL_FOUND_DWORD, 68, &why), 69632,
             "68: a step above the bottom");
    CHECK_EQ(XhciXpolValue(XHCI_XPOL_FOUND_DWORD, 60, &why), 1048576,
             "60: below the range");
    CHECK_EQ(why, XHCI_XPOL_WHY_RANGE, "said as range");
    CHECK_EQ(XhciXpolValue(XHCI_XPOL_FOUND_DWORD, 0, &why), 1048576,
             "0: below the range, not off");
    CHECK_EQ(why, XHCI_XPOL_WHY_RANGE, "range");
    CHECK_EQ(XhciXpolValue(XHCI_XPOL_FOUND_DWORD, 2052, &why), 1048576,
             "2052: above it");
    CHECK_EQ(XhciXpolValue(XHCI_XPOL_FOUND_DWORD, 0xFFFFFFFFUL, &why),
             1048576, "all ones: above it, no overflow");
    CHECK_EQ(why, XHCI_XPOL_WHY_RANGE, "range");
    CHECK_EQ(XhciXpolValue(XHCI_XPOL_FOUND_DWORD, 1025, &why), 1048576,
             "1025: off a step");
    CHECK_EQ(why, XHCI_XPOL_WHY_STEP, "said as the step");
    CHECK_EQ(XhciXpolValue(XHCI_XPOL_FOUND_DWORD, 66, &why), 1048576,
             "66: off a step");
    CHECK_EQ(XHCI_XPOL_MIN_KB * 1024, XHCI98_POLICY_MIN_BYTES,
             "the bus's range is the interface's");
    CHECK_EQ(XHCI_XPOL_MAX_KB * 1024, XHCI98_POLICY_MAX_BYTES, "its top");
    CHECK_EQ(XHCI_XPOL_STEP_KB * 1024, XHCI98_POLICY_STEP_BYTES, "its step");
    CHECK_EQ(XHCI_XPOL_DEFAULT_KB * 1024, XHCI98_POLICY_DEFAULT_BYTES,
             "its default");
}

/* XhciXstatInterval over two stamps given as (Lo, Hi, Cpu). */
static ULONG iv(PXHCI_XSTAT_TIME t, PULONG discarded, PULONG crossed,
                ULONG fromLo, ULONG fromHi, ULONG fromCpu, ULONG toLo,
                ULONG toHi, ULONG toCpu)
{
    XHCI_XSTAT_STAMP from;
    XHCI_XSTAT_STAMP to;

    from.Lo = fromLo;
    from.Hi = fromHi;
    from.Cpu = fromCpu;
    to.Lo = toLo;
    to.Hi = toHi;
    to.Cpu = toCpu;
    return XhciXstatInterval(t, discarded, crossed, &from, &to);
}

/* 36.21's arithmetic, as the driver calls it. */
static void test_xstat_math(void)
{
    XHCI_XSTAT s;
    XHCI_XSTAT_TIME t;
    XHCI_XSTAT_STAMP st;
    ULONG discarded;
    ULONG crossed;
    ULONG lo;
    ULONG hi;
    ULONG i;

    CHECK_EQ(XhciXstatBucket(0), 0, "an empty length in the first");
    CHECK_EQ(XhciXstatBucket(4096), 0, "4 KiB in the first");
    CHECK_EQ(XhciXstatBucket(4097), 1, "past it, the second");
    CHECK_EQ(XhciXstatBucket(16384), 1, "16 KiB");
    CHECK_EQ(XhciXstatBucket(65536), 2, "64 KiB");
    CHECK_EQ(XhciXstatBucket(65537), 3, "past 64 KiB");
    CHECK_EQ(XhciXstatBucket(131072), 3, "128 KiB");
    CHECK_EQ(XhciXstatBucket(262144), 4, "256 KiB");
    CHECK_EQ(XhciXstatBucket(524288), 5, "512 KiB");
    CHECK_EQ(XhciXstatBucket(1048576), 6, "1 MiB");
    CHECK_EQ(XhciXstatBucket(2097152), 7, "2 MiB");
    CHECK_EQ(XhciXstatBucket(2097153), 8, "above 2 MiB");
    CHECK_EQ(XhciXstatBucket(0xFFFFFFFFUL), 8, "and the most");
    CHECK_EQ(XhciXstatChunkBucket(0), 0, "no chunk counts as one");
    CHECK_EQ(XhciXstatChunkBucket(1), 0, "one");
    CHECK_EQ(XhciXstatChunkBucket(2), 1, "two");
    CHECK_EQ(XhciXstatChunkBucket(8), 2, "eight: 1 MiB at 128 KiB");
    CHECK_EQ(XhciXstatChunkBucket(9), 3, "nine: unaligned 1 MiB");
    CHECK_EQ(XhciXstatChunkBucket(16), 3, "sixteen: 2 MiB");
    CHECK_EQ(XhciXstatChunkBucket(17), 4, "seventeen and more");
    CHECK_EQ(XhciXstatDepthBucket(0), 0, "none in flight reads as one");
    CHECK_EQ(XhciXstatDepthBucket(1), 0, "one");
    CHECK_EQ(XhciXstatDepthBucket(2), 1, "two");
    CHECK_EQ(XhciXstatDepthBucket(4), 2, "three to four");
    CHECK_EQ(XhciXstatDepthBucket(5), 3, "five to eight");
    CHECK_EQ(XhciXstatDepthBucket(16), 4, "nine to sixteen");
    CHECK_EQ(XhciXstatDepthBucket(30), 5, "thirty: the most UAS runs");

    lo = 0xFFFFFFF0UL;
    hi = 2;
    XhciXstatAdd64(&lo, &hi, 0x20);
    CHECK(lo == 0x10 && hi == 3, "a sum carries into the high word");
    XhciXstatAdd64(&lo, &hi, 0);
    CHECK(lo == 0x10 && hi == 3, "nothing added, nothing carried");

    for (i = 0; i < sizeof(t); i++) {
        ((unsigned char *)&t)[i] = 0;
    }
    discarded = 0;
    crossed = 0;
    CHECK_EQ(iv(&t, &discarded, &crossed, 0, 0, 0, 500, 0, 0), 0,
             "an unstamped start records nothing");
    CHECK_EQ(iv(&t, &discarded, &crossed, 500, 0, 0, 0, 0, 0), 0,
             "nor an unstamped end");
    CHECK_EQ(iv(&t, &discarded, &crossed, 0, 1, 0, 300, 1, 0), 1,
             "a start with only its high half set is stamped");
    CHECK_EQ(iv(&t, &discarded, &crossed, 101, 7, 2, 401, 7, 2), 1,
             "300 counts on processor 2");
    CHECK_EQ(iv(&t, &discarded, &crossed, 0xFFFFFF01UL, 7, 0, 0x101, 8, 0),
             1, "across the low half's carry: 512 counts");
    CHECK(t.Count == 3 && t.SumLo == 1112 && t.SumHi == 0 && t.Max == 512,
          "three intervals, their sum and the longest");
    CHECK_EQ(iv(&t, &discarded, &crossed, 101, 7, 0, 401, 7, 1), 0,
             "two processors' stamps are no interval");
    CHECK_EQ(crossed, 1, "and are counted as crossed");
    CHECK_EQ(discarded, 0, "not as discarded");
    CHECK_EQ(iv(&t, &discarded, &crossed, 1001, 7, 0, 1, 7, 0), 0,
             "a counter that ran backwards is no interval");
    CHECK_EQ(discarded, 1, "and is counted as discarded");
    CHECK_EQ(iv(&t, &discarded, &crossed, 1000, 7, 0, 2000, 8, 0), 0,
             "2^32 + 1000 counts is not 1000 (review round 1, finding 2)");
    CHECK_EQ(iv(&t, &discarded, &crossed, 1000, 7, 0, 999, 8, 0), 0,
             "nor is 2^32 - 1");
    CHECK_EQ(iv(&t, &discarded, &crossed, 1, 7, 0, 0x80000002UL, 7, 0), 0,
             "nor one past the limit");
    CHECK_EQ(iv(&t, &discarded, &crossed, 1, 7, 0, 0x80000000UL, 7, 0), 1,
             "the limit itself is one");
    CHECK_EQ(t.Count, 4, "four recorded in all");
    CHECK_EQ(discarded, 4, "four discarded");
    st.Lo = 5;
    st.Hi = 6;
    st.Cpu = 7;
    CHECK_EQ(XhciXstatStamped(&st), 1, "a stamp");
    XhciXstatClear(&st);
    CHECK(st.Lo == 0 && st.Hi == 0 && st.Cpu == 0, "cleared whole");
    CHECK_EQ(XhciXstatStamped(&st), 0, "and reads unstamped");

    for (i = 0; i < sizeof(s); i++) {
        ((unsigned char *)&s)[i] = 0;
    }
    XhciXstatUrb(&s, 1048576, 8);
    XhciXstatUrb(&s, 65536, 1);
    XhciXstatUrb(&s, 0xFFFFFFF0UL, 17);
    CHECK_EQ(s.Urbs, 3, "three URBs");
    CHECK(s.BytesLo == 0x0010FFF0UL && s.BytesHi == 1,
          "their lengths' sum, carried");
    CHECK_EQ(s.LengthMax, 0xFFFFFFF0UL, "the longest");
    CHECK(s.Hist[6] == 1 && s.Hist[2] == 1 && s.Hist[8] == 1,
          "each in its bucket");
    CHECK_EQ(s.MultiChunk, 2, "two of more than one chunk");
    CHECK_EQ(s.ChunksMax, 17, "the most chunks");
    CHECK(s.ChunkHist[0] == 1 && s.ChunkHist[2] == 1 && s.ChunkHist[4] == 1,
          "by chunk count");
}

int main(void)
{
    test_header();
    test_tol_words();
    test_pirq_words();
    test_xstat_words();
    test_delivery_words();
    test_xstat_math();
    test_xpol();
    test_walk(0);
    test_walk(1);
    test_walk(18);
    test_walk(XHCI_MAX_ROOT_PORTS);
    test_record_words();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
