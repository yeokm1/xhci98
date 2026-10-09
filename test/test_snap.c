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
#include "../src/xhci98_xfer.h"
#include "test_harness.h"

#define H XHCI_SNAPSHOT_HCD_HEAD_WORDS

static void test_header(void)
{
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_VERSION_AT), 1,
             "version 1");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_HEAD_BYTES), 780,
             "a 195-word header (35-T.8 appended twelve, 36.3 28, 36.21 "
             "147)");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_PORTS), 18,
             "the E460's eighteen root ports");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_PORT_BYTES), 64,
             "a sixteen-word record");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_PORTS_AT), 780,
             "records right after the header");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_COUNTERS), 63,
             "the whole counter block");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_COUNTERS_AT),
             780 + 18 * 64, "counters after the last record");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_NOTE_BUDGET),
             XHCI_ENUM_NOTE_BUDGET, "the notes' budget");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_HEAD_WORDS), 0,
             "past the header reads 0");
    CHECK_EQ(XHCI_SNAPSHOT_HCD_COUNTER_WORDS * sizeof(ULONG),
             sizeof(XHCIHC_COUNTERS), "the counters word for word");
    CHECK_EQ(XhciSnapHcdWords(18), 195 + 18 * 16 + 63, "the E460's image");
    CHECK_EQ(XhciSnapHcdWords(0), 195 + 63,
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
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_XSTAT_WORDS), 54,
             "fifty-four readings");
    CHECK_EQ(sizeof(XHCI_XSTAT), 54 * 4, "the whole of XHCI_XSTAT");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_TELE_RECORDS), 2,
             "two class driver records");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_TELE_WORDS), 45,
             "four words of the bus's, then the record");
    CHECK_EQ(XHCI_SNAPSHOT_HCD_TELE_AT, 51 + 54, "the records after");
    CHECK_EQ(XHCI_SNAPSHOT_HCD_TELE_AT + 2 * 45, XHCI_SNAPSHOT_HCD_HEAD_WORDS,
             "and they end the header");
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
             "TeleRefused last");
}

/* 36.21's arithmetic, as the driver calls it. */
static void test_xstat_math(void)
{
    XHCI_XSTAT s;
    XHCI_XSTAT_TIME t;
    ULONG discarded;
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
    CHECK_EQ(XhciXstatInterval(&t, &discarded, 0, 500), 0,
             "an unstamped start records nothing");
    CHECK_EQ(XhciXstatInterval(&t, &discarded, 500, 0), 0,
             "nor an unstamped end");
    CHECK_EQ(XhciXstatInterval(&t, &discarded, 101, 401), 1, "300 counts");
    CHECK_EQ(XhciXstatInterval(&t, &discarded, 0xFFFFFF01UL, 0x101), 1,
             "across the low half's wrap: 512 counts");
    CHECK(t.Count == 2 && t.SumLo == 812 && t.SumHi == 0 && t.Max == 512,
          "two intervals, their sum and the longest");
    CHECK_EQ(XhciXstatInterval(&t, &discarded, 1001, 1), 0,
             "a counter that ran backwards is no interval");
    CHECK_EQ(discarded, 1, "and is counted as discarded");
    CHECK_EQ(XhciXstatInterval(&t, &discarded, 1, 0x80000001UL), 0,
             "nor one past the limit");
    CHECK_EQ(XhciXstatInterval(&t, &discarded, 1, 0x80000000UL), 1,
             "the limit itself is one");
    CHECK_EQ(t.Count, 3, "three recorded in all");
    CHECK_EQ(discarded, 2, "two discarded");

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
    test_xstat_math();
    test_walk(0);
    test_walk(1);
    test_walk(18);
    test_walk(XHCI_MAX_ROOT_PORTS);
    test_record_words();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
