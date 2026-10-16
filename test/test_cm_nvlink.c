/*
 * test_cm_nvlink.c - the pure half of the stor98 link-state channel
 * (src/cm/cm_nvlink.c; roadmap-usb4.md tasks 39-A.7 and 40-A.7): the
 * table stor98cm.h defines, written by the entry protocol and read back
 * by a modelled receiver that follows the header's reading rules
 * (generation recorded at "HwFindAdapter", DOWN_QUIESCED applied for that
 * generation or a newer one, DOWN without QUIESCED and older generations
 * ignored, an odd or changed Sequence read again); the two profiles'
 * statement conditions; where the channel is offered and the wait with
 * no receiver; interface 3's locator bytes; the REMOVE_PENDING request.
 */

#include <stdio.h>
#include <string.h>
#include "../src/cm/tbt_topo.h"
#include "../src/cm/cm_nvlink.h"
#include "test_harness.h"

/* A modelled stor98 instance bound to (bus, slot). */
typedef struct _RX {
    ULONG Bus;
    ULONG Slot;
    ULONG Index;
    ULONG Recorded;
    ULONG Applied;      /* statements applied */
    ULONG Ignored;
    ULONG Done;         /* this instance applied one already (its own state,
                           never the shared AckGeneration) */
} RX;

/* The header's seqlock read: TRUE with a consistent copy. */
static BOOLEAN rxRead(const STOR98LS_TABLE *t, ULONG i, STOR98LS_ENTRY *copy)
{
    ULONG s;

    s = t->Entry[i].Sequence;
    if (s & 1UL) {
        return FALSE;
    }
    memcpy(copy, (const void *)&t->Entry[i], sizeof(*copy));
    return (BOOLEAN)(t->Entry[i].Sequence == s);
}

static void rxFind(RX *rx, const STOR98LS_TABLE *t, ULONG bus, ULONG slot)
{
    STOR98LS_ENTRY e;

    rx->Bus = bus;
    rx->Slot = slot;
    rx->Applied = 0;
    rx->Ignored = 0;
    rx->Done = 0;
    rx->Index = CmNvlinkFind(t, bus, slot);
    rx->Recorded = 0;
    if (rx->Index != CM_NVLINK_NONE && rxRead(t, rx->Index, &e)) {
        rx->Recorded = e.LinkGeneration;
    }
}

/* One poll: applies and acknowledges a statement for its generation. */
static void rxPoll(RX *rx, STOR98LS_TABLE *t)
{
    STOR98LS_ENTRY e;

    if (rx->Index == CM_NVLINK_NONE || !rxRead(t, rx->Index, &e)) {
        return;
    }
    if ((e.Flags & STOR98LS_DOWN_QUIESCED) != 0 &&
        e.LinkGeneration >= rx->Recorded) {
        if (!rx->Done) {
            rx->Applied++;
            rx->Done = 1;
            t->Entry[rx->Index].AckGeneration = e.LinkGeneration;
        }
        return;
    }
    if ((e.Flags & STOR98LS_UP) == 0) {
        rx->Ignored++;
    }
}

static STOR98LS_TABLE table;

static void test_layout(void)
{
    ULONG i;

    CHECK_EQ(sizeof(STOR98LS_ENTRY), 32, "an entry is eight ULONGs");
    CHECK_EQ(sizeof(STOR98LS_TABLE), 16 + 8 * 32, "the table is 272 bytes");
    memset(&table, 0xA5, sizeof(table));
    CmNvlinkTableInit(&table);
    CHECK_EQ(table.Signature, 0x4C383953UL, "signature 'S98L'");
    CHECK(memcmp(&table.Signature, "S98L", 4) == 0, "'S98L' in memory");
    CHECK_EQ(table.Version, 4, "version 4");
    CHECK_EQ(STOR98LS9X_LOCATOR_VERSION, 4, "locator version 4");
    CHECK_EQ(STOR98CM_VERSION, 4, "IOCTL message version 4");
    CHECK(memcmp(STOR98CM_SIGNATURE, "STOR98CM", 8) == 0, "IOCTL signature");
    CHECK_EQ(table.Count, STOR98LS_ENTRIES, "count");
    CHECK_EQ(table.Cookie, 0, "cookie zero");
    for (i = 0; i < STOR98LS_ENTRIES; i++) {
        CHECK_EQ(table.Entry[i].Flags, 0, "every entry unused");
        CHECK_EQ(table.Entry[i].Sequence, 0, "every sequence zero");
    }
    CHECK_EQ(CmNvlinkFind(&table, 4, 0), CM_NVLINK_NONE, "nothing found");
    CHECK_EQ(CmNvlinkFind(NULL, 4, 0), CM_NVLINK_NONE, "no table");
}

static void test_cycle(void)
{
    ULONG i;
    ULONG o;
    ULONG seq;
    RX rx;

    CmNvlinkTableInit(&table);
    i = CmNvlinkPublishUp(&table, 4, 0, FALSE, &o);
    CHECK_EQ(o, CM_NVLINK_UP_NEW, "a fresh entry");
    CHECK_EQ(i, 0, "the first entry");
    CHECK_EQ(table.Entry[0].BusNumber, 4, "bus");
    CHECK_EQ(table.Entry[0].SlotNumber, 0, "slot");
    CHECK_EQ(table.Entry[0].LinkGeneration, 1, "generation 1");
    CHECK_EQ(table.Entry[0].Flags, STOR98LS_IN_USE | STOR98LS_UP, "IN_USE | UP");
    CHECK_EQ(table.Entry[0].Sequence, 2, "one write: odd then even");

    /* stor98 binds after the UP (the obligation's order). */
    rxFind(&rx, &table, 4, 0);
    CHECK_EQ(rx.Index, 0, "the receiver finds its entry");
    CHECK_EQ(rx.Recorded, 1, "and records generation 1");
    rxPoll(&rx, &table);
    CHECK_EQ(rx.Applied, 0, "UP is not a statement");

    i = CmNvlinkPublishUp(&table, 4, 0, FALSE, &o);
    CHECK_EQ(o, CM_NVLINK_UP_ALREADY, "UP again while up");
    CHECK_EQ(table.Entry[0].LinkGeneration, 1, "the generation unchanged");
    CHECK_EQ(table.Entry[0].Sequence, 2, "and nothing written");

    seq = table.Entry[0].Sequence;
    CHECK(CmNvlinkSetDown(&table, 0, TRUE), "DOWN_QUIESCED written");
    CHECK_EQ(table.Entry[0].Flags, STOR98LS_IN_USE | STOR98LS_DOWN_QUIESCED, "flags");
    CHECK_EQ(table.Entry[0].Sequence, seq + 2, "bracketed");
    CHECK(!CmNvlinkAcked(&table, 0), "not yet acknowledged");
    rxPoll(&rx, &table);
    CHECK_EQ(rx.Applied, 1, "the receiver applies it");
    CHECK(CmNvlinkAcked(&table, 0), "and acknowledges it");
    rxPoll(&rx, &table);
    CHECK_EQ(rx.Applied, 1, "once");
    CHECK(!CmNvlinkSetDown(&table, 0, TRUE), "no second DOWN on an entry not UP");

    /* A re-plug: a new generation; a receiver of the old one still
     * applies a statement of the newer generation (the header's rule). */
    i = CmNvlinkPublishUp(&table, 4, 0, FALSE, &o);
    CHECK_EQ(o, CM_NVLINK_UP_AGAIN, "the same entry again");
    CHECK_EQ(table.Entry[0].LinkGeneration, 2, "generation 2");
    CHECK(!CmNvlinkAcked(&table, 0), "an UP entry is not acknowledged");

    /* Withheld: UP leaves, no QUIESCED; the receiver ignores it. */
    rxFind(&rx, &table, 4, 0);
    CHECK(CmNvlinkSetDown(&table, 0, FALSE), "withheld DOWN written");
    CHECK_EQ(table.Entry[0].Flags, STOR98LS_IN_USE, "IN_USE alone");
    rxPoll(&rx, &table);
    CHECK_EQ(rx.Applied, 0, "DOWN without QUIESCED is not evidence");
    CHECK(!CmNvlinkAcked(&table, 0), "nothing to acknowledge");
    i = CmNvlinkPublishUp(&table, 4, 0, FALSE, &o);
    CHECK_EQ(table.Entry[0].LinkGeneration, 3, "the next UP counts again");

    /* An odd sequence is read again later. */
    table.Entry[0].Sequence++;
    CHECK(CmNvlinkSetDown(&table, 0, TRUE), "a write over an odd sequence");
    CHECK_EQ(table.Entry[0].Sequence & 1UL, 0, "ends even");
    CHECK_EQ(CmNvlinkNextUp(&table, 4, 0), CM_NVLINK_NONE, "nothing UP on bus 4");
    (void)i;
}

static void test_full(void)
{
    ULONG i;
    ULONG o;
    ULONG k;

    CmNvlinkTableInit(&table);
    for (k = 0; k < STOR98LS_ENTRIES; k++) {
        i = CmNvlinkPublishUp(&table, 4 + k, 8, TRUE, &o);
        CHECK_EQ(i, k, "entries in order");
    }
    i = CmNvlinkPublishUp(&table, 20, 0, TRUE, &o);
    CHECK_EQ(i, CM_NVLINK_NONE, "a ninth function finds no entry");
    CHECK_EQ(o, CM_NVLINK_UP_FULL, "FULL");
    CHECK_EQ(CmNvlinkNextUp(&table, 7, 0), 3, "NextUp by bus");
    CHECK_EQ(CmNvlinkNextUp(&table, 7, 4), CM_NVLINK_NONE, "from past it");

    CmNvlinkSetDown(&table, 2, TRUE);
    table.Entry[2].AckGeneration = table.Entry[2].LinkGeneration;
    CmNvlinkSetDown(&table, 5, FALSE);
    i = CmNvlinkPublishUp(&table, 20, 0, TRUE, &o);
    CHECK_EQ(o, CM_NVLINK_UP_FULL,
             "an entry once given to a function is never given to another");
    CHECK_EQ(CmNvlinkFind(&table, 6, 8), 2, "the acknowledged entry keeps its key");
    i = CmNvlinkPublishUp(&table, 6, 8, FALSE, &o);
    CHECK_EQ(o, CM_NVLINK_UP_AGAIN, "its own function comes back to it");
    CHECK_EQ(table.Entry[2].LinkGeneration, 2, "with the next generation");
    i = CmNvlinkPublishUp(&table, 300, 0, TRUE, &o);
    CHECK_EQ(o, CM_NVLINK_UP_BAD, "a bus past 255");
    i = CmNvlinkPublishUp(NULL, 1, 0, TRUE, &o);
    CHECK_EQ(o, CM_NVLINK_UP_BAD, "no table");
    CHECK(!CmNvlinkSetDown(&table, STOR98LS_ENTRIES, TRUE), "index past the table");
    CHECK(!CmNvlinkAcked(&table, STOR98LS_ENTRIES), "index past the table");
}

static void test_verdicts(void)
{
    CHECK_EQ(CmNvlinkIcmVerdict(1, XHCI_TBT_SRC_SHPC, TRUE, TRUE, XHCI_TBT_POLL_REACHED,
                                XHCI_TBT_PORT_DOWN),
             CM_NVLINK_SEND, "ICM: one SHPC member read DOWN");
    CHECK_EQ(CmNvlinkIcmVerdict(0, XHCI_TBT_SRC_SHPC, TRUE, TRUE, XHCI_TBT_POLL_REACHED,
                                XHCI_TBT_PORT_DOWN),
             CM_NVLINK_NO_SET, "ICM: no set");
    CHECK_EQ(CmNvlinkIcmVerdict(2, XHCI_TBT_SRC_SHPC, TRUE, TRUE, XHCI_TBT_POLL_REACHED,
                                XHCI_TBT_PORT_DOWN),
             CM_NVLINK_SEVERAL, "ICM: two members");
    CHECK_EQ(CmNvlinkIcmVerdict(1, XHCI_TBT_SRC_SWITCH, TRUE, TRUE, XHCI_TBT_POLL_REACHED,
                                XHCI_TBT_PORT_DOWN),
             CM_NVLINK_HARDWARE, "ICM: a hardware port");
    CHECK_EQ(CmNvlinkIcmVerdict(1, XHCI_TBT_SRC_OVERRIDE, TRUE, TRUE, XHCI_TBT_POLL_REACHED,
                                XHCI_TBT_PORT_DOWN),
             CM_NVLINK_HARDWARE, "ICM: the override is not the emulator");
    CHECK_EQ(CmNvlinkIcmVerdict(1, XHCI_TBT_SRC_SHPC, TRUE, TRUE, XHCI_TBT_POLL_GONE,
                                XHCI_TBT_PORT_GONE),
             CM_NVLINK_NOT_DOWN, "ICM: the bridge gone is not the signal");
    CHECK_EQ(CmNvlinkIcmVerdict(1, XHCI_TBT_SRC_SHPC, TRUE, TRUE, XHCI_TBT_POLL_TIMEOUT,
                                XHCI_TBT_PORT_UP),
             CM_NVLINK_NOT_DOWN, "ICM: timed out");
    CHECK_EQ(CmNvlinkIcmVerdict(1, XHCI_TBT_SRC_SHPC, TRUE, TRUE, XHCI_TBT_POLL_REACHED,
                                XHCI_TBT_PORT_MIXED),
             CM_NVLINK_NOT_DOWN, "ICM: reached on a reading not DOWN");

    CHECK_EQ(CmNvlinkU4Verdict(TRUE, TRUE, XHCI_TBT_SRC_SHPC, TRUE, TRUE,
                               XHCI_TBT_PORT_DOWN, 2000),
             CM_NVLINK_SEND, "USB4: closed, known, DOWN, time left");
    CHECK_EQ(CmNvlinkU4Verdict(FALSE, TRUE, XHCI_TBT_SRC_SHPC, TRUE, TRUE,
                               XHCI_TBT_PORT_DOWN, 2000),
             CM_NVLINK_PATH_OPEN, "USB4: PathClosed not proven");
    CHECK_EQ(CmNvlinkU4Verdict(TRUE, FALSE, XHCI_TBT_SRC_SHPC, TRUE, TRUE,
                               XHCI_TBT_PORT_DOWN, 2000),
             CM_NVLINK_SEVERAL, "USB4: member unknown");
    CHECK_EQ(CmNvlinkU4Verdict(TRUE, TRUE, XHCI_TBT_SRC_ROUTER, TRUE, TRUE,
                               XHCI_TBT_PORT_DOWN, 2000),
             CM_NVLINK_HARDWARE, "USB4: a router-named hardware port");
    CHECK_EQ(CmNvlinkU4Verdict(TRUE, TRUE, XHCI_TBT_SRC_SHPC, TRUE, TRUE,
                               XHCI_TBT_PORT_UP, 2000),
             CM_NVLINK_NOT_DOWN, "USB4: still present");
    CHECK_EQ(CmNvlinkU4Verdict(TRUE, TRUE, XHCI_TBT_SRC_SHPC, TRUE, TRUE,
                               XHCI_TBT_PORT_DOWN, 99),
             CM_NVLINK_LATE, "USB4: under the delivery bound");
    CHECK_EQ(CmNvlinkU4Verdict(TRUE, TRUE, XHCI_TBT_SRC_SHPC, TRUE, TRUE,
                               XHCI_TBT_PORT_DOWN, 100),
             CM_NVLINK_SEND, "USB4: exactly the bound");
    CHECK_EQ(CmNvlinkU4Verdict(TRUE, TRUE, XHCI_TBT_SRC_SHPC, TRUE, TRUE,
                               XHCI_TBT_PORT_DOWN, 0),
             CM_NVLINK_LATE, "USB4: no anchor");

    CHECK_EQ(CmNvlinkIcmVerdict(1, XHCI_TBT_SRC_SHPC, FALSE, TRUE,
                                XHCI_TBT_POLL_REACHED, XHCI_TBT_PORT_DOWN),
             CM_NVLINK_HARDWARE, "ICM: an SHPC bridge that is not the emulator's");
    CHECK_EQ(CmNvlinkIcmVerdict(1, XHCI_TBT_SRC_SHPC, TRUE, FALSE,
                                XHCI_TBT_POLL_REACHED, XHCI_TBT_PORT_DOWN),
             CM_NVLINK_UNQUALIFIED, "ICM: an operating system not qualified");
    CHECK_EQ(CmNvlinkU4Verdict(TRUE, TRUE, XHCI_TBT_SRC_SHPC, FALSE, TRUE,
                               XHCI_TBT_PORT_DOWN, 2000),
             CM_NVLINK_HARDWARE, "USB4: not the emulator's bridge");
    CHECK_EQ(CmNvlinkU4Verdict(TRUE, TRUE, XHCI_TBT_SRC_SHPC, TRUE, FALSE,
                               XHCI_TBT_PORT_DOWN, 2000),
             CM_NVLINK_UNQUALIFIED, "USB4: an operating system not qualified");

    CHECK_EQ(CmNvlinkAckBoundMs(TRUE), 5000, "NT: the 5 s gone deadline");
    CHECK_EQ(CmNvlinkAckBoundMs(FALSE), 3000, "9x: 3 s");
    CHECK(CmNvlinkAckBoundMs(FALSE) >= STOR98LS9X_CM_LINK_WAIT_MS &&
          CmNvlinkAckBoundMs(FALSE) <= STOR98LS9X_GONE_MS,
          "9x: within interface 3's R12 S4 bounds");

    /* Where the channel is offered, and the wait for an acknowledgement. */
    CHECK(CmNvlinkOffered(TRUE), "NT: offered (interface 2)");
    CHECK(!CmNvlinkOffered(FALSE), "9x: not offered by this build");
    CHECK_EQ(CmNvlinkAwaitBoundMs(FALSE, CmNvlinkOffered(FALSE)), 0,
             "9x: a link-down with no channel waits for nothing");
    CHECK_EQ(CmNvlinkAwaitBoundMs(TRUE, TRUE), 5000,
             "NT with a receiver: the 5 s bound");
    CHECK_EQ(CmNvlinkAwaitBoundMs(TRUE, FALSE), 0,
             "NT with no stor98 loaded: no wait");

    CHECK_EQ(CmNvlinkLeft(1000, 1000, 5000), 5000, "nothing spent");
    CHECK_EQ(CmNvlinkLeft(1000, 3000, 5000), 3000, "2 s spent");
    CHECK_EQ(CmNvlinkLeft(1000, 6000, 5000), 0, "spent exactly");
    CHECK_EQ(CmNvlinkLeft(1000, 9000, 5000), 0, "overspent");
    CHECK_EQ(CmNvlinkLeft(0xFFFFFF00UL, 0x100UL, 5000), 5000 - 0x200,
             "across the clock's wrap");
}

static void test_record(void)
{
    UCHAR r[CM_NVLINK_RECORD_BYTES];
    static const UCHAR want[CM_NVLINK_RECORD_BYTES] = {
        0x53, 0x39, 0x38, 0x4C, 0x04, 0x00, 0x00, 0x00,
        0x00, 0x12, 0x34, 0xC1, 0x10, 0x01, 0x00, 0x00,
        0xEF, 0xBE, 0xAD, 0xDE
    };

    CmNvlinkRecordEncode(r, 0xC1341200UL, sizeof(STOR98LS_TABLE),
                         0xDEADBEEFUL);
    CHECK(memcmp(r, want, sizeof(want)) == 0, "the record's bytes");
    CHECK(memcmp(r, "S98L", 4) == 0, "starts with the ASCII signature");
    CHECK_EQ(CM_NVLINK_RECORD_BYTES, 20, "the locator is 20 bytes");
    CHECK(strcmp(CM_NVLINK_REC_VALUE, "LinkState") == 0, "the locator value");
}

static void test_remove(void)
{
    ULONG i;
    ULONG o;

    CmNvlinkTableInit(&table);
    i = CmNvlinkPublishUp(&table, 4, 0, FALSE, &o);
    CHECK(!CmNvlinkRemoveRequest(&table, i, 0), "sequence 0 refused");
    CHECK(!CmNvlinkRemoveRequest(&table, i, 0x01000000UL), "sequence past 24 bits");
    CHECK(CmNvlinkRemoveRequest(&table, i, 7), "request 7");
    CHECK_EQ(table.Entry[i].Flags,
             STOR98LS_IN_USE | STOR98LS_UP | CM_NVLINK_REMOVE_PENDING,
             "UP kept, REMOVE_PENDING set");
    CHECK_EQ(table.Entry[i].RemoveSequence, 7, "the sequence");
    CHECK_EQ(CmNvlinkRemoveAnswer(&table, i, 7), CM_NVLINK_RM_NONE, "not answered");
    table.Entry[i].RemoveAck = (6UL << 8) | CM_NVLINK_RM_CLEAN;
    CHECK_EQ(CmNvlinkRemoveAnswer(&table, i, 7), CM_NVLINK_RM_NONE,
             "an answer to an older request");
    table.Entry[i].RemoveAck = (7UL << 8) | CM_NVLINK_RM_ISOLATED;
    CHECK_EQ(CmNvlinkRemoveAnswer(&table, i, 7), CM_NVLINK_RM_ISOLATED, "isolated");
    table.Entry[i].RemoveAck = (7UL << 8) | 9UL;
    CHECK_EQ(CmNvlinkRemoveAnswer(&table, i, 7), CM_NVLINK_RM_FAILED,
             "an unknown outcome is not clean");
    table.Entry[i].RemoveAck = (7UL << 8) | CM_NVLINK_RM_CLEAN;
    CHECK_EQ(CmNvlinkRemoveAnswer(&table, i, 7), CM_NVLINK_RM_CLEAN, "clean");
    CmNvlinkRemoveCancel(&table, i);
    CHECK_EQ(table.Entry[i].Flags, STOR98LS_IN_USE | STOR98LS_UP, "cancelled");
    CHECK(CmNvlinkRemoveRequest(&table, i, 8), "request 8");
    CHECK(CmNvlinkSetDown(&table, i, TRUE), "a tunnel down over a pending request");
    CHECK_EQ(table.Entry[i].Flags, STOR98LS_IN_USE | STOR98LS_DOWN_QUIESCED,
             "replaces it");
    CHECK(!CmNvlinkRemoveRequest(&table, i, 9), "no request on an entry not UP");
    i = CmNvlinkPublishUp(&table, 4, 0, FALSE, &o);
    CHECK(CmNvlinkRemoveRequest(&table, i, 10), "request 10");
    CHECK_EQ(CmNvlinkPublishUp(&table, 5, 0, TRUE, &o), 1, "another function");
    CHECK_EQ(CmNvlinkRemoveAnswer(NULL, 0, 1), CM_NVLINK_RM_NONE, "no table");
}

static void test_fresh(void)
{
    ULONG i;
    ULONG o;
    RX rx;

    CmNvlinkTableInit(&table);
    i = CmNvlinkPublishUp(&table, 4, 0, TRUE, &o);
    rxFind(&rx, &table, 4, 0);
    i = CmNvlinkPublishUp(&table, 4, 0, TRUE, &o);
    CHECK_EQ(o, CM_NVLINK_UP_AGAIN, "a fresh exposure over UP: a new connection");
    CHECK_EQ(table.Entry[i].LinkGeneration, 2, "generation 2");
    CmNvlinkSetDown(&table, i, TRUE);
    rxPoll(&rx, &table);
    CHECK_EQ(rx.Applied, 1, "the older instance still applies the newer DOWN");

    /* A statement left in place, acknowledged: a new instance binding to
     * it applies it at once (the reader rule has no other guard) - the
     * hazard the retirement exists for. */
    rxFind(&rx, &table, 4, 0);
    rxPoll(&rx, &table);
    CHECK_EQ(rx.Applied, 1, "an unretired old statement reaches a new instance");

    /* Retired before the exposure: the binding instance stays off it. */
    CHECK_EQ(CmNvlinkRetire(&table, 4), 1, "one entry retired");
    CHECK_EQ(table.Entry[i].Flags, STOR98LS_IN_USE, "IN_USE alone");
    CHECK_EQ(table.Entry[i].LinkGeneration, 3, "a new generation with no DOWN");
    rxFind(&rx, &table, 4, 0);
    CHECK_EQ(rx.Recorded, 3, "the new instance records it");
    rxPoll(&rx, &table);
    CHECK_EQ(rx.Applied, 0, "and applies nothing");
    CHECK_EQ(CmNvlinkRetire(&table, 5), 0, "another bus: nothing");
    CHECK_EQ(CmNvlinkRetire(&table, 4), 0, "an entry already retired: nothing");
    CHECK(!CmNvlinkRetireEntry(&table, i), "nor by index");
    CHECK(!CmNvlinkRetireEntry(&table, STOR98LS_ENTRIES), "an index past the table");
    i = CmNvlinkPublishUp(&table, 4, 0, FALSE, &o);
    CHECK_EQ(CmNvlinkRetire(&table, 4), 0, "an UP entry is not retired");
    CHECK_EQ(CmNvlinkRetire(NULL, 4), 0, "no table");
}
int main(void)
{
    test_layout();
    test_cycle();
    test_full();
    test_verdicts();
    test_fresh();
    test_record();
    test_remove();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
