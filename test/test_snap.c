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

static void test_header(void)
{
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_VERSION_AT), 1,
             "version 1");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_HEAD_BYTES), 32,
             "an eight-word header");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_PORTS), 18,
             "the E460's eighteen root ports");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_PORT_BYTES), 64,
             "a sixteen-word record");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_PORTS_AT), 32,
             "records right after the header");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_COUNTERS), 63,
             "the whole counter block");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_COUNTERS_AT),
             32 + 18 * 64, "counters after the last record");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_NOTE_BUDGET),
             XHCI_ENUM_NOTE_BUDGET, "the notes' budget");
    CHECK_EQ(XhciSnapHcdHead(18, XHCI_SNAPSHOT_HCD_HEAD_WORDS), 0,
             "past the header reads 0");
    CHECK_EQ(XHCI_SNAPSHOT_HCD_COUNTER_WORDS * sizeof(ULONG),
             sizeof(XHCIHC_COUNTERS), "the counters word for word");
    CHECK_EQ(XhciSnapHcdWords(18), 8 + 18 * 16 + 63, "the E460's image");
    CHECK_EQ(XhciSnapHcdWords(0), 8 + 63,
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
        if (index < 8) {
            expectPart = XHCI_SNAPSHOT_HCD_IN_HEAD;
            expectRecord = 0;
            expectWord = index;
        } else if (index < 8 + ports * 16) {
            expectPart = XHCI_SNAPSHOT_HCD_IN_PORT;
            expectRecord = (index - 8) / 16;
            expectWord = (index - 8) % 16;
        } else if (index < total) {
            expectPart = XHCI_SNAPSHOT_HCD_IN_COUNTERS;
            expectRecord = 0;
            expectWord = index - 8 - ports * 16;
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
        CHECK_EQ(XhciSnapHcdLocate(ports, 8 + ports * 16 - 1, &record,
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

int main(void)
{
    test_header();
    test_walk(0);
    test_walk(1);
    test_walk(18);
    test_walk(XHCI_MAX_ROOT_PORTS);
    test_record_words();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
