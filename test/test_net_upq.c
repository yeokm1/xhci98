/*
 * test_net_upq.c - the deferred upcalls' queues of xhcinet.sys on 98 SE
 * and ME (src/net/net_upq.c; design record 35 section 7.8, task 48.38):
 * one timer asked for until the routine finds every queue empty, none
 * while it runs or after the halt; the completions in order with the
 * reset's after those queued before it; the frame ring in order across
 * its wrap with every byte kept, a frame that does not fit refused and
 * the ring whole again once empty, frames of up to 65550 bytes; the
 * halt's stop and cancel, the queues still delivered by a run after it.
 */

#include <stdio.h>
#include "../src/net/net_upq.h"
#include "test_harness.h"

/* A fake packet: its link slot and an id. */
typedef struct {
    PVOID Next;
    ULONG Id;
} ITEM;

static ITEM items[64];
static UCHAR ring[16384];
static UCHAR big[262144];
static UCHAR frame[70000];

static PVOID *link(PVOID item)
{
    return &((ITEM *)item)->Next;
}

static ULONG idOf(PVOID p)
{
    return ((ITEM *)p)->Id;
}

static void init(PNET_UPQ q, PUCHAR r, ULONG size)
{
    ULONG i;

    for (i = 0; i < 64; i++) {
        items[i].Next = (PVOID)1;
        items[i].Id = i;
    }
    NetUpqInit(q, r, size);
}

/* A frame of `len` bytes whose bytes say its number. */
static const UCHAR *mk(ULONG no, ULONG len)
{
    ULONG i;

    for (i = 0; i < len; i++) {
        frame[i] = (UCHAR)(no * 7 + i);
    }
    return frame;
}

static int same(const UCHAR *p, ULONG no, ULONG len)
{
    ULONG i;

    for (i = 0; i < len; i++) {
        if (p[i] != (UCHAR)(no * 7 + i)) {
            return 0;
        }
    }
    return 1;
}

/* One timer until the routine is done; none while it runs. */
static void testArm(void)
{
    NET_UPQ q;
    PVOID done;
    const UCHAR *f;
    ULONG len;
    ULONG reset;
    ULONG tag;

    init(&q, ring, sizeof(ring));
    CHECK_EQ(NetUpqBusy(&q), 0UL, "idle at the start");
    CHECK_EQ(NetUpqPending(&q), 0UL, "nothing pending");
    CHECK_EQ(NetUpqRx(&q, mk(0, 60), 60, 0), 1UL, "the first frame asks a timer");
    CHECK_EQ(NetUpqRx(&q, mk(1, 1514), 1514, 1), 0UL, "a second does not");
    CHECK_EQ(NetUpqDone(&q, &items[2], link), 0UL, "nor a completion");
    CHECK_EQ(NetUpqReset(&q), 0UL, "nor a reset");
    CHECK_EQ(NetUpqBusy(&q), 1UL, "busy with a timer set");
    CHECK_EQ(NetUpqPending(&q), 1UL, "pending");

    NetUpqEnter(&q);
    CHECK_EQ(q.Runs, 1UL, "one run");
    CHECK_EQ(NetUpqTake(&q, &done, &reset, &f, &len, &tag), 1UL, "work");
    CHECK(done == &items[2] && items[2].Next == NULL, "the completion");
    CHECK_EQ(reset, 1UL, "the reset");
    CHECK(f != NULL && len == 60 && same(f, 0, 60), "the first frame");
    /* Queued while the routine runs: no timer, the same run takes it. */
    CHECK_EQ(NetUpqDone(&q, &items[4], link), 0UL, "no timer while it runs");
    NetUpqRxPop(&q);
    CHECK_EQ(NetUpqTake(&q, &done, &reset, &f, &len, &tag), 1UL, "more work");
    CHECK(done == &items[4], "the late completion");
    CHECK_EQ(reset, 0UL, "the reset once only");
    CHECK(f != NULL && len == 1514 && same(f, 1, 1514), "the second frame");
    NetUpqRxPop(&q);
    CHECK_EQ(NetUpqRx(&q, mk(3, 100), 100, 3), 0UL, "no timer for a late frame");
    CHECK_EQ(NetUpqTake(&q, &done, &reset, &f, &len, &tag), 1UL, "the late frame");
    CHECK(done == NULL && reset == 0 && len == 100 && same(f, 3, 100),
          "the late frame alone");
    NetUpqRxPop(&q);
    CHECK_EQ(NetUpqBusy(&q), 1UL, "still busy until an empty take");
    CHECK_EQ(NetUpqTake(&q, &done, &reset, &f, &len, &tag), 0UL, "empty");
    CHECK(done == NULL && f == NULL && reset == 0, "nothing returned");
    CHECK_EQ(NetUpqBusy(&q), 0UL, "done: no timer, not running");
    CHECK_EQ(NetUpqDone(&q, &items[5], link), 1UL, "after it, a timer again");
}

/* Completions in order through the link; the reset after them. */
static void testOrder(void)
{
    NET_UPQ q;
    PVOID done;
    PVOID p;
    const UCHAR *f;
    ULONG len;
    ULONG reset;
    ULONG tag;
    ULONG i;
    ULONG ok;

    init(&q, ring, sizeof(ring));
    for (i = 10; i < 20; i++) {
        (VOID)NetUpqDone(&q, &items[i], link);
    }
    (VOID)NetUpqReset(&q);
    NetUpqEnter(&q);
    CHECK_EQ(NetUpqTake(&q, &done, &reset, &f, &len, &tag), 1UL, "work");
    ok = 1;
    i = 10;
    for (p = done; p != NULL; p = *link(p)) {
        if (idOf(p) != i) {
            ok = 0;
        }
        i++;
    }
    CHECK(ok && i == 20, "ten completions in the order queued");
    CHECK_EQ(reset, 1UL, "the reset due after them, in the same take");
    CHECK(f == NULL, "no frame");
    CHECK(q.DoneHead == NULL && q.DoneTail == NULL, "the list emptied");
    (VOID)NetUpqDone(&q, &items[20], link);
    CHECK_EQ(NetUpqTake(&q, &done, &reset, &f, &len, &tag), 1UL, "later");
    CHECK(done == &items[20] && *link(done) == NULL && reset == 0,
          "the later completion alone");
}

/* Frames of varied lengths in order across many wraps, every byte kept;
 * the ring never overruns what is held. */
static void testRing(void)
{
    NET_UPQ q;
    PVOID done;
    const UCHAR *f;
    ULONG len;
    ULONG reset;
    ULONG tag;
    ULONG put;
    ULONG got;
    ULONG k;
    ULONG r;
    ULONG lens[7];
    ULONG ok;
    ULONG held;

    lens[0] = 60;
    lens[1] = 1514;
    lens[2] = 61;
    lens[3] = 999;
    lens[4] = 14;
    lens[5] = 4093;
    lens[6] = 1;
    init(&q, ring, sizeof(ring));
    NetUpqEnter(&q);
    put = 0;
    got = 0;
    ok = 1;
    for (k = 0; k < 4000; k++) {
        /* Three in, two out, until it refuses; then drain a little. */
        r = NetUpqRx(&q, mk(put, lens[put % 7]), lens[put % 7], put);
        if (r == 2) {
            held = q.Frames;
            CHECK(held != 0 || lens[put % 7] > sizeof(ring),
                  "a refusal only when something is held");
        } else {
            put++;
        }
        if (k % 3 == 2 || r == 2) {
            if (NetUpqTake(&q, &done, &reset, &f, &len, &tag) && f != NULL) {
                if (len != lens[got % 7] || !same(f, got, len) ||
                    tag != got) {
                    ok = 0;
                }
                NetUpqRxPop(&q);
                got++;
            }
        }
        if (q.Used > q.Size) {
            ok = 0;
        }
    }
    while (NetUpqTake(&q, &done, &reset, &f, &len, &tag)) {
        if (f == NULL) {
            continue;
        }
        if (len != lens[got % 7] || !same(f, got, len)) {
            ok = 0;
        }
        NetUpqRxPop(&q);
        got++;
    }
    CHECK(ok, "every frame in order with its tag, every byte kept, never overrun");
    CHECK(put == got && put > 1000, "every frame queued was read");
    CHECK(q.Used == 0 && q.Head == 0 && q.Tail == 0 && q.Frames == 0,
          "the ring whole again when empty");
    CHECK(q.Refused != 0, "the ring filled at times");

    /* A full ring refuses; once read it takes the frame. */
    init(&q, ring, sizeof(ring));
    for (k = 0; k < 4; k++) {
        CHECK(NetUpqRx(&q, mk(k, 4088), 4088, k) != 2, "four of 4096 fit");
    }
    CHECK(q.Used == 16384 && q.Tail == 0, "the ring exactly full");
    CHECK_EQ(NetUpqRx(&q, mk(9, 4088), 4088, 9), 2UL, "a fifth does not");
    CHECK_EQ(q.Refused, 1UL, "counted");
    CHECK_EQ(NetUpqRx(&q, mk(9, 1), 1, 9), 2UL, "nor a byte");
    CHECK_EQ(NetUpqRx(&q, mk(9, 0), 0, 9), 2UL, "an empty frame refused");
    NetUpqEnter(&q);
    (VOID)NetUpqTake(&q, &done, &reset, &f, &len, &tag);
    NetUpqRxPop(&q);
    CHECK(tag == 0, "the first frame's tag");
    CHECK_EQ(NetUpqRx(&q, mk(10, 4088), 4088, 10), 0UL, "room again: at the start");
    CHECK_EQ(q.Tail, 4096UL, "after the marker-free wrap");
}

/* Frames up to 65550 bytes through a 256 KiB ring, wrapping. */
static void testBig(void)
{
    NET_UPQ q;
    PVOID done;
    const UCHAR *f;
    ULONG len;
    ULONG reset;
    ULONG tag;
    ULONG k;
    ULONG ok;

    init(&q, big, sizeof(big));
    NetUpqEnter(&q);
    ok = 1;
    for (k = 0; k < 20; k++) {
        CHECK(NetUpqRx(&q, mk(k, 65550), 65550, k) != 2, "a 65550-byte frame");
        CHECK(NetUpqRx(&q, mk(k + 100, 1514), 1514, k + 100) != 2, "and a small one");
        if (k >= 2) {
            (VOID)NetUpqTake(&q, &done, &reset, &f, &len, &tag);
            if (len != 65550 || !same(f, k - 2, 65550)) {
                ok = 0;
            }
            NetUpqRxPop(&q);
            (VOID)NetUpqTake(&q, &done, &reset, &f, &len, &tag);
            if (len != 1514 || !same(f, k - 2 + 100, 1514)) {
                ok = 0;
            }
            NetUpqRxPop(&q);
        }
    }
    CHECK(ok, "big and small frames in order across the wraps");
    CHECK_EQ(q.Refused, 0UL, "none refused with two of each held");
}

/* The halt: no timer after the stop; a cancelled timer clears the state;
 * what is queued is still delivered, in order, by a run. */
static void testStop(void)
{
    NET_UPQ q;
    PVOID done;
    const UCHAR *f;
    ULONG len;
    ULONG reset;
    ULONG tag;

    init(&q, ring, sizeof(ring));
    CHECK_EQ(NetUpqRx(&q, mk(0, 64), 64, 0), 1UL, "a timer");
    NetUpqStop(&q);
    CHECK_EQ(NetUpqDone(&q, &items[1], link), 0UL, "none after the stop");
    CHECK_EQ(NetUpqBusy(&q), 1UL, "the set timer still counts");
    NetUpqCancelled(&q);
    CHECK_EQ(NetUpqBusy(&q), 0UL, "cancelled: idle");
    (VOID)NetUpqDone(&q, &items[2], link);
    (VOID)NetUpqReset(&q);
    CHECK_EQ(NetUpqBusy(&q), 0UL, "still no timer after the stop");
    CHECK_EQ(NetUpqPending(&q), 1UL, "still pending");
    NetUpqEnter(&q);
    CHECK_EQ(NetUpqTake(&q, &done, &reset, &f, &len, &tag), 1UL, "a run");
    CHECK(done == &items[1] && *link(done) == &items[2] &&
          items[2].Next == NULL, "both completions in order");
    CHECK_EQ(reset, 1UL, "the reset after them");
    CHECK(f != NULL && len == 64 && same(f, 0, 64), "the frame");
    NetUpqRxPop(&q);
    CHECK_EQ(NetUpqTake(&q, &done, &reset, &f, &len, &tag), 0UL, "empty");
    CHECK_EQ(NetUpqPending(&q), 0UL, "nothing pending");
    CHECK_EQ(NetUpqBusy(&q), 0UL, "idle");
    CHECK(q.Used == 0 && q.Frames == 0, "the ring empty");

    /* A stop while the routine runs: it ends on its empty take. */
    init(&q, ring, sizeof(ring));
    (VOID)NetUpqRx(&q, mk(0, 64), 64, 0);
    NetUpqEnter(&q);
    NetUpqStop(&q);
    CHECK_EQ(NetUpqTake(&q, &done, &reset, &f, &len, &tag), 1UL, "work");
    NetUpqRxPop(&q);
    CHECK_EQ(NetUpqTake(&q, &done, &reset, &f, &len, &tag), 0UL, "empty");
    CHECK_EQ(NetUpqBusy(&q), 0UL, "the routine ended");
}

/* A D3 with completions queued and the routine held off past 2 s: the
 * power request is not answered (no quiescence) for as long as the
 * routine has not run, every completion comes from the routine before
 * it is, the queues then ask for no timer until the D0, and the D0 sets
 * one for what was queued meanwhile. The worker's loop is modelled one
 * millisecond a pass, as the miniport's NdisMSleep(1000) loop. */
static void testPower(void)
{
    NET_UPQ q;
    PVOID done;
    PVOID p;
    const UCHAR *f;
    ULONG len;
    ULONG reset;
    ULONG tag;
    ULONG ms;
    ULONG answered;
    ULONG delivered;
    ULONG i;

    init(&q, ring, sizeof(ring));
    CHECK_EQ(NetUpqDone(&q, &items[0], link), 1UL, "a timer for the first");
    for (i = 1; i < 6; i++) {
        (VOID)NetUpqDone(&q, &items[i], link);
    }
    (VOID)NetUpqRx(&q, mk(1, 100), 100, 1);
    CHECK_EQ(NetUpqResume(&q), 0UL, "the D3's resume: a timer already set");
    answered = 0;
    delivered = 0;
    for (ms = 0; ms < 3000 && !answered; ms++) {
        answered = NetUpqQuiesce(&q);
    }
    CHECK_EQ(answered, 0UL, "not answered in 3 s while the routine is held");
    CHECK_EQ(q.Stopped, 0UL, "the queues not stopped meanwhile");
    /* The routine runs at last. */
    NetUpqEnter(&q);
    CHECK_EQ(NetUpqQuiesce(&q), 0UL, "not while the routine runs");
    while (NetUpqTake(&q, &done, &reset, &f, &len, &tag)) {
        for (p = done; p != NULL; p = *link(p)) {
            if (idOf(p) == delivered) {
                delivered++;
            }
        }
        if (f != NULL) {
            NetUpqRxPop(&q);
        }
        CHECK_EQ(NetUpqQuiesce(&q), 0UL, "not before the routine is done");
    }
    CHECK_EQ(delivered, 6UL, "every completion made by the routine, in order");
    CHECK_EQ(NetUpqQuiesce(&q), 1UL, "then answered");
    CHECK_EQ(q.Stopped, 1UL, "and the queues stopped");
    CHECK_EQ(NetUpqDone(&q, &items[7], link), 0UL, "no timer while asleep");
    CHECK_EQ(NetUpqBusy(&q), 0UL, "nothing running or set");
    CHECK_EQ(NetUpqResume(&q), 1UL, "the D0 sets one for what waited");
    CHECK_EQ(q.Stopped, 0UL, "delivery again");
    CHECK_EQ(NetUpqResume(&q), 0UL, "and not twice");
    init(&q, ring, sizeof(ring));
    CHECK_EQ(NetUpqQuiesce(&q), 1UL, "an idle D3 is answered at once");
    CHECK_EQ(NetUpqResume(&q), 0UL, "and its D0 sets no timer");
}

int main(void)
{
    testArm();
    testOrder();
    testRing();
    testBig();
    testStop();
    testPower();
    printf("test_net_upq: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
