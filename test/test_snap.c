/*
 * test_snap.c - host vectors for the snapshot's HCD region layout
 * (src\xhci_snap.c; src\xhci.h, XHCI_SNAPSHOT_REGION_HCD; roadmap-hcd.md
 * task 35.3).
 *
 * The door fills the image word by word from where XhciSnapHcdLocate says
 * each word lies, and XHCISNAP walks it by the sizes the header gives, so
 * the two must agree: every word of the image is located exactly once, in
 * order, the header's offsets name the words Locate puts there, and the
 * counter block is the whole of XHCIHC_COUNTERS. The diagnostic build
 * 2.2.0.2 (roadmap-hcd.md task 36.1) appends four parts and the helpers
 * that fill two of them as rings; both are held here too.
 */

#include <stdio.h>
#include "../src/xhci.h"
#include "../src/xhci_counters.h"
#include "../src/xhci_enum.h"
#include "test_harness.h"

#define H XHCI_SNAPSHOT_HCD_HEAD_WORDS
/* The diagnostic build's parts, in words: the block, 24 pipe records of
 * 39, 32 timeline entries of 15, 16 samples of 16. */
#define D (41 + 24 * 39 + 32 * 15 + 16 * 16)

static void test_header(void)
{
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_VERSION_AT), 1,
             "version 1");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_HEAD_BYTES), 124,
             "a thirty-one-word header (35-T.8 appended twelve, 2.2.0.2 "
             "eleven)");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_PORTS), 18,
             "the E460's eighteen root ports");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_PORT_BYTES), 64,
             "a sixteen-word record");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_PORTS_AT), 124,
             "records right after the header");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_COUNTERS), 63,
             "the whole counter block");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_COUNTERS_AT),
             124 + 18 * 64, "counters after the last record");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_DIAG_AT),
             124 + 18 * 64 + 63 * 4, "the diagnostic block after them");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_DIAG_BYTES), 41 * 4,
             "forty-one words");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_PIPES), 24,
             "twenty-four pipe records");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_PIPE_BYTES), 39 * 4,
             "of thirty-nine words");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_PIPES_AT),
             124 + 18 * 64 + 63 * 4 + 41 * 4, "after the block");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_RECS), 32,
             "thirty-two timeline entries");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_REC_BYTES), 15 * 4,
             "of fifteen words");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_RECS_AT),
             124 + 18 * 64 + 63 * 4 + 41 * 4 + 24 * 39 * 4,
             "after the pipes");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_IRQS), 16,
             "sixteen backstop samples");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_IRQ_BYTES), 16 * 4,
             "of sixteen words");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_IRQS_AT),
             124 + 18 * 64 + 63 * 4 + 41 * 4 + 24 * 39 * 4 + 32 * 15 * 4,
             "after the timeline");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_NOTE_BUDGET),
             XHCI_ENUM_NOTE_BUDGET, "the notes' budget");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_HEAD_WORDS), 0,
             "past the header reads 0");
    CHECK_EQ(XHCI_SNAPSHOT_HCD_COUNTER_WORDS * sizeof(ULONG),
             sizeof(XHCIHC_COUNTERS), "the counters word for word");
    CHECK_EQ(XhciSnapHcdWords(18), 31 + 18 * 16 + 63 + D, "the E460's image");
    CHECK_EQ(XhciSnapHcdWords(0), 31 + 63 + D,
             "no ports: a header, the counters and the diagnostics");
    CHECK_EQ(XhciSnapHcdWords(1000), XhciSnapHcdWords(XHCI_MAX_ROOT_PORTS),
             "ports held to the root port maximum");
    CHECK_EQ(XhciSnapHcdHead(1000, XHCI_SNAPSHOT_HCD_PORTS),
             XHCI_MAX_ROOT_PORTS, "and so is the header's count");
    CHECK(XhciSnapHcdWords(XHCI_MAX_ROOT_PORTS) * 4UL <= 0xF000UL - 256UL,
          "the largest image fits one of XHCISNAP's windows");
    /* 2.2.0.0's XHCISNAP refuses a region past 32 KB (SNAP_HCD_MAX_BYTES):
     * inside it, that tool still decodes the ports and counters. */
    CHECK(XhciSnapHcdWords(XHCI_MAX_ROOT_PORTS) * 4UL <= 32768UL,
          "the largest image an older XHCISNAP still reads");
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
        } else if (index < H + ports * 16 + 63) {
            expectPart = XHCI_SNAPSHOT_HCD_IN_COUNTERS;
            expectRecord = 0;
            expectWord = index - H - ports * 16;
        } else if (index < H + ports * 16 + 63 + 41) {
            expectPart = XHCI_SNAPSHOT_HCD_IN_DIAG;
            expectRecord = 0;
            expectWord = index - H - ports * 16 - 63;
        } else if (index < H + ports * 16 + 63 + 41 + 24 * 39) {
            expectPart = XHCI_SNAPSHOT_HCD_IN_PIPE;
            expectRecord = (index - H - ports * 16 - 63 - 41) / 39;
            expectWord = (index - H - ports * 16 - 63 - 41) % 39;
        } else if (index < H + ports * 16 + 63 + 41 + 24 * 39 + 32 * 15) {
            expectPart = XHCI_SNAPSHOT_HCD_IN_REC;
            expectRecord = (index - H - ports * 16 - 63 - 41 - 24 * 39) / 15;
            expectWord = (index - H - ports * 16 - 63 - 41 - 24 * 39) % 15;
        } else if (index < total) {
            expectPart = XHCI_SNAPSHOT_HCD_IN_IRQ;
            expectRecord =
                (index - H - ports * 16 - 63 - 41 - 24 * 39 - 32 * 15) / 16;
            expectWord =
                (index - H - ports * 16 - 63 - 41 - 24 * 39 - 32 * 15) % 16;
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
                                               XHCI_SNAPSHOT_HCD_DIAG_AT) / 4,
                               &record, &word),
             XHCI_SNAPSHOT_HCD_IN_DIAG,
             "the header's block offset is the block");
    CHECK_EQ(record + word, 0, "its first word");
    CHECK_EQ(XhciSnapHcdLocate(ports,
                               XhciSnapHcdHead(ports,
                                               XHCI_SNAPSHOT_HCD_PIPES_AT) / 4,
                               &record, &word),
             XHCI_SNAPSHOT_HCD_IN_PIPE,
             "the header's pipe offset is the first pipe record");
    CHECK_EQ(record + word, 0, "its first word");
    CHECK_EQ(XhciSnapHcdLocate(ports,
                               XhciSnapHcdHead(ports,
                                               XHCI_SNAPSHOT_HCD_RECS_AT) / 4,
                               &record, &word),
             XHCI_SNAPSHOT_HCD_IN_REC,
             "the header's timeline offset is the first entry");
    CHECK_EQ(record + word, 0, "its first word");
    CHECK_EQ(XhciSnapHcdLocate(ports,
                               XhciSnapHcdHead(ports,
                                               XHCI_SNAPSHOT_HCD_IRQS_AT) / 4,
                               &record, &word),
             XHCI_SNAPSHOT_HCD_IN_IRQ,
             "the header's sample offset is the first sample");
    CHECK_EQ(record + word, 0, "its first word");
    CHECK_EQ(XhciSnapHcdLocate(ports, total - 1, &record, &word),
             XHCI_SNAPSHOT_HCD_IN_IRQ, "the image ends with a sample");
    CHECK_EQ(record, XHCI_SNAPSHOT_HCD_IRQS_KEPT - 1, "the last one");
    CHECK_EQ(word, XHCI_SNAPSHOT_IRQ_WORDS - 1, "its last word");
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
    CHECK_EQ(XHCI_SNAPSHOT_HCD_DIAG_AT, 20, "2.2.0.2's words right after");
    CHECK_EQ(XHCI_SNAPSHOT_HCD_IRQS_AT, XHCI_SNAPSHOT_HCD_HEAD_WORDS - 1,
             "the sample offset last");
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

/*
 * 2.2.0.2's word positions XHCISNAP decodes by number (xhcisnap.c, the
 * SNAP_DG_*, SNAP_PR_*, SNAP_RE_* and SNAP_IQ_* constants): a word moved
 * here fails until the tool moves with it.
 */
static void test_diag_words(void)
{
    CHECK_EQ(XHCI_SNAPSHOT_DIAG_WORDS, 41, "the block's words");
    CHECK_EQ(XHCI_SNAPSHOT_DIAG_SLOW_DEPTH, 3, "slow depth at 3");
    CHECK_EQ(XHCI_SNAPSHOT_DIAG_ISR_TICK, 18, "the ISR's tick at 18");
    CHECK_EQ(XHCI_SNAPSHOT_DIAG_BS_IP, 25, "the backstop classes from 25");
    CHECK_EQ(XHCI_SNAPSHOT_DIAG_USBCMD, 30, "the live registers from 30");
    CHECK_EQ(XHCI_SNAPSHOT_DIAG_PIPES_OPEN, 38, "open pipes at 38");
    CHECK_EQ(XHCI_SNAPSHOT_DIAG_PIPES_OMITTED, 39, "the omitted at 39");
    CHECK_EQ(XHCI_SNAPSHOT_DIAG_BS_REFUSED, 40, "refused drains last");
    CHECK_EQ(XHCI_SNAPSHOT_PIPE_WORDS, 39, "a pipe record's words");
    CHECK_EQ(XHCI_SNAPSHOT_PIPE_RING_TRBS, 38, "the ring's size last");
    CHECK_EQ(XHCI_SNAPSHOT_X_EP0_THREAD, 5, "the thread's EP0 transfer");
    CHECK_EQ(XHCI_SNAPSHOT_X_HUB_STATUS, 6, "a hub's status transfer");
    CHECK_EQ(XHCI_SNAPSHOT_RD_HELD, 1, "a held request");
    CHECK_EQ(XHCI_SNAPSHOT_RD_PUT_BACK, 2, "one put back");
    CHECK_EQ(XHCI_SNAPSHOT_RD_REFUSED, 3, "one whose hold was declined");
    CHECK_EQ(XHCI_SNAPSHOT_RD_STREAMS, 0x100, "a streams status");
    CHECK_EQ(XHCI_SNAPSHOT_PIPE_X_STATE, 15, "its oldest record from 15");
    CHECK_EQ(XHCI_SNAPSHOT_PIPE_X_USBD, 32, "to 32");
    CHECK_EQ(XHCI_SNAPSHOT_PIPE_EP_DW0, 33, "the Endpoint Context from 33");
    CHECK_EQ(XHCI_SNAPSHOT_PIPE_SLOT_DW3, 37, "the Slot Context's DW3 last");
    CHECK_EQ(XHCI_SNAPSHOT_REC_WORDS, 15, "a timeline entry's words");
    CHECK_EQ(XHCI_SNAPSHOT_REC_CMD01, 11, "its commands at 11 and 12");
    CHECK_EQ(XHCI_SNAPSHOT_REC_TICK, 14, "its tick last");
    CHECK_EQ(XHCI_SNAPSHOT_IRQ_WORDS, 16, "a sample's words");
    CHECK_EQ(XHCI_SNAPSHOT_IRQ_EVT_DW3, 9, "its event TRB at 9");
    CHECK_EQ(XHCI_SNAPSHOT_IRQ_DPCS, 15, "the DPC count last");
}

/* The system time in milliseconds, by half-words: against quotients worked
 * out with 64-bit arithmetic on the host. */
static void test_ms(void)
{
    CHECK_EQ(XhciSnapMs(0, 0), 0, "zero");
    CHECK_EQ(XhciSnapMs(0, 9999), 0, "under a millisecond");
    CHECK_EQ(XhciSnapMs(0, 10000), 1, "one millisecond");
    CHECK_EQ(XhciSnapMs(0, 0xFFFFFFFFUL), 429496, "the low word's top");
    CHECK_EQ(XhciSnapMs(1, 0), 429496, "the high word's first unit");
    CHECK_EQ(XhciSnapMs(0x01DB1234UL, 0x89ABCDEFUL), 0x6D11DC41UL,
             "a date in this century");
    CHECK_EQ(XhciSnapMs(0x01DD0000UL, 0x00002710UL), 0x113404EBUL,
             "another, its own low word one millisecond");
    CHECK_EQ(XhciSnapMs(0x7FFFFFFFUL, 0xFFFFFFFFUL), 0x5D638865UL,
             "the largest time: the low 32 bits kept");
    CHECK_EQ(XhciSnapMs(0x01DB1234UL, 0x89ABCDEFUL + 10000UL) -
                 XhciSnapMs(0x01DB1234UL, 0x89ABCDEFUL),
             1, "ten thousand units apart: one millisecond");
}

/* The oldest record in use, by a sequence that wraps. */
static void test_oldest(void)
{
    ULONG states[4];
    ULONG seqs[4];

    states[0] = 0; states[1] = 0; states[2] = 0; states[3] = 0;
    seqs[0] = 5; seqs[1] = 6; seqs[2] = 7; seqs[3] = 8;
    CHECK_EQ(XhciSnapOldest(states, seqs, 4, 0), 4, "all free: none");
    states[2] = 2;
    CHECK_EQ(XhciSnapOldest(states, seqs, 4, 0), 2, "the one in use");
    states[3] = 1;
    states[1] = 4;
    CHECK_EQ(XhciSnapOldest(states, seqs, 4, 0), 1,
             "the lowest sequence of three");
    seqs[0] = 0xFFFFFFFEUL;
    seqs[1] = 0xFFFFFFFFUL;
    seqs[2] = 0;
    seqs[3] = 1;
    states[0] = 0;
    states[1] = 0;
    CHECK_EQ(XhciSnapOldest(states, seqs, 4, 0), 2,
             "past the wrap, 0 before 1");
    states[1] = 2;
    CHECK_EQ(XhciSnapOldest(states, seqs, 4, 0), 1,
             "and FFFFFFFFh before 0");
    CHECK_EQ(XhciSnapOldest(states, seqs, 0, 0), 0, "no records: 0");
    CHECK_EQ(XhciSnapOldest(states, seqs, 4, 2), 0,
             "another free state: of the rest, FFFFFFFEh before 1");
}

/* The rings: numbered from 1, the oldest overwritten, an overwritten entry
 * no longer found; and a timeline entry's commands. */
static void test_rings(void)
{
    ULONG ring[4 * 5];
    ULONG rec[XHCI_SNAPSHOT_REC_WORDS];
    ULONG total;
    ULONG i;
    ULONG bad;
    PULONG e;

    for (i = 0; i < 20; i++) {
        ring[i] = 0xEEEEEEEEUL;
    }
    total = 0;
    e = XhciSnapRingNext(ring, 4, 5, &total);
    CHECK(e == &ring[0], "the first entry first");
    CHECK_EQ(e[0], 1, "numbered 1");
    CHECK_EQ(total, 1, "the total advanced");
    bad = 0;
    for (i = 1; i < 5; i++) {
        bad += (e[i] != 0) ? 1UL : 0UL;
    }
    CHECK_EQ(bad, 0, "the rest zeroed");
    CHECK_EQ(ring[5], 0xEEEEEEEEUL, "the next entry untouched");
    e[3] = 0x33;
    for (i = 0; i < 4; i++) {
        (VOID)XhciSnapRingNext(ring, 4, 5, &total);
    }
    CHECK_EQ(total, 5, "five written");
    CHECK_EQ(ring[0], 5, "the fifth in the first entry's place");
    CHECK_EQ(ring[3], 0, "and the first's words gone");
    CHECK(XhciSnapRingFind(ring, 4, 5, 1) == NULL, "the first overwritten");
    CHECK(XhciSnapRingFind(ring, 4, 5, 2) == &ring[5], "the second found");
    CHECK(XhciSnapRingFind(ring, 4, 5, 5) == &ring[0], "the fifth found");
    CHECK(XhciSnapRingFind(ring, 4, 5, 6) == NULL, "not yet written");
    CHECK(XhciSnapRingFind(ring, 4, 5, 0) == NULL, "0 is never an entry");
    total = 0xFFFFFFFFUL;
    e = XhciSnapRingNext(ring, 4, 5, &total);
    CHECK_EQ(total, 1, "the total wraps past 0");
    CHECK_EQ(e[0], 1, "an entry is never numbered 0");

    for (i = 0; i < XHCI_SNAPSHOT_REC_WORDS; i++) {
        rec[i] = 0;
    }
    XhciSnapRecCommand(rec, 15, 1);
    CHECK_EQ(rec[XHCI_SNAPSHOT_REC_COMMANDS], 1, "one command");
    CHECK_EQ(rec[XHCI_SNAPSHOT_REC_CMD01], 0x0F01, "Stop Endpoint, OK");
    XhciSnapRecCommand(rec, 14, 19);
    CHECK_EQ(rec[XHCI_SNAPSHOT_REC_CMD01], 0x0E130F01UL,
             "Reset Endpoint, Context State Error, in the high half");
    XhciSnapRecCommand(rec, 16, 0);
    XhciSnapRecCommand(rec, 0x7F, 0x1FF);
    CHECK_EQ(rec[XHCI_SNAPSHOT_REC_CMD23], 0x3FFF1000UL,
             "never completed (0); the type and code held to their fields");
    XhciSnapRecCommand(rec, 12, 1);
    CHECK_EQ(rec[XHCI_SNAPSHOT_REC_COMMANDS], 5, "five counted");
    CHECK_EQ(rec[XHCI_SNAPSHOT_REC_CMD23], 0x3FFF1000UL, "the fifth not kept");
    CHECK_EQ(rec[XHCI_SNAPSHOT_REC_DETAIL], 0, "nothing past them");
    rec[XHCI_SNAPSHOT_REC_COMMANDS] = 0xFFFFFFFFUL;
    XhciSnapRecCommand(rec, 12, 1);
    CHECK_EQ(rec[XHCI_SNAPSHOT_REC_COMMANDS], 0xFFFFFFFFUL,
             "the count does not wrap");
}

/* A backstop look's class. */
static void test_irq_class(void)
{
    CHECK_EQ(XhciSnapIrqClass(0, 1, XHCI_IMAN_IP), XHCI_SNAPSHOT_BS_SKIPPED,
             "not admitted");
    CHECK_EQ(XhciSnapIrqClass(1, 0, XHCI_IMAN_IP), XHCI_SNAPSHOT_BS_IDLE,
             "nothing pending, whatever IMAN says");
    CHECK_EQ(XhciSnapIrqClass(1, 1, XHCI_IMAN_IP | XHCI_IMAN_IE),
             XHCI_SNAPSHOT_BS_IP, "pending with IP set");
    CHECK_EQ(XhciSnapIrqClass(1, 1, XHCI_IMAN_IE), XHCI_SNAPSHOT_BS_NO_IP,
             "pending with IP clear");
    CHECK_EQ(XHCI_SNAPSHOT_BS_NO_IP, 3, "four classes, one counter each");
}

/*
 * The pipe records' order, as the door takes it (hcd_door.c,
 * hcdDoorDiagBuild): a model of 40 pipes over eight slots, the records
 * taken one scan at a time - each the first ranked after the last one
 * taken - must come out outstanding first, the oldest first, then flagged,
 * then idle, each pipe once; and the outstanding ones that do not fit are
 * the youngest.
 */
static void test_pipe_order(void)
{
    ULONG cls[40];
    ULONG tick[40];
    ULONG id[40];
    ULONG taken[24];
    ULONG n;
    ULONG i;
    ULONG k;
    ULONG best;
    ULONG found;
    ULONG bad;
    ULONG outstanding;

    CHECK_EQ(XhciSnapPipeClass(1, 1), XHCI_SNAPSHOT_PC_OUTSTANDING,
             "outstanding beats flagged");
    CHECK_EQ(XhciSnapPipeClass(0, 1), XHCI_SNAPSHOT_PC_FLAGGED, "flagged");
    CHECK_EQ(XhciSnapPipeClass(0, 0), XHCI_SNAPSHOT_PC_IDLE, "idle");
    CHECK(XhciSnapPipeBefore(0, 900, 99, 1, 5, 1), "a class first");
    CHECK(XhciSnapPipeBefore(0, 5, 99, 0, 900, 1), "then the older claim");
    CHECK(!XhciSnapPipeBefore(1, 5, 99, 1, 900, 1),
          "a flagged pipe's tick is no order");
    CHECK(XhciSnapPipeBefore(1, 5, 1, 1, 900, 99), "then the id");
    CHECK(XhciSnapPipeBefore(0, 0xFFFFFFF0UL, 9, 0, 5, 1),
          "a claim before the clock wrapped is older");
    CHECK(!XhciSnapPipeBefore(2, 0, 7, 2, 0, 7), "never before itself");

    /* 30 outstanding (ticks falling with the id, so the oldest is last in
     * slot order), five flagged, five idle; flagged and idle interleaved
     * in slot order before them. */
    for (i = 0; i < 40; i++) {
        id[i] = ((i / 5 + 1) << 5) | (i % 5 + 1);
        if (i < 10) {
            cls[i] = (i % 2) ? XHCI_SNAPSHOT_PC_FLAGGED
                             : XHCI_SNAPSHOT_PC_IDLE;
            tick[i] = 0;
        } else {
            cls[i] = XHCI_SNAPSHOT_PC_OUTSTANDING;
            tick[i] = 1000 - i;
        }
    }
    n = 0;
    outstanding = 30;
    while (n < 24) {
        found = 0;
        best = 0;
        for (i = 0; i < 40; i++) {
            k = n - 1;
            if (n != 0 && !XhciSnapPipeBefore(cls[taken[k]], tick[taken[k]],
                                              id[taken[k]], cls[i], tick[i],
                                              id[i])) {
                continue;
            }
            if (!found || XhciSnapPipeBefore(cls[i], tick[i], id[i],
                                             cls[best], tick[best],
                                             id[best])) {
                found = 1;
                best = i;
            }
        }
        if (!found) {
            break;
        }
        taken[n++] = best;
        outstanding -= (cls[best] == XHCI_SNAPSHOT_PC_OUTSTANDING) ? 1UL : 0UL;
    }
    CHECK_EQ(n, 24, "the records filled");
    bad = 0;
    for (i = 0; i < 24; i++) {
        bad += (cls[taken[i]] != XHCI_SNAPSHOT_PC_OUTSTANDING) ? 1UL : 0UL;
        bad += (taken[i] != 39 - i) ? 1UL : 0UL;
    }
    CHECK_EQ(bad, 0, "outstanding only, the oldest first");
    CHECK_EQ(outstanding, 6, "six outstanding pipes did not fit");

    bad = 0;
    for (i = 0; i < 40; i++) {
        for (k = 0; k < 40; k++) {
            if (i != k && XhciSnapPipeBefore(cls[i], tick[i], id[i], cls[k],
                                             tick[k], id[k]) ==
                              XhciSnapPipeBefore(cls[k], tick[k], id[k],
                                                 cls[i], tick[i], id[i])) {
                bad++;
            }
        }
    }
    CHECK_EQ(bad, 0, "a total order: of two pipes exactly one ranks first");
}

/* A thread control transfer's outcome as the timeline records it. */
static void test_clear_of(void)
{
    CHECK_EQ(XhciSnapClearOf(0), XHCI_SNAPSHOT_CLEAR_DONE, "DONE: taken");
    CHECK_EQ(XhciSnapClearOf(1), XHCI_SNAPSHOT_CLEAR_STALLED,
             "STALLED: the device stalled it");
    CHECK_EQ(XhciSnapClearOf(2), XHCI_SNAPSHOT_CLEAR_FAILED, "FAILED");
    CHECK_EQ(XhciSnapClearOf(3), XHCI_SNAPSHOT_CLEAR_NOT_SENT,
             "NOT_SENT: never reached the device");
    CHECK_EQ(XhciSnapClearOf(4), XHCI_SNAPSHOT_CLEAR_ABANDONED,
             "ABANDONED for a cycle");
    CHECK_EQ(XhciSnapClearOf(77), XHCI_SNAPSHOT_CLEAR_NOT_SENT,
             "an unknown outcome is no answer from the device");

    /* A request held for an unreadable controller: only an actual park is
     * "held"; one an earlier abort covered was completed in place. */
    CHECK_EQ(XhciSnapHoldOf(0), XHCI_SNAPSHOT_RD_REFUSED,
             "declined: refused at the next tick");
    CHECK_EQ(XhciSnapHoldOf(1), XHCI_SNAPSHOT_RD_HELD, "parked: held");
    CHECK_EQ(XhciSnapHoldOf(2), XHCI_SNAPSHOT_RD_COVERED,
             "an abort's horizon covered it: completed, not held");
    CHECK(XhciSnapHoldOf(2) != XHCI_SNAPSHOT_RD_HELD,
          "never called held once completed");
    CHECK_EQ(XHCI_SNAPSHOT_RD_COVERED, 4, "covered is its own outcome");
}

/* A stamp is the clock plus one: the clock's first 100 ms are not
 * "never", and an age is the same difference either way. */
static void test_tick(void)
{
    CHECK_EQ(XhciSnapTick(0), 1, "the clock's 0 stamps 1, not never");
    CHECK_EQ(XhciSnapTick(899), 900, "plus one");
    CHECK_EQ(XhciSnapTick(900) - XhciSnapTick(0), 900,
             "ages unchanged by the offset");
    CHECK_EQ(XhciSnapTick(0xFFFFFFFEUL), 0xFFFFFFFFUL, "the last value");
    CHECK_EQ(XhciSnapTick(0xFFFFFFFFUL), 0xFFFFFFFFUL,
             "and never wrapped back to 0");
}

int main(void)
{
    test_pipe_order();
    test_clear_of();
    test_tick();
    test_header();
    test_diag_words();
    test_ms();
    test_oldest();
    test_rings();
    test_irq_class();
    test_tol_words();
    test_walk(0);
    test_walk(1);
    test_walk(18);
    test_walk(XHCI_MAX_ROOT_PORTS);
    test_record_words();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
