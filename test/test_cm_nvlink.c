/*
 * test_cm_nvlink.c - the pure half of the stor98 link-state channel
 * (src/cm/cm_nvlink.c; roadmap-usb4.md tasks 39-A.7 and 40-A.7): the
 * table stor98cm.h defines, written by the entry protocol and read back
 * by a modelled receiver that follows the header's reading rules
 * (generation recorded at "HwFindAdapter", DOWN_QUIESCED applied for that
 * generation or a newer one, DOWN without QUIESCED and older generations
 * ignored, an odd or changed Sequence read again); the two profiles'
 * statement conditions; interface 3's locator bytes and proof query
 * handler; the REMOVE_PENDING request (R9); interface 3's publication
 * order (R3, R4) and the two waits' deadline-first steps (R11, R12 S4).
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
    CHECK_EQ(table.Cookie, 0, "cookie zero until the 9x publication");
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
    CHECK_EQ(CM_NVLINK_PROOF_BYTES, 12, "the proof table is 12 bytes");
    CHECK(strcmp(CM_NVLINK_REC_KEY,
                 "System\\CurrentControlSet\\Services\\stor98") == 0,
          "the locator key");
    CHECK(strcmp(CM_NVLINK_REC_VALUE, "LinkState") == 0, "the locator value");
    CHECK(strcmp(STOR98LS9X_PROOF_KEY, "Stor98LinkState") == 0,
          "the proof key");
    CHECK_EQ(CM_NVLINK_REMOVE_MS, 14000, "T_ack is 14 s (R11 O4)");
    CHECK(CM_NVLINK_ACK_9X_MS >= STOR98LS9X_CM_LINK_WAIT_MS &&
          CM_NVLINK_ACK_9X_MS <= STOR98LS9X_GONE_MS,
          "the 9x link-down wait within R12 S4's bounds");
}

static void test_proof(void)
{
    ULONG cookie;
    ULONG other;
    STOR98LS9X_PROOF_TABLE proof;
    CM_NV_VALCTX vc[3];
    UCHAR out[32];
    ULONG cb;

    cookie = 0x12345678UL;
    other = 0;
    proof.TableLinear = 0xC1341200UL;
    proof.TableSize = sizeof(STOR98LS_TABLE);
    proof.Cookie = cookie;
    memset(vc, 0, sizeof(vc));
    vc[0].value_context = &cookie;
    vc[1].value_context = &proof;
    vc[2].value_context = &other;
    cb = 0;
    CHECK_EQ(CmNvlinkProofQuery(vc, 2, NULL, &cb, &cookie, &proof), 0,
             "a length query");
    CHECK_EQ(cb, 16, "4 + 12 bytes");
    CHECK_EQ(vc[0].valuelen, 4, "BootCookie's length");
    CHECK_EQ(vc[1].valuelen, 12, "TableLinear's length");
    cb = 15;
    CHECK_EQ(CmNvlinkProofQuery(vc, 2, out, &cb, &cookie, &proof),
             CM_NV_ERROR_MORE_DATA, "a short buffer");
    CHECK_EQ(cb, 16, "the length it needs");
    memset(out, 0xEE, sizeof(out));
    cb = sizeof(out);
    CHECK_EQ(CmNvlinkProofQuery(vc, 2, out, &cb, &cookie, &proof), 0,
             "both values");
    CHECK_EQ(cb, 16, "16 bytes written");
    CHECK(vc[0].val_buff_ptr == (PVOID)out &&
          vc[1].val_buff_ptr == (PVOID)(out + 4), "each value's place");
    CHECK(memcmp(out, &cookie, 4) == 0, "the cookie");
    CHECK(memcmp(out + 4, &proof, 12) == 0, "the proof table");
    CHECK_EQ(out[16], 0xEE, "nothing past them");
    cb = sizeof(out);
    CHECK_EQ(CmNvlinkProofQuery(vc + 1, 1, out, &cb, &cookie, &proof), 0,
             "one value");
    CHECK_EQ(cb, 12, "TableLinear alone");
    cb = sizeof(out);
    CHECK_EQ(CmNvlinkProofQuery(vc + 2, 1, out, &cb, &cookie, &proof), 0,
             "an unknown value");
    CHECK_EQ(cb, 0, "no bytes");
    CHECK_EQ(vc[2].valuelen, 0, "length 0");
}

static void test_remove(void)
{
    ULONG i;
    ULONG o;
    ULONG g;

    CmNvlinkTableInit(&table);
    i = CmNvlinkPublishUp(&table, 4, 0, FALSE, &o);
    CHECK(!CmNvlinkRemoveRequest(&table, i, 0), "sequence 0 refused");
    CHECK(!CmNvlinkRemoveRequest(&table, i, 0x01000000UL), "sequence past 24 bits");
    CHECK(CmNvlinkRemoveRequest(&table, i, 7), "request 7");
    CHECK_EQ(table.Entry[i].Flags,
             STOR98LS_IN_USE | STOR98LS_UP | CM_NVLINK_REMOVE_PENDING,
             "UP kept, REMOVE_PENDING set");
    CHECK_EQ(table.Entry[i].RemoveSequence, 7, "the sequence");
    g = table.Entry[i].LinkGeneration;
    CHECK_EQ(CmNvlinkRemoveAnswer(&table, i, 7, g), CM_NVLINK_RM_NONE, "not answered");
    table.Entry[i].RemoveAck = (6UL << 8) | CM_NVLINK_RM_CLEAN;
    CHECK_EQ(CmNvlinkRemoveAnswer(&table, i, 7, g), CM_NVLINK_RM_NONE,
             "an answer to an older request");
    table.Entry[i].RemoveAck = (7UL << 8) | CM_NVLINK_RM_ISOLATED;
    CHECK_EQ(CmNvlinkRemoveAnswer(&table, i, 7, g), CM_NVLINK_RM_ISOLATED, "isolated");
    CHECK_EQ(CmNvlinkRemoveAnswer(&table, i, 7, g + 1), CM_NVLINK_RM_NONE,
             "not on another generation (R9c)");
    table.Entry[i].RemoveAck = (7UL << 8) | 9UL;
    CHECK_EQ(CmNvlinkRemoveAnswer(&table, i, 7, g), CM_NVLINK_RM_FAILED,
             "an unknown outcome is not clean");
    table.Entry[i].RemoveAck = (7UL << 8) | CM_NVLINK_RM_CLEAN;
    CHECK_EQ(CmNvlinkRemoveAnswer(&table, i, 7, g), CM_NVLINK_RM_CLEAN, "clean");
    CHECK_EQ(CM_NVLINK_RM_CLEAN, 1, "CLEAN is 1");
    CHECK_EQ(CM_NVLINK_RM_ISOLATED, 2, "ISOLATED_NOT_CLEAN is 2");
    CHECK_EQ(CM_NVLINK_RM_FAILED, 3, "FAILED is 3");
    CHECK_EQ(CM_NVLINK_REMOVE_PENDING, 8, "REMOVE_PENDING is Flags bit 3");
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
    CHECK_EQ(CmNvlinkRemoveAnswer(NULL, 0, 1, 1), CM_NVLINK_RM_NONE, "no table");
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
/* A fake publisher for CmNvlinkPublish9x: each step appends its letter. */
typedef struct _PUB {
    char Order[8];
    ULONG N;
    BOOLEAN Resident;
    ULONG Open;         /* 0: the proof key exists */
    ULONG Create;
    ULONG Locator;
} PUB;

static void pubMark(PUB *p, char c)
{
    if (p->N < sizeof(p->Order) - 1) {
        p->Order[p->N++] = c;
        p->Order[p->N] = '\0';
    }
}

static BOOLEAN pubResident(PVOID c)
{
    pubMark((PUB *)c, 'R');
    return ((PUB *)c)->Resident;
}

static ULONG pubOpen(PVOID c)
{
    pubMark((PUB *)c, 'O');
    return ((PUB *)c)->Open;
}

static ULONG pubCreate(PVOID c)
{
    pubMark((PUB *)c, 'P');
    return ((PUB *)c)->Create;
}

static ULONG pubLocator(PVOID c)
{
    pubMark((PUB *)c, 'L');
    return ((PUB *)c)->Locator;
}

static ULONG pubRun(PUB *p, BOOLEAN resident, ULONG open, ULONG create,
                    ULONG locator, PULONG error)
{
    CM_NV_PUB9X ops;

    memset(p, 0, sizeof(*p));
    p->Resident = resident;
    p->Open = open;
    p->Create = create;
    p->Locator = locator;
    ops.Resident = pubResident;
    ops.OpenProof = pubOpen;
    ops.CreateProof = pubCreate;
    ops.WriteLocator = pubLocator;
    ops.Context = p;
    return CmNvlinkPublish9x(&ops, error);
}

static void test_publish9x(void)
{
    PUB p;
    ULONG e;

    /* The open of a key that is not there fails (2, file not found). */
    CHECK_EQ(pubRun(&p, TRUE, 2, 0, 0, &e), CM_NVLINK_PUB_OK, "published");
    CHECK(strcmp(p.Order, "ROPL") == 0,
          "residency, open first, proof, then the locator (R3, R4)");
    CHECK_EQ(e, 0, "no error");

    CHECK_EQ(pubRun(&p, TRUE, 0, 0, 0, &e), CM_NVLINK_PUB_PUBLISHER,
             "an existing proof key is another publisher's (R3a)");
    CHECK(strcmp(p.Order, "RO") == 0, "nothing created, no locator");

    CHECK_EQ(pubRun(&p, FALSE, 2, 0, 0, &e), CM_NVLINK_PUB_RESIDENCY,
             "not resident: nothing published (R4)");
    CHECK(strcmp(p.Order, "R") == 0, "not even the open");

    CHECK_EQ(pubRun(&p, TRUE, 2, 5, 0, &e), CM_NVLINK_PUB_PROOF,
             "the dynamic key refused");
    CHECK(strcmp(p.Order, "ROP") == 0, "no locator without the proof");
    CHECK_EQ(e, 5, "the VMM's code");

    CHECK_EQ(pubRun(&p, TRUE, 2, 0, 1013, &e), CM_NVLINK_PUB_LOCATOR,
             "the locator refused");
    CHECK(strcmp(p.Order, "ROPL") == 0, "after the proof");
    CHECK_EQ(e, 1013, "its code");
}

static void test_waits(void)
{
    ULONG i;
    ULONG o;
    ULONG g;

    /* The link-down acknowledgement wait: the deadline before the read. */
    CmNvlinkTableInit(&table);
    i = CmNvlinkPublishUp(&table, 4, 0, TRUE, &o);
    CHECK_EQ(CmNvlinkAckStep(&table, i, TRUE, 1000, 1200, 3000),
             CM_NVLINK_AW_SUPERSEDED, "an UP entry is not waited for");
    CmNvlinkSetDown(&table, i, TRUE);
    CHECK_EQ(CmNvlinkAckStep(&table, i, FALSE, 1000, 1200, 3000),
             CM_NVLINK_AW_NO_RECEIVER, "no receiver: no wait (no proof read)");
    CHECK_EQ(CmNvlinkAckStep(&table, i, TRUE, 1000, 1200, 3000),
             CM_NVLINK_AW_PENDING, "waiting");
    CHECK_EQ(CmNvlinkAckStep(&table, i, TRUE, 1000, 4000, 3000),
             CM_NVLINK_AW_TIMEOUT, "the 3 s spent");
    table.Entry[i].AckGeneration = table.Entry[i].LinkGeneration;
    CHECK_EQ(CmNvlinkAckStep(&table, i, TRUE, 1000, 3999, 3000),
             CM_NVLINK_AW_ACKED, "acknowledged inside the bound");
    CHECK_EQ(CmNvlinkAckStep(&table, i, TRUE, 1000, 4000, 3000),
             CM_NVLINK_AW_LATE,
             "first seen at the deadline: late, never taken (R11, R12 S4)");
    CHECK_EQ(CmNvlinkAckStep(&table, i, TRUE, 0xFFFFFF00UL, 0x100UL, 3000),
             CM_NVLINK_AW_ACKED, "across the clock's wrap");
    table.Entry[i].AckGeneration = table.Entry[i].LinkGeneration - 1;
    CHECK_EQ(CmNvlinkAckStep(&table, i, TRUE, 1000, 1200, 3000),
             CM_NVLINK_AW_PENDING, "an older generation's acknowledgement");
    CHECK_EQ(CmNvlinkAckStep(NULL, 0, TRUE, 0, 0, 3000),
             CM_NVLINK_AW_SUPERSEDED, "no table");

    /* The removal wait: T_ack read before the answer. */
    CmNvlinkTableInit(&table);
    i = CmNvlinkPublishUp(&table, 4, 0, TRUE, &o);
    g = table.Entry[i].LinkGeneration;
    CHECK(CmNvlinkRemoveRequest(&table, i, 3), "request 3");
    CHECK_EQ(CmNvlinkRemoveStep(&table, i, 3, g, 0), CM_NVLINK_RM_NONE,
             "no answer yet");
    CHECK_EQ(CmNvlinkRemoveStep(&table, i, 3, g, CM_NVLINK_REMOVE_MS),
             CM_NVLINK_RM_TIMEOUT, "T_ack spent");
    table.Entry[i].RemoveAck = (3UL << 8) | CM_NVLINK_RM_CLEAN;
    CHECK_EQ(CmNvlinkRemoveStep(&table, i, 3, g, 220), CM_NVLINK_RM_CLEAN,
             "clean at 220 ms");
    CHECK_EQ(CmNvlinkRemoveStep(&table, i, 3, g, CM_NVLINK_REMOVE_MS - 1),
             CM_NVLINK_RM_CLEAN, "clean just inside T_ack");
    CHECK_EQ(CmNvlinkRemoveStep(&table, i, 3, g, CM_NVLINK_REMOVE_MS),
             CM_NVLINK_RM_LATE, "first seen at T_ack: late, not clean (R11)");
    CHECK_EQ(CmNvlinkRemoveStep(&table, i, 3, g + 1, 220), CM_NVLINK_RM_NONE,
             "another generation's: no answer (R9c)");
    CHECK_EQ(CmNvlinkRemoveStep(&table, i, 4, g, 220), CM_NVLINK_RM_NONE,
             "another request's: no answer (R9c)");
    CHECK(CM_NVLINK_RM_TIMEOUT != CM_NVLINK_RM_CLEAN &&
          CM_NVLINK_RM_LATE != CM_NVLINK_RM_CLEAN &&
          CM_NVLINK_RM_LATE <= 0xFF, "the wait's own ends fit the log byte");
    CHECK(CM_NVLINK_RETRY_MS == 2000 && CM_NVLINK_RETRY_FOR_MS == 30000,
          "Remove_SubTree retried every 2 s for 30 s (R11 O6)");
    CHECK(CM_NVLINK_POLL_MS <= 50, "read at least every 50 ms (R10)");
}

int main(void)
{
    test_layout();
    test_cycle();
    test_full();
    test_verdicts();
    test_fresh();
    test_record();
    test_proof();
    test_remove();
    test_publish9x();
    test_waits();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
